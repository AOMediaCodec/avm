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

#include <immintrin.h>

#include "config/av2_rtcd.h"

#include "av2/common/enums.h"

// Index of the nearest centroid for each of the n values, eight values at a
// time. As in the C code, the first of several centroids at the same distance
// wins.
void av2_calc_indices_dim1_avx2(const int *data, const int *centroids,
                                uint8_t *indices, int n, int k) {
  __m256i cents[PALETTE_MAX_SIZE];
  assert(k >= 1 && k <= PALETTE_MAX_SIZE);
  for (int j = 0; j < k; ++j) cents[j] = _mm256_set1_epi32(centroids[j]);

  int i = 0;
  for (; i + 8 <= n; i += 8) {
    const __m256i d = _mm256_loadu_si256((const __m256i *)(data + i));
    __m256i diff = _mm256_sub_epi32(d, cents[0]);
    __m256i min_dist = _mm256_mullo_epi32(diff, diff);
    __m256i ind = _mm256_setzero_si256();
    for (int j = 1; j < k; ++j) {
      diff = _mm256_sub_epi32(d, cents[j]);
      const __m256i dist = _mm256_mullo_epi32(diff, diff);
      const __m256i is_nearer = _mm256_cmpgt_epi32(min_dist, dist);
      min_dist = _mm256_blendv_epi8(min_dist, dist, is_nearer);
      ind = _mm256_blendv_epi8(ind, _mm256_set1_epi32(j), is_nearer);
    }
    // Narrow the eight indices to bytes.
    const __m128i ind16 = _mm_packus_epi32(_mm256_castsi256_si128(ind),
                                           _mm256_extracti128_si256(ind, 1));
    _mm_storel_epi64((__m128i *)(indices + i), _mm_packus_epi16(ind16, ind16));
  }
  for (; i < n; ++i) {
    int diff = data[i] - centroids[0];
    int min_dist = diff * diff;
    indices[i] = 0;
    for (int j = 1; j < k; ++j) {
      diff = data[i] - centroids[j];
      const int this_dist = diff * diff;
      if (this_dist < min_dist) {
        min_dist = this_dist;
        indices[i] = j;
      }
    }
  }
}
