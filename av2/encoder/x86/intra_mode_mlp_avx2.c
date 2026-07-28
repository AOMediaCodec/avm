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

// Processes 8 output neurons at a time. Assumes out_dim is a multiple of 8,
// true for all layer sizes this is called with (MLP_H1/H2/H3_DIM).
void av2_intra_mlp_layer_avx2(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  for (int i = 0; i < out_dim; i += 8) {
    __m256 sum0 = _mm256_loadu_ps(&bias[i]);
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    int j = 0;
    for (; j + 3 < in_dim; j += 4) {
      __m256 w0 = _mm256_loadu_ps(&weights[(j + 0) * out_dim + i]);
      __m256 w1 = _mm256_loadu_ps(&weights[(j + 1) * out_dim + i]);
      __m256 w2 = _mm256_loadu_ps(&weights[(j + 2) * out_dim + i]);
      __m256 w3 = _mm256_loadu_ps(&weights[(j + 3) * out_dim + i]);
      __m256 f0 = _mm256_broadcast_ss(&input[j + 0]);
      __m256 f1 = _mm256_broadcast_ss(&input[j + 1]);
      __m256 f2 = _mm256_broadcast_ss(&input[j + 2]);
      __m256 f3 = _mm256_broadcast_ss(&input[j + 3]);
      sum0 = _mm256_add_ps(sum0, _mm256_mul_ps(f0, w0));
      sum1 = _mm256_add_ps(sum1, _mm256_mul_ps(f1, w1));
      sum2 = _mm256_add_ps(sum2, _mm256_mul_ps(f2, w2));
      sum3 = _mm256_add_ps(sum3, _mm256_mul_ps(f3, w3));
    }
    sum0 = _mm256_add_ps(sum0, sum1);
    sum2 = _mm256_add_ps(sum2, sum3);
    sum0 = _mm256_add_ps(sum0, sum2);
    for (; j < in_dim; j++) {
      __m256 w = _mm256_loadu_ps(&weights[j * out_dim + i]);
      __m256 f = _mm256_broadcast_ss(&input[j]);
      sum0 = _mm256_add_ps(sum0, _mm256_mul_ps(f, w));
    }
    if (apply_relu) {
      __m256 zero = _mm256_setzero_ps();
      sum0 = _mm256_max_ps(sum0, zero);
    }
    _mm256_storeu_ps(&output[i], sum0);
  }
}
