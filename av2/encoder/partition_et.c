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

#include "av2/encoder/partition_et.h"

#include <math.h>

#include "av2/encoder/partition_et_weights.h"

float av2_partition_et_predict(const float *features) {
  float z[PART_ET_NUM_FEATURES];
  for (int i = 0; i < PART_ET_NUM_FEATURES; ++i) {
    z[i] = (features[i] - part_et_mean[i]) * part_et_inv_std[i];
  }
  float out = part_et_b2;
  for (int h = 0; h < PART_ET_HIDDEN; ++h) {
    float a = part_et_b1[h];
    const float *w = part_et_w1[h];
    for (int i = 0; i < PART_ET_NUM_FEATURES; ++i) a += w[i] * z[i];
    if (a > 0.0f) out += part_et_w2[h] * a;
  }
  return 1.0f / (1.0f + expf(-out));
}
