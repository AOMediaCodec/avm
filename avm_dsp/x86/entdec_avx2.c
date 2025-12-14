/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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

#include "config/avm_dsp_rtcd.h"
#include "avm_dsp/entdec.h"
#include "avm_dsp/entcode.h"

/*Decodes a symbol given an inverse cumulative distribution function (CDF)
   table in Q15.
  icdf: CDF_PROB_TOP minus the CDF, such that symbol s falls in the range
         [s > 0 ? (CDF_PROB_TOP - icdf[s - 1]) : 0, CDF_PROB_TOP - icdf[s]).
        The values must be monotonically non-increasing, and icdf[nsyms - 1]
         must be 0.
  nsyms: The number of symbols in the alphabet.
         This should be at most 16.
  Return: The decoded symbol s.*/
int od_ec_decode_cdf_q15_avx2(od_ec_dec *dec, const uint16_t *icdf, int nsyms) {
  uint16_t scaled_cdf[16];

  __m256i cdf = _mm256_lddqu_si256((__m256i *)icdf);
  cdf = _mm256_srli_epi16(cdf, EC_PROB_SHIFT);
  cdf = _mm256_slli_epi16(cdf, EC_PROB_SHIFT - 2);
  __m256i inc = _mm256_lddqu_si256((__m256i *)av2_prob_inc_tbl[nsyms - 2]);
  __m256i mask = _mm256_srai_epi16(inc, 15);
  inc = _mm256_slli_epi16(inc, EC_PROB_SHIFT - 6);
  cdf = _mm256_add_epi16(cdf, inc);
  __m256i rng = _mm256_set1_epi16(dec->rng);
  __m256i rngv = _mm256_srli_epi16(rng, 8);
  rngv = _mm256_slli_epi16(rngv, 8);
  __m256i sc_cdf = _mm256_mulhi_epu16(cdf, rngv);
  sc_cdf = _mm256_slli_epi16(sc_cdf, 3);
  od_ec_window dif = dec->dif;
  __m256i difv = _mm256_set1_epi16((int16_t)(dif >> (OD_EC_WINDOW_SIZE - 16)));
  difv = _mm256_or_si256(mask, difv);
  __m256i cmp_min = _mm256_min_epu16(sc_cdf, difv);
  __m256i gt = _mm256_cmpeq_epi16(sc_cdf, cmp_min);
  __m256i retv = _mm256_hadd_epi16(gt, gt);
  retv = _mm256_hadd_epi16(retv, retv);
  retv = _mm256_hadd_epi16(retv, retv);
  __m128i retv_hi = _mm256_extractf128_si256(retv, 1);
  __m128i retv_lo = _mm256_castsi256_si128(retv);
  retv_lo = _mm_add_epi16(retv_lo, retv_hi);
  int16_t ret = (int16_t)_mm_extract_epi16(retv_lo, 0);
  ret = 16 + ret;

  __m256i sc_cdf1 = _mm256_permute2x128_si256(sc_cdf, rng, 0x02);
  sc_cdf1 = _mm256_alignr_epi8(sc_cdf, sc_cdf1, 14);
  __m256i sc_cdf_diff = _mm256_sub_epi16(sc_cdf1, sc_cdf);
  _mm256_storeu_si256((__m256i *)scaled_cdf, sc_cdf);
  uint16_t scaled_cdf_diff[16];
  _mm256_storeu_si256((__m256i *)scaled_cdf_diff, sc_cdf_diff);
  unsigned v = scaled_cdf[ret];
  unsigned r = scaled_cdf_diff[ret];
  dif -= (od_ec_window)v << (OD_EC_WINDOW_SIZE - 16);

  return od_ec_dec_normalize(dec, dif, r, ret);
}
