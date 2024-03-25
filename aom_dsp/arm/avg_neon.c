/*
 *  Copyright (c) 2019, Alliance for Open Media. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>

#include "config/aom_dsp_rtcd.h"
#include "aom/aom_integer.h"
#include "aom_dsp/arm/sum_neon.h"
#include "aom_dsp/arm/mem_neon.h"
#include "aom_dsp/arm/transpose_neon.h"

int aom_satd_lp_neon(const int16_t *coeff, int length) {
  const int16x4_t zero = vdup_n_s16(0);
  int32x4_t accum = vdupq_n_s32(0);

  do {
    const int16x8_t src0 = vld1q_s16(coeff);
    const int16x8_t src8 = vld1q_s16(coeff + 8);
    accum = vabal_s16(accum, vget_low_s16(src0), zero);
    accum = vabal_s16(accum, vget_high_s16(src0), zero);
    accum = vabal_s16(accum, vget_low_s16(src8), zero);
    accum = vabal_s16(accum, vget_high_s16(src8), zero);
    length -= 16;
    coeff += 16;
  } while (length != 0);

  {
    // satd: 26 bits, dynamic range [-32640 * 1024, 32640 * 1024]
    const int64x2_t s0 = vpaddlq_s32(accum);  // cascading summation of 'accum'.
    const int32x2_t s1 = vadd_s32(vreinterpret_s32_s64(vget_low_s64(s0)),
                                  vreinterpret_s32_s64(vget_high_s64(s0)));
    const int satd = vget_lane_s32(s1, 0);
    return satd;
  }
}

// coeff: 16 bits, dynamic range [-32640, 32640].
// length: value range {16, 64, 256, 1024}.
int aom_satd_neon(const tran_low_t *coeff, int length) {
  const int32x4_t zero = vdupq_n_s32(0);
  int32x4_t accum = zero;
  do {
    const int32x4_t src0 = vld1q_s32(&coeff[0]);
    const int32x4_t src8 = vld1q_s32(&coeff[4]);
    const int32x4_t src16 = vld1q_s32(&coeff[8]);
    const int32x4_t src24 = vld1q_s32(&coeff[12]);
    accum = vabaq_s32(accum, src0, zero);
    accum = vabaq_s32(accum, src8, zero);
    accum = vabaq_s32(accum, src16, zero);
    accum = vabaq_s32(accum, src24, zero);
    length -= 16;
    coeff += 16;
  } while (length != 0);

  // satd: 26 bits, dynamic range [-32640 * 1024, 32640 * 1024]
#ifdef __aarch64__
  return vaddvq_s32(accum);
#else
  return horizontal_add_s32x4(accum);
#endif  // __aarch64__
}

int aom_vector_var_neon(const int16_t *ref, const int16_t *src, const int bwl) {
  int32x4_t v_mean = vdupq_n_s32(0);
  int32x4_t v_sse = v_mean;
  int16x8_t v_ref, v_src;
  int16x4_t v_low;

  int i, width = 4 << bwl;
  for (i = 0; i < width; i += 8) {
    v_ref = vld1q_s16(&ref[i]);
    v_src = vld1q_s16(&src[i]);
    const int16x8_t diff = vsubq_s16(v_ref, v_src);
    // diff: dynamic range [-510, 510], 10 bits.
    v_mean = vpadalq_s16(v_mean, diff);
    v_low = vget_low_s16(diff);
    v_sse = vmlal_s16(v_sse, v_low, v_low);
#if defined(__aarch64__)
    v_sse = vmlal_high_s16(v_sse, diff, diff);
#else
    const int16x4_t v_high = vget_high_s16(diff);
    v_sse = vmlal_s16(v_sse, v_high, v_high);
#endif
  }
#if defined(__aarch64__)
  const int mean = vaddvq_s32(v_mean);
  const uint32_t sse = (uint32_t)vaddvq_s32(v_sse);
#else
  const int mean = horizontal_add_s32x4(v_mean);
  const uint32_t sse = (uint32_t)horizontal_add_s32x4(v_sse);
#endif
  // (mean * mean): dynamic range 32 bits - can be stored in uint32_t
  const uint32_t meansq = (uint32_t)abs(mean) * (uint32_t)abs(mean);
  const int var = sse - (int)(meansq >> (bwl + 2));
  return var;
}
