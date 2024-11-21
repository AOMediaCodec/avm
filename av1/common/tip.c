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

#if !CONFIG_TMVP_MEM_OPT
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

#if CONFIG_TIP_LD
static void tip_find_two_closest_past_ref_frames(AV1_COMMON *cm,
                                                 int ref_order_hints[2],
                                                 MV_REFERENCE_FRAME rf[2]) {
  const OrderHintInfo *const order_hint_info = &cm->seq_params.order_hint_info;
  if (!order_hint_info->enable_order_hint || frame_is_intra_only(cm)) return;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int cur_order_hint = cm->current_frame.display_order_hint;
#else
  const int cur_order_hint = cm->current_frame.order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  // Identify two nearest past references.
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
      if (rf[0] == NONE_FRAME ||
          get_relative_dist(order_hint_info, ref_order_hint,
                            ref_order_hints[0]) > 0) {
        ref_order_hints[0] = ref_order_hint;
        rf[0] = i;
      } else if (rf[1] == NONE_FRAME ||
                 get_relative_dist(order_hint_info, ref_order_hint,
                                   ref_order_hints[1]) > 0) {
        ref_order_hints[1] = ref_order_hint;
        rf[1] = i;
      }
    }
  }
}
#endif  // CONFIG_TIP_LD

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

  if (abs(ref_frame_offset) > MAX_FRAME_DISTANCE) {
    return 0;
  }

  const RefCntBuffer *const start_frame_buf =
      get_ref_frame_buf(cm, start_frame);
  if (!is_ref_motion_field_eligible(cm, start_frame_buf)) return 0;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int start_frame_order_hint = start_frame_buf->display_order_hint;
#else
  const int start_frame_order_hint = start_frame_buf->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
#if CONFIG_ACROSS_SCALE_TPL_MVS
  const int is_scaled = (start_frame_buf->width != cm->width ||
                         start_frame_buf->height != cm->height);
  struct scale_factors sf_;
  // Inverse scale factor
  av1_setup_scale_factors_for_frame(&sf_, cm->width, cm->height,
                                    start_frame_buf->width,
                                    start_frame_buf->height);
  const struct scale_factors *sf = &sf_;
#else
  assert(start_frame_buf->width == cm->width &&
         start_frame_buf->height == cm->height);
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
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
  const int start_mvs_rows =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_rows, TMVP_SHIFT_BITS);
  const int start_mvs_cols =
      ROUND_POWER_OF_TWO(start_frame_buf->mi_cols, TMVP_SHIFT_BITS);
  (void)mvs_rows;
#if CONFIG_ACROSS_SCALE_TPL_MVS
  uint32_t scaled_blk_col_hr_0 = 0;
  uint32_t scaled_blk_col_hr_step = 0;
  uint32_t scaled_blk_col_hr = 0;
  uint32_t scaled_blk_row_hr_0 = 0;
  uint32_t scaled_blk_row_hr_step = 0;
  uint32_t scaled_blk_row_hr = 0;
  if (is_scaled) {
    scaled_blk_col_hr_0 =
        (uint32_t)sf->x_scale_fp * 4;  // center of first block
    scaled_blk_col_hr_step = (uint32_t)sf->x_scale_fp * 8;  // step
    scaled_blk_row_hr_0 =
        (uint32_t)sf->y_scale_fp * 4;  // center of first block
    scaled_blk_row_hr_step = (uint32_t)sf->y_scale_fp * 8;  // step
    scaled_blk_row_hr = scaled_blk_row_hr_0;
  }
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
  for (int blk_row = 0; blk_row < start_mvs_rows; ++blk_row) {
    int scaled_blk_row = blk_row;
#if CONFIG_ACROSS_SCALE_TPL_MVS
    if (is_scaled) {
      scaled_blk_col_hr = scaled_blk_col_hr_0;
      scaled_blk_row =
          ROUND_POWER_OF_TWO(scaled_blk_row_hr, REF_SCALE_SHIFT + 3);
      scaled_blk_row = AOMMIN(scaled_blk_row, mvs_rows - 1);
    }
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
    for (int blk_col = 0; blk_col < start_mvs_cols; ++blk_col) {
      const MV_REF *mv_ref = &mv_ref_base[blk_row * start_mvs_cols + blk_col];
      MV_REFERENCE_FRAME ref_frame[2] = { mv_ref->ref_frame[0],
                                          mv_ref->ref_frame[1] };
      for (int idx = 0; idx < 2; ++idx) {
        if (is_inter_ref_frame(ref_frame[idx])) {
          const int ref_frame_order_hint = ref_order_hints[ref_frame[idx]];
          if (ref_frame_order_hint == target_order_hint) {
            MV ref_mv = mv_ref->mv[idx].as_mv;
#if CONFIG_TMVP_MV_COMPRESSION
            fetch_mv_from_tmvp(&ref_mv);
#endif  // CONFIG_TMVP_MV_COMPRESSION
            int scaled_blk_col = blk_col;
#if CONFIG_ACROSS_SCALE_TPL_MVS
            if (is_scaled) {
              scaled_blk_col =
                  ROUND_POWER_OF_TWO(scaled_blk_col_hr, REF_SCALE_SHIFT + 3);
              scaled_blk_col = AOMMIN(scaled_blk_col, mvs_cols - 1);
              ref_mv.row = sf->scale_value_y_gen(ref_mv.row, sf);
              ref_mv.col = sf->scale_value_x_gen(ref_mv.col, sf);
            }
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
            int_mv this_mv;
            int mi_r = 0;
            int mi_c = 0;
            tip_get_mv_projection(&this_mv.as_mv, ref_mv,
                                  temporal_scale_factor);
            const int pos_valid =
                get_block_position(cm, &mi_r, &mi_c, scaled_blk_row,
                                   scaled_blk_col, this_mv.as_mv, 0);
            if (pos_valid) {
              assert(mi_r < mvs_rows);
              assert(mi_c < mvs_cols);
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
#if CONFIG_ACROSS_SCALE_TPL_MVS
      if (is_scaled) scaled_blk_col_hr += scaled_blk_col_hr_step;
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
    }
#if CONFIG_ACROSS_SCALE_TPL_MVS
    if (is_scaled) scaled_blk_row_hr += scaled_blk_row_hr_step;
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
  }

  return 1;
}

void av1_derive_tip_nearest_ref_frames_motion_projection(AV1_COMMON *cm) {
  int nearest_ref_order_hints[2] = { -1, INT_MAX };
  MV_REFERENCE_FRAME nearest_rf[2] = { NONE_FRAME, NONE_FRAME };
#if CONFIG_TIP_LD
  if (cm->has_both_sides_refs) {
    tip_find_closest_bi_dir_ref_frames(cm, nearest_ref_order_hints, nearest_rf);
  } else if (cm->ref_frames_info.num_past_refs >= 2) {
    tip_find_two_closest_past_ref_frames(cm, nearest_ref_order_hints,
                                         nearest_rf);
  }
#else
  tip_find_closest_bi_dir_ref_frames(cm, nearest_ref_order_hints, nearest_rf);
#endif  // CONFIG_TIP_LD
  if (nearest_rf[0] != NONE_FRAME && nearest_rf[1] != NONE_FRAME) {
    cm->tip_ref.ref_frame[0] = nearest_rf[0];
    cm->tip_ref.ref_frame[1] = nearest_rf[1];
    tip_motion_field_projection(cm, nearest_rf, nearest_ref_order_hints);
  } else {
    cm->tip_ref.ref_frame[0] = NONE_FRAME;
    cm->tip_ref.ref_frame[1] = NONE_FRAME;
  }
}
#endif  // !CONFIG_TMVP_MEM_OPT

static void tip_temporal_scale_motion_field(AV1_COMMON *cm,
                                            const int ref_frames_offset) {
  if (abs(ref_frames_offset) > MAX_FRAME_DISTANCE) {
    return;
  }

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
      } else {
        tpl_mvs->ref_frame_offset = ref_frames_offset;
      }
    }
  }
}

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

static void tip_fill_motion_field_holes(AV1_COMMON *cm) {
  int mvs_rows = 0;
  int mvs_cols = 0;
  int mvs_stride = 0;
  int sb_tmvp_size = 0;
#if CONFIG_TMVP_MEM_OPT
  assert(cm->tmvp_sample_step > 0);
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

static void tip_blk_average_filter_mv(AV1_COMMON *cm) {
  int mvs_rows = 0;
  int mvs_cols = 0;
  int mvs_stride = 0;
  int sb_tmvp_size = 0;
#if CONFIG_TMVP_MEM_OPT
  assert(cm->tmvp_sample_step > 0);
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

#if CONFIG_ACROSS_SCALE_TPL_MVS
  const int width = cm->width;
  const int height = cm->height;
#else
  const int width = (mvs_cols << TMVP_MI_SZ_LOG2);
  const int height = (mvs_rows << TMVP_MI_SZ_LOG2);
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS

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
        mf_need_clamp[cur_pos] = 0;
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
  assert(cm->tmvp_sample_step > 0);
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

#if CONFIG_TIP_LD
      int ref_frames_offset = 0;
      if (cm->has_both_sides_refs) {
        ref_frames_offset = get_relative_dist(
            order_hint_info, ref1_frame_order_hint, ref0_frame_order_hint);
      } else {
        ref_frames_offset = get_relative_dist(
            order_hint_info, ref0_frame_order_hint, ref1_frame_order_hint);
      }
#else
      const int ref_frames_offset = get_relative_dist(
          order_hint_info, ref1_frame_order_hint, ref0_frame_order_hint);
#endif  // CONFIG_TIP_LD
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

#if CONFIG_OPTFLOW_ON_TIP && CONFIG_TIP_LD
    cm->features.use_optflow_tip =
        cm->features.tip_frame_mode && cm->has_both_sides_refs;
#endif  // CONFIG_OPTFLOW_ON_TIP && CONFIG_TIP_LD
  }
}

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
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MV mv[2], int mi_x,
    int mi_y, uint16_t **mc_buf, CONV_BUF_TYPE *tmp_conv_dst,
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst, int dst_stride
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

    apply_mv_refinement(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y, mc_buf, mv,
                        calc_subpel_params_func, comp_pixel_x, comp_pixel_y,
                        dst_ref0, dst_ref1, best_mv_ref, bw, bh
#if CONFIG_OPFL_MEMBW_REDUCTION
                        ,
                        ref_area
#endif  // CONFIG_OPFL_MEMBW_REDUCTION
    );
#if CONFIG_IMPROVE_REFINED_MV
    REFINEMV_SUBMB_INFO *refinemv_subinfo = &xd->refinemv_subinfo[0];
    fill_subblock_refine_mv(refinemv_subinfo, bw, bh, best_mv_ref[0],
                            best_mv_ref[1]);
#endif  // CONFIG_IMPROVE_REFINED_MV
  }

#endif  // CONFIG_REFINEMV
#if CONFIG_IMPROVE_REFINED_MV
  if (plane == 0) {
    mv_refined[0].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[0]);
    mv_refined[1].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[1]);
  }
#endif  // CONFIG_IMPROVE_REFINED_MV

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
    av1_get_optflow_based_mv(
        cm, xd, plane, mbmi, mv_refined, bw, bh, mi_x, mi_y,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
        0 /* build_for_decode */,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
        mc_buf, calc_subpel_params_func, gx0, gy0, gx1, gy1,
#if CONFIG_AFFINE_REFINEMENT
        use_affine_opfl ? wms : NULL, &use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
        vx0, vy0, vx1, vy1, dst0, dst1, 0, use_4x4
#if CONFIG_REFINEMV
        ,
        best_mv_ref, bw, bh
#endif  // CONFIG_REFINEMV
    );
#if CONFIG_IMPROVE_REFINED_MV
    xd->opfl_vxy_bufs[0] = *vx0;
    xd->opfl_vxy_bufs[N_OF_OFFSETS * 1] = *vx1;
    xd->opfl_vxy_bufs[N_OF_OFFSETS * 2] = *vy0;
    xd->opfl_vxy_bufs[N_OF_OFFSETS * 3] = *vy1;
#endif  // CONFIG_IMPROVE_REFINED_MV
  }

#if CONFIG_IMPROVE_REFINED_MV
  if (plane == 0) {
    xd->mv_refined[0] = mv_refined[0];
    xd->mv_refined[1] = mv_refined[1];
  }
#endif  // CONFIG_IMPROVE_REFINED_MV

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp = cm->features.enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf = cm->tip_ref.ref_scale_factor[ref];
    struct buf_2d *const pred_buf = &xd->plane[plane].pre[ref];

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
#if CONFIG_OPFL_MEMBW_REDUCTION
    if (apply_refinemv || do_opfl) {
#else
    if (apply_refinemv) {
#endif  // CONFIG_OPFL_MEMBW_REDUCTION
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

    inter_pred_params.conv_params =
        get_conv_params_no_round(ref, plane, tmp_conv_dst, MAX_SB_SIZE, 1, bd);

    if (do_opfl) {
      av1_opfl_rebuild_inter_predictor(
          dst, dst_stride, plane, mv_refined, vxy_bufs, 4, &inter_pred_params,
          xd, mi_x, mi_y,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
          0 /* build_for_decode */,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
#if CONFIG_AFFINE_REFINEMENT
          cm, bw, mbmi->comp_refine_type, use_affine_opfl ? wms : NULL,
          &mbmi->mv[ref], use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
          ref, mc_buf, calc_subpel_params_func, use_4x4
#if CONFIG_OPFL_MEMBW_REDUCTION || CONFIG_WARP_BD_BOX
          ,
          mbmi, bh
#endif  // CONFIG_OPFL_MEMBW_REDUCTION||CONFIG_WARP_BD_BOX
#if CONFIG_OPFL_MEMBW_REDUCTION
          ,
          0
#endif  // CONFIG_OPFL_MEMBW_REDUCTION
#if CONFIG_WARP_BD_BOX
          ,
          0
#endif  // CONFIG_WARP_BD_BOX
      );
    } else {
#if CONFIG_IMPROVE_REFINED_MV
#if CONFIG_REFINEMV
      const MV mv_1_16th_pel = convert_mv_to_1_16th_pel(&best_mv_ref[ref]);
#else
      const MV mv_1_16th_pel = convert_mv_to_1_16th_pel(&mv[ref]);
#endif
#endif  // CONFIG_IMPROVE_REFINED_MV
      av1_build_one_inter_predictor(dst, dst_stride,
#if CONFIG_IMPROVE_REFINED_MV
                                    &mv_1_16th_pel,
#else
#if CONFIG_REFINEMV
                                    &best_mv_ref[ref],
#else
                                    &mv[ref],
#endif  // CONFIG_REFINEMV
#endif  // CONFIG_IMPROVE_REFINED_MV
                                    &inter_pred_params, xd, mi_x, mi_y, ref,
                                    mc_buf, calc_subpel_params_func);
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

  const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
  const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
  xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_y);
  xd->mb_to_bottom_edge = GET_MV_SUBPEL(height - bh - mi_y);
  xd->mb_to_left_edge = -GET_MV_SUBPEL(mi_x);
  xd->mb_to_right_edge = GET_MV_SUBPEL(width - bw - mi_x);
#if CONFIG_IMPROVE_REFINED_MV
  const int ss_x = plane ? cm->seq_params.subsampling_x : 0;
  const int ss_y = plane ? cm->seq_params.subsampling_y : 0;
  const int comp_pixel_x = (mi_x >> ss_x);
  const int comp_pixel_y = (mi_y >> ss_y);
  const int comp_bw = bw >> ss_x;
  const int comp_bh = bh >> ss_y;
#endif  // CONFIG_IMPROVE_REFINED_MV

#if CONFIG_TIP_LD || CONFIG_TIP_ENHANCEMENT
  const int has_both_sides_refs = cm->has_both_sides_refs;
#endif  // CONFIG_TIP_LD || CONFIG_TIP_ENHANCEMENT
#if CONFIG_TIP_ENHANCEMENT
  const int tip_wtd_index = cm->tip_global_wtd_index;
  const int8_t tip_weight = tip_weighting_factors[tip_wtd_index];
  const int is_compound = tip_weight != TIP_SINGLE_WTD;
#else
  const int is_compound = 1;
#endif  // CONFIG_TIP_ENHANCEMENT

#if CONFIG_REFINEMV || CONFIG_OPTFLOW_ON_TIP
#if CONFIG_REFINEMV
#if CONFIG_SUBBLK_REF_EXT
  uint16_t
      dst0_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH +
                        2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)) *
                       (REFINEMV_SUBBLOCK_HEIGHT +
                        2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))];
  uint16_t
      dst1_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH +
                        2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)) *
                       (REFINEMV_SUBBLOCK_HEIGHT +
                        2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))];
#else
  uint16_t
      dst0_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES) *
                       (REFINEMV_SUBBLOCK_HEIGHT + 2 * DMVR_SEARCH_EXT_LINES)];
  uint16_t
      dst1_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES) *
                       (REFINEMV_SUBBLOCK_HEIGHT + 2 * DMVR_SEARCH_EXT_LINES)];
#endif  // CONFIG_SUBBLK_REF_EXT

#if CONFIG_TIP_LD
  const int apply_refinemv = (cm->seq_params.enable_refinemv && plane == 0 &&
                              has_both_sides_refs && is_compound
#if CONFIG_TIP_ENHANCEMENT
                              && tip_weight == TIP_EQUAL_WTD
#endif  // CONFIG_TIP_ENHANCEMENT
  );
#else
  int apply_refinemv = (plane == 0);
#endif  // CONFIG_TIP_LD

  ReferenceArea ref_area[2];
#if CONFIG_IMPROVE_REFINED_MV
  const int do_ref_area_pad =
#if CONFIG_TIP_LD
      cm->has_both_sides_refs &&
#endif
      (comp_bw > 4 || comp_bh > 4);
  if (do_ref_area_pad) {
#else
  if (apply_refinemv) {
#endif  // CONFIG_IMPROVE_REFINED_MV
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
#if CONFIG_IMPROVE_REFINED_MV
    const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
    const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
    mbmi->chroma_ref_info.mi_row_chroma_base = mi_row;
    mbmi->chroma_ref_info.mi_col_chroma_base = mi_col;
#endif  // CONFIG_IMPROVE_REFINED_MV
    av1_get_reference_area_with_padding(cm, xd, plane, mbmi, mv,
#if CONFIG_IMPROVE_REFINED_MV
                                        comp_bw, comp_bh,
#else
                                        bw, bh,
#endif  // CONFIG_IMPROVE_REFINED_MV
                                        mi_x, mi_y, ref_area, bw, bh);
    aom_free(mbmi);
  }
#endif  // CONFIG_REFINEMV

  int dst_stride = dst_buf->stride;
  if (plane == 0 &&
#if CONFIG_TIP_ENHANCEMENT
      is_any_mv_refinement_allowed(cm) && is_compound &&
      tip_weight == TIP_EQUAL_WTD &&
#endif  // CONFIG_TIP_ENHANCEMENT
      (cm->features.use_optflow_tip
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
    tip_build_inter_predictors_8x8(cm, xd, plane, mv, mi_x, mi_y, mc_buf,
                                   tmp_conv_dst, calc_subpel_params_func, dst,
                                   dst_stride
#if CONFIG_REFINEMV
                                   ,
                                   dst0_16_refinemv, dst1_16_refinemv, ref_area
#endif  // CONFIG_REFINEMV
    );
    return;
  }
#endif  // CONFIG_OPTFLOW_ON_TIP || CONFIG_REFINEMV

  const int bd = cm->seq_params.bit_depth;

#if CONFIG_IMPROVE_REFINED_MV
  if (plane == 0) {
    xd->mv_refined[0].as_mv = convert_mv_to_1_16th_pel(&mv[0]);
    xd->mv_refined[1].as_mv = convert_mv_to_1_16th_pel(&mv[1]);
  }
#else
  const int ss_x = plane ? cm->seq_params.subsampling_x : 0;
  const int ss_y = plane ? cm->seq_params.subsampling_y : 0;
  const int comp_pixel_x = (mi_x >> ss_x);
  const int comp_pixel_y = (mi_y >> ss_y);
  const int comp_bw = bw >> ss_x;
  const int comp_bh = bh >> ss_y;
#endif  // CONFIG_IMPROVE_REFINED_MV

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp =
#if CONFIG_TIP_ENHANCEMENT
      is_compound && tip_weight == TIP_EQUAL_WTD &&
#endif  // CONFIG_TIP_ENHANCEMENT
      cm->features.enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf = cm->tip_ref.ref_scale_factor[ref];
    struct buf_2d *const pred_buf = &xd->plane[plane].pre[ref];

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, comp_pixel_y,
                          comp_pixel_x, ss_x, ss_y, bd, 0, sf, pred_buf,
#if CONFIG_TIP_DIRECT_FRAME_MV
                          cm->tip_interp_filter
#else
                          MULTITAP_SHARP
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
    );

#if CONFIG_TIP_ENHANCEMENT
    if (is_compound) {
#endif  // CONFIG_TIP_ENHANCEMENT
      inter_pred_params.comp_mode = UNIFORM_COMP;
#if CONFIG_TIP_ENHANCEMENT
    }
#endif  // CONFIG_TIP_ENHANCEMENT

#if CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point to the first ref
    inter_pred_params.sb_type = BLOCK_8X8;
    assert(bw == 8 &&
           bh == 8);  // Currently BACP is supported only for 8x8 block
#if CONFIG_TIP_ENHANCEMENT
    if (is_compound) {
#endif  // CONFIG_TIP_ENHANCEMENT
      inter_pred_params.mask_comp.type = COMPOUND_AVERAGE;
#if CONFIG_TIP_ENHANCEMENT
    }
#endif  // CONFIG_TIP_ENHANCEMENT
#endif  // CONFIG_D071_IMP_MSK_BLD

    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, tmp_conv_dst, MAX_SB_SIZE, is_compound, bd);

#if CONFIG_TIP_ENHANCEMENT
    set_tip_interp_weight_factor(cm, ref, &inter_pred_params);
#endif  // CONFIG_TIP_ENHANCEMENT

#if CONFIG_IMPROVE_REFINED_MV
    if (do_ref_area_pad) {
      inter_pred_params.use_ref_padding = 1;
      inter_pred_params.ref_area = &ref_area[ref];
    }

    const MV mv_1_16th_pel =
        plane ? xd->mv_refined[ref].as_mv : convert_mv_to_1_16th_pel(&mv[ref]);
#endif  // CONFIG_IMPROVE_REFINED_MV

    av1_build_one_inter_predictor(dst, dst_buf->stride,
#if CONFIG_IMPROVE_REFINED_MV
                                  &mv_1_16th_pel,
#else
                                  &mv[ref],
#endif  // CONFIG_IMPROVE_REFINED_MV
                                  &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
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
    AV1_COMMON *cm, MACROBLOCKD *xd,
#if !CONFIG_IMPROVE_REFINED_MV
    int plane,
#endif  // !CONFIG_IMPROVE_REFINED_MV
    int blk_row_start, int blk_col_start, int blk_row_end, int blk_col_end,
    int mvs_stride, int unit_blk_size, int max_allow_blk_size,
    uint16_t **mc_buf, CONV_BUF_TYPE *tmp_conv_dst,
    CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_IMPROVE_REFINED_MV
    ,
    int copy_refined_mvs
#endif  // CONFIG_IMPROVE_REFINED_MV
) {
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
      if (tpl_mvs->mfmv0.as_int != 0 && tpl_mvs->mfmv0.as_int != INVALID_MV) {
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

#if CONFIG_IMPROVE_REFINED_MV
      for (int plane = 0; plane < av1_num_planes(cm); plane++) {
        if (plane == 0 && copy_refined_mvs) {
          REFINEMV_SUBMB_INFO *refinemv_subinfo = &xd->refinemv_subinfo[0];
          fill_subblock_refine_mv(refinemv_subinfo, unit_blk_size,
                                  unit_blk_size, mv[0], mv[1]);
          xd->opfl_vxy_bufs[0] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 1] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 2] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 3] = 0;
        }

        setup_pred_planes_for_tip(&cm->tip_ref, xd, plane, plane + 1,
                                  tpl_col >> MI_SIZE_LOG2,
                                  tpl_row >> MI_SIZE_LOG2);

        tip_component_setup_dst_planes(cm, plane, tpl_row, tpl_col);
        tip_component_build_inter_predictors(
            cm, xd, plane, tip_ref->tip_plane, mv, blk_width, blk_height,
            tpl_col, tpl_row, mc_buf, tmp_conv_dst, calc_subpel_params_func);

        if (plane == 0 && copy_refined_mvs) {
          MB_MODE_INFO mbmi;
          av1_zero(mbmi);
          mbmi.sb_type[PLANE_TYPE_Y] = BLOCK_8X8;
          mbmi.sb_type[PLANE_TYPE_UV] = BLOCK_4X4;
          mbmi.ref_frame[0] = TIP_FRAME;
          mbmi.ref_frame[1] = NONE_FRAME;
          mbmi.mode = NEWMV;
          mbmi.skip_mode = 0;
          mbmi.motion_mode = SIMPLE_TRANSLATION;
          mbmi.interinter_comp.type = COMPOUND_AVERAGE;
          mbmi.cwp_idx = 0;
          mbmi.comp_refine_type = COMP_REFINE_SUBBLK2P;
          mbmi.refinemv_flag = 0;

          // Save the MVs before refinement into the TMVP list.
#if CONFIG_TIP_DIRECT_FRAME_MV
          mbmi.mv[0].as_mv = cm->tip_global_motion.as_mv;
          mbmi.mv[1].as_mv = cm->tip_global_motion.as_mv;
#else
          mbmi.mv[0].as_mv = zero_mv[0];
          mbmi.mv[1].as_mv = zero_mv[1];
#endif
          av1_copy_frame_mvs(cm, xd, &mbmi, blk_row << TMVP_SHIFT_BITS,
                             blk_col << TMVP_SHIFT_BITS,
                             step << TMVP_SHIFT_BITS, step << TMVP_SHIFT_BITS);

          av1_copy_frame_refined_mvs(cm, xd, &mbmi, blk_row << TMVP_SHIFT_BITS,
                                     blk_col << TMVP_SHIFT_BITS,
                                     step << TMVP_SHIFT_BITS,
                                     step << TMVP_SHIFT_BITS);
        }
      }
#else
      setup_pred_planes_for_tip(&cm->tip_ref, xd, plane, plane + 1,
                                tpl_col >> MI_SIZE_LOG2,
                                tpl_row >> MI_SIZE_LOG2);

      tip_component_setup_dst_planes(cm, plane, tpl_row, tpl_col);
      tip_component_build_inter_predictors(
          cm, xd, plane, tip_ref->tip_plane, mv, blk_width, blk_height, tpl_col,
          tpl_row, mc_buf, tmp_conv_dst, calc_subpel_params_func);
#endif  // CONFIG_IMPROVE_REFINED_MV
    }
  }
}

static AOM_INLINE void tip_setup_tip_frame_planes(
    AV1_COMMON *cm, MACROBLOCKD *xd, int blk_row_start, int blk_col_start,
    int blk_row_end, int blk_col_end, int mvs_stride, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_IMPROVE_REFINED_MV
    ,
    int copy_refined_mvs
#endif  // CONFIG_IMPROVE_REFINED_MV
) {
#if CONFIG_IMPROVE_REFINED_MV
  tip_setup_tip_frame_plane(cm, xd, blk_row_start, blk_col_start, blk_row_end,
                            blk_col_end, mvs_stride, TMVP_MI_SIZE,
                            MAX_BLOCK_SIZE_WITH_SAME_MV, mc_buf, tmp_conv_dst,
                            calc_subpel_params_func, copy_refined_mvs);
#else
  const int num_planes = av1_num_planes(cm);
  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane == 0) {
      tip_setup_tip_frame_plane(cm, xd, plane, blk_row_start, blk_col_start,
                                blk_row_end, blk_col_end, mvs_stride,
                                TMVP_MI_SIZE, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                mc_buf, tmp_conv_dst, calc_subpel_params_func);
    } else {
      // TMVP_MI_SIZE_UV is the block size in luma unit for Chroma
      // TIP interpolation, will convert to the step size in TMVP 8x8 unit
      tip_setup_tip_frame_plane(cm, xd, plane, blk_row_start, blk_col_start,
                                blk_row_end, blk_col_end, mvs_stride,
                                TMVP_MI_SIZE_UV, MAX_BLOCK_SIZE_WITH_SAME_MV,
                                mc_buf, tmp_conv_dst, calc_subpel_params_func);
    }
  }
#endif  // CONFIG_IMPROVE_REFINED_MV

  aom_extend_frame_borders(&cm->tip_ref.tip_frame->buf, av1_num_planes(cm));
}

void av1_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd, uint16_t **mc_buf,
                         CONV_BUF_TYPE *tmp_conv_dst,
                         CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_IMPROVE_REFINED_MV
                         ,
                         int copy_refined_mvs
#endif  // CONFIG_IMPROVE_REFINED_MV
) {
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  tip_setup_tip_frame_planes(cm, xd, 0, 0, mvs_rows, mvs_cols, mvs_cols, mc_buf,
                             tmp_conv_dst, calc_subpel_params_func
#if CONFIG_IMPROVE_REFINED_MV
                             ,
                             copy_refined_mvs
#endif  // CONFIG_IMPROVE_REFINED_MV
  );
}

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
#if CONFIG_TMVP_MV_COMPRESSION
      for (int idx = 0; idx < 2; ++idx) {
        if (is_inter_ref_frame(mv->ref_frame[idx])) {
          process_mv_for_tmvp(&mv->mv[idx].as_mv);
        }
      }
#endif  // CONFIG_TMVP_MV_COMPRESSION
      mv++;
      tpl_mv++;
    }
    frame_mvs += mvs_stride;
    tpl_mvs += mvs_stride;
  }
}
