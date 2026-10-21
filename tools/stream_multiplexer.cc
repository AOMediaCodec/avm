/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

// multi-streams muxing test
// ===========================
//
// This is an example demonstrating how to mux multiple sub-bitstreams.
//

#include "tools/stream_mux.h"

// When 1, rewrite TD with 2-byte header (extension_flag=1, xlayer_id=GLOBAL).
// When 0, preserve the original 1-byte TD header from the input stream.
#define REWRITE_TD_WITH_GLOBAL_XLAYER 0

// This function writes a multi-stream decoder operation OBU,
// when multiple sub-streams are merged into one bitstream.
static int write_multi_stream_decoder_operation_obu(uint8_t *const dst,
                                                    int num_streams,
                                                    int *stream_ids,
                                                    int *stream_buffer_units) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  int obu_type = OBU_MULTI_STREAM_DECODER_OPERATION;
  uint32_t size = 0;

  avm_wb_write_literal(&wb, 1, 1);              // obu_header_extension_flag
  avm_wb_write_literal(&wb, (int)obu_type, 5);  // obu_type
  avm_wb_write_literal(&wb, 0, 2);              // obu_tlayer
  avm_wb_write_literal(&wb, 0, 3);              // obu_mlayer
  avm_wb_write_literal(&wb, 31, 5);             // obu_xlayer

  avm_wb_write_literal(&wb, num_streams - 2, 3);  // signal number of streams
  avm_wb_write_literal(&wb, 2, PROFILE_BITS);     // multistream_profile_idc
  avm_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                       LEVEL_BITS);  // multistream_level_idx
  avm_wb_write_bit(&wb, 0);          // multistream_tier_idx

  int multistream_even_allocation_flag = 1;
  int multistream_large_picture_idc = 0;
  int max_buffer_unit = stream_buffer_units[0];
  for (int i = 0; i < num_streams; i++) {
    if (stream_buffer_units[i] != max_buffer_unit) {
      multistream_even_allocation_flag = 0;
      if (stream_buffer_units[i] > max_buffer_unit) {
        multistream_large_picture_idc = i;
        max_buffer_unit = stream_buffer_units[i];
      }
    }
  }

  avm_wb_write_bit(
      &wb,
      multistream_even_allocation_flag);  // multistream_even_allocation_flag
  if (!multistream_even_allocation_flag)
    avm_wb_write_literal(&wb, multistream_large_picture_idc,
                         3);  // multistream_large_picture_idc

  for (int i = 0; i < num_streams; i++) {
    avm_wb_write_literal(&wb, stream_ids[i], XLAYER_BITS);  // signal stream IDs
    avm_wb_write_literal(&wb, 0, PROFILE_BITS);  // substream profile_idc
    avm_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                         LEVEL_BITS);  // substream level_idx
    avm_wb_write_bit(&wb, 0);          // substream tier_idx
  }

  avm_wb_write_bit(&wb, 1);  // msdo_doh_constraint_flag

  if ((wb.bit_offset % CHAR_BIT == 0)) {
    avm_wb_write_literal(&wb, 0x80, 8);
  } else {
    // assumes that the other bits are already 0s
    avm_wb_write_bit(&wb, 1);
  }

  size = avm_wb_bytes_written(&wb);
  return size;
}

static void write_obu_header_with_stream_id(uint8_t *const dst,
                                            ObuHeader *obu_header,
                                            int stream_id) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  avm_wb_write_bit(&wb, 1);                                 // extention flag
  avm_wb_write_literal(&wb, obu_header->type, 5);           // obu_type
  avm_wb_write_literal(&wb, obu_header->obu_tlayer_id, 2);  // obu_temporal
  avm_wb_write_literal(&wb, obu_header->obu_mlayer_id, 3);  // obu_mlayer
  avm_wb_write_literal(&wb, stream_id, 5);                  // obu_xlayer
}

// Maximum number of sub-streams representable in MSDO (3-bit num_streams - 2).
constexpr int kMaxMsdoStreams = 9;
// Maximum index representable in 3-bit multistream_large_picture_idc.
constexpr int kMaxLargePictureIdc = 7;
// Maximum sum of stream buffer units across all streams.
constexpr int kMaxStreamBufferUnits = 9;
// Maximum encoded byte length of an AV2 LEB128 size field.
constexpr size_t kMaxLeb128Size = 8;

static bool TuHasKeyFrame(const uint8_t *data, int length) {
  int pos = 0;
  while (pos < length) {
    size_t lfs = 0;
    uint64_t obu_sz = 0;
    if (avm_uleb_decode(data + pos, length - pos, &obu_sz, &lfs) != 0) break;
    if (obu_sz == 0 || lfs + obu_sz > static_cast<uint64_t>(length - pos)) {
      break;
    }
    ObuHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!ParseAV2ObuHeader(*(data + pos + lfs), &hdr)) break;
    if (hdr.type == OBU_CLOSED_LOOP_KEY || hdr.type == OBU_OPEN_LOOP_KEY ||
        hdr.type == OBU_RAS_FRAME)
      return true;
    pos += static_cast<int>(obu_sz) + static_cast<int>(lfs);
  }
  return false;
}

// Extract non-TD OBUs from a single input TU, rewriting headers with stream_id.
// Used by the combined TU path.
static std::vector<uint8_t> WriteStreamOBUs(const uint8_t *data, int length,
                                            int stream_id) {
  std::vector<uint8_t> obus;
  const int kObuHeaderSizeBytes = 1;
  int consumed = 0;
  ObuHeader obu_header;

  while (consumed < length) {
    const int remaining = length - consumed;
    size_t length_field_size = 0;
    uint64_t obu_total_size = 0;

    memset(&obu_header, 0, sizeof(obu_header));
    if (avm_uleb_decode(data + consumed, remaining, &obu_total_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n", consumed);
      break;
    }
    if (obu_total_size == 0 ||
        length_field_size + obu_total_size > static_cast<uint64_t>(remaining)) {
      fprintf(stderr, "OBU parsing failed: invalid OBU size at offset %d.\n",
              consumed);
      break;
    }

    const uint8_t obu_header_byte = *(data + consumed + length_field_size);
    if (!ParseAV2ObuHeader(obu_header_byte, &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              consumed + static_cast<int>(length_field_size));
      break;
    }

    int obu_header_size = 1;
    if (obu_header.obu_header_extension_flag) {
      if (obu_total_size < 2) {
        fprintf(stderr, "OBU header extension truncated at offset %d.\n",
                consumed + static_cast<int>(length_field_size));
        break;
      }
      const uint8_t obu_header_ext_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes);
      ParseAV2ObuHeaderExtension(obu_header_ext_byte, &obu_header);
      ++obu_header_size;
    }

    consumed +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);

    // Skip TD OBUs — the combined TU path writes its own single TD.
    if (obu_header.type == OBU_TEMPORAL_DELIMITER) continue;

    // Rewrite OBU header with stream_id as xlayer_id
    uint8_t obu_size_data[kMaxLeb128Size] = {};
    size_t coded_obu_size = 0;
    avm_uleb_encode(obu_total_size - obu_header_size + 2, sizeof(obu_size_data),
                    obu_size_data, &coded_obu_size);
    obus.insert(obus.end(), obu_size_data, obu_size_data + coded_obu_size);

    uint8_t obu_header_data[2] = {};
    write_obu_header_with_stream_id(obu_header_data, &obu_header, stream_id);
    obus.insert(obus.end(), obu_header_data, obu_header_data + 2);

    // Append OBU payload (after original header)
    const uint8_t *payload_start =
        data + (consumed - static_cast<int>(obu_total_size)) + obu_header_size;
    int payload_size = static_cast<int>(obu_total_size) - obu_header_size;
    obus.insert(obus.end(), payload_start, payload_start + payload_size);
  }

  return obus;
}

// Write a single combined TU containing OBUs from all streams.
// sorted_indices provides stream indices in ascending stream_id order.
static std::vector<uint8_t> WriteCombinedTU(
    std::vector<std::vector<uint8_t>> &per_stream_obus,
    const int *sorted_indices, int num_streams, int *stream_ids,
    int *stream_buffer_units, bool any_key_frame, bool redundant_msdo) {
  std::vector<uint8_t> tu;

  // Write TD OBU
#if REWRITE_TD_WITH_GLOBAL_XLAYER
  uint8_t td_obu_size = 2;
  tu.push_back(td_obu_size);
  tu.push_back(0x88);
  tu.push_back(0x1F);
#else
  // Write a minimal 1-byte TD: size=1, header byte for TD type
  uint8_t td_header = (OBU_TEMPORAL_DELIMITER << 2);  // ext=0, tlayer=0
  tu.push_back(1);                                    // leb128 size = 1
  tu.push_back(td_header);
#endif

  // Insert MSDO if any stream has a key frame or redundant mode
  if (redundant_msdo || any_key_frame) {
    std::vector<uint8_t> multi_stream_obu(num_streams * 2 + 16, 0);
    int multi_header_obu_size = write_multi_stream_decoder_operation_obu(
        multi_stream_obu.data(), num_streams, stream_ids, stream_buffer_units);
    uint8_t msdo_size_data[kMaxLeb128Size] = {};
    size_t msdo_length_field_size = 0;
    avm_uleb_encode(multi_header_obu_size, sizeof(msdo_size_data),
                    msdo_size_data, &msdo_length_field_size);
    tu.insert(tu.end(), msdo_size_data,
              msdo_size_data + msdo_length_field_size);
    tu.insert(tu.end(), multi_stream_obu.begin(),
              multi_stream_obu.begin() + multi_header_obu_size);
  }

  // Append OBUs from each stream in ascending stream_id order
  for (int j = 0; j < num_streams; ++j) {
    const auto &obus = per_stream_obus[sorted_indices[j]];
    tu.insert(tu.end(), obus.begin(), obus.end());
  }

  return tu;
}

// This function read a temporal unit from a sub-stream,
// writes the temporal unit with updates of xlayer_id,
// and multiplex the temporal units into a merged bitstream.
std::vector<uint8_t> WriteTU(const uint8_t *data, int length,
                             int *obu_overhead_bytes, int seg_idx,
                             int num_streams, int *stream_ids,
                             int *stream_buffer_units, bool redundant_msdo) {
  std::vector<uint8_t> tu_obus;
  const uint8_t *data_ptr = data;
  const int kObuHeaderSizeBytes = 1;
  const int kMinimumBytesRequired = 1 + kObuHeaderSizeBytes;
  int consumed = 0;
  int obu_overhead = 0;
  ObuHeader obu_header;

  const bool tu_has_key_frame = TuHasKeyFrame(data, length);

  while (consumed < length) {
    const int remaining = length - consumed;
    if (remaining < kMinimumBytesRequired) {
      fprintf(stderr,
              "OBU parse error. Did not consume all data, %d bytes remain.\n",
              remaining);
      break;
    }

    int obu_header_size = 0;
    size_t length_field_size = 0;
    uint64_t obu_total_size = 0;

    memset(&obu_header, 0, sizeof(obu_header));

    if (avm_uleb_decode(data + consumed, remaining, &obu_total_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n", consumed);
      break;
    }
    if (obu_total_size == 0 ||
        length_field_size + obu_total_size > static_cast<uint64_t>(remaining)) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
      break;
    }

    const uint8_t obu_header_byte = *(data + consumed + length_field_size);
    if (!ParseAV2ObuHeader(obu_header_byte, &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              consumed + static_cast<int>(length_field_size));
      break;
    }
    ++obu_overhead;
    ++obu_header_size;

    if (obu_header.obu_header_extension_flag) {
      if (obu_total_size < 2) {
        fprintf(stderr, "OBU header extension truncated at offset %d.\n",
                static_cast<int>(consumed + length_field_size +
                                 kObuHeaderSizeBytes));
        break;
      }
      const uint8_t obu_header_ext_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes);
      if (!ParseAV2ObuHeaderExtension(obu_header_ext_byte, &obu_header)) {
        fprintf(stderr, "OBU header extension parsing failed at offset %d.\n",
                static_cast<int>(consumed + length_field_size +
                                 kObuHeaderSizeBytes));
        break;
      }

      ++obu_overhead;
      ++obu_header_size;
    }

    consumed +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);

#if PRINT_TU_INFO
    PrintObuHeader(&obu_header);
#endif  // PRINT_TU_INFO

    std::vector<uint8_t> obu_tmp(data_ptr + length_field_size,
                                 data_ptr + obu_total_size + length_field_size);

    // Write the temporal delimiter first, before any MSDO insertion,
    // since the MSDO must come after the TD in the OBU order.
    if (obu_header.type == OBU_TEMPORAL_DELIMITER) {
#if REWRITE_TD_WITH_GLOBAL_XLAYER
      // Rewrite TD with 2-byte header: extension_flag=1, xlayer_id=31(GLOBAL)
      // Byte 0: ext=1 | type=00010(TD) | tlayer=00 = 0x88
      // Byte 1: mlayer=000 | xlayer=11111 = 0x1F
      uint8_t td_obu_size = 2;  // leb128 encoding of OBU size (2 header bytes)
      tu_obus.push_back(td_obu_size);
      tu_obus.push_back(0x88);
      tu_obus.push_back(0x1F);
#else
      uint8_t obu_size_data[kMaxLeb128Size] = {};
      size_t coded_obu_size = 0;
      avm_uleb_encode(obu_total_size, sizeof(obu_size_data), obu_size_data,
                      &coded_obu_size);
      tu_obus.insert(tu_obus.end(), obu_size_data,
                     obu_size_data + coded_obu_size);
      tu_obus.insert(tu_obus.end(), obu_tmp.begin(), obu_tmp.end());
#endif

      // Insert MSDO immediately after the TD when required.
      if (redundant_msdo || tu_has_key_frame) {
        std::vector<uint8_t> multi_stream_obu(num_streams * 2 + 16, 0);
        int multi_header_obu_size = write_multi_stream_decoder_operation_obu(
            multi_stream_obu.data(), num_streams, stream_ids,
            stream_buffer_units);
        uint8_t multi_header_obu_size_data[kMaxLeb128Size] = {};
        size_t multi_header_length_field_size = 0;
        avm_uleb_encode(
            multi_header_obu_size, sizeof(multi_header_obu_size_data),
            multi_header_obu_size_data, &multi_header_length_field_size);
        tu_obus.insert(
            tu_obus.end(), multi_header_obu_size_data,
            multi_header_obu_size_data + multi_header_length_field_size);
        tu_obus.insert(tu_obus.end(), multi_stream_obu.begin(),
                       multi_stream_obu.begin() + multi_header_obu_size);
      }

      data_ptr += static_cast<int>(obu_total_size) +
                  static_cast<int>(length_field_size);
      continue;
    }

    // Rewrite OBU header with signaling stream_id
    {
      uint8_t obu_size_data[kMaxLeb128Size] = {};
      size_t coded_obu_size = 0;
      avm_uleb_encode(obu_total_size - obu_header_size + 2,
                      sizeof(obu_size_data), obu_size_data, &coded_obu_size);
      tu_obus.insert(tu_obus.end(), obu_size_data,
                     obu_size_data + coded_obu_size);

      uint8_t obu_header_data[2] = {};
      write_obu_header_with_stream_id(obu_header_data, &obu_header,
                                      stream_ids[seg_idx]);
      tu_obus.insert(tu_obus.end(), obu_header_data, obu_header_data + 2);
      tu_obus.insert(tu_obus.end(), obu_tmp.begin() + obu_header_size,
                     obu_tmp.end());
    }
    data_ptr +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);
  }

  if (obu_overhead_bytes != nullptr) *obu_overhead_bytes = obu_overhead;

  return tu_obus;
}

int main(int argc, const char *argv[]) {
  bool redundant_msdo = false;
  bool separate_tu = false;
  int arg_offset = 0;

  // Parse optional flags
  while (arg_offset + 1 < argc) {
    if (strcmp(argv[arg_offset + 1], "--redundant-msdo") == 0) {
      redundant_msdo = true;
      ++arg_offset;
    } else if (strcmp(argv[arg_offset + 1], "--separate-tu") == 0) {
      separate_tu = true;
      ++arg_offset;
    } else {
      break;
    }
  }

  if (argc - arg_offset < 8 || (argc - arg_offset - 2) % 3) {
    fprintf(stderr,
            "command: %s [--redundant-msdo] [--separate-tu] "
            "[input file1] [stream ID 1] "
            "[unit size 1] [input "
            "file2] [stream ID 2] [unit size 2] ... [outfile]\n",
            argv[0]);
    return -1;
  } else if (argc - arg_offset > (kMaxMsdoStreams * 3 + 2)) {
    fprintf(stderr,
            "The number of input files cannot exceed the maximum number of "
            "streams (%d)\n",
            kMaxMsdoStreams);
    return -1;
  }

  int num_streams = (argc - arg_offset - 2) / 3;
  int sum_buffer_units = 0;

  for (int i = 0; i < num_streams; ++i) {
    int stream_id = atoi(argv[arg_offset + i * 3 + 2]);
    if (stream_id < 0 || stream_id >= GLOBAL_XLAYER_ID) {
      fprintf(stderr, "The value of stream_id must be in range [0, %d]\n",
              GLOBAL_XLAYER_ID - 1);
      return -1;
    }
    for (int j = 0; j < i; ++j) {
      if (atoi(argv[arg_offset + j * 3 + 2]) == stream_id) {
        fprintf(stderr, "Duplicate stream_id (%d) is not allowed\n", stream_id);
        return -1;
      }
    }
  }
  int first_unit = atoi(argv[arg_offset + 3]);
  int max_unit = first_unit;
  int large_picture_idc = 0;
  bool even_allocation = true;
  for (int i = 0; i < num_streams; ++i) {
    int unit = atoi(argv[arg_offset + i * 3 + 3]);
    if (unit < 0) {
      fprintf(stderr, "Stream buffer unit size must be non-negative\n");
      return -1;
    }
    if (unit != max_unit) {
      even_allocation = false;
      if (unit > max_unit) {
        max_unit = unit;
        large_picture_idc = i;
      }
    }
    sum_buffer_units += unit;
  }
  if (sum_buffer_units > kMaxStreamBufferUnits) {
    fprintf(stderr,
            "The sum of stream buffer units cannot exceed the max value (%d)\n",
            kMaxStreamBufferUnits);
    return -1;
  }
  if (!even_allocation && large_picture_idc > kMaxLargePictureIdc) {
    fprintf(stderr,
            "The stream index with the largest buffer unit (%d) cannot exceed "
            "%d\n",
            large_picture_idc, kMaxLargePictureIdc);
    return -1;
  }

  InputContext input_ctx[AVM_MAX_NUM_STREAMS];
  AvxInputContext avx_ctx[AVM_MAX_NUM_STREAMS];
  ObuDecInputContext obu_ctx[AVM_MAX_NUM_STREAMS];
#if CONFIG_WEBM_IO
  WebmInputContext webm_ctx[AVM_MAX_NUM_STREAMS];
#endif
  std::vector<uint8_t> segments;
  FILE *fin[AVM_MAX_NUM_STREAMS] = {};

  // Initialize file read for each stream before creating the output file so
  // missing inputs never leave behind an empty/truncated output file.
  for (int i = 0; i < num_streams; ++i) {
    const char *in_path = argv[arg_offset + i * 3 + 1];
    fin[i] = fopen(in_path, "rb");
    if (fin[i] == nullptr) {
      fprintf(stderr, "Error: failed to open the input file: %s\n", in_path);
      for (int j = 0; j < i; ++j) {
        if (fin[j] != nullptr) fclose(fin[j]);
      }
      return -1;
    }

    input_ctx[i].avx_ctx = &avx_ctx[i];
    input_ctx[i].obu_ctx = &obu_ctx[i];
#if CONFIG_WEBM_IO
    input_ctx[i].webm_ctx = &webm_ctx[i];
#endif

    input_ctx[i].Init();
    avx_ctx[i].file = fin[i];
    avx_ctx[i].file_type = GetFileType(&input_ctx[i]);

    // behavior underneath the function calls.
    input_ctx[i].unit_buffer =
        reinterpret_cast<uint8_t *>(calloc(kInitialBufferSize, 1));
    if (!input_ctx[i].unit_buffer) {
      fprintf(stderr, "Error: No memory, can't alloc input buffer.\n");
      for (int j = 0; j <= i; ++j) {
        if (fin[j] != nullptr) fclose(fin[j]);
      }
      return -1;
    }
    input_ctx[i].unit_buffer_size = kInitialBufferSize;
  }

  FILE *fout = fopen(argv[argc - 1], "wb");
  if (fout == nullptr) {
    fprintf(stderr, "Error: failed to open the output file: %s\n",
            argv[argc - 1]);
    for (int i = 0; i < num_streams; ++i) {
      if (fin[i] != nullptr) fclose(fin[i]);
    }
    exit(1);
  }

#if PRINT_TU_INFO
  printf("\n =========== Start muxing bitstreams ==============\n\n");
  printf("  Number of streams: %d\n", num_streams);
#endif  // PRINT_TU_INFO

  // Set the values of unit sizes of streams
  int stream_ids[AVM_MAX_NUM_STREAMS];
#if PRINT_TU_INFO
  printf("  Stream IDs: ");
#endif  // PRINT_TU_INFO
  for (int i = 0; i < num_streams; ++i) {
    stream_ids[i] = atoi(argv[arg_offset + i * 3 + 2]);
#if PRINT_TU_INFO
    printf("[%d] ", stream_ids[i]);
#endif  // PRINT_TU_INFO
  }

#if PRINT_TU_INFO
  printf("\n  Stream_buffer_units: ");
#endif  // PRINT_TU_INFO
  // Set the values of unit sizes of streams
  int stream_buffer_units[AVM_MAX_NUM_STREAMS];
  for (int i = 0; i < num_streams; ++i) {
    stream_buffer_units[i] = atoi(argv[arg_offset + i * 3 + 3]);
#if PRINT_TU_INFO
    printf("[%d] ", stream_buffer_units[i]);
#endif  // PRINT_TU_INFO
  }
#if PRINT_TU_INFO
  printf("\n\n");
#endif  // PRINT_TU_INFO

  // Multiplex TUs of multi-streams
  int num_tu_read = 1;
  int num_total_tus = 0;
  int unit_number[AVM_MAX_NUM_STREAMS];
  for (int i = 0; i < num_streams; ++i) unit_number[i] = 0;

  if (separate_tu) {
    // Legacy path: one output TU per input TU, round-robin
    while (num_tu_read) {
      num_tu_read = 0;
      for (int i = 0; i < num_streams; ++i) {
        size_t unit_size = 0;
        if (ReadTemporalUnit(&input_ctx[i], &unit_size)) {
#if PRINT_TU_INFO
          printf("Stream Idx %d\n", i);
          printf("Temporal unit %d\n", unit_number[i]);
#endif  // PRINT_TU_INFO
          int obu_overhead_current_unit = 0;
          segments =
              WriteTU(input_ctx[i].unit_buffer, static_cast<int>(unit_size),
                      &obu_overhead_current_unit, i, num_streams, stream_ids,
                      stream_buffer_units, redundant_msdo);
          fwrite(segments.data(), 1, segments.size(), fout);
#if PRINT_TU_INFO
          printf("  TU overhead:    %d\n", obu_overhead_current_unit);
          printf("  TU total:    %ld\n", unit_size);
#endif  // PRINT_TU_INFO
          ++unit_number[i];
          ++num_tu_read;
        }
      }
    }
  } else {
    // Combined TU path: merge corresponding TUs into a single output TU

    // Pre-compute stream indices sorted by ascending stream_id
    int sorted_indices[AVM_MAX_NUM_STREAMS];
    for (int i = 0; i < num_streams; ++i) sorted_indices[i] = i;
    for (int i = 0; i < num_streams - 1; ++i) {
      for (int j = i + 1; j < num_streams; ++j) {
        if (stream_ids[sorted_indices[j]] < stream_ids[sorted_indices[i]]) {
          int tmp = sorted_indices[i];
          sorted_indices[i] = sorted_indices[j];
          sorted_indices[j] = tmp;
        }
      }
    }

    while (num_tu_read) {
      num_tu_read = 0;
      bool any_key_frame = false;
      std::vector<std::vector<uint8_t>> per_stream_obus(num_streams);

      // Phase 1: Read one TU from each active stream
      for (int i = 0; i < num_streams; ++i) {
        size_t unit_size = 0;
        if (ReadTemporalUnit(&input_ctx[i], &unit_size)) {
#if PRINT_TU_INFO
          printf("Stream Idx %d\n", i);
          printf("Temporal unit %d\n", unit_number[i]);
#endif  // PRINT_TU_INFO
          if (TuHasKeyFrame(input_ctx[i].unit_buffer,
                            static_cast<int>(unit_size))) {
            any_key_frame = true;
          }

          per_stream_obus[i] =
              WriteStreamOBUs(input_ctx[i].unit_buffer,
                              static_cast<int>(unit_size), stream_ids[i]);

          ++unit_number[i];
          ++num_tu_read;
        }
      }

      // Phase 2: Write single combined TU
      if (num_tu_read > 0) {
        segments = WriteCombinedTU(per_stream_obus, sorted_indices, num_streams,
                                   stream_ids, stream_buffer_units,
                                   any_key_frame, redundant_msdo);
        fwrite(segments.data(), 1, segments.size(), fout);
      }
    }
  }

  for (int i = 0; i < num_streams; ++i) num_total_tus += unit_number[i];
  (void)num_total_tus;
#if PRINT_TU_INFO
  printf("  Total number of TUs: %d\n\n", num_total_tus);
  for (int i = 0; i < num_streams; ++i)
    printf("  Number of TUs with stream ID %d: %d\n", stream_ids[i],
           unit_number[i]);
  printf("\n ========== Completed muxing bitstreams ========== \n\n");
#endif  // PRINT_TU_INFO
  // Initialize file read for each stream
  for (int i = 0; i < num_streams; ++i) {
    if (fin[i] != nullptr) {
      fclose(fin[i]);
      fin[i] = nullptr;
    }
  }
  fclose(fout);
  return EXIT_SUCCESS;
}
