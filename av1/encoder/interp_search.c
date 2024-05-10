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

#include "av1/common/pred_common.h"
#include "av1/encoder/interp_search.h"
#include "av1/encoder/model_rd.h"
#include "av1/encoder/rdopt_utils.h"
#include "av1/encoder/reconinter_enc.h"

// return mv_diff
static INLINE int is_interp_filter_good_match(
    const INTERPOLATION_FILTER_STATS *st, MB_MODE_INFO *const mi,
    int skip_level) {
  const int is_comp = has_second_ref(mi);
  int i;

  for (i = 0; i < 1 + is_comp; ++i) {
    if (st->ref_frames[i] != mi->ref_frame[i]) return INT_MAX;
  }

  if (skip_level == 1 && is_comp) {
    if (st->comp_type != mi->interinter_comp.type) return INT_MAX;
  }

  int mv_diff = 0;
  for (i = 0; i < 1 + is_comp; ++i) {
    mv_diff += abs(st->mv[i].as_mv.row - mi->mv[i].as_mv.row) +
               abs(st->mv[i].as_mv.col - mi->mv[i].as_mv.col);
  }
  return mv_diff;
}

static INLINE int save_interp_filter_search_stat(
    MB_MODE_INFO *const mbmi, int64_t rd, unsigned int pred_sse,
    INTERPOLATION_FILTER_STATS *interp_filter_stats,
    int interp_filter_stats_idx) {
  if (interp_filter_stats_idx < MAX_INTERP_FILTER_STATS) {
    INTERPOLATION_FILTER_STATS stat = { mbmi->interp_fltr,
                                        { mbmi->mv[0], mbmi->mv[1] },
                                        { mbmi->ref_frame[0],
                                          mbmi->ref_frame[1] },
                                        mbmi->interinter_comp.type,
                                        rd,
                                        pred_sse };
    interp_filter_stats[interp_filter_stats_idx] = stat;
    interp_filter_stats_idx++;
  }
  return interp_filter_stats_idx;
}

static INLINE int find_interp_filter_in_stats(
    MB_MODE_INFO *const mbmi, INTERPOLATION_FILTER_STATS *interp_filter_stats,
    int interp_filter_stats_idx, int skip_level) {
  // [skip_levels][single or comp]
  const int thr[2][2] = { { 0, 0 }, { 3, 7 } };
  const int is_comp = has_second_ref(mbmi);

  // Find good enough match.
  // TODO(yunqing): Separate single-ref mode and comp mode stats for fast
  // search.
  int best = INT_MAX;
  int match = -1;
  for (int j = 0; j < interp_filter_stats_idx; ++j) {
    const INTERPOLATION_FILTER_STATS *st = &interp_filter_stats[j];
    const int mv_diff = is_interp_filter_good_match(st, mbmi, skip_level);
    // Exact match is found.
    if (mv_diff == 0) {
      match = j;
      break;
    } else if (mv_diff < best && mv_diff <= thr[skip_level - 1][is_comp]) {
      best = mv_diff;
      match = j;
    }
  }

  if (match != -1) {
    mbmi->interp_fltr = interp_filter_stats[match].interp_fltr;
    return match;
  }
  return -1;  // no match result found
}

int av1_find_interp_filter_match(
    MB_MODE_INFO *const mbmi, const AV1_COMP *const cpi,
    const InterpFilter assign_filter, const int need_search,
    INTERPOLATION_FILTER_STATS *interp_filter_stats,
#if CONFIG_COMPOUND_4XN
    MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
    int interp_filter_stats_idx) {
  int match_found_idx = -1;
  if (cpi->sf.interp_sf.use_interp_filter && need_search)
    match_found_idx = find_interp_filter_in_stats(
        mbmi, interp_filter_stats, interp_filter_stats_idx,
        cpi->sf.interp_sf.use_interp_filter);

  if (!need_search || match_found_idx == -1)
    set_default_interp_filters(mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
                               &cpi->common,
#if CONFIG_COMPOUND_4XN
                               xd,
#endif  // CONFIG_COMPOUND_4XN
#endif  // CONFIG_OPTFLOW_REFINEMENT
                               assign_filter);
  return match_found_idx;
}

static INLINE void swap_dst_buf(MACROBLOCKD *xd, const BUFFER_SET *dst_bufs[2],
                                int num_planes) {
  const BUFFER_SET *buf0 = dst_bufs[0];
  dst_bufs[0] = dst_bufs[1];
  dst_bufs[1] = buf0;
  restore_dst_buf(xd, *dst_bufs[0], num_planes);
}

static INLINE int get_switchable_rate(MACROBLOCK *const x,
                                      const InterpFilter interp_fltr,
                                      const int ctx[2]) {
#if CONFIG_OPTFLOW_REFINEMENT
  // When optical flow refinement is used, interp filter type is always set
  // to MULTITAP_SHARP, and thus is not switchable.
  assert(x->e_mbd.mi[0]->mode < NEAR_NEARMV_OPTFLOW);
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
  assert(!x->e_mbd.mi[0]->refinemv_flag);
#endif  // CONFIG_REFINEMV
  const int inter_filter_cost =
      x->mode_costs.switchable_interp_costs[ctx[0]][interp_fltr];
  return SWITCHABLE_INTERP_RATE_FACTOR * inter_filter_cost;
}

// Build inter predictor and calculate model rd
// for a given plane.
static INLINE void interp_model_rd_eval(
    MACROBLOCK *const x, const AV1_COMP *const cpi, BLOCK_SIZE bsize,
    const BUFFER_SET *const orig_dst, int plane_from, int plane_to,
    RD_STATS *rd_stats, int is_skip_build_pred) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_STATS tmp_rd_stats;
  av1_init_rd_stats(&tmp_rd_stats);

  // Skip inter predictor if the predictor is already available.
  if (!is_skip_build_pred) {
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                  plane_from, plane_to);
  }

  // The chroma can have different bsize than luma, so they need to taken care
  // of separately.
  assert(IMPLIES(plane_from == AOM_PLANE_Y, plane_to == AOM_PLANE_Y));
#if CONFIG_EXT_RECUR_PARTITIONS
  if (plane_from > AOM_PLANE_Y) {
    bsize = get_bsize_base(xd, xd->mi[0], AOM_PLANE_U);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  model_rd_sb_fn[MODELRD_TYPE_INTERP_FILTER](
      cpi, bsize, x, xd, plane_from, plane_to, &tmp_rd_stats.rate,
      &tmp_rd_stats.dist, &tmp_rd_stats.skip_txfm, &tmp_rd_stats.sse, NULL,
      NULL, NULL
#if CONFIG_MRSSE
      ,
      SSE_TYPE_INTERP_FILTER
#endif  // CONFIG_MRSSE
  );

  av1_merge_rd_stats(rd_stats, &tmp_rd_stats);
}

// calculate the rdcost of given interpolation_filter
static INLINE int64_t interpolation_filter_rd(
    MACROBLOCK *const x, const AV1_COMP *const cpi,
    const TileDataEnc *tile_data, BLOCK_SIZE bsize,
    const BUFFER_SET *const orig_dst, int64_t *const rd,
    RD_STATS *rd_stats_luma, RD_STATS *rd_stats, int *const switchable_rate,
    const BUFFER_SET *dst_bufs[2], int filter_idx, const int switchable_ctx[2],
    const int skip_pred) {
  const AV1_COMMON *cm = &cpi->common;
  const InterpSearchFlags *interp_search_flags = &cpi->interp_search_flags;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  RD_STATS this_rd_stats_luma, this_rd_stats;

  // Initialize rd_stats structures to default values.
  av1_init_rd_stats(&this_rd_stats_luma);
  this_rd_stats = *rd_stats_luma;
  const InterpFilter last_best = mbmi->interp_fltr;
  mbmi->interp_fltr = filter_idx;
#if CONFIG_OPTFLOW_REFINEMENT
  const int tmp_rs =
      (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                  xd,
#endif  // CONFIG_COMPOUND_4XN
                                  mbmi)
#if CONFIG_REFINEMV
       || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
#if CONFIG_TIP_REF_PRED_MERGING
       || is_tip_ref_frame(mbmi->ref_frame[0])
#endif
           )
          ? 0
          : get_switchable_rate(x, mbmi->interp_fltr, switchable_ctx);
#else
  const int tmp_rs = get_switchable_rate(x, mbmi->interp_fltr, switchable_ctx);
#endif  // CONFIG_OPTFLOW_REFINEMENT

  int64_t min_rd = RDCOST(x->rdmult, tmp_rs, 0);
  if (min_rd > *rd) {
    mbmi->interp_fltr = last_best;
    return 0;
  }

  (void)tile_data;

  assert(skip_pred != 2);
  assert((rd_stats_luma->rate >= 0) && (rd_stats->rate >= 0));
  assert((rd_stats_luma->dist >= 0) && (rd_stats->dist >= 0));
  assert((rd_stats_luma->sse >= 0) && (rd_stats->sse >= 0));
  assert((rd_stats_luma->skip_txfm == 0) || (rd_stats_luma->skip_txfm == 1));
  assert((rd_stats->skip_txfm == 0) || (rd_stats->skip_txfm == 1));
  assert((skip_pred >= 0) &&
         (skip_pred <= interp_search_flags->default_interp_skip_flags));

  // When skip_txfm pred is equal to default_interp_skip_flags,
  // skip both luma and chroma MC.
  // For mono-chrome images:
  // num_planes = 1 and cpi->default_interp_skip_flags = 1,
  // skip_pred = 1: skip both luma and chroma
  // skip_pred = 0: Evaluate luma and as num_planes=1,
  // skip chroma evaluation
  int tmp_skip_pred =
      (skip_pred == interp_search_flags->default_interp_skip_flags)
          ? INTERP_SKIP_LUMA_SKIP_CHROMA
          : skip_pred;

  switch (tmp_skip_pred) {
    case INTERP_EVAL_LUMA_EVAL_CHROMA:
      // skip_pred = 0: Evaluate both luma and chroma.
      // Luma MC
      interp_model_rd_eval(x, cpi, bsize, orig_dst, AOM_PLANE_Y, AOM_PLANE_Y,
                           &this_rd_stats_luma, 0);
      this_rd_stats = this_rd_stats_luma;
#if CONFIG_COLLECT_RD_STATS == 3
      RD_STATS rd_stats_y;
      av1_pick_recursive_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                          INT64_MAX);
      PrintPredictionUnitStats(cpi, tile_data, x, &rd_stats_y, bsize);
#endif  // CONFIG_COLLECT_RD_STATS == 3
      AOM_FALLTHROUGH_INTENDED;
    case INTERP_SKIP_LUMA_EVAL_CHROMA:
      // skip_pred = 1: skip luma evaluation (retain previous best luma stats)
      // and do chroma evaluation.
      for (int plane = 1; plane < num_planes; ++plane) {
        int64_t tmp_rd =
            RDCOST(x->rdmult, tmp_rs + this_rd_stats.rate, this_rd_stats.dist);
        if (tmp_rd >= *rd) {
          mbmi->interp_fltr = last_best;
          return 0;
        }
        interp_model_rd_eval(x, cpi, bsize, orig_dst, plane, plane,
                             &this_rd_stats, 0);
      }
      break;
    case INTERP_SKIP_LUMA_SKIP_CHROMA:
      // both luma and chroma evaluation is skipped
      this_rd_stats = *rd_stats;
      break;
    case INTERP_EVAL_INVALID:
    default: assert(0); return 0;
  }
  int64_t tmp_rd =
      RDCOST(x->rdmult, tmp_rs + this_rd_stats.rate, this_rd_stats.dist);

  if (tmp_rd < *rd) {
    *rd = tmp_rd;
    *switchable_rate = tmp_rs;
    if (skip_pred != interp_search_flags->default_interp_skip_flags) {
      if (skip_pred == INTERP_EVAL_LUMA_EVAL_CHROMA) {
        // Overwrite the data as current filter is the best one
        *rd_stats_luma = this_rd_stats_luma;
        *rd_stats = this_rd_stats;
        // As luma MC data is computed, no need to recompute after the search
        x->recalc_luma_mc_data = 0;
      } else if (skip_pred == INTERP_SKIP_LUMA_EVAL_CHROMA) {
        // As luma MC data is not computed, update of luma data can be skipped
        *rd_stats = this_rd_stats;
        // As luma MC data is not recomputed and current filter is the best,
        // indicate the possibility of recomputing MC data
        // If current buffer contains valid MC data, toggle to indicate that
        // luma MC data needs to be recomputed
        x->recalc_luma_mc_data ^= 1;
      }
      swap_dst_buf(xd, dst_bufs, num_planes);
    }
    return 1;
  }
  mbmi->interp_fltr = last_best;
  return 0;
}

// Find the best interp filter if dual_interp_filter = 0
static INLINE void find_best_non_dual_interp_filter(
    MACROBLOCK *const x, const AV1_COMP *const cpi,
    const TileDataEnc *tile_data, BLOCK_SIZE bsize,
    const BUFFER_SET *const orig_dst, int64_t *const rd, RD_STATS *rd_stats_y,
    RD_STATS *rd_stats, int *const switchable_rate,
    const BUFFER_SET *dst_bufs[2], const int switchable_ctx[2],
    const int skip_ver, const int skip_hor) {
  const int skip_pred = (skip_hor & skip_ver);
  for (int i = EIGHTTAP_REGULAR + 1; i < SWITCHABLE_FILTERS; ++i) {
    interpolation_filter_rd(x, cpi, tile_data, bsize, orig_dst, rd, rd_stats_y,
                            rd_stats, switchable_rate, dst_bufs, i,
                            switchable_ctx, skip_pred);
  }
}

static INLINE void calc_interp_skip_pred_flag(MACROBLOCK *const x,
                                              const AV1_COMP *const cpi,
                                              int *skip_hor, int *skip_ver) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int num_planes = av1_num_planes(cm);
  const int is_compound = has_second_ref(mbmi);
  assert(is_intrabc_block(mbmi, xd->tree_type) == 0);
  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf =
        get_ref_scale_factors_const(cm, mbmi->ref_frame[ref]);
    // TODO(any): Refine skip flag calculation considering scaling
    if (av1_is_scaled(sf)) {
      *skip_hor = 0;
      *skip_ver = 0;
      break;
    }
    const MV mv = mbmi->mv[ref].as_mv;
    int skip_hor_plane = 0;
    int skip_ver_plane = 0;
    for (int plane_idx = 0; plane_idx < AOMMAX(1, (num_planes - 1));
         ++plane_idx) {
      struct macroblockd_plane *const pd = &xd->plane[plane_idx];
      const int bw = pd->width;
      const int bh = pd->height;
      const MV mv_q4 =
          clamp_mv_to_umv_border_sb(xd, &mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                    0,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                    pd->subsampling_x, pd->subsampling_y);
      const int sub_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
      const int sub_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;
      skip_hor_plane |= ((sub_x == 0) << plane_idx);
      skip_ver_plane |= ((sub_y == 0) << plane_idx);
    }
    *skip_hor &= skip_hor_plane;
    *skip_ver &= skip_ver_plane;
    // It is not valid that "luma MV is sub-pel, whereas chroma MV is not"
    assert(*skip_hor != 2);
    assert(*skip_ver != 2);
  }
  // When compond prediction type is compound segment wedge, luma MC and chroma
  // MC need to go hand in hand as mask generated during luma MC is reuired for
  // chroma MC. If skip_hor = 0 and skip_ver = 1, mask used for chroma MC during
  // vertical filter decision may be incorrect as temporary MC evaluation
  // overwrites the mask. Make skip_ver as 0 for this case so that mask is
  // populated during luma MC
  if (is_compound && mbmi->interinter_comp.type == COMPOUND_DIFFWTD) {
    assert(mbmi->comp_group_idx == 1);
    if (*skip_hor == 0 && *skip_ver == 1) *skip_ver = 0;
  }
}

/*!\brief AV1 interpolation filter search
 *
 * \ingroup inter_mode_search
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     tile_data         Pointer to struct holding adaptive
 *                                  data/contexts/models for the tile during
 *                                  encoding.
 * \param[in]     x                 Pointer to struc holding all the data for
 *                                  the current macroblock.
 * \param[in]     bsize             Current block size.
 * \param[in]     tmp_dst           A temporary prediction buffer to hold a
 *                                  computed prediction.
 * \param[in,out] orig_dst          A prediction buffer to hold a computed
 *                                  prediction. This will eventually hold the
 *                                  final prediction, and the tmp_dst info will
 *                                  be copied here.
 * \param[in,out] rd                The RD cost associated with the selected
 *                                  interpolation filter parameters.
 * \param[in,out] switchable_rate   The rate associated with using a SWITCHABLE
 *                                  filter mode.
 * \param[in,out] skip_build_pred   Indicates whether or not to build the inter
 *                                  predictor. If this is 0, the inter predictor
 *                                  has already been built and thus we can avoid
 *                                  repeating computation.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 *
 * \return Returns INT64_MAX if the filter parameters are invalid and the
 * current motion mode being tested should be skipped. It returns 0 if the
 * parameter search is a success.
 */
int64_t av1_interpolation_filter_search(
    MACROBLOCK *const x, const AV1_COMP *const cpi,
    const TileDataEnc *tile_data, BLOCK_SIZE bsize,
    const BUFFER_SET *const tmp_dst, const BUFFER_SET *const orig_dst,
    int64_t *const rd, int *const switchable_rate, int *skip_build_pred,
    HandleInterModeArgs *args, int64_t ref_best_rd) {
  const AV1_COMMON *cm = &cpi->common;
  const InterpSearchFlags *interp_search_flags = &cpi->interp_search_flags;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int need_search = av1_is_interp_needed(cm, xd);
  const int ref_frame = COMPACT_INDEX0_NRS(xd->mi[0]->ref_frame[0]);
  RD_STATS rd_stats_luma, rd_stats;

#if CONFIG_EXTENDED_WARP_PREDICTION
  if (mbmi->mode == WARPMV) return 0;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

  // Initialization of rd_stats structures with default values
  av1_init_rd_stats(&rd_stats_luma);
  av1_init_rd_stats(&rd_stats);

  int match_found_idx = -1;
  const InterpFilter assign_filter = cm->features.interp_filter;

  match_found_idx = av1_find_interp_filter_match(
      mbmi, cpi, assign_filter, need_search, args->interp_filter_stats,
#if CONFIG_COMPOUND_4XN
      xd,
#endif  // CONFIG_COMPOUND_4XN
      args->interp_filter_stats_idx);

  if (match_found_idx != -1) {
    *rd = args->interp_filter_stats[match_found_idx].rd;
    x->pred_sse[ref_frame] =
        args->interp_filter_stats[match_found_idx].pred_sse;
    return 0;
  }

  int switchable_ctx[2];
  switchable_ctx[0] = av1_get_pred_context_switchable_interp(xd, 0);
  switchable_ctx[1] = av1_get_pred_context_switchable_interp(xd, 1);
#if CONFIG_OPTFLOW_REFINEMENT
  *switchable_rate =
      (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                  xd,
#endif  // CONFIG_COMPOUND_4XN
                                  mbmi)
#if CONFIG_REFINEMV
       || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
#if CONFIG_TIP_REF_PRED_MERGING
       || is_tip_ref_frame(mbmi->ref_frame[0])
#endif
           )
          ? 0
          : get_switchable_rate(x, mbmi->interp_fltr, switchable_ctx);
#else
  *switchable_rate = get_switchable_rate(x, mbmi->interp_fltr, switchable_ctx);
#endif  // CONFIG_OPTFLOW_REFINEMENT

  // Do MC evaluation for default filter_type.
  // Luma MC
  interp_model_rd_eval(x, cpi, bsize, orig_dst, AOM_PLANE_Y, AOM_PLANE_Y,
                       &rd_stats_luma, *skip_build_pred);

#if CONFIG_COLLECT_RD_STATS == 3
  RD_STATS rd_stats_y;
  av1_pick_recursive_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize, INT64_MAX);
  PrintPredictionUnitStats(cpi, tile_data, x, &rd_stats_y, bsize);
#endif  // CONFIG_COLLECT_RD_STATS == 3
  // Chroma MC
  if (num_planes > 1) {
    interp_model_rd_eval(x, cpi, bsize, orig_dst, AOM_PLANE_U, AOM_PLANE_V,
                         &rd_stats, *skip_build_pred);
  }
  *skip_build_pred = 1;

  av1_merge_rd_stats(&rd_stats, &rd_stats_luma);

  assert(rd_stats.rate >= 0);

  *rd = RDCOST(x->rdmult, *switchable_rate + rd_stats.rate, rd_stats.dist);
  x->pred_sse[ref_frame] = (unsigned int)(rd_stats_luma.sse >> 4);

  if (assign_filter != SWITCHABLE || match_found_idx != -1) {
    return 0;
  }
  if (!need_search) {
#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
#if CONFIG_TIP_REF_PRED_MERGING
    assert(mbmi->interp_fltr ==
           ((opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                        xd,
#endif  // CONFIG_COMPOUND_4XN
                                        mbmi) ||
             mbmi->refinemv_flag || is_tip_ref_frame(mbmi->ref_frame[0]))
                ? MULTITAP_SHARP
                : EIGHTTAP_REGULAR));
#else
    assert(mbmi->interp_fltr == ((opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                                             xd,
#endif  // CONFIG_COMPOUND_4XN
                                                             mbmi) ||
                                  mbmi->refinemv_flag)
                                     ? MULTITAP_SHARP
                                     : EIGHTTAP_REGULAR));
#endif
#else
    assert(mbmi->interp_fltr == (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                                            xd,
#endif  // CONFIG_COMPOUND_4XN
                                                            mbmi)
#if CONFIG_TIP_REF_PRED_MERGING
                                         || is_tip_ref_frame(mbmi->ref_frame[0])
#endif
                                     ? MULTITAP_SHARP
                                     : EIGHTTAP_REGULAR));
#endif  // CONFIG_REFINEMV
#else
    assert(mbmi->interp_fltr == EIGHTTAP_REGULAR);
#endif  // CONFIG_OPTFLOW_REFINEMENT
    return 0;
  }
  if (args->modelled_rd != NULL) {
#if CONFIG_REFINEMV
    int use_default_filter = mbmi->refinemv_flag
#if CONFIG_OPTFLOW_REFINEMENT
                             || opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                                           xd,
#endif  // CONFIG_COMPOUND_4XN
                                                           mbmi)
#endif
#if CONFIG_TIP_REF_PRED_MERGING
                             || is_tip_ref_frame(mbmi->ref_frame[0])
#endif
        ;
    if (has_second_ref(mbmi) && !use_default_filter) {
#else
#if CONFIG_OPTFLOW_REFINEMENT
    if (has_second_ref(mbmi) && !opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                                            xd,
#endif  // CONFIG_COMPOUND_4XN
                                                            mbmi)) {
#else
    if (has_second_ref(mbmi)) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
#endif  // CONFIG_REFINEMV
#if !CONFIG_SEP_COMP_DRL
      const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // !CONFIG_SEP_COMP_DRL
      MV_REFERENCE_FRAME *refs = mbmi->ref_frame;
      const int mode0 = compound_ref0_mode(mbmi->mode);
      const int mode1 = compound_ref1_mode(mbmi->mode);
#if CONFIG_SEP_COMP_DRL
      const int64_t mrd =
          AOMMIN(args->modelled_rd[mode0][get_ref_mv_idx(mbmi, 0)][refs[0]],
                 args->modelled_rd[mode1][get_ref_mv_idx(mbmi, 1)][refs[1]]);
#else
      const int64_t mrd = AOMMIN(args->modelled_rd[mode0][ref_mv_idx][refs[0]],
                                 args->modelled_rd[mode1][ref_mv_idx][refs[1]]);
#endif  // CONFIG_SEP_COMP_DRL

      if ((*rd >> 1) > mrd && ref_best_rd < INT64_MAX) {
        return INT64_MAX;
      }
    }
  }

  x->recalc_luma_mc_data = 0;
  // skip_flag=xx (in binary form)
  // Setting 0th flag corresonds to skipping luma MC and setting 1st bt
  // corresponds to skipping chroma MC  skip_flag=0 corresponds to "Don't skip
  // luma and chroma MC"  Skip flag=1 corresponds to "Skip Luma MC only"
  // Skip_flag=2 is not a valid case
  // skip_flag=3 corresponds to "Skip both luma and chroma MC"
  int skip_hor = interp_search_flags->default_interp_skip_flags;
  int skip_ver = interp_search_flags->default_interp_skip_flags;
  calc_interp_skip_pred_flag(x, cpi, &skip_hor, &skip_ver);

  // do interp_filter search
  restore_dst_buf(xd, *tmp_dst, num_planes);
  const BUFFER_SET *dst_bufs[2] = { tmp_dst, orig_dst };
  find_best_non_dual_interp_filter(
      x, cpi, tile_data, bsize, orig_dst, rd, &rd_stats_luma, &rd_stats,
      switchable_rate, dst_bufs, switchable_ctx, skip_ver, skip_hor);
  swap_dst_buf(xd, dst_bufs, num_planes);
  // Recompute final MC data if required
  if (x->recalc_luma_mc_data == 1) {
    // Recomputing final luma MC data is required only if the same was skipped
    // in either of the directions  Condition below is necessary, but not
    // sufficient
    assert((skip_hor == 1) || (skip_ver == 1));
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                  AOM_PLANE_Y, AOM_PLANE_Y);
  }
  x->pred_sse[ref_frame] = (unsigned int)(rd_stats_luma.sse >> 4);

  // save search results
  if (cpi->sf.interp_sf.use_interp_filter) {
    assert(match_found_idx == -1);
    args->interp_filter_stats_idx = save_interp_filter_search_stat(
        mbmi, *rd, x->pred_sse[ref_frame], args->interp_filter_stats,
        args->interp_filter_stats_idx);
  }
  return 0;
}
