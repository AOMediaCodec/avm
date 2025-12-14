/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AV2_ENCODER_INTRA_DIP_MODE_PRUNE_TFLITE_H
#define AV2_ENCODER_INTRA_DIP_MODE_PRUNE_TFLITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/common/av2_common_int.h"

#define DIP_PRUNING_NUM_INPUTS 5
#define DIP_PRUNING_MAX_INPUT_SIZE 64

typedef struct {
  const char *name;
  float values[DIP_PRUNING_MAX_INPUT_SIZE];
  int size;
  int orig_index;
  int tflite_index;
} dip_pruning_input;

typedef struct {
  dip_pruning_input inputs[DIP_PRUNING_NUM_INPUTS];
} dip_pruning_inputs;

static const float DIP_PRUNING_THRESHOLDS[6] = {
  0.588f, 0.462f, 0.562f, 0.5f, 0.525f, 0.05f,
};

static const int DIP_PRUNING_QPS[6] = { 85, 110, 135, 160, 185, 210 };

extern const uint8_t dip_pruning_tflite_qp85[58072];
extern const uint8_t dip_pruning_tflite_qp110[58072];
extern const uint8_t dip_pruning_tflite_qp135[58072];
extern const uint8_t dip_pruning_tflite_qp160[58072];
extern const uint8_t dip_pruning_tflite_qp185[58072];
extern const uint8_t dip_pruning_tflite_qp210[58072];

int intra_dip_mode_prune_tflite(void **context, float *output, int qp);

int intra_dip_mode_prune_get_model_index(int qp);

dip_pruning_inputs *intra_dip_mode_prune_get_inputs(void **context, int qp);

void intra_dip_mode_prune_normalize_and_resize_8x8(const uint16_t *input,
                                                   size_t stride, int bd,
                                                   size_t width, size_t height,
                                                   float *output);
void intra_dip_mode_prune_close(void **context);
#ifdef __cplusplus
}
#endif

#endif  // AV2_ENCODER_INTRA_DIP_MODE_PRUNE_TFLITE_H
