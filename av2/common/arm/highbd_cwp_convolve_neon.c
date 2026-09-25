/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.
 * If the Alliance for Open Media Patent License 1.0 was not distributed with
 * this source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>
#include <assert.h>

#include "config/av2_rtcd.h"

#include "av2/common/convolve.h"
#include "av2/common/enums.h"
#include "av2/common/filter.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"

static inline int16x8_t cwp_narrow_combine_s32(int32x4_t lo, int32x4_t hi) {
  return vcombine_s16(vmovn_s32(lo), vmovn_s32(hi));
}

// 4-tap horizontal filter helpers

static inline int16x8_t cwp_highbd_convolve4_8_2d_h(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x4_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
  int32x4_t sum0 = vmlal_lane_s16(offset, vget_low_s16(s0), x_filter, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), x_filter, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), x_filter, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), x_filter, 3);

  int32x4_t sum1 = vmlal_lane_s16(offset, vget_high_s16(s0), x_filter, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), x_filter, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), x_filter, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), x_filter, 3);

  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return cwp_narrow_combine_s32(sum0, sum1);
}

static inline int16x4_t cwp_highbd_convolve4_4_2d_h(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, x_filter, 0);
  sum = vmlal_lane_s16(sum, s1, x_filter, 1);
  sum = vmlal_lane_s16(sum, s2, x_filter, 2);
  sum = vmlal_lane_s16(sum, s3, x_filter, 3);
  sum = vrshlq_s32(sum, round_shift);
  return vmovn_s32(sum);
}

// 6-tap horizontal filter helpers

static inline int16x8_t cwp_highbd_convolve6_8_2d_h(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t offset,
    const int32x4_t round_shift) {
  int32x4_t sum0 = vmlal_lane_s16(offset, vget_low_s16(s0), f_lo, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f_lo, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f_lo, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f_lo, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f_hi, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f_hi, 1);

  int32x4_t sum1 = vmlal_lane_s16(offset, vget_high_s16(s0), f_lo, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f_lo, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f_lo, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f_lo, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f_hi, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f_hi, 1);

  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return cwp_narrow_combine_s32(sum0, sum1);
}

// Symmetric 6-tap: fold s[0]+s[5], s[1]+s[4], s[2]+s[3] before 3 MACs.
// Dual accumulators break the 3-MAC chain (9 cycles) into a 2-MAC chain
// (6 cycles) + independent vmull (3 cycles) + vaddq merge (2 cycles) = 8
// cycles.
static inline int16x8_t cwp_highbd_convolve6_sym_8_2d_h(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_sym, const int32x4_t offset,
    const int32x4_t round_shift) {
  int16x8_t a = vaddq_s16(s0, s5);
  int16x8_t b = vaddq_s16(s1, s4);
  int16x8_t c = vaddq_s16(s2, s3);

  // Lo half: split into 2-MAC chain + independent multiply
  int32x4_t acc0_lo = vmlal_lane_s16(offset, vget_low_s16(a), f_sym, 0);
  int32x4_t ind_lo = vmull_lane_s16(vget_low_s16(b), f_sym, 1);
  acc0_lo = vmlal_lane_s16(acc0_lo, vget_low_s16(c), f_sym, 2);
  int32x4_t sum0 = vaddq_s32(acc0_lo, ind_lo);

  // Hi half: same split
  int32x4_t acc0_hi = vmlal_lane_s16(offset, vget_high_s16(a), f_sym, 0);
  int32x4_t ind_hi = vmull_lane_s16(vget_high_s16(b), f_sym, 1);
  acc0_hi = vmlal_lane_s16(acc0_hi, vget_high_s16(c), f_sym, 2);
  int32x4_t sum1 = vaddq_s32(acc0_hi, ind_hi);

  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return cwp_narrow_combine_s32(sum0, sum1);
}

// 8-tap horizontal filter helpers

static inline int16x8_t cwp_highbd_convolve8_8_2d_h(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t x_filter,
    const int32x4_t offset, const int32x4_t round_shift) {
  const int16x4_t f_lo = vget_low_s16(x_filter);
  const int16x4_t f_hi = vget_high_s16(x_filter);

  int32x4_t sum0 = vmlal_lane_s16(offset, vget_low_s16(s0), f_lo, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f_lo, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f_lo, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f_lo, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f_hi, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f_hi, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s6), f_hi, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s7), f_hi, 3);

  int32x4_t sum1 = vmlal_lane_s16(offset, vget_high_s16(s0), f_lo, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f_lo, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f_lo, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f_lo, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f_hi, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f_hi, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s6), f_hi, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s7), f_hi, 3);

  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return cwp_narrow_combine_s32(sum0, sum1);
}

// 8-tap horizontal loop function with 2-row interleaving to hide load latency.
// 8 loads per row x 2 rows = 16 independent loads for OOO scheduling.
static inline void cwp_highbd_convolve_2d_horiz_8wide_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x8_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
  // Process 2 rows per outer iteration to interleave independent load/compute
  // chains, hiding Apple Silicon's 4-cycle load latency.
  while (h >= 2) {
    int width = w;
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    int16_t *d0 = im;
    int16_t *d1 = im + im_stride;
    do {
      // Row 0 first 4 loads
      int16x8_t r0_s0 = vld1q_s16(s0_ptr + 0);
      int16x8_t r0_s1 = vld1q_s16(s0_ptr + 1);
      int16x8_t r0_s2 = vld1q_s16(s0_ptr + 2);
      int16x8_t r0_s3 = vld1q_s16(s0_ptr + 3);
      // Row 1 first 4 loads (interleaved to fill load latency bubbles)
      int16x8_t r1_s0 = vld1q_s16(s1_ptr + 0);
      int16x8_t r1_s1 = vld1q_s16(s1_ptr + 1);
      int16x8_t r1_s2 = vld1q_s16(s1_ptr + 2);
      int16x8_t r1_s3 = vld1q_s16(s1_ptr + 3);
      // Row 0 remaining 4 loads
      int16x8_t r0_s4 = vld1q_s16(s0_ptr + 4);
      int16x8_t r0_s5 = vld1q_s16(s0_ptr + 5);
      int16x8_t r0_s6 = vld1q_s16(s0_ptr + 6);
      int16x8_t r0_s7 = vld1q_s16(s0_ptr + 7);
      // Row 1 remaining 4 loads
      int16x8_t r1_s4 = vld1q_s16(s1_ptr + 4);
      int16x8_t r1_s5 = vld1q_s16(s1_ptr + 5);
      int16x8_t r1_s6 = vld1q_s16(s1_ptr + 6);
      int16x8_t r1_s7 = vld1q_s16(s1_ptr + 7);
      // Compute both rows
      int16x8_t res0 = cwp_highbd_convolve8_8_2d_h(
          r0_s0, r0_s1, r0_s2, r0_s3, r0_s4, r0_s5, r0_s6, r0_s7, x_filter,
          offset, round_shift);
      int16x8_t res1 = cwp_highbd_convolve8_8_2d_h(
          r1_s0, r1_s1, r1_s2, r1_s3, r1_s4, r1_s5, r1_s6, r1_s7, x_filter,
          offset, round_shift);
      vst1q_s16(d0, res0);
      vst1q_s16(d1, res1);
      s0_ptr += 8;
      s1_ptr += 8;
      d0 += 8;
      d1 += 8;
      width -= 8;
    } while (width > 0);
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    int width = w;
    const int16_t *s = (const int16_t *)src;
    int16_t *d = im;
    do {
      int16x8_t s0 = vld1q_s16(s + 0);
      int16x8_t s1 = vld1q_s16(s + 1);
      int16x8_t s2 = vld1q_s16(s + 2);
      int16x8_t s3 = vld1q_s16(s + 3);
      int16x8_t s4 = vld1q_s16(s + 4);
      int16x8_t s5 = vld1q_s16(s + 5);
      int16x8_t s6 = vld1q_s16(s + 6);
      int16x8_t s7 = vld1q_s16(s + 7);
      int16x8_t r = cwp_highbd_convolve8_8_2d_h(s0, s1, s2, s3, s4, s5, s6, s7,
                                                x_filter, offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
  }
}

// 4-tap horizontal loop functions
// 2-row interleaving to hide load latency (4 loads/row x 2 rows = 8 loads).

static inline void cwp_highbd_convolve_2d_horiz_8wide_4tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x4_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
  while (h >= 2) {
    int width = w;
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    int16_t *d0 = im;
    int16_t *d1 = im + im_stride;
    do {
      // Row 0 loads
      int16x8_t r0_s0 = vld1q_s16(s0_ptr + 0);
      int16x8_t r0_s1 = vld1q_s16(s0_ptr + 1);
      // Row 1 loads (interleaved)
      int16x8_t r1_s0 = vld1q_s16(s1_ptr + 0);
      int16x8_t r1_s1 = vld1q_s16(s1_ptr + 1);
      // Row 0 remaining loads
      int16x8_t r0_s2 = vld1q_s16(s0_ptr + 2);
      int16x8_t r0_s3 = vld1q_s16(s0_ptr + 3);
      // Row 1 remaining loads
      int16x8_t r1_s2 = vld1q_s16(s1_ptr + 2);
      int16x8_t r1_s3 = vld1q_s16(s1_ptr + 3);
      // Compute both rows
      int16x8_t res0 = cwp_highbd_convolve4_8_2d_h(
          r0_s0, r0_s1, r0_s2, r0_s3, x_filter, offset, round_shift);
      int16x8_t res1 = cwp_highbd_convolve4_8_2d_h(
          r1_s0, r1_s1, r1_s2, r1_s3, x_filter, offset, round_shift);
      vst1q_s16(d0, res0);
      vst1q_s16(d1, res1);
      s0_ptr += 8;
      s1_ptr += 8;
      d0 += 8;
      d1 += 8;
      width -= 8;
    } while (width > 0);
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    int width = w;
    const int16_t *s = (const int16_t *)src;
    int16_t *d = im;
    do {
      int16x8_t s0 = vld1q_s16(s + 0);
      int16x8_t s1 = vld1q_s16(s + 1);
      int16x8_t s2 = vld1q_s16(s + 2);
      int16x8_t s3 = vld1q_s16(s + 3);
      int16x8_t r = cwp_highbd_convolve4_8_2d_h(s0, s1, s2, s3, x_filter,
                                                offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
  }
}

static inline void cwp_highbd_convolve_2d_horiz_4wide_4tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int h,
    const int16x4_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
  // 2-row interleaving to hide load latency (4 loads/row x 2 rows = 8 loads).
  while (h >= 2) {
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    // Row 0 first 2 loads
    int16x4_t r0_s0 = vld1_s16(s0_ptr + 0);
    int16x4_t r0_s1 = vld1_s16(s0_ptr + 1);
    // Row 1 first 2 loads (interleaved)
    int16x4_t r1_s0 = vld1_s16(s1_ptr + 0);
    int16x4_t r1_s1 = vld1_s16(s1_ptr + 1);
    // Row 0 remaining loads
    int16x4_t r0_s2 = vld1_s16(s0_ptr + 2);
    int16x4_t r0_s3 = vld1_s16(s0_ptr + 3);
    // Row 1 remaining loads
    int16x4_t r1_s2 = vld1_s16(s1_ptr + 2);
    int16x4_t r1_s3 = vld1_s16(s1_ptr + 3);
    // Compute both rows
    int16x4_t res0 = cwp_highbd_convolve4_4_2d_h(r0_s0, r0_s1, r0_s2, r0_s3,
                                                 x_filter, offset, round_shift);
    int16x4_t res1 = cwp_highbd_convolve4_4_2d_h(r1_s0, r1_s1, r1_s2, r1_s3,
                                                 x_filter, offset, round_shift);
    vst1_s16(im, res0);
    vst1_s16(im + im_stride, res1);
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    const int16_t *s = (const int16_t *)src;
    int16x4_t s0 = vld1_s16(s + 0);
    int16x4_t s1 = vld1_s16(s + 1);
    int16x4_t s2 = vld1_s16(s + 2);
    int16x4_t s3 = vld1_s16(s + 3);
    int16x4_t r = cwp_highbd_convolve4_4_2d_h(s0, s1, s2, s3, x_filter, offset,
                                              round_shift);
    vst1_s16(im, r);
  }
}

// 6-tap horizontal loop functions
// 2-row interleaving to hide load latency (6 loads/row x 2 rows = 12 loads).

static inline void cwp_highbd_convolve_2d_horiz_8wide_6tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t offset,
    const int32x4_t round_shift) {
  while (h >= 2) {
    int width = w;
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    int16_t *d0 = im;
    int16_t *d1 = im + im_stride;
    do {
      // Row 0 first 3 loads
      int16x8_t r0_s0 = vld1q_s16(s0_ptr + 0);
      int16x8_t r0_s1 = vld1q_s16(s0_ptr + 1);
      int16x8_t r0_s2 = vld1q_s16(s0_ptr + 2);
      // Row 1 first 3 loads (interleaved)
      int16x8_t r1_s0 = vld1q_s16(s1_ptr + 0);
      int16x8_t r1_s1 = vld1q_s16(s1_ptr + 1);
      int16x8_t r1_s2 = vld1q_s16(s1_ptr + 2);
      // Row 0 remaining 3 loads
      int16x8_t r0_s3 = vld1q_s16(s0_ptr + 3);
      int16x8_t r0_s4 = vld1q_s16(s0_ptr + 4);
      int16x8_t r0_s5 = vld1q_s16(s0_ptr + 5);
      // Row 1 remaining 3 loads
      int16x8_t r1_s3 = vld1q_s16(s1_ptr + 3);
      int16x8_t r1_s4 = vld1q_s16(s1_ptr + 4);
      int16x8_t r1_s5 = vld1q_s16(s1_ptr + 5);
      // Compute both rows
      int16x8_t res0 =
          cwp_highbd_convolve6_8_2d_h(r0_s0, r0_s1, r0_s2, r0_s3, r0_s4, r0_s5,
                                      f_lo, f_hi, offset, round_shift);
      int16x8_t res1 =
          cwp_highbd_convolve6_8_2d_h(r1_s0, r1_s1, r1_s2, r1_s3, r1_s4, r1_s5,
                                      f_lo, f_hi, offset, round_shift);
      vst1q_s16(d0, res0);
      vst1q_s16(d1, res1);
      s0_ptr += 8;
      s1_ptr += 8;
      d0 += 8;
      d1 += 8;
      width -= 8;
    } while (width > 0);
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    int width = w;
    const int16_t *s = (const int16_t *)src;
    int16_t *d = im;
    do {
      int16x8_t s0 = vld1q_s16(s + 0);
      int16x8_t s1 = vld1q_s16(s + 1);
      int16x8_t s2 = vld1q_s16(s + 2);
      int16x8_t s3 = vld1q_s16(s + 3);
      int16x8_t s4 = vld1q_s16(s + 4);
      int16x8_t s5 = vld1q_s16(s + 5);
      int16x8_t r = cwp_highbd_convolve6_8_2d_h(s0, s1, s2, s3, s4, s5, f_lo,
                                                f_hi, offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
  }
}

static inline void cwp_highbd_convolve_2d_horiz_8wide_6tap_sym_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x4_t f_sym, const int32x4_t offset,
    const int32x4_t round_shift) {
  // Process 2 rows per outer iteration to interleave independent load/compute
  // chains, hiding Apple Silicon's 4-cycle load latency.
  while (h >= 2) {
    int width = w;
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    int16_t *d0 = im;
    int16_t *d1 = im + im_stride;
    do {
      // Row 0 loads
      int16x8_t r0_s0 = vld1q_s16(s0_ptr + 0);
      int16x8_t r0_s1 = vld1q_s16(s0_ptr + 1);
      int16x8_t r0_s2 = vld1q_s16(s0_ptr + 2);
      // Row 1 loads (interleaved to fill load latency bubbles)
      int16x8_t r1_s0 = vld1q_s16(s1_ptr + 0);
      int16x8_t r1_s1 = vld1q_s16(s1_ptr + 1);
      int16x8_t r1_s2 = vld1q_s16(s1_ptr + 2);
      // Row 0 remaining loads
      int16x8_t r0_s3 = vld1q_s16(s0_ptr + 3);
      int16x8_t r0_s4 = vld1q_s16(s0_ptr + 4);
      int16x8_t r0_s5 = vld1q_s16(s0_ptr + 5);
      // Row 1 remaining loads
      int16x8_t r1_s3 = vld1q_s16(s1_ptr + 3);
      int16x8_t r1_s4 = vld1q_s16(s1_ptr + 4);
      int16x8_t r1_s5 = vld1q_s16(s1_ptr + 5);
      // Compute both rows
      int16x8_t res0 = cwp_highbd_convolve6_sym_8_2d_h(
          r0_s0, r0_s1, r0_s2, r0_s3, r0_s4, r0_s5, f_sym, offset, round_shift);
      int16x8_t res1 = cwp_highbd_convolve6_sym_8_2d_h(
          r1_s0, r1_s1, r1_s2, r1_s3, r1_s4, r1_s5, f_sym, offset, round_shift);
      vst1q_s16(d0, res0);
      vst1q_s16(d1, res1);
      s0_ptr += 8;
      s1_ptr += 8;
      d0 += 8;
      d1 += 8;
      width -= 8;
    } while (width > 0);
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    int width = w;
    const int16_t *s = (const int16_t *)src;
    int16_t *d = im;
    do {
      int16x8_t s0 = vld1q_s16(s + 0);
      int16x8_t s1 = vld1q_s16(s + 1);
      int16x8_t s2 = vld1q_s16(s + 2);
      int16x8_t s3 = vld1q_s16(s + 3);
      int16x8_t s4 = vld1q_s16(s + 4);
      int16x8_t s5 = vld1q_s16(s + 5);
      int16x8_t r = cwp_highbd_convolve6_sym_8_2d_h(s0, s1, s2, s3, s4, s5,
                                                    f_sym, offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
  }
}

// Vertical 8-tap filter producing CONV_BUF_TYPE (uint16_t) compound result
static inline int32x4_t cwp_highbd_convolve8_v_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_low_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f_hi, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s6), f_hi, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s7), f_hi, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve8_v_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_high_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f_hi, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s6), f_hi, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s7), f_hi, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve8_v_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, f_lo, 0);
  sum = vmlal_lane_s16(sum, s1, f_lo, 1);
  sum = vmlal_lane_s16(sum, s2, f_lo, 2);
  sum = vmlal_lane_s16(sum, s3, f_lo, 3);
  sum = vmlal_lane_s16(sum, s4, f_hi, 0);
  sum = vmlal_lane_s16(sum, s5, f_hi, 1);
  sum = vmlal_lane_s16(sum, s6, f_hi, 2);
  sum = vmlal_lane_s16(sum, s7, f_hi, 3);
  return sum;
}

// 4-tap vertical filter helpers

static inline int32x4_t cwp_highbd_convolve4_v_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x4_t f, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_low_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve4_v_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x4_t f, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_high_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve4_v_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t f, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, f, 0);
  sum = vmlal_lane_s16(sum, s1, f, 1);
  sum = vmlal_lane_s16(sum, s2, f, 2);
  sum = vmlal_lane_s16(sum, s3, f, 3);
  return sum;
}

// 6-tap vertical filter helpers

static inline int32x4_t cwp_highbd_convolve6_v_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_low_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f_hi, 1);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve6_v_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_high_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f_hi, 1);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve6_v_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, f_lo, 0);
  sum = vmlal_lane_s16(sum, s1, f_lo, 1);
  sum = vmlal_lane_s16(sum, s2, f_lo, 2);
  sum = vmlal_lane_s16(sum, s3, f_lo, 3);
  sum = vmlal_lane_s16(sum, s4, f_hi, 0);
  sum = vmlal_lane_s16(sum, s5, f_hi, 1);
  return sum;
}

// Symmetric 6-tap vertical filter helpers using widening add (s16->s32).
// Horizontal intermediates use the full int16 range, so vadd_s16 overflows.
// vaddl_s16 widens to s32 safely, then we multiply in s32.

static inline int32x4_t cwp_highbd_convolve6_sym_v_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int32x4_t f0, const int32x4_t f1, const int32x4_t f2,
    const int32x4_t offset) {
  // Fold symmetric pairs: s0+s5, s1+s4, s2+s3 (widening to s32)
  int32x4_t a = vaddl_s16(vget_low_s16(s0), vget_low_s16(s5));
  int32x4_t b = vaddl_s16(vget_low_s16(s1), vget_low_s16(s4));
  int32x4_t c = vaddl_s16(vget_low_s16(s2), vget_low_s16(s3));
  // Multiply and accumulate in s32
  int32x4_t sum = vmlaq_s32(offset, a, f0);
  sum = vmlaq_s32(sum, b, f1);
  sum = vmlaq_s32(sum, c, f2);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve6_sym_v_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int32x4_t f0, const int32x4_t f1, const int32x4_t f2,
    const int32x4_t offset) {
  int32x4_t a = vaddl_s16(vget_high_s16(s0), vget_high_s16(s5));
  int32x4_t b = vaddl_s16(vget_high_s16(s1), vget_high_s16(s4));
  int32x4_t c = vaddl_s16(vget_high_s16(s2), vget_high_s16(s3));
  int32x4_t sum = vmlaq_s32(offset, a, f0);
  sum = vmlaq_s32(sum, b, f1);
  sum = vmlaq_s32(sum, c, f2);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve6_sym_v_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int32x4_t f0, const int32x4_t f1, const int32x4_t f2,
    const int32x4_t offset) {
  int32x4_t a = vaddl_s16(s0, s5);
  int32x4_t b = vaddl_s16(s1, s4);
  int32x4_t c = vaddl_s16(s2, s3);
  int32x4_t sum = vmlaq_s32(offset, a, f0);
  sum = vmlaq_s32(sum, b, f1);
  sum = vmlaq_s32(sum, c, f2);
  return sum;
}

static inline uint16x4_t cwp_compound_avg_clip_4(
    int32x4_t vert_sum, const CONV_BUF_TYPE *dst16, int32_t fwd_offset,
    int32_t bck_offset, int use_wtd_comp_avg, const int32x4_t sub_const,
    int round_bits, uint16x4_t max_val, uint16x4_t fwd_u16,
    uint16x4_t bck_u16) {
  uint16x4_t d16_raw = vld1_u16(dst16);
  int32x4_t tmp;

  if (use_wtd_comp_avg) {
    // Narrow vert to u16 (non-negative due to vert_offset bias), then use
    // u16 widening multiply-accumulate: vmull_u16 implicitly widens both
    // operands, eliminating the separate vmovl_u16 + vmulq_n_s32 path.
    uint16x4_t vert_u16 = vqrshrun_n_s32(vert_sum, COMPOUND_ROUND1_BITS);
    uint32x4_t wtd = vmull_u16(d16_raw, fwd_u16);
    wtd = vmlal_u16(wtd, vert_u16, bck_u16);
    wtd = vshrq_n_u32(wtd, DIST_PRECISION_BITS);
    tmp = vreinterpretq_s32_u32(wtd);
  } else {
    // Narrow vert to u16 (values are in [0, 65535] due to vert_offset bias),
    // then use u16 halving add: (a + b) >> 1 in a single instruction.
    uint16x4_t vert_u16 = vqrshrun_n_s32(vert_sum, COMPOUND_ROUND1_BITS);
    uint16x4_t avg = vhadd_u16(d16_raw, vert_u16);
    // Widen back to s32 for sub_const and final rounding shift.
    tmp = vreinterpretq_s32_u32(vmovl_u16(avg));
  }
  tmp = vsubq_s32(tmp, sub_const);

  assert(round_bits == 4 || round_bits == 2);
  uint16x4_t res;
  if (round_bits == 4) {
    res = vqrshrun_n_s32(tmp, 4);
  } else {
    res = vqrshrun_n_s32(tmp, 2);
  }
  res = vmin_u16(res, max_val);
  return res;
}

static inline uint16x8_t cwp_compound_avg_clip_8(
    int32x4_t vert_lo, int32x4_t vert_hi, const CONV_BUF_TYPE *dst16,
    int32_t fwd_offset, int32_t bck_offset, int use_wtd_comp_avg,
    const int32x4_t sub_const, int round_bits, uint16x8_t max_val,
    uint16x4_t fwd_u16, uint16x4_t bck_u16) {
  uint16x8_t d16_raw = vld1q_u16(dst16);
  int32x4_t tmp_lo, tmp_hi;

  if (use_wtd_comp_avg) {
    // Narrow vert to u16 (non-negative due to vert_offset bias), then use
    // u16 widening multiply-accumulate: vmull_u16 implicitly widens both
    // operands, eliminating the separate vmovl_u16 + vmulq_n_s32 path.
    uint16x4_t vert_lo_u16 = vqrshrun_n_s32(vert_lo, COMPOUND_ROUND1_BITS);
    uint16x4_t vert_hi_u16 = vqrshrun_n_s32(vert_hi, COMPOUND_ROUND1_BITS);
    uint32x4_t wtd_lo = vmull_u16(vget_low_u16(d16_raw), fwd_u16);
    wtd_lo = vmlal_u16(wtd_lo, vert_lo_u16, bck_u16);
    wtd_lo = vshrq_n_u32(wtd_lo, DIST_PRECISION_BITS);
    uint32x4_t wtd_hi = vmull_u16(vget_high_u16(d16_raw), fwd_u16);
    wtd_hi = vmlal_u16(wtd_hi, vert_hi_u16, bck_u16);
    wtd_hi = vshrq_n_u32(wtd_hi, DIST_PRECISION_BITS);
    tmp_lo = vreinterpretq_s32_u32(wtd_lo);
    tmp_hi = vreinterpretq_s32_u32(wtd_hi);
  } else {
    // Narrow vert to u16 (values are in [0, 65535] due to vert_offset bias),
    // then use u16 halving add: (a + b) >> 1 in a single instruction.
    uint16x8_t vert_u16 =
        vcombine_u16(vqrshrun_n_s32(vert_lo, COMPOUND_ROUND1_BITS),
                     vqrshrun_n_s32(vert_hi, COMPOUND_ROUND1_BITS));
    uint16x8_t avg = vhaddq_u16(d16_raw, vert_u16);
    // Widen back to s32 for sub_const and final rounding shift.
    tmp_lo = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(avg)));
    tmp_hi = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(avg)));
  }
  tmp_lo = vsubq_s32(tmp_lo, sub_const);
  tmp_hi = vsubq_s32(tmp_hi, sub_const);

  assert(round_bits == 4 || round_bits == 2);
  uint16x8_t res;
  if (round_bits == 4) {
    res = vcombine_u16(vqrshrun_n_s32(tmp_lo, 4), vqrshrun_n_s32(tmp_hi, 4));
  } else {
    res = vcombine_u16(vqrshrun_n_s32(tmp_lo, 2), vqrshrun_n_s32(tmp_hi, 2));
  }
  res = vminq_u16(res, max_val);
  return res;
}

// Store to dst16 (no-average path)
static inline void cwp_store_dst16_4(int32x4_t vert_sum, CONV_BUF_TYPE *dst16) {
  uint16x4_t res = vqrshrun_n_s32(vert_sum, COMPOUND_ROUND1_BITS);
  vst1_u16(dst16, res);
}

static inline void cwp_store_dst16_8(int32x4_t vert_lo, int32x4_t vert_hi,
                                     CONV_BUF_TYPE *dst16) {
  uint16x8_t res = vcombine_u16(vqrshrun_n_s32(vert_lo, COMPOUND_ROUND1_BITS),
                                vqrshrun_n_s32(vert_hi, COMPOUND_ROUND1_BITS));
  vst1q_u16(dst16, res);
}

// MODE: 0 = store to dst16, 1 = simple compound avg, 2 = weighted compound avg
#define CWP_FINISH_ROW_8(lo, hi, MODE)                                     \
  do {                                                                     \
    if ((MODE) == 0) {                                                     \
      cwp_store_dst16_8((lo), (hi), d16);                                  \
    } else {                                                               \
      uint16x8_t _r8 = cwp_compound_avg_clip_8(                            \
          (lo), (hi), d16, fwd_offset, bck_offset, (MODE) == 2, sub_const, \
          round_bits, max_val, fwd_u16, bck_u16);                          \
      vst1q_u16(d, _r8);                                                   \
    }                                                                      \
    d += dst_stride;                                                       \
    d16 += dst16_stride;                                                   \
  } while (0)

#define CWP_FINISH_ROW_4(sum, MODE)                                   \
  do {                                                                \
    if ((MODE) == 0) {                                                \
      cwp_store_dst16_4((sum), d16);                                  \
    } else {                                                          \
      uint16x4_t _r4 = cwp_compound_avg_clip_4(                       \
          (sum), d16, fwd_offset, bck_offset, (MODE) == 2, sub_const, \
          round_bits, max_val, fwd_u16, bck_u16);                     \
      vst1_u16(d, _r4);                                               \
    }                                                                 \
    d += dst_stride;                                                  \
    d16 += dst16_stride;                                              \
  } while (0)

static inline void cwp_highbd_convolve_2d_vert_8wide_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h,
    const int16x8_t y_filter, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);
  const int16x4_t f_lo = vget_low_s16(y_filter);
  const int16x4_t f_hi = vget_high_s16(y_filter);

#define VERT8_8W_4ROWS(MODE)                                                \
  do {                                                                      \
    int height = h;                                                         \
    const int16_t *s = src;                                                 \
    uint16_t *d = dst;                                                      \
    CONV_BUF_TYPE *d16 = dst16;                                             \
    int16x8_t s0 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s1 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s2 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s3 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s4 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s5 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    int16x8_t s6 = vld1q_s16(s);                                            \
    s += src_stride;                                                        \
    do {                                                                    \
      int16x8_t s7 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s8 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s9 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s10 = vld1q_s16(s);                                         \
      s += src_stride;                                                      \
      int32x4_t lo, hi;                                                     \
      lo = cwp_highbd_convolve8_v_lo(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                     f_hi, vert_offset);                    \
      hi = cwp_highbd_convolve8_v_hi(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                       \
      lo = cwp_highbd_convolve8_v_lo(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                     f_hi, vert_offset);                    \
      hi = cwp_highbd_convolve8_v_hi(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                       \
      lo = cwp_highbd_convolve8_v_lo(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                     f_hi, vert_offset);                    \
      hi = cwp_highbd_convolve8_v_hi(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                       \
      lo = cwp_highbd_convolve8_v_lo(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                     f_hi, vert_offset);                    \
      hi = cwp_highbd_convolve8_v_hi(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                       \
      s0 = s4;                                                              \
      s1 = s5;                                                              \
      s2 = s6;                                                              \
      s3 = s7;                                                              \
      s4 = s8;                                                              \
      s5 = s9;                                                              \
      s6 = s10;                                                             \
      height -= 4;                                                          \
    } while (height > 0);                                                   \
  } while (0)

  do {
    if (!do_average) {
      VERT8_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT8_8W_4ROWS(2);
    } else {
      VERT8_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT8_8W_4ROWS
}

static inline void cwp_highbd_convolve_2d_vert_4wide_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int16x8_t y_filter,
    const int32x4_t vert_offset, const int32x4_t sub_const, int round_bits,
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset,
    int32_t bck_offset, int bd, uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);
  const int16x4_t f_lo = vget_low_s16(y_filter);
  const int16x4_t f_hi = vget_high_s16(y_filter);

#define VERT8_4W_4ROWS(MODE)                                                \
  do {                                                                      \
    const int16_t *s = src;                                                 \
    uint16_t *d = dst;                                                      \
    CONV_BUF_TYPE *d16 = dst16;                                             \
    int height = h;                                                         \
    int16x4_t s0 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s1 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s2 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s3 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s4 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s5 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    int16x4_t s6 = vld1_s16(s);                                             \
    s += src_stride;                                                        \
    do {                                                                    \
      int16x4_t s7 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s8 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s9 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s10 = vld1_s16(s);                                          \
      s += src_stride;                                                      \
      int32x4_t sum;                                                        \
      sum = cwp_highbd_convolve8_v_4(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_4(sum, MODE);                                          \
      sum = cwp_highbd_convolve8_v_4(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_4(sum, MODE);                                          \
      sum = cwp_highbd_convolve8_v_4(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_4(sum, MODE);                                          \
      sum = cwp_highbd_convolve8_v_4(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                     f_hi, vert_offset);                    \
      CWP_FINISH_ROW_4(sum, MODE);                                          \
      s0 = s4;                                                              \
      s1 = s5;                                                              \
      s2 = s6;                                                              \
      s3 = s7;                                                              \
      s4 = s8;                                                              \
      s5 = s9;                                                              \
      s6 = s10;                                                             \
      height -= 4;                                                          \
    } while (height > 0);                                                   \
  } while (0)

  if (!do_average) {
    VERT8_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT8_4W_4ROWS(2);
  } else {
    VERT8_4W_4ROWS(1);
  }

#undef VERT8_4W_4ROWS
}

// 4-tap vertical loop functions

static inline void cwp_highbd_convolve_2d_vert_8wide_4tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h,
    const int16x4_t y_filter, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT4_8W_4ROWS(MODE)                                                 \
  do {                                                                       \
    int height = h;                                                          \
    const int16_t *s = src;                                                  \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    int16x8_t s0 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s1 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s2 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    do {                                                                     \
      int16x8_t s3 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s4 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s5 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s6 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int32x4_t lo, hi;                                                      \
      lo = cwp_highbd_convolve4_v_lo(s0, s1, s2, s3, y_filter, vert_offset); \
      hi = cwp_highbd_convolve4_v_hi(s0, s1, s2, s3, y_filter, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve4_v_lo(s1, s2, s3, s4, y_filter, vert_offset); \
      hi = cwp_highbd_convolve4_v_hi(s1, s2, s3, s4, y_filter, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve4_v_lo(s2, s3, s4, s5, y_filter, vert_offset); \
      hi = cwp_highbd_convolve4_v_hi(s2, s3, s4, s5, y_filter, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve4_v_lo(s3, s4, s5, s6, y_filter, vert_offset); \
      hi = cwp_highbd_convolve4_v_hi(s3, s4, s5, s6, y_filter, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)

  do {
    if (!do_average) {
      VERT4_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT4_8W_4ROWS(2);
    } else {
      VERT4_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT4_8W_4ROWS
}

static inline void cwp_highbd_convolve_2d_vert_4wide_4tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int16x4_t y_filter,
    const int32x4_t vert_offset, const int32x4_t sub_const, int round_bits,
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset,
    int32_t bck_offset, int bd, uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT4_4W_4ROWS(MODE)                                                 \
  do {                                                                       \
    const int16_t *s = src;                                                  \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    int height = h;                                                          \
    int16x4_t s0 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s1 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s2 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    do {                                                                     \
      int16x4_t s3 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s4 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s5 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s6 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int32x4_t sum;                                                         \
      sum = cwp_highbd_convolve4_v_4(s0, s1, s2, s3, y_filter, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve4_v_4(s1, s2, s3, s4, y_filter, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve4_v_4(s2, s3, s4, s5, y_filter, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve4_v_4(s3, s4, s5, s6, y_filter, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)

  if (!do_average) {
    VERT4_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT4_4W_4ROWS(2);
  } else {
    VERT4_4W_4ROWS(1);
  }

#undef VERT4_4W_4ROWS
}

// 6-tap vertical loop functions

static inline void cwp_highbd_convolve_2d_vert_8wide_6tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT6_8W_4ROWS(MODE)                                             \
  do {                                                                   \
    int height = h;                                                      \
    const int16_t *s = src;                                              \
    uint16_t *d = dst;                                                   \
    CONV_BUF_TYPE *d16 = dst16;                                          \
    int16x8_t s0 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s1 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s2 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s3 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s4 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    do {                                                                 \
      int16x8_t s5 = vld1q_s16(s);                                       \
      s += src_stride;                                                   \
      int16x8_t s6 = vld1q_s16(s);                                       \
      s += src_stride;                                                   \
      int16x8_t s7 = vld1q_s16(s);                                       \
      s += src_stride;                                                   \
      int16x8_t s8 = vld1q_s16(s);                                       \
      s += src_stride;                                                   \
      int32x4_t lo, hi;                                                  \
      lo = cwp_highbd_convolve6_v_lo(s0, s1, s2, s3, s4, s5, f_lo, f_hi, \
                                     vert_offset);                       \
      hi = cwp_highbd_convolve6_v_hi(s0, s1, s2, s3, s4, s5, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                    \
      lo = cwp_highbd_convolve6_v_lo(s1, s2, s3, s4, s5, s6, f_lo, f_hi, \
                                     vert_offset);                       \
      hi = cwp_highbd_convolve6_v_hi(s1, s2, s3, s4, s5, s6, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                    \
      lo = cwp_highbd_convolve6_v_lo(s2, s3, s4, s5, s6, s7, f_lo, f_hi, \
                                     vert_offset);                       \
      hi = cwp_highbd_convolve6_v_hi(s2, s3, s4, s5, s6, s7, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                    \
      lo = cwp_highbd_convolve6_v_lo(s3, s4, s5, s6, s7, s8, f_lo, f_hi, \
                                     vert_offset);                       \
      hi = cwp_highbd_convolve6_v_hi(s3, s4, s5, s6, s7, s8, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                    \
      s0 = s4;                                                           \
      s1 = s5;                                                           \
      s2 = s6;                                                           \
      s3 = s7;                                                           \
      s4 = s8;                                                           \
      height -= 4;                                                       \
    } while (height > 0);                                                \
  } while (0)

  do {
    if (!do_average) {
      VERT6_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT6_8W_4ROWS(2);
    } else {
      VERT6_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT6_8W_4ROWS
}

static inline void cwp_highbd_convolve_2d_vert_4wide_6tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT6_4W_4ROWS(MODE)                                             \
  do {                                                                   \
    const int16_t *s = src;                                              \
    uint16_t *d = dst;                                                   \
    CONV_BUF_TYPE *d16 = dst16;                                          \
    int height = h;                                                      \
    int16x4_t s0 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s1 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s2 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s3 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s4 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    do {                                                                 \
      int16x4_t s5 = vld1_s16(s);                                        \
      s += src_stride;                                                   \
      int16x4_t s6 = vld1_s16(s);                                        \
      s += src_stride;                                                   \
      int16x4_t s7 = vld1_s16(s);                                        \
      s += src_stride;                                                   \
      int16x4_t s8 = vld1_s16(s);                                        \
      s += src_stride;                                                   \
      int32x4_t sum;                                                     \
      sum = cwp_highbd_convolve6_v_4(s0, s1, s2, s3, s4, s5, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                       \
      sum = cwp_highbd_convolve6_v_4(s1, s2, s3, s4, s5, s6, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                       \
      sum = cwp_highbd_convolve6_v_4(s2, s3, s4, s5, s6, s7, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                       \
      sum = cwp_highbd_convolve6_v_4(s3, s4, s5, s6, s7, s8, f_lo, f_hi, \
                                     vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                       \
      s0 = s4;                                                           \
      s1 = s5;                                                           \
      s2 = s6;                                                           \
      s3 = s7;                                                           \
      s4 = s8;                                                           \
      height -= 4;                                                       \
    } while (height > 0);                                                \
  } while (0)

  if (!do_average) {
    VERT6_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT6_4W_4ROWS(2);
  } else {
    VERT6_4W_4ROWS(1);
  }

#undef VERT6_4W_4ROWS
}

// Symmetric 6-tap vertical loop functions (widening add, safe for full s16
// range)

static inline void cwp_highbd_convolve_2d_vert_8wide_6tap_sym_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h, const int32x4_t f0,
    const int32x4_t f1, const int32x4_t f2, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT6S_8W_4ROWS(MODE)                                                \
  do {                                                                       \
    int height = h;                                                          \
    const int16_t *s = src;                                                  \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    int16x8_t s0 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s1 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s2 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s3 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s4 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    do {                                                                     \
      int16x8_t s5 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s6 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s7 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s8 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int32x4_t lo, hi;                                                      \
      lo = cwp_highbd_convolve6_sym_v_lo(s0, s1, s2, s3, s4, s5, f0, f1, f2, \
                                         vert_offset);                       \
      hi = cwp_highbd_convolve6_sym_v_hi(s0, s1, s2, s3, s4, s5, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve6_sym_v_lo(s1, s2, s3, s4, s5, s6, f0, f1, f2, \
                                         vert_offset);                       \
      hi = cwp_highbd_convolve6_sym_v_hi(s1, s2, s3, s4, s5, s6, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve6_sym_v_lo(s2, s3, s4, s5, s6, s7, f0, f1, f2, \
                                         vert_offset);                       \
      hi = cwp_highbd_convolve6_sym_v_hi(s2, s3, s4, s5, s6, s7, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      lo = cwp_highbd_convolve6_sym_v_lo(s3, s4, s5, s6, s7, s8, f0, f1, f2, \
                                         vert_offset);                       \
      hi = cwp_highbd_convolve6_sym_v_hi(s3, s4, s5, s6, s7, s8, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                        \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      s3 = s7;                                                               \
      s4 = s8;                                                               \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)

  do {
    if (!do_average) {
      VERT6S_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT6S_8W_4ROWS(2);
    } else {
      VERT6S_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT6S_8W_4ROWS
}

static inline void cwp_highbd_convolve_2d_vert_4wide_6tap_sym_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int32x4_t f0,
    const int32x4_t f1, const int32x4_t f2, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int32_t bck_offset, int bd,
    uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT6S_4W_4ROWS(MODE)                                                \
  do {                                                                       \
    const int16_t *s = src;                                                  \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    int height = h;                                                          \
    int16x4_t s0 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s1 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s2 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s3 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s4 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    do {                                                                     \
      int16x4_t s5 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s6 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s7 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s8 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int32x4_t sum;                                                         \
      sum = cwp_highbd_convolve6_sym_v_4(s0, s1, s2, s3, s4, s5, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve6_sym_v_4(s1, s2, s3, s4, s5, s6, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve6_sym_v_4(s2, s3, s4, s5, s6, s7, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      sum = cwp_highbd_convolve6_sym_v_4(s3, s4, s5, s6, s7, s8, f0, f1, f2, \
                                         vert_offset);                       \
      CWP_FINISH_ROW_4(sum, MODE);                                           \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      s3 = s7;                                                               \
      s4 = s8;                                                               \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)

  if (!do_average) {
    VERT6S_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT6S_4W_4ROWS(2);
  } else {
    VERT6S_4W_4ROWS(1);
  }

#undef VERT6S_4W_4ROWS
}

// 2-tap horizontal functions

static inline void cwp_highbd_convolve_2d_horiz_8wide_2tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x4_t f, const int32x4_t offset,
    const int32x4_t round_shift) {
  do {
    int width = w;
    const int16_t *s = (const int16_t *)src;
    int16_t *d = im;
    do {
      int16x8_t s0 = vld1q_s16(s + 0);
      int16x8_t s1 = vld1q_s16(s + 1);

      int32x4_t sum0 = vmlal_lane_s16(offset, vget_low_s16(s0), f, 0);
      sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f, 1);
      int32x4_t sum1 = vmlal_lane_s16(offset, vget_high_s16(s0), f, 0);
      sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f, 1);

      sum0 = vrshlq_s32(sum0, round_shift);
      sum1 = vrshlq_s32(sum1, round_shift);
      vst1q_s16(d, cwp_narrow_combine_s32(sum0, sum1));
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
    src += src_stride;
    im += im_stride;
  } while (--h > 0);
}

static inline void cwp_highbd_convolve_2d_horiz_4wide_2tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int h,
    const int16x4_t f, const int32x4_t offset, const int32x4_t round_shift) {
  // 2-row interleaving to hide load latency (2 loads/row x 2 rows = 4 loads).
  while (h >= 2) {
    const int16_t *s0_ptr = (const int16_t *)src;
    const int16_t *s1_ptr = (const int16_t *)(src + src_stride);
    // Row 0 loads
    int16x4_t r0_s0 = vld1_s16(s0_ptr + 0);
    int16x4_t r0_s1 = vld1_s16(s0_ptr + 1);
    // Row 1 loads (interleaved)
    int16x4_t r1_s0 = vld1_s16(s1_ptr + 0);
    int16x4_t r1_s1 = vld1_s16(s1_ptr + 1);
    // Compute both rows
    int32x4_t sum0 = vmlal_lane_s16(offset, r0_s0, f, 0);
    sum0 = vmlal_lane_s16(sum0, r0_s1, f, 1);
    int32x4_t sum1 = vmlal_lane_s16(offset, r1_s0, f, 0);
    sum1 = vmlal_lane_s16(sum1, r1_s1, f, 1);
    sum0 = vrshlq_s32(sum0, round_shift);
    sum1 = vrshlq_s32(sum1, round_shift);
    vst1_s16(im, vmovn_s32(sum0));
    vst1_s16(im + im_stride, vmovn_s32(sum1));
    src += 2 * src_stride;
    im += 2 * im_stride;
    h -= 2;
  }
  // Handle odd remaining row
  if (h > 0) {
    const int16_t *s = (const int16_t *)src;
    int16x4_t s0 = vld1_s16(s + 0);
    int16x4_t s1 = vld1_s16(s + 1);
    int32x4_t sum = vmlal_lane_s16(offset, s0, f, 0);
    sum = vmlal_lane_s16(sum, s1, f, 1);
    sum = vrshlq_s32(sum, round_shift);
    vst1_s16(im, vmovn_s32(sum));
  }
}

static inline void cwp_highbd_convolve_2d_vert_8wide_2tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h, const int16x4_t f,
    const int32x4_t vert_offset, const int32x4_t sub_const, int round_bits,
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset,
    int32_t bck_offset, int bd, uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT2_ROW_8(SA, SB, MODE)                                        \
  do {                                                                   \
    int32x4_t lo = vmlal_lane_s16(vert_offset, vget_low_s16(SA), f, 0);  \
    lo = vmlal_lane_s16(lo, vget_low_s16(SB), f, 1);                     \
    int32x4_t hi = vmlal_lane_s16(vert_offset, vget_high_s16(SA), f, 0); \
    hi = vmlal_lane_s16(hi, vget_high_s16(SB), f, 1);                    \
    CWP_FINISH_ROW_8(lo, hi, MODE);                                      \
  } while (0)

#define VERT2_8W_4ROWS(MODE)       \
  do {                             \
    int height = h;                \
    const int16_t *s = src;        \
    uint16_t *d = dst;             \
    CONV_BUF_TYPE *d16 = dst16;    \
    int16x8_t s0 = vld1q_s16(s);   \
    s += src_stride;               \
    do {                           \
      int16x8_t s1 = vld1q_s16(s); \
      s += src_stride;             \
      int16x8_t s2 = vld1q_s16(s); \
      s += src_stride;             \
      int16x8_t s3 = vld1q_s16(s); \
      s += src_stride;             \
      int16x8_t s4 = vld1q_s16(s); \
      s += src_stride;             \
      VERT2_ROW_8(s0, s1, MODE);   \
      VERT2_ROW_8(s1, s2, MODE);   \
      VERT2_ROW_8(s2, s3, MODE);   \
      VERT2_ROW_8(s3, s4, MODE);   \
      s0 = s4;                     \
      height -= 4;                 \
    } while (height > 0);          \
  } while (0)

  do {
    if (!do_average) {
      VERT2_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT2_8W_4ROWS(2);
    } else {
      VERT2_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT2_ROW_8
#undef VERT2_8W_4ROWS
}

static inline void cwp_highbd_convolve_2d_vert_4wide_2tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int16x4_t f,
    const int32x4_t vert_offset, const int32x4_t sub_const, int round_bits,
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset,
    int32_t bck_offset, int bd, uint16x4_t offset_u16) {
  (void)offset_u16;
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t fwd_u16 = vdup_n_u16((uint16_t)fwd_offset);
  const uint16x4_t bck_u16 = vdup_n_u16((uint16_t)bck_offset);

#define VERT2_ROW_4(SA, SB, MODE)                          \
  do {                                                     \
    int32x4_t sum = vmlal_lane_s16(vert_offset, SA, f, 0); \
    sum = vmlal_lane_s16(sum, SB, f, 1);                   \
    CWP_FINISH_ROW_4(sum, MODE);                           \
  } while (0)

#define VERT2_4W_4ROWS(MODE)      \
  do {                            \
    const int16_t *s = src;       \
    uint16_t *d = dst;            \
    CONV_BUF_TYPE *d16 = dst16;   \
    int height = h;               \
    int16x4_t s0 = vld1_s16(s);   \
    s += src_stride;              \
    do {                          \
      int16x4_t s1 = vld1_s16(s); \
      s += src_stride;            \
      int16x4_t s2 = vld1_s16(s); \
      s += src_stride;            \
      int16x4_t s3 = vld1_s16(s); \
      s += src_stride;            \
      int16x4_t s4 = vld1_s16(s); \
      s += src_stride;            \
      VERT2_ROW_4(s0, s1, MODE);  \
      VERT2_ROW_4(s1, s2, MODE);  \
      VERT2_ROW_4(s2, s3, MODE);  \
      VERT2_ROW_4(s3, s4, MODE);  \
      s0 = s4;                    \
      height -= 4;                \
    } while (height > 0);         \
  } while (0)

  if (!do_average) {
    VERT2_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT2_4W_4ROWS(2);
  } else {
    VERT2_4W_4ROWS(1);
  }

#undef VERT2_ROW_4
#undef VERT2_4W_4ROWS
}

void av2_highbd_cwp_convolve_2d_neon(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const int tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int tap_y = get_filter_tap(filter_params_y, subpel_y_qn);

  // Fall back to C for 12-tap filters
  if (tap_x == 12 || tap_y == 12) {
    av2_highbd_cwp_convolve_2d_c(src, src_stride, dst, dst_stride, w, h,
                                 filter_params_x, filter_params_y, subpel_x_qn,
                                 subpel_y_qn, conv_params, bd);
    return;
  }

  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  DECLARE_ALIGNED(16, int16_t,
                  im_block[(MAX_SB_SIZE + MAX_FILTER_TAP) * MAX_SB_SIZE]);
  const int im_h = h + tap_y - 1;
  const int im_stride = w;
  assert(w <= MAX_SB_SIZE && h <= MAX_SB_SIZE);
  assert(w == 4 || (w % 8) == 0);
  assert(h % 4 == 0);
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const int fo_vert = tap_y / 2 - 1;
  const int fo_horiz = tap_x / 2 - 1;

  const int16_t *x_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  const int16_t *y_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);

  // Horizontal pass constants
  const int32_t horiz_offset = 1 << (bd + FILTER_BITS - 1);
  const int32x4_t horiz_offset_v = vdupq_n_s32(horiz_offset);
  const int32x4_t horiz_round_shift = vdupq_n_s32(-conv_params->round_0);

  // Vertical pass constants
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int32_t vert_offset = 1 << offset_bits;
  const int32x4_t vert_offset_v = vdupq_n_s32(vert_offset);

  // Compound averaging constants
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  assert(round_bits >= 0);
  const int32_t sub_val = (1 << (offset_bits - conv_params->round_1)) +
                          (1 << (offset_bits - conv_params->round_1 - 1));
  const int32x4_t sub_const = vdupq_n_s32(sub_val);
  // For simple avg path: offset fits in uint16 for supported bd (10, 12).
  const uint16x4_t offset_u16 = vdup_n_u16((uint16_t)sub_val);

  const uint16_t *src_horiz = src - fo_vert * src_stride - fo_horiz;

  // Horizontal pass -- dispatch by tap count
  if (tap_x == 2) {
    const int16_t xf2_arr[4] = { x_filter_ptr[3], x_filter_ptr[4], 0, 0 };
    const int16x4_t xf2 = vld1_s16(xf2_arr);
    if (w == 4) {
      cwp_highbd_convolve_2d_horiz_4wide_2tap_neon(
          src_horiz, src_stride, im_block, im_stride, im_h, xf2, horiz_offset_v,
          horiz_round_shift);
    } else {
      cwp_highbd_convolve_2d_horiz_8wide_2tap_neon(
          src_horiz, src_stride, im_block, im_stride, w, im_h, xf2,
          horiz_offset_v, horiz_round_shift);
    }
  } else if (tap_x == 4) {
    // 4-tap: coefficients in filter[2..5]
    const int16x4_t xf4 = vld1_s16(x_filter_ptr + 2);
    if (w == 4) {
      cwp_highbd_convolve_2d_horiz_4wide_4tap_neon(
          src_horiz, src_stride, im_block, im_stride, im_h, xf4, horiz_offset_v,
          horiz_round_shift);
    } else {
      cwp_highbd_convolve_2d_horiz_8wide_4tap_neon(
          src_horiz, src_stride, im_block, im_stride, w, im_h, xf4,
          horiz_offset_v, horiz_round_shift);
    }
  } else if (tap_x == 6) {
    assert(w != 4);
    const int x_sym = (x_filter_ptr[1] == x_filter_ptr[6]) &&
                      (x_filter_ptr[2] == x_filter_ptr[5]) &&
                      (x_filter_ptr[3] == x_filter_ptr[4]);
    if (x_sym) {
      const int16_t xf_sym_arr[4] = { x_filter_ptr[1], x_filter_ptr[2],
                                      x_filter_ptr[3], 0 };
      const int16x4_t xf_sym = vld1_s16(xf_sym_arr);
      cwp_highbd_convolve_2d_horiz_8wide_6tap_sym_neon(
          src_horiz, src_stride, im_block, im_stride, w, im_h, xf_sym,
          horiz_offset_v, horiz_round_shift);
    } else {
      const int16x4_t xf6_lo = vld1_s16(x_filter_ptr + 1);
      const int16_t xf6_hi_arr[4] = { x_filter_ptr[5], x_filter_ptr[6], 0, 0 };
      const int16x4_t xf6_hi = vld1_s16(xf6_hi_arr);
      cwp_highbd_convolve_2d_horiz_8wide_6tap_neon(
          src_horiz, src_stride, im_block, im_stride, w, im_h, xf6_lo, xf6_hi,
          horiz_offset_v, horiz_round_shift);
    }
  } else {
    // 8-tap
    assert(w != 4);
    const int16x8_t x_filter = vld1q_s16(x_filter_ptr);
    cwp_highbd_convolve_2d_horiz_8wide_neon(src_horiz, src_stride, im_block,
                                            im_stride, w, im_h, x_filter,
                                            horiz_offset_v, horiz_round_shift);
  }

  // Vertical pass -- dispatch by tap count
  if (tap_y == 2) {
    const int16_t yf2_arr[4] = { y_filter_ptr[3], y_filter_ptr[4], 0, 0 };
    const int16x4_t yf2 = vld1_s16(yf2_arr);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_2tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h, yf2,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, conv_params->bck_offset, bd, offset_u16);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_2tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h, yf2,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, conv_params->bck_offset, bd, offset_u16);
    }
  } else if (tap_y == 4) {
    // 4-tap: coefficients in filter[2..5]
    const int16x4_t yf4 = vld1_s16(y_filter_ptr + 2);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_4tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h, yf4,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, conv_params->bck_offset, bd, offset_u16);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_4tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h, yf4,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, conv_params->bck_offset, bd, offset_u16);
    }
  } else if (tap_y == 6) {
    const int y_sym = (y_filter_ptr[1] == y_filter_ptr[6]) &&
                      (y_filter_ptr[2] == y_filter_ptr[5]) &&
                      (y_filter_ptr[3] == y_filter_ptr[4]);
    if (y_sym) {
      // Symmetric 6-tap: fold pairs with widening add (safe for full s16 range)
      const int32x4_t yf_s0 = vdupq_n_s32((int32_t)y_filter_ptr[1]);
      const int32x4_t yf_s1 = vdupq_n_s32((int32_t)y_filter_ptr[2]);
      const int32x4_t yf_s2 = vdupq_n_s32((int32_t)y_filter_ptr[3]);
      if (w == 4) {
        cwp_highbd_convolve_2d_vert_4wide_6tap_sym_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h, yf_s0,
            yf_s1, yf_s2, vert_offset_v, sub_const, round_bits, do_average,
            use_wtd_comp_avg, conv_params->fwd_offset, conv_params->bck_offset,
            bd, offset_u16);
      } else {
        cwp_highbd_convolve_2d_vert_8wide_6tap_sym_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
            yf_s0, yf_s1, yf_s2, vert_offset_v, sub_const, round_bits,
            do_average, use_wtd_comp_avg, conv_params->fwd_offset,
            conv_params->bck_offset, bd, offset_u16);
      }
    } else {
      const int16x4_t yf6_lo = vld1_s16(y_filter_ptr + 1);
      const int16_t yf6_hi_arr[4] = { y_filter_ptr[5], y_filter_ptr[6], 0, 0 };
      const int16x4_t yf6_hi = vld1_s16(yf6_hi_arr);
      if (w == 4) {
        cwp_highbd_convolve_2d_vert_4wide_6tap_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h,
            yf6_lo, yf6_hi, vert_offset_v, sub_const, round_bits, do_average,
            use_wtd_comp_avg, conv_params->fwd_offset, conv_params->bck_offset,
            bd, offset_u16);
      } else {
        cwp_highbd_convolve_2d_vert_8wide_6tap_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
            yf6_lo, yf6_hi, vert_offset_v, sub_const, round_bits, do_average,
            use_wtd_comp_avg, conv_params->fwd_offset, conv_params->bck_offset,
            bd, offset_u16);
      }
    }
  } else {
    // 8-tap
    const int16x8_t y_filter = vld1q_s16(y_filter_ptr);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h,
          y_filter, vert_offset_v, sub_const, round_bits, do_average,
          use_wtd_comp_avg, conv_params->fwd_offset, conv_params->bck_offset,
          bd, offset_u16);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
          y_filter, vert_offset_v, sub_const, round_bits, do_average,
          use_wtd_comp_avg, conv_params->fwd_offset, conv_params->bck_offset,
          bd, offset_u16);
    }
  }
}
