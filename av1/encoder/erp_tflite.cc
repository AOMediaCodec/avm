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

#include "config/av1_rtcd.h"

#include "av1/encoder/erp_tflite.h"
#include "av1/encoder/erp_models.h"
#include "av1/encoder/ml.h"

#if CONFIG_ERP_TFLITE
#include <vector>
#include "av1/tflite_models/op_registrations.h"
#include "common/tf_lite_includes.h"
#endif  // CONFIG_ERP_TFLITE

#if CONFIG_EXT_RECUR_PARTITIONS
#define MAKE_ERP_MODEL_SWITCH_CASE(bsize)           \
  case bsize:                                       \
    return is_hd ? av1_erp_rect_hd_##bsize##_tflite \
                 : av1_erp_rect_##bsize##_tflite;

#define MAKE_ERP_DNN_MODEL_SWITCH_CASE(bsize)         \
  case bsize:                                         \
    return is_hd ? &av1_erp_rect_hd_nn_config_##bsize \
                 : &av1_erp_rect_nn_config_##bsize;

#define MAKE_ERP_MEAN_SWITCH_CASE(bsize)                \
  case bsize:                                           \
    return is_hd ? av1_erp_rect_hd_feature_mean_##bsize \
                 : av1_erp_rect_feature_mean_##bsize;

#define MAKE_ERP_STD_SWITCH_CASE(bsize)                \
  case bsize:                                          \
    return is_hd ? av1_erp_rect_hd_feature_std_##bsize \
                 : av1_erp_rect_feature_std_##bsize;

#if CONFIG_ERP_TFLITE
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
#else
static const NN_CONFIG *get_dnn_model(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}
#endif  // CONFIG_ERP_TFLITE

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
#if CONFIG_ERP_TFLITE
  std::unique_ptr<tflite::Interpreter> interpreter =
      get_tflite_interpreter(bsize, is_hd);

  // Prepare input.
  float *input = interpreter->typed_input_tensor<float>(0);
  const float *mean = get_mean(bsize, is_hd);
  const float *std = get_std(bsize, is_hd);
  normalize(input, features, mean, std, 19);

  // Invoke TFlite inference.
  const float *output;
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }
  output = interpreter->typed_output_tensor<float>(0);
  interpreter.reset();
#else
  // Prepare input.
  float input[19];
  const float *mean = get_mean(bsize, is_hd);
  const float *std = get_std(bsize, is_hd);
  normalize(input, features, mean, std, 19);

  // Call nn config
  float output[3];
  const NN_CONFIG *nn_config = get_dnn_model(bsize, is_hd);
  av1_nn_predict(input, nn_config, 1, output);
#endif  // CONFIG_ERP_TFLITE

  float probs[3];
  av1_nn_softmax(output, probs, 3);

  static const float threshes[2][5] = {
    // Non-hd
    {
        // 128, 64, 32, 16, 8
        0.00889f,
        0.00268f,
        0.01480f,
        0.03531f,
        0.04103f,
    },
    // HD
    {
        // 128, 64, 32, 16, 8
        0.01911f,
        0.00327f,
        0.00520f,
        0.01669f,
        0.00176f,
    },
  };

  float thresh = 0.0f;
  switch (bsize) {
    case BLOCK_128X128:
    case BLOCK_128X64:
    case BLOCK_64X128: thresh = threshes[is_hd][0]; break;
    case BLOCK_64X64:
    case BLOCK_64X32:
    case BLOCK_32X64: thresh = threshes[is_hd][1]; break;
    case BLOCK_32X32:
    case BLOCK_32X16:
    case BLOCK_16X32: thresh = threshes[is_hd][2]; break;
    case BLOCK_16X16:
    case BLOCK_16X8:
    case BLOCK_8X16: thresh = threshes[is_hd][3]; break;
    case BLOCK_8X8: thresh = threshes[is_hd][4]; break;
    default:
      assert("Unexpected block size in erp pruning model!\n");
      thresh = 0.0f;
  }

  if (probs[1] < thresh) {
    *prune_horz = true;
  }
  if (probs[2] < thresh) {
    *prune_vert = true;
  }

  return 1;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS
