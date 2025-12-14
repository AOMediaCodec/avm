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

#include "tools/stream_mux.h"
// This function read a multi-stream decoder operation OBU.
static int read_multi_stream_decoder_operation(struct avm_read_bit_buffer *rb,
                                               int *stream_ids) {
#if PRINT_TU_INFO
  printf("\n==Parse mutl-stream header==\n");
#endif  // PRINT_TU_INFO
  const int num_streams =
      avm_rb_read_literal(rb, 3) + 2;  // read number of streams
#if PRINT_TU_INFO
  printf("--num_streams: %d\n", num_streams);
#endif  // PRINT_TU_INFO
  if (num_streams > AVM_MAX_NUM_STREAMS) {
    fprintf(stderr, "The number of streams cannot exceed the max value (4).\n");
    return -1;
  }

  const int multistream_profile_idx =
      avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
#if PRINT_TU_INFO
  printf("--multistream_profile_idx: %d\n", multistream_profile_idx);
#endif  // PRINT_TU_INFO
  (void)multistream_profile_idx;

  const int multistream_level_idx =
      avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
#if PRINT_TU_INFO
  printf("--multistream_level_idx: %d\n", multistream_level_idx);
#endif  // PRINT_TU_INFO
  (void)multistream_level_idx;

  const int multistream_tier_idx =
      avm_rb_read_bit(rb);  // read tier of multistream
#if PRINT_TU_INFO
  printf("--multistream_tier_idx: %d\n", multistream_tier_idx);
#endif  // PRINT_TU_INFO
  (void)multistream_tier_idx;

  const int multistream_even_allocation_flag =
      avm_rb_read_bit(rb);  // read multistream_even_allocation_flag

  if (!multistream_even_allocation_flag) {
    const int multistream_large_picture_idc =
        avm_rb_read_literal(rb, 3);  // read multistream_large_picture_idc
    (void)multistream_large_picture_idc;
  }

  for (int i = 0; i < num_streams; i++) {
    stream_ids[i] = avm_rb_read_literal(rb, 5);  // read stream ID
#if PRINT_TU_INFO
    printf("--stream_ids[%d]: %d\n", i, stream_ids[i]);
#endif  // PRINT_TU_INFO
    const int substream_profile_idx =
        avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
#if PRINT_TU_INFO
    printf("--sub-stream_profile_idx[%d]: %d\n", stream_ids[i],
           substream_profile_idx);
#endif  // PRINT_TU_INFO
    (void)substream_profile_idx;
    const int substream_level_idx =
        avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
#if PRINT_TU_INFO
    printf("--sub-stream_level_idx[%d]: %d\n", stream_ids[i],
           substream_level_idx);
#endif  // PRINT_TU_INFO
    (void)substream_level_idx;
    const int substream_tier_idx =
        avm_rb_read_bit(rb);  // read tier of multistream
#if PRINT_TU_INFO
    printf("--sub-stream_tier_idx[%d]: %d\n", stream_ids[i],
           substream_tier_idx);
#endif  // PRINT_TU_INFO
    (void)substream_tier_idx;
  }

  return num_streams;
}

static void write_obu_header_without_stream_id(uint8_t *const dst,
                                               ObuHeader *obu_header) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  avm_wb_write_bit(&wb, 0);                                 // extention flag
  avm_wb_write_literal(&wb, obu_header->type, 5);           // obu_type
  avm_wb_write_literal(&wb, obu_header->obu_tlayer_id, 2);  // obu_temporal
}

// This function read a temporal unit from a merged bitstream,
// writes the temporal unit into each sub-stream.
std::vector<uint8_t> ExtractTU(const uint8_t *data, int length,
                               int *obu_overhead_bytes, int *num_streams,
                               int *stream_ids, int *current_stream_id) {
  std::vector<uint8_t> tu_obus;
  const uint8_t *data_ptr = data;

  struct avm_read_bit_buffer rb;

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

    if (avm_uleb_decode(data + consumed, remaining, &obu_total_size,
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

    if (obu_header.obu_extension_flag) {
      *current_stream_id = obu_header.obu_xlayer_id;
    } else if (obu_header.type == OBU_MSDO) {
      *current_stream_id = 31;
    } else {
      *current_stream_id = 0;
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

    // Rewrite OBU header with signaling stream_id
    if (obu_header.type == OBU_TEMPORAL_DELIMITER) {
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      avm_uleb_encode(obu_total_size, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());
      tu_obus.insert(tu_obus.end(), obu_tmp.begin(), obu_tmp.end());
    } else if (obu_header.type == OBU_MSDO) {
      init_read_bit_buffer(
          &rb, data_ptr + obu_header_size + static_cast<int>(length_field_size),
          data_ptr + obu_total_size + length_field_size);
      *num_streams = read_multi_stream_decoder_operation(&rb, stream_ids);
    } else {
      std::vector<uint8_t> obu_size_data(length_field_size);
      size_t coded_obu_size;
      avm_uleb_encode(obu_total_size - 1, sizeof(obu_total_size),
                      obu_size_data.data(), &coded_obu_size);
      if (length_field_size != coded_obu_size)
        fprintf(stderr, "\nError: length_field_size != coded_obu_size\n");
      tu_obus.insert(tu_obus.end(), obu_size_data.begin(), obu_size_data.end());

      std::vector<uint8_t> obu_header_data(1);
      write_obu_header_without_stream_id(obu_header_data.data(), &obu_header);
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

void generate_filenames(char *base_filename, int num_files, char **file_names) {
  char *dot_pos;
#if PRINT_TU_INFO
  printf("generate_filenames : base filne name :%s, num of riles: %d\n",
         base_filename, num_files);
#endif  // PRINT_TU_INFO
  // Find the position of the last dot (extension)
  dot_pos = strrchr(base_filename, '.');

  // Generate filenames for the specified number of files
  for (int index = 0; index < num_files; index++) {
    if (dot_pos != NULL) {
      // Copy the part before the extension
      strncpy(file_names[index], base_filename, dot_pos - base_filename);
      file_names[index][dot_pos - base_filename] = '\0';

      // Add the index
      sprintf(file_names[index] + strlen(file_names[index]), "_");
      sprintf(file_names[index] + strlen(file_names[index]), "%d", index);

      // Add the extension
      strcat(file_names[index], dot_pos);
    } else {
      // No extension in the base filename
      strcpy(file_names[index], base_filename);
      sprintf(file_names[index] + strlen(file_names[index]), "_");
      sprintf(file_names[index] + strlen(file_names[index]), "%d", index);
    }
  }
#if PRINT_TU_INFO
  for (int index = 0; index < num_files; index++)
    printf("File name: %s\n", file_names[index]);
#endif  // PRINT_TU_INFO
}

int main(int argc, const char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "command: %s [inputfile] [outfile]\n", argv[0]);
    return -1;
  }

  int num_streams = 1;
  int stream_ids[AVM_MAX_NUM_STREAMS] = { -1, -1, -1, -1 };
  char base_filename[100];
  strcpy(base_filename, argv[2]);

  // Initialize file read for the merged stream
  FILE *fin;
  fin = fopen(argv[1], "rb");

  if (fin == nullptr) {
    fprintf(stderr, "Error: failed to open the output file: %s",
            argv[argc - 1]);
    exit(1);
  }

  InputContext input_ctx;
  AvxInputContext avx_ctx;
  ObuDecInputContext obu_ctx;
#if CONFIG_WEBM_IO
  WebmInputContext webm_ctx;
#endif
  std::vector<uint8_t> segments;

  input_ctx.avx_ctx = &avx_ctx;
  input_ctx.obu_ctx = &obu_ctx;
#if CONFIG_WEBM_IO
  input_ctx.webm_ctx = &webm_ctx;
#endif

  input_ctx.Init();
  avx_ctx.file = fin;
  avx_ctx.file_type = GetFileType(&input_ctx);

  // behavior underneath the function calls.
  input_ctx.unit_buffer =
      reinterpret_cast<uint8_t *>(calloc(kInitialBufferSize, 1));
  if (!input_ctx.unit_buffer) {
    fprintf(stderr, "Error: No memory, can't alloc input buffer.\n");
    exit(1);
  }
  input_ctx.unit_buffer_size = kInitialBufferSize;

  // Initialize file write for each stream
  char *output_file_names[AVM_MAX_NUM_STREAMS];
  for (int i = 0; i < AVM_MAX_NUM_STREAMS; ++i) {
    output_file_names[i] = (char *)malloc(100 * sizeof(char));
    if (output_file_names[i] == NULL) {
      fprintf(stderr, "Error: No memory, can't alloc output_file_names.\n");
      exit(1);
    }
  }

  FILE *fout[AVM_MAX_NUM_STREAMS];

  generate_filenames(base_filename, AVM_MAX_NUM_STREAMS, output_file_names);

  for (int i = 0; i < num_streams; ++i) {
    fout[i] = fopen(output_file_names[i], "wb");

    if (fout[i] == nullptr) {
      fprintf(stderr, "Error: failed to open the output file: %s",
              output_file_names[i]);
      exit(1);
    }
  }
#if PRINT_TU_INFO
  printf("\n ========== Start demuxing bitstreams =============\n\n");
#endif  // PRINT_TU_INFO

  // Multiplex TUs of multi-streams
  int num_tu_read = 1;
  int num_total_tus = 0;
  int unit_number[AVM_MAX_NUM_STREAMS];
  for (int i = 0; i < AVM_MAX_NUM_STREAMS; ++i) unit_number[i] = 0;

  while (num_tu_read) {
    int idx = 0;
    size_t unit_size = 0;
    int current_stream_id = -1;
    num_tu_read = 0;
    if (ReadTemporalUnit(&input_ctx, &unit_size)) {
      int updated_num_streams = num_streams;
      int obu_overhead_current_unit = 0;
      segments = ExtractTU(input_ctx.unit_buffer, static_cast<int>(unit_size),
                           &obu_overhead_current_unit, &updated_num_streams,
                           stream_ids, &current_stream_id);
      if (current_stream_id < 0)
        fprintf(stderr,
                "Error: the stream ID value of a frame (header) OBU shall be "
                "equal to or greater than 0\n");
      for (int i = 0; i < updated_num_streams; ++i) {
        if (current_stream_id == stream_ids[i]) {
          idx = i;
          break;
        }
      }
#if PRINT_TU_INFO
      printf("Stream Id %d\n", current_stream_id);
      printf("Temporal unit %d\n", unit_number[idx]);
#endif  // PRINT_TU_INFO

      if (updated_num_streams != num_streams) {
#if PRINT_TU_INFO
        printf("  Update number of streams: %d to %d\n", num_streams,
               updated_num_streams);
#endif  // PRINT_TU_INFO
        if (updated_num_streams < 0) {
          fprintf(stderr,
                  "Error: the parameters in multi_stream header OBU do not "
                  "fullfill the requirements.\n");
          return -1;
        } else if (updated_num_streams > num_streams) {
          for (int i = num_tu_read; i < updated_num_streams; ++i) {
            fout[i] = fopen(output_file_names[i], "wb");

            if (fout[i] == nullptr) {
              fprintf(stderr, "Error: failed to open the output file: %s",
                      output_file_names[i]);
              exit(1);
            }
          }
        }
        num_streams = updated_num_streams;
      }
      fwrite(segments.data(), 1, segments.size(), fout[idx]);
#if PRINT_TU_INFO
      printf("  TU overhead:    %d\n", obu_overhead_current_unit);
      printf("  TU total:    %ld\n", unit_size);
#endif  // PRINT_TU_INFO
      ++unit_number[idx];
      ++num_tu_read;
    }
  }

  for (int i = 0; i < num_streams; ++i) num_total_tus += unit_number[i];
#if PRINT_TU_INFO
  printf("  Total number of TUs: %d\n\n", num_total_tus);
  for (int i = 0; i < num_streams; ++i)
    printf("  Number of TUs with stream ID %d: %d\n", stream_ids[i],
           unit_number[i]);
  printf("\n ========= Completed demuxing bitstreams ========= \n\n");
#endif  // PRINT_TU_INFO
  for (int i = 0; i < num_streams; ++i) fclose(fout[i]);

  for (int i = 0; i < AVM_MAX_NUM_STREAMS; ++i) {
    free(output_file_names[i]);
  }

  return EXIT_SUCCESS;
}
