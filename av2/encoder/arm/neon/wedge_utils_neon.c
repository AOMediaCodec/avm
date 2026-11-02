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

#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/arm/sum_neon.h"
#include "av2/common/reconinter.h"

// Returns clamp(MAX_MASK_VALUE * r1[i] + m[i] * d[i]) for 16 elements.
static INLINE void wedge_blend_16(const int16_t *r1, const int16_t *d,
                                  const uint8_t *m, int16x4_t t[4]) {
  const int16x8_t v_r0 = vld1q_s16(r1);
  const int16x8_t v_r1 = vld1q_s16(r1 + 8);
  const int16x8_t v_d0 = vld1q_s16(d);
  const int16x8_t v_d1 = vld1q_s16(d + 8);
  const uint8x16_t v_m_u8 = vld1q_u8(m);

  const uint8x16x2_t v_mz = vzipq_u8(v_m_u8, vdupq_n_u8(0));
  const int16x8_t v_m0 = vreinterpretq_s16_u8(v_mz.val[0]);
  const int16x8_t v_m1 = vreinterpretq_s16_u8(v_mz.val[1]);

  int32x4_t v_t0l = vshll_n_s16(vget_low_s16(v_r0), WEDGE_WEIGHT_BITS);
  int32x4_t v_t0h = vshll_n_s16(vget_high_s16(v_r0), WEDGE_WEIGHT_BITS);
  int32x4_t v_t1l = vshll_n_s16(vget_low_s16(v_r1), WEDGE_WEIGHT_BITS);
  int32x4_t v_t1h = vshll_n_s16(vget_high_s16(v_r1), WEDGE_WEIGHT_BITS);

  v_t0l = vmlal_s16(v_t0l, vget_low_s16(v_m0), vget_low_s16(v_d0));
  v_t0h = vmlal_s16(v_t0h, vget_high_s16(v_m0), vget_high_s16(v_d0));
  v_t1l = vmlal_s16(v_t1l, vget_low_s16(v_m1), vget_low_s16(v_d1));
  v_t1h = vmlal_s16(v_t1h, vget_high_s16(v_m1), vget_high_s16(v_d1));

  t[0] = vqmovn_s32(v_t0l);
  t[1] = vqmovn_s32(v_t0h);
  t[2] = vqmovn_s32(v_t1l);
  t[3] = vqmovn_s32(v_t1h);
}

uint64_t av2_wedge_sse_from_residuals_neon(const int16_t *r1, const int16_t *d,
                                           const uint8_t *m, int N) {
  assert(N % 64 == 0);

  // N >= 64, so the loop runs at least once.
  uint64x2_t v_acc0 = vdupq_n_u64(0);
  uint64x2_t v_acc1 = vdupq_n_u64(0);

  const int16_t *const r1_end = r1 + N;
  do {
    int16x4_t t[8];
    wedge_blend_16(r1, d, m, t);
    wedge_blend_16(r1 + 16, d + 16, m + 16, t + 4);

    // Each square of a clamped term is at most 2^30, so a u32 lane holds the
    // sum of 3 squares (< 2^32) before widening; the signed multiply-
    // accumulates wrap mod 2^32 and give the exact unsigned sum.
    int32x4_t v_sq0 = vmull_s16(t[0], t[0]);
    v_sq0 = vmlal_s16(v_sq0, t[1], t[1]);
    v_sq0 = vmlal_s16(v_sq0, t[2], t[2]);
    int32x4_t v_sq1 = vmull_s16(t[3], t[3]);
    v_sq1 = vmlal_s16(v_sq1, t[4], t[4]);
    v_sq1 = vmlal_s16(v_sq1, t[5], t[5]);
    int32x4_t v_sq2 = vmull_s16(t[6], t[6]);
    v_sq2 = vmlal_s16(v_sq2, t[7], t[7]);

    v_acc0 = vpadalq_u32(v_acc0, vreinterpretq_u32_s32(v_sq0));
    v_acc1 = vpadalq_u32(v_acc1, vreinterpretq_u32_s32(v_sq1));
    v_acc0 = vpadalq_u32(v_acc0, vreinterpretq_u32_s32(v_sq2));
    r1 += 32;
    d += 32;
    m += 32;
  } while (r1 < r1_end);

  return ROUND_POWER_OF_TWO(horizontal_add_u64x2(vaddq_u64(v_acc0, v_acc1)),
                            2 * WEDGE_WEIGHT_BITS);
}
