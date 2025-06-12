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

#include "av1/common/av1_common_int.h"
#include "av1/common/intra_dip.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/intra_mode_search.h"
#include "av1/encoder/intra_mode_search_utils.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/tx_search.h"
#include "av1/common/reconinter.h"
#if CONFIG_DIP_EXT_PRUNING
#include "av1/encoder/intra_dip_mode_prune_tflite.h"
#endif  // CONFIG_DIP_EXT_PRUNING

#if CONFIG_DIP_EXT_PRUNING
// Check if the mode is found in the modelrd list
static uint8_t skip_this_dip_mode(
    DIPModeRDInfo intra_model_rds[TOP_DIP_INTRA_MODEL_COUNT], int this_mode) {
  uint8_t mode_found = 0;
  for (int i = 0; i < TOP_DIP_INTRA_MODEL_COUNT; i++) {
    if (this_mode == intra_model_rds[i].intra_dip_mode &&
        intra_model_rds[i].modelrd != INT64_MAX) {
      mode_found = 1;
      break;
    }
  }
  return !mode_found;
}

static void rd_pick_intra_dip_sby_modelrd(
    const AV1_COMP *const cpi, MACROBLOCK *x, BLOCK_SIZE bsize, int mode_cost,
    DIPModeRDInfo intra_model_rds[TOP_DIP_INTRA_MODEL_COUNT],
    float dip_mode_model_log_rd[12]) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];

  int num_modes = av1_intra_dip_modes(bsize);
  int has_transpose = av1_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

  for (int i = 0; i < TOP_DIP_INTRA_MODEL_COUNT; i++) {
    intra_model_rds[i].modelrd = INT64_MAX;
  }

  for (int transpose = 0; transpose < num_transpose; transpose++) {
    for (int ml_mode = 0; ml_mode < num_modes; ml_mode++) {
      int mode = (transpose << 4) + ml_mode;
      int dip_index = ml_mode + transpose * num_modes;
      mbmi->intra_dip_mode = mode;
      const int64_t this_model_rd = intra_model_yrd(cpi, x, bsize, mode_cost);
      dip_mode_model_log_rd[dip_index] =
          log10f((float)AOMMAX(this_model_rd, 1));

      for (int i = 0; i < TOP_DIP_INTRA_MODEL_COUNT; i++) {
        if (this_model_rd < intra_model_rds[i].modelrd) {
          for (int j = TOP_DIP_INTRA_MODEL_COUNT - 1; j > i; j--) {
            intra_model_rds[j] = intra_model_rds[j - 1];
          }
          intra_model_rds[i].modelrd = this_model_rd;
          intra_model_rds[i].intra_dip_mode = mbmi->intra_dip_mode;
          break;
        }
      }
    }
  }
}
/*! \brief Extra features passed to DIP pruning model. */
struct extra_dip_info {
  /*! \brief Whether any intra mode beats best_rd passed to intra search. */
  int beat_best_rd;
  /*! \brief RD cost of the DC mode. */
  int64_t dc_mode_rd;
  /*! \brief The best_rd passed to intra search. */
  int64_t orig_best_rd;
  /*! \brief Best non-DIP mode found during intra search. */
  int best_mode;
};
#endif  // CONFIG_DIP_EXT_PRUNING

/*!\brief Search for the best intra_dip mode when coding intra frame.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function loops through all intra_dip modes to find the best one.
 *
 * \return Returns 1 if a new intra_dip mode is selected; 0 otherwise.
 */
static int rd_pick_intra_dip_sby(const AV1_COMP *const cpi, ThreadData *td,
                                 MACROBLOCK *x, int *rate, int *rate_tokenonly,
                                 int64_t *distortion, int *skippable,
                                 BLOCK_SIZE bsize, int mode_cost,
                                 int64_t *best_rd, int64_t *best_model_rd,
                                 PICK_MODE_CONTEXT *ctx
#if CONFIG_DIP_EXT_PRUNING
                                 ,
                                 struct extra_dip_info *extra
#endif  // CONFIG_DIP_EXT_PRUNING

) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  int intra_dip_selected_flag = 0;
  int best_ml_mode = 0;
  TX_SIZE best_tx_size = TX_8X8;
  TX_PARTITION_TYPE best_tx_partition = TX_PARTITION_NONE;
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  (void)ctx;
  mbmi->use_intra_dip = 1;
  mbmi->mode = DC_PRED;
  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->mrl_index = 0;
  mbmi->multi_line_mrl = 0;
  if (xd->lossless[mbmi->segment_id]) {
    mbmi->use_dpcm_y = 0;
    mbmi->dpcm_mode_y = 0;
    mbmi->use_dpcm_uv = 0;
    mbmi->dpcm_mode_uv = 0;
  }
  mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
  mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
#if CONFIG_NEW_CONTEXT_MODELING
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
#endif  // CONFIG_NEW_CONTEXT_MODELING
  mbmi->joint_y_mode_delta_angle = DC_PRED;
  mbmi->y_mode_idx = DC_PRED;

  mbmi->angle_delta[PLANE_TYPE_Y] = 0;
  mbmi->angle_delta[PLANE_TYPE_UV] = 0;
  int num_modes = av1_intra_dip_modes(bsize);
  int has_transpose = av1_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

#if CONFIG_DIP_EXT_PRUNING
  float dip_mode_model_log_rd[12] = {};

  MB_MODE_INFO base_mbmi = *mbmi;
  DIPModeRDInfo intra_model_rds[TOP_DIP_INTRA_MODEL_COUNT];

  rd_pick_intra_dip_sby_modelrd(cpi, x, bsize, mode_cost, intra_model_rds,
                                dip_mode_model_log_rd);
  int16_t intra_dip_features[11];
  for (int i = 0; i < 11; i++) {
    intra_dip_features[i] = mbmi->intra_dip_features[i];
  }

  *mbmi = base_mbmi;
  const int dc_q =
      av1_dc_quant_QTX(x->qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                       xd->bd) >>
      (xd->bd - 8);

  // Clamp all RD values to a minimum of 1 to avoid arithmetic exceptions.
  const float log_best_rd = log10f((float)AOMMAX(*best_rd, 1));
  const float log_best_model_rd = log10f((float)AOMMAX(*best_model_rd, 1));
  const float log_dc_mode_rd = log10f((float)AOMMAX(extra->dc_mode_rd, 1));
  const float log_orig_best_rd = log10f((float)AOMMAX(extra->orig_best_rd, 1));
  bool pred_keep = true;
  int adjusted_qindex = x->qindex - MAXQ_OFFSET * (xd->bd - 8);
  int dip_model_index = intra_dip_mode_prune_get_model_index(adjusted_qindex);
  if (dip_model_index != -1) {
    dip_pruning_inputs *dip_pruning_in = intra_dip_mode_prune_get_inputs(
        &td->dip_pruning_model, adjusted_qindex);

    dip_pruning_in->inputs[0].values[0] = log_best_model_rd;
    dip_pruning_in->inputs[0].values[1] = log_best_rd;
    dip_pruning_in->inputs[0].values[2] = log10f(dc_q);
    dip_pruning_in->inputs[0].values[3] = log_dc_mode_rd;
    dip_pruning_in->inputs[0].values[4] = log_orig_best_rd;
    for (int i = 0; i < 13; i++) {
      dip_pruning_in->inputs[0].values[5 + i] = (float)(extra->best_mode == i);
    }
    dip_pruning_in->inputs[0].values[18] = extra->beat_best_rd;
    intra_dip_mode_prune_normalize_and_resize_8x8(
        x->plane[0].src.buf, x->plane[0].src.stride, xd->bd,
        block_size_wide[bsize], block_size_high[bsize],
        &dip_pruning_in->inputs[1].values[0]);
    float norm = (float)((1 << xd->bd) - 1);
    for (int i = 0; i < 11; i++) {
      dip_pruning_in->inputs[2].values[i] = (float)intra_dip_features[i] / norm;
    }
    dip_pruning_in->inputs[3].values[0] = log2f((float)block_size_wide[bsize]);
    dip_pruning_in->inputs[3].values[1] = log2f((float)block_size_high[bsize]);
    for (int i = 0; i < 12; i++) {
      dip_pruning_in->inputs[4].values[i] = dip_mode_model_log_rd[i];
    }
    float dip_prune_output = -1.0;
    intra_dip_mode_prune_tflite(&td->dip_pruning_model, &dip_prune_output,
                                adjusted_qindex);
    float dip_prune_threshold = DIP_PRUNING_THRESHOLDS[dip_model_index];
    pred_keep = dip_prune_output >= dip_prune_threshold;
  }
  if (pred_keep) {
#else
  (void)td;
#endif  // CONFIG_DIP_EXT_PRUNING
    for (int transpose = 0; transpose < num_transpose; transpose++) {
      for (int ml_mode = 0; ml_mode < num_modes; ml_mode++) {
        int mode = (transpose << 4) + ml_mode;
        mbmi->intra_dip_mode = mode;
#if CONFIG_DIP_EXT_PRUNING
        if (skip_this_dip_mode(intra_model_rds, mbmi->intra_dip_mode)) continue;
#else
      if (model_intra_yrd_and_prune(cpi, x, bsize, mode_cost, best_model_rd)) {
        continue;
      }
#endif  // CONFIG_DIP_EXT_PRUNING

        int64_t this_rd;
        RD_STATS tokenonly_rd_stats;
        av1_pick_uniform_tx_size_type_yrd(cpi, x, &tokenonly_rd_stats, bsize,
                                          *best_rd);
        if (tokenonly_rd_stats.rate != INT_MAX) {
          const int this_rate =
              tokenonly_rd_stats.rate +
              intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_cost);
          this_rd = RDCOST(x->rdmult, this_rate, tokenonly_rd_stats.dist);
          // Collect mode stats for multiwinner mode processing
          const int txfm_search_done = 1;
          const MV_REFERENCE_FRAME refs[2] = { -1, -1 };
          store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, refs,
                                  0, NULL, bsize, this_rd,
                                  cpi->sf.winner_mode_sf.multi_winner_mode_type,
                                  txfm_search_done);
          if (this_rd < *best_rd) {
            *best_rd = this_rd;
            best_tx_size = mbmi->tx_size;
            best_tx_partition = mbmi->tx_partition_type[0];
            av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
            memcpy(ctx->blk_skip[AOM_PLANE_Y],
                   x->txfm_search_info.blk_skip[AOM_PLANE_Y],
                   sizeof(*x->txfm_search_info.blk_skip[AOM_PLANE_Y]) *
                       ctx->num_4x4_blk);
            *rate = this_rate;
            *rate_tokenonly = tokenonly_rd_stats.rate;
            *distortion = tokenonly_rd_stats.dist;
            *skippable = tokenonly_rd_stats.skip_txfm;
            intra_dip_selected_flag = 1;
            best_ml_mode = mode;
          }
        }
      }
    }
#if CONFIG_DIP_EXT_PRUNING
  }
#endif  // CONFIG_DIP_EXT_PRUNING

  if (intra_dip_selected_flag) {
    mbmi->intra_dip_mode = best_ml_mode;
    mbmi->mode = DC_PRED;
    mbmi->tx_size = best_tx_size;
    mbmi->tx_partition_type[0] = best_tx_partition;
    av1_copy_array(ctx->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
    mbmi->joint_y_mode_delta_angle = DC_PRED;
    mbmi->y_mode_idx = DC_PRED;
    mbmi->angle_delta[PLANE_TYPE_Y] = 0;
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_dpcm_uv = 0;
      mbmi->dpcm_mode_uv = 0;
    }
    mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
    mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
#if CONFIG_NEW_CONTEXT_MODELING
    mbmi->use_intrabc[0] = 0;
    mbmi->use_intrabc[1] = 0;
#endif  // CONFIG_NEW_CONTEXT_MODELING
    return 1;
  } else {
    mbmi->use_intra_dip = 0;
    return 0;
  }
}

void av1_count_colors_highbd(const uint16_t *src, int stride, int rows,
                             int cols, int bit_depth, int *val_count,
                             int *bin_val_count, int *num_color_bins,
                             int *num_colors) {
  assert(bit_depth <= 12);
  const int max_bin_val = 1 << 8;
  const int max_pix_val = 1 << bit_depth;
  memset(bin_val_count, 0, max_bin_val * sizeof(val_count[0]));
  if (val_count != NULL)
    memset(val_count, 0, max_pix_val * sizeof(val_count[0]));
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      /*
       * Down-convert the pixels to 8-bit domain before counting.
       * This provides consistency of behavior for palette search
       * between lbd and hbd encodes. This down-converted pixels
       * are only used for calculating the threshold (n).
       */
      const int this_val = ((src[r * stride + c]) >> (bit_depth - 8));
      assert(this_val < max_bin_val);
      if (this_val >= max_bin_val) continue;
      ++bin_val_count[this_val];
      if (val_count != NULL) ++val_count[(src[r * stride + c])];
    }
  }
  int n = 0;
  // Count the colors based on 8-bit domain used to gate the palette path
  for (int i = 0; i < max_bin_val; ++i) {
    if (bin_val_count[i]) ++n;
  }
  *num_color_bins = n;

  // Count the actual hbd colors used to create top_colors
  n = 0;
  if (val_count != NULL) {
    for (int i = 0; i < max_pix_val; ++i) {
      if (val_count[i]) ++n;
    }
    *num_colors = n;
  }
}

/*! \brief prune luma intra mode    based on the model rd.
 * \param[in]    this_model_rd      model rd for current mode.
 * \param[in]    best_model_rd      Best model RD seen for this block so
 *                                  far.
 * \param[in]    top_intra_model_rd Top intra model RD seen for this
 *                                  block so far.
 */
int prune_intra_y_mode(int64_t this_model_rd, int64_t *best_model_rd,
                       int64_t top_intra_model_rd[]) {
  const double thresh_top = 1.00;
  for (int i = 0; i < TOP_INTRA_MODEL_COUNT; i++) {
    if (this_model_rd < top_intra_model_rd[i]) {
      for (int j = TOP_INTRA_MODEL_COUNT - 1; j > i; j--) {
        top_intra_model_rd[j] = top_intra_model_rd[j - 1];
      }
      top_intra_model_rd[i] = this_model_rd;
      break;
    }
  }
  if (top_intra_model_rd[TOP_INTRA_MODEL_COUNT - 1] != INT64_MAX &&
      this_model_rd >
          thresh_top * top_intra_model_rd[TOP_INTRA_MODEL_COUNT - 1])
    return 1;

  if (this_model_rd < *best_model_rd) *best_model_rd = this_model_rd;
  return 0;
}

#define PLANE_SIGN_TO_JOINT_SIGN(plane, a, b) \
  (plane == CFL_PRED_U ? a * CFL_SIGNS + b - 1 : b * CFL_SIGNS + a - 1)
static int cfl_rd_pick_alpha(MACROBLOCK *const x, const AV1_COMP *const cpi,
                             TX_SIZE tx_size, int64_t best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const MACROBLOCKD_PLANE *pd = &xd->plane[AOM_PLANE_U];
  const ModeCosts *mode_costs = &x->mode_costs;
  assert(xd->tree_type != LUMA_PART);
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, mbmi, PLANE_TYPE_UV, pd->subsampling_x, pd->subsampling_y);

  assert(is_cfl_allowed(xd) && cpi->oxcf.intra_mode_cfg.enable_cfl_intra);
  assert(plane_bsize < BLOCK_SIZES_ALL);
  if (!xd->lossless[mbmi->segment_id]) {
    assert(block_size_wide[plane_bsize] == tx_size_wide[tx_size]);
    assert(block_size_high[plane_bsize] == tx_size_high[tx_size]);
  }

  xd->cfl.use_dc_pred_cache = 1;
  xd->cfl.dc_pred_is_cached[0] = 0;
  xd->cfl.dc_pred_is_cached[1] = 0;
  const int cfl_ctx = get_cfl_ctx(xd);
  ;
  const int64_t mode_rd =
      RDCOST(x->rdmult, mode_costs->cfl_mode_cost[cfl_ctx][1], 0);

  int64_t best_rd_uv[CFL_JOINT_SIGNS][CFL_PRED_PLANES];
  int best_c[CFL_JOINT_SIGNS][CFL_PRED_PLANES];

#if CONFIG_CONTEXT_DERIVATION
  const int skip_trellis = 0;
  int8_t best_joint_sign = -1;
  // process CFL_PRED_U
  RD_STATS rd_stats;
  av1_init_rd_stats(&rd_stats);
  for (int plane = 0; plane < CFL_PRED_PLANES; plane++) {
    for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
      best_rd_uv[joint_sign][plane] = INT64_MAX;
      best_c[joint_sign][plane] = 0;
    }
  }
  // Collect RD stats for an alpha value of zero in CFL_PRED_U.
  // Skip i == CFL_SIGN_ZERO as (0, 0) is invalid.
  for (int i = CFL_SIGN_NEG; i < CFL_SIGNS; i++) {
    const int8_t joint_sign =
        PLANE_SIGN_TO_JOINT_SIGN(CFL_PRED_U, CFL_SIGN_ZERO, i);
    mbmi->cfl_alpha_idx = 0;
    mbmi->cfl_alpha_signs = joint_sign;
    av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize, tx_size,
                         FTXS_NONE, skip_trellis);
    if (rd_stats.rate == INT_MAX) break;
    const int alpha_rate = mode_costs->cfl_cost[joint_sign][CFL_PRED_U][0];
    best_rd_uv[joint_sign][CFL_PRED_U] =
        RDCOST(x->rdmult, rd_stats.rate + alpha_rate, rd_stats.dist);
  }
  // Collect RD stats for alpha values other than zero in CFL_PRED_U.
  for (int pn_sign = CFL_SIGN_NEG; pn_sign < CFL_SIGNS; pn_sign++) {
    int progress = 0;
    for (int c = 0; c < CFL_ALPHABET_SIZE; c++) {
      int flag = 0;
      if (c > 2 && progress < c) break;
      av1_init_rd_stats(&rd_stats);
      for (int i = 0; i < CFL_SIGNS; i++) {
        const int8_t joint_sign =
            PLANE_SIGN_TO_JOINT_SIGN(CFL_PRED_U, pn_sign, i);
        mbmi->cfl_alpha_idx = (c << CFL_ALPHABET_SIZE_LOG2) + c;
        mbmi->cfl_alpha_signs = joint_sign;
        av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize,
                             tx_size, FTXS_NONE, skip_trellis);
        if (rd_stats.rate == INT_MAX) break;
        const int alpha_rate = mode_costs->cfl_cost[joint_sign][CFL_PRED_U][c];
        int64_t this_rd =
            RDCOST(x->rdmult, rd_stats.rate + alpha_rate, rd_stats.dist);
        if (this_rd >= best_rd_uv[joint_sign][CFL_PRED_U]) continue;
        best_rd_uv[joint_sign][CFL_PRED_U] = this_rd;
        best_c[joint_sign][CFL_PRED_U] = c;
        flag = 2;
        if (best_rd_uv[joint_sign][CFL_PRED_V] == INT64_MAX) continue;
        this_rd += mode_rd + best_rd_uv[joint_sign][CFL_PRED_V];
        if (this_rd >= best_rd) continue;
        best_rd = this_rd;
        best_joint_sign = joint_sign;
      }
      progress += flag;
    }
  }
  // process CFL_PRED_V
  // Collect RD stats for all alpha values and joint_signs for CFL_PRED_V
  // taking into consideration the best alpha for CFL_PRED_U for that
  // joint_sign. This is necessary due to cross component dependency from
  // CONFIG_CONTEXT_DERIVATION. The combined (CFL_PRED_U and CFL_PRED_V) RDCOST
  // will be used to decide the best_joint_sign.
  for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
    int progress = 0;
    for (int c = 0; c < CFL_ALPHABET_SIZE; c++) {
      int flag = 0;
      if (c > 2 && progress < c) break;
      av1_init_rd_stats(&rd_stats);
      mbmi->cfl_alpha_idx =
          (best_c[joint_sign][CFL_PRED_U] << CFL_ALPHABET_SIZE_LOG2) + c;
      mbmi->cfl_alpha_signs = joint_sign;
      av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize,
                           tx_size, FTXS_NONE, skip_trellis);
      av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 2, plane_bsize,
                           tx_size, FTXS_NONE, skip_trellis);
      if (rd_stats.rate == INT_MAX) break;
      const int alpha_rate = mode_costs->cfl_cost[joint_sign][CFL_PRED_V][c];
      int64_t this_rd =
          RDCOST(x->rdmult, rd_stats.rate + alpha_rate, rd_stats.dist);
      if (this_rd >= best_rd_uv[joint_sign][CFL_PRED_V]) continue;
      best_rd_uv[joint_sign][CFL_PRED_V] = this_rd;
      best_c[joint_sign][CFL_PRED_V] = c;
      flag = 2;
      if (best_rd_uv[joint_sign][CFL_PRED_U] == INT64_MAX) continue;
      this_rd += mode_rd + best_rd_uv[joint_sign][CFL_PRED_U];
      if (this_rd >= best_rd) continue;
      best_rd = this_rd;
      best_joint_sign = joint_sign;
      progress += flag;
    }
  }
#else
  const int skip_trellis = 0;
  for (int plane = 0; plane < CFL_PRED_PLANES; plane++) {
    RD_STATS rd_stats;
    av1_init_rd_stats(&rd_stats);
    for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
      best_rd_uv[joint_sign][plane] = INT64_MAX;
      best_c[joint_sign][plane] = 0;
    }
    // Collect RD stats for an alpha value of zero in this plane.
    // Skip i == CFL_SIGN_ZERO as (0, 0) is invalid.
    for (int i = CFL_SIGN_NEG; i < CFL_SIGNS; i++) {
      const int8_t joint_sign =
          PLANE_SIGN_TO_JOINT_SIGN(plane, CFL_SIGN_ZERO, i);
      if (i == CFL_SIGN_NEG) {
        mbmi->cfl_alpha_idx = 0;
        mbmi->cfl_alpha_signs = joint_sign;
        av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, plane + 1,
                             plane_bsize, tx_size, FTXS_NONE, skip_trellis);
        if (rd_stats.rate == INT_MAX) break;
      }
      const int alpha_rate = mode_costs->cfl_cost[joint_sign][plane][0];
      best_rd_uv[joint_sign][plane] =
          RDCOST(x->rdmult, rd_stats.rate + alpha_rate, rd_stats.dist);
    }
  }

  int8_t best_joint_sign = -1;

  for (int plane = 0; plane < CFL_PRED_PLANES; plane++) {
    for (int pn_sign = CFL_SIGN_NEG; pn_sign < CFL_SIGNS; pn_sign++) {
      int progress = 0;
      for (int c = 0; c < CFL_ALPHABET_SIZE; c++) {
        int flag = 0;
        RD_STATS rd_stats;
        if (c > 2 && progress < c) break;
        av1_init_rd_stats(&rd_stats);
        for (int i = 0; i < CFL_SIGNS; i++) {
          const int8_t joint_sign = PLANE_SIGN_TO_JOINT_SIGN(plane, pn_sign, i);
          if (i == 0) {
            mbmi->cfl_alpha_idx = (c << CFL_ALPHABET_SIZE_LOG2) + c;
            mbmi->cfl_alpha_signs = joint_sign;
            av1_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, plane + 1,
                                 plane_bsize, tx_size, FTXS_NONE, skip_trellis);
            if (rd_stats.rate == INT_MAX) break;
          }
          const int alpha_rate = mode_costs->cfl_cost[joint_sign][plane][c];
          int64_t this_rd =
              RDCOST(x->rdmult, rd_stats.rate + alpha_rate, rd_stats.dist);
          if (this_rd >= best_rd_uv[joint_sign][plane]) continue;
          best_rd_uv[joint_sign][plane] = this_rd;
          best_c[joint_sign][plane] = c;
          flag = 2;
          if (best_rd_uv[joint_sign][!plane] == INT64_MAX) continue;
          this_rd += mode_rd + best_rd_uv[joint_sign][!plane];
          if (this_rd >= best_rd) continue;
          best_rd = this_rd;
          best_joint_sign = joint_sign;
        }
        progress += flag;
      }
    }
  }
#endif

  int best_rate_overhead = INT_MAX;
  uint8_t ind = 0;
  if (best_joint_sign >= 0) {
    const int u = best_c[best_joint_sign][CFL_PRED_U];
    const int v = best_c[best_joint_sign][CFL_PRED_V];
    ind = (u << CFL_ALPHABET_SIZE_LOG2) + v;
    best_rate_overhead = mode_costs->cfl_cost[best_joint_sign][CFL_PRED_U][u] +
                         mode_costs->cfl_cost[best_joint_sign][CFL_PRED_V][v];
  } else {
    best_joint_sign = 0;
  }

  mbmi->cfl_alpha_idx = ind;
  mbmi->cfl_alpha_signs = best_joint_sign;
  xd->cfl.use_dc_pred_cache = 0;
  xd->cfl.dc_pred_is_cached[0] = 0;
  xd->cfl.dc_pred_is_cached[1] = 0;
  return best_rate_overhead;
}

int get_uv_mode_cost(MB_MODE_INFO *mbmi, const ModeCosts mode_costs,
                     MACROBLOCKD *xd, CFL_ALLOWED_TYPE cfl_allowed,
                     int mode_index) {
  assert(mode_index < UV_CFL_PRED);
  const int uv_context = av1_is_directional_mode(mbmi->mode) ? 1 : 0;
  if (cfl_allowed) {
    const int cfl_ctx = get_cfl_ctx(xd);
    if (mbmi->uv_mode == UV_CFL_PRED) {
      return mode_costs.cfl_mode_cost[cfl_ctx][1];
    }
    int cost = mode_costs.cfl_mode_cost[cfl_ctx][0];
    cost += mode_costs.intra_uv_mode_cost[uv_context][mode_index];
    return cost;
  }
  return mode_costs.intra_uv_mode_cost[uv_context][mode_index];
}

// For a given chroma (UV) mode, compute a specific index and use the same to
// store/fetch rate and distortion information.
// The below specifies the index given for chroma modes:
// 0       : for UV_DC_PRED.
// 1 - 56  : for directional UV modes.
// 57 - 60 : for non-directional UV modes (i.e., UV_SMOOTH_PRED,
// UV_SMOOTH_V_PRED, UV_SMOOTH_H_PRED, UV_PAETH_PRED)
static AOM_INLINE int get_chroma_idx_for_reuse_uvrd(MB_MODE_INFO *mbmi) {
  const int uv_angle = mbmi->angle_delta[AOM_PLANE_U];
  int chroma_idx = 0;
  if (av1_is_directional_mode(get_uv_mode(mbmi->uv_mode))) {
    chroma_idx = 1;
    chroma_idx += (mbmi->uv_mode - 1) * TOTAL_ANGLE_DELTA_COUNT;
    chroma_idx += (uv_angle < 0) ? (abs(uv_angle) + MAX_ANGLE_DELTA) : uv_angle;
  } else if (mbmi->uv_mode != UV_DC_PRED) {
    chroma_idx = LUMA_MODE_COUNT - NON_DIRECTIONAL_MODES_COUNT +
                 (mbmi->uv_mode - DIRECTIONAL_MODES);
  }
  assert(chroma_idx >= 0 && chroma_idx < LUMA_MODE_COUNT);
  return chroma_idx;
}

// Stores the UV mode RD information during the first evaluation.
static AOM_INLINE void store_uv_mode_rd_info(ModeRDInfoUV *mode_rd_info_uv,
                                             RD_STATS *tokenonly_rd_stats,
                                             const int chroma_idx) {
  mode_rd_info_uv->dist_info[chroma_idx] = tokenonly_rd_stats->dist;
  mode_rd_info_uv->rate_info[chroma_idx] = tokenonly_rd_stats->rate;
  mode_rd_info_uv->mode_evaluated[chroma_idx] = true;
}
// Fetch and reuse the UV mode RD information.
static AOM_INLINE void fetch_uv_mode_rd_info(ModeRDInfoUV *mode_rd_info_uv,
                                             RD_STATS *tokenonly_rd_stats,
                                             const int chroma_idx) {
  av1_init_rd_stats(tokenonly_rd_stats);
  tokenonly_rd_stats->dist = mode_rd_info_uv->dist_info[chroma_idx];
  tokenonly_rd_stats->rate = mode_rd_info_uv->rate_info[chroma_idx];
}

int64_t av1_rd_pick_intra_sbuv_mode(const AV1_COMP *const cpi, MACROBLOCK *x,
                                    int *rate, int *rate_tokenonly,
                                    int64_t *distortion, int *skippable,
                                    const PICK_MODE_CONTEXT *ctx,
                                    BLOCK_SIZE bsize, TX_SIZE max_tx_size,
                                    ModeRDInfoUV *mode_rd_info_uv) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  assert(!is_inter_block(mbmi, xd->tree_type));
  MB_MODE_INFO best_mbmi = *mbmi;
  int64_t best_rd = INT64_MAX, this_rd;
  const ModeCosts *mode_costs = &x->mode_costs;
  const IntraModeCfg *const intra_mode_cfg = &cpi->oxcf.intra_mode_cfg;
  // Temporary buffer to hold the best cross-chroma txfm type corresponds
  // to best chroma mode of a given partition block.
  CctxType tmp_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];

  init_sbuv_mode(mbmi);

  mbmi->is_wide_angle[1][0] = 0;
  mbmi->mapped_intra_mode[1][0] = DC_PRED;

  // Return if the current block does not correspond to a chroma block.
  if (!xd->is_chroma_ref) {
    *rate = 0;
    *rate_tokenonly = 0;
    *distortion = 0;
    *skippable = 1;
    return INT64_MAX;
  }

  // Only store reconstructed luma when there's chroma RDO. When there's no
  // chroma RDO, the reconstructed luma will be stored in encode_superblock().
  if (frame_is_intra_only(cm) || xd->tree_type != CHROMA_PART)
    xd->cfl.store_y = store_cfl_required_rdo(cm, x);
  if (xd->tree_type == SHARED_PART) {
    if (xd->cfl.store_y) {
      av1_encode_intra_block_plane(cpi, x, mbmi->sb_type[PLANE_TYPE_Y],
                                   AOM_PLANE_Y, DRY_RUN_NORMAL,
                                   cpi->optimize_seg_arr[mbmi->segment_id]);
      xd->cfl.store_y = 0;
    }
  }

  // Search through all non-palette modes.
  // Checks if the best mode chosen needs to be re-evaluated, applicable only
  // when the sf 'reuse_uv_mode_rd_info' is enabled.
  const int is_reeval_best_mode =
      mode_rd_info_uv != NULL && !xd->lossless[mbmi->segment_id];
  int best_uv_mode_idx = -1;
  get_uv_intra_mode_set(mbmi);
  int implicit_cfl_mode_num = 1 + MHCCP_MODE_NUM;
  mbmi->use_dpcm_uv = 0;
  mbmi->dpcm_mode_uv = 0;
  int dpcm_uv_loop_num = 1;
  if (xd->lossless[mbmi->segment_id]) {
    dpcm_uv_loop_num = 2;  // dpcm is only applied for lossless mode
  }
  for (int dpcm_uv_index = 0; dpcm_uv_index < dpcm_uv_loop_num;
       ++dpcm_uv_index) {
    mbmi->use_dpcm_uv = dpcm_uv_index;
    const int mode_loop_count = UV_INTRA_MODES + implicit_cfl_mode_num;
    for (int mode_idx = 0; mode_idx < mode_loop_count + is_reeval_best_mode;
         ++mode_idx) {
      // If the best mode is chosen based on stored UV modes RD information, use
      // the last iteration to re-evaluate the same. This ensures appropriate
      // update of 'mbmi' and 'cctx_type_map' which may be required for the
      // computation of recon buffer.
      const int is_reevaluation = (mode_idx >= mode_loop_count);
      if (is_reevaluation) {
        if (best_uv_mode_idx == -1) continue;
        mode_idx = best_uv_mode_idx;
      }
      if (!xd->lossless[mbmi->segment_id] && dpcm_uv_index > 0) {
        continue;
      }
      mbmi->cfl_idx = 0;
      // Reorder modes to search. Let the encoder search CFL first, then the
      // rest modes.
      if (mode_idx == 0) {
        mbmi->cfl_idx = 0;
        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
      } else if (mode_idx == 1) {
        mbmi->cfl_idx = 1;
        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
      } else if (mode_idx == 2) {
        mbmi->cfl_idx = 2;
        mbmi->mh_dir = 0;
        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
      } else if (mode_idx == 3) {
        mbmi->cfl_idx = 2;
        mbmi->mh_dir = 1;
        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
      } else if (mode_idx == 4) {
        mbmi->cfl_idx = 2;
        mbmi->mh_dir = 2;
        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
      } else {
        mbmi->cfl_idx = 0;
        mbmi->uv_mode = mbmi->uv_intra_mode_list[mode_idx - 5];
        mbmi->uv_mode_idx = mode_idx - 5;
      }
      if (mbmi->uv_mode == mbmi->mode)
        mbmi->angle_delta[PLANE_TYPE_UV] = mbmi->angle_delta[PLANE_TYPE_Y];
      else
        mbmi->angle_delta[PLANE_TYPE_UV] = 0;
      UV_PREDICTION_MODE mode = mbmi->uv_mode;
      if (dpcm_uv_index > 0 && ((mode != V_PRED && mode != H_PRED))) {
        continue;
      }
      int mode_cost = 0;
      if (xd->lossless[mbmi->segment_id]) {
        int dpcm_uv_cost = x->mode_costs.dpcm_uv_cost[dpcm_uv_index];
        mode_cost += dpcm_uv_cost;
      }
      if (mbmi->use_dpcm_uv == 0) {
        mode_cost += get_uv_mode_cost(mbmi, x->mode_costs, xd,
                                      is_cfl_allowed(xd), mbmi->uv_mode_idx);
      } else {
        mbmi->dpcm_mode_uv = mode - 1;
        int dpcm_uv_dir_cost =
            x->mode_costs.dpcm_uv_vert_horz_cost[mbmi->dpcm_mode_uv];
        mode_cost += dpcm_uv_dir_cost;
      }
      int this_rate;
      RD_STATS tokenonly_rd_stats;
      if (!(cpi->sf.intra_sf
                .intra_uv_mode_mask[txsize_sqr_up_map[max_tx_size]] &
            (1 << mode)))
        continue;
      if (!intra_mode_cfg->enable_smooth_intra && mode >= UV_SMOOTH_PRED &&
          mode <= UV_SMOOTH_H_PRED)
        continue;

      if (!intra_mode_cfg->enable_paeth_intra && mode == UV_PAETH_PRED)
        continue;

      // Init variables for cfl and angle delta
      int cfl_alpha_rate = 0;
      int filter_dir_rate = 0;
      int cfl_idx_rate = 0;
      if (mode == UV_CFL_PRED) {
        if (!is_cfl_allowed(xd) || !intra_mode_cfg->enable_cfl_intra) continue;
#if CONFIG_SDP_CFL_LATENCY_FIX
        assert(IMPLIES(xd->tree_type == CHROMA_PART,
                       xd->is_cfl_allowed_in_sdp == CFL_ALLOWED_FOR_CHROMA));
#endif  // CONFIG_SDP_CFL_LATENCY_FIX
#if MHCCP_RUNTIME_FLAG
        if (mbmi->cfl_idx == CFL_MULTI_PARAM && !intra_mode_cfg->enable_mhccp)
          continue;
#else
        if (mbmi->cfl_idx == CFL_MULTI_PARAM_V && !intra_mode_cfg->enable_mhccp)
          continue;
#endif  // MHCCP_RUNTIME_FLAG
        const TX_SIZE uv_tx_size = av1_get_tx_size(AOM_PLANE_U, xd);
        if (mbmi->cfl_idx == 0)
          cfl_alpha_rate = cfl_rd_pick_alpha(x, cpi, uv_tx_size, best_rd);
#if MHCCP_RUNTIME_FLAG
        if (intra_mode_cfg->enable_mhccp) {
          cfl_idx_rate +=
              x->mode_costs.cfl_mhccp_cost[mbmi->cfl_idx == CFL_MULTI_PARAM];
        }
        if (mbmi->cfl_idx != CFL_MULTI_PARAM) {
          cfl_idx_rate += x->mode_costs.cfl_index_cost[mbmi->cfl_idx];
        }
#else
        cfl_idx_rate = x->mode_costs.cfl_index_cost[mbmi->cfl_idx];
#endif  // MHCCP_RUNTIME_FLAG
#if MHCCP_RUNTIME_FLAG
        if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
#else
        if (mbmi->cfl_idx == CFL_MULTI_PARAM_V) {
#endif  // MHCCP_RUNTIME_FLAG
          const uint8_t mh_size_group = size_group_lookup[bsize];
          filter_dir_rate =
              x->mode_costs.filter_dir_cost[mh_size_group][mbmi->mh_dir];
        }
        if (cfl_alpha_rate == INT_MAX) continue;
      }
      mode_cost += cfl_alpha_rate + cfl_idx_rate + filter_dir_rate;
      // Check if the reuse is enabled. If enabled, save the mode information
      // (i.e., rate and distortion) when the mode is evaluated for the first
      // time, else fetch the mode info saved.
      bool is_mode_rd_info_fetched = false;
      const int is_reuse_enabled = !xd->lossless[mbmi->segment_id] &&
                                   mode_rd_info_uv != NULL &&
                                   mode != UV_CFL_PRED && !is_reevaluation;
      if (!is_reuse_enabled) {
        if (!av1_txfm_uvrd(cpi, x, &tokenonly_rd_stats, INT64_MAX)) continue;
      } else {
        const int chroma_idx = get_chroma_idx_for_reuse_uvrd(mbmi);
        if (!mode_rd_info_uv->mode_evaluated[chroma_idx]) {
          if (!av1_txfm_uvrd(cpi, x, &tokenonly_rd_stats, INT64_MAX)) continue;
          store_uv_mode_rd_info(mode_rd_info_uv, &tokenonly_rd_stats,
                                chroma_idx);
        } else {
          fetch_uv_mode_rd_info(mode_rd_info_uv, &tokenonly_rd_stats,
                                chroma_idx);
          is_mode_rd_info_fetched = true;
        }
      }
      this_rate = tokenonly_rd_stats.rate +
                  intra_mode_info_cost_uv(cpi, x, mbmi, bsize, mode_cost);
      if (mode == UV_CFL_PRED) {
        assert(is_cfl_allowed(xd) && intra_mode_cfg->enable_cfl_intra);
      }
      this_rd = RDCOST(x->rdmult, this_rate, tokenonly_rd_stats.dist);

      if (this_rd < best_rd || is_reevaluation) {
        // When the RDCost retrieved using the fetched UV mode information, the
        // same mode needs to be reevaluated at the end. Hence, capture the best
        // mode_idx information here.
        best_uv_mode_idx = is_mode_rd_info_fetched ? mode_idx : -1;
        best_mbmi = *mbmi;
        // The buffer 'tmp_cctx_type_map' holds the best cross-chroma txfm type
        // map across the chroma modes.
        av1_copy_array(tmp_cctx_type_map, xd->cctx_type_map,
                       ctx->num_4x4_blk_chroma);
        best_rd = this_rd;
        *rate = this_rate;
        *rate_tokenonly = tokenonly_rd_stats.rate;
        *distortion = tokenonly_rd_stats.dist;
        *skippable = tokenonly_rd_stats.skip_txfm;
      }
      // Break the loop so that no further modes are evaluated after the best
      // mode reevaulation.
      if (is_reevaluation) break;
    }
  }

  // Search palette mode
  const int try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av1_allow_palette(cpi->common.features.allow_screen_content_tools,
                        mbmi->sb_type[PLANE_TYPE_UV]);
  if (try_palette) {
    uint8_t *best_palette_color_map = x->palette_buffer->best_palette_color_map;
    av1_rd_pick_palette_intra_sbuv(
        cpi, x,
        // This cost is not used actually in the caller function.
        mode_costs->intra_uv_mode_cost[0][0], best_palette_color_map,
        &best_mbmi, tmp_cctx_type_map, &best_rd, rate, rate_tokenonly,
        distortion, skippable, ctx->num_4x4_blk_chroma);
  }

  *mbmi = best_mbmi;
#if CONFIG_SDP_CFL_LATENCY_FIX
  assert(IMPLIES(xd->tree_type == CHROMA_PART && mbmi->uv_mode == UV_CFL_PRED,
                 is_cfl_allowed(xd) &&
                     xd->is_cfl_allowed_in_sdp == CFL_ALLOWED_FOR_CHROMA));
#endif  // CONFIG_SDP_CFL_LATENCY_FIX
  // Copy back the best cross-chroma txfm type (tmp_cctx_type_map)
  // to xd->cctx_type_map.
  av1_copy_array(xd->cctx_type_map, tmp_cctx_type_map, ctx->num_4x4_blk_chroma);
  // Make sure we actually chose a mode
  assert(best_rd < INT64_MAX);
  return best_rd;
}

// Searches palette mode for luma channel in inter frame.
int av1_search_palette_mode(IntraModeSearchState *intra_search_state,
                            const AV1_COMP *cpi, MACROBLOCK *x,
                            BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                            PICK_MODE_CONTEXT *ctx, RD_STATS *this_rd_cost,
                            int64_t best_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  MB_MODE_INFO *const mbmi = x->e_mbd.mi[0];
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  int rate2 = 0;
  int64_t distortion2 = 0, best_rd_palette = best_rd, this_rd,
          best_model_rd_palette = INT64_MAX;
  int skippable = 0;
  uint8_t *const best_palette_color_map =
      x->palette_buffer->best_palette_color_map;
  uint8_t *const color_map = xd->plane[0].color_index_map;
  MB_MODE_INFO best_mbmi_palette = *mbmi;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  const ModeCosts *mode_costs = &x->mode_costs;
  const int *const intra_mode_cost =
      mode_costs->mbmode_cost[size_group_lookup[bsize]];
  const int rows = block_size_high[bsize];
  const int cols = block_size_wide[bsize];

#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
  mbmi->mode = DC_PRED;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  if (xd->lossless[mbmi->segment_id]) {
    mbmi->use_dpcm_y = 0;
    mbmi->dpcm_mode_y = 0;
    mbmi->use_dpcm_uv = 0;
    mbmi->dpcm_mode_uv = 0;
  }

  mbmi->refinemv_flag = 0;
  mbmi->morph_pred = 0;

  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->six_param_warp_model_flag = 0;

  mbmi->warp_precision_idx = 0;
#if CONFIG_WARP_INTER_INTRA
  mbmi->warp_inter_intra = 0;
#endif  // CONFIG_WARP_INTER_INTRA

  RD_STATS rd_stats_y;
  av1_invalid_rd_stats(&rd_stats_y);
  av1_rd_pick_palette_intra_sby(
      cpi, x, bsize, intra_mode_cost[DC_PRED], &best_mbmi_palette,
      best_palette_color_map, &best_rd_palette, &best_model_rd_palette,
      &rd_stats_y.rate, NULL, &rd_stats_y.dist, &rd_stats_y.skip_txfm, NULL,
      ctx, best_blk_skip, best_tx_type_map);
  if (rd_stats_y.rate == INT_MAX || pmi->palette_size[0] == 0) {
    this_rd_cost->rdcost = INT64_MAX;
    return skippable;
  }

  memcpy(x->txfm_search_info.blk_skip[AOM_PLANE_Y], best_blk_skip,
         sizeof(best_blk_skip[0]) * bsize_to_num_blk(bsize));
  av1_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
  memcpy(color_map, best_palette_color_map,
         rows * cols * sizeof(best_palette_color_map[0]));

  skippable = rd_stats_y.skip_txfm;
  distortion2 = rd_stats_y.dist;
  rate2 = rd_stats_y.rate + ref_frame_cost;
  if (num_planes > 1) {
    {
      // We have not found any good uv mode yet, so we need to search for it.
      TX_SIZE uv_tx = av1_get_tx_size(AOM_PLANE_U, xd);
      av1_rd_pick_intra_sbuv_mode(cpi, x, &intra_search_state->rate_uv_intra,
                                  &intra_search_state->rate_uv_tokenonly,
                                  &intra_search_state->dist_uvs,
                                  &intra_search_state->skip_uvs, ctx, bsize,
                                  uv_tx, NULL /*ModeRDInfoUV*/
      );
      intra_search_state->mode_uv = mbmi->uv_mode;
      if (xd->lossless[mbmi->segment_id]) {
        intra_search_state->best_dpcm_uv_index = mbmi->use_dpcm_uv;
        intra_search_state->best_dpcm_uv_dir = mbmi->dpcm_mode_uv;
      }
      intra_search_state->pmi_uv = *pmi;
      intra_search_state->uv_angle_delta = mbmi->angle_delta[PLANE_TYPE_UV];
    }

    // We have found at least one good uv mode before, so copy and paste it
    // over.
    mbmi->uv_mode = intra_search_state->mode_uv;
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_uv = intra_search_state->best_dpcm_uv_index;
      mbmi->dpcm_mode_uv = intra_search_state->best_dpcm_uv_dir;
    }
    pmi->palette_size[1] = intra_search_state->pmi_uv.palette_size[1];
    if (pmi->palette_size[1] > 0) {
      memcpy(pmi->palette_colors + PALETTE_MAX_SIZE,
             intra_search_state->pmi_uv.palette_colors + PALETTE_MAX_SIZE,
             2 * PALETTE_MAX_SIZE * sizeof(pmi->palette_colors[0]));
    }
    mbmi->angle_delta[PLANE_TYPE_UV] = intra_search_state->uv_angle_delta;
    skippable = skippable && intra_search_state->skip_uvs;
    distortion2 += intra_search_state->dist_uvs;
    rate2 += intra_search_state->rate_uv_intra;
  }

  if (skippable) {
    rate2 -= rd_stats_y.rate;
    if (num_planes > 1) rate2 -= intra_search_state->rate_uv_tokenonly;
  }

  this_rd = RDCOST(x->rdmult, rate2, distortion2);
  this_rd_cost->rate = rate2;
  this_rd_cost->dist = distortion2;
  this_rd_cost->rdcost = this_rd;
  return skippable;
}

/*!\brief Get the intra prediction by searching through tx_type and tx_size.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * Currently this function is only used in the intra frame code path for
 * winner-mode processing.
 *
 * \return Returns whether the current mode is an improvement over best_rd.
 */
static AOM_INLINE int intra_block_yrd(const AV1_COMP *const cpi, MACROBLOCK *x,
                                      BLOCK_SIZE bsize, const int mode_costs,
                                      int64_t *best_rd, int *rate,
                                      int *rate_tokenonly, int64_t *distortion,
                                      int *skippable, MB_MODE_INFO *best_mbmi,
                                      PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  RD_STATS rd_stats;
  x->prune_tx_partition = 0;
  // In order to improve txfm search avoid rd based breakouts during winner
  // mode evaluation. Hence passing ref_best_rd as a maximum value
  av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats, bsize, INT64_MAX);
  if (rd_stats.rate == INT_MAX) return 0;
  int this_rate_tokenonly = rd_stats.rate;
  if (!xd->lossless[mbmi->segment_id] &&
      block_signals_txsize(mbmi->sb_type[PLANE_TYPE_Y])) {
    // av1_pick_uniform_tx_size_type_yrd above includes the cost of the
    // tx_size in the tokenonly rate, but for intra blocks, tx_size is always
    // coded (prediction granularity), so we account for it in the full rate,
    // not the tokenonly rate.
    this_rate_tokenonly -= tx_size_cost(x, bsize, mbmi->tx_size);
  }
  const int this_rate =
      rd_stats.rate + intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_costs);
  const int64_t this_rd = RDCOST(x->rdmult, this_rate, rd_stats.dist);
  if (this_rd < *best_rd) {
    *best_mbmi = *mbmi;
    *best_rd = this_rd;
    *rate = this_rate;
    *rate_tokenonly = this_rate_tokenonly;
    *distortion = rd_stats.dist;
    *skippable = rd_stats.skip_txfm;
    av1_copy_array(ctx->blk_skip[AOM_PLANE_Y],
                   x->txfm_search_info.blk_skip[AOM_PLANE_Y], ctx->num_4x4_blk);
    av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
    return 1;
  }
  return 0;
}

/*!\brief Search for the best data-driven intra mode when coding inter frame.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function loops through all data-driven intra modes to find the best one.
 *
 * Returns nothing, but updates the mbmi and rd_stats.
 */
static INLINE void handle_intra_dip_mode(const AV1_COMP *cpi, MACROBLOCK *x,
                                         BLOCK_SIZE bsize,
                                         const PICK_MODE_CONTEXT *ctx,
                                         RD_STATS *rd_stats_y, int mode_cost,
                                         int64_t best_rd,
                                         int64_t best_rd_so_far) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->mode == DC_PRED &&
         av1_intra_dip_allowed_bsize(&cpi->common, bsize));

  set_mv_precision(mbmi, mbmi->max_mv_precision);

  mbmi->refinemv_flag = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;

  RD_STATS rd_stats_y_iml;
  int intra_dip_selected_flag = 0;
  int best_ml_mode = 0;
  TX_SIZE best_tx_size = mbmi->tx_size;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  memcpy(best_blk_skip, x->txfm_search_info.blk_skip[AOM_PLANE_Y],
         sizeof(best_blk_skip[0]) * ctx->num_4x4_blk);
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_SIZE best_tx_partition = mbmi->tx_partition_type[0];
  av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
  mbmi->use_intra_dip = 1;

  int num_modes = av1_intra_dip_modes(bsize);
  int has_transpose = av1_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

  for (int transpose = 0; transpose < num_transpose; transpose++) {
    for (int ml_mode = 0; ml_mode < num_modes; ml_mode++) {
      int mode = (transpose << 4) + ml_mode;
      mbmi->intra_dip_mode = mode;

      av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y_iml, bsize,
                                        best_rd);

      if (rd_stats_y_iml.rate == INT_MAX) continue;
      const int this_rate_tmp =
          rd_stats_y_iml.rate +
          intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_cost);
      const int64_t this_rd_tmp =
          RDCOST(x->rdmult, this_rate_tmp, rd_stats_y_iml.dist);

      if (this_rd_tmp != INT64_MAX && this_rd_tmp / 2 > best_rd) {
        break;
      }
      if (this_rd_tmp < best_rd_so_far) {
        best_tx_size = mbmi->tx_size;
        best_tx_partition = mbmi->tx_partition_type[0];
        av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
        memcpy(best_blk_skip, x->txfm_search_info.blk_skip[AOM_PLANE_Y],
               sizeof(best_blk_skip[0]) * ctx->num_4x4_blk);
        best_ml_mode = mode;
        *rd_stats_y = rd_stats_y_iml;
        intra_dip_selected_flag = 1;
        best_rd_so_far = this_rd_tmp;
      }
    }
  }

  mbmi->tx_size = best_tx_size;
  mbmi->tx_partition_type[0] = best_tx_partition;
  av1_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
  memcpy(x->txfm_search_info.blk_skip[AOM_PLANE_Y], best_blk_skip,
         sizeof(*x->txfm_search_info.blk_skip[AOM_PLANE_Y]) * ctx->num_4x4_blk);

  if (intra_dip_selected_flag) {
    mbmi->use_intra_dip = 1;
    mbmi->intra_dip_mode = best_ml_mode;
    mbmi->mode = DC_PRED;
    mbmi->angle_delta[PLANE_TYPE_Y] = 0;
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;

    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_dpcm_uv = 0;
      mbmi->dpcm_mode_uv = 0;
    }
  } else {
    mbmi->use_intra_dip = 0;
  }
}

int64_t av1_handle_intra_mode(IntraModeSearchState *intra_search_state,
                              const AV1_COMP *cpi, MACROBLOCK *x,
                              BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                              const PICK_MODE_CONTEXT *ctx, RD_STATS *rd_stats,
                              RD_STATS *rd_stats_y, RD_STATS *rd_stats_uv,
                              ModeRDInfoUV *mode_rd_info_uv, int64_t best_rd,
                              int64_t *best_intra_rd, int64_t *best_model_rd,
                              int64_t top_intra_model_rd[]) {
  const AV1_COMMON *cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->ref_frame[0] == INTRA_FRAME);
  const PREDICTION_MODE mode = mbmi->mode;
  const ModeCosts *mode_costs = &x->mode_costs;

  int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
  int mrl_idx_cost =
      (av1_is_directional_mode(mbmi->mode) &&
       cpi->common.seq_params.enable_mrls)
          ? x->mode_costs.mrl_index_cost[mrl_ctx][mbmi->mrl_index]
          : 0;
  if (av1_is_directional_mode(mbmi->mode) &&
      cpi->common.seq_params.enable_mrls && mbmi->mrl_index) {
    int multi_line_mrl_ctx =
        get_multi_line_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
    mrl_idx_cost +=
        x->mode_costs
            .multi_line_mrl_cost[multi_line_mrl_ctx][mbmi->multi_line_mrl];
  }
  int mode_cost = 0;
  if (xd->lossless[mbmi->segment_id]) {
    int dpcm_cost = x->mode_costs.dpcm_cost[mbmi->use_dpcm_y];
    mode_cost += dpcm_cost;
    if (mbmi->use_dpcm_y == 0) {
      const int context = get_y_mode_idx_ctx(xd);
      int mode_set_index = mbmi->y_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
      mode_set_index +=
          ((mbmi->y_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
      mode_cost += x->mode_costs.y_primary_flag_cost[mode_set_index];
      if (mbmi->y_mode_idx < FIRST_MODE_COUNT) {
        mode_cost +=
            x->mode_costs.y_first_mode_costs[context][mbmi->y_mode_idx];
      } else {
#if CONFIG_CTX_Y_SECOND_MODE
        mode_cost += av1_cost_literal(4);
#else
        mode_cost +=
            x->mode_costs
                .y_second_mode_costs[context]
                                    [mbmi->y_mode_idx - FIRST_MODE_COUNT -
                                     SECOND_MODE_COUNT * (mode_set_index - 1)];
#endif  // CONFIG_CTX_Y_SECOND_MODE
      }
      mode_cost += ref_frame_cost;
      mode_cost += mrl_idx_cost;
    } else {
      mode_cost += x->mode_costs.dpcm_vert_horz_cost[mbmi->dpcm_mode_y];
    }
  } else {
    const int context = get_y_mode_idx_ctx(xd);
    int mode_set_index = mbmi->y_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
    mode_set_index +=
        ((mbmi->y_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
    mode_cost += x->mode_costs.y_primary_flag_cost[mode_set_index];
    if (mbmi->y_mode_idx < FIRST_MODE_COUNT) {
      mode_cost += x->mode_costs.y_first_mode_costs[context][mbmi->y_mode_idx];
    } else {
#if CONFIG_CTX_Y_SECOND_MODE
      mode_cost += av1_cost_literal(4);
#else
      mode_cost +=
          x->mode_costs
              .y_second_mode_costs[context]
                                  [mbmi->y_mode_idx - FIRST_MODE_COUNT -
                                   SECOND_MODE_COUNT * (mode_set_index - 1)];
#endif  // CONFIG_CTX_Y_SECOND_MODE
    }
    mode_cost += ref_frame_cost;
    mode_cost += mrl_idx_cost;
  }

  const int intra_cost_penalty = av1_get_intra_cost_penalty(
      cm->quant_params.base_qindex, cm->quant_params.y_dc_delta_q,
      cm->seq_params.base_y_dc_delta_q, cm->seq_params.bit_depth);

  int known_rate = mode_cost;
  if (mode != DC_PRED && mode != PAETH_PRED) known_rate += intra_cost_penalty;
  const int64_t known_rd = RDCOST(x->rdmult, known_rate, 0);
  if (known_rd > best_rd) {
    intra_search_state->skip_intra_modes = 1;
    return INT64_MAX;
  }

  const int is_directional_mode = av1_is_directional_mode(mode);
  if (is_directional_mode && cpi->oxcf.intra_mode_cfg.enable_angle_delta) {
    if (sf->intra_sf.intra_pruning_with_hog &&
        !intra_search_state->dir_mode_skip_mask_ready) {
      prune_intra_mode_with_hog(x, bsize,
                                cpi->sf.intra_sf.intra_pruning_with_hog_thresh,
                                intra_search_state->directional_mode_skip_mask);
      intra_search_state->dir_mode_skip_mask_ready = 1;
    }
    if (intra_search_state->directional_mode_skip_mask[mode] &&
        mbmi->y_mode_idx >= FIRST_MODE_COUNT)
      return INT64_MAX;
  }

  int64_t this_model_rd = intra_model_yrd(cpi, x, bsize, mode_cost);
  if (prune_intra_y_mode(this_model_rd, best_model_rd, top_intra_model_rd) &&
      (!xd->lossless[mbmi->segment_id] || mbmi->use_dpcm_y == 0))
    return INT64_MAX;
  av1_init_rd_stats(rd_stats_y);
  x->prune_tx_partition = 0;
  av1_pick_uniform_tx_size_type_yrd(cpi, x, rd_stats_y, bsize, best_rd);

  if (mode == DC_PRED && xd->tree_type != CHROMA_PART &&
      av1_intra_dip_allowed_bsize(cm, bsize)) {
    int try_intra_dip = 1;
    int64_t best_rd_so_far = INT64_MAX;
    if (rd_stats_y->rate != INT_MAX) {
      int iml_ctx =
          get_intra_dip_ctx(xd->neighbors[0], xd->neighbors[1], bsize);
      const int tmp_rate =
          rd_stats_y->rate + mode_costs->intra_dip_cost[iml_ctx][0] + mode_cost;
      best_rd_so_far = RDCOST(x->rdmult, tmp_rate, rd_stats_y->dist);
      // try_intra_dip = (best_rd_so_far / 2) <= best_rd;
    }
    if (try_intra_dip) {
      handle_intra_dip_mode(cpi, x, bsize, ctx, rd_stats_y, mode_cost, best_rd,
                            best_rd_so_far);
    }
  }

  if (rd_stats_y->rate == INT_MAX) return INT64_MAX;

  const int mode_cost_y =
      intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_cost);
  av1_init_rd_stats(rd_stats);
  av1_init_rd_stats(rd_stats_uv);
  const int num_planes = av1_num_planes(cm);
  if (num_planes > 1) {
    // TODO(chiyotsai@google.com): Consolidate the chroma search code here
    // with the one in av1_search_palette_mode.
    PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
    const int try_palette =
        cpi->oxcf.tool_cfg.enable_palette &&
        av1_allow_palette(cm->features.allow_screen_content_tools,
                          mbmi->sb_type[PLANE_TYPE_Y]);
    // If no good uv-predictor had been found, search for it.
    const int rate_y = rd_stats_y->rate;
    const int64_t rdy =
        RDCOST(x->rdmult, rate_y + mode_cost_y, rd_stats_y->dist);
    if (best_rd < (INT64_MAX / 2) && rdy > (best_rd + (best_rd >> 2))) {
      intra_search_state->skip_intra_modes = 1;
      return INT64_MAX;
    }
    const TX_SIZE uv_tx = av1_get_tx_size(AOM_PLANE_U, xd);
    av1_rd_pick_intra_sbuv_mode(
        cpi, x, &intra_search_state->rate_uv_intra,
        &intra_search_state->rate_uv_tokenonly, &intra_search_state->dist_uvs,
        &intra_search_state->skip_uvs, ctx, bsize, uv_tx,
        sf->intra_sf.reuse_uv_mode_rd_info ? mode_rd_info_uv : NULL);
    intra_search_state->mode_uv = mbmi->uv_mode;
    if (xd->lossless[mbmi->segment_id]) {
      intra_search_state->best_dpcm_uv_index = mbmi->use_dpcm_uv;
      intra_search_state->best_dpcm_uv_dir = mbmi->dpcm_mode_uv;
    }
    if (try_palette) intra_search_state->pmi_uv = *pmi;
    intra_search_state->uv_angle_delta = mbmi->angle_delta[PLANE_TYPE_UV];

    intra_search_state->uv_mode_idx = mbmi->uv_mode_idx;
    const int uv_rate = intra_search_state->rate_uv_tokenonly;
    const int64_t uv_dist = intra_search_state->dist_uvs;
    const int64_t uv_rd = RDCOST(x->rdmult, uv_rate, uv_dist);
    if (uv_rd > best_rd) {
      // If there is no good intra uv-mode available, we can skip all intra
      // modes.
      intra_search_state->skip_intra_modes = 1;
      return INT64_MAX;
    }

    // If we are here, then the encoder has found at least one good intra uv
    // predictor, so we can directly copy its statistics over.
    // TODO(any): the stats here is probably not right if the current best
    // mode is cfl.
    rd_stats_uv->rate = intra_search_state->rate_uv_tokenonly;
    rd_stats_uv->dist = intra_search_state->dist_uvs;
    rd_stats_uv->skip_txfm = intra_search_state->skip_uvs;
    rd_stats->skip_txfm = rd_stats_y->skip_txfm && rd_stats_uv->skip_txfm;
    mbmi->uv_mode = intra_search_state->mode_uv;
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_uv = intra_search_state->best_dpcm_uv_index;
      mbmi->dpcm_mode_uv = intra_search_state->best_dpcm_uv_dir;
    }
    mbmi->uv_mode_idx = intra_search_state->uv_mode_idx;
    if (try_palette) {
      pmi->palette_size[1] = intra_search_state->pmi_uv.palette_size[1];
      memcpy(pmi->palette_colors + PALETTE_MAX_SIZE,
             intra_search_state->pmi_uv.palette_colors + PALETTE_MAX_SIZE,
             2 * PALETTE_MAX_SIZE * sizeof(pmi->palette_colors[0]));
    }
    mbmi->angle_delta[PLANE_TYPE_UV] = intra_search_state->uv_angle_delta;
  }

  rd_stats->rate = rd_stats_y->rate + mode_cost_y;
  if (!xd->lossless[mbmi->segment_id] && block_signals_txsize(bsize)) {
    // av1_pick_uniform_tx_size_type_yrd above includes the cost of the
    // tx_size in the tokenonly rate, but for intra blocks, tx_size is always
    // coded (prediction granularity), so we account for it in the full rate,
    // not the tokenonly rate.
    rd_stats_y->rate -= tx_size_cost(x, bsize, mbmi->tx_size);
  }
  if (num_planes > 1 && xd->is_chroma_ref) {
    const int uv_mode_cost = get_uv_mode_cost(
        mbmi, x->mode_costs, xd, is_cfl_allowed(xd), mbmi->uv_mode_idx);
    rd_stats->rate +=
        rd_stats_uv->rate +
        intra_mode_info_cost_uv(cpi, x, mbmi, bsize, uv_mode_cost);
  }

  // Intra block is always coded as non-skip
  rd_stats->skip_txfm = 0;
  rd_stats->dist = rd_stats_y->dist + rd_stats_uv->dist;
  // Calculate the final RD estimate for this mode.
  const int64_t this_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
  // Keep record of best intra rd
  if (this_rd < *best_intra_rd) {
    *best_intra_rd = this_rd;
    intra_search_state->best_intra_mode = mode;
    intra_search_state->best_fsc = mbmi->fsc_mode[xd->tree_type == CHROMA_PART];
    intra_search_state->best_mrl_index = mbmi->mrl_index;
    intra_search_state->best_multi_line_mrl = mbmi->multi_line_mrl;
    if (xd->lossless[mbmi->segment_id]) {
      intra_search_state->best_dpcm_index = mbmi->use_dpcm_y;
      intra_search_state->best_dpcm_dir = mbmi->dpcm_mode_y;
    }
  }

  if (sf->intra_sf.skip_intra_in_interframe) {
    if (best_rd < (INT64_MAX / 2) && this_rd > (best_rd + (best_rd >> 1)))
      intra_search_state->skip_intra_modes = 1;
  }

  for (int i = 0; i < REFERENCE_MODES; ++i) {
    intra_search_state->best_pred_rd[i] =
        AOMMIN(intra_search_state->best_pred_rd[i], this_rd);
  }

  return this_rd;
}

void search_fsc_mode(const AV1_COMP *const cpi, MACROBLOCK *x, int *rate,
                     int *rate_tokenonly, int64_t *distortion, int *skippable,
                     BLOCK_SIZE bsize, int mode_costs, uint8_t *dir_skip_mask,
                     int64_t *best_rd, int64_t *best_model_rd,
                     PICK_MODE_CONTEXT *ctx, MB_MODE_INFO *best_mbmi) {
  (void)ctx;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int context = get_y_mode_idx_ctx(xd);
  uint8_t best_y_mode_idx = best_mbmi->y_mode_idx;
  uint8_t best_joint_ymode = best_mbmi->joint_y_mode_delta_angle;
  uint8_t best_fsc_mode = 0;
  PREDICTION_MODE best_intra_mode = best_mbmi->mode;
  TX_SIZE best_tx_size = best_mbmi->tx_size;
  TX_PARTITION_TYPE best_tx_partition_type[INTER_TX_SIZE_BUF_LEN];
  av1_copy(best_tx_partition_type, best_mbmi->tx_partition_type);
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  int8_t best_angle_delta = best_mbmi->angle_delta[PLANE_TYPE_Y];
  uint8_t best_mrl = best_mbmi->mrl_index;
  uint8_t enable_mrls_flag = cpi->common.seq_params.enable_mrls;
  uint8_t mrl_loop = (enable_mrls_flag && best_mrl) ? 2 : 1;
  uint8_t best_multi_line_mrl = best_mbmi->multi_line_mrl;
  uint8_t multi_line_mrl_loop = (enable_mrls_flag && best_mrl) ? 2 : 1;

  int dpcm_fsc_loop = 1;
  uint8_t best_dpcm_fsc = mbmi->use_dpcm_y;
  uint8_t best_dpcm_fsc_dir = mbmi->dpcm_mode_y;
  // uint8_t best_dpcm_fsc_angle_delta = mbmi->dpcm_angle_delta;
  mbmi->use_dpcm_y = 0;
  if (xd->lossless[mbmi->segment_id]) {
    dpcm_fsc_loop = 2;
  }
  int64_t top_intra_model_rd[TOP_INTRA_MODEL_COUNT];
  for (int i = 0; i < TOP_INTRA_MODEL_COUNT; i++) {
    top_intra_model_rd[i] = INT64_MAX;
  }
  x->prune_tx_partition = 1;
  for (int i = 0; i < TOP_TX_PART_COUNT; i++) {
    x->top_tx_part_rd[i] = INT64_MAX;
  }
  for (int dpcm_fsc_index = 0; dpcm_fsc_index < dpcm_fsc_loop;
       dpcm_fsc_index++) {
    mbmi->use_dpcm_y = dpcm_fsc_index;
    for (int mrl_idx = 0; mrl_idx < mrl_loop; ++mrl_idx) {
      mbmi->mrl_index = mrl_idx ? best_mbmi->mrl_index : mrl_idx;
      for (int multi_line_mrl = 0;
           multi_line_mrl < (mrl_idx ? multi_line_mrl_loop : 1);
           ++multi_line_mrl) {
        mbmi->multi_line_mrl =
            multi_line_mrl ? best_mbmi->multi_line_mrl : multi_line_mrl;
        for (int mode_idx = INTRA_MODE_START; mode_idx < LUMA_MODE_COUNT;
             ++mode_idx) {
          mbmi->y_mode_idx = mode_idx;
          mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[mode_idx];
          set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
          if (mbmi->y_mode_idx >= FIRST_MODE_COUNT &&
              !(mbmi->angle_delta[PLANE_TYPE_Y] ==
                best_mbmi->angle_delta[PLANE_TYPE_Y])) {
            continue;
          }
          mode_costs = 0;
          if (xd->lossless[mbmi->segment_id]) {
            if (mbmi->use_dpcm_y > 0 &&
                (mrl_idx > 0 ||
                 (mbmi->mode != V_PRED && mbmi->mode != H_PRED) ||
                 ((mbmi->mode == V_PRED || mbmi->mode == H_PRED) &&
                  mbmi->angle_delta[0] != 0))) {
              continue;
            }
            int dpcm_cost = x->mode_costs.dpcm_cost[mbmi->use_dpcm_y];
            mode_costs += dpcm_cost;
            if (mbmi->use_dpcm_y > 0) {
              mbmi->dpcm_mode_y = mbmi->mode - 1;
            }
          }
          if (mbmi->use_dpcm_y == 0) {
            int mode_set_index = mbmi->y_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
            mode_set_index +=
                ((mbmi->y_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
            mode_costs += x->mode_costs.y_primary_flag_cost[mode_set_index];
            if (mode_idx < FIRST_MODE_COUNT) {
              mode_costs += x->mode_costs.y_first_mode_costs[context][mode_idx];
            } else {
#if CONFIG_CTX_Y_SECOND_MODE
              mode_costs += av1_cost_literal(4);
#else
              mode_costs +=
                  x->mode_costs.y_second_mode_costs
                      [context][mbmi->y_mode_idx - FIRST_MODE_COUNT -
                                SECOND_MODE_COUNT * (mode_set_index - 1)];

#endif  // CONFIG_CTX_Y_SECOND_MODE
            }
          } else {
            int dpcm_dir_cost =
                x->mode_costs.dpcm_vert_horz_cost[mbmi->dpcm_mode_y];
            mode_costs += dpcm_dir_cost;
          }
          if (xd->lossless[mbmi->segment_id]) {
            mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 1;
          } else {
            mbmi->fsc_mode[PLANE_TYPE_Y] = 1;
          }

          mbmi->use_intra_dip = 0;
          mbmi->palette_mode_info.palette_size[0] = 0;
          int64_t this_rd;
          RD_STATS tokenonly_rd_stats;
          if ((!cpi->oxcf.intra_mode_cfg.enable_smooth_intra ||
               cpi->sf.intra_sf.disable_smooth_intra) &&
              (mbmi->mode == SMOOTH_PRED || mbmi->mode == SMOOTH_H_PRED ||
               mbmi->mode == SMOOTH_V_PRED)) {
            continue;
          }
          if (!cpi->oxcf.intra_mode_cfg.enable_paeth_intra &&
              mbmi->mode == PAETH_PRED) {
            continue;
          }
          int is_directional_mode = av1_is_directional_mode(mbmi->mode);
          if (is_directional_mode && dir_skip_mask[mbmi->mode] &&
              mode_idx >= FIRST_MODE_COUNT)
            continue;

          if (!is_directional_mode && mrl_idx) continue;
          if (((best_mbmi->mrl_index == 0 &&
                av1_is_directional_mode(best_mbmi->mode) == 0) ||
               (best_mbmi->mrl_index && mbmi->multi_line_mrl == 0)) &&
              mbmi->mrl_index > 1 && mbmi->multi_line_mrl) {
            continue;
          }
          int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
          int mrl_idx_cost =
              (is_directional_mode && enable_mrls_flag)
                  ? x->mode_costs.mrl_index_cost[mrl_ctx][mbmi->mrl_index]
                  : 0;

          if (is_directional_mode && enable_mrls_flag && mbmi->mrl_index) {
            int multi_line_mrl_ctx = get_multi_line_mrl_index_ctx(
                xd->neighbors[0], xd->neighbors[1]);
            mrl_idx_cost +=
                x->mode_costs.multi_line_mrl_cost[multi_line_mrl_ctx]
                                                 [mbmi->multi_line_mrl];
          }
          mode_costs += mrl_idx_cost;
          int64_t this_model_rd;
          this_model_rd = intra_model_yrd(cpi, x, bsize, mode_costs);

          if (prune_intra_y_mode(this_model_rd, best_model_rd,
                                 top_intra_model_rd) &&
              (!xd->lossless[mbmi->segment_id] || mbmi->use_dpcm_y == 0)) {
            continue;
          }
          av1_pick_uniform_tx_size_type_yrd(cpi, x, &tokenonly_rd_stats, bsize,
                                            *best_rd);
          if (tokenonly_rd_stats.rate == INT_MAX) continue;
          const int this_rate =
              tokenonly_rd_stats.rate +
              intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_costs);
          this_rd = RDCOST(x->rdmult, this_rate, tokenonly_rd_stats.dist);
          // Collect mode stats for multiwinner mode processing
          const int txfm_search_done = 1;
          const MV_REFERENCE_FRAME refs[2] = { -1, -1 };
          store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, refs,
                                  0, NULL, bsize, this_rd,
                                  cpi->sf.winner_mode_sf.multi_winner_mode_type,
                                  txfm_search_done);

          if (this_rd < *best_rd) {
            *best_rd = this_rd;
            best_tx_size = mbmi->tx_size;
            av1_copy(best_tx_partition_type, mbmi->tx_partition_type);
            best_intra_mode = mbmi->mode;
            best_y_mode_idx = mbmi->y_mode_idx;
            best_joint_ymode = mbmi->joint_y_mode_delta_angle;
            best_mrl = mbmi->mrl_index;
            best_multi_line_mrl = mbmi->multi_line_mrl;
            if (xd->lossless[mbmi->segment_id]) {
              best_dpcm_fsc = mbmi->use_dpcm_y;
              best_dpcm_fsc_dir = mbmi->dpcm_mode_y;
            }
            best_angle_delta = mbmi->angle_delta[PLANE_TYPE_Y];
            av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
            memcpy(ctx->blk_skip[AOM_PLANE_Y],
                   x->txfm_search_info.blk_skip[AOM_PLANE_Y],
                   sizeof(*x->txfm_search_info.blk_skip[AOM_PLANE_Y]) *
                       ctx->num_4x4_blk);
            *rate = this_rate;
            *rate_tokenonly = tokenonly_rd_stats.rate;
            *distortion = tokenonly_rd_stats.dist;
            *skippable = tokenonly_rd_stats.skip_txfm;
            best_fsc_mode = 1;
          }
        }
      }
    }
  }
  if (best_fsc_mode) {
    mbmi->fsc_mode[PLANE_TYPE_Y] = 1;
    mbmi->mode = best_intra_mode;
    mbmi->y_mode_idx = best_y_mode_idx;
    mbmi->joint_y_mode_delta_angle = best_joint_ymode;
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_y = best_dpcm_fsc;
      mbmi->dpcm_mode_y = best_dpcm_fsc_dir;
    }
    mbmi->tx_size = best_tx_size;
    av1_copy(mbmi->tx_partition_type, best_tx_partition_type);
    mbmi->mrl_index = best_mrl;
    mbmi->multi_line_mrl = best_multi_line_mrl;
    mbmi->angle_delta[PLANE_TYPE_Y] = best_angle_delta;
    av1_copy_array(ctx->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
    *best_mbmi = *mbmi;
  } else {
    *mbmi = *best_mbmi;
  }
}

// Finds the best non-intrabc mode on an intra frame.
int64_t av1_rd_pick_intra_sby_mode(const AV1_COMP *const cpi, ThreadData *td,
                                   MACROBLOCK *x, int *rate,
                                   int *rate_tokenonly, int64_t *distortion,
                                   int *skippable, BLOCK_SIZE bsize,
                                   int64_t best_rd, PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(!is_inter_block(mbmi, xd->tree_type));
  int64_t best_model_rd = INT64_MAX;
  int is_directional_mode;
  mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;
  uint8_t directional_mode_skip_mask[INTRA_MODES] = { 0 };

#if CONFIG_DIP_EXT_PRUNING
  // Collect RD features for DIP ML pruning.
  struct extra_dip_info extra_dip;
  extra_dip.beat_best_rd = 0;
  extra_dip.dc_mode_rd = INT64_MAX;
  extra_dip.orig_best_rd = best_rd;
  extra_dip.best_mode = 0;
#endif  // CONFIG_DIP_EXT_PRUNING
  // Flag to check rd of any intra mode is better than best_rd passed to this
  // function
  int beat_best_rd = 0;
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av1_allow_palette(cpi->common.features.allow_screen_content_tools,
                        mbmi->sb_type[PLANE_TYPE_Y]);
  uint8_t *best_palette_color_map =
      try_palette ? x->palette_buffer->best_palette_color_map : NULL;
  const int context = get_y_mode_idx_ctx(xd);
  int mode_costs = 0;

  mbmi->angle_delta[PLANE_TYPE_Y] = 0;
  if (cpi->sf.intra_sf.intra_pruning_with_hog) {
    prune_intra_mode_with_hog(x, bsize,
                              cpi->sf.intra_sf.intra_pruning_with_hog_thresh,
                              directional_mode_skip_mask);
  }
  mbmi->use_intra_dip = 0;
  pmi->palette_size[0] = 0;

  mbmi->motion_mode = SIMPLE_TRANSLATION;

  // Set params for mode evaluation
  set_mode_eval_params(cpi, x, MODE_EVAL);

  get_y_intra_mode_set(mbmi, xd);
  mbmi->is_wide_angle[0][mbmi->txb_idx] = 0;
  mbmi->mapped_intra_mode[0][mbmi->txb_idx] = DC_PRED;

  MB_MODE_INFO best_mbmi = *mbmi;
  av1_zero(x->winner_mode_stats);
  x->winner_mode_count = 0;
  mbmi->use_dpcm_y = 0;
  mbmi->dpcm_mode_y = 0;
  // mbmi->dpcm_angle_delta = 0;
  //  Searches the intra-modes except for intrabc, palette, and filter_intra.
  int64_t top_intra_model_rd[TOP_INTRA_MODEL_COUNT];
  for (int i = 0; i < TOP_INTRA_MODEL_COUNT; i++) {
    top_intra_model_rd[i] = INT64_MAX;
  }
  x->prune_tx_partition = 1;
  for (int i = 0; i < TOP_TX_PART_COUNT; i++) {
    x->top_tx_part_rd[i] = INT64_MAX;
  }
  uint8_t enable_mrls_flag = cpi->common.seq_params.enable_mrls;
  int dpcm_loop_num = 1;
  if (xd->lossless[mbmi->segment_id]) {
    dpcm_loop_num = 2;
  }
  for (int dpcm_index = 0; dpcm_index < dpcm_loop_num; ++dpcm_index) {
    for (int mrl_idx = 0; mrl_idx < (enable_mrls_flag ? MRL_LINE_NUMBER : 1);
         ++mrl_idx) {
      mbmi->mrl_index = mrl_idx;
      for (int multi_line_mrl = 0; multi_line_mrl < (mrl_idx ? 2 : 1);
           multi_line_mrl++) {
        mbmi->multi_line_mrl = multi_line_mrl;
        for (int mode_idx = INTRA_MODE_START; mode_idx < LUMA_MODE_COUNT;
             ++mode_idx) {
          mbmi->y_mode_idx = mode_idx;
          mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[mode_idx];
          // the below function changes the mbmi->mode based on the mode_idx
          set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
          mode_costs = 0;
          if (dpcm_index > 0 &&
              (mrl_idx > 0 || (mbmi->mode != V_PRED && mbmi->mode != H_PRED) ||
               ((mbmi->mode == V_PRED || mbmi->mode == H_PRED) &&
                mbmi->angle_delta[0] != 0))) {
            continue;
          }
          int dpcm_cost = 0;
          if (xd->lossless[mbmi->segment_id]) {
            dpcm_cost = x->mode_costs.dpcm_cost[dpcm_index];
            mode_costs += dpcm_cost;
          }
          mbmi->use_dpcm_y = dpcm_index;
          if (mbmi->use_dpcm_y > 0) {
            mbmi->dpcm_mode_y = mbmi->mode - 1;
          } else {
            mbmi->dpcm_mode_y = 0;
          }
          if (mbmi->use_dpcm_y == 0) {
            int mode_set_index = mbmi->y_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
            mode_set_index +=
                ((mbmi->y_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
            mode_costs += x->mode_costs.y_primary_flag_cost[mode_set_index];
            if (mode_idx < FIRST_MODE_COUNT) {
              mode_costs += x->mode_costs.y_first_mode_costs[context][mode_idx];
            } else {
#if CONFIG_CTX_Y_SECOND_MODE
              mode_costs += av1_cost_literal(4);
#else
              mode_costs +=
                  x->mode_costs.y_second_mode_costs
                      [context][mbmi->y_mode_idx - FIRST_MODE_COUNT -
                                SECOND_MODE_COUNT * (mode_set_index - 1)];
#endif  // CONFIG_CTX_Y_SECOND_MODE
            }
          } else {
            int dpcm_dir_cost =
                x->mode_costs.dpcm_vert_horz_cost[mbmi->dpcm_mode_y];
            mode_costs += dpcm_dir_cost;
          }
          RD_STATS this_rd_stats;
          int this_rate, this_rate_tokenonly, s;
          int64_t this_distortion, this_rd;
          if ((!cpi->oxcf.intra_mode_cfg.enable_smooth_intra ||
               cpi->sf.intra_sf.disable_smooth_intra) &&
              (mbmi->mode == SMOOTH_PRED || mbmi->mode == SMOOTH_H_PRED ||
               mbmi->mode == SMOOTH_V_PRED))
            continue;
          if (!cpi->oxcf.intra_mode_cfg.enable_paeth_intra &&
              mbmi->mode == PAETH_PRED)
            continue;
          is_directional_mode = av1_is_directional_mode(mbmi->mode);
          if (is_directional_mode && directional_mode_skip_mask[mbmi->mode] &&
              mode_idx >= FIRST_MODE_COUNT)
            continue;

          if (!is_directional_mode && mrl_idx) continue;

          if (((best_mbmi.mrl_index == 0 &&
                av1_is_directional_mode(best_mbmi.mode) == 0) ||
               (best_mbmi.mrl_index && mbmi->multi_line_mrl == 0)) &&
              mbmi->mrl_index > 1 && mbmi->multi_line_mrl) {
            continue;
          }
          int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
          int mrl_idx_cost =
              (is_directional_mode && enable_mrls_flag)
                  ? x->mode_costs.mrl_index_cost[mrl_ctx][mbmi->mrl_index]
                  : 0;

          if (is_directional_mode && enable_mrls_flag && mbmi->mrl_index) {
            int multi_line_mrl_ctx = get_multi_line_mrl_index_ctx(
                xd->neighbors[0], xd->neighbors[1]);
            mrl_idx_cost +=
                x->mode_costs.multi_line_mrl_cost[multi_line_mrl_ctx]
                                                 [mbmi->multi_line_mrl];
          }
          if (dpcm_index == 0) mode_costs += mrl_idx_cost;
          int64_t this_model_rd;
          this_model_rd = intra_model_yrd(cpi, x, bsize, mode_costs);
          if (prune_intra_y_mode(this_model_rd, &best_model_rd,
                                 top_intra_model_rd) &&
              (!xd->lossless[mbmi->segment_id] || mbmi->use_dpcm_y == 0))
            continue;

          av1_pick_uniform_tx_size_type_yrd(cpi, x, &this_rd_stats, bsize,
                                            best_rd);
          this_rate_tokenonly = this_rd_stats.rate;
          this_distortion = this_rd_stats.dist;
          s = this_rd_stats.skip_txfm;

          if (this_rate_tokenonly == INT_MAX) continue;
          if (!xd->lossless[mbmi->segment_id] &&
              block_signals_txsize(mbmi->sb_type[PLANE_TYPE_Y])) {
            // av1_pick_uniform_tx_size_type_yrd above includes the cost of the
            // tx_size in the tokenonly rate, but for intra blocks, tx_size is
            // always coded (prediction granularity), so we account for it in
            // the full rate, not the tokenonly rate.
            this_rate_tokenonly -= tx_size_cost(x, bsize, mbmi->tx_size);
          }
          this_rate = this_rd_stats.rate +
                      intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_costs);
          this_rd = RDCOST(x->rdmult, this_rate, this_distortion);
#if CONFIG_DIP_EXT_PRUNING
          if (mbmi->mode == DC_PRED) {
            extra_dip.dc_mode_rd = this_rd;
          }
#endif  // CONFIG_DIP_EXT_PRUNING
        // Collect mode stats for multiwinner mode processing
          const int txfm_search_done = 1;
          const MV_REFERENCE_FRAME refs[2] = { -1, -1 };
          store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, refs,
                                  0, NULL, bsize, this_rd,
                                  cpi->sf.winner_mode_sf.multi_winner_mode_type,
                                  txfm_search_done);
          if (this_rd < best_rd) {
            best_mbmi = *mbmi;
            best_rd = this_rd;
            // Setting beat_best_rd flag because current mode rd is better than
            // best_rd passed to this function
            beat_best_rd = 1;
#if CONFIG_DIP_EXT_PRUNING
            extra_dip.beat_best_rd = 1;
            extra_dip.best_mode = mbmi->mode;
#endif  // CONFIG_DIP_EXT_PRUNING
            *rate = this_rate;
            *rate_tokenonly = this_rate_tokenonly;
            *distortion = this_distortion;
            *skippable = s;
            memcpy(ctx->blk_skip[AOM_PLANE_Y],
                   x->txfm_search_info.blk_skip[AOM_PLANE_Y],
                   sizeof(*x->txfm_search_info.blk_skip[AOM_PLANE_Y]) *
                       ctx->num_4x4_blk);
            av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
          }
        }
      }
    }
  }

  // Searches forward skip coding
  if (beat_best_rd && allow_fsc_intra(&cpi->common, bsize, mbmi)) {
    search_fsc_mode(cpi, x, rate, rate_tokenonly, distortion, skippable, bsize,
                    mode_costs, directional_mode_skip_mask, &best_rd,
                    &best_model_rd, ctx, &best_mbmi);
  }

  // Searches palette
  mode_costs = x->mode_costs.y_primary_flag_cost[DC_PRED];
  mode_costs += x->mode_costs.y_first_mode_costs[context][DC_PRED];
  if (try_palette) {
    av1_rd_pick_palette_intra_sby(
        cpi, x, bsize, mode_costs, &best_mbmi, best_palette_color_map, &best_rd,
        &best_model_rd, rate, rate_tokenonly, distortion, skippable,
        &beat_best_rd, ctx, ctx->blk_skip[AOM_PLANE_Y], ctx->tx_type_map);
  }

  // Try Intra ML prediction (within intra frame).
  const int try_intra_dip = !cpi->sf.intra_sf.skip_intra_dip_search &&
                            av1_intra_dip_allowed_bsize(&cpi->common, bsize);
  if (try_intra_dip) {
    if (rd_pick_intra_dip_sby(cpi, td, x, rate, rate_tokenonly, distortion,
                              skippable, bsize, mode_costs, &best_rd,
                              &best_model_rd, ctx
#if CONFIG_DIP_EXT_PRUNING

                              ,
                              &extra_dip
#endif  // CONFIG_DIP_EXT_PRUNING
                              )) {
      best_mbmi = *mbmi;
    }
  }

  // No mode is identified with less rd value than best_rd passed to this
  // function. In such cases winner mode processing is not necessary and
  // return best_rd as INT64_MAX to indicate best mode is not identified
  if (!beat_best_rd) return INT64_MAX;

  // In multi-winner mode processing, perform tx search for few best modes
  // identified during mode evaluation. Winner mode processing uses best tx
  // configuration for tx search.
  if (cpi->sf.winner_mode_sf.multi_winner_mode_type) {
    int best_mode_idx = 0;
    int block_width, block_height;
    uint8_t *color_map_dst = xd->plane[PLANE_TYPE_Y].color_index_map;
    av1_get_block_dimensions(bsize, AOM_PLANE_Y, xd, &block_width,
                             &block_height, NULL, NULL);

    for (int mode_idx = 0; mode_idx < x->winner_mode_count; mode_idx++) {
      *mbmi = x->winner_mode_stats[mode_idx].mbmi;
      if (is_winner_mode_processing_enabled(cpi, mbmi, mbmi->mode)) {
        // Restore color_map of palette mode before winner mode processing
        if (mbmi->palette_mode_info.palette_size[0] > 0) {
          uint8_t *color_map_src =
              x->winner_mode_stats[mode_idx].color_index_map;
          memcpy(color_map_dst, color_map_src,
                 block_width * block_height * sizeof(*color_map_src));
        }
        // Set params for winner mode evaluation
        set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);

        // Winner mode processing
        // If previous searches use only the default tx type/no R-D
        // optimization of quantized coeffs, do an extra search for the best
        // tx type/better R-D optimization of quantized coeffs
        if (intra_block_yrd(cpi, x, bsize, mode_costs, &best_rd, rate,
                            rate_tokenonly, distortion, skippable, &best_mbmi,
                            ctx))
          best_mode_idx = mode_idx;
      }
    }
    // Copy color_map of palette mode for final winner mode
    if (best_mbmi.palette_mode_info.palette_size[0] > 0) {
      uint8_t *color_map_src =
          x->winner_mode_stats[best_mode_idx].color_index_map;
      memcpy(color_map_dst, color_map_src,
             block_width * block_height * sizeof(*color_map_src));
    }
  } else {
    // If previous searches use only the default tx type/no R-D optimization
    // of quantized coeffs, do an extra search for the best tx type/better R-D
    // optimization of quantized coeffs
    if (is_winner_mode_processing_enabled(cpi, mbmi, best_mbmi.mode)) {
      // Set params for winner mode evaluation
      set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);
      *mbmi = best_mbmi;
      intra_block_yrd(cpi, x, bsize, mode_costs, &best_rd, rate, rate_tokenonly,
                      distortion, skippable, &best_mbmi, ctx);
    }
  }
  *mbmi = best_mbmi;
  if (mbmi->joint_y_mode_delta_angle < NON_DIRECTIONAL_MODES_COUNT)
    assert(mbmi->joint_y_mode_delta_angle == mbmi->y_mode_idx);
  av1_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
  return best_rd;
}
