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

// Processes 4 output neurons at a time. Assumes out_dim is a multiple of 4,
// true for all layer sizes this is called with (MLP_H1/H2/H3_DIM).
void av2_intra_mlp_layer_neon(const float *input, int in_dim,
                              const float *weights, const float *bias,
                              float *output, int out_dim, int apply_relu) {
  for (int i = 0; i < out_dim; i += 4) {
    float32x4_t sum0 = vld1q_f32(&bias[i]);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    int j = 0;
    for (; j + 1 < in_dim; j += 2) {
      float32x4_t w0 = vld1q_f32(&weights[(j + 0) * out_dim + i]);
      float32x4_t w1 = vld1q_f32(&weights[(j + 1) * out_dim + i]);
      float32x4_t f0 = vdupq_n_f32(input[j + 0]);
      float32x4_t f1 = vdupq_n_f32(input[j + 1]);
      sum0 = vmlaq_f32(sum0, f0, w0);
      sum1 = vmlaq_f32(sum1, f1, w1);
    }
    sum0 = vaddq_f32(sum0, sum1);
    for (; j < in_dim; j++) {
      float32x4_t w = vld1q_f32(&weights[j * out_dim + i]);
      float32x4_t f = vdupq_n_f32(input[j]);
      sum0 = vmlaq_f32(sum0, f, w);
    }
    if (apply_relu) {
      float32x4_t zero = vdupq_n_f32(0.0f);
      sum0 = vmaxq_f32(sum0, zero);
    }
    vst1q_f32(&output[i], sum0);
  }
}
