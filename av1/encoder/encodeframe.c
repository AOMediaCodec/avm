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

#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/psnr.h"
#include "aom_ports/mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/system_state.h"

#if CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_MISMATCH_DEBUG
#include "av1/common/bru.h"

#include "av1/common/cfl.h"
#include "av1/common/common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/idct.h"
#include "av1/common/mv.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconintra.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"
#include "av1/common/tip.h"
#include "av1/common/warped_motion.h"

#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/global_motion_facade.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/extend.h"
#include "av1/encoder/ml.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/partition_search.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/tpl_model.h"

#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

/*!\cond */
// This is used as a reference when computing the source variance for the
//  purposes of activity masking.
// Eventually this should be replaced by custom no-reference routines,
//  which will be faster.

static const uint16_t AV1_HIGH_VAR_OFFS_8[MAX_SB_SIZE] = {
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128
};

static const uint16_t AV1_HIGH_VAR_OFFS_10[MAX_SB_SIZE] = {
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4,
  128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4, 128 * 4
};

static const uint16_t AV1_HIGH_VAR_OFFS_12[MAX_SB_SIZE] = {
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16, 128 * 16,
  128 * 16, 128 * 16
};
/*!\endcond */

unsigned int av1_high_get_sby_perpixel_variance(const AV1_COMP *cpi,
                                                const struct buf_2d *ref,
                                                BLOCK_SIZE bs, int bd) {
  unsigned int var, sse;
  assert(bd == 8 || bd == 10 || bd == 12);
  const int off_index = (bd - 8) >> 1;
  const uint16_t *high_var_offs[3] = { AV1_HIGH_VAR_OFFS_8,
                                       AV1_HIGH_VAR_OFFS_10,
                                       AV1_HIGH_VAR_OFFS_12 };
  var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride, high_var_offs[off_index], 0,
                           &sse);
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bs]);
}

static unsigned int get_sby_perpixel_diff_variance(const AV1_COMP *const cpi,
                                                   const struct buf_2d *ref,
                                                   int mi_row, int mi_col,
                                                   BLOCK_SIZE bs) {
  unsigned int sse, var;
  uint16_t *last_y;
  const YV12_BUFFER_CONFIG *last = get_ref_frame_yv12_buf(
      &cpi->common, get_closest_pastcur_ref_or_ref0(&cpi->common));

  assert(last != NULL);
  last_y =
      &last->y_buffer[mi_row * MI_SIZE * last->y_stride + mi_col * MI_SIZE];
  var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride, last_y, last->y_stride, &sse);
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bs]);
}

static BLOCK_SIZE get_rd_var_based_fixed_partition(AV1_COMP *cpi, MACROBLOCK *x,
                                                   int mi_row, int mi_col) {
  unsigned int var = get_sby_perpixel_diff_variance(
      cpi, &x->plane[0].src, mi_row, mi_col, BLOCK_64X64);
  if (var < 8)
    return BLOCK_64X64;
  else if (var < 128)
    return BLOCK_32X32;
  else if (var < 2048)
    return BLOCK_16X16;
  else
    return BLOCK_8X8;
}

void av1_setup_src_planes(MACROBLOCK *x, const YV12_BUFFER_CONFIG *src,
                          int mi_row, int mi_col, const int num_planes,
                          const CHROMA_REF_INFO *chroma_ref_info) {
  // Set current frame pointer.
  x->e_mbd.cur_buf = src;

  // We use AOMMIN(num_planes, MAX_MB_PLANE) instead of num_planes to quiet
  // the static analysis warnings.
  for (int i = 0; i < AOMMIN(num_planes, MAX_MB_PLANE); i++) {
    const int is_uv = i > 0;
    setup_pred_plane(&x->plane[i].src, src->buffers[i], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                     mi_col, NULL, x->e_mbd.plane[i].subsampling_x,
                     x->e_mbd.plane[i].subsampling_y, chroma_ref_info);
  }
}

/*!\brief Assigns different quantization parameters to each super
 * block based on its TPL weight.
 *
 * \ingroup tpl_modelling
 *
 * \param[in]     cpi         Top level encoder instance structure
 * \param[in,out] td          Thread data structure
 * \param[in,out] x           Macro block level data for this block.
 * \param[in]     tile_info   Tile infromation / identification
 * \param[in]     mi_row      Block row (in "MI_SIZE" units) index
 * \param[in]     mi_col      Block column (in "MI_SIZE" units) index
 * \param[out]    num_planes  Number of image planes (e.g. Y,U,V)
 *
 * No return value but updates macroblock and thread data relating
 * to the q / q delta to be used.
 */
static AOM_INLINE void setup_delta_q(AV1_COMP *const cpi, ThreadData *td,
                                     MACROBLOCK *const x,
                                     const TileInfo *const tile_info,
                                     int mi_row, int mi_col, int num_planes) {
  AV1_COMMON *const cm = &cpi->common;
#if !CONFIG_DF_DQP
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
#endif  // !CONFIG_DF_DQP
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  assert(delta_q_info->delta_q_present_flag);

  const BLOCK_SIZE sb_size = cm->sb_size;
  // Delta-q modulation based on variance
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, NULL);

  int current_qindex = cm->quant_params.base_qindex;
  if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_PERCEPTUAL) {
    if (DELTA_Q_PERCEPTUAL_MODULATION == 1) {
      const int block_wavelet_energy_level =
          av1_block_wavelet_energy_level(cpi, x, sb_size);
      x->sb_energy_level = block_wavelet_energy_level;
      current_qindex = av1_compute_q_from_energy_level_deltaq_mode(
          cpi, block_wavelet_energy_level);
    } else {
      const int block_var_level = av1_log_block_var(cpi, x, sb_size
#if CONFIG_MIXED_LOSSLESS_ENCODE
                                                    ,
                                                    mi_row, mi_col
#endif  // CONFIG_MIXED_LOSSLESS_ENCODE
      );
      x->sb_energy_level = block_var_level;
      current_qindex =
          av1_compute_q_from_energy_level_deltaq_mode(cpi, block_var_level);
    }
  } else if (cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_OBJECTIVE &&
             cpi->oxcf.algo_cfg.enable_tpl_model) {
    // Setup deltaq based on tpl stats
    current_qindex =
        av1_get_q_for_deltaq_objective(cpi, sb_size, mi_row, mi_col);
  }

  const int delta_q_res = delta_q_info->delta_q_res;
  // Right now aq only works with tpl model. So if tpl is disabled, we set the
  // current_qindex to base_qindex.
  if (cpi->oxcf.algo_cfg.enable_tpl_model &&
      cpi->oxcf.q_cfg.deltaq_mode != NO_DELTA_Q) {
    current_qindex =
        clamp(current_qindex, delta_q_res, 256 - delta_q_info->delta_q_res);
  } else {
    current_qindex = cm->quant_params.base_qindex;
  }

  MACROBLOCKD *const xd = &x->e_mbd;
  const int sign_deltaq_index =
      current_qindex - xd->current_base_qindex >= 0 ? 1 : -1;
  const int deltaq_deadzone = delta_q_res / 4;
  const int qmask = ~(delta_q_res - 1);
  int abs_deltaq_index = abs(current_qindex - xd->current_base_qindex);
  abs_deltaq_index = (abs_deltaq_index + deltaq_deadzone) & qmask;
  current_qindex =
      xd->current_base_qindex + sign_deltaq_index * abs_deltaq_index;
  current_qindex = AOMMAX(current_qindex, MINQ + 1);
  assert(current_qindex > 0);

  x->delta_qindex = current_qindex - cm->quant_params.base_qindex;
  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size, NULL);
  xd->mi[0]->current_qindex = current_qindex;
  av1_init_plane_quantizers(cpi, x, xd->mi[0]->segment_id);

  // keep track of any non-zero delta-q used
  td->deltaq_used |= (x->delta_qindex != 0);

#if !CONFIG_DF_DQP
  if (cpi->oxcf.tool_cfg.enable_deltalf_mode) {
    const int delta_lf_res = delta_q_info->delta_lf_res;
    const int lfmask = ~(delta_lf_res - 1);
    const int delta_lf_from_base =
        ((x->delta_qindex / 2 + delta_lf_res / 2) & lfmask);
    const int8_t delta_lf =
        (int8_t)clamp(delta_lf_from_base, -MAX_LOOP_FILTER, MAX_LOOP_FILTER);
    const int frame_lf_count =
        av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
    const int mib_size = cm->mib_size;

    // pre-set the delta lf for loop filter. Note that this value is set
    // before mi is assigned for each block in current superblock
    for (int j = 0; j < AOMMIN(mib_size, mi_params->mi_rows - mi_row); j++) {
      for (int k = 0; k < AOMMIN(mib_size, mi_params->mi_cols - mi_col); k++) {
        const int grid_idx = get_mi_grid_idx(mi_params, mi_row + j, mi_col + k);
        mi_params->mi_grid_base[grid_idx]->delta_lf_from_base = delta_lf;

        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          mi_params->mi_grid_base[grid_idx]->delta_lf[lf_id] = delta_lf;
        }
      }
    }
  }
#endif  // !CONFIG_DF_DQP
}

static void init_ref_frame_space(AV1_COMP *cpi, ThreadData *td, int mi_row,
                                 int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCK *x = &td->mb;
  const int frame_idx = cpi->gf_group.index;
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[frame_idx];
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;

  av1_zero(x->tpl_keep_ref_frame);

  if (tpl_frame->is_valid == 0) return;
  if (!is_frame_tpl_eligible(gf_group, gf_group->index)) return;
  if (frame_idx >= MAX_TPL_FRAME_IDX) return;
  if (cpi->oxcf.q_cfg.aq_mode != NO_AQ) return;

  const int is_overlay =
      cpi->gf_group.update_type[frame_idx] == OVERLAY_UPDATE ||
      cpi->gf_group.update_type[frame_idx] == KFFLT_OVERLAY_UPDATE;
  if (is_overlay) {
    memset(x->tpl_keep_ref_frame, 1, sizeof(x->tpl_keep_ref_frame));
    return;
  }

  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const int tpl_stride = tpl_frame->stride;
  int64_t inter_cost[INTER_REFS_PER_FRAME] = { 0 };
  const int step = 1 << block_mis_log2;
  const BLOCK_SIZE sb_size = cm->sb_size;

  const int mi_row_end =
      AOMMIN(mi_size_high[sb_size] + mi_row, mi_params->mi_rows);
  const int mi_cols_sr = av1_pixels_to_mi(cm->width);
  const int mi_col_sr = mi_col;
  const int mi_col_end_sr = AOMMIN(mi_col + mi_size_wide[sb_size], mi_cols_sr);
  const int row_step = step;
  const int col_step_sr = step;
  for (int row = mi_row; row < mi_row_end; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      const TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      int64_t tpl_pred_error[INTER_REFS_PER_FRAME] = { 0 };
      // Find the winner ref frame idx for the current block
      int64_t best_inter_cost = this_stats->pred_error[0];
      int best_rf_idx = 0;
      for (int idx = 1; idx < INTER_REFS_PER_FRAME; ++idx) {
        if ((this_stats->pred_error[idx] < best_inter_cost) &&
            (this_stats->pred_error[idx] != 0)) {
          best_inter_cost = this_stats->pred_error[idx];
          best_rf_idx = idx;
        }
      }
      // tpl_pred_error is the pred_error reduction of best_ref w.r.t.
      // rank 0 frame.
      tpl_pred_error[best_rf_idx] =
          this_stats->pred_error[best_rf_idx] - this_stats->pred_error[0];

      for (int rf_idx = 1; rf_idx < INTER_REFS_PER_FRAME; ++rf_idx)
        inter_cost[rf_idx] += tpl_pred_error[rf_idx];
    }
  }

  int rank_index[INTER_REFS_PER_FRAME - 1];
  for (int idx = 0; idx < INTER_REFS_PER_FRAME - 1; ++idx) {
    rank_index[idx] = idx + 1;
    for (int i = idx; i > 0; --i) {
      if (inter_cost[rank_index[i - 1]] > inter_cost[rank_index[i]]) {
        const int tmp = rank_index[i - 1];
        rank_index[i - 1] = rank_index[i];
        rank_index[i] = tmp;
      }
    }
  }

  x->tpl_keep_ref_frame[INTRA_FRAME_INDEX] = 1;
  x->tpl_keep_ref_frame[0] = 1;

  int cutoff_ref = 0;
  for (int idx = 0; idx < INTER_REFS_PER_FRAME - 1; ++idx) {
    x->tpl_keep_ref_frame[rank_index[idx]] = 1;
    if (idx > 2) {
      if (!cutoff_ref) {
        // If the predictive coding gains are smaller than the previous more
        // relevant frame over certain amount, discard this frame and all the
        // frames afterwards.
        if (llabs(inter_cost[rank_index[idx]]) <
                llabs(inter_cost[rank_index[idx - 1]]) / 8 ||
            inter_cost[rank_index[idx]] == 0)
          cutoff_ref = 1;
      }

      if (cutoff_ref) x->tpl_keep_ref_frame[rank_index[idx]] = 0;
    }
  }
}

static AOM_INLINE void adjust_rdmult_tpl_model(AV1_COMP *cpi, MACROBLOCK *x,
                                               int mi_row, int mi_col) {
  const BLOCK_SIZE sb_size = cpi->common.sb_size;
  const int orig_rdmult = cpi->rd.RDMULT;

  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));
  const int gf_group_index = cpi->gf_group.index;
  if (cpi->oxcf.algo_cfg.enable_tpl_model && cpi->oxcf.q_cfg.aq_mode == NO_AQ &&
      cpi->oxcf.q_cfg.deltaq_mode == NO_DELTA_Q && gf_group_index > 0 &&
      (cpi->gf_group.update_type[gf_group_index] == ARF_UPDATE ||
       cpi->gf_group.update_type[gf_group_index] == KFFLT_UPDATE)) {
    const int dr =
        av1_get_rdmult_delta(cpi, sb_size, mi_row, mi_col, orig_rdmult);
    x->rdmult = dr;
  }
}

#define AVG_CDF_WEIGHT_LEFT 3
#define AVG_CDF_WEIGHT_TOP_RIGHT 1

static void fill_sms_buf(SimpleMotionDataBufs *data_buf,
                         SIMPLE_MOTION_DATA_TREE *sms_node, int mi_row,
                         int mi_col, BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                         int8_t sdp_flag) {
  SimpleMotionData *sms_data = av1_get_sms_data_entry(data_buf, mi_row, mi_col,
                                                      bsize, sb_size, sdp_flag);
  sms_data->old_sms = sms_node;
  if (bsize >= BLOCK_8X8 && bsize != BLOCK_INVALID) {
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
    for (int r_idx = 0; r_idx < SUB_PARTITIONS_SPLIT; r_idx++) {
      assert(bsize < BLOCK_SIZES_ALL);
      const int w_mi = mi_size_wide[bsize];
      const int h_mi = mi_size_high[bsize];
      const int sub_mi_col = mi_col + (r_idx & 1) * w_mi / 2;
      const int sub_mi_row = mi_row + (r_idx >> 1) * h_mi / 2;
      SIMPLE_MOTION_DATA_TREE *sub_tree = sms_node->split[r_idx];

      fill_sms_buf(data_buf, sub_tree, sub_mi_row, sub_mi_col, subsize, sb_size,
                   sdp_flag);
    }
  }
}

// This function initializes the stats for encode_rd_sb.
static INLINE void init_encode_rd_sb(AV1_COMP *cpi, ThreadData *td,
                                     const TileDataEnc *tile_data,
                                     SIMPLE_MOTION_DATA_TREE *sms_root,
                                     RD_STATS *rd_cost, int mi_row, int mi_col,
                                     int gather_tpl_data) {
  const AV1_COMMON *cm = &cpi->common;
  const TileInfo *tile_info = &tile_data->tile_info;
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  SB_INFO *sbi = xd->sbi;

  const SPEED_FEATURES *sf = &cpi->sf;
  const int use_simple_motion_search =
      (sf->part_sf.simple_motion_search_split ||
       sf->part_sf.simple_motion_search_prune_rect ||
       sf->part_sf.simple_motion_search_early_term_none ||
       sf->part_sf.ml_early_term_after_part_split_level) &&
      !frame_is_intra_only(cm);
  if (use_simple_motion_search) {
    init_simple_motion_search_mvs(sms_root);
  }

  (void)sbi;
  init_ref_frame_space(cpi, td, mi_row, mi_col);
  x->sb_energy_level = 0;
  x->part_search_info.cnn_output_valid = 0;
  if (gather_tpl_data) {
    if (cm->delta_q_info.delta_q_present_flag && xd->tree_type != CHROMA_PART) {
      const int num_planes = av1_num_planes(cm);
      const BLOCK_SIZE sb_size = cm->sb_size;
      setup_delta_q(cpi, td, x, tile_info, mi_row, mi_col, num_planes);
      av1_tpl_rdmult_setup_sb(cpi, x, sb_size, mi_row, mi_col);
    }
    if (cpi->oxcf.algo_cfg.enable_tpl_model) {
      adjust_rdmult_tpl_model(cpi, x, mi_row, mi_col);
    }
  }

  // Reset hash state for transform/mode rd hash information
  reset_hash_records(&x->txfm_search_info, cpi->sf.tx_sf.use_inter_txb_hash);
  av1_zero(x->picked_ref_frames_mask);
  av1_invalid_rd_stats(rd_cost);
  SimpleMotionDataBufs *data_bufs = x->sms_bufs;
  av1_init_sms_data_bufs(data_bufs);
  fill_sms_buf(data_bufs, sms_root, mi_row, mi_col, cm->sb_size, cm->sb_size,
               0);
  fill_sms_buf(data_bufs, sms_root, mi_row, mi_col, cm->sb_size, cm->sb_size,
               1);
  if (x->e_mbd.tree_type == CHROMA_PART) {
    assert(is_bsize_square(x->sb_enc.min_partition_size));
    x->sb_enc.min_partition_size =
        AOMMAX(x->sb_enc.min_partition_size, BLOCK_8X8);
  }
}

/*!\brief Parameters for \ref perform_one_partition_pass to support multiple sb
 * passes.
 * \ingroup partition_search
 * */
typedef struct SbMultiPassParams {
  /*!\brief The reference partition tree template. */
  const PARTITION_TREE *template_tree;
} SbMultiPassParams;

/*!\brief Perform one pass of partition search.
 *
 * \ingroup partition_search
 * This is a helper function used to handle some SDP related logics.
 */
static AOM_INLINE void perform_one_partition_pass(
    AV1_COMP *cpi, ThreadData *td, TileDataEnc *tile_data, TokenExtra **tp,
    TokenExtra **tp_chroma, const int mi_row, const int mi_col,
    const SB_MULTI_PASS_MODE multi_pass_mode,
    const SbMultiPassParams *multi_pass_params) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  SuperBlockEnc *sb_enc = &x->sb_enc;
  SIMPLE_MOTION_DATA_TREE *const sms_root = td->sms_root;
  const BLOCK_SIZE sb_size = cm->sb_size;
  const int ss_x = cm->seq_params.subsampling_x;
  const int ss_y = cm->seq_params.subsampling_y;
  RD_STATS dummy_rdc;
  av1_invalid_rd_stats(&dummy_rdc);
  const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
  const int total_loop_num = intra_sdp_enabled ? 2 : 1;
  x->is_whole_sb = mi_row + mi_size_high[sb_size] <= cm->mi_params.mi_rows &&
                   mi_col + mi_size_wide[sb_size] <= cm->mi_params.mi_cols;
  for (int loop_idx = 0; loop_idx < total_loop_num; loop_idx++) {
    const BLOCK_SIZE min_partition_size = sb_enc->min_partition_size;
    xd->tree_type =
        (total_loop_num == 1 ? SHARED_PART
                             : (loop_idx == 0 ? LUMA_PART : CHROMA_PART));
    init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row, mi_col,
                      1);
    PC_TREE *const pc_root =
        av1_alloc_pc_tree_node(xd->tree_type, mi_row, mi_col, cm->sb_size,
                               sb_size, NULL, PARTITION_NONE, 0, 1, ss_x, ss_y);
    if (!frame_is_intra_only(cm)) {
      pc_root->region_type = MIXED_INTER_INTRA_REGION;
    } else {
      pc_root->region_type = INTRA_REGION;
    }

    const PARTITION_TREE *template_tree =
        multi_pass_params ? multi_pass_params->template_tree : NULL;
    assert(IMPLIES(template_tree, total_loop_num == 1) &&
           "perform_one_partition_pass cannot handle fixed partitioning for "
           "erp yet.");
#if CONFIG_ML_PART_SPLIT
    int force_prune_flags[3] = { 0, 0, 0 };
#endif  // CONFIG_ML_PART_SPLIT
    av1_rd_pick_partition(
        cpi, td, tile_data,
        (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tp_chroma : tp,
        mi_row, mi_col, sb_size, PARTITION_NONE, &dummy_rdc, dummy_rdc, pc_root,
        xd->tree_type == CHROMA_PART ? xd->sbi->ptree_root[0] : NULL,
        template_tree, INT_MAX, sms_root, NULL, multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
        ,
        force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
    );
    sb_enc->min_partition_size = min_partition_size;
  }

  xd->tree_type = SHARED_PART;
  x->is_whole_sb = 0;
}

/*!\brief Perform partition search twice (for unit testing only).
 *
 * \ingroup partition_search
 * This function is mostly used to unit tests to make sure that
 * SB_FIRST_PASS_STATS caches the correct statistics to recode the superblock.
 */
static AOM_INLINE void perform_two_partition_passes(
    AV1_COMP *cpi, ThreadData *td, TileDataEnc *tile_data, TokenExtra **tp,
    TokenExtra **tp_chroma, const int mi_row, const int mi_col) {
  SIMPLE_MOTION_DATA_TREE *const sms_root = td->sms_root;
  AV1_COMMON *const cm = &cpi->common;
  const BLOCK_SIZE sb_size = cm->sb_size;

  // First pass
  SB_FIRST_PASS_STATS sb_fp_stats;
  av1_backup_sb_state(&sb_fp_stats, cpi, td, tile_data, mi_row, mi_col);
  REF_MV_BANK stored_mv_bank = td->mb.e_mbd.ref_mv_bank;
#if WARP_CU_BANK
  WARP_PARAM_BANK stored_warp_bank = td->mb.e_mbd.warp_param_bank;
#endif  // WARP_CU_BANK
  perform_one_partition_pass(cpi, td, tile_data, tp, tp_chroma, mi_row, mi_col,
                             SB_DRY_PASS, NULL);

  // Second pass
  RD_STATS dummy_rdc;
  init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row, mi_col,
                    0);
  av1_reset_mbmi(&cm->mi_params, sb_size, mi_row, mi_col);
  av1_reset_simple_motion_tree_partition(sms_root, sb_size);

  av1_restore_sb_state(&sb_fp_stats, cpi, td, tile_data, mi_row, mi_col);
  td->mb.e_mbd.ref_mv_bank = stored_mv_bank;
#if WARP_CU_BANK
  td->mb.e_mbd.warp_param_bank = stored_warp_bank;
#endif  // WARP_CU_BANK
  perform_one_partition_pass(cpi, td, tile_data, tp, tp_chroma, mi_row, mi_col,
                             SB_WET_PASS, NULL);
}

/*!\brief Set all tree nodes <= min_bsize to PARTITION_INVALID.
 *
 * \ingroup partition_search
 */
static AOM_INLINE void set_min_none_to_invalid(PARTITION_TREE *part_tree,
                                               BLOCK_SIZE min_bsize) {
  const BLOCK_SIZE bsize = part_tree->bsize;
  const PARTITION_TYPE part_type = part_tree->partition;
  if (!is_bsize_geq(bsize, min_bsize)) {
    part_tree->partition = PARTITION_INVALID;
    for (int idx = 0; idx < 4; idx++) {
      av1_free_ptree_recursive(part_tree->sub_tree[idx]);
      part_tree->sub_tree[idx] = NULL;
    }

    return;
  }

  int num_subtrees = 0;
  switch (part_type) {
    case PARTITION_NONE: num_subtrees = 0; break;
    case PARTITION_HORZ:
    case PARTITION_VERT: num_subtrees = 2; break;
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B:
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3:
    case PARTITION_SPLIT: num_subtrees = 4; break;
    default:
      assert(0 && "Invalid partition type in set_min_none_to_invalid!");
      return;
  }

  for (int idx = 0; idx < num_subtrees; idx++) {
    set_min_none_to_invalid(part_tree->sub_tree[idx], min_bsize);
  }
}

/*!\brief Performs partition search in two passes.
 *
 * \ingroup partition_search
 * In the first pass, partition search is performed with the
 * minimum bsize set to BLOCK_16X16. In the second pass, partition search is
 * performed with the same partition tree from the first pass, but partition
 * search is allowed to search recursively starting from BLOCK_32X32.
 */
static AOM_INLINE void perform_two_pass_partition_search(
    AV1_COMP *cpi, ThreadData *td, TileDataEnc *tile_data, TokenExtra **tp,
    TokenExtra **tp_chroma, const int mi_row, const int mi_col) {
  SIMPLE_MOTION_DATA_TREE *const sms_root = td->sms_root;
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  const BLOCK_SIZE sb_size = cm->sb_size;
  assert(!frame_is_intra_only(cm));

  // First pass to estimate  partition structures
  SB_FIRST_PASS_STATS sb_fp_stats;
  av1_backup_sb_state(&sb_fp_stats, cpi, td, tile_data, mi_row, mi_col);
  const BLOCK_SIZE fp_min_bsize = BLOCK_16X16;
  x->sb_enc.min_partition_size = fp_min_bsize;
  perform_one_partition_pass(cpi, td, tile_data, tp, tp_chroma, mi_row, mi_col,
                             SB_DRY_PASS, NULL);
  PARTITION_TREE *part_ref = xd->sbi->ptree_root[0];
  // Set this to NULL otherwise part_ref will get freed in the second pass.
  xd->sbi->ptree_root[0] = NULL;
  set_min_none_to_invalid(part_ref, get_larger_sqr_bsize(fp_min_bsize));

  // Second pass
  RD_STATS dummy_rdc;
  init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row, mi_col,
                    0);
  av1_reset_mbmi(&cm->mi_params, sb_size, mi_row, mi_col);
  av1_reset_simple_motion_tree_partition(sms_root, sb_size);

  SbMultiPassParams multi_pass_params = { part_ref };
  av1_restore_sb_state(&sb_fp_stats, cpi, td, tile_data, mi_row, mi_col);
  perform_one_partition_pass(cpi, td, tile_data, tp, tp_chroma, mi_row, mi_col,
                             SB_WET_PASS, &multi_pass_params);

  av1_free_ptree_recursive(part_ref);
}

/*!\brief Encode a superblock (RD-search-based)
 *
 * \ingroup partition_search
 * Conducts partition search for a superblock, based on rate-distortion costs,
 * from scratch or adjusting from a pre-calculated partition pattern.
 */
static AOM_INLINE void encode_rd_sb(AV1_COMP *cpi, ThreadData *td,
                                    TileDataEnc *tile_data, TokenExtra **tp,
                                    TokenExtra **tp_chroma, const int mi_row,
                                    const int mi_col, const int seg_skip) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const TileInfo *const tile_info = &tile_data->tile_info;
  MB_MODE_INFO **mi = cm->mi_params.mi_grid_base +
                      get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
  const BLOCK_SIZE sb_size = cm->sb_size;
  const int num_planes = av1_num_planes(cm);
  int dummy_rate;
  int64_t dummy_dist;
  RD_STATS dummy_rdc;
  SIMPLE_MOTION_DATA_TREE *const sms_root = td->sms_root;
  const int ss_x = cm->seq_params.subsampling_x;
  const int ss_y = cm->seq_params.subsampling_y;
  (void)tile_info;
  (void)num_planes;
  (void)mi;

  const int total_loop_num = is_sdp_enabled_in_keyframe(cm) ? 2 : 1;
  MACROBLOCKD *const xd = &x->e_mbd;
  x->e_mbd.sbi->sb_mv_precision = cm->features.fr_mv_precision;

  x->sms_bufs = td->sms_bufs;
  x->reuse_inter_mode_cache_type = cpi->sf.inter_sf.reuse_erp_mode_flag;
  init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row, mi_col,
                    1);
  const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);

  // Encode the superblock
  if (sf->part_sf.partition_search_type == FIXED_PARTITION || seg_skip) {
    // partition search by adjusting a fixed-size partition
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size, NULL);
    const BLOCK_SIZE bsize =
        seg_skip ? sb_size : sf->part_sf.fixed_partition_size;
    av1_set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
    for (int loop_idx = 0; loop_idx < total_loop_num; loop_idx++) {
      const BLOCK_SIZE min_partition_size = x->sb_enc.min_partition_size;
      xd->tree_type =
          (total_loop_num == 1 ? SHARED_PART
                               : (loop_idx == 0 ? LUMA_PART : CHROMA_PART));
      init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row,
                        mi_col, 1);
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
      av1_build_partition_tree_fixed_partitioning(
          cm, xd->tree_type, mi_row, mi_col, bsize,
          xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
          xd->tree_type == CHROMA_PART ? xd->sbi->ptree_root[0] : NULL);
      PC_TREE *const pc_root = av1_alloc_pc_tree_node(
          xd->tree_type, mi_row, mi_col, cm->sb_size, sb_size, NULL,
          PARTITION_NONE, 0, 1, ss_x, ss_y);
      av1_rd_use_partition(
          cpi, td, tile_data, mi,
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tp_chroma : tp,
          mi_row, mi_col, sb_size, &dummy_rate, &dummy_dist, 1,
          xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)], pc_root,
          (xd->tree_type == CHROMA_PART) ? xd->sbi->ptree_root[0] : NULL);
      av1_free_pc_tree_recursive(pc_root, num_planes, 0, 0);
      x->sb_enc.min_partition_size = min_partition_size;
    }
    xd->tree_type = SHARED_PART;
  } else if (cpi->partition_search_skippable_frame) {
    // partition search by adjusting a fixed-size partition for which the size
    // is determined by the source variance
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, sb_size, NULL);
    const BLOCK_SIZE bsize =
        get_rd_var_based_fixed_partition(cpi, x, mi_row, mi_col);
    av1_set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
    for (int loop_idx = 0; loop_idx < total_loop_num; loop_idx++) {
      const BLOCK_SIZE min_partition_size = x->sb_enc.min_partition_size;
      xd->tree_type =
          (total_loop_num == 1 ? SHARED_PART
                               : (loop_idx == 0 ? LUMA_PART : CHROMA_PART));
      init_encode_rd_sb(cpi, td, tile_data, sms_root, &dummy_rdc, mi_row,
                        mi_col, 1);
      PC_TREE *const pc_root = av1_alloc_pc_tree_node(
          xd->tree_type, mi_row, mi_col, cm->sb_size, sb_size, NULL,
          PARTITION_NONE, 0, 1, ss_x, ss_y);
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
      av1_build_partition_tree_fixed_partitioning(
          cm, xd->tree_type, mi_row, mi_col, bsize,
          xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
          xd->tree_type == CHROMA_PART ? xd->sbi->ptree_root[0] : NULL);
      av1_rd_use_partition(
          cpi, td, tile_data, mi,
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tp_chroma : tp,
          mi_row, mi_col, sb_size, &dummy_rate, &dummy_dist, 1,
          xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)], pc_root,
          (xd->tree_type == CHROMA_PART) ? xd->sbi->ptree_root[0] : NULL);
      av1_free_pc_tree_recursive(pc_root, num_planes, 0, 0);
      x->sb_enc.min_partition_size = min_partition_size;
    }
    xd->tree_type = SHARED_PART;
  } else {
    // The most exhaustive recursive partition search
    SuperBlockEnc *sb_enc = &x->sb_enc;
    // No stats for overlay frames. Exclude key frame.
    av1_get_tpl_stats_sb(cpi, sb_size, mi_row, mi_col, sb_enc);

    // Reset the tree for simple motion search data
    av1_reset_simple_motion_tree_partition(sms_root, sb_size);

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, rd_pick_partition_time);
#endif

    // Estimate the maximum square partition block size, which will be used
    // as the starting block size for partitioning the sb
    set_max_min_partition_size(sb_enc, cpi, x, sf, sb_size, mi_row, mi_col);

    // Sets the sb_mv_precision
    x->e_mbd.sbi->sb_mv_precision = cm->features.fr_mv_precision;

    if (cpi->oxcf.unit_test_cfg.sb_multipass_unit_test) {
      perform_two_partition_passes(cpi, td, tile_data, tp, tp_chroma, mi_row,
                                   mi_col);
    } else if (!frame_is_intra_only(cm) &&
               bru_is_sb_active(cm, mi_col, mi_row) &&
               sf->part_sf.two_pass_partition_search) {
      perform_two_pass_partition_search(cpi, td, tile_data, tp, tp_chroma,
                                        mi_row, mi_col);
    } else {
      perform_one_partition_pass(cpi, td, tile_data, tp, tp_chroma, mi_row,
                                 mi_col, SB_SINGLE_PASS, NULL);
    }

    // Reset to 0 so that it wouldn't be used elsewhere mistakenly.
    sb_enc->tpl_data_count = 0;
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, rd_pick_partition_time);
#endif
  }

  if (cm->bru.enabled && cm->current_frame.frame_type != KEY_FRAME) {
    if (bru_is_sb_available(cm, mi_col, mi_row)) {
      assert(get_ref_frame_buf(cm, cm->bru.update_ref_idx) != NULL);
      bru_update_sb(cm, mi_col, mi_row);
    }
  }
  // Update the inter rd model
  // TODO(angiebird): Let inter_mode_rd_model_estimation support multi-tile.
  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1 &&
      cm->tiles.cols == 1 && cm->tiles.rows == 1) {
    av1_inter_mode_data_fit(tile_data, x->rdmult);
  }
}

#if CONFIG_CWG_F317
static AOM_INLINE void bridge_frame_set_offsets(
    AV1_COMMON *const cm, MACROBLOCKD *const xd, BLOCK_SIZE bsize, int mi_row,
    int mi_col, PARTITION_TREE *parent, int index, PARTITION_TYPE partition) {
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];

  const int num_planes = av1_num_planes(cm);
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const TileInfo *const tile = &xd->tile;

  const int mi_grid_idx = get_mi_grid_idx(mi_params, mi_row, mi_col);
  xd->mi = mi_params->mi_grid_base + mi_grid_idx;
  xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART] = bsize;
  if (xd->tree_type != CHROMA_PART) {
    xd->mi[0]->mi_row_start = mi_row;
    xd->mi[0]->mi_col_start = mi_col;
  }
  xd->mi_row = mi_row;
  xd->mi_col = mi_col;

  CHROMA_REF_INFO *chroma_ref_info = &xd->mi[0]->chroma_ref_info;
  set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, bsize,
                      chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->bsize : BLOCK_INVALID,
                      parent ? parent->partition : PARTITION_NONE,
                      xd->plane[1].subsampling_x, xd->plane[1].subsampling_y);
  set_plane_n4(xd, bw, bh, num_planes, chroma_ref_info);

  // Distance of Mb to the various image edges. These are specified to 8th pel
  // as they are always compared to values that are in 1/8th pel units
  set_mi_row_col(
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      cm,
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows, mi_params->mi_cols,
      chroma_ref_info);

  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);

  xd->mi[0]->partition = partition;
  // set region_type for each mbmi
  xd->mi[0]->region_type =
      (parent == NULL) ? MIXED_INTER_INTRA_REGION : parent->region_type;
  // set tree_type for each mbmi
  xd->mi[0]->tree_type = xd->tree_type;
  xd->mi[0]->sb_root_partition_info =
      (parent == NULL) ? -1 : parent->sb_root_partition_info;
  xd->mi[0]->local_rest_type =
      1;  // set non zero default type, it is only matter 1 or 0 in SW
  xd->mi[0]->local_ccso_blk_flag =
      1;  // set non zero default type, it is only matter 1 or 0 in SW
  xd->mi[0]->local_gdf_mode =
      1;  // set non zero default type, it is only matter 1 or 0 in SW

  if (xd->tree_type != LUMA_PART) {
    const struct macroblockd_plane *const pd_u = &xd->plane[1];
    const BLOCK_SIZE chroma_bsize_base =
        get_bsize_base(xd, xd->mi[0], AOM_PLANE_U);
    assert(chroma_bsize_base < BLOCK_SIZES_ALL);
    if (get_plane_block_size(chroma_bsize_base, pd_u->subsampling_x,
                             pd_u->subsampling_y) == BLOCK_INVALID) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Block size %dx%d invalid with this subsampling mode",
                         block_size_wide[chroma_bsize_base],
                         block_size_high[chroma_bsize_base]);
    }
  }
}

static AOM_INLINE void bridge_frame_predict_inter_block(AV1_COMMON *const cm,
                                                        MACROBLOCKD *const xd) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int num_planes = av1_num_planes(cm);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  for (int ref = 0; ref < 1; ++ref) {
    const MV_REFERENCE_FRAME frame = mbmi->ref_frame[ref];
    const RefCntBuffer *ref_buf = is_tip_ref_frame(mbmi->ref_frame[0])
                                      ? cm->tip_ref.ref_frame_buffer[ref]
                                      : get_ref_frame_buf(cm, frame);
    const struct scale_factors *ref_scale_factors =
        get_ref_scale_factors_const(cm, frame);

    xd->block_ref_scale_factors[ref] = ref_scale_factors;
    av1_setup_pre_planes(xd, ref, &ref_buf->buf, mi_row, mi_col,
                         ref_scale_factors, num_planes, &mbmi->chroma_ref_info);
  }

  struct macroblockd_plane *p = xd->plane;
  const BUFFER_SET orig_dst = {
    { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
    { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
  };

  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const int mi_x = mi_col * MI_SIZE;
    const int mi_y = mi_row * MI_SIZE;

    enc_build_inter_predictors(cm, xd, plane, xd->mi[0], &orig_dst, 0,
                               xd->plane[plane].width, xd->plane[plane].height,
                               mi_x, mi_y);
  }
}

static PARTITION_TYPE bridge_frame_read_partition(
    const AV1_COMMON *const cm, MACROBLOCKD *xd, int mi_row, int mi_col,
    int has_rows, int has_cols, const PARTITION_TREE *ptree,
    const PARTITION_TREE *ptree_luma, BLOCK_SIZE bsize) {
  (void)has_rows;
  (void)has_cols;
  const int ssx = cm->seq_params.subsampling_x;
  const int ssy = cm->seq_params.subsampling_y;
  PARTITION_TYPE derived_partition = av1_get_normative_forced_partition_type(
      &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
      ptree_luma);

  bool partition_allowed[ALL_PARTITION_TYPES];
  init_allowed_partitions_for_signaling(
      partition_allowed, cm, xd->tree_type,
      (ptree->parent ? ptree->parent->region_type : INTRA_REGION), mi_row,
      mi_col, ssx, ssy, bsize, &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID &&
      partition_allowed[derived_partition]) {
    return derived_partition;
  }

  derived_partition = only_allowed_partition(partition_allowed);
  if (derived_partition != PARTITION_INVALID) {
    return derived_partition;
  }

  return PARTITION_NONE;
}

static AOM_INLINE void bridge_frame_decode_partition(
    AV1_COMP *cpi, ThreadData *const td, const TileInfo *const tile, int mi_row,
    int mi_col, BLOCK_SIZE bsize, SB_INFO *sbi, PARTITION_TREE *ptree,
    PARTITION_TREE *ptree_luma) {
  assert(bsize < BLOCK_SIZES_ALL);
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;
  // Half block width/height.
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  // One-eighth block width/height.
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
  PARTITION_TYPE partition;
  const int has_rows = (mi_row + hbs_h) < cm->mi_params.mi_rows;
  const int has_cols = (mi_col + hbs_w) < cm->mi_params.mi_cols;

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;

  const int is_intra_sdp_enabled = 0;

  const int is_sb_root = bsize == cm->sb_size;
  if (is_sb_root) {
    xd->is_cfl_allowed_in_sdp =
        is_cfl_allowed_for_sdp(cm, xd, ptree_luma, PARTITION_NONE, bsize);
    ptree->is_cfl_allowed_for_this_chroma_partition = CFL_DISALLOWED_FOR_CHROMA;
    ptree->region_type = MIXED_INTER_INTRA_REGION;
    ptree->extended_sdp_allowed_flag = cm->seq_params.enable_extended_sdp;
  }

  if (is_sb_root) {
    const int mi_grid_idx = get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
    const int mi_alloc_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
    cm->mi_params.mi_grid_base[mi_grid_idx] =
        &cm->mi_params.mi_alloc[mi_alloc_idx];
    // 'xd->mi' should point to an offset in 'mi_grid_base';
    xd->mi = cm->mi_params.mi_grid_base + mi_grid_idx;
    xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART] = bsize;
    xd->mi_row = mi_row;
    xd->mi_col = mi_col;
    // set_sb_mv_precision(sbi, cpi);
  }

  ptree->bsize = bsize;
  ptree->mi_row = mi_row;
  ptree->mi_col = mi_col;
  ptree->is_settled = 1;

  if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
    ptree_luma->bsize = bsize;
    ptree_luma->mi_row = mi_row;
    ptree_luma->mi_col = mi_col;
    ptree_luma->is_settled = 1;
    ptree_luma->is_cfl_allowed_for_this_chroma_partition =
        CFL_DISALLOWED_FOR_CHROMA;
  }

  PARTITION_TREE *parent = ptree->parent;
  set_chroma_ref_info(xd->tree_type, mi_row, mi_col, ptree->index, bsize,
                      &ptree->chroma_ref_info,
                      parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->bsize : BLOCK_INVALID,
                      parent ? parent->partition : PARTITION_NONE, ss_x, ss_y);

  partition =
      !is_partition_point(bsize)
          ? PARTITION_NONE
          : bridge_frame_read_partition(cm, xd, mi_row, mi_col, has_rows,
                                        has_cols, ptree, ptree_luma, bsize);
  ptree->is_cfl_allowed_for_this_chroma_partition |=
      is_cfl_allowed_for_sdp(cm, xd, ptree_luma, partition, bsize);
  CFL_ALLOWED_FOR_SDP_TYPE is_cfl_allowed_in_sdp =
      ptree->is_cfl_allowed_for_this_chroma_partition;
  ptree->partition = partition;

  if (!is_sb_root && parent) {
    if (parent->extended_sdp_allowed_flag)
      ptree->extended_sdp_allowed_flag = is_extended_sdp_allowed(
          cm->seq_params.enable_extended_sdp, parent->bsize, parent->partition);
    else
      ptree->extended_sdp_allowed_flag = 0;

    ptree->region_type = parent->region_type;
  }

  switch (partition) {
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B:
    case PARTITION_SPLIT:
      ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
      ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
      ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
      ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
      if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
        ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
        ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
        ptree_luma->sub_tree[2] = av1_alloc_ptree_node(ptree_luma, 2);
        ptree_luma->sub_tree[3] = av1_alloc_ptree_node(ptree_luma, 3);
      }
      break;
    case PARTITION_HORZ:
    case PARTITION_VERT:
      ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
      ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);

      if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
        ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
        ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
      }
      break;
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3:
      ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
      ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
      ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
      ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);

      if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
        ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
        ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
        ptree_luma->sub_tree[2] = av1_alloc_ptree_node(ptree_luma, 2);
        ptree_luma->sub_tree[3] = av1_alloc_ptree_node(ptree_luma, 3);
      }
      break;
    default: break;
  }
  switch (partition) {
    case PARTITION_NONE:
      xd->is_cfl_allowed_in_sdp = is_cfl_allowed_in_sdp;
      break;
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B:
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3:
    case PARTITION_SPLIT:
      ptree->sub_tree[0]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      ptree->sub_tree[1]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      ptree->sub_tree[2]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      ptree->sub_tree[3]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      break;
    case PARTITION_HORZ:
    case PARTITION_VERT:
      ptree->sub_tree[0]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      ptree->sub_tree[1]->is_cfl_allowed_for_this_chroma_partition =
          is_cfl_allowed_in_sdp;
      break;
    default: break;
  }

  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  if (subsize == BLOCK_INVALID) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Partition %d is invalid for block size %dx%d",
                       partition, block_size_wide[bsize],
                       block_size_high[bsize]);
    assert(0);
  }
  // Check the bitstream is conformant: if there is subsampling on the
  // chroma planes, subsize must subsample to a valid block size.
  const struct macroblockd_plane *const pd_u = &xd->plane[1];
  BLOCK_SIZE test_subsize = subsize;
  if (xd->tree_type == SHARED_PART) {
    parent = ptree;
    CHROMA_REF_INFO chroma_ref_info;
    const int index =
        (partition == PARTITION_HORZ || partition == PARTITION_VERT) ? 1 : 0;
    set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, subsize,
                        &chroma_ref_info,
                        parent ? &parent->chroma_ref_info : NULL,
                        parent ? parent->bsize : BLOCK_INVALID,
                        parent ? parent->partition : PARTITION_NONE,
                        xd->plane[1].subsampling_x, xd->plane[1].subsampling_y);
    test_subsize = chroma_ref_info.bsize_base;
    assert(test_subsize != BLOCK_INVALID);
  }
  if (xd->tree_type != LUMA_PART &&
      get_plane_block_size(test_subsize, pd_u->subsampling_x,
                           pd_u->subsampling_y) == BLOCK_INVALID) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Block size %dx%d invalid with this subsampling mode",
                       block_size_wide[test_subsize],
                       block_size_high[test_subsize]);
  }
  // Check that chroma ref block isn't completely outside the boundary.
  if (!is_chroma_ref_within_boundary(
          cm, xd->tree_type, ptree->chroma_ref_info.is_chroma_ref, mi_row,
          mi_col, bsize, partition, pd_u->subsampling_x, pd_u->subsampling_y)) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid partitioning %d at location [%d, %d]: chroma "
                       "info not coded.",
                       partition, mi_row << MI_SIZE_LOG2,
                       mi_col << MI_SIZE_LOG2);
  }

#define BRIDGE_FRAME_DEC_BLOCK_STX_ARG
#define BRIDGE_FRAME_DEC_BLOCK_EPT_ARG partition,
#define BRIDGE_FRAME_DEC_BLOCK(db_r, db_c, db_subsize, index) \
  bridge_frame_predict_inter_block(cm, xd)
#define BRIDGE_FRAME_DEC_PARTITION(db_r, db_c, db_subsize, index)  \
  bridge_frame_decode_partition(                                   \
      cpi, td, tile, BRIDGE_FRAME_DEC_BLOCK_STX_ARG(db_r), (db_c), \
      (db_subsize), sbi, ptree->sub_tree[(index)],                 \
      get_partition_subtree_const(ptree_luma, index))

  switch (partition) {
    case PARTITION_NONE:
      bridge_frame_set_offsets(cm, xd, subsize, mi_row, mi_col, parent, 0,
                               partition);
      // from av1_read_mode_info
      MB_MODE_INFO *const mi = xd->mi[0];
      if (xd->tree_type == SHARED_PART)
        mi->sb_type[PLANE_TYPE_UV] = mi->sb_type[PLANE_TYPE_Y];
      BRIDGE_FRAME_DEC_BLOCK(mi_row, mi_col, subsize, 0);
      break;
    case PARTITION_HORZ:
      BRIDGE_FRAME_DEC_PARTITION(mi_row, mi_col, subsize, 0);
      if ((mi_row + hbs_h) < cm->mi_params.mi_rows)
        BRIDGE_FRAME_DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 1);
      break;
    case PARTITION_VERT:
      BRIDGE_FRAME_DEC_PARTITION(mi_row, mi_col, subsize, 0);
      if ((mi_col + hbs_w) < cm->mi_params.mi_cols)
        BRIDGE_FRAME_DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
      break;
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      int this_mi_row = mi_row;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, subsize, 0);
      this_mi_row += ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, bsize_med, 1);
      this_mi_row += 2 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, bsize_big, 2);
      this_mi_row += 4 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, subsize, 3);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      int this_mi_row = mi_row;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, subsize, 0);
      this_mi_row += ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, bsize_big, 1);
      this_mi_row += 4 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, bsize_med, 2);
      this_mi_row += 2 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      BRIDGE_FRAME_DEC_PARTITION(this_mi_row, mi_col, subsize, 3);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      int this_mi_col = mi_col;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, subsize, 0);
      this_mi_col += ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, bsize_med, 1);
      this_mi_col += 2 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, bsize_big, 2);
      this_mi_col += 4 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, subsize, 3);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      int this_mi_col = mi_col;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, subsize, 0);
      this_mi_col += ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, bsize_big, 1);
      this_mi_col += 4 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, bsize_med, 2);
      this_mi_col += 2 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      BRIDGE_FRAME_DEC_PARTITION(mi_row, this_mi_col, subsize, 3);
      break;
    }
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3: {
      for (int i = 0; i < 4; ++i) {
        BLOCK_SIZE this_bsize = get_h_partition_subsize(bsize, i, partition);
        const int offset_r = get_h_partition_offset_mi_row(bsize, i, partition);
        const int offset_c = get_h_partition_offset_mi_col(bsize, i, partition);

        assert(this_bsize != BLOCK_INVALID);
        assert(offset_r >= 0 && offset_c >= 0);

        const int this_mi_row = mi_row + offset_r;
        const int this_mi_col = mi_col + offset_c;
        if (partition == PARTITION_HORZ_3) {
          if (this_mi_row >= cm->mi_params.mi_rows) break;
        } else {
          if (this_mi_col >= cm->mi_params.mi_cols) break;
        }

        BRIDGE_FRAME_DEC_PARTITION(this_mi_row, this_mi_col, this_bsize, i);
      }
      break;
    }
    case PARTITION_SPLIT:
      BRIDGE_FRAME_DEC_PARTITION(mi_row, mi_col, subsize, 0);
      BRIDGE_FRAME_DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
      BRIDGE_FRAME_DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 2);
      BRIDGE_FRAME_DEC_PARTITION(mi_row + hbs_h, mi_col + hbs_w, subsize, 3);
      break;
    default: assert(0 && "Invalid partition type");
  }

  parent = ptree->parent;
  if (!is_sb_root && parent) {
    if (!frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
        ptree->partition && parent->region_type != INTRA_REGION &&
        ptree->region_type == INTRA_REGION) {
      // decode chroma part in one intra region
      xd->tree_type = CHROMA_PART;
      bridge_frame_set_offsets(cm, xd, bsize, mi_row, mi_col, parent, 0,
                               partition);
      BRIDGE_FRAME_DEC_BLOCK(mi_row, mi_col, bsize, 0);
      // reset back to shared part
      xd->tree_type = SHARED_PART;
    }
  }

#undef BRIDGE_FRAME_DEC_PARTITION
#undef BRIDGE_FRAME_DEC_BLOCK
#undef BRIDGE_FRAME_DEC_BLOCK_EPT_ARG
#undef BRIDGE_FRAME_DEC_BLOCK_STX_ARG
}

static AOM_INLINE void bridge_frame_decode_partition_sb(
    AV1_COMP *cpi, ThreadData *const td, const TileInfo *const tile, int mi_row,
    int mi_col, BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  xd->tree_type = SHARED_PART;
  const int is_intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);

  bridge_frame_decode_partition(
      cpi, td, tile, mi_row, mi_col, bsize, xd->sbi,
      xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
      (is_intra_sdp_enabled ? xd->sbi->ptree_root[1] : NULL));
}
#endif  // CONFIG_CWG_F317

/*!\brief Encode a superblock row by breaking it into superblocks
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 * Do partition and mode search for an sb row: one row of superblocks filling up
 * the width of the current tile.
 */
static AOM_INLINE void encode_sb_row(AV1_COMP *cpi, ThreadData *td,
                                     TileDataEnc *tile_data, int mi_row,
                                     TokenExtra **tp, TokenExtra **tp_chroma) {
  AV1_COMMON *const cm = &cpi->common;
  const TileInfo *const tile_info = &tile_data->tile_info;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1EncRowMultiThreadInfo *const enc_row_mt = &mt_info->enc_row_mt;
  AV1EncRowMultiThreadSync *const row_mt_sync = &tile_data->row_mt_sync;
  bool row_mt_enabled = mt_info->row_mt_enabled;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int sb_cols_in_tile = av1_get_sb_cols_in_tile(cm, tile_data->tile_info);
  const BLOCK_SIZE sb_size = cm->sb_size;
  const int mib_size = cm->mib_size;
  const int mib_size_log2 = cm->mib_size_log2;
  const int sb_row = (mi_row - tile_info->mi_row_start) >> mib_size_log2;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_sb_time);
#endif

  // Initialize the left context for the new SB row
  av1_zero_left_context(xd);

  // Reset delta for quantizer and loof filters at the beginning of every tile
  if (mi_row == tile_info->mi_row_start || row_mt_enabled) {
    if (cm->delta_q_info.delta_q_present_flag)
      xd->current_base_qindex = cm->quant_params.base_qindex;
    if (cm->delta_q_info.delta_lf_present_flag) {
      av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
    }
  }

  reset_thresh_freq_fact(x);

  // Code each SB in the row
  for (int mi_col = tile_info->mi_col_start, sb_col_in_tile = 0;
       mi_col < tile_info->mi_col_end; mi_col += mib_size, sb_col_in_tile++) {
    (*(enc_row_mt->sync_read_ptr))(row_mt_sync, sb_row, sb_col_in_tile);
    av1_reset_is_mi_coded_map(xd, cm->mib_size);
    BruActiveMode sb_active_mode =
        enc_get_cur_sb_active_mode(cm, mi_col, mi_row);
    // use for lpf only, use causal restriction only
    av1_set_sb_info(cm, xd, mi_row, mi_col, sb_active_mode);

    if (tile_data->allow_update_cdf && row_mt_enabled &&
        (tile_info->mi_row_start != mi_row)) {
      if ((tile_info->mi_col_start == mi_col)) {
        // restore frame context at the 1st column sb
        memcpy(xd->tile_ctx, x->row_ctx, sizeof(*xd->tile_ctx));
      } else {
        // update context
        int wt_left = AVG_CDF_WEIGHT_LEFT;
        int wt_tr = AVG_CDF_WEIGHT_TOP_RIGHT;
        if (tile_info->mi_col_end > (mi_col + mib_size))
          av1_avg_cdf_symbols(xd->tile_ctx, x->row_ctx + sb_col_in_tile,
                              wt_left, wt_tr);
        else
          av1_avg_cdf_symbols(xd->tile_ctx, x->row_ctx + sb_col_in_tile - 1,
                              wt_left, wt_tr);
      }
    }

    // Update the rate cost tables for some symbols
    av1_set_cost_upd_freq(cpi, td, tile_info, mi_row, mi_col);

    xd->cur_frame_force_integer_mv = cm->features.cur_frame_force_integer_mv;
    x->source_variance = UINT_MAX;
    td->mb.cb_coef_buff = av1_get_cb_coeff_buffer(cpi, mi_row, mi_col);

    av1_reset_refmv_bank(cm, xd, tile_info, mi_row, mi_col);

    // Get segment id and skip flag
    const struct segmentation *const seg = &cm->seg;
    int seg_skip = 0;
    if (seg->enabled) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      const int segment_id =
          map ? get_segment_id(&cm->mi_params, map, sb_size, mi_row, mi_col)
              : 0;
      seg_skip = segfeature_active(seg, segment_id, SEG_LVL_SKIP);
    }

    BruActiveMode cur_sb_active_mode =
        enc_get_cur_sb_active_mode(cm, mi_col, mi_row);
    // support SB let it go to RD but restrict
    assert(xd->sbi->sb_active_mode == cur_sb_active_mode);
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      CHROMA_REF_INFO chroma_ref_info;
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
      xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]->partition =
          PARTITION_NONE;
      set_chroma_ref_info(
          xd->tree_type, mi_row, mi_col, 0, cm->seq_params.sb_size,
          &chroma_ref_info,
          &xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]->chroma_ref_info,
          BLOCK_INVALID, PARTITION_NONE, xd->plane[1].subsampling_x,
          xd->plane[1].subsampling_y);
      av1_set_offsets_without_segment_id(cpi, &tile_data->tile_info, &td->mb,
                                         mi_row, mi_col, cm->seq_params.sb_size,
                                         NULL);
      set_sb_mbmi_bru_mode(cm, xd, mi_col, mi_row, cm->seq_params.sb_size,
                           BRU_INACTIVE_SB);
      initialize_chroma_ref_info(mi_row, mi_col, cm->seq_params.sb_size,
                                 &xd->mi[0]->chroma_ref_info);
      bru_set_default_inter_mb_mode_info(cm, xd, xd->mi[0],
                                         cm->seq_params.sb_size);
      const int w = mi_size_wide[sb_size];
      const int h = mi_size_high[sb_size];
      const int x_inside_boundary = AOMMIN(w, cm->mi_params.mi_cols - mi_col);
      const int y_inside_boundary = AOMMIN(h, cm->mi_params.mi_rows - mi_row);
      bru_zero_sb_mvs(cm, -1, mi_row, mi_col, x_inside_boundary,
                      y_inside_boundary);
      av1_reset_entropy_context(xd, cm->seq_params.sb_size, av1_num_planes(cm));
      bridge_frame_decode_partition_sb(cpi, td, &tile_data->tile_info, mi_row,
                                       mi_col, cm->seq_params.sb_size);
    } else
#endif  // CONFIG_CWG_F317
      // use for lpf only, use causal restriction only
      if (cm->bru.enabled && (cur_sb_active_mode != BRU_ACTIVE_SB)) {
        CHROMA_REF_INFO chroma_ref_info;
        // xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]
        av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
        xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]->partition =
            PARTITION_NONE;
        set_chroma_ref_info(xd->tree_type, mi_row, mi_col, 0,
                            cm->seq_params.sb_size, &chroma_ref_info,
                            &xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]
                                 ->chroma_ref_info,
                            BLOCK_INVALID, PARTITION_NONE,
                            xd->plane[1].subsampling_x,
                            xd->plane[1].subsampling_y);
        av1_set_offsets_without_segment_id(cpi, &tile_data->tile_info, &td->mb,
                                           mi_row, mi_col,
                                           cm->seq_params.sb_size, NULL);
        set_sb_mbmi_bru_mode(cm, xd, mi_col, mi_row, cm->seq_params.sb_size,
                             cur_sb_active_mode);
        initialize_chroma_ref_info(mi_row, mi_col, cm->seq_params.sb_size,
                                   &xd->mi[0]->chroma_ref_info);
        bru_set_default_inter_mb_mode_info(cm, xd, xd->mi[0],
                                           cm->seq_params.sb_size);
        const int w = mi_size_wide[sb_size];
        const int h = mi_size_high[sb_size];
        const int x_inside_boundary = AOMMIN(w, cm->mi_params.mi_cols - mi_col);
        const int y_inside_boundary = AOMMIN(h, cm->mi_params.mi_rows - mi_row);
        // set to bru ref mvs to 0
        bru_zero_sb_mvs(cm, cm->bru.update_ref_idx, mi_row, mi_col,
                        x_inside_boundary, y_inside_boundary);
        // only support need to be copied
        // no even support do not eed to copy
        if (cur_sb_active_mode == BRU_SUPPORT_SB) {
          bru_copy_sb(cm, mi_col, mi_row);
        }
        av1_reset_entropy_context(xd, cm->seq_params.sb_size,
                                  av1_num_planes(cm));
      } else
        // encode the superblock
        encode_rd_sb(cpi, td, tile_data, tp, tp_chroma, mi_row, mi_col,
                     seg_skip);

    // Update the top-right context in row_mt coding
    if (tile_data->allow_update_cdf && row_mt_enabled &&
        (tile_info->mi_row_end > (mi_row + mib_size))) {
      if (sb_cols_in_tile == 1)
        memcpy(x->row_ctx, xd->tile_ctx, sizeof(*xd->tile_ctx));
      else if (sb_col_in_tile >= 1)
        memcpy(x->row_ctx + sb_col_in_tile - 1, xd->tile_ctx,
               sizeof(*xd->tile_ctx));
    }
    (*(enc_row_mt->sync_write_ptr))(row_mt_sync, sb_row, sb_col_in_tile,
                                    sb_cols_in_tile);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_sb_time);
#endif
}

static AOM_INLINE void init_encode_frame_mb_context(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  // Copy data over into macro block data structures.
  av1_setup_src_planes(x, cpi->source, 0, 0, num_planes, NULL);

  av1_setup_block_planes(xd, cm->seq_params.subsampling_x,
                         cm->seq_params.subsampling_y, num_planes);
}

void av1_alloc_tile_data(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;

  if (cpi->tile_data != NULL) aom_free(cpi->tile_data);
  CHECK_MEM_ERROR(
      cm, cpi->tile_data,
      aom_memalign(32, tile_cols * tile_rows * sizeof(*cpi->tile_data)));

  cpi->allocated_tiles = tile_cols * tile_rows;
}

void av1_init_tile_data(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  int tile_col, tile_row;
  TokenInfo *const token_info = &cpi->token_info;
  TokenExtra *pre_tok = token_info->tile_tok[0][0];
  TokenList *tplist = token_info->tplist[0][0];
  unsigned int tile_tok = 0;
  int tplist_count = 0;

  const int num_tiles = tile_rows * tile_cols;
  if (cm->bru.enabled) {
    memset(cm->tiles.tile_active_bitmap, 0, (num_tiles + 7) / 8);
    if (num_tiles == 1) {
      cm->tiles.tile_active_bitmap[0] = 1;
    }
  }
  for (tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
      TileDataEnc *const tile_data =
          &cpi->tile_data[tile_row * tile_cols + tile_col];
      TileInfo *const tile_info = &tile_data->tile_info;
      av1_tile_init(tile_info, cm, tile_row, tile_col);
      tile_data->firstpass_top_mv = kZeroMv;

      if (pre_tok != NULL && tplist != NULL) {
        token_info->tile_tok[tile_row][tile_col] = pre_tok + tile_tok;
        pre_tok = token_info->tile_tok[tile_row][tile_col];
        tile_tok = allocated_tokens(
            *tile_info, cm->mib_size_log2 + MI_SIZE_LOG2, num_planes);
        token_info->tplist[tile_row][tile_col] = tplist + tplist_count;
        tplist = token_info->tplist[tile_row][tile_col];
        tplist_count = av1_get_sb_rows_in_tile(cm, tile_data->tile_info);
      }
      tile_data->allow_update_cdf = 1;
      tile_data->allow_update_cdf =
          tile_data->allow_update_cdf && !cm->features.disable_cdf_update;
      tile_data->tctx = *cm->fc;
      tile_info->tile_active_mode = 1;
      // check tile skip
      if (cm->bru.enabled) {
        tile_info->tile_active_mode = 0;
#if CONFIG_CWG_F317
        if (!cm->bru.frame_inactive_flag &&
            !cm->bridge_frame_info.is_bridge_frame) {
#else
        if (!cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
          for (int mi_y = tile_info->mi_row_start; mi_y < tile_info->mi_row_end;
               mi_y += cm->mib_size) {
            for (int mi_x = tile_info->mi_col_start;
                 mi_x < tile_info->mi_col_end; mi_x += cm->mib_size) {
              BruActiveMode sb_active_mode =
                  enc_get_cur_sb_active_mode(cm, mi_x, mi_y);
              if (sb_active_mode != BRU_INACTIVE_SB) {
                tile_info->tile_active_mode = 1;
                break;
              }
            }
            if (tile_info->tile_active_mode) {
              break;
            }
          }
        }
        const int tile_idx = tile_col + tile_cols * tile_row;
        const int active_bitmap_byte = tile_idx >> 3;
        const int active_bitmap_bit = tile_idx & 7;
        cm->tiles.tile_active_bitmap[active_bitmap_byte] +=
            (tile_info->tile_active_mode << active_bitmap_bit);
      }
    }
  }
}

/*!\brief Encode a superblock row
 *
 * \ingroup partition_search
 */
void av1_encode_sb_row(AV1_COMP *cpi, ThreadData *td, int tile_row,
                       int tile_col, int mi_row) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const int tile_cols = cm->tiles.cols;
  TileDataEnc *this_tile = &cpi->tile_data[tile_row * tile_cols + tile_col];
  const TileInfo *const tile_info = &this_tile->tile_info;
  TokenExtra *tok = NULL;
  TokenList *const tplist = cpi->token_info.tplist[tile_row][tile_col];
  const int sb_row_in_tile =
      (mi_row - tile_info->mi_row_start) >> cm->mib_size_log2;
  const int tile_mb_cols =
      (tile_info->mi_col_end - tile_info->mi_col_start + 2) >> 2;
  const int num_mb_rows_in_sb =
      ((1 << (cm->mib_size_log2 + MI_SIZE_LOG2)) + 8) >> 4;

  get_start_tok(cpi, tile_row, tile_col, mi_row, &tok,
                cm->mib_size_log2 + MI_SIZE_LOG2, num_planes);
  tplist[sb_row_in_tile].start = tok;

  /* tok chroma takes a pointer after one channel length assoicated with
    storage of palette information. tok parameter will be associated with luma
    channel. tok chroma is associated with chroma channel.
    Only in key frames tok chroma will be used to store palette infomation of
    chroma.
   */
  const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
  int temp_num_mb_rows_in_sb = num_mb_rows_in_sb;
  TokenExtra *tok_chroma =
      tok + get_token_alloc(temp_num_mb_rows_in_sb, tile_mb_cols,
                            cm->mib_size_log2 + MI_SIZE_LOG2, 1);
  tplist[sb_row_in_tile].start_chroma = tok_chroma;

  encode_sb_row(cpi, td, this_tile, mi_row, &tok, &tok_chroma);

  tplist[sb_row_in_tile].count =
      (unsigned int)(tok - tplist[sb_row_in_tile].start);

  tplist[sb_row_in_tile].count_chroma =
      (unsigned int)(tok_chroma - tplist[sb_row_in_tile].start_chroma);
  if (intra_sdp_enabled) {
    assert((unsigned int)(tok_chroma - tplist[sb_row_in_tile].start) <=
           get_token_alloc(temp_num_mb_rows_in_sb, tile_mb_cols,
                           cm->mib_size_log2 + MI_SIZE_LOG2, num_planes));
  } else
    assert((unsigned int)(tok - tplist[sb_row_in_tile].start) <=
           get_token_alloc(num_mb_rows_in_sb, tile_mb_cols,
                           cm->mib_size_log2 + MI_SIZE_LOG2, num_planes));
  (void)tile_mb_cols;
  (void)num_mb_rows_in_sb;
}

/*!\brief Encode a tile
 *
 * \ingroup partition_search
 */
void av1_encode_tile(AV1_COMP *cpi, ThreadData *td, int tile_row,
                     int tile_col) {
  AV1_COMMON *const cm = &cpi->common;
  TileDataEnc *const this_tile =
      &cpi->tile_data[tile_row * cm->tiles.cols + tile_col];
  const TileInfo *const tile_info = &this_tile->tile_info;

  av1_inter_mode_data_init(this_tile);

  av1_zero_above_context(cm, &td->mb.e_mbd, tile_info->mi_col_start,
                         tile_info->mi_col_end, tile_row);
  av1_init_above_context(&cm->above_contexts, av1_num_planes(cm), tile_row,
                         &td->mb.e_mbd);

  if (cpi->oxcf.intra_mode_cfg.enable_cfl_intra)
    cfl_init(&td->mb.e_mbd.cfl, &cm->seq_params);

  av1_crc32c_calculator_init(
      &td->mb.txfm_search_info.mb_rd_record.crc_calculator);

  for (int mi_row = tile_info->mi_row_start; mi_row < tile_info->mi_row_end;
       mi_row += cm->mib_size) {
    av1_zero(td->mb.e_mbd.ref_mv_bank);

    av1_zero(td->mb.e_mbd.warp_param_bank);
#if !WARP_CU_BANK
    td->mb.e_mbd.warp_param_bank_pt = &td->mb.e_mbd.warp_param_bank;
#endif  //! WARP_CU_BANK

    av1_encode_sb_row(cpi, td, tile_row, tile_col, mi_row);
  }
}

/*!\brief Break one frame into tiles and encode the tiles
 *
 * \ingroup partition_search
 *
 * \param[in]    cpi    Top-level encoder structure
 */
static AOM_INLINE void encode_tiles(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  int tile_col, tile_row;

  assert(IMPLIES(cpi->tile_data == NULL,
                 cpi->allocated_tiles < tile_cols * tile_rows));
  if (cpi->allocated_tiles < tile_cols * tile_rows) av1_alloc_tile_data(cpi);

  av1_init_tile_data(cpi);
  alloc_inter_modes_info_data(cm, &cpi->td.mb);

  for (tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
      TileDataEnc *const this_tile =
          &cpi->tile_data[tile_row * cm->tiles.cols + tile_col];
      cpi->td.intrabc_used = 0;
      cpi->td.deltaq_used = 0;
      cpi->td.mb.e_mbd.tile_ctx = &this_tile->tctx;
      cpi->td.mb.tile_pb_ctx = &this_tile->tctx;
      cpi->td.mb.palette_pixels = 0;
      av1_encode_tile(cpi, &cpi->td, tile_row, tile_col);
      cpi->palette_pixel_num += cpi->td.mb.palette_pixels;
      cpi->intrabc_used |= cpi->td.intrabc_used;
      cpi->deltaq_used |= cpi->td.deltaq_used;
    }
  }
  dealloc_inter_modes_info_data(&cpi->td.mb);
}

// Set the relative distance of a reference frame w.r.t. current frame
static AOM_INLINE void set_rel_frame_dist(
    const AV1_COMMON *const cm, RefFrameDistanceInfo *const ref_frame_dist_info,
    const int ref_frame_flags) {
  MV_REFERENCE_FRAME ref_frame;
  int min_past_dist = INT32_MAX, min_future_dist = INT32_MAX;
  ref_frame_dist_info->nearest_past_ref = NONE_FRAME;
  ref_frame_dist_info->nearest_future_ref = NONE_FRAME;
  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    ref_frame_dist_info->ref_relative_dist[ref_frame] = 0;
    if (ref_frame_flags & (1 << ref_frame)) {
      int dist = av1_encoder_get_relative_dist(
          cm->cur_frame->ref_display_order_hint[ref_frame],
          cm->current_frame.display_order_hint);
      ref_frame_dist_info->ref_relative_dist[ref_frame] = dist;
      // Get the nearest ref_frame in the past
      if (abs(dist) < min_past_dist && dist < 0) {
        ref_frame_dist_info->nearest_past_ref = ref_frame;
        min_past_dist = abs(dist);
      }
      // Get the nearest ref_frame in the future
      if (dist < min_future_dist && dist > 0) {
        ref_frame_dist_info->nearest_future_ref = ref_frame;
        min_future_dist = dist;
      }
    }
  }
}

static INLINE int refs_are_one_sided(const AV1_COMMON *cm) {
  assert(!frame_is_intra_only(cm));

  return (cm->ref_frames_info.num_past_refs == 0 &&
          cm->ref_frames_info.num_cur_refs == 0) ||
         cm->ref_frames_info.num_future_refs == 0;
}

static int check_skip_mode_enabled(AV1_COMP *const cpi) {
  AV1_COMMON *const cm = &cpi->common;

  av1_setup_skip_mode_allowed(cm);
  if (!cm->current_frame.skip_mode_info.skip_mode_allowed) return 0;

  // Turn off skip mode if the temporal distances of the reference pair to the
  // current frame are different by more than 1 frame.
  const int cur_offset = (int)cm->current_frame.display_order_hint;
  int ref_offset[2];
  get_skip_mode_ref_offsets(cm, ref_offset);
  const int cur_to_ref0 = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                                cur_offset, ref_offset[0]));
  const int cur_to_ref1 = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                                cur_offset, ref_offset[1]));
  if (abs(cur_to_ref0 - cur_to_ref1) > 1) return 0;

  // High Latency: Turn off skip mode if all refs are fwd.
  if (cpi->all_one_sided_refs && cpi->oxcf.gf_cfg.lag_in_frames > 0) return 0;

  const int ref_frame[2] = { cm->current_frame.skip_mode_info.ref_frame_idx_0,
                             cm->current_frame.skip_mode_info.ref_frame_idx_1 };
  assert(ref_frame[0] <= INTER_REFS_PER_FRAME &&
         ref_frame[1] <= INTER_REFS_PER_FRAME);
  if (!(cpi->common.ref_frame_flags & (1 << ref_frame[0])) ||
      !(cpi->common.ref_frame_flags & (1 << ref_frame[1])))
    return 0;

  return 1;
}

static AOM_INLINE void set_default_interp_skip_flags(
    const AV1_COMMON *cm, InterpSearchFlags *interp_search_flags) {
  const int num_planes = av1_num_planes(cm);
  interp_search_flags->default_interp_skip_flags =
      (num_planes == 1) ? INTERP_SKIP_LUMA_EVAL_CHROMA
                        : INTERP_SKIP_LUMA_SKIP_CHROMA;
}

#define TIP_COUNT_THRESHOLD 6
// This is an encoder-only function:
// Based on the statistics of the number of blocks coded in TIP mode in
// previous frames, a decision is made whether TIP mode should be enabled.
static AOM_INLINE int could_tip_mode_be_selected(AV1_COMP *const cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  const int cur_order_hint = cm->current_frame.display_order_hint;
  if (cm->bru.enabled) {
    if ((cm->tip_ref.ref_frame[0] == cm->bru.update_ref_idx) ||
        (cm->tip_ref.ref_frame[1] == cm->bru.update_ref_idx)) {
      return 0;
    }
  }
  if (cm->has_both_sides_refs) return 1;
  if (cur_order_hint < INTER_REFS_PER_FRAME) return 1;

  if (cur_order_hint >= INTER_REFS_PER_FRAME) {
    const int mvs_rows = cm->mi_params.mi_rows;
    const int mvs_cols = cm->mi_params.mi_cols;
    const int mvs_total = mvs_rows * mvs_cols;

    for (int index = 0; index < INTER_REFS_PER_FRAME; ++index) {
      const int percent = (cpi->tip_mode_count[index] * 100) / mvs_total;
      if (percent >= TIP_COUNT_THRESHOLD) {
        return 1;
      }
    }
  }

  return 0;
}

static AOM_INLINE void decide_tip_setting_and_setup_tip_frame(AV1_COMP *cpi) {
  ThreadData *const td = &cpi->td;
  AV1_COMMON *const cm = &cpi->common;
  if (cm->features.tip_frame_mode) {
    if (is_unequal_weighted_tip_allowed(cm)) {
      int64_t best_sse = INT64_MAX;
      int8_t best_wtd_index = 0;
      YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
      for (int8_t wtd_index = 0; wtd_index < MAX_TIP_WTD_NUM; wtd_index++) {
        cm->tip_global_wtd_index = wtd_index;
        av1_setup_tip_frame(cm, &td->mb.e_mbd, NULL, td->mb.tmp_conv_dst,
                            av1_enc_calc_subpel_params, 0 /* copy_refined_mvs */
        );

        int64_t this_sse = aom_highbd_get_y_sse(cpi->source, tip_frame_buf);
        this_sse +=
            aom_highbd_sse(cpi->source->u_buffer, cpi->source->uv_stride,
                           tip_frame_buf->u_buffer, tip_frame_buf->uv_stride,
                           cpi->source->uv_width, cpi->source->uv_height);
        this_sse +=
            aom_highbd_sse(cpi->source->v_buffer, cpi->source->uv_stride,
                           tip_frame_buf->v_buffer, tip_frame_buf->uv_stride,
                           cpi->source->uv_width, cpi->source->uv_height);
        if (this_sse < best_sse) {
          best_wtd_index = wtd_index;
          best_sse = this_sse;
        }
      }
      cm->tip_global_wtd_index = best_wtd_index;
    } else {
      cm->tip_global_wtd_index = 0;
    }

    av1_setup_tip_frame(cm, &td->mb.e_mbd, NULL, td->mb.tmp_conv_dst,
                        av1_enc_calc_subpel_params, 0 /* copy_refined_mvs */
    );
  }
}

static AOM_INLINE void av1_enc_setup_tip_frame(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  cm->tip_global_motion.as_int = 0;
  cm->tip_interp_filter = MULTITAP_SHARP;
  cm->tip_global_wtd_index = 0;

  if (cm->seq_params.enable_tip && could_tip_mode_be_selected(cpi)) {
    if (cm->features.allow_ref_frame_mvs &&
        (cm->has_both_sides_refs || cm->ref_frames_info.num_past_refs >= 2)) {
#if CONFIG_COLLECT_COMPONENT_TIMING
      start_timing(cpi, av1_enc_setup_tip_frame_time);
#endif
      av1_enc_setup_tip_motion_field(cm);
      if (cm->features.tip_frame_mode) {
        decide_tip_setting_and_setup_tip_frame(cpi);
      }
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(cpi, av1_enc_setup_tip_frame_time);
#endif
    } else {
      cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
    }
  } else {
    cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
  }

  if (cm->features.allow_ref_frame_mvs &&
      cm->features.tip_frame_mode == TIP_FRAME_DISABLED) {
    // TPL mvs at non-sampled locations will be filled after it is hole-filled
    // and smoothed.
    av1_fill_tpl_mvs_sample_gap(cm);
  }

  const int cur_order_hint = cm->current_frame.display_order_hint;
  if (!cm->has_both_sides_refs && cur_order_hint < INTER_REFS_PER_FRAME) {
    cpi->tip_mode_count[cur_order_hint] = 0;
  }
}

/*!\brief Set the lossless flags for a frame before encoding it
 *
 * \ingroup high_level_algo
 */
void av1_set_lossless(AV1_COMP *cpi) {
  // NOTE lossless flags needs to be set before tcq_mode and parity_hiding are
  // set for a frame
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  AV1_COMMON *const cm = &cpi->common;
  const CommonQuantParams *quant_params = &cm->quant_params;
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  cm->features.has_lossless_segment = 0;
#endif  //  CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  cpi->enc_seg.has_lossless_segment = 0;
  for (int i = 0; i < MAX_SEGMENTS; ++i) {
    const int qindex =
        cm->seg.enabled ? av1_get_qindex(&cm->seg, i, quant_params->base_qindex,
                                         cm->seq_params.bit_depth)
                        : quant_params->base_qindex;
    xd->lossless[i] =
        qindex == 0 && cm->delta_q_info.delta_q_present_flag == 0 &&
        (quant_params->y_dc_delta_q + cm->seq_params.base_y_dc_delta_q <= 0) &&
        (quant_params->u_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        (quant_params->v_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        (quant_params->u_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <= 0) &&
        (quant_params->v_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <= 0);

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
    cm->features.lossless_segment[i] = xd->lossless[i];
    if (xd->lossless[i]) cm->features.has_lossless_segment = 1;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

    if (xd->lossless[i]) cpi->enc_seg.has_lossless_segment = 1;
    xd->qindex[i] = qindex;
    if (xd->lossless[i]) {
      cpi->optimize_seg_arr[i] = NO_TRELLIS_OPT;
    } else {
      cpi->optimize_seg_arr[i] = cpi->sf.rd_sf.optimize_coefficients;
    }
  }

#if CONFIG_MIXED_LOSSLESS_ENCODE
  const int max_seg_num =
      cm->seg.enabled ? (cm->seg.enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8)
                      : 1;
  printf(" Encoder Segmentation QPS = ");
  for (int i = 0; i < max_seg_num; ++i) {
    printf(" %d ", xd->qindex[i]);
  }
  printf(" \n");
  printf(" Encoder lossless map = ");
  for (int i = 0; i < max_seg_num; ++i) {
#if !CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
    printf(" %d ", xd->lossless[i]);
#else
    printf(" %d ", cm->features.lossless_segment[i]);
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  }
  printf(" \n");
#endif  // CONFIG_MIXED_LOSSLESS_ENCODE

  cm->features.coded_lossless = is_coded_lossless(cm, xd);
  cm->features.all_lossless = cm->features.coded_lossless;
}

/*!\brief Set the tcq_mode for a frame before encoding it
 *
 * \ingroup high_level_algo
 */
void av1_set_frame_tcq_mode(AV1_COMP *cpi) {
  // NOTE tcq_mode needs to be set after lossless flags are set and before
  // parity_hiding is set for a frame
  AV1_COMMON *const cm = &cpi->common;
  if (cm->features.coded_lossless) {
    // Disable TCQ for lossless since TCQ may not be reversible
    cm->features.tcq_mode = 0;
  } else {
    if (cm->seq_params.enable_tcq >= TCQ_8ST_FR) {
      cm->features.tcq_mode =
          frame_is_intra_only(cm) || cm->current_frame.pyramid_level <= 1;
    } else {
      cm->features.tcq_mode = cm->seq_params.enable_tcq;
    }
  }
}

void av1_enc_setup_ph_frame(AV1_COMP *cpi) {
  // Note parity_hiding is to be set for a frame after lossless and tcq_mode
  // are set
  AV1_COMMON *const cm = &cpi->common;
  if (cm->features.coded_lossless || !cm->seq_params.enable_parity_hiding ||
      cm->features.tcq_mode)
    cm->features.allow_parity_hiding = false;
  else
    cm->features.allow_parity_hiding = true;
}

/*!\brief Encoder setup(only for the current frame), encoding, and recontruction
 * for a single frame
 *
 * \ingroup high_level_algo
 */
static AOM_INLINE void encode_frame_internal(AV1_COMP *cpi) {
  ThreadData *const td = &cpi->td;
  MACROBLOCK *const x = &td->mb;
  AV1_COMMON *const cm = &cpi->common;
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_COUNTS *const rdc = &cpi->td.rd_counts;
  FrameProbInfo *const frame_probs = &cpi->frame_probs;
  IntraBCHashInfo *const intrabc_hash_info = &x->intrabc_hash_info;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1EncRowMultiThreadInfo *const enc_row_mt = &mt_info->enc_row_mt;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const DELTAQ_MODE deltaq_mode = oxcf->q_cfg.deltaq_mode;
  int i;

  mi_params->setup_mi(mi_params);

  set_mi_offsets(mi_params, xd, 0, 0, 0, 0);

  av1_zero(*td->counts);
  av1_zero(rdc->comp_pred_diff);
  av1_zero(rdc->tx_type_used);
  av1_zero(rdc->warped_used);

  for (i = 0; i < CCSO_NUM_COMPONENTS; ++i) {
    cm->ccso_info.ccso_frame_flag = 0;
    cm->ccso_info.reuse_ccso[i] = 0;
    cm->ccso_info.sb_reuse_ccso[i] = 0;
    cm->ccso_info.ccso_ref_idx[i] = UINT8_MAX;
  }

  // Reset the flag.
  cpi->intrabc_used = 0;

  features->allow_intrabc &= (oxcf->kf_cfg.enable_intrabc);

  // Decide which motion modes to scan this frame
  // TODO(rachelbarker): Rework pruning into something more unified in phase 2

#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
  if (cm->seq_params.seq_frame_motion_modes_present_flag) {
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT

    int enabled_motion_modes = cm->seq_params.seq_enabled_motion_modes;

    if ((enabled_motion_modes & (1 << WARP_CAUSAL)) != 0 &&
        cpi->sf.inter_sf.prune_warped_prob_thresh > 0) {
      const FRAME_UPDATE_TYPE update_type =
          get_frame_update_type(&cpi->gf_group);
      if (frame_probs->warped_probs[update_type] <
          cpi->sf.inter_sf.prune_warped_prob_thresh)
        enabled_motion_modes &= ~(1 << WARP_CAUSAL);
    }

    features->enabled_motion_modes = enabled_motion_modes;

#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
  } else {
    features->enabled_motion_modes = cm->seq_params.seq_enabled_motion_modes;
  }
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT

  features->allow_warpmv_mode =
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0;
  if (features->allow_warpmv_mode &&
      cpi->sf.inter_sf.prune_warpmv_prob_thresh > 0) {
    const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
    if (frame_probs->warped_probs[update_type] <
        cpi->sf.inter_sf.prune_warpmv_prob_thresh) {
      features->allow_warpmv_mode = 0;
    }
  }

  int hash_table_created = 0;
  if (!is_stat_generation_stage(cpi) && av1_use_hash_me(cpi)) {
    // TODO(any): move this outside of the recoding loop to avoid recalculating
    // the hash table.
    // add to hash table
    const int pic_width = cpi->source->y_crop_width;
    const int pic_height = cpi->source->y_crop_height;
    uint32_t *block_hash_values[2][2];
    int8_t *is_block_same[2][3];
    int k, j;

    for (k = 0; k < 2; k++) {
      for (j = 0; j < 2; j++) {
        CHECK_MEM_ERROR(cm, block_hash_values[k][j],
                        aom_malloc(sizeof(uint32_t) * pic_width * pic_height));
      }

      for (j = 0; j < 3; j++) {
        CHECK_MEM_ERROR(cm, is_block_same[k][j],
                        aom_malloc(sizeof(int8_t) * pic_width * pic_height));
      }
    }

    av1_hash_table_init(intrabc_hash_info);
    av1_hash_table_create(&intrabc_hash_info->intrabc_hash_table);
    hash_table_created = 1;
    av1_generate_block_2x2_hash_value(intrabc_hash_info, cpi->source,
                                      block_hash_values[0], is_block_same[0]);
    // Hash data generated for screen contents is used for intraBC ME
    const int min_alloc_size = block_size_wide[mi_params->mi_alloc_bsize];
    const int max_sb_size = (1 << (cm->mib_size_log2 + MI_SIZE_LOG2));
    int src_idx = 0;
    for (int size = 4; size <= max_sb_size; size *= 2, src_idx = !src_idx) {
      const int dst_idx = !src_idx;
      av1_generate_block_hash_value(
          intrabc_hash_info, cpi->source, size, block_hash_values[src_idx],
          block_hash_values[dst_idx], is_block_same[src_idx],
          is_block_same[dst_idx]);
      if (size >= min_alloc_size) {
        av1_add_to_hash_map_by_row_with_precal_data(
            &intrabc_hash_info->intrabc_hash_table, block_hash_values[dst_idx],
            is_block_same[dst_idx][2], pic_width, pic_height, size);
      }
    }

    for (k = 0; k < 2; k++) {
      for (j = 0; j < 2; j++) {
        aom_free(block_hash_values[k][j]);
      }

      for (j = 0; j < 3; j++) {
        aom_free(is_block_same[k][j]);
      }
    }
  }

  const CommonQuantParams *quant_params = &cm->quant_params;

  // Fix delta q resolution for the moment
  cm->delta_q_info.delta_q_res = 0;
  if (cpi->oxcf.q_cfg.aq_mode != CYCLIC_REFRESH_AQ) {
    if (deltaq_mode == DELTA_Q_OBJECTIVE)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_OBJECTIVE;
    else if (deltaq_mode == DELTA_Q_PERCEPTUAL)
      cm->delta_q_info.delta_q_res = DEFAULT_DELTA_Q_RES_PERCEPTUAL;
    // Set delta_q_present_flag before it is used for the first time
    cm->delta_q_info.delta_lf_res = DEFAULT_DELTA_LF_RES;
    cm->delta_q_info.delta_q_present_flag = deltaq_mode != NO_DELTA_Q;

    // Turn off cm->delta_q_info.delta_q_present_flag if objective delta_q
    // is used for ineligible frames. That effectively will turn off row_mt
    // usage. Note objective delta_q and tpl eligible frames are only altref
    // frames currently.
    const GF_GROUP *gf_group = &cpi->gf_group;
    if (cm->delta_q_info.delta_q_present_flag) {
      if (deltaq_mode == DELTA_Q_OBJECTIVE &&
          !is_frame_tpl_eligible(gf_group, gf_group->index))
        cm->delta_q_info.delta_q_present_flag = 0;
    }

    // Reset delta_q_used flag
    cpi->deltaq_used = 0;

    cm->delta_q_info.delta_lf_present_flag =
        cm->delta_q_info.delta_q_present_flag &&
        oxcf->tool_cfg.enable_deltalf_mode;
    cm->delta_q_info.delta_lf_multi = DEFAULT_DELTA_LF_MULTI;

    // update delta_q_present_flag and delta_lf_present_flag based on
    // base_qindex
    cm->delta_q_info.delta_q_present_flag &= quant_params->base_qindex > 0;
    cm->delta_q_info.delta_lf_present_flag &= quant_params->base_qindex > 0;
  }

  av1_frame_init_quantizer(cpi);
  av1_initialize_rd_consts(cpi);
  av1_set_sad_per_bit(cpi, &x->mv_costs, quant_params->base_qindex);

  av1_reset_txk_skip_array(cm);

  init_encode_frame_mb_context(cpi);
  set_default_interp_skip_flags(cm, &cpi->interp_search_flags);
  if (cm->prev_frame && cm->prev_frame->seg.enabled)
    cm->last_frame_seg_map = cm->prev_frame->seg_map;
  else
    cm->last_frame_seg_map = NULL;
  if (features->coded_lossless) {
    av1_set_default_ref_deltas(cm->lf.ref_deltas);
    av1_set_default_mode_deltas(cm->lf.mode_deltas);
  } else if (cm->prev_frame) {
    memcpy(cm->lf.ref_deltas, cm->prev_frame->ref_deltas, SINGLE_REF_FRAMES);
    memcpy(cm->lf.mode_deltas, cm->prev_frame->mode_deltas, MAX_MODE_LF_DELTAS);
  }
  memcpy(cm->cur_frame->ref_deltas, cm->lf.ref_deltas, SINGLE_REF_FRAMES);
  memcpy(cm->cur_frame->mode_deltas, cm->lf.mode_deltas, MAX_MODE_LF_DELTAS);

  cpi->all_one_sided_refs =
      frame_is_intra_only(cm) ? 0 : refs_are_one_sided(cm);

  x->txfm_search_info.txb_split_count = 0;
#if CONFIG_SPEED_STATS
  x->txfm_search_info.tx_search_count = 0;
#endif  // CONFIG_SPEED_STATS

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_compute_global_motion_time);
#endif
  av1_compute_global_motion_facade(cpi);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_compute_global_motion_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_setup_motion_field_time);
#endif

  cm->tmvp_sample_step = 1;
  cm->tmvp_sample_stepl2 = 0;
  if (features->allow_ref_frame_mvs) {
    cm->tmvp_sample_step = -1;
    av1_setup_motion_field(cm);
    if (cm->tmvp_sample_step < 0) {
      cm->tmvp_sample_step = 1;
      cm->tmvp_sample_stepl2 = 0;
    }
  } else {
    av1_setup_ref_frame_sides(cm);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_setup_motion_field_time);
#endif

  const int sub_pu_qp_thr =
      SUB_PU_QTHR + (cm->seq_params.bit_depth - AOM_BITS_8) * SUB_PU_BD_FACTOR;
  if (cm->seq_params.enable_lf_sub_pu &&
      (cm->quant_params.base_qindex >= sub_pu_qp_thr) &&
      (cm->current_frame.frame_type == INTER_FRAME || frame_is_sframe(cm)))
    cm->features.allow_lf_sub_pu = 1;
  else
    cm->features.allow_lf_sub_pu = 0;

  av1_enc_setup_tip_frame(cpi);

  cm->current_frame.skip_mode_info.skip_mode_flag =
      check_skip_mode_enabled(cpi) && cpi->oxcf.tool_cfg.enable_skip_mode;

  enc_row_mt->sync_read_ptr = av1_row_mt_sync_read_dummy;
  enc_row_mt->sync_write_ptr = av1_row_mt_sync_write_dummy;
  mt_info->row_mt_enabled = 0;

  if (oxcf->row_mt && (mt_info->num_workers > 1)) {
    mt_info->row_mt_enabled = 1;
    enc_row_mt->sync_read_ptr = av1_row_mt_sync_read;
    enc_row_mt->sync_write_ptr = av1_row_mt_sync_write;
    av1_encode_tiles_row_mt(cpi);
  } else {
    if (AOMMIN(mt_info->num_workers, cm->tiles.cols * cm->tiles.rows) > 1)
      av1_encode_tiles_mt(cpi);
    else
      encode_tiles(cpi);
  }

  // If intrabc is allowed but never selected, reset the allow_intrabc flag.
  if (features->allow_intrabc && !cpi->intrabc_used) {
    features->allow_intrabc = 0;
  }
  if (cm->delta_q_info.delta_q_present_flag && cpi->deltaq_used == 0) {
    cm->delta_q_info.delta_q_present_flag = 0;
  }

  // Set the transform size appropriately before bitstream creation
  const MODE_EVAL_TYPE eval_type =
      cpi->sf.winner_mode_sf.enable_winner_mode_for_tx_size_srch
          ? WINNER_MODE_EVAL
          : DEFAULT_EVAL;
  const TX_SIZE_SEARCH_METHOD tx_search_type =
      cpi->winner_mode_params.tx_size_search_methods[eval_type];
  assert(oxcf->txfm_cfg.enable_tx64 || tx_search_type != USE_LARGESTALL);
  features->tx_mode = select_tx_mode(cm, tx_search_type);

  if (cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats) {
    const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);

    for (i = 0; i < TX_SIZES_ALL; i++) {
      int sum = 0;
      int j;
      int left = 1024;

      for (j = 0; j < TX_TYPES; j++)
        sum += cpi->td.rd_counts.tx_type_used[i][j];

      for (j = TX_TYPES - 1; j >= 0; j--) {
        const int new_prob =
            sum ? 1024 * cpi->td.rd_counts.tx_type_used[i][j] / sum
                : (j ? 0 : 1024);
        int prob =
            (frame_probs->tx_type_probs[update_type][i][j] + new_prob) >> 1;
        left -= prob;
        if (j == 0) prob += left;
        frame_probs->tx_type_probs[update_type][i][j] = prob;
      }
    }
  }

  if (cpi->sf.inter_sf.prune_warped_prob_thresh > 0) {
    const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
    int sum = 0;
    for (i = 0; i < 2; i++) sum += cpi->td.rd_counts.warped_used[i];
    const int new_prob = sum ? 128 * cpi->td.rd_counts.warped_used[1] / sum : 0;
    frame_probs->warped_probs[update_type] =
        (frame_probs->warped_probs[update_type] + new_prob) >> 1;
  }

  if (cpi->sf.inter_sf.prune_warpmv_prob_thresh > 0) {
    const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
    int sum = 0;
    for (i = 0; i < 2; i++) sum += cpi->td.rd_counts.warped_used[i];
    const int new_prob = sum ? 128 * cpi->td.rd_counts.warped_used[1] / sum : 0;
    frame_probs->warped_probs[update_type] =
        (frame_probs->warped_probs[update_type] + new_prob) >> 1;
  }

  if ((!is_stat_generation_stage(cpi) && av1_use_hash_me(cpi)) ||
      hash_table_created) {
    av1_hash_table_destroy(&intrabc_hash_info->intrabc_hash_table);
  }
}

/*!\brief Setup reference frame buffers and encode a frame
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi    Top-level encoder structure
 */
void av1_encode_frame(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
  const int num_planes = av1_num_planes(cm);
  // Indicates whether or not to use a default reduced set for ext-tx
  // rather than the potential full set of 16 transforms
  features->reduced_tx_set_used = cpi->oxcf.txfm_cfg.reduced_tx_type_set;

  if (cm->bru.enabled && features->all_lossless) {
    init_bru_params(cm);
    memset(cm->bru.active_mode_map, 2, sizeof(uint8_t) * cm->bru.total_units);
  }
  // Make sure segment_id is no larger than last_active_segid.
  if (cm->seg.enabled && cm->seg.update_map) {
    const int mi_rows = cm->mi_params.mi_rows;
    const int mi_cols = cm->mi_params.mi_cols;
    const int last_active_segid = cm->seg.last_active_segid;
    uint8_t *map = cpi->enc_seg.map;
    for (int mi_row = 0; mi_row < mi_rows; ++mi_row) {
      for (int mi_col = 0; mi_col < mi_cols; ++mi_col) {
        map[mi_col] = AOMMIN(map[mi_col], last_active_segid);
      }
      map += mi_cols;
    }
  }
  // av1_set_lossless(cpi, xd);
  // av1_init_quantizer(&cm->seq_params, &cpi->enc_quant_dequant_params, cm);

  av1_setup_frame_buf_refs(cm);
  enforce_max_ref_frames(cpi, &cm->ref_frame_flags);
  set_rel_frame_dist(cm, &cpi->ref_frame_dist_info, cm->ref_frame_flags);
  av1_setup_frame_sign_bias(cm);
  cpi->palette_pixel_num = 0;
#if CONFIG_MISMATCH_DEBUG
  mismatch_reset_frame(num_planes);
#else
  (void)num_planes;
#endif  // CONFIG_MISMATCH_DEBUG

  if (cpi->sf.hl_sf.frame_parameter_update) {
    RD_COUNTS *const rdc = &cpi->td.rd_counts;

    if (frame_is_intra_only(cm))
      current_frame->reference_mode = SINGLE_REFERENCE;
    else
      current_frame->reference_mode = REFERENCE_MODE_SELECT;

    features->interp_filter = SWITCHABLE;

    rdc->compound_ref_used_flag = 0;
    rdc->skip_mode_used_flag = 0;

    if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
      // Auto mode: encoder decides which refine type to use for each frame.
      // For now, set all frame to REFINE_SWITCHABLE. The search or heuristic
      // that encoder can use is left for future work.
      features->opfl_refine_type = REFINE_SWITCHABLE;
      if (cm->seq_params.enable_tip &&
          features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
        features->opfl_refine_type = REFINE_ALL;
      }
    } else {
      // 0: REFINE_NONE, 1: REFINE_SWTICHABLE, 2: REFINE_ALL
      features->opfl_refine_type = cm->seq_params.enable_opfl_refine;
    }

    encode_frame_internal(cpi);

    if (current_frame->reference_mode == REFERENCE_MODE_SELECT) {
      // Use a flag that includes 4x4 blocks
      if (rdc->compound_ref_used_flag == 0) {
        current_frame->reference_mode = SINGLE_REFERENCE;
#if CONFIG_ENTROPY_STATS
        av1_zero(cpi->td.counts->comp_inter);
#endif  // CONFIG_ENTROPY_STATS
      }
    }
    // Re-check on the skip mode status as reference mode may have been
    // changed.
    SkipModeInfo *const skip_mode_info = &current_frame->skip_mode_info;
    if (frame_is_intra_only(cm)
        // This line should be added, however it will have stats change, can be
        // enabled as bug fix if confirmed.
        //        || current_frame->reference_mode == SINGLE_REFERENCE
    ) {
      skip_mode_info->skip_mode_allowed = 0;
      skip_mode_info->skip_mode_flag = 0;
    }
    if (skip_mode_info->skip_mode_flag && rdc->skip_mode_used_flag == 0)
      skip_mode_info->skip_mode_flag = 0;

    if (features->tx_mode == TX_MODE_SELECT &&
        cpi->td.mb.txfm_search_info.txb_split_count == 0)
      features->tx_mode = TX_MODE_LARGEST;
  } else {
    encode_frame_internal(cpi);
  }
}
