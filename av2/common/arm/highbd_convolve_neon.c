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

// The filter is pre-shifted so that its taps-wide nonzero window starts at
// lane 0; the taps beyond it are skipped (they are zero). round_0 is a
// compile-time constant: ROUND0_BITS (bd 8/10, every production call) rounds
// and narrows with one immediate shift, 0 selects the runtime round_shift.
static AVM_FORCE_INLINE int16x8_t
highbd_convolve_8_2d_h(const int16_t *s, const int16x8_t f, const int taps,
                       const int round_0, const int32x4_t round_shift) {
  const int16x4_t f_lo = vget_low_s16(f);
  const int16x4_t f_hi = vget_high_s16(f);

  const int16x8_t s0 = vld1q_s16(s + 0);
  const int16x8_t s1 = vld1q_s16(s + 1);
  int32x4_t sum0 = vmull_lane_s16(vget_low_s16(s0), f_lo, 0);
  int32x4_t sum1 = vmull_lane_s16(vget_high_s16(s0), f_lo, 0);
  sum0 = vmlal_lane_s16(sum0, vget_low_s16(s1), f_lo, 1);
  sum1 = vmlal_lane_s16(sum1, vget_high_s16(s1), f_lo, 1);
  if (taps > 2) {
    const int16x8_t s2 = vld1q_s16(s + 2);
    const int16x8_t s3 = vld1q_s16(s + 3);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s2), f_lo, 2);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s2), f_lo, 2);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s3), f_lo, 3);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s3), f_lo, 3);
  }
  if (taps > 4) {
    const int16x8_t s4 = vld1q_s16(s + 4);
    const int16x8_t s5 = vld1q_s16(s + 5);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s4), f_hi, 0);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s4), f_hi, 0);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s5), f_hi, 1);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s5), f_hi, 1);
  }
  if (taps > 6) {
    const int16x8_t s6 = vld1q_s16(s + 6);
    const int16x8_t s7 = vld1q_s16(s + 7);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s6), f_hi, 2);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s6), f_hi, 2);
    sum0 = vmlal_lane_s16(sum0, vget_low_s16(s7), f_hi, 3);
    sum1 = vmlal_lane_s16(sum1, vget_high_s16(s7), f_hi, 3);
  }

  if (round_0 == ROUND0_BITS) {
    return vcombine_s16(vrshrn_n_s32(sum0, ROUND0_BITS),
                        vrshrn_n_s32(sum1, ROUND0_BITS));
  }
  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  return vcombine_s16(vmovn_s32(sum0), vmovn_s32(sum1));
}

static AVM_FORCE_INLINE int16x4_t
highbd_convolve_4_2d_h(const int16_t *s, const int16x8_t f, const int taps,
                       const int round_0, const int32x4_t round_shift) {
  const int16x4_t f_lo = vget_low_s16(f);
  const int16x4_t f_hi = vget_high_s16(f);

  int32x4_t sum = vmull_lane_s16(vld1_s16(s + 0), f_lo, 0);
  sum = vmlal_lane_s16(sum, vld1_s16(s + 1), f_lo, 1);
  if (taps > 2) {
    sum = vmlal_lane_s16(sum, vld1_s16(s + 2), f_lo, 2);
    sum = vmlal_lane_s16(sum, vld1_s16(s + 3), f_lo, 3);
  }
  if (taps > 4) {
    sum = vmlal_lane_s16(sum, vld1_s16(s + 4), f_hi, 0);
    sum = vmlal_lane_s16(sum, vld1_s16(s + 5), f_hi, 1);
  }
  if (taps > 6) {
    sum = vmlal_lane_s16(sum, vld1_s16(s + 6), f_hi, 2);
    sum = vmlal_lane_s16(sum, vld1_s16(s + 7), f_hi, 3);
  }
  if (round_0 == ROUND0_BITS) return vrshrn_n_s32(sum, ROUND0_BITS);
  sum = vrshlq_s32(sum, round_shift);
  return vmovn_s32(sum);
}

// Horizontal 8-wide filter at bd 8 in 16-bit lanes (8 products per MLA
// instead of 4 per SMLAL), with the halved filter fh = f >> 1. When every tap
// is even (highbd_2d_h16_fits()), S / 2 = sum(fh[k] * px[k]) exactly, and
// highbd_2d_h16_fits() also guarantees it lies in [-32768, 32767], so the
// wrapping 16-bit sum is exact as a signed value and the im value
// ROUND_POWER_OF_TWO_SIGNED(S, ROUND0_BITS) is a signed rounding shift of
// S / 2 by ROUND0_BITS - 1: no offset to add or subtract.
static AVM_FORCE_INLINE int16x8_t highbd_convolve_8_2d_h16(const int16_t *s,
                                                           const int16x8_t fh,
                                                           const int taps) {
  const int16x4_t f_lo = vget_low_s16(fh);
  const int16x4_t f_hi = vget_high_s16(fh);

  int16x8_t sum = vmulq_lane_s16(vld1q_s16(s + 0), f_lo, 0);
  sum = vmlaq_lane_s16(sum, vld1q_s16(s + 1), f_lo, 1);
  if (taps > 2) {
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 2), f_lo, 2);
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 3), f_lo, 3);
  }
  if (taps > 4) {
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 4), f_hi, 0);
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 5), f_hi, 1);
  }
  if (taps > 6) {
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 6), f_hi, 2);
    sum = vmlaq_lane_s16(sum, vld1q_s16(s + 7), f_hi, 3);
  }
  return vrshrq_n_s16(sum, ROUND0_BITS - 1);
}

// Whether the halved 16-bit horizontal path is exact for this filter at this
// bit depth: every tap is even, and every half sum sum(f[k] * px[k]) / 2 over
// pixels in [0, (1 << bd) - 1] lies in [-32768, 32767]. Holds for every
// encoder filter table at bd 8 (all taps even; largest negative-tap sum 56,
// positive-tap sum 184), never at bd 10.
static inline int highbd_2d_h16_fits(const int16_t *f, int bd) {
  int neg = 0, pos = 0, odd = 0;
  for (int k = 0; k < SUBPEL_TAPS; ++k) {
    odd |= f[k];
    if (f[k] < 0) {
      neg -= f[k];
    } else {
      pos += f[k];
    }
  }
  const int max_px = (1 << bd) - 1;
  return (odd & 1) == 0 && max_px * neg <= 2 * 32768 &&
         max_px * pos <= 2 * 32767;
}

// Horizontal pass over w8 = w & ~7 columns, one 8-wide column strip at a
// time (rows inside), then the last 4 columns when w % 8 == 4. h16
// (compile-time) selects the halved 16-bit 8-wide filter; the 4-wide tail
// always accumulates in 32 bits (4 lanes either way).
static AVM_FORCE_INLINE void highbd_convolve_2d_sr_horiz_neon(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x8_t f, const int taps, const int round_0,
    const int32x4_t round_shift, const int h16) {
  const int w8 = w & ~7;
  const int16x8_t fh = vshrq_n_s16(f, 1);
  if (w8 > 0) {
    // Column strips outside, rows inside (as in the vertical pass): the row
    // loop is the only per-row control, which matters most at w8 == 8.
    const uint16_t *src_col = src;
    int16_t *im_col = im;
    int width = w8;
    do {
      const int16_t *s = (const int16_t *)src_col;
      int16_t *d = im_col;
      int height = h;
      do {
        if (h16) {
          vst1q_s16(d, highbd_convolve_8_2d_h16(s, fh, taps));
        } else {
          vst1q_s16(d,
                    highbd_convolve_8_2d_h(s, f, taps, round_0, round_shift));
        }
        s += src_stride;
        d += im_stride;
      } while (--height > 0);
      src_col += 8;
      im_col += 8;
      width -= 8;
    } while (width > 0);
  }
  if (w != w8) {
    const int16_t *s = (const int16_t *)(src + w8);
    int16_t *d = im + w8;
    int height = h;
    do {
      vst1_s16(d, highbd_convolve_4_2d_h(s, f, taps, round_0, round_shift));
      s += src_stride;
      d += im_stride;
    } while (--height > 0);
  }
}

// The vertical rounding shift of every bd 8/10 call (round_0 == ROUND0_BITS).
#define HBD_2D_ROUND1_BITS (2 * FILTER_BITS - ROUND0_BITS)

// Sum of the taps-wide vertical window r[0..taps) for one 4-lane half.
static AVM_FORCE_INLINE int32x4_t highbd_convolve_4_2d_v_sum(
    const int16x4_t r0, const int16x4_t r1, const int16x4_t r2,
    const int16x4_t r3, const int16x4_t r4, const int16x4_t r5,
    const int16x4_t r6, const int16x4_t r7, const int16x4_t f_lo,
    const int16x4_t f_hi, const int taps) {
  int32x4_t sum = vmull_lane_s16(r0, f_lo, 0);
  sum = vmlal_lane_s16(sum, r1, f_lo, 1);
  if (taps > 2) {
    sum = vmlal_lane_s16(sum, r2, f_lo, 2);
    sum = vmlal_lane_s16(sum, r3, f_lo, 3);
  }
  if (taps > 4) {
    sum = vmlal_lane_s16(sum, r4, f_hi, 0);
    sum = vmlal_lane_s16(sum, r5, f_hi, 1);
  }
  if (taps > 6) {
    sum = vmlal_lane_s16(sum, r6, f_hi, 2);
    sum = vmlal_lane_s16(sum, r7, f_hi, 3);
  }
  return sum;
}

// round_1 is a compile-time constant: HBD_2D_ROUND1_BITS (bd 8/10, every
// production call) rounds, shifts and saturates with one immediate
// SQRSHRUN, 0 selects the runtime round_shift.
static AVM_FORCE_INLINE uint16x8_t
highbd_convolve_8_2d_v(const int16x8_t *r, const int16x4_t f_lo,
                       const int16x4_t f_hi, const int taps, const int round_1,
                       const int32x4_t round_shift, const uint16x8_t max_val) {
  // Rows past the taps-wide window are never read (taps is a constant).
  const int16x8_t z = vdupq_n_s16(0);
  const int16x8_t r2 = taps > 2 ? r[2] : z;
  const int16x8_t r3 = taps > 2 ? r[3] : z;
  const int16x8_t r4 = taps > 4 ? r[4] : z;
  const int16x8_t r5 = taps > 4 ? r[5] : z;
  const int16x8_t r6 = taps > 6 ? r[6] : z;
  const int16x8_t r7 = taps > 6 ? r[7] : z;
  int32x4_t sum0 = highbd_convolve_4_2d_v_sum(
      vget_low_s16(r[0]), vget_low_s16(r[1]), vget_low_s16(r2),
      vget_low_s16(r3), vget_low_s16(r4), vget_low_s16(r5), vget_low_s16(r6),
      vget_low_s16(r7), f_lo, f_hi, taps);
  int32x4_t sum1 = highbd_convolve_4_2d_v_sum(
      vget_high_s16(r[0]), vget_high_s16(r[1]), vget_high_s16(r2),
      vget_high_s16(r3), vget_high_s16(r4), vget_high_s16(r5),
      vget_high_s16(r6), vget_high_s16(r7), f_lo, f_hi, taps);

  if (round_1 == HBD_2D_ROUND1_BITS) {
    const uint16x8_t res =
        vcombine_u16(vqrshrun_n_s32(sum0, HBD_2D_ROUND1_BITS),
                     vqrshrun_n_s32(sum1, HBD_2D_ROUND1_BITS));
    return vminq_u16(res, max_val);
  }
  sum0 = vrshlq_s32(sum0, round_shift);
  sum1 = vrshlq_s32(sum1, round_shift);
  const uint16x8_t res = vcombine_u16(vqmovun_s32(sum0), vqmovun_s32(sum1));
  return vminq_u16(res, max_val);
}

static AVM_FORCE_INLINE uint16x4_t
highbd_convolve_4_2d_v(const int16x4_t *r, const int16x4_t f_lo,
                       const int16x4_t f_hi, const int taps, const int round_1,
                       const int32x4_t round_shift, const uint16x4_t max_val) {
  const int16x4_t z = vdup_n_s16(0);
  int32x4_t sum = highbd_convolve_4_2d_v_sum(
      r[0], r[1], taps > 2 ? r[2] : z, taps > 2 ? r[3] : z, taps > 4 ? r[4] : z,
      taps > 4 ? r[5] : z, taps > 6 ? r[6] : z, taps > 6 ? r[7] : z, f_lo, f_hi,
      taps);
  if (round_1 == HBD_2D_ROUND1_BITS) {
    return vmin_u16(vqrshrun_n_s32(sum, HBD_2D_ROUND1_BITS), max_val);
  }
  sum = vrshlq_s32(sum, round_shift);
  return vmin_u16(vqmovun_s32(sum), max_val);
}

// Vertical pass over im (h + taps - 1 rows): w8 = w & ~7 columns 8 at a
// time, then the last 4 columns when w % 8 == 4. Each block of 4 output rows
// loads its taps + 3 im rows straight from im (hot in L1) instead of carrying
// a sliding window across iterations, which clang kept in a stack array; the
// last h % 4 rows load taps rows each.
static AVM_FORCE_INLINE void highbd_convolve_2d_sr_vert_neon(
    const int16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const int16x8_t f, const int taps, const int round_1,
    const int32x4_t round_shift, int bd) {
  const int16x4_t f_lo = vget_low_s16(f);
  const int16x4_t f_hi = vget_high_s16(f);
  const int w8 = w & ~7;

  if (w8 > 0) {
    const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);
    const int16_t *src_col = src;
    uint16_t *dst_col = dst;
    int width = w8;
    do {
      const int16_t *s = src_col;
      uint16_t *d = dst_col;
      int height = h;
      while (height >= 4) {
        int16x8_t r[11];
        for (int i = 0; i < taps + 3; ++i) r[i] = vld1q_s16(s + i * src_stride);
        vst1q_u16(d + 0 * dst_stride,
                  highbd_convolve_8_2d_v(r + 0, f_lo, f_hi, taps, round_1,
                                         round_shift, max_val));
        vst1q_u16(d + 1 * dst_stride,
                  highbd_convolve_8_2d_v(r + 1, f_lo, f_hi, taps, round_1,
                                         round_shift, max_val));
        vst1q_u16(d + 2 * dst_stride,
                  highbd_convolve_8_2d_v(r + 2, f_lo, f_hi, taps, round_1,
                                         round_shift, max_val));
        vst1q_u16(d + 3 * dst_stride,
                  highbd_convolve_8_2d_v(r + 3, f_lo, f_hi, taps, round_1,
                                         round_shift, max_val));
        s += 4 * src_stride;
        d += 4 * dst_stride;
        height -= 4;
      }
      while (height > 0) {
        int16x8_t r[8];
        for (int i = 0; i < taps; ++i) r[i] = vld1q_s16(s + i * src_stride);
        vst1q_u16(d, highbd_convolve_8_2d_v(r, f_lo, f_hi, taps, round_1,
                                            round_shift, max_val));
        s += src_stride;
        d += dst_stride;
        height--;
      }
      src_col += 8;
      dst_col += 8;
      width -= 8;
    } while (width > 0);
  }

  if (w != w8) {
    const uint16x4_t max_val = vdup_n_u16((1 << bd) - 1);
    const int16_t *s = src + w8;
    uint16_t *d = dst + w8;
    int height = h;
    while (height >= 4) {
      int16x4_t r[11];
      for (int i = 0; i < taps + 3; ++i) r[i] = vld1_s16(s + i * src_stride);
      vst1_u16(d + 0 * dst_stride,
               highbd_convolve_4_2d_v(r + 0, f_lo, f_hi, taps, round_1,
                                      round_shift, max_val));
      vst1_u16(d + 1 * dst_stride,
               highbd_convolve_4_2d_v(r + 1, f_lo, f_hi, taps, round_1,
                                      round_shift, max_val));
      vst1_u16(d + 2 * dst_stride,
               highbd_convolve_4_2d_v(r + 2, f_lo, f_hi, taps, round_1,
                                      round_shift, max_val));
      vst1_u16(d + 3 * dst_stride,
               highbd_convolve_4_2d_v(r + 3, f_lo, f_hi, taps, round_1,
                                      round_shift, max_val));
      s += 4 * src_stride;
      d += 4 * dst_stride;
      height -= 4;
    }
    while (height > 0) {
      int16x4_t r[8];
      for (int i = 0; i < taps; ++i) r[i] = vld1_s16(s + i * src_stride);
      vst1_u16(d, highbd_convolve_4_2d_v(r, f_lo, f_hi, taps, round_1,
                                         round_shift, max_val));
      s += src_stride;
      d += dst_stride;
      height--;
    }
  }
}

// Both passes in one loop per 8-wide column strip, for bd 8 calls whose x
// filter takes the halved 16-bit path (h16) and w % 8 == 0. taps_x and taps_y
// are compile-time constants (the effective tap counts; each filter is
// shifted so its nonzero window starts at lane 0). The horizontal pass is
// load-bound and the vertical pass vector-bound, so each iteration computes
// the 4 im rows the next 4 output rows need (the h16 loads) and those 4
// outputs (the vertical multiplies) from a taps_y + 3 row register window;
// its last taps_y - 1 rows carry to the next iteration in the named registers
// r0..r6 (an array window rotated in place was kept on the stack by clang).
// Each new im row is computed just before the output that first needs it:
// with the 4 rows first, clang hoisted all 32 8/8 loads and spilled 7 of them.
// Each im row and each output is computed exactly as in the two-pass path
// (same helpers), so the results are identical.
static AVM_FORCE_INLINE void highbd_2d_sr_fused_h16_neon(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const int16x8_t fh, const int taps_x, const int16x8_t fy,
    const int taps_y, int bd) {
  const int16x4_t fy_lo = vget_low_s16(fy);
  const int16x4_t fy_hi = vget_high_s16(fy);
  const int32x4_t round_shift = vdupq_n_s32(-HBD_2D_ROUND1_BITS);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);
  const int16x8_t z = vdupq_n_s16(0);
  const ptrdiff_t ss = src_stride;
  int width = w;
  do {
    const int16_t *s = (const int16_t *)src;
    uint16_t *d = dst;
    int16x8_t r0 = highbd_convolve_8_2d_h16(s + 0 * ss, fh, taps_x);
    int16x8_t r1 = z, r2 = z, r3 = z, r4 = z, r5 = z, r6 = z;
    if (taps_y > 2) {
      r1 = highbd_convolve_8_2d_h16(s + 1 * ss, fh, taps_x);
      r2 = highbd_convolve_8_2d_h16(s + 2 * ss, fh, taps_x);
    }
    if (taps_y > 4) {
      r3 = highbd_convolve_8_2d_h16(s + 3 * ss, fh, taps_x);
      r4 = highbd_convolve_8_2d_h16(s + 4 * ss, fh, taps_x);
    }
    if (taps_y > 6) {
      r5 = highbd_convolve_8_2d_h16(s + 5 * ss, fh, taps_x);
      r6 = highbd_convolve_8_2d_h16(s + 6 * ss, fh, taps_x);
    }
    s += (taps_y - 1) * ss;
    int height = h;
    while (height >= 4) {
      int16x8_t win[11] = { r0, r1, r2, r3, r4, r5, r6, z, z, z, z };
      win[taps_y - 1] = highbd_convolve_8_2d_h16(s + 0 * ss, fh, taps_x);
      vst1q_u16(
          d + 0 * dst_stride,
          highbd_convolve_8_2d_v(win + 0, fy_lo, fy_hi, taps_y,
                                 HBD_2D_ROUND1_BITS, round_shift, max_val));
      win[taps_y + 0] = highbd_convolve_8_2d_h16(s + 1 * ss, fh, taps_x);
      vst1q_u16(
          d + 1 * dst_stride,
          highbd_convolve_8_2d_v(win + 1, fy_lo, fy_hi, taps_y,
                                 HBD_2D_ROUND1_BITS, round_shift, max_val));
      win[taps_y + 1] = highbd_convolve_8_2d_h16(s + 2 * ss, fh, taps_x);
      vst1q_u16(
          d + 2 * dst_stride,
          highbd_convolve_8_2d_v(win + 2, fy_lo, fy_hi, taps_y,
                                 HBD_2D_ROUND1_BITS, round_shift, max_val));
      win[taps_y + 2] = highbd_convolve_8_2d_h16(s + 3 * ss, fh, taps_x);
      vst1q_u16(
          d + 3 * dst_stride,
          highbd_convolve_8_2d_v(win + 3, fy_lo, fy_hi, taps_y,
                                 HBD_2D_ROUND1_BITS, round_shift, max_val));
      r0 = win[4];
      r1 = win[5];
      r2 = win[6];
      r3 = win[7];
      r4 = win[8];
      r5 = win[9];
      r6 = win[10];
      s += 4 * ss;
      d += 4 * dst_stride;
      height -= 4;
    }
    while (height > 0) {
      int16x8_t win[8] = { r0, r1, r2, r3, r4, r5, r6, z };
      win[taps_y - 1] = highbd_convolve_8_2d_h16(s, fh, taps_x);
      vst1q_u16(
          d, highbd_convolve_8_2d_v(win, fy_lo, fy_hi, taps_y,
                                    HBD_2D_ROUND1_BITS, round_shift, max_val));
      r0 = win[1];
      r1 = win[2];
      r2 = win[3];
      r3 = win[4];
      r4 = win[5];
      r5 = win[6];
      r6 = win[7];
      s += ss;
      d += dst_stride;
      height--;
    }
    src += 8;
    dst += 8;
    width -= 8;
  } while (width > 0);
}

// Shifts an 8-tap filter so its window [k0, k0 + taps), k0 = (8 - taps) / 2,
// starts at lane 0. The vextq_s16 shift must be an immediate, so each tap
// count is its own case.
static AVM_FORCE_INLINE int16x8_t highbd_2d_shift_filter(const int16x8_t f,
                                                         const int taps) {
  const int16x8_t zero = vdupq_n_s16(0);
  switch (taps) {
    case 2: return vextq_s16(f, zero, 3);
    case 4: return vextq_s16(f, zero, 2);
    case 6: return vextq_s16(f, zero, 1);
    default: return f;
  }
}

// Dispatches the horizontal pass on the effective x tap count, passing it as
// a constant so each tap count gets its own inlined loop.
static AVM_FORCE_INLINE void highbd_2d_sr_horiz_dispatch(
    const uint16_t *src, int src_stride, int16_t *im, int im_stride, int w,
    int h, const int16x8_t x_filter, const int taps_x, const int round_0,
    const int32x4_t round_shift, const int h16) {
  switch (taps_x) {
    case 2:
      highbd_convolve_2d_sr_horiz_neon(src, src_stride, im, im_stride, w, h,
                                       highbd_2d_shift_filter(x_filter, 2), 2,
                                       round_0, round_shift, h16);
      break;
    case 4:
      highbd_convolve_2d_sr_horiz_neon(src, src_stride, im, im_stride, w, h,
                                       highbd_2d_shift_filter(x_filter, 4), 4,
                                       round_0, round_shift, h16);
      break;
    case 6:
      highbd_convolve_2d_sr_horiz_neon(src, src_stride, im, im_stride, w, h,
                                       highbd_2d_shift_filter(x_filter, 6), 6,
                                       round_0, round_shift, h16);
      break;
    default:
      highbd_convolve_2d_sr_horiz_neon(src, src_stride, im, im_stride, w, h,
                                       highbd_2d_shift_filter(x_filter, 8), 8,
                                       round_0, round_shift, h16);
      break;
  }
}

// Dispatches the vertical pass on the effective y tap count, passing it as a
// constant so each tap count gets its own inlined loop.
static AVM_FORCE_INLINE void highbd_2d_sr_vert_dispatch(
    const int16_t *im, int im_stride, uint16_t *dst, int dst_stride, int w,
    int h, const int16x8_t y_filter, const int taps_y, const int round_1,
    const int32x4_t round_shift, int bd) {
  switch (taps_y) {
    case 2:
      highbd_convolve_2d_sr_vert_neon(im, im_stride, dst, dst_stride, w, h,
                                      highbd_2d_shift_filter(y_filter, 2), 2,
                                      round_1, round_shift, bd);
      break;
    case 4:
      highbd_convolve_2d_sr_vert_neon(im, im_stride, dst, dst_stride, w, h,
                                      highbd_2d_shift_filter(y_filter, 4), 4,
                                      round_1, round_shift, bd);
      break;
    case 6:
      highbd_convolve_2d_sr_vert_neon(im, im_stride, dst, dst_stride, w, h,
                                      highbd_2d_shift_filter(y_filter, 6), 6,
                                      round_1, round_shift, bd);
      break;
    default:
      highbd_convolve_2d_sr_vert_neon(im, im_stride, dst, dst_stride, w, h,
                                      highbd_2d_shift_filter(y_filter, 8), 8,
                                      round_1, round_shift, bd);
      break;
  }
}

void av2_highbd_convolve_2d_sr_neon(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_x_qn,
                                    const int subpel_y_qn,
                                    ConvolveParams *conv_params, int bd) {
  if (w < 4 || (w & 3) != 0 || filter_params_x->taps != SUBPEL_TAPS ||
      filter_params_y->taps != SUBPEL_TAPS) {
    av2_highbd_convolve_2d_sr_c(src, src_stride, dst, dst_stride, w, h,
                                filter_params_x, filter_params_y, subpel_x_qn,
                                subpel_y_qn, conv_params, bd);
    return;
  }

  int16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP) * MAX_SB_SIZE];
  const int im_stride = w;
  assert(w <= MAX_SB_SIZE && h <= MAX_SB_SIZE);
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const int16_t *x_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  const int16_t *y_filter_ptr = av2_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);

  // Only the window [k0, k0 + taps) of each filter is applied, with
  // k0 = (8 - taps) / 2: get_filter_tap returns the smallest centered window
  // that holds every nonzero tap. The horizontal pass starts k0_x columns and
  // k0_y rows into the 8-tap source window and produces h + taps_y - 1 im
  // rows.
  const int taps_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int taps_y = get_filter_tap(filter_params_y, subpel_y_qn);
  const int k0_x = (SUBPEL_TAPS - taps_x) / 2;
  const int k0_y = (SUBPEL_TAPS - taps_y) / 2;
  const int im_h = h + taps_y - 1;

  // The C reference adds 1 << (bd + FILTER_BITS - 1) before the round_0
  // shift and 1 << offset_bits before the round_1 shift, then subtracts
  // (1 << (offset_bits - round_1)) + (1 << (offset_bits - round_1 - 1)).
  // Each offset is a multiple of its rounding divisor and every filter sums
  // to 1 << FILTER_BITS, so with round_0 + round_1 == 2 * FILTER_BITS (the
  // non-compound path) the offsets cancel exactly: both passes drop them,
  // the signed im values fit in int16, and the vertical result is a rounding
  // shift saturated to [0, (1 << bd) - 1].
  assert(conv_params->round_0 + conv_params->round_1 == 2 * FILTER_BITS);
  const int32x4_t horiz_round_shift = vdupq_n_s32(-conv_params->round_0);
  const int32x4_t round_shift = vdupq_n_s32(-conv_params->round_1);

  const int fo = SUBPEL_TAPS / 2 - 1;
  const uint16_t *src_horiz = src - (fo - k0_y) * src_stride - (fo - k0_x);

  const int16x8_t x_filter = vld1q_s16(x_filter_ptr);
  const int16x8_t y_filter = vld1q_s16(y_filter_ptr);

  // round_0 is ROUND0_BITS for bd 8/10 (immediate rounding narrow); bd 12
  // raises it (convolve.h), which takes the runtime-shift instantiation.
  // At bd 8 the 8-wide horizontal filter accumulates the halved filter in
  // 16-bit lanes when its taps are even and the half sums fit
  // (highbd_2d_h16_fits).
  const int h16 = conv_params->round_0 == ROUND0_BITS &&
                  highbd_2d_h16_fits(x_filter_ptr, bd);
  // The hot tap pairs at bd 8 with w % 8 == 0 run both passes in one loop
  // (highbd_2d_sr_fused_h16_neon): 6/6 (about 42% of calls), 8/8, 4/4, 4/6
  // and 6/4 (about 32% together); round_1 is then HBD_2D_ROUND1_BITS
  // (round_0 + round_1 == 2 * FILTER_BITS).
  if (h16 && (w & 7) == 0) {
    const int16x8_t fx4 = vshrq_n_s16(highbd_2d_shift_filter(x_filter, 4), 1);
    const int16x8_t fx6 = vshrq_n_s16(highbd_2d_shift_filter(x_filter, 6), 1);
    const int16x8_t fy4 = highbd_2d_shift_filter(y_filter, 4);
    const int16x8_t fy6 = highbd_2d_shift_filter(y_filter, 6);
    switch (taps_x * 16 + taps_y) {
      case 6 * 16 + 6:
        highbd_2d_sr_fused_h16_neon(src_horiz, src_stride, dst, dst_stride, w,
                                    h, fx6, 6, fy6, 6, bd);
        return;
      case 8 * 16 + 8:
        highbd_2d_sr_fused_h16_neon(src_horiz, src_stride, dst, dst_stride, w,
                                    h, vshrq_n_s16(x_filter, 1), 8, y_filter, 8,
                                    bd);
        return;
      case 4 * 16 + 4:
        highbd_2d_sr_fused_h16_neon(src_horiz, src_stride, dst, dst_stride, w,
                                    h, fx4, 4, fy4, 4, bd);
        return;
      case 4 * 16 + 6:
        highbd_2d_sr_fused_h16_neon(src_horiz, src_stride, dst, dst_stride, w,
                                    h, fx4, 4, fy6, 6, bd);
        return;
      case 6 * 16 + 4:
        highbd_2d_sr_fused_h16_neon(src_horiz, src_stride, dst, dst_stride, w,
                                    h, fx6, 6, fy4, 4, bd);
        return;
      default: break;
    }
  }
  if (h16) {
    highbd_2d_sr_horiz_dispatch(src_horiz, src_stride, im_block, im_stride, w,
                                im_h, x_filter, taps_x, ROUND0_BITS,
                                horiz_round_shift, 1);
  } else if (conv_params->round_0 == ROUND0_BITS) {
    highbd_2d_sr_horiz_dispatch(src_horiz, src_stride, im_block, im_stride, w,
                                im_h, x_filter, taps_x, ROUND0_BITS,
                                horiz_round_shift, 0);
  } else {
    highbd_2d_sr_horiz_dispatch(src_horiz, src_stride, im_block, im_stride, w,
                                im_h, x_filter, taps_x, 0, horiz_round_shift,
                                0);
  }

  // round_1 is 2 * FILTER_BITS - ROUND0_BITS for bd 8/10 (immediate
  // rounding narrow); bd 12 lowers it, which takes the runtime-shift
  // instantiation.
  if (conv_params->round_1 == HBD_2D_ROUND1_BITS) {
    highbd_2d_sr_vert_dispatch(im_block, im_stride, dst, dst_stride, w, h,
                               y_filter, taps_y, HBD_2D_ROUND1_BITS,
                               round_shift, bd);
  } else {
    highbd_2d_sr_vert_dispatch(im_block, im_stride, dst, dst_stride, w, h,
                               y_filter, taps_y, 0, round_shift, bd);
  }
}
