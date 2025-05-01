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

#include "config/aom_dsp_rtcd.h"
#include "aom_ports/system_state.h"

#include "av1/common/reconinter.h"

#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/partition_search.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tpl_model.h"

#define RIGHT_SHIFT_MV(x) (((x) + 3 + ((x) >= 0)) >> 3)

typedef struct {
  FULLPEL_MV fmv;
  int weight;
} cand_mv_t;

static int compare_weight(const void *a, const void *b) {
  const int diff = ((cand_mv_t *)a)->weight - ((cand_mv_t *)b)->weight;
  if (diff < 0)
    return 1;
  else if (diff > 0)
    return -1;
  return 0;
}

// Allow more mesh searches for screen content type on the ARF.
static int use_fine_search_interval(const AV1_COMP *const cpi) {
  return cpi->is_screen_content_type &&
         (cpi->gf_group.update_type[cpi->gf_group.index] == ARF_UPDATE ||
          cpi->gf_group.update_type[cpi->gf_group.index] == KFFLT_UPDATE) &&
         cpi->oxcf.speed <= 3;
}

void av1_single_motion_search(const AV1_COMP *const cpi, MACROBLOCK *x,
                              BLOCK_SIZE bsize, int ref_idx, int *rate_mv,
                              int search_range, inter_mode_info *mode_info,
                              int_mv *best_mv, const int_mv *warp_ref_mv) {
  MACROBLOCKD *xd = &x->e_mbd;
  const AV1_COMMON *cm = &cpi->common;
  const MotionVectorSearchParams *mv_search_params = &cpi->mv_search_params;
  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_OPFL_MV_SEARCH
  int opfl_its = get_opfl_mv_iterations(cpi, mbmi);
  int_mv cur_mv;
  FULLPEL_MV this_best_opfl_mv, this_second_best_opfl_mv;
  int best_opfl_sme = INT_MAX;
  const int radix =
      1 << AOMMAX(0, MV_PRECISION_ONE_PEL - mbmi->pb_mv_precision);
  const int range_thr = OMVS_RANGE_THR * radix;
  const int step = OMVS_BIG_STEP * radix;
#endif  // CONFIG_OPFL_MV_SEARCH
  MOTION_MODE backup_motion_mode = mbmi->motion_mode;
  // Make the motion mode transalational, so that transalation MS can be used.
  if (mbmi->mode == WARPMV) mbmi->motion_mode = SIMPLE_TRANSLATION;

  struct buf_2d backup_yv12[MAX_MB_PLANE] = { { 0, 0, 0, 0, 0 } };
  int bestsme = INT_MAX;
  const int ref = mbmi->ref_frame[ref_idx];
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const MvCosts *mv_costs = &x->mv_costs;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);

  if (scaled_ref_frame) {
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // full-pixel motion search code to be used without additional
    // modifications.
    for (int i = 0; i < num_planes; i++) {
      backup_yv12[i] = xd->plane[i].pre[ref_idx];
    }
    av1_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL,
                         num_planes, &mbmi->chroma_ref_info);
  }

  // Work out the size of the first step in the mv step search.
  // 0 here is maximum length first step. 1 is AOMMAX >> 1 etc.
  int step_param;
  if (cpi->sf.mv_sf.auto_mv_step_size && cm->show_frame) {
    // Take the weighted average of the step_params based on the last frame's
    // max mv magnitude and that based on the best ref mvs of the current
    // block for the given reference.
    const int ref_frame_idx = COMPACT_INDEX0_NRS(ref);
    step_param = (av1_init_search_range(x->max_mv_context[ref_frame_idx]) +
                  mv_search_params->mv_step_param) /
                 2;
  } else {
    step_param = mv_search_params->mv_step_param;
  }

  MV ref_mv_low_prec = (mbmi->mode == WARPMV)
                           ? warp_ref_mv->as_mv
                           : av1_get_ref_mv(x, ref_idx).as_mv;
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(ref_mv_low_prec, &sub_mv_offset, mbmi->pb_mv_precision);
  if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
    lower_mv_precision(&ref_mv_low_prec, mbmi->pb_mv_precision);
  const MV ref_mv = ref_mv_low_prec;

  FULLPEL_MV start_mv;
  if (mbmi->motion_mode != SIMPLE_TRANSLATION) {
    start_mv = get_fullmv_from_mv(&mbmi->mv[0].as_mv);
  } else {
    start_mv = get_fullmv_from_mv(
        &ref_mv);  // ref_mv is already converted to low precision
  }

  full_pel_lower_mv_precision(&start_mv, mbmi->pb_mv_precision);

  // cand stores start_mv and all possible MVs in a SB.
  cand_mv_t cand[MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB + 1] = { { { 0, 0 },
                                                                  0 } };
  cand[0].fmv = start_mv;
  int cnt = 1;
  int total_weight = 0;

  if (!cpi->sf.mv_sf.full_pixel_search_level && !is_tip_ref_frame(ref) &&
      mbmi->motion_mode == SIMPLE_TRANSLATION) {
    SuperBlockEnc *sb_enc = &x->sb_enc;
    if (sb_enc->tpl_data_count) {
      const BLOCK_SIZE tpl_bsize =
          convert_length_to_bsize(cpi->tpl_data.tpl_bsize_1d);
      const int tplw = mi_size_wide[tpl_bsize];
      const int tplh = mi_size_high[tpl_bsize];
      const int nw = mi_size_wide[bsize] / tplw;
      const int nh = mi_size_high[bsize] / tplh;

      if (nw >= 1 && nh >= 1) {
        const int of_h = mi_row % mi_size_high[cm->sb_size];
        const int of_w = mi_col % mi_size_wide[cm->sb_size];
        const int start = of_h / tplh * sb_enc->tpl_stride + of_w / tplw;
        int valid = 1;

        // Assign large weight to start_mv, so it is always tested.
        cand[0].weight = nw * nh;

        for (int k = 0; k < nh; k++) {
          for (int l = 0; l < nw; l++) {
            const int_mv mv =
                sb_enc->tpl_mv[start + k * sb_enc->tpl_stride + l][ref];
            if (mv.as_int == INVALID_MV) {
              valid = 0;
              break;
            }

            FULLPEL_MV fmv = { GET_MV_RAWPEL(mv.as_mv.row),
                               GET_MV_RAWPEL(mv.as_mv.col) };
            full_pel_lower_mv_precision(&fmv, mbmi->pb_mv_precision);

            int unique = 1;
            for (int m = 0; m < cnt; m++) {
              // TODO (Mohammed): fmv is already in full pel, do we need right
              // shift here?
              if (RIGHT_SHIFT_MV(fmv.row) == RIGHT_SHIFT_MV(cand[m].fmv.row) &&
                  RIGHT_SHIFT_MV(fmv.col) == RIGHT_SHIFT_MV(cand[m].fmv.col)) {
                unique = 0;
                cand[m].weight++;
                break;
              }
            }

            if (unique) {
              cand[cnt].fmv = fmv;
              cand[cnt].weight = 1;
              cnt++;
            }
          }
          if (!valid) break;
        }

        if (valid) {
          total_weight = 2 * nh * nw;
          if (cnt > 2) qsort(cand, cnt, sizeof(cand[0]), &compare_weight);
        }
      }
    }
  }

  // Further reduce the search range.
  if (search_range < INT_MAX) {
    const search_site_config *search_site_cfg =
        &mv_search_params
             ->search_site_cfg[SS_CFG_SRC][cpi->sf.mv_sf.search_method];
    // Max step_param is search_site_cfg->num_search_steps.
    if (search_range < 1) {
      step_param = search_site_cfg->num_search_steps;
    } else {
      while (search_site_cfg->radius[search_site_cfg->num_search_steps -
                                     step_param - 1] > (search_range << 1) &&
             search_site_cfg->num_search_steps - step_param - 1 > 0)
        step_param++;
    }
  }

  int cost_list[5];
  int_mv second_best_mv;
  best_mv->as_int = second_best_mv.as_int = INVALID_MV;

  // Allow more mesh searches for screen content type on the ARF.
  const int fine_search_interval = use_fine_search_interval(cpi);
  const search_site_config *src_search_sites =
      mv_search_params->search_site_cfg[SS_CFG_SRC];
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 0;
#endif

  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize, &ref_mv,
                                     pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                     is_ibc_cost,
#endif
                                     src_search_sites, fine_search_interval);

  switch (mbmi->motion_mode) {
    case SIMPLE_TRANSLATION: {
      int sum_weight = 0;

      for (int m = 0; m < cnt; m++) {
        FULLPEL_MV smv = cand[m].fmv;
        FULLPEL_MV this_best_mv, this_second_best_mv;

#if CONFIG_OPFL_MV_SEARCH
        this_best_mv = smv;
        best_opfl_sme = INT_MAX;
        int term = 0;
        for (int it = 0; it < 1 + opfl_its; it++) {
          if (it > 0) {
            smv = this_best_mv;
            FULLPEL_MV prev_smv = smv;
            MV prev_mv = get_mv_from_fullmv(&prev_smv);
            cur_mv.as_mv = prev_mv;
            term = opfl_refine_fullpel_mv_one_sided(cm, xd, &full_ms_params,
                                                    mbmi, &smv, &cur_mv, bsize);
            if (term) break;

            FULLPEL_MV cur_fullmv = get_fullmv_from_mv(&cur_mv.as_mv);
            full_pel_lower_mv_precision(&cur_fullmv, mbmi->pb_mv_precision);

            // If MV delta is so small that full pel MV local search is likely
            // to cover this delta, then up scale this delta. For delta being
            // (0,0), apply (1,-1)*big_step here.
            if (abs(cur_fullmv.row - prev_smv.row) <= range_thr &&
                abs(cur_fullmv.col - prev_smv.col) <= range_thr) {
              smv.row += cur_mv.as_mv.row >= prev_mv.row ? step : -step;
              smv.col += cur_mv.as_mv.col > prev_mv.col ? step : -step;
            } else {
              smv = cur_fullmv;
            }
          }
#endif  // CONFIG_OPFL_MV_SEARCH

          int thissme = av1_full_pixel_search(
              smv, &full_ms_params, step_param, cond_cost_list(cpi, cost_list),
              &this_best_mv, &this_second_best_mv);

          full_pel_lower_mv_precision(&this_second_best_mv,
                                      mbmi->pb_mv_precision);
          assert(is_this_mv_precision_compliant(
              get_mv_from_fullmv(&this_best_mv), pb_mv_precision));
          assert(is_this_mv_precision_compliant(
              get_mv_from_fullmv(&this_second_best_mv), pb_mv_precision));

#if CONFIG_OPFL_MV_SEARCH
          if (thissme < best_opfl_sme) {
            best_opfl_sme = thissme;
            this_best_opfl_mv = this_best_mv;
            this_second_best_opfl_mv = this_second_best_mv;
          } else {
            break;
          }
        }
        if (best_opfl_sme < bestsme) {
          bestsme = best_opfl_sme;
          best_mv->as_fullmv = this_best_opfl_mv;
          second_best_mv.as_fullmv = this_second_best_opfl_mv;
        }
#else
        if (thissme < bestsme) {
          bestsme = thissme;
          best_mv->as_fullmv = this_best_mv;
          second_best_mv.as_fullmv = this_second_best_mv;
        }
#endif  // CONFIG_OPFL_MV_SEARCH

        sum_weight += cand[m].weight;
        if (m >= 2 || 4 * sum_weight > 3 * total_weight) break;
      }
    } break;
    default: assert(0 && "Invalid motion mode!\n");
  }

  if (scaled_ref_frame) {
    // Swap back the original buffers for subpel motion search.
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[ref_idx] = backup_yv12[i];
    }
  }

  assert(is_this_mv_precision_compliant(get_mv_from_fullmv(&best_mv->as_fullmv),
                                        mbmi->pb_mv_precision));

  // Terminate search with the current ref_idx if we have already encountered
  // another ref_mv in the drl such that:
  //  1. The other drl has the same fullpel_mv during the SIMPLE_TRANSLATION
  //     search process as the current fullpel_mv.
  //  2. The rate needed to encode the current fullpel_mv is larger than that
  //     for the other ref_mv.
  if (cpi->sf.inter_sf.skip_repeated_full_newmv &&
      mbmi->motion_mode == SIMPLE_TRANSLATION && mbmi->mode != WARPMV &&
      best_mv->as_int != INVALID_MV) {
    int_mv this_mv;
    this_mv.as_mv = get_mv_from_fullmv(&best_mv->as_fullmv);
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
      this_mv.as_mv.row += sub_mv_offset.row;
      this_mv.as_mv.col += sub_mv_offset.col;
    }
#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_SEP_COMP_DRL
    const int ref_mv_idx = av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx);
#else
    const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
    const int this_mv_rate =
        av1_mv_bit_cost(&this_mv.as_mv, &ref_mv, pb_mv_precision, mv_costs,
                        MV_COST_WEIGHT, is_adaptive_mvd);
    mode_info[ref_mv_idx].full_search_mv.as_int = this_mv.as_int;
    mode_info[ref_mv_idx].full_mv_rate = this_mv_rate;

    for (int prev_ref_idx = 0; prev_ref_idx < ref_mv_idx; ++prev_ref_idx) {
      // Check if the motion search result same as previous results
      if (this_mv.as_int == mode_info[prev_ref_idx].full_search_mv.as_int) {
        // Compare the rate cost
        const int prev_rate_cost = mode_info[prev_ref_idx].full_mv_rate +
                                   mode_info[prev_ref_idx].drl_cost;
        const int this_rate_cost =
            this_mv_rate + mode_info[ref_mv_idx].drl_cost;

        if (prev_rate_cost <= this_rate_cost) {
          // If the current rate_cost is worse than the previous rate_cost, then
          // we terminate the search. Since av1_single_motion_search is only
          // called by handle_new_mv in SIMPLE_TRANSLATION mode, we set the
          // best_mv to INVALID mv to signal that we wish to terminate search
          // for the current mode.
          best_mv->as_int = INVALID_MV;
          return;
        }
      }
    }
  }

#if CONFIG_VQ_MVD_CODING
  assert(IMPLIES(!is_adaptive_mvd, is_this_mv_precision_compliant(
                                       get_mv_from_fullmv(&best_mv->as_fullmv),
                                       mbmi->pb_mv_precision)));
#else
  assert(is_this_mv_precision_compliant(get_mv_from_fullmv(&best_mv->as_fullmv),
                                        mbmi->pb_mv_precision));
#endif  // CONFIG_VQ_MVD_CODING

  if (cpi->common.features.cur_frame_force_integer_mv) {
    convert_fullmv_to_mv(best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
    if (mbmi->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
      best_mv->as_mv.col += sub_mv_offset.col;
      best_mv->as_mv.row += sub_mv_offset.row;
    }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  }

  const int use_fractional_mv =
      bestsme < INT_MAX && cpi->common.features.cur_frame_force_integer_mv == 0;
  if (use_fractional_mv) {
    int_mv fractional_ms_list[3];
    av1_set_fractional_mv(fractional_ms_list);
    int dis; /* TODO: use dis in distortion calculation later. */
    const int ref_pred = COMPACT_INDEX0_NRS(ref);

    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      cost_list);
    MV subpel_start_mv = get_mv_from_fullmv(&best_mv->as_fullmv);
    assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, subpel_start_mv));
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
      subpel_start_mv.col += sub_mv_offset.col;
      subpel_start_mv.row += sub_mv_offset.row;
    }
#endif  // CONFIG_C071_SUBBLK_WARPMV

    switch (mbmi->motion_mode) {
      case SIMPLE_TRANSLATION:
        if (cpi->sf.mv_sf.subpel_search_type) {
          const int try_second = second_best_mv.as_int != INVALID_MV &&
                                 second_best_mv.as_int != best_mv->as_int;
          const int best_mv_var = mv_search_params->find_fractional_mv_step(
              xd, cm, &ms_params, subpel_start_mv, &best_mv->as_mv, &dis,
              &x->pred_sse[ref_pred], fractional_ms_list);

          if (try_second) {
            MV this_best_mv;
            subpel_start_mv = get_mv_from_fullmv(&second_best_mv.as_fullmv);
#if CONFIG_C071_SUBBLK_WARPMV
            if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
              subpel_start_mv.col += sub_mv_offset.col;
              subpel_start_mv.row += sub_mv_offset.row;
            }
#endif  // CONFIG_C071_SUBBLK_WARPMV
            if (av1_is_subpelmv_in_range(&ms_params.mv_limits,
                                         subpel_start_mv)) {
              const int this_var = mv_search_params->find_fractional_mv_step(
                  xd, cm, &ms_params, subpel_start_mv, &this_best_mv, &dis,
                  &x->pred_sse[ref_pred], fractional_ms_list);
              if (this_var < best_mv_var) best_mv->as_mv = this_best_mv;
            }
          }
        } else {
          mv_search_params->find_fractional_mv_step(
              xd, cm, &ms_params, subpel_start_mv, &best_mv->as_mv, &dis,
              &x->pred_sse[ref_pred], NULL);
        }
        break;
      default: assert(0 && "Invalid motion mode!\n");
    }
  }
  *rate_mv = av1_mv_bit_cost(&best_mv->as_mv, &ref_mv, pb_mv_precision,
                             mv_costs, MV_COST_WEIGHT, is_adaptive_mvd);

#if !CONFIG_C071_SUBBLK_WARPMV
  assert(is_this_mv_precision_compliant(best_mv->as_mv, mbmi->pb_mv_precision));
#endif  // !CONFIG_C071_SUBBLK_WARPMV

  // Restore the motion mode
  if (mbmi->mode == WARPMV) mbmi->motion_mode = backup_motion_mode;
}

void av1_single_motion_search_high_precision(const AV1_COMP *const cpi,
                                             MACROBLOCK *x, BLOCK_SIZE bsize,
                                             int ref_idx, int *rate_mv,
                                             inter_mode_info *mode_info,
                                             const int_mv *start_mv,
                                             int_mv *best_mv) {
  MACROBLOCKD *xd = &x->e_mbd;
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *mbmi = xd->mi[0];
  struct buf_2d backup_yv12[MAX_MB_PLANE] = { { 0, 0, 0, 0, 0 } };
  int bestsme = INT_MAX;
  int_mv curr_best_mv;
  const int ref = mbmi->ref_frame[ref_idx];
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const MvCosts *mv_costs = &x->mv_costs;
  *best_mv = *start_mv;

#if CONFIG_OPFL_MV_SEARCH
  int opfl_its = get_opfl_mv_iterations(cpi, mbmi);
  int_mv cur_mv;
  int best_opfl_sme = INT_MAX;
  const int radix =
      1 << AOMMAX(0, MV_PRECISION_ONE_PEL - mbmi->pb_mv_precision);

  const int range_thr = OMVS_RANGE_THR * radix;
  const int step = OMVS_BIG_STEP * radix;
#endif  // CONFIG_OPFL_MV_SEARCH

  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  assert(!is_adaptive_mvd);

  if (scaled_ref_frame) {
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // full-pixel motion search code to be used without additional
    // modifications.
    for (int i = 0; i < num_planes; i++) {
      backup_yv12[i] = xd->plane[i].pre[ref_idx];
    }
    av1_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL,
                         num_planes, &mbmi->chroma_ref_info);
  }

  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  MV ref_mv_low_prec = av1_get_ref_mv(x, ref_idx).as_mv;
  FULLPEL_MV start_fullmv = get_fullmv_from_mv(&start_mv->as_mv);
  full_pel_lower_mv_precision(&start_fullmv, mbmi->pb_mv_precision);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(ref_mv_low_prec, &sub_mv_offset, mbmi->pb_mv_precision);
  if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
    lower_mv_precision(&ref_mv_low_prec, mbmi->pb_mv_precision);
  const MV ref_mv = ref_mv_low_prec;

#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 0;
#endif

  av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize, &ref_mv,
                                     pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                     is_ibc_cost,
#endif
                                     NULL, 0);
#if CONFIG_OPFL_MV_SEARCH
  FULLPEL_MV best_opfl_mv = start_fullmv;
  best_opfl_sme = INT_MAX;
  curr_best_mv = *start_mv;
  int term = 0;
  for (int it = 0; it < 1 + opfl_its; it++) {
    if (it > 0) {
      start_fullmv = curr_best_mv.as_fullmv;
      FULLPEL_MV prev_fullmv = start_fullmv;
      MV prev_mv = get_mv_from_fullmv(&prev_fullmv);
      cur_mv.as_mv = prev_mv;
      term = opfl_refine_fullpel_mv_one_sided(cm, xd, &full_ms_params, mbmi,
                                              &start_fullmv, &cur_mv, bsize);
      if (term) break;

      FULLPEL_MV cur_fullmv = get_fullmv_from_mv(&cur_mv.as_mv);
      full_pel_lower_mv_precision(&cur_fullmv, mbmi->pb_mv_precision);

      // If MV delta is so small that full pel MV local search is likely to
      // cover this delta, then up scale this delta. For delta being (0,0),
      // apply (1,-1)*big_step here.
      if (abs(cur_fullmv.row - prev_fullmv.row) <= range_thr &&
          abs(cur_fullmv.col - prev_fullmv.col) <= range_thr) {
        start_fullmv.row += cur_fullmv.row >= prev_fullmv.row ? step : -step;
        start_fullmv.col += cur_fullmv.col > prev_fullmv.col ? step : -step;
      } else {
        start_fullmv = cur_fullmv;
      }
    }
#endif  // CONFIG_OPFL_MV_SEARCH

    if (pb_mv_precision < MV_PRECISION_ONE_PEL)
      bestsme = av1_refining_search_8p_c_low_precision(
          &full_ms_params, start_fullmv, &curr_best_mv.as_fullmv,
          cpi->sf.flexmv_sf.fast_mv_refinement);
    else
      bestsme = av1_refining_search_8p_c(&full_ms_params, start_fullmv,
                                         &curr_best_mv.as_fullmv);

#if CONFIG_OPFL_MV_SEARCH
    if (bestsme < best_opfl_sme) {
      best_opfl_sme = bestsme;
      best_opfl_mv = curr_best_mv.as_fullmv;
    } else {
      break;
    }
  }
  bestsme = best_opfl_sme;
  curr_best_mv.as_fullmv = best_opfl_mv;
#endif  // CONFIG_OPFL_MV_SEARCH

  if (scaled_ref_frame) {
    // Swap back the original buffers for subpel motion search.
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[ref_idx] = backup_yv12[i];
    }
  }

  assert(is_this_mv_precision_compliant(
      get_mv_from_fullmv(&curr_best_mv.as_fullmv), mbmi->pb_mv_precision));

  // Terminate search with the current ref_idx if we have already encountered
  // another ref_mv in the drl such that:
  //  1. The other drl has the same fullpel_mv during the SIMPLE_TRANSLATION
  //     search process as the current fullpel_mv.
  //  2. The rate needed to encode the current fullpel_mv is larger than that
  //     for the other ref_mv.
  if (cpi->sf.flexmv_sf.skip_repeated_newmv_low_prec &&
      mbmi->pb_mv_precision != mbmi->max_mv_precision &&
      mbmi->motion_mode == SIMPLE_TRANSLATION &&
      curr_best_mv.as_int != INVALID_MV) {
    int_mv this_mv;
    this_mv.as_mv = get_mv_from_fullmv(&curr_best_mv.as_fullmv);
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
      this_mv.as_mv.row += sub_mv_offset.row;
      this_mv.as_mv.col += sub_mv_offset.col;
    }
#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_SEP_COMP_DRL
    const int ref_mv_idx = av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx);
#else
    const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
    const int this_mv_rate = av1_mv_bit_cost(
        &this_mv.as_mv, &ref_mv, pb_mv_precision, mv_costs, MV_COST_WEIGHT, 0);

    mode_info[ref_mv_idx].full_search_mv.as_int = this_mv.as_int;
    mode_info[ref_mv_idx].full_mv_rate = this_mv_rate;

    for (int prev_ref_idx = 0; prev_ref_idx < ref_mv_idx; ++prev_ref_idx) {
      // Check if the motion search result same as previous results
      if (this_mv.as_int == mode_info[prev_ref_idx].full_search_mv.as_int) {
        // Compare the rate cost
        const int prev_rate_cost = mode_info[prev_ref_idx].full_mv_rate +
                                   mode_info[prev_ref_idx].drl_cost;
        const int this_rate_cost =
            this_mv_rate + mode_info[ref_mv_idx].drl_cost;

        if (prev_rate_cost <= this_rate_cost) {
          // If the current rate_cost is worse than the previous rate_cost, then
          // we terminate the search. Since av1_single_motion_search is only
          // called by handle_new_mv in SIMPLE_TRANSLATION mode, we set the
          // best_mv to INVALID mv to signal that we wish to terminate search
          // for the current mode.
          curr_best_mv.as_int = INVALID_MV;
#if CONFIG_C071_SUBBLK_WARPMV
          best_mv->as_int = INVALID_MV;
#endif  // CONFIG_C071_SUBBLK_WARPMV
          return;
        }
      }
    }
  }

  assert(is_this_mv_precision_compliant(
      get_mv_from_fullmv(&curr_best_mv.as_fullmv), mbmi->pb_mv_precision));

  const int use_fractional_mv =
      bestsme < INT_MAX && cpi->common.features.cur_frame_force_integer_mv == 0;
  if (use_fractional_mv) {
    int dis; /* TODO: use dis in distortion calculation later. */
    unsigned int sse;
    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      NULL);
    // ms_params.forced_stop = EIGHTH_PEL;

    MV start_mv1 = get_mv_from_fullmv(&curr_best_mv.as_fullmv);
    assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, start_mv1));
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
      start_mv1.col += sub_mv_offset.col;
      start_mv1.row += sub_mv_offset.row;
    }
#endif  // CONFIG_C071_SUBBLK_WARPMV
    bestsme = cpi->mv_search_params.find_fractional_mv_step(
        xd, cm, &ms_params, start_mv1, &curr_best_mv.as_mv, &dis, &sse, NULL);
  }

  if (bestsme < INT_MAX) *best_mv = curr_best_mv;
  *rate_mv = av1_mv_bit_cost(&best_mv->as_mv, &ref_mv, pb_mv_precision,
                             mv_costs, MV_COST_WEIGHT, is_adaptive_mvd);
#if !CONFIG_C071_SUBBLK_WARPMV
  assert(is_this_mv_precision_compliant(best_mv->as_mv, mbmi->pb_mv_precision));
#endif  // !CONFIG_C071_SUBBLK_WARPMV
}

void av1_joint_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                             BLOCK_SIZE bsize, int_mv *cur_mv,
                             const uint8_t *mask, int mask_stride,
                             int *rate_mv) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const int pw = block_size_wide[bsize];
  const int ph = block_size_high[bsize];
  const int plane = 0;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  // This function should only ever be called for compound modes
  assert(has_second_ref(mbmi));
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  assert(!is_adaptive_mvd);
  // TODO(Mohammed): May not necessary, need to double check
  lower_mv_precision(&cur_mv[0].as_mv, pb_mv_precision);
  lower_mv_precision(&cur_mv[1].as_mv, pb_mv_precision);

  const int_mv init_mv[2] = { cur_mv[0], cur_mv[1] };
  const MV_REFERENCE_FRAME refs[2] = { mbmi->ref_frame[0], mbmi->ref_frame[1] };
  const MvCosts *mv_costs = &x->mv_costs;
  int_mv ref_mv[2];
  int ite, ref;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // Do joint motion search in compound mode to get more accurate mv.
  struct buf_2d backup_yv12[2][MAX_MB_PLANE];
  int last_besterr[2] = { INT_MAX, INT_MAX };
  const YV12_BUFFER_CONFIG *const scaled_ref_frame[2] = {
    av1_get_scaled_ref_frame(cpi, refs[0]),
    av1_get_scaled_ref_frame(cpi, refs[1])
  };

  // Prediction buffer from second frame.
  DECLARE_ALIGNED(16, uint16_t, second_pred[MAX_SB_SQUARE]);
  int_mv best_mv;

  // Allow joint search multiple times iteratively for each reference frame
  // and break out of the search loop if it couldn't find a better mv.
  for (ite = 0; ite < 4; ite++) {
    struct buf_2d ref_yv12[2];
    int bestsme = INT_MAX;
    // Even iterations search in the first reference frame, odd iterations
    // search in the second. The predictor found for the 'other' reference
    // frame is factored in.
    int id = ite % 2;

    if (ite >= 2 && cur_mv[!id].as_int == init_mv[!id].as_int) {
      if (cur_mv[id].as_int == init_mv[id].as_int) {
        break;
      } else {
        int_mv cur_int_mv, init_int_mv;
        cur_int_mv.as_mv.col = cur_mv[id].as_mv.col >> 3;
        cur_int_mv.as_mv.row = cur_mv[id].as_mv.row >> 3;
        init_int_mv.as_mv.row = init_mv[id].as_mv.row >> 3;
        init_int_mv.as_mv.col = init_mv[id].as_mv.col >> 3;
        if (cur_int_mv.as_int == init_int_mv.as_int) {
          break;
        }
      }
    }
    for (ref = 0; ref < 2; ++ref) {
      ref_mv[ref] = av1_get_ref_mv(x, ref);
      // Swap out the reference frame for a version that's been scaled to
      // match the resolution of the current frame, allowing the existing
      // motion search code to be used without additional modifications.
      if (scaled_ref_frame[ref]) {
        int i;
        for (i = 0; i < num_planes; i++)
          backup_yv12[ref][i] = xd->plane[i].pre[ref];
        av1_setup_pre_planes(xd, ref, scaled_ref_frame[ref], mi_row, mi_col,
                             NULL, num_planes, &mbmi->chroma_ref_info);
      }
    }

    assert(IMPLIES(scaled_ref_frame[0] != NULL,
                   cm->width == scaled_ref_frame[0]->y_crop_width &&
                       cm->height == scaled_ref_frame[0]->y_crop_height));
    assert(IMPLIES(scaled_ref_frame[1] != NULL,
                   cm->width == scaled_ref_frame[1]->y_crop_width &&
                       cm->height == scaled_ref_frame[1]->y_crop_height));

    // Initialize based on (possibly scaled) prediction buffers.
    ref_yv12[0] = xd->plane[plane].pre[0];
    ref_yv12[1] = xd->plane[plane].pre[1];

    InterPredParams inter_pred_params;
    const InterpFilter interp_filters = EIGHTTAP_REGULAR;

    av1_init_inter_params(&inter_pred_params, pw, ph, mi_row * MI_SIZE,
                          mi_col * MI_SIZE, 0, 0, xd->bd, 0, &cm->sf_identity,
                          &ref_yv12[!id], interp_filters);
    inter_pred_params.conv_params = get_conv_params(0, 0, xd->bd);

    // Since we have scaled the reference frames to match the size of the
    // current frame we must use a unit scaling factor during mode selection.
    av1_enc_build_one_inter_predictor(second_pred, pw, &cur_mv[!id].as_mv,
                                      &inter_pred_params);
    // Do full-pixel compound motion search on the current reference frame.
    if (id) {
      xd->plane[plane].pre[0] = ref_yv12[id];
      const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
      xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[id];
      xd->block_ref_scale_factors[id] = tmp_sf;
    }

#if CONFIG_IBC_BV_IMPROVEMENT
    const int is_ibc_cost = 0;
#endif  // CONFIG_IBC_BV_IMPROVEMENT

    // Make motion search params
    FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
    av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize,
                                       &ref_mv[id].as_mv, pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                       is_ibc_cost,
#endif  // CONFIG_IBC_BV_IMPROVEMENT

                                       NULL,
                                       /*fine_search_interval=*/0);

    av1_set_ms_compound_refs(&full_ms_params.ms_buffers, second_pred, mask,
                             mask_stride, id);

    // Use the mv result from the single mode as mv predictor.
    const FULLPEL_MV start_fullmv = get_fullmv_from_mv(&cur_mv[id].as_mv);

#if CONFIG_C071_SUBBLK_WARPMV
    MV sub_mv_offset = { 0, 0 };
    get_phase_from_mv(ref_mv[id].as_mv, &sub_mv_offset, pb_mv_precision);
#endif  // CONFIG_C071_SUBBLK_WARPMV

    // Small-range full-pixel motion search.
    if (pb_mv_precision < MV_PRECISION_ONE_PEL)
      bestsme = av1_refining_search_8p_c_low_precision(
          &full_ms_params, start_fullmv, &best_mv.as_fullmv,
          cpi->sf.flexmv_sf.fast_mv_refinement);
    else
      bestsme = av1_refining_search_8p_c(&full_ms_params, start_fullmv,
                                         &best_mv.as_fullmv);

    // Restore the pointer to the first (possibly scaled) prediction buffer.
    if (id) xd->plane[plane].pre[0] = ref_yv12[0];

    for (ref = 0; ref < 2; ++ref) {
      if (scaled_ref_frame[ref]) {
        // Swap back the original buffers for subpel motion search.
        for (int i = 0; i < num_planes; i++) {
          xd->plane[i].pre[ref] = backup_yv12[ref][i];
        }
        // Re-initialize based on unscaled prediction buffers.
        ref_yv12[ref] = xd->plane[plane].pre[ref];
      }
    }

    // Do sub-pixel compound motion search on the current reference frame.
    if (id) xd->plane[plane].pre[0] = ref_yv12[id];

    if (cpi->common.features.cur_frame_force_integer_mv) {
      convert_fullmv_to_mv(&best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
      if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
        best_mv.as_mv.col += sub_mv_offset.col;
        best_mv.as_mv.row += sub_mv_offset.row;
      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
    }
    if (bestsme < INT_MAX &&
        cpi->common.features.cur_frame_force_integer_mv == 0) {
      int dis; /* TODO: use dis in distortion calculation later. */
      unsigned int sse;
      SUBPEL_MOTION_SEARCH_PARAMS ms_params;
      av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                        &ref_mv[id].as_mv, pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                        0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                        NULL);
      av1_set_ms_compound_refs(&ms_params.var_params.ms_buffers, second_pred,
                               mask, mask_stride, id);
      ms_params.forced_stop = EIGHTH_PEL;
      MV start_mv = get_mv_from_fullmv(&best_mv.as_fullmv);
      assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, start_mv));
#if CONFIG_C071_SUBBLK_WARPMV
      if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
        start_mv.col += sub_mv_offset.col;
        start_mv.row += sub_mv_offset.row;
      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
      bestsme = cpi->mv_search_params.find_fractional_mv_step(
          xd, cm, &ms_params, start_mv, &best_mv.as_mv, &dis, &sse, NULL);
    }

    // Restore the pointer to the first prediction buffer.
    if (id) {
      xd->plane[plane].pre[0] = ref_yv12[0];
      const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
      xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[id];
      xd->block_ref_scale_factors[id] = tmp_sf;
    }
    if (bestsme < last_besterr[id]) {
      cur_mv[id] = best_mv;
      last_besterr[id] = bestsme;
    } else {
      break;
    }
  }

  *rate_mv = 0;
  for (ref = 0; ref < 2; ++ref) {
    const int_mv curr_ref_mv = av1_get_ref_mv(x, ref);
    *rate_mv += av1_mv_bit_cost(&cur_mv[ref].as_mv, &curr_ref_mv.as_mv,
                                mbmi->pb_mv_precision, mv_costs, MV_COST_WEIGHT,
                                is_adaptive_mvd);
  }
}

void av1_amvd_single_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                                   BLOCK_SIZE bsize, MV *this_mv, int *rate_mv,
                                   int ref_idx) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int ref = mbmi->ref_frame[ref_idx];
  const int_mv ref_mv = av1_get_ref_mv(x, ref_idx);
  struct macroblockd_plane *const pd = &xd->plane[0];
  const MvCosts *mv_costs = &x->mv_costs;

  assert(!is_pb_mv_precision_active(cm, mbmi, bsize));
#if BUGFIX_AMVD_AMVR
  set_amvd_mv_precision(mbmi, mbmi->max_mv_precision);
#else
  assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif  // BUGFIX_AMVD_AMVR

  struct buf_2d backup_yv12[MAX_MB_PLANE];
  const YV12_BUFFER_CONFIG *const scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref);

  // Store the first prediction buffer.
  struct buf_2d orig_yv12;
  if (ref_idx) {
    orig_yv12 = pd->pre[0];
    pd->pre[0] = pd->pre[ref_idx];
    const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
    xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[ref_idx];
    xd->block_ref_scale_factors[ref_idx] = tmp_sf;
  }

  if (scaled_ref_frame) {
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // full-pixel motion search code to be used without additional
    // modifications.
    for (int i = 0; i < num_planes; i++) {
      backup_yv12[i] = xd->plane[i].pre[0];
    }
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    av1_setup_pre_planes(xd, 0, scaled_ref_frame, mi_row, mi_col, NULL,
                         num_planes, &mbmi->chroma_ref_info);
  }

  int bestsme = INT_MAX;
  int_mv best_mv;

  int dis; /* TODO: use dis in distortion calculation later. */
  unsigned int sse;
  SUBPEL_MOTION_SEARCH_PARAMS ms_params;
  av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv.as_mv,
                                    mbmi->pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                    0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                    NULL);
  ms_params.forced_stop = EIGHTH_PEL;

  bestsme = adaptive_mvd_search(cm, xd, &ms_params, ref_mv.as_mv,
                                &best_mv.as_mv, &dis, &sse);
#if CONFIG_VQ_MVD_CODING
  if (bestsme == INT_MAX) best_mv.as_int = INVALID_MV;
#endif

  if (scaled_ref_frame) {
    // Swap back the original buffers for subpel motion search.
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = backup_yv12[i];
    }
  }
  // Restore the pointer to the first unscaled prediction buffer.
  if (ref_idx) {
    pd->pre[ref_idx] = pd->pre[0];
    pd->pre[0] = orig_yv12;
    const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
    xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[ref_idx];
    xd->block_ref_scale_factors[ref_idx] = tmp_sf;
  }

  if (bestsme < INT_MAX) {
    *this_mv = best_mv.as_mv;
    const MV diff = { best_mv.as_mv.row - ref_mv.as_mv.row,
                      best_mv.as_mv.col - ref_mv.as_mv.col };
    if (diff.row != 0 && diff.col != 0) {
      printf("assertion failure error! [%5d %5d ]\n", diff.row, diff.col);
    }
    assert(diff.row == 0 || diff.col == 0);
  }
#if CONFIG_VQ_MVD_CODING
  else {
    best_mv.as_int = INVALID_MV;
    return;
  }
#endif  // CONFIG_VQ_MVD_CODING
  *rate_mv = 0;
  *rate_mv +=
      av1_mv_bit_cost(this_mv, &ref_mv.as_mv, mbmi->pb_mv_precision, mv_costs,
                      MV_COST_WEIGHT, ms_params.mv_cost_params.is_adaptive_mvd);
}

// Search for the best mv for one component of a compound,
// given that the other component is fixed.
void av1_compound_single_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                                       BLOCK_SIZE bsize, MV *this_mv,
                                       MV *other_mv, uint16_t *second_pred,
                                       const uint8_t *mask, int mask_stride,
                                       int *rate_mv, int ref_idx) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const MV_REFERENCE_FRAME refs[2] = { mbmi->ref_frame[0],
                                       is_interintra_mode(mbmi)
                                           ? INTRA_FRAME
                                           : mbmi->ref_frame[1] };
  const int_mv ref_mv = av1_get_ref_mv(x, ref_idx);
  struct macroblockd_plane *const pd = &xd->plane[0];
  const MvCosts *mv_costs = &x->mv_costs;
  MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 0;
#endif

  struct buf_2d backup_yv12[2][MAX_MB_PLANE];
  const YV12_BUFFER_CONFIG *const scaled_ref_frame[2] = {
    av1_get_scaled_ref_frame(cpi, refs[0]),
    av1_get_scaled_ref_frame(cpi, refs[1])
  };

  // Check that this is either an interinter or an interintra block
  assert(has_second_ref(mbmi) || (ref_idx == 0 && is_interintra_mode(mbmi)));

  for (int idx = 0; idx < 2; idx++) {
    if (scaled_ref_frame[idx]) {
      // Swap out the reference frame for a version that's been scaled to
      // match the resolution of the current frame, allowing the existing
      // full-pixel motion search code to be used without additional
      // modifications.
      for (int i = 0; i < num_planes; i++) {
        backup_yv12[idx][i] = xd->plane[i].pre[idx];
      }
      const int mi_row = xd->mi_row;
      const int mi_col = xd->mi_col;
      av1_setup_pre_planes(xd, idx, scaled_ref_frame[idx], mi_row, mi_col, NULL,
                           num_planes, &mbmi->chroma_ref_info);
    }
  }

  InterPredParams inter_pred_params;
  if (is_joint_mvd_coding_mode(mbmi->mode)) {
    const int pw = block_size_wide[bsize];
    const int ph = block_size_high[bsize];
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    const int_mv ref_other_mv = av1_get_ref_mv(x, 1 - ref_idx);
    other_mv->row = ref_other_mv.as_mv.row;
    other_mv->col = ref_other_mv.as_mv.col;
    struct buf_2d ref_yv12 = xd->plane[0].pre[!ref_idx];
    av1_init_inter_params(&inter_pred_params, pw, ph, mi_row * MI_SIZE,
                          mi_col * MI_SIZE, 0, 0, xd->bd, 0, &cm->sf_identity,
                          &ref_yv12, EIGHTTAP_REGULAR);
    inter_pred_params.conv_params = get_conv_params(0, PLANE_TYPE_Y, xd->bd);
  }

  if (ref_idx) {
    struct buf_2d orig_yv12;
    orig_yv12 = pd->pre[0];
    pd->pre[0] = pd->pre[ref_idx];
    pd->pre[ref_idx] = orig_yv12;
    const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
    xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[ref_idx];
    xd->block_ref_scale_factors[ref_idx] = tmp_sf;
  }

  int bestsme = INT_MAX;
  int_mv best_mv;
  best_mv.as_mv = kZeroMv;
  int_mv best_other_mv;
  best_other_mv.as_mv = kZeroMv;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
#if BUGFIX_AMVD_AMVR
  if (is_adaptive_mvd) {
    set_amvd_mv_precision(mbmi, mbmi->max_mv_precision);
    pb_mv_precision = mbmi->pb_mv_precision;
  }
#endif
  if (is_adaptive_mvd && !is_joint_amvd_coding_mode(mbmi->mode)) {
    int dis; /* TODO: use dis in distortion calculation later. */
    unsigned int sse;
    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv.as_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      NULL);
    av1_set_ms_compound_refs(&ms_params.var_params.ms_buffers, second_pred,
                             mask, mask_stride, ref_idx);
    ms_params.forced_stop = EIGHTH_PEL;

    bestsme = adaptive_mvd_search(cm, xd, &ms_params, ref_mv.as_mv,
                                  &best_mv.as_mv, &dis, &sse);
    for (int idx = 0; idx < 2; idx++) {
      if (scaled_ref_frame[idx]) {
        // Swap back the original buffers for subpel motion search
        for (int i = 0; i < num_planes; i++) {
          xd->plane[i].pre[idx] = backup_yv12[idx][i];
        }
      }
    }

#if CONFIG_VQ_MVD_CODING
    if (bestsme == INT_MAX) best_mv.as_int = INVALID_MV;
#endif  // CONFIG_VQ_MVD_CODING
  } else if (mbmi->mode == JOINT_NEWMV || mbmi->mode == JOINT_NEWMV_OPTFLOW) {
    int dis; /* TODO: use dis in distortion calculation later. */
    unsigned int sse;
    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv.as_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      NULL);
    av1_set_ms_compound_refs(&ms_params.var_params.ms_buffers, second_pred,
                             mask, mask_stride, ref_idx);
    ms_params.forced_stop = EIGHTH_PEL;

    lower_mv_precision(this_mv, pb_mv_precision);
    if (pb_mv_precision < MV_PRECISION_ONE_PEL) {
      bestsme = low_precision_joint_mvd_search(
          cm, xd, &ms_params, ref_mv.as_mv, this_mv, &best_mv.as_mv, &dis, &sse,
          ref_idx, other_mv, &best_other_mv.as_mv, second_pred,
          &inter_pred_params);
    } else {
      bestsme = joint_mvd_search(cm, xd, &ms_params, ref_mv.as_mv, this_mv,
                                 &best_mv.as_mv, &dis, &sse, ref_idx, other_mv,
                                 &best_other_mv.as_mv, second_pred,
                                 &inter_pred_params, NULL);
      for (int idx = 0; idx < 2; idx++) {
        if (scaled_ref_frame[idx]) {
          // Swap back the original buffers for subpel motion search
          for (int i = 0; i < num_planes; i++) {
            xd->plane[i].pre[idx] = backup_yv12[idx][i];
          }
        }
      }
    }
  } else if (mbmi->mode == JOINT_AMVDNEWMV ||
             mbmi->mode == JOINT_AMVDNEWMV_OPTFLOW) {
    int dis; /* TODO: use dis in distortion calculation later. */
    unsigned int sse;
    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv.as_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      NULL);
    av1_set_ms_compound_refs(&ms_params.var_params.ms_buffers, second_pred,
                             mask, mask_stride, ref_idx);
    ms_params.forced_stop = EIGHTH_PEL;

    bestsme = av1_joint_amvd_motion_search(cm, xd, &ms_params,
#if CONFIG_C071_SUBBLK_WARPMV
                                           &ref_mv.as_mv,
#else
                                           this_mv,
#endif  // CONFIG_C071_SUBBLK_WARPMV
                                           &best_mv.as_mv, &dis, &sse, ref_idx,
                                           other_mv, &best_other_mv.as_mv,
                                           second_pred, &inter_pred_params);

    for (int idx = 0; idx < 2; idx++) {
      if (scaled_ref_frame[idx]) {
        // Swap back the original buffers for subpel motion search
        for (int i = 0; i < num_planes; i++) {
          xd->plane[i].pre[idx] = backup_yv12[idx][i];
        }
      }
    }

#if CONFIG_VQ_MVD_CODING
    if (bestsme == INT_MAX) {
      best_mv.as_int = INVALID_MV;
      best_other_mv.as_int = INVALID_MV;
    }
#endif  // CONFIG_VQ_MVD_CODING
  } else {
    // Make motion search params
    FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;

    av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize,
                                       &ref_mv.as_mv,

                                       pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                       is_ibc_cost,
#endif

                                       NULL,
                                       /*fine_search_interval=*/0);

    av1_set_ms_compound_refs(&full_ms_params.ms_buffers, second_pred, mask,
                             mask_stride, ref_idx);

    lower_mv_precision(this_mv, pb_mv_precision);

    // Use the mv result from the single mode as mv predictor.
    const FULLPEL_MV start_fullmv = get_fullmv_from_mv(this_mv);

#if CONFIG_C071_SUBBLK_WARPMV
    MV sub_mv_offset = { 0, 0 };
    get_phase_from_mv(ref_mv.as_mv, &sub_mv_offset, pb_mv_precision

    );
#endif  // CONFIG_C071_SUBBLK_WARPMV

    // Small-range full-pixel motion search.
    if (pb_mv_precision < MV_PRECISION_ONE_PEL) {
      bestsme = av1_refining_search_8p_c_low_precision(
          &full_ms_params, start_fullmv, &best_mv.as_fullmv,
          cpi->sf.flexmv_sf.fast_mv_refinement);
    } else {
      // Small-range full-pixel motion search.
      bestsme = av1_refining_search_8p_c(&full_ms_params, start_fullmv,
                                         &best_mv.as_fullmv);
    }

    for (int idx = 0; idx < 2; idx++) {
      if (scaled_ref_frame[idx]) {
        // Swap back the original buffers for subpel motion search
        for (int i = 0; i < num_planes; i++) {
          xd->plane[i].pre[idx] = backup_yv12[idx][i];
        }
      }
    }

    if (cpi->common.features.cur_frame_force_integer_mv) {
      convert_fullmv_to_mv(&best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
      if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
        best_mv.as_mv.col += sub_mv_offset.col;
        best_mv.as_mv.row += sub_mv_offset.row;
      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
    }
    const int use_fractional_mv =
        bestsme < INT_MAX &&
        cpi->common.features.cur_frame_force_integer_mv == 0;
    if (use_fractional_mv) {
      int dis; /* TODO: use dis in distortion calculation later. */
      unsigned int sse;
      SUBPEL_MOTION_SEARCH_PARAMS ms_params;
      av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                        &ref_mv.as_mv, pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                        0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                        NULL);
      av1_set_ms_compound_refs(&ms_params.var_params.ms_buffers, second_pred,
                               mask, mask_stride, ref_idx);
      ms_params.forced_stop = EIGHTH_PEL;
      MV start_mv = get_mv_from_fullmv(&best_mv.as_fullmv);
      assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, start_mv));
#if CONFIG_C071_SUBBLK_WARPMV
      if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
        start_mv.col += sub_mv_offset.col;
        start_mv.row += sub_mv_offset.row;
      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
      bestsme = cpi->mv_search_params.find_fractional_mv_step(
          xd, cm, &ms_params, start_mv, &best_mv.as_mv, &dis, &sse, NULL);
    }
  }

  // Restore the pointer to the first unscaled prediction buffer.
  if (ref_idx) {
    struct buf_2d orig_yv12 = pd->pre[0];
    pd->pre[0] = pd->pre[ref_idx];
    pd->pre[ref_idx] = orig_yv12;
    const struct scale_factors *tmp_sf = xd->block_ref_scale_factors[0];
    xd->block_ref_scale_factors[0] = xd->block_ref_scale_factors[ref_idx];
    xd->block_ref_scale_factors[ref_idx] = tmp_sf;
  }

  if (bestsme < INT_MAX
#if CONFIG_VQ_MVD_CODING
      || is_adaptive_mvd
#endif  // CONFIG_VQ_MVD_CODING
  )
    *this_mv = best_mv.as_mv;

  if (is_joint_mvd_coding_mode(mbmi->mode)) {
    if (bestsme < INT_MAX
#if CONFIG_VQ_MVD_CODING
        || is_adaptive_mvd
#endif  // CONFIG_VQ_MVD_CODING
    )
      *other_mv = best_other_mv.as_mv;
  }

  *rate_mv = 0;
  if (is_adaptive_mvd) {
#if CONFIG_VQ_MVD_CODING
    if (bestsme < INT_MAX) {
#endif  // CONFIG_VQ_MVD_CODING

      *rate_mv += av1_mv_bit_cost(this_mv, &ref_mv.as_mv, pb_mv_precision,
                                  mv_costs, MV_COST_WEIGHT, is_adaptive_mvd);
#if CONFIG_VQ_MVD_CODING
    }
#endif  // CONFIG_VQ_MVD_CODING
  } else {
    *rate_mv += av1_mv_bit_cost(this_mv, &ref_mv.as_mv, pb_mv_precision,
                                mv_costs, MV_COST_WEIGHT, is_adaptive_mvd);
  }
}

static AOM_INLINE void build_second_inter_pred(const AV1_COMP *cpi,
                                               MACROBLOCK *x, BLOCK_SIZE bsize,
                                               const MV *other_mv, int ref_idx,
                                               uint16_t *second_pred) {
  const AV1_COMMON *const cm = &cpi->common;
  const int pw = block_size_wide[bsize];
  const int ph = block_size_high[bsize];
  MACROBLOCKD *xd = &x->e_mbd;
  struct macroblockd_plane *const pd = &xd->plane[0];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int p_col = ((mi_col * MI_SIZE) >> pd->subsampling_x);
  const int p_row = ((mi_row * MI_SIZE) >> pd->subsampling_y);

  // This function should only ever be called for compound modes
  assert(has_second_ref(xd->mi[0]));

  const int plane = 0;
  struct buf_2d ref_yv12 = xd->plane[plane].pre[!ref_idx];

  struct scale_factors sf;
  av1_setup_scale_factors_for_frame(&sf, ref_yv12.width, ref_yv12.height,
                                    cm->width, cm->height);

  InterPredParams inter_pred_params;

  av1_init_inter_params(&inter_pred_params, pw, ph, p_row, p_col,
                        pd->subsampling_x, pd->subsampling_y, xd->bd, 0, &sf,
                        &ref_yv12, EIGHTTAP_REGULAR);
  inter_pred_params.conv_params = get_conv_params(0, plane, xd->bd);

  // Get the prediction block from the 'other' reference frame.
  av1_enc_build_one_inter_predictor(second_pred, pw, other_mv,
                                    &inter_pred_params);
}

// Wrapper for av1_compound_single_motion_search, for the common case
// where the second prediction is also an inter mode.
void av1_compound_single_motion_search_interinter(
    const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize, int_mv *cur_mv,
    const uint8_t *mask, int mask_stride, int *rate_mv, int ref_idx) {
  MACROBLOCKD *xd = &x->e_mbd;
  (void)xd;
  // This function should only ever be called for compound modes
  assert(has_second_ref(xd->mi[0]));

  // Prediction buffer from second frame.
  DECLARE_ALIGNED(16, uint16_t, second_pred_alloc_16[MAX_SB_SQUARE]);
  uint16_t *second_pred = (uint16_t *)second_pred_alloc_16;

  MV *this_mv = &cur_mv[ref_idx].as_mv;

  MV *other_mv = &cur_mv[!ref_idx].as_mv;

  build_second_inter_pred(cpi, x, bsize, other_mv, ref_idx, second_pred);
  av1_compound_single_motion_search(cpi, x, bsize, this_mv, other_mv,
                                    second_pred, mask, mask_stride, rate_mv,
                                    ref_idx);
}

static AOM_INLINE void do_masked_motion_search_indexed(
    const AV1_COMP *const cpi, MACROBLOCK *x, const int_mv *const cur_mv,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE bsize,
    int_mv *tmp_mv, int *rate_mv, int which) {
  // NOTE: which values: 0 - 0 only, 1 - 1 only, 2 - both
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  BLOCK_SIZE sb_type = mbmi->sb_type[PLANE_TYPE_Y];
  const uint8_t *mask;
  const int mask_stride = block_size_wide[bsize];

  mask = av1_get_compound_type_mask(comp_data, sb_type);

  tmp_mv[0].as_int = cur_mv[0].as_int;
  tmp_mv[1].as_int = cur_mv[1].as_int;
  if (which == 0 || which == 1) {
    av1_compound_single_motion_search_interinter(cpi, x, bsize, tmp_mv, mask,
                                                 mask_stride, rate_mv, which);
  } else if (which == 2) {
    av1_joint_motion_search(cpi, x, bsize, tmp_mv, mask, mask_stride, rate_mv);
  }
}

int av1_interinter_compound_motion_search(const AV1_COMP *const cpi,
                                          MACROBLOCK *x,
                                          const int_mv *const cur_mv,
                                          const BLOCK_SIZE bsize,
                                          const PREDICTION_MODE this_mode) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  int_mv tmp_mv[2];
  int tmp_rate_mv = 0;
  mbmi->interinter_comp.seg_mask = xd->seg_mask;
  const INTERINTER_COMPOUND_DATA *compound_data = &mbmi->interinter_comp;

  if (this_mode == NEW_NEWMV || this_mode == NEW_NEWMV_OPTFLOW) {
    do_masked_motion_search_indexed(cpi, x, cur_mv, compound_data, bsize,
                                    tmp_mv, &tmp_rate_mv, 2);
    mbmi->mv[0].as_int = tmp_mv[0].as_int;
    mbmi->mv[1].as_int = tmp_mv[1].as_int;
  } else if (have_nearmv_newmv_in_inter_mode(this_mode)) {
    const AV1_COMMON *const cm = &cpi->common;
    const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
    const int which =
        (NEWMV == compound_ref1_mode(this_mode) ||
         (is_joint_mvd_coding_mode(this_mode) && jmvd_base_ref_list));
    do_masked_motion_search_indexed(cpi, x, cur_mv, compound_data, bsize,
                                    tmp_mv, &tmp_rate_mv, which);
    mbmi->mv[which].as_int = tmp_mv[which].as_int;
    mbmi->mv[1 - which].as_int = tmp_mv[1 - which].as_int;
  }
  return tmp_rate_mv;
}

int_mv av1_simple_motion_search(AV1_COMP *const cpi, MACROBLOCK *x, int mi_row,
                                int mi_col, BLOCK_SIZE bsize, int ref,
                                FULLPEL_MV start_mv, int num_planes,
                                int use_subpixel) {
  assert(num_planes == 1 &&
         "Currently simple_motion_search only supports luma plane");
  assert(!frame_is_intra_only(&cpi->common) &&
         "Simple motion search only enabled for non-key frames");
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;

  set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);

  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->sb_type[PLANE_TYPE_Y] = bsize;
  mbmi->ref_frame[0] = ref;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->interp_fltr = EIGHTTAP_REGULAR;

#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_MORPH_PRED
  mbmi->morph_pred = 0;
#endif  // CONFIG_MORPH_PRED

  mbmi->cwp_idx = CWP_EQUAL;

  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);
  // This function is used only during the pruning process and is not used
  // during the actual motion search based on the prediction mode. Initialize to
  // mbmi->mode to NEWMV mode so that is_adaptive_mvd (in the function
  // av1_make_default_fullpel_ms_params) is disabled during pruning process
  mbmi->mode = NEWMV;

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV
#if CONFIG_WARP_INTER_INTRA
  mbmi->warp_inter_intra = 0;
#endif  // CONFIG_WARP_INTER_INTRA

  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, ref);
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref);
  struct buf_2d backup_yv12;
  // ref_mv is used to calculate the cost of the motion vector
  const MV ref_mv = kZeroMv;
  const int step_param =
      AOMMIN(cpi->mv_search_params.mv_step_param +
                 cpi->sf.part_sf.simple_motion_search_reduce_search_steps,
             MAX_MVSEARCH_STEPS - 2);
  const search_site_config *src_search_sites =
      cpi->mv_search_params.search_site_cfg[SS_CFG_SRC];
  int cost_list[5];
  const int ref_idx = 0;
  int var;
  int_mv best_mv;

  av1_setup_pre_planes(xd, ref_idx, yv12, mi_row, mi_col,
                       get_ref_scale_factors(cm, ref), num_planes, NULL);
  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  if (scaled_ref_frame) {
    backup_yv12 = xd->plane[AOM_PLANE_Y].pre[ref_idx];
    av1_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL,
                         num_planes, NULL);
  }

  // Allow more mesh searches for screen content type on the ARF.
  const int fine_search_interval = use_fine_search_interval(cpi);
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 0;
#endif

  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize, &ref_mv,
                                     pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                     is_ibc_cost,
#endif

                                     src_search_sites, fine_search_interval);
  full_pel_lower_mv_precision(&start_mv, pb_mv_precision);

  var = av1_full_pixel_search(start_mv, &full_ms_params, step_param,
                              cond_cost_list(cpi, cost_list),
                              &best_mv.as_fullmv, NULL);

  const int use_subpel_search =
      var < INT_MAX && !cpi->common.features.cur_frame_force_integer_mv &&
      use_subpixel;
  if (scaled_ref_frame) {
    xd->plane[AOM_PLANE_Y].pre[ref_idx] = backup_yv12;
  }
  if (use_subpel_search) {
    int not_used = 0;

    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv,
                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      cost_list);
    // TODO(yunqing): integrate this into av1_make_default_subpel_ms_params().
    ms_params.forced_stop = cpi->sf.mv_sf.simple_motion_subpel_force_stop;

    MV subpel_start_mv = get_mv_from_fullmv(&best_mv.as_fullmv);
    assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, subpel_start_mv));

    cpi->mv_search_params.find_fractional_mv_step(
        xd, cm, &ms_params, subpel_start_mv, &best_mv.as_mv, &not_used,
        &x->pred_sse[COMPACT_INDEX0_NRS(ref)], NULL);
  } else {
    // Manually convert from units of pixel to 1/8-pixels if we are not doing
    // subpel search
    convert_fullmv_to_mv(&best_mv);
  }

  mbmi->mv[0] = best_mv;

  // Get a copy of the prediction output
  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                AOM_PLANE_Y, AOM_PLANE_Y);

  aom_clear_system_state();

  if (scaled_ref_frame) {
    xd->plane[AOM_PLANE_Y].pre[ref_idx] = backup_yv12;
  }

  return best_mv;
}

int_mv av1_simple_motion_sse_var(AV1_COMP *cpi, MACROBLOCK *x, int mi_row,
                                 int mi_col, BLOCK_SIZE bsize,
                                 const FULLPEL_MV start_mv, int use_subpixel,
                                 unsigned int *sse, unsigned int *var) {
  MACROBLOCKD *xd = &x->e_mbd;
  const MV_REFERENCE_FRAME ref = get_closest_pastcur_ref_or_ref0(&cpi->common);

  int_mv best_mv = av1_simple_motion_search(cpi, x, mi_row, mi_col, bsize, ref,
                                            start_mv, 1, use_subpixel);

  const uint16_t *src = x->plane[0].src.buf;
  const int src_stride = x->plane[0].src.stride;
  const uint16_t *dst = xd->plane[0].dst.buf;
  const int dst_stride = xd->plane[0].dst.stride;

  *var = cpi->fn_ptr[bsize].vf(src, src_stride, dst, dst_stride, sse);

  return best_mv;
}

#if CONFIG_EXT_RECUR_PARTITIONS
/*!\brief A simplified version for motion search used for speed features. Same
 * as \ref av1_simple_motion_search, but it is used with ERP. */
int_mv av1_simple_motion_search_ext(AV1_COMP *const cpi,
                                    const TileInfo *const tile, MACROBLOCK *x,
                                    int mi_row, int mi_col, BLOCK_SIZE bsize,
                                    int ref, FULLPEL_MV start_mv,
                                    int num_planes, int use_subpixel,
                                    SimpleMotionData *sms_data) {
  assert(num_planes == 1 &&
         "Currently simple_motion_search only supports luma plane");
  assert(!frame_is_intra_only(&cpi->common) &&
         "Simple motion search only enabled for non-key frames");
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;

  // TODO(debargha,chiyotsai): Can we use set_offsets_for_motion_search()
  av1_set_offsets(cpi, tile, x, mi_row, mi_col, bsize, NULL);
  // set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);

  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->sb_type[0] = mbmi->sb_type[1] = bsize;
  mbmi->ref_frame[0] = ref;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->interp_fltr = EIGHTTAP_REGULAR;

#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_MORPH_PRED
  mbmi->morph_pred = 0;
#endif  // CONFIG_MORPH_PRED

  mbmi->cwp_idx = CWP_EQUAL;
  mbmi->mode = NEWMV;

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

#if CONFIG_WARP_INTER_INTRA
  mbmi->warp_inter_intra = 0;
#endif  // CONFIG_WARP_INTER_INTRA

  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, ref);
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref);
  struct buf_2d backup_yv12;
  // ref_mv is used to calculate the cost of the motion vector
  const MV ref_mv = kZeroMv;
  const int step_param =
      AOMMIN(cpi->mv_search_params.mv_step_param +
                 cpi->sf.part_sf.simple_motion_search_reduce_search_steps,
             MAX_MVSEARCH_STEPS - 2);
  const search_site_config *src_search_sites =
      cpi->mv_search_params.search_site_cfg[SS_CFG_SRC];
  int cost_list[5];
  const int ref_idx = 0;
  int var;
  int_mv best_mv;

  av1_setup_pre_planes(xd, ref_idx, yv12, mi_row, mi_col,
                       get_ref_scale_factors(cm, ref), num_planes, NULL);
  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  if (scaled_ref_frame) {
    backup_yv12 = xd->plane[AOM_PLANE_Y].pre[ref_idx];
    av1_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL,
                         num_planes, NULL);
  }

  // Allow more mesh searches for screen content type on the ARF.
  const int fine_search_interval = use_fine_search_interval(cpi);
  sms_data->sadpb = x->mv_costs.sadperbit;
  sms_data->errorperbit = x->mv_costs.errorperbit;

  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 0;
#endif

  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize, &ref_mv,
                                     pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                     is_ibc_cost,
#endif

                                     src_search_sites, fine_search_interval);

  full_pel_lower_mv_precision(&start_mv, pb_mv_precision);

  var = av1_full_pixel_search(start_mv, &full_ms_params, step_param,
                              cond_cost_list(cpi, cost_list),
                              &best_mv.as_fullmv, NULL);

  sms_data->fullmv = best_mv.as_mv;
  const int use_subpel_search =
      var < INT_MAX && !cpi->common.features.cur_frame_force_integer_mv &&
      use_subpixel;
  if (scaled_ref_frame) {
    xd->plane[AOM_PLANE_Y].pre[ref_idx] = backup_yv12;
  }
  if (use_subpel_search) {
    int not_used = 0;

    SUBPEL_MOTION_SEARCH_PARAMS ms_params;
    av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &ref_mv,

                                      pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                      0,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                      cost_list);
    // TODO(yunqing): integrate this into av1_make_default_subpel_ms_params().
    ms_params.forced_stop = cpi->sf.mv_sf.simple_motion_subpel_force_stop;

    MV subpel_start_mv = get_mv_from_fullmv(&best_mv.as_fullmv);
    assert(av1_is_subpelmv_in_range(&ms_params.mv_limits, subpel_start_mv));

    cpi->mv_search_params.find_fractional_mv_step(
        xd, cm, &ms_params, subpel_start_mv, &best_mv.as_mv, &not_used,
        &x->pred_sse[ref], NULL);
  } else {
    // Manually convert from units of pixel to 1/8-pixels if we are not doing
    // subpel search
    convert_fullmv_to_mv(&best_mv);
  }
  sms_data->submv = best_mv.as_mv;
  mbmi->mv[0] = best_mv;

  // Get a copy of the prediction output
  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                AOM_PLANE_Y, AOM_PLANE_Y);

  aom_clear_system_state();

  if (scaled_ref_frame) {
    xd->plane[AOM_PLANE_Y].pre[ref_idx] = backup_yv12;
  }

  return best_mv;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS
