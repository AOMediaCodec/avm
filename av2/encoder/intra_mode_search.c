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

#include <math.h>

#include "av2/common/av2_common_int.h"
#include "av2/common/intra_dip.h"
#include "av2/common/reconintra.h"

#include "av2/encoder/intra_mode_mlp.h"
#include "av2/encoder/intra_mode_search.h"
#include "av2/encoder/intra_mode_search_utils.h"
#include "av2/encoder/palette.h"
#include "av2/encoder/tx_search.h"
#include "av2/common/reconinter.h"
#if CONFIG_DIP_EXT_PRUNING
#include "av2/encoder/intra_dip_mode_prune_tflite.h"
#endif  // CONFIG_DIP_EXT_PRUNING

static AVM_INLINE int get_intra_dip_mode(int transpose, int ml_mode) {
  return (transpose << 4) + ml_mode;
}

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

/*! \brief Determine whether to keep DIP mode search based on pruning model.
 * \param[in/out] td                    ThreadData structure with pruning
 *                                      model.
 * \param[in]     x                     MACROBLOCK structure.
 * \param[in]     bsize                 Block size.
 * \param[in]     intra_dip_features    DIP features array.
 * \param[in]     dip_mode_model_log_rd Log RD values for DIP modes.
 * \param[in]     best_rd               Best rate-distortion found so far.
 * \param[in]     best_model_rd         Best model rate-distortion found.
 * \param[in]     base_y_dc_delta_q     Base delta Q value of DC for luma plane.
 * \return Returns true if DIP mode search should proceed, false otherwise.
 */
static bool keep_intra_dip_mode(ThreadData *const td, const MACROBLOCK *const x,
                                BLOCK_SIZE bsize,
                                const uint16_t intra_dip_features[11],
                                const float dip_mode_model_log_rd[12],
                                int64_t best_rd, int64_t best_model_rd,
                                int8_t base_y_dc_delta_q) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  const DipMlInfo *const dip_ml_feature = &x->dip_ml_feature;
  const int adjusted_qindex = x->qindex - MAXQ_OFFSET * (xd->bd - AVM_BITS_8);
  const int dip_model_index =
      intra_dip_mode_prune_get_model_index(adjusted_qindex);
  if (dip_model_index == -1) return true;

  const int dc_q = av2_dc_quant_QTX(x->qindex, 0, base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - AVM_BITS_8);

  // Clamp all RD values to a minimum of 1 to avoid arithmetic exceptions.
  const float log_best_rd = log10f((float)AVMMAX(best_rd, 1));
  const float log_best_model_rd = log10f((float)AVMMAX(best_model_rd, 1));
  const float log_dc_mode_rd =
      log10f((float)AVMMAX(dip_ml_feature->dc_mode_rd, 1));
  const float log_orig_best_rd =
      log10f((float)AVMMAX(dip_ml_feature->orig_best_rd, 1));

  dip_pruning_inputs *const dip_pruning_in =
      intra_dip_mode_prune_get_inputs(&td->dip_pruning_model, adjusted_qindex);

  dip_pruning_in->inputs[0].values[0] = log_best_model_rd;
  dip_pruning_in->inputs[0].values[1] = log_best_rd;
  dip_pruning_in->inputs[0].values[2] = log10f((float)dc_q);
  dip_pruning_in->inputs[0].values[3] = log_dc_mode_rd;
  dip_pruning_in->inputs[0].values[4] = log_orig_best_rd;
  for (int i = 0; i < 13; ++i) {
    dip_pruning_in->inputs[0].values[5 + i] =
        (float)(dip_ml_feature->best_mode == i);
  }
  dip_pruning_in->inputs[0].values[18] = (float)dip_ml_feature->beat_best_rd;
  intra_dip_mode_prune_normalize_and_resize_8x8(
      x->plane[0].src.buf, x->plane[0].src.stride, xd->bd,
      block_size_wide[bsize], block_size_high[bsize],
      &dip_pruning_in->inputs[1].values[0]);

  const float norm = (float)((1 << xd->bd) - 1);
  for (int i = 0; i < 11; ++i) {
    dip_pruning_in->inputs[2].values[i] = (float)intra_dip_features[i] / norm;
  }
  dip_pruning_in->inputs[3].values[0] = log2f((float)block_size_wide[bsize]);
  dip_pruning_in->inputs[3].values[1] = log2f((float)block_size_high[bsize]);
  for (int i = 0; i < 12; ++i) {
    dip_pruning_in->inputs[4].values[i] = dip_mode_model_log_rd[i];
  }
  float dip_prune_output = -1.0;
  intra_dip_mode_prune_tflite(&td->dip_pruning_model, &dip_prune_output,
                              adjusted_qindex);
  const float dip_prune_threshold = DIP_PRUNING_THRESHOLDS[dip_model_index];

  return dip_prune_output >= dip_prune_threshold;
}

static void rd_pick_intra_dip_sby_modelrd(
    const AV2_COMP *const cpi, MACROBLOCK *x, BLOCK_SIZE bsize, int mode_cost,
    DIPModeRDInfo intra_model_rds[TOP_DIP_INTRA_MODEL_COUNT],
    float dip_mode_model_log_rd[12]) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];

  int num_modes = av2_intra_dip_modes(bsize);
  int has_transpose = av2_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

  for (int i = 0; i < TOP_DIP_INTRA_MODEL_COUNT; ++i) {
    intra_model_rds[i].modelrd = INT64_MAX;
  }

  for (int transpose = 0; transpose < num_transpose; ++transpose) {
    for (int ml_mode = 0; ml_mode < num_modes; ++ml_mode) {
      int mode = get_intra_dip_mode(transpose, ml_mode);
      int dip_index = ml_mode + transpose * num_modes;
      mbmi->intra_dip_mode = mode;
      const int64_t this_model_rd = intra_model_yrd(cpi, x, bsize, mode_cost);
      dip_mode_model_log_rd[dip_index] =
          log10f((float)AVMMAX(this_model_rd, 1));

      for (int i = 0; i < TOP_DIP_INTRA_MODEL_COUNT; ++i) {
        if (this_model_rd < intra_model_rds[i].modelrd) {
          for (int j = TOP_DIP_INTRA_MODEL_COUNT - 1; j > i; --j) {
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
#endif  // CONFIG_DIP_EXT_PRUNING

/*! \brief Initialize macroblock mode info fields for intra DIP search.
 *
 * \param[out]  mbmi  Pointer to macroblock mode info structure to initialize.
 * \param[in]   xd    Pointer to macroblock decoder context containing segment
                      info.
*/
static AVM_INLINE void init_mbmi_for_intra_dip(MB_MODE_INFO *mbmi,
                                               const MACROBLOCKD *xd) {
  mbmi->mode = DC_PRED;
  if (xd->lossless[mbmi->segment_id]) {
    mbmi->use_dpcm_y = 0;
    mbmi->dpcm_mode_y = 0;
    mbmi->use_dpcm_uv = 0;
    mbmi->dpcm_mode_uv = 0;
  }
  mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
  mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
  mbmi->joint_y_mode_delta_angle = DC_PRED;
  mbmi->y_mode_idx = DC_PRED;

  mbmi->angle_delta[PLANE_TYPE_Y] = 0;
  mbmi->angle_delta[PLANE_TYPE_UV] = 0;
}

/*! \brief Search for the best intra_dip mode when coding intra frame.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function loops through all intra_dip modes to find the best one.
 *
 * \return Returns 1 if a new intra_dip mode is selected; 0 otherwise.
 */
static bool rd_pick_intra_dip_sby(const AV2_COMP *const cpi, ThreadData *td,
                                  MACROBLOCK *x, int *rate, int *rate_tokenonly,
                                  int64_t *distortion, int *skippable,
                                  BLOCK_SIZE bsize, int mode_cost,
                                  int64_t *best_rd, int64_t *best_model_rd,
                                  PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  bool intra_dip_selected = false;
  int best_ml_mode = 0;
  TX_SIZE best_tx_size = TX_8X8;
  TX_PARTITION_TYPE best_tx_partition = TX_PARTITION_NONE;
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  mbmi->use_intra_dip = 1;
  mbmi->palette_mode_info.palette_size[PLANE_TYPE_Y] = 0;
  mbmi->mrl_index = 0;
  mbmi->multi_line_mrl = 0;
  init_mbmi_for_intra_dip(mbmi, xd);

  int num_modes = av2_intra_dip_modes(bsize);
  int has_transpose = av2_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

#if CONFIG_DIP_EXT_PRUNING
  float dip_mode_model_log_rd[12] = { 0.0f };

  // Save baseline MB_MODE_INFO state since model RD evaluation modifies mbmi
  // fields.
  MB_MODE_INFO base_mbmi = *mbmi;
  DIPModeRDInfo intra_model_rds[TOP_DIP_INTRA_MODEL_COUNT];

  rd_pick_intra_dip_sby_modelrd(cpi, x, bsize, mode_cost, intra_model_rds,
                                dip_mode_model_log_rd);

  // Evaluate pruning decision using intra_dip_features computed by modelrd
  // pass.
  const bool pred_keep = keep_intra_dip_mode(
      td, x, bsize, mbmi->intra_dip_features, dip_mode_model_log_rd, *best_rd,
      *best_model_rd, cpi->common.seq_params.base_y_dc_delta_q);

  // Restore baseline mode info after ML pruning evaluation.
  *mbmi = base_mbmi;

  if (!pred_keep) {
    mbmi->use_intra_dip = 0;
    return false;
  }
#else
  (void)td;
#endif  // CONFIG_DIP_EXT_PRUNING

  for (int transpose = 0; transpose < num_transpose; ++transpose) {
    for (int ml_mode = 0; ml_mode < num_modes; ++ml_mode) {
      int mode = get_intra_dip_mode(transpose, ml_mode);
      mbmi->intra_dip_mode = mode;
#if CONFIG_DIP_EXT_PRUNING
      if (skip_this_dip_mode(intra_model_rds, mbmi->intra_dip_mode)) continue;
#else
      if (model_intra_yrd_and_prune(cpi, x, bsize, mode_cost, best_model_rd)) {
        continue;
      }
#endif  // CONFIG_DIP_EXT_PRUNING

      RD_STATS tokenonly_rd_stats;
      av2_pick_uniform_tx_size_type_yrd(cpi, x, &tokenonly_rd_stats, bsize,
                                        *best_rd);
      if (tokenonly_rd_stats.rate != INT_MAX) {
        const int this_rate =
            tokenonly_rd_stats.rate +
            intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_cost);
        int64_t this_rd = RDCOST(x->rdmult, this_rate, tokenonly_rd_stats.dist);
        // Collect mode stats for multiwinner mode processing.
        const int txfm_search_done = 1;
        const MV_REFERENCE_FRAME refs[2] = { NONE_FRAME, NONE_FRAME };
        store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, refs,
                                0, NULL, bsize, this_rd,
                                cpi->sf.winner_mode_sf.multi_winner_mode_type,
                                txfm_search_done);
        if (this_rd < *best_rd) {
          *best_rd = this_rd;
          best_tx_size = mbmi->tx_size;
          best_tx_partition = mbmi->tx_partition_type[0];
          av2_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
          memcpy(ctx->blk_skip[AVM_PLANE_Y],
                 x->txfm_search_info.blk_skip[AVM_PLANE_Y],
                 sizeof(*x->txfm_search_info.blk_skip[AVM_PLANE_Y]) *
                     ctx->num_4x4_blk);
          *rate = this_rate;
          *rate_tokenonly = tokenonly_rd_stats.rate;
          *distortion = tokenonly_rd_stats.dist;
          *skippable = tokenonly_rd_stats.skip_txfm;
          intra_dip_selected = true;
          best_ml_mode = mode;
        }
      }
    }
  }

  if (intra_dip_selected) {
    mbmi->intra_dip_mode = best_ml_mode;
    mbmi->tx_size = best_tx_size;
    mbmi->tx_partition_type[PLANE_TYPE_Y] = best_tx_partition;
    av2_copy_array(ctx->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
    init_mbmi_for_intra_dip(mbmi, xd);
    return true;
  } else {
    mbmi->use_intra_dip = 0;
    return false;
  }
}

void av2_count_colors_highbd(const uint16_t *src, int stride, int rows,
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

bool prune_intra_y_mode(int64_t this_model_rd, int64_t *best_model_rd,
                        int64_t top_intra_model_rd[], int prune_top,
                        int lossless, uint8_t use_dpcm_y) {
  (void)lossless;
  assert(IMPLIES(use_dpcm_y != 0, lossless != 0));
  assert(prune_top > 0);
  if (this_model_rd < *best_model_rd) *best_model_rd = this_model_rd;
  if (use_dpcm_y != 0) return false;

  // Only the top prune_top candidates get full RD; the rest are pruned.
  const int threshold_slot = prune_top - 1;
  // top_intra_model_rd[] holds the smallest model RDs seen so far, in
  // ascending order, kept sorted up to threshold_slot.
  for (int i = 0; i <= threshold_slot; i++) {
    if (this_model_rd < top_intra_model_rd[i]) {
      for (int j = threshold_slot; j > i; j--) {
        top_intra_model_rd[j] = top_intra_model_rd[j - 1];
      }
      top_intra_model_rd[i] = this_model_rd;
      break;
    }
  }
  if (top_intra_model_rd[threshold_slot] != INT64_MAX &&
      this_model_rd > top_intra_model_rd[threshold_slot])
    return true;

  return false;
}

#define PLANE_SIGN_TO_JOINT_SIGN(plane, a, b) \
  (plane == CFL_PRED_U ? a * CFL_SIGNS + b - 1 : b * CFL_SIGNS + a - 1)
static int cfl_rd_pick_alpha(MACROBLOCK *const x, const AV2_COMP *const cpi,
                             int64_t best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const MACROBLOCKD_PLANE *pd = &xd->plane[AVM_PLANE_U];
  const ModeCosts *mode_costs = &x->mode_costs;
  assert(xd->tree_type != LUMA_PART);
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, mbmi, PLANE_TYPE_UV, pd->subsampling_x, pd->subsampling_y);
  assert(((is_cfl_allowed(cpi->oxcf.intra_mode_cfg.enable_cfl_intra, xd) &&
           cpi->oxcf.intra_mode_cfg.enable_cfl_intra) ||
          is_mhccp_allowed(&cpi->common, xd)));

  assert(plane_bsize < BLOCK_SIZES_ALL);
  xd->cfl.use_dc_pred_cache = 1;
  xd->cfl.dc_pred_is_cached[0] = 0;
  xd->cfl.dc_pred_is_cached[1] = 0;
  const int cfl_ctx = get_cfl_ctx(xd);
  ;
  const int64_t mode_rd =
      RDCOST(x->rdmult, mode_costs->cfl_mode_cost[cfl_ctx][1], 0);

  int64_t best_rd_uv[CFL_JOINT_SIGNS][CFL_PRED_PLANES];
  int best_c[CFL_JOINT_SIGNS][CFL_PRED_PLANES];

  const int skip_trellis = 0;
  int8_t best_joint_sign = -1;
  // process CFL_PRED_U
  RD_STATS rd_stats;
  av2_init_rd_stats(&rd_stats);
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
    av2_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize,
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
      av2_init_rd_stats(&rd_stats);
      for (int i = 0; i < CFL_SIGNS; i++) {
        const int8_t joint_sign =
            PLANE_SIGN_TO_JOINT_SIGN(CFL_PRED_U, pn_sign, i);
        mbmi->cfl_alpha_idx = (c << CFL_ALPHABET_SIZE_LOG2) + c;
        mbmi->cfl_alpha_signs = joint_sign;
        av2_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize,
                             FTXS_NONE, skip_trellis);
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
  // joint_sign. This is necessary due to cross component dependency.
  // The combined (CFL_PRED_U and CFL_PRED_V) RDCOST will be used to decide
  // the best_joint_sign.
  for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
    int progress = 0;
    for (int c = 0; c < CFL_ALPHABET_SIZE; c++) {
      int flag = 0;
      if (c > 2 && progress < c) break;
      av2_init_rd_stats(&rd_stats);
      mbmi->cfl_alpha_idx =
          (best_c[joint_sign][CFL_PRED_U] << CFL_ALPHABET_SIZE_LOG2) + c;
      mbmi->cfl_alpha_signs = joint_sign;
      av2_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 1, plane_bsize,
                           FTXS_NONE, skip_trellis);
      av2_txfm_rd_in_plane(x, cpi, &rd_stats, best_rd, 0, 2, plane_bsize,
                           FTXS_NONE, skip_trellis);
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
  const int uv_context = av2_is_directional_mode(mbmi->mode) ? 1 : 0;
  if (cfl_allowed) {
    const int cfl_ctx = get_cfl_ctx(xd);
    if (mbmi->uv_mode == UV_CFL_PRED) {
      return mode_costs.cfl_mode_cost[cfl_ctx][1];
    }
    int cost = mode_costs.cfl_mode_cost[cfl_ctx][0];
    int mode_set_low = AVMMIN(mode_index, CHROMA_INTRA_MODE_INDEX_COUNT - 1);
    cost += mode_costs.intra_uv_mode_cost[uv_context][mode_set_low];
    if (mode_set_low == (CHROMA_INTRA_MODE_INDEX_COUNT - 1))
      cost += av2_cost_literal(3);
    return cost;
  }
  int mode_set_low = AVMMIN(mode_index, CHROMA_INTRA_MODE_INDEX_COUNT - 1);
  int cost = mode_costs.intra_uv_mode_cost[uv_context][mode_set_low];
  if (mode_set_low == (CHROMA_INTRA_MODE_INDEX_COUNT - 1))
    cost += av2_cost_literal(3);
  return cost;
}

// For a given chroma (UV) mode, compute a specific index and use the same to
// store/fetch rate and distortion information.
// The below specifies the index given for chroma modes:
// 0       : for UV_DC_PRED.
// 1 - 56  : for directional UV modes.
// 57 - 60 : for non-directional UV modes (i.e., UV_SMOOTH_PRED,
// UV_SMOOTH_V_PRED, UV_SMOOTH_H_PRED, UV_PAETH_PRED)
static AVM_INLINE int get_chroma_idx_for_reuse_uvrd(MB_MODE_INFO *mbmi) {
  const int uv_angle = mbmi->angle_delta[AVM_PLANE_U];
  int chroma_idx = 0;
  if (av2_is_directional_mode(get_uv_mode(mbmi->uv_mode))) {
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
static AVM_INLINE void store_uv_mode_rd_info(ModeRDInfoUV *mode_rd_info_uv,
                                             RD_STATS *tokenonly_rd_stats,
                                             const int chroma_idx) {
  mode_rd_info_uv->dist_info[chroma_idx] = tokenonly_rd_stats->dist;
  mode_rd_info_uv->rate_info[chroma_idx] = tokenonly_rd_stats->rate;
  mode_rd_info_uv->mode_evaluated[chroma_idx] = true;
}
// Fetch and reuse the UV mode RD information.
static AVM_INLINE void fetch_uv_mode_rd_info(ModeRDInfoUV *mode_rd_info_uv,
                                             RD_STATS *tokenonly_rd_stats,
                                             const int chroma_idx) {
  av2_init_rd_stats(tokenonly_rd_stats);
  tokenonly_rd_stats->dist = mode_rd_info_uv->dist_info[chroma_idx];
  tokenonly_rd_stats->rate = mode_rd_info_uv->rate_info[chroma_idx];
}

int64_t av2_rd_pick_intra_sbuv_mode(const AV2_COMP *const cpi, MACROBLOCK *x,
                                    int *rate, int *rate_tokenonly,
                                    int64_t *distortion, int *skippable,
                                    const PICK_MODE_CONTEXT *ctx,
                                    BLOCK_SIZE bsize, TX_SIZE max_tx_size,
                                    ModeRDInfoUV *mode_rd_info_uv) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  assert(!is_inter_block(mbmi, xd->tree_type));
  MB_MODE_INFO best_mbmi = *mbmi;
  int64_t best_rd = INT64_MAX, this_rd;
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
      av2_encode_intra_block_plane(cpi, x, mbmi->sb_type[PLANE_TYPE_Y],
                                   AVM_PLANE_Y, DRY_RUN_NORMAL,
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
        mode_cost += get_uv_mode_cost(
            mbmi, x->mode_costs, xd,
            is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
                is_mhccp_allowed(cm, xd),
            mbmi->uv_mode_idx);
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
        if (!is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) &&
            (mbmi->cfl_idx == CFL_EXPLICIT ||
             mbmi->cfl_idx == CFL_DERIVED_ALPHA))
          continue;

        if (mbmi->cfl_idx == CFL_MULTI_PARAM && !is_mhccp_allowed(cm, xd))
          continue;
        if (mbmi->cfl_idx == 0)
          cfl_alpha_rate = cfl_rd_pick_alpha(x, cpi, best_rd);
        if (is_mhccp_allowed(cm, xd)) {
          if (intra_mode_cfg->enable_cfl_intra)
            cfl_idx_rate +=
                x->mode_costs.cfl_mhccp_cost[mbmi->cfl_idx == CFL_MULTI_PARAM];
        }
        if (mbmi->cfl_idx != CFL_MULTI_PARAM &&
            intra_mode_cfg->enable_cfl_intra) {
          cfl_idx_rate += x->mode_costs.cfl_index_cost[mbmi->cfl_idx];
        }
        if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
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
        if (!av2_txfm_uvrd(cpi, x, &tokenonly_rd_stats, INT64_MAX)) continue;
      } else {
        const int chroma_idx = get_chroma_idx_for_reuse_uvrd(mbmi);
        if (!mode_rd_info_uv->mode_evaluated[chroma_idx]) {
          if (!av2_txfm_uvrd(cpi, x, &tokenonly_rd_stats, INT64_MAX)) continue;
          store_uv_mode_rd_info(mode_rd_info_uv, &tokenonly_rd_stats,
                                chroma_idx);
        } else {
          fetch_uv_mode_rd_info(mode_rd_info_uv, &tokenonly_rd_stats,
                                chroma_idx);
          is_mode_rd_info_fetched = true;
        }
      }
      this_rate = tokenonly_rd_stats.rate + mode_cost;

      if (mode == UV_CFL_PRED &&
          (cm->seq_params.enable_cfl_intra || cm->seq_params.enable_mhccp)) {
        assert((is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) &&
                intra_mode_cfg->enable_cfl_intra) ||
               is_mhccp_allowed(cm, xd));
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
        av2_copy_array(tmp_cctx_type_map, xd->cctx_type_map,
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

  *mbmi = best_mbmi;
  // Copy back the best cross-chroma txfm type (tmp_cctx_type_map)
  // to xd->cctx_type_map.
  av2_copy_array(xd->cctx_type_map, tmp_cctx_type_map, ctx->num_4x4_blk_chroma);
  // Make sure we actually chose a mode
  assert(best_rd < INT64_MAX);
  return best_rd;
}

// Searches palette mode for luma channel in inter frame.
int av2_search_palette_mode(IntraModeSearchState *intra_search_state,
                            const AV2_COMP *cpi, MACROBLOCK *x,
                            BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                            PICK_MODE_CONTEXT *ctx, RD_STATS *rd_cost,
                            int64_t *best_rd, MB_MODE_INFO *best_mbmode,
                            int *best_skip2, int *best_mode_skippable,
                            int is_intra_mode_allowed) {
  if (!is_intra_mode_allowed) return 0;

  const AV2_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  MB_MODE_INFO *const mbmi = x->e_mbd.mi[0];

  // Only try palette mode when the best mode so far is an intra mode.
  int search_palette_mode =
      cpi->oxcf.tool_cfg.enable_palette &&
      av2_allow_palette(PLANE_TYPE_Y, features->allow_screen_content_tools,
                        mbmi->sb_type[PLANE_TYPE_Y]) &&
      !is_inter_mode(best_mbmode->mode) && rd_cost->rate < INT_MAX;
  const MB_MODE_INFO *cached_mode = x->inter_mode_cache[0];
  if (should_reuse_mode(x, REUSE_INTRA_MODE_IN_INTERFRAME_FLAG) &&
      cached_mode &&
      !(cached_mode->mode == DC_PRED &&
        cached_mode->palette_mode_info.palette_size[0] > 0)) {
    search_palette_mode = 0;
  }

  if (!search_palette_mode) return 0;

  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int num_planes = av2_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  int rate2 = 0;
  int64_t distortion2 = 0, best_rd_palette = *best_rd, this_rd,
          best_model_rd_palette = INT64_MAX;
  int skippable = 0;
  uint8_t *const best_palette_color_map =
      x->palette_buffer->best_palette_color_map;
  uint8_t *const color_map = xd->plane[0].color_index_map;
  MB_MODE_INFO best_mbmi_palette = *mbmi;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  const int context = get_y_mode_idx_ctx(xd);
  int mode_cost = x->mode_costs.y_primary_flag_cost[DC_PRED];
  mode_cost += x->mode_costs.y_mode_idx_costs[context][DC_PRED];
  const int rows = block_size_high[bsize];
  const int cols = block_size_wide[bsize];

  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
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
  mbmi->warp_inter_intra = 0;

  RD_STATS rd_stats_y;
  av2_invalid_rd_stats(&rd_stats_y);
  av2_rd_pick_palette_intra_sby(cpi, x, bsize, mode_cost, &best_mbmi_palette,
                                best_palette_color_map, &best_rd_palette,
                                &best_model_rd_palette, &rd_stats_y.rate, NULL,
                                &rd_stats_y.dist, &rd_stats_y.skip_txfm, NULL,
                                ctx, best_blk_skip, best_tx_type_map);
  if (rd_stats_y.rate == INT_MAX || pmi->palette_size[0] == 0) {
    return 0;
  }

  memcpy(x->txfm_search_info.blk_skip[AVM_PLANE_Y], best_blk_skip,
         sizeof(best_blk_skip[0]) * bsize_to_num_blk(bsize));
  av2_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
  memcpy(color_map, best_palette_color_map,
         rows * cols * sizeof(best_palette_color_map[0]));

  skippable = rd_stats_y.skip_txfm;
  distortion2 = rd_stats_y.dist;
  rate2 = rd_stats_y.rate + ref_frame_cost;
  if (num_planes > 1) {
    {
      // We have not found any good uv mode yet, so we need to search for it.
      TX_SIZE uv_tx = av2_get_tx_size(AVM_PLANE_U, xd);
      av2_rd_pick_intra_sbuv_mode(cpi, x, &intra_search_state->rate_uv_intra,
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

  if (this_rd < *best_rd) {
    mbmi->mv[0].as_int = 0;
    rd_cost->rate = rate2;
    rd_cost->dist = distortion2;
    rd_cost->rdcost = this_rd;
    *best_rd = this_rd;
    *best_mbmode = *mbmi;
    *best_skip2 = 0;
    *best_mode_skippable = skippable;

    TxfmSearchInfo *txfm_info = &x->txfm_search_info;
    for (int i = 0; i < num_planes; ++i) {
      const int num_blk_plane =
          (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
      memcpy(ctx->blk_skip[i], txfm_info->blk_skip[i],
             sizeof(*txfm_info->blk_skip[i]) * num_blk_plane);
    }
    av2_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
    av2_copy_array(ctx->cctx_type_map, xd->cctx_type_map,
                   ctx->num_4x4_blk_chroma);
    return 1;
  }

  return 0;
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
static AVM_INLINE int intra_block_yrd(const AV2_COMP *const cpi, MACROBLOCK *x,
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
  av2_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats, bsize, INT64_MAX);
  if (rd_stats.rate == INT_MAX) return 0;
  int this_rate_tokenonly = rd_stats.rate;
  if (!xd->lossless[mbmi->segment_id] &&
      block_signals_txsize(mbmi->sb_type[PLANE_TYPE_Y])) {
    // av2_pick_uniform_tx_size_type_yrd above includes the cost of the
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
    av2_copy_array(ctx->blk_skip[AVM_PLANE_Y],
                   x->txfm_search_info.blk_skip[AVM_PLANE_Y], ctx->num_4x4_blk);
    av2_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
    return 1;
  }
  return 0;
}

/*!\brief Prune a DIP mode candidate based on model RD cost.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function estimates the model RD cost for the current DIP mode and
 * prunes it if it falls outside the top N model RD candidates. Pruning is
 * skipped when the speed feature is disabled or when the block is lossless
 * with DPCM enabled.
 *
 * \param[in]     cpi                Top-level encoder structure.
 * \param[in]     x                  Pointer to macroblock structure.
 * \param[in,out] top_intra_model_rd Top intra model RD cost array.
 * \param[in,out] best_model_rd      Best model RD cost seen so far.
 * \param[in]     bsize              Block size.
 * \param[in]     mode_cost          Mode signaling cost.
 *
 * \return 1 if the mode should be pruned, 0 otherwise.
 */
static INLINE int prune_intra_dip_mode(const AV2_COMP *cpi, MACROBLOCK *x,
                                       int64_t top_intra_model_rd[],
                                       int64_t *best_model_rd, BLOCK_SIZE bsize,
                                       int mode_cost) {
  if (!cpi->sf.intra_sf.include_dip_for_top_n_model_rd_pruning) return 0;

  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int64_t this_model_rd = intra_model_yrd(cpi, x, bsize, mode_cost);
  if (prune_intra_y_mode(this_model_rd, best_model_rd, top_intra_model_rd,
                         x->intra_mode_prune_top,
                         xd->lossless[mbmi->segment_id], mbmi->use_dpcm_y))
    return 1;
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
static INLINE void handle_intra_dip_mode(
    const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
    const PICK_MODE_CONTEXT *ctx, RD_STATS *rd_stats_y, int mode_cost,
    int64_t best_rd, int64_t best_rd_so_far, int64_t *best_model_rd,
    int64_t top_intra_model_rd[]) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->mode == DC_PRED &&
         av2_intra_dip_allowed_bsize(&cpi->common, bsize));

  set_mv_precision(mbmi, mbmi->max_mv_precision);

  mbmi->refinemv_flag = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;

  RD_STATS rd_stats_y_iml;
  int intra_dip_selected_flag = 0;
  int best_ml_mode = 0;
  TX_SIZE best_tx_size = mbmi->tx_size;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  memcpy(best_blk_skip, x->txfm_search_info.blk_skip[AVM_PLANE_Y],
         sizeof(best_blk_skip[0]) * ctx->num_4x4_blk);
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_SIZE best_tx_partition = mbmi->tx_partition_type[0];
  av2_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
  mbmi->use_intra_dip = 1;

  int num_modes = av2_intra_dip_modes(bsize);
  int has_transpose = av2_intra_dip_has_transpose(bsize);
  int num_transpose = has_transpose ? 2 : 1;

  for (int transpose = 0; transpose < num_transpose; transpose++) {
    for (int ml_mode = 0; ml_mode < num_modes; ml_mode++) {
      int mode = (transpose << 4) + ml_mode;
      mbmi->intra_dip_mode = mode;

      if (prune_intra_dip_mode(cpi, x, top_intra_model_rd, best_model_rd, bsize,
                               mode_cost)) {
        continue;
      }

      av2_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y_iml, bsize,
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
        av2_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
        memcpy(best_blk_skip, x->txfm_search_info.blk_skip[AVM_PLANE_Y],
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
  av2_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
  memcpy(x->txfm_search_info.blk_skip[AVM_PLANE_Y], best_blk_skip,
         sizeof(*x->txfm_search_info.blk_skip[AVM_PLANE_Y]) * ctx->num_4x4_blk);

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

int64_t av2_handle_intra_mode(IntraModeSearchState *intra_search_state,
                              const AV2_COMP *cpi, MACROBLOCK *x,
                              BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                              const PICK_MODE_CONTEXT *ctx, RD_STATS *rd_stats,
                              RD_STATS *rd_stats_y, RD_STATS *rd_stats_uv,
                              ModeRDInfoUV *mode_rd_info_uv, int64_t best_rd,
                              int64_t *best_intra_rd, int64_t *best_model_rd,
                              int64_t top_intra_model_rd[]) {
  const AV2_COMMON *cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->ref_frame[0] == INTRA_FRAME);
  const PREDICTION_MODE mode = mbmi->mode;
  const ModeCosts *mode_costs = &x->mode_costs;

  int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
  int mrl_idx_cost =
      (av2_is_directional_mode(mbmi->mode) &&
       cpi->common.seq_params.enable_mrls)
          ? x->mode_costs.mrl_index_cost[mrl_ctx][mbmi->mrl_index]
          : 0;
  if (av2_is_directional_mode(mbmi->mode) &&
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
        int mode_set_low =
            AVMMIN(mbmi->y_mode_idx, LUMA_INTRA_MODE_INDEX_COUNT - 1);
        mode_cost += x->mode_costs.y_mode_idx_costs[context][mode_set_low];
        if (mode_set_low == (LUMA_INTRA_MODE_INDEX_COUNT - 1))
          mode_cost +=
              x->mode_costs.y_mode_idx_offset_costs[context][mbmi->y_mode_idx -
                                                             mode_set_low];
      } else {
        mode_cost += av2_cost_literal(4);
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
      int mode_set_low =
          AVMMIN(mbmi->y_mode_idx, LUMA_INTRA_MODE_INDEX_COUNT - 1);
      mode_cost += x->mode_costs.y_mode_idx_costs[context][mode_set_low];
      if (mode_set_low == (LUMA_INTRA_MODE_INDEX_COUNT - 1))
        mode_cost +=
            x->mode_costs.y_mode_idx_offset_costs[context][mbmi->y_mode_idx -
                                                           mode_set_low];
    } else {
      mode_cost += av2_cost_literal(4);
    }
    mode_cost += ref_frame_cost;
    mode_cost += mrl_idx_cost;
  }

  const int intra_cost_penalty = av2_get_intra_cost_penalty(
      cm->quant_params.base_qindex, cm->quant_params.y_dc_delta_q,
      cm->seq_params.base_y_dc_delta_q, cm->seq_params.bit_depth);

  int known_rate = mode_cost;
  if (mode != DC_PRED && mode != PAETH_PRED) known_rate += intra_cost_penalty;
  const int64_t known_rd = RDCOST(x->rdmult, known_rate, 0);
  if (known_rd > best_rd) {
    intra_search_state->skip_intra_modes = 1;
    return INT64_MAX;
  }

  const int is_directional_mode = av2_is_directional_mode(mode);
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
  if (prune_intra_y_mode(this_model_rd, best_model_rd, top_intra_model_rd,
                         x->intra_mode_prune_top,
                         xd->lossless[mbmi->segment_id], mbmi->use_dpcm_y))
    return INT64_MAX;
  if (cpi->sf.intra_sf.intra_pruning_with_mlp && mbmi->mrl_index == 0 &&
      av2_is_directional_mode(mbmi->mode))
    intra_search_state->mrl0_dir_mode_survived[mbmi->mode] = 1;
  av2_init_rd_stats(rd_stats_y);
  x->prune_tx_partition = 0;
  av2_pick_uniform_tx_size_type_yrd(cpi, x, rd_stats_y, bsize, best_rd);

  if (mode == DC_PRED && xd->tree_type != CHROMA_PART &&
      av2_intra_dip_allowed_bsize(cm, bsize)) {
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
                            best_rd_so_far, best_model_rd, top_intra_model_rd);
    }
  }

  if (rd_stats_y->rate == INT_MAX) return INT64_MAX;

  const int mode_cost_y =
      intra_mode_info_cost_y(cpi, x, mbmi, bsize, mode_cost);
  av2_init_rd_stats(rd_stats);
  av2_init_rd_stats(rd_stats_uv);
  const int num_planes = av2_num_planes(cm);
  if (num_planes > 1) {
    // TODO(chiyotsai@google.com): Consolidate the chroma search code here
    // with the one in av2_search_palette_mode.
    PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
    const int try_palette =
        cpi->oxcf.tool_cfg.enable_palette &&
        av2_allow_palette(PLANE_TYPE_UV,
                          cm->features.allow_screen_content_tools,
                          mbmi->sb_type[PLANE_TYPE_Y]);
    // If no good uv-predictor had been found, search for it.
    const int rate_y = rd_stats_y->rate;
    const int64_t rdy =
        RDCOST(x->rdmult, rate_y + mode_cost_y, rd_stats_y->dist);
    if (best_rd < (INT64_MAX / 2) && rdy > (best_rd + (best_rd >> 2))) {
      intra_search_state->skip_intra_modes = 1;
      return INT64_MAX;
    }
    const TX_SIZE uv_tx = av2_get_tx_size(AVM_PLANE_U, xd);
    av2_rd_pick_intra_sbuv_mode(
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
    // av2_pick_uniform_tx_size_type_yrd above includes the cost of the
    // tx_size in the tokenonly rate, but for intra blocks, tx_size is always
    // coded (prediction granularity), so we account for it in the full rate,
    // not the tokenonly rate.
    rd_stats_y->rate -= tx_size_cost(x, bsize, mbmi->tx_size);
  }
  if (num_planes > 1 && xd->is_chroma_ref) {
    const int uv_mode_cost = get_uv_mode_cost(
        mbmi, x->mode_costs, xd,
        is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd), mbmi->uv_mode_idx);
    rd_stats->rate += rd_stats_uv->rate + uv_mode_cost;
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
    intra_search_state->best_mrl_index = mbmi->mrl_index;
    intra_search_state->best_multi_line_mrl = mbmi->multi_line_mrl;
  }

  if (sf->intra_sf.skip_intra_in_interframe) {
    if (best_rd < (INT64_MAX / 2) && this_rd > (best_rd + (best_rd >> 1)))
      intra_search_state->skip_intra_modes = 1;
  }

  for (int i = 0; i < REFERENCE_MODES; ++i) {
    intra_search_state->best_pred_rd[i] =
        AVMMIN(intra_search_state->best_pred_rd[i], this_rd);
  }

  return this_rd;
}

// Compute MLP-derived base-mode mask for the current block. See header for
// API doc. Runs once per block.
void av2_intra_mlp_compute_mode_mask(
    const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
    uint8_t mlp_mode_mask[INTRA_MODES], int *mlp_fallback,
    uint8_t directional_mode_skip_mask[INTRA_MODES]) {
  MACROBLOCKD *const xd = &x->e_mbd;

  memset(mlp_mode_mask, 0, INTRA_MODES * sizeof(mlp_mode_mask[0]));
  *mlp_fallback = 0;

  // Fall back for 4x4, 4x8, 8x4 blocks.
  if (block_size_wide[bsize] * block_size_high[bsize] <= 32) {
    *mlp_fallback = 1;
    if (cpi->sf.intra_sf.intra_pruning_with_hog) {
      prune_intra_mode_with_hog(x, bsize,
                                cpi->sf.intra_sf.intra_pruning_with_hog_thresh,
                                directional_mode_skip_mask);
    }
    return;
  }

  float mlp_features[MLP_INPUT_DIM];
  float mlp_logits[MLP_OUTPUT_DIM];
  int mlp_topk_modes[MLP_TOP_K];

  // Extract neighbor context. Treat inter / IBC neighbors as unavailable so
  // the MLP receives the same "no intra neighbor" sentinel it saw during
  // training; otherwise mbmi->mode would be an inter mode index (>=
  // INTRA_MODES), which collides with the [0, 12] intra range and is
  // out-of-distribution.
  int above_mode = -1, above_delta = 0, left_mode = -1, left_delta = 0;
  const MB_MODE_INFO *above_mi = xd->above_mbmi;
  const MB_MODE_INFO *left_mi = xd->left_mbmi;
  if (above_mi && !is_inter_block(above_mi, SHARED_PART)) {
    above_mode = above_mi->mode;
    above_delta = above_mi->angle_delta[PLANE_TYPE_Y];
  }
  if (left_mi && !is_inter_block(left_mi, SHARED_PART)) {
    left_mode = left_mi->mode;
    left_delta = left_mi->angle_delta[PLANE_TYPE_Y];
  }

  intra_mode_mlp_prepare_features(
      x->plane[0].src.buf, x->plane[0].src.stride, xd->bd,
      block_size_wide[bsize], block_size_high[bsize],
      x->qindex - MAXQ_OFFSET * (xd->bd - 8), above_mode, above_delta,
      left_mode, left_delta, !frame_is_intra_only(&cpi->common), mlp_features);

  intra_mode_mlp_predict(mlp_features, mlp_logits);

  // Use MLP top-K if confident, fall back to HOG if all logits negative.
  float max_logit = mlp_logits[0];
  for (int i = 1; i < MLP_OUTPUT_DIM; i++)
    if (mlp_logits[i] > max_logit) max_logit = mlp_logits[i];

  if (max_logit >= 0.0f) {
    intra_mode_mlp_get_topk(mlp_logits, MLP_TOP_K, mlp_topk_modes);
    for (int i = 0; i < MLP_TOP_K; i++) mlp_mode_mask[mlp_topk_modes[i]] = 1;
  } else {
    *mlp_fallback = 1;
    if (cpi->sf.intra_sf.intra_pruning_with_hog) {
      prune_intra_mode_with_hog(x, bsize,
                                cpi->sf.intra_sf.intra_pruning_with_hog_thresh,
                                directional_mode_skip_mask);
    }
  }
}

/*! \brief Structure to hold best FSC mode parameters during intra mode search.
 */
typedef struct FscBestParams {
  /*! \brief Reference line index for multiple reference line selection. */
  int mrl_idx;
  /*! \brief Flag to indicate more than one reference line used for multiple
   * reference line selection. */
  int multi_line_mrl;
  /*! \brief Delta angle used in directional mode. */
  int angle_delta;
  /*! \brief Transform type map for the given transform block. */
  TX_TYPE tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  /*! \brief Transform partition type. */
  TX_PARTITION_TYPE tx_partition_type[TX_PARTITION_BUF];
  /*! \brief The intra prediction mode used. */
  PREDICTION_MODE intra_mode;
  /*! \brief Transform size when fixed size txfm is used. */
  TX_SIZE tx_size;
  /*! \brief Mode index corresponding to a luma intra mode after re-ordering. */
  uint8_t y_mode_idx;
  /*! \brief Joint mode index corresponding to a luma intra mode before
   * re-ordering. */
  uint8_t joint_ymode;
  /*! \brief Flag to indicate if a valid best FSC mode is found. */
  uint8_t best_fsc_mode;
  /*! \brief Whether DPCM mode is selected for luma block. */
  uint8_t use_dpcm_y;
  /*! \brief DPCM direction if DPCM is selected for luma block. */
  uint8_t dpcm_mode_y;
} FscBestParams;

static AVM_INLINE void init_fsc_best_params(FscBestParams *const fsc_best,
                                            const MB_MODE_INFO *const best_mbmi,
                                            const MB_MODE_INFO *const mbmi) {
  fsc_best->y_mode_idx = best_mbmi->y_mode_idx;
  fsc_best->joint_ymode = best_mbmi->joint_y_mode_delta_angle;
  fsc_best->best_fsc_mode = 0;
  fsc_best->intra_mode = best_mbmi->mode;
  fsc_best->tx_size = best_mbmi->tx_size;
  av2_copy(fsc_best->tx_partition_type, best_mbmi->tx_partition_type);
  fsc_best->mrl_idx = best_mbmi->mrl_index;
  fsc_best->multi_line_mrl = best_mbmi->multi_line_mrl;
  fsc_best->angle_delta = best_mbmi->angle_delta[PLANE_TYPE_Y];
  fsc_best->use_dpcm_y = mbmi->use_dpcm_y;
  fsc_best->dpcm_mode_y = mbmi->dpcm_mode_y;
}

// Calculates mode signaling bit cost for luma intra prediction mode.
static AVM_INLINE int get_intra_mode_cost_y(
    MACROBLOCKD *const xd, MB_MODE_INFO *const mbmi,
    const ModeCosts *const mode_costs, int mode_idx, int context,
    bool is_lossless, bool enable_mrls_flag, int fsc_mode) {
  const MB_MODE_INFO *const neighbor0 = xd->neighbors[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors[1];
  int use_dpcm_y = mbmi->use_dpcm_y;
  int this_mode_cost = 0;
  if (is_lossless) {
    const int dpcm_cost = mode_costs->dpcm_cost[use_dpcm_y];
    this_mode_cost += dpcm_cost;
  }

  // Add the bit cost for signaling the selected luma prediction mode. Non-DPCM
  // modes are signaled using the standard luma mode syntax, whereas DPCM modes
  // use the dedicated DPCM direction syntax.
  if (use_dpcm_y == 0) {
    // Reset DPCM prediction direction to 0 for non-DPCM modes during standard
    // non-FSC search. In FSC pass, preserve the initial DPCM direction state.
    if (!fsc_mode) mbmi->dpcm_mode_y = 0;
    int mode_set_index = mbmi->y_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
    mode_set_index +=
        ((mbmi->y_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
    this_mode_cost += mode_costs->y_primary_flag_cost[mode_set_index];
    if (mode_idx < FIRST_MODE_COUNT) {
      const int mode_set_low =
          AVMMIN(mode_idx, LUMA_INTRA_MODE_INDEX_COUNT - 1);
      this_mode_cost += mode_costs->y_mode_idx_costs[context][mode_set_low];
      if (mode_set_low == (LUMA_INTRA_MODE_INDEX_COUNT - 1))
        this_mode_cost +=
            mode_costs
                ->y_mode_idx_offset_costs[context][mode_idx - mode_set_low];
    } else {
      this_mode_cost += av2_cost_literal(4);
    }
  } else {
    mbmi->dpcm_mode_y = mbmi->mode - 1;
    const int dpcm_dir_cost =
        mode_costs->dpcm_vert_horz_cost[mbmi->dpcm_mode_y];
    this_mode_cost += dpcm_dir_cost;
  }

  // In FSC intra search, MRL index signaling cost is added unconditionally to
  // account for MRL signaling when combining MRL with FSC mode.
  if (use_dpcm_y == 0 || fsc_mode) {
    const bool is_directional_mode = av2_is_directional_mode(mbmi->mode);
    const bool use_mrl = is_directional_mode && enable_mrls_flag;
    if (use_mrl) {
      const int mrl_ctx = get_mrl_index_ctx(neighbor0, neighbor1);
      int mrl_idx_cost = mode_costs->mrl_index_cost[mrl_ctx][mbmi->mrl_index];
      if (mbmi->mrl_index) {
        const int multi_line_mrl_ctx =
            get_multi_line_mrl_index_ctx(neighbor0, neighbor1);
        mrl_idx_cost +=
            mode_costs
                ->multi_line_mrl_cost[multi_line_mrl_ctx][mbmi->multi_line_mrl];
      }
      this_mode_cost += mrl_idx_cost;
    }
  }

  return this_mode_cost;
}

// Determines whether the given intra mode should be skipped based on speed
// features and pruning heuristics.
static AVM_INLINE bool skip_intra_mode(
    const MB_MODE_INFO *mbmi, int mode_idx, int mrl_idx, int dpcm_index,
    const uint8_t directional_mode_skip_mask[], const MB_MODE_INFO *best_mbmi,
    bool allow_smooth_intra, bool allow_paeth_intra,
    uint8_t mrl0_dir_mode_survived, uint8_t mlp_mode_mask, int mlp_fallback,
    bool intra_pruning_with_mlp, bool fsc_mode) {
  const PREDICTION_MODE mode = mbmi->mode;
  // Skip unsupported DPCM candidates.
  if (dpcm_index > 0 && (mrl_idx > 0 || (mode != V_PRED && mode != H_PRED) ||
                         ((mode == V_PRED || mode == H_PRED) &&
                          mbmi->angle_delta[PLANE_TYPE_Y] != 0)))
    return true;

  // Skip smooth intra modes when disabled by encoder configuration or speed
  // features.
  if (!allow_smooth_intra &&
      (mode == SMOOTH_PRED || mode == SMOOTH_H_PRED || mode == SMOOTH_V_PRED))
    return true;

  // Skip Paeth prediction when disabled.
  if (!allow_paeth_intra && mode == PAETH_PRED) return true;

  const bool is_directional_mode = av2_is_directional_mode(mode);

  // Skip secondary directional modes for MRL lines when MLP pruning is enabled
  // if the mode did not survive line 0 (MRL 0) evaluation.
  if (!fsc_mode && intra_pruning_with_mlp && mrl_idx > 0 &&
      is_directional_mode && mode_idx >= FIRST_MODE_COUNT &&
      !mrl0_dir_mode_survived)
    return true;

  // Skip MRL candidates for non-directional intra prediction modes.
  if (!is_directional_mode && mrl_idx) return true;

  // Skip secondary directional modes if excluded by MLP pruning mask.
  if (!fsc_mode && !mlp_fallback && mode_idx >= FIRST_MODE_COUNT &&
      !mlp_mode_mask)
    return true;

  // Skip directional modes based on directional mode mask.
  if (is_directional_mode && directional_mode_skip_mask[mode] &&
      mode_idx >= FIRST_MODE_COUNT)
    return true;

  // Prune higher-order multi-line MRL candidates based on the current best
  // mode.
  if (((best_mbmi->mrl_index == 0 &&
        av2_is_directional_mode(best_mbmi->mode) == 0) ||
       (best_mbmi->mrl_index && mbmi->multi_line_mrl == 0)) &&
      mbmi->mrl_index > 1 && mbmi->multi_line_mrl)
    return true;

  return false;
}

// Restores the saved luma palette color map.
static AVM_INLINE void restore_palette_color_map(MACROBLOCKD *xd,
                                                 BLOCK_SIZE bsize,
                                                 const MB_MODE_INFO *mbmi,
                                                 const uint8_t *color_map_src) {
  if (mbmi->palette_mode_info.palette_size[PLANE_TYPE_Y] > 0) {
    int block_width, block_height;
    av2_get_block_dimensions(bsize, AVM_PLANE_Y, xd, &block_width,
                             &block_height, /*rows_within_bounds*/ NULL,
                             /*cols_within_bounds*/ NULL);
    uint8_t *color_map_dst = xd->plane[PLANE_TYPE_Y].color_index_map;
    av2_copy_array(color_map_dst, color_map_src, block_width * block_height);
  }
}

// If multi-winner mode processing is enabled, this function iterates over the
// top N candidate intra modes saved during mode evaluation and performs a
// detailed transform search(with Rate - Distortion Optimized Quantization RDOQ
// enabled) to find the best configuration. If disabled, it refines only the
// single best intra candidate mode.
static AVM_INLINE void refine_winner_intra_mode_tx(
    const AV2_COMP *const cpi, MACROBLOCK *x, BLOCK_SIZE bsize, int mode_cost,
    int64_t *best_rd_so_far, int *rate, int *rate_tokenonly,
    int64_t *distortion, int *skippable, MB_MODE_INFO *best_mbmi,
    PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];

  // If previous searches use only the default tx type/no R-D optimization of
  // quantized coeffs, do an extra search for the best tx type/better R-D
  // optimization of quantized coeffs.
  if (cpi->sf.winner_mode_sf.multi_winner_mode_type) {
    int best_mode_idx = 0;

    for (int mode_idx = 0; mode_idx < x->winner_mode_count; ++mode_idx) {
      *mbmi = x->winner_mode_stats[mode_idx].mbmi;
      if (is_winner_mode_processing_enabled(cpi, mbmi, mbmi->mode)) {
        // Restore color_map of palette mode before winner mode processing.
        restore_palette_color_map(
            xd, bsize, mbmi, x->winner_mode_stats[mode_idx].color_index_map);
        // Set params for winner mode evaluation.
        set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);

        // Winner mode processing.
        if (intra_block_yrd(cpi, x, bsize, mode_cost, best_rd_so_far, rate,
                            rate_tokenonly, distortion, skippable, best_mbmi,
                            ctx))
          best_mode_idx = mode_idx;
      }
    }
    // Copy color_map of palette mode for final winner mode.
    restore_palette_color_map(
        xd, bsize, best_mbmi,
        x->winner_mode_stats[best_mode_idx].color_index_map);
  } else {
    if (is_winner_mode_processing_enabled(cpi, mbmi, best_mbmi->mode)) {
      // Set params for winner mode evaluation.
      set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);
      *mbmi = *best_mbmi;
      intra_block_yrd(cpi, x, bsize, mode_cost, best_rd_so_far, rate,
                      rate_tokenonly, distortion, skippable, best_mbmi, ctx);
    }
  }
}

// Finds the best non-intrabc mode on an intra frame.
int64_t av2_rd_pick_intra_sby_mode(const AV2_COMP *const cpi, ThreadData *td,
                                   MACROBLOCK *x, int *rate,
                                   int *rate_tokenonly, int64_t *distortion,
                                   int *skippable, BLOCK_SIZE bsize,
                                   int64_t best_rd_so_far,
                                   PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const AV2_COMMON *const cm = &cpi->common;
  const INTRA_MODE_SPEED_FEATURES *const intra_sf = &cpi->sf.intra_sf;
  const ModeCosts *const mode_costs = &x->mode_costs;
  const bool is_lossless = xd->lossless[mbmi->segment_id];
  const bool allow_smooth_intra =
      cpi->oxcf.intra_mode_cfg.enable_smooth_intra &&
      !intra_sf->disable_smooth_intra;
  const bool allow_paeth_intra = cpi->oxcf.intra_mode_cfg.enable_paeth_intra;
  assert(!is_inter_block(mbmi, xd->tree_type));
  int64_t best_model_rd = INT64_MAX;
  mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;
  uint8_t directional_mode_skip_mask[INTRA_MODES] = { 0 };

#if CONFIG_DIP_EXT_PRUNING
  // Collect RD features for DIP ML pruning.
  DipMlInfo *dip_ml_feature = &x->dip_ml_feature;
  dip_ml_feature->beat_best_rd = 0;
  dip_ml_feature->dc_mode_rd = INT64_MAX;
  dip_ml_feature->orig_best_rd = best_rd_so_far;
  dip_ml_feature->best_mode = 0;
#endif  // CONFIG_DIP_EXT_PRUNING
  // Flag to check rd of any intra mode is better than best_rd_so_far passed to
  // this function.
  int beat_best_rd = 0;
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const bool try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av2_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        mbmi->sb_type[PLANE_TYPE_Y]);
  uint8_t *best_palette_color_map =
      try_palette ? x->palette_buffer->best_palette_color_map : NULL;
  const int context = get_y_mode_idx_ctx(xd);

  mbmi->angle_delta[PLANE_TYPE_Y] = 0;
  uint8_t mlp_mode_mask[INTRA_MODES] = { 0 };
  int mlp_fallback = !intra_sf->intra_pruning_with_mlp;
  if (intra_sf->intra_pruning_with_mlp) {
    av2_intra_mlp_compute_mode_mask(cpi, x, bsize, mlp_mode_mask, &mlp_fallback,
                                    directional_mode_skip_mask);
  } else if (intra_sf->intra_pruning_with_hog) {
    prune_intra_mode_with_hog(x, bsize, intra_sf->intra_pruning_with_hog_thresh,
                              directional_mode_skip_mask);
  }
  mbmi->use_intra_dip = 0;
  pmi->palette_size[PLANE_TYPE_Y] = 0;

  mbmi->motion_mode = SIMPLE_TRANSLATION;

  // Set params for mode evaluation.
  set_mode_eval_params(cpi, x, MODE_EVAL);

  get_y_intra_mode_set(mbmi, xd);
  mbmi->is_wide_angle[PLANE_TYPE_Y][mbmi->txb_idx] = 0;
  mbmi->mapped_intra_mode[PLANE_TYPE_Y][mbmi->txb_idx] = DC_PRED;

  MB_MODE_INFO best_mbmi = *mbmi;
  av2_zero(x->winner_mode_stats);
  x->winner_mode_count = 0;
  mbmi->dpcm_mode_y = 0;

  //  Searches the intra-modes except for intrabc, palette, and filter_intra.
  const int fsc_loop_num = allow_fsc_intra(cm, bsize, mbmi) ? FSC_MODES : 1;

  for (int fsc_mode = 0; fsc_mode < fsc_loop_num; ++fsc_mode) {
    if (fsc_mode == 1 && !beat_best_rd) break;

    FscBestParams fsc_best;
    init_fsc_best_params(&fsc_best, &best_mbmi, mbmi);

    int64_t top_intra_model_rd[TOP_INTRA_MODEL_COUNT];
    for (int i = 0; i < TOP_INTRA_MODEL_COUNT; ++i) {
      top_intra_model_rd[i] = INT64_MAX;
    }
    x->prune_tx_partition = 1;
    for (int i = 0; i < TOP_TX_PART_COUNT; ++i) {
      x->top_tx_part_rd[i] = INT64_MAX;
    }

    const uint8_t enable_mrls_flag = cm->seq_params.enable_mrls;
    int mrl_loop_num;
    if (fsc_mode) {
      mrl_loop_num = (enable_mrls_flag && best_mbmi.mrl_index) ? 2 : 1;
    } else {
      mrl_loop_num = enable_mrls_flag ? MRL_LINE_NUMBER : 1;
    }

    mbmi->use_dpcm_y = 0;
    const int dpcm_loop_num = is_lossless ? 2 : 1;

    for (int dpcm_idx = 0; dpcm_idx < dpcm_loop_num; ++dpcm_idx) {
      uint8_t mrl0_dir_mode_survived[INTRA_MODES] = { 0 };
      for (int mrl_iter = 0; mrl_iter < mrl_loop_num; ++mrl_iter) {
        const int mrl_idx =
            (fsc_mode && mrl_iter > 0) ? best_mbmi.mrl_index : mrl_iter;
        mbmi->mrl_index = mrl_idx;
        const int ml_mrl_loop_num = mrl_idx ? 2 : 1;
        for (int ml_mrl_iter = 0; ml_mrl_iter < ml_mrl_loop_num;
             ++ml_mrl_iter) {
          const int ml_mrl_idx = (fsc_mode && ml_mrl_iter > 0)
                                     ? best_mbmi.multi_line_mrl
                                     : ml_mrl_iter;
          mbmi->multi_line_mrl = ml_mrl_idx;
          for (int mode_idx = INTRA_MODE_START; mode_idx < LUMA_MODE_COUNT;
               ++mode_idx) {
            mbmi->y_mode_idx = mode_idx;
            mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[mode_idx];
            mbmi->use_dpcm_y = dpcm_idx;
            // The below function changes the mbmi->mode based on the mode_idx.
            av2_set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle,
                                           mbmi);
            // During FSC search, prune secondary directional modes that differ
            // in angle delta from the best non-FSC mode.
            if (fsc_mode && (mbmi->y_mode_idx >= FIRST_MODE_COUNT &&
                             mbmi->angle_delta[PLANE_TYPE_Y] !=
                                 best_mbmi.angle_delta[PLANE_TYPE_Y])) {
              continue;
            }

            const uint8_t this_mrl0_dir_mode_survived =
                mrl0_dir_mode_survived[mbmi->mode];
            const uint8_t this_mlp_mode_mask = mlp_mode_mask[mbmi->mode];
            const bool intra_pruning_with_mlp =
                intra_sf->intra_pruning_with_mlp;

            // Check if the current mode can be skipped.
            if (skip_intra_mode(mbmi, mode_idx, mrl_idx, dpcm_idx,
                                directional_mode_skip_mask, &best_mbmi,
                                allow_smooth_intra, allow_paeth_intra,
                                this_mrl0_dir_mode_survived, this_mlp_mode_mask,
                                mlp_fallback, intra_pruning_with_mlp, fsc_mode))
              continue;

            // Set FSC mode flag and disable incompatible modes (DIP and
            // Palette) during FSC search pass.
            if (fsc_mode) {
              if (is_lossless) {
                mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 1;
              } else {
                mbmi->fsc_mode[PLANE_TYPE_Y] = 1;
              }
              mbmi->use_intra_dip = 0;
              mbmi->palette_mode_info.palette_size[PLANE_TYPE_Y] = 0;
            }

            const int this_mode_cost =
                get_intra_mode_cost_y(xd, mbmi, mode_costs, mode_idx, context,
                                      is_lossless, enable_mrls_flag, fsc_mode);

            // Calculate intra model rd for Luma.
            const int64_t this_model_rd =
                intra_model_yrd(cpi, x, bsize, this_mode_cost);
            if (prune_intra_y_mode(this_model_rd, &best_model_rd,
                                   top_intra_model_rd, x->intra_mode_prune_top,
                                   is_lossless, mbmi->use_dpcm_y))
              continue;

            const bool is_directional_mode =
                av2_is_directional_mode(mbmi->mode);
            if (!fsc_mode && intra_sf->intra_pruning_with_mlp && mrl_idx == 0 &&
                is_directional_mode)
              mrl0_dir_mode_survived[mbmi->mode] = 1;

            // Evaluate best transform size and type for Luma.
            RD_STATS this_rd_stats;
            av2_pick_uniform_tx_size_type_yrd(cpi, x, &this_rd_stats, bsize,
                                              best_rd_so_far);
            int this_rate_tokenonly = this_rd_stats.rate;
            const int64_t this_distortion = this_rd_stats.dist;
            const int this_skip_txfm = this_rd_stats.skip_txfm;

            if (this_rate_tokenonly == INT_MAX) continue;

            if (!fsc_mode &&
                (!is_lossless &&
                 block_signals_txsize(mbmi->sb_type[PLANE_TYPE_Y]))) {
              // av2_pick_uniform_tx_size_type_yrd above includes the cost of
              // the tx_size in the tokenonly rate, but for intra blocks,
              // tx_size is always coded (prediction granularity), so we account
              // for it in the full rate, not the tokenonly rate.
              this_rate_tokenonly -= tx_size_cost(x, bsize, mbmi->tx_size);
            }
            const int this_rate =
                this_rd_stats.rate +
                intra_mode_info_cost_y(cpi, x, mbmi, bsize, this_mode_cost);
            const int64_t this_rd =
                RDCOST(x->rdmult, this_rate, this_distortion);
#if CONFIG_DIP_EXT_PRUNING
            if (!fsc_mode && mbmi->mode == DC_PRED) {
              dip_ml_feature->dc_mode_rd = this_rd;
            }
#endif  // CONFIG_DIP_EXT_PRUNING
        // Collect mode stats for multiwinner mode processing.
            const int txfm_search_done = 1;
            const MV_REFERENCE_FRAME refs[2] = { NONE_FRAME, NONE_FRAME };
            store_winner_mode_stats(
                cm, x, mbmi, NULL, NULL, NULL, refs, 0, NULL, bsize, this_rd,
                cpi->sf.winner_mode_sf.multi_winner_mode_type,
                txfm_search_done);
            if (this_rd < best_rd_so_far) {
              // Save winning FSC mode parameters and transform maps when FSC
              // mode beats best RD cost.
              if (fsc_mode) {
                fsc_best.tx_size = mbmi->tx_size;
                av2_copy(fsc_best.tx_partition_type, mbmi->tx_partition_type);
                fsc_best.intra_mode = mbmi->mode;
                fsc_best.y_mode_idx = mbmi->y_mode_idx;
                fsc_best.joint_ymode = mbmi->joint_y_mode_delta_angle;
                fsc_best.mrl_idx = mbmi->mrl_index;
                fsc_best.multi_line_mrl = mbmi->multi_line_mrl;
                if (is_lossless) {
                  fsc_best.use_dpcm_y = mbmi->use_dpcm_y;
                  fsc_best.dpcm_mode_y = mbmi->dpcm_mode_y;
                }
                fsc_best.angle_delta = mbmi->angle_delta[PLANE_TYPE_Y];
                av2_copy_array(fsc_best.tx_type_map, xd->tx_type_map,
                               ctx->num_4x4_blk);
                fsc_best.best_fsc_mode = 1;
              } else {
                best_mbmi = *mbmi;
                // Setting beat_best_rd flag because current mode rd is better
                // than best_rd_so_far passed to this function.
                beat_best_rd = 1;
#if CONFIG_DIP_EXT_PRUNING
                dip_ml_feature->beat_best_rd = 1;
                dip_ml_feature->best_mode = mbmi->mode;
#endif  // CONFIG_DIP_EXT_PRUNING
                av2_copy_array(ctx->tx_type_map, xd->tx_type_map,
                               ctx->num_4x4_blk);
              }
              *rate = this_rate;
              *rate_tokenonly = this_rate_tokenonly;
              *distortion = this_distortion;
              *skippable = this_skip_txfm;
              best_rd_so_far = this_rd;
              av2_copy_array(ctx->blk_skip[AVM_PLANE_Y],
                             x->txfm_search_info.blk_skip[AVM_PLANE_Y],
                             ctx->num_4x4_blk);
            }
          }  // End of mode_idx loop.
        }  // End of ml_mrl_iter loop.
      }  // End of mrl_iter loop.
    }  // End of dpcm_idx loop.

    if (fsc_mode) {
      if (fsc_best.best_fsc_mode) {
        mbmi->fsc_mode[PLANE_TYPE_Y] = 1;
        mbmi->mode = fsc_best.intra_mode;
        mbmi->y_mode_idx = fsc_best.y_mode_idx;
        mbmi->joint_y_mode_delta_angle = fsc_best.joint_ymode;
        if (is_lossless) {
          mbmi->use_dpcm_y = fsc_best.use_dpcm_y;
          mbmi->dpcm_mode_y = fsc_best.dpcm_mode_y;
        }
        mbmi->tx_size = fsc_best.tx_size;
        av2_copy(mbmi->tx_partition_type, fsc_best.tx_partition_type);
        mbmi->mrl_index = fsc_best.mrl_idx;
        mbmi->multi_line_mrl = fsc_best.multi_line_mrl;
        mbmi->angle_delta[PLANE_TYPE_Y] = fsc_best.angle_delta;
        av2_copy_array(ctx->tx_type_map, fsc_best.tx_type_map,
                       ctx->num_4x4_blk);
        best_mbmi = *mbmi;
      } else {
        *mbmi = best_mbmi;
      }
    }
  }  // End of fsc_mode loop.

  // Searches palette.
  int mode_cost = mode_costs->y_primary_flag_cost[DC_PRED];
  mode_cost += mode_costs->y_mode_idx_costs[context][DC_PRED];
  if (try_palette) {
    av2_rd_pick_palette_intra_sby(cpi, x, bsize, mode_cost, &best_mbmi,
                                  best_palette_color_map, &best_rd_so_far,
                                  &best_model_rd, rate, rate_tokenonly,
                                  distortion, skippable, &beat_best_rd, ctx,
                                  ctx->blk_skip[AVM_PLANE_Y], ctx->tx_type_map);
  }

  // Try Intra ML prediction (within intra frame).
  const bool try_intra_dip = !intra_sf->skip_intra_dip_search &&
                             av2_intra_dip_allowed_bsize(cm, bsize);
  if (try_intra_dip) {
    if (rd_pick_intra_dip_sby(cpi, td, x, rate, rate_tokenonly, distortion,
                              skippable, bsize, mode_cost, &best_rd_so_far,
                              &best_model_rd, ctx)) {
      best_mbmi = *mbmi;
    }
  }

  // No mode is identified with less rd value than best_rd_so_far passed to this
  // function. In such cases winner mode processing is not necessary and
  // return best_rd_so_far as INT64_MAX to indicate best mode is not identified.
  if (!beat_best_rd) return INT64_MAX;

  // In multi-winner mode processing, perform tx search for few best modes
  // identified during mode evaluation. Winner mode processing uses best tx
  // configuration for tx search.
  refine_winner_intra_mode_tx(cpi, x, bsize, mode_cost, &best_rd_so_far, rate,
                              rate_tokenonly, distortion, skippable, &best_mbmi,
                              ctx);
  *mbmi = best_mbmi;
  if (mbmi->joint_y_mode_delta_angle < NON_DIRECTIONAL_MODES_COUNT)
    assert(mbmi->joint_y_mode_delta_angle == mbmi->y_mode_idx);
  av2_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
  return best_rd_so_far;
}
