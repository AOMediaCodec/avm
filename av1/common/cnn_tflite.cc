/*
 * Copyright (c) 2020, Alliance for Open Media. All rights reserved
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
#include "av1/common/cnn_tflite.h"
#include "av1/tflite_models/op_registrations.h"
#include "av1/tflite_models/intra_frame_model/uv_qp0_90.h"
#include "av1/tflite_models/intra_frame_model/uv_qp91_120.h"
#include "av1/tflite_models/intra_frame_model/uv_qp121_145.h"
#include "av1/tflite_models/intra_frame_model/uv_qp146_175.h"
#include "av1/tflite_models/intra_frame_model/uv_qp176_205.h"
#include "av1/tflite_models/intra_frame_model/uv_qp206_255.h"
#include "av1/tflite_models/intra_frame_model/qp0_90.h"
#include "av1/tflite_models/intra_frame_model/qp91_120.h"
#include "av1/tflite_models/intra_frame_model/qp121_145.h"
#include "av1/tflite_models/intra_frame_model/qp146_175.h"
#include "av1/tflite_models/intra_frame_model/qp176_205.h"
#include "av1/tflite_models/intra_frame_model/qp206_255.h"
#include "av1/tflite_models/inter_frame_model/uv_qp0_90.h"
#include "av1/tflite_models/inter_frame_model/uv_qp91_120.h"
#include "av1/tflite_models/inter_frame_model/uv_qp121_145.h"
#include "av1/tflite_models/inter_frame_model/uv_qp146_175.h"
#include "av1/tflite_models/inter_frame_model/uv_qp176_205.h"
#include "av1/tflite_models/inter_frame_model/uv_qp206_255.h"
#include "av1/tflite_models/inter_frame_model/qp0_90.h"
#include "av1/tflite_models/inter_frame_model/qp91_120.h"
#include "av1/tflite_models/inter_frame_model/qp121_145.h"
#include "av1/tflite_models/inter_frame_model/qp146_175.h"
#include "av1/tflite_models/inter_frame_model/qp176_205.h"
#include "av1/tflite_models/inter_frame_model/qp206_255.h"
#if CONFIG_EXT_SUPERRES
#include "av1/tflite_models/inter_frame_model/sr5by4ra_1_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr5by4ra_2_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr5by4ra_3_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr3by2ra_1_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr3by2ra_2_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr3by2ra_3_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr7by4ra_1_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr7by4ra_2_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr7by4ra_3_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr2by1ra_1_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr2by1ra_2_tflite.h"
#include "av1/tflite_models/inter_frame_model/sr2by1ra_3_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr5by4ai_1_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr5by4ai_2_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr5by4ai_3_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr3by2ai_1_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr3by2ai_2_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr3by2ai_3_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr7by4ai_1_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr7by4ai_2_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr7by4ai_3_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr2by1ai_1_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr2by1ai_2_tflite.h"
#include "av1/tflite_models/intra_frame_model/sr2by1ai_3_tflite.h"
#endif  // CONFIG_EXT_SUPERRES

#if CONFIG_CNN_GUIDED_QUADTREE
#include "av1/tflite_models/inter_frame_model/qp0_90_quadtree.h"
#include "av1/tflite_models/inter_frame_model/qp91_120_quadtree.h"
#include "av1/tflite_models/inter_frame_model/qp121_145_quadtree.h"
#include "av1/tflite_models/inter_frame_model/qp146_175_quadtree.h"
#include "av1/tflite_models/inter_frame_model/qp176_205_quadtree.h"
#include "av1/tflite_models/inter_frame_model/qp206_255_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp0_90_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp91_120_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp121_145_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp146_175_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp176_205_quadtree.h"
#include "av1/tflite_models/intra_frame_model/qp206_255_quadtree.h"
#endif

#include "common/tf_lite_includes.h"

#if CONFIG_CNN_RESTORATION

#define USE_XNNPACK 0

// Returns the TF-lite model based on the qindex.
static const unsigned char *get_intra_model_from_qindex(int qindex,
                                                        int superres_denom,
                                                        int is_luma,
                                                        int cnn_index) {
  if (qindex <= MIN_CNN_Q_INDEX) {
    assert(0);
    return nullptr;
  }
#if CONFIG_EXT_SUPERRES
  assert(superres_denom == SCALE_NUMERATOR || superres_denom == 10 ||
         superres_denom == 12 || superres_denom == 14 || superres_denom == 16);
#else
  assert(superres_denom == SCALE_NUMERATOR);
#endif  // CONFIG_EXT_SUPERRES
#if CONFIG_CNN_GUIDED_QUADTREE
  if (superres_denom == SCALE_NUMERATOR) {  // quadtree
    if (is_luma) {
      if (qindex <= 90) {
        return (cnn_index == 0)   ? qp0_90_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp91_120_quadtree_model_tflite_data
                                  : qp121_145_quadtree_model_tflite_data;
      } else if (qindex <= 120) {
        return (cnn_index == 0)   ? qp91_120_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp0_90_quadtree_model_tflite_data
                                  : qp121_145_quadtree_model_tflite_data;
      } else if (qindex <= 145) {
        return (cnn_index == 0)   ? qp121_145_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp91_120_quadtree_model_tflite_data
                                  : qp146_175_quadtree_model_tflite_data;
      } else if (qindex <= 175) {
        return (cnn_index == 0)   ? qp146_175_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp121_145_quadtree_model_tflite_data
                                  : qp176_205_quadtree_model_tflite_data;
      } else if (qindex <= 205) {
        return (cnn_index == 0)   ? qp176_205_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp146_175_quadtree_model_tflite_data
                                  : qp206_255_quadtree_model_tflite_data;
      } else {
        return (cnn_index == 0)   ? qp206_255_quadtree_model_tflite_data
               : (cnn_index == 1) ? qp176_205_quadtree_model_tflite_data
                                  : qp146_175_quadtree_model_tflite_data;
      }
    }
  }
#endif  // CONFIG_CNN_GUIDED_QUADTREE

  if (superres_denom == SCALE_NUMERATOR) {
    if (is_luma) {
      if (qindex < 91) {
        return (cnn_index == 0)   ? qp0_90_model_tflite_data
               : (cnn_index == 1) ? qp91_120_model_tflite_data
                                  : qp121_145_model_tflite_data;
      } else if (qindex < 121) {
        return (cnn_index == 0)   ? qp91_120_model_tflite_data
               : (cnn_index == 1) ? qp0_90_model_tflite_data
                                  : qp121_145_model_tflite_data;
      } else if (qindex < 146) {
        return (cnn_index == 0)   ? qp121_145_model_tflite_data
               : (cnn_index == 1) ? qp91_120_model_tflite_data
                                  : qp146_175_model_tflite_data;
      } else if (qindex < 176) {
        return (cnn_index == 0)   ? qp146_175_model_tflite_data
               : (cnn_index == 1) ? qp121_145_model_tflite_data
                                  : qp176_205_model_tflite_data;
      } else if (qindex < 206) {
        return (cnn_index == 0)   ? qp176_205_model_tflite_data
               : (cnn_index == 1) ? qp146_175_model_tflite_data
                                  : qp206_255_model_tflite_data;
      } else {
        return (cnn_index == 0)   ? qp206_255_model_tflite_data
               : (cnn_index == 1) ? qp176_205_model_tflite_data
                                  : qp146_175_model_tflite_data;
      }
    } else {
      assert(cnn_index == 0);
      if (qindex < 91) {
        return uv_qp0_90_model_tflite_data;
      } else if (qindex < 121) {
        return uv_qp91_120_model_tflite_data;
      } else if (qindex < 146) {
        return uv_qp121_145_model_tflite_data;
      } else if (qindex < 176) {
        return uv_qp146_175_model_tflite_data;
      } else if (qindex < 206) {
        return uv_qp176_205_model_tflite_data;
      } else {
        return uv_qp206_255_model_tflite_data;
      }
    }
  }
#if CONFIG_EXT_SUPERRES
  assert(is_luma);
#if SELECT_CNN_FOR_SUPERRES
  switch (superres_denom) {
    case 10:
      return (cnn_index == 0)   ? sr5by4ai_1_tflite
             : (cnn_index == 1) ? sr5by4ai_2_tflite
                                : sr5by4ai_3_tflite;
    case 12:
      return (cnn_index == 0)   ? sr3by2ai_1_tflite
             : (cnn_index == 1) ? sr3by2ai_2_tflite
                                : sr3by2ai_3_tflite;
    case 14:
      return (cnn_index == 0)   ? sr7by4ai_1_tflite
             : (cnn_index == 1) ? sr7by4ai_2_tflite
                                : sr7by4ai_3_tflite;
    case 16:
      return (cnn_index == 0)   ? sr2by1ai_1_tflite
             : (cnn_index == 1) ? sr2by1ai_2_tflite
                                : sr2by1ai_3_tflite;
    default: assert(0); return nullptr;
  }
#else   // SELECT_CNN_FOR_SUPERRES
  switch (superres_denom) {
    case 10:
      if (qindex < 120)
        return sr5by4ai_1_tflite;
      else if (qindex < 180)
        return sr5by4ai_2_tflite;
      else
        return sr5by4ai_3_tflite;
    case 12:
      if (qindex < 120)
        return sr3by2ai_1_tflite;
      else if (qindex < 180)
        return sr3by2ai_2_tflite;
      else
        return sr3by2ai_3_tflite;
    case 14:
      if (qindex < 120)
        return sr7by4ai_1_tflite;
      else if (qindex < 180)
        return sr7by4ai_2_tflite;
      else
        return sr7by4ai_3_tflite;
    case 16:
      if (qindex < 120)
        return sr2by1ai_1_tflite;
      else if (qindex < 180)
        return sr2by1ai_2_tflite;
      else
        return sr2by1ai_3_tflite;
    default: assert(0); return nullptr;
  }
#endif  // SELECT_CNN_FOR_SUPERRES
#endif  // CONFIG_EXT_SUPERRES
  return nullptr;
}

// Returns the TF-lite model based on the qindex.
static const unsigned char *get_inter_model_from_qindex(int qindex,
                                                        int superres_denom,
                                                        int is_luma,
                                                        int cnn_index) {
  if (qindex <= MIN_CNN_Q_INDEX) {
    assert(0);
    return nullptr;
  }
#if CONFIG_EXT_SUPERRES
  assert(superres_denom == SCALE_NUMERATOR || superres_denom == 10 ||
         superres_denom == 12 || superres_denom == 14 || superres_denom == 16);
#else
  assert(superres_denom == SCALE_NUMERATOR);
#endif  // CONFIG_EXT_SUPERRES
#if CONFIG_CNN_GUIDED_QUADTREE
  if (superres_denom == SCALE_NUMERATOR) {  // quadtree
    if (is_luma) {
      if (qindex <= 90) {
        return (cnn_index == 0)   ? qp0_90_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp91_120_quadtree_inter_model_tflite_data
                                  : qp121_145_quadtree_inter_model_tflite_data;
      } else if (qindex <= 120) {
        return (cnn_index == 0)   ? qp91_120_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp0_90_quadtree_inter_model_tflite_data
                                  : qp121_145_quadtree_inter_model_tflite_data;
      } else if (qindex <= 145) {
        return (cnn_index == 0)   ? qp121_145_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp91_120_quadtree_inter_model_tflite_data
                                  : qp146_175_quadtree_inter_model_tflite_data;
      } else if (qindex <= 175) {
        return (cnn_index == 0)   ? qp146_175_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp121_145_quadtree_inter_model_tflite_data
                                  : qp176_205_quadtree_inter_model_tflite_data;
      } else if (qindex <= 205) {
        return (cnn_index == 0)   ? qp176_205_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp146_175_quadtree_inter_model_tflite_data
                                  : qp206_255_quadtree_inter_model_tflite_data;
      } else {
        return (cnn_index == 0)   ? qp206_255_quadtree_inter_model_tflite_data
               : (cnn_index == 1) ? qp176_205_quadtree_inter_model_tflite_data
                                  : qp146_175_quadtree_inter_model_tflite_data;
      }
    }
  }
#endif  // CONFIG_CNN_GUIDED_QUADTREE

  if (superres_denom == SCALE_NUMERATOR) {
    if (is_luma) {
      if (qindex < 91) {
        return (cnn_index == 0)   ? qp0_90_inter_model_tflite_data
               : (cnn_index == 1) ? qp91_120_inter_model_tflite_data
                                  : qp121_145_inter_model_tflite_data;
      } else if (qindex < 121) {
        return (cnn_index == 0)   ? qp91_120_inter_model_tflite_data
               : (cnn_index == 1) ? qp0_90_inter_model_tflite_data
                                  : qp121_145_inter_model_tflite_data;
      } else if (qindex < 146) {
        return (cnn_index == 0)   ? qp121_145_inter_model_tflite_data
               : (cnn_index == 1) ? qp91_120_inter_model_tflite_data
                                  : qp146_175_inter_model_tflite_data;
      } else if (qindex < 176) {
        return (cnn_index == 0)   ? qp146_175_inter_model_tflite_data
               : (cnn_index == 1) ? qp121_145_inter_model_tflite_data
                                  : qp176_205_inter_model_tflite_data;
      } else if (qindex < 206) {
        return (cnn_index == 0)   ? qp176_205_inter_model_tflite_data
               : (cnn_index == 1) ? qp146_175_inter_model_tflite_data
                                  : qp206_255_inter_model_tflite_data;
      } else {
        return (cnn_index == 0)   ? qp206_255_inter_model_tflite_data
               : (cnn_index == 1) ? qp176_205_inter_model_tflite_data
                                  : qp146_175_inter_model_tflite_data;
      }
    } else {
      assert(cnn_index == 0);
      if (qindex < 91) {
        return uv_qp0_90_inter_model_tflite_data;
      } else if (qindex < 121) {
        return uv_qp91_120_inter_model_tflite_data;
      } else if (qindex < 146) {
        return uv_qp121_145_inter_model_tflite_data;
      } else if (qindex < 176) {
        return uv_qp146_175_inter_model_tflite_data;
      } else if (qindex < 206) {
        return uv_qp176_205_inter_model_tflite_data;
      } else {
        return uv_qp206_255_inter_model_tflite_data;
      }
    }
  }

#if CONFIG_EXT_SUPERRES
  assert(is_luma);
  switch (superres_denom) {
    case 10:
      if (qindex < 120)
        return sr5by4ra_1_tflite;
      else if (qindex < 180)
        return sr5by4ra_2_tflite;
      else
        return sr5by4ra_3_tflite;
    case 12:
      if (qindex < 120)
        return sr3by2ra_1_tflite;
      else if (qindex < 180)
        return sr3by2ra_2_tflite;
      else
        return sr3by2ra_3_tflite;
    case 14:
      if (qindex < 120)
        return sr7by4ra_1_tflite;
      else if (qindex < 180)
        return sr7by4ra_2_tflite;
      else
        return sr7by4ra_3_tflite;
    case 16:
      if (qindex < 120)
        return sr2by1ra_1_tflite;
      else if (qindex < 180)
        return sr2by1ra_2_tflite;
      else
        return sr2by1ra_3_tflite;
    default: assert(0); return nullptr;
  }
#endif  // CONFIG_EXT_SUPERRES
  return nullptr;
}

#if USE_XNNPACK
static TfLiteDelegate *get_tflite_xnnpack_delegate(int num_threads) {
  TfLiteXNNPackDelegateOptions xnnpack_options =
      TfLiteXNNPackDelegateOptionsDefault();
  xnnpack_options.num_threads = AOMMAX(num_threads, 1);
  return TfLiteXNNPackDelegateCreate(&xnnpack_options);
}
#endif  // USE_XNNPACK

// Builds and returns the TFlite interpreter.
static std::unique_ptr<tflite::Interpreter> get_tflite_interpreter(
    int qindex, int superres_denom, int width, int height, int num_threads,
    int is_intra_only, int is_luma, int cnn_index
#if USE_XNNPACK
    ,
    TfLiteDelegate *xnnpack_delegate
#endif  // USE_XNNPACK
) {
  const unsigned char *const model_tflite_data =
      is_intra_only ? get_intra_model_from_qindex(qindex, superres_denom,
                                                  is_luma, cnn_index)
                    : get_inter_model_from_qindex(qindex, superres_denom,
                                                  is_luma, cnn_index);
  auto model = tflite::GetModel(model_tflite_data);
  tflite::MutableOpResolver resolver;
  RegisterSelectedOpsAllQps(&resolver);
  tflite::InterpreterBuilder builder(model, resolver);
  // TODO(urvang): Investigate if caching the interpreter object provides
  // further speed-up. May still have to re-build the interpreter if qindex
  // changes.
  std::unique_ptr<tflite::Interpreter> interpreter;
  builder(&interpreter);
  interpreter->SetNumThreads(AOMMAX(num_threads, 1));
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();

  // Dimension order: batch_size, height, width, num_channels.
  // Note: height comes before width here!
  const std::vector<int> in_out_dims = { 1, height, width, 1 };
  // We only need to resize the input tensor. All other tensors (including
  // output tensor) will be resized automatically.
  if (interpreter->ResizeInputTensor(interpreter->inputs()[0], in_out_dims) !=
      kTfLiteOk) {
    reporter->Report("Failed at input tensor resize");
    return nullptr;
  }

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    reporter->Report("Failed at tensor allocation");
    return nullptr;
  }

#if USE_XNNPACK
  if (interpreter->ModifyGraphWithDelegate(xnnpack_delegate) != kTfLiteOk) {
    reporter->Report("Failed at modifying graph with XNNPack delegate");
    return nullptr;
  }
#endif  // USE_XNNPACK

  return interpreter;
}

extern "C" int av1_restore_cnn_img_tflite_highbd(
    int qindex, int superres_denom, const uint16_t *dgd, int width, int height,
    int dgd_stride, uint16_t *rst, int rst_stride, int num_threads,
    int bit_depth, int is_intra_only, int is_luma, int cnn_index) {
  // Ensure image can be downscaled by factor of 8 on each axis
  int padding_width = int(ceil(float(width) / 8.0) * 8);
  int padding_height = int(ceil(float(height) / 8.0) * 8);
#if USE_XNNPACK
  TfLiteDelegate *xnnpack_delegate = get_tflite_xnnpack_delegate(num_threads);
#endif  // USE_XNNPACK
  std::unique_ptr<tflite::Interpreter> interpreter = get_tflite_interpreter(
      qindex, superres_denom, padding_width, padding_height, num_threads,
      is_intra_only, is_luma, cnn_index
#if USE_XNNPACK
      ,
      xnnpack_delegate
#endif  // USE_XNNPACK
  );

  // Prepare input.
  const auto max_val = static_cast<float>((1 << bit_depth) - 1);
  const int in_stride = padding_width;
  auto input = interpreter->typed_input_tensor<float>(0);
  for (int r = 0; r < padding_height; ++r) {
    for (int c = 0; c < padding_width; ++c) {
      if (r < height && c < width) {
        input[r * in_stride + c] =
            static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
        assert(input[r * in_stride + c] >= 0.0f);
        assert(input[r * in_stride + c] <= 1.0f);
      } else {
        // Padding with either zeros or by copies
        // input[r * in_stride + c] = 0;  // Pad with zeros
        int w_copy_idx = c;
        if (c >= width) {
          w_copy_idx = width + (width - c) - 1;
        }
        int h_copy_idx = r;
        if (r >= height) {
          h_copy_idx = height + (height - r) - 1;
        }
        input[r * in_stride + c] = input[h_copy_idx * in_stride + w_copy_idx];
      }
    }
  }

  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }

  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = width;
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int residue =
          static_cast<int>(output[r * out_stride + c] * max_val + 0.5);
      rst[r * rst_stride + c] =
          clip_pixel_highbd(dgd[r * dgd_stride + c] + residue, bit_depth);
    }
  }

  interpreter.reset();

#if USE_XNNPACK
  // IMPORTANT: release the interpreter before destroying the delegate.
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK

  return 1;
}

extern "C" void av1_restore_cnn_tflite(const AV1_COMMON *cm, int num_threads,
                                       const int apply_cnn[MAX_MB_PLANE],
                                       const int cnn_indices[MAX_MB_PLANE]) {
  YV12_BUFFER_CONFIG *buf = &cm->cur_frame->buf;
  const int is_intra_only = frame_is_intra_only(cm);
  for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
    if (!apply_cnn[plane]) continue;
    const int is_luma = (plane == AOM_PLANE_Y);
    const int cnn_index = cnn_indices[plane];
    assert(cnn_index >= 0 &&
           cnn_index < av1_num_cnn_indices_for_plane(cm, plane));
    switch (plane) {
      case AOM_PLANE_Y:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->y_buffer), buf->y_crop_width,
            buf->y_crop_height, buf->y_stride,
            CONVERT_TO_SHORTPTR(buf->y_buffer), buf->y_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      case AOM_PLANE_U:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      case AOM_PLANE_V:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      default: assert(0 && "Invalid plane index");
    }
  }
}

#if CONFIG_CNN_GUIDED_QUADTREE

void deTree_tflite(QUADInfo *quad_info, int *Aindex, int *A, int *Sindex,
                   int *Split) {
  int flag0 = quad_info->split_info[quad_info->split_info_index].split;
  quad_info->split_info_index++;
  int flag1 = quad_info->split_info[quad_info->split_info_index].split;
  quad_info->split_info_index++;
  if (flag0 == 0 && flag1 == 1) {
    *(Split + (*Sindex)) = 1;
    (*Sindex)++;

    int a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    int a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

    a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

    a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

    a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

  } else if (flag0 == 1 && flag1 == 1) {
    *(Split + (*Sindex)) = 3;
    (*Sindex)++;

    int a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    int a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

    a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

  } else if (flag0 == 1 && flag1 == 0) {
    *(Split + (*Sindex)) = 2;
    (*Sindex)++;

    int a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    int a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

    a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;

  } else if (flag0 == 0 && flag1 == 0) {
    *(Split + (*Sindex)) = 0;
    (*Sindex)++;

    int a0 = quad_info->unit_info[quad_info->unit_info_index].xqd[0];
    int a1 = quad_info->unit_info[quad_info->unit_info_index].xqd[1];
    quad_info->unit_info_index++;
    A[(*Aindex)++] = a0;
    A[(*Aindex)++] = a1;
  }
}

extern "C" int TFlite_Predict_quadtree_hbd(
    uint16_t *dgd, uint16_t *src, int *A, int A_size, int height, int width,
    int dgd_stride, int src_stride, int QP, int unit_width, int unit_height,
    uint16_t *rePic, int rec_stride, int superres_denom, int num_threads,
    int bit_depth, int is_intra_only, int is_luma, int cnn_index) {
  // makesure can downscale 3 times
  int padding_width = ceil(width * 1.0 / 8) * 8;
  int padding_height = ceil(height * 1.0 / 8) * 8;
#if USE_XNNPACK
  TfLiteDelegate *xnnpack_delegate = get_tflite_xnnpack_delegate(num_threads);
#endif  // USE_XNNPACK
  std::unique_ptr<tflite::Interpreter> interpreter =
      get_tflite_interpreter(QP, superres_denom, padding_width, padding_height,
                             num_threads, is_intra_only, is_luma, cnn_index
#if USE_XNNPACK
                             ,
                             xnnpack_delegate
#endif  // USE_XNNPACK
      );

  // Prepare input.
  const auto max_val = static_cast<float>((1 << bit_depth) - 1);
  const int in_stride = padding_width;
  auto input = interpreter->typed_input_tensor<float>(0);
  // for (int r = 0; r < height; ++r) {
  //  for (int c = 0; c < width; ++c) {
  //    input[r * in_stride + c] =
  //        static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
  //    assert(input[r * in_stride + c] >= 0.0f);
  //    assert(input[r * in_stride + c] <= 1.0f);
  //  }
  //}
  for (int r = 0; r < padding_height; ++r) {
    for (int c = 0; c < padding_width; ++c) {
      if (r < height && c < width) {
        input[r * in_stride + c] =
            static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
        assert(input[r * in_stride + c] >= 0.0f);
        assert(input[r * in_stride + c] <= 1.0f);
      } else {
        input[r * in_stride + c] = 0;
      }
    }
  }

  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }
  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = padding_width;

  uint16_t **sub_dgr = new uint16_t *[height];
  for (int i = 0; i < height; i++) {
    sub_dgr[i] = new uint16_t[width];
  }
  uint16_t **sub_src = new uint16_t *[height];
  for (int i = 0; i < height; i++) {
    sub_src[i] = new uint16_t[width];
  }
  int **sub_r = new int *[height];
  for (int i = 0; i < height; i++) {
    sub_r[i] = new int[width];
  }
  // channel 0
  double **r0 = new double *[height];
  for (int i = 0; i < height; i++) {
    r0[i] = new double[width];
  }
  // channel 1
  double **r1 = new double *[height];
  for (int i = 0; i < height; i++) {
    r1[i] = new double[width];
  }

  uint16_t **repic = new uint16_t *[height];
  for (int i = 0; i < height; i++) {
    repic[i] = new uint16_t[width];
  }

  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      // reconstruct image
      sub_dgr[r][c] = dgd[r * dgd_stride + c];
      // src img
      sub_src[r][c] = src[r * src_stride + c];
      // src img-reconstruct image
      sub_r[r][c] = sub_src[r][c] - sub_dgr[r][c];
      // from tflite get channel 0
      r0[r][c] = output[r * 2 * out_stride + c * 2] * max_val;
      // from tflite get channel 1
      r1[r][c] = output[r * 2 * out_stride + c * 2 + 1] * max_val;
    }
  }

  int scale0, scale1, A0_min, A1_min;
  int *quadtset;
  quadtset = get_quadparm_from_qindex(QP, superres_denom, is_intra_only,
                                      is_luma, cnn_index);
  scale0 = quadtset[0];
  scale1 = quadtset[1];
  A0_min = quadtset[2];
  A1_min = quadtset[3];

  int cols = int(ceil(double(height) / unit_height));
  int rows = int(ceil(double(width) / unit_width));
  int number_crlc = cols * rows;
  int index_A = 0;
  int start_row = 0;
  int end_row = 0;
  int start_clow = 0;
  int end_clow = 0;
  int testnum = 10;
  for (int i = 0; i < cols; i++) {
    for (int j = 0; j < rows; j++) {
      if (i == cols - 1) {
        start_clow = i * unit_height;
        end_clow = height;
      } else {
        start_clow = i * unit_height;
        end_clow = (i + 1) * unit_height;
      }
      if (j == rows - 1) {
        start_row = j * unit_width;
        end_row = width;
      } else {
        start_row = j * unit_width;
        end_row = (j + 1) * unit_width;
      }
      if (width < unit_width) {
        start_row = 0;
        end_row = width;
      }
      if (height < unit_height) {
        start_clow = 0;
        end_clow = height;
      }
      int lenth_clows = end_clow - start_clow;
      int lenth_rows = end_row - start_row;

      int lenth = lenth_clows * lenth_rows;
      int *sub_r_flatten = new int[lenth];
      int k = 0;
      for (int i = start_clow; i < end_clow; i++) {
        for (int j = start_row; j < end_row; j++) {
          sub_r_flatten[k] = sub_r[i][j];
          k = k + 1;
        }
      }

      double *sub_r0 = new double[lenth];

      int k_r0 = 0;
      for (int i = start_clow; i < end_clow; i++) {
        for (int j = start_row; j < end_row; j++) {
          sub_r0[k_r0] = r0[i][j];
          k_r0++;
        }
      }

      double *sub_r1 = new double[lenth];
      int k_r1 = 0;
      for (int i = start_clow; i < end_clow; i++) {
        for (int j = start_row; j < end_row; j++) {
          sub_r1[k_r1] = r1[i][j];
          k_r1++;
        }
      }

      double **R = new double *[lenth];
      for (int i = 0; i < lenth; i++) {
        R[i] = new double[2];
      }

      for (int i = 0; i < lenth; i++) {
        for (int j = 0; j < 2; j++) {
          if (j == 0) {
            R[i][j] = sub_r0[i];
          }
          if (j == 1) {
            R[i][j] = sub_r1[i];
          }
        }
      }

      double **R_T = new double *[2];
      for (int i = 0; i < 2; i++) {
        R_T[i] = new double[lenth];
      }

      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < lenth; j++) {
          if (i == 0) {
            R_T[i][j] = sub_r0[j];
          }
          if (i == 1) {
            R_T[i][j] = sub_r1[j];
          }
        }
      }

      double **R_TDotR = new double *[2];
      for (int i = 0; i < 2; i++) {
        R_TDotR[i] = new double[2];
      }

      R_TDotR[0][0] = 0;
      for (int i = 0; i < lenth; i++) {
        R_TDotR[0][0] += R_T[0][i] * R[i][0];
      }
      R_TDotR[0][1] = 0;
      for (int i = 0; i < lenth; i++) {
        R_TDotR[0][1] += R_T[0][i] * R[i][1];
      }
      R_TDotR[1][0] = 0;
      for (int i = 0; i < lenth; i++) {
        R_TDotR[1][0] += R_T[1][i] * R[i][0];
      }
      R_TDotR[1][1] = 0;
      for (int i = 0; i < lenth; i++) {
        R_TDotR[1][1] += R_T[1][i] * R[i][1];
      }

      double value_R_TDotR =
          R_TDotR[0][0] * R_TDotR[1][1] - R_TDotR[0][1] * R_TDotR[1][0];
      double a00 = R_TDotR[1][1] / value_R_TDotR;
      double a01 = -1 * R_TDotR[0][1] / value_R_TDotR;
      double a10 = -1 * R_TDotR[1][0] / value_R_TDotR;
      double a11 = R_TDotR[0][0] / value_R_TDotR;

      double **R_TDotR_inver = new double *[2];
      for (int i = 0; i < 2; i++) {
        R_TDotR_inver[i] = new double[2];
      }

      R_TDotR_inver[0][0] = a00;
      R_TDotR_inver[0][1] = a01;
      R_TDotR_inver[1][0] = a10;
      R_TDotR_inver[1][1] = a11;

      double **mid = new double *[2];
      for (int i = 0; i < 2; i++) {
        mid[i] = new double[lenth];
      }
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < lenth; j++) {
          if (i == 0) {
            mid[i][j] = R_TDotR_inver[0][0] * R_T[0][j] +
                        R_TDotR_inver[0][1] * R_T[1][j];
          }
          if (i == 1) {
            mid[i][j] = R_TDotR_inver[1][0] * R_T[0][j] +
                        R_TDotR_inver[1][1] * R_T[1][j];
          }
        }
      }

      double A0 = 0;
      double A1 = 0;
      for (int i = 0; i < lenth; i++) {
        A0 += mid[0][i] * sub_r_flatten[i];
        A1 += mid[1][i] * sub_r_flatten[i];
      }
      A0 = A0 * scale0;
      A1 = A1 * scale1;
      A0 = int(round(A0));
      A1 = int(round(A1));
      if (A0 < A0_min) {
        A0 = A0_min;
      }
      if (A0 > A0_min + 15) {
        A0 = A0_min + 15;
      }
      A[index_A] = int(A0);
      index_A = index_A + 1;
      if (A1 < A1_min) {
        A1 = A1_min;
      }
      if (A1 > A1_min + 15) {
        A1 = A1_min + 15;
      }
      A[index_A] = int(A1);
      // fprintf(stderr, "ENCODER A VALUES ARE %d, %d\n", int(A0), int(A1));
      index_A = index_A + 1;
      // printf("A0:%lf  A1:%lf\n", A0, A1);
      for (int i = start_clow; i < end_clow; i++) {
        for (int j = start_row; j < end_row; j++) {
          repic[i][j] = int(round(sub_dgr[i][j] + A0 * r0[i][j] / scale0 +
                                  A1 * r1[i][j] / scale1));
          // repic[i][j] = int(round(sub_dgr[i][j]));
          repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
        }
      }

      for (int i = 0; i < lenth; i++) {
        delete[] R[i];
      }
      delete[] R;
      R = NULL;
      for (int i = 0; i < 2; i++) {
        delete[] R_T[i];
      }
      delete[] R_T;
      R_T = NULL;
      for (int i = 0; i < 2; i++) {
        delete[] R_TDotR[i];
      }
      delete[] R_TDotR;
      R_TDotR = NULL;
      for (int i = 0; i < 2; i++) {
        delete[] R_TDotR_inver[i];
      }
      delete[] R_TDotR_inver;
      R_TDotR_inver = NULL;
      for (int i = 0; i < 2; i++) {
        delete[] mid[i];
      }
      delete[] mid;
      mid = NULL;

      delete[] sub_r_flatten;
      delete[] sub_r0;
      delete[] sub_r1;
      sub_r_flatten = NULL;
      sub_r0 = NULL;
      sub_r1 = NULL;
    }
  }

  assert(A_size == index_A);

  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      rePic[r * rec_stride + c] = clip_pixel_highbd(repic[r][c], bit_depth);
    }
  }

  // delete
  for (int i = 0; i < height; i++) {
    delete[] sub_dgr[i];
  }
  delete[] sub_dgr;
  sub_dgr = NULL;
  for (int i = 0; i < height; i++) {
    delete[] sub_src[i];
  }
  delete[] sub_src;
  sub_src = NULL;

  for (int i = 0; i < height; i++) {
    delete[] sub_r[i];
  }
  delete[] sub_r;
  sub_r = NULL;
  // channel 0
  for (int i = 0; i < height; i++) {
    delete[] r0[i];
  }
  delete[] r0;
  r0 = NULL;
  // channel 1
  for (int i = 0; i < height; i++) {
    delete[] r1[i];
  }
  delete[] r1;
  r1 = NULL;
  for (int i = 0; i < height; i++) {
    delete[] repic[i];
  }
  delete[] repic;
  repic = NULL;
  // cv::Mat image;
  // uint8_t cc[480][832];
  // for (int r = 0; r < height; ++r) {
  //  for (int c = 0; c < width; ++c) {
  //    cc[r][c] = rePic[r * rec_stride + c];
  //  }
  //}
  // image = cv::Mat(480, 832, CV_8UC1, (void *)cc);
  // cv::imshow("dgr", image);
  // cv::waitKey();
  // cv::imwrite("../../res/pre_Image.jpg", image);

  interpreter.reset();

#if USE_XNNPACK
  // IMPORTANT: release the interpreter before destroying the delegate.
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK

  return 1;
}

extern "C" int TFlite_recon_quadtree_regular_hbd(
    uint16_t *dgd, int dgd_stride, int img_height, int img_width,
    uint16_t *rePic, int rec_stride, int QP, int *A, int *Split, int block_size,
    int superres_denom, int num_threads, int bit_depth, int is_intra_only,
    int is_luma, int cnn_index) {
#if USE_XNNPACK
  TfLiteDelegate *xnnpack_delegate = get_tflite_xnnpack_delegate(num_threads);
#endif  // USE_XNNPACK
  std::unique_ptr<tflite::Interpreter> interpreter =
      get_tflite_interpreter(QP, superres_denom, img_width, img_height,
                             num_threads, is_intra_only, is_luma, cnn_index
#if USE_XNNPACK
                             ,
                             xnnpack_delegate
#endif  // USE_XNNPACK
      );

  // Prepare input.
  const auto max_val = static_cast<float>((1 << bit_depth) - 1);
  const int in_stride = img_width;
  auto input = interpreter->typed_input_tensor<float>(0);
  for (int r = 0; r < img_height; ++r) {
    for (int c = 0; c < img_width; ++c) {
      input[r * in_stride + c] =
          static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
      assert(input[r * in_stride + c] >= 0.0f);
      assert(input[r * in_stride + c] <= 1.0f);
    }
  }

  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }

  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = img_width;
  uint16_t **sub_dgr = new uint16_t *[img_height];
  for (int i = 0; i < img_height; i++) {
    sub_dgr[i] = new uint16_t[img_width];
  }

  double **r0 = new double *[img_height];
  for (int i = 0; i < img_height; i++) {
    r0[i] = new double[img_width];
  }

  double **r1 = new double *[img_height];
  for (int i = 0; i < img_height; i++) {
    r1[i] = new double[img_width];
  }

  uint16_t **repic = new uint16_t *[img_height];
  for (int i = 0; i < img_height; i++) {
    repic[i] = new uint16_t[img_width];
  }

  for (int r = 0; r < img_height; ++r) {
    for (int c = 0; c < img_width; ++c) {
      // sub_dgr[r][c] = dgd[r * in_stride + c];
      sub_dgr[r][c] = dgd[r * dgd_stride + c];
      r0[r][c] = output[r * 2 * out_stride + c * 2] * max_val;
      r1[r][c] = output[r * 2 * out_stride + c * 2 + 1] * max_val;
    }
  }
  int scale0, scale1, A0_min, A1_min;
  int *quadtset;
  quadtset = get_quadparm_from_qindex(QP, superres_denom, is_intra_only,
                                      is_luma, cnn_index);
  scale0 = quadtset[0];
  scale1 = quadtset[1];
  A0_min = quadtset[2];
  A1_min = quadtset[3];

  int index_A = 0;
  int index_split = 0;
  int start_row = 0;
  int end_row = 0;
  int start_clow = 0;
  int end_clow = 0;
  int num_block = 0;

  for (int i = 0; i < img_height; i += block_size) {
    for (int j = 0; j < img_width; j += block_size) {
      if (i + block_size > img_height || j + block_size > img_width) {
        continue;
      } else {
        int starty = i;
        int startx = j;
        int buf_height;
        int buf_width;
        if (Split[index_split] == 0) {
          buf_height = block_size;
          buf_width = block_size;
          start_row = starty;
          end_row = starty + buf_height;
          start_clow = startx;
          end_clow = startx + buf_width;

          int lenth_clows = end_clow - start_clow;
          int lenth_rows = end_row - start_row;

          int lenth = lenth_clows * lenth_rows;
          int *sub_r_flatten = new int[lenth];

          int a0 = A[index_A++];
          int a1 = A[index_A++];

          for (int i = start_row; i < end_row; i++) {
            for (int j = start_clow; j < end_clow; j++) {
              repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                      a1 * r1[i][j] / scale1));
              repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
            }
          }

          for (int i = start_row; i < end_row; i++) {
            for (int j = start_clow; j < end_clow; j++) {
              rePic[i * rec_stride + j] =
                  clip_pixel_highbd(repic[i][j], bit_depth);
            }
          }
          delete[] sub_r_flatten;
          sub_r_flatten = NULL;
        } else if (Split[index_split] == 1) {
          buf_height = block_size / 2;
          buf_width = block_size / 2;
          for (int time = 0; time < 4; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
              case 2:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 3:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = A[index_A++];
            int a1 = A[index_A++];

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                        a1 * r1[i][j] / scale1));
                repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                rePic[i * rec_stride + j] =
                    clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }
            delete[] sub_r_flatten;
            sub_r_flatten = NULL;
          }

        } else if (Split[index_split] == 2) {  // vert

          buf_height = block_size;
          buf_width = block_size / 2;
          for (int time = 0; time < 2; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = A[index_A++];
            int a1 = A[index_A++];

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                        a1 * r1[i][j] / scale1));
                repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                rePic[i * rec_stride + j] =
                    clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }
            delete[] sub_r_flatten;
            sub_r_flatten = NULL;
          }
        } else if (Split[index_split] == 3) {  // horz

          buf_height = block_size / 2;
          buf_width = block_size;
          for (int time = 0; time < 2; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = A[index_A++];
            int a1 = A[index_A++];

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                        a1 * r1[i][j] / scale1));
                repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }

            for (int i = start_row; i < end_row; i++) {
              for (int j = start_clow; j < end_clow; j++) {
                rePic[i * rec_stride + j] =
                    clip_pixel_highbd(repic[i][j], bit_depth);
              }
            }
            delete[] sub_r_flatten;
            sub_r_flatten = NULL;
          }
        }

        index_split++;
      }
    }
  }

  for (int i = 0; i < img_height; i++) {
    delete[] sub_dgr[i];
  }
  delete[] sub_dgr;
  sub_dgr = NULL;
  for (int i = 0; i < img_height; i++) {
    delete[] r0[i];
  }
  delete[] r0;
  r0 = NULL;
  for (int i = 0; i < img_height; i++) {
    delete[] r1[i];
  }
  delete[] r1;
  r1 = NULL;
  for (int i = 0; i < img_height; i++) {
    delete[] repic[i];
  }
  delete[] repic;
  repic = NULL;

  interpreter.reset();

#if USE_XNNPACK
  // IMPORTANT: release the interpreter before destroying the delegate.
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK

  return 1;
}

extern "C" int TFlite_recon_quadtree_unregular_hbd(
    uint16_t *dgd, int dgd_stride, int img_height, int img_width,
    uint16_t *rePic, int rec_stride, int QP, int *A, int *regular_A, int *Split,
    int block_size, int superres_denom, int num_threads, int bit_depth,
    int is_intra_only, int is_luma, int cnn_index) {
  // makesure can downscale 3 times
  int padding_width = ceil(img_width * 1.0 / 8) * 8;
  int padding_height = ceil(img_height * 1.0 / 8) * 8;
#if USE_XNNPACK
  TfLiteDelegate *xnnpack_delegate = get_tflite_xnnpack_delegate(num_threads);
#endif  // USE_XNNPACK
  std::unique_ptr<tflite::Interpreter> interpreter =
      get_tflite_interpreter(QP, superres_denom, padding_width, padding_height,
                             num_threads, is_intra_only, is_luma, cnn_index
#if USE_XNNPACK
                             ,
                             xnnpack_delegate
#endif  // USE_XNNPACK
      );

  // Prepare input.
  const auto max_val = static_cast<float>((1 << bit_depth) - 1);
  const int in_stride = padding_width;
  auto input = interpreter->typed_input_tensor<float>(0);
  for (int r = 0; r < padding_height; ++r) {
    for (int c = 0; c < padding_width; ++c) {
      if (r < img_height && c < img_width) {
        input[r * in_stride + c] =
            static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
        assert(input[r * in_stride + c] >= 0.0f);
        assert(input[r * in_stride + c] <= 1.0f);
      } else {
        input[r * in_stride + c] = 0;
      }
    }
  }
  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }

  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = padding_width;

  uint16_t **sub_dgr = new uint16_t *[img_height];
  for (int i = 0; i < img_height; i++) {
    sub_dgr[i] = new uint16_t[img_width];
  }

  double **r0 = new double *[img_height];
  for (int i = 0; i < img_height; i++) {
    r0[i] = new double[img_width];
  }

  double **r1 = new double *[img_height];
  for (int i = 0; i < img_height; i++) {
    r1[i] = new double[img_width];
  }

  uint16_t **repic = new uint16_t *[img_height];
  for (int i = 0; i < img_height; i++) {
    repic[i] = new uint16_t[img_width];
  }

  for (int r = 0; r < img_height; ++r) {
    for (int c = 0; c < img_width; ++c) {
      // sub_dgr[r][c] = dgd[r * in_stride + c];
      sub_dgr[r][c] = dgd[r * dgd_stride + c];
      r0[r][c] = output[r * 2 * out_stride + c * 2] * max_val;
      r1[r][c] = output[r * 2 * out_stride + c * 2 + 1] * max_val;
    }
  }
  int scale0, scale1, A0_min, A1_min;
  int *quadtset;
  quadtset = get_quadparm_from_qindex(QP, superres_denom, is_intra_only,
                                      is_luma, cnn_index);
  scale0 = quadtset[0];
  scale1 = quadtset[1];
  A0_min = quadtset[2];
  A1_min = quadtset[3];

  int index_A = 0;
  int index_regular_A = 0;
  int index_split = 0;
  int start_row = 0;
  int end_row = 0;
  int start_clow = 0;
  int end_clow = 0;
  int num_block = 0;

  for (int i = 0; i < img_height; i += block_size) {
    for (int j = 0; j < img_width; j += block_size) {
      if (i + block_size > img_height || j + block_size > img_width) {
        start_row = i;
        end_row = img_height;
        start_clow = j;
        end_clow = img_width;

        int lenth_clows = end_clow - start_clow;
        int lenth_rows = end_row - start_row;

        int lenth = lenth_clows * lenth_rows;
        int *sub_r_flatten = new int[lenth];

        int a0 = A[index_A++];
        int a1 = A[index_A++];

        for (int i = start_row; i < end_row; i++) {
          for (int j = start_clow; j < end_clow; j++) {
            repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                    a1 * r1[i][j] / scale1));
            repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
          }
        }
        for (int i = start_row; i < end_row; i++) {
          for (int j = start_clow; j < end_clow; j++) {
            rePic[i * rec_stride + j] =
                clip_pixel_highbd(repic[i][j], bit_depth);
          }
        }
      } else {
        int starty = i;
        int startx = j;
        int buf_height;
        int buf_width;
        if (Split[index_split] == 0) {
          buf_height = block_size;
          buf_width = block_size;
          start_row = starty;
          end_row = starty + buf_height;
          start_clow = startx;
          end_clow = startx + buf_width;

          int lenth_clows = end_clow - start_clow;
          int lenth_rows = end_row - start_row;

          int lenth = lenth_clows * lenth_rows;
          int *sub_r_flatten = new int[lenth];

          int a0 = regular_A[index_regular_A++];
          int a1 = regular_A[index_regular_A++];

          for (int i = start_row; i < end_row; i++) {
            for (int j = start_clow; j < end_clow; j++) {
              repic[i][j] = int(round(sub_dgr[i][j] + a0 * r0[i][j] / scale0 +
                                      a1 * r1[i][j] / scale1));
              repic[i][j] = clip_pixel_highbd(repic[i][j], bit_depth);
            }
          }

          for (int i = start_row; i < end_row; i++) {
            for (int j = start_clow; j < end_clow; j++) {
              rePic[i * rec_stride + j] =
                  clip_pixel_highbd(repic[i][j], bit_depth);
            }
          }

        } else if (Split[index_split] == 1) {
          buf_height = block_size / 2;
          buf_width = block_size / 2;
          for (int time = 0; time < 4; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
              case 2:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 3:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = regular_A[index_regular_A++];
            int a1 = regular_A[index_regular_A++];
          }
        } else if (Split[index_split] == 2) {  // vert

          buf_height = block_size;
          buf_width = block_size / 2;
          for (int time = 0; time < 2; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx + buf_width;
                end_clow = startx + buf_width * 2;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = regular_A[index_regular_A++];
            int a1 = regular_A[index_regular_A++];
          }
        } else if (Split[index_split] == 3) {  // horz

          buf_height = block_size / 2;
          buf_width = block_size;
          for (int time = 0; time < 2; time++) {
            switch (time) {
              case 0:
                start_row = starty;
                end_row = starty + buf_height;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
              case 1:
                start_row = starty + buf_height;
                end_row = starty + buf_height * 2;
                start_clow = startx;
                end_clow = startx + buf_width;
                break;
            }

            int lenth_clows = end_clow - start_clow;
            int lenth_rows = end_row - start_row;
            int lenth = lenth_clows * lenth_rows;
            int *sub_r_flatten = new int[lenth];

            int a0 = regular_A[index_regular_A++];
            int a1 = regular_A[index_regular_A++];
          }
        }
        index_split++;
      }
    }
  }

  interpreter.reset();

#if USE_XNNPACK
  // IMPORTANT: release the interpreter before destroying the delegate.
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK

  return 1;
}

void Tree_tflite_hbd(uint16_t *rec, uint16_t *buf_256, uint16_t *buf_128,
                     uint16_t *buf_128_horz, uint16_t *buf_128_vert,
                     uint16_t *dgd, uint16_t *src, int dgd_stride,
                     int src_stride, int *A_256, int *A_128, int *A_128_horz,
                     int *A_128_vert, int height, int width, double dgd_psnr,
                     double delta_128, double delta_128_horz,
                     double delta_128_vert, int depth, int block_length,
                     int starty, int startx, std::vector<int> *Split,
                     std::vector<std::pair<int, int>> *A, int RDMULT,
                     int bit_depth) {
  int index;
  int quadtree_max_size = block_length;

#if CONFIG_CNN_GUIDED_QUADTREE_RDCOST
  int split_sse =
      computeSSE_buf_tflite_hbd(buf_128, src, startx, starty, block_length,
                                block_length, height, width, width, src_stride);

  int vert_sse =
      computeSSE_buf_tflite_hbd(buf_128_vert, src, startx, starty, block_length,
                                block_length, height, width, width, src_stride);

  int horz_sse =
      computeSSE_buf_tflite_hbd(buf_128_horz, src, startx, starty, block_length,
                                block_length, height, width, width, src_stride);

  int all_sse =
      computeSSE_buf_tflite_hbd(buf_256, src, startx, starty, block_length,
                                block_length, height, width, width, src_stride);

  double cost_split = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      RDMULT, (4 * 2 * 4 + 2) << 5, split_sse, bit_depth);

  double cost_vert = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      RDMULT, (4 * 2 * 2 + 2) << 5, vert_sse, bit_depth);

  double cost_horz = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      RDMULT, (4 * 2 * 2 + 2) << 5, horz_sse, bit_depth);

  double cost_all = RDCOST_DBL_WITH_NATIVE_BD_DIST(RDMULT, (4 * 2 + 2) << 5,
                                                   all_sse, bit_depth);

  double best_cost = min_tflite(cost_split, cost_vert, cost_horz, cost_all);
  // double best_cost = cost_all;

  if (cost_split == best_cost) {
    replace_tflite_hbd(startx, starty, block_length, block_length, rec, buf_128,
                       width);
    Split[0].push_back(0);
    Split[0].push_back(1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty, startx, quadtree_max_size);
    int a0 = A_128[index * 2];
    int a1 = A_128[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index =
        CalculateIndex_tflite(width, block_length / 2, block_length / 2, starty,
                              startx + block_length / 2, quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty + block_length / 2, startx,
                                  quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty + block_length / 2,
                                  startx + block_length / 2, quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

  } else if (cost_horz == best_cost) {
    replace_tflite_hbd(startx, starty, block_length, block_length, rec,
                       buf_128_horz, width);
    Split[0].push_back(1);
    Split[0].push_back(1);

    int index = CalculateIndex_tflite(width, block_length / 2, block_length,
                                      starty, startx, quadtree_max_size);
    int a0 = A_128_horz[index * 2];
    int a1 = A_128_horz[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length,
                                  starty + block_length / 2, startx,
                                  quadtree_max_size);
    a0 = A_128_horz[index * 2];
    a1 = A_128_horz[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

  } else if (cost_vert == best_cost) {
    replace_tflite_hbd(startx, starty, block_length, block_length, rec,
                       buf_128_vert, width);
    Split[0].push_back(1);
    Split[0].push_back(0);

    int index = CalculateIndex_tflite(width, block_length, block_length / 2,
                                      starty, startx, quadtree_max_size);
    int a0 = A_128_vert[index * 2];
    int a1 = A_128_vert[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length, block_length / 2, starty,
                                  startx + block_length / 2, quadtree_max_size);
    a0 = A_128_vert[index * 2];
    a1 = A_128_vert[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

  } else {
    replace_tflite_hbd(startx, starty, block_length, block_length, rec, buf_256,
                       width);
    Split[0].push_back(0);
    Split[0].push_back(0);
    index = CalculateIndex_tflite(width, block_length, block_length, starty,
                                  startx, quadtree_max_size);
    int a0 = A_256[index * 2];
    int a1 = A_256[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);
  }

#else
  double split_psnr = computePSNR_buf_tflite_hbd(
      buf_128, dgd, src, startx, starty, block_length, block_length, height,
      width, width, dgd_stride, src_stride, bit_depth);

  double split_psnr_horz = computePSNR_buf_tflite_hbd(
      buf_128_horz, dgd, src, startx, starty, block_length, block_length,
      height, width, width, dgd_stride, src_stride, bit_depth);

  double split_psnr_vert = computePSNR_buf_tflite_hbd(
      buf_128_vert, dgd, src, startx, starty, block_length, block_length,
      height, width, width, dgd_stride, src_stride, bit_depth);

  // int64_t split_rate = 4 * 2 * 3 + 4;
  // int64_t split_rate =4;
  double split_delta = (split_psnr - dgd_psnr) * 1000 / 4;
  double split_delta_horz = (split_psnr_horz - dgd_psnr) * 1000 / 2;
  double split_delta_vert = (split_psnr_vert - dgd_psnr) * 1000 / 2;

  double best_delta =
      AOMMAX(AOMMAX(split_delta, split_delta_horz), split_delta_vert);

  if (split_delta > delta_128 && best_delta == split_delta) {
    // printf("fast using split\n");
    replace_tflite_hbd(startx, starty, block_length, block_length, rec, buf_128,
                       width);
    Split[0].push_back(0);
    Split[0].push_back(1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty, startx, quadtree_max_size);
    int a0 = A_128[index * 2];
    int a1 = A_128[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index =
        CalculateIndex_tflite(width, block_length / 2, block_length / 2, starty,
                              startx + block_length / 2, quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty + block_length / 2, startx,
                                  quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length / 2,
                                  starty + block_length / 2,
                                  startx + block_length / 2, quadtree_max_size);
    a0 = A_128[index * 2];
    a1 = A_128[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);
  } else if (split_delta_horz > delta_128_horz &&
             best_delta == split_delta_horz) {
    // printf("fast using horz\n");
    replace_tflite_hbd(startx, starty, block_length, block_length, rec,
                       buf_128_horz, width);
    Split[0].push_back(1);
    Split[0].push_back(1);

    int index = CalculateIndex_tflite(width, block_length / 2, block_length,
                                      starty, startx, quadtree_max_size);
    int a0 = A_128_horz[index * 2];
    int a1 = A_128_horz[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length / 2, block_length,
                                  starty + block_length / 2, startx,
                                  quadtree_max_size);
    a0 = A_128_horz[index * 2];
    a1 = A_128_horz[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);

  } else if (split_delta_vert > delta_128_vert &&
             best_delta == split_delta_vert) {
    // printf("fast using vert\n");
    replace_tflite_hbd(startx, starty, block_length, block_length, rec,
                       buf_128_vert, width);
    Split[0].push_back(1);
    Split[0].push_back(0);

    int index = CalculateIndex_tflite(width, block_length, block_length / 2,
                                      starty, startx, quadtree_max_size);
    int a0 = A_128_vert[index * 2];
    int a1 = A_128_vert[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);

    index = CalculateIndex_tflite(width, block_length, block_length / 2, starty,
                                  startx + block_length / 2, quadtree_max_size);
    a0 = A_128_vert[index * 2];
    a1 = A_128_vert[index * 2 + 1];
    A0A1.first = a0;
    A0A1.second = a1;
    A[0].push_back(A0A1);
  } else {
    // printf("fast using all\n");
    replace_tflite_hbd(startx, starty, block_length, block_length, rec, buf_256,
                       width);
    Split[0].push_back(0);
    Split[0].push_back(0);
    index = CalculateIndex_tflite(width, block_length, block_length, starty,
                                  startx, quadtree_max_size);
    int a0 = A_256[index * 2];
    int a1 = A_256[index * 2 + 1];
    std::pair<int, int> A0A1(a0, a1);
    A[0].push_back(A0A1);
  }

#endif  // CONFIG_CNN_GUIDED_QUADTREE_RDCOST
}

extern "C" int av1_restore_cnn_quadtree_img_tflite_highbd(
    YV12_BUFFER_CONFIG *source_frame, AV1_COMMON *cm, int superres_denom,
    int RDMULT, int num_threads, int bit_depth, int is_intra_only, int is_luma,
    int cnn_index) {
  // save Split flag
  std::vector<int> Split;
  // save a0 a1
  std::vector<std::pair<int, int>> A;

  YV12_BUFFER_CONFIG *pcPicYuvRec = &cm->cur_frame->buf;
  uint16_t *dgr = CONVERT_TO_SHORTPTR(pcPicYuvRec->y_buffer);
  uint16_t *src = CONVERT_TO_SHORTPTR(source_frame->y_buffer);

  int quadtree_max_size = cm->cur_quad_info.unit_size;
  int height = pcPicYuvRec->y_crop_height;
  int width = pcPicYuvRec->y_crop_width;
  int dgr_stride = pcPicYuvRec->y_stride;
  int src_stride = source_frame->y_stride;
  int qp = cm->quant_params.base_qindex;

  // temp buf
  uint16_t *rec = new uint16_t[height * width];
  uint16_t *buf_level_0 = new uint16_t[height * width];
  uint16_t *buf_level_1 = new uint16_t[height * width];
  uint16_t *buf_level_1_horz = new uint16_t[height * width];
  uint16_t *buf_level_1_vert = new uint16_t[height * width];

  // save unfilter frame psnr
  double dgdpsnr = computePSNR_tflite_hbd(dgr, src, height, width, dgr_stride,
                                          src_stride, bit_depth);

  int regular_height_num = (int)floor(((float)height) / quadtree_max_size);
  int regular_width_num = (int)floor(((float)width) / quadtree_max_size);
  int block_num_level_0 = (int)ceil(((float)width) / quadtree_max_size) *
                          (int)ceil(((float)height) / quadtree_max_size);
  int regularblock_num = regular_height_num * regular_width_num;
  int un_regularblock_num = block_num_level_0 - regularblock_num;

  int A_num_level_0 = block_num_level_0 * 2;
  int *A_level_0 = new int[A_num_level_0];

  int A_num_level_1 = regularblock_num * 4 * 2;
  int *A_level_1 = new int[A_num_level_1];

  int A_num_level_1_horz = regularblock_num * 2 * 2;
  int *A_level_1_horz = new int[A_num_level_1_horz];

  int A_num_level_1_vert = regularblock_num * 2 * 2;
  int *A_level_1_vert = new int[A_num_level_1_vert];

  // uint16_t buf[512][512];
  // cv::Mat image;
  // for (int i = 0; i < 512; i++)
  //  for (int j = 0; j < 512; j++)
  //      buf[i][j] = dgr[i * dgr_stride + j];
  // image = cv::Mat(512, 512, CV_16UC1, (void *)buf);
  //// cv::imshow("rec", image);
  //// cv::waitKey();
  // cv::imwrite("../../res/beforeloop.jpg", image);

  // loopfilter all frame
  TFlite_Predict_quadtree_hbd(dgr, src, A_level_0, A_num_level_0, height, width,
                              dgr_stride, src_stride, qp, quadtree_max_size,
                              quadtree_max_size, buf_level_0, width,
                              superres_denom, num_threads, bit_depth,
                              is_intra_only, is_luma, cnn_index);
  double psnr_level_0 = computePSNR_tflite_hbd(buf_level_0, src, height, width,
                                               width, src_stride, bit_depth);
  // for (int i = 0; i < 512; i++)
  //  for (int j = 0; j < 512; j++) buf[i][j] = buf_level_0[i * width + j];
  // image = cv::Mat(512, 512, CV_16UC1, (void *)buf);
  ////cv::imshow("rec", image);
  ////cv::waitKey();
  // cv::imwrite("../../res/afterloop.jpg", image);

  replace_tflite_hbd(0, 0, width, height, rec, buf_level_0, width);

  double delta_level_1 = 0;
  double delta_level_1_horz = 0;
  double delta_level_1_vert = 0;
  if (regularblock_num != 0) {  // start quadtree
    TFlite_Predict_quadtree_hbd(dgr, src, A_level_1, A_num_level_1,
                                regular_height_num * quadtree_max_size,
                                regular_width_num * quadtree_max_size,
                                dgr_stride, src_stride, qp,
                                quadtree_max_size / 2, quadtree_max_size / 2,
                                buf_level_1, width, superres_denom, num_threads,
                                bit_depth, is_intra_only, is_luma, cnn_index);
    double psnr_level_1 = computePSNR_tflite_hbd(
        buf_level_1, src, height, width, width, src_stride, bit_depth);
    double num_level_1 = regular_height_num * regular_width_num * 4;
    delta_level_1 = ((psnr_level_1 - dgdpsnr) * 1000) / num_level_1;

    TFlite_Predict_quadtree_hbd(dgr, src, A_level_1_horz, A_num_level_1_horz,
                                regular_height_num * quadtree_max_size,
                                regular_width_num * quadtree_max_size,
                                dgr_stride, src_stride, qp, quadtree_max_size,
                                quadtree_max_size / 2, buf_level_1_horz, width,
                                superres_denom, num_threads, bit_depth,
                                is_intra_only, is_luma, cnn_index);
    double psnr_buf_level_1_horz = computePSNR_tflite_hbd(
        buf_level_1_horz, src, height, width, width, src_stride, bit_depth);
    double rate_buf_level_1_horz = regular_height_num * regular_width_num * 2;
    delta_level_1_horz =
        ((psnr_buf_level_1_horz - dgdpsnr) * 1000) / rate_buf_level_1_horz;

    TFlite_Predict_quadtree_hbd(
        dgr, src, A_level_1_vert, A_num_level_1_vert,
        regular_height_num * quadtree_max_size,
        regular_width_num * quadtree_max_size, dgr_stride, src_stride, qp,
        quadtree_max_size / 2, quadtree_max_size, buf_level_1_vert, width,
        superres_denom, num_threads, bit_depth, is_intra_only, is_luma,
        cnn_index);
    double psnr_buf_level_1_vert = computePSNR_tflite_hbd(
        buf_level_1_vert, src, height, width, width, src_stride, bit_depth);
    double rate_buf_level_1_vert = regular_height_num * regular_width_num * 2;
    delta_level_1_vert =
        ((psnr_buf_level_1_vert - dgdpsnr) * 1000) / rate_buf_level_1_vert;
  }

  int index = 0;
  for (int i = 0; i < height; i += quadtree_max_size) {
    for (int j = 0; j < width; j += quadtree_max_size) {
      if (i + quadtree_max_size > height ||
          j + quadtree_max_size > width) {  // unregular block  replace
        index =
            CalculateIndex_tflite(width, quadtree_max_size, quadtree_max_size,
                                  i, j, quadtree_max_size);
        int a0 = A_level_0[index * 2];
        int a1 = A_level_0[index * 2 + 1];
        std::pair<int, int> A0A1(a0, a1);
        A.push_back(A0A1);
      } else {  // retular block  start   judge
        Tree_tflite_hbd(rec, buf_level_0, buf_level_1, buf_level_1_horz,
                        buf_level_1_vert, dgr, src, dgr_stride, src_stride,
                        A_level_0, A_level_1, A_level_1_horz, A_level_1_vert,
                        height, width, dgdpsnr, delta_level_1,
                        delta_level_1_horz, delta_level_1_vert, 0,
                        quadtree_max_size, i, j, &Split, &A, RDMULT, bit_depth);
      }
    }
  }

  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      *(dgr + i * dgr_stride + j) =
          *(rec + i * width + j);  // Fill in the luma buffer again
    }
  }

  dgdpsnr = computePSNR_tflite_hbd(dgr, src, height, width, dgr_stride,
                                   src_stride, bit_depth);

  // FILE *fp_out;
  // fp_out = fopen("../../res/rec_unregular.txt", "w");
  // for (int i = 0; i < 256; i++) {
  //  for (int j = 0; j < 256; j++) {
  //    fprintf(fp_out, "%d\n", *(rec + i * width + j));
  //  }
  //}
  if (rec != NULL) {
    delete[] rec;
    rec = NULL;
  }
  if (buf_level_0 != NULL) {
    delete[] buf_level_0;
    buf_level_0 = NULL;
  }
  if (buf_level_1 != NULL) {
    delete[] buf_level_1;
    buf_level_1 = NULL;
  }
  if (buf_level_1_horz != NULL) {
    delete[] buf_level_1_horz;
    buf_level_1_horz = NULL;
  }
  if (buf_level_1_vert != NULL) {
    delete[] buf_level_1_vert;
    buf_level_1_vert = NULL;
  }
  if (A_level_0 != NULL) {
    delete[] A_level_0;
    A_level_0 = NULL;
  }
  if (A_level_1 != NULL) {
    delete[] A_level_1;
    A_level_1 = NULL;
  }
  if (A_level_1_horz != NULL) {
    delete[] A_level_1_horz;
    A_level_1_horz = NULL;
  }
  if (A_level_1_vert != NULL) {
    delete[] A_level_1_vert;
    A_level_1_vert = NULL;
  }

  cm->cur_quad_info.split_info_length = (int)Split.size();
  cm->cur_quad_info.unit_info_length = (int)A.size();

  for (unsigned int i = 0; i < Split.size(); ++i) {
    cm->cur_quad_info.split_info[i].split = Split[i];
  }

  for (unsigned int i = 0; i < A.size(); ++i) {
    cm->cur_quad_info.unit_info[i].xqd[0] = A[i].first;
    cm->cur_quad_info.unit_info[i].xqd[1] = A[i].second;
    // printf("a0:%d a1:%d\n", A[i].first, A[i].second);
  }

  return 1;
}

extern "C" int av1_restore_cnn_quadtree_decode_img_tflite_highbd(
    AV1_COMMON *cm, int superres_denom, int num_threads, int bit_depth,
    int is_intra_only, int is_luma, int cnn_index) {
  YV12_BUFFER_CONFIG *pcPicYuvRec = &cm->cur_frame->buf;
  uint16_t *dgr = CONVERT_TO_SHORTPTR(pcPicYuvRec->y_buffer);
  int height = pcPicYuvRec->y_crop_height;
  int width = pcPicYuvRec->y_crop_width;
  int dgr_stride = pcPicYuvRec->y_stride;
  int qp = cm->quant_params.base_qindex;

  int quadtree_max_size = cm->postcnn_quad_info.unit_size;
  int regular_height_num = (int)floor(((float)height) / quadtree_max_size);
  int regular_width_num = (int)floor(((float)width) / quadtree_max_size);
  int block_size_256 = (int)ceil(((float)width) / quadtree_max_size) *
                       (int)ceil(((float)height) / quadtree_max_size);
  int regularblock_num = regular_height_num * regular_width_num;
  int un_regularblock_num = block_size_256 - regularblock_num;

  QUADInfo quadinfo = cm->postcnn_quad_info;
  QUADSplitInfo *split_info = quadinfo.split_info;
  QUADUnitInfo *unit_info = quadinfo.unit_info;
  quadinfo.split_info_index = 0;
  quadinfo.unit_info_index = 0;

  uint16_t *rec = new uint16_t[height * width];

  int A_num_unregular = un_regularblock_num * 2;
  int unregular_index = 0;
  int *A_unregular = new int[A_num_unregular];

  int A_num_regular = quadinfo.unit_info_length * 2;
  int regular_index = 0;
  int *A_regular = new int[A_num_regular];

  int split_index = 0;
  int *Split = new int[quadinfo.split_info_length / 2];

  int index = 0;
  for (int i = 0; i < height; i += quadtree_max_size) {
    for (int j = 0; j < width; j += quadtree_max_size) {
      if (i + quadtree_max_size > height || j + quadtree_max_size > width) {
        int a0 = quadinfo.unit_info[quadinfo.unit_info_index].xqd[0];
        int a1 = quadinfo.unit_info[quadinfo.unit_info_index].xqd[1];
        quadinfo.unit_info_index++;
        A_unregular[unregular_index++] = a0;
        A_unregular[unregular_index++] = a1;

      } else {
        deTree_tflite(&quadinfo, &regular_index, A_regular, &split_index,
                      Split);
      }
      index++;
    }
  }

  TFlite_recon_quadtree_regular_hbd(
      dgr, dgr_stride, regular_height_num * quadtree_max_size,
      regular_width_num * quadtree_max_size, rec, width, qp, A_regular, Split,
      quadtree_max_size, superres_denom, num_threads, bit_depth, is_intra_only,
      is_luma, cnn_index);

  TFlite_recon_quadtree_unregular_hbd(
      dgr, dgr_stride, height, width, rec, width, qp, A_unregular, A_regular,
      Split, quadtree_max_size, superres_denom, num_threads, bit_depth,
      is_intra_only, is_luma, cnn_index);

  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      *(dgr + i * dgr_stride + j) =
          *(rec + i * width + j);  // Fill in the luma buffer again
    }
  }
  // FILE *fp_out;
  // fp_out = fopen("../../res/dec_unregular.txt", "w");
  // for (int i = 0; i < 256; i++) {
  //  for (int j = 0; j < 256; j++) {
  //    fprintf(fp_out, "%d\n", *(rec + i * width + j));
  //  }
  //}
  delete[] rec;
  rec = NULL;
  return 1;
}

extern "C" void av1_restore_cnn_quadtree_tflite(
    struct AV1Common *cm, YV12_BUFFER_CONFIG *source_frame, int RDMULT,
    int num_threads, const int apply_cnn[MAX_MB_PLANE],
    const int cnn_indices[MAX_MB_PLANE]) {
  YV12_BUFFER_CONFIG *buf = &cm->cur_frame->buf;
  const int is_intra_only = frame_is_intra_only(cm);
  for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
    if (!apply_cnn[plane]) continue;
    const int is_luma = (plane == AOM_PLANE_Y);
    const int cnn_index = cnn_indices[plane];
    assert(cnn_index >= 0 &&
           cnn_index < av1_num_cnn_indices_for_plane(cm, plane));
    switch (plane) {
      case AOM_PLANE_Y:
        av1_restore_cnn_quadtree_img_tflite_highbd(
            source_frame, cm, cm->superres_scale_denominator, RDMULT,
            num_threads, cm->seq_params.bit_depth, is_intra_only, is_luma,
            cnn_index);
        break;
      case AOM_PLANE_U:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      case AOM_PLANE_V:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      default: assert(0 && "Invalid plane index");
    }
  }
}

extern "C" void av1_restore_cnn_quadtree_decode_tflite(
    struct AV1Common *cm, int num_threads, int use_quadtree,
    const int apply_cnn[MAX_MB_PLANE], const int cnn_indices[MAX_MB_PLANE]) {
  YV12_BUFFER_CONFIG *buf = &cm->cur_frame->buf;
  const int is_intra_only = frame_is_intra_only(cm);
  for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
    if (!apply_cnn[plane]) continue;
    const int is_luma = (plane == AOM_PLANE_Y);
    if (is_luma && !use_quadtree) continue;
    const int cnn_index = cnn_indices[plane];
    assert(cnn_index >= 0 &&
           cnn_index < av1_num_cnn_indices_for_plane(cm, plane));
    switch (plane) {
      case AOM_PLANE_Y:
        av1_restore_cnn_quadtree_decode_img_tflite_highbd(
            cm, cm->superres_scale_denominator, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      case AOM_PLANE_U:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      case AOM_PLANE_V:
        av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        break;
      default: assert(0 && "Invalid plane index");
    }
  }
}
#endif  // CONFIG_CNN_GUIDED_QUADTREE

#endif  // CONFIG_CNN_RESTORATION
