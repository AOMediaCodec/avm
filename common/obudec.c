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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config/aom_config.h"
#include "common/obudec.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/mem_ops.h"
#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/enums.h"

#define OBU_BUFFER_SIZE (500 * 1024)

/*!\brief Maximum OBU header size in bytes. */
#define OBU_HEADER_SIZE 2
#define OBU_EXTENSION_SIZE 1
#define OBU_MAX_LENGTH_FIELD_SIZE 8

#define OBU_MAX_HEADER_SIZE \
  (OBU_HEADER_SIZE + OBU_EXTENSION_SIZE + 2 * OBU_MAX_LENGTH_FIELD_SIZE)

#define OBU_DETECTION_SIZE \
  (OBU_HEADER_SIZE + OBU_EXTENSION_SIZE + 4 * OBU_MAX_LENGTH_FIELD_SIZE)

// Reads unsigned LEB128 integer and returns 0 upon successful read and decode.
// Stores raw bytes in 'value_buffer', length of the number in 'value_length',
// and decoded value in 'value'.
static int obudec_read_leb128(FILE *f, uint8_t *value_buffer,
                              size_t *value_length, uint64_t *value) {
  if (!f || !value_buffer || !value_length || !value) return -1;
  size_t len;
  for (len = 0; len < OBU_MAX_LENGTH_FIELD_SIZE; ++len) {
    const size_t num_read = fread(&value_buffer[len], 1, 1, f);
    if (num_read == 0) {
      if (len == 0 && feof(f)) {
        *value_length = 0;
        return 0;
      }
      // Ran out of data before completing read of value.
      return -1;
    }
    if ((value_buffer[len] >> 7) == 0) {
      ++len;
      *value_length = len;
      break;
    }
  }

  return aom_uleb_decode(value_buffer, len, value, NULL);
}

#if OBU_ORDER_IN_TU
int check_obu_order(OBU_TYPE prev_obu_type, OBU_TYPE curr_obu_type) {
  if ((prev_obu_type == OBU_TEMPORAL_DELIMITER) &&
      (curr_obu_type == OBU_MSDO ||
       curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
       curr_obu_type == OBU_ATLAS_SEGMENT ||
       curr_obu_type == OBU_OPERATING_POINT_SET ||
       curr_obu_type == OBU_SEQUENCE_HEADER ||
       curr_obu_type == OBU_MULTI_FRAME_HEADER || curr_obu_type == OBU_SEF ||
       curr_obu_type == OBU_TIP || curr_obu_type == OBU_SWITCH ||
       curr_obu_type == OBU_RAS_FRAME || curr_obu_type == OBU_BRIDGE_FRAME ||
       curr_obu_type == OBU_TILE_GROUP || curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_MSDO) &&
             (curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_LAYER_CONFIGURATION_RECORD) &&
             (curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_OPERATING_POINT_SET) &&
             (curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_ATLAS_SEGMENT) &&
             (curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_SEQUENCE_HEADER) &&
             (curr_obu_type == OBU_SEQUENCE_HEADER ||
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_BUFFER_REMOVAL_TIMING ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_BUFFER_REMOVAL_TIMING) &&
             (curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_MULTI_FRAME_HEADER) &&
             (curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA)) {
    return 0;
  } else if ((prev_obu_type == OBU_METADATA) &&
             (curr_obu_type == OBU_SEF || curr_obu_type == OBU_TIP ||
              curr_obu_type == OBU_SWITCH || curr_obu_type == OBU_RAS_FRAME ||
              curr_obu_type == OBU_BRIDGE_FRAME ||
              curr_obu_type == OBU_TILE_GROUP ||
              curr_obu_type == OBU_METADATA ||
              curr_obu_type == OBU_TEMPORAL_DELIMITER)) {
    return 0;
  } else if (prev_obu_type == OBU_TEMPORAL_DELIMITER ||
             prev_obu_type == OBU_TILE_GROUP || prev_obu_type == OBU_SWITCH ||
             prev_obu_type == OBU_RAS_FRAME || prev_obu_type == OBU_SEF ||
             prev_obu_type == OBU_TIP || prev_obu_type == OBU_BRIDGE_FRAME ||
             prev_obu_type == OBU_PADDING) {
    return 0;
  }
  return 1;
}
#endif  // OBU_ORDER_IN_TU

static int read_nbyte_from_file(FILE *f, size_t obu_header_size,
                                uint8_t *buffer, ObuHeader *obu_header) {
  if (!f) {
    return -2;
  }

  size_t bytes_read = fread(buffer, sizeof(uint8_t), obu_header_size, f);
  if (feof(f) && bytes_read == 0) {
    return -1;
  } else if (bytes_read != obu_header_size) {
    fprintf(stderr, "obudec: Failure reading OBU header.\n");
    return -2;
  }

  obu_header->obu_extension_flag = (buffer[0] >> 7) & 1;  // obu_extension_flag
  obu_header->type = (buffer[0] >> 2) & 31;               // obu_type
  obu_header->obu_tlayer_id = (buffer[0]) & 3;            // obu_temporal
  if (obu_header->obu_extension_flag) {
    obu_header->obu_mlayer_id = (buffer[1] >> 5) & 7;  // obu_layer (mlayer)
    obu_header->obu_xlayer_id = (buffer[1]) & 31;      // obu_layer (xlayer)
  } else {
    obu_header->obu_mlayer_id = 0;  // obu_layer (mlayer)
#if CONFIG_SET_DEFAULT_VALUE_XLAYER_ID
    if (obu_header->type == OBU_MSDO)
      obu_header->obu_xlayer_id = MAX_NUM_XLAYERS - 1;  // obu_layer (xlayer)
    else
#endif                                // CONFIG_SET_DEFAULT_VALUE_XLAYER_ID
      obu_header->obu_xlayer_id = 0;  // obu_layer (xlayer)
  }

  return 0;
}

static int peek_obu_from_file(FILE *f, size_t obu_header_size, uint8_t *buffer,
                              ObuHeader *obu_header) {
  if (!f) {
    return -2;
  }
  unsigned long fpos = ftell(f);
  const int status =
      read_nbyte_from_file(f, obu_header_size, buffer, obu_header);
  if (status == -2) {
    fprintf(stderr, "obudec: Failure peeking OBU header.\n");
    return -2;
  } else if (status == -1) {
    // bytes_read is already 0
    return -1;
  }
  fseek(f, fpos, SEEK_SET);
  return 0;
}

int file_is_obu(struct ObuDecInputContext *obu_ctx) {
  if (!obu_ctx || !obu_ctx->avx_ctx) return 0;

  struct AvxInputContext *avx_ctx = obu_ctx->avx_ctx;
  uint8_t detect_buf[OBU_DETECTION_SIZE] = { 0 };
  const int is_annexb = obu_ctx->is_annexb;
  FILE *f = avx_ctx->file;

  if (!f) {
    return 0;
  }

  while (1) {
    {
      size_t obu_payload_size_bytelength = 0;
      uint64_t obu_payload_size = 0;
      if (is_annexb) {
        if (obudec_read_leb128(f, &detect_buf[0], &obu_payload_size_bytelength,
                               &obu_payload_size) != 0) {
          fprintf(stderr, "file_type: Failure reading obu size\n");
          rewind(f);
          return 0;
        } else if (feof(f)) {
          break;
        }
        obu_payload_size -= OBU_HEADER_SIZE;
      } else {
        fprintf(stderr, "file_type: OBU size is required\n");
        rewind(f);
        return 0;
      }
      ObuHeader obu_header;
      memset(&obu_header, 0, sizeof(obu_header));
      const int read_status =
          read_nbyte_from_file(f, OBU_HEADER_SIZE, &detect_buf[0], &obu_header);
      if (read_status == -2) {
        fprintf(stderr, "file_type: Failure reading an OBU.\n");
        rewind(f);
        return 0;
      } else if (read_status == -1) {  // end of file
        break;
      }
      fseek(f, obu_payload_size, SEEK_CUR);
    }
  }  // while

  // move the file pointer back to the beginning
  rewind(f);
  // fseek(f, 0, SEEK_SET);
  return 1;
}

int obudec_read_temporal_unit(struct ObuDecInputContext *obu_ctx,
                              uint8_t **buffer, size_t *bytes_read,
                              size_t *buffer_size) {
  FILE *f = obu_ctx->avx_ctx->file;
  if (!f) return -1;

  *buffer_size = 0;
  *bytes_read = 0;

  if (feof(f)) {
    return 1;
  }

  size_t tu_size = 0;
  unsigned long fpos = ftell(f);
  uint8_t detect_buf[OBU_DETECTION_SIZE] = { 0 };
  int first_td = 1;
#if OBU_ORDER_IN_TU
  OBU_TYPE prev_obu_type = 0;
  OBU_TYPE curr_obu_type = 0;
#endif  // OBU_ORDER_IN_TU
  while (1) {
    ObuHeader obu_header;
    memset(&obu_header, 0, sizeof(obu_header));
    uint64_t obu_size = 0;
    size_t obu_size_bytelength = 0;
    if (obu_ctx->is_annexb) {
      if (obudec_read_leb128(f, &detect_buf[0], &obu_size_bytelength,
                             &obu_size) != 0) {
        fprintf(stderr, "obudec: Failure reading frame unit header\n");
      } else if (feof(f)) {
        if (tu_size == 0)
          return 1;
        else
          break;
      }
    }

    const int read_status =
        peek_obu_from_file(f, OBU_HEADER_SIZE, &detect_buf[0], &obu_header);
    if (read_status == -2) {
      return -1;
    } else if (read_status == -1) {
      // end of file
      return 1;
    }
#if OBU_ORDER_IN_TU
    curr_obu_type = obu_header.type;
    if (prev_obu_type > 0 && curr_obu_type > 0 &&
        check_obu_order(prev_obu_type, curr_obu_type)) {
      fprintf(stderr, "obudec: OBU orders is incorrect in TU, %d, %d\n",
              prev_obu_type, curr_obu_type);
      return -1;
    }
    prev_obu_type = curr_obu_type;
#endif  // OBU_ORDER_IN_TU

    if ((obu_header.type == OBU_TEMPORAL_DELIMITER && first_td != 1)) {
      break;
    } else {
      if (obu_header.type == OBU_TEMPORAL_DELIMITER) first_td = 0;
      fseek(f, obu_size, SEEK_CUR);
      tu_size += (obu_size + obu_size_bytelength);
    }
  }  // while
  fseek(f, fpos, SEEK_SET);

#if defined AOM_MAX_ALLOCABLE_MEMORY
  if (tu_size > AOM_MAX_ALLOCABLE_MEMORY) {
    fprintf(stderr, "obudec: Temporal Unit size exceeds max alloc size.\n");
    return -1;
  }
#endif
  if (tu_size > 0) {
    uint8_t *new_buffer = (uint8_t *)realloc(*buffer, tu_size);
    if (!new_buffer) {
      free(*buffer);
      fprintf(stderr, "obudec: Out of memory.\n");
      return -1;
    }
    *buffer = new_buffer;
  }
  *bytes_read = tu_size;
  *buffer_size = tu_size;

  if (!feof(f)) {
    // save from frame unit size
    if (fread(*buffer, sizeof(uint8_t), tu_size, f) != tu_size) {
      fprintf(stderr, "obudec: Failed to read full temporal unit\n");
      return -1;
    }
  }
  return 0;
}

void obudec_free(struct ObuDecInputContext *obu_ctx) { free(obu_ctx->buffer); }
