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

// Processes 4 output neurons at a time. Assumes out_dim is a multiple of 4,
// true for all layer sizes this is called with (MLP_H1/H2/H3_DIM).
void av2_intra_mlp_layer_sse2(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  for (int i = 0; i < out_dim; i += 4) {
    __m128 sum0 = _mm_loadu_ps(&bias[i]);
    __m128 sum1 = _mm_setzero_ps();
    int j = 0;
    for (; j + 1 < in_dim; j += 2) {
      __m128 w0 = _mm_loadu_ps(&weights[(j + 0) * out_dim + i]);
      __m128 w1 = _mm_loadu_ps(&weights[(j + 1) * out_dim + i]);
      __m128 f0 = _mm_set1_ps(input[j + 0]);
      __m128 f1 = _mm_set1_ps(input[j + 1]);
      sum0 = _mm_add_ps(sum0, _mm_mul_ps(f0, w0));
      sum1 = _mm_add_ps(sum1, _mm_mul_ps(f1, w1));
    }
    sum0 = _mm_add_ps(sum0, sum1);
    for (; j < in_dim; j++) {
      __m128 w = _mm_loadu_ps(&weights[j * out_dim + i]);
      __m128 f = _mm_set1_ps(input[j]);
      sum0 = _mm_add_ps(sum0, _mm_mul_ps(f, w));
    }
    if (apply_relu) {
      __m128 zero = _mm_setzero_ps();
      sum0 = _mm_max_ps(sum0, zero);
    }
    _mm_storeu_ps(&output[i], sum0);
  }
}
