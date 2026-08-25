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
#include "av2/common/filter.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"

void av2_highbd_convolve_x_sr_neon(const uint16_t *src, int src_stride,
                                   uint16_t *dst, int dst_stride, int w, int h,
                                   const InterpFilterParams *filter_params_x,
                                   const int subpel_x_qn,
                                   ConvolveParams *conv_params, int bd) {
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const int bits = FILTER_BITS - conv_params->round_0;
  assert(bits >= 0);

  const int16_t *x_filter = av2_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);

  const int32x4_t round0_shift = vdupq_n_s32(-conv_params->round_0);
  const int32x4_t bits_shift = vdupq_n_s32(-bits);
  const int16x8_t zero_s16 = vdupq_n_s16(0);
  const int16x8_t max_val = vdupq_n_s16((1 << bd) - 1);

  const int16x8_t filt = vld1q_s16(x_filter);
  const int16x4_t f_lo = vget_low_s16(filt);
  const int16x4_t f_hi = vget_high_s16(filt);

  for (int y = 0; y < h; ++y) {
    const int16_t *s = (const int16_t *)src + y * src_stride - fo_horiz;
    uint16_t *d = dst + y * dst_stride;
    int x = 0;

    for (; x + 8 <= w; x += 8) {
      int16x8_t s0 = vld1q_s16(s + x + 0);
      int16x8_t s1 = vld1q_s16(s + x + 1);
      int16x8_t s2 = vld1q_s16(s + x + 2);
      int16x8_t s3 = vld1q_s16(s + x + 3);
      int16x8_t s4 = vld1q_s16(s + x + 4);
      int16x8_t s5 = vld1q_s16(s + x + 5);
      int16x8_t s6 = vld1q_s16(s + x + 6);
      int16x8_t s7 = vld1q_s16(s + x + 7);

      int32x4_t sum_lo =
          vmlal_lane_s16(vdupq_n_s32(0), vget_low_s16(s0), f_lo, 0);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s1), f_lo, 1);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s2), f_lo, 2);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s3), f_lo, 3);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s4), f_hi, 0);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s5), f_hi, 1);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s6), f_hi, 2);
      sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(s7), f_hi, 3);

      int32x4_t sum_hi =
          vmlal_lane_s16(vdupq_n_s32(0), vget_high_s16(s0), f_lo, 0);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s1), f_lo, 1);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s2), f_lo, 2);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s3), f_lo, 3);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s4), f_hi, 0);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s5), f_hi, 1);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s6), f_hi, 2);
      sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(s7), f_hi, 3);

      sum_lo = vrshlq_s32(sum_lo, round0_shift);
      sum_hi = vrshlq_s32(sum_hi, round0_shift);
      sum_lo = vrshlq_s32(sum_lo, bits_shift);
      sum_hi = vrshlq_s32(sum_hi, bits_shift);
      int16x8_t res = vcombine_s16(vqmovn_s32(sum_lo), vqmovn_s32(sum_hi));
      res = vmaxq_s16(res, zero_s16);
      res = vminq_s16(res, max_val);
      vst1q_u16(d + x, vreinterpretq_u16_s16(res));
    }

    if (x + 4 <= w) {
      int16x4_t s0 = vld1_s16(s + x + 0);
      int16x4_t s1 = vld1_s16(s + x + 1);
      int16x4_t s2 = vld1_s16(s + x + 2);
      int16x4_t s3 = vld1_s16(s + x + 3);
      int16x4_t s4 = vld1_s16(s + x + 4);
      int16x4_t s5 = vld1_s16(s + x + 5);
      int16x4_t s6 = vld1_s16(s + x + 6);
      int16x4_t s7 = vld1_s16(s + x + 7);

      int32x4_t sum = vmlal_lane_s16(vdupq_n_s32(0), s0, f_lo, 0);
      sum = vmlal_lane_s16(sum, s1, f_lo, 1);
      sum = vmlal_lane_s16(sum, s2, f_lo, 2);
      sum = vmlal_lane_s16(sum, s3, f_lo, 3);
      sum = vmlal_lane_s16(sum, s4, f_hi, 0);
      sum = vmlal_lane_s16(sum, s5, f_hi, 1);
      sum = vmlal_lane_s16(sum, s6, f_hi, 2);
      sum = vmlal_lane_s16(sum, s7, f_hi, 3);

      sum = vrshlq_s32(sum, round0_shift);
      sum = vrshlq_s32(sum, bits_shift);
      int16x4_t res = vqmovn_s32(sum);
      res = vmax_s16(res, vget_low_s16(zero_s16));
      res = vmin_s16(res, vget_low_s16(max_val));
      vst1_u16(d + x, vreinterpret_u16_s16(res));
      x += 4;
    }

    for (; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_x->taps; ++k) {
        res += x_filter[k] * s[x + k];
      }
      res = ROUND_POWER_OF_TWO(res, conv_params->round_0);
      d[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(res, bits), bd);
    }
  }
}

void av2_highbd_convolve_y_sr_neon(const uint16_t *src, int src_stride,
                                   uint16_t *dst, int dst_stride, int w, int h,
                                   const InterpFilterParams *filter_params_y,
                                   const int subpel_y_qn, int bd) {
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int16_t *y_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);

  if (w < 4 || h < 4 || (h % 4) != 0) {
    for (int y = 0; y < h; ++y) {
      uint16_t *d = dst + y * dst_stride;
      for (int x = 0; x < w; ++x) {
        int32_t res = 0;
        for (int k = 0; k < filter_params_y->taps; ++k) {
          res += y_filter_ptr[k] *
                 ((const int16_t *)src)[(y - fo_vert + k) * src_stride + x];
        }
        d[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(res, FILTER_BITS), bd);
      }
    }
    return;
  }
  assert(h % 4 == 0);

  const int16x8_t filter = vld1q_s16(y_filter_ptr);
  const int16x4_t f_lo = vget_low_s16(filter);
  const int16x4_t f_hi = vget_high_s16(filter);
  const int32x4_t round_shift = vdupq_n_s32(-FILTER_BITS);
  const int16x8_t max_val_q = vdupq_n_s16((1 << bd) - 1);
  const int16x4_t max_val_d = vdup_n_s16((1 << bd) - 1);

  const int16_t *src_col = (const int16_t *)src - fo_vert * src_stride;
  int col = 0;

  for (; col + 8 <= w; col += 8) {
    const int16_t *s = src_col + col;
    uint16_t *d = dst + col;

    int16x8_t s0 = vld1q_s16(s + 0 * src_stride);
    int16x8_t s1 = vld1q_s16(s + 1 * src_stride);
    int16x8_t s2 = vld1q_s16(s + 2 * src_stride);
    int16x8_t s3 = vld1q_s16(s + 3 * src_stride);
    int16x8_t s4 = vld1q_s16(s + 4 * src_stride);
    int16x8_t s5 = vld1q_s16(s + 5 * src_stride);
    int16x8_t s6 = vld1q_s16(s + 6 * src_stride);
    s += 7 * src_stride;

    int height = h;
    do {
      int16x8_t s7 = vld1q_s16(s + 0 * src_stride);
      int16x8_t s8 = vld1q_s16(s + 1 * src_stride);
      int16x8_t s9 = vld1q_s16(s + 2 * src_stride);
      int16x8_t s10 = vld1q_s16(s + 3 * src_stride);
      s += 4 * src_stride;

      int32x4_t lo, hi;
      int16x8_t res;

      lo = vmlal_lane_s16(vdupq_n_s32(0), vget_low_s16(s0), f_lo, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s1), f_lo, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s2), f_lo, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s3), f_lo, 3);
      lo = vmlal_lane_s16(lo, vget_low_s16(s4), f_hi, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s5), f_hi, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s6), f_hi, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s7), f_hi, 3);
      hi = vmlal_lane_s16(vdupq_n_s32(0), vget_high_s16(s0), f_lo, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s1), f_lo, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s2), f_lo, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s3), f_lo, 3);
      hi = vmlal_lane_s16(hi, vget_high_s16(s4), f_hi, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s5), f_hi, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s6), f_hi, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s7), f_hi, 3);
      lo = vrshlq_s32(lo, round_shift);
      hi = vrshlq_s32(hi, round_shift);
      res = vcombine_s16(vqmovn_s32(lo), vqmovn_s32(hi));
      res = vmaxq_s16(res, vdupq_n_s16(0));
      res = vminq_s16(res, max_val_q);
      vst1q_u16(d, vreinterpretq_u16_s16(res));
      d += dst_stride;

      lo = vmlal_lane_s16(vdupq_n_s32(0), vget_low_s16(s1), f_lo, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s2), f_lo, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s3), f_lo, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s4), f_lo, 3);
      lo = vmlal_lane_s16(lo, vget_low_s16(s5), f_hi, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s6), f_hi, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s7), f_hi, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s8), f_hi, 3);
      hi = vmlal_lane_s16(vdupq_n_s32(0), vget_high_s16(s1), f_lo, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s2), f_lo, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s3), f_lo, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s4), f_lo, 3);
      hi = vmlal_lane_s16(hi, vget_high_s16(s5), f_hi, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s6), f_hi, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s7), f_hi, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s8), f_hi, 3);
      lo = vrshlq_s32(lo, round_shift);
      hi = vrshlq_s32(hi, round_shift);
      res = vcombine_s16(vqmovn_s32(lo), vqmovn_s32(hi));
      res = vmaxq_s16(res, vdupq_n_s16(0));
      res = vminq_s16(res, max_val_q);
      vst1q_u16(d, vreinterpretq_u16_s16(res));
      d += dst_stride;

      lo = vmlal_lane_s16(vdupq_n_s32(0), vget_low_s16(s2), f_lo, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s3), f_lo, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s4), f_lo, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s5), f_lo, 3);
      lo = vmlal_lane_s16(lo, vget_low_s16(s6), f_hi, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s7), f_hi, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s8), f_hi, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s9), f_hi, 3);
      hi = vmlal_lane_s16(vdupq_n_s32(0), vget_high_s16(s2), f_lo, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s3), f_lo, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s4), f_lo, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s5), f_lo, 3);
      hi = vmlal_lane_s16(hi, vget_high_s16(s6), f_hi, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s7), f_hi, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s8), f_hi, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s9), f_hi, 3);
      lo = vrshlq_s32(lo, round_shift);
      hi = vrshlq_s32(hi, round_shift);
      res = vcombine_s16(vqmovn_s32(lo), vqmovn_s32(hi));
      res = vmaxq_s16(res, vdupq_n_s16(0));
      res = vminq_s16(res, max_val_q);
      vst1q_u16(d, vreinterpretq_u16_s16(res));
      d += dst_stride;

      lo = vmlal_lane_s16(vdupq_n_s32(0), vget_low_s16(s3), f_lo, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s4), f_lo, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s5), f_lo, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s6), f_lo, 3);
      lo = vmlal_lane_s16(lo, vget_low_s16(s7), f_hi, 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s8), f_hi, 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s9), f_hi, 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s10), f_hi, 3);
      hi = vmlal_lane_s16(vdupq_n_s32(0), vget_high_s16(s3), f_lo, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s4), f_lo, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s5), f_lo, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s6), f_lo, 3);
      hi = vmlal_lane_s16(hi, vget_high_s16(s7), f_hi, 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s8), f_hi, 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s9), f_hi, 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s10), f_hi, 3);
      lo = vrshlq_s32(lo, round_shift);
      hi = vrshlq_s32(hi, round_shift);
      res = vcombine_s16(vqmovn_s32(lo), vqmovn_s32(hi));
      res = vmaxq_s16(res, vdupq_n_s16(0));
      res = vminq_s16(res, max_val_q);
      vst1q_u16(d, vreinterpretq_u16_s16(res));
      d += dst_stride;

      s0 = s4;
      s1 = s5;
      s2 = s6;
      s3 = s7;
      s4 = s8;
      s5 = s9;
      s6 = s10;
      height -= 4;
    } while (height > 0);
  }

  for (; col + 4 <= w; col += 4) {
    const int16_t *s = src_col + col;
    uint16_t *d = dst + col;

    int16x4_t s0 = vld1_s16(s + 0 * src_stride);
    int16x4_t s1 = vld1_s16(s + 1 * src_stride);
    int16x4_t s2 = vld1_s16(s + 2 * src_stride);
    int16x4_t s3 = vld1_s16(s + 3 * src_stride);
    int16x4_t s4 = vld1_s16(s + 4 * src_stride);
    int16x4_t s5 = vld1_s16(s + 5 * src_stride);
    int16x4_t s6 = vld1_s16(s + 6 * src_stride);
    s += 7 * src_stride;

    int height = h;
    do {
      int16x4_t s7 = vld1_s16(s + 0 * src_stride);
      int16x4_t s8 = vld1_s16(s + 1 * src_stride);
      int16x4_t s9 = vld1_s16(s + 2 * src_stride);
      int16x4_t s10 = vld1_s16(s + 3 * src_stride);
      s += 4 * src_stride;

      int32x4_t sum;
      int16x4_t res;

      sum = vmlal_lane_s16(vdupq_n_s32(0), s0, f_lo, 0);
      sum = vmlal_lane_s16(sum, s1, f_lo, 1);
      sum = vmlal_lane_s16(sum, s2, f_lo, 2);
      sum = vmlal_lane_s16(sum, s3, f_lo, 3);
      sum = vmlal_lane_s16(sum, s4, f_hi, 0);
      sum = vmlal_lane_s16(sum, s5, f_hi, 1);
      sum = vmlal_lane_s16(sum, s6, f_hi, 2);
      sum = vmlal_lane_s16(sum, s7, f_hi, 3);
      sum = vrshlq_s32(sum, round_shift);
      res = vqmovn_s32(sum);
      res = vmax_s16(res, vdup_n_s16(0));
      res = vmin_s16(res, max_val_d);
      vst1_u16(d, vreinterpret_u16_s16(res));
      d += dst_stride;

      sum = vmlal_lane_s16(vdupq_n_s32(0), s1, f_lo, 0);
      sum = vmlal_lane_s16(sum, s2, f_lo, 1);
      sum = vmlal_lane_s16(sum, s3, f_lo, 2);
      sum = vmlal_lane_s16(sum, s4, f_lo, 3);
      sum = vmlal_lane_s16(sum, s5, f_hi, 0);
      sum = vmlal_lane_s16(sum, s6, f_hi, 1);
      sum = vmlal_lane_s16(sum, s7, f_hi, 2);
      sum = vmlal_lane_s16(sum, s8, f_hi, 3);
      sum = vrshlq_s32(sum, round_shift);
      res = vqmovn_s32(sum);
      res = vmax_s16(res, vdup_n_s16(0));
      res = vmin_s16(res, max_val_d);
      vst1_u16(d, vreinterpret_u16_s16(res));
      d += dst_stride;

      sum = vmlal_lane_s16(vdupq_n_s32(0), s2, f_lo, 0);
      sum = vmlal_lane_s16(sum, s3, f_lo, 1);
      sum = vmlal_lane_s16(sum, s4, f_lo, 2);
      sum = vmlal_lane_s16(sum, s5, f_lo, 3);
      sum = vmlal_lane_s16(sum, s6, f_hi, 0);
      sum = vmlal_lane_s16(sum, s7, f_hi, 1);
      sum = vmlal_lane_s16(sum, s8, f_hi, 2);
      sum = vmlal_lane_s16(sum, s9, f_hi, 3);
      sum = vrshlq_s32(sum, round_shift);
      res = vqmovn_s32(sum);
      res = vmax_s16(res, vdup_n_s16(0));
      res = vmin_s16(res, max_val_d);
      vst1_u16(d, vreinterpret_u16_s16(res));
      d += dst_stride;

      sum = vmlal_lane_s16(vdupq_n_s32(0), s3, f_lo, 0);
      sum = vmlal_lane_s16(sum, s4, f_lo, 1);
      sum = vmlal_lane_s16(sum, s5, f_lo, 2);
      sum = vmlal_lane_s16(sum, s6, f_lo, 3);
      sum = vmlal_lane_s16(sum, s7, f_hi, 0);
      sum = vmlal_lane_s16(sum, s8, f_hi, 1);
      sum = vmlal_lane_s16(sum, s9, f_hi, 2);
      sum = vmlal_lane_s16(sum, s10, f_hi, 3);
      sum = vrshlq_s32(sum, round_shift);
      res = vqmovn_s32(sum);
      res = vmax_s16(res, vdup_n_s16(0));
      res = vmin_s16(res, max_val_d);
      vst1_u16(d, vreinterpret_u16_s16(res));
      d += dst_stride;

      s0 = s4;
      s1 = s5;
      s2 = s6;
      s3 = s7;
      s4 = s8;
      s5 = s9;
      s6 = s10;
      height -= 4;
    } while (height > 0);
  }

  for (int y = 0; y < h; ++y) {
    uint16_t *d = dst + y * dst_stride;
    for (int x = col; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_y->taps; ++k) {
        res += y_filter_ptr[k] *
               ((const int16_t *)src)[(y - fo_vert + k) * src_stride + x];
      }
      d[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(res, FILTER_BITS), bd);
    }
  }
}

static inline int16x8_t highbd_convolve8_8_2d_h(
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
  return vcombine_s16(vmovn_s32(sum0), vmovn_s32(sum1));
}

static inline int16x4_t highbd_convolve8_4_2d_h(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t x_filter_lo,
    const int16x4_t x_filter_hi, const int32x4_t offset,
    const int32x4_t round_shift) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, x_filter_lo, 0);
  sum = vmlal_lane_s16(sum, s1, x_filter_lo, 1);
  sum = vmlal_lane_s16(sum, s2, x_filter_lo, 2);
  sum = vmlal_lane_s16(sum, s3, x_filter_lo, 3);
  sum = vmlal_lane_s16(sum, s4, x_filter_hi, 0);
  sum = vmlal_lane_s16(sum, s5, x_filter_hi, 1);
  sum = vmlal_lane_s16(sum, s6, x_filter_hi, 2);
  sum = vmlal_lane_s16(sum, s7, x_filter_hi, 3);
  sum = vrshlq_s32(sum, round_shift);
  return vmovn_s32(sum);
}

static inline void highbd_convolve_2d_sr_horiz_8wide_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x8_t x_filter, const int32x4_t offset,
    const int32x4_t round_shift) {
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
      int16x8_t r = highbd_convolve8_8_2d_h(s0, s1, s2, s3, s4, s5, s6, s7,
                                            x_filter, offset, round_shift);
      vst1q_s16(d, r);
      s += 8;
      d += 8;
      width -= 8;
    } while (width > 0);
    src += src_stride;
    im += im_stride;
  } while (--h > 0);
}

static inline void highbd_convolve_2d_sr_horiz_4wide_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int h,
    const int16x4_t x_filter_lo, const int16x4_t x_filter_hi,
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
    int16x4_t r =
        highbd_convolve8_4_2d_h(s0, s1, s2, s3, s4, s5, s6, s7, x_filter_lo,
                                x_filter_hi, offset, round_shift);
    vst1_s16(im, r);
    src += src_stride;
    im += im_stride;
  } while (--h > 0);
}

static inline uint16x8_t highbd_convolve8_8_2d_v(
    const int16x8_t s0, const int16x8_t s1, const int16x8_t s2,
    const int16x8_t s3, const int16x8_t s4, const int16x8_t s5,
    const int16x8_t s6, const int16x8_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t offset, const int32x4_t round_shift,
    const int32x4_t sub_v, const int16x8_t max_val) {
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
  sum0 = vsubq_s32(sum0, sub_v);
  sum1 = vsubq_s32(sum1, sub_v);

  int16x8_t res = vcombine_s16(vqmovn_s32(sum0), vqmovn_s32(sum1));
  res = vmaxq_s16(res, vdupq_n_s16(0));
  res = vminq_s16(res, max_val);
  return vreinterpretq_u16_s16(res);
}

static inline uint16x4_t highbd_convolve8_4_2d_v(
    const int16x4_t s0, const int16x4_t s1, const int16x4_t s2,
    const int16x4_t s3, const int16x4_t s4, const int16x4_t s5,
    const int16x4_t s6, const int16x4_t s7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int32x4_t offset, const int32x4_t round_shift,
    const int32x4_t sub_v, const int16x4_t max_val) {
  int32x4_t sum = vmlal_lane_s16(offset, s0, f_lo, 0);
  sum = vmlal_lane_s16(sum, s1, f_lo, 1);
  sum = vmlal_lane_s16(sum, s2, f_lo, 2);
  sum = vmlal_lane_s16(sum, s3, f_lo, 3);
  sum = vmlal_lane_s16(sum, s4, f_hi, 0);
  sum = vmlal_lane_s16(sum, s5, f_hi, 1);
  sum = vmlal_lane_s16(sum, s6, f_hi, 2);
  sum = vmlal_lane_s16(sum, s7, f_hi, 3);

  sum = vrshlq_s32(sum, round_shift);
  sum = vsubq_s32(sum, sub_v);

  int16x4_t res = vqmovn_s32(sum);
  res = vmax_s16(res, vdup_n_s16(0));
  res = vmin_s16(res, max_val);
  return vreinterpret_u16_s16(res);
}

static inline void highbd_convolve_2d_sr_vert_8wide_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const int16x8_t y_filter, const int32x4_t offset,
    const int32x4_t round_shift, const int32x4_t sub_v, int bd) {
  const int16x8_t max_val = vdupq_n_s16((1 << bd) - 1);
  const int16x4_t f_lo = vget_low_s16(y_filter);
  const int16x4_t f_hi = vget_high_s16(y_filter);

  do {
    int height = h;
    const int16_t *s = src;
    uint16_t *d = dst;

    int16x8_t s0 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s1 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s2 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s3 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s4 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s5 = vld1q_s16(s);
    s += src_stride;
    int16x8_t s6 = vld1q_s16(s);
    s += src_stride;

    do {
      int16x8_t s7 = vld1q_s16(s);
      s += src_stride;
      int16x8_t s8 = vld1q_s16(s);
      s += src_stride;
      int16x8_t s9 = vld1q_s16(s);
      s += src_stride;
      int16x8_t s10 = vld1q_s16(s);
      s += src_stride;

      uint16x8_t d0 =
          highbd_convolve8_8_2d_v(s0, s1, s2, s3, s4, s5, s6, s7, f_lo, f_hi,
                                  offset, round_shift, sub_v, max_val);
      uint16x8_t d1 =
          highbd_convolve8_8_2d_v(s1, s2, s3, s4, s5, s6, s7, s8, f_lo, f_hi,
                                  offset, round_shift, sub_v, max_val);
      uint16x8_t d2 =
          highbd_convolve8_8_2d_v(s2, s3, s4, s5, s6, s7, s8, s9, f_lo, f_hi,
                                  offset, round_shift, sub_v, max_val);
      uint16x8_t d3 =
          highbd_convolve8_8_2d_v(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, f_hi,
                                  offset, round_shift, sub_v, max_val);

      vst1q_u16(d, d0);
      d += dst_stride;
      vst1q_u16(d, d1);
      d += dst_stride;
      vst1q_u16(d, d2);
      d += dst_stride;
      vst1q_u16(d, d3);
      d += dst_stride;

      s0 = s4;
      s1 = s5;
      s2 = s6;
      s3 = s7;
      s4 = s8;
      s5 = s9;
      s6 = s10;
      height -= 4;
    } while (height > 0);
    src += 8;
    dst += 8;
    w -= 8;
  } while (w > 0);
}

static inline void highbd_convolve_2d_sr_vert_4wide_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride, int h,
    const int16x8_t y_filter, const int32x4_t offset,
    const int32x4_t round_shift, const int32x4_t sub_v, int bd) {
  const int16x4_t max_val = vdup_n_s16((1 << bd) - 1);
  const int16x4_t f_lo = vget_low_s16(y_filter);
  const int16x4_t f_hi = vget_high_s16(y_filter);

  const int16_t *s = src;
  uint16_t *d = dst;

  int16x4_t s0 = vld1_s16(s);
  s += src_stride;
  int16x4_t s1 = vld1_s16(s);
  s += src_stride;
  int16x4_t s2 = vld1_s16(s);
  s += src_stride;
  int16x4_t s3 = vld1_s16(s);
  s += src_stride;
  int16x4_t s4 = vld1_s16(s);
  s += src_stride;
  int16x4_t s5 = vld1_s16(s);
  s += src_stride;
  int16x4_t s6 = vld1_s16(s);
  s += src_stride;

  do {
    int16x4_t s7 = vld1_s16(s);
    s += src_stride;
    int16x4_t s8 = vld1_s16(s);
    s += src_stride;
    int16x4_t s9 = vld1_s16(s);
    s += src_stride;
    int16x4_t s10 = vld1_s16(s);
    s += src_stride;

    uint16x4_t d0 =
        highbd_convolve8_4_2d_v(s0, s1, s2, s3, s4, s5, s6, s7, f_lo, f_hi,
                                offset, round_shift, sub_v, max_val);
    uint16x4_t d1 =
        highbd_convolve8_4_2d_v(s1, s2, s3, s4, s5, s6, s7, s8, f_lo, f_hi,
                                offset, round_shift, sub_v, max_val);
    uint16x4_t d2 =
        highbd_convolve8_4_2d_v(s2, s3, s4, s5, s6, s7, s8, s9, f_lo, f_hi,
                                offset, round_shift, sub_v, max_val);
    uint16x4_t d3 =
        highbd_convolve8_4_2d_v(s3, s4, s5, s6, s7, s8, s9, s10, f_lo, f_hi,
                                offset, round_shift, sub_v, max_val);

    vst1_u16(d, d0);
    d += dst_stride;
    vst1_u16(d, d1);
    d += dst_stride;
    vst1_u16(d, d2);
    d += dst_stride;
    vst1_u16(d, d3);
    d += dst_stride;

    s0 = s4;
    s1 = s5;
    s2 = s6;
    s3 = s7;
    s4 = s8;
    s5 = s9;
    s6 = s10;
    h -= 4;
  } while (h > 0);
}

void av2_highbd_convolve_2d_sr_neon(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_x_qn,
                                    const int subpel_y_qn,
                                    ConvolveParams *conv_params, int bd) {
  if (w < 4 || h < 4 || (w != 4 && (w % 8) != 0) || (h % 4) != 0) {
    av2_highbd_convolve_2d_sr_c(src, src_stride, dst, dst_stride, w, h,
                                filter_params_x, filter_params_y, subpel_x_qn,
                                subpel_y_qn, conv_params, bd);
    return;
  }

  int16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP) * MAX_SB_SIZE];
  const int im_h = h + filter_params_y->taps - 1;
  const int im_stride = w;
  assert(w <= MAX_SB_SIZE && h <= MAX_SB_SIZE);
  assert(w == 4 || (w % 8) == 0);
  assert(h % 4 == 0);
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int fo_horiz = filter_params_x->taps / 2 - 1;

  const int16_t *x_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  const int16_t *y_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);

  const int32_t horiz_offset = 1 << (bd + FILTER_BITS - 1);
  const int32x4_t horiz_offset_v = vdupq_n_s32(horiz_offset);
  const int32x4_t horiz_round_shift = vdupq_n_s32(-conv_params->round_0);

  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int32_t vert_offset = 1 << offset_bits;
  const int32x4_t vert_offset_v = vdupq_n_s32(vert_offset);
  const int32x4_t round_shift = vdupq_n_s32(-conv_params->round_1);
  const int32_t sub_const = (1 << (offset_bits - conv_params->round_1)) +
                            (1 << (offset_bits - conv_params->round_1 - 1));
  const int32x4_t sub_v = vdupq_n_s32(sub_const);

  const uint16_t *src_horiz = src - fo_vert * src_stride - fo_horiz;

  const int16x8_t x_filter = vld1q_s16(x_filter_ptr);
  const int16x8_t y_filter = vld1q_s16(y_filter_ptr);

  if (w == 4) {
    const int16x4_t xf_lo = vget_low_s16(x_filter);
    const int16x4_t xf_hi = vget_high_s16(x_filter);
    highbd_convolve_2d_sr_horiz_4wide_neon(src_horiz, src_stride, im_block,
                                           im_stride, im_h, xf_lo, xf_hi,
                                           horiz_offset_v, horiz_round_shift);
    highbd_convolve_2d_sr_vert_4wide_neon(im_block, im_stride, dst, dst_stride,
                                          h, y_filter, vert_offset_v,
                                          round_shift, sub_v, bd);
  } else {
    highbd_convolve_2d_sr_horiz_8wide_neon(src_horiz, src_stride, im_block,
                                           im_stride, w, im_h, x_filter,
                                           horiz_offset_v, horiz_round_shift);
    highbd_convolve_2d_sr_vert_8wide_neon(im_block, im_stride, dst, dst_stride,
                                          w, h, y_filter, vert_offset_v,
                                          round_shift, sub_v, bd);
  }
}
