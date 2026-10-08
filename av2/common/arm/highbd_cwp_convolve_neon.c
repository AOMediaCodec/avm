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

// CWP supports up to 12-tap filters; MAX_FILTER_TAP (filter.h) is 8.
#define CWP_MAX_FILTER_TAP 12

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

// 12-tap horizontal filter helper for 2D path.
// 12 coefficients in three int16x4_t registers (f0, f1, f2).

static inline int16x8_t cwp_highbd_convolve12_8_2d_h(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t s8,
    const int16x8_t s9, const int16x8_t s10, const int16x8_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset, const int32x4_t round_shift) {
  int32x4_t sum0 = vmlal_lane_s16(offset, vget_low_s16(s0), f0, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f0, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f0, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f0, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f1, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f1, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s6), f1, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s7), f1, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s8), f2, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s9), f2, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s10), f2, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s11), f2, 3);

  int32x4_t sum1 = vmlal_lane_s16(offset, vget_high_s16(s0), f0, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f0, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f0, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f0, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f1, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f1, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s6), f1, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s7), f1, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s8), f2, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s9), f2, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s10), f2, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s11), f2, 3);

  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return cwp_narrow_combine_s32(sum0, sum1);
}

// 12-tap horizontal loop function for 2D path, w >= 8.
static inline void cwp_highbd_convolve_2d_horiz_8wide_12tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset, const int32x4_t round_shift) {
  do {
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
      int16x8_t s8 = vld1q_s16(s + 8);
      int16x8_t s9 = vld1q_s16(s + 9);
      int16x8_t s10 = vld1q_s16(s + 10);
      int16x8_t s11 = vld1q_s16(s + 11);
      int16x8_t r = cwp_highbd_convolve12_8_2d_h(s0, s1, s2, s3, s4, s5, s6, s7,
                                                 s8, s9, s10, s11, f0, f1, f2,
                                                 offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
    src += src_stride;
    im += im_stride;
    h--;
  } while (h > 0);
}

// 12-tap horizontal loop function for 2D path, w == 4.
static inline void cwp_highbd_convolve_2d_horiz_4wide_12tap_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int h,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset, const int32x4_t round_shift) {
  do {
    const int16_t *s = (const int16_t *)src;
    int16x4_t s0 = vld1_s16(s + 0);
    int16x4_t s1 = vld1_s16(s + 1);
    int16x4_t s2 = vld1_s16(s + 2);
    int16x4_t s3 = vld1_s16(s + 3);
    int16x4_t s4 = vld1_s16(s + 4);
    int16x4_t s5 = vld1_s16(s + 5);
    int16x4_t s6 = vld1_s16(s + 6);
    int16x4_t s7 = vld1_s16(s + 7);
    int16x4_t s8 = vld1_s16(s + 8);
    int16x4_t s9 = vld1_s16(s + 9);
    int16x4_t s10 = vld1_s16(s + 10);
    int16x4_t s11 = vld1_s16(s + 11);

    int32x4_t sum = vmlal_lane_s16(offset, s0, f0, 0);
    sum = vmlal_lane_s16(sum, s1, f0, 1);
    sum = vmlal_lane_s16(sum, s2, f0, 2);
    sum = vmlal_lane_s16(sum, s3, f0, 3);
    sum = vmlal_lane_s16(sum, s4, f1, 0);
    sum = vmlal_lane_s16(sum, s5, f1, 1);
    sum = vmlal_lane_s16(sum, s6, f1, 2);
    sum = vmlal_lane_s16(sum, s7, f1, 3);
    sum = vmlal_lane_s16(sum, s8, f2, 0);
    sum = vmlal_lane_s16(sum, s9, f2, 1);
    sum = vmlal_lane_s16(sum, s10, f2, 2);
    sum = vmlal_lane_s16(sum, s11, f2, 3);
    sum = vrshlq_s32(sum, round_shift);
    vst1_s16(im, vmovn_s32(sum));
    src += src_stride;
    im += im_stride;
    h--;
  } while (h > 0);
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
    int32x4_t vert_sum, const CONV_BUF_TYPE *dst16, int use_wtd_comp_avg,
    const int32x4_t sub_const, int round_bits, uint16x4_t max_val,
    int32x4_t fwd_s32) {
  uint16x4_t d16_raw = vld1_u16(dst16);
  int32x4_t tmp;

  if (use_wtd_comp_avg) {
    // Since fwd + bck == 1 << DIST_PRECISION_BITS (always 16 for CWP/TIP):
    //   d16*fwd + res*bck = (d16-res)*fwd + res*16
    //   >> 4 gives: ((d16-res)*fwd) >> 4 + res
    // One multiply instead of two, halving multiply-unit pressure.
    int32x4_t res_s32 = vrshrq_n_s32(vert_sum, COMPOUND_ROUND1_BITS);
    int32x4_t d16_s32 = vreinterpretq_s32_u32(vmovl_u16(d16_raw));
    int32x4_t diff = vsubq_s32(d16_s32, res_s32);
    tmp = vaddq_s32(vshrq_n_s32(vmulq_s32(diff, fwd_s32), DIST_PRECISION_BITS),
                    res_s32);
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
    int use_wtd_comp_avg, const int32x4_t sub_const, int round_bits,
    uint16x8_t max_val, int32x4_t fwd_s32) {
  uint16x8_t d16_raw = vld1q_u16(dst16);
  int32x4_t tmp_lo, tmp_hi;

  if (use_wtd_comp_avg) {
    // Since fwd + bck == 1 << DIST_PRECISION_BITS (always 16 for CWP/TIP):
    //   d16*fwd + res*bck = (d16-res)*fwd + res*16
    //   >> 4 gives: ((d16-res)*fwd) >> 4 + res
    // One multiply per half instead of two, halving multiply-unit pressure.
    int32x4_t res_lo = vrshrq_n_s32(vert_lo, COMPOUND_ROUND1_BITS);
    int32x4_t res_hi = vrshrq_n_s32(vert_hi, COMPOUND_ROUND1_BITS);
    int32x4_t d16_lo = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(d16_raw)));
    int32x4_t d16_hi = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(d16_raw)));
    int32x4_t diff_lo = vsubq_s32(d16_lo, res_lo);
    int32x4_t diff_hi = vsubq_s32(d16_hi, res_hi);
    tmp_lo = vaddq_s32(
        vshrq_n_s32(vmulq_s32(diff_lo, fwd_s32), DIST_PRECISION_BITS), res_lo);
    tmp_hi = vaddq_s32(
        vshrq_n_s32(vmulq_s32(diff_hi, fwd_s32), DIST_PRECISION_BITS), res_hi);
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
      uint16x8_t _r8 =                                                     \
          cwp_compound_avg_clip_8((lo), (hi), d16, (MODE) == 2, sub_const, \
                                  round_bits, max_val, fwd_s32);           \
      vst1q_u16(d, _r8);                                                   \
    }                                                                      \
    d += dst_stride;                                                       \
    d16 += dst16_stride;                                                   \
  } while (0)

#define CWP_FINISH_ROW_4(sum, MODE)                                          \
  do {                                                                       \
    if ((MODE) == 0) {                                                       \
      cwp_store_dst16_4((sum), d16);                                         \
    } else {                                                                 \
      uint16x4_t _r4 = cwp_compound_avg_clip_4(                              \
          (sum), d16, (MODE) == 2, sub_const, round_bits, max_val, fwd_s32); \
      vst1_u16(d, _r4);                                                      \
    }                                                                        \
    d += dst_stride;                                                         \
    d16 += dst16_stride;                                                     \
  } while (0)

static inline void cwp_highbd_convolve_2d_vert_8wide_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h,
    const int16x8_t y_filter, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);
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
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);
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

// 12-tap vertical filter helpers for 2D path

static inline int32x4_t cwp_highbd_convolve12_v_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t s8,
    const int16x8_t s9, const int16x8_t s10, const int16x8_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_low_s16(s0), f0, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f0, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f0, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f0, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f1, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f1, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s6), f1, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s7), f1, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s8), f2, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s9), f2, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s10), f2, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s11), f2, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve12_v_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t s8,
    const int16x8_t s9, const int16x8_t s10, const int16x8_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, vget_high_s16(s0), f0, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f0, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f0, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f0, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f1, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f1, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s6), f1, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s7), f1, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s8), f2, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s9), f2, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s10), f2, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s11), f2, 3);
  return sum;
}

static inline int32x4_t cwp_highbd_convolve12_v_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t s8,
    const int16x4_t s9, const int16x4_t s10, const int16x4_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2,
    const int32x4_t offset) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, f0, 0);
  sum = vmlal_lane_s16(sum, s1, f0, 1);
  sum = vmlal_lane_s16(sum, s2, f0, 2);
  sum = vmlal_lane_s16(sum, s3, f0, 3);
  sum = vmlal_lane_s16(sum, s4, f1, 0);
  sum = vmlal_lane_s16(sum, s5, f1, 1);
  sum = vmlal_lane_s16(sum, s6, f1, 2);
  sum = vmlal_lane_s16(sum, s7, f1, 3);
  sum = vmlal_lane_s16(sum, s8, f2, 0);
  sum = vmlal_lane_s16(sum, s9, f2, 1);
  sum = vmlal_lane_s16(sum, s10, f2, 2);
  sum = vmlal_lane_s16(sum, s11, f2, 3);
  return sum;
}

// 12-tap vertical loop function for 2D path, w >= 8.
static inline void cwp_highbd_convolve_2d_vert_8wide_12tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h, const int16x4_t f0,
    const int16x4_t f1, const int16x4_t f2, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

#define VERT12_8W_4ROWS(MODE)                                                  \
  do {                                                                         \
    int height = h;                                                            \
    const int16_t *s = src;                                                    \
    uint16_t *d = dst;                                                         \
    CONV_BUF_TYPE *d16 = dst16;                                                \
    int16x8_t s0 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s1 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s2 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s3 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s4 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s5 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s6 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s7 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s8 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s9 = vld1q_s16(s);                                               \
    s += src_stride;                                                           \
    int16x8_t s10 = vld1q_s16(s);                                              \
    s += src_stride;                                                           \
    do {                                                                       \
      int16x8_t s11 = vld1q_s16(s);                                            \
      s += src_stride;                                                         \
      int16x8_t s12 = vld1q_s16(s);                                            \
      s += src_stride;                                                         \
      int16x8_t s13 = vld1q_s16(s);                                            \
      s += src_stride;                                                         \
      int16x8_t s14 = vld1q_s16(s);                                            \
      s += src_stride;                                                         \
      int32x4_t lo, hi;                                                        \
      lo = cwp_highbd_convolve12_v_lo(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9,  \
                                      s10, s11, f0, f1, f2, vert_offset);      \
      hi = cwp_highbd_convolve12_v_hi(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9,  \
                                      s10, s11, f0, f1, f2, vert_offset);      \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                          \
      lo = cwp_highbd_convolve12_v_lo(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, \
                                      s11, s12, f0, f1, f2, vert_offset);      \
      hi = cwp_highbd_convolve12_v_hi(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, \
                                      s11, s12, f0, f1, f2, vert_offset);      \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                          \
      lo = cwp_highbd_convolve12_v_lo(s2, s3, s4, s5, s6, s7, s8, s9, s10,     \
                                      s11, s12, s13, f0, f1, f2, vert_offset); \
      hi = cwp_highbd_convolve12_v_hi(s2, s3, s4, s5, s6, s7, s8, s9, s10,     \
                                      s11, s12, s13, f0, f1, f2, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                          \
      lo = cwp_highbd_convolve12_v_lo(s3, s4, s5, s6, s7, s8, s9, s10, s11,    \
                                      s12, s13, s14, f0, f1, f2, vert_offset); \
      hi = cwp_highbd_convolve12_v_hi(s3, s4, s5, s6, s7, s8, s9, s10, s11,    \
                                      s12, s13, s14, f0, f1, f2, vert_offset); \
      CWP_FINISH_ROW_8(lo, hi, MODE);                                          \
      s0 = s4;                                                                 \
      s1 = s5;                                                                 \
      s2 = s6;                                                                 \
      s3 = s7;                                                                 \
      s4 = s8;                                                                 \
      s5 = s9;                                                                 \
      s6 = s10;                                                                \
      s7 = s11;                                                                \
      s8 = s12;                                                                \
      s9 = s13;                                                                \
      s10 = s14;                                                               \
      height -= 4;                                                             \
    } while (height > 0);                                                      \
  } while (0)

  do {
    if (!do_average) {
      VERT12_8W_4ROWS(0);
    } else if (use_wtd_comp_avg) {
      VERT12_8W_4ROWS(2);
    } else {
      VERT12_8W_4ROWS(1);
    }
    src += 8;
    dst += 8;
    dst16 += 8;
    w -= 8;
  } while (w > 0);

#undef VERT12_8W_4ROWS
}

// 12-tap vertical loop function for 2D path, w == 4.
static inline void cwp_highbd_convolve_2d_vert_4wide_12tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int h, const int16x4_t f0,
    const int16x4_t f1, const int16x4_t f2, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

#define VERT12_4W_4ROWS(MODE)                                                  \
  do {                                                                         \
    const int16_t *s = src;                                                    \
    uint16_t *d = dst;                                                         \
    CONV_BUF_TYPE *d16 = dst16;                                                \
    int height = h;                                                            \
    int16x4_t s0 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s1 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s2 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s3 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s4 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s5 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s6 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s7 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s8 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s9 = vld1_s16(s);                                                \
    s += src_stride;                                                           \
    int16x4_t s10 = vld1_s16(s);                                               \
    s += src_stride;                                                           \
    do {                                                                       \
      int16x4_t s11 = vld1_s16(s);                                             \
      s += src_stride;                                                         \
      int16x4_t s12 = vld1_s16(s);                                             \
      s += src_stride;                                                         \
      int16x4_t s13 = vld1_s16(s);                                             \
      s += src_stride;                                                         \
      int16x4_t s14 = vld1_s16(s);                                             \
      s += src_stride;                                                         \
      int32x4_t sum;                                                           \
      sum = cwp_highbd_convolve12_v_4(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9,  \
                                      s10, s11, f0, f1, f2, vert_offset);      \
      CWP_FINISH_ROW_4(sum, MODE);                                             \
      sum = cwp_highbd_convolve12_v_4(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, \
                                      s11, s12, f0, f1, f2, vert_offset);      \
      CWP_FINISH_ROW_4(sum, MODE);                                             \
      sum = cwp_highbd_convolve12_v_4(s2, s3, s4, s5, s6, s7, s8, s9, s10,     \
                                      s11, s12, s13, f0, f1, f2, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                             \
      sum = cwp_highbd_convolve12_v_4(s3, s4, s5, s6, s7, s8, s9, s10, s11,    \
                                      s12, s13, s14, f0, f1, f2, vert_offset); \
      CWP_FINISH_ROW_4(sum, MODE);                                             \
      s0 = s4;                                                                 \
      s1 = s5;                                                                 \
      s2 = s6;                                                                 \
      s3 = s7;                                                                 \
      s4 = s8;                                                                 \
      s5 = s9;                                                                 \
      s6 = s10;                                                                \
      s7 = s11;                                                                \
      s8 = s12;                                                                \
      s9 = s13;                                                                \
      s10 = s14;                                                               \
      height -= 4;                                                             \
    } while (height > 0);                                                      \
  } while (0)

  if (!do_average) {
    VERT12_4W_4ROWS(0);
  } else if (use_wtd_comp_avg) {
    VERT12_4W_4ROWS(2);
  } else {
    VERT12_4W_4ROWS(1);
  }

#undef VERT12_4W_4ROWS
}

// 4-tap vertical loop functions

static inline void cwp_highbd_convolve_2d_vert_8wide_4tap_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    CONV_BUF_TYPE *dst16, int dst16_stride, int w, int h,
    const int16x4_t y_filter, const int32x4_t vert_offset,
    const int32x4_t sub_const, int round_bits, int do_average,
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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
    int do_average, int use_wtd_comp_avg, int32_t fwd_offset, int bd) {
  const uint16x4_t max_val = vdup_n_u16((uint16_t)((1 << bd) - 1));
  const int32x4_t fwd_s32 = vdupq_n_s32(fwd_offset);

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

  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  DECLARE_ALIGNED(16, int16_t,
                  im_block[(MAX_SB_SIZE + CWP_MAX_FILTER_TAP) * MAX_SB_SIZE]);
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
  } else if (tap_x == 12) {
    const int16x4_t xf12_0 = vld1_s16(x_filter_ptr + 0);
    const int16x4_t xf12_1 = vld1_s16(x_filter_ptr + 4);
    const int16x4_t xf12_2 = vld1_s16(x_filter_ptr + 8);
    if (w == 4) {
      cwp_highbd_convolve_2d_horiz_4wide_12tap_neon(
          src_horiz, src_stride, im_block, im_stride, im_h, xf12_0, xf12_1,
          xf12_2, horiz_offset_v, horiz_round_shift);
    } else {
      cwp_highbd_convolve_2d_horiz_8wide_12tap_neon(
          src_horiz, src_stride, im_block, im_stride, w, im_h, xf12_0, xf12_1,
          xf12_2, horiz_offset_v, horiz_round_shift);
    }
  } else {
    // 8-tap
    assert(tap_x == 8);
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
          conv_params->fwd_offset, bd);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_2tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h, yf2,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, bd);
    }
  } else if (tap_y == 4) {
    // 4-tap: coefficients in filter[2..5]
    const int16x4_t yf4 = vld1_s16(y_filter_ptr + 2);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_4tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h, yf4,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, bd);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_4tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h, yf4,
          vert_offset_v, sub_const, round_bits, do_average, use_wtd_comp_avg,
          conv_params->fwd_offset, bd);
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
            use_wtd_comp_avg, conv_params->fwd_offset, bd);
      } else {
        cwp_highbd_convolve_2d_vert_8wide_6tap_sym_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
            yf_s0, yf_s1, yf_s2, vert_offset_v, sub_const, round_bits,
            do_average, use_wtd_comp_avg, conv_params->fwd_offset, bd);
      }
    } else {
      const int16x4_t yf6_lo = vld1_s16(y_filter_ptr + 1);
      const int16_t yf6_hi_arr[4] = { y_filter_ptr[5], y_filter_ptr[6], 0, 0 };
      const int16x4_t yf6_hi = vld1_s16(yf6_hi_arr);
      if (w == 4) {
        cwp_highbd_convolve_2d_vert_4wide_6tap_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h,
            yf6_lo, yf6_hi, vert_offset_v, sub_const, round_bits, do_average,
            use_wtd_comp_avg, conv_params->fwd_offset, bd);
      } else {
        cwp_highbd_convolve_2d_vert_8wide_6tap_neon(
            im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
            yf6_lo, yf6_hi, vert_offset_v, sub_const, round_bits, do_average,
            use_wtd_comp_avg, conv_params->fwd_offset, bd);
      }
    }
  } else if (tap_y == 12) {
    const int16x4_t yf12_0 = vld1_s16(y_filter_ptr + 0);
    const int16x4_t yf12_1 = vld1_s16(y_filter_ptr + 4);
    const int16x4_t yf12_2 = vld1_s16(y_filter_ptr + 8);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_12tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h, yf12_0,
          yf12_1, yf12_2, vert_offset_v, sub_const, round_bits, do_average,
          use_wtd_comp_avg, conv_params->fwd_offset, bd);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_12tap_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
          yf12_0, yf12_1, yf12_2, vert_offset_v, sub_const, round_bits,
          do_average, use_wtd_comp_avg, conv_params->fwd_offset, bd);
    }
  } else {
    // 8-tap
    assert(tap_y == 8);
    const int16x8_t y_filter = vld1q_s16(y_filter_ptr);
    if (w == 4) {
      cwp_highbd_convolve_2d_vert_4wide_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, h,
          y_filter, vert_offset_v, sub_const, round_bits, do_average,
          use_wtd_comp_avg, conv_params->fwd_offset, bd);
    } else {
      cwp_highbd_convolve_2d_vert_8wide_neon(
          im_block, im_stride, dst, dst_stride, dst16, dst16_stride, w, h,
          y_filter, vert_offset_v, sub_const, round_bits, do_average,
          use_wtd_comp_avg, conv_params->fwd_offset, bd);
    }
  }
}

// ---------------------------------------------------------------------------
// Single-pass compound blend helpers for 2d_copy / convolve_x / convolve_y.
//
// Unlike the 2D path where the vertical filter produces int32 values that
// still need COMPOUND_ROUND1_BITS rounding, these single-pass variants
// produce values already at CONV_BUF_TYPE (uint16) precision. The compound
// blend operates on int32 "res" values = (filtered << bits) + round_offset.
// ---------------------------------------------------------------------------

// MODE: 0 = store to dst16, 1 = simple avg, 2 = weighted avg
// RBITS: compile-time round_bits (4 for bd=10, 2 for bd=12, 0 = use runtime).
// When RBITS is a literal, the compiler dead-code-eliminates unused branches
// and vqrshrun_n_s32 gets a compile-time immediate. This eliminates 2 cmp +
// 2 branch instructions per 8-pixel group in the inner width loop, reducing
// CyUnitI pressure (the iter-0 bottleneck at 69.4).
#define CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS)                      \
  do {                                                                        \
    if ((MODE) == 0) {                                                        \
      vst1q_u16(d16, vcombine_u16(vqmovun_s32(res_lo), vqmovun_s32(res_hi))); \
    } else {                                                                  \
      uint16x8_t d16_raw = vld1q_u16(d16);                                    \
      int32x4_t tmp_lo, tmp_hi;                                               \
      if ((MODE) == 2) {                                                      \
        int32x4_t d_lo =                                                      \
            vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(d16_raw)));          \
        int32x4_t d_hi =                                                      \
            vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(d16_raw)));         \
        tmp_lo = vaddq_s32(                                                   \
            vshrq_n_s32(vmulq_s32(vsubq_s32(d_lo, (res_lo)), fwd_s32),        \
                        DIST_PRECISION_BITS),                                 \
            (res_lo));                                                        \
        tmp_hi = vaddq_s32(                                                   \
            vshrq_n_s32(vmulq_s32(vsubq_s32(d_hi, (res_hi)), fwd_s32),        \
                        DIST_PRECISION_BITS),                                 \
            (res_hi));                                                        \
        tmp_lo = vsubq_s32(tmp_lo, sub_const);                                \
        tmp_hi = vsubq_s32(tmp_hi, sub_const);                                \
      } else {                                                                \
        /* s32 halving add: skip narrow-to-u16/widen-back cycle. */           \
        /* res values are biased by round_offset (~24576 for bd=10) so */     \
        /* always non-negative and in u16 range; s32 hadd matches u16. */     \
        int32x4_t d_lo =                                                      \
            vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(d16_raw)));          \
        int32x4_t d_hi =                                                      \
            vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(d16_raw)));         \
        tmp_lo = vhaddq_s32(d_lo, (res_lo));                                  \
        tmp_hi = vhaddq_s32(d_hi, (res_hi));                                  \
      }                                                                       \
      uint16x8_t r8;                                                          \
      if ((MODE) == 1 && (RBITS) == 4) {                                      \
        /* Post-narrow sub: 2x vsubq_s32 -> 1x vqsubq_u16. */                 \
        /* vqrshrun(x-C,N) = vqsub(vqrshrun(x,N),C>>N) when C%(1<<N)==0. */   \
        r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 4),                          \
                          vqrshrun_n_s32(tmp_hi, 4));                         \
        r8 = vqsubq_u16(r8, sub_shr4);                                        \
      } else if ((MODE) == 1 && (RBITS) == 2) {                               \
        r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 2),                          \
                          vqrshrun_n_s32(tmp_hi, 2));                         \
        r8 = vqsubq_u16(r8, sub_shr2);                                        \
      } else if ((RBITS) == 4) {                                              \
        r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 4),                          \
                          vqrshrun_n_s32(tmp_hi, 4));                         \
      } else if ((RBITS) == 2) {                                              \
        r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 2),                          \
                          vqrshrun_n_s32(tmp_hi, 2));                         \
      } else {                                                                \
        if ((MODE) == 1) {                                                    \
          tmp_lo = vsubq_s32(tmp_lo, sub_const);                              \
          tmp_hi = vsubq_s32(tmp_hi, sub_const);                              \
        }                                                                     \
        int32x4_t rnd = vdupq_n_s32((1 << round_bits) >> 1);                  \
        int32x4_t neg_rb = vdupq_n_s32(-round_bits);                          \
        r8 = vcombine_u16(                                                    \
            vqmovun_s32(vshlq_s32(vaddq_s32(tmp_lo, rnd), neg_rb)),           \
            vqmovun_s32(vshlq_s32(vaddq_s32(tmp_hi, rnd), neg_rb)));          \
      }                                                                       \
      r8 = vminq_u16(r8, max_val);                                            \
      vst1q_u16(d, r8);                                                       \
    }                                                                         \
    d += dst_stride;                                                          \
    d16 += dst16_stride;                                                      \
  } while (0)

#define CWP_1D_FINISH_ROW_4(res_s32, MODE, RBITS)                        \
  do {                                                                   \
    if ((MODE) == 0) {                                                   \
      vst1_u16(d16, vqmovun_s32(res_s32));                               \
    } else {                                                             \
      uint16x4_t d16_raw = vld1_u16(d16);                                \
      int32x4_t tmp;                                                     \
      if ((MODE) == 2) {                                                 \
        int32x4_t d_s32 = vreinterpretq_s32_u32(vmovl_u16(d16_raw));     \
        tmp = vaddq_s32(                                                 \
            vshrq_n_s32(vmulq_s32(vsubq_s32(d_s32, (res_s32)), fwd_s32), \
                        DIST_PRECISION_BITS),                            \
            (res_s32));                                                  \
        tmp = vsubq_s32(tmp, sub_const);                                 \
      } else {                                                           \
        /* s32 halving add: skip narrow-to-u16/widen-back cycle. */      \
        int32x4_t d_s32 = vreinterpretq_s32_u32(vmovl_u16(d16_raw));     \
        tmp = vhaddq_s32(d_s32, (res_s32));                              \
      }                                                                  \
      uint16x4_t r4;                                                     \
      if ((MODE) == 1 && (RBITS) == 4) {                                 \
        /* Post-narrow sub: 1x vsubq_s32 -> 1x vqsub_u16. */             \
        r4 = vqrshrun_n_s32(tmp, 4);                                     \
        r4 = vqsub_u16(r4, sub_shr4_4);                                  \
      } else if ((MODE) == 1 && (RBITS) == 2) {                          \
        r4 = vqrshrun_n_s32(tmp, 2);                                     \
        r4 = vqsub_u16(r4, sub_shr2_4);                                  \
      } else if ((RBITS) == 4) {                                         \
        r4 = vqrshrun_n_s32(tmp, 4);                                     \
      } else if ((RBITS) == 2) {                                         \
        r4 = vqrshrun_n_s32(tmp, 2);                                     \
      } else {                                                           \
        if ((MODE) == 1) {                                               \
          tmp = vsubq_s32(tmp, sub_const);                               \
        }                                                                \
        int32x4_t rnd = vdupq_n_s32((1 << round_bits) >> 1);             \
        int32x4_t neg_rb = vdupq_n_s32(-round_bits);                     \
        r4 = vqmovun_s32(vshlq_s32(vaddq_s32(tmp, rnd), neg_rb));        \
      }                                                                  \
      r4 = vmin_u16(r4, max_val4);                                       \
      vst1_u16(d, r4);                                                   \
    }                                                                    \
    d += dst_stride;                                                     \
    d16 += dst16_stride;                                                 \
  } while (0)

// ===========================================================================
// av2_highbd_cwp_convolve_2d_copy_neon -- no filter, shift + offset + blend
// ===========================================================================

void av2_highbd_cwp_convolve_2d_copy_neon(const uint16_t *src, int src_stride,
                                          uint16_t *dst, int dst_stride, int w,
                                          int h, ConvolveParams *conv_params,
                                          int bd) {
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  const int dst16_stride = conv_params->dst_stride;
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  const int bits =
      FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int round_bits = bits;

  assert(bits >= 0);
  assert(bits <= 4);
  assert(w == 4 || (w % 8) == 0);

  const int32x4_t fwd_s32 = vdupq_n_s32(conv_params->fwd_offset);
  const int32x4_t sub_const = vdupq_n_s32(round_offset);
  const int32x4_t offset_v = vdupq_n_s32(round_offset);

  // For the no-average path, val = src << bits + round_offset.
  // Since bits <= 4 and src <= (1<<bd)-1, the result fits in uint16 when
  // round_offset is small enough. Use the u16 fast path when possible.
  const uint16x8_t offset_u16 = vdupq_n_u16((uint16_t)round_offset);

  if (!do_average) {
    // Store path: val = (src << bits) + round_offset. Stays in u16.
    if (w == 4) {
      for (int y = 0; y < h; y++) {
        uint16x4_t s = vld1_u16(src);
        uint32x4_t wide = vmovl_u16(s);
        wide = vshlq_u32(wide, vdupq_n_s32(bits));
        wide = vaddq_u32(wide, vreinterpretq_u32_s32(offset_v));
        vst1_u16(dst16, vmovn_u32(wide));
        src += src_stride;
        dst16 += dst16_stride;
      }
    } else {
      for (int y = 0; y < h; y++) {
        int x = 0;
        do {
          uint16x8_t s = vld1q_u16(src + x);
          // Shift left by bits then add offset. Since bits <= 4 and
          // src is at most 12-bit, the result fits in u16 after offset.
          uint16x8_t res = vshlq_u16(s, vdupq_n_s16((int16_t)bits));
          res = vaddq_u16(res, offset_u16);
          vst1q_u16(dst16 + x, res);
          x += 8;
        } while (x < w);
        src += src_stride;
        dst16 += dst16_stride;
      }
    }
  } else {
    // Average path: need compound blend
    const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
    const uint16x4_t max_val4 = vdup_n_u16((uint16_t)((1 << bd) - 1));
    // Post-narrow sub vectors for CWP_1D_FINISH_ROW MODE=1:
    // sub_const = round_offset, shifted right by RBITS after narrowing.
    const uint16x8_t sub_shr4 = vdupq_n_u16((uint16_t)(round_offset >> 4));
    const uint16x8_t sub_shr2 = vdupq_n_u16((uint16_t)(round_offset >> 2));
    const uint16x4_t sub_shr4_4 = vdup_n_u16((uint16_t)(round_offset >> 4));
    const uint16x4_t sub_shr2_4 = vdup_n_u16((uint16_t)(round_offset >> 2));

    if (w == 4) {
#define COPY_AVG_4(MODE, RBITS)                                             \
  do {                                                                      \
    uint16_t *d = dst;                                                      \
    CONV_BUF_TYPE *d16 = dst16;                                             \
    const uint16_t *s = src;                                                \
    for (int y = 0; y < h; y++) {                                           \
      uint16x4_t sv = vld1_u16(s);                                          \
      uint32x4_t wide = vmovl_u16(sv);                                      \
      wide = vshlq_u32(wide, vdupq_n_s32(bits));                            \
      int32x4_t res_s32 = vaddq_s32(vreinterpretq_s32_u32(wide), offset_v); \
      CWP_1D_FINISH_ROW_4(res_s32, MODE, RBITS);                            \
      s += src_stride;                                                      \
    }                                                                       \
  } while (0)

      if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          COPY_AVG_4(2, 4);
        } else {
          COPY_AVG_4(1, 4);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          COPY_AVG_4(2, 2);
        } else {
          COPY_AVG_4(1, 2);
        }
      } else {
        if (use_wtd_comp_avg) {
          COPY_AVG_4(2, 0);
        } else {
          COPY_AVG_4(1, 0);
        }
      }
#undef COPY_AVG_4
    } else {
#define COPY_AVG_8(MODE, RBITS)                                              \
  do {                                                                       \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    const uint16_t *s = src;                                                 \
    for (int y = 0; y < h; y++) {                                            \
      int x = 0;                                                             \
      do {                                                                   \
        uint16x8_t sv = vld1q_u16(s + x);                                    \
        uint32x4_t lo_u = vmovl_u16(vget_low_u16(sv));                       \
        uint32x4_t hi_u = vmovl_u16(vget_high_u16(sv));                      \
        lo_u = vshlq_u32(lo_u, vdupq_n_s32(bits));                           \
        hi_u = vshlq_u32(hi_u, vdupq_n_s32(bits));                           \
        int32x4_t res_lo = vaddq_s32(vreinterpretq_s32_u32(lo_u), offset_v); \
        int32x4_t res_hi = vaddq_s32(vreinterpretq_s32_u32(hi_u), offset_v); \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                    \
        d += 8 - dst_stride;                                                 \
        d16 += 8 - dst16_stride;                                             \
        x += 8;                                                              \
      } while (x < w);                                                       \
      d += dst_stride - w;                                                   \
      d16 += dst16_stride - w;                                               \
      s += src_stride;                                                       \
    }                                                                        \
  } while (0)

      if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          COPY_AVG_8(2, 4);
        } else {
          COPY_AVG_8(1, 4);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          COPY_AVG_8(2, 2);
        } else {
          COPY_AVG_8(1, 2);
        }
      } else {
        if (use_wtd_comp_avg) {
          COPY_AVG_8(2, 0);
        } else {
          COPY_AVG_8(1, 0);
        }
      }
#undef COPY_AVG_8
    }
  }
}

// ===========================================================================
// av2_highbd_cwp_convolve_x_neon -- horizontal-only filter + compound blend
// ===========================================================================

// Single-pass horizontal filter helpers producing int32 output.
// Unlike the 2D horizontal helpers which narrow to int16 intermediate,
// these keep the result in int32 for direct compound blend.

// Raw variant: returns filter sum without post-filter rounding.
// Caller applies CWP_X_POST_FILTER with compile-time constants.
static inline void cwp_highbd_convolve_x_8_8tap_raw(const int16_t *s,
                                                    const int16x8_t x_filter,
                                                    int32x4_t *res_lo,
                                                    int32x4_t *res_hi) {
  const int16x4_t f_lo = vget_low_s16(x_filter);
  const int16x4_t f_hi = vget_high_s16(x_filter);
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);
  int16x8_t s4 = vld1q_s16(s + 4);
  int16x8_t s5 = vld1q_s16(s + 5);
  int16x8_t s6 = vld1q_s16(s + 6);
  int16x8_t s7 = vld1q_s16(s + 7);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f_lo, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f_lo, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f_lo, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f_lo, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f_hi, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f_hi, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s6), f_hi, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s7), f_hi, 3);

  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f_lo, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f_lo, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f_lo, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f_lo, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f_hi, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f_hi, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s6), f_hi, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s7), f_hi, 3);

  *res_lo = sum0;
  *res_hi = sum1;
}

// Symmetric 8-tap raw: exploit f[i]==f[7-i] to halve MAC count.
// Pre-add symmetric pairs (s0+s7, s1+s6, s2+s5, s3+s4), then 4 MACs with
// dual accumulators. Returns raw filter sum (no post-filter rounding).
static inline void cwp_highbd_convolve_x_8_8tap_sym_raw(const int16_t *s,
                                                        const int16x4_t f_sym,
                                                        int32x4_t *res_lo,
                                                        int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);
  int16x8_t s4 = vld1q_s16(s + 4);
  int16x8_t s5 = vld1q_s16(s + 5);
  int16x8_t s6 = vld1q_s16(s + 6);
  int16x8_t s7 = vld1q_s16(s + 7);

  // Pre-add symmetric pairs
  int16x8_t a = vaddq_s16(s0, s7);
  int16x8_t b = vaddq_s16(s1, s6);
  int16x8_t c = vaddq_s16(s2, s5);
  int16x8_t d = vaddq_s16(s3, s4);

  // Lo half: dual accumulator to break dependency chain
  int32x4_t acc_lo = vmull_lane_s16(vget_low_s16(a), f_sym, 0);
  int32x4_t ind_lo = vmull_lane_s16(vget_low_s16(b), f_sym, 1);
  acc_lo = vmlal_lane_s16(acc_lo, vget_low_s16(c), f_sym, 2);
  ind_lo = vmlal_lane_s16(ind_lo, vget_low_s16(d), f_sym, 3);
  *res_lo = vaddq_s32(acc_lo, ind_lo);

  // Hi half: same split
  int32x4_t acc_hi = vmull_lane_s16(vget_high_s16(a), f_sym, 0);
  int32x4_t ind_hi = vmull_lane_s16(vget_high_s16(b), f_sym, 1);
  acc_hi = vmlal_lane_s16(acc_hi, vget_high_s16(c), f_sym, 2);
  ind_hi = vmlal_lane_s16(ind_hi, vget_high_s16(d), f_sym, 3);
  *res_hi = vaddq_s32(acc_hi, ind_hi);
}

static inline void cwp_highbd_convolve_x_8_4tap(
    const int16_t *s, const int16x4_t x_filter, const int32x4_t round_const,
    const int32x4_t round_shift, const int32x4_t bits_shift,
    const int32x4_t offset, int32x4_t *res_lo, int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), x_filter, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), x_filter, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), x_filter, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), x_filter, 3);

  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), x_filter, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), x_filter, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), x_filter, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), x_filter, 3);

  sum0 = vaddq_s32(sum0, round_const);
  sum1 = vaddq_s32(sum1, round_const);
  sum0 = vshlq_s32(sum0, round_shift);
  sum1 = vshlq_s32(sum1, round_shift);
  sum0 = vshlq_s32(sum0, bits_shift);
  sum1 = vshlq_s32(sum1, bits_shift);
  *res_lo = vaddq_s32(sum0, offset);
  *res_hi = vaddq_s32(sum1, offset);
}

// Raw variant: returns filter sum without post-filter rounding.
// Caller applies CWP_X_POST_FILTER with compile-time constants.
static inline void cwp_highbd_convolve_x_8_4tap_raw(const int16_t *s,
                                                    const int16x4_t x_filter,
                                                    int32x4_t *res_lo,
                                                    int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), x_filter, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), x_filter, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), x_filter, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), x_filter, 3);

  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), x_filter, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), x_filter, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), x_filter, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), x_filter, 3);

  *res_lo = sum0;
  *res_hi = sum1;
}

static inline void cwp_highbd_convolve_x_4_4tap(
    const int16_t *s, const int16x4_t x_filter, const int32x4_t round_const,
    const int32x4_t round_shift, const int32x4_t bits_shift,
    const int32x4_t offset, int32x4_t *res) {
  int16x4_t s0 = vld1_s16(s + 0);
  int16x4_t s1 = vld1_s16(s + 1);
  int16x4_t s2 = vld1_s16(s + 2);
  int16x4_t s3 = vld1_s16(s + 3);

  int32x4_t sum = vmull_lane_s16(s0, x_filter, 0);
  sum = vmlal_lane_s16(sum, s1, x_filter, 1);
  sum = vmlal_lane_s16(sum, s2, x_filter, 2);
  sum = vmlal_lane_s16(sum, s3, x_filter, 3);

  sum = vaddq_s32(sum, round_const);
  sum = vshlq_s32(sum, round_shift);
  sum = vshlq_s32(sum, bits_shift);
  *res = vaddq_s32(sum, offset);
}

// Raw variant: returns filter sum without post-filter rounding.
static inline void cwp_highbd_convolve_x_4_4tap_raw(const int16_t *s,
                                                    const int16x4_t x_filter,
                                                    int32x4_t *res) {
  int16x4_t s0 = vld1_s16(s + 0);
  int16x4_t s1 = vld1_s16(s + 1);
  int16x4_t s2 = vld1_s16(s + 2);
  int16x4_t s3 = vld1_s16(s + 3);

  int32x4_t sum = vmull_lane_s16(s0, x_filter, 0);
  sum = vmlal_lane_s16(sum, s1, x_filter, 1);
  sum = vmlal_lane_s16(sum, s2, x_filter, 2);
  sum = vmlal_lane_s16(sum, s3, x_filter, 3);

  *res = sum;
}

// Raw variant: returns filter sum without post-filter rounding.
// Caller applies CWP_X_POST_FILTER with compile-time constants.
static inline void cwp_highbd_convolve_x_8_6tap_raw(const int16_t *s,
                                                    const int16x4_t f_lo,
                                                    const int16x4_t f_hi,
                                                    int32x4_t *res_lo,
                                                    int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);
  int16x8_t s4 = vld1q_s16(s + 4);
  int16x8_t s5 = vld1q_s16(s + 5);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f_lo, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f_lo, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f_lo, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f_lo, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f_hi, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f_hi, 1);

  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f_lo, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f_lo, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f_lo, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f_lo, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f_hi, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f_hi, 1);

  *res_lo = sum0;
  *res_hi = sum1;
}

// Raw variant: returns filter sum without post-filter rounding.
// Caller applies CWP_X_POST_FILTER with compile-time constants.
static inline void cwp_highbd_convolve_x_8_6tap_sym_raw(const int16_t *s,
                                                        const int16x4_t f_sym,
                                                        int32x4_t *res_lo,
                                                        int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);
  int16x8_t s4 = vld1q_s16(s + 4);
  int16x8_t s5 = vld1q_s16(s + 5);

  int16x8_t a = vaddq_s16(s0, s5);
  int16x8_t b = vaddq_s16(s1, s4);
  int16x8_t c = vaddq_s16(s2, s3);

  int32x4_t acc_lo = vmull_lane_s16(vget_low_s16(a), f_sym, 0);
  int32x4_t ind_lo = vmull_lane_s16(vget_low_s16(b), f_sym, 1);
  acc_lo = vmlal_lane_s16(acc_lo, vget_low_s16(c), f_sym, 2);
  *res_lo = vaddq_s32(acc_lo, ind_lo);

  int32x4_t acc_hi = vmull_lane_s16(vget_high_s16(a), f_sym, 0);
  int32x4_t ind_hi = vmull_lane_s16(vget_high_s16(b), f_sym, 1);
  acc_hi = vmlal_lane_s16(acc_hi, vget_high_s16(c), f_sym, 2);
  *res_hi = vaddq_s32(acc_hi, ind_hi);
}

// Post-filter rounding macro for convolve_x: replaces 4 ops (vadd, vshl,
// vshl, vadd) with 2 ops (vrshr_n, vadd) when ROUND0 is a compile-time
// constant 3 or 5 and BITS is 0. Falls back to the original 4-op sequence
// for non-standard values.
// Captures round_const, round_shift, bits_shift from enclosing scope.
#define CWP_X_POST_FILTER(sum, ROUND0, BITS, offset) \
  do {                                               \
    if ((ROUND0) == 3) {                             \
      (sum) = vrshrq_n_s32((sum), 3);                \
    } else if ((ROUND0) == 5) {                      \
      (sum) = vrshrq_n_s32((sum), 5);                \
    } else {                                         \
      (sum) = vaddq_s32((sum), round_const);         \
      (sum) = vshlq_s32((sum), round_shift);         \
    }                                                \
    if ((BITS) == 4) {                               \
      (sum) = vshlq_n_s32((sum), 4);                 \
    } else if ((BITS) == 3) {                        \
      (sum) = vshlq_n_s32((sum), 3);                 \
    } else if ((BITS) > 0) {                         \
      (sum) = vshlq_s32((sum), bits_shift);          \
    }                                                \
    (sum) = vaddq_s32((sum), (offset));              \
  } while (0)

static inline void cwp_highbd_convolve_x_8_2tap(
    const int16_t *s, const int16x4_t f, const int32x4_t round_const,
    const int32x4_t round_shift, const int32x4_t bits_shift,
    const int32x4_t offset, int32x4_t *res_lo, int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f, 1);
  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f, 1);

  sum0 = vaddq_s32(sum0, round_const);
  sum1 = vaddq_s32(sum1, round_const);
  sum0 = vshlq_s32(sum0, round_shift);
  sum1 = vshlq_s32(sum1, round_shift);
  sum0 = vshlq_s32(sum0, bits_shift);
  sum1 = vshlq_s32(sum1, bits_shift);
  *res_lo = vaddq_s32(sum0, offset);
  *res_hi = vaddq_s32(sum1, offset);
}

// Raw variant: returns filter sum without post-filter rounding.
static inline void cwp_highbd_convolve_x_8_2tap_raw(const int16_t *s,
                                                    const int16x4_t f,
                                                    int32x4_t *res_lo,
                                                    int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f, 1);
  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f, 1);

  *res_lo = sum0;
  *res_hi = sum1;
}

static inline void cwp_highbd_convolve_x_4_2tap(
    const int16_t *s, const int16x4_t f, const int32x4_t round_const,
    const int32x4_t round_shift, const int32x4_t bits_shift,
    const int32x4_t offset, int32x4_t *res) {
  int16x4_t s0 = vld1_s16(s + 0);
  int16x4_t s1 = vld1_s16(s + 1);

  int32x4_t sum = vmull_lane_s16(s0, f, 0);
  sum = vmlal_lane_s16(sum, s1, f, 1);

  sum = vaddq_s32(sum, round_const);
  sum = vshlq_s32(sum, round_shift);
  sum = vshlq_s32(sum, bits_shift);
  *res = vaddq_s32(sum, offset);
}

// Raw variant: returns filter sum without post-filter rounding.
static inline void cwp_highbd_convolve_x_4_2tap_raw(const int16_t *s,
                                                    const int16x4_t f,
                                                    int32x4_t *res) {
  int16x4_t s0 = vld1_s16(s + 0);
  int16x4_t s1 = vld1_s16(s + 1);

  int32x4_t sum = vmull_lane_s16(s0, f, 0);
  sum = vmlal_lane_s16(sum, s1, f, 1);

  *res = sum;
}

// 12-tap horizontal raw helpers for convolve_x.
// 12 coefficients in three int16x4_t registers (f0, f1, f2).
static inline void cwp_highbd_convolve_x_8_12tap_raw(
    const int16_t *s, const int16x4_t f0, const int16x4_t f1,
    const int16x4_t f2, int32x4_t *res_lo, int32x4_t *res_hi) {
  int16x8_t s0 = vld1q_s16(s + 0);
  int16x8_t s1 = vld1q_s16(s + 1);
  int16x8_t s2 = vld1q_s16(s + 2);
  int16x8_t s3 = vld1q_s16(s + 3);
  int16x8_t s4 = vld1q_s16(s + 4);
  int16x8_t s5 = vld1q_s16(s + 5);
  int16x8_t s6 = vld1q_s16(s + 6);
  int16x8_t s7 = vld1q_s16(s + 7);
  int16x8_t s8 = vld1q_s16(s + 8);
  int16x8_t s9 = vld1q_s16(s + 9);
  int16x8_t s10 = vld1q_s16(s + 10);
  int16x8_t s11 = vld1q_s16(s + 11);

  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f0, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f0, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f0, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f0, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f1, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f1, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s6), f1, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s7), f1, 3);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s8), f2, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s9), f2, 1);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s10), f2, 2);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s11), f2, 3);

  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f0, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f0, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f0, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f0, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f1, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f1, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s6), f1, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s7), f1, 3);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s8), f2, 0);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s9), f2, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s10), f2, 2);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s11), f2, 3);

  *res_lo = sum0;
  *res_hi = sum1;
}

static inline void cwp_highbd_convolve_x_4_12tap_raw(const int16_t *s,
                                                     const int16x4_t f0,
                                                     const int16x4_t f1,
                                                     const int16x4_t f2,
                                                     int32x4_t *res) {
  int16x4_t s0 = vld1_s16(s + 0);
  int16x4_t s1 = vld1_s16(s + 1);
  int16x4_t s2 = vld1_s16(s + 2);
  int16x4_t s3 = vld1_s16(s + 3);
  int16x4_t s4 = vld1_s16(s + 4);
  int16x4_t s5 = vld1_s16(s + 5);
  int16x4_t s6 = vld1_s16(s + 6);
  int16x4_t s7 = vld1_s16(s + 7);
  int16x4_t s8 = vld1_s16(s + 8);
  int16x4_t s9 = vld1_s16(s + 9);
  int16x4_t s10 = vld1_s16(s + 10);
  int16x4_t s11 = vld1_s16(s + 11);

  int32x4_t sum = vmull_lane_s16(s0, f0, 0);
  sum = vmlal_lane_s16(sum, s1, f0, 1);
  sum = vmlal_lane_s16(sum, s2, f0, 2);
  sum = vmlal_lane_s16(sum, s3, f0, 3);
  sum = vmlal_lane_s16(sum, s4, f1, 0);
  sum = vmlal_lane_s16(sum, s5, f1, 1);
  sum = vmlal_lane_s16(sum, s6, f1, 2);
  sum = vmlal_lane_s16(sum, s7, f1, 3);
  sum = vmlal_lane_s16(sum, s8, f2, 0);
  sum = vmlal_lane_s16(sum, s9, f2, 1);
  sum = vmlal_lane_s16(sum, s10, f2, 2);
  sum = vmlal_lane_s16(sum, s11, f2, 3);

  *res = sum;
}

void av2_highbd_cwp_convolve_x_neon(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const int subpel_x_qn,
                                    ConvolveParams *conv_params, int bd) {
  const int tap_x = get_filter_tap(filter_params_x, subpel_x_qn);

  CONV_BUF_TYPE *dst16 = conv_params->dst;
  const int dst16_stride = conv_params->dst_stride;
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  const int fo_horiz = tap_x / 2 - 1;
  const int16_t *x_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);

  // Rounding: res = ROUND_POWER_OF_TWO(filter_sum, round_0)
  //           res = res * (1 << bits) + round_offset
  const int bits = FILTER_BITS - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;

  assert(bits >= 0);
  assert(round_bits >= 0);
  assert(w >= 4 && (w == 4 || (w % 8) == 0));

  const int32x4_t round_const = vdupq_n_s32((1 << conv_params->round_0) >> 1);
  const int32x4_t round_shift = vdupq_n_s32(-conv_params->round_0);
  const int32x4_t bits_shift = vdupq_n_s32(bits);
  const int32x4_t offset_v = vdupq_n_s32(round_offset);
  const int32x4_t fwd_s32 = vdupq_n_s32(conv_params->fwd_offset);
  const int32x4_t sub_const = vdupq_n_s32(round_offset);
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t max_val4 = vdup_n_u16((uint16_t)((1 << bd) - 1));
  // Post-narrow sub vectors for CWP_1D_FINISH_ROW MODE=1:
  // sub_const = round_offset, shifted right by RBITS after narrowing.
  const uint16x8_t sub_shr4 = vdupq_n_u16((uint16_t)(round_offset >> 4));
  const uint16x8_t sub_shr2 = vdupq_n_u16((uint16_t)(round_offset >> 2));
  const uint16x4_t sub_shr4_4 = vdup_n_u16((uint16_t)(round_offset >> 4));
  const uint16x4_t sub_shr2_4 = vdup_n_u16((uint16_t)(round_offset >> 2));

  const uint16_t *src_horiz = src - fo_horiz;

  // Dispatch by tap count, then by width, then by MODE + RBITS.
  // RBITS hoists the round_bits comparison out of the inner loop.
  if (tap_x == 2) {
    const int16_t f_arr[4] = { x_filter_ptr[3], x_filter_ptr[4], 0, 0 };
    const int16x4_t f = vld1_s16(f_arr);

    if (w == 4) {
#define CONV_X_2TAP_4(MODE, RBITS)                                           \
  do {                                                                       \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    const uint16_t *s = src_horiz;                                           \
    for (int y = 0; y < h; y++) {                                            \
      int32x4_t res;                                                         \
      cwp_highbd_convolve_x_4_2tap((const int16_t *)s, f, round_const,       \
                                   round_shift, bits_shift, offset_v, &res); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      s += src_stride;                                                       \
    }                                                                        \
  } while (0)
#define CONV_X_2TAP_4_RAW(MODE, RBITS, R0, XBITS)                    \
  do {                                                               \
    uint16_t *d = dst;                                               \
    CONV_BUF_TYPE *d16 = dst16;                                      \
    const uint16_t *s = src_horiz;                                   \
    for (int y = 0; y < h; y++) {                                    \
      int32x4_t res;                                                 \
      cwp_highbd_convolve_x_4_2tap_raw((const int16_t *)s, f, &res); \
      CWP_X_POST_FILTER(res, R0, XBITS, offset_v);                   \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      s += src_stride;                                               \
    }                                                                \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_2TAP_4_RAW(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_2TAP_4_RAW(0, 0, 5, 0);
        } else {
          CONV_X_2TAP_4_RAW(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_4_RAW(2, 4, 3, 0);
        } else {
          CONV_X_2TAP_4_RAW(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_4_RAW(2, 2, 5, 0);
        } else {
          CONV_X_2TAP_4_RAW(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_4(2, 0);
        } else {
          CONV_X_2TAP_4(1, 0);
        }
      }
#undef CONV_X_2TAP_4
#undef CONV_X_2TAP_4_RAW
    } else {
#define CONV_X_2TAP_8(MODE, RBITS)                                             \
  do {                                                                         \
    uint16_t *d = dst;                                                         \
    CONV_BUF_TYPE *d16 = dst16;                                                \
    const uint16_t *s = src_horiz;                                             \
    for (int y = 0; y < h; y++) {                                              \
      int x = 0;                                                               \
      do {                                                                     \
        int32x4_t res_lo, res_hi;                                              \
        cwp_highbd_convolve_x_8_2tap((const int16_t *)(s + x), f, round_const, \
                                     round_shift, bits_shift, offset_v,        \
                                     &res_lo, &res_hi);                        \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                      \
        d += 8 - dst_stride;                                                   \
        d16 += 8 - dst16_stride;                                               \
        x += 8;                                                                \
      } while (x < w);                                                         \
      d += dst_stride - w;                                                     \
      d16 += dst16_stride - w;                                                 \
      s += src_stride;                                                         \
    }                                                                          \
  } while (0)
#define CONV_X_2TAP_8_RAW(MODE, RBITS, R0, XBITS)                              \
  do {                                                                         \
    uint16_t *d = dst;                                                         \
    CONV_BUF_TYPE *d16 = dst16;                                                \
    const uint16_t *s = src_horiz;                                             \
    for (int y = 0; y < h; y++) {                                              \
      int x = 0;                                                               \
      do {                                                                     \
        int32x4_t res_lo, res_hi;                                              \
        cwp_highbd_convolve_x_8_2tap_raw((const int16_t *)(s + x), f, &res_lo, \
                                         &res_hi);                             \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                        \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                        \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                      \
        d += 8 - dst_stride;                                                   \
        d16 += 8 - dst16_stride;                                               \
        x += 8;                                                                \
      } while (x < w);                                                         \
      d += dst_stride - w;                                                     \
      d16 += dst16_stride - w;                                                 \
      s += src_stride;                                                         \
    }                                                                          \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_2TAP_8_RAW(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_2TAP_8_RAW(0, 0, 5, 0);
        } else {
          CONV_X_2TAP_8_RAW(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_8_RAW(2, 4, 3, 0);
        } else {
          CONV_X_2TAP_8_RAW(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_8_RAW(2, 2, 5, 0);
        } else {
          CONV_X_2TAP_8_RAW(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_2TAP_8(2, 0);
        } else {
          CONV_X_2TAP_8(1, 0);
        }
      }
#undef CONV_X_2TAP_8
#undef CONV_X_2TAP_8_RAW
    }
  } else if (tap_x == 4) {
    const int16x4_t xf4 = vld1_s16(x_filter_ptr + 2);

    if (w == 4) {
#define CONV_X_4TAP_4(MODE, RBITS)                                           \
  do {                                                                       \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    const uint16_t *s = src_horiz;                                           \
    for (int y = 0; y < h; y++) {                                            \
      int32x4_t res;                                                         \
      cwp_highbd_convolve_x_4_4tap((const int16_t *)s, xf4, round_const,     \
                                   round_shift, bits_shift, offset_v, &res); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      s += src_stride;                                                       \
    }                                                                        \
  } while (0)
#define CONV_X_4TAP_4_RAW(MODE, RBITS, R0, XBITS)                      \
  do {                                                                 \
    uint16_t *d = dst;                                                 \
    CONV_BUF_TYPE *d16 = dst16;                                        \
    const uint16_t *s = src_horiz;                                     \
    for (int y = 0; y < h; y++) {                                      \
      int32x4_t res;                                                   \
      cwp_highbd_convolve_x_4_4tap_raw((const int16_t *)s, xf4, &res); \
      CWP_X_POST_FILTER(res, R0, XBITS, offset_v);                     \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                           \
      s += src_stride;                                                 \
    }                                                                  \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_4TAP_4_RAW(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_4TAP_4_RAW(0, 0, 5, 0);
        } else {
          CONV_X_4TAP_4_RAW(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_4_RAW(2, 4, 3, 0);
        } else {
          CONV_X_4TAP_4_RAW(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_4_RAW(2, 2, 5, 0);
        } else {
          CONV_X_4TAP_4_RAW(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_4(2, 0);
        } else {
          CONV_X_4TAP_4(1, 0);
        }
      }
#undef CONV_X_4TAP_4
#undef CONV_X_4TAP_4_RAW
    } else {
#define CONV_X_4TAP_8(MODE, RBITS)                                         \
  do {                                                                     \
    uint16_t *d = dst;                                                     \
    CONV_BUF_TYPE *d16 = dst16;                                            \
    const uint16_t *s = src_horiz;                                         \
    for (int y = 0; y < h; y++) {                                          \
      int x = 0;                                                           \
      do {                                                                 \
        int32x4_t res_lo, res_hi;                                          \
        cwp_highbd_convolve_x_8_4tap((const int16_t *)(s + x), xf4,        \
                                     round_const, round_shift, bits_shift, \
                                     offset_v, &res_lo, &res_hi);          \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                  \
        d += 8 - dst_stride;                                               \
        d16 += 8 - dst16_stride;                                           \
        x += 8;                                                            \
      } while (x < w);                                                     \
      d += dst_stride - w;                                                 \
      d16 += dst16_stride - w;                                             \
      s += src_stride;                                                     \
    }                                                                      \
  } while (0)
#define CONV_X_4TAP_8_RAW(MODE, RBITS, R0, XBITS)                       \
  do {                                                                  \
    uint16_t *d = dst;                                                  \
    CONV_BUF_TYPE *d16 = dst16;                                         \
    const uint16_t *s = src_horiz;                                      \
    for (int y = 0; y < h; y++) {                                       \
      int x = 0;                                                        \
      do {                                                              \
        int32x4_t res_lo, res_hi;                                       \
        cwp_highbd_convolve_x_8_4tap_raw((const int16_t *)(s + x), xf4, \
                                         &res_lo, &res_hi);             \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                 \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                 \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);               \
        d += 8 - dst_stride;                                            \
        d16 += 8 - dst16_stride;                                        \
        x += 8;                                                         \
      } while (x < w);                                                  \
      d += dst_stride - w;                                              \
      d16 += dst16_stride - w;                                          \
      s += src_stride;                                                  \
    }                                                                   \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_4TAP_8_RAW(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_4TAP_8_RAW(0, 0, 5, 0);
        } else {
          CONV_X_4TAP_8_RAW(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_8_RAW(2, 4, 3, 0);
        } else {
          CONV_X_4TAP_8_RAW(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_8_RAW(2, 2, 5, 0);
        } else {
          CONV_X_4TAP_8_RAW(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_4TAP_8(2, 0);
        } else {
          CONV_X_4TAP_8(1, 0);
        }
      }
#undef CONV_X_4TAP_8
#undef CONV_X_4TAP_8_RAW
    }
  } else if (tap_x == 6) {
    // 6-tap always w >= 8
    assert(w >= 8);

    const int sym_6tap = (x_filter_ptr[1] == x_filter_ptr[6]) &&
                         (x_filter_ptr[2] == x_filter_ptr[5]) &&
                         (x_filter_ptr[3] == x_filter_ptr[4]);

    if (sym_6tap) {
      // Symmetric path: f_sym = { filter[1], filter[2], filter[3], 0 }
      const int16_t xf6_sym_arr[4] = { x_filter_ptr[1], x_filter_ptr[2],
                                       x_filter_ptr[3], 0 };
      const int16x4_t xf6_sym = vld1_s16(xf6_sym_arr);

#define CONV_X_6TAP_SYM_8(MODE, RBITS, R0, XBITS)                        \
  do {                                                                   \
    uint16_t *d = dst;                                                   \
    CONV_BUF_TYPE *d16 = dst16;                                          \
    const uint16_t *s = src_horiz;                                       \
    for (int y = 0; y < h; y++) {                                        \
      int x = 0;                                                         \
      do {                                                               \
        int32x4_t res_lo, res_hi;                                        \
        cwp_highbd_convolve_x_8_6tap_sym_raw((const int16_t *)(s + x),   \
                                             xf6_sym, &res_lo, &res_hi); \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                  \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                  \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                \
        d += 8 - dst_stride;                                             \
        d16 += 8 - dst16_stride;                                         \
        x += 8;                                                          \
      } while (x < w);                                                   \
      d += dst_stride - w;                                               \
      d16 += dst16_stride - w;                                           \
      s += src_stride;                                                   \
    }                                                                    \
  } while (0)
      if (!do_average) {
        // MODE=0: specialize on round_0 for compile-time shifts.
        // bits is always 0 for compound (FILTER_BITS - COMPOUND_ROUND1_BITS).
        // For bd=10: round_0=3 -> vrshrq_n_s32(,3) + vadd (2 ops vs 4).
        // For bd=12: round_0=5 -> vrshrq_n_s32(,5) + vadd (2 ops vs 4).
        if (conv_params->round_0 == 3) {
          CONV_X_6TAP_SYM_8(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_6TAP_SYM_8(0, 0, 5, 0);
        } else {
          CONV_X_6TAP_SYM_8(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        // round_bits==4 <-> bd=10 <-> round_0=3. Specialize R0 for
        // compile-time immediate shift in CWP_X_POST_FILTER.
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_SYM_8(2, 4, 3, 0);
        } else {
          CONV_X_6TAP_SYM_8(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        // round_bits==2 <-> bd=12 <-> round_0=5.
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_SYM_8(2, 2, 5, 0);
        } else {
          CONV_X_6TAP_SYM_8(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_SYM_8(2, 0, 0, 0);
        } else {
          CONV_X_6TAP_SYM_8(1, 0, 0, 0);
        }
      }
#undef CONV_X_6TAP_SYM_8
    } else {
      // Asymmetric fallback
      const int16x4_t xf6_lo = vld1_s16(x_filter_ptr + 1);
      const int16_t xf6_hi_arr[4] = { x_filter_ptr[5], x_filter_ptr[6], 0, 0 };
      const int16x4_t xf6_hi = vld1_s16(xf6_hi_arr);

#define CONV_X_6TAP_8(MODE, RBITS, R0, XBITS)                              \
  do {                                                                     \
    uint16_t *d = dst;                                                     \
    CONV_BUF_TYPE *d16 = dst16;                                            \
    const uint16_t *s = src_horiz;                                         \
    for (int y = 0; y < h; y++) {                                          \
      int x = 0;                                                           \
      do {                                                                 \
        int32x4_t res_lo, res_hi;                                          \
        cwp_highbd_convolve_x_8_6tap_raw((const int16_t *)(s + x), xf6_lo, \
                                         xf6_hi, &res_lo, &res_hi);        \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                    \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                    \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                  \
        d += 8 - dst_stride;                                               \
        d16 += 8 - dst16_stride;                                           \
        x += 8;                                                            \
      } while (x < w);                                                     \
      d += dst_stride - w;                                                 \
      d16 += dst16_stride - w;                                             \
      s += src_stride;                                                     \
    }                                                                      \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_6TAP_8(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_6TAP_8(0, 0, 5, 0);
        } else {
          CONV_X_6TAP_8(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_8(2, 4, 3, 0);
        } else {
          CONV_X_6TAP_8(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_8(2, 2, 5, 0);
        } else {
          CONV_X_6TAP_8(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_6TAP_8(2, 0, 0, 0);
        } else {
          CONV_X_6TAP_8(1, 0, 0, 0);
        }
      }
#undef CONV_X_6TAP_8
    }
  } else if (tap_x == 12) {
    // 12-tap, w >= 4
    const int16x4_t xf12_0 = vld1_s16(x_filter_ptr + 0);
    const int16x4_t xf12_1 = vld1_s16(x_filter_ptr + 4);
    const int16x4_t xf12_2 = vld1_s16(x_filter_ptr + 8);

    if (w == 4) {
#define CONV_X_12TAP_4(MODE, RBITS, R0, XBITS)                              \
  do {                                                                      \
    uint16_t *d = dst;                                                      \
    CONV_BUF_TYPE *d16 = dst16;                                             \
    const uint16_t *s = src_horiz;                                          \
    for (int y = 0; y < h; y++) {                                           \
      int32x4_t res;                                                        \
      cwp_highbd_convolve_x_4_12tap_raw((const int16_t *)s, xf12_0, xf12_1, \
                                        xf12_2, &res);                      \
      CWP_X_POST_FILTER(res, R0, XBITS, offset_v);                          \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                \
      s += src_stride;                                                      \
    }                                                                       \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_12TAP_4(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_12TAP_4(0, 0, 5, 0);
        } else {
          CONV_X_12TAP_4(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_4(2, 4, 3, 0);
        } else {
          CONV_X_12TAP_4(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_4(2, 2, 5, 0);
        } else {
          CONV_X_12TAP_4(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_4(2, 0, 0, 0);
        } else {
          CONV_X_12TAP_4(1, 0, 0, 0);
        }
      }
#undef CONV_X_12TAP_4
    } else {
#define CONV_X_12TAP_8(MODE, RBITS, R0, XBITS)                               \
  do {                                                                       \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
    const uint16_t *s = src_horiz;                                           \
    for (int y = 0; y < h; y++) {                                            \
      int x = 0;                                                             \
      do {                                                                   \
        int32x4_t res_lo, res_hi;                                            \
        cwp_highbd_convolve_x_8_12tap_raw((const int16_t *)(s + x), xf12_0,  \
                                          xf12_1, xf12_2, &res_lo, &res_hi); \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                      \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                      \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                    \
        d += 8 - dst_stride;                                                 \
        d16 += 8 - dst16_stride;                                             \
        x += 8;                                                              \
      } while (x < w);                                                       \
      d += dst_stride - w;                                                   \
      d16 += dst16_stride - w;                                               \
      s += src_stride;                                                       \
    }                                                                        \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_12TAP_8(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_12TAP_8(0, 0, 5, 0);
        } else {
          CONV_X_12TAP_8(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_8(2, 4, 3, 0);
        } else {
          CONV_X_12TAP_8(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_8(2, 2, 5, 0);
        } else {
          CONV_X_12TAP_8(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_12TAP_8(2, 0, 0, 0);
        } else {
          CONV_X_12TAP_8(1, 0, 0, 0);
        }
      }
#undef CONV_X_12TAP_8
    }
  } else {
    // 8-tap, always w >= 8
    assert(tap_x == 8);
    assert(w >= 8);

    // Check for symmetric coefficients: f[0]==f[7], f[1]==f[6], f[2]==f[5],
    // f[3]==f[4]. MULTITAP_SHARP at subpel_qn=8 has
    // {-4,12,-24,80,80,-24,12,-4}. Symmetric folding halves MAC count (4 MACs
    // instead of 8).
    const int x_sym_8 = (x_filter_ptr[0] == x_filter_ptr[7]) &&
                        (x_filter_ptr[1] == x_filter_ptr[6]) &&
                        (x_filter_ptr[2] == x_filter_ptr[5]) &&
                        (x_filter_ptr[3] == x_filter_ptr[4]);

    if (x_sym_8) {
      // Pack 4 unique symmetric coefficients into int16x4_t for lane-indexed
      // MLA: {f[0], f[1], f[2], f[3]}
      const int16_t xf8_sym_arr[4] = { x_filter_ptr[0], x_filter_ptr[1],
                                       x_filter_ptr[2], x_filter_ptr[3] };
      const int16x4_t xf8_sym = vld1_s16(xf8_sym_arr);

#define CONV_X_8TAP_SYM_8(MODE, RBITS, R0, XBITS)                        \
  do {                                                                   \
    uint16_t *d = dst;                                                   \
    CONV_BUF_TYPE *d16 = dst16;                                          \
    const uint16_t *s = src_horiz;                                       \
    for (int y = 0; y < h; y++) {                                        \
      int x = 0;                                                         \
      do {                                                               \
        int32x4_t res_lo, res_hi;                                        \
        cwp_highbd_convolve_x_8_8tap_sym_raw((const int16_t *)(s + x),   \
                                             xf8_sym, &res_lo, &res_hi); \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                  \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                  \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);                \
        d += 8 - dst_stride;                                             \
        d16 += 8 - dst16_stride;                                         \
        x += 8;                                                          \
      } while (x < w);                                                   \
      d += dst_stride - w;                                               \
      d16 += dst16_stride - w;                                           \
      s += src_stride;                                                   \
    }                                                                    \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_8TAP_SYM_8(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_8TAP_SYM_8(0, 0, 5, 0);
        } else {
          CONV_X_8TAP_SYM_8(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_SYM_8(2, 4, 3, 0);
        } else {
          CONV_X_8TAP_SYM_8(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_SYM_8(2, 2, 5, 0);
        } else {
          CONV_X_8TAP_SYM_8(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_SYM_8(2, 0, 0, 0);
        } else {
          CONV_X_8TAP_SYM_8(1, 0, 0, 0);
        }
      }
#undef CONV_X_8TAP_SYM_8
    } else {
      // Asymmetric 8-tap fallback
      const int16x8_t xf8 = vld1q_s16(x_filter_ptr);

#define CONV_X_8TAP_8(MODE, RBITS, R0, XBITS)                           \
  do {                                                                  \
    uint16_t *d = dst;                                                  \
    CONV_BUF_TYPE *d16 = dst16;                                         \
    const uint16_t *s = src_horiz;                                      \
    for (int y = 0; y < h; y++) {                                       \
      int x = 0;                                                        \
      do {                                                              \
        int32x4_t res_lo, res_hi;                                       \
        cwp_highbd_convolve_x_8_8tap_raw((const int16_t *)(s + x), xf8, \
                                         &res_lo, &res_hi);             \
        CWP_X_POST_FILTER(res_lo, R0, XBITS, offset_v);                 \
        CWP_X_POST_FILTER(res_hi, R0, XBITS, offset_v);                 \
        CWP_1D_FINISH_ROW_8(res_lo, res_hi, MODE, RBITS);               \
        d += 8 - dst_stride;                                            \
        d16 += 8 - dst16_stride;                                        \
        x += 8;                                                         \
      } while (x < w);                                                  \
      d += dst_stride - w;                                              \
      d16 += dst16_stride - w;                                          \
      s += src_stride;                                                  \
    }                                                                   \
  } while (0)
      if (!do_average) {
        if (conv_params->round_0 == 3) {
          CONV_X_8TAP_8(0, 0, 3, 0);
        } else if (conv_params->round_0 == 5) {
          CONV_X_8TAP_8(0, 0, 5, 0);
        } else {
          CONV_X_8TAP_8(0, 0, 0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_8(2, 4, 3, 0);
        } else {
          CONV_X_8TAP_8(1, 4, 3, 0);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_8(2, 2, 5, 0);
        } else {
          CONV_X_8TAP_8(1, 2, 5, 0);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_X_8TAP_8(2, 0, 0, 0);
        } else {
          CONV_X_8TAP_8(1, 0, 0, 0);
        }
      }
#undef CONV_X_8TAP_8
    }
  }
}

// ===========================================================================
// av2_highbd_cwp_convolve_y_neon -- vertical-only filter + compound blend
// ===========================================================================

// Vertical filter helpers for y-only path: input is uint16 (raw pixels cast
// to int16), output is int32 after filter + (1 << bits) scaling + round_1
// rounding + round_offset.
//
// C reference:
//   res = sum(y_filter[k] * src[(y - fo_vert + k) * stride + x])
//   res *= (1 << bits)      // bits = FILTER_BITS - round_0
//   res = ROUND_POWER_OF_TWO(res, round_1) + round_offset

// Raw 8-tap helpers: return filter sum before post-filter rounding.
// Callers apply CWP_Y_POST_FILTER with compile-time ROUND0 for
// immediate-shift instructions instead of variable-shift operands.
static inline int32x4_t cwp_convolve_y_8tap_raw_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi) {
  int32x4_t sum = vmull_lane_s16(vget_low_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f_hi, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s6), f_hi, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s7), f_hi, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_8tap_raw_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi) {
  int32x4_t sum = vmull_lane_s16(vget_high_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f_hi, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s6), f_hi, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s7), f_hi, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_8tap_raw_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi) {
  int32x4_t sum = vmull_lane_s16(s0, f_lo, 0);
  sum = vmlal_lane_s16(sum, s1, f_lo, 1);
  sum = vmlal_lane_s16(sum, s2, f_lo, 2);
  sum = vmlal_lane_s16(sum, s3, f_lo, 3);
  sum = vmlal_lane_s16(sum, s4, f_hi, 0);
  sum = vmlal_lane_s16(sum, s5, f_hi, 1);
  sum = vmlal_lane_s16(sum, s6, f_hi, 2);
  sum = vmlal_lane_s16(sum, s7, f_hi, 3);
  return sum;
}

// Raw 8-tap symmetric helpers: exploit f[i]==f[7-i] symmetry to halve MAC
// count (4 MACs instead of 8). Pre-add symmetric pairs (s0+s7, s1+s6, s2+s5,
// s3+s4) in int16, then 4 lane-indexed MACs with the 4 unique coefficients.
// Uses dual-accumulator pattern to break the dependency chain.
static inline int32x4_t cwp_convolve_y_8tap_sym_raw_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_sym) {
  int16x8_t a = vaddq_s16(s0, s7);
  int16x8_t b = vaddq_s16(s1, s6);
  int16x8_t c = vaddq_s16(s2, s5);
  int16x8_t d = vaddq_s16(s3, s4);
  int32x4_t acc = vmull_lane_s16(vget_low_s16(a), f_sym, 0);
  int32x4_t ind = vmull_lane_s16(vget_low_s16(b), f_sym, 1);
  acc = vmlal_lane_s16(acc, vget_low_s16(c), f_sym, 2);
  ind = vmlal_lane_s16(ind, vget_low_s16(d), f_sym, 3);
  return vaddq_s32(acc, ind);
}

static inline int32x4_t cwp_convolve_y_8tap_sym_raw_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_sym) {
  int16x8_t a = vaddq_s16(s0, s7);
  int16x8_t b = vaddq_s16(s1, s6);
  int16x8_t c = vaddq_s16(s2, s5);
  int16x8_t d = vaddq_s16(s3, s4);
  int32x4_t acc = vmull_lane_s16(vget_high_s16(a), f_sym, 0);
  int32x4_t ind = vmull_lane_s16(vget_high_s16(b), f_sym, 1);
  acc = vmlal_lane_s16(acc, vget_high_s16(c), f_sym, 2);
  ind = vmlal_lane_s16(ind, vget_high_s16(d), f_sym, 3);
  return vaddq_s32(acc, ind);
}

static inline int32x4_t cwp_convolve_y_8tap_sym_raw_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t f_sym) {
  int16x4_t a = vadd_s16(s0, s7);
  int16x4_t b = vadd_s16(s1, s6);
  int16x4_t c = vadd_s16(s2, s5);
  int16x4_t d = vadd_s16(s3, s4);
  int32x4_t acc = vmull_lane_s16(a, f_sym, 0);
  int32x4_t ind = vmull_lane_s16(b, f_sym, 1);
  acc = vmlal_lane_s16(acc, c, f_sym, 2);
  ind = vmlal_lane_s16(ind, d, f_sym, 3);
  return vaddq_s32(acc, ind);
}

// Raw 4-tap helpers: return filter sum before post-filter rounding.
// Callers apply CWP_Y_POST_FILTER with compile-time ROUND0 for
// immediate-shift instructions instead of variable-shift operands.
static inline int32x4_t cwp_convolve_y_4tap_raw_lo(const int16x8_t s0,
                                                   const int16x8_t s1,
                                                   const int16x8_t s2,
                                                   const int16x8_t s3,
                                                   const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(vget_low_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_4tap_raw_hi(const int16x8_t s0,
                                                   const int16x8_t s1,
                                                   const int16x8_t s2,
                                                   const int16x8_t s3,
                                                   const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(vget_high_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_4tap_raw_4(const int16x4_t s0,
                                                  const int16x4_t s1,
                                                  const int16x4_t s2,
                                                  const int16x4_t s3,
                                                  const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(s0, f, 0);
  sum = vmlal_lane_s16(sum, s1, f, 1);
  sum = vmlal_lane_s16(sum, s2, f, 2);
  sum = vmlal_lane_s16(sum, s3, f, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_6tap_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t bits_shift,
    const int32x4_t round_const, const int32x4_t round_shift,
    const int32x4_t offset) {
  int32x4_t sum = vmull_lane_s16(vget_low_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f_hi, 1);
  sum = vshlq_s32(sum, bits_shift);
  sum = vaddq_s32(sum, round_const);
  sum = vshlq_s32(sum, round_shift);
  return vaddq_s32(sum, offset);
}

static inline int32x4_t cwp_convolve_y_6tap_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t bits_shift,
    const int32x4_t round_const, const int32x4_t round_shift,
    const int32x4_t offset) {
  int32x4_t sum = vmull_lane_s16(vget_high_s16(s0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f_lo, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f_lo, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f_lo, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f_hi, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f_hi, 1);
  sum = vshlq_s32(sum, bits_shift);
  sum = vaddq_s32(sum, round_const);
  sum = vshlq_s32(sum, round_shift);
  return vaddq_s32(sum, offset);
}

static inline int32x4_t cwp_convolve_y_6tap_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t f_lo, const int16x4_t f_hi, const int32x4_t bits_shift,
    const int32x4_t round_const, const int32x4_t round_shift,
    const int32x4_t offset) {
  int32x4_t sum = vmull_lane_s16(s0, f_lo, 0);
  sum = vmlal_lane_s16(sum, s1, f_lo, 1);
  sum = vmlal_lane_s16(sum, s2, f_lo, 2);
  sum = vmlal_lane_s16(sum, s3, f_lo, 3);
  sum = vmlal_lane_s16(sum, s4, f_hi, 0);
  sum = vmlal_lane_s16(sum, s5, f_hi, 1);
  sum = vshlq_s32(sum, bits_shift);
  sum = vaddq_s32(sum, round_const);
  sum = vshlq_s32(sum, round_shift);
  return vaddq_s32(sum, offset);
}

// Symmetric 6-tap vertical: fold s0+s5, s1+s4, s2+s3 in int16 before 3 MACs.
// Uses vaddq_s16 (stay in s16) + vmull/vmlal_lane_s16 (3-cycle 16x16->32 MLA).
// Same dual-accumulator pattern as the horizontal symmetric helper: split the
// 3-MAC chain into a 2-MAC chain + independent vmull + vaddq merge to break
// the dependency chain.
// Filter coefficients are packed into int16x4_t (3 unique taps + padding).
// Safe for bd<=10: max pixel 1023, so s0+s5 <= 2046 fits in int16.
//
// Post-filter rounding: the 4-instruction sequence
//   sum <<= bits; sum += round_const; sum >>= round_1; sum += offset;
// simplifies to 2 instructions because bits = FILTER_BITS - round_0 and
// round_1 = COMPOUND_ROUND1_BITS = FILTER_BITS (= 7), so the net shift is
// just -round_0. For RBITS=4 (bd=10): round_0=3. For RBITS=2 (bd=12):
// round_0=5. ROUND0 must be a compile-time constant (3 or 5).
//
// The _raw helpers return the filter sum *before* the post-filter rounding
// chain. The CONV_Y_6TAP_SYM loop macros apply the post-filter inline via
// CWP_Y_POST_FILTER, allowing the compiler to emit immediate-shift
// instructions (vrshr #3 or #5) instead of variable-shift (vshl with
// register operand).

// Post-filter rounding macro: replaces 4 ops (vshl, vadd, vshl, vadd) with
// 2 ops (vrshr_n, vadd) when ROUND0 is a compile-time constant 3 or 5.
// Falls back to the original 4-op sequence for ROUND0=0 (non-standard).
// Captures bits_shift, round_const_v, round_shift_v from enclosing scope.
#define CWP_Y_POST_FILTER(sum, ROUND0, offset) \
  do {                                         \
    if ((ROUND0) == 3) {                       \
      (sum) = vrshrq_n_s32((sum), 3);          \
    } else if ((ROUND0) == 5) {                \
      (sum) = vrshrq_n_s32((sum), 5);          \
    } else {                                   \
      (sum) = vshlq_s32((sum), bits_shift);    \
      (sum) = vaddq_s32((sum), round_const_v); \
      (sum) = vshlq_s32((sum), round_shift_v); \
    }                                          \
    (sum) = vaddq_s32((sum), (offset));        \
  } while (0)

// Post-filter rounding WITHOUT offset addition. Used by offset-elision paths
// (MODE=1 avg) where round_offset is deferred and halved in the finish row.
#define CWP_Y_POST_FILTER_NOOFF(sum, ROUND0)   \
  do {                                         \
    if ((ROUND0) == 3) {                       \
      (sum) = vrshrq_n_s32((sum), 3);          \
    } else if ((ROUND0) == 5) {                \
      (sum) = vrshrq_n_s32((sum), 5);          \
    } else {                                   \
      (sum) = vshlq_s32((sum), bits_shift);    \
      (sum) = vaddq_s32((sum), round_const_v); \
      (sum) = vshlq_s32((sum), round_shift_v); \
    }                                          \
  } while (0)

// Finish row for MODE=1 (simple avg) with offset elision: d16 carries
// round_offset from the first pass but res does NOT include round_offset.
// vhaddq_s32(d16, res) = (d16_with_offset + res_no_offset) / 2, then
// subtract round_offset/2 (= half_sub_const) to remove the bias.
// Saves 2 vaddq_s32 per 8-pixel row vs the standard post-filter path.
#define CWP_1D_FINISH_ROW_8_AVG_NOOFF(res_lo, res_hi, RBITS)                   \
  do {                                                                         \
    uint16x8_t d16_raw = vld1q_u16(d16);                                       \
    int32x4_t d_lo = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(d16_raw)));  \
    int32x4_t d_hi = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(d16_raw))); \
    int32x4_t tmp_lo = vhaddq_s32(d_lo, (res_lo));                             \
    int32x4_t tmp_hi = vhaddq_s32(d_hi, (res_hi));                             \
    uint16x8_t r8;                                                             \
    if ((RBITS) == 4) {                                                        \
      /* Subtract after narrow: vqrshrun(x-C,N) = vqsub(vqrshrun(x,N), C>>N)   \
       */                                                                      \
      /* when C is divisible by 1<<N. Saves 1 vsubq_s32 per row. */            \
      r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 4), vqrshrun_n_s32(tmp_hi, 4)); \
      r8 = vqsubq_u16(r8, half_sub_shr4);                                      \
    } else if ((RBITS) == 2) {                                                 \
      r8 = vcombine_u16(vqrshrun_n_s32(tmp_lo, 2), vqrshrun_n_s32(tmp_hi, 2)); \
      r8 = vqsubq_u16(r8, half_sub_shr2);                                      \
    } else {                                                                   \
      tmp_lo = vsubq_s32(tmp_lo, half_sub_const);                              \
      tmp_hi = vsubq_s32(tmp_hi, half_sub_const);                              \
      int32x4_t rnd = vdupq_n_s32((1 << round_bits) >> 1);                     \
      int32x4_t neg_rb = vdupq_n_s32(-round_bits);                             \
      r8 = vcombine_u16(                                                       \
          vqmovun_s32(vshlq_s32(vaddq_s32(tmp_lo, rnd), neg_rb)),              \
          vqmovun_s32(vshlq_s32(vaddq_s32(tmp_hi, rnd), neg_rb)));             \
    }                                                                          \
    r8 = vminq_u16(r8, max_val);                                               \
    vst1q_u16(d, r8);                                                          \
    d += dst_stride;                                                           \
    d16 += dst16_stride;                                                       \
  } while (0)

#define CWP_1D_FINISH_ROW_4_AVG_NOOFF(res_s32, RBITS)            \
  do {                                                           \
    uint16x4_t d16_raw = vld1_u16(d16);                          \
    int32x4_t d_s32 = vreinterpretq_s32_u32(vmovl_u16(d16_raw)); \
    int32x4_t tmp = vhaddq_s32(d_s32, (res_s32));                \
    uint16x4_t r4;                                               \
    if ((RBITS) == 4) {                                          \
      /* Post-narrow sub: saves 1 vsubq_s32 vs pre-narrow. */    \
      r4 = vqrshrun_n_s32(tmp, 4);                               \
      r4 = vqsub_u16(r4, half_sub_shr4_4);                       \
    } else if ((RBITS) == 2) {                                   \
      r4 = vqrshrun_n_s32(tmp, 2);                               \
      r4 = vqsub_u16(r4, half_sub_shr2_4);                       \
    } else {                                                     \
      tmp = vsubq_s32(tmp, half_sub_const);                      \
      int32x4_t rnd = vdupq_n_s32((1 << round_bits) >> 1);       \
      int32x4_t neg_rb = vdupq_n_s32(-round_bits);               \
      r4 = vqmovun_s32(vshlq_s32(vaddq_s32(tmp, rnd), neg_rb));  \
    }                                                            \
    r4 = vmin_u16(r4, max_val4);                                 \
    vst1_u16(d, r4);                                             \
    d += dst_stride;                                             \
    d16 += dst16_stride;                                         \
  } while (0)

// Filter-only helpers: return raw filter sum (no post-filter rounding).
static inline int32x4_t cwp_convolve_y_6tap_sym_raw_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_sym) {
  int16x8_t a = vaddq_s16(s0, s5);
  int16x8_t b = vaddq_s16(s1, s4);
  int16x8_t c = vaddq_s16(s2, s3);
  int32x4_t acc = vmull_lane_s16(vget_low_s16(a), f_sym, 0);
  int32x4_t ind = vmull_lane_s16(vget_low_s16(b), f_sym, 1);
  acc = vmlal_lane_s16(acc, vget_low_s16(c), f_sym, 2);
  return vaddq_s32(acc, ind);
}

static inline int32x4_t cwp_convolve_y_6tap_sym_raw_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x4_t f_sym) {
  int16x8_t a = vaddq_s16(s0, s5);
  int16x8_t b = vaddq_s16(s1, s4);
  int16x8_t c = vaddq_s16(s2, s3);
  int32x4_t acc = vmull_lane_s16(vget_high_s16(a), f_sym, 0);
  int32x4_t ind = vmull_lane_s16(vget_high_s16(b), f_sym, 1);
  acc = vmlal_lane_s16(acc, vget_high_s16(c), f_sym, 2);
  return vaddq_s32(acc, ind);
}

static inline int32x4_t cwp_convolve_y_6tap_sym_raw_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t f_sym) {
  int16x4_t a = vadd_s16(s0, s5);
  int16x4_t b = vadd_s16(s1, s4);
  int16x4_t c = vadd_s16(s2, s3);
  int32x4_t acc = vmull_lane_s16(a, f_sym, 0);
  int32x4_t ind = vmull_lane_s16(b, f_sym, 1);
  acc = vmlal_lane_s16(acc, c, f_sym, 2);
  return vaddq_s32(acc, ind);
}

// Raw 12-tap helpers: return filter sum before post-filter rounding.
// 12 coefficients in three int16x4_t registers (f0, f1, f2).
static inline int32x4_t cwp_convolve_y_12tap_raw_lo(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t s8,
    const int16x8_t s9, const int16x8_t s10, const int16x8_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2) {
  int32x4_t sum = vmull_lane_s16(vget_low_s16(s0), f0, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f0, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s2), f0, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s3), f0, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s4), f1, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s5), f1, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s6), f1, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s7), f1, 3);
  sum = vmlal_lane_s16(sum, vget_low_s16(s8), f2, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s9), f2, 1);
  sum = vmlal_lane_s16(sum, vget_low_s16(s10), f2, 2);
  sum = vmlal_lane_s16(sum, vget_low_s16(s11), f2, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_12tap_raw_hi(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x8_t s8,
    const int16x8_t s9, const int16x8_t s10, const int16x8_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2) {
  int32x4_t sum = vmull_lane_s16(vget_high_s16(s0), f0, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f0, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s2), f0, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s3), f0, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s4), f1, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s5), f1, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s6), f1, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s7), f1, 3);
  sum = vmlal_lane_s16(sum, vget_high_s16(s8), f2, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s9), f2, 1);
  sum = vmlal_lane_s16(sum, vget_high_s16(s10), f2, 2);
  sum = vmlal_lane_s16(sum, vget_high_s16(s11), f2, 3);
  return sum;
}

static inline int32x4_t cwp_convolve_y_12tap_raw_4(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t s8,
    const int16x4_t s9, const int16x4_t s10, const int16x4_t s11,
    const int16x4_t f0, const int16x4_t f1, const int16x4_t f2) {
  int32x4_t sum = vmull_lane_s16(s0, f0, 0);
  sum = vmlal_lane_s16(sum, s1, f0, 1);
  sum = vmlal_lane_s16(sum, s2, f0, 2);
  sum = vmlal_lane_s16(sum, s3, f0, 3);
  sum = vmlal_lane_s16(sum, s4, f1, 0);
  sum = vmlal_lane_s16(sum, s5, f1, 1);
  sum = vmlal_lane_s16(sum, s6, f1, 2);
  sum = vmlal_lane_s16(sum, s7, f1, 3);
  sum = vmlal_lane_s16(sum, s8, f2, 0);
  sum = vmlal_lane_s16(sum, s9, f2, 1);
  sum = vmlal_lane_s16(sum, s10, f2, 2);
  sum = vmlal_lane_s16(sum, s11, f2, 3);
  return sum;
}

// Raw 2-tap helpers: return filter sum before post-filter rounding.
// Callers apply CWP_Y_POST_FILTER with compile-time ROUND0 for
// immediate-shift instructions instead of variable-shift operands.
static inline int32x4_t cwp_convolve_y_2tap_raw_lo(const int16x8_t s0,
                                                   const int16x8_t s1,
                                                   const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(vget_low_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_low_s16(s1), f, 1);
  return sum;
}

static inline int32x4_t cwp_convolve_y_2tap_raw_hi(const int16x8_t s0,
                                                   const int16x8_t s1,
                                                   const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(vget_high_s16(s0), f, 0);
  sum = vmlal_lane_s16(sum, vget_high_s16(s1), f, 1);
  return sum;
}

static inline int32x4_t cwp_convolve_y_2tap_raw_4(const int16x4_t s0,
                                                  const int16x4_t s1,
                                                  const int16x4_t f) {
  int32x4_t sum = vmull_lane_s16(s0, f, 0);
  sum = vmlal_lane_s16(sum, s1, f, 1);
  return sum;
}

void av2_highbd_cwp_convolve_y_neon(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_y_qn,
                                    ConvolveParams *conv_params, int bd) {
  const int tap_y = get_filter_tap(filter_params_y, subpel_y_qn);

  (void)tap_y;

  CONV_BUF_TYPE *dst16 = conv_params->dst;
  const int dst16_stride = conv_params->dst_stride;
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  const int fo_vert = tap_y / 2 - 1;
  const int16_t *y_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);

  // C ref: res = filter_sum * (1 << bits)
  //        res = ROUND_POWER_OF_TWO(res, round_1) + round_offset
  const int bits = FILTER_BITS - conv_params->round_0;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;

  assert(bits >= 0);
  assert(round_bits >= 0);
  assert(w >= 4 && (w == 4 || (w % 8) == 0));
  assert(h % 4 == 0);

  const int32x4_t bits_shift = vdupq_n_s32(bits);
  const int32x4_t round_const_v = vdupq_n_s32((1 << conv_params->round_1) >> 1);
  const int32x4_t round_shift_v = vdupq_n_s32(-conv_params->round_1);
  const int32x4_t offset_v = vdupq_n_s32(round_offset);
  const int32x4_t fwd_s32 = vdupq_n_s32(conv_params->fwd_offset);
  const int32x4_t sub_const = vdupq_n_s32(round_offset);
  // For MODE=1 offset elision: half of round_offset for halving-add paths
  // where the post-filter skips adding round_offset. See
  // CWP_1D_FINISH_ROW_*_AVG_NOOFF.
  const int32x4_t half_sub_const = vdupq_n_s32(round_offset / 2);
  // Post-narrow subtraction: shift half_sub_const right by RBITS and apply in
  // u16 space after vqrshrun, saving 1 vsubq_s32 per row vs pre-narrow sub.
  // Identity: vqrshrun(x - C, N) = vqsub(vqrshrun(x, N), C >> N) when C is
  // divisible by (1 << N). round_offset is always divisible by 2^(RBITS+1).
  const uint16x8_t half_sub_shr4 = vdupq_n_u16((uint16_t)(round_offset >> 5));
  const uint16x8_t half_sub_shr2 = vdupq_n_u16((uint16_t)(round_offset >> 3));
  const uint16x4_t half_sub_shr4_4 = vdup_n_u16((uint16_t)(round_offset >> 5));
  const uint16x4_t half_sub_shr2_4 = vdup_n_u16((uint16_t)(round_offset >> 3));
  // Post-narrow sub for standard (non-NOOFF) CWP_1D_FINISH_ROW MODE=1:
  // sub_const = round_offset, shifted right by RBITS after narrowing.
  const uint16x8_t sub_shr4 = vdupq_n_u16((uint16_t)(round_offset >> 4));
  const uint16x8_t sub_shr2 = vdupq_n_u16((uint16_t)(round_offset >> 2));
  const uint16x4_t sub_shr4_4 = vdup_n_u16((uint16_t)(round_offset >> 4));
  const uint16x4_t sub_shr2_4 = vdup_n_u16((uint16_t)(round_offset >> 2));
  const uint16x8_t max_val = vdupq_n_u16((uint16_t)((1 << bd) - 1));
  const uint16x4_t max_val4 = vdup_n_u16((uint16_t)((1 << bd) - 1));

  const uint16_t *src_vert = src - fo_vert * src_stride;

  if (tap_y == 2) {
    const int16_t f_arr[4] = { y_filter_ptr[3], y_filter_ptr[4], 0, 0 };
    const int16x4_t f = vld1_s16(f_arr);

    if (w == 4) {
#define CONV_Y_2TAP_4(MODE, RBITS)                                   \
  do {                                                               \
    const int16_t *s = (const int16_t *)src_vert;                    \
    uint16_t *d = dst;                                               \
    CONV_BUF_TYPE *d16 = dst16;                                      \
    int16x4_t s0 = vld1_s16(s);                                      \
    s += src_stride;                                                 \
    for (int y = 0; y < h; y++) {                                    \
      int16x4_t s1 = vld1_s16(s);                                    \
      s += src_stride;                                               \
      int32x4_t res = cwp_convolve_y_2tap_raw_4(s0, s1, f);          \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      s0 = s1;                                                       \
    }                                                                \
  } while (0)
      if (!do_average) {
        // MODE=0: specialize on round_bits for compile-time shifts.
        if (round_bits == 4) {
          CONV_Y_2TAP_4(0, 4);
        } else if (round_bits == 2) {
          CONV_Y_2TAP_4(0, 2);
        } else {
          CONV_Y_2TAP_4(0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_Y_2TAP_4(2, 4);
        } else {
          CONV_Y_2TAP_4(1, 4);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_Y_2TAP_4(2, 2);
        } else {
          CONV_Y_2TAP_4(1, 2);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_Y_2TAP_4(2, 0);
        } else {
          CONV_Y_2TAP_4(1, 0);
        }
      }
#undef CONV_Y_2TAP_4
    } else {
      int col = w;
      do {
#define CONV_Y_2TAP_8(MODE, RBITS)                                  \
  do {                                                              \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));     \
    uint16_t *d = dst + (w - col);                                  \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                         \
    int16x8_t s0 = vld1q_s16(s);                                    \
    s += src_stride;                                                \
    for (int y = 0; y < h; y++) {                                   \
      int16x8_t s1 = vld1q_s16(s);                                  \
      s += src_stride;                                              \
      int32x4_t lo = cwp_convolve_y_2tap_raw_lo(s0, s1, f);         \
      int32x4_t hi = cwp_convolve_y_2tap_raw_hi(s0, s1, f);         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                     \
      s0 = s1;                                                      \
    }                                                               \
  } while (0)
        if (!do_average) {
          // MODE=0: specialize on round_bits for compile-time shifts.
          if (round_bits == 4) {
            CONV_Y_2TAP_8(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_2TAP_8(0, 2);
          } else {
            CONV_Y_2TAP_8(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_2TAP_8(2, 4);
          } else {
            CONV_Y_2TAP_8(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_2TAP_8(2, 2);
          } else {
            CONV_Y_2TAP_8(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_2TAP_8(2, 0);
          } else {
            CONV_Y_2TAP_8(1, 0);
          }
        }
#undef CONV_Y_2TAP_8
        col -= 8;
      } while (col > 0);
    }
  } else if (tap_y == 4) {
    const int16x4_t yf4 = vld1_s16(y_filter_ptr + 2);

    if (w == 4) {
#define CONV_Y_4TAP_4(MODE, RBITS)                                   \
  do {                                                               \
    const int16_t *s = (const int16_t *)src_vert;                    \
    uint16_t *d = dst;                                               \
    CONV_BUF_TYPE *d16 = dst16;                                      \
    int16x4_t s0 = vld1_s16(s);                                      \
    s += src_stride;                                                 \
    int16x4_t s1 = vld1_s16(s);                                      \
    s += src_stride;                                                 \
    int16x4_t s2 = vld1_s16(s);                                      \
    s += src_stride;                                                 \
    int height = h;                                                  \
    do {                                                             \
      int16x4_t s3 = vld1_s16(s);                                    \
      s += src_stride;                                               \
      int16x4_t s4 = vld1_s16(s);                                    \
      s += src_stride;                                               \
      int16x4_t s5 = vld1_s16(s);                                    \
      s += src_stride;                                               \
      int16x4_t s6 = vld1_s16(s);                                    \
      s += src_stride;                                               \
      int32x4_t res;                                                 \
      res = cwp_convolve_y_4tap_raw_4(s0, s1, s2, s3, yf4);          \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      res = cwp_convolve_y_4tap_raw_4(s1, s2, s3, s4, yf4);          \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      res = cwp_convolve_y_4tap_raw_4(s2, s3, s4, s5, yf4);          \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      res = cwp_convolve_y_4tap_raw_4(s3, s4, s5, s6, yf4);          \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                         \
      s0 = s4;                                                       \
      s1 = s5;                                                       \
      s2 = s6;                                                       \
      height -= 4;                                                   \
    } while (height > 0);                                            \
  } while (0)
      if (!do_average) {
        // MODE=0: specialize on round_bits for compile-time shifts.
        // CWP_Y_POST_FILTER uses ROUND0 = 7 - RBITS.
        // For bd=10: round_bits=4, RBITS=4 -> ROUND0=3 -> vrshrq_n_s32(,3).
        // For bd=12: round_bits=2, RBITS=2 -> ROUND0=5 -> vrshrq_n_s32(,5).
        if (round_bits == 4) {
          CONV_Y_4TAP_4(0, 4);
        } else if (round_bits == 2) {
          CONV_Y_4TAP_4(0, 2);
        } else {
          CONV_Y_4TAP_4(0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_Y_4TAP_4(2, 4);
        } else {
          CONV_Y_4TAP_4(1, 4);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_Y_4TAP_4(2, 2);
        } else {
          CONV_Y_4TAP_4(1, 2);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_Y_4TAP_4(2, 0);
        } else {
          CONV_Y_4TAP_4(1, 0);
        }
      }
#undef CONV_Y_4TAP_4
    } else {
      int col = w;
      do {
#define CONV_Y_4TAP_8(MODE, RBITS)                                  \
  do {                                                              \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));     \
    uint16_t *d = dst + (w - col);                                  \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                         \
    int16x8_t s0 = vld1q_s16(s);                                    \
    s += src_stride;                                                \
    int16x8_t s1 = vld1q_s16(s);                                    \
    s += src_stride;                                                \
    int16x8_t s2 = vld1q_s16(s);                                    \
    s += src_stride;                                                \
    int height = h;                                                 \
    do {                                                            \
      int16x8_t s3 = vld1q_s16(s);                                  \
      s += src_stride;                                              \
      int16x8_t s4 = vld1q_s16(s);                                  \
      s += src_stride;                                              \
      int16x8_t s5 = vld1q_s16(s);                                  \
      s += src_stride;                                              \
      int16x8_t s6 = vld1q_s16(s);                                  \
      s += src_stride;                                              \
      int32x4_t lo, hi;                                             \
      lo = cwp_convolve_y_4tap_raw_lo(s0, s1, s2, s3, yf4);         \
      hi = cwp_convolve_y_4tap_raw_hi(s0, s1, s2, s3, yf4);         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                     \
      lo = cwp_convolve_y_4tap_raw_lo(s1, s2, s3, s4, yf4);         \
      hi = cwp_convolve_y_4tap_raw_hi(s1, s2, s3, s4, yf4);         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                     \
      lo = cwp_convolve_y_4tap_raw_lo(s2, s3, s4, s5, yf4);         \
      hi = cwp_convolve_y_4tap_raw_hi(s2, s3, s4, s5, yf4);         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                     \
      lo = cwp_convolve_y_4tap_raw_lo(s3, s4, s5, s6, yf4);         \
      hi = cwp_convolve_y_4tap_raw_hi(s3, s4, s5, s6, yf4);         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v); \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                     \
      s0 = s4;                                                      \
      s1 = s5;                                                      \
      s2 = s6;                                                      \
      height -= 4;                                                  \
    } while (height > 0);                                           \
  } while (0)
        if (!do_average) {
          // MODE=0: specialize on round_bits for compile-time shifts.
          if (round_bits == 4) {
            CONV_Y_4TAP_8(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_4TAP_8(0, 2);
          } else {
            CONV_Y_4TAP_8(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_4TAP_8(2, 4);
          } else {
            CONV_Y_4TAP_8(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_4TAP_8(2, 2);
          } else {
            CONV_Y_4TAP_8(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_4TAP_8(2, 0);
          } else {
            CONV_Y_4TAP_8(1, 0);
          }
        }
#undef CONV_Y_4TAP_8
        col -= 8;
      } while (col > 0);
    }
  } else if (tap_y == 6) {
    // Check for symmetric coefficients (e.g., EIGHTTAP_REGULAR at subpel_qn=8
    // gives {2,-14,76,76,-14,2}). Symmetric folding halves MAC count.
    const int y_sym = (y_filter_ptr[1] == y_filter_ptr[6]) &&
                      (y_filter_ptr[2] == y_filter_ptr[5]) &&
                      (y_filter_ptr[3] == y_filter_ptr[4]);
    if (y_sym) {
      // Pack 3 unique symmetric coefficients into int16x4_t for lane-indexed
      // MLA
      const int16_t yf_sym_arr[4] = { y_filter_ptr[1], y_filter_ptr[2],
                                      y_filter_ptr[3], 0 };
      const int16x4_t yf_sym = vld1_s16(yf_sym_arr);

      if (w == 4) {
#define CONV_Y_6TAP_SYM_4(MODE, RBITS)                                     \
  do {                                                                     \
    const int16_t *s = (const int16_t *)src_vert;                          \
    uint16_t *d = dst;                                                     \
    CONV_BUF_TYPE *d16 = dst16;                                            \
    int16x4_t s0 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s1 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s2 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s3 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s4 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x4_t s5 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s6 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s7 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s8 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int32x4_t res;                                                       \
      res = cwp_convolve_y_6tap_sym_raw_4(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_6tap_sym_raw_4(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_6tap_sym_raw_4(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_6tap_sym_raw_4(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
        if (!do_average) {
          // MODE=0 doesn't use RBITS for blend, but CWP_Y_POST_FILTER uses
          // RBITS to derive ROUND0 = 7 - RBITS. Passing RBITS matching
          // round_bits gives a compile-time constant shift (vrshrq_n_s32)
          // instead of the 4-op variable-shift fallback when RBITS=0.
          if (round_bits == 4) {
            CONV_Y_6TAP_SYM_4(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_6TAP_SYM_4(0, 2);
          } else {
            CONV_Y_6TAP_SYM_4(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_SYM_4(2, 4);
          } else {
// Offset-elision variant for MODE=1: skip offset_v add in post-filter,
// use half_sub_const in finish row. Saves 1 vaddq per 4-pixel row.
#define CONV_Y_6TAP_SYM_4_AVG(RBITS)                                       \
  do {                                                                     \
    const int16_t *s = (const int16_t *)src_vert;                          \
    uint16_t *d = dst;                                                     \
    CONV_BUF_TYPE *d16 = dst16;                                            \
    int16x4_t s0 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s1 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s2 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s3 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s4 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x4_t s5 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s6 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s7 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s8 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int32x4_t res;                                                       \
      res = cwp_convolve_y_6tap_sym_raw_4(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(res, (RBITS) ? (7 - (RBITS)) : 0);           \
      CWP_1D_FINISH_ROW_4_AVG_NOOFF(res, RBITS);                           \
      res = cwp_convolve_y_6tap_sym_raw_4(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(res, (RBITS) ? (7 - (RBITS)) : 0);           \
      CWP_1D_FINISH_ROW_4_AVG_NOOFF(res, RBITS);                           \
      res = cwp_convolve_y_6tap_sym_raw_4(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(res, (RBITS) ? (7 - (RBITS)) : 0);           \
      CWP_1D_FINISH_ROW_4_AVG_NOOFF(res, RBITS);                           \
      res = cwp_convolve_y_6tap_sym_raw_4(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(res, (RBITS) ? (7 - (RBITS)) : 0);           \
      CWP_1D_FINISH_ROW_4_AVG_NOOFF(res, RBITS);                           \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
            CONV_Y_6TAP_SYM_4_AVG(4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_SYM_4(2, 2);
          } else {
            CONV_Y_6TAP_SYM_4_AVG(2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_SYM_4(2, 0);
          } else {
            CONV_Y_6TAP_SYM_4_AVG(0);
          }
        }
#undef CONV_Y_6TAP_SYM_4_AVG
#undef CONV_Y_6TAP_SYM_4
      } else {
        int col = w;
        do {
#define CONV_Y_6TAP_SYM_8(MODE, RBITS)                                     \
  do {                                                                     \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));            \
    uint16_t *d = dst + (w - col);                                         \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                                \
    int16x8_t s0 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s1 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s2 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s3 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s4 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x8_t s5 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s6 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s7 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s8 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int32x4_t lo, hi;                                                    \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
          if (!do_average) {
            if (round_bits == 4) {
              CONV_Y_6TAP_SYM_8(0, 4);
            } else if (round_bits == 2) {
              CONV_Y_6TAP_SYM_8(0, 2);
            } else {
              CONV_Y_6TAP_SYM_8(0, 0);
            }
          } else if (round_bits == 4) {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_SYM_8(2, 4);
            } else {
// Offset-elision variant for MODE=1 w>=8: skip offset_v add in post-filter,
// use half_sub_const in finish row. Saves 2 vaddq per 8-pixel row.
#define CONV_Y_6TAP_SYM_8_AVG(RBITS)                                       \
  do {                                                                     \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));            \
    uint16_t *d = dst + (w - col);                                         \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                                \
    int16x8_t s0 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s1 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s2 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s3 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s4 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x8_t s5 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s6 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s7 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s8 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int32x4_t lo, hi;                                                    \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(lo, (RBITS) ? (7 - (RBITS)) : 0);            \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s0, s1, s2, s3, s4, s5, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(hi, (RBITS) ? (7 - (RBITS)) : 0);            \
      CWP_1D_FINISH_ROW_8_AVG_NOOFF(lo, hi, RBITS);                        \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(lo, (RBITS) ? (7 - (RBITS)) : 0);            \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s1, s2, s3, s4, s5, s6, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(hi, (RBITS) ? (7 - (RBITS)) : 0);            \
      CWP_1D_FINISH_ROW_8_AVG_NOOFF(lo, hi, RBITS);                        \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(lo, (RBITS) ? (7 - (RBITS)) : 0);            \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s2, s3, s4, s5, s6, s7, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(hi, (RBITS) ? (7 - (RBITS)) : 0);            \
      CWP_1D_FINISH_ROW_8_AVG_NOOFF(lo, hi, RBITS);                        \
      lo = cwp_convolve_y_6tap_sym_raw_lo(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(lo, (RBITS) ? (7 - (RBITS)) : 0);            \
      hi = cwp_convolve_y_6tap_sym_raw_hi(s3, s4, s5, s6, s7, s8, yf_sym); \
      CWP_Y_POST_FILTER_NOOFF(hi, (RBITS) ? (7 - (RBITS)) : 0);            \
      CWP_1D_FINISH_ROW_8_AVG_NOOFF(lo, hi, RBITS);                        \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
              CONV_Y_6TAP_SYM_8_AVG(4);
            }
          } else if (round_bits == 2) {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_SYM_8(2, 2);
            } else {
              CONV_Y_6TAP_SYM_8_AVG(2);
            }
          } else {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_SYM_8(2, 0);
            } else {
              CONV_Y_6TAP_SYM_8_AVG(0);
            }
          }
#undef CONV_Y_6TAP_SYM_8_AVG
#undef CONV_Y_6TAP_SYM_8
          col -= 8;
        } while (col > 0);
      }
    } else {
      // Asymmetric 6-tap fallback
      const int16x4_t yf6_lo = vld1_s16(y_filter_ptr + 1);
      const int16_t yf6_hi_arr[4] = { y_filter_ptr[5], y_filter_ptr[6], 0, 0 };
      const int16x4_t yf6_hi = vld1_s16(yf6_hi_arr);

      if (w == 4) {
#define CONV_Y_6TAP_4(MODE, RBITS)                                          \
  do {                                                                      \
    const int16_t *s = (const int16_t *)src_vert;                           \
    uint16_t *d = dst;                                                      \
    CONV_BUF_TYPE *d16 = dst16;                                             \
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
    int height = h;                                                         \
    do {                                                                    \
      int16x4_t s5 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s6 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s7 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int16x4_t s8 = vld1_s16(s);                                           \
      s += src_stride;                                                      \
      int32x4_t res;                                                        \
      res = cwp_convolve_y_6tap_4(s0, s1, s2, s3, s4, s5, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                \
      res = cwp_convolve_y_6tap_4(s1, s2, s3, s4, s5, s6, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                \
      res = cwp_convolve_y_6tap_4(s2, s3, s4, s5, s6, s7, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                \
      res = cwp_convolve_y_6tap_4(s3, s4, s5, s6, s7, s8, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                \
      s0 = s4;                                                              \
      s1 = s5;                                                              \
      s2 = s6;                                                              \
      s3 = s7;                                                              \
      s4 = s8;                                                              \
      height -= 4;                                                          \
    } while (height > 0);                                                   \
  } while (0)
        if (!do_average) {
          CONV_Y_6TAP_4(0, 0);
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_4(2, 4);
          } else {
            CONV_Y_6TAP_4(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_4(2, 2);
          } else {
            CONV_Y_6TAP_4(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_6TAP_4(2, 0);
          } else {
            CONV_Y_6TAP_4(1, 0);
          }
        }
#undef CONV_Y_6TAP_4
      } else {
        int col = w;
        do {
#define CONV_Y_6TAP_8(MODE, RBITS)                                          \
  do {                                                                      \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));             \
    uint16_t *d = dst + (w - col);                                          \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                                 \
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
    int height = h;                                                         \
    do {                                                                    \
      int16x8_t s5 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s6 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s7 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int16x8_t s8 = vld1q_s16(s);                                          \
      s += src_stride;                                                      \
      int32x4_t lo, hi;                                                     \
      lo = cwp_convolve_y_6tap_lo(s0, s1, s2, s3, s4, s5, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      hi = cwp_convolve_y_6tap_hi(s0, s1, s2, s3, s4, s5, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                             \
      lo = cwp_convolve_y_6tap_lo(s1, s2, s3, s4, s5, s6, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      hi = cwp_convolve_y_6tap_hi(s1, s2, s3, s4, s5, s6, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                             \
      lo = cwp_convolve_y_6tap_lo(s2, s3, s4, s5, s6, s7, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      hi = cwp_convolve_y_6tap_hi(s2, s3, s4, s5, s6, s7, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                             \
      lo = cwp_convolve_y_6tap_lo(s3, s4, s5, s6, s7, s8, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      hi = cwp_convolve_y_6tap_hi(s3, s4, s5, s6, s7, s8, yf6_lo, yf6_hi,   \
                                  bits_shift, round_const_v, round_shift_v, \
                                  offset_v);                                \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                             \
      s0 = s4;                                                              \
      s1 = s5;                                                              \
      s2 = s6;                                                              \
      s3 = s7;                                                              \
      s4 = s8;                                                              \
      height -= 4;                                                          \
    } while (height > 0);                                                   \
  } while (0)
          if (!do_average) {
            CONV_Y_6TAP_8(0, 0);
          } else if (round_bits == 4) {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_8(2, 4);
            } else {
              CONV_Y_6TAP_8(1, 4);
            }
          } else if (round_bits == 2) {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_8(2, 2);
            } else {
              CONV_Y_6TAP_8(1, 2);
            }
          } else {
            if (use_wtd_comp_avg) {
              CONV_Y_6TAP_8(2, 0);
            } else {
              CONV_Y_6TAP_8(1, 0);
            }
          }
#undef CONV_Y_6TAP_8
          col -= 8;
        } while (col > 0);
      }
    }
  } else if (tap_y == 12) {
    const int16x4_t f0 = vld1_s16(y_filter_ptr);
    const int16x4_t f1 = vld1_s16(y_filter_ptr + 4);
    const int16x4_t f2 = vld1_s16(y_filter_ptr + 8);

    if (w == 4) {
#define CONV_Y_12TAP_4(MODE, RBITS)                                      \
  do {                                                                   \
    const int16_t *s = (const int16_t *)src_vert;                        \
    uint16_t *d = dst;                                                   \
    CONV_BUF_TYPE *d16 = dst16;                                          \
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
    int16x4_t s5 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s6 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s7 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s8 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s9 = vld1_s16(s);                                          \
    s += src_stride;                                                     \
    int16x4_t s10 = vld1_s16(s);                                         \
    s += src_stride;                                                     \
    int height = h;                                                      \
    do {                                                                 \
      int16x4_t s11 = vld1_s16(s);                                       \
      s += src_stride;                                                   \
      int32x4_t res = cwp_convolve_y_12tap_raw_4(                        \
          s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, f0, f1, f2); \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);     \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                             \
      s0 = s1;                                                           \
      s1 = s2;                                                           \
      s2 = s3;                                                           \
      s3 = s4;                                                           \
      s4 = s5;                                                           \
      s5 = s6;                                                           \
      s6 = s7;                                                           \
      s7 = s8;                                                           \
      s8 = s9;                                                           \
      s9 = s10;                                                          \
      s10 = s11;                                                         \
      height--;                                                          \
    } while (height > 0);                                                \
  } while (0)
      if (!do_average) {
        if (round_bits == 4) {
          CONV_Y_12TAP_4(0, 4);
        } else if (round_bits == 2) {
          CONV_Y_12TAP_4(0, 2);
        } else {
          CONV_Y_12TAP_4(0, 0);
        }
      } else if (round_bits == 4) {
        if (use_wtd_comp_avg) {
          CONV_Y_12TAP_4(2, 4);
        } else {
          CONV_Y_12TAP_4(1, 4);
        }
      } else if (round_bits == 2) {
        if (use_wtd_comp_avg) {
          CONV_Y_12TAP_4(2, 2);
        } else {
          CONV_Y_12TAP_4(1, 2);
        }
      } else {
        if (use_wtd_comp_avg) {
          CONV_Y_12TAP_4(2, 0);
        } else {
          CONV_Y_12TAP_4(1, 0);
        }
      }
#undef CONV_Y_12TAP_4
    } else {
      int col = w;
      do {
#define CONV_Y_12TAP_8(MODE, RBITS)                                      \
  do {                                                                   \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));          \
    uint16_t *d = dst + (w - col);                                       \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                              \
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
    int16x8_t s5 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s6 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s7 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s8 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s9 = vld1q_s16(s);                                         \
    s += src_stride;                                                     \
    int16x8_t s10 = vld1q_s16(s);                                        \
    s += src_stride;                                                     \
    int height = h;                                                      \
    do {                                                                 \
      int16x8_t s11 = vld1q_s16(s);                                      \
      s += src_stride;                                                   \
      int32x4_t lo = cwp_convolve_y_12tap_raw_lo(                        \
          s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, f0, f1, f2); \
      int32x4_t hi = cwp_convolve_y_12tap_raw_hi(                        \
          s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, f0, f1, f2); \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);      \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);      \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                          \
      s0 = s1;                                                           \
      s1 = s2;                                                           \
      s2 = s3;                                                           \
      s3 = s4;                                                           \
      s4 = s5;                                                           \
      s5 = s6;                                                           \
      s6 = s7;                                                           \
      s7 = s8;                                                           \
      s8 = s9;                                                           \
      s9 = s10;                                                          \
      s10 = s11;                                                         \
      height--;                                                          \
    } while (height > 0);                                                \
  } while (0)
        if (!do_average) {
          if (round_bits == 4) {
            CONV_Y_12TAP_8(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_12TAP_8(0, 2);
          } else {
            CONV_Y_12TAP_8(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_12TAP_8(2, 4);
          } else {
            CONV_Y_12TAP_8(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_12TAP_8(2, 2);
          } else {
            CONV_Y_12TAP_8(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_12TAP_8(2, 0);
          } else {
            CONV_Y_12TAP_8(1, 0);
          }
        }
#undef CONV_Y_12TAP_8
        col -= 8;
      } while (col > 0);
    }
  } else {
    // 8-tap
    assert(tap_y == 8);
    const int16x8_t yf8 = vld1q_s16(y_filter_ptr);

    // Check for symmetric coefficients: f[0]==f[7], f[1]==f[6], f[2]==f[5],
    // f[3]==f[4]. MULTITAP_SHARP at subpel_qn=8 has
    // {-4,12,-24,80,80,-24,12,-4}. Symmetric folding halves MAC count (4 MACs
    // instead of 8).
    const int y_sym_8 = (y_filter_ptr[0] == y_filter_ptr[7]) &&
                        (y_filter_ptr[1] == y_filter_ptr[6]) &&
                        (y_filter_ptr[2] == y_filter_ptr[5]) &&
                        (y_filter_ptr[3] == y_filter_ptr[4]);
    if (y_sym_8) {
      // Pack 4 unique symmetric coefficients into int16x4_t for lane-indexed
      // MLA: {f[0], f[1], f[2], f[3]}
      const int16_t yf_sym_arr[4] = { y_filter_ptr[0], y_filter_ptr[1],
                                      y_filter_ptr[2], y_filter_ptr[3] };
      const int16x4_t yf_sym = vld1_s16(yf_sym_arr);

      if (w == 4) {
#define CONV_Y_8TAP_SYM_4(MODE, RBITS)                                     \
  do {                                                                     \
    const int16_t *s = (const int16_t *)src_vert;                          \
    uint16_t *d = dst;                                                     \
    CONV_BUF_TYPE *d16 = dst16;                                            \
    int16x4_t s0 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s1 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s2 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s3 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s4 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s5 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int16x4_t s6 = vld1_s16(s);                                            \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x4_t s7 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s8 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s9 = vld1_s16(s);                                          \
      s += src_stride;                                                     \
      int16x4_t s10 = vld1_s16(s);                                         \
      s += src_stride;                                                     \
      int32x4_t res;                                                       \
      res = cwp_convolve_y_8tap_sym_raw_4(s0, s1, s2, s3, s4, s5, s6, s7,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_8tap_sym_raw_4(s1, s2, s3, s4, s5, s6, s7, s8,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_8tap_sym_raw_4(s2, s3, s4, s5, s6, s7, s8, s9,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      res = cwp_convolve_y_8tap_sym_raw_4(s3, s4, s5, s6, s7, s8, s9, s10, \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);       \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                               \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      s5 = s9;                                                             \
      s6 = s10;                                                            \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
        if (!do_average) {
          if (round_bits == 4) {
            CONV_Y_8TAP_SYM_4(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_8TAP_SYM_4(0, 2);
          } else {
            CONV_Y_8TAP_SYM_4(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_SYM_4(2, 4);
          } else {
            CONV_Y_8TAP_SYM_4(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_SYM_4(2, 2);
          } else {
            CONV_Y_8TAP_SYM_4(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_SYM_4(2, 0);
          } else {
            CONV_Y_8TAP_SYM_4(1, 0);
          }
        }
#undef CONV_Y_8TAP_SYM_4
      } else {
        int col = w;
        do {
#define CONV_Y_8TAP_SYM_8(MODE, RBITS)                                     \
  do {                                                                     \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));            \
    uint16_t *d = dst + (w - col);                                         \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                                \
    int16x8_t s0 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s1 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s2 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s3 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s4 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s5 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int16x8_t s6 = vld1q_s16(s);                                           \
    s += src_stride;                                                       \
    int height = h;                                                        \
    do {                                                                   \
      int16x8_t s7 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s8 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s9 = vld1q_s16(s);                                         \
      s += src_stride;                                                     \
      int16x8_t s10 = vld1q_s16(s);                                        \
      s += src_stride;                                                     \
      int32x4_t lo, hi;                                                    \
      lo = cwp_convolve_y_8tap_sym_raw_lo(s0, s1, s2, s3, s4, s5, s6, s7,  \
                                          yf_sym);                         \
      hi = cwp_convolve_y_8tap_sym_raw_hi(s0, s1, s2, s3, s4, s5, s6, s7,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_8tap_sym_raw_lo(s1, s2, s3, s4, s5, s6, s7, s8,  \
                                          yf_sym);                         \
      hi = cwp_convolve_y_8tap_sym_raw_hi(s1, s2, s3, s4, s5, s6, s7, s8,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_8tap_sym_raw_lo(s2, s3, s4, s5, s6, s7, s8, s9,  \
                                          yf_sym);                         \
      hi = cwp_convolve_y_8tap_sym_raw_hi(s2, s3, s4, s5, s6, s7, s8, s9,  \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      lo = cwp_convolve_y_8tap_sym_raw_lo(s3, s4, s5, s6, s7, s8, s9, s10, \
                                          yf_sym);                         \
      hi = cwp_convolve_y_8tap_sym_raw_hi(s3, s4, s5, s6, s7, s8, s9, s10, \
                                          yf_sym);                         \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);        \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                            \
      s0 = s4;                                                             \
      s1 = s5;                                                             \
      s2 = s6;                                                             \
      s3 = s7;                                                             \
      s4 = s8;                                                             \
      s5 = s9;                                                             \
      s6 = s10;                                                            \
      height -= 4;                                                         \
    } while (height > 0);                                                  \
  } while (0)
          if (!do_average) {
            if (round_bits == 4) {
              CONV_Y_8TAP_SYM_8(0, 4);
            } else if (round_bits == 2) {
              CONV_Y_8TAP_SYM_8(0, 2);
            } else {
              CONV_Y_8TAP_SYM_8(0, 0);
            }
          } else if (round_bits == 4) {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_SYM_8(2, 4);
            } else {
              CONV_Y_8TAP_SYM_8(1, 4);
            }
          } else if (round_bits == 2) {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_SYM_8(2, 2);
            } else {
              CONV_Y_8TAP_SYM_8(1, 2);
            }
          } else {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_SYM_8(2, 0);
            } else {
              CONV_Y_8TAP_SYM_8(1, 0);
            }
          }
#undef CONV_Y_8TAP_SYM_8
          col -= 8;
        } while (col > 0);
      }
    } else {
      // Asymmetric 8-tap fallback
      const int16x4_t f_lo = vget_low_s16(yf8);
      const int16x4_t f_hi = vget_high_s16(yf8);

      if (w == 4) {
#define CONV_Y_8TAP_4(MODE, RBITS)                                           \
  do {                                                                       \
    const int16_t *s = (const int16_t *)src_vert;                            \
    uint16_t *d = dst;                                                       \
    CONV_BUF_TYPE *d16 = dst16;                                              \
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
    int16x4_t s5 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int16x4_t s6 = vld1_s16(s);                                              \
    s += src_stride;                                                         \
    int height = h;                                                          \
    do {                                                                     \
      int16x4_t s7 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s8 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s9 = vld1_s16(s);                                            \
      s += src_stride;                                                       \
      int16x4_t s10 = vld1_s16(s);                                           \
      s += src_stride;                                                       \
      int32x4_t res;                                                         \
      res = cwp_convolve_y_8tap_raw_4(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);         \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      res = cwp_convolve_y_8tap_raw_4(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);         \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      res = cwp_convolve_y_8tap_raw_4(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);         \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      res = cwp_convolve_y_8tap_raw_4(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(res, (RBITS) ? (7 - (RBITS)) : 0, offset_v);         \
      CWP_1D_FINISH_ROW_4(res, MODE, RBITS);                                 \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      s3 = s7;                                                               \
      s4 = s8;                                                               \
      s5 = s9;                                                               \
      s6 = s10;                                                              \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)
        if (!do_average) {
          if (round_bits == 4) {
            CONV_Y_8TAP_4(0, 4);
          } else if (round_bits == 2) {
            CONV_Y_8TAP_4(0, 2);
          } else {
            CONV_Y_8TAP_4(0, 0);
          }
        } else if (round_bits == 4) {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_4(2, 4);
          } else {
            CONV_Y_8TAP_4(1, 4);
          }
        } else if (round_bits == 2) {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_4(2, 2);
          } else {
            CONV_Y_8TAP_4(1, 2);
          }
        } else {
          if (use_wtd_comp_avg) {
            CONV_Y_8TAP_4(2, 0);
          } else {
            CONV_Y_8TAP_4(1, 0);
          }
        }
#undef CONV_Y_8TAP_4
      } else {
        int col = w;
        do {
#define CONV_Y_8TAP_8(MODE, RBITS)                                           \
  do {                                                                       \
    const int16_t *s = (const int16_t *)(src_vert + (w - col));              \
    uint16_t *d = dst + (w - col);                                           \
    CONV_BUF_TYPE *d16 = dst16 + (w - col);                                  \
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
    int16x8_t s5 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int16x8_t s6 = vld1q_s16(s);                                             \
    s += src_stride;                                                         \
    int height = h;                                                          \
    do {                                                                     \
      int16x8_t s7 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s8 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s9 = vld1q_s16(s);                                           \
      s += src_stride;                                                       \
      int16x8_t s10 = vld1q_s16(s);                                          \
      s += src_stride;                                                       \
      int32x4_t lo, hi;                                                      \
      lo = cwp_convolve_y_8tap_raw_lo(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                      f_hi);                                 \
      hi = cwp_convolve_y_8tap_raw_hi(s0, s1, s2, s3, s4, s5, s6, s7, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                              \
      lo = cwp_convolve_y_8tap_raw_lo(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                      f_hi);                                 \
      hi = cwp_convolve_y_8tap_raw_hi(s1, s2, s3, s4, s5, s6, s7, s8, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                              \
      lo = cwp_convolve_y_8tap_raw_lo(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                      f_hi);                                 \
      hi = cwp_convolve_y_8tap_raw_hi(s2, s3, s4, s5, s6, s7, s8, s9, f_lo,  \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                              \
      lo = cwp_convolve_y_8tap_raw_lo(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                      f_hi);                                 \
      hi = cwp_convolve_y_8tap_raw_hi(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, \
                                      f_hi);                                 \
      CWP_Y_POST_FILTER(lo, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_Y_POST_FILTER(hi, (RBITS) ? (7 - (RBITS)) : 0, offset_v);          \
      CWP_1D_FINISH_ROW_8(lo, hi, MODE, RBITS);                              \
      s0 = s4;                                                               \
      s1 = s5;                                                               \
      s2 = s6;                                                               \
      s3 = s7;                                                               \
      s4 = s8;                                                               \
      s5 = s9;                                                               \
      s6 = s10;                                                              \
      height -= 4;                                                           \
    } while (height > 0);                                                    \
  } while (0)
          if (!do_average) {
            if (round_bits == 4) {
              CONV_Y_8TAP_8(0, 4);
            } else if (round_bits == 2) {
              CONV_Y_8TAP_8(0, 2);
            } else {
              CONV_Y_8TAP_8(0, 0);
            }
          } else if (round_bits == 4) {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_8(2, 4);
            } else {
              CONV_Y_8TAP_8(1, 4);
            }
          } else if (round_bits == 2) {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_8(2, 2);
            } else {
              CONV_Y_8TAP_8(1, 2);
            }
          } else {
            if (use_wtd_comp_avg) {
              CONV_Y_8TAP_8(2, 0);
            } else {
              CONV_Y_8TAP_8(1, 0);
            }
          }
#undef CONV_Y_8TAP_8
          col -= 8;
        } while (col > 0);
      }
    }
  }
}
