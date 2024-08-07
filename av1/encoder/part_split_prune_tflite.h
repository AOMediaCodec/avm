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

#ifndef AV1_ENCODER_SIMPLE_INTRAPRED_TFLITE_H_
#define AV1_ENCODER_SIMPLE_INTRAPRED_TFLITE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/common/av1_common_int.h"

#define DEFAULT_QP_LOW 85
#define DEFAULT_QP_HIGH 85

typedef enum {
  MODEL_OTHER = 0,
  MODEL_128X128,
  MODEL_64X64,
  MODEL_32X32,
  MODEL_16X16,
  MODEL_INTER_64X64,
  MODEL_INTER_32X32,
  MODEL_INTER_16X16,
  MODEL_INTER_8X8,
} MODEL_TYPE;

struct ModelParams {
  float thresh_low;
  float thresh_high;
  int qp_low;
  int qp_high;
};

int av2_part_split_prune_tflite_exec(void **context, const float *ml_input,
                                     int input_len, float *ml_output,
                                     int output_len, MODEL_TYPE model_type);
void av2_part_split_prune_tflite_close(void **context);
int av2_part_split_prune_tflite_params(MODEL_TYPE model_type, int prune_level,
                                       struct ModelParams *params);

#ifdef __cplusplus
}
#endif

#endif  // AV1_ENCODER_SIMPLE_INTRAPRED_TFLITE_H_
