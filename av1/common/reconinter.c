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
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/tip.h"

// This function will determine whether or not to create a warped
// prediction.
int av1_allow_warp(const MB_MODE_INFO *const mbmi,
                   const WarpTypesAllowed *const warp_types,
                   const WarpedMotionParams *const gm_params, int ref,
                   const struct scale_factors *const sf,
                   WarpedMotionParams *final_warp_params) {
  // Note: As per the spec, we must test the fixed point scales here, which are
  // at a higher precision (1 << 14) than the xs and ys in subpel_params (that
  // have 1 << 10 precision).
#if CONFIG_ACROSS_SCALE_WARP
  (void)sf;
#else
  if (av1_is_scaled(sf)) return 0;
#endif  // CONFIG_ACROSS_SCALE_WARP

  if (final_warp_params != NULL) *final_warp_params = default_warp_params;

  if (warp_types->local_warp_allowed && !mbmi->wm_params[ref].invalid) {
    if (final_warp_params != NULL)
      memcpy(final_warp_params, &mbmi->wm_params[ref],
             sizeof(*final_warp_params));
    return 1;
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
  inter_pred_params->orig_block_width = block_width;
  inter_pred_params->orig_block_height = block_height;
  inter_pred_params->original_pu_width = block_width;
  inter_pred_params->original_pu_height = block_height;

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

  inter_pred_params->use_ref_padding = 0;
  inter_pred_params->ref_area = NULL;

  inter_pred_params->use_warp_bd_box = 0;
  inter_pred_params->warp_bd_box = NULL;

  inter_pred_params->border_data.enable_bacp = 0;
  inter_pred_params->border_data.bacp_block_data = NULL;

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
  if (is_tip_ref_frame(mi->ref_frame[0])) return;

  // We do not do refineMV for warp blocks
  // We may need to return from here.
  if (mi->refinemv_flag) return;

  if (xd->cur_frame_force_integer_mv) return;

  if (av1_allow_warp(mi, warp_types, &xd->global_motion[mi->ref_frame[ref]],
                     ref, inter_pred_params->scale_factors,
                     &inter_pred_params->warp_params))
    inter_pred_params->mode = WARP_PRED;
}

void av1_make_inter_predictor(const uint16_t *src, int src_stride,
                              uint16_t *dst, int dst_stride,
                              InterPredParams *inter_pred_params,
                              const SubpelParams *subpel_params) {
  assert(IMPLIES(inter_pred_params->conv_params.is_compound,
                 inter_pred_params->conv_params.dst != NULL));
#if CONFIG_ACROSS_SCALE_REF_OPT
  assert(IMPLIES(av1_is_scaled(inter_pred_params->scale_factors),
                 av1_is_valid_scale(inter_pred_params->scale_factors)));
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
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
        inter_pred_params->subsampling_y, &inter_pred_params->conv_params
#if CONFIG_ACROSS_SCALE_WARP
        ,
        inter_pred_params->scale_factors
#endif  // CONFIG_ACROSS_SCALE_WARP
        ,
        inter_pred_params->use_warp_bd_box, inter_pred_params->warp_bd_box);
  } else if (inter_pred_params->mode == TRANSLATION_PRED) {
    highbd_inter_predictor(
        src, src_stride, dst, dst_stride, subpel_params,
        inter_pred_params->block_width, inter_pred_params->block_height,
        &inter_pred_params->conv_params,
        inter_pred_params->interp_filter_params, inter_pred_params->bit_depth,
        inter_pred_params->is_intrabc);
  }
}

/* clang-format off */
#if WEDGE_BLD_SIG
#if CONFIG_ADAPTIVE_WEDGE_BOUNDARY
// rounded cosine and sine look-up tables given by round(32*cos(i)) and round(16*cos(i)) for two wedge boundaries
static const int8_t wedge_cos_lut_all[MAX_WEDGE_BOUNDARY_TYPES][WEDGE_ANGLES] = {
  {
  //   0,  1,  2,  4,  6
      32, 32, 32, 16, 16,
  //   8, 10, 12, 14, 15
       0,-16,-16,-32,-32,
  //  16, 17, 18, 20, 22
     -32,-32,-32,-16,-16,
  //  24, 26, 28, 30, 31
       0, 16, 16, 32, 32
  },
  {
  //   0,  1,  2,  4,  6,
      16, 16, 16,  8,  8,
  //   8, 10, 12, 14, 15
       0, -8, -8,-16,-16,
  //  16, 17, 18, 20, 22
     -16,-16,-16, -8, -8,
  //  24, 26, 28, 30, 31
       0,  8,  8, 16, 16
  }
};
static const int8_t wedge_sin_lut_all[MAX_WEDGE_BOUNDARY_TYPES][WEDGE_ANGLES] = {
  {
  //   0,  1,  2,  4,  6,
       0, -8,-16,-16,-32,
  //   8, 10, 12, 14, 15
     -32,-32,-16,-16, -8,
  //  16, 17, 18, 20, 22
       0,  8, 16, 16, 32,
  //  24, 26, 28, 30, 31
      32, 32, 16, 16,  8
    },
   {
  //  0,  1,  2,  4,  6,
      0, -4, -8, -8,-16,
  //  8, 10, 12, 14, 15
    -16,-16, -8, -8, -4,
  // 16, 17, 18, 20, 22
      0,  4,  8,  8, 16,
  // 24, 26, 28, 30, 31
     16, 16,  8,  8,  4
    }
};
#else
// rounded cosine and sine look-up tables given by round(32*cos(i))
static const int8_t wedge_cos_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
     32, 32, 32, 16, 16,
  //  8, 10, 12, 14, 15,
      0,-16,-16,-32,-32,
  // 16, 17, 18, 20, 22,
    -32,-32,-32,-16,-16,
  // 24, 26, 28, 30, 31
      0, 16, 16, 32, 32
};
static const int8_t wedge_sin_lut[WEDGE_ANGLES] = {
  //  0,  1,  2,  4,  6,
      0, -8,-16,-16,-32,
  //  8, 10, 12, 14, 15,
    -32,-32,-16,-16, -8,
  // 16, 17, 18, 20, 22,
      0,  8, 16, 16, 32,
  // 24, 26, 28, 30, 31
     32, 32, 16, 16,  8
};
#endif //CONFIG_ADAPTIVE_WEDGE_BOUNDARY

// rounded sigmoid function look-up talbe given by round(1/(1+exp(-x)))
static const int8_t pos_dist_2_bld_weight[WEDGE_BLD_LUT_SIZE] = {
  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10,
  10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
};

static const int8_t neg_dist_2_bld_weight[WEDGE_BLD_LUT_SIZE] = {
  8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
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
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
DECLARE_ALIGNED(16, static uint8_t,
                wedge_allmaster_mask[2][WEDGE_ANGLES][MAX_WEDGE_BOUNDARY_TYPES]
                                    [MASK_MASTER_SIZE * MASK_MASTER_SIZE]);
#else
// [negative][direction]
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_master_mask[2][WEDGE_ANGLES][MASK_MASTER_SIZE * MASK_MASTER_SIZE]);
#endif  // WEDGE_BLD_SIG &&CONFIG_ADAPTIVE_WEDGE_BOUNDARY

#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
DECLARE_ALIGNED(16, static uint8_t,
                all_wedge_mask_buf[2 * MAX_WEDGE_TYPES * H_WEDGE_ANGLES *
                                   MAX_WEDGE_SQUARE]);
#else
// 4 * MAX_WEDGE_SQUARE is an easy to compute and fairly tight upper bound
// on the sum of all mask sizes up to an including MAX_WEDGE_SQUARE.
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_mask_buf[2 * MAX_WEDGE_TYPES * H_WEDGE_ANGLES * MAX_WEDGE_SQUARE]);
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
DECLARE_ALIGNED(
    16, static uint8_t,
    wedge_tmvp_decision_buf[2 * MAX_WEDGE_TYPES * MAX_WEDGE_BOUNDARY_TYPES *
                            H_WEDGE_ANGLES * MAX_WEDGE_SQUARE]);
#else
DECLARE_ALIGNED(16, static uint8_t,
                wedge_tmvp_decision_buf[2 * MAX_WEDGE_TYPES * H_WEDGE_ANGLES *
                                        MAX_WEDGE_SQUARE]);
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

DECLARE_ALIGNED(16, static uint8_t,
                smooth_interintra_mask_buf[INTERINTRA_MODES][BLOCK_SIZES_ALL]
                                          [MAX_WEDGE_SQUARE]);

DECLARE_ALIGNED(16, static int8_t, cwp_mask[2][MAX_CWP_NUM][MAX_SB_SQUARE]);

#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
static all_wedge_masks_type all_wedge_masks[BLOCK_SIZES_ALL][2];
#else
static wedge_masks_type wedge_masks[BLOCK_SIZES_ALL][2];
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

static wedge_decisions_type wedge_tmvp_decisions[BLOCK_SIZES_ALL][2];

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

// Look up table of params for wedge mode for different block sizes.
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL] = {
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_8X8],
    wedge_tmvp_decisions[BLOCK_8X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_8X16],
    wedge_tmvp_decisions[BLOCK_8X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_16X8],
    wedge_tmvp_decisions[BLOCK_16X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_16X16],
    wedge_tmvp_decisions[BLOCK_16X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_16X32],
    wedge_tmvp_decisions[BLOCK_16X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_32X16],
    wedge_tmvp_decisions[BLOCK_32X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_32X32],
    wedge_tmvp_decisions[BLOCK_32X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_32X64],
    wedge_tmvp_decisions[BLOCK_32X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_64X32],
    wedge_tmvp_decisions[BLOCK_64X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_64X64],
    wedge_tmvp_decisions[BLOCK_64X64] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_8X32],
    wedge_tmvp_decisions[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_32X8],
    wedge_tmvp_decisions[BLOCK_32X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_16X64],
    wedge_tmvp_decisions[BLOCK_16X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_64X16],
    wedge_tmvp_decisions[BLOCK_64X16] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_8X64],
    wedge_tmvp_decisions[BLOCK_8X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, all_wedge_masks[BLOCK_64X8],
    wedge_tmvp_decisions[BLOCK_64X8] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
};
#else
const wedge_params_type av1_wedge_params_lookup[BLOCK_SIZES_ALL] = {
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X8],
    wedge_tmvp_decisions[BLOCK_8X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X16],
    wedge_tmvp_decisions[BLOCK_8X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X8],
    wedge_tmvp_decisions[BLOCK_16X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X16],
    wedge_tmvp_decisions[BLOCK_16X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X32],
    wedge_tmvp_decisions[BLOCK_16X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X16],
    wedge_tmvp_decisions[BLOCK_32X16] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X32],
    wedge_tmvp_decisions[BLOCK_32X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X64],
    wedge_tmvp_decisions[BLOCK_32X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X32],
    wedge_tmvp_decisions[BLOCK_64X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X64],
    wedge_tmvp_decisions[BLOCK_64X64] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X32],
    wedge_tmvp_decisions[BLOCK_8X32] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_32X8],
    wedge_tmvp_decisions[BLOCK_32X8] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_16X64],
    wedge_tmvp_decisions[BLOCK_16X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X16],
    wedge_tmvp_decisions[BLOCK_64X16] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_8X64],
    wedge_tmvp_decisions[BLOCK_8X64] },
  { MAX_WEDGE_TYPES, wedge_codebook_16, NULL, wedge_masks[BLOCK_64X8],
    wedge_tmvp_decisions[BLOCK_64X8] },
  { 0, NULL, NULL, NULL, NULL },
  { 0, NULL, NULL, NULL, NULL },
};
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

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

#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
// get wedge masks for both boundaries
static const uint8_t *get_wedge_allmask_inplace(int wedge_index,
                                                int boundary_index, int neg,
                                                BLOCK_SIZE sb_type) {
  const uint8_t *master;
  const int bh = block_size_high[sb_type];
  const int bw = block_size_wide[sb_type];
  const wedge_code_type *a =
      av1_wedge_params_lookup[sb_type].codebook + wedge_index;
  int woff, hoff;
  assert(wedge_index >= 0 && wedge_index < get_wedge_types_lookup(sb_type));
  woff = (a->x_offset * bw) >> 3;
  hoff = (a->y_offset * bh) >> 3;
  master = wedge_allmaster_mask[neg][a->direction][boundary_index] +
           MASK_MASTER_STRIDE * (MASK_MASTER_SIZE / 2 - hoff) +
           MASK_MASTER_SIZE / 2 - woff;
  return master;
}
#else
static const uint8_t *get_wedge_mask_inplace(int wedge_index, int neg,
                                             BLOCK_SIZE sb_type) {
  const uint8_t *master;
  const int bh = block_size_high[sb_type];
  const int bw = block_size_wide[sb_type];
  const wedge_code_type *a =
      av1_wedge_params_lookup[sb_type].codebook + wedge_index;
  int woff, hoff;

  assert(wedge_index >= 0 && wedge_index < get_wedge_types_lookup(sb_type));
  woff = (a->x_offset * bw) >> 3;
  hoff = (a->y_offset * bh) >> 3;
  master = wedge_master_mask[neg][a->direction] +
           MASK_MASTER_STRIDE * (MASK_MASTER_SIZE / 2 - hoff) +
           MASK_MASTER_SIZE / 2 - woff;
  return master;
}
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

// For each 8x8 block, decide (if using wedge mode), whether it should store
// both MVs as the TMVP MVs, or just 1 of them (and in this case which one to
// store).
static void get_wedge_tmvp_decision(const uint8_t *mask, int mask_stride,
                                    int bw, int bh, uint8_t *decision,
                                    int decision_stride) {
  for (int h_start = 0; h_start < bh; h_start += 8) {
    for (int w_start = 0; w_start < bw; w_start += 8) {
      const uint8_t *mask_start = mask + h_start * mask_stride + w_start;
      uint8_t *decision_start = decision + h_start * decision_stride + w_start;
      int ref0_count = 0;
      int ref1_count = 0;
      for (int h = 0; h < 8; h++) {
        for (int w = 0; w < 8; w++) {
          if (mask_start[h * mask_stride + w] > 60) {
            ref0_count++;
          } else if (mask_start[h * mask_stride + w] < 4) {
            ref1_count++;
          }
        }
      }
      int this_decision = 2;
      if (ref0_count >= 60) {
        this_decision = 0;
      } else if (ref1_count >= 60) {
        this_decision = 1;
      }
      for (int h = 0; h < 8; h++) {
        for (int w = 0; w < 8; w++) {
          decision_start[h * decision_stride + w] = this_decision;
        }
      }
    }
  }
}

const uint8_t *av1_get_compound_type_mask(
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type) {
  (void)sb_type;
  switch (comp_data->type) {
    case COMPOUND_WEDGE:
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
      return av1_get_all_contiguous_soft_mask(comp_data->wedge_index,
                                              comp_data->wedge_sign, sb_type,
                                              comp_data->wedge_boundary_index);
#else
      return av1_get_contiguous_soft_mask(comp_data->wedge_index,
                                          comp_data->wedge_sign, sb_type);
#endif  // WEDGE_BLD_SIG &&CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    case COMPOUND_AVERAGE:
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

#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
// initiate master wedge masks for both boundaries for extended wedges
static AOM_INLINE void init_wedge_master_all_masks() {
  const int w = MASK_MASTER_SIZE;
  const int h = MASK_MASTER_SIZE;
  for (int k = 0; k < MAX_WEDGE_BOUNDARY_TYPES; k++) {
    for (int angle = 0; angle < WEDGE_ANGLES; angle++) {
      int idx = 0;
      for (int n = 0; n < h; n++) {
        int y = ((n << 1) - h + 1) * wedge_sin_lut_all[k][angle];
        for (int m = 0; m < w; m++, idx++) {
          int d = ((m << 1) - w + 1) * wedge_cos_lut_all[k][angle] + y;
          const int clamp_d = clamp(d, -127, 127);
          wedge_allmaster_mask[0][angle][k][idx] =
              clamp_d >= 0 ? (pos_dist_2_bld_weight[clamp_d] << (7 - 5))
                           : (neg_dist_2_bld_weight[-clamp_d] << (7 - 5));
          wedge_allmaster_mask[1][angle][k][idx] =
              64 - wedge_allmaster_mask[0][angle][k][idx];
        }
      }
    }
  }
}
#else
static AOM_INLINE void init_wedge_master_masks() {
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
            clamp_d >= 0 ? (pos_dist_2_bld_weight[clamp_d] << (7 - 5))
                         : (neg_dist_2_bld_weight[-clamp_d] << (7 - 5));
#else
        wedge_master_mask[0][angle][idx] = clamp((d + 32), 0, 64);
#endif
        wedge_master_mask[1][angle][idx] =
            64 - wedge_master_mask[0][angle][idx];
      }
    }
  }
}
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

static AOM_INLINE void init_wedge_masks() {
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  uint8_t *dst_all = all_wedge_mask_buf;
#else
  uint8_t *dst = wedge_mask_buf;
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  BLOCK_SIZE bsize;
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  memset(all_wedge_masks, 0, sizeof(all_wedge_masks));
#else
  memset(wedge_masks, 0, sizeof(wedge_masks));
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  uint8_t *dst_tmvp_decision = wedge_tmvp_decision_buf;
  memset(wedge_tmvp_decisions, 0, sizeof(wedge_tmvp_decisions));
  for (bsize = BLOCK_4X4; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const wedge_params_type *wedge_params = &av1_wedge_params_lookup[bsize];
    const int wtypes = wedge_params->wedge_types;
    if (wtypes == 0) continue;
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    const uint8_t *all_mask;
#else
    const uint8_t *mask;
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    const int bw = block_size_wide[bsize];
    const int bh = block_size_high[bsize];
    int w;
    for (w = 0; w < wtypes; ++w) {
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
      for (int k = 0; k < MAX_WEDGE_BOUNDARY_TYPES; k++) {
        all_mask = get_wedge_allmask_inplace(w, k, 0, bsize);
        aom_convolve_copy(all_mask, MASK_MASTER_STRIDE, dst_all,
                          bw /* dst_stride */, bw, bh);
        wedge_params->all_masks[0][w][k] = dst_all;
        get_wedge_tmvp_decision(dst_all, bw, bw, bh, dst_tmvp_decision, bw);
        wedge_params->tmvp_mv_decisions[0][w][k] = dst_tmvp_decision;
        dst_tmvp_decision += bw * bh;
        dst_all += bw * bh;

        all_mask = get_wedge_allmask_inplace(w, k, 1, bsize);
        aom_convolve_copy(all_mask, MASK_MASTER_STRIDE, dst_all,
                          bw /* dst_stride */, bw, bh);
        wedge_params->all_masks[1][w][k] = dst_all;
        get_wedge_tmvp_decision(dst_all, bw, bw, bh, dst_tmvp_decision, bw);
        wedge_params->tmvp_mv_decisions[1][w][k] = dst_tmvp_decision;
        dst_tmvp_decision += bw * bh;
        dst_all += bw * bh;
      }
#else
      mask = get_wedge_mask_inplace(w, 0, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw /* dst_stride */, bw,
                        bh);
      wedge_params->masks[0][w] = dst;
      get_wedge_tmvp_decision(dst, bw, bw, bh, dst_tmvp_decision, bw);
      wedge_params->tmvp_mv_decisions[0][w] = dst_tmvp_decision;
      dst_tmvp_decision += bw * bh;
      dst += bw * bh;

      mask = get_wedge_mask_inplace(w, 1, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw /* dst_stride */, bw,
                        bh);
      wedge_params->masks[1][w] = dst;
      wedge_params->tmvp_mv_decisions[1][w] = dst_tmvp_decision;
      get_wedge_tmvp_decision(dst, bw, bw, bh, dst_tmvp_decision, bw);
      dst_tmvp_decision += bw * bh;
      dst += bw * bh;
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    }
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    assert(sizeof(all_wedge_mask_buf) >=
           (size_t)(dst_all - all_wedge_mask_buf));
#else
    assert(sizeof(wedge_mask_buf) >= (size_t)(dst - wedge_mask_buf));
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
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
    0,  0,  0,  // unused
    8,  8,  4,  4,  2, 2,
    4,  4,  2,  2,  2, 2,
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

// Compute the SAD values for refineMV modes
int get_refinemv_sad(uint16_t *src1, uint16_t *src2, int stride, int width,
                     int height, int bd) {
#if CONFIG_SUBBLK_REF_EXT
  (void)bd;
#if CONFIG_SUBBLK_REF_DS
  return get_highbd_sad_ds(src1, stride, src2, stride, 8, width, height);
#else
  return get_highbd_sad(src1, stride, src2, stride, 8, width, height);
#endif
#else
#if CONFIG_SUBBLK_REF_DS
  return get_highbd_sad_ds(src1, stride, src2, stride, bd, width, height);
#else
  return get_highbd_sad(src1, stride, src2, stride, bd, width, height);
#endif  // CONFIG_SUBBLK_REF_DS
#endif  // CONFIG_SUBBLK_REF_EXT
}

int64_t stable_mult_shift(const int64_t a, const int64_t b, const int shift,
                          const int msb_a, const int msb_b, const int max_bd,
                          int *rem_shift) {
  assert(shift >= 0);

  // Remaining bit shifts (may be used in the next stage of multiplcation)
  int rem = AOMMAX(0, msb_a + msb_b - shift + 1 - max_bd);
  if (rem_shift) *rem_shift += rem;
  if (msb_a + msb_b + 2 < max_bd)
    return ROUND_POWER_OF_TWO_SIGNED_64(a * b, shift);

  // To determine s1/s2/s3 in ((a>>s1)*(b>>s2))>>s3, consider the equation
  //   (1+msb_a-s1)+(1+msb_b-s2)+1 <= max_bd+rem,
  // where better numerical stability is obtained when
  //   msb_a-s1 ~= msb_b-s2.
  // This leads to the following solution
  int msb_diff = abs(msb_a - msb_b);
  // Total required shifts (s1 + s2)
  int s = msb_a + msb_b - max_bd - rem + 4;
  int diff = AOMMIN(s, msb_diff);
  int s1 = (s - diff) >> 1;
  int s2 = s1;
  if (msb_a >= msb_b)
    s1 = s - s2;
  else
    s2 = s - s1;

  assert(s1 >= 0);
  assert(s2 >= 0);
  if (shift - s1 - s2 < 0) {
    // bit depth not large enough to hold the result
    return ((a > 0) ^ (b > 0)) ? -((1LL << (max_bd - 1)) - 1)
                               : ((1LL << (max_bd - 1)) - 1);
  }
  return ROUND_POWER_OF_TWO_SIGNED_64(
      ROUND_POWER_OF_TWO_SIGNED_64(a, s1) * ROUND_POWER_OF_TWO_SIGNED_64(b, s2),
      shift - s1 - s2);
}

// Restrict MV delta to 1 or 2 pixels. This restriction would reduce complexity
// in hardware.
#define OPFL_CLAMP_MV_DELTA 1
#define OPFL_MV_DELTA_LIMIT (1 << MV_REFINE_PREC_BITS)

void reduce_temporal_dist(int *d0, int *d1) {
  if (*d0 == 0 || *d1 == 0) return;
  int sign0 = *d0 < 0;
  int sign1 = *d1 < 0;
  int mag0 = sign0 ? -(*d0) : (*d0);
  int mag1 = sign1 ? -(*d1) : (*d1);
  // Only do simple checks for the case |d0|=|d1| and for factor 2
  if (mag0 == mag1) {
    mag0 = mag1 = 1;
  } else if (mag0 > mag1) {
    mag0 = 2;
    mag1 = 1;
  } else {
    mag0 = 1;
    mag1 = 2;
  }
  *d0 = sign0 ? -mag0 : mag0;
  *d1 = sign1 ? -mag1 : mag1;
}

void av1_opfl_build_inter_predictor(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    InterPredParams *inter_pred_params,
    CalcSubpelParamsFunc calc_subpel_params_func, int ref, uint16_t *pred_dst,
    const MV *const src_mv, int pu_width, int pu_height) {
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
  assert(cm->seq_params.order_hint_info.enable_order_hint);
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
  const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;

  // Do references one at a time
  const int is_compound = 0;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  struct buf_2d *const dst_buf = &pd->dst;

  const WarpedMotionParams *const wm = &xd->global_motion[mi->ref_frame[ref]];
  const WarpTypesAllowed warp_types = { is_global_mv_block(mi, wm->wmtype),
                                        is_warp_mode(mi->motion_mode) };
  const struct scale_factors *const sf =
      is_tip
          ? cm->tip_ref.ref_scale_factor[ref]
          : (is_intrabc ? &cm->sf_identity : xd->block_ref_scale_factors[ref]);

  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  const int row_start = (bw == 4) && ss_y ? -1 : 0;
  const int col_start = (bh == 4) && ss_x ? -1 : 0;
  const int pre_x = (mi_x + MI_SIZE * col_start) >> ss_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> ss_y;

  const struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];

  av1_init_inter_params(inter_pred_params, bw, bh, pre_y, pre_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd,
                        mi->use_intrabc[0], sf, pre_buf, BILINEAR);
  inter_pred_params->original_pu_width = pu_width;
  inter_pred_params->original_pu_height = pu_height;

  inter_pred_params->conv_params = get_conv_params_no_round(
      0, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

  av1_init_warp_params(inter_pred_params, &warp_types, ref, xd, mi);
  if (inter_pred_params->mode == WARP_PRED) return;

  assert(mi->interinter_comp.type == COMPOUND_AVERAGE);
  const MV mv_1_16th_pel = convert_mv_to_1_16th_pel(src_mv);
  av1_build_one_inter_predictor(pred_dst, bw, &mv_1_16th_pel, inter_pred_params,
                                xd, mi_x, mi_y, ref, mc_buf,
                                calc_subpel_params_func);
}

void av1_bicubic_grad_interpolation_highbd_c(const int16_t *pred_src,
                                             int16_t *x_grad, int16_t *y_grad,
                                             const int stride, const int bw,
                                             const int bh) {
#if OPFL_BICUBIC_GRAD
  for (int i = 0; i < bh; i++) {
    for (int j = 0; j < bw; j++) {
      int id_prev, id_prev2, id_next, id_next2, is_boundary;
      int32_t temp = 0;
      // Subtract interpolated pixel at (i, j+delta) by the one at (i, j-delta)
      id_prev = AOMMAX(j - 1, 0);
      id_prev2 = AOMMAX(j - 2, 0);
      id_next = AOMMIN(j + 1, bw - 1);
      id_next2 = AOMMIN(j + 2, bw - 1);
      is_boundary = (j + 1 > bw - 1 || j - 1 < 0);
      temp = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][is_boundary] *
                 (int32_t)(pred_src[i * stride + id_next] -
                           pred_src[i * stride + id_prev]) +
             coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][is_boundary] *
                 (int32_t)(pred_src[i * stride + id_next2] -
                           pred_src[i * stride + id_prev2]);
      x_grad[i * stride + j] =
          clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bicubic_bits),
                -OPFL_GRAD_CLAMP_VAL, OPFL_GRAD_CLAMP_VAL);

      // Subtract interpolated pixel at (i+delta, j) by the one at (i-delta, j)
      id_prev = AOMMAX(i - 1, 0);
      id_prev2 = AOMMAX(i - 2, 0);
      id_next = AOMMIN(i + 1, bh - 1);
      id_next2 = AOMMIN(i + 2, bh - 1);
      is_boundary = (i + 1 > bh - 1 || i - 1 < 0);
      temp = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][is_boundary] *
                 (int32_t)(pred_src[id_next * stride + j] -
                           pred_src[id_prev * stride + j]) +
             coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][is_boundary] *
                 (int32_t)(pred_src[id_next2 * stride + j] -
                           pred_src[id_prev2 * stride + j]);
      y_grad[i * stride + j] =
          clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bicubic_bits),
                -OPFL_GRAD_CLAMP_VAL, OPFL_GRAD_CLAMP_VAL);
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
      // Subtract interpolated pixel at (i, j+delta) by the one at (i, j-delta)
      id_next = AOMMIN(j + 1, bw - 1);
      id_prev = AOMMAX(j - 1, 0);
      is_boundary = (j + 1 > bw - 1 || j - 1 < 0);
      temp = coeffs_bilinear[SUBPEL_GRAD_DELTA_BITS][is_boundary] *
             (int32_t)(pred_src[i * bw + id_next] - pred_src[i * bw + id_prev]);
      x_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bilinear_bits),
                                 -OPFL_GRAD_CLAMP_VAL, OPFL_GRAD_CLAMP_VAL);
      // Subtract interpolated pixel at (i+delta, j) by the one at (i-delta, j)
      id_next = AOMMIN(i + 1, bh - 1);
      id_prev = AOMMAX(i - 1, 0);
      is_boundary = (i + 1 > bh - 1 || i - 1 < 0);
      temp = coeffs_bilinear[SUBPEL_GRAD_DELTA_BITS][is_boundary] *
             (int32_t)(pred_src[id_next * bw + j] - pred_src[id_prev * bw + j]);
      y_grad[i * bw + j] = clamp(ROUND_POWER_OF_TWO_SIGNED(temp, bilinear_bits),
                                 -OPFL_GRAD_CLAMP_VAL, OPFL_GRAD_CLAMP_VAL);
    }
  }
}
#endif  // OPFL_BILINEAR_GRAD

void av1_compute_subpel_gradients_interp(int16_t *pred_dst, int bw, int bh,
                                         int *grad_prec_bits, int16_t *x_grad,
                                         int16_t *y_grad) {
#if OPFL_BILINEAR_GRAD
  (void)is_hbd;
  av1_bilinear_grad_interpolation_c(pred_dst, x_grad, y_grad, bw, bh);
#else
  int sub_bw = AOMMIN(OPFL_GRAD_UNIT, bw);
  int sub_bh = AOMMIN(OPFL_GRAD_UNIT, bh);
  for (int i = 0; i < bh; i += sub_bh) {
    for (int j = 0; j < bw; j += sub_bw) {
      // Reuse pixels in pred_dst to compute gradients
      // SIMD code does not support bw=4 or bh=4
      if (bw < 8 || bh < 8)
        av1_bicubic_grad_interpolation_highbd_c(
            pred_dst + i * bw + j, x_grad + i * bw + j, y_grad + i * bw + j, bw,
            sub_bw, sub_bh);
      else
        av1_bicubic_grad_interpolation_highbd(
            pred_dst + i * bw + j, x_grad + i * bw + j, y_grad + i * bw + j, bw,
            sub_bw, sub_bh);
    }
  }
#endif  // OPFL_BILINEAR_GRAD

  *grad_prec_bits = 3 - SUBPEL_GRAD_DELTA_BITS - 2;
}

// Apply average pooling to reduce the sizes of pred difference and gradients
// arrays. It reduces the complexity of the parameter solving routine
void av1_avg_pooling_pdiff_gradients_c(int16_t *pdiff, const int pstride,
                                       int16_t *gx, int16_t *gy,
                                       const int gstride, const int bw,
                                       const int bh, const int n) {
  const int bh_low = AOMMIN(bh, n);
  const int bw_low = AOMMIN(bw, n);
  if (bh == bh_low && bw == bw_low) return;
  const int step_h = bh / bh_low;
  const int step_w = bw / bw_low;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  for (int i = 0; i < bh_low; i++) {
    for (int j = 0; j < bw_low; j++) {
      int32_t tmp_gx = 0, tmp_gy = 0, tmp_pdiff = 0;
      for (int k = 0; k < step_h; k++) {
        for (int l = 0; l < step_w; l++) {
          tmp_gx += gx[(i * step_h + k) * gstride + (j * step_w + l)];
          tmp_gy += gy[(i * step_h + k) * gstride + (j * step_w + l)];
          tmp_pdiff += pdiff[(i * step_h + k) * pstride + (j * step_w + l)];
        }
      }
      gx[i * gstride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_gx, avg_bits);
      gy[i * gstride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_gy, avg_bits);
      pdiff[i * pstride + j] =
          (int16_t)ROUND_POWER_OF_TWO_SIGNED(tmp_pdiff, avg_bits);
    }
  }
}

void calc_mv_process(int32_t su2, int32_t sv2, int32_t suv, int32_t suw,
                     int32_t svw, const int d0, const int d1, const int bits,
                     const int rls_alpha, int *vx0, int *vy0, int *vx1,
                     int *vy1) {
#if OPFL_REGULARIZED_LS
  su2 += rls_alpha;
  sv2 += rls_alpha;
#else
  (void)rls_alpha;
#endif

  // Solve 2x2 matrix inverse: [ su2  suv ]   [ vx0 ]     [ -suw ]
  //                           [ suv  sv2 ] * [ vy0 ]  =  [ -svw ]
  int shifts[2] = { bits, bits };
  int msb_su2 = 1 + get_msb_signed(su2);
  int msb_sv2 = 1 + get_msb_signed(sv2);
  int msb_suv = 1 + get_msb_signed(suv);
  int msb_suw = 1 + get_msb_signed(suw);
  int msb_svw = 1 + get_msb_signed(svw);
  // Make sure the max bit depth of det, sol[0], and sol[1] are within
  // MAX_LS_BITS
  int max_mult_msb = AOMMAX(
      msb_su2 + msb_sv2, AOMMAX(AOMMAX(msb_sv2 + msb_suw, msb_suv + msb_svw),
                                AOMMAX(msb_su2 + msb_svw, msb_suv + msb_suw)));
  int redbit = AOMMAX(0, max_mult_msb - MAX_LS_BITS + 3) >> 1;

  su2 = ROUND_POWER_OF_TWO_SIGNED(su2, redbit);
  sv2 = ROUND_POWER_OF_TWO_SIGNED(sv2, redbit);
  suv = ROUND_POWER_OF_TWO_SIGNED(suv, redbit);
  suw = ROUND_POWER_OF_TWO_SIGNED(suw, redbit);
  svw = ROUND_POWER_OF_TWO_SIGNED(svw, redbit);
  const int32_t det = su2 * sv2 - suv * suv;
  if (det <= 0) {
    *vx0 = 0;
    *vy0 = 0;
    *vx1 = 0;
    *vy1 = 0;
    return;
  }

  int32_t sol[2] = { sv2 * suw - suv * svw, su2 * svw - suv * suw };

  divide_and_round_array(sol, det, 2, shifts);

  const int tmp_vx0 = -sol[0];
  const int tmp_vy0 = -sol[1];
  *vx0 = tmp_vx0 * d0;
  *vy0 = tmp_vy0 * d0;
  *vx1 = tmp_vx0 * d1;
  *vy1 = tmp_vy0 * d1;
}

// Solve vx and vy given pdiff = P0 - P1 and the gradients gx/gy of
// d0 * P0 - d1 * P1.
void av1_opfl_mv_refinement(const int16_t *pdiff, int pstride,
                            const int16_t *gx, const int16_t *gy, int gstride,
                            int bw, int bh, int d0, int d1, int grad_prec_bits,
                            int mv_prec_bits, int *vx0, int *vy0, int *vx1,
                            int *vy1) {
  int32_t su2 = 0;
  int32_t suv = 0;
  int32_t sv2 = 0;
  int32_t suw = 0;
  int32_t svw = 0;
  // TODO(kslu) clean up all grad_bits if later it is still not needed
  int grad_bits = 0;
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
      const int u = gx[i * gstride + j];
      const int v = gy[i * gstride + j];
      const int w = pdiff[i * pstride + j];
      su2 += ROUND_POWER_OF_TWO_SIGNED(u * u, grad_bits);
      suv += ROUND_POWER_OF_TWO_SIGNED(u * v, grad_bits);
      sv2 += ROUND_POWER_OF_TWO_SIGNED(v * v, grad_bits);
      suw += ROUND_POWER_OF_TWO_SIGNED(u * w, grad_bits);
      svw += ROUND_POWER_OF_TWO_SIGNED(v * w, grad_bits);
    }
#if !CONFIG_F107_GRADIENT_SIMPLIFY
    // For every 8 pixels, do a range check and add a downshift if range is
    // getting close to the max allowed bit depth
    if (bw >= 8 || i % 2 == 1) {
      // Do a range check and add a downshift if range is getting close to the
      // bit depth cap
      int32_t max_autocorr = AOMMAX(su2, sv2);
      int32_t max_xcorr = AOMMAX(abs(suw), abs(svw));
      if (get_msb_signed(AOMMAX(max_autocorr, max_xcorr)) >=
          MAX_OPFL_AUTOCORR_BITS - 2) {
        su2 = ROUND_POWER_OF_TWO_SIGNED(su2, 1);
        suv = ROUND_POWER_OF_TWO_SIGNED(suv, 1);
        sv2 = ROUND_POWER_OF_TWO_SIGNED(sv2, 1);
        suw = ROUND_POWER_OF_TWO_SIGNED(suw, 1);
        svw = ROUND_POWER_OF_TWO_SIGNED(svw, 1);
        grad_bits++;
      }
    }
#endif  // !CONFIG_F107_GRADIENT_SIMPLIFY
  }
  const int bits = mv_prec_bits + grad_prec_bits;
  const int rls_alpha = (bw * bh >> 4) * OPFL_RLS_PARAM;

  calc_mv_process(su2, sv2, suv, suw, svw, d0, d1, bits, rls_alpha, vx0, vy0,
                  vx1, vy1);
}

int av1_opfl_mv_refinement_nxn_c(const int16_t *pdiff, int pstride,
                                 const int16_t *gx, const int16_t *gy,
                                 int gstride, int bw, int bh, int n, int d0,
                                 int d1, int grad_prec_bits, int mv_prec_bits,
                                 int mi_x, int mi_y, int mi_cols, int mi_rows,
                                 int build_for_decode, int *vx0, int *vy0,
                                 int *vx1, int *vy1) {
  assert(bw % n == 0 && bh % n == 0);
  int n_blocks = 0;
  for (int i = 0; i < bh; i += n) {
    for (int j = 0; j < bw; j += n) {
      if (is_subblock_outside(mi_x + j, mi_y + i, mi_cols, mi_rows,
                              build_for_decode)) {
        n_blocks++;
        continue;
      }
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

static AOM_FORCE_INLINE void compute_pred_using_interp_grad_highbd(
    const uint16_t *src1, const uint16_t *src2, int src_stride, int16_t *dst1,
    int16_t *dst2, int bw, int bh, int d0, int d1, int bd, int centered) {
  for (int i = 0; i < bh; ++i) {
    for (int j = 0; j < bw; ++j) {
      // To avoid overflow, we clamp d0*P0-d1*P1 and P0-P1.
      int32_t tmp_dst = d0 * (int32_t)src1[i * src_stride + j] -
                        d1 * (int32_t)src2[i * src_stride + j];
      if (centered) tmp_dst = ROUND_POWER_OF_TWO_SIGNED(tmp_dst, 1);
      tmp_dst = ROUND_POWER_OF_TWO_SIGNED(tmp_dst, bd - 8);
      dst1[i * bw + j] = clamp(tmp_dst, -OPFL_PRED_MAX, OPFL_PRED_MAX);
      if (dst2) {
        tmp_dst = (int32_t)src1[i * src_stride + j] -
                  (int32_t)src2[i * src_stride + j];
        tmp_dst = ROUND_POWER_OF_TWO_SIGNED(tmp_dst, bd - 8);
        dst2[i * bw + j] = clamp(tmp_dst, -OPFL_PRED_MAX, OPFL_PRED_MAX);
      }
    }
  }
}

void av1_copy_pred_array_highbd_c(const uint16_t *src1, const uint16_t *src2,
                                  int src_stride, int16_t *dst1, int16_t *dst2,
                                  int bw, int bh, int d0, int d1, int bd,
                                  int centered) {
  compute_pred_using_interp_grad_highbd(src1, src2, src_stride, dst1, dst2, bw,
                                        bh, d0, d1, bd, centered);
}

void av1_get_optflow_based_mv(const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                              const MB_MODE_INFO *mbmi, int_mv *mv_refined,
                              int bw, int bh, int mi_x, int mi_y,
                              int build_for_decode, uint16_t **mc_buf,
                              CalcSubpelParamsFunc calc_subpel_params_func,
                              int16_t *gx0, int16_t *gy0, int16_t *gx1,
                              int16_t *gy1, int *vx0, int *vy0, int *vx1,
                              int *vy1, uint16_t *dst0, uint16_t *dst1,
                              int dst_stride, int do_pred, int use_4x4,
                              MV *best_mv_ref, int pu_width, int pu_height) {
  const int target_prec = MV_REFINE_PREC_BITS;
  const int n = opfl_get_subblock_size(bw, bh, plane, use_4x4);
  int n_blocks = (bw / n) * (bh / n);
  // Convert output MV to 1/16th pel
  assert(MV_REFINE_PREC_BITS >= 3);
  const int mv_mult = 1 << (MV_REFINE_PREC_BITS - 3);
  for (int mvi = 0; mvi < n_blocks; mvi++) {
    mv_refined[mvi * 2].as_mv.row =
        clamp(mv_refined[mvi * 2].as_mv.row * mv_mult, MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
    mv_refined[mvi * 2].as_mv.col =
        clamp(mv_refined[mvi * 2].as_mv.col * mv_mult, MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
    mv_refined[mvi * 2 + 1].as_mv.row =
        clamp(mv_refined[mvi * 2 + 1].as_mv.row * mv_mult, MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
    mv_refined[mvi * 2 + 1].as_mv.col =
        clamp(mv_refined[mvi * 2 + 1].as_mv.col * mv_mult, MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
  }

  // Obtain d0 and d1
  int d0, d1;
  if (mbmi->ref_frame[0] == TIP_FRAME) {
    d0 = cm->tip_ref.ref_offset[0];
    d1 = cm->tip_ref.ref_offset[1];
  } else {
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
  }

  if (d0 == 0 || d1 == 0) {
    // Though OPFL is disabled when the
    // distance from either of the reference
    // frames is zero, the MV offset buffers
    // are still used to update the mv_delta
    // buffer. Hence, memset the MV offset
    // buffers vx and vy to zero.
    av1_zero_array(vx0, n_blocks);
    av1_zero_array(vx1, n_blocks);
    av1_zero_array(vy0, n_blocks);
    av1_zero_array(vy1, n_blocks);
    return;
  }

  reduce_temporal_dist(&d0, &d1);

  if (do_pred) {
    // Obrain P0 and P1
    InterPredParams params0, params1;
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params0, calc_subpel_params_func, 0,
                                   dst0, &best_mv_ref[0], pu_width, pu_height);
    av1_opfl_build_inter_predictor(cm, xd, plane, mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params1, calc_subpel_params_func, 1,
                                   dst1, &best_mv_ref[1], pu_width, pu_height);
  }

  int grad_prec_bits;

  // Compute gradients of P0 and P1 with
  // interpolation
  (void)gx1;
  (void)gy1;

  // Compute tmp1 = P0 - P1 and gradients of tmp0 = d0 * P0 - d1 * P1
  DECLARE_ALIGNED(32, int16_t, tmp0[MAX_SB_SIZE * MAX_SB_SIZE]);
  DECLARE_ALIGNED(32, int16_t, tmp1[MAX_SB_SIZE * MAX_SB_SIZE]);
  av1_copy_pred_array_highbd(dst0, dst1, dst_stride, tmp0, tmp1, bw, bh, d0, d1,
                             xd->bd, 0);
  // Buffers gx0 and gy0 are used to store the
  // gradients of tmp0
  av1_compute_subpel_gradients_interp(tmp0, bw, bh, &grad_prec_bits, gx0, gy0);

  n_blocks = av1_opfl_mv_refinement_nxn(
      tmp1, bw, gx0, gy0, bw, bw, bh, n, d0, d1, grad_prec_bits, target_prec,
      mi_x, mi_y, cm->mi_params.mi_cols, cm->mi_params.mi_rows,
      build_for_decode, vx0, vy0, vx1, vy1);

  for (int i = 0; i < n_blocks; i++) {
    vy0[i] = clamp(vy0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vx0[i] = clamp(vx0[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vy1[i] = clamp(vy1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    vx1[i] = clamp(vx1[i], -OPFL_MV_DELTA_LIMIT, OPFL_MV_DELTA_LIMIT);
    mv_refined[i * 2].as_mv.row = clamp(mv_refined[i * 2].as_mv.row + vy0[i],
                                        MV_1_16TH_PEL_MIN, MV_1_16TH_PEL_MAX);
    mv_refined[i * 2].as_mv.col = clamp(mv_refined[i * 2].as_mv.col + vx0[i],
                                        MV_1_16TH_PEL_MIN, MV_1_16TH_PEL_MAX);
    mv_refined[i * 2 + 1].as_mv.row =
        clamp(mv_refined[i * 2 + 1].as_mv.row + vy1[i], MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
    mv_refined[i * 2 + 1].as_mv.col =
        clamp(mv_refined[i * 2 + 1].as_mv.col + vx1[i], MV_1_16TH_PEL_MIN,
              MV_1_16TH_PEL_MAX);
  }
}

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

// Equation of line: f(x, y) = a[0]*(x -
// a[2]*w/8) + a[1]*(y - a[3]*h/8) = 0
void av1_init_wedge_masks() {
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  init_wedge_master_all_masks();
#else
  init_wedge_master_masks();
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  init_wedge_masks();
  init_smooth_interintra_masks();
}

static AOM_INLINE void build_masked_compound_no_round(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w, InterPredParams *inter_pred_params) {
  const int ssy = (inter_pred_params->conv_params.plane &&
                   comp_data->type == COMPOUND_AVERAGE)
                      ? 0
                      : inter_pred_params->subsampling_y;
  const int ssx = (inter_pred_params->conv_params.plane &&
                   comp_data->type == COMPOUND_AVERAGE)
                      ? 0
                      : inter_pred_params->subsampling_x;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  const int mask_stride = block_size_wide[sb_type];
  aom_highbd_blend_a64_d16_mask(dst, dst_stride, src0, src0_stride, src1,
                                src1_stride, mask, mask_stride, w, h, ssx, ssy,
                                &inter_pred_params->conv_params,
                                inter_pred_params->bit_depth);
}
void make_masked_inter_predictor(const uint16_t *pre, int pre_stride,
                                 uint16_t *dst, int dst_stride,
                                 InterPredParams *inter_pred_params,
                                 const SubpelParams *subpel_params,
                                 int use_bacp, int sub_block_id) {
  const INTERINTER_COMPOUND_DATA *comp_data = &inter_pred_params->mask_comp;
  BLOCK_SIZE sb_type = inter_pred_params->sb_type;

  // We're going to call
  // av1_make_inter_predictor to generate a
  // prediction into a temporary buffer, then
  // will blend that temporary buffer with
  // that from the other reference.
  DECLARE_ALIGNED(32, uint16_t, tmp_buf[MAX_SB_SQUARE]);

  const int tmp_buf_stride = MAX_SB_SIZE;
  CONV_BUF_TYPE *org_dst = inter_pred_params->conv_params.dst;
  int org_dst_stride = inter_pred_params->conv_params.dst_stride;
  CONV_BUF_TYPE *tmp_buf16 = (CONV_BUF_TYPE *)tmp_buf;
  inter_pred_params->conv_params.dst = tmp_buf16;
  inter_pred_params->conv_params.dst_stride = tmp_buf_stride;
  assert(inter_pred_params->conv_params.do_average == 0);

  // This will generate a prediction in
  // tmp_buf for the second reference
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

  // Mask is generated from luma and reuse for
  // chroma
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

    // Take out this area in p0
    int frame_width = inter_pred_params->ref_frame_buf.width;
    int frame_height = inter_pred_params->ref_frame_buf.height;
    int block_width = inter_pred_params->block_width;
    int block_height = inter_pred_params->block_height;

    int p0_x_start = b_data_0->x0 < 0 ? 0 : frame_width - b_data_0->x0;
    p0_x_start = AOMMIN(p0_x_start, block_width);
    p0_x_start = AOMMAX(p0_x_start, 0);

    int p0_x_end = b_data_0->x1 > frame_width ? block_width : -b_data_0->x0;
    p0_x_end = AOMMAX(p0_x_end, 0);
    p0_x_end = AOMMIN(p0_x_end, block_width);

    int p0_y_start = b_data_0->y0 < 0 ? 0 : frame_height - b_data_0->y0;
    p0_y_start = AOMMIN(p0_y_start, block_height);
    p0_y_start = AOMMAX(p0_y_start, 0);
    int p0_y_end = b_data_0->y1 > frame_height ? block_height : -b_data_0->y0;
    p0_y_end = AOMMAX(p0_y_end, 0);
    p0_y_end = AOMMIN(p0_y_end, block_height);

    int p1_x_start = b_data_1->x0 < 0 ? 0 : frame_width - b_data_1->x0;
    p1_x_start = AOMMIN(p1_x_start, block_width);
    p1_x_start = AOMMAX(p1_x_start, 0);
    int p1_x_end = b_data_1->x1 > frame_width ? block_width : -b_data_1->x0;
    p1_x_end = AOMMAX(p1_x_end, 0);
    p1_x_end = AOMMIN(p1_x_end, block_width);

    int p1_y_start = b_data_1->y0 < 0 ? 0 : frame_height - b_data_1->y0;
    p1_y_start = AOMMIN(p1_y_start, block_height);
    p1_y_start = AOMMAX(p1_y_start, 0);
    int p1_y_end = b_data_1->y1 > frame_height ? block_height : -b_data_1->y0;
    p1_y_end = AOMMAX(p1_y_end, 0);
    p1_y_end = AOMMIN(p1_y_end, block_height);

    // Initialize the mask block
    for (int idy = 0; idy < block_height; ++idy)
      memset(mask + mask_stride * idy, AOM_BLEND_A64_MAX_ALPHA >> 1,
             block_width);

    int line_start = (p1_x_start == 0) ? p1_x_end : 0;
    int line_end = (p1_x_start == 0) ? block_width : p1_x_start;
    int mem_width = line_end - line_start;
    int row_start =
        (p1_y_start == 0) ? AOMMAX(p0_y_start, p1_y_end) : p0_y_start;
    int row_end = (p1_y_start == 0) ? p0_y_end : AOMMIN(p0_y_end, p1_y_start);

    if (mem_width > 0) {
      for (int idy = row_start; idy < row_end; ++idy) {
        memset(mask + mask_stride * idy + line_start, DEFAULT_IMP_MSK_WT,
               mem_width);
      }
    }

    line_start = (p0_x_start == 0) ? p0_x_end : 0;
    line_end = (p0_x_start == 0) ? block_width : p0_x_start;
    mem_width = line_end - line_start;
    row_start = (p0_y_start == 0) ? AOMMAX(p1_y_start, p0_y_end) : p1_y_start;
    row_end = (p0_y_start == 0) ? p1_y_end : AOMMIN(p1_y_end, p0_y_start);

    if (mem_width > 0) {
      for (int idy = row_start; idy < row_end; ++idy) {
        memset(mask + mask_stride * idy + line_start,
               AOM_BLEND_A64_MAX_ALPHA - DEFAULT_IMP_MSK_WT, mem_width);
      }
    }

    int start_idx =
        (p1_x_start == 0) ? AOMMAX(p0_x_start, p1_x_end) : p0_x_start;
    int end_idx = (p1_x_start == 0) ? p0_x_end : AOMMIN(p0_x_end, p1_x_start);
    int len = end_idx - start_idx;
    if (len > 0) {
      for (int idy = 0; idy < block_height; ++idy) {
        int value = DEFAULT_IMP_MSK_WT;
        if (idy >= p1_y_start && idy < p1_y_end)
          value = AOM_BLEND_A64_MAX_ALPHA >> 1;
        memset(mask + mask_stride * idy + start_idx, value, len);
      }
    }

    start_idx = (p0_x_start == 0) ? AOMMAX(p1_x_start, p0_x_end) : p1_x_start;
    end_idx = (p0_x_start == 0) ? p1_x_end : AOMMIN(p1_x_end, p0_x_start);
    len = end_idx - start_idx;
    if (len > 0) {
      for (int idy = 0; idy < block_height; ++idy) {
        int value = AOM_BLEND_A64_MAX_ALPHA - DEFAULT_IMP_MSK_WT;
        if (idy >= p0_y_start && idy < p0_y_end)
          value = AOM_BLEND_A64_MAX_ALPHA >> 1;
        memset(mask + mask_stride * idy + start_idx, value, len);
      }
    }
  }

  build_masked_compound_no_round(
      dst, dst_stride, org_dst, org_dst_stride, tmp_buf16, tmp_buf_stride,
      comp_data, sb_type, inter_pred_params->block_height,
      inter_pred_params->block_width, inter_pred_params);

  // restore to previous state
  inter_pred_params->conv_params.dst = org_dst;
  inter_pred_params->conv_params.dst_stride = org_dst_stride;
}

// Makes the interpredictor for the region by
// dividing it up into nxn blocks and running
// the interpredictor code on each one.
void make_inter_pred_of_nxn(uint16_t *dst, int dst_stride,
                            int_mv *const mv_refined,
                            InterPredParams *inter_pred_params, MACROBLOCKD *xd,
                            int mi_x, int mi_y, int build_for_decode,
                            const AV1_COMMON *cm, int pu_width, int plane,
                            int ref, uint16_t **mc_buf,
                            CalcSubpelParamsFunc calc_subpel_params_func,
                            int use_4x4, SubpelParams *subpel_params) {
  int opfl_sub_bw = OF_BSIZE;
  int opfl_sub_bh = OF_BSIZE;
  opfl_subblock_size_plane(xd, plane, use_4x4, &opfl_sub_bw, &opfl_sub_bh);

  int n_blocks = 0;
  int bw = inter_pred_params->orig_block_width;
  int bh = inter_pred_params->orig_block_height;
  int sub_bw = opfl_sub_bw;
  int sub_bh = opfl_sub_bh;
  assert(bw % sub_bw == 0);
  assert(bh % sub_bh == 0);
  CONV_BUF_TYPE *orig_conv_dst = inter_pred_params->conv_params.dst;
  inter_pred_params->block_width = sub_bw;
  inter_pred_params->block_height = sub_bh;

  MV *subblock_mv;
  uint16_t *pre;
  int src_stride = 0;
  MV avg_mv;

  // Process whole nxn blocks.
  for (int j = 0; j < bh; j += sub_bh) {
    for (int i = 0; i < bw; i += sub_bw) {
      int delta_idx = (j / sub_bh) * (pu_width / sub_bw) + (i / sub_bw);
      const int x = mi_x + i * (1 << inter_pred_params->subsampling_x);
      const int y = mi_y + j * (1 << inter_pred_params->subsampling_y);
      if (is_subblock_outside(x, y, cm->mi_params.mi_cols,
                              cm->mi_params.mi_rows, build_for_decode)) {
        n_blocks++;
        dst += sub_bw;
        inter_pred_params->conv_params.dst += sub_bw;
        inter_pred_params->pix_col += sub_bw;
        continue;
      }
      if (bw == 4 && bh == 4 && sub_bw == 4 && sub_bh == 4) {
        avg_mv.row =
            ROUND_POWER_OF_TWO_SIGNED(mv_refined[0 * 2 + ref].as_mv.row +
                                          mv_refined[1 * 2 + ref].as_mv.row +
                                          mv_refined[2 * 2 + ref].as_mv.row +
                                          mv_refined[3 * 2 + ref].as_mv.row,
                                      2);
        avg_mv.col =
            ROUND_POWER_OF_TWO_SIGNED(mv_refined[0 * 2 + ref].as_mv.col +
                                          mv_refined[1 * 2 + ref].as_mv.col +
                                          mv_refined[2 * 2 + ref].as_mv.col +
                                          mv_refined[3 * 2 + ref].as_mv.col,
                                      2);
        subblock_mv = &avg_mv;
      } else if (bw == 4 && bh == 8 && sub_bw == 4 && sub_bh == 4) {
        const int sub_idx = delta_idx * 2;
        avg_mv.row = ROUND_POWER_OF_TWO_SIGNED(
            mv_refined[sub_idx * 2 + ref].as_mv.row +
                mv_refined[(sub_idx + 1) * 2 + ref].as_mv.row,
            1);
        avg_mv.col = ROUND_POWER_OF_TWO_SIGNED(
            mv_refined[sub_idx * 2 + ref].as_mv.col +
                mv_refined[(sub_idx + 1) * 2 + ref].as_mv.col,
            1);
        subblock_mv = &avg_mv;
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

      calc_subpel_params_func(subblock_mv, inter_pred_params, xd, mi_x + i,
                              mi_y + j, ref, 1, mc_buf, &pre, subpel_params,
                              &src_stride);

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
        av1_make_inter_predictor(pre, src_stride, dst, dst_stride,
                                 inter_pred_params, subpel_params);
      }

      // Restored to original
      // inter_pred_params
      if (use_bacp && inter_pred_params->mask_comp.type == COMPOUND_AVERAGE) {
        inter_pred_params->conv_params.do_average = stored_do_average;
        inter_pred_params->comp_mode = stored_comp_mode;
        inter_pred_params->mask_comp.seg_mask = stored_seg_mask;
      }
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

// Use a second pass of motion compensation to
// rebuild inter predictor
void av1_opfl_rebuild_inter_predictor(
    uint16_t *dst, int dst_stride, int plane, int_mv *const mv_refined,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
    int build_for_decode, const AV1_COMMON *cm, int pu_width, int ref,
    uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func,
    int use_4x4) {
  SubpelParams subpel_params;

  make_inter_pred_of_nxn(dst, dst_stride, mv_refined, inter_pred_params, xd,
                         mi_x, mi_y, build_for_decode, cm, pu_width, plane, ref,
                         mc_buf, calc_subpel_params_func, use_4x4,
                         &subpel_params);
}

void av1_build_one_inter_predictor(
    uint16_t *dst, int dst_stride, const MV *const src_mv,
    InterPredParams *inter_pred_params, MACROBLOCKD *xd, int mi_x, int mi_y,
    int ref, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func) {
  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;
  calc_subpel_params_func(src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
                          1 /* is_mv_1_16th_pel  */, mc_buf, &src,
                          &subpel_params, &src_stride);

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

  if (inter_pred_params->comp_mode == UNIFORM_SINGLE ||
      inter_pred_params->comp_mode == UNIFORM_COMP) {
    av1_make_inter_predictor(src, src_stride, dst, dst_stride,
                             inter_pred_params, &subpel_params);
    assert(IMPLIES(use_bacp, ref == 0));
    assert(use_bacp == 0);
  } else {
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params, use_bacp, 0);
    assert(IMPLIES(inter_pred_params->border_data.enable_bacp, ref == 1));
  }
}

// The bellow arrays are used to map the
// number of BAWP reference samples to a 2^N
// number for each side (left or above).
static const uint8_t blk_size_log2_bawp[BAWP_MAX_REF_NUMB + 1] = {
  0, 0, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4
};
static const uint8_t log_to_blk_size[5] = { 0, 2, 4, 8, 16 };

// The below function is used to allocate the
// number of reference samples for the left
// and above based on the availablity of the
// left and above and the total number of
// available samples. The final number should
// be 0, 4, 8, 16 or 32 in total.
static void derive_number_ref_samples_bawp(bool above_valid, bool left_valid,
                                           int width, int height, int *numb_up,
                                           int *numb_left) {
  // If the number of adjusted number of
  // samples is zero, set the availability to
  // be false
  const bool above_available = width ? above_valid : false;
  const bool left_available = height ? left_valid : false;

  // If both left and above references are
  // availalbe, the numbers of reference
  // samples in each side are calculated based
  // on the clamped width and clamped height.
  // Else, only the reference samples in the
  // available side is used.

  *numb_up = -1;
  *numb_left = -1;
  if (above_available && left_available) {
    if (width == 16 && height == 16) {
      *numb_up = 16;
      *numb_left = 16;  // Using 32 samples in
                        // total for 16x16
    } else if (width > 4 && height > 4) {
      *numb_up = 8;
      *numb_left = 8;  // (16) 8x8, 8x16, 16x8
    } else if (width < 16 && height < 16) {
      *numb_up = 4;
      *numb_left = 4;  // (8) 4x8, 8x4
    } else if (width == 16) {
      *numb_up = 16;
      *numb_left = 0;  // (16) 16x4
    } else {
      *numb_up = 0;
      *numb_left = 16;  // (16) 4x16
    }
  } else if (above_available) {
    *numb_up = width;
    *numb_left = 0;
  } else if (left_available) {
    *numb_up = 0;
    *numb_left = height;
  } else {
    *numb_up = 0;
    *numb_left = 0;
  }
}

// Derive the scaling factor and offset of
// block adaptive weighted prediction mode.
// One row from the top boundary and one
// column from the left boundary are used in
// the less square error process.
static void derive_bawp_parameters(MACROBLOCKD *xd, uint16_t *recon_top,
                                   uint16_t *recon_left, int rec_stride,
                                   uint16_t *ref_top, uint16_t *ref_left,
                                   int ref_stride, int ref, int plane, int bw,
                                   int bh) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  if (!mbmi->morph_pred) assert(mbmi->bawp_flag[0] >= 1);
  // only integer position of reference, may
  // need to consider fractional position of
  // ref samples
  int count = 0;
  int sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;

  const int max_numb_each_size =
      plane ? (BAWP_MAX_REF_NUMB >> 1) : BAWP_MAX_REF_NUMB;
  // Clamp the bw and bh to use up to 16
  // samples in the left and above
  bw = AOMMIN(bw, max_numb_each_size);
  bh = AOMMIN(bh, max_numb_each_size);

  // Make the number of samples in each side
  // to 4, 8, or 16 by padding. If the number
  // of sample in a side is smaller than 3,
  // dont use the reference in this side (set
  // the corresponding elements in
  // blk_size_log2_bawp to zero).
  const int log2_width = blk_size_log2_bawp[bw];
  const int width = log_to_blk_size[log2_width];

  const int log2_height = blk_size_log2_bawp[bh];
  const int height = log_to_blk_size[log2_height];

  int numb_up = 0, numb_left = 0;
  derive_number_ref_samples_bawp(xd->up_available, xd->left_available, width,
                                 height, &numb_up, &numb_left);

  uint16_t ref_pad[BAWP_MAX_REF_NUMB] = { 0 };
  uint16_t recon_pad[BAWP_MAX_REF_NUMB] = { 0 };

  if (numb_up) {
    const int step = (int)width / numb_up;
    const int start = step == 1 ? 0 : step >> 1;
    const int delta_w = width - bw;

    for (int i = 0; i < bw; ++i) {
      ref_pad[i] = ref_top[i];
      recon_pad[i] = recon_top[i];
    }
    // Padding
    if (delta_w > 0) {
      for (int i = 0; i < delta_w; i++) {
        ref_pad[i + bw] = ref_pad[i];
        recon_pad[i + bw] = recon_pad[i];
      }
    }

    for (int i = start; i < width; i = i + step) {
      sum_x += ref_pad[i];
      sum_y += recon_pad[i];
      sum_xy += ref_pad[i] * recon_pad[i];
      sum_xx += ref_pad[i] * ref_pad[i];
    }
    count += numb_up;
  }

  if (numb_left) {
    const int step_left = (int)height / numb_left;
    const int start_left = step_left == 1 ? 0 : step_left >> 1;
    const int delta = height - bh;

    for (int i = 0; i < bh; ++i) {
      ref_pad[i] = ref_left[0];
      recon_pad[i] = recon_left[0];

      recon_left += rec_stride;
      ref_left += ref_stride;
    }
    // Padding
    if (delta > 0) {
      for (int i = 0; i < delta; i++) {
        ref_pad[i + bh] = ref_pad[i];
        recon_pad[i + bh] = recon_pad[i];
      }
    }
    for (int i = start_left; i < height; i = i + step_left) {
      sum_x += ref_pad[i];
      sum_y += recon_pad[i];
      sum_xy += ref_pad[i] * recon_pad[i];
      sum_xx += ref_pad[i] * ref_pad[i];
    }
    count += numb_left;
  }

  const int16_t shift = 8;  // maybe a smaller value can be used

  if (mbmi->bawp_flag[0] > 1 && plane == 0) {
    if (count > 0) {
      const int beta = derive_linear_parameters_beta(
          sum_x, sum_y, count, shift, mbmi->bawp_alpha[plane][ref]);
      mbmi->bawp_beta[plane][ref] = beta;
    } else {
      mbmi->bawp_beta[plane][ref] = -(1 << shift);
    }
  } else {
    if (count > 0) {
      if (plane == 0) {
        const int16_t alpha = derive_linear_parameters_alpha(
            sum_x, sum_y, sum_xx, sum_xy, count, shift);
        mbmi->bawp_alpha[plane][ref] = (alpha == 0) ? (1 << shift) : alpha;
      } else {
        mbmi->bawp_alpha[plane][ref] = mbmi->bawp_alpha[0][ref];
      }

      const int beta = derive_linear_parameters_beta(
          sum_x, sum_y, count, shift, mbmi->bawp_alpha[plane][ref]);
      mbmi->bawp_beta[plane][ref] = beta;
    } else {
      mbmi->bawp_alpha[plane][ref] = 1 << shift;
      mbmi->bawp_beta[plane][ref] = -(1 << shift);
    }
  }
}

// Generate weighted prediction of the block.
void av1_make_bawp_block_c(uint16_t *dst, int dst_stride, int16_t alpha,
                           int32_t beta, int shift, int bw, int bh, int bd) {
  for (int j = 0; j < bh; ++j) {
    for (int i = 0; i < bw; ++i) {
      dst[j * dst_stride + i] = clip_pixel_highbd(
          (dst[j * dst_stride + i] * alpha + beta) >> shift, bd);
    }
  }
}

// generate inter prediction of a block coded
// in bwap mode enabled
void av1_build_one_bawp_inter_predictor(
    uint16_t *dst, int dst_stride, const MV *const src_mv,
    InterPredParams *inter_pred_params, const AV1_COMMON *cm, MACROBLOCKD *xd,
    const BUFFER_SET *dst_orig, int bw, int bh, int mi_x, int mi_y, int ref,
    int plane, uint16_t **mc_buf,
    CalcSubpelParamsFunc calc_subpel_params_func) {
  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;
  calc_subpel_params_func(src_mv, inter_pred_params, xd, mi_x, mi_y, ref, 0,
                          mc_buf, &src, &subpel_params, &src_stride);

  assert(inter_pred_params->comp_mode == UNIFORM_SINGLE);
  if (inter_pred_params->comp_mode == UNIFORM_SINGLE ||
      inter_pred_params->comp_mode == UNIFORM_COMP) {
    av1_make_inter_predictor(src, src_stride, dst, dst_stride,
                             inter_pred_params, &subpel_params);
  } else {
    make_masked_inter_predictor(src, src_stride, dst, dst_stride,
                                inter_pred_params, &subpel_params, 0, 0);
  }

  const int shift = 8;
  MB_MODE_INFO *mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int x_off = GET_MV_RAWPEL(mbmi->mv[ref].as_mv.col);
  const int y_off = GET_MV_RAWPEL(mbmi->mv[ref].as_mv.row);

  const int x_off_p = x_off >> inter_pred_params->subsampling_x;
  const int y_off_p = y_off >> inter_pred_params->subsampling_y;

  const int mi_x_p = mi_x >> inter_pred_params->subsampling_x;
  const int mi_y_p = mi_y >> inter_pred_params->subsampling_y;

#if CONFIG_F054_PIC_BOUNDARY
  const int width_p = pd->dst.width;
  const int height_p = pd->dst.height;
#else
  const int width_p = cm->width >> inter_pred_params->subsampling_x;
  const int height_p = cm->height >> inter_pred_params->subsampling_y;
#endif  // CONFIG_F054_PIC_BOUNDARY

  int ref_w = bw;
  if ((mi_x_p + bw) >= width_p) ref_w = width_p - mi_x_p;

  int ref_h = bh;
  if ((mi_y_p + bh) >= height_p) ref_h = height_p - mi_y_p;
  if ((mi_x_p + x_off_p - BAWP_REF_LINES) < 0 ||
      (mi_y_p + y_off_p - BAWP_REF_LINES) < 0 || ref_w <= 0 || ref_h <= 0 ||
      (mi_x_p + ref_w + x_off_p) >= width_p ||
      (mi_y_p + ref_h + y_off_p) >= height_p) {
    mbmi->bawp_alpha[plane][ref] = 1 << shift;
    mbmi->bawp_beta[plane][ref] = -(1 << shift);
    return;
  } else {
    uint16_t *recon_buf = xd->plane[plane].dst.buf;
    int recon_stride = xd->plane[plane].dst.stride;
    if (dst_orig != NULL) {
      recon_buf = dst_orig->plane[plane];
      recon_stride = dst_orig->stride[plane];
    }
    uint16_t *recon_top = recon_buf - BAWP_REF_LINES * recon_stride;
    uint16_t *recon_left = recon_buf - BAWP_REF_LINES;

    // the picture boundary limitation to be
    // checked.
    const int ref_stride = pd->pre[ref].stride;
    uint16_t *ref_buf = pd->pre[ref].buf + y_off_p * ref_stride + x_off_p;
    uint16_t *ref_top = ref_buf - BAWP_REF_LINES * ref_stride;
    uint16_t *ref_left = ref_buf - BAWP_REF_LINES;
    if (mbmi->bawp_flag[0] > 1 && plane == 0) {
      const int first_ref_dist =
          cm->ref_frame_relative_dist[mbmi->ref_frame[0]];
      const int bawp_scale_table[3][EXPLICIT_BAWP_SCALE_CNT] = { { -1, 1 },
                                                                 { -2, 2 },
                                                                 { -3, 3 } };
      const int list_index =
          (mbmi->mode == NEARMV)
              ? 0
              : ((mbmi->mode == NEWMV && mbmi->use_amvd) ? 1 : 2);

      int delta_scales = bawp_scale_table[list_index][mbmi->bawp_flag[0] - 2];
      const int delta_sign = delta_scales > 0 ? 1 : -1;
      const int delta_magtitude = delta_sign * delta_scales;
      if (first_ref_dist > 4) delta_scales = delta_sign * (delta_magtitude + 1);
      mbmi->bawp_alpha[plane][ref] = 256 + (delta_scales * 16);
    }

    derive_bawp_parameters(xd, recon_top, recon_left, recon_stride, ref_top,
                           ref_left, ref_stride, ref, plane, ref_w, ref_h);
  }

  int16_t alpha = mbmi->bawp_alpha[plane][ref];
  int32_t beta = mbmi->bawp_beta[plane][ref];
  av1_make_bawp_block(dst, dst_stride, alpha, beta, shift, bw, bh, xd->bd);
}

// True if the following hold:
//  1. Not intrabc
//  2. At least one dimension is size 4 with
//  subsampling
//  3. If sub-sampled, none of the previous
//  blocks around the sub-sample
//     are intrabc or inter-blocks
static bool is_sub8x8_inter(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                            const MB_MODE_INFO *mi, int plane, int is_intrabc) {
#if CONFIG_CHROMA_MERGE_LATENCY_FIX
  (void)xd;
#endif  // CONFIG_CHROMA_MERGE_LATENCY_FIX
  if (is_intrabc
#if CONFIG_CHROMA_MERGE_LATENCY_FIX
      && frame_is_intra_only(cm)
#endif  // CONFIG_CHROMA_MERGE_LATENCY_FIX
  ) {
    return false;
  }
  if (!(plane &&
        (mi->sb_type[PLANE_TYPE_UV] != mi->chroma_ref_info.bsize_base)))
    return false;

#if CONFIG_CHROMA_MERGE_LATENCY_FIX
  assert(!frame_is_intra_only(cm));
#else
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
#endif  // CONFIG_CHROMA_MERGE_LATENCY_FIX
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

  // For sub8x8 chroma blocks, we may be
  // covering more than one luma block's worth
  // of pixels. Thus (mi_x, mi_y) may not be
  // the correct coordinates for the top-left
  // corner of the prediction source - the
  // correct top-left corner is at (pre_x,
  // pre_y).
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

  // Row progress keeps track of which mi
  // block in the row has been set.
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

      // The flag here is a block of mi_width
      // many 1s offset by the mi_col. For
      // example, if the current mi_col is 2,
      // and the mi_width is 2, then the flag
      // will be 00110000. We or this with
      // row_progress to update the blocks
      // that have been coded. Note that
      // because we are always coding in a
      // causal order, we could technically
      // simplify the bitwise operation, and
      // use the flag 11110000 in the above
      // example instead. However, we are not
      // taking this approach here to keep the
      // logic simpler.
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
      // TODO(yuec): enabling compound
      // prediction in none sub8x8 mbs in the
      // group
      bool is_compound = 0;
      struct buf_2d *const dst_buf = &pd->dst;
      uint16_t *dst = dst_buf->buf + dst_buf->stride * pixel_row + pixel_col;
      int ref = 0;
      const RefCntBuffer *ref_buf =
          get_ref_frame_buf(cm, this_mbmi->ref_frame[ref]);
      const struct scale_factors *ref_scale_factors =
          get_ref_scale_factors_const(cm, this_mbmi->ref_frame[ref]);
      const struct scale_factors *const sf = ref_scale_factors;
#if CONFIG_F054_PIC_BOUNDARY
      const struct buf_2d pre_buf = {
        NULL,
        (plane == 1) ? ref_buf->buf.u_buffer : ref_buf->buf.v_buffer,
        ref_buf->buf.uv_width,
        ref_buf->buf.uv_height,
        ref_buf->buf.uv_crop_width,
        ref_buf->buf.uv_crop_height,
        ref_buf->buf.uv_stride,
      };
#else
      const struct buf_2d pre_buf = {
        NULL,
        (plane == 1) ? ref_buf->buf.u_buffer : ref_buf->buf.v_buffer,
        ref_buf->buf.uv_crop_width,
        ref_buf->buf.uv_crop_height,
        ref_buf->buf.uv_crop_width,
        ref_buf->buf.uv_crop_height,
        ref_buf->buf.uv_stride,
      };
#endif  // CONFIG_F054_PIC_BOUNDARY

      const MV mv = this_mbmi->mv[ref].as_mv;
      InterPredParams inter_pred_params;
      av1_init_inter_params(
          &inter_pred_params, chroma_width, chroma_height, pre_y + pixel_row,
          pre_x + pixel_col, pd->subsampling_x, pd->subsampling_y, xd->bd,
          mi->use_intrabc[0], sf, &pre_buf, this_mbmi->interp_fltr);
      inter_pred_params.conv_params =
          get_conv_params_no_round(ref, plane, NULL, 0, is_compound, xd->bd);

      if (is_thin_4xn_nx4_block(bsize) && has_second_ref(this_mbmi)) {
        assert(this_mbmi->interinter_comp.type != COMPOUND_DIFFWTD);
      }

      const MV mv_1_16th_pel = convert_mv_to_1_16th_pel(&mv);
      av1_build_one_inter_predictor(dst, dst_buf->stride, &mv_1_16th_pel,
                                    &inter_pred_params, xd, mi_x + pixel_col,
                                    mi_y + pixel_row, ref, mc_buf,
                                    calc_subpel_params_func);
    }
  }
  xd->mb_to_top_edge = mb_to_top_edge_start;
  xd->mb_to_bottom_edge = mb_to_bottom_edge_start;
  xd->mb_to_left_edge = mb_to_left_edge_start;
  xd->mb_to_right_edge = mb_to_right_edge_start;
}

#if CONFIG_BRU
AOM_INLINE void highbd_build_mc_border(const uint16_t *src, int src_stride,
                                       uint16_t *dst, int dst_stride, int x,
                                       int y, int b_w, int b_h, int w, int h) {
  // Get a pointer to the start of the real
  // data for this row.
  const uint16_t *ref_row = src - x - y * src_stride;

  if (y >= h)
    ref_row += (h - 1) * src_stride;
  else if (y > 0)
    ref_row += y * src_stride;

  do {
    int right = 0, copy;
    int left = x < 0 ? -x : 0;

    if (left > b_w) left = b_w;

    if (x + b_w > w) right = x + b_w - w;

    if (right > b_w) right = b_w;

    copy = b_w - left - right;

    if (left) aom_memset16(dst, ref_row[0], left);

    if (copy) memcpy(dst + left, ref_row + x + left, copy * sizeof(uint16_t));

    if (right) aom_memset16(dst + left + copy, ref_row[w - 1], right);

    dst += dst_stride;
    ++y;

    if (y > 0 && y < h) ref_row += src_stride;
  } while (--b_h);
}
/* Extend MC border for support SB in BRU
 * optimized decoder */
void bru_extend_mc_border(const AV1_COMMON *const cm, int mi_row, int mi_col,
                          BLOCK_SIZE bsize, YV12_BUFFER_CONFIG *src) {
  const int org_bw = mi_size_wide[bsize];
  const int org_bh = mi_size_high[bsize];
  const int ss_x = src->uv_width < src->y_width;
  const int ss_y = src->uv_height < src->y_height;
  uint16_t *src_data;
  uint16_t *dst_data;
  for (int plane = 0; plane < av1_num_planes(cm); plane++) {
    const int is_uv = plane > 0;
    const int s_x = is_uv ? ss_x : 0;
    const int s_y = is_uv ? ss_y : 0;
    PadBlock block;
    PadBlock block_cur;
#if CONFIG_F054_PIC_BOUNDARY
    const int frame_H = is_uv ? src->uv_height : src->y_height;
    const int frame_W = is_uv ? src->uv_width : src->y_width;
#else
    const int frame_H = is_uv ? src->uv_crop_height : src->y_crop_height;
    const int frame_W = is_uv ? src->uv_crop_width : src->y_crop_width;
#endif  // CONFIG_F054_PIC_BOUNDARY
    block.x0 = mi_col << (MI_SIZE_LOG2 - s_x);
    block.y0 = mi_row << (MI_SIZE_LOG2 - s_y);
    block.x1 = block.x0 + (org_bw << (MI_SIZE_LOG2 - s_x));
    block.y1 = block.y0 + (org_bh << (MI_SIZE_LOG2 - s_y));
    block_cur = block;
    if (block.x1 > frame_W) block.x1 = frame_W;
    if (block.y1 > frame_H) block.y1 = frame_H;
    block.x0 -= AOM_INTERP_EXTEND - 1;
    block.x1 += AOM_INTERP_EXTEND;
    block.y0 -= AOM_INTERP_EXTEND - 1;
    block.y1 += AOM_INTERP_EXTEND;
    if (block.x0 < 0 || block.x1 > frame_W - 1 || block.y0 < 0 ||
        block.y1 > frame_H - 1) {
      // BRU extend border should not touch
      // any pixel in the frame , but only in
      // the extend region if block -
      // AOM_INTERP_EXTEND >= 0, means this is
      // not on the top/left border, then
      // reset to current block
      if (block.x0 >= 0) block.x0 = block_cur.x0;
      if (block.y0 >= 0) block.y0 = block_cur.y0;
      // if block + AOM_INTERP_EXTEND <= W/H,
      // means this is not on the bottom/right
      // border, then reset to current block
      if (block.x1 <= frame_W) block.x1 = block_cur.x1;
      if (block.y1 <= frame_H) block.y1 = block_cur.y1;

      int b_w = block.x1 - block.x0;
      int b_h = block.y1 - block.y0;

      int stride = src->strides[is_uv];
      // Get reference block pointer.
      src_data = src->buffers[plane] +
                 scaled_buffer_offset(block.x0, block.y0, stride, NULL);
      dst_data = src->buffers[plane] +
                 scaled_buffer_offset(block.x0, block.y0, stride, NULL);

      highbd_build_mc_border(src_data, stride, dst_data, stride, block.x0,
                             block.y0, b_w, b_h, frame_W, frame_H);
    }
  }
}
#endif  // CONFIG_BRU

static inline void aom_memset16_optimized(uint16_t *dst, uint16_t value,
                                          int count) {
  while (count >= 8) {
    dst[0] = value;
    dst[1] = value;
    dst[2] = value;
    dst[3] = value;
    dst[4] = value;
    dst[5] = value;
    dst[6] = value;
    dst[7] = value;
    dst += 8;
    count -= 8;
  }
  while (count >= 4) {
    dst[0] = value;
    dst[1] = value;
    dst[2] = value;
    dst[3] = value;
    dst += 4;
    count -= 4;
  }
  while (count > 0) {
    *dst++ = value;
    count--;
  }
}

void refinemv_highbd_pad_mc_border(const uint16_t *src, int src_stride,
                                   uint16_t *dst, int dst_stride, int x0,
                                   int y0, int b_w, int b_h,
                                   const ReferenceArea *ref_area) {
  const int ref_x0 = ref_area->pad_block.x0;
  const int ref_y0 = ref_area->pad_block.y0;
  const int ref_x1 = ref_area->pad_block.x1;
  const int ref_y1 = ref_area->pad_block.y1;

  // Get a pointer to the start of the real
  // data for this row.
  const uint16_t *ref_row = src - x0 - y0 * src_stride;

  if (y0 >= ref_area->pad_block.y1)
    ref_row += (ref_area->pad_block.y1 - 1) * src_stride;
  else if (y0 >= ref_area->pad_block.y0)
    ref_row += y0 * src_stride;
  else
    ref_row += ref_area->pad_block.y0 * src_stride;

  int left = x0 < ref_x0 ? ref_x0 - x0 : 0;
  if (left > b_w) left = b_w;
  int right = (x0 + b_w > ref_x1) ? (x0 + b_w - ref_x1) : 0;
  if (right > b_w) right = b_w;
  const int copy = b_w - left - right;

  do {
    if (left)
      aom_memset16_optimized(dst, ref_row[ref_area->pad_block.x0], left);
    if (copy) memcpy(dst + left, ref_row + x0 + left, copy * sizeof(uint16_t));
    if (right)
      aom_memset16_optimized(dst + left + copy,
                             ref_row[ref_area->pad_block.x1 - 1], right);

    dst += dst_stride;
    ++y0;

    if (y0 > ref_y0 && y0 < ref_y1) ref_row += src_stride;
  } while (--b_h);
}

// check if padding is required during motion
// compensation return 1 means reference pixel
// is outside of the reference range and
// padding is required return 0 means no
// padding.
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
  // width/height is not a multiple of 8
  // pixels. Extension is needed in optical
  // flow refinement to obtain MV offsets
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

    // Skip border extension if block is
    // inside the frame.
    if (block->x0 < 0 || block->x1 > frame_width - 1 || block->y0 < 0 ||
        block->y1 > frame_height - 1) {
      return 1;
    }

    if (ref_area) {
      // Skip border extension if block is in
      // the reference area.
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

// perform padding of the motion compensated
// block if requires. Padding is performed if
// the motion compensated block is partially
// out of the reference area.
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

void dec_calc_subpel_params(const MV *const src_mv,
                            InterPredParams *const inter_pred_params,
                            const MACROBLOCKD *const xd, int mi_x, int mi_y,
                            uint16_t **pre, SubpelParams *subpel_params,
                            int *src_stride, PadBlock *block,
                            int use_optflow_refinement, MV32 *scaled_mv,
                            int *subpel_x_mv, int *subpel_y_mv) {
  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;

  const int bw = inter_pred_params->original_pu_width;
  const int bh = inter_pred_params->original_pu_height;
  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled) {
    int ssx = inter_pred_params->subsampling_x;
    int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
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

    // Get reference block top left
    // coordinate.
    block->x0 = pos_x >> SCALE_SUBPEL_BITS;
    block->y0 = pos_y >> SCALE_SUBPEL_BITS;

    // Get reference block bottom right
    // coordinate.
    block->x1 =
        ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
         SCALE_SUBPEL_BITS) +
        1;
    block->y1 =
        ((pos_y + (inter_pred_params->block_height - 1) * subpel_params->ys) >>
         SCALE_SUBPEL_BITS) +
        1;

    MV temp_mv;
    temp_mv = clamp_mv_to_umv_border_sb(
        xd, src_mv, bw, bh, use_optflow_refinement,
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
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
        xd, src_mv, bw, bh, use_optflow_refinement,
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;

    // Get reference block top left
    // coordinate.
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    block->x0 = pos_x >> SUBPEL_BITS;
    block->y0 = pos_y >> SUBPEL_BITS;

    // Get reference block bottom right
    // coordinate.
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

  if (inter_pred_params->border_data.enable_bacp) {
    subpel_params->x0 = block->x0;
    subpel_params->x1 = block->x1;
    subpel_params->y0 = block->y0;
    subpel_params->y1 = block->y1;
  }
}

void common_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
    int use_optflow_refinement, uint16_t **mc_buf, uint16_t **pre,
    SubpelParams *subpel_params, int *src_stride) {
  (void)ref;
  (void)mc_buf;

  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  assert(inter_pred_params->use_ref_padding);
  dec_calc_subpel_params(
      src_mv, inter_pred_params, xd, mi_x, mi_y, pre, subpel_params, src_stride,
      &block, use_optflow_refinement, &scaled_mv, &subpel_x_mv, &subpel_y_mv);

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
                              int use_optflow_refinement, uint16_t **pre,
                              SubpelParams *subpel_params, int *src_stride,
                              ReferenceArea *ref_area) {
  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;

  dec_calc_subpel_params(
      src_mv, inter_pred_params, xd, mi_x, mi_y, pre, subpel_params, src_stride,
      &block, use_optflow_refinement, &scaled_mv, &subpel_x_mv, &subpel_y_mv);

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

void av1_get_reference_area_with_padding_single(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, const MB_MODE_INFO *mi,
    const MV mv, int bw, int bh, int mi_x, int mi_y, ReferenceArea *ref_area,
    int pu_width, int pu_height, int ref) {
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;
  struct macroblockd_plane *const pd = &xd->plane[plane];

  int row_start = 0;
  int col_start = 0;
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
  col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;

  const int pre_x = ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);

  const struct scale_factors *const sf = is_tip
                                             ? cm->tip_ref.ref_scale_factor[ref]
                                             : xd->block_ref_scale_factors[ref];
  const struct buf_2d *const pre_buf = &pd->pre[ref];

  // initialize the reference buffer
  ref_area->pad_block.x0 = 0;
  ref_area->pad_block.y0 = 0;
  ref_area->pad_block.x1 = cm->width;
  ref_area->pad_block.y1 = cm->height;
  ref_area->paded_ref_buf_stride = REF_BUFFER_WIDTH;

  InterPredParams inter_pred_params;
  av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd,
                        mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);

  inter_pred_params.original_pu_width = pu_width;
  inter_pred_params.original_pu_height = pu_height;

  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;

  const MV *src_mv = &mv;
  get_ref_area_info(src_mv, &inter_pred_params, xd, mi_x, mi_y, 0, &src,
                    &subpel_params, &src_stride, ref_area);
}

static void get_ref_area_info_warp(const MV *const src_mv,
                                   InterPredParams *const inter_pred_params,
                                   MACROBLOCKD *const xd, int mi_x, int mi_y,
                                   int use_optflow_refinement, uint16_t **pre,
                                   SubpelParams *subpel_params, int *src_stride,
                                   WarpBoundaryBox *ref_area) {
  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;

  dec_calc_subpel_params(
      src_mv, inter_pred_params, xd, mi_x, mi_y, pre, subpel_params, src_stride,
      &block, use_optflow_refinement, &scaled_mv, &subpel_x_mv, &subpel_y_mv);

  struct buf_2d *const pre_buf = &inter_pred_params->ref_frame_buf;
  int frame_height = pre_buf->height;
  int frame_width = pre_buf->width;
  block.x0 -= REF_LEFT_BORDER_WARP;
  block.x1 += REF_RIGHT_BORDER_WARP;
  block.y0 -= REF_TOP_BORDER_WARP;
  block.y1 += REF_BOTTOM_BORDER_WARP;

  ref_area->x0 = CLIP(block.x0, 0, frame_width - 1);
  ref_area->y0 = CLIP(block.y0, 0, frame_height - 1);
  ref_area->x1 = CLIP(block.x1, 1, frame_width);
  ref_area->y1 = CLIP(block.y1, 1, frame_height);
}

void av1_get_reference_area_with_padding_single_warp(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    const MV mv, int bw, int bh, int mi_x, int mi_y, WarpBoundaryBox *ref_area,
    int pu_width, int pu_height, int ref) {
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;
  struct macroblockd_plane *const pd = &xd->plane[plane];

  int row_start = 0;
  int col_start = 0;
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
  col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;

  const int pre_x = ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);

  const struct scale_factors *const sf = is_tip
                                             ? cm->tip_ref.ref_scale_factor[ref]
                                             : xd->block_ref_scale_factors[ref];
  const struct buf_2d *const pre_buf = &pd->pre[ref];

  // initialize the reference buffer
  ref_area->x0 = 0;
  ref_area->y0 = 0;
  ref_area->x1 = cm->width;
  ref_area->y1 = cm->height;

  InterPredParams inter_pred_params;
  av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd,
                        mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);

  inter_pred_params.original_pu_width = pu_width;
  inter_pred_params.original_pu_height = pu_height;

  SubpelParams subpel_params;
  uint16_t *src;
  int src_stride;

  const MV *src_mv = &mv;
  get_ref_area_info_warp(src_mv, &inter_pred_params, xd, mi_x, mi_y, 0, &src,
                         &subpel_params, &src_stride, ref_area);
}

void av1_get_reference_area_with_padding(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         int plane, MB_MODE_INFO *mi,
                                         const MV mv[2], int bw, int bh,
                                         int mi_x, int mi_y,
                                         ReferenceArea ref_area[2],
                                         int pu_width, int pu_height) {
  const int is_tip = mi->ref_frame[0] == TIP_FRAME;
  assert(IMPLIES(!is_tip, has_second_ref(mi)));
  assert(!is_intrabc_block(mi, xd->tree_type));
  struct macroblockd_plane *const pd = &xd->plane[plane];

  if (is_tip && bw < 8 && bh < 8) return;

  int row_start = 0;
  int col_start = 0;
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
  col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;

  const int pre_x = ((mi_x + MI_SIZE * col_start) >> pd->subsampling_x);
  const int pre_y = ((mi_y + MI_SIZE * row_start) >> pd->subsampling_y);

  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf =
        is_tip ? cm->tip_ref.ref_scale_factor[ref]
               : xd->block_ref_scale_factors[ref];
    const struct buf_2d *const pre_buf = &pd->pre[ref];

    // initialize the reference buffer
    ref_area[ref].pad_block.x0 = 0;
    ref_area[ref].pad_block.y0 = 0;
    ref_area[ref].pad_block.x1 = cm->width;
    ref_area[ref].pad_block.y1 = cm->height;
    ref_area[ref].paded_ref_buf_stride = REF_BUFFER_WIDTH;

    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);

    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;

    SubpelParams subpel_params;
    uint16_t *src;
    int src_stride;

    assert(!inter_pred_params.use_ref_padding);

    const MV *src_mv = ref == 0 ? &mv[0] : &mv[1];
    get_ref_area_info(src_mv, &inter_pred_params, xd, mi_x, mi_y, 0, &src,
                      &subpel_params, &src_stride, &ref_area[ref]);
  }
}

void av1_refinemv_build_predictors(MACROBLOCKD *xd, int mi_x, int mi_y,
                                   uint16_t **mc_buf,
                                   CalcSubpelParamsFunc calc_subpel_params_func,
                                   uint16_t *dst_ref0, uint16_t *dst_ref1,
                                   int dst_stride, MV mv0, MV mv1,
                                   InterPredParams *inter_pred_params) {
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
                            ref, 0, mc_buf, &src, &subpel_params, &src_stride);
    assert(inter_pred_params[ref].comp_mode == UNIFORM_SINGLE ||
           inter_pred_params[ref].comp_mode == UNIFORM_COMP);
    av1_make_inter_predictor(src, src_stride, dst_ref, dst_stride,
                             &inter_pred_params[ref], &subpel_params);
  }
}

void apply_mv_refinement(const AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                         MB_MODE_INFO *mi, int bw, int bh, int mi_x, int mi_y,
                         uint16_t **mc_buf, const MV mv[2],
                         CalcSubpelParamsFunc calc_subpel_params_func,
                         int pre_x, int pre_y, uint16_t *dst_ref0,
                         uint16_t *dst_ref1, uint16_t **dst_ref0_ptr,
                         uint16_t **dst_ref1_ptr, MV *best_mv_ref, int pu_width,
                         int pu_height, ReferenceArea ref_area[2]) {
  // initialize basemv as best MV
  best_mv_ref[0] = mv[0];
  best_mv_ref[1] = mv[1];

  // Check if any component of the MV exceed
  // maximum value If any of the MV components
  // exceed the maximum value, do not refine
  // mv
  const int max_sr = 2;  // Maximum search range at unit of
                         // 1-pel
  for (int k = 0; k < 2; k++) {
    for (int comp = 0; comp < 2; comp++) {
      int val = comp == 0 ? mv[k].row : mv[k].col;
      int min_mv_comp = val - max_sr * 8;
      int max_mv_comp = val + max_sr * 8;
#if CONFIG_SUBBLK_REF_EXT
      min_mv_comp -= 8 * SUBBLK_REF_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT
      if (min_mv_comp < (MV_LOW + 1) || min_mv_comp > (MV_UPP - 1) ||
          max_mv_comp < (MV_LOW + 1) || max_mv_comp > (MV_UPP - 1))
        return;
    }
  }

#if CONFIG_SUBBLK_REF_EXT
  bw += 2 * SUBBLK_REF_EXT_LINES;
  bh += 2 * SUBBLK_REF_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT

  const int dsts_offset = (REFINEMV_SUBBLOCK_WIDTH +
                           2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)) *
                          (REFINEMV_SUBBLOCK_HEIGHT +
                           2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES));
  uint16_t *dsts0[2] = { dst_ref0, dst_ref0 + dsts_offset };
  uint16_t *dsts1[2] = { dst_ref1, dst_ref1 + dsts_offset };
  int dsts_cur = 0;

  const MV center_mvs[2] = { best_mv_ref[0], best_mv_ref[1] };
  assert(mi->refinemv_flag < REFINEMV_NUM_MODES);
  assert(cm->seq_params.enable_refinemv);
#if CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
  assert(IMPLIES(is_tip_ref_frame(mi->ref_frame[0]),
                 cm->seq_params.enable_tip_refinemv));
#endif  // CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG

  // Generate MV independent inter_pred_params
  // for both references
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
    const struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];

    av1_init_inter_params(&inter_pred_params[ref], bw, bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, BILINEAR);

    inter_pred_params[ref].original_pu_width = pu_width;
    inter_pred_params[ref].original_pu_height = pu_height;
    inter_pred_params[ref].conv_params = get_conv_params_no_round(
        0, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    assert(inter_pred_params[ref].mode == TRANSLATION_PRED);
    assert(inter_pred_params[ref].comp_mode == UNIFORM_SINGLE);
    assert(inter_pred_params[ref].conv_params.is_compound == 0);
    assert(inter_pred_params[ref].conv_params.do_average == 0);
    assert(mi->interinter_comp.type == COMPOUND_AVERAGE);
  }

  int switchable_refinemv_flags =
      (mi->ref_frame[0] != TIP_FRAME) && switchable_refinemv_flag(cm, mi);

  // If we signal the refinemv_flags we do not
  // select sad0 Set sad0 a large value so
  // that it does not be selected
#if CONFIG_SUBBLK_REF_EXT
  const int dst_stride = REFINEMV_SUBBLOCK_WIDTH +
                         2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES);
#else
  const int dst_stride = REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT
  int sad0 = INT32_MAX >> 1;
  if (!switchable_refinemv_flags) {
    av1_refinemv_build_predictors(
        xd, mi_x, mi_y, mc_buf, calc_subpel_params_func, dst_ref0, dst_ref1,
        dst_stride, center_mvs[0], center_mvs[1], inter_pred_params);
    sad0 = get_refinemv_sad(dst_ref0, dst_ref1, dst_stride, bw, bh, xd->bd);
    *dst_ref0_ptr =
        dst_ref0 + SUBBLK_REF_EXT_LINES * dst_stride + SUBBLK_REF_EXT_LINES;
    *dst_ref1_ptr =
        dst_ref1 + SUBBLK_REF_EXT_LINES * dst_stride + SUBBLK_REF_EXT_LINES;
    dsts_cur = !dsts_cur;
    dst_ref0 = dsts0[dsts_cur];
    dst_ref1 = dsts1[dsts_cur];
  }
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
  MV refined_mv[2];
  refined_mv[0] = center_mvs[0];
  refined_mv[1] = center_mvs[1];

  static const MV neighbors[DMVR_SEARCH_NUM_NEIGHBORS] = {
    { -2, -2 }, { -2, -1 }, { -2, 0 }, { -2, 1 }, { -2, 2 }, { -1, -2 },
    { -1, -1 }, { -1, 0 },  { -1, 1 }, { -1, 2 }, { 0, -2 }, { 0, -1 },
    { 0, 1 },   { 0, 2 },   { 1, -2 }, { 1, -1 }, { 1, 0 },  { 1, 1 },
    { 1, 2 },   { 2, -2 },  { 2, -1 }, { 2, 0 },  { 2, 1 },  { 2, 2 }
  };

  MV best_offset = { 0, 0 };
  // Prediction is generated at once for
  // (bw+4) x (bh+4) block, by extending 2
  // samples (search range of the refinement
  // stage) on each side. Later, the
  // prediction buffers are appropriately
  // offset for SAD calculation.
  const int ext_bw = bw + 4;
  const int ext_bh = bh + 4;
  for (int ref = 0; ref < 2; ref++) {
    inter_pred_params[ref].use_ref_padding = 1;
    inter_pred_params[ref].ref_area = &ref_area[ref];
    inter_pred_params[ref].block_width = ext_bw;
    inter_pred_params[ref].block_height = ext_bh;
    inter_pred_params[ref].original_pu_width = pu_width + 4;
    inter_pred_params[ref].original_pu_height = pu_height + 4;
    refined_mv[ref].row -= 8 * DMVR_SEARCH_EXT_LINES;
    refined_mv[ref].col -= 8 * DMVR_SEARCH_EXT_LINES;
  }

  av1_refinemv_build_predictors(xd, mi_x, mi_y, mc_buf, calc_subpel_params_func,
                                dst_ref0, dst_ref1, dst_stride, refined_mv[0],
                                refined_mv[1], inter_pred_params);

  for (int idx = 0; idx < DMVR_SEARCH_NUM_NEIGHBORS; ++idx) {
    const MV offset = { neighbors[idx].row, neighbors[idx].col };

    uint16_t *dst_ref0_offset =
        dst_ref0 + (2 + offset.row) * dst_stride + 2 + offset.col;
    uint16_t *dst_ref1_offset =
        dst_ref1 + (2 - offset.row) * dst_stride + 2 - offset.col;

    const int this_sad = get_refinemv_sad(dst_ref0_offset, dst_ref1_offset,
                                          dst_stride, bw, bh, xd->bd);

    if (this_sad < min_sad) {
      min_sad = this_sad;
      best_offset = offset;
      if (dst_ref0_ptr != NULL && dst_ref1_ptr != NULL) {
        *dst_ref0_ptr = dst_ref0_offset + SUBBLK_REF_EXT_LINES * dst_stride +
                        SUBBLK_REF_EXT_LINES;
        *dst_ref1_ptr = dst_ref1_offset + SUBBLK_REF_EXT_LINES * dst_stride +
                        SUBBLK_REF_EXT_LINES;
      }
    }
  }

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

// This function consolidates the refinemv
// enabling check for both TIP ref mode blocks
// and non-TIP ref mode blocks.
static AOM_INLINE int is_sub_block_refinemv_enabled(const AV1_COMMON *cm,
                                                    const MB_MODE_INFO *mi,
                                                    int tip_ref_frame) {
  if (!cm->seq_params.enable_refinemv) return 0;

  if (tip_ref_frame) {
#if CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
    if (!cm->seq_params.enable_tip_refinemv) return 0;
#endif  // CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
#if CONFIG_TIP_ENHANCEMENT
    const int tip_wtd_index = cm->tip_global_wtd_index;
    const int8_t tip_weight = tip_weighting_factors[tip_wtd_index];
    return (cm->has_both_sides_refs && tip_weight == TIP_EQUAL_WTD);
#else
#if CONFIG_TIP_LD
    return (cm->has_both_sides_refs);
#else
    return 1;
#endif  // CONFIG_TIP_LD
#endif  // CONFIG_TIP_ENHANCEMENT
  } else {
    int apply_sub_block_refinemv =
        mi->refinemv_flag && !is_tip_ref_frame(mi->ref_frame[0]);

    if (apply_sub_block_refinemv && default_refinemv_modes(mi))
      apply_sub_block_refinemv &=
          (mi->comp_group_idx == 0 &&
           mi->interinter_comp.type == COMPOUND_AVERAGE);
    return apply_sub_block_refinemv;
  }
}

// check if the refinemv mode is allowed for a
// given block
static INLINE int is_mv_refine_allowed(const AV1_COMMON *cm,
                                       const MB_MODE_INFO *mbmi, int plane) {
  if (plane != 0) return 0;
  if (is_tip_ref_frame(mbmi->ref_frame[0]))
    return is_refinemv_allowed_tip_blocks(cm, mbmi);
  return 1;
}

// Calculate the SAD of 2 compound prediction
// blocks and use it to decide whether or not
// to skip the optical flow MV refinement for
// the TIP block.
static AOM_INLINE int skip_opfl_refine_with_tip(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, int bw, int bh,
    int pu_width, int pu_height, int mi_x, int mi_y, uint16_t **mc_buf,
    MV best_mv_ref[2], CalcSubpelParamsFunc calc_subpel_params_func,
    uint16_t *dst0, uint16_t *dst1, int dst_stride, int do_pred) {
  if (do_pred) {
    MB_MODE_INFO mbmi;
    memset(&mbmi, 0, sizeof(mbmi));
    mbmi.mv[0].as_mv = best_mv_ref[0];
    mbmi.mv[1].as_mv = best_mv_ref[1];
    mbmi.ref_frame[0] = TIP_FRAME;
    mbmi.ref_frame[1] = NONE_FRAME;
    mbmi.interp_fltr = cm->tip_interp_filter;
    mbmi.use_intrabc[xd->tree_type == CHROMA_PART] = 0;
    mbmi.use_intrabc[0] = 0;
    mbmi.mode = NEWMV;
    mbmi.motion_mode = SIMPLE_TRANSLATION;
    mbmi.sb_type[PLANE_TYPE_Y] = BLOCK_8X8;
    mbmi.interinter_comp.type = COMPOUND_AVERAGE;
    mbmi.max_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
    mbmi.pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
    mbmi.morph_pred = 0;

    assert(dst_stride == bw);
    InterPredParams params0, params1;
    av1_opfl_build_inter_predictor(cm, xd, plane, &mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params0, calc_subpel_params_func, 0,
                                   dst0, &best_mv_ref[0], pu_width, pu_height);
    av1_opfl_build_inter_predictor(cm, xd, plane, &mbmi, bw, bh, mi_x, mi_y,
                                   mc_buf, &params1, calc_subpel_params_func, 1,
                                   dst1, &best_mv_ref[1], pu_width, pu_height);
  }
  const int bd = cm->seq_params.bit_depth;
  const unsigned int sad_thres =
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT ? 15 : 6;

  const unsigned int sad =
      get_highbd_sad(dst0, dst_stride, dst1, dst_stride, bd, 8, 8);

  return (sad < sad_thres);
}

static void build_inter_predictors_8x8_and_bigger_refinemv(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    int build_for_decode, int bw, int bh, int mi_x, int mi_y, uint16_t **mc_buf,
    MV mi_mv[2], CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst,
    int dst_stride, int subblk_start_x, int subblk_start_y, int pu_width,
    int pu_height, uint16_t *dst0_16_refinemv, uint16_t *dst1_16_refinemv,
    int row_start, int col_start, MV *sb_refined_mv, MV *chroma_refined_mv,
    int build_for_refine_mv_only, ReferenceArea ref_area[2], int_mv *mv_refined,
    int *opfl_vxy_bufs) {
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  const int is_compound = has_second_ref(mi) || tip_ref_frame;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  assert(!is_intrabc_block(mi, xd->tree_type));
  assert(is_compound);
  assert(!mi->bawp_flag[0]);
  assert(!is_masked_compound_type(mi->interinter_comp.type));

  assert(mi->cwp_idx == CWP_EQUAL);

  int is_global[2] = { 0, 0 };
  if (!tip_ref_frame) {
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
      const WarpedMotionParams *const wm =
          &xd->global_motion[mi->ref_frame[ref]];
      is_global[ref] = is_global_mv_block(mi, wm->wmtype);
    }
  }

  assert(!is_global[0] && !is_global[1]);

  const int pre_x = (mi_x + MI_SIZE * col_start) >> pd->subsampling_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> pd->subsampling_y;

  uint16_t *refinemv_ref0 = NULL;
  uint16_t *refinemv_ref1 = NULL;

  int apply_refinemv = is_mv_refine_allowed(cm, mi, plane);

  MV best_mv_ref[2] = { mi_mv[0], mi_mv[1] };
  if (apply_refinemv) {
    uint16_t *dst_ref0 = NULL, *dst_ref1 = NULL;
    dst_ref0 = &dst0_16_refinemv[0];
    dst_ref1 = &dst1_16_refinemv[0];

    apply_mv_refinement(cm, xd, plane, mi, bw, bh, mi_x, mi_y, mc_buf, mi_mv,
                        calc_subpel_params_func, pre_x, pre_y, dst_ref0,
                        dst_ref1, &refinemv_ref0, &refinemv_ref1, best_mv_ref,
                        pu_width, pu_height, ref_area);
    if (sb_refined_mv) {
      // store the DMVR refined MV so that
      // chroma can use it
      sb_refined_mv[0] = best_mv_ref[0];
      sb_refined_mv[1] = best_mv_ref[1];
    }
    assert(IMPLIES(plane, !build_for_refine_mv_only));
    // if build_for_refine_mv_only is
    // non-zero, we build only to get the
    // refinemv values The actual prediction
    // values are not necessary
    if (build_for_refine_mv_only) {
      return;
    }
  } else if (!tip_ref_frame) {
    best_mv_ref[0] = chroma_refined_mv[0];
    best_mv_ref[1] = chroma_refined_mv[1];
  }

  if (tip_ref_frame && plane == 0) {
    mv_refined[0].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[0]);
    mv_refined[1].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[1]);
  }
  int use_optflow_refinement =
      is_optflow_refinement_enabled(cm, xd, mi, plane, tip_ref_frame);
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));

  // Optical flow refinement with masked comp
  // types or with non-sharp interpolation
  // filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
  assert(IMPLIES(use_optflow_refinement && tip_ref_frame, plane == 0));

  int use_4x4 = tip_ref_frame ? 0 : 1;
  int n = opfl_get_subblock_size(bw, bh, plane, use_4x4);
  const int n_blocks = (bw / n) * (bh / n);

  // optical flow refined MVs in a subblock
  // (16x16) unit
  int_mv mv_refined_sb[4 * 2];
  memset(mv_refined_sb, 0, 4 * 2 * sizeof(int_mv));
  const int opfl_mv_stride = pu_width / n;
  const int opfl_sb_idx =
      (subblk_start_y / n) * opfl_mv_stride + subblk_start_x / n;
  const int sb_rows = bh / n;
  const int sb_cols = bw / n;

  if (use_optflow_refinement && plane == 0) {
    // Pointers to hold optical flow MV
    // offsets in a subblock unit.
    int vx0_sb[4] = { 0 };
    int vx1_sb[4] = { 0 };
    int vy0_sb[4] = { 0 };
    int vy1_sb[4] = { 0 };

    // Pointers to hold gradient and dst
    // buffers.
    int16_t *gx0 = xd->opfl_gxy_bufs;
    int16_t *gx1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 1);
    int16_t *gy0 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 2);
    int16_t *gy1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 3);

    // Initialize refined mv
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];

    // Refine MV using optical flow. The final
    // output MV will be in 1/16 precision.
    uint16_t *dst0 = xd->opfl_dst_bufs;
    uint16_t *dst1 = xd->opfl_dst_bufs + MAX_SB_SQUARE;
    int do_pred = 1;
    int opfl_dst_stride = bw;
    if (refinemv_ref0 != NULL && refinemv_ref1 != NULL) {
      dst0 = refinemv_ref0;
      dst1 = refinemv_ref1;
#if CONFIG_SUBBLK_REF_EXT
      opfl_dst_stride = REFINEMV_SUBBLOCK_WIDTH +
                        2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES);
#else
      opfl_dst_stride = REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES;
#endif  // CONFIG_SUBBLK_REF_EXT
      do_pred = 0;
    }

    if (tip_ref_frame) {
      use_optflow_refinement = !skip_opfl_refine_with_tip(
          cm, xd, plane, bw, bh, pu_width, pu_height, mi_x, mi_y, mc_buf,
          best_mv_ref, calc_subpel_params_func, dst0, dst1, opfl_dst_stride,
          do_pred);
      do_pred = 0;
    }
    if (use_optflow_refinement) {
      for (int mvi = 0; mvi < n_blocks; mvi++) {
        mv_refined_sb[mvi * 2].as_mv = mv0;
        mv_refined_sb[mvi * 2 + 1].as_mv = mv1;
      }
      av1_get_optflow_based_mv(
          cm, xd, plane, mi, mv_refined_sb, bw, bh, mi_x, mi_y,
          build_for_decode, mc_buf, calc_subpel_params_func, gx0, gy0, gx1, gy1,
          vx0_sb, vy0_sb, vx1_sb, vy1_sb, dst0, dst1, opfl_dst_stride, do_pred,
          use_4x4, best_mv_ref, pu_width, pu_height);
      for (int i = 0; i < sb_rows; i++) {
        for (int j = 0; j < sb_cols; j++) {
          int mvidx = opfl_sb_idx + i * opfl_mv_stride + j;
          int mvidx_sb = i * sb_cols + j;
          mv_refined[2 * mvidx].as_mv = mv_refined_sb[2 * mvidx_sb].as_mv;
          mv_refined[2 * mvidx + 1].as_mv =
              mv_refined_sb[2 * mvidx_sb + 1].as_mv;
          // Store subblock MV delta at the
          // prediction block level
          opfl_vxy_bufs[mvidx] = vx0_sb[mvidx_sb];
          opfl_vxy_bufs[N_OF_OFFSETS * 1 + mvidx] = vx1_sb[mvidx_sb];
          opfl_vxy_bufs[N_OF_OFFSETS * 2 + mvidx] = vy0_sb[mvidx_sb];
          opfl_vxy_bufs[N_OF_OFFSETS * 3 + mvidx] = vy1_sb[mvidx_sb];
        }
      }
    }
  }

  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp =
      tip_ref_frame
          ? cm->features.enable_imp_msk_bld &&
                !av1_is_scaled(cm->tip_ref.ref_scale_factor[0]) &&
                !av1_is_scaled(cm->tip_ref.ref_scale_factor[1])
          : use_border_aware_compound(cm, xd, mi) && mi->cwp_idx == CWP_EQUAL &&
                cm->features.enable_imp_msk_bld;

  int opfl_sub_bw = OF_BSIZE;
  int opfl_sub_bh = OF_BSIZE;
  opfl_subblock_size_plane(xd, plane, use_4x4, &opfl_sub_bw, &opfl_sub_bh);

  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf =
        tip_ref_frame ? cm->tip_ref.ref_scale_factor[ref]
                      : xd->block_ref_scale_factors[ref];
    struct buf_2d *const pre_buf = &pd->pre[ref];

    MV mv = best_mv_ref[ref];
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };
    InterPredParams inter_pred_params;
    const int comp_bw = tip_ref_frame ? (bw >> ss_x) : bw;
    const int comp_bh = tip_ref_frame ? (bh >> ss_y) : bh;

    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
    const int use_ref_padding =
        tip_ref_frame ? ((apply_refinemv || use_optflow_refinement) ||
                         (plane && (comp_bw > 4 || comp_bh > 4)))
                      : 1;
    if (use_ref_padding) {
      inter_pred_params.use_ref_padding = 1;
      inter_pred_params.ref_area = &ref_area[ref];
    }

    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;

    if (is_compound) av1_init_comp_mode(&inter_pred_params);
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point
                              // to the first
                              // ref
    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);
    assert(inter_pred_params.mode != WARP_PRED);

    if (is_compound) {
      inter_pred_params.sb_type = tip_ref_frame ?
#if CONFIG_FLEX_TIP_BLK_SIZE
                                                get_tip_bsize_from_bw_bh(bw, bh)
#else
                                                BLOCK_8X8
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
                                                : mi->sb_type[PLANE_TYPE_Y];
      inter_pred_params.mask_comp = mi->interinter_comp;
    }

    if (use_optflow_refinement && plane == 0) {
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr,
                                                       opfl_sub_bw);

      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr,
                                                       opfl_sub_bh);

      av1_opfl_rebuild_inter_predictor(
          dst, dst_stride, plane, mv_refined_sb, &inter_pred_params, xd, mi_x,
          mi_y, build_for_decode, cm, pu_width, ref, mc_buf,
          calc_subpel_params_func, use_4x4);
      continue;
    }
    const MV mv_1_16th_pel = (tip_ref_frame && plane)
                                 ? mv_refined[ref].as_mv
                                 : convert_mv_to_1_16th_pel(&mv);
    av1_build_one_inter_predictor(dst, dst_stride, &mv_1_16th_pel,
                                  &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
  }
}

static void build_inter_predictors_8x8_and_bigger(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    const BUFFER_SET *dst_orig, int build_for_decode, int bw, int bh, int mi_x,
    int mi_y, uint16_t **mc_buf, MV mi_mv[2],
    CalcSubpelParamsFunc calc_subpel_params_func, uint16_t *dst, int dst_stride,
    int pu_width, int pu_height, int build_for_refine_mv_only,
    bool *ext_warp_used, int_mv *mv_refined,
    REFINEMV_SUBMB_INFO *block_refinemv_subinfo, int *opfl_vxy_bufs) {
  // In case of chroma, even for 4xN and Nx4
  // blocks, single prediction is used.
  int singleref_for_compound =
      plane && has_second_ref(mi) &&
      is_thin_4xn_nx4_block(mi->sb_type[xd->tree_type == CHROMA_PART]);
#if CONFIG_TIP_ENHANCEMENT
  const int tip_wtd_index = cm->tip_global_wtd_index;
  const int8_t tip_weight = tip_weighting_factors[tip_wtd_index];
#endif  // CONFIG_TIP_ENHANCEMENT
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  const int is_compound = (!singleref_for_compound && has_second_ref(mi)) ||
#if CONFIG_TIP_ENHANCEMENT
                          (tip_ref_frame && tip_weight != TIP_SINGLE_WTD)
#else
                          tip_ref_frame
#endif  // CONFIG_TIP_ENHANCEMENT
      ;
  const int is_intrabc = is_intrabc_block(mi, xd->tree_type);
  assert(IMPLIES(is_intrabc, !is_compound));
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;

  assert(IMPLIES(mi->refinemv_flag, !is_intrabc));
  assert(IMPLIES(mi->refinemv_flag, is_compound));
  assert(IMPLIES(mi->refinemv_flag && switchable_refinemv_flag(cm, mi),
                 mi->interinter_comp.type == COMPOUND_AVERAGE));
  assert(IMPLIES(mi->refinemv_flag, mi->bawp_flag[0] == 0));
  assert(IMPLIES(mi->refinemv_flag, mi->interp_fltr == MULTITAP_SHARP));

  assert(IMPLIES(tip_ref_frame,
                 mi->use_intrabc[0] == 0 && mi->use_intrabc[1] == 0));
  assert(IMPLIES(tip_ref_frame, mi->motion_mode == SIMPLE_TRANSLATION));
  assert(IMPLIES(tip_ref_frame, mi->interinter_comp.type == COMPOUND_AVERAGE));

  assert(IMPLIES(
      mi->refinemv_flag,
      !is_thin_4xn_nx4_block(mi->sb_type[xd->tree_type == CHROMA_PART])));
  if (is_thin_4xn_nx4_block(mi->sb_type[xd->tree_type == CHROMA_PART]) &&
      has_second_ref(mi)) {
    assert(mi->interinter_comp.type != COMPOUND_DIFFWTD);
  }

  if (is_sub_block_refinemv_enabled(cm, mi, tip_ref_frame)) {
    assert(IMPLIES(mi->refinemv_flag, mi->cwp_idx == CWP_EQUAL));
#if CONFIG_FLEX_TIP_BLK_SIZE
    const int sub_block_width = !tip_ref_frame
                                    ? (REFINEMV_SUBBLOCK_WIDTH >> ss_x)
                                    : REFINEMV_SUBBLOCK_WIDTH;
    const int sub_block_height = !tip_ref_frame
                                     ? (REFINEMV_SUBBLOCK_HEIGHT >> ss_y)
                                     : REFINEMV_SUBBLOCK_HEIGHT;
    const int refinemv_sb_size_width = AOMMIN(sub_block_width, bw);
    const int refinemv_sb_size_height = AOMMIN(sub_block_height, bh);
#else
    int refinemv_sb_size_width =
        AOMMIN((REFINEMV_SUBBLOCK_WIDTH >> pd->subsampling_x), bw);
    int refinemv_sb_size_height =
        AOMMIN(REFINEMV_SUBBLOCK_HEIGHT >> pd->subsampling_y, bh);
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
#if CONFIG_SUBBLK_REF_EXT
    uint16_t
        dst0_16_refinemv[2 *
                         (REFINEMV_SUBBLOCK_WIDTH +
                          2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)) *
                         (REFINEMV_SUBBLOCK_HEIGHT +
                          2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))];
    uint16_t
        dst1_16_refinemv[2 *
                         (REFINEMV_SUBBLOCK_WIDTH +
                          2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES)) *
                         (REFINEMV_SUBBLOCK_HEIGHT +
                          2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))];
#else
    uint16_t dst0_16_refinemv
        [2 * (REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES) *
         (REFINEMV_SUBBLOCK_HEIGHT + 2 * DMVR_SEARCH_EXT_LINES)];
    uint16_t dst1_16_refinemv
        [2 * (REFINEMV_SUBBLOCK_WIDTH + 2 * DMVR_SEARCH_EXT_LINES) *
         (REFINEMV_SUBBLOCK_HEIGHT + 2 * DMVR_SEARCH_EXT_LINES)];
#endif  // CONFIG_SUBBLK_REF_EXT

    ReferenceArea ref_area[2];
#if !CONFIG_SUBBLK_PAD
    av1_get_reference_area_with_padding(cm, xd, plane, mi, mi_mv, bw, bh, mi_x,
                                        mi_y, ref_area, pu_width, pu_height);
#endif  //! CONFIG_SUBBLK_PAD
    CONV_BUF_TYPE *tmp_conv_dst = xd->tmp_conv_dst;
    assert(bw % refinemv_sb_size_width == 0);
    assert(bh % refinemv_sb_size_height == 0);
    for (int h = 0; h < bh; h += refinemv_sb_size_height) {
      for (int w = 0; w < bw; w += refinemv_sb_size_width) {
        const int x = mi_x + w * (1 << pd->subsampling_x);
        const int y = mi_y + h * (1 << pd->subsampling_y);
        if (is_subblock_outside(x, y, cm->mi_params.mi_cols,
                                cm->mi_params.mi_rows, build_for_decode)) {
          continue;
        }
        uint16_t *dst_buf = dst + h * dst_stride + w;
        xd->tmp_conv_dst = tmp_conv_dst + h * MAX_SB_SIZE + w;

        const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
        const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
        int row_start =
            plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
        int col_start =
            plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;
        MV luma_refined_mv[2] = { { mi_mv[0].row, mi_mv[0].col },
                                  { mi_mv[1].row, mi_mv[1].col } };

        MV chroma_refined_mv[2] = {
          { mi->mv[0].as_mv.row, mi->mv[0].as_mv.col },
          { mi->mv[1].as_mv.row, mi->mv[1].as_mv.col }
        };

        if (plane != 0) {
          int luma_h = (h << pd->subsampling_y);
          int luma_w = (w << pd->subsampling_x);
          REFINEMV_SUBMB_INFO
          *refinemv_subinfo =
              &block_refinemv_subinfo[(luma_h >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                      (luma_w >> MI_SIZE_LOG2)];
          chroma_refined_mv[0] = refinemv_subinfo->refinemv[0].as_mv;
          chroma_refined_mv[1] = refinemv_subinfo->refinemv[1].as_mv;
        }
#if CONFIG_SUBBLK_PAD
        // sub_mi_x, and sub_mi_y are the
        // top-left position of the luma
        // samples of the sub-block
        const int sub_mi_x = mi_x + w * (1 << pd->subsampling_x);
        const int sub_mi_y = mi_y + h * (1 << pd->subsampling_y);
        const int comp_bw = tip_ref_frame ? (refinemv_sb_size_width >> ss_x)
                                          : refinemv_sb_size_width;
        const int comp_bh = tip_ref_frame ? (refinemv_sb_size_height >> ss_y)
                                          : refinemv_sb_size_height;
        av1_get_reference_area_with_padding(cm, xd, plane, mi, mi_mv, comp_bw,
                                            comp_bh, sub_mi_x, sub_mi_y,
                                            ref_area, pu_width, pu_height);
#endif  // CONFIG_SUBBLK_PAD
        // mi_x, and mi_y are the top-left position of the luma samples of the
        // sub-block
        build_inter_predictors_8x8_and_bigger_refinemv(
            cm, xd, plane, mi, build_for_decode, refinemv_sb_size_width,
            refinemv_sb_size_height, mi_x + w * (1 << pd->subsampling_x),
            mi_y + h * (1 << pd->subsampling_y), mc_buf, mi_mv,
            calc_subpel_params_func, dst_buf, dst_stride, w, h, pu_width,
            pu_height, dst0_16_refinemv, dst1_16_refinemv, row_start, col_start,
            plane == 0 ? luma_refined_mv : NULL, chroma_refined_mv,
            build_for_refine_mv_only, ref_area, mv_refined, opfl_vxy_bufs);

        if (plane == 0) {
          REFINEMV_SUBMB_INFO
          *refinemv_subinfo =
              &block_refinemv_subinfo[(h >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
                                      (w >> MI_SIZE_LOG2)];
          fill_subblock_refine_mv(refinemv_subinfo, refinemv_sb_size_width,
                                  refinemv_sb_size_height, luma_refined_mv[0],
                                  luma_refined_mv[1]);
        }
      }
    }
    xd->tmp_conv_dst = tmp_conv_dst;
    return;
  }

  int is_global[2] = { 0, 0 };
  if (!tip_ref_frame) {
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
      const WarpedMotionParams *const wm =
          &xd->global_motion[mi->ref_frame[ref]];
      is_global[ref] = is_global_mv_block(mi, wm->wmtype);
    }
  }

  int row_start = 0;
  int col_start = 0;
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  row_start = plane ? (mi->chroma_ref_info.mi_row_chroma_base - mi_row) : 0;
  col_start = plane ? (mi->chroma_ref_info.mi_col_chroma_base - mi_col) : 0;
  const int pre_x = (mi_x + MI_SIZE * col_start) >> pd->subsampling_x;
  const int pre_y = (mi_y + MI_SIZE * row_start) >> pd->subsampling_y;
  MV best_mv_ref[2] = { mi_mv[0], mi_mv[1] };
  if (tip_ref_frame && plane == 0) {
    mv_refined[0].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[0]);
    mv_refined[1].as_mv = convert_mv_to_1_16th_pel(&best_mv_ref[1]);
  }
  int use_optflow_refinement =
      is_optflow_refinement_enabled(cm, xd, mi, plane, tip_ref_frame);
  int use_4x4 = tip_ref_frame ? 0 : 1;
  assert(IMPLIES(use_optflow_refinement,
                 cm->features.opfl_refine_type != REFINE_NONE));

  // Optical flow refinement with masked comp
  // types or with non-sharp interpolation
  // filter should only exist in REFINE_ALL.
  assert(IMPLIES(
      use_optflow_refinement && mi->interinter_comp.type != COMPOUND_AVERAGE,
      cm->features.opfl_refine_type == REFINE_ALL));
  assert(IMPLIES(use_optflow_refinement && tip_ref_frame, plane == 0));
  // In REFINE_ALL mode, refinement should be
  // used whenever applicable
  assert(IMPLIES(cm->features.opfl_refine_type == REFINE_ALL &&
                     !tip_ref_frame && opfl_allowed_cur_pred_mode(cm, xd, mi),
                 use_optflow_refinement));

  assert(IMPLIES(
      use_optflow_refinement,
      !is_thin_4xn_nx4_block(mi->sb_type[xd->tree_type == CHROMA_PART])));

  // Pointers to gradient and dst buffers

  if (use_optflow_refinement && plane == 0) {
    // Pointers to hold optical flow MV
    // offsets.
    int *vx0 = opfl_vxy_bufs;
    int *vx1 = opfl_vxy_bufs + (N_OF_OFFSETS * 1);
    int *vy0 = opfl_vxy_bufs + (N_OF_OFFSETS * 2);
    int *vy1 = opfl_vxy_bufs + (N_OF_OFFSETS * 3);

    // Allocate gradient and dst buffers
    const int n = opfl_get_subblock_size(bw, bh, plane, use_4x4);
    const int n_blocks = (bw / n) * (bh / n);
    int16_t *gx0 = xd->opfl_gxy_bufs;
    int16_t *gx1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 1);
    int16_t *gy0 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 2);
    int16_t *gy1 = xd->opfl_gxy_bufs + (MAX_SB_SQUARE * 3);

    // Initialize refined mv
    const MV mv0 = best_mv_ref[0];
    const MV mv1 = best_mv_ref[1];
    // Refine MV using optical flow. The final
    // output MV will be in 1/16 precision.
    uint16_t *dst0 = xd->opfl_dst_bufs;
    uint16_t *dst1 = xd->opfl_dst_bufs + MAX_SB_SQUARE;

    if (tip_ref_frame) {
      use_optflow_refinement = !skip_opfl_refine_with_tip(
          cm, xd, plane, bw, bh, pu_width, pu_height, mi_x, mi_y, mc_buf,
          best_mv_ref, calc_subpel_params_func, dst0, dst1, bw,
          /*do_pred=*/1);
    }
    if (use_optflow_refinement) {
      int do_pred = tip_ref_frame ? 0 : 1;
      for (int mvi = 0; mvi < n_blocks; mvi++) {
        mv_refined[mvi * 2].as_mv = mv0;
        mv_refined[mvi * 2 + 1].as_mv = mv1;
      }
      av1_get_optflow_based_mv(cm, xd, plane, mi, mv_refined, bw, bh, mi_x,
                               mi_y, build_for_decode, mc_buf,
                               calc_subpel_params_func, gx0, gy0, gx1, gy1, vx0,
                               vy0, vx1, vy1, dst0, dst1, bw, do_pred, use_4x4,
                               best_mv_ref, pu_width, pu_height);
    }
  }

  int opfl_sub_bw = OF_BSIZE;
  int opfl_sub_bh = OF_BSIZE;
  opfl_subblock_size_plane(xd, plane, use_4x4, &opfl_sub_bw, &opfl_sub_bh);

  BacpBlockData bacp_block_data[2 * N_OF_OFFSETS];
  uint8_t use_bacp = tip_ref_frame
                         ?
#if CONFIG_TIP_ENHANCEMENT
                         is_compound && tip_weight == TIP_EQUAL_WTD &&
#endif  // CONFIG_TIP_ENHANCEMENT
                             cm->features.enable_imp_msk_bld &&
                             !av1_is_scaled(cm->tip_ref.ref_scale_factor[0]) &&
                             !av1_is_scaled(cm->tip_ref.ref_scale_factor[1])
                         : use_border_aware_compound(cm, xd, mi) &&
                               mi->cwp_idx == CWP_EQUAL &&
                               cm->features.enable_imp_msk_bld;

  WarpBoundaryBox warp_bd_box_mem[MAX_WARP_BD_SQ];
  assert(IMPLIES(singleref_for_compound, !is_compound));
  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const struct scale_factors *const sf =
        tip_ref_frame ? cm->tip_ref.ref_scale_factor[ref]
                      : (is_intrabc ? &cm->sf_identity
                                    : xd->block_ref_scale_factors[ref]);
    struct buf_2d *const pre_buf = is_intrabc ? &pd->dst : &pd->pre[ref];
    MV mv = mi_mv[ref];
    const WarpTypesAllowed warp_types = { is_global[ref],
                                          is_warp_mode(mi->motion_mode) };

    InterPredParams inter_pred_params;
    const int comp_bw = tip_ref_frame ? (bw >> ss_x) : bw;
    const int comp_bh = tip_ref_frame ? (bh >> ss_y) : bh;
    av1_init_inter_params(&inter_pred_params, comp_bw, comp_bh, pre_y, pre_x,
                          pd->subsampling_x, pd->subsampling_y, xd->bd,
                          mi->use_intrabc[0], sf, pre_buf, mi->interp_fltr);
    inter_pred_params.original_pu_width = pu_width;
    inter_pred_params.original_pu_height = pu_height;

    if (is_compound) av1_init_comp_mode(&inter_pred_params);
    inter_pred_params.border_data.enable_bacp = use_bacp;
    inter_pred_params.border_data.bacp_block_data =
        &bacp_block_data[0];  // Always point
                              // to the first
                              // ref

    inter_pred_params.conv_params = get_conv_params_no_round(
        ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

    av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);
#if CONFIG_ACROSS_SCALE_WARP
    assert(IMPLIES(inter_pred_params.mode == WARP_PRED &&
                       av1_is_scaled(inter_pred_params.scale_factors),
                   !inter_pred_params.warp_params.use_affine_filter));
#endif  // CONFIG_ACROSS_SCALE_WARP
    if (inter_pred_params.mode == WARP_PRED &&
        (!inter_pred_params.warp_params.use_affine_filter ||
#if CONFIG_ACROSS_SCALE_WARP
         av1_is_scaled(inter_pred_params.scale_factors) ||
#endif  // CONFIG_ACROSS_SCALE_WARP
         (comp_bw < 8 || comp_bh < 8))) {
      *ext_warp_used = true;
      inter_pred_params.use_warp_bd_box = 1;
      inter_pred_params.warp_bd_box = &warp_bd_box_mem[0];
      const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
      const int_mv warp_mv = get_int_warp_mv_for_fb(
          xd, &inter_pred_params.warp_params, bsize, (mi_x >> MI_SIZE_LOG2),
          (mi_y >> MI_SIZE_LOG2));
      // printf("warpmv (%d, %d), loc (%d,
      // %d)\n", warp_mv.as_mv.col,
      //        warp_mv.as_mv.row, mi_x,
      //        mi_y);
      // printf("precision %d\n",
      // mi->pb_mv_precision);

      int warp_bd_box_mem_stride = MAX_WARP_BD_SIZE;
      for (int sub_mi_y = pre_y; sub_mi_y < pre_y + pu_height; sub_mi_y += 4) {
        for (int sub_mi_x = pre_x; sub_mi_x < pre_x + pu_width; sub_mi_x += 4) {
          int x_loc = sub_mi_x - pre_x;
          int y_loc = sub_mi_y - pre_y;
          int block_width = AOMMIN(8, comp_bw);
          int block_height = AOMMIN(8, comp_bh);
          if ((x_loc & 7) == 0 && (y_loc & 7) == 0) {
            av1_get_reference_area_with_padding_single_warp(
                cm, xd, plane, mi, warp_mv.as_mv, block_width, block_height,
                (sub_mi_x << pd->subsampling_x),
                (sub_mi_y << pd->subsampling_y),
                &inter_pred_params
                     .warp_bd_box[(x_loc >> 3) +
                                  (y_loc >> 3) * warp_bd_box_mem_stride],
                pu_width, pu_height, ref);
          } else {
            continue;
          }
        }
      }
    }

    if (is_compound) {
      inter_pred_params.sb_type = tip_ref_frame ?
#if CONFIG_FLEX_TIP_BLK_SIZE
                                                get_tip_bsize_from_bw_bh(bw, bh)
#else
                                                BLOCK_8X8
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
                                                : mi->sb_type[PLANE_TYPE_Y];
      inter_pred_params.mask_comp = mi->interinter_comp;
    }

    if (is_masked_compound_type(mi->interinter_comp.type)) {
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

    if (use_optflow_refinement && plane == 0) {
      inter_pred_params.interp_filter_params[0] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr,
                                                       opfl_sub_bw);
      inter_pred_params.interp_filter_params[1] =
          av1_get_interp_filter_params_with_block_size(mi->interp_fltr,
                                                       opfl_sub_bh);
      av1_opfl_rebuild_inter_predictor(
          dst, dst_stride, plane, mv_refined, &inter_pred_params, xd, mi_x,
          mi_y, build_for_decode, cm, pu_width, ref, mc_buf,
          calc_subpel_params_func, use_4x4);
      continue;
    }
    if (mi->bawp_flag[0] > 0 && (plane == 0 || mi->bawp_flag[1])) {
      av1_build_one_bawp_inter_predictor(
          dst, dst_stride, &mv, &inter_pred_params, cm, xd, dst_orig, bw, bh,
          mi_x, mi_y, ref, plane, mc_buf, calc_subpel_params_func);
      continue;
    }

#if CONFIG_TIP_ENHANCEMENT
    if (tip_ref_frame) {
      set_tip_interp_weight_factor(cm, ref, &inter_pred_params);
    }
#endif  // CONFIG_TIP_ENHANCEMENT

    const MV mv_1_16th_pel = (tip_ref_frame && plane)
                                 ? mv_refined[ref].as_mv
                                 : convert_mv_to_1_16th_pel(&mv);
    av1_build_one_inter_predictor(dst, dst_stride, &mv_1_16th_pel,
                                  &inter_pred_params, xd, mi_x, mi_y, ref,
                                  mc_buf, calc_subpel_params_func);
  }
}

// This function consolidates the prediction
// process of the TIP ref mode block and the
// non-TIP ref mode block.
static void build_inter_predictors_8x8_and_bigger_facade(
    const AV1_COMMON *cm, MACROBLOCKD *xd, int plane, MB_MODE_INFO *mi,
    const BUFFER_SET *dst_orig, int build_for_decode, int bw, int bh, int mi_x,
    int mi_y, uint16_t **mc_buf, CalcSubpelParamsFunc calc_subpel_params_func,
    int build_for_refine_mv_only) {
  const int tip_ref_frame = is_tip_ref_frame(mi->ref_frame[0]);
  bool ext_warp_used = false;

  struct macroblockd_plane *pd = &xd->plane[plane];
  struct buf_2d *dst_buf = &pd->dst;
  const int dst_stride = dst_buf->stride;
  uint16_t *const dst = dst_buf->buf;

  if (tip_ref_frame) {
#if CONFIG_FLEX_TIP_BLK_SIZE
    const int width = xd->width << MI_SIZE_LOG2;
    const int height = xd->height << MI_SIZE_LOG2;
    const BLOCK_SIZE unit_bsize =
        get_unit_bsize_for_tip_ref(TIP_FRAME_AS_REF, width, height);
    const int unit_blk_size = block_size_wide[unit_bsize];
    const int end_pixel_row = mi_y + height;
    const int end_pixel_col = mi_x + width;
#else
    // TMVP_MI_SIZE_UV is the block size in
    // luma unit for Chroma TIP interpolation,
    // will convert to the step size in TMVP
    // 8x8 unit
    const int unit_blk_size = (plane == 0) ? TMVP_MI_SIZE : TMVP_MI_SIZE_UV;
    const int end_pixel_row = mi_y + (xd->height << MI_SIZE_LOG2);
    const int end_pixel_col = mi_x + (xd->width << MI_SIZE_LOG2);
#endif  // CONFIG_FLEX_TIP_BLK_SIZE

    for (int pixel_row = mi_y; pixel_row < end_pixel_row;
         pixel_row += unit_blk_size) {
      for (int pixel_col = mi_x; pixel_col < end_pixel_col;
           pixel_col += unit_blk_size) {
        const int tpl_row = pixel_row >> TMVP_MI_SZ_LOG2;
        const int tpl_col = pixel_col >> TMVP_MI_SZ_LOG2;
        const int row_offset = (pixel_row - mi_y) >> TMVP_MI_SZ_LOG2;
        const int col_offset = (pixel_col - mi_x) >> TMVP_MI_SZ_LOG2;
        const int tip_mv_offset = (row_offset * TIP_MV_STRIDE + col_offset)
                                  << 1;
        const int refinemv_offset =
            ((pixel_row - mi_y) >> MI_SIZE_LOG2) * MAX_MIB_SIZE +
            ((pixel_col - mi_x) >> MI_SIZE_LOG2);
        const int opfl_vxy_offset =
            ((pixel_row - mi_y) >> OF_BSIZE_LOG2) *
                (xd->width >> (OF_BSIZE_LOG2 - MI_SIZE_LOG2)) +
            ((pixel_col - mi_x) >> OF_BSIZE_LOG2);
        const int ss_x = pd->subsampling_x;
        const int ss_y = pd->subsampling_y;
        MV tip_mv[2];
        int_mv tip_mv_tmp[2];

        if (is_subblock_outside(pixel_col, pixel_row, cm->mi_params.mi_cols,
                                cm->mi_params.mi_rows, build_for_decode)) {
          continue;
        }

        get_tip_mv(cm, &mi->mv[0].as_mv, tpl_col, tpl_row, tip_mv_tmp);

        tip_mv[0] = tip_mv_tmp[0].as_mv;
        tip_mv[1] = tip_mv_tmp[1].as_mv;
        if (plane == 0) {
          REFINEMV_SUBMB_INFO
          *refinemv_subinfo = &xd->refinemv_subinfo[refinemv_offset];
          fill_subblock_refine_mv(refinemv_subinfo, unit_blk_size,
                                  unit_blk_size, tip_mv[0], tip_mv[1]);
          xd->opfl_vxy_bufs[opfl_vxy_offset] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 1 + opfl_vxy_offset] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 2 + opfl_vxy_offset] = 0;
          xd->opfl_vxy_bufs[N_OF_OFFSETS * 3 + opfl_vxy_offset] = 0;
        }
        dst_buf->buf = dst +
                       ((row_offset << TMVP_MI_SZ_LOG2) >> ss_y) * dst_stride +
                       ((col_offset << TMVP_MI_SZ_LOG2) >> ss_x);

        build_inter_predictors_8x8_and_bigger(
            cm, xd, plane, mi, dst_orig, build_for_decode, unit_blk_size,
            unit_blk_size, pixel_col, pixel_row, mc_buf, tip_mv,
            calc_subpel_params_func, dst_buf->buf, dst_stride, bw, bh,
            build_for_refine_mv_only, &ext_warp_used,
            &xd->mv_refined[tip_mv_offset],
            &xd->refinemv_subinfo[refinemv_offset],
            &xd->opfl_vxy_bufs[opfl_vxy_offset]);
      }
    }

    dst_buf->buf = dst;
  } else {
    MV mv[2] = { mi->mv[0].as_mv, mi->mv[1].as_mv };
    build_inter_predictors_8x8_and_bigger(
        cm, xd, plane, mi, dst_orig, build_for_decode, bw, bh, mi_x, mi_y,
        mc_buf, mv, calc_subpel_params_func, dst, dst_stride, bw, bh,
        build_for_refine_mv_only, &ext_warp_used, xd->mv_refined,
        xd->refinemv_subinfo, xd->opfl_vxy_bufs);
  }
}

void av1_build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                int plane, MB_MODE_INFO *mi,
                                const BUFFER_SET *dst_orig,
                                int build_for_refine_mv_only,
                                int build_for_decode, int bw, int bh, int mi_x,
                                int mi_y, uint16_t **mc_buf,
                                CalcSubpelParamsFunc calc_subpel_params_func) {
  if (plane == AOM_PLANE_Y)
    memset(xd->mv_refined, 0, 2 * N_OF_OFFSETS * sizeof(int_mv));
  // just for debugging purpose
  // Can be removed later on
  if (mi->mode == WARPMV) {
    assert(mi->ref_mv_idx[0] == 0);
    assert(mi->ref_mv_idx[1] == 0);
    assert(mi->motion_mode == WARP_DELTA || mi->motion_mode == WARP_CAUSAL);
  }

  if (is_sub8x8_inter(cm, xd, mi, plane, is_intrabc_block(mi, xd->tree_type))) {
    build_inter_predictors_sub8x8(cm, xd, plane, mi, mi_x, mi_y, mc_buf,
                                  calc_subpel_params_func);
  } else {
    build_inter_predictors_8x8_and_bigger_facade(
        cm, xd, plane, mi, dst_orig, build_for_decode, bw, bh, mi_x, mi_y,
        mc_buf, calc_subpel_params_func, build_for_refine_mv_only);
  }
}

void av1_setup_dst_planes(struct macroblockd_plane *planes,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const int plane_start, const int plane_end,
                          const CHROMA_REF_INFO *chroma_ref_info) {
  // We use AOMMIN(num_planes, MAX_MB_PLANE)
  // instead of num_planes to quiet the static
  // analysis warnings.
  for (int i = plane_start; i < AOMMIN(plane_end, MAX_MB_PLANE); ++i) {
    struct macroblockd_plane *const pd = &planes[i];
    const int is_uv = i > 0;
#if CONFIG_F054_PIC_BOUNDARY
    setup_pred_plane(&pd->dst, src->buffers[i], src->widths[is_uv],
                     src->heights[is_uv], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                     mi_col, NULL, pd->subsampling_x, pd->subsampling_y,
                     chroma_ref_info);
#else
    setup_pred_plane(&pd->dst, src->buffers[i], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->crop_widths[is_uv],
                     src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                     mi_col, NULL, pd->subsampling_x, pd->subsampling_y,
                     chroma_ref_info);
#endif  // CONFIG_F054_PIC_BOUNDARY
  }
}

void av1_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf, const int num_planes,
                          const CHROMA_REF_INFO *chroma_ref_info) {
  if (src != NULL) {
    // We use AOMMIN(num_planes, MAX_MB_PLANE)
    // instead of num_planes to quiet the
    // static analysis warnings.
    for (int i = 0; i < AOMMIN(num_planes, MAX_MB_PLANE); ++i) {
      struct macroblockd_plane *const pd = &xd->plane[i];
      const int is_uv = i > 0;
#if CONFIG_F054_PIC_BOUNDARY
      setup_pred_plane(&pd->pre[idx], src->buffers[i], src->widths[is_uv],
                       src->heights[is_uv], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                       mi_col, sf, pd->subsampling_x, pd->subsampling_y,
                       chroma_ref_info);
#else
      setup_pred_plane(&pd->pre[idx], src->buffers[i], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->crop_widths[is_uv],
                       src->crop_heights[is_uv], src->strides[is_uv], mi_row,
                       mi_col, sf, pd->subsampling_x, pd->subsampling_y,
                       chroma_ref_info);
#endif  // CONFIG_F054_PIC_BOUNDARY
    }
  }
}

static AOM_INLINE void combine_interintra_highbd(
    INTERINTRA_MODE mode, int8_t use_wedge_interintra, int8_t wedge_index,
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    int8_t boundary_index,
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
    int8_t wedge_sign, BLOCK_SIZE bsize, BLOCK_SIZE plane_bsize,
    uint16_t *comppred8, int compstride, const uint16_t *interpred8,
    int interstride, const uint16_t *intrapred8, int intrastride, int bd) {
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];

  if (use_wedge_interintra) {
    if (av1_is_wedge_used(bsize)) {
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
      const uint8_t *mask = av1_get_all_contiguous_soft_mask(
          wedge_index, wedge_sign, bsize, boundary_index);
#else
      const uint8_t *mask =
          av1_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY

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

void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd, int plane,
                                               const BUFFER_SET *ctx,
                                               uint16_t *dst, int dst_stride) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ssx = xd->plane[plane].subsampling_x;
  const int ssy = xd->plane[plane].subsampling_y;
  BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], plane, ssx, ssy);
  PREDICTION_MODE mode = interintra_to_intra_mode[xd->mi[0]->interintra_mode];
  assert(xd->mi[0]->angle_delta[PLANE_TYPE_Y] == 0);
  assert(xd->mi[0]->angle_delta[PLANE_TYPE_UV] == 0);
  assert(xd->mi[0]->use_intrabc[PLANE_TYPE_Y] == 0);
  xd->mi[0]->txb_idx = 0;
  av1_predict_intra_block(cm, xd, pd->width, pd->height,
                          max_txsize_rect_lookup[plane_bsize], mode, 0, 0,
                          ctx->plane[plane], ctx->stride[plane], dst,
                          dst_stride, 0, 0, plane);
}

void av1_combine_interintra(MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane,
                            const uint16_t *inter_pred, int inter_stride,
                            const uint16_t *intra_pred, int intra_stride) {
  const int ssx = xd->plane[plane].subsampling_x;
  const int ssy = xd->plane[plane].subsampling_y;
  BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], plane, ssx, ssy);

  combine_interintra_highbd(
      xd->mi[0]->interintra_mode, xd->mi[0]->use_wedge_interintra,
      xd->mi[0]->interintra_wedge_index,
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
      xd->mi[0]->wedge_boundary_index,
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
      INTERINTRA_WEDGE_SIGN, bsize, plane_bsize, xd->plane[plane].dst.buf,
      xd->plane[plane].dst.stride, inter_pred, inter_stride, intra_pred,
      intra_stride, xd->bd);
}

// build interintra_predictors for one plane
void av1_build_interintra_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t *pred, int stride,
                                    const BUFFER_SET *ctx, int plane,
                                    BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  DECLARE_ALIGNED(16, uint16_t, intrapredictor[MAX_SB_SQUARE]);
  av1_build_intra_predictors_for_interintra(cm, xd, plane, ctx, intrapredictor,
                                            MAX_SB_SIZE);
  av1_combine_interintra(xd, bsize, plane, pred, stride, intrapredictor,
                         MAX_SB_SIZE);
}

int av1_get_mpp_flag_context(const AV1_COMMON *cm, const MACROBLOCKD *xd) {
  (void)cm;
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor && is_inter_block(neighbor, SHARED_PART) &&
        !is_intrabc_block(neighbor, SHARED_PART)) {
      ctx += (neighbor->most_probable_pb_mv_precision ==
              neighbor->pb_mv_precision);
    }
  }

  return ctx;
#else
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
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
}

// Derive the context index for refinemv flag
int av1_get_refinemv_context(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             BLOCK_SIZE bsize) {
  (void)cm;
  (void)bsize;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->skip_mode) return 0;
  return (1 + (mbmi->mode - NEAR_NEARMV));
}

int av1_get_pb_mv_precision_down_context(const AV1_COMMON *cm,
                                         const MACROBLOCKD *xd) {
  (void)cm;
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor && is_inter_block(neighbor, SHARED_PART) &&
        !is_intrabc_block(neighbor, SHARED_PART)) {
      ctx += (neighbor->max_mv_precision - neighbor->pb_mv_precision);
    }
  }

  return ctx > 0;
#else
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
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
}

int av1_get_mv_class_context(const MvSubpelPrecision pb_mv_precision) {
  return pb_mv_precision;
}

void set_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision) {
  mbmi->pb_mv_precision = precision;
}

// Function to check if precision need to be
// signaled or not
int is_intraBC_bv_precision_active(const AV1_COMMON *const cm,
                                   const int intrabc_mode) {
  assert(IMPLIES(!cm->features.allow_screen_content_tools,
                 !cm->features.cur_frame_force_integer_mv));
  assert(IMPLIES(cm->features.cur_frame_force_integer_mv,
                 cm->features.allow_screen_content_tools));
  return (!cm->features.cur_frame_force_integer_mv && intrabc_mode == 0);
}
// Set max value as default precision
void set_default_intraBC_bv_precision(const AV1_COMMON *const cm,
                                      MB_MODE_INFO *mbmi) {
  assert(IMPLIES(!cm->features.allow_screen_content_tools,
                 !cm->features.cur_frame_force_integer_mv));
  assert(IMPLIES(cm->features.cur_frame_force_integer_mv,
                 cm->features.allow_screen_content_tools));
  mbmi->pb_mv_precision =
      cm->features.cur_frame_force_integer_mv
          ? MV_PRECISION_ONE_PEL
          : av1_intraBc_precision_sets
                .precision[av1_intraBc_precision_sets.num_precisions - 1];
}

// set the mv precision for amvd applied mode
void set_amvd_mv_precision(MB_MODE_INFO *mbmi, MvSubpelPrecision precision) {
  mbmi->pb_mv_precision =
      precision <= MV_PRECISION_QTR_PEL ? precision : MV_PRECISION_QTR_PEL;
}
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
                       int *ref_mv_idx) {
  (void)bsize;
  (void)cm;
  (void)xd;
  (void)ref_mv_idx;
#if CONFIG_FRAME_HALF_PRECISION
  mbmi->mb_precision_set =
      (mbmi->max_mv_precision < MV_PRECISION_HALF_PEL)
          ? 0
          : MV_PRECISION_ONE_EIGHTH_PEL - mbmi->max_mv_precision;
#else
  int set_idx = 0;

  int offset_idx = (mbmi->max_mv_precision == MV_PRECISION_QTR_PEL)
                       ? NUMBER_OF_PRECISION_SETS
                       : 0;
  mbmi->mb_precision_set = set_idx + offset_idx;
#endif  // CONFIG_FRAME_HALF_PRECISION
}
void set_default_precision_set(const AV1_COMMON *const cm, MB_MODE_INFO *mbmi,
                               const BLOCK_SIZE bsize) {
  (void)bsize;
  (void)cm;

#if CONFIG_FRAME_HALF_PRECISION
  mbmi->mb_precision_set =
      (mbmi->max_mv_precision < MV_PRECISION_HALF_PEL)
          ? 0
          : MV_PRECISION_ONE_EIGHTH_PEL - mbmi->max_mv_precision;
#else
  int set_idx = 0;
  int offset_idx = (mbmi->max_mv_precision == MV_PRECISION_QTR_PEL)
                       ? NUMBER_OF_PRECISION_SETS
                       : 0;
  mbmi->mb_precision_set = set_idx + offset_idx;
#endif  // CONFIG_FRAME_HALF_PRECISION
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

// Copy mv0 and mv1 to the sub-blocks
// submi is the top-left corner of the
// sub-block need to fill bw is the block
// width in the unit of pixel bh is the block
// height in unit of pixel
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

bool av1_build_morph_pred(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                          const BLOCK_SIZE bsize, const int mi_row,
                          const int mi_col) {
#if CONFIG_F054_PIC_BOUNDARY
  (void)cm;
#endif  // CONFIG_F054_PIC_BOUNDARY
        // Predictor, i.e., the reconstructed block
        // found from intrabc.
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  uint16_t *const dst = pd->dst.buf;
  const int dst_stride = pd->dst.stride;
  MB_MODE_INFO *mbmi = xd->mi[0];
  FULLPEL_MV dv = get_fullmv_from_mv(&mbmi->mv[0].as_mv);
  const int cur_x = mi_col * MI_SIZE;
  const int cur_y = mi_row * MI_SIZE;
#if CONFIG_F054_PIC_BOUNDARY
  if (cur_x >= pd->dst.width || cur_y >= pd->dst.height) return false;
#else
  if (cur_x >= cm->width || cur_y >= cm->height) return false;
#endif  // CONFIG_F054_PIC_BOUNDARY

  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  int ref_w = bw;
  int ref_h = bh;
#if CONFIG_F054_PIC_BOUNDARY
  if (cur_x + bw >= pd->dst.width) ref_w = pd->dst.width - cur_x;
  if (cur_y + bh >= pd->dst.height) ref_h = pd->dst.height - cur_y;
#else
  if (cur_x + bw >= cm->width) ref_w = cm->width - cur_x;
  if (cur_y + bh >= cm->height) ref_h = cm->height - cur_y;
#endif  // CONFIG_F054_PIC_BOUNDARY

  const int cur_tmplt_x = cur_x - BAWP_REF_LINES;
  const int cur_tmplt_y = cur_y - BAWP_REF_LINES;
  const int ref_x = cur_x + dv.col;
  const int ref_y = cur_y + dv.row;
  const int ref_tmplt_x = ref_x - BAWP_REF_LINES;
  const int ref_tmplt_y = ref_y - BAWP_REF_LINES;
#if CONFIG_F054_PIC_BOUNDARY
  assert(cur_tmplt_x + ref_w < pd->dst.width);
  assert(cur_tmplt_y + ref_h < pd->dst.height);
  if (ref_tmplt_x < 0 || ref_tmplt_y < 0 || ref_x + ref_w >= pd->dst.width ||
      ref_y + ref_h >= pd->dst.height) {
#else
  assert(cur_tmplt_x + ref_w < cm->width);
  assert(cur_tmplt_y + ref_h < cm->height);
  if (ref_tmplt_x < 0 || ref_tmplt_y < 0 || ref_x + ref_w >= cm->width ||
      ref_y + ref_h >= cm->height) {
#endif  // CONFIG_F054_PIC_BOUNDARY
    return false;
  }
#if !CONFIG_LOCAL_INTRABC_BAWP
  // Restriction: the reference block's
  // template can't be outside the local 64x64
  // block for local intra block copy. If
  // local intra block copy extends to
  // 128x128, one has to change the
  // restrictions here to make it match.
  const int is_same_unit_x = (cur_x >> 6) == (ref_x >> 6);
  const int is_same_unit_y = (cur_y >> 6) == (ref_y >> 6);
  if (is_same_unit_x && is_same_unit_y) {
    if (ref_x > 0 && (ref_x % 64 == 0)) return false;
    if (ref_y > 0 && (ref_y % 64 == 0)) return false;
  }
#endif  // !CONFIG_LOCAL_INTRABC_BAWP
        // Restriction: the reference block's
        // template can't be outside the current
        // tile.
  const TileInfo *const tile = &xd->tile;
  // Is the source top-left inside the current
  // tile?
  const int tile_top_edge = tile->mi_row_start * MI_SIZE;
  if (ref_tmplt_y < tile_top_edge) return false;
  const int tile_left_edge = tile->mi_col_start * MI_SIZE;
  if (ref_tmplt_x < tile_left_edge) return false;
  // Is the bottom right inside the current
  // tile?
  const int ref_bottom_edge = ref_y + bh;
  const int tile_bottom_edge = tile->mi_row_end * MI_SIZE;
  if (ref_bottom_edge > tile_bottom_edge) return false;
  const int ref_right_edge = ref_x + bw;
  const int tile_right_edge = tile->mi_col_end * MI_SIZE;
  if (ref_right_edge > tile_right_edge) return false;
  // The current block's template can't be
  // outside the current tile too.
  if (cur_tmplt_y < tile_top_edge) return false;
  if (cur_tmplt_x < tile_left_edge) return false;

  uint16_t *recon_buf = xd->plane[0].dst.buf;
  uint16_t *recon_top = dst - BAWP_REF_LINES * dst_stride;
  uint16_t *recon_left = dst - BAWP_REF_LINES;
  uint16_t *ref_buf = recon_buf + dv.row * dst_stride + dv.col;
  uint16_t *ref_top = ref_buf - BAWP_REF_LINES * dst_stride;
  uint16_t *ref_left = ref_buf - BAWP_REF_LINES;
  derive_bawp_parameters(xd, recon_top, recon_left, dst_stride, ref_top,
                         ref_left, dst_stride, /*ref=*/0, /*plane=*/0, ref_w,
                         ref_h);
  int16_t alpha = mbmi->bawp_alpha[0][0];
  int32_t beta = mbmi->bawp_beta[0][0];
  const int shift = 8;
  for (int j = 0; j < bh; ++j) {
    for (int i = 0; i < bw; ++i) {
      dst[j * dst_stride + i] = clip_pixel_highbd(
          (dst[j * dst_stride + i] * alpha + beta) >> shift, xd->bd);
    }
  }
  return true;
}
