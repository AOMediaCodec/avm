/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "tools/stream_mux.h"

#if CONFIG_F159_OBU_HEADER
static int write_multi_stream_HEADER_obu(uint8_t *const dst, int num_streams,
                                         int *stream_ids,
                                         int *stream_buffer_units) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  int obu_type = OBU_MULTI_STREAM_HEADER;
  uint32_t size = 0;

  aom_wb_write_literal(&wb, (int)obu_type, 4);  // obu_type
  aom_wb_write_literal(&wb, 1, 1);              // obu_extension_flag
  aom_wb_write_literal(&wb, 0, 3);              // obu_tlayer
  aom_wb_write_literal(&wb, 0, 3);              // obu_mlayer
  aom_wb_write_literal(&wb, 31, 5);             // obu_xlayer

  aom_wb_write_literal(&wb, num_streams - 1, 3);  // signal number of streams
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
    aom_wb_write_literal(&wb, stream_ids[i], 5);  // signal stream IDs
    aom_wb_write_literal(&wb, 0, PROFILE_BITS);   // substream profile_idx
    aom_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                         LEVEL_BITS);  // substream level_idx
    aom_wb_write_bit(&wb, 0);          // substream tier_idx
  }
  size = aom_wb_bytes_written(&wb);
  return size;
}
#else   // CONFIG_F159_OBU_HEADER
static void write_multi_stream_HEADER_obu(uint8_t *const dst, int num_streams,
                                          int *stream_ids,
                                          int *stream_buffer_units) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  int obu_type = OBU_MULTI_STREAM_HEADER;

  aom_wb_write_bit(&wb, 0);                // forbidden bit.
  aom_wb_write_literal(&wb, obu_type, 4);  // obu type
  aom_wb_write_bit(&wb, 0);                // extention flag
  aom_wb_write_bit(&wb, 1);                // obu_has_payload_length_field
  aom_wb_write_bit(&wb, 0);                // reserved
  aom_wb_write_literal(&wb, (num_streams * 2) + 2, 8);  // obu_size 1

  aom_wb_write_literal(&wb, num_streams - 1, 3);  // signal number of streams
  aom_wb_write_literal(&wb, 0, PROFILE_BITS);     // multistream_profile_idx
  aom_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                       LEVEL_BITS);  // multistream_level_idx
  aom_wb_write_bit(&wb, 0);          // multistream_tier_idx

  aom_wb_write_bit(&wb, 0);         // multistream_even_allocation_flag
  aom_wb_write_literal(&wb, 0, 3);  // multistream_large_picture_idc

  for (int i = 0; i < num_streams; i++) {
    aom_wb_write_literal(&wb, stream_ids[i], 5);  // signal stream IDs
    aom_wb_write_literal(&wb, 0, PROFILE_BITS);   // substream profile_idx
    aom_wb_write_literal(&wb, SEQ_LEVEL_4_0,
                         LEVEL_BITS);  // substream level_idx
    aom_wb_write_bit(&wb, 0);          // substream tier_idx
  }
  for (int i = 0; i < num_streams * 2; i++)
    aom_wb_write_bit(&wb, 0);  // trailing bits
}
#endif  // CONFIG_F159_OBU_HEADER

std::vector<uint8_t> WriteTU(const uint8_t *data, int length,
                             int *obu_overhead_bytes, int seg_idx,
                             int num_streams, int *stream_ids,
                             int *stream_buffer_units) {
  std::vector<uint8_t> tu_obus;
  const uint8_t *data_ptr = data;

  struct aom_read_bit_buffer rb;

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

    memset(&obu_header, 0, sizeof(obu_header));
#if CONFIG_F159_OBU_HEADER
    uint64_t obu_total_size = 0;
    if (aom_uleb_decode(data + consumed, remaining, &obu_total_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n", consumed);
    }

    const uint8_t obu_header_firstbyte =
        *(data + consumed + static_cast<int>(length_field_size));
    const uint8_t obu_header_secondbyte =
        *(data + consumed + static_cast<int>(length_field_size) + 1);
    if (!ParseAV2ObuHeader(obu_header_firstbyte, obu_header_secondbyte,
                           &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              consumed + static_cast<int>(length_field_size));
    }
    obu_overhead += 2;
    obu_header_size += 2;

    int current_obu_length = static_cast<int>(obu_total_size) - obu_header_size;
    if (obu_header_size + static_cast<int>(length_field_size) +
            current_obu_length >
        remaining) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
    }
    consumed +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);
#else   // CONFIG_F159_OBU_HEADER
    uint64_t obu_size = 0;

    const uint8_t obu_header_byte = *(data + consumed);
    if (!ParseAV2ObuHeader(obu_header_byte, &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n", consumed);
    }
    ++obu_overhead;
    ++obu_header_size;

    if (obu_header.has_extension) {
      const uint8_t obu_ext_header_byte =
          *(data + consumed + kObuHeaderSizeBytes);
      if (!ParseAV2ObuExtensionHeader(obu_ext_header_byte, &obu_header)) {
        fprintf(stderr, "OBU extension parsing failed at offset %d.\n",
                consumed + kObuHeaderSizeBytes);
      }

      ++obu_overhead;
      ++obu_header_size;
    }

    if (aom_uleb_decode(data + consumed + obu_header_size,
                        remaining - obu_header_size, &obu_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n",
              consumed + obu_header_size);
    }
    int current_obu_length = static_cast<int>(obu_size);
    if (obu_header_size + static_cast<int>(length_field_size) +
            current_obu_length >
        remaining) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
    }
    uint64_t obu_total_size = obu_header_size +
                              static_cast<int>(length_field_size) +
                              current_obu_length;
    consumed += obu_total_size;
#endif  // CONFIG_F159_OBU_HEADER

#if PRINT_TU_INFO
    PrintObuHeader(&obu_header);
#endif  // PRINT_TU_INFO

#if CONFIG_F159_OBUSIZE_ANNEXB
    std::vector<uint8_t> obu_tmp(data_ptr + length_field_size,
                                 data_ptr + obu_total_size + length_field_size);
#else   // CONFIG_F159_OBUSIZE_ANNEXB
    std::vector<uint8_t> obu_tmp(data_ptr, data_ptr + obu_total_size);
#endif  // CONFIG_F159_OBUSIZE_ANNEXB

    init_read_bit_buffer(
        &rb, data_ptr + obu_header_size + static_cast<int>(length_field_size),
#if CONFIG_F159_OBUSIZE_ANNEXB
        data_ptr + obu_total_size + length_field_size);
#else   // CONFIG_F159_OBUSIZE_ANNEXB
        data_ptr + obu_total_size);
#endif  // CONFIG_F159_OBUSIZE_ANNEXB

#if CONFIG_F159_OBU_HEADER
    if (obu_header.type == OBU_SEQUENCE_HEADER && seg_idx == 0) {
      std::vector<uint8_t> multi_stream_obu(num_streams * 2 + 4);
      int multi_header_obu_size =
          write_multi_stream_HEADER_obu(multi_stream_obu.data(), num_streams,
                                        stream_ids, stream_buffer_units);
#if CONFIG_F159_OBUSIZE_ANNEXB
      std::vector<uint8_t> multi_header_obu_size_data(length_field_size);
      size_t multi_header_length_field_size = 0;
      aom_uleb_encode(multi_header_obu_size, sizeof(multi_header_obu_size),
                      multi_header_obu_size_data.data(),
                      &multi_header_length_field_size);
      tu_obus.insert(
          tu_obus.end(), multi_header_obu_size_data.begin(),
          multi_header_obu_size_data.begin() + multi_header_length_field_size);
#endif  // CONFIG_F159_OBUSIZE_ANNEXB
      tu_obus.insert(tu_obus.end(), multi_stream_obu.begin(),
                     multi_stream_obu.begin() + multi_header_obu_size);
    }
#else   // CONFIG_F159_OBU_HEADER
    if (obu_header.type == OBU_SEQUENCE_HEADER && seg_idx == 0) {
      std::vector<uint8_t> multi_stream_obu(3 + num_streams);
      write_multi_stream_HEADER_obu(multi_stream_obu.data(), num_streams,
                                    stream_ids, stream_buffer_units);
      tu_obus.insert(tu_obus.end(), multi_stream_obu.begin(),
                     multi_stream_obu.end());
    }
#endif  // CONFIG_F159_OBU_HEADER

    // Rewrite OBU header with signaling stream_id
    if (obu_header.type == OBU_TEMPORAL_DELIMITER) {
#if CONFIG_F159_OBUSIZE_ANNEXB
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      aom_uleb_encode(obu_total_size, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());
#endif  // CONFIG_F159_OBUSIZE_ANNEXB

      tu_obus.insert(tu_obus.end(), obu_tmp.begin(), obu_tmp.end());
    } else {
#if CONFIG_F159_OBUSIZE_ANNEXB
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      aom_uleb_encode(obu_total_size, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());
#endif  // CONFIG_F159_OBUSIZE_ANNEXB

      std::vector<uint8_t> obu_header_data(2);
      write_obu_header_with_stream_id(obu_header_data.data(), &obu_header,
                                      stream_ids[seg_idx]);
      tu_obus.insert(tu_obus.end(), obu_header_data.begin(),
                     obu_header_data.end());
      tu_obus.insert(tu_obus.end(), obu_tmp.begin() + obu_header_size,
                     obu_tmp.end());
    }
#if CONFIG_F159_OBUSIZE_ANNEXB
    data_ptr +=
        static_cast<int>(obu_total_size) + static_cast<int>(length_field_size);
#else   // CONFIG_F159_OBUSIZE_ANNEXB
    data_ptr += obu_total_size;
#endif  // CONFIG_F159_OBUSIZE_ANNEXB
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
  } else if (argc > (MAX_NUM_STREAMS * 3 + 2)) {
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

  InputContext input_ctx[MAX_NUM_STREAMS];
  AvxInputContext avx_ctx[MAX_NUM_STREAMS];
  ObuDecInputContext obu_ctx[MAX_NUM_STREAMS];
#if CONFIG_WEBM_IO
  WebmInputContext webm_ctx[MAX_NUM_STREAMS];
#endif
  std::vector<uint8_t> segments;
  FILE *fin[MAX_NUM_STREAMS];

  // Initialize file read for each stream
  for (int i = 0; i < num_streams; ++i) {
    fin[i] = fopen(argv[i * 3 + 1], "rb");
    if (fin[i] == nullptr) {
      fprintf(stderr, "Error: failed to open the input file\n");
      return EXIT_FAILURE;
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
      return EXIT_FAILURE;
    }
    input_ctx[i].unit_buffer_size = kInitialBufferSize;
  }

  printf("\n =========== Start muxing bitstreams ==============\n\n");
  printf("  Number of streams: %d\n", num_streams);

  // Set the values of unit sizes of streams
  int stream_ids[MAX_NUM_STREAMS];
  printf("  Stream IDs: ");
  for (int i = 0; i < num_streams; ++i) {
    stream_ids[i] = atoi(argv[i * 3 + 2]);
    printf("[%d] ", stream_ids[i]);
  }

  printf("\n  Stream_buffer_units: ");
  // Set the values of unit sizes of streams
  int stream_buffer_units[MAX_NUM_STREAMS];
  for (int i = 0; i < num_streams; ++i) {
    stream_buffer_units[i] = atoi(argv[i * 3 + 3]);
    printf("[%d] ", stream_buffer_units[i]);
  }
  printf("\n\n");

  // Multiplex TUs of multi-streams
  int num_tu_read = 1;
  int num_total_tus = 0;
  int unit_number[MAX_NUM_STREAMS];
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

  printf("  Total number of TUs: %d\n\n", num_total_tus);
  for (int i = 0; i < num_streams; ++i)
    printf("  Number of TUs with stream ID %d: %d\n", stream_ids[i],
           unit_number[i]);
  printf("\n ========== Completed muxing bitstreams ========== \n\n");

  fclose(fout);
  return EXIT_SUCCESS;
}
