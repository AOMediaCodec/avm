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
#include "common/tools_common.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/mem_ops.h"
#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/enums.h"

/*!\brief Maximum OBU header size in bytes. */
#define OBU_HEADER_SIZE 1
#define OBU_EXTENSION_SIZE 1
#define OBU_MAX_LENGTH_FIELD_SIZE 8

#define OBU_DETECTION_SIZE \
  (OBU_HEADER_SIZE + OBU_EXTENSION_SIZE + OBU_MAX_LENGTH_FIELD_SIZE)

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

// Returns the OBU header size (1 or 2) on success. Returns -1 on EOF. Returns
// -2 on error.
static int read_obu_header_from_file(FILE *f, size_t obu_size, uint8_t *buffer,
                                     ObuHeader *obu_header) {
  if (!f) {
    return -2;
  }

  if (obu_size < 1) {
    return -2;
  }
  size_t bytes_read = fread(&buffer[0], sizeof(uint8_t), 1, f);
  if (feof(f) && bytes_read == 0) {
    return -1;
  } else if (bytes_read != 1) {
    fprintf(stderr, "obudec: Failure reading OBU header.\n");
    return -2;
  }

  obu_header->obu_extension_flag = (buffer[0] >> 7) & 1;  // obu_extension_flag
  obu_header->type = (buffer[0] >> 2) & 31;               // obu_type
  obu_header->obu_tlayer_id = (buffer[0]) & 3;            // obu_temporal
  if (obu_header->obu_extension_flag) {
    if (obu_size < 2) {
      return -2;
    }
    bytes_read = fread(&buffer[1], sizeof(uint8_t), 1, f);
    if (bytes_read != 1) {
      fprintf(stderr, "obudec: Failure reading OBU extension header.\n");
      return -2;
    }

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

  return obu_header->obu_extension_flag ? 2 : 1;
}
#if CONFIG_F024_KEYOBU
static int is_single_tile_vcl_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_REGULAR_SEF || obu_type == OBU_LEADING_SEF ||
         obu_type == OBU_REGULAR_TIP || obu_type == OBU_LEADING_TIP ||
         obu_type == OBU_BRIDGE_FRAME;
}
static int is_multi_tile_vcl_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_REGULAR_TILE_GROUP ||
         obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_SWITCH ||
         obu_type == OBU_RAS_FRAME || obu_type == OBU_CLK ||
         obu_type == OBU_OLK;
}
#endif  // CONFIG_F024_KEYOBU

static int peek_obu_from_file(FILE *f, size_t obu_size, uint8_t *buffer,
                              ObuHeader *obu_header
#if CONFIG_F160_TD && CONFIG_F106_OBU_TILEGROUP
                              ,
                              uint8_t *first_tile_group
#endif
) {
  if (!f) {
    return -2;
  }
  FileOffset fpos = ftello(f);
  const int obu_header_size =
      read_obu_header_from_file(f, obu_size, buffer, obu_header);
  if (obu_header_size == -2) {
    fprintf(stderr, "obudec: Failure peeking OBU header.\n");
    return -2;
  } else if (obu_header_size == -1) {
    // bytes_read is already 0
    return -1;
  }
#if CONFIG_F160_TD && CONFIG_F106_OBU_TILEGROUP
  // TODO(any): The `if` and `else if` conditions below combined are same
  // as the condition used in 2 places with TODOs below. Need to refactor
  // after macros are cleaned up.
#if CONFIG_F024_KEYOBU
  if (is_multi_tile_vcl_obu(obu_header->type))
#else
  if (obu_header->type == OBU_TILE_GROUP || obu_header->type == OBU_SWITCH
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      || obu_header->type == OBU_RAS_FRAME
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  )
#endif  // CONFIG_F024_KEYOBU
  {
    if (obu_size < (size_t)obu_header_size + 1) {
      return -2;
    }
    size_t bytes_read = fread(&buffer[obu_header_size], sizeof(uint8_t), 1, f);
    if (bytes_read != 1) {
      fprintf(stderr, "obudec: Failure reading first tile group byte.\n");
      return -2;
    }
    *first_tile_group = buffer[obu_header_size];
  } else if (
#if CONFIG_F024_KEYOBU
      is_single_tile_vcl_obu(obu_header->type)
#else
      obu_header->type == OBU_TIP || obu_header->type == OBU_SEF
#if CONFIG_CWG_F317
      || obu_header->type == OBU_BRIDGE_FRAME
#endif  // CONFIG_CWG_F317
#endif  // CONFIG_F024_KEYOBU
  ) {
    *first_tile_group = 1;
  } else {
    *first_tile_group = 0;
  }
#endif
  if (fseeko(f, fpos, SEEK_SET) != 0) {
    fprintf(stderr, "obudec: Failure restoring file position indicator.\n");
    return -2;
  }
  return 0;
}

int file_is_obu(struct ObuDecInputContext *obu_ctx) {
  if (!obu_ctx || !obu_ctx->avx_ctx) return 0;
#if CONFIG_F160_TD
  obu_ctx->has_temporal_delimiter = 0;
#endif  // CONFIG_F160_TD
  struct AvxInputContext *avx_ctx = obu_ctx->avx_ctx;
  uint8_t detect_buf[OBU_DETECTION_SIZE] = { 0 };
  FILE *f = avx_ctx->file;

  if (!f) {
    return 0;
  }

  while (1) {
    {
      size_t obu_size_bytelength = 0;
      uint64_t obu_size = 0;
      if (obudec_read_leb128(f, &detect_buf[0], &obu_size_bytelength,
                             &obu_size) != 0) {
        fprintf(stderr, "file_type: Failure reading obu size\n");
        rewind(f);
        return 0;
      } else if (feof(f)) {
        break;
      }
      ObuHeader obu_header;
      memset(&obu_header, 0, sizeof(obu_header));
      const int obu_header_size = read_obu_header_from_file(
          f, (size_t)obu_size, &detect_buf[0], &obu_header);
      if (obu_header_size == -2) {
        fprintf(stderr, "file_type: Failure reading an OBU.\n");
        rewind(f);
        return 0;
      } else if (obu_header_size == -1) {  // end of file
        break;
      }
#if CONFIG_F160_TD
      if (obu_header.type == OBU_TEMPORAL_DELIMITER)
        obu_ctx->has_temporal_delimiter = 1;
#endif  // CONFIG_F160_TD
      if (fseeko(f, (FileOffset)obu_size - obu_header_size, SEEK_CUR) != 0) {
        fprintf(stderr, "file_type: Failure seeking to end of OBU.\n");
        rewind(f);
        return 0;
      }
    }
  }  // while

  // move the file pointer back to the beginning
  rewind(f);
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
  FileOffset fpos = ftello(f);
  uint8_t detect_buf[OBU_DETECTION_SIZE] = { 0 };
  int first_td = 1;

#if CONFIG_F160_TD
  int vcl_obu_count = 0;  // a local variable to count the nubmer of obus
#endif                    // CONFIG_F160_TD
  while (1) {
    ObuHeader obu_header;
    memset(&obu_header, 0, sizeof(obu_header));
    uint64_t obu_size = 0;
    size_t obu_size_bytelength = 0;
    if (obudec_read_leb128(f, &detect_buf[0], &obu_size_bytelength,
                           &obu_size) != 0) {
      fprintf(stderr, "obudec: Failure reading frame unit header\n");
    } else if (feof(f)) {
      if (tu_size == 0)
        return 1;
      else
        break;
    }
#if CONFIG_F160_TD && CONFIG_F106_OBU_TILEGROUP
    uint8_t first_tile_group_byte = 0;
#endif
    const int read_status =
        peek_obu_from_file(f, (size_t)obu_size, &detect_buf[0], &obu_header
#if CONFIG_F160_TD && CONFIG_F106_OBU_TILEGROUP
                           ,
                           &first_tile_group_byte
#endif
        );
    if (read_status == -2) {
      return -1;
    } else if (read_status == -1) {
      // end of file
      return 1;
    }

#if CONFIG_F160_TD
    // A data chunk before next decoding_unit_token is read from the file to
    // buffer to be decoded.
    int decoding_unit_token =
        (obu_header.type == OBU_TEMPORAL_DELIMITER && first_td != 1);
    if (!obu_ctx->has_temporal_delimiter) {
#if CONFIG_F106_OBU_TILEGROUP
      int first_tile_group_in_frame =
#if CONFIG_F024_KEYOBU
          is_multi_tile_vcl_obu(obu_header.type)
#else
          obu_header.type == OBU_TILE_GROUP
#endif  // CONFIG_F024_KEYOBU
              ? ((first_tile_group_byte >> 7) & 1u)
              : first_tile_group_byte;
#endif  // CONFIG_F106_OBU_TILEGROUP
      // TODO(any): OBU header type condition is almost same as
      // `is_coded_frame`, except `type == OBU_BRIDGE_FRAME` condition. Need to
      // refactor after macros are cleaned up.
      decoding_unit_token =
          ((vcl_obu_count > 0 &&
#if CONFIG_F024_KEYOBU
            (is_multi_tile_vcl_obu(obu_header.type) ||
             is_single_tile_vcl_obu(obu_header.type))
#else
            (obu_header.type == OBU_TILE_GROUP || obu_header.type == OBU_SEF ||
             obu_header.type == OBU_TIP || obu_header.type == OBU_SWITCH
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
             || obu_header.type == OBU_RAS_FRAME
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
             )
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F106_OBU_TILEGROUP
            && first_tile_group_in_frame
#endif
            )
#if !CONFIG_F106_OBU_TILEGROUP
           || (vcl_obu_count > 0 && obu_header.type == OBU_FRAME_HEADER)
#endif
           || (vcl_obu_count > 0 && obu_header.type == OBU_SEQUENCE_HEADER));
    }
    if (decoding_unit_token)
#else
    if ((obu_header.type == OBU_TEMPORAL_DELIMITER && first_td != 1))
#endif  // CONFIG_F160_TD
    {
      break;
    } else {
      if (obu_header.type == OBU_TEMPORAL_DELIMITER) first_td = 0;
#if CONFIG_F160_TD
#if CONFIG_F106_OBU_TILEGROUP
#if CONFIG_F024_KEYOBU
      if (is_multi_tile_vcl_obu(obu_header.type) ||
          obu_header.type == OBU_REGULAR_SEF ||
          obu_header.type == OBU_LEADING_SEF ||
          obu_header.type == OBU_REGULAR_TIP ||
          obu_header.type == OBU_LEADING_TIP)
#else
      if (obu_header.type == OBU_TILE_GROUP || obu_header.type == OBU_SEF ||
          obu_header.type == OBU_TIP || obu_header.type == OBU_SWITCH
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
          || obu_header.type == OBU_RAS_FRAME
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      )
#endif  // CONFIG_F024_KEYOBU
#else   // CONFIG_F106_OBU_TILEGROUP
      if (obu_header.type == OBU_FRAME || obu_header.type == OBU_FRAME_HEADER ||
          obu_header.type == OBU_REDUNDANT_FRAME_HEADER)
#endif  // CONFIG_F106_OBU_TILEGROUP
        vcl_obu_count++;
#endif  // CONFIG_F160_TD
      if (fseeko(f, (FileOffset)obu_size, SEEK_CUR) != 0) {
        fprintf(stderr, "obudec: Failure seeking to end of OBU.\n");
        return -1;
      }
      tu_size += (obu_size + obu_size_bytelength);
    }
  }  // while
  if (fseeko(f, fpos, SEEK_SET) != 0) {
    fprintf(stderr, "obudec: Failure restoring file position indicator.\n");
    return -1;
  }

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
