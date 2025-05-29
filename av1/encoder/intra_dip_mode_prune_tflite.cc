#include "av1/encoder/intra_dip_mode_prune_tflite.h"

#include <cstdio>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>

#include "common/tf_lite_includes.h"

#if HAVE_FEXCEPT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fenv.h>
#endif

struct DipContext {
  std::unique_ptr<tflite::Interpreter> interpreter;
  std::unique_ptr<tflite::FlatBufferModel> model;
  dip_pruning_inputs dip_pruning_in;
  int model_index = -1;
};

std::mutex dip_prune_mutex;

static void create_interpreter(DipContext *context,
                               const unsigned char *model_def, int model_len) {
  std::lock_guard<std::mutex> lock(dip_prune_mutex);
  tflite::LoggerOptions::SetMinimumLogSeverity(tflite::TFLITE_LOG_ERROR);
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromBuffer((const char *)model_def,
                                               model_len);
  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
  if (tflite::InterpreterBuilder(*model, resolver)(&interpreter) != kTfLiteOk) {
    std::cerr << "Failed to build interpreter for DIP model." << std::endl;
    exit(1);
  }

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    std::cerr << "Failed to allocate tensors for DIP model." << std::endl;
    exit(1);
  }

  context->interpreter = std::move(interpreter);
  context->model = std::move(model);
}

static void ensure_tflite_init(void **context, int model_index) {
  if (*context == nullptr) {
    *context = new DipContext();
  }
  DipContext *ctx = (DipContext *)*context;
  if (!ctx->interpreter || model_index != ctx->model_index) {
    ctx->model_index = model_index;
    const uint8_t *model_bytes = NULL;
    int model_len = -1;

    switch (model_index) {
      case 0:
        model_bytes = dip_pruning_tflite_qp85;
        model_len = sizeof(dip_pruning_tflite_qp85) /
                    sizeof(dip_pruning_tflite_qp85[0]);
        break;
      case 1:
        model_bytes = dip_pruning_tflite_qp110;
        model_len = sizeof(dip_pruning_tflite_qp110) /
                    sizeof(dip_pruning_tflite_qp110[0]);
        break;
      case 2:
        model_bytes = dip_pruning_tflite_qp135;
        model_len = sizeof(dip_pruning_tflite_qp135) /
                    sizeof(dip_pruning_tflite_qp135[0]);
        break;
      case 3:
        model_bytes = dip_pruning_tflite_qp160;
        model_len = sizeof(dip_pruning_tflite_qp160) /
                    sizeof(dip_pruning_tflite_qp160[0]);
        break;
      case 4:
        model_bytes = dip_pruning_tflite_qp185;
        model_len = sizeof(dip_pruning_tflite_qp185) /
                    sizeof(dip_pruning_tflite_qp185[0]);
        break;
      case 5:
        model_bytes = dip_pruning_tflite_qp210;
        model_len = sizeof(dip_pruning_tflite_qp210) /
                    sizeof(dip_pruning_tflite_qp210[0]);
        break;
      default:
        fprintf(stderr, "Bad DIP pruning model index: %d\n", model_index);
        exit(1);
    }

    create_interpreter(ctx, (const unsigned char *)model_bytes, model_len);

    ctx->dip_pruning_in.inputs[0].name = "extra_features";
    ctx->dip_pruning_in.inputs[0].size = 19;
    ctx->dip_pruning_in.inputs[0].orig_index = 0;
    ctx->dip_pruning_in.inputs[0].tflite_index = 3;
    ctx->dip_pruning_in.inputs[1].name = "source_pixels";
    ctx->dip_pruning_in.inputs[1].size = 64;
    ctx->dip_pruning_in.inputs[1].orig_index = 1;
    ctx->dip_pruning_in.inputs[1].tflite_index = 0;
    ctx->dip_pruning_in.inputs[2].name = "dip_features";
    ctx->dip_pruning_in.inputs[2].size = 11;
    ctx->dip_pruning_in.inputs[2].orig_index = 2;
    ctx->dip_pruning_in.inputs[2].tflite_index = 1;
    ctx->dip_pruning_in.inputs[3].name = "block_size";
    ctx->dip_pruning_in.inputs[3].size = 2;
    ctx->dip_pruning_in.inputs[3].orig_index = 3;
    ctx->dip_pruning_in.inputs[3].tflite_index = 4;
    ctx->dip_pruning_in.inputs[4].name = "dip_model_rds";
    ctx->dip_pruning_in.inputs[4].size = 12;
    ctx->dip_pruning_in.inputs[4].orig_index = 4;
    ctx->dip_pruning_in.inputs[4].tflite_index = 2;
  }
}

#if HAVE_FEXCEPT && CONFIG_DEBUG
#define FLOATING_POINT_DISABLE_EXCEPTIONS \
  const int float_excepts = fedisableexcept(FE_UNDERFLOW | FE_OVERFLOW);
#define FLOATING_POINT_RESTORE_EXCEPTIONS feenableexcept(float_excepts);
#else
#define FLOATING_POINT_DISABLE_EXCEPTIONS
#define FLOATING_POINT_RESTORE_EXCEPTIONS
#endif  // HAVE_FEXCEPT && CONFIG_DEBUG

static std::vector<float> run_inference(void **context) {
  DipContext *ctx = (DipContext *)*context;
  tflite::Interpreter *interpreter = ctx->interpreter.get();
  for (int i = 0; i < DIP_PRUNING_NUM_INPUTS; i++) {
    int tflite_index = ctx->dip_pruning_in.inputs[i].tflite_index;
    float *tensor_input = interpreter->typed_input_tensor<float>(tflite_index);
    memcpy(tensor_input, ctx->dip_pruning_in.inputs[i].values,
           ctx->dip_pruning_in.inputs[i].size * sizeof(float));
  }

  FLOATING_POINT_DISABLE_EXCEPTIONS
  if (interpreter->Invoke() != kTfLiteOk) {
    std::cerr << "Failed to run DIP pruning inference." << std::endl;
    exit(1);
  }
  FLOATING_POINT_RESTORE_EXCEPTIONS

  float *output = interpreter->typed_output_tensor<float>(0);
  size_t output_size =
      interpreter->tensor(interpreter->outputs()[0])->bytes / sizeof(float);
  std::vector<float> output_data(output, output + output_size);

  return output_data;
}

extern "C" int intra_dip_mode_prune_tflite(void **context, float *output,
                                           int qp) {
  ensure_tflite_init(context, intra_dip_mode_prune_get_model_index(qp));
  auto output_vec = run_inference(context);
  memcpy(output, output_vec.data(), 1 * sizeof(float));
  return 0;
}

extern "C" int intra_dip_mode_prune_get_model_index(int qp) {
  int closest_index = 0;
  int closest_delta = 10000;
  for (int i = 0; i < 6; i++) {
    int delta = abs(qp - DIP_PRUNING_QPS[i]);
    if (delta < closest_delta) {
      closest_delta = delta;
      closest_index = i;
    }
  }
  // Don't run pruning for QP>=210 (index 5).
  // TODO(comc): Re-train QP=210 model.
  if (closest_index == 5) {
    return -1;
  }
  return closest_index;
}

extern "C" dip_pruning_inputs *intra_dip_mode_prune_get_inputs(void **context,
                                                               int qp) {
  ensure_tflite_init(context, intra_dip_mode_prune_get_model_index(qp));
  DipContext *ctx = (DipContext *)*context;
  return &(ctx->dip_pruning_in);
}

extern "C" void intra_dip_mode_prune_normalize_and_resize_8x8(
    const uint16_t *input, size_t stride, int bd, size_t width, size_t height,
    float *output) {
  const size_t x_step = width / 8;
  const size_t y_step = height / 8;
  const float norm = (float)((1 << bd) - 1);
  for (size_t out_y = 0; out_y < 8; out_y++) {
    const size_t in_y = out_y * y_step;
    for (size_t out_x = 0; out_x < 8; out_x++) {
      const size_t in_x = out_x * x_step;
      const size_t in_index = in_y * stride + in_x;
      const size_t out_index = out_y * 8 + out_x;
      output[out_index] = (float)input[in_index] / norm;
    }
  }
}

extern "C" void intra_dip_mode_prune_close(void **context) {
  DipContext *ctx = (DipContext *)*context;
  if (ctx != nullptr) delete ctx;
  *context = nullptr;
}
