/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h> /* AVX2 */

#include "aom_dsp/aom_dsp_common.h"
#include "av1/common/intra_matrix.h"

// Multiply 11 element feature vector with matrix to generate 8x8 prediction.
// A - pointer to matrix
// B - pointer to feature vector
// C - 8x8 output prediction
// bd - bit depth
void av1_dip_matrix_multiplication_avx2(const uint16_t *A, const uint16_t *B,
                                        uint16_t *C, int bd) {
  static const uint16_t mask[16] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, 0,  0,  0,  0,  0 };

  __m256i in0 = _mm256_lddqu_si256((__m256i *)B);
  __m256i in_mask = _mm256_lddqu_si256((__m256i *)mask);
  in0 = _mm256_and_si256(in0, in_mask);
  // in0 = { B0, B1, B2, B3, B4, B5, B6, B7 | B8, B9, B10, 0, 0, 0, 0, 0 }
  __m256i negsum = _mm256_madd_epi16(in0, in_mask);
  negsum = _mm256_hadd_epi32(negsum, negsum);
  negsum = _mm256_hadd_epi32(negsum, negsum);
  negsum = _mm256_slli_epi32(negsum, DIP_BITS - 2);
  __m128i offset = _mm_set1_epi32(DIP_OFFSET >> 2);
  __m128i maxval = _mm_set1_epi32((1 << bd) - 1);
  __m128i zero = _mm_setzero_si128();

  for (int i = 0; i < DIP_ROWS; i += 4) {
    __m256i row0 = _mm256_lddqu_si256((__m256i *)&A[i * DIP_COLS]);
    __m256i row1 = _mm256_lddqu_si256((__m256i *)&A[(i + 1) * DIP_COLS]);
    __m256i row2 = _mm256_lddqu_si256((__m256i *)&A[(i + 2) * DIP_COLS]);
    __m256i row3 = _mm256_lddqu_si256((__m256i *)&A[(i + 3) * DIP_COLS]);
    __m256i m0 = _mm256_madd_epi16(row0, in0);
    __m256i m1 = _mm256_madd_epi16(row1, in0);
    __m256i m2 = _mm256_madd_epi16(row2, in0);
    __m256i m3 = _mm256_madd_epi16(row3, in0);
    __m256i m01 = _mm256_hadd_epi32(m0, m1);
    __m256i m23 = _mm256_hadd_epi32(m2, m3);
    __m256i m0123 = _mm256_hadd_epi32(m01, m23);
    __m256i sum0 = _mm256_add_epi32(m0123, negsum);
    __m128i sum0_lo = _mm256_castsi256_si128(sum0);
    __m128i sum0_hi = _mm256_extracti128_si256(sum0, 1);
    __m128i sum1 = _mm_add_epi32(sum0_lo, sum0_hi);
    sum1 = _mm_add_epi32(sum1, offset);
    sum1 = _mm_srai_epi32(sum1, DIP_BITS - 2);
    sum1 = _mm_min_epi32(sum1, maxval);
    sum1 = _mm_max_epi32(sum1, zero);
    __m128i out0 = _mm_packus_epi32(sum1, sum1);
    _mm_storeu_si64(&C[i], out0);
  }
}
