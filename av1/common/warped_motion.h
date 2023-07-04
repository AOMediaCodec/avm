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

#ifndef AOM_AV1_COMMON_WARPED_MOTION_H_
#define AOM_AV1_COMMON_WARPED_MOTION_H_

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>

#include "config/aom_config.h"

#include "aom_ports/mem.h"
#include "aom_dsp/aom_dsp_common.h"
#include "av1/common/mv.h"
#include "av1/common/convolve.h"

#define MAX_PARAMDIM 9
#define LEAST_SQUARES_SAMPLES_MAX_BITS 3
#define LEAST_SQUARES_SAMPLES_MAX (1 << LEAST_SQUARES_SAMPLES_MAX_BITS)
#define SAMPLES_ARRAY_SIZE (LEAST_SQUARES_SAMPLES_MAX * 2)
#define WARPED_MOTION_DEBUG 0
#define DEFAULT_WMTYPE AFFINE
#define WARP_ERROR_BLOCK_LOG 5
#define WARP_ERROR_BLOCK (1 << WARP_ERROR_BLOCK_LOG)

#define DIV_LUT_PREC_BITS 14
#define DIV_LUT_BITS 8
#define DIV_LUT_NUM (1 << DIV_LUT_BITS)

extern const uint16_t div_lut[DIV_LUT_NUM + 1];

// Decomposes a divisor D such that 1/D = y/2^shift, where y is returned
// at precision of DIV_LUT_PREC_BITS along with the shift.
static INLINE int16_t resolve_divisor_64(uint64_t D, int16_t *shift) {
  int64_t f;
  *shift = (int16_t)((D >> 32) ? get_msb((unsigned int)(D >> 32)) + 32
                               : get_msb((unsigned int)D));
  // e is obtained from D after resetting the most significant 1 bit.
  const int64_t e = D - ((uint64_t)1 << *shift);
  // Get the most significant DIV_LUT_BITS (8) bits of e into f
  if (*shift > DIV_LUT_BITS)
    f = ROUND_POWER_OF_TWO_64(e, *shift - DIV_LUT_BITS);
  else
    f = e << (DIV_LUT_BITS - *shift);
  assert(f <= DIV_LUT_NUM);
  *shift += DIV_LUT_PREC_BITS;
  // Use f as lookup into the precomputed table of multipliers
  return div_lut[f];
}

static INLINE int16_t resolve_divisor_32(uint32_t D, int16_t *shift) {
  int32_t f;
  *shift = get_msb(D);
  // e is obtained from D after resetting the most significant 1 bit.
  const int32_t e = D - ((uint32_t)1 << *shift);
  // Get the most significant DIV_LUT_BITS (8) bits of e into f
  if (*shift > DIV_LUT_BITS)
    f = ROUND_POWER_OF_TWO(e, *shift - DIV_LUT_BITS);
  else
    f = e << (DIV_LUT_BITS - *shift);
  assert(f <= DIV_LUT_NUM);
  *shift += DIV_LUT_PREC_BITS;
  // Use f as lookup into the precomputed table of multipliers
  return div_lut[f];
}

#if CONFIG_IMPROVED_CFL
static INLINE int16_t resolve_divisor_32_CfL(int32_t N, int32_t D,
                                             int16_t shift) {
  int32_t f_n, f_d;
  int ret;

  assert(D >= 0);
  int sign_N = N >= 0 ? 0 : 1;

  if (sign_N) N = -N;

  if (N == 0 || D == 0)
    return 0;
  else {
    int16_t shift_n = get_msb(N);
    int16_t shift_d = get_msb(D);
    // e is obtained from D after resetting the most significant 1 bit.
    const int32_t e_d = D - ((uint32_t)1 << shift_d);
    // Get the most significant DIV_LUT_BITS (8) bits of e into f
    if (shift_d > DIV_LUT_BITS)
      f_d = ROUND_POWER_OF_TWO(e_d, shift_d - DIV_LUT_BITS);
    else
      f_d = e_d << (DIV_LUT_BITS - shift_d);
    assert(f_d <= DIV_LUT_NUM);

    if (shift_n > DIV_LUT_BITS)
      f_n = ROUND_POWER_OF_TWO(N, shift_n - DIV_LUT_BITS);
    else
      f_n = N << (DIV_LUT_BITS - shift_n);

    assert(f_d <= DIV_LUT_NUM);
    assert(f_n <= DIV_LUT_NUM * 2);

    const int shift_add = shift_d - shift_n - shift;

    // The maximum value of `div_lut[f_d] * f_n` is
    // `1 << (DIV_LUT_PREC_BITS + DIV_LUT_BITS + 1)`
    // Hence `shift_add`below is constrained to be <= 1.
    if (shift_add <= 1) {
      ret = (div_lut[f_d] * f_n) >>
            (DIV_LUT_PREC_BITS + DIV_LUT_BITS + shift_add);
    } else {
      ret = 0;
    }

    if (ret >= (2 << shift) - 1) ret = (2 << shift) - 1;

    if (sign_N) ret = -ret;
    return ret;
  }
}
#endif

extern const int16_t av1_warped_filter[WARPEDPIXEL_PREC_SHIFTS * 3 + 1][8];

DECLARE_ALIGNED(8, extern const int8_t,
                av1_filter_8bit[WARPEDPIXEL_PREC_SHIFTS * 3 + 1][8]);

/* clang-format off */
static const int error_measure_lut[512] = {
    // pow 0.7
    16384, 16339, 16294, 16249, 16204, 16158, 16113, 16068,
    16022, 15977, 15932, 15886, 15840, 15795, 15749, 15703,
    15657, 15612, 15566, 15520, 15474, 15427, 15381, 15335,
    15289, 15242, 15196, 15149, 15103, 15056, 15010, 14963,
    14916, 14869, 14822, 14775, 14728, 14681, 14634, 14587,
    14539, 14492, 14445, 14397, 14350, 14302, 14254, 14206,
    14159, 14111, 14063, 14015, 13967, 13918, 13870, 13822,
    13773, 13725, 13676, 13628, 13579, 13530, 13481, 13432,
    13383, 13334, 13285, 13236, 13187, 13137, 13088, 13038,
    12988, 12939, 12889, 12839, 12789, 12739, 12689, 12639,
    12588, 12538, 12487, 12437, 12386, 12335, 12285, 12234,
    12183, 12132, 12080, 12029, 11978, 11926, 11875, 11823,
    11771, 11719, 11667, 11615, 11563, 11511, 11458, 11406,
    11353, 11301, 11248, 11195, 11142, 11089, 11036, 10982,
    10929, 10875, 10822, 10768, 10714, 10660, 10606, 10552,
    10497, 10443, 10388, 10333, 10279, 10224, 10168, 10113,
    10058, 10002,  9947,  9891,  9835,  9779,  9723,  9666,
    9610, 9553, 9497, 9440, 9383, 9326, 9268, 9211,
    9153, 9095, 9037, 8979, 8921, 8862, 8804, 8745,
    8686, 8627, 8568, 8508, 8449, 8389, 8329, 8269,
    8208, 8148, 8087, 8026, 7965, 7903, 7842, 7780,
    7718, 7656, 7593, 7531, 7468, 7405, 7341, 7278,
    7214, 7150, 7086, 7021, 6956, 6891, 6826, 6760,
    6695, 6628, 6562, 6495, 6428, 6361, 6293, 6225,
    6157, 6089, 6020, 5950, 5881, 5811, 5741, 5670,
    5599, 5527, 5456, 5383, 5311, 5237, 5164, 5090,
    5015, 4941, 4865, 4789, 4713, 4636, 4558, 4480,
    4401, 4322, 4242, 4162, 4080, 3998, 3916, 3832,
    3748, 3663, 3577, 3490, 3402, 3314, 3224, 3133,
    3041, 2948, 2854, 2758, 2661, 2562, 2461, 2359,
    2255, 2148, 2040, 1929, 1815, 1698, 1577, 1452,
    1323, 1187, 1045,  894,  731,  550,  339,    0,
    339,  550,  731,  894, 1045, 1187, 1323, 1452,
    1577, 1698, 1815, 1929, 2040, 2148, 2255, 2359,
    2461, 2562, 2661, 2758, 2854, 2948, 3041, 3133,
    3224, 3314, 3402, 3490, 3577, 3663, 3748, 3832,
    3916, 3998, 4080, 4162, 4242, 4322, 4401, 4480,
    4558, 4636, 4713, 4789, 4865, 4941, 5015, 5090,
    5164, 5237, 5311, 5383, 5456, 5527, 5599, 5670,
    5741, 5811, 5881, 5950, 6020, 6089, 6157, 6225,
    6293, 6361, 6428, 6495, 6562, 6628, 6695, 6760,
    6826, 6891, 6956, 7021, 7086, 7150, 7214, 7278,
    7341, 7405, 7468, 7531, 7593, 7656, 7718, 7780,
    7842, 7903, 7965, 8026, 8087, 8148, 8208, 8269,
    8329, 8389, 8449, 8508, 8568, 8627, 8686, 8745,
    8804, 8862, 8921, 8979, 9037, 9095, 9153, 9211,
    9268, 9326, 9383, 9440, 9497, 9553, 9610, 9666,
    9723,  9779,  9835,  9891,  9947, 10002, 10058, 10113,
    10168, 10224, 10279, 10333, 10388, 10443, 10497, 10552,
    10606, 10660, 10714, 10768, 10822, 10875, 10929, 10982,
    11036, 11089, 11142, 11195, 11248, 11301, 11353, 11406,
    11458, 11511, 11563, 11615, 11667, 11719, 11771, 11823,
    11875, 11926, 11978, 12029, 12080, 12132, 12183, 12234,
    12285, 12335, 12386, 12437, 12487, 12538, 12588, 12639,
    12689, 12739, 12789, 12839, 12889, 12939, 12988, 13038,
    13088, 13137, 13187, 13236, 13285, 13334, 13383, 13432,
    13481, 13530, 13579, 13628, 13676, 13725, 13773, 13822,
    13870, 13918, 13967, 14015, 14063, 14111, 14159, 14206,
    14254, 14302, 14350, 14397, 14445, 14492, 14539, 14587,
    14634, 14681, 14728, 14775, 14822, 14869, 14916, 14963,
    15010, 15056, 15103, 15149, 15196, 15242, 15289, 15335,
    15381, 15427, 15474, 15520, 15566, 15612, 15657, 15703,
    15749, 15795, 15840, 15886, 15932, 15977, 16022, 16068,
    16113, 16158, 16204, 16249, 16294, 16339, 16384, 16384,
};
/* clang-format on */

static const uint8_t warp_pad_left[14][16] = {
  { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 3, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 4, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 5, 5, 5, 5, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 6, 6, 6, 6, 6, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 7, 7, 7, 7, 7, 7, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 11, 12, 13, 14, 15 },
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 12, 13, 14, 15 },
  { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 13, 14, 15 },
  { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 14, 15 },
  { 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 15 },
  { 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15 },
};

static const uint8_t warp_pad_right[14][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 11, 11, 11 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7 },
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
  { 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 },
  { 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
  { 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }
};

static INLINE int error_measure(int err) {
  return error_measure_lut[255 + err];
}

// Recompute the translational part of a warp model, so that the center
// of the current block (determined by `mi_row`, `mi_col`, `bsize`)
// has an induced motion vector of `mv`
void av1_set_warp_translation(int mi_row, int mi_col, BLOCK_SIZE bsize, MV mv,
                              WarpedMotionParams *wm);

// Returns the error between the frame described by 'ref' and the frame
// described by 'dst'.
int64_t av1_frame_error(int bd, const uint16_t *ref, int stride, uint16_t *dst,
                        int p_width, int p_height, int p_stride);

int64_t av1_segmented_frame_error(int bd, const uint16_t *ref, int stride,
                                  uint16_t *dst, int p_width, int p_height,
                                  int p_stride, uint8_t *segment_map,
                                  int segment_map_stride);

int64_t av1_calc_highbd_frame_error(const uint16_t *const ref, int stride,
                                    const uint16_t *const dst, int p_width,
                                    int p_height, int p_stride, int bd);

void highbd_warp_plane(WarpedMotionParams *wm, const uint16_t *const ref,
                       int width, int height, int stride, uint16_t *const pred,
                       int p_col, int p_row, int p_width, int p_height,
                       int p_stride, int subsampling_x, int subsampling_y,
                       int bd, ConvolveParams *conv_params);

void warp_plane(WarpedMotionParams *wm, const uint8_t *const ref, int width,
                int height, int stride, uint8_t *pred, int p_col, int p_row,
                int p_width, int p_height, int p_stride, int subsampling_x,
                int subsampling_y, ConvolveParams *conv_params);

void av1_warp_plane(WarpedMotionParams *wm, int bd, const uint16_t *ref,
                    int width, int height, int stride, uint16_t *pred,
                    int p_col, int p_row, int p_width, int p_height,
                    int p_stride, int subsampling_x, int subsampling_y,
                    ConvolveParams *conv_params);

int av1_find_projection(int np, const int *pts1, const int *pts2,
                        BLOCK_SIZE bsize, MV mv, WarpedMotionParams *wm_params,
                        int mi_row, int mi_col);

int av1_get_shear_params(WarpedMotionParams *wm);

#if CONFIG_EXTENDED_WARP_PREDICTION
// Reduce the precision of a warp model, ready for use in the warp filter
// and for storage. This should be called after the non-translational parameters
// are calculated, but before av1_set_warp_translation() or
// av1_get_shear_params() are called
void av1_reduce_warp_model(WarpedMotionParams *wm);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_EXTENDED_WARP_PREDICTION
int av1_extend_warp_model(const bool neighbor_is_above, const BLOCK_SIZE bsize,
                          const MV *center_mv, const int mi_row,
                          const int mi_col,
                          const WarpedMotionParams *neighbor_wm,
                          WarpedMotionParams *wm_params);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_IMPROVED_GLOBAL_MOTION
// Given a warp model which was initially used at a temporal distance of
// `in_distance`, rescale it to a new temporal distance of `out_distance`.
// Both distances are allowed to be negative, but they must be nonzero.
//
// The mathematically ideal way to rescale a warp model from one temporal
// distance to another would be to use a matrix exponential: If we write the
// input model as a 3x3 matrix M, then the output model should be
//
//  ideal output = M ^ (out_distance / in_distance)
//
// However, computing a matrix exponential is complicated, especially in
// fixed point, and so would not be very hardware friendly. In addition,
// this function is mainly used to predict global motion parameters, with
// the true values being coded as a delta from this prediction. As the
// global motion will not be perfectly consistent, there's a limit to how
// accurate our prediction can be.
//
// For these reasons, we approximate the matrix exponential using its
// first-order Taylor series:
//
//  output = I + (M - I) * (out_distance / in_distance)
//
// This is far easier to compute, and provides a "good enough" approximation
// for the models we use in practice, which are all reasonably near to the
// identity model (all parameters except for the translational part are
// within +/- 1/2 of the identity).
static INLINE void av1_scale_warp_model(const WarpedMotionParams *in_params,
                                        int in_distance,
                                        WarpedMotionParams *out_params,
                                        int out_distance) {
  static int param_shift[MAX_PARAMDIM - 1] = {
    GM_TRANS_PREC_DIFF,    GM_TRANS_PREC_DIFF,   GM_ALPHA_PREC_DIFF,
    GM_ALPHA_PREC_DIFF,    GM_ALPHA_PREC_DIFF,   GM_ALPHA_PREC_DIFF,
    GM_ROW3HOMO_PREC_DIFF, GM_ROW3HOMO_PREC_DIFF
  };

  static int param_min[MAX_PARAMDIM - 1] = { GM_TRANS_MIN,    GM_TRANS_MIN,
                                             GM_ALPHA_MIN,    GM_ALPHA_MIN,
                                             GM_ALPHA_MIN,    GM_ALPHA_MIN,
                                             GM_ROW3HOMO_MIN, GM_ROW3HOMO_MIN };

  static int param_max[MAX_PARAMDIM - 1] = { GM_TRANS_MAX,    GM_TRANS_MAX,
                                             GM_ALPHA_MAX,    GM_ALPHA_MAX,
                                             GM_ALPHA_MAX,    GM_ALPHA_MAX,
                                             GM_ROW3HOMO_MAX, GM_ROW3HOMO_MAX };

  assert(in_distance != 0);
  assert(out_distance != 0);

  // Flip signs so that in_distance is positive.
  // We do this because
  //   scaled_value = (... + divisor/2) / divisor
  // is the simplest way to implement division with round-to-nearest in C,
  // but it only works correctly if the divisor is positive
  if (in_distance < 0) {
    in_distance = -in_distance;
    out_distance = -out_distance;
  }

  out_params->wmtype = in_params->wmtype;
  for (int param = 0; param < MAX_PARAMDIM - 1; param++) {
    int center = default_warp_params.wmmat[param];

    int input = in_params->wmmat[param] - center;
    int divisor = in_distance * (1 << param_shift[param]);
    int output = (int)(((int64_t)input * out_distance + divisor / 2) / divisor);
    output = clamp(output, param_min[param], param_max[param]) *
             (1 << param_shift[param]);

    out_params->wmmat[param] = center + output;
  }
}
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

#endif  // AOM_AV1_COMMON_WARPED_MOTION_H_
