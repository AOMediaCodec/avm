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

#include "av1/encoder/part_split_prune_tflite.h"

#include <cstdio>
#include <memory>

#include "common/tf_lite_includes.h"

#include "av1/encoder/simple_intrapred_tflite_model_128x128.h"
#include "av1/encoder/simple_intrapred_tflite_model_16x16.h"
#include "av1/encoder/simple_intrapred_tflite_model_32x32.h"
#include "av1/encoder/simple_intrapred_tflite_model_64x64.h"
#include "av1/encoder/sms_part_split_prune_tflite_model.h"

struct Context {
  tflite::Model *model_128X128;
  tflite::Model *model_64X64;
  tflite::Model *model_32X32;
  tflite::Model *model_16X16;
  tflite::Model *model_inter;
  tflite::MutableOpResolver resolver;
};

extern "C" void *av2_part_split_prune_tflite_init() {
  Context *ctx = new Context();
  ctx->model_128X128 = (tflite::Model *)tflite::GetModel(
      a3_qp96_128_160_luma_BLOCK_128X128_intra_tflite);
  ctx->model_64X64 = (tflite::Model *)tflite::GetModel(
      a3_qp96_128_160_luma_BLOCK_64X64_intra_tflite);
  ctx->model_32X32 = (tflite::Model *)tflite::GetModel(
      a3_qp96_128_160_luma_BLOCK_32X32_intra_tflite);
  ctx->model_16X16 = (tflite::Model *)tflite::GetModel(
      a3_qp96_128_160_luma_BLOCK_16X16_intra_tflite);
  ctx->model_inter = (tflite::Model*)tflite::GetModel(
      sms_part_split_prune_tflite_model);
  RegisterSelectedOps(&ctx->resolver);
  return (void *)ctx;
}

extern "C" int av2_part_split_prune_tflite_params(
    MODEL_TYPE model_type, int prune_level, struct ModelParams *params) {
  switch (model_type) {
    case MODEL_128X128:
      *params =
          a3_qp96_128_160_luma_BLOCK_128X128_intra_tflite_params[prune_level];
      break;
    case MODEL_64X64:
      *params =
          a3_qp96_128_160_luma_BLOCK_64X64_intra_tflite_params[prune_level];
      break;
    case MODEL_32X32:
      *params =
          a3_qp96_128_160_luma_BLOCK_32X32_intra_tflite_params[prune_level];
      break;
    case MODEL_16X16:
      *params =
          a3_qp96_128_160_luma_BLOCK_16X16_intra_tflite_params[prune_level];
      break;
    case MODEL_INTER:
      *params = sms_part_split_prune_tflite_model_params[prune_level];
      break;
    default: return -1;
  }
  return 0;
}
// Simple intra ML TFLite based inference
extern "C" int av2_part_split_prune_tflite_exec(
    void *context, const float *ml_input, int input_len, float *ml_output,
    int output_len, MODEL_TYPE model_type) {
  // Build the interpreter.
  Context *ctx = (Context *)context;
  tflite::Model *model;
  switch (model_type) {
    case MODEL_128X128: model = ctx->model_128X128; break;
    case MODEL_64X64: model = ctx->model_64X64; break;
    case MODEL_32X32: model = ctx->model_32X32; break;
    case MODEL_16X16: model = ctx->model_16X16; break;
    case MODEL_INTER: model = ctx->model_inter; break;
    default: return -1;
  }
  tflite::InterpreterBuilder builder(model, ctx->resolver);

  std::unique_ptr<tflite::Interpreter> interpreter;
  builder(&interpreter);
  tflite::ErrorReporter *reporter(tflite::DefaultErrorReporter());

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    reporter->Report("Failed at allocating tensors");
    exit(1);
  }

  float *input = interpreter->typed_input_tensor<float>(0);
  for (int i = 0; i < input_len; i++) {
    input[i] = ml_input[i];
  }

  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at invoke");
    exit(1);
  }

  float *output = interpreter->typed_output_tensor<float>(0);
  for (int i = 0; i < output_len; i++) {
    ml_output[i] = output[i];
  }
  return 0;
}

extern "C" void av2_part_split_prune_tflite_close(void **context) {
  Context *ctx = (Context *)*context;
  delete ctx;
  context = nullptr;
}
