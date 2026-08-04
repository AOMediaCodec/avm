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

// NEON implementations for highbd convolve 8-tap horiz/vert.

#include "config/avm_dsp_rtcd.h"

#include <arm_neon.h>
#include <string.h>

#include "avm_dsp/avm_convolve_common.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"

static INLINE void highbd_convolve_copy_neon(const uint16_t *src,
                                             ptrdiff_t src_stride,
                                             uint16_t *dst,
                                             ptrdiff_t dst_stride, int w,
                                             int h) {
  const size_t row_bytes = (size_t)w * sizeof(uint16_t);
  for (int y = 0; y < h; ++y) {
    memcpy(dst, src, row_bytes);
    src += src_stride;
    dst += dst_stride;
  }
}

// Horizontal 2-tap bilinear filter (only taps 3,4 active).
static void highbd_convolve_horiz_2tap_neon(const uint16_t *src,
                                            ptrdiff_t src_stride, uint16_t *dst,
                                            ptrdiff_t dst_stride,
                                            const int16_t *filter, int w, int h,
                                            int bd) {
  const int16x4_t f3 = vdup_n_s16(filter[3]);
  const int16x4_t f4 = vdup_n_s16(filter[4]);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);

  for (int y = 0; y < h; ++y) {
    int x = 0;
    for (; x + 8 <= w; x += 8) {
      const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(&src[x + 0]));
      const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(&src[x + 1]));

      int32x4_t lo = vmull_s16(vget_low_s16(s0), f3);
      lo = vmlal_s16(lo, vget_low_s16(s1), f4);
      int32x4_t hi = vmull_s16(vget_high_s16(s0), f3);
      hi = vmlal_s16(hi, vget_high_s16(s1), f4);
      uint16x8_t res =
          vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
      vst1q_u16(&dst[x], vminq_u16(res, max_val));
    }
    for (; x + 4 <= w; x += 4) {
      const int16x4_t s0 = vreinterpret_s16_u16(vld1_u16(&src[x + 0]));
      const int16x4_t s1 = vreinterpret_s16_u16(vld1_u16(&src[x + 1]));

      int32x4_t acc = vmull_s16(s0, f3);
      acc = vmlal_s16(acc, s1, f4);
      uint16x4_t res = vqrshrun_n_s32(acc, 7);
      vst1_u16(&dst[x], vmin_u16(res, vget_low_u16(max_val)));
    }
    for (; x < w; ++x) {
      int sum = (int)src[x] * filter[3] + (int)src[x + 1] * filter[4];
      dst[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
    }
    src += src_stride;
    dst += dst_stride;
  }
}

// Horizontal 4-tap filter for widths >= 8 (taps 2-5 active).
static void highbd_convolve_horiz_4tap_neon(const uint16_t *src,
                                            ptrdiff_t src_stride, uint16_t *dst,
                                            ptrdiff_t dst_stride,
                                            const int16_t *filter, int w, int h,
                                            int bd) {
  // Only taps 2,3,4,5 are non-zero.
  const int16x4_t f2 = vdup_n_s16(filter[2]);
  const int16x4_t f3 = vdup_n_s16(filter[3]);
  const int16x4_t f4 = vdup_n_s16(filter[4]);
  const int16x4_t f5 = vdup_n_s16(filter[5]);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);

  // Taps 0,1 are zero, so effective offset is -(4/2-1) + 2 = +1 from base.
  // But the caller already offsets src by -3, so relative to that: src+2.
  src -= 3;  // standard 8-tap offset
  const uint16_t *s = src + 2;

  for (int y = 0; y < h; ++y) {
    int x = 0;
    for (; x + 8 <= w; x += 8) {
      const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(&s[x + 0]));
      const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(&s[x + 1]));
      const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(&s[x + 2]));
      const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(&s[x + 3]));

      int32x4_t lo = vmull_s16(vget_low_s16(s0), f2);
      lo = vmlal_s16(lo, vget_low_s16(s1), f3);
      lo = vmlal_s16(lo, vget_low_s16(s2), f4);
      lo = vmlal_s16(lo, vget_low_s16(s3), f5);
      int32x4_t hi = vmull_s16(vget_high_s16(s0), f2);
      hi = vmlal_s16(hi, vget_high_s16(s1), f3);
      hi = vmlal_s16(hi, vget_high_s16(s2), f4);
      hi = vmlal_s16(hi, vget_high_s16(s3), f5);
      uint16x8_t res =
          vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
      vst1q_u16(&dst[x], vminq_u16(res, max_val));
    }
    for (; x + 4 <= w; x += 4) {
      const int16x4_t s0 = vreinterpret_s16_u16(vld1_u16(&s[x + 0]));
      const int16x4_t s1 = vreinterpret_s16_u16(vld1_u16(&s[x + 1]));
      const int16x4_t s2 = vreinterpret_s16_u16(vld1_u16(&s[x + 2]));
      const int16x4_t s3 = vreinterpret_s16_u16(vld1_u16(&s[x + 3]));

      int32x4_t acc = vmull_s16(s0, f2);
      acc = vmlal_s16(acc, s1, f3);
      acc = vmlal_s16(acc, s2, f4);
      acc = vmlal_s16(acc, s3, f5);
      uint16x4_t res = vqrshrun_n_s32(acc, 7);
      vst1_u16(&dst[x], vmin_u16(res, vget_low_u16(max_val)));
    }
    for (; x < w; ++x) {
      int sum = (int)s[x + 0] * filter[2] + (int)s[x + 1] * filter[3] +
                (int)s[x + 2] * filter[4] + (int)s[x + 3] * filter[5];
      dst[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
    }
    s += src_stride;
    dst += dst_stride;
  }
}

// Horizontal 8-tap filter for widths >= 8 (all taps active).
static void highbd_convolve_horiz_8tap_neon(const uint16_t *src,
                                            ptrdiff_t src_stride, uint16_t *dst,
                                            ptrdiff_t dst_stride,
                                            const int16_t *filter, int w, int h,
                                            int bd) {
  const int16x8_t f = vld1q_s16(filter);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);

  src -= 3;

  for (int y = 0; y < h; ++y) {
    const int16_t *sp = (const int16_t *)src;
    int x = 0;
    for (; x + 8 <= w; x += 8) {
      const int16x8_t s0 = vld1q_s16(sp + x + 0);
      const int16x8_t s1 = vld1q_s16(sp + x + 1);
      const int16x8_t s2 = vld1q_s16(sp + x + 2);
      const int16x8_t s3 = vld1q_s16(sp + x + 3);
      const int16x8_t s4 = vld1q_s16(sp + x + 4);
      const int16x8_t s5 = vld1q_s16(sp + x + 5);
      const int16x8_t s6 = vld1q_s16(sp + x + 6);
      const int16x8_t s7 = vld1q_s16(sp + x + 7);

      int32x4_t lo = vmull_lane_s16(vget_low_s16(s0), vget_low_s16(f), 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s1), vget_low_s16(f), 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s2), vget_low_s16(f), 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s3), vget_low_s16(f), 3);
      lo = vmlal_lane_s16(lo, vget_low_s16(s4), vget_high_s16(f), 0);
      lo = vmlal_lane_s16(lo, vget_low_s16(s5), vget_high_s16(f), 1);
      lo = vmlal_lane_s16(lo, vget_low_s16(s6), vget_high_s16(f), 2);
      lo = vmlal_lane_s16(lo, vget_low_s16(s7), vget_high_s16(f), 3);
      int32x4_t hi = vmull_lane_s16(vget_high_s16(s0), vget_low_s16(f), 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s1), vget_low_s16(f), 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s2), vget_low_s16(f), 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s3), vget_low_s16(f), 3);
      hi = vmlal_lane_s16(hi, vget_high_s16(s4), vget_high_s16(f), 0);
      hi = vmlal_lane_s16(hi, vget_high_s16(s5), vget_high_s16(f), 1);
      hi = vmlal_lane_s16(hi, vget_high_s16(s6), vget_high_s16(f), 2);
      hi = vmlal_lane_s16(hi, vget_high_s16(s7), vget_high_s16(f), 3);
      uint16x8_t res =
          vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
      vst1q_u16(&dst[x], vminq_u16(res, max_val));
    }
    for (; x + 4 <= w; x += 4) {
      const int16x4_t s0 = vld1_s16(sp + x + 0);
      const int16x4_t s1 = vld1_s16(sp + x + 1);
      const int16x4_t s2 = vld1_s16(sp + x + 2);
      const int16x4_t s3 = vld1_s16(sp + x + 3);
      const int16x4_t s4 = vld1_s16(sp + x + 4);
      const int16x4_t s5 = vld1_s16(sp + x + 5);
      const int16x4_t s6 = vld1_s16(sp + x + 6);
      const int16x4_t s7 = vld1_s16(sp + x + 7);

      int32x4_t acc = vmull_lane_s16(s0, vget_low_s16(f), 0);
      acc = vmlal_lane_s16(acc, s1, vget_low_s16(f), 1);
      acc = vmlal_lane_s16(acc, s2, vget_low_s16(f), 2);
      acc = vmlal_lane_s16(acc, s3, vget_low_s16(f), 3);
      acc = vmlal_lane_s16(acc, s4, vget_high_s16(f), 0);
      acc = vmlal_lane_s16(acc, s5, vget_high_s16(f), 1);
      acc = vmlal_lane_s16(acc, s6, vget_high_s16(f), 2);
      acc = vmlal_lane_s16(acc, s7, vget_high_s16(f), 3);
      uint16x4_t res = vqrshrun_n_s32(acc, 7);
      vst1_u16(&dst[x], vmin_u16(res, vget_low_u16(max_val)));
    }
    for (; x < w; ++x) {
      int sum = 0;
      for (int k = 0; k < 8; ++k) sum += sp[x + k] * filter[k];
      dst[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
    }
    src += src_stride;
    dst += dst_stride;
  }
}

void avm_highbd_convolve8_horiz_neon(const uint16_t *src, ptrdiff_t src_stride,
                                     uint16_t *dst, ptrdiff_t dst_stride,
                                     const int16_t *filter_x, int x_step_q4,
                                     const int16_t *filter_y, int y_step_q4,
                                     int w, int h, int bd) {
  const InterpKernel *const filters_x = get_filter_base(filter_x);
  const int x0_q4 = get_filter_offset(filter_x, filters_x);
  (void)filter_y;
  (void)y_step_q4;

  if (x_step_q4 == 16) {
    if (filter_x[3] == 128) {
      highbd_convolve_copy_neon(src, src_stride, dst, dst_stride, w, h);
      return;
    }
    const int16_t *f = filters_x[x0_q4 & SUBPEL_MASK];
    if ((f[0] | f[1] | f[6] | f[7]) == 0) {
      if ((f[2] | f[5]) == 0) {
        highbd_convolve_horiz_2tap_neon(src, src_stride, dst, dst_stride, f, w,
                                        h, bd);
      } else {
        highbd_convolve_horiz_4tap_neon(src, src_stride, dst, dst_stride, f, w,
                                        h, bd);
      }
      return;
    }
    highbd_convolve_horiz_8tap_neon(src, src_stride, dst, dst_stride, f, w, h,
                                    bd);
    return;
  }
  avm_highbd_convolve8_horiz_c(src, src_stride, dst, dst_stride, filter_x,
                               x_step_q4, filter_y, y_step_q4, w, h, bd);
}

// Vertical 2-tap bilinear filter (only taps 3,4 active).
static void highbd_convolve_vert_2tap_neon(const uint16_t *src,
                                           ptrdiff_t src_stride, uint16_t *dst,
                                           ptrdiff_t dst_stride,
                                           const int16_t *filter, int w, int h,
                                           int bd) {
  const int16x4_t f3 = vdup_n_s16(filter[3]);
  const int16x4_t f4 = vdup_n_s16(filter[4]);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);

  for (int x = 0; x + 8 <= w; x += 8) {
    const uint16_t *sp = src + x;
    uint16_t *dp = dst + x;
    int16x8_t r0 = vreinterpretq_s16_u16(vld1q_u16(sp));
    int y = 0;
    for (; y + 2 <= h; y += 2) {
      const int16x8_t r1 = vreinterpretq_s16_u16(vld1q_u16(sp + src_stride));
      const int16x8_t r2 =
          vreinterpretq_s16_u16(vld1q_u16(sp + 2 * src_stride));

      int32x4_t lo0 = vmull_s16(vget_low_s16(r0), f3);
      lo0 = vmlal_s16(lo0, vget_low_s16(r1), f4);
      int32x4_t hi0 = vmull_s16(vget_high_s16(r0), f3);
      hi0 = vmlal_s16(hi0, vget_high_s16(r1), f4);
      uint16x8_t res0 =
          vcombine_u16(vqrshrun_n_s32(lo0, 7), vqrshrun_n_s32(hi0, 7));
      vst1q_u16(dp, vminq_u16(res0, max_val));

      int32x4_t lo1 = vmull_s16(vget_low_s16(r1), f3);
      lo1 = vmlal_s16(lo1, vget_low_s16(r2), f4);
      int32x4_t hi1 = vmull_s16(vget_high_s16(r1), f3);
      hi1 = vmlal_s16(hi1, vget_high_s16(r2), f4);
      uint16x8_t res1 =
          vcombine_u16(vqrshrun_n_s32(lo1, 7), vqrshrun_n_s32(hi1, 7));
      vst1q_u16(dp + dst_stride, vminq_u16(res1, max_val));

      r0 = r2;
      sp += 2 * src_stride;
      dp += 2 * dst_stride;
    }
    for (; y < h; ++y) {
      const int16x8_t r1 = vreinterpretq_s16_u16(vld1q_u16(sp + src_stride));
      int32x4_t lo = vmull_s16(vget_low_s16(r0), f3);
      lo = vmlal_s16(lo, vget_low_s16(r1), f4);
      int32x4_t hi = vmull_s16(vget_high_s16(r0), f3);
      hi = vmlal_s16(hi, vget_high_s16(r1), f4);
      uint16x8_t res =
          vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
      vst1q_u16(dp, vminq_u16(res, max_val));
      r0 = r1;
      sp += src_stride;
      dp += dst_stride;
    }
  }
  for (int x = (w & ~7); x < w; ++x) {
    const uint16_t *sp = src + x;
    uint16_t *dp = dst + x;
    for (int y = 0; y < h; ++y) {
      int sum = (int)sp[0] * filter[3] + (int)sp[src_stride] * filter[4];
      *dp = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
      sp += src_stride;
      dp += dst_stride;
    }
  }
}

// Vertical 4-tap filter (taps 2-5 active).
static void highbd_convolve_vert_4tap_neon(const uint16_t *src,
                                           ptrdiff_t src_stride, uint16_t *dst,
                                           ptrdiff_t dst_stride,
                                           const int16_t *filter, int w, int h,
                                           int bd) {
  const int16x4_t f2 = vdup_n_s16(filter[2]);
  const int16x4_t f3 = vdup_n_s16(filter[3]);
  const int16x4_t f4 = vdup_n_s16(filter[4]);
  const int16x4_t f5 = vdup_n_s16(filter[5]);
  const uint16x8_t max_val = vdupq_n_u16((1 << bd) - 1);

  // Standard 8-tap offset adjusted for 4-tap: active taps start at index 2.
  src -= src_stride * 3;
  const uint16_t *s = src + 2 * src_stride;

  for (int x = 0; x + 8 <= w; x += 8) {
    const uint16_t *sp = s + x;
    uint16_t *dp = dst + x;

    int16x8_t r0 = vreinterpretq_s16_u16(vld1q_u16(sp + 0 * src_stride));
    int16x8_t r1 = vreinterpretq_s16_u16(vld1q_u16(sp + 1 * src_stride));
    int16x8_t r2 = vreinterpretq_s16_u16(vld1q_u16(sp + 2 * src_stride));
    sp += 3 * src_stride;

    for (int y = 0; y < h; ++y) {
      int16x8_t r3 = vreinterpretq_s16_u16(vld1q_u16(sp));

      int32x4_t lo = vmull_s16(vget_low_s16(r0), f2);
      lo = vmlal_s16(lo, vget_low_s16(r1), f3);
      lo = vmlal_s16(lo, vget_low_s16(r2), f4);
      lo = vmlal_s16(lo, vget_low_s16(r3), f5);
      int32x4_t hi = vmull_s16(vget_high_s16(r0), f2);
      hi = vmlal_s16(hi, vget_high_s16(r1), f3);
      hi = vmlal_s16(hi, vget_high_s16(r2), f4);
      hi = vmlal_s16(hi, vget_high_s16(r3), f5);
      uint16x8_t res =
          vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
      res = vminq_u16(res, max_val);
      vst1q_u16(dp, res);

      r0 = r1;
      r1 = r2;
      r2 = r3;
      sp += src_stride;
      dp += dst_stride;
    }
  }
  // Handle remaining columns < 8 wide.
  for (int x = (w & ~7); x < w; ++x) {
    const uint16_t *sp = s + x;
    uint16_t *dp = dst + x;
    for (int y = 0; y < h; ++y) {
      int sum = (int)sp[0 * src_stride] * filter[2] +
                (int)sp[1 * src_stride] * filter[3] +
                (int)sp[2 * src_stride] * filter[4] +
                (int)sp[3 * src_stride] * filter[5];
      *dp = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
      sp += src_stride;
      dp += dst_stride;
    }
  }
}

static INLINE int16x8_t highbd_convolve8_8_v(int16x8_t s0, int16x8_t s1,
                                             int16x8_t s2, int16x8_t s3,
                                             int16x8_t s4, int16x8_t s5,
                                             int16x8_t s6, int16x8_t s7,
                                             int16x8_t f, int16x8_t max_val) {
  int32x4_t lo = vmull_lane_s16(vget_low_s16(s0), vget_low_s16(f), 0);
  lo = vmlal_lane_s16(lo, vget_low_s16(s1), vget_low_s16(f), 1);
  lo = vmlal_lane_s16(lo, vget_low_s16(s2), vget_low_s16(f), 2);
  lo = vmlal_lane_s16(lo, vget_low_s16(s3), vget_low_s16(f), 3);
  lo = vmlal_lane_s16(lo, vget_low_s16(s4), vget_high_s16(f), 0);
  lo = vmlal_lane_s16(lo, vget_low_s16(s5), vget_high_s16(f), 1);
  lo = vmlal_lane_s16(lo, vget_low_s16(s6), vget_high_s16(f), 2);
  lo = vmlal_lane_s16(lo, vget_low_s16(s7), vget_high_s16(f), 3);
  int32x4_t hi = vmull_lane_s16(vget_high_s16(s0), vget_low_s16(f), 0);
  hi = vmlal_lane_s16(hi, vget_high_s16(s1), vget_low_s16(f), 1);
  hi = vmlal_lane_s16(hi, vget_high_s16(s2), vget_low_s16(f), 2);
  hi = vmlal_lane_s16(hi, vget_high_s16(s3), vget_low_s16(f), 3);
  hi = vmlal_lane_s16(hi, vget_high_s16(s4), vget_high_s16(f), 0);
  hi = vmlal_lane_s16(hi, vget_high_s16(s5), vget_high_s16(f), 1);
  hi = vmlal_lane_s16(hi, vget_high_s16(s6), vget_high_s16(f), 2);
  hi = vmlal_lane_s16(hi, vget_high_s16(s7), vget_high_s16(f), 3);
  uint16x8_t res = vcombine_u16(vqrshrun_n_s32(lo, 7), vqrshrun_n_s32(hi, 7));
  return vreinterpretq_s16_u16(vminq_u16(res, vreinterpretq_u16_s16(max_val)));
}

// Vertical 8-tap filter (all taps active).
static void highbd_convolve_vert_8tap_neon(const uint16_t *src,
                                           ptrdiff_t src_stride, uint16_t *dst,
                                           ptrdiff_t dst_stride,
                                           const int16_t *filter, int w, int h,
                                           int bd) {
  const int16x8_t f = vld1q_s16(filter);
  const int16x8_t max_val = vdupq_n_s16((1 << bd) - 1);

  src -= src_stride * 3;

  for (int x = 0; x + 8 <= w; x += 8) {
    const uint16_t *sp = src + x;
    uint16_t *dp = dst + x;

    int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(sp + 0 * src_stride));
    int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(sp + 1 * src_stride));
    int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(sp + 2 * src_stride));
    int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(sp + 3 * src_stride));
    int16x8_t s4 = vreinterpretq_s16_u16(vld1q_u16(sp + 4 * src_stride));
    int16x8_t s5 = vreinterpretq_s16_u16(vld1q_u16(sp + 5 * src_stride));
    int16x8_t s6 = vreinterpretq_s16_u16(vld1q_u16(sp + 6 * src_stride));
    sp += 7 * src_stride;

    for (int y = 0; y < h; ++y) {
      int16x8_t s7 = vreinterpretq_s16_u16(vld1q_u16(sp));
      int16x8_t res =
          highbd_convolve8_8_v(s0, s1, s2, s3, s4, s5, s6, s7, f, max_val);
      vst1q_u16(dp, vreinterpretq_u16_s16(res));

      s0 = s1;
      s1 = s2;
      s2 = s3;
      s3 = s4;
      s4 = s5;
      s5 = s6;
      s6 = s7;
      sp += src_stride;
      dp += dst_stride;
    }
  }
  for (int x = (w & ~7); x < w; ++x) {
    const uint16_t *sp = src + x;
    uint16_t *dp = dst + x;
    for (int y = 0; y < h; ++y) {
      int sum = 0;
      for (int k = 0; k < 8; ++k) sum += sp[k * src_stride] * filter[k];
      *dp = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
      sp += src_stride;
      dp += dst_stride;
    }
  }
}

void avm_highbd_convolve8_vert_neon(const uint16_t *src, ptrdiff_t src_stride,
                                    uint16_t *dst, ptrdiff_t dst_stride,
                                    const int16_t *filter_x, int x_step_q4,
                                    const int16_t *filter_y, int y_step_q4,
                                    int w, int h, int bd) {
  const InterpKernel *const filters_y = get_filter_base(filter_y);
  const int y0_q4 = get_filter_offset(filter_y, filters_y);
  (void)filter_x;
  (void)x_step_q4;

  if (y_step_q4 == 16) {
    if (filter_y[3] == 128) {
      highbd_convolve_copy_neon(src, src_stride, dst, dst_stride, w, h);
      return;
    }
    const int16_t *f = filters_y[y0_q4 & SUBPEL_MASK];
    if ((f[0] | f[1] | f[6] | f[7]) == 0) {
      if ((f[2] | f[5]) == 0) {
        highbd_convolve_vert_2tap_neon(src, src_stride, dst, dst_stride, f, w,
                                       h, bd);
      } else {
        highbd_convolve_vert_4tap_neon(src, src_stride, dst, dst_stride, f, w,
                                       h, bd);
      }
      return;
    }
    highbd_convolve_vert_8tap_neon(src, src_stride, dst, dst_stride, f, w, h,
                                   bd);
    return;
  }
  avm_highbd_convolve8_vert_c(src, src_stride, dst, dst_stride, filter_x,
                              x_step_q4, filter_y, y_step_q4, w, h, bd);
}
