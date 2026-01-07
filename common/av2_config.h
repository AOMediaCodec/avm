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
#ifndef AVM_COMMON_AV2_CONFIG_H_
#define AVM_COMMON_AV2_CONFIG_H_

#include "avm/avm_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Struct representing ISOBMFF/Matroska AV2 config. See:
// https://aomediacodec.github.io/av2-isobmff/#av2codecconfigurationbox-syntax
//
// The AV2 config has the following format:
//
// unsigned int (1) marker = 1;
// unsigned int (7) version = 1;
// unsigned int (3) seq_profile;
// unsigned int (5) seq_level_idx_0;
// unsigned int (1) seq_tier_0;
// unsigned int (2) bitdepth_idx;
// unsigned int (1) monochrome;
// unsigned int (1) chroma_subsampling_x;
// unsigned int (1) chroma_subsampling_y;
// unsigned int (3) chroma_sample_position;
// unsigned int (2) reserved = 0;
//
// unsigned int (1) initial_presentation_delay_present;
// if (initial_presentation_delay_present) {
//   unsigned int (4) initial_presentation_delay_minus_one;
// } else {
//   unsigned int (4) reserved = 0;
// }
//
// unsigned int (8)[] configOBUs;
//
// Note: get_av2config_from_obu() does not currently store 'configOBUs' data, so
// the field is omitted.
typedef struct _Av2Config {
  uint8_t marker;
  uint8_t version;
  uint8_t seq_header_id;
  uint8_t seq_lcr_id;
  uint8_t seq_profile;
  uint8_t seq_level_idx_0;
  uint8_t seq_tier_0;
  uint8_t bitdepth_idx;
  uint8_t monochrome;
  uint8_t chroma_subsampling_x;
  uint8_t chroma_subsampling_y;
  int conf_win_enabled_flag;
  int conf_win_left_offset;
  int conf_win_right_offset;
  int conf_win_top_offset;
  int conf_win_bottom_offset;
  uint8_t chroma_sample_position;
  uint8_t initial_presentation_delay_present;
  uint8_t initial_presentation_delay_minus_one;
  // Content interpretation OBU parameters
  uint8_t ci_present;
  uint8_t ci_scan_type_idc;
  uint8_t ci_color_description_present_flag;
  uint8_t ci_chroma_sample_position_present_flag;
  uint8_t ci_aspect_ratio_info_present_flag;
  uint8_t ci_timing_info_present_flag;
  // Color information
  uint8_t ci_color_primaries;
  uint8_t ci_matrix_coefficients;
  uint8_t ci_transfer_characteristics;
  uint8_t ci_full_range_flag;
  // Chroma sample position from CI
  uint8_t ci_chroma_sample_position_1;
  // Sample aspect ratio
  uint8_t ci_sar_aspect_ratio_idc;
  uint8_t ci_sar_width;
  uint8_t ci_sar_height;
} Av2Config;

// Attempts to parse a Sequence Header OBU and set the paramenters of 'config'.
// Returns 0 upon success, and -1 upon failure. 'buffer' can contain multiple
// OBUs, but the Sequence Header OBU must be the first OBU within the buffer.
int get_av2config_from_obu(const uint8_t *buffer, size_t length,
                           Av2Config *config);

// Attempts to parse an AV2 config from 'buffer'. Returns 0 upon success.
// Returns -1 when 'buffer_length' is less than 4, when passed NULL pointers, or
// when parsing of 'buffer' fails.
int read_av2config(const uint8_t *buffer, size_t buffer_length,
                   size_t *bytes_read, Av2Config *config);

// Writes 'config' to 'buffer'. Returns 0 upon successful write to 'buffer'.
// Returns -1 when passed NULL pointers or when 'capacity' insufficient.
int write_av2config(const Av2Config *config, size_t capacity,
                    size_t *bytes_written, uint8_t *buffer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // AVM_COMMON_AV2_CONFIG_H_
