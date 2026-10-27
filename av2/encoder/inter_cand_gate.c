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

#include "av2/encoder/inter_cand_gate.h"

#include "av2/encoder/inter_cand_gate_weights.h"

float av2_inter_cand_gate_predict(const float *features) {
  float z[INTER_CAND_GATE_NUM_FEATURES];
  for (int i = 0; i < INTER_CAND_GATE_NUM_FEATURES; ++i) {
    z[i] = (features[i] - inter_cand_gate_mean[i]) * inter_cand_gate_inv_std[i];
  }
  float out = inter_cand_gate_b2;
  for (int h = 0; h < INTER_CAND_GATE_HIDDEN; ++h) {
    float a = inter_cand_gate_b1[h];
    const float *w = inter_cand_gate_w1[h];
    for (int i = 0; i < INTER_CAND_GATE_NUM_FEATURES; ++i) a += w[i] * z[i];
    if (a > 0.0f) out += inter_cand_gate_w2[h] * a;
  }
  return out;
}
