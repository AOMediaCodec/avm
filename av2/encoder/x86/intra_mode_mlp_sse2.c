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

void av2_intra_mlp_layer_sse2(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  const __m128 zero = _mm_setzero_ps();
  int i = 0;
  for (; i + 15 < out_dim; i += 16) {
    __m128 sum0 = _mm_loadu_ps(&bias[i + 0]);
    __m128 sum1 = _mm_loadu_ps(&bias[i + 4]);
    __m128 sum2 = _mm_loadu_ps(&bias[i + 8]);
    __m128 sum3 = _mm_loadu_ps(&bias[i + 12]);
    for (int j = 0; j < in_dim; j++) {
      const float *w = &weights[j * out_dim + i];
      const __m128 f = _mm_set1_ps(input[j]);
      sum0 = _mm_add_ps(sum0, _mm_mul_ps(f, _mm_loadu_ps(w + 0)));
      sum1 = _mm_add_ps(sum1, _mm_mul_ps(f, _mm_loadu_ps(w + 4)));
      sum2 = _mm_add_ps(sum2, _mm_mul_ps(f, _mm_loadu_ps(w + 8)));
      sum3 = _mm_add_ps(sum3, _mm_mul_ps(f, _mm_loadu_ps(w + 12)));
    }
    if (apply_relu) {
      sum0 = _mm_max_ps(sum0, zero);
      sum1 = _mm_max_ps(sum1, zero);
      sum2 = _mm_max_ps(sum2, zero);
      sum3 = _mm_max_ps(sum3, zero);
    }
    _mm_storeu_ps(&output[i + 0], sum0);
    _mm_storeu_ps(&output[i + 4], sum1);
    _mm_storeu_ps(&output[i + 8], sum2);
    _mm_storeu_ps(&output[i + 12], sum3);
  }
  for (; i + 3 < out_dim; i += 4) {
    __m128 sum = _mm_loadu_ps(&bias[i]);
    for (int j = 0; j < in_dim; j++) {
      const __m128 w = _mm_loadu_ps(&weights[j * out_dim + i]);
      const __m128 f = _mm_set1_ps(input[j]);
      sum = _mm_add_ps(sum, _mm_mul_ps(f, w));
    }
    if (apply_relu) sum = _mm_max_ps(sum, zero);
    _mm_storeu_ps(&output[i], sum);
  }
  // scalar processing for remaining weights
  for (; i < out_dim; i++) {
    float sum = bias[i];
    for (int j = 0; j < in_dim; j++) sum += input[j] * weights[j * out_dim + i];
    output[i] = apply_relu ? AVMMAX(sum, 0.0f) : sum;
  }
}
