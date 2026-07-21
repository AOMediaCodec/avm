/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this file, you can obtain it at
 * aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <math.h>

#include "avm_dsp/avm_dsp_common.h"

#include "av2/encoder/intra_mode_mlp.h"
#include "av2/encoder/intra_mode_mlp_weights.h"

#include "config/av2_rtcd.h"

void intra_mode_mlp_prepare_features(
    const uint16_t *src, int src_stride, int bd, int bw, int bh, int qp,
    int neighbor_above_mode, int neighbor_above_delta, int neighbor_left_mode,
    int neighbor_left_delta, int is_inter_frame, float *features) {
  const float pixel_norm = (float)((1 << bd) - 1);

  // Nearest-neighbor downsample to 8x8
  for (int r = 0; r < 8; r++) {
    const int src_r = (r * bh) >> 3;
    for (int c = 0; c < 8; c++) {
      const int src_c = (c * bw) >> 3;
      features[r * 8 + c] = (float)src[src_r * src_stride + src_c] / pixel_norm;
    }
  }

  // Neighbor mode features (normalized)
  int above_mode = (neighbor_above_mode < 0) ? 13 : neighbor_above_mode;
  int left_mode = (neighbor_left_mode < 0) ? 13 : neighbor_left_mode;
  int above_delta = (neighbor_above_mode < 0) ? 0 : neighbor_above_delta;
  int left_delta = (neighbor_left_mode < 0) ? 0 : neighbor_left_delta;

  features[64] = (float)above_mode / MLP_NORM_NEIGHBOR_MODE;
  features[65] =
      ((float)above_delta + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE;
  features[66] = (float)left_mode / MLP_NORM_NEIGHBOR_MODE;
  features[67] =
      ((float)left_delta + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE;

  // Block size and QP (normalized)
  features[68] = (float)bw / MLP_NORM_BW;
  features[69] = (float)bh / MLP_NORM_BH;
  features[70] = (float)qp / MLP_NORM_QP;

  // is_inter_frame flag (binary, no normalization)
  features[71] = (float)is_inter_frame;

  // HOG 32-bin histogram, appended after the 72 base features above.
  float *hog = &features[72];
  for (int i = 0; i < 32; i++) hog[i] = 0.0f;
  float hog_total = 0.1f;
  for (int r = 1; r < bh - 1; r++) {
    for (int c = 1; c < bw - 1; c++) {
      const int idx_center = r * src_stride + c;
      const int dx =
          (int)src[idx_center + 1 - src_stride] + 2 * (int)src[idx_center + 1] +
          (int)src[idx_center + 1 + src_stride] -
          (int)src[idx_center - 1 - src_stride] - 2 * (int)src[idx_center - 1] -
          (int)src[idx_center - 1 + src_stride];
      const int dy = (int)src[idx_center + src_stride - 1] +
                     2 * (int)src[idx_center + src_stride] +
                     (int)src[idx_center + src_stride + 1] -
                     (int)src[idx_center - src_stride - 1] -
                     2 * (int)src[idx_center - src_stride] -
                     (int)src[idx_center - src_stride + 1];
      if (dx == 0 && dy == 0) continue;
      const int mag = abs(dx) + abs(dy);
      hog_total += mag;
      if (dx == 0) {
        hog[0] += mag / 2;
        hog[31] += mag / 2;
      } else {
        float angle = atan2f((float)dy, (float)dx);
        if (angle < 0) angle += 3.14159265f;
        int bin = (int)(angle * 32.0f / 3.14159265f);
        if (bin >= 32) bin = 31;
        if (bin < 0) bin = 0;
        hog[bin] += mag;
      }
    }
  }
  for (int i = 0; i < 32; i++) hog[i] /= hog_total;
}

// Scalar reference implementation of one fully-connected layer.
void av2_intra_mlp_layer_c(const float *input, int in_dim, const float *weights,
                           const float *bias, float *output, int out_dim,
                           int apply_relu) {
  for (int i = 0; i < out_dim; i++) {
    float sum = bias[i];
    for (int j = 0; j < in_dim; j++) sum += input[j] * weights[j * out_dim + i];
    output[i] = apply_relu ? AVMMAX(sum, 0.0f) : sum;
  }
}

void intra_mode_mlp_predict(const float *features, float *logits) {
  float h1[MLP_H1_DIM], h2[MLP_H2_DIM], h3[MLP_H3_DIM];
  av2_intra_mlp_layer(features, MLP_INPUT_DIM, mlp_w1, mlp_b1, h1, MLP_H1_DIM,
                      1);
  av2_intra_mlp_layer(h1, MLP_H1_DIM, mlp_w2, mlp_b2, h2, MLP_H2_DIM, 1);
  av2_intra_mlp_layer(h2, MLP_H2_DIM, mlp_w3, mlp_b3, h3, MLP_H3_DIM, 1);
  // Layer 4: output dim=13, not a multiple of the SIMD widths above — scalar.
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
    float sum = mlp_b4[i];
    for (int j = 0; j < MLP_H3_DIM; j++)
      sum += h3[j] * mlp_w4[j * MLP_OUTPUT_DIM + i];
    logits[i] = sum;
  }
}

void intra_mode_mlp_get_topk(const float *logits, int k, int *modes) {
  uint8_t used[MLP_OUTPUT_DIM] = { 0 };
  for (int t = 0; t < k; t++) {
    float best_val = -1e30f;
    int best_idx = -1;
    for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
      if (used[i]) continue;
      // First unused index is a NaN-safe fallback: logits[i] > best_val is
      // always false when logits[i] is NaN, so without this best_idx could
      // otherwise stay unset (or, previously, default to an already-used 0).
      if (best_idx < 0) best_idx = i;
      if (logits[i] > best_val) {
        best_val = logits[i];
        best_idx = i;
      }
    }
    modes[t] = best_idx;
    used[best_idx] = 1;
  }
}
