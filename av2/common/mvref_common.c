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

#include <stdlib.h>

#include "av2/common/mv.h"
#include "av2/common/mvref_common.h"
#include "av2/common/reconintra.h"
#include "av2/common/tip.h"
#include "av2/common/warped_motion.h"

typedef struct single_mv_candidate {
  int_mv mv;
  MV_REFERENCE_FRAME ref_frame;
} SINGLE_MV_CANDIDATE;

#define TMVP_SEARCH_COUNT 2
#define SMVP_COL_SEARCH_COUNT 2
typedef struct mvp_unit_status {
  int is_available;
  int row_offset;
  int col_offset;
} MVP_UNIT_STATUS;

enum {
  /**
   * Coding block width is 4
   */
  BLOCK_WIDTH_4 = 0,
  /**
   * Coding block width is 8
   */
  BLOCK_WIDTH_8,
  /**
   * Coding block width no less than 16
   */
  BLOCK_WIDTH_OTHERS,
  /**
   * Coding block width types
   */
  BLOCK_WIDTH_TYPES,
} UENUM1BYTE(BLOCK_WIDTH_TYPE);

#define TIP_MFMV_STACK_SIZE 3  // The limit for original TMVP w/ TIP.
#define MFMV_STACK_SIZE 4      // The total limit of motion field candidates.

// Check and make sure that the MVs are stored to the correct slots.
static INLINE void check_frame_mv_slot(const AV2_COMMON *const cm, MV_REF *mv) {
  if (mv->ref_frame[0] != NONE_FRAME && mv->ref_frame[1] == NONE_FRAME) {
    mv->ref_frame[1] = mv->ref_frame[0];
    mv->mv[1] = mv->mv[0];
  } else if (mv->ref_frame[0] == NONE_FRAME && mv->ref_frame[1] != NONE_FRAME) {
    mv->ref_frame[0] = mv->ref_frame[1];
    mv->mv[0] = mv->mv[1];
  } else if (mv->ref_frame[0] != NONE_FRAME && mv->ref_frame[1] != NONE_FRAME) {
    int ref_display_order[2] = {
      get_ref_frame_buf(cm, mv->ref_frame[0])->display_order_hint,
      get_ref_frame_buf(cm, mv->ref_frame[1])->display_order_hint
    };
    int cur_display_order = cm->cur_frame->display_order_hint;

    const int diff_0_cur =
        get_relative_dist(&cm->seq_params.order_hint_info, ref_display_order[0],
                          cur_display_order);
    const int diff_1_cur =
        get_relative_dist(&cm->seq_params.order_hint_info, ref_display_order[1],
                          cur_display_order);
    const int diff_0_1 =
        get_relative_dist(&cm->seq_params.order_hint_info, ref_display_order[0],
                          ref_display_order[1]);

    bool to_switch = false;
    if (diff_0_cur < 0 && diff_1_cur < 0) {
      if (diff_0_1 < 0) {
        to_switch = true;
      }
    } else if (diff_0_cur > 0 && diff_1_cur > 0) {
      if (diff_0_1 < 0) {
        to_switch = true;
      }
    } else if (diff_0_cur > 0 && diff_1_cur < 0) {
      to_switch = true;
    }

    if (to_switch) {
      int tmp_ref = mv->ref_frame[0];
      int_mv tmp_mv = mv->mv[0];

      mv->ref_frame[0] = mv->ref_frame[1];
      mv->mv[0] = mv->mv[1];

      mv->ref_frame[1] = tmp_ref;
      mv->mv[1] = tmp_mv;
    }
  }
}

#define ADJACENT_SMVP_WEIGHT 1
#define OTHER_SMVP_WEIGHT 0
#define TMVP_WEIGHT 1
#define HIGH_PRIORITY_TMVP_WEIGHT 2

// Check if a given block uses a valid warp affine transformation
// (either global or local) for motion compensation and set the corresponding
// warp parameters.
static INLINE int is_warp_affine_block(const MACROBLOCKD *xd,
                                       const MB_MODE_INFO *mi, int ref_idx,
                                       WarpedMotionParams *warp_params) {
  *warp_params = default_warp_params;
  MV_REFERENCE_FRAME ref_frame = mi->ref_frame[ref_idx];

  if (!is_inter_ref_frame(ref_frame)) return 0;

  if (xd->cur_frame_force_integer_mv) return 0;

  const WarpedMotionParams gm_params = xd->global_motion[ref_frame];
  const WarpTypesAllowed warp_types = {
    is_global_mv_block(mi, gm_params.wmtype), is_warp_mode(mi->motion_mode)
  };

  if ((warp_types.local_warp_allowed && !mi->wm_params[ref_idx].invalid) ||
      (warp_types.global_warp_allowed && !gm_params.invalid)) {
    *warp_params =
        warp_types.local_warp_allowed ? mi->wm_params[ref_idx] : gm_params;
    return 1;
  }
  return 0;
}

// Computes the 8x8 sub-block warp motion vector from the warp model.
static INLINE MV get_sub_block_warp_mv(const WarpedMotionParams *warp_params,
                                       int pixel_x, int pixel_y, int bw,
                                       int bh) {
  const int center_x = pixel_x + (bw >> 1);
  const int center_y = pixel_y + (bh >> 1);

  const int32_t submv_x_hp = get_subblk_offset_x_hp(
      warp_params->wmmat, center_x, center_y, 1 << WARPEDMODEL_PREC_BITS);
  const int32_t submv_y_hp = get_subblk_offset_y_hp(
      warp_params->wmmat, center_x, center_y, 1 << WARPEDMODEL_PREC_BITS);

  MV submv;
  submv.col = ROUND_POWER_OF_TWO_SIGNED(submv_x_hp, WARPEDMODEL_PREC_BITS - 3);
  submv.row = ROUND_POWER_OF_TWO_SIGNED(submv_y_hp, WARPEDMODEL_PREC_BITS - 3);
  return submv;
}

#define OPFL_MVS_CLAMPED 0
// Overwrite the MVs in TMVP list by optical flow refined MVs (for TIP frame
// mode)
void av2_copy_frame_refined_mvs_tip_frame_mode(const AV2_COMMON *const cm,
                                               const MACROBLOCKD *xd,
                                               const MB_MODE_INFO *const mi,
                                               int mi_row, int mi_col,
                                               int x_inside_boundary,
                                               int y_inside_boundary) {
  const int frame_mvs_stride =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int cur_tpl_row = (mi_row >> TMVP_SHIFT_BITS);
  const int cur_tpl_col = (mi_col >> TMVP_SHIFT_BITS);
  const int offset = cur_tpl_row * frame_mvs_stride + cur_tpl_col;
  MV_REF *frame_mvs = cm->cur_frame->mvs + offset;
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, TMVP_SHIFT_BITS);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, TMVP_SHIFT_BITS);
  int bw = block_size_wide[mi->sb_type[xd->tree_type == CHROMA_PART]];
  int bh = block_size_high[mi->sb_type[xd->tree_type == CHROMA_PART]];
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  const int use_4x4 = !tip_ref_frame;
  const bool is_opfl_mode =
      is_optflow_refinement_enabled(cm, xd, mi, AVM_PLANE_Y, tip_ref_frame);
  int n = opfl_get_subblock_size(bw, bh, AVM_PLANE_Y, use_4x4);

  // Pointers to hold optical flow MV offsets.
  int *vx0 = xd->opfl_vxy_bufs;
  int *vx1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 1);
  int *vy0 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 2);
  int *vy1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 3);

  int apply_sub_block_refinemv = tip_ref_frame || mi->refinemv_flag;

  // If this is a TIP block and a refinement algorithm is applied,
  // no special handling is needed for TIP 16x16 blocks, because refined MVs
  // are used for TMVP in this case, and they are stored at a finer granularity.

  for (int h = 0; h < y_inside_boundary; h++) {
    MV_REF *mv = frame_mvs;
    for (int w = 0; w < x_inside_boundary; w++) {
      for (int idx = 0; idx < 2; ++idx) {
        mv->ref_frame[idx] = NONE_FRAME;
        mv->mv[idx].as_int = 0;
        MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
        if (!is_inter_ref_frame(ref_frame) && !tip_ref_frame) continue;

        int_mv refined_mv;
        // Refined MVs are stored per 4x4 in refinemv_subinfo, but h and
        // w for TMVP are per 8x8, so (h<<1) and (w<<1) are used here.
        if (apply_sub_block_refinemv)
          refined_mv.as_mv =
              xd->refinemv_subinfo[(h << 1) * MAX_MIB_SIZE + (w << 1)]
                  .refinemv[idx]
                  .as_mv;
        else
          refined_mv.as_mv = mi->mv[idx].as_mv;
        if (is_opfl_mode) {
          int *vy = idx ? vy1 : vy0;
          int *vx = idx ? vx1 : vx0;
          if (n == 4) {
            // Since TMVP is stored per 8x8 unit, for refined MV with 4x4
            // subblock, take the average of 4 refined MVs
            refined_mv.as_mv.row += ROUND_POWER_OF_TWO_SIGNED(
                vy[0] + vy[1] + vy[2] + vy[3], 2 + MV_REFINE_PREC_BITS - 3);
            refined_mv.as_mv.col += ROUND_POWER_OF_TWO_SIGNED(
                vx[0] + vx[1] + vx[2] + vx[3], 2 + MV_REFINE_PREC_BITS - 3);
          } else {
            int sbmv_stride = bw >> 3;
            refined_mv.as_mv.row += ROUND_POWER_OF_TWO_SIGNED(
                vy[h * sbmv_stride + w], MV_REFINE_PREC_BITS - 3);
            refined_mv.as_mv.col += ROUND_POWER_OF_TWO_SIGNED(
                vx[h * sbmv_stride + w], MV_REFINE_PREC_BITS - 3);
          }
        }
#if OPFL_MVS_CLAMPED
        refined_mv.as_mv.row =
            clamp(refined_mv.as_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
        refined_mv.as_mv.col =
            clamp(refined_mv.as_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
#else
        if ((abs(refined_mv.as_mv.row) > REFMVS_LIMIT) ||
            (abs(refined_mv.as_mv.col) > REFMVS_LIMIT))
          continue;
#endif
        mv->ref_frame[idx] =
            tip_ref_frame ? cm->tip_ref.ref_frame[idx] : ref_frame;
        mv->mv[idx].as_int = refined_mv.as_int;
        process_mv_for_tmvp(&mv->mv[idx].as_mv);
      }
      check_frame_mv_slot(cm, mv);
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}

// Copy the MVs into the TMVP list (for TIP frame mode)
void av2_copy_frame_mvs_tip_frame_mode(const AV2_COMMON *const cm,
                                       const MACROBLOCKD *const xd,
                                       const MB_MODE_INFO *const mi, int mi_row,
                                       int mi_col, int x_inside_boundary,
                                       int y_inside_boundary) {
  const int frame_mvs_stride =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int cur_tpl_row = (mi_row >> TMVP_SHIFT_BITS);
  const int cur_tpl_col = (mi_col >> TMVP_SHIFT_BITS);
  const int offset = cur_tpl_row * frame_mvs_stride + cur_tpl_col;
  MV_REF *frame_mvs = cm->cur_frame->mvs + offset;
  const TIP *tip_ref = &cm->tip_ref;
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, TMVP_SHIFT_BITS);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, TMVP_SHIFT_BITS);

  const uint8_t *decisions = NULL;
  const BLOCK_SIZE bsize = mi->sb_type[xd->tree_type == CHROMA_PART];
  const int bw = block_size_wide[bsize];
  const bool is_wedge = is_inter_ref_frame(mi->ref_frame[0]) &&
                        is_inter_ref_frame(mi->ref_frame[1]) &&
                        !is_tip_ref_frame(mi->ref_frame[0]) &&
                        !is_tip_ref_frame(mi->ref_frame[1]) &&
                        mi->interinter_comp.type == COMPOUND_WEDGE;
  if (is_wedge) {
    decisions = av2_get_contiguous_soft_mask_decision(
        mi->interinter_comp.wedge_index, mi->interinter_comp.wedge_sign,
        mi->interinter_comp.wedge_boundary_index, bsize);
  }

  int is_tip_two_refs = 1;
  if (is_tip_ref_frame(mi->ref_frame[0])) {
    const int tip_wtd_index = cm->tip_global_wtd_index;
    const int8_t tip_weight = tip_weighting_factors[tip_wtd_index];
    is_tip_two_refs = tip_weight != TIP_SINGLE_WTD;
  }

  WarpedMotionParams warp_params[2];
  int is_warp[2] = { 0 };
  for (int idx = 0; idx < 2; idx++) {
    is_warp[idx] = is_warp_affine_block(xd, mi, idx, &warp_params[idx]);
  }

  const int is_tip_16_16 = is_tip_coded_as_16x16_block(cm, mi);

  for (int h = 0; h < y_inside_boundary; h++) {
    MV_REF *mv = frame_mvs;
    for (int w = 0; w < x_inside_boundary; w++) {
      for (int idx = 0; idx < 2; ++idx) {
        mv->ref_frame[idx] = NONE_FRAME;
        mv->mv[idx].as_int = 0;
      }

      for (int idx = 0; idx < 2; ++idx) {
        MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
        if (is_inter_ref_frame(ref_frame) && !is_tip_ref_frame(ref_frame)) {
          int_mv sub_block_mv;
          if (is_wedge) {
            const int this_decision =
                decisions[h * TMVP_MI_SIZE * bw + w * TMVP_MI_SIZE];

            if (this_decision == 0 && idx == 1) continue;
            if (this_decision == 1 && idx == 0) continue;
          }
          if (is_warp[idx]) {
            const int32_t pixel_x = mi_col * MI_SIZE + w * TMVP_MI_SIZE;
            const int32_t pixel_y = mi_row * MI_SIZE + h * TMVP_MI_SIZE;
            sub_block_mv.as_mv =
                get_sub_block_warp_mv(&warp_params[idx], pixel_x, pixel_y,
                                      TMVP_MI_SIZE, TMVP_MI_SIZE);
          } else {
            sub_block_mv.as_mv = mi->mv[idx].as_mv;
          }
          if ((abs(sub_block_mv.as_mv.row) > REFMVS_LIMIT) ||
              (abs(sub_block_mv.as_mv.col) > REFMVS_LIMIT))
            continue;
          mv->ref_frame[idx] = ref_frame;
          mv->mv[idx].as_int = sub_block_mv.as_int;
          process_mv_for_tmvp(&mv->mv[idx].as_mv);
        } else if (is_tip_ref_frame(ref_frame)) {
          if (is_tip_16_16 && ((h % 2) || (w % 2))) {
            const int col_offset = (w % 2) ? -1 : 0;
            const int row_offset = (h % 2) ? -1 : 0;
            *mv = mv[row_offset * frame_mvs_stride + col_offset];
          } else {
            int_mv this_mv[2] = { { 0 } };
            const MV *blk_mv = &mi->mv[idx].as_mv;
            get_tip_mv(cm, blk_mv, cur_tpl_col + w, cur_tpl_row + h, this_mv);
            if ((abs(this_mv[0].as_mv.row) <= REFMVS_LIMIT) &&
                (abs(this_mv[0].as_mv.col) <= REFMVS_LIMIT)) {
              mv->ref_frame[0] = tip_ref->ref_frame[0];
              mv->mv[0].as_int = this_mv[0].as_int;
              process_mv_for_tmvp(&mv->mv[0].as_mv);
            }

            if (is_tip_two_refs) {
              if ((abs(this_mv[1].as_mv.row) <= REFMVS_LIMIT) &&
                  (abs(this_mv[1].as_mv.col) <= REFMVS_LIMIT)) {
                mv->ref_frame[1] = tip_ref->ref_frame[1];
                mv->mv[1].as_int = this_mv[1].as_int;
                process_mv_for_tmvp(&mv->mv[1].as_mv);
              }
            }
          }
          break;
        }
      }
      check_frame_mv_slot(cm, mv);
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}

// Overwrite the MVs in TMVP list by optical flow refined MVs
void av2_copy_frame_refined_mvs(const AV2_COMMON *const cm,
                                const MACROBLOCKD *xd,
                                const MB_MODE_INFO *const mi, int mi_row,
                                int mi_col, int x_inside_boundary,
                                int y_inside_boundary) {
  if (cm->seq_params.enable_tip && cm->features.tip_frame_mode) {
    av2_copy_frame_refined_mvs_tip_frame_mode(
        cm, xd, mi, mi_row, mi_col, x_inside_boundary, y_inside_boundary);
    return;
  }

  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, 1);
  MV_REF *frame_mvs =
      cm->cur_frame->mvs + (mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, 1);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, 1);
  int bw = block_size_wide[mi->sb_type[xd->tree_type == CHROMA_PART]];
  int bh = block_size_high[mi->sb_type[xd->tree_type == CHROMA_PART]];
  const bool is_opfl_mode = opfl_allowed_cur_pred_mode(cm, xd, mi);
  int n = opfl_get_subblock_size(bw, bh, AVM_PLANE_Y, 1);
  int w, h;

  // Pointers to hold optical flow MV offsets.
  int *vx0 = xd->opfl_vxy_bufs;
  int *vx1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 1);
  int *vy0 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 2);
  int *vy1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 3);

  int apply_sub_block_refinemv =
      mi->refinemv_flag && !is_tip_ref_frame(mi->ref_frame[0]);

  for (h = 0; h < y_inside_boundary; h++) {
    MV_REF *mv = frame_mvs;
    for (w = 0; w < x_inside_boundary; w++) {
      for (int idx = 0; idx < 2; ++idx) {
        mv->ref_frame[idx] = NONE_FRAME;
        mv->mv[idx].as_int = 0;
        MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
        if (is_inter_ref_frame(ref_frame)) {
          int_mv refined_mv;
          // Refined MVs are stored per 4x4 in refinemv_subinfo, but h and
          // w for TMVP are per 8x8, so (h<<1) and (w<<1) are used here.
          if (apply_sub_block_refinemv)
            refined_mv.as_mv =
                xd->refinemv_subinfo[(h << 1) * MAX_MIB_SIZE + (w << 1)]
                    .refinemv[idx]
                    .as_mv;
          else
            refined_mv.as_mv = mi->mv[idx].as_mv;
          if (is_opfl_mode) {
            int *vy = idx ? vy1 : vy0;
            int *vx = idx ? vx1 : vx0;
            if (n == 4) {
              // Since TMVP is stored per 8x8 unit, for refined MV with 4x4
              // subblock, take the average of 4 refined MVs
              refined_mv.as_mv.row += ROUND_POWER_OF_TWO_SIGNED(
                  vy[0] + vy[1] + vy[2] + vy[3], 2 + MV_REFINE_PREC_BITS - 3);
              refined_mv.as_mv.col += ROUND_POWER_OF_TWO_SIGNED(
                  vx[0] + vx[1] + vx[2] + vx[3], 2 + MV_REFINE_PREC_BITS - 3);
            } else {
              int sbmv_stride = bw >> 3;
              refined_mv.as_mv.row += ROUND_POWER_OF_TWO_SIGNED(
                  vy[h * sbmv_stride + w], MV_REFINE_PREC_BITS - 3);
              refined_mv.as_mv.col += ROUND_POWER_OF_TWO_SIGNED(
                  vx[h * sbmv_stride + w], MV_REFINE_PREC_BITS - 3);
            }
          }
#if OPFL_MVS_CLAMPED
          refined_mv.as_mv.row =
              clamp(refined_mv.as_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
          refined_mv.as_mv.col =
              clamp(refined_mv.as_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
#else
          if ((abs(refined_mv.as_mv.row) > REFMVS_LIMIT) ||
              (abs(refined_mv.as_mv.col) > REFMVS_LIMIT))
            continue;
#endif
          mv->ref_frame[idx] = ref_frame;
          mv->mv[idx].as_int = refined_mv.as_int;
          process_mv_for_tmvp(&mv->mv[idx].as_mv);
        }
      }
      check_frame_mv_slot(cm, mv);
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}

// Copy mvs from bru ref frame to cur frame
// Used for keeping the old ref frame mvs in the cur frame
void bru_copy_sb_mvs(const AV2_COMMON *const cm, int src_ref_idx,
                     int dst_ref_idx, int mi_row, int mi_col,
                     int x_inside_boundary, int y_inside_boundary) {
  // if src_ref_idx or dst_ref_idx < 0 (prefer -1), means cm->cur_frame
  if (src_ref_idx == dst_ref_idx) return;
  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, 1);
  MV_REF *src_frame_mvs =
      ((src_ref_idx < 0) ? cm->cur_frame->mvs
                         : get_ref_frame_buf(cm, src_ref_idx)->mvs) +
      +(mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  MV_REF *dst_frame_mvs =
      ((dst_ref_idx < 0) ? cm->cur_frame->mvs
                         : get_ref_frame_buf(cm, dst_ref_idx)->mvs) +
      +(mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, 1);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, 1);
  int w, h;
  for (h = 0; h < y_inside_boundary; h++) {
    // get mvs stored in bru ref frame
    MV_REF *src_ref = src_frame_mvs;
    MV_REF *dst_ref = dst_frame_mvs;
    for (w = 0; w < x_inside_boundary; w++) {
      dst_ref->ref_frame[0] = NONE_FRAME;
      dst_ref->ref_frame[1] = NONE_FRAME;
      dst_ref->mv[0].as_int = 0;
      dst_ref->mv[1].as_int = 0;
      if (is_inter_ref_frame(src_ref->ref_frame[0]) &&
          src_ref->ref_frame[1] == NONE_FRAME) {
        if ((abs(src_ref->mv[0].as_mv.row) <= REFMVS_LIMIT) &&
            (abs(src_ref->mv[0].as_mv.col) <= REFMVS_LIMIT)) {
          dst_ref->ref_frame[0] = src_ref->ref_frame[0];
          dst_ref->mv[0].as_int = src_ref->mv[0].as_int;
        }
      } else {
        for (int idx = 0; idx < 2; ++idx) {
          if (is_inter_ref_frame(src_ref->ref_frame[idx])) {
            int_mv src_ref_mv = src_ref->mv[idx];
            if ((abs(src_ref_mv.as_mv.row) > REFMVS_LIMIT) ||
                (abs(src_ref_mv.as_mv.col) > REFMVS_LIMIT)) {
              continue;
            } else {
              // TODO: careful, may need to map this to cur ref
              dst_ref->ref_frame[idx] = src_ref->ref_frame[idx];
              dst_ref->mv[idx] = src_ref_mv;
            }
          }
        }
      }
      check_frame_mv_slot(cm, dst_ref);
      dst_ref++;
      src_ref++;
    }
    dst_frame_mvs += frame_mvs_stride;
    src_frame_mvs += frame_mvs_stride;
  }
}

void bru_zero_sb_mvs(const AV2_COMMON *const cm, int dst_ref_idx, int mi_row,
                     int mi_col, int x_inside_boundary, int y_inside_boundary) {
  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, 1);
  MV_REF *dst_frame_mvs =
      ((dst_ref_idx < 0) ? cm->cur_frame->mvs
                         : get_ref_frame_buf(cm, dst_ref_idx)->mvs) +
      +(mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, 1);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, 1);
  int w, h;

  for (h = 0; h < y_inside_boundary; h++) {
    // get mvs stored in bru ref frame
    // MV_REF *bru_ref = frame_mvs;
    MV_REF *dst_ref = dst_frame_mvs;
    // MV_REFERENCE_FRAME ref_frame[2];
    for (w = 0; w < x_inside_boundary; w++) {
      if (cm->bridge_frame_info.is_bridge_frame) {
        dst_ref->ref_frame[0] =
            cm->bridge_frame_info.bridge_frame_ref_idx_remapped;
      } else {
        dst_ref->ref_frame[0] = cm->bru.update_ref_idx;
      }
      dst_ref->ref_frame[1] = NONE_FRAME;
      dst_ref->mv[0].as_int = 0;
      dst_ref->mv[1].as_int = 0;
      check_frame_mv_slot(cm, dst_ref);
      dst_ref++;
    }
    dst_frame_mvs += frame_mvs_stride;
  }
}

// Copy the MVs into the TMVP list
void av2_copy_frame_mvs(const AV2_COMMON *const cm, const MACROBLOCKD *const xd,
                        const MB_MODE_INFO *const mi, int mi_row, int mi_col,
                        int x_inside_boundary, int y_inside_boundary) {
  if (cm->seq_params.enable_tip && cm->features.tip_frame_mode) {
    av2_copy_frame_mvs_tip_frame_mode(cm, xd, mi, mi_row, mi_col,
                                      x_inside_boundary, y_inside_boundary);
    return;
  }

  const int frame_mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, 1);
  MV_REF *frame_mvs =
      cm->cur_frame->mvs + (mi_row >> 1) * frame_mvs_stride + (mi_col >> 1);
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, 1);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, 1);
  int w, h;

  const uint8_t *decisions = NULL;
  const BLOCK_SIZE bsize = mi->sb_type[xd->tree_type == CHROMA_PART];
  const int bw = block_size_wide[bsize];
  const bool is_wedge = is_inter_ref_frame(mi->ref_frame[0]) &&
                        is_inter_ref_frame(mi->ref_frame[1]) &&
                        !is_tip_ref_frame(mi->ref_frame[0]) &&
                        !is_tip_ref_frame(mi->ref_frame[1]) &&
                        mi->interinter_comp.type == COMPOUND_WEDGE;
  if (is_wedge) {
    decisions = av2_get_contiguous_soft_mask_decision(
        mi->interinter_comp.wedge_index, mi->interinter_comp.wedge_sign,
        mi->interinter_comp.wedge_boundary_index, bsize);
  }
  WarpedMotionParams warp_params[2];
  int is_warp[2] = { 0 };
  for (int idx = 0; idx < 2; idx++) {
    is_warp[idx] = is_warp_affine_block(xd, mi, idx, &warp_params[idx]);
  }

  for (h = 0; h < y_inside_boundary; h++) {
    MV_REF *mv = frame_mvs;
    for (w = 0; w < x_inside_boundary; w++) {
      mv->ref_frame[0] = NONE_FRAME;
      mv->ref_frame[1] = NONE_FRAME;
      mv->mv[0].as_int = 0;
      mv->mv[1].as_int = 0;

      if (is_inter_ref_frame(mi->ref_frame[0]) &&
          mi->ref_frame[1] == NONE_FRAME) {
        int_mv sub_block_mv;
        if (is_warp[0]) {
          const int32_t pixel_x = mi_col * MI_SIZE + w * TMVP_MI_SIZE;
          const int32_t pixel_y = mi_row * MI_SIZE + h * TMVP_MI_SIZE;
          sub_block_mv.as_mv = get_sub_block_warp_mv(
              &warp_params[0], pixel_x, pixel_y, TMVP_MI_SIZE, TMVP_MI_SIZE);
        } else {
          sub_block_mv.as_mv = mi->mv[0].as_mv;
        }

        if ((abs(sub_block_mv.as_mv.row) <= REFMVS_LIMIT) &&
            (abs(sub_block_mv.as_mv.col) <= REFMVS_LIMIT)) {
          mv->ref_frame[0] = mi->ref_frame[0];
          mv->mv[0].as_mv = sub_block_mv.as_mv;
          process_mv_for_tmvp(&mv->mv[0].as_mv);
        }
      } else {
        for (int idx = 0; idx < 2; ++idx) {
          MV_REFERENCE_FRAME ref_frame = mi->ref_frame[idx];
          if (is_inter_ref_frame(ref_frame)) {
            int_mv sub_block_mv;
            if (is_wedge) {
              const int this_decision =
                  decisions[h * TMVP_MI_SIZE * bw + w * TMVP_MI_SIZE];

              if (this_decision == 0 && idx == 1) continue;
              if (this_decision == 1 && idx == 0) continue;
            }
            if (is_warp[idx]) {
              const int32_t pixel_x = mi_col * MI_SIZE + w * TMVP_MI_SIZE;
              const int32_t pixel_y = mi_row * MI_SIZE + h * TMVP_MI_SIZE;
              sub_block_mv.as_mv =
                  get_sub_block_warp_mv(&warp_params[idx], pixel_x, pixel_y,
                                        TMVP_MI_SIZE, TMVP_MI_SIZE);
            } else {
              sub_block_mv.as_mv = mi->mv[idx].as_mv;
            }
            if ((abs(sub_block_mv.as_mv.row) > REFMVS_LIMIT) ||
                (abs(sub_block_mv.as_mv.col) > REFMVS_LIMIT))
              continue;

            mv->ref_frame[idx] = ref_frame;
            mv->mv[idx].as_int = sub_block_mv.as_int;
            process_mv_for_tmvp(&mv->mv[idx].as_mv);
          }
        }
      }
      check_frame_mv_slot(cm, mv);
      mv++;
    }
    frame_mvs += frame_mvs_stride;
  }
}

// Fetch MVP candidates from derived SMVP into MVP candidate list
// when there is no enough MVP candidates.
static AVM_INLINE void fill_mvp_from_derived_smvp(
    const MV_REFERENCE_FRAME rf[2], CANDIDATE_MV *ref_mv_stack,
    uint16_t *ref_mv_weight, uint8_t *refmv_count,
    CANDIDATE_MV *derived_mv_stack, uint8_t derived_mv_count,
    const int max_ref_mv_count, int *drl_pr_count) {
  int index = 0;
  int derived_idx = 0;

  if (rf[1] == NONE_FRAME) {
    for (derived_idx = 0; derived_idx < derived_mv_count; ++derived_idx) {
      if (*drl_pr_count < MAX_PR_NUM) {
        for (index = 0; index < *refmv_count; ++index) {
          ++(*drl_pr_count);
          if (ref_mv_stack[index].this_mv.as_int ==
              derived_mv_stack[derived_idx].this_mv.as_int) {
            break;
          }
        }

        // Add a new item to the list.
        if (index == *refmv_count && *refmv_count < max_ref_mv_count) {
          ref_mv_stack[index].this_mv = derived_mv_stack[derived_idx].this_mv;
          ref_mv_stack[index].row_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[index].col_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[index].cwp_idx = CWP_EQUAL;
          ref_mv_weight[index] = REF_CAT_LEVEL;
          ++(*refmv_count);
        }
      } else {
        if (*refmv_count < max_ref_mv_count) {
          ref_mv_stack[*refmv_count].this_mv =
              derived_mv_stack[derived_idx].this_mv;
          ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
          ref_mv_weight[*refmv_count] = REF_CAT_LEVEL;
          ++(*refmv_count);
        }
      }
    }
  } else {
    for (derived_idx = 0; derived_idx < derived_mv_count; ++derived_idx) {
      if (*drl_pr_count < MAX_PR_NUM) {
        for (index = 0; index < *refmv_count; ++index) {
          ++(*drl_pr_count);
          if ((ref_mv_stack[index].this_mv.as_int ==
               derived_mv_stack[derived_idx].this_mv.as_int) &&
              (ref_mv_stack[index].comp_mv.as_int ==
               derived_mv_stack[derived_idx].comp_mv.as_int)) {
            break;
          }
        }

        // Add a new item to the list.
        if (index == *refmv_count && *refmv_count < max_ref_mv_count) {
          ref_mv_stack[index].this_mv = derived_mv_stack[derived_idx].this_mv;
          ref_mv_stack[index].comp_mv = derived_mv_stack[derived_idx].comp_mv;
          ref_mv_stack[index].row_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[index].col_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[index].cwp_idx = CWP_EQUAL;
          ref_mv_weight[index] = REF_CAT_LEVEL;
          ++(*refmv_count);
        }
      } else {
        if (*refmv_count < max_ref_mv_count) {
          ref_mv_stack[*refmv_count].this_mv =
              derived_mv_stack[derived_idx].this_mv;
          ref_mv_stack[*refmv_count].comp_mv =
              derived_mv_stack[derived_idx].comp_mv;
          ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
          ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
          ref_mv_weight[*refmv_count] = REF_CAT_LEVEL;
          ++(*refmv_count);
        }
      }
    }
  }
}

static AVM_INLINE void derive_ref_mv_candidate_from_tip_mode(
    const AV2_COMMON *cm, int mi_row_cand, int mi_col_cand,
    const MB_MODE_INFO *const candidate, uint8_t *refmv_count,
    uint8_t *ref_match_count, uint8_t *newmv_count, CANDIDATE_MV *ref_mv_stack,
    uint16_t *ref_mv_weight, uint16_t weight, int *drl_pr_count) {
  int index = 0;

  int_mv ref_mv[2];
  derive_non_tip_mode_smvp_from_tip(cm, candidate, mi_row_cand, mi_col_cand,
                                    is_tip_coded_as_16x16_block(cm, candidate),
                                    ref_mv);

  if (*drl_pr_count < MAX_PR_NUM) {
    for (index = 0; index < *refmv_count; ++index) {
      ++(*drl_pr_count);
      if ((ref_mv_stack[index].this_mv.as_int == ref_mv[0].as_int) &&
          (ref_mv_stack[index].comp_mv.as_int == ref_mv[1].as_int)) {
        ref_mv_weight[index] += weight;
        break;
      }
    }

    // Add a new item to the list.
    if (index == *refmv_count && index < MAX_REF_MV_STACK_SIZE) {
      ref_mv_stack[index].this_mv = ref_mv[0];
      ref_mv_stack[index].comp_mv = ref_mv[1];
      ref_mv_weight[index] = weight;
      ref_mv_stack[index].row_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[index].col_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[index].cwp_idx = CWP_EQUAL;
      ++(*refmv_count);
    }
  } else {
    if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
      ref_mv_stack[*refmv_count].this_mv = ref_mv[0];
      ref_mv_stack[*refmv_count].comp_mv = ref_mv[1];
      ref_mv_weight[*refmv_count] = weight;
      ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
      ++(*refmv_count);
    }
  }

  if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
  ++*ref_match_count;
}

// add neighbor info to inter mode contexts
static AVM_INLINE void add_ref_mv_candidate_ctx(
    const MB_MODE_INFO *const candidate, uint8_t *ref_match_count,
    uint8_t *newmv_count, const AV2_COMMON *cm, const MV_REFERENCE_FRAME rf[2],
    const MB_MODE_INFO *mbmi) {
  if (!is_inter_block(candidate, SHARED_PART)) return;
  const TIP *tip_ref = &cm->tip_ref;
  if (mbmi->skip_mode) return;
  if (rf[1] == NONE_FRAME) {
    // single reference frame
    if (candidate->ref_frame[0] == rf[0]) {
      if (is_inter_mode(candidate->mode) &&
          compound_ref0_mode(candidate->mode) == NEWMV)
        ++*newmv_count;
      ++*ref_match_count;
    }
    if (candidate->ref_frame[1] == rf[0] &&
        candidate->ref_frame[0] != candidate->ref_frame[1]) {
      if (is_inter_compound_mode(candidate->mode) &&
          compound_ref1_mode(candidate->mode) == NEWMV)
        ++*newmv_count;
      ++*ref_match_count;
    }
  } else {
    if (is_tip_ref_frame(candidate->ref_frame[0]) &&
        candidate->ref_frame[1] == NONE_FRAME &&
        rf[0] == tip_ref->ref_frame[0] && rf[1] == tip_ref->ref_frame[1] &&
        cm->features.tip_frame_mode) {
      if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
      ++*ref_match_count;
    } else {
      // compound reference frame
      if (candidate->ref_frame[0] == rf[0] &&
          candidate->ref_frame[1] == rf[1]) {
        if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
        ++*ref_match_count;
      }
    }
  }
}

static AVM_INLINE void add_ref_mv_candidate(
    int mi_row, int mi_col, int mi_row_cand, int mi_col_cand,
    const MB_MODE_INFO *const candidate, const SUBMB_INFO *const submi,
    const MV_REFERENCE_FRAME rf[2], uint8_t *refmv_count,
    uint8_t *ref_match_count, uint8_t *newmv_count, CANDIDATE_MV *ref_mv_stack,
    uint16_t *ref_mv_weight, int_mv *gm_mv_candidates,
    const WarpedMotionParams *gm_params, const AV2_COMMON *cm, int add_more_mvs,
    SINGLE_MV_CANDIDATE *single_mv, uint8_t *single_mv_count,
    CANDIDATE_MV *derived_mv_stack, uint16_t *derived_mv_weight,
    uint8_t *derived_mv_count, uint8_t is_intrabc, int row_offset,
    int col_offset, uint16_t weight, int *drl_pr_count, int *drl_dr_pr_count,
    int *drl_dr_single_pr_count) {
  if (!is_inter_block(candidate, SHARED_PART)) return;

  if (is_intrabc != is_intrabc_block(candidate, SHARED_PART)) return;

  int index, ref;
  const TIP *tip_ref = &cm->tip_ref;

  if (rf[1] == NONE_FRAME) {
    int_mv cand_tip_mvs[2];
    MV_REFERENCE_FRAME cand_tip_ref_frames[2];
    if (is_tip_ref_frame(candidate->ref_frame[0])) {
      derive_non_tip_mode_smvp_from_tip(
          cm, candidate, mi_row_cand, mi_col_cand,
          is_tip_coded_as_16x16_block(cm, candidate), cand_tip_mvs);

      cand_tip_ref_frames[0] = cm->tip_ref.ref_frame[0];
      cand_tip_ref_frames[1] = cm->tip_ref.ref_frame[1];
    } else {
      cand_tip_mvs[0].as_int = INVALID_MV;
      cand_tip_mvs[1].as_int = INVALID_MV;
      cand_tip_ref_frames[0] = NONE_FRAME;
      cand_tip_ref_frames[1] = NONE_FRAME;
    }

    // single reference frame
    for (ref = 0; ref < 2; ++ref) {
      if (candidate->ref_frame[ref] == rf[0]) {
        int_mv this_refmv;
        if (is_tip_ref_frame(rf[0])) {
          this_refmv = get_block_mv(candidate, submi, ref);
        } else {
          const int is_gm_block =
              is_global_mv_block(candidate, gm_params[rf[0]].wmtype);
          this_refmv = is_gm_block ? gm_mv_candidates[0]
                                   : get_block_mv(candidate, submi, ref);
        }

        if (*drl_pr_count < MAX_PR_NUM) {
          for (index = 0; index < *refmv_count; ++index) {
            ++(*drl_pr_count);
            if (ref_mv_stack[index].this_mv.as_int == this_refmv.as_int) {
              ref_mv_weight[index] += weight;
              break;
            }
          }

          // Add a new item to the list.
          if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[index].this_mv = this_refmv;
            ref_mv_stack[index].row_offset = row_offset;
            ref_mv_stack[index].col_offset = col_offset;
            ref_mv_stack[index].cwp_idx = CWP_EQUAL;
            ref_mv_weight[index] = weight;
            ++(*refmv_count);
          }
        } else {
          if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[*refmv_count].this_mv = this_refmv;
            ref_mv_stack[*refmv_count].row_offset = row_offset;
            ref_mv_stack[*refmv_count].col_offset = col_offset;
            ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
            ref_mv_weight[*refmv_count] = weight;
            ++(*refmv_count);
          }
        }
        if (ref == 0) {
          if (is_inter_mode(candidate->mode) &&
              compound_ref0_mode(candidate->mode) == NEWMV)
            ++*newmv_count;
          ++*ref_match_count;
        }

        if (ref == 1 && candidate->ref_frame[0] != candidate->ref_frame[1]) {
          if (is_inter_compound_mode(candidate->mode) &&
              compound_ref1_mode(candidate->mode) == NEWMV)
            ++*newmv_count;
          ++*ref_match_count;
        }
      } else if (cand_tip_ref_frames[ref] == rf[0]) {
        int_mv this_refmv = cand_tip_mvs[ref];

        if (*drl_pr_count < MAX_PR_NUM) {
          for (index = 0; index < *refmv_count; ++index) {
            ++(*drl_pr_count);
            if (ref_mv_stack[index].this_mv.as_int == this_refmv.as_int) {
              ref_mv_weight[index] += weight;
              break;
            }
          }

          // Add a new item to the list.
          if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[index].this_mv = this_refmv;
            ref_mv_stack[index].row_offset = row_offset;
            ref_mv_stack[index].col_offset = col_offset;
            ref_mv_stack[index].cwp_idx = CWP_EQUAL;
            ref_mv_weight[index] = weight;
            ++(*refmv_count);
          }
        } else {
          if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[*refmv_count].this_mv = this_refmv;
            ref_mv_stack[*refmv_count].row_offset = row_offset;
            ref_mv_stack[*refmv_count].col_offset = col_offset;
            ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
            ref_mv_weight[*refmv_count] = weight;
            ++(*refmv_count);
          }
        }
        if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
        ++*ref_match_count;
      } else if (add_more_mvs && ref == 0 && is_tip_ref_frame(rf[0]) &&
                 candidate->ref_frame[0] == tip_ref->ref_frame[0] &&
                 candidate->ref_frame[1] == tip_ref->ref_frame[1] &&
                 cm->features.tip_frame_mode) {
        int_mv cand_mvs[2];
        for (int cand_ref = 0; cand_ref < 2; ++cand_ref) {
          cand_mvs[cand_ref] = get_block_mv(candidate, submi, cand_ref);
        }

        int_mv cand_linear_mv;
        cand_linear_mv.as_mv.row =
            cand_mvs[0].as_mv.row - cand_mvs[1].as_mv.row;
        cand_linear_mv.as_mv.col =
            cand_mvs[0].as_mv.col - cand_mvs[1].as_mv.col;

        int_mv cand_projected_mv_0;
        tip_get_mv_projection(&cand_projected_mv_0.as_mv, cand_linear_mv.as_mv,
                              tip_ref->ref_frames_offset_sf[0]);

        int_mv derived_mv;
        derived_mv.as_mv.row =
            cand_mvs[0].as_mv.row - cand_projected_mv_0.as_mv.row;
        derived_mv.as_mv.col =
            cand_mvs[0].as_mv.col - cand_projected_mv_0.as_mv.col;

        const int clamp_max = MV_UPP - 1;
        const int clamp_min = MV_LOW + 1;
        derived_mv.as_mv.row =
            clamp(derived_mv.as_mv.row, clamp_min, clamp_max);
        derived_mv.as_mv.col =
            clamp(derived_mv.as_mv.col, clamp_min, clamp_max);

        if (*drl_dr_pr_count < MAX_DR_PR_NUM) {
          for (index = 0; index < *derived_mv_count; ++index) {
            ++(*drl_dr_pr_count);
            if (derived_mv_stack[index].this_mv.as_int == derived_mv.as_int) {
              derived_mv_weight[index] += weight;
              break;
            }
          }
          // Add a new item to the list.
          if (index == *derived_mv_count &&
#if MAX_DR_STACK_SIZE
              *derived_mv_count < MAX_DR_STACK_SIZE
#else
              *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
          ) {
            derived_mv_stack[index].this_mv = derived_mv;
            derived_mv_weight[index] = weight;
            derived_mv_stack[index].cwp_idx = CWP_EQUAL;
            ++(*derived_mv_count);
          }
        } else {
          if (
#if MAX_DR_STACK_SIZE
              *derived_mv_count < MAX_DR_STACK_SIZE
#else
              *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
          ) {
            derived_mv_stack[*derived_mv_count].this_mv = derived_mv;
            derived_mv_weight[*derived_mv_count] = weight;
            derived_mv_stack[*derived_mv_count].cwp_idx = CWP_EQUAL;
            ++(*derived_mv_count);
          }
        }
      } else if (add_more_mvs &&
                 (is_inter_ref_frame(candidate->ref_frame[ref]) ||
                  is_tip_ref_frame(candidate->ref_frame[0])) &&
                 rf[0] != INTRA_FRAME && !is_tip_ref_frame(rf[0])) {
        int_mv cand_refmv;
        MV_REFERENCE_FRAME cand_ref_frame;
        if (is_tip_ref_frame(candidate->ref_frame[0])) {
          cand_refmv.as_int = cand_tip_mvs[ref].as_int;
          cand_ref_frame = cand_tip_ref_frames[ref];
        } else {
          cand_refmv = get_block_mv(candidate, submi, ref);
          cand_ref_frame = candidate->ref_frame[ref];
        }

        int this_tpl_row = mi_row >> 1;
        int this_tpl_col = mi_col >> 1;
        if (cm->seq_params.enable_mv_traj && cm->features.allow_ref_frame_mvs &&
            cm->id_offset_map_rows[rf[0]][this_tpl_row][this_tpl_col].as_int !=
                INVALID_MV &&
            cm->id_offset_map_rows[cand_ref_frame][this_tpl_row][this_tpl_col]
                    .as_int != INVALID_MV) {
          int_mv mv_traj_cand_ref =
              cm->id_offset_map_rows[cand_ref_frame][this_tpl_row]
                                    [this_tpl_col];
          int_mv mv_traj_cur_ref =
              cm->id_offset_map_rows[rf[0]][this_tpl_row][this_tpl_col];

          const int clamp_max = MV_UPP - 1;
          const int clamp_min = MV_LOW + 1;
          int_mv derived_mv;
          derived_mv.as_mv.row = clamp(
              cand_refmv.as_mv.row +
                  (mv_traj_cur_ref.as_mv.row - mv_traj_cand_ref.as_mv.row),
              clamp_min, clamp_max);
          derived_mv.as_mv.col = clamp(
              cand_refmv.as_mv.col +
                  (mv_traj_cur_ref.as_mv.col - mv_traj_cand_ref.as_mv.col),
              clamp_min, clamp_max);

          if (*drl_dr_pr_count < MAX_DR_PR_NUM) {
            for (index = 0; index < *derived_mv_count; ++index) {
              ++(*drl_dr_pr_count);
              if (derived_mv_stack[index].this_mv.as_int == derived_mv.as_int) {
                derived_mv_weight[index] += weight;
                break;
              }
            }
            // Add a new item to the list.
            if (index == *derived_mv_count &&
#if MAX_DR_STACK_SIZE
                *derived_mv_count < MAX_DR_STACK_SIZE
#else
                *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
            ) {
              derived_mv_stack[index].this_mv = derived_mv;
              derived_mv_weight[index] = weight;
              derived_mv_stack[index].cwp_idx = CWP_EQUAL;
              ++(*derived_mv_count);
            }
          } else {
            if (
#if MAX_DR_STACK_SIZE
                *derived_mv_count < MAX_DR_STACK_SIZE
#else
                *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
            ) {
              derived_mv_stack[*derived_mv_count].this_mv = derived_mv;
              derived_mv_weight[*derived_mv_count] = weight;
              derived_mv_stack[*derived_mv_count].cwp_idx = CWP_EQUAL;
              ++(*derived_mv_count);
            }
          }
        } else {
          const int cur_blk_ref_side = cm->ref_frame_side[rf[0]];
          const int cand_blk_ref_side = cm->ref_frame_side[cand_ref_frame];

          const int same_side =
              (cur_blk_ref_side > 0 && cand_blk_ref_side > 0) ||
              (cur_blk_ref_side == 0 && cand_blk_ref_side == 0);

          if (same_side) {
            const int cur_to_ref_dist = cm->ref_frame_relative_dist[rf[0]];
            const int cand_to_ref_dist =
                cm->ref_frame_relative_dist[cand_ref_frame];

            int_mv this_refmv;
            get_mv_projection(&this_refmv.as_mv, cand_refmv.as_mv,
                              cur_to_ref_dist, cand_to_ref_dist);

            if (*drl_dr_pr_count < MAX_DR_PR_NUM) {
              for (index = 0; index < *derived_mv_count; ++index) {
                ++(*drl_dr_pr_count);
                if (derived_mv_stack[index].this_mv.as_int ==
                    this_refmv.as_int) {
                  derived_mv_weight[index] += weight;
                  break;
                }
              }

              // Add a new item to the list.
              if (index == *derived_mv_count &&
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[index].this_mv = this_refmv;
                derived_mv_weight[index] = weight;
                derived_mv_stack[index].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            } else {
              if (
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[*derived_mv_count].this_mv = this_refmv;
                derived_mv_weight[*derived_mv_count] = weight;
                derived_mv_stack[*derived_mv_count].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            }
          }
        }
      }
    }
  } else {
    if (is_tip_ref_frame(candidate->ref_frame[0]) &&
        candidate->ref_frame[1] == NONE_FRAME &&
        rf[0] == tip_ref->ref_frame[0] && rf[1] == tip_ref->ref_frame[1] &&
        cm->features.tip_frame_mode) {
      derive_ref_mv_candidate_from_tip_mode(
          cm, mi_row_cand, mi_col_cand, candidate, refmv_count, ref_match_count,
          newmv_count, ref_mv_stack, ref_mv_weight, weight, drl_pr_count);
    } else {
      // compound reference frame
      if (candidate->ref_frame[0] == rf[0] &&
          candidate->ref_frame[1] == rf[1]) {
        int_mv this_refmv[2];

        for (ref = 0; ref < 2; ++ref) {
          if (is_global_mv_block(candidate, gm_params[rf[ref]].wmtype))
            this_refmv[ref] = gm_mv_candidates[ref];
          else
            this_refmv[ref] = get_block_mv(candidate, submi, ref);
        }

        if (*drl_pr_count < MAX_PR_NUM) {
          for (index = 0; index < *refmv_count; ++index) {
            ++(*drl_pr_count);
            if ((ref_mv_stack[index].this_mv.as_int == this_refmv[0].as_int) &&
                (ref_mv_stack[index].comp_mv.as_int == this_refmv[1].as_int)) {
              ref_mv_weight[index] += weight;
              break;
            }
          }

          // Add a new item to the list.
          if (index == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[index].this_mv = this_refmv[0];
            ref_mv_stack[index].comp_mv = this_refmv[1];
            ref_mv_weight[index] = weight;
            ref_mv_stack[index].row_offset = OFFSET_NONSPATIAL;
            ref_mv_stack[index].col_offset = OFFSET_NONSPATIAL;
            ref_mv_stack[index].cwp_idx = candidate->cwp_idx;
            ++(*refmv_count);
          }
        } else {
          if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
            ref_mv_stack[*refmv_count].this_mv = this_refmv[0];
            ref_mv_stack[*refmv_count].comp_mv = this_refmv[1];
            ref_mv_weight[*refmv_count] = weight;
            ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
            ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
            ref_mv_stack[*refmv_count].cwp_idx = candidate->cwp_idx;
            ++(*refmv_count);
          }
        }
        if (have_newmv_in_inter_mode(candidate->mode)) ++*newmv_count;
        ++*ref_match_count;
      } else if (add_more_mvs) {
        if (cm->seq_params.enable_mv_traj && cm->features.allow_ref_frame_mvs &&
            rf[0] != rf[1] && is_inter_ref_frame(rf[0]) &&
            is_inter_ref_frame(rf[1])) {
          for (ref = 0; ref < 2; ref++) {
            if (!is_inter_ref_frame(candidate->ref_frame[ref]) ||
                is_tip_ref_frame(candidate->ref_frame[ref]))
              continue;
            const int_mv cand_refmv = get_block_mv(candidate, submi, ref);

            int this_tpl_row = mi_row >> 1;
            int this_tpl_col = mi_col >> 1;
            int_mv mv_traj_cand_ref =
                cm->id_offset_map_rows[candidate->ref_frame[ref]][this_tpl_row]
                                      [this_tpl_col];
            int_mv mv_traj_cur_ref0 =
                cm->id_offset_map_rows[rf[0]][this_tpl_row][this_tpl_col];
            int_mv mv_traj_cur_ref1 =
                cm->id_offset_map_rows[rf[1]][this_tpl_row][this_tpl_col];

            if (mv_traj_cand_ref.as_int == INVALID_MV ||
                mv_traj_cur_ref0.as_int == INVALID_MV ||
                mv_traj_cur_ref1.as_int == INVALID_MV) {
              continue;
            }

            int_mv this_refmv[2];
            const int clamp_max = MV_UPP - 1;
            const int clamp_min = MV_LOW + 1;
            this_refmv[0].as_mv.row = clamp(
                cand_refmv.as_mv.row +
                    (mv_traj_cur_ref0.as_mv.row - mv_traj_cand_ref.as_mv.row),
                clamp_min, clamp_max);
            this_refmv[0].as_mv.col = clamp(
                cand_refmv.as_mv.col +
                    (mv_traj_cur_ref0.as_mv.col - mv_traj_cand_ref.as_mv.col),
                clamp_min, clamp_max);
            this_refmv[1].as_mv.row = clamp(
                cand_refmv.as_mv.row +
                    (mv_traj_cur_ref1.as_mv.row - mv_traj_cand_ref.as_mv.row),
                clamp_min, clamp_max);
            this_refmv[1].as_mv.col = clamp(
                cand_refmv.as_mv.col +
                    (mv_traj_cur_ref1.as_mv.col - mv_traj_cand_ref.as_mv.col),
                clamp_min, clamp_max);
            if (*drl_dr_pr_count < MAX_DR_PR_NUM) {
              for (index = 0; index < *derived_mv_count; ++index) {
                ++(*drl_dr_pr_count);
                if ((derived_mv_stack[index].this_mv.as_int ==
                     this_refmv[0].as_int) &&
                    (derived_mv_stack[index].comp_mv.as_int ==
                     this_refmv[1].as_int)) {
                  derived_mv_weight[index] += weight;
                  break;
                }
              }

              // Add a new item to the list.
              if (index == *derived_mv_count &&
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[index].this_mv = this_refmv[0];
                derived_mv_stack[index].comp_mv = this_refmv[1];
                derived_mv_weight[index] = weight;
                derived_mv_stack[index].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            } else {
              if (
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[*derived_mv_count].this_mv = this_refmv[0];
                derived_mv_stack[*derived_mv_count].comp_mv = this_refmv[1];
                derived_mv_weight[*derived_mv_count] = weight;
                derived_mv_stack[*derived_mv_count].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            }
          }
        }

        // Compound reference frame, but only have one reference frame
        // is the same as the reference frame of the neighboring block
        int candidate_ref_idx0 = -1;
        int candidate_ref_idx1 = -1;
        int which_cand_ref = -1;
        if (candidate->ref_frame[0] == rf[0] ||
            candidate->ref_frame[1] == rf[0]) {
          candidate_ref_idx0 = 0;
          candidate_ref_idx1 = 1;
          which_cand_ref = (candidate->ref_frame[0] == rf[0]) ? 0 : 1;
        } else if (candidate->ref_frame[0] == rf[1] ||
                   candidate->ref_frame[1] == rf[1]) {
          candidate_ref_idx0 = 1;
          candidate_ref_idx1 = 0;
          which_cand_ref = (candidate->ref_frame[0] == rf[1]) ? 0 : 1;
        }

        if (candidate_ref_idx0 != -1 && candidate_ref_idx1 != -1) {
          int_mv this_refmv[2];
          const int is_gm_block = is_global_mv_block(
              candidate, gm_params[rf[candidate_ref_idx0]].wmtype);
          this_refmv[candidate_ref_idx0] =
              is_gm_block ? gm_mv_candidates[candidate_ref_idx0]
                          : get_block_mv(candidate, submi, which_cand_ref);

          int cand_idx = 0;
          for (cand_idx = 0; cand_idx < *single_mv_count; ++cand_idx) {
            if (single_mv[cand_idx].ref_frame == rf[candidate_ref_idx1]) {
              this_refmv[candidate_ref_idx1].as_int =
                  single_mv[cand_idx].mv.as_int;
              break;
            }
          }

          // Add a new item to the list.
          if (cand_idx < *single_mv_count) {
            if (*drl_dr_pr_count < MAX_DR_PR_NUM) {
              for (index = 0; index < *derived_mv_count; ++index) {
                ++(*drl_dr_pr_count);
                if ((derived_mv_stack[index].this_mv.as_int ==
                     this_refmv[0].as_int) &&
                    (derived_mv_stack[index].comp_mv.as_int ==
                     this_refmv[1].as_int)) {
                  derived_mv_weight[index] += weight;
                  break;
                }
              }

              // Add a new item to the list.
              if (index == *derived_mv_count &&
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[index].this_mv = this_refmv[0];
                derived_mv_stack[index].comp_mv = this_refmv[1];
                derived_mv_weight[index] = weight;
                derived_mv_stack[index].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            } else {
              if (
#if MAX_DR_STACK_SIZE
                  *derived_mv_count < MAX_DR_STACK_SIZE
#else
                  *derived_mv_count < MAX_REF_MV_STACK_SIZE
#endif
              ) {
                derived_mv_stack[*derived_mv_count].this_mv = this_refmv[0];
                derived_mv_stack[*derived_mv_count].comp_mv = this_refmv[1];
                derived_mv_weight[*derived_mv_count] = weight;
                derived_mv_stack[*derived_mv_count].cwp_idx = CWP_EQUAL;
                ++(*derived_mv_count);
              }
            }
          }

          // Add the candidate to single MV stack
          if (*drl_dr_single_pr_count < MAX_DR_PR_NUM) {
            for (cand_idx = 0; cand_idx < *single_mv_count; ++cand_idx) {
              ++(*drl_dr_single_pr_count);
              if (single_mv[cand_idx].ref_frame == rf[candidate_ref_idx0] &&
                  (single_mv[cand_idx].mv.as_int ==
                   this_refmv[candidate_ref_idx0].as_int)) {
                break;
              }
            }

            if (cand_idx == *single_mv_count &&
#if MAX_DR_STACK_SIZE
                *single_mv_count < MAX_DR_STACK_SIZE
#else
                *single_mv_count < MAX_REF_MV_STACK_SIZE
#endif
            ) {
              single_mv[cand_idx].mv.as_int =
                  this_refmv[candidate_ref_idx0].as_int;
              single_mv[cand_idx].ref_frame = rf[candidate_ref_idx0];
              ++(*single_mv_count);
            }
          } else {
            if (
#if MAX_DR_STACK_SIZE
                *single_mv_count < MAX_DR_STACK_SIZE
#else
                *single_mv_count < MAX_REF_MV_STACK_SIZE
#endif
            ) {
              single_mv[*single_mv_count].mv.as_int =
                  this_refmv[candidate_ref_idx0].as_int;
              single_mv[*single_mv_count].ref_frame = rf[candidate_ref_idx0];
              ++(*single_mv_count);
            }
          }
        }
      }
    }
  }
}

// Check if the candidate block has valid warp parameters
// Return 1 if the candidate warp parameters are valid
static INLINE uint8_t is_valid_warp_parameters(
    const AV2_COMMON *cm, const MB_MODE_INFO *neighbor_mbmi,
    const int ref_frame, WarpedMotionParams *neighbor_params) {
  (void)cm;
#if !COMPOUND_WARP_LINE_BUFFER_REDUCTION
  if (is_warp_mode(neighbor_mbmi->motion_mode)) {
    for (int ref_idx = 0;
         ref_idx < 1 + is_inter_compound_mode(neighbor_mbmi->mode); ref_idx++) {
      int is_same_ref = (neighbor_mbmi->ref_frame[ref_idx] == ref_frame);
      if (is_same_ref && !neighbor_mbmi->wm_params[ref_idx].invalid &&
          neighbor_params) {
        *neighbor_params = neighbor_mbmi->wm_params[ref_idx];
        return 1;
      }
    }
  }
#else
  int is_same_ref = (neighbor_mbmi->ref_frame[0] == ref_frame);
  if (is_same_ref && is_warp_mode(neighbor_mbmi->motion_mode) &&
      !neighbor_mbmi->wm_params[0].invalid && neighbor_params) {
    *neighbor_params = neighbor_mbmi->wm_params[0];
    return 1;
  }
#endif  // !COMPOUND_WARP_LINE_BUFFER_REDUCTION
  return 0;
}

// Insert the candidate warp parameters to the WRL
void insert_neighbor_warp_candidate(
    WARP_CANDIDATE warp_candidates[MAX_WARP_REF_CANDIDATES],
    const WarpedMotionParams *neigh_params, uint8_t curr_num_of_candidates,
    const WarpProjectionType proj_type) {
  if (neigh_params)
    warp_candidates[curr_num_of_candidates].wm_params = *neigh_params;
  warp_candidates[curr_num_of_candidates].proj_type = proj_type;
}

// Check if the candidate warp parameters are already in the list or not.

void check_this_warp_candidate(
    const AV2_COMMON *cm, const MB_MODE_INFO *const neighbor_mbmi,
    WARP_CANDIDATE warp_candidates[MAX_WARP_REF_CANDIDATES],
    const int ref_frame, const int max_num_of_candidates,
    uint8_t *curr_num_of_candidates, const WarpProjectionType proj_type) {
  if (!is_inter_block(neighbor_mbmi, SHARED_PART)) return;
  if (is_intrabc_block(neighbor_mbmi, SHARED_PART)) return;

  WarpedMotionParams neigh_params;
  if (*curr_num_of_candidates < max_num_of_candidates &&
      is_valid_warp_parameters(cm, neighbor_mbmi, ref_frame, &neigh_params)) {
    insert_neighbor_warp_candidate(warp_candidates, &neigh_params,
                                   *curr_num_of_candidates, proj_type);
    ++(*curr_num_of_candidates);
  }
}

// Check whether SMVP candidates in the second column are redundant.
static AVM_INLINE int is_valid_candidate(const MACROBLOCKD *xd, int mi_row,
                                         int mi_col, int row_offset,
                                         int col_offset, int check_col_offset) {
  const TileInfo *const tile = &xd->tile;
  const POSITION mi_pos = { row_offset, col_offset };
  if (is_inside(tile, mi_col, mi_row, &mi_pos)) {
    const MB_MODE_INFO *const cand_col =
        xd->mi[row_offset * xd->mi_stride + col_offset];
    // If outer column is within the tile, inner column must be within the tile.
    // Thus, no need to check if inner column is within the tile
    const MB_MODE_INFO *const cand_other_col =
        xd->mi[row_offset * xd->mi_stride + check_col_offset];
    if (cand_col->mi_col_start == cand_other_col->mi_col_start) {
      return 0;
    }

    return 1;
  }

  return 0;
}

static AVM_INLINE void scan_blk_mbmi_ctx(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, const int mi_row,
    const int mi_col, const MV_REFERENCE_FRAME rf[2], int row_offset,
    int col_offset, uint8_t *ref_match_count, uint8_t *newmv_count) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;

  mi_pos.row = row_offset;
  mi_pos.col = col_offset;
  if (is_inside(tile, mi_col, mi_row, &mi_pos)) {
    const MB_MODE_INFO *const candidate =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    add_ref_mv_candidate_ctx(candidate, ref_match_count, newmv_count, cm, rf,
                             xd->mi[0]);
  }
}

static AVM_INLINE void scan_blk_mbmi(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, const int mi_row,
    const int mi_col, const MV_REFERENCE_FRAME rf[2], int row_offset,
    int col_offset, CANDIDATE_MV *ref_mv_stack, uint16_t *ref_mv_weight,
    uint8_t *ref_match_count, uint8_t *newmv_count, int_mv *gm_mv_candidates,
    int add_more_mvs, SINGLE_MV_CANDIDATE *single_mv, uint8_t *single_mv_count,
    CANDIDATE_MV *derived_mv_stack, uint16_t *derived_mv_weight,
    uint8_t *derived_mv_count,
    WARP_CANDIDATE warp_param_stack[MAX_WARP_REF_CANDIDATES],
    int max_num_of_warp_candidates, uint8_t *valid_num_warp_candidates,
    MV_REFERENCE_FRAME ref_frame, uint8_t *refmv_count, int *drl_pr_count,
    int *drl_dr_pr_count, int *drl_dr_single_pr_count) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;

  mi_pos.row = row_offset;
  mi_pos.col = col_offset;

  if (is_inside(tile, mi_col, mi_row, &mi_pos)) {
    const MB_MODE_INFO *const candidate =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    const SUBMB_INFO *const submi =
        xd->submi[mi_pos.row * xd->mi_stride + mi_pos.col];

    uint16_t weight = ADJACENT_SMVP_WEIGHT;
    // Don't add weight to (-1,-1) which is in the outer area
    if (row_offset == -1 && col_offset == -1) {
      weight = OTHER_SMVP_WEIGHT;
    }
    // Don't add weight to col_offset < -1 which is in the outer area
    if (col_offset < -1) {
      weight = OTHER_SMVP_WEIGHT;
    }

    const int cand_mi_row = xd->mi_row + mi_pos.row;
    const int cand_mi_col = xd->mi_col + mi_pos.col;

    if (warp_param_stack && valid_num_warp_candidates &&
        max_num_of_warp_candidates) {
      check_this_warp_candidate(cm, candidate, warp_param_stack, ref_frame,
                                max_num_of_warp_candidates,
                                valid_num_warp_candidates, PROJ_SPATIAL);
    }

    if (*refmv_count >= MAX_REF_MV_STACK_SIZE) return;

    add_ref_mv_candidate(
        mi_row, mi_col, cand_mi_row, cand_mi_col, candidate, submi, rf,
        refmv_count, ref_match_count, newmv_count, ref_mv_stack, ref_mv_weight,
        gm_mv_candidates, cm->global_motion, cm, add_more_mvs, single_mv,
        single_mv_count, derived_mv_stack, derived_mv_weight, derived_mv_count,
        xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART], row_offset,
        col_offset, weight, drl_pr_count, drl_dr_pr_count,
        drl_dr_single_pr_count);
  }  // Analyze a single 8x8 block motion information.
}

static int has_top_right(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                         int mi_row, int mi_col, int n4_w) {
  const int sb_mi_size = mi_size_wide[cm->sb_size];
  const int mask_row = mi_row & (sb_mi_size - 1);
  const int mask_col = mi_col & (sb_mi_size - 1);

  if (n4_w > mi_size_wide[BLOCK_64X64]) return 0;

  const int tr_mask_row = mask_row - 1;
  const int tr_mask_col = mask_col + n4_w;
  int has_tr;

  if (tr_mask_row < 0) {
    // The top-right block is in a superblock above the current sb row. If it is
    // in the current tile or a previously coded one, it has been coded.
    // Otherwise later the tile boundary checker will figure out whether it is
    // available.
    has_tr = 1;
  } else if (tr_mask_col >= sb_mi_size) {
    // The top-right block is in the superblock on the right side, therefore it
    // is not coded yet.
    has_tr = 0;
  } else {
    // For a general case, we use is_mi_coded array for the current superblock
    // to figure out the availability.
    const int tr_offset = tr_mask_row * xd->is_mi_coded_stride + tr_mask_col;

    has_tr = (xd->is_mi_coded[av2_get_sdp_idx(xd->tree_type)][tr_offset] == 1);
  }

  return has_tr;
}

static int has_bottom_left(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                           int mi_row, int mi_col, int n4_h) {
  const int sb_mi_size = mi_size_wide[cm->sb_size];
  const int mask_row = mi_row & (sb_mi_size - 1);
  const int mask_col = mi_col & (sb_mi_size - 1);

  if (n4_h > mi_size_high[BLOCK_64X64]) return 0;

  const int bl_mask_row = mask_row + n4_h;
  const int bl_mask_col = mask_col - 1;

  if (bl_mask_row >= sb_mi_size) {
    // If the bottom right block is in the superblock row below, then it's not
    // ready yet
    // TODO(chiyotsai): Take care of tile boundary
    return 0;
  } else if (bl_mask_col < 0) {
    // The bottom-left block is in a superblock left of the current sb and it is
    // in the same sb row.  If it in the same tile, then it has been coded.
    // Otherwise, boundary check will figure out when it's available
    return 1;
  } else {
    // For a general case, we use is_mi_coded array for the current superblock
    // to figure out the availability.
    const int bl_offset = bl_mask_row * xd->is_mi_coded_stride + bl_mask_col;

    return xd->is_mi_coded[av2_get_sdp_idx(xd->tree_type)][bl_offset] == 1;
  }
}

static AVM_INLINE int compute_cur_to_ref_dist(const AV2_COMMON *cm,
                                              MV_REFERENCE_FRAME ref_frame) {
  const int cur_frame_index = cm->cur_frame->display_order_hint;
  const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
  const int frame_index = buf->display_order_hint;
  const int cur_ref_offset = get_relative_dist(&cm->seq_params.order_hint_info,
                                               cur_frame_index, frame_index);
  return cur_ref_offset;
}

static int add_tpl_ref_mv(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                          int mi_row, int mi_col, MV_REFERENCE_FRAME ref_frame,
                          int blk_row, int blk_col, uint8_t *const refmv_count,
                          int *added_tmvp_cnt,
                          CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
                          uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
                          int *drl_pr_count) {
  if (*refmv_count >= MAX_REF_MV_STACK_SIZE) return 0;

  POSITION mi_pos;
  mi_pos.row = blk_row;
  mi_pos.col = blk_col;

  if (!is_inside(&xd->tile, mi_col, mi_row, &mi_pos)) return 0;

  const int tpl_row = ((mi_row + mi_pos.row) >> TMVP_SHIFT_BITS);
  const int tpl_col = ((mi_col + mi_pos.col) >> TMVP_SHIFT_BITS);

  assert((((tpl_row << TMVP_SHIFT_BITS) + 1) >= mi_row) &&
         (mi_row + mi_size_high[xd->mi[0]->sb_type[PLANE_TYPE_Y]] >
          tpl_row << TMVP_SHIFT_BITS));
  assert((((tpl_col << TMVP_SHIFT_BITS) + 1) >= mi_col) &&
         (mi_col + mi_size_wide[xd->mi[0]->sb_type[PLANE_TYPE_Y]] >
          tpl_col << TMVP_SHIFT_BITS));

  const TPL_MV_REF *prev_frame_mvs = cm->tpl_mvs_rows[tpl_row] + tpl_col;

  MV_REFERENCE_FRAME rf[2];
  av2_set_ref_frame(rf, ref_frame);

  if (is_tip_ref_frame(rf[0])) {
    return 0;
  }

  bool linear_available = prev_frame_mvs->mfmv0.as_int != INVALID_MV;
  bool mvtj_available[2] = { true, true };
  if (!cm->seq_params.enable_mv_traj || rf[0] == NONE_FRAME ||
      rf[0] >= cm->ref_frames_info.num_total_refs ||
      cm->id_offset_map_rows[rf[0]][tpl_row][tpl_col].as_int == INVALID_MV) {
    mvtj_available[0] = false;
  }
  if (!cm->seq_params.enable_mv_traj || rf[1] == NONE_FRAME ||
      rf[1] >= cm->ref_frames_info.num_total_refs ||
      cm->id_offset_map_rows[rf[1]][tpl_row][tpl_col].as_int == INVALID_MV) {
    mvtj_available[1] = false;
  }
  bool mvtj_ok = rf[1] == NONE_FRAME ? mvtj_available[0]
                                     : mvtj_available[0] && mvtj_available[1];
  if (!linear_available && !mvtj_ok) return 0;

  const int cur_frame_index = cm->cur_frame->display_order_hint;
  const RefCntBuffer *const buf_0 = get_ref_frame_buf(cm, rf[0]);
  const int frame0_index = buf_0->display_order_hint;
  const int cur_offset_0 = get_relative_dist(&cm->seq_params.order_hint_info,
                                             cur_frame_index, frame0_index);
  int idx;

  int_mv this_refmv;
  if (mvtj_available[0]) {
    this_refmv = cm->id_offset_map_rows[rf[0]][tpl_row][tpl_col];
  } else {
    assert(linear_available);
    get_mv_projection(&this_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                      cur_offset_0, prev_frame_mvs->ref_frame_offset);
  }

  uint16_t weight = TMVP_WEIGHT;

  if (rf[1] == NONE_FRAME) {
    if (abs(cur_offset_0) <= 2) {
      weight = HIGH_PRIORITY_TMVP_WEIGHT;
    }

    if (*drl_pr_count < MAX_PR_NUM) {
      for (idx = 0; idx < *refmv_count; ++idx) {
        ++(*drl_pr_count);
        if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int) break;
      }

      if (idx < *refmv_count) ref_mv_weight[idx] += weight;

      if (idx == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[idx].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].cwp_idx = CWP_EQUAL;
        ref_mv_weight[idx] = weight;
        ++(*refmv_count);
        ++(*added_tmvp_cnt);
      }
    } else {
      if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[*refmv_count].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
        ref_mv_weight[*refmv_count] = weight;
        ++(*refmv_count);
        ++(*added_tmvp_cnt);
      }
    }
  } else {
    // Process compound inter mode
    int_mv comp_refmv;
    if (mvtj_available[1]) {
      comp_refmv = cm->id_offset_map_rows[rf[1]][tpl_row][tpl_col];
    } else {
      assert(linear_available);
      const RefCntBuffer *const buf_1 = get_ref_frame_buf(cm, rf[1]);
      const int frame1_index = buf_1->display_order_hint;
      const int cur_offset_1 = get_relative_dist(
          &cm->seq_params.order_hint_info, cur_frame_index, frame1_index);
      get_mv_projection(&comp_refmv.as_mv, prev_frame_mvs->mfmv0.as_mv,
                        cur_offset_1, prev_frame_mvs->ref_frame_offset);
    }

    if (*drl_pr_count < MAX_PR_NUM) {
      for (idx = 0; idx < *refmv_count; ++idx) {
        ++(*drl_pr_count);
        if (this_refmv.as_int == ref_mv_stack[idx].this_mv.as_int &&
            comp_refmv.as_int == ref_mv_stack[idx].comp_mv.as_int)
          break;
      }

      if (idx < *refmv_count) ref_mv_weight[idx] += weight;

      if (idx == *refmv_count && *refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[idx].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[idx].comp_mv.as_int = comp_refmv.as_int;
        ref_mv_stack[idx].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].cwp_idx = CWP_EQUAL;
        ref_mv_weight[idx] = weight;
        ++(*refmv_count);
        ++(*added_tmvp_cnt);
      }
    } else {
      if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[*refmv_count].this_mv.as_int = this_refmv.as_int;
        ref_mv_stack[*refmv_count].comp_mv.as_int = comp_refmv.as_int;
        ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
        ref_mv_weight[*refmv_count] = weight;
        ++(*refmv_count);
        ++(*added_tmvp_cnt);
      }
    }
  }

  return 1;
}

// This function determines which refmv bank list should be used for the
// given input reference frame.
// In total, 9 refmv bank lists are required:
// 1) Single inter with reference frames 0~5, each has its own refmv bank list.
// 2) Compound inter with reference frame pairs [0, 0] and [0, 1],
//    each has its own refmv bank list.
// 3) All remaining inter predictions share a single refmv bank list.
static AVM_INLINE int get_rmb_list_index(const MV_REFERENCE_FRAME ref_frame) {
  MV_REFERENCE_FRAME rf[2];
  av2_set_ref_frame(rf, ref_frame);

  if (rf[0] == 0 && rf[1] == NONE_FRAME) {
    return 0;
  } else if (rf[0] == 1 && rf[1] == NONE_FRAME) {
    return 1;
  } else if (rf[0] == 2 && rf[1] == NONE_FRAME) {
    return 2;
  } else if (rf[0] == 3 && rf[1] == NONE_FRAME) {
    return 3;
  } else if (rf[0] == 4 && rf[1] == NONE_FRAME) {
    return 4;
  } else if (rf[0] == 5 && rf[1] == NONE_FRAME) {
    return 5;
  } else if (rf[0] == 0 && rf[1] == 0) {
    return 6;
  } else if (rf[0] == 0 && rf[1] == 1) {
    return 7;
  } else {
    return 8;
  }
}

static AVM_INLINE bool check_rmb_cand(
    CANDIDATE_MV cand_mv, CANDIDATE_MV *ref_mv_stack, uint16_t *ref_mv_weight,
    uint8_t *refmv_count, int is_comp, int mi_row, int mi_col, int block_width,
    int block_height, int frame_width, int frame_height, int *drl_pr_count) {
  // Check if the MV candidate is already existing in the ref MV stack.
  if (*drl_pr_count < MAX_PR_NUM) {
    for (int i = 0; i < *refmv_count; ++i) {
      ++(*drl_pr_count);
      if (ref_mv_stack[i].this_mv.as_int == cand_mv.this_mv.as_int &&
          (!is_comp ||
           ref_mv_stack[i].comp_mv.as_int == cand_mv.comp_mv.as_int)) {
        return false;
      }
    }
  }

  // Check if the MV candidate is pointing to ref block inside frame boundary.
  for (int i = 0; i < 1 + is_comp; ++i) {
    const int mv_row =
        (i ? cand_mv.comp_mv.as_mv.row : cand_mv.this_mv.as_mv.row) / 8;
    const int mv_col =
        (i ? cand_mv.comp_mv.as_mv.col : cand_mv.this_mv.as_mv.col) / 8;
    const int ref_x = mi_col * MI_SIZE + mv_col;
    const int ref_y = mi_row * MI_SIZE + mv_row;
    if (ref_x <= -block_width || ref_y <= -block_height ||
        ref_x >= frame_width || ref_y >= frame_height) {
      return false;
    }
  }

  ref_mv_stack[*refmv_count] = cand_mv;
  ref_mv_weight[*refmv_count] = REF_CAT_LEVEL;
  ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
  ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
  ref_mv_stack[*refmv_count].cwp_idx = cand_mv.cwp_idx;
  ++*refmv_count;

  return true;
}

// Add a BV candidate to ref MV stack without duplicate check
static AVM_INLINE bool add_to_ref_bv_list(CANDIDATE_MV cand_mv,
                                          CANDIDATE_MV *ref_mv_stack,
                                          uint16_t *ref_mv_weight,
                                          uint8_t *refmv_count) {
  ref_mv_stack[*refmv_count] = cand_mv;
  ref_mv_weight[*refmv_count] = REF_CAT_LEVEL;
  ref_mv_stack[*refmv_count].cwp_idx = cand_mv.cwp_idx;
  ++*refmv_count;

  return true;
}

static AVM_INLINE void add_tmvp_candidate(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref_frame,
    uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE], int mi_row, int mi_col,
    int *drl_pr_count) {
  MV_REFERENCE_FRAME rf[2];
  av2_set_ref_frame(rf, ref_frame);
  if (cm->features.allow_ref_frame_mvs &&
      (xd->mi[0]->skip_mode || rf[0] != rf[1]) &&
      !xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART]) {
    const int blk_row_end = AVMMIN(xd->height, mi_size_high[BLOCK_64X64]);
    const int blk_col_end = AVMMIN(xd->width, mi_size_wide[BLOCK_64X64]);

    const int step_h = (xd->height >= mi_size_high[BLOCK_64X64])
                           ? mi_size_high[BLOCK_16X16]
                           : mi_size_high[BLOCK_8X8];
    const int step_w = (xd->width >= mi_size_wide[BLOCK_64X64])
                           ? mi_size_wide[BLOCK_16X16]
                           : mi_size_wide[BLOCK_8X8];

    int added_tmvp_cnt = 0;
    const MVP_UNIT_STATUS tmvp_units_status[TMVP_SEARCH_COUNT] = {
      { blk_row_end >= step_h && blk_col_end >= step_w, blk_row_end - step_h,
        blk_col_end - step_w },
      { (blk_row_end >= 3 * step_h) || (blk_col_end >= 3 * step_w),
        blk_row_end >> 1, blk_col_end >> 1 },
    };

    for (int iter = 0; iter < TMVP_SEARCH_COUNT; ++iter) {
      if (added_tmvp_cnt) break;
      if (tmvp_units_status[iter].is_available) {
        add_tpl_ref_mv(cm, xd, mi_row, mi_col, ref_frame,
                       tmvp_units_status[iter].row_offset,
                       tmvp_units_status[iter].col_offset, refmv_count,
                       &added_tmvp_cnt, ref_mv_stack, ref_mv_weight,
                       drl_pr_count);
      }
    }
  }
}

// This function determines whether to put TMVP candidate before
// adjacent SMVP candidates based on some predefined conditions
static AVM_INLINE int assign_tmvp_high_priority(const AV2_COMMON *cm,
                                                MV_REFERENCE_FRAME rf[2]) {
  if (cm->features.allow_ref_frame_mvs == 0) return 0;

  if (cm->seq_params.enable_drl_reorder == DRL_REORDER_ALWAYS) return 0;

  if (!cm->has_both_sides_refs &&
      (rf[1] == NONE_FRAME && is_inter_ref_frame(rf[0]) &&
       !is_tip_ref_frame(rf[0]))) {
    const int cur_to_ref_offset = abs(compute_cur_to_ref_dist(cm, rf[0]));
    if (cur_to_ref_offset <= 2) return 1;
  }
  return 0;
}

static AVM_INLINE void add_derived_smvp_candidates(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME *rf,
    uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
#if MAX_DR_STACK_SIZE
    CANDIDATE_MV derived_mv_stack[MAX_DR_STACK_SIZE],
#else
    CANDIDATE_MV derived_mv_stack[MAX_REF_MV_STACK_SIZE],
#endif
    uint8_t derived_mv_count, int *drl_pr_count) {
  const int max_ref_mv_count =
      xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART]
          ? AVMMIN(cm->features.max_bvp_drl_bits + 1, MAX_REF_BV_STACK_SIZE)
          : AVMMIN(cm->features.max_drl_bits + 1, MAX_REF_MV_STACK_SIZE);
  if (*refmv_count < max_ref_mv_count && derived_mv_count > 0) {
    fill_mvp_from_derived_smvp(rf, ref_mv_stack, ref_mv_weight, refmv_count,
                               derived_mv_stack, derived_mv_count,
                               max_ref_mv_count, drl_pr_count);
  }
}

static AVM_INLINE void add_ref_mv_bank_candidates(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME *rf,
    MV_REFERENCE_FRAME ref_frame, uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE], int *drl_pr_count) {
  const int ref_mv_limit =
      xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART]
          ? AVMMIN(cm->features.max_bvp_drl_bits + 1, MAX_REF_BV_STACK_SIZE)
          : AVMMIN(cm->features.max_drl_bits + 1, MAX_REF_MV_STACK_SIZE);
  // If open slots are available, fetch reference MVs from the ref mv banks.
  if (*refmv_count < ref_mv_limit) {
    const REF_MV_BANK *ref_mv_bank = &xd->ref_mv_bank;
    const int rmb_list_index = get_rmb_list_index(ref_frame);
    const CANDIDATE_MV *queue = ref_mv_bank->rmb_buffer[rmb_list_index];
    const MV_REFERENCE_FRAME *rmb_ref_frame = ref_mv_bank->rmb_ref_frame;
    const int count = ref_mv_bank->rmb_count[rmb_list_index];
    const int start_idx = ref_mv_bank->rmb_start_idx[rmb_list_index];
    const int is_comp = is_inter_ref_frame(rf[1]);
    const int block_width = xd->width * MI_SIZE;
    const int block_height = xd->height * MI_SIZE;
    for (int idx_bank = 0; idx_bank < count && *refmv_count < ref_mv_limit;
         ++idx_bank) {
      const int idx = (start_idx + count - 1 - idx_bank) % REF_MV_BANK_SIZE;
      const CANDIDATE_MV cand_mv = queue[idx];
      if (rmb_list_index == REF_MV_BANK_LIST_FOR_ALL_OTHERS &&
          rmb_ref_frame[idx] != ref_frame)
        continue;

      check_rmb_cand(cand_mv, ref_mv_stack, ref_mv_weight, refmv_count, is_comp,
                     xd->mi_row, xd->mi_col, block_width, block_height,
                     xd->plane[0].dst.width, xd->plane[0].dst.height,
                     drl_pr_count);
    }
  }
}

// Compute the offset between 8x8 aligned spatial neighbor block and mi_col
static AVM_INLINE int compute_aligned_offset(int mi_col, int col_offset) {
  // Around the super-block boundary, align mi_col to 8x8 grid
  const int aligned_mi_col = (mi_col >> 1) << 1;
  return aligned_mi_col + col_offset - mi_col;
}

// Check if the above spatial neighbor block is within the tile
static AVM_INLINE int is_above_smvp_available(const MACROBLOCKD *xd, int mi_col,
                                              int col_offset) {
  if (!xd->up_available) return 0;
  // Around the super-block boundary, align mi_col to 8x8 grid
  const int aligned_smvp_mi_col = ((mi_col >> 1) << 1) + col_offset;
  return aligned_smvp_mi_col >= xd->tile.mi_col_start &&
         aligned_smvp_mi_col < xd->tile.mi_col_end;
}

// Compute the availability and the offset of the above row neighbor block
static AVM_INLINE void get_row_smvp_states(const AV2_COMMON *cm,
                                           const MACROBLOCKD *xd,
                                           MVP_UNIT_STATUS row_smvp_state[4]) {
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int has_tr = has_top_right(cm, xd, mi_row, mi_col, xd->width);

  const int block_width_type =
      xd->width == 1 ? BLOCK_WIDTH_4
                     : (xd->width == 2 ? BLOCK_WIDTH_8 : BLOCK_WIDTH_OTHERS);
  const int is_sb_boundary = (mi_row % cm->mib_size == 0);

  const MVP_UNIT_STATUS row_smvp_all_states[2][BLOCK_WIDTH_TYPES][4] = {
    {
        // Within the super-block, similar to the existing algorithm,
        // access to information is allowed for each 4x4 unit.
        {
            // Block width is 4
            { xd->up_available, -1, (xd->width - 1) },
            { 0, -1, 0 },
            { has_tr, -1, xd->width },
            { xd->up_available && xd->left_available, -1, -1 },
        },
        {
            // Block width is 8
            { xd->up_available, -1, (xd->width - 1) },
            { xd->up_available, -1, 0 },
            { has_tr, -1, xd->width },
            { xd->up_available && xd->left_available, -1, -1 },
        },
        {
            // Block width is no less than 8
            { xd->up_available, -1, (xd->width - 1) },
            { xd->up_available, -1, 0 },
            { has_tr, -1, xd->width },
            { xd->up_available && xd->left_available, -1, -1 },
        },
    },
    {
        // Around the super-block boundary, to reduce the size of the line
        // buffer, access to information is allowed for each 8x8 unit. The
        // bottom-left 4x4 block's information is used to represent the entire
        // 8x8 block.
        {
            // Block width is 4
            { is_above_smvp_available(xd, mi_col, 0), -1,
              compute_aligned_offset(mi_col, 0) },
            { 0, -1, 0 },
            { has_tr && is_above_smvp_available(xd, mi_col, 2), -1,
              compute_aligned_offset(mi_col, 2) },
            { is_above_smvp_available(xd, mi_col, -2), -1,
              compute_aligned_offset(mi_col, -2) },
        },
        {
            // Block width is 8
            { is_above_smvp_available(xd, mi_col, 0), -1,
              compute_aligned_offset(mi_col, 0) },
            { 0, -1, 0 },
            { has_tr && is_above_smvp_available(xd, mi_col, 2), -1,
              compute_aligned_offset(mi_col, 2) },
            { is_above_smvp_available(xd, mi_col, -2), -1,
              compute_aligned_offset(mi_col, -2) },
        },
        {
            // Block width is no less than 8
            { is_above_smvp_available(xd, mi_col, xd->width - 2), -1,
              compute_aligned_offset(mi_col, xd->width - 2) },
            { is_above_smvp_available(xd, mi_col, 0), -1,
              compute_aligned_offset(mi_col, 0) },
            { has_tr && is_above_smvp_available(xd, mi_col, xd->width), -1,
              compute_aligned_offset(mi_col, xd->width) },
            { is_above_smvp_available(xd, mi_col, -2), -1,
              compute_aligned_offset(mi_col, -2) },
        },
    }
  };

  for (int i = 0; i < 4; i++) {
    row_smvp_state[i] =
        row_smvp_all_states[is_sb_boundary][block_width_type][i];
  }
}

// return 1 if valid point is found
// return 0 if the point is not valid
static AVM_INLINE int fill_warp_corner_projected_point(
    const MACROBLOCKD *xd, const MB_MODE_INFO *neighbor_mi,
    MV_REFERENCE_FRAME this_ref, const int pos_col, const int pos_row, int *pts,
    int *mvs, int *n_points) {
  // return if the source point is invalid
  if (pos_col < 0 || pos_row < 0) return 0;

  int mv_row = 0;
  int mv_col = 0;
  int find_match_ref = 0;
  const int is_neighbor_nonwarp_comp =
      has_second_ref(neighbor_mi) && !is_warp_mode(neighbor_mi->motion_mode);
  for (int idx = 0; idx < 1 + is_neighbor_nonwarp_comp; ++idx) {
    if (!is_inter_ref_frame(neighbor_mi->ref_frame[idx])) continue;
    if (neighbor_mi->ref_frame[idx] != this_ref) continue;
    if (is_warp_mode(neighbor_mi->motion_mode)) {
      int_mv warp_mv = get_warp_motion_vector_xy_pos(
          xd, &neighbor_mi->wm_params[idx], pos_col, pos_row,
          MV_PRECISION_ONE_EIGHTH_PEL);
      mv_row = warp_mv.as_mv.row;
      mv_col = warp_mv.as_mv.col;
    } else {
      mv_row = neighbor_mi->mv[idx].as_mv.row;
      mv_col = neighbor_mi->mv[idx].as_mv.col;
    }

    find_match_ref = 1;
    break;
  }

  if (!find_match_ref) return 0;
  pts[2 * (*n_points)] = pos_col;
  pts[2 * (*n_points) + 1] = pos_row;
  mvs[2 * (*n_points)] = mv_col;
  mvs[2 * (*n_points) + 1] = mv_row;
  ++(*n_points);
  return 1;
}

// Check all 3 neighbors to generate projected points
static AVM_INLINE int generate_points_from_corners(
    const MACROBLOCKD *xd, const MVP_UNIT_STATUS corner_unit_status[3],
    int *pts, int *mvs, int *np, MV_REFERENCE_FRAME ref_frame) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;
  int valid_points = 0;
  MV_REFERENCE_FRAME rf[2];
  av2_set_ref_frame(rf, ref_frame);
  MV_REFERENCE_FRAME this_ref = rf[0];
  const int bw = xd->width * MI_SIZE;
  const int bh = xd->height * MI_SIZE;

  mi_pos.row = corner_unit_status[0].row_offset;
  mi_pos.col = corner_unit_status[0].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      corner_unit_status[0].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    int pos_row = xd->mi_row * MI_SIZE;
    int pos_col = xd->mi_col * MI_SIZE;
    int valid = fill_warp_corner_projected_point(
        xd, neighbor_mi, this_ref, pos_col, pos_row, pts, mvs, np);
    if (valid) {
      valid_points++;
    }
  }

  mi_pos.row = corner_unit_status[1].row_offset;
  mi_pos.col = corner_unit_status[1].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      corner_unit_status[1].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    int pos_row = xd->mi_row * MI_SIZE;
    int pos_col = xd->mi_col * MI_SIZE + bw;
    int valid = fill_warp_corner_projected_point(
        xd, neighbor_mi, this_ref, pos_col, pos_row, pts, mvs, np);
    if (valid) {
      valid_points++;
    }
  }

  mi_pos.row = corner_unit_status[2].row_offset;
  mi_pos.col = corner_unit_status[2].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      corner_unit_status[2].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    int pos_row = xd->mi_row * MI_SIZE + bh;
    int pos_col = xd->mi_col * MI_SIZE;
    int valid = fill_warp_corner_projected_point(
        xd, neighbor_mi, this_ref, pos_col, pos_row, pts, mvs, np);
    if (valid) {
      valid_points++;
    }
  }

  assert(valid_points <= 3);
  return valid_points;
}

static int insert_mvp_candidate(
    MV_REFERENCE_FRAME rf[2], CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
    MV_COMP_DATA_TYPE this_mv_col, MV_COMP_DATA_TYPE this_mv_row,
    MV_COMP_DATA_TYPE comp_mv_col, MV_COMP_DATA_TYPE comp_mv_row,
    uint16_t weight, uint8_t *refmv_count, int *drl_pr_count) {
  CANDIDATE_MV ext_mv;
  ext_mv.this_mv.as_mv.col = this_mv_col;
  ext_mv.this_mv.as_mv.row = this_mv_row;
  ext_mv.comp_mv.as_mv.col = comp_mv_col;
  ext_mv.comp_mv.as_mv.row = comp_mv_row;

  if ((*drl_pr_count) < MAX_PR_NUM) {
    int idx = 0;
    for (idx = 0; idx < *refmv_count; ++idx) {
      ++(*drl_pr_count);
      if ((ref_mv_stack[idx].this_mv.as_int == ext_mv.this_mv.as_int) &&
          (rf[1] == NONE_FRAME ||
           (rf[1] > NONE_FRAME &&
            ref_mv_stack[idx].comp_mv.as_int == ext_mv.comp_mv.as_int))) {
        break;
      }
    }
    // Add a new item to the list.
    if (idx == *refmv_count) {
      ref_mv_stack[idx].this_mv.as_int = ext_mv.this_mv.as_int;
      ref_mv_stack[idx].comp_mv.as_int = ext_mv.comp_mv.as_int;
      ref_mv_stack[idx].row_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[idx].col_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[idx].cwp_idx = CWP_EQUAL;
      ref_mv_weight[idx] = weight;
      ++(*refmv_count);
      return 1;
    }
    return 0;
  } else {
    if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
      ref_mv_stack[*refmv_count].this_mv.as_int = ext_mv.this_mv.as_int;
      ref_mv_stack[*refmv_count].comp_mv.as_int = ext_mv.comp_mv.as_int;
      ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
      ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
      ref_mv_weight[*refmv_count] = weight;
      ++(*refmv_count);
      return 1;
    }
    return 0;
  }
  return 0;
}

static AVM_INLINE void setup_ref_mv_list(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref_frame,
    uint8_t *const refmv_count,
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[MAX_REF_MV_STACK_SIZE],
    int_mv mv_ref_list[MAX_MV_REF_CANDIDATES], int_mv *gm_mv_candidates,
    int mi_row, int mi_col,
    WARP_CANDIDATE warp_param_stack[MAX_WARP_REF_CANDIDATES],
    int max_num_of_warp_candidates, uint8_t *valid_num_warp_candidates) {
  const int has_bl = has_bottom_left(cm, xd, mi_row, mi_col, xd->height);
  MV_REFERENCE_FRAME rf[2];

  const TileInfo *const tile = &xd->tile;
  int max_col_offset = 0;
  const int col_adj = (xd->width < mi_size_wide[BLOCK_8X8]) && (mi_col & 0x01);

  av2_set_ref_frame(rf, ref_frame);
  *refmv_count = 0;

  int drl_pr_count = 0;
  int drl_dr_pr_count = 0;
  int drl_dr_single_pr_count = 0;

  /*
   * The constuction of the DRL after CWG-E021 was adopted:
   *
   * 1) Adjacent SMVP search; up to 9 blocks.
   * 2) TMVP search; up to 5 blocks.
   * 3) Non-adjacent SMVP search; up to 3 blocks in 2nd or 3rd column
   * 4) Find the adjacent SMVP candidate with the maximum weight, then switch
   *    it to the first place of the DRL.
   * 4) RefMV bank search.
   * 5) Derived SMVP search.
   * 6) Global MV is added if there is space in DRL.
   *
   */

  for (int k = 0; k < MAX_REF_MV_STACK_SIZE; k++) {
    ref_mv_stack[k].row_offset = OFFSET_NONSPATIAL;
    ref_mv_stack[k].col_offset = OFFSET_NONSPATIAL;
    ref_mv_stack[k].cwp_idx = CWP_EQUAL;
  }

  MVP_UNIT_STATUS row_smvp_state[4] = { 0 };
  get_row_smvp_states(cm, xd, row_smvp_state);

  // derive a warp model from the 3 corner MVs
  if (warp_param_stack && valid_num_warp_candidates &&
      *valid_num_warp_candidates < max_num_of_warp_candidates) {
// 0: top_left, top_right, bottom_left
// 1: top, top_right_next, bottom_left
#define WRL_CORNER_MVS_NUM 2
    const MVP_UNIT_STATUS wrl_corner_mv_status[WRL_CORNER_MVS_NUM][3] = {
      {
          { row_smvp_state[3].is_available, -1, row_smvp_state[3].col_offset },
          { row_smvp_state[0].is_available, -1, row_smvp_state[0].col_offset },
          { xd->left_available, (xd->height - 1), -1 },
      },
      {
          { row_smvp_state[1].is_available, -1, row_smvp_state[1].col_offset },
          { row_smvp_state[2].is_available, -1, row_smvp_state[2].col_offset },
          { xd->left_available, (xd->height - 1), -1 },
      }
    };

    int derived_wrl_count = 0;
    for (int iter = 0;
         iter < WRL_CORNER_MVS_NUM && derived_wrl_count < 1 &&
         (*valid_num_warp_candidates < max_num_of_warp_candidates);
         ++iter) {
      if (wrl_corner_mv_status[iter][0].is_available &&
          wrl_corner_mv_status[iter][1].is_available &&
          wrl_corner_mv_status[iter][2].is_available) {
        int mvs_32[2 * 3];
        int pts[2 * 3];
        int np = 0;
        WarpedMotionParams cand_warp_param = default_warp_params;
        const int valid_points = generate_points_from_corners(
            xd, wrl_corner_mv_status[iter], pts, mvs_32, &np, ref_frame);
        const int valid_model = get_model_from_corner_mvs(
            &cand_warp_param, pts, valid_points, mvs_32,
            xd->mi[0]->sb_type[PLANE_TYPE_Y],
            get_ref_scale_factors_const(cm, ref_frame));
        if (valid_model && !cand_warp_param.invalid) {
          insert_neighbor_warp_candidate(warp_param_stack, &cand_warp_param,
                                         *valid_num_warp_candidates,
                                         PROJ_SPATIAL);
          (*valid_num_warp_candidates)++;
          derived_wrl_count++;
        }
      }
    }
  }

  if (xd->left_available) {
    max_col_offset = -(MVREF_COLS << 1) + col_adj;

    if (xd->width < mi_size_wide[BLOCK_8X8])
      max_col_offset = -(2 << 1) + col_adj;

    max_col_offset = find_valid_col_offset(tile, mi_col, max_col_offset);
  }

  uint8_t col_match_count = 0;
  uint8_t row_match_count = 0;
  uint8_t newmv_count = 0;

#if MAX_DR_STACK_SIZE
  SINGLE_MV_CANDIDATE single_mv[MAX_DR_STACK_SIZE];
#else
  SINGLE_MV_CANDIDATE single_mv[MAX_REF_MV_STACK_SIZE];
#endif
  uint8_t single_mv_count = 0;
#if MAX_DR_STACK_SIZE
  CANDIDATE_MV derived_mv_stack[MAX_DR_STACK_SIZE];
#else
  CANDIDATE_MV derived_mv_stack[MAX_REF_MV_STACK_SIZE];
#endif
  uint16_t derived_mv_weight[MAX_REF_MV_STACK_SIZE];
  uint8_t derived_mv_count = 0;

  const int height_at_least_two = xd->left_available ? (xd->height > 1) : 0;

  const int is_tmvp_high_priority = assign_tmvp_high_priority(cm, rf);
  if (is_tmvp_high_priority) {
    add_tmvp_candidate(cm, xd, ref_frame, refmv_count, ref_mv_stack,
                       ref_mv_weight, mi_row, mi_col, &drl_pr_count);
  }

  if (xd->left_available) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, (xd->height - 1), -1,
                  ref_mv_stack, ref_mv_weight, &col_match_count, &newmv_count,
                  gm_mv_candidates, 1, single_mv, &single_mv_count,
                  derived_mv_stack, derived_mv_weight, &derived_mv_count,
                  warp_param_stack, max_num_of_warp_candidates,
                  valid_num_warp_candidates, ref_frame, refmv_count,
                  &drl_pr_count, &drl_dr_pr_count, &drl_dr_single_pr_count);
  }

  if (row_smvp_state[0].is_available) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, row_smvp_state[0].row_offset,
                  row_smvp_state[0].col_offset, ref_mv_stack, ref_mv_weight,
                  &row_match_count, &newmv_count, gm_mv_candidates, 1,
                  single_mv, &single_mv_count, derived_mv_stack,
                  derived_mv_weight, &derived_mv_count, warp_param_stack,
                  max_num_of_warp_candidates, valid_num_warp_candidates,
                  ref_frame, refmv_count, &drl_pr_count, &drl_dr_pr_count,
                  &drl_dr_single_pr_count);
  }

  if (height_at_least_two) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, 0, -1, ref_mv_stack,
                  ref_mv_weight, &col_match_count, &newmv_count,
                  gm_mv_candidates, 1, single_mv, &single_mv_count,
                  derived_mv_stack, derived_mv_weight, &derived_mv_count,
                  warp_param_stack, max_num_of_warp_candidates,
                  valid_num_warp_candidates, ref_frame, refmv_count,
                  &drl_pr_count, &drl_dr_pr_count, &drl_dr_single_pr_count);
  }

  if (row_smvp_state[1].is_available) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, row_smvp_state[1].row_offset,
                  row_smvp_state[1].col_offset, ref_mv_stack, ref_mv_weight,
                  &row_match_count, &newmv_count, gm_mv_candidates, 1,
                  single_mv, &single_mv_count, derived_mv_stack,
                  derived_mv_weight, &derived_mv_count, warp_param_stack,
                  max_num_of_warp_candidates, valid_num_warp_candidates,
                  ref_frame, refmv_count, &drl_pr_count, &drl_dr_pr_count,
                  &drl_dr_single_pr_count);
  }
  if (has_bl) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, xd->height, -1, ref_mv_stack,
                  ref_mv_weight, &col_match_count, &newmv_count,
                  gm_mv_candidates, 1, single_mv, &single_mv_count,
                  derived_mv_stack, derived_mv_weight, &derived_mv_count,
                  warp_param_stack, max_num_of_warp_candidates,
                  valid_num_warp_candidates, ref_frame, refmv_count,
                  &drl_pr_count, &drl_dr_pr_count, &drl_dr_single_pr_count);
  }

  if (row_smvp_state[2].is_available) {
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, row_smvp_state[2].row_offset,
                  row_smvp_state[2].col_offset, ref_mv_stack, ref_mv_weight,
                  &row_match_count, &newmv_count, gm_mv_candidates, 1,
                  single_mv, &single_mv_count, derived_mv_stack,
                  derived_mv_weight, &derived_mv_count, warp_param_stack,
                  max_num_of_warp_candidates, valid_num_warp_candidates,
                  ref_frame, refmv_count, &drl_pr_count, &drl_dr_pr_count,
                  &drl_dr_single_pr_count);
  }

  if (!is_tmvp_high_priority) {
    add_tmvp_candidate(cm, xd, ref_frame, refmv_count, ref_mv_stack,
                       ref_mv_weight, mi_row, mi_col, &drl_pr_count);
  }

  if (row_smvp_state[3].is_available) {
    uint8_t dummy_ref_match_count = 0;
    uint8_t dummy_new_mv_count = 0;
    scan_blk_mbmi(cm, xd, mi_row, mi_col, rf, row_smvp_state[3].row_offset,
                  row_smvp_state[3].col_offset, ref_mv_stack, ref_mv_weight,
                  &dummy_ref_match_count, &dummy_new_mv_count, gm_mv_candidates,
                  1, single_mv, &single_mv_count, derived_mv_stack,
                  derived_mv_weight, &derived_mv_count, warp_param_stack,
                  max_num_of_warp_candidates, valid_num_warp_candidates,
                  ref_frame, refmv_count, &drl_pr_count, &drl_dr_pr_count,
                  &drl_dr_single_pr_count);
  }

  const uint8_t nearest_refmv_count = *refmv_count;

  if (xd->left_available) {
    for (int idx = 2; idx <= MVREF_COLS; ++idx) {
      const int col_offset = -(idx << 1) + 1 + col_adj;
      const MVP_UNIT_STATUS col_units_status[SMVP_COL_SEARCH_COUNT] = {
        { 1, (xd->height - 1), col_offset },
        { xd->height > 1, 0, col_offset },
      };
      if (abs(col_offset) <= abs(max_col_offset)) {
        for (int unit_idx = 0; unit_idx < SMVP_COL_SEARCH_COUNT; unit_idx++) {
          if (col_units_status[unit_idx].is_available &&
              is_valid_candidate(xd, mi_row, mi_col,
                                 col_units_status[unit_idx].row_offset,
                                 col_units_status[unit_idx].col_offset, -1)) {
            scan_blk_mbmi(cm, xd, mi_row, mi_col, rf,
                          col_units_status[unit_idx].row_offset,
                          col_units_status[unit_idx].col_offset, ref_mv_stack,
                          ref_mv_weight, &col_match_count, &newmv_count,
                          gm_mv_candidates, 1, single_mv, &single_mv_count,
                          derived_mv_stack, derived_mv_weight,
                          &derived_mv_count, warp_param_stack,
                          max_num_of_warp_candidates, valid_num_warp_candidates,
                          ref_frame, refmv_count, &drl_pr_count,
                          &drl_dr_pr_count, &drl_dr_single_pr_count);
          }
        }
      }
    }
  }

  if (cm->seq_params.enable_drl_reorder == DRL_REORDER_ALWAYS ||
      (cm->seq_params.enable_drl_reorder == DRL_REORDER_CONSTRAINT &&
       (!is_tmvp_high_priority && nearest_refmv_count >= 4))) {
    if (nearest_refmv_count > 1) {
      int max_weight = ref_mv_weight[0];
      int max_weight_idx = 0;
      for (int idx = 1; idx < nearest_refmv_count; ++idx) {
        if (ref_mv_weight[idx] > max_weight) {
          max_weight = ref_mv_weight[idx];
          max_weight_idx = idx;
        }
      }

      if (max_weight_idx != 0) {
        const CANDIDATE_MV tmp_mv = ref_mv_stack[0];
        const uint16_t tmp_ref_mv_weight = ref_mv_weight[0];
        ref_mv_stack[0] = ref_mv_stack[max_weight_idx];
        ref_mv_stack[max_weight_idx] = tmp_mv;
        ref_mv_weight[0] = ref_mv_weight[max_weight_idx];
        ref_mv_weight[max_weight_idx] = tmp_ref_mv_weight;
      }
    }
  }

  const int is_compound = is_inter_ref_frame(rf[1]);
  if (is_compound) {
    add_derived_smvp_candidates(cm, xd, rf, refmv_count, ref_mv_stack,
                                ref_mv_weight, derived_mv_stack,
                                derived_mv_count, &drl_pr_count);
    if (cm->seq_params.enable_refmvbank)
      add_ref_mv_bank_candidates(cm, xd, rf, ref_frame, refmv_count,
                                 ref_mv_stack, ref_mv_weight, &drl_pr_count);
  } else {
    if (cm->seq_params.enable_refmvbank)
      add_ref_mv_bank_candidates(cm, xd, rf, ref_frame, refmv_count,
                                 ref_mv_stack, ref_mv_weight, &drl_pr_count);
    add_derived_smvp_candidates(cm, xd, rf, refmv_count, ref_mv_stack,
                                ref_mv_weight, derived_mv_stack,
                                derived_mv_count, &drl_pr_count);
  }

  for (int idx = 0; idx < *refmv_count; ++idx) {
    clamp_mv_ref(&ref_mv_stack[idx].this_mv.as_mv, xd->width << MI_SIZE_LOG2,
                 xd->height << MI_SIZE_LOG2, xd);
    if (rf[1] > NONE_FRAME) {
      clamp_mv_ref(&ref_mv_stack[idx].comp_mv.as_mv, xd->width << MI_SIZE_LOG2,
                   xd->height << MI_SIZE_LOG2, xd);
    }
  }

  if (rf[1] == NONE_FRAME && mv_ref_list != NULL) {
    for (int idx = *refmv_count; idx < MAX_MV_REF_CANDIDATES; ++idx) {
      mv_ref_list[idx].as_int = gm_mv_candidates[0].as_int;
    }

    for (int idx = 0; idx < AVMMIN(MAX_MV_REF_CANDIDATES, *refmv_count);
         ++idx) {
      mv_ref_list[idx].as_int = ref_mv_stack[idx].this_mv.as_int;
    }
  }

  // If there is extra space in the stack, copy the GLOBALMV vector into it.
  // This also guarantees the existence of at least one vector to search.
  if (*refmv_count < MAX_REF_MV_STACK_SIZE &&
      !xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART]) {
    if (drl_pr_count < MAX_PR_NUM) {
      int idx = 0;
      for (idx = 0; idx < *refmv_count; ++idx) {
        ++(drl_pr_count);
        if ((ref_mv_stack[idx].this_mv.as_int == gm_mv_candidates[0].as_int) &&
            (rf[1] == NONE_FRAME ||
             (rf[1] > NONE_FRAME && ref_mv_stack[idx].comp_mv.as_int ==
                                        gm_mv_candidates[1].as_int))) {
          break;
        }
      }

      // Add a new item to the list.
      if (idx == *refmv_count) {
        ref_mv_stack[idx].this_mv.as_int = gm_mv_candidates[0].as_int;
        ref_mv_stack[idx].comp_mv.as_int = gm_mv_candidates[1].as_int;
        ref_mv_stack[idx].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[idx].cwp_idx = CWP_EQUAL;
        ref_mv_weight[idx] = REF_CAT_LEVEL;
        ++(*refmv_count);
      }
    } else {
      if (*refmv_count < MAX_REF_MV_STACK_SIZE) {
        ref_mv_stack[*refmv_count].this_mv.as_int = gm_mv_candidates[0].as_int;
        ref_mv_stack[*refmv_count].comp_mv.as_int = gm_mv_candidates[1].as_int;
        ref_mv_stack[*refmv_count].row_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].col_offset = OFFSET_NONSPATIAL;
        ref_mv_stack[*refmv_count].cwp_idx = CWP_EQUAL;
        ref_mv_weight[*refmv_count] = REF_CAT_LEVEL;
        ++(*refmv_count);
      }
    }
    uint8_t added_ext_cnt = 0;
    int original_mv_count = *refmv_count;
    uint8_t max_ext_stack_size = 0;
    if (xd->width <= 8 || xd->height <= 8)
      max_ext_stack_size = 0;
    else
      max_ext_stack_size = 6;

    if (max_ext_stack_size) {
      MV_COMP_DATA_TYPE row = 0;
      MV_COMP_DATA_TYPE col = 0;
      MV_COMP_DATA_TYPE comp_mv_row = 0;
      MV_COMP_DATA_TYPE comp_mv_col = 0;
      uint16_t curr_mv_weight = 1;
      if (*refmv_count < MAX_REF_MV_STACK_SIZE && original_mv_count > 1 &&
          added_ext_cnt < max_ext_stack_size) {
        // Add (0_y, 1_x)
        row = ref_mv_stack[0].this_mv.as_mv.row;
        col = ref_mv_stack[1].this_mv.as_mv.col;
        comp_mv_row = ref_mv_stack[0].comp_mv.as_mv.row;
        comp_mv_col = ref_mv_stack[1].comp_mv.as_mv.col;
        curr_mv_weight = (ref_mv_weight[0] + ref_mv_weight[1] + 1) / 2;
        added_ext_cnt += insert_mvp_candidate(
            rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col, comp_mv_row,
            curr_mv_weight, refmv_count, &drl_pr_count);
        if (*refmv_count < MAX_REF_MV_STACK_SIZE &&
            added_ext_cnt < max_ext_stack_size) {
          // Add (1_y, 0_x)
          row = ref_mv_stack[1].this_mv.as_mv.row;
          col = ref_mv_stack[0].this_mv.as_mv.col;
          comp_mv_row = ref_mv_stack[1].comp_mv.as_mv.row;
          comp_mv_col = ref_mv_stack[0].comp_mv.as_mv.col;
          curr_mv_weight = (ref_mv_weight[0] + ref_mv_weight[1] + 1) / 2;
          added_ext_cnt += insert_mvp_candidate(
              rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col,
              comp_mv_row, curr_mv_weight, refmv_count, &drl_pr_count);
        }
      }
      if (*refmv_count < MAX_REF_MV_STACK_SIZE && original_mv_count > 2 &&
          added_ext_cnt < max_ext_stack_size) {
        // Add (0_y, 2_x)
        row = ref_mv_stack[0].this_mv.as_mv.row;
        col = ref_mv_stack[2].this_mv.as_mv.col;
        comp_mv_row = ref_mv_stack[0].comp_mv.as_mv.row;
        comp_mv_col = ref_mv_stack[2].comp_mv.as_mv.col;
        curr_mv_weight = (ref_mv_weight[0] + ref_mv_weight[2] + 1) / 2;
        added_ext_cnt += insert_mvp_candidate(
            rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col, comp_mv_row,
            curr_mv_weight, refmv_count, &drl_pr_count);

        if (*refmv_count < MAX_REF_MV_STACK_SIZE &&
            added_ext_cnt < max_ext_stack_size) {
          // Add(2_y,0_x)
          row = ref_mv_stack[2].this_mv.as_mv.row;
          col = ref_mv_stack[0].this_mv.as_mv.col;
          comp_mv_row = ref_mv_stack[2].comp_mv.as_mv.row;
          comp_mv_col = ref_mv_stack[0].comp_mv.as_mv.col;
          curr_mv_weight = (ref_mv_weight[0] + ref_mv_weight[2] + 1) / 2;
          added_ext_cnt += insert_mvp_candidate(
              rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col,
              comp_mv_row, curr_mv_weight, refmv_count, &drl_pr_count);
        }

        if (*refmv_count < MAX_REF_MV_STACK_SIZE &&
            added_ext_cnt < max_ext_stack_size) {
          // Add (1_y, 2_x)
          row = ref_mv_stack[1].this_mv.as_mv.row;
          col = ref_mv_stack[2].this_mv.as_mv.col;
          comp_mv_row = ref_mv_stack[1].comp_mv.as_mv.row;
          comp_mv_col = ref_mv_stack[2].comp_mv.as_mv.col;
          curr_mv_weight = (ref_mv_weight[1] + ref_mv_weight[2] + 1) / 2;
          added_ext_cnt += insert_mvp_candidate(
              rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col,
              comp_mv_row, curr_mv_weight, refmv_count, &drl_pr_count);
        }
        // Add (2_y, 1_x)
        if (*refmv_count < MAX_REF_MV_STACK_SIZE &&
            added_ext_cnt < max_ext_stack_size) {
          row = ref_mv_stack[2].this_mv.as_mv.row;
          col = ref_mv_stack[1].this_mv.as_mv.col;
          comp_mv_row = ref_mv_stack[2].comp_mv.as_mv.row;
          comp_mv_col = ref_mv_stack[1].comp_mv.as_mv.col;
          curr_mv_weight = (ref_mv_weight[1] + ref_mv_weight[2] + 1) / 2;
          added_ext_cnt += insert_mvp_candidate(
              rf, ref_mv_stack, ref_mv_weight, col, row, comp_mv_col,
              comp_mv_row, curr_mv_weight, refmv_count, &drl_pr_count);
          (void)added_ext_cnt;
        }
      }
    }
  }

  if (warp_param_stack && valid_num_warp_candidates &&
      *valid_num_warp_candidates < max_num_of_warp_candidates) {
    // Insert warp parameters from the bank
#if WARP_CU_BANK
    const WARP_PARAM_BANK *warp_param_bank = &xd->warp_param_bank;
#else
    const WARP_PARAM_BANK *warp_param_bank = xd->warp_param_bank_pt;
#endif  // WARP_CU_BANK
    const WarpedMotionParams *queue = warp_param_bank->wpb_buffer[ref_frame];
    const int count = warp_param_bank->wpb_count[ref_frame];
    const int start_idx = warp_param_bank->wpb_start_idx[ref_frame];

    for (int idx_bank = 0; idx_bank < count && *valid_num_warp_candidates <
                                                   max_num_of_warp_candidates;
         ++idx_bank) {
      const int idx = (start_idx + count - 1 - idx_bank) % WARP_PARAM_BANK_SIZE;
      const WarpedMotionParams cand_warp_param = queue[idx];

      if (!cand_warp_param.invalid) {
        insert_neighbor_warp_candidate(warp_param_stack, &cand_warp_param,
                                       *valid_num_warp_candidates,
                                       PROJ_PARAM_BANK);
        (*valid_num_warp_candidates)++;
      }
    }

    // Insert Global motion of the current
    if (*valid_num_warp_candidates < max_num_of_warp_candidates) {
      if (!xd->global_motion[ref_frame].invalid) {
        insert_neighbor_warp_candidate(
            warp_param_stack, &xd->global_motion[ref_frame],
            *valid_num_warp_candidates, PROJ_GLOBAL_MOTION);
        (*valid_num_warp_candidates)++;
      }
    }

    // Filled with default values( currently all params are zeros)
    int max_num_of_default_allowed = AVMMIN(2, max_num_of_warp_candidates);
    int current_number_of_defaults = 0;
    int tmp_curr_num = *valid_num_warp_candidates;
    for (int cand_num = tmp_curr_num;
         (cand_num < max_num_of_warp_candidates) &&
         (current_number_of_defaults < max_num_of_default_allowed);
         cand_num++) {
      warp_param_stack[cand_num].wm_params = default_warp_params;
      warp_param_stack[cand_num].proj_type = PROJ_DEFAULT;
      (*valid_num_warp_candidates)++;
      current_number_of_defaults++;
    }
  }

  // If there are open slots in reference BV candidate list
  // fetch reference BVs from the default BVPs
  if (xd->mi[0]->use_intrabc[xd->tree_type == CHROMA_PART]) {
    const int w = xd->width * MI_SIZE;
    const int h = xd->height * MI_SIZE;
    const int sb_width = block_size_wide[cm->sb_size];
    const int sb_height = block_size_high[cm->sb_size];
    const int default_ref_bv_list[MAX_REF_BV_STACK_SIZE][2] = {
      { 0, -sb_height },
      { -sb_width - INTRABC_DELAY_PIXELS, 0 },
      { 0, -h },
      { -w, 0 },
    };
    const int max_bvp_size = cm->features.max_bvp_drl_bits + 1;
    for (int i = 0; i < max_bvp_size; ++i) {
      if (*refmv_count >= max_bvp_size) break;
      CANDIDATE_MV tmp_mv;
      tmp_mv.this_mv.as_mv.col =
          (MV_COMP_DATA_TYPE)GET_MV_SUBPEL(default_ref_bv_list[i][0]);
      tmp_mv.this_mv.as_mv.row =
          (MV_COMP_DATA_TYPE)GET_MV_SUBPEL(default_ref_bv_list[i][1]);
      tmp_mv.comp_mv.as_int = 0;
      add_to_ref_bv_list(tmp_mv, ref_mv_stack, ref_mv_weight, refmv_count);
    }
  }
}

void get_skip_mode_ref_offsets(const AV2_COMMON *cm, int ref_order_hint[2]) {
  const SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;
  ref_order_hint[0] = ref_order_hint[1] = 0;
  if (!skip_mode_info->skip_mode_allowed) return;

  const RefCntBuffer *const buf_0 =
      get_ref_frame_buf(cm, skip_mode_info->ref_frame_idx_0);
  const RefCntBuffer *const buf_1 =
      get_ref_frame_buf(cm, skip_mode_info->ref_frame_idx_1);
  assert(buf_0 != NULL && buf_1 != NULL);

  ref_order_hint[0] = buf_0->display_order_hint;
  ref_order_hint[1] = buf_1->display_order_hint;
}

// Initialize the warp parameter list
void av2_initialize_warp_wrl_list(
    WARP_CANDIDATE warp_param_stack[][MAX_WARP_REF_CANDIDATES],
    uint8_t valid_num_warp_candidates[INTER_REFS_PER_FRAME]) {
  for (int ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ref_frame++) {
    for (int warp_idx = 0; warp_idx < MAX_WARP_REF_CANDIDATES; warp_idx++) {
      warp_param_stack[ref_frame][warp_idx].wm_params = default_warp_params;
      warp_param_stack[ref_frame][warp_idx].wm_params.invalid = 1;
    }
    valid_num_warp_candidates[ref_frame] = 0;
  }
}

void av2_find_mode_ctx(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                       int16_t *mode_context, MV_REFERENCE_FRAME ref_frame) {
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  MV_REFERENCE_FRAME rf[2];

  av2_set_ref_frame(rf, ref_frame);
  mode_context[ref_frame] = 0;

  uint8_t col_match_count = 0;
  uint8_t row_match_count = 0;
  uint8_t newmv_count = 0;

  if (xd->left_available) {
    scan_blk_mbmi_ctx(cm, xd, mi_row, mi_col, rf, (xd->height - 1), -1,
                      &col_match_count, &newmv_count);
  }
  if (xd->up_available) {
    scan_blk_mbmi_ctx(cm, xd, mi_row, mi_col, rf, -1, (xd->width - 1),
                      &row_match_count, &newmv_count);
  }
  if (xd->left_available) {
    scan_blk_mbmi_ctx(cm, xd, mi_row, mi_col, rf, 0, -1, &col_match_count,
                      &newmv_count);
  }
  if (xd->up_available) {
    scan_blk_mbmi_ctx(cm, xd, mi_row, mi_col, rf, -1, 0, &row_match_count,
                      &newmv_count);
  }

  const uint8_t nearest_match = (row_match_count > 0) + (col_match_count > 0);

  mode_context[ref_frame] = nearest_match + (newmv_count > 0) * 2;
}

// Initialize ref_mv_stack with zero MVs.
void av2_initialize_ref_mv_stack(
    CANDIDATE_MV ref_mv_stack[MAX_REF_MV_STACK_SIZE], int max_cand_num) {
  for (int i = 0; i < max_cand_num; ++i) {
    ref_mv_stack[i].this_mv.as_int = 0;
    ref_mv_stack[i].comp_mv.as_int = 0;
    ref_mv_stack[i].row_offset = OFFSET_NONSPATIAL;
    ref_mv_stack[i].col_offset = OFFSET_NONSPATIAL;
    ref_mv_stack[i].cwp_idx = CWP_EQUAL;
  }
}

void av2_find_mv_refs(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, MB_MODE_INFO *mi,
    MV_REFERENCE_FRAME ref_frame, uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
    CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
    uint16_t ref_mv_weight[][MAX_REF_MV_STACK_SIZE],
    int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES], int_mv *global_mvs,
    WARP_CANDIDATE warp_param_stack[][MAX_WARP_REF_CANDIDATES],
    int max_num_of_warp_candidates,
    uint8_t valid_num_warp_candidates[INTER_REFS_PER_FRAME]) {
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  int_mv gm_mv[2];

  if (ref_frame == INTRA_FRAME || is_tip_ref_frame(ref_frame)) {
    gm_mv[0].as_int = gm_mv[1].as_int = 0;
  } else {
    const BLOCK_SIZE bsize = mi->sb_type[PLANE_TYPE_Y];
    const int fr_mv_precision = cm->features.fr_mv_precision;
    if (ref_frame < INTER_REFS_PER_FRAME) {
      gm_mv[0] = get_warp_motion_vector(xd, &cm->global_motion[ref_frame],
                                        fr_mv_precision, bsize, mi_col, mi_row);
      gm_mv[1].as_int = 0;
      if (global_mvs != NULL) global_mvs[ref_frame] = gm_mv[0];
    } else {
      MV_REFERENCE_FRAME rf[2];
      av2_set_ref_frame(rf, ref_frame);
      gm_mv[0] = get_warp_motion_vector(xd, &cm->global_motion[rf[0]],
                                        fr_mv_precision, bsize, mi_col, mi_row);
      gm_mv[1] = get_warp_motion_vector(xd, &cm->global_motion[rf[1]],
                                        fr_mv_precision, bsize, mi_col, mi_row);
    }
  }

  bool derive_wrl = (warp_param_stack && valid_num_warp_candidates &&
                     max_num_of_warp_candidates);
  derive_wrl &= (ref_frame < INTER_REFS_PER_FRAME);
  if (has_second_drl(mi)) derive_wrl = 0;

  derive_wrl &= is_motion_variation_allowed_bsize(mi->sb_type[PLANE_TYPE_Y],
                                                  mi_row, mi_col);
  if (derive_wrl && valid_num_warp_candidates) {
    valid_num_warp_candidates[ref_frame] =
        0;  // initialize the number of valid candidates to 0 at the beginning
  }

  if (mi->skip_mode) {
    SKIP_MODE_MVP_LIST *skip_list =
        (SKIP_MODE_MVP_LIST *)&(xd->skip_mvp_candidate_list);
    av2_initialize_ref_mv_stack(skip_list->ref_mv_stack,
                                USABLE_REF_MV_STACK_SIZE);
    setup_ref_mv_list(cm, xd, ref_frame, &(skip_list->ref_mv_count),
                      skip_list->ref_mv_stack, skip_list->weight,
                      mv_ref_list ? mv_ref_list[ref_frame] : NULL, gm_mv,
                      mi_row, mi_col, NULL, 0, NULL);
  } else {
    MV_REFERENCE_FRAME rf[2];
    av2_set_ref_frame(rf, ref_frame);
    if (!has_second_drl(mi))
      rf[0] = ref_frame;
    else {
      const BLOCK_SIZE bsize = mi->sb_type[PLANE_TYPE_Y];
      const int fr_mv_precision = cm->features.fr_mv_precision;
      gm_mv[0] = get_warp_motion_vector(xd, &cm->global_motion[rf[0]],
                                        fr_mv_precision, bsize, mi_col, mi_row);
      gm_mv[1].as_int = 0;
    }
    if (ref_frame == INTRA_FRAME) {
      av2_initialize_ref_mv_stack(ref_mv_stack[rf[0]],
                                  cm->features.max_bvp_drl_bits + 1);
    } else {
      av2_initialize_ref_mv_stack(ref_mv_stack[rf[0]], MAX_REF_MV_STACK_SIZE);
    }
    setup_ref_mv_list(cm, xd, rf[0], &ref_mv_count[rf[0]], ref_mv_stack[rf[0]],
                      ref_mv_weight[rf[0]],
                      mv_ref_list ? mv_ref_list[rf[0]] : NULL, gm_mv, mi_row,
                      mi_col, derive_wrl ? warp_param_stack[rf[0]] : NULL,
                      derive_wrl ? max_num_of_warp_candidates : 0,
                      derive_wrl ? &valid_num_warp_candidates[rf[0]] : NULL);

    if (has_second_drl(mi)) {
      assert(rf[0] == mi->ref_frame[0]);
      assert(rf[1] == mi->ref_frame[1]);
      const BLOCK_SIZE bsize = mi->sb_type[PLANE_TYPE_Y];
      const int fr_mv_precision = cm->features.fr_mv_precision;
      gm_mv[0] = get_warp_motion_vector(xd, &cm->global_motion[rf[1]],
                                        fr_mv_precision, bsize, mi_col, mi_row);
      gm_mv[1].as_int = 0;

      av2_initialize_ref_mv_stack(ref_mv_stack[rf[1]], MAX_REF_MV_STACK_SIZE);
      setup_ref_mv_list(cm, xd, rf[1], &ref_mv_count[rf[1]],
                        ref_mv_stack[rf[1]], ref_mv_weight[rf[1]],
                        mv_ref_list ? mv_ref_list[rf[1]] : NULL, gm_mv, mi_row,
                        mi_col, derive_wrl ? warp_param_stack[rf[1]] : NULL,
                        derive_wrl ? max_num_of_warp_candidates : 0,
                        derive_wrl ? &valid_num_warp_candidates[rf[1]] : NULL);
    }
    if (derive_wrl) assert(rf[0] == ref_frame);
  }
}

void av2_find_best_ref_mvs(int_mv *mvlist, int_mv *nearest_mv, int_mv *near_mv,
                           MvSubpelPrecision precision) {
  int i;
  // Make sure all the candidates are properly clamped etc
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
    lower_mv_precision(&mvlist[i].as_mv, precision);
  }
  *nearest_mv = mvlist[0];
  *near_mv = mvlist[1];
}

void av2_setup_frame_buf_refs(AV2_COMMON *cm) {
  cm->cur_frame->order_hint = cm->current_frame.order_hint;
  cm->cur_frame->display_order_hint = cm->current_frame.display_order_hint;
  cm->cur_frame->display_order_hint_restricted =
      cm->current_frame.display_order_hint_restricted;
  cm->cur_frame->long_term_id = cm->current_frame.long_term_id;
  cm->cur_frame->absolute_poc = cm->current_frame.absolute_poc;
  cm->cur_frame->pyramid_level = cm->current_frame.pyramid_level;
  cm->cur_frame->tlayer_id = cm->current_frame.tlayer_id;
  cm->cur_frame->mlayer_id = cm->current_frame.mlayer_id;
  cm->cur_frame->xlayer_id = cm->current_frame.xlayer_id;

  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (buf != NULL && ref_frame < cm->ref_frames_info.num_total_refs &&
        !buf->is_restricted) {
      cm->cur_frame->ref_order_hints[ref_frame] = buf->order_hint;
      cm->cur_frame->ref_display_order_hint[ref_frame] =
          buf->display_order_hint;
      cm->cur_frame->ref_mlayer_ids[ref_frame] = buf->mlayer_id;
      cm->cur_frame->refs_restricted_status[ref_frame] = buf->is_restricted;
    } else {
      cm->cur_frame->ref_order_hints[ref_frame] = -1;
      cm->cur_frame->ref_display_order_hint[ref_frame] = -1;
      cm->cur_frame->ref_mlayer_ids[ref_frame] = -1;
      cm->cur_frame->refs_restricted_status[ref_frame] = 1;
    }
  }
}

void av2_setup_frame_sign_bias(AV2_COMMON *cm) {
  memset(&cm->ref_frame_sign_bias, 0, sizeof(cm->ref_frame_sign_bias));
  for (int ref_frame = 0; ref_frame < cm->ref_frames_info.num_future_refs;
       ++ref_frame) {
    const int index = cm->ref_frames_info.future_refs[ref_frame];
    cm->ref_frame_sign_bias[index] = 1;
  }
}

// Get the temporal distance of start_frame to its closest ref frame
// that has interpolation property relative to current frame. Interpolation
// means start_frame and its ref frame are on two sides of current frame
static INLINE int get_dist_to_closest_interp_ref(const AV2_COMMON *const cm,
                                                 MV_REFERENCE_FRAME start_frame,
                                                 const int find_forward_ref) {
  if (start_frame == -1) return INT_MAX;
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);

  if (!is_ref_motion_field_eligible(cm, start_frame_buf)) return INT_MAX;

  const int start_frame_order_hint = start_frame_buf->display_order_hint;
  const int cur_order_hint = cm->cur_frame->display_order_hint;
  int abs_closest_ref_offset = INT_MAX;
  const int *const ref_order_hints =
      &start_frame_buf->ref_display_order_hint[0];
  for (MV_REFERENCE_FRAME ref = 0; ref < INTER_REFS_PER_FRAME; ++ref) {
    if (ref_order_hints[ref] != -1) {
      const int start_to_ref_offset = get_relative_dist(
          order_hint_info, start_frame_order_hint, ref_order_hints[ref]);
      const int cur_to_ref_offset = get_relative_dist(
          order_hint_info, cur_order_hint, ref_order_hints[ref]);
      const int abs_start_to_ref_offset = abs(start_to_ref_offset);
      const int is_two_sides =
          (start_to_ref_offset > 0 && cur_to_ref_offset > 0 &&
           find_forward_ref == 1) ||
          (start_to_ref_offset < 0 && cur_to_ref_offset < 0 &&
           find_forward_ref == 0);
      if (is_two_sides && abs_start_to_ref_offset < abs_closest_ref_offset) {
        abs_closest_ref_offset = abs_start_to_ref_offset;
      }
    }
  }

  return abs_closest_ref_offset;
}

// Check if a reference frame is an overlay frame (i.e., has the same
// order_hint as the current frame).
static INLINE int is_ref_overlay(const AV2_COMMON *const cm, int ref) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;
  if (order_hint_info->order_hint_bits_minus_1 < 0) return 0;
  const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref);
  if (buf == NULL) return -1;
  const int ref_order_hint = buf->display_order_hint;
  for (int r = 0; r < INTER_REFS_PER_FRAME; ++r) {
    if (buf->refs_restricted_status[r] == 1) continue;
    if (buf->ref_display_order_hint[r] == -1) continue;
    const int ref_ref_order_hint = buf->ref_display_order_hint[r];
    if (get_relative_dist(order_hint_info, ref_order_hint,
                          ref_ref_order_hint) == 0)
      return 1;
  }
  return 0;
}

// Performs a topological sort on the reference frames so they are sorted by
// their dependency.
static void recur_topo_sort_refs(const AV2_COMMON *cm, const bool *is_overlay,
                                 int *rf_stack, int *visited, int *stack_count,
                                 int rf) {
  visited[rf] = 1;
  const RefCntBuffer *const buf = get_ref_frame_buf(cm, rf);
  if (buf->frame_type == INTER_FRAME) {
    for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
      if (buf->refs_restricted_status[i] == 1) continue;
      const int target_ref_hint = buf->ref_display_order_hint[i];
      if (target_ref_hint < 0) continue;
      int found_rf = -1;
      for (int j = 0; j < cm->ref_frames_info.num_total_refs; j++) {
        const int ref_hint = get_ref_frame_buf(cm, j)->display_order_hint;
        if (get_relative_dist(&cm->seq_params.order_hint_info, ref_hint,
                              target_ref_hint) == 0) {
          if (is_overlay[j]) continue;
          found_rf = j;
          break;
        }
      }
      if (found_rf == -1) continue;
      if (visited[found_rf] == 0) {
        recur_topo_sort_refs(cm, is_overlay, rf_stack, visited, stack_count,
                             found_rf);
      }
    }
  }
  rf_stack[*stack_count] = rf;
  *stack_count = *stack_count + 1;
}

// Whether a reference frame buffer had a reference frame in the future.
static int has_future_ref(const AV2_COMMON *cm, int rf) {
  if (rf < 0) return 0;
  RefCntBuffer *buf = get_ref_frame_buf(cm, rf);
  const int cur_hint = buf->display_order_hint;
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (buf->refs_restricted_status[i]) continue;
    const int ref_hint = buf->ref_display_order_hint[i];
    if (ref_hint >= 0 && get_relative_dist(&cm->seq_params.order_hint_info,
                                           ref_hint, cur_hint) > 0) {
      return 1;
    }
  }
  return 0;
}

// Whether a reference frame buffer had a reference frame in the past.
static int has_past_ref(const AV2_COMMON *cm, int rf) {
  if (rf < 0) return 0;
  RefCntBuffer *buf = get_ref_frame_buf(cm, rf);
  const int cur_hint = buf->display_order_hint;
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (buf->refs_restricted_status[i]) continue;
    const int ref_hint = buf->ref_display_order_hint[i];
    if (ref_hint >= 0 && get_relative_dist(&cm->seq_params.order_hint_info,
                                           ref_hint, cur_hint) < 0) {
      return 1;
    }
  }
  return 0;
}

// Struct to store the reference frame motion field candidates.
struct ProcessRefTMVP {
  int type;  // 0 means with side, 1 means with target frame.
  int side;  // 0 means right to left, 1 means left to right.
  int start_frame;
  int target_frame;
};

// Add a reference frame motion field candidate, if we have not reached the
// maximum allowed.
static void check_and_add_process_ref(const AV2_COMMON *cm, int max_check,
                                      int type, int start_frame,
                                      int target_frame, int side,
                                      int checked_ref[INTER_REFS_PER_FRAME][2],
                                      int *checked_count,
                                      struct ProcessRefTMVP *process_ref,
                                      int *process_count) {
  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  if (!is_ref_motion_field_eligible(cm, start_frame_buf)) return;

  const int start_frame_order_hint = start_frame_buf->display_order_hint;
  const int cur_order_hint = cm->cur_frame->display_order_hint;
  int start_to_current_frame_offset = get_relative_dist(
      &cm->seq_params.order_hint_info, start_frame_order_hint, cur_order_hint);
  if (abs(start_to_current_frame_offset) > MAX_FRAME_DISTANCE) return;

  if (cm->bru.enabled && cm->bru.update_ref_idx != -1) {
    if (start_frame == cm->bru.update_ref_idx) return;
    if (target_frame == cm->bru.update_ref_idx) return;
  }

  if (type == 1 && start_frame == target_frame) return;

  if (*process_count >= MFMV_STACK_SIZE) return;
  if (checked_ref[start_frame][side]) return;

  if (!checked_ref[start_frame][side] && *checked_count < max_check) {
    checked_ref[start_frame][side] = 1;
    (*checked_count)++;
  }
  if (checked_ref[start_frame][side]) {
    process_ref[*process_count].type = type;
    process_ref[*process_count].start_frame = start_frame;
    process_ref[*process_count].side = side;
    process_ref[*process_count].target_frame = target_frame;
    (*process_count)++;
  }
}

static INLINE int get_blk_id_k(int this_col, int tmvp_proc_sizel2) {
  return (this_col >> tmvp_proc_sizel2) % 3;
}

// Check if the mv intersects with exisiting trajectories, and if yes, update
// the trajectories.
static INLINE void check_traj_intersect(AV2_COMMON *cm,
                                        MV_REFERENCE_FRAME start_frame,
                                        MV_REFERENCE_FRAME end_frame,
                                        const MV *mv, int start_row,
                                        int start_col, int mvs_cols) {
  (void)mvs_cols;
  const int clamp_max = REFMVS_LIMIT;
  const int clamp_min = -REFMVS_LIMIT;
  assert(start_row % cm->tmvp_sample_step == 0);
  assert(start_col % cm->tmvp_sample_step == 0);
  // Check starting point
  for (int k = 0; k < 3; k++) {
    int_mv ***blk_id_map_rows = cm->blk_id_map_rows[k];
    if (blk_id_map_rows[start_frame][start_row][start_col].as_int !=
        INVALID_MV) {
      if (end_frame != NONE_FRAME) {
        int traj_row =
            blk_id_map_rows[start_frame][start_row][start_col].as_mv.row;
        int traj_col =
            blk_id_map_rows[start_frame][start_row][start_col].as_mv.col;
        assert(traj_row % cm->tmvp_sample_step == 0);
        assert(traj_col % cm->tmvp_sample_step == 0);
        if (get_blk_id_k(traj_col, cm->tmvp_proc_sizel2) != k) continue;
        if (check_block_position(cm, start_row, start_col, traj_row,
                                 traj_col) &&
            cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_int ==
                INVALID_MV) {
          // Update trajectory mv
          cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_mv.row =
              clamp(cm->id_offset_map_rows[start_frame][traj_row][traj_col]
                            .as_mv.row +
                        mv->row,
                    clamp_min, clamp_max);
          cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_mv.col =
              clamp(cm->id_offset_map_rows[start_frame][traj_row][traj_col]
                            .as_mv.col +
                        mv->col,
                    clamp_min, clamp_max);
          // Update reverse mapping
          int end_row = 0, end_col = 0;
          int pos_valid;
          if (cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_int ==
              0) {
            pos_valid = 1;
            end_row = traj_row;
            end_col = traj_col;
          } else {
            pos_valid = get_block_position(
                cm, &end_row, &end_col, traj_row, traj_col,
                cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_mv, 0);
          }
          end_row = (end_row >> cm->tmvp_sample_stepl2)
                    << cm->tmvp_sample_stepl2;
          end_col = (end_col >> cm->tmvp_sample_stepl2)
                    << cm->tmvp_sample_stepl2;

          if (pos_valid) {
            blk_id_map_rows[end_frame][end_row][end_col].as_mv.row = traj_row;
            blk_id_map_rows[end_frame][end_row][end_col].as_mv.col = traj_col;
          }
        }
      }
    }
  }

  // Check ending point
  if (end_frame != NONE_FRAME) {
    int end_row = 0, end_col = 0;
    int pos_valid;
    if (mv->col == 0 && mv->row == 0) {
      pos_valid = 1;
      end_row = start_row;
      end_col = start_col;
    } else {
      pos_valid = get_block_position_no_constraint(
          cm, &end_row, &end_col, start_row, start_col, *mv, 0);
    }
    end_row = (end_row >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;
    end_col = (end_col >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;

    for (int k = 0; k < 3; k++) {
      int_mv ***blk_id_map_rows = cm->blk_id_map_rows[k];
      if (pos_valid &&
          blk_id_map_rows[end_frame][end_row][end_col].as_int != INVALID_MV) {
        int traj_row = blk_id_map_rows[end_frame][end_row][end_col].as_mv.row;
        int traj_col = blk_id_map_rows[end_frame][end_row][end_col].as_mv.col;
        assert(traj_row % cm->tmvp_sample_step == 0);
        assert(traj_col % cm->tmvp_sample_step == 0);
        if (get_blk_id_k(traj_col, cm->tmvp_proc_sizel2) != k) continue;

        if (check_block_position(cm, start_row, start_col, traj_row,
                                 traj_col) &&
            check_block_position(cm, end_row, end_col, traj_row, traj_col) &&
            cm->id_offset_map_rows[start_frame][traj_row][traj_col].as_int ==
                INVALID_MV) {
          // Update trajectory mv
          cm->id_offset_map_rows[start_frame][traj_row][traj_col]
              .as_mv.row = clamp(
              cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_mv.row -
                  mv->row,
              clamp_min, clamp_max);
          cm->id_offset_map_rows[start_frame][traj_row][traj_col]
              .as_mv.col = clamp(
              cm->id_offset_map_rows[end_frame][traj_row][traj_col].as_mv.col -
                  mv->col,
              clamp_min, clamp_max);
          // Update reverse mapping
          int new_start_row = 0, new_start_col = 0;
          int new_pos_valid;
          if (cm->id_offset_map_rows[start_frame][traj_row][traj_col].as_int ==
              0) {
            new_pos_valid = 1;
            new_start_row = traj_row;
            new_start_col = traj_col;
          } else {
            new_pos_valid = get_block_position(
                cm, &new_start_row, &new_start_col, traj_row, traj_col,
                cm->id_offset_map_rows[start_frame][traj_row][traj_col].as_mv,
                0);
          }

          new_start_row = (new_start_row >> cm->tmvp_sample_stepl2)
                          << cm->tmvp_sample_stepl2;
          new_start_col = (new_start_col >> cm->tmvp_sample_stepl2)
                          << cm->tmvp_sample_stepl2;

          if (new_pos_valid) {
            blk_id_map_rows[start_frame][new_start_row][new_start_col]
                .as_mv.row = traj_row;
            blk_id_map_rows[start_frame][new_start_row][new_start_col]
                .as_mv.col = traj_col;
          }
        }
      }
    }
  }
}

// Calculate the projected motion field from the TMVP mvs that points from
// start_frame to target_frame.
static int motion_field_projection_start_target(
    AV2_COMMON *cm, MV_REFERENCE_FRAME start_frame,
    MV_REFERENCE_FRAME target_frame) {
  const int cur_order_hint = cm->cur_frame->display_order_hint;
  int start_order_hint = get_ref_frame_buf(cm, start_frame)->display_order_hint;
  int target_order_hint =
      get_ref_frame_buf(cm, target_frame)->display_order_hint;

  OrderHintInfo *order_hint_info = &cm->seq_params.order_hint_info;

  int ref_frame_offset =
      get_relative_dist(order_hint_info, start_order_hint, target_order_hint);

  if (abs(ref_frame_offset) > MAX_FRAME_DISTANCE) {
    return 0;
  }

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  if (!is_ref_motion_field_eligible(cm, start_frame_buf)) return 0;

  assert(start_frame_buf->width == cm->width &&
         start_frame_buf->height == cm->height);

  const int *const ref_order_hints = start_frame_buf->ref_display_order_hint;

  int start_to_current_frame_offset =
      get_relative_dist(order_hint_info, start_order_hint, cur_order_hint);

  if (abs(start_to_current_frame_offset) > MAX_FRAME_DISTANCE) {
    return 0;
  }

  const int is_backward = ref_frame_offset < 0;
  int mv_idx = is_backward ? 1 : 0;
  if (is_backward) {
    ref_frame_offset = -ref_frame_offset;
    start_to_current_frame_offset = -start_to_current_frame_offset;
  }

  const int temporal_scale_factor =
      tip_derive_scale_factor(start_to_current_frame_offset, ref_frame_offset);
  const int ref_temporal_scale_factor = tip_derive_scale_factor(
      -ref_frame_offset + start_to_current_frame_offset, -ref_frame_offset);

  const MV_REF *mv_ref_base = start_frame_buf->mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int start_mvs_rows =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_rows, TMVP_SHIFT_BITS);
  const int start_mvs_cols =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_cols, TMVP_SHIFT_BITS);
  (void)mvs_rows;

  assert(cm->tmvp_sample_step > 0);
  for (int blk_row = 0; blk_row < start_mvs_rows;
       blk_row += cm->tmvp_sample_step) {
    for (int blk_col = 0; blk_col < start_mvs_cols;
         blk_col += cm->tmvp_sample_step) {
      const MV_REF *mv_ref = &mv_ref_base[blk_row * start_mvs_cols + blk_col];
      if (is_inter_ref_frame(mv_ref->ref_frame[mv_idx])) {
        const int ref_frame_order_hint =
            ref_order_hints[mv_ref->ref_frame[mv_idx]];
        if (get_relative_dist(order_hint_info, ref_frame_order_hint,
                              target_order_hint) == 0) {
          MV ref_mv = mv_ref->mv[mv_idx].as_mv;
          fetch_mv_from_tmvp(&ref_mv);
          int scaled_blk_col = blk_col;
          int scaled_blk_row = blk_row;

          if (cm->seq_params.enable_mv_traj) {
            check_traj_intersect(cm, start_frame, target_frame, &ref_mv,
                                 scaled_blk_row, scaled_blk_col, mvs_cols);
          }

          int_mv this_mv;
          int mi_r = 0;
          int mi_c = 0;
          tip_get_mv_projection(&this_mv.as_mv, ref_mv, temporal_scale_factor);
          int pos_valid;
          if (this_mv.as_int == 0) {
            pos_valid = 1;
            mi_r = scaled_blk_row;
            mi_c = scaled_blk_col;
          } else {
            pos_valid = get_block_position_no_constraint(
                cm, &mi_r, &mi_c, scaled_blk_row, scaled_blk_col, this_mv.as_mv,
                0);
          }
          mi_r = (mi_r >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;
          mi_c = (mi_c >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;

          if (pos_valid)
            pos_valid = check_block_position(cm, scaled_blk_row, scaled_blk_col,
                                             mi_r, mi_c);

          if (pos_valid) {
            if (cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_int == INVALID_MV) {
              if (cm->seq_params.enable_mv_traj) {
                int blk_id_k = get_blk_id_k(mi_c, cm->tmvp_proc_sizel2);
                int_mv ***blk_id_map_rows = cm->blk_id_map_rows[blk_id_k];
                cm->id_offset_map_rows[start_frame][mi_r][mi_c].as_mv.row =
                    clamp(-this_mv.as_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
                cm->id_offset_map_rows[start_frame][mi_r][mi_c].as_mv.col =
                    clamp(-this_mv.as_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
                blk_id_map_rows[start_frame][scaled_blk_row][scaled_blk_col]
                    .as_mv.row = mi_r;
                blk_id_map_rows[start_frame][scaled_blk_row][scaled_blk_col]
                    .as_mv.col = mi_c;

                MV target_frame_mv;
                tip_get_mv_projection(&target_frame_mv, ref_mv,
                                      ref_temporal_scale_factor);
                cm->id_offset_map_rows[target_frame][mi_r][mi_c].as_mv.row =
                    clamp(target_frame_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
                cm->id_offset_map_rows[target_frame][mi_r][mi_c].as_mv.col =
                    clamp(target_frame_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
                int target_row = 0, target_col = 0;
                int target_pos_valid;
                if (ref_mv.row == 0 && ref_mv.col == 0) {
                  target_pos_valid = 1;
                  target_row = scaled_blk_row;
                  target_col = scaled_blk_col;
                } else {
                  target_pos_valid = get_block_position_no_constraint(
                      cm, &target_row, &target_col, scaled_blk_row,
                      scaled_blk_col, ref_mv, 0);
                }
                target_row = (target_row >> cm->tmvp_sample_stepl2)
                             << cm->tmvp_sample_stepl2;
                target_col = (target_col >> cm->tmvp_sample_stepl2)
                             << cm->tmvp_sample_stepl2;

                if (target_pos_valid)
                  target_pos_valid = check_block_position(
                      cm, target_row, target_col, mi_r, mi_c);

                if (target_pos_valid) {
                  blk_id_map_rows[target_frame][target_row][target_col]
                      .as_mv.row = mi_r;
                  blk_id_map_rows[target_frame][target_row][target_col]
                      .as_mv.col = mi_c;
                }
              }

              if (is_backward) {
                ref_mv.row = -ref_mv.row;
                ref_mv.col = -ref_mv.col;
              }
              cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_mv.row = ref_mv.row;
              cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_mv.col = ref_mv.col;
              cm->tpl_mvs_rows[mi_r][mi_c].ref_frame_offset = ref_frame_offset;
            }
          }
        }
      }
    }
  }
  return 1;
}

// Calculate the projected motion field from the TMVP mvs that points from
// start_frame to one side.
static int motion_field_projection_side(AV2_COMMON *cm,
                                        MV_REFERENCE_FRAME start_frame,
                                        MV_REFERENCE_FRAME target_frame,
                                        int side_idx) {
  int ref_offset[INTER_REFS_PER_FRAME] = { 0 };

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  const int start_frame_order_hint = start_frame_buf->display_order_hint;
  const int cur_order_hint = cm->cur_frame->display_order_hint;
  int start_to_current_frame_offset = get_relative_dist(
      &cm->seq_params.order_hint_info, start_frame_order_hint, cur_order_hint);

  int temporal_scale_factor[REF_FRAMES] = { 0 };
  int ref_temporal_scale_factor[REF_FRAMES] = { 0 };
  int ref_abs_offset[REF_FRAMES] = { 0 };

  assert(start_frame_buf->width == cm->width &&
         start_frame_buf->height == cm->height);

  const int *const ref_order_hints =
      &start_frame_buf->ref_display_order_hint[0];
  for (MV_REFERENCE_FRAME rf = 0; rf < INTER_REFS_PER_FRAME; ++rf) {
    if (start_frame_buf->refs_restricted_status[rf] == 1) continue;
    if (ref_order_hints[rf] != -1) {
      ref_offset[rf] =
          get_relative_dist(&cm->seq_params.order_hint_info,
                            start_frame_order_hint, ref_order_hints[rf]);
      ref_abs_offset[rf] = abs(ref_offset[rf]);
      temporal_scale_factor[rf] = tip_derive_scale_factor(
          start_to_current_frame_offset, ref_offset[rf]);
      int ref_to_current_frame_offset =
          -ref_offset[rf] + start_to_current_frame_offset;
      ref_temporal_scale_factor[rf] =
          tip_derive_scale_factor(ref_to_current_frame_offset, -ref_offset[rf]);
    }
  }

  int start_ref_map[INTER_REFS_PER_FRAME];
  for (int k = 0; k < INTER_REFS_PER_FRAME; k++) {
    start_ref_map[k] = NONE_FRAME;

    const int ref_k_hint = start_frame_buf->ref_display_order_hint[k];
    if (ref_k_hint < 0) continue;
    if (get_relative_dist(&cm->seq_params.order_hint_info,
                          start_frame_order_hint, ref_k_hint) == 0) {
      continue;
    }
    for (int rf = 0; rf < cm->ref_frames_info.num_total_refs; rf++) {
      const int rf_hint = get_ref_frame_buf(cm, rf)->display_order_hint;
      if (rf_hint >= 0 && ref_k_hint >= 0 &&
          get_relative_dist(&cm->seq_params.order_hint_info, rf_hint,
                            ref_k_hint) == 0) {
        start_ref_map[k] = rf;
        break;
      }
    }
  }

  MV_REF *mv_ref_base = start_frame_buf->mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int start_mvs_rows =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_rows, TMVP_SHIFT_BITS);
  const int start_mvs_cols =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_cols, TMVP_SHIFT_BITS);
  (void)mvs_rows;

  assert(cm->tmvp_sample_step > 0);
  for (int blk_row = 0; blk_row < start_mvs_rows;
       blk_row += cm->tmvp_sample_step) {
    for (int blk_col = 0; blk_col < start_mvs_cols;
         blk_col += cm->tmvp_sample_step) {
      MV_REF *mv_ref = &mv_ref_base[blk_row * start_mvs_cols + blk_col];
      const MV_REFERENCE_FRAME ref_frame = mv_ref->ref_frame[side_idx];
      if (is_inter_ref_frame(ref_frame)) {
        MV ref_mv = mv_ref->mv[side_idx].as_mv;
        fetch_mv_from_tmvp(&ref_mv);
        int scaled_blk_col = blk_col;
        int scaled_blk_row = blk_row;

        MV_REFERENCE_FRAME end_frame = start_ref_map[ref_frame];
        if (end_frame == start_frame) continue;

        if (cm->seq_params.enable_mv_traj) {
          check_traj_intersect(cm, start_frame, end_frame, &ref_mv,
                               scaled_blk_row, scaled_blk_col, mvs_cols);
        }

        int ref_frame_offset = ref_offset[ref_frame];
        int pos_valid = ref_abs_offset[ref_frame] <= MAX_FRAME_DISTANCE;
        if ((side_idx == 0 && ref_frame_offset < 0) ||
            (side_idx == 1 && ref_frame_offset > 0)) {
          pos_valid = 0;
        }
        if (pos_valid) {
          int_mv this_mv;
          int mi_r = scaled_blk_row;
          int mi_c = scaled_blk_col;
          if (!(ref_mv.row == 0 && ref_mv.col == 0)) {
            tip_get_mv_projection(&this_mv.as_mv, ref_mv,
                                  temporal_scale_factor[ref_frame]);
            pos_valid = get_block_position_no_constraint(
                cm, &mi_r, &mi_c, scaled_blk_row, scaled_blk_col, this_mv.as_mv,
                0);
          } else {
            this_mv.as_int = 0;
          }
          mi_r = (mi_r >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;
          mi_c = (mi_c >> cm->tmvp_sample_stepl2) << cm->tmvp_sample_stepl2;

          if (pos_valid)
            pos_valid = check_block_position(cm, scaled_blk_row, scaled_blk_col,
                                             mi_r, mi_c);
          if (pos_valid) {
            if (cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_int == INVALID_MV ||
                (target_frame != -1 && end_frame == target_frame &&
                 cm->tpl_mvs_rows[mi_r][mi_c].ref_frame_offset !=
                     ref_abs_offset[ref_frame])) {
              if (cm->seq_params.enable_mv_traj) {
                int blk_id_k = get_blk_id_k(mi_c, cm->tmvp_proc_sizel2);
                int_mv ***blk_id_map_rows = cm->blk_id_map_rows[blk_id_k];

                cm->id_offset_map_rows[start_frame][mi_r][mi_c].as_mv.row =
                    clamp(-this_mv.as_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
                cm->id_offset_map_rows[start_frame][mi_r][mi_c].as_mv.col =
                    clamp(-this_mv.as_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
                blk_id_map_rows[start_frame][scaled_blk_row][scaled_blk_col]
                    .as_mv.row = mi_r;
                blk_id_map_rows[start_frame][scaled_blk_row][scaled_blk_col]
                    .as_mv.col = mi_c;

                if (end_frame != NONE_FRAME) {
                  MV end_frame_mv;
                  tip_get_mv_projection(&end_frame_mv, ref_mv,
                                        ref_temporal_scale_factor[ref_frame]);
                  cm->id_offset_map_rows[end_frame][mi_r][mi_c].as_mv.row =
                      clamp(end_frame_mv.row, -REFMVS_LIMIT, REFMVS_LIMIT);
                  cm->id_offset_map_rows[end_frame][mi_r][mi_c].as_mv.col =
                      clamp(end_frame_mv.col, -REFMVS_LIMIT, REFMVS_LIMIT);
                  int end_row = 0, end_col = 0;
                  int end_pos_valid;
                  if (ref_mv.row == 0 && ref_mv.col == 0) {
                    end_pos_valid = 1;
                    end_row = scaled_blk_row;
                    end_col = scaled_blk_col;
                  } else {
                    end_pos_valid = get_block_position_no_constraint(
                        cm, &end_row, &end_col, scaled_blk_row, scaled_blk_col,
                        ref_mv, 0);
                  }
                  end_row = (end_row >> cm->tmvp_sample_stepl2)
                            << cm->tmvp_sample_stepl2;
                  end_col = (end_col >> cm->tmvp_sample_stepl2)
                            << cm->tmvp_sample_stepl2;

                  if (end_pos_valid)
                    end_pos_valid =
                        check_block_position(cm, end_row, end_col, mi_r, mi_c);

                  if (end_pos_valid) {
                    blk_id_map_rows[end_frame][end_row][end_col].as_mv.row =
                        mi_r;
                    blk_id_map_rows[end_frame][end_row][end_col].as_mv.col =
                        mi_c;
                  }
                }
              }

              if (side_idx == 1) {
                ref_mv.row = -ref_mv.row;
                ref_mv.col = -ref_mv.col;
                ref_frame_offset = ref_abs_offset[ref_frame];
              }
              cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_mv.row = ref_mv.row;
              cm->tpl_mvs_rows[mi_r][mi_c].mfmv0.as_mv.col = ref_mv.col;
              cm->tpl_mvs_rows[mi_r][mi_c].ref_frame_offset = ref_frame_offset;
            }
          }
        }
      }
    }
  }
  return 1;
}

// Whether to do interpolation for sampled TMVP mvs.
#define DO_AVG_FILL 1

static INLINE int calc_avg(int sum, int count) {
  assert(count > 0 && count <= 4);
  if (count == 1) {
    return sum;
  }
  if (count == 2) {
    return ROUND_POWER_OF_TWO_SIGNED(sum, 1);
  }
  if (count == 4) {
    return ROUND_POWER_OF_TWO_SIGNED(sum, 2);
  }
  assert(count == 3);
  return ROUND_POWER_OF_TWO_SIGNED(sum * 85, 8);
}

// Calculate the average MV length in the reference frame motion field
// candidate.
void calc_and_set_avg_lengths(AV2_COMMON *cm, int ref, int side) {
  RefCntBuffer *buf = get_ref_frame_buf(cm, ref);

  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);

  int64_t avg_row = 0;
  int64_t avg_col = 0;
  int64_t count = 0;

  const int buf_hint = buf->display_order_hint;

  for (int r = 0; r < mvs_rows; r += 2) {
    for (int c = 0; c < mvs_cols; c += 2) {
      const MV_REF *mv_ref = &buf->mvs[r * mvs_cols + c];
      if (mv_ref->ref_frame[side] != NONE_FRAME) {
        const int ref_hint =
            buf->ref_display_order_hint[mv_ref->ref_frame[side]];

        const int dist = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                               buf_hint, ref_hint));

        if (dist != 0) {
          MV ref_mv = mv_ref->mv[side].as_mv;
          fetch_mv_from_tmvp(&ref_mv);
          avg_row += abs(ref_mv.row * 2 / dist);
          avg_col += abs(ref_mv.col * 2 / dist);
          count++;
        }
      }
    }
  }
  buf->avg_row[side] = count == 0 ? 0 : avg_row / count;
  buf->avg_col[side] = count == 0 ? 0 : avg_col / count;
  return;
}

// Determine whether we should use sampling of TMVP mvs for the current frame.
void determine_tmvp_sample_step(AV2_COMMON *cm,
                                int checked_ref[INTER_REFS_PER_FRAME][2]) {
  cm->tmvp_sample_step = 1;
  cm->tmvp_sample_stepl2 = 0;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int sb_size = block_size_high[seq_params->sb_size];
  if (sb_size == 64) {
    return;
  }
  int small_count = 0;
  int large_count = 0;
  const int cur_hint = cm->cur_frame->display_order_hint;
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    for (int j = 0; j < 2; j++) {
      if (!checked_ref[i][j]) continue;
      const RefCntBuffer *buf = get_ref_frame_buf(cm, i);
      if (buf == NULL || buf->is_restricted) continue;
      const int buf_hint = buf->display_order_hint;
      if (!is_ref_motion_field_eligible(cm, buf)) continue;
      calc_and_set_avg_lengths(cm, i, j);
      const int dist = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                             cur_hint, buf_hint));
      if (buf->avg_row[j] * dist / 16 > 8 || buf->avg_col[j] * dist / 16 > 16) {
        large_count++;
      } else {
        small_count++;
      }
    }
  }
  if (large_count > small_count * 2) {
    cm->tmvp_sample_step = 2;
    cm->tmvp_sample_stepl2 = 1;
  }
}

// Interpolate the sampled tpl_mvs.
void av2_fill_tpl_mvs_sample_gap(AV2_COMMON *cm) {
  assert(cm->tmvp_sample_step > 0);
  if (cm->tmvp_sample_step != 2) {
    return;
  }
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);

  const SequenceHeader *const seq_params = &cm->seq_params;
  const int sb_size = block_size_high[seq_params->sb_size];
  const int mf_sb_size_log2 =
      get_mf_sb_size_log2(sb_size, cm->mib_size_log2, cm->tmvp_sample_step);
  const int mf_sb_size = (1 << mf_sb_size_log2);
  const int sb_tmvp_size = (mf_sb_size >> TMVP_MI_SZ_LOG2);
  const int sb_tmvp_size_log2 = mf_sb_size_log2 - TMVP_MI_SZ_LOG2;

  for (int r = 0; r < mvs_rows; r += cm->tmvp_sample_step) {
    for (int c = 0; c < mvs_cols; c += cm->tmvp_sample_step) {
      if (cm->tpl_mvs_rows[r][c].mfmv0.as_int == INVALID_MV) continue;

      int count[3] = { 0 };   // [hor, ver, diag]
      int avg[3][2] = { 0 };  // [hor, ver, diag][row, col]
      int norm_ref_offset = -1;

      // this
      if (cm->tpl_mvs_rows[r][c].mfmv0.as_int != INVALID_MV) {
        norm_ref_offset = cm->tpl_mvs_rows[r][c].ref_frame_offset;

        count[0]++;
        count[1]++;
        count[2]++;

        avg[0][0] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.row;
        avg[1][0] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.row;
        avg[2][0] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.row;

        avg[0][1] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.col;
        avg[1][1] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.col;
        avg[2][1] += cm->tpl_mvs_rows[r][c].mfmv0.as_mv.col;
      }

      if (DO_AVG_FILL) {
        const int base_blk_row = (r >> sb_tmvp_size_log2) << sb_tmvp_size_log2;
        const int base_blk_col = (c >> sb_tmvp_size_log2) << sb_tmvp_size_log2;

        int_mv tmp_mv;
        // right
        const int c_right_step = c + cm->tmvp_sample_step;
        if (c_right_step < mvs_cols &&
            c_right_step < base_blk_col + sb_tmvp_size &&
            cm->tpl_mvs_rows[r][c_right_step].mfmv0.as_int != INVALID_MV) {
          if (norm_ref_offset == -1)
            norm_ref_offset =
                cm->tpl_mvs_rows[r][c_right_step].ref_frame_offset;

          get_mv_projection_clamp(
              &tmp_mv.as_mv, cm->tpl_mvs_rows[r][c_right_step].mfmv0.as_mv,
              norm_ref_offset,
              cm->tpl_mvs_rows[r][c_right_step].ref_frame_offset, -REFMVS_LIMIT,
              REFMVS_LIMIT);

          count[0]++;
          count[2]++;

          avg[0][0] += tmp_mv.as_mv.row;
          avg[2][0] += tmp_mv.as_mv.row;

          avg[0][1] += tmp_mv.as_mv.col;
          avg[2][1] += tmp_mv.as_mv.col;
        }

        // lower
        const int r_lower_step = r + cm->tmvp_sample_step;
        if (r_lower_step < mvs_rows &&
            r_lower_step < base_blk_row + sb_tmvp_size &&
            cm->tpl_mvs_rows[r_lower_step][c].mfmv0.as_int != INVALID_MV) {
          if (norm_ref_offset == -1)
            norm_ref_offset =
                cm->tpl_mvs_rows[r_lower_step][c].ref_frame_offset;

          get_mv_projection_clamp(
              &tmp_mv.as_mv, cm->tpl_mvs_rows[r_lower_step][c].mfmv0.as_mv,
              norm_ref_offset,
              cm->tpl_mvs_rows[r_lower_step][c].ref_frame_offset, -REFMVS_LIMIT,
              REFMVS_LIMIT);

          count[1]++;
          count[2]++;

          avg[1][0] += tmp_mv.as_mv.row;
          avg[2][0] += tmp_mv.as_mv.row;

          avg[1][1] += tmp_mv.as_mv.col;
          avg[2][1] += tmp_mv.as_mv.col;
        }

        // lower_right
        if (r_lower_step < mvs_rows &&
            r_lower_step < base_blk_row + sb_tmvp_size &&
            c_right_step < mvs_cols &&
            c_right_step < base_blk_col + sb_tmvp_size &&
            cm->tpl_mvs_rows[r_lower_step][c_right_step].mfmv0.as_int !=
                INVALID_MV) {
          if (norm_ref_offset == -1)
            norm_ref_offset =
                cm->tpl_mvs_rows[r_lower_step][c_right_step].ref_frame_offset;

          get_mv_projection_clamp(
              &tmp_mv.as_mv,
              cm->tpl_mvs_rows[r_lower_step][c_right_step].mfmv0.as_mv,
              norm_ref_offset,
              cm->tpl_mvs_rows[r_lower_step][c_right_step].ref_frame_offset,
              -REFMVS_LIMIT, REFMVS_LIMIT);

          count[2]++;

          avg[2][0] += tmp_mv.as_mv.row;
          avg[2][1] += tmp_mv.as_mv.col;
        }
      }

      const int c_right = c + 1;
      const int r_lower = r + 1;
      if (c_right < mvs_cols && count[0] > 0) {
        assert(cm->tpl_mvs_rows[r][c_right].mfmv0.as_int == INVALID_MV);
        cm->tpl_mvs_rows[r][c_right].mfmv0.as_mv.row =
            calc_avg(avg[0][0], count[0]);
        cm->tpl_mvs_rows[r][c_right].mfmv0.as_mv.col =
            calc_avg(avg[0][1], count[0]);
        cm->tpl_mvs_rows[r][c_right].ref_frame_offset = norm_ref_offset;
      }
      if (r_lower < mvs_rows && count[1] > 0) {
        assert(cm->tpl_mvs_rows[r_lower][c].mfmv0.as_int == INVALID_MV);
        cm->tpl_mvs_rows[r_lower][c].mfmv0.as_mv.row =
            calc_avg(avg[1][0], count[1]);
        cm->tpl_mvs_rows[r_lower][c].mfmv0.as_mv.col =
            calc_avg(avg[1][1], count[1]);
        cm->tpl_mvs_rows[r_lower][c].ref_frame_offset = norm_ref_offset;
      }
      if (r_lower < mvs_rows && c_right < mvs_cols && count[2] > 0) {
        assert(cm->tpl_mvs_rows[r_lower][c_right].mfmv0.as_int == INVALID_MV);
        cm->tpl_mvs_rows[r_lower][c_right].mfmv0.as_mv.row =
            calc_avg(avg[2][0], count[2]);
        cm->tpl_mvs_rows[r_lower][c_right].mfmv0.as_mv.col =
            calc_avg(avg[2][1], count[2]);
        cm->tpl_mvs_rows[r_lower][c_right].ref_frame_offset = norm_ref_offset;
      }
    }
  }
}

// Interpolate the sampled id_offset_map.
static void fill_id_offset_sample_gap(AV2_COMMON *cm) {
  assert(cm->tmvp_sample_step > 0);
  if (cm->tmvp_sample_step != 2) {
    return;
  }
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int sb_size = block_size_high[seq_params->sb_size];
  const int mf_sb_size_log2 =
      get_mf_sb_size_log2(sb_size, cm->mib_size_log2, cm->tmvp_sample_step);
  const int mf_sb_size = (1 << mf_sb_size_log2);
  const int sb_tmvp_size = (mf_sb_size >> TMVP_MI_SZ_LOG2);
  const int sb_tmvp_size_log2 = mf_sb_size_log2 - TMVP_MI_SZ_LOG2;
  for (int rf = 0; rf < cm->ref_frames_info.num_total_refs; rf++) {
    const RefCntBuffer *buf = get_ref_frame_buf(cm, rf);
    if (buf == NULL || buf->is_restricted) continue;
    for (int r = 0; r < mvs_rows; r += cm->tmvp_sample_step) {
      for (int c = 0; c < mvs_cols; c += cm->tmvp_sample_step) {
        if (cm->id_offset_map_rows[rf][r][c].as_int == INVALID_MV) continue;

        int count[3] = { 0 };   // [hor, ver, diag]
        int avg[3][2] = { 0 };  // [hor, ver, diag][row, col]

        // this
        if (cm->id_offset_map_rows[rf][r][c].as_int != INVALID_MV) {
          count[0]++;
          count[1]++;
          count[2]++;

          avg[0][0] += cm->id_offset_map_rows[rf][r][c].as_mv.row;
          avg[1][0] += cm->id_offset_map_rows[rf][r][c].as_mv.row;
          avg[2][0] += cm->id_offset_map_rows[rf][r][c].as_mv.row;

          avg[0][1] += cm->id_offset_map_rows[rf][r][c].as_mv.col;
          avg[1][1] += cm->id_offset_map_rows[rf][r][c].as_mv.col;
          avg[2][1] += cm->id_offset_map_rows[rf][r][c].as_mv.col;
        }

        if (DO_AVG_FILL) {
          const int base_blk_row = (r >> sb_tmvp_size_log2)
                                   << sb_tmvp_size_log2;
          const int base_blk_col = (c >> sb_tmvp_size_log2)
                                   << sb_tmvp_size_log2;

          // right
          const int c_right_step = c + cm->tmvp_sample_step;
          if (c_right_step < mvs_cols &&
              c_right_step < base_blk_col + sb_tmvp_size &&
              cm->id_offset_map_rows[rf][r][c_right_step].as_int !=
                  INVALID_MV) {
            count[0]++;
            count[2]++;

            avg[0][0] += cm->id_offset_map_rows[rf][r][c_right_step].as_mv.row;
            avg[2][0] += cm->id_offset_map_rows[rf][r][c_right_step].as_mv.row;

            avg[0][1] += cm->id_offset_map_rows[rf][r][c_right_step].as_mv.col;
            avg[2][1] += cm->id_offset_map_rows[rf][r][c_right_step].as_mv.col;
          }

          // lower
          const int r_lower_step = r + cm->tmvp_sample_step;
          if (r_lower_step < mvs_rows &&
              r_lower_step < base_blk_row + sb_tmvp_size &&
              cm->id_offset_map_rows[rf][r_lower_step][c].as_int !=
                  INVALID_MV) {
            count[1]++;
            count[2]++;

            avg[1][0] += cm->id_offset_map_rows[rf][r_lower_step][c].as_mv.row;
            avg[2][0] += cm->id_offset_map_rows[rf][r_lower_step][c].as_mv.row;

            avg[1][1] += cm->id_offset_map_rows[rf][r_lower_step][c].as_mv.col;
            avg[2][1] += cm->id_offset_map_rows[rf][r_lower_step][c].as_mv.col;
          }

          // lower_right
          if (r_lower_step < mvs_rows &&
              r_lower_step < base_blk_row + sb_tmvp_size &&
              c_right_step < mvs_cols &&
              c_right_step < base_blk_col + sb_tmvp_size &&
              cm->id_offset_map_rows[rf][r_lower_step][c_right_step].as_int !=
                  INVALID_MV) {
            count[2]++;

            avg[2][0] += cm->id_offset_map_rows[rf][r_lower_step][c_right_step]
                             .as_mv.row;
            avg[2][1] += cm->id_offset_map_rows[rf][r_lower_step][c_right_step]
                             .as_mv.col;
          }
        }

        const int c_right = c + 1;
        const int r_lower = r + 1;
        if (c_right < mvs_cols && count[0] > 0) {
          assert(cm->id_offset_map_rows[rf][r][c_right].as_int == INVALID_MV);
          cm->id_offset_map_rows[rf][r][c_right].as_mv.row =
              calc_avg(avg[0][0], count[0]);
          cm->id_offset_map_rows[rf][r][c_right].as_mv.col =
              calc_avg(avg[0][1], count[0]);
        }
        if (r_lower < mvs_rows && count[1] > 0) {
          assert(cm->id_offset_map_rows[rf][r_lower][c].as_int == INVALID_MV);
          cm->id_offset_map_rows[rf][r_lower][c].as_mv.row =
              calc_avg(avg[1][0], count[1]);
          cm->id_offset_map_rows[rf][r_lower][c].as_mv.col =
              calc_avg(avg[1][1], count[1]);
        }
        if (r_lower < mvs_rows && c_right < mvs_cols && count[2] > 0) {
          assert(cm->id_offset_map_rows[rf][r_lower][c_right].as_int ==
                 INVALID_MV);
          cm->id_offset_map_rows[rf][r_lower][c_right].as_mv.row =
              calc_avg(avg[2][0], count[2]);
          cm->id_offset_map_rows[rf][r_lower][c_right].as_mv.col =
              calc_avg(avg[2][1], count[2]);
        }
      }
    }
  }
}

void av2_setup_motion_field(AV2_COMMON *cm) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;

  memset(cm->ref_frame_side, 0, sizeof(cm->ref_frame_side));
  memset(cm->ref_frame_relative_dist, 0, sizeof(cm->ref_frame_relative_dist));
  if (order_hint_info->order_hint_bits_minus_1 < 0) return;

  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  int size = mvs_rows * mvs_cols;
  for (int idx = 0; idx < size; ++idx) {
    tpl_mvs_base[idx].mfmv0.as_int = INVALID_MV;
    tpl_mvs_base[idx].ref_frame_offset = 0;
  }

  const RefCntBuffer *ref_buf[INTER_REFS_PER_FRAME];

  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    ref_buf[i] = NULL;
    cm->ref_frame_side[i] = 0;
    cm->ref_frame_relative_dist[i] = 0;
  }
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
    if (get_ref_frame_buf(cm, i)->is_restricted) {
      cm->ref_frame_relative_dist[i] = INT_MAX;
    }
  }
  for (int index = 0; index < cm->ref_frames_info.num_past_refs; index++) {
    const int ref_frame = cm->ref_frames_info.past_refs[index];
    cm->ref_frame_side[ref_frame] = 0;
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    assert(!buf->is_restricted);
    ref_buf[ref_frame] = buf;
    const int relative_dist =
        get_relative_dist(order_hint_info, buf->display_order_hint,
                          cm->cur_frame->display_order_hint);
    cm->ref_frame_relative_dist[ref_frame] = abs(relative_dist);
  }
  for (int index = 0; index < cm->ref_frames_info.num_future_refs; index++) {
    const int ref_frame = cm->ref_frames_info.future_refs[index];
    cm->ref_frame_side[ref_frame] = 1;
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    assert(!buf->is_restricted);
    ref_buf[ref_frame] = buf;
    const int relative_dist =
        get_relative_dist(order_hint_info, buf->display_order_hint,
                          cm->cur_frame->display_order_hint);
    cm->ref_frame_relative_dist[ref_frame] = abs(relative_dist);
  }
  for (int index = 0; index < cm->ref_frames_info.num_cur_refs; index++) {
    const int ref_frame = cm->ref_frames_info.cur_refs[index];
    cm->ref_frame_side[ref_frame] = -1;
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    assert(!buf->is_restricted);
    ref_buf[ref_frame] = buf;
    const int relative_dist =
        get_relative_dist(order_hint_info, buf->display_order_hint,
                          cm->cur_frame->display_order_hint);
    cm->ref_frame_relative_dist[ref_frame] = abs(relative_dist);
  }

  cm->has_both_sides_refs = (cm->ref_frames_info.num_future_refs > 0) &&
                            (cm->ref_frames_info.num_past_refs > 0);

  (void)ref_buf;
  if (cm->seq_params.enable_mv_traj) {
    for (int rf = 0; rf < INTER_REFS_PER_FRAME; rf++) {
      for (int i = 0; i < mvs_rows * mvs_cols; i++) {
        cm->id_offset_map[rf][i].as_int = INVALID_MV;
        for (int k = 0; k < 3; k++) {
          cm->blk_id_map[k][rf][i].as_int = INVALID_MV;
        }
      }
    }
  }

  // Find the sorted map of refs.
  int sort_ref[INTER_REFS_PER_FRAME] = { 0, 1, 2, 3, 4, 5, 6 };
  int disp_order[INTER_REFS_PER_FRAME] = { 0 };

  bool is_overlay[INTER_REFS_PER_FRAME] = { false };
  for (int rf = cm->ref_frames_info.num_total_refs - 1; rf >= 0; rf--) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, rf);
    if (buf != NULL && buf->is_restricted) {
      continue;
    }
    if (is_ref_overlay(cm, rf) &&
        get_ref_frame_buf(cm, rf)->frame_type != KEY_FRAME) {
      is_overlay[rf] = true;
    }
  }

  for (int rf = 0; rf < cm->ref_frames_info.num_total_refs; rf++) {
    disp_order[rf] = get_ref_frame_buf(cm, rf)->display_order_hint;
  }
  // Sort the points by x.
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
    for (int j = i + 1; j < cm->ref_frames_info.num_total_refs; j++) {
      if (!get_ref_frame_buf(cm, sort_ref[j])->is_restricted &&
          get_relative_dist(order_hint_info, disp_order[j], disp_order[i]) <
              0) {
        int tmp = disp_order[i];
        disp_order[i] = disp_order[j];
        disp_order[j] = tmp;

        tmp = sort_ref[i];
        sort_ref[i] = sort_ref[j];
        sort_ref[j] = tmp;
      }
    }
  }
  int cur_disp_order = cm->cur_frame->display_order_hint;
  // The idx of rf in sort_ref that is before current frame, and closest.
  int cur_frame_sort_idx = -1;
  for (int rf_idx = 0; rf_idx < cm->ref_frames_info.num_total_refs; rf_idx++) {
    if (get_relative_dist(order_hint_info, disp_order[rf_idx], cur_disp_order) <
        0) {
      cur_frame_sort_idx = rf_idx;
    } else {
      break;
    }
  }

  int rf_stack[INTER_REFS_PER_FRAME];
  int visited[INTER_REFS_PER_FRAME] = { 0 };
  int stack_count = 0;
  for (int rf = 0; rf < cm->ref_frames_info.num_total_refs; rf++) {
    if (get_ref_frame_buf(cm, rf)->is_restricted) continue;
    if (visited[rf] == 0) {
      recur_topo_sort_refs(cm, is_overlay, rf_stack, visited, &stack_count, rf);
    }
  }

  if (stack_count < 2) return;

  int rf_topo_stack_idx[INTER_REFS_PER_FRAME];
  for (int rf = 0; rf < cm->ref_frames_info.num_total_refs; rf++) {
    rf_topo_stack_idx[rf] = -1;
    if (get_ref_frame_buf(cm, rf)->is_restricted) continue;
    for (int stack_idx = 0; stack_idx < stack_count; stack_idx++) {
      if (rf_stack[stack_idx] == rf) {
        rf_topo_stack_idx[rf] = stack_idx;
        break;
      }
    }
  }

  struct ProcessRefTMVP process_ref[20];
  int process_count = 0;
  int checked_ref[INTER_REFS_PER_FRAME][2] = { 0 };
  int checked_count = 0;

  if (cm->seq_params.enable_tip) {
    if (cm->has_both_sides_refs || cm->ref_frames_info.num_past_refs >= 2) {
      if (cm->has_both_sides_refs) {
        cm->tip_ref.ref_frame[0] = sort_ref[cur_frame_sort_idx];
        cm->tip_ref.ref_frame[1] = sort_ref[cur_frame_sort_idx + 1];
      } else if (cm->ref_frames_info.num_past_refs >= 2) {
        cm->tip_ref.ref_frame[0] = sort_ref[cur_frame_sort_idx];
        cm->tip_ref.ref_frame[1] = sort_ref[cur_frame_sort_idx - 1];
      }
      int start_frame, target_frame;
      int side;
      if (rf_topo_stack_idx[cm->tip_ref.ref_frame[0]] >
          rf_topo_stack_idx[cm->tip_ref.ref_frame[1]]) {
        start_frame = cm->tip_ref.ref_frame[0];
        target_frame = cm->tip_ref.ref_frame[1];
      } else {
        start_frame = cm->tip_ref.ref_frame[1];
        target_frame = cm->tip_ref.ref_frame[0];
      }
      assert(!(get_ref_frame_buf(cm, start_frame)->is_restricted) &&
             !(get_ref_frame_buf(cm, target_frame)->is_restricted));
      int dist_diff = get_relative_dist(
          order_hint_info,
          get_ref_frame_buf(cm, start_frame)->display_order_hint,
          get_ref_frame_buf(cm, target_frame)->display_order_hint);

      side = dist_diff < 0 ? 1 : 0;
      check_and_add_process_ref(cm, TIP_MFMV_STACK_SIZE, 0, start_frame,
                                target_frame, side, checked_ref, &checked_count,
                                process_ref, &process_count);
    } else {
      cm->tip_ref.ref_frame[0] = NONE_FRAME;
      cm->tip_ref.ref_frame[1] = NONE_FRAME;
    }
  }
  int valid_ref_num = cm->ref_frames_info.num_total_refs;
  if (cm->ref_frames_info.num_total_refs >
      cm->ref_frames_info.num_valid_refs_without_restricted_ref)
    valid_ref_num = cm->ref_frames_info.num_valid_refs_without_restricted_ref;
  for (int group_idx = 0; group_idx < 2; ++group_idx) {
    int past_ref_sort_idx =
        cur_frame_sort_idx >= group_idx ? cur_frame_sort_idx - group_idx : -1;
    if (past_ref_sort_idx >= 0 &&
        !has_future_ref(cm, sort_ref[past_ref_sort_idx]))
      past_ref_sort_idx = -1;

    int future_ref_sort_idx = cur_frame_sort_idx < valid_ref_num - group_idx - 1
                                  ? cur_frame_sort_idx + 1 + group_idx
                                  : -1;
    if (future_ref_sort_idx >= 0 &&
        !has_past_ref(cm, sort_ref[future_ref_sort_idx]))
      future_ref_sort_idx = -1;

    const int past_ref_to_its_ref_dist =
        past_ref_sort_idx >= 0
            ? get_dist_to_closest_interp_ref(cm, sort_ref[past_ref_sort_idx], 0)
            : -1;

    const int future_ref_to_its_ref_dist =
        future_ref_sort_idx >= 0 ? get_dist_to_closest_interp_ref(
                                       cm, sort_ref[future_ref_sort_idx], 1)
                                 : -1;

    if (future_ref_to_its_ref_dist < past_ref_to_its_ref_dist) {
      if (future_ref_sort_idx != -1) {
        check_and_add_process_ref(
            cm, TIP_MFMV_STACK_SIZE, 0, sort_ref[future_ref_sort_idx], -1, 0,
            checked_ref, &checked_count, process_ref, &process_count);
      }
      if (past_ref_sort_idx != -1) {
        check_and_add_process_ref(
            cm, TIP_MFMV_STACK_SIZE, 0, sort_ref[past_ref_sort_idx], -1, 1,
            checked_ref, &checked_count, process_ref, &process_count);
      }
    } else {
      if (past_ref_sort_idx != -1) {
        check_and_add_process_ref(
            cm, TIP_MFMV_STACK_SIZE, 0, sort_ref[past_ref_sort_idx], -1, 1,
            checked_ref, &checked_count, process_ref, &process_count);
      }
      if (future_ref_sort_idx != -1) {
        check_and_add_process_ref(
            cm, TIP_MFMV_STACK_SIZE, 0, sort_ref[future_ref_sort_idx], -1, 0,
            checked_ref, &checked_count, process_ref, &process_count);
      }
    }
  }

  if (cur_frame_sort_idx >= 0) {
    check_and_add_process_ref(cm, TIP_MFMV_STACK_SIZE, 0,
                              sort_ref[cur_frame_sort_idx], -1, 0, checked_ref,
                              &checked_count, process_ref, &process_count);
  }
  if (cur_frame_sort_idx >= 1) {
    check_and_add_process_ref(
        cm, TIP_MFMV_STACK_SIZE, 0, sort_ref[cur_frame_sort_idx - 1], -1, 0,
        checked_ref, &checked_count, process_ref, &process_count);
  }

  for (int ri = stack_count - 1; ri > 0; ri--) {
    int side;
    const int ref_hint =
        get_ref_frame_buf(cm, rf_stack[ri])->display_order_hint;
    if (get_relative_dist(order_hint_info, ref_hint, cur_disp_order) < 0) {
      side = 1;
    } else {
      side = 0;
    }

    if (!checked_ref[rf_stack[ri]][side]) {
      check_and_add_process_ref(cm, MFMV_STACK_SIZE, 0, rf_stack[ri], -1, side,
                                checked_ref, &checked_count, process_ref,
                                &process_count);
    }

    side = !side;
    if (!checked_ref[rf_stack[ri]][side]) {
      check_and_add_process_ref(cm, MFMV_STACK_SIZE, 0, rf_stack[ri], -1, side,
                                checked_ref, &checked_count, process_ref,
                                &process_count);
    }
  }

  // At the encoder cm->tmvp_sample_step is initialized as -1, while at the
  // decoder it is already read from bitstream before this.
  if (cm->tmvp_sample_step < 0) {
    determine_tmvp_sample_step(cm, checked_ref);
  }

  get_proc_size_and_offset(cm);

  assert(order_hint_info->reduced_ref_frame_mvs_mode >= 0 &&
         order_hint_info->reduced_ref_frame_mvs_mode <= 1);
  if (order_hint_info->reduced_ref_frame_mvs_mode > 0) {
    // Cap the reference frame combination to 1 for reduced_ref_frame_mvs_mode.
    process_count = AVMMIN(1, process_count);
  }
  for (int pi = 0; pi < process_count; pi++) {
    if (process_ref[pi].type == 0) {
      motion_field_projection_side(cm, process_ref[pi].start_frame,
                                   process_ref[pi].target_frame,
                                   process_ref[pi].side);
    } else {
      motion_field_projection_start_target(cm, process_ref[pi].start_frame,
                                           process_ref[pi].target_frame);
    }
  }

  if (cm->seq_params.enable_mv_traj) {
    fill_id_offset_sample_gap(cm);
  }
}

void av2_setup_ref_frame_sides(AV2_COMMON *cm) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;

  memset(cm->ref_frame_side, 0, sizeof(cm->ref_frame_side));
  memset(cm->ref_frame_relative_dist, 0, sizeof(cm->ref_frame_relative_dist));
  if (order_hint_info->order_hint_bits_minus_1 < 0) return;

  const int cur_order_hint = cm->cur_frame->display_order_hint;

  const int num_refs =
      AVMMIN(cm->ref_frames_info.num_total_refs, INTER_REFS_PER_FRAME);
  for (int ref_frame = 0; ref_frame < num_refs; ref_frame++) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    int order_hint = 0;
    if (buf != NULL && buf->is_restricted) {
      cm->ref_frame_side[ref_frame] = 0;
      cm->ref_frame_relative_dist[ref_frame] = INT_MAX;
      continue;
    }
    if (buf != NULL) order_hint = buf->display_order_hint;
    const int relative_dist =
        get_relative_dist(order_hint_info, order_hint, cur_order_hint);
    if (relative_dist > 0) {
      cm->ref_frame_side[ref_frame] = 1;
    } else if (order_hint == cur_order_hint) {
      cm->ref_frame_side[ref_frame] = -1;
    }
    cm->ref_frame_relative_dist[ref_frame] = abs(relative_dist);
  }
}

static INLINE void record_samples(const MB_MODE_INFO *mbmi, int ref, int *pts,
                                  int *pts_inref, int row_offset, int sign_r,
                                  int col_offset, int sign_c) {
  int bw = block_size_wide[mbmi->sb_type[PLANE_TYPE_Y]];
  int bh = block_size_high[mbmi->sb_type[PLANE_TYPE_Y]];
  int x = col_offset * MI_SIZE + sign_c * AVMMAX(bw, MI_SIZE) / 2 - 1;
  int y = row_offset * MI_SIZE + sign_r * AVMMAX(bh, MI_SIZE) / 2 - 1;

  pts[0] = GET_MV_SUBPEL(x);
  pts[1] = GET_MV_SUBPEL(y);
  pts_inref[0] = GET_MV_SUBPEL(x) + mbmi->mv[ref].as_mv.col;
  pts_inref[1] = GET_MV_SUBPEL(y) + mbmi->mv[ref].as_mv.row;
}
// Note: Samples returned are at 1/8-pel precision
// Sample are the neighbor block center point's coordinates relative to the
// left-top pixel of current block.
uint8_t av2_findSamples(const AV2_COMMON *cm, MACROBLOCKD *xd, int *pts,
                        int *pts_inref, int ref_idx) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int ref_frame = mbmi->ref_frame[ref_idx];
  const int up_available = xd->up_available;
  const int left_available = xd->left_available;
  int i, mi_step;
  uint8_t np = 0;
  int do_top_left = 1;
  int do_top_right = 1;
  const int mi_stride = xd->mi_stride;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const TileInfo *const tile = &xd->tile;

  const int is_sb_border = (mi_row % cm->mib_size == 0);
  // scan the nearest above rows
  if (up_available) {
    const int mi_row_offset = -1;
    MB_MODE_INFO *above_mbmi = xd->mi[mi_row_offset * mi_stride];
    const int above_mc_offset_start = above_mbmi->mi_col_start - mi_col;

    if (above_mc_offset_start < 0) do_top_left = 0;

    for (i = above_mc_offset_start;
         i < AVMMIN(xd->width, cm->mi_params.mi_cols - mi_col); i += mi_step) {
      above_mbmi = xd->mi[i + mi_row_offset * mi_stride];
      mi_step = mi_size_wide[above_mbmi->sb_type[PLANE_TYPE_Y]];
      if (is_sb_border && ((mi_col + i) % 2)) {
        // Block MI width is 1 and block is in odd column
        if (mi_step == 1) continue;

        const int adjust_mi_col_offset =
            ((ROUND_POWER_OF_TWO(mi_col + i, 1)) << 1) - mi_col;

        const POSITION block_pos = { -1, adjust_mi_col_offset };
        if (!is_inside(tile, mi_col, mi_row, &block_pos)) {
          continue;
        }

        above_mbmi = xd->mi[adjust_mi_col_offset + mi_row_offset * mi_stride];
      }

      for (int ref = 0; ref < 1 + has_second_ref(above_mbmi); ++ref) {
        if (above_mbmi->ref_frame[ref] == ref_frame) {
          record_samples(above_mbmi, ref, pts, pts_inref, 0, -1, i, 1);
          pts += 2;
          pts_inref += 2;
          if (++np >= LEAST_SQUARES_SAMPLES_MAX)
            return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }

    do_top_right = (i == xd->width) && (i < cm->mi_params.mi_cols - mi_col);
  }

  // Scan the nearest left columns
  if (left_available) {
    const int mi_col_offset = -1;
    MB_MODE_INFO *left_mbmi = xd->mi[mi_col_offset];
    const int left_mr_offset_start = left_mbmi->mi_row_start - mi_row;

    if (left_mr_offset_start < 0) do_top_left = 0;

    for (i = left_mr_offset_start;
         i < AVMMIN(xd->height, cm->mi_params.mi_rows - mi_row); i += mi_step) {
      left_mbmi = xd->mi[mi_col_offset + i * mi_stride];
      mi_step = mi_size_high[left_mbmi->sb_type[PLANE_TYPE_Y]];

      for (int ref = 0; ref < 1 + has_second_ref(left_mbmi); ++ref) {
        if (left_mbmi->ref_frame[ref] == ref_frame) {
          record_samples(left_mbmi, ref, pts, pts_inref, i, 1, 0, -1);
          pts += 2;
          pts_inref += 2;
          if (++np >= LEAST_SQUARES_SAMPLES_MAX) {
            return LEAST_SQUARES_SAMPLES_MAX;
          }
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-left block
  if (do_top_left && left_available && up_available) {
    const int mi_row_offset = -1;
    const int mi_col_offset = -1;
    int has_valid_top_left = 1;
    MB_MODE_INFO *top_left_mbmi =
        xd->mi[mi_col_offset + mi_row_offset * mi_stride];
    const int top_left_mi_col = (mi_col + mi_col_offset);
    if (is_sb_border && (top_left_mi_col % 2)) {
      mi_step = mi_size_wide[top_left_mbmi->sb_type[PLANE_TYPE_Y]];
      if (mi_step == 1) {
        // Block MI width is 1 and block is in odd column
        has_valid_top_left = 0;
      } else {
        int adjust_mi_col_offset = 0;
        if (top_left_mi_col > top_left_mbmi->mi_col_start) {
          adjust_mi_col_offset = top_left_mi_col - 1 - mi_col;
        } else {
          adjust_mi_col_offset = top_left_mi_col + 1 - mi_col;
        }

        top_left_mbmi =
            xd->mi[adjust_mi_col_offset + mi_row_offset * mi_stride];
      }
    }

    if (has_valid_top_left) {
      for (int ref = 0; ref < 1 + has_second_ref(top_left_mbmi); ++ref) {
        if (top_left_mbmi->ref_frame[ref] == ref_frame) {
          record_samples(top_left_mbmi, ref, pts, pts_inref, 0, -1, 0, -1);
          pts += 2;
          pts_inref += 2;
          if (++np >= LEAST_SQUARES_SAMPLES_MAX)
            return LEAST_SQUARES_SAMPLES_MAX;
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  // Top-right block
  if (do_top_right && has_top_right(cm, xd, mi_row, mi_col, xd->width)) {
    const POSITION top_right_block_pos = { -1, xd->width };

    if (is_inside(tile, mi_col, mi_row, &top_right_block_pos)) {
      const int mi_row_offset = -1;
      const int mi_col_offset = xd->width;
      int has_valid_top_right = 1;
      MB_MODE_INFO *top_right_mbmi =
          xd->mi[mi_col_offset + mi_row_offset * mi_stride];
      const int top_right_mi_col = (mi_col + mi_col_offset);
      if (is_sb_border && (top_right_mi_col % 2)) {
        mi_step = mi_size_wide[top_right_mbmi->sb_type[PLANE_TYPE_Y]];
        if (mi_step == 1) {
          // Block MI width is 1 and block is in odd column
          has_valid_top_right = 0;
        } else {
          int adjust_mi_col_offset = 0;
          if (top_right_mi_col > top_right_mbmi->mi_col_start) {
            adjust_mi_col_offset = top_right_mi_col - 1 - mi_col;
          } else {
            adjust_mi_col_offset = top_right_mi_col + 1 - mi_col;
          }

          const POSITION block_pos = { -1, adjust_mi_col_offset };
          if (is_inside(tile, mi_col, mi_row, &block_pos)) {
            top_right_mbmi =
                xd->mi[adjust_mi_col_offset + mi_row_offset * mi_stride];
          } else {
            has_valid_top_right = 0;
          }
        }
      }

      if (has_valid_top_right) {
        for (int ref = 0; ref < 1 + has_second_ref(top_right_mbmi); ++ref) {
          if (top_right_mbmi->ref_frame[ref] == ref_frame) {
            record_samples(top_right_mbmi, ref, pts, pts_inref, 0, -1,
                           xd->width, 1);
            pts += 2;
            pts_inref += 2;
            if (++np >= LEAST_SQUARES_SAMPLES_MAX) {
              return LEAST_SQUARES_SAMPLES_MAX;
            }
          }
        }
      }
    }
  }
  assert(np <= LEAST_SQUARES_SAMPLES_MAX);

  return np;
}

void av2_setup_skip_mode_allowed(AV2_COMMON *cm) {
  SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;

  skip_mode_info->skip_mode_allowed = 0;
  skip_mode_info->ref_frame_idx_0 = INVALID_IDX;
  skip_mode_info->ref_frame_idx_1 = INVALID_IDX;

  if (frame_is_intra_only(cm)
      // This line should be added, however it will have stats change, can be
      // enabled as bug fix if confirmed.
      //      || cm->current_frame.reference_mode == SINGLE_REFERENCE
      || frame_is_sframe(cm))
    return;

  skip_mode_info->skip_mode_allowed = 1;
  // NOTE: at the encoder side num_total_refs doesnot include
  // restricted reference
  if (cm->ref_frames_info.num_valid_refs_with_restricted_ref > 1) {
    skip_mode_info->ref_frame_idx_1 = 1;
    skip_mode_info->ref_frame_idx_0 = 0;
    const int cur_order_hint = cm->current_frame.display_order_hint;
    int ref_offset[2];
    get_skip_mode_ref_offsets(cm, ref_offset);
    const int cur_to_ref0 =
        (get_ref_frame_buf(cm, skip_mode_info->ref_frame_idx_0)->is_restricted)
            ? 0
            : abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                    cur_order_hint, ref_offset[0]));
    const int cur_to_ref1 =
        (get_ref_frame_buf(cm, skip_mode_info->ref_frame_idx_1)->is_restricted)
            ? 0
            : abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                    cur_order_hint, ref_offset[1]));
    if (abs(cur_to_ref0 - cur_to_ref1) > 1) {
      skip_mode_info->ref_frame_idx_0 = 0;
      skip_mode_info->ref_frame_idx_1 = 0;
    }
  } else {
    skip_mode_info->ref_frame_idx_1 = 0;
    skip_mode_info->ref_frame_idx_0 = 0;
  }
}

#define SB_TO_RMB_UNITS_LOG2 3
#define BANK_1ST_UNIT_UPDATE_COUNT 4
#define BANK_UNIT_MAX_ALLOWED_LEFTOVER_UPDATES 16

// Divide SB into 64 units, and each unit has one hit. Thus, in worst case,
// one SB has 64 hits as defined in MAX_RMB_SB_HITS.
// If SB size is 128x128, then each unit size is (128/8)x(128/8)=16x16
// If SB size is 256x256, then each unit size is (256/8)x(256/8)=32x32

void decide_rmb_unit_update_count(const AV2_COMMON *const cm,
                                  MACROBLOCKD *const xd,
                                  const MB_MODE_INFO *const mbmi) {
  if (!cm->seq_params.enable_refmvbank) return;
  if (xd->tree_type == CHROMA_PART) return;
  const int mi_sb_size = cm->mib_size;
  const int mi_sb_size_log2 = cm->mib_size_log2;
  const int mi_row_in_sb = xd->mi_row % mi_sb_size;
  const int mi_col_in_sb = xd->mi_col % mi_sb_size;
  const int rmb_unit_mi_size_log2 = mi_sb_size_log2 - SB_TO_RMB_UNITS_LOG2;
  const int rmb_unit_mi_size = (1 << rmb_unit_mi_size_log2);

  BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int mi_bw = mi_size_wide[bsize];
  const int mi_bh = mi_size_high[bsize];
  const int rmb_units_count = AVMMAX(mi_bw >> rmb_unit_mi_size_log2, 1) *
                              AVMMAX(mi_bh >> rmb_unit_mi_size_log2, 1);
  if (mi_row_in_sb == 0 && mi_col_in_sb == 0) {
    xd->ref_mv_bank.remain_hits =
        AVMMAX(rmb_units_count, BANK_1ST_UNIT_UPDATE_COUNT);
    xd->ref_mv_bank.rmb_unit_hits = 0;
  } else if (((mi_row_in_sb % rmb_unit_mi_size) == 0) &&
             ((mi_col_in_sb % rmb_unit_mi_size) == 0)) {
    xd->ref_mv_bank.remain_hits += rmb_units_count;
    xd->ref_mv_bank.rmb_unit_hits = 0;
  }
}

static INLINE void update_ref_mv_bank(const AV2_COMMON *const cm,
                                      MACROBLOCKD *const xd, int from_within_sb,
                                      const MB_MODE_INFO *const mbmi,
                                      REF_MV_BANK *ref_mv_bank) {
  if (from_within_sb) {
    decide_rmb_unit_update_count(cm, xd, mbmi);

    if (ref_mv_bank->remain_hits == 0 ||
        ref_mv_bank->rmb_unit_hits >= BANK_UNIT_MAX_ALLOWED_LEFTOVER_UPDATES ||
        ref_mv_bank->rmb_sb_hits >= MAX_RMB_SB_HITS) {
      return;
    }

    ref_mv_bank->remain_hits--;
    ref_mv_bank->rmb_unit_hits++;
  } else {
    // If max hits have been reached return.
    if (ref_mv_bank->rmb_sb_hits >= MAX_RMB_SB_HITS) return;
  }

  // else increment count and proceed with updating.
  ++ref_mv_bank->rmb_sb_hits;

  const MV_REFERENCE_FRAME ref_frame = av2_ref_frame_type(mbmi->ref_frame);
  const int rmb_list_index = get_rmb_list_index(ref_frame);
  CANDIDATE_MV *queue = ref_mv_bank->rmb_buffer[rmb_list_index];
  MV_REFERENCE_FRAME *rmb_ref_frame = ref_mv_bank->rmb_ref_frame;

  const int is_comp = has_second_ref(mbmi);
  const int start_idx = ref_mv_bank->rmb_start_idx[rmb_list_index];
  const int count = ref_mv_bank->rmb_count[rmb_list_index];
  int found = -1;

  // Check if current MV is already existing in the buffer.
  for (int i = 0; i < count; ++i) {
    const int idx = (start_idx + i) % REF_MV_BANK_SIZE;
    if ((rmb_list_index != REF_MV_BANK_LIST_FOR_ALL_OTHERS ||
         rmb_ref_frame[idx] == ref_frame) &&
        mbmi->mv[0].as_int == queue[idx].this_mv.as_int &&
        (!is_comp || mbmi->mv[1].as_int == queue[idx].comp_mv.as_int)) {
      found = i;
      break;
    }
  }

  // If current MV is found in the buffer, move it to the end of the buffer.
  if (found >= 0) {
    const int idx = (start_idx + found) % REF_MV_BANK_SIZE;
    const CANDIDATE_MV cand = queue[idx];
    for (int i = found; i < count - 1; ++i) {
      const int idx0 = (start_idx + i) % REF_MV_BANK_SIZE;
      const int idx1 = (start_idx + i + 1) % REF_MV_BANK_SIZE;
      queue[idx0] = queue[idx1];
      if (rmb_list_index == REF_MV_BANK_LIST_FOR_ALL_OTHERS) {
        rmb_ref_frame[idx0] = rmb_ref_frame[idx1];
      }
    }
    const int tail = (start_idx + count - 1) % REF_MV_BANK_SIZE;
    queue[tail] = cand;
    if (rmb_list_index == REF_MV_BANK_LIST_FOR_ALL_OTHERS) {
      rmb_ref_frame[tail] = ref_frame;
    }
    return;
  }

  // If current MV is not found in the buffer, append it to the end of the
  // buffer, and update the count and start_idx accordingly.
  const int idx = (start_idx + count) % REF_MV_BANK_SIZE;
  queue[idx].this_mv = mbmi->mv[0];
  if (is_comp) queue[idx].comp_mv = mbmi->mv[1];
  if (rmb_list_index == REF_MV_BANK_LIST_FOR_ALL_OTHERS) {
    rmb_ref_frame[idx] = ref_frame;
  }
  queue[idx].cwp_idx = mbmi->cwp_idx;
  if (count < REF_MV_BANK_SIZE) {
    ++ref_mv_bank->rmb_count[rmb_list_index];
  } else {
    ++ref_mv_bank->rmb_start_idx[rmb_list_index];
  }
}

void av2_update_ref_mv_bank(const AV2_COMMON *const cm, MACROBLOCKD *const xd,
                            int from_within_sb,
                            const MB_MODE_INFO *const mbmi) {
  update_ref_mv_bank(cm, xd, from_within_sb, mbmi, &xd->ref_mv_bank);
  (void)cm;
}

void assign_warpmv(const AV2_COMMON *cm, SUBMB_INFO **submi, BLOCK_SIZE bsize,
                   WarpedMotionParams *wm_params, int mi_row, int mi_col,
                   int ref) {
  assert(wm_params->invalid == 0);
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int p_x_mis = AVMMIN(bw, cm->mi_params.mi_cols - mi_col) * MI_SIZE;
  const int p_y_mis = AVMMIN(bh, cm->mi_params.mi_rows - mi_row) * MI_SIZE;
  const int p_row = mi_row * MI_SIZE;
  const int p_col = mi_col * MI_SIZE;
  const int mi_stride = cm->mi_params.mi_stride;
  for (int i = p_row; i < p_row + p_y_mis; i += 8) {
    for (int j = p_col; j < p_col + p_x_mis; j += 8) {
      const int32_t src_x = j + 4;
      const int32_t src_y = i + 4;

      const int32_t submv_x_hp = get_subblk_offset_x_hp(
          wm_params->wmmat, src_x, src_y, 1 << WARPEDMODEL_PREC_BITS);
      const int32_t submv_y_hp = get_subblk_offset_y_hp(
          wm_params->wmmat, src_x, src_y, 1 << WARPEDMODEL_PREC_BITS);

      int mi_y = (i - p_row) / MI_SIZE;
      int mi_x = (j - p_col) / MI_SIZE;
      const int mv_row =
          ROUND_POWER_OF_TWO_SIGNED(submv_y_hp, WARPEDMODEL_PREC_BITS - 3);
      const int mv_col =
          ROUND_POWER_OF_TWO_SIGNED(submv_x_hp, WARPEDMODEL_PREC_BITS - 3);
      submi[mi_y * mi_stride + mi_x]->mv[ref].as_mv.row =
          clamp(mv_row, MV_LOW + 1, MV_UPP - 1);
      submi[mi_y * mi_stride + mi_x]->mv[ref].as_mv.col =
          clamp(mv_col, MV_LOW + 1, MV_UPP - 1);

      span_submv(cm, (submi + mi_y * mi_stride + mi_x), mi_row + mi_y,
                 mi_col + mi_x, BLOCK_8X8, ref);
    }
  }
}

void span_submv(const AV2_COMMON *cm, SUBMB_INFO **submi, int mi_row,
                int mi_col, BLOCK_SIZE bsize, int ref) {
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int x_inside_boundary = AVMMIN(bw, cm->mi_params.mi_cols - mi_col);
  const int y_inside_boundary = AVMMIN(bh, cm->mi_params.mi_rows - mi_row);
  const int stride = cm->mi_params.mi_stride;
  for (int y = 0; y < y_inside_boundary; y++) {
    for (int x = 0; x < x_inside_boundary; x++) {
      if (x == 0 && y == 0) continue;
      submi[y * stride + x]->mv[ref] = submi[0]->mv[ref];
    }
  }
}

#define MAX_WARP_SB_HITS 64
// Update the warp parameter bank
//  If the warp parameters are already exist in the bank, then bank is
//  rearranged If the warp parameters are not in the bank, insert it to the
//  bank.
static INLINE void update_warp_param_bank(const MB_MODE_INFO *const mbmi,
#if COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                          int cand_from_sb_above,
#endif  // COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                          WARP_PARAM_BANK *warp_param_bank) {
#if COMPOUND_WARP_LINE_BUFFER_REDUCTION
  const int can_use_second_model =
      is_inter_compound_mode(mbmi->mode) && !cand_from_sb_above;
#else
  const int can_use_second_model = is_inter_compound_mode(mbmi->mode);
#endif  // COMPOUND_WARP_LINE_BUFFER_REDUCTION
  for (int ref_idx = 0; ref_idx < 1 + can_use_second_model; ref_idx++) {
    if (!mbmi->wm_params[ref_idx].invalid) {
      const MV_REFERENCE_FRAME ref_frame = mbmi->ref_frame[ref_idx];
      WarpedMotionParams *queue = warp_param_bank->wpb_buffer[ref_frame];
      const int start_idx = warp_param_bank->wpb_start_idx[ref_frame];
      const int count = warp_param_bank->wpb_count[ref_frame];
      int found = -1;

      // If max hits have been reached return.
      if (warp_param_bank->wpb_sb_hits >= MAX_WARP_SB_HITS) return;
      // else increment count and proceed with updating.
      ++warp_param_bank->wpb_sb_hits;

      // Check if current warp parameters is already existing in the buffer.
      for (int i = 0; i < count; ++i) {
        const int idx = (start_idx + i) % WARP_PARAM_BANK_SIZE;
        int same_param =
            (mbmi->wm_params[ref_idx].wmmat[2] == queue[idx].wmmat[2]);
        same_param &=
            (mbmi->wm_params[ref_idx].wmmat[3] == queue[idx].wmmat[3]);

        same_param &=
            (mbmi->wm_params[ref_idx].wmmat[4] == queue[idx].wmmat[4]);
        same_param &=
            (mbmi->wm_params[ref_idx].wmmat[5] == queue[idx].wmmat[5]);
        if (same_param) {
          found = i;
          break;
        }
      }

      // If current warp parameters is found in the buffer, move it to the end
      // of the buffer.
      if (found >= 0) {
        const int idx = (start_idx + found) % WARP_PARAM_BANK_SIZE;
        const WarpedMotionParams cand = queue[idx];
        for (int i = found; i < count - 1; ++i) {
          const int idx0 = (start_idx + i) % WARP_PARAM_BANK_SIZE;
          const int idx1 = (start_idx + i + 1) % WARP_PARAM_BANK_SIZE;
          queue[idx0] = queue[idx1];
        }
        const int tail = (start_idx + count - 1) % WARP_PARAM_BANK_SIZE;
        queue[tail] = cand;
        continue;
      }

      // If current warp parameter is not found in the buffer, append it to the
      // end of the buffer, and update the count and start_idx accordingly.
      const int idx = (start_idx + count) % WARP_PARAM_BANK_SIZE;
      queue[idx].wmtype = mbmi->wm_params[ref_idx].wmtype;
      queue[idx].wmmat[0] = mbmi->wm_params[ref_idx].wmmat[0];
      queue[idx].wmmat[1] = mbmi->wm_params[ref_idx].wmmat[1];
      queue[idx].wmmat[2] = mbmi->wm_params[ref_idx].wmmat[2];
      queue[idx].wmmat[3] = mbmi->wm_params[ref_idx].wmmat[3];
      queue[idx].wmmat[4] = mbmi->wm_params[ref_idx].wmmat[4];
      queue[idx].wmmat[5] = mbmi->wm_params[ref_idx].wmmat[5];

      if (count < WARP_PARAM_BANK_SIZE) {
        ++warp_param_bank->wpb_count[ref_frame];
      } else {
        ++warp_param_bank->wpb_start_idx[ref_frame];
      }
    }
  }
}
void av2_update_warp_param_bank(const AV2_COMMON *const cm,
                                MACROBLOCKD *const xd,
#if COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                int cand_from_sb_above,
#endif  // COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                const MB_MODE_INFO *const mbmi) {
  (void)cm;
  if (is_warp_mode(mbmi->motion_mode)) {
    update_warp_param_bank(mbmi,
#if COMPOUND_WARP_LINE_BUFFER_REDUCTION
                           cand_from_sb_above,
#endif  // COMPOUND_WARP_LINE_BUFFER_REDUCTION
                           &xd->warp_param_bank);
  }
}

// The wrl_list is the warp reference list which is already generated in the
// av2_find_mv_refs

// If the mode is not equal to the GLOBALMV mode, wrl_list is copied to the
// warp_param_stack
void av2_find_warp_delta_base_candidates(
    const MACROBLOCKD *xd, const MB_MODE_INFO *mbmi,
    WARP_CANDIDATE warp_param_stack[MAX_WARP_REF_CANDIDATES],
    WARP_CANDIDATE wrl_list[MAX_WARP_REF_CANDIDATES], uint8_t num_wrl_cand,
    uint8_t *p_valid_num_candidates) {
  // Global MV mode insert the global motion
  if (mbmi->mode == GLOBALMV) {
    warp_param_stack[0].wm_params = xd->global_motion[mbmi->ref_frame[0]];
    warp_param_stack[0].proj_type = PROJ_GLOBAL_MOTION;
    if (p_valid_num_candidates) {
      *p_valid_num_candidates = 1;
    }
    return;
  }

  // Copy the entire wrl_list where all candidates have been properly defined.
  // Note that the first num_wrl_cand have been filled, and the rest have been
  // initialized as default_warp_params.
  memcpy(&warp_param_stack[0], &wrl_list[0],
         MAX_WARP_REF_CANDIDATES * sizeof(wrl_list[0]));
  if (p_valid_num_candidates) {
    // for NEARMV mode, the maximum number of candidates is 1
    *p_valid_num_candidates = (mbmi->mode == NEARMV) ? 1 : num_wrl_cand;
  }
}

// check if the the derive MV is inside of frame boundary
// return false if the MV is outside of the frame boundary
bool is_warp_candidate_inside_of_frame(const AV2_COMMON *cm,
                                       const MACROBLOCKD *xd, int_mv cand_mv) {
  // Check if the MV candidate is pointing to ref block inside frame boundary.

  const int block_width = xd->width * MI_SIZE;
  const int block_height = xd->height * MI_SIZE;
  int frame_width = cm->width;
  int frame_height = cm->height;

  const int mv_row = (cand_mv.as_mv.row) / 8;
  const int mv_col = (cand_mv.as_mv.col) / 8;
  const int ref_x = xd->mi_col * MI_SIZE + mv_col;
  const int ref_y = xd->mi_row * MI_SIZE + mv_row;
  if (ref_x <= -block_width || ref_y <= -block_height || ref_x >= frame_width ||
      ref_y >= frame_height) {
    return false;
  }
  return true;
}

static int is_same_ref_frame(const MB_MODE_INFO *neighbor_mi,
                             const MB_MODE_INFO *mbmi) {
  return (is_inter_ref_frame(neighbor_mi->ref_frame[0]) &&
          neighbor_mi->ref_frame[0] == mbmi->ref_frame[0]) ||
         (is_inter_ref_frame(neighbor_mi->ref_frame[1]) &&
          neighbor_mi->ref_frame[1] == mbmi->ref_frame[0]);
}

static int is_same_ref_frame_for_this_ref_frame(
    const MB_MODE_INFO *neighbor_mi, const MV_REFERENCE_FRAME ref_frame) {
  return (is_inter_ref_frame(neighbor_mi->ref_frame[0]) &&
          neighbor_mi->ref_frame[0] == ref_frame) ||
         (is_inter_ref_frame(neighbor_mi->ref_frame[1]) &&
          neighbor_mi->ref_frame[1] == ref_frame);
}

int allow_extend_nb(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                    const MB_MODE_INFO *mbmi, int *p_num_of_warp_neighbors) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos;

  int allow_new_ext = 0;
  int allow_near_ext = 0;

  MVP_UNIT_STATUS row_smvp_state[4] = { 0 };
  get_row_smvp_states(cm, xd, row_smvp_state);

  // counter to count number of warp neighbors
  int num_of_warp_neighbors = 0;

  // left
  mi_pos.row = xd->height - 1;
  mi_pos.col = -1;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) && xd->left_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame(neighbor_mi, mbmi)) {
      allow_new_ext |= 1;
      allow_near_ext |= is_warp_mode(neighbor_mi->motion_mode);
      if (p_num_of_warp_neighbors && is_warp_mode(neighbor_mi->motion_mode))
        num_of_warp_neighbors++;
    }
  }

  // up
  mi_pos.row = row_smvp_state[0].row_offset;
  mi_pos.col = row_smvp_state[0].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      row_smvp_state[0].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame(neighbor_mi, mbmi)) {
      allow_new_ext |= 1;
      allow_near_ext |= is_warp_mode(neighbor_mi->motion_mode);
      if (p_num_of_warp_neighbors && is_warp_mode(neighbor_mi->motion_mode))
        num_of_warp_neighbors++;
    }
  }

  // left
  mi_pos.row = 0;
  mi_pos.col = -1;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) && xd->left_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame(neighbor_mi, mbmi)) {
      allow_new_ext |= 1;
      allow_near_ext |= is_warp_mode(neighbor_mi->motion_mode);
      if (p_num_of_warp_neighbors && is_warp_mode(neighbor_mi->motion_mode))
        num_of_warp_neighbors++;
    }
  }

  // up
  mi_pos.row = row_smvp_state[1].row_offset;
  mi_pos.col = row_smvp_state[1].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      row_smvp_state[1].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame(neighbor_mi, mbmi)) {
      allow_new_ext |= 1;
      allow_near_ext |= is_warp_mode(neighbor_mi->motion_mode);
      if (p_num_of_warp_neighbors && is_warp_mode(neighbor_mi->motion_mode))
        num_of_warp_neighbors++;
    }
  }

  if (p_num_of_warp_neighbors) {
    *p_num_of_warp_neighbors = num_of_warp_neighbors;
    return num_of_warp_neighbors;
  }

  if (mbmi->mode == NEWMV || mbmi->mode == WARP_NEWMV) {
    return allow_new_ext;
  } else if (mbmi->mode == NEARMV) {
    return allow_near_ext;
  } else {
    return 0;
  }
}

uint8_t av2_is_warp_causal_allowed(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                                   const MV_REFERENCE_FRAME ref_frame) {
  const TileInfo *const tile = &xd->tile;

  POSITION mi_pos;
  MVP_UNIT_STATUS row_smvp_state[4] = { 0 };
  get_row_smvp_states(cm, xd, row_smvp_state);

  // left
  mi_pos.row = xd->height - 1;
  mi_pos.col = -1;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) && xd->left_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame_for_this_ref_frame(neighbor_mi, ref_frame)) {
      return 1;
    }
  }

  // up
  mi_pos.row = row_smvp_state[0].row_offset;
  mi_pos.col = row_smvp_state[0].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      row_smvp_state[0].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame_for_this_ref_frame(neighbor_mi, ref_frame)) {
      return 1;
    }
  }

  // left
  mi_pos.row = 0;
  mi_pos.col = -1;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) && xd->left_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame_for_this_ref_frame(neighbor_mi, ref_frame)) {
      return 1;
    }
  }

  // up
  mi_pos.row = row_smvp_state[1].row_offset;
  mi_pos.col = row_smvp_state[1].col_offset;
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      row_smvp_state[1].is_available) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame_for_this_ref_frame(neighbor_mi, ref_frame)) {
      return 1;
    }
  }

  return 0;
}

static AVM_INLINE POSITION get_pos_from_pos_idx(const AV2_COMMON *cm,
                                                const MACROBLOCKD *xd,
                                                int pos_idx) {
  MVP_UNIT_STATUS row_smvp_state[4] = { 0 };
  if (pos_idx == 2 || pos_idx == 4 || pos_idx == 6 || pos_idx == 7) {
    get_row_smvp_states(cm, xd, row_smvp_state);
  }

  POSITION ret_pos = { 0, 0 };
  // 1/3/5 are left neighbors, 2/4/6/7 are above neighbors
  if (pos_idx == 5) {
    ret_pos.row = xd->height;
    ret_pos.col = -1;
  } else if (pos_idx == 1) {
    ret_pos.row = (xd->height - 1);
    ret_pos.col = -1;
  } else if (pos_idx == 3) {
    ret_pos.row = 0;
    ret_pos.col = -1;
  } else if (pos_idx == 7) {
    if (row_smvp_state[3].is_available) {
      ret_pos.row = -1;
      ret_pos.col = row_smvp_state[3].col_offset;
    }
  } else if (pos_idx == 4) {
    if (row_smvp_state[1].is_available) {
      ret_pos.row = -1;
      ret_pos.col = row_smvp_state[1].col_offset;
    }
  } else if (pos_idx == 2) {
    if (row_smvp_state[0].is_available) {
      ret_pos.row = -1;
      ret_pos.col = row_smvp_state[0].col_offset;
    }
  } else if (pos_idx == 6) {
    if (row_smvp_state[2].is_available) {
      ret_pos.row = -1;
      ret_pos.col = row_smvp_state[2].col_offset;
    }
  } else {
    assert(0);
  }

  return ret_pos;
}

static AVM_INLINE int get_cand_from_pos_idx(const AV2_COMMON *cm,
                                            const MACROBLOCKD *xd,
                                            int pos_idx) {
  MVP_UNIT_STATUS row_smvp_state[4] = { 0 };
  if (pos_idx == 2 || pos_idx == 4 || pos_idx == 6 || pos_idx == 7) {
    get_row_smvp_states(cm, xd, row_smvp_state);
  }

  const int has_bl =
      has_bottom_left(cm, xd, xd->mi_row, xd->mi_col, xd->height);

  int ret_cand = 0;

  // 1/3/5 are left neighbors, 2/4/6/7 are above neighbors
  if (pos_idx == 5) {
    ret_cand = has_bl;
  } else if (pos_idx == 1) {
    ret_cand = xd->left_available;
  } else if (pos_idx == 3) {
    ret_cand = xd->left_available;
  } else if (pos_idx == 7) {
    ret_cand = row_smvp_state[3].is_available;
  } else if (pos_idx == 4) {
    ret_cand = row_smvp_state[1].is_available;
  } else if (pos_idx == 2) {
    ret_cand = row_smvp_state[0].is_available;
  } else if (pos_idx == 6) {
    ret_cand = row_smvp_state[2].is_available;
  } else {
    assert(0);
  }

  return ret_cand;
}

static AVM_INLINE int check_pos_and_get_base_pos(const AV2_COMMON *cm,
                                                 const MACROBLOCKD *xd,
                                                 const MB_MODE_INFO *mbmi,
                                                 POSITION *base_pos,
                                                 int pos_idx) {
  const TileInfo *const tile = &xd->tile;
  POSITION mi_pos = get_pos_from_pos_idx(cm, xd, pos_idx);
  if (is_inside(tile, xd->mi_col, xd->mi_row, &mi_pos) &&
      get_cand_from_pos_idx(cm, xd, pos_idx)) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mi_pos.row * xd->mi_stride + mi_pos.col];
    if (is_same_ref_frame(neighbor_mi, mbmi)) {
      if ((is_warp_mode(neighbor_mi->motion_mode) && mbmi->mode == NEARMV) ||
          mbmi->mode == WARP_NEWMV || mbmi->mode == NEWMV) {
        base_pos->row = mi_pos.row;
        base_pos->col = mi_pos.col;
        return 1;
      }
    }
  }
  return 0;
}

int get_extend_base_pos(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                        const MB_MODE_INFO *mbmi, int mvp_row_offset,
                        int mvp_col_offset, POSITION *base_pos) {
  if (mvp_col_offset == -1 || mvp_row_offset == -1) {
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[mvp_row_offset * xd->mi_stride + mvp_col_offset];
    if (!is_tip_ref_frame(neighbor_mi->ref_frame[0])) {
      if ((is_warp_mode(neighbor_mi->motion_mode) && mbmi->mode == NEARMV) ||
          mbmi->mode == WARP_NEWMV || mbmi->mode == NEWMV) {
        base_pos->row = mvp_row_offset;
        base_pos->col = mvp_col_offset;
        return 1;
      }
    }
  }

  for (int pos_idx = 1; pos_idx <= 4; pos_idx++) {
    if (check_pos_and_get_base_pos(cm, xd, mbmi, base_pos, pos_idx)) return 1;
  }
  return 0;
}

int16_t inter_warpmv_mode_ctx(const AV2_COMMON *cm, const MACROBLOCKD *xd,
                              const MB_MODE_INFO *mbmi) {
  int num_of_warp_neighbors = 0;
  int ctx = allow_extend_nb(cm, xd, mbmi, &num_of_warp_neighbors);
  assert(num_of_warp_neighbors == ctx);
  assert(ctx < WARPMV_MODE_CONTEXT);
  return ctx;
}
