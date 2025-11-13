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

#ifndef AOM_AV1_ENCODER_RDOPT_UTILS_H_
#define AOM_AV1_ENCODER_RDOPT_UTILS_H_

#include "aom/aom_integer.h"
#include "av1/encoder/block.h"
#include "av1/common/cfl.h"
#include "av1/common/pred_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_REF_MV_SEARCH (MAX_REF_MV_STACK_SIZE)
#define INTER_INTRA_RD_THRESH_SCALE 9
#define INTER_INTRA_RD_THRESH_SHIFT 4

static AOM_INLINE void restore_dst_buf(MACROBLOCKD *xd, const BUFFER_SET dst,
                                       const int num_planes) {
  for (int i = 0; i < num_planes; i++) {
    xd->plane[i].dst.buf = dst.plane[i];
    xd->plane[i].dst.stride = dst.stride[i];
  }
}

/* clang-format on */
// Calculate rd threshold based on ref best rd and relevant scaling factors
static AOM_INLINE int64_t get_rd_thresh_from_best_rd(int64_t ref_best_rd,
                                                     int mul_factor,
                                                     int div_factor) {
  int64_t rd_thresh = ref_best_rd;
  if (div_factor != 0) {
    rd_thresh = ref_best_rd < (div_factor * (INT64_MAX / mul_factor))
                    ? ((ref_best_rd / div_factor) * mul_factor)
                    : INT64_MAX;
  }
  return rd_thresh;
}

static AOM_INLINE int inter_mode_data_block_idx(BLOCK_SIZE bsize) {
  if (bsize == BLOCK_4X4 || bsize == BLOCK_4X8 || bsize == BLOCK_8X4 ||
      bsize == BLOCK_4X16 || bsize == BLOCK_16X4) {
    return -1;
  }
  return 1;
}

// Get transform block visible dimensions cropped to the MI units.
static AOM_INLINE void get_txb_dimensions(const MACROBLOCKD *xd, int plane,
                                          BLOCK_SIZE plane_bsize, int blk_row,
                                          int blk_col, BLOCK_SIZE tx_bsize,
                                          int *width, int *height,
                                          int *visible_width,
                                          int *visible_height) {
  const int txb_height = block_size_high[tx_bsize];
  const int txb_width = block_size_wide[tx_bsize];
  assert(txb_height <= block_size_high[plane_bsize]);
  assert(txb_width <= block_size_wide[plane_bsize]);
  const struct macroblockd_plane *const pd = &xd->plane[plane];

  // TODO(aconverse@google.com): Investigate using crop_width/height here rather
  // than the MI size
  if (xd->mb_to_bottom_edge >= 0) {
    *visible_height = txb_height;
  } else {
    const int block_height = block_size_high[plane_bsize];
    const int block_rows =
        (xd->mb_to_bottom_edge >> (3 + pd->subsampling_y)) + block_height;
    *visible_height =
        clamp(block_rows - (blk_row << MI_SIZE_LOG2), 0, txb_height);
  }
  if (height) *height = txb_height;

  if (xd->mb_to_right_edge >= 0) {
    *visible_width = txb_width;
  } else {
    const int block_width = block_size_wide[plane_bsize];
    const int block_cols =
        (xd->mb_to_right_edge >> (3 + pd->subsampling_x)) + block_width;
    *visible_width =
        clamp(block_cols - (blk_col << MI_SIZE_LOG2), 0, txb_width);
  }
  if (width) *width = txb_width;
}

static AOM_INLINE int get_visible_dimensions(const MACROBLOCKD *xd, int plane,
                                             int blk_col, int blk_row, int cols,
                                             int rows, int frame_width,
                                             int frame_height,
                                             int *visible_cols,
                                             int *visible_rows) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;

  const int col_start =
      plane ? xd->mi[0]->chroma_ref_info.mi_col_chroma_base : xd->mi_col;
  const int row_start =
      plane ? xd->mi[0]->chroma_ref_info.mi_row_chroma_base : xd->mi_row;
  const int x = (col_start << MI_SIZE_LOG2) >> pd->subsampling_x;
  const int y = (row_start << MI_SIZE_LOG2) >> pd->subsampling_y;

  const int mi_x = x + (blk_col << MI_SIZE_LOG2);
  const int mi_y = y + (blk_row << MI_SIZE_LOG2);
  const int plane_frame_width = frame_width >> ss_x;
  const int plane_frame_height = frame_height >> ss_y;
  int valid_cols, valid_rows;

  if (mi_x + cols <= plane_frame_width) {
    valid_cols = cols;
  } else {
    valid_cols = clamp(plane_frame_width - mi_x, 0, cols);
  }

  if (mi_y + rows <= plane_frame_height) {
    valid_rows = rows;
  } else {
    valid_rows = clamp(plane_frame_height - mi_y, 0, rows);
  }

  if (visible_cols != NULL && visible_rows != NULL) {
    *visible_cols = valid_cols;
    *visible_rows = valid_rows;
  }
  return (valid_cols < cols || valid_rows < rows);
}

static AOM_INLINE int bsize_to_num_blk(BLOCK_SIZE bsize) {
  int num_blk = 1 << (num_pels_log2_lookup[bsize] - 2 * MI_SIZE_LOG2);
  return num_blk;
}

static INLINE int check_txfm_eval(MACROBLOCK *const x, BLOCK_SIZE bsize,
                                  int64_t best_skip_rd, int64_t skip_rd,
                                  int level, int is_luma_only) {
  int eval_txfm = 1;
  // Derive aggressiveness factor for gating the transform search
  // Lower value indicates more aggressiveness. Be more conservative (high
  // value) for (i) low quantizers (ii) regions where prediction is poor
  const int scale[5] = { INT_MAX, 4, 3, 3, 2 };
  const int qslope = 2 * (!is_luma_only);
  int aggr_factor = 1;
  if (!is_luma_only) {
    aggr_factor = AOMMAX(
        1, ((MAXQ - x->qindex) * qslope + QINDEX_RANGE / 2) >> QINDEX_BITS);
  }
  if (best_skip_rd >
      (x->source_variance << (num_pels_log2_lookup[bsize] + RDDIV_BITS)))
    aggr_factor *= scale[level];
  // For level setting 1, be more conservative for luma only case even when
  // prediction is good
  else if ((level <= 1) && !is_luma_only)
    aggr_factor *= 2;

  // Be more conservative for luma only cases (called from compound type rd)
  // since best_skip_rd is computed after and skip_rd is computed (with 8-bit
  // prediction signals blended for WEDGE/DIFFWTD rather than 16-bit) before
  // interpolation filter search
  const int luma_mul[5] = { INT_MAX, 32, 29, 20, 17 };
  int mul_factor = is_luma_only ? luma_mul[level] : 16;
  int64_t rd_thresh =
      (best_skip_rd == INT64_MAX)
          ? best_skip_rd
          : (int64_t)(best_skip_rd * aggr_factor * mul_factor >> 4);
  if (skip_rd > rd_thresh) eval_txfm = 0;
  return eval_txfm;
}

static TX_MODE select_tx_mode(
    const AV1_COMMON *cm, const TX_SIZE_SEARCH_METHOD tx_size_search_method) {
  if (cm->features.coded_lossless) return ONLY_4X4;
  if (tx_size_search_method == USE_LARGESTALL) {
    return TX_MODE_LARGEST;
  } else {
    assert(tx_size_search_method == USE_FULL_RD ||
           tx_size_search_method == USE_FAST_RD);
    return TX_MODE_SELECT;
  }
}
// Checks the conditions to enable winner mode processing
static INLINE int is_winner_mode_processing_enabled(
    const struct AV1_COMP *cpi, MB_MODE_INFO *const mbmi,
    const PREDICTION_MODE best_mode) {
  const SPEED_FEATURES *sf = &cpi->sf;

  // TODO(any): Move block independent condition checks to frame level
  if (is_inter_block(mbmi, SHARED_PART)) {
    if (is_inter_mode(best_mode) &&
        sf->tx_sf.tx_type_search.fast_inter_tx_type_search &&
        !cpi->oxcf.txfm_cfg.use_inter_dct_only)
      return 1;
  } else {
    if (sf->tx_sf.tx_type_search.fast_intra_tx_type_search &&
        !cpi->oxcf.txfm_cfg.use_intra_default_tx_only &&
        !cpi->oxcf.txfm_cfg.use_intra_dct_only)
      return 1;
  }

  // Check speed feature related to winner mode processing
  if (sf->winner_mode_sf.enable_winner_mode_for_coeff_opt &&
      cpi->optimize_seg_arr[mbmi->segment_id] != NO_TRELLIS_OPT &&
      cpi->optimize_seg_arr[mbmi->segment_id] != FINAL_PASS_TRELLIS_OPT)
    return 1;
  if (sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch) return 1;

  return 0;
}

static INLINE void set_tx_size_search_method(
    const AV1_COMMON *cm, const WinnerModeParams *winner_mode_params,
    TxfmSearchParams *txfm_params, int enable_winner_mode_for_tx_size_srch,
    int is_winner_mode, const MACROBLOCK *x,
    bool use_largest_tx_size_for_small_bsize) {
  // Populate transform size search method/transform mode appropriately
  txfm_params->tx_size_search_method =
      winner_mode_params->tx_size_search_methods[DEFAULT_EVAL];
  if (enable_winner_mode_for_tx_size_srch) {
    if (is_winner_mode)
      txfm_params->tx_size_search_method =
          winner_mode_params->tx_size_search_methods[WINNER_MODE_EVAL];
    else
      txfm_params->tx_size_search_method =
          winner_mode_params->tx_size_search_methods[MODE_EVAL];
  }

  const BLOCK_SIZE bsize = x->e_mbd.mi[0]->sb_type[0];
  if (!frame_is_intra_only(cm) && x->sb_enc.min_partition_size == BLOCK_4X4 &&
      use_largest_tx_size_for_small_bsize && is_bsize_geq(BLOCK_16X16, bsize)) {
    txfm_params->tx_size_search_method = USE_LARGESTALL;
  }
  txfm_params->tx_mode_search_type =
      select_tx_mode(cm, txfm_params->tx_size_search_method);
}

static INLINE void set_tx_type_prune(const SPEED_FEATURES *sf,
                                     TxfmSearchParams *txfm_params,
                                     int winner_mode_tx_type_pruning,
                                     int is_winner_mode) {
  // Populate prune transform mode appropriately
  txfm_params->prune_2d_txfm_mode = sf->tx_sf.tx_type_search.prune_2d_txfm_mode;
  if (!winner_mode_tx_type_pruning) return;

  const int prune_mode[2][2] = { { TX_TYPE_PRUNE_4, TX_TYPE_PRUNE_0 },
                                 { TX_TYPE_PRUNE_5, TX_TYPE_PRUNE_2 } };
  txfm_params->prune_2d_txfm_mode =
      prune_mode[winner_mode_tx_type_pruning - 1][is_winner_mode];
}

static INLINE void set_tx_domain_dist_params(
    const WinnerModeParams *winner_mode_params, TxfmSearchParams *txfm_params,
    int enable_winner_mode_for_tx_domain_dist, int is_winner_mode) {
  if (!enable_winner_mode_for_tx_domain_dist) {
    txfm_params->use_transform_domain_distortion =
        winner_mode_params->use_transform_domain_distortion[DEFAULT_EVAL];
    txfm_params->tx_domain_dist_threshold =
        winner_mode_params->tx_domain_dist_threshold[DEFAULT_EVAL];
    return;
  }

  if (is_winner_mode) {
    txfm_params->use_transform_domain_distortion =
        winner_mode_params->use_transform_domain_distortion[WINNER_MODE_EVAL];
    txfm_params->tx_domain_dist_threshold =
        winner_mode_params->tx_domain_dist_threshold[WINNER_MODE_EVAL];
  } else {
    txfm_params->use_transform_domain_distortion =
        winner_mode_params->use_transform_domain_distortion[MODE_EVAL];
    txfm_params->tx_domain_dist_threshold =
        winner_mode_params->tx_domain_dist_threshold[MODE_EVAL];
  }
}

// This function sets mode parameters for different mode evaluation stages
static INLINE void set_mode_eval_params(const struct AV1_COMP *cpi,
                                        MACROBLOCK *x,
                                        MODE_EVAL_TYPE mode_eval_type) {
  const AV1_COMMON *cm = &cpi->common;
  const SPEED_FEATURES *sf = &cpi->sf;
  const WinnerModeParams *winner_mode_params = &cpi->winner_mode_params;
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  switch (mode_eval_type) {
    case DEFAULT_EVAL:
      txfm_params->use_default_inter_tx_type = 0;
      txfm_params->use_default_intra_tx_type = 0;
      txfm_params->skip_txfm_level =
          winner_mode_params->skip_txfm_level[DEFAULT_EVAL];
      txfm_params->predict_dc_level =
          winner_mode_params->predict_dc_level[DEFAULT_EVAL];
      // Set default transform domain distortion type
      set_tx_domain_dist_params(winner_mode_params, txfm_params, 0, 0);

      // Get default threshold for R-D optimization of coefficients
      txfm_params->coeff_opt_dist_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_dist_threshold, 0, 0);
      txfm_params->coeff_opt_satd_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_satd_threshold, 0, 0);

      // Set default transform size search method
      set_tx_size_search_method(cm, winner_mode_params, txfm_params, 0, 0, x,
                                sf->tx_sf.use_largest_tx_size_for_small_bsize);
      // Set default transform type prune
      set_tx_type_prune(sf, txfm_params, 0, 0);
      break;
    case MODE_EVAL:
      txfm_params->use_default_intra_tx_type =
          (cpi->sf.tx_sf.tx_type_search.fast_intra_tx_type_search ||
           cpi->oxcf.txfm_cfg.use_intra_default_tx_only);
      txfm_params->use_default_inter_tx_type =
          cpi->sf.tx_sf.tx_type_search.fast_inter_tx_type_search;
      txfm_params->skip_txfm_level =
          winner_mode_params->skip_txfm_level[MODE_EVAL];
      txfm_params->predict_dc_level =
          winner_mode_params->predict_dc_level[MODE_EVAL];
      // Set transform domain distortion type for mode evaluation
      set_tx_domain_dist_params(
          winner_mode_params, txfm_params,
          sf->winner_mode_sf.enable_winner_mode_for_use_tx_domain_dist, 0);

      // Get threshold for R-D optimization of coefficients during mode
      // evaluation
      txfm_params->coeff_opt_dist_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_dist_threshold,
          sf->winner_mode_sf.enable_winner_mode_for_coeff_opt, 0);
      txfm_params->coeff_opt_satd_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_satd_threshold,
          sf->winner_mode_sf.enable_winner_mode_for_coeff_opt, 0);

      // Set the transform size search method for mode evaluation
      set_tx_size_search_method(
          cm, winner_mode_params, txfm_params,
          sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch, 0, x,
          sf->tx_sf.use_largest_tx_size_for_small_bsize);
      // Set transform type prune for mode evaluation
      set_tx_type_prune(sf, txfm_params,
                        sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning,
                        0);
      break;
    case WINNER_MODE_EVAL:
      txfm_params->use_default_inter_tx_type = 0;
      txfm_params->use_default_intra_tx_type = 0;
      txfm_params->skip_txfm_level =
          winner_mode_params->skip_txfm_level[WINNER_MODE_EVAL];
      txfm_params->predict_dc_level =
          winner_mode_params->predict_dc_level[WINNER_MODE_EVAL];

      // Set transform domain distortion type for winner mode evaluation
      set_tx_domain_dist_params(
          winner_mode_params, txfm_params,
          sf->winner_mode_sf.enable_winner_mode_for_use_tx_domain_dist, 1);

      // Get threshold for R-D optimization of coefficients for winner mode
      // evaluation
      txfm_params->coeff_opt_dist_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_dist_threshold,
          sf->winner_mode_sf.enable_winner_mode_for_coeff_opt, 1);
      txfm_params->coeff_opt_satd_threshold = get_rd_opt_coeff_thresh(
          winner_mode_params->coeff_opt_satd_threshold,
          sf->winner_mode_sf.enable_winner_mode_for_coeff_opt, 1);

      // Set the transform size search method for winner mode evaluation
      set_tx_size_search_method(
          cm, winner_mode_params, txfm_params,
          sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch, 1, x,
          sf->tx_sf.use_largest_tx_size_for_small_bsize);
      // Set default transform type prune mode for winner mode evaluation
      set_tx_type_prune(sf, txfm_params,
                        sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning,
                        1);

      // Reset hash state for winner mode processing. Winner mode and subsequent
      // transform/mode evaluations (palette/IntraBC) cann't reuse old data as
      // the decisions would have been sub-optimal
      // TODO(any): Move the evaluation of palette/IntraBC modes before winner
      // mode is processed and clean-up the code below
      reset_hash_records(txfm_info, cpi->sf.tx_sf.use_inter_txb_hash);

      break;
    default: assert(0);
  }
}

// Similar to store_cfl_required(), but for use during the RDO process,
// where we haven't yet determined whether this block uses CfL.
static INLINE CFL_ALLOWED_TYPE store_cfl_required_rdo(const AV1_COMMON *cm,
                                                      const MACROBLOCK *x) {
  const MACROBLOCKD *xd = &x->e_mbd;

  if (cm->seq_params.monochrome || !xd->is_chroma_ref) return CFL_DISALLOWED;

  if (!xd->is_chroma_ref) {
    // CfL is available to luma partitions lesser than or equal to 32x32.
    const BLOCK_SIZE bsize = xd->mi[0]->sb_type[0];
    return (CFL_ALLOWED_TYPE)(block_size_wide[bsize] <= CFL_BUF_LINE &&
                              block_size_high[bsize] <= CFL_BUF_LINE);
  }

  // For chroma reference blocks, we should store data in the encoder iff we're
  // allowed to try out CfL.

  return is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
         is_mhccp_allowed(cm, xd);
}

static AOM_INLINE void init_sbuv_mode(MB_MODE_INFO *const mbmi) {
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->palette_mode_info.palette_size[1] = 0;
}

// Store best mode stats for winner mode processing
static INLINE void store_winner_mode_stats(
    const AV1_COMMON *const cm, MACROBLOCK *x, const MB_MODE_INFO *mbmi,
    RD_STATS *rd_cost, RD_STATS *rd_cost_y, RD_STATS *rd_cost_uv,
    const MV_REFERENCE_FRAME *refs, PREDICTION_MODE mode, uint8_t *color_map,
    BLOCK_SIZE bsize, int64_t this_rd, int multi_winner_mode_type,
    int txfm_search_done) {
  WinnerModeStats *winner_mode_stats = x->winner_mode_stats;
  int mode_idx = 0;
  int is_palette_mode = mbmi->palette_mode_info.palette_size[PLANE_TYPE_Y] > 0;
  // Mode stat is not required when multiwinner mode processing is disabled
  if (multi_winner_mode_type == MULTI_WINNER_MODE_OFF) return;
  // Ignore mode with maximum rd
  if (this_rd == INT64_MAX) return;
  // TODO(any): Winner mode processing is currently not applicable for palette
  // mode in Inter frames. Clean-up the following code, once support is added
  if (!frame_is_intra_only(cm) && is_palette_mode) return;

  int max_winner_mode_count = frame_is_intra_only(cm)
                                  ? MAX_WINNER_MODE_COUNT_INTRA
                                  : MAX_WINNER_MODE_COUNT_INTER;
  max_winner_mode_count = (multi_winner_mode_type == MULTI_WINNER_MODE_FAST)
                              ? AOMMIN(max_winner_mode_count, 2)
                              : max_winner_mode_count;
  assert(x->winner_mode_count >= 0 &&
         x->winner_mode_count <= max_winner_mode_count);

  if (x->winner_mode_count) {
    // Find the mode which has higher rd cost than this_rd
    for (mode_idx = 0; mode_idx < x->winner_mode_count; mode_idx++)
      if (winner_mode_stats[mode_idx].rd > this_rd) break;

    if (mode_idx == max_winner_mode_count) {
      // No mode has higher rd cost than this_rd
      return;
    } else if (mode_idx < max_winner_mode_count - 1) {
      // Create a slot for current mode and move others to the next slot
      memmove(
          &winner_mode_stats[mode_idx + 1], &winner_mode_stats[mode_idx],
          (max_winner_mode_count - mode_idx - 1) * sizeof(*winner_mode_stats));
    }
  }
  // Add a mode stat for winner mode processing
  winner_mode_stats[mode_idx].mbmi = *mbmi;
  winner_mode_stats[mode_idx].rd = this_rd;
  winner_mode_stats[mode_idx].mode = mode;
  winner_mode_stats[mode_idx].refs[0] = refs[0];
  winner_mode_stats[mode_idx].refs[1] = refs[1];

  // Update rd stats required for inter frame
  if (!frame_is_intra_only(cm) && rd_cost && rd_cost_y && rd_cost_uv) {
    const MACROBLOCKD *xd = &x->e_mbd;
    const int skip_ctx = av1_get_skip_txfm_context(xd);
    const int is_intra_mode = mode < INTRA_MODE_END;
    const int skip_txfm =
        mbmi->skip_txfm[xd->tree_type == CHROMA_PART] && !is_intra_mode;

    winner_mode_stats[mode_idx].rd_cost = *rd_cost;
    if (txfm_search_done) {
      winner_mode_stats[mode_idx].rate_y =
          rd_cost_y->rate +
          (!is_intra_mode
               ? x->mode_costs
                     .skip_txfm_cost[skip_ctx][rd_cost->skip_txfm || skip_txfm]
               : 0);
      winner_mode_stats[mode_idx].rate_uv = rd_cost_uv->rate;
    }
  }

  if (color_map) {
    // Store color_index_map for palette mode
    const MACROBLOCKD *const xd = &x->e_mbd;
    int block_width, block_height;
    av1_get_block_dimensions(bsize, AOM_PLANE_Y, xd, &block_width,
                             &block_height, NULL, NULL);
    memcpy(winner_mode_stats[mode_idx].color_index_map, color_map,
           block_width * block_height * sizeof(color_map[0]));
  }

  x->winner_mode_count =
      AOMMIN(x->winner_mode_count + 1, max_winner_mode_count);
}

unsigned int av1_high_get_sby_perpixel_variance(const struct AV1_COMP *cpi,
                                                const struct buf_2d *ref,
                                                BLOCK_SIZE bs, int bd);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_RDOPT_UTILS_H_
