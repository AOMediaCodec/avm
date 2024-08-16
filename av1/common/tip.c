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

#include "av1/common/tip.h"
#include "config/aom_scale_rtcd.h"
#if CONFIG_OPTFLOW_ON_TIP
#include "config/aom_dsp_rtcd.h"
#endif  // CONFIG_OPTFLOW_ON_TIP
#if CONFIG_AFFINE_REFINEMENT || CONFIG_OPTFLOW_ON_TIP
#include "av1/common/reconinter.h"
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_OPTFLOW_ON_TIP

#if !CONFIG_TIP_REF_PRED_MERGING
// CHROMA_MI_SIZE is the block size in luma unit for Chroma TIP interpolation
#define CHROMA_MI_SIZE (TMVP_MI_SIZE)
#endif  // !CONFIG_TIP_REF_PRED_MERGING
// Maximum block size is allowed to combine the blocks with same MV
#define MAX_BLOCK_SIZE_WITH_SAME_MV \
  8  // Needs to be 8 when across scale
     // prediction is needed due to use of
     // superres or resize. A higher value
     // such as 128 could be used if
     // across scale prediction is not
     // invoked.
// Percentage threshold of number of blocks with available motion
// projection in a frame to allow TIP mode
#define TIP_ENABLE_COUNT_THRESHOLD 60

static void tip_find_closest_bi_dir_ref_frames(AV1_COMMON *cm,
                                               int ref_order_hints[2],
                                               MV_REFERENCE_FRAME rf[2]) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;

  if (!order_hint_info->enable_order_hint || frame_is_intra_only(cm)) return;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int cur_order_hint = cm->current_frame.display_order_hint;
#else
  const int cur_order_hint = cm->current_frame.order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC

  // Identify the nearest forward and backward references.
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, i);
    if (buf == NULL) continue;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    const int ref_order_hint = buf->display_order_hint;
#else
    const int ref_order_hint = buf->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    const int ref_to_cur_dist =
        get_relative_dist(order_hint_info, ref_order_hint, cur_order_hint);
    if (ref_to_cur_dist < 0) {
      // Forward reference
      if (ref_order_hints[0] == -1 ||
          get_relative_dist(order_hint_info, ref_order_hint,
                            ref_order_hints[0]) > 0) {
        ref_order_hints[0] = ref_order_hint;
        rf[0] = i;
      }
    } else if (ref_to_cur_dist > 0) {
      // Backward reference
      if (ref_order_hints[1] == INT_MAX ||
          get_relative_dist(order_hint_info, ref_order_hint,
                            ref_order_hints[1]) < 0) {
        ref_order_hints[1] = ref_order_hint;
        rf[1] = i;
      }
    }
  }
}

static AOM_INLINE int tip_find_reference_frame(AV1_COMMON *cm, int start_frame,
                                               int target_frame_order) {
  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int *const ref_order_hints =
      &start_frame_buf->ref_display_order_hint[0];
#else
  const int *const ref_order_hints = &start_frame_buf->ref_order_hints[0];
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  for (MV_REFERENCE_FRAME rf = 0; rf < INTER_REFS_PER_FRAME; ++rf) {
    if (ref_order_hints[rf] == target_frame_order) {
      return 1;
    }
  }

  return 0;
}

static int tip_motion_field_projection(AV1_COMMON *cm,
                                       MV_REFERENCE_FRAME nearest_ref[2],
                                       int nearest_ref_order_hint[2]) {
  int ref_frame_offset = 0;
  int target_order_hint = 0;
  OrderHintInfo *order_hint_info = &cm->seq_params.order_hint_info;

  MV_REFERENCE_FRAME start_frame = NONE_FRAME;
  int find_ref =
      tip_find_reference_frame(cm, nearest_ref[0], nearest_ref_order_hint[1]);
  if (find_ref) {
    ref_frame_offset = get_relative_dist(
        order_hint_info, nearest_ref_order_hint[0], nearest_ref_order_hint[1]);
    start_frame = nearest_ref[0];
    target_order_hint = nearest_ref_order_hint[1];
  } else {
    find_ref =
        tip_find_reference_frame(cm, nearest_ref[1], nearest_ref_order_hint[0]);
    if (!find_ref) return 0;
    ref_frame_offset = get_relative_dist(
        order_hint_info, nearest_ref_order_hint[1], nearest_ref_order_hint[0]);
    start_frame = nearest_ref[1];
    target_order_hint = nearest_ref_order_hint[0];
  }

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  if (!is_ref_motion_field_eligible(cm, start_frame_buf)) return 0;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int start_frame_order_hint = start_frame_buf->display_order_hint;
#else
  const int start_frame_order_hint = start_frame_buf->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC

  assert(start_frame_buf->width == cm->width &&
         start_frame_buf->height == cm->height);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int *const ref_order_hints = start_frame_buf->ref_display_order_hint;
  const int cur_order_hint = cm->cur_frame->display_order_hint;
#else
  const int *const ref_order_hints = start_frame_buf->ref_order_hints;
  const int cur_order_hint = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  int start_to_current_frame_offset = get_relative_dist(
      order_hint_info, start_frame_order_hint, cur_order_hint);

  const int is_backward = ref_frame_offset < 0;
  if (is_backward) {
    ref_frame_offset = -ref_frame_offset;
    start_to_current_frame_offset = -start_to_current_frame_offset;
  }

  const int temporal_scale_factor =
      tip_derive_scale_factor(start_to_current_frame_offset, ref_frame_offset);

  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const MV_REF *mv_ref_base = start_frame_buf->mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;
  for (int blk_row = 0; blk_row < mvs_rows; ++blk_row) {
    for (int blk_col = 0; blk_col < mvs_cols; ++blk_col) {
      const MV_REF *mv_ref = &mv_ref_base[blk_row * mvs_stride + blk_col];
      MV_REFERENCE_FRAME ref_frame[2] = { mv_ref->ref_frame[0],
                                          mv_ref->ref_frame[1] };
      for (int idx = 0; idx < 2; ++idx) {
        if (is_inter_ref_frame(ref_frame[idx])) {
          const int ref_frame_order_hint = ref_order_hints[ref_frame[idx]];
          if (ref_frame_order_hint == target_order_hint) {
            MV ref_mv = mv_ref->mv[idx].as_mv;
            int_mv this_mv;
            int mi_r = 0;
            int mi_c = 0;
            tip_get_mv_projection(&this_mv.as_mv, ref_mv,
                                  temporal_scale_factor);
            const int pos_valid = get_block_position(cm, &mi_r, &mi_c, blk_row,
                                                     blk_col, this_mv.as_mv, 0);
            if (pos_valid) {
              if (is_backward) {
                ref_mv.row = -ref_mv.row;
                ref_mv.col = -ref_mv.col;
              }

              const int mi_offset = mi_r * mvs_stride + mi_c;
              if (tpl_mvs_base[mi_offset].mfmv0.as_int == INVALID_MV) {
                tpl_mvs_base[mi_offset].mfmv0.as_mv.row = ref_mv.row;
                tpl_mvs_base[mi_offset].mfmv0.as_mv.col = ref_mv.col;
                tpl_mvs_base[mi_offset].ref_frame_offset = ref_frame_offset;
              }
            }
          }
        }
      }
    }
  }

  return 1;
}

void av1_derive_tip_nearest_ref_frames_motion_projection(AV1_COMMON *cm) {
  int nearest_ref_order_hints[2] = { -1, INT_MAX };
  MV_REFERENCE_FRAME nearest_rf[2] = { NONE_FRAME, NONE_FRAME };
  tip_find_closest_bi_dir_ref_frames(cm, nearest_ref_order_hints, nearest_rf);
  if (nearest_rf[0] != NONE_FRAME && nearest_rf[1] != NONE_FRAME) {
    cm->tip_ref.ref_frame[0] = nearest_rf[0];
    cm->tip_ref.ref_frame[1] = nearest_rf[1];
    tip_motion_field_projection(cm, nearest_rf, nearest_ref_order_hints);
  } else {
    cm->tip_ref.ref_frame[0] = NONE_FRAME;
    cm->tip_ref.ref_frame[1] = NONE_FRAME;
  }
}

static void tip_temporal_scale_motion_field(AV1_COMMON *cm,
                                            const int ref_frames_offset) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;
  for (int blk_row = 0; blk_row < mvs_rows; ++blk_row) {
    for (int blk_col = 0; blk_col < mvs_cols; ++blk_col) {
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;
      if (tpl_mvs->mfmv0.as_int != INVALID_MV) {
        int_mv this_refmv;
        get_mv_projection(&this_refmv.as_mv, tpl_mvs->mfmv0.as_mv,
                          ref_frames_offset, tpl_mvs->ref_frame_offset);
        tpl_mvs->mfmv0.as_int = this_refmv.as_int;
        tpl_mvs->ref_frame_offset = ref_frames_offset;
      }
#if CONFIG_MF_HOLE_FILL_SIMPLIFY
      else {
        tpl_mvs->ref_frame_offset = ref_frames_offset;
      }
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY
    }
  }
}

#if CONFIG_MF_HOLE_FILL_SIMPLIFY
// Compute number of rows/columns/stride in TMVP unit (8x8)
static void compute_tmvp_mi_info(const AV1_COMMON *cm, int *mvs_rows,
                                 int *mvs_cols, int *mvs_stride,
                                 int *sb_tmvp_size) {
  *mvs_rows = ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  *mvs_cols = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  *mvs_stride = *mvs_cols;

  const SequenceHeader *const seq_params = &cm->seq_params;
  const int mf_sb_size_log2 =
      AOMMIN(seq_params->mib_size_log2, mi_size_high_log2[BLOCK_128X128]) +
      MI_SIZE_LOG2;
  const int mf_sb_size = (1 << mf_sb_size_log2);
  *sb_tmvp_size = (mf_sb_size >> TMVP_MI_SZ_LOG2);
}
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY

#if CONFIG_MF_HOLE_FILL_SIMPLIFY
static void tip_fill_motion_field_holes(AV1_COMMON *cm) {
  int mvs_rows = 0;
  int mvs_cols = 0;
  int mvs_stride = 0;
  int sb_tmvp_size = 0;
#if CONFIG_TMVP_MEM_OPT
  int sample = cm->tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

  compute_tmvp_mi_info(cm, &mvs_rows, &mvs_cols, &mvs_stride, &sb_tmvp_size);

  const int dirs[4][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  for (int sb_row = 0; sb_row < mvs_rows; sb_row += sb_tmvp_size) {
    const int start_row = sb_row;
    const int end_row = AOMMIN(sb_row + sb_tmvp_size, mvs_rows);
    for (int sb_col = 0; sb_col < mvs_cols; sb_col += sb_tmvp_size) {
      const int start_col = sb_col;
      const int end_col = AOMMIN(sb_col + sb_tmvp_size, mvs_cols);
#if CONFIG_TMVP_MEM_OPT
      for (int row = start_row; row < end_row; row += sample) {
        for (int col = start_col; col < end_col; col += sample) {
#else
      for (int row = start_row; row < end_row; row++) {
        for (int col = start_col; col < end_col; col++) {
#endif  // CONFIG_TMVP_MEM_OPT
          const int cur_tpl_offset = row * mvs_stride + col;
          const TPL_MV_REF *cur_tpl_mvs = tpl_mvs_base + cur_tpl_offset;
          if (cur_tpl_mvs->mfmv0.as_int != INVALID_MV) {
            for (int dir = 0; dir < 4; ++dir) {
#if CONFIG_TMVP_MEM_OPT
              const int neighbor_row = row + dirs[dir][0] * sample;
              const int neighbor_col = col + dirs[dir][1] * sample;
#else
              const int neighbor_row = row + dirs[dir][0];
              const int neighbor_col = col + dirs[dir][1];
#endif  // CONFIG_TMVP_MEM_OPT
              const int neighbor_tpl_offset =
                  neighbor_row * mvs_stride + neighbor_col;
              if (neighbor_row >= start_row && neighbor_row < end_row &&
                  neighbor_col >= start_col && neighbor_col < end_col &&
                  tpl_mvs_base[neighbor_tpl_offset].mfmv0.as_int ==
                      INVALID_MV) {
                tpl_mvs_base[neighbor_tpl_offset].mfmv0.as_int =
                    cur_tpl_mvs->mfmv0.as_int;
                tpl_mvs_base[neighbor_tpl_offset].ref_frame_offset =
                    cur_tpl_mvs->ref_frame_offset;
              }
            }
          }
        }
      }
    }
  }
}
#else
static void tip_fill_motion_field_holes(AV1_COMMON *cm) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;
  const int total_units = mvs_rows * mvs_cols;
#if CONFIG_TMVP_MEM_OPT
  int sample = cm->tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

  MV_REF *tmvp_mvs = cm->cur_frame->mvs;
  int write_pos = 0;
#if CONFIG_TMVP_MEM_OPT
  for (int blk_row = 0; blk_row < mvs_rows; blk_row += sample) {
    for (int blk_col = 0; blk_col < mvs_cols; blk_col += sample) {
#else
  for (int blk_row = 0; blk_row < mvs_rows; ++blk_row) {
    for (int blk_col = 0; blk_col < mvs_cols; ++blk_col) {
#endif  // CONFIG_TMVP_MEM_OPT
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;
      if (tpl_mvs->mfmv0.as_int != INVALID_MV) {
        tmvp_mvs[write_pos].mv[0].as_mv.row = blk_row;
        tmvp_mvs[write_pos].mv[0].as_mv.col = blk_col;
        write_pos++;
      }
    }
  }

#define ITER_DIR 4
  const int dirs[ITER_DIR][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };

  int read_pos = 0;
  while (read_pos < write_pos && write_pos < total_units) {
    const int start = read_pos;
    const int end = write_pos;
    for (int i = start; i < end; ++i) {
      const int cur_row = tmvp_mvs[i].mv[0].as_mv.row;
      const int cur_col = tmvp_mvs[i].mv[0].as_mv.col;
      const int cur_tpl_offset = cur_row * mvs_stride + cur_col;
      for (int dir = 0; dir < ITER_DIR; ++dir) {
#if CONFIG_TMVP_MEM_OPT
        const int next_row = cur_row + dirs[dir][0] * sample;
        const int next_col = cur_col + dirs[dir][1] * sample;
#else
        const int next_row = cur_row + dirs[dir][0];
        const int next_col = cur_col + dirs[dir][1];
#endif  // CONFIG_TMVP_MEM_OPT
        const int next_tpl_offset = next_row * mvs_stride + next_col;
        if (next_row < 0 || next_row >= mvs_rows || next_col < 0 ||
            next_col >= mvs_cols ||
            tpl_mvs_base[next_tpl_offset].mfmv0.as_int != INVALID_MV) {
          continue;
        }
        tpl_mvs_base[next_tpl_offset].mfmv0.as_int =
            tpl_mvs_base[cur_tpl_offset].mfmv0.as_int;
        tpl_mvs_base[next_tpl_offset].ref_frame_offset =
            tpl_mvs_base[cur_tpl_offset].ref_frame_offset;
        tmvp_mvs[write_pos].mv[0].as_mv.row = next_row;
        tmvp_mvs[write_pos].mv[0].as_mv.col = next_col;
        write_pos++;
      }
    }
    read_pos = end;
  }
}
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY

#if CONFIG_MF_HOLE_FILL_SIMPLIFY
static void tip_blk_average_filter_mv(AV1_COMMON *cm) {
  int mvs_rows = 0;
  int mvs_cols = 0;
  int mvs_stride = 0;
  int sb_tmvp_size = 0;
#if CONFIG_TMVP_MEM_OPT
  int sample = cm->tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

  compute_tmvp_mi_info(cm, &mvs_rows, &mvs_cols, &mvs_stride, &sb_tmvp_size);

#define DIV_SHIFT_BITS 16
  // Avoid the division operation. Maximum 5 MVs would be used for MV smoothing.
  // weight_div_mult[i] where i is the number of MVs are available.
  // The value of weight_div_mult[i] is to implement 1/i without division with
  // the shift of (weight_div_mult[i] >> 16)
  const int weight_div_mult[6] = { 0, 65536, 32768, 21845, 16384, 13107 };
  const int sb_stride = MAX_SB_TMVP_SIZE;
  int_mv tpl_mfs[MAX_SB_TMVP_SIZE * MAX_SB_TMVP_SIZE];
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  for (int sb_row = 0; sb_row < mvs_rows; sb_row += sb_tmvp_size) {
    const int start_row = sb_row;
    const int end_row = AOMMIN(sb_row + sb_tmvp_size, mvs_rows);
    for (int sb_col = 0; sb_col < mvs_cols; sb_col += sb_tmvp_size) {
      const int start_col = sb_col;
      const int end_col = AOMMIN(sb_col + sb_tmvp_size, mvs_cols);
#if CONFIG_TMVP_MEM_OPT
      for (int row = start_row; row < end_row; row += sample) {
        const int i0 = row - sample;
        const int i1 = row + sample;
        for (int col = start_col; col < end_col; col += sample) {
          const int j0 = col - sample;
          const int j1 = col + sample;
#else
      for (int row = start_row; row < end_row; row++) {
        const int i0 = row - 1;
        const int i1 = row + 1;
        for (int col = start_col; col < end_col; col++) {
          const int j0 = col - 1;
          const int j1 = col + 1;
#endif  // CONFIG_TMVP_MEM_OPT
          int weights = 0;
          int sum_mv_row = 0;
          int sum_mv_col = 0;
          const int cur_pos = row * mvs_stride + col;
          if (tpl_mvs_base[cur_pos].mfmv0.as_int != INVALID_MV) {
            weights++;
            sum_mv_row += tpl_mvs_base[cur_pos].mfmv0.as_mv.row;
            sum_mv_col += tpl_mvs_base[cur_pos].mfmv0.as_mv.col;
          }

          if (i0 >= start_row) {
            const int top_pos = i0 * mvs_stride + col;
            if (tpl_mvs_base[top_pos].mfmv0.as_int != INVALID_MV) {
              weights++;
              sum_mv_row += tpl_mvs_base[top_pos].mfmv0.as_mv.row;
              sum_mv_col += tpl_mvs_base[top_pos].mfmv0.as_mv.col;
            }
          }

          if (i1 < end_row) {
            const int bottom_pos = i1 * mvs_stride + col;
            if (tpl_mvs_base[bottom_pos].mfmv0.as_int != INVALID_MV) {
              weights++;
              sum_mv_row += tpl_mvs_base[bottom_pos].mfmv0.as_mv.row;
              sum_mv_col += tpl_mvs_base[bottom_pos].mfmv0.as_mv.col;
            }
          }

          if (j0 >= start_col) {
            const int left_pos = row * mvs_stride + j0;
            if (tpl_mvs_base[left_pos].mfmv0.as_int != INVALID_MV) {
              weights++;
              sum_mv_row += tpl_mvs_base[left_pos].mfmv0.as_mv.row;
              sum_mv_col += tpl_mvs_base[left_pos].mfmv0.as_mv.col;
            }
          }

          if (j1 < end_col) {
            const int right_pos = row * mvs_stride + j1;
            if (tpl_mvs_base[right_pos].mfmv0.as_int != INVALID_MV) {
              weights++;
              sum_mv_row += tpl_mvs_base[right_pos].mfmv0.as_mv.row;
              sum_mv_col += tpl_mvs_base[right_pos].mfmv0.as_mv.col;
            }
          }

          const int blk_pos_in_sb = (row - sb_row) * sb_stride + (col - sb_col);
          if (weights) {
            const int scale_factor = weight_div_mult[weights];
            tpl_mfs[blk_pos_in_sb].as_mv.row =
                (int16_t)ROUND_POWER_OF_TWO_SIGNED(sum_mv_row * scale_factor,
                                                   DIV_SHIFT_BITS);
            tpl_mfs[blk_pos_in_sb].as_mv.col =
                (int16_t)ROUND_POWER_OF_TWO_SIGNED(sum_mv_col * scale_factor,
                                                   DIV_SHIFT_BITS);
          } else {
            tpl_mfs[blk_pos_in_sb].as_int = INVALID_MV;
          }
        }
      }

#if CONFIG_TMVP_MEM_OPT
      for (int row = start_row; row < end_row; row += sample) {
        for (int col = start_col; col < end_col; col += sample) {
#else
      for (int row = start_row; row < end_row; row++) {
        for (int col = start_col; col < end_col; col++) {
#endif  // CONFIG_TMVP_MEM_OPT
          const int tpl_offset = row * mvs_stride + col;
          const int blk_pos_in_sb = (row - sb_row) * sb_stride + (col - sb_col);
          tpl_mvs_base[tpl_offset].mfmv0.as_int = tpl_mfs[blk_pos_in_sb].as_int;
        }
      }
    }
  }
}
#else
static void tip_blk_average_filter_mv(AV1_COMMON *cm) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;
#if CONFIG_TMVP_MEM_OPT
  int sample = cm->tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

  MV_REF *avg_mvs = cm->cur_frame->mvs;
#if CONFIG_TMVP_MEM_OPT
  for (int i = 0; i < mvs_rows; i += sample) {
    const int i0 = i - sample;
    const int i1 = i + sample;
    for (int j = 0; j < mvs_cols; j += sample) {
      const int j0 = j - sample;
      const int j1 = j + sample;
#else
  for (int i = 0; i < mvs_rows; i++) {
    const int i0 = i - 1;
    const int i1 = i + 1;
    for (int j = 0; j < mvs_cols; j++) {
      const int j0 = j - 1;
      const int j1 = j + 1;
#endif  // CONFIG_TMVP_MEM_OPT
      int count = 1;
      MV this_mv;
      const int cur_pos = i * mvs_stride + j;
      this_mv.row = tpl_mvs_base[cur_pos].mfmv0.as_mv.row;
      this_mv.col = tpl_mvs_base[cur_pos].mfmv0.as_mv.col;
      if (i0 >= 0) {
        count++;
        const int top_pos = i0 * mvs_stride + j;
        this_mv.row += tpl_mvs_base[top_pos].mfmv0.as_mv.row;
        this_mv.col += tpl_mvs_base[top_pos].mfmv0.as_mv.col;
      }

      if (i1 < mvs_rows) {
        count++;
        const int bottom_pos = i1 * mvs_stride + j;
        this_mv.row += tpl_mvs_base[bottom_pos].mfmv0.as_mv.row;
        this_mv.col += tpl_mvs_base[bottom_pos].mfmv0.as_mv.col;
      }

      if (j0 >= 0) {
        count++;
        const int left_pos = i * mvs_stride + j0;
        this_mv.row += tpl_mvs_base[left_pos].mfmv0.as_mv.row;
        this_mv.col += tpl_mvs_base[left_pos].mfmv0.as_mv.col;
      }

      if (j1 < mvs_cols) {
        count++;
        const int right_pos = i * mvs_stride + j1;
        this_mv.row += tpl_mvs_base[right_pos].mfmv0.as_mv.row;
        this_mv.col += tpl_mvs_base[right_pos].mfmv0.as_mv.col;
      }

      avg_mvs[cur_pos].mv[0].as_mv.row = this_mv.row / count;
      avg_mvs[cur_pos].mv[0].as_mv.col = this_mv.col / count;
    }
  }

#if CONFIG_TMVP_MEM_OPT
  for (int i = 0; i < mvs_rows; i += sample) {
    for (int j = 0; j < mvs_cols; j += sample) {
#else
  for (int i = 0; i < mvs_rows; i++) {
    for (int j = 0; j < mvs_cols; j++) {
#endif  // CONFIG_TMVP_MEM_OPT
      const int tpl_offset = i * mvs_stride + j;
      tpl_mvs_base[tpl_offset].mfmv0.as_int = avg_mvs[tpl_offset].mv[0].as_int;
    }
  }
}
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY

static INLINE MV tip_clamp_tip_mv_to_umv_border_sb(
    const MV *src_mv, int bw, int bh, int ss_x, int ss_y, int dist_to_left_edge,
    int dist_to_right_edge, int dist_to_top_edge, int dist_to_bottom_edge) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (AOM_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (AOM_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
  MV clamped_mv = { (int16_t)(src_mv->row * (1 << (1 - ss_y))),
                    (int16_t)(src_mv->col * (1 << (1 - ss_x))) };
  assert(ss_x <= 1);
  assert(ss_y <= 1);
  const SubpelMvLimits mv_limits = {
    dist_to_left_edge * (1 << (1 - ss_x)) - spel_left,
    dist_to_right_edge * (1 << (1 - ss_x)) + spel_right,
    dist_to_top_edge * (1 << (1 - ss_y)) - spel_top,
    dist_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom
  };

  clamp_mv(&clamped_mv, &mv_limits);

  return clamped_mv;
}

static INLINE int tip_check_motion_field(AV1_COMMON *cm, const MV *mv, int mi_x,
                                         int mi_y, int bw, int bh, int frame_w,
                                         int frame_h) {
  TIP *tip_ref = &cm->tip_ref;
  MV this_mv[2];
  tip_get_mv_projection(&this_mv[0], *mv, tip_ref->ref_frames_offset_sf[0]);
  tip_get_mv_projection(&this_mv[1], *mv, tip_ref->ref_frames_offset_sf[1]);

  const int dist_to_top_edge = -GET_MV_SUBPEL(mi_y);
  const int dist_to_bottom_edge = GET_MV_SUBPEL(frame_h - bh - mi_y);
  const int dist_to_left_edge = -GET_MV_SUBPEL(mi_x);
  const int dist_to_right_edge = GET_MV_SUBPEL(frame_w - bw - mi_x);

  MV temp_mv;
  temp_mv = tip_clamp_tip_mv_to_umv_border_sb(
      &this_mv[0], bw, bh, 0, 0, dist_to_left_edge, dist_to_right_edge,
      dist_to_top_edge, dist_to_bottom_edge);
  if (temp_mv.row != this_mv[0].row || temp_mv.col != this_mv[0].col) {
    return 1;
  }

  temp_mv = tip_clamp_tip_mv_to_umv_border_sb(
      &this_mv[1], bw, bh, 0, 0, dist_to_left_edge, dist_to_right_edge,
      dist_to_top_edge, dist_to_bottom_edge);
  if (temp_mv.row != this_mv[1].row || temp_mv.col != this_mv[1].col) {
    return 1;
  }

  return 0;
}

static void tip_motion_field_within_frame(AV1_COMMON *cm) {
  TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;
  assert(mvs_rows * mvs_stride <= cm->tpl_mvs_mem_size);
  av1_zero_array(cm->tip_ref.mf_need_clamp, mvs_rows * mvs_stride);

  const int width = (mvs_cols << TMVP_MI_SZ_LOG2);
  const int height = (mvs_rows << TMVP_MI_SZ_LOG2);

  int *mf_need_clamp = cm->tip_ref.mf_need_clamp;
  for (int i = 0; i < mvs_rows; i++) {
    for (int j = 0; j < mvs_cols; j++) {
      const int cur_pos = i * mvs_stride + j;
      if (tpl_mvs_base[cur_pos].mfmv0.as_int != INVALID_MV &&
          tpl_mvs_base[cur_pos].mfmv0.as_int != 0) {
        MV this_mv;
        this_mv.row = tpl_mvs_base[cur_pos].mfmv0.as_mv.row;
        this_mv.col = tpl_mvs_base[cur_pos].mfmv0.as_mv.col;

        const int tpl_row = i << TMVP_MI_SZ_LOG2;
        const int tpl_col = j << TMVP_MI_SZ_LOG2;

        mf_need_clamp[cur_pos] =
            tip_check_motion_field(cm, &this_mv, tpl_col, tpl_row, TMVP_MI_SIZE,
                                   TMVP_MI_SIZE, width, height);
      } else {
#if CONFIG_MF_HOLE_FILL_SIMPLIFY
        mf_need_clamp[cur_pos] = 0;
#else
        tpl_mvs_base[cur_pos].mfmv0.as_int = 0;
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY
      }
    }
  }
}

static void tip_check_enable_tip_mode(AV1_COMMON *cm) {
  const TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;

#if CONFIG_TMVP_MEM_OPT
  const int sample = cm->tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

  int count = 0;
#if CONFIG_TMVP_MEM_OPT
  for (int blk_row = 0; blk_row < mvs_rows; blk_row += sample) {
    for (int blk_col = 0; blk_col < mvs_cols; blk_col += sample) {
#else
  for (int blk_row = 0; blk_row < mvs_rows; ++blk_row) {
    for (int blk_col = 0; blk_col < mvs_cols; ++blk_col) {
#endif  // CONFIG_TMVP_MEM_OPT
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      const TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;
      if (tpl_mvs->mfmv0.as_int != INVALID_MV) {
        ++count;
      }
    }
  }

#if CONFIG_TMVP_MEM_OPT
  count *= sample * sample;
#endif  // CONFIG_TMVP_MEM_OPT

  // Percentage of number of blocks with available motion field
  const int percent = (count * 100) / (mvs_rows * mvs_cols);
  if (percent < TIP_ENABLE_COUNT_THRESHOLD) {
    cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
  } else {
    cm->features.tip_frame_mode = TIP_FRAME_AS_REF;
  }
}

static void tip_config_tip_parameter(AV1_COMMON *cm, int check_tip_threshold) {
  TIP *tip_ref = &cm->tip_ref;
  if (cm->current_frame.frame_type == KEY_FRAME ||
      cm->current_frame.frame_type == INTRA_ONLY_FRAME ||
      cm->current_frame.frame_type == S_FRAME) {
    cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
    tip_ref->ref_frame[0] = NONE_FRAME;
    tip_ref->ref_frame[1] = NONE_FRAME;
    return;
  }

  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int cur_order_hint = cm->cur_frame->display_order_hint;
#else
  const int cur_order_hint = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC

  MV_REFERENCE_FRAME nearest_rf[2] = { tip_ref->ref_frame[0],
                                       tip_ref->ref_frame[1] };

  if (nearest_rf[0] != NONE_FRAME && nearest_rf[1] != NONE_FRAME &&
      (is_ref_motion_field_eligible(cm, get_ref_frame_buf(cm, nearest_rf[0])) ||
       is_ref_motion_field_eligible(cm,
                                    get_ref_frame_buf(cm, nearest_rf[1])))) {
    if (check_tip_threshold) {
      tip_check_enable_tip_mode(cm);
    }

    if (cm->features.tip_frame_mode) {
      cm->features.allow_tip_hole_fill = cm->seq_params.enable_tip_hole_fill;
      RefCntBuffer *ref0_frame_buf = get_ref_frame_buf(cm, nearest_rf[0]);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int ref0_frame_order_hint = ref0_frame_buf->display_order_hint;
#else
      const int ref0_frame_order_hint = ref0_frame_buf->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int cur_to_ref0_offset = get_relative_dist(
          order_hint_info, cur_order_hint, ref0_frame_order_hint);

      RefCntBuffer *ref1_frame_buf = get_ref_frame_buf(cm, nearest_rf[1]);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int ref1_frame_order_hint = ref1_frame_buf->display_order_hint;
#else
      const int ref1_frame_order_hint = ref1_frame_buf->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int cur_to_ref1_offset = get_relative_dist(
          order_hint_info, cur_order_hint, ref1_frame_order_hint);

      const int ref_frames_offset = get_relative_dist(
          order_hint_info, ref1_frame_order_hint, ref0_frame_order_hint);
      tip_ref->ref_frame_buffer[0] = ref0_frame_buf;
      tip_ref->ref_frame_buffer[1] = ref1_frame_buf;
      tip_ref->ref_scale_factor[0] =
          get_ref_scale_factors_const(cm, nearest_rf[0]);
      tip_ref->ref_scale_factor[1] =
          get_ref_scale_factors_const(cm, nearest_rf[1]);
      tip_ref->ref_frames_offset_sf[0] =
          tip_derive_scale_factor(cur_to_ref0_offset, ref_frames_offset);
      tip_ref->ref_frames_offset_sf[1] =
          tip_derive_scale_factor(cur_to_ref1_offset, ref_frames_offset);
      tip_ref->ref_frames_offset = ref_frames_offset;
      tip_ref->ref_offset[0] = cur_to_ref0_offset;
      tip_ref->ref_offset[1] = cur_to_ref1_offset;
      tip_ref->ref_order_hint[0] = ref0_frame_order_hint;
      tip_ref->ref_order_hint[1] = ref1_frame_order_hint;
    }
  } else {
    cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
    cm->features.allow_tip_hole_fill = false;
    tip_ref->ref_frame[0] = NONE_FRAME;
    tip_ref->ref_frame[1] = NONE_FRAME;
  }
}

void av1_setup_tip_motion_field(AV1_COMMON *cm, int check_tip_threshold) {
  tip_config_tip_parameter(cm, check_tip_threshold);
  if (cm->features.tip_frame_mode) {
    tip_temporal_scale_motion_field(cm, cm->tip_ref.ref_frames_offset);
    if (cm->features.allow_tip_hole_fill) {
      tip_fill_motion_field_holes(cm);
      tip_blk_average_filter_mv(cm);
    }
#if CONFIG_TMVP_MEM_OPT
    av1_fill_tpl_mvs_sample_gap(cm);
#endif  // CONFIG_TMVP_MEM_OPT
    tip_motion_field_within_frame(cm);
  }
}

#if !CONFIG_TIP_REF_PRED_MERGING
static AOM_INLINE void tip_highbd_convolve_2d_facade_compound(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    const int w, const int h, const InterpFilterParams *interp_filters[2],
    SubpelParams *subpel_params, ConvolveParams *conv_params, int bd) {
  const int subpel_x_qn = subpel_params->subpel_x;
  const int subpel_y_qn = subpel_params->subpel_y;
  if (subpel_x_qn && subpel_y_qn) {
    assert(subpel_x_qn && subpel_y_qn);
    av1_highbd_dist_wtd_convolve_2d(src, src_stride, dst, dst_stride, w, h,
                                    interp_filters[0], interp_filters[1],
                                    subpel_x_qn, subpel_y_qn, conv_params, bd);
  } else if (subpel_x_qn && !subpel_y_qn) {
    av1_highbd_dist_wtd_convolve_x(src, src_stride, dst, dst_stride, w, h,
                                   interp_filters[0], subpel_x_qn, conv_params,
                                   bd);
  } else if (!subpel_x_qn && subpel_y_qn) {
    av1_highbd_dist_wtd_convolve_y(src, src_stride, dst, dst_stride, w, h,
                                   interp_filters[1], subpel_y_qn, conv_params,
                                   bd);
  } else {
    av1_highbd_dist_wtd_convolve_2d_copy(src, src_stride, dst, dst_stride, w, h,
                                         conv_params, bd);
  }
}

static AOM_INLINE void tip_highbd_inter_predictor(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    SubpelParams *subpel_params, int w, int h, ConvolveParams *conv_params,
    const InterpFilterParams *interp_filters[2], int bd) {
  assert(conv_params->do_average == 0 || conv_params->do_average == 1);
  const int is_scaled = has_scale(subpel_params->xs, subpel_params->ys);
  assert(conv_params->dst != NULL);
  if (is_scaled) {
    av1_highbd_convolve_2d_scale(
        src, src_stride, dst, dst_stride, w, h, interp_filters[0],
        interp_filters[1], subpel_params->subpel_x, subpel_params->xs,
        subpel_params->subpel_y, subpel_params->ys, conv_params, bd);
  } else {
    revert_scale_extra_bits(subpel_params);
    tip_highbd_convolve_2d_facade_compound(src, src_stride, dst, dst_stride, w,
                                           h, interp_filters, subpel_params,
                                           conv_params, bd);
  }
}

#if !CONFIG_REFINEMV
static AOM_INLINE
#endif  //! CONFIG_REFINEMV
    void
    tip_build_one_inter_predictor(
        uint16_t *dst, int dst_stride, const MV *const src_mv,
        InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
        int ref, uint16_t **mc_buf,
        CalcSubpelParamsFunc calc_subpel_params_func) {
  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;
  calc_subpel_params_func(src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
                          0,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                          mc_buf, &src, &subpel_params, &src_stride);

#if CONFIG_D071_IMP_MSK_BLD
  int use_bacp = 0;
  int n_blocks = 0;
  if (inter_pred_params->border_data.enable_bacp) {
    const int sub_blk_idx = n_blocks * 2 + ref;
    inter_pred_params->border_data.bacp_block_data[sub_blk_idx].x0 =
        subpel_params.x0;
    inter_pred_params->border_data.bacp_block_data[sub_blk_idx].x1 =
        subpel_params.x1;
    inter_pred_params->border_data.bacp_block_data[sub_blk_idx].y0 =
        subpel_params.y0;
    inter_pred_params->border_data.bacp_block_data[sub_blk_idx].y1 =
        subpel_params.y1;
    if (ref == 1) {
      use_bacp = is_out_of_frame_block(
          inter_pred_params, inter_pred_params->ref_frame_buf.width,
          inter_pred_params->ref_frame_buf.height, n_blocks);

      if (use_bacp && inter_pred_params->mask_comp.type == COMPOUND_AVERAGE) {
        inter_pred_params->conv_params.do_average = 0;
        inter_pred_params->comp_mode = MASK_COMP;
        inter_pred_params->mask_comp.seg_mask = xd->seg_mask;
      }
    }
  }

  assert(IMPLIES(ref == 0, !use_bacp));

  if (use_bacp) {
    assert(inter_pred_params->comp_mode == MASK_COMP);
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params, use_bacp,
                                n_blocks);

  } else {
#endif  // CONFIG_D071_IMP_MSK_BLD

    tip_highbd_inter_predictor(
        src, src_stride, dst, dst_stride, &subpel_params,
        inter_pred_params->block_width, inter_pred_params->block_height,
        &inter_pred_params->conv_params,
        inter_pred_params->interp_filter_params, inter_pred_params->bit_depth);
#if CONFIG_D071_IMP_MSK_BLD
  }
#endif  // CONFIG_D071_IMP_MSK_BLD
}
#endif  // !CONFIG_TIP_REF_PRED_MERGING

#if CONFIG_OPTFLOW_ON_TIP || CONFIG_REFINEMV
#define MAKE_BFP_SAD_WRAPPER_COMMON8x8(fnname)                                \
  static unsigned int fnname##_8(const uint16_t *src_ptr, int source_stride,  \
                                 const uint16_t *ref_ptr, int ref_stride) {   \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride);               \
  }                                                                           \
  static unsigned int fnname##_10(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2;          \
  }                                                                           \
  static unsigned int fnname##_12(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4;          \
  }

MAKE_BFP_SAD_WRAPPER_COMMON8x8(aom_highbd_sad8x8)
#define MAKE_BFP_SAD_WRAPPER_COMMON16x8(fnname)                               \
  static unsigned int fnname##_8(const uint16_t *src_ptr, int source_stride,  \
                                 const uint16_t *ref_ptr, int ref_stride) {   \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride);               \
  }                                                                           \
  static unsigned int fnname##_10(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2;          \
  }                                                                           \
  static unsigned int fnname##_12(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4;          \
  }

    MAKE_BFP_SAD_WRAPPER_COMMON16x8(aom_highbd_sad16x8)

#define MAKE_BFP_SAD_WRAPPER_COMMON8x16(fnname)                               \
  static unsigned int fnname##_8(const uint16_t *src_ptr, int source_stride,  \
                                 const uint16_t *ref_ptr, int ref_stride) {   \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride);               \
  }                                                                           \
  static unsigned int fnname##_10(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2;          \
  }                                                                           \
  static unsigned int fnname##_12(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4;          \
  }

        MAKE_BFP_SAD_WRAPPER_COMMON8x16(aom_highbd_sad8x16)

#define MAKE_BFP_SAD_WRAPPER_COMMON16x16(fnname)                              \
  static unsigned int fnname##_8(const uint16_t *src_ptr, int source_stride,  \
                                 const uint16_t *ref_ptr, int ref_stride) {   \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride);               \
  }                                                                           \
  static unsigned int fnname##_10(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2;          \
  }                                                                           \
  static unsigned int fnname##_12(const uint16_t *src_ptr, int source_stride, \
                                  const uint16_t *ref_ptr, int ref_stride) {  \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4;          \
  }

            MAKE_BFP_SAD_WRAPPER_COMMON16x16(aom_highbd_sad16x16)

                unsigned int get_highbd_sad(const uint16_t *src_ptr,
                                            int source_stride,
                                            const uint16_t *ref_ptr,
                                            int ref_stride, int bd, int bw,
                                            int bh) {
  if (bd == 8) {
    if (bw == 8 && bh == 8)
      return aom_highbd_sad8x8_8(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 8)
      return aom_highbd_sad16x8_8(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 8 && bh == 16)
      return aom_highbd_sad8x16_8(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 16)
      return aom_highbd_sad16x16_8(src_ptr, source_stride, ref_ptr, ref_stride);
#if CONFIG_SUBBLK_REF_EXT
    else if (bw == 12 && bh == 12)
      return aom_highbd_sad12x12(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 20 && bh == 12)
      return aom_highbd_sad20x12(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 12 && bh == 20)
      return aom_highbd_sad12x20(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 20 && bh == 20)
      return aom_highbd_sad20x20(src_ptr, source_stride, ref_ptr, ref_stride);
#endif  // CONFIG_SUBBLK_REF_EXT
    else {
      assert(0);
      return 0;
    }
  } else if (bd == 10) {
    if (bw == 8 && bh == 8)
      return aom_highbd_sad8x8_10(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 8)
      return aom_highbd_sad16x8_10(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 8 && bh == 16)
      return aom_highbd_sad8x16_10(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 16)
      return aom_highbd_sad16x16_10(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else {
      assert(0);
      return 0;
    }
  } else if (bd == 12) {
    if (bw == 8 && bh == 8)
      return aom_highbd_sad8x8_12(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 8)
      return aom_highbd_sad16x8_12(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 8 && bh == 16)
      return aom_highbd_sad8x16_12(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 16)
      return aom_highbd_sad16x16_12(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else {
      assert(0);
      return 0;
    }
  } else {
    assert(0);
    return 0;
  }
}
// Build an 8x8 block in the TIP frame
static AOM_INLINE void tip_build_inter_predictors_8x8(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
#if !CONFIG_TIP_REF_PRED_MERGING
    TIP_PLANE *tip_plane,
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    const MV mv[2], int mi_x, int mi_y, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func,
    uint16_t *dst, int dst_stride
#if CONFIG_REFINEMV
    ,
    uint16_t *dst0_16_refinemv, uint16_t *dst1_16_refinemv,
    ReferenceArea ref_area[2]
#endif  // CONFIG_REFINEMV

) {
  // TODO(any): currently this only works for y plane
  assert(plane == 0);

  int bw = 8;
  int bh = 8;

#if !CONFIG_TIP_REF_PRED_MERGING
  TIP_PLANE *const tip = &tip_plane[plane];
#endif  // !CONFIG_TIP_REF_PRED_MERGING

  const int bd = cm->seq_params.bit_depth;

  const int ss_x = plane ? cm->seq_params.subsampling_x : 0;
  const int ss_y = plane ? cm->seq_params.subsampling_y : 0;
  const int comp_pixel_x = (mi_x >> ss_x);
  const int comp_pixel_y = (mi_y >> ss_y);
  const int comp_bw = bw >> ss_x;
  const int comp_bh = bh >> ss_y;

  MB_MODE_INFO mbmi_buf;
  av1_zero(mbmi_buf);
  MB_MODE_INFO *mbmi = &mbmi_buf;

  int_mv mv_refined[2 * 4];
  memset(mv_refined, 0, 2 * 4 * sizeof(int_mv));

  CONV_BUF_TYPE *org_buf = xd->tmp_conv_dst;
  xd->tmp_conv_dst = tmp_conv_dst;

  mbmi->mv[0].as_mv = mv[0];
  mbmi->mv[1].as_mv = mv[1];
  mbmi->ref_frame[0] = TIP_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
#if CONFIG_TIP_DIRECT_FRAME_MV
  mbmi->interp_fltr = cm->tip_interp_filter;
#else
  mbmi->interp_fltr = EIGHTTAP_REGULAR;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
  mbmi->use_intrabc[0] = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->sb_type[PLANE_TYPE_Y] = BLOCK_8X8;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->max_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
  mbmi->pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
#if CONFIG_MORPH_PRED
  mbmi->morph_pred = 0;
#endif  // CONFIG_MORPH_PRED

#if CONFIG_AFFINE_REFINEMENT
  mbmi->comp_refine_type = COMP_REFINE_SUBBLK2P;
  int use_affine_opfl = 0;
  WarpedMotionParams wms[8];
  for (int mvi = 0; mvi < 4; mvi++) {
    wms[2 * mvi] = default_warp_params;
    wms[2 * mvi + 1] = default_warp_params;
  }
#endif  // CONFIG_AFFINE_REFINEMENT

#if CONFIG_REFINEMV
  MV best_mv_ref[2] = { { mbmi->mv[0].as_mv.row, mbmi->mv[0].as_mv.col },
                        { mbmi->mv[1].as_mv.row, mbmi->mv[1].as_mv.col } };

  int apply_refinemv = (is_refinemv_allowed_tip_blocks(cm, mbmi) && plane == 0);

  if (apply_refinemv) {
    uint16_t *dst_ref0 = NULL, *dst_ref1 = NULL;
    dst_ref0 = &dst0_16_refinemv[0];
    dst_ref1 = &dst1_16_refinemv[0];
    mbmi->refinemv_flag = 1;

    apply_mv_refinement(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y, mc_buf,
#if CONFIG_TIP_REF_PRED_MERGING
                        mv,
#endif  // CONFIG_TIP_REF_PRED_MERGING
                        calc_subpel_params_func, comp_pixel_x, comp_pixel_y,
                        dst_ref0, dst_ref1, best_mv_ref, bw, bh);
  }

#endif  // CONFIG_REFINEMV

  // Arrays to hold optical flow offsets.
  int vxy_bufs[4 * 4] = { 0 };
  int *vx0 = vxy_bufs;
  int *vx1 = vxy_bufs + (4 * 1);
  int *vy0 = vxy_bufs + (4 * 2);
  int *vy1 = vxy_bufs + (4 * 3);

  // Pointers to gradient and dst buffers
  int16_t *gx0 = cm->gx0, *gy0 = cm->gy0, *gx1 = cm->gx1, *gy1 = cm->gy1;
  uint16_t *dst0 = NULL, *dst1 = NULL;

  dst0 = cm->dst0_16_tip;
  dst1 = cm->dst1_16_tip;

  int do_opfl = (opfl_allowed_for_cur_refs(cm,
#if CONFIG_COMPOUND_4XN
                                           xd,
#endif  // CONFIG_COMPOUND_4XN
                                           mbmi) &&
                 plane == 0);

  const unsigned int sad_thres =
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT ? 15 : 6;

  const int use_4x4 = 0;
  if (do_opfl) {
    InterPredParams params0, params1;
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params0, calc_subpel_params_func, 0,
                                   dst0
#if CONFIG_REFINEMV
                                   ,
                                   &best_mv_ref[0], bw, bh
#endif  // CONFIG_REFINEMV
    );
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params1, calc_subpel_params_func, 1,
                                   dst1
#if CONFIG_REFINEMV
                                   ,
                                   &best_mv_ref[1], bw, bh
#endif  // CONFIG_REFINEMV

    );
    const unsigned int sad = get_highbd_sad(dst0, bw, dst1, bw, bd, 8, 8);
    if (sad < sad_thres) {
      do_opfl = 0;
    }
  }

  if (do_opfl) {
    // Initialize refined mv
#if CONFIG_REFINEMV
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];
#else
    const MV mv0 = mv[0];
    const MV mv1 = mv[1];
#endif  // CONFIG_REFINEMV
    for (int mvi = 0; mvi < 4; mvi++) {
      mv_refined[mvi * 2].as_mv = mv0;
      mv_refined[mvi * 2 + 1].as_mv = mv1;
    }
    // Refine MV using optical flow. The final output MV will be in 1/16
    // precision.
    av1_get_optflow_based_mv(cm, xd, plane, mbmi, mv_refined, bw, bh, mi_x,
                             mi_y, mc_buf, calc_subpel_params_func, gx0, gy0,
                             gx1, gy1,
#if CONFIG_AFFINE_REFINEMENT
                             use_affine_opfl ? wms : NULL, &use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
                             vx0, vy0, vx1, vy1, dst0, dst1, 0, use_4x4
#if CONFIG_REFINEMV
                             ,
                             best_mv_ref, bw, bh
#endif  // CONFIG_REFINEMV
    );
  }

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp = cm->features.enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf = cm->tip_ref.ref_scale_factor[ref];
#if CONFIG_TIP_REF_PRED_MERGING
    struct buf_2d *const pred_buf = &xd->plane[plane].pre[ref];
#else
    struct buf_2d *const pred_buf = &tip->pred[ref];
#endif  // CONFIG_TIP_REF_PRED_MERGING

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, comp_pixel_y,
                          comp_pixel_x, ss_x, ss_y, bd, 0, sf, pred_buf,
#if CONFIG_TIP_DIRECT_FRAME_MV
                          cm->tip_interp_filter
#else
                          MULTITAP_SHARP
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
    );

#if CONFIG_REFINEMV
    if (apply_refinemv) {
      inter_pred_params.use_ref_padding = 1;
      inter_pred_params.ref_area = &ref_area[ref];
    }
#endif  // CONFIG_REFINEMV

    inter_pred_params.comp_mode = UNIFORM_COMP;

#if CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point to the first ref
    inter_pred_params.sb_type = mbmi->sb_type[PLANE_TYPE_Y];
    inter_pred_params.mask_comp = mbmi->interinter_comp;
#endif  // CONFIG_D071_IMP_MSK_BLD

#if !CONFIG_TIP_REF_PRED_MERGING
    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params.dist_to_top_edge = -GET_MV_SUBPEL(mi_y);
    inter_pred_params.dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - mi_y);
    inter_pred_params.dist_to_left_edge = -GET_MV_SUBPEL(mi_x);
    inter_pred_params.dist_to_right_edge = GET_MV_SUBPEL(width - bw - mi_x);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

    inter_pred_params.conv_params =
        get_conv_params_no_round(ref, plane, tmp_conv_dst, MAX_SB_SIZE, 1, bd);

    if (do_opfl) {
      av1_opfl_rebuild_inter_predictor(
          dst, dst_stride, plane, mv_refined, vxy_bufs, 4, &inter_pred_params,
          xd, mi_x, mi_y,
#if CONFIG_AFFINE_REFINEMENT
          cm, bw, mbmi->comp_refine_type, use_affine_opfl ? wms : NULL,
          &mbmi->mv[ref], use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
          ref, mc_buf, calc_subpel_params_func, use_4x4);
    } else {
#if CONFIG_TIP_REF_PRED_MERGING
      av1_build_one_inter_predictor(dst, dst_stride,
#if CONFIG_REFINEMV
                                    &best_mv_ref[ref],
#else
                                    &mv[ref],
#endif  // CONFIG_REFINEMV
                                    &inter_pred_params, xd, mi_x, mi_y, ref,
                                    mc_buf, calc_subpel_params_func);
#else
      tip_build_one_inter_predictor(dst, dst_stride,
#if CONFIG_REFINEMV
                                    &best_mv_ref[ref],
#else
                                    &mv[ref],
#endif  // CONFIG_REFINEMV
                                    &inter_pred_params, xd, mi_x, mi_y, ref,
                                    mc_buf, calc_subpel_params_func);
#endif  // CONFIG_TIP_REF_PRED_MERGING
    }
  }

  xd->tmp_conv_dst = org_buf;
}
#endif  // CONFIG_OPTFLOW_ON_TIP || CONFIG_REFINEMV

static AOM_INLINE void tip_build_inter_predictors_8x8_and_bigger(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, TIP_PLANE *tip_plane,
    const MV mv[2], int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  TIP_PLANE *const tip = &tip_plane[plane];
  struct buf_2d *const dst_buf = &tip->dst;
  uint16_t *const dst = dst_buf->buf;

#if CONFIG_TIP_REF_PRED_MERGING
  const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
  const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
  xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_y);
  xd->mb_to_bottom_edge = GET_MV_SUBPEL(height - bh - mi_y);
  xd->mb_to_left_edge = -GET_MV_SUBPEL(mi_x);
  xd->mb_to_right_edge = GET_MV_SUBPEL(width - bw - mi_x);
#endif  // CONFIG_TIP_REF_PRED_MERGING

#if CONFIG_REFINEMV || CONFIG_OPTFLOW_ON_TIP
#if CONFIG_REFINEMV
#if CONFIG_SUBBLK_REF_EXT
  uint16_t
      dst0_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * SUBBLK_REF_EXT_LINES) *
                       (REFINEMV_SUBBLOCK_HEIGHT + 2 * SUBBLK_REF_EXT_LINES)];
  uint16_t
      dst1_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * SUBBLK_REF_EXT_LINES) *
                       (REFINEMV_SUBBLOCK_HEIGHT + 2 * SUBBLK_REF_EXT_LINES)];
#else
  uint16_t dst0_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
  uint16_t dst1_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
#endif  // CONFIG_SUBBLK_REF_EXT
  int apply_refinemv = (plane == 0);
  ReferenceArea ref_area[2];
  if (apply_refinemv) {
    MB_MODE_INFO *mbmi = aom_calloc(1, sizeof(*mbmi));
    mbmi->mv[0].as_mv = mv[0];
    mbmi->mv[1].as_mv = mv[1];
    mbmi->ref_frame[0] = TIP_FRAME;
    mbmi->ref_frame[1] = NONE_FRAME;
    mbmi->interp_fltr = EIGHTTAP_REGULAR;
    mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
    mbmi->use_intrabc[0] = 0;
#if CONFIG_MORPH_PRED
    mbmi->morph_pred = 0;
#endif  // CONFIG_MORPH_PRED
    mbmi->motion_mode = SIMPLE_TRANSLATION;
    mbmi->sb_type[PLANE_TYPE_Y] = BLOCK_8X8;
    mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    mbmi->max_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
    mbmi->pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
#if CONFIG_TIP_REF_PRED_MERGING
    av1_get_reference_area_with_padding(cm, xd, plane, mbmi, mv, bw, bh, mi_x,
                                        mi_y, ref_area, bw, bh);
#else
    const int ss_x = plane ? cm->seq_params.subsampling_x : 0;
    const int ss_y = plane ? cm->seq_params.subsampling_y : 0;
    const int comp_pixel_x = (mi_x >> ss_x);
    const int comp_pixel_y = (mi_y >> ss_y);
    av1_get_reference_area_with_padding(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                        ref_area, comp_pixel_x, comp_pixel_y);
#endif  // CONFIG_TIP_REF_PRED_MERGING
    aom_free(mbmi);
  }
#endif  // CONFIG_REFINEMV

  int dst_stride = dst_buf->stride;
  if (plane == 0 && (cm->features.use_optflow_tip
#if CONFIG_REFINEMV
                     || apply_refinemv
#endif  // CONFIG_REFINEMV
                     )) {
    if (bw != 8 || bh != 8) {
      for (int h = 0; h < bh; h += 8) {
        for (int w = 0; w < bw; w += 8) {
          dst_buf->buf = dst + h * dst_stride + w;
          tip_build_inter_predictors_8x8_and_bigger(
              cm, xd, plane, tip_plane, mv, 8, 8, mi_x + w, mi_y + h, mc_buf,
              tmp_conv_dst, calc_subpel_params_func);
        }
      }
      dst_buf->buf = dst;
      return;
    }
    tip_build_inter_predictors_8x8(cm, xd, plane,
#if !CONFIG_TIP_REF_PRED_MERGING
                                   tip_plane,
#endif  // !CONFIG_TIP_REF_PRED_MERGING
                                   mv, mi_x, mi_y, mc_buf, tmp_conv_dst,
                                   calc_subpel_params_func, dst, dst_stride
#if CONFIG_REFINEMV
                                   ,
                                   dst0_16_refinemv, dst1_16_refinemv, ref_area
#endif  // CONFIG_REFINEMV
    );
    return;
  }
#endif  // CONFIG_OPTFLOW_ON_TIP || CONFIG_REFINEMV

  const int bd = cm->seq_params.bit_depth;

  const int ss_x = plane ? cm->seq_params.subsampling_x : 0;
  const int ss_y = plane ? cm->seq_params.subsampling_y : 0;
  const int comp_pixel_x = (mi_x >> ss_x);
  const int comp_pixel_y = (mi_y >> ss_y);
  const int comp_bw = bw >> ss_x;
  const int comp_bh = bh >> ss_y;

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp = cm->features.enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf = cm->tip_ref.ref_scale_factor[ref];
#if CONFIG_TIP_REF_PRED_MERGING
    struct buf_2d *const pred_buf = &xd->plane[plane].pre[ref];
#else
    struct buf_2d *const pred_buf = &tip->pred[ref];
#endif  // CONFIG_TIP_REF_PRED_MERGING

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, comp_pixel_y,
                          comp_pixel_x, ss_x, ss_y, bd, 0, sf, pred_buf,
#if CONFIG_TIP_DIRECT_FRAME_MV
                          cm->tip_interp_filter
#else
                          MULTITAP_SHARP
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
    );

    inter_pred_params.comp_mode = UNIFORM_COMP;

#if CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point to the first ref
    inter_pred_params.sb_type = BLOCK_8X8;
    assert(bw == 8 &&
           bh == 8);  // Currently BACP is supported only for 8x8 block
    inter_pred_params.mask_comp.type = COMPOUND_AVERAGE;
#endif  // CONFIG_D071_IMP_MSK_BLD

#if !CONFIG_TIP_REF_PRED_MERGING
    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params.dist_to_top_edge = -GET_MV_SUBPEL(mi_y);
    inter_pred_params.dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - mi_y);
    inter_pred_params.dist_to_left_edge = -GET_MV_SUBPEL(mi_x);
    inter_pred_params.dist_to_right_edge = GET_MV_SUBPEL(width - bw - mi_x);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

    inter_pred_params.conv_params =
        get_conv_params_no_round(ref, plane, tmp_conv_dst, MAX_SB_SIZE, 1, bd);

#if CONFIG_TIP_REF_PRED_MERGING
    av1_build_one_inter_predictor(dst, dst_buf->stride, &mv[ref],
                                  &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
#else
    tip_build_one_inter_predictor(dst, dst_buf->stride, &mv[ref],
                                  &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
#endif  // CONFIG_TIP_REF_PRED_MERGING
  }
}

static AOM_INLINE void tip_component_build_inter_predictors(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, TIP_PLANE *tip_plane,
    const MV mv[2], int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  tip_build_inter_predictors_8x8_and_bigger(
      cm, xd, plane, tip_plane, mv, bw, bh, mi_x, mi_y, mc_buf, tmp_conv_dst,
      calc_subpel_params_func);
}

static INLINE void tip_setup_pred_plane(struct buf_2d *dst, uint16_t *src,
                                        int width, int height, int stride,
                                        int tpl_row, int tpl_col,
                                        const struct scale_factors *scale,
                                        int subsampling_x, int subsampling_y) {
  const int x = tpl_col >> subsampling_x;
  const int y = tpl_row >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->buf0 = src;
  dst->width = width;
  dst->height = height;
  dst->stride = stride;
}

#if !CONFIG_TIP_REF_PRED_MERGING
static AOM_INLINE void tip_component_setup_pred_planes(AV1_COMMON *const cm,
                                                       const int plane,
                                                       const int tpl_row,
                                                       const int tpl_col) {
  TIP *tip_ref = &cm->tip_ref;
  for (int ref = 0; ref < 2; ++ref) {
    const YV12_BUFFER_CONFIG *ref_buf = &tip_ref->ref_frame_buffer[ref]->buf;
    TIP_PLANE *const pd = &tip_ref->tip_plane[plane];
    int is_uv = 0;
    int subsampling_x = 0;
    int subsampling_y = 0;
    if (plane > 0) {
      is_uv = 1;
      subsampling_x = cm->seq_params.subsampling_x;
      subsampling_y = cm->seq_params.subsampling_y;
    }
    tip_setup_pred_plane(
        &pd->pred[ref], ref_buf->buffers[plane], ref_buf->crop_widths[is_uv],
        ref_buf->crop_heights[is_uv], ref_buf->strides[is_uv], tpl_row, tpl_col,
        tip_ref->ref_scale_factor[ref], subsampling_x, subsampling_y);
  }
}
#endif  // !CONFIG_TIP_REF_PRED_MERGING

static AOM_INLINE void tip_component_setup_dst_planes(AV1_COMMON *const cm,
                                                      const int plane,
                                                      const int tpl_row,
                                                      const int tpl_col) {
  const YV12_BUFFER_CONFIG *src = &cm->tip_ref.tip_frame->buf;
  TIP_PLANE *const pd = &cm->tip_ref.tip_plane[plane];
  int is_uv = 0;
  int subsampling_x = 0;
  int subsampling_y = 0;
  if (plane > 0) {
    is_uv = 1;
    subsampling_x = cm->seq_params.subsampling_x;
    subsampling_y = cm->seq_params.subsampling_y;
  }
  tip_setup_pred_plane(&pd->dst, src->buffers[plane], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->strides[is_uv], tpl_row,
                       tpl_col, NULL, subsampling_x, subsampling_y);
}

static void tip_setup_tip_frame_plane(
    AV1_COMMON *cm, MACROBLOCKD *xd, int plane, int blk_row_start,
    int blk_col_start, int blk_row_end, int blk_col_end, int mvs_stride,
    int unit_blk_size, int max_allow_blk_size, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  TIP *tip_ref = &cm->tip_ref;
  const TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;

  MV zero_mv[2];
  memset(zero_mv, 0, sizeof(zero_mv));

  const int step = (unit_blk_size >> TMVP_MI_SZ_LOG2);
  for (int blk_row = blk_row_start; blk_row < blk_row_end; blk_row += step) {
    for (int blk_col = blk_col_start; blk_col < blk_col_end; blk_col += step) {
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      const TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;
      const int tpl_row = blk_row << TMVP_MI_SZ_LOG2;
      const int tpl_col = blk_col << TMVP_MI_SZ_LOG2;

      int blk_width = unit_blk_size;
      int blk_height = unit_blk_size;
      int offset = step;
      while (blk_col + offset < blk_col_end && blk_width < max_allow_blk_size &&
             tip_ref->mf_need_clamp[tpl_offset] ==
                 tip_ref->mf_need_clamp[tpl_offset + offset] &&
             tpl_mvs->mfmv0.as_int ==
                 tpl_mvs_base[tpl_offset + offset].mfmv0.as_int) {
        blk_width += unit_blk_size;
        offset += step;
      }
      blk_col += (offset - step);

      MV mv[2];
      if (tpl_mvs->mfmv0.as_int != 0
#if CONFIG_MF_HOLE_FILL_SIMPLIFY
          && tpl_mvs->mfmv0.as_int != INVALID_MV
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY
      ) {
        tip_get_mv_projection(&mv[0], tpl_mvs->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[0]);
        tip_get_mv_projection(&mv[1], tpl_mvs->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[1]);
#if CONFIG_TIP_DIRECT_FRAME_MV
        mv[0].row = (int16_t)clamp(mv[0].row + cm->tip_global_motion.as_mv.row,
                                   MV_LOW + 1, MV_UPP - 1);
        mv[0].col = (int16_t)clamp(mv[0].col + cm->tip_global_motion.as_mv.col,
                                   MV_LOW + 1, MV_UPP - 1);
        mv[1].row = (int16_t)clamp(mv[1].row + cm->tip_global_motion.as_mv.row,
                                   MV_LOW + 1, MV_UPP - 1);
        mv[1].col = (int16_t)clamp(mv[1].col + cm->tip_global_motion.as_mv.col,
                                   MV_LOW + 1, MV_UPP - 1);
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
      } else {
#if CONFIG_TIP_DIRECT_FRAME_MV
        mv[0] = cm->tip_global_motion.as_mv;
        mv[1] = cm->tip_global_motion.as_mv;
#else
        mv[0] = zero_mv[0];
        mv[1] = zero_mv[1];
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
      }

#if CONFIG_TIP_REF_PRED_MERGING
      setup_pred_planes_for_tip(&cm->tip_ref, xd, plane, plane + 1,
                                tpl_col >> MI_SIZE_LOG2,
                                tpl_row >> MI_SIZE_LOG2);
#else
      tip_component_setup_pred_planes(cm, plane, tpl_row, tpl_col);
#endif  // CONFIG_TIP_REF_PRED_MERGING
      tip_component_setup_dst_planes(cm, plane, tpl_row, tpl_col);
      tip_component_build_inter_predictors(
          cm, xd, plane, tip_ref->tip_plane, mv, blk_width, blk_height, tpl_col,
          tpl_row, mc_buf, tmp_conv_dst, calc_subpel_params_func);
    }
  }
}

static AOM_INLINE void tip_setup_tip_frame_planes(
    AV1_COMMON *cm, MACROBLOCKD *xd, int blk_row_start, int blk_col_start,
    int blk_row_end, int blk_col_end, int mvs_stride, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  const int num_planes = av1_num_planes(cm);
  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane == 0) {
      tip_setup_tip_frame_plane(cm, xd, plane, blk_row_start, blk_col_start,
                                blk_row_end, blk_col_end, mvs_stride,
                                TMVP_MI_SIZE, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                mc_buf, tmp_conv_dst, calc_subpel_params_func);
    } else {
#if CONFIG_TIP_REF_PRED_MERGING
      // TMVP_MI_SIZE_UV is the block size in luma unit for Chroma
      // TIP interpolation, will convert to the step size in TMVP 8x8 unit
      tip_setup_tip_frame_plane(cm, xd, plane, blk_row_start, blk_col_start,
                                blk_row_end, blk_col_end, mvs_stride,
                                TMVP_MI_SIZE_UV, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                mc_buf, tmp_conv_dst, calc_subpel_params_func);
#else
      // CHROMA_MI_SIZE is the block size in luma unit for Chroma
      // TIP interpolation, will convert to the step size in TMVP 8x8 unit
      tip_setup_tip_frame_plane(cm, xd, plane, blk_row_start, blk_col_start,
                                blk_row_end, blk_col_end, mvs_stride,
                                CHROMA_MI_SIZE, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                mc_buf, tmp_conv_dst, calc_subpel_params_func);
#endif  // CONFIG_TIP_REF_PRED_MERGING
    }
  }

  aom_extend_frame_borders(&cm->tip_ref.tip_frame->buf, av1_num_planes(cm));
}

void av1_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd, uint16_t **mc_buf,
                         CONV_BUF_TYPE *tmp_conv_dst,
                         CalcSubpelParamsFunc calc_subpel_params_func) {
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  tip_setup_tip_frame_planes(cm, xd, 0, 0, mvs_rows, mvs_cols, mvs_cols, mc_buf,
                             tmp_conv_dst, calc_subpel_params_func);
}

#if !CONFIG_TIP_REF_PRED_MERGING
static void tip_extend_plane_block_based_highbd(
    uint16_t *const src, int src_stride, int width, int height, int extend_top,
    int extend_left, int extend_bottom, int extend_right, int start_w,
    int start_h, int blk_w, int blk_h) {
  assert(src != NULL);
  int i = 0;

  if (extend_left) {
    // copy the left most columns out
    uint16_t *src_ptr = src + start_h * src_stride;
    uint16_t *dst_ptr = src_ptr - extend_left;
    for (i = 0; i < blk_h; ++i) {
      aom_memset16(dst_ptr, src_ptr[0], extend_left);
      src_ptr += src_stride;
      dst_ptr += src_stride;
    }
  }

  if (extend_right) {
    // copy the right most columns out
    uint16_t *src_ptr = src + start_h * src_stride + width - 1;
    uint16_t *dst_ptr = src_ptr + 1;
    for (i = 0; i < blk_h; ++i) {
      aom_memset16(dst_ptr, src_ptr[0], extend_right);
      src_ptr += src_stride;
      dst_ptr += src_stride;
    }
  }

  if (extend_top) {
    // copy the top lines into each line of the respective borders
    uint16_t *src_ptr = src + start_w - extend_left;
    uint16_t *dst_ptr = src_ptr - src_stride * extend_top;
    const int extend_size = extend_left + extend_right + blk_w;
    for (i = 0; i < extend_top; ++i) {
      memcpy(dst_ptr, src_ptr, extend_size * sizeof(uint16_t));
      dst_ptr += src_stride;
    }
  }

  if (extend_bottom) {
    // copy the bottom lines into each line of the respective borders
    uint16_t *src_ptr = src + src_stride * (height - 1) + start_w - extend_left;
    uint16_t *dst_ptr = src_ptr + src_stride;
    const int extend_size = extend_left + extend_right + blk_w;
    for (i = 0; i < extend_bottom; ++i) {
      memcpy(dst_ptr, src_ptr, extend_size * sizeof(uint16_t));
      dst_ptr += src_stride;
    }
  }
}

static void tip_extend_plane_border(AV1_COMMON *cm, int blk_row_start,
                                    int blk_col_start, int blk_height,
                                    int blk_width) {
  YV12_BUFFER_CONFIG *tip_buf = &cm->tip_ref.tip_frame->buf;
  const int width = tip_buf->y_width;
  const int height = tip_buf->y_height;

  int top_border = 0;
  int bottom_border = 0;
  int left_border = 0;
  int right_border = 0;
  if (blk_row_start == 0) {
    top_border = 1;
  }

  if (blk_row_start + blk_height >= height) {
    bottom_border = 1;
    blk_height = height - blk_row_start;
  }

  if (blk_col_start == 0) {
    left_border = 1;
  }

  if (blk_col_start + blk_width >= width) {
    right_border = 1;
    blk_width = width - blk_col_start;
  }

  if (top_border || bottom_border || left_border || right_border) {
    const int subsampling_x = cm->seq_params.subsampling_x;
    const int subsampling_y = cm->seq_params.subsampling_y;
    const int y_stride = tip_buf->y_stride;
    const int uv_stride = tip_buf->uv_stride;
    const int extend_border = tip_buf->border;
    const int y_width = tip_buf->y_crop_width;
    const int y_height = tip_buf->y_crop_height;
    const int uv_width = tip_buf->uv_crop_width;
    const int uv_height = tip_buf->uv_crop_height;
    uint16_t *y_dst = tip_buf->y_buffer;
    uint16_t *u_dst = tip_buf->u_buffer;
    uint16_t *v_dst = tip_buf->v_buffer;

    const int extend_top = top_border ? extend_border : 0;
    const int extend_bottom = bottom_border ? extend_border : 0;
    const int extend_left = left_border ? extend_border : 0;
    const int extend_right = right_border ? extend_border : 0;

    const int uv_extend_top = extend_top >> subsampling_y;
    const int uv_extend_bottom = extend_bottom >> subsampling_y;
    const int uv_extend_left = extend_left >> subsampling_x;
    const int uv_extend_right = extend_right >> subsampling_x;

    tip_extend_plane_block_based_highbd(y_dst, y_stride, y_width, y_height,
                                        extend_top, extend_left, extend_bottom,
                                        extend_right, blk_col_start,
                                        blk_row_start, blk_width, blk_height);

    blk_col_start >>= subsampling_x;
    blk_row_start >>= subsampling_y;
    blk_width >>= subsampling_x;
    blk_height >>= subsampling_y;
    tip_extend_plane_block_based_highbd(
        u_dst, uv_stride, uv_width, uv_height, uv_extend_top, uv_extend_left,
        uv_extend_bottom, uv_extend_right, blk_col_start, blk_row_start,
        blk_width, blk_height);
    tip_extend_plane_block_based_highbd(
        v_dst, uv_stride, uv_width, uv_height, uv_extend_top, uv_extend_left,
        uv_extend_bottom, uv_extend_right, blk_col_start, blk_row_start,
        blk_width, blk_height);
  }
}

static void tip_setup_tip_plane_blocks(
    AV1_COMMON *cm, MACROBLOCKD *xd, int plane, int blk_row_start,
    int blk_col_start, int blk_row_end, int blk_col_end, int mvs_stride,
    int unit_blk_size, int max_allow_blk_size, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  TIP *tip_ref = &cm->tip_ref;
  const TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;

  MV zero_mv[2];
  memset(zero_mv, 0, sizeof(zero_mv));

  const int step = (unit_blk_size >> TMVP_MI_SZ_LOG2);
  for (int blk_row = blk_row_start; blk_row < blk_row_end; blk_row += step) {
    for (int blk_col = blk_col_start; blk_col < blk_col_end; blk_col += step) {
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      if (tip_ref->available_flag[tpl_offset]) continue;
      const TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;
      const int tpl_row = blk_row << TMVP_MI_SZ_LOG2;
      const int tpl_col = blk_col << TMVP_MI_SZ_LOG2;

      int blk_width = unit_blk_size;
      int blk_height = unit_blk_size;
      int offset = step;
      while (blk_col + offset < blk_col_end && blk_width < max_allow_blk_size &&
             !tip_ref->available_flag[tpl_offset + offset] &&
             tip_ref->mf_need_clamp[tpl_offset] ==
                 tip_ref->mf_need_clamp[tpl_offset + offset] &&
             tpl_mvs->mfmv0.as_int ==
                 tpl_mvs_base[tpl_offset + offset].mfmv0.as_int) {
        blk_width += unit_blk_size;
        offset += step;
      }
      blk_col += (offset - step);

      MV mv[2];
      if (tpl_mvs->mfmv0.as_int != 0
#if CONFIG_MF_HOLE_FILL_SIMPLIFY
          && tpl_mvs->mfmv0.as_int != INVALID_MV
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY
      ) {
        tip_get_mv_projection(&mv[0], tpl_mvs->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[0]);
        tip_get_mv_projection(&mv[1], tpl_mvs->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[1]);
      } else {
        mv[0] = zero_mv[0];
        mv[1] = zero_mv[1];
      }

      tip_component_setup_pred_planes(cm, plane, tpl_row, tpl_col);
      tip_component_setup_dst_planes(cm, plane, tpl_row, tpl_col);
      tip_component_build_inter_predictors(
          cm, xd, plane, tip_ref->tip_plane, mv, blk_width, blk_height, tpl_col,
          tpl_row, mc_buf, tmp_conv_dst, calc_subpel_params_func);
    }
  }
}

static AOM_INLINE void tip_setup_tip_planes_blocks(
    AV1_COMMON *cm, MACROBLOCKD *xd, int blk_row_start, int blk_col_start,
    int blk_row_end, int blk_col_end, int mvs_stride, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func) {
  const int num_planes = av1_num_planes(cm);
  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane == 0) {
      tip_setup_tip_plane_blocks(cm, xd, plane, blk_row_start, blk_col_start,
                                 blk_row_end, blk_col_end, mvs_stride,
                                 TMVP_MI_SIZE, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                 mc_buf, tmp_conv_dst, calc_subpel_params_func);
    } else {
      // CHROMA_MI_SIZE is the block size in luma unit for Chroma
      // TIP interpolation, will convert to the step size in TMVP 8x8 unit
      tip_setup_tip_plane_blocks(cm, xd, plane, blk_row_start, blk_col_start,
                                 blk_row_end, blk_col_end, mvs_stride,
                                 CHROMA_MI_SIZE, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                 mc_buf, tmp_conv_dst, calc_subpel_params_func);
    }
  }

  const int step = (TMVP_MI_SIZE >> TMVP_MI_SZ_LOG2);
  for (int blk_row = blk_row_start; blk_row < blk_row_end; blk_row += step) {
    for (int blk_col = blk_col_start; blk_col < blk_col_end; blk_col += step) {
      const int tpl_offset = blk_row * mvs_stride + blk_col;
      cm->tip_ref.available_flag[tpl_offset] = 1;
    }
  }
}

void av1_setup_tip_on_the_fly(AV1_COMMON *cm, MACROBLOCKD *xd,
                              int blk_row_start, int blk_col_start,
                              int blk_row_end, int blk_col_end, int mvs_stride,
                              uint16_t **mc_buf, CONV_BUF_TYPE *tmp_conv_dst,
                              CalcSubpelParamsFunc calc_subpel_params_func) {
  tip_setup_tip_planes_blocks(cm, xd, blk_row_start, blk_col_start, blk_row_end,
                              blk_col_end, mvs_stride, mc_buf, tmp_conv_dst,
                              calc_subpel_params_func);
  tip_extend_plane_border(cm, blk_row_start << TMVP_MI_SZ_LOG2,
                          blk_col_start << TMVP_MI_SZ_LOG2,
                          (blk_row_end - blk_row_start) << TMVP_MI_SZ_LOG2,
                          (blk_col_end - blk_col_start) << TMVP_MI_SZ_LOG2);
}
#endif  // !CONFIG_TIP_REF_PRED_MERGING

void av1_copy_tip_frame_tmvp_mvs(const AV1_COMMON *const cm) {
  MV_REF *frame_mvs = cm->cur_frame->mvs;
  const TPL_MV_REF *tpl_mvs = cm->tpl_mvs;
  const TIP *tip_ref = &cm->tip_ref;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int mvs_stride = mvs_cols;

  for (int h = 0; h < mvs_rows; h++) {
    MV_REF *mv = frame_mvs;
    const TPL_MV_REF *tpl_mv = tpl_mvs;
    for (int w = 0; w < mvs_cols; w++) {
      mv->ref_frame[0] = NONE_FRAME;
      mv->ref_frame[1] = NONE_FRAME;
      mv->mv[0].as_int = 0;
      mv->mv[1].as_int = 0;
      if (tpl_mv->mfmv0.as_int != INVALID_MV) {
        int_mv this_mv[2] = { { 0 } };
        tip_get_mv_projection(&this_mv[0].as_mv, tpl_mv->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[0]);
        tip_get_mv_projection(&this_mv[1].as_mv, tpl_mv->mfmv0.as_mv,
                              tip_ref->ref_frames_offset_sf[1]);
#if CONFIG_TIP_DIRECT_FRAME_MV
        this_mv[0].as_mv.row += cm->tip_global_motion.as_mv.row;
        this_mv[0].as_mv.col += cm->tip_global_motion.as_mv.col;
        this_mv[1].as_mv.row += cm->tip_global_motion.as_mv.row;
        this_mv[1].as_mv.col += cm->tip_global_motion.as_mv.col;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV

        if ((abs(this_mv[0].as_mv.row) <= REFMVS_LIMIT) &&
            (abs(this_mv[0].as_mv.col) <= REFMVS_LIMIT)) {
          mv->ref_frame[0] = tip_ref->ref_frame[0];
#if CONFIG_TIP_DIRECT_FRAME_MV
          mv->mv[0].as_mv.row = this_mv[0].as_mv.row;
          mv->mv[0].as_mv.col = this_mv[0].as_mv.col;
#else
          mv->mv[0].as_int = this_mv[0].as_int;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
        }

        if ((abs(this_mv[1].as_mv.row) <= REFMVS_LIMIT) &&
            (abs(this_mv[1].as_mv.col) <= REFMVS_LIMIT)) {
          mv->ref_frame[1] = tip_ref->ref_frame[1];
#if CONFIG_TIP_DIRECT_FRAME_MV
          mv->mv[1].as_mv.row = this_mv[1].as_mv.row;
          mv->mv[1].as_mv.col = this_mv[1].as_mv.col;
#else
          mv->mv[1].as_int = this_mv[1].as_int;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
        }
      }
      mv++;
      tpl_mv++;
    }
    frame_mvs += mvs_stride;
    tpl_mvs += mvs_stride;
  }
}
