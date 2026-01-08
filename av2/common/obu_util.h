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
#ifndef AVM_AV2_COMMON_OBU_UTIL_H_
#define AVM_AV2_COMMON_OBU_UTIL_H_

#include "avm/avm_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  size_t size;  // Size (1 or 2 bytes) of the OBU header (including the
                // optional second byte) in the bitstream.
  int obu_extension_flag;
  OBU_TYPE type;
  int obu_tlayer_id;
  int obu_mlayer_id;  // same as spatial_layer_id in the old design
  int obu_xlayer_id;
} ObuHeader;

avm_codec_err_t avm_read_obu_header_and_size(const uint8_t *data,
                                             size_t bytes_available,
                                             ObuHeader *obu_header,
                                             size_t *const payload_size,
                                             size_t *const bytes_read);

#if CONFIG_F024_KEYOBU
/*!\brief Returns 1 when the one tilegroup is allowed for the obu_type
 */
static INLINE int is_single_tile_vcl_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_REGULAR_SEF || obu_type == OBU_LEADING_SEF ||
         obu_type == OBU_REGULAR_TIP || obu_type == OBU_LEADING_TIP ||
         obu_type == OBU_BRIDGE_FRAME;
}

/*!\brief Returns 1 when the multiple tilegroup is allowed for the obu_type
 */
static INLINE int is_multi_tile_vcl_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_REGULAR_TILE_GROUP ||
         obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_SWITCH ||
         obu_type == OBU_RAS_FRAME || obu_type == OBU_CLK ||
         obu_type == OBU_OLK;
}

/*!\brief Returns 1 when the obu is non vcl obu and can lead a temporal unit
 */
static INLINE int is_tu_head_non_vcl_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_SEQUENCE_HEADER ||
         obu_type == OBU_TEMPORAL_DELIMITER ||
         obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
         obu_type == OBU_ATLAS_SEGMENT || obu_type == OBU_OPERATING_POINT_SET ||
         obu_type == OBU_MSDO;
}
#endif  // CONFIG_F024_KEYOBU

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_OBU_UTIL_H_
