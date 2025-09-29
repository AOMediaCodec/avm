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

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <assert.h>

#include "config/aom_dsp_rtcd.h"

#include "av1/encoder/global_motion.h"

#include "av1/common/convolve.h"
#include "av1/common/warped_motion.h"

#include "av1/encoder/segmentation.h"

// Border over which to compute the global motion
#define ERRORADV_BORDER 0

int av1_is_enough_erroradvantage(double best_erroradvantage, int params_cost) {
  return best_erroradvantage < erroradv_tr &&
         best_erroradvantage * params_cost < erroradv_prod_tr;
}

static void convert_to_params(const double *params, int32_t *model) {
  int i;
  model[0] = (int32_t)floor(params[0] * (1 << GM_TRANS_PREC_BITS) + 0.5);
  model[1] = (int32_t)floor(params[1] * (1 << GM_TRANS_PREC_BITS) + 0.5);
  model[0] = (int32_t)clamp(model[0], GM_TRANS_MIN, GM_TRANS_MAX) *
             GM_TRANS_DECODE_FACTOR;
  model[1] = (int32_t)clamp(model[1], GM_TRANS_MIN, GM_TRANS_MAX) *
             GM_TRANS_DECODE_FACTOR;

  for (i = 2; i < 6; ++i) {
    const int diag_value = ((i == 2 || i == 5) ? (1 << GM_ALPHA_PREC_BITS) : 0);
    model[i] = (int32_t)floor(params[i] * (1 << GM_ALPHA_PREC_BITS) + 0.5);
    model[i] =
        (int32_t)clamp(model[i] - diag_value, GM_ALPHA_MIN, GM_ALPHA_MAX);
    model[i] = (model[i] + diag_value) * GM_ALPHA_DECODE_FACTOR;
  }
}

void av1_convert_model_to_params(const double *params,
                                 WarpedMotionParams *model) {
  convert_to_params(params, model->wmmat);
  model->wmtype = get_wmtype(model);
  model->invalid = 0;
}

// Adds some offset to a global motion parameter and handles
// all of the necessary precision shifts, clamping, and
// zero-centering.
static int32_t add_param_offset(int param_index, int32_t param_value,
                                int32_t offset) {
  const int scale_vals[2] = { GM_TRANS_PREC_DIFF, GM_ALPHA_PREC_DIFF };
  const int clamp_vals[2] = { GM_TRANS_MAX, GM_ALPHA_MAX };
  // type of param: 0 - translation, 1 - affine
  const int param_type = (param_index < 2 ? 0 : 1);
  const int is_one_centered = (param_index == 2 || param_index == 5);

  // Make parameter zero-centered and offset the shift that was done to make
  // it compatible with the warped model
  param_value = (param_value - (is_one_centered << WARPEDMODEL_PREC_BITS)) >>
                scale_vals[param_type];
  // Add desired offset to the rescaled/zero-centered parameter
  param_value += offset;
  // Clamp the parameter so it does not overflow the number of bits allotted
  // to it in the bitstream
  param_value = (int32_t)clamp(param_value, -clamp_vals[param_type],
                               clamp_vals[param_type]);
  // Rescale the parameter to WARPEDMODEL_PRECISION_BITS so it is compatible
  // with the warped motion library
  param_value *= (1 << scale_vals[param_type]);

  // Undo the zero-centering step if necessary
  return param_value + (is_one_centered << WARPEDMODEL_PREC_BITS);
}

static void force_wmtype(WarpedMotionParams *wm, TransformationType wmtype) {
  switch (wmtype) {
    case IDENTITY:
      wm->wmmat[0] = 0;
      wm->wmmat[1] = 0;
      AOM_FALLTHROUGH_INTENDED;
    case TRANSLATION:
      wm->wmmat[2] = 1 << WARPEDMODEL_PREC_BITS;
      wm->wmmat[3] = 0;
      AOM_FALLTHROUGH_INTENDED;
    case ROTZOOM:
      wm->wmmat[4] = -wm->wmmat[3];
      wm->wmmat[5] = wm->wmmat[2];
      AOM_FALLTHROUGH_INTENDED;
    case AFFINE: wm->wmmat[6] = wm->wmmat[7] = 0; break;
    default: assert(0);
  }
  wm->wmtype = wmtype;
}

static INLINE int generic_sad_highbd(const uint16_t *const ref, int ref_stride,
                                     const uint16_t *const dst, int dst_stride,
                                     int p_width, int p_height) {
  // This function should only be called for patches smaller than
  // WARP_ERROR_BLOCK x WARP_ERROR_BLOCK. This keeps the number of pixels
  // small enough that we don't need a 64-bit accumulator
  assert(p_width <= WARP_ERROR_BLOCK && p_height <= WARP_ERROR_BLOCK);

  int sad = 0;
  for (int i = 0; i < p_height; ++i) {
    for (int j = 0; j < p_width; ++j) {
      sad += abs(dst[j + i * dst_stride] - ref[j + i * ref_stride]);
    }
  }
  return sad;
}

#if WARP_ERROR_BLOCK != 32
#error "Need to change SAD call size in highbd_segmented_frame_error"
#endif  // WARP_ERROR_BLOCK != 32
static int64_t highbd_segmented_frame_error(
    const uint16_t *const ref, int ref_stride, const uint16_t *const dst,
    int dst_stride, int p_width, int p_height, int bd, uint8_t *segment_map,
    int segment_map_stride) {
  (void)bd;
  int patch_w, patch_h;
  const int error_bsize_w = AOMMIN(p_width, WARP_ERROR_BLOCK);
  const int error_bsize_h = AOMMIN(p_height, WARP_ERROR_BLOCK);
  int64_t sum_error = 0;
  for (int i = 0; i < p_height; i += WARP_ERROR_BLOCK) {
    for (int j = 0; j < p_width; j += WARP_ERROR_BLOCK) {
      int seg_x = j >> WARP_ERROR_BLOCK_LOG;
      int seg_y = i >> WARP_ERROR_BLOCK_LOG;
      // Only compute the error if this block contains inliers from the motion
      // model
      if (!segment_map[seg_y * segment_map_stride + seg_x]) continue;

      // avoid computing error into the frame padding
      patch_w = AOMMIN(error_bsize_w, p_width - j);
      patch_h = AOMMIN(error_bsize_h, p_height - i);

      if (patch_w == WARP_ERROR_BLOCK && patch_h == WARP_ERROR_BLOCK) {
        sum_error += aom_highbd_sad32x32(ref + j + i * ref_stride, ref_stride,
                                         dst + j + i * dst_stride, dst_stride);
      } else {
        sum_error += generic_sad_highbd(ref + j + i * ref_stride, ref_stride,
                                        dst + j + i * dst_stride, dst_stride,
                                        patch_w, patch_h);
      }
    }
  }
  return sum_error;
}

#if WARP_ERROR_BLOCK != 32
#error "Need to change SAD call size in highbd_warp_error"
#endif  // WARP_ERROR_BLOCK != 32
static int64_t highbd_warp_error(WarpedMotionParams *wm,
                                 const uint16_t *const ref, int ref_width,
                                 int ref_height, int ref_stride,
                                 const uint16_t *const dst, int dst_stride,
                                 int p_col, int p_row, int p_width,
                                 int p_height, int subsampling_x,
                                 int subsampling_y, int bd, int64_t best_error,
                                 uint8_t *segment_map, int segment_map_stride) {
  int64_t gm_sumerr = 0;
  const int error_bsize_w = AOMMIN(p_width, WARP_ERROR_BLOCK);
  const int error_bsize_h = AOMMIN(p_height, WARP_ERROR_BLOCK);
  DECLARE_ALIGNED(32, uint16_t, tmp[WARP_ERROR_BLOCK * WARP_ERROR_BLOCK]);

  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  for (int i = p_row; i < p_row + p_height; i += WARP_ERROR_BLOCK) {
    for (int j = p_col; j < p_col + p_width; j += WARP_ERROR_BLOCK) {
      int seg_x = j >> WARP_ERROR_BLOCK_LOG;
      int seg_y = i >> WARP_ERROR_BLOCK_LOG;
      // Only compute the error if this block contains inliers from the motion
      // model
      if (!segment_map[seg_y * segment_map_stride + seg_x]) continue;
      // avoid warping extra 8x8 blocks in the padded region of the frame
      // when p_width and p_height are not multiples of WARP_ERROR_BLOCK
      const int warp_w = AOMMIN(error_bsize_w, p_col + ref_width - j);
      const int warp_h = AOMMIN(error_bsize_h, p_row + ref_height - i);
      highbd_warp_plane(wm, ref, ref_width, ref_height, ref_stride, tmp, j, i,
                        warp_w, warp_h, WARP_ERROR_BLOCK, subsampling_x,
                        subsampling_y, bd, &conv_params
#if CONFIG_ACROSS_SCALE_WARP
                        ,
                        NULL
#endif  // CONFIG_ACROSS_SCALE_WARP
                        ,
                        0, NULL);

      if (warp_w == WARP_ERROR_BLOCK && warp_h == WARP_ERROR_BLOCK) {
        gm_sumerr += aom_highbd_sad32x32(tmp, WARP_ERROR_BLOCK,
                                         dst + j + i * dst_stride, dst_stride);
      } else {
        gm_sumerr +=
            generic_sad_highbd(tmp, WARP_ERROR_BLOCK, dst + j + i * dst_stride,
                               dst_stride, warp_w, warp_h);
      }

      if (gm_sumerr > best_error) return INT64_MAX;
    }
  }
  return gm_sumerr;
}

int64_t av1_segmented_frame_error(int bd, const uint16_t *ref, int stride,
                                  uint16_t *dst, int p_width, int p_height,
                                  int p_stride, uint8_t *segment_map,
                                  int segment_map_stride) {
  return highbd_segmented_frame_error(ref, stride, dst, p_width, p_height,
                                      p_stride, bd, segment_map,
                                      segment_map_stride);
}

int64_t av1_warp_error(WarpedMotionParams *wm, int bd, const uint16_t *ref,
                       int width, int height, int stride, uint16_t *dst,
                       int p_col, int p_row, int p_width, int p_height,
                       int p_stride, int subsampling_x, int subsampling_y,
                       int64_t best_error, uint8_t *segment_map,
                       int segment_map_stride) {
  if (wm->wmtype <= AFFINE) {
    av1_reduce_warp_model(wm);
    if (!av1_get_shear_params(wm
#if CONFIG_ACROSS_SCALE_WARP
                              ,
                              NULL
#endif  // CONFIG_ACROSS_SCALE_WARP
                              ))
      return INT64_MAX;
  }

  return highbd_warp_error(wm, ref, width, height, stride, dst, p_col, p_row,
                           p_width, p_height, p_stride, subsampling_x,
                           subsampling_y, bd, best_error, segment_map,
                           segment_map_stride);
}

int64_t av1_refine_integerized_param(
    WarpedMotionParams *wm, TransformationType wmtype, int bd, uint16_t *ref,
    int r_width, int r_height, int r_stride, uint16_t *dst, int d_width,
    int d_height, int d_stride, int n_refinements, int64_t ref_frame_error,
    uint8_t *segment_map, int segment_map_stride
#if CONFIG_ACROSS_SCALE_WARP
    ,
    const struct scale_factors *sf
#endif  // CONFIG_ACROSS_SCALE_WARP
) {
  static const int max_trans_model_params[TRANS_TYPES] = { 0, 2, 4, 6 };
  const int border = ERRORADV_BORDER;
  int i = 0, p;
  int n_params = max_trans_model_params[wmtype];
  int32_t *param_mat = wm->wmmat;
  int64_t step_error, best_error;
  int32_t step;
  int32_t *param;
  int32_t curr_param;
  int32_t best_param;

  force_wmtype(wm, wmtype);
  wm->wmtype = get_wmtype(wm);

  if (n_refinements == 0) {
    // Compute the maximum error value that will be accepted, so that
    // av1_warp_error can terminate early if it proves the model will not
    // be accepted.
    int64_t selection_threshold = (int64_t)lrint(ref_frame_error * erroradv_tr);
    return av1_warp_error(wm, bd, ref, r_width, r_height, r_stride,
                          dst + border * d_stride + border, d_stride, border,
                          border, d_width - 2 * border, d_height - 2 * border,
                          0, 0, selection_threshold, segment_map,
                          segment_map_stride);
  }

  // When refining, use a slightly higher threshold for the initial error
  // calculation - see comment above erroradv_early_tr for why.
  int64_t selection_threshold =
      (int64_t)lrint(ref_frame_error * erroradv_early_tr);
  best_error =
      av1_warp_error(wm, bd, ref, r_width, r_height, r_stride,
                     dst + border * d_stride + border, d_stride, border, border,
                     d_width - 2 * border, d_height - 2 * border, 0, 0,
                     selection_threshold, segment_map, segment_map_stride);

  if (best_error > selection_threshold) {
    return INT64_MAX;
  }

  step = 1 << (n_refinements - 1);
  for (i = 0; i < n_refinements; i++, step >>= 1) {
    for (p = 0; p < n_params; ++p) {
      int step_dir = 0;
      param = param_mat + p;
      curr_param = *param;
      best_param = curr_param;
      // look to the left
      // Note: We have to use force_wmtype() to keep the proper symmetry for
      // ROTZOOM type models
      *param = add_param_offset(p, curr_param, -step);
      force_wmtype(wm, wmtype);
      step_error =
          av1_warp_error(wm, bd, ref, r_width, r_height, r_stride,
                         dst + border * d_stride + border, d_stride, border,
                         border, d_width - 2 * border, d_height - 2 * border, 0,
                         0, best_error, segment_map, segment_map_stride);
      if (step_error < best_error) {
        best_error = step_error;
        best_param = *param;
        step_dir = -1;
      }

      // look to the right
      *param = add_param_offset(p, curr_param, step);
      force_wmtype(wm, wmtype);
      step_error =
          av1_warp_error(wm, bd, ref, r_width, r_height, r_stride,
                         dst + border * d_stride + border, d_stride, border,
                         border, d_width - 2 * border, d_height - 2 * border, 0,
                         0, best_error, segment_map, segment_map_stride);
      if (step_error < best_error) {
        best_error = step_error;
        best_param = *param;
        step_dir = 1;
      }

      // look to the direction chosen above repeatedly until error increases
      // for the biggest step size
      while (step_dir) {
        *param = add_param_offset(p, best_param, step * step_dir);
        force_wmtype(wm, wmtype);
        step_error =
            av1_warp_error(wm, bd, ref, r_width, r_height, r_stride,
                           dst + border * d_stride + border, d_stride, border,
                           border, d_width - 2 * border, d_height - 2 * border,
                           0, 0, best_error, segment_map, segment_map_stride);
        if (step_error < best_error) {
          best_error = step_error;
          best_param = *param;
        } else {
          step_dir = 0;
        }
      }

      // Restore best parameter value so far
      *param = best_param;
      force_wmtype(wm, wmtype);
    }
  }

  wm->wmtype = get_wmtype(wm);
  // Recompute shear params for the refined model
  // This should never fail, because we only ever consider warp-able models
  if (!av1_get_shear_params(wm
#if CONFIG_ACROSS_SCALE_WARP
                            ,
                            sf
#endif  // CONFIG_ACROSS_SCALE_WARP
                            )) {
    assert(0);
  }
  return best_error;
}

#define FEAT_COUNT_TR 3
#define SEG_COUNT_TR 48
void av1_compute_feature_segmentation_map(uint8_t *segment_map, int width,
                                          int height, int *inliers,
                                          int num_inliers) {
  int seg_count = 0;
  memset(segment_map, 0, sizeof(*segment_map) * width * height);

  for (int i = 0; i < num_inliers; i++) {
    int x = inliers[i * 2];
    int y = inliers[i * 2 + 1];
    int seg_x = x >> WARP_ERROR_BLOCK_LOG;
    int seg_y = y >> WARP_ERROR_BLOCK_LOG;
    segment_map[seg_y * width + seg_x] += 1;
  }

  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      uint8_t feat_count = segment_map[i * width + j];
      segment_map[i * width + j] = (feat_count >= FEAT_COUNT_TR);
      seg_count += (segment_map[i * width + j]);
    }
  }

  // If this motion does not make up a large enough portion of the frame,
  // use the unsegmented version of the error metric
  if (seg_count < SEG_COUNT_TR)
    memset(segment_map, 1, width * height * sizeof(*segment_map));
}
