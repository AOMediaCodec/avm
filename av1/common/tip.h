/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.
 * If the Alliance for Open Media Patent License 1.0 was not distributed with
 * this source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_COMMON_TIP_H_
#define AOM_AV1_COMMON_TIP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/common/av1_common_int.h"
#include "av1/common/mvref_common.h"
#include "av1/common/reconinter.h"

#define TIP_RD_CORRECTION 100000

// Maximum block size allowed to combine the blocks with same MV
#define MAX_BLOCK_SIZE_WITH_SAME_MV 64

// Encoder: Decide TIP mode is enabled or disabled. If TIP is enabled, derive
// temporal motion field from one closest forward and one closet backward
// reference frames, then fill the hole
void av1_enc_setup_tip_motion_field(AV1_COMMON *cm);

// Derive temporal motion field from one closest forward and one closet
// backward reference frames, then fill the hole
void av1_setup_tip_motion_field(AV1_COMMON *cm);

// Generate the whole TIP frame with the temporal motion field
void av1_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd, uint16_t **mc_buf,
                         CONV_BUF_TYPE *tmp_conv_dst,
                         CalcSubpelParamsFunc calc_subpel_params_func,
                         int copy_refined_mvs);

// Derive TMVP from closest forward and closet backward reference frames
void av1_derive_tip_nearest_ref_frames_motion_projection(AV1_COMMON *cm);

// Save TMVP motion fields for future frame's TMVP if current frame is coded
// as TIP_FRAME_AS_OUTPUT
void av1_copy_tip_frame_tmvp_mvs(const AV1_COMMON *const cm);

// Get the block width when blocks with same MV are combined.
static AOM_INLINE int get_tip_block_width_with_same_mv(
    const TPL_MV_REF *tpl_mvs, int unit_blk_size, int blk_col, int blk_col_end,
    int max_allow_blk_size) {
  int blk_width = unit_blk_size;
  const int step = unit_blk_size >> TMVP_MI_SZ_LOG2;
  int offset = step;
  const int is_zero_tpl_mv =
      tpl_mvs->mfmv0.as_int == 0 || tpl_mvs->mfmv0.as_int == INVALID_MV;

  while (blk_col + offset < blk_col_end && blk_width < max_allow_blk_size &&
         (tpl_mvs->mfmv0.as_int == tpl_mvs[offset].mfmv0.as_int ||
          (is_zero_tpl_mv && (tpl_mvs[offset].mfmv0.as_int == 0 ||
                              tpl_mvs[offset].mfmv0.as_int == INVALID_MV)))) {
    blk_width += unit_blk_size;
    offset += step;
  }
  blk_width = 1 << get_msb(blk_width);
  return blk_width;
}

// Check if MV refinement is disabled for TIP 16x16 blocks.
static AOM_INLINE int is_tip_mv_refinement_disabled_for_unit_size_16x16(
    int unit_blk_size, int enable_tip_refinemv, TIP_FRAME_MODE tip_frame_mode) {
  if (unit_blk_size != 16) return 0;
  return !enable_tip_refinemv || tip_frame_mode == TIP_FRAME_AS_OUTPUT;
}

// Compute scale factor for temporal scaling
static AOM_INLINE int tip_derive_scale_factor(int num, int den_signed) {
  int den = den_signed < 0 ? -den_signed : den_signed;
  int sign = den_signed < 0 ? -1 : 1;
  den = AOMMIN(den, MAX_FRAME_DISTANCE);
  num = num > 0 ? AOMMIN(num, MAX_FRAME_DISTANCE)
                : AOMMAX(num, -MAX_FRAME_DISTANCE);
  return sign * num * div_mult[den];
}
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_TIP_H_
