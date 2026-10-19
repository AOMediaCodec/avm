/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>
#include <assert.h>

#include "config/av2_rtcd.h"

#include "avm/avm_integer.h"
#include "avm_dsp/avm_dsp_common.h"

static AVM_FORCE_INLINE int16_t horizontal_max_s16(int16x8_t v) {
#if AVM_ARCH_AARCH64
  return vmaxvq_s16(v);
#else
  const int16x4_t v4 = vmax_s16(vget_low_s16(v), vget_high_s16(v));
  const int16x4_t v2 = vpmax_s16(v4, v4);
  const int16x4_t v1 = vpmax_s16(v2, v2);
  return vget_lane_s16(v1, 0);
#endif
}

// Hides v from the optimizer so the ls0 dequant stays one mla.
static AVM_FORCE_INLINE int32x4_t opaque_s32(int32x4_t v) {
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
  __asm__("" : "+w"(v));
#endif
  return v;
}

// ceil(dq / (9 * 2^s)) == ceil(ceil(dq / 2^s) / 9): a 32-bit divide by 9.
static AVM_FORCE_INLINE int32_t zbin_threshold(int32_t dequant, int log_scale,
                                               int use_tcq_deadzone_boost) {
  const int s = 1 + log_scale + QUANT_TABLE_BITS;
  int64_t dq = dequant;
  if (use_tcq_deadzone_boost == 1) dq *= 10;
  int32_t t = (int32_t)((dq + ((int64_t)1 << s) - 1) >> s);
  if (t < 0) t = 0;
  if (use_tcq_deadzone_boost == 1) t = (int32_t)(((uint32_t)t + 8) / 9);
  return t;
}

static AVM_FORCE_INLINE void quantize_8_fp_neon(
    const tran_low_t *coeff_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *iscan, const int32x4_t round_lo,
    const int32x4_t round_hi, const int32x4_t quant_lo,
    const int32x4_t quant_hi, const int32x4_t dequant_lo,
    const int32x4_t dequant_hi, const int32x4_t thresh_lo,
    const int32x4_t thresh_hi, const int32x4_t dq_bias,
    const int32x4_t dq_shift, const int zbin_on, const int ls0,
    const int zero_skip, int16x8_t *eob) {
  const int32x4_t coeff_lo = vld1q_s32(coeff_ptr);
  const int32x4_t coeff_hi = vld1q_s32(coeff_ptr + 4);
  const int32x4_t abs_lo = vabsq_s32(coeff_lo);
  const int32x4_t abs_hi = vabsq_s32(coeff_hi);
  const uint32x4_t pass_lo = vcgeq_s32(abs_lo, thresh_lo);
  const uint32x4_t pass_hi = vcgeq_s32(abs_hi, thresh_hi);
  if (zero_skip) {
    // Lanes are all-ones or zero, so bits 16..47 of each u64 cover both lanes.
    const uint32x2_t any =
        vshrn_n_u64(vreinterpretq_u64_u32(vorrq_u32(pass_lo, pass_hi)), 16);
    if (vget_lane_u64(vreinterpret_u64_u32(any), 0) == 0) {
      const int32x4_t zero = vdupq_n_s32(0);
      vst1q_s32(qcoeff_ptr, zero);
      vst1q_s32(qcoeff_ptr + 4, zero);
      vst1q_s32(dqcoeff_ptr, zero);
      vst1q_s32(dqcoeff_ptr + 4, zero);
      return;
    }
  }
  int32x4_t q_lo = vqdmulhq_s32(vaddq_s32(abs_lo, round_lo), quant_lo);
  int32x4_t q_hi = vqdmulhq_s32(vaddq_s32(abs_hi, round_hi), quant_hi);
  if (zbin_on) {
    q_lo = vandq_s32(q_lo, vreinterpretq_s32_u32(pass_lo));
    q_hi = vandq_s32(q_hi, vreinterpretq_s32_u32(pass_hi));
  }
  const int32x4_t zero_s32 = vdupq_n_s32(0);
  const uint16x8_t nz_eob_mask =
      vcombine_u16(vmovn_u32(vceqq_s32(q_lo, zero_s32)),
                   vmovn_u32(vceqq_s32(q_hi, zero_s32)));
  const int32x4_t sign_lo = vshrq_n_s32(coeff_lo, 31);
  q_lo = veorq_s32(vaddq_s32(q_lo, sign_lo), sign_lo);
  const int32x4_t sign_hi = vshrq_n_s32(coeff_hi, 31);
  q_hi = veorq_s32(vaddq_s32(q_hi, sign_hi), sign_hi);
  int32x4_t dq_lo, dq_hi;
  if (ls0) {
    dq_lo = vrshrq_n_s32(vmlaq_s32(opaque_s32(sign_lo), q_lo, dequant_lo),
                         QUANT_TABLE_BITS);
    dq_hi = vrshrq_n_s32(vmlaq_s32(opaque_s32(sign_hi), q_hi, dequant_hi),
                         QUANT_TABLE_BITS);
  } else {
    dq_lo = vrshlq_s32(vmlaq_s32(veorq_s32(dq_bias, sign_lo), q_lo, dequant_lo),
                       dq_shift);
    dq_hi = vrshlq_s32(vmlaq_s32(veorq_s32(dq_bias, sign_hi), q_hi, dequant_hi),
                       dq_shift);
  }
  vst1q_s32(qcoeff_ptr, q_lo);
  vst1q_s32(qcoeff_ptr + 4, q_hi);
  vst1q_s32(dqcoeff_ptr, dq_lo);
  vst1q_s32(dqcoeff_ptr + 4, dq_hi);
  const int16x8_t iscan_val = vld1q_s16(iscan);
  *eob =
      vmaxq_s16(*eob, vorrq_s16(iscan_val, vreinterpretq_s16_u16(nz_eob_mask)));
}

static AVM_FORCE_INLINE void quantize_ac_loop(
    const tran_low_t *coeff_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int16_t *iscan, intptr_t n_coeffs,
    const int32x4_t round_v, const int32x4_t quant_v, const int32x4_t dequant_v,
    const int32x4_t thresh_v, const int32x4_t dq_bias, const int32x4_t dq_shift,
    const int zbin_on, const int ls0, const int zero_skip, int16x8_t *eob) {
  while (n_coeffs > 0) {
    quantize_8_fp_neon(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, round_v,
                       round_v, quant_v, quant_v, dequant_v, dequant_v,
                       thresh_v, thresh_v, dq_bias, dq_shift, zbin_on, ls0,
                       zero_skip, eob);
    coeff_ptr += 8;
    qcoeff_ptr += 8;
    dqcoeff_ptr += 8;
    iscan += 8;
    n_coeffs -= 8;
  }
}

void av2_highbd_quantize_fp_neon(
    const int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr,
    intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr,
    const int32_t *quant_ptr, const int32_t *quant_shift_ptr,
    tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr,
    uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan,
    int log_scale) {
  (void)scan;
  (void)zbin_ptr;
  (void)quant_shift_ptr;

  const int32x4_t dq_shift = vdupq_n_s32(-(log_scale + QUANT_TABLE_BITS));
  // vrshl(q_signed * dequant + (dq_bias ^ sign), -m) matches C's
  // sign * ((|q| * dequant + 4) >> m) for both signs.
  const int32x4_t dq_bias =
      vdupq_n_s32(4 - ((1 << (log_scale + QUANT_TABLE_BITS)) >> 1));
  const int quant_pre_shift = 11 + log_scale;
  // quant << quant_pre_shift must fit in int32.
  assert(dequant_ptr[0] > (8 << log_scale) &&
         dequant_ptr[1] > (8 << log_scale));

  const int32_t round_dc = ROUND_POWER_OF_TWO(round_ptr[0], log_scale);
  const int32_t round_ac = ROUND_POWER_OF_TWO(round_ptr[1], log_scale);
  const int32_t quant_dc = quant_ptr[0] << quant_pre_shift;
  const int32_t quant_ac = quant_ptr[1] << quant_pre_shift;
  const int32_t thresh_dc =
      zbin_threshold(dequant_ptr[0], log_scale, use_tcq_deadzone_boost);
  const int32_t thresh_ac =
      zbin_threshold(dequant_ptr[1], log_scale, use_tcq_deadzone_boost);

  const int32x4_t round_v = vdupq_n_s32(round_ac);
  const int32x4_t quant_v = vdupq_n_s32(quant_ac);
  const int32x4_t dequant_v = vdupq_n_s32(dequant_ptr[1]);
  const int32x4_t thresh_v = vdupq_n_s32(thresh_ac);

  int16x8_t eob = vdupq_n_s16(-1);

  quantize_8_fp_neon(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan,
                     vsetq_lane_s32(round_dc, round_v, 0), round_v,
                     vsetq_lane_s32(quant_dc, quant_v, 0), quant_v,
                     vsetq_lane_s32(dequant_ptr[0], dequant_v, 0), dequant_v,
                     vsetq_lane_s32(thresh_dc, thresh_v, 0), thresh_v, dq_bias,
                     dq_shift, /*zbin_on=*/1, /*ls0=*/0, /*zero_skip=*/0, &eob);
  coeff_ptr += 8;
  qcoeff_ptr += 8;
  dqcoeff_ptr += 8;
  iscan += 8;
  n_coeffs -= 8;

  if (log_scale == 0 && n_coeffs < 128 - 8) {
    quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                     round_v, quant_v, dequant_v, thresh_v, dq_bias, dq_shift,
                     1, 1, 0, &eob);
  } else {
    // q is non-decreasing in abs: if 0 at abs = thresh_ac - 1, no-op mask.
    const int64_t x_max = (int64_t)thresh_ac - 1 + round_ac;
    const int skip_zbin_mask =
        round_ac >= 0 && quant_ac >= 0 &&
        (thresh_ac <= 0 ||
         (x_max <= INT32_MAX && ((x_max * quant_ac) >> 31) == 0));

    if (log_scale == 0) {
      if (skip_zbin_mask) {
        quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                         round_v, quant_v, dequant_v, thresh_v, dq_bias,
                         dq_shift, 0, 1, 0, &eob);
      } else if (n_coeffs >= 256 - 8) {
        quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                         round_v, quant_v, dequant_v, thresh_v, dq_bias,
                         dq_shift, 1, 1, 1, &eob);
      } else {
        quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                         round_v, quant_v, dequant_v, thresh_v, dq_bias,
                         dq_shift, 1, 1, 0, &eob);
      }
    } else {
      if (skip_zbin_mask) {
        quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                         round_v, quant_v, dequant_v, thresh_v, dq_bias,
                         dq_shift, 0, 0, 1, &eob);
      } else {
        quantize_ac_loop(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, iscan, n_coeffs,
                         round_v, quant_v, dequant_v, thresh_v, dq_bias,
                         dq_shift, 1, 0, 1, &eob);
      }
    }
  }

  const int16_t max_eob = horizontal_max_s16(eob);
  *eob_ptr = (uint16_t)(max_eob + 1);
}
