/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <immintrin.h>

#include "config/av2_rtcd.h"

#include "avm/avm_integer.h"
#include "avm_dsp/avm_dsp_common.h"

// Round, quant, dequant and the TCQ zero threshold; lane 0 holds the DC values
// until update_qp().
static INLINE void set_dq10(__m256i *qp) {
  qp[3] = _mm256_add_epi32(_mm256_slli_epi32(qp[2], 3),
                           _mm256_add_epi32(qp[2], qp[2]));
}

static INLINE void update_qp(__m256i *qp) {
  qp[0] = _mm256_permute2x128_si256(qp[0], qp[0], 0x11);
  qp[1] = _mm256_permute2x128_si256(qp[1], qp[1], 0x11);
  qp[2] = _mm256_permute2x128_si256(qp[2], qp[2], 0x11);
  set_dq10(qp);
}

static INLINE void init_qp(const int32_t *round_ptr, const int32_t *quant_ptr,
                           const int32_t *dequant_ptr, int log_scale,
                           __m256i *qp) {
  qp[0] = _mm256_loadu_si256((const __m256i *)round_ptr);
  qp[1] = _mm256_loadu_si256((const __m256i *)quant_ptr);
  qp[2] = _mm256_loadu_si256((const __m256i *)dequant_ptr);

  if (log_scale > 0) {
    const __m256i rnd = _mm256_set1_epi32((int16_t)(1 << (log_scale - 1)));
    qp[0] = _mm256_add_epi32(qp[0], rnd);
    qp[0] = _mm256_srai_epi32(qp[0], log_scale);
  }
  set_dq10(qp);
}

// Get the max eob from the lower 128 bits.
static AVM_FORCE_INLINE uint16_t get_max_eob(__m256i eob) {
  __m256i eob_s;
  eob_s = _mm256_shuffle_epi32(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 1);
  eob = _mm256_max_epi16(eob, eob_s);
  return (uint16_t)_mm256_extract_epi16(eob, 0);
}

static AVM_FORCE_INLINE __m256i get_max_lane_eob(const int16_t *iscan_ptr,
                                                 __m256i eobmax,
                                                 __m256i nz_mask) {
  const __m256i packed_nz_mask = _mm256_packs_epi32(nz_mask, nz_mask);
  const __m256i packed_nz_mask_perm =
      _mm256_permute4x64_epi64(packed_nz_mask, 0xD8);
  const __m256i iscan =
      _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)iscan_ptr));
  const __m256i iscan_plus1 = _mm256_sub_epi16(iscan, packed_nz_mask_perm);
  const __m256i nz_iscan = _mm256_and_si256(iscan_plus1, packed_nz_mask_perm);
  return _mm256_max_epi16(eobmax, nz_iscan);
}

// Mask of the coefficients that quantize to zero.
static AVM_FORCE_INLINE __m256i zero_mask(const __m256i *qp, __m256i abs_coeff,
                                          int log_scale,
                                          int use_tcq_deadzone_boost) {
  const __m256i abs_s =
      _mm256_slli_epi32(abs_coeff, 1 + log_scale + QUANT_TABLE_BITS);
  if (use_tcq_deadzone_boost == 1) {
    const __m256i abs_s9 = _mm256_add_epi32(_mm256_slli_epi32(abs_s, 3), abs_s);
    return _mm256_cmpgt_epi32(qp[3], abs_s9);
  }
  return _mm256_cmpgt_epi32(qp[2], abs_s);
}

// Quantizes 8 coefficients, of which the ones in mask quantize to zero.
static AVM_FORCE_INLINE void quantize(const __m256i *qp, __m256i coeff,
                                      __m256i abs_coeff, __m256i mask,
                                      const int16_t *iscan_ptr, int log_scale,
                                      tran_low_t *qcoeff, tran_low_t *dqcoeff,
                                      __m256i *eob) {
  // If all the transformed coefficient values are less than the dequantized
  // value then reset both qcoeff and dqcoeff and skip further computations of
  // quantization and dequantization.
  if (LIKELY(_mm256_testc_si256(mask, _mm256_set1_epi32(-1)))) {
    const __m256i zero = _mm256_setzero_si256();
    _mm256_storeu_si256((__m256i *)qcoeff, zero);
    _mm256_storeu_si256((__m256i *)dqcoeff, zero);
    return;
  }
  const __m256i round = _mm256_set1_epi32((1 << QUANT_TABLE_BITS) >> 1);
  __m256i q = _mm256_add_epi32(abs_coeff, qp[0]);
  __m256i q_lo = _mm256_mul_epi32(q, qp[1]);
  __m256i q_hi = _mm256_srli_epi64(q, 32);
  const __m256i qp_hi = _mm256_srli_epi64(qp[1], 32);
  q_hi = _mm256_mul_epi32(q_hi, qp_hi);
  q_lo = _mm256_srli_epi64(q_lo, 16 - log_scale + QUANT_FP_BITS);
  q_hi = _mm256_slli_epi64(q_hi, 32 - (16 - log_scale + QUANT_FP_BITS));
  q = _mm256_blend_epi32(q_lo, q_hi, 0xAA);
  q = _mm256_andnot_si256(mask, q);

  __m256i dq = _mm256_mullo_epi32(q, qp[2]);
  dq = _mm256_add_epi64(dq, round);
  dq = _mm256_srai_epi32(dq, log_scale + QUANT_TABLE_BITS);
  const __m256i nz_mask = _mm256_cmpgt_epi32(q, _mm256_setzero_si256());
  q = _mm256_sign_epi32(q, coeff);
  dq = _mm256_sign_epi32(dq, coeff);

  _mm256_storeu_si256((__m256i *)qcoeff, q);
  _mm256_storeu_si256((__m256i *)dqcoeff, dq);

  *eob = get_max_lane_eob(iscan_ptr, *eob, nz_mask);
}

static AVM_FORCE_INLINE void quantize_8(const __m256i *qp,
                                        const tran_low_t *coeff_ptr,
                                        const int16_t *iscan_ptr, int log_scale,
                                        tran_low_t *qcoeff, tran_low_t *dqcoeff,
                                        __m256i *eob,
                                        int use_tcq_deadzone_boost) {
  const __m256i coeff = _mm256_loadu_si256((const __m256i *)coeff_ptr);
  const __m256i abs_coeff = _mm256_abs_epi32(coeff);
  quantize(qp, coeff, abs_coeff,
           zero_mask(qp, abs_coeff, log_scale, use_tcq_deadzone_boost),
           iscan_ptr, log_scale, qcoeff, dqcoeff, eob);
}

static AVM_FORCE_INLINE void quantize_fp(
    const int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr,
    intptr_t n_coeffs, const int32_t *round_ptr, const int32_t *quant_ptr,
    tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr,
    uint16_t *eob_ptr, const int16_t *iscan, int log_scale) {
  __m256i qp[4];
  init_qp(round_ptr, quant_ptr, dequant_ptr, log_scale, qp);

  __m256i eob = _mm256_setzero_si256();
  // The first 8 coefficients include the DC one.
  quantize_8(qp, coeff_ptr, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob,
             use_tcq_deadzone_boost);
  coeff_ptr += 8;
  qcoeff_ptr += 8;
  dqcoeff_ptr += 8;
  iscan += 8;
  n_coeffs -= 8;
  update_qp(qp);

  // Most AC coefficients quantize to zero: test 16 of them at a time.
  const __m256i all_ones = _mm256_set1_epi32(-1);
  for (; n_coeffs >= 16; n_coeffs -= 16) {
    const __m256i coeff0 = _mm256_loadu_si256((const __m256i *)coeff_ptr);
    const __m256i coeff1 = _mm256_loadu_si256((const __m256i *)(coeff_ptr + 8));
    const __m256i abs0 = _mm256_abs_epi32(coeff0);
    const __m256i abs1 = _mm256_abs_epi32(coeff1);
    const __m256i mask0 =
        zero_mask(qp, abs0, log_scale, use_tcq_deadzone_boost);
    const __m256i mask1 =
        zero_mask(qp, abs1, log_scale, use_tcq_deadzone_boost);
    if (_mm256_testc_si256(_mm256_and_si256(mask0, mask1), all_ones)) {
      const __m256i zero = _mm256_setzero_si256();
      _mm256_storeu_si256((__m256i *)qcoeff_ptr, zero);
      _mm256_storeu_si256((__m256i *)(qcoeff_ptr + 8), zero);
      _mm256_storeu_si256((__m256i *)dqcoeff_ptr, zero);
      _mm256_storeu_si256((__m256i *)(dqcoeff_ptr + 8), zero);
    } else {
      quantize(qp, coeff0, abs0, mask0, iscan, log_scale, qcoeff_ptr,
               dqcoeff_ptr, &eob);
      quantize(qp, coeff1, abs1, mask1, iscan + 8, log_scale, qcoeff_ptr + 8,
               dqcoeff_ptr + 8, &eob);
    }
    coeff_ptr += 16;
    qcoeff_ptr += 16;
    dqcoeff_ptr += 16;
    iscan += 16;
  }
  if (n_coeffs > 0) {
    quantize_8(qp, coeff_ptr, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob,
               use_tcq_deadzone_boost);
  }
  *eob_ptr = get_max_eob(eob);
}

void av2_highbd_quantize_fp_avx2(
    const int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr,
    intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr,
    const int32_t *quant_ptr, const int32_t *quant_shift_ptr,
    tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr,
    uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan,
    int log_scale) {
  (void)scan;
  (void)zbin_ptr;
  (void)quant_shift_ptr;
  // n_coeffs is a multiple of 8.
  if (use_tcq_deadzone_boost == 1) {
    quantize_fp(1, coeff_ptr, n_coeffs, round_ptr, quant_ptr, qcoeff_ptr,
                dqcoeff_ptr, dequant_ptr, eob_ptr, iscan, log_scale);
  } else {
    quantize_fp(0, coeff_ptr, n_coeffs, round_ptr, quant_ptr, qcoeff_ptr,
                dqcoeff_ptr, dequant_ptr, eob_ptr, iscan, log_scale);
  }
}
