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

static INLINE int32x4_t horizontal_add_4d_s32x4_fast(const int32x4_t sum[4]) {
  const int32x2_t p0 = vpadd_s32(vget_low_s32(sum[0]), vget_high_s32(sum[0]));
  const int32x2_t p1 = vpadd_s32(vget_low_s32(sum[1]), vget_high_s32(sum[1]));
  const int32x2_t p2 = vpadd_s32(vget_low_s32(sum[2]), vget_high_s32(sum[2]));
  const int32x2_t p3 = vpadd_s32(vget_low_s32(sum[3]), vget_high_s32(sum[3]));
  const int32x2_t q01 = vpadd_s32(p0, p1);
  const int32x2_t q23 = vpadd_s32(p2, p3);
  return vcombine_s32(q01, q23);
}

static INLINE void fwd_stxfm_4x4_neon(const int32_t *src, int32_t *dst,
                                      const int32_t *kernel, int bd) {
  const int32x4_t max_v = vdupq_n_s32((1 << (7 + bd)) - 1);
  const int32x4_t min_v = vdupq_n_s32(-(1 << (7 + bd)));
  const int32x4_t bias = vdupq_n_s32(64);

  const int32x4_t s0 = vld1q_s32(src);
  const int32x4_t s1 = vld1q_s32(src + 4);
  const int32x4_t s2 = vld1q_s32(src + 8);
  const int32x4_t s3 = vld1q_s32(src + 12);

  int32x4_t a0 = vmulq_s32(s0, vld1q_s32(kernel));
  int32x4_t a1 = vmulq_s32(s0, vld1q_s32(kernel + 16));
  int32x4_t a2 = vmulq_s32(s0, vld1q_s32(kernel + 32));
  int32x4_t a3 = vmulq_s32(s0, vld1q_s32(kernel + 48));
  int32x4_t a4 = vmulq_s32(s0, vld1q_s32(kernel + 64));
  int32x4_t a5 = vmulq_s32(s0, vld1q_s32(kernel + 80));
  int32x4_t a6 = vmulq_s32(s0, vld1q_s32(kernel + 96));
  int32x4_t a7 = vmulq_s32(s0, vld1q_s32(kernel + 112));

  a0 = vmlaq_s32(a0, s1, vld1q_s32(kernel + 4));
  a1 = vmlaq_s32(a1, s1, vld1q_s32(kernel + 20));
  a2 = vmlaq_s32(a2, s1, vld1q_s32(kernel + 36));
  a3 = vmlaq_s32(a3, s1, vld1q_s32(kernel + 52));
  a4 = vmlaq_s32(a4, s1, vld1q_s32(kernel + 68));
  a5 = vmlaq_s32(a5, s1, vld1q_s32(kernel + 84));
  a6 = vmlaq_s32(a6, s1, vld1q_s32(kernel + 100));
  a7 = vmlaq_s32(a7, s1, vld1q_s32(kernel + 116));

  a0 = vmlaq_s32(a0, s2, vld1q_s32(kernel + 8));
  a1 = vmlaq_s32(a1, s2, vld1q_s32(kernel + 24));
  a2 = vmlaq_s32(a2, s2, vld1q_s32(kernel + 40));
  a3 = vmlaq_s32(a3, s2, vld1q_s32(kernel + 56));
  a4 = vmlaq_s32(a4, s2, vld1q_s32(kernel + 72));
  a5 = vmlaq_s32(a5, s2, vld1q_s32(kernel + 88));
  a6 = vmlaq_s32(a6, s2, vld1q_s32(kernel + 104));
  a7 = vmlaq_s32(a7, s2, vld1q_s32(kernel + 120));

  a0 = vmlaq_s32(a0, s3, vld1q_s32(kernel + 12));
  a1 = vmlaq_s32(a1, s3, vld1q_s32(kernel + 28));
  a2 = vmlaq_s32(a2, s3, vld1q_s32(kernel + 44));
  a3 = vmlaq_s32(a3, s3, vld1q_s32(kernel + 60));
  a4 = vmlaq_s32(a4, s3, vld1q_s32(kernel + 76));
  a5 = vmlaq_s32(a5, s3, vld1q_s32(kernel + 92));
  a6 = vmlaq_s32(a6, s3, vld1q_s32(kernel + 108));
  a7 = vmlaq_s32(a7, s3, vld1q_s32(kernel + 124));

  const int32x4_t sum0[4] = { a0, a1, a2, a3 };
  const int32x4_t sum1[4] = { a4, a5, a6, a7 };
  int32x4_t c0 = horizontal_add_4d_s32x4_fast(sum0);
  int32x4_t c1 = horizontal_add_4d_s32x4_fast(sum1);
  int32x4_t sign0 = vshrq_n_s32(c0, 31);
  c0 = vshrq_n_s32(vaddq_s32(vaddq_s32(c0, bias), sign0), 7);
  c0 = vminq_s32(vmaxq_s32(c0, min_v), max_v);
  int32x4_t sign1 = vshrq_n_s32(c1, 31);
  c1 = vshrq_n_s32(vaddq_s32(vaddq_s32(c1, bias), sign1), 7);
  c1 = vminq_s32(vmaxq_s32(c1, min_v), max_v);
  vst1q_s32(dst, c0);
  vst1q_s32(dst + 4, c1);
}

static INLINE void fwd_stxfm_8x8_neon(const int32_t *src, int32_t *dst,
                                      const int32_t *kernel, int reduced_height,
                                      int bd) {
  assert(reduced_height % 4 == 0);
  assert(IST_8x8_WIDTH % 4 == 0);
  const int32x4_t max_v = vdupq_n_s32((1 << (7 + bd)) - 1);
  const int32x4_t min_v = vdupq_n_s32(-(1 << (7 + bd)));
  const int32x4_t bias = vdupq_n_s32(64);

  const int32x4_t src0 = vld1q_s32(src);
  const int32x4_t src1 = vld1q_s32(src + 4);
  const int32x4_t src2 = vld1q_s32(src + 8);
  const int32x4_t src3 = vld1q_s32(src + 12);
  const int32x4_t src4 = vld1q_s32(src + 16);
  const int32x4_t src5 = vld1q_s32(src + 20);
  const int32x4_t src6 = vld1q_s32(src + 24);
  const int32x4_t src7 = vld1q_s32(src + 28);
  const int32x4_t src8 = vld1q_s32(src + 32);
  const int32x4_t src9 = vld1q_s32(src + 36);
  const int32x4_t src10 = vld1q_s32(src + 40);
  const int32x4_t src11 = vld1q_s32(src + 44);

  int j = 0;
  for (; j + 7 < reduced_height; j += 8) {
    int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
    int32x4_t a2 = vdupq_n_s32(0), a3 = vdupq_n_s32(0);
    int32x4_t a4 = vdupq_n_s32(0), a5 = vdupq_n_s32(0);
    int32x4_t a6 = vdupq_n_s32(0), a7 = vdupq_n_s32(0);
    const int32_t *k0 = kernel + (j + 0) * IST_8x8_WIDTH;
    const int32_t *k1 = kernel + (j + 1) * IST_8x8_WIDTH;
    const int32_t *k2 = kernel + (j + 2) * IST_8x8_WIDTH;
    const int32_t *k3 = kernel + (j + 3) * IST_8x8_WIDTH;
    const int32_t *k4 = kernel + (j + 4) * IST_8x8_WIDTH;
    const int32_t *k5 = kernel + (j + 5) * IST_8x8_WIDTH;
    const int32_t *k6 = kernel + (j + 6) * IST_8x8_WIDTH;
    const int32_t *k7 = kernel + (j + 7) * IST_8x8_WIDTH;

    // clang-format off
#define STEP(S, OFF)                              \
  a0 = vmlaq_s32(a0, S, vld1q_s32(k0 + OFF));    \
  a1 = vmlaq_s32(a1, S, vld1q_s32(k1 + OFF));    \
  a2 = vmlaq_s32(a2, S, vld1q_s32(k2 + OFF));    \
  a3 = vmlaq_s32(a3, S, vld1q_s32(k3 + OFF));    \
  a4 = vmlaq_s32(a4, S, vld1q_s32(k4 + OFF));    \
  a5 = vmlaq_s32(a5, S, vld1q_s32(k5 + OFF));    \
  a6 = vmlaq_s32(a6, S, vld1q_s32(k6 + OFF));    \
  a7 = vmlaq_s32(a7, S, vld1q_s32(k7 + OFF));

    STEP(src0, 0)
    STEP(src1, 4)
    STEP(src2, 8)
    STEP(src3, 12)
    STEP(src4, 16)
    STEP(src5, 20)
    STEP(src6, 24)
    STEP(src7, 28)
    STEP(src8, 32)
    STEP(src9, 36)
    STEP(src10, 40)
    STEP(src11, 44)
#undef STEP
    // clang-format on

    const int32x4_t sum0[4] = { a0, a1, a2, a3 };
    const int32x4_t sum1[4] = { a4, a5, a6, a7 };
    int32x4_t c0 = horizontal_add_4d_s32x4_fast(sum0);
    int32x4_t c1 = horizontal_add_4d_s32x4_fast(sum1);
    int32x4_t sign0 = vshrq_n_s32(c0, 31);
    c0 = vshrq_n_s32(vaddq_s32(vaddq_s32(c0, bias), sign0), 7);
    c0 = vminq_s32(vmaxq_s32(c0, min_v), max_v);
    int32x4_t sign1 = vshrq_n_s32(c1, 31);
    c1 = vshrq_n_s32(vaddq_s32(vaddq_s32(c1, bias), sign1), 7);
    c1 = vminq_s32(vmaxq_s32(c1, min_v), max_v);
    vst1q_s32(dst + j, c0);
    vst1q_s32(dst + j + 4, c1);
  }
  for (; j + 3 < reduced_height; j += 4) {
    int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
    int32x4_t a2 = vdupq_n_s32(0), a3 = vdupq_n_s32(0);
    const int32_t *k0 = kernel + (j + 0) * IST_8x8_WIDTH;
    const int32_t *k1 = kernel + (j + 1) * IST_8x8_WIDTH;
    const int32_t *k2 = kernel + (j + 2) * IST_8x8_WIDTH;
    const int32_t *k3 = kernel + (j + 3) * IST_8x8_WIDTH;
    for (int i = 0; i < IST_8x8_WIDTH; i += 4) {
      int32x4_t s = vld1q_s32(src + i);
      a0 = vmlaq_s32(a0, s, vld1q_s32(k0 + i));
      a1 = vmlaq_s32(a1, s, vld1q_s32(k1 + i));
      a2 = vmlaq_s32(a2, s, vld1q_s32(k2 + i));
      a3 = vmlaq_s32(a3, s, vld1q_s32(k3 + i));
    }
    const int32x4_t sum0[4] = { a0, a1, a2, a3 };
    int32x4_t c0 = horizontal_add_4d_s32x4_fast(sum0);
    int32x4_t sign0 = vshrq_n_s32(c0, 31);
    c0 = vshrq_n_s32(vaddq_s32(vaddq_s32(c0, bias), sign0), 7);
    c0 = vminq_s32(vmaxq_s32(c0, min_v), max_v);
    vst1q_s32(dst + j, c0);
  }
}

void fwd_stxfm_neon(tran_low_t *src, tran_low_t *dst,
                    const PREDICTION_MODE mode, const uint8_t stx_idx,
                    const int size, const int bd) {
  assert(stx_idx < 4);
  if (size == 0) {
    const int32_t *kernel = ist_4x4_kernel_int32[mode][stx_idx][0];
    fwd_stxfm_4x4_neon(src, dst, kernel, bd);
  } else {
    const int32_t *kernel = ist_8x8_kernel_int32[mode][stx_idx][0];
    int rh = (size == 1)   ? IST_8x8_HEIGHT_RED
             : (size == 3) ? IST_ADST_NZ_CNT
                           : IST_8x8_HEIGHT;
    fwd_stxfm_8x8_neon(src, dst, kernel, rh, bd);
  }
}
