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

#ifndef AVM_AV2_ENCODER_GDR_UTILS_H_
#define AVM_AV2_ENCODER_GDR_UTILS_H_

#include "av2/common/enums.h"
#include "av2/common/reconintra.h"
#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Columns of forced-intra overlap added right of the logical stripe end so the
// in-loop filters near the boundary only read deterministic samples.
#define GDR_LF_OVERLAP_MI 8

// Worst-case trailing reach of inter prediction past the reference block edge
// (8-tap interpolation, sub-block predictor extension, DMVR refinement).
#define GDR_MV_BOUNDARY_GUARD_PX \
  (AVM_INTERP_EXTEND + SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)

// Column mi_col lies in the dirty area of the current frame.
static AVM_INLINE int gdr_sb_is_dirty(const AV2_COMP *cpi, int mi_col) {
  return mi_col >= cpi->gdr_deterministic_right_mi;
}

// A block overlapping the forced-intra region (stripe + loop-filter overlap)
// must be coded intra-only. Active on every window frame, incl. the switch
// frame.
static AVM_INLINE int gdr_force_intra_block(const AV2_COMP *cpi, int mi_col,
                                            int bw_mi) {
  return cpi->gdr_stripe_active && mi_col < cpi->gdr_stripe_end_mi &&
         mi_col + bw_mi > cpi->gdr_stripe_start_mi;
}

// Above-right intra fetch of a block reads into the dirty area. The top picture
// row is exempt: its above-right is duplicated, not read from a neighbor.
static AVM_INLINE int gdr_intra_tr_into_dirty(const AV2_COMP *cpi, int mi_row,
                                              int mi_col, int bw_mi) {
  return mi_row > 0 && gdr_sb_is_dirty(cpi, mi_col + bw_mi);
}

// Intra luma modes that read above-right neighbor samples (directly, via the
// wide-angle remap of non-square blocks, or via the directional edge filter).
static AVM_INLINE int gdr_mode_uses_above_right(PREDICTION_MODE mode) {
  return av2_is_directional_mode(mode) || mode == SMOOTH_PRED ||
         mode == SMOOTH_H_PRED;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_GDR_UTILS_H_
