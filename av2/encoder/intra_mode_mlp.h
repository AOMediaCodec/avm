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

#ifndef AV2_ENCODER_INTRA_MODE_MLP_H_
#define AV2_ENCODER_INTRA_MODE_MLP_H_

#include <stdint.h>

#include "av2/encoder/intra_mode_mlp_weights.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fills all MLP_INPUT_DIM (104) features: 64 downsampled pixels, 4 neighbor
// mode/delta, 3 size/QP, 1 is_inter_frame flag, and a 32-bin HOG histogram.
void intra_mode_mlp_prepare_features(
    const uint16_t *src, int src_stride, int bd, int bw, int bh, int qp,
    int neighbor_above_mode, int neighbor_above_delta, int neighbor_left_mode,
    int neighbor_left_delta, int is_inter_frame, float *features);

void intra_mode_mlp_predict(const float *features, float *logits);

void intra_mode_mlp_get_topk(const float *logits, int k, int *modes);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_INTRA_MODE_MLP_H_
