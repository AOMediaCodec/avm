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
#include "config/av1_rtcd.h"

#include "av1/common/filter.h"
#include "av1/common/scale.h"
#include "aom_dsp/aom_filter.h"

// Note: Expect val to be in q4 precision
static INLINE int scaled_x(int val, const struct scale_factors *sf) {
  const int off =
      (sf->x_scale_fp - (1 << REF_SCALE_SHIFT)) * (1 << (SUBPEL_BITS - 1));
  const int64_t tval = (int64_t)val * sf->x_scale_fp + off;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval,
                                           REF_SCALE_SHIFT - SCALE_EXTRA_BITS);
}

// Note: Expect val to be in q4 precision
static INLINE int scaled_y(int val, const struct scale_factors *sf) {
  const int off =
      (sf->y_scale_fp - (1 << REF_SCALE_SHIFT)) * (1 << (SUBPEL_BITS - 1));
  const int64_t tval = (int64_t)val * sf->y_scale_fp + off;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval,
                                           REF_SCALE_SHIFT - SCALE_EXTRA_BITS);
}

// Note: Expect val to be in q4 precision
static int unscaled_value(int val, const struct scale_factors *sf) {
  (void)sf;
  return val * (1 << SCALE_EXTRA_BITS);
}

#if CONFIG_ACROSS_SCALE_TPL_MVS
static INLINE int scaled_x_gen(int val, const struct scale_factors *sf) {
  const int64_t tval = (int64_t)val * sf->x_scale_fp;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval, REF_SCALE_SHIFT);
}

static INLINE int scaled_y_gen(int val, const struct scale_factors *sf) {
  const int64_t tval = (int64_t)val * sf->y_scale_fp;
  return (int)ROUND_POWER_OF_TWO_SIGNED_64(tval, REF_SCALE_SHIFT);
}

static int unscaled_value_gen(int val, const struct scale_factors *sf) {
  (void)sf;
  return val;
}
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS

static int get_fixed_point_scale_factor(int other_size, int this_size,
                                        int prec_bits) {
  // Calculate scaling factor once for each reference frame
  // and use fixed point scaling factors in decoding and encoding routines.
  // Hardware implementations can calculate scale factor in device driver
  // and use multiplication and shifting on hardware instead of division.
  return ((other_size << prec_bits) + this_size / 2) / this_size;
}

// Given the fixed point scale, calculate coarse point scale.
static int fixed_point_scale_to_coarse_point_scale(int scale_fp,
                                                   int lower_prec_by) {
  if (lower_prec_by >= 0)
    return ROUND_POWER_OF_TWO(scale_fp, lower_prec_by);
  else
    return scale_fp << (-lower_prec_by);
}

// Note: x and y are integer precision, mvq4 is q4 precision.
MV32 av1_scale_mv(const MV *mvq4, int x, int y,
                  const struct scale_factors *sf) {
  const int x_off_q4 = scaled_x(x << SUBPEL_BITS, sf);
  const int y_off_q4 = scaled_y(y << SUBPEL_BITS, sf);
  const MV32 res = { scaled_y((y << SUBPEL_BITS) + mvq4->row, sf) - y_off_q4,
                     scaled_x((x << SUBPEL_BITS) + mvq4->col, sf) - x_off_q4 };
  return res;
}

void av1_setup_scale_factors_for_frame(struct scale_factors *sf, int other_w,
                                       int other_h, int this_w, int this_h) {
  if (!valid_ref_frame_size(other_w, other_h, this_w, this_h)) {
    sf->x_scale_fp = REF_INVALID_SCALE;
    sf->y_scale_fp = REF_INVALID_SCALE;
    return;
  }

  sf->x_scale_fp =
      get_fixed_point_scale_factor(other_w, this_w, REF_SCALE_SHIFT);
  sf->y_scale_fp =
      get_fixed_point_scale_factor(other_h, this_h, REF_SCALE_SHIFT);

  sf->x_step_q4 = fixed_point_scale_to_coarse_point_scale(
      sf->x_scale_fp, REF_SCALE_SHIFT - SCALE_SUBPEL_BITS);
  sf->y_step_q4 = fixed_point_scale_to_coarse_point_scale(
      sf->y_scale_fp, REF_SCALE_SHIFT - SCALE_SUBPEL_BITS);

  if (av1_is_scaled(sf)) {
    sf->scale_value_x = scaled_x;
    sf->scale_value_y = scaled_y;
#if CONFIG_ACROSS_SCALE_TPL_MVS
    sf->scale_value_x_gen = scaled_x_gen;
    sf->scale_value_y_gen = scaled_y_gen;
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
  } else {
    sf->scale_value_x = unscaled_value;
    sf->scale_value_y = unscaled_value;
#if CONFIG_ACROSS_SCALE_TPL_MVS
    sf->scale_value_x_gen = unscaled_value_gen;
    sf->scale_value_y_gen = unscaled_value_gen;
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
  }
}
