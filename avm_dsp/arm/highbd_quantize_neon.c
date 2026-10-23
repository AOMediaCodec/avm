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

#include "config/avm_dsp_rtcd.h"

#include "avm/avm_integer.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/bitops.h"

// C's abs_q = ((((a * quant) >> 16) + a) * quant_shift) >> (16 - ls). With
// Q = 65536 + quant and quant_shift = 2^k (invert_quant() makes it a power of
// two) this is (a * Q) >> (16 + s), s = 16 - ls - k, as floored power-of-two
// divisions compose, so abs_q = vqdmulhq_s32(a, Q << (15 - s)).
static INLINE int fused_quant_mult(int32_t quant, int32_t quant_shift,
                                   int log_scale, int32_t *mult) {
  assert(quant_shift > 0 && (quant_shift & (quant_shift - 1)) == 0);
  const int s = 16 - log_scale - get_msb((unsigned int)quant_shift);
  const int64_t q = 65536 + (int64_t)quant;
  assert(s >= 0 && s <= 16 && q > 0);
  assert(log_scale == 0 || s <= 15);
  // s == 16 (12-bit, log_scale 0, dequant >= 2^19): the caller falls back to C.
  if (s > 15) return 0;
  const int64_t m = q << (15 - s);
  assert(m <= INT32_MAX);
  *mult = (int32_t)m;
  return 1;
}

// The empty asm stops clang turning SABA into abs + add and the signed-dequant
// mla into mul + ssra. armv7 gets the plain code, which gives the same result.
static AVM_FORCE_INLINE int32x4_t opaque_s32(int32x4_t v) {
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
  __asm__("" : "+w"(v));
#endif
  return v;
}

static AVM_FORCE_INLINE int32x4_t abs_add_s32(const int32x4_t round,
                                              const int32x4_t coeff) {
  return vabaq_s32(round, coeff, opaque_s32(vdupq_n_s32(0)));
}

static AVM_FORCE_INLINE int32x4_t quantize_q(
    const int32x4_t *qp, const int32x4_t coeff, const int32x4_t abs_rnd,
    const uint32x4_t zbin_mask, int32x4_t *coeff_sign, uint32x4_t *nz_mask) {
  const int32x4_t tmp_rnd =
      vandq_s32(abs_rnd, vreinterpretq_s32_u32(zbin_mask));
  const int32x4_t abs_q = vqdmulhq_s32(tmp_rnd, qp[2]);
  *nz_mask = vcgtq_s32(abs_q, vdupq_n_s32(0));
  *coeff_sign = vreinterpretq_s32_u32(vcltq_s32(coeff, vdupq_n_s32(0)));
  return vsubq_s32(veorq_s32(abs_q, *coeff_sign), *coeff_sign);
}

static AVM_FORCE_INLINE uint16x4_t quantize_b_neon_core(
    const int32x4_t *qp, const int32x4_t coeff, const int32x4_t abs_rnd,
    const uint32x4_t zbin_mask, const int log_scale, tran_low_t *qcoeff,
    tran_low_t *dqcoeff) {
  int32x4_t coeff_sign;
  uint32x4_t nz_mask;
  const int32x4_t q =
      quantize_q(qp, coeff, abs_rnd, zbin_mask, &coeff_sign, &nz_mask);

  // With k = QUANT_TABLE_BITS + log_scale, the bias qp[4] (its complement for
  // negative q) makes vrshr by k give C's sign * ((|q| * dequant + 4) >> k).
  const int32x4_t dq_in = vmlaq_s32(veorq_s32(qp[4], coeff_sign), q, qp[3]);
  const int32x4_t dq = log_scale == 2
                           ? vrshrq_n_s32(dq_in, QUANT_TABLE_BITS + 2)
                           : vrshrq_n_s32(dq_in, QUANT_TABLE_BITS + 1);

  vst1q_s32(qcoeff, q);
  vst1q_s32(dqcoeff, dq);

  return vshrn_n_u32(nz_mask, 16);
}

static AVM_FORCE_INLINE uint16x4_t quantize_b_neon(const int32x4_t *qp,
                                                   const tran_low_t *coeff_ptr,
                                                   const int log_scale,
                                                   tran_low_t *qcoeff,
                                                   tran_low_t *dqcoeff) {
  const int32x4_t coeff = vld1q_s32(coeff_ptr);
  const int32x4_t abs_rnd = abs_add_s32(qp[1], coeff);
  const uint32x4_t zbin_mask = vcgtq_s32(abs_rnd, qp[0]);
  return quantize_b_neon_core(qp, coeff, abs_rnd, zbin_mask, log_scale, qcoeff,
                              dqcoeff);
}

static AVM_FORCE_INLINE uint16x4_t
quantize_b_neon_ls0(const int32x4_t *qp, const tran_low_t *coeff_ptr,
                    tran_low_t *qcoeff, tran_low_t *dqcoeff) {
  const int32x4_t coeff = vld1q_s32(coeff_ptr);
  const int32x4_t abs_rnd = abs_add_s32(qp[1], coeff);
  const uint32x4_t zbin_mask = vcgtq_s32(abs_rnd, qp[0]);
  int32x4_t coeff_sign;
  uint32x4_t nz_mask;
  const int32x4_t q =
      quantize_q(qp, coeff, abs_rnd, zbin_mask, &coeff_sign, &nz_mask);

  // Adding the sign (-1 for negative q) makes vrshr round negatives like C:
  // (q * dequant - 1 + 4) >> 3 = -((|q| * dequant + 4) >> 3).
  const int32x4_t dq = vrshrq_n_s32(vmlaq_s32(opaque_s32(coeff_sign), q, qp[3]),
                                    QUANT_TABLE_BITS);

  vst1q_s32(qcoeff, q);
  vst1q_s32(dqcoeff, dq);

  return vshrn_n_u32(nz_mask, 16);
}

static AVM_FORCE_INLINE int16x8_t max_nz_iscan(const int16x8_t eob,
                                               const uint16x8_t nz_mask,
                                               const int16x8_t iscan_val) {
  return vmaxq_s16(eob, vbslq_s16(nz_mask, iscan_val, vdupq_n_s16(-1)));
}

static AVM_FORCE_INLINE void update_eob_8(const int16_t *iscan_ptr,
                                          uint16x4_t nz_lo, uint16x4_t nz_hi,
                                          int16x8_t *eob) {
  const uint16x8_t nz_mask = vcombine_u16(nz_lo, nz_hi);
  *eob = max_nz_iscan(*eob, nz_mask, vld1q_s16(iscan_ptr));
}

static AVM_FORCE_INLINE int16_t horizontal_max_s16x8(const int16x8_t v) {
  int16x4_t m = vpmax_s16(vget_low_s16(v), vget_high_s16(v));
  m = vpmax_s16(m, m);
  m = vpmax_s16(m, m);
  return vget_lane_s16(m, 0);
}

static AVM_FORCE_INLINE int16x8_t quantize_b_large_neon(
    int32x4_t *qp, const tran_low_t *coeff_ptr, intptr_t n_coeffs,
    const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t mult_ac,
    tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr,
    const int16_t *iscan, const int log_scale) {
  const int step = 4;
  int16x8_t eob = vdupq_n_s16(-1);

  uint16x4_t nz_dc =
      quantize_b_neon(qp, coeff_ptr, log_scale, qcoeff_ptr, dqcoeff_ptr);

  coeff_ptr += step;
  qcoeff_ptr += step;
  dqcoeff_ptr += step;
  iscan += step;
  n_coeffs -= step;

  qp[0] = vdupq_n_s32(ROUND_POWER_OF_TWO(zbin_ptr[1], log_scale) +
                      ROUND_POWER_OF_TWO(round_ptr[1], log_scale) - 1);
  qp[1] = vdupq_n_s32(ROUND_POWER_OF_TWO(round_ptr[1], log_scale));
  qp[2] = vdupq_n_s32(mult_ac);
  qp[3] = vdupq_n_s32(dequant_ptr[1]);

  uint16x4_t nz_ac0 =
      quantize_b_neon(qp, coeff_ptr, log_scale, qcoeff_ptr, dqcoeff_ptr);
  update_eob_8(iscan - step, nz_dc, nz_ac0, &eob);

  coeff_ptr += step;
  qcoeff_ptr += step;
  dqcoeff_ptr += step;
  iscan += step;
  n_coeffs -= step;

  while (n_coeffs > step) {
    const int32x4_t coeff_lo = vld1q_s32(coeff_ptr);
    const int32x4_t coeff_hi = vld1q_s32(coeff_ptr + step);
    const int32x4_t abs_rnd_lo = abs_add_s32(qp[1], coeff_lo);
    const int32x4_t abs_rnd_hi = abs_add_s32(qp[1], coeff_hi);
    const uint32x4_t zbin_lo = vcgtq_s32(abs_rnd_lo, qp[0]);
    const uint32x4_t zbin_hi = vcgtq_s32(abs_rnd_hi, qp[0]);
    const uint16x4_t any_zbin = vshrn_n_u32(vorrq_u32(zbin_lo, zbin_hi), 16);
    if (vget_lane_u64(vreinterpret_u64_u16(any_zbin), 0) == 0) {
      const int32x4_t zero = vdupq_n_s32(0);
      vst1q_s32(qcoeff_ptr, zero);
      vst1q_s32(qcoeff_ptr + step, zero);
      vst1q_s32(dqcoeff_ptr, zero);
      vst1q_s32(dqcoeff_ptr + step, zero);
    } else {
      const int16x8_t iscan_pre = vld1q_s16(iscan);
      const uint16x4_t nz_lo =
          quantize_b_neon_core(qp, coeff_lo, abs_rnd_lo, zbin_lo, log_scale,
                               qcoeff_ptr, dqcoeff_ptr);
      const uint16x4_t nz_hi =
          quantize_b_neon_core(qp, coeff_hi, abs_rnd_hi, zbin_hi, log_scale,
                               qcoeff_ptr + step, dqcoeff_ptr + step);
      eob = max_nz_iscan(eob, vcombine_u16(nz_lo, nz_hi), iscan_pre);
    }

    coeff_ptr += 2 * step;
    qcoeff_ptr += 2 * step;
    dqcoeff_ptr += 2 * step;
    iscan += 2 * step;
    n_coeffs -= 2 * step;
  }
  return eob;
}

void avm_highbd_quantize_b_neon(
    int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr, intptr_t n_coeffs,
    const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr,
    const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, const int log_scale) {
  assert(log_scale >= 0 && log_scale <= 2);
  assert(n_coeffs >= 16 && (n_coeffs & 7) == 0);

  int32_t mult_dc, mult_ac;
  if (!fused_quant_mult(quant_ptr[0], quant_shift_ptr[0], log_scale,
                        &mult_dc) ||
      !fused_quant_mult(quant_ptr[1], quant_shift_ptr[1], log_scale,
                        &mult_ac)) {
    avm_highbd_quantize_b_c(use_tcq_deadzone_boost, coeff_ptr, n_coeffs,
                            zbin_ptr, round_ptr, quant_ptr, quant_shift_ptr,
                            qcoeff_ptr, dqcoeff_ptr, dequant_ptr, eob_ptr, scan,
                            iscan, log_scale);
    return;
  }

  const int step = 4;
  int32x4_t qp[5];

  int32_t zbin_arr[4], round_arr[4], mult_arr[4], dequant_arr[4];
  for (int i = 0; i < 4; i++) {
    const int idx = (i == 0) ? 0 : 1;
    round_arr[i] = ROUND_POWER_OF_TWO(round_ptr[idx], log_scale);
    zbin_arr[i] =
        ROUND_POWER_OF_TWO(zbin_ptr[idx], log_scale) + round_arr[i] - 1;
    mult_arr[i] = (i == 0) ? mult_dc : mult_ac;
    dequant_arr[i] = dequant_ptr[idx];
  }

  qp[0] = vld1q_s32(zbin_arr);
  qp[1] = vld1q_s32(round_arr);
  qp[2] = vld1q_s32(mult_arr);
  qp[3] = vld1q_s32(dequant_arr);

  int16x8_t eob = vdupq_n_s16(-1);

  if (log_scale == 0) {
    uint16x4_t nz_dc =
        quantize_b_neon_ls0(qp, coeff_ptr, qcoeff_ptr, dqcoeff_ptr);

    coeff_ptr += step;
    qcoeff_ptr += step;
    dqcoeff_ptr += step;
    iscan += step;
    n_coeffs -= step;

    qp[0] = vdupq_n_s32(zbin_arr[1]);
    qp[1] = vdupq_n_s32(round_arr[1]);
    qp[2] = vdupq_n_s32(mult_ac);
    qp[3] = vdupq_n_s32(dequant_ptr[1]);

    uint16x4_t nz_ac0 =
        quantize_b_neon_ls0(qp, coeff_ptr, qcoeff_ptr, dqcoeff_ptr);
    update_eob_8(iscan - step, nz_dc, nz_ac0, &eob);

    coeff_ptr += step;
    qcoeff_ptr += step;
    dqcoeff_ptr += step;
    iscan += step;
    n_coeffs -= step;

    while (n_coeffs > step) {
      const uint16x4_t nz_lo =
          quantize_b_neon_ls0(qp, coeff_ptr, qcoeff_ptr, dqcoeff_ptr);
      const uint16x4_t nz_hi = quantize_b_neon_ls0(
          qp, coeff_ptr + step, qcoeff_ptr + step, dqcoeff_ptr + step);
      const int16x8_t iscan_val = vld1q_s16(iscan);
      const int16x4_t eob_lo =
          vorn_s16(vget_low_s16(iscan_val), vreinterpret_s16_u16(nz_lo));
      const int16x4_t eob_hi =
          vorn_s16(vget_high_s16(iscan_val), vreinterpret_s16_u16(nz_hi));
      eob = vmaxq_s16(eob, vcombine_s16(eob_lo, eob_hi));

      coeff_ptr += 2 * step;
      qcoeff_ptr += 2 * step;
      dqcoeff_ptr += 2 * step;
      iscan += 2 * step;
      n_coeffs -= 2 * step;
    }
  } else {
    qp[4] = vdupq_n_s32(4 - (1 << (QUANT_TABLE_BITS - 1 + log_scale)));
    if (log_scale == 1) {
      eob = quantize_b_large_neon(qp, coeff_ptr, n_coeffs, zbin_ptr, round_ptr,
                                  mult_ac, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                                  iscan, 1);
    } else {
      eob = quantize_b_large_neon(qp, coeff_ptr, n_coeffs, zbin_ptr, round_ptr,
                                  mult_ac, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                                  iscan, 2);
    }
  }

  *eob_ptr = (uint16_t)(horizontal_max_s16x8(eob) + 1);
}
