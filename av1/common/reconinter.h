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

#if CONFIG_WEDGE_MOD_EXT
#include "av1/encoder/block.h"
#endif

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

#if !CONFIG_WEDGE_MOD_EXT
#define MAX_WEDGE_TYPES 16
#endif

#if CONFIG_WEDGE_MOD_EXT
#define MAX_WEDGE_SIZE_LOG2 6  // 64x64
#else
#define MAX_WEDGE_SIZE_LOG2 5  // 32x32
#endif
#define MAX_WEDGE_SIZE (1 << MAX_WEDGE_SIZE_LOG2)
#define MAX_WEDGE_SQUARE (MAX_WEDGE_SIZE * MAX_WEDGE_SIZE)

#define WEDGE_WEIGHT_BITS 6

#define WEDGE_NONE -1

#if CONFIG_MORPH_PRED
#define MORPH_FIT_SHIFT 8
#define TEMPLATE_SIZE 1
#endif  // CONFIG_MORPH_PRED

#if CONFIG_WEDGE_MOD_EXT
static const int wedge_angle_dist_2_index[WEDGE_ANGLES][NUM_WEDGE_DIST] = {
  { -1, 0, 1, 2 },     // WEDGE_0
  { 3, 4, 5, 6 },      // WEDGE_14
  { 7, 8, 9, 10 },     // WEDGE_27
  { 11, 12, 13, 14 },  // WEDGE_45
  { 15, 16, 17, 18 },  // WEDGE_63
  { -1, 19, 20, 21 },  // WEDGE_90
  { 22, 23, 24, 25 },  // WEDGE_117
  { 26, 27, 28, 29 },  // WEDGE_135
  { 30, 31, 32, 33 },  // WEDGE_153
  { 34, 35, 36, 37 },  // WEDGE_166
  { -1, 38, 39, 40 },  // WEDGE_180
  { -1, 41, 42, 43 },  // WEDGE_194
  { -1, 44, 45, 46 },  // WEDGE_207
  { -1, 47, 48, 49 },  // WEDGE_225
  { -1, 50, 51, 52 },  // WEDGE_243
  { -1, 53, 54, 55 },  // WEDGE_270
  { -1, 56, 57, 58 },  // WEDGE_297
  { -1, 59, 60, 61 },  // WEDGE_315
  { -1, 62, 63, 64 },  // WEDGE_333
  { -1, 65, 66, 67 },  // WEDGE_346
};

static const int wedge_index_2_angle[MAX_WEDGE_TYPES] = {
  WEDGE_0,   WEDGE_0,   WEDGE_0,               // WEDGE_0
  WEDGE_14,  WEDGE_14,  WEDGE_14,  WEDGE_14,   // WEDGE_14
  WEDGE_27,  WEDGE_27,  WEDGE_27,  WEDGE_27,   // WEDGE_27
  WEDGE_45,  WEDGE_45,  WEDGE_45,  WEDGE_45,   // WEDGE_45
  WEDGE_63,  WEDGE_63,  WEDGE_63,  WEDGE_63,   // WEDGE_63
  WEDGE_90,  WEDGE_90,  WEDGE_90,              // WEDGE_90
  WEDGE_117, WEDGE_117, WEDGE_117, WEDGE_117,  // WEDGE_117
  WEDGE_135, WEDGE_135, WEDGE_135, WEDGE_135,  // WEDGE_135
  WEDGE_153, WEDGE_153, WEDGE_153, WEDGE_153,  // WEDGE_153
  WEDGE_166, WEDGE_166, WEDGE_166, WEDGE_166,  // WEDGE_166
  WEDGE_180, WEDGE_180, WEDGE_180,             // WEDGE_180
  WEDGE_194, WEDGE_194, WEDGE_194,             // WEDGE_194
  WEDGE_207, WEDGE_207, WEDGE_207,             // WEDGE_207
  WEDGE_225, WEDGE_225, WEDGE_225,             // WEDGE_225
  WEDGE_243, WEDGE_243, WEDGE_243,             // WEDGE_243
  WEDGE_270, WEDGE_270, WEDGE_270,             // WEDGE_270
  WEDGE_297, WEDGE_297, WEDGE_297,             // WEDGE_297
  WEDGE_315, WEDGE_315, WEDGE_315,             // WEDGE_315
  WEDGE_333, WEDGE_333, WEDGE_333,             // WEDGE_333
  WEDGE_346, WEDGE_346, WEDGE_346              // WEDGE_346
};

static const int wedge_index_2_dist[MAX_WEDGE_TYPES] = {
  1, 2, 3,     // WEDGE_0
  0, 1, 2, 3,  // WEDGE_14
  0, 1, 2, 3,  // WEDGE_27
  0, 1, 2, 3,  // WEDGE_45
  0, 1, 2, 3,  // WEDGE_63
  1, 2, 3,     // WEDGE_90
  0, 1, 2, 3,  // WEDGE_117
  0, 1, 2, 3,  // WEDGE_135
  0, 1, 2, 3,  // WEDGE_153
  0, 1, 2, 3,  // WEDGE_166
  1, 2, 3,     // WEDGE_180
  1, 2, 3,     // WEDGE_194
  1, 2, 3,     // WEDGE_207
  1, 2, 3,     // WEDGE_225
  1, 2, 3,     // WEDGE_243
  1, 2, 3,     // WEDGE_270
  1, 2, 3,     // WEDGE_297
  1, 2, 3,     // WEDGE_315
  1, 2, 3,     // WEDGE_333
  1, 2, 3,     // WEDGE_346
};
#endif  // CONFIG_WEDGE_MOD_EXT

#if CONFIG_BAWP
#define BAWP_REF_LINES 1
#endif

// Angles are with respect to horizontal anti-clockwise
#if !CONFIG_WEDGE_MOD_EXT
enum {
  WEDGE_HORIZONTAL = 0,
  WEDGE_VERTICAL = 1,
  WEDGE_OBLIQUE27 = 2,
  WEDGE_OBLIQUE63 = 3,
  WEDGE_OBLIQUE117 = 4,
  WEDGE_OBLIQUE153 = 5,
  WEDGE_DIRECTIONS
} UENUM1BYTE(WedgeDirectionType);
#endif

// 3-tuple: {direction, x_offset, y_offset}
typedef struct {
  WedgeDirectionType direction;
  int x_offset;
  int y_offset;
} wedge_code_type;

typedef uint8_t *wedge_masks_type[MAX_WEDGE_TYPES];
#if CONFIG_WEDGE_TMVP
typedef uint8_t *wedge_decisions_type[MAX_WEDGE_TYPES];
#endif  // CONFIG_WEDGE_TMVP

typedef struct {
  int wedge_types;
  const wedge_code_type *codebook;
  uint8_t *signflip;
  wedge_masks_type *masks;
#if CONFIG_WEDGE_TMVP
  wedge_decisions_type *tmvp_mv_decisions;
#endif  // CONFIG_WEDGE_TMVP
} wedge_params_type;

extern const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL];

typedef struct SubpelParams {
  int xs;
  int ys;
  int subpel_x;
  int subpel_y;
#if CONFIG_D071_IMP_MSK_BLD
  int x0;  // top left sample horizontal cood.
  int y0;  // top left sample vertical cood.
  int x1;  // x0 + bw
  int y1;  // y0 + bh
#endif     // CONFIG_D071_IMP_MSK_BLD

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

#if CONFIG_REFINEMV
#define REFINE_MV_MAX_OFFSET 1
#define REF_TOP_BORDER (AOM_INTERP_EXTEND - 1 + REFINE_MV_MAX_OFFSET)
#define REF_LEFT_BORDER (AOM_INTERP_EXTEND - 1 + REFINE_MV_MAX_OFFSET)
#define REF_RIGHT_BORDER (AOM_INTERP_EXTEND + REFINE_MV_MAX_OFFSET)
#define REF_BOTTOM_BORDER (AOM_INTERP_EXTEND + REFINE_MV_MAX_OFFSET)
#endif  // CONFIG_REFINEMV

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
  // In optical flow refinement, block_width and block_height will pass the
  // subblock size into av1_make_inter_predictor, while orig_block_width and
  // orig_block_height keep the original block size that is needed by
  // calc_subpel_params_func
  int orig_block_width;
  int orig_block_height;

#if CONFIG_REFINEMV
  // In refinemV, the prediction is generated maximum 16x16 sub-block basis
  // original_pu_width and  original_pu_height represents the width and height
  // of the original block.
  int original_pu_width;
  int original_pu_height;
#endif  // CONFIG_REFINEMV

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
  /**
   * \name Distance of TIP block from frame edges in 1/8th pixel units.
   */
  /**@{*/
  int dist_to_left_edge;   /*!< Distance from left edge */
  int dist_to_right_edge;  /*!< Distance from right edge */
  int dist_to_top_edge;    /*!< Distance from top edge */
  int dist_to_bottom_edge; /*!< Distance from bottom edge */

#if CONFIG_REFINEMV
  int use_ref_padding;
  ReferenceArea *ref_area;
#endif  // CONFIG_REFINEMV

#if CONFIG_D071_IMP_MSK_BLD
  INTERINTER_COMPOUND_BORDER_DATA border_data;
#endif  // CONFIG_D071_IMP_MSK_BLD
} InterPredParams;

// Apply bilinear and bicubic interpolation for subpel gradient to avoid
// calls of build_one_inter_predictor function. Bicubic interpolation
// brings better quality but the speed results are neutral. As such, bilinear
// interpolation is used by default for a better trade-off between quality
// and complexity.
#define OPFL_BILINEAR_GRAD 0
#define OPFL_BICUBIC_GRAD 1

// Use downsampled gradient arrays to compute MV offsets
#define OPFL_DOWNSAMP_QUINCUNX 0

// Delta to use for computing gradients in bits, with 0 referring to
// integer-pel. The actual delta value used from the 1/8-pel original MVs
// is 2^(3 - SUBPEL_GRAD_DELTA_BITS). The max value of this macro is 3.
#define SUBPEL_GRAD_DELTA_BITS 2

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

void av1_init_inter_params(InterPredParams *inter_pred_params, int block_width,
                           int block_height, int pix_row, int pix_col,
                           int subsampling_x, int subsampling_y, int bit_depth,
                           int is_intrabc, const struct scale_factors *sf,
                           const struct buf_2d *ref_buf,
                           InterpFilter interp_filter);

// Get the step size and maximum coded index of the warp block
static INLINE void get_warp_model_steps(const MB_MODE_INFO *mbmi,
                                        int *step_size, int *max_coded_index) {
#if CONFIG_WARP_PRECISION
  int step_size_log2 = mbmi->warp_precision_idx == 0 ? 11 : 10;
  *step_size = 1 << step_size_log2;
  *max_coded_index = mbmi->warp_precision_idx == 0 ? 7 : 14;
#else
  (void)mbmi;
  *step_size = (1 << WARP_DELTA_STEP_BITS);
  *max_coded_index = WARP_DELTA_CODED_MAX;
#endif  // CONFIG_WARP_PRECISION
}

#if CONFIG_SIX_PARAM_WARP_DELTA
// Get the default value of the six_param_flag
static INLINE int get_default_six_param_flag(const AV1_COMMON *const cm,
                                             const MB_MODE_INFO *mbmi) {
  return (cm->seq_params.enable_six_param_warp_delta && mbmi->warp_ref_idx == 0)
             ? 1
             : 0;
}
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
// Check if the signaling of the warp delta parameters are allowed
static INLINE int allow_warp_parameter_signaling(const AV1_COMMON *const cm,
                                                 const MB_MODE_INFO *mbmi) {
  // Warp delta parameters are signalled in following two cases
  // Case0: sequence level enable_six_param_warp_delta is enabled AND
  // (mbmi->warp_ref_idx == 0), in this case 6-parameter delta is signaled.
  // Case1: (mbmi->warp_ref_idx == 1), in this case 4-parameter delta is
  // signaled.

  int allow_delta_for_this_warp_ref_idx =
#if CONFIG_SIX_PARAM_WARP_DELTA
      get_default_six_param_flag(cm, mbmi) ||  // 6-parameter
#endif                                         // CONFIG_SIX_PARAM_WARP_DELTA
      (mbmi->warp_ref_idx == 1);               // 4-parameter

  return (mbmi->mode != WARPMV && cm->features.allow_warpmv_mode &&
          mbmi->motion_mode == WARP_DELTA && allow_delta_for_this_warp_ref_idx);
}

// Map the index to weighting factor for compound weighted prediction
static INLINE int get_cwp_coding_idx(int val, int encode,
                                     const AV1_COMMON *const cm,
                                     const MB_MODE_INFO *const mbmi) {
  int is_same_side = 0;
  int cur_ref_side = 0;
  int other_ref_side = 0;
  if (has_second_ref(mbmi)) {
    cur_ref_side = cm->ref_frame_side[mbmi->ref_frame[0]];
    other_ref_side = cm->ref_frame_side[mbmi->ref_frame[1]];

    is_same_side = (cur_ref_side > 0 && other_ref_side > 0) ||
                   (cur_ref_side == 0 && other_ref_side == 0);
  }

  if (encode) {
    for (int i = 0; i < MAX_CWP_NUM; i++) {
      if (cwp_weighting_factor[is_same_side][i] == val) return i;
    }
    return 0;
  } else {
    return cwp_weighting_factor[is_same_side][val];
  }
}

static INLINE int enable_adaptive_mvd_resolution(const AV1_COMMON *const cm,
                                                 const MB_MODE_INFO *mbmi) {
  const int mode = mbmi->mode;

  return (mode == NEAR_NEWMV || mode == NEW_NEARMV ||
          mode == NEAR_NEWMV_OPTFLOW || mode == NEW_NEARMV_OPTFLOW ||
          mode == JOINT_AMVDNEWMV_OPTFLOW || mode == AMVDNEWMV ||
          mode == JOINT_AMVDNEWMV) &&
         cm->seq_params.enable_adaptive_mvd;
}

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
    int bd, int is_intrabc) {
  assert(conv_params->do_average == 0 || conv_params->do_average == 1);
  const int is_scaled = has_scale(subpel_params->xs, subpel_params->ys);
  if (is_scaled) {
    assert(is_intrabc == 0);
    av1_highbd_convolve_2d_facade(
        src, src_stride, dst, dst_stride, w, h, interp_filters,
        subpel_params->subpel_x, subpel_params->xs, subpel_params->subpel_y,
        subpel_params->ys, 1, conv_params, bd, is_intrabc);
  } else {
    SubpelParams sp = *subpel_params;
    revert_scale_extra_bits(&sp);
    av1_highbd_convolve_2d_facade(
        src, src_stride, dst, dst_stride, w, h, interp_filters, sp.subpel_x,
        sp.xs, sp.subpel_y, sp.ys, 0, conv_params, bd, is_intrabc);
  }
}

void av1_modify_neighbor_predictor_for_obmc(MB_MODE_INFO *mbmi);
int av1_skip_u4x4_pred_in_obmc(BLOCK_SIZE bsize,
                               const struct macroblockd_plane *pd, int dir);

static INLINE int is_interinter_compound_used(COMPOUND_TYPE type,
                                              BLOCK_SIZE sb_type) {
  const int comp_allowed = is_comp_ref_allowed(sb_type);
  switch (type) {
#if CONFIG_COMPOUND_4XN
    case COMPOUND_AVERAGE: return comp_allowed;
    case COMPOUND_DIFFWTD:
      return comp_allowed && !is_thin_4xn_nx4_block(sb_type);
#else
    case COMPOUND_AVERAGE:
    case COMPOUND_DIFFWTD: return comp_allowed;
#endif  //  CONFIG_COMPOUND_4XN

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
                                     int ref, int use_optflow_refinement,
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
#if CONFIG_REFINEMV
                                int build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
                                int build_for_obmc,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
                                int build_for_decode,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
                                int bw, int bh, int mi_x, int mi_y,
                                uint16_t **mc_buf,
                                CalcSubpelParamsFunc calc_subpel_params_func);

// Precision of refined MV returned, 0 being integer pel. For now, only 1/8 or
// 1/16-pel can be used.
#define MV_REFINE_PREC_BITS 4  // (1/16-pel)

// Apply regularized least squares (RLS). The RLS parameter is bw * bh * 2^(b-4)
// where b = OPFL_RLS_PARAM.
#define OPFL_REGULARIZED_LS 1
#define OPFL_RLS_PARAM 16

// Number of bits allowed for all intermediate results of covariance matrix
// filling
#define MAX_OPFL_AUTOCORR_BITS 28
// Clamp range for u/v/w. If it uses h unsigned bits, then u2/v2 uses 2h
// unsigned bits. Every sum of 8 u2/v2 use at most 2h+3 unsigned bits, and
// must not exceed max bd of su2/sv2 minus 2. Thus, 2h+3 <= H-2
#define OPFL_SAMP_CLAMP_VAL ((1 << ((MAX_OPFL_AUTOCORR_BITS - 6) >> 1)) - 1)

void av1_opfl_build_inter_predictor(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    InterPredParams *inter_pred_params,
    CalcSubpelParamsFunc calc_subpel_params_func, int ref, uint16_t *pred_dst
#if CONFIG_REFINEMV
    ,
    const MV *const src_mv, int pu_width, int pu_height
#endif  // CONFIG_REFINEMV
);

// Generate refined MVs using optflow refinement
void av1_get_optflow_based_mv(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mbmi,
    int_mv *mv_refined, int bw, int bh, int mi_x, int mi_y,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
    int build_for_decode,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
    uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func,
    int16_t *gx0, int16_t *gy0, int16_t *gx1, int16_t *gy1,
#if CONFIG_AFFINE_REFINEMENT
    WarpedMotionParams *wms, int *use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
    int *vx0, int *vy0, int *vx1, int *vy1, uint16_t *dst0, uint16_t *dst1
#if CONFIG_OPTFLOW_ON_TIP
    ,
    int do_pred, int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
#if CONFIG_REFINEMV
    ,
    MV *best_mv_ref, int pu_width, int pu_height
#endif  // CONFIG_REFINEMV
);

// With the refined MVs, generate the inter prediction for the block.
void av1_opfl_rebuild_inter_predictor(
    uint16_t *dst, int dst_stride, int plane, int_mv *const mv_refined,
    int *vxy_bufs, const int vxy_size, InterPredParams *inter_pred_params,
    MACROBLOCKD *xd, int mi_x, int mi_y,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
    int build_for_decode,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
#if CONFIG_AFFINE_REFINEMENT
    const AV1_COMMON *cm, int pu_width, CompoundRefineType comp_refine_type,
    WarpedMotionParams *wms, int_mv *mv, const int use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
    ,
    int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
);

// We consider this tunable number K=MAX_LS_BITS-1 (sign bit excluded)
// as the target maximum bit depth of all intermediate results for LS problem.
#define MAX_LS_BITS 32

// Divide all elements of a vector by a common factor, and apply shifts.
// The integer division is based on lookup table.
// sol: numerator (will be updated to the solution)
// den: denominator
// out: output result (sol / den)
// TODO(kslu) reduce input bit depth to int32_t
static INLINE void divide_and_round_array(int64_t *sol, int64_t den,
                                          const int dim, int *shifts) {
  assert(den != 0);
  if (den < 0) {
    for (int i = 0; i < dim; i++) sol[i] = -sol[i];
    divide_and_round_array(sol, -den, dim, shifts);
    return;
  }
  // TODO(kslu) use resolve_divisor_32
  int16_t den_shift = 0;
  int16_t inv_den = (den == 1) ? 1 : resolve_divisor_64(den, &den_shift);
  int inv_den_msb = get_msb_signed(inv_den);

  // Apply shifts to sol[i] and den to keep both bit depths within K.
  for (int i = 0; i < dim; i++) {
    if (sol[i] == 0) continue;
    int sign = sol[i] > 0;
    sol[i] = sign ? sol[i] : -sol[i];
    int num_red_bits =
        AOMMAX(0, get_msb_signed_64(sol[i]) + inv_den_msb + 1 - MAX_LS_BITS);
    if (num_red_bits > 0)
      sol[i] = ROUND_POWER_OF_TWO_SIGNED_64(sol[i], num_red_bits);

    int inc_bits = shifts[i] + num_red_bits - den_shift;
    if (inc_bits >= 0)
      sol[i] = sol[i] * inv_den * (1 << inc_bits);
    else
      sol[i] = ROUND_POWER_OF_TWO_SIGNED_64(sol[i] * inv_den, -inc_bits);
    sol[i] = sign ? sol[i] : -sol[i];
  }
}

#if CONFIG_AFFINE_REFINEMENT || CONFIG_E125_MHCCP_SIMPLIFY
// This function is a stable version of ROUND_POWER_OF_TWO_SIGNED(a*b, shift),
// where shifts are partially applied before multiplcation operations to avoid
// overflow issues, i.e., (a>>s1)*(b>>s2)>>s3, where s1+s2+s3=shift
int64_t stable_mult_shift(const int64_t a, const int64_t b, const int shift,
                          const int msb_a, const int msb_b, const int max_bd,
                          int *rem_shift);
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_E125_MHCCP_SIMPLIFY

#if CONFIG_AFFINE_REFINEMENT
int solver_4d(int64_t *mat, int64_t *vec, int *precbits, int64_t *sol);
void av1_avg_pooling_pdiff_gradients_c(int16_t *pdiff, const int pstride,
                                       int16_t *gx, int16_t *gy,
                                       const int gstride, const int bw,
                                       const int bh, const int n);
void av1_calc_affine_autocorrelation_matrix_c(const int16_t *pdiff, int pstride,
                                              const int16_t *gx,
                                              const int16_t *gy, int gstride,
                                              int bw, int bh,
#if CONFIG_AFFINE_REFINEMENT_SB
                                              int x_offset, int y_offset,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
                                              int64_t *mat_a, int64_t *vec_b);

#define AFFINE_OPFL_BASED_ON_SAD 1
#define AFFINE_FAST_ENC_SEARCH 1

// Method to refine chroma in DAMR
// 0: no chroma refinement at all
// 1: affine compound prediction using DAMR warp MVs
// 2: translational compound prediction using DAMR warp MVs, whole block based
// 3: translational compound prediction using DAMR warp MVs, subblock based
#define AFFINE_CHROMA_REFINE_METHOD 1

// Fast intermediate warp prediction
// 0: normal per 8x8 warp prediction
// 1: per 4x4 translational warp (requires CONFIG_EXT_WARP_FILTER)
// 2: per pixel bicubic interpolated warp prediction
// 3: per pixel bilinear interpolated warp prediction
#define AFFINE_FAST_WARP_METHOD 3

// Apply averaging of gradient array to downscale prediction block. It can
// be set to MAX_SB_SIZE_LOG2 to turn off the avg pooling feature.
#define AFFINE_AVG_MAX_SIZE_LOG2 4
#define AFFINE_AVG_MAX_SIZE (1 << AFFINE_AVG_MAX_SIZE_LOG2)

// We consider this tunable number H=MAX_AFFINE_AUTOCORR_BITS-1 (sign bit
// excluded) as the maximum bit depth for autocorrelation matrix filling.
// This value should not be set lower than 25, since gx*x+gy*y can reach 25
// bits given the most extreme case (16+8+1 bits).
#define MAX_AFFINE_AUTOCORR_BITS 32
// Clamp range for a[] and d. If it uses h unsigned bits, then a[s]a[t] uses 2h
// unsigned bits. Every sum of 16 a[s]a[t] use at most 2h+4 unsigned bits, and
// must not exceed max bd of A minus 2. Thus, 2h+4 <= H-2
#define AFFINE_SAMP_CLAMP_VAL \
  ((1L << ((MAX_AFFINE_AUTOCORR_BITS - 7) >> 1)) - 1)
#define AFFINE_COORDS_OFFSET_BITS 2

// Internal bit depths for affine parameter derivation
#define AFFINE_GRAD_BITS_THR 32
#define AFFINE_PREC_BITS 12
#define AFFINE_RLS_PARAM 2

#if AFFINE_FAST_WARP_METHOD == 3
#define BILINEAR_WARP_PREC_BITS 12
#endif  // AFFINE_FAST_WARP_METHOD == 3

static INLINE int is_translational_refinement_allowed(const AV1_COMMON *cm,
#if CONFIG_COMPOUND_4XN
                                                      BLOCK_SIZE bsize,
#endif  // CONFIG_COMPOUND_4XN
                                                      const int mode) {
  assert(cm->seq_params.enable_opfl_refine);
  if (mode < NEAR_NEARMV_OPTFLOW) return 0;
#if CONFIG_COMPOUND_4XN
  if (is_thin_4xn_nx4_block(bsize)) return 0;
#endif  // CONFIG_COMPOUND_4XN

  if (!cm->seq_params.enable_affine_refine) return 1;

  return 0;
}

static INLINE int is_affine_refinement_allowed(const AV1_COMMON *cm,
                                               const MACROBLOCKD *xd,
                                               const int mode) {
  if (!cm->seq_params.enable_affine_refine) return 0;
  if (mode < NEAR_NEARMV) return 0;

  if (cm->features.opfl_refine_type == REFINE_NONE ||
      (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
       mode < NEAR_NEARMV_OPTFLOW))
    return 0;

  const MB_MODE_INFO *mbmi = xd->mi[0];
  if (mbmi->skip_mode && COMP_REFINE_TYPE_FOR_SKIP < COMP_AFFINE_REFINE_START)
    return 0;

#if CONFIG_COMPOUND_4XN
  if (has_second_ref(mbmi) &&
      is_thin_4xn_nx4_block(mbmi->sb_type[xd->tree_type == CHROMA_PART]))
    return 0;
#endif  // CONFIG_COMPOUND_4XN

  return 1;
}

// Return 1 if the second step (subblock based translational refinement) is
// applied.
static INLINE int damr_refine_subblock(int plane, const int bw, const int bh,
                                       CompoundRefineType comp_refine_type,
                                       int opfl_sub_bw, int opfl_sub_bh) {
  if (opfl_sub_bw > bw || opfl_sub_bh > bh) return 0;
  if (plane == 0 && opfl_sub_bw == bw && opfl_sub_bh == bh) return 0;
#if !CONFIG_EXT_WARP_FILTER
  if (opfl_sub_bw < 8 || opfl_sub_bh < 8 || bw < 8 || bh < 8) return 0;
#endif  // !CONFIG_EXT_WARP_FILTER

#if AFFINE_CHROMA_REFINE_METHOD == 2
  if (plane) return 0;
#endif

  if (comp_refine_type < COMP_AFFINE_REFINE_START) return 0;
  return comp_refine_type == COMP_REFINE_ROTZOOM4P_SUBBLK2P;
}

static INLINE int get_allowed_comp_refine_type_mask(const AV1_COMMON *cm,
                                                    const MACROBLOCKD *xd,
                                                    const MB_MODE_INFO *mbmi) {
  if (cm->features.opfl_refine_type == REFINE_ALL &&
      opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                 xd,
#endif  // CONFIG_COMPOUND_4XN
                                 mbmi)) {
    if (cm->seq_params.enable_affine_refine)
      return 1 << COMP_REFINE_TYPE_FOR_REFINE_ALL;
    else
      return 1 << COMP_REFINE_SUBBLK2P;
  }
  if (mbmi->mode < NEAR_NEARMV_OPTFLOW) return 1 << COMP_REFINE_NONE;

  int mask = 0;
  if (is_translational_refinement_allowed(
          cm,
#if CONFIG_COMPOUND_4XN
          mbmi->sb_type[xd->tree_type == CHROMA_PART],
#endif  // CONFIG_COMPOUND_4XN
          mbmi->mode))
    mask |= (1 << COMP_REFINE_SUBBLK2P);

  if (is_affine_refinement_allowed(cm, xd, mbmi->mode)) {
    for (CompoundRefineType r = COMP_AFFINE_REFINE_START; r < COMP_REFINE_TYPES;
         r++)
      mask |= (1 << r);
  }
  // Some kind of refinement must be applied in OPTFLOW modes
  assert(mask != 0);
  return mask;
}
#endif  // CONFIG_AFFINE_REFINEMENT

#if CONFIG_REFINEMV
// Compute the SAD between the two predictors when refinemv is ON
int get_refinemv_sad(uint16_t *src1, uint16_t *src2, int width, int height,
                     int bd);
// Genrate two prediction signals and compute SAD of a given mv0 and mv1
int av1_refinemv_build_predictors_and_get_sad(
    MACROBLOCKD *xd, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst_ref0,
    uint16_t *dst_ref1, MV mv0, MV mv1, InterPredParams *inter_pred_params);

// Get the context index to code refinemv flag
int av1_get_refinemv_context(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             BLOCK_SIZE bsize);

// Full blocks refine MVs are stored in 4x4 grid so that the MVs can be reused
// for chroma
void fill_subblock_refine_mv(REFINEMV_SUBMB_INFO *refinemv_subinfo, int bw,
                             int bh, MV mv0, MV mv1);

// Generate the reference area ( bounding box) based on the signaled MV
void av1_get_reference_area_with_padding(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         int plane, MB_MODE_INFO *mi,
                                         const MV mv[2], int bw, int bh,
                                         int mi_x, int mi_y,
                                         ReferenceArea ref_area[2],
                                         int pu_width, int pu_height);

// Derive the sub-pixel related parameters of non-TIP blocks
// Sub-pel related parameters are stored in the structures pointed by
// "subpel_params" and "block"
void dec_calc_subpel_params(const MV *const src_mv,
                            InterPredParams *const inter_pred_params,
                            const MACROBLOCKD *const xd, int mi_x, int mi_y,
                            uint16_t **pre, SubpelParams *subpel_params,
                            int *src_stride, PadBlock *block,
                            int use_optflow_refinement, MV32 *scaled_mv,
                            int *subpel_x_mv, int *subpel_y_mv);

// check if the refinemv mode is allwed for a given blocksize
static INLINE int is_refinemv_allowed_bsize(BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
#if CONFIG_COMPOUND_4XN
  if (AOMMIN(block_size_wide[bsize], block_size_high[bsize]) < 8) return 0;
#endif  // CONFIG_COMPOUND_4XN

  return (block_size_wide[bsize] >= 16 || block_size_high[bsize] >= 16);
}

#if CONFIG_AFFINE_REFINEMENT
static INLINE int is_damr_allowed_with_refinemv(const PREDICTION_MODE mode) {
  // if (mode == NEAR_NEARMV_OPTFLOW) return 1;
  (void)mode;
  return 0;
}
#endif  // CONFIG_AFFINE_REFINEMENT

// check if the refinemv mode is allwed for a given mode and precision
static INLINE int is_refinemv_allowed_mode_precision(
    PREDICTION_MODE mode, MvSubpelPrecision precision,
    const AV1_COMMON *const cm) {
  (void)precision;
  if (mode == GLOBAL_GLOBALMV) return 0;

#if CONFIG_AFFINE_REFINEMENT
  // Refine MV is allow in JOINT_NEWMV and disallowed in *_OPTFLOW modes
  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      cm->seq_params.enable_affine_refine) {
    if (mode == JOINT_NEWMV) return 1;
  }
#endif  // CONFIG_AFFINE_REFINEMENT

  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      (mode == JOINT_NEWMV || mode == JOINT_AMVDNEWMV || mode == NEAR_NEWMV ||
       mode == NEW_NEARMV || mode == NEW_NEWMV))
    return 0;

#if CONFIG_AFFINE_REFINEMENT
  if (cm->seq_params.enable_affine_refine) {
    if (is_damr_allowed_with_refinemv(mode)) return 1;
    if (cm->features.opfl_refine_type == REFINE_ALL) return 0;
    return mode == NEAR_NEARMV;
  }
#endif
  return (mode >= NEAR_NEARMV && mode <= JOINT_AMVDNEWMV_OPTFLOW);
}

// check if the prediction mode infered to refimemv to always 1.
#if CONFIG_AFFINE_REFINEMENT
static INLINE int default_refinemv_modes(const AV1_COMMON *cm,
                                         const MB_MODE_INFO *mbmi) {
#else
static INLINE int default_refinemv_modes(const MB_MODE_INFO *mbmi) {
#endif
#if CONFIG_AFFINE_REFINEMENT
  if (cm->seq_params.enable_affine_refine) {
    if (is_damr_allowed_with_refinemv(mbmi->mode)) return 1;
    return (
#if !CONFIG_SKIP_MODE_NO_REFINEMENTS
        mbmi->skip_mode ||
#endif  // !CONFIG_SKIP_MODE_NO_REFINEMENTS
        mbmi->mode == NEAR_NEARMV || mbmi->mode == JOINT_NEWMV);
  }
#endif  // CONFIG_AFFINE_REFINEMENT
  return (
#if !CONFIG_SKIP_MODE_NO_REFINEMENTS
      mbmi->skip_mode ||
#endif  // !CONFIG_SKIP_MODE_NO_REFINEMENTS
      mbmi->mode == NEAR_NEARMV || mbmi->mode == NEAR_NEARMV_OPTFLOW ||
      mbmi->mode == JOINT_NEWMV_OPTFLOW);
}
// Check if the compound and equal distance references
static INLINE int is_refinemv_allowed_reference(const AV1_COMMON *cm,
                                                const MB_MODE_INFO *mbmi) {
  if (!cm->seq_params.enable_refinemv) return 0;
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const unsigned int cur_index = cm->cur_frame->display_order_hint;
#else
  const unsigned int cur_index = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  int d0, d1;
  int is_tip = (mbmi->ref_frame[0] == TIP_FRAME);

  if (is_tip) {
    d0 = cm->tip_ref.ref_offset[0];
    d1 = cm->tip_ref.ref_offset[1];
  } else {
    if (!has_second_ref(mbmi)) return 0;
    const RefCntBuffer *const ref0 = get_ref_frame_buf(cm, mbmi->ref_frame[0]);
    const RefCntBuffer *const ref1 = get_ref_frame_buf(cm, mbmi->ref_frame[1]);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    d0 = get_relative_dist(&cm->seq_params.order_hint_info, cur_index,
                           ref0->display_order_hint);
    d1 = get_relative_dist(&cm->seq_params.order_hint_info, cur_index,
                           ref1->display_order_hint);
#else
    d0 = (int)cur_index - (int)ref0->order_hint;
    d1 = (int)cur_index - (int)ref1->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  }

  // reference frame has to be both sides to apply dmvr
  if (!((d0 <= 0) ^ (d1 <= 0))) return 0;

  // Current implementation only supports when both has the same distance
  if (abs(d0) != abs(d1)) return 0;

  return 1;
}

// check if the refinemv mode is allowed for a given block
static INLINE int is_refinemv_allowed(const AV1_COMMON *const cm,
                                      const MB_MODE_INFO *mbmi,
                                      BLOCK_SIZE bsize) {
  if (!cm->seq_params.enable_refinemv ||
      cm->superres_scale_denominator != SCALE_NUMERATOR)
    return 0;
  int is_tip = is_tip_ref_frame(mbmi->ref_frame[0]);
  if (is_tip) return 0;
  assert(!mbmi->skip_mode);
  int is_compound = has_second_ref(mbmi);
  return is_compound && is_refinemv_allowed_bsize(bsize) &&
         is_refinemv_allowed_mode_precision(mbmi->mode, mbmi->pb_mv_precision,
                                            cm) &&
         is_refinemv_allowed_reference(cm, mbmi);
}

// check if the refinemv mode is allowed for a given block for TIP mode
static INLINE int is_refinemv_allowed_tip_blocks(const AV1_COMMON *const cm,
                                                 const MB_MODE_INFO *mbmi) {
  assert(is_tip_ref_frame(mbmi->ref_frame[0]));
  return cm->seq_params.enable_refinemv &&
         cm->superres_scale_denominator == SCALE_NUMERATOR &&
         is_refinemv_allowed_reference(cm, mbmi);
}

// check if the refinemv mode is allowed for a given block for skip mode
static INLINE int is_refinemv_allowed_skip_mode(const AV1_COMMON *const cm,
                                                const MB_MODE_INFO *mbmi) {
#if CONFIG_SKIP_MODE_NO_REFINEMENTS
  (void)cm;
  (void)mbmi;
  return 0;
#else
  assert(mbmi->skip_mode);
#if CONFIG_D072_SKIP_MODE_IMPROVE
  if (mbmi->ref_frame[1] == NONE_FRAME) return 0;
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
  return cm->seq_params.enable_refinemv &&
         cm->superres_scale_denominator == SCALE_NUMERATOR &&
         is_refinemv_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y]) &&
         is_refinemv_allowed_reference(cm, mbmi);
#endif  // CONFIG_SKIP_MODE_NO_REFINEMENTS
}
static INLINE int get_default_refinemv_flag(const AV1_COMMON *const cm,
                                            const MB_MODE_INFO *mbmi) {
  if (!cm->seq_params.enable_refinemv ||
      cm->superres_scale_denominator != SCALE_NUMERATOR)
    return 0;
  int is_refinemv =
      (mbmi->skip_mode
           ? is_refinemv_allowed_skip_mode(cm, mbmi)
           : is_refinemv_allowed(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]));
  if (is_refinemv) {
#if CONFIG_AFFINE_REFINEMENT
    if (default_refinemv_modes(cm, mbmi)) return 1;
#else
    if (default_refinemv_modes(mbmi)) return 1;
#endif  // CONFIG_AFFINE_REFINEMENT
  }
  return 0;
}

// check if the refinemv mode is switchable for a given block
static INLINE int switchable_refinemv_flag(const AV1_COMMON *const cm,
                                           const MB_MODE_INFO *mbmi) {
  if (!cm->seq_params.enable_refinemv) return 0;
  int is_refinemv =
      (mbmi->skip_mode
           ? is_refinemv_allowed_skip_mode(cm, mbmi)
           : is_refinemv_allowed(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]));
  if (is_refinemv && !is_tip_ref_frame(mbmi->ref_frame[0])) {
#if CONFIG_AFFINE_REFINEMENT
    if (default_refinemv_modes(cm, mbmi)) return 0;
#else
    if (default_refinemv_modes(mbmi)) return 0;
#endif  // CONFIG_AFFINE_REFINEMENT
    return 1;
  }

  return 0;
}

// This function conduct the SAD search between two predictors and find the best
// MVs
void apply_mv_refinement(const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                         MB_MODE_INFO *mi, int bw, int bh, int mi_x, int mi_y,
                         uint16_t **mc_buf, const MV mv[2],
                         CalcSubpelParamsFunc calc_subpel_params_func,
                         int pre_x, int pre_y, uint16_t *dst_ref0,
                         uint16_t *dst_ref1, MV *best_mv_ref, int pu_width,
                         int pu_height);

// check if padding is required during motion compensation
// return 1 means reference pixel is outside of the reference range and padding
// is required return 0 means no padding.
int update_extend_mc_border_params(const struct scale_factors *const sf,
                                   struct buf_2d *const pre_buf, MV32 scaled_mv,
                                   PadBlock *block, int subpel_x_mv,
                                   int subpel_y_mv, int do_warp, int is_intrabc,
                                   int *x_pad, int *y_pad,
                                   const ReferenceArea *ref_area);

// Derive the sub-pixel related parameters of refinemv non-TIP blocks
// Sub-pel related parameters are stored in the structures pointed by
// "subpel_params" Also do padding if required This function is used for both
// encoder and decoder
void common_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
    int use_optflow_refinement, uint16_t **mc_buf, uint16_t **pre,
    SubpelParams *subpel_params, int *src_stride);
#endif  // CONFIG_REFINEMV

#if CONFIG_REFINEMV || CONFIG_OPTFLOW_ON_TIP
unsigned int get_highbd_sad(const uint16_t *src_ptr, int source_stride,
                            const uint16_t *ref_ptr, int ref_stride, int bd,
                            int bw, int bh);
#endif  // CONFIG_REFINEMV || CONFIG_OPTFLOW_ON_TIP

#if CONFIG_SUBBLK_REF_DS
unsigned int get_highbd_sad_ds(const uint16_t *src_ptr, int source_stride,
                               const uint16_t *ref_ptr, int ref_stride, int bd,
                               int bw, int bh);
#endif  // CONFIG_SUBBLK_REF_DS

void calc_mv_process(int64_t su2, int64_t sv2, int64_t suv, int64_t suw,
                     int64_t svw, const int d0, const int d1, const int bits,
                     const int rls_alpha, int *vx0, int *vy0, int *vx1,
                     int *vy1);
void av1_opfl_mv_refinement(const int16_t *pdiff, int pstride0,
                            const int16_t *gx, const int16_t *gy, int gstride,
                            int bw, int bh, int d0, int d1, int grad_prec_bits,
                            int mv_prec_bits, int *vx0, int *vy0, int *vx1,
                            int *vy1);
void av1_compute_subpel_gradients_interp(int16_t *pred_dst, int bw, int bh,
                                         int *grad_prec_bits, int16_t *x_grad,
                                         int16_t *y_grad);

// TODO(jkoleszar): yet another mv clamping function :-(
static INLINE MV clamp_mv_to_umv_border_sb(const MACROBLOCKD *xd,
                                           const MV *src_mv, int bw, int bh,
                                           int use_optflow_refinement, int ss_x,
                                           int ss_y) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (AOM_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (AOM_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
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

#if CONFIG_D071_IMP_MSK_BLD
void make_masked_inter_predictor(const uint16_t *pre, int pre_stride,
                                 uint16_t *dst, int dst_stride,
                                 InterPredParams *inter_pred_params,
                                 const SubpelParams *subpel_params,
                                 int use_bacp, int sub_block_id);

static INLINE int use_border_aware_compound(const AV1_COMMON *cm,
                                            const MB_MODE_INFO *mbmi) {
  if (is_masked_compound_type(mbmi->interinter_comp.type) ||
      mbmi->mode == GLOBAL_GLOBALMV)
    return 0;

  (void)cm;
  return has_second_ref(mbmi) &&
         (mbmi->mode >= COMP_INTER_MODE_START &&
          mbmi->mode < COMP_INTER_MODE_END) &&
         (mbmi->interinter_comp.type == COMPOUND_DIFFWTD ||
          mbmi->interinter_comp.type == COMPOUND_AVERAGE);
}
int is_out_of_frame_block(InterPredParams const *inter_pred_params,
                          int frame_width, int frame_height, int sub_block_id);
#endif  // CONFIG_D071_IMP_MSK_BLD

static INLINE int64_t scaled_buffer_offset(int x_offset, int y_offset,
                                           int stride,
                                           const struct scale_factors *sf) {
  const int x =
      sf ? sf->scale_value_x(x_offset, sf) >> SCALE_EXTRA_BITS : x_offset;
  const int y =
      sf ? sf->scale_value_y(y_offset, sf) >> SCALE_EXTRA_BITS : y_offset;
  return (int64_t)y * stride + x;
}

static INLINE void setup_pred_plane(struct buf_2d *dst, uint16_t *src,
                                    int width, int height, int stride,
                                    int mi_row, int mi_col,
                                    const struct scale_factors *scale,
                                    int subsampling_x, int subsampling_y,
                                    const CHROMA_REF_INFO *chroma_ref_info) {
  // Offset the buffer pointer
  if (chroma_ref_info && (subsampling_x || subsampling_y)) {
    mi_row = chroma_ref_info->mi_row_chroma_base;
    mi_col = chroma_ref_info->mi_col_chroma_base;
  }

  const int x = (MI_SIZE * mi_col) >> subsampling_x;
  const int y = (MI_SIZE * mi_row) >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->buf0 = src;
  dst->width = width;
  dst->height = height;
  dst->stride = stride;
}

void av1_setup_dst_planes(struct macroblockd_plane *planes,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const int plane_start, const int plane_end,
                          const CHROMA_REF_INFO *chroma_ref_info);

void av1_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf, const int num_planes,
                          const CHROMA_REF_INFO *chroma_ref_info);

static AOM_INLINE void setup_pred_planes_for_tip(const TIP *tip_ref,
                                                 MACROBLOCKD *xd,
                                                 int plane_start, int plane_end,
                                                 int mi_col, int mi_row) {
  for (int plane = plane_start; plane < AOMMIN(plane_end, MAX_MB_PLANE);
       ++plane) {
    struct macroblockd_plane *const pd = &xd->plane[plane];
    int is_uv = plane > 0;

    for (int ref = 0; ref < 2; ++ref) {
      const YV12_BUFFER_CONFIG *ref_buf = &tip_ref->ref_frame_buffer[ref]->buf;
      setup_pred_plane(&pd->pre[ref], ref_buf->buffers[plane],
                       ref_buf->crop_widths[is_uv],
                       ref_buf->crop_heights[is_uv], ref_buf->strides[is_uv],
                       mi_row, mi_col, tip_ref->ref_scale_factor[ref],
                       pd->subsampling_x, pd->subsampling_y, NULL);
    }
  }
}

static INLINE void set_default_interp_filters(
    MB_MODE_INFO *const mbmi, const AV1_COMMON *cm,
#if CONFIG_COMPOUND_4XN
    const MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
    InterpFilter frame_interp_filter) {

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode) {
    mbmi->interp_fltr = MULTITAP_SHARP;
    return;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  mbmi->interp_fltr = (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                                  xd,
#endif  // CONFIG_COMPOUND_4XN
                                                  mbmi)
#if CONFIG_REFINEMV
                       || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
                       || is_tip_ref_frame(mbmi->ref_frame[0]))
                          ? MULTITAP_SHARP
                          : av1_unswitchable_filter(frame_interp_filter);
}

static INLINE int av1_is_interp_needed(const AV1_COMMON *const cm,
                                       const MACROBLOCKD *const xd) {
  (void)cm;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->skip_mode) return 0;

  if (mbmi->mode == WARPMV) return 0;
  // No interpolation filter search when optical flow MV refinement is used.
  if (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                 xd,
#endif  // CONFIG_COMPOUND_4XN
                                 mbmi))
    return 0;

#if CONFIG_REFINEMV
  // No interpolation filter search when MV refinement is used.
  if (mbmi->refinemv_flag) return 0;
#endif  // CONFIG_REFINEMV

  if (is_warp_mode(mbmi->motion_mode)) return 0;
  if (is_nontrans_global_motion(xd, xd->mi[0])) return 0;
  if (is_tip_ref_frame(mbmi->ref_frame[0])) return 0;
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

#if CONFIG_WEDGE_TMVP
static INLINE const uint8_t *av1_get_contiguous_soft_mask_decision(
    int8_t wedge_index, int8_t wedge_sign, BLOCK_SIZE sb_type) {
  return av1_wedge_params_lookup[sb_type]
      .tmvp_mv_decisions[wedge_sign][wedge_index];
}
#endif  // CONFIG_WEDGE_TMVP

const uint8_t *av1_get_compound_type_mask(
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type);

// Init the masks for compound weighted prediction
void init_cwp_masks();
// Get the mask for compound weighted prediction
const int8_t *av1_get_cwp_mask(int list_idx, int idx);

// build interintra_predictors for one plane
void av1_build_interintra_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t *pred, int stride,
                                    const BUFFER_SET *ctx, int plane,
                                    BLOCK_SIZE bsize);

#if CONFIG_EXT_RECUR_PARTITIONS
void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride);
#else
void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd,
                                               BLOCK_SIZE bsize, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

void av1_combine_interintra(MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane,
                            const uint16_t *inter_pred, int inter_stride,
                            const uint16_t *intra_pred, int intra_stride);

int av1_allow_warp(const MB_MODE_INFO *const mbmi,
                   const WarpTypesAllowed *const warp_types,
                   const WarpedMotionParams *const gm_params, int ref,
                   int build_for_obmc, const struct scale_factors *const sf,
                   WarpedMotionParams *final_warp_params);

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

#if CONFIG_IBC_SUBPEL_PRECISION
// Function to check if precision need to be signaled or not
int is_intraBC_bv_precision_active(const int intrabc_mode);
// Set max value as default precision
void set_default_intraBC_bv_precision(MB_MODE_INFO *mbmi);
#endif  // CONFIG_IBC_SUBPEL_PRECISION

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
#if CONFIG_SEP_COMP_DRL
                       int *ref_mv_idx);
#else
                       uint8_t ref_mv_idx);
#endif  // CONFIG_SEP_COMP_DRL
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

// check if the WARPMV mode is allwed for a given blocksize
static INLINE int is_warpmv_allowed_bsize(BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  return AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8;
}

// check if WARPMV mode is allowed
static INLINE int is_warpmv_mode_allowed(const AV1_COMMON *const cm,
                                         const MB_MODE_INFO *mbmi,
                                         BLOCK_SIZE bsize) {
  int frame_warp_delta_allowed =
      (cm->features.enabled_motion_modes & (1 << WARP_DELTA)) != 0;

  if (has_second_ref(mbmi) || !frame_warp_delta_allowed ||
      is_tip_ref_frame(mbmi->ref_frame[0]) || !cm->features.allow_warpmv_mode)
    return 0;

  return frame_warp_delta_allowed && is_warpmv_allowed_bsize(bsize);
}

// check if warpmv with mvd is allowed or not
static INLINE int allow_warpmv_with_mvd_coding(const AV1_COMMON *const cm,
                                               const MB_MODE_INFO *mbmi) {
  if (!cm->features.allow_warpmv_mode) return 0;
  return (mbmi->mode == WARPMV && mbmi->warp_ref_idx < 2);
}

#if CONFIG_DERIVED_MVD_SIGN
// Return the threshold value for number of non-zero componenets for sign
// derivation.
static INLINE int get_derive_sign_nzero_th(const MB_MODE_INFO *mbmi) {
  return (mbmi->mode == NEW_NEWMV || mbmi->mode == NEW_NEWMV_OPTFLOW) ? 4 : 1;
}
// Check if the sign derivation method is allowed or not for current block
static INLINE int is_mvd_sign_derive_allowed(const AV1_COMMON *const cm,
                                             const MACROBLOCKD *const xd,
                                             const MB_MODE_INFO *mbmi) {
  if (!cm->seq_params.enable_mvd_sign_derive ||
      mbmi->motion_mode != SIMPLE_TRANSLATION ||
      is_intrabc_block(mbmi, xd->tree_type) ||
      enable_adaptive_mvd_resolution(cm, mbmi) || mbmi->skip_mode ||
      cm->features.allow_screen_content_tools ||
      cm->features.fr_mv_precision > MV_PRECISION_QTR_PEL ||
      mbmi->pb_mv_precision >= MV_PRECISION_QTR_PEL)
    return 0;

  if (has_second_ref(mbmi)) {
    int drl_idx = mbmi->ref_mv_idx[0];
    if (has_second_drl(mbmi)) {
      drl_idx = AOMMAX(mbmi->ref_mv_idx[0], mbmi->ref_mv_idx[1]);
    }
    if (drl_idx > 0) return 0;
  }
  return (mbmi->mode == NEWMV || mbmi->mode == JOINT_NEWMV ||
          mbmi->mode == JOINT_NEWMV_OPTFLOW || mbmi->mode == NEW_NEWMV ||
          mbmi->mode == NEW_NEWMV_OPTFLOW);
}
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_MORPH_PRED
void av1_build_linear_predictor(uint16_t *dst, const int dst_stride,
                                const int width, const int height,
                                const int alpha, const int beta,
                                const int bit_depth);
bool av1_build_morph_pred(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                          const BLOCK_SIZE bsize, const int mi_row,
                          const int mi_col);
#endif  // CONFIG_MORPH_PRED

#if CONFIG_E191_OFS_PRED_RES_HANDLE
static AOM_INLINE bool is_subblock_outside(int x, int y, int mi_cols,
                                           int mi_rows, int build_for_decode) {
  if (!build_for_decode) return 0;
  return (x >= mi_cols * MI_SIZE || y >= mi_rows * MI_SIZE);
}
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_RECONINTER_H_
