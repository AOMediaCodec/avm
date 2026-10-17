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

#include <assert.h>
#include <immintrin.h>

#include "config/av2_rtcd.h"
#include "av2/encoder/pickrst.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE __m256i load_16(const int16_t *p) {
  return _mm256_loadu_si256((const __m256i *)p);
}

// Folds 8 int32 lanes to 4.
static INLINE __m128i fold_to_4lanes(const __m256i sum) {
  return _mm_add_epi32(_mm256_castsi256_si128(sum),
                       _mm256_extracti128_si256(sum, 1));
}

// Returns the dot product of rows a and b in 4 int32 lanes.
static INLINE __m128i dot_4lanes(const int16_t *a, const int16_t *b, int n16,
                                 int has_8) {
  __m256i sum = _mm256_setzero_si256();
  for (int x = 0; x < n16; x += 16) {
    sum = _mm256_add_epi32(sum,
                           _mm256_madd_epi16(load_16(a + x), load_16(b + x)));
  }
  __m128i sum4 = fold_to_4lanes(sum);
  if (has_8) {
    sum4 = _mm_add_epi32(
        sum4, _mm_madd_epi16(xx_loadu_128(a + n16), xx_loadu_128(b + n16)));
  }
  return sum4;
}

static_assert(WIENERNS_STATS_BATCH / 2 * (int64_t)(WIENERNS_STATS_MAX_ABS - 1) *
                      (WIENERNS_STATS_MAX_ABS - 1) <=
                  INT32_MAX,
              "Two int32 lanes must hold their sum of products");

// Returns { sum of the lanes of a, sum of the lanes of b } in 64 bits. Two
// lanes are added in 32 bits first, see below for why it cannot overflow.
static INLINE __m128i add_lanes_pair_epi32(const __m128i a, const __m128i b) {
  const __m128i pair =
      _mm_add_epi32(_mm_unpacklo_epi32(a, b), _mm_unpackhi_epi32(a, b));
  return _mm_add_epi64(_mm_cvtepi32_epi64(pair),
                       _mm_cvtepi32_epi64(_mm_srli_si128(pair, 8)));
}

// Returns the sum of the lanes of a in 64 bits, adding two lanes in 32 bits
// first like add_lanes_pair_epi32().
static INLINE int64_t add_lanes_epi32(const __m128i a) {
  const __m128i pair = _mm_add_epi32(a, _mm_srli_si128(a, 8));
  return (int64_t)_mm_cvtsi128_si32(pair) + _mm_extract_epi32(pair, 1);
}

// Each of the 4 int32 lanes left before the 64-bit adds sums n / 4 <= 16
// products. The values are below 2^13 in absolute value, so each product is
// at most (2^13 - 1)^2 and 2 lanes sum at most 32 * (2^13 - 1)^2 < 2^31.
void av2_wienerns_stats_batch_avx2(int64_t *A, const int16_t *feat,
                                   int feat_stride, int n, int dim) {
  if (n == 0) return;
  assert(n % 8 == 0 && n <= WIENERNS_STATS_BATCH);
  const int n16 = n & ~15;
  const int has_8 = n & 8;
  for (int k = 0; k < dim; ++k) {
    const int16_t *feat_k = feat + k * feat_stride;
    int64_t *A_k = A + k * dim;
    int l = 0;
    for (; l + 3 <= k; l += 4) {
      // Same as dot_4lanes() on each row, loading feat_k once for 4 rows.
      const int16_t *f0 = feat + l * feat_stride;
      const int16_t *f1 = f0 + feat_stride;
      const int16_t *f2 = f1 + feat_stride;
      const int16_t *f3 = f2 + feat_stride;
      __m256i s0 = _mm256_setzero_si256(), s1 = _mm256_setzero_si256();
      __m256i s2 = _mm256_setzero_si256(), s3 = _mm256_setzero_si256();
      for (int x = 0; x < n16; x += 16) {
        const __m256i a = load_16(feat_k + x);
        s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(a, load_16(f0 + x)));
        s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(a, load_16(f1 + x)));
        s2 = _mm256_add_epi32(s2, _mm256_madd_epi16(a, load_16(f2 + x)));
        s3 = _mm256_add_epi32(s3, _mm256_madd_epi16(a, load_16(f3 + x)));
      }
      __m128i sum0 = fold_to_4lanes(s0), sum1 = fold_to_4lanes(s1);
      __m128i sum2 = fold_to_4lanes(s2), sum3 = fold_to_4lanes(s3);
      if (has_8) {
        const __m128i a = xx_loadu_128(feat_k + n16);
        sum0 = _mm_add_epi32(sum0, _mm_madd_epi16(a, xx_loadu_128(f0 + n16)));
        sum1 = _mm_add_epi32(sum1, _mm_madd_epi16(a, xx_loadu_128(f1 + n16)));
        sum2 = _mm_add_epi32(sum2, _mm_madd_epi16(a, xx_loadu_128(f2 + n16)));
        sum3 = _mm_add_epi32(sum3, _mm_madd_epi16(a, xx_loadu_128(f3 + n16)));
      }
      __m128i *dst01 = (__m128i *)(A_k + l);
      __m128i *dst23 = (__m128i *)(A_k + l + 2);
      _mm_storeu_si128(dst01, _mm_add_epi64(_mm_loadu_si128(dst01),
                                            add_lanes_pair_epi32(sum0, sum1)));
      _mm_storeu_si128(dst23, _mm_add_epi64(_mm_loadu_si128(dst23),
                                            add_lanes_pair_epi32(sum2, sum3)));
    }
    for (; l <= k; ++l) {
      A_k[l] += add_lanes_epi32(
          dot_4lanes(feat_k, feat + l * feat_stride, n16, has_8));
    }
  }
}
