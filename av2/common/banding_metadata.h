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

#ifndef AVM_AV2_COMMON_BANDING_METADATA_H_
#define AVM_AV2_COMMON_BANDING_METADATA_H_

#include "avm/avm_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BAND_COMPONENTS 3
#define MAX_BAND_UNITS_ROWS 32
#define MAX_BAND_UNITS_COLS 32

/*!\brief Banding hints metadata structure */
typedef struct avm_banding_hints_metadata {
  uint8_t coding_banding_present_flag;
  uint8_t source_banding_present_flag;

  // Optional banding hints information
  uint8_t banding_hints_flag;
  uint8_t three_color_components;

  // Per-component banding information
  uint8_t banding_in_component_present_flag[MAX_BAND_COMPONENTS];
  uint8_t max_band_width_minus4[MAX_BAND_COMPONENTS];  // 6 bits
  uint8_t max_band_step_minus1[MAX_BAND_COMPONENTS];   // 4 bits

  // Band units information
  uint8_t band_units_information_present_flag;
  uint8_t num_band_units_rows_minus_1;  // 5 bits
  uint8_t num_band_units_cols_minus_1;  // 5 bits
  uint8_t varying_size_band_units_flag;
  uint8_t band_block_in_luma_samples;  // 3 bits

  // Variable size band units
  uint8_t vert_size_in_band_blocks_minus1[MAX_BAND_UNITS_ROWS];  // 5 bits each
  uint8_t horz_size_in_band_blocks_minus1[MAX_BAND_UNITS_COLS];  // 5 bits each

  // Per-tile banding flags
  uint8_t banding_in_band_unit_present_flag[MAX_BAND_UNITS_ROWS]
                                           [MAX_BAND_UNITS_COLS];
} avm_banding_hints_metadata_t;

/*!\brief Encode banding hints metadata to payload
 *
 * \param[in]     metadata        Banding hints metadata structure
 * \param[out]    payload         Output payload buffer
 * \param[in,out] payload_size    Input: buffer size, Output: actual payload
 * size
 *
 * \return 0 on success, -1 on error
 */
int avm_encode_banding_hints_metadata(
    const avm_banding_hints_metadata_t *metadata, uint8_t *payload,
    size_t *payload_size);

/*!\brief Decode banding hints metadata from payload
 *
 * \param[in]     payload         Input payload buffer
 * \param[in]     payload_size    Payload size
 * \param[out]    metadata        Decoded banding hints metadata structure
 *
 * \return 0 on success, -1 on error
 */
int avm_decode_banding_hints_metadata(const uint8_t *payload,
                                      size_t payload_size,
                                      avm_banding_hints_metadata_t *metadata);

/*!\brief Add banding hints metadata to an image
 *
 * \param[in,out] img               Image to add metadata to
 * \param[in]     banding_metadata  Banding hints metadata structure
 * \param[in]     insert_flag       When to insert the metadata
 *
 * \return 0 on success, -1 on error
 */
int avm_img_add_banding_hints_metadata(
    avm_image_t *img, const avm_banding_hints_metadata_t *banding_metadata,
    avm_metadata_insert_flags_t insert_flag);

// Forward declaration for encoder
struct AV2_COMP;

/*!\brief Write banding hints metadata to bitstream
 *
 * \param[in]     cpi               Encoder context
 * \param[out]    dst               Output buffer
 * \param[in]     banding_metadata  Banding hints metadata structure
 *
 * \return Number of bytes written, 0 on error
 */
size_t av2_write_banding_hints_metadata(
    struct AV2_COMP *const cpi, uint8_t *dst,
    const avm_banding_hints_metadata_t *const banding_metadata);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AV2_COMMON_BANDING_METADATA_H_
