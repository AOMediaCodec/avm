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

// This function writes a multi-stream decoder operation OBU,
// when multiple sub-streams are merged into one bitstream.
static int write_multi_stream_decoder_operation_obu(uint8_t *const dst,
                                                    int num_streams,
                                                    int *stream_ids,
                                                    int *stream_buffer_units) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  int obu_type = OBU_MSDO;
  uint32_t size = 0;

  aom_wb_write_literal(&wb, 1, 1);              // obu_extension_flag
  aom_wb_write_literal(&wb, (int)obu_type, 5);  // obu_type
  aom_wb_write_literal(&wb, 0, 2);              // obu_tlayer
  aom_wb_write_literal(&wb, 0, 3);              // obu_mlayer
  aom_wb_write_literal(&wb, 31, 5);             // obu_xlayer

  aom_wb_write_literal(&wb, num_streams - 2, 3);  // signal number of streams
  aom_wb_write_literal(&wb, 0, PROFILE_BITS);     // multistream_profile_idx
  aom_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                       LEVEL_BITS);  // multistream_level_idx
  aom_wb_write_bit(&wb, 0);          // multistream_tier_idx

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

  aom_wb_write_bit(
      &wb,
      multistream_even_allocation_flag);  // multistream_even_allocation_flag
  if (!multistream_even_allocation_flag)
    aom_wb_write_literal(&wb, multistream_large_picture_idc,
                         3);  // multistream_large_picture_idc

  for (int i = 0; i < num_streams; i++) {
    aom_wb_write_literal(&wb, stream_ids[i], XLAYER_BITS);  // signal stream IDs
    aom_wb_write_literal(&wb, 0, PROFILE_BITS);  // substream profile_idx
    aom_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                         LEVEL_BITS);  // substream level_idx
    aom_wb_write_bit(&wb, 0);          // substream tier_idx
  }

  if ((wb.bit_offset % CHAR_BIT == 0)) {
    aom_wb_write_literal(&wb, 0x80, 8);
  } else {
    // assumes that the other bits are already 0s
    aom_wb_write_bit(&wb, 1);
  }

  size = aom_wb_bytes_written(&wb);
  return size;
}

static void write_obu_header_with_stream_id(uint8_t *const dst,
                                            ObuHeader *obu_header,
                                            int stream_id) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  aom_wb_write_bit(&wb, 1);                                 // extention flag
  aom_wb_write_literal(&wb, obu_header->type, 5);           // obu_type
  aom_wb_write_literal(&wb, obu_header->obu_tlayer_id, 2);  // obu_temporal
  aom_wb_write_literal(&wb, obu_header->obu_mlayer_id, 3);  // obu_mlayer
  aom_wb_write_literal(&wb, stream_id, 5);                  // obu_xlayer
}

// This function read a temporal unit from a sub-stream,
// writes the temporal unit with updates of xlayer_id,
// and multiplex the temporal units into a merged bitstream.
std::vector<uint8_t> WriteTU(const uint8_t *data, int length,
                             int *obu_overhead_bytes, int seg_idx,
                             int num_streams, int *stream_ids,
                             int *stream_buffer_units) {
  std::vector<uint8_t> tu_obus;
  const uint8_t *data_ptr = data;

  const int kObuHeaderSizeBytes = 1;
  const int kMinimumBytesRequired = 1 + kObuHeaderSizeBytes;
  int consumed = 0;
  int obu_overhead = 0;
  ObuHeader obu_header;
  while (consumed < length) {
    const int remaining = length - consumed;
    if (remaining < kMinimumBytesRequired) {
      fprintf(stderr,
              "OBU parse error. Did not consume all data, %d bytes remain.\n",
              remaining);
    }

    int obu_header_size = 0;
    size_t length_field_size = 0;
    uint64_t obu_total_size = 0;

    memset(&obu_header, 0, sizeof(obu_header));

    if (aom_uleb_decode(data + consumed, remaining, &obu_total_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n", consumed);
    }

    const uint8_t obu_header_byte = *(data + consumed + length_field_size);
    if (!ParseAV2ObuHeader(obu_header_byte, &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              consumed + static_cast<int>(length_field_size));
    }
    ++obu_overhead;
    ++obu_header_size;

    if (obu_header.obu_extension_flag) {
      const uint8_t obu_ext_header_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes);
      if (!ParseAV2ObuExtensionHeader(obu_ext_header_byte, &obu_header)) {
        fprintf(stderr, "OBU extension parsing failed at offset %d.\n",
                static_cast<int>(consumed + length_field_size +
                                 kObuHeaderSizeBytes));
      }

      ++obu_overhead;
      ++obu_header_size;
    }

    int current_obu_length = static_cast<int>(obu_total_size) - obu_header_size;
    if (obu_header_size + static_cast<int>(length_field_size) +
            current_obu_length >
        remaining) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
    }
    consumed +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);

#if PRINT_TU_INFO
    PrintObuHeader(&obu_header);
#endif  // PRINT_TU_INFO

    std::vector<uint8_t> obu_tmp(data_ptr + length_field_size,
                                 data_ptr + obu_total_size + length_field_size);

    if (obu_header.type == OBU_SEQUENCE_HEADER && seg_idx == 0) {
      std::vector<uint8_t> multi_stream_obu(num_streams * 2 + 4);
      int multi_header_obu_size = write_multi_stream_decoder_operation_obu(
          multi_stream_obu.data(), num_streams, stream_ids,
          stream_buffer_units);
      std::vector<uint8_t> multi_header_obu_size_data(length_field_size);
      size_t multi_header_length_field_size = 0;
      aom_uleb_encode(multi_header_obu_size, sizeof(multi_header_obu_size),
                      multi_header_obu_size_data.data(),
                      &multi_header_length_field_size);
      tu_obus.insert(
          tu_obus.end(), multi_header_obu_size_data.begin(),
          multi_header_obu_size_data.begin() + multi_header_length_field_size);
      tu_obus.insert(tu_obus.end(), multi_stream_obu.begin(),
                     multi_stream_obu.begin() + multi_header_obu_size);
    }

    // Rewrite OBU header with signaling stream_id
    if (obu_header.type == OBU_TEMPORAL_DELIMITER) {
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      aom_uleb_encode(obu_total_size, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());
      tu_obus.insert(tu_obus.end(), obu_tmp.begin(), obu_tmp.end());
    } else {
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      aom_uleb_encode(obu_total_size + 1, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());

      std::vector<uint8_t> obu_header_data(2);
      write_obu_header_with_stream_id(obu_header_data.data(), &obu_header,
                                      stream_ids[seg_idx]);
      tu_obus.insert(tu_obus.end(), obu_header_data.begin(),
                     obu_header_data.end());
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
  if (argc < 3 || (argc - 2) % 3) {
    fprintf(stderr,
            "command: %s [input file1], [stream ID 1], [unit size 1], [input "
            "file2], [stream ID 2], [unit size 2], ... [outfile]\n",
            argv[0]);
    return -1;
  } else if (argc > (AOM_MAX_NUM_STREAMS * 3 + 2)) {
    fprintf(stderr,
            "The number of input files cannot exceed the maximum number of "
            "streams (8)\n");
    return -1;
  }

  int num_streams = (argc - 2) / 3;
  int sum_buffer_units = 0;

  for (int i = 0; i < num_streams; ++i) {
    int stream_id = atoi(argv[i * 3 + 2]);
    if (stream_id > 7) {
      fprintf(stderr,
              "The value of stream_id cannot exceed the max value (7)\n");
      return -1;
    }
  }
  for (int i = 0; i < num_streams; ++i) {
    sum_buffer_units += atoi(argv[i * 3 + 3]);
  }
  if (sum_buffer_units > 8) {
    fprintf(stderr,
            "The sum of stream buffer units cannot exceed the max value (8)\n");
    return -1;
  }

  FILE *fout = fopen(argv[argc - 1], "wb");

  if (fout == nullptr) {
    fprintf(stderr, "Error: failed to open the output file: %s",
            argv[argc - 1]);
    exit(1);
  }

  InputContext input_ctx[AOM_MAX_NUM_STREAMS];
  AvxInputContext avx_ctx[AOM_MAX_NUM_STREAMS];
  ObuDecInputContext obu_ctx[AOM_MAX_NUM_STREAMS];
#if CONFIG_WEBM_IO
  WebmInputContext webm_ctx[AOM_MAX_NUM_STREAMS];
#endif
  std::vector<uint8_t> segments;
  FILE *fin[AOM_MAX_NUM_STREAMS];

  // Initialize file read for each stream
  for (int i = 0; i < num_streams; ++i) {
    fin[i] = fopen(argv[i * 3 + 1], "rb");
    if (fin[i] == nullptr) {
      fprintf(stderr, "Error: failed to open the input file\n");
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
    }
    input_ctx[i].unit_buffer_size = kInitialBufferSize;
  }

#if PRINT_TU_INFO
  printf("\n =========== Start muxing bitstreams ==============\n\n");
  printf("  Number of streams: %d\n", num_streams);
#endif  // PRINT_TU_INFO

  // Set the values of unit sizes of streams
  int stream_ids[AOM_MAX_NUM_STREAMS];
#if PRINT_TU_INFO
  printf("  Stream IDs: ");
#endif  // PRINT_TU_INFO
  for (int i = 0; i < num_streams; ++i) {
    stream_ids[i] = atoi(argv[i * 3 + 2]);
#if PRINT_TU_INFO
    printf("[%d] ", stream_ids[i]);
#endif  // PRINT_TU_INFO
  }

#if PRINT_TU_INFO
  printf("\n  Stream_buffer_units: ");
#endif  // PRINT_TU_INFO
  // Set the values of unit sizes of streams
  int stream_buffer_units[AOM_MAX_NUM_STREAMS];
  for (int i = 0; i < num_streams; ++i) {
    stream_buffer_units[i] = atoi(argv[i * 3 + 3]);
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
  int unit_number[AOM_MAX_NUM_STREAMS];
  for (int i = 0; i < num_streams; ++i) unit_number[i] = 0;

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
                    stream_buffer_units);
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

  for (int i = 0; i < num_streams; ++i) num_total_tus += unit_number[i];
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
