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
#include <assert.h>

#include "av1/common/obu_util.h"

#include "config/aom_config.h"

#include "aom_dsp/bitreader_buffer.h"
#include "av1/common/enums.h"

// Returns 1 when OBU type is valid, and 0 otherwise.
static int valid_obu_type(int obu_type) {
  int valid_type = 0;
  switch (obu_type) {
    case OBU_SEQUENCE_HEADER:
    case OBU_TEMPORAL_DELIMITER:
#if CONFIG_MULTI_FRAME_HEADER
    case OBU_MULTI_FRAME_HEADER:
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_F106_OBU_TILEGROUP
#if CONFIG_F106_OBU_SWITCH
    case OBU_SWITCH:
#endif  // CONFIG_F106_OBU_SWITCH
#if CONFIG_F106_OBU_SEF
    case OBU_SEF:
#endif  // CONFIG_F106_OBU_SEF
#if CONFIG_F106_OBU_TIP
    case OBU_TIP:
#endif  // CONFIG_F106_OBU_TIP
    case OBU_TILE_GROUP:
#else
    case OBU_FRAME_HEADER:
    case OBU_TILE_GROUP:
#endif  // CONFIG_F106_OBU_TILEGROUP
    case OBU_METADATA:
#if !CONFIG_F106_OBU_TILEGROUP
    case OBU_FRAME:
#if !CONFIG_REMOVAL_REDUNDANT_FRAME_HEADER
    case OBU_REDUNDANT_FRAME_HEADER:
#endif  // !CONFIG_REMOVAL_REDUNDANT_FRAME_HEADER
#endif  // !CONFIG_F106_OBU_TILEGROUP
#if CONFIG_MULTILAYER_HLS
    case OBU_LAYER_CONFIGURATION_RECORD:
    case OBU_ATLAS_SEGMENT:
    case OBU_OPERATING_POINT_SET:
#endif  // CONFIG_MULTILAYER_HLS
#if CONFIG_CWG_F317
    case OBU_BRIDGE_FRAME:
#endif  // CONFIG_CWG_F317
    case OBU_PADDING: valid_type = 1; break;
    default: break;
  }
  return valid_type;
}

static aom_codec_err_t read_obu_size(const uint8_t *data,
                                     size_t bytes_available,
                                     size_t *const obu_size,
                                     size_t *const length_field_size) {
  uint64_t u_obu_size = 0;
  if (aom_uleb_decode(data, bytes_available, &u_obu_size, length_field_size) !=
      0) {
    return AOM_CODEC_CORRUPT_FRAME;
  }

  if (u_obu_size > UINT32_MAX) return AOM_CODEC_CORRUPT_FRAME;
  *obu_size = (size_t)u_obu_size;
  return AOM_CODEC_OK;
}

// Parses OBU header and stores values in 'header'.
static aom_codec_err_t read_obu_header(struct aom_read_bit_buffer *rb,
                                       int is_annexb, ObuHeader *header) {
  if (!rb || !header) return AOM_CODEC_INVALID_PARAM;

  const ptrdiff_t bit_buffer_byte_length = rb->bit_buffer_end - rb->bit_buffer;

  (void)is_annexb;
  if (bit_buffer_byte_length < 1) return AOM_CODEC_CORRUPT_FRAME;
  header->size = 1;

  header->obu_extension_flag = aom_rb_read_bit(rb);
  header->type = (OBU_TYPE)aom_rb_read_literal(rb, 5);  // obu_type
  if (!valid_obu_type(header->type)) return AOM_CODEC_CORRUPT_FRAME;

  header->obu_tlayer_id = aom_rb_read_literal(rb, TLAYER_BITS);

  if (header->obu_extension_flag) {
    if (bit_buffer_byte_length == 1) return AOM_CODEC_CORRUPT_FRAME;
    header->size += 1;

    header->obu_mlayer_id = aom_rb_read_literal(rb, MLAYER_BITS);
    header->obu_xlayer_id = aom_rb_read_literal(rb, XLAYER_BITS);
  } else {
    header->obu_mlayer_id = 0;
    header->obu_xlayer_id = 0;
  }

  return AOM_CODEC_OK;
}

aom_codec_err_t aom_read_obu_header_and_size(const uint8_t *data,
                                             size_t bytes_available,
                                             int is_annexb,
                                             ObuHeader *obu_header,
                                             size_t *const payload_size,
                                             size_t *const bytes_read) {
  size_t length_field_size_obu = 0;
  size_t obu_size = 0;
  aom_codec_err_t status;

  if (is_annexb) {
    // Size field comes before the OBU header, and includes the OBU header
    status =
        read_obu_size(data, bytes_available, &obu_size, &length_field_size_obu);

    if (status != AOM_CODEC_OK) return status;
  }

  struct aom_read_bit_buffer rb = { data + length_field_size_obu,
                                    data + bytes_available, 0, NULL, NULL };

  status = read_obu_header(&rb, is_annexb, obu_header);
  if (status != AOM_CODEC_OK) return status;

  // Derive the payload size from the data we've already read
  if (obu_size < obu_header->size) return AOM_CODEC_CORRUPT_FRAME;
  *payload_size = obu_size - obu_header->size;
  *bytes_read = length_field_size_obu + obu_header->size;

  return AOM_CODEC_OK;
}
