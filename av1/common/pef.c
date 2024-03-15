/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <math.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/av1_loopfilter.h"
#include "av1/common/reconinter.h"
#include "av1/common/seg_common.h"
static const int pef_w_mult[4] = { 85, 51, 37, 28 };
static const int pef_q_mult[4] = { 32, 25, 19, 19 };

void init_pef_parameter(AV1_COMMON *cm, int plane_start, int plane_end) {
  int q_ind[MAX_MB_PLANE], q_ind_r[MAX_MB_PLANE], side_ind[MAX_MB_PLANE],
      side_ind_r[MAX_MB_PLANE];
  PefInfo *const pefi = &cm->pef_info;
  PefParams *const pef = &cm->pef_params;
  const int pef_delta = pef->pef_delta;
  pef->filter_level[0] = 1;
  pef->filter_level[1] = 1;
  pef->filter_level_u = 1;
  pef->filter_level_v = 1;
  pef->delta_q_luma[0] = pef_delta;
  pef->delta_q_luma[1] = pef_delta;
  pef->delta_side_luma[0] = pef_delta;
  pef->delta_side_luma[1] = pef_delta;
  pef->delta_q_u = pef_delta;
  pef->delta_side_u = pef_delta;
  pef->delta_q_v = pef_delta;
  pef->delta_side_v = pef_delta;
  const int base_qindex = cm->quant_params.base_qindex;
  const int u_ac_delta_q = cm->quant_params.u_ac_delta_q;
  const int v_ac_delta_q = cm->quant_params.v_ac_delta_q;

  q_ind[0] = base_qindex + pef->delta_q_luma[0] * PEF_DELTA_SCALE;
  side_ind[0] = base_qindex + pef->delta_side_luma[0] * PEF_DELTA_SCALE;

  q_ind[1] = base_qindex + u_ac_delta_q + pef->delta_q_u * PEF_DELTA_SCALE;
  side_ind[1] =
      base_qindex + u_ac_delta_q + pef->delta_side_u * PEF_DELTA_SCALE;

  q_ind[2] = base_qindex + v_ac_delta_q + pef->delta_q_v * PEF_DELTA_SCALE;
  side_ind[2] =
      base_qindex + v_ac_delta_q + pef->delta_side_v * PEF_DELTA_SCALE;

  q_ind_r[0] = base_qindex + pef->delta_q_luma[1] * PEF_DELTA_SCALE;
  side_ind_r[0] = base_qindex + pef->delta_side_luma[1] * PEF_DELTA_SCALE;

  q_ind_r[1] = base_qindex + u_ac_delta_q + pef->delta_q_u * PEF_DELTA_SCALE;
  side_ind_r[1] =
      base_qindex + u_ac_delta_q + pef->delta_side_u * PEF_DELTA_SCALE;

  q_ind_r[2] = base_qindex + v_ac_delta_q + pef->delta_q_v * PEF_DELTA_SCALE;
  side_ind_r[2] =
      base_qindex + v_ac_delta_q + pef->delta_side_v * PEF_DELTA_SCALE;

  assert(plane_start >= AOM_PLANE_Y);
  assert(plane_end <= MAX_MB_PLANE);
  int plane;
  for (plane = plane_start; plane < plane_end; plane++) {
    if (plane == 0 && !pef->filter_level[0] && !pef->filter_level[1])
      break;
    else if (plane == 1 && !pef->filter_level_u)
      continue;
    else if (plane == 2 && !pef->filter_level_v)
      continue;
    for (int dir = 0; dir < 2; ++dir) {
      const int q_ind_plane = (dir == 0) ? q_ind[plane] : q_ind_r[plane];
      const int side_ind_plane =
          (dir == 0) ? side_ind[plane] : side_ind_r[plane];

      const int q_thr =
          df_quant_from_qindex(q_ind_plane, cm->seq_params.bit_depth);
      const int side_thr =
          df_side_from_qindex(side_ind_plane, cm->seq_params.bit_depth);
      pefi->q_thr[plane][dir] = q_thr >> PEF_THR_SHIFT;
      pefi->side_thr[plane][dir] = side_thr >> PEF_THR_SHIFT;
    }
  }
}

// derive the number of samples to filter
static INLINE int derive_filter_length(uint16_t *s, int pitch,
                                       uint16_t side_thresh, int filt_len) {
  if (filt_len < 1 || filt_len > 3) return 0;
  const int16_t second_deriv_m2 =
      abs(s[-3 * pitch] - (s[-2 * pitch] << 1) + s[-pitch]);
  const int16_t second_deriv_p1 = abs(s[0] - (s[pitch] << 1) + s[2 * pitch]);
  int8_t mask = 0;
  mask |= (second_deriv_m2 > side_thresh);
  mask |= (second_deriv_p1 > side_thresh);
  if (mask)
    return 0;
  else
    return filt_len;
}

// filter vertical prediction samples
static INLINE void filt_vert_pred(int length, uint16_t *s, const int stride,
                                  int bd, uint16_t q_thresh, int q_mult,
                                  int w_mult) {
  if (length < 1) return;
  int delta_m2 = (3 * (s[0] - s[-1]) - (s[stride] - s[-2])) * 4;
  const int q_thresh_clamp = q_thresh * q_mult;
  delta_m2 = clamp(delta_m2, -q_thresh_clamp, q_thresh_clamp);
  delta_m2 *= w_mult;

  for (int i = 0; i < length; i++) {
    const int offset = ROUND_POWER_OF_TWO(delta_m2 * (length - i), PEF_SHIFT);
    s[(-i - 1)] = clip_pixel_highbd(s[(-i - 1)] + offset, bd);
    s[i] = clip_pixel_highbd(s[i] - offset, bd);
  }
}

// filter horizontal prediction samples
static INLINE void filt_horz_pred(int length, uint16_t *s, const int stride,
                                  int bd, uint16_t q_thresh, int q_mult,
                                  int w_mult) {
  if (length < 1) return;
  int delta_m2 =
      (3 * (s[0] - s[-1 * stride]) - (s[stride] - s[-2 * stride])) * 4;
  const int q_thresh_clamp = q_thresh * q_mult;
  delta_m2 = clamp(delta_m2, -q_thresh_clamp, q_thresh_clamp);
  delta_m2 *= w_mult;

  for (int i = 0; i < length; i++) {
    const int offset = ROUND_POWER_OF_TWO(delta_m2 * (length - i), PEF_SHIFT);
    s[(-i - 1) * stride] = clip_pixel_highbd(s[(-i - 1) * stride] + offset, bd);
    s[i * stride] = clip_pixel_highbd(s[i * stride] - offset, bd);
  }
}

// Loop through each sample on the vertical edge
void highbd_filt_vert_pred_c(uint16_t *s, int stride, int bd, uint16_t q_thresh,
                             uint16_t side_thresh, int q_mult, int w_mult,
                             int n, int filt_len) {
  for (int i = 0; i < n; ++i) {
    const int length = derive_filter_length(s, 1, side_thresh, filt_len);
    filt_vert_pred(length, s, 1, bd, q_thresh, q_mult, w_mult);
    s += stride;
  }
}

// Loop through each sample on the horizontal edge
void highbd_filt_horz_pred_c(uint16_t *s, int stride, int bd, uint16_t q_thresh,
                             uint16_t side_thresh, int q_mult, int w_mult,
                             int n, int filt_len) {
  for (int i = 0; i < n; ++i) {
    const int length = derive_filter_length(s, stride, side_thresh, filt_len);
    filt_horz_pred(length, s, stride, bd, q_thresh, q_mult, w_mult);
    ++s;
  }
}

// setup PEF input structure
void setup_pef_input(MACROBLOCKD *xd, int pef_mode, int plane, uint16_t *dst,
                     int dst_stride, int bw, int bh, int_mv *mv_refined,
#if CONFIG_REFINEMV
                     REFINEMV_SUBMB_INFO *refinemv_subinfo,
#endif  // CONFIG_REFINEMV
                     PefFuncInput *pef_input) {
  pef_input->pef_mode = pef_mode;
  pef_input->plane = plane;
  pef_input->bw = bw;
  pef_input->bh = bh;
  pef_input->ss_x = xd->plane[plane].subsampling_x;
  pef_input->ss_y = xd->plane[plane].subsampling_y;
  pef_input->bit_depth = xd->bd;
  pef_input->dst = dst;
  pef_input->dst_stride = dst_stride;
  pef_input->mv_refined = mv_refined;
#if CONFIG_REFINEMV
  pef_input->refinemv_subinfo = refinemv_subinfo;
#endif  // CONFIG_REFINEMV
}

// check if the neighboring mvs are the same
void check_mv(bool *diff_mv, int pef_mode, int mv_rows, int mv_cols,
              int mvs_stride, const TPL_MV_REF *tpl_mvs, int tip_step,
#if !CONFIG_TIP_REF_PRED_MERGING
              int n_blocks, int_mv *mv_refined, int opfl_step
#else
              int offset, int_mv *mv_refined, int step
#endif  // !CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
              ,
              REFINEMV_SUBMB_INFO *refinemv_subinfo, int refinemv_step
#endif  // CONFIG_REFINEMV
) {
#if CONFIG_EXT_WARP_FILTER
  if (pef_mode < 0 || pef_mode > 4) return;
#else
#if CONFIG_REFINEMV
  if (pef_mode < 0 || pef_mode > 3) return;
#else
  if (pef_mode < 0 || pef_mode > 2) return;
#endif  // CONFIG_REFINEMV
#endif  // CONFIG_EXT_WARP_FILTER
#if CONFIG_TIP_REF_PRED_MERGING
  if (pef_mode == 0 || pef_mode == 1) {  // opfl mv || refined tip mv
    const int_mv *cur_mv_refined_ref0 = &mv_refined[offset];
    const int_mv *cur_mv_refined_ref1 = &mv_refined[offset + 1];
    *diff_mv =
        cur_mv_refined_ref0[0].as_int != cur_mv_refined_ref0[-step].as_int;
    *diff_mv |=
        cur_mv_refined_ref1[0].as_int != cur_mv_refined_ref1[-step].as_int;
#else
  if (pef_mode == 0) {  // opfl mv
    const int_mv *cur_mv_refined_ref0 = &mv_refined[n_blocks * 2 + 0];
    const int_mv *cur_mv_refined_ref1 = &mv_refined[n_blocks * 2 + 1];
    *diff_mv =
        cur_mv_refined_ref0[0].as_int != cur_mv_refined_ref0[-opfl_step].as_int;
    *diff_mv |=
        cur_mv_refined_ref1[0].as_int != cur_mv_refined_ref1[-opfl_step].as_int;
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
  } else if (pef_mode == 3) {  // refinemv mv
    const int_mv *cur_mv_refined_ref0 = &refinemv_subinfo->refinemv[0];
    const int_mv *cur_mv_refined_ref1 = &refinemv_subinfo->refinemv[1];
    const int_mv *prev_mv_refined_ref0 =
        &refinemv_subinfo[-refinemv_step].refinemv[0];
    const int_mv *prev_mv_refined_ref1 =
        &refinemv_subinfo[-refinemv_step].refinemv[1];
    *diff_mv = cur_mv_refined_ref0[0].as_int != prev_mv_refined_ref0[0].as_int;
    *diff_mv |= cur_mv_refined_ref1[0].as_int != prev_mv_refined_ref1[0].as_int;
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
  } else if (pef_mode == 4) {  // Extended warp filter
    // Filter every 4x4 unit boundary
    *diff_mv = true;
#endif      // CONFIG_EXT_WARP_FILTER
  } else {  // tip mv
    const TPL_MV_REF *cur_tpl_mv = tpl_mvs + mv_rows * mvs_stride + mv_cols;
    const TPL_MV_REF *prev_tpl_mv = cur_tpl_mv - tip_step;
    *diff_mv = cur_tpl_mv->mfmv0.as_int != prev_tpl_mv->mfmv0.as_int;
  }
  return;
}

// main function for enhancing prediction block boundaries
static INLINE void enhance_sub_prediction_blocks(const AV1_COMMON *cm,
                                                 MACROBLOCKD *xd,
                                                 PefFuncInput *pef_input) {
  const int bw = pef_input->bw;
  const int bh = pef_input->bh;
  const int pef_mode = pef_input->pef_mode;
  if (pef_mode == 1 && (bw < PEF_MCU_SZ || bh < PEF_MCU_SZ)) return;
  const int plane = pef_input->plane;

  int n = PEF_MCU_SZ;  // n is motion compensation unit size
#if CONFIG_OPTFLOW_REFINEMENT
  if (pef_mode == 0)
    n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                               ,
                               1
#endif  // CONFIG_OPTFLOW_ON_TIP
    );
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_EXT_WARP_FILTER
  if (pef_mode == 4) n = PEF_MCU_SZ / 2;
#endif  // CONFIG_EXT_WARP_FILTER

  const int bit_depth = pef_input->bit_depth;
  const int dst_stride = pef_input->dst_stride;
  const int ss_x = pef_input->ss_x;
  const int ss_y = pef_input->ss_y;
  uint16_t *dst = pef_input->dst;

  // retrieve filter parameters
  const PefInfo *pefi = &cm->pef_info;
  const uint16_t q_horz = pefi->q_thr[plane][HORZ_EDGE];
  const uint16_t side_horz = pefi->side_thr[plane][HORZ_EDGE];
  const uint16_t q_vert = pefi->q_thr[plane][VERT_EDGE];
  const uint16_t side_vert = pefi->side_thr[plane][VERT_EDGE];
  const bool filter_horz = q_horz && side_horz;
  const bool filter_vert = q_vert && side_vert;
  if (!filter_horz && !filter_vert) return;
  const int filt_len = n == PEF_MCU_SZ ? 3 : 1;
  const int q_mult = pef_q_mult[filt_len - 1];
  const int w_mult = pef_w_mult[filt_len - 1];

  // derive blockiness location
#if !CONFIG_TIP_REF_PRED_MERGING
  int x_offset = 0;
  int y_offset = 0;
#endif  // !CONFIG_TIP_REF_PRED_MERGING
  int max_tpl_row = 0;
  int max_tpl_col = 0;
  int tpl_start_row = 0;
  int tpl_start_col = 0;
  int tpl_offset = 0;
  int mvs_stride = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const TPL_MV_REF *tpl_mvs_base = cm->tpl_mvs;

  if (pef_mode == 1) {
#if CONFIG_TIP_REF_PRED_MERGING
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    int ref_start_pixel_row = (mi_row << MI_SIZE_LOG2);
    int ref_start_pixel_col = (mi_col << MI_SIZE_LOG2);

    max_tpl_row = ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
    max_tpl_col = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
    tpl_start_row = ref_start_pixel_row >> TMVP_MI_SZ_LOG2;
    tpl_start_col = ref_start_pixel_col >> TMVP_MI_SZ_LOG2;
#else
    const int frame_w = xd->plane[0].dst.width;
    const int frame_h = xd->plane[0].dst.height;
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    const MV *mv = &xd->mi[0]->mv[0].as_mv;
    const FULLPEL_MV start_mv = get_fullmv_from_mv(mv);
    int ref_start_pixel_row = (mi_row << MI_SIZE_LOG2) + start_mv.row;
    int ref_start_pixel_col = (mi_col << MI_SIZE_LOG2) + start_mv.col;
    ref_start_pixel_row = AOMMAX(0, ref_start_pixel_row);
    ref_start_pixel_col = AOMMAX(0, ref_start_pixel_col);
    ref_start_pixel_row = AOMMIN(frame_h, ref_start_pixel_row);
    ref_start_pixel_col = AOMMIN(frame_w, ref_start_pixel_col);
    x_offset = (ref_start_pixel_col >> ss_x) % n;
    y_offset = (ref_start_pixel_row >> ss_y) % n;
    if (x_offset) x_offset = n - x_offset;
    if (y_offset) y_offset = n - y_offset;

    max_tpl_row = frame_h >> TMVP_MI_SZ_LOG2;
    max_tpl_col = frame_w >> TMVP_MI_SZ_LOG2;
    tpl_start_row = ref_start_pixel_row >> TMVP_MI_SZ_LOG2;
    tpl_start_col = ref_start_pixel_col >> TMVP_MI_SZ_LOG2;
    tpl_offset = tpl_start_row * mvs_stride + tpl_start_col;
#endif  // CONFIG_TIP_REF_PRED_MERGING
  }

  const TPL_MV_REF *tpl_mvs = tpl_mvs_base + tpl_offset;

  // initialize x_step and y_step based on blockiness location
#if CONFIG_TIP_REF_PRED_MERGING
  int last_col_x_step = n;
  int x_step = n;

  int last_row_y_step = n;
  int y_step = n;
#else
  int first_col_x_step = n;
  int last_col_x_step = n;
  int x_step = n;
  if (x_offset) {
    first_col_x_step = x_offset;
    last_col_x_step = n - x_offset;
    x_step = first_col_x_step;
  }

  int first_row_y_step = n;
  int last_row_y_step = n;
  int y_step = n;
  if (y_offset) {
    first_row_y_step = y_offset;
    last_row_y_step = n - y_offset;
    y_step = first_row_y_step;
  }
#endif  // CONFIG_TIP_REF_PRED_MERGING

  // start filtering
  const int wn = bw / n;
  const int h = bh - last_row_y_step;
  const int w = bw - last_col_x_step;
  const int rw = bw - (bw % n);
  int prev_y_step = y_step;
  int prev_x_step = x_step;
  int n_blocks = 0;
  int mv_rows = 0;
  for (int j = 0; j <= h; j += y_step) {
#if !CONFIG_TIP_REF_PRED_MERGING
    if (pef_mode == 1) {
      // update y_step based on current j
      prev_y_step = y_step;
      if (j == h)
        y_step = last_row_y_step;
      else if (j > 0)
        y_step = n;
      else  // j == 0
        y_step = first_row_y_step;
    }
#endif  // !CONFIG_TIP_REF_PRED_MERGING

    int mv_cols = 0;
    for (int i = 0; i <= w; i += x_step) {
#if !CONFIG_TIP_REF_PRED_MERGING
      if (pef_mode == 1) {
        // update x_step based on current i
        prev_x_step = x_step;
        if (i == w)
          x_step = last_col_x_step;
        else if (i > 0)
          x_step = n;
        else  // i == 0
          x_step = first_col_x_step;
      }
#endif  // !CONFIG_TIP_REF_PRED_MERGING

      bool within_tpl_boundary = 1;
      if (pef_mode == 1) {
        within_tpl_boundary = (tpl_start_col + mv_cols) < max_tpl_col &&
                              (tpl_start_row + mv_rows) < max_tpl_row;
      }

      const int luma_j = (j << ss_y);
      const int luma_i = (i << ss_x);
#if CONFIG_TIP_REF_PRED_MERGING
      const int mv_offset =
          (pef_mode == 1) ? 2 * ((luma_j >> TMVP_MI_SZ_LOG2) * TIP_MV_STRIDE +
                                 (luma_i >> TMVP_MI_SZ_LOG2))
                          : 2 * n_blocks;
#endif  // CONFIG_TIP_REF_PRED_MERGING

      // filter vertical boundary
      if (filter_vert && i > 0 && within_tpl_boundary &&
          AOMMIN(prev_x_step, x_step) >= filt_len) {
        bool diff_mv = 0;
        check_mv(&diff_mv, pef_mode, mv_rows, mv_cols, mvs_stride, tpl_mvs, 1
#if CONFIG_TIP_REF_PRED_MERGING
                 ,
                 mv_offset, pef_input->mv_refined, 2
#else
                 ,
                 n_blocks, pef_input->mv_refined, 2
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
                 ,
                 (pef_mode == 3) ? (pef_input->refinemv_subinfo +
                                    (luma_j >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                    (luma_i >> MI_SIZE_LOG2))
                                 : NULL,
                 1
#endif  // CONFIG_REFINEMV
        );
        if (diff_mv) {
          filt_func filt_vert_func =
              (y_step == PEF_MCU_SZ && x_step == PEF_MCU_SZ)
                  ? highbd_filt_vert_pred
                  : highbd_filt_vert_pred_c;
          filt_vert_func(dst, dst_stride, bit_depth, q_vert, side_vert, q_mult,
                         w_mult, y_step, filt_len);
        }
      }
      // filter horizontal boundary
      if (filter_horz && j > 0 && within_tpl_boundary &&
          AOMMIN(prev_y_step, y_step) >= filt_len) {
        bool diff_mv = 0;
#if CONFIG_TIP_REF_PRED_MERGING
        const int vstep = (pef_mode == 0) ? 2 * wn : 2 * TIP_MV_STRIDE;
#endif  // CONFIG_TIP_REF_PRED_MERGING
        check_mv(&diff_mv, pef_mode, mv_rows, mv_cols, mvs_stride, tpl_mvs,
                 mvs_stride,
#if CONFIG_TIP_REF_PRED_MERGING
                 mv_offset, pef_input->mv_refined, vstep
#else
                 n_blocks, pef_input->mv_refined, 2 * wn
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
                 ,
                 (pef_mode == 3) ? (pef_input->refinemv_subinfo +
                                    (luma_j >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                    (luma_i >> MI_SIZE_LOG2))
                                 : NULL,
                 MAX_MIB_SIZE
#endif  // CONFIG_REFINEMV
        );
        if (diff_mv) {
          filt_func filt_horz_func = x_step == PEF_MCU_SZ
                                         ? highbd_filt_horz_pred
                                         : highbd_filt_horz_pred_c;
          filt_horz_func(dst, dst_stride, bit_depth, q_horz, side_horz, q_mult,
                         w_mult, x_step, filt_len);
        }
      }
      n_blocks++;
      mv_cols += (1 << ss_x);
      dst += x_step;
    }
    mv_rows += (1 << ss_y);
    dst -= rw;
    dst += y_step * dst_stride;
  }
}

// setup dst buffer for each color component
static INLINE void pef_setup_pred_plane(struct buf_2d *dst, uint16_t *src,
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

// setup dst buffer
static AOM_INLINE void pef_component_setup_dst_planes(AV1_COMMON *const cm,
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
  pef_setup_pred_plane(&pd->dst, src->buffers[plane], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->strides[is_uv], tpl_row,
                       tpl_col, NULL, subsampling_x, subsampling_y);
}

void enhance_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd) {
  const int num_planes = av1_num_planes(cm);
  for (int plane = 0; plane < num_planes; ++plane) {
    TIP *tip_ref = &cm->tip_ref;
    pef_component_setup_dst_planes(cm, plane, 0, 0);
    TIP_PLANE *const tip = &tip_ref->tip_plane[plane];
    struct buf_2d *const dst_buf = &tip->dst;
    uint16_t *const dst = dst_buf->buf;
    const int dst_stride = dst_buf->stride;
    PefFuncInput pef_input;
    setup_pef_input(xd, 2, plane, dst, dst_stride, dst_buf->width,
                    dst_buf->height, NULL,
#if CONFIG_REFINEMV

                    NULL,
#endif  // CONFIG_REFINEMV
                    &pef_input);
    enhance_sub_prediction_blocks(cm, xd, &pef_input);
  }
}

void enhance_prediction(const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                        uint16_t *dst, int dst_stride, int bw, int bh
#if CONFIG_OPTFLOW_REFINEMENT
                        ,
                        int_mv *const mv_refined, int use_opfl
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
                        ,
                        int use_refinemv, REFINEMV_SUBMB_INFO *refinemv_subinfo
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
                        ,
                        bool ext_warp_used
#endif  // CONFIG_EXT_WARP_FILTER
) {
  if (!cm->seq_params.enable_pef) return;
  if (!cm->features.allow_pef) return;

  MB_MODE_INFO *mbmi = xd->mi[0];
  const int use_tip = is_tip_ref_frame(mbmi->ref_frame[0]);
  if (use_tip) {
    PefFuncInput pef_input;
    setup_pef_input(xd, 1, plane, dst, dst_stride, bw, bh,
#if CONFIG_TIP_REF_PRED_MERGING
                    mv_refined,
#else
                    NULL,
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
                    NULL,
#endif  // CONFIG_REFINEMV
                    &pef_input);
    enhance_sub_prediction_blocks(cm, xd, &pef_input);
    return;
  }

#if CONFIG_OPTFLOW_REFINEMENT
#if !CONFIG_AFFINE_REFINEMENT
  use_opfl &= (plane == 0);
#endif  // !CONFIG_AFFINE_REFINEMENT
  if (use_opfl) {
    PefFuncInput pef_input;
    setup_pef_input(xd, 0, plane, dst, dst_stride, bw, bh, mv_refined,
#if CONFIG_REFINEMV
                    NULL,
#endif  // CONFIG_REFINEMV
                    &pef_input);
    enhance_sub_prediction_blocks(cm, xd, &pef_input);
    return;
  }
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
  if (use_refinemv) {
    PefFuncInput pef_input;
    setup_pef_input(xd, 3, plane, dst, dst_stride, bw, bh, mv_refined,
                    refinemv_subinfo, &pef_input);
    enhance_sub_prediction_blocks(cm, xd, &pef_input);
    return;
  }
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
  if (ext_warp_used) {
    PefFuncInput pef_input;
    setup_pef_input(xd, 4, plane, dst, dst_stride, bw, bh, 0,
#if CONFIG_REFINEMV
                    NULL,
#endif  // CONFIG_REFINEMV
                    &pef_input);
    enhance_sub_prediction_blocks(cm, xd, &pef_input);
    return;
  }
#endif  // CONFIG_EXT_WARP_FILTER
  return;
}
