/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <vector>

#include "av1/common/av1_common_int.h"
#include "av1/tflite_models/op_registrations.h"

#include "av1/encoder/erp_tflite.h"
#include "av1/encoder/erp_models.h"
#include "av1/encoder/ml.h"

#include "common/tf_lite_includes.h"

#if CONFIG_EXT_RECUR_PARTITIONS
#define MAKE_ERP_MODEL_SWITCH_CASE(bsize)           \
  case bsize:                                       \
    return is_hd ? av1_erp_rect_hd_##bsize##_tflite \
                 : av1_erp_rect_##bsize##_tflite;

#define MAKE_ERP_MEAN_SWITCH_CASE(bsize)                \
  case bsize:                                           \
    return is_hd ? av1_erp_rect_hd_feature_mean_##bsize \
                 : av1_erp_rect_feature_mean_##bsize;

#define MAKE_ERP_STD_SWITCH_CASE(bsize)                \
  case bsize:                                          \
    return is_hd ? av1_erp_rect_hd_feature_std_##bsize \
                 : av1_erp_rect_feature_std_##bsize;

static const unsigned char *get_model_data(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_MODEL_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}

static const float *get_mean(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}

static const float *get_std(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}
#undef MAKE_ERP_MODEL_SWITCH_CASE

static std::unique_ptr<tflite::Interpreter> get_tflite_interpreter(
    BLOCK_SIZE bsize, bool is_hd) {
  const unsigned char *const model_tflite_data = get_model_data(bsize, is_hd);
  auto model = tflite::GetModel(model_tflite_data);
  tflite::MutableOpResolver resolver;
  RegisterSelectedOpsAllQps(&resolver);
  tflite::InterpreterBuilder builder(model, resolver);
  std::unique_ptr<tflite::Interpreter> interpreter;
  builder(&interpreter);
  interpreter->SetNumThreads(1);
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();

  // Dimension order: batch_size, feature_size
  const std::vector<int> in_out_dims = { 1, 19 };

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    reporter->Report("Failed at tensor allocation");
    return nullptr;
  }

  return interpreter;
}

static inline void normalize(float *features_dst, const float *features_src,
                             const float *mean, const float *std,
                             size_t num_features) {
#define EPSILON 0.00001f
  for (size_t idx = 0; idx < num_features; idx++) {
    if (std[idx] <= EPSILON) {
      // Low variance. Assumes a constant
      features_dst[idx] = 0.0f;
    } else {
      features_dst[idx] = (features_src[idx] - mean[idx]) / std[idx];
    }
  }
#undef EPSILON
}

extern "C" int av1_erp_prune_rect(BLOCK_SIZE bsize, bool is_hd,
                                  const float *features, bool *prune_horz,
                                  bool *prune_vert) {
  std::unique_ptr<tflite::Interpreter> interpreter =
      get_tflite_interpreter(bsize, is_hd);

  // Prepare input.
  float *input = interpreter->typed_input_tensor<float>(0);
  const float *mean = get_mean(bsize, is_hd);
  const float *std = get_std(bsize, is_hd);
  normalize(input, features, mean, std, 19);

  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }

  const float *output = interpreter->typed_output_tensor<float>(0);
  float probs[3];
  av1_nn_softmax(output, probs, 3);

  if (probs[1] < 0.05f) {
    *prune_horz = true;
  }
  if (probs[2] < 0.05f) {
    *prune_vert = true;
  }

  interpreter.reset();

  return 1;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS
