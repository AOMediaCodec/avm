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

#ifndef AOM_AV1_COMMON_RECONINTER_H_
#define AOM_AV1_COMMON_RECONINTER_H_

#include "av1/common/av1_common_int.h"
#include "av1/common/convolve.h"
#include "av1/common/filter.h"
#include "av1/common/warped_motion.h"
#include "aom/aom_integer.h"

// Work out how many pixels off the edge of a reference frame we're allowed
// to go when forming an inter prediction.
// The outermost row/col of each referernce frame is extended by
// (AOM_BORDER_IN_PIXELS >> subsampling) pixels, but we need to keep
// at least AOM_INTERP_EXTEND pixels within that to account for filtering.
//
// We have to break this up into two macros to keep both clang-format and
// tools/lint-hunks.py happy.
#define AOM_LEFT_TOP_MARGIN_PX(subsampling) \
  ((AOM_BORDER_IN_PIXELS >> subsampling) - AOM_INTERP_EXTEND)
#define AOM_LEFT_TOP_MARGIN_SCALED(subsampling) \
  (AOM_LEFT_TOP_MARGIN_PX(subsampling) << SCALE_SUBPEL_BITS)

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WEDGE_TYPES 16

#define MAX_WEDGE_SIZE_LOG2 5  // 32x32
#define MAX_WEDGE_SIZE (1 << MAX_WEDGE_SIZE_LOG2)
#define MAX_WEDGE_SQUARE (MAX_WEDGE_SIZE * MAX_WEDGE_SIZE)

#define WEDGE_WEIGHT_BITS 6

#define WEDGE_NONE -1

#if CONFIG_BAWP
#define BAWP_REF_LINES 1
#endif

// Angles are with respect to horizontal anti-clockwise
enum {
  WEDGE_HORIZONTAL = 0,
  WEDGE_VERTICAL = 1,
  WEDGE_OBLIQUE27 = 2,
  WEDGE_OBLIQUE63 = 3,
  WEDGE_OBLIQUE117 = 4,
  WEDGE_OBLIQUE153 = 5,
  WEDGE_DIRECTIONS
} UENUM1BYTE(WedgeDirectionType);

// 3-tuple: {direction, x_offset, y_offset}
typedef struct {
  WedgeDirectionType direction;
  int x_offset;
  int y_offset;
} wedge_code_type;

typedef uint8_t *wedge_masks_type[MAX_WEDGE_TYPES];

typedef struct {
  int wedge_types;
  const wedge_code_type *codebook;
  uint8_t *signflip;
  wedge_masks_type *masks;
} wedge_params_type;

extern const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL];

typedef struct SubpelParams {
  int xs;
  int ys;
  int subpel_x;
  int subpel_y;
} SubpelParams;

struct build_prediction_ctxt {
  const AV1_COMMON *cm;
  uint16_t **tmp_buf;
  int *tmp_width;
  int *tmp_height;
  int *tmp_stride;
  int mb_to_far_edge;
  void *dcb;  // Decoder-only coding block.
};

typedef enum InterPredMode {
  TRANSLATION_PRED,
  WARP_PRED,
} InterPredMode;

typedef enum InterCompMode {
  UNIFORM_SINGLE,
  UNIFORM_COMP,
  MASK_COMP,
} InterCompMode;

typedef struct InterPredParams {
  InterPredMode mode;
  InterCompMode comp_mode;
  WarpedMotionParams warp_params;
  ConvolveParams conv_params;
  const InterpFilterParams *interp_filter_params[2];
  int block_width;
  int block_height;
#if CONFIG_OPTFLOW_REFINEMENT
  // In optical flow refinement, block_width and block_height will pass the
  // subblock size into av1_make_inter_predictor, while orig_block_width and
  // orig_block_height keep the original block size that is needed by
  // calc_subpel_params_func
  int orig_block_width;
  int orig_block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT
  int pix_row;
  int pix_col;
  struct buf_2d ref_frame_buf;
  int subsampling_x;
  int subsampling_y;
  const struct scale_factors *scale_factors;
  int bit_depth;
  INTERINTER_COMPOUND_DATA mask_comp;
  BLOCK_SIZE sb_type;
  int is_intrabc;
#if CONFIG_TIP
  /**
   * \name Distance of this block from frame edges in 1/8th pixel units.
   */
  /**@{*/
  int dist_to_left_edge;   /*!< Distance from left edge */
  int dist_to_right_edge;  /*!< Distance from right edge */
  int dist_to_top_edge;    /*!< Distance from top edge */
  int dist_to_bottom_edge; /*!< Distance from bottom edge */
#endif                     // CONFIG_TIP
} InterPredParams;

#if CONFIG_OPTFLOW_REFINEMENT

// Apply bilinear and bicubic interpolation for subpel gradient to avoid
// calls of build_one_inter_predictor function. Bicubic interpolation
// brings better quality but the speed results are neutral. As such, bilinear
// interpolation is used by default for a better trade-off between quality
// and complexity.
#define OPFL_BILINEAR_GRAD 0
#define OPFL_BICUBIC_GRAD 1

// Use downsampled gradient arrays to compute MV offsets
#define OPFL_DOWNSAMP_QUINCUNX 1

// Delta to use for computing gradients in bits, with 0 referring to
// integer-pel. The actual delta value used from the 1/8-pel original MVs
// is 2^(3 - SUBPEL_GRAD_DELTA_BITS). The max value of this macro is 3.
#define SUBPEL_GRAD_DELTA_BITS 3

// Combine computations of interpolated gradients and the least squares
// solver. The basic idea is that, typically we would compute the following:
// 1. d0, d1, P0 and P1
// 2. Gradients of P0 and P1: gx0, gx1, gy0, and gy1
// 3. Solving least squares for vx and vy, which requires d0*gx0-d1*gx1,
//    d0*gy0-d1*gy1, and P0-P1.
// When this flag is turned on, we compute the following
// 1. d0, d1, P0 and P1
// 2. tmp0 = d0*P0-d1*P1 and tmp1 = P0-P1
// 3. Gradients of tmp0: gx and gy
// 4. Solving least squares for vx and vy using gx, gy and tmp1
// Note that this only requires 2 gradient operators instead of 4 and thus
// reduces the complexity. However, it is only feasible when gradients are
// obtained using bilinear or bicubic interpolation. Thus, this flag should
// only be on when either of OPFL_BILINEAR_GRAD and OPFL_BICUBIC_GRAD is on.
#define OPFL_COMBINE_INTERP_GRAD_LS 1

// Bilinear and bicubic coefficients. Note that, at boundary, we apply
// coefficients that are doubled because spatial distance between the two
// interpolated pixels is halved. In other words, instead of computing
//   coeff * (v[delta] - v[-delta]) / (2 * delta),
// we are practically computing
//   coeff * (v[delta] - v[0]) / (2 * delta).
// Thus, coeff is doubled to get a better gradient quality.
#if OPFL_BILINEAR_GRAD
static const int bilinear_bits = 3;
static const int32_t coeffs_bilinear[4][2] = {
  { 8, 16 },  // delta = 1 (SUBPEL_GRAD_DELTA_BITS = 0)
  { 4, 8 },   // delta = 0.5 (SUBPEL_GRAD_DELTA_BITS = 1)
  { 2, 4 },   // delta = 0.25 (SUBPEL_GRAD_DELTA_BITS = 2)
  { 1, 2 },   // delta = 0.125 (SUBPEL_GRAD_DELTA_BITS = 3)
};
#endif

#if OPFL_BICUBIC_GRAD
static const int bicubic_bits = 7;
static const int32_t coeffs_bicubic[4][2][2] = {
  { { 128, 256 }, { 0, 0 } },    // delta = 1 (SUBPEL_GRAD_DELTA_BITS = 0)
  { { 80, 160 }, { -8, -16 } },  // delta = 0.5 (SUBPEL_GRAD_DELTA_BITS = 1)
  { { 42, 84 }, { -5, -10 } },   // delta = 0.25 (SUBPEL_GRAD_DELTA_BITS = 2)
  { { 21, 42 }, { -3, -6 } },    // delta = 0.125 (SUBPEL_GRAD_DELTA_BITS = 3)
};
#endif
#endif  // CONFIG_OPTFLOW_REFINEMENT

void av1_init_inter_params(InterPredParams *inter_pred_params, int block_width,
                           int block_height, int pix_row, int pix_col,
                           int subsampling_x, int subsampling_y, int bit_depth,
                           int is_intrabc, const struct scale_factors *sf,
                           const struct buf_2d *ref_buf,
                           InterpFilter interp_filter);

#if CONFIG_WARP_REF_LIST
// Check if the signaling of the warp delta parameters are allowed
static INLINE int allow_warp_parameter_signaling(const MB_MODE_INFO *mbmi) {
  return (
#if CONFIG_WARPMV
      mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
      mbmi->motion_mode == WARP_DELTA && mbmi->warp_ref_idx == 1);
}
#endif  // CONFIG_WARP_REF_LIST

#if CONFIG_ADAPTIVE_MVD
static INLINE int enable_adaptive_mvd_resolution(const AV1_COMMON *const cm,
                                                 const MB_MODE_INFO *mbmi) {
  const int mode = mbmi->mode;

  return (mode == NEAR_NEWMV || mode == NEW_NEARMV
#if CONFIG_OPTFLOW_REFINEMENT
          || mode == NEAR_NEWMV_OPTFLOW || mode == NEW_NEARMV_OPTFLOW
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
          || mode == JOINT_AMVDNEWMV_OPTFLOW
#endif  // IMPROVED_AMVD && CONFIG_JOINT_MVD
#endif
#if IMPROVED_AMVD
          || mode == AMVDNEWMV
#endif  // IMPROVED_AMVD
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
          || mode == JOINT_AMVDNEWMV
#endif
          ) &&
         cm->seq_params.enable_adaptive_mvd;
}
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_JOINT_MVD
// get the base reference frame list for joint MVD coding, the MVD for base
// reference frame is the same as the joint MVD, the MVD for the other reference
// frame is scaled from the joint MVD.
static INLINE int get_joint_mvd_base_ref_list(const AV1_COMMON *const cm,
                                              const MB_MODE_INFO *mbmi) {
  int base_ref_list = 0;
  int first_ref_dist = 0;
  int sec_ref_dist = 0;
  if (has_second_ref(mbmi)) {
    first_ref_dist = cm->ref_frame_relative_dist[mbmi->ref_frame[0]];
    sec_ref_dist = cm->ref_frame_relative_dist[mbmi->ref_frame[1]];

    if (first_ref_dist >= sec_ref_dist) {
      base_ref_list = 0;
    } else {
      base_ref_list = 1;
    }
  }
  return base_ref_list;
}
// check whether the direction of two reference frames are from same side
static INLINE int is_ref_frame_same_side(const AV1_COMMON *const cm,
                                         const MB_MODE_INFO *mbmi) {
  int is_same_side = 0;
  int cur_ref_side = 0;
  int other_ref_side = 0;
  if (has_second_ref(mbmi)) {
    cur_ref_side = cm->ref_frame_side[mbmi->ref_frame[0]];
    other_ref_side = cm->ref_frame_side[mbmi->ref_frame[1]];

    is_same_side = (cur_ref_side > 0 && other_ref_side > 0) ||
                   (cur_ref_side == 0 && other_ref_side == 0);
  }
  return is_same_side;
}
#endif  // CONFIG_JOINT_MVD

void av1_init_comp_mode(InterPredParams *inter_pred_params);

void av1_init_warp_params(InterPredParams *inter_pred_params,
                          const WarpTypesAllowed *warp_types, int ref,
                          const MACROBLOCKD *xd, const MB_MODE_INFO *mi);

static INLINE int has_scale(int xs, int ys) {
  return xs != SCALE_SUBPEL_SHIFTS || ys != SCALE_SUBPEL_SHIFTS;
}

static INLINE void revert_scale_extra_bits(SubpelParams *sp) {
  sp->subpel_x >>= SCALE_EXTRA_BITS;
  sp->subpel_y >>= SCALE_EXTRA_BITS;
  sp->xs >>= SCALE_EXTRA_BITS;
  sp->ys >>= SCALE_EXTRA_BITS;
  assert(sp->subpel_x < SUBPEL_SHIFTS);
  assert(sp->subpel_y < SUBPEL_SHIFTS);
  assert(sp->xs <= SUBPEL_SHIFTS);
  assert(sp->ys <= SUBPEL_SHIFTS);
}

static INLINE void highbd_inter_predictor(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    const SubpelParams *subpel_params, int w, int h,
    ConvolveParams *conv_params, const InterpFilterParams *interp_filters[2],
    int bd) {
  assert(conv_params->do_average == 0 || conv_params->do_average == 1);
  const int is_scaled = has_scale(subpel_params->xs, subpel_params->ys);
  if (is_scaled) {
    av1_highbd_convolve_2d_facade(src, src_stride, dst, dst_stride, w, h,
                                  interp_filters, subpel_params->subpel_x,
                                  subpel_params->xs, subpel_params->subpel_y,
                                  subpel_params->ys, 1, conv_params, bd);
  } else {
    SubpelParams sp = *subpel_params;
    revert_scale_extra_bits(&sp);
    av1_highbd_convolve_2d_facade(src, src_stride, dst, dst_stride, w, h,
                                  interp_filters, sp.subpel_x, sp.xs,
                                  sp.subpel_y, sp.ys, 0, conv_params, bd);
  }
}

void av1_modify_neighbor_predictor_for_obmc(MB_MODE_INFO *mbmi);
int av1_skip_u4x4_pred_in_obmc(BLOCK_SIZE bsize,
                               const struct macroblockd_plane *pd, int dir);

static INLINE int is_interinter_compound_used(COMPOUND_TYPE type,
                                              BLOCK_SIZE sb_type) {
  const int comp_allowed = is_comp_ref_allowed(sb_type);
  switch (type) {
    case COMPOUND_AVERAGE:
    case COMPOUND_DIFFWTD: return comp_allowed;
    case COMPOUND_WEDGE:
      return comp_allowed && av1_wedge_params_lookup[sb_type].wedge_types > 0;
    default: assert(0); return 0;
  }
}

static INLINE int is_any_masked_compound_used(BLOCK_SIZE sb_type) {
  COMPOUND_TYPE comp_type;
  int i;
  if (!is_comp_ref_allowed(sb_type)) return 0;
  for (i = 0; i < COMPOUND_TYPES; i++) {
    comp_type = (COMPOUND_TYPE)i;
    if (is_masked_compound_type(comp_type) &&
        is_interinter_compound_used(comp_type, sb_type))
      return 1;
  }
  return 0;
}

static INLINE int get_wedge_types_lookup(BLOCK_SIZE sb_type) {
  return av1_wedge_params_lookup[sb_type].wedge_types;
}

static INLINE int av1_is_wedge_used(BLOCK_SIZE sb_type) {
  return av1_wedge_params_lookup[sb_type].wedge_types > 0;
}

void av1_make_inter_predictor(const uint16_t *src, int src_stride,
                              uint16_t *dst, int dst_stride,
                              InterPredParams *inter_pred_params,
                              const SubpelParams *subpel_params);

typedef void (*CalcSubpelParamsFunc)(const MV *const src_mv,
                                     InterPredParams *const inter_pred_params,
                                     MACROBLOCKD *xd, int mi_x, int mi_y,
                                     int ref,
#if CONFIG_OPTFLOW_REFINEMENT
                                     int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                     uint16_t **mc_buf, uint16_t **pre,
                                     SubpelParams *subpel_params,
                                     int *src_stride);

void av1_build_one_inter_predictor(
    uint16_t *dst, int dst_stride, const MV *const src_mv,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func);

void av1_build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                int plane, MB_MODE_INFO *mi,
#if CONFIG_BAWP
                                const BUFFER_SET *dst_orig,
#endif
                                int build_for_obmc, int bw, int bh, int mi_x,
                                int mi_y, uint16_t **mc_buf,
                                CalcSubpelParamsFunc calc_subpel_params_func);

#if CONFIG_OPTFLOW_REFINEMENT
// This parameter k=OPFL_DIST_RATIO_THR is used to prune MV refinement for the
// case where d0 and d1 are very different. Assuming a = max(|d0|, |d1|) and
// b = min(|d0|, |d1|), MV refinement will only be allowed only if a/b <= k.
// If k is set to 0, refinement will always be enabled.
// If k is set to 1, refinement will only be enabled when |d0|=|d1|.
#define OPFL_DIST_RATIO_THR 0

// Apply regularized least squares (RLS). The RLS parameter is bw * bh * 2^(b-4)
// where b = OPFL_RLS_PARAM_BITS.
#define OPFL_REGULARIZED_LS 1
#define OPFL_RLS_PARAM_BITS 4

// Number of bits allowed for covariance matrix elements (su2, sv2, suv, suw
// and svw) so that det, det_x, and det_y does not cause overflow issue in
// int64_t. Its value must be <= (64 - mv_prec_bits - grad_prec_bits) / 2.
#define OPFL_COV_CLAMP_BITS 28
#define OPFL_COV_CLAMP_VAL (1 << OPFL_COV_CLAMP_BITS)

// Precision of refined MV returned, 0 being integer pel. For now, only 1/8 or
// 1/16-pel can be used.
#define MV_REFINE_PREC_BITS 4  // (1/16-pel)
void av1_opfl_mv_refinement_highbd(const uint16_t *p0, int pstride0,
                                   const uint16_t *p1, int pstride1,
                                   const int16_t *gx0, const int16_t *gy0,
                                   const int16_t *gx1, const int16_t *gy1,
                                   int gstride, int bw, int bh, int d0, int d1,
                                   int grad_prec_bits, int mv_prec_bits,
                                   int *vx0, int *vy0, int *vx1, int *vy1);

void av1_opfl_build_inter_predictor(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    InterPredParams *inter_pred_params,
    CalcSubpelParamsFunc calc_subpel_params_func, int ref, uint16_t *pred_dst);

static INLINE int is_opfl_refine_allowed(const AV1_COMMON *cm,
                                         const MB_MODE_INFO *mbmi) {
  if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_NONE ||
      cm->features.opfl_refine_type == REFINE_NONE)
    return 0;
  const unsigned int cur_index = cm->cur_frame->order_hint;
  int d0, d1;
#if CONFIG_OPTFLOW_ON_TIP
  if (mbmi->ref_frame[0] == TIP_FRAME) {
    d0 = cm->tip_ref.ref_offset[0];
    d1 = cm->tip_ref.ref_offset[1];
  } else {
#endif  // CONFIG_OPTFLOW_ON_TIP
    if (!mbmi->ref_frame[1]) return 0;
    const RefCntBuffer *const ref0 = get_ref_frame_buf(cm, mbmi->ref_frame[0]);
    const RefCntBuffer *const ref1 = get_ref_frame_buf(cm, mbmi->ref_frame[1]);
    d0 = (int)cur_index - (int)ref0->order_hint;
    d1 = (int)cur_index - (int)ref1->order_hint;
#if CONFIG_OPTFLOW_ON_TIP
  }
#endif  // CONFIG_OPTFLOW_ON_TIP
  if (!((d0 <= 0) ^ (d1 <= 0))) return 0;

  return OPFL_DIST_RATIO_THR == 0 ||
         (AOMMAX(abs(d0), abs(d1)) <=
          OPFL_DIST_RATIO_THR * AOMMIN(abs(d0), abs(d1)));
}

// Generate refined MVs using optflow refinement
int av1_get_optflow_based_mv_highbd(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mbmi,
    int_mv *mv_refined, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func, int16_t *gx0, int16_t *gy0,
    int16_t *gx1, int16_t *gy1, int *vx0, int *vy0, int *vx1, int *vy1,
    uint16_t *dst0, uint16_t *dst1
#if CONFIG_OPTFLOW_ON_TIP
    ,
    int do_pred, int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
);

// With the refined MVs, generate the inter prediction for the block.
void av1_opfl_rebuild_inter_predictor(
    uint16_t *dst, int dst_stride, int plane, int_mv *const mv_refined,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
    ,
    int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
);

// Integer division based on lookup table.
// num: numerator
// den: denominator
// out: output result (num / den)
static INLINE int32_t divide_and_round_signed(int64_t num, int64_t den) {
  if (llabs(den) == 1) return (int32_t)(den < 0 ? -num : num);
  const int optflow_prec_bits = 16;
  int16_t shift;
  const int sign_den = (den < 0 ? -1 : 1);
  uint16_t inverse_den = resolve_divisor_64(llabs(den), &shift);
  shift -= optflow_prec_bits;
  if (shift < 0) {
    inverse_den <<= (-shift);
    shift = 0;
  }
  int32_t out;
  // Make sure 1) the bits for right shift is < 63 and 2) the bit depth
  // of num is < 48 to avoid overflow in num * inverse_den
  if (optflow_prec_bits + shift >= 63 ||
      ROUND_POWER_OF_TWO_SIGNED_64(num, 63 - optflow_prec_bits) != 0) {
    int64_t out_tmp = ROUND_POWER_OF_TWO_SIGNED_64(num, optflow_prec_bits);
    out = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(
        out_tmp * (int64_t)inverse_den * sign_den, shift);
  } else {
    out = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(
        num * (int64_t)inverse_den * sign_den, optflow_prec_bits + shift);
  }
#ifndef NDEBUG
  // Verify that the result is consistent with built-in division.
  // Quick overflow check
  int32_t out_div = (llabs(num) + llabs(den) < 0)
                        ? (int32_t)DIVIDE_AND_ROUND_SIGNED(
                              ROUND_POWER_OF_TWO_SIGNED_64(num, 2),
                              ROUND_POWER_OF_TWO_SIGNED_64(den, 2))
                        : (int32_t)DIVIDE_AND_ROUND_SIGNED(num, den);
  // check if error is at most 1 at usable values of out_div
  if (abs(out_div - out) > 1 && abs(out_div) <= 64) {
    printf("Warning: num = %" PRId64 ", den = %" PRId64
           ", inverse_den = %d, shift = %d, v0 = %d, v = %d\n",
           num, den, inverse_den, shift, out_div, out);
  }
#endif  // NDEBUG
  return out;
}

// Return 1 if current frame is REFINE_ALL and the current block uses optical
// flow refinement, i.e., inter mode is in {NEAR_NEARMV, NEAR_NEWMV,
// NEW_NEARMV, NEW_NEWMV}, and compound type is simple compound average.
static INLINE int use_opfl_refine_all(const AV1_COMMON *cm,
                                      const MB_MODE_INFO *mbmi) {
  return cm->features.opfl_refine_type == REFINE_ALL &&
         mbmi->mode >= COMP_INTER_MODE_START &&
         mbmi->mode < COMP_OPTFLOW_MODE_START &&
         mbmi->mode != GLOBAL_GLOBALMV &&
         mbmi->interinter_comp.type == COMPOUND_AVERAGE;
}
#endif  // CONFIG_OPTFLOW_REFINEMENT

// TODO(jkoleszar): yet another mv clamping function :-(
static INLINE MV clamp_mv_to_umv_border_sb(const MACROBLOCKD *xd,
                                           const MV *src_mv, int bw, int bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                           int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                           int ss_x, int ss_y) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (AOM_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (AOM_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
#if CONFIG_OPTFLOW_REFINEMENT
  MV clamped_mv;
  if (use_optflow_refinement) {
    // optflow refinement always returns MVs with 1/16 precision so it is not
    // necessary to shift the MV before clamping
    clamped_mv.row = (int16_t)ROUND_POWER_OF_TWO_SIGNED(
        src_mv->row * (1 << SUBPEL_BITS), MV_REFINE_PREC_BITS + ss_y);
    clamped_mv.col = (int16_t)ROUND_POWER_OF_TWO_SIGNED(
        src_mv->col * (1 << SUBPEL_BITS), MV_REFINE_PREC_BITS + ss_x);
  } else {
    clamped_mv.row = (int16_t)(src_mv->row * (1 << (1 - ss_y)));
    clamped_mv.col = (int16_t)(src_mv->col * (1 << (1 - ss_x)));
  }
#else
  MV clamped_mv = { (int16_t)(src_mv->row * (1 << (1 - ss_y))),
                    (int16_t)(src_mv->col * (1 << (1 - ss_x))) };
#endif  // CONFIG_OPTFLOW_REFINEMENT
  assert(ss_x <= 1);
  assert(ss_y <= 1);
  const SubpelMvLimits mv_limits = {
    xd->mb_to_left_edge * (1 << (1 - ss_x)) - spel_left,
    xd->mb_to_right_edge * (1 << (1 - ss_x)) + spel_right,
    xd->mb_to_top_edge * (1 << (1 - ss_y)) - spel_top,
    xd->mb_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom
  };

  clamp_mv(&clamped_mv, &mv_limits);

  return clamped_mv;
}

static INLINE int64_t scaled_buffer_offset(int x_offset, int y_offset,
                                           int stride,
                                           const struct scale_factors *sf) {
  const int x =
      sf ? sf->scale_value_x(x_offset, sf) >> SCALE_EXTRA_BITS : x_offset;
  const int y =
      sf ? sf->scale_value_y(y_offset, sf) >> SCALE_EXTRA_BITS : y_offset;
  return (int64_t)y * stride + x;
}

static INLINE void setup_pred_plane(struct buf_2d *dst, BLOCK_SIZE bsize,
                                    uint16_t *src, int width, int height,
                                    int stride, int mi_row, int mi_col,
                                    const struct scale_factors *scale,
                                    int subsampling_x, int subsampling_y) {
  // Offset the buffer pointer
  if (subsampling_y && (mi_row & 0x01) && (mi_size_high[bsize] == 1))
    mi_row -= 1;
  if (subsampling_x && (mi_col & 0x01) && (mi_size_wide[bsize] == 1))
    mi_col -= 1;

  const int x = (MI_SIZE * mi_col) >> subsampling_x;
  const int y = (MI_SIZE * mi_row) >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->buf0 = src;
  dst->width = width;
  dst->height = height;
  dst->stride = stride;
}

void av1_setup_dst_planes(struct macroblockd_plane *planes, BLOCK_SIZE bsize,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const int plane_start, const int plane_end);

void av1_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf, const int num_planes);

static INLINE void set_default_interp_filters(
    MB_MODE_INFO *const mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
    const AV1_COMMON *cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    InterpFilter frame_interp_filter) {

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode) {
    mbmi->interp_fltr = MULTITAP_SHARP;
    return;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
#if CONFIG_OPTFLOW_REFINEMENT
  mbmi->interp_fltr =
      (mbmi->mode >= NEAR_NEARMV_OPTFLOW || use_opfl_refine_all(cm, mbmi))
          ? MULTITAP_SHARP
          : av1_unswitchable_filter(frame_interp_filter);
#else
  mbmi->interp_fltr = av1_unswitchable_filter(frame_interp_filter);
#endif  // CONFIG_OPTFLOW_REFINEMENT
}

static INLINE int av1_is_interp_needed(const AV1_COMMON *const cm,
                                       const MACROBLOCKD *const xd) {
  (void)cm;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->skip_mode) return 0;

#if CONFIG_WARPMV
  if (mbmi->mode == WARPMV) return 0;
#endif  // CONFIG_WARPMV

#if CONFIG_OPTFLOW_REFINEMENT
  // No interpolation filter search when optical flow MV refinement is used.
  if (mbmi->mode >= NEAR_NEARMV_OPTFLOW || use_opfl_refine_all(cm, mbmi))
    return 0;
#endif  // CONFIG_OPTFLOW_REFINEMENT
  if (is_warp_mode(mbmi->motion_mode)) return 0;
  if (is_nontrans_global_motion(xd, xd->mi[0])) return 0;
  return 1;
}

// Sets up buffers 'dst_buf1' and 'dst_buf2' from relevant buffers in 'xd' for
// subsequent use in OBMC prediction.
void av1_setup_obmc_dst_bufs(MACROBLOCKD *xd, uint16_t **dst_buf1,
                             uint16_t **dst_buf2);

void av1_setup_build_prediction_by_above_pred(
    MACROBLOCKD *xd, int rel_mi_col, uint8_t above_mi_width,
    MB_MODE_INFO *above_mbmi, struct build_prediction_ctxt *ctxt,
    const int num_planes);
void av1_setup_build_prediction_by_left_pred(MACROBLOCKD *xd, int rel_mi_row,
                                             uint8_t left_mi_height,
                                             MB_MODE_INFO *left_mbmi,
                                             struct build_prediction_ctxt *ctxt,
                                             const int num_planes);
void av1_build_obmc_inter_prediction(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                     uint16_t *above[MAX_MB_PLANE],
                                     int above_stride[MAX_MB_PLANE],
                                     uint16_t *left[MAX_MB_PLANE],
                                     int left_stride[MAX_MB_PLANE]);

const uint8_t *av1_get_obmc_mask(int length);
void av1_count_overlappable_neighbors(const AV1_COMMON *cm, MACROBLOCKD *xd);

#define MASK_MASTER_SIZE ((MAX_WEDGE_SIZE) << 1)
#define MASK_MASTER_STRIDE (MASK_MASTER_SIZE)

void av1_init_wedge_masks();

static INLINE const uint8_t *av1_get_contiguous_soft_mask(int8_t wedge_index,
                                                          int8_t wedge_sign,
                                                          BLOCK_SIZE sb_type) {
  return av1_wedge_params_lookup[sb_type].masks[wedge_sign][wedge_index];
}

const uint8_t *av1_get_compound_type_mask(
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type);

// build interintra_predictors for one plane
void av1_build_interintra_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t *pred, int stride,
                                    const BUFFER_SET *ctx, int plane,
                                    BLOCK_SIZE bsize);

void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd,
                                               BLOCK_SIZE bsize, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride);

void av1_combine_interintra(MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane,
                            const uint16_t *inter_pred, int inter_stride,
                            const uint16_t *intra_pred, int intra_stride);

int av1_allow_warp(const MB_MODE_INFO *const mbmi,
                   const WarpTypesAllowed *const warp_types,
                   const WarpedMotionParams *const gm_params,
#if CONFIG_EXTENDED_WARP_PREDICTION
                   int ref,
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
                   int build_for_obmc, const struct scale_factors *const sf,
                   WarpedMotionParams *final_warp_params);

#if CONFIG_FLEX_MVRES
// derive the context of the mpp_flag
int av1_get_mpp_flag_context(const AV1_COMMON *cm, const MACROBLOCKD *xd);

// derive the context of the precision signaling
int av1_get_pb_mv_precision_down_context(const AV1_COMMON *cm,
                                         const MACROBLOCKD *xd);

// derive the context of the mv class
int av1_get_mv_class_context(const MvSubpelPrecision pb_mv_precision);

// set the precision of a block to the precision
void set_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision);
#if BUGFIX_AMVD_AMVR
void set_amvd_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision);
#endif  // BUGFIX_AMVD_AMVR

// set the most probable mv precision of the block
// Currently, the most probable MV precision is same as the maximum precision of
// the block.
void set_most_probable_mv_precision(const AV1_COMMON *const cm,
                                    MB_MODE_INFO *mbmi, const BLOCK_SIZE bsize);

// Set the default value fo the precision set. Currently the value is always 0.
void set_default_precision_set(const AV1_COMMON *const cm, MB_MODE_INFO *mbmi,
                               const BLOCK_SIZE bsize);

// Set the precision set of the block. Currently, the value is 0.
void set_precision_set(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                       MB_MODE_INFO *mbmi, const BLOCK_SIZE bsize,
                       uint8_t ref_mv_idx);
// Get the index of the precision
// this index is signalled when precision is not same as the most probable
// precision
int av1_get_pb_mv_precision_index(const MB_MODE_INFO *mbmi);

// get the actual precision value from the signalled index
MvSubpelPrecision av1_get_precision_from_index(MB_MODE_INFO *mbmi,
                                               int precision_idx_coded_value);

// Set the maximum precision to the default value
void set_default_max_mv_precision(MB_MODE_INFO *mbmi,
                                  MvSubpelPrecision precision);

// get the maximum allowed precision value of the block
MvSubpelPrecision av1_get_mbmi_max_mv_precision(const AV1_COMMON *const cm,
                                                const SB_INFO *sbi,
                                                const MB_MODE_INFO *mbmi);

// check if pb_mv_precision is allowed or not
int is_pb_mv_precision_active(const AV1_COMMON *const cm,
                              const MB_MODE_INFO *mbmi, const BLOCK_SIZE bsize);

#endif

#if CONFIG_WARPMV
// check if the WARPMV mode is allwed for a given blocksize
static INLINE int is_warpmv_allowed_bsize(BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  return AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8;
}

// check if WARPMV mode is allowed
static INLINE int is_warpmv_mode_allowed(const AV1_COMMON *const cm,
                                         const MB_MODE_INFO *mbmi,
                                         BLOCK_SIZE bsize) {
  if (has_second_ref(mbmi) || !cm->features.enabled_motion_modes
#if CONFIG_TIP
      || is_tip_ref_frame(mbmi->ref_frame[0])
#endif  // CONFIG_TIP
  )
    return 0;

  int frame_warp_delta_allowed =
      cm->features.enabled_motion_modes & (1 << WARP_DELTA);
  return frame_warp_delta_allowed && is_warpmv_allowed_bsize(bsize);
}
#endif  // CONFIG_WARPMV

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_RECONINTER_H_
