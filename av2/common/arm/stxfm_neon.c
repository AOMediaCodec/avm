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

#include "av2/common/av2_txfm.h"
#include "av2/common/enums.h"

static INLINE int32x4_t round_shift_s32(int32x4_t v) {
  const int32x4_t bias = vdupq_n_s32(64);
  const int32x4_t sign = vshrq_n_s32(v, 31);
  return vshrq_n_s32(vaddq_s32(vaddq_s32(v, bias), sign), 7);
}

static INLINE int32x4_t clamp_s32(int32x4_t v, int32x4_t min_v,
                                  int32x4_t max_v) {
  return vminq_s32(vmaxq_s32(v, min_v), max_v);
}

static void inv_stxfm_4x4_kernel(const tran_low_t *src, tran_low_t *dst,
                                 const int32_t *kernel, int bd) {
  const int32x4_t max_v = vdupq_n_s32((1 << (7 + bd)) - 1);
  const int32x4_t min_v = vdupq_n_s32(-(1 << (7 + bd)));

  const int32x4_t s0 = vld1q_s32(src);
  const int32x4_t s1 = vld1q_s32(src + 4);
  const int32x2_t s0_lo = vget_low_s32(s0);
  const int32x2_t s0_hi = vget_high_s32(s0);
  const int32x2_t s1_lo = vget_low_s32(s1);
  const int32x2_t s1_hi = vget_high_s32(s1);
  const int32_t *k = kernel;

  int32x4_t a0 = vmulq_lane_s32(vld1q_s32(k), s0_lo, 0);
  int32x4_t a1 = vmulq_lane_s32(vld1q_s32(k + 4), s0_lo, 0);
  int32x4_t a2 = vmulq_lane_s32(vld1q_s32(k + 8), s0_lo, 0);
  int32x4_t a3 = vmulq_lane_s32(vld1q_s32(k + 12), s0_lo, 0);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s0_lo, 1);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s0_lo, 1);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s0_lo, 1);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s0_lo, 1);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s0_hi, 0);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s0_hi, 0);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s0_hi, 0);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s0_hi, 0);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s0_hi, 1);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s0_hi, 1);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s0_hi, 1);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s0_hi, 1);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s1_lo, 0);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s1_lo, 0);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s1_lo, 0);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s1_lo, 0);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s1_lo, 1);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s1_lo, 1);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s1_lo, 1);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s1_lo, 1);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s1_hi, 0);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s1_hi, 0);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s1_hi, 0);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s1_hi, 0);
  k += IST_4x4_WIDTH;
  a0 = vmlaq_lane_s32(a0, vld1q_s32(k), s1_hi, 1);
  a1 = vmlaq_lane_s32(a1, vld1q_s32(k + 4), s1_hi, 1);
  a2 = vmlaq_lane_s32(a2, vld1q_s32(k + 8), s1_hi, 1);
  a3 = vmlaq_lane_s32(a3, vld1q_s32(k + 12), s1_hi, 1);

  vst1q_s32(dst, clamp_s32(round_shift_s32(a0), min_v, max_v));
  vst1q_s32(dst + 4, clamp_s32(round_shift_s32(a1), min_v, max_v));
  vst1q_s32(dst + 8, clamp_s32(round_shift_s32(a2), min_v, max_v));
  vst1q_s32(dst + 12, clamp_s32(round_shift_s32(a3), min_v, max_v));
}

static void inv_stxfm_8x8_kernel(const tran_low_t *src, tran_low_t *dst,
                                 const int32_t *kernel, int rh, int bd) {
  assert(rh % 4 == 0);
  const int32x4_t max_v = vdupq_n_s32((1 << (7 + bd)) - 1);
  const int32x4_t min_v = vdupq_n_s32(-(1 << (7 + bd)));

  int32x4_t a0 = vdupq_n_s32(0);
  int32x4_t a1 = vdupq_n_s32(0);
  int32x4_t a2 = vdupq_n_s32(0);
  int32x4_t a3 = vdupq_n_s32(0);
  int32x4_t a4 = vdupq_n_s32(0);
  int32x4_t a5 = vdupq_n_s32(0);
  int32x4_t a6 = vdupq_n_s32(0);
  int32x4_t a7 = vdupq_n_s32(0);
  int32x4_t a8 = vdupq_n_s32(0);
  int32x4_t a9 = vdupq_n_s32(0);
  int32x4_t a10 = vdupq_n_s32(0);
  int32x4_t a11 = vdupq_n_s32(0);

  for (int j = 0; j < rh; j += 4) {
    const int32x4_t s = vld1q_s32(src + j);
    const int32x2_t s_lo = vget_low_s32(s);
    const int32x2_t s_hi = vget_high_s32(s);
    const int32_t *k0 = kernel + j * IST_8x8_WIDTH;
    const int32_t *k1 = k0 + IST_8x8_WIDTH;
    const int32_t *k2 = k1 + IST_8x8_WIDTH;
    const int32_t *k3 = k2 + IST_8x8_WIDTH;

    a0 = vmlaq_lane_s32(a0, vld1q_s32(k0), s_lo, 0);
    a1 = vmlaq_lane_s32(a1, vld1q_s32(k0 + 4), s_lo, 0);
    a2 = vmlaq_lane_s32(a2, vld1q_s32(k0 + 8), s_lo, 0);
    a3 = vmlaq_lane_s32(a3, vld1q_s32(k0 + 12), s_lo, 0);
    a4 = vmlaq_lane_s32(a4, vld1q_s32(k0 + 16), s_lo, 0);
    a5 = vmlaq_lane_s32(a5, vld1q_s32(k0 + 20), s_lo, 0);
    a6 = vmlaq_lane_s32(a6, vld1q_s32(k0 + 24), s_lo, 0);
    a7 = vmlaq_lane_s32(a7, vld1q_s32(k0 + 28), s_lo, 0);
    a8 = vmlaq_lane_s32(a8, vld1q_s32(k0 + 32), s_lo, 0);
    a9 = vmlaq_lane_s32(a9, vld1q_s32(k0 + 36), s_lo, 0);
    a10 = vmlaq_lane_s32(a10, vld1q_s32(k0 + 40), s_lo, 0);
    a11 = vmlaq_lane_s32(a11, vld1q_s32(k0 + 44), s_lo, 0);

    a0 = vmlaq_lane_s32(a0, vld1q_s32(k1), s_lo, 1);
    a1 = vmlaq_lane_s32(a1, vld1q_s32(k1 + 4), s_lo, 1);
    a2 = vmlaq_lane_s32(a2, vld1q_s32(k1 + 8), s_lo, 1);
    a3 = vmlaq_lane_s32(a3, vld1q_s32(k1 + 12), s_lo, 1);
    a4 = vmlaq_lane_s32(a4, vld1q_s32(k1 + 16), s_lo, 1);
    a5 = vmlaq_lane_s32(a5, vld1q_s32(k1 + 20), s_lo, 1);
    a6 = vmlaq_lane_s32(a6, vld1q_s32(k1 + 24), s_lo, 1);
    a7 = vmlaq_lane_s32(a7, vld1q_s32(k1 + 28), s_lo, 1);
    a8 = vmlaq_lane_s32(a8, vld1q_s32(k1 + 32), s_lo, 1);
    a9 = vmlaq_lane_s32(a9, vld1q_s32(k1 + 36), s_lo, 1);
    a10 = vmlaq_lane_s32(a10, vld1q_s32(k1 + 40), s_lo, 1);
    a11 = vmlaq_lane_s32(a11, vld1q_s32(k1 + 44), s_lo, 1);

    a0 = vmlaq_lane_s32(a0, vld1q_s32(k2), s_hi, 0);
    a1 = vmlaq_lane_s32(a1, vld1q_s32(k2 + 4), s_hi, 0);
    a2 = vmlaq_lane_s32(a2, vld1q_s32(k2 + 8), s_hi, 0);
    a3 = vmlaq_lane_s32(a3, vld1q_s32(k2 + 12), s_hi, 0);
    a4 = vmlaq_lane_s32(a4, vld1q_s32(k2 + 16), s_hi, 0);
    a5 = vmlaq_lane_s32(a5, vld1q_s32(k2 + 20), s_hi, 0);
    a6 = vmlaq_lane_s32(a6, vld1q_s32(k2 + 24), s_hi, 0);
    a7 = vmlaq_lane_s32(a7, vld1q_s32(k2 + 28), s_hi, 0);
    a8 = vmlaq_lane_s32(a8, vld1q_s32(k2 + 32), s_hi, 0);
    a9 = vmlaq_lane_s32(a9, vld1q_s32(k2 + 36), s_hi, 0);
    a10 = vmlaq_lane_s32(a10, vld1q_s32(k2 + 40), s_hi, 0);
    a11 = vmlaq_lane_s32(a11, vld1q_s32(k2 + 44), s_hi, 0);

    a0 = vmlaq_lane_s32(a0, vld1q_s32(k3), s_hi, 1);
    a1 = vmlaq_lane_s32(a1, vld1q_s32(k3 + 4), s_hi, 1);
    a2 = vmlaq_lane_s32(a2, vld1q_s32(k3 + 8), s_hi, 1);
    a3 = vmlaq_lane_s32(a3, vld1q_s32(k3 + 12), s_hi, 1);
    a4 = vmlaq_lane_s32(a4, vld1q_s32(k3 + 16), s_hi, 1);
    a5 = vmlaq_lane_s32(a5, vld1q_s32(k3 + 20), s_hi, 1);
    a6 = vmlaq_lane_s32(a6, vld1q_s32(k3 + 24), s_hi, 1);
    a7 = vmlaq_lane_s32(a7, vld1q_s32(k3 + 28), s_hi, 1);
    a8 = vmlaq_lane_s32(a8, vld1q_s32(k3 + 32), s_hi, 1);
    a9 = vmlaq_lane_s32(a9, vld1q_s32(k3 + 36), s_hi, 1);
    a10 = vmlaq_lane_s32(a10, vld1q_s32(k3 + 40), s_hi, 1);
    a11 = vmlaq_lane_s32(a11, vld1q_s32(k3 + 44), s_hi, 1);
  }

  vst1q_s32(dst, clamp_s32(round_shift_s32(a0), min_v, max_v));
  vst1q_s32(dst + 4, clamp_s32(round_shift_s32(a1), min_v, max_v));
  vst1q_s32(dst + 8, clamp_s32(round_shift_s32(a2), min_v, max_v));
  vst1q_s32(dst + 12, clamp_s32(round_shift_s32(a3), min_v, max_v));
  vst1q_s32(dst + 16, clamp_s32(round_shift_s32(a4), min_v, max_v));
  vst1q_s32(dst + 20, clamp_s32(round_shift_s32(a5), min_v, max_v));
  vst1q_s32(dst + 24, clamp_s32(round_shift_s32(a6), min_v, max_v));
  vst1q_s32(dst + 28, clamp_s32(round_shift_s32(a7), min_v, max_v));
  vst1q_s32(dst + 32, clamp_s32(round_shift_s32(a8), min_v, max_v));
  vst1q_s32(dst + 36, clamp_s32(round_shift_s32(a9), min_v, max_v));
  vst1q_s32(dst + 40, clamp_s32(round_shift_s32(a10), min_v, max_v));
  vst1q_s32(dst + 44, clamp_s32(round_shift_s32(a11), min_v, max_v));
}

void inv_stxfm_neon(tran_low_t *src, tran_low_t *dst,
                    const PREDICTION_MODE mode, const uint8_t stx_idx,
                    const int size, const int bd) {
  assert(stx_idx < 4);
  const int32_t *kernel = (size == 0) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];
  if (size == 0) {
    inv_stxfm_4x4_kernel(src, dst, kernel, bd);
  } else {
    int rh = (size == 1)   ? IST_8x8_HEIGHT_RED
             : (size == 3) ? IST_ADST_NZ_CNT
                           : IST_8x8_HEIGHT;
    inv_stxfm_8x8_kernel(src, dst, kernel, rh, bd);
  }
}
