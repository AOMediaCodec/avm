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
#include <math.h>
#include <stdio.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/common.h"
#include "av1/common/filter.h"
#include "av1/common/mvref_common.h"
#include "av1/common/reconinter.h"

#include "av1/common/cost.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#if CONFIG_VQ_MVD_CODING
#include "aom_dsp/binary_codes_writer.h"
#endif

#if !CONFIG_IBC_SUBPEL_PRECISION
static INLINE void init_mv_cost_params(MV_COST_PARAMS *mv_cost_params,
                                       const MvCosts *mv_costs,
                                       int is_adaptive_mvd, const MV *ref_mv,
                                       MvSubpelPrecision pb_mv_precision
#if CONFIG_IBC_BV_IMPROVEMENT
                                       ,
                                       const int is_ibc_cost
#endif

) {
  mv_cost_params->ref_mv = ref_mv;
  mv_cost_params->full_ref_mv = get_fullmv_from_mv(ref_mv);
  mv_cost_params->mv_cost_type = MV_COST_ENTROPY;

  mv_cost_params->mv_costs = mv_costs;
  mv_cost_params->pb_mv_precision = pb_mv_precision;

  mv_cost_params->is_adaptive_mvd = is_adaptive_mvd;

#if CONFIG_IBC_BV_IMPROVEMENT
  mv_cost_params->is_ibc_cost = is_ibc_cost;
#endif
}

#endif  //! CONFIG_IBC_SUBPEL_PRECISION

static INLINE void init_ms_buffers(MSBuffers *ms_buffers, const MACROBLOCK *x) {
  ms_buffers->ref = &x->e_mbd.plane[0].pre[0];
  ms_buffers->src = &x->plane[0].src;

  av1_set_ms_compound_refs(ms_buffers, NULL, NULL, 0, 0);

  ms_buffers->wsrc = x->obmc_buffer.wsrc;
  ms_buffers->obmc_mask = x->obmc_buffer.mask;
}

static AOM_INLINE SEARCH_METHODS
get_faster_search_method(SEARCH_METHODS search_method) {
  // Note on search method's accuracy:
  //  1. NSTEP
  //  2. DIAMOND
  //  3. BIGDIA \approx SQUARE
  //  4. HEX.
  //  5. FAST_HEX \approx FAST_DIAMOND
  switch (search_method) {
    case NSTEP: return DIAMOND;
    case DIAMOND: return BIGDIA;
    case BIGDIA: return HEX;
    case SQUARE: return HEX;
    case HEX: return FAST_HEX;
    case FAST_HEX: return FAST_HEX;
    case FAST_DIAMOND: return FAST_DIAMOND;
    case FAST_BIGDIA: return FAST_BIGDIA;
    default: assert(0 && "Invalid search method!"); return DIAMOND;
  }
}

void av1_make_default_fullpel_ms_params(
    FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const struct AV1_COMP *cpi,
    const MACROBLOCK *x, BLOCK_SIZE bsize, const MV *ref_mv,
    const MvSubpelPrecision pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
    const int is_ibc_cost,
#endif

    const search_site_config search_sites[NUM_DISTINCT_SEARCH_METHODS],
    int fine_search_interval) {
  const MV_SPEED_FEATURES *mv_sf = &cpi->sf.mv_sf;

  const MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];

  const int is_adaptive_mvd =
      enable_adaptive_mvd_resolution(&cpi->common, mbmi);

  ms_params->xd = xd;

  // High level params
  ms_params->bsize = bsize;
  ms_params->vfp = &cpi->fn_ptr[bsize];

  init_ms_buffers(&ms_params->ms_buffers, x);

  SEARCH_METHODS search_method = mv_sf->search_method;
  const int min_dim = AOMMIN(block_size_wide[bsize], block_size_high[bsize]);
  if (mv_sf->use_bsize_dependent_search_method) {
    if (min_dim >= 32) {
      search_method = get_faster_search_method(search_method);
    }
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  const int max_dim = AOMMAX(block_size_wide[bsize], block_size_high[bsize]);
  if (cpi->sf.mv_sf.fast_motion_estimation_on_block_256 && max_dim >= 256) {
    search_method = get_faster_search_method(search_method);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  // MV search of flex MV precision is supported only for NSTEP or DIAMOND
  // search
  if (cpi->common.seq_params.enable_flex_mvres &&
      (search_method != NSTEP && search_method != DIAMOND))
    search_method = NSTEP;

  av1_set_mv_search_method(ms_params, search_sites, search_method);

  const int use_downsampled_sad =
      mv_sf->use_downsampled_sad && block_size_high[bsize] >= 16;
  if (use_downsampled_sad) {
    ms_params->sdf = ms_params->vfp->sdsf;
    ms_params->sdx4df = ms_params->vfp->sdsx4df;
  } else {
    ms_params->sdf = ms_params->vfp->sdf;
    ms_params->sdx4df = ms_params->vfp->sdx4df;
  }

  ms_params->mesh_patterns[0] = mv_sf->mesh_patterns;
  ms_params->mesh_patterns[1] = mv_sf->intrabc_mesh_patterns;
  ms_params->force_mesh_thresh = mv_sf->exhaustive_searches_thresh;
  ms_params->prune_mesh_search = mv_sf->prune_mesh_search;
  ms_params->run_mesh_search = 0;
  ms_params->fine_search_interval = fine_search_interval;

  ms_params->is_intra_mode = 0;
  ms_params->fast_obmc_search =
      (pb_mv_precision == mbmi->max_mv_precision)
          ? mv_sf->obmc_full_pixel_search_level
          : cpi->sf.flexmv_sf.low_prec_obmc_full_pixel_search_level;

  ms_params->mv_limits = x->mv_limits;
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    av1_set_tip_mv_search_range(&ms_params->mv_limits);
  } else {
    av1_set_mv_search_range(&ms_params->mv_limits, ref_mv, pb_mv_precision

    );
  }
  // Mvcost params

  init_mv_cost_params(&ms_params->mv_cost_params, &x->mv_costs, is_adaptive_mvd,
                      ref_mv, pb_mv_precision
#if CONFIG_IBC_BV_IMPROVEMENT
                      ,
                      is_ibc_cost
#endif

  );
}

void av1_make_default_subpel_ms_params(SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                       const struct AV1_COMP *cpi,
                                       const MACROBLOCK *x, BLOCK_SIZE bsize,
                                       const MV *ref_mv,
                                       const MvSubpelPrecision pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                       const int is_ibc_cost,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                       const int *cost_list) {
  const AV1_COMMON *cm = &cpi->common;

#if CONFIG_IBC_BV_IMPROVEMENT && !CONFIG_IBC_SUBPEL_PRECISION
  const int is_ibc_cost = 0;
#endif

  const MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];

  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);

#if !BUGFIX_AMVD_AMVR
  assert(
      !(is_adaptive_mvd && (mbmi->pb_mv_precision != mbmi->max_mv_precision)));
#endif  // BUGFIX_AMVD_AMVR

  // High level params
  ms_params->forced_stop = cpi->sf.mv_sf.subpel_force_stop;
  ms_params->iters_per_step = cpi->sf.mv_sf.subpel_iters_per_step;
  ms_params->cost_list = cond_cost_list_const(cpi, cost_list);

  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    av1_set_tip_subpel_mv_search_range(&ms_params->mv_limits, &x->mv_limits);
  } else {
    av1_set_subpel_mv_search_range(
        &ms_params->mv_limits, &x->mv_limits, ref_mv, pb_mv_precision

    );
  }

  // Mvcost params
  init_mv_cost_params(&ms_params->mv_cost_params, &x->mv_costs, is_adaptive_mvd,
                      ref_mv, pb_mv_precision

#if CONFIG_IBC_BV_IMPROVEMENT
                      ,
                      is_ibc_cost
#endif
  );

  // Subpel variance params
  ms_params->var_params.vfp = &cpi->fn_ptr[bsize];
  ms_params->var_params.subpel_search_type = cpi->sf.mv_sf.subpel_search_type;

#if CONFIG_EXT_RECUR_PARTITIONS
  if (cpi->sf.mv_sf.fast_motion_estimation_on_block_256 &&
      AOMMAX(block_size_wide[bsize], block_size_high[bsize]) >= 256) {
    ms_params->var_params.subpel_search_type =
        AOMMIN(ms_params->var_params.subpel_search_type, USE_2_TAPS);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_IBC_SUBPEL_PRECISION
  if (is_ibc_cost) {
    ms_params->var_params.subpel_search_type =
        AOMMIN(ms_params->var_params.subpel_search_type, USE_2_TAPS);
  }
#endif  // CONFIG_IBC_SUBPEL_PRECISION

  ms_params->var_params.w = block_size_wide[bsize];
  ms_params->var_params.h = block_size_high[bsize];

  // Ref and src buffers
  MSBuffers *ms_buffers = &ms_params->var_params.ms_buffers;
  init_ms_buffers(ms_buffers, x);
  assert(ms_params->var_params.subpel_search_type &&
         "Subpel type 2_TAPS_ORIG is no longer supported!");
}

static INLINE int get_offset_from_fullmv(const FULLPEL_MV *mv, int stride) {
  return mv->row * stride + mv->col;
}

static INLINE const uint16_t *get_buf_from_fullmv(const struct buf_2d *buf,
                                                  const FULLPEL_MV *mv) {
  return &buf->buf[get_offset_from_fullmv(mv, buf->stride)];
}

void av1_set_mv_search_range(FullMvLimits *mv_limits, const MV *mv,
                             MvSubpelPrecision pb_mv_precision

) {
  //  We have to make sure the generated mv_limits
  //  are compatible with target precision.
  // prec_shift is the number of LSBs need to be 0 to make the mv/mv_limit
  // compatible
  const int prec_shift = (pb_mv_precision < MV_PRECISION_ONE_PEL)
                             ? (MV_PRECISION_ONE_PEL - pb_mv_precision)
                             : 0;

  const int max_full_mv = av1_lower_mv_limit(MAX_FULL_PEL_VAL, prec_shift);

  // Producing the reference mv value to the target precision
  FULLPEL_MV full_ref_mv = get_fullmv_from_mv(mv);
  MV low_prec_mv = { GET_MV_SUBPEL(full_ref_mv.row),
                     GET_MV_SUBPEL(full_ref_mv.col) };
  lower_mv_precision(&low_prec_mv, pb_mv_precision);

  // Calculate the outermost full-pixel MVs which are inside the limits set by
  // av1_set_subpel_mv_search_range().
  //
  // The subpel limits are simply mv->col +/- 8*MAX_FULL_PEL_VAL, and similar
  // for mv->row. We can then divide by 8 to find the fullpel MV limits. But
  // we have to be careful about the rounding. We want these bounds to be
  // at least as tight as the subpel limits, which means that we must round
  // the minimum values up and the maximum values down when dividing.
  int col_min = ((low_prec_mv.col + 7) >> 3) - max_full_mv;
  int row_min = ((low_prec_mv.row + 7) >> 3) - max_full_mv;
  int col_max = (low_prec_mv.col >> 3) + max_full_mv;
  int row_max = (low_prec_mv.row >> 3) + max_full_mv;

  col_min = AOMMAX(col_min, (MV_LOW >> 3) + (1 << prec_shift));
  row_min = AOMMAX(row_min, (MV_LOW >> 3) + (1 << prec_shift));
  col_max = AOMMIN(col_max, (MV_UPP >> 3) - (1 << prec_shift));
  row_max = AOMMIN(row_max, (MV_UPP >> 3) - (1 << prec_shift));

  full_pel_lower_mv_precision_one_comp(&mv_limits->col_min, pb_mv_precision, 0);
  full_pel_lower_mv_precision_one_comp(&mv_limits->row_min, pb_mv_precision, 0);
  full_pel_lower_mv_precision_one_comp(&mv_limits->col_max, pb_mv_precision, 1);
  full_pel_lower_mv_precision_one_comp(&mv_limits->row_max, pb_mv_precision, 1);

  // Get intersection of UMV window and valid MV window to reduce # of checks
  // in diamond search.
  if (mv_limits->col_min < col_min) mv_limits->col_min = col_min;
  if (mv_limits->col_max > col_max) mv_limits->col_max = col_max;
  if (mv_limits->row_min < row_min) mv_limits->row_min = row_min;
  if (mv_limits->row_max > row_max) mv_limits->row_max = row_max;

  mv_limits->col_max = AOMMAX(mv_limits->col_min, mv_limits->col_max);
  mv_limits->row_max = AOMMAX(mv_limits->row_min, mv_limits->row_max);
}

#if CONFIG_OPFL_MV_SEARCH
// Obtain number of iterations for optical flow based MV search.
int get_opfl_mv_iterations(const AV1_COMP *cpi, const MB_MODE_INFO *mbmi) {
  // Allowed only for screen content
  const AV1_COMMON *cm = &cpi->common;
  if (!cm->features.allow_screen_content_tools) return 0;

  if (mbmi->ref_frame[0] == NONE_FRAME) return 0;

  // Optical flow MV search is allowed for NEWMV and WARPMV only, since it
  // shows little improvements in compound modes.
  if (mbmi->mode == NEWMV || mbmi->mode == WARPMV) return 3;

  return 0;
}

// Derive a MVD based on optical flow method. In the two sided optical flow
// refinement implemented in av1_get_optflow_based_mv_highbd, two predicted
// blocks (P0, P1) are used to solve a MV delta, which is scaled based on d0
// and d1 to derive MVs of src relative to P0 and P1. Alternatively, this
// routine is a one sided optical flow solver, which uses the source block (src)
// and one predicted block (P0) to derives an MV delta, which is by itself
// relative to P0.
int opfl_refine_fullpel_mv_one_sided(
    const AV1_COMMON *cm, MACROBLOCKD *xd,
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, MB_MODE_INFO *mbmi,
    const FULLPEL_MV *const smv, int_mv *mv_refined, BLOCK_SIZE bsize) {
  (void)cm;
  (void)xd;
  (void)mbmi;
  int bw = block_size_wide[bsize];
  int bh = block_size_high[bsize];

  const struct buf_2d *const pred = ms_params->ms_buffers.ref;
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  uint16_t *pred_ptr = &pred->buf[smv->row * pred->stride + smv->col];

#if OMVS_EARLY_TERM
  // Early termination based on SAD
  // int sad = ms_params->vfp->sdf(dst0, bw, dst1, bw);
  int sad = ms_params->vfp->sdf(src->buf, src->stride, pred_ptr, pred->stride);
  if (sad < bw * bh * OMVS_SAD_THR) return 1;
#endif

  int vx0, vx1, vy0, vy1;
  int16_t *gx0, *gy0;
  uint16_t *dst0 = NULL, *dst1 = NULL;
  gx0 = (int16_t *)aom_memalign(16, bw * bh * sizeof(int16_t));
  gy0 = (int16_t *)aom_memalign(16, bw * bh * sizeof(int16_t));
  dst0 = (uint16_t *)aom_memalign(16, bw * bh * sizeof(uint16_t));
  dst1 = (uint16_t *)aom_memalign(16, bw * bh * sizeof(uint16_t));

  // Obrain Pred as dst0 and Cur as dst1
  aom_highbd_convolve_copy(pred_ptr, pred->stride, dst0, bw, bw, bh);
  aom_highbd_convolve_copy(src->buf, src->stride, dst1, bw, bw, bh);

  int grad_prec_bits;
  int16_t *tmp0 =
      (int16_t *)aom_memalign(16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(int16_t));
  int16_t *tmp1 =
      (int16_t *)aom_memalign(16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(int16_t));
  // tmp0 = (P0 + Cur) / 2, tmp1 = P0 - Cur
  if (bw < 8)
    av1_copy_pred_array_highbd_c(dst0, dst1, tmp0, tmp1, bw, bh, 1, -1, 1);
  else
    av1_copy_pred_array_highbd(dst0, dst1, tmp0, tmp1, bw, bh, 1, -1, 1);
  // Buffers gx0 and gy0 are used to store the gradients of tmp0
  av1_compute_subpel_gradients_interp(tmp0, bw, bh, &grad_prec_bits, gx0, gy0);
  int bits = 3 + get_opfl_mv_upshift_bits(mbmi);
#if OMVS_AVG_POOLING
  int n = AOMMIN(8, AOMMIN(bw, bh));
  // TODO(kslu) Make SIMD code support it
  av1_avg_pooling_pdiff_gradients_c(tmp1, bw, gx0, gy0, bw, bw, bh, n);
  // The SIMD version performs refinement for every 4x8 or 8x8 region. It is
  // only applicable when n == 8 in optical flow based MV search
  if (n == 8)
    av1_opfl_mv_refinement_nxn(tmp1, bw, gx0, gy0, bw, n, n, n, 1, 0,
                               grad_prec_bits, bits, &vx0, &vy0, &vx1, &vy1);
  else
    av1_opfl_mv_refinement(tmp1, bw, gx0, gy0, bw, n, n, 1, 0, grad_prec_bits,
                           bits, &vx0, &vy0, &vx1, &vy1);
#else
  av1_opfl_mv_refinement(tmp1, bw, gx0, gy0, bw, bw, bh, 1, 0, grad_prec_bits,
                         bits, &vx0, &vy0, &vx1, &vy1);
#endif

  aom_free(tmp0);
  aom_free(tmp1);
  aom_free(dst0);
  aom_free(dst1);
  aom_free(gx0);
  aom_free(gy0);
  mv_refined[0].as_mv.row += vy0;
  mv_refined[0].as_mv.col += vx0;
  return 0;
}
#endif  // CONFIG_OPFL_MV_SEARCH

void av1_set_tip_mv_search_range(FullMvLimits *mv_limits) {
  const int tmvp_mv = (TIP_MV_SEARCH_RANGE << TMVP_MI_SZ_LOG2);
  const int col_min = -tmvp_mv;
  const int row_min = -tmvp_mv;
  const int col_max = tmvp_mv;
  const int row_max = tmvp_mv;

  // Get intersection of UMV window and valid MV window to reduce # of checks
  // in diamond search.
  if (mv_limits->col_min < col_min) mv_limits->col_min = col_min;
  if (mv_limits->col_max > col_max) mv_limits->col_max = col_max;
  if (mv_limits->row_min < row_min) mv_limits->row_min = row_min;
  if (mv_limits->row_max > row_max) mv_limits->row_max = row_max;
}

int av1_init_search_range(int size) {
  int sr = 0;
  // Minimum search size no matter what the passed in value.
  size = AOMMAX(16, size);

  while ((size << sr) < MAX_FULL_PEL_VAL) sr++;

  sr = AOMMIN(sr, MAX_MVSEARCH_STEPS - 2);
  return sr;
}

// ============================================================================
//  Cost of motion vectors
// ============================================================================
// TODO(any): Adaptively adjust the regularization strength based on image size
// and motion activity instead of using hard-coded values. It seems like we
// roughly half the lambda for each increase in resolution
// These are multiplier used to perform regularization in motion compensation
// when x->mv_cost_type is set to MV_COST_L1.
// LOWRES
#define SSE_LAMBDA_LOWRES 2   // Used by mv_cost_err_fn
#define SAD_LAMBDA_LOWRES 32  // Used by mvsad_err_cost during full pixel search
// MIDRES
#define SSE_LAMBDA_MIDRES 0   // Used by mv_cost_err_fn
#define SAD_LAMBDA_MIDRES 15  // Used by mvsad_err_cost during full pixel search
// HDRES
#define SSE_LAMBDA_HDRES 1  // Used by mv_cost_err_fn
#define SAD_LAMBDA_HDRES 8  // Used by mvsad_err_cost during full pixel search

#if !CONFIG_VQ_MVD_CODING
// Returns the rate of encoding the current motion vector based on the
// joint_cost and comp_cost. joint_costs covers the cost of transmitting
// JOINT_MV, and comp_cost covers the cost of transmitting the actual motion
// vector.
static INLINE int mv_cost(const MV *mv, const int *joint_cost,
                          const int *const comp_cost[2]) {
  return joint_cost[av1_get_mv_joint(mv)] + comp_cost[0][mv->row] +
         comp_cost[1][mv->col];
}
#endif  //! CONFIG_VQ_MVD_CODING

#define CONVERT_TO_CONST_MVCOST(ptr) ((const int *const *)(ptr))
#if CONFIG_VQ_MVD_CODING
static INLINE int get_vq_col_mvd_cost(const MvCosts *mv_costs,
                                      const MvSubpelPrecision pb_mv_precision,
                                      const int max_coded_value, int col,
                                      int max_trunc_unary_value,
                                      int is_ibc_cost,
                                      const IntraBCMvCosts *dv_costs) {
  int cost = 0;
  int max_idx_bits = AOMMIN(max_coded_value, max_trunc_unary_value);
  assert(max_idx_bits > 0);
  const int coded_col =
      col > max_trunc_unary_value ? max_trunc_unary_value : col;
  cost =
      is_ibc_cost
          ? (dv_costs
#if CONFIG_IBC_SUBPEL_PRECISION
                 ? dv_costs
                       ->dv_col_mv_greater_flags_costs[pb_mv_precision]
                                                      [max_idx_bits][coded_col]
                 : mv_costs
                       ->dv_col_mv_greater_flags_costs[pb_mv_precision]
                                                      [max_idx_bits][coded_col])
#else
                 ? dv_costs
                       ->dv_col_mv_greater_flags_costs[max_idx_bits][coded_col]
                 : mv_costs
                       ->dv_col_mv_greater_flags_costs[max_idx_bits][coded_col])
#endif  // CONFIG_IBC_SUBPEL_PRECISION
          : mv_costs->col_mv_greater_flags_costs[pb_mv_precision][max_idx_bits]
                                                [coded_col];
  if (max_coded_value > max_trunc_unary_value && col >= max_trunc_unary_value) {
    int remainder = col - max_trunc_unary_value;
    int remainder_max_value = max_coded_value - max_trunc_unary_value;
    int length =
        aom_count_primitive_quniform(remainder_max_value + 1, remainder);
    cost += av1_cost_literal(length);
  }
  return cost;
}

static INLINE int get_vq_amvd_cost(const MV mv_diff, const MvCosts *mv_costs) {
  int total_cost = 0;
  const MV mv_diff_index = { get_index_from_amvd_mvd(mv_diff.row),
                             get_index_from_amvd_mvd(mv_diff.col) };
  assert(abs(mv_diff_index.row) <= MAX_AMVD_INDEX);
  assert(abs(mv_diff_index.col) <= MAX_AMVD_INDEX);
  total_cost +=
      mv_costs
          ->amvd_index_mag_cost[abs(mv_diff_index.row)][abs(mv_diff_index.col)];
  if (mv_diff_index.row) {
    int sign = mv_diff_index.row < 0;
    total_cost += mv_costs->amvd_index_sign_cost[0][sign];
  }
  if (mv_diff_index.col) {
    int sign = mv_diff_index.col < 0;
    total_cost += mv_costs->amvd_index_sign_cost[1][sign];
  }
  return total_cost;
}

static INLINE int get_vq_mvd_cost(const MV mv_diff,
                                  const MvSubpelPrecision pb_mv_precision,
                                  const MvCosts *mv_costs, int is_adaptive_mvd,
                                  int is_ibc_cost,
                                  const IntraBCMvCosts *dv_costs) {
  if (is_adaptive_mvd) {
    return get_vq_amvd_cost(mv_diff, mv_costs);
  }
  int total_cost = 0;
  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision);
  const MV scaled_mv_diff = { abs(mv_diff.row) >> start_lsb,
                              abs(mv_diff.col) >> start_lsb };

  const int shell_index = (scaled_mv_diff.row) + (scaled_mv_diff.col);
  assert(shell_index <= ((2 * MV_MAX) >> start_lsb));
  const int *nmv_joint_shell_cost =
#if CONFIG_IBC_SUBPEL_PRECISION
      is_ibc_cost ? (dv_costs ? dv_costs->dv_joint_shell_cost[pb_mv_precision]
                              : mv_costs->dv_joint_shell_cost[pb_mv_precision])
#else
      is_ibc_cost ? (dv_costs ? dv_costs->dv_joint_shell_cost
                              : mv_costs->dv_joint_shell_cost)
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                  : (mv_costs->nmv_joint_shell_cost[pb_mv_precision]);
  assert(nmv_joint_shell_cost);
  total_cost += nmv_joint_shell_cost[shell_index];

  assert(scaled_mv_diff.col <= shell_index);
  assert(IMPLIES(shell_index == 0, scaled_mv_diff.col == 0));
  // Compute cost of column
  // For a given shell_index compute the cost of col_mvd
  int col_cost = 0;
  if (shell_index > 0) {
    int max_trunc_unary_value = MAX_COL_TRUNCATED_UNARY_VAL;
    // Coding the col here
    int maximum_pair_index = shell_index >> 1;
    int this_pair_index = scaled_mv_diff.col <= maximum_pair_index
                              ? scaled_mv_diff.col
                              : shell_index - scaled_mv_diff.col;
    assert(this_pair_index <= maximum_pair_index);
    // Encode the pair index
    if (maximum_pair_index > 0) {
      col_cost += get_vq_col_mvd_cost(
          mv_costs, pb_mv_precision, maximum_pair_index, this_pair_index,
          max_trunc_unary_value, is_ibc_cost, dv_costs);
    }
    int skip_coding_col_bit =
        (this_pair_index == maximum_pair_index) && ((shell_index % 2 == 0));
    assert(
        IMPLIES(skip_coding_col_bit, scaled_mv_diff.col == maximum_pair_index));
    if (!skip_coding_col_bit) {
#ifndef NDEBUG
      int num_mv_class = get_default_num_shell_class(pb_mv_precision);
#endif
      int shell_cls_offset;
      const int shell_class =
          get_shell_class_with_precision(shell_index, &shell_cls_offset);
      assert(shell_class >= 0 && shell_class < num_mv_class);
      int context_index = shell_class < NUM_CTX_COL_MV_INDEX
                              ? shell_class
                              : NUM_CTX_COL_MV_INDEX - 1;
      assert(context_index < NUM_CTX_COL_MV_INDEX);
      col_cost +=
          is_ibc_cost
#if CONFIG_IBC_SUBPEL_PRECISION
              ? (dv_costs ? dv_costs->dv_col_mv_index_cost[pb_mv_precision]
                                                          [context_index]
                                                          [scaled_mv_diff.col >
                                                           maximum_pair_index]
                          : mv_costs->dv_col_mv_index_cost[pb_mv_precision]
                                                          [context_index]
                                                          [scaled_mv_diff.col >
                                                           maximum_pair_index])
#else
              ? (dv_costs ? dv_costs->dv_col_mv_index_cost[context_index]
                                                          [scaled_mv_diff.col >
                                                           maximum_pair_index]
                          : mv_costs->dv_col_mv_index_cost[context_index]
                                                          [scaled_mv_diff.col >
                                                           maximum_pair_index])
#endif  // CONFIG_IBC_SUBPEL_PRECISION

              : mv_costs->col_mv_index_cost[pb_mv_precision][context_index]
                                           [scaled_mv_diff.col >
                                            maximum_pair_index];
    }
  }
  total_cost += col_cost;

  // Add sign costs
  int sign_costs = 0;
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff.row : mv_diff.col;
    if (value) {
      sign_costs +=
          is_ibc_cost
#if CONFIG_IBC_SUBPEL_PRECISION
              ? (dv_costs
                     ? dv_costs
                           ->dv_sign_cost[pb_mv_precision][component][value < 0]
                     : mv_costs->dv_sign_cost[pb_mv_precision][component]
                                             [value < 0])
#else
              ? (dv_costs ? dv_costs->dv_sign_cost[component][value < 0]
                          : mv_costs->dv_sign_cost[component][value < 0])
#endif  // CONFIG_IBC_SUBPEL_PRECISION
              : mv_costs->nmv_sign_cost[component][value < 0];
    }
  }
  total_cost += sign_costs;

  // printf(" total cost = %d \n", total_cost);
  return total_cost;
}

#endif  // CONFIG_VQ_MVD_CODING
static INLINE int get_mv_cost_with_precision(
    const MV mv, const MV ref_mv, const MvSubpelPrecision pb_mv_precision,
    const int is_adaptive_mvd,
#if CONFIG_IBC_BV_IMPROVEMENT
    const int is_ibc_cost,
#endif
    const MvCosts *mv_costs, int weight, int round_bits) {

  MV low_prec_ref_mv = ref_mv;
#if BUGFIX_AMVD_AMVR
  if (!is_adaptive_mvd)
#endif
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { mv.row - low_prec_ref_mv.row,
                    mv.col - low_prec_ref_mv.col };
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_VQ_MVD_CODING
  assert(IMPLIES(!is_adaptive_mvd,
                 is_this_mv_precision_compliant(diff, pb_mv_precision)));
#else
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));
#endif  // CONFIG_VQ_MVD_CODING
#endif  // CONFIG_C071_SUBBLK_WARPMV

#if CONFIG_VQ_MVD_CODING
  return (int)ROUND_POWER_OF_TWO_64(
      (int64_t)get_vq_mvd_cost(diff, pb_mv_precision, mv_costs, is_adaptive_mvd,
                               is_ibc_cost, NULL) *
          weight,
      round_bits);
#else
  const int *mvjcost =
      is_adaptive_mvd
          ? mv_costs->amvd_nmv_joint_cost
#if CONFIG_IBC_BV_IMPROVEMENT
          : (is_ibc_cost ? mv_costs->dv_joint_cost : mv_costs->nmv_joint_cost);
#else
          : mv_costs->nmv_joint_cost;
#endif
  const int *const *mvcost =
      is_adaptive_mvd
          ? CONVERT_TO_CONST_MVCOST(mv_costs->amvd_nmv_cost)
#if CONFIG_IBC_BV_IMPROVEMENT
          : (is_ibc_cost ? CONVERT_TO_CONST_MVCOST(mv_costs->dv_nmv_cost)
                         : CONVERT_TO_CONST_MVCOST(
                               mv_costs->nmv_costs[pb_mv_precision]));
#else
          : CONVERT_TO_CONST_MVCOST(mv_costs->nmv_costs[pb_mv_precision]);
#endif

  if (mvcost) {
    return (int)ROUND_POWER_OF_TWO_64(
        ((int64_t)mv_cost(&diff, mvjcost, mvcost)) * weight, round_bits);
  }
#endif  // CONFIG_VQ_MVD_CODING

  return 0;
}

static INLINE int get_intrabc_mv_cost_with_precision(
    const MV diff, const IntraBCMvCosts *dv_costs, int weight, int round_bits
#if CONFIG_IBC_SUBPEL_PRECISION
    ,
    MvSubpelPrecision precision
#endif
) {
#if CONFIG_VQ_MVD_CODING
  if (dv_costs) {
#if CONFIG_IBC_SUBPEL_PRECISION
    return (int)ROUND_POWER_OF_TWO_64(
        (int64_t)get_vq_mvd_cost(diff, precision, NULL, 0, 1, dv_costs) *
            weight,
        round_bits);
#else
    return (int)ROUND_POWER_OF_TWO_64(
        (int64_t)get_vq_mvd_cost(diff, MV_PRECISION_ONE_PEL, NULL, 0, 1,
                                 dv_costs) *
            weight,
        round_bits);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
  }
#else

  const int *dvjcost = dv_costs->joint_mv;
  const int *const *dvcost = CONVERT_TO_CONST_MVCOST(dv_costs->dv_costs);

  if (dv_costs) {
    return (int)ROUND_POWER_OF_TWO_64(
        (int64_t)mv_cost(&diff, dvjcost, dvcost) * weight, round_bits);
  }

#endif  // CONFIG_VQ_MVD_CODING

  return 0;
}

#if CONFIG_DERIVED_MVD_SIGN
int av1_mv_sign_cost(const int sign, const int comp, const MvCosts *mv_costs,
                     int weight, int round_bit, const int is_adaptive_mvd

) {
#if CONFIG_VQ_MVD_CODING
  assert(!is_adaptive_mvd);
  const int *mv_sign_cost = mv_costs->nmv_sign_cost[comp];
  (void)is_adaptive_mvd;
#else
  const int *mv_sign_cost = is_adaptive_mvd ? mv_costs->amvd_nmv_sign_cost[comp]
                                            : mv_costs->nmv_sign_cost[comp];
#endif  // CONFIG_VQ_MVD_CODING
  if (mv_sign_cost) {
    return (int)ROUND_POWER_OF_TWO_64((int64_t)mv_sign_cost[sign] * weight,
                                      round_bit);
  }
  return 0;
}
#endif  // CONFIG_DERIVED_MVD_SIGN

// Returns the cost of encoding the motion vector diff := *mv - *ref. The cost
// is defined as the rate required to encode diff * weight, rounded to the
// nearest 2 ** 7.
// This is NOT used during motion compensation.
int av1_mv_bit_cost(const MV *mv, const MV *ref_mv,
                    const MvSubpelPrecision pb_mv_precision,
                    const MvCosts *mv_costs, int weight,
                    const int is_adaptive_mvd) {
#if CONFIG_IBC_BV_IMPROVEMENT
  // For ibc block this function should not be called
  const int is_ibc_cost = 0;
#endif

  return get_mv_cost_with_precision(*mv, *ref_mv, pb_mv_precision,
                                    is_adaptive_mvd,
#if CONFIG_IBC_BV_IMPROVEMENT
                                    is_ibc_cost,
#endif
                                    mv_costs, weight, 7);
}

int av1_intrabc_mv_bit_cost(const MV *mv, const MV *ref_mv,
                            const IntraBCMvCosts *mv_costs, int weight
#if CONFIG_IBC_SUBPEL_PRECISION
                            ,
                            MvSubpelPrecision precision
#endif  // CONFIG_IBC_SUBPEL_PRECISION
) {
  const MV diff = { mv->row - ref_mv->row, mv->col - ref_mv->col };
  return get_intrabc_mv_cost_with_precision(diff, mv_costs, weight, 7
#if CONFIG_IBC_SUBPEL_PRECISION
                                            ,
                                            precision
#endif  // CONFIG_IBC_SUBPEL_PRECISION
  );
}
// Returns the cost of using the current mv during the motion search. This is
// used when var is used as the error metric.
#define PIXEL_TRANSFORM_ERROR_SCALE 4
static INLINE int mv_err_cost(const MV mv,
                              const MV_COST_PARAMS *mv_cost_params) {
  const MV ref_mv = *mv_cost_params->ref_mv;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  const MV_COST_TYPE mv_cost_type = mv_cost_params->mv_cost_type;
  const MvCosts *mv_costs = mv_cost_params->mv_costs;
  MV low_prec_ref_mv = ref_mv;
#if BUGFIX_AMVD_AMVR
  if (!mv_cost_params->is_adaptive_mvd)
#endif
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { mv.row - low_prec_ref_mv.row,
                    mv.col - low_prec_ref_mv.col };
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_VQ_MVD_CODING
  assert(IMPLIES(!mv_cost_params->is_adaptive_mvd,
                 is_this_mv_precision_compliant(diff, pb_mv_precision)));
#else
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));
#endif  // CONFIG_VQ_MVD_CODING
#endif  // CONFIG_C071_SUBBLK_WARPMV

  const MV abs_diff = { abs(diff.row), abs(diff.col) };

  switch (mv_cost_type) {
    case MV_COST_ENTROPY:
      return get_mv_cost_with_precision(
          mv, ref_mv, mv_cost_params->pb_mv_precision,
          mv_cost_params->is_adaptive_mvd,
#if CONFIG_IBC_BV_IMPROVEMENT
          mv_cost_params->is_ibc_cost,
#endif
          mv_costs, mv_costs->errorperbit,
          RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT +
              PIXEL_TRANSFORM_ERROR_SCALE);

    case MV_COST_L1_LOWRES:
      return (SSE_LAMBDA_LOWRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_MIDRES:
      return (SSE_LAMBDA_MIDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_HDRES:
      return (SSE_LAMBDA_HDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_NONE: return 0;
    default: assert(0 && "Invalid rd_cost_type"); return 0;
  }
}

// Returns the cost of using the current mv during the motion search. This is
// only used during full pixel motion search when sad is used as the error
// metric
static INLINE int mvsad_err_cost(const FULLPEL_MV mv,
                                 const MV_COST_PARAMS *mv_cost_params) {
  MV this_mv = { GET_MV_SUBPEL(mv.row), GET_MV_SUBPEL(mv.col) };
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;

  MV ref_mv = { GET_MV_SUBPEL(mv_cost_params->full_ref_mv.row),
                GET_MV_SUBPEL(mv_cost_params->full_ref_mv.col) };

#if BUGFIX_AMVD_AMVR
  if (!mv_cost_params->is_adaptive_mvd)
#endif
    lower_mv_precision(&ref_mv, pb_mv_precision);

  const MV diff = { (this_mv.row - ref_mv.row), (this_mv.col - ref_mv.col) };

  const MV abs_diff = { abs(diff.row), abs(diff.col) };

  const MvCosts *mv_costs = mv_cost_params->mv_costs;

#if !CONFIG_VQ_MVD_CODING
#if CONFIG_IBC_BV_IMPROVEMENT
  const int *mvjcost =
#if !CONFIG_VQ_MVD_CODING
      mv_cost_params->is_ibc_cost ? mv_costs->dv_joint_cost :
#endif  //! CONFIG_VQ_MVD_CODING
                                  (mv_cost_params->is_adaptive_mvd
                                       ? mv_costs->amvd_nmv_joint_cost
                                       : mv_costs->nmv_joint_cost);

  const int *const *mvcost =
#if !CONFIG_VQ_MVD_CODING
      mv_cost_params->is_ibc_cost
          ? CONVERT_TO_CONST_MVCOST(mv_costs->dv_nmv_cost)
          :
#endif  // !CONFIG_VQ_MVD_CODING
          (mv_cost_params->is_adaptive_mvd
               ? CONVERT_TO_CONST_MVCOST(mv_costs->amvd_nmv_cost)
               : CONVERT_TO_CONST_MVCOST(mv_costs->nmv_costs[pb_mv_precision]));
#else
  const int *mvjcost = mv_cost_params->is_adaptive_mvd
                           ? mv_costs->amvd_nmv_joint_cost
                           : mv_costs->nmv_joint_cost;
  const int *const *mvcost =
      mv_cost_params->is_adaptive_mvd
          ? CONVERT_TO_CONST_MVCOST(mv_costs->amvd_nmv_cost)
          : CONVERT_TO_CONST_MVCOST(mv_costs->nmv_costs[pb_mv_precision]);
#endif
#endif  // !CONFIG_VQ_MVD_CODING

  const int sad_per_bit = mv_costs->sadperbit;

  const MV_COST_TYPE mv_cost_type = mv_cost_params->mv_cost_type;

  switch (mv_cost_type) {
    case MV_COST_ENTROPY:
#if CONFIG_VQ_MVD_CODING
      return ROUND_POWER_OF_TWO(
          (unsigned)get_vq_mvd_cost(diff, pb_mv_precision, mv_costs,
                                    mv_cost_params->is_adaptive_mvd,
                                    mv_cost_params->is_ibc_cost, NULL) *
              sad_per_bit,
          AV1_PROB_COST_SHIFT);
#else
      return ROUND_POWER_OF_TWO(
          (unsigned)mv_cost(&diff, mvjcost, mvcost) * sad_per_bit,
          AV1_PROB_COST_SHIFT);
#endif  // CONFIG_VQ_MVD_CODING
    case MV_COST_L1_LOWRES:
      return (SAD_LAMBDA_LOWRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_MIDRES:
      return (SAD_LAMBDA_MIDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_L1_HDRES:
      return (SAD_LAMBDA_HDRES * (abs_diff.row + abs_diff.col)) >> 3;
    case MV_COST_NONE: return 0;
    default: assert(0 && "Invalid rd_cost_type"); return 0;
  }
}

// =============================================================================
//  Fullpixel Motion Search: Translational
// =============================================================================
#define MAX_PATTERN_SCALES 11
#define MAX_PATTERN_CANDIDATES 8  // max number of candidates per scale
#define PATTERN_CANDIDATES_REF 3  // number of refinement candidates

void av1_init_dsmotion_compensation(search_site_config *cfg, int stride) {
  int num_search_steps = 0;
  int stage_index = MAX_MVSEARCH_STEPS - 1;

  cfg->site[stage_index][0].mv.col = cfg->site[stage_index][0].mv.row = 0;
  cfg->site[stage_index][0].offset = 0;
  cfg->stride = stride;

  for (int radius = MAX_FIRST_STEP; radius > 0; radius /= 2) {
    int num_search_pts = 8;

    const FULLPEL_MV search_site_mvs[13] = {
      { 0, 0 },           { -radius, 0 },      { radius, 0 },
      { 0, -radius },     { 0, radius },       { -radius, -radius },
      { radius, radius }, { -radius, radius }, { radius, -radius },
    };

    int i;
    for (i = 0; i <= num_search_pts; ++i) {
      search_site *const site = &cfg->site[stage_index][i];
      site->mv = search_site_mvs[i];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    cfg->searches_per_step[stage_index] = num_search_pts;
    cfg->radius[stage_index] = radius;
    --stage_index;
    ++num_search_steps;
  }
  cfg->num_search_steps = num_search_steps;
}

void av1_init_motion_fpf(search_site_config *cfg, int stride) {
  int num_search_steps = 0;
  int stage_index = MAX_MVSEARCH_STEPS - 1;

  cfg->site[stage_index][0].mv.col = cfg->site[stage_index][0].mv.row = 0;
  cfg->site[stage_index][0].offset = 0;
  cfg->stride = stride;

  for (int radius = MAX_FIRST_STEP; radius > 0; radius /= 2) {
    // Generate offsets for 8 search sites per step.
    int tan_radius = AOMMAX((int)(0.41 * radius), 1);
    int num_search_pts = 12;
    if (radius == 1) num_search_pts = 8;

    const FULLPEL_MV search_site_mvs[13] = {
      { 0, 0 },
      { -radius, 0 },
      { radius, 0 },
      { 0, -radius },
      { 0, radius },
      { -radius, -tan_radius },
      { radius, tan_radius },
      { -tan_radius, radius },
      { tan_radius, -radius },
      { -radius, tan_radius },
      { radius, -tan_radius },
      { tan_radius, radius },
      { -tan_radius, -radius },
    };

    int i;
    for (i = 0; i <= num_search_pts; ++i) {
      search_site *const site = &cfg->site[stage_index][i];
      site->mv = search_site_mvs[i];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    cfg->searches_per_step[stage_index] = num_search_pts;
    cfg->radius[stage_index] = radius;
    --stage_index;
    ++num_search_steps;
  }
  cfg->num_search_steps = num_search_steps;
}

// Search site initialization for NSTEP search method.
void av1_init_motion_compensation_nstep(search_site_config *cfg, int stride) {
  int num_search_steps = 0;
  int stage_index = 0;
  cfg->stride = stride;
  int radius = 1;
#if CONFIG_MV_SEARCH_RANGE
  for (stage_index = 0; stage_index < 16; ++stage_index) {
#else
  for (stage_index = 0; stage_index < 15; ++stage_index) {
#endif  // CONFIG_MV_SEARCH_RANGE
    int tan_radius = AOMMAX((int)(0.41 * radius), 1);
    int num_search_pts = 12;
    if (radius <= 5) {
      tan_radius = radius;
      num_search_pts = 8;
    }
    const FULLPEL_MV search_site_mvs[13] = {
      { 0, 0 },
      { -radius, 0 },
      { radius, 0 },
      { 0, -radius },
      { 0, radius },
      { -radius, -tan_radius },
      { radius, tan_radius },
      { -tan_radius, radius },
      { tan_radius, -radius },
      { -radius, tan_radius },
      { radius, -tan_radius },
      { tan_radius, radius },
      { -tan_radius, -radius },
    };

    for (int i = 0; i <= num_search_pts; ++i) {
      search_site *const site = &cfg->site[stage_index][i];
      site->mv = search_site_mvs[i];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    cfg->searches_per_step[stage_index] = num_search_pts;
    cfg->radius[stage_index] = radius;
    ++num_search_steps;
#if !CONFIG_MV_SEARCH_RANGE
    if (stage_index < 12)
#endif  // CONFIG_MV_SEARCH_RANGE
      radius = (int)AOMMAX((radius * 1.5 + 0.5), radius + 1);
  }
  cfg->num_search_steps = num_search_steps;
}

// Search site initialization for BIGDIA / FAST_BIGDIA / FAST_DIAMOND
// search methods.
void av1_init_motion_compensation_bigdia(search_site_config *cfg, int stride) {
  cfg->stride = stride;
  // First scale has 4-closest points, the rest have 8 points in diamond
  // shape at increasing scales
  static const int bigdia_num_candidates[MAX_PATTERN_SCALES] = {
    4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  };

  // BIGDIA search method candidates.
  // Note that the largest candidate step at each scale is 2^scale
  /* clang-format off */
  static const FULLPEL_MV
      site_candidates[MAX_PATTERN_SCALES][MAX_PATTERN_CANDIDATES] = {
          { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, 0 }, { 0, 0 },
            { 0, 0 }, { 0, 0 } },
          { { -1, -1 }, { 0, -2 }, { 1, -1 }, { 2, 0 }, { 1, 1 }, { 0, 2 },
            { -1, 1 }, { -2, 0 } },
          { { -2, -2 }, { 0, -4 }, { 2, -2 }, { 4, 0 }, { 2, 2 }, { 0, 4 },
            { -2, 2 }, { -4, 0 } },
          { { -4, -4 }, { 0, -8 }, { 4, -4 }, { 8, 0 }, { 4, 4 }, { 0, 8 },
            { -4, 4 }, { -8, 0 } },
          { { -8, -8 }, { 0, -16 }, { 8, -8 }, { 16, 0 }, { 8, 8 }, { 0, 16 },
            { -8, 8 }, { -16, 0 } },
          { { -16, -16 }, { 0, -32 }, { 16, -16 }, { 32, 0 }, { 16, 16 },
            { 0, 32 }, { -16, 16 }, { -32, 0 } },
          { { -32, -32 }, { 0, -64 }, { 32, -32 }, { 64, 0 }, { 32, 32 },
            { 0, 64 }, { -32, 32 }, { -64, 0 } },
          { { -64, -64 }, { 0, -128 }, { 64, -64 }, { 128, 0 }, { 64, 64 },
            { 0, 128 }, { -64, 64 }, { -128, 0 } },
          { { -128, -128 }, { 0, -256 }, { 128, -128 }, { 256, 0 },
            { 128, 128 }, { 0, 256 }, { -128, 128 }, { -256, 0 } },
          { { -256, -256 }, { 0, -512 }, { 256, -256 }, { 512, 0 },
            { 256, 256 }, { 0, 512 }, { -256, 256 }, { -512, 0 } },
          { { -512, -512 }, { 0, -1024 }, { 512, -512 }, { 1024, 0 },
            { 512, 512 }, { 0, 1024 }, { -512, 512 }, { -1024, 0 } },
        };

  /* clang-format on */
  int radius = 1;
  for (int i = 0; i < MAX_PATTERN_SCALES; ++i) {
    cfg->searches_per_step[i] = bigdia_num_candidates[i];
    cfg->radius[i] = radius;
    for (int j = 0; j < MAX_PATTERN_CANDIDATES; ++j) {
      search_site *const site = &cfg->site[i][j];
      site->mv = site_candidates[i][j];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    radius *= 2;
  }
  cfg->num_search_steps = MAX_PATTERN_SCALES;
}

// Search site initialization for SQUARE search method.
void av1_init_motion_compensation_square(search_site_config *cfg, int stride) {
  cfg->stride = stride;
  // All scales have 8 closest points in square shape.
  static const int square_num_candidates[MAX_PATTERN_SCALES] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  };

  // Square search method candidates.
  // Note that the largest candidate step at each scale is 2^scale.
  /* clang-format off */
    static const FULLPEL_MV
        square_candidates[MAX_PATTERN_SCALES][MAX_PATTERN_CANDIDATES] = {
             { { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 },
               { -1, 1 }, { -1, 0 } },
             { { -2, -2 }, { 0, -2 }, { 2, -2 }, { 2, 0 }, { 2, 2 }, { 0, 2 },
               { -2, 2 }, { -2, 0 } },
             { { -4, -4 }, { 0, -4 }, { 4, -4 }, { 4, 0 }, { 4, 4 }, { 0, 4 },
               { -4, 4 }, { -4, 0 } },
             { { -8, -8 }, { 0, -8 }, { 8, -8 }, { 8, 0 }, { 8, 8 }, { 0, 8 },
               { -8, 8 }, { -8, 0 } },
             { { -16, -16 }, { 0, -16 }, { 16, -16 }, { 16, 0 }, { 16, 16 },
               { 0, 16 }, { -16, 16 }, { -16, 0 } },
             { { -32, -32 }, { 0, -32 }, { 32, -32 }, { 32, 0 }, { 32, 32 },
               { 0, 32 }, { -32, 32 }, { -32, 0 } },
             { { -64, -64 }, { 0, -64 }, { 64, -64 }, { 64, 0 }, { 64, 64 },
               { 0, 64 }, { -64, 64 }, { -64, 0 } },
             { { -128, -128 }, { 0, -128 }, { 128, -128 }, { 128, 0 },
               { 128, 128 }, { 0, 128 }, { -128, 128 }, { -128, 0 } },
             { { -256, -256 }, { 0, -256 }, { 256, -256 }, { 256, 0 },
               { 256, 256 }, { 0, 256 }, { -256, 256 }, { -256, 0 } },
             { { -512, -512 }, { 0, -512 }, { 512, -512 }, { 512, 0 },
               { 512, 512 }, { 0, 512 }, { -512, 512 }, { -512, 0 } },
             { { -1024, -1024 }, { 0, -1024 }, { 1024, -1024 }, { 1024, 0 },
               { 1024, 1024 }, { 0, 1024 }, { -1024, 1024 }, { -1024, 0 } },
    };

  /* clang-format on */
  int radius = 1;
  for (int i = 0; i < MAX_PATTERN_SCALES; ++i) {
    cfg->searches_per_step[i] = square_num_candidates[i];
    cfg->radius[i] = radius;
    for (int j = 0; j < MAX_PATTERN_CANDIDATES; ++j) {
      search_site *const site = &cfg->site[i][j];
      site->mv = square_candidates[i][j];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    radius *= 2;
  }
  cfg->num_search_steps = MAX_PATTERN_SCALES;
}

// Search site initialization for HEX / FAST_HEX search methods.
void av1_init_motion_compensation_hex(search_site_config *cfg, int stride) {
  cfg->stride = stride;
  // First scale has 8-closest points, the rest have 6 points in hex shape
  // at increasing scales.
  static const int hex_num_candidates[MAX_PATTERN_SCALES] = { 8, 6, 6, 6, 6, 6,
                                                              6, 6, 6, 6, 6 };
  // Note that the largest candidate step at each scale is 2^scale.
  /* clang-format off */
    static const FULLPEL_MV
        hex_candidates[MAX_PATTERN_SCALES][MAX_PATTERN_CANDIDATES] = {
        { { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 },
          { -1, 1 }, { -1, 0 } },
        { { -1, -2 }, { 1, -2 }, { 2, 0 }, { 1, 2 }, { -1, 2 }, { -2, 0 } },
        { { -2, -4 }, { 2, -4 }, { 4, 0 }, { 2, 4 }, { -2, 4 }, { -4, 0 } },
        { { -4, -8 }, { 4, -8 }, { 8, 0 }, { 4, 8 }, { -4, 8 }, { -8, 0 } },
        { { -8, -16 }, { 8, -16 }, { 16, 0 }, { 8, 16 },
          { -8, 16 }, { -16, 0 } },
        { { -16, -32 }, { 16, -32 }, { 32, 0 }, { 16, 32 }, { -16, 32 },
          { -32, 0 } },
        { { -32, -64 }, { 32, -64 }, { 64, 0 }, { 32, 64 }, { -32, 64 },
          { -64, 0 } },
        { { -64, -128 }, { 64, -128 }, { 128, 0 }, { 64, 128 },
          { -64, 128 }, { -128, 0 } },
        { { -128, -256 }, { 128, -256 }, { 256, 0 }, { 128, 256 },
          { -128, 256 }, { -256, 0 } },
        { { -256, -512 }, { 256, -512 }, { 512, 0 }, { 256, 512 },
          { -256, 512 }, { -512, 0 } },
        { { -512, -1024 }, { 512, -1024 }, { 1024, 0 }, { 512, 1024 },
          { -512, 1024 }, { -1024, 0 } },
    };

  /* clang-format on */
  int radius = 1;
  for (int i = 0; i < MAX_PATTERN_SCALES; ++i) {
    cfg->searches_per_step[i] = hex_num_candidates[i];
    cfg->radius[i] = radius;
    for (int j = 0; j < hex_num_candidates[i]; ++j) {
      search_site *const site = &cfg->site[i][j];
      site->mv = hex_candidates[i][j];
      site->offset = get_offset_from_fullmv(&site->mv, stride);
    }
    radius *= 2;
  }
  cfg->num_search_steps = MAX_PATTERN_SCALES;
}

// Checks whether the mv is within range of the mv_limits
static INLINE int check_bounds(const FullMvLimits *mv_limits, int row, int col,
                               int range) {
  return ((row - range) >= mv_limits->row_min) &
         ((row + range) <= mv_limits->row_max) &
         ((col - range) >= mv_limits->col_min) &
         ((col + range) <= mv_limits->col_max);
}

#if CONFIG_IBC_BV_IMPROVEMENT
int av1_get_mv_err_cost(const MV *mv, const MV_COST_PARAMS *mv_cost_params) {
  return mv_err_cost(*mv, mv_cost_params);
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT

static INLINE int get_mvpred_var_cost(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV *this_mv) {
  const aom_variance_fn_ptr_t *vfp = ms_params->vfp;
#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV sub_this_mv = get_mv_from_fullmv(this_mv);
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const uint16_t *src_buf = src->buf;
  const int src_stride = src->stride;
  const int ref_stride = ref->stride;

  unsigned unused;
  int bestsme;

  bestsme = vfp->vf(src_buf, src_stride, get_buf_from_fullmv(ref, this_mv),
                    ref_stride, &unused);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*ms_params->mv_cost_params.ref_mv, &sub_mv_offset,
                    ms_params->mv_cost_params.pb_mv_precision);
  if (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    sub_this_mv.col += sub_mv_offset.col;
    sub_this_mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  bestsme += mv_err_cost(sub_this_mv, &ms_params->mv_cost_params);

  return bestsme;
}

static INLINE int get_mvpred_sad(const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                                 const struct buf_2d *const src,
                                 const uint16_t *const ref_address,
                                 const int ref_stride) {
  const uint16_t *src_buf = src->buf;
  const int src_stride = src->stride;

  return ms_params->sdf(src_buf, src_stride, ref_address, ref_stride);
}

static INLINE int get_mvpred_compound_var_cost(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV *this_mv) {
  const aom_variance_fn_ptr_t *vfp = ms_params->vfp;
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const uint16_t *src_buf = src->buf;
  const int src_stride = src->stride;
  const int ref_stride = ref->stride;

  const uint8_t *mask = ms_params->ms_buffers.mask;
  const uint16_t *second_pred = ms_params->ms_buffers.second_pred;
  const int mask_stride = ms_params->ms_buffers.mask_stride;
  const int invert_mask = ms_params->ms_buffers.inv_mask;
  unsigned unused;
  int bestsme;

  if (mask) {
    bestsme = vfp->msvf(get_buf_from_fullmv(ref, this_mv), ref_stride, 0, 0,
                        src_buf, src_stride, second_pred, mask, mask_stride,
                        invert_mask, &unused);
  } else if (second_pred) {
    bestsme = vfp->svaf(get_buf_from_fullmv(ref, this_mv), ref_stride, 0, 0,
                        src_buf, src_stride, &unused, second_pred);
  } else {
    bestsme = vfp->vf(src_buf, src_stride, get_buf_from_fullmv(ref, this_mv),
                      ref_stride, &unused);
  }

#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV sub_this_mv = get_mv_from_fullmv(this_mv);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*ms_params->mv_cost_params.ref_mv, &sub_mv_offset,
                    ms_params->mv_cost_params.pb_mv_precision);
  if (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    sub_this_mv.col += sub_mv_offset.col;
    sub_this_mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  bestsme += mv_err_cost(sub_this_mv, &ms_params->mv_cost_params);

  return bestsme;
}

// Set weighting factor for two reference frames
static INLINE void set_cmp_weight(const MB_MODE_INFO *mi, int invert_mask,
                                  DIST_WTD_COMP_PARAMS *jcp_param) {
  int weight = get_cwp_idx(mi);
  weight = invert_mask ? (1 << CWP_WEIGHT_BITS) - weight : weight;
  jcp_param->fwd_offset = weight;
  jcp_param->bck_offset = (1 << CWP_WEIGHT_BITS) - weight;
}

static INLINE int get_mvpred_compound_sad(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    const struct buf_2d *const src, const uint16_t *const ref_address,
    const int ref_stride) {
  const aom_variance_fn_ptr_t *vfp = ms_params->vfp;
  const uint16_t *src_buf = src->buf;
  const int src_stride = src->stride;

  const uint8_t *mask = ms_params->ms_buffers.mask;
  const uint16_t *second_pred = ms_params->ms_buffers.second_pred;
  const int mask_stride = ms_params->ms_buffers.mask_stride;
  const int invert_mask = ms_params->ms_buffers.inv_mask;

  if (mask) {
    return vfp->msdf(src_buf, src_stride, ref_address, ref_stride, second_pred,
                     mask, mask_stride, invert_mask);
  } else if (second_pred) {
    const MB_MODE_INFO *mi = ms_params->xd->mi[0];
    if (get_cwp_idx(mi) != CWP_EQUAL) {
      DIST_WTD_COMP_PARAMS jcp_param;
      set_cmp_weight(mi, invert_mask, &jcp_param);

      return vfp->jsdaf(src_buf, src_stride, ref_address, ref_stride,
                        second_pred, &jcp_param);
    }
    return vfp->sdaf(src_buf, src_stride, ref_address, ref_stride, second_pred);
  } else {
    return ms_params->sdf(src_buf, src_stride, ref_address, ref_stride);
  }
}

// Calculates and returns a sad+mvcost list around an integer best pel during
// fullpixel motion search. The resulting list can be used to speed up subpel
// motion search later.
#define USE_SAD_COSTLIST 1

// calc_int_cost_list uses var to populate the costlist, which is more accurate
// than sad but slightly slower.
static AOM_FORCE_INLINE void calc_int_cost_list(
    const FULLPEL_MV best_mv, const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    int *cost_list) {
  static const FULLPEL_MV neighbors[4] = {
    { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }
  };
  const int br = best_mv.row;
  const int bc = best_mv.col;
  // costlist is not supported for the 2/4 MV precision
  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  cost_list[0] = get_mvpred_var_cost(ms_params, &best_mv);

  if (check_bounds(&ms_params->mv_limits, br, bc, 1)) {
    for (int i = 0; i < 4; i++) {
      const FULLPEL_MV neighbor_mv = { br + neighbors[i].row,
                                       bc + neighbors[i].col };
      cost_list[i + 1] = get_mvpred_var_cost(ms_params, &neighbor_mv);
    }
  } else {
    for (int i = 0; i < 4; i++) {
      const FULLPEL_MV neighbor_mv = { br + neighbors[i].row,
                                       bc + neighbors[i].col };
      if (!av1_is_fullmv_in_range(&ms_params->mv_limits, neighbor_mv,
                                  ms_params->mv_cost_params.pb_mv_precision

                                  )) {
        cost_list[i + 1] = INT_MAX;
      } else {
        cost_list[i + 1] = get_mvpred_var_cost(ms_params, &neighbor_mv);
      }
    }
  }
}

// calc_int_sad_list uses sad to populate the costlist, which is less accurate
// than var but faster.
static AOM_FORCE_INLINE void calc_int_sad_list(
    const FULLPEL_MV best_mv, const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    int *cost_list, int costlist_has_sad) {
  static const FULLPEL_MV neighbors[4] = {
    { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }
  };
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const int ref_stride = ref->stride;
  const int br = best_mv.row;
  const int bc = best_mv.col;
  assert(av1_is_fullmv_in_range(&ms_params->mv_limits, best_mv,
                                ms_params->mv_cost_params.pb_mv_precision));

  // costlist is not supported for the 2/4 MV precision
  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  // Refresh the costlist it does not contain valid sad
  if (!costlist_has_sad) {
#if CONFIG_IBC_SR_EXT
    if (ms_params->is_intra_mode &&
        ms_params->cm->features.allow_local_intrabc) {
      MV sub_mv = { (int16_t)GET_MV_SUBPEL(best_mv.row),
                    (int16_t)GET_MV_SUBPEL(best_mv.col) };
      int flag = av1_is_dv_valid(sub_mv, ms_params->cm, ms_params->xd,
                                 ms_params->mi_row, ms_params->mi_col,
                                 ms_params->bsize, ms_params->mib_size_log2);
      if (flag) {
        cost_list[0] = get_mvpred_sad(
            ms_params, src, get_buf_from_fullmv(ref, &best_mv), ref_stride);
      } else {
        cost_list[0] = INT_MAX;
      }
    } else {
      cost_list[0] = get_mvpred_sad(
          ms_params, src, get_buf_from_fullmv(ref, &best_mv), ref_stride);
    }
#else
    cost_list[0] = get_mvpred_sad(
        ms_params, src, get_buf_from_fullmv(ref, &best_mv), ref_stride);
#endif  // CONFIG_IBC_SR_EXT

    if (check_bounds(&ms_params->mv_limits, br, bc, 1)) {
      for (int i = 0; i < 4; i++) {
        const FULLPEL_MV this_mv = { br + neighbors[i].row,
                                     bc + neighbors[i].col };
#if CONFIG_IBC_SR_EXT
        if (ms_params->is_intra_mode &&
            ms_params->cm->features.allow_local_intrabc) {
          MV sub_mv = { (int16_t)GET_MV_SUBPEL(this_mv.row),
                        (int16_t)GET_MV_SUBPEL(this_mv.col) };
          int flag = av1_is_dv_valid(
              sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
              ms_params->mi_col, ms_params->bsize, ms_params->mib_size_log2);
          if (flag) {
            cost_list[i + 1] = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
          } else {
            cost_list[i + 1] = INT_MAX;
          }
        } else {
          cost_list[i + 1] = get_mvpred_sad(
              ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
        }
#else
        cost_list[i + 1] = get_mvpred_sad(
            ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
#endif  // CONFIG_IBC_SR_EXT
      }
    } else {
      for (int i = 0; i < 4; i++) {
        const FULLPEL_MV this_mv = { br + neighbors[i].row,
                                     bc + neighbors[i].col };
        if (!av1_is_fullmv_in_range(
                &ms_params->mv_limits, this_mv,
                ms_params->mv_cost_params.pb_mv_precision)) {
          cost_list[i + 1] = INT_MAX;
        } else {
#if CONFIG_IBC_SR_EXT
          if (ms_params->is_intra_mode &&
              ms_params->cm->features.allow_local_intrabc) {
            MV sub_mv = { (int16_t)GET_MV_SUBPEL(this_mv.row),
                          (int16_t)GET_MV_SUBPEL(this_mv.col) };
            int flag = av1_is_dv_valid(
                sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
                ms_params->mi_col, ms_params->bsize, ms_params->mib_size_log2);
            if (flag) {
              cost_list[i + 1] = get_mvpred_sad(
                  ms_params, src, get_buf_from_fullmv(ref, &this_mv),
                  ref_stride);
            } else {
              cost_list[i + 1] = INT_MAX;
            }
          } else {
            cost_list[i + 1] = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
          }
#else
          cost_list[i + 1] = get_mvpred_sad(
              ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
#endif  // CONFIG_IBC_SR_EXT
        }
      }
    }
  }

  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  cost_list[0] += mvsad_err_cost(best_mv, mv_cost_params);

  for (int idx = 0; idx < 4; idx++) {
    if (cost_list[idx + 1] != INT_MAX) {
      const FULLPEL_MV this_mv = { br + neighbors[idx].row,
                                   bc + neighbors[idx].col };
      cost_list[idx + 1] += mvsad_err_cost(this_mv, mv_cost_params);
    }
  }
}

// Computes motion vector cost and adds to the sad cost.
// Then updates the best sad and motion vectors.
// Inputs:
//   this_sad: the sad to be evaluated.
//   mv: the current motion vector.
//   mv_cost_params: a structure containing information to compute mv cost.
//   best_sad: the current best sad.
//   raw_best_sad (optional): the current best sad without calculating mv cost.
//   best_mv: the current best motion vector.
//   second_best_mv (optional): the second best motion vector up to now.
// Modifies:
//   best_sad, raw_best_sad, best_mv, second_best_mv
//   If the current sad is lower than the current best sad.
// Returns:
//   Whether the input sad (mv) is better than the current best.
static int update_mvs_and_sad(const unsigned int this_sad, const FULLPEL_MV *mv,
                              const MV_COST_PARAMS *mv_cost_params,
                              unsigned int *best_sad,
                              unsigned int *raw_best_sad, FULLPEL_MV *best_mv,
                              FULLPEL_MV *second_best_mv) {
  if (this_sad >= *best_sad) return 0;

  // Add the motion vector cost.
  const unsigned int sad = this_sad + mvsad_err_cost(*mv, mv_cost_params);
  if (sad < *best_sad) {
    if (raw_best_sad) *raw_best_sad = this_sad;
    *best_sad = sad;
    if (second_best_mv) *second_best_mv = *best_mv;
    *best_mv = *mv;
    return 1;
  }
  return 0;
}

// Calculate sad4 and update the bestmv information
// in FAST_DIAMOND search method.
static void calc_sad4_update_bestmv(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    const MV_COST_PARAMS *mv_cost_params, FULLPEL_MV *best_mv,
    FULLPEL_MV *temp_best_mv, unsigned int *bestsad, unsigned int *raw_bestsad,
    int search_step, int *best_site, int cand_start) {
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const search_site *site = ms_params->search_sites->site[search_step];

  uint16_t const *block_offset[4];
  unsigned int sads[4];
  const uint16_t *best_address;
  const uint16_t *src_buf = src->buf;
  const int src_stride = src->stride;
  best_address = get_buf_from_fullmv(ref, temp_best_mv);
  // Loop over number of candidates.
  for (int j = 0; j < 4; j++)
    block_offset[j] = site[cand_start + j].offset + best_address;

  // 4-point sad calculation.
  ms_params->sdx4df(src_buf, src_stride, block_offset, ref->stride, sads);
  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  for (int j = 0; j < 4; j++) {
    const FULLPEL_MV this_mv = {
      temp_best_mv->row + site[cand_start + j].mv.row,
      temp_best_mv->col + site[cand_start + j].mv.col
    };
    const int found_better_mv = update_mvs_and_sad(
        sads[j], &this_mv, mv_cost_params, bestsad, raw_bestsad, best_mv,
        /*second_best_mv=*/NULL);
    if (found_better_mv) *best_site = cand_start + j;
  }
}

// Calculate sad and update the bestmv information
// in FAST_DIAMOND search method.
static void calc_sad_update_bestmv(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    const MV_COST_PARAMS *mv_cost_params, FULLPEL_MV *best_mv,
    FULLPEL_MV *temp_best_mv, unsigned int *bestsad, unsigned int *raw_bestsad,
    int search_step, int *best_site, const int num_candidates, int cand_start) {
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const search_site *site = ms_params->search_sites->site[search_step];

  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  // Loop over number of candidates.
  for (int i = cand_start; i < num_candidates; i++) {
    const FULLPEL_MV this_mv = { temp_best_mv->row + site[i].mv.row,
                                 temp_best_mv->col + site[i].mv.col };
    if (!av1_is_fullmv_in_range(&ms_params->mv_limits, this_mv,
                                ms_params->mv_cost_params.pb_mv_precision))
      continue;
    int thissad = get_mvpred_sad(
        ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref->stride);
    const int found_better_mv = update_mvs_and_sad(
        thissad, &this_mv, mv_cost_params, bestsad, raw_bestsad, best_mv,
        /*second_best_mv=*/NULL);
    if (found_better_mv) *best_site = i;
  }
}

// Generic pattern search function that searches over multiple scales.
// Each scale can have a different number of candidates and shape of
// candidates as indicated in the num_candidates and candidates arrays
// passed into this function
static int pattern_search(FULLPEL_MV start_mv,
                          const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                          int search_step, const int do_init_search,
                          int *cost_list, FULLPEL_MV *best_mv) {
  static const int search_steps[MAX_MVSEARCH_STEPS] = {
    10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
  };
  int i, s, t;

  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const search_site_config *search_sites = ms_params->search_sites;
  const int *num_candidates = search_sites->searches_per_step;
  const int ref_stride = ref->stride;
  const int last_is_4 = num_candidates[0] == 4;
  int br, bc;
  unsigned int bestsad = UINT_MAX, raw_bestsad = UINT_MAX;
  int thissad;
  int k = -1;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  search_step = AOMMIN(search_step, MAX_MVSEARCH_STEPS - 1);
  assert(search_step >= 0);
  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  int best_init_s = search_steps[search_step];
  // adjust ref_mv to make sure it is within MV range
  clamp_fullmv(&start_mv, &ms_params->mv_limits);
  br = start_mv.row;
  bc = start_mv.col;
  if (cost_list != NULL) {
    cost_list[0] = cost_list[1] = cost_list[2] = cost_list[3] = cost_list[4] =
        INT_MAX;
  }
  int costlist_has_sad = 0;

  // Work out the start point for the search
  raw_bestsad = get_mvpred_sad(ms_params, src,
                               get_buf_from_fullmv(ref, &start_mv), ref_stride);
  bestsad = raw_bestsad + mvsad_err_cost(start_mv, mv_cost_params);

  // Search all possible scales up to the search param around the center point
  // pick the scale of the point that is best as the starting scale of
  // further steps around it.
  if (do_init_search) {
    s = best_init_s;
    best_init_s = -1;
    for (t = 0; t <= s; ++t) {
      int best_site = -1;
      FULLPEL_MV temp_best_mv;
      temp_best_mv.row = br;
      temp_best_mv.col = bc;
      if (check_bounds(&ms_params->mv_limits, br, bc, 1 << t)) {
        // Call 4-point sad for multiples of 4 candidates.
        const int no_of_4_cand_loops = num_candidates[t] >> 2;
        for (i = 0; i < no_of_4_cand_loops; i++) {
          calc_sad4_update_bestmv(ms_params, mv_cost_params, best_mv,
                                  &temp_best_mv, &bestsad, &raw_bestsad, t,
                                  &best_site, i * 4);
        }
        // Rest of the candidates
        const int remaining_cand = num_candidates[t] % 4;
        calc_sad_update_bestmv(ms_params, mv_cost_params, best_mv,
                               &temp_best_mv, &bestsad, &raw_bestsad, t,
                               &best_site, remaining_cand,
                               no_of_4_cand_loops * 4);
      } else {
        calc_sad_update_bestmv(ms_params, mv_cost_params, best_mv,
                               &temp_best_mv, &bestsad, &raw_bestsad, t,
                               &best_site, num_candidates[t], 0);
      }
      if (best_site == -1) {
        continue;
      } else {
        best_init_s = t;
        k = best_site;
      }
    }
    if (best_init_s != -1) {
      br += search_sites->site[best_init_s][k].mv.row;
      bc += search_sites->site[best_init_s][k].mv.col;
    }
  }

  // If the center point is still the best, just skip this and move to
  // the refinement step.
  if (best_init_s != -1) {
    const int last_s = (last_is_4 && cost_list != NULL);
    int best_site = -1;
    s = best_init_s;

    for (; s >= last_s; s--) {
      // No need to search all points the 1st time if initial search was used
      if (!do_init_search || s != best_init_s) {
        FULLPEL_MV temp_best_mv;
        temp_best_mv.row = br;
        temp_best_mv.col = bc;
        if (check_bounds(&ms_params->mv_limits, br, bc, 1 << s)) {
          // Call 4-point sad for multiples of 4 candidates.
          const int no_of_4_cand_loops = num_candidates[s] >> 2;
          for (i = 0; i < no_of_4_cand_loops; i++) {
            calc_sad4_update_bestmv(ms_params, mv_cost_params, best_mv,
                                    &temp_best_mv, &bestsad, &raw_bestsad, s,
                                    &best_site, i * 4);
          }
          // Rest of the candidates
          const int remaining_cand = num_candidates[s] % 4;
          calc_sad_update_bestmv(ms_params, mv_cost_params, best_mv,
                                 &temp_best_mv, &bestsad, &raw_bestsad, s,
                                 &best_site, remaining_cand,
                                 no_of_4_cand_loops * 4);
        } else {
          calc_sad_update_bestmv(ms_params, mv_cost_params, best_mv,
                                 &temp_best_mv, &bestsad, &raw_bestsad, s,
                                 &best_site, num_candidates[s], 0);
        }

        if (best_site == -1) {
          continue;
        } else {
          br += search_sites->site[s][best_site].mv.row;
          bc += search_sites->site[s][best_site].mv.col;
          k = best_site;
        }
      }

      do {
        int next_chkpts_indices[PATTERN_CANDIDATES_REF];
        best_site = -1;
        next_chkpts_indices[0] = (k == 0) ? num_candidates[s] - 1 : k - 1;
        next_chkpts_indices[1] = k;
        next_chkpts_indices[2] = (k == num_candidates[s] - 1) ? 0 : k + 1;

        if (check_bounds(&ms_params->mv_limits, br, bc, 1 << s)) {
          for (i = 0; i < PATTERN_CANDIDATES_REF; i++) {
            const FULLPEL_MV this_mv = {
              br + search_sites->site[s][next_chkpts_indices[i]].mv.row,
              bc + search_sites->site[s][next_chkpts_indices[i]].mv.col
            };
            thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        } else {
          for (i = 0; i < PATTERN_CANDIDATES_REF; i++) {
            const FULLPEL_MV this_mv = {
              br + search_sites->site[s][next_chkpts_indices[i]].mv.row,
              bc + search_sites->site[s][next_chkpts_indices[i]].mv.col
            };
            if (!av1_is_fullmv_in_range(
                    &ms_params->mv_limits, this_mv,
                    ms_params->mv_cost_params.pb_mv_precision))
              continue;
            thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        }

        if (best_site != -1) {
          k = next_chkpts_indices[best_site];
          br += search_sites->site[s][k].mv.row;
          bc += search_sites->site[s][k].mv.col;
        }
      } while (best_site != -1);
    }

    // Note: If we enter the if below, then cost_list must be non-NULL.
    if (s == 0) {
      cost_list[0] = raw_bestsad;
      costlist_has_sad = 1;
      if (!do_init_search || s != best_init_s) {
        if (check_bounds(&ms_params->mv_limits, br, bc, 1 << s)) {
          for (i = 0; i < num_candidates[s]; i++) {
            const FULLPEL_MV this_mv = { br + search_sites->site[s][i].mv.row,
                                         bc + search_sites->site[s][i].mv.col };
            cost_list[i + 1] = thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        } else {
          for (i = 0; i < num_candidates[s]; i++) {
            const FULLPEL_MV this_mv = { br + search_sites->site[s][i].mv.row,
                                         bc + search_sites->site[s][i].mv.col };
            if (!av1_is_fullmv_in_range(
                    &ms_params->mv_limits, this_mv,
                    ms_params->mv_cost_params.pb_mv_precision))
              continue;
            cost_list[i + 1] = thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        }

        if (best_site != -1) {
          br += search_sites->site[s][best_site].mv.row;
          bc += search_sites->site[s][best_site].mv.col;
          k = best_site;
        }
      }
      while (best_site != -1) {
        int next_chkpts_indices[PATTERN_CANDIDATES_REF];
        best_site = -1;
        next_chkpts_indices[0] = (k == 0) ? num_candidates[s] - 1 : k - 1;
        next_chkpts_indices[1] = k;
        next_chkpts_indices[2] = (k == num_candidates[s] - 1) ? 0 : k + 1;
        cost_list[1] = cost_list[2] = cost_list[3] = cost_list[4] = INT_MAX;
        cost_list[((k + 2) % 4) + 1] = cost_list[0];
        cost_list[0] = raw_bestsad;

        if (check_bounds(&ms_params->mv_limits, br, bc, 1 << s)) {
          for (i = 0; i < PATTERN_CANDIDATES_REF; i++) {
            const FULLPEL_MV this_mv = {
              br + search_sites->site[s][next_chkpts_indices[i]].mv.row,
              bc + search_sites->site[s][next_chkpts_indices[i]].mv.col
            };
            cost_list[next_chkpts_indices[i] + 1] = thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        } else {
          for (i = 0; i < PATTERN_CANDIDATES_REF; i++) {
            const FULLPEL_MV this_mv = {
              br + search_sites->site[s][next_chkpts_indices[i]].mv.row,
              bc + search_sites->site[s][next_chkpts_indices[i]].mv.col
            };
            if (!av1_is_fullmv_in_range(
                    &ms_params->mv_limits, this_mv,
                    ms_params->mv_cost_params.pb_mv_precision)) {
              cost_list[next_chkpts_indices[i] + 1] = INT_MAX;
              continue;
            }
            cost_list[next_chkpts_indices[i] + 1] = thissad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &this_mv), ref_stride);
            const int found_better_mv =
                update_mvs_and_sad(thissad, &this_mv, mv_cost_params, &bestsad,
                                   &raw_bestsad, best_mv,
                                   /*second_best_mv=*/NULL);
            if (found_better_mv) best_site = i;
          }
        }

        if (best_site != -1) {
          k = next_chkpts_indices[best_site];
          br += search_sites->site[s][k].mv.row;
          bc += search_sites->site[s][k].mv.col;
        }
      }
    }
  }

  best_mv->row = br;
  best_mv->col = bc;

  // Returns the one-away integer pel cost/sad around the best as follows:
  // cost_list[0]: cost/sad at the best integer pel
  // cost_list[1]: cost/sad at delta {0, -1} (left)   from the best integer pel
  // cost_list[2]: cost/sad at delta { 1, 0} (bottom) from the best integer pel
  // cost_list[3]: cost/sad at delta { 0, 1} (right)  from the best integer pel
  // cost_list[4]: cost/sad at delta {-1, 0} (top)    from the best integer pel
  if (cost_list) {
    if (USE_SAD_COSTLIST) {
      calc_int_sad_list(*best_mv, ms_params, cost_list, costlist_has_sad);
    } else {
      calc_int_cost_list(*best_mv, ms_params, cost_list);
    }
  }
  best_mv->row = br;
  best_mv->col = bc;

  const int var_cost = get_mvpred_var_cost(ms_params, best_mv);
  return var_cost;
}

// For the following foo_search, the input arguments are:
// start_mv: where we are starting our motion search
// ms_params: a collection of motion search parameters
// search_step: how many steps to skip in our motion search. For example,
//   a value 3 suggests that 3 search steps have already taken place prior to
//   this function call, so we jump directly to step 4 of the search process
// do_init_search: if on, do an initial search of all possible scales around the
//   start_mv, and then pick the best scale.
// cond_list: used to hold the cost around the best full mv so we can use it to
//   speed up subpel search later.
// best_mv: the best mv found in the motion search
static int hex_search(const FULLPEL_MV start_mv,
                      const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                      const int search_step, const int do_init_search,
                      int *cost_list, FULLPEL_MV *best_mv) {
  return pattern_search(start_mv, ms_params, search_step, do_init_search,
                        cost_list, best_mv);
}

static int bigdia_search(const FULLPEL_MV start_mv,
                         const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                         const int search_step, const int do_init_search,
                         int *cost_list, FULLPEL_MV *best_mv) {
  return pattern_search(start_mv, ms_params, search_step, do_init_search,
                        cost_list, best_mv);
}

static int square_search(const FULLPEL_MV start_mv,
                         const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                         const int search_step, const int do_init_search,
                         int *cost_list, FULLPEL_MV *best_mv) {
  return pattern_search(start_mv, ms_params, search_step, do_init_search,
                        cost_list, best_mv);
}

static int fast_hex_search(const FULLPEL_MV start_mv,
                           const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                           const int search_step, const int do_init_search,
                           int *cost_list, FULLPEL_MV *best_mv) {
  return hex_search(start_mv, ms_params,
                    AOMMAX(MAX_MVSEARCH_STEPS - 2, search_step), do_init_search,
                    cost_list, best_mv);
}

static int fast_dia_search(const FULLPEL_MV start_mv,
                           const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                           const int search_step, const int do_init_search,
                           int *cost_list, FULLPEL_MV *best_mv) {
  return bigdia_search(start_mv, ms_params,
                       AOMMAX(MAX_MVSEARCH_STEPS - 2, search_step),
                       do_init_search, cost_list, best_mv);
}

static int fast_bigdia_search(const FULLPEL_MV start_mv,
                              const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                              const int search_step, const int do_init_search,
                              int *cost_list, FULLPEL_MV *best_mv) {
  return bigdia_search(start_mv, ms_params,
                       AOMMAX(MAX_MVSEARCH_STEPS - 3, search_step),
                       do_init_search, cost_list, best_mv);
}

static int diamond_search_sad(FULLPEL_MV start_mv,
                              const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                              const int search_step, int *num00,
                              FULLPEL_MV *best_mv, FULLPEL_MV *second_best_mv) {
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;

  const int ref_stride = ref->stride;
  const uint16_t *best_address;

  const uint8_t *mask = ms_params->ms_buffers.mask;
  const uint16_t *second_pred = ms_params->ms_buffers.second_pred;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;

  const search_site_config *cfg = ms_params->search_sites;
  const int prec_shift =
      (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? 0
          : (MV_PRECISION_ONE_PEL - ms_params->mv_cost_params.pb_mv_precision);
  const int prec_multiplier = (1 << prec_shift);

  assert(is_this_mv_precision_compliant(
      get_mv_from_fullmv(&start_mv),
      ms_params->mv_cost_params.pb_mv_precision));

  unsigned int bestsad = INT_MAX;
  int best_site = 0;
  int is_off_center = 0;

  clamp_fullmv(&start_mv, &ms_params->mv_limits);

  // search_step determines the length of the initial step and hence the number
  // of iterations.
  const int tot_steps = cfg->num_search_steps - search_step;

  *num00 = 0;
  *best_mv = start_mv;

  // Check the starting position

#if CONFIG_IBC_SR_EXT
  if (ms_params->is_intra_mode && ms_params->cm->features.allow_local_intrabc) {
    MV sub_mv = { (int16_t)GET_MV_SUBPEL(start_mv.row),
                  (int16_t)GET_MV_SUBPEL(start_mv.col) };
    if (av1_is_dv_valid(sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
                        ms_params->mi_col, ms_params->bsize,
                        ms_params->mib_size_log2)) {
      best_address = get_buf_from_fullmv(ref, &start_mv);
      bestsad =
          get_mvpred_compound_sad(ms_params, src, best_address, ref_stride);
      bestsad += mvsad_err_cost(*best_mv, &ms_params->mv_cost_params);
    } else {
      best_address = get_buf_from_fullmv(ref, &start_mv);
      bestsad = INT_MAX;
    }
  } else {
    best_address = get_buf_from_fullmv(ref, &start_mv);
    bestsad = get_mvpred_compound_sad(ms_params, src, best_address, ref_stride);
    bestsad += mvsad_err_cost(*best_mv, &ms_params->mv_cost_params);
  }
#else
  best_address = get_buf_from_fullmv(ref, &start_mv);
  bestsad = get_mvpred_compound_sad(ms_params, src, best_address, ref_stride);
  bestsad += mvsad_err_cost(*best_mv, &ms_params->mv_cost_params);
#endif  // CONFIG_IBC_SR_EXT

  int next_step_size = tot_steps > 2 ? cfg->radius[tot_steps - 2] : 1;
  for (int step = tot_steps - 1; step >= 0; --step) {
    const search_site *site = cfg->site[step];
    best_site = 0;
    if (step > 0) next_step_size = cfg->radius[step - 1];

    int all_in = 1, j;
    // Trap illegal vectors

    all_in &= best_mv->row + (site[1].mv.row * prec_multiplier) >=
              ms_params->mv_limits.row_min;
    all_in &= best_mv->row + (site[2].mv.row * prec_multiplier) <=
              ms_params->mv_limits.row_max;
    all_in &= best_mv->col + (site[3].mv.col * prec_multiplier) >=
              ms_params->mv_limits.col_min;
    all_in &= best_mv->col + (site[4].mv.col * prec_multiplier) <=
              ms_params->mv_limits.col_max;

#if CONFIG_IBC_SR_EXT
    if (ms_params->is_intra_mode &&
        ms_params->cm->features.allow_local_intrabc) {
      for (j = 0; j < 4; j++) {
        MV sub_mv = { (int16_t)GET_MV_SUBPEL(best_mv->row + site[1 + j].mv.row),
                      (int16_t)GET_MV_SUBPEL(best_mv->col +
                                             site[1 + j].mv.col) };
        all_in &= av1_is_dv_valid(sub_mv, ms_params->cm, ms_params->xd,
                                  ms_params->mi_row, ms_params->mi_col,
                                  ms_params->bsize, ms_params->mib_size_log2);
      }
    }
#endif  // CONFIG_IBC_SR_EXT
    // TODO(anyone): Implement 4 points search for msdf&sdaf
    if (all_in && !mask && !second_pred) {
      const uint16_t *src_buf = src->buf;
      const int src_stride = src->stride;
      for (int idx = 1; idx <= cfg->searches_per_step[step]; idx += 4) {
        uint16_t const *block_offset[4];
        unsigned int sads[4];

#if CONFIG_IBC_SR_EXT
        int valid = 1;
        for (j = 0; j < 4; j++) {
          if (ms_params->is_intra_mode &&
              ms_params->cm->features.allow_local_intrabc) {
            MV sub_mv = {
              (int16_t)GET_MV_SUBPEL(best_mv->row + site[idx + j].mv.row),
              (int16_t)GET_MV_SUBPEL(best_mv->col + site[idx + j].mv.col)
            };
            valid &= av1_is_dv_valid(
                sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
                ms_params->mi_col, ms_params->bsize, ms_params->mib_size_log2);
          }
        }
        if (!valid) continue;
#endif  // CONFIG_IBC_SR_EXT

        for (j = 0; j < 4; j++) {
          int row = (site[idx + j].mv.row * prec_multiplier);
          int col = (site[idx + j].mv.col * prec_multiplier);
          block_offset[j] = (row * ref_stride + col) + best_address;
        }

        ms_params->sdx4df(src_buf, src_stride, block_offset, ref_stride, sads);
        for (j = 0; j < 4; j++) {
          if (sads[j] < bestsad) {
            const FULLPEL_MV this_mv = {
              best_mv->row + (site[idx + j].mv.row * prec_multiplier),
              best_mv->col + (site[idx + j].mv.col * prec_multiplier)
            };

            unsigned int thissad =
                sads[j] + mvsad_err_cost(this_mv, mv_cost_params);
            if (thissad < bestsad) {
              bestsad = thissad;
              best_site = idx + j;
            }
          }
        }
      }
    } else {
      for (int idx = 1; idx <= cfg->searches_per_step[step]; idx++) {
        const FULLPEL_MV this_mv = {
          best_mv->row + (site[idx].mv.row * prec_multiplier),
          best_mv->col + (site[idx].mv.col * prec_multiplier)
        };

        if (av1_is_fullmv_in_range(&ms_params->mv_limits, this_mv,
                                   ms_params->mv_cost_params.pb_mv_precision)) {
#if CONFIG_IBC_SR_EXT
          if (ms_params->is_intra_mode &&
              ms_params->cm->features.allow_local_intrabc) {
            MV sub_mv = { (int16_t)GET_MV_SUBPEL(this_mv.row),
                          (int16_t)GET_MV_SUBPEL(this_mv.col) };
            int valid = av1_is_dv_valid(
                sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
                ms_params->mi_col, ms_params->bsize, ms_params->mib_size_log2);
            if (!valid) continue;
          }
#endif  // CONFIG_IBC_SR_EXT

          int r = (site[idx].mv.row * prec_multiplier);
          int c = (site[idx].mv.col * prec_multiplier);
          const uint16_t *const check_here =
              (r * ref_stride + c) + best_address;
          unsigned int thissad;

          thissad =
              get_mvpred_compound_sad(ms_params, src, check_here, ref_stride);

          assert(is_this_mv_precision_compliant(
              get_mv_from_fullmv(&this_mv),
              ms_params->mv_cost_params.pb_mv_precision));

          if (thissad < bestsad) {
            thissad += mvsad_err_cost(this_mv, mv_cost_params);
            if (thissad < bestsad) {
              bestsad = thissad;
              best_site = idx;
            }
          }
        }
      }
    }

    if (best_site != 0) {
      if (second_best_mv) {
        *second_best_mv = *best_mv;
      }
      best_mv->row += (site[best_site].mv.row * prec_multiplier);
      best_mv->col += (site[best_site].mv.col * prec_multiplier);
      best_address += (site[best_site].mv.row * prec_multiplier) * ref_stride +
                      (site[best_site].mv.col * prec_multiplier);

      is_off_center = 1;
    }

    if (is_off_center == 0) (*num00)++;

    if (best_site == 0) {
      while (next_step_size == cfg->radius[step] && step > 2) {
        ++(*num00);
        --step;
        next_step_size = cfg->radius[step - 1];
      }
    }
  }

  return bestsad;
}

/* do_refine: If last step (1-away) of n-step search doesn't pick the center
              point as the best match, we will do a final 1-away diamond
              refining search  */
static int full_pixel_diamond(const FULLPEL_MV start_mv,
                              const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                              const int step_param, int *cost_list,
                              FULLPEL_MV *best_mv, FULLPEL_MV *second_best_mv) {
  const search_site_config *cfg = ms_params->search_sites;
  int thissme, n, num00 = 0;
  int bestsme = diamond_search_sad(start_mv, ms_params, step_param, &n, best_mv,
                                   second_best_mv);

  if (bestsme < INT_MAX) {
    bestsme = get_mvpred_compound_var_cost(ms_params, best_mv);
  }

  // If there won't be more n-step search, check to see if refining search is
  // needed.
  const int further_steps = cfg->num_search_steps - 1 - step_param;
  while (n < further_steps) {
    ++n;

    if (num00) {
      num00--;
    } else {
      // TODO(chiyotsai@google.com): There is another bug here where the second
      // best mv gets incorrectly overwritten. Fix it later.
      FULLPEL_MV tmp_best_mv;
      thissme = diamond_search_sad(start_mv, ms_params, step_param + n, &num00,
                                   &tmp_best_mv, second_best_mv);

      if (thissme < INT_MAX) {
        thissme = get_mvpred_compound_var_cost(ms_params, &tmp_best_mv);
      }

      if (thissme < bestsme) {
        bestsme = thissme;
        *best_mv = tmp_best_mv;
      }
    }
  }

  // Return cost list.
  if (cost_list) {
    if (USE_SAD_COSTLIST) {
      const int costlist_has_sad = 0;
      calc_int_sad_list(*best_mv, ms_params, cost_list, costlist_has_sad);
    } else {
      calc_int_cost_list(*best_mv, ms_params, cost_list);
    }
  }
  return bestsme;
}

// Exhaustive motion search around a given centre position with a given
// step size.
static int exhaustive_mesh_search(FULLPEL_MV start_mv,
                                  const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                                  const int range, const int step,
                                  FULLPEL_MV *best_mv,
                                  FULLPEL_MV *second_best_mv) {
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const struct buf_2d *const src = ms_params->ms_buffers.src;
  const struct buf_2d *const ref = ms_params->ms_buffers.ref;
  const int ref_stride = ref->stride;
  unsigned int best_sad = INT_MAX;
  int r, c, i;
  int start_col, end_col, start_row, end_row;
  const int col_step = (step > 1) ? step : 4;

  assert(step >= 1);

  clamp_fullmv(&start_mv, &ms_params->mv_limits);
  *best_mv = start_mv;
#if CONFIG_IBC_SR_EXT
  if (ms_params->is_intra_mode && ms_params->cm->features.allow_local_intrabc) {
    const MV sub_mv = { (int16_t)GET_MV_SUBPEL(start_mv.row),
                        (int16_t)GET_MV_SUBPEL(start_mv.col) };
    if (av1_is_dv_valid(sub_mv, ms_params->cm, ms_params->xd, ms_params->mi_row,
                        ms_params->mi_col, ms_params->bsize,
                        ms_params->mib_size_log2)) {
      best_sad = get_mvpred_sad(
          ms_params, src, get_buf_from_fullmv(ref, &start_mv), ref_stride);
      best_sad += mvsad_err_cost(start_mv, mv_cost_params);

    } else {
      best_sad = INT_MAX;
    }
  } else {
    best_sad = get_mvpred_sad(ms_params, src,
                              get_buf_from_fullmv(ref, &start_mv), ref_stride);
    best_sad += mvsad_err_cost(start_mv, mv_cost_params);
  }
#else
  best_sad = get_mvpred_sad(ms_params, src, get_buf_from_fullmv(ref, &start_mv),
                            ref_stride);
  best_sad += mvsad_err_cost(start_mv, mv_cost_params);
#endif  // CONFIG_IBC_SR_EXT

  start_row = AOMMAX(-range, ms_params->mv_limits.row_min - start_mv.row);
  start_col = AOMMAX(-range, ms_params->mv_limits.col_min - start_mv.col);
  end_row = AOMMIN(range, ms_params->mv_limits.row_max - start_mv.row);
  end_col = AOMMIN(range, ms_params->mv_limits.col_max - start_mv.col);

#if CONFIG_IBC_SR_EXT
  if (ms_params->is_intra_mode && ms_params->cm->features.allow_local_intrabc) {
    int part_size = 65;
    int part_start_row;
    int part_start_col;
    int part_end_row;
    int part_end_col;
    FULLPEL_MV best_valid_mv = start_mv;
    unsigned int best_valid_sad = best_sad;
    for (part_start_row = start_row; part_start_row <= end_row;
         part_start_row += part_size) {
      part_end_row = AOMMIN(part_start_row + part_size - 1, end_row);
      for (part_start_col = start_col; part_start_col <= end_col;
           part_start_col += part_size) {
        part_end_col = AOMMIN(part_start_col + part_size - 1, end_col);
        for (r = part_start_row; r <= part_end_row; r += step) {
          for (c = part_start_col; c <= part_end_col; c += col_step) {
            // Step > 1 means we are not checking every location in this pass.
            if (step > 1) {
              const FULLPEL_MV mv = { start_mv.row + r, start_mv.col + c };
              unsigned int sad = get_mvpred_sad(
                  ms_params, src, get_buf_from_fullmv(ref, &mv), ref_stride);
              update_mvs_and_sad(sad, &mv, mv_cost_params, &best_sad,
                                 /*raw_best_sad=*/NULL, best_mv,
                                 second_best_mv);
            } else {
              // 4 sads in a single call if we are checking every location
              if (c + 3 <= part_end_col) {
                unsigned int sads[4];
                const uint16_t *addrs[4];
                for (i = 0; i < 4; ++i) {
                  const FULLPEL_MV mv = { start_mv.row + r,
                                          start_mv.col + c + i };
                  addrs[i] = get_buf_from_fullmv(ref, &mv);
                }

                ms_params->sdx4df(src->buf, src->stride, addrs, ref_stride,
                                  sads);

                for (i = 0; i < 4; ++i) {
                  if (sads[i] < best_sad) {
                    const FULLPEL_MV mv = { start_mv.row + r,
                                            start_mv.col + c + i };
                    update_mvs_and_sad(sads[i], &mv, mv_cost_params, &best_sad,
                                       /*raw_best_sad=*/NULL, best_mv,
                                       second_best_mv);
                  }
                }
              } else {
                for (i = 0; i < part_end_col - c; ++i) {
                  const FULLPEL_MV mv = { start_mv.row + r,
                                          start_mv.col + c + i };
                  unsigned int sad =
                      get_mvpred_sad(ms_params, src,
                                     get_buf_from_fullmv(ref, &mv), ref_stride);
                  update_mvs_and_sad(sad, &mv, mv_cost_params, &best_sad,
                                     /*raw_best_sad=*/NULL, best_mv,
                                     second_best_mv);
                }
              }
            }
          }
        }

        // stores the best valid mv
        if (best_valid_mv.row != best_mv->row ||
            best_valid_mv.col != best_mv->col) {
          const MV sub_mv = { (int16_t)GET_MV_SUBPEL(best_mv->row),
                              (int16_t)GET_MV_SUBPEL(best_mv->col) };
          if (av1_is_dv_valid(sub_mv, ms_params->cm, ms_params->xd,
                              ms_params->mi_row, ms_params->mi_col,
                              ms_params->bsize, ms_params->mib_size_log2)) {
            best_valid_mv = *best_mv;
            best_valid_sad = best_sad;
          }
        }
        *best_mv = best_valid_mv;
        best_sad = best_valid_sad;
      }
    }
    return best_sad;
  }
#endif  // CONFIG_IBC_SR_EXT

  for (r = start_row; r <= end_row; r += step) {
    for (c = start_col; c <= end_col; c += col_step) {
      // Step > 1 means we are not checking every location in this pass.
      if (step > 1) {
        const FULLPEL_MV mv = { start_mv.row + r, start_mv.col + c };
        unsigned int sad = get_mvpred_sad(
            ms_params, src, get_buf_from_fullmv(ref, &mv), ref_stride);
        update_mvs_and_sad(sad, &mv, mv_cost_params, &best_sad,
                           /*raw_best_sad=*/NULL, best_mv, second_best_mv);
      } else {
        // 4 sads in a single call if we are checking every location
        if (c + 3 <= end_col) {
          unsigned int sads[4];
          const uint16_t *addrs[4];
          for (i = 0; i < 4; ++i) {
            const FULLPEL_MV mv = { start_mv.row + r, start_mv.col + c + i };
            addrs[i] = get_buf_from_fullmv(ref, &mv);
          }

          ms_params->sdx4df(src->buf, src->stride, addrs, ref_stride, sads);

          for (i = 0; i < 4; ++i) {
            if (sads[i] < best_sad) {
              const FULLPEL_MV mv = { start_mv.row + r, start_mv.col + c + i };
              update_mvs_and_sad(sads[i], &mv, mv_cost_params, &best_sad,
                                 /*raw_best_sad=*/NULL, best_mv,
                                 second_best_mv);
            }
          }
        } else {
          for (i = 0; i < end_col - c; ++i) {
            const FULLPEL_MV mv = { start_mv.row + r, start_mv.col + c + i };
            unsigned int sad = get_mvpred_sad(
                ms_params, src, get_buf_from_fullmv(ref, &mv), ref_stride);
            update_mvs_and_sad(sad, &mv, mv_cost_params, &best_sad,
                               /*raw_best_sad=*/NULL, best_mv, second_best_mv);
          }
        }
      }
    }
  }

  return best_sad;
}

// Runs an limited range exhaustive mesh search using a pattern set
// according to the encode speed profile.
static int full_pixel_exhaustive(const FULLPEL_MV start_mv,
                                 const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                                 const struct MESH_PATTERN *const mesh_patterns,
                                 int *cost_list, FULLPEL_MV *best_mv,
                                 FULLPEL_MV *second_best_mv) {
  const int kMinRange = 7;
  const int kMaxRange = 256;
  const int kMinInterval = 1;

  int bestsme;
  int i;
  int interval = mesh_patterns[0].interval;
  int range = mesh_patterns[0].range;
  int baseline_interval_divisor;

  *best_mv = start_mv;

  // Trap illegal values for interval and range for this function.
  if ((range < kMinRange) || (range > kMaxRange) || (interval < kMinInterval) ||
      (interval > range))
    return INT_MAX;

  baseline_interval_divisor = range / interval;

  // Check size of proposed first range against magnitude of the centre
  // value used as a starting point.
  range = AOMMAX(range, (5 * AOMMAX(abs(best_mv->row), abs(best_mv->col))) / 4);
  range = AOMMIN(range, kMaxRange);
  interval = AOMMAX(interval, range / baseline_interval_divisor);
  // Use a small search step/interval for certain kind of clips.
  // For example, screen content clips with a lot of texts.
  // Large interval could lead to a false matching position, and it can't find
  // the best global candidate in following iterations due to reduced search
  // range. The solution here is to use a small search iterval in the beginning
  // and thus reduces the chance of missing the best candidate.
  if (ms_params->fine_search_interval) {
    interval = AOMMIN(interval, 4);
  }

  // initial search
  bestsme = exhaustive_mesh_search(*best_mv, ms_params, range, interval,
                                   best_mv, second_best_mv);

  if ((interval > kMinInterval) && (range > kMinRange)) {
    // Progressive searches with range and step size decreasing each time
    // till we reach a step size of 1. Then break out.
    for (i = 1; i < MAX_MESH_STEP; ++i) {
      // First pass with coarser step and longer range
      bestsme = exhaustive_mesh_search(
          *best_mv, ms_params, mesh_patterns[i].range,
          mesh_patterns[i].interval, best_mv, second_best_mv);

      if (mesh_patterns[i].interval == 1) break;
    }
  }

  if (bestsme < INT_MAX) {
    bestsme = get_mvpred_var_cost(ms_params, best_mv);
  }

  // Return cost list.
  if (cost_list) {
    if (USE_SAD_COSTLIST) {
      const int costlist_has_sad = 0;
      calc_int_sad_list(*best_mv, ms_params, cost_list, costlist_has_sad);
    } else {
      calc_int_cost_list(*best_mv, ms_params, cost_list);
    }
  }
  return bestsme;
}

// This function is called when we do joint motion search in comp_inter_inter
// mode, or when searching for one component of an ext-inter compound mode.
int av1_refining_search_8p_c(const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                             const FULLPEL_MV start_mv, FULLPEL_MV *best_mv) {
  static const search_neighbors neighbors[8] = {
    { { -1, 0 }, -1 * SEARCH_GRID_STRIDE_8P + 0 },
    { { 0, -1 }, 0 * SEARCH_GRID_STRIDE_8P - 1 },
    { { 0, 1 }, 0 * SEARCH_GRID_STRIDE_8P + 1 },
    { { 1, 0 }, 1 * SEARCH_GRID_STRIDE_8P + 0 },
    { { -1, -1 }, -1 * SEARCH_GRID_STRIDE_8P - 1 },
    { { 1, -1 }, 1 * SEARCH_GRID_STRIDE_8P - 1 },
    { { -1, 1 }, -1 * SEARCH_GRID_STRIDE_8P + 1 },
    { { 1, 1 }, 1 * SEARCH_GRID_STRIDE_8P + 1 }
  };

  uint8_t do_refine_search_grid[SEARCH_GRID_STRIDE_8P *
                                SEARCH_GRID_STRIDE_8P] = { 0 };
  int grid_center = SEARCH_GRID_CENTER_8P;
  int grid_coord = grid_center;

  assert(ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL);

  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const FullMvLimits *mv_limits = &ms_params->mv_limits;
  const MSBuffers *ms_buffers = &ms_params->ms_buffers;
  const struct buf_2d *src = ms_buffers->src;
  const struct buf_2d *ref = ms_buffers->ref;
  const int ref_stride = ref->stride;

  *best_mv = start_mv;
  clamp_fullmv(best_mv, mv_limits);

  unsigned int best_sad = get_mvpred_compound_sad(
      ms_params, src, get_buf_from_fullmv(ref, best_mv), ref_stride);
  best_sad += mvsad_err_cost(*best_mv, mv_cost_params);

  do_refine_search_grid[grid_coord] = 1;

  for (int i = 0; i < SEARCH_RANGE_8P; ++i) {
    int best_site = -1;

    for (int j = 0; j < 8; ++j) {
      grid_coord = grid_center + neighbors[j].coord_offset;
      if (do_refine_search_grid[grid_coord] == 1) {
        continue;
      }
      const FULLPEL_MV mv = { best_mv->row + neighbors[j].coord.row,
                              best_mv->col + neighbors[j].coord.col };

      do_refine_search_grid[grid_coord] = 1;
      if (av1_is_fullmv_in_range(mv_limits, mv

                                 ,
                                 ms_params->mv_cost_params.pb_mv_precision

                                 )) {
        unsigned int sad;
        sad = get_mvpred_compound_sad(
            ms_params, src, get_buf_from_fullmv(ref, &mv), ref_stride);
        if (sad < best_sad) {
          sad += mvsad_err_cost(mv, mv_cost_params);

          if (sad < best_sad) {
            best_sad = sad;
            best_site = j;
          }
        }
      }
    }

    if (best_site == -1) {
      break;
    } else {
      best_mv->row += neighbors[best_site].coord.row;
      best_mv->col += neighbors[best_site].coord.col;
      grid_center += neighbors[best_site].coord_offset;
    }
  }
  return best_sad;
}

// This function is called when precision of motion vector is lower than inter
// pel. This function is called when we do joint motion search in
// comp_inter_inter mode, or when searching for one component of an ext-inter
// compound mode.
int av1_refining_search_8p_c_low_precision(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV start_mv,
    FULLPEL_MV *best_mv, int fast_mv_refinement) {
  assert(ms_params->mv_cost_params.pb_mv_precision < MV_PRECISION_ONE_PEL);
  const int search_range =
      1 << (MV_PRECISION_ONE_PEL - ms_params->mv_cost_params.pb_mv_precision);
  const int search_grid_stride = (2 * search_range + 1);
  const search_neighbors neighbors[8] = {
    { { -search_range, 0 }, -search_range * search_grid_stride + 0 },
    { { 0, -search_range }, 0 * search_grid_stride - search_range },
    { { 0, search_range }, 0 * search_grid_stride + search_range },
    { { search_range, 0 }, search_range * search_grid_stride + 0 },
    { { -search_range, -search_range },
      -search_range * search_grid_stride - search_range },
    { { search_range, -search_range },
      search_range * search_grid_stride - search_range },
    { { -search_range, search_range },
      -search_range * search_grid_stride + search_range },
    { { search_range, search_range },
      search_range * search_grid_stride + search_range }
  };

  const int num_of_search_steps = fast_mv_refinement ? 1 : 3;

  assert(ms_params->mv_cost_params.pb_mv_precision < MV_PRECISION_ONE_PEL);
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const FullMvLimits *mv_limits = &ms_params->mv_limits;
  const MSBuffers *ms_buffers = &ms_params->ms_buffers;
  const struct buf_2d *src = ms_buffers->src;
  const struct buf_2d *ref = ms_buffers->ref;
  const int ref_stride = ref->stride;

  *best_mv = start_mv;
  clamp_fullmv(best_mv, mv_limits);

  unsigned int best_sad = get_mvpred_compound_sad(
      ms_params, src, get_buf_from_fullmv(ref, best_mv), ref_stride);
  best_sad += mvsad_err_cost(*best_mv, mv_cost_params);

  for (int step = 0; step < num_of_search_steps; step++) {
    int best_site = -1;
    // TODO(Mohammed): remove retundant search points to reduce complexity
    for (int j = 0; j < 8; ++j) {
      const FULLPEL_MV mv = { best_mv->row + neighbors[j].coord.row,
                              best_mv->col + neighbors[j].coord.col };

      if (av1_is_fullmv_in_range(mv_limits, mv,
                                 ms_params->mv_cost_params.pb_mv_precision)) {
        unsigned int sad;
        sad = get_mvpred_compound_sad(
            ms_params, src, get_buf_from_fullmv(ref, &mv), ref_stride);
        if (sad < best_sad) {
          sad += mvsad_err_cost(mv, mv_cost_params);
          if (sad < best_sad) {
            best_sad = sad;
            best_site = j;
          }
        }
      }
    }

    if (best_site == -1) {
      break;
    } else {
      best_mv->row += neighbors[best_site].coord.row;
      best_mv->col += neighbors[best_site].coord.col;
    }
  }

  return best_sad;
}

int av1_full_pixel_search(const FULLPEL_MV start_mv,
                          const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                          const int step_param, int *cost_list,
                          FULLPEL_MV *best_mv, FULLPEL_MV *second_best_mv) {
  const BLOCK_SIZE bsize = ms_params->bsize;
  const SEARCH_METHODS search_method = ms_params->search_method;

  const int is_intra_mode = ms_params->is_intra_mode;
  int run_mesh_search = ms_params->run_mesh_search;

  assert(is_this_mv_precision_compliant(
      get_mv_from_fullmv(&start_mv),
      ms_params->mv_cost_params.pb_mv_precision));

  int var = 0;
  MARK_MV_INVALID(best_mv);
  if (second_best_mv) {
    MARK_MV_INVALID(second_best_mv);
  }

  assert(ms_params->ms_buffers.second_pred == NULL &&
         ms_params->ms_buffers.mask == NULL &&
         "av1_full_pixel_search does not support compound pred");

  if (cost_list) {
    cost_list[0] = INT_MAX;
    cost_list[1] = INT_MAX;
    cost_list[2] = INT_MAX;
    cost_list[3] = INT_MAX;
    cost_list[4] = INT_MAX;
  }

  switch (search_method) {
    case FAST_BIGDIA:
      var = fast_bigdia_search(start_mv, ms_params, step_param, 0, cost_list,
                               best_mv);
      break;
    case FAST_DIAMOND:
      var = fast_dia_search(start_mv, ms_params, step_param, 0, cost_list,
                            best_mv);
      break;
    case FAST_HEX:
      var = fast_hex_search(start_mv, ms_params, step_param, 0, cost_list,
                            best_mv);
      break;
    case HEX:
      var = hex_search(start_mv, ms_params, step_param, 1, cost_list, best_mv);
      break;
    case SQUARE:
      var =
          square_search(start_mv, ms_params, step_param, 1, cost_list, best_mv);
      break;
    case BIGDIA:
      var =
          bigdia_search(start_mv, ms_params, step_param, 1, cost_list, best_mv);
      break;
    case NSTEP:
    case DIAMOND:
      var = full_pixel_diamond(start_mv, ms_params, step_param, cost_list,
                               best_mv, second_best_mv);
      break;
    default: assert(0 && "Invalid search method.");
  }
#if CONFIG_DEBUG
  if (best_mv) {
    assert(is_this_mv_precision_compliant(
        get_mv_from_fullmv(best_mv),
        ms_params->mv_cost_params.pb_mv_precision));
  }
  if (second_best_mv) {
    assert(is_this_mv_precision_compliant(
        get_mv_from_fullmv(second_best_mv),
        ms_params->mv_cost_params.pb_mv_precision));
  }
  assert((!(ms_params->mv_cost_params.pb_mv_precision < MV_PRECISION_ONE_PEL &&
            search_method != NSTEP && search_method != DIAMOND)));
#endif

  // Should we allow a follow on exhaustive search?
  if (!run_mesh_search && search_method == NSTEP) {
    int exhaustive_thr = ms_params->force_mesh_thresh;
    const int right_shift =
        10 - (mi_size_wide_log2[bsize] + mi_size_high_log2[bsize]);
    if (right_shift >= 0) {
      exhaustive_thr >>= right_shift;
    } else {
      exhaustive_thr <<= (-right_shift);
    }
    // Threshold variance for an exhaustive full search.
    if (var > exhaustive_thr) run_mesh_search = 1;
  }

  // TODO(yunqing): the following is used to reduce mesh search in temporal
  // filtering. Can extend it to intrabc.
  if (!is_intra_mode && ms_params->prune_mesh_search) {
    const int full_pel_mv_diff = AOMMAX(abs(start_mv.row - best_mv->row),
                                        abs(start_mv.col - best_mv->col));
    if (full_pel_mv_diff <= 4) {
      run_mesh_search = 0;
    }
  }

  if ((ms_params->sdf != ms_params->vfp->sdf) &&
      (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL)) {
    // If we are skipping rows when we perform the motion search, we need to
    // check the quality of skipping. If it's bad, then we run mesh search with
    // skip row features off.
    // TODO(chiyotsai@google.com): Handle the case where we have a vertical
    // offset of 1 before we hit this statement to avoid having to redo
    // motion search.
    const struct buf_2d *src = ms_params->ms_buffers.src;
    const struct buf_2d *ref = ms_params->ms_buffers.ref;
    const int src_stride = src->stride;
    const int ref_stride = ref->stride;

    const uint16_t *src_address = src->buf;
    const uint16_t *best_address = get_buf_from_fullmv(ref, best_mv);
    const int sad =
        ms_params->vfp->sdf(src_address, src_stride, best_address, ref_stride);
    const int skip_sad =
        ms_params->vfp->sdsf(src_address, src_stride, best_address, ref_stride);
    // We will keep the result of skipping rows if it's good enough. Here, good
    // enough means the error is less than 1 per pixel.
    const int kSADThresh =
        1 << (mi_size_wide_log2[bsize] + mi_size_high_log2[bsize]);
    if (sad > kSADThresh && abs(skip_sad - sad) * 10 >= AOMMAX(sad, 1) * 9) {
      // There is a large discrepancy between skipping and not skipping, so we
      // need to redo the motion search.
      FULLPEL_MOTION_SEARCH_PARAMS new_ms_params = *ms_params;
      new_ms_params.sdf = new_ms_params.vfp->sdf;
      new_ms_params.sdx4df = new_ms_params.vfp->sdx4df;

      return av1_full_pixel_search(start_mv, &new_ms_params, step_param,
                                   cost_list, best_mv, second_best_mv);
    }
  }

  if (run_mesh_search &&
      (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL)) {
    int var_ex;
    FULLPEL_MV tmp_mv_ex;
    // Pick the mesh pattern for exhaustive search based on the toolset (intraBC
    // or non-intraBC)
    // TODO(chiyotsai@google.com):  There is a bug here where the second best mv
    // gets overwritten without actually comparing the rdcost.
    const MESH_PATTERN *const mesh_patterns =
        ms_params->mesh_patterns[is_intra_mode];
    // TODO(chiyotsai@google.com): the second best mv is not set correctly by
    // full_pixel_exhaustive, which can incorrectly override it.
    var_ex = full_pixel_exhaustive(*best_mv, ms_params, mesh_patterns,
                                   cost_list, &tmp_mv_ex, second_best_mv);
    assert(is_this_mv_precision_compliant(
        get_mv_from_fullmv(&tmp_mv_ex),
        ms_params->mv_cost_params.pb_mv_precision));

    if (var_ex < var) {
      var = var_ex;
      *best_mv = tmp_mv_ex;
    }
  }

  return var;
}

// Get the cost for compound weighted prediction
int av1_get_cwp_idx_cost(int8_t cwp_idx, const AV1_COMMON *const cm,
                         const MACROBLOCK *x) {
  assert(cwp_idx >= CWP_MIN && cwp_idx <= CWP_MAX);
  const MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mi = xd->mi[0];
  int cost = 0;
  int bit_cnt = 0;
  const int ctx = 0;

  const int8_t final_idx = get_cwp_coding_idx(cwp_idx, 1, cm, mi);
  for (int idx = 0; idx < MAX_CWP_NUM - 1; ++idx) {
    cost += x->mode_costs.cwp_idx_cost[ctx][bit_cnt][final_idx != idx];
    if (final_idx == idx) return cost;
    ++bit_cnt;
  }
  return cost;
}

#if CONFIG_IBC_BV_IMPROVEMENT
int av1_get_ref_mvpred_var_cost(const AV1_COMP *cpi, const MACROBLOCKD *xd,
                                const FULLPEL_MOTION_SEARCH_PARAMS *ms_params) {
  const BLOCK_SIZE bsize = ms_params->bsize;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  const FullMvLimits *mv_limits = &ms_params->mv_limits;
  const MV *dv = ms_params->mv_cost_params.ref_mv;
  if (!av1_is_dv_valid(*dv, &cpi->common, xd, mi_row, mi_col, bsize,
                       cpi->common.mib_size_log2))
    return INT_MAX;

  FULLPEL_MV cur_mv = get_fullmv_from_mv(dv);
  if (!av1_is_fullmv_in_range(mv_limits, cur_mv,
                              ms_params->mv_cost_params.pb_mv_precision))
    return INT_MAX;

  int cost = get_mvpred_var_cost(ms_params, &cur_mv) -
             mv_err_cost(*dv, &ms_params->mv_cost_params);
  return cost;
}

void av1_init_ref_mv(MV_COST_PARAMS *mv_cost_params, const MV *ref_mv) {
  mv_cost_params->ref_mv = ref_mv;
  mv_cost_params->full_ref_mv = get_fullmv_from_mv(ref_mv);
}

void get_default_ref_bv(int_mv *cur_ref_bv,
                        const FULLPEL_MOTION_SEARCH_PARAMS *fullms_params) {
  if (cur_ref_bv->as_int == 0 || cur_ref_bv->as_int == INVALID_MV) {
    cur_ref_bv->as_int = 0;
  }
  if (cur_ref_bv->as_int == 0) {
    const TileInfo *const tile = &fullms_params->xd->tile;
    const AV1_COMMON *cm = fullms_params->cm;
    const int mi_row = fullms_params->mi_row;
    av1_find_ref_dv(cur_ref_bv, tile, cm->mib_size, mi_row);
  }
#if CONFIG_IBC_SUBPEL_PRECISION
  is_this_mv_precision_compliant(cur_ref_bv->as_mv,
                                 fullms_params->mv_cost_params.pb_mv_precision);
#else
  // Ref DV should not have sub-pel.
  assert((cur_ref_bv->as_mv.col & 7) == 0);
  assert((cur_ref_bv->as_mv.row & 7) == 0);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
}

int av1_get_intrabc_drl_idx_cost(int max_ref_bv_num, int intrabc_drl_idx,
                                 const MACROBLOCK *x) {
  assert(intrabc_drl_idx < max_ref_bv_num);
  int cost = 0;
  int bit_cnt = 0;
  for (int idx = 0; idx < max_ref_bv_num - 1; ++idx) {
    cost += x->mode_costs.intrabc_drl_idx_cost[bit_cnt][intrabc_drl_idx != idx];
    if (intrabc_drl_idx == idx) return cost;
    ++bit_cnt;
  }
  return cost;
}

int av1_get_ref_bv_rate_cost(int intrabc_mode, int intrabc_drl_idx,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                             int max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                             MACROBLOCK *x, int errorperbit, int ref_bv_cnt) {
  (void)ref_bv_cnt;
  int ref_bv_cost = 0;
  ref_bv_cost += x->mode_costs.intrabc_mode_cost[intrabc_mode];
  ref_bv_cost +=
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
      av1_get_intrabc_drl_idx_cost(max_bvp_drl_bits + 1, intrabc_drl_idx, x);
#else
      av1_get_intrabc_drl_idx_cost(MAX_REF_BV_STACK_SIZE, intrabc_drl_idx, x);
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  ref_bv_cost = (int)ROUND_POWER_OF_TWO_64(
      (int64_t)ref_bv_cost * errorperbit,
      RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT + 4);

  return ref_bv_cost;
}

int av1_pick_ref_bv(FULLPEL_MV *best_full_mv,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                    int max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                    const FULLPEL_MOTION_SEARCH_PARAMS *fullms_params) {
  MACROBLOCK *x = fullms_params->x;
  const MACROBLOCKD *const xd = fullms_params->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  int ref_bv_cnt = fullms_params->ref_bv_cnt;
  int cur_intrabc_drl_idx = 0;
  int_mv cur_ref_bv;
  cur_ref_bv.as_int = 0;
  int cur_ref_bv_cost = INT_MAX;
  MV best_mv = get_mv_from_fullmv(best_full_mv);
  int best_ref_bv_cost = INT_MAX;
  FULLPEL_MOTION_SEARCH_PARAMS ref_bv_ms_params = *fullms_params;

  for (cur_intrabc_drl_idx = 0; cur_intrabc_drl_idx < ref_bv_cnt;
       cur_intrabc_drl_idx++) {
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
    if (cur_intrabc_drl_idx > max_bvp_drl_bits) break;
#else
    if (cur_intrabc_drl_idx > MAX_REF_BV_STACK_SIZE - 1) break;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
    cur_ref_bv = xd->ref_mv_stack[INTRA_FRAME][cur_intrabc_drl_idx].this_mv;
    get_default_ref_bv(&cur_ref_bv, fullms_params);

    ref_bv_ms_params.mv_limits = fullms_params->mv_limits;
    av1_init_ref_mv(&ref_bv_ms_params.mv_cost_params, &cur_ref_bv.as_mv);

    // ref_mv value is changed. mv_limits need to recalculate
    av1_set_mv_search_range(&ref_bv_ms_params.mv_limits, &cur_ref_bv.as_mv,
                            mbmi->pb_mv_precision);
    if (!av1_is_fullmv_in_range(&ref_bv_ms_params.mv_limits, *best_full_mv,
                                mbmi->pb_mv_precision))
      continue;

    cur_ref_bv_cost =
        av1_get_ref_bv_rate_cost(
            0, cur_intrabc_drl_idx,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
            max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
            x, ref_bv_ms_params.mv_cost_params.mv_costs->errorperbit,
            ref_bv_cnt) +
        av1_get_mv_err_cost(&best_mv, &ref_bv_ms_params.mv_cost_params);

    if (cur_ref_bv_cost < best_ref_bv_cost) {
      best_ref_bv_cost = cur_ref_bv_cost;
      mbmi->intrabc_mode = 0;
      mbmi->intrabc_drl_idx = cur_intrabc_drl_idx;
      mbmi->ref_bv = cur_ref_bv;
    }
  }

  if (best_ref_bv_cost != INT_MAX) {
    assert(mbmi->intrabc_drl_idx >= 0);
    return best_ref_bv_cost;
  }
  return INT_MAX;
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT

int av1_intrabc_hash_search(const AV1_COMP *cpi, const MACROBLOCKD *xd,
                            const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                            IntraBCHashInfo *intrabc_hash_info,
                            FULLPEL_MV *best_mv) {
  if (!av1_use_hash_me(cpi)) return INT_MAX;

  const BLOCK_SIZE bsize = ms_params->bsize;
  const int block_width = block_size_wide[bsize];
  const int block_height = block_size_high[bsize];

  if (block_width != block_height) return INT_MAX;

  const FullMvLimits *mv_limits = &ms_params->mv_limits;
  const MSBuffers *ms_buffer = &ms_params->ms_buffers;

  const uint16_t *src = ms_buffer->src->buf;
  const int src_stride = ms_buffer->src->stride;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int x_pos = mi_col * MI_SIZE;
  const int y_pos = mi_row * MI_SIZE;

  uint32_t hash_value1, hash_value2;
  int best_hash_cost = INT_MAX;
#if CONFIG_IBC_BV_IMPROVEMENT
  int best_intrabc_mode = 0;
  int best_intrabc_drl_idx = 0;
  int_mv best_ref_bv;
  best_ref_bv.as_mv = *ms_params->mv_cost_params.ref_mv;
  MB_MODE_INFO *mbmi = xd->mi[0];
#endif  // CONFIG_IBC_BV_IMPROVEMENT

  // for the hashMap
  hash_table *ref_frame_hash = &intrabc_hash_info->intrabc_hash_table;

  av1_get_block_hash_value(intrabc_hash_info, src, src_stride, block_width,
                           &hash_value1, &hash_value2);

  const int count = av1_hash_table_count(ref_frame_hash, hash_value1);
  if (count <= 1) {
    return INT_MAX;
  }

  Iterator iterator = av1_hash_get_first_iterator(ref_frame_hash, hash_value1);
  for (int i = 0; i < count; i++, aom_iterator_increment(&iterator)) {
    block_hash ref_block_hash = *(block_hash *)(aom_iterator_get(&iterator));
    if (hash_value2 == ref_block_hash.hash_value2) {
      // Make sure the prediction is from valid area.
      const MV dv = { GET_MV_SUBPEL(ref_block_hash.y - y_pos),
                      GET_MV_SUBPEL(ref_block_hash.x - x_pos) };
      if (!av1_is_dv_valid(dv, &cpi->common, xd, mi_row, mi_col, bsize,
                           cpi->common.mib_size_log2))
        continue;

      FULLPEL_MV hash_mv;
      hash_mv.col = ref_block_hash.x - x_pos;
      hash_mv.row = ref_block_hash.y - y_pos;
      if (!av1_is_fullmv_in_range(mv_limits, hash_mv

                                  ,
                                  ms_params->mv_cost_params.pb_mv_precision

                                  ))
        continue;
#if CONFIG_IBC_BV_IMPROVEMENT
      int refCost = get_mvpred_var_cost(ms_params, &hash_mv);
      int cur_intrabc_mode = 0;
      int cur_intrabc_drl_idx = 0;
      int_mv cur_ref_bv;
      cur_ref_bv.as_mv = *(ms_params->mv_cost_params.ref_mv);
      int_mv cur_bv;
      cur_bv.as_mv = get_mv_from_fullmv(&hash_mv);

      int cur_dist = refCost - av1_get_mv_err_cost(&cur_bv.as_mv,
                                                   &ms_params->mv_cost_params);
      int cur_rate = av1_pick_ref_bv(&hash_mv,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                                     cpi->common.features.max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                                     ms_params);
      if (cur_rate != INT_MAX) {
        cur_ref_bv.as_mv = mbmi->ref_bv.as_mv;
        cur_intrabc_drl_idx = mbmi->intrabc_drl_idx;
        cur_intrabc_mode = mbmi->intrabc_mode;
        assert(cur_intrabc_mode == 0);
        refCost = cur_dist + cur_rate;
      }
#else
      const int refCost = get_mvpred_var_cost(ms_params, &hash_mv);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
      if (refCost < best_hash_cost) {
        best_hash_cost = refCost;
        *best_mv = hash_mv;
#if CONFIG_IBC_BV_IMPROVEMENT
        best_intrabc_mode = cur_intrabc_mode;
        best_intrabc_drl_idx = cur_intrabc_drl_idx;
        best_ref_bv = cur_ref_bv;
#endif  // CONFIG_IBC_BV_IMPROVEMENT
      }
    }
  }

#if CONFIG_IBC_BV_IMPROVEMENT
  mbmi->ref_bv = best_ref_bv;
  mbmi->intrabc_drl_idx = best_intrabc_drl_idx;
  mbmi->intrabc_mode = best_intrabc_mode;
#endif  // CONFIG_IBC_BV_IMPROVEMENT

  return best_hash_cost;
}

// =============================================================================
//  Fullpixel Motion Search: OBMC
// =============================================================================
static INLINE int get_obmc_mvpred_var(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV *this_mv) {
  const aom_variance_fn_ptr_t *vfp = ms_params->vfp;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const MSBuffers *ms_buffers = &ms_params->ms_buffers;
  const int32_t *wsrc = ms_buffers->wsrc;
  const int32_t *mask = ms_buffers->obmc_mask;
  const struct buf_2d *ref_buf = ms_buffers->ref;

#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV mv = get_mv_from_fullmv(this_mv);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*mv_cost_params->ref_mv, &sub_mv_offset,
                    mv_cost_params->pb_mv_precision);
  if (mv_cost_params->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    mv.col += sub_mv_offset.col;
    mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  unsigned int unused;

  return vfp->ovf(get_buf_from_fullmv(ref_buf, this_mv), ref_buf->stride, wsrc,
                  mask, &unused) +
         mv_err_cost(mv, mv_cost_params);
}

static int obmc_refining_search_sad(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, FULLPEL_MV *best_mv) {
  const aom_variance_fn_ptr_t *fn_ptr = ms_params->vfp;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const MSBuffers *ms_buffers = &ms_params->ms_buffers;
  const int32_t *wsrc = ms_buffers->wsrc;
  const int32_t *mask = ms_buffers->obmc_mask;
  const struct buf_2d *ref_buf = ms_buffers->ref;

  const int prec_shift =
      (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? 0
          : (MV_PRECISION_ONE_PEL - ms_params->mv_cost_params.pb_mv_precision);
  const FULLPEL_MV neighbors[4] = { { -(1 << prec_shift), 0 },
                                    { 0, -(1 << prec_shift) },
                                    { 0, (1 << prec_shift) },
                                    { (1 << prec_shift), 0 } };

  const int kSearchRange = 8;

  unsigned int best_sad = fn_ptr->osdf(get_buf_from_fullmv(ref_buf, best_mv),
                                       ref_buf->stride, wsrc, mask) +
                          mvsad_err_cost(*best_mv, mv_cost_params);

  for (int i = 0; i < kSearchRange; i++) {
    int best_site = -1;

    for (int j = 0; j < 4; j++) {
      const FULLPEL_MV mv = { best_mv->row + neighbors[j].row,
                              best_mv->col + neighbors[j].col };
      if (av1_is_fullmv_in_range(&ms_params->mv_limits, mv,
                                 ms_params->mv_cost_params.pb_mv_precision)) {
        unsigned int sad = fn_ptr->osdf(get_buf_from_fullmv(ref_buf, &mv),
                                        ref_buf->stride, wsrc, mask);
        if (sad < best_sad) {
          sad += mvsad_err_cost(mv, mv_cost_params);
          if (sad < best_sad) {
            best_sad = sad;
            best_site = j;
          }
        }
      }
    }

    if (best_site == -1) {
      break;
    } else {
      best_mv->row += neighbors[best_site].row;
      best_mv->col += neighbors[best_site].col;
    }
  }
  return best_sad;
}

static int obmc_diamond_search_sad(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, FULLPEL_MV start_mv,
    FULLPEL_MV *best_mv, int search_step, int *num00) {
  const aom_variance_fn_ptr_t *fn_ptr = ms_params->vfp;
  const search_site_config *cfg = ms_params->search_sites;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const MSBuffers *ms_buffers = &ms_params->ms_buffers;
  const int32_t *wsrc = ms_buffers->wsrc;
  const int32_t *mask = ms_buffers->obmc_mask;
  const struct buf_2d *const ref_buf = ms_buffers->ref;
  // search_step determines the length of the initial step and hence the number
  // of iterations
  // 0 = initial step (MAX_FIRST_STEP) pel : 1 = (MAX_FIRST_STEP/2) pel, 2 =
  // (MAX_FIRST_STEP/4) pel... etc.

  const int tot_steps = MAX_MVSEARCH_STEPS - 1 - search_step;
  const uint16_t *best_address, *init_ref;
  int best_sad = INT_MAX;
  int best_site = 0;
  int step;

  clamp_fullmv(&start_mv, &ms_params->mv_limits);
  best_address = init_ref = get_buf_from_fullmv(ref_buf, &start_mv);
  *num00 = 0;
  *best_mv = start_mv;

  // Check the starting position
  best_sad = fn_ptr->osdf(best_address, ref_buf->stride, wsrc, mask) +
             mvsad_err_cost(*best_mv, mv_cost_params);

  const int prec_shift =
      (ms_params->mv_cost_params.pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? 0
          : (MV_PRECISION_ONE_PEL - ms_params->mv_cost_params.pb_mv_precision);
  const int prec_multiplier = (1 << prec_shift);

  for (step = tot_steps; step >= 0; --step) {
    const search_site *const site = cfg->site[step];
    best_site = 0;
    for (int idx = 1; idx <= cfg->searches_per_step[step]; ++idx) {
      int r = (site[idx].mv.row * prec_multiplier);
      int c = (site[idx].mv.col * prec_multiplier);
      int offset = r * ref_buf->stride + c;
      const FULLPEL_MV mv = { best_mv->row + r, best_mv->col + c };

      if (av1_is_fullmv_in_range(&ms_params->mv_limits, mv,
                                 ms_params->mv_cost_params.pb_mv_precision

                                 )) {
        int sad =
            fn_ptr->osdf(best_address + offset, ref_buf->stride, wsrc, mask);

        if (sad < best_sad) {
          sad += mvsad_err_cost(mv, mv_cost_params);

          if (sad < best_sad) {
            best_sad = sad;
            best_site = idx;
          }
        }
      }
    }

    if (best_site != 0) {
      best_mv->row += (site[best_site].mv.row * prec_multiplier);
      best_mv->col += (site[best_site].mv.col * prec_multiplier);
      best_address +=
          ((site[best_site].mv.row * prec_multiplier) * ref_buf->stride +
           (site[best_site].mv.col * prec_multiplier));

    } else if (best_address == init_ref) {
      (*num00)++;
    }
  }
  return best_sad;
}

static int obmc_full_pixel_diamond(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV start_mv,
    int step_param, int do_refine, FULLPEL_MV *best_mv) {
  const search_site_config *cfg = ms_params->search_sites;
  FULLPEL_MV tmp_mv;
  int thissme, n, num00 = 0;
  int bestsme =
      obmc_diamond_search_sad(ms_params, start_mv, &tmp_mv, step_param, &n);
  if (bestsme < INT_MAX) bestsme = get_obmc_mvpred_var(ms_params, &tmp_mv);
  *best_mv = tmp_mv;

  // If there won't be more n-step search, check to see if refining search is
  // needed.
  const int further_steps = cfg->num_search_steps - 1 - step_param;
  if (n > further_steps) do_refine = 0;

  while (n < further_steps) {
    ++n;

    if (num00) {
      num00--;
    } else {
      thissme = obmc_diamond_search_sad(ms_params, start_mv, &tmp_mv,
                                        step_param + n, &num00);
      if (thissme < INT_MAX) thissme = get_obmc_mvpred_var(ms_params, &tmp_mv);

      // check to see if refining search is needed.
      if (num00 > further_steps - n) do_refine = 0;

      if (thissme < bestsme) {
        bestsme = thissme;
        *best_mv = tmp_mv;
      }
    }
  }

  // final 1-away diamond refining search
  if (do_refine) {
    tmp_mv = *best_mv;
    thissme = obmc_refining_search_sad(ms_params, &tmp_mv);
    if (thissme < INT_MAX) thissme = get_obmc_mvpred_var(ms_params, &tmp_mv);
    if (thissme < bestsme) {
      bestsme = thissme;
      *best_mv = tmp_mv;
    }
  }
  return bestsme;
}

int av1_obmc_full_pixel_search(const FULLPEL_MV start_mv,
                               const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                               const int step_param, FULLPEL_MV *best_mv) {
  if (!ms_params->fast_obmc_search) {
    const int do_refine = 1;
    const int bestsme = obmc_full_pixel_diamond(ms_params, start_mv, step_param,
                                                do_refine, best_mv);
    return bestsme;
  } else {
    *best_mv = start_mv;
    clamp_fullmv(best_mv, &ms_params->mv_limits);
    int thissme = obmc_refining_search_sad(ms_params, best_mv);
    if (thissme < INT_MAX) thissme = get_obmc_mvpred_var(ms_params, best_mv);
    return thissme;
  }
}

// =============================================================================
//  Subpixel Motion Search: Translational
// =============================================================================
#define INIT_SUBPEL_STEP_SIZE (4)
/*
 * To avoid the penalty for crossing cache-line read, preload the reference
 * area in a small buffer, which is aligned to make sure there won't be crossing
 * cache-line read while reading from this buffer. This reduced the cpu
 * cycles spent on reading ref data in sub-pixel filter functions.
 * TODO: Currently, since sub-pixel search range here is -3 ~ 3, copy 22 rows x
 * 32 cols area that is enough for 16x16 macroblock. Later, for SPLITMV, we
 * could reduce the area.
 */

// Returns the subpel offset used by various subpel variance functions [m]sv[a]f
static INLINE int get_subpel_part(int x) { return x & 7; }

// Gets the address of the ref buffer at subpel location (r, c), rounded to the
// nearest fullpel precision toward - \infty

static INLINE const uint16_t *get_buf_from_mv(const struct buf_2d *buf,
                                              const MV mv) {
  const int offset = (mv.row >> 3) * buf->stride + (mv.col >> 3);
  return &buf->buf[offset];
}

// Estimates the variance of prediction residue using bilinear filter for fast
// search.
static INLINE int estimated_pref_error(
    const MV *this_mv, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    unsigned int *sse) {
  const aom_variance_fn_ptr_t *vfp = var_params->vfp;

  const MSBuffers *ms_buffers = &var_params->ms_buffers;
  const uint16_t *src = ms_buffers->src->buf;
  const uint16_t *ref = get_buf_from_mv(ms_buffers->ref, *this_mv);
  const int src_stride = ms_buffers->src->stride;
  const int ref_stride = ms_buffers->ref->stride;
  const uint16_t *second_pred = ms_buffers->second_pred;
  const uint8_t *mask = ms_buffers->mask;
  const int mask_stride = ms_buffers->mask_stride;
  const int invert_mask = ms_buffers->inv_mask;

  const int subpel_x_q3 = get_subpel_part(this_mv->col);
  const int subpel_y_q3 = get_subpel_part(this_mv->row);

  if (second_pred == NULL) {
    return vfp->svf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride,
                    sse);
  } else if (mask) {
    return vfp->msvf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride,
                     second_pred, mask, mask_stride, invert_mask, sse);
  } else {
    return vfp->svaf(ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride,
                     sse, second_pred);
  }
}

// Calculates the variance of prediction residue.
#if !CONFIG_IBC_SUBPEL_PRECISION
static
#endif  // CONFIG_IBC_SUBPEL_PRECISION
    int
    upsampled_pref_error(MACROBLOCKD *xd, const AV1_COMMON *cm,
                         const MV *this_mv,
                         const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                         unsigned int *sse) {
  const aom_variance_fn_ptr_t *vfp = var_params->vfp;
  const SUBPEL_SEARCH_TYPE subpel_search_type = var_params->subpel_search_type;

  const MSBuffers *ms_buffers = &var_params->ms_buffers;
  const uint16_t *src = ms_buffers->src->buf;
  const uint16_t *ref = get_buf_from_mv(ms_buffers->ref, *this_mv);
  const int src_stride = ms_buffers->src->stride;
  const int ref_stride = ms_buffers->ref->stride;
  uint16_t *pred = xd->tmp_upsample_pred;
  const uint16_t *second_pred = ms_buffers->second_pred;
  const uint8_t *mask = ms_buffers->mask;
  const int mask_stride = ms_buffers->mask_stride;
  const int invert_mask = ms_buffers->inv_mask;
  const int w = var_params->w;
  const int h = var_params->h;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int subpel_x_q3 = get_subpel_part(this_mv->col);
  const int subpel_y_q3 = get_subpel_part(this_mv->row);

  unsigned int besterr;
  const int is_scaled_ref = ms_buffers->src->width == ms_buffers->ref->width &&
                            ms_buffers->src->height == ms_buffers->ref->height;

  if (second_pred != NULL) {
    if (mask) {
      aom_highbd_comp_mask_upsampled_pred(
          xd, cm, mi_row, mi_col, this_mv, pred, second_pred, w, h, subpel_x_q3,
          subpel_y_q3, ref, ref_stride, mask, mask_stride, invert_mask, xd->bd,
          subpel_search_type, is_scaled_ref);
    } else {
      if (get_cwp_idx(xd->mi[0]) != CWP_EQUAL) {
        DIST_WTD_COMP_PARAMS jcp_param;
        set_cmp_weight(xd->mi[0], invert_mask, &jcp_param);

        aom_highbd_dist_wtd_comp_avg_upsampled_pred(
            xd, cm, mi_row, mi_col, this_mv, pred, second_pred, w, h,
            subpel_x_q3, subpel_y_q3, ref, ref_stride, xd->bd, &jcp_param,
            subpel_search_type, is_scaled_ref);
      } else

        aom_highbd_comp_avg_upsampled_pred(xd, cm, mi_row, mi_col, this_mv,
                                           pred, second_pred, w, h, subpel_x_q3,
                                           subpel_y_q3, ref, ref_stride, xd->bd,
                                           subpel_search_type, is_scaled_ref);
    }
  } else {
    aom_highbd_upsampled_pred(xd, cm, mi_row, mi_col, this_mv, pred, w, h,
                              subpel_x_q3, subpel_y_q3, ref, ref_stride, xd->bd,
                              subpel_search_type, is_scaled_ref);
  }
  besterr = vfp->vf(pred, w, src, src_stride, sse);

  return besterr;
}

// Estimates whether this_mv is better than best_mv. This function incorporates
// both prediction error and residue into account. It is suffixed "fast" because
// it uses bilinear filter to estimate the prediction.
static INLINE unsigned int check_better_fast(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV *this_mv, MV *best_mv,
    const SubpelMvLimits *mv_limits, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int *has_better_mv, int is_scaled) {
  unsigned int cost;
  if (av1_is_subpelmv_in_range(mv_limits, *this_mv)) {
    unsigned int sse;
    int thismse;
    if (is_scaled) {
      thismse = upsampled_pref_error(xd, cm, this_mv, var_params, &sse);
    } else {
      thismse = estimated_pref_error(this_mv, var_params, &sse);
    }
    cost = mv_err_cost(*this_mv, mv_cost_params);
    cost += thismse;

    if (cost < *besterr) {
      *besterr = cost;
      *best_mv = *this_mv;
      *distortion = thismse;
      *sse1 = sse;
      *has_better_mv |= 1;
    }
  } else {
    cost = INT_MAX;
  }
  return cost;
}

// Checks whether this_mv is better than best_mv. This function incorporates
// both prediction error and residue into account.
static AOM_FORCE_INLINE unsigned int check_better(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV *this_mv, MV *best_mv,
    const SubpelMvLimits *mv_limits, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int *is_better) {
  unsigned int cost;
  if (av1_is_subpelmv_in_range(mv_limits, *this_mv)) {
    unsigned int sse;
    int thismse;
    thismse = upsampled_pref_error(xd, cm, this_mv, var_params, &sse);
    cost = mv_err_cost(*this_mv, mv_cost_params);
    cost += thismse;
    if (cost < *besterr) {
      *besterr = cost;
      *best_mv = *this_mv;
      *distortion = thismse;
      *sse1 = sse;
      *is_better |= 1;
    }
  } else {
    cost = INT_MAX;
  }
  return cost;
}

static INLINE MV get_best_diag_step(int step_size, unsigned int left_cost,
                                    unsigned int right_cost,
                                    unsigned int up_cost,
                                    unsigned int down_cost) {
  const MV diag_step = { up_cost <= down_cost ? -step_size : step_size,
                         left_cost <= right_cost ? -step_size : step_size };

  return diag_step;
}

// Searches the four cardinal direction for a better mv, then follows up with a
// search in the best quadrant. This uses bilinear filter to speed up the
// calculation.
static AOM_FORCE_INLINE MV first_level_check_fast(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV this_mv, MV *best_mv,
    int hstep, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int is_scaled) {
  // Check the four cardinal directions
  const MV left_mv = { this_mv.row, this_mv.col - hstep };
  int dummy = 0;
  const unsigned int left = check_better_fast(
      xd, cm, &left_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr,
      sse1, distortion, &dummy, is_scaled);

  const MV right_mv = { this_mv.row, this_mv.col + hstep };
  const unsigned int right = check_better_fast(
      xd, cm, &right_mv, best_mv, mv_limits, var_params, mv_cost_params,
      besterr, sse1, distortion, &dummy, is_scaled);

  const MV top_mv = { this_mv.row - hstep, this_mv.col };
  const unsigned int up = check_better_fast(
      xd, cm, &top_mv, best_mv, mv_limits, var_params, mv_cost_params, besterr,
      sse1, distortion, &dummy, is_scaled);

  const MV bottom_mv = { this_mv.row + hstep, this_mv.col };
  const unsigned int down = check_better_fast(
      xd, cm, &bottom_mv, best_mv, mv_limits, var_params, mv_cost_params,
      besterr, sse1, distortion, &dummy, is_scaled);

  const MV diag_step = get_best_diag_step(hstep, left, right, up, down);
  const MV diag_mv = { this_mv.row + diag_step.row,
                       this_mv.col + diag_step.col };

  // Check the diagonal direction with the best mv
  check_better_fast(xd, cm, &diag_mv, best_mv, mv_limits, var_params,
                    mv_cost_params, besterr, sse1, distortion, &dummy,
                    is_scaled);

  return diag_step;
}

// Performs a following up search after first_level_check_fast is called. This
// performs two extra chess pattern searches in the best quadrant.
static AOM_FORCE_INLINE void second_level_check_fast(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV this_mv, const MV diag_step,
    MV *best_mv, int hstep, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int is_scaled) {
  assert(diag_step.row == hstep || diag_step.row == -hstep);
  assert(diag_step.col == hstep || diag_step.col == -hstep);
  const int tr = this_mv.row;
  const int tc = this_mv.col;
  const int br = best_mv->row;
  const int bc = best_mv->col;
  int dummy = 0;
  if (tr != br && tc != bc) {
    assert(diag_step.col == bc - tc);
    assert(diag_step.row == br - tr);
    const MV chess_mv_1 = { br, bc + diag_step.col };
    const MV chess_mv_2 = { br + diag_step.row, bc };
    check_better_fast(xd, cm, &chess_mv_1, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);

    check_better_fast(xd, cm, &chess_mv_2, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);
  } else if (tr == br && tc != bc) {
    assert(diag_step.col == bc - tc);
    // Continue searching in the best direction
    const MV bottom_long_mv = { br + hstep, bc + diag_step.col };
    const MV top_long_mv = { br - hstep, bc + diag_step.col };
    check_better_fast(xd, cm, &bottom_long_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);
    check_better_fast(xd, cm, &top_long_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);

    // Search in the direction opposite of the best quadrant
    const MV rev_mv = { br - diag_step.row, bc };
    check_better_fast(xd, cm, &rev_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);
  } else if (tr != br && tc == bc) {
    assert(diag_step.row == br - tr);
    // Continue searching in the best direction
    const MV right_long_mv = { br + diag_step.row, bc + hstep };
    const MV left_long_mv = { br + diag_step.row, bc - hstep };
    check_better_fast(xd, cm, &right_long_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);
    check_better_fast(xd, cm, &left_long_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);

    // Search in the direction opposite of the best quadrant
    const MV rev_mv = { br, bc - diag_step.col };
    check_better_fast(xd, cm, &rev_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion, &dummy,
                      is_scaled);
  }
}

// Combines first level check and second level check when applicable. This first
// searches the four cardinal directions, and perform several
// diagonal/chess-pattern searches in the best quadrant.
static AOM_FORCE_INLINE void two_level_checks_fast(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV this_mv, MV *best_mv,
    int hstep, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int iters, int is_scaled) {
  const MV diag_step = first_level_check_fast(
      xd, cm, this_mv, best_mv, hstep, mv_limits, var_params, mv_cost_params,
      besterr, sse1, distortion, is_scaled);
  if (iters > 1) {
    second_level_check_fast(xd, cm, this_mv, diag_step, best_mv, hstep,
                            mv_limits, var_params, mv_cost_params, besterr,
                            sse1, distortion, is_scaled);
  }
}

static AOM_FORCE_INLINE MV
first_level_check(MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV this_mv,
                  MV *best_mv, const int hstep, const SubpelMvLimits *mv_limits,
                  const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                  const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
                  unsigned int *sse1, int *distortion) {
  int dummy = 0;
  const MV left_mv = { this_mv.row, this_mv.col - hstep };
  const MV right_mv = { this_mv.row, this_mv.col + hstep };
  const MV top_mv = { this_mv.row - hstep, this_mv.col };
  const MV bottom_mv = { this_mv.row + hstep, this_mv.col };

  const unsigned int left =
      check_better(xd, cm, &left_mv, best_mv, mv_limits, var_params,
                   mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int right =
      check_better(xd, cm, &right_mv, best_mv, mv_limits, var_params,
                   mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int up =
      check_better(xd, cm, &top_mv, best_mv, mv_limits, var_params,
                   mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int down =
      check_better(xd, cm, &bottom_mv, best_mv, mv_limits, var_params,
                   mv_cost_params, besterr, sse1, distortion, &dummy);

  const MV diag_step = get_best_diag_step(hstep, left, right, up, down);
  const MV diag_mv = { this_mv.row + diag_step.row,
                       this_mv.col + diag_step.col };

  // Check the diagonal direction with the best mv
  check_better(xd, cm, &diag_mv, best_mv, mv_limits, var_params, mv_cost_params,
               besterr, sse1, distortion, &dummy);

  return diag_step;
}
// A newer version of second level check that gives better quality.
// TODO(chiyotsai@google.com): evaluate this on subpel_search_types different
// from av1_find_best_sub_pixel_tree

static AOM_FORCE_INLINE void second_level_check_v2(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV this_mv, MV diag_step,
    MV *best_mv, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion) {
  assert(best_mv->row == this_mv.row + diag_step.row ||
         best_mv->col == this_mv.col + diag_step.col);
  if (CHECK_MV_EQUAL(this_mv, *best_mv)) {
    return;
  } else if (this_mv.row == best_mv->row) {
    // Search away from diagonal step since diagonal search did not provide any
    // improvement
    diag_step.row *= -1;
  } else if (this_mv.col == best_mv->col) {
    diag_step.col *= -1;
  }

  const MV row_bias_mv = { best_mv->row + diag_step.row, best_mv->col };
  const MV col_bias_mv = { best_mv->row, best_mv->col + diag_step.col };
  const MV diag_bias_mv = { best_mv->row + diag_step.row,
                            best_mv->col + diag_step.col };
  int has_better_mv = 0;

  check_better(xd, cm, &row_bias_mv, best_mv, mv_limits, var_params,
               mv_cost_params, besterr, sse1, distortion, &has_better_mv);
  check_better(xd, cm, &col_bias_mv, best_mv, mv_limits, var_params,
               mv_cost_params, besterr, sse1, distortion, &has_better_mv);

  // Do an additional search if the second iteration gives a better mv
  if (has_better_mv) {
    check_better(xd, cm, &diag_bias_mv, best_mv, mv_limits, var_params,
                 mv_cost_params, besterr, sse1, distortion, &has_better_mv);
  }
}

// Gets the error at the beginning when the mv has fullpel precision
static unsigned int setup_center_error(
    const MACROBLOCKD *xd, const MV *bestmv,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *sse1, int *distortion) {
  (void)xd;
  const aom_variance_fn_ptr_t *vfp = var_params->vfp;
  const int w = var_params->w;
  const int h = var_params->h;

  const MSBuffers *ms_buffers = &var_params->ms_buffers;
  const uint16_t *src = ms_buffers->src->buf;
  const uint16_t *y = get_buf_from_mv(ms_buffers->ref, *bestmv);
  const int src_stride = ms_buffers->src->stride;
  const int y_stride = ms_buffers->ref->stride;
  const uint16_t *second_pred = ms_buffers->second_pred;
  const uint8_t *mask = ms_buffers->mask;
  const int mask_stride = ms_buffers->mask_stride;
  const int invert_mask = ms_buffers->inv_mask;

  unsigned int besterr;

  if (second_pred != NULL) {
    DECLARE_ALIGNED(16, uint16_t, comp_pred[MAX_SB_SQUARE]);
    if (mask) {
      aom_highbd_comp_mask_pred(comp_pred, second_pred, w, h, y, y_stride, mask,
                                mask_stride, invert_mask);
    } else {
      aom_highbd_comp_avg_pred(comp_pred, second_pred, w, h, y, y_stride);
    }
    besterr = vfp->vf(comp_pred, w, src, src_stride, sse1);
  } else {
    besterr = vfp->vf(y, y_stride, src, src_stride, sse1);
  }
  *distortion = besterr;
  besterr += mv_err_cost(*bestmv, mv_cost_params);

  return besterr;
}

// Gets the error at the beginning when the mv has fullpel precision
static unsigned int upsampled_setup_center_error(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV *bestmv,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *sse1, int *distortion) {
  unsigned int besterr = upsampled_pref_error(xd, cm, bestmv, var_params, sse1);
  *distortion = besterr;
  besterr += mv_err_cost(*bestmv, mv_cost_params);
  return besterr;
}

static INLINE int divide_and_round(int n, int d) {
  return ((n < 0) ^ (d < 0)) ? ((n - d / 2) / d) : ((n + d / 2) / d);
}

static INLINE int is_cost_list_wellbehaved(const int *cost_list) {
  return cost_list[0] < cost_list[1] && cost_list[0] < cost_list[2] &&
         cost_list[0] < cost_list[3] && cost_list[0] < cost_list[4];
}

// Returns surface minima estimate at given precision in 1/2^n bits.
// Assume a model for the cost surface: S = A(x - x0)^2 + B(y - y0)^2 + C
// For a given set of costs S0, S1, S2, S3, S4 at points
// (y, x) = (0, 0), (0, -1), (1, 0), (0, 1) and (-1, 0) respectively,
// the solution for the location of the minima (x0, y0) is given by:
// x0 = 1/2 (S1 - S3)/(S1 + S3 - 2*S0),
// y0 = 1/2 (S4 - S2)/(S4 + S2 - 2*S0).
// The code below is an integerized version of that.
static AOM_INLINE void get_cost_surf_min(const int *cost_list, int *ir, int *ic,
                                         int bits) {
  *ic = divide_and_round((cost_list[1] - cost_list[3]) * (1 << (bits - 1)),
                         (cost_list[1] - 2 * cost_list[0] + cost_list[3]));
  *ir = divide_and_round((cost_list[4] - cost_list[2]) * (1 << (bits - 1)),
                         (cost_list[4] - 2 * cost_list[0] + cost_list[2]));
}

// Checks the list of mvs searched in the last iteration and see if we are
// repeating it. If so, return 1. Otherwise we update the last_mv_search_list
// with current_mv and return 0.
static INLINE int check_repeated_mv_and_update(int_mv *last_mv_search_list,
                                               const MV current_mv, int iter) {
  if (last_mv_search_list) {
    if (CHECK_MV_EQUAL(last_mv_search_list[iter].as_mv, current_mv)) {
      return 1;
    }

    last_mv_search_list[iter].as_mv = current_mv;
  }
  return 0;
}

static AOM_INLINE int setup_center_error_facade(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV *bestmv,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *sse1, int *distortion,
    int is_scaled) {
  if (is_scaled) {
    return upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                        mv_cost_params, sse1, distortion);
  } else {
    return setup_center_error(xd, bestmv, var_params, mv_cost_params, sse1,
                              distortion);
  }
}

// motion search for joint mvd coding
int joint_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                     SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV ref_mv,
                     MV *start_mv, MV *bestmv, int *distortion,
                     unsigned int *sse1, int ref_idx, MV *other_mv,
                     MV *best_other_mv, uint16_t *second_pred,
                     InterPredParams *inter_pred_params,
                     int_mv *last_mv_search_list) {
  const int forced_stop = ms_params->forced_stop;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  MB_MODE_INFO *const mbmi = xd->mi[0];
  // perform prediction for second MV
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];

#if CONFIG_C071_SUBBLK_WARPMV
  if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
    lower_mv_precision(&ref_mv, mbmi->pb_mv_precision);
  // We are not signaling other_mv. So frame level precision should be okay.

  // How many steps to take. A round of 0 means fullpel search only, 1 means
  // half-pel, and so on.
  const int round = (mbmi->pb_mv_precision >= MV_PRECISION_ONE_PEL)
                        ? AOMMIN(FULL_PEL - forced_stop,
                                 mbmi->pb_mv_precision - MV_PRECISION_ONE_PEL)
                        : 0;

  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel

  unsigned int besterr = INT_MAX;

  *bestmv = *start_mv;
  *best_other_mv = *other_mv;

#if CONFIG_C071_SUBBLK_WARPMV
  if (mbmi->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    FULLPEL_MV tmp_full_bestmv = get_fullmv_from_mv(bestmv);
    *bestmv = get_mv_from_fullmv(&tmp_full_bestmv);
    MV sub_mv_offset = { 0, 0 };
    get_phase_from_mv(ref_mv, &sub_mv_offset, mbmi->pb_mv_precision);
    bestmv->col += sub_mv_offset.col;
    bestmv->row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV

  const int same_side = is_ref_frame_same_side(cm, mbmi);

  const int cur_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[ref_idx]];
  int other_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[1 - ref_idx]];

  other_ref_dist = same_side ? other_ref_dist : -other_ref_dist;

  int dummy = 0;

  // full-pel search for one list
  static const search_neighbors neighbors[9] = {
    { { 0, 0 }, 0 * SEARCH_GRID_STRIDE_8P + 0 },
    { { -8, 0 }, -1 * SEARCH_GRID_STRIDE_8P + 0 },
    { { 0, -8 }, 0 * SEARCH_GRID_STRIDE_8P - 1 },
    { { 0, 8 }, 0 * SEARCH_GRID_STRIDE_8P + 1 },
    { { 8, 0 }, 1 * SEARCH_GRID_STRIDE_8P + 0 },
    { { -8, -8 }, -1 * SEARCH_GRID_STRIDE_8P - 1 },
    { { 8, -8 }, 1 * SEARCH_GRID_STRIDE_8P - 1 },
    { { -8, 8 }, -1 * SEARCH_GRID_STRIDE_8P + 1 },
    { { 8, 8 }, 1 * SEARCH_GRID_STRIDE_8P + 1 }
  };

  uint8_t do_refine_search_grid[SEARCH_GRID_STRIDE_8P *
                                SEARCH_GRID_STRIDE_8P] = { 0 };
  int grid_center = SEARCH_GRID_CENTER_8P;
  int grid_coord;

  // do_refine_search_grid[grid_coord] = 0;

  for (int i = 0; i < SEARCH_RANGE_8P; ++i) {
    int best_site = -1;

    for (int j = 0; j < 9; ++j) {
      grid_coord = grid_center + neighbors[j].coord_offset;
      if (do_refine_search_grid[grid_coord] == 1) {
        continue;
      }
      const MV cur_mv = { bestmv->row + neighbors[j].coord.row,
                          bestmv->col + neighbors[j].coord.col };

      const MV cur_mvd = { cur_mv.row - ref_mv.row, cur_mv.col - ref_mv.col };
      MV other_mvd = { 0, 0 };
      MV other_cand_mv = { 0, 0 };

      do_refine_search_grid[grid_coord] = 1;
      if (av1_is_subpelmv_in_range(mv_limits, cur_mv)) {
        // fprintf(stdout, "has happened\n");
        get_mv_projection(&other_mvd, cur_mvd, other_ref_dist, cur_ref_dist);
        scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);
#if !CONFIG_C071_SUBBLK_WARPMV
        lower_mv_precision(&other_mvd, cm->features.fr_mv_precision);
#endif  // !CONFIG_C071_SUBBLK_WARPMV

        other_cand_mv.row = (int)(other_mv->row + other_mvd.row);
        other_cand_mv.col = (int)(other_mv->col + other_mvd.col);
        if (av1_is_subpelmv_in_range(mv_limits, other_cand_mv) == 0) continue;
        av1_enc_build_one_inter_predictor(second_pred, block_size_wide[bsize],
                                          &other_cand_mv, inter_pred_params);
        unsigned int sad =
            check_better(xd, cm, &cur_mv, bestmv, mv_limits, var_params,
                         mv_cost_params, &besterr, sse1, distortion, &dummy);

        if (sad == besterr && bestmv->row == cur_mv.row &&
            bestmv->col == cur_mv.col) {
          best_site = j;
          // best mv on the other reference list
          best_other_mv->row = other_cand_mv.row;
          best_other_mv->col = other_cand_mv.col;
        }
      }
    }
    if (best_site == -1) {
      break;
    } else {
      grid_center += neighbors[best_site].coord_offset;
    }
  }  // end of full-pel search

  if (besterr == INT_MAX) {
    bestmv->row = ref_mv.row;
    bestmv->col = ref_mv.col;
    start_mv->row = ref_mv.row;
    start_mv->col = ref_mv.col;
  }

  // If forced_stop is FULL_PEL, return.
  if (round == 0) return besterr;
  // fractional motion search

  const int cand_pos[4][2] = {
    { 0, -1 },  // left
    { 0, +1 },  // right
    { -1, 0 },  // up
    { +1, 0 }   // down
  };

  for (int iter = 0; iter < round; ++iter) {
    MV iter_center_mv = *bestmv;
    if (check_repeated_mv_and_update(last_mv_search_list, iter_center_mv,
                                     iter)) {
      return INT_MAX;
    }
    MV candidate_mv[2];
    MV cur_mvd = { 0, 0 };
    // mv cost of top, left, right, bottom
    int mvcost[5] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX };
    for (int i = 0; i < 5; ++i) {
#if CONFIG_C071_SUBBLK_WARPMV
      if (av1_is_subpelmv_in_range(mv_limits, iter_center_mv) == 0) continue;
#endif  // CONFIG_C071_SUBBLK_WARPMV
      if (i < 4) {
        cur_mvd.row = cand_pos[i][0] * hstep;
        cur_mvd.col = cand_pos[i][1] * hstep;
      } else {
        cur_mvd = get_best_diag_step(hstep, mvcost[0], mvcost[1], mvcost[2],
                                     mvcost[3]);
      }
      MV other_mvd = { 0, 0 };
      candidate_mv[0].row = iter_center_mv.row + cur_mvd.row;
      candidate_mv[0].col = iter_center_mv.col + cur_mvd.col;
#if CONFIG_C071_SUBBLK_WARPMV
      if (av1_is_subpelmv_in_range(mv_limits, candidate_mv[0]) == 0) continue;
#endif  // CONFIG_C071_SUBBLK_WARPMV

      const MV final_mvd = { candidate_mv[0].row - ref_mv.row,
                             candidate_mv[0].col - ref_mv.col };

      get_mv_projection(&other_mvd, final_mvd, other_ref_dist, cur_ref_dist);
      scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);

#if !CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&other_mvd, cm->features.fr_mv_precision);
#endif  // !CONFIG_C071_SUBBLK_WARPMV

      candidate_mv[1].row = (int)(other_mv->row + other_mvd.row);
      candidate_mv[1].col = (int)(other_mv->col + other_mvd.col);
      if (av1_is_subpelmv_in_range(mv_limits, candidate_mv[1]) == 0) continue;
      av1_enc_build_one_inter_predictor(second_pred, block_size_wide[bsize],
                                        &candidate_mv[1], inter_pred_params);
      mvcost[i] =
          check_better(xd, cm, &candidate_mv[0], bestmv, mv_limits, var_params,
                       mv_cost_params, &besterr, sse1, distortion, &dummy);

      // best mv on the other reference list
      if (bestmv->row == candidate_mv[0].row &&
          bestmv->col == candidate_mv[0].col) {
        // fprintf(stdout, "has selected\n");
        best_other_mv->row = candidate_mv[1].row;
        best_other_mv->col = candidate_mv[1].col;
      }
    }
    hstep >>= 1;
  }
#if CONFIG_C071_SUBBLK_WARPMV
  if (av1_is_subpelmv_in_range(&ms_params->mv_limits, *bestmv) == 0)
    besterr = INT_MAX;
#endif  // CONFIG_C071_SUBBLK_WARPMV
  return besterr;
}

// motion search for 2/4/8 pel precision for joint mvd coding
int low_precision_joint_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                                   SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                   MV ref_mv, MV *start_mv, MV *bestmv,
                                   int *distortion, unsigned int *sse1,
                                   int ref_idx, MV *other_mv, MV *best_other_mv,
                                   uint16_t *second_pred,
                                   InterPredParams *inter_pred_params) {
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  MB_MODE_INFO *const mbmi = xd->mi[0];
  // perform prediction for second MV
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];

#if CONFIG_C071_SUBBLK_WARPMV
  if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif
    lower_mv_precision(&ref_mv, mbmi->pb_mv_precision);
  // We are not signaling other_mv. So frame level precision should be okay.

  unsigned int besterr = INT_MAX;

  *bestmv = *start_mv;
  *best_other_mv = *other_mv;

  const int same_side = is_ref_frame_same_side(cm, mbmi);

  const int cur_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[ref_idx]];
  int other_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[1 - ref_idx]];

  other_ref_dist = same_side ? other_ref_dist : -other_ref_dist;

  int dummy = 0;

  const int search_range = 1 << (MV_PRECISION_ONE_PEL - mbmi->pb_mv_precision);
  const int search_grid_stride = (2 * search_range + 1);
  const search_neighbors neighbors[9] = {
    { { 0, 0 }, 0 * search_grid_stride + 0 },
    { { -search_range, 0 }, -search_range * search_grid_stride + 0 },
    { { 0, -search_range }, 0 * search_grid_stride - search_range },
    { { 0, search_range }, 0 * search_grid_stride + search_range },
    { { search_range, 0 }, search_range * search_grid_stride + 0 },
    { { -search_range, -search_range },
      -search_range * search_grid_stride - search_range },
    { { search_range, -search_range },
      search_range * search_grid_stride - search_range },
    { { -search_range, search_range },
      -search_range * search_grid_stride + search_range },
    { { search_range, search_range },
      search_range * search_grid_stride + search_range }
  };

  const int num_of_search_steps = 3;

  for (int i = 0; i < num_of_search_steps; ++i) {
    int best_site = -1;

    for (int j = 0; j < 9; ++j) {
      const MV cur_mv = { (bestmv->row + (neighbors[j].coord.row * 8)),
                          (bestmv->col + (neighbors[j].coord.col * 8)) };

      const MV cur_mvd = { cur_mv.row - ref_mv.row, cur_mv.col - ref_mv.col };
      MV other_mvd = { 0, 0 };
      MV other_cand_mv = { 0, 0 };

      if (av1_is_subpelmv_in_range(mv_limits, cur_mv)) {
        get_mv_projection(&other_mvd, cur_mvd, other_ref_dist, cur_ref_dist);
        scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);
#if !CONFIG_C071_SUBBLK_WARPMV
        lower_mv_precision(&other_mvd, cm->features.fr_mv_precision);
#endif  // !CONFIG_C071_SUBBLK_WARPMV

        other_cand_mv.row = (int)(other_mv->row + other_mvd.row);
        other_cand_mv.col = (int)(other_mv->col + other_mvd.col);
        if (av1_is_subpelmv_in_range(mv_limits, other_cand_mv) == 0) continue;
        av1_enc_build_one_inter_predictor(second_pred, block_size_wide[bsize],
                                          &other_cand_mv, inter_pred_params);
        unsigned int sad =
            check_better(xd, cm, &cur_mv, bestmv, mv_limits, var_params,
                         mv_cost_params, &besterr, sse1, distortion, &dummy);

        if (sad == besterr && bestmv->row == cur_mv.row &&
            bestmv->col == cur_mv.col) {
          best_site = j;
          // best mv on the other reference list
          best_other_mv->row = other_cand_mv.row;
          best_other_mv->col = other_cand_mv.col;
        }
      }
    }
    if (best_site == -1) {
      break;
    }
  }  // end of full-pel search

  if (besterr == INT_MAX) {
    bestmv->row = ref_mv.row;
    bestmv->col = ref_mv.col;
    start_mv->row = ref_mv.row;
    start_mv->col = ref_mv.col;
  }

#if CONFIG_DEBUG
  const MV xxmv = { bestmv->row, bestmv->col };
  assert(is_this_mv_precision_compliant(xxmv, mbmi->pb_mv_precision));
  (void)xxmv;
#endif

  return besterr;
}

// motion search for near_new and new_near mode when adaptive MVD resolution is
// applied
int adaptive_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv,
                        MV *bestmv, int *distortion, unsigned int *sse1) {
#if !CONFIG_VQ_MVD_CODING
  const int allow_hp = 0;
  const int forced_stop = ms_params->forced_stop;
#endif  //! CONFIG_VQ_MVD_CODING
  // const int iters_per_step = ms_params->iters_per_step;
  MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SUBPEL_SEARCH_TYPE subpel_search_type =
      ms_params->var_params.subpel_search_type;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  MB_MODE_INFO *const mbmi = xd->mi[0];
#if BUGFIX_AMVD_AMVR
  set_amvd_mv_precision(mbmi, mbmi->max_mv_precision);
  mv_cost_params->pb_mv_precision = mbmi->pb_mv_precision;
#else
  assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif

#if !CONFIG_VQ_MVD_CODING
  // How many steps to take. A round of 0 means fullpel search only, 1 means
  // half-pel, and so on.
  int round = AOMMIN(FULL_PEL - forced_stop, FULL_PEL - !allow_hp);
  if (cm->features.cur_frame_force_integer_mv) round = 0;
  int hstep = 8 >> round;  // Step size, initialized to 4/8=1/2 pel
#endif                     //! CONFIG_VQ_MVD_CODING

  unsigned int besterr = INT_MAX;

#if CONFIG_VQ_MVD_CODING
  MV this_mvd;
  get_adaptive_mvd_from_ref_mv(start_mv, *ms_params->mv_cost_params.ref_mv,
                               &this_mvd);
  if (is_valid_amvd_mvd(this_mvd)) {
#endif  // CONFIG_VQ_MVD_CODING
    *bestmv = start_mv;

    if (subpel_search_type != FILTER_UNUSED) {
      besterr = upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                             mv_cost_params, sse1, distortion);
    } else {
      besterr = setup_center_error(xd, bestmv, var_params, mv_cost_params, sse1,
                                   distortion);
    }
#if CONFIG_VQ_MVD_CODING
  }
#endif  // CONFIG_VQ_MVD_CODING

  MV iter_center_mv = start_mv;
  const int cand_pos[4][2] = {
    { 0, -1 },  // left
    { 0, +1 },  // right
    { -1, 0 },  // above
    { +1, 0 }   // down
  };

#if CONFIG_VQ_MVD_CODING
  for (int curr_index = 1; curr_index <= MAX_AMVD_INDEX; curr_index++) {
    MV candidate_mv[2];
    int dummy = 0;
    // loop 4 directions, left, right, above, and right
    for (int i = 0; i < 4; ++i) {
      const MV cur_mvd_idx = { cand_pos[i][0] * curr_index,
                               cand_pos[i][1] * curr_index };
      //  printf("curr_index = %d row = %d col = %d \n ", curr_index
      //  ,cur_mvd_idx.row, cur_mvd_idx.col);

      const MV cur_mvd = { get_mvd_from_amvd_index(cur_mvd_idx.row),
                           get_mvd_from_amvd_index(cur_mvd_idx.col) };
      candidate_mv[0].row = iter_center_mv.row + cur_mvd.row;
      candidate_mv[0].col = iter_center_mv.col + cur_mvd.col;
      assert(is_valid_amvd_mvd(cur_mvd));

      check_better(xd, cm, &candidate_mv[0], bestmv, mv_limits, var_params,
                   mv_cost_params, &besterr, sse1, distortion, &dummy);
    }
    int abs_mvd_from_idx = get_mvd_from_amvd_index(curr_index);

    if (abs_mvd_from_idx >= 16) {
      if (abs(bestmv->row - start_mv.row) <= abs_mvd_from_idx / 4 &&
          abs(bestmv->col - start_mv.col) <= abs_mvd_from_idx / 4)
        break;
    }
  }
#else
  for (int iter = hstep; iter <= 256;) {
    int dummy = 0;
    MV candidate_mv[2];
    // loop 4 directions, left, right, above, and right
    for (int i = 0; i < 4; ++i) {
      const MV cur_mvd = { cand_pos[i][0] * iter, cand_pos[i][1] * iter };
      candidate_mv[0].row = iter_center_mv.row + cur_mvd.row;
      candidate_mv[0].col = iter_center_mv.col + cur_mvd.col;

      check_better(xd, cm, &candidate_mv[0], bestmv, mv_limits, var_params,
                   mv_cost_params, &besterr, sse1, distortion, &dummy);
    }

    // fast encoder method to early terminate the MV search
    // after searching [-2,+2] MV samples, if the best MV is still within
    // [-1/2,1/2], stop the MV search
    if (iter >= 16) {
      if (abs(bestmv->row - start_mv.row) <= iter / 4 &&
          abs(bestmv->col - start_mv.col) <= iter / 4)
        break;
    }

    // allow integer and fractional MVD in (0,1]
    if (iter < 8) iter += hstep;
    // only allow integer MVD in (1,2]
    else if (iter < 16)
      iter += 8;
    // only allow 4 integer MVD in (2,4]
    else if (iter < 32)
      iter += 16;
    // only allow 8 sample integer MVD in (4,8]
    else if (iter < 64)
      iter += 32;
    // only allow 16 sample integer MVD in (8,16]
    else if (iter < 128)
      iter += 64;
    // only allow 32 sample integer MVD in (16,32]
    else
      iter += 128;
  }
#endif  // CONFIG_VQ_MVD_CODING
  return besterr;
}

int av1_joint_amvd_motion_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                                 SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                 const MV *start_mv, MV *bestmv,
                                 int *distortion, unsigned int *sse1,
                                 int ref_idx, MV *other_mv, MV *best_other_mv,
                                 uint16_t *second_pred,
                                 InterPredParams *inter_pred_params) {
#if !CONFIG_VQ_MVD_CODING
  const int allow_hp_mvd = 0;
  const int forced_stop = ms_params->forced_stop;
#endif  // !CONFIG_VQ_MVD_CODING
  // const int iters_per_step = ms_params->iters_per_step;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SUBPEL_SEARCH_TYPE subpel_search_type =
      ms_params->var_params.subpel_search_type;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  MB_MODE_INFO *const mbmi = xd->mi[0];
  // perform prediction for second MV
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];

#if BUGFIX_AMVD_AMVR
  set_amvd_mv_precision(mbmi, mbmi->max_mv_precision);
#else
  assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif

#if !CONFIG_VQ_MVD_CODING
  // How many steps to take. A round of 0 means fullpel search only, 1 means
  // half-pel, and so on.
  int round = AOMMIN(FULL_PEL - forced_stop, 3 - !allow_hp_mvd);
  if (cm->features.cur_frame_force_integer_mv) round = 0;
  int hstep = 8 >> round;  // Step size, initialized to 4/8=1/2 pel
#endif                     //! CONFIG_VQ_MVD_CODING
  unsigned int besterr = INT_MAX;

#if CONFIG_VQ_MVD_CODING
  MV this_mvd;
  get_adaptive_mvd_from_ref_mv(*start_mv, *mv_cost_params->ref_mv, &this_mvd);
  if (is_valid_amvd_mvd(this_mvd)) {
#endif  // CONFIG_VQ_MVD_CODING

    *bestmv = *start_mv;
    *best_other_mv = *other_mv;

    if (subpel_search_type != FILTER_UNUSED) {
      besterr = upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                             mv_cost_params, sse1, distortion);
    } else {
      besterr = setup_center_error(xd, bestmv, var_params, mv_cost_params, sse1,
                                   distortion);
    }
#if CONFIG_VQ_MVD_CODING
  }
#endif  // CONFIG_VQ_MVD_CODING

  MV iter_center_mv = *start_mv;
  const int cand_pos[4][2] = {
    { 0, -1 },  // left
    { 0, +1 },  // right
    { -1, 0 },  // above
    { +1, 0 }   // down
  };

  const int same_side = is_ref_frame_same_side(cm, mbmi);

  const int cur_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[ref_idx]];
  int other_ref_dist =
      cm->ref_frame_relative_dist[mbmi->ref_frame[1 - ref_idx]];

  other_ref_dist = same_side ? other_ref_dist : -other_ref_dist;

#if CONFIG_VQ_MVD_CODING
  for (int curr_index = 1; curr_index <= MAX_AMVD_INDEX; curr_index++) {
    int dummy = 0;
    MV candidate_mv[2];
    // loop left, right, above, bottom directions
    for (int i = 0; i < 4; ++i) {
      const MV cur_mvd_idx = { cand_pos[i][0] * curr_index,
                               cand_pos[i][1] * curr_index };
      //  printf("curr_index = %d row = %d col = %d \n ", curr_index
      //  ,cur_mvd_idx.row, cur_mvd_idx.col);
      const MV cur_mvd = { get_mvd_from_amvd_index(cur_mvd_idx.row),
                           get_mvd_from_amvd_index(cur_mvd_idx.col) };
      assert(is_valid_amvd_mvd(cur_mvd));

      MV other_mvd = { 0, 0 };
      candidate_mv[0].row = iter_center_mv.row + cur_mvd.row;
      candidate_mv[0].col = iter_center_mv.col + cur_mvd.col;

      get_mv_projection(&other_mvd, cur_mvd, other_ref_dist, cur_ref_dist);
      scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);
#if !CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&other_mvd,
#if BUGFIX_AMVD_AMVR
                         cm->features.fr_mv_precision);
#else
                         mbmi->pb_mv_precision);
#endif
#endif  // !CONFIG_C071_SUBBLK_WARPMV

      candidate_mv[1].row = (int)(other_mv->row + other_mvd.row);
      candidate_mv[1].col = (int)(other_mv->col + other_mvd.col);
      if (av1_is_subpelmv_in_range(mv_limits, candidate_mv[1]) == 0) continue;
      av1_enc_build_one_inter_predictor(second_pred, block_size_wide[bsize],
                                        &candidate_mv[1], inter_pred_params);
      check_better(xd, cm, &candidate_mv[0], bestmv, mv_limits, var_params,
                   mv_cost_params, &besterr, sse1, distortion, &dummy);

      // best mv on the other reference list
      if (bestmv->row == candidate_mv[0].row &&
          bestmv->col == candidate_mv[0].col) {
        best_other_mv->row = candidate_mv[1].row;
        best_other_mv->col = candidate_mv[1].col;
      }
    }

    int abs_mvd_from_idx = get_mvd_from_amvd_index(curr_index);

    if (abs_mvd_from_idx >= 16) {
      if (abs(bestmv->row - start_mv->row) <= abs_mvd_from_idx / 4 &&
          abs(bestmv->col - start_mv->col) <= abs_mvd_from_idx / 4)
        break;
    }
  }
#else
  for (int iter = hstep; iter <= 256;) {
    int dummy = 0;
    MV candidate_mv[2];
    // loop left, right, above, bottom directions
    for (int i = 0; i < 4; ++i) {
      const MV cur_mvd = { cand_pos[i][0] * iter, cand_pos[i][1] * iter };
      MV other_mvd = { 0, 0 };
      candidate_mv[0].row = iter_center_mv.row + cur_mvd.row;
      candidate_mv[0].col = iter_center_mv.col + cur_mvd.col;

      get_mv_projection(&other_mvd, cur_mvd, other_ref_dist, cur_ref_dist);
      scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);
#if !CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&other_mvd,
#if BUGFIX_AMVD_AMVR
                         cm->features.fr_mv_precision);
#else
                         mbmi->pb_mv_precision);
#endif
#endif  // !CONFIG_C071_SUBBLK_WARPMV

      candidate_mv[1].row = (int)(other_mv->row + other_mvd.row);
      candidate_mv[1].col = (int)(other_mv->col + other_mvd.col);
      if (av1_is_subpelmv_in_range(mv_limits, candidate_mv[1]) == 0) continue;
      av1_enc_build_one_inter_predictor(second_pred, block_size_wide[bsize],
                                        &candidate_mv[1], inter_pred_params);
      check_better(xd, cm, &candidate_mv[0], bestmv, mv_limits, var_params,
                   mv_cost_params, &besterr, sse1, distortion, &dummy);

      // best mv on the other reference list
      if (bestmv->row == candidate_mv[0].row &&
          bestmv->col == candidate_mv[0].col) {
        best_other_mv->row = candidate_mv[1].row;
        best_other_mv->col = candidate_mv[1].col;
      }
    }
    // fast encoder method to early terminate the MV search
    // after searching [-2,+2] MV samples, if the best MV is still within
    // [-1/2,1/2], stop the MV search
    if (iter >= 16) {
      if (abs(bestmv->row - start_mv->row) <= iter / 4 &&
          abs(bestmv->col - start_mv->col) <= iter / 4)
        break;
    }

    // allow integer and fractional MVD in (0,1]
    if (iter < 8) iter += hstep;
    // only allow integer MVD in (1,2]
    else if (iter < 16)
      iter += 8;
    // only allow 4 integer MVD in (2,4]
    else if (iter < 32)
      iter += 16;
    // only allow 8 sample integer MVD in (4,8]
    else if (iter < 64)
      iter += 32;
    // only allow 16 sample integer MVD in (8,16]
    else if (iter < 128)
      iter += 64;
    // only allow 32 sample integer MVD in (16,32]
    else
      iter += 128;
  }
#endif  // CONFIG_VQ_MVD_CODING

  return besterr;
}

int av1_find_best_sub_pixel_tree_pruned_evenmore(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, int_mv *last_mv_search_list) {
  (void)cm;
  const int iters_per_step = ms_params->iters_per_step;
  const int *cost_list = ms_params->cost_list;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  const int forced_stop =
      (pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMAX(ms_params->forced_stop,
                   MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)
          : FULL_PEL;

  // The iteration we are current searching for. Iter 0 corresponds to fullpel
  // mv, iter 1 to half pel, and so on
  int iter = 0;
  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel
  unsigned int besterr = INT_MAX;
  *bestmv = start_mv;
  const struct scale_factors *const sf =
      is_intrabc_block(xd->mi[0], xd->tree_type)
          ? &cm->sf_identity
          : xd->block_ref_scale_factors[0];
  const int is_scaled = av1_is_scaled(sf);
  besterr = setup_center_error_facade(
      xd, cm, bestmv, var_params, mv_cost_params, sse1, distortion, is_scaled);

  // If forced_stop is FULL_PEL, return.
  if (forced_stop == FULL_PEL) return besterr;

  if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
    return INT_MAX;
  }
  iter++;

  if (cost_list && cost_list[0] != INT_MAX && cost_list[1] != INT_MAX &&
      cost_list[2] != INT_MAX && cost_list[3] != INT_MAX &&
      cost_list[4] != INT_MAX && is_cost_list_wellbehaved(cost_list)) {
    int ir, ic;
    int dummy = 0;
    get_cost_surf_min(cost_list, &ir, &ic, 2);
    if (ir != 0 || ic != 0) {
      const MV this_mv = { start_mv.row + 2 * ir, start_mv.col + 2 * ic };
      check_better_fast(xd, cm, &this_mv, bestmv, mv_limits, var_params,
                        mv_cost_params, &besterr, sse1, distortion, &dummy,
                        is_scaled);
    }
  } else {
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);

    // Each subsequent iteration checks at least one point in common with
    // the last iteration could be 2 ( if diag selected) 1/4 pel
    if (forced_stop < HALF_PEL) {
      if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
        return INT_MAX;
      }
      iter++;

      hstep >>= 1;
      start_mv = *bestmv;
      two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                            var_params, mv_cost_params, &besterr, sse1,
                            distortion, iters_per_step, is_scaled);
    }
  }

  if (forced_stop == EIGHTH_PEL) {
    if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
      return INT_MAX;
    }
    iter++;

    hstep >>= 1;
    start_mv = *bestmv;
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  return besterr;
}

int av1_find_best_sub_pixel_tree_pruned_more(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, int_mv *last_mv_search_list) {
  (void)cm;
  const int iters_per_step = ms_params->iters_per_step;
  const int *cost_list = ms_params->cost_list;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  const int forced_stop =
      (pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMAX(ms_params->forced_stop,
                   MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)
          : FULL_PEL;

  // The iteration we are current searching for. Iter 0 corresponds to fullpel
  // mv, iter 1 to half pel, and so on
  int iter = 0;
  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel
  unsigned int besterr = INT_MAX;
  *bestmv = start_mv;
  const struct scale_factors *const sf =
      is_intrabc_block(xd->mi[0], xd->tree_type)
          ? &cm->sf_identity
          : xd->block_ref_scale_factors[0];
  const int is_scaled = av1_is_scaled(sf);
  besterr = setup_center_error_facade(
      xd, cm, bestmv, var_params, mv_cost_params, sse1, distortion, is_scaled);

  // If forced_stop is FULL_PEL, return.
  if (forced_stop == FULL_PEL) return besterr;

  if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
    return INT_MAX;
  }
  iter++;

  if (cost_list && cost_list[0] != INT_MAX && cost_list[1] != INT_MAX &&
      cost_list[2] != INT_MAX && cost_list[3] != INT_MAX &&
      cost_list[4] != INT_MAX && is_cost_list_wellbehaved(cost_list)) {
    int ir, ic;
    get_cost_surf_min(cost_list, &ir, &ic, 1);
    if (ir != 0 || ic != 0) {
      const MV this_mv = { start_mv.row + ir * hstep,
                           start_mv.col + ic * hstep };
      int dummy = 0;
      check_better_fast(xd, cm, &this_mv, bestmv, mv_limits, var_params,
                        mv_cost_params, &besterr, sse1, distortion, &dummy,
                        is_scaled);
    }
  } else {
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  // Each subsequent iteration checks at least one point in common with
  // the last iteration could be 2 ( if diag selected) 1/4 pel
  if (forced_stop < HALF_PEL) {
    if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
      return INT_MAX;
    }
    iter++;

    hstep >>= 1;
    start_mv = *bestmv;
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }
  if (forced_stop == EIGHTH_PEL) {
    if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
      return INT_MAX;
    }
    iter++;

    hstep >>= 1;
    start_mv = *bestmv;
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  return besterr;
}

int av1_find_best_sub_pixel_tree_pruned(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, int_mv *last_mv_search_list) {
  (void)cm;
  const int iters_per_step = ms_params->iters_per_step;
  const int *cost_list = ms_params->cost_list;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  const int forced_stop =
      (pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMAX(ms_params->forced_stop,
                   MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)
          : FULL_PEL;

  // The iteration we are current searching for. Iter 0 corresponds to fullpel
  // mv, iter 1 to half pel, and so on
  int iter = 0;
  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel
  unsigned int besterr = INT_MAX;
  *bestmv = start_mv;
  const struct scale_factors *const sf =
      is_intrabc_block(xd->mi[0], xd->tree_type)
          ? &cm->sf_identity
          : xd->block_ref_scale_factors[0];
  const int is_scaled = av1_is_scaled(sf);
  besterr = setup_center_error_facade(
      xd, cm, bestmv, var_params, mv_cost_params, sse1, distortion, is_scaled);

  // If forced_stop is FULL_PEL, return.
  if (forced_stop == FULL_PEL) return besterr;

  if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
    return INT_MAX;
  }
  iter++;

  if (cost_list && cost_list[0] != INT_MAX && cost_list[1] != INT_MAX &&
      cost_list[2] != INT_MAX && cost_list[3] != INT_MAX &&
      cost_list[4] != INT_MAX) {
    const unsigned int whichdir = (cost_list[1] < cost_list[3] ? 0 : 1) +
                                  (cost_list[2] < cost_list[4] ? 0 : 2);

    const MV left_mv = { start_mv.row, start_mv.col - hstep };
    const MV right_mv = { start_mv.row, start_mv.col + hstep };
    const MV bottom_mv = { start_mv.row + hstep, start_mv.col };
    const MV top_mv = { start_mv.row - hstep, start_mv.col };

    const MV bottom_left_mv = { start_mv.row + hstep, start_mv.col - hstep };
    const MV bottom_right_mv = { start_mv.row + hstep, start_mv.col + hstep };
    const MV top_left_mv = { start_mv.row - hstep, start_mv.col - hstep };
    const MV top_right_mv = { start_mv.row - hstep, start_mv.col + hstep };

    int dummy = 0;

    switch (whichdir) {
      case 0:  // bottom left quadrant
        check_better_fast(xd, cm, &left_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &bottom_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &bottom_left_mv, bestmv, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, &dummy, is_scaled);
        break;
      case 1:  // bottom right quadrant
        check_better_fast(xd, cm, &right_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &bottom_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &bottom_right_mv, bestmv, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, &dummy, is_scaled);
        break;
      case 2:  // top left quadrant
        check_better_fast(xd, cm, &left_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &top_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &top_left_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        break;
      case 3:  // top right quadrant
        check_better_fast(xd, cm, &right_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &top_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        check_better_fast(xd, cm, &top_right_mv, bestmv, mv_limits, var_params,
                          mv_cost_params, &besterr, sse1, distortion, &dummy,
                          is_scaled);
        break;
    }
  } else {
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  // Each subsequent iteration checks at least one point in common with
  // the last iteration could be 2 ( if diag selected) 1/4 pel
  if (forced_stop < HALF_PEL) {
    if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
      return INT_MAX;
    }
    iter++;

    hstep >>= 1;
    start_mv = *bestmv;
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  if (forced_stop == EIGHTH_PEL) {
    if (check_repeated_mv_and_update(last_mv_search_list, *bestmv, iter)) {
      return INT_MAX;
    }
    iter++;

    hstep >>= 1;
    start_mv = *bestmv;
    two_level_checks_fast(xd, cm, start_mv, bestmv, hstep, mv_limits,
                          var_params, mv_cost_params, &besterr, sse1,
                          distortion, iters_per_step, is_scaled);
  }

  return besterr;
}

int av1_find_best_sub_pixel_tree(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                 const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                 MV start_mv, MV *bestmv, int *distortion,
                                 unsigned int *sse1,
                                 int_mv *last_mv_search_list) {
  const int forced_stop = ms_params->forced_stop;
  const int iters_per_step = ms_params->iters_per_step;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  // How many steps to take. A round of 0 means fullpel search only, 1 means
  // half-pel, and so on.

  const int round =
      (mv_cost_params->pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMIN(FULL_PEL - forced_stop,
                   mv_cost_params->pb_mv_precision - MV_PRECISION_ONE_PEL)
          : 0;

  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel

  unsigned int besterr = INT_MAX;

  *bestmv = start_mv;
  besterr = upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                         mv_cost_params, sse1, distortion);

  // If forced_stop is FULL_PEL, return.
  if (!round) return besterr;

  for (int iter = 0; iter < round; ++iter) {
    MV iter_center_mv = *bestmv;
    if (check_repeated_mv_and_update(last_mv_search_list, iter_center_mv,
                                     iter)) {
      return INT_MAX;
    }

    MV diag_step;
    diag_step = first_level_check(xd, cm, iter_center_mv, bestmv, hstep,
                                  mv_limits, var_params, mv_cost_params,
                                  &besterr, sse1, distortion);

    // Check diagonal sub-pixel position
    if (!CHECK_MV_EQUAL(iter_center_mv, *bestmv) && iters_per_step > 1) {
      second_level_check_v2(xd, cm, iter_center_mv, diag_step, bestmv,
                            mv_limits, var_params, mv_cost_params, &besterr,
                            sse1, distortion);
    }

    hstep >>= 1;
  }

  return besterr;
}

#if CONFIG_IBC_SUBPEL_PRECISION
// Check the diagonal MVS at the second level of search
static AOM_FORCE_INLINE void second_level_check_v2_intraBC(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV iter_center_mv,
    MV diag_step, MV *best_mv, const SubpelMvLimits *sub_pel_mv_limits,
    FullMvLimits *full_pel_mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, BLOCK_SIZE bsize) {
  assert(best_mv->row == iter_center_mv.row + diag_step.row ||
         best_mv->col == iter_center_mv.col + diag_step.col);
  if (CHECK_MV_EQUAL(iter_center_mv, *best_mv)) {
    return;
  } else if (iter_center_mv.row == best_mv->row) {
    // Search away from diagonal step since diagonal search did not provide any
    // improvement
    diag_step.row *= -1;
  } else if (iter_center_mv.col == best_mv->col) {
    diag_step.col *= -1;
  }

  const MV row_bias_mv = { best_mv->row + diag_step.row, best_mv->col };
  const MV col_bias_mv = { best_mv->row, best_mv->col + diag_step.col };
  const MV diag_bias_mv = { best_mv->row + diag_step.row,
                            best_mv->col + diag_step.col };
  int has_better_mv = 0;

  if (is_sub_pel_bv_valid(row_bias_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                          sub_pel_mv_limits, full_pel_mv_limits,
                          mv_cost_params->pb_mv_precision)) {
    check_better(xd, cm, &row_bias_mv, best_mv, sub_pel_mv_limits, var_params,
                 mv_cost_params, besterr, sse1, distortion, &has_better_mv);
  }

  if (is_sub_pel_bv_valid(col_bias_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                          sub_pel_mv_limits, full_pel_mv_limits,
                          mv_cost_params->pb_mv_precision)) {
    check_better(xd, cm, &col_bias_mv, best_mv, sub_pel_mv_limits, var_params,
                 mv_cost_params, besterr, sse1, distortion, &has_better_mv);
  }

  // Do an additional search if the second iteration gives a better mv
  if (has_better_mv) {
    if (is_sub_pel_bv_valid(diag_bias_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                            sub_pel_mv_limits, full_pel_mv_limits,
                            mv_cost_params->pb_mv_precision)) {
      check_better(xd, cm, &diag_bias_mv, best_mv, sub_pel_mv_limits,
                   var_params, mv_cost_params, besterr, sse1, distortion,
                   &has_better_mv);
    }
  }
}

// Search around best integer pixel position
int av1_find_best_sub_pixel_intraBC_dv(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, FullMvLimits *full_pel_mv_limits,
    BLOCK_SIZE bsize) {
  const int forced_stop = ms_params->forced_stop;
  const int iters_per_step = ms_params->iters_per_step;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *sub_pel_mv_limits = &ms_params->mv_limits;
  assert(mv_cost_params->is_ibc_cost);
  assert(is_this_mv_precision_compliant(start_mv,
                                        mv_cost_params->pb_mv_precision));
  assert(is_sub_pel_bv_valid(start_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                             sub_pel_mv_limits, full_pel_mv_limits,
                             mv_cost_params->pb_mv_precision));
  assert(av1_is_fullmv_in_range(full_pel_mv_limits,
                                get_fullmv_from_mv(&start_mv),
                                mv_cost_params->pb_mv_precision));

  // How many steps to take. A round of 0 means fullpel search only, 1 means
  // half-pel, and so on.

  const int round =
      (mv_cost_params->pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMIN(FULL_PEL - forced_stop,
                   mv_cost_params->pb_mv_precision - MV_PRECISION_ONE_PEL)
          : 0;

  int hstep = INIT_SUBPEL_STEP_SIZE;  // Step size, initialized to 4/8=1/2 pel

  unsigned int besterr = INT_MAX;

  *bestmv = start_mv;
  besterr = upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                         mv_cost_params, sse1, distortion);

  // printf(" subpel_search_type = %d \n", var_params->subpel_search_type);

  // If forced_stop is FULL_PEL, return.
  if (!round) return besterr;

  for (int iter = 0; iter < round; ++iter) {
    MV iter_center_mv = *bestmv;

    int dummy = 0;
    unsigned int costs[4] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };
    const MV neighbor_mvs[4] = {
      { iter_center_mv.row, iter_center_mv.col - hstep },  // left
      { iter_center_mv.row, iter_center_mv.col + hstep },  // right
      { iter_center_mv.row - hstep, iter_center_mv.col },  // top
      { iter_center_mv.row + hstep, iter_center_mv.col }   // bottom
    };

    for (int k = 0; k < 4; k++) {
      if (is_sub_pel_bv_valid(neighbor_mvs[k], cm, xd, xd->mi_row, xd->mi_col,
                              bsize, sub_pel_mv_limits, full_pel_mv_limits,
                              mv_cost_params->pb_mv_precision)) {
        costs[k] = check_better(xd, cm, &neighbor_mvs[k], bestmv,
                                sub_pel_mv_limits, var_params, mv_cost_params,
                                &besterr, sse1, distortion, &dummy);
      }
    }

    const MV diag_step =
        get_best_diag_step(hstep, costs[0], costs[1], costs[2], costs[3]);
    const MV diag_mv = { iter_center_mv.row + diag_step.row,
                         iter_center_mv.col + diag_step.col };

    // Check the diagonal direction with the best mv
    if (is_sub_pel_bv_valid(diag_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                            sub_pel_mv_limits, full_pel_mv_limits,
                            mv_cost_params->pb_mv_precision)) {
      check_better(xd, cm, &diag_mv, bestmv, sub_pel_mv_limits, var_params,
                   mv_cost_params, &besterr, sse1, distortion, &dummy);
    }

    // Check diagonal sub-pixel position
    if (!CHECK_MV_EQUAL(iter_center_mv, *bestmv) && iters_per_step > 1) {
      second_level_check_v2_intraBC(xd, cm, iter_center_mv, diag_step, bestmv,
                                    sub_pel_mv_limits, full_pel_mv_limits,
                                    var_params, mv_cost_params, &besterr, sse1,
                                    distortion, bsize);
    }
    hstep >>= 1;
  }

  return besterr;
}

#if CONFIG_IBC_SUBPEL_PRECISION
int av1_refine_low_precision_intraBC_dv(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, FullMvLimits *full_pel_mv_limits,
    BLOCK_SIZE bsize) {
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *sub_pel_mv_limits = &ms_params->mv_limits;
  assert(mv_cost_params->is_ibc_cost);
  assert(is_this_mv_precision_compliant(start_mv,
                                        mv_cost_params->pb_mv_precision));

  const int hstep =
      (1 << (MV_PRECISION_ONE_EIGHTH_PEL - mv_cost_params->pb_mv_precision));
  unsigned int besterr = INT_MAX;

  *bestmv = start_mv;
  if (is_sub_pel_bv_valid(start_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                          sub_pel_mv_limits, full_pel_mv_limits,
                          mv_cost_params->pb_mv_precision)) {
    besterr = upsampled_setup_center_error(xd, cm, bestmv, var_params,
                                           mv_cost_params, sse1, distortion);
  }

  MV iter_center_mv = *bestmv;
  int num_of_search_steps = 3;
  for (int step = 0; step < num_of_search_steps; step++) {
    int best_site = -1;

    MV neighbor_mvs[8] = {
      { iter_center_mv.row, iter_center_mv.col - hstep },  // left
      { iter_center_mv.row, iter_center_mv.col + hstep },  // right
      { iter_center_mv.row - hstep, iter_center_mv.col },  // top
      { iter_center_mv.row + hstep, iter_center_mv.col },  // bottom
      { iter_center_mv.row + hstep, iter_center_mv.col - hstep },
      { iter_center_mv.row + hstep, iter_center_mv.col + hstep },
      { iter_center_mv.row - hstep, iter_center_mv.col - hstep },
      { iter_center_mv.row - hstep, iter_center_mv.col + hstep }
    };

    for (int k = 0; k < 8; k++) {
      MV this_mv = neighbor_mvs[k];
      assert(is_this_mv_precision_compliant(this_mv,
                                            mv_cost_params->pb_mv_precision));

      if (is_sub_pel_bv_valid(this_mv, cm, xd, xd->mi_row, xd->mi_col, bsize,
                              sub_pel_mv_limits, full_pel_mv_limits,
                              mv_cost_params->pb_mv_precision)) {
        unsigned int sse;
        unsigned int this_cost =
            upsampled_pref_error(xd, cm, &this_mv, var_params, &sse);
        this_cost += mv_err_cost(this_mv, mv_cost_params);
        if (this_cost < besterr) {
          besterr = this_cost;
          *bestmv = this_mv;
          best_site = k;
        }
      }
    }

    if (best_site == -1) {
      break;
    } else {
      iter_center_mv.row = bestmv->row;
      iter_center_mv.col = bestmv->col;
      assert(bestmv->row == neighbor_mvs[best_site].row);
      assert(bestmv->col == neighbor_mvs[best_site].col);
    }
  }

  return besterr;
}
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#endif  // CONFIG_IBC_SUBPEL_PRECISION

// Note(yunqingwang): The following 2 functions are only used in the motion
// vector unit test, which return extreme motion vectors allowed by the MV
// limits.
// Returns the maximum MV.
int av1_return_max_sub_pixel_mv(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                MV start_mv, MV *bestmv, int *distortion,
                                unsigned int *sse1,
                                int_mv *last_mv_search_list) {
  (void)xd;
  (void)cm;
  (void)start_mv;
  (void)sse1;
  (void)distortion;
  (void)last_mv_search_list;

  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

#if CONFIG_C071_SUBBLK_WARPMV
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(start_mv, &sub_mv_offset, pb_mv_precision);
  bestmv->row = mv_limits->row_max;
  bestmv->col = mv_limits->col_max;
  MV diff = { 0, 0 };
  diff.row = bestmv->row - start_mv.row;
  diff.col = bestmv->col - start_mv.col;
  bool check_row =
      diff.row & ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  bool check_col =
      diff.col & ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  if (check_row) {
    bestmv->row -= abs(sub_mv_offset.row);
  }
  if (check_col) {
    bestmv->col -= abs(sub_mv_offset.col);
  }
#else
  bestmv->row = mv_limits->row_max;
  bestmv->col = mv_limits->col_max;
#endif

  unsigned int besterr = 0;

  return besterr;
}

// Returns the minimum MV.
int av1_return_min_sub_pixel_mv(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                MV start_mv, MV *bestmv, int *distortion,
                                unsigned int *sse1,
                                int_mv *last_mv_search_list) {
  (void)xd;
  (void)cm;
  (void)start_mv;
  (void)sse1;
  (void)distortion;
  (void)last_mv_search_list;

  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

#if CONFIG_C071_SUBBLK_WARPMV
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const MvSubpelPrecision pb_mv_precision = mv_cost_params->pb_mv_precision;
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(start_mv, &sub_mv_offset, pb_mv_precision);
  bestmv->row = mv_limits->row_min;
  bestmv->col = mv_limits->col_min;
  MV diff = { 0, 0 };
  diff.row = bestmv->row - start_mv.row;
  diff.col = bestmv->col - start_mv.col;
  bool check_row =
      diff.row & ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  bool check_col =
      diff.col & ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  if (check_row) {
    bestmv->row += abs(sub_mv_offset.row);
  }
  if (check_col) {
    bestmv->col += abs(sub_mv_offset.col);
  }
#else
  bestmv->row = mv_limits->row_min;
  bestmv->col = mv_limits->col_min;
#endif

  return 0;
}

// Computes the cost of the current predictor by going through the whole
// av1_enc_build_inter_predictor pipeline. This is mainly used by warped mv
// during motion_mode_rd. We are going through the whole
// av1_enc_build_inter_predictor because we might have changed the interpolation
// filter, etc before motion_mode_rd is called.
static INLINE unsigned int compute_motion_cost(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, BLOCK_SIZE bsize,
    const MV *this_mv) {
  unsigned int mse;
  unsigned int sse;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  set_default_interp_filters(xd->mi[0], cm,
#if CONFIG_COMPOUND_4XN
                             xd,
#endif  // CONFIG_COMPOUND_4XN
                             cm->features.interp_filter);
  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                AOM_PLANE_Y, AOM_PLANE_Y);

  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const MSBuffers *ms_buffers = &var_params->ms_buffers;

  const uint16_t *const src = ms_buffers->src->buf;
  const int src_stride = ms_buffers->src->stride;
  const uint16_t *const dst = xd->plane[0].dst.buf;
  const int dst_stride = xd->plane[0].dst.stride;
  const aom_variance_fn_ptr_t *vfp = ms_params->var_params.vfp;

  mse = vfp->vf(dst, dst_stride, src, src_stride, &sse);
  mse += mv_err_cost(*this_mv, &ms_params->mv_cost_params);
  return mse;
}

// Macros to build bitmasks which help us avoid redundant computations
// during warp refinement (av1_refine_warped_mv and
// av1_refine_mv_for_warp_extend)
//
// To explain the idea here, imagine that on the first iteration of the
// loop below, we step rightwards. Then, on the second iteration, the neighbors
// to consider are:
//     . . .
//     0 1 .
//     . . .
// Where 0 is the initial search point, 1 is the best candidate found in the
// first iteration, and the dots are the other neighbors of point 1.
//
// Naively, we would now need to scan all 8 neighbors of point 1 (point 0 and
// the seven points marked with dots), and compare them to see where to move
// next. However, we already evaluated 5 of those 8 neighbors in the last
// iteration, and decided that they are worse than point 1. So we don't need
// to re-consider these points. We only really need to consider the three
// points which are adjacent to point 1 but *not* to point 0.
//
// As the algorithm goes on, there are other ways that redundant evaluations
// can happen, if the search path curls back around on itself.
//
// To avoid all possible redundancies, we'd have to build a set containing
// every point we have already checked, and this be quite expensive.
//
// So instead, we apply a 95%-effective solution with a much lower overhead:
// we prune out the points which were considered during the previous
// iteration, but we don't worry about any prior iteration. This can be done
// as follows:
//
// We build a static table, called neighbor_mask, which answers the question
// "if we moved in direction X last time, which neighbors are new, and which
//  were scanned last iteration?"
// Then we can query this table to quickly determine which points we need to
// evaluate, and which we can skip.
//
// To query the table, the logic is simply:
// neighbor_mask[i] & (1 << j) == "if we moved in direction i last iteration,
//                             do we need to scan neighbor j this iteration?"
#define NEIGHBOR_MASK4(a, b, c, d) (a | (b << 1) | (c << 2) | (d << 3))

#define NEIGHBOR_MASK8(a, b, c, d, e, f, g, h)                           \
  (a | (b << 1) | (c << 2) | (d << 3) | (e << 4) | (f << 5) | (g << 6) | \
   (h << 7))

static const warp_search_config warp_search_info[WARP_SEARCH_METHODS] = {
  // WARP_SEARCH_DIAMOND
  {
    .num_neighbors = 4,
    .neighbors = { { 0, -1 }, { 1, 0 }, { 0, 1 },   { -1, 0 } },
    .neighbor_mask = {
      // If we stepped left last time, consider all points except right
      NEIGHBOR_MASK4(1, 1, 0, 1),
      // If we stepped down last time, consider all points except up
      NEIGHBOR_MASK4(1, 1, 1, 0),
      // Stepped right last time
      NEIGHBOR_MASK4(0, 1, 1, 1),
      // Stepped up last time
      NEIGHBOR_MASK4(1, 0, 1, 1),
    },
  },
  // WARP_SEARCH_SQUARE
  {
    .num_neighbors = 8,
    .neighbors = { { 0, -1 }, { 1, 0 }, { 0, 1 },   { -1, 0 },
               { 1, -1 }, { 1, 1 }, { -1, -1 }, { -1, 1 } },
    .neighbor_mask = {
      // If we stepped left last time, then we only need to consider 3 points:
      // left, up+left, down+left
      NEIGHBOR_MASK8(1, 0, 0, 0, 1, 0, 1, 0),
      // If we stepped down last time, then we only need to consider 3 points:
      // down, down+left, down+right
      NEIGHBOR_MASK8(0, 1, 0, 0, 1, 1, 0, 0),
      // Stepped right last time
      NEIGHBOR_MASK8(0, 0, 1, 0, 0, 1, 0, 1),
      // Stepped up last time
      NEIGHBOR_MASK8(0, 0, 0, 1, 0, 0, 1, 1),

      // If we stepped down+left last time, then we need to consider 5 points:
      // down+left, left, up+left, down, down+right
      NEIGHBOR_MASK8(1, 1, 0, 0, 1, 1, 1, 0),
      // Stepped down+right last time
      NEIGHBOR_MASK8(0, 1, 1, 0, 1, 1, 0, 1),
      // Stepped up+left last time
      NEIGHBOR_MASK8(1, 0, 0, 1, 1, 0, 1, 1),
      // Stepped up+right last time
      NEIGHBOR_MASK8(0, 0, 1, 1, 0, 1, 1, 1),
    },
  },
};

// Refines MV in a small range
unsigned int av1_refine_warped_mv(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                  const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                  BLOCK_SIZE bsize, const int *pts0,
                                  const int *pts_inref0, int total_samples,
#if CONFIG_COMPOUND_WARP_CAUSAL
                                  int8_t ref,
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
                                  WARP_SEARCH_METHOD search_method,
                                  int num_iterations) {
  MB_MODE_INFO *mbmi = xd->mi[0];

  const MV *neighbors = warp_search_info[search_method].neighbors;
  const int num_neighbors = warp_search_info[search_method].num_neighbors;
  const uint8_t *neighbor_mask = warp_search_info[search_method].neighbor_mask;

#if CONFIG_COMPOUND_WARP_CAUSAL
  MV *best_mv = &mbmi->mv[ref].as_mv;
  WarpedMotionParams best_wm_params = mbmi->wm_params[ref];
  int best_num_proj_ref = mbmi->num_proj_ref[ref];
#else   // CONFIG_COMPOUND_WARP_CAUSAL
  MV *best_mv = &mbmi->mv[0].as_mv;
  WarpedMotionParams best_wm_params = mbmi->wm_params[0];
  int best_num_proj_ref = mbmi->num_proj_ref;
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  unsigned int bestmse;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  const int mv_shift =
      (MV_PRECISION_ONE_EIGHTH_PEL - ms_params->mv_cost_params.pb_mv_precision);

  // Calculate the center position's error
  assert(av1_is_subpelmv_in_range(mv_limits, *best_mv));
  bestmse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);

  // MV search
  int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // First iteration always scans all neighbors
  uint8_t valid_neighbors = UINT8_MAX;

  for (int ite = 0; ite < num_iterations; ++ite) {
    int best_idx = -1;

    for (int idx = 0; idx < num_neighbors; ++idx) {
      if ((valid_neighbors & (1 << idx)) == 0) {
        continue;
      }

      unsigned int thismse;

      MV this_mv = { best_mv->row + neighbors[idx].row * (1 << mv_shift),
                     best_mv->col + neighbors[idx].col * (1 << mv_shift) };
      if (av1_is_subpelmv_in_range(mv_limits, this_mv)) {
        memcpy(pts, pts0, total_samples * 2 * sizeof(*pts0));
        memcpy(pts_inref, pts_inref0, total_samples * 2 * sizeof(*pts_inref0));
        if (total_samples > 1)
#if CONFIG_COMPOUND_WARP_CAUSAL
          mbmi->num_proj_ref[ref] =
#else
          mbmi->num_proj_ref =
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
              av1_selectSamples(&this_mv, pts, pts_inref, total_samples, bsize);

#if CONFIG_COMPOUND_WARP_CAUSAL
        if (!av1_find_projection(mbmi->num_proj_ref[ref], pts, pts_inref, bsize,
                                 this_mv, &mbmi->wm_params[ref], mi_row,
#else
        if (!av1_find_projection(mbmi->num_proj_ref, pts, pts_inref, bsize,
                                 this_mv, &mbmi->wm_params[0], mi_row,
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
                                 mi_col)) {
          thismse = compute_motion_cost(xd, cm, ms_params, bsize, &this_mv);

          if (thismse < bestmse) {
            best_idx = idx;
#if CONFIG_COMPOUND_WARP_CAUSAL
            best_wm_params = mbmi->wm_params[ref];
            best_num_proj_ref = mbmi->num_proj_ref[ref];
#else
            best_wm_params = mbmi->wm_params[0];
            best_num_proj_ref = mbmi->num_proj_ref;
#endif
            bestmse = thismse;
          }
        }
      }
    }

    if (best_idx == -1) break;

    if (best_idx >= 0) {
      best_mv->row += neighbors[best_idx].row * (1 << mv_shift);
      best_mv->col += neighbors[best_idx].col * (1 << mv_shift);
      valid_neighbors = neighbor_mask[best_idx];
    }
  }

#if CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->wm_params[ref] = best_wm_params;
  mbmi->num_proj_ref[ref] = best_num_proj_ref;
#else
  mbmi->wm_params[0] = best_wm_params;
  mbmi->num_proj_ref = best_num_proj_ref;
#endif
  return bestmse;
}

#if CONFIG_DERIVED_MVD_SIGN
// This function check if the MV adjust is required  for MVD sign derivation
uint8_t need_mv_adjustment(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                           MACROBLOCK *const x, MB_MODE_INFO *mbmi,
                           BLOCK_SIZE bsize, MV *mv_diffs, MV *ref_mvs,
                           MvSubpelPrecision pb_mv_precision,
                           int *num_signaled_mvd, int *start_signaled_mvd_idx,
                           int *num_nonzero_mvd) {
  uint16_t sum_mvd = 0;
  int last_ref = 0;
  int last_comp = 0;
  int num_nonzero_mvd_comp = 0;
  int precision_shift = MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision;
  int th_for_num_nonzero = get_derive_sign_nzero_th(mbmi);
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);

  if (num_signaled_mvd) *num_signaled_mvd = 0;
  if (start_signaled_mvd_idx) *start_signaled_mvd_idx = 0;

  if (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag) {
    WarpedMotionParams ref_warp_model =
        x->mbmi_ext
            ->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
                              [mbmi->warp_ref_idx]
            .wm_params;
    int_mv ref_mv = get_mv_from_wrl(xd, &ref_warp_model, mbmi->pb_mv_precision,
                                    bsize, xd->mi_col, xd->mi_row);
    ref_mvs[0] = ref_mv.as_mv;
    get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                        pb_mv_precision, &mv_diffs[0]);
    if (num_signaled_mvd) *num_signaled_mvd = 1;
    for (int comp = 0; comp < 2; comp++) {
      int this_mvd_comp = comp == 0 ? mv_diffs[0].row : mv_diffs[0].col;
      if (this_mvd_comp) {
        last_ref = 0;
        last_comp = comp;
        sum_mvd += (abs(this_mvd_comp) >> precision_shift);
        num_nonzero_mvd_comp++;
      }
    }
  } else if (have_newmv_in_each_reference(mbmi->mode)) {
    if (num_signaled_mvd) *num_signaled_mvd = 1 + has_second_ref(mbmi);
    for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
      const int_mv ref_mv = av1_get_ref_mv(x, ref);
      get_mvd_from_ref_mv(mbmi->mv[ref].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                          pb_mv_precision, &mv_diffs[ref]);
      ref_mvs[ref] = ref_mv.as_mv;
      for (int comp = 0; comp < 2; comp++) {
        int this_mvd_comp = comp == 0 ? mv_diffs[ref].row : mv_diffs[ref].col;
        if (this_mvd_comp) {
          last_ref = ref;
          last_comp = comp;
          sum_mvd += (abs(this_mvd_comp) >> precision_shift);
          num_nonzero_mvd_comp++;
        }
      }
    }
  } else if (mbmi->mode == NEAR_NEWMV || mbmi->mode == NEAR_NEWMV_OPTFLOW ||
             (is_joint_mvd_coding_mode(mbmi->mode) &&
              jmvd_base_ref_list == 1)) {
    const int_mv ref_mv = av1_get_ref_mv(x, 1);
    get_mvd_from_ref_mv(mbmi->mv[1].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                        pb_mv_precision, &mv_diffs[1]);
    ref_mvs[1] = ref_mv.as_mv;
    if (start_signaled_mvd_idx) *start_signaled_mvd_idx = 1;
    if (num_signaled_mvd) *num_signaled_mvd = 1;
    for (int comp = 0; comp < 2; comp++) {
      int this_mvd_comp = comp == 0 ? mv_diffs[1].row : mv_diffs[1].col;
      if (this_mvd_comp) {
        last_ref = 1;
        last_comp = comp;
        sum_mvd += (abs(this_mvd_comp) >> precision_shift);
        num_nonzero_mvd_comp++;
      }
    }
  } else if (mbmi->mode == NEW_NEARMV || mbmi->mode == NEW_NEARMV_OPTFLOW ||
             (is_joint_mvd_coding_mode(mbmi->mode) &&
              jmvd_base_ref_list == 0)) {
    const int_mv ref_mv = av1_get_ref_mv(x, 0);
    get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                        pb_mv_precision, &mv_diffs[0]);
    ref_mvs[0] = ref_mv.as_mv;
    if (num_signaled_mvd) *num_signaled_mvd = 1;
    for (int comp = 0; comp < 2; comp++) {
      int this_mvd_comp = comp == 0 ? mv_diffs[0].row : mv_diffs[0].col;
      if (this_mvd_comp) {
        last_ref = 0;
        last_comp = comp;
        sum_mvd += (abs(this_mvd_comp) >> precision_shift);
        num_nonzero_mvd_comp++;
      }
    }
  }

  *num_nonzero_mvd = num_nonzero_mvd_comp;
  if (num_nonzero_mvd_comp < th_for_num_nonzero) return 0;
  int last_mvd_comp_value =
      (last_comp == 0) ? mv_diffs[last_ref].row : mv_diffs[last_ref].col;
  assert(last_mvd_comp_value != 0);
  int last_sign = last_mvd_comp_value < 0;
  int sum_parity = sum_mvd & 0x1;
  return (last_sign == sum_parity) ? 0 : 1;
}
#endif  // CONFIG_DERIVED_MVD_SIGN

#define MAX_WARP_DELTA_ITERS 8

// Returns 1 if able to select a good model, 0 if not
// TODO(rachelbarker):
// This function cannot use the same neighbor pruning used in the other warp
// search functions, due to the way that it alternates MV and warp parameter
// refinement. Need to revisit this function in phase 2 and revisit whether
// there is a good way to do something similar.
int av1_pick_warp_delta(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        MB_MODE_INFO *mbmi, const MB_MODE_INFO_EXT *mbmi_ext,
                        const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                        const ModeCosts *mode_costs,
                        WARP_CANDIDATE *warp_param_stack) {
  WarpedMotionParams *params = &mbmi->wm_params[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  int mi_row = xd->mi_row;
  int mi_col = xd->mi_col;

  // Note(rachelbarker): Technically we can refine MVs for the AMVDNEWMV mode
  // too, but it requires more complex logic for less payoff compared to
  // refinement for NEWMV. So we don't do that currently.
  bool can_refine_mv = (mbmi->mode == NEWMV);
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  WarpedMotionParams base_params;

  // Motion vector to use at the center of the block.
  // This is the MV which should be passed into av1_set_warp_translation()
  // to determine the translational part of the model, and may differ from
  // `best_mv`, which is the signaled MV (mbmi->mv[0]) and is passed into
  // compute_motion_cost() to calculate the motion vector cost.
  //
  // For (AMVD)NEWMV, this will be the same as mbmi->mv[0], and will need to
  // be updated if we refine that motion vector.
  // For NEARMV and GLOBALMV, this will be derived from the base warp model,
  // and may differ from mbmi->mv[0]. But in these cases it won't be refined.
  int_mv center_mv;

  av1_get_warp_base_params(cm, mbmi, &base_params, &center_mv,
                           warp_param_stack);

  MV *best_mv = &mbmi->mv[0].as_mv;
  WarpedMotionParams best_wm_params;
  int rate, sse;
  int delta;
  uint64_t best_rd, inc_rd, dec_rd;
  int valid;

  static const MV neighbors[8] = { { 0, -1 }, { 1, 0 }, { 0, 1 },   { -1, 0 },
                                   { 1, -1 }, { 1, 1 }, { -1, -1 }, { -1, 1 } };
  static const int num_neighbors = 8;

  const int mv_shift =
      (MV_PRECISION_ONE_EIGHTH_PEL - ms_params->mv_cost_params.pb_mv_precision);
  const int error_per_bit = ms_params->mv_cost_params.mv_costs->errorperbit;

  // Set up initial model by copying global motion model
  // and adjusting for the chosen motion vector
  params->wmtype = ROTZOOM;
  params->wmmat[2] = base_params.wmmat[2];
  params->wmmat[3] = base_params.wmmat[3];
  params->wmmat[4] = -params->wmmat[3];
  params->wmmat[5] = params->wmmat[2];
  av1_reduce_warp_model(params);
#if CONFIG_EXT_WARP_FILTER
  av1_get_shear_params(params);
  params->invalid = 0;
#else
  valid = av1_get_shear_params(params);
  params->invalid = !valid;
  if (!valid) {
    // Don't try to refine from a broken starting point
    return 0;
  }
#endif  // CONFIG_EXT_WARP_FILTER

  av1_set_warp_translation(mi_row, mi_col, bsize, center_mv.as_mv, params);

  // Calculate initial error
  best_wm_params = *params;
  rate = av1_cost_warp_delta(cm, xd, mbmi, mbmi_ext, mode_costs);
  sse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);
  best_rd = sse + (int)ROUND_POWER_OF_TWO_64((int64_t)rate * error_per_bit,
                                             RDDIV_BITS + AV1_PROB_COST_SHIFT -
                                                 RD_EPB_SHIFT +
                                                 PIXEL_TRANSFORM_ERROR_SCALE);

  // Refine model, by making a few passes through the available
  // parameters and trying to increase/decrease them
  const int step_size = (1 << WARP_DELTA_STEP_BITS);

  for (int iter = 0; iter < MAX_WARP_DELTA_ITERS; iter++) {
    int center_best_so_far = 1;

    if (can_refine_mv) {
      int best_idx = -1;

      // Load non-translational part of the warp model
      // This part will not be changed in the following loop,
      // and the shear part has already been calculated (and is known to
      // be valid), which saves us a lot of recalculation.
      *params = best_wm_params;

      // Cost up the non-translational part of the model. Again, this will
      // not change between iterations of the following loop
      rate = av1_cost_warp_delta(cm, xd, mbmi, mbmi_ext, mode_costs);

      for (int idx = 0; idx < num_neighbors; ++idx) {
        MV this_mv = { best_mv->row + neighbors[idx].row * (1 << mv_shift),
                       best_mv->col + neighbors[idx].col * (1 << mv_shift) };
        if (av1_is_subpelmv_in_range(mv_limits, this_mv)) {
          // Update model and costs according to the motion vector which
          // is being tried out this iteration
          av1_set_warp_translation(mi_row, mi_col, bsize, this_mv, params);

          unsigned int this_sse =
              compute_motion_cost(xd, cm, ms_params, bsize, &this_mv);
          uint64_t this_rd =
              this_sse + (int)ROUND_POWER_OF_TWO_64(
                             (int64_t)rate * error_per_bit,
                             RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT +
                                 PIXEL_TRANSFORM_ERROR_SCALE);

          if (this_rd < best_rd) {
            best_idx = idx;
            best_wm_params = *params;
            best_rd = this_rd;
          }
        }
      }

      if (best_idx >= 0) {
        // Commit to this motion vector
        best_mv->row += neighbors[best_idx].row * (1 << mv_shift);
        best_mv->col += neighbors[best_idx].col * (1 << mv_shift);
        center_mv.as_mv = *best_mv;
      }
    }

    for (int param_index = 2; param_index < 4; param_index++) {
      // Try increasing the parameter
      *params = best_wm_params;
      params->wmmat[param_index] += step_size;
      delta = params->wmmat[param_index] - base_params.wmmat[param_index];
      if (abs(delta) > WARP_DELTA_MAX) {
        inc_rd = UINT64_MAX;
      } else {
        params->wmmat[4] = -params->wmmat[3];
        params->wmmat[5] = params->wmmat[2];
#if CONFIG_EXT_WARP_FILTER
        valid =
            av1_is_warp_model_reduced(params) && av1_get_shear_params(params);
#else
        av1_reduce_warp_model(params);
        valid = av1_get_shear_params(params);
        params->invalid = !valid;
#endif  // CONFIG_EXT_WARP_FILTER
        if (valid) {
          av1_set_warp_translation(mi_row, mi_col, bsize, center_mv.as_mv,
                                   params);
          rate = av1_cost_warp_delta(cm, xd, mbmi, mbmi_ext, mode_costs);
          sse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);
          inc_rd = sse + (int)ROUND_POWER_OF_TWO_64(
                             (int64_t)rate * error_per_bit,
                             RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT +
                                 PIXEL_TRANSFORM_ERROR_SCALE);
        } else {
          inc_rd = UINT64_MAX;
        }
      }
      WarpedMotionParams inc_params = *params;

      // Try decreasing the parameter
      *params = best_wm_params;
      params->wmmat[param_index] -= step_size;
      delta = params->wmmat[param_index] - base_params.wmmat[param_index];
      if (abs(delta) > WARP_DELTA_MAX) {
        dec_rd = UINT64_MAX;
      } else {
        params->wmmat[4] = -params->wmmat[3];
        params->wmmat[5] = params->wmmat[2];
#if CONFIG_EXT_WARP_FILTER
        valid =
            av1_is_warp_model_reduced(params) && av1_get_shear_params(params);
#else
        av1_reduce_warp_model(params);
        valid = av1_get_shear_params(params);
        params->invalid = !valid;
#endif  // CONFIG_EXT_WARP_FILTER
        if (valid) {
          av1_set_warp_translation(mi_row, mi_col, bsize, center_mv.as_mv,
                                   params);
          rate = av1_cost_warp_delta(cm, xd, mbmi, mbmi_ext, mode_costs);
          sse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);
          dec_rd = sse + (int)ROUND_POWER_OF_TWO_64(
                             (int64_t)rate * error_per_bit,
                             RDDIV_BITS + AV1_PROB_COST_SHIFT - RD_EPB_SHIFT +
                                 PIXEL_TRANSFORM_ERROR_SCALE);
        } else {
          dec_rd = UINT64_MAX;
        }
      }
      WarpedMotionParams dec_params = *params;

      // Pick the best parameter value at this level
      if (inc_rd < best_rd) {
        if (dec_rd < inc_rd) {
          // Decreasing is best
          best_wm_params = dec_params;
          best_rd = dec_rd;
          center_best_so_far = 0;
        } else {
          // Increasing is best
          best_wm_params = inc_params;
          best_rd = inc_rd;
          center_best_so_far = 0;
        }
      } else if (dec_rd < best_rd) {
        // Decreasing is best
        best_wm_params = dec_params;
        best_rd = dec_rd;
        center_best_so_far = 0;
      } else {
        // Current is best
        // No need to change anything
      }
    }

    if (center_best_so_far) {
      break;
    }
  }

  mbmi->wm_params[0] = best_wm_params;

  return 1;
}

// Returns 1 if able to select a good model, 0 if not
int av1_refine_mv_for_base_param_warp_model(
    const AV1_COMMON *const cm, MACROBLOCKD *xd, MB_MODE_INFO *mbmi,
    const MB_MODE_INFO_EXT *mbmi_ext,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
    WARP_SEARCH_METHOD search_method, int num_iterations) {
  WarpedMotionParams *params = &mbmi->wm_params[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  int mi_row = xd->mi_row;
  int mi_col = xd->mi_col;

  assert(IMPLIES(mbmi->warpmv_with_mvd_flag, mbmi->mode == WARPMV));

  bool can_refine_mv = (mbmi->mode == NEWMV ||
                        (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag));
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  // get the base parameters
  WarpedMotionParams base_params;
  int_mv center_mv;
  av1_get_warp_base_params(
      cm, mbmi, &base_params, &center_mv,
      mbmi_ext->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]);

  *params = base_params;

  av1_set_warp_translation(mi_row, mi_col, bsize, center_mv.as_mv, params);
  int valid = av1_get_shear_params(params);
  params->invalid = !valid;
  if (!valid) {
    // Don't try to refine from a broken starting point
    return 0;
  }

  // parameters are valid however, mv refinement is not supported
  if (!can_refine_mv) return 1;  // returning 1 means valid model is found

  WarpedMotionParams best_wm_params;
  int sse;
  uint64_t best_rd;
  MV *best_mv = &mbmi->mv[0].as_mv;

  const MV *neighbors = warp_search_info[search_method].neighbors;
  const int num_neighbors = warp_search_info[search_method].num_neighbors;
  const uint8_t *neighbor_mask = warp_search_info[search_method].neighbor_mask;

  const int mv_shift =
      (MV_PRECISION_ONE_EIGHTH_PEL - ms_params->mv_cost_params.pb_mv_precision);

  // Calculate initial error
  best_wm_params = *params;

  sse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);
  best_rd = sse;

  // First iteration always scans all neighbors
  uint8_t valid_neighbors = UINT8_MAX;

  for (int ite = 0; ite < num_iterations; ++ite) {
    int best_idx = -1;
    *params = base_params;  // best_wm_params;

    for (int idx = 0; idx < num_neighbors; ++idx) {
      if ((valid_neighbors & (1 << idx)) == 0) {
        continue;
      }

      MV this_mv = { best_mv->row + neighbors[idx].row * (1 << mv_shift),
                     best_mv->col + neighbors[idx].col * (1 << mv_shift) };
      if (av1_is_subpelmv_in_range(mv_limits, this_mv)) {
        // Update model and costs according to the motion vector which
        // is being tried out this iteration
        av1_set_warp_translation(mi_row, mi_col, bsize, this_mv, params);
        if (!av1_get_shear_params(params)) continue;

        unsigned int this_sse =
            compute_motion_cost(xd, cm, ms_params, bsize, &this_mv);
        uint64_t this_rd = this_sse;

        if (this_rd < best_rd) {
          best_idx = idx;
          best_wm_params = *params;
          best_rd = this_rd;
        }
      }
    }
    if (best_idx == -1) break;
    if (best_idx >= 0) {
      // Commit to this motion vector
      best_mv->row += neighbors[best_idx].row * (1 << mv_shift);
      best_mv->col += neighbors[best_idx].col * (1 << mv_shift);
      center_mv.as_mv = *best_mv;
      valid_neighbors = neighbor_mask[best_idx];
    }
  }

  mbmi->wm_params[0] = best_wm_params;

  return 1;
}

// Try to improve the motion vector over the one determined by NEWMV search,
// by running the full WARP_EXTEND prediction pipeline.
// For now, this uses the same method as av1_refine_warped_mv()
// TODO(rachelbarker): See if we can improve this and av1_refine_warped_mv().
void av1_refine_mv_for_warp_extend(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                   const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                   bool neighbor_is_above, BLOCK_SIZE bsize,
                                   const WarpedMotionParams *neighbor_params,
                                   WARP_SEARCH_METHOD search_method,
                                   int num_iterations) {
  MB_MODE_INFO *mbmi = xd->mi[0];

  const MV *neighbors = warp_search_info[search_method].neighbors;
  const int num_neighbors = warp_search_info[search_method].num_neighbors;
  const uint8_t *neighbor_mask = warp_search_info[search_method].neighbor_mask;

  const int mv_shift =
      (MV_PRECISION_ONE_EIGHTH_PEL - ms_params->mv_cost_params.pb_mv_precision);

  MV *best_mv = &mbmi->mv[0].as_mv;

  WarpedMotionParams best_wm_params = mbmi->wm_params[0];
  unsigned int bestmse;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  // Before this function is called, motion_mode_rd will have selected a valid
  // warp model, and stored it into mbmi->wm_params, but we have not yet
  // actually built and evaluated the resulting prediction
  assert(av1_is_subpelmv_in_range(mv_limits, *best_mv));
  bestmse = compute_motion_cost(xd, cm, ms_params, bsize, best_mv);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // First iteration always scans all neighbors
  uint8_t valid_neighbors = UINT8_MAX;

  for (int ite = 0; ite < num_iterations; ++ite) {
    int best_idx = -1;

    for (int idx = 0; idx < num_neighbors; ++idx) {
      if ((valid_neighbors & (1 << idx)) == 0) {
        continue;
      }

      unsigned int thismse;

      MV this_mv = { best_mv->row + neighbors[idx].row * (1 << mv_shift),
                     best_mv->col + neighbors[idx].col * (1 << mv_shift) };
      if (av1_is_subpelmv_in_range(mv_limits, this_mv)) {
        if (!av1_extend_warp_model(neighbor_is_above, bsize, &this_mv, mi_row,
                                   mi_col, neighbor_params,
                                   &mbmi->wm_params[0])) {
          thismse = compute_motion_cost(xd, cm, ms_params, bsize, &this_mv);

          if (thismse < bestmse) {
            best_idx = idx;
            best_wm_params = mbmi->wm_params[0];
            bestmse = thismse;
          }
        }
      }
    }

    if (best_idx == -1) break;

    if (best_idx >= 0) {
      best_mv->row += neighbors[best_idx].row * (1 << mv_shift);
      best_mv->col += neighbors[best_idx].col * (1 << mv_shift);
      valid_neighbors = neighbor_mask[best_idx];
    }
  }

  mbmi->wm_params[0] = best_wm_params;
}

// Calculates the variance of prediction residue
static int upsampled_obmc_pref_error(MACROBLOCKD *xd, const AV1_COMMON *cm,
                                     const MV *this_mv,
                                     const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                                     unsigned int *sse) {
  const aom_variance_fn_ptr_t *vfp = var_params->vfp;
  const SUBPEL_SEARCH_TYPE subpel_search_type = var_params->subpel_search_type;
  const int w = var_params->w;
  const int h = var_params->h;

  const MSBuffers *ms_buffers = &var_params->ms_buffers;
  const int32_t *wsrc = ms_buffers->wsrc;
  const int32_t *mask = ms_buffers->obmc_mask;
  const uint16_t *ref = get_buf_from_mv(ms_buffers->ref, *this_mv);
  const int ref_stride = ms_buffers->ref->stride;

  const int subpel_x_q3 = get_subpel_part(this_mv->col);
  const int subpel_y_q3 = get_subpel_part(this_mv->row);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  unsigned int besterr;
  const int is_scaled_ref = ms_buffers->src->width == ms_buffers->ref->width &&
                            ms_buffers->src->height == ms_buffers->ref->height;

  DECLARE_ALIGNED(16, uint16_t, pred[MAX_SB_SQUARE]);
  aom_highbd_upsampled_pred(xd, cm, mi_row, mi_col, this_mv, pred, w, h,
                            subpel_x_q3, subpel_y_q3, ref, ref_stride, xd->bd,
                            subpel_search_type, is_scaled_ref);
  besterr = vfp->ovf(pred, w, wsrc, mask, sse);

  return besterr;
}

static unsigned int upsampled_setup_obmc_center_error(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV *this_mv,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *sse1, int *distortion) {
  unsigned int besterr =
      upsampled_obmc_pref_error(xd, cm, this_mv, var_params, sse1);
  *distortion = besterr;
  besterr += mv_err_cost(*this_mv, mv_cost_params);
  return besterr;
}

// Estimates whether this_mv is better than best_mv. This function incorporates
// both prediction error and residue into account.
static INLINE unsigned int obmc_check_better(
    MACROBLOCKD *xd, const AV1_COMMON *cm, const MV *this_mv, MV *best_mv,
    const SubpelMvLimits *mv_limits, const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion, int *has_better_mv) {
  unsigned int cost;
  if (av1_is_subpelmv_in_range(mv_limits, *this_mv)) {
    unsigned int sse;
    const int thismse =
        upsampled_obmc_pref_error(xd, cm, this_mv, var_params, &sse);

    cost = mv_err_cost(*this_mv, mv_cost_params);
    cost += thismse;

    if (cost < *besterr) {
      *besterr = cost;
      *best_mv = *this_mv;
      *distortion = thismse;
      *sse1 = sse;
      *has_better_mv |= 1;
    }
  } else {
    cost = INT_MAX;
  }
  return cost;
}

static AOM_FORCE_INLINE MV obmc_first_level_check(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV this_mv, MV *best_mv,
    const int hstep, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion) {
  int dummy = 0;
  const MV left_mv = { this_mv.row, this_mv.col - hstep };
  const MV right_mv = { this_mv.row, this_mv.col + hstep };
  const MV top_mv = { this_mv.row - hstep, this_mv.col };
  const MV bottom_mv = { this_mv.row + hstep, this_mv.col };

  const unsigned int left =
      obmc_check_better(xd, cm, &left_mv, best_mv, mv_limits, var_params,
                        mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int right =
      obmc_check_better(xd, cm, &right_mv, best_mv, mv_limits, var_params,
                        mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int up =
      obmc_check_better(xd, cm, &top_mv, best_mv, mv_limits, var_params,
                        mv_cost_params, besterr, sse1, distortion, &dummy);
  const unsigned int down =
      obmc_check_better(xd, cm, &bottom_mv, best_mv, mv_limits, var_params,
                        mv_cost_params, besterr, sse1, distortion, &dummy);

  const MV diag_step = get_best_diag_step(hstep, left, right, up, down);
  const MV diag_mv = { this_mv.row + diag_step.row,
                       this_mv.col + diag_step.col };

  // Check the diagonal direction with the best mv
  obmc_check_better(xd, cm, &diag_mv, best_mv, mv_limits, var_params,
                    mv_cost_params, besterr, sse1, distortion, &dummy);

  return diag_step;
}

// A newer version of second level check for obmc that gives better quality.
static AOM_FORCE_INLINE void obmc_second_level_check_v2(
    MACROBLOCKD *xd, const AV1_COMMON *const cm, const MV this_mv, MV diag_step,
    MV *best_mv, const SubpelMvLimits *mv_limits,
    const SUBPEL_SEARCH_VAR_PARAMS *var_params,
    const MV_COST_PARAMS *mv_cost_params, unsigned int *besterr,
    unsigned int *sse1, int *distortion) {
  assert(best_mv->row == this_mv.row + diag_step.row ||
         best_mv->col == this_mv.col + diag_step.col);
  if (CHECK_MV_EQUAL(this_mv, *best_mv)) {
    return;
  } else if (this_mv.row == best_mv->row) {
    // Search away from diagonal step since diagonal search did not provide any
    // improvement
    diag_step.row *= -1;
  } else if (this_mv.col == best_mv->col) {
    diag_step.col *= -1;
  }

  const MV row_bias_mv = { best_mv->row + diag_step.row, best_mv->col };
  const MV col_bias_mv = { best_mv->row, best_mv->col + diag_step.col };
  const MV diag_bias_mv = { best_mv->row + diag_step.row,
                            best_mv->col + diag_step.col };
  int has_better_mv = 0;
  obmc_check_better(xd, cm, &row_bias_mv, best_mv, mv_limits, var_params,
                    mv_cost_params, besterr, sse1, distortion, &has_better_mv);
  obmc_check_better(xd, cm, &col_bias_mv, best_mv, mv_limits, var_params,
                    mv_cost_params, besterr, sse1, distortion, &has_better_mv);

  // Do an additional search if the second iteration gives a better mv
  if (has_better_mv) {
    obmc_check_better(xd, cm, &diag_bias_mv, best_mv, mv_limits, var_params,
                      mv_cost_params, besterr, sse1, distortion,
                      &has_better_mv);
  }
}

int av1_find_best_obmc_sub_pixel_tree_up(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, int_mv *last_mv_search_list) {
  (void)last_mv_search_list;
  const int forced_stop = ms_params->forced_stop;
  const int iters_per_step = ms_params->iters_per_step;
  const MV_COST_PARAMS *mv_cost_params = &ms_params->mv_cost_params;
  const SUBPEL_SEARCH_VAR_PARAMS *var_params = &ms_params->var_params;
  const SubpelMvLimits *mv_limits = &ms_params->mv_limits;

  int hstep = INIT_SUBPEL_STEP_SIZE;
  const int round =
      (mv_cost_params->pb_mv_precision >= MV_PRECISION_ONE_PEL)
          ? AOMMIN(FULL_PEL - forced_stop,
                   mv_cost_params->pb_mv_precision - MV_PRECISION_ONE_PEL)
          : 0;

  unsigned int besterr = INT_MAX;
  *bestmv = start_mv;

  besterr = upsampled_setup_obmc_center_error(xd, cm, bestmv, var_params,
                                              mv_cost_params, sse1, distortion);

  for (int iter = 0; iter < round; ++iter) {
    MV iter_center_mv = *bestmv;
    MV diag_step = obmc_first_level_check(xd, cm, iter_center_mv, bestmv, hstep,
                                          mv_limits, var_params, mv_cost_params,
                                          &besterr, sse1, distortion);

    if (!CHECK_MV_EQUAL(iter_center_mv, *bestmv) && iters_per_step > 1) {
      obmc_second_level_check_v2(xd, cm, iter_center_mv, diag_step, bestmv,
                                 mv_limits, var_params, mv_cost_params,
                                 &besterr, sse1, distortion);
    }
    hstep >>= 1;
  }

  return besterr;
}

// =============================================================================
//  Public cost function: mv_cost + pred error
// =============================================================================
int av1_get_mvpred_sse(const MV_COST_PARAMS *mv_cost_params,
                       const FULLPEL_MV best_mv,
                       const aom_variance_fn_ptr_t *vfp,
                       const struct buf_2d *src, const struct buf_2d *pre) {
#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV mv = get_mv_from_fullmv(&best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*mv_cost_params->ref_mv, &sub_mv_offset,
                    mv_cost_params->pb_mv_precision);
  if (mv_cost_params->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    mv.col += sub_mv_offset.col;
    mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  unsigned int sse, var;

  var = vfp->vf(src->buf, src->stride, get_buf_from_fullmv(pre, &best_mv),
                pre->stride, &sse);
  (void)var;
  return sse + mv_err_cost(mv, mv_cost_params);
}

static INLINE int get_mvpred_av_var(const MV_COST_PARAMS *mv_cost_params,
                                    const FULLPEL_MV best_mv,
                                    const uint16_t *second_pred,
                                    const aom_variance_fn_ptr_t *vfp,
                                    const struct buf_2d *src,
                                    const struct buf_2d *pre) {
#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV mv = get_mv_from_fullmv(&best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*mv_cost_params->ref_mv, &sub_mv_offset,
                    mv_cost_params->pb_mv_precision);
  if (mv_cost_params->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    mv.col += sub_mv_offset.col;
    mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  unsigned int unused;

  return vfp->svaf(get_buf_from_fullmv(pre, &best_mv), pre->stride, 0, 0,
                   src->buf, src->stride, &unused, second_pred) +
         mv_err_cost(mv, mv_cost_params);
}

static INLINE int get_mvpred_mask_var(
    const MV_COST_PARAMS *mv_cost_params, const FULLPEL_MV best_mv,
    const uint16_t *second_pred, const uint8_t *mask, int mask_stride,
    int invert_mask, const aom_variance_fn_ptr_t *vfp, const struct buf_2d *src,
    const struct buf_2d *pre) {
#if !CONFIG_C071_SUBBLK_WARPMV
  const
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      MV mv = get_mv_from_fullmv(&best_mv);
#if CONFIG_C071_SUBBLK_WARPMV
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(*mv_cost_params->ref_mv, &sub_mv_offset,
                    mv_cost_params->pb_mv_precision);
  if (mv_cost_params->pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    mv.col += sub_mv_offset.col;
    mv.row += sub_mv_offset.row;
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  unsigned int unused;

  return vfp->msvf(get_buf_from_fullmv(pre, &best_mv), pre->stride, 0, 0,
                   src->buf, src->stride, second_pred, mask, mask_stride,
                   invert_mask, &unused) +
         mv_err_cost(mv, mv_cost_params);
}

int av1_get_mvpred_compound_var(
    const MV_COST_PARAMS *mv_cost_params, const FULLPEL_MV best_mv,
    const uint16_t *second_pred, const uint8_t *mask, int mask_stride,
    int invert_mask, const aom_variance_fn_ptr_t *vfp, const struct buf_2d *src,
    const struct buf_2d *pre) {
  if (mask) {
    return get_mvpred_mask_var(mv_cost_params, best_mv, second_pred, mask,
                               mask_stride, invert_mask, vfp, src, pre);
  } else {
    return get_mvpred_av_var(mv_cost_params, best_mv, second_pred, vfp, src,
                             pre);
  }
}
