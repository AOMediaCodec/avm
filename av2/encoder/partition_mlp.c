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

#include <math.h>
#include <stdlib.h>

#include "av2/encoder/partition_mlp.h"
#include "av2/encoder/partition_mlp_inter_weights.h"
#include "av2/encoder/partition_mlp_kf_weights.h"

#define PART_MLP_PI 3.14159265f

/* ------------------------------------------------------------------ */
/* Feature dimensions                                                  */
/* ------------------------------------------------------------------ */
#define PART_MLP_IN_DIM 38
#define PART_MLP_H1_DIM 64
#define PART_MLP_H2_DIM 32
#define PART_MLP_OUT_DIM 4
#define PART_MLP_HOG_BINS 32

/* ------------------------------------------------------------------ */
/* HOG computation (same as partition feature logging)                 */
/* ------------------------------------------------------------------ */
static void compute_hog(const uint16_t *src, int stride, int bw, int bh,
                        float *hog) {
  float hog_total = 0.1f;
  for (int i = 0; i < PART_MLP_HOG_BINS; i++) hog[i] = 0.0f;

  for (int r = 1; r < bh - 1; r++) {
    for (int c = 1; c < bw - 1; c++) {
      const int idx = r * stride + c;
      const int dx = (int)src[idx + 1 - stride] + 2 * (int)src[idx + 1] +
                     (int)src[idx + 1 + stride] - (int)src[idx - 1 - stride] -
                     2 * (int)src[idx - 1] - (int)src[idx - 1 + stride];
      const int dy = (int)src[idx + stride - 1] + 2 * (int)src[idx + stride] +
                     (int)src[idx + stride + 1] - (int)src[idx - stride - 1] -
                     2 * (int)src[idx - stride] - (int)src[idx - stride + 1];
      if (dx == 0 && dy == 0) continue;
      const int mag = abs(dx) + abs(dy);
      hog_total += mag;
      if (dx == 0) {
        hog[0] += mag / 2.0f;
        hog[31] += mag / 2.0f;
      } else {
        float angle = atan2f((float)dy, (float)dx);
        if (angle < 0) angle += PART_MLP_PI;
        int bin = (int)(angle * 32.0f / PART_MLP_PI);
        if (bin >= 32) bin = 31;
        if (bin < 0) bin = 0;
        hog[bin] += mag;
      }
    }
  }
  for (int i = 0; i < PART_MLP_HOG_BINS; i++) hog[i] /= hog_total;
}

/* ------------------------------------------------------------------ */
/* FC layer: out = ReLU(W @ in + b)                                    */
/* W is stored row-major: W[out_dim][in_dim]                           */
/* ------------------------------------------------------------------ */
static void fc_relu(const float *w, const float *b, const float *in, float *out,
                    int in_dim, int out_dim) {
  for (int i = 0; i < out_dim; i++) {
    float acc = b[i];
    const float *row = w + i * in_dim;
    for (int j = 0; j < in_dim; j++) acc += row[j] * in[j];
    out[i] = acc > 0.0f ? acc : 0.0f;
  }
}

static void fc(const float *w, const float *b, const float *in, float *out,
               int in_dim, int out_dim) {
  for (int i = 0; i < out_dim; i++) {
    float acc = b[i];
    const float *row = w + i * in_dim;
    for (int j = 0; j < in_dim; j++) acc += row[j] * in[j];
    out[i] = acc;
  }
}

/* ------------------------------------------------------------------ */
/* Forward pass                                                         */
/* ------------------------------------------------------------------ */
static int forward(const float *w0, const float *b0, const float *w2,
                   const float *b2, const float *w4, const float *b4,
                   const float *input, float *logits_out) {
  float h1[PART_MLP_H1_DIM];
  float h2[PART_MLP_H2_DIM];

  fc_relu(w0, b0, input, h1, PART_MLP_IN_DIM, PART_MLP_H1_DIM);
  fc_relu(w2, b2, h1, h2, PART_MLP_H1_DIM, PART_MLP_H2_DIM);
  fc(w4, b4, h2, logits_out, PART_MLP_H2_DIM, PART_MLP_OUT_DIM);

  int best = 0;
  for (int i = 1; i < PART_MLP_OUT_DIM; i++)
    if (logits_out[i] > logits_out[best]) best = i;
  return best;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
int av2_partition_mlp_predict(const uint16_t *src, int stride, int bw, int bh,
                              int bsize, int qindex, unsigned int source_var,
                              int64_t none_rd, int above_part, int left_part,
                              int is_intra, float *logits_out) {
  float input[PART_MLP_IN_DIM];

  /* HOG (32 bins) */
  compute_hog(src, stride, bw, bh, input);

  /* Scalar features — normalisation must match training */
  input[32] = logf(1.0f + (float)source_var) / 12.0f;
  input[33] = (float)bsize / 18.0f;
  input[34] = (float)qindex / 255.0f;
  input[35] =
      logf(1.0f + (float)(none_rd > 0 && none_rd < INT64_MAX ? none_rd : 0)) /
      30.0f;
  input[36] = ((float)above_part + 1.0f) / 10.0f;
  input[37] = ((float)left_part + 1.0f) / 10.0f;

  if (is_intra) {
    return forward(kf_w0, kf_b0, kf_w1, kf_b1, kf_w2, kf_b2, input, logits_out);
  } else {
    return forward(inter_w0, inter_b0, inter_w1, inter_b1, inter_w2, inter_b2,
                   input, logits_out);
  }
}
