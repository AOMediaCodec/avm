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
#include "av1/common/mvref_common.h"
#include "av1/common/obmc.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"

// This function will determine whether or not to create a warped
// prediction.
int av1_allow_warp(const MB_MODE_INFO *const mbmi,
                   const WarpTypesAllowed *const warp_types,
                   const WarpedMotionParams *const gm_params,
#if CONFIG_EXTENDED_WARP_PREDICTION
                   int ref,
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
                   int build_for_obmc, const struct scale_factors *const sf,
                   WarpedMotionParams *final_warp_params) {
  // Note: As per the spec, we must test the fixed point scales here, which are
  // at a higher precision (1 << 14) than the xs and ys in subpel_params (that
  // have 1 << 10 precision).
  if (av1_is_scaled(sf)) return 0;

  if (final_warp_params != NULL) *final_warp_params = default_warp_params;

  if (build_for_obmc) return 0;

#if CONFIG_EXTENDED_WARP_PREDICTION
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
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
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

#if CONFIG_TIP
  if (is_tip_ref_frame(mi->ref_frame[ref])) return;
#endif  // CONFIG_TIP

#if CONFIG_REFINEMV
  // We do not do refineMV for warp blocks
  // We may need to return from here.
  if (mi->refinemv_flag) return;
#endif  // CONFIG_REFINEMV

  if (xd->cur_frame_force_integer_mv) return;

  if (av1_allow_warp(mi, warp_types, &xd->global_motion[mi->ref_frame[ref]],
#if CONFIG_EXTENDED_WARP_PREDICTION
                     ref,
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
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
  { 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, },
  { 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },  // not used
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

#if CONFIG_CWP
DECLARE_ALIGNED(16, static int8_t, cwp_mask[2][MAX_CWP_NUM][MAX_SB_SQUARE]);
#endif  // CONFIG_CWP

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
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X16] },
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
  { MAX_WEDGE_TYPES, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_8X32],
    wedge_masks[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_32X8],
    wedge_masks[BLOCK_32X8] },
  { 0, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL },
};
#endif

#if CONFIG_CWP
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
#endif  // CONFIG_CWP

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
  assert(is_masked_compound_type(comp_data->type));
  (void)sb_type;
  switch (comp_data->type) {
    case COMPOUND_WEDGE:
      return av1_get_contiguous_soft_mask(comp_data->wedge_index,
                                          comp_data->wedge_sign, sb_type);
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
    8,  8,  4,  4,  2, 2
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

#if CONFIG_REFINEMV
// Compute the SAD values for refineMV modes
int get_refinemv_sad(uint16_t *src1, uint16_t *src2, int width, int height,
                     int bd) {
  return get_highbd_sad(src1, width, src2, width, bd, width, height);
}
#endif  // CONFIG_REFINEMV

#if CONFIG_OPTFLOW_REFINEMENT
// Restrict MV delta to 1 or 2 pixels. This restriction would reduce complexity
// in hardware.
#define OPFL_CLAMP_MV_DELTA 1
#define OPFL_MV_DELTA_LIMIT (1 << MV_REFINE_PREC_BITS)

static INLINE int opfl_get_subblock_size(int bw, int bh, int plane
#if CONFIG_OPTFLOW_ON_TIP
                                         ,
                                         int use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
) {
#if CONFIG_OPTFLOW_ON_TIP
  return ((plane || (bh <= 8 && bw <= 8)) && use_4x4) ? OF_MIN_BSIZE : OF_BSIZE;
#else
  return (plane || (bh <= 8 && bw <= 8)) ? OF_MIN_BSIZE : OF_BSIZE;
#endif  // CONFIG_OPTFLOW_ON_TIP
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
  const struct buf_2d *const pre_buf =
      is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref]
             : (is_intrabc ? dst_buf : &pd->pre[ref]);
#else
  struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#endif  // CONFIG_OPTFLOW_ON_TIP

  av1_init_inter_params(inter_pred_params, bw, bh, pre_y, pre_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd,
                        mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
#if CONFIG_REFINEMV
  inter_pred_params->original_pu_width = pu_width;
  inter_pred_params->original_pu_height = pu_height;
#endif  // CONFIG_REFINEMV

#if CONFIG_TIP
  const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
  const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
  inter_pred_params->dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
  inter_pred_params->dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - pre_y);
  inter_pred_params->dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
  inter_pred_params->dist_to_right_edge = GET_MV_SUBPEL(width - bw - pre_x);
#endif

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

// Note: grad_prec_bits param returned correspond to the precision
// of the gradient information in bits assuming gradient
// computed at unit pixel step normalization is 0 scale.
// Negative values indicate gradient returned at reduced precision, and
// positive values indicate gradient returned at higher precision.
void av1_compute_subpel_gradients_mc_highbd(
    MACROBLOCKD *xd, const MB_MODE_INFO *mi, int bw, int bh, int mi_x, int mi_y,
    uint16_t **mc_buf, InterPredParams *inter_pred_params,
    CalcSubpelParamsFunc calc_subpel_params_func, int ref, int *grad_prec_bits,
    int16_t *x_grad, int16_t *y_grad) {
  *grad_prec_bits = 3 - SUBPEL_GRAD_DELTA_BITS - 2;

  // Original predictor
  const MV mv_orig = mi->mv[ref].as_mv;
  MV mv_modified = mv_orig;
  uint16_t tmp_buf1[MAX_SB_SIZE * MAX_SB_SIZE] = { 0 };
  uint16_t tmp_buf2[MAX_SB_SIZE * MAX_SB_SIZE] = { 0 };
  // X gradient
  // Get predictor to the left
  mv_modified.col = mv_orig.col - (1 << (3 - SUBPEL_GRAD_DELTA_BITS));
  mv_modified.row = mv_orig.row;
  av1_build_one_inter_predictor(tmp_buf1, bw, &mv_modified, inter_pred_params,
                                xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
  // Get predictor to the right
  mv_modified.col = mv_orig.col + (1 << (3 - SUBPEL_GRAD_DELTA_BITS));
  mv_modified.row = mv_orig.row;
  av1_build_one_inter_predictor(tmp_buf2, bw, &mv_modified, inter_pred_params,
                                xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
  // Compute difference.
  // Note since the deltas are at +2^g/8 and -2^g/8 subpel locations
  // (g = 3 - SUBPEL_GRAD_DELTA_BITS), the actual unit pel gradient is
  // 4/2^g = 2^(2-g) times the difference. Therefore the gradient returned
  // is at reduced precision by 2-g bits. That explains the grad_prec_bits
  // return value of g-2 at the end of this function.

  aom_highbd_subtract_block(bh, bw, x_grad, bw, tmp_buf2, bw, tmp_buf1, bw,
                            xd->bd);

  // Y gradient
  // Get predictor below
  mv_modified.col = mv_orig.col;
  mv_modified.row = mv_orig.row - (1 << (3 - SUBPEL_GRAD_DELTA_BITS));
  av1_build_one_inter_predictor(tmp_buf1, bw, &mv_modified, inter_pred_params,
                                xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
  // Get predictor above
  mv_modified.col = mv_orig.col;
  mv_modified.row = mv_orig.row + (1 << (3 - SUBPEL_GRAD_DELTA_BITS));
  av1_build_one_inter_predictor(tmp_buf2, bw, &mv_modified, inter_pred_params,
                                xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
  // Compute difference.
  // Note since the deltas are at +2^g/8 and -2^g/8 subpel locations
  // (g = 3 - SUBPEL_GRAD_DELTA_BITS), the actual unit pel gradient is
  // 4/2^g = 2^(2-g) times the difference. Therefore the gradient returned
  // is at reduced precision by 2-g bits. That explains the grad_prec_bits
  // return value of g-2 at the end of this function.

  aom_highbd_subtract_block(bh, bw, y_grad, bw, tmp_buf2, bw, tmp_buf1, bw,
                            xd->bd);
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

#if OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
void av1_compute_subpel_gradients_interp(int16_t *pred_dst, int bw, int bh,
                                         int *grad_prec_bits, int16_t *x_grad,
                                         int16_t *y_grad) {
  // Reuse pixels in pred_dst to compute gradients
#if OPFL_BILINEAR_GRAD
  (void)is_hbd;
  av1_bilinear_grad_interpolation_c(pred_dst, x_grad, y_grad, bw, bh);
#else
  av1_bicubic_grad_interpolation_highbd(pred_dst, x_grad, y_grad, bw, bh);
#endif  // OPFL_BILINEAR_GRAD
  *grad_prec_bits = 3 - SUBPEL_GRAD_DELTA_BITS - 2;
}
#endif  // OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD

// Optical flow based mv refinement computation function:
//
// p0, pstride0: predictor 0 and its stride
// p1, pstride1: predictor 1 and its stride
// gx0, gy0: x and y gradients for p0
// gx1, gy1: x and y gradients for p1
// gstride: stride for all the gradients assumed to be the same
// bw, bh: block dimensions
// d0: distances of p0 to current frame, where positive value refers to p0
//     before the current frame.
// d1: distances of p1 to current frame, where positive value refers to p1
//     before the current frame.
// max_prec_bits: maximum offset in bits
// vx0, vy0: output high resolution mv offset for p0
// vx1, vy1: output high resolution mv offset for p1
void av1_opfl_mv_refinement_highbd(const uint16_t *p0, int pstride0,
                                   const uint16_t *p1, int pstride1,
                                   const int16_t *gx0, const int16_t *gy0,
                                   const int16_t *gx1, const int16_t *gy1,
                                   int gstride, int bw, int bh, int d0, int d1,
                                   int grad_prec_bits, int mv_prec_bits,
                                   int *vx0, int *vy0, int *vx1, int *vy1) {
  assert(IMPLIES(OPFL_DIST_RATIO_THR == 1, d0 + d1 == 0));
  int64_t su2 = 0;
  int64_t suv = 0;
  int64_t sv2 = 0;
  int64_t suw = 0;
  int64_t svw = 0;
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      const int64_t u = d0 * gx0[i * gstride + j] - d1 * gx1[i * gstride + j];
      const int64_t v = d0 * gy0[i * gstride + j] - d1 * gy1[i * gstride + j];
      const int64_t w = d0 * (p0[i * pstride0 + j] - p1[i * pstride1 + j]);
      su2 += (u * u);
      suv += (u * v);
      sv2 += (v * v);
      suw += (u * w);
      svw += (v * w);
    }
  }
  const int bits = mv_prec_bits + grad_prec_bits;
#if OPFL_REGULARIZED_LS
  const int rls_alpha = (bw * bh >> 4) << OPFL_RLS_PARAM_BITS;
  su2 += rls_alpha;
  sv2 += rls_alpha;
#endif

  // Clamp su2, sv2, suv, suw, and svw to avoid overflow in det, det_x, and
  // det_y
  su2 = (int64_t)clamp((int)su2, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  sv2 = (int64_t)clamp((int)sv2, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  suv = (int64_t)clamp((int)suv, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  suw = (int64_t)clamp((int)suw, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  svw = (int64_t)clamp((int)svw, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);

  // Solve 2x2 matrix inverse: [ su2  suv ]   [ vx0 ]     [ -suw ]
  //                           [ suv  sv2 ] * [ vy0 ]  =  [ -svw ]
  const int64_t det = su2 * sv2 - suv * suv;
  if (det == 0) return;
  const int64_t det_x = (suv * svw - sv2 * suw) * (1 << bits);
  const int64_t det_y = (suv * suw - su2 * svw) * (1 << bits);

  *vx0 = (int)divide_and_round_signed(det_x, det);
  *vy0 = (int)divide_and_round_signed(det_y, det);
  const int tx1 = (*vx0) * d1;
  const int ty1 = (*vy0) * d1;
  *vx1 = (int)divide_and_round_signed(tx1, d0);
  *vy1 = (int)divide_and_round_signed(ty1, d0);
}

#if OPFL_COMBINE_INTERP_GRAD_LS
// Solve vx and vy given pdiff = P0 - P1 and the gradients gx/gy of
// d0 * P0 - d1 * P1.
void av1_opfl_mv_refinement_interp_grad(const int16_t *pdiff, int pstride0,
                                        const int16_t *gx, const int16_t *gy,
                                        int gstride, int bw, int bh, int d0,
                                        int d1, int grad_prec_bits,
                                        int mv_prec_bits, int *vx0, int *vy0,
                                        int *vx1, int *vy1) {
  assert(IMPLIES(OPFL_DIST_RATIO_THR == 1, d0 + d1 == 0));
  int64_t su2 = 0;
  int64_t suv = 0;
  int64_t sv2 = 0;
  int64_t suw = 0;
  int64_t svw = 0;
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
#if OPFL_DOWNSAMP_QUINCUNX
      if ((i + j) % 2 == 1) continue;
#endif
      const int u = gx[i * gstride + j];
      const int v = gy[i * gstride + j];
      const int w = pdiff[i * pstride0 + j];
      su2 += (u * u);
      suv += (u * v);
      sv2 += (v * v);
      suw += (u * w);
      svw += (v * w);
    }
  }
  const int bits = mv_prec_bits + grad_prec_bits;
#if OPFL_REGULARIZED_LS
  const int rls_alpha = (bw * bh >> 4) << OPFL_RLS_PARAM_BITS;
  su2 += rls_alpha;
  sv2 += rls_alpha;
#endif

  // Clamp su2, sv2, suv, suw, and svw to avoid overflow in det, det_x, and
  // det_y
  su2 = (int64_t)clamp((int)su2, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  sv2 = (int64_t)clamp((int)sv2, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  suv = (int64_t)clamp((int)suv, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  suw = (int64_t)clamp((int)suw, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);
  svw = (int64_t)clamp((int)svw, -OPFL_COV_CLAMP_VAL, OPFL_COV_CLAMP_VAL);

  // Solve 2x2 matrix inverse: [ su2  suv ]   [ vx0 ]     [ -suw ]
  //                           [ suv  sv2 ] * [ vy0 ]  =  [ -svw ]
  const int64_t det = su2 * sv2 - suv * suv;
  if (det == 0) return;
  const int64_t det_x = (suv * svw - sv2 * suw) * (1 << bits);
  const int64_t det_y = (suv * suw - su2 * svw) * (1 << bits);

  *vx0 = (int)divide_and_round_signed(det_x, det);
  *vy0 = (int)divide_and_round_signed(det_y, det);
  const int tx1 = (*vx0) * d1;
  const int ty1 = (*vy0) * d1;
  *vx1 = (int)divide_and_round_signed(tx1, d0);
  *vy1 = (int)divide_and_round_signed(ty1, d0);
}
#endif  // OPFL_COMBINE_INTERP_GRAD_LS

int av1_opfl_mv_refinement_nxn_interp_grad_c(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int bw, int bh, int n, int d0, int d1, int grad_prec_bits,
    int mv_prec_bits, int *vx0, int *vy0, int *vx1, int *vy1) {
  assert(bw % n == 0 && bh % n == 0);
  int n_blocks = 0;
#if OPFL_COMBINE_INTERP_GRAD_LS
  for (int i = 0; i < bh; i += n) {
    for (int j = 0; j < bw; j += n) {
      av1_opfl_mv_refinement_interp_grad(
          pdiff + (i * pstride + j), pstride, gx + (i * gstride + j),
          gy + (i * gstride + j), gstride, n, n, d0, d1, grad_prec_bits,
          mv_prec_bits, vx0 + n_blocks, vy0 + n_blocks, vx1 + n_blocks,
          vy1 + n_blocks);
      n_blocks++;
    }
  }
#else
  (void)pdiff;
  (void)pstride;
  (void)gx;
  (void)gy;
  (void)gstride;
  (void)bw;
  (void)bh;
  (void)n;
  (void)d0;
  (void)d1;
  (void)grad_prec_bits;
  (void)mv_prec_bits;
  (void)vx0;
  (void)vy0;
  (void)vx1;
  (void)vy1;
#endif  // OPFL_COMBINE_INTERP_GRAD_LS
  return n_blocks;
}

// Function to compute optical flow offsets in nxn blocks
int av1_opfl_mv_refinement_nxn_highbd_c(const uint16_t *p0, int pstride0,
                                        const uint16_t *p1, int pstride1,
                                        const int16_t *gx0, const int16_t *gy0,
                                        const int16_t *gx1, const int16_t *gy1,
                                        int gstride, int bw, int bh, int n,
                                        int d0, int d1, int grad_prec_bits,
                                        int mv_prec_bits, int *vx0, int *vy0,
                                        int *vx1, int *vy1) {
  assert(bw % n == 0 && bh % n == 0);
  int n_blocks = 0;
  for (int i = 0; i < bh; i += n) {
    for (int j = 0; j < bw; j += n) {
      av1_opfl_mv_refinement_highbd(
          p0 + (i * pstride0 + j), pstride0, p1 + (i * pstride1 + j), pstride1,
          gx0 + (i * gstride + j), gy0 + (i * gstride + j),
          gx1 + (i * gstride + j), gy1 + (i * gstride + j), gstride, n, n, d0,
          d1, grad_prec_bits, mv_prec_bits, vx0 + n_blocks, vy0 + n_blocks,
          vx1 + n_blocks, vy1 + n_blocks);
      n_blocks++;
    }
  }
  return n_blocks;
}

#if OPFL_COMBINE_INTERP_GRAD_LS
static AOM_FORCE_INLINE void compute_pred_using_interp_grad_highbd(
    const uint16_t *src1, const uint16_t *src2, int16_t *dst1, int16_t *dst2,
    int bw, int bh, int d0, int d1) {
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
      // To avoid overflow, we clamp d0*P0-d1*P1 and P0-P1.
      int32_t tmp_dst =
          d0 * (int32_t)src1[i * bw + j] - d1 * (int32_t)src2[i * bw + j];
      dst1[i * bw + j] = clamp(tmp_dst, INT16_MIN, INT16_MAX);
      tmp_dst = d0 * ((int32_t)src1[i * bw + j] - (int32_t)src2[i * bw + j]);
      dst2[i * bw + j] = clamp(tmp_dst, INT16_MIN, INT16_MAX);
    }
  }
}
#endif  // OPFL_COMBINE_INTERP_GRAD_LS

void av1_copy_pred_array_highbd_c(const uint16_t *src1, const uint16_t *src2,
                                  int16_t *dst1, int16_t *dst2, int bw, int bh,
                                  int d0, int d1) {
#if OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
#if OPFL_COMBINE_INTERP_GRAD_LS
  compute_pred_using_interp_grad_highbd(src1, src2, dst1, dst2, bw, bh, d0, d1);
#else
  (void)src2;
  (void)dst2;
  (void)d0;
  (void)d1;
  for (int i = 0; i < bh; ++i)
    for (int j = 0; j < bw; ++j) dst1[i * bw + j] = (int16_t)src1[i * bw + j];
#endif  // OPFL_COMBINE_INTERP_GRAD_LS
#else
  (void)src1;
  (void)dst1;
  (void)src2;
  (void)dst2;
  (void)d0;
  (void)d1;
  (void)bw;
  (void)bh;
#endif  // OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
}

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
#if CONFIG_REFINEMV
    ,
    MV *best_mv_ref, int pu_width, int pu_height
#endif  // CONFIG_REFINEMV
) {
  const int target_prec = MV_REFINE_PREC_BITS;
  // Convert output MV to 1/16th pel
  assert(MV_REFINE_PREC_BITS >= 3);
#if CONFIG_OPTFLOW_ON_TIP
  const int num_mv = (mbmi->ref_frame[0] == TIP_FRAME) ? 4 : N_OF_OFFSETS;
#else
  const int num_mv = N_OF_OFFSETS;
#endif  // CONFIG_OPTFLOW_ON_TIP
  for (int mvi = 0; mvi < num_mv; mvi++) {
    mv_refined[mvi * 2].as_mv.row *= 1 << (MV_REFINE_PREC_BITS - 3);
    mv_refined[mvi * 2].as_mv.col *= 1 << (MV_REFINE_PREC_BITS - 3);
    mv_refined[mvi * 2 + 1].as_mv.row *= 1 << (MV_REFINE_PREC_BITS - 3);
    mv_refined[mvi * 2 + 1].as_mv.col *= 1 << (MV_REFINE_PREC_BITS - 3);
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
  if (d0 == 0 || d1 == 0) return target_prec;

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

  int n_blocks = 1;
  int grad_prec_bits;
  int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                                 ,
                                 use_4x4
#endif  // CONFIG_OPTFLOW_ON_TIP
  );

#if OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
  // Compute gradients of P0 and P1 with interpolation
#if OPFL_COMBINE_INTERP_GRAD_LS
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
  av1_copy_pred_array_highbd(dst0, dst1, tmp0, tmp1, bw, bh, d0, d1);
  // Buffers gx0 and gy0 are used to store the gradients of tmp0
  av1_compute_subpel_gradients_interp(tmp0, bw, bh, &grad_prec_bits, gx0, gy0);

  n_blocks = av1_opfl_mv_refinement_nxn_interp_grad(
      tmp1, bw, gx0, gy0, bw, bw, bh, n, d0, d1, grad_prec_bits, target_prec,
      vx0, vy0, vx1, vy1);

  aom_free(tmp0);
  aom_free(tmp1);
#else
  int16_t *tmp =
      (int16_t *)aom_memalign(16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(int16_t));
  av1_copy_pred_array_highbd(dst0, NULL, tmp, NULL, bw, bh, d0, d1);
  av1_compute_subpel_gradients_interp(tmp, bw, bh, &grad_prec_bits, gx0, gy0);

  av1_copy_pred_array_highbd(dst1, NULL, tmp, NULL, bw, bh, d0, d1);
  av1_compute_subpel_gradients_interp(tmp, bw, bh, &grad_prec_bits, gx1, gy1);

  n_blocks = av1_opfl_mv_refinement_nxn_highbd(
      dst0, bw, dst1, bw, gx0, gy0, gx1, gy1, bw, bw, bh, n, d0, d1,
      grad_prec_bits, target_prec, vx0, vy0, vx1, vy1);

  aom_free(tmp);
#endif  // OPFL_COMBINE_INTERP_GRAD_LS
#else
  // Compute gradients of P0 and P1 with MC
  av1_compute_subpel_gradients_mc_highbd(xd, mbmi, bw, bh, mi_x, mi_y, mc_buf,
                                         &params0, calc_subpel_params_func, 0,
                                         &grad_prec_bits, gx0, gy0);
  av1_compute_subpel_gradients_mc_highbd(xd, mbmi, bw, bh, mi_x, mi_y, mc_buf,
                                         &params1, calc_subpel_params_func, 1,
                                         &grad_prec_bits, gx1, gy1);

  n_blocks = av1_opfl_mv_refinement_nxn_highbd(
      dst0, bw, dst1, bw, gx0, gy0, gx1, gy1, bw, bw, bh, n, d0, d1,
      grad_prec_bits, target_prec, vx0, vy0, vx1, vy1);

#endif  // OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD

  for (int i = 0; i < n_blocks; i++) {
#if OPFL_CLAMP_MV_DELTA
    mv_refined[i * 2].as_mv.row +=
        clamp(vy0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    mv_refined[i * 2].as_mv.col +=
        clamp(vx0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    mv_refined[i * 2 + 1].as_mv.row +=
        clamp(vy1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    mv_refined[i * 2 + 1].as_mv.col +=
        clamp(vx1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
#else
    mv_refined[i * 2].as_mv.row += vy0[i];
    mv_refined[i * 2].as_mv.col += vx0[i];
    mv_refined[i * 2 + 1].as_mv.row += vy1[i];
    mv_refined[i * 2 + 1].as_mv.col += vx1[i];
#endif
  }

  return target_prec;
}

// Makes the interpredictor for the region by dividing it up into nxn blocks
// and running the interpredictor code on each one.
void make_inter_pred_of_nxn(uint16_t *dst, int dst_stride,
                            int_mv *const mv_refined,
                            InterPredParams *inter_pred_params, MACROBLOCKD *xd,
                            int mi_x, int mi_y, int ref, uint16_t **mc_buf,
                            CalcSubpelParamsFunc calc_subpel_params_func, int n,
                            SubpelParams *subpel_params) {
  int n_blocks = 0;
  int w = inter_pred_params->orig_block_width;
  int h = inter_pred_params->orig_block_height;
  assert(w % n == 0);
  assert(h % n == 0);
  CONV_BUF_TYPE *orig_conv_dst = inter_pred_params->conv_params.dst;
  inter_pred_params->block_width = n;
  inter_pred_params->block_height = n;

  uint16_t *pre;
  int src_stride = 0;

  // Process whole nxn blocks.
  for (int j = 0; j <= h - n; j += n) {
    for (int i = 0; i <= w - n; i += n) {
      calc_subpel_params_func(&(mv_refined[n_blocks * 2 + ref].as_mv),
                              inter_pred_params, xd, mi_x + i, mi_y + j, ref, 1,
                              mc_buf, &pre, subpel_params, &src_stride);
      av1_make_inter_predictor(pre, src_stride, dst, dst_stride,
                               inter_pred_params, subpel_params);
      n_blocks++;
      dst += n;
      inter_pred_params->conv_params.dst += n;
      inter_pred_params->pix_col += n;
    }
    dst -= w;
    inter_pred_params->conv_params.dst -= w;
    inter_pred_params->pix_col -= w;

    dst += n * dst_stride;
    inter_pred_params->conv_params.dst +=
        n * inter_pred_params->conv_params.dst_stride;
    inter_pred_params->pix_row += n;
  }

  inter_pred_params->conv_params.dst = orig_conv_dst;
}

// Use a second pass of motion compensation to rebuild inter predictor
void av1_opfl_rebuild_inter_predictor(
    uint16_t *dst, int dst_stride, int plane, int_mv *const mv_refined,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
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
  make_inter_pred_of_nxn(dst, dst_stride, mv_refined, inter_pred_params, xd,
                         mi_x, mi_y, ref, mc_buf, calc_subpel_params_func, n,
                         &subpel_params);
}
#endif  // CONFIG_OPTFLOW_REFINEMENT

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
  const int ssy = inter_pred_params->subsampling_y;
  const int ssx = inter_pred_params->subsampling_x;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  const int mask_stride = block_size_wide[sb_type];
  aom_highbd_blend_a64_d16_mask(dst, dst_stride, src0, src0_stride, src1,
                                src1_stride, mask, mask_stride, w, h, ssx, ssy,
                                &inter_pred_params->conv_params,
                                inter_pred_params->bit_depth);
}

static void make_masked_inter_predictor(const uint16_t *pre, int pre_stride,
                                        uint16_t *dst, int dst_stride,
                                        InterPredParams *inter_pred_params,
                                        const SubpelParams *subpel_params) {
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
  build_masked_compound_no_round(
      dst, dst_stride, org_dst, org_dst_stride, tmp_buf16, tmp_buf_stride,
      comp_data, sb_type, inter_pred_params->block_height,
      inter_pred_params->block_width, inter_pred_params);
}

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

  if (inter_pred_params->comp_mode == UNIFORM_SINGLE ||
      inter_pred_params->comp_mode == UNIFORM_COMP) {
    av1_make_inter_predictor(src, src_stride, dst, dst_stride,
                             inter_pred_params, &subpel_params);
  } else {
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params);
  }
}

#if CONFIG_BAWP
// Derive the scaling factor and offset of block adaptive weighted prediction
// mode. One row from the top boundary and one column from the left boundary
// are used in the less square error process.
void derive_bawp_parameters(MACROBLOCKD *xd, uint16_t *recon_top,
                            uint16_t *recon_left, int rec_stride,
                            uint16_t *ref_top, uint16_t *ref_left,
                            int ref_stride, int ref, int plane, int bw,
                            int bh) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  assert(mbmi->bawp_flag == 1);
  // only integer position of reference, may need to consider
  // fractional position of ref samples
  int count = 0;
  int sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;

  if (xd->up_available) {
    for (int i = 0; i < bw; ++i) {
      sum_x += ref_top[i];
      sum_y += recon_top[i];
      sum_xy += ref_top[i] * recon_top[i];
      sum_xx += ref_top[i] * ref_top[i];
    }
    count += bw;
  }

  if (xd->left_available) {
    for (int i = 0; i < bh; ++i) {
      sum_x += ref_left[0];
      sum_y += recon_left[0];
      sum_xy += ref_left[0] * recon_left[0];
      sum_xx += ref_left[0] * ref_left[0];

      recon_left += rec_stride;
      ref_left += ref_stride;
    }
    count += bh;
  }

  const int16_t shift = 8;  // maybe a smaller value can be used
  if (count > 0) {
    int32_t der = sum_xx - (int32_t)((int64_t)sum_x * sum_x / count);
    int32_t nor = sum_xy - (int32_t)((int64_t)sum_x * sum_y / count);
    // Add a small portion to both self-correlation and cross-correlation to
    // keep mode stable and have scaling factor leaning to value 1.0
    // Temporal design, to be further updated
    nor += der / 16;
    der += der / 16;

    if (nor && der)
      mbmi->bawp_alpha[plane][ref] = resolve_divisor_32_CfL(nor, der, shift);
    else
      mbmi->bawp_alpha[plane][ref] = 1 << shift;
    mbmi->bawp_beta[plane][ref] =
        ((sum_y << shift) - sum_x * mbmi->bawp_alpha[plane][ref]) / count;
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
                                inter_pred_params, &subpel_params);
  }

  int shift = 8;
  MB_MODE_INFO *mbmi = xd->mi[0];
  int x_off = mbmi->mv[ref].as_mv.col >> 3;
  int y_off = mbmi->mv[ref].as_mv.row >> 3;

  int ref_w = bw;
  if (mi_x + bw >= cm->width) ref_w = cm->width - mi_x;
  int ref_h = bh;
  if (mi_y + bh >= cm->height) ref_h = cm->height - mi_y;

  if (mi_x + x_off - BAWP_REF_LINES < 0 || mi_y + y_off - BAWP_REF_LINES < 0 ||
      mi_x + ref_w + x_off >= cm->width || mi_y + ref_h + y_off >= cm->height) {
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
    const int ref_stride = pd->pre[ref].stride;
    uint16_t *ref_buf = pd->pre[ref].buf + y_off * ref_stride + x_off;
    uint16_t *ref_top = ref_buf - BAWP_REF_LINES * ref_stride;
    uint16_t *ref_left = ref_buf - BAWP_REF_LINES;

    derive_bawp_parameters(xd, recon_top, recon_left, recon_stride, ref_top,
                           ref_left, ref_stride, ref, plane, ref_w, ref_h);
  }

  int16_t alpha = mbmi->bawp_alpha[plane][ref];
  int32_t beta = mbmi->bawp_beta[plane][ref];
  for (int j = 0; j < ref_h; ++j) {
    for (int i = 0; i < ref_w; ++i) {
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
static bool is_sub8x8_inter(const MACROBLOCKD *xd, const MB_MODE_INFO *mi,
                            int plane, int is_intrabc, int build_for_obmc) {
  if (is_intrabc || build_for_obmc) {
    return false;
  }

  if (!(plane &&
        (mi->sb_type[PLANE_TYPE_UV] != mi->chroma_ref_info.bsize_base)))
    return false;

  // For sub8x8 chroma blocks, we may be covering more than one luma block's
  // worth of pixels. Thus (mi_x, mi_y) may not be the correct coordinates for
  // the top-left corner of the prediction source - the correct top-left corner
  // is at (pre_x, pre_y).
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  const int row_start =
      plane ? mi->chroma_ref_info.mi_row_chroma_base - mi_row : 0;
  const int col_start =
      plane ? mi->chroma_ref_info.mi_col_chroma_base - mi_col : 0;

  for (int row = row_start; row <= 0; ++row) {
    for (int col = col_start; col <= 0; ++col) {
      const MB_MODE_INFO *this_mbmi = xd->mi[row * xd->mi_stride + col];
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
  const BLOCK_SIZE bsize = mi->sb_type[PLANE_TYPE_Y];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const bool ss_x = pd->subsampling_x;
  const bool ss_y = pd->subsampling_y;
  const int b4_w = block_size_wide[bsize] >> ss_x;
  const int b4_h = block_size_high[bsize] >> ss_y;
  const BLOCK_SIZE plane_bsize = plane ? mi->chroma_ref_info.bsize_base : bsize;
  const int b8_w = block_size_wide[plane_bsize] >> ss_x;
  const int b8_h = block_size_high[plane_bsize] >> ss_y;
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

  int row = row_start;
  for (int y = 0; y < b8_h; y += b4_h) {
    int col = col_start;
    for (int x = 0; x < b8_w; x += b4_w) {
      MB_MODE_INFO *this_mbmi = xd->mi[row * xd->mi_stride + col];
#if CONFIG_EXT_RECUR_PARTITIONS
      // TODO(yuec): enabling compound prediction in none sub8x8 mbs in the
      // group
      bool is_compound = 0;
#else
      bool is_compound = has_second_ref(this_mbmi);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      struct buf_2d *const dst_buf = &pd->dst;
      uint16_t *dst = dst_buf->buf + dst_buf->stride * y + x;
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
      av1_init_inter_params(&inter_pred_params, b4_w, b4_h, pre_y + y,
                            pre_x + x, pd->subsampling_x, pd->subsampling_y,
                            xd->bd, mi->use_intrabc[0], sf, &pre_buf,
                            this_mbmi->interp_fltr);
      inter_pred_params.conv_params =
          get_conv_params_no_round(ref, plane, NULL, 0, is_compound, xd->bd);

      av1_build_one_inter_predictor(dst, dst_buf->stride, &mv,
                                    &inter_pred_params, xd, mi_x + x, mi_y + y,
                                    ref, mc_buf, calc_subpel_params_func);

      col += mi_size_wide[bsize];
    }
    row += mi_size_high[bsize];
  }
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
#if CONFIG_OPTFLOW_REFINEMENT || CONFIG_TIP
  // Extension is needed in optical flow refinement to obtain MV offsets
  (void)scaled_mv;
  if (!is_intrabc && !do_warp) {
#else
  const int is_scaled = av1_is_scaled(sf);
  if ((!is_intrabc) && (!do_warp) &&
      (is_scaled || scaled_mv.col || scaled_mv.row || (frame_width & 0x7) ||
       (frame_height & 0x7))) {
#endif  // CONFIG_OPTFLOW_REFINEMENT || CONFIG_TIP
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

#if CONFIG_TIP
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

    // Get reference block bottom right coordinate.
    block->x1 =
        ((pos_x + (bw - 1) * subpel_params->xs) >> SCALE_SUBPEL_BITS) + 1;
    block->y1 =
        ((pos_y + (bh - 1) * subpel_params->ys) >> SCALE_SUBPEL_BITS) + 1;

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
    block->x1 = pos_x + bw;
    block->y1 = pos_y + bh;

    scaled_mv->row = mv_q4.row;
    scaled_mv->col = mv_q4.col;
    *subpel_x_mv = scaled_mv->col & SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SUBPEL_MASK;
  }
  *pre = pre_buf->buf0 + block->y0 * pre_buf->stride + block->x0;
  *src_stride = pre_buf->stride;
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
#endif

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
                              int *src_stride, ReferenceArea *ref_area,
                              int is_tip) {
  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;

  if (is_tip) {
    tip_dec_calc_subpel_params(src_mv, inter_pred_params, mi_x, mi_y, pre,
                               subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                               use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                               &scaled_mv, &subpel_x_mv, &subpel_y_mv);

  } else {
    dec_calc_subpel_params(src_mv, inter_pred_params, xd, mi_x, mi_y, pre,
                           subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                           use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                           &scaled_mv, &subpel_x_mv, &subpel_y_mv);
  }

  struct buf_2d *const pre_buf = &inter_pred_params->ref_frame_buf;
  int frame_height = pre_buf->height;
  int frame_width = pre_buf->width;
  block.x0 -= REF_LEFT_BORDER;
  block.x1 += REF_RIGHT_BORDER;
  block.y0 -= REF_TOP_BORDER;
  block.y1 += REF_BOTTOM_BORDER;

  ref_area->pad_block.x0 = CLIP(block.x0, 0, frame_width - 1);
  ref_area->pad_block.y0 = CLIP(block.y0, 0, frame_height - 1);
  ref_area->pad_block.x1 = CLIP(block.x1, 0, frame_width);
  ref_area->pad_block.y1 = CLIP(block.y1, 0, frame_height);
}

void av1_get_reference_area_with_padding(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         int plane, MB_MODE_INFO *mi, int bw,
                                         int bh, int mi_x, int mi_y,
                                         ReferenceArea ref_area[2],
                                         const int comp_pixel_x,
                                         const int comp_pixel_y) {
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

  const int pre_x = is_tip
                        ? comp_pixel_x
                        : ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = is_tip
                        ? comp_pixel_y
                        : ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf =
        is_tip ? cm->tip_ref.ref_scale_factor[ref]
               : xd->block_ref_scale_factors[ref];
    const struct buf_2d *const pre_buf =
        is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref] : &pd->pre[ref];

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
                          is_tip ? MULTITAP_SHARP : mi->interp_fltr);

    inter_pred_params.original_pu_width = bw;
    inter_pred_params.original_pu_height = bh;

#if CONFIG_TIP
    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params.dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
    inter_pred_params.dist_to_bottom_edge = GET_MV_SUBPEL(height - bh - pre_y);
    inter_pred_params.dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
    inter_pred_params.dist_to_right_edge = GET_MV_SUBPEL(width - bw - pre_x);
#endif

    SubpelParams subpel_params;
    uint16_t *src;
    int src_stride;

    assert(!inter_pred_params.use_ref_padding);

    MV *src_mv = ref == 0 ? &mi->mv[0].as_mv : &mi->mv[1].as_mv;
    get_ref_area_info(src_mv, &inter_pred_params, xd, mi_x, mi_y,
#if CONFIG_OPTFLOW_REFINEMENT
                      0, /* use_optflow_refinement */
#endif                   // CONFIG_OPTFLOW_REFINEMENT
                      &src, &subpel_params, &src_stride, &ref_area[ref],
                      is_tip);
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
                         CalcSubpelParamsFunc calc_subpel_params_func,
                         int pre_x, int pre_y, uint16_t *dst_ref0,
                         uint16_t *dst_ref1, MV *best_mv_ref, int pu_width,
                         int pu_height) {
  // initialize basemv as best MV
  best_mv_ref[0] = mi->mv[0].as_mv;
  best_mv_ref[1] = mi->mv[1].as_mv;

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
    const struct buf_2d *const pre_buf =
        is_tip ? &cm->tip_ref.tip_plane[plane].pred[ref]
               : (is_intrabc ? dst_buf : &pd->pre[ref]);

    av1_init_inter_params(&inter_pred_params[ref], bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, BILINEAR);

#if CONFIG_REFINEMV
    inter_pred_params[ref].original_pu_width = pu_width;
    inter_pred_params[ref].original_pu_height = pu_height;
#endif  // CONFIG_REFINEMV

#if CONFIG_TIP
    const int width = (cm->mi_params.mi_cols << MI_SIZE_LOG2);
    const int height = (cm->mi_params.mi_rows << MI_SIZE_LOG2);
    inter_pred_params[ref].dist_to_top_edge = -GET_MV_SUBPEL(pre_y);
    inter_pred_params[ref].dist_to_bottom_edge =
        GET_MV_SUBPEL(height - bh - pre_y);
    inter_pred_params[ref].dist_to_left_edge = -GET_MV_SUBPEL(pre_x);
    inter_pred_params[ref].dist_to_right_edge =
        GET_MV_SUBPEL(width - bw - pre_x);
#endif

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
  assert(mi->refinemv_flag);

  // If we signal the refinemv_flags we do not select sad0
  // Set sad0 a large value so that it does not be selected
  int sad0 = switchable_refinemv_flags
                 ? (INT32_MAX >> 1)
                 : av1_refinemv_build_predictors_and_get_sad(
                       xd, bw, bh, mi_x, mi_y, mc_buf, calc_subpel_params_func,
                       dst_ref0, dst_ref1, center_mvs[0], center_mvs[1],
                       inter_pred_params);

  assert(IMPLIES(mi->ref_frame[0] == TIP_FRAME, bw == 8 && bh == 8));
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

static void build_inter_predictors_8x8_and_bigger_refinemv(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    int build_for_obmc, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst, int dst_stride,
    int pu_width, int pu_height, uint16_t *dst0_16_refinemv,
    uint16_t *dst1_16_refinemv, int16_t *opt_gx0, int16_t *opt_gx1,
    int row_start, int col_start, MV *sb_refined_mv, MV *chroma_refined_mv,
    int build_for_refine_mv_only, ReferenceArea ref_area[2]) {
  const int is_compound = has_second_ref(mi);
  struct macroblockd_plane *const pd = &xd->plane[plane];
  assert(!is_intrabc_block(mi, xd->tree_type));
  assert(is_compound);
  assert(!mi->bawp_flag);
  assert(!build_for_obmc);
  assert(!is_masked_compound_type(mi->interinter_comp.type));
  assert(!is_tip_ref_frame(mi->ref_frame[0]));

#if CONFIG_CWP
  assert(mi->cwp_idx == CWP_EQUAL);
#endif

  int is_global[2] = { 0, 0 };
  for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_TIP
    if (!is_tip_ref_frame(mi->ref_frame[ref])) {
#endif  // CONFIG_TIP
      const WarpedMotionParams *const wm =
          &xd->global_motion[mi->ref_frame[ref]];
      is_global[ref] = is_global_mv_block(mi, wm->wmtype);
#if CONFIG_TIP
    }
#endif  // CONFIG_TIP
  }

  assert(!is_global[0] && !is_global[1]);

  const int pre_x = (mi_x + MI_SIZE * col_start) >> pd->subsampling_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> pd->subsampling_y;

  int apply_refinemv = (plane == 0);

  MV best_mv_ref[2] = { { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
                        { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col } };
  if (apply_refinemv) {
    uint16_t *dst_ref0 = NULL, *dst_ref1 = NULL;
    dst_ref0 = &dst0_16_refinemv[0];
    dst_ref1 = &dst1_16_refinemv[0];

    assert(IMPLIES(!mi->skip_mode,
                   is_refinemv_allowed(cm, mi, mi->sb_type[PLANE_TYPE_Y])));
    assert(IMPLIES(mi->skip_mode, is_refinemv_allowed_skip_mode(cm, mi)));
    apply_mv_refinement(cm, xd, plane, mi, bw, bh, mi_x, mi_y, mc_buf,
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
  } else {
    best_mv_ref[0] = chroma_refined_mv[0];
    best_mv_ref[1] = chroma_refined_mv[1];
  }

#if CONFIG_OPTFLOW_REFINEMENT
  int_mv mv_refined[2 * N_OF_OFFSETS];
  const int use_optflow_refinement =
      (mi->mode >= NEAR_NEARMV_OPTFLOW ||
       (cm->features.opfl_refine_type == REFINE_ALL &&
        mi->mode != GLOBAL_GLOBALMV &&
        mi->interinter_comp.type == COMPOUND_AVERAGE)) &&
      is_compound && is_opfl_refine_allowed(cm, mi);
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));
  assert(IMPLIES(use_optflow_refinement, !build_for_obmc));

  // Optical flow refinement with masked comp types or with non-sharp
  // interpolation filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
  assert(IMPLIES(use_optflow_refinement && mi->interp_fltr != MULTITAP_SHARP,
                 cm->features.opfl_refine_type == REFINE_ALL));

  // Arrays to hold optical flow offsets.
  int vx0[N_OF_OFFSETS] = { 0 };
  int vx1[N_OF_OFFSETS] = { 0 };
  int vy0[N_OF_OFFSETS] = { 0 };
  int vy1[N_OF_OFFSETS] = { 0 };

  // Pointers to gradient and dst buffers
  int16_t *gx0, *gy0, *gx1, *gy1;
  uint16_t *dst0 = NULL, *dst1 = NULL;

  if (use_optflow_refinement && plane == 0) {
    // Allocate gradient and dst buffers
    // gx0 = aom_memalign(32, 2 * MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*gx0));
    // gx1 = aom_memalign(32, 2 * MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*gx1));
    gx0 = &opt_gx0[0];
    gx1 = &opt_gx1[0];
    gy0 = gx0 + (REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT);
    gy1 = gx1 + (REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT);

    // Initialize refined mv
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];

    for (int mvi = 0; mvi < N_OF_OFFSETS; mvi++) {
      mv_refined[mvi * 2].as_mv = mv0;
      mv_refined[mvi * 2 + 1].as_mv = mv1;
    }
    // Refine MV using optical flow. The final output MV will be in 1/16
    // precision.
    dst0 = &dst0_16_refinemv[0];
    dst1 = &dst1_16_refinemv[0];
    // dst0 = aom_calloc(1, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));
    // dst1 = aom_calloc(1, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));

    av1_get_optflow_based_mv_highbd(cm, xd, plane, mi, mv_refined, bw, bh, mi_x,
                                    mi_y, mc_buf, calc_subpel_params_func, gx0,
                                    gy0, gx1, gy1, vx0, vy0, vx1, vy1, dst0,
                                    dst1
#if CONFIG_OPTFLOW_ON_TIP
                                    ,
                                    1, 1
#endif  // CONFIG_OPTFLOW_ON_TIP
                                    ,
                                    best_mv_ref, pu_width, pu_height);
  }
#endif  // CONFIG_OPTFLOW_REFINEMENT

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf = xd->block_ref_scale_factors[ref];
    struct buf_2d *const pre_buf = &pd->pre[ref];

    const MV mv = best_mv_ref[ref];
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };
    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);

#if CONFIG_REFINEMV
    inter_pred_params.use_ref_padding = 1;
    inter_pred_params.ref_area = &ref_area[ref];
#endif  // CONFIG_REFINEMV

    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;

    if (is_compound) av1_init_comp_mode(&inter_pred_params);
    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    if (!build_for_obmc)
      av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);

#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement && plane == 0) {
      int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                                     ,
                                     1
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);

      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);

      av1_opfl_rebuild_inter_predictor(dst, dst_stride, plane, mv_refined,
                                       &inter_pred_params, xd, mi_x, mi_y, ref,
                                       mc_buf, calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
                                       ,
                                       1
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
      continue;
    }
#endif  // CONFIG_OPTFLOW_REFINEMENT

    av1_build_one_inter_predictor(dst, dst_stride, &mv, &inter_pred_params, xd,
                                  mi_x, mi_y, ref, mc_buf,
                                  calc_subpel_params_func);
  }

#if CONFIG_PEF
  if (use_optflow_refinement && plane == 0) {
    enhance_prediction(cm, xd, plane, dst, dst_stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                       ,
                       mv_refined, use_optflow_refinement
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_REFINEMV
                       ,
                       0, NULL
#endif  // CONFIG_REFINEMV
    );
  }
#endif  // CONFIG_PEF
}

#endif  // CONFIG_REFINEMV

static void build_inter_predictors_8x8_and_bigger(
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
  const int is_compound = has_second_ref(mi);
  const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
  assert(IMPLIES(is_intrabc, !is_compound));
  struct macroblockd_plane *const pd = &xd->plane[plane];
  struct buf_2d *const dst_buf = &pd->dst;
  uint16_t *const dst = dst_buf->buf;

#if CONFIG_REFINEMV
  assert(IMPLIES(mi->refinemv_flag, !is_intrabc));
  assert(IMPLIES(mi->refinemv_flag && !build_for_obmc, is_compound));
  assert(IMPLIES(
      !build_for_obmc && mi->refinemv_flag && switchable_refinemv_flag(cm, mi),
      mi->interinter_comp.type == COMPOUND_AVERAGE));
  assert(IMPLIES(mi->refinemv_flag, mi->bawp_flag == 0));
  assert(IMPLIES(mi->refinemv_flag, mi->interp_fltr == MULTITAP_SHARP));

  int apply_sub_block_refinemv = mi->refinemv_flag && (!build_for_obmc) &&
                                 !is_tip_ref_frame(mi->ref_frame[0]);

  if (apply_sub_block_refinemv && default_refinemv_modes(mi))
    apply_sub_block_refinemv &= (mi->comp_group_idx == 0 &&
                                 mi->interinter_comp.type == COMPOUND_AVERAGE);

  if (apply_sub_block_refinemv) {
#if CONFIG_CWP
    assert(IMPLIES(mi->refinemv_flag, mi->cwp_idx == CWP_EQUAL));
#endif
    int refinemv_sb_size_width =
        AOMMIN((REFINEMV_SUBBLOCK_WIDTH >> pd->subsampling_x), bw);
    int refinemv_sb_size_height =
        AOMMIN(REFINEMV_SUBBLOCK_HEIGHT >> pd->subsampling_y, bh);
    uint16_t
        dst0_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
    uint16_t
        dst1_16_refinemv[REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT];
    DECLARE_ALIGNED(
        32, int16_t,
        opt_gx0[2 * REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT]);
    DECLARE_ALIGNED(
        32, int16_t,
        opt_gx1[2 * REFINEMV_SUBBLOCK_WIDTH * REFINEMV_SUBBLOCK_HEIGHT]);

    ReferenceArea ref_area[2];
    av1_get_reference_area_with_padding(cm, xd, plane, mi, bw, bh, mi_x, mi_y,
                                        ref_area, 0, 0);

    int dst_stride = dst_buf->stride;
    CONV_BUF_TYPE *tmp_conv_dst = xd->tmp_conv_dst;
    assert(bw % refinemv_sb_size_width == 0);
    assert(bh % refinemv_sb_size_height == 0);
    for (int h = 0; h < bh; h += refinemv_sb_size_height) {
      for (int w = 0; w < bw; w += refinemv_sb_size_width) {
        dst_buf->buf = dst + h * dst_stride + w;
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
        // mi_x, and mi_y are the top-left position of the luma samples of the
        // sub-block
        build_inter_predictors_8x8_and_bigger_refinemv(
            cm, xd, plane, mi, build_for_obmc, refinemv_sb_size_width,
            refinemv_sb_size_height, mi_x + w * (1 << pd->subsampling_x),
            mi_y + h * (1 << pd->subsampling_y), mc_buf,
            calc_subpel_params_func, dst_buf->buf, dst_stride, bw, bh,
            dst0_16_refinemv, dst1_16_refinemv, opt_gx0, opt_gx1, row_start,
            col_start, plane == 0 ? luma_refined_mv : NULL, chroma_refined_mv,
            build_for_refine_mv_only, ref_area);

        if (plane == 0) {
          REFINEMV_SUBMB_INFO *refinemv_subinfo =
              &xd->refinemv_subinfo[(h >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                    (w >> MI_SIZE_LOG2)];
          fill_subblock_refine_mv(refinemv_subinfo, refinemv_sb_size_width,
                                  refinemv_sb_size_height, luma_refined_mv[0],
                                  luma_refined_mv[1]);
        }
      }
    }

#if CONFIG_PEF
    enhance_prediction(cm, xd, plane, dst, dst_stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                       ,
                       NULL, 0
#endif  // CONFIG_OPTFLOW_REFINEMENT
                       ,
                       apply_sub_block_refinemv, &xd->refinemv_subinfo[0]);
#endif  // CONFIG_PEF
    dst_buf->buf = dst;
    xd->tmp_conv_dst = tmp_conv_dst;
    return;
  }
#endif  // CONFIG_REFINEMV

  int is_global[2] = { 0, 0 };
  for (int ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_TIP
    if (!is_tip_ref_frame(mi->ref_frame[ref])) {
#endif  // CONFIG_TIP
      const WarpedMotionParams *const wm =
          &xd->global_motion[mi->ref_frame[ref]];
      is_global[ref] = is_global_mv_block(mi, wm->wmtype);
#if CONFIG_TIP
    }
#endif  // CONFIG_TIP
  }

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
  MV best_mv_ref[2] = { { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
                        { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col } };
#endif  // CONFIG_REFINEMV
#if CONFIG_OPTFLOW_REFINEMENT
  int_mv mv_refined[2 * N_OF_OFFSETS];
  const int use_optflow_refinement =
      (mi->mode >= NEAR_NEARMV_OPTFLOW ||
       (cm->features.opfl_refine_type == REFINE_ALL &&
        mi->mode != GLOBAL_GLOBALMV &&
#if CONFIG_CWP
        mi->cwp_idx == CWP_EQUAL &&
#endif  // CONFIG_CWP
        mi->interinter_comp.type == COMPOUND_AVERAGE)) &&
      is_compound && is_opfl_refine_allowed(cm, mi);
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));
  assert(IMPLIES(use_optflow_refinement, !build_for_obmc));

  // Optical flow refinement with masked comp types or with non-sharp
  // interpolation filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
  assert(IMPLIES(use_optflow_refinement && mi->interp_fltr != MULTITAP_SHARP,
                 cm->features.opfl_refine_type == REFINE_ALL));

  // Arrays to hold optical flow offsets.
  int vx0[N_OF_OFFSETS] = { 0 };
  int vx1[N_OF_OFFSETS] = { 0 };
  int vy0[N_OF_OFFSETS] = { 0 };
  int vy1[N_OF_OFFSETS] = { 0 };

  // Pointers to gradient and dst buffers
  int16_t *gx0, *gy0, *gx1, *gy1;
  uint16_t *dst0 = NULL, *dst1 = NULL;

  if (use_optflow_refinement && plane == 0) {
    // Allocate gradient and dst buffers
    gx0 = aom_memalign(32, 2 * MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*gx0));
    gx1 = aom_memalign(32, 2 * MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*gx1));
    gy0 = gx0 + (MAX_SB_SIZE * MAX_SB_SIZE);
    gy1 = gx1 + (MAX_SB_SIZE * MAX_SB_SIZE);

    // Initialize refined mv
#if CONFIG_REFINEMV
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];
#else
      const MV mv0 = mi->mv[0].as_mv;
      const MV mv1 = mi->mv[1].as_mv;
#endif  // CONFIG_REFINEMV
    for (int mvi = 0; mvi < N_OF_OFFSETS; mvi++) {
      mv_refined[mvi * 2].as_mv = mv0;
      mv_refined[mvi * 2 + 1].as_mv = mv1;
    }
    // Refine MV using optical flow. The final output MV will be in 1/16
    // precision.
    dst0 = aom_calloc(1, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));
    dst1 = aom_calloc(1, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));
    av1_get_optflow_based_mv_highbd(cm, xd, plane, mi, mv_refined, bw, bh, mi_x,
                                    mi_y, mc_buf, calc_subpel_params_func, gx0,
                                    gy0, gx1, gy1, vx0, vy0, vx1, vy1, dst0,
                                    dst1
#if CONFIG_OPTFLOW_ON_TIP
                                    ,
                                    1, 1
#endif  // CONFIG_OPTFLOW_ON_TIP
#if CONFIG_REFINEMV
                                    ,
                                    best_mv_ref, bw, bh
#endif  // CONFIG_REFINEMV
    );
    aom_free(dst0);
    aom_free(dst1);
    aom_free(gx0);
    aom_free(gx1);
  }
#endif  // CONFIG_OPTFLOW_REFINEMENT

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf =
        is_intrabc ? &cm->sf_identity : xd->block_ref_scale_factors[ref];
    struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
    const MV mv = mi->mv[ref].as_mv;
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
    if (is_compound) av1_init_comp_mode(&inter_pred_params);
    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    if (!build_for_obmc)
      av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);

    if (is_masked_compound_type(mi->interinter_comp.type)) {
      inter_pred_params.sb_type = mi->sb_type[PLANE_TYPE_Y];
      inter_pred_params.mask_comp = mi->interinter_comp;
      if (ref == 1) {
        inter_pred_params.conv_params.do_average = 0;
        inter_pred_params.comp_mode = MASK_COMP;
      }
      // Assign physical buffer.
      inter_pred_params.mask_comp.seg_mask = xd->seg_mask;
    }

#if CONFIG_CWP
    if (ref == 1 && inter_pred_params.conv_params.do_average == 1) {
      if (get_cwp_idx(mi) != CWP_EQUAL) {
        int8_t weight = get_cwp_idx(mi);
        assert(mi->cwp_idx >= CWP_MIN && mi->cwp_idx <= CWP_MAX);
        inter_pred_params.conv_params.fwd_offset = weight;
        inter_pred_params.conv_params.bck_offset =
            (1 << CWP_WEIGHT_BITS) - weight;
      }
    }
#endif  // CONFIG_CWP

#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement && plane == 0) {
      int n = opfl_get_subblock_size(bw, bh, plane
#if CONFIG_OPTFLOW_ON_TIP
                                     ,
                                     1
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);
      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr, n);
      av1_opfl_rebuild_inter_predictor(dst, dst_buf->stride, plane, mv_refined,
                                       &inter_pred_params, xd, mi_x, mi_y, ref,
                                       mc_buf, calc_subpel_params_func
#if CONFIG_OPTFLOW_ON_TIP
                                       ,
                                       1
#endif  // CONFIG_OPTFLOW_ON_TIP
      );
      continue;
    }
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_BAWP
    if (mi->bawp_flag == 1 && plane == 0 && !build_for_obmc) {
      av1_build_one_bawp_inter_predictor(
          dst, dst_buf->stride, &mv, &inter_pred_params, cm, xd, dst_orig, bw,
          bh, mi_x, mi_y, ref, plane, mc_buf, calc_subpel_params_func);
      continue;
    }
#endif  // CONFIG_BAWP
    av1_build_one_inter_predictor(dst, dst_buf->stride, &mv, &inter_pred_params,
                                  xd, mi_x, mi_y, ref, mc_buf,
                                  calc_subpel_params_func);
  }
#if CONFIG_PEF
  enhance_prediction(cm, xd, plane, dst, dst_buf->stride, bw, bh
#if CONFIG_OPTFLOW_REFINEMENT
                     ,
                     mv_refined, use_optflow_refinement
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
                     ,
                     0, NULL
#endif  // CONFIG_REFINEMV
  );
#endif  // CONFIG_PEF
}

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
#if CONFIG_WARPMV
  // just for debugging purpose
  // Can be removed later on
  if (mi->mode == WARPMV) {
    assert(mi->ref_mv_idx == 0);
    assert(mi->motion_mode == WARP_DELTA || mi->motion_mode == WARPED_CAUSAL);
  }
#endif  // CONFIG_WARPMV
  if (is_sub8x8_inter(xd, mi, plane, is_intrabc_block(mi, xd->tree_type),
                      build_for_obmc)) {
#if !CONFIG_EXT_RECUR_PARTITIONS
    assert(bw < 8 || bh < 8);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    build_inter_predictors_sub8x8(cm, xd, plane, mi, mi_x, mi_y, mc_buf,
                                  calc_subpel_params_func);
  } else {
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

#if CONFIG_FLEX_MVRES
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
  assert(mpp_found);
#endif
}
void set_precision_set(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                       MB_MODE_INFO *mbmi, const BLOCK_SIZE bsize,
                       uint8_t ref_mv_idx) {
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
#if CONFIG_ADAPTIVE_MVD
  if (enable_adaptive_mvd_resolution(cm, mbmi)) return 0;
#endif
  return cm->seq_params.enable_flex_mvres &&
         (mbmi->max_mv_precision >= MV_PRECISION_HALF_PEL) &&
         cm->features.use_pb_mv_precision &&
         have_newmv_in_inter_mode(mbmi->mode);
}

#endif

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
