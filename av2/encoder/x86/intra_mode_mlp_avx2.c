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

// See fp_contract note in intra_mode_mlp.c
#if !defined(__clang__) && defined(_MSC_VER) && _MSC_VER < 1930
#pragma fp_contract(off)
#endif

void av2_intra_mlp_layer_avx2(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  const __m256 zero = _mm256_setzero_ps();
  int i = 0;
  for (; i + 31 < out_dim; i += 32) {
    __m256 sum0 = _mm256_loadu_ps(&bias[i + 0]);
    __m256 sum1 = _mm256_loadu_ps(&bias[i + 8]);
    __m256 sum2 = _mm256_loadu_ps(&bias[i + 16]);
    __m256 sum3 = _mm256_loadu_ps(&bias[i + 24]);
    for (int j = 0; j < in_dim; j++) {
      const float *w = &weights[j * out_dim + i];
      const __m256 f = _mm256_broadcast_ss(&input[j]);
      sum0 = _mm256_add_ps(sum0, _mm256_mul_ps(f, _mm256_loadu_ps(w + 0)));
      sum1 = _mm256_add_ps(sum1, _mm256_mul_ps(f, _mm256_loadu_ps(w + 8)));
      sum2 = _mm256_add_ps(sum2, _mm256_mul_ps(f, _mm256_loadu_ps(w + 16)));
      sum3 = _mm256_add_ps(sum3, _mm256_mul_ps(f, _mm256_loadu_ps(w + 24)));
    }
    if (apply_relu) {
      sum0 = _mm256_max_ps(sum0, zero);
      sum1 = _mm256_max_ps(sum1, zero);
      sum2 = _mm256_max_ps(sum2, zero);
      sum3 = _mm256_max_ps(sum3, zero);
    }
    _mm256_storeu_ps(&output[i + 0], sum0);
    _mm256_storeu_ps(&output[i + 8], sum1);
    _mm256_storeu_ps(&output[i + 16], sum2);
    _mm256_storeu_ps(&output[i + 24], sum3);
  }
  for (; i + 7 < out_dim; i += 8) {
    __m256 sum = _mm256_loadu_ps(&bias[i]);
    for (int j = 0; j < in_dim; j++) {
      const __m256 w = _mm256_loadu_ps(&weights[j * out_dim + i]);
      const __m256 f = _mm256_broadcast_ss(&input[j]);
      sum = _mm256_add_ps(sum, _mm256_mul_ps(f, w));
    }
    if (apply_relu) sum = _mm256_max_ps(sum, zero);
    _mm256_storeu_ps(&output[i], sum);
  }
  // Remaining (out_dim % 8) outputs: masked loads and store.
  if (i < out_dim) {
    static const int32_t kMaskTable[16] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                            0,  0,  0,  0,  0,  0,  0,  0 };
    const __m256i mask =
        _mm256_loadu_si256((const __m256i *)&kMaskTable[8 - (out_dim - i)]);
    __m256 sum = _mm256_maskload_ps(&bias[i], mask);
    for (int j = 0; j < in_dim; j++) {
      const __m256 w = _mm256_maskload_ps(&weights[j * out_dim + i], mask);
      const __m256 f = _mm256_broadcast_ss(&input[j]);
      sum = _mm256_add_ps(sum, _mm256_mul_ps(f, w));
    }
    if (apply_relu) sum = _mm256_max_ps(sum, zero);
    _mm256_maskstore_ps(&output[i], mask, sum);
  }
}
