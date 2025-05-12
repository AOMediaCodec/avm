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
#include <mutex>

#include "common/tf_lite_includes.h"

#include "av1/encoder/simple_intrapred_tflite_model_128x128.h"
#include "av1/encoder/simple_intrapred_tflite_model_16x16.h"
#include "av1/encoder/simple_intrapred_tflite_model_32x32.h"
#include "av1/encoder/simple_intrapred_tflite_model_64x64.h"
#include "av1/encoder/sms_part_split_prune_tflite_model.h"

#if HAVE_FEXCEPT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fenv.h>
#endif

typedef std::unique_ptr<TfLiteDelegate, decltype(&TfLiteXNNPackDelegateDelete)>
    TfLiteDelegateType;

struct PartSplitContext {
  std::unique_ptr<tflite::Interpreter> model_128X128;
  std::unique_ptr<tflite::Interpreter> model_64X64;
  std::unique_ptr<tflite::Interpreter> model_32X32;
  std::unique_ptr<tflite::Interpreter> model_16X16;
  std::unique_ptr<tflite::Interpreter> model_inter_64x64;
  std::unique_ptr<tflite::Interpreter> model_inter_32x32;
  std::unique_ptr<tflite::Interpreter> model_inter_16x16;
  std::unique_ptr<tflite::Interpreter> model_inter_8x8;

  std::vector<TfLiteDelegateType> to_delete;
};

std::mutex tfliteMutex;

static std::unique_ptr<tflite::Interpreter> create_interpreter(
    unsigned char *model_def, std::vector<TfLiteDelegateType> &to_delete) {
  std::lock_guard<std::mutex> lock(tfliteMutex);
  tflite::Model *model = (tflite::Model *)tflite::GetModel(model_def);

  const int num_threads = 1;
  TfLiteXNNPackDelegateOptions xnnpack_options =
      TfLiteXNNPackDelegateOptionsDefault();
  xnnpack_options.num_threads = AOMMAX(num_threads, 1);
  TfLiteDelegateType xnnpack_delegate(
      TfLiteXNNPackDelegateCreate(&xnnpack_options),
      &TfLiteXNNPackDelegateDelete);

  tflite::MutableOpResolver resolver;
  RegisterSelectedOps(&resolver);

  tflite::InterpreterBuilder builder(model, resolver);
  tflite::ErrorReporter *reporter(tflite::DefaultErrorReporter());
  std::unique_ptr<tflite::Interpreter> interpreter;
  builder(&interpreter);
  if (interpreter->ModifyGraphWithDelegate(xnnpack_delegate.get()) !=
      kTfLiteOk) {
    reporter->Report("Failed at modifying graph with XNNPack delegate");
    exit(1);
  }

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    reporter->Report("Failed at allocating tensors");
    exit(1);
  }

  to_delete.push_back(std::move(xnnpack_delegate));
  return interpreter;
}

static void ensure_tflite_init(void **context, MODEL_TYPE model_type) {
  assert(model_type != MODEL_OTHER);

  if (*context == nullptr) *context = new PartSplitContext();
  PartSplitContext *ctx = (PartSplitContext *)*context;
  switch (model_type) {
    case MODEL_128X128:
      if (!ctx->model_128X128) {
        ctx->model_128X128 = create_interpreter(
            a3_qp96_128_160_luma_BLOCK_128X128_intra_tflite, ctx->to_delete);
      }
      break;
    case MODEL_64X64:
      if (!ctx->model_64X64) {
        ctx->model_64X64 = create_interpreter(
            a3_qp96_128_160_luma_BLOCK_64X64_intra_tflite, ctx->to_delete);
      }
      break;
    case MODEL_32X32:
      if (!ctx->model_32X32) {
        ctx->model_32X32 = create_interpreter(
            a3_qp96_128_160_luma_BLOCK_32X32_intra_tflite, ctx->to_delete);
      }
      break;
    case MODEL_16X16:
      if (!ctx->model_16X16) {
        ctx->model_16X16 = create_interpreter(
            a3_qp96_128_160_luma_BLOCK_16X16_intra_tflite, ctx->to_delete);
      }
      break;
    case MODEL_INTER_64X64:
      if (!ctx->model_inter_64x64) {
        ctx->model_inter_64x64 = create_interpreter(
            sms_part_split_prune_tflite_model_bs12, ctx->to_delete);
      }
      break;
    case MODEL_INTER_32X32:
      if (!ctx->model_inter_32x32) {
        ctx->model_inter_32x32 = create_interpreter(
            sms_part_split_prune_tflite_model_bs9, ctx->to_delete);
      }
      break;
    case MODEL_INTER_16X16:
      if (!ctx->model_inter_16x16) {
        ctx->model_inter_16x16 = create_interpreter(
            sms_part_split_prune_tflite_model_bs6, ctx->to_delete);
      }
      break;
    case MODEL_INTER_8X8:
      if (!ctx->model_inter_8x8) {
        ctx->model_inter_8x8 = create_interpreter(
            sms_part_split_prune_tflite_model_bs3, ctx->to_delete);
      }
      break;
    default: break;
  }
}

extern "C" int av2_part_split_prune_tflite_params(MODEL_TYPE model_type,
                                                  int prune_level,
                                                  struct ModelParams *params) {
  assert(model_type != MODEL_OTHER);
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
    case MODEL_INTER_64X64:
      *params = sms_part_split_prune_tflite_model_params_bs12[prune_level];
      break;
    case MODEL_INTER_32X32:
      *params = sms_part_split_prune_tflite_model_params_bs9[prune_level];
      break;
    case MODEL_INTER_16X16:
      *params = sms_part_split_prune_tflite_model_params_bs6[prune_level];
      break;
    case MODEL_INTER_8X8:
      *params = sms_part_split_prune_tflite_model_params_bs3[prune_level];
      break;
    default: return -1;
  }
  return 0;
}

#if HAVE_FEXCEPT && CONFIG_DEBUG
#define FLOATING_POINT_DISABLE_EXCEPTIONS \
  const int float_excepts = fedisableexcept(FE_UNDERFLOW | FE_OVERFLOW);
#define FLOATING_POINT_RESTORE_EXCEPTIONS feenableexcept(float_excepts);
#else
#define FLOATING_POINT_DISABLE_EXCEPTIONS
#define FLOATING_POINT_RESTORE_EXCEPTIONS
#endif  // HAVE_FEXCEPT && CONFIG_DEBUG

// Simple intra ML TFLite based inference

extern "C" int av2_part_split_prune_tflite_exec(void **context,
                                                const float *ml_input,
                                                int input_len, float *ml_output,
                                                int output_len,
                                                MODEL_TYPE model_type) {
  assert(model_type != MODEL_OTHER);

  ensure_tflite_init(context, model_type);
  PartSplitContext *ctx = (PartSplitContext *)*context;
  tflite::Interpreter *interpreter;
  switch (model_type) {
    case MODEL_128X128: interpreter = ctx->model_128X128.get(); break;
    case MODEL_64X64: interpreter = ctx->model_64X64.get(); break;
    case MODEL_32X32: interpreter = ctx->model_32X32.get(); break;
    case MODEL_16X16: interpreter = ctx->model_16X16.get(); break;
    case MODEL_INTER_64X64: interpreter = ctx->model_inter_64x64.get(); break;
    case MODEL_INTER_32X32: interpreter = ctx->model_inter_32x32.get(); break;
    case MODEL_INTER_16X16: interpreter = ctx->model_inter_16x16.get(); break;
    case MODEL_INTER_8X8: interpreter = ctx->model_inter_8x8.get(); break;
    default: return -1;
  }
  tflite::ErrorReporter *reporter(tflite::DefaultErrorReporter());

  float *input = interpreter->typed_input_tensor<float>(0);
  for (int i = 0; i < input_len; i++) {
    input[i] = ml_input[i];
  }

  FLOATING_POINT_DISABLE_EXCEPTIONS
  auto status = interpreter->Invoke();
  FLOATING_POINT_RESTORE_EXCEPTIONS

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
  PartSplitContext *ctx = (PartSplitContext *)*context;
  if (ctx != nullptr) delete ctx;
  *context = nullptr;
}
