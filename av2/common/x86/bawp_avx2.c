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

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

void av2_make_bawp_block_avx2(uint16_t *dst, int dst_stride, int16_t alpha,
                              int32_t beta, int shift, int bw, int bh, int bd) {
  const __m256i alpha_reg = _mm256_set1_epi32((int)alpha);
  const __m256i beta_reg = _mm256_set1_epi32(beta);
  const __m256i clip_pixel =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  if (bw == 4 && ((bh & 3) == 0)) {
    for (int j = 0; j < bh; j += 4) {
      // d00 d01 d02 d03
      const __m128i dst_0 = _mm_cvtepu16_epi32(
          _mm_loadl_epi64((const __m128i *)(&dst[j * dst_stride])));
      // d10 d11 d12 d13
      const __m128i dst_1 = _mm_cvtepu16_epi32(
          _mm_loadl_epi64((const __m128i *)(&dst[(j + 1) * dst_stride])));
      // d00 d01 d02 d03 | d10 d11 d12 d13
      const __m256i dst_01 =
          _mm256_inserti128_si256(_mm256_castsi128_si256(dst_0), dst_1, 1);
      // d20 d21 d22 d23
      const __m128i dst_2 = _mm_cvtepu16_epi32(
          _mm_loadl_epi64((const __m128i *)(&dst[(j + 2) * dst_stride])));
      // d30 d31 d32 d33
      const __m128i dst_3 = _mm_cvtepu16_epi32(
          _mm_loadl_epi64((const __m128i *)(&dst[(j + 3) * dst_stride])));
      // d20 d21 d22 d23 | d30 d31 d32 d33
      const __m256i dst_23 =
          _mm256_inserti128_si256(_mm256_castsi128_si256(dst_2), dst_3, 1);

      const __m256i res_0 = _mm256_srai_epi32(
          _mm256_add_epi32(beta_reg, _mm256_mullo_epi32(dst_01, alpha_reg)),
          shift);
      const __m256i res_1 = _mm256_srai_epi32(
          _mm256_add_epi32(beta_reg, _mm256_mullo_epi32(dst_23, alpha_reg)),
          shift);
      // 00 01 02 03 | 20 21 22 23 | 10 11 12 13 | 30 31 32 33
      const __m256i res_2 = _mm256_packus_epi32(res_0, res_1);
      const __m256i res = _mm256_min_epu16(res_2, clip_pixel);
      const __m128i res_lo = _mm256_castsi256_si128(res);
      const __m128i res_hi = _mm256_extracti128_si256(res, 1);

      _mm_storel_epi64((__m128i *)(&dst[j * dst_stride]), res_lo);
      _mm_storel_epi64((__m128i *)(&dst[(j + 1) * dst_stride]), res_hi);
      _mm_storel_epi64((__m128i *)(&dst[(j + 2) * dst_stride]),
                       _mm_srli_si128(res_lo, 8));
      _mm_storel_epi64((__m128i *)(&dst[(j + 3) * dst_stride]),
                       _mm_srli_si128(res_hi, 8));
    }
  } else if (((bw & 7) == 0) && ((bh & 1) == 0)) {
    for (int j = 0; j < bh; j += 2) {
      for (int i = 0; i < bw; i += 8) {
        // d00 d01 d02 d03 d04 d05 d06 d07
        const __m256i dst_0 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((const __m128i *)(&dst[j * dst_stride + i])));
        // d10 d11 d12 d13 d14 d15 d16 d17
        const __m256i dst_1 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((const __m128i *)(&dst[(j + 1) * dst_stride + i])));

        const __m256i res_0 = _mm256_srai_epi32(
            _mm256_add_epi32(beta_reg, _mm256_mullo_epi32(dst_0, alpha_reg)),
            shift);
        const __m256i res_1 = _mm256_srai_epi32(
            _mm256_add_epi32(beta_reg, _mm256_mullo_epi32(dst_1, alpha_reg)),
            shift);
        const __m256i res_2 =
            _mm256_permute4x64_epi64(_mm256_packus_epi32(res_0, res_1), 0xD8);
        const __m256i res = _mm256_min_epu16(res_2, clip_pixel);

        _mm_storeu_si128((__m128i *)(&dst[j * dst_stride + i]),
                         _mm256_castsi256_si128(res));
        _mm_storeu_si128((__m128i *)(&dst[(j + 1) * dst_stride + i]),
                         _mm256_extracti128_si256(res, 1));
      }
    }
  } else {
    av2_make_bawp_block_c(dst, dst_stride, alpha, beta, shift, bw, bh, bd);
  }
}
