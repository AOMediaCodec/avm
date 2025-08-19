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
#ifndef AOM_AV1_COMMON_OBU_UTIL_H_
#define AOM_AV1_COMMON_OBU_UTIL_H_

#include "aom/aom_codec.h"
#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
#if CONFIG_NEW_OBU_HEADER
  size_t size;  // Size (1 or 2 bytes) of the OBU header (including the
                // optional second byte) in the bitstream.
#else
  size_t size;  // Size (1 or 2 bytes) of the OBU header (including the
                // optional OBU extension header) in the bitstream.
#endif  // CONFIG_NEW_OBU_HEADER
#if CONFIG_NEW_OBU_HEADER
  int obu_extension_flag;
#endif  // CONFIG_NEW_OBU_HEADER
  OBU_TYPE type;
#if CONFIG_NEW_OBU_HEADER
  int obu_tlayer_id;
  int obu_mlayer_id;  // same as spatial_layer_id in the old design
  int obu_xlayer_id;
#else
  int has_size_field;
  int has_extension;
  // The following fields come from the OBU extension header and therefore are
  // only used if has_extension is true.
  int temporal_layer_id;
  int spatial_layer_id;
#endif  // CONFIG_NEW_OBU_HEADER
} ObuHeader;

#if !CONFIG_NEW_OBU_HEADER
aom_codec_err_t aom_read_obu_header(uint8_t *buffer, size_t buffer_length,
                                    size_t *consumed, ObuHeader *header,
                                    int is_annexb);
#endif  // !CONFIG_NEW_OBU_HEADER

aom_codec_err_t aom_read_obu_header_and_size(const uint8_t *data,
                                             size_t bytes_available,
                                             int is_annexb,
                                             ObuHeader *obu_header,
                                             size_t *const payload_size,
                                             size_t *const bytes_read);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_OBU_UTIL_H_
