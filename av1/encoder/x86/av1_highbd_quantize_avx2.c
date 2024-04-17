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

#include "config/av1_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/aom_dsp_common.h"

static INLINE void update_qp(__m256i *qp) {
  qp[0] = _mm256_permute2x128_si256(qp[0], qp[0], 0x11);
  qp[1] = _mm256_permute2x128_si256(qp[1], qp[1], 0x11);
  qp[2] = _mm256_permute2x128_si256(qp[2], qp[2], 0x11);
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
}

// Get the max eob from the lower 128 bits.
static AOM_FORCE_INLINE uint16_t get_max_eob(__m256i eob) {
  __m256i eob_s;
  eob_s = _mm256_shuffle_epi32(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 1);
  eob = _mm256_max_epi16(eob, eob_s);
  return (uint16_t)_mm256_extract_epi16(eob, 0);
}

static AOM_FORCE_INLINE __m256i get_max_lane_eob(const int16_t *iscan_ptr,
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

static INLINE void quantize(const __m256i *qp, const tran_low_t *coeff_ptr,
                            const int16_t *iscan_ptr, int log_scale,
                            tran_low_t *qcoeff, tran_low_t *dqcoeff,
                            __m256i *eob) {
  const __m256i coeff = _mm256_loadu_si256((const __m256i *)coeff_ptr);
  const __m256i abs_coeff = _mm256_abs_epi32(coeff);

  const __m256i round = _mm256_set1_epi32((1 << QUANT_TABLE_BITS) >> 1);
  __m256i q = _mm256_add_epi32(abs_coeff, qp[0]);
  __m256i q_lo = _mm256_mul_epi32(q, qp[1]);
  __m256i q_hi = _mm256_srli_epi64(q, 32);
  const __m256i qp_hi = _mm256_srli_epi64(qp[1], 32);
  q_hi = _mm256_mul_epi32(q_hi, qp_hi);
  q_lo = _mm256_srli_epi64(q_lo, 16 - log_scale + QUANT_FP_BITS);
  q_hi = _mm256_slli_epi64(q_hi, 32 - (16 - log_scale + QUANT_FP_BITS));
  q = _mm256_blend_epi32(q_lo, q_hi, 0xAA);

  log_scale += QUANT_TABLE_BITS;
  const __m256i abs_s = _mm256_slli_epi32(abs_coeff, 1 + log_scale);
  const __m256i mask = _mm256_cmpgt_epi32(qp[2], abs_s);
  q = _mm256_andnot_si256(mask, q);

  __m256i dq = _mm256_mullo_epi32(q, qp[2]);
  dq = _mm256_add_epi64(dq, round);
  dq = _mm256_srai_epi32(dq, log_scale);
  const __m256i nz_mask = _mm256_cmpgt_epi32(q, _mm256_setzero_si256());
  q = _mm256_sign_epi32(q, coeff);
  dq = _mm256_sign_epi32(dq, coeff);

  _mm256_storeu_si256((__m256i *)qcoeff, q);
  _mm256_storeu_si256((__m256i *)dqcoeff, dq);

  *eob = get_max_lane_eob(iscan_ptr, *eob, nz_mask);
}

void av1_highbd_quantize_fp_avx2(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr,
    const int32_t *round_ptr, const int32_t *quant_ptr,
    const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, int log_scale) {
  (void)scan;
  (void)zbin_ptr;
  (void)quant_shift_ptr;
  const unsigned int step = 8;
  __m256i qp[3];

  init_qp(round_ptr, quant_ptr, dequant_ptr, log_scale, qp);

  __m256i eob = _mm256_setzero_si256();
  quantize(qp, coeff_ptr, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob);

  coeff_ptr += step;
  qcoeff_ptr += step;
  dqcoeff_ptr += step;
  iscan += step;
  n_coeffs -= step;

  update_qp(qp);
  while (n_coeffs > 0) {
    quantize(qp, coeff_ptr, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob);

    coeff_ptr += step;
    qcoeff_ptr += step;
    dqcoeff_ptr += step;
    iscan += step;
    n_coeffs -= step;
  }
  *eob_ptr = get_max_eob(eob);
}
