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
  const int round1 = ROUND_POWER_OF_TWO(round_ptr[1], log_scale);
  const int round0 = ROUND_POWER_OF_TWO(round_ptr[0], log_scale);
  qp[0] = _mm256_set_epi32(round1, round1, round1, round1, round1, round1,
                           round1, round0);

  const int quant1 = quant_ptr[1];
  const int quant0 = quant_ptr[0];
  qp[1] = _mm256_set_epi32(quant1, quant1, quant1, quant1, quant1, quant1,
                           quant1, quant0);

  const int dequant1 = dequant_ptr[1];
  const int dequant0 = dequant_ptr[0];
  qp[2] = _mm256_set_epi32(dequant1, dequant1, dequant1, dequant1, dequant1,
                           dequant1, dequant1, dequant0);
}

static INLINE void quantize(const __m256i *qp, __m256i *c,
                            const int16_t *iscan_ptr, int log_scale,
                            tran_low_t *qcoeff, tran_low_t *dqcoeff,
                            __m256i *eob) {
  const __m256i abs_coeff = _mm256_abs_epi32(*c);
  const __m256i round = _mm256_set1_epi32((1 << QUANT_TABLE_BITS) >> 1);
  __m256i q = _mm256_add_epi32(abs_coeff, qp[0]);
  __m256i q_lo = _mm256_mul_epi32(q, qp[1]);
  __m256i q_hi = _mm256_srli_epi64(q, 32);
  const __m256i qp_hi = _mm256_srli_epi64(qp[1], 32);
  q_hi = _mm256_mul_epi32(q_hi, qp_hi);
  q_lo = _mm256_srli_epi64(q_lo, 16 - log_scale + QUANT_FP_BITS);
  q_hi = _mm256_srli_epi64(q_hi, 16 - log_scale + QUANT_FP_BITS);
  log_scale += QUANT_TABLE_BITS;

  q_hi = _mm256_slli_epi64(q_hi, 32);
  q = _mm256_or_si256(q_lo, q_hi);
  const __m256i abs_s = _mm256_slli_epi32(abs_coeff, 1 + log_scale);
  const __m256i mask = _mm256_cmpgt_epi32(qp[2], abs_s);
  q = _mm256_andnot_si256(mask, q);

  __m256i dq = _mm256_mullo_epi32(q, qp[2]);
  dq = _mm256_add_epi64(dq, round);
  dq = _mm256_srai_epi32(dq, log_scale);
  q = _mm256_sign_epi32(q, *c);
  dq = _mm256_sign_epi32(dq, *c);

  _mm256_storeu_si256((__m256i *)qcoeff, q);
  _mm256_storeu_si256((__m256i *)dqcoeff, dq);

  const __m128i isc = _mm_loadu_si128((const __m128i *)iscan_ptr);
  const __m128i zr = _mm_setzero_si128();
  const __m128i lo = _mm_unpacklo_epi16(isc, zr);
  const __m128i hi = _mm_unpackhi_epi16(isc, zr);
  const __m256i iscan =
      _mm256_insertf128_si256(_mm256_castsi128_si256(lo), hi, 1);

  const __m256i zero = _mm256_setzero_si256();
  const __m256i zc = _mm256_cmpeq_epi32(dq, zero);
  const __m256i nz = _mm256_cmpeq_epi32(zc, zero);
  __m256i cur_eob = _mm256_sub_epi32(iscan, nz);
  cur_eob = _mm256_and_si256(cur_eob, nz);
  *eob = _mm256_max_epi32(cur_eob, *eob);
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
  __m256i qp[3], coeff;

  init_qp(round_ptr, quant_ptr, dequant_ptr, log_scale, qp);
  coeff = _mm256_loadu_si256((const __m256i *)coeff_ptr);

  __m256i eob = _mm256_setzero_si256();
  quantize(qp, &coeff, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob);

  coeff_ptr += step;
  qcoeff_ptr += step;
  dqcoeff_ptr += step;
  iscan += step;
  n_coeffs -= step;

  update_qp(qp);
  while (n_coeffs > 0) {
    coeff = _mm256_loadu_si256((const __m256i *)coeff_ptr);
    quantize(qp, &coeff, iscan, log_scale, qcoeff_ptr, dqcoeff_ptr, &eob);

    coeff_ptr += step;
    qcoeff_ptr += step;
    dqcoeff_ptr += step;
    iscan += step;
    n_coeffs -= step;
  }
  {
    __m256i eob_s;
    eob_s = _mm256_shuffle_epi32(eob, 0xe);
    eob = _mm256_max_epi16(eob, eob_s);
    eob_s = _mm256_shufflelo_epi16(eob, 0xe);
    eob = _mm256_max_epi16(eob, eob_s);
    eob_s = _mm256_shufflelo_epi16(eob, 1);
    eob = _mm256_max_epi16(eob, eob_s);
    const __m128i final_eob = _mm_max_epi16(_mm256_castsi256_si128(eob),
                                            _mm256_extractf128_si256(eob, 1));
    *eob_ptr = _mm_extract_epi16(final_eob, 0);
  }
}
