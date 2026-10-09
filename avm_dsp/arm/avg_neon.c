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

#include "config/avm_dsp_rtcd.h"
#include "avm/avm_integer.h"
#include "avm_dsp/arm/sum_neon.h"
#include "avm_dsp/arm/mem_neon.h"
#include "avm_dsp/arm/transpose_neon.h"

int avm_satd_lp_neon(const int16_t *coeff, int length) {
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
int avm_satd_neon(const tran_low_t *coeff, int length) {
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

void avm_int_pro_row_neon(int16_t *hbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 8 == 0);
  assert(height % 4 == 0);
  const int32x4_t norm = vdupq_n_s32(-norm_factor);

  for (int idx = 0; idx < width; idx += 8) {
    uint32x4_t s0 = vdupq_n_u32(0);
    uint32x4_t s1 = vdupq_n_u32(0);
    const uint16_t *ref_tmp = ref + idx;
    for (int y = 0; y < height; y += 4) {
      const uint16x8_t r0 = vld1q_u16(ref_tmp);
      const uint16x8_t r1 = vld1q_u16(ref_tmp + ref_stride);
      const uint16x8_t r2 = vld1q_u16(ref_tmp + 2 * ref_stride);
      const uint16x8_t r3 = vld1q_u16(ref_tmp + 3 * ref_stride);
      const uint16x8_t r0123 = vaddq_u16(vaddq_u16(r0, r1), vaddq_u16(r2, r3));
      s0 = vaddw_u16(s0, vget_low_u16(r0123));
      s1 = vaddw_u16(s1, vget_high_u16(r0123));
      ref_tmp += 4 * ref_stride;
    }
    s0 = vshlq_u32(s0, norm);
    s1 = vshlq_u32(s1, norm);
    const int16x8_t res =
        vreinterpretq_s16_u16(vcombine_u16(vmovn_u32(s0), vmovn_u32(s1)));
    vst1q_s16(hbuf + idx, res);
  }
}

void avm_int_pro_col_neon(int16_t *vbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 8 == 0);
  assert(height % 4 == 0);
  const int32x4_t norm = vdupq_n_s32(-norm_factor);

  for (int ht = 0; ht < height; ht += 4) {
    const uint16_t *r0 = ref;
    const uint16_t *r1 = r0 + ref_stride;
    const uint16_t *r2 = r1 + ref_stride;
    const uint16_t *r3 = r2 + ref_stride;
    uint32x4_t acc[4] = { vdupq_n_u32(0), vdupq_n_u32(0), vdupq_n_u32(0),
                          vdupq_n_u32(0) };
    for (int idx = 0; idx < width; idx += 8) {
      acc[0] = vpadalq_u16(acc[0], vld1q_u16(r0 + idx));
      acc[1] = vpadalq_u16(acc[1], vld1q_u16(r1 + idx));
      acc[2] = vpadalq_u16(acc[2], vld1q_u16(r2 + idx));
      acc[3] = vpadalq_u16(acc[3], vld1q_u16(r3 + idx));
    }
    const uint32x4_t sum4 = vshlq_u32(horizontal_add_4d_u32x4(acc), norm);
    vst1_s16(vbuf + ht, vreinterpret_s16_u16(vmovn_u32(sum4)));
    ref += 4 * ref_stride;
  }
}

int avm_vector_var_neon(const int16_t *ref, const int16_t *src, const int bwl) {
  assert(bwl >= 2 && bwl <= 6);
  int16x8_t v_mean16 = vdupq_n_s16(0);
  int32x4_t v_sse = vdupq_n_s32(0);

  const int width = 4 << bwl;
  for (int i = 0; i < width; i += 8) {
    const int16x8_t v_ref = vld1q_s16(&ref[i]);
    const int16x8_t v_src = vld1q_s16(&src[i]);
    const int16x8_t diff = vsubq_s16(v_ref, v_src);
    // diff: dynamic range [-511, 511], max 32 iterations -> [-16352, 16352].
    v_mean16 = vaddq_s16(v_mean16, diff);
    const int16x4_t v_low = vget_low_s16(diff);
    v_sse = vmlal_s16(v_sse, v_low, v_low);
#if defined(__aarch64__)
    v_sse = vmlal_high_s16(v_sse, diff, diff);
#else
    const int16x4_t v_high = vget_high_s16(diff);
    v_sse = vmlal_s16(v_sse, v_high, v_high);
#endif
  }
  const int mean = horizontal_add_s16x8(v_mean16);
  const uint32_t sse = (uint32_t)horizontal_add_s32x4(v_sse);
  // (mean * mean): dynamic range up to 34 bits for bwl=6 - store in uint64_t
  const uint64_t meansq = (uint64_t)abs(mean) * (uint64_t)abs(mean);
  const int var = sse - (uint32_t)(meansq >> (bwl + 2));
  return var;
}
