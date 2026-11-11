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

#include <arm_neon.h>

#include "config/av2_rtcd.h"

// See fp_contract note in intra_mode_mlp.c
#if !defined(__clang__) && defined(_MSC_VER) && _MSC_VER < 1930
#pragma fp_contract(off)
#endif

void av2_intra_mlp_layer_neon(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  const float32x4_t zero = vdupq_n_f32(0.0f);
  int i = 0;
  for (; i + 15 < out_dim; i += 16) {
    float32x4_t sum0 = vld1q_f32(&bias[i + 0]);
    float32x4_t sum1 = vld1q_f32(&bias[i + 4]);
    float32x4_t sum2 = vld1q_f32(&bias[i + 8]);
    float32x4_t sum3 = vld1q_f32(&bias[i + 12]);
    for (int j = 0; j < in_dim; j++) {
      const float *w = &weights[j * out_dim + i];
      const float32x4_t f = vld1q_dup_f32(&input[j]);
      sum0 = vaddq_f32(sum0, vmulq_f32(f, vld1q_f32(w + 0)));
      sum1 = vaddq_f32(sum1, vmulq_f32(f, vld1q_f32(w + 4)));
      sum2 = vaddq_f32(sum2, vmulq_f32(f, vld1q_f32(w + 8)));
      sum3 = vaddq_f32(sum3, vmulq_f32(f, vld1q_f32(w + 12)));
    }
    if (apply_relu) {
      sum0 = vmaxq_f32(sum0, zero);
      sum1 = vmaxq_f32(sum1, zero);
      sum2 = vmaxq_f32(sum2, zero);
      sum3 = vmaxq_f32(sum3, zero);
    }
    vst1q_f32(&output[i + 0], sum0);
    vst1q_f32(&output[i + 4], sum1);
    vst1q_f32(&output[i + 8], sum2);
    vst1q_f32(&output[i + 12], sum3);
  }
  for (; i + 3 < out_dim; i += 4) {
    float32x4_t sum = vld1q_f32(&bias[i]);
    for (int j = 0; j < in_dim; j++) {
      const float32x4_t w = vld1q_f32(&weights[j * out_dim + i]);
      const float32x4_t f = vld1q_dup_f32(&input[j]);
      sum = vaddq_f32(sum, vmulq_f32(f, w));
    }
    if (apply_relu) sum = vmaxq_f32(sum, zero);
    vst1q_f32(&output[i], sum);
  }
  // scalar processing for remaining weights
  for (; i < out_dim; i++) {
    float sum = bias[i];
    for (int j = 0; j < in_dim; j++) sum += input[j] * weights[j * out_dim + i];
    output[i] = apply_relu ? AVMMAX(sum, 0.0f) : sum;
  }
}
