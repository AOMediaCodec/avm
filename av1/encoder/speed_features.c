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

#include "av1/common/reconintra.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/speed_features.h"
#include "av1/encoder/rdopt.h"

#include "aom_dsp/aom_dsp_common.h"

#define MAX_MESH_SPEED 5  // Max speed setting for mesh motion method
// Max speed setting for tx domain evaluation
static MESH_PATTERN
    good_quality_mesh_patterns[MAX_MESH_SPEED + 1][MAX_MESH_STEP] = {
      { { 64, 8 }, { 28, 4 }, { 15, 1 }, { 7, 1 } },
      { { 64, 8 }, { 28, 4 }, { 15, 1 }, { 7, 1 } },
      { { 64, 8 }, { 28, 4 }, { 15, 1 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
      { { 64, 16 }, { 24, 8 }, { 12, 4 }, { 7, 1 } },
    };

// TODO(huisu@google.com): These settings are pretty relaxed, tune them for
// each speed setting
static MESH_PATTERN intrabc_mesh_patterns[MAX_MESH_SPEED + 1][MAX_MESH_STEP] = {
  { { 256, 1 }, { 256, 1 }, { 0, 0 }, { 0, 0 } },
  { { 256, 1 }, { 256, 1 }, { 0, 0 }, { 0, 0 } },
  { { 256, 1 }, { 256, 1 }, { 0, 0 }, { 0, 0 } },
  { { 64, 1 }, { 64, 1 }, { 0, 0 }, { 0, 0 } },
  { { 64, 4 }, { 16, 1 }, { 0, 0 }, { 0, 0 } },
  { { 64, 4 }, { 16, 1 }, { 0, 0 }, { 0, 0 } },
};

// Threshold values to be used for pruning the txfm_domain_distortion
// based on block MSE
// Index 0: Default mode evaluation, Winner mode processing is not
// applicable (Eg : IntraBc). Index 1: Mode evaluation.
// Index 2: Winner mode evaluation. Index 1 and 2 are applicable when
// enable_winner_mode_for_use_tx_domain_dist speed feature is ON
// TODO(any): Experiment the threshold logic based on variance metric
static unsigned int tx_domain_dist_thresholds[3][MODE_EVAL_TYPES] = {
  { UINT_MAX, UINT_MAX, UINT_MAX }, { 22026, 22026, 22026 }, { 0, 0, 0 }
};

// Transform domain distortion type to be used for default, mode and winner mode
// evaluation Index 0: Default mode evaluation, Winner mode processing is not
// applicable (Eg : IntraBc). Index 1: Mode evaluation. Index 2: Winner mode
// evaluation. Index 1 and 2 are applicable when
// enable_winner_mode_for_use_tx_domain_dist speed feature is ON
static unsigned int tx_domain_dist_types[3][MODE_EVAL_TYPES] = { { 0, 2, 0 },
                                                                 { 1, 2, 0 },
                                                                 { 2, 2, 0 } };

// Threshold values to be used for disabling coeff RD-optimization
// based on block MSE / qstep^2.
// TODO(any): Experiment the threshold logic based on variance metric.
// For each row, the indices are as follows.
// Index 0: Default mode evaluation, Winner mode processing is not applicable
// (Eg : IntraBc)
// Index 1: Mode evaluation.
// Index 2: Winner mode evaluation.
// Index 1 and 2 are applicable when enable_winner_mode_for_coeff_opt speed
// feature is ON
// There are 7 levels with increasing speed, mapping to vertical indices.
static unsigned int coeff_opt_dist_thresholds[7][MODE_EVAL_TYPES] = {
  { UINT_MAX, UINT_MAX, UINT_MAX },
  { 3200, 250, UINT_MAX },
  { 1728, 142, UINT_MAX },
  { 864, 142, UINT_MAX },
  { 432, 86, UINT_MAX },
  { 216, 86, UINT_MAX },
  { 216, 0, UINT_MAX }
};

static unsigned int coeff_opt_satd_thresholds[3][MODE_EVAL_TYPES] = {
  { UINT_MAX, UINT_MAX, UINT_MAX },
  { 97, 16, UINT_MAX },
  { 25, 10, UINT_MAX },
};

// Transform size to be used for default, mode and winner mode evaluation
// Index 0: Default mode evaluation, Winner mode processing is not applicable
// (Eg : IntraBc) Index 1: Mode evaluation. Index 2: Winner mode evaluation.
// Index 1 and 2 are applicable when enable_winner_mode_for_tx_size_srch speed
// feature is ON
static TX_SIZE_SEARCH_METHOD tx_size_search_methods[3][MODE_EVAL_TYPES] = {
  { USE_FULL_RD, USE_LARGESTALL, USE_FULL_RD },
  { USE_FAST_RD, USE_LARGESTALL, USE_FULL_RD },
  { USE_LARGESTALL, USE_LARGESTALL, USE_FULL_RD }
};

// Predict transform skip levels to be used for default, mode and winner mode
// evaluation. Index 0: Default mode evaluation, Winner mode processing is not
// applicable. Index 1: Mode evaluation, Index 2: Winner mode evaluation
// Values indicate the aggressiveness of skip flag prediction.
// 0 : no early skip prediction
// 1 : conservative early skip prediction using DCT_DCT
// 2 : early skip prediction based on SSE
static unsigned int predict_skip_levels[3][MODE_EVAL_TYPES] = { { 0, 0, 0 },
                                                                { 1, 1, 1 },
                                                                { 1, 2, 1 } };

// Predict DC block levels to be used for default, mode and winner mode
// evaluation. Index 0: Default mode evaluation, Winner mode processing is not
// applicable. Index 1: Mode evaluation, Index 2: Winner mode evaluation
// Values indicate the aggressiveness of skip flag prediction.
// 0 : no early DC block prediction
// 1 : Early DC block prediction based on error variance
static unsigned int predict_dc_levels[3][MODE_EVAL_TYPES] = { { 0, 0, 0 },
                                                              { 1, 1, 0 },
                                                              { 1, 1, 1 } };

// Intra only frames, golden frames (except alt ref overlays) and
// alt ref frames tend to be coded at a higher than ambient quality
static int frame_is_boosted(const AV1_COMP *cpi) {
  return frame_is_kf_gf_arf(cpi);
}

static void set_good_speed_feature_framesize_dependent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const int is_4k_or_larger = AOMMIN(cm->width, cm->height) >= 2160;
  if (cm->seq_params.enable_flex_mvres) {
    if (is_1080p_or_larger) {
      sf->hl_sf.high_precision_mv_usage = QTR_ONLY;
    }

    if (!is_1080p_or_larger) {
      sf->flexmv_sf.do_not_search_8_pel_precision = 1;
    }
    if (!is_480p_or_larger) sf->flexmv_sf.do_not_search_4_pel_precision = 1;
  }

  if (is_480p_or_larger) {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    if (is_720p_or_larger)
      sf->part_sf.auto_max_partition_based_on_simple_motion = ADAPT_PRED;
    else
      sf->part_sf.auto_max_partition_based_on_simple_motion = RELAXED_PRED;
  } else {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
  }

  if (is_4k_or_larger) {
    sf->part_sf.default_min_partition_size = BLOCK_8X8;
  }

  // TODO(huisu@google.com): train models for 720P and above.
  if (!is_720p_or_larger) {
    sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
    sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
    sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
    sf->part_sf.ml_partition_search_breakout_thresh[3] = 500;  // BLOCK_64X64
    sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;   // BLOCK_128X128
    sf->part_sf.ml_early_term_after_part_split_level = 1;
  }

  if (is_720p_or_larger) {
    // TODO(chiyotsai@google.com): make this speed feature adaptive based on
    // current block's vertical texture instead of hardcoded with resolution
    sf->mv_sf.use_downsampled_sad = 1;
  }

  if (speed >= 2) {
    if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    } else if (is_480p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;
    }

    if (!is_720p_or_larger) {
      sf->part_sf.ml_partition_search_breakout_thresh[0] = 200;  // BLOCK_8X8
      sf->part_sf.ml_partition_search_breakout_thresh[1] = 250;  // BLOCK_16X16
      sf->part_sf.ml_partition_search_breakout_thresh[2] = 300;  // BLOCK_32X32
      sf->part_sf.ml_partition_search_breakout_thresh[3] = 300;  // BLOCK_64X64
      sf->part_sf.ml_partition_search_breakout_thresh[4] = -1;  // BLOCK_128X128
    }
    sf->part_sf.ml_early_term_after_part_split_level = 2;
  }

  if (speed >= 3) {
    sf->part_sf.use_square_partition_only_threshold = BLOCK_128X128;

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 1;
    }

    sf->part_sf.ml_early_term_after_part_split_level = 0;

    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 25);
      sf->part_sf.partition_search_breakout_rate_thr = 200;
    } else {
      sf->part_sf.max_intra_bsize = BLOCK_32X32;
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 23);
      sf->part_sf.partition_search_breakout_rate_thr = 120;
    }
  }

  if (speed >= 4) {
    if (is_720p_or_larger) {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 26);
    } else {
      sf->part_sf.partition_search_breakout_dist_thr = (1 << 24);
    }

    if (is_480p_or_larger) {
      sf->tx_sf.tx_type_search.prune_tx_type_using_stats = 2;
    }
  }

  if (speed >= 5) {
    if (is_720p_or_larger) {
      sf->inter_sf.prune_warped_prob_thresh = 16;
    } else if (is_480p_or_larger) {
      sf->inter_sf.prune_warped_prob_thresh = 8;
    }
  }

  if (speed >= 6) {
    if (is_720p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = NOT_IN_USE;
    } else if (is_480p_or_larger) {
      sf->part_sf.auto_max_partition_based_on_simple_motion = DIRECT_PRED;
    }

    if (is_1080p_or_larger) {
      sf->part_sf.default_min_partition_size = BLOCK_8X8;
    }

    if (is_720p_or_larger) {
      sf->inter_sf.disable_masked_comp = 1;
    }

    // TODO(yunqing): use BLOCK_32X32 for >= 4k.
    if (is_4k_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_64X64;
    } else if (is_720p_or_larger) {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_32X32;
    } else {
      sf->part_sf.use_square_partition_only_threshold = BLOCK_16X16;
    }

    if (is_720p_or_larger) {
      sf->inter_sf.prune_ref_mv_idx_search = 2;
    } else {
      sf->inter_sf.prune_ref_mv_idx_search = 1;
    }
  }
  sf->part_sf.use_square_partition_only_threshold = BLOCK_MAX;
}

static void set_good_speed_features_framesize_independent(
    const AV1_COMP *const cpi, SPEED_FEATURES *const sf, int speed) {
  const AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  const int boosted = frame_is_boosted(cpi);
  const int is_boosted_arf2_bwd_type =
      boosted || gf_group->update_type[gf_group->index] == INTNL_ARF_UPDATE;
  const int allow_screen_content_tools =
      cm->features.allow_screen_content_tools;
  if (!cpi->oxcf.tile_cfg.enable_large_scale_tile) {
    sf->hl_sf.high_precision_mv_usage = LAST_MV_DATA;
  }

  if (cm->seq_params.enable_flex_mvres) {
    sf->flexmv_sf.terminate_early_4_pel_precision = 1;
    sf->flexmv_sf.skip_similar_ref_mv = 1;
    sf->flexmv_sf.skip_repeated_newmv_low_prec = 1;
    sf->flexmv_sf.fast_mv_refinement = 1;
    sf->flexmv_sf.fast_motion_search_low_precision = 1;
  }

  sf->inter_sf.early_terminate_jmvd_scale_factor = 1;

  // Speed 0 for all speed features that give neutral coding performance change.
  sf->gm_sf.max_ref_frames = 2;
  sf->gm_sf.prune_ref_frame_for_gm_search = boosted ? 0 : 1;
  sf->gm_sf.disable_gm_search_based_on_stats = 1;

  sf->part_sf.less_rectangular_check_level = 1;
  sf->part_sf.simple_motion_search_prune_rect = 1;

  sf->inter_sf.disable_wedge_search_var_thresh = 0;
  // TODO(debargha): Test, tweak and turn on either 1 or 2

  sf->inter_sf.inter_mode_rd_model_estimation = 0;

  sf->inter_sf.model_based_post_interp_filter_breakout = 1;
  sf->inter_sf.prune_compound_using_single_ref = 1;
  sf->inter_sf.prune_mode_search_simple_translation = 1;
  sf->inter_sf.prune_motion_mode_level = 1;
  sf->inter_sf.prune_ref_frames = 0;
  sf->inter_sf.prune_wedge_pred_diff_based = 1;
  sf->inter_sf.reduce_inter_modes = 1;
  sf->inter_sf.selective_ref_frame = 1;
  sf->inter_sf.skip_mode_eval_based_on_rate_cost = 1;
  sf->inter_sf.skip_eval_intrabc_in_inter_frame =
      cm->features.allow_screen_content_tools ? 0
      : cm->current_frame.pyramid_level < 3   ? 0
                                              : 1;

  sf->intra_sf.intra_pruning_with_hog = 1;
  sf->intra_sf.intra_pruning_with_hog_thresh = -1.2f;
  sf->intra_sf.reuse_uv_mode_rd_info = true;

  sf->tx_sf.adaptive_tx_type_search_idx = 1;
  sf->tx_sf.adaptive_tx_partition_type_search_idx = 1;
  sf->tx_sf.intra_tx_size_search_init_depth_sqr = 1;
  sf->tx_sf.model_based_prune_tx_search_level = 1;
  sf->tx_sf.prune_tx_rd_eval_sec_tx_sse = true;
  sf->tx_sf.tx_type_search.use_reduced_intra_txset = 1;
  sf->tx_sf.tx_type_search.skip_stx_search = 0;
  sf->tx_sf.tx_type_search.skip_cctx_search = 0;

  if (cpi->twopass.fr_content_type == FC_HIGHMOTION ||
      cpi->is_screen_content_type) {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 20);
  } else {
    sf->mv_sf.exhaustive_searches_thresh = (1 << 25);
  }
  sf->mv_sf.fast_motion_estimation_on_block_256 = 1;

  sf->rd_sf.perform_coeff_opt = 1;
  if (speed >= 1) {
    sf->inter_sf.selective_ref_frame = 2;

    sf->tx_sf.adaptive_tx_type_search_idx = 4;
    sf->tx_sf.adaptive_tx_partition_type_search_idx = 4;

    sf->inter_sf.prune_comp_search_by_single_result = boosted ? 2 : 1;
  }

  if (speed >= 2) {
    sf->part_sf.intra_cnn_split = 0;
    sf->part_sf.simple_motion_search_early_term_none = 1;
    // TODO(Venkat): Clean-up frame type dependency for
    // simple_motion_search_split in partition search function and set the
    // speed feature accordingly
    sf->part_sf.simple_motion_search_split = allow_screen_content_tools ? 1 : 2;

    if (cpi->twopass.fr_content_type == FC_HIGHMOTION ||
        cpi->is_screen_content_type) {
      sf->mv_sf.exhaustive_searches_thresh = (1 << 21);
    } else {
      sf->mv_sf.exhaustive_searches_thresh = (1 << 26);
    }
    sf->mv_sf.subpel_search_type = USE_4_TAPS;

    sf->inter_sf.disable_interinter_wedge_newmv_search = boosted ? 0 : 1;
    sf->inter_sf.prune_comp_type_by_comp_avg = 1;
    sf->inter_sf.prune_comp_type_by_model_rd = boosted ? 0 : 1;
    sf->inter_sf.prune_motion_mode_level = 2;
    sf->inter_sf.prune_ref_frames =
        (frame_is_intra_only(&cpi->common) || (allow_screen_content_tools))
            ? 0
            : (boosted ? 1 : 2);
    sf->inter_sf.reduce_inter_modes = boosted ? 1 : 2;
    sf->inter_sf.reuse_inter_intra_mode = 1;
    sf->inter_sf.selective_ref_frame = 2;
    sf->inter_sf.skip_repeated_newmv = 1;

    sf->interp_sf.use_interp_filter = 1;
    sf->intra_sf.prune_palette_search_level = 1;
    sf->intra_sf.skip_intra_dip_search = true;

    sf->tx_sf.adaptive_tx_type_search_idx = 4;
    sf->tx_sf.adaptive_tx_partition_type_search_idx = 4;
    sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
    sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
    sf->tx_sf.model_based_prune_tx_search_level = 0;
    sf->tx_sf.tx_type_search.ml_tx_split_thresh = 400;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_2;
    sf->tx_sf.tx_type_search.skip_tx_search = 1;
    sf->tx_sf.use_intra_txb_hash = 1;

    sf->rd_sf.disable_tcq = 1;
    sf->rd_sf.perform_coeff_opt = boosted ? 2 : 3;
    sf->rd_sf.tx_domain_dist_level = boosted ? 1 : 2;
    sf->rd_sf.tx_domain_dist_thres_level = 1;

    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL1;
    sf->lpf_sf.wienerns_refine_iters = 0;

    // TODO(any, yunqing): move this feature to speed 0.
    sf->tpl_sf.skip_alike_starting_mv = 1;
  }

  if (speed >= 3) {
    sf->hl_sf.high_precision_mv_usage = CURRENT_Q;
    sf->hl_sf.recode_loop = ALLOW_RECODE_KFARFGF;

    sf->part_sf.allow_partition_search_skip = 1;
    sf->part_sf.less_rectangular_check_level = 2;
    sf->part_sf.simple_motion_search_prune_agg = 1;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 3 : 4;

    sf->mv_sf.auto_mv_step_size = 1;
    sf->mv_sf.subpel_iters_per_step = 1;
    // adaptive_motion_search breaks encoder multi-thread tests.
    // The values in x->pred_mv[] differ for single and multi-thread cases.
    // See aomedia:1778.
    // sf->mv_sf.adaptive_motion_search = 1;
    sf->mv_sf.full_pixel_search_level = 1;
    sf->mv_sf.simple_motion_subpel_force_stop = QUARTER_PEL;
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED;
    sf->mv_sf.search_method = DIAMOND;

    sf->gm_sf.num_refinement_steps = 0;

    // TODO(chiyotsai@google.com): We can get 10% speed up if we move
    // adaptive_rd_thresh to speed 2. But currently it performs poorly on some
    // clips (e.g. 5% loss on dinner_1080p). We need to examine the sequence a
    // bit more closely to figure out why.
    sf->inter_sf.adaptive_rd_thresh = 1;
    sf->inter_sf.comp_inter_joint_search_thresh = BLOCK_SIZES_ALL;
    sf->inter_sf.disable_wedge_search_var_thresh = 100;
    sf->inter_sf.fast_interintra_wedge_search = 1;
    sf->inter_sf.prune_compound_using_neighbors = 1;
    sf->inter_sf.prune_comp_type_by_comp_avg = 2;
    sf->inter_sf.disable_sb_level_mv_cost_upd = 1;
    // TODO(yunqing): evaluate this speed feature for speed 1 & 2, and combine
    // it with cpi->sf.disable_wedge_search_var_thresh.
    sf->inter_sf.disable_wedge_interintra_search = 1;
    sf->inter_sf.disable_smooth_interintra = boosted ? 0 : 1;
    // TODO(any): Experiment with the early exit mechanism for speeds 0, 1 and 2
    // and clean-up the speed feature
    sf->inter_sf.perform_best_rd_based_gating_for_chroma = 1;
    sf->inter_sf.prune_inter_modes_based_on_tpl = boosted ? 0 : 1;
    sf->inter_sf.prune_comp_search_by_single_result = boosted ? 4 : 2;
    sf->inter_sf.selective_ref_frame = 4;
    sf->inter_sf.skip_repeated_ref_mv = 1;
    sf->inter_sf.skip_repeated_full_newmv = 1;
    // TODO(any): Set this speed feature to 2 after correcting the match
    // criteria by considering tools like OPFL, DMVR.
    sf->inter_sf.reuse_compound_type_data = 0;
    sf->inter_sf.txfm_rd_gate_level =
        boosted ? 0 : (is_boosted_arf2_bwd_type ? 1 : 2);

    sf->intra_sf.disable_smooth_intra =
        !frame_is_intra_only(&cpi->common) || (cpi->rc.frames_to_key != 1);
    sf->intra_sf.prune_palette_search_level = 2;

    sf->tpl_sf.prune_ref_frames_in_tpl = 1;
    sf->tpl_sf.skip_alike_starting_mv = 2;
    sf->tpl_sf.prune_intra_modes = 1;
    sf->tpl_sf.prune_starting_mv = 1;
    sf->tpl_sf.reduce_first_step_size = 6;
    sf->tpl_sf.subpel_force_stop = QUARTER_PEL;
    sf->tpl_sf.search_method = DIAMOND;

    sf->tx_sf.tx_type_search.skip_stx_search = 1;
    sf->tx_sf.tx_type_search.skip_cctx_search = 1;
    sf->tx_sf.adaptive_tx_type_search_idx = boosted ? 4 : 5;
    sf->tx_sf.adaptive_tx_partition_type_search_idx = boosted ? 4 : 5;
    sf->tx_sf.tx_type_search.use_skip_flag_prediction = 2;

    // TODO(any): Refactor the code related to following winner mode speed
    // features
    sf->winner_mode_sf.enable_winner_mode_for_coeff_opt = 1;
    // TODO(any): Re-enable for inter frames after fixing speed feature.
    sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = 0;
    sf->winner_mode_sf.enable_winner_mode_for_use_tx_domain_dist = 1;
    sf->winner_mode_sf.motion_mode_for_winner_cand =
        boosted                                                      ? 0
        : gf_group->update_type[gf_group->index] == INTNL_ARF_UPDATE ? 1
                                                                     : 2;

    // TODO(any): evaluate if these lpf features can be moved to speed 2.
    // For screen content, "prune_sgr_based_on_wiener = 2" cause large quality
    // loss.
    sf->lpf_sf.disable_loop_restoration_chroma = boosted ? 0 : 1;
  }

  if (speed >= 4) {
    sf->mv_sf.subpel_search_method = SUBPEL_TREE_PRUNED_MORE;

    sf->gm_sf.downsample_level = 1;

    sf->part_sf.simple_motion_search_prune_agg = 2;
    sf->part_sf.simple_motion_search_reduce_search_steps = 4;

    sf->inter_sf.alt_ref_search_fp = 1;
    sf->inter_sf.txfm_rd_gate_level = boosted ? 0 : 4;

    sf->inter_sf.prune_inter_modes_based_on_tpl = boosted ? 0 : 3;
    sf->inter_sf.prune_compound_using_neighbors = 2;
    sf->inter_sf.prune_comp_using_best_single_mode_ref = 2;
    sf->inter_sf.disable_smooth_interintra = 1;
    sf->inter_sf.disable_onesided_comp = 1;

    sf->interp_sf.cb_pred_filter_search = 1;
    sf->interp_sf.use_interp_filter = 2;

    sf->intra_sf.intra_uv_mode_mask[TX_16X16] = UV_INTRA_DC_H_V_CFL;
    sf->intra_sf.intra_uv_mode_mask[TX_32X32] = UV_INTRA_DC_H_V_CFL;
    sf->intra_sf.intra_uv_mode_mask[TX_64X64] = UV_INTRA_DC_H_V_CFL;
    sf->intra_sf.intra_y_mode_mask[TX_16X16] = INTRA_DC_H_V;
    sf->intra_sf.intra_y_mode_mask[TX_32X32] = INTRA_DC_H_V;
    sf->intra_sf.intra_y_mode_mask[TX_64X64] = INTRA_DC_H_V;
    // TODO(any): Experiment with this speed feature set to 2 for higher quality
    // presets as well
    sf->intra_sf.skip_intra_in_interframe = 2;

    sf->mv_sf.simple_motion_subpel_force_stop = HALF_PEL;

    sf->tpl_sf.prune_starting_mv = 2;
    sf->tpl_sf.subpel_force_stop = HALF_PEL;
    sf->tpl_sf.search_method = FAST_BIGDIA;

    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 1;
    sf->tx_sf.tx_type_search.fast_intra_tx_type_search = 1;
    sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_3;
    sf->tx_sf.tx_type_search.prune_tx_type_est_rd = 1;
    // TODO(any): Experiment with enabling of this speed feature as hash state
    // is reset during winner mode processing
    sf->tx_sf.use_intra_txb_hash = 0;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 3 : 5;
    sf->rd_sf.perform_coeff_opt_based_on_satd =
        is_boosted_arf2_bwd_type ? 1 : 2;
    sf->rd_sf.tx_domain_dist_thres_level = 2;

    // TODO(any): Extend multi-winner mode processing support for inter frames
    sf->winner_mode_sf.multi_winner_mode_type =
        frame_is_intra_only(&cpi->common) ? MULTI_WINNER_MODE_DEFAULT
                                          : MULTI_WINNER_MODE_OFF;
    // TODO(any): Re-enable for all frames after fixing speed feature.
    sf->winner_mode_sf.enable_winner_mode_for_tx_size_srch = 0;

    sf->lpf_sf.lpf_pick = LPF_PICK_FROM_FULL_IMAGE_NON_DUAL;
    sf->lpf_sf.cdef_pick_method = CDEF_FAST_SEARCH_LVL3;

    sf->mv_sf.reduce_search_range = 1;
  }

  if (speed >= 5) {
    sf->part_sf.simple_motion_search_prune_agg = 3;
    sf->inter_sf.disable_interinter_wedge = 1;
    sf->inter_sf.prune_inter_modes_if_skippable = 1;

    // TODO(any): Extend multi-winner mode processing support for inter frames
    sf->winner_mode_sf.multi_winner_mode_type =
        frame_is_intra_only(&cpi->common) ? MULTI_WINNER_MODE_FAST
                                          : MULTI_WINNER_MODE_OFF;

    sf->lpf_sf.disable_lr_filter = 1;

    sf->mv_sf.prune_mesh_search = 1;
    sf->mv_sf.warp_search_method = WARP_SEARCH_DIAMOND;

    sf->tpl_sf.prune_starting_mv = 3;

    sf->winner_mode_sf.dc_blk_pred_level = 1;
    sf->winner_mode_sf.tx_size_search_level = USE_LARGESTALL;
  }

  if (speed >= 6) {
    sf->hl_sf.disable_unequal_scale_refs = true;

    sf->gm_sf.downsample_level = 2;

    sf->mv_sf.simple_motion_subpel_force_stop = FULL_PEL;
    sf->mv_sf.use_bsize_dependent_search_method = 1;

    sf->tpl_sf.disable_gop_length_decision = 1;
    sf->tpl_sf.subpel_force_stop = FULL_PEL;
    sf->tpl_sf.disable_filtered_key_tpl = 1;

    sf->tx_sf.tx_type_search.winner_mode_tx_type_pruning = 2;
    sf->tx_sf.use_intra_txb_hash = 1;
    sf->tx_sf.tx_type_search.prune_tx_type_est_rd = 0;

    sf->rd_sf.perform_coeff_opt = is_boosted_arf2_bwd_type ? 4 : 6;

    sf->winner_mode_sf.dc_blk_pred_level = 2;
    sf->winner_mode_sf.multi_winner_mode_type = MULTI_WINNER_MODE_OFF;
  }

  // Intra txb hash is currently not compatible with multi-winner mode as the
  // hashes got reset during multi-winner mode processing.
  assert(IMPLIES(
      sf->winner_mode_sf.multi_winner_mode_type != MULTI_WINNER_MODE_OFF,
      !sf->tx_sf.use_intra_txb_hash));
}

static AOM_INLINE void init_hl_sf(HIGH_LEVEL_SPEED_FEATURES *hl_sf) {
  // best quality defaults
  hl_sf->frame_parameter_update = 1;
  hl_sf->recode_loop = ALLOW_RECODE;
  hl_sf->disable_unequal_scale_refs = false;
  // Recode loop tolerance %.
  hl_sf->recode_tolerance = 25;
  hl_sf->high_precision_mv_usage = CURRENT_Q;
}

static AOM_INLINE void init_tpl_sf(TPL_SPEED_FEATURES *tpl_sf) {
  tpl_sf->disable_gop_length_decision = 0;
  tpl_sf->prune_intra_modes = 0;
  tpl_sf->prune_starting_mv = 0;
  tpl_sf->reduce_first_step_size = 0;
  tpl_sf->skip_alike_starting_mv = 0;
  tpl_sf->subpel_force_stop = EIGHTH_PEL;
  tpl_sf->search_method = NSTEP;
  tpl_sf->disable_filtered_key_tpl = 0;
  tpl_sf->prune_ref_frames_in_tpl = 0;
}

static AOM_INLINE void init_gm_sf(GLOBAL_MOTION_SPEED_FEATURES *gm_sf) {
  gm_sf->max_ref_frames = INTER_REFS_PER_FRAME;
  gm_sf->prune_ref_frame_for_gm_search = 0;
  gm_sf->downsample_level = 0;
  gm_sf->num_refinement_steps = GM_MAX_REFINEMENT_STEPS;
  gm_sf->disable_gm_search_based_on_stats = 0;
}

static AOM_INLINE void init_part_sf(PARTITION_SPEED_FEATURES *part_sf) {
  part_sf->partition_search_type = SEARCH_PARTITION;
  part_sf->less_rectangular_check_level = 0;
  part_sf->use_square_partition_only_threshold = BLOCK_128X128;
  part_sf->auto_max_partition_based_on_simple_motion = NOT_IN_USE;
  part_sf->default_max_partition_size = BLOCK_LARGEST;
  part_sf->default_min_partition_size = BLOCK_4X4;
  part_sf->allow_partition_search_skip = 0;
  part_sf->max_intra_bsize = BLOCK_LARGEST;
  // This setting only takes effect when partition_search_type is set
  // to FIXED_PARTITION.
  part_sf->fixed_partition_size = BLOCK_16X16;
  // Recode loop tolerance %.
  part_sf->partition_search_breakout_dist_thr = 0;
  part_sf->partition_search_breakout_rate_thr = 0;
  part_sf->ml_early_term_after_part_split_level = 0;
  for (int i = 0; i < PARTITION_BLOCK_SIZES; ++i) {
    part_sf->ml_partition_search_breakout_thresh[i] =
        -1;  // -1 means not enabled.
  }
  part_sf->simple_motion_search_prune_agg = 0;
  part_sf->simple_motion_search_split = 0;
  part_sf->simple_motion_search_prune_rect = 0;
  part_sf->simple_motion_search_early_term_none = 0;
  part_sf->simple_motion_search_reduce_search_steps = 0;
  part_sf->intra_cnn_split = 0;
  part_sf->prune_rect_with_none_rd = 0;
  part_sf->prune_ext_part_with_part_none = 0;
  part_sf->prune_ext_part_with_part_rect = 0;
  part_sf->prune_part_4_with_partition_boundary = 0;
  part_sf->prune_part_4_horz_or_vert = 0;
  part_sf->prune_part_4_with_part_3 = 0;
  part_sf->prune_part_4b_with_part_4a = 0;
  part_sf->two_pass_partition_search = 0;
  part_sf->prune_rect_with_ml = 0;
  part_sf->end_part_search_after_consec_failures = 0;
  part_sf->ext_recur_depth_level = 0;
  part_sf->prune_rect_with_split_depth = 0;
  part_sf->prune_part_h_with_partition_boundary = 0;
#if CONFIG_ML_PART_SPLIT
  part_sf->prune_split_with_ml = 0;
  part_sf->prune_none_with_ml = 0;
  part_sf->prune_split_ml_level = -2;  // default pruning
  part_sf->prune_split_ml_level_inter = -1;
#endif  // CONFIG_ML_PART_SPLIT
}

static AOM_INLINE void init_mv_sf(MV_SPEED_FEATURES *mv_sf) {
  mv_sf->full_pixel_search_level = 0;
  mv_sf->auto_mv_step_size = 0;
  mv_sf->exhaustive_searches_thresh = 0;
  mv_sf->prune_mesh_search = 0;
  mv_sf->reduce_search_range = 0;
  mv_sf->search_method = NSTEP;
  mv_sf->simple_motion_subpel_force_stop = EIGHTH_PEL;
  mv_sf->subpel_force_stop = EIGHTH_PEL;
  mv_sf->subpel_iters_per_step = 2;
  mv_sf->subpel_search_method = SUBPEL_TREE;
  mv_sf->subpel_search_type = USE_8_TAPS;
  mv_sf->use_bsize_dependent_search_method = 0;
  mv_sf->use_fullpel_costlist = 0;
  mv_sf->use_downsampled_sad = 0;
  mv_sf->warp_search_method = WARP_SEARCH_SQUARE;
  mv_sf->warp_search_iters = 8;
  mv_sf->fast_motion_estimation_on_block_256 = 0;
}

static AOM_INLINE void init_flexmv_sf(
    FLEXMV_PRECISION_SPEED_FEATURES *flexmv_sf) {
  flexmv_sf->do_not_search_4_pel_precision = 0;
  flexmv_sf->do_not_search_8_pel_precision = 0;
  flexmv_sf->terminate_early_4_pel_precision = 0;
  flexmv_sf->skip_similar_ref_mv = 0;
  flexmv_sf->skip_repeated_newmv_low_prec = 0;
  flexmv_sf->fast_mv_refinement = 0;
  flexmv_sf->fast_motion_search_low_precision = 0;
  flexmv_sf->prune_mv_prec_using_best_mv_prec_so_far = 0;
}

static AOM_INLINE void init_inter_sf(INTER_MODE_SPEED_FEATURES *inter_sf) {
  inter_sf->comp_inter_joint_search_thresh = BLOCK_4X4;
  inter_sf->adaptive_rd_thresh = 0;
  inter_sf->model_based_post_interp_filter_breakout = 0;
  inter_sf->reduce_inter_modes = 0;
  inter_sf->alt_ref_search_fp = 0;
  inter_sf->selective_ref_frame = 0;
  inter_sf->prune_ref_frames = 0;
  inter_sf->disable_wedge_search_var_thresh = 0;
  inter_sf->fast_wedge_sign_estimate = 0;
  inter_sf->prune_wedge_pred_diff_based = 0;
  inter_sf->reuse_inter_intra_mode = 0;
  inter_sf->disable_sb_level_coeff_cost_upd = 0;
  inter_sf->disable_sb_level_mv_cost_upd = 0;
  inter_sf->prune_inter_modes_based_on_tpl = 0;
  inter_sf->prune_comp_search_by_single_result = 0;
  inter_sf->skip_repeated_ref_mv = 0;
  inter_sf->skip_repeated_newmv = 0;
  inter_sf->skip_eval_intrabc_in_inter_frame = 0;
  inter_sf->skip_repeated_full_newmv = 0;
  inter_sf->inter_mode_rd_model_estimation = 0;
  inter_sf->prune_compound_using_single_ref = 0;
  inter_sf->prune_compound_using_neighbors = 0;
  inter_sf->prune_comp_using_best_single_mode_ref = 0;
  inter_sf->disable_onesided_comp = 0;
  inter_sf->prune_mode_search_simple_translation = 0;
  inter_sf->prune_comp_type_by_comp_avg = 0;
  inter_sf->disable_interinter_wedge_newmv_search = 0;
  inter_sf->enable_interinter_diffwtd_newmv_search = 0;
  inter_sf->disable_smooth_interintra = 0;
  inter_sf->prune_motion_mode_level = 0;
  inter_sf->disable_wedge_interintra_search = 0;
  inter_sf->fast_interintra_wedge_search = 0;
  inter_sf->prune_comp_type_by_model_rd = 0;
  inter_sf->perform_best_rd_based_gating_for_chroma = 0;
  inter_sf->disable_interinter_wedge = 0;
  inter_sf->prune_ref_mv_idx_search = 0;
  inter_sf->prune_warped_prob_thresh = 0;
  inter_sf->reuse_compound_type_data = 0;
  inter_sf->txfm_rd_gate_level = 0;
  inter_sf->prune_inter_modes_if_skippable = 0;
  inter_sf->disable_masked_comp = 0;
  inter_sf->skip_mode_eval_based_on_rate_cost = 0;
  inter_sf->reuse_erp_mode_flag = 0;
  inter_sf->prune_warpmv_prob_thresh = 32;
}

static AOM_INLINE void init_interp_sf(INTERP_FILTER_SPEED_FEATURES *interp_sf) {
  interp_sf->cb_pred_filter_search = 0;
  interp_sf->use_interp_filter = 0;
}

static AOM_INLINE void init_intra_sf(INTRA_MODE_SPEED_FEATURES *intra_sf) {
  intra_sf->skip_intra_in_interframe = 1;
  intra_sf->intra_pruning_with_hog = 0;
  intra_sf->src_var_thresh_intra_skip = 1;
  intra_sf->prune_palette_search_level = 0;
  intra_sf->reuse_uv_mode_rd_info = false;
  intra_sf->skip_intra_dip_search = false;

  for (int i = 0; i < TX_SIZES; i++) {
    intra_sf->intra_y_mode_mask[i] = INTRA_ALL;
    intra_sf->intra_uv_mode_mask[i] = UV_INTRA_ALL;
  }
  intra_sf->disable_smooth_intra = 0;
}

static AOM_INLINE void init_tx_sf(TX_SPEED_FEATURES *tx_sf) {
  tx_sf->inter_tx_size_search_init_depth_sqr = 0;
  tx_sf->inter_tx_size_search_init_depth_rect = 0;
  tx_sf->intra_tx_size_search_init_depth_rect = 0;
  tx_sf->intra_tx_size_search_init_depth_sqr = 0;
  tx_sf->tx_size_search_lgr_block = 0;
  tx_sf->model_based_prune_tx_search_level = 0;
  tx_sf->tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_1;
  tx_sf->tx_type_search.ml_tx_split_thresh = 400;
  tx_sf->tx_type_search.use_skip_flag_prediction = 1;
  tx_sf->tx_type_search.use_reduced_intra_txset = 0;
  tx_sf->tx_type_search.fast_intra_tx_type_search = 0;
  tx_sf->tx_type_search.fast_inter_tx_type_search = 0;
  tx_sf->tx_type_search.skip_tx_search = 0;
  tx_sf->tx_type_search.prune_tx_type_using_stats = 0;
  tx_sf->tx_type_search.prune_tx_type_est_rd = 0;
  tx_sf->tx_type_search.winner_mode_tx_type_pruning = 0;
  tx_sf->txb_split_cap = 1;
  tx_sf->adaptive_tx_type_search_idx = 0;
  tx_sf->adaptive_tx_partition_type_search_idx = 0;
  tx_sf->use_intra_txb_hash = 0;
  tx_sf->use_inter_txb_hash = 1;
  tx_sf->refine_fast_tx_search_results = 1;
  tx_sf->prune_tx_rd_eval_sec_tx_sse = false;
  tx_sf->use_largest_tx_size_for_small_bsize = false;
  tx_sf->restrict_tx_partition_type_search = 0;
  tx_sf->prune_inter_tx_part_rd_eval = false;
  tx_sf->enable_tx_partition = true;
}

static AOM_INLINE void init_rd_sf(RD_CALC_SPEED_FEATURES *rd_sf,
                                  const AV1EncoderConfig *oxcf) {
  const int enable_trellis_quant = oxcf->algo_cfg.enable_trellis_quant;
  if (enable_trellis_quant == 3) {
    rd_sf->optimize_coefficients = !is_lossless_requested(&oxcf->rc_cfg)
                                       ? NO_ESTIMATE_YRD_TRELLIS_OPT
                                       : NO_TRELLIS_OPT;
  } else if (enable_trellis_quant == 2) {
    rd_sf->optimize_coefficients = !is_lossless_requested(&oxcf->rc_cfg)
                                       ? FINAL_PASS_TRELLIS_OPT
                                       : NO_TRELLIS_OPT;
  } else if (enable_trellis_quant == 1) {
    if (is_lossless_requested(&oxcf->rc_cfg)) {
      rd_sf->optimize_coefficients = NO_TRELLIS_OPT;
    } else {
      rd_sf->optimize_coefficients = FULL_TRELLIS_OPT;
    }
  } else if (enable_trellis_quant == 0) {
    rd_sf->optimize_coefficients = NO_TRELLIS_OPT;
  } else {
    assert(0 && "Invalid enable_trellis_quant value");
  }
  rd_sf->use_mb_rd_hash = 1;
  rd_sf->simple_model_rd_from_var = 0;
  rd_sf->disable_tcq = 0;
  rd_sf->tx_domain_dist_level = 0;
  rd_sf->tx_domain_dist_thres_level = 0;
  rd_sf->perform_coeff_opt = 0;
  rd_sf->perform_coeff_opt_based_on_satd = 0;
}

static AOM_INLINE void init_winner_mode_sf(
    WINNER_MODE_SPEED_FEATURES *winner_mode_sf) {
  winner_mode_sf->motion_mode_for_winner_cand = 0;
  // Set this at the appropriate speed levels
  winner_mode_sf->tx_size_search_level = USE_FULL_RD;
  winner_mode_sf->enable_winner_mode_for_coeff_opt = 0;
  winner_mode_sf->enable_winner_mode_for_tx_size_srch = 0;
  winner_mode_sf->enable_winner_mode_for_use_tx_domain_dist = 0;
  winner_mode_sf->multi_winner_mode_type = 0;
  winner_mode_sf->dc_blk_pred_level = 0;
}

static AOM_INLINE void init_lpf_sf(LOOP_FILTER_SPEED_FEATURES *lpf_sf) {
  lpf_sf->disable_loop_restoration_chroma = 0;
  lpf_sf->lpf_pick = LPF_PICK_FROM_FULL_IMAGE;
  lpf_sf->cdef_pick_method = CDEF_FULL_SEARCH;
  lpf_sf->disable_lr_filter = 0;
  lpf_sf->wienerns_refine_iters = 2;
}

static void av1_disable_ml_based_transform_sf(TX_SPEED_FEATURES *const tx_sf) {
  tx_sf->tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_0;
  tx_sf->tx_type_search.ml_tx_split_thresh = -1;
}

static void av1_disable_ml_based_partition_sf(
    PARTITION_SPEED_FEATURES *const part_sf) {
  part_sf->ml_early_term_after_part_split_level = 0;
  part_sf->auto_max_partition_based_on_simple_motion = NOT_IN_USE;
  part_sf->intra_cnn_split = 0;
  part_sf->simple_motion_search_split = 0;
  part_sf->simple_motion_search_prune_rect = 0;
  part_sf->simple_motion_search_early_term_none = 0;

  for (int i = 0; i < PARTITION_BLOCK_SIZES; ++i) {
    part_sf->ml_partition_search_breakout_thresh[i] = -1;
  }
}

static AOM_INLINE void set_erp_speed_features_framesize_dependent(
    AV1_COMP *cpi) {
  SPEED_FEATURES *const sf = &cpi->sf;
  const AV1_COMMON *const cm = &cpi->common;
#if CONFIG_ML_PART_SPLIT
  const int is_2k_or_larger = AOMMIN(cm->width, cm->height) >= 2160;
#endif  // CONFIG_ML_PART_SPLIT
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const unsigned int erp_pruning_level = cpi->oxcf.part_cfg.erp_pruning_level;
  const int is_720p_or_lesser = AOMMIN(cm->width, cm->height) <= 720;

  switch (erp_pruning_level) {
    case 6: AOM_FALLTHROUGH_INTENDED;
    case 5:
      if (is_1080p_or_larger) {
        sf->part_sf.partition_search_breakout_dist_thr = (1 << 22) + (1 << 21);
      } else {
        sf->part_sf.partition_search_breakout_dist_thr = (1 << 22);
      }
      const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
      if (is_720p_or_larger) {
        sf->part_sf.prune_rect_with_split_depth = 1;
      }
      sf->part_sf.partition_search_breakout_rate_thr = 100;
#if CONFIG_ML_PART_SPLIT
      if (is_2k_or_larger) {
        sf->part_sf.prune_split_ml_level = 1;
      } else if (is_1080p_or_larger) {
        sf->part_sf.prune_split_ml_level = 1;
      } else if (is_720p_or_larger) {
        sf->part_sf.prune_split_ml_level = 0;
        sf->part_sf.prune_none_with_ml = 0;
      } else {
        sf->part_sf.prune_split_with_ml = 1;
        sf->part_sf.prune_none_with_ml = 0;
      }
      sf->part_sf.prune_split_ml_level_inter =
          sf->part_sf.prune_none_with_ml ? -1 : 0;
#endif  // CONFIG_ML_PART_SPLIT
      AOM_FALLTHROUGH_INTENDED;
    case 4: AOM_FALLTHROUGH_INTENDED;
    case 3: AOM_FALLTHROUGH_INTENDED;
    case 2: AOM_FALLTHROUGH_INTENDED;
    case 1: AOM_FALLTHROUGH_INTENDED;
    case 0: break;
    default: assert(0 && "Invalid ERP pruning level.");
  }

  if (cpi->speed >= 1) {
    if (is_720p_or_lesser && !cm->features.allow_screen_content_tools) {
      sf->part_sf.simple_motion_search_early_term_none =
          cm->current_frame.pyramid_level > 4 ? 1 : 0;
    }
  }

  if (cpi->speed >= 2) {
    sf->part_sf.simple_motion_search_early_term_none = 1;
  }
}

void av1_set_speed_features_framesize_dependent(AV1_COMP *cpi, int speed) {
  SPEED_FEATURES *const sf = &cpi->sf;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;

  if (oxcf->mode == GOOD) {
    set_good_speed_feature_framesize_dependent(cpi, sf, speed);
  }

  // This is only used in motion vector unit test.
  if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test == 1)
    cpi->mv_search_params.find_fractional_mv_step = av1_return_max_sub_pixel_mv;
  else if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test == 2)
    cpi->mv_search_params.find_fractional_mv_step = av1_return_min_sub_pixel_mv;

  if (oxcf->part_cfg.disable_ml_partition_speed_features)
    av1_disable_ml_based_partition_sf(&sf->part_sf);

  if (oxcf->txfm_cfg.disable_ml_transform_speed_features)
    av1_disable_ml_based_transform_sf(&sf->tx_sf);

  if (oxcf->txfm_cfg.enable_tx_partition == 0) {
    sf->tx_sf.enable_tx_partition = false;
  }
}

static AOM_INLINE void set_erp_speed_features(AV1_COMP *cpi) {
  SPEED_FEATURES *const sf = &cpi->sf;
  const AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  const int boosted = frame_is_boosted(cpi);
  const int is_boosted_arf2_bwd_type =
      boosted || gf_group->update_type[gf_group->index] == INTNL_ARF_UPDATE;
  const int allow_screen_content_tools =
      cm->features.allow_screen_content_tools;
  const unsigned int erp_pruning_level = cpi->oxcf.part_cfg.erp_pruning_level;
  const size_t num_pixels = cm->width * cm->height;
  // TODO(jinzhaoipc@google.com): Make sure this is aligned with the new size
  // categories post-v6
  const bool is_1080p_or_larger = num_pixels > (1366 * 768);

  switch (erp_pruning_level) {
    case 6:
      sf->part_sf.ext_recur_depth_level = 2;
      sf->part_sf.simple_motion_search_split = 1;
      sf->part_sf.simple_motion_search_early_term_none = 1;
      AOM_FALLTHROUGH_INTENDED;
    case 5:
      sf->part_sf.prune_part_h_with_partition_boundary = true;
      sf->part_sf.adaptive_partition_search_order = true;
      // Skip tx partition search for block_sizes smaller than or equal to
      // BLOCK_16X16 on inter frames for 1080p or larger resolution
      sf->tx_sf.use_largest_tx_size_for_small_bsize = is_1080p_or_larger;
      // TODO(chiyotsai@google.com): This speed feature causes large regression
      // on b2 testset. Disable this for now until we figure out how to avoid
      // the loss.
      // sf->part_sf.end_part_search_after_consec_failures = 1;
      sf->part_sf.prune_part_4b_with_part_4a = 1;
      AOM_FALLTHROUGH_INTENDED;
    case 4:
      sf->part_sf.prune_ext_part_with_part_rect = 1;
      sf->part_sf.prune_part_4_horz_or_vert = 1;
      sf->part_sf.prune_part_4_with_part_3 = 1;
      AOM_FALLTHROUGH_INTENDED;
    case 3:
      sf->part_sf.prune_ext_part_with_part_none = 1;
      AOM_FALLTHROUGH_INTENDED;
    case 2:
      sf->inter_sf.prune_ref_frames = (boosted || (allow_screen_content_tools))
                                          ? 0
                                          : (is_boosted_arf2_bwd_type ? 1 : 2);
      AOM_FALLTHROUGH_INTENDED;
    case 1:
      sf->inter_sf.reuse_erp_mode_flag =
          (REUSE_PARTITION_MODE_FLAG | REUSE_INTERFRAME_FLAG);
      sf->part_sf.prune_rect_with_none_rd = 1;
      AOM_FALLTHROUGH_INTENDED;
    case 0: break;
    default: assert(0 && "Invalid ERP pruning level.");
  }

  if (cpi->speed >= 1) {
    // Emulate erp_pruning_level = 6.
    sf->part_sf.ext_recur_depth_level = 1;
  }

  if (cpi->speed >= 2) {
    sf->part_sf.ext_recur_depth_level = 2;
    sf->part_sf.simple_motion_search_split = 1;
    sf->part_sf.simple_motion_search_early_term_none = 1;
  }
  sf->part_sf.prune_rect_with_ml = cpi->oxcf.part_cfg.use_ml_erp_pruning & 1;
#if CONFIG_ML_PART_SPLIT
  // Don't work for the screen content
  if (!cm->features.allow_screen_content_tools) {
    sf->part_sf.prune_split_with_ml =
        !!(cpi->oxcf.part_cfg.use_ml_erp_pruning & 2);
    // Only using NONE-pruning in RA
    sf->part_sf.prune_none_with_ml =
        !!(cpi->oxcf.part_cfg.use_ml_erp_pruning & 4) &&
        cpi->oxcf.gf_cfg.lag_in_frames > 0;
    if (!sf->part_sf.prune_none_with_ml)
      sf->part_sf.prune_split_ml_level_inter = 0;
  }
#endif  // CONFIG_ML_PART_SPLIT
}

void av1_set_speed_features_framesize_independent(AV1_COMP *cpi, int speed) {
  SPEED_FEATURES *const sf = &cpi->sf;
  WinnerModeParams *const winner_mode_params = &cpi->winner_mode_params;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  int i;

  init_hl_sf(&sf->hl_sf);
  init_tpl_sf(&sf->tpl_sf);
  init_gm_sf(&sf->gm_sf);
  init_part_sf(&sf->part_sf);
  init_mv_sf(&sf->mv_sf);
  init_inter_sf(&sf->inter_sf);
  init_interp_sf(&sf->interp_sf);
  init_intra_sf(&sf->intra_sf);
  init_tx_sf(&sf->tx_sf);
  init_rd_sf(&sf->rd_sf, oxcf);
  init_winner_mode_sf(&sf->winner_mode_sf);
  init_lpf_sf(&sf->lpf_sf);
  init_flexmv_sf(&sf->flexmv_sf);

  if (oxcf->mode == GOOD)
    set_good_speed_features_framesize_independent(cpi, sf, speed);

  if (!cpi->seq_params_locked) {
    cpi->common.seq_params.enable_restoration &= !sf->lpf_sf.disable_lr_filter;

    cpi->common.seq_params.enable_masked_compound &=
        !sf->inter_sf.disable_masked_comp;

    if (sf->inter_sf.disable_wedge_interintra_search) {
      cpi->common.seq_params.seq_enabled_motion_modes &= ~(1 << INTERINTRA);
    }
    if (sf->intra_sf.skip_intra_dip_search) {
      cpi->common.seq_params.enable_intra_dip = 0;
    }
    // Disable tcq modes in sequence header when cpu-used >= 2
    if (sf->rd_sf.disable_tcq) {
      cpi->common.seq_params.enable_tcq = TCQ_DISABLE;
      cpi->common.features.tcq_mode = TCQ_DISABLE;
    }
  }

  // sf->part_sf.partition_search_breakout_dist_thr is set assuming max 64x64
  // blocks. Normalise this if the blocks are bigger.
  if (MAX_SB_SIZE_LOG2 > 6) {
    sf->part_sf.partition_search_breakout_dist_thr <<=
        2 * (MAX_SB_SIZE_LOG2 - 6);
  }

  const int mesh_speed = AOMMIN(speed, MAX_MESH_SPEED);
  for (i = 0; i < MAX_MESH_STEP; ++i) {
    sf->mv_sf.mesh_patterns[i].range =
        good_quality_mesh_patterns[mesh_speed][i].range;
    sf->mv_sf.mesh_patterns[i].interval =
        good_quality_mesh_patterns[mesh_speed][i].interval;
  }

  // Update the mesh pattern of exhaustive motion search for intraBC
  // Though intraBC mesh pattern is populated for all frame types, it is used
  // only for intra frames of screen contents
  for (i = 0; i < MAX_MESH_STEP; ++i) {
    sf->mv_sf.intrabc_mesh_patterns[i].range =
        intrabc_mesh_patterns[mesh_speed][i].range;
    sf->mv_sf.intrabc_mesh_patterns[i].interval =
        intrabc_mesh_patterns[mesh_speed][i].interval;
  }

  // Slow quant, dct and trellis not worthwhile for first pass
  // so make sure they are always turned off.
  if (is_stat_generation_stage(cpi))
    sf->rd_sf.optimize_coefficients = NO_TRELLIS_OPT;

  // No recode or trellis for 1 pass.
  if (oxcf->pass == 0 && has_no_stats_stage(cpi))
    sf->hl_sf.recode_loop = DISALLOW_RECODE;

  MotionVectorSearchParams *const mv_search_params = &cpi->mv_search_params;
  if (sf->mv_sf.subpel_search_method == SUBPEL_TREE) {
    mv_search_params->find_fractional_mv_step = av1_find_best_sub_pixel_tree;
  } else if (sf->mv_sf.subpel_search_method == SUBPEL_TREE_PRUNED) {
    mv_search_params->find_fractional_mv_step =
        av1_find_best_sub_pixel_tree_pruned;
  } else if (sf->mv_sf.subpel_search_method == SUBPEL_TREE_PRUNED_MORE) {
    mv_search_params->find_fractional_mv_step =
        av1_find_best_sub_pixel_tree_pruned_more;
  } else if (sf->mv_sf.subpel_search_method == SUBPEL_TREE_PRUNED_EVENMORE) {
    mv_search_params->find_fractional_mv_step =
        av1_find_best_sub_pixel_tree_pruned_evenmore;
  }

  // This is only used in motion vector unit test.
  if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test == 1)
    mv_search_params->find_fractional_mv_step = av1_return_max_sub_pixel_mv;
  else if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test == 2)
    mv_search_params->find_fractional_mv_step = av1_return_min_sub_pixel_mv;

  // assert ensures that tx_domain_dist_level is accessed correctly
  assert(cpi->sf.rd_sf.tx_domain_dist_thres_level >= 0 &&
         cpi->sf.rd_sf.tx_domain_dist_thres_level < 3);
  memcpy(winner_mode_params->tx_domain_dist_threshold,
         tx_domain_dist_thresholds[cpi->sf.rd_sf.tx_domain_dist_thres_level],
         sizeof(winner_mode_params->tx_domain_dist_threshold));

  assert(cpi->sf.rd_sf.tx_domain_dist_level >= 0 &&
         cpi->sf.rd_sf.tx_domain_dist_level < 3);
  memcpy(winner_mode_params->use_transform_domain_distortion,
         tx_domain_dist_types[cpi->sf.rd_sf.tx_domain_dist_level],
         sizeof(winner_mode_params->use_transform_domain_distortion));

  // assert ensures that coeff_opt_dist_thresholds is accessed correctly
  assert(cpi->sf.rd_sf.perform_coeff_opt >= 0 &&
         cpi->sf.rd_sf.perform_coeff_opt < 7);
  memcpy(winner_mode_params->coeff_opt_dist_threshold,
         coeff_opt_dist_thresholds[cpi->sf.rd_sf.perform_coeff_opt],
         sizeof(winner_mode_params->coeff_opt_dist_threshold));

  // assert ensures that coeff_opt_satd_thresholds is accessed correctly
  assert(cpi->sf.rd_sf.perform_coeff_opt_based_on_satd >= 0 &&
         cpi->sf.rd_sf.perform_coeff_opt_based_on_satd < 3);
  memcpy(
      winner_mode_params->coeff_opt_satd_threshold,
      coeff_opt_satd_thresholds[cpi->sf.rd_sf.perform_coeff_opt_based_on_satd],
      sizeof(winner_mode_params->coeff_opt_satd_threshold));

  // assert ensures that predict_skip_levels is accessed correctly
  assert(cpi->sf.tx_sf.tx_type_search.use_skip_flag_prediction >= 0 &&
         cpi->sf.tx_sf.tx_type_search.use_skip_flag_prediction < 3);
  memcpy(winner_mode_params->skip_txfm_level,
         predict_skip_levels[cpi->sf.tx_sf.tx_type_search
                                 .use_skip_flag_prediction],
         sizeof(winner_mode_params->skip_txfm_level));

  // assert ensures that tx_size_search_level is accessed correctly
  assert(cpi->sf.winner_mode_sf.tx_size_search_level >= 0 &&
         cpi->sf.winner_mode_sf.tx_size_search_level < 3);
  memcpy(winner_mode_params->tx_size_search_methods,
         tx_size_search_methods[cpi->sf.winner_mode_sf.tx_size_search_level],
         sizeof(winner_mode_params->tx_size_search_methods));
  memcpy(winner_mode_params->predict_dc_level,
         predict_dc_levels[cpi->sf.winner_mode_sf.dc_blk_pred_level],
         sizeof(winner_mode_params->predict_dc_level));

  if (cpi->oxcf.row_mt == 1 && (cpi->oxcf.max_threads > 1)) {
    if (sf->inter_sf.inter_mode_rd_model_estimation == 1) {
      // Revert to type 2
      sf->inter_sf.inter_mode_rd_model_estimation = 2;
    }

    // Disable the speed feature 'prune_ref_frame_for_gm_search' to achieve
    // better parallelism when number of threads available are greater than or
    // equal to maximum number of reference frames allowed for global motion.
    if (sf->gm_sf.max_ref_frames > 0 &&
        cpi->oxcf.max_threads >= sf->gm_sf.max_ref_frames)
      sf->gm_sf.prune_ref_frame_for_gm_search = 0;
  }
}

static AOM_INLINE void set_erp_speed_features_qindex_dependent(AV1_COMP *cpi) {
  SPEED_FEATURES *const sf = &cpi->sf;
  const AV1_COMMON *const cm = &cpi->common;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;
  const unsigned int erp_pruning_level = cpi->oxcf.part_cfg.erp_pruning_level;
  const int boosted = frame_is_boosted(cpi);

  const int qindex_offset = MAXQ_OFFSET * (cm->seq_params.bit_depth - 8);
  const int qindex_thresh2 = 113 + qindex_offset;
  const int qindex_thresh3 = 135 + qindex_offset;

  switch (erp_pruning_level) {
    case 6: AOM_FALLTHROUGH_INTENDED;
    case 5:
      if (is_1080p_or_larger &&
          cm->quant_params.base_qindex <= qindex_thresh2 &&
          !frame_is_intra_only(cm)) {
        sf->part_sf.two_pass_partition_search = 1;
      }
      AOM_FALLTHROUGH_INTENDED;
    case 4: AOM_FALLTHROUGH_INTENDED;
    case 3: AOM_FALLTHROUGH_INTENDED;
    case 2: AOM_FALLTHROUGH_INTENDED;
    case 1: AOM_FALLTHROUGH_INTENDED;
    case 0: break;
    default: assert(0 && "Invalid ERP pruning level.");
  }

  if (cpi->speed == 1) {
    if (!boosted && cm->quant_params.base_qindex < qindex_thresh3) {
      sf->part_sf.simple_motion_search_split = 1;
    }
  }
}

// Override some speed features based on qindex
void av1_set_speed_features_qindex_dependent(AV1_COMP *cpi, int speed) {
  AV1_COMMON *const cm = &cpi->common;
  SPEED_FEATURES *const sf = &cpi->sf;
  WinnerModeParams *const winner_mode_params = &cpi->winner_mode_params;
  const int boosted = frame_is_boosted(cpi);
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_1080p_or_larger = AOMMIN(cm->width, cm->height) >= 1080;

  const int qindex_offset = MAXQ_OFFSET * (cm->seq_params.bit_depth - 8);

  if (cpi->oxcf.mode == GOOD && speed == 0) {
    const int qindex_thresh = 124 + qindex_offset;
    const int qindex_thresh2 = 135 + qindex_offset;
    if (cm->quant_params.base_qindex <= qindex_thresh) {
      sf->tx_sf.adaptive_tx_type_search_idx =
          (boosted || cm->features.allow_screen_content_tools) ? 1 : 2;
      sf->tx_sf.adaptive_tx_partition_type_search_idx =
          (boosted || cm->features.allow_screen_content_tools) ? 1 : 2;
    } else if (cm->quant_params.base_qindex <= qindex_thresh2) {
      sf->tx_sf.adaptive_tx_partition_type_search_idx =
          (boosted || cm->features.allow_screen_content_tools) ? 1 : 3;
      sf->tx_sf.adaptive_tx_type_search_idx =
          (boosted || cm->features.allow_screen_content_tools) ? 1 : 3;
    }
  }

  if (is_720p_or_larger && cpi->oxcf.mode == GOOD && speed <= 1) {
    const int qindex_thresh = 124 + qindex_offset;
    const int qindex_thresh2 = 113 + qindex_offset;

    if (cm->quant_params.base_qindex <= qindex_thresh) {
      sf->rd_sf.perform_coeff_opt = 2 + is_1080p_or_larger;
      memcpy(winner_mode_params->coeff_opt_dist_threshold,
             coeff_opt_dist_thresholds[sf->rd_sf.perform_coeff_opt],
             sizeof(winner_mode_params->coeff_opt_dist_threshold));
      sf->tx_sf.inter_tx_size_search_init_depth_rect = 1;
      sf->tx_sf.inter_tx_size_search_init_depth_sqr = 1;
      sf->tx_sf.intra_tx_size_search_init_depth_rect = 1;
      sf->inter_sf.skip_repeated_newmv = 1;
      sf->tx_sf.model_based_prune_tx_search_level = 0;

      if (is_1080p_or_larger &&
          cm->quant_params.base_qindex <= qindex_thresh2) {
        sf->inter_sf.selective_ref_frame = 2;
        sf->rd_sf.tx_domain_dist_level = boosted ? 1 : 2;
        sf->rd_sf.tx_domain_dist_thres_level = 1;
        sf->tx_sf.tx_type_search.ml_tx_split_thresh = 400;
        sf->interp_sf.cb_pred_filter_search = 0;
        sf->tx_sf.tx_type_search.prune_2d_txfm_mode = TX_TYPE_PRUNE_2;
        sf->tx_sf.tx_type_search.skip_tx_search = 1;
        sf->tx_sf.use_intra_txb_hash = 1;
      }
    }
  }

  if (cpi->oxcf.mode == GOOD && speed >= 0) {
    const int qindex_thresh = 135 + qindex_offset;
    const int qindex_thresh2 = 113 + qindex_offset;
    if (cpi->oxcf.gf_cfg.lag_in_frames == 0) {
      if (cm->quant_params.base_qindex <= (frame_is_intra_only(&cpi->common)
                                               ? qindex_thresh2
                                               : qindex_thresh)) {
        sf->tx_sf.restrict_tx_partition_type_search = 2;
      }
    } else {
      if (cm->quant_params.base_qindex <= qindex_thresh2) {
        sf->tx_sf.restrict_tx_partition_type_search = 1;
      }
    }

    if (cm->quant_params.base_qindex <= qindex_thresh &&
        !cm->features.allow_screen_content_tools) {
      sf->flexmv_sf.prune_mv_prec_using_best_mv_prec_so_far = boosted ? 0 : 1;
      sf->tx_sf.prune_inter_tx_part_rd_eval = true;
    }
  }

  set_erp_speed_features(cpi);
  set_erp_speed_features_framesize_dependent(cpi);
  set_erp_speed_features_qindex_dependent(cpi);
}
