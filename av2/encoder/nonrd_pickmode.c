/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-clause-clear/. If
 * the Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/bru.h"
#include "av2/common/cfl.h"
#include "av2/common/mvref_common.h"
#include "av2/common/pred_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/reconintra.h"
#include "av2/common/seg_common.h"
#include "av2/common/txb_common.h"

#include "av2/encoder/aq_cyclicrefresh.h"
#include "av2/encoder/encodemb.h"
#include "av2/encoder/encodemv.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/intra_mode_search.h"
#include "av2/encoder/intra_mode_search_utils.h"
#include "av2/encoder/mcomp.h"
#include "av2/encoder/model_rd.h"
#include "av2/encoder/motion_search_facade.h"
#include "av2/encoder/nonrd_opt.h"
#include "av2/encoder/palette.h"
#include "av2/encoder/rd.h"
#include "av2/encoder/rdopt.h"
#include "av2/encoder/rdopt_utils.h"
#include "av2/encoder/reconinter_enc.h"
#include "av2/encoder/tx_search.h"

#define NONRD_MAX_INTER_CANDIDATES 32

typedef struct {
  PREDICTION_MODE mode;
  MV_REFERENCE_FRAME rf0;
  MV_REFERENCE_FRAME rf1;
  uint8_t ref_mv_idx;
} NonRdInterCandidate;

static const PREDICTION_MODE kIntraModesNonRd[] = { DC_PRED, V_PRED, H_PRED,
                                                    SMOOTH_PRED };
#define NUM_INTRA_MODES_NONRD \
  ((int)(sizeof(kIntraModesNonRd) / sizeof(kIntraModesNonRd[0])))

// =============================================================================
// Section 1: Mode Info, Transform Partition, and Context Helpers
// =============================================================================

// Returns true if ranked reference rf can be used by inter modes of the frame.
static AVM_INLINE bool is_ref_usable_nonrd(const AV2_COMMON *cm, int rf) {
  return (cm->ref_frame_flags & (1 << rf)) &&
         !(cm->bru.enabled && rf == cm->bru.update_ref_idx) &&
         get_ref_frame_yv12_buf(cm, rf) != NULL;
}

static AVM_INLINE bool is_screen_content_nonrd(const AV2_COMP *cpi) {
  const AV2_COMMON *const cm = &cpi->common;
  return cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN ||
         cm->features.allow_screen_content_tools ||
         cm->features.is_scc_content_by_detector;
}

static AVM_INLINE bool is_boosted_segment_nonrd(const AV2_COMP *cpi,
                                                int segment_id) {
  return cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
         (segment_id == CR_SEGMENT_ID_BOOST1 ||
          segment_id == CR_SEGMENT_ID_BOOST2);
}

// Returns the luma AC quantizer step size in 8-bit pixel units (1x scale).
static AVM_INLINE int64_t get_luma_qstep_px_nonrd(const MACROBLOCK *x) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[AVM_PLANE_Y];
  return AVMMAX(
      ROUND_POWER_OF_TWO(p->dequant_QTX[1], QUANT_TABLE_BITS) >> (xd->bd - 5),
      1);
}

// Returns the transform partition that splits 64-length transforms into
// 32-length transforms. 64-length transforms zero out coefficients outside the
// 32x32 low-frequency corner (blurring sharp edges and text) and disallow IDTX.
static TX_PARTITION_TYPE get_tx_partition_nonrd(BLOCK_SIZE bsize) {
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
  const int txw = tx_size_wide[max_tx_size];
  const int txh = tx_size_high[max_tx_size];
  const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
  const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
  if (txw == 64 && txh == 64) {
    return (allow_horz && allow_vert) ? TX_PARTITION_SPLIT : TX_PARTITION_NONE;
  }
  if (txw == 64) return allow_vert ? TX_PARTITION_VERT : TX_PARTITION_NONE;
  if (txh == 64) return allow_horz ? TX_PARTITION_HORZ : TX_PARTITION_NONE;
  return TX_PARTITION_NONE;
}

// Selects an adaptive inter transform partition based on residual variance,
// SSE, quantizer step size, and cyclic-refresh segment boost (ported from AV1's
// calculate_tx_size() under tx_size_level_based_on_qstep):
//   - Always splits 64-length transforms into 32-length transforms.
//   - Splits 32-length transforms into 16x16 on AC-dominant residuals, boosted
//     cyclic-refresh blocks, or non-trivial screen residuals.
//   - Splits 16x16 into 8x8 on AC-dominant residuals or boosted segments.
static TX_PARTITION_TYPE calculate_inter_tx_partition_nonrd(
    const AV2_COMP *cpi, const MACROBLOCK *x, BLOCK_SIZE bsize,
    int64_t pred_sse, unsigned int pred_var) {
  const TX_PARTITION_TYPE base_part = get_tx_partition_nonrd(bsize);
  if (!cpi->sf.rt_sf.tx_size_level_based_on_qstep ||
      base_part != TX_PARTITION_NONE) {
    return base_part;
  }
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
  const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
  const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
  if (!allow_horz && !allow_vert) return TX_PARTITION_NONE;

  static const uint8_t kMult[4] = { 9, 8, 7, 5 };
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int max_q = MAXQ_FOR_GIVEN_BIT_DEPTH(xd->bd);
  const int qband = clamp((x->qindex * 4) / (max_q + 1), 0, 3);
  const int64_t qstep_px = get_luma_qstep_px_nonrd(x);
  const int64_t qstep_sq = qstep_px * qstep_px;
  const int64_t var = AVMMIN((int64_t)pred_var, pred_sse);
  const bool boosted = is_boosted_segment_nonrd(cpi, xd->mi[0]->segment_id);
  const int64_t min_ac_var = boosted ? qstep_sq : (qstep_sq << 1);
  const bool ac_dominant =
      (pred_sse <= ((var * kMult[qband]) >> 2)) && (var >= min_ac_var);
  const bool screen_detail = is_screen_content_nonrd(cpi) && (var >= qstep_sq);

  if (bsize == BLOCK_32X32) {
    if ((ac_dominant || screen_detail) && allow_horz && allow_vert) {
      return TX_PARTITION_SPLIT;
    }
  } else if (bsize == BLOCK_32X16) {
    if ((ac_dominant || screen_detail) && allow_vert) {
      return TX_PARTITION_VERT;
    }
  } else if (bsize == BLOCK_16X32) {
    if ((ac_dominant || screen_detail) && allow_horz) {
      return TX_PARTITION_HORZ;
    }
  } else if (bsize == BLOCK_16X16) {
    if (ac_dominant && allow_horz && allow_vert) {
      return TX_PARTITION_SPLIT;
    }
  }
  return TX_PARTITION_NONE;
}

// Sets the luma transform partition across all 64x64 transform units of a
// coding block (e.g. all 4 quadrants of BLOCK_128X128) and derives
// mbmi->tx_size and mbmi->sub_txs as the encoder/decoder expect.
static AVM_INLINE void set_tx_partition_nonrd(MACROBLOCKD *xd,
                                              MB_MODE_INFO *mbmi,
                                              BLOCK_SIZE bsize,
                                              TX_PARTITION_TYPE partition,
                                              TX_SIZE unsplit_tx_size) {
  for (int i = 0; i < TX_PARTITION_BUF; ++i) {
    mbmi->tx_partition_type[i] = partition;
  }
  get_tx_partition_sizes(partition, max_txsize_rect_lookup[bsize],
                         &mbmi->txb_pos, mbmi->sub_txs, xd->error_info);
  mbmi->tx_size =
      (partition == TX_PARTITION_NONE) ? unsplit_tx_size : mbmi->sub_txs[0];
  mbmi->txb_idx = 0;
}

// Counts the number of visible 64x64 transform units inside bsize for which an
// inter transform partition symbol is signaled in the bitstream.
static AVM_INLINE int count_64x64_tx_units_nonrd(const MACROBLOCKD *xd,
                                                 BLOCK_SIZE bsize) {
  const int max_blocks_wide = max_block_wide(xd, bsize, AVM_PLANE_Y);
  const int max_blocks_high = max_block_high(xd, bsize, AVM_PLANE_Y);
  const int mu_wide = mi_size_wide[BLOCK_64X64];
  const int mu_high = mi_size_high[BLOCK_64X64];
  const int units_w = (max_blocks_wide + mu_wide - 1) / mu_wide;
  const int units_h = (max_blocks_high + mu_high - 1) / mu_high;
  return AVMMAX(1, units_w * units_h);
}

static AVM_INLINE void init_mbmi_nonrd(
    MB_MODE_INFO *mbmi, PREDICTION_MODE curr_mode, MV_REFERENCE_FRAME rf0,
    MV_REFERENCE_FRAME rf1, const AV2_COMMON *cm, MACROBLOCKD *const xd) {
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
  mbmi->mode = curr_mode;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = rf0;
  mbmi->ref_frame[1] = rf1;
  pmi->palette_size[0] = 0;
  pmi->palette_size[1] = 0;
  mbmi->use_intra_dip = 0;
  mbmi->mv[0].as_int = 0;
  mbmi->mv[1].as_int = 0;
  mbmi->cwp_idx = CWP_EQUAL;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  mbmi->num_proj_ref[0] = 0;
  mbmi->num_proj_ref[1] = 0;
  for (int i = 0; i < MAX_TX_PARTITIONS; ++i) {
    mbmi->is_wide_angle[0][i] = 0;
    mbmi->is_wide_angle[1][i] = 0;
    mbmi->mapped_intra_mode[0][i] = DC_PRED;
    mbmi->mapped_intra_mode[1][i] = DC_PRED;
  }
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
  mbmi->intrabc_mode = 0;
  mbmi->intrabc_drl_idx = 0;
  mbmi->ref_bv.as_int = 0;
  mbmi->morph_pred = 0;
  mbmi->local_rest_type = 1;
  mbmi->local_ccso_blk_flag = 1;
  mbmi->local_gdf_mode = 1;

  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
  set_most_probable_mv_precision(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);

  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->six_param_warp_model_flag = 0;
  mbmi->warp_precision_idx = 0;
  mbmi->warp_inter_intra = 0;
  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
  mbmi->jmvd_scale_mode = 0;
  mbmi->comp_group_idx = 0;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->skip_mode = 0;
  mbmi->use_amvd = 0;
  mbmi->fsc_mode[0] = 0;
  mbmi->fsc_mode[1] = 0;
  mbmi->angle_delta[0] = 0;
  mbmi->angle_delta[1] = 0;
  mbmi->use_dpcm_y = 0;
  mbmi->dpcm_mode_y = 0;
  mbmi->use_dpcm_uv = 0;
  mbmi->dpcm_mode_uv = 0;
  mbmi->mrl_index = 0;
  mbmi->multi_line_mrl = 0;
  mbmi->y_mode_idx = 0;
  mbmi->joint_y_mode_delta_angle = 0;
  mbmi->uv_mode_idx = 0;
  mbmi->cfl_idx = 0;
  mbmi->cfl_alpha_idx = 0;
  mbmi->cfl_alpha_signs = 0;
  mbmi->mh_dir = 0;
  for (int i = 0; i < TX_PARTITION_BUF; ++i) {
    mbmi->tx_partition_type[i] = TX_PARTITION_NONE;
  }
  mbmi->refinemv_flag = is_inter_block(mbmi, xd->tree_type)
                            ? get_default_refinemv_flag(cm, mbmi)
                            : 0;
  set_default_interp_filters(mbmi, cm, xd, cm->features.interp_filter);
}

static void save_best_mode_context(const AV2_COMMON *cm, MACROBLOCK *x,
                                   PICK_MODE_CONTEXT *ctx,
                                   const RD_STATS *this_rdc, int this_skip,
                                   RD_STATS *best_rdc, MB_MODE_INFO *best_mbmi,
                                   int *best_skip) {
  MACROBLOCKD *const xd = &x->e_mbd;
  TxfmSearchInfo *const txfm_info = &x->txfm_search_info;
  const int num_planes = av2_num_planes(cm);

  *best_rdc = *this_rdc;
  *best_mbmi = *xd->mi[0];
  *best_skip = this_skip;

  for (int i = 0; i < num_planes; ++i) {
    const int num_blk_plane =
        (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    memcpy(ctx->blk_skip[i], txfm_info->blk_skip[i],
           sizeof(*txfm_info->blk_skip[i]) * num_blk_plane);
  }
  av2_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
  av2_copy_array(ctx->cctx_type_map, xd->cctx_type_map,
                 ctx->num_4x4_blk_chroma);
}

// Restores the winning MB_MODE_INFO, transform maps, and reference/IntraBC
// predictor pointers into x and ctx so encode_b() can reconstruct the block.
static void restore_winning_mode_context(
    const AV2_COMMON *cm, MACROBLOCK *x, BLOCK_SIZE bsize,
    PICK_MODE_CONTEXT *ctx, RD_STATS *rd_cost, const RD_STATS *best_rdc,
    const MB_MODE_INFO *best_mbmi, int best_skip) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int num_planes = av2_num_planes(cm);

  *mbmi = *best_mbmi;
  for (int i = 0; i < num_planes; ++i) {
    const int num_blk_plane =
        (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    memcpy(x->txfm_search_info.blk_skip[i], ctx->blk_skip[i],
           sizeof(uint8_t) * num_blk_plane);
  }
  av2_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
  av2_copy_array(xd->cctx_type_map, ctx->cctx_type_map,
                 ctx->num_4x4_blk_chroma);
  x->txfm_search_info.skip_txfm = best_skip;

  if (is_intrabc_block(mbmi, xd->tree_type)) {
    // encode_b() relies on xd->plane[i].pre[0] pointing to xd->cur_buf when
    // reconstructing an IntraBC block (since get_ref_frame_yv12_buf() is NULL).
    struct buf_2d yv12_mb[MAX_MB_PLANE];
    av2_setup_pred_block(xd, yv12_mb, xd->cur_buf, NULL, NULL, num_planes);
    for (int i = 0; i < num_planes; ++i) {
      xd->plane[i].pre[0] = yv12_mb[i];
    }
    x->mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = mbmi->ref_bv;
  } else if (is_inter_block(mbmi, xd->tree_type)) {
    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    if (mbmi->mode == GLOBALMV || mbmi->mode == GLOBAL_GLOBALMV) {
      if (is_nontrans_global_motion(xd, mbmi)) {
        mbmi->interp_fltr = av2_unswitchable_filter(cm->features.interp_filter);
      }
    }
    if (is_motion_variation_allowed_bsize(bsize, xd->mi_row, xd->mi_col) &&
        !is_tip_ref_frame(mbmi->ref_frame[0]) && !mbmi->skip_mode &&
        (!has_second_ref(mbmi) ||
         is_compound_warp_causal_allowed(cm, xd, mbmi))) {
      int pts0[SAMPLES_ARRAY_SIZE], pts0_inref[SAMPLES_ARRAY_SIZE];
      mbmi->num_proj_ref[0] = av2_findSamples(cm, xd, pts0, pts0_inref, 0);
      if (has_second_ref(mbmi)) {
        int pts1[SAMPLES_ARRAY_SIZE], pts1_inref[SAMPLES_ARRAY_SIZE];
        mbmi->num_proj_ref[1] = av2_findSamples(cm, xd, pts1, pts1_inref, 1);
      }
    }
    assert(av2_check_newmv_joint_nonzero(cm, x));
    assert(check_mv_precision(cm, mbmi, x));
  }

  *rd_cost = *best_rdc;
  ctx->rd_stats = *best_rdc;
  ctx->rd_stats.skip_txfm = x->txfm_search_info.skip_txfm;
  ctx->skippable = best_skip;
  ctx->mic = *mbmi;
  if (xd->tree_type != CHROMA_PART) {
    av2_copy_mbmi_ext_to_mbmi_ext_frame(&ctx->mbmi_ext_best, x->mbmi_ext, mbmi,
                                        mbmi->skip_mode,
                                        av2_ref_frame_type(mbmi->ref_frame));
  }
  ctx->single_pred_diff = 0;
  ctx->comp_pred_diff = 0;
  ctx->hybrid_pred_diff = 0;
}

// =============================================================================
// Section 2: Reference, Motion, and Interpolation Cost/Search Helpers
// =============================================================================

static void estimate_ref_frame_costs_nonrd(
    const AV2_COMMON *cm, const MACROBLOCKD *xd, const ModeCosts *mode_costs,
    int segment_id, unsigned int *ref_costs_single,
    unsigned int (*ref_costs_comp)[MAX_COMPOUND_REF_INDEX]) {
  (void)segment_id;
  unsigned int base_cost = 0;
  const int intra_inter_ctx = av2_get_intra_inter_context(xd);
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  int is_intra_allowed = 1;
  if (mbmi->tree_type == SHARED_PART &&
      mbmi->region_type == MIXED_INTER_INTRA_REGION &&
      mbmi->chroma_ref_info.offset_started) {
    is_intra_allowed = 0;
  }
  if (is_intra_allowed) {
    ref_costs_single[INTRA_FRAME_INDEX] =
        base_cost + mode_costs->intra_inter_cost[intra_inter_ctx][0];
    base_cost += mode_costs->intra_inter_cost[intra_inter_ctx][1];
  } else {
    ref_costs_single[INTRA_FRAME_INDEX] = INT_MAX;
  }
  if (mbmi->sb_type[PLANE_TYPE_Y] == BLOCK_4X4) {
    ref_costs_single[INTRA_FRAME_INDEX] = 0;
  }

  if (is_tip_allowed(cm, xd)) {
    const int tip_ctx = get_tip_ctx(xd);
    ref_costs_single[TIP_FRAME_INDEX] =
        base_cost + mode_costs->tip_cost[tip_ctx][1];
    base_cost += mode_costs->tip_cost[tip_ctx][0];
  } else {
    ref_costs_single[TIP_FRAME_INDEX] = INT_MAX;
  }

  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    ref_costs_single[i] = base_cost;
  }

  // Determine the cost of each single ref frame.
  const int n_refs = cm->ref_frames_info.num_total_refs;
  for (int i = 0; i < n_refs; ++i) {
    if (cm->bru.enabled && i == cm->bru.update_ref_idx) {
      ref_costs_single[i] = INT_MAX;
      continue;
    }
    for (int j = 0; j <= AVMMIN(i, n_refs - 2); ++j) {
      if (cm->bru.enabled && j == cm->bru.update_ref_idx) continue;
      const avm_cdf_prob ctx = av2_get_ref_pred_context(xd, j, n_refs);
      const int bit = (i == j);
      ref_costs_single[i] += mode_costs->single_ref_cost[ctx][j][bit];
    }
  }
  for (int i = n_refs; i < INTER_REFS_PER_FRAME; ++i) {
    ref_costs_single[i] = INT_MAX;
  }

  if (ref_costs_comp == NULL) return;

  if (cm->current_frame.reference_mode != SINGLE_REFERENCE) {
    for (int i = 0; i < MAX_COMPOUND_REF_INDEX; ++i) {
      for (int j = 0; j < MAX_COMPOUND_REF_INDEX; ++j) {
        ref_costs_comp[i][j] = INT_MAX;
      }
    }
    const int use_same_ref_comp = cm->ref_frames_info.num_same_ref_compound > 0;
    for (int i = 0; i < n_refs + use_same_ref_comp - 1; ++i) {
      if (i >= RANKED_REF0_TO_PRUNE) break;
      if (cm->bru.enabled && i == cm->bru.update_ref_idx) continue;
      if (i == n_refs - 1 && i >= cm->ref_frames_info.num_same_ref_compound) {
        break;
      }
      unsigned int prev_cost = base_cost;
      for (int j = 0; j < n_refs; ++j) {
        const int implicit_ref0_bit =
            j >= RANKED_REF0_TO_PRUNE - 1 ||
            (i == j && i < cm->ref_frames_info.num_same_ref_compound &&
             i + 1 >= cm->ref_frames_info.num_same_ref_compound &&
             i >= n_refs - 2);
        const int implicit_ref0_ref1_bits =
            j >= n_refs - 2 && j >= cm->ref_frames_info.num_same_ref_compound;
        if (j <= i) {
          if (cm->bru.enabled && j == cm->bru.update_ref_idx) continue;
          const avm_cdf_prob ctx = av2_get_ref_pred_context(xd, j, n_refs);
          const int bit = (i == j);
          if (!implicit_ref0_bit && !implicit_ref0_ref1_bits) {
            prev_cost += mode_costs->comp_ref0_cost[ctx][j][bit];
          }
        }
        if (j > i ||
            (j == i && i < cm->ref_frames_info.num_same_ref_compound)) {
          ref_costs_comp[i][j] = prev_cost;
          if (j < n_refs - 1) {
            if (cm->bru.enabled &&
                (i == cm->bru.update_ref_idx || j == cm->bru.update_ref_idx)) {
              ref_costs_comp[i][j] = INT_MAX;
              ref_costs_comp[j][i] = INT_MAX;
              continue;
            }
            const avm_cdf_prob ctx = av2_get_ref_pred_context(xd, j, n_refs);
            const int bit_type =
                av2_get_compound_ref_bit_type(&cm->ref_frames_info, i, j);
            ref_costs_comp[i][j] +=
                mode_costs->comp_ref1_cost[ctx][bit_type][j][1];
            prev_cost += mode_costs->comp_ref1_cost[ctx][bit_type][j][0];
          }
        }
      }
    }
  } else {
    for (int ref0 = 0; ref0 < MAX_COMPOUND_REF_INDEX; ++ref0) {
      ref_costs_comp[ref0][ref0] = INT_MAX;
      for (int ref1 = ref0 + 1; ref1 < MAX_COMPOUND_REF_INDEX; ++ref1) {
        ref_costs_comp[ref0][ref1] = 512;
        ref_costs_comp[ref1][ref0] = 512;
      }
    }
  }
}

static int cost_inter_mode_nonrd(const AV2_COMMON *cm, MACROBLOCK *x,
                                 const MB_MODE_INFO *mbmi, int16_t mode_ctx) {
  const ModeCosts *const mode_costs = &x->mode_costs;
  const PREDICTION_MODE mode = mbmi->mode;
  MACROBLOCKD *const xd = &x->e_mbd;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  int mode_cost = 0;

  if (is_inter_compound_mode(mode)) {
    const int comp_mode_idx = opfl_get_comp_idx(mode);
    if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
        opfl_allowed_cur_refs_bsize(cm, xd, mbmi)) {
      const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
      const int opfl_ctx =
          get_optflow_context(comp_idx_to_opfl_mode[comp_mode_idx]);
      mode_cost += mode_costs->use_optflow_cost[opfl_ctx][use_optical_flow];
    }
    if (is_new_nearmv_pred_mode_disallowed(mbmi)) {
      assert(comp_mode_idx >= 0 &&
             comp_mode_idx <= INTER_COMPOUND_SAME_REFS_TYPES);
      const int signal_mode_idx =
          comp_mode_idx_to_mode_signal_idx[comp_mode_idx];
      mode_cost +=
          mode_costs
              ->inter_compound_mode_same_refs_cost[mode_ctx][signal_mode_idx];
    } else {
      const int is_joint_ctx =
          get_inter_compound_mode_is_joint_context(cm, mbmi);
      const int is_joint = is_joint_mvd_coding_mode(mode);
      mode_cost +=
          mode_costs->inter_compound_mode_is_joint_cost[is_joint_ctx][is_joint];
      if (!is_joint) {
        mode_cost +=
            mode_costs->inter_compound_mode_non_joint_type_cost[mode_ctx]
                                                               [comp_mode_idx];
      }
    }
  } else {
    assert(is_inter_singleref_mode(mode));
    if (is_tip_ref_frame(mbmi->ref_frame[0])) {
      const int tip_pred_index =
          tip_pred_mode_to_index[mode - SINGLE_INTER_MODE_START];
      mode_cost += mode_costs->tip_mode_cost[tip_pred_index];
    } else {
      if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
        const int16_t warp_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
        mode_cost += mode_costs->inter_warp_mode_cost[warp_ctx][0];
      }
      const int ctx = inter_single_mode_ctx(mode_ctx);
      mode_cost +=
          mode_costs
              ->inter_single_mode_cost[ctx][mode - SINGLE_INTER_MODE_START];
    }
  }

  if (cm->seq_params.enable_adaptive_mvd && allow_amvd_mode(mode)) {
    const int amvd_index = amvd_mode_to_index(mode);
    const int amvd_ctx = get_amvd_context(xd);
    mode_cost += mode_costs->amvd_mode_cost[amvd_index][amvd_ctx][0];
  }
  if (cm->features.enable_bawp &&
      av2_allow_bawp(cm, mbmi, xd->mi_row, xd->mi_col)) {
    mode_cost += mode_costs->bawp_flg_cost[0][0];
  }

  const uint8_t ref_frame_type = av2_ref_frame_type(mbmi->ref_frame);
  const int allowed_motion_modes = motion_mode_allowed(
      cm, xd, x->mbmi_ext->ref_mv_stack[ref_frame_type], mbmi);
  if (allowed_motion_modes & (1 << INTERINTRA)) {
    mode_cost += mode_costs->interintra_cost[size_group_lookup[bsize]][0];
  }
  if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
    mode_cost += mode_costs->warp_causal_cost[av2_get_warp_causal_ctx(xd)][0];
  }

  if (have_drl_index(mode)) {
    mode_cost += get_drl_cost(cm->features.max_drl_bits, mbmi, x->mbmi_ext, x);
  }
  if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
    MB_MODE_INFO tmp_mbmi = *mbmi;
    const int down_ctx = av2_get_pb_mv_precision_down_context(cm, xd);
    const int mpp_flag_context = av2_get_mpp_flag_context(cm, xd);
    set_precision_set(cm, xd, &tmp_mbmi, bsize);
    set_most_probable_mv_precision(cm, &tmp_mbmi, bsize);
    const int mpp_flag =
        (tmp_mbmi.pb_mv_precision == tmp_mbmi.most_probable_pb_mv_precision);
    mode_cost +=
        mode_costs->pb_block_mv_mpp_flag_costs[mpp_flag_context][mpp_flag];
    if (!mpp_flag) {
      const int down = av2_get_pb_mv_precision_index(&tmp_mbmi);
      if (down >= 0) {
        mode_cost +=
            mode_costs
                ->pb_block_mv_precision_costs[down_ctx]
                                             [tmp_mbmi.max_mv_precision -
                                              MV_PRECISION_HALF_PEL][down];
      }
    }
  }
  if (!mbmi->skip_mode && switchable_refinemv_flag(cm, mbmi)) {
    const int refinemv_ctx = av2_get_refinemv_context(cm, xd, bsize);
    mode_cost +=
        mode_costs->refinemv_flag_cost[refinemv_ctx][mbmi->refinemv_flag];
  }
  return mode_cost;
}

static bool combined_motion_search(const AV2_COMP *cpi, MACROBLOCK *x,
                                   BLOCK_SIZE bsize, int_mv *tmp_mv,
                                   int *rate_mv) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MV ref_mv_low_prec = av2_get_ref_mv(x, 0).as_mv;
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(ref_mv_low_prec, &sub_mv_offset, mbmi->pb_mv_precision);
  if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL) {
    lower_mv_precision(&ref_mv_low_prec, mbmi->pb_mv_precision);
  }
  const MV ref_mv = ref_mv_low_prec;

  int step_param;
  if (cpi->sf.mv_sf.auto_mv_step_size && cm->immediate_output_picture) {
    const int ref_frame_idx = COMPACT_INDEX0_NRS(mbmi->ref_frame[0]);
    step_param = (av2_init_search_range(x->max_mv_context[ref_frame_idx],
                                        cpi->oxcf.tool_cfg.enable_high_motion) +
                  cpi->mv_search_params.mv_step_param) /
                 2;
  } else {
    step_param = cpi->mv_search_params.mv_step_param;
  }
  FULLPEL_MV start_mv = get_fullmv_from_mv(&tmp_mv->as_mv);
  if (!av2_is_fullmv_in_range(&x->mv_limits, start_mv,
                              cm->features.fr_mv_precision)) {
    start_mv = get_fullmv_from_mv(&ref_mv);
  }

  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av2_get_scaled_ref_frame(cpi, mbmi->ref_frame[0]);
  struct buf_2d backup_yv12;
  if (scaled_ref_frame) {
    backup_yv12 = xd->plane[AVM_PLANE_Y].pre[0];
    av2_setup_pre_planes(xd, 0, scaled_ref_frame, xd->mi_row, xd->mi_col, NULL,
                         1, &mbmi->chroma_ref_info);
  }

  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  av2_make_default_fullpel_ms_params(
      &full_ms_params, cpi, x, bsize, &ref_mv, mbmi->pb_mv_precision,
      /*is_ibc_cost=*/0, cpi->mv_search_params.search_site_cfg[SS_CFG_SRC],
      /*fine_search_interval=*/0);
  if (mbmi->pb_mv_precision >= MV_PRECISION_ONE_PEL &&
      cpi->sf.mv_sf.search_method == FAST_DIAMOND) {
    av2_set_mv_search_method(&full_ms_params,
                             cpi->mv_search_params.search_site_cfg[SS_CFG_SRC],
                             FAST_DIAMOND);
  }
  full_pel_lower_mv_precision(&start_mv, mbmi->pb_mv_precision);

  FULLPEL_MV best_full_mv;
  int cost_list[5];
  const int var = av2_full_pixel_search(start_mv, &full_ms_params, step_param,
                                        cond_cost_list(cpi, cost_list),
                                        &best_full_mv, NULL);
  if (scaled_ref_frame) {
    xd->plane[AVM_PLANE_Y].pre[0] = backup_yv12;
  }
  if (var == INT_MAX) {
    return false;
  }

  SUBPEL_MOTION_SEARCH_PARAMS ms_params;
  av2_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv,
                                    mbmi->pb_mv_precision, /*is_ibc_cost=*/0,
                                    cost_list);
  if (cpi->sf.rt_sf.use_nonrd_subpel_search) {
    const int qpel_min_pels =
        (cpi->sf.rt_sf.use_nonrd_subpel_search >= 2) ? (32 * 32) : (16 * 16);
    if (block_size_wide[bsize] * block_size_high[bsize] >= qpel_min_pels &&
        ms_params.forced_stop == EIGHTH_PEL) {
      ms_params.forced_stop = QUARTER_PEL;
    }
  }

  MV subpel_start_mv = get_mv_from_fullmv(&best_full_mv);
  if (cm->features.cur_frame_force_integer_mv ||
      mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL) {
    if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL) {
      lower_mv_precision(&subpel_start_mv, mbmi->pb_mv_precision);
    } else {
      subpel_start_mv.col += sub_mv_offset.col;
      subpel_start_mv.row += sub_mv_offset.row;
    }
    if (!av2_is_subpelmv_in_range(&ms_params.mv_limits, subpel_start_mv)) {
      return false;
    }
    tmp_mv->as_mv = subpel_start_mv;
  } else {
    subpel_start_mv.col += sub_mv_offset.col;
    subpel_start_mv.row += sub_mv_offset.row;
    if (!av2_is_subpelmv_in_range(&ms_params.mv_limits, subpel_start_mv)) {
      return false;
    }
    int distortion = 0;
    unsigned int sse = 0;
    cpi->mv_search_params.find_fractional_mv_step(
        xd, cm, &ms_params, subpel_start_mv, &tmp_mv->as_mv, &distortion, &sse,
        NULL);
  }

  mbmi->mv[0].as_int = tmp_mv->as_int;

  // Ensure MVD sign derivation parity when active.
  int last_sign_cost = 0;
  if (is_mvd_sign_derive_allowed(cm, xd, mbmi)) {
    MV mv_diff[2] = { kZeroMv, kZeroMv };
    MV ref_mvs[2] = { ref_mv, kZeroMv };
    int num_signaled_mvd = 0;
    int start_signaled_mvd_idx = 0;
    int num_nonzero_mvd = 0;
    const int th_for_num_nonzero = get_derive_sign_nzero_th(mbmi);
    if (need_mv_adjustment(xd, cm, x, mbmi, bsize, mv_diff, ref_mvs,
                           mbmi->pb_mv_precision, &num_signaled_mvd,
                           &start_signaled_mvd_idx, &num_nonzero_mvd)) {
      const int step =
          1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision);
      const MV orig_mv = mbmi->mv[0].as_mv;
      bool adjusted = false;
      const int deltas[4][2] = {
        { 0, mv_diff[0].col >= 0 ? step : -step },
        { mv_diff[0].row >= 0 ? step : -step, 0 },
        { 0, mv_diff[0].col >= 0 ? -step : step },
        { mv_diff[0].row >= 0 ? -step : step, 0 },
      };
      for (int d = 0; d < 4; ++d) {
        MV cand_mv = { orig_mv.row + deltas[d][0], orig_mv.col + deltas[d][1] };
        if (!av2_is_subpelmv_in_range(&ms_params.mv_limits, cand_mv)) continue;
        mbmi->mv[0].as_mv = cand_mv;
        if (mbmi->mv[0].as_mv.row == ref_mv.row &&
            mbmi->mv[0].as_mv.col == ref_mv.col)
          continue;
        if (!need_mv_adjustment(xd, cm, x, mbmi, bsize, mv_diff, ref_mvs,
                                mbmi->pb_mv_precision, &num_signaled_mvd,
                                &start_signaled_mvd_idx, &num_nonzero_mvd)) {
          adjusted = true;
          break;
        }
      }
      if (!adjusted) return false;
      tmp_mv->as_int = mbmi->mv[0].as_int;
    }
    if (num_nonzero_mvd >= th_for_num_nonzero) {
      int last_sign = -1;
      int last_comp = -1;
      if (mv_diff[0].col != 0) {
        last_sign = mv_diff[0].col < 0;
        last_comp = 1;
      } else if (mv_diff[0].row != 0) {
        last_sign = mv_diff[0].row < 0;
        last_comp = 0;
      }
      if (last_comp != -1) {
        last_sign_cost =
            av2_mv_sign_cost(last_sign, last_comp, &x->mv_costs, MV_COST_WEIGHT,
                             7, enable_adaptive_mvd_resolution(cm, mbmi));
      }
    }
  }

  if (!av2_check_newmv_joint_nonzero(cm, x) ||
      !check_mv_precision(cm, mbmi, x)) {
    return false;
  }

  *rate_mv = av2_mv_bit_cost(&tmp_mv->as_mv, &ref_mv, mbmi->pb_mv_precision,
                             &x->mv_costs, MV_COST_WEIGHT,
                             enable_adaptive_mvd_resolution(cm, mbmi)) -
             last_sign_cost;
  if (*rate_mv < 0) *rate_mv = 0;
  return true;
}

static void search_filter_ref_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                                    RD_STATS *this_rdc, unsigned int *pred_var,
                                    BLOCK_SIZE bsize, uint16_t *orig_dst_buf,
                                    int orig_dst_stride) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblock_plane *const p = &x->plane[AVM_PLANE_Y];
  struct macroblockd_plane *const pd = &xd->plane[AVM_PLANE_Y];
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const InterpFilter frame_filter = cm->features.interp_filter;

  // The high bit-depth variance functions already return the SSE in the 8-bit
  // domain.
  if (is_nontrans_global_motion(xd, mbmi) &&
      (mbmi->mode == GLOBALMV || mbmi->mode == GLOBAL_GLOBALMV)) {
    mbmi->interp_fltr = av2_unswitchable_filter(frame_filter);
    av2_enc_build_inter_predictor(cm, xd, xd->mi_row, xd->mi_col, NULL, bsize,
                                  AVM_PLANE_Y, AVM_PLANE_Y, x->tip_unit_hooks);
    unsigned int sse_u = 0;
    *pred_var = cpi->fn_ptr[bsize].vf(p->src.buf, p->src.stride, pd->dst.buf,
                                      pd->dst.stride, &sse_u);
    const int64_t sse = (int64_t)sse_u;
    this_rdc->sse = sse;
    this_rdc->dist = sse << 4;
    this_rdc->rate = av2_get_switchable_rate(x, xd, frame_filter);
    return;
  }

  const bool force_multitap_sharp =
      opfl_allowed_cur_pred_mode(cm, xd, mbmi) || mbmi->refinemv_flag ||
      (cm->bru.enabled && xd->sbi->sb_active_mode != BRU_ACTIVE_SB) ||
      is_tip_ref_frame(mbmi->ref_frame[0]);
  if (force_multitap_sharp || frame_filter != SWITCHABLE ||
      !cpi->sf.rt_sf.use_nonrd_filter_search || has_second_ref(mbmi) ||
      x->source_sad_level == kZeroSad || !av2_is_interp_needed(cm, xd)) {
    if (force_multitap_sharp) {
      mbmi->interp_fltr = MULTITAP_SHARP;
    } else {
      mbmi->interp_fltr =
          (frame_filter == SWITCHABLE) ? EIGHTTAP_REGULAR : frame_filter;
    }
    av2_enc_build_inter_predictor(cm, xd, xd->mi_row, xd->mi_col, NULL, bsize,
                                  AVM_PLANE_Y, AVM_PLANE_Y, x->tip_unit_hooks);
    unsigned int sse_u = 0;
    *pred_var = cpi->fn_ptr[bsize].vf(p->src.buf, p->src.stride, pd->dst.buf,
                                      pd->dst.stride, &sse_u);
    const int64_t sse = (int64_t)sse_u;
    this_rdc->sse = sse;
    this_rdc->dist = sse << 4;
    this_rdc->rate =
        force_multitap_sharp ? 0 : av2_get_switchable_rate(x, xd, frame_filter);
    return;
  }

  static const InterpFilter kFilters[3] = { EIGHTTAP_REGULAR, EIGHTTAP_SMOOTH,
                                            MULTITAP_SHARP };
  DECLARE_ALIGNED(32, uint16_t, tmp_pred_buf[MAX_SB_SQUARE]);
  uint16_t *const best_pred_buf = pd->dst.buf;
  const int best_pred_stride = pd->dst.stride;

  InterpFilter best_fltr = EIGHTTAP_REGULAR;
  int best_rs = INT_MAX;
  int64_t best_sse = INT64_MAX;
  unsigned int best_var = 0;
  int64_t best_rd = INT64_MAX;

  for (int i = 0; i < 3; ++i) {
    mbmi->interp_fltr = kFilters[i];
    const int rs = av2_get_switchable_rate(x, xd, frame_filter);
    if (i > 0) {
      pd->dst.buf = tmp_pred_buf;
      pd->dst.stride = bw;
    }
    av2_enc_build_inter_predictor(cm, xd, xd->mi_row, xd->mi_col, NULL, bsize,
                                  AVM_PLANE_Y, AVM_PLANE_Y, x->tip_unit_hooks);
    unsigned int sse_u = 0;
    const unsigned int var_u = cpi->fn_ptr[bsize].vf(
        p->src.buf, p->src.stride, pd->dst.buf, pd->dst.stride, &sse_u);
    const int64_t sse = (int64_t)sse_u;
    int rate_q = 0;
    int64_t dist_q = 0;
    model_rd_from_sse(cpi, x, bsize, AVM_PLANE_Y, sse, bw * bh, &rate_q,
                      &dist_q);
    const int64_t rd = RDCOST(x->rdmult, rs + rate_q, dist_q);
    if (rd < best_rd) {
      best_rd = rd;
      best_fltr = kFilters[i];
      best_rs = rs;
      best_sse = sse;
      best_var = var_u;
      if (i > 0) {
        for (int r = 0; r < bh; ++r) {
          memcpy(best_pred_buf + r * best_pred_stride, tmp_pred_buf + r * bw,
                 bw * sizeof(uint16_t));
        }
      }
    }
  }

  pd->dst.buf = orig_dst_buf;
  pd->dst.stride = orig_dst_stride;
  mbmi->interp_fltr = best_fltr;
  *pred_var = best_var;
  this_rdc->sse = best_sse;
  this_rdc->dist = best_sse << 4;
  this_rdc->rate = best_rs;
}

// =============================================================================
// Section 3: Chroma and Block RD Finalization Helpers
// =============================================================================

// Models the rate and distortion of one chroma plane by splitting the
// prediction residual into its DC part, carried by one coefficient per
// transform block, and its AC part (the variance), as in libaom's
// model_rd_for_sb_uv().
static void model_rd_uv_plane_nonrd(const MACROBLOCK *x, int plane,
                                    BLOCK_SIZE plane_bsize, int64_t sse,
                                    int64_t var, int *rate, int64_t *dist) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[plane];
  const int dequant_shift = xd->bd - 5;
  const unsigned int dc_qstep = AVMMAX(
      ROUND_POWER_OF_TWO(p->dequant_QTX[0], QUANT_TABLE_BITS) >> dequant_shift,
      1);
  const unsigned int ac_qstep = AVMMAX(
      ROUND_POWER_OF_TWO(p->dequant_QTX[1], QUANT_TABLE_BITS) >> dequant_shift,
      1);
  const int num_pels_log2 = num_pels_log2_lookup[plane_bsize];
  int rate_dc = 0;
  int rate_ac = 0;
  int64_t dist_dc = 0;
  int64_t dist_ac = 0;
  av2_model_rd_from_var_lapndz(sse - var, num_pels_log2, dc_qstep, &rate_dc,
                               &dist_dc);
  av2_model_rd_from_var_lapndz(var, num_pels_log2, ac_qstep, &rate_ac,
                               &dist_ac);
  *rate = (rate_dc >> 1) + rate_ac;
  *dist = (dist_dc << 3) + (dist_ac << 4);
}

// Block-level refinement of x->color_sensitivity[] for reference rf0 (ported
// from AV1's av1_set_color_sensitivity()): when VBP marked the superblock
// undecided (sb_sens == 2), compares normalized zero-MV chroma SAD against
// normalized luma SAD to decide whether chroma needs evaluation on this block.
static void set_color_sensitivity_nonrd(
    const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
    MV_REFERENCE_FRAME rf0, int y_sad,
    const struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE],
    uint8_t out_sens[2]) {
  const AV2_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  if (rf0 < 0 || rf0 >= INTER_REFS_PER_FRAME ||
      !is_inter_ref_frame(x->vbp_ref_frame) || y_sad == INT_MAX) {
    out_sens[0] = 1;
    out_sens[1] = 1;
    return;
  }
  const bool is_vbp_ref = (rf0 == x->vbp_ref_frame);
  const uint8_t *const sb_sens =
      is_vbp_ref ? x->color_sensitivity_sb : x->color_sensitivity_sb_alt[rf0];
  const bool ref_is_scaled =
      av2_is_scaled(get_ref_scale_factors_const(cm, rf0));
  const int factor = (bsize >= BLOCK_32X32) ? 2 : 3;
  // Both x->pred_mv_sad[rf0] and cpi->fn_ptr[bs].sdf already normalize high
  // bit-depth SAD to the 8-bit domain.
  const unsigned int norm_y_sad =
      (unsigned int)AVMMAX(y_sad, 0) >> num_pels_log2_lookup[bsize];

  for (int i = 0; i < 2; ++i) {
    if (sb_sens[i] == 2) {
      const int plane = AVM_PLANE_U + i;
      const struct macroblock_plane *const p = &x->plane[plane];
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      const BLOCK_SIZE bs = get_mb_plane_block_size(
          xd, xd->mi[0], plane, pd->subsampling_x, pd->subsampling_y);
      if (bs >= BLOCK_SIZES_ALL || ref_is_scaled ||
          x->source_variance == UINT_MAX) {
        out_sens[i] = 1;
        continue;
      }
      const unsigned int uv_sad = cpi->fn_ptr[bs].sdf(
          p->src.buf, p->src.stride, yv12_mb[rf0][plane].buf,
          yv12_mb[rf0][plane].stride);
      const unsigned int norm_uv_sad = uv_sad >> num_pels_log2_lookup[bs];
      const unsigned int y_thr =
          (!is_screen_content_nonrd(cpi) &&
           cpi->sf.rt_sf.use_color_sensitivity >= 2)
              ? (norm_y_sad >> 1)
              : ((norm_y_sad + (norm_y_sad >> factor)) >> 1);
      out_sens[i] =
          norm_uv_sad > y_thr || (norm_uv_sad > 16 && x->source_variance < 50);
    } else {
      out_sens[i] = sb_sens[i];
    }
  }
}

static bool block_uvrd_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                             const PICK_MODE_CONTEXT *ctx,
                             RD_STATS *this_rdc_uv, int *skippable_uv,
                             BLOCK_SIZE bsize, bool use_color_sens) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int num_planes = av2_num_planes(cm);

  av2_init_rd_stats(this_rdc_uv);
  *skippable_uv = 1;
  if (num_planes <= 1 || !xd->is_chroma_ref || xd->tree_type == LUMA_PART) {
    return true;
  }

  for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
    xd->cctx_type_map[i] = CCTX_NONE;
  }

  const bool is_inter = is_inter_block(mbmi, xd->tree_type);
  const bool is_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  const bool lossless = xd->lossless[mbmi->segment_id] != 0;
  const bool gate_by_color_sens =
      use_color_sens && is_inter && !is_intrabc && !lossless;

  if (gate_by_color_sens && !x->color_sensitivity[0] &&
      !x->color_sensitivity[1]) {
    this_rdc_uv->skip_txfm = 1;
    return true;
  }

  int total_rate = 0;
  int64_t total_dist = 0;
  int64_t total_sse = 0;

  for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
    struct macroblock_plane *const p = &x->plane[plane];
    struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs = get_mb_plane_block_size(
        xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
    if (bs >= BLOCK_SIZES_ALL) continue;

    if (gate_by_color_sens && !x->color_sensitivity[plane - AVM_PLANE_U]) {
      continue;
    }

    // Note: for IntraBC blocks, the caller has already built the U/V predictors
    // using xd->cur_buf in xd->plane[plane].pre[0].
    if (is_inter && !is_intrabc) {
      av2_enc_build_inter_predictor(cm, xd, xd->mi_row, xd->mi_col, NULL, bsize,
                                    plane, plane, x->tip_unit_hooks);
    } else if (!is_inter) {
      const TX_SIZE uv_tx_size = av2_get_tx_size(plane, xd);
      const int stepr = tx_size_high_unit[uv_tx_size];
      const int stepc = tx_size_wide_unit[uv_tx_size];
      const int max_blocks_wide = max_block_wide(xd, bs, plane);
      const int max_blocks_high = max_block_high(xd, bs, plane);
      for (int row = 0; row < max_blocks_high; row += stepr) {
        for (int col = 0; col < max_blocks_wide; col += stepc) {
          av2_predict_intra_block_facade(cm, xd, plane, col, row, uv_tx_size);
        }
      }
    }

    int plane_rate = 0;
    int64_t plane_dist = 0;
    int64_t sse = 0;
    int plane_skip = 0;

    if (lossless) {
      const int64_t raw_sse = av2_exact_plane_sse_nonrd(x, plane, bs);
      if (raw_sse == 0) {
        plane_skip = 1;
      } else {
        unsigned int sse_u = 0;
        const unsigned int var_u = cpi->fn_ptr[bs].vf(
            p->src.buf, p->src.stride, pd->dst.buf, pd->dst.stride, &sse_u);
        sse = AVMMAX((int64_t)1, ROUND_POWER_OF_TWO(raw_sse, 2 * (xd->bd - 8)));
        const int64_t var = AVMMIN((int64_t)var_u, sse);
        int64_t dist_unused = 0;
        model_rd_uv_plane_nonrd(x, plane, bs, sse, var, &plane_rate,
                                &dist_unused);
        const int min_lossless_rate =
            get_msb((unsigned int)AVMMIN(raw_sse, (int64_t)INT_MAX) + 1)
            << AV2_PROB_COST_SHIFT;
        plane_rate = AVMMAX(plane_rate, min_lossless_rate);
        plane_dist = 0;
        plane_skip = 0;
      }
    } else {
      // The high bit-depth variance functions already return the SSE and the
      // variance in the 8-bit domain.
      unsigned int sse_u = 0;
      const unsigned int var_u = cpi->fn_ptr[bs].vf(
          p->src.buf, p->src.stride, pd->dst.buf, pd->dst.stride, &sse_u);
      sse = (int64_t)sse_u;
      const int64_t var = AVMMIN((int64_t)var_u, sse);
      model_rd_uv_plane_nonrd(x, plane, bs, sse, var, &plane_rate, &plane_dist);
      plane_skip = (plane_rate == 0);
    }

    *skippable_uv = *skippable_uv && plane_skip;
    const int num_4x4_blk_uv =
        AVMMAX(mi_size_wide[bs] * mi_size_high[bs], ctx->num_4x4_blk_chroma);
    for (int i = 0; i < num_4x4_blk_uv; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[plane], i, 0);
    }

    total_rate += plane_rate;
    total_dist += plane_dist;
    total_sse += sse << 4;
  }

  // Skip the chroma residual when coding it does not pay off (as in libaom's
  // model_rd_for_sb_uv()), except in lossless mode where non-zero residuals
  // must always be coded.
  if (!lossless && !*skippable_uv &&
      RDCOST(x->rdmult, total_rate, total_dist) >=
          RDCOST(x->rdmult, 0, total_sse)) {
    *skippable_uv = 1;
    total_rate = 0;
    total_dist = total_sse;
  }

  this_rdc_uv->rate = total_rate;
  this_rdc_uv->dist = total_dist;
  this_rdc_uv->sse = total_sse;
  this_rdc_uv->skip_txfm = *skippable_uv;
  return true;
}

// Combines the luma and chroma RD estimates of an inter/IntraBC mode into the
// block RD cost, and sets the transform partition, skip flags, and transform
// type map accordingly. Returns 1 if the whole block is skipped.
static int finalize_inter_rd_nonrd(
    const AV2_COMMON *cm, MACROBLOCK *x, const PICK_MODE_CONTEXT *ctx,
    BLOCK_SIZE bsize, int rate_mode, int skip_ctx, const RD_STATS *rdc_y,
    int skippable_y, bool keep_subblk_skip_y, const RD_STATS *rdc_uv,
    int skippable_uv, PRIMARY_TX_TYPE tx_type_y,
    TX_PARTITION_TYPE nonskip_tx_partition, TX_SIZE unsplit_tx_size,
    bool signal_tx_partition, RD_STATS *rdc) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const ModeCosts *const mode_costs = &x->mode_costs;
  const int num_planes = av2_num_planes(cm);
  const int is_skippable = skippable_y && skippable_uv;
  const TX_PARTITION_TYPE partition =
      skippable_y ? TX_PARTITION_NONE : nonskip_tx_partition;
  set_tx_partition_nonrd(xd, mbmi, bsize, partition, unsplit_tx_size);

  // When av2_block_yrd() evaluated the exact coded transform sub-blocks, keep
  // its per-sub-block skip flags so av2_encode_sb() skips zero sub-blocks;
  // otherwise set luma blk_skip uniformly.
  for (int i = 0; i < ctx->num_4x4_blk; ++i) {
    if (skippable_y || !keep_subblk_skip_y) {
      set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i, skippable_y);
    }
    const bool subblk_skipped =
        is_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i);
    xd->tx_type_map[i] =
        MAKE_TX_TYPE_FROM_PRIMARY_TX_TYPE(subblk_skipped ? DCT_DCT : tx_type_y);
  }
  // Unlike AV1 (where blk_skip is luma-only), AV2's encode_block() skips the
  // forward DCT and quantizer on chroma planes whenever blk_skip[plane] == 1.
  // Keep chroma blk_skip == 0 on every non-skipped block so real DCT chroma
  // coefficients are quantized and coded instead of dropped by model deadzones.
  for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
    for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[plane], i, is_skippable);
    }
  }
  for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
    xd->cctx_type_map[i] = CCTX_NONE;
  }

  rdc->dist = rdc_y->dist + rdc_uv->dist;
  rdc->sse = rdc_y->sse + rdc_uv->sse;
  rdc->rate = rate_mode + mode_costs->skip_txfm_cost[skip_ctx][is_skippable];
  if (!is_skippable) {
    rdc->rate += rdc_y->rate + rdc_uv->rate;
    if (signal_tx_partition) {
      const int num_tx_units = count_64x64_tx_units_nonrd(xd, bsize);
      rdc->rate +=
          num_tx_units * inter_tx_partition_cost(x, partition, bsize,
                                                 max_txsize_rect_lookup[bsize]);
    }
  }
  rdc->skip_txfm = is_skippable;
  rdc->rdcost = RDCOST(x->rdmult, rdc->rate, rdc->dist);
  mbmi->skip_txfm[0] = is_skippable;
  mbmi->skip_txfm[1] = is_skippable;
  x->txfm_search_info.skip_txfm = is_skippable;
  return is_skippable;
}

// =============================================================================
// Section 4: Shared Non-RD Intra, Luma Palette, and IntraBC Evaluators
// =============================================================================

// Shared non-RD evaluator for directional/smooth/DC intra modes (plus intra FSC
// when enabled), used on both intra frames (av2_nonrd_pick_intra_mode) and
// inter frames (estimate_intra_mode_nonrd).
static void evaluate_intra_modes_nonrd(
    const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize, bool is_intra_frame,
    unsigned int ref_cost_intra, int no_skip_mode_cost, int intra_cost_penalty,
    RD_STATS *best_rdc, MB_MODE_INFO *best_mbmi, int *best_skip,
    PICK_MODE_CONTEXT *ctx, int *best_y_rate_out, int64_t *best_y_dist_out,
    int64_t *best_y_sse_out) {
  const AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const ModeCosts *const mode_costs = &x->mode_costs;
  const bool is_screen = is_screen_content_nonrd(cpi);
  const int lossless = xd->lossless[mbmi->segment_id];
  xd->reduced_tx_part_set = cm->seq_params.reduced_tx_part_set;

  const bool signal_intra_tx_partition =
      !lossless &&
      x->txfm_search_params.tx_mode_search_type == TX_MODE_SELECT &&
      block_signals_txsize(bsize);
  const TX_PARTITION_TYPE intra_partition =
      (signal_intra_tx_partition && sf->tx_sf.enable_tx_partition)
          ? get_tx_partition_nonrd(bsize)
          : TX_PARTITION_NONE;
  const TX_SIZE unsplit_tx_size =
      lossless ? TX_4X4
               : tx_size_from_tx_mode(
                     bsize, x->txfm_search_params.tx_mode_search_type);
  const TX_SIZE coded_intra_tx_size =
      (intra_partition == TX_PARTITION_NONE)
          ? unsplit_tx_size
          : get_tx_partition_one_size(intra_partition,
                                      max_txsize_rect_lookup[bsize]);
  const TX_SIZE yrd_tx_size = AVMMIN(TX_32X32, coded_intra_tx_size);
  const int tx_part_cost =
      signal_intra_tx_partition
          ? intra_tx_partition_cost(x, intra_partition,
                                    max_txsize_rect_lookup[bsize])
          : 0;

  const int stepr = tx_size_high_unit[coded_intra_tx_size];
  const int stepc = tx_size_wide_unit[coded_intra_tx_size];
  const int max_blocks_wide = max_block_wide(xd, bsize, AVM_PLANE_Y);
  const int max_blocks_high = max_block_high(xd, bsize, AVM_PLANE_Y);

  init_mbmi_nonrd(mbmi, DC_PRED, INTRA_FRAME, NONE_FRAME, cm, xd);
  set_tx_partition_nonrd(
      xd, mbmi, bsize, TX_PARTITION_NONE,
      xd->lossless[mbmi->segment_id] ? TX_4X4 : max_txsize_rect_lookup[bsize]);
  get_y_intra_mode_set(mbmi, xd);

  const int y_ctx = get_y_mode_idx_ctx(xd);
  const bool allow_smooth = cpi->oxcf.intra_mode_cfg.enable_smooth_intra &&
                            !sf->intra_sf.disable_smooth_intra;
  const bool has_chroma =
      av2_num_planes(cm) > 1 && xd->is_chroma_ref && xd->tree_type != LUMA_PART;

  for (int m_idx = 0; m_idx < NUM_INTRA_MODES_NONRD; ++m_idx) {
    const PREDICTION_MODE target_mode = kIntraModesNonRd[m_idx];
    if (target_mode == SMOOTH_PRED && !allow_smooth) continue;
    // On keyframes, force DC_PRED on spatially flat large blocks at the
    // top-left corner (as in libaom av1_nonrd_pick_intra_mode).
    if (is_intra_frame && x->source_variance == 0 && xd->mi_row == 0 &&
        xd->mi_col == 0 && block_size_wide[bsize] >= 32 &&
        block_size_high[bsize] >= 32 && target_mode != DC_PRED) {
      continue;
    }
    if (!is_intra_frame && block_size_wide[bsize] >= 32 &&
        block_size_high[bsize] >= 32 && target_mode == SMOOTH_PRED &&
        !is_screen) {
      continue;
    }

    // Locate target_mode (with angle_delta == 0) in mbmi->y_intra_mode_list.
    int found_mode_idx = -1;
    for (int list_idx = 0; list_idx < LUMA_MODE_COUNT; ++list_idx) {
      mbmi->y_mode_idx = list_idx;
      mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[list_idx];
      av2_set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
      if (mbmi->mode == target_mode && mbmi->angle_delta[PLANE_TYPE_Y] == 0) {
        found_mode_idx = list_idx;
        break;
      }
    }
    if (found_mode_idx < 0) continue;

    const int max_fsc =
        (is_screen && cm->seq_params.enable_fsc &&
         allow_fsc_intra(cm, bsize, mbmi) && xd->tree_type != CHROMA_PART)
            ? FSC_MODES
            : 1;
    bool pred_built = false;
    bool uv_evaluated = false;
    UV_PREDICTION_MODE cached_uv_mode = UV_DC_PRED;
    uint8_t cached_uv_mode_idx = 0;
    int cached_uv_mode_cost = 0;
    RD_STATS cached_rdc_uv;
    av2_init_rd_stats(&cached_rdc_uv);

    for (int fsc_y = 0; fsc_y < max_fsc; ++fsc_y) {
      init_mbmi_nonrd(mbmi, target_mode, INTRA_FRAME, NONE_FRAME, cm, xd);
      get_y_intra_mode_set(mbmi, xd);
      mbmi->y_mode_idx = found_mode_idx;
      mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[found_mode_idx];
      av2_set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
      mbmi->fsc_mode[PLANE_TYPE_Y] = fsc_y;
      mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
      set_tx_partition_nonrd(xd, mbmi, bsize, intra_partition, unsplit_tx_size);

      get_uv_intra_mode_set(mbmi);
      mbmi->uv_mode = UV_DC_PRED;
      mbmi->uv_mode_idx = av2_is_directional_mode(mbmi->mode) ? 1 : 0;

      // Mode signaling cost (matching av2_handle_intra_mode).
      int mode_set_index = found_mode_idx < FIRST_MODE_COUNT ? 0 : 1;
      mode_set_index +=
          ((found_mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
      int base_mode_cost = mode_costs->y_primary_flag_cost[mode_set_index];
      if (found_mode_idx < FIRST_MODE_COUNT) {
        const int mode_set_low =
            AVMMIN(found_mode_idx, LUMA_INTRA_MODE_INDEX_COUNT - 1);
        base_mode_cost += mode_costs->y_mode_idx_costs[y_ctx][mode_set_low];
        if (mode_set_low == (LUMA_INTRA_MODE_INDEX_COUNT - 1)) {
          base_mode_cost +=
              mode_costs->y_mode_idx_offset_costs[y_ctx][found_mode_idx -
                                                         mode_set_low];
        }
      } else {
        base_mode_cost += av2_cost_literal(4);
      }
      if (cm->seq_params.enable_mrls && av2_is_directional_mode(mbmi->mode)) {
        const int mrl_ctx =
            get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
        base_mode_cost += mode_costs->mrl_index_cost[mrl_ctx][0];
      }
      base_mode_cost += (int)ref_cost_intra + no_skip_mode_cost;

      // In inter frames, intra_cost_penalty applies to all intra modes
      // (including DC_PRED), as in libaom av1_estimate_intra_mode().
      const int mode_cost_y =
          intra_mode_info_cost_y(cpi, x, mbmi, bsize, base_mode_cost) +
          tx_part_cost + intra_cost_penalty;

      if (RDCOST(x->rdmult, mode_cost_y, 0) >= best_rdc->rdcost) {
        continue;
      }

      // Build luma intra prediction across transform sub-blocks.
      if (!pred_built) {
        for (int row = 0; row < max_blocks_high; row += stepr) {
          for (int col = 0; col < max_blocks_wide; col += stepc) {
            av2_predict_intra_block_facade(cm, xd, AVM_PLANE_Y, col, row,
                                           coded_intra_tx_size);
          }
        }
        pred_built = true;
      }

      const PRIMARY_TX_TYPE intra_tx_type =
          (fsc_y && !lossless) ? IDTX : DCT_DCT;
      RD_STATS this_rdc_y;
      int is_skippable_y = 0;
      av2_block_yrd(cpi, x, &this_rdc_y, &is_skippable_y, bsize, yrd_tx_size,
                    intra_tx_type, /*is_inter=*/0);
      if (RDCOST(x->rdmult, mode_cost_y + this_rdc_y.rate, this_rdc_y.dist) >=
          best_rdc->rdcost) {
        continue;
      }
      for (int i = 0; i < ctx->num_4x4_blk; ++i) {
        set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i, 0);
        xd->tx_type_map[i] = MAKE_TX_TYPE_FROM_PRIMARY_TX_TYPE(intra_tx_type);
      }

      // Evaluate UV_DC_PRED and (on intra frames or screen blocks) the chroma
      // mode matching target_mode, caching the winner across fsc_y = 0..1.
      if (!uv_evaluated) {
        if (has_chroma) {
          const CFL_ALLOWED_TYPE cfl_or_mhccp =
              is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
              is_mhccp_allowed(cm, xd);
          const int dpcm_uv_extra = lossless ? mode_costs->dpcm_uv_cost[0] : 0;
          UV_PREDICTION_MODE uv_modes[2] = { UV_DC_PRED, UV_DC_PRED };
          uint8_t uv_indices[2] = {
            (uint8_t)(av2_is_directional_mode(target_mode) ? 1 : 0), 0
          };
          int num_uv_modes = 1;
          if ((is_intra_frame || is_screen) && target_mode != DC_PRED) {
            uv_modes[1] = (target_mode == SMOOTH_PRED)
                              ? UV_SMOOTH_PRED
                              : (UV_PREDICTION_MODE)target_mode;
            uv_indices[1] = av2_is_directional_mode(target_mode) ? 0 : 1;
            num_uv_modes = 2;
          }
          int64_t best_uv_rd = INT64_MAX;
          for (int u_idx = 0; u_idx < num_uv_modes; ++u_idx) {
            mbmi->uv_mode = uv_modes[u_idx];
            mbmi->uv_mode_idx = uv_indices[u_idx];
            mbmi->angle_delta[PLANE_TYPE_UV] = 0;
            RD_STATS cand_rdc_uv;
            int cand_skip_uv = 1;
            block_uvrd_nonrd(cpi, x, ctx, &cand_rdc_uv, &cand_skip_uv, bsize,
                             /*use_color_sens=*/false);
            const int cand_uv_cost =
                dpcm_uv_extra + get_uv_mode_cost(mbmi, mode_costs, xd,
                                                 cfl_or_mhccp,
                                                 mbmi->uv_mode_idx);
            const int64_t cand_uv_rd = RDCOST(
                x->rdmult, cand_uv_cost + cand_rdc_uv.rate, cand_rdc_uv.dist);
            if (cand_uv_rd < best_uv_rd) {
              best_uv_rd = cand_uv_rd;
              cached_uv_mode = uv_modes[u_idx];
              cached_uv_mode_idx = uv_indices[u_idx];
              cached_uv_mode_cost = cand_uv_cost;
              cached_rdc_uv = cand_rdc_uv;
            }
          }
        }
        uv_evaluated = true;
      }

      mbmi->uv_mode = cached_uv_mode;
      mbmi->uv_mode_idx = cached_uv_mode_idx;
      mbmi->angle_delta[PLANE_TYPE_UV] = 0;
      for (int plane = AVM_PLANE_U; plane < av2_num_planes(cm); ++plane) {
        for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
          set_blk_skip(x->txfm_search_info.blk_skip[plane], i, 0);
        }
      }
      for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
        xd->cctx_type_map[i] = CCTX_NONE;
      }

      // Intra blocks are always coded with skip_txfm = 0.
      mbmi->skip_txfm[0] = 0;
      mbmi->skip_txfm[1] = 0;
      x->txfm_search_info.skip_txfm = 0;

      RD_STATS this_rdc;
      this_rdc.rate = mode_cost_y + this_rdc_y.rate + cached_uv_mode_cost +
                      cached_rdc_uv.rate;
      this_rdc.dist = this_rdc_y.dist + cached_rdc_uv.dist;
      this_rdc.sse = this_rdc_y.sse + cached_rdc_uv.sse;
      this_rdc.skip_txfm = 0;
      this_rdc.rdcost = RDCOST(x->rdmult, this_rdc.rate, this_rdc.dist);

      if (this_rdc.rdcost < best_rdc->rdcost) {
        save_best_mode_context(cm, x, ctx, &this_rdc, /*this_skip=*/0, best_rdc,
                               best_mbmi, best_skip);
        if (best_y_rate_out != NULL) {
          *best_y_rate_out = mode_cost_y + this_rdc_y.rate;
          *best_y_dist_out = this_rdc_y.dist;
          *best_y_sse_out = this_rdc_y.sse;
        }
      }
    }
  }
}

// Evaluates Chroma-from-Luma (UV_CFL_PRED) on the single winning non-RD luma
// intra mode using both neighbor-derived scaling (CFL_DERIVED_ALPHA) and
// closed-form least-squares explicit alphas (CFL_EXPLICIT).
static void search_cfl_uv_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                                BLOCK_SIZE bsize, int best_y_rate,
                                int64_t best_y_dist, int64_t best_y_sse,
                                RD_STATS *best_rdc, MB_MODE_INFO *best_mbmi,
                                int *best_skip, PICK_MODE_CONTEXT *ctx) {
  const AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const ModeCosts *const mode_costs = &x->mode_costs;

  if (!sf->rt_sf.use_cfl_nonrd || best_mbmi->mode == MODE_INVALID ||
      is_inter_mode(best_mbmi->mode) ||
      best_mbmi->palette_mode_info.palette_size[0] > 0 ||
      best_mbmi->use_intrabc[0]) {
    return;
  }
  if (!cpi->oxcf.intra_mode_cfg.enable_cfl_intra ||
      !cm->seq_params.enable_cfl_intra || av2_num_planes(cm) <= 1 ||
      !xd->is_chroma_ref || xd->tree_type != SHARED_PART ||
      xd->lossless[best_mbmi->segment_id]) {
    return;
  }
  if (RDCOST(x->rdmult, best_y_rate, 0) >= best_rdc->rdcost) {
    return;
  }

  *mbmi = *best_mbmi;
  memcpy(x->txfm_search_info.blk_skip[AVM_PLANE_Y], ctx->blk_skip[AVM_PLANE_Y],
         sizeof(uint8_t) * ctx->num_4x4_blk);
  av2_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);

  if (!is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd)) {
    return;
  }

  const struct macroblockd_plane *const pd_u = &xd->plane[AVM_PLANE_U];
  const BLOCK_SIZE uv_bsize = get_mb_plane_block_size(
      xd, mbmi, AVM_PLANE_U, pd_u->subsampling_x, pd_u->subsampling_y);
  const TX_SIZE uv_tx_size = av2_get_tx_size(AVM_PLANE_U, xd);
  if (uv_bsize >= BLOCK_SIZES_ALL ||
      block_size_wide[uv_bsize] != tx_size_wide[uv_tx_size] ||
      block_size_high[uv_bsize] != tx_size_high[uv_tx_size]) {
    return;
  }

  // Reconstruct luma into xd->plane[AVM_PLANE_Y].dst so CfL subsamples the
  // exact quantized-and-reconstructed luma samples seen by the decoder.
  xd->cfl.store_y = 0;
  av2_encode_intra_block_plane(cpi, x, bsize, AVM_PLANE_Y, DRY_RUN_NORMAL,
                               NO_TRELLIS_OPT);
  for (int i = 0; i < ctx->num_4x4_blk; ++i) {
    set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i, 0);
  }
  mbmi->skip_txfm[0] = 0;
  mbmi->skip_txfm[1] = 0;
  x->txfm_search_info.skip_txfm = 0;

  const CFL_ALLOWED_TYPE cfl_or_mhccp =
      is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
      is_mhccp_allowed(cm, xd);
  const bool mhccp_allowed = is_mhccp_allowed(cm, xd) != 0;

  // 1. Evaluate implicit neighbor-derived scaling (CFL_DERIVED_ALPHA).
  mbmi->uv_mode = UV_CFL_PRED;
  mbmi->uv_mode_idx = 0;
  mbmi->angle_delta[PLANE_TYPE_UV] = 0;
  mbmi->cfl_idx = CFL_DERIVED_ALPHA;
  mbmi->cfl_alpha_idx = 0;
  mbmi->cfl_alpha_signs = 0;

  const int base_cfl_cost =
      get_uv_mode_cost(mbmi, mode_costs, xd, cfl_or_mhccp, 0) +
      (mhccp_allowed ? mode_costs->cfl_mhccp_cost[0] : 0);

  RD_STATS derived_uv;
  int derived_skip_uv = 1;
  block_uvrd_nonrd(cpi, x, ctx, &derived_uv, &derived_skip_uv, bsize,
                   /*use_color_sens=*/false);
  const int derived_cost =
      base_cfl_cost + mode_costs->cfl_index_cost[CFL_DERIVED_ALPHA];
  for (int plane = AVM_PLANE_U; plane < av2_num_planes(cm); ++plane) {
    for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[plane], i, 0);
    }
  }
  for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
    xd->cctx_type_map[i] = CCTX_NONE;
  }
  mbmi->skip_txfm[0] = 0;
  mbmi->skip_txfm[1] = 0;
  x->txfm_search_info.skip_txfm = 0;

  RD_STATS cand_rdc;
  cand_rdc.rate = best_y_rate + derived_cost + derived_uv.rate;
  cand_rdc.dist = best_y_dist + derived_uv.dist;
  cand_rdc.sse = best_y_sse + derived_uv.sse;
  cand_rdc.skip_txfm = 0;
  cand_rdc.rdcost = RDCOST(x->rdmult, cand_rdc.rate, cand_rdc.dist);
  if (cand_rdc.rdcost < best_rdc->rdcost) {
    save_best_mode_context(cm, x, ctx, &cand_rdc, /*this_skip=*/0, best_rdc,
                           best_mbmi, best_skip);
  }

  // 2. Estimate closed-form least-squares explicit alphas (CFL_EXPLICIT) from
  //    xd->cfl.ac_buf_q3 (already populated during the derived-alpha pass).
  const int vis_uw = max_block_wide(xd, uv_bsize, AVM_PLANE_U) << MI_SIZE_LOG2;
  const int vis_uh = max_block_high(xd, uv_bsize, AVM_PLANE_U) << MI_SIZE_LOG2;
  const int count = vis_uw * vis_uh;
  if (count > 0 && xd->cfl.are_parameters_computed) {
    const int16_t *const ac_q3 = xd->cfl.ac_buf_q3;
    int64_t sum_xx = 0;
    for (int r = 0; r < vis_uh; ++r) {
      const int16_t *const ac_row = ac_q3 + r * CFL_BUF_LINE;
      for (int c = 0; c < vis_uw; ++c) {
        const int64_t v = ac_row[c];
        sum_xx += v * v;
      }
    }
    if (sum_xx > 0) {
      int m_uv[2] = { 0, 0 };
      for (int p_idx = 0; p_idx < 2; ++p_idx) {
        const int plane = AVM_PLANE_U + p_idx;
        const uint16_t *const src_uv = x->plane[plane].src.buf;
        const int src_stride = x->plane[plane].src.stride;
        int64_t sum_y = 0;
        for (int r = 0; r < vis_uh; ++r) {
          const uint16_t *const s_row = src_uv + r * src_stride;
          for (int c = 0; c < vis_uw; ++c) {
            sum_y += s_row[c];
          }
        }
        const int64_t avg_y = (sum_y + (count >> 1)) / count;
        int64_t sum_xy = 0;
        for (int r = 0; r < vis_uh; ++r) {
          const int16_t *const ac_row = ac_q3 + r * CFL_BUF_LINE;
          const uint16_t *const s_row = src_uv + r * src_stride;
          for (int c = 0; c < vis_uw; ++c) {
            sum_xy += (int64_t)ac_row[c] * ((int64_t)s_row[c] - avg_y);
          }
        }
        const int64_t num =
            sum_xy * 64 + (sum_xy >= 0 ? (sum_xx >> 1) : -(sum_xx >> 1));
        m_uv[p_idx] =
            clamp((int)(num / sum_xx), -CFL_ALPHABET_SIZE, CFL_ALPHABET_SIZE);
      }

      if (m_uv[0] != 0 || m_uv[1] != 0) {
        const int sign_u = (m_uv[0] == 0)
                               ? CFL_SIGN_ZERO
                               : (m_uv[0] > 0 ? CFL_SIGN_POS : CFL_SIGN_NEG);
        const int sign_v = (m_uv[1] == 0)
                               ? CFL_SIGN_ZERO
                               : (m_uv[1] > 0 ? CFL_SIGN_POS : CFL_SIGN_NEG);
        const int joint_sign = sign_u * CFL_SIGNS + sign_v - 1;
        assert(joint_sign >= 0 && joint_sign < CFL_JOINT_SIGNS);
        const int idx_u = (m_uv[0] == 0) ? 0 : (abs(m_uv[0]) - 1);
        const int idx_v = (m_uv[1] == 0) ? 0 : (abs(m_uv[1]) - 1);

        mbmi->uv_mode = UV_CFL_PRED;
        mbmi->uv_mode_idx = 0;
        mbmi->angle_delta[PLANE_TYPE_UV] = 0;
        mbmi->cfl_idx = CFL_EXPLICIT;
        mbmi->cfl_alpha_signs = (int8_t)joint_sign;
        mbmi->cfl_alpha_idx =
            (uint8_t)((idx_u << CFL_ALPHABET_SIZE_LOG2) + idx_v);

        const int explicit_cost =
            base_cfl_cost + mode_costs->cfl_index_cost[CFL_EXPLICIT] +
            mode_costs->cfl_cost[joint_sign][CFL_PRED_U][idx_u] +
            mode_costs->cfl_cost[joint_sign][CFL_PRED_V][idx_v];

        if (RDCOST(x->rdmult, best_y_rate + explicit_cost, 0) <
            best_rdc->rdcost) {
          RD_STATS explicit_uv;
          int explicit_skip_uv = 1;
          block_uvrd_nonrd(cpi, x, ctx, &explicit_uv, &explicit_skip_uv, bsize,
                           /*use_color_sens=*/false);
          for (int plane = AVM_PLANE_U; plane < av2_num_planes(cm); ++plane) {
            for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
              set_blk_skip(x->txfm_search_info.blk_skip[plane], i, 0);
            }
          }
          for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
            xd->cctx_type_map[i] = CCTX_NONE;
          }
          mbmi->skip_txfm[0] = 0;
          mbmi->skip_txfm[1] = 0;
          x->txfm_search_info.skip_txfm = 0;

          cand_rdc.rate = best_y_rate + explicit_cost + explicit_uv.rate;
          cand_rdc.dist = best_y_dist + explicit_uv.dist;
          cand_rdc.sse = best_y_sse + explicit_uv.sse;
          cand_rdc.skip_txfm = 0;
          cand_rdc.rdcost = RDCOST(x->rdmult, cand_rdc.rate, cand_rdc.dist);
          if (cand_rdc.rdcost < best_rdc->rdcost) {
            save_best_mode_context(cm, x, ctx, &cand_rdc, /*this_skip=*/0,
                                   best_rdc, best_mbmi, best_skip);
          }
        }
      }
    }
  }

  xd->cfl.use_dc_pred_cache = 0;
  xd->cfl.dc_pred_is_cached[0] = 0;
  xd->cfl.dc_pred_is_cached[1] = 0;
}

static void estimate_intra_mode_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                                      BLOCK_SIZE bsize,
                                      unsigned int ref_cost_intra,
                                      int no_skip_mode_cost,
                                      bool best_early_term, RD_STATS *best_rdc,
                                      MB_MODE_INFO *best_mbmi, int *best_skip,
                                      int best_skip_y, PICK_MODE_CONTEXT *ctx) {
  const AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];

  if (mbmi->tree_type == SHARED_PART &&
      mbmi->region_type == MIXED_INTER_INTRA_REGION &&
      mbmi->chroma_ref_info.offset_started) {
    return;
  }
  if (!sf->rt_sf.check_intra_pred_nonrd || best_early_term) {
    return;
  }

  const bool is_screen = is_screen_content_nonrd(cpi);
  // As in libaom (max_intra_bsize = BLOCK_32X32 at speed >= 7 except on
  // high-SAD or flat non-zero-SAD screen blocks, and source_metrics_sb_nonrd
  // in av1_estimate_intra_mode), skip intra evaluation on low-SAD textured
  // superblocks where temporal prediction holds, and on > 32x32 blocks.
  if (best_rdc->rdcost < INT64_MAX) {
    if (x->source_variance >= 50 && x->source_sad_level <= kLowSad) {
      return;
    }
    const bool allow_large_screen_intra =
        is_screen &&
        (cpi->rc.high_source_sad ||
         (x->source_variance < 50 && x->source_sad_level != kZeroSad));
    if (!allow_large_screen_intra &&
        (block_size_wide[bsize] > 32 || block_size_high[bsize] > 32)) {
      return;
    }
  }

  // Aggressive intra skip when the best inter mode already has low distortion
  // relative to the active quantizer step size. Screen content uses a tighter
  // bound: a skipped block with visible error there usually means newly
  // uncovered content that intra can code better.
  if (sf->rt_sf.nonrd_aggressive_skip_intra && best_rdc->rdcost < INT64_MAX &&
      is_inter_mode(best_mbmi->mode)) {
    const int64_t num_pels =
        (int64_t)block_size_wide[bsize] * block_size_high[bsize];
    const int64_t qstep_px = get_luma_qstep_px_nonrd(x);
    const int64_t qstep_sq = qstep_px * qstep_px;
    const int64_t skip_dist_thresh =
        is_screen ? AVMMIN(num_pels << 6, num_pels * qstep_sq)
                  : (num_pels * qstep_sq);
    const int64_t eff_thresh = is_boosted_segment_nonrd(cpi, mbmi->segment_id)
                                   ? (skip_dist_thresh >> 1)
                                   : skip_dist_thresh;
    if (best_skip_y && best_rdc->dist <= eff_thresh) {
      return;
    }
    if (num_pels >= 64 * 64 && best_rdc->dist <= (eff_thresh << 1)) {
      return;
    }
  }

  int intra_cost_penalty = av2_get_intra_cost_penalty(
      cm->quant_params.base_qindex, cm->quant_params.y_dc_delta_q,
      cm->seq_params.base_y_dc_delta_q, cm->seq_params.bit_depth);
  // Reduce the intra penalty on low-variance blocks when the winning inter mode
  // uses a secondary reference or large motion (as in libaom
  // av1_estimate_intra_mode()).
  if (x->source_variance < 50 && best_rdc->rdcost < INT64_MAX &&
      (abs(best_mbmi->mv[0].as_mv.row) >= 32 ||
       abs(best_mbmi->mv[0].as_mv.col) >= 32)) {
    intra_cost_penalty >>= 2;
  }

  if (RDCOST(x->rdmult,
             (int)ref_cost_intra + no_skip_mode_cost + intra_cost_penalty,
             0) >= best_rdc->rdcost) {
    return;
  }

  int intra_y_rate = 0;
  int64_t intra_y_dist = 0;
  int64_t intra_y_sse = 0;
  const int64_t prev_best_rd = best_rdc->rdcost;
  evaluate_intra_modes_nonrd(cpi, x, bsize, /*is_intra_frame=*/false,
                             ref_cost_intra, no_skip_mode_cost,
                             intra_cost_penalty, best_rdc, best_mbmi, best_skip,
                             ctx, &intra_y_rate, &intra_y_dist, &intra_y_sse);
  if (best_rdc->rdcost < prev_best_rd && !is_inter_mode(best_mbmi->mode)) {
    search_cfl_uv_nonrd(cpi, x, bsize, intra_y_rate, intra_y_dist, intra_y_sse,
                        best_rdc, best_mbmi, best_skip, ctx);
  }
}

// Self-contained luma-only palette search for non-RD intra and inter pickmode
// (modeled on libaom's av1_search_palette_mode_luma): searches luma palettes
// via av2_rd_pick_palette_intra_sby() under WINNER_MODE_EVAL while keeping
// chroma on UV_DC_PRED via block_uvrd_nonrd(). Returns true if palette won.
static bool search_palette_luma_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                                      BLOCK_SIZE bsize, int extra_header_cost,
                                      PICK_MODE_CONTEXT *ctx,
                                      RD_STATS *best_rdc,
                                      MB_MODE_INFO *best_mbmi, int *best_skip) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const ModeCosts *const mode_costs = &x->mode_costs;
  const int num_planes = av2_num_planes(cm);

  uint8_t saved_blk_skip[MAX_MB_PLANE][MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE saved_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  CctxType saved_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  for (int i = 0; i < num_planes; ++i) {
    const int num_blk_plane =
        (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    memcpy(saved_blk_skip[i], ctx->blk_skip[i],
           sizeof(uint8_t) * num_blk_plane);
  }
  av2_copy_array(saved_tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
  av2_copy_array(saved_cctx_type_map, ctx->cctx_type_map,
                 ctx->num_4x4_blk_chroma);

  init_mbmi_nonrd(mbmi, DC_PRED, INTRA_FRAME, NONE_FRAME, cm, xd);
  set_tx_partition_nonrd(
      xd, mbmi, bsize, TX_PARTITION_NONE,
      xd->lossless[mbmi->segment_id] ? TX_4X4 : max_txsize_rect_lookup[bsize]);
  get_y_intra_mode_set(mbmi, xd);
  get_uv_intra_mode_set(mbmi);
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->uv_mode_idx = 0;

  const int y_ctx = get_y_mode_idx_ctx(xd);
  const int dc_mode_cost = mode_costs->y_primary_flag_cost[DC_PRED] +
                           mode_costs->y_mode_idx_costs[y_ctx][DC_PRED] +
                           extra_header_cost;

  MB_MODE_INFO best_mbmi_palette = *mbmi;
  uint8_t *const best_palette_color_map =
      x->palette_buffer->best_palette_color_map;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  int64_t best_rd_palette = best_rdc->rdcost;
  int64_t best_model_rd_palette = INT64_MAX;
  RD_STATS rd_stats_y;
  av2_invalid_rd_stats(&rd_stats_y);

  av2_rd_pick_palette_intra_sby(cpi, x, bsize, dc_mode_cost, &best_mbmi_palette,
                                best_palette_color_map, &best_rd_palette,
                                &best_model_rd_palette, &rd_stats_y.rate, NULL,
                                &rd_stats_y.dist, &rd_stats_y.skip_txfm, NULL,
                                ctx, best_blk_skip, best_tx_type_map);

  bool palette_won = false;
  if (rd_stats_y.rate < INT_MAX &&
      mbmi->palette_mode_info.palette_size[0] > 0) {
    memcpy(x->txfm_search_info.blk_skip[AVM_PLANE_Y], best_blk_skip,
           sizeof(best_blk_skip[0]) * ctx->num_4x4_blk);
    av2_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);

    RD_STATS rdc_uv;
    int skippable_uv = 1;
    block_uvrd_nonrd(cpi, x, ctx, &rdc_uv, &skippable_uv, bsize,
                     /*use_color_sens=*/false);

    int uv_mode_cost = 0;
    if (num_planes > 1 && xd->is_chroma_ref && xd->tree_type != LUMA_PART) {
      if (xd->lossless[mbmi->segment_id]) {
        uv_mode_cost += mode_costs->dpcm_uv_cost[0];
      }
      uv_mode_cost += get_uv_mode_cost(
          mbmi, mode_costs, xd,
          is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
              is_mhccp_allowed(cm, xd),
          mbmi->uv_mode_idx);
    }

    mbmi->skip_txfm[0] = 0;
    mbmi->skip_txfm[1] = 0;
    x->txfm_search_info.skip_txfm = 0;

    RD_STATS this_rdc;
    this_rdc.rate = rd_stats_y.rate + uv_mode_cost + rdc_uv.rate;
    this_rdc.dist = rd_stats_y.dist + rdc_uv.dist;
    this_rdc.sse = this_rdc.dist;
    this_rdc.skip_txfm = 0;
    this_rdc.rdcost = RDCOST(x->rdmult, this_rdc.rate, this_rdc.dist);

    if (this_rdc.rdcost < best_rdc->rdcost) {
      save_best_mode_context(cm, x, ctx, &this_rdc, /*this_skip=*/0, best_rdc,
                             best_mbmi, best_skip);
      palette_won = true;
    }
  }

  if (!palette_won) {
    for (int i = 0; i < num_planes; ++i) {
      const int num_blk_plane =
          (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
      memcpy(ctx->blk_skip[i], saved_blk_skip[i],
             sizeof(uint8_t) * num_blk_plane);
    }
    av2_copy_array(ctx->tx_type_map, saved_tx_type_map, ctx->num_4x4_blk);
    av2_copy_array(ctx->cctx_type_map, saved_cctx_type_map,
                   ctx->num_4x4_blk_chroma);
  }
  return palette_won;
}

static AVM_INLINE bool is_bv_valid_nonrd(
    const FULLPEL_MV *full_mv, const AV2_COMMON *cm, const MACROBLOCKD *xd,
    int mi_row, int mi_col, BLOCK_SIZE bsize,
    const FULLPEL_MOTION_SEARCH_PARAMS *fullms_params) {
  const MV dv = get_mv_from_fullmv(full_mv);
  return av2_is_fullmv_in_range(
             &fullms_params->mv_limits, *full_mv,
             fullms_params->mv_cost_params.pb_mv_precision) &&
         av2_enc_is_dv_valid(dv, cm, xd, mi_row, mi_col, bsize,
                             cm->mib_size_log2);
}

// Self-contained non-RD IntraBC search (modeled on libaom's
// av1_search_intrabc_nonrd and AV2 IntraBC block vector syntax), completely
// decoupled from rdopt.c. Supports both keyframe global+local IntraBC and
// inter-frame local IntraBC using av2_block_yrd() + block_uvrd_nonrd().
static bool search_intrabc_nonrd(const AV2_COMP *cpi, MACROBLOCK *x,
                                 BLOCK_SIZE bsize, int skip_interintra_cost,
                                 PICK_MODE_CONTEXT *ctx, RD_STATS *best_rdc,
                                 MB_MODE_INFO *best_mbmi, int *best_skip) {
  const AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const TileInfo *const tile = &xd->tile;
  const int num_planes = av2_num_planes(cm);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int sb_row = mi_row >> cm->mib_size_log2;
  const int sb_col = mi_col >> cm->mib_size_log2;

  const int ibc_loop_start =
      (frame_is_intra_only(cm) && cm->features.allow_global_intrabc) ? 0 : 1;
  const int ibc_loop_end = cm->features.allow_local_intrabc ? 2 : 1;
  if (ibc_loop_start >= ibc_loop_end) return false;

  struct buf_2d ori_pre[MAX_MB_PLANE];
  for (int i = 0; i < num_planes; ++i) {
    ori_pre[i] = xd->plane[i].pre[0];
  }
  const int_mv ori_ref_bv0 = mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv;

  struct buf_2d yv12_mb[MAX_MB_PLANE];
  av2_setup_pred_block(xd, yv12_mb, xd->cur_buf, NULL, NULL, num_planes);
  for (int i = 0; i < num_planes; ++i) {
    xd->plane[i].pre[0] = yv12_mb[i];
  }

  init_mbmi_nonrd(mbmi, DC_PRED, INTRA_FRAME, NONE_FRAME, cm, xd);
  set_default_intraBC_bv_precision(cm, mbmi);
  const MvSubpelPrecision default_mv_precision = mbmi->pb_mv_precision;
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);

  mbmi->use_intrabc[0] = 1;
  av2_find_mv_refs(cm, xd, mbmi, INTRA_FRAME, mbmi_ext->ref_mv_count,
                   xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs,
                   NULL, 0, NULL);
  mbmi->use_intrabc[0] = 0;
  av2_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, INTRA_FRAME);

  int_mv dv_ref = av2_find_best_ref_mv_from_stack(mbmi_ext, mbmi, INTRA_FRAME,
                                                  default_mv_precision);
  if (dv_ref.as_int == INVALID_MV || mbmi_ext->ref_mv_count[INTRA_FRAME] == 0) {
    dv_ref.as_int = 0;
  }
  if (dv_ref.as_int == 0) {
    av2_find_ref_dv(&dv_ref, tile, cm->mib_size, mi_row);
  }
  mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = dv_ref;

  FULLPEL_MOTION_SEARCH_PARAMS fullms_params;
  const search_site_config *lookahead_search_sites =
      cpi->mv_search_params.search_site_cfg[SS_CFG_LOOKAHEAD];
  av2_make_default_fullpel_ms_params(
      &fullms_params, cpi, x, bsize, &dv_ref.as_mv, default_mv_precision,
      /*is_ibc_cost=*/1, lookahead_search_sites, /*fine_search_interval=*/0);
  fullms_params.is_intra_mode = 1;
  fullms_params.xd = xd;
  fullms_params.cm = cm;
  fullms_params.mib_size_log2 = cm->mib_size_log2;
  fullms_params.mi_col = mi_col;
  fullms_params.mi_row = mi_row;
  fullms_params.x = x;
  fullms_params.ref_bv_cnt = mbmi_ext->ref_mv_count[INTRA_FRAME];

  enum { IBC_DIR_ABOVE = 0, IBC_DIR_LEFT = 1, IBC_DIRS = 2 };

  int best_cand_cost = INT_MAX;
  int best_intrabc_mode = 0;
  int best_intrabc_drl_idx = 0;
  int_mv best_ref_bv = dv_ref;
  MV best_dv = { 0, 0 };
  FullMvLimits best_mv_limits = fullms_params.mv_limits;

  for (int ibc_loop = ibc_loop_start; ibc_loop < ibc_loop_end; ++ibc_loop) {
    for (int dir = 0; dir < IBC_DIRS; ++dir) {
      set_default_intraBC_bv_precision(cm, mbmi);
      FullMvLimits region_limits = fullms_params.mv_limits;
      if (ibc_loop == 0) {
        if (dir == IBC_DIR_ABOVE) {
          region_limits.col_min = (tile->mi_col_start - mi_col) * MI_SIZE;
          region_limits.col_max = (tile->mi_col_end - mi_col) * MI_SIZE - w;
          region_limits.row_min = (tile->mi_row_start - mi_row) * MI_SIZE;
          region_limits.row_max =
              (sb_row * cm->mib_size - mi_row) * MI_SIZE - h;
        } else {
          region_limits.col_min = (tile->mi_col_start - mi_col) * MI_SIZE;
          region_limits.col_max =
              (sb_col * cm->mib_size - mi_col) * MI_SIZE - w;
          region_limits.row_min = (tile->mi_row_start - mi_row) * MI_SIZE;
          const int bottom_coded_mi_edge =
              AVMMIN((sb_row + 1) * cm->mib_size, tile->mi_row_end);
          region_limits.row_max = (bottom_coded_mi_edge - mi_row) * MI_SIZE - h;
        }
      } else {
        const int num_left_sb =
            (INTRABC_BUFFER_NUM * (1 << (2 * INTRABC_BUFFER_SIZE_LOG2)) +
             (1 << (2 * (cm->mib_size_log2 + MI_SIZE_LOG2))) - 1) >>
            (2 * (cm->mib_size_log2 + MI_SIZE_LOG2));
        int num_left_active_sb = num_left_sb;
        if (cm->mib_size_log2 == mi_size_wide_log2[BLOCK_64X64] &&
            cm->bru.enabled) {
          num_left_active_sb = 1;
          while (num_left_active_sb < num_left_sb) {
            if (!bru_is_sb_available(
                    cm, (sb_col - num_left_active_sb - 1) * cm->mib_size,
                    sb_row * cm->mib_size)) {
              break;
            }
            num_left_active_sb++;
          }
        }
        const int left_coded_mi_edge = AVMMAX(
            (sb_col - num_left_active_sb) * cm->mib_size, tile->mi_col_start);
        const int right_coded_mi_edge =
            AVMMIN((sb_col + 1) * cm->mib_size, tile->mi_col_end);
        const int up_coded_mi_edge =
            AVMMAX(sb_row * cm->mib_size, tile->mi_row_start);
        const int bottom_coded_mi_edge =
            AVMMIN((sb_row + 1) * cm->mib_size, tile->mi_row_end);
        if (dir == IBC_DIR_ABOVE) {
          region_limits.col_min = (left_coded_mi_edge - mi_col) * MI_SIZE;
          region_limits.col_max = (right_coded_mi_edge - mi_col) * MI_SIZE - w;
          region_limits.row_min = (up_coded_mi_edge - mi_row) * MI_SIZE;
          region_limits.row_max = -h;
        } else {
          region_limits.col_min = (left_coded_mi_edge - mi_col) * MI_SIZE;
          region_limits.col_max = -w;
          region_limits.row_min = (up_coded_mi_edge - mi_row) * MI_SIZE;
          region_limits.row_max = (bottom_coded_mi_edge - mi_row) * MI_SIZE - h;
        }
      }

      if (region_limits.col_max < region_limits.col_min ||
          region_limits.row_max < region_limits.row_min) {
        continue;
      }

      int_mv region_ref_bv = dv_ref;
      unsigned int best_drl_sse = UINT_MAX;
      // 1. Evaluate predicted block vector stack (intrabc_mode = 1).
      const int max_drl = AVMMIN(mbmi_ext->ref_mv_count[INTRA_FRAME],
                                 cm->features.max_bvp_drl_bits + 1);
      for (int drl = 0; drl < max_drl; ++drl) {
        int_mv cur_ref = xd->ref_mv_stack[INTRA_FRAME][drl].this_mv;
        if (cur_ref.as_int == INVALID_MV) cur_ref.as_int = 0;
        if (cur_ref.as_int == 0) {
          av2_find_ref_dv(&cur_ref, tile, cm->mib_size, mi_row);
        }
        FullMvLimits drl_limits = region_limits;
        av2_set_mv_search_range(&drl_limits, &cur_ref.as_mv,
                                default_mv_precision);
        if (drl_limits.col_max < drl_limits.col_min ||
            drl_limits.row_max < drl_limits.row_min) {
          continue;
        }
        SUBPEL_MOTION_SEARCH_PARAMS sub_params;
        av2_make_default_subpel_ms_params(&sub_params, cpi, x, bsize,
                                          &cur_ref.as_mv, default_mv_precision,
                                          /*is_ibc_cost=*/1, NULL);
        av2_set_subpel_mv_search_range(&sub_params.mv_limits, &drl_limits,
                                       &cur_ref.as_mv, default_mv_precision);
        if (!is_sub_pel_bv_valid(cur_ref.as_mv, cm, xd, mi_row, mi_col, bsize,
                                 &sub_params.mv_limits, &drl_limits,
                                 default_mv_precision)) {
          continue;
        }
        unsigned int drl_sse = 0;
        int cur_cost = upsampled_pref_error(xd, cm, &cur_ref.as_mv,
                                            &sub_params.var_params, &drl_sse);
        if (cur_cost != INT_MAX) {
          cur_cost += av2_get_ref_bv_rate_cost(
              /*intrabc_mode=*/1, drl, cm->features.max_bvp_drl_bits, x,
              sub_params.mv_cost_params.mv_costs->errorperbit,
              mbmi_ext->ref_mv_count[INTRA_FRAME]);
          if (drl == 0) region_ref_bv = cur_ref;
          if (drl_sse < best_drl_sse) best_drl_sse = drl_sse;
          if (cur_cost < best_cand_cost) {
            best_cand_cost = cur_cost;
            best_intrabc_mode = 1;
            best_intrabc_drl_idx = drl;
            best_ref_bv = cur_ref;
            best_dv = cur_ref.as_mv;
            best_mv_limits = drl_limits;
          }
        }
      }

      if (best_drl_sse == 0) {
        continue;
      }

      // 2. Hash search + full-pixel fallback (intrabc_mode = 0).
      fullms_params.mv_limits = region_limits;
      mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = region_ref_bv;
      av2_init_ref_mv(&fullms_params.mv_cost_params, &region_ref_bv.as_mv);
      init_mv_cost_params(&fullms_params.mv_cost_params, &x->mv_costs, 0,
                          &region_ref_bv.as_mv, default_mv_precision,
                          /*is_ibc_cost=*/1);
      av2_set_mv_search_range(&fullms_params.mv_limits, &region_ref_bv.as_mv,
                              default_mv_precision);
      if (fullms_params.mv_limits.col_max < fullms_params.mv_limits.col_min ||
          fullms_params.mv_limits.row_max < fullms_params.mv_limits.row_min) {
        continue;
      }

      int_mv hash_mv;
      const int hashsme =
          (!sf->rt_sf.rt_prune_intrabc_nonrd || bsize <= BLOCK_8X8)
              ? av2_intrabc_hash_search(cpi, xd, &fullms_params,
                                        &x->intrabc_hash_info,
                                        &hash_mv.as_fullmv)
              : INT_MAX;
      if (hashsme != INT_MAX &&
          is_bv_valid_nonrd(&hash_mv.as_fullmv, cm, xd, mi_row, mi_col, bsize,
                            &fullms_params)) {
        if (hashsme < best_cand_cost) {
          best_cand_cost = hashsme;
          best_intrabc_mode = mbmi->intrabc_mode;
          best_intrabc_drl_idx = mbmi->intrabc_drl_idx;
          best_ref_bv = mbmi->ref_bv;
          best_dv = get_mv_from_fullmv(&hash_mv.as_fullmv);
          best_mv_limits = region_limits;
        }
      } else {
        const FULLPEL_MV start_mv = get_fullmv_from_mv(&region_ref_bv.as_mv);
        // On keyframes, fall back to full-pixel diamond search when directional
        // intra left a large prediction error so repeated text/UI outside the
        // hash table is still matched.
        const bool high_intra_err_kf =
            frame_is_intra_only(cm) && block_size_wide[bsize] <= 16 &&
            block_size_high[bsize] <= 16 && best_rdc->rdcost < INT64_MAX &&
            best_rdc->sse > ((int64_t)w * h << 10);
        const int miss_mode =
            high_intra_err_kf ? 0 : sf->rt_sf.rt_intrabc_miss_mode;
        int_mv full_mv = { 0 };
        int bestsme = INT_MAX;
        if (miss_mode == 0) {
          bestsme = av2_full_pixel_search(start_mv, &fullms_params,
                                          cpi->mv_search_params.mv_step_param,
                                          NULL, &full_mv.as_fullmv, NULL);
        } else {
          // Local start_mv + 12-point neighbor probe on IntraBC hash misses
          // (modeled on libaom rt_intrabc_miss_mode = 1..2).
          static const int kProbeOffsets[13][2] = {
            { 0, 0 },  { -1, 0 }, { 1, 0 },  { 0, -1 }, { 0, 1 },
            { -2, 0 }, { 2, 0 },  { 0, -2 }, { 0, 2 },  { -1, -1 },
            { -1, 1 }, { 1, -1 }, { 1, 1 },
          };
          const int num_probes = (miss_mode == 2) ? 13 : 1;
          const struct buf_2d *const src_buf = fullms_params.ms_buffers.src;
          const struct buf_2d *const ref_buf = fullms_params.ms_buffers.ref;
          for (int k = 0; k < num_probes; ++k) {
            const FULLPEL_MV probe_mv = { start_mv.row + kProbeOffsets[k][0],
                                          start_mv.col + kProbeOffsets[k][1] };
            if (!is_bv_valid_nonrd(&probe_mv, cm, xd, mi_row, mi_col, bsize,
                                   &fullms_params)) {
              continue;
            }
            const uint16_t *const ref_ptr =
                &ref_buf->buf[probe_mv.row * ref_buf->stride + probe_mv.col];
            unsigned int probe_sse = 0;
            (void)fullms_params.vfp->vf(src_buf->buf, src_buf->stride, ref_ptr,
                                        ref_buf->stride, &probe_sse);
            const MV probe_dv = get_mv_from_fullmv(&probe_mv);
            const int mv_err =
                av2_get_mv_err_cost(&probe_dv, &fullms_params.mv_cost_params);
            if ((int64_t)probe_sse + mv_err < bestsme) {
              bestsme = (int)probe_sse + mv_err;
              full_mv.as_fullmv = probe_mv;
            }
          }
        }
        if (bestsme != INT_MAX &&
            is_bv_valid_nonrd(&full_mv.as_fullmv, cm, xd, mi_row, mi_col, bsize,
                              &fullms_params)) {
          const MV cand_dv = get_mv_from_fullmv(&full_mv.as_fullmv);
          const int dist_only =
              bestsme -
              av2_get_mv_err_cost(&cand_dv, &fullms_params.mv_cost_params);
          const int ref_bv_rate = av2_get_ref_bv_rate_cost(
              /*intrabc_mode=*/0, /*intrabc_drl_idx=*/0,
              cm->features.max_bvp_drl_bits, x,
              fullms_params.mv_cost_params.mv_costs->errorperbit,
              mbmi_ext->ref_mv_count[INTRA_FRAME]);
          const int total_sme = bestsme + ref_bv_rate;
          if (dist_only >= 0 && total_sme < best_cand_cost) {
            best_cand_cost = total_sme;
            best_intrabc_mode = 0;
            best_intrabc_drl_idx = 0;
            best_ref_bv = region_ref_bv;
            best_dv = cand_dv;
            best_mv_limits = fullms_params.mv_limits;
          }
        }
      }
    }
  }

  bool ibc_won = false;
  if (best_cand_cost < INT_MAX) {
    uint8_t saved_blk_skip[MAX_MB_PLANE][MAX_MIB_SIZE * MAX_MIB_SIZE];
    TX_TYPE saved_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
    CctxType saved_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
    for (int i = 0; i < num_planes; ++i) {
      const int num_blk_plane =
          (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
      memcpy(saved_blk_skip[i], ctx->blk_skip[i],
             sizeof(uint8_t) * num_blk_plane);
    }
    av2_copy_array(saved_tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
    av2_copy_array(saved_cctx_type_map, ctx->cctx_type_map,
                   ctx->num_4x4_blk_chroma);

    init_mbmi_nonrd(mbmi, DC_PRED, INTRA_FRAME, NONE_FRAME, cm, xd);
    mbmi->use_intrabc[0] = 1;
    mbmi->intrabc_mode = best_intrabc_mode;
    mbmi->intrabc_drl_idx = best_intrabc_drl_idx;
    mbmi->ref_bv = best_ref_bv;
    mbmi->mv[0].as_mv = best_dv;
    mbmi->interp_fltr = BILINEAR;
    mbmi->morph_pred = 0;
    set_default_intraBC_bv_precision(cm, mbmi);
    set_default_precision_set(cm, mbmi, bsize);
    set_most_probable_mv_precision(cm, mbmi, bsize);

    const int is_pb_prec_active =
        is_intraBC_bv_precision_active(cm, mbmi->intrabc_mode);
    if (is_pb_prec_active && (best_dv.row & 7) == 0 && (best_dv.col & 7) == 0) {
      const MvSubpelPrecision one_pel = av2_intraBc_precision_sets.precision[0];
      FullMvLimits one_pel_full_limits = best_mv_limits;
      av2_set_mv_search_range(&one_pel_full_limits, &best_ref_bv.as_mv,
                              one_pel);
      SubpelMvLimits one_pel_limits;
      av2_set_subpel_mv_search_range(&one_pel_limits, &one_pel_full_limits,
                                     &best_ref_bv.as_mv, one_pel);
      if (is_sub_pel_bv_valid(best_dv, cm, xd, mi_row, mi_col, bsize,
                              &one_pel_limits, &one_pel_full_limits, one_pel)) {
        mbmi->pb_mv_precision = one_pel;
      }
    }

    int rate_mode =
        x->mode_costs.intrabc_cost[get_intrabc_ctx(xd)][1] +
        skip_interintra_cost +
        x->mode_costs.intrabc_mode_cost[mbmi->intrabc_mode] +
        av2_get_intrabc_drl_idx_cost(cm->features.max_bvp_drl_bits + 1,
                                     mbmi->intrabc_drl_idx);
    if (!mbmi->intrabc_mode) {
      rate_mode += av2_intrabc_mv_bit_cost(
          &mbmi->mv[0].as_mv, &mbmi->ref_bv.as_mv, &x->dv_costs,
          MV_COST_WEIGHT_SUB, mbmi->pb_mv_precision);
    }
    if (is_pb_prec_active) {
      const int prec_idx =
          av2_intraBc_precision_to_index[mbmi->pb_mv_precision];
      rate_mode += x->mode_costs.intrabc_bv_precision_cost[0][prec_idx];
    }
    if (av2_allow_intrabc_morph_pred(cm)) {
      rate_mode += x->mode_costs.morph_pred_cost[get_morph_pred_ctx(xd)][0];
    }

    const int lossless = xd->lossless[mbmi->segment_id];
    const TX_SIZE unsplit_tx_size =
        lossless ? TX_4X4
                 : tx_size_from_tx_mode(
                       bsize, x->txfm_search_params.tx_mode_search_type);
    const bool signal_tx_partition =
        x->txfm_search_params.tx_mode_search_type == TX_MODE_SELECT &&
        block_signals_txsize(bsize) && !lossless;
    const TX_PARTITION_TYPE nonskip_tx_partition =
        (signal_tx_partition && sf->tx_sf.enable_tx_partition)
            ? get_tx_partition_nonrd(bsize)
            : TX_PARTITION_NONE;
    const TX_SIZE coded_tx_size =
        (nonskip_tx_partition == TX_PARTITION_NONE)
            ? unsplit_tx_size
            : get_tx_partition_one_size(nonskip_tx_partition,
                                        max_txsize_rect_lookup[bsize]);
    const TX_SIZE yrd_tx_size = AVMMIN(TX_16X16, coded_tx_size);

    set_tx_partition_nonrd(xd, mbmi, bsize, TX_PARTITION_NONE, unsplit_tx_size);
    const int skip_ctx = av2_get_skip_txfm_context(xd);
    const int min_skip_cost = AVMMIN(x->mode_costs.skip_txfm_cost[skip_ctx][0],
                                     x->mode_costs.skip_txfm_cost[skip_ctx][1]);

    if (RDCOST(x->rdmult, rate_mode + min_skip_cost, 0) < best_rdc->rdcost) {
      av2_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                    AVM_PLANE_Y, num_planes - 1,
                                    x->tip_unit_hooks);

      RD_STATS rdc_y;
      int skippable_y = 0;
      av2_block_yrd(cpi, x, &rdc_y, &skippable_y, bsize, yrd_tx_size, DCT_DCT,
                    /*is_inter=*/1);
      PRIMARY_TX_TYPE tx_type_y = DCT_DCT;
      const TxSetType tx_set = av2_get_ext_tx_set_type(
          coded_tx_size, /*is_inter=*/1, cm->features.reduced_tx_set_used);
      if (!skippable_y && !lossless && !cpi->oxcf.txfm_cfg.use_inter_dct_only &&
          av2_ext_tx_used[tx_set][IDTX] && w >= 8 && h >= 8) {
        RD_STATS idtx_rdc_y;
        int idtx_skip_y = 0;
        av2_block_yrd(cpi, x, &idtx_rdc_y, &idtx_skip_y, bsize, yrd_tx_size,
                      IDTX, /*is_inter=*/1);
        if (!idtx_skip_y && idtx_rdc_y.rdcost < rdc_y.rdcost) {
          rdc_y = idtx_rdc_y;
          tx_type_y = IDTX;
        }
      }

      RD_STATS rdc_uv;
      int skippable_uv = 1;
      block_uvrd_nonrd(cpi, x, ctx, &rdc_uv, &skippable_uv, bsize,
                       /*use_color_sens=*/false);

      RD_STATS this_rdc;
      const int is_skippable = finalize_inter_rd_nonrd(
          cm, x, ctx, bsize, rate_mode, skip_ctx, &rdc_y, skippable_y,
          /*keep_subblk_skip_y=*/
          (tx_type_y == IDTX && yrd_tx_size == coded_tx_size), &rdc_uv,
          skippable_uv, tx_type_y, nonskip_tx_partition, unsplit_tx_size,
          signal_tx_partition, &this_rdc);

      if (this_rdc.rdcost < best_rdc->rdcost) {
        save_best_mode_context(cm, x, ctx, &this_rdc, is_skippable, best_rdc,
                               best_mbmi, best_skip);
        ibc_won = true;
      }
    }

    if (!ibc_won) {
      for (int i = 0; i < num_planes; ++i) {
        const int num_blk_plane =
            (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
        memcpy(ctx->blk_skip[i], saved_blk_skip[i],
               sizeof(uint8_t) * num_blk_plane);
      }
      av2_copy_array(ctx->tx_type_map, saved_tx_type_map, ctx->num_4x4_blk);
      av2_copy_array(ctx->cctx_type_map, saved_cctx_type_map,
                     ctx->num_4x4_blk_chroma);
    }
  }

  if (!ibc_won) {
    mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = ori_ref_bv0;
  }
  for (int i = 0; i < num_planes; ++i) {
    xd->plane[i].pre[0] = ori_pre[i];
  }
  return ibc_won;
}

// =============================================================================
// Section 5: Public Non-RD Mode Search Entry Points
// =============================================================================

void av2_nonrd_pick_intra_mode(AV2_COMP *cpi, MACROBLOCK *x, RD_STATS *rd_cost,
                               BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  const AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;

  set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);
  av2_invalid_rd_stats(rd_cost);

  RD_STATS best_rdc;
  av2_invalid_rd_stats(&best_rdc);
  MB_MODE_INFO best_mbmi;
  av2_zero(best_mbmi);
  best_mbmi.mode = MODE_INVALID;
  int best_skip = 0;

  // 1. Evaluate reduced intra mode set (DC, V, H, SMOOTH + intra FSC), then
  //    screen Chroma-from-Luma (UV_CFL_PRED) on the winning luma intra mode.
  //    Note intra_mode_info_cost_y() already includes intrabc_cost[ctx][0] when
  //    IntraBC is allowed on the block.
  int best_y_rate = 0;
  int64_t best_y_dist = 0;
  int64_t best_y_sse = 0;
  evaluate_intra_modes_nonrd(cpi, x, bsize, /*is_intra_frame=*/true,
                             /*ref_cost_intra=*/0,
                             /*no_skip_mode_cost=*/0, /*intra_cost_penalty=*/0,
                             &best_rdc, &best_mbmi, &best_skip, ctx,
                             &best_y_rate, &best_y_dist, &best_y_sse);
  search_cfl_uv_nonrd(cpi, x, bsize, best_y_rate, best_y_dist, best_y_sse,
                      &best_rdc, &best_mbmi, &best_skip, ctx);

  // 2. Evaluate luma Palette mode when enabled (pruned to <= 16x16 blocks with
  //    source_variance > 200 when prune_palette_search_nonrd is set, matching
  //    libaom av1_nonrd_pick_intra_mode).
  bool try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av2_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        bsize);
  if (sf->rt_sf.prune_palette_search_nonrd > 0) {
    try_palette = try_palette && block_size_wide[bsize] <= 16 &&
                  block_size_high[bsize] <= 16 && x->source_variance > 200;
  }
  bool palette_selected = false;
  if (try_palette) {
    palette_selected =
        search_palette_luma_nonrd(cpi, x, bsize, /*extra_header_cost=*/0, ctx,
                                  &best_rdc, &best_mbmi, &best_skip);
  }

  // 3. Evaluate IntraBC when enabled (respecting prune_intrabc_on_keyframe and
  //    rt_prune_intrabc_nonrd, matching libaom av1_nonrd_pick_intra_mode).
  bool try_intrabc = cpi->oxcf.kf_cfg.enable_intrabc &&
                     av2_allow_intrabc(cm, xd, bsize) &&
                     xd->tree_type != CHROMA_PART &&
                     (!sf->rt_sf.rt_prune_intrabc_nonrd || palette_selected);
  if (frame_is_intra_only(cm) && sf->rt_sf.prune_intrabc_on_keyframe) {
    try_intrabc = try_intrabc && block_size_wide[bsize] <= 16 &&
                  block_size_high[bsize] <= 16 && x->source_variance > 50;
  }
  if (try_intrabc) {
    const int min_ibc_cost = x->mode_costs.intrabc_cost[get_intrabc_ctx(xd)][1];
    if (RDCOST(x->rdmult, min_ibc_cost, 0) < best_rdc.rdcost) {
      (void)search_intrabc_nonrd(cpi, x, bsize, /*skip_interintra_cost=*/0, ctx,
                                 &best_rdc, &best_mbmi, &best_skip);
    }
  }

  if (best_mbmi.mode == MODE_INVALID || best_rdc.rdcost >= INT64_MAX) {
    av2_invalid_rd_stats(rd_cost);
    return;
  }

  if (xd->lossless[best_mbmi.segment_id]) {
    best_skip = 0;
    const int num_planes = av2_num_planes(cm);
    for (int i = 0; i < num_planes; ++i) {
      const int num_blk_plane =
          (i == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
      memset(ctx->blk_skip[i], 0, sizeof(uint8_t) * num_blk_plane);
    }
  }
  restore_winning_mode_context(cm, x, bsize, ctx, rd_cost, &best_rdc,
                               &best_mbmi, best_skip);
}

void av2_nonrd_pick_inter_mode_sb_seg_skip(AV2_COMP *cpi,
                                           struct TileDataEnc *tile_data,
                                           MACROBLOCK *x, RD_STATS *rd_cost,
                                           BLOCK_SIZE bsize,
                                           PICK_MODE_CONTEXT *ctx) {
  (void)tile_data;
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const ModeCosts *const mode_costs = &x->mode_costs;
  const int num_planes = av2_num_planes(cm);

  assert(segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP));
  assert(xd->sbi->sb_active_mode == BRU_ACTIVE_SB);

  for (int i = 0; i < SINGLE_REF_FRAMES; ++i) {
    x->pred_sse[i] = INT_MAX;
    x->pred_mv_sad[i] = INT_MAX;
  }
  av2_collect_neighbors_ref_counts(xd);

  unsigned int ref_costs_single[SINGLE_REF_FRAMES];
  estimate_ref_frame_costs_nonrd(cm, xd, mode_costs, mbmi->segment_id,
                                 ref_costs_single, NULL);

  // Under SEG_LVL_SKIP no reference symbol is written; the bitstream and
  // decoder unconditionally infer get_closest_pastcur_ref_or_ref0(cm).
  const MV_REFERENCE_FRAME last_frame = get_closest_pastcur_ref_or_ref0(cm);

  init_mbmi_nonrd(mbmi, GLOBALMV, last_frame, NONE_FRAME, cm, xd);
  if (is_tip_ref_frame(last_frame)) {
    mbmi->mv[0].as_int = 0;
  } else {
    mbmi->mv[0] = get_warp_motion_vector(xd, &cm->global_motion[last_frame],
                                         cm->features.fr_mv_precision, bsize,
                                         xd->mi_col, xd->mi_row);
  }
  set_tx_partition_nonrd(xd, mbmi, bsize, TX_PARTITION_NONE,
                         max_txsize_lookup[bsize]);
  mbmi->skip_txfm[0] = 1;
  mbmi->skip_txfm[1] = 1;
  x->txfm_search_info.skip_txfm = 1;

  const InterpFilter frame_filter = cm->features.interp_filter;
  const bool force_multitap_sharp =
      opfl_allowed_cur_pred_mode(cm, xd, mbmi) || mbmi->refinemv_flag ||
      (cm->bru.enabled && xd->sbi->sb_active_mode != BRU_ACTIVE_SB) ||
      is_tip_ref_frame(last_frame);
  InterpFilter best_filter =
      force_multitap_sharp
          ? MULTITAP_SHARP
          : ((frame_filter == SWITCHABLE) ? EIGHTTAP_REGULAR : frame_filter);
  int filter_rate = 0;
  if (!force_multitap_sharp && frame_filter == SWITCHABLE &&
      av2_is_interp_needed(cm, xd)) {
    int best_rs = INT_MAX;
    for (int i = 0; i < SWITCHABLE_FILTERS; ++i) {
      mbmi->interp_fltr = i;
      const int rs = av2_get_switchable_rate(x, xd, frame_filter);
      if (rs < best_rs) {
        best_rs = rs;
        best_filter = i;
      }
    }
    filter_rate = best_rs;
  }
  mbmi->interp_fltr = best_filter;

  for (int plane = 0; plane < num_planes; ++plane) {
    const int num_blk =
        (plane == AVM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    for (int i = 0; i < num_blk; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[plane], i, 1);
    }
  }
  for (int i = 0; i < ctx->num_4x4_blk; ++i) {
    xd->tx_type_map[i] = MAKE_TX_TYPE_FROM_PRIMARY_TX_TYPE(DCT_DCT);
  }
  for (int i = 0; i < ctx->num_4x4_blk_chroma; ++i) {
    xd->cctx_type_map[i] = CCTX_NONE;
  }

  int64_t rate64 = filter_rate;
  if (ref_costs_single[last_frame] < (unsigned int)INT_MAX) {
    rate64 += (int64_t)ref_costs_single[last_frame];
  }
  if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
    rate64 +=
        mode_costs->comp_inter_cost[av2_get_reference_mode_context(cm, xd)][0];
  }
  const int rate = (int)AVMMIN(rate64, (int64_t)(INT_MAX / 2));

  RD_STATS this_rdc;
  av2_init_rd_stats(&this_rdc);
  this_rdc.rate = rate;
  this_rdc.dist = 0;
  this_rdc.sse = 0;
  this_rdc.skip_txfm = 1;
  this_rdc.rdcost = RDCOST(x->rdmult, rate, 0);

  RD_STATS best_rdc;
  MB_MODE_INFO best_mbmi;
  int best_skip = 1;
  save_best_mode_context(cm, x, ctx, &this_rdc, /*this_skip=*/1, &best_rdc,
                         &best_mbmi, &best_skip);
  restore_winning_mode_context(cm, x, bsize, ctx, rd_cost, &best_rdc,
                               &best_mbmi, best_skip);
}

// Fast path for stationary blocks where variance-based partitioning already
// verified an exact zero-MV Y/U/V match against x->vbp_ref_frame: selects a
// zero-MV single-reference mode (NEARMV or GLOBALMV) without running motion
// search, interpolation filter search, or residual transforms.
static bool try_exact_zeromv_skip_nonrd(
    const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
    const unsigned int *ref_costs_single, const int *comp_inter_cost,
    bool reference_mode_select, int no_skip_mode_cost, TX_SIZE inter_tx_size,
    RD_STATS *rd_cost, PICK_MODE_CONTEXT *ctx) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const MV_REFERENCE_FRAME rf0 = x->vbp_ref_frame;

  if (xd->tree_type != SHARED_PART || rf0 < 0 ||
      rf0 >= cm->ref_frames_info.num_total_refs ||
      rf0 >= INTER_REFS_PER_FRAME || is_tip_ref_frame(rf0) ||
      !is_ref_usable_nonrd(cm, rf0) ||
      ref_costs_single[rf0] >= (unsigned int)INT_MAX ||
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_GLOBALMV) ||
      av2_get_scaled_ref_frame(cpi, rf0) != NULL ||
      av2_is_scaled(get_ref_scale_factors_const(cm, rf0))) {
    return false;
  }

  mbmi->mode = NEARMV;
  mbmi->skip_mode = 0;
  mbmi->use_intrabc[0] = 0;
  mbmi->ref_frame[0] = rf0;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi_ext->mode_context[rf0] = 0;
  mbmi_ext->ref_mv_count[rf0] = UINT8_MAX;
  av2_find_mv_refs(cm, xd, mbmi, rf0, mbmi_ext->ref_mv_count, xd->ref_mv_stack,
                   xd->weight, NULL, mbmi_ext->global_mvs, xd->warp_param_stack,
                   MAX_WARP_REF_CANDIDATES, xd->valid_num_warp_candidates);
  av2_find_mode_ctx(cm, xd, mbmi_ext->mode_context, rf0);
  av2_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, rf0);

  const uint8_t ref_frame_type = av2_ref_frame_type(mbmi->ref_frame);
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  PREDICTION_MODE chosen_mode = MODE_INVALID;
  uint8_t chosen_drl = 0;

  const int_mv near0_mv =
      (ref_mv_count > 0) ? mbmi_ext->ref_mv_stack[ref_frame_type][0].this_mv
                         : mbmi_ext->global_mvs[rf0];
  if (near0_mv.as_int == 0) {
    chosen_mode = NEARMV;
    chosen_drl = 0;
  } else if (cm->features.max_drl_bits > 0 && ref_mv_count > 1 &&
             mbmi_ext->ref_mv_stack[ref_frame_type][1].this_mv.as_int == 0) {
    chosen_mode = NEARMV;
    chosen_drl = 1;
  } else {
    const int_mv gmv = get_warp_motion_vector(xd, &cm->global_motion[rf0],
                                              cm->features.fr_mv_precision,
                                              bsize, xd->mi_col, xd->mi_row);
    if (gmv.as_int == 0) {
      chosen_mode = GLOBALMV;
      chosen_drl = 0;
    }
  }
  if (chosen_mode == MODE_INVALID) {
    return false;
  }

  init_mbmi_nonrd(mbmi, chosen_mode, rf0, NONE_FRAME, cm, xd);
  mbmi->ref_mv_idx[0] = chosen_drl;
  mbmi->mv[0].as_int = 0;
  set_ref_ptrs(cm, xd, rf0, NONE_FRAME);

  const FULLPEL_MV zero_fmv = { 0, 0 };
  if (!av2_is_fullmv_in_range(&x->mv_limits, zero_fmv,
                              cm->features.fr_mv_precision) ||
      !bru_is_valid_inter(cm, xd) ||
      (chosen_mode == GLOBALMV && is_nontrans_global_motion(xd, mbmi))) {
    return false;
  }

  const InterpFilter frame_filter = cm->features.interp_filter;
  const bool force_multitap_sharp =
      opfl_allowed_cur_pred_mode(cm, xd, mbmi) || mbmi->refinemv_flag ||
      (cm->bru.enabled && xd->sbi->sb_active_mode != BRU_ACTIVE_SB);
  mbmi->interp_fltr =
      force_multitap_sharp
          ? MULTITAP_SHARP
          : ((frame_filter == SWITCHABLE) ? EIGHTTAP_REGULAR : frame_filter);
  const int filter_rate =
      force_multitap_sharp ? 0 : av2_get_switchable_rate(x, xd, frame_filter);

  x->pred_sse[rf0] = 0;
  const int16_t mode_ctx =
      av2_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
  int rate_mode = cost_inter_mode_nonrd(cm, x, mbmi, mode_ctx) + filter_rate +
                  no_skip_mode_cost + (int)ref_costs_single[rf0];
  if (reference_mode_select) {
    rate_mode += comp_inter_cost[0];
  }

  set_tx_partition_nonrd(xd, mbmi, bsize, TX_PARTITION_NONE, inter_tx_size);
  const int skip_ctx = av2_get_skip_txfm_context(xd);

  RD_STATS zero_rdc_y, zero_rdc_uv, this_rdc;
  av2_init_rd_stats(&zero_rdc_y);
  av2_init_rd_stats(&zero_rdc_uv);
  zero_rdc_y.skip_txfm = 1;
  zero_rdc_uv.skip_txfm = 1;
  const int is_skippable = finalize_inter_rd_nonrd(
      cm, x, ctx, bsize, rate_mode, skip_ctx, &zero_rdc_y, /*skippable_y=*/1,
      /*keep_subblk_skip_y=*/false, &zero_rdc_uv, /*skippable_uv=*/1, DCT_DCT,
      TX_PARTITION_NONE, inter_tx_size, /*signal_tx_partition=*/false,
      &this_rdc);
  assert(is_skippable == 1);

  RD_STATS best_rdc;
  MB_MODE_INFO best_mbmi;
  int best_skip = 1;
  save_best_mode_context(cm, x, ctx, &this_rdc, is_skippable, &best_rdc,
                         &best_mbmi, &best_skip);
  restore_winning_mode_context(cm, x, bsize, ctx, rd_cost, &best_rdc,
                               &best_mbmi, best_skip);
  return true;
}

void av2_nonrd_pick_inter_mode_sb(AV2_COMP *cpi, struct TileDataEnc *tile_data,
                                  MACROBLOCK *x, RD_STATS *rd_cost,
                                  BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  (void)tile_data;
  AV2_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  struct macroblockd_plane *const pd = &xd->plane[AVM_PLANE_Y];
  const ModeCosts *const mode_costs = &x->mode_costs;
  const int num_planes = av2_num_planes(cm);

  assert(xd->sbi->sb_active_mode == BRU_ACTIVE_SB);

  set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);

  x->joint_mvd_pred.num_filled = 0;
  for (int i = 0; i < SINGLE_REF_FRAMES; ++i) {
    x->pred_sse[i] = INT_MAX;
    x->pred_mv_sad[i] = INT_MAX;
  }
  av2_invalid_rd_stats(rd_cost);

  av2_collect_neighbors_ref_counts(xd);

  unsigned int ref_costs_single[SINGLE_REF_FRAMES];
  unsigned int ref_costs_comp[MAX_COMPOUND_REF_INDEX][MAX_COMPOUND_REF_INDEX];
  estimate_ref_frame_costs_nonrd(cm, xd, mode_costs, mbmi->segment_id,
                                 ref_costs_single, ref_costs_comp);
  const int *const comp_inter_cost =
      mode_costs->comp_inter_cost[av2_get_reference_mode_context(cm, xd)];
  const bool comp_ref_allowed =
      is_comp_ref_allowed(bsize) &&
      cm->current_frame.reference_mode != SINGLE_REFERENCE;
  const bool reference_mode_select =
      (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT);
  const int skip_mode_ctx = av2_get_skip_mode_context(xd);
  const int no_skip_mode_cost =
      is_skip_mode_allowed(cm, xd)
          ? mode_costs->skip_mode_cost[skip_mode_ctx][0]
          : 0;
  const int lossless = xd->lossless[mbmi->segment_id];
  xd->reduced_tx_part_set = cm->seq_params.reduced_tx_part_set;
  const TX_SIZE inter_tx_size =
      lossless ? TX_4X4
               : tx_size_from_tx_mode(
                     bsize, x->txfm_search_params.tx_mode_search_type);

  if (x->force_zeromv_skip_for_sb == 2 && !lossless &&
      try_exact_zeromv_skip_nonrd(cpi, x, bsize, ref_costs_single,
                                  comp_inter_cost, reference_mode_select,
                                  no_skip_mode_cost, inter_tx_size, rd_cost,
                                  ctx)) {
    return;
  }
  if (x->source_variance == UINT_MAX) {
    x->source_variance = av2_high_get_sby_perpixel_variance(
        cpi, &x->plane[0].src, bsize, xd->bd);
  }

  const int64_t num_pels =
      (int64_t)block_size_wide[bsize] * block_size_high[bsize];

  // Identify available inter reference frames (ranked 0 .. num_total_refs - 1).
  int active_refs[INTER_REFS_PER_FRAME];
  int num_active_refs = 0;
  int max_refs_to_check = INTER_REFS_PER_FRAME;
  if (sf->rt_sf.nonrd_prune_ref_frame_search >= 2) {
    max_refs_to_check = 2;
  } else if (sf->rt_sf.nonrd_prune_ref_frame_search == 1) {
    max_refs_to_check = 3;
  }

  int_mv frame_mv[MB_MODE_COUNT][INTER_REFS_PER_FRAME];
  struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE];
  bool ref_mv_ready[INTER_REFS_PER_FRAME] = { false };
  memset(frame_mv, 0, sizeof(frame_mv));

  for (int rf = 0;
       rf < cm->ref_frames_info.num_total_refs && rf < INTER_REFS_PER_FRAME &&
       num_active_refs < max_refs_to_check;
       ++rf) {
    if (!is_ref_usable_nonrd(cm, rf)) continue;
    if (num_active_refs > 0 && num_pels < 16 * 16 &&
        sf->rt_sf.nonrd_prune_ref_frame_search >= 1) {
      continue;
    }
    active_refs[num_active_refs++] = rf;
  }

  // Always ensure VBP's selected reference (x->vbp_ref_frame) and the active
  // long-term reference (when rtc_use_lt_ref is enabled) are included in
  // active_refs[]. References are ranked by temporal distance and base_qindex,
  // so a low-qindex long-term reference is not necessarily ranked last.
  int lt_ref = NONE_FRAME;
  if (sf->rt_sf.rtc_use_lt_ref) {
    int max_dist = 2;
    for (int rf = 0;
         rf < cm->ref_frames_info.num_total_refs && rf < INTER_REFS_PER_FRAME;
         ++rf) {
      if (is_ref_usable_nonrd(cm, rf) &&
          cm->ref_frames_info.ref_frame_distance[rf] > max_dist) {
        max_dist = cm->ref_frames_info.ref_frame_distance[rf];
        lt_ref = rf;
      }
    }
  }
  const int special_refs[2] = { x->vbp_ref_frame, lt_ref };
  for (int s = 0; s < 2; ++s) {
    const int rf = special_refs[s];
    if (rf < 0 || rf >= cm->ref_frames_info.num_total_refs ||
        rf >= INTER_REFS_PER_FRAME || num_active_refs >= INTER_REFS_PER_FRAME) {
      continue;
    }
    if (!is_ref_usable_nonrd(cm, rf)) continue;
    bool already_added = false;
    for (int i = 0; i < num_active_refs; ++i) {
      if (active_refs[i] == rf) {
        already_added = true;
        break;
      }
    }
    if (!already_added) {
      active_refs[num_active_refs++] = rf;
    }
  }

  if (num_active_refs == 0) {
    for (int rf = 0;
         rf < cm->ref_frames_info.num_total_refs && rf < INTER_REFS_PER_FRAME;
         ++rf) {
      if (cm->bru.enabled && rf == cm->bru.update_ref_idx) continue;
      if (get_ref_frame_yv12_buf(cm, rf) == NULL) continue;
      active_refs[num_active_refs++] = rf;
      break;
    }
  }

  if (num_active_refs > 0) {
    const int rf0 = active_refs[0];
    av2_find_best_ref_mvs_from_stack(x, cm, cpi, mbmi, rf0, frame_mv, yv12_mb,
                                     bsize);
    ref_mv_ready[rf0] = true;
  }

  // Build candidate list:
  //   1. Primary & secondary refs: NEARMV (DRL 0..1), GLOBALMV, NEWMV
  //   2. Compound averaging (COMPOUND_AVERAGE, CWP_EQUAL, comp_group_idx=0):
  //      NEAR_NEARMV, GLOBAL_GLOBALMV on (active_refs[0], active_refs[1..2])
  NonRdInterCandidate candidates[NONRD_MAX_INTER_CANDIDATES];
  int num_candidates = 0;

  for (int idx = 0; idx < num_active_refs &&
                    num_candidates + 4 <= NONRD_MAX_INTER_CANDIDATES;
       ++idx) {
    const MV_REFERENCE_FRAME rf = active_refs[idx];
    const bool is_primary_or_special_ref =
        (idx == 0 || rf == x->vbp_ref_frame || rf == lt_ref);
    candidates[num_candidates++] =
        (NonRdInterCandidate){ NEARMV, rf, NONE_FRAME, 0 };
    if (cm->features.max_drl_bits > 0 &&
        (is_primary_or_special_ref ||
         !sf->rt_sf.nonrd_prune_ref_frame_search)) {
      if (!ref_mv_ready[rf] ||
          (mbmi_ext->ref_mv_count[rf] > 1 &&
           mbmi_ext->ref_mv_stack[rf][1].this_mv.as_int !=
               mbmi_ext->ref_mv_stack[rf][0].this_mv.as_int)) {
        candidates[num_candidates++] =
            (NonRdInterCandidate){ NEARMV, rf, NONE_FRAME, 1 };
      }
    }
    candidates[num_candidates++] =
        (NonRdInterCandidate){ GLOBALMV, rf, NONE_FRAME, 0 };
    const bool allow_newmv_on_ref =
        is_primary_or_special_ref || num_pels >= 16 * 16;
    if (allow_newmv_on_ref) {
      candidates[num_candidates++] =
          (NonRdInterCandidate){ NEWMV, rf, NONE_FRAME, 0 };
    }
  }

  if (sf->rt_sf.use_comp_ref_nonrd && comp_ref_allowed &&
      num_active_refs >= 2 && num_pels >= 16 * 16) {
    const MV_REFERENCE_FRAME rf0 = active_refs[0];
    if (rf0 >= 0 && rf0 < RANKED_REF0_TO_PRUNE) {
      const int max_comp_sec = AVMMIN(num_active_refs, 3);
      for (int idx = 1; idx < max_comp_sec &&
                        num_candidates + 2 <= NONRD_MAX_INTER_CANDIDATES;
           ++idx) {
        const MV_REFERENCE_FRAME rf1 = active_refs[idx];
        if (rf1 <= rf0 ||
            rf1 >= cm->ref_frames_info.num_valid_refs_with_restricted_ref ||
            ref_costs_comp[rf0][rf1] >= (unsigned int)INT_MAX) {
          continue;
        }
        if (!cpi->oxcf.ref_frm_cfg.enable_onesided_comp &&
            cpi->all_one_sided_refs) {
          break;
        }
        candidates[num_candidates++] =
            (NonRdInterCandidate){ NEAR_NEARMV, rf0, rf1, 0 };
        candidates[num_candidates++] =
            (NonRdInterCandidate){ GLOBAL_GLOBALMV, rf0, rf1, 0 };
      }
    }
  }

  const bool is_screen = is_screen_content_nonrd(cpi);
  const bool boosted_seg = is_boosted_segment_nonrd(cpi, mbmi->segment_id);
  // cm->features.tx_mode is only finalized after the tiles are encoded, so use
  // the search-time transform mode, as the RD transform search does.
  const bool signal_tx_partition =
      x->txfm_search_params.tx_mode_search_type == TX_MODE_SELECT &&
      block_signals_txsize(bsize) && !lossless;

  // Quantizer-adaptive distortion bounds (16x SSE) under which a skippable mode
  // stops the search of secondary references and NEWMV: scaling by qstep_px^2
  // keeps the early-skip threshold near quantization MSE across all bitrates
  // and bit depths instead of stopping early at high quality.
  const int64_t qstep_px = get_luma_qstep_px_nonrd(x);
  const int64_t qstep_sq = qstep_px * qstep_px;
  const int64_t base_skip_dist_thresh =
      is_screen ? AVMMIN(num_pels << 6, num_pels * qstep_sq)
                : (num_pels * qstep_sq);
  const int64_t skip_dist_thresh =
      boosted_seg ? (base_skip_dist_thresh >> 1) : base_skip_dist_thresh;
  const int64_t newmv_skip_dist_thresh =
      is_screen ? 0 : (base_skip_dist_thresh >> 2);
  // dequant_QTX is scaled in Q6 (QUANT_TABLE_BITS = 3 on top of Q3), and
  // filter_rdc.sse is normalized to the 8-bit domain by vf: shift by
  // (bd - 8) + 7 + QUANT_TABLE_BITS so norm_q = qstep / 16 (matching libaom).
  const int64_t min_dequant = AVMMIN(x->plane[AVM_PLANE_Y].dequant_QTX[0],
                                     x->plane[AVM_PLANE_Y].dequant_QTX[1]);
  const int64_t norm_q = min_dequant >> ((xd->bd - 8) + 7 + QUANT_TABLE_BITS);
  const int64_t yrd_zero_sse_thresh = norm_q * norm_q;

  uint16_t *const orig_dst_buf = pd->dst.buf;
  const int orig_dst_stride = pd->dst.stride;

  RD_STATS best_rdc;
  av2_invalid_rd_stats(&best_rdc);
  MB_MODE_INFO best_mbmi;
  av2_zero(best_mbmi);
  best_mbmi.mode = MODE_INVALID;
  int best_skip = 0;
  int best_skip_y = 0;
  int best_rate_mode = 0;
  int best_skip_ctx = 0;
  int64_t best_y_dist = 0;
  int64_t best_y_sse = 0;
  int best_uv_rate = 0;
  bool best_unchecked_uv = false;
  bool best_early_term = false;
  bool comp_mv_refs_ready[MODE_CTX_REF_FRAMES] = { false };
  uint8_t ref_color_sens[INTER_REFS_PER_FRAME][2] = { { 0 } };
  bool ref_color_sens_ready[INTER_REFS_PER_FRAME] = { false };
  const bool use_color_sens =
      sf->rt_sf.use_color_sensitivity &&
      sf->part_sf.partition_search_type == VAR_BASED_PARTITION &&
      !cpi->partition_search_skippable_frame &&
      is_inter_ref_frame(x->vbp_ref_frame) && !lossless;

  for (int c_idx = 0; c_idx < num_candidates; ++c_idx) {
    const PREDICTION_MODE this_mode = candidates[c_idx].mode;
    const MV_REFERENCE_FRAME rf0 = candidates[c_idx].rf0;
    const MV_REFERENCE_FRAME rf1 = candidates[c_idx].rf1;
    uint8_t drl_idx = candidates[c_idx].ref_mv_idx;
    const bool is_comp = (rf1 >= 0 && rf1 < INTER_REFS_PER_FRAME);

    if (best_early_term && this_mode != NEARMV && this_mode != GLOBALMV) {
      continue;
    }

    if (best_skip_y && best_rdc.rdcost < INT64_MAX) {
      if (is_comp && best_rdc.dist <= skip_dist_thresh) continue;
      if (rf0 != active_refs[0]) {
        if (this_mode == NEWMV && rf0 != x->vbp_ref_frame &&
            best_rdc.dist <= (skip_dist_thresh << 1)) {
          continue;
        }
        const bool is_aq_cr = (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ);
        const bool preserve_sec_zmv =
            sf->rt_sf.nonrd_preserve_sec_ref_zeromv && !is_screen;
        const int64_t sec_skip_thresh =
            (preserve_sec_zmv && is_aq_cr && this_mode != NEWMV)
                ? (skip_dist_thresh >> 1)
                : skip_dist_thresh;
        const bool keep_special_zmv =
            preserve_sec_zmv ? ((rf0 == x->vbp_ref_frame ||
                                 (rf0 == lt_ref && is_aq_cr &&
                                  best_rdc.dist > (skip_dist_thresh >> 1))) &&
                                this_mode != NEWMV && drl_idx == 0)
                             : (rf0 == x->vbp_ref_frame && x->sb_me_partition &&
                                this_mode == NEARMV && drl_idx == 0);
        if (best_rdc.dist <= sec_skip_thresh && !keep_special_zmv) {
          continue;
        }
      } else if (this_mode == NEWMV && x->source_sad_level <= kLowSad &&
                 best_rdc.dist <= newmv_skip_dist_thresh) {
        continue;
      }
    }

    if (is_screen && sf->rt_sf.nonrd_prune_ref_frame_search && !is_comp &&
        this_mode == NEWMV && rf0 != active_refs[0] &&
        rf0 != x->vbp_ref_frame && best_rdc.rdcost < INT64_MAX &&
        best_y_dist <= (skip_dist_thresh << 2)) {
      continue;
    }

    if (is_screen && sf->rt_sf.skip_newmv_flat_blocks_screen && !is_comp &&
        this_mode == NEWMV && best_skip_y && best_rdc.rdcost < INT64_MAX &&
        best_rdc.dist <= skip_dist_thresh && x->source_sad_level == kZeroSad &&
        rf0 == active_refs[0] && best_mbmi.mv[0].as_int == 0) {
      continue;
    }

    if (!ref_mv_ready[rf0]) {
      av2_find_best_ref_mvs_from_stack(x, cm, cpi, mbmi, rf0, frame_mv, yv12_mb,
                                       bsize);
      ref_mv_ready[rf0] = true;
    }
    if (is_comp && !ref_mv_ready[rf1]) {
      av2_find_best_ref_mvs_from_stack(x, cm, cpi, mbmi, rf1, frame_mv, yv12_mb,
                                       bsize);
      ref_mv_ready[rf1] = true;
    }

    if (!is_comp && this_mode == NEARMV && drl_idx > 0) {
      if (mbmi_ext->ref_mv_count[rf0] <= drl_idx ||
          mbmi_ext->ref_mv_stack[rf0][drl_idx].this_mv.as_int ==
              mbmi_ext->ref_mv_stack[rf0][0].this_mv.as_int) {
        continue;
      }
    }

    if (!is_comp && this_mode == NEWMV && cm->features.max_drl_bits > 0 &&
        mbmi_ext->ref_mv_count[rf0] > 1) {
      int_mv near0 = mbmi_ext->ref_mv_stack[rf0][0].this_mv;
      int_mv near1 = mbmi_ext->ref_mv_stack[rf0][1].this_mv;
      clamp_mv_for_nonrd(&near0.as_mv, xd);
      clamp_mv_for_nonrd(&near1.as_mv, xd);
      if (frame_mv[NEWMV][rf0].as_int == near1.as_int &&
          frame_mv[NEWMV][rf0].as_int != near0.as_int) {
        drl_idx = 1;
      }
    }

    if (!is_comp && this_mode == GLOBALMV &&
        frame_mv[GLOBALMV][rf0].as_int == frame_mv[NEARMV][rf0].as_int &&
        best_rdc.rdcost < INT64_MAX) {
      continue;
    }

    init_mbmi_nonrd(mbmi, this_mode, rf0, rf1, cm, xd);
    mbmi->ref_mv_idx[0] = drl_idx;
    set_ref_ptrs(cm, xd, rf0, rf1);
    for (int i = 0; i < num_planes; ++i) {
      xd->plane[i].pre[0] = yv12_mb[rf0][i];
      if (is_comp) {
        xd->plane[i].pre[1] = yv12_mb[rf1][i];
      }
    }

    int rate_mv = 0;
    if (!is_comp) {
      if (this_mode == NEWMV) {
        int_mv tmp_mv = frame_mv[NEWMV][rf0];
        if (!combined_motion_search(cpi, x, bsize, &tmp_mv, &rate_mv)) {
          continue;
        }
        if (best_rdc.rdcost < INT64_MAX &&
            (tmp_mv.as_int == frame_mv[NEARMV][rf0].as_int ||
             tmp_mv.as_int == frame_mv[GLOBALMV][rf0].as_int ||
             (cm->features.max_drl_bits > 0 &&
              mbmi_ext->ref_mv_count[rf0] > 1 &&
              tmp_mv.as_int ==
                  mbmi_ext->ref_mv_stack[rf0][1].this_mv.as_int))) {
          continue;
        }
        frame_mv[NEWMV][rf0] = tmp_mv;
        mbmi->mv[0].as_int = tmp_mv.as_int;
      } else if (this_mode == NEARMV && drl_idx > 0) {
        mbmi->mv[0] = mbmi_ext->ref_mv_stack[rf0][drl_idx].this_mv;
        if (!av2_is_fullmv_in_range(&x->mv_limits,
                                    get_fullmv_from_mv(&mbmi->mv[0].as_mv),
                                    cm->features.fr_mv_precision)) {
          continue;
        }
      } else {
        mbmi->mv[0].as_int = frame_mv[this_mode][rf0].as_int;
        if (!av2_is_fullmv_in_range(&x->mv_limits,
                                    get_fullmv_from_mv(&mbmi->mv[0].as_mv),
                                    cm->features.fr_mv_precision)) {
          continue;
        }
      }
    } else {
      const MV_REFERENCE_FRAME comp_rf = av2_ref_frame_type(mbmi->ref_frame);
      if (!comp_mv_refs_ready[comp_rf]) {
        mbmi_ext->mode_context[comp_rf] = 0;
        mbmi_ext->ref_mv_count[comp_rf] = 0;
        av2_find_mode_ctx(cm, xd, mbmi_ext->mode_context, comp_rf);
        comp_mv_refs_ready[comp_rf] = true;
      }
      if (this_mode == NEAR_NEARMV) {
        mbmi->mv[0] = frame_mv[NEARMV][rf0];
        mbmi->mv[1] = frame_mv[NEARMV][rf1];
      } else {
        assert(this_mode == GLOBAL_GLOBALMV);
        mbmi->mv[0] = frame_mv[GLOBALMV][rf0];
        mbmi->mv[1] = frame_mv[GLOBALMV][rf1];
        if (mbmi->mv[0].as_int == frame_mv[NEARMV][rf0].as_int &&
            mbmi->mv[1].as_int == frame_mv[NEARMV][rf1].as_int &&
            best_rdc.rdcost < INT64_MAX) {
          continue;
        }
      }
      if (!av2_is_fullmv_in_range(&x->mv_limits,
                                  get_fullmv_from_mv(&mbmi->mv[0].as_mv),
                                  cm->features.fr_mv_precision) ||
          !av2_is_fullmv_in_range(&x->mv_limits,
                                  get_fullmv_from_mv(&mbmi->mv[1].as_mv),
                                  cm->features.fr_mv_precision)) {
        continue;
      }
    }

    if (!bru_is_valid_inter(cm, xd)) {
      continue;
    }

    RD_STATS filter_rdc;
    unsigned int pred_var = 0;
    av2_init_rd_stats(&filter_rdc);
    search_filter_ref_nonrd(cpi, x, &filter_rdc, &pred_var, bsize, orig_dst_buf,
                            orig_dst_stride);

    if (!is_comp && this_mode == NEARMV) {
      x->pred_sse[rf0] = (unsigned int)filter_rdc.sse;
    }

    const int16_t mode_ctx =
        av2_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
    int rate_mode = cost_inter_mode_nonrd(cm, x, mbmi, mode_ctx) + rate_mv +
                    filter_rdc.rate + no_skip_mode_cost;
    rate_mode +=
        is_comp ? (int)ref_costs_comp[rf0][rf1] : (int)ref_costs_single[rf0];
    if (reference_mode_select) {
      rate_mode += comp_inter_cost[is_comp];
    }
    if (is_comp &&
        (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi))) {
      const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                       cm->seq_params.enable_masked_compound;
      if (masked_compound_used) {
        const int ctx_comp_group_idx = get_comp_group_idx_context(cm, xd);
        rate_mode += mode_costs->comp_group_idx_cost[ctx_comp_group_idx][0];
      }
      if (cm->features.enable_cwp && is_cwp_allowed(mbmi)) {
        rate_mode += av2_get_cwp_idx_cost(mbmi->cwp_idx, cm, x);
      }
    }

    const TX_PARTITION_TYPE cand_tx_partition =
        (signal_tx_partition && sf->tx_sf.enable_tx_partition)
            ? calculate_inter_tx_partition_nonrd(cpi, x, bsize, filter_rdc.sse,
                                                 pred_var)
            : TX_PARTITION_NONE;
    const TX_SIZE cand_coded_tx_size =
        (cand_tx_partition == TX_PARTITION_NONE)
            ? inter_tx_size
            : get_tx_partition_one_size(cand_tx_partition,
                                        max_txsize_rect_lookup[bsize]);
    const TX_SIZE cand_yrd_tx_size = AVMMIN(TX_16X16, cand_coded_tx_size);

    set_tx_partition_nonrd(xd, mbmi, bsize, TX_PARTITION_NONE, inter_tx_size);

    const int skip_ctx = av2_get_skip_txfm_context(xd);
    const int min_skip_cost = AVMMIN(mode_costs->skip_txfm_cost[skip_ctx][0],
                                     mode_costs->skip_txfm_cost[skip_ctx][1]);
    if (RDCOST(x->rdmult, rate_mode + min_skip_cost, 0) >= best_rdc.rdcost) {
      continue;
    }

    RD_STATS this_rdc_y;
    int is_skippable_y = 0;
    if (!lossless && filter_rdc.sse < yrd_zero_sse_thresh) {
      av2_init_rd_stats(&this_rdc_y);
      this_rdc_y.sse = filter_rdc.sse << 4;
      this_rdc_y.dist = filter_rdc.sse << 4;
      this_rdc_y.rate = 0;
      this_rdc_y.skip_txfm = 1;
      this_rdc_y.rdcost = RDCOST(x->rdmult, 0, this_rdc_y.dist);
      is_skippable_y = 1;
    } else {
      av2_block_yrd(cpi, x, &this_rdc_y, &is_skippable_y, bsize,
                    cand_yrd_tx_size, DCT_DCT, /*is_inter=*/1);
    }

    PRIMARY_TX_TYPE tx_type_y = DCT_DCT;
    const TxSetType cand_tx_set_type = av2_get_ext_tx_set_type(
        cand_coded_tx_size, /*is_inter=*/1, cm->features.reduced_tx_set_used);
    const bool allow_inter_idtx =
        (is_screen ||
         (cm->seq_params.enable_fsc && block_size_wide[bsize] <= 16 &&
          block_size_high[bsize] <= 16)) &&
        !lossless && !cpi->oxcf.txfm_cfg.use_inter_dct_only &&
        av2_ext_tx_used[cand_tx_set_type][IDTX];
    if (allow_inter_idtx && !is_skippable_y && block_size_wide[bsize] >= 8 &&
        block_size_high[bsize] >= 8) {
      uint8_t dct_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
      memcpy(dct_blk_skip, x->txfm_search_info.blk_skip[AVM_PLANE_Y],
             sizeof(dct_blk_skip[0]) * ctx->num_4x4_blk);
      RD_STATS idtx_rdc_y;
      int idtx_skippable_y = 0;
      av2_block_yrd(cpi, x, &idtx_rdc_y, &idtx_skippable_y, bsize,
                    cand_yrd_tx_size, IDTX, /*is_inter=*/1);
      if (!idtx_skippable_y && idtx_rdc_y.rdcost < this_rdc_y.rdcost) {
        this_rdc_y = idtx_rdc_y;
        tx_type_y = IDTX;
      } else {
        memcpy(x->txfm_search_info.blk_skip[AVM_PLANE_Y], dct_blk_skip,
               sizeof(dct_blk_skip[0]) * ctx->num_4x4_blk);
      }
    }

    const int min_rate_y =
        rate_mode + min_skip_cost + (is_skippable_y ? 0 : this_rdc_y.rate);
    if (RDCOST(x->rdmult, min_rate_y, this_rdc_y.dist) >= best_rdc.rdcost) {
      continue;
    }

    if (use_color_sens) {
      if (!ref_color_sens_ready[rf0]) {
        set_color_sensitivity_nonrd(cpi, x, bsize, rf0, x->pred_mv_sad[rf0],
                                    yv12_mb, ref_color_sens[rf0]);
        ref_color_sens_ready[rf0] = true;
      }
      if (is_comp) {
        if (!ref_color_sens_ready[rf1]) {
          set_color_sensitivity_nonrd(cpi, x, bsize, rf1, x->pred_mv_sad[rf1],
                                      yv12_mb, ref_color_sens[rf1]);
          ref_color_sens_ready[rf1] = true;
        }
        x->color_sensitivity[0] =
            ref_color_sens[rf0][0] || ref_color_sens[rf1][0];
        x->color_sensitivity[1] =
            ref_color_sens[rf0][1] || ref_color_sens[rf1][1];
      } else {
        x->color_sensitivity[0] = ref_color_sens[rf0][0];
        x->color_sensitivity[1] = ref_color_sens[rf0][1];
      }
    }

    RD_STATS this_rdc_uv;
    int is_skippable_uv = 1;
    block_uvrd_nonrd(cpi, x, ctx, &this_rdc_uv, &is_skippable_uv, bsize,
                     use_color_sens);

    RD_STATS this_rdc;
    const int is_skippable = finalize_inter_rd_nonrd(
        cm, x, ctx, bsize, rate_mode, skip_ctx, &this_rdc_y, is_skippable_y,
        /*keep_subblk_skip_y=*/
        (tx_type_y == IDTX && cand_yrd_tx_size == cand_coded_tx_size),
        &this_rdc_uv, is_skippable_uv, tx_type_y, cand_tx_partition,
        inter_tx_size, signal_tx_partition, &this_rdc);

    if (this_rdc.rdcost < best_rdc.rdcost) {
      save_best_mode_context(cm, x, ctx, &this_rdc, is_skippable, &best_rdc,
                             &best_mbmi, &best_skip);
      best_skip_y = is_skippable_y;
      best_rate_mode = rate_mode;
      best_skip_ctx = skip_ctx;
      best_y_dist = this_rdc_y.dist;
      best_y_sse = this_rdc_y.sse;
      best_uv_rate = this_rdc_uv.rate;
      best_unchecked_uv = use_color_sens && (!x->color_sensitivity[0] ||
                                             !x->color_sensitivity[1]);
      const bool strict_kzerosad_et =
          sf->rt_sf.nonrd_preserve_sec_ref_zeromv && !is_screen &&
          (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ ||
           (is_inter_ref_frame(x->vbp_ref_frame) && x->vbp_ref_frame != rf0));
      if (is_skippable_y && (this_mode == NEARMV || this_mode == GLOBALMV) &&
          mbmi->mv[0].as_int == 0 &&
          (this_rdc_y.sse == 0 ||
           (x->source_sad_level == kZeroSad &&
            (!strict_kzerosad_et ||
             this_rdc_y.dist <= (skip_dist_thresh >> 1))))) {
        best_early_term = true;
      }
      if (x->force_zeromv_skip_for_sb == 1 && is_skippable && !is_comp &&
          rf0 == x->vbp_ref_frame && mbmi->mv[0].as_int == 0 &&
          this_rdc_y.dist <= skip_dist_thresh) {
        best_early_term = true;
        break;
      }
    }
  }

  pd->dst.buf = orig_dst_buf;
  pd->dst.stride = orig_dst_stride;

  // If the winning inter mode skipped the whole block while color_sensitivity
  // left one or both chroma planes unchecked, re-check chroma once so non-zero
  // chroma residuals are never falsely whole-block skipped. When
  // use_color_sensitivity >= 2 on camera content, also populate best_rdc with
  // real UV cost on non-skipped winning inter modes so intra-in-inter modes
  // are compared on equal Y+UV footing.
  const bool check_nonskip_uv =
      !is_screen && sf->rt_sf.use_color_sensitivity >= 2;
  if (best_mbmi.mode != MODE_INVALID && is_inter_mode(best_mbmi.mode) &&
      (best_skip == 1 || check_nonskip_uv) && best_unchecked_uv && !lossless) {
    const MV_REFERENCE_FRAME win_rf0 = best_mbmi.ref_frame[0];
    const MV_REFERENCE_FRAME win_rf1 = best_mbmi.ref_frame[1];
    const bool win_is_comp = (win_rf1 >= 0 && win_rf1 < INTER_REFS_PER_FRAME);
    *mbmi = best_mbmi;
    set_ref_ptrs(cm, xd, win_rf0, win_rf1);
    for (int i = 0; i < num_planes; ++i) {
      xd->plane[i].pre[0] = yv12_mb[win_rf0][i];
      if (win_is_comp) {
        xd->plane[i].pre[1] = yv12_mb[win_rf1][i];
      }
    }
    RD_STATS recheck_uv;
    int recheck_skip_uv = 1;
    block_uvrd_nonrd(cpi, x, ctx, &recheck_uv, &recheck_skip_uv, bsize,
                     /*use_color_sens=*/false);
    if (best_skip == 0) {
      best_rdc.rate = best_rdc.rate - best_uv_rate + recheck_uv.rate;
      best_rdc.dist = best_y_dist + recheck_uv.dist;
      best_rdc.sse = best_y_sse + recheck_uv.sse;
      best_rdc.rdcost = RDCOST(x->rdmult, best_rdc.rate, best_rdc.dist);
    } else if (!recheck_skip_uv) {
      best_skip = 0;
      best_mbmi.skip_txfm[0] = 0;
      best_mbmi.skip_txfm[1] = 0;
      for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
        memset(ctx->blk_skip[plane], 0,
               sizeof(uint8_t) * ctx->num_4x4_blk_chroma);
      }
      int tx_part_rate = 0;
      if (signal_tx_partition) {
        tx_part_rate = count_64x64_tx_units_nonrd(xd, bsize) *
                       inter_tx_partition_cost(x, TX_PARTITION_NONE, bsize,
                                               max_txsize_rect_lookup[bsize]);
      }
      best_rdc.rate = best_rate_mode +
                      mode_costs->skip_txfm_cost[best_skip_ctx][0] +
                      recheck_uv.rate + tx_part_rate;
      best_rdc.dist = best_y_dist + recheck_uv.dist;
      best_rdc.sse = best_y_sse + recheck_uv.sse;
      best_rdc.skip_txfm = 0;
      best_rdc.rdcost = RDCOST(x->rdmult, best_rdc.rate, best_rdc.dist);
    } else {
      best_rdc.dist = best_y_dist + recheck_uv.dist;
      best_rdc.sse = best_y_sse + recheck_uv.sse;
      best_rdc.rdcost = RDCOST(x->rdmult, best_rdc.rate, best_rdc.dist);
    }
  }

  const unsigned int intra_ref_frame_cost = ref_costs_single[INTRA_FRAME_INDEX];
  int is_intra_mode_allowed = 1;
  if (mbmi->tree_type == SHARED_PART &&
      mbmi->region_type == MIXED_INTER_INTRA_REGION &&
      mbmi->chroma_ref_info.offset_started) {
    is_intra_mode_allowed = 0;
  }

  // Evaluate intra modes in inter frame (including intra FSC when enabled).
  if (is_intra_mode_allowed) {
    estimate_intra_mode_nonrd(cpi, x, bsize, intra_ref_frame_cost,
                              no_skip_mode_cost, best_early_term, &best_rdc,
                              &best_mbmi, &best_skip, best_skip_y, ctx);
  }

  // Evaluate luma Palette mode when enabled (pruned to <= 16x16 high-variance
  // blocks or intra-winning blocks).
  if (is_intra_mode_allowed && cpi->oxcf.tool_cfg.enable_palette &&
      av2_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        bsize) &&
      !best_early_term && best_mbmi.mode != MODE_INVALID) {
    const bool try_palette_nonrd =
        !is_inter_mode(best_mbmi.mode) ||
        (is_screen && block_size_wide[bsize] <= 16 &&
         block_size_high[bsize] <= 16 && !best_skip_y &&
         x->source_sad_level > kZeroSad && x->source_variance > 200 &&
         best_rdc.sse > (num_pels * 256));
    if (try_palette_nonrd) {
      const int palette_cost_header =
          (int)intra_ref_frame_cost + no_skip_mode_cost;
      (void)search_palette_luma_nonrd(cpi, x, bsize, palette_cost_header, ctx,
                                      &best_rdc, &best_mbmi, &best_skip);
    }
  }

  // Evaluate local SB buffer IntraBC in inter frames when enable_intrabc and
  // enable_intrabc_ext are active.
  if (is_intra_mode_allowed && !best_early_term && !best_skip &&
      (!is_inter_mode(best_mbmi.mode) || x->source_sad_level > kZeroSad) &&
      cpi->oxcf.kf_cfg.enable_intrabc && cpi->oxcf.kf_cfg.enable_intrabc_ext &&
      av2_allow_intrabc(cm, xd, bsize) && xd->tree_type != CHROMA_PART) {
    const unsigned int var_thresh = 50;
    const bool skip_intrabc =
        (sf->inter_sf.skip_eval_intrabc_in_inter_frame == 1) ||
        (sf->inter_sf.skip_eval_intrabc_in_inter_frame == 2 &&
         !(block_size_wide[bsize] <= 16 && block_size_high[bsize] <= 16 &&
           x->source_variance > var_thresh));
    if (!skip_intrabc) {
      const int skip_interintra_cost =
          (int)intra_ref_frame_cost + no_skip_mode_cost;
      (void)search_intrabc_nonrd(cpi, x, bsize, skip_interintra_cost, ctx,
                                 &best_rdc, &best_mbmi, &best_skip);
    }
  }

  if (best_mbmi.mode == MODE_INVALID || best_rdc.rdcost >= INT64_MAX) {
    av2_invalid_rd_stats(rd_cost);
    return;
  }

  restore_winning_mode_context(cm, x, bsize, ctx, rd_cost, &best_rdc,
                               &best_mbmi, best_skip);
}
