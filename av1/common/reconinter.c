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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#include "av1/common/enums.h"
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/blend.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/cfl.h"
#include "av1/common/mvref_common.h"
#include "av1/common/mv.h"
#include "av1/common/obmc.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#if CONFIG_TIP_REF_PRED_MERGING
#include "av1/common/tip.h"
#endif  // CONFIG_TIP_REF_PRED_MERGING

// This function will determine whether or not to create a warped
// prediction.
int av1_allow_warp(const MB_MODE_INFO *const mbmi,
                   const WarpTypesAllowed *const warp_types,
                   const WarpedMotionParams *const gm_params,
#if CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
                   int ref,
#endif  // CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
                   int build_for_obmc, const struct scale_factors *const sf,
                   WarpedMotionParams *final_warp_params) {
  // Note: As per the spec, we must test the fixed point scales here, which are
  // at a higher precision (1 << 14) than the xs and ys in subpel_params (that
  // have 1 << 10 precision).
  if (av1_is_scaled(sf)) return 0;

  if (final_warp_params != NULL) *final_warp_params = default_warp_params;

  if (build_for_obmc) return 0;

#if CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
  if (warp_types->local_warp_allowed && !mbmi->wm_params[ref].invalid) {
    if (final_warp_params != NULL)
      memcpy(final_warp_params, &mbmi->wm_params[ref],
             sizeof(*final_warp_params));
    return 1;
#else
  if (warp_types->local_warp_allowed && !mbmi->wm_params.invalid) {
    if (final_warp_params != NULL)
      memcpy(final_warp_params, &mbmi->wm_params, sizeof(*final_warp_params));
    return 1;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
  } else if (warp_types->global_warp_allowed && !gm_params->invalid) {
    if (final_warp_params != NULL)
      memcpy(final_warp_params, gm_params, sizeof(*final_warp_params));
    return 1;
  }

  return 0;
}

void av1_init_inter_params(InterPredParams *inter_pred_params, int block_width,
                           int block_height, int pix_row, int pix_col,
                           int subsampling_x, int subsampling_y, int bit_depth,
                           int is_intrabc, const struct scale_factors *sf,
                           const struct buf_2d *ref_buf,
                           InterpFilter interp_filter) {
  inter_pred_params->block_width = block_width;
  inter_pred_params->block_height = block_height;
#if CONFIG_OPTFLOW_REFINEMENT
  inter_pred_params->orig_block_width = block_width;
  inter_pred_params->orig_block_height = block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_REFINEMV
  inter_pred_params->original_pu_width = block_width;
  inter_pred_params->original_pu_height = block_height;
#endif  // CONFIG_REFINEMV

  inter_pred_params->pix_row = pix_row;
  inter_pred_params->pix_col = pix_col;
  inter_pred_params->subsampling_x = subsampling_x;
  inter_pred_params->subsampling_y = subsampling_y;
  inter_pred_params->bit_depth = bit_depth;
  inter_pred_params->is_intrabc = is_intrabc;
  inter_pred_params->scale_factors = sf;
  inter_pred_params->ref_frame_buf = *ref_buf;
  inter_pred_params->mode = TRANSLATION_PRED;
  inter_pred_params->comp_mode = UNIFORM_SINGLE;

#if CONFIG_REFINEMV
  inter_pred_params->use_ref_padding = 0;
  inter_pred_params->ref_area = NULL;
#endif  // CONFIG_REFINEMV

#if CONFIG_D071_IMP_MSK_BLD
  inter_pred_params->border_data.enable_bacp = 0;
  inter_pred_params->border_data.bacp_block_data = NULL;
#endif  // CONFIG_D071_IMP_MSK_BLD

  if (is_intrabc) {
    inter_pred_params->interp_filter_params[0] = &av1_intrabc_filter_params;
    inter_pred_params->interp_filter_params[1] = &av1_intrabc_filter_params;
  } else {
    inter_pred_params->interp_filter_params[0] =
        av1_get_interp_filter_params_with_block_size(interp_filter,
                                                     block_width);
    inter_pred_params->interp_filter_params[1] =
        av1_get_interp_filter_params_with_block_size(interp_filter,
                                                     block_height);
  }
}

void av1_init_comp_mode(InterPredParams *inter_pred_params) {
  inter_pred_params->comp_mode = UNIFORM_COMP;
}

void av1_init_warp_params(InterPredParams *inter_pred_params,
                          const WarpTypesAllowed *warp_types, int ref,
                          const MACROBLOCKD *xd, const MB_MODE_INFO *mi) {
  if (inter_pred_params->block_height < 8 || inter_pred_params->block_width < 8)
    return;

  if (is_tip_ref_frame(mi->ref_frame[0])) return;

#if CONFIG_REFINEMV
  // We do not do refineMV for warp blocks
  // We may need to return from here.
  if (mi->refinemv_flag) return;
#endif  // CONFIG_REFINEMV

  if (xd->cur_frame_force_integer_mv) return;

  if (av1_allow_warp(mi, warp_types, &xd->global_motion[mi->ref_frame[ref]],
#if CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
                     ref,
#endif  // CONFIG_EXTENDED_WARP_PREDICTION || CONFIG_AFFINE_REFINEMENT
                     0, inter_pred_params->scale_factors,
                     &inter_pred_params->warp_params))
    inter_pred_params->mode = WARP_PRED;
}

void av1_make_inter_predictor(const uint16_t *src, int src_stride,
                              uint16_t *dst, int dst_stride,
                              InterPredParams *inter_pred_params,
                              const SubpelParams *subpel_params) {
  assert(IMPLIES(inter_pred_params->conv_params.is_compound,
                 inter_pred_params->conv_params.dst != NULL));

  // TODO(jingning): av1_warp_plane() can be further cleaned up.
  if (inter_pred_params->mode == WARP_PRED) {
    av1_warp_plane(
        &inter_pred_params->warp_params, inter_pred_params->bit_depth,
        inter_pred_params->ref_frame_buf.buf0,
        inter_pred_params->ref_frame_buf.width,
        inter_pred_params->ref_frame_buf.height,
        inter_pred_params->ref_frame_buf.stride, dst,
        inter_pred_params->pix_col, inter_pred_params->pix_row,
        inter_pred_params->block_width, inter_pred_params->block_height,
        dst_stride, inter_pred_params->subsampling_x,
        inter_pred_params->subsampling_y, &inter_pred_params->conv_params);
  } else if (inter_pred_params->mode == TRANSLATION_PRED) {
    highbd_inter_predictor(
        src, src_stride, dst, dst_stride, subpel_params,
        inter_pred_params->block_width, inter_pred_params->block_height,
        &inter_pred_params->conv_params,
        inter_pred_params->interp_filter_params, inter_pred_params->bit_depth);
  }
}

#if !CONFIG_WEDGE_MOD_EXT
static const uint8_t wedge_master_oblique_odd[MASK_MASTER_SIZE] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  6,  18,
  37, 53, 60, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};
static const uint8_t wedge_master_oblique_even[MASK_MASTER_SIZE] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  4,  11, 27,
  46, 58, 62, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};
static const uint8_t wedge_master_vertical[MASK_MASTER_SIZE] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  2,  7,  21,
  43, 57, 62, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};
#else
/* clang-format off */
#if WEDGE_BLD_SIG
// rounded cosine and sine look-up tables given by round(32*cos(i))
static const int8_t wedge_cos_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
     32, 31, 29, 23, 14,
  //  8, 10, 12, 14, 15,
      0,-14,-23,-29,-31,
  // 16, 17, 18, 20, 22,
    -32,-31,-29,-23,-14,
  // 24, 26, 28, 30, 31
      0, 14, 23, 29, 31
};
static const int8_t wedge_sin_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
      0, -8,-14,-23,-29,
  //  8, 10, 12, 14, 15,
    -32,-29,-23,-14, -8,
  // 16, 17, 18, 20, 22,
      0,  8, 14, 23, 29,
  // 24, 26, 28, 30, 31
     32, 29, 23, 14,  8
};

// rounded sigmoid function look-up talbe given by round(1/(1+exp(-x)))
static const int8_t pos_dist_2_bld_weight[WEDGE_BLD_LUT_SIZE]={
  32, 32, 33, 33, 34, 34, 35, 35,
  36, 36, 37, 37, 38, 38, 39, 39,
  40, 40, 41, 41, 42, 42, 43, 43,
  43, 44, 44, 45, 45, 46, 46, 46,
  47, 47, 48, 48, 48, 49, 49, 49,
  50, 50, 50, 51, 51, 51, 52, 52,
  52, 53, 53, 53, 53, 54, 54, 54,
  55, 55, 55, 55, 55, 56, 56, 56,
  56, 57, 57, 57, 57, 57, 58, 58,
  58, 58, 58, 58, 59, 59, 59, 59,
  59, 59, 59, 60, 60, 60, 60, 60,
  60, 60, 60, 60, 61, 61, 61, 61,
  61, 61, 61, 61, 61, 61, 61, 62,
  62, 62, 62, 62, 62, 62, 62, 62,
  62, 62, 62, 62, 62, 62, 62, 62,
  63, 63, 63, 63, 63, 63, 63, 64
};

static const int8_t neg_dist_2_bld_weight[WEDGE_BLD_LUT_SIZE]={
  32, 32, 31, 31, 30, 30, 29, 29,
  28, 28, 27, 27, 26, 26, 25, 25,
  24, 24, 23, 23, 22, 22, 21, 21,
  21, 20, 20, 19, 19, 18, 18, 18,
  17, 17, 16, 16, 16, 15, 15, 15,
  14, 14, 14, 13, 13, 13, 12, 12,
  12, 11, 11, 11, 11, 10, 10, 10,
   9,  9,  9,  9,  9,  8,  8,  8,
   8,  7,  7,  7,  7,  7,  6,  6,
   6,  6,  6,  6,  5,  5,  5,  5,
   5,  5,  5,  4,  4,  4,  4,  4,
   4,  4,  4,  4,  3,  3,  3,  3,
   3,  3,  3,  3,  3,  3,  3,  2,
   2,  2,  2,  2,  2,  2,  2,  2,
   2,  2,  2,  2,  2,  2,  2,  2,
   1,  1,  1,  1,  1,  1,  1,  0
};
#else
static const int8_t wedge_cos_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
      8,  8,  8,  4,  4,
  //  8, 10, 12, 14, 15,
      0, -4, -4, -8, -8,
  // 16, 17, 18, 20, 22,
     -8, -8, -8, -4, -4,
  // 24, 26, 28, 30, 31
      0,  4,  4,  8,  8
};
static const int8_t wedge_sin_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
      0, -2, -4, -4, -8,
  //  8, 10, 12, 14, 15,
     -8, -8, -4, -4, -2,
  // 16, 17, 18, 20, 22,
      0,  2,  4,  4,  8,
  // 24, 26, 28, 30, 31
      8,  8,  4,  4,  2
};
#endif
/* clang-format on */
#endif  // !CONFIG_WEDGE_MOD_EXT

#if !CONFIG_WEDGE_MOD_EXT
static AOM_INLINE void shift_copy(const uint8_t *src, uint8_t *dst, int shift,
                                  int width) {
  if (shift >= 0) {
    memcpy(dst + shift, src, width - shift);
    memset(dst, src[0], shift);
  } else {
    shift = -shift;
    memcpy(dst, src + shift, width - shift);
    memset(dst + width - shift, src[width - 1], shift);
  }
}

/* clang-format off */
DECLARE_ALIGNED(16, static uint8_t,
                wedge_signflip_lookup[BLOCK_SIZES_ALL][MAX_WEDGE_TYPES]) = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
#if CONFIG_BLOCK_256
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
#endif  // CONFIG_BLOCK_256
  { 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
#if CONFIG_FLEX_PARTITION
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
#endif  // CONFIG_FLEX_PARTITION
};
/* clang-format on */
#endif  // !CONFIG_WEDGE_MOD_EXT

// [negative][direction]
#if CONFIG_WEDGE_MOD_EXT
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_master_mask[2][WEDGE_ANGLES][MASK_MASTER_SIZE * MASK_MASTER_SIZE]);
#else
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_mask_obl[2][WEDGE_DIRECTIONS][MASK_MASTER_SIZE * MASK_MASTER_SIZE]);
#endif  // CONFIG_WEDGE_MOD_EXT

// 4 * MAX_WEDGE_SQUARE is an easy to compute and fairly tight upper bound
// on the sum of all mask sizes up to an including MAX_WEDGE_SQUARE.
#if CONFIG_WEDGE_MOD_EXT
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_mask_buf[2 * MAX_WEDGE_TYPES * H_WEDGE_ANGLES * MAX_WEDGE_SQUARE]);
#else
DECLARE_ALIGNED(16, static uint8_t,
                wedge_mask_buf[2 * MAX_WEDGE_TYPES * 4 * MAX_WEDGE_SQUARE]);
#endif  // CONFIG_WEDGE_MOD_EXT

DECLARE_ALIGNED(16, static uint8_t,
                smooth_interintra_mask_buf[INTERINTRA_MODES][BLOCK_SIZES_ALL]
                                          [MAX_WEDGE_SQUARE]);

DECLARE_ALIGNED(16, static int8_t, cwp_mask[2][MAX_CWP_NUM][MAX_SB_SQUARE]);

static wedge_masks_type wedge_masks[BLOCK_SIZES_ALL][2];

#if CONFIG_WEDGE_MOD_EXT
static const wedge_code_type wedge_codebook_16[MAX_WEDGE_TYPES] = {
  { WEDGE_0, 5, 4 },   { WEDGE_0, 6, 4 },   { WEDGE_0, 7, 4 },
  { WEDGE_14, 4, 4 },  { WEDGE_14, 5, 4 },  { WEDGE_14, 6, 4 },
  { WEDGE_14, 7, 4 },  { WEDGE_27, 4, 4 },  { WEDGE_27, 5, 4 },
  { WEDGE_27, 6, 4 },  { WEDGE_27, 7, 4 },  { WEDGE_45, 4, 4 },
  { WEDGE_45, 5, 4 },  { WEDGE_45, 6, 4 },  { WEDGE_45, 7, 4 },
  { WEDGE_63, 4, 4 },  { WEDGE_63, 4, 3 },  { WEDGE_63, 4, 2 },
  { WEDGE_63, 4, 1 },  { WEDGE_90, 4, 3 },  { WEDGE_90, 4, 2 },
  { WEDGE_90, 4, 1 },  { WEDGE_117, 4, 4 }, { WEDGE_117, 4, 3 },
  { WEDGE_117, 4, 2 }, { WEDGE_117, 4, 1 }, { WEDGE_135, 4, 4 },
  { WEDGE_135, 3, 4 }, { WEDGE_135, 2, 4 }, { WEDGE_135, 1, 4 },
  { WEDGE_153, 4, 4 }, { WEDGE_153, 3, 4 }, { WEDGE_153, 2, 4 },
  { WEDGE_153, 1, 4 }, { WEDGE_166, 4, 4 }, { WEDGE_166, 3, 4 },
  { WEDGE_166, 2, 4 }, { WEDGE_166, 1, 4 }, { WEDGE_180, 3, 4 },
  { WEDGE_180, 2, 4 }, { WEDGE_180, 1, 4 }, { WEDGE_194, 3, 4 },
  { WEDGE_194, 2, 4 }, { WEDGE_194, 1, 4 }, { WEDGE_207, 3, 4 },
  { WEDGE_207, 2, 4 }, { WEDGE_207, 1, 4 }, { WEDGE_225, 3, 4 },
  { WEDGE_225, 2, 4 }, { WEDGE_225, 1, 4 }, { WEDGE_243, 4, 5 },
  { WEDGE_243, 4, 6 }, { WEDGE_243, 4, 7 }, { WEDGE_270, 4, 5 },
  { WEDGE_270, 4, 6 }, { WEDGE_270, 4, 7 }, { WEDGE_297, 4, 5 },
  { WEDGE_297, 4, 6 }, { WEDGE_297, 4, 7 }, { WEDGE_315, 5, 4 },
  { WEDGE_315, 6, 4 }, { WEDGE_315, 7, 4 }, { WEDGE_333, 5, 4 },
  { WEDGE_333, 6, 4 }, { WEDGE_333, 7, 4 }, { WEDGE_346, 5, 4 },
  { WEDGE_346, 6, 4 }, { WEDGE_346, 7, 4 },
};
#else
static const wedge_code_type wedge_codebook_16_hgtw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 6 }, { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

static const wedge_code_type wedge_codebook_16_hltw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_VERTICAL, 6, 4 },   { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

static const wedge_code_type wedge_codebook_16_heqw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 6 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 6, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};
#endif  // CONFIG_WEDGE_MOD_EXT

#if CONFIG_WEDGE_MOD_EXT
const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL] = {
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X64] },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#if CONFIG_BLOCK_256
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#endif  // CONFIG_BLOCK_256
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X16] },
#if CONFIG_FLEX_PARTITION
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X8] },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#endif  // CONFIG_FLEX_PARTITION
};
#else
const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL] = {
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_8X8],
    wedge_masks[BLOCK_8X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_8X16],
    wedge_masks[BLOCK_8X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_16X8],
    wedge_masks[BLOCK_16X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_16X16],
    wedge_masks[BLOCK_16X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_16X32],
    wedge_masks[BLOCK_16X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_32X16],
    wedge_masks[BLOCK_32X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_32X32],
    wedge_masks[BLOCK_32X32] },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#if CONFIG_BLOCK_256
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#endif  // CONFIG_BLOCK_256
  { MAX_WEDGE_TYPES, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_8X32],
    wedge_masks[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_32X8],
    wedge_masks[BLOCK_32X8] },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#if CONFIG_FLEX_PARTITION
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
#endif  // CONFIG_FLEX_PARTITION
};
#endif

// Init the cwp masks, called by init_cwp_masks
static AOM_INLINE void build_cwp_mask(int8_t *mask, int stride,
                                      BLOCK_SIZE plane_bsize, int8_t w) {
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) mask[j] = w;
    mask += stride;
  }
}
// Init the cwp masks
void init_cwp_masks() {
  const int bs = BLOCK_128X128;
  const int bw = block_size_wide[bs];
  for (int list_idx = 0; list_idx < 2; ++list_idx) {
    for (int idx = 0; idx < MAX_CWP_NUM; ++idx) {
      int8_t weight = cwp_weighting_factor[list_idx][idx] * 4;
      build_cwp_mask(cwp_mask[list_idx][idx], bw, bs, weight);
    }
  }
}
// Return the associated cwp mask
const int8_t *av1_get_cwp_mask(int list_idx, int idx) {
  return cwp_mask[list_idx][idx];
}

static const uint8_t *get_wedge_mask_inplace(int wedge_index, int neg,
                                             BLOCK_SIZE sb_type) {
  const uint8_t *master;
  const int bh = block_size_high[sb_type];
  const int bw = block_size_wide[sb_type];
  const wedge_code_type *a =
      av1_wedge_params_lookup[sb_type].codebook + wedge_index;
  int woff, hoff;
#if !CONFIG_WEDGE_MOD_EXT
  const uint8_t wsignflip =
      av1_wedge_params_lookup[sb_type].signflip[wedge_index];
#endif

  assert(wedge_index >= 0 && wedge_index < get_wedge_types_lookup(sb_type));
  woff = (a->x_offset * bw) >> 3;
  hoff = (a->y_offset * bh) >> 3;
#if CONFIG_WEDGE_MOD_EXT
  master = wedge_master_mask[neg][a->direction] +
           MASK_MASTER_STRIDE * (MASK_MASTER_SIZE / 2 - hoff) +
           MASK_MASTER_SIZE / 2 - woff;
#else
  master = wedge_mask_obl[neg ^ wsignflip][a->direction] +
           MASK_MASTER_STRIDE * (MASK_MASTER_SIZE / 2 - hoff) +
           MASK_MASTER_SIZE / 2 - woff;
#endif  // CONFIG_WEDGE_MOD_EXT
  return master;
}

const uint8_t *av1_get_compound_type_mask(
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type) {
#if !CONFIG_D071_IMP_MSK_BLD
  assert(is_masked_compound_type(comp_data->type));
#endif  // !CONFIG_D071_IMP_MSK_BLD
  (void)sb_type;
  switch (comp_data->type) {
    case COMPOUND_WEDGE:
      return av1_get_contiguous_soft_mask(comp_data->wedge_index,
                                          comp_data->wedge_sign, sb_type);
#if CONFIG_D071_IMP_MSK_BLD
    case COMPOUND_AVERAGE:
#endif  // CONFIG_D071_IMP_MSK_BLD
    case COMPOUND_DIFFWTD: return comp_data->seg_mask;
    default: assert(0); return NULL;
  }
}

static AOM_INLINE void diffwtd_mask_d16(
    uint8_t *mask, int which_inverse, int mask_base, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w,
    ConvolveParams *conv_params, int bd) {
  int round =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1 + (bd - 8);
  int i, j, m, diff;
  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      diff = abs(src0[i * src0_stride + j] - src1[i * src1_stride + j]);
      diff = ROUND_POWER_OF_TWO(diff, round);
      m = clamp(mask_base + (diff / DIFF_FACTOR), 0, AOM_BLEND_A64_MAX_ALPHA);
      mask[i * w + j] = which_inverse ? AOM_BLEND_A64_MAX_ALPHA - m : m;
    }
  }
}

void av1_build_compound_diffwtd_mask_d16_c(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w,
    ConvolveParams *conv_params, int bd) {
  switch (mask_type) {
    case DIFFWTD_38:
      diffwtd_mask_d16(mask, 0, 38, src0, src0_stride, src1, src1_stride, h, w,
                       conv_params, bd);
      break;
    case DIFFWTD_38_INV:
      diffwtd_mask_d16(mask, 1, 38, src0, src0_stride, src1, src1_stride, h, w,
                       conv_params, bd);
      break;
    default: assert(0);
  }
}

static AOM_FORCE_INLINE void diffwtd_mask_highbd(
    uint8_t *mask, int which_inverse, int mask_base, const uint16_t *src0,
    int src0_stride, const uint16_t *src1, int src1_stride, int h, int w,
    const unsigned int bd) {
  assert(bd >= 8);
  if (bd == 8) {
    if (which_inverse) {
      for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
          int diff = abs((int)src0[j] - (int)src1[j]) / DIFF_FACTOR;
          unsigned int m = negative_to_zero(mask_base + diff);
          m = AOMMIN(m, AOM_BLEND_A64_MAX_ALPHA);
          mask[j] = AOM_BLEND_A64_MAX_ALPHA - m;
        }
        src0 += src0_stride;
        src1 += src1_stride;
        mask += w;
      }
    } else {
      for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
          int diff = abs((int)src0[j] - (int)src1[j]) / DIFF_FACTOR;
          unsigned int m = negative_to_zero(mask_base + diff);
          m = AOMMIN(m, AOM_BLEND_A64_MAX_ALPHA);
          mask[j] = m;
        }
        src0 += src0_stride;
        src1 += src1_stride;
        mask += w;
      }
    }
  } else {
    const unsigned int bd_shift = bd - 8;
    if (which_inverse) {
      for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
          int diff =
              (abs((int)src0[j] - (int)src1[j]) >> bd_shift) / DIFF_FACTOR;
          unsigned int m = negative_to_zero(mask_base + diff);
          m = AOMMIN(m, AOM_BLEND_A64_MAX_ALPHA);
          mask[j] = AOM_BLEND_A64_MAX_ALPHA - m;
        }
        src0 += src0_stride;
        src1 += src1_stride;
        mask += w;
      }
    } else {
      for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
          int diff =
              (abs((int)src0[j] - (int)src1[j]) >> bd_shift) / DIFF_FACTOR;
          unsigned int m = negative_to_zero(mask_base + diff);
          m = AOMMIN(m, AOM_BLEND_A64_MAX_ALPHA);
          mask[j] = m;
        }
        src0 += src0_stride;
        src1 += src1_stride;
        mask += w;
      }
    }
  }
}

void av1_build_compound_diffwtd_mask_highbd_c(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const uint16_t *src0,
    int src0_stride, const uint16_t *src1, int src1_stride, int h, int w,
    int bd) {
  switch (mask_type) {
    case DIFFWTD_38:
      diffwtd_mask_highbd(mask, 0, 38, src0, src0_stride, src1, src1_stride, h,
                          w, bd);
      break;
    case DIFFWTD_38_INV:
      diffwtd_mask_highbd(mask, 1, 38, src0, src0_stride, src1, src1_stride, h,
                          w, bd);
      break;
    default: assert(0);
  }
}

static AOM_INLINE void init_wedge_master_masks() {
#if CONFIG_WEDGE_MOD_EXT
  const int w = MASK_MASTER_SIZE;
  const int h = MASK_MASTER_SIZE;
  for (int angle = 0; angle < WEDGE_ANGLES; angle++) {
    int idx = 0;
    // printf("angle: %d\n", angle);
    for (int n = 0; n < h; n++) {
      int y = ((n << 1) - h + 1) * wedge_sin_lut[angle];
      for (int m = 0; m < w; m++, idx++) {
        int d = ((m << 1) - w + 1) * wedge_cos_lut[angle] + y;
#if WEDGE_BLD_SIG
        const int clamp_d = clamp(d, -127, 127);
        wedge_master_mask[0][angle][idx] =
            clamp_d >= 0 ? pos_dist_2_bld_weight[clamp_d]
                         : neg_dist_2_bld_weight[-clamp_d];
#else
        wedge_master_mask[0][angle][idx] = clamp((d + 32), 0, 64);
#endif
        wedge_master_mask[1][angle][idx] =
            64 - wedge_master_mask[0][angle][idx];
      }
    }
  }
#else
  int i, j;
  const int w = MASK_MASTER_SIZE;
  const int h = MASK_MASTER_SIZE;
  const int stride = MASK_MASTER_STRIDE;

  // Note: index [0] stores the masters, and [1] its complement.
  // Generate prototype by shifting the masters
  int shift = h / 4;
  for (i = 0; i < h; i += 2) {
    shift_copy(wedge_master_oblique_even,
               &wedge_mask_obl[0][WEDGE_OBLIQUE63][i * stride], shift,
               MASK_MASTER_SIZE);
    shift--;
    shift_copy(wedge_master_oblique_odd,
               &wedge_mask_obl[0][WEDGE_OBLIQUE63][(i + 1) * stride], shift,
               MASK_MASTER_SIZE);
    memcpy(&wedge_mask_obl[0][WEDGE_VERTICAL][i * stride],
           wedge_master_vertical,
           MASK_MASTER_SIZE * sizeof(wedge_master_vertical[0]));
    memcpy(&wedge_mask_obl[0][WEDGE_VERTICAL][(i + 1) * stride],
           wedge_master_vertical,
           MASK_MASTER_SIZE * sizeof(wedge_master_vertical[0]));
  }

  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      const int msk = wedge_mask_obl[0][WEDGE_OBLIQUE63][i * stride + j];
      wedge_mask_obl[0][WEDGE_OBLIQUE27][j * stride + i] = msk;
      wedge_mask_obl[0][WEDGE_OBLIQUE117][i * stride + w - 1 - j] =
          wedge_mask_obl[0][WEDGE_OBLIQUE153][(w - 1 - j) * stride + i] =
              (1 << WEDGE_WEIGHT_BITS) - msk;
      wedge_mask_obl[1][WEDGE_OBLIQUE63][i * stride + j] =
          wedge_mask_obl[1][WEDGE_OBLIQUE27][j * stride + i] =
              (1 << WEDGE_WEIGHT_BITS) - msk;
      wedge_mask_obl[1][WEDGE_OBLIQUE117][i * stride + w - 1 - j] =
          wedge_mask_obl[1][WEDGE_OBLIQUE153][(w - 1 - j) * stride + i] = msk;
      const int mskx = wedge_mask_obl[0][WEDGE_VERTICAL][i * stride + j];
      wedge_mask_obl[0][WEDGE_HORIZONTAL][j * stride + i] = mskx;
      wedge_mask_obl[1][WEDGE_VERTICAL][i * stride + j] =
          wedge_mask_obl[1][WEDGE_HORIZONTAL][j * stride + i] =
              (1 << WEDGE_WEIGHT_BITS) - mskx;
    }
  }
#endif
}

static AOM_INLINE void init_wedge_masks() {
  uint8_t *dst = wedge_mask_buf;
  BLOCK_SIZE bsize;
  memset(wedge_masks, 0, sizeof(wedge_masks));
  for (bsize = BLOCK_4X4; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const wedge_params_type *wedge_params = &av1_wedge_params_lookup[bsize];
    const int wtypes = wedge_params->wedge_types;
    if (wtypes == 0) continue;
    const uint8_t *mask;
    const int bw = block_size_wide[bsize];
    const int bh = block_size_high[bsize];
    int w;
    for (w = 0; w < wtypes; ++w) {
      mask = get_wedge_mask_inplace(w, 0, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw /* dst_stride */, bw,
                        bh);
      wedge_params->masks[0][w] = dst;
      dst += bw * bh;

      mask = get_wedge_mask_inplace(w, 1, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw /* dst_stride */, bw,
                        bh);
      wedge_params->masks[1][w] = dst;
      dst += bw * bh;
    }
    assert(sizeof(wedge_mask_buf) >= (size_t)(dst - wedge_mask_buf));
  }
}

/* clang-format off */
static const uint8_t ii_weights1d[MAX_SB_SIZE] = {
  60, 58, 56, 54, 52, 50, 48, 47, 45, 44, 42, 41, 39, 38, 37, 35, 34, 33, 32,
  31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 22, 21, 20, 19, 19, 18, 18, 17, 16,
  16, 15, 15, 14, 14, 13, 13, 12, 12, 12, 11, 11, 10, 10, 10,  9,  9,  9,  8,
  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  6,  6,  5,  5,  5,  5,  5,  4,  4,
  4,  4,  4,  4,  4,  4,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1
};
static uint8_t ii_size_scales[BLOCK_SIZES_ALL] = {
    32, 16, 16, 16, 8, 8, 8, 4,
    4,  4,  2,  2,  2, 1, 1, 1,
#if CONFIG_BLOCK_256
    0,  0,  0,  // unused
#endif  // CONFIG_BLOCK_256
    8,  8,  4,  4,  2, 2,
#if CONFIG_FLEX_PARTITION
    4,  4,  2,  2,  2, 2,
#endif  // CONFIG_FLEX_PARTITION
};
/* clang-format on */

static AOM_INLINE void build_smooth_interintra_mask(uint8_t *mask, int stride,
                                                    BLOCK_SIZE plane_bsize,
                                                    INTERINTRA_MODE mode) {
  int i, j;
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];
  const int size_scale = ii_size_scales[plane_bsize];

  switch (mode) {
    case II_V_PRED:
      for (i = 0; i < bh; ++i) {
        memset(mask, ii_weights1d[i * size_scale], bw * sizeof(mask[0]));
        mask += stride;
      }
      break;

    case II_H_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) mask[j] = ii_weights1d[j * size_scale];
        mask += stride;
      }
      break;

    case II_SMOOTH_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j)
          mask[j] = ii_weights1d[(i < j ? i : j) * size_scale];
        mask += stride;
      }
      break;

    case II_DC_PRED:
    default:
      for (i = 0; i < bh; ++i) {
        memset(mask, 32, bw * sizeof(mask[0]));
        mask += stride;
      }
      break;
  }
}

static AOM_INLINE void init_smooth_interintra_masks() {
  for (int m = 0; m < INTERINTRA_MODES; ++m) {
    for (int bs = 0; bs < BLOCK_SIZES_ALL; ++bs) {
      const int bw = block_size_wide[bs];
      const int bh = block_size_high[bs];
      if (bw > MAX_WEDGE_SIZE || bh > MAX_WEDGE_SIZE) continue;
      build_smooth_interintra_mask(smooth_interintra_mask_buf[m][bs], bw, bs,
                                   m);
    }
  }
}

#if CONFIG_SUBBLK_REF_DS
unsigned int get_highbd_sad_ds(const uint16_t *src_ptr, int source_stride,
                               const uint16_t *ref_ptr, int ref_stride, int bd,
                               int bw, int bh) {
  if (bd == 8) {
    if (bw == 16 && bh == 8)
      return aom_highbd_sad16x8_ds(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 16 && bh == 16)
      return aom_highbd_sad16x16_ds(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else if (bw == 8 && bh == 8)
      return aom_highbd_sad8x8_ds(src_ptr, source_stride, ref_ptr, ref_stride);
    else if (bw == 8 && bh == 16)
      return aom_highbd_sad8x16_ds(src_ptr, source_stride, ref_ptr, ref_stride);
#if CONFIG_SUBBLK_REF_EXT
    else if (bw == 12 && bh == 12)
      return aom_highbd_sad12x12_ds(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else if (bw == 20 && bh == 12)
      return aom_highbd_sad20x12_ds(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else if (bw == 12 && bh == 20)
      return aom_highbd_sad12x20_ds(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
    else if (bw == 20 && bh == 20)
      return aom_highbd_sad20x20_ds(src_ptr, source_stride, ref_ptr,
                                    ref_stride);
#endif  // CONFIG_SUBBLK_REF_EXT
    else {
      assert(0);
      return 0;
    }
  } else if (bd == 10) {
    if (bw == 16 && bh == 8)
      return (
          aom_highbd_sad16x8_ds(src_ptr, source_stride, ref_ptr, ref_stride) >>
          2);
    else if (bw == 16 && bh == 16)
      return (
          aom_highbd_sad16x16_ds(src_ptr, source_stride, ref_ptr, ref_stride) >>
          2);
    else if (bw == 8 && bh == 8)
      return (
          aom_highbd_sad8x8_ds(src_ptr, source_stride, ref_ptr, ref_stride) >>
          2);
    else if (bw == 8 && bh == 16)
      return (
          aom_highbd_sad8x16_ds(src_ptr, source_stride, ref_ptr, ref_stride) >>
          2);
    else {
      assert(0);
      return 0;
    }
  } else {
    assert(0);
    return 0;
  }
}
#endif  // CONFIG_SUBBLK_REF_DS

#if CONFIG_REFINEMV
// Compute the SAD values for refineMV modes
int get_refinemv_sad(uint16_t *src1, uint16_t *src2, int width, int height,
                     int bd) {
#if CONFIG_SUBBLK_REF_EXT
  (void)bd;
#if CONFIG_SUBBLK_REF_DS
  return get_highbd_sad_ds(src1, width, src2, width, 8, width, height);
#else
  return get_highbd_sad(src1, width, src2, width, 8, width, height);
#endif
#else
#if CONFIG_SUBBLK_REF_DS
  return get_highbd_sad_ds(src1, width, src2, width, bd, width, height);
#else
  return get_highbd_sad(src1, width, src2, width, bd, width, height);
#endif  // CONFIG_SUBBLK_REF_DS
#endif  // CONFIG_SUBBLK_REF_EXT
}
#endif  // CONFIG_REFINEMV

#if CONFIG_AFFINE_REFINEMENT
#if AFFINE_FAST_WARP_METHOD == 2
#define BICUBIC_PHASE_BITS 6
#define BICUBIC_WARP_PREC_BITS 10
// Warp prediction using bicubic interpolation (effectively 4-tap filter)
void av1_warp_plane_bicubic(WarpedMotionParams *wm, int bd, const uint16_t *ref,
                            int width, int height, int stride, uint16_t *pred,
                            int p_col, int p_row, int p_width, int p_height,
                            int p_stride, int subsampling_x, int subsampling_y,
                            ConvolveParams *conv_params) {
  (void)conv_params;
  assert(wm->wmtype <= AFFINE);
  assert(!is_uneven_wtd_comp_avg(conv_params));
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
  const int32_t *const mat = wm->wmmat;

  // bicubic coefficient matrix is the following one divided by 6
  const int bicubic_mat[4][4] = {
    { -1, 3, -3, 1 }, { 3, -6, 3, 0 }, { -2, -3, 6, -1 }, { 0, 6, 0, 0 }
  };
  const int onesixth_bits = 12;
  const int onesixth = 683;  // Integerized (1 << onesixth_bits) / 6

  int32_t sum = 0;
  int32_t tmp[4] = { 0 };
  for (int i = p_row; i < p_row + p_height; i++) {
    for (int j = p_col; j < p_col + p_width; j++) {
      uint16_t *p = &pred[(i - p_row) * p_stride + (j - p_col)];

      // Project to luma coordinates (if in a subsampled chroma plane), apply
      // the affine transformation, then convert back to the original
      // coordinates (if necessary)
      const int32_t src_x = j << subsampling_x;
      const int32_t src_y = i << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];
      const int64_t x = dst_x >> subsampling_x;
      const int64_t y = dst_y >> subsampling_y;

      const int32_t ix = (int32_t)(x >> WARPEDMODEL_PREC_BITS);
      const int32_t ixs[4] = { clamp(ix - 1, 0, width - 1),
                               clamp(ix, 0, width - 1),
                               clamp(ix + 1, 0, width - 1),
                               clamp(ix + 2, 0, width - 1) };
      const int32_t sx = x & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy = (int32_t)(y >> WARPEDMODEL_PREC_BITS);
      const int32_t iys[4] = { clamp(iy - 1, 0, height - 1),
                               clamp(iy, 0, height - 1),
                               clamp(iy + 1, 0, height - 1),
                               clamp(iy + 2, 0, height - 1) };
      const int32_t sy = y & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t spel_x =
          ROUND_POWER_OF_TWO(sx, WARPEDMODEL_PREC_BITS - BICUBIC_PHASE_BITS);
      const int32_t spel_y =
          ROUND_POWER_OF_TWO(sy, WARPEDMODEL_PREC_BITS - BICUBIC_PHASE_BITS);

      int32_t xx[4] = { spel_x * spel_x * spel_x, spel_x * spel_x, spel_x, 1 };
      int32_t yy[4] = { spel_y * spel_y * spel_y, spel_y * spel_y, spel_y, 1 };
      assert(onesixth_bits - BICUBIC_WARP_PREC_BITS >= 0);

      // Horizontal filter
      for (int k = 0; k < 4; k++) {
        tmp[k] = 0;
        for (int l = 0; l < 4; l++) {
          int bits = (3 - l) * BICUBIC_PHASE_BITS + onesixth_bits -
                     BICUBIC_WARP_PREC_BITS;
          tmp[k] += ROUND_POWER_OF_TWO_SIGNED(
              xx[l] * bicubic_mat[l][k] * onesixth, bits);
        }
      }
      for (int k = 0; k < 4; k++) {
        xx[k] = 0;
        for (int l = 0; l < 4; l++) {
          xx[k] += tmp[l] * ref[iys[k] * stride + ixs[l]];
        }
        xx[k] = ROUND_POWER_OF_TWO(xx[k], BICUBIC_WARP_PREC_BITS);
      }

      // Vertical filter
      for (int k = 0; k < 4; k++) {
        tmp[k] = 0;
        for (int l = 0; l < 4; l++) {
          int bits = (3 - l) * BICUBIC_PHASE_BITS + onesixth_bits -
                     BICUBIC_WARP_PREC_BITS;
          tmp[k] += ROUND_POWER_OF_TWO_SIGNED(
              yy[l] * bicubic_mat[l][k] * onesixth, bits);
        }
      }
      for (int l = 0; l < 4; l++) {
        sum += tmp[l] * xx[l];
      }
      sum = ROUND_POWER_OF_TWO(sum, BICUBIC_WARP_PREC_BITS);
      *p = clip_pixel_highbd(sum, bd);
    }
  }
}
#endif  // AFFINE_FAST_WARP_METHOD == 2
void av1_warp_plane_bilinear_c(WarpedMotionParams *wm, int bd,
                               const uint16_t *ref, int width, int height,
                               int stride, uint16_t *pred, int p_col, int p_row,
                               int p_width, int p_height, int p_stride,
                               int subsampling_x, int subsampling_y,
                               ConvolveParams *conv_params) {
  (void)conv_params;
#if AFFINE_FAST_WARP_METHOD == 3
#define BILINEAR_WARP_PREC_BITS 12
  assert(wm->wmtype <= AFFINE);
  assert(!is_uneven_wtd_comp_avg(conv_params));
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
  const int32_t *const mat = wm->wmmat;

  for (int i = p_row; i < p_row + p_height; i++) {
    for (int j = p_col; j < p_col + p_width; j++) {
      uint16_t *p = &pred[(i - p_row) * p_stride + (j - p_col)];

      // Project to luma coordinates (if in a subsampled chroma plane), apply
      // the affine transformation, then convert back to the original
      // coordinates (if necessary)
      const int32_t src_x = j << subsampling_x;
      const int32_t src_y = i << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];
      const int64_t x = dst_x >> subsampling_x;
      const int64_t y = dst_y >> subsampling_y;

      const int32_t ix = (int32_t)(x >> WARPEDMODEL_PREC_BITS);
      const int32_t ix0 = clamp(ix, 0, width - 1);
      const int32_t ix1 = clamp(ix + 1, 0, width - 1);
      const int32_t sx = x & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy = (int32_t)(y >> WARPEDMODEL_PREC_BITS);
      const int32_t iy0 = clamp(iy, 0, height - 1);
      const int32_t iy1 = clamp(iy + 1, 0, height - 1);
      const int32_t sy = y & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      const int32_t unit_offset = 1 << BILINEAR_WARP_PREC_BITS;
      const int32_t coeff_x = ROUND_POWER_OF_TWO(
          sx, WARPEDMODEL_PREC_BITS - BILINEAR_WARP_PREC_BITS);
      const int32_t coeff_y = ROUND_POWER_OF_TWO(
          sy, WARPEDMODEL_PREC_BITS - BILINEAR_WARP_PREC_BITS);

      // Horizontal filter
      int32_t tmp0 = ref[iy0 * stride + ix0] * (unit_offset - coeff_x) +
                     ref[iy0 * stride + ix1] * coeff_x;
      tmp0 = ROUND_POWER_OF_TWO(tmp0, BILINEAR_WARP_PREC_BITS);
      int32_t tmp1 = ref[iy1 * stride + ix0] * (unit_offset - coeff_x) +
                     ref[iy1 * stride + ix1] * coeff_x;
      tmp1 = ROUND_POWER_OF_TWO(tmp1, BILINEAR_WARP_PREC_BITS);

      // Vertical filter
      int32_t sum = tmp0 * (unit_offset - coeff_y) + tmp1 * coeff_y;
      sum = ROUND_POWER_OF_TWO(sum, BILINEAR_WARP_PREC_BITS);

      *p = clip_pixel_highbd(sum, bd);
    }
  }
#else
  (void)wm;
  (void)bd;
  (void)ref;
  (void)width;
  (void)height;
  (void)stride;
  (void)pred;
  (void)p_col;
  (void)p_row;
  (void)p_width;
  (void)p_height;
  (void)p_stride;
  (void)subsampling_x;
  (void)subsampling_y;
#endif  // AFFINE_FAST_WARP_METHOD == 3
}

#if CONFIG_REFINEMENT_SIMPLIFY
// Obtain the bit depth ranges for each row and column of a square matrix
void get_mat4d_shifts(const int64_t *mat, int *shifts, const int max_mat_bits) {
  int bits[16] = { 0 };
  for (int i = 0; i < 4; i++) {
    for (int j = i; j < 4; j++)
      bits[i * 4 + j] = 1 + get_msb_signed_64(mat[i * 4 + j]);
    shifts[i] = -AOMMAX(0, (bits[i * 4 + i] - max_mat_bits + 1) >> 1);
  }
  for (int i = 0; i < 4; i++) {
    for (int j = i; j < 4; j++) {
      if (bits[i * 4 + j] + shifts[i] + shifts[j] > max_mat_bits)
        shifts[shifts[i] < shifts[j] ? j : i]--;
    }
  }

  // Get stats of bits after shifts
  int bits_sum[4] = { 0 };
  int bits_max[4] = { 0 };
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      int bits_ij =
          (j >= i ? bits[i * 4 + j] : bits[j * 4 + i]) + shifts[i] + shifts[j];
      bits_sum[i] += bits_ij;
      bits_max[i] = AOMMAX(bits_max[i], bits_ij);
    }
  }

  // For the i-th row/col, if bit depth does not exceeds the threshold,
  // compute the gap of bit depth to that of the largest diagonal element,
  // and apply an upshift based on this gap.
  int max_sum = AOMMAX(AOMMAX(bits_sum[0], bits_sum[1]),
                       AOMMAX(bits_sum[2], bits_sum[3]));
  for (int i = 0; i < 4; i++)
    shifts[i] +=
        AOMMIN((max_mat_bits - bits_max[i]) >> 1, (max_sum - bits_sum[i]) >> 2);
}

// Obtain the bit depth range of a vector
void get_vec_bit_ranges(const int64_t *vec, int *bits_max, const int dim) {
  int bits = 0;
  for (int i = 0; i < dim; i++) {
    bits = 1 + get_msb_signed_64(vec[i]);
    *bits_max = AOMMAX(*bits_max, bits);
  }
}

#define MAX_LS_DIM 4
// Swap two rows for Gaussian elimination routine
void swap_rows(int64_t *mat, int64_t *sol, const int i, const int j,
               const int dim) {
  int64_t temp = sol[i];
  sol[i] = sol[j];
  sol[j] = temp;
  for (int col = 0; col < dim; col++) {
    temp = mat[i * dim + col];
    mat[i * dim + col] = mat[j * dim + col];
    mat[j * dim + col] = temp;
  }
}

// For better precision, set this number as minimal bits for intermediate
// result of Gaussian elimination.
#define GE_MULT_PREC_BITS 12
// This function is a stable version of ROUND_POWER_OF_TWO_SIGNED(a*b, shift),
// where shifts are partially applied before multiplcation operations to avoid
// overflow issues, i.e., (a>>s1)*(b>>s2)>>s3, where s1+s2+s3=shift
int64_t stable_mult_shift(const int64_t a, const int64_t b, const int shift,
                          const int msb_a, const int msb_b, const int max_bd,
                          int *rem_shift) {
  assert(shift >= 0);

  // Remaining bit shifts (may be used in the next stage of multiplcation)
  int rem = AOMMAX(0, msb_a + msb_b - shift + 1 - max_bd);
  if (rem_shift) *rem_shift += rem;
  if (msb_a + msb_b + 1 <= max_bd)
    return ROUND_POWER_OF_TWO_SIGNED_64(a * b, shift);

  // To determine s1/s2/s3 in ((a>>s1)*(b>>s2))>>s3, consider the equation
  //   (1+msb_a-s1)+(1+msb_b-s2)+1 <= max_bd+rem,
  // where better numerical stability is obtained when
  //   msb_a-s1 ~= msb_b-s2.
  // This leads to the following solution
  int msb_diff = abs(msb_a - msb_b);
  // Total required shifts (s1 + s2)
  int s = msb_a + msb_b - max_bd - rem + 3;
  int diff = AOMMIN(s, msb_diff);
  int s1 = (s - diff) >> 1;
  int s2 = s1;
  if (msb_a >= msb_b)
    s1 += diff;
  else
    s2 += diff;

  assert(s1 >= 0);
  assert(s2 >= 0);
  if (shift - s1 - s2 < 0) {
    // bit depth not large enough to hold the result
    return ((a > 0) ^ (b > 0)) ? -((1 << (max_bd - 1)) - 1)
                               : ((1 << (max_bd - 1)) - 1);
  }
  return ROUND_POWER_OF_TWO_SIGNED_64(
      ROUND_POWER_OF_TWO_SIGNED_64(a, s1) * ROUND_POWER_OF_TWO_SIGNED_64(b, s2),
      shift - s1 - s2);
}

// Perform Gaussian elimination routine to solve a matrix inverse problem
int gaussian_elimination(int64_t *mat, int64_t *sol, int *precbits,
                         const int dim) {
  int shifts[MAX_LS_DIM] = { 0 };
  int16_t inv_pivot[MAX_LS_DIM] = { 0 };
  int16_t inv_pivot_shift[MAX_LS_DIM] = { 0 };

  // Bit range adjustment: add shifts such that the bit depths of shifted mat
  // and sol elements are capped by K-dim+1. This is because each element of
  // mat and sol during forward elimination is updated at most dim-1 times.
  // Each update is a subtraction that can increase the bit depth by 1 at the
  // extreme case.
  // Goal: shift Aij by si+sj+e bits, and shift bi by si+e+f bits. These shifts
  // will satisfy
  //     1+MSB(Aij)+si+sj+e <= (K-dim+1)-1 unsigned bits
  //     1+MSB(bi)+si+e+f <= (K-dim+1)-1 unsigned bits
  //     precbits[i]-f+si <= 0
  // The last constraint is preferred but not strictly required, since
  // precbits[i]-f+si shifts will be applied to b at the end. Making all these
  // shifts negative means there is no loss of precision during the Gaussian
  // elimination procedure. One quick solution is given as follows:
  //     si=floor(min(K-dim+1-MSB(Aii), min(MSB(Aii))-MSB(Aii))/2)
  //     f=max_i(precbits[i]+si)
  //     e=min(K-dim+1-MSB(bi)-si) - max(precbits[i]+si)
  int bd_cap = MAX_LS_BITS - dim;
  int a_extra_shift = 64;
  int b_extra_shift = -64;
  int min_diag_msb = 64;
  int mat_diag_bits[MAX_LS_DIM] = { 0 };
  int sol_bits[MAX_LS_DIM] = { 0 };
  for (int i = 0; i < dim; i++) {
    mat_diag_bits[i] = 1 + get_msb_signed_64(mat[i * dim + i]);
    min_diag_msb = AOMMIN(min_diag_msb, mat_diag_bits[i]);
    sol_bits[i] = 1 + get_msb_signed_64(sol[i]);
  }
  for (int i = 0; i < dim; i++) {
    shifts[i] =
        -(AOMMAX(mat_diag_bits[i] - bd_cap, mat_diag_bits[i] - min_diag_msb) >>
          1);
    a_extra_shift = AOMMIN(a_extra_shift, bd_cap - sol_bits[i] - shifts[i]);
    b_extra_shift = AOMMAX(b_extra_shift, precbits[i] + shifts[i]);
  }
  a_extra_shift -= b_extra_shift;
  for (int i = 0; i < dim; i++) {
    for (int j = 0; j < dim; j++) {
      int abits = a_extra_shift + shifts[i] + shifts[j];
      assert(a_extra_shift < 64);
      mat[i * dim + j] =
          abits >= 0 ? (mat[i * dim + j] * (1 << abits))
                     : ROUND_POWER_OF_TWO_SIGNED_64(mat[i * dim + j], -abits);
    }
    int bbits = shifts[i] + a_extra_shift + b_extra_shift;
    sol[i] = bbits >= 0 ? (sol[i] * (1 << bbits))
                        : ROUND_POWER_OF_TWO_SIGNED_64(sol[i], -bbits);
    precbits[i] = precbits[i] - b_extra_shift + shifts[i];
  }

  // Elimination for the i-th column
  int64_t diff = 0;
  for (int i = 0; i < dim; i++) {
    int64_t pivot = mat[i * dim + i];
    int idx_pivot = i;

    for (int j = i + 1; j < dim; j++) {
      int64_t new_pivot = mat[j * dim + i];
      if (llabs(new_pivot) > llabs(pivot)) {
        idx_pivot = j;
        pivot = new_pivot;
      }
    }

    // Check singularity
    if (pivot == 0) return 0;

    // Put the row with the pivot first, and get inverse of the pivot
    if (i != idx_pivot) swap_rows(mat, sol, i, idx_pivot, dim);
    inv_pivot[i] = (pivot > 0 ? 1 : -1) *
                   resolve_divisor_64(llabs(pivot), inv_pivot_shift + i);

    for (int k = i + 1; k < dim; k++) {
      // Compute Akj = Akj - Aki * Aij / Aii, while keeping all intermediate
      // result within K bits
      int msb_ki = get_msb_signed_64(mat[k * dim + i]);
      int msb_invpiv = get_msb_signed(inv_pivot[i]);
      // Apply an upshift first if intermediate results will be close to zero.
      int inc_bits = AOMMAX(
          0, GE_MULT_PREC_BITS - msb_ki - msb_invpiv + inv_pivot_shift[i]);
      int fshift = inc_bits;
      int64_t f = stable_mult_shift(mat[k * dim + i], (int64_t)inv_pivot[i],
                                    inv_pivot_shift[i] - inc_bits, msb_ki,
                                    msb_invpiv, MAX_LS_BITS, &fshift);
      int msb_f = get_msb_signed_64(f);
      mat[k * dim + i] = 0;

      for (int j = i + 1; j < dim; j++) {
        int msb_ij = get_msb_signed_64(mat[i * dim + j]);
        diff = stable_mult_shift(mat[i * dim + j], f, fshift, msb_ij, msb_f,
                                 MAX_LS_BITS, NULL);
        mat[k * dim + j] -= diff;
      }
      int msb_sol = get_msb_signed_64(sol[i]);
      diff = stable_mult_shift(sol[i], f, fshift, msb_sol, msb_f, MAX_LS_BITS,
                               NULL);
      sol[k] -= diff;
    }
  }

  // Backward substitution
  for (int i = dim - 1; i >= 0; i--) {
    // To reduce bit depth requirement, do a MSB check and downshift the entire
    // matrix row: 1+MSB(Aij)+1+MSB(bj) <= K-1-2(3 subtractions), for all i,j.
    int max_mult_bits = 0;
    for (int j = i + 1; j < dim; j++)
      max_mult_bits =
          AOMMAX(max_mult_bits, 2 + get_msb_signed_64(mat[i * dim + j]) +
                                    get_msb_signed_64(sol[j]));
    int redbit = AOMMAX(0, max_mult_bits - MAX_LS_BITS + 3);
    sol[i] = ROUND_POWER_OF_TWO_SIGNED_64(sol[i], redbit);
    for (int j = i + 1; j < dim; j++) {
      diff = ROUND_POWER_OF_TWO_SIGNED_64(mat[i * dim + j], redbit) * sol[j];
      sol[i] = sol[i] - diff;
    }
    sol[i] = stable_mult_shift(sol[i], (int64_t)inv_pivot[i],
                               inv_pivot_shift[i] - redbit,
                               get_msb_signed_64(sol[i]),
                               get_msb_signed(inv_pivot[i]), MAX_LS_BITS, NULL);
  }

  // Apply remaining downscaling
  for (int i = 0; i < dim; i++)
    sol[i] = precbits[i] >= 0
                 ? (sol[i] * (1 << precbits[i]))
                 : ROUND_POWER_OF_TWO_SIGNED_64(sol[i], -precbits[i]);

  return 1;
}
#else
// Compute intermediate results for 4D linear solver.
void getsub_4d(int64_t *sub, const int64_t *mat, const int64_t *vec) {
  sub[0] = mat[0] * mat[5] - mat[1] * mat[4];
  sub[1] = mat[0] * mat[6] - mat[2] * mat[4];
  sub[2] = mat[0] * mat[7] - mat[3] * mat[4];
  sub[3] = mat[0] * vec[1] - vec[0] * mat[4];
  sub[4] = mat[1] * mat[6] - mat[2] * mat[5];
  sub[5] = mat[1] * mat[7] - mat[3] * mat[5];
  sub[6] = mat[1] * vec[1] - vec[0] * mat[5];
  sub[7] = mat[2] * mat[7] - mat[3] * mat[6];
  sub[8] = mat[2] * vec[1] - vec[0] * mat[6];
  sub[9] = mat[3] * vec[1] - vec[0] * mat[7];
}

// Solve a 4-dimensional matrix inverse using inverse determinant method:
// x = A^(-1) * b, where A: mat, b: vec, x: sol
int inverse_determinant_4d(int64_t *mat, int64_t *vec, int *precbits,
                           int64_t *sol) {
  int64_t a[10], b[10];  // values of 20 2D subdeterminants
  getsub_4d(&a[0], mat, vec);
  getsub_4d(&b[0], mat + 8, vec + 2);

  // Flexibly adjust range to avoid overflow without losing precision. This
  // moves the bit depth of a[] and b[] within 29 so that det and sol will
  // not overflow
  int64_t max_el = 0;
  for (int i = 0; i < 10; i++) {
    max_el = AOMMAX(max_el, llabs(a[i]));
    max_el = AOMMAX(max_el, llabs(b[i]));
  }
  int max_bits = get_msb_signed_64(max_el);
  int subdet_reduce_bits = AOMMAX(0, max_bits - 28);
  for (int i = 0; i < 10; i++) {
    a[i] = ROUND_POWER_OF_TWO_SIGNED_64(a[i], subdet_reduce_bits);
    b[i] = ROUND_POWER_OF_TWO_SIGNED_64(b[i], subdet_reduce_bits);
  }

  int64_t det = a[0] * b[7] + a[7] * b[0] + a[2] * b[4] + a[4] * b[2] -
                a[5] * b[1] - a[1] * b[5];

  sol[0] = a[5] * b[8] + a[8] * b[5] - a[6] * b[7] - a[7] * b[6] - a[4] * b[9] -
           a[9] * b[4];
  sol[1] = a[1] * b[9] + a[9] * b[1] + a[3] * b[7] + a[7] * b[3] - a[2] * b[8] -
           a[8] * b[2];
  sol[2] = a[2] * b[6] + a[6] * b[2] - a[0] * b[9] - a[9] * b[0] - a[3] * b[5] -
           a[5] * b[3];
  sol[3] = a[0] * b[8] + a[8] * b[0] + a[3] * b[4] + a[4] * b[3] - a[6] * b[1] -
           a[1] * b[6];

  int max_det_msb = get_msb_signed_64(det);
  for (int i = 0; i < 4; i++)
    max_det_msb = AOMMAX(max_det_msb, get_msb_signed_64(sol[i]) + precbits[i]);

  int det_red_bits = AOMMAX(0, max_det_msb - 60);
  det = ROUND_POWER_OF_TWO_SIGNED_64(det, det_red_bits);
  if (det <= 0) return 0;

  for (int i = 0; i < 4; i++) {
    int reduce_bits = det_red_bits - precbits[i];
    if (reduce_bits >= 0)
      sol[i] = ROUND_POWER_OF_TWO_SIGNED_64(sol[i], reduce_bits);
    else
      sol[i] = clamp64(sol[i] * (1 << (-reduce_bits)), INT64_MIN, INT64_MAX);
  }

  sol[0] = divide_and_round_signed(sol[0], det);
  sol[1] = divide_and_round_signed(sol[1], det);
  sol[2] = divide_and_round_signed(sol[2], det);
  sol[3] = divide_and_round_signed(sol[3], det);
  return 1;
}
#endif  // CONFIG_REFINEMENT_SIMPLIFY

// Solve a 4-dimensional matrix inverse
int solver_4d(int64_t *mat, int64_t *vec, int *precbits, int64_t *sol) {
#if CONFIG_REFINEMENT_SIMPLIFY
  memcpy(sol, vec, 4 * sizeof(int64_t));
  int ret = gaussian_elimination(mat, sol, precbits, 4);
#else
  int ret = inverse_determinant_4d(mat, vec, precbits, sol);
#endif  // CONFIG_REFINEMENT_SIMPLIFY
  return ret;
}
#endif  // CONFIG_AFFINE_REFINEMENT

#if CONFIG_OPTFLOW_REFINEMENT
// Restrict MV delta to 1 or 2 pixels. This restriction would reduce complexity
// in hardware.
#define OPFL_CLAMP_MV_DELTA 1
#define OPFL_MV_DELTA_LIMIT (1 << MV_REFINE_PREC_BITS)

// Divide d0 and d1 by their common factors (no divisions)
void reduce_temporal_dist(int *d0, int *d1) {
  if (*d0 == 0 || *d1 == 0) return;
  int sign0 = *d0 < 0;
  int sign1 = *d1 < 0;
  int mag0 = sign0 ? -(*d0) : (*d0);
  int mag1 = sign1 ? -(*d1) : (*d1);
  // Only do simple checks for the case |d0|=|d1| and for factor 2
  if (mag0 == mag1) {
    mag0 = mag1 = 1;
  } else {
    while (mag0 % 2 == 0 && mag1 % 2 == 0) {
      assert(mag0 > 0 && mag1 > 0);
      mag0 >>= 1;
      mag1 >>= 1;
    }
  }
  *d0 = sign0 ? -mag0 : mag0;
  *d1 = sign1 ? -mag1 : mag1;
  return;
}

void av1_opfl_build_inter_predictor(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    InterPredParams *inter_pred_params,
    CalcSubpelParamsFunc calc_subpel_params_func, int ref, uint16_t *pred_dst
#if CONFIG_REFINEMV
    ,
    const MV *const src_mv, int pu_width, int pu_height
#endif  // CONFIG_REFINEMV
) {
  assert(cm->seq_params.order_hint_info.enable_order_hint);
  const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
#if CONFIG_OPTFLOW_ON_TIP
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;
#endif  // CONFIG_OPTFLOW_ON_TIP

  // Do references one at a time
  const int is_compound = 0;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  struct buf_2d *const dst_buf = &pd->dst;

  const WarpedMotionParams *const wm = &xd->global_motion[mi->ref_frame[ref]];
  const WarpTypesAllowed warp_types = { is_global_mv_block(mi, wm->wmtype),
                                        is_warp_mode(mi->motion_mode) };
#if CONFIG_OPTFLOW_ON_TIP
  const struct scale_factors *const sf =
      is_tip
          ? cm->tip_ref.ref_scale_factor[ref]
          : (is_intrabc ? &cm->sf_identity : xd->block_ref_scale_factors[ref]);
#else
  const struct scale_factors *const sf =
      is_intrabc ? &cm->sf_identity : xd->block_ref_scale_factors[ref];
#endif  // CONFIG_OPTFLOW_ON_TIP

  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
#if CONFIG_REFINEMV
  const int row_start = (bw == 4) && ss_y ? -1 : 0;
  const int col_start = (bh == 4) && ss_x ? -1 : 0;
#else
  const BLOCK_SIZE bsize = mi->sb_type[PLANE_TYPE_Y];
  const int row_start = (block_size_high[bsize] == 4) && ss_y ? -1 : 0;
  const int col_start = (block_size_wide[bsize] == 4) && ss_x ? -1 : 0;
#endif  // CONFIG_REFINEMV

  const int pre_x = (mi_x + MI_SIZE * col_start) >> ss_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> ss_y;

#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
  const struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#else
  const struct buf_2d *const pre_buf =
      is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref]
             : (is_intrabc ? dst_buf : &pd->pre[ref]);
#endif  // CONFIG_TIP_REF_PRED_MERGING
#else
  struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#endif  // CONFIG_OPTFLOW_ON_TIP

  av1_init_inter_params(inter_pred_params, bw, bh, pre_y, pre_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd,
                        mi->use_intrabc[0], sf, pre_buf,
#if CONFIG_OPFL_BI
                        BILINEAR
#else
                        mi->interp_fltr
#endif
  );
#if CONFIG_REFINEMV
  inter_pred_params->original_pu_width = pu_width;
  inter_pred_params->original_pu_height = pu_height;
#endif  // CONFIG_REFINEMV

#if !CONFIG_TIP_REF_PRED_MERGING
  const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
  const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
  inter_pred_params->dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
  inter_pred_params->dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - pre_y);
  inter_pred_params->dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
  inter_pred_params->dist_to_right_edge = GET_MV_SUBPEL(width - bw - pre_x);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

  inter_pred_params->conv_params = get_conv_params_no_round(
      0, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

  av1_init_warp_params(inter_pred_params, &warp_types, ref, xd, mi);
  if (inter_pred_params->mode == WARP_PRED) return;

  assert(mi->interinter_comp.type == COMPOUND_AVERAGE);

  av1_build_one_inter_predictor(pred_dst, bw,
#if CONFIG_REFINEMV
                                src_mv,
#else
                                &mi->mv[ref].as_mv,
#endif  // CONFIG_REFINEMV
                                inter_pred_params, xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
}

void av1_bicubic_grad_interpolation_highbd_c(const int16_t *pred_src,
                                             int16_t *x_grad, int16_t *y_grad,
                                             const int bw, const int bh) {
#if OPFL_BICUBIC_GRAD
  for (int i = 0; i < bh; i++) {
    for (int j = 0; j < bw; j++) {
      int id_prev, id_prev2, id_next, id_next2, is_boundary;
      int32_t temp = 0;
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      // Subtract interpolated pixel at (i, j+delta) by the one at (i, j-delta)
      id_prev = AOMMAX(j - 1, 0);
      id_prev2 = AOMMAX(j - 2, 0);
      id_next = AOMMIN(j + 1, bw - 1);
      id_next2 = AOMMIN(j + 2, bw - 1);
      is_boundary = (j + 1 > bw - 1 || j - 1 < 0);
      temp = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][is_boundary] *
                 (int32_t)(pred_src[i * bw + id_next] -
                           pred_src[i * bw + id_prev]) +
             coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][is_boundary] *
                 (int32_t)(pred_src[i * bw + id_next2] -
                           pred_src[i * bw + id_prev2]);
      x_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bicubic_bits),
                                 INT16_MIN, INT16_MAX);

      // Subtract interpolated pixel at (i+delta, j) by the one at (i-delta, j)
      id_prev = AOMMAX(i - 1, 0);
      id_prev2 = AOMMAX(i - 2, 0);
      id_next = AOMMIN(i + 1, bh - 1);
      id_next2 = AOMMIN(i + 2, bh - 1);
      is_boundary = (i + 1 > bh - 1 || i - 1 < 0);
      temp = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][is_boundary] *
                 (int32_t)(pred_src[id_next * bw + j] -
                           pred_src[id_prev * bw + j]) +
             coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][is_boundary] *
                 (int32_t)(pred_src[id_next2 * bw + j] -
                           pred_src[id_prev2 * bw + j]);
      y_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bicubic_bits),
                                 INT16_MIN, INT16_MAX);
    }
  }
#else
  (void)pred_src;
  (void)x_grad;
  (void)y_grad;
  (void)bw;
  (void)bh;
#endif  // OPFL_BICUBIC_GRAD
}

#if OPFL_BILINEAR_GRAD
void av1_bilinear_grad_interpolation_c(const int16_t *pred_src, int16_t *x_grad,
                                       int16_t *y_grad, const int bw,
                                       const int bh) {
  int id_next, id_prev, is_boundary;
  int32_t temp = 0;
  for (int i = 0; i < bh; i++) {
    for (int j = 0; j < bw; j++) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      // Subtract interpolated pixel at (i, j+delta) by the one at (i, j-delta)
      id_next = AOMMIN(j + 1, bw - 1);
      id_prev = AOMMAX(j - 1, 0);
      is_boundary = (j + 1 > bw - 1 || j - 1 < 0);
      temp = coeffs_bilinear[SUBPEL_GRAD_DELTA_BITS][is_boundary] *
             (int32_t)(pred_src[i * bw + id_next] - pred_src[i * bw + id_prev]);
      x_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bilinear_bits),
                                 INT16_MIN, INT16_MAX);
      // Subtract interpolated pixel at (i+delta, j) by the one at (i-delta, j)
      id_next = AOMMIN(i + 1, bh - 1);
      id_prev = AOMMAX(i - 1, 0);
      is_boundary = (i + 1 > bh - 1 || i - 1 < 0);
      temp = coeffs_bilinear[SUBPEL_GRAD_DELTA_BITS][is_boundary] *
             (int32_t)(pred_src[id_next * bw + j] - pred_src[id_prev * bw + j]);
      y_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bilinear_bits),
                                 INT16_MIN, INT16_MAX);
    }
  }
}
#endif  // OPFL_BILINEAR_GRAD

void av1_compute_subpel_gradients_interp(int16_t *pred_dst, int bw, int bh,
                                         int *grad_prec_bits, int16_t *x_grad,
                                         int16_t *y_grad) {
  // Reuse pixels in pred_dst to compute gradients
#if OPFL_BILINEAR_GRAD
  (void)is_hbd;
  av1_bilinear_grad_interpolation_c(pred_dst, x_grad, y_grad, bw, bh);
#else
#if CONFIG_OPFL_MV_SEARCH
  if (bw < 8 || bh < 8)
    av1_bicubic_grad_interpolation_highbd_c(pred_dst, x_grad, y_grad, bw, bh);
  else
#endif  // CONFIG_OPFL_MV_SEARCH
    av1_bicubic_grad_interpolation_highbd(pred_dst, x_grad, y_grad, bw, bh);
#endif  // OPFL_BILINEAR_GRAD
  *grad_prec_bits = 3 - SUBPEL_GRAD_DELTA_BITS - 2;
}

#if CONFIG_AFFINE_REFINEMENT || CONFIG_OPFL_MV_SEARCH
// Apply average pooling to reduce the sizes of pred difference and gradients
// arrays. It reduces the complexity of the parameter solving routine
void av1_avg_pooling_pdiff_gradients_c(int16_t *pdiff, const int pstride,
                                       int16_t *gx, int16_t *gy,
                                       const int gstride, const int bw,
                                       const int bh, const int n) {
  const int bh_low = AOMMIN(bh, n);
  const int bw_low = AOMMIN(bw, n);
  const int step_h = bh / bh_low;
  const int step_w = bw / bw_low;
  int avg_stride = bw;
#if OPFL_DOWNSAMP_QUINCUNX
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w) - 1;
#else
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
#endif
  for (int i = 0; i < bh_low; i++) {
    for (int j = 0; j < bw_low; j++) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      int32_t tmp_gx = 0, tmp_gy = 0, tmp_pdiff = 0;
      for (int k = 0; k < step_h; k++) {
        for (int l = 0; l < step_w; l++) {
#if OPFL_DOWNSAMP_QUINCUNX
          if ((i * step_h + j * step_w + k + l) % 2 == 1) continue;
#endif
          tmp_gx += gx[(i * step_h + k) * gstride + (j * step_w + l)];
          tmp_gy += gy[(i * step_h + k) * gstride + (j * step_w + l)];
          tmp_pdiff += pdiff[(i * step_h + k) * pstride + (j * step_w + l)];
        }
      }
      gx[i * avg_stride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_gx, avg_bits);
      gy[i * avg_stride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_gy, avg_bits);
      pdiff[i * avg_stride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_pdiff, avg_bits);
    }
  }
}
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_OPFL_MV_SEARCH

#if CONFIG_AFFINE_REFINEMENT
/* Map affine model parameters to warped motion parameters based on signed
   temporal distance d (positive for past ref, negative for future ref).

   For d < 0, let t = -d > 0, the affine model is
   /x'\ = / cos(t*theta)  -sin(t*theta) \ * /1+t*alpha   0   \ * /x\ + / t*tx \
   \y'/   \ sin(t*theta)   cos(t*theta) /   \    0   1+t*beta/   \y/   \ t*ty /

   which is associated with warped motion matrix

        / (1+t*alpha)*cos(t*theta)  -(1+t*beta)*sin(t*theta)  t*tx \
   A = |  (1+t*alpha)*sin(t*theta)   (1+t*beta)*cos(t*theta)  t*ty  |
        \            0                         0                1  /

   For d > 0, we let t = d > 0, and the warped motion matrix is given by the
   inverse matrix of A. Approximate 1/(1+x) by 1-x, then

    -1    / (1-t*alpha)*cos(t*theta)  (1-t*alpha)*sin(t*theta)  tx' \
   A   = |  -(1-t*beta)*sin(t*theta)  (1-t*beta)*cos(t*theta)   ty'  |
          \             0                         0              1  /,

   where tx' = -t*(1-t*alpha)*[cos(t*theta)*tx+sin(t*theta)*ty]
         ty' = t*(1-t*beta)*[cos(t*theta)*tx+sin(t*theta)*ty]
*/
void get_ref_affine_params(int bw, int bh, int mi_x, int mi_y,
                           const AffineModelParams *am_params,
                           WarpedMotionParams *wm, const int d,
                           const MV *const mv) {
  wm->invalid = 1;

  const int unit_offset = 1 << WARPEDMODEL_PREC_BITS;
  int64_t cos_angle = unit_offset;
  int64_t sin_angle = 0;
  const int64_t scale_x = unit_offset - d * am_params->scale_alpha;
  const int64_t scale_y = unit_offset - d * am_params->scale_beta;

  const int angle = -d * am_params->rot_angle;
  cos_angle = unit_offset;
  sin_angle = angle * (1 << (WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS));
  wm->wmmat[2] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(scale_x * cos_angle,
                                                       WARPEDMODEL_PREC_BITS);
  wm->wmmat[5] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(scale_y * cos_angle,
                                                       WARPEDMODEL_PREC_BITS);
  if (d > 0) {
    // Parameters of A^-1
    wm->wmmat[3] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(-scale_x * sin_angle,
                                                         WARPEDMODEL_PREC_BITS);
    wm->wmmat[4] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(scale_y * sin_angle,
                                                         WARPEDMODEL_PREC_BITS);
    int64_t tmp_tx = (int64_t)wm->wmmat[2] * (int64_t)am_params->tran_x -
                     (int64_t)wm->wmmat[3] * (int64_t)am_params->tran_y;
    int64_t tmp_ty = (int64_t)wm->wmmat[4] * (int64_t)am_params->tran_x +
                     (int64_t)wm->wmmat[5] * (int64_t)am_params->tran_y;
    wm->wmmat[0] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(-d * tmp_tx,
                                                         WARPEDMODEL_PREC_BITS);
    wm->wmmat[1] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(-d * tmp_ty,
                                                         WARPEDMODEL_PREC_BITS);
  } else {
    // Parameters of A
    wm->wmmat[3] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(-scale_y * sin_angle,
                                                         WARPEDMODEL_PREC_BITS);
    wm->wmmat[4] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(scale_x * sin_angle,
                                                         WARPEDMODEL_PREC_BITS);
    wm->wmmat[0] = -d * am_params->tran_x;
    wm->wmmat[1] = -d * am_params->tran_y;
  }
#if CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - unit_offset);
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - unit_offset);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[6] = wm->wmmat[7] = 0;

#if CONFIG_EXTENDED_WARP_PREDICTION
  av1_reduce_warp_model(wm);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_EXT_WARP_FILTER
  av1_get_shear_params(wm);
#else
  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm)) {
    wm->wmmat[2] = default_warp_params.wmmat[2];
    wm->wmmat[3] = default_warp_params.wmmat[3];
    wm->wmmat[4] = default_warp_params.wmmat[4];
    wm->wmmat[5] = default_warp_params.wmmat[5];
    wm->alpha = wm->beta = wm->gamma = wm->delta = 0;
  }
#endif  // CONFIG_EXT_WARP_FILTER

  // Apply offset based on the coordinate of the block center and the MV to
  // convert the base point of warped motion from block center to the top-left
  // pixel of the frame.
  const int center_x = mi_x + bw / 2 - 1;
  const int center_y = mi_y + bh / 2 - 1;
  wm->wmmat[0] +=
      mv->col * (1 << (WARPEDMODEL_PREC_BITS - 3)) -
      (center_x * (wm->wmmat[2] - unit_offset) + center_y * wm->wmmat[3]);
  wm->wmmat[1] +=
      mv->row * (1 << (WARPEDMODEL_PREC_BITS - 3)) -
      (center_x * wm->wmmat[4] + center_y * (wm->wmmat[5] - unit_offset));

#if CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - unit_offset);
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - unit_offset);
#else
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

  wm->wmtype = AFFINE;
  wm->invalid = 0;
}

// Find the maximum element of pdiff/gx/gy in absolute value
int find_max_matrix_element(const int16_t *pdiff, int pstride,
                            const int16_t *gx, const int16_t *gy, int gstride,
                            int bw, int bh) {
  // TODO(kslu) do it in a better way to remove repeated computations, or
  // handle this in gradient computation
  int max_el = 0;
  for (int i = 0; i < bh; i++) {
    for (int j = 0; j < bw; j++) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      if (AOMMAX(i, j) >= AFFINE_AVG_MAX_SIZE) continue;
      max_el = AOMMAX(max_el, abs((int)gx[i * gstride + j]));
      max_el = AOMMAX(max_el, abs((int)gy[i * gstride + j]));
      max_el = AOMMAX(max_el, abs((int)pdiff[i * pstride + j]));
    }
  }
  return max_el;
}

// Autocorrelation matrix filling procedure for affine refinement.
void av1_calc_affine_autocorrelation_matrix_c(const int16_t *pdiff, int pstride,
                                              const int16_t *gx,
                                              const int16_t *gy, int gstride,
                                              int bw, int bh,
#if CONFIG_AFFINE_REFINEMENT_SB
                                              int x_offset, int y_offset,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
                                              int64_t *mat_a, int64_t *vec_b) {
  int x_range_log2 = get_msb(bw);
  int y_range_log2 = get_msb(bh);
  int step_h = AOMMAX(1, bh >> AFFINE_AVG_MAX_SIZE_LOG2);
  int step_w = AOMMAX(1, bw >> AFFINE_AVG_MAX_SIZE_LOG2);
  int npel_log2 = AOMMIN(AFFINE_AVG_MAX_SIZE_LOG2, x_range_log2) +
                  AOMMIN(AFFINE_AVG_MAX_SIZE_LOG2, y_range_log2);
#if OPFL_DOWNSAMP_QUINCUNX
  npel_log2--;
#endif
  // Check range of gradient and prediction differences. If maximum absolute
  // value is very large, matrix A is likely to be clamped. To improve
  // stability, we adaptively reduce the dynamic range here
  int max_el = find_max_matrix_element(pdiff, pstride, gx, gy, gstride, bw, bh);
  int max_el_msb = max_el > 0 ? get_msb(max_el) : 0;
  int grad_bits =
      AOMMAX(0, max_el_msb * 2 + npel_log2 +
                    AOMMAX(x_range_log2, y_range_log2) - AFFINE_GRAD_BITS_THR);
  const int coords_bits = AOMMAX(
      0, ((x_range_log2 + y_range_log2) >> 1) - AFFINE_COORDS_OFFSET_BITS);
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      if (AOMMAX(i, j) >= AFFINE_AVG_MAX_SIZE) continue;
#if CONFIG_AFFINE_REFINEMENT_SB
      // Offsets are added in order to obtain affine parameter relative to
      // original block center rather than the subblock center
      const int x = step_w * j - bw / 2 + x_offset + 1;
      const int y = step_h * i - bh / 2 + y_offset + 1;
#else
      const int x = step_w * j - bw / 2 + 1;
      const int y = step_h * i - bh / 2 + 1;
#endif  // CONFIG_AFFINE_REFINEMENT_SB
      int gidx = i * gstride + j;
      int a[4];
      a[0] =
          ROUND_POWER_OF_TWO_SIGNED(-gx[gidx] * y + gy[gidx] * x, coords_bits);
      a[1] =
          ROUND_POWER_OF_TWO_SIGNED(gx[gidx] * x + gy[gidx] * y, coords_bits);
      a[2] = gx[gidx];
      a[3] = gy[gidx];
      for (int s = 0; s < 4; ++s)
        a[s] = clamp(a[s], -AFFINE_SAMP_CLAMP_VAL, AFFINE_SAMP_CLAMP_VAL);
#if CONFIG_REFINEMENT_SIMPLIFY
      const int d = clamp(pdiff[i * pstride + j], -AFFINE_SAMP_CLAMP_VAL,
                          AFFINE_SAMP_CLAMP_VAL);
#else
      const int d = pdiff[i * pstride + j];
#endif  // CONFIG_REFINEMENT_SIMPLIFY
      for (int s = 0; s < 4; ++s) {
        for (int t = 0; t <= s; ++t) {
          mat_a[s * 4 + t] += ROUND_POWER_OF_TWO_SIGNED_64(
              (int64_t)a[s] * (int64_t)a[t], grad_bits);
        }
        vec_b[s] +=
            ROUND_POWER_OF_TWO_SIGNED_64((int64_t)a[s] * (int64_t)d, grad_bits);
      }
    }
#if CONFIG_REFINEMENT_SIMPLIFY
    // Do a range check and add a downshift if range is getting close to the bit
    // depth cap. This check is done for every 16 pixels so it can be easily
    // replicated in the SIMD version.
    if (bw >= 16 || i % 2 == 1) {
      int64_t max_autocorr =
          AOMMAX(AOMMAX(mat_a[0], mat_a[5]), AOMMAX(mat_a[10], mat_a[15]));
      int64_t max_xcorr = AOMMAX(AOMMAX(llabs(vec_b[0]), llabs(vec_b[1])),
                                 AOMMAX(llabs(vec_b[2]), llabs(vec_b[3])));
      if (get_msb_signed_64(AOMMAX(max_autocorr, max_xcorr)) >=
          MAX_AFFINE_AUTOCORR_BITS - 2) {
        for (int s = 0; s < 4; ++s) {
          for (int t = 0; t <= s; ++t)
            mat_a[s * 4 + t] =
                ROUND_POWER_OF_TWO_SIGNED_64(mat_a[s * 4 + t], 1);
          vec_b[s] = ROUND_POWER_OF_TWO_SIGNED_64(vec_b[s], 1);
        }
        grad_bits++;
      }
    }
#endif  // CONFIG_REFINEMENT_SIMPLIFY
  }
  for (int s = 0; s < 4; ++s) {
    for (int t = s + 1; t < 4; ++t) mat_a[s * 4 + t] = mat_a[t * 4 + s];
  }
  const int rls_alpha = (bw * bh >> 4) * AFFINE_RLS_PARAM;
  mat_a[0] += rls_alpha;
  mat_a[5] += rls_alpha;
  mat_a[10] += rls_alpha;
  mat_a[15] += rls_alpha;

#if !CONFIG_REFINEMENT_SIMPLIFY
  for (int s = 0; s < 4; ++s) {
    for (int t = 0; t < 4; ++t) {
      mat_a[s * 4 + t] = clamp64(mat_a[s * 4 + t], -AFFINE_AUTOCORR_CLAMP_VAL,
                                 AFFINE_AUTOCORR_CLAMP_VAL);
    }
    vec_b[s] = clamp64(vec_b[s], -AFFINE_AUTOCORR_CLAMP_VAL,
                       AFFINE_AUTOCORR_CLAMP_VAL);
  }
#endif  // !CONFIG_REFINEMENT_SIMPLIFY
}

// Derivation of four parameters in the rotation-scale-translation affine model
// (in the pipeline where gradients are computed directly from d0*P0-d1*P1)
int av1_opfl_affine_refinement(const int16_t *pdiff, int pstride,
                               const int16_t *gx, const int16_t *gy,
                               int gstride, int bw, int bh,
#if CONFIG_AFFINE_REFINEMENT_SB
                               int x_offset, int y_offset,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
                               int grad_prec_bits,
                               AffineModelParams *am_params) {

  int64_t mat_a[16] = { 0 };
  int64_t vec_b[4] = { 0 };
  int64_t vec_x[4];
#if !OPFL_DOWNSAMP_QUINCUNX
  av1_calc_affine_autocorrelation_matrix(pdiff, pstride, gx, gy, gstride, bw,
                                         bh,
#if CONFIG_AFFINE_REFINEMENT_SB
                                         x_offset, y_offset,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
                                         mat_a, vec_b);
#else
  av1_calc_affine_autocorrelation_matrix_c(pdiff, pstride, gx, gy, gstride, bw,
                                           bh,
#if CONFIG_AFFINE_REFINEMENT_SB
                                           x_offset, y_offset,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
                                           mat_a, vec_b);
#endif  // !OPFL_DOWNSAMP_QUINCUNX

  const int coords_bits =
      AOMMAX(0, ((get_msb(bw) + get_msb(bh)) >> 1) - AFFINE_COORDS_OFFSET_BITS);
  int prec_bits[4] = {
    grad_prec_bits + AFFINE_PREC_BITS - coords_bits,
    grad_prec_bits + AFFINE_PREC_BITS - coords_bits,
    grad_prec_bits + AFFINE_PREC_BITS,
    grad_prec_bits + AFFINE_PREC_BITS,
  };
  if (!solver_4d(mat_a, vec_b, prec_bits, vec_x)) return 1;

  assert(WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS >= 0);
  am_params->rot_angle = (int)vec_x[0];
  am_params->scale_alpha =
      (int)vec_x[1] * (1 << (WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS));
  am_params->scale_beta =
      (int)vec_x[1] * (1 << (WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS));
  am_params->tran_x =
      (int)vec_x[2] * (1 << (WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS));
  am_params->tran_y =
      (int)vec_x[3] * (1 << (WARPEDMODEL_PREC_BITS - AFFINE_PREC_BITS));
  return 0;
}
#endif  // CONFIG_AFFINE_REFINEMENT

// Solve vx and vy given pdiff = P0 - P1 and the gradients gx/gy of
// d0 * P0 - d1 * P1.
void av1_opfl_mv_refinement(const int16_t *pdiff, int pstride,
                            const int16_t *gx, const int16_t *gy, int gstride,
                            int bw, int bh, int d0, int d1, int grad_prec_bits,
                            int mv_prec_bits, int *vx0, int *vy0, int *vx1,
                            int *vy1) {
  assert(IMPLIES(OPFL_DIST_RATIO_THR == 1, d0 + d1 == 0));
  int64_t su2 = 0;
  int64_t suv = 0;
  int64_t sv2 = 0;
  int64_t suw = 0;
  int64_t svw = 0;
  int grad_bits = 0;
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
#if CONFIG_REFINEMENT_SIMPLIFY
      const int u =
          clamp(gx[i * gstride + j], -OPFL_SAMP_CLAMP_VAL, OPFL_SAMP_CLAMP_VAL);
      const int v =
          clamp(gy[i * gstride + j], -OPFL_SAMP_CLAMP_VAL, OPFL_SAMP_CLAMP_VAL);
      const int w = clamp(pdiff[i * pstride + j], -OPFL_SAMP_CLAMP_VAL,
                          OPFL_SAMP_CLAMP_VAL);
#else
      const int u = gx[i * gstride + j];
      const int v = gy[i * gstride + j];
      const int w = pdiff[i * pstride + j];
#endif  // CONFIG_REFINEMENT_SIMPLIFY
      su2 += ROUND_POWER_OF_TWO_SIGNED_64(u * u, grad_bits);
      suv += ROUND_POWER_OF_TWO_SIGNED_64(u * v, grad_bits);
      sv2 += ROUND_POWER_OF_TWO_SIGNED_64(v * v, grad_bits);
      suw += ROUND_POWER_OF_TWO_SIGNED_64(u * w, grad_bits);
      svw += ROUND_POWER_OF_TWO_SIGNED_64(v * w, grad_bits);
    }
#if CONFIG_REFINEMENT_SIMPLIFY
    // For every 8 pixels, do a range check and add a downshift if range is
    // getting close to the max allowed bit depth
    if (bw >= 8 || i % 2 == 1) {
      // Do a range check and add a downshift if range is getting close to the
      // bit depth cap
      int64_t max_autocorr = AOMMAX(su2, sv2);
      int64_t max_xcorr = AOMMAX(llabs(suw), llabs(svw));
      if (get_msb_signed_64(AOMMAX(max_autocorr, max_xcorr)) >=
          MAX_OPFL_AUTOCORR_BITS - 2) {
        su2 = ROUND_POWER_OF_TWO_SIGNED_64(su2, 1);
        suv = ROUND_POWER_OF_TWO_SIGNED_64(suv, 1);
        sv2 = ROUND_POWER_OF_TWO_SIGNED_64(sv2, 1);
        suw = ROUND_POWER_OF_TWO_SIGNED_64(suw, 1);
        svw = ROUND_POWER_OF_TWO_SIGNED_64(svw, 1);
        grad_bits++;
      }
    }
#endif  // CONFIG_REFINEMENT_SIMPLIFY
  }
  const int bits = mv_prec_bits + grad_prec_bits;
#if OPFL_REGULARIZED_LS
  const int rls_alpha = (bw * bh >> 4) * OPFL_RLS_PARAM;
  su2 += rls_alpha;
  sv2 += rls_alpha;
#endif

#if !CONFIG_REFINEMENT_SIMPLIFY
  // Clamp su2, sv2, suv, suw, and svw to avoid overflow in det, det_x, and
  // det_y
  su2 = clamp64(su2, -OPFL_AUTOCORR_CLAMP_VAL, OPFL_AUTOCORR_CLAMP_VAL);
  sv2 = clamp64(sv2, -OPFL_AUTOCORR_CLAMP_VAL, OPFL_AUTOCORR_CLAMP_VAL);
  suv = clamp64(suv, -OPFL_AUTOCORR_CLAMP_VAL, OPFL_AUTOCORR_CLAMP_VAL);
  suw = clamp64(suw, -OPFL_AUTOCORR_CLAMP_VAL, OPFL_AUTOCORR_CLAMP_VAL);
  svw = clamp64(svw, -OPFL_AUTOCORR_CLAMP_VAL, OPFL_AUTOCORR_CLAMP_VAL);
#endif  // !CONFIG_REFINEMENT_SIMPLIFY

  // Solve 2x2 matrix inverse: [ su2  suv ]   [ vx0 ]     [ -suw ]
  //                           [ suv  sv2 ] * [ vy0 ]  =  [ -svw ]
#if CONFIG_REFINEMENT_SIMPLIFY
  int shifts[2] = { bits, bits };
  int msb_su2 = 1 + get_msb_signed_64(su2);
  int msb_sv2 = 1 + get_msb_signed_64(sv2);
  int msb_suv = 1 + get_msb_signed_64(suv);
  int msb_suw = 1 + get_msb_signed_64(suw);
  int msb_svw = 1 + get_msb_signed_64(svw);
  // Make sure the max bit depth of det, sol[0], and sol[1] are within
  // MAX_LS_BITS
  int max_mult_msb = AOMMAX(
      msb_su2 + msb_sv2, AOMMAX(AOMMAX(msb_sv2 + msb_suw, msb_suv + msb_svw),
                                AOMMAX(msb_su2 + msb_svw, msb_suv + msb_suw)));
  int redbit = AOMMAX(0, max_mult_msb - MAX_LS_BITS + 3) >> 1;

  su2 = ROUND_POWER_OF_TWO_SIGNED_64(su2, redbit);
  sv2 = ROUND_POWER_OF_TWO_SIGNED_64(sv2, redbit);
  suv = ROUND_POWER_OF_TWO_SIGNED_64(suv, redbit);
  suw = ROUND_POWER_OF_TWO_SIGNED_64(suw, redbit);
  svw = ROUND_POWER_OF_TWO_SIGNED_64(svw, redbit);
  const int64_t det = su2 * sv2 - suv * suv;
  if (det <= 0) {
    *vx0 = 0;
    *vy0 = 0;
    *vx1 = 0;
    *vy1 = 0;
    return;
  }

  int64_t sol[2] = { sv2 * suw - suv * svw, su2 * svw - suv * suw };

  divide_and_round_array(sol, det, 2, shifts);
  *vx0 = (int)-sol[0];
  *vy0 = (int)-sol[1];
#else
  const int64_t det = su2 * sv2 - suv * suv;
  if (det <= 0) {
    *vx0 = 0;
    *vy0 = 0;
    *vx1 = 0;
    *vy1 = 0;
    return;
  }
  const int64_t det_x = (suv * svw - sv2 * suw) * (1 << bits);
  const int64_t det_y = (suv * suw - su2 * svw) * (1 << bits);

  *vx0 = (int)divide_and_round_signed(det_x, det);
  *vy0 = (int)divide_and_round_signed(det_y, det);
#endif  // CONFIG_REFINEMENT_SIMPLIFY
  *vx1 = (*vx0) * d1;
  *vy1 = (*vy0) * d1;
  *vx0 = (*vx0) * d0;
  *vy0 = (*vy0) * d0;
}

int av1_opfl_mv_refinement_nxn_c(const int16_t *pdiff, int pstride,
                                 const int16_t *gx, const int16_t *gy,
                                 int gstride, int bw, int bh, int n, int d0,
                                 int d1, int grad_prec_bits, int mv_prec_bits,
                                 int *vx0, int *vy0, int *vx1, int *vy1) {
  assert(bw % n == 0 && bh % n == 0);
  int n_blocks = 0;
  for (int i = 0; i < bh; i += n) {
    for (int j = 0; j < bw; j += n) {
      av1_opfl_mv_refinement(pdiff + (i * pstride + j), pstride,
                             gx + (i * gstride + j), gy + (i * gstride + j),
                             gstride, n, n, d0, d1, grad_prec_bits,
                             mv_prec_bits, vx0 + n_blocks, vy0 + n_blocks,
                             vx1 + n_blocks, vy1 + n_blocks);
      n_blocks++;
    }
  }
  return n_blocks;
}

#if CONFIG_AFFINE_REFINEMENT
// Solve the affine model given pdiff = P0 - P1 and the gradients gx/gy of
// d0 * P0 - d1 * P1.
void av1_opfl_affine_refinement_mxn(int16_t *pdiff, int pstride, int16_t *gx,
                                    int16_t *gy, int gstride, int bw, int bh,
                                    int d0, int d1, int mi_x, int mi_y,
#if CONFIG_REFINEMV
                                    const MV *const src_mv,
#endif  // CONFIG_REFINEMV
                                    int grad_prec_bits,
                                    WarpedMotionParams *wms) {
  int n_blocks = 0;
#if CONFIG_AFFINE_REFINEMENT_SB
  int sub_bw = AOMMIN(AFFINE_MAX_UNIT, bw);
  int sub_bh = AOMMIN(AFFINE_MAX_UNIT, bh);
#else
  int sub_bw = bw;
  int sub_bh = bh;
#endif  // CONFIG_AFFINE_REFINEMENT_SB

  for (int i = 0; i < bh; i += sub_bh) {
    for (int j = 0; j < bw; j += sub_bw) {
      av1_avg_pooling_pdiff_gradients(
          pdiff + i * pstride + j, bw, gx + i * gstride + j,
          gy + i * gstride + j, gstride, sub_bw, sub_bh, AFFINE_AVG_MAX_SIZE);

      AffineModelParams affine_params = default_affine_params;
      // In some rare cases, the determinant in the solver may be zero or
      // negative due to numerical errors. In this case we still set invalid=0,
      // but the warped parameters remain the default values.
      if (!av1_opfl_affine_refinement(
              pdiff + i * pstride + j, pstride, gx + i * gstride + j,
              gy + i * gstride + j, gstride, sub_bw, sub_bh,
#if CONFIG_AFFINE_REFINEMENT_SB
              j + (sub_bw - bw) / 2, i + (sub_bh - bh) / 2,
#endif  // CONFIG_AFFINE_REFINEMENT_SB
              grad_prec_bits, &affine_params)) {
#if CONFIG_REFINEMV
        get_ref_affine_params(bw, bh, mi_x, mi_y, &affine_params,
                              wms + n_blocks * 2, d0, &src_mv[0]);
        get_ref_affine_params(bw, bh, mi_x, mi_y, &affine_params,
                              wms + n_blocks * 2 + 1, d1, &src_mv[1]);
#else
        get_ref_affine_params(bw, bh, mi_x, mi_y, &affine_params,
                              wms + n_blocks * 2, d0, &mbmi->mv[0].as_mv);
        get_ref_affine_params(bw, bh, mi_x, mi_y, &affine_params,
                              wms + n_blocks * 2 + 1, d1, &mbmi->mv[1].as_mv);
#endif  // CONFIG_REFINEMV
      }
      n_blocks++;
    }
  }
}

#if AFFINE_OPFL_BASED_ON_SAD
// TODO(kslu) use SIMD versions
static INLINE unsigned int sad_generic(const uint16_t *a, int a_stride,
                                       const uint16_t *b, int b_stride,
                                       int width, int height) {
  int y, x;
  unsigned int sad = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      sad += abs(a[x] - b[x]);
    }

    a += a_stride;
    b += b_stride;
  }
  return sad;
}
#endif  // AFFINE_OPFL_BASED_ON_SAD

// Update predicted blocks (P0 & P1) and their gradients based on the affine
// model derived from the first DAMR step
void update_pred_grad_with_affine_model(MACROBLOCKD *xd, int plane, int bw,
                                        int bh, WarpedMotionParams *wms,
                                        int mi_x, int mi_y, int16_t *tmp0,
                                        int16_t *tmp1, int16_t *gx0,
                                        int16_t *gy0, const int d0,
                                        const int d1, int *grad_prec_bits) {
  uint16_t *dst_warped =
      (uint16_t *)aom_memalign(16, 2 * bw * bh * sizeof(uint16_t));
  struct macroblockd_plane *const pd = &xd->plane[plane];
  ConvolveParams conv_params =
      get_conv_params_no_round(0, plane, NULL, 0, 0, xd->bd);
  for (int ref = 0; ref < 2; ref++) {
    struct buf_2d *const pre_buf = &pd->pre[ref];
#if CONFIG_AFFINE_REFINEMENT_SB
    int sub_bw = AOMMIN(AFFINE_MAX_UNIT, bw);
    int sub_bh = AOMMIN(AFFINE_MAX_UNIT, bh);
    int nb = 0;
    for (int i = 0; i < bh; i += sub_bh) {
      for (int j = 0; j < bw; j += sub_bw) {
#if AFFINE_FAST_WARP_METHOD == 3
        av1_warp_plane_bilinear(
            wms + 2 * nb + ref, xd->bd, pre_buf->buf0, pre_buf->width,
            pre_buf->height, pre_buf->stride,
            &dst_warped[ref * bw * bh + i * bw + j], mi_x + j, mi_y + i, sub_bw,
            sub_bh, bw, pd->subsampling_x, pd->subsampling_y, &conv_params);
#elif AFFINE_FAST_WARP_METHOD == 2
        av1_warp_plane_bicubic(
            wms + 2 * nb + ref, xd->bd, pre_buf->buf0, pre_buf->width,
            pre_buf->height, pre_buf->stride,
            &dst_warped[ref * bw * bh + i * bw + j], mi_x + j, mi_y + i, sub_bw,
            sub_bh, bw, pd->subsampling_x, pd->subsampling_y, &conv_params);
#elif AFFINE_FAST_WARP_METHOD == 1 && CONFIG_EXT_WARP_FILTER
        av1_warp_plane_ext(wms + 2 * nb + ref, xd->bd, pre_buf->buf0,
                           pre_buf->width, pre_buf->height, pre_buf->stride,
                           &dst_warped[ref * bw * bh + i * bw + j], mi_x + j,
                           mi_y + i, sub_bw, sub_bh, bw, pd->subsampling_x,
                           pd->subsampling_y, &conv_params);
#else   // AFFINE_FAST_WARP_METHOD == 0
        av1_warp_plane(wms + 2 * nb + ref, xd->bd, pre_buf->buf0,
                       pre_buf->width, pre_buf->height, pre_buf->stride,
                       &dst_warped[ref * bw * bh + i * bw + j], mi_x + j,
                       mi_y + i, sub_bw, sub_bh, bw, pd->subsampling_x,
                       pd->subsampling_y, &conv_params);
#endif  // AFFINE_FAST_WARP_METHOD == 3
        nb++;
      }
    }
#else
#if AFFINE_FAST_WARP_METHOD == 3
    av1_warp_plane_bilinear(&wms[ref], xd->bd, pre_buf->buf0, pre_buf->width,
                            pre_buf->height, pre_buf->stride,
                            &dst_warped[ref * bw * bh], mi_x, mi_y, bw, bh, bw,
                            pd->subsampling_x, pd->subsampling_y, &conv_params);
#elif AFFINE_FAST_WARP_METHOD == 2
    av1_warp_plane_bicubic(&wms[ref], xd->bd, pre_buf->buf0, pre_buf->width,
                           pre_buf->height, pre_buf->stride,
                           &dst_warped[ref * bw * bh], mi_x, mi_y, bw, bh, bw,
                           pd->subsampling_x, pd->subsampling_y, &conv_params);
#elif AFFINE_FAST_WARP_METHOD == 1 && CONFIG_EXT_WARP_FILTER
    av1_warp_plane_ext(&wms[ref], xd->bd, pre_buf->buf0, pre_buf->width,
                       pre_buf->height, pre_buf->stride,
                       &dst_warped[ref * bw * bh], mi_x, mi_y, bw, bh, bw,
                       pd->subsampling_x, pd->subsampling_y, &conv_params);
#else   // AFFINE_FAST_WARP_METHOD == 0
    av1_warp_plane(&wms[ref], xd->bd, pre_buf->buf0, pre_buf->width,
                   pre_buf->height, pre_buf->stride, &dst_warped[ref * bw * bh],
                   mi_x, mi_y, bw, bh, bw, pd->subsampling_x, pd->subsampling_y,
                   &conv_params);
#endif  // AFFINE_FAST_WARP_METHOD == 3
#endif  // CONFIG_AFFINE_REFINEMENT_SB
  }
  av1_copy_pred_array_highbd(&dst_warped[0], &dst_warped[bw * bh], tmp0, tmp1,
                             bw, bh, d0, d1, 0);
  // Buffers gx0 and gy0 are used to store the gradients of tmp0
  av1_compute_subpel_gradients_interp(tmp0, bw, bh, grad_prec_bits, gx0, gy0);
  aom_free(dst_warped);
}
#endif  // CONFIG_AFFINE_REFINEMENT

static AOM_FORCE_INLINE void compute_pred_using_interp_grad_highbd(
    const uint16_t *src1, const uint16_t *src2, int16_t *dst1, int16_t *dst2,
    int bw, int bh, int d0, int d1, int centered) {
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
      // To avoid overflow, we clamp d0*P0-d1*P1 and P0-P1.
      int32_t tmp_dst =
          d0 * (int32_t)src1[i * bw + j] - d1 * (int32_t)src2[i * bw + j];
      if (centered) tmp_dst = ROUND_POWER_OF_TWO_SIGNED(tmp_dst, 1);
      dst1[i * bw + j] = clamp(tmp_dst, INT16_MIN, INT16_MAX);
      if (dst2) {
        tmp_dst = (int32_t)src1[i * bw + j] - (int32_t)src2[i * bw + j];
        dst2[i * bw + j] = clamp(tmp_dst, INT16_MIN, INT16_MAX);
      }
    }
  }
}

void av1_copy_pred_array_highbd_c(const uint16_t *src1, const uint16_t *src2,
                                  int16_t *dst1, int16_t *dst2, int bw, int bh,
                                  int d0, int d1, int centered) {
  compute_pred_using_interp_grad_highbd(src1, src2, dst1, dst2, bw, bh, d0, d1,
                                        centered);
}

void av1_get_optflow_based_mv(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mbmi,
    int_mv *mv_refined, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func, int16_t *gx0, int16_t *gy0,
    int16_t *gx1, int16_t *gy1,
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
) {
#if CONFIG_AFFINE_REFINEMENT
  *use_affine_opfl = 0;
#endif  // CONFIG_AFFINE_REFINEMENT
  const int target_prec = MV_REFINE_PREC_BITS;
  const int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                                       ,
                                       use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
  );
  int n_blocks = (bw / n) * (bh / n);
  // Convert output MV to 1/16th pel
  assert(MV_REFINE_PREC_BITS >= 3);
  const int mv_mult = 1 << (MV_REFINE_PREC_BITS - 3);
  for (int mvi = 0; mvi < n_blocks; mvi++) {
    mv_refined[mvi * 2].as_mv.row =
        clamp(mv_refined[mvi * 2].as_mv.row * mv_mult, INT16_MIN, INT16_MAX);
    mv_refined[mvi * 2].as_mv.col =
        clamp(mv_refined[mvi * 2].as_mv.col * mv_mult, INT16_MIN, INT16_MAX);
    mv_refined[mvi * 2 + 1].as_mv.row = clamp(
        mv_refined[mvi * 2 + 1].as_mv.row * mv_mult, INT16_MIN, INT16_MAX);
    mv_refined[mvi * 2 + 1].as_mv.col = clamp(
        mv_refined[mvi * 2 + 1].as_mv.col * mv_mult, INT16_MIN, INT16_MAX);
  }

  // Obtain d0 and d1
  int d0, d1;
#if CONFIG_OPTFLOW_ON_TIP
  if (mbmi->ref_frame[0] == TIP_FRAME) {
    d0 = cm->tip_ref.ref_offset[0];
    d1 = cm->tip_ref.ref_offset[1];
  } else {
#endif  // CONFIG_OPTFLOW_ON_TIP
    const RefCntBuffer *const r0_buf =
        get_ref_frame_buf(cm, mbmi->ref_frame[0]);
    const RefCntBuffer *const r1_buf =
        get_ref_frame_buf(cm, mbmi->ref_frame[1]);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    d0 = get_relative_dist(&cm->seq_params.order_hint_info,
                           cm->cur_frame->display_order_hint,
                           r0_buf->display_order_hint);
    d1 = get_relative_dist(&cm->seq_params.order_hint_info,
                           cm->cur_frame->display_order_hint,
                           r1_buf->display_order_hint);
#else
  d0 = get_relative_dist(&cm->seq_params.order_hint_info,
                         cm->cur_frame->order_hint, r0_buf->order_hint);
  d1 = get_relative_dist(&cm->seq_params.order_hint_info,
                         cm->cur_frame->order_hint, r1_buf->order_hint);
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
#if CONFIG_OPTFLOW_ON_TIP
  }
#endif  // CONFIG_OPTFLOW_ON_TIP
  if (d0 == 0 || d1 == 0) {
    // Though OPFL is disabled when the distance from either of the reference
    // frames is zero, the MV offset buffers are still used to update the
    // mv_delta buffer. Hence, memset the MV offset buffers vx and vy to zero.
    av1_zero_array(vx0, n_blocks);
    av1_zero_array(vx1, n_blocks);
    av1_zero_array(vy0, n_blocks);
    av1_zero_array(vy1, n_blocks);
    return;
  }

  reduce_temporal_dist(&d0, &d1);

#if CONFIG_OPTFLOW_ON_TIP
  if (do_pred) {
#endif  // CONFIG_OPTFLOW_ON_TIP
    // Obrain P0 and P1
    InterPredParams params0, params1;
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params0, calc_subpel_params_func, 0,
                                   dst0
#if CONFIG_REFINEMV
                                   ,
                                   &best_mv_ref[0], pu_width, pu_height
#endif  // CONFIG_REFINEMV
    );
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params1, calc_subpel_params_func, 1,
                                   dst1
#if CONFIG_REFINEMV
                                   ,
                                   &best_mv_ref[1], pu_width, pu_height
#endif  // CONFIG_REFINEMV
    );
#if CONFIG_OPTFLOW_ON_TIP
  }
#endif  // CONFIG_OPTFLOW_ON_TIP

  int grad_prec_bits;

  // Compute gradients of P0 and P1 with interpolation
  (void)gx1;
  (void)gy1;

  // Compute tmp1 = P0 - P1 and gradients of tmp0 = d0 * P0 - d1 * P1
#if CONFIG_OPTFLOW_ON_TIP
  const int tmp_w = (mbmi->ref_frame[0] == TIP_FRAME) ? bw : MAX_SB_SIZE;
  const int tmp_h = (mbmi->ref_frame[0] == TIP_FRAME) ? bh : MAX_SB_SIZE;
  int16_t *tmp0 = (int16_t *)aom_memalign(16, tmp_w * tmp_h * sizeof(int16_t));
  int16_t *tmp1 = (int16_t *)aom_memalign(16, tmp_w * tmp_h * sizeof(int16_t));
#else
  int16_t *tmp0 =
      (int16_t *)aom_memalign(16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(int16_t));
  int16_t *tmp1 =
      (int16_t *)aom_memalign(16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(int16_t));
#endif  // CONFIG_OPTFLOW_ON_TIP
  av1_copy_pred_array_highbd(dst0, dst1, tmp0, tmp1, bw, bh, d0, d1, 0);
  // Buffers gx0 and gy0 are used to store the gradients of tmp0
  av1_compute_subpel_gradients_interp(tmp0, bw, bh, &grad_prec_bits, gx0, gy0);

#if CONFIG_AFFINE_REFINEMENT
#if AFFINE_OPFL_BASED_ON_SAD
  const unsigned int sad_thr = 1;
  if (mbmi->comp_refine_type >= COMP_AFFINE_REFINE_START && wms) {
    unsigned int sad_pred = sad_generic(dst0, bw, dst1, bw, bw, bh);
    if (sad_pred >= sad_thr * bw * bh) *use_affine_opfl = 1;
  }
#endif

  if (mbmi->comp_refine_type >= COMP_AFFINE_REFINE_START && wms &&
      *use_affine_opfl) {
    av1_opfl_affine_refinement_mxn(tmp1, bw, gx0, gy0, bw, bw, bh, d0, d1, mi_x,
                                   mi_y,
#if CONFIG_REFINEMV
                                   best_mv_ref,
#endif  // CONFIG_REFINEMV
                                   grad_prec_bits, wms);

    update_pred_grad_with_affine_model(xd, plane, bw, bh, wms, mi_x, mi_y, tmp0,
                                       tmp1, gx0, gy0, d0, d1, &grad_prec_bits);

    // Subblock wise translational refinement
    if (damr_refine_subblock(plane, bw, bh, mbmi->comp_refine_type, n)) {
      // Find translational parameters per subblock.
      n_blocks = av1_opfl_mv_refinement_nxn(tmp1, bw, gx0, gy0, bw, bw, bh, n,
                                            d0, d1, grad_prec_bits, target_prec,
                                            vx0, vy0, vx1, vy1);
    }
  } else {
    n_blocks = av1_opfl_mv_refinement_nxn(tmp1, bw, gx0, gy0, bw, bw, bh, n, d0,
                                          d1, grad_prec_bits, target_prec, vx0,
                                          vy0, vx1, vy1);
  }
#else
  n_blocks = av1_opfl_mv_refinement_nxn(tmp1, bw, gx0, gy0, bw, bw, bh, n, d0,
                                        d1, grad_prec_bits, target_prec, vx0,
                                        vy0, vx1, vy1);
#endif  // CONFIG_AFFINE_REFINEMENT

  aom_free(tmp0);
  aom_free(tmp1);

  for (int i = 0; i < n_blocks; i++) {
#if OPFL_CLAMP_MV_DELTA
    vy0[i] = clamp(vy0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vx0[i] = clamp(vx0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vy1[i] = clamp(vy1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vx1[i] = clamp(vx1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
#endif
    mv_refined[i * 2].as_mv.row =
        clamp(mv_refined[i * 2].as_mv.row + vy0[i], INT16_MIN, INT16_MAX);
    mv_refined[i * 2].as_mv.col =
        clamp(mv_refined[i * 2].as_mv.col + vx0[i], INT16_MIN, INT16_MAX);
    mv_refined[i * 2 + 1].as_mv.row =
        clamp(mv_refined[i * 2 + 1].as_mv.row + vy1[i], INT16_MIN, INT16_MAX);
    mv_refined[i * 2 + 1].as_mv.col =
        clamp(mv_refined[i * 2 + 1].as_mv.col + vx1[i], INT16_MIN, INT16_MAX);
  }
}
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_D071_IMP_MSK_BLD
int is_out_of_frame_block(const InterPredParams *inter_pred_params,
                          int frame_width, int frame_height, int sub_block_id) {
  for (int ref = 0; ref < 2; ref++) {
    const BacpBlockData *const b_data =
        &inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + ref];
    if (b_data->x0 < 0 || b_data->x0 > frame_width - 1 || b_data->x1 < 0 ||
        b_data->x1 > frame_width

        || b_data->y0 < 0 || b_data->y0 > frame_height - 1 || b_data->y1 < 0 ||
        b_data->y1 > frame_height) {
      return 1;
    }
  }
  return 0;
}
#endif  // CONFIG_D071_IMP_MSK_BLD

// Equation of line: f(x, y) = a[0]*(x - a[2]*w/8) + a[1]*(y - a[3]*h/8) = 0
void av1_init_wedge_masks() {
  init_wedge_master_masks();
  init_wedge_masks();
  init_smooth_interintra_masks();
}

static AOM_INLINE void build_masked_compound_no_round(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w, InterPredParams *inter_pred_params) {
#if CONFIG_D071_IMP_MSK_BLD
  const int ssy = (inter_pred_params->conv_params.plane &&
                   comp_data->type == COMPOUND_AVERAGE)
                      ? 0
                      : inter_pred_params->subsampling_y;
  const int ssx = (inter_pred_params->conv_params.plane &&
                   comp_data->type == COMPOUND_AVERAGE)
                      ? 0
                      : inter_pred_params->subsampling_x;
#else
  const int ssy = inter_pred_params->subsampling_y;
  const int ssx = inter_pred_params->subsampling_x;
#endif  // CONFIG_D071_IMP_MSK_BLD
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  const int mask_stride = block_size_wide[sb_type];
  aom_highbd_blend_a64_d16_mask(dst, dst_stride, src0, src0_stride, src1,
                                src1_stride, mask, mask_stride, w, h, ssx, ssy,
                                &inter_pred_params->conv_params,
                                inter_pred_params->bit_depth);
}
#if !CONFIG_D071_IMP_MSK_BLD
static
#endif
    void
    make_masked_inter_predictor(const uint16_t *pre, int pre_stride,
                                uint16_t *dst, int dst_stride,
                                InterPredParams *inter_pred_params,
                                const SubpelParams *subpel_params
#if CONFIG_D071_IMP_MSK_BLD
                                ,
                                int use_bacp, int sub_block_id
#endif  // CONFIG_D071_IMP_MSK_BLD
    ) {
  const INTERINTER_COMPOUND_DATA *comp_data = &inter_pred_params->mask_comp;
  BLOCK_SIZE sb_type = inter_pred_params->sb_type;

  // We're going to call av1_make_inter_predictor to generate a prediction into
  // a temporary buffer, then will blend that temporary buffer with that from
  // the other reference.
  DECLARE_ALIGNED(32, uint16_t, tmp_buf[MAX_SB_SQUARE]);

  const int tmp_buf_stride = MAX_SB_SIZE;
  CONV_BUF_TYPE *org_dst = inter_pred_params->conv_params.dst;
  int org_dst_stride = inter_pred_params->conv_params.dst_stride;
  CONV_BUF_TYPE *tmp_buf16 = (CONV_BUF_TYPE *)tmp_buf;
  inter_pred_params->conv_params.dst = tmp_buf16;
  inter_pred_params->conv_params.dst_stride = tmp_buf_stride;
  assert(inter_pred_params->conv_params.do_average == 0);

  // This will generate a prediction in tmp_buf for the second reference
  av1_make_inter_predictor(pre, pre_stride, tmp_buf, MAX_SB_SIZE,
                           inter_pred_params, subpel_params);

  if (!inter_pred_params->conv_params.plane &&
      comp_data->type == COMPOUND_DIFFWTD) {
    av1_build_compound_diffwtd_mask_d16(
        comp_data->seg_mask, comp_data->mask_type, org_dst, org_dst_stride,
        tmp_buf16, tmp_buf_stride, inter_pred_params->block_height,
        inter_pred_params->block_width, &inter_pred_params->conv_params,
        inter_pred_params->bit_depth);
  }

#if CONFIG_D071_IMP_MSK_BLD
  // Mask is generated from luma and reuse for chroma
  const int generate_mask_for_this_plane =
      (!inter_pred_params->conv_params.plane ||
       comp_data->type == COMPOUND_AVERAGE);
  if (use_bacp && generate_mask_for_this_plane) {
    uint8_t *mask = comp_data->seg_mask;
    int mask_stride = block_size_wide[sb_type];
    BacpBlockData *b_data_0 =
        &inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + 0];
    BacpBlockData *b_data_1 =
        &inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + 1];

    for (int i = 0; i < inter_pred_params->block_height; ++i) {
      for (int j = 0; j < inter_pred_params->block_width; ++j) {
        int x = b_data_0->x0 + j;
        int y = b_data_0->y0 + i;

        int p0_available =
            (x >= 0 && x < inter_pred_params->ref_frame_buf.width && y >= 0 &&
             y < inter_pred_params->ref_frame_buf.height);

        x = b_data_1->x0 + j;
        y = b_data_1->y0 + i;
        int p1_available =
            (x >= 0 && x < inter_pred_params->ref_frame_buf.width && y >= 0 &&
             y < inter_pred_params->ref_frame_buf.height);

        if (p0_available && !p1_available) {
          mask[j] = AOM_BLEND_A64_MAX_ALPHA - DEFAULT_IMP_MSK_WT;
        } else if (!p0_available && p1_available) {
          mask[j] = DEFAULT_IMP_MSK_WT;
        } else if (comp_data->type == COMPOUND_AVERAGE) {
          mask[j] = AOM_BLEND_A64_MAX_ALPHA >> 1;
        }
      }
      mask += mask_stride;
    }
  }
#endif  // CONFIG_D071_IMP_MSK_BLD

  build_masked_compound_no_round(
      dst, dst_stride, org_dst, org_dst_stride, tmp_buf16, tmp_buf_stride,
      comp_data, sb_type, inter_pred_params->block_height,
      inter_pred_params->block_width, inter_pred_params);

#if CONFIG_D071_IMP_MSK_BLD
  // restore to previous state
  inter_pred_params->conv_params.dst = org_dst;
  inter_pred_params->conv_params.dst_stride = org_dst_stride;
#endif  // CONFIG_D071_IMP_MSK_BLD
}

#if CONFIG_OPTFLOW_REFINEMENT
// Makes the interpredictor for the region by dividing it up into nxn blocks
// and running the interpredictor code on each one.
void make_inter_pred_of_nxn(
    uint16_t *dst, int dst_stride, int_mv *const mv_refined,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
#if CONFIG_AFFINE_REFINEMENT
    const AV1_COMMON *cm, int pu_width, int plane,
    CompoundRefineType comp_refine_type, WarpedMotionParams *wms, int_mv *mv,
    const int use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func,
    int n, SubpelParams *subpel_params) {
  int n_blocks = 0;
  int bw = inter_pred_params->orig_block_width;
  int bh = inter_pred_params->orig_block_height;
  int sub_bw = n;
  int sub_bh = n;
#if CONFIG_AFFINE_REFINEMENT
  MV ref_mv, cur_mv;
  ref_mv.row = mv_refined[ref].as_mv.row;
  ref_mv.col = mv_refined[ref].as_mv.col;
  if (comp_refine_type >= COMP_AFFINE_REFINE_START &&
      !damr_refine_subblock(plane, bw, bh, comp_refine_type, n)) {
    sub_bw = bw;
    sub_bh = bh;
  }
  const int unit_offset = 1 << WARPEDMODEL_PREC_BITS;
#if AFFINE_CHROMA_REFINE_METHOD >= 2
  if (wms && comp_refine_type >= COMP_AFFINE_REFINE_START && plane) {
    WarpedMotionParams ref_wm = wms ? wms[ref] : default_warp_params;
    // Apply offsets based on the affine parameters. bw, bh, and wm are
    // for luma plane, so compute the warp MV in luma and then scale it
    // for chroma
    const int32_t blk_offset_x_hp =
        ref_wm.wmmat[0] - mv->as_mv.col * (1 << (WARPEDMODEL_PREC_BITS - 3)) +
        mi_x * (ref_wm.wmmat[2] - unit_offset) + mi_y * ref_wm.wmmat[3];
    const int32_t blk_offset_y_hp =
        ref_wm.wmmat[1] - mv->as_mv.row * (1 << (WARPEDMODEL_PREC_BITS - 3)) +
        mi_x * ref_wm.wmmat[4] + mi_y * (ref_wm.wmmat[5] - unit_offset);
    ref_mv.col += ROUND_POWER_OF_TWO_SIGNED(
        blk_offset_x_hp, WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS);
    ref_mv.row += ROUND_POWER_OF_TWO_SIGNED(
        blk_offset_y_hp, WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS);
  }
#else
  (void)mv;
#endif
#endif  // CONFIG_AFFINE_REFINEMENT
  assert(bw % sub_bw == 0);
  assert(bh % sub_bh == 0);
  CONV_BUF_TYPE *orig_conv_dst = inter_pred_params->conv_params.dst;
  inter_pred_params->block_width = sub_bw;
  inter_pred_params->block_height = sub_bh;

  MV *subblock_mv;
  uint16_t *pre;
  int src_stride = 0;
#if CONFIG_AFFINE_REFINEMENT_SB
  int sb_idx = 0;
  int affine_sub_bw =
      AOMMIN(AFFINE_MAX_UNIT >> inter_pred_params->subsampling_x, bw);
  int affine_sub_bh =
      AOMMIN(AFFINE_MAX_UNIT >> inter_pred_params->subsampling_y, bh);
  int wms_stride = bw / affine_sub_bw;
#endif  // CONFIG_AFFINE_REFINEMENT_SB

  // Process whole nxn blocks.
  for (int j = 0; j < bh; j += sub_bh) {
    for (int i = 0; i < bw; i += sub_bw) {
#if CONFIG_AFFINE_REFINEMENT_SB
      // Identify warped parameter to used for this nxn subblock
      sb_idx = (j / affine_sub_bh) * wms_stride + (i / affine_sub_bw);
      WarpedMotionParams *wms_sb = wms ? (wms + 2 * sb_idx) : NULL;
#else
      WarpedMotionParams *wms_sb = wms;
#endif  // CONFIG_AFFINE_REFINEMENT_SB
#if CONFIG_AFFINE_REFINEMENT
      int delta_idx = (j / n) * (pu_width / n) + (i / n);
      if (wms_sb && comp_refine_type >= COMP_AFFINE_REFINE_START &&
          use_affine_opfl) {
        // If warped model is not valid, wmmat[0] and wmmat[1] remain the
        // translational offset parameters in block-relative coordinates. Here
        // they are applied as MV offsets for simple translational prediction
        WarpedMotionParams this_wm = wms_sb[ref];
        if (this_wm.invalid
#if !CONFIG_EXT_WARP_FILTER
            || sub_bh < 8 || sub_bw < 8
#endif  // !CONFIG_EXT_WARP_FILTER
#if AFFINE_CHROMA_REFINE_METHOD >= 2
            || plane
#endif  // AFFINE_CHROMA_REFINE_METHOD >= 2
        ) {
          // When warp prediction is not allowed, apply translational prediction
          // based on warp parameters
          inter_pred_params->mode = TRANSLATION_PRED;
          cur_mv = ref_mv;
          WarpedMotionParams ref_wm =
              wms_sb ? wms_sb[ref] : default_warp_params;
          // Apply offsets based on current subblock center position
          const int subblk_center_x = (i + sub_bw / 2 - 1)
                                      << inter_pred_params->subsampling_x;
          const int subblk_center_y = (j + sub_bh / 2 - 1)
                                      << inter_pred_params->subsampling_y;
          const int32_t subblk_offset_x_hp =
              subblk_center_x * (ref_wm.wmmat[2] - unit_offset) +
              subblk_center_y * ref_wm.wmmat[3];
          const int32_t subblk_offset_y_hp =
              subblk_center_x * ref_wm.wmmat[4] +
              subblk_center_y * (ref_wm.wmmat[5] - unit_offset);
          cur_mv.col += ROUND_POWER_OF_TWO_SIGNED(
              subblk_offset_x_hp, WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS);
          cur_mv.row += ROUND_POWER_OF_TWO_SIGNED(
              subblk_offset_y_hp, WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS);
#if AFFINE_CHROMA_REFINE_METHOD == 3
          if (comp_refine_type == COMP_REFINE_ROTZOOM4P_SUBBLK2P
#if !CONFIG_EXT_WARP_FILTER
              && n > 4
#endif  // !CONFIG_EXT_WARP_FILTER
          ) {
            // If this is a 4x4 colocated chroma block of a 8x8 luma block,
            // colocated subblocks will be 2x2. In this case we take the average
            // of 4 refined MVs and use it to refine prediction at 4x4 level.
            if (bw == 4 && bh == 4 && n == 4) {
              cur_mv.col += ROUND_POWER_OF_TWO_SIGNED(
                  xd->mv_delta[0].mv[ref].as_mv.col +
                      xd->mv_delta[1].mv[ref].as_mv.col +
                      xd->mv_delta[2].mv[ref].as_mv.col +
                      xd->mv_delta[3].mv[ref].as_mv.col,
                  2);
              cur_mv.row += ROUND_POWER_OF_TWO_SIGNED(
                  xd->mv_delta[0].mv[ref].as_mv.row +
                      xd->mv_delta[1].mv[ref].as_mv.row +
                      xd->mv_delta[2].mv[ref].as_mv.row +
                      xd->mv_delta[3].mv[ref].as_mv.row,
                  2);
            } else {
              cur_mv.col += xd->mv_delta[delta_idx].mv[ref].as_mv.col;
              cur_mv.row += xd->mv_delta[delta_idx].mv[ref].as_mv.row;
            }
          }
#endif  // AFFINE_CHROMA_REFINE_METHOD == 3
          subblock_mv = &cur_mv;
          subblock_mv->col = clamp(subblock_mv->col, MV_LOW + 1, MV_UPP - 1);
          subblock_mv->row = clamp(subblock_mv->row, MV_LOW + 1, MV_UPP - 1);
        } else {
          // Overwrite inter_pred_params to trigger warped prediction in
          // av1_make_inter_predictor()
          inter_pred_params->mode = WARP_PRED;
          inter_pred_params->warp_params = this_wm;
          if (comp_refine_type == COMP_REFINE_ROTZOOM4P_SUBBLK2P
#if !CONFIG_EXT_WARP_FILTER
              && n > 4
#endif  // !CONFIG_EXT_WARP_FILTER
          ) {
            // If this is a 4x4 colocated chroma block of a 8x8 luma block,
            // colocated subblocks will be 2x2. In this case we take the average
            // of 4 refined MVs and use it to refine prediction at 4x4 level.
            if (bw == 4 && bh == 4 && n == 4) {
              inter_pred_params->warp_params.wmmat[0] +=
                  (xd->mv_delta[0].mv[ref].as_mv.col +
                   xd->mv_delta[1].mv[ref].as_mv.col +
                   xd->mv_delta[2].mv[ref].as_mv.col +
                   xd->mv_delta[3].mv[ref].as_mv.col) *
                  (1 << (WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS - 2));
              inter_pred_params->warp_params.wmmat[1] +=
                  (xd->mv_delta[0].mv[ref].as_mv.row +
                   xd->mv_delta[1].mv[ref].as_mv.row +
                   xd->mv_delta[2].mv[ref].as_mv.row +
                   xd->mv_delta[3].mv[ref].as_mv.row) *
                  (1 << (WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS - 2));
            } else {
              inter_pred_params->warp_params.wmmat[0] +=
                  xd->mv_delta[delta_idx].mv[ref].as_mv.col *
                  (1 << (WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS));
              inter_pred_params->warp_params.wmmat[1] +=
                  xd->mv_delta[delta_idx].mv[ref].as_mv.row *
                  (1 << (WARPEDMODEL_PREC_BITS - MV_REFINE_PREC_BITS));
            }
#if CONFIG_EXTENDED_WARP_PREDICTION
            inter_pred_params->warp_params.wmmat[0] =
                clamp(inter_pred_params->warp_params.wmmat[0],
                      -WARPEDMODEL_TRANS_CLAMP,
                      WARPEDMODEL_TRANS_CLAMP - unit_offset);
            inter_pred_params->warp_params.wmmat[1] =
                clamp(inter_pred_params->warp_params.wmmat[1],
                      -WARPEDMODEL_TRANS_CLAMP,
                      WARPEDMODEL_TRANS_CLAMP - unit_offset);
#else
            inter_pred_params->warp_params.wmmat[0] =
                clamp(inter_pred_params->warp_params.wmmat[0],
                      -WARPEDMODEL_TRANS_CLAMP, WARPEDMODEL_TRANS_CLAMP - 1);
            inter_pred_params->warp_params.wmmat[1] =
                clamp(inter_pred_params->warp_params.wmmat[1],
                      -WARPEDMODEL_TRANS_CLAMP, WARPEDMODEL_TRANS_CLAMP - 1);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
          }
          subblock_mv = &mv_refined[ref].as_mv;
        }
      } else {
        subblock_mv = &(mv_refined[n_blocks * 2 + ref].as_mv);
      }

      const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
      const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
      inter_pred_params->dist_to_top_edge = -GET_MV_SUBPEL(mi_y + j);
      inter_pred_params->dist_to_bottom_edge =
          GET_MV_SUBPEL(height - bh - mi_y - j);
      inter_pred_params->dist_to_left_edge = -GET_MV_SUBPEL(mi_x + i);
      inter_pred_params->dist_to_right_edge =
          GET_MV_SUBPEL(width - bw - mi_x - i);
#else
      subblock_mv = &(mv_refined[n_blocks * 2 + ref].as_mv);
#endif  // CONFIG_AFFINE_REFINEMENT

      calc_subpel_params_func(subblock_mv, inter_pred_params, xd, mi_x + i,
                              mi_y + j, ref, 1, mc_buf, &pre, subpel_params,
                              &src_stride);

#if CONFIG_D071_IMP_MSK_BLD
      int use_bacp = 0;
      assert(inter_pred_params->mask_comp.type == COMPOUND_AVERAGE);
      assert(inter_pred_params->comp_mode == UNIFORM_COMP);
      int stored_do_average = inter_pred_params->conv_params.do_average;
      InterCompMode stored_comp_mode = inter_pred_params->comp_mode;
      uint8_t *stored_seg_mask = inter_pred_params->mask_comp.seg_mask;

      if (inter_pred_params->border_data.enable_bacp) {
        inter_pred_params->border_data.bacp_block_data[n_blocks * 2 + ref].x0 =
            subpel_params->x0;
        inter_pred_params->border_data.bacp_block_data[n_blocks * 2 + ref].x1 =
            subpel_params->x1;
        inter_pred_params->border_data.bacp_block_data[n_blocks * 2 + ref].y0 =
            subpel_params->y0;
        inter_pred_params->border_data.bacp_block_data[n_blocks * 2 + ref].y1 =
            subpel_params->y1;
        if (ref == 1) {
          use_bacp = is_out_of_frame_block(
              inter_pred_params, inter_pred_params->ref_frame_buf.width,
              inter_pred_params->ref_frame_buf.height, n_blocks);

          if (use_bacp &&
              inter_pred_params->mask_comp.type == COMPOUND_AVERAGE) {
            inter_pred_params->conv_params.do_average = 0;
            inter_pred_params->comp_mode = MASK_COMP;
            inter_pred_params->mask_comp.seg_mask = xd->seg_mask;
          }
        }
      }

      assert(IMPLIES(ref == 0, !use_bacp));
      if (use_bacp) {
        assert(inter_pred_params->comp_mode == MASK_COMP);
        make_masked_inter_predictor(pre, src_stride, dst, dst_stride,
                                    inter_pred_params, subpel_params, use_bacp,
                                    n_blocks);

      } else {
#endif

        av1_make_inter_predictor(pre, src_stride, dst, dst_stride,
                                 inter_pred_params, subpel_params);
#if CONFIG_D071_IMP_MSK_BLD
      }

      // Restored to original inter_pred_params
      if (use_bacp && inter_pred_params->mask_comp.type == COMPOUND_AVERAGE) {
        inter_pred_params->conv_params.do_average = stored_do_average;
        inter_pred_params->comp_mode = stored_comp_mode;
        inter_pred_params->mask_comp.seg_mask = stored_seg_mask;
      }
#endif  // CONFIG_D071_IMP_MSK_BLD
      n_blocks++;
      dst += sub_bw;
      inter_pred_params->conv_params.dst += sub_bw;
      inter_pred_params->pix_col += sub_bw;
    }
    dst -= bw;
    inter_pred_params->conv_params.dst -= bw;
    inter_pred_params->pix_col -= bw;

    dst += sub_bh * dst_stride;
    inter_pred_params->conv_params.dst +=
        sub_bh * inter_pred_params->conv_params.dst_stride;
    inter_pred_params->pix_row += sub_bh;
  }

  inter_pred_params->conv_params.dst = orig_conv_dst;
}

// Use a second pass of motion compensation to rebuild inter predictor
void av1_opfl_rebuild_inter_predictor(
    uint16_t *dst, int dst_stride, int plane, int_mv *const mv_refined,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
#if CONFIG_AFFINE_REFINEMENT
    const AV1_COMMON *cm, int pu_width, CompoundRefineType comp_refine_type,
    WarpedMotionParams *wms, int_mv *mv, const int use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
    ,
    int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
) {
  SubpelParams subpel_params;
  int w = inter_pred_params->block_width;
  int h = inter_pred_params->block_height;
  int n = opfl_get_subblock_size(w, h, plane
#if CONFIG_OPTFLOW_ON_TIP
                                 ,
                                 use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
  );
  make_inter_pred_of_nxn(
      dst, dst_stride, mv_refined, inter_pred_params, xd, mi_x, mi_y,
#if CONFIG_AFFINE_REFINEMENT
      cm, pu_width, plane, comp_refine_type, wms, mv, use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
      ref, mc_buf, calc_subpel_params_func, n, &subpel_params);
}
#endif  // CONFIG_OPTFLOW_REFINEMENT

void av1_build_one_inter_predictor(
    uint16_t *dst, int dst_stride, const MV *const src_mv,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func) {
  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;
  calc_subpel_params_func(src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
                          0, /* use_optflow_refinement */
#endif                       // CONFIG_OPTFLOW_REFINEMENT
                          mc_buf, &src, &subpel_params, &src_stride);

#if CONFIG_D071_IMP_MSK_BLD
  int use_bacp = 0;
  int sub_block_id = 0;
  if (inter_pred_params->border_data.enable_bacp) {
    inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + ref].x0 =
        subpel_params.x0;
    inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + ref].x1 =
        subpel_params.x1;
    inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + ref].y0 =
        subpel_params.y0;
    inter_pred_params->border_data.bacp_block_data[2 * sub_block_id + ref].y1 =
        subpel_params.y1;
    if (ref == 1) {
      use_bacp = is_out_of_frame_block(
          inter_pred_params, inter_pred_params->ref_frame_buf.width,
          inter_pred_params->ref_frame_buf.height, sub_block_id);
      if (use_bacp && inter_pred_params->mask_comp.type == COMPOUND_AVERAGE) {
        inter_pred_params->conv_params.do_average = 0;
        inter_pred_params->comp_mode = MASK_COMP;
        inter_pred_params->mask_comp.seg_mask = xd->seg_mask;
      }
    }
  }

  assert(IMPLIES(ref == 0, !use_bacp));
#endif  // CONFIG_D071_IMP_MSK_BLD

  if (inter_pred_params->comp_mode == UNIFORM_SINGLE ||
      inter_pred_params->comp_mode == UNIFORM_COMP) {
    av1_make_inter_predictor(src, src_stride, dst, dst_stride,
                             inter_pred_params, &subpel_params);
#if CONFIG_D071_IMP_MSK_BLD
    assert(IMPLIES(use_bacp, ref == 0));
    assert(use_bacp == 0);
#endif  // CONFIG_D071_IMP_MSK_BLD
  } else {
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params
#if CONFIG_D071_IMP_MSK_BLD
                                ,
                                use_bacp, 0
#endif  // CONFIG_D071_IMP_MSK_BLD
    );
#if CONFIG_D071_IMP_MSK_BLD
    assert(IMPLIES(inter_pred_params->border_data.enable_bacp, ref == 1));
#endif  // CONFIG_D071_IMP_MSK_BLD
  }
}

#if CONFIG_EXPLICIT_BAWP
// Derive the offset value of block adaptive weighted prediction
// mode. One row from the top boundary and one column from the left boundary
// are used in the less square error process.
static void derive_explicit_bawp_offsets(MACROBLOCKD *xd, uint16_t *recon_top,
                                         uint16_t *recon_left, int rec_stride,
                                         uint16_t *ref_top, uint16_t *ref_left,
                                         int ref_stride, int ref, int plane,
                                         int bw, int bh) {
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_BAWP_CHROMA
  assert(mbmi->bawp_flag[0] > 1);
#else
  assert(mbmi->bawp_flag > 1);
#endif  // CONFIG_BAWP_CHROMA
  // only integer position of reference, may need to consider
  // fractional position of ref samples
  int count = 0;
  int sum_x = 0, sum_y = 0;

  if (xd->up_available) {
    for (int i = 0; i < bw; ++i) {
      sum_x += ref_top[i];
      sum_y += recon_top[i];
    }
    count += bw;
  }

  if (xd->left_available) {
    for (int i = 0; i < bh; ++i) {
      sum_x += ref_left[0];
      sum_y += recon_left[0];

      recon_left += rec_stride;
      ref_left += ref_stride;
    }
    count += bh;
  }

  const int16_t shift = 8;  // maybe a smaller value can be used
  if (count > 0) {
    const int beta = derive_linear_parameters_beta(
        sum_x, sum_y, count, shift, mbmi->bawp_alpha[plane][ref]);
    mbmi->bawp_beta[plane][ref] = beta;
  } else {
    mbmi->bawp_beta[plane][ref] = -(1 << shift);
  }
}
#endif  // CONFIG_EXPLICIT_BAWP

#if CONFIG_BAWP
#if CONFIG_BAWP_ACROSS_SCALES_FIX
// The below functions are used for scaling X, Y position
// for BAWP with across scale prediction
// In future, more generalized implementations for all inter-coding tools
// are required for supporting across scale prediction
static INLINE int scaled_x_gen(int val, const struct scale_factors *sf) {
  const int64_t tval = (int64_t)val * sf->x_scale_fp;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval, REF_SCALE_SHIFT);
}

static INLINE int scaled_y_gen(int val, const struct scale_factors *sf) {
  const int64_t tval = (int64_t)val * sf->y_scale_fp;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval, REF_SCALE_SHIFT);
}
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
// Derive the scaling factor and offset of block adaptive weighted prediction
// mode. One row from the top boundary and one column from the left boundary
// are used in the less square error process.
static void derive_bawp_parameters(MACROBLOCKD *xd, uint16_t *recon_top,
                                   uint16_t *recon_left, int rec_stride,
                                   uint16_t *ref_top, uint16_t *ref_left,
                                   int ref_stride, int ref, int plane, int bw,
#if CONFIG_BAWP_ACROSS_SCALES_FIX
                                   int bh, const struct scale_factors *sf) {
#else   // CONFIG_BAWP_ACROSS_SCALES_FIX
                                   int bh) {
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_BAWP_CHROMA
  assert(mbmi->bawp_flag[0] >= 1);
#else
  assert(mbmi->bawp_flag == 1);
#endif  // CONFIG_BAWP_CHROMA
  // only integer position of reference, may need to consider
  // fractional position of ref samples
  int count = 0;
  int sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;

  if (xd->up_available) {
#if CONFIG_BAWP_ACROSS_SCALES_FIX
    if (sf->x_scale_fp != REF_NO_SCALE) {
      for (int i = 0; i < bw; i++) {
        int idx = scaled_x_gen(i, sf);
        sum_x += ref_top[idx];
        sum_y += recon_top[i];
        sum_xy += ref_top[idx] * recon_top[i];
        sum_xx += ref_top[idx] * ref_top[idx];
      }
    } else {
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
      for (int i = 0; i < bw; ++i) {
        sum_x += ref_top[i];
        sum_y += recon_top[i];
        sum_xy += ref_top[i] * recon_top[i];
        sum_xx += ref_top[i] * ref_top[i];
      }
#if CONFIG_BAWP_ACROSS_SCALES_FIX
    }
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
    count += bw;
  }

  if (xd->left_available) {
#if CONFIG_BAWP_ACROSS_SCALES_FIX
    if (sf->y_scale_fp != REF_NO_SCALE) {
      for (int i = 0; i < bh; i++) {
        int ref_left_tmp_idx = scaled_y_gen(i, sf) * ref_stride;
        sum_x += ref_left[ref_left_tmp_idx];
        sum_y += recon_left[0];
        sum_xy += ref_left[ref_left_tmp_idx] * recon_left[0];
        sum_xx += ref_left[ref_left_tmp_idx] * ref_left[ref_left_tmp_idx];
        recon_left += rec_stride;
      }
    } else {
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
      for (int i = 0; i < bh; ++i) {
        sum_x += ref_left[0];
        sum_y += recon_left[0];
        sum_xy += ref_left[0] * recon_left[0];
        sum_xx += ref_left[0] * ref_left[0];

        recon_left += rec_stride;
        ref_left += ref_stride;
      }
#if CONFIG_BAWP_ACROSS_SCALES_FIX
    }
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
    count += bh;
  }

  const int16_t shift = 8;  // maybe a smaller value can be used
  if (count > 0) {
#if CONFIG_BAWP_CHROMA
    if (plane == 0) {
      const int16_t alpha = derive_linear_parameters_alpha(
          sum_x, sum_y, sum_xx, sum_xy, count, shift, 1);
      mbmi->bawp_alpha[plane][ref] = (alpha == 0) ? (1 << shift) : alpha;
    } else {
      mbmi->bawp_alpha[plane][ref] = mbmi->bawp_alpha[0][ref];
    }
#else
    const int16_t alpha = derive_linear_parameters_alpha(
        sum_x, sum_y, sum_xx, sum_xy, count, shift, 1);
    mbmi->bawp_alpha[plane][ref] = (alpha == 0) ? (1 << shift) : alpha;
#endif  // CONFIG_BAWP_CHROMA
    const int beta = derive_linear_parameters_beta(
        sum_x, sum_y, count, shift, mbmi->bawp_alpha[plane][ref]);
    mbmi->bawp_beta[plane][ref] = beta;
  } else {
    mbmi->bawp_alpha[plane][ref] = 1 << shift;
    mbmi->bawp_beta[plane][ref] = -(1 << shift);
  }
}

// generate inter prediction of a block coded in bwap mode enabled
void av1_build_one_bawp_inter_predictor(
    uint16_t *dst, int dst_stride, const MV *const src_mv,
    InterPredParams *inter_pred_params, const AV1_COMMON *cm, MACROBLOCKD *xd,
    const BUFFER_SET *dst_orig, int bw, int bh, int mi_x, int mi_y, int ref,
    int plane, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func) {
  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;
  calc_subpel_params_func(src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
                          0, /* use_optflow_refinement */
#endif                       // CONFIG_OPTFLOW_REFINEMENT
                          mc_buf, &src, &subpel_params, &src_stride);

  assert(inter_pred_params->comp_mode == UNIFORM_SINGLE);
  if (inter_pred_params->comp_mode == UNIFORM_SINGLE ||
      inter_pred_params->comp_mode == UNIFORM_COMP) {
    av1_make_inter_predictor(src, src_stride, dst, dst_stride,
                             inter_pred_params, &subpel_params);
  } else {
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params
#if CONFIG_D071_IMP_MSK_BLD
                                ,
                                0, 0
#endif  // CONFIG_D071_IMP_MSK_BLD
    );
  }

  const int shift = 8;
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_BAWP_ACROSS_SCALES_FIX
  const struct scale_factors *sf = inter_pred_params->scale_factors;

  const int x_off = scaled_x_gen(mbmi->mv[ref].as_mv.col, sf) >> 3;
  const int y_off = scaled_y_gen(mbmi->mv[ref].as_mv.row, sf) >> 3;

  const int x_off_p = x_off >> inter_pred_params->subsampling_x;
  const int y_off_p = y_off >> inter_pred_params->subsampling_y;

  const int mi_x_p = scaled_x_gen(mi_x, sf) >> inter_pred_params->subsampling_x;
  const int mi_y_p = scaled_y_gen(mi_y, sf) >> inter_pred_params->subsampling_y;

  const int width_p =
      (sf->x_scale_fp != REF_NO_SCALE)
          ? pd->pre[ref].width >> inter_pred_params->subsampling_x
          : cm->width >> inter_pred_params->subsampling_x;
  const int height_p =
      (sf->y_scale_fp != REF_NO_SCALE)
          ? pd->pre[ref].height >> inter_pred_params->subsampling_y
          : cm->height >> inter_pred_params->subsampling_y;

  int ref_w = scaled_x_gen(bw, sf);
  if ((mi_x_p + ref_w) >= width_p) ref_w = width_p - mi_x_p;

  int ref_h = scaled_y_gen(bh, sf);
  if ((mi_y_p + ref_h) >= height_p) ref_h = height_p - mi_y_p;
#else   // CONFIG_BAWP_ACROSS_SCALES_FIX
  const int x_off = mbmi->mv[ref].as_mv.col >> 3;
  const int y_off = mbmi->mv[ref].as_mv.row >> 3;

  const int x_off_p = x_off >> inter_pred_params->subsampling_x;
  const int y_off_p = y_off >> inter_pred_params->subsampling_y;

  const int mi_x_p = mi_x >> inter_pred_params->subsampling_x;
  const int mi_y_p = mi_y >> inter_pred_params->subsampling_y;

  const int width_p = cm->width >> inter_pred_params->subsampling_x;
  const int height_p = cm->height >> inter_pred_params->subsampling_y;

  int ref_w = bw;
  if ((mi_x_p + bw) >= width_p) ref_w = width_p - mi_x_p;

  int ref_h = bh;
  if ((mi_y_p + bh) >= height_p) ref_h = height_p - mi_y_p;
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
#if CONFIG_BAWP_ACROSS_SCALES_FIX
  if ((mi_x_p + x_off_p - scaled_x_gen(BAWP_REF_LINES, sf)) < 0 ||
      (mi_y_p + y_off_p - scaled_y_gen(BAWP_REF_LINES, sf)) < 0 || ref_w <= 0 ||
      ref_h <= 0 ||
#else   // CONFIG_BAWP_ACROSS_SCALES_FIX
  if ((mi_x_p + x_off_p - BAWP_REF_LINES) < 0 ||
      (mi_y_p + y_off_p - BAWP_REF_LINES) < 0 ||
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
      (mi_x_p + ref_w + x_off_p) >= width_p ||
      (mi_y_p + ref_h + y_off_p) >= height_p) {
    mbmi->bawp_alpha[plane][ref] = 1 << shift;
    mbmi->bawp_beta[plane][ref] = -(1 << shift);
  } else {
    uint16_t *recon_buf = xd->plane[plane].dst.buf;
    int recon_stride = xd->plane[plane].dst.stride;
    if (dst_orig != NULL) {
      recon_buf = dst_orig->plane[plane];
      recon_stride = dst_orig->stride[plane];
    }
    uint16_t *recon_top = recon_buf - BAWP_REF_LINES * recon_stride;
    uint16_t *recon_left = recon_buf - BAWP_REF_LINES;

    // the picture boundary limitation to be checked.
    struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_BAWP_ACROSS_SCALES_FIX
    int ref_stride = pd->pre[ref].stride;
    uint16_t *ref_buf = pd->pre[ref].buf + y_off_p * ref_stride + x_off_p;
    if (sf->x_scale_fp != REF_NO_SCALE || sf->y_scale_fp != REF_NO_SCALE) {
      const int mi_x_p_unscaled = mi_x >> inter_pred_params->subsampling_x;
      const int mi_y_p_unscaled = mi_y >> inter_pred_params->subsampling_y;
      const int width_p_unscaled =
          cm->width >> inter_pred_params->subsampling_x;
      const int height_p_unscaled =
          cm->height >> inter_pred_params->subsampling_y;
      ref_w = bw;
      if ((mi_x_p_unscaled + bw) >= width_p_unscaled)
        ref_w = width_p_unscaled - mi_x_p_unscaled;
      ref_h = bh;
      if ((mi_y_p_unscaled + bh) >= height_p_unscaled)
        ref_h = height_p_unscaled - mi_y_p_unscaled;

      calc_subpel_params_func(&mbmi->mv[ref].as_mv, inter_pred_params, xd, mi_x,
                              mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
                              0, /* use_optflow_refinement */
#endif                           // CONFIG_OPTFLOW_REFINEMENT
                              mc_buf, &ref_buf, &subpel_params, &ref_stride);
    }
    uint16_t *ref_top = ref_buf - ref_stride * scaled_y_gen(BAWP_REF_LINES, sf);
    uint16_t *ref_left = ref_buf - scaled_x_gen(BAWP_REF_LINES, sf);
#else   // CONFIG_BAWP_ACROSS_SCALES_FIX
    const int ref_stride = pd->pre[ref].stride;
    uint16_t *ref_buf = pd->pre[ref].buf + y_off_p * ref_stride + x_off_p;
    uint16_t *ref_top = ref_buf - BAWP_REF_LINES * ref_stride;
    uint16_t *ref_left = ref_buf - BAWP_REF_LINES;
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
#if CONFIG_EXPLICIT_BAWP
#if CONFIG_BAWP_CHROMA
    if (mbmi->bawp_flag[0] > 1 && plane == 0) {
#else
    if (mbmi->bawp_flag > 1) {
#endif  // CONFIG_BAWP_CHROMA
      const int first_ref_dist =
          cm->ref_frame_relative_dist[mbmi->ref_frame[0]];
      const int bawp_scale_table[3][EXPLICIT_BAWP_SCALE_CNT] = { { -1, 1 },
                                                                 { -2, 2 },
                                                                 { -3, 3 } };
      const int list_index =
          (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
#if CONFIG_BAWP_CHROMA
      int delta_scales = bawp_scale_table[list_index][mbmi->bawp_flag[0] - 2];
#else
      int delta_scales = bawp_scale_table[list_index][mbmi->bawp_flag - 2];
#endif  // CONFIG_BAWP_CHROMA
      const int delta_sign = delta_scales > 0 ? 1 : -1;
      const int delta_magtitude = delta_sign * delta_scales;
      if (first_ref_dist > 4) delta_scales = delta_sign * (delta_magtitude + 1);
      mbmi->bawp_alpha[plane][ref] = 256 + (delta_scales * 16);
      derive_explicit_bawp_offsets(xd, recon_top, recon_left, recon_stride,
                                   ref_top, ref_left, ref_stride, ref, plane,
                                   ref_w, ref_h);
    } else
#endif  // CONFIG_EXPLICIT_BAWP
      derive_bawp_parameters(xd, recon_top, recon_left, recon_stride, ref_top,
#if CONFIG_BAWP_ACROSS_SCALES_FIX
                             ref_left, ref_stride, ref, plane, ref_w, ref_h,
                             sf);
#else   // CONFIG_BAWP_ACROSS_SCALES_FIX
                           ref_left, ref_stride, ref, plane, ref_w, ref_h);
#endif  // CONFIG_BAWP_ACROSS_SCALES_FIX
  }

  int16_t alpha = mbmi->bawp_alpha[plane][ref];
  int32_t beta = mbmi->bawp_beta[plane][ref];
  for (int j = 0; j < bh; ++j) {
    for (int i = 0; i < bw; ++i) {
      dst[j * dst_stride + i] = clip_pixel_highbd(
          (dst[j * dst_stride + i] * alpha + beta) >> shift, xd->bd);
    }
  }
}
#endif  // CONFIG_BAWP

// True if the following hold:
//  1. Not intrabc and not build_for_obmc
//  2. At least one dimension is size 4 with subsampling
//  3. If sub-sampled, none of the previous blocks around the sub-sample
//     are intrabc or inter-blocks
static bool is_sub8x8_inter(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                            const MB_MODE_INFO *mi, int plane, int is_intrabc,
                            int build_for_obmc) {
  if (is_intrabc || build_for_obmc) {
    return false;
  }

  if (!(plane &&
        (mi->sb_type[PLANE_TYPE_UV] != mi->chroma_ref_info.bsize_base)))
    return false;

  // For sub8x8 chroma blocks, we may be covering more than one luma block's
  // worth of pixels. Thus (mi_row, mi_col) may not be the correct coordinates
  // for the top-left corner of the prediction source. So, we need to find the
  // correct top-left corner (row_start, col_start).
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int row_start =
      plane ? mi->chroma_ref_info.mi_row_chroma_base - mi_row : 0;
  const int col_start =
      plane ? mi->chroma_ref_info.mi_col_chroma_base - mi_col : 0;
  const BLOCK_SIZE plane_bsize =
      plane ? mi->chroma_ref_info.bsize_base : mi->sb_type[PLANE_TYPE_Y];
  const int plane_mi_height = mi_size_high[plane_bsize];
  const int plane_mi_width = mi_size_wide[plane_bsize];
  const int mi_rows = cm->mi_params.mi_rows;
  const int mi_cols = cm->mi_params.mi_cols;

  // Scan through all the blocks in the current chroma unit
  for (int row = 0; row < plane_mi_height; ++row) {
    const int row_coord = row_start + row;
    if (mi_row + row_coord >= mi_rows) break;
    for (int col = 0; col < plane_mi_width; ++col) {
      const int col_coord = col_start + col;
      if (mi_col + col_coord >= mi_cols) break;
      // For the blocks at the lower right of the final chroma block, the mis
      // are not set up correctly yet, so we do not check them.
      if ((row_coord >= 0 && col_coord > 0) ||
          (col_coord >= 0 && row_coord > 0)) {
        break;
      }
      const MB_MODE_INFO *this_mbmi =
          xd->mi[row_coord * xd->mi_stride + col_coord];
      assert(this_mbmi != NULL);
      if (!is_inter_block(this_mbmi, xd->tree_type)) return false;
      if (is_intrabc_block(this_mbmi, xd->tree_type)) return false;
    }
  }
  return true;
}

static void build_inter_predictors_sub8x8(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const bool ss_x = pd->subsampling_x;
  const bool ss_y = pd->subsampling_y;
  const BLOCK_SIZE plane_bsize =
      plane ? mi->chroma_ref_info.bsize_base : mi->sb_type[PLANE_TYPE_Y];
  const int plane_mi_height = mi_size_high[plane_bsize];
  const int plane_mi_width = mi_size_wide[plane_bsize];
  assert(!is_intrabc_block(mi, xd->tree_type));

  // For sub8x8 chroma blocks, we may be covering more than one luma block's
  // worth of pixels. Thus (mi_x, mi_y) may not be the correct coordinates for
  // the top-left corner of the prediction source - the correct top-left corner
  // is at (pre_x, pre_y).
  const int row_start =
      plane ? (mi->chroma_ref_info.mi_row_chroma_base - xd->mi_row) : 0;
  const int col_start =
      plane ? (mi->chroma_ref_info.mi_col_chroma_base - xd->mi_col) : 0;
  const int pre_x = (mi_x + MI_SIZE * col_start) >> ss_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> ss_y;
  const int mi_stride = xd->mi_stride;
  const int mi_rows = cm->mi_params.mi_rows;
  const int mi_cols = cm->mi_params.mi_cols;

  const int mb_to_top_edge_start = xd->mb_to_top_edge;
  const int mb_to_left_edge_start = xd->mb_to_left_edge;
  const int mb_to_bottom_edge_start = xd->mb_to_bottom_edge;
  const int mb_to_right_edge_start = xd->mb_to_right_edge;

  // Row progress keeps track of which mi block in the row has been set.
  SUB_8_BITMASK_T row_progress[MAX_MI_LUMA_SIZE_FOR_SUB_8] = { 0 };
  assert(plane_mi_height <= MAX_MI_LUMA_SIZE_FOR_SUB_8);
  assert(plane_mi_width <= MAX_MI_LUMA_SIZE_FOR_SUB_8);
  assert(MAX_MI_LUMA_SIZE_FOR_SUB_8 == SUB_8_BITMASK_SIZE);
  for (int mi_row = 0; mi_row < plane_mi_height; mi_row++) {
    if (xd->mi_row + row_start + mi_row >= mi_rows) break;
    for (int mi_col = 0; mi_col < plane_mi_width; mi_col++) {
      if (xd->mi_col + col_start + mi_col >= mi_cols) break;
      const SUB_8_BITMASK_T check_flag = 1 << (SUB_8_BITMASK_SIZE - 1 - mi_col);
      if (row_progress[mi_row] & check_flag) {
        continue;
      }

      const MB_MODE_INFO *this_mbmi =
          xd->mi[(row_start + mi_row) * mi_stride + (col_start + mi_col)];
      assert(this_mbmi != NULL);

      const BLOCK_SIZE bsize = this_mbmi->sb_type[PLANE_TYPE_Y];
      const int mi_width = mi_size_wide[bsize];
      const int mi_height = mi_size_high[bsize];

      int row = row_start + mi_row + xd->mi_row;
      int col = col_start + mi_col + xd->mi_col;
      xd->mb_to_top_edge = -GET_MV_SUBPEL(row * MI_SIZE);
      xd->mb_to_bottom_edge =
          GET_MV_SUBPEL((cm->mi_params.mi_rows - mi_height - row) * MI_SIZE);
      xd->mb_to_left_edge = -GET_MV_SUBPEL((col * MI_SIZE));
      xd->mb_to_right_edge =
          GET_MV_SUBPEL((cm->mi_params.mi_cols - mi_width - col) * MI_SIZE);

      // The flag here is a block of mi_width many 1s offset by the mi_col.
      // For example, if the current mi_col is 2, and the mi_width is 2, then
      // the flag will be 00110000. We or this with row_progress to update the
      // blocks that have been coded.
      // Note that because we are always coding in a causal order, we could
      // technically simplify the bitwise operation, and use the flag 11110000
      // in the above example instead. However, we are not taking this approach
      // here to keep the logic simpler.
      const SUB_8_BITMASK_T set_flag =
          ((SUB_8_BITMASK_ON << (SUB_8_BITMASK_SIZE - mi_width)) &
           SUB_8_BITMASK_ON) >>
          mi_col;
      for (int mi_row_offset = 0; mi_row_offset < mi_height; mi_row_offset++) {
        row_progress[mi_row + mi_row_offset] |= set_flag;
      }

      assert(is_inter_block(this_mbmi, xd->tree_type));
      const int chroma_width = block_size_wide[bsize] >> ss_x;
      const int chroma_height = block_size_high[bsize] >> ss_y;
      const int pixel_row = (MI_SIZE * mi_row >> ss_y);
      const int pixel_col = (MI_SIZE * mi_col >> ss_x);
#if CONFIG_EXT_RECUR_PARTITIONS
      // TODO(yuec): enabling compound prediction in none sub8x8 mbs in the
      // group
      bool is_compound = 0;
#else
      bool is_compound = has_second_ref(this_mbmi);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      struct buf_2d *const dst_buf = &pd->dst;
      uint16_t *dst = dst_buf->buf + dst_buf->stride * pixel_row + pixel_col;
      int ref = 0;
      const RefCntBuffer *ref_buf =
          get_ref_frame_buf(cm, this_mbmi->ref_frame[ref]);
      const struct scale_factors *ref_scale_factors =
          get_ref_scale_factors_const(cm, this_mbmi->ref_frame[ref]);
      const struct scale_factors *const sf = ref_scale_factors;
      const struct buf_2d pre_buf = {
        NULL,
        (plane == 1) ? ref_buf->buf.u_buffer : ref_buf->buf.v_buffer,
        ref_buf->buf.uv_crop_width,
        ref_buf->buf.uv_crop_height,
        ref_buf->buf.uv_stride,
      };

      const MV mv = this_mbmi->mv[ref].as_mv;
      InterPredParams inter_pred_params;
      av1_init_inter_params(
          &inter_pred_params, chroma_width, chroma_height, pre_y + pixel_row,
          pre_x + pixel_col, pd->subsampling_x, pd->subsampling_y, xd->bd,
          mi->use_intrabc[0], sf, &pre_buf, this_mbmi->interp_fltr);
      inter_pred_params.conv_params =
          get_conv_params_no_round(ref, plane, NULL, 0, is_compound, xd->bd);

      av1_build_one_inter_predictor(
          dst, dst_buf->stride, &mv, &inter_pred_params, xd, mi_x + pixel_col,
          mi_y + pixel_row, ref, mc_buf, calc_subpel_params_func);
    }
  }
  xd->mb_to_top_edge = mb_to_top_edge_start;
  xd->mb_to_bottom_edge = mb_to_bottom_edge_start;
  xd->mb_to_left_edge = mb_to_left_edge_start;
  xd->mb_to_right_edge = mb_to_right_edge_start;
}

#if CONFIG_REFINEMV
// Padding if the pixel position falls outside of the defined reference area
static void refinemv_highbd_pad_mc_border(const uint16_t *src, int src_stride,
                                          uint16_t *dst, int dst_stride, int x0,
                                          int y0, int b_w, int b_h,
                                          const ReferenceArea *ref_area) {
  // Get a pointer to the start of the real data for this row.
  const uint16_t *ref_row = src - x0 - y0 * src_stride;

  if (y0 >= ref_area->pad_block.y1)
    ref_row += (ref_area->pad_block.y1 - 1) * src_stride;
  else if (y0 >= ref_area->pad_block.y0)
    ref_row += y0 * src_stride;
  else
    ref_row += ref_area->pad_block.y0 * src_stride;

  do {
    int right = 0, copy;
    int left = x0 < ref_area->pad_block.x0 ? ref_area->pad_block.x0 - x0 : 0;

    if (left > b_w) left = b_w;

    if (x0 + b_w > ref_area->pad_block.x1)
      right = x0 + b_w - ref_area->pad_block.x1;

    if (right > b_w) right = b_w;

    copy = b_w - left - right;

    if (left) aom_memset16(dst, ref_row[0], left);

    if (copy) memcpy(dst + left, ref_row + x0 + left, copy * sizeof(uint16_t));

    if (right)
      aom_memset16(dst + left + copy, ref_row[ref_area->pad_block.x1 - 1],
                   right);

    dst += dst_stride;
    ++y0;

    if (y0 > ref_area->pad_block.y0 && y0 < ref_area->pad_block.y1)
      ref_row += src_stride;
  } while (--b_h);
}
// check if padding is required during motion compensation
// return 1 means reference pixel is outside of the reference range and padding
// is required return 0 means no padding.
int update_extend_mc_border_params(const struct scale_factors *const sf,
                                   struct buf_2d *const pre_buf, MV32 scaled_mv,
                                   PadBlock *block, int subpel_x_mv,
                                   int subpel_y_mv, int do_warp, int is_intrabc,
                                   int *x_pad, int *y_pad,
                                   const ReferenceArea *ref_area) {
  // Get reference width and height.
  int frame_width = pre_buf->width;
  int frame_height = pre_buf->height;

  // Do border extension if there is motion or
  // width/height is not a multiple of 8 pixels.
  // Extension is needed in optical flow refinement to obtain MV offsets
  (void)scaled_mv;
  if (!is_intrabc && !do_warp) {
    if (subpel_x_mv || (sf->x_step_q4 != SUBPEL_SHIFTS)) {
      block->x0 -= AOM_INTERP_EXTEND - 1;
      block->x1 += AOM_INTERP_EXTEND;
      *x_pad = 1;
    }

    if (subpel_y_mv || (sf->y_step_q4 != SUBPEL_SHIFTS)) {
      block->y0 -= AOM_INTERP_EXTEND - 1;
      block->y1 += AOM_INTERP_EXTEND;
      *y_pad = 1;
    }

    // Skip border extension if block is inside the frame.
    if (block->x0 < 0 || block->x1 > frame_width - 1 || block->y0 < 0 ||
        block->y1 > frame_height - 1) {
      return 1;
    }

    if (ref_area) {
      // Skip border extension if block is in the reference area.
      if (block->x0 < ref_area->pad_block.x0 ||
          block->x1 > ref_area->pad_block.x1 ||
          block->y0 < ref_area->pad_block.y0 ||
          block->y1 > ref_area->pad_block.y1) {
        return 1;
      }
    }
  }
  return 0;
};

// perform padding of the motion compensated block if requires.
// Padding is performed if the motion compensated block is partially out of the
// reference area.
static void refinemv_extend_mc_border(
    const struct scale_factors *const sf, struct buf_2d *const pre_buf,
    MV32 scaled_mv, PadBlock block, int subpel_x_mv, int subpel_y_mv,
    int do_warp, int is_intrabc, uint16_t *paded_ref_buf,
    int paded_ref_buf_stride, uint16_t **pre, int *src_stride,
    const ReferenceArea *ref_area) {
  int x_pad = 0, y_pad = 0;
  if (update_extend_mc_border_params(sf, pre_buf, scaled_mv, &block,
                                     subpel_x_mv, subpel_y_mv, do_warp,
                                     is_intrabc, &x_pad, &y_pad, ref_area)) {
    // printf(" Out of border \n");
    // Get reference block pointer.
    const uint16_t *const buf_ptr =
        pre_buf->buf0 + block.y0 * pre_buf->stride + block.x0;
    int buf_stride = pre_buf->stride;
    const int b_w = block.x1 - block.x0;
    const int b_h = block.y1 - block.y0;

    refinemv_highbd_pad_mc_border(buf_ptr, buf_stride, paded_ref_buf,
                                  paded_ref_buf_stride, block.x0, block.y0, b_w,
                                  b_h, ref_area);
    *src_stride = paded_ref_buf_stride;
    *pre = paded_ref_buf +
           y_pad * (AOM_INTERP_EXTEND - 1) * paded_ref_buf_stride +
           x_pad * (AOM_INTERP_EXTEND - 1);
  }
}

#if !CONFIG_TIP_REF_PRED_MERGING
// Derive the sub-pixel related parameters of TIP blocks
// Sub-pel related parameters are stored in the structures pointed by
// "subpel_params" and "block"
void tip_dec_calc_subpel_params(const MV *const src_mv,
                                InterPredParams *const inter_pred_params,
                                int mi_x, int mi_y, uint16_t **pre,
                                SubpelParams *subpel_params, int *src_stride,
                                PadBlock *block,
#if CONFIG_OPTFLOW_REFINEMENT
                                int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                MV32 *scaled_mv, int *subpel_x_mv,
                                int *subpel_y_mv) {
  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;

#if CONFIG_REFINEMV
  const int bw = inter_pred_params->original_pu_width;
  const int bh = inter_pred_params->original_pu_height;
#else
#if CONFIG_OPTFLOW_REFINEMENT
  // Use original block size to clamp MV and to extend block boundary
  const int bw = use_optflow_refinement ? inter_pred_params->orig_block_width
                                        : inter_pred_params->block_width;
  const int bh = use_optflow_refinement ? inter_pred_params->orig_block_height
                                        : inter_pred_params->block_height;
#else
  const int bw = inter_pred_params->block_width;
  const int bh = inter_pred_params->block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT
#endif  // CONFIG_REFINEMV

  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled) {
    const int ssx = inter_pred_params->subsampling_x;
    const int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
#else
    orig_pos_y += src_mv->row * (1 << (1 - ssy));
    orig_pos_x += src_mv->col * (1 << (1 - ssx));
#endif  // CONFIG_OPTFLOW_REFINEMENT
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    subpel_params->subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_params->subpel_y = pos_y & SCALE_SUBPEL_MASK;
    subpel_params->xs = sf->x_step_q4;
    subpel_params->ys = sf->y_step_q4;

    // Get reference block top left coordinate.
    block->x0 = pos_x >> SCALE_SUBPEL_BITS;
    block->y0 = pos_y >> SCALE_SUBPEL_BITS;

#if CONFIG_D071_IMP_MSK_BLD
    block->x1 =
        ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
         SCALE_SUBPEL_BITS) +
        1;
    block->y1 =
        ((pos_y + (inter_pred_params->block_height - 1) * subpel_params->ys) >>
         SCALE_SUBPEL_BITS) +
        1;
#else
    // Get reference block bottom right coordinate.
    block->x1 =
        ((pos_x + (bw - 1) * subpel_params->xs) >> SCALE_SUBPEL_BITS) + 1;
    block->y1 =
        ((pos_y + (bh - 1) * subpel_params->ys) >> SCALE_SUBPEL_BITS) + 1;
#endif  // CONFIG_D071_IMP_MSK_BLD

    MV temp_mv;
    temp_mv = tip_clamp_mv_to_umv_border_sb(inter_pred_params, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                            use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                            inter_pred_params->subsampling_x,
                                            inter_pred_params->subsampling_y);
    *scaled_mv = av1_scale_mv(&temp_mv, mi_x, mi_y, sf);
    scaled_mv->row += SCALE_EXTRA_OFF;
    scaled_mv->col += SCALE_EXTRA_OFF;

    *subpel_x_mv = scaled_mv->col & SCALE_SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SCALE_SUBPEL_MASK;
  } else {
    // Get block position in current frame.
    int pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    int pos_y = inter_pred_params->pix_row << SUBPEL_BITS;

    const MV mv_q4 = tip_clamp_mv_to_umv_border_sb(
        inter_pred_params, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;

    // Get reference block top left coordinate.
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    pos_x = (pos_x >> SUBPEL_BITS);
    pos_y = (pos_y >> SUBPEL_BITS);
    block->x0 = pos_x;
    block->y0 = pos_y;

    // Get reference block bottom right coordinate.
#if CONFIG_D071_IMP_MSK_BLD
    block->x1 = pos_x + inter_pred_params->block_width;
    block->y1 = pos_y + inter_pred_params->block_height;
#else
    block->x1 = pos_x + bw;
    block->y1 = pos_y + bh;
#endif  // CONFIG_D071_IMP_MSK_BLD

    scaled_mv->row = mv_q4.row;
    scaled_mv->col = mv_q4.col;
    *subpel_x_mv = scaled_mv->col & SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SUBPEL_MASK;
  }
  *pre = pre_buf->buf0 + block->y0 * pre_buf->stride + block->x0;
  *src_stride = pre_buf->stride;
#if CONFIG_D071_IMP_MSK_BLD
  if (inter_pred_params->border_data.enable_bacp) {
    subpel_params->x0 = block->x0;
    subpel_params->x1 = block->x1;
    subpel_params->y0 = block->y0;
    subpel_params->y1 = block->y1;
  }
#endif  // CONFIG_D071_IMP_MSK_BLD
}

void tip_common_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    uint16_t **mc_buf, uint16_t **pre, SubpelParams *subpel_params,
    int *src_stride) {
  (void)ref;
  (void)mc_buf;
  (void)xd;

  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  assert(inter_pred_params->use_ref_padding);

  tip_dec_calc_subpel_params(src_mv, inter_pred_params, mi_x, mi_y, pre,
                             subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                             use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             &scaled_mv, &subpel_x_mv, &subpel_y_mv);

  const int paded_ref_buf_stride =
      inter_pred_params->ref_area->paded_ref_buf_stride;
  refinemv_extend_mc_border(
      inter_pred_params->scale_factors, &inter_pred_params->ref_frame_buf,
      scaled_mv, block, subpel_x_mv, subpel_y_mv,
      inter_pred_params->mode == WARP_PRED, inter_pred_params->is_intrabc,
      &inter_pred_params->ref_area->paded_ref_buf[0], paded_ref_buf_stride, pre,
      src_stride, inter_pred_params->ref_area);
}
#endif  // !CONFIG_TIP_REF_PRED_MERGING

void dec_calc_subpel_params(const MV *const src_mv,
                            InterPredParams *const inter_pred_params,
                            const MACROBLOCKD *const xd, int mi_x, int mi_y,
                            uint16_t **pre, SubpelParams *subpel_params,
                            int *src_stride, PadBlock *block,
#if CONFIG_OPTFLOW_REFINEMENT
                            int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                            MV32 *scaled_mv, int *subpel_x_mv,
                            int *subpel_y_mv) {
  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;

#if CONFIG_REFINEMV
  const int bw = inter_pred_params->original_pu_width;
  const int bh = inter_pred_params->original_pu_height;
#else

#if CONFIG_OPTFLOW_REFINEMENT
  // Use original block size to clamp MV and to extend block boundary
  const int bw = use_optflow_refinement ? inter_pred_params->orig_block_width
                                        : inter_pred_params->block_width;
  const int bh = use_optflow_refinement ? inter_pred_params->orig_block_height
                                        : inter_pred_params->block_height;
#else
  const int bw = inter_pred_params->block_width;
  const int bh = inter_pred_params->block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT
#endif  // CONFIG_REFINEMV

  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled) {
    int ssx = inter_pred_params->subsampling_x;
    int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
#else
    orig_pos_y += src_mv->row * (1 << (1 - ssy));
    orig_pos_x += src_mv->col * (1 << (1 - ssx));
#endif  // CONFIG_OPTFLOW_REFINEMENT
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    subpel_params->subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_params->subpel_y = pos_y & SCALE_SUBPEL_MASK;
    subpel_params->xs = sf->x_step_q4;
    subpel_params->ys = sf->y_step_q4;

    // Get reference block top left coordinate.
    block->x0 = pos_x >> SCALE_SUBPEL_BITS;
    block->y0 = pos_y >> SCALE_SUBPEL_BITS;

    // Get reference block bottom right coordinate.
    block->x1 =
        ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
         SCALE_SUBPEL_BITS) +
        1;
    block->y1 =
        ((pos_y + (inter_pred_params->block_height - 1) * subpel_params->ys) >>
         SCALE_SUBPEL_BITS) +
        1;

    MV temp_mv;
    temp_mv = clamp_mv_to_umv_border_sb(xd, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                        inter_pred_params->subsampling_x,
                                        inter_pred_params->subsampling_y);
    *scaled_mv = av1_scale_mv(&temp_mv, mi_x, mi_y, sf);
    scaled_mv->row += SCALE_EXTRA_OFF;
    scaled_mv->col += SCALE_EXTRA_OFF;

    *subpel_x_mv = scaled_mv->col & SCALE_SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SCALE_SUBPEL_MASK;
  } else {
    // Get block position in current frame.
    int pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    int pos_y = inter_pred_params->pix_row << SUBPEL_BITS;

    const MV mv_q4 = clamp_mv_to_umv_border_sb(
        xd, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;

    // Get reference block top left coordinate.
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    block->x0 = pos_x >> SUBPEL_BITS;
    block->y0 = pos_y >> SUBPEL_BITS;

    // Get reference block bottom right coordinate.
    block->x1 =
        (pos_x >> SUBPEL_BITS) + (inter_pred_params->block_width - 1) + 1;
    block->y1 =
        (pos_y >> SUBPEL_BITS) + (inter_pred_params->block_height - 1) + 1;

    scaled_mv->row = mv_q4.row;
    scaled_mv->col = mv_q4.col;
    *subpel_x_mv = scaled_mv->col & SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SUBPEL_MASK;
  }
  *pre = pre_buf->buf0 + block->y0 * pre_buf->stride + block->x0;
  *src_stride = pre_buf->stride;

#if CONFIG_D071_IMP_MSK_BLD
  if (inter_pred_params->border_data.enable_bacp) {
    subpel_params->x0 = block->x0;
    subpel_params->x1 = block->x1;
    subpel_params->y0 = block->y0;
    subpel_params->y1 = block->y1;
  }
#endif  // CONFIG_D071_IMP_MSK_BLD
}

void common_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    uint16_t **mc_buf, uint16_t **pre, SubpelParams *subpel_params,
    int *src_stride) {
  (void)ref;
  (void)mc_buf;

  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  assert(inter_pred_params->use_ref_padding);
  dec_calc_subpel_params(src_mv, inter_pred_params, xd, mi_x, mi_y, pre,
                         subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                         use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                         &scaled_mv, &subpel_x_mv, &subpel_y_mv);

  // printf(" Use ref padding \n");
  const int paded_ref_buf_stride =
      inter_pred_params->ref_area->paded_ref_buf_stride;
  refinemv_extend_mc_border(
      inter_pred_params->scale_factors, &inter_pred_params->ref_frame_buf,
      scaled_mv, block, subpel_x_mv, subpel_y_mv,
      inter_pred_params->mode == WARP_PRED, inter_pred_params->is_intrabc,
      &inter_pred_params->ref_area->paded_ref_buf[0], paded_ref_buf_stride, pre,
      src_stride, inter_pred_params->ref_area);
}

static void get_ref_area_info(const MV *const src_mv,
                              InterPredParams *const inter_pred_params,
                              MACROBLOCKD *const xd, int mi_x, int mi_y,
#if CONFIG_OPTFLOW_REFINEMENT
                              int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                              uint16_t **pre, SubpelParams *subpel_params,
                              int *src_stride, ReferenceArea *ref_area
#if !CONFIG_TIP_REF_PRED_MERGING
                              ,
                              int is_tip
#endif  // !CONFIG_TIP_REF_PRED_MERGING
) {
  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;

#if !CONFIG_TIP_REF_PRED_MERGING
  if (is_tip) {
    tip_dec_calc_subpel_params(src_mv, inter_pred_params, mi_x, mi_y, pre,
                               subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                               use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                               &scaled_mv, &subpel_x_mv, &subpel_y_mv);

  } else {
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    dec_calc_subpel_params(src_mv, inter_pred_params, xd, mi_x, mi_y, pre,
                           subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                           use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                           &scaled_mv, &subpel_x_mv, &subpel_y_mv);
#if !CONFIG_TIP_REF_PRED_MERGING
  }
#endif  // !CONFIG_TIP_REF_PRED_MERGING

  struct buf_2d *const pre_buf = &inter_pred_params->ref_frame_buf;
  int frame_height = pre_buf->height;
  int frame_width = pre_buf->width;
  block.x0 -= REF_LEFT_BORDER;
  block.x1 += REF_RIGHT_BORDER;
  block.y0 -= REF_TOP_BORDER;
  block.y1 += REF_BOTTOM_BORDER;

  ref_area->pad_block.x0 = CLIP(block.x0, 0, frame_width - 1);
  ref_area->pad_block.y0 = CLIP(block.y0, 0, frame_height - 1);
  ref_area->pad_block.x1 = CLIP(block.x1, 1, frame_width);
  ref_area->pad_block.y1 = CLIP(block.y1, 1, frame_height);
}

void av1_get_reference_area_with_padding(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         int plane, MB_MODE_INFO *mi,
#if CONFIG_TIP_REF_PRED_MERGING
                                         const MV mv[2],
#endif  // CONFIG_TIP_REF_PRED_MERGING
                                         int bw, int bh, int mi_x, int mi_y,
                                         ReferenceArea ref_area[2]
#if !CONFIG_TIP_REF_PRED_MERGING
                                         ,
                                         const int comp_pixel_x,
                                         const int comp_pixel_y
#else
                                         ,
                                         int pu_width, int pu_height
#endif  // !CONFIG_TIP_REF_PRED_MERGING
) {
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;
  assert(IMPLIES(!is_tip, has_second_ref(mi)));
  assert(!is_intrabc_block(mi, xd->tree_type));
  struct macroblockd_plane *const pd = &xd->plane[plane];

  int row_start = 0;
  int col_start = 0;
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
  col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;

#if CONFIG_TIP_REF_PRED_MERGING
  const int pre_x = ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);
#else
  const int pre_x = is_tip
                        ? comp_pixel_x
                        : ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = is_tip
                        ? comp_pixel_y
                        : ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);
#endif  // CONFIG_TIP_REF_PRED_MERGING

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf =
        is_tip ? cm->tip_ref.ref_scale_factor[ref]
               : xd->block_ref_scale_factors[ref];
#if CONFIG_TIP_REF_PRED_MERGING
    const struct buf_2d *const pre_buf = &pd->pre[ref];
#else
    const struct buf_2d *const pre_buf =
        is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref] : &pd->pre[ref];
#endif  // CONFIG_TIP_REF_PRED_MERGING

    // initialize the reference buffer
    ref_area[ref].pad_block.x0 = 0;
    ref_area[ref].pad_block.y0 = 0;
    ref_area[ref].pad_block.x1 = cm->width;
    ref_area[ref].pad_block.y1 = cm->height;
    ref_area[ref].paded_ref_buf_stride = REF_BUFFER_WIDTH;

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf,
#if CONFIG_TIP_REF_PRED_MERGING
                          mi->interp_fltr
#else
                          is_tip ? MULTITAP_SHARP : mi->interp_fltr
#endif
    );

#if !CONFIG_TIP_REF_PRED_MERGING
    inter_pred_params.original_pu_width = bw;
    inter_pred_params.original_pu_height = bh;

    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params.dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
    inter_pred_params.dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - pre_y);
    inter_pred_params.dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
    inter_pred_params.dist_to_right_edge = GET_MV_SUBPEL(width - bw - pre_x);
#else
    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;
#endif  // CONFIG_TIP_REF_PRED_MERGING

    SubpelParams subpel_params;
    uint16_t *src;
    int src_stride;

    assert(!inter_pred_params.use_ref_padding);

#if CONFIG_TIP_REF_PRED_MERGING
    const MV *src_mv = ref == 0 ? &mv[0] : &mv[1];
#else
    MV *src_mv = ref == 0 ? &mi->mv[0].as_mv : &mi->mv[1].as_mv;
#endif
    get_ref_area_info(src_mv, &inter_pred_params, xd, mi_x, mi_y,
#if CONFIG_OPTFLOW_REFINEMENT
                      0, /* use_optflow_refinement */
#endif                   // CONFIG_OPTFLOW_REFINEMENT
                      &src, &subpel_params, &src_stride, &ref_area[ref]
#if !CONFIG_TIP_REF_PRED_MERGING
                      ,
                      is_tip
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    );
  }
}

int av1_refinemv_build_predictors_and_get_sad(
    MACROBLOCKD *xd, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst_ref0,
    uint16_t *dst_ref1, MV mv0, MV mv1, InterPredParams *inter_pred_params) {
  for (int ref = 0; ref < 2; ref++) {
    SubpelParams subpel_params;
    uint16_t *src;
    int src_stride;
    uint16_t *dst_ref = ref == 0 ? dst_ref0 : dst_ref1;
    MV *src_mv = ref == 0 ? &mv0 : &mv1;
#if CONFIG_SUBBLK_REF_EXT
    src_mv->row -= 8 * SUBBLK_REF_EXT_LINES;
    src_mv->col -= 8 * SUBBLK_REF_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT
    calc_subpel_params_func(src_mv, &inter_pred_params[ref], xd, mi_x, mi_y,
                            ref,
#if CONFIG_OPTFLOW_REFINEMENT
                            0, /* use_optflow_refinement */
#endif                         // CONFIG_OPTFLOW_REFINEMENT
                            mc_buf, &src, &subpel_params, &src_stride);
    assert(inter_pred_params[ref].comp_mode == UNIFORM_SINGLE ||
           inter_pred_params[ref].comp_mode == UNIFORM_COMP);
    av1_make_inter_predictor(src, src_stride, dst_ref, bw,
                             &inter_pred_params[ref], &subpel_params);
  }

  return get_refinemv_sad(dst_ref0, dst_ref1, bw, bh, xd->bd);
}
void apply_mv_refinement(const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                         MB_MODE_INFO *mi, int bw, int bh, int mi_x, int mi_y,
                         uint16_t **mc_buf,
#if CONFIG_TIP_REF_PRED_MERGING
                         const MV mv[2],
#endif  // CONFIG_TIP_REF_PRED_MERGING
                         CalcSubpelParamsFunc calc_subpel_params_func,
                         int pre_x, int pre_y, uint16_t *dst_ref0,
                         uint16_t *dst_ref1, MV *best_mv_ref, int pu_width,
                         int pu_height) {
  // initialize basemv as best MV
#if CONFIG_TIP_REF_PRED_MERGING
  best_mv_ref[0] = mv[0];
  best_mv_ref[1] = mv[1];
#else
  best_mv_ref[0] = mi->mv[0].as_mv;
  best_mv_ref[1] = mi->mv[1].as_mv;
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_SUBBLK_REF_EXT
  bw += 2 * SUBBLK_REF_EXT_LINES;
  bh += 2 * SUBBLK_REF_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT

  const MV center_mvs[2] = { best_mv_ref[0], best_mv_ref[1] };
  assert(mi->refinemv_flag < REFINEMV_NUM_MODES);
  assert(cm->seq_params.enable_refinemv);

  // Generate MV independent inter_pred_params for both references
  InterPredParams inter_pred_params[2];
  for (int ref = 0; ref < 2; ref++) {
    const int is_compound = 0;
    const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
    const int is_tip = mi->ref_frame[0] == TIP_FRAME;

    assert(is_intrabc == 0);
    assert(plane == 0);
    struct macroblockd_plane *const pd = &xd->plane[plane];
    struct buf_2d *const dst_buf = &pd->dst;

    const struct scale_factors *const sf =
        is_tip ? cm->tip_ref.ref_scale_factor[ref]
               : (is_intrabc ? &cm->sf_identity
                             : xd->block_ref_scale_factors[ref]);
#if CONFIG_TIP_REF_PRED_MERGING
    const struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#else
    const struct buf_2d *const pre_buf =
        is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref]
               : (is_intrabc ? dst_buf : &pd->pre[ref]);
#endif  // CONFIG_TIP_REF_PRED_MERGING

    av1_init_inter_params(&inter_pred_params[ref], bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, BILINEAR);

#if CONFIG_REFINEMV
    inter_pred_params[ref].original_pu_width = pu_width;
    inter_pred_params[ref].original_pu_height = pu_height;
#endif  // CONFIG_REFINEMV

#if !CONFIG_TIP_REF_PRED_MERGING
    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params[ref].dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
    inter_pred_params[ref].dist_to_bottom_edge =
        GET_MV_SUBPEL(height - bh - pre_y);
    inter_pred_params[ref].dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
    inter_pred_params[ref].dist_to_right_edge =
        GET_MV_SUBPEL(width - bw - pre_x);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

    inter_pred_params[ref].conv_params = get_conv_params_no_round(
        0, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    assert(inter_pred_params[ref].mode == TRANSLATION_PRED);
    assert(inter_pred_params[ref].comp_mode == UNIFORM_SINGLE);
    assert(inter_pred_params[ref].conv_params.is_compound == 0);
    assert(inter_pred_params[ref].conv_params.do_average == 0);
    assert(mi->interinter_comp.type == COMPOUND_AVERAGE);
  }

#if !SINGLE_STEP_SEARCH
  // Search integer-delta values
  int search_range = 2;
#endif

  int switchable_refinemv_flags =
      (mi->ref_frame[0] != TIP_FRAME) && switchable_refinemv_flag(cm, mi);
#if !CONFIG_TIP_REF_PRED_MERGING
  assert(mi->refinemv_flag);
#endif  // !CONFIG_TIP_REF_PRED_MERGING

  // If we signal the refinemv_flags we do not select sad0
  // Set sad0 a large value so that it does not be selected
  int sad0 = switchable_refinemv_flags
                 ? (INT32_MAX >> 1)
                 : av1_refinemv_build_predictors_and_get_sad(
                       xd, bw, bh, mi_x, mi_y, mc_buf, calc_subpel_params_func,
                       dst_ref0, dst_ref1, center_mvs[0], center_mvs[1],
                       inter_pred_params);
#if !CONFIG_SUBBLK_REF_EXT
  assert(IMPLIES(mi->ref_frame[0] == TIP_FRAME, bw == 8 && bh == 8));
#endif  // !CONFIG_SUBBLK_REF_EXT
  if (mi->ref_frame[0] == TIP_FRAME) {
    const int tip_sad_thres = bw * bh;
    if (!switchable_refinemv_flags && sad0 < tip_sad_thres) return;
  }

  if (!switchable_refinemv_flags) {
    int shift = 3;
    int th = (bw * bh) << 1;
    sad0 -= (sad0 >> shift);
    assert(sad0 >= 0);
    if (sad0 < th) return;
  }

  int min_sad = sad0;
  MV refined_mv0, refined_mv1;
  refined_mv0 = center_mvs[0];
  refined_mv1 = center_mvs[1];
  int et_sad_th = (bw * bh) << 1;

#if !SINGLE_STEP_SEARCH
  uint8_t already_searched[5][5];
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      already_searched[i][j] = 0;
    }
  }
#endif

  MV best_offset = { 0, 0 };

#if SINGLE_STEP_SEARCH
  const int num_neighbors = 24;
  static const MV neighbors[24] = {
    { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 },   { 1, 1 },   { 1, 0 },
    { 1, -1 },  { 0, -1 }, { 0, -2 }, { -1, -2 }, { -2, -2 }, { -2, -1 },
    { -2, 0 },  { -2, 1 }, { -2, 2 }, { -1, 2 },  { 0, 2 },   { 1, 2 },
    { 2, 2 },   { 2, 1 },  { 2, 0 },  { 2, -1 },  { 2, -2 },  { 1, -2 }

  };

#else
  const int num_neighbors = 8;
  // Apply two-step full pel refinement
  static const MV neighbors[8] = { { 0, -1 }, { 1, 0 }, { 0, 1 },   { -1, 0 },
                                   { 1, -1 }, { 1, 1 }, { -1, -1 }, { -1, 1 } };

  const int num_iterations = search_range;
  already_searched[0 + search_range][0 + search_range] =
      1;  // center point is already searched before
  for (int ite = 0; ite < num_iterations; ++ite) {
#endif  // SINGLE_STEP_SEARCH

  int best_idx = -1;

  for (int idx = 0; idx < num_neighbors; ++idx) {
    MV offset = { best_offset.row + neighbors[idx].row,
                  best_offset.col + neighbors[idx].col };
#if !SINGLE_STEP_SEARCH
    if (already_searched[offset.row + search_range][offset.col + search_range])
      continue;
#endif
    refined_mv0.row = center_mvs[0].row + 8 * offset.row;
    refined_mv0.col = center_mvs[0].col + 8 * offset.col;
    refined_mv1.row = center_mvs[1].row - 8 * offset.row;
    refined_mv1.col = center_mvs[1].col - 8 * offset.col;

    int this_sad = av1_refinemv_build_predictors_and_get_sad(
        xd, bw, bh, mi_x, mi_y, mc_buf, calc_subpel_params_func, dst_ref0,
        dst_ref1, refined_mv0, refined_mv1, inter_pred_params);

#if !SINGLE_STEP_SEARCH
    already_searched[offset.row + search_range][offset.col + search_range] = 1;
#endif

    if (this_sad < min_sad) {
      min_sad = this_sad;
      best_idx = idx;
      // if the SAD is less than predefined threshold consider this candidate
      // as good enough to skip rest of the search.
      if (min_sad < et_sad_th) {
        best_mv_ref[0] = refined_mv0;
        best_mv_ref[1] = refined_mv1;
        return;
      }
    }
  }

  // if the center is best, skip rest of the search.
  if (best_idx == -1) {
    best_mv_ref[0].row = center_mvs[0].row + 8 * best_offset.row;
    best_mv_ref[0].col = center_mvs[0].col + 8 * best_offset.col;
    best_mv_ref[1].row = center_mvs[1].row - 8 * best_offset.row;
    best_mv_ref[1].col = center_mvs[1].col - 8 * best_offset.col;

    return;
  }

  if (best_idx >= 0) {
    best_offset.row += neighbors[best_idx].row;
    best_offset.col += neighbors[best_idx].col;
  }
#if !SINGLE_STEP_SEARCH
}
#endif

best_mv_ref[0].row = center_mvs[0].row + 8 * best_offset.row;
best_mv_ref[0].col = center_mvs[0].col + 8 * best_offset.col;
best_mv_ref[1].row = center_mvs[1].row - 8 * best_offset.row;
best_mv_ref[1].col = center_mvs[1].col - 8 * best_offset.col;

assert(min_sad <= sad0);

assert(IMPLIES(switchable_refinemv_flags,
               !(best_mv_ref[0].row == center_mvs[0].row &&
                 best_mv_ref[0].col == center_mvs[0].col &&
                 best_mv_ref[1].row == center_mvs[1].row &&
                 best_mv_ref[1].col == center_mvs[1].col)));
}

#if CONFIG_TIP_REF_PRED_MERGING
// This function consolidates the refinemv enabling check for both TIP ref mode
// blocks and non-TIP ref mode blocks.
static AOM_INLINE int is_sub_block_refinemv_enabled(const AV1_COMMON *cm,
                                                    const MB_MODE_INFO *mi,
                                                    int build_for_obmc,
                                                    int plane,
                                                    int tip_ref_frame) {
  if (tip_ref_frame) {
    return (plane == 0);
  } else {
    int apply_sub_block_refinemv =
        mi->refinemv_flag && (!build_for_obmc) &&
#if CONFIG_AFFINE_REFINEMENT
        (is_damr_allowed_with_refinemv(mi->mode) ||
         mi->comp_refine_type < COMP_AFFINE_REFINE_START) &&
#endif  // CONFIG_AFFINE_REFINEMENT
        !is_tip_ref_frame(mi->ref_frame[0]);

#if CONFIG_AFFINE_REFINEMENT
    if (apply_sub_block_refinemv && default_refinemv_modes(cm, mi))
#else
    if (apply_sub_block_refinemv && default_refinemv_modes(mi))
#endif  // CONFIG_AFFINE_REFINEMENT
      apply_sub_block_refinemv &=
          (mi->comp_group_idx == 0 &&
           mi->interinter_comp.type == COMPOUND_AVERAGE);
    return apply_sub_block_refinemv;
  }
}

// check if the refinemv mode is allowed for a given block
static INLINE int is_mv_refine_allowed(const AV1_COMMON *cm,
                                       const MB_MODE_INFO *mbmi, int plane) {
  if (plane != 0) return 0;
  if (is_tip_ref_frame(mbmi->ref_frame[0]))
    return is_refinemv_allowed_tip_blocks(cm, mbmi);
  return 1;
}

// Check if the optical flow MV refinement is enabled for a given block.
static AOM_INLINE int is_optflow_refinement_enabled(const AV1_COMMON *cm,
                                                    const MB_MODE_INFO *mi,
                                                    int plane,
                                                    int tip_ref_frame) {
  if (tip_ref_frame) {
    return (opfl_allowed_for_cur_refs(cm, mi) && plane == 0);
  } else {
    return (opfl_allowed_for_cur_block(cm, mi));
  }
}

// Calculate the SAD of 2 compound prediction blocks and use it to decide
// whether or not to skip the optical flow MV refinement for the TIP block.
static AOM_INLINE int skip_opfl_refine_with_tip(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, int bw, int bh,
    int pu_width, int pu_height, int mi_x, int mi_y, uint16_t **mc_buf,
    MV best_mv_ref[2], CalcSubpelParamsFunc calc_subpel_params_func,
    uint16_t *dst0, uint16_t *dst1) {
  MB_MODE_INFO mbmi;
  memset(&mbmi, 0, sizeof(mbmi));
  mbmi.mv[0].as_mv = best_mv_ref[0];
  mbmi.mv[1].as_mv = best_mv_ref[1];
  mbmi.ref_frame[0] = TIP_FRAME;
  mbmi.ref_frame[1] = NONE_FRAME;
#if CONFIG_TIP_DIRECT_FRAME_MV
  mbmi.interp_fltr = cm->tip_interp_filter;
#else
  mbmi.interp_fltr = EIGHTTAP_REGULAR;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
  mbmi.use_intrabc[xd->tree_type == CHROMA_PART] = 0;
  mbmi.use_intrabc[0] = 0;
  mbmi.mode = NEWMV;
  mbmi.motion_mode = SIMPLE_TRANSLATION;
  mbmi.sb_type[PLANE_TYPE_Y] = BLOCK_8X8;
  mbmi.interinter_comp.type = COMPOUND_AVERAGE;
  mbmi.max_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
  mbmi.pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
#if CONFIG_AFFINE_REFINEMENT
  mbmi.comp_refine_type = COMP_REFINE_SUBBLK2P;
#endif

  InterPredParams params0, params1;
  av1_opfl_build_inter_predictor(cm, xd, plane, &mbmi, bw, bh, mi_x, mi_y,
                                 mc_buf, &params0, calc_subpel_params_func, 0,
                                 dst0
#if CONFIG_REFINEMV
                                 ,
                                 &best_mv_ref[0], pu_width, pu_height
#endif  // CONFIG_REFINEMV
  );
  av1_opfl_build_inter_predictor(cm, xd, plane, &mbmi, bw, bh, mi_x, mi_y,
                                 mc_buf, &params1, calc_subpel_params_func, 1,
                                 dst1
#if CONFIG_REFINEMV
                                 ,
                                 &best_mv_ref[1], pu_width, pu_height
#endif  // CONFIG_REFINEMV

  );
  const int bd = cm->seq_params.bit_depth;
  const unsigned int sad_thres =
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT ? 15 : 6;

  const unsigned int sad = get_highbd_sad(dst0, bw, dst1, bw, bd, 8, 8);

  return (sad < sad_thres);
}
#endif  // CONFIG_TIP_REF_PRED_MERGING

static void build_inter_predictors_8x8_and_bigger_refinemv(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    int build_for_obmc, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
#if CONFIG_TIP_REF_PRED_MERGING
    MV mi_mv[2],
#endif  // CONFIG_TIP_REF_PRED_MERGING
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst, int dst_stride,
#if CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
    int subblk_start_x, int subblk_start_y,
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
    int pu_width, int pu_height, uint16_t *dst0_16_refinemv,
    uint16_t *dst1_16_refinemv, int row_start, int col_start, MV *sb_refined_mv,
    MV *chroma_refined_mv, int build_for_refine_mv_only,
    ReferenceArea ref_area[2]
#if CONFIG_TIP_REF_PRED_MERGING
    ,
    int_mv *mv_refined
#endif  // CONFIG_TIP_REF_PRED_MERGING
) {
#if CONFIG_TIP_REF_PRED_MERGING
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  const int is_compound = has_second_ref(mi) || tip_ref_frame;
#else
    const int is_compound = has_second_ref(mi);
#endif  // CONFIG_TIP_REF_PRED_MERGING
  struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_TIP_REF_PRED_MERGING
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
#endif
  assert(!is_intrabc_block(mi, xd->tree_type));
  assert(is_compound);
#if CONFIG_BAWP_CHROMA
  assert(!mi->bawp_flag[0]);
#else
    assert(!mi->bawp_flag);
#endif  // CONFIG_BAWP_CHROMA
  assert(!build_for_obmc);
  assert(!is_masked_compound_type(mi->interinter_comp.type));
#if !CONFIG_TIP_REF_PRED_MERGING
  assert(!is_tip_ref_frame(mi->ref_frame[0]));
#endif  // !CONFIG_TIP_REF_PRED_MERGING

  assert(mi->cwp_idx == CWP_EQUAL);

  int is_global[2] = { 0, 0 };
#if CONFIG_TIP_REF_PRED_MERGING
  if (!tip_ref_frame) {
#endif
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if !CONFIG_TIP_REF_PRED_MERGING
      if (!is_tip_ref_frame(mi->ref_frame[ref])) {
#endif
        const WarpedMotionParams *const wm =
            &xd->global_motion[mi->ref_frame[ref]];
        is_global[ref] = is_global_mv_block(mi, wm->wmtype);
#if !CONFIG_TIP_REF_PRED_MERGING
      }
#endif
    }
#if CONFIG_TIP_REF_PRED_MERGING
  }
#endif

  assert(!is_global[0] && !is_global[1]);

  const int pre_x = (mi_x + MI_SIZE * col_start) >> pd->subsampling_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> pd->subsampling_y;

#if CONFIG_TIP_REF_PRED_MERGING
  int apply_refinemv = is_mv_refine_allowed(cm, mi, plane);

  MV best_mv_ref[2] = { mi_mv[0], mi_mv[1] };
#else
    int apply_refinemv = (plane == 0);

    MV best_mv_ref[2] = { { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
                          { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col } };
#endif  // CONFIG_TIP_REF_PRED_MERGING
  if (apply_refinemv) {
    uint16_t *dst_ref0 = NULL, *dst_ref1 = NULL;
    dst_ref0 = &dst0_16_refinemv[0];
    dst_ref1 = &dst1_16_refinemv[0];

#if !CONFIG_TIP_REF_PRED_MERGING
    assert(IMPLIES(!mi->skip_mode,
                   is_refinemv_allowed(cm, mi, mi->sb_type[PLANE_TYPE_Y])));
    assert(IMPLIES(mi->skip_mode, is_refinemv_allowed_skip_mode(cm, mi)));
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    apply_mv_refinement(cm, xd, plane, mi, bw, bh, mi_x, mi_y, mc_buf,
#if CONFIG_TIP_REF_PRED_MERGING
                        mi_mv,
#endif  // CONFIG_TIP_REF_PRED_MERGING
                        calc_subpel_params_func, pre_x, pre_y, dst_ref0,
                        dst_ref1, best_mv_ref, pu_width, pu_height);
    if (sb_refined_mv) {
      // store the DMVR refined MV so that chroma can use it
      sb_refined_mv[0] = best_mv_ref[0];
      sb_refined_mv[1] = best_mv_ref[1];
    }
    assert(IMPLIES(plane, !build_for_refine_mv_only));
    // if build_for_refine_mv_only is non-zero, we build only to get the
    // refinemv values The actual prediction values are not necessary
    if (build_for_refine_mv_only) {
      return;
    }
  }
#if CONFIG_TIP_REF_PRED_MERGING
  else if (!tip_ref_frame) {
#else
    else {
#endif  // CONFIG_TIP_REF_PRED_MERGING
    best_mv_ref[0] = chroma_refined_mv[0];
    best_mv_ref[1] = chroma_refined_mv[1];
  }

#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_TIP_REF_PRED_MERGING
  if (tip_ref_frame) {
    mv_refined[0].as_mv.row = best_mv_ref[0].row * (1 << (1 - ss_y));
    mv_refined[0].as_mv.col = best_mv_ref[0].col * (1 << (1 - ss_x));
    mv_refined[1].as_mv.row = best_mv_ref[1].row * (1 << (1 - ss_y));
    mv_refined[1].as_mv.col = best_mv_ref[1].col * (1 << (1 - ss_x));
  }
  int use_optflow_refinement =
      is_optflow_refinement_enabled(cm, mi, plane, tip_ref_frame);
#else
  int_mv mv_refined[2 * N_OF_OFFSETS];
  memset(mv_refined, 0, 2 * N_OF_OFFSETS * sizeof(int_mv));
  const int use_optflow_refinement = opfl_allowed_for_cur_block(cm, mi);
#endif  // CONFIG_TIP_REF_PRED_MERGING
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));
  assert(IMPLIES(use_optflow_refinement, !build_for_obmc));

  // Optical flow refinement with masked comp types or with non-sharp
  // interpolation filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
#if CONFIG_TIP_REF_PRED_MERGING
  assert(IMPLIES(use_optflow_refinement && tip_ref_frame, plane == 0));
#else
  assert(IMPLIES(use_optflow_refinement && mi->interp_fltr != MULTITAP_SHARP,
                 cm->features.opfl_refine_type == REFINE_ALL));
#endif  // CONFIG_TIP_REF_PRED_MERGING

#if CONFIG_TIP_REF_PRED_MERGING
  int use_4x4 = tip_ref_frame ? 0 : 1;
#endif
  int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                                 ,
                                 use_4x4
#else
                                 ,
                                 1
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
  );
  const int n_blocks = (bw / n) * (bh / n);

#if CONFIG_AFFINE_REFINEMENT
  int do_affine = is_damr_allowed_with_refinemv(mi->mode) &&
                  mi->comp_refine_type >= COMP_AFFINE_REFINE_START;
  int use_affine_opfl = do_affine;
  WarpedMotionParams wms[2];
  wms[0] = wms[1] = default_warp_params;
#if AFFINE_CHROMA_REFINE_METHOD > 0
  if (use_optflow_refinement && do_affine && plane) {
    memcpy(wms, mi->wm_params, 2 * sizeof(wms[0]));
  }
#endif
#endif  // CONFIG_AFFINE_REFINEMENT

  if (use_optflow_refinement && plane == 0) {
    // Pointers to hold optical flow MV offsets.
    int *vx0 = xd->opfl_vxy_bufs;
    int *vx1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 1);
    int *vy0 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 2);
    int *vy1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 3);

    // Pointers to hold gradient and dst buffers.
    int16_t *gx0 = xd->opfl_gxy_bufs;
    int16_t *gx1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 1);
    int16_t *gy0 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 2);
    int16_t *gy1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 3);

    // Initialize refined mv
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];

#if !CONFIG_TIP_REF_PRED_MERGING
    for (int mvi = 0; mvi < n_blocks; mvi++) {
      mv_refined[mvi * 2].as_mv = mv0;
      mv_refined[mvi * 2 + 1].as_mv = mv1;
    }
#endif
    // Refine MV using optical flow. The final output MV will be in 1/16
    // precision.
    uint16_t *dst0 = xd->opfl_dst_bufs;
    uint16_t *dst1 = xd->opfl_dst_bufs + MAX_SB_SQUARE;
#if CONFIG_TIP_REF_PRED_MERGING
    if (tip_ref_frame) {
      use_optflow_refinement = !skip_opfl_refine_with_tip(
          cm, xd, plane, bw, bh, pu_width, pu_height, mi_x, mi_y, mc_buf,
          best_mv_ref, calc_subpel_params_func, dst0, dst1);
    }
    int do_pred = tip_ref_frame ? 0 : 1;
    if (use_optflow_refinement) {
      for (int mvi = 0; mvi < n_blocks; mvi++) {
        mv_refined[mvi * 2].as_mv = mv0;
        mv_refined[mvi * 2 + 1].as_mv = mv1;
      }
#endif
      av1_get_optflow_based_mv(cm, xd, plane, mi, mv_refined, bw, bh, mi_x,
                               mi_y, mc_buf, calc_subpel_params_func, gx0, gy0,
                               gx1, gy1,
#if CONFIG_AFFINE_REFINEMENT
                               do_affine ? wms : NULL, &use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
                               vx0, vy0, vx1, vy1, dst0, dst1,
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                               use_4x4, do_pred,
#else
                               1, 1,
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
                               best_mv_ref, pu_width, pu_height);
#if CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
      const int mvi_stride = pu_width / n;
      const int subblk_rows = bh / n;
      const int subblk_cols = bw / n;
      const int cur_subblk_idx =
          (subblk_start_y / n) * mvi_stride + (subblk_start_x / n);
      for (int i = 0; i < subblk_rows; i++) {
        for (int j = 0; j < subblk_cols; j++) {
          int mvi_idx = cur_subblk_idx + i * mvi_stride + j;
          int mv_delta_idx = i * subblk_cols + j;
          xd->mv_delta[mvi_idx].mv[0].as_mv.row = vy0[mv_delta_idx];
          xd->mv_delta[mvi_idx].mv[0].as_mv.col = vx0[mv_delta_idx];
          xd->mv_delta[mvi_idx].mv[1].as_mv.row = vy1[mv_delta_idx];
          xd->mv_delta[mvi_idx].mv[1].as_mv.col = vx1[mv_delta_idx];
        }
      }
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
#if CONFIG_AFFINE_REFINEMENT
      mi->wm_params[0] = wms[0];
      mi->wm_params[1] = wms[1];
#endif  // CONFIG_AFFINE_REFINEMENT
#if CONFIG_TIP_REF_PRED_MERGING
    }
#endif  // CONFIG_TIP_REF_PRED_MERGING
  }
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
#if CONFIG_TIP_REF_PRED_MERGING
  uint8_t use_bacp = tip_ref_frame ? cm->features.enable_imp_msk_bld
                                   : !build_for_obmc &&
                                         use_border_aware_compound(cm, mi) &&
                                         mi->cwp_idx == CWP_EQUAL &&
                                         cm->features.enable_imp_msk_bld;
#else
  uint8_t use_bacp = !build_for_obmc && use_border_aware_compound(cm, mi) &&
                     mi->cwp_idx == CWP_EQUAL &&
                     cm->features.enable_imp_msk_bld;
#endif  // CONFIG_TIP_REF_PRED_MERGING
#endif  // CONFIG_D071_IMP_MSK_BLD

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_TIP_REF_PRED_MERGING
    const struct scale_factors *const sf =
        tip_ref_frame ? cm->tip_ref.ref_scale_factor[ref]
                      : xd->block_ref_scale_factors[ref];
#else
      const struct scale_factors *const sf = xd->block_ref_scale_factors[ref];
#endif  // CONFIG_TIP_REF_PRED_MERGING
    struct buf_2d *const pre_buf = &pd->pre[ref];

    const MV mv = best_mv_ref[ref];
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };
    InterPredParams inter_pred_params;
#if CONFIG_TIP_REF_PRED_MERGING
    const int comp_bw = tip_ref_frame ? (bw >> ss_x) : bw;
    const int comp_bh = tip_ref_frame ? (bh >> ss_y) : bh;

    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
#if CONFIG_REFINEMV
    const int use_ref_padding = tip_ref_frame ? apply_refinemv : 1;
    if (use_ref_padding) {
      inter_pred_params.use_ref_padding = 1;
      inter_pred_params.ref_area = &ref_area[ref];
    }
#endif  // CONFIG_REFINEMV
#else
      av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                            pd->subsampling_x, pd->subsampling_y, xd->bd,
                            mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);

#if CONFIG_REFINEMV
      inter_pred_params.use_ref_padding = 1;
      inter_pred_params.ref_area = &ref_area[ref];
#endif  // CONFIG_REFINEMV
#endif  // CONFIG_TIP_REF_PRED_MERGING

    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;

    if (is_compound) av1_init_comp_mode(&inter_pred_params);
#if CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point to the first ref
#endif                        // CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    if (!build_for_obmc) {
      av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);
      assert(inter_pred_params.mode != WARP_PRED);
    }

#if CONFIG_D071_IMP_MSK_BLD
    if (is_compound) {
#if CONFIG_TIP_REF_PRED_MERGING
      inter_pred_params.sb_type =
          tip_ref_frame ? BLOCK_8X8 : mi->sb_type[PLANE_TYPE_Y];
#else
      inter_pred_params.sb_type = mi->sb_type[PLANE_TYPE_Y];
#endif  // CONFIG_TIP_REF_PRED_MERGING
      inter_pred_params.mask_comp = mi->interinter_comp;
    }
#endif  // CONFIG_D071_IMP_MSK_BLD

#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_AFFINE_REFINEMENT
    if (use_optflow_refinement && (do_affine || plane == 0)) {
#else
    if (use_optflow_refinement && plane == 0) {
#endif  // CONFIG_AFFINE_REFINEMENT
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);

      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);

      av1_opfl_rebuild_inter_predictor(dst, dst_stride, plane, mv_refined,
                                       &inter_pred_params, xd, mi_x, mi_y,
#if CONFIG_AFFINE_REFINEMENT
                                       cm, pu_width, mi->comp_refine_type,
                                       do_affine ? wms : NULL, &mi->mv[ref],
                                       use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
                                       ref, mc_buf, calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                                       ,
                                       use_4x4
#else
                                       ,
                                       1
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
      continue;
    }
#endif  // CONFIG_OPTFLOW_REFINEMENT
    av1_build_one_inter_predictor(dst, dst_stride, &mv, &inter_pred_params, xd,
                                  mi_x, mi_y, ref, mc_buf,
                                  calc_subpel_params_func);
  }

#if CONFIG_AFFINE_REFINEMENT
  const int apply_pef_opfl =
      (mi->comp_refine_type == COMP_REFINE_SUBBLK2P && plane == 0) ||
      (damr_refine_subblock(plane, bw, bh, mi->comp_refine_type, n) &&
       do_affine);
#endif  // CONFIG_AFFINE_REFINEMENT
#if CONFIG_TIP_REF_PRED_MERGING
  if (use_optflow_refinement && plane == 0 && !tip_ref_frame) {
#else
    if (use_optflow_refinement && plane == 0) {
#endif  // CONFIG_TIP_REF_PRED_MERGING
    enhance_prediction(cm, xd, plane, dst, dst_stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                       ,
                       mv_refined,
                       use_optflow_refinement
#if CONFIG_AFFINE_REFINEMENT
                           && apply_pef_opfl
#endif  // CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
                       ,
                       0, NULL
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
                       ,
                       false
#endif  // CONFIG_EXT_WARP_FILTER
    );
  }
}
#endif  // CONFIG_REFINEMV

static void build_inter_predictors_8x8_and_bigger(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
#if CONFIG_BAWP
    const BUFFER_SET *dst_orig,
#endif  // CONFIG_BAWP
    int build_for_obmc, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf
#if CONFIG_TIP_REF_PRED_MERGING
    ,
    MV mi_mv[2], CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst,
    int dst_stride, int pu_width, int pu_height
#else
    ,
    CalcSubpelParamsFunc calc_subpel_params_func
#endif  // CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_REFINEMV
    ,
    int build_for_refine_mv_only
#endif  // CONFIG_REFINEMV
#if CONFIG_TIP_REF_PRED_MERGING
    ,
    int_mv *mv_refined, bool *ext_warp_used
#endif  // CONFIG_TIP_REF_PRED_MERGING
) {
#if CONFIG_TIP_REF_PRED_MERGING
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  const int is_compound = has_second_ref(mi) || tip_ref_frame;
  if (tip_ref_frame) mi->comp_refine_type = COMP_REFINE_SUBBLK2P;
#else
  const int is_compound = has_second_ref(mi);
#endif  // CONFIG_TIP_REF_PRED_MERGING
  const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
  assert(IMPLIES(is_intrabc, !is_compound));
  struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_TIP_REF_PRED_MERGING
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
#else
  struct buf_2d *const dst_buf = &pd->dst;
  uint16_t *const dst = dst_buf->buf;
#endif  // CONFIG_TIP_REF_PRED_MERGING

#if CONFIG_REFINEMV
  assert(IMPLIES(mi->refinemv_flag, !is_intrabc));
  assert(IMPLIES(mi->refinemv_flag && !build_for_obmc, is_compound));
  assert(IMPLIES(
      !build_for_obmc && mi->refinemv_flag && switchable_refinemv_flag(cm, mi),
      mi->interinter_comp.type == COMPOUND_AVERAGE));
#if CONFIG_BAWP_CHROMA
  assert(IMPLIES(mi->refinemv_flag, mi->bawp_flag[0] == 0));
#else
    assert(IMPLIES(mi->refinemv_flag, mi->bawp_flag == 0));
#endif  // CONFIG_BAWP_CHROMA
  assert(IMPLIES(mi->refinemv_flag, mi->interp_fltr == MULTITAP_SHARP));

#if CONFIG_TIP_REF_PRED_MERGING
  assert(IMPLIES(tip_ref_frame,
                 mi->use_intrabc[0] == 0 && mi->use_intrabc[1] == 0));
  assert(IMPLIES(tip_ref_frame, mi->motion_mode == SIMPLE_TRANSLATION));
  assert(IMPLIES(tip_ref_frame, mi->interinter_comp.type == COMPOUND_AVERAGE));
#endif

#if CONFIG_TIP_REF_PRED_MERGING
  if (is_sub_block_refinemv_enabled(cm, mi, build_for_obmc, plane,
                                    tip_ref_frame)) {
#else
    int apply_sub_block_refinemv =
        mi->refinemv_flag && (!build_for_obmc) &&
#if CONFIG_AFFINE_REFINEMENT
        (is_damr_allowed_with_refinemv(mi->mode) ||
         mi->comp_refine_type < COMP_AFFINE_REFINE_START) &&
#endif  // CONFIG_AFFINE_REFINEMENT
        !is_tip_ref_frame(mi->ref_frame[0]);

#if CONFIG_AFFINE_REFINEMENT
    if (apply_sub_block_refinemv && default_refinemv_modes(cm, mi))
#else
    if (apply_sub_block_refinemv && default_refinemv_modes(mi))
#endif  // CONFIG_AFFINE_REFINEMENT
      apply_sub_block_refinemv &=
          (mi->comp_group_idx == 0 &&
           mi->interinter_comp.type == COMPOUND_AVERAGE);

    if (apply_sub_block_refinemv) {
#endif  // CONFIG_TIP_REF_PRED_MERGING
    assert(IMPLIES(mi->refinemv_flag, mi->cwp_idx == CWP_EQUAL));
#if CONFIG_TIP_REF_PRED_MERGING
    assert(IMPLIES(tip_ref_frame, plane == 0));
#endif  // CONFIG_TIP_REF_PRED_MERGING
    int refinemv_sb_size_width =
        AOMMIN((REFINEMV_SUBBLOCK_WIDTH >> pd->subsampling_x), bw);
    int refinemv_sb_size_height =
        AOMMIN(REFINEMV_SUBBLOCK_HEIGHT >> pd->subsampling_y, bh);
#if CONFIG_SUBBLK_REF_EXT
    uint16_t
        dst0_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * SUBBLK_REF_EXT_LINES) *
                         (REFINEMV_SUBBLOCK_HEIGHT + 2 * SUBBLK_REF_EXT_LINES)];
    uint16_t
        dst1_16_refinemv[(REFINEMV_SUBBLOCK_WIDTH + 2 * SUBBLK_REF_EXT_LINES) *
                         (REFINEMV_SUBBLOCK_HEIGHT + 2 * SUBBLK_REF_EXT_LINES)];
#else
      uint16_t
          dst0_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
      uint16_t
          dst1_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
#endif  // CONFIG_SUBBLK_REF_EXT

    ReferenceArea ref_area[2];
#if !CONFIG_SUBBLK_PAD
#if CONFIG_TIP_REF_PRED_MERGING
    av1_get_reference_area_with_padding(cm, xd, plane, mi, mi_mv, bw, bh, mi_x,
                                        mi_y, ref_area, pu_width, pu_height);
#else
    av1_get_reference_area_with_padding(cm, xd, plane, mi, bw, bh, mi_x, mi_y,
                                        ref_area, 0, 0);
#endif  // CONFIG_TIP_REF_PRED_MERGING
#endif  //! CONFIG_SUBBLK_PAD
#if !CONFIG_TIP_REF_PRED_MERGING
    int dst_stride = dst_buf->stride;
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    CONV_BUF_TYPE *tmp_conv_dst = xd->tmp_conv_dst;
    assert(bw % refinemv_sb_size_width == 0);
    assert(bh % refinemv_sb_size_height == 0);
    for (int h = 0; h < bh; h += refinemv_sb_size_height) {
      for (int w = 0; w < bw; w += refinemv_sb_size_width) {
#if CONFIG_TIP_REF_PRED_MERGING
        uint16_t *dst_buf = dst + h * dst_stride + w;
#else
          dst_buf->buf = dst + h * dst_stride + w;
#endif  // CONFIG_TIP_REF_PRED_MERGING
        xd->tmp_conv_dst = tmp_conv_dst + h * MAX_SB_SIZE + w;

        const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
        const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
        int row_start =
            plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
        int col_start =
            plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;
        MV luma_refined_mv[2] = { { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
                                  { mi->mv[1].as_mv.row,
                                    mi->mv[1].as_mv.col } };

        MV chroma_refined_mv[2] = {
          { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
          { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col }
        };

        if (plane != 0) {
          int luma_h = (h << pd->subsampling_y);
          int luma_w = (w << pd->subsampling_x);
          REFINEMV_SUBMB_INFO *refinemv_subinfo =
              &xd->refinemv_subinfo[(luma_h >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                    (luma_w >> MI_SIZE_LOG2)];
          chroma_refined_mv[0] = refinemv_subinfo->refinemv[0].as_mv;
          chroma_refined_mv[1] = refinemv_subinfo->refinemv[1].as_mv;
        }
#if CONFIG_SUBBLK_PAD
        // sub_mi_x, and sub_mi_y are the top-left position of the luma samples
        // of the sub-block
        const int sub_mi_x = mi_x + w * (1 << pd->subsampling_x);
        const int sub_mi_y = mi_y + h * (1 << pd->subsampling_y);
#if CONFIG_TIP_REF_PRED_MERGING
        av1_get_reference_area_with_padding(
            cm, xd, plane, mi, mi_mv, refinemv_sb_size_width,
            refinemv_sb_size_height, sub_mi_x, sub_mi_y, ref_area, pu_width,
            pu_height);
#else
        av1_get_reference_area_with_padding(
            cm, xd, plane, mi, refinemv_sb_size_width, refinemv_sb_size_height,
            sub_mi_x, sub_mi_y, ref_area, 0, 0);
#endif  // CONFIG_TIP_REF_PRED_MERGING
#endif  // CONFIG_SUBBLK_PAD
#if CONFIG_TIP_REF_PRED_MERGING
        // mi_x, and mi_y are the top-left position of the luma samples of the
        // sub-block
        build_inter_predictors_8x8_and_bigger_refinemv(
            cm, xd, plane, mi, build_for_obmc, refinemv_sb_size_width,
            refinemv_sb_size_height, mi_x + w * (1 << pd->subsampling_x),
            mi_y + h * (1 << pd->subsampling_y), mc_buf, mi_mv,
            calc_subpel_params_func, dst_buf, dst_stride,
#if CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
            w, h,
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
            pu_width, pu_height, dst0_16_refinemv, dst1_16_refinemv, row_start,
            col_start, plane == 0 ? luma_refined_mv : NULL, chroma_refined_mv,
            build_for_refine_mv_only, ref_area, mv_refined);

        if (plane == 0 && !tip_ref_frame) {
#else
          // mi_x, and mi_y are the top-left position of the luma samples of the
          // sub-block
          build_inter_predictors_8x8_and_bigger_refinemv(
              cm, xd, plane, mi, build_for_obmc, refinemv_sb_size_width,
              refinemv_sb_size_height, mi_x + w * (1 << pd->subsampling_x),
              mi_y + h * (1 << pd->subsampling_y), mc_buf,
              calc_subpel_params_func, dst_buf->buf, dst_stride,
#if CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
              w, h,
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
              bw, bh, dst0_16_refinemv, dst1_16_refinemv, row_start, col_start,
              plane == 0 ? luma_refined_mv : NULL, chroma_refined_mv,
              build_for_refine_mv_only, ref_area);

          if (plane == 0) {
#endif  // CONFIG_TIP_REF_PRED_MERGING
          REFINEMV_SUBMB_INFO *refinemv_subinfo =
              &xd->refinemv_subinfo[(h >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                    (w >> MI_SIZE_LOG2)];
          fill_subblock_refine_mv(refinemv_subinfo, refinemv_sb_size_width,
                                  refinemv_sb_size_height, luma_refined_mv[0],
                                  luma_refined_mv[1]);
        }
      }
    }

#if !CONFIG_TIP_REF_PRED_MERGING
    enhance_prediction(cm, xd, plane, dst, dst_stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                       ,
                       NULL, 0
#endif  // CONFIG_OPTFLOW_REFINEMENT
                       ,
                       apply_sub_block_refinemv, &xd->refinemv_subinfo[0]
#if CONFIG_EXT_WARP_FILTER
                       ,
                       false
#endif  // CONFIG_EXT_WARP_FILTER
    );
    dst_buf->buf = dst;
#endif  // !CONFIG_TIP_REF_PRED_MERGING
    xd->tmp_conv_dst = tmp_conv_dst;
    return;
  }
#endif  // CONFIG_REFINEMV

  int is_global[2] = { 0, 0 };
#if CONFIG_TIP_REF_PRED_MERGING
  if (!tip_ref_frame) {
#endif
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if !CONFIG_TIP_REF_PRED_MERGING
      if (!is_tip_ref_frame(mi->ref_frame[ref])) {
#endif
        const WarpedMotionParams *const wm =
            &xd->global_motion[mi->ref_frame[ref]];
        is_global[ref] = is_global_mv_block(mi, wm->wmtype);
#if !CONFIG_TIP_REF_PRED_MERGING
      }
#endif
    }
#if CONFIG_TIP_REF_PRED_MERGING
  }
#endif

  int row_start = 0;
  int col_start = 0;
  if (!build_for_obmc) {
    const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
    const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
    row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
    col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;
  }
  const int pre_x = (mi_x + MI_SIZE * col_start) >> pd->subsampling_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> pd->subsampling_y;
#if CONFIG_REFINEMV
#if CONFIG_TIP_REF_PRED_MERGING
  MV best_mv_ref[2] = { mi_mv[0], mi_mv[1] };
#else
    MV best_mv_ref[2] = { { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
                          { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col } };
#endif  // CONFIG_TIP_REF_PRED_MERGING
#endif  // CONFIG_REFINEMV
#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_TIP_REF_PRED_MERGING
  if (tip_ref_frame) {
    mv_refined[0].as_mv.row = best_mv_ref[0].row * (1 << (1 - ss_y));
    mv_refined[0].as_mv.col = best_mv_ref[0].col * (1 << (1 - ss_x));
    mv_refined[1].as_mv.row = best_mv_ref[1].row * (1 << (1 - ss_y));
    mv_refined[1].as_mv.col = best_mv_ref[1].col * (1 << (1 - ss_x));
  }
  int use_optflow_refinement =
      is_optflow_refinement_enabled(cm, mi, plane, tip_ref_frame);
  int use_4x4 = tip_ref_frame ? 0 : 1;
#else
    int_mv mv_refined[2 * N_OF_OFFSETS];
    memset(mv_refined, 0, 2 * N_OF_OFFSETS * sizeof(int_mv));
    const int use_optflow_refinement = opfl_allowed_for_cur_block(cm, mi);
#endif  // CONFIG_TIP_REF_PRED_MERGING
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));
  assert(IMPLIES(use_optflow_refinement, !build_for_obmc));

  // Optical flow refinement with masked comp types or with non-sharp
  // interpolation filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
#if !CONFIG_TIP_REF_PRED_MERGING
  assert(IMPLIES(use_optflow_refinement && mi->interp_fltr != MULTITAP_SHARP,
                 cm->features.opfl_refine_type == REFINE_ALL));
#else
    assert(IMPLIES(use_optflow_refinement && tip_ref_frame, plane == 0));
#endif

#if CONFIG_AFFINE_REFINEMENT
  int use_affine_opfl = mi->comp_refine_type >= COMP_AFFINE_REFINE_START;
#if CONFIG_AFFINE_REFINEMENT_SB
  WarpedMotionParams wms[2 * NUM_AFFINE_PARAMS];
  for (int i = 0; i < 2 * NUM_AFFINE_PARAMS; i++) wms[i] = default_warp_params;
#if AFFINE_CHROMA_REFINE_METHOD > 0
  if (use_optflow_refinement && plane)
    memcpy(wms, xd->wm_params_sb, 2 * NUM_AFFINE_PARAMS * sizeof(wms[0]));
#endif
#else
  WarpedMotionParams wms[2];
  wms[0] = default_warp_params;
  wms[1] = default_warp_params;
#if AFFINE_CHROMA_REFINE_METHOD > 0
  if (use_optflow_refinement && plane) {
    wms[0] = mi->wm_params[0];
    wms[1] = mi->wm_params[1];
  }
#endif
#endif  // CONFIG_AFFINE_REFINEMENT_SB
#endif  // CONFIG_AFFINE_REFINEMENT

  // Pointers to gradient and dst buffers

  if (use_optflow_refinement && plane == 0) {
    // Pointers to hold optical flow MV offsets.
    int *vx0 = xd->opfl_vxy_bufs;
    int *vx1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 1);
    int *vy0 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 2);
    int *vy1 = xd->opfl_vxy_bufs + (N_OF_OFFSETS * 3);

#if CONFIG_AFFINE_REFINEMENT
    assert(mi->comp_refine_type > COMP_REFINE_NONE);
    assert(IMPLIES(mi->comp_refine_type >= COMP_AFFINE_REFINE_START,
                   is_affine_refinement_allowed(cm, xd, mi->mode)));
#endif  // CONFIG_AFFINE_REFINEMENT
    // Allocate gradient and dst buffers
    const int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                                         ,
                                         use_4x4
#else
                                         ,
                                         1
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
    );
    const int n_blocks = (bw / n) * (bh / n);
    int16_t *gx0 = xd->opfl_gxy_bufs;
    int16_t *gx1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 1);
    int16_t *gy0 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 2);
    int16_t *gy1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 3);

    // Initialize refined mv
#if CONFIG_REFINEMV
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];
#else
      const MV mv0 = mi->mv[0].as_mv;
      const MV mv1 = mi->mv[1].as_mv;
#endif  // CONFIG_REFINEMV
#if !CONFIG_TIP_REF_PRED_MERGING
    for (int mvi = 0; mvi < n_blocks; mvi++) {
      mv_refined[mvi * 2].as_mv = mv0;
      mv_refined[mvi * 2 + 1].as_mv = mv1;
    }
#endif
    // Refine MV using optical flow. The final output MV will be in 1/16
    // precision.
    uint16_t *dst0 = xd->opfl_dst_bufs;
    uint16_t *dst1 = xd->opfl_dst_bufs + MAX_SB_SQUARE;

#if CONFIG_TIP_REF_PRED_MERGING
    if (tip_ref_frame) {
      use_optflow_refinement = !skip_opfl_refine_with_tip(
          cm, xd, plane, bw, bh, pu_width, pu_height, mi_x, mi_y, mc_buf,
          best_mv_ref, calc_subpel_params_func, dst0, dst1);
    }
    if (use_optflow_refinement) {
      int do_pred = tip_ref_frame ? 0 : 1;
      for (int mvi = 0; mvi < n_blocks; mvi++) {
        mv_refined[mvi * 2].as_mv = mv0;
        mv_refined[mvi * 2 + 1].as_mv = mv1;
      }
#endif
      av1_get_optflow_based_mv(cm, xd, plane, mi, mv_refined, bw, bh, mi_x,
                               mi_y, mc_buf, calc_subpel_params_func, gx0, gy0,
                               gx1, gy1,
#if CONFIG_AFFINE_REFINEMENT
                               wms, &use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
                               vx0, vy0, vx1, vy1, dst0, dst1
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                               ,
                               use_4x4, do_pred
#else
                               ,
                               1, 1
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
#if CONFIG_REFINEMV
                               ,
                               best_mv_ref, bw, bh
#endif  // CONFIG_REFINEMV
      );
#if CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
      for (int mvi = 0; mvi < n_blocks; mvi++) {
        xd->mv_delta[mvi].mv[0].as_mv.row = vy0[mvi];
        xd->mv_delta[mvi].mv[0].as_mv.col = vx0[mvi];
        xd->mv_delta[mvi].mv[1].as_mv.row = vy1[mvi];
        xd->mv_delta[mvi].mv[1].as_mv.col = vx1[mvi];
      }

      // TODO(any): The memset is required as the MV delta offsets of optical
      // flow refinement stored in 'mv_delta' buffer is accessed beyond n_blocks
      // when 'AFFINE_CHROMA_REFINE_METHOD' is enabled. Recheck if access beyond
      // n_blocks of 'mv_delta' buffer is valid.
      for (int mvi = n_blocks; mvi < N_OF_OFFSETS; mvi++) {
        xd->mv_delta[mvi].mv[0].as_mv.row = 0;
        xd->mv_delta[mvi].mv[0].as_mv.col = 0;
        xd->mv_delta[mvi].mv[1].as_mv.row = 0;
        xd->mv_delta[mvi].mv[1].as_mv.col = 0;
      }
#endif  // CONFIG_AFFINE_REFINEMENT || CONFIG_REFINED_MVS_IN_TMVP
#if CONFIG_AFFINE_REFINEMENT_SB
      memcpy(xd->wm_params_sb, wms, 2 * NUM_AFFINE_PARAMS * sizeof(wms[0]));
#elif CONFIG_AFFINE_REFINEMENT
      // parameters derived are saved here and may be reused by chroma
      mi->wm_params[0] = wms[0];
      mi->wm_params[1] = wms[1];
#endif  // CONFIG_AFFINE_REFINEMENT_SB
#if CONFIG_TIP_REF_PRED_MERGING
    }
#endif
  }

  int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
#if CONFIG_TIP_REF_PRED_MERGING
                                 ,
                                 use_4x4
#else
                                 ,
                                 1
#endif
#endif  // CONFIG_OPTFLOW_ON_TIP
  );
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_D071_IMP_MSK_BLD
  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
#if CONFIG_TIP_REF_PRED_MERGING
  uint8_t use_bacp = tip_ref_frame ? cm->features.enable_imp_msk_bld
                                   : !build_for_obmc &&
                                         use_border_aware_compound(cm, mi) &&
                                         mi->cwp_idx == CWP_EQUAL &&
                                         cm->features.enable_imp_msk_bld;
#else
    uint8_t use_bacp = !build_for_obmc && use_border_aware_compound(cm, mi) &&
                       mi->cwp_idx == CWP_EQUAL &&
                       cm->features.enable_imp_msk_bld;
#endif  // CONFIG_TIP_REF_PRED_MERGING
#endif  // CONFIG_D071_IMP_MSK_BLD

#if CONFIG_EXT_WARP_FILTER
#if !CONFIG_TIP_REF_PRED_MERGING
  // Track whether we used the extended warp filter for either ref frame,
  // so that we can apply PEF
  bool ext_warp_used = false;
#endif
#endif  // CONFIG_EXT_WARP_FILTER

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_TIP_REF_PRED_MERGING
    const struct scale_factors *const sf =
        tip_ref_frame ? cm->tip_ref.ref_scale_factor[ref]
                      : (is_intrabc ? &cm->sf_identity
                                    : xd->block_ref_scale_factors[ref]);
    struct buf_2d *const pre_buf = is_intrabc ? &pd->dst : &pd->pre[ref];
    const MV mv = mi_mv[ref];
#else
    const struct scale_factors *const sf =
        is_intrabc ? &cm->sf_identity : xd->block_ref_scale_factors[ref];
    struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
    const MV mv = mi->mv[ref].as_mv;
#endif  // CONFIG_TIP_REF_PRED_MERGING
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };

    InterPredParams inter_pred_params;
#if CONFIG_TIP_REF_PRED_MERGING
    const int comp_bw = tip_ref_frame ? (bw >> ss_x) : bw;
    const int comp_bh = tip_ref_frame ? (bh >> ss_y) : bh;
    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;
#else
    av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
#endif  // CONFIG_TIP_REF_PRED_MERGING
    if (is_compound) av1_init_comp_mode(&inter_pred_params);
#if CONFIG_D071_IMP_MSK_BLD
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point to the first ref
#endif                        // CONFIG_D071_IMP_MSK_BLD

    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    if (!build_for_obmc) {
      av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);
#if CONFIG_EXT_WARP_FILTER
      if (inter_pred_params.mode == WARP_PRED &&
          !inter_pred_params.warp_params.use_affine_filter) {
#if CONFIG_TIP_REF_PRED_MERGING
        *ext_warp_used = true;
#else
          ext_warp_used = true;
#endif
      }
#if CONFIG_AFFINE_REFINEMENT
      if (use_optflow_refinement &&
          mi->comp_refine_type >= COMP_AFFINE_REFINE_START && n == 4)
#if CONFIG_TIP_REF_PRED_MERGING
        *ext_warp_used = true;
#else
        ext_warp_used = true;
#endif
#endif  // CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_EXT_WARP_FILTER
    }

#if CONFIG_D071_IMP_MSK_BLD
    if (is_compound) {
#if CONFIG_TIP_REF_PRED_MERGING
      inter_pred_params.sb_type =
          tip_ref_frame ? BLOCK_8X8 : mi->sb_type[PLANE_TYPE_Y];
#else
        inter_pred_params.sb_type = mi->sb_type[PLANE_TYPE_Y];
#endif  // CONFIG_TIP_REF_PRED_MERGING
      inter_pred_params.mask_comp = mi->interinter_comp;
    }
#endif  // CONFIG_D071_IMP_MSK_BLD

    if (is_masked_compound_type(mi->interinter_comp.type)) {
#if !CONFIG_D071_IMP_MSK_BLD
      inter_pred_params.sb_type = mi->sb_type[PLANE_TYPE_Y];
      inter_pred_params.mask_comp = mi->interinter_comp;
#endif  // !CONFIG_D071_IMP_MSK_BLD

      if (ref == 1) {
        inter_pred_params.conv_params.do_average = 0;
        inter_pred_params.comp_mode = MASK_COMP;
      }
      // Assign physical buffer.
      inter_pred_params.mask_comp.seg_mask = xd->seg_mask;
    }

    if (ref == 1 && inter_pred_params.conv_params.do_average == 1) {
      if (get_cwp_idx(mi) != CWP_EQUAL) {
        int8_t weight = get_cwp_idx(mi);
        assert(mi->cwp_idx >= CWP_MIN && mi->cwp_idx <= CWP_MAX);
        inter_pred_params.conv_params.fwd_offset = weight;
        inter_pred_params.conv_params.bck_offset =
            (1 << CWP_WEIGHT_BITS) - weight;
      }
    }

#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_AFFINE_REFINEMENT
    if (use_optflow_refinement &&
#if AFFINE_CHROMA_REFINE_METHOD > 0
        (mi->comp_refine_type >= COMP_AFFINE_REFINE_START || plane == 0)
#else
        mi->comp_refine_type >= COMP_AFFINE_REFINE_START && plane == 0
#endif
    ) {
#else
      if (use_optflow_refinement && plane == 0) {
#endif  // CONFIG_AFFINE_REFINEMENT
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);
      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);
#if CONFIG_TIP_REF_PRED_MERGING
      av1_opfl_rebuild_inter_predictor(dst, dst_stride, plane, mv_refined,
                                       &inter_pred_params, xd, mi_x, mi_y,
#if CONFIG_AFFINE_REFINEMENT
                                       cm, bw, mi->comp_refine_type, wms,
                                       &mi->mv[ref], use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
                                       ref, mc_buf, calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
                                       ,
                                       use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
#else
        av1_opfl_rebuild_inter_predictor(
            dst, dst_buf->stride, plane, mv_refined, &inter_pred_params, xd,
            mi_x, mi_y,
#if CONFIG_AFFINE_REFINEMENT
            mi->comp_refine_type, wms, &mi->mv[ref], use_affine_opfl,
#endif  // CONFIG_AFFINE_REFINEMENT
            ref, mc_buf, calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
            ,
            1
#endif  // CONFIG_OPTFLOW_ON_TIP
        );
#endif
      continue;
    }
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    if (mi->bawp_flag[0] > 0 && (plane == 0 || mi->bawp_flag[1]) &&
        !build_for_obmc) {
#else
      if (mi->bawp_flag > 0 && plane == 0 && !build_for_obmc) {
#endif  // CONFIG_BAWP_CHROMA
      av1_build_one_bawp_inter_predictor(
          dst,
#if CONFIG_TIP_REF_PRED_MERGING
          dst_stride,
#else
            dst_buf->stride,
#endif  // CONFIG_TIP_REF_PRED_MERGING
          &mv, &inter_pred_params, cm, xd, dst_orig, bw, bh, mi_x, mi_y, ref,
          plane, mc_buf, calc_subpel_params_func);
      continue;
    }
#endif  // CONFIG_BAWP
    av1_build_one_inter_predictor(dst,
#if CONFIG_TIP_REF_PRED_MERGING
                                  dst_stride,
#else
                                  dst_buf->stride,
#endif  // CONFIG_TIP_REF_PRED_MERGING
                                  &mv, &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
  }
#if !CONFIG_TIP_REF_PRED_MERGING
#if CONFIG_AFFINE_REFINEMENT
  const int apply_pef_opfl =
      (mi->comp_refine_type == COMP_REFINE_SUBBLK2P && plane == 0) ||
      damr_refine_subblock(plane, bw, bh, mi->comp_refine_type, n);
#endif  // CONFIG_AFFINE_REFINEMENT
  enhance_prediction(cm, xd, plane, dst, dst_buf->stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                     ,
                     mv_refined,
                     use_optflow_refinement
#if CONFIG_AFFINE_REFINEMENT
                         && apply_pef_opfl
#endif  // CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
                     ,
                     0, NULL
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
                     ,
                     ext_warp_used
#endif  // CONFIG_EXT_WARP_FILTER
  );
#endif  // !CONFIG_TIP_REF_PRED_MERGING
}

#if CONFIG_TIP_REF_PRED_MERGING
static AOM_INLINE void get_tip_mv(const AV1_COMMON *cm, MV *block_mv,
                                  int blk_col, int blk_row, MV tip_mv[2]) {
  const int mvs_stride =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);

  const int blk_to_tip_frame_offset =
      derive_block_mv_tpl_offset(cm, block_mv, blk_row, blk_col);
  const int tpl_offset =
      blk_row * mvs_stride + blk_col + blk_to_tip_frame_offset;
  const TPL_MV_REF *tpl_mvs = cm->tpl_mvs + tpl_offset;

  if (tpl_mvs->mfmv0.as_int != 0
#if CONFIG_MF_HOLE_FILL_SIMPLIFY
      && tpl_mvs->mfmv0.as_int != INVALID_MV
#endif  // CONFIG_MF_HOLE_FILL_SIMPLIFY
  ) {
    tip_get_mv_projection(&tip_mv[0], tpl_mvs->mfmv0.as_mv,
                          cm->tip_ref.ref_frames_offset_sf[0]);
    tip_get_mv_projection(&tip_mv[1], tpl_mvs->mfmv0.as_mv,
                          cm->tip_ref.ref_frames_offset_sf[1]);
  } else {
    MV zero_mv[2];
    memset(zero_mv, 0, sizeof(zero_mv));
    tip_mv[0] = zero_mv[0];
    tip_mv[1] = zero_mv[1];
  }
  tip_mv[0].row += block_mv->row;
  tip_mv[0].col += block_mv->col;
  tip_mv[1].row += block_mv->row;
  tip_mv[1].col += block_mv->col;
}

// Find the start row/col and end row/col for a given TIP prediction block.
static AOM_INLINE void set_tip_start_end_location(
    int start_pixel_col, int start_pixel_row, int block_width, int block_height,
    int *tpl_start_col, int *tpl_start_row, int *tpl_end_col,
    int *tpl_end_row) {
  // define the block start and end pixel locations
  const int bw = (block_width << MI_SIZE_LOG2);
  const int bh = (block_height << MI_SIZE_LOG2);
  int end_pixel_row = start_pixel_row + bh;
  int end_pixel_col = start_pixel_col + bw;

  // convert the pixel block location to MV field grid location
  *tpl_start_row = start_pixel_row >> TMVP_MI_SZ_LOG2;
  *tpl_end_row = (end_pixel_row + TMVP_MI_SIZE - 1) >> TMVP_MI_SZ_LOG2;
  *tpl_start_col = start_pixel_col >> TMVP_MI_SZ_LOG2;
  *tpl_end_col = (end_pixel_col + TMVP_MI_SIZE - 1) >> TMVP_MI_SZ_LOG2;
}

// This function consolidates the prediction process of the TIP ref mode block
// and the non-TIP ref mode block.
static void build_inter_predictors_8x8_and_bigger_facade(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
#if CONFIG_BAWP
    const BUFFER_SET *dst_orig,
#endif  // CONFIG_BAWP
    int build_for_obmc, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func
#if CONFIG_REFINEMV
    ,
    int build_for_refine_mv_only
#endif  // CONFIG_REFINEMV
) {
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  int_mv mv_refined[2 * N_OF_OFFSETS];
  memset(mv_refined, 0, 2 * N_OF_OFFSETS * sizeof(int_mv));
  bool ext_warp_used = false;

  struct macroblockd_plane *pd = &xd->plane[plane];
  struct buf_2d *dst_buf = &pd->dst;
  const int dst_stride = dst_buf->stride;
  uint16_t *const dst = dst_buf->buf;

  if (tip_ref_frame) {
    int tpl_start_col, tpl_start_row, tpl_end_col, tpl_end_row;
    set_tip_start_end_location(mi_x, mi_y, xd->width, xd->height,
                               &tpl_start_col, &tpl_start_row, &tpl_end_col,
                               &tpl_end_row);

    // TMVP_MI_SIZE_UV is the block size in luma unit for Chroma
    // TIP interpolation, will convert to the step size in TMVP 8x8 unit
    const int unit_blk_size = (plane == 0) ? TMVP_MI_SIZE : TMVP_MI_SIZE_UV;
    const int step = (unit_blk_size >> TMVP_MI_SZ_LOG2);

    for (int blk_row = tpl_start_row; blk_row < tpl_end_row; blk_row += step) {
      for (int blk_col = tpl_start_col; blk_col < tpl_end_col;
           blk_col += step) {
        const int tpl_row = blk_row << TMVP_MI_SZ_LOG2;
        const int tpl_col = blk_col << TMVP_MI_SZ_LOG2;
        const int row_offset = (blk_row - tpl_start_row);
        const int col_offset = (blk_col - tpl_start_col);
        const int tip_mv_offset = (row_offset * TIP_MV_STRIDE + col_offset)
                                  << 1;
        const int ss_x = pd->subsampling_x;
        const int ss_y = pd->subsampling_y;
        MV tip_mv[2];

        get_tip_mv(cm, &mi->mv[0].as_mv, blk_col, blk_row, tip_mv);

        dst_buf->buf = dst +
                       ((row_offset << TMVP_MI_SZ_LOG2) >> ss_y) * dst_stride +
                       ((col_offset << TMVP_MI_SZ_LOG2) >> ss_x);

        build_inter_predictors_8x8_and_bigger(
            cm, xd, plane, mi,
#if CONFIG_BAWP
            dst_orig,
#endif
            build_for_obmc, unit_blk_size, unit_blk_size, tpl_col, tpl_row,
            mc_buf, tip_mv, calc_subpel_params_func, dst_buf->buf, dst_stride,
            bw, bh,
#if CONFIG_REFINEMV
            build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
            &mv_refined[tip_mv_offset], &ext_warp_used);
      }
    }

    dst_buf->buf = dst;
  } else {
    MV mv[2] = { mi->mv[0].as_mv, mi->mv[1].as_mv };
    build_inter_predictors_8x8_and_bigger(cm, xd, plane, mi,
#if CONFIG_BAWP
                                          dst_orig,
#endif
                                          build_for_obmc, bw, bh, mi_x, mi_y,
                                          mc_buf, mv, calc_subpel_params_func,
                                          dst, dst_stride, bw, bh,
#if CONFIG_REFINEMV
                                          build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
                                          mv_refined, &ext_warp_used);
  }
  int apply_sub_block_refinemv =
      !tip_ref_frame && is_sub_block_refinemv_enabled(cm, mi, build_for_obmc,
                                                      plane, tip_ref_frame);
  int use_optflow_refinement =
      !tip_ref_frame && !apply_sub_block_refinemv &&
      is_optflow_refinement_enabled(cm, mi, plane, tip_ref_frame);
  int use_4x4 = tip_ref_frame ? 0 : 1;
  int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                                 ,
                                 use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
  );
#if CONFIG_AFFINE_REFINEMENT
  const int apply_pef_opfl =
      (mi->comp_refine_type == COMP_REFINE_SUBBLK2P && plane == 0) ||
      damr_refine_subblock(plane, bw, bh, mi->comp_refine_type, n);
#endif  // CONFIG_AFFINE_REFINEMENT
  enhance_prediction(cm, xd, plane, dst, dst_stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                     ,
                     mv_refined,
                     use_optflow_refinement
#if CONFIG_AFFINE_REFINEMENT
                         && apply_pef_opfl
#endif  // CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
                     ,
                     apply_sub_block_refinemv, &xd->refinemv_subinfo[0]
#endif  // CONFIG_REFINEMV
#if CONFIG_EXT_WARP_FILTER
                     ,
                     ext_warp_used
#endif  // CONFIG_EXT_WARP_FILTER
  );
}
#endif  // CONFIG_TIP_REF_PRED_MERGING

void av1_build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                int plane, MB_MODE_INFO *mi,
#if CONFIG_BAWP
                                const BUFFER_SET *dst_orig,
#endif
#if CONFIG_REFINEMV
                                int build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
                                int build_for_obmc, int bw, int bh, int mi_x,
                                int mi_y, uint16_t **mc_buf,
                                CalcSubpelParamsFunc calc_subpel_params_func) {
#if CONFIG_EXTENDED_WARP_PREDICTION
  // just for debugging purpose
  // Can be removed later on
  if (mi->mode == WARPMV) {
#if CONFIG_SEP_COMP_DRL
    assert(mi->ref_mv_idx[0] == 0);
    assert(mi->ref_mv_idx[1] == 0);
#else
      assert(mi->ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
    assert(mi->motion_mode == WARP_DELTA || mi->motion_mode == WARPED_CAUSAL);
  }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  if (is_sub8x8_inter(cm, xd, mi, plane, is_intrabc_block(mi, xd->tree_type),
                      build_for_obmc)) {
#if !CONFIG_EXT_RECUR_PARTITIONS
    assert(bw < 8 || bh < 8);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    build_inter_predictors_sub8x8(cm, xd, plane, mi, mi_x, mi_y, mc_buf,
                                  calc_subpel_params_func);
  } else {
#if CONFIG_TIP_REF_PRED_MERGING
    build_inter_predictors_8x8_and_bigger_facade(
        cm, xd, plane, mi,
#if CONFIG_BAWP
        dst_orig,
#endif
        build_for_obmc, bw, bh, mi_x, mi_y, mc_buf, calc_subpel_params_func
#if CONFIG_REFINEMV
        ,
        build_for_refine_mv_only
#endif  // CONFIG_REFINEMV
    );
#else
    build_inter_predictors_8x8_and_bigger(cm, xd, plane, mi,
#if CONFIG_BAWP
                                          dst_orig,
#endif
                                          build_for_obmc, bw, bh, mi_x, mi_y,
                                          mc_buf, calc_subpel_params_func
#if CONFIG_REFINEMV
                                          ,
                                          build_for_refine_mv_only
#endif  // CONFIG_REFINEMV
    );
#endif  // CONFIG_TIP_REF_PRED_MERGING
  }
}

void av1_setup_dst_planes(struct macroblockd_plane *planes,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const int plane_start, const int plane_end,
                          const CHROMA_REF_INFO *chroma_ref_info) {
  // We use AOMMIN(num_planes, MAX_MB_PLANE) instead of num_planes to quiet
  // the static analysis warnings.
  for (int i = plane_start; i < AOMMIN(plane_end, MAX_MB_PLANE); ++i) {
    struct macroblockd_plane *const pd = &planes[i];
    const int is_uv = i > 0;
    setup_pred_plane(&pd->dst, src->buffers[i], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                     mi_col, NULL, pd->subsampling_x, pd->subsampling_y,
                     chroma_ref_info);
  }
}

void av1_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf, const int num_planes,
                          const CHROMA_REF_INFO *chroma_ref_info) {
  if (src != NULL) {
    // We use AOMMIN(num_planes, MAX_MB_PLANE) instead of num_planes to quiet
    // the static analysis warnings.
    for (int i = 0; i < AOMMIN(num_planes, MAX_MB_PLANE); ++i) {
      struct macroblockd_plane *const pd = &xd->plane[i];
      const int is_uv = i > 0;
      setup_pred_plane(&pd->pre[idx], src->buffers[i], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                       mi_col, sf, pd->subsampling_x, pd->subsampling_y,
                       chroma_ref_info);
    }
  }
}

// obmc_mask_N[overlap_position]
static const uint8_t obmc_mask_1[1] = { 64 };
DECLARE_ALIGNED(2, static const uint8_t, obmc_mask_2[2]) = { 45, 64 };

DECLARE_ALIGNED(4, static const uint8_t, obmc_mask_4[4]) = { 39, 50, 59, 64 };

static const uint8_t obmc_mask_8[8] = { 36, 42, 48, 53, 57, 61, 64, 64 };

static const uint8_t obmc_mask_16[16] = { 34, 37, 40, 43, 46, 49, 52, 54,
                                          56, 58, 60, 61, 64, 64, 64, 64 };

static const uint8_t obmc_mask_32[32] = { 33, 35, 36, 38, 40, 41, 43, 44,
                                          45, 47, 48, 50, 51, 52, 53, 55,
                                          56, 57, 58, 59, 60, 60, 61, 62,
                                          64, 64, 64, 64, 64, 64, 64, 64 };

static const uint8_t obmc_mask_64[64] = {
  33, 34, 35, 35, 36, 37, 38, 39, 40, 40, 41, 42, 43, 44, 44, 44,
  45, 46, 47, 47, 48, 49, 50, 51, 51, 51, 52, 52, 53, 54, 55, 56,
  56, 56, 57, 57, 58, 58, 59, 60, 60, 60, 60, 60, 61, 62, 62, 62,
  62, 62, 63, 63, 63, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};

const uint8_t *av1_get_obmc_mask(int length) {
  switch (length) {
    case 1: return obmc_mask_1;
    case 2: return obmc_mask_2;
    case 4: return obmc_mask_4;
    case 8: return obmc_mask_8;
    case 16: return obmc_mask_16;
    case 32: return obmc_mask_32;
    case 64: return obmc_mask_64;
    default: assert(0); return NULL;
  }
}

static INLINE void increment_uint8_t_ptr(MACROBLOCKD *xd, int rel_mi_row,
                                         int rel_mi_col, uint8_t op_mi_size,
                                         int dir, MB_MODE_INFO *mi,
                                         void *fun_ctxt, const int num_planes) {
  (void)xd;
  (void)rel_mi_row;
  (void)rel_mi_col;
  (void)op_mi_size;
  (void)dir;
  (void)mi;
  ++*(uint8_t *)fun_ctxt;
  (void)num_planes;
}

void av1_count_overlappable_neighbors(const AV1_COMMON *cm, MACROBLOCKD *xd) {
  MB_MODE_INFO *mbmi = xd->mi[0];

  mbmi->overlappable_neighbors[0] = 0;
  mbmi->overlappable_neighbors[1] = 0;
  if (!is_motion_variation_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y],
                                         xd->mi_row, xd->mi_col))
    return;

  foreach_overlappable_nb_above(cm, xd, INT_MAX, increment_uint8_t_ptr,
                                &mbmi->overlappable_neighbors[0], true);
  if (mbmi->overlappable_neighbors[0]) return;
  foreach_overlappable_nb_left(cm, xd, INT_MAX, increment_uint8_t_ptr,
                               &mbmi->overlappable_neighbors[1]);
}

// HW does not support < 4x4 prediction. To limit the bandwidth requirement, if
// block-size of current plane is smaller than 8x8, always only blend with the
// left neighbor(s) (skip blending with the above side).
#define DISABLE_CHROMA_U8X8_OBMC 0  // 0: one-sided obmc; 1: disable

int av1_skip_u4x4_pred_in_obmc(BLOCK_SIZE bsize,
                               const struct macroblockd_plane *pd, int dir) {
  const BLOCK_SIZE bsize_plane =
      get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
  switch (bsize_plane) {
#if DISABLE_CHROMA_U8X8_OBMC
    case BLOCK_4X4:
    case BLOCK_8X4:
    case BLOCK_4X8: return 1; break;
#else
    case BLOCK_4X4:
    case BLOCK_8X4:
    case BLOCK_4X8: return dir == 0; break;
#endif
    default: return 0;
  }
}

void av1_modify_neighbor_predictor_for_obmc(MB_MODE_INFO *mbmi) {
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;

  return;
}

struct obmc_inter_pred_ctxt {
  uint16_t **adjacent;
  int *adjacent_stride;
};

static INLINE void build_obmc_inter_pred_above(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *above_mi, void *fun_ctxt, const int num_planes) {
  (void)above_mi;
  (void)rel_mi_row;
  (void)dir;
  struct obmc_inter_pred_ctxt *ctxt = (struct obmc_inter_pred_ctxt *)fun_ctxt;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  const int overlap =
      AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;

  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    const int bw = (op_mi_size * MI_SIZE) >> pd->subsampling_x;
    const int bh = overlap >> pd->subsampling_y;
    const int plane_col = (rel_mi_col * MI_SIZE) >> pd->subsampling_x;

    if (av1_skip_u4x4_pred_in_obmc(bsize, pd, 0)) continue;

    const int dst_stride = pd->dst.stride;
    uint16_t *const dst = &pd->dst.buf[plane_col];
    const int tmp_stride = ctxt->adjacent_stride[plane];
    const uint16_t *const tmp = &ctxt->adjacent[plane][plane_col];
    const uint8_t *const mask = av1_get_obmc_mask(bh);
    aom_highbd_blend_a64_vmask(dst, dst_stride, dst, dst_stride, tmp,
                               tmp_stride, mask, bw, bh, xd->bd);
  }
}

static INLINE void build_obmc_inter_pred_left(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *left_mi, void *fun_ctxt, const int num_planes) {
  (void)left_mi;
  (void)rel_mi_col;
  (void)dir;
  struct obmc_inter_pred_ctxt *ctxt = (struct obmc_inter_pred_ctxt *)fun_ctxt;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  const int overlap =
      AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;

  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    const int bw = overlap >> pd->subsampling_x;
    const int bh = (op_mi_size * MI_SIZE) >> pd->subsampling_y;
    const int plane_row = (rel_mi_row * MI_SIZE) >> pd->subsampling_y;

    if (av1_skip_u4x4_pred_in_obmc(bsize, pd, 1)) continue;

    const int dst_stride = pd->dst.stride;
    uint16_t *const dst = &pd->dst.buf[plane_row * dst_stride];
    const int tmp_stride = ctxt->adjacent_stride[plane];
    const uint16_t *const tmp = &ctxt->adjacent[plane][plane_row * tmp_stride];
    const uint8_t *const mask = av1_get_obmc_mask(bw);

    aom_highbd_blend_a64_hmask(dst, dst_stride, dst, dst_stride, tmp,
                               tmp_stride, mask, bw, bh, xd->bd);
  }
}

// This function combines motion compensated predictions that are generated by
// top/left neighboring blocks' inter predictors with the regular inter
// prediction. We assume the original prediction (bmc) is stored in
// xd->plane[].dst.buf
void av1_build_obmc_inter_prediction(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                     uint16_t *above[MAX_MB_PLANE],
                                     int above_stride[MAX_MB_PLANE],
                                     uint16_t *left[MAX_MB_PLANE],
                                     int left_stride[MAX_MB_PLANE]) {
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];

  // handle above row
  struct obmc_inter_pred_ctxt ctxt_above = { above, above_stride };
  foreach_overlappable_nb_above(
      cm, xd, max_neighbor_obmc[mi_size_wide_log2[bsize]],
      build_obmc_inter_pred_above, &ctxt_above, false);

  // handle left column
  struct obmc_inter_pred_ctxt ctxt_left = { left, left_stride };
  foreach_overlappable_nb_left(cm, xd,
                               max_neighbor_obmc[mi_size_high_log2[bsize]],
                               build_obmc_inter_pred_left, &ctxt_left);
}

void av1_setup_obmc_dst_bufs(MACROBLOCKD *xd, uint16_t **dst_buf1,
                             uint16_t **dst_buf2) {
  dst_buf1[0] = xd->tmp_obmc_bufs[0];
  dst_buf1[1] = xd->tmp_obmc_bufs[0] + MAX_SB_SQUARE;
  dst_buf1[2] = xd->tmp_obmc_bufs[0] + MAX_SB_SQUARE * 2;
  dst_buf2[0] = xd->tmp_obmc_bufs[1];
  dst_buf2[1] = xd->tmp_obmc_bufs[1] + MAX_SB_SQUARE;
  dst_buf2[2] = xd->tmp_obmc_bufs[1] + MAX_SB_SQUARE * 2;
}

void av1_setup_build_prediction_by_above_pred(
    MACROBLOCKD *xd, int rel_mi_col, uint8_t above_mi_width,
    MB_MODE_INFO *above_mbmi, struct build_prediction_ctxt *ctxt,
    const int num_planes) {
  const int above_mi_col = xd->mi_col + rel_mi_col;

  av1_modify_neighbor_predictor_for_obmc(above_mbmi);

  for (int j = 0; j < num_planes; ++j) {
    struct macroblockd_plane *const pd = &xd->plane[j];
    setup_pred_plane(&pd->dst, ctxt->tmp_buf[j], ctxt->tmp_width[j],
                     ctxt->tmp_height[j], ctxt->tmp_stride[j], 0, rel_mi_col,
                     NULL, pd->subsampling_x, pd->subsampling_y, NULL);
  }

  const int num_refs = 1 + has_second_ref(above_mbmi);

  for (int ref = 0; ref < num_refs; ++ref) {
    const MV_REFERENCE_FRAME frame = above_mbmi->ref_frame[ref];

    const RefCntBuffer *const ref_buf = get_ref_frame_buf(ctxt->cm, frame);
    const struct scale_factors *const sf =
        get_ref_scale_factors_const(ctxt->cm, frame);
    xd->block_ref_scale_factors[ref] = sf;
    if ((!av1_is_valid_scale(sf)))
      aom_internal_error(xd->error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Reference frame has invalid dimensions");
    av1_setup_pre_planes(xd, ref, &ref_buf->buf, xd->mi_row, above_mi_col, sf,
                         num_planes, NULL);
  }

  xd->mb_to_left_edge = 8 * MI_SIZE * (-above_mi_col);
  xd->mb_to_right_edge =
      ctxt->mb_to_far_edge +
      (xd->width - rel_mi_col - above_mi_width) * MI_SIZE * 8;
}

void av1_setup_build_prediction_by_left_pred(MACROBLOCKD *xd, int rel_mi_row,
                                             uint8_t left_mi_height,
                                             MB_MODE_INFO *left_mbmi,
                                             struct build_prediction_ctxt *ctxt,
                                             const int num_planes) {
  const int left_mi_row = xd->mi_row + rel_mi_row;

  av1_modify_neighbor_predictor_for_obmc(left_mbmi);

  for (int j = 0; j < num_planes; ++j) {
    struct macroblockd_plane *const pd = &xd->plane[j];
    setup_pred_plane(&pd->dst, ctxt->tmp_buf[j], ctxt->tmp_width[j],
                     ctxt->tmp_height[j], ctxt->tmp_stride[j], rel_mi_row, 0,
                     NULL, pd->subsampling_x, pd->subsampling_y, NULL);
  }

  const int num_refs = 1 + has_second_ref(left_mbmi);

  for (int ref = 0; ref < num_refs; ++ref) {
    const MV_REFERENCE_FRAME frame = left_mbmi->ref_frame[ref];

    const RefCntBuffer *const ref_buf = get_ref_frame_buf(ctxt->cm, frame);
    const struct scale_factors *const ref_scale_factors =
        get_ref_scale_factors_const(ctxt->cm, frame);

    xd->block_ref_scale_factors[ref] = ref_scale_factors;
    if ((!av1_is_valid_scale(ref_scale_factors)))
      aom_internal_error(xd->error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Reference frame has invalid dimensions");
    av1_setup_pre_planes(xd, ref, &ref_buf->buf, left_mi_row, xd->mi_col,
                         ref_scale_factors, num_planes, NULL);
  }

  xd->mb_to_top_edge = GET_MV_SUBPEL(MI_SIZE * (-left_mi_row));
  xd->mb_to_bottom_edge =
      ctxt->mb_to_far_edge +
      GET_MV_SUBPEL((xd->height - rel_mi_row - left_mi_height) * MI_SIZE);
}

static AOM_INLINE void combine_interintra_highbd(
    INTERINTRA_MODE mode, int8_t use_wedge_interintra, int8_t wedge_index,
    int8_t wedge_sign, BLOCK_SIZE bsize, BLOCK_SIZE plane_bsize,
    uint16_t *comppred8, int compstride, const uint16_t *interpred8,
    int interstride, const uint16_t *intrapred8, int intrastride, int bd) {
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];

  if (use_wedge_interintra) {
    if (av1_is_wedge_used(bsize)) {
      const uint8_t *mask =
          av1_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
      const int subh = 2 * mi_size_high[bsize] == bh;
      const int subw = 2 * mi_size_wide[bsize] == bw;
      aom_highbd_blend_a64_mask(comppred8, compstride, intrapred8, intrastride,
                                interpred8, interstride, mask,
                                block_size_wide[bsize], bw, bh, subw, subh, bd);
    }
    return;
  }

  uint8_t mask[MAX_SB_SQUARE];
  build_smooth_interintra_mask(mask, bw, plane_bsize, mode);
  aom_highbd_blend_a64_mask(comppred8, compstride, intrapred8, intrastride,
                            interpred8, interstride, mask, bw, bw, bh, 0, 0,
                            bd);
}

#if CONFIG_EXT_RECUR_PARTITIONS
void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride) {
#else
void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd,
                                               BLOCK_SIZE bsize, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ssx = xd->plane[plane].subsampling_x;
  const int ssy = xd->plane[plane].subsampling_y;
#if CONFIG_EXT_RECUR_PARTITIONS
  BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], plane, ssx, ssy);
#else
  BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, ssx, ssy);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  PREDICTION_MODE mode = interintra_to_intra_mode[xd->mi[0]->interintra_mode];
  assert(xd->mi[0]->angle_delta[PLANE_TYPE_Y] == 0);
  assert(xd->mi[0]->angle_delta[PLANE_TYPE_UV] == 0);
  assert(xd->mi[0]->filter_intra_mode_info.use_filter_intra == 0);
  assert(xd->mi[0]->use_intrabc[PLANE_TYPE_Y] == 0);
#if CONFIG_TX_PARTITION_TYPE_EXT
  xd->mi[0]->txb_idx = 0;
#endif  // CONFIG_TX_PARTITION_TYPE_EXT
  av1_predict_intra_block(cm, xd, pd->width, pd->height,
                          max_txsize_rect_lookup[plane_bsize], mode, 0, 0,
                          FILTER_INTRA_MODES, ctx->plane[plane],
                          ctx->stride[plane], dst, dst_stride, 0, 0, plane);
}

void av1_combine_interintra(MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane,
                            const uint16_t *inter_pred, int inter_stride,
                            const uint16_t *intra_pred, int intra_stride) {
  const int ssx = xd->plane[plane].subsampling_x;
  const int ssy = xd->plane[plane].subsampling_y;
  BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], plane, ssx, ssy);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize == get_plane_block_size(bsize, ssx, ssy));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  combine_interintra_highbd(
      xd->mi[0]->interintra_mode, xd->mi[0]->use_wedge_interintra,
      xd->mi[0]->interintra_wedge_index, INTERINTRA_WEDGE_SIGN, bsize,
      plane_bsize, xd->plane[plane].dst.buf, xd->plane[plane].dst.stride,
      inter_pred, inter_stride, intra_pred, intra_stride, xd->bd);
}

// build interintra_predictors for one plane
void av1_build_interintra_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t *pred, int stride,
                                    const BUFFER_SET *ctx, int plane,
                                    BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  DECLARE_ALIGNED(16, uint16_t, intrapredictor[MAX_SB_SQUARE]);
#if CONFIG_EXT_RECUR_PARTITIONS
  av1_build_intra_predictors_for_interintra(cm, xd, plane, ctx, intrapredictor,
                                            MAX_SB_SIZE);
#else
  av1_build_intra_predictors_for_interintra(cm, xd, bsize, plane, ctx,
                                            intrapredictor, MAX_SB_SIZE);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  av1_combine_interintra(xd, bsize, plane, pred, stride, intrapredictor,
                         MAX_SB_SIZE);
}

int av1_get_mpp_flag_context(const AV1_COMMON *cm, const MACROBLOCKD *xd) {
  (void)cm;
  const MB_MODE_INFO *const above_mi = xd->above_mbmi;
  const MB_MODE_INFO *const left_mi = xd->left_mbmi;
  const int above_mpp_flag =
      (above_mi && is_inter_block(above_mi, SHARED_PART) &&
       !is_intrabc_block(above_mi, SHARED_PART))
          ? (above_mi->most_probable_pb_mv_precision ==
             above_mi->pb_mv_precision)
          : 0;
  const int left_mpp_flag =
      (left_mi && is_inter_block(left_mi, SHARED_PART) &&
       !is_intrabc_block(left_mi, SHARED_PART))
          ? (left_mi->most_probable_pb_mv_precision == left_mi->pb_mv_precision)
          : 0;

  return (above_mpp_flag + left_mpp_flag);
}

#if CONFIG_REFINEMV
// Derive the context index for refinemv flag
int av1_get_refinemv_context(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             BLOCK_SIZE bsize) {
  (void)cm;
  (void)bsize;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->skip_mode) return 0;
  return (1 + (mbmi->mode - NEAR_NEARMV));
}
#endif  // CONFIG_REFINEMV

int av1_get_pb_mv_precision_down_context(const AV1_COMMON *cm,
                                         const MACROBLOCKD *xd) {
  (void)cm;
  const MB_MODE_INFO *const above_mi = xd->above_mbmi;
  const MB_MODE_INFO *const left_mi = xd->left_mbmi;
  const int above_down =
      (above_mi && is_inter_block(above_mi, SHARED_PART) &&
       !is_intrabc_block(above_mi, SHARED_PART))
          ? above_mi->max_mv_precision - above_mi->pb_mv_precision
          : 0;
  const int left_down =
      (left_mi && is_inter_block(left_mi, SHARED_PART) &&
       !is_intrabc_block(left_mi, SHARED_PART))  // && !left_mi->skip_mode)
          ? left_mi->max_mv_precision - left_mi->pb_mv_precision
          : 0;
  assert(above_down >= 0);
  assert(left_down >= 0);
  return (above_down + left_down > 0);
}

int av1_get_mv_class_context(const MvSubpelPrecision pb_mv_precision) {
  return pb_mv_precision;
}

void set_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision) {
  mbmi->pb_mv_precision = precision;
}
#if BUGFIX_AMVD_AMVR
// set the mv precision for amvd applied mode
void set_amvd_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision) {
  mbmi->pb_mv_precision =
      precision <= MV_PRECISION_QTR_PEL ? precision : MV_PRECISION_QTR_PEL;
}
#endif  // BUGFIX_AMVD_AMVR
int av1_get_pb_mv_precision_index(const MB_MODE_INFO *mbmi) {
  const PRECISION_SET *precision_def =
      &av1_mv_precision_sets[mbmi->mb_precision_set];
  int coded_precision_idx = -1;
  for (int precision_dx = precision_def->num_precisions - 1; precision_dx >= 0;
       precision_dx--) {
    MvSubpelPrecision pb_mv_precision = precision_def->precision[precision_dx];
    if (pb_mv_precision != mbmi->most_probable_pb_mv_precision) {
      coded_precision_idx++;
      if (pb_mv_precision == mbmi->pb_mv_precision) return coded_precision_idx;
    }
  }
  assert(0);
  return coded_precision_idx;
}

MvSubpelPrecision av1_get_precision_from_index(MB_MODE_INFO *mbmi,
                                               int precision_idx_coded_value) {
  const PRECISION_SET *precision_def =
      &av1_mv_precision_sets[mbmi->mb_precision_set];
  int coded_precision_idx = -1;
  MvSubpelPrecision pb_mv_precision = NUM_MV_PRECISIONS;
  for (int precision_dx = precision_def->num_precisions - 1; precision_dx >= 0;
       precision_dx--) {
    pb_mv_precision = precision_def->precision[precision_dx];
    if (pb_mv_precision != mbmi->most_probable_pb_mv_precision) {
      coded_precision_idx++;
      if (coded_precision_idx == precision_idx_coded_value)
        return pb_mv_precision;
    }
  }
  assert(0);
  return pb_mv_precision;
}
void set_most_probable_mv_precision(const AV1_COMMON *const cm,
                                    MB_MODE_INFO *mbmi,
                                    const BLOCK_SIZE bsize) {
  (void)bsize;
  (void)cm;
  const PRECISION_SET *precision_def =
      &av1_mv_precision_sets[mbmi->mb_precision_set];
  mbmi->most_probable_pb_mv_precision =
      precision_def->precision[precision_def->num_precisions - 1];

#if CONFIG_DEBUG
  int mpp_found = 0;
  for (int precision_dx = precision_def->num_precisions - 1; precision_dx >= 0;
       precision_dx--) {
    MvSubpelPrecision pb_mv_precision = precision_def->precision[precision_dx];
    if (pb_mv_precision == mbmi->most_probable_pb_mv_precision) {
      mpp_found = 1;
      break;
    }
  }
  (void)mpp_found;
  assert(mpp_found);
#endif
}
void set_precision_set(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                       MB_MODE_INFO *mbmi, const BLOCK_SIZE bsize,
#if CONFIG_SEP_COMP_DRL
                       int *ref_mv_idx) {
#else
                       uint8_t ref_mv_idx) {
#endif  // CONFIG_SEP_COMP_DRL
  (void)bsize;
  (void)cm;
  (void)xd;
  (void)ref_mv_idx;

  int set_idx = 0;

  int offset_idx = (mbmi->max_mv_precision == MV_PRECISION_QTR_PEL)
                       ? NUMBER_OF_PRECISION_SETS
                       : 0;
  mbmi->mb_precision_set = set_idx + offset_idx;
}
void set_default_precision_set(const AV1_COMMON *const cm, MB_MODE_INFO *mbmi,
                               const BLOCK_SIZE bsize) {
  (void)bsize;
  (void)cm;
  int set_idx = 0;
  int offset_idx = (mbmi->max_mv_precision == MV_PRECISION_QTR_PEL)
                       ? NUMBER_OF_PRECISION_SETS
                       : 0;
  mbmi->mb_precision_set = set_idx + offset_idx;
}
void set_default_max_mv_precision(MB_MODE_INFO *mbmi,
                                  MvSubpelPrecision precision) {
  mbmi->max_mv_precision = precision;
}
MvSubpelPrecision av1_get_mbmi_max_mv_precision(const AV1_COMMON *const cm,
                                                const SB_INFO *sbi,
                                                const MB_MODE_INFO *mbmi) {
  (void)mbmi;
  (void)sbi;
  return cm->features.fr_mv_precision;
}

int is_pb_mv_precision_active(const AV1_COMMON *const cm,
                              const MB_MODE_INFO *mbmi,
                              const BLOCK_SIZE bsize) {
  (void)bsize;
  if (enable_adaptive_mvd_resolution(cm, mbmi)) return 0;
  return cm->seq_params.enable_flex_mvres &&
         (mbmi->max_mv_precision >= MV_PRECISION_HALF_PEL) &&
         cm->features.use_pb_mv_precision &&
         have_newmv_in_inter_mode(mbmi->mode);
}

#if CONFIG_REFINEMV
// Copy mv0 and mv1 to the sub-blocks
// submi is the top-left corner of the sub-block need to fill
// bw is the block width in the unit of pixel
// bh is the block height in unit of pixel
void fill_subblock_refine_mv(REFINEMV_SUBMB_INFO *refinemv_subinfo, int bw,
                             int bh, MV mv0, MV mv1) {
  const int stride = MAX_MIB_SIZE;
  for (int y = 0; y < (bh >> MI_SIZE_LOG2); y++) {
    for (int x = 0; x < (bw >> MI_SIZE_LOG2); x++) {
      refinemv_subinfo[x].refinemv[0].as_mv = mv0;
      refinemv_subinfo[x].refinemv[1].as_mv = mv1;
    }
    refinemv_subinfo += stride;
  }
}
#endif  // CONFIG_REFINEMV

#if CONFIG_MORPH_PRED
// Let the dst buffer point to the given position (x, y) in the src buffer.
static void set_buffer(struct buf_2d *dst, uint16_t *src, int width, int height,
                       int stride, int x, int y) {
  dst->buf = src + y * stride + x;
  dst->buf0 = src;
  dst->width = width;
  dst->height = height;
  dst->stride = stride;
}

static bool fetch_neighbor_recon_regions(
    const AV1_COMMON *const cm, MACROBLOCKD *const xd, const BLOCK_SIZE bsize,
    const int mi_row, const int mi_col, struct buf_2d *cur_template_recon,
    struct buf_2d *ref_template_recon, int *template_width,
    int *template_height) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  FULLPEL_MV dv = get_fullmv_from_mv(&mbmi->mv[0].as_mv);
  const int tgt_width = TEMPLATE_SIZE;
  const int tgt_height = TEMPLATE_SIZE;
  const int cur_x = mi_col * MI_SIZE;
  const int cur_y = mi_row * MI_SIZE;
  const int cur_tmplt_x = AOMMAX(cur_x - tgt_width, 0);
  const int cur_tmplt_y = AOMMAX(cur_y - tgt_height, 0);
  const int ref_tmplt_x = AOMMAX(cur_x + dv.col - tgt_width, 0);
  const int ref_tmplt_y = AOMMAX(cur_y + dv.row - tgt_height, 0);

  // Restriction: the reference block's template can't be outside the local
  // 64x64 block for local intra block copy.
  // If local intra block copy extends to 128x128, one has to change the
  // restrictions here to make it match.
  const int ref_x = AOMMAX(cur_x + dv.col, 0);
  const int ref_y = AOMMAX(cur_y + dv.row, 0);
  const int is_same_unit_x = (cur_x >> 6) == (ref_x >> 6);
  const int is_same_unit_y = (cur_y >> 6) == (ref_y >> 6);
  if (is_same_unit_x && is_same_unit_y) {
    if (ref_x > 0 && (ref_x % 64 == 0)) return false;
    if (ref_y > 0 && (ref_y % 64 == 0)) return false;
  }
  // Restriction: the reference block's template can't be outside the current
  // tile.
  const TileInfo *const tile = &xd->tile;
  // Is the source top-left inside the current tile?
  const int tile_top_edge = tile->mi_row_start * MI_SIZE;
  if (ref_tmplt_y < tile_top_edge) return false;
  const int tile_left_edge = tile->mi_col_start * MI_SIZE;
  if (ref_tmplt_x < tile_left_edge) return false;
  // Is the bottom right inside the current tile?
  const int ref_bottom_edge = cur_y + dv.row + bh;
  const int tile_bottom_edge = tile->mi_row_end * MI_SIZE;
  if (ref_bottom_edge > tile_bottom_edge) return false;
  const int ref_right_edge = cur_x + dv.col + bw;
  const int tile_right_edge = tile->mi_col_end * MI_SIZE;
  if (ref_right_edge > tile_right_edge) return false;
  // The current block's template can't be outside the current tile too.
  if (cur_tmplt_y < tile_top_edge) return false;
  if (cur_tmplt_x < tile_left_edge) return false;

  const int cur_tmplt_width = cur_x - cur_tmplt_x;
  const int cur_tmplt_height = cur_y - cur_tmplt_y;
  const int ref_tmplt_width = cur_x + dv.col - ref_tmplt_x;
  const int ref_tmplt_height = cur_y + dv.row - ref_tmplt_y;
  if (cur_tmplt_width <= 0 || cur_tmplt_height <= 0 || ref_tmplt_width <= 0 ||
      ref_tmplt_height <= 0) {
    return false;
  }

  *template_width = AOMMIN(cur_tmplt_width, ref_tmplt_width);
  *template_height = AOMMIN(cur_tmplt_height, ref_tmplt_height);
  const YV12_BUFFER_CONFIG *const recon_buffer = &cm->cur_frame->buf;
  set_buffer(cur_template_recon, recon_buffer->buffers[0], *template_width,
             *template_height, recon_buffer->strides[0], cur_tmplt_x,
             cur_tmplt_y);
  set_buffer(ref_template_recon, recon_buffer->buffers[0], *template_width,
             *template_height, recon_buffer->strides[0], ref_tmplt_x,
             ref_tmplt_y);
  return true;
}

static bool derive_linear_params_from_template(
    const AV1_COMMON *const cm, const int mi_row, const int mi_col,
    const uint16_t *src, const int src_stride, const uint16_t *pred,
    const int pred_stride, const int width, const int height,
    const int template_width, const int template_height, int *alpha,
    int *beta) {
  const uint16_t *src_ptr = src;
  const uint16_t *pred_ptr = pred;
  int sum_x = 0;
  int sum_y = 0;
  int sum_xy = 0;
  int sum_xx = 0;
  int count = 0;
  const int frame_width = cm->width;
  const int frame_height = cm->height;
  const int x_start = mi_col * MI_SIZE;
  const int y_start = mi_row * MI_SIZE;
  if (x_start >= frame_width || y_start >= frame_height) return false;

  int x_max = template_width + AOMMIN(width, frame_width - x_start);
  int y_max = AOMMIN(template_height, frame_height - y_start);

  for (int y = 0; y < y_max; ++y) {
    for (int x = 0; x < x_max; ++x) {
      sum_x += pred_ptr[x];
      sum_y += src_ptr[x];
      sum_xy += src_ptr[x] * pred_ptr[x];
      sum_xx += pred_ptr[x] * pred_ptr[x];
    }
    count += x_max;
    src_ptr += src_stride;
    pred_ptr += pred_stride;
  }
  x_max = AOMMIN(template_width, frame_width - x_start);
  y_max = AOMMIN(height, frame_height - y_start);
  for (int y = 0; y < y_max; ++y) {
    for (int x = 0; x < x_max; ++x) {
      sum_x += pred_ptr[x];
      sum_y += src_ptr[x];
      sum_xy += src_ptr[x] * pred_ptr[x];
      sum_xx += pred_ptr[x] * pred_ptr[x];
    }
    count += x_max;
    src_ptr += src_stride;
    pred_ptr += pred_stride;
  }
  if (count == 0) return false;

  *alpha = derive_linear_parameters_alpha(sum_x, sum_y, sum_xx, sum_xy, count,
                                          MORPH_FIT_SHIFT, 0);
  *beta = derive_linear_parameters_beta(sum_x, sum_y, count, MORPH_FIT_SHIFT,
                                        *alpha);
  return true;
}

void av1_build_linear_predictor(uint16_t *dst, const int dst_stride,
                                const int width, const int height,
                                const int alpha, const int beta,
                                const int bit_depth) {
  const int alpha_shift = alpha;
  const int beta_shift = beta;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      dst[x] = clip_pixel_highbd(
          ROUND_POWER_OF_TWO_SIGNED(alpha_shift * dst[x] + beta_shift,
                                    MORPH_FIT_SHIFT),
          bit_depth);
    }
    dst += dst_stride;
  }
}

bool av1_build_morph_pred(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                          const BLOCK_SIZE bsize, const int mi_row,
                          const int mi_col) {
  // Predictor, i.e., the reconstructed block found from intrabc.
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  uint16_t *const dst = pd->dst.buf;
  const int dst_stride = pd->dst.stride;
  const int width = block_size_wide[bsize];
  const int height = block_size_high[bsize];
  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->morph_alpha = 0;
  mbmi->morph_beta = 0;
  struct buf_2d cur_template_recon;
  struct buf_2d ref_template_recon;
  int template_width = width >> 1;
  int template_height = height >> 1;
  const bool valid_region = fetch_neighbor_recon_regions(
      cm, xd, bsize, mi_row, mi_col, &cur_template_recon, &ref_template_recon,
      &template_width, &template_height);
  if (!valid_region) return false;

  const bool valid_params = derive_linear_params_from_template(
      cm, mi_row, mi_col, cur_template_recon.buf, cur_template_recon.stride,
      ref_template_recon.buf, ref_template_recon.stride, width, height,
      template_width, template_height, &mbmi->morph_alpha, &mbmi->morph_beta);
  if (!valid_params) return false;
  av1_build_linear_predictor(dst, dst_stride, width, height, mbmi->morph_alpha,
                             mbmi->morph_beta, xd->bd);
  return true;
}
#endif  // CONFIG_MORPH_PRED
