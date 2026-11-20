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

#ifndef AVM_AV2_COMMON_LEVEL_H_
#define AVM_AV2_COMMON_LEVEL_H_

#include "av2/common/av2_common_int.h"

// AV2 Level Specifications
typedef struct {
  AV2_LEVEL level;
  int max_picture_size;
  int max_h_size;
  int max_v_size;
  int max_header_rate;
  int max_tile_rate;
  int max_tiles;
  int max_tile_cols;
  int64_t max_display_rate;
  int64_t max_decode_rate;
  double main_mbps;
  double high_mbps;
  double main_cr;
  double high_cr;
} AV2LevelSpec;

extern const AV2LevelSpec av2_level_defs[SEQ_LEVELS];

// AV2 Substream Level Specifications
typedef struct {
  int max_picture_size;
  int max_picture_size_x;
  double scale_factor_x;  // This is present here to align with the table in
                          // Specification text. This one is derived from the
                          // bitstream
  int max_v_size_x;
  int max_h_size_x;
  int max_tile_cols_x;
  int max_header_rate_x;
} AV2SubstreamLevelSpec;

// Validated read-only Annex A lookups used by the decoder-model verifier.
int av2_get_level_compression_basis(int level_index, int tier,
                                    uint32_t *compression_basis);
int av2_get_substream_level_spec(int level_index, uint32_t scale_numerator,
                                 uint32_t scale_denominator,
                                 AV2SubstreamLevelSpec *level_spec);

// Returns a borrowed row for the encoder's legacy index mapping. The index
// must identify an existing row in the Annex A substream table.
const AV2SubstreamLevelSpec *av2_get_substream_level_spec_by_index(int index);

bool is_filter_enabled_frame(const AV2_COMMON *const cm);

// Return maximum legal DPB size defined by the level.
int av2_get_max_level_ref_frames(const AV2_COMMON *const cm, OBU_TYPE obu_type,
                                 AV2_LEVEL level_index);

#endif  // AVM_AV2_COMMON_LEVEL_H_
