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

#if CONFIG_OPTFLOW_ON_TIP
#define TIP_RD_CORRECTION 100000
#endif  // CONFIG_OPTFLOW_ON_TIP

// Derive temporal motion field from one closest forward and one closet backward
// reference frames, then fill the hole
void av1_setup_tip_motion_field(AV1_COMMON *cm, int check_tip_threshold);

// Generate the whole TIP frame with the temporal motion field
void av1_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd, uint16_t **mc_buf,
                         CONV_BUF_TYPE *tmp_conv_dst,
                         CalcSubpelParamsFunc calc_subpel_params_func);

#if !CONFIG_TIP_REF_PRED_MERGING
// Generate TIP reference block if current block is coded as TIP mode
void av1_setup_tip_on_the_fly(AV1_COMMON *cm, MACROBLOCKD *xd,
                              int blk_row_start, int blk_col_start,
                              int blk_row_end, int blk_col_end, int mvs_stride,
                              uint16_t **mc_buf, CONV_BUF_TYPE *tmp_conv_dst,
                              CalcSubpelParamsFunc calc_subpel_params_func);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

// Derive TMVP from closest forward and closet backward reference frames
void av1_derive_tip_nearest_ref_frames_motion_projection(AV1_COMMON *cm);

// Save TMVP motion fields for future frame's TMVP if current frame is coded
// as TIP_FRAME_AS_OUTPUT
void av1_copy_tip_frame_tmvp_mvs(const AV1_COMMON *const cm);

// Compute scale factor for temporal scaling
static AOM_INLINE int tip_derive_scale_factor(int num, int den_signed) {
  int den = den_signed < 0 ? -den_signed : den_signed;
  int sign = den_signed < 0 ? -1 : 1;
  den = AOMMIN(den, MAX_FRAME_DISTANCE);
  num = num > 0 ? AOMMIN(num, MAX_FRAME_DISTANCE)
                : AOMMAX(num, -MAX_FRAME_DISTANCE);
  return sign * num * div_mult[den];
}

// SMVP candidate which is derived from TIP mode
static AOM_INLINE void clamp_tip_smvp_refmv(const AV1_COMMON *const cm, MV *mv,
                                            const int blk_row,
                                            const int blk_col) {
  const int y_width = cm->tip_ref.ref_frame_buffer[0]->buf.y_width;
  const int y_height = cm->tip_ref.ref_frame_buffer[0]->buf.y_height;

  FULLPEL_MV fullmv;
  fullmv = get_fullmv_from_mv(mv);
  const int row_offset = fullmv.row + (blk_row << MI_SIZE_LOG2);
  if (row_offset > y_height) {
    fullmv.row = y_height - (blk_row << MI_SIZE_LOG2);
  } else if (row_offset < 0) {
    fullmv.row = -(blk_row << MI_SIZE_LOG2);
  }

  const int col_offset = fullmv.col + (blk_col << MI_SIZE_LOG2);
  if (col_offset > y_width) {
    fullmv.col = y_width - (blk_col << MI_SIZE_LOG2);
  } else if (col_offset < 0) {
    fullmv.col = -(blk_col << MI_SIZE_LOG2);
  }

  *mv = get_mv_from_fullmv(&fullmv);
}

#if !CONFIG_TIP_REF_PRED_MERGING
#if !CONFIG_REFINEMV
// Clamp MV to UMV border based on its distance to left/right/top/bottom edge
static AOM_INLINE MV tip_clamp_mv_to_umv_border_sb(
    InterPredParams *const inter_pred_params, const MV *src_mv, int bw, int bh,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    int ss_x, int ss_y) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (AOM_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (AOM_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
#if CONFIG_OPTFLOW_REFINEMENT
  MV clamped_mv;
  if (use_optflow_refinement) {
    // optflow refinement always returns MVs with 1/16 precision so it is not
    // necessary to shift the MV before clamping
    // Here it should be:
    // clamped_mv.row = (int16_t)ROUND_POWER_OF_TWO_SIGNED(
    //     src_mv->row * (1 << SUBPEL_BITS), MV_REFINE_PREC_BITS + ss_y);
    // But currently SUBPEL_BITS == MV_REFINE_PREC_BITS
    assert(SUBPEL_BITS == MV_REFINE_PREC_BITS);

    if (ss_y || ss_x) {
      clamped_mv.row = (int16_t)ROUND_POWER_OF_TWO_SIGNED(
          src_mv->row * (1 << SUBPEL_BITS), MV_REFINE_PREC_BITS + ss_y);
      clamped_mv.col = (int16_t)ROUND_POWER_OF_TWO_SIGNED(
          src_mv->col * (1 << SUBPEL_BITS), MV_REFINE_PREC_BITS + ss_x);
    } else {
      clamped_mv = *src_mv;
    }
  } else {
    clamped_mv.row = (int16_t)(src_mv->row * (1 << (1 - ss_y)));
    clamped_mv.col = (int16_t)(src_mv->col * (1 << (1 - ss_x)));
  }
#else
  MV clamped_mv = { (int16_t)(src_mv->row * (1 << (1 - ss_y))),
                    (int16_t)(src_mv->col * (1 << (1 - ss_x))) };
#endif  // CONFIG_OPTFLOW_REFINEMENT
  assert(ss_x <= 1);
  assert(ss_y <= 1);
  const SubpelMvLimits mv_limits = {
    inter_pred_params->dist_to_left_edge * (1 << (1 - ss_x)) - spel_left,
    inter_pred_params->dist_to_right_edge * (1 << (1 - ss_x)) + spel_right,
    inter_pred_params->dist_to_top_edge * (1 << (1 - ss_y)) - spel_top,
    inter_pred_params->dist_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom
  };

  clamp_mv(&clamped_mv, &mv_limits);

  return clamped_mv;
}
#endif  //! CONFIG_REFINEMV
#endif  // !CONFIG_TIP_REF_PRED_MERGING
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_TIP_H_
