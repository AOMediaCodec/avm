/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license.
 */

#ifndef AOM_AV2_ENCODER_PARTITION_SEARCH_RULE_H_
#define AOM_AV2_ENCODER_PARTITION_SEARCH_RULE_H_

#include "av2/common/enums.h"

/* Intra partition-prune helpers for part_rule_pruning. */

static AVM_INLINE int part_rule_qp_band(int qindex) {
  /* Nearest-center classification; centers at {85,110,135,160,185,210};
   * midpoints at 97,122,147,172,197. Returns 1..6. */
  if (qindex < 97) return 1;
  if (qindex < 122) return 2;
  if (qindex < 147) return 3;
  if (qindex < 172) return 4;
  if (qindex < 197) return 5;
  return 6;
}

static AVM_INLINE int part_rule_is_4way_intra_bsize(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_8X8:
    case BLOCK_4X16:
    case BLOCK_16X4:
    case BLOCK_8X16:
    case BLOCK_16X8:
    case BLOCK_8X32:
    case BLOCK_32X8:
    case BLOCK_16X16:
    case BLOCK_16X32:
    case BLOCK_32X16:
    case BLOCK_32X32:
    case BLOCK_16X64:
    case BLOCK_64X16:
    case BLOCK_32X64:
    case BLOCK_64X32:
    case BLOCK_64X64: return 1;
    default: return 0;
  }
}

static AVM_INLINE int part_rule_is_3way_intra_bsize(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_8X16:
    case BLOCK_16X8:
    case BLOCK_8X32:
    case BLOCK_32X8:
    case BLOCK_16X16:
    case BLOCK_16X32:
    case BLOCK_32X16:
    case BLOCK_32X32:
    case BLOCK_16X64:
    case BLOCK_64X16:
    case BLOCK_32X64:
    case BLOCK_64X32:
    case BLOCK_64X64: return 1;
    default: return 0;
  }
}

static AVM_INLINE int part_rule_split_bsize_idx(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_64X64: return 0;
    case BLOCK_32X32: return 1;
    case BLOCK_32X64:
    case BLOCK_64X32: return 2;
    default: return -1;
  }
}

/* SPLIT-prune thresholds indexed [bsize_idx][qp_band - 1]. 10-bit only; no
 * 8-bit coverage (unlike RECT which covers both bd10 and bd8). Hook guarded
 * on xd->bd == 10 in partition_search.c. */
extern const int64_t part_rule_split_thr_bd10[3][6];

/* Per-qindex RECT-prune LUT tables indexed by (bsize, bitdepth).
 * Each is a 256-entry array indexed directly by qindex; values log-linearly
 * interpolated from calibration samples; clamped at endpoints outside the
 * sampled range. A threshold of 0 disables the prune at that qindex. */
#define PART_RULE_LUT_QINDEX_COUNT 256

extern const int64_t part_rule_rect_lut_bd10_32x32[PART_RULE_LUT_QINDEX_COUNT];

extern const int64_t part_rule_rect_lut_bd10_64x64[PART_RULE_LUT_QINDEX_COUNT];

extern const int64_t part_rule_rect_lut_bd10_32x64[PART_RULE_LUT_QINDEX_COUNT];

extern const int64_t part_rule_rect_lut_bd10_64x32[PART_RULE_LUT_QINDEX_COUNT];

extern const int64_t part_rule_rect_lut_bd8_32x32[PART_RULE_LUT_QINDEX_COUNT];

/* Returns the RECT-prune threshold for the given (bsize, bitdepth, qindex)
 * cohort. Returns 0 when the prune is disabled for that cohort, which the
 * hook interprets as 'do not prune'. */
static AVM_INLINE int64_t part_rule_rect_lut_lookup(BLOCK_SIZE bsize,
                                                    int bitdepth, int qindex) {
  if (qindex < 0 || qindex >= PART_RULE_LUT_QINDEX_COUNT) return 0;
  if (bitdepth == 10) {
    switch (bsize) {
      case BLOCK_32X32: return part_rule_rect_lut_bd10_32x32[qindex];
      case BLOCK_64X64: return part_rule_rect_lut_bd10_64x64[qindex];
      case BLOCK_32X64: return part_rule_rect_lut_bd10_32x64[qindex];
      case BLOCK_64X32: return part_rule_rect_lut_bd10_64x32[qindex];
      default: return 0;
    }
  } else if (bitdepth == 8 && bsize == BLOCK_32X32) {
    return part_rule_rect_lut_bd8_32x32[qindex];
  }
  return 0;
}

#endif  // AOM_AV2_ENCODER_PARTITION_SEARCH_RULE_H_
