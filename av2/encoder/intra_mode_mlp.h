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

// Side of the area covered by IntraMlpHogCache: the largest superblock.
#define MLP_HOG_CACHE_SIZE 256

// HOG entries of the samples of one superblock. The entry of a sample (its
// histogram bin and its gradient magnitude) depends on the source only, and a
// sample is visited once for every block of the partition search that has it
// in its interior, so the entry is kept from the first visit.
typedef struct IntraMlpHogCache {
  // 0 when the entries belong to another superblock: they are cleared on the
  // next use.
  int valid;
  // Gradient magnitude |dx| + |dy| of the sample.
  uint16_t mag[MLP_HOG_CACHE_SIZE * MLP_HOG_CACHE_SIZE];
  // Histogram bin of the sample, or one of the MLP_HOG_BIN_* codes.
  uint8_t bin[MLP_HOG_CACHE_SIZE * MLP_HOG_CACHE_SIZE];
} IntraMlpHogCache;

// Fills all MLP_INPUT_DIM (104) features: 64 downsampled pixels, 4 neighbor
// mode/delta, 3 size/QP, 1 is_inter_frame flag, and a 32-bin HOG histogram.
// hog_cache, if not NULL, holds the HOG entries of the superblock of the
// block, whose top-left sample is at (sb_row, sb_col) in the superblock.
void intra_mode_mlp_prepare_features(
    const uint16_t *src, int src_stride, int bd, int bw, int bh, int qp,
    int neighbor_above_mode, int neighbor_above_delta, int neighbor_left_mode,
    int neighbor_left_delta, int is_inter_frame, IntraMlpHogCache *hog_cache,
    int sb_row, int sb_col, float *features);

void intra_mode_mlp_predict(const float *features, float *logits);

void intra_mode_mlp_get_topk(const float *logits, int k, int *modes);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_INTRA_MODE_MLP_H_
