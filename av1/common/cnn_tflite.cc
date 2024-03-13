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

#include "aom_dsp/binary_codes_writer.h"
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

// ------------------- Guided Quadtree: Common -------------------------------//

// Given single-channel input in 'dgd', generate intermediate 2-channel CNN
// output 'interm'.
static int generate_interm_guided_restoration(
    const uint16_t *dgd, int dgd_stride, int qindex, int superres_denom,
    int width, int height, int num_threads, int is_intra_only, int is_luma,
    int cnn_index, int bit_depth,
    std::vector<std::vector<std::vector<double>>> &interm) {
  // Make sure we can downscale 4 times.
  const int padding_width = (int)ceil(width * 1.0 / 16) * 16;
  const int padding_height = (int)ceil(height * 1.0 / 16) * 16;
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
        input[r * in_stride + c] =
            static_cast<float>(dgd[AOMMIN(r, height - 1) * dgd_stride +
                                   AOMMIN(c, width - 1)]) /
            max_val;
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

  // Store the output in 'interm'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = padding_width;

  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      interm[r][c][0] = output[r * 2 * out_stride + c * 2] * max_val;
      interm[r][c][1] = output[r * 2 * out_stride + c * 2 + 1] * max_val;
    }
  }

  // Cleanup.
  interpreter.reset();
#if USE_XNNPACK
  // IMPORTANT: release the interpreter before destroying the delegate.
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK
  return 1;
}

// Get unit width and height based on max size and partition type.
static void get_unit_size(int max_unit_width, int max_unit_height,
                          GuidedQuadTreePartitionType partition_type,
                          int *unit_width, int *unit_height) {
  assert(partition_type >= 0 && partition_type < GUIDED_QT_TYPES);
  *unit_width =
      (partition_type == GUIDED_QT_NONE || partition_type == GUIDED_QT_HORZ)
          ? max_unit_width
          : max_unit_width >> 1;
  *unit_height =
      (partition_type == GUIDED_QT_NONE || partition_type == GUIDED_QT_VERT)
          ? max_unit_height
          : max_unit_height >> 1;
}

// ------------------- Guided Quadtree: Encoder ------------------------------//

// Given 2-channel intermediate output 'interm', degraded frame 'dgd' and source
// frame 'src', generates the single-channel output 'out' and corresponding
// linear combination weight pairs 'a'.
// Assumes that `width x height` area needs to be combined using unit of size
// `unit_width x unit_height`.
static void generate_linear_combination(
    const std::vector<std::vector<std::vector<double>>> &interm,
    const uint16_t *src, int src_stride, const uint16_t *dgd, int dgd_stride,
    int start_row, int end_row, int start_col, int end_col, int unit_width,
    int unit_height, const int *quadtset, int rdmult, const int *norestorecost,
    int bit_depth, std::vector<std::vector<uint16_t>> &out,
    std::vector<std::pair<int, int>> &A) {
  const int scale0 = quadtset[0];
  const int scale1 = quadtset[1];
  const int A0_min = quadtset[2];
  const int A1_min = quadtset[3];

  for (int row = start_row; row < end_row; row += unit_height) {
    const int this_start_row = row;
    const int this_end_row = AOMMIN(row + unit_height, end_row);
    for (int col = start_col; col < end_col; col += unit_width) {
      const int this_start_col = col;
      const int this_end_col = AOMMIN(col + unit_width, end_col);
      const int num_pixels =
          (this_end_row - this_start_row) * (this_end_col - this_start_col);

      // Extract some flattened arrays.
      std::vector<int> sub_r_flatten;
      sub_r_flatten.reserve(num_pixels);
      for (int i = this_start_row; i < this_end_row; i++) {
        for (int j = this_start_col; j < this_end_col; j++) {
          sub_r_flatten.push_back(src[i * src_stride + j] -
                                  dgd[i * dgd_stride + j]);
        }
      }
      assert((int)sub_r_flatten.size() == num_pixels);

      std::vector<double> sub_r0;
      sub_r0.reserve(num_pixels);
      for (int i = this_start_row; i < this_end_row; i++) {
        for (int j = this_start_col; j < this_end_col; j++) {
          sub_r0.push_back(interm[i][j][0]);
        }
      }
      assert((int)sub_r0.size() == num_pixels);

      std::vector<double> sub_r1;
      sub_r1.reserve(num_pixels);
      for (int i = this_start_row; i < this_end_row; i++) {
        for (int j = this_start_col; j < this_end_col; j++) {
          sub_r1.push_back(interm[i][j][1]);
        }
      }
      assert((int)sub_r1.size() == num_pixels);

      // Get R.
      std::vector<std::vector<double>> R(num_pixels, std::vector<double>(2));
      for (int i = 0; i < num_pixels; i++) {
        R[i][0] = sub_r0[i];
        R[i][1] = sub_r1[i];
      }

      // Get R^T.
      std::vector<std::vector<double>> R_T(2, std::vector<double>(num_pixels));
      for (int i = 0; i < num_pixels; i++) {
        R_T[0][i] = sub_r0[i];
        R_T[1][i] = sub_r1[i];
      }

      // Get R^T * R.
      double R_TDotR[2][2] = { 0 };
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
          for (int k = 0; k < num_pixels; k++) {
            R_TDotR[i][j] += R_T[i][k] * R[k][j];
          }
        }
      }

      // Get (R^T * R)^-1.
      const double value_R_TDotR =
          R_TDotR[0][0] * R_TDotR[1][1] - R_TDotR[0][1] * R_TDotR[1][0];

      double R_TDotR_inver[2][2] = {
        { R_TDotR[1][1] / value_R_TDotR, -1 * R_TDotR[0][1] / value_R_TDotR },
        { -1 * R_TDotR[1][0] / value_R_TDotR, R_TDotR[0][0] / value_R_TDotR }
      };

      // Get (R^T * R)^-1 * R^T.
      std::vector<std::vector<double>> mid(2, std::vector<double>(num_pixels));
      for (int j = 0; j < num_pixels; j++) {
        mid[0][j] =
            R_TDotR_inver[0][0] * R_T[0][j] + R_TDotR_inver[0][1] * R_T[1][j];
        mid[1][j] =
            R_TDotR_inver[1][0] * R_T[0][j] + R_TDotR_inver[1][1] * R_T[1][j];
      }

      // Compute A = (R^T * R)^-1 * R^T * residual.
      double A0 = 0;
      double A1 = 0;
      for (int i = 0; i < num_pixels; i++) {
        A0 += mid[0][i] * sub_r_flatten[i];
        A1 += mid[1][i] * sub_r_flatten[i];
      }
      A0 = A0 * scale0;
      A1 = A1 * scale1;

      // Do a finer search for best A0, A1 pair amongst four options:
      // (1) A0_floor = floor(A0), A1_floor = floor(A1)
      // (2) A0_floor, A1_floor + 1
      // (3) A0_floor + 1, A1_floor
      // (4) A0_floor + 1, A1_floor + 1
      const bool do_finer_search = true;
      if (do_finer_search) {
        double bestA0 = 0;
        double bestA1 = 0;
        double cost;
        int64_t err = 0;
        for (int i = this_start_row; i < this_end_row; i++) {
          for (int j = this_start_col; j < this_end_col; j++) {
            const int diff = src[i * src_stride + j] - dgd[i * dgd_stride + j];
            err += diff * diff;
          }
        }
        double bestcost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            rdmult, norestorecost[1] >> 4, err, bit_depth);

        // finer search
        double flrA0 = (floor(A0));
        double flrA1 = (floor(A1));
        flrA0 = AOMMIN(AOMMAX(flrA0, A0_min), A0_min + GUIDED_A_RANGE);
        flrA1 = AOMMIN(AOMMAX(flrA1, A1_min), A1_min + GUIDED_A_RANGE);
        {
          A0 = flrA0;
          A1 = flrA1;
          err = 0;
          for (int i = this_start_row; i < this_end_row; i++) {
            for (int j = this_start_col; j < this_end_col; j++) {
              int rest = int(round(dgd[i * dgd_stride + j] +
                                   A0 * interm[i][j][0] / scale0 +
                                   A1 * interm[i][j][1] / scale1));
              rest = clip_pixel_highbd(rest, bit_depth);
              const int diff = src[i * src_stride + j] - rest;
              err += diff * diff;
            }
          }
          // approx RD cost assuming GUIDED_A_PAIR_BITS bits per a0, a1 pair
          cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              rdmult,
              (norestorecost[0] +
               (GUIDED_A_PAIR_BITS << AV1_PROB_COST_SHIFT)) >>
                  4,
              err, bit_depth);
          if (cost < bestcost) {
            bestA0 = A0;
            bestA1 = A1;
            bestcost = cost;
          }
        }
        if (flrA0 < A0_min + GUIDED_A_RANGE) {
          A0 = flrA0 + 1;
          A1 = flrA1;
          err = 0;
          for (int i = this_start_row; i < this_end_row; i++) {
            for (int j = this_start_col; j < this_end_col; j++) {
              int rest = int(round(dgd[i * dgd_stride + j] +
                                   A0 * interm[i][j][0] / scale0 +
                                   A1 * interm[i][j][1] / scale1));
              rest = clip_pixel_highbd(rest, bit_depth);
              const int diff = src[i * src_stride + j] - rest;
              err += diff * diff;
            }
          }
          // approx RD cost assuming GUIDED_A_PAIR_BITS bits per a0, a1 pair
          cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              rdmult,
              (norestorecost[0] +
               (GUIDED_A_PAIR_BITS << AV1_PROB_COST_SHIFT)) >>
                  4,
              err, bit_depth);
          if (cost < bestcost) {
            bestA0 = A0;
            bestA1 = A1;
            bestcost = cost;
          }
        }
        if (flrA1 < A1_min + GUIDED_A_RANGE) {
          A0 = flrA0;
          A1 = flrA1 + 1;
          err = 0;
          for (int i = this_start_row; i < this_end_row; i++) {
            for (int j = this_start_col; j < this_end_col; j++) {
              int rest = int(round(dgd[i * dgd_stride + j] +
                                   A0 * interm[i][j][0] / scale0 +
                                   A1 * interm[i][j][1] / scale1));
              rest = clip_pixel_highbd(rest, bit_depth);
              const int diff = src[i * src_stride + j] - rest;
              err += diff * diff;
            }
          }
          // approx RD cost assuming GUIDED_A_PAIR_BITS bits per a0, a1 pair
          cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              rdmult,
              (norestorecost[0] +
               (GUIDED_A_PAIR_BITS << AV1_PROB_COST_SHIFT)) >>
                  4,
              err, bit_depth);
          if (cost < bestcost) {
            bestA0 = A0;
            bestA1 = A1;
            bestcost = cost;
          }
        }
        if (flrA0 < A0_min + GUIDED_A_RANGE &&
            flrA1 < A1_min + GUIDED_A_RANGE) {
          A0 = flrA0 + 1;
          A1 = flrA1 + 1;
          err = 0;
          for (int i = this_start_row; i < this_end_row; i++) {
            for (int j = this_start_col; j < this_end_col; j++) {
              int rest = int(round(dgd[i * dgd_stride + j] +
                                   A0 * interm[i][j][0] / scale0 +
                                   A1 * interm[i][j][1] / scale1));
              rest = clip_pixel_highbd(rest, bit_depth);
              const int diff = src[i * src_stride + j] - rest;
              err += diff * diff;
            }
          }
          // approx RD cost assuming GUIDED_A_PAIR_BITS bits per a0, a1 pair
          cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              rdmult,
              (norestorecost[0] +
               (GUIDED_A_PAIR_BITS << AV1_PROB_COST_SHIFT)) >>
                  4,
              err, bit_depth);
          if (cost < bestcost) {
            bestA0 = A0;
            bestA1 = A1;
            bestcost = cost;
          }
        }
        A0 = bestA0;
        A1 = bestA1;
      } else {
        A0 = (round(A0));
        A1 = (round(A1));
        A0 = AOMMIN(AOMMAX(A0, A0_min), A0_min + GUIDED_A_RANGE);
        A1 = AOMMIN(AOMMAX(A1, A1_min), A1_min + GUIDED_A_RANGE);
      }

      A0 = AOMMIN(AOMMAX(A0, A0_min), A0_min + GUIDED_A_RANGE);
      A1 = AOMMIN(AOMMAX(A1, A1_min), A1_min + GUIDED_A_RANGE);
      A.emplace_back((int)A0, (int)A1);
      for (int i = this_start_row; i < this_end_row; i++) {
        for (int j = this_start_col; j < this_end_col; j++) {
          const int out_unclipped = int(round(dgd[i * dgd_stride + j] +
                                              A0 * interm[i][j][0] / scale0 +
                                              A1 * interm[i][j][1] / scale1));
          out[i - start_row][j - start_col] =
              clip_pixel_highbd(out_unclipped, bit_depth);
        }
      }
    }
  }
#ifndef NDEBUG
  const auto num_units_row =
      (size_t)ceil((double)(end_row - start_row) / unit_height);
  const auto num_units_col =
      (size_t)ceil((double)(end_col - start_col) / unit_width);
  assert(A.size() == num_units_row * num_units_col);
#endif  // NDEBUG
}

// Computes SSE between 'rst' and 'src'.
static int64_t compute_sse(const std::vector<std::vector<uint16_t>> &rst,
                           const uint16_t *src, int src_stride, int start_row,
                           int end_row, int start_col, int end_col) {
  int64_t sse = 0;
  for (int r = start_row; r < end_row; ++r) {
    for (int c = start_col; c < end_col; ++c) {
      const uint16_t this_rst = rst[r - start_row][c - start_col];
      const uint16_t this_src = src[r * src_stride + c];
      const int64_t diff = (int64_t)(this_rst - this_src);
      sse += diff * diff;
    }
  }
  return sse;
}

// Computes bitrate for the given weight parameters.
static int compute_rate(const std::vector<std::pair<int, int>> &A,
                        const std::pair<int, int> &prev_A, const int *quadtset,
                        const int *norestorecosts) {
  const int A0_min = quadtset[2];
  const int A1_min = quadtset[3];
  int num_bits = 0;
  int ref0 = AOMMIN(AOMMAX(prev_A.first - A0_min, 0), GUIDED_A_RANGE);
  int ref1 = AOMMIN(AOMMAX(prev_A.second - A1_min, 0), GUIDED_A_RANGE);
  for (auto &this_A : A) {
    if (this_A.first == 0 && this_A.second == 0) {
      num_bits += norestorecosts[1];
    } else {
      num_bits += norestorecosts[0];
      num_bits += (aom_count_primitive_refsubexpfin(
                       GUIDED_A_NUM_VALUES, 1, ref0, this_A.first - A0_min) +
                   aom_count_primitive_refsubexpfin(
                       GUIDED_A_NUM_VALUES, 1, ref1, this_A.second - A1_min))
                  << AV1_PROB_COST_SHIFT;
    }
    ref0 = AOMMIN(AOMMAX(this_A.first - A0_min, 0), GUIDED_A_RANGE);
    ref1 = AOMMIN(AOMMAX(this_A.second - A1_min, 0), GUIDED_A_RANGE);
  }
  return num_bits;
}

// Given 2-channel intermediate output in 'interm' as well as 'src' and 'dgd'
// buffers, tries the given partition type on a single quadtree unit. Outputs
// the RDCost in 'this_rdcost' and restored unit in 'out'.
static void try_one_partition(
    const std::vector<std::vector<std::vector<double>>> &interm,
    GuidedQuadTreePartitionType partition_type, const uint16_t *src,
    int src_stride, const uint16_t *dgd, int dgd_stride, int start_row,
    int end_row, int start_col, int end_col, int max_unit_width,
    int max_unit_height, const int *quadtset, int rdmult,
    const std::pair<int, int> &prev_A, const int *quad_split_costs,
    const int *binary_split_costs, const int *norestorecosts, int bit_depth,
    bool is_horz_partitioning_allowed, int is_vert_partitioning_allowed,
    double *this_rdcost, std::vector<std::vector<uint16_t>> &out,
    std::vector<std::pair<int, int>> &A) {
  assert(IMPLIES(
      !is_horz_partitioning_allowed,
      partition_type == GUIDED_QT_NONE || partition_type == GUIDED_QT_VERT));
  assert(IMPLIES(
      !is_vert_partitioning_allowed,
      partition_type == GUIDED_QT_NONE || partition_type == GUIDED_QT_HORZ));
  // Get unit width and height based on partition type.
  int unit_width;
  int unit_height;
  get_unit_size(max_unit_width, max_unit_height, partition_type, &unit_width,
                &unit_height);

  // Compute restored unit, a0 and a1.
  generate_linear_combination(interm, src, src_stride, dgd, dgd_stride,
                              start_row, end_row, start_col, end_col,
                              unit_width, unit_height, quadtset, rdmult,
                              norestorecosts, bit_depth, out, A);
  assert(IMPLIES(partition_type == GUIDED_QT_NONE, A.size() == 1));
  assert(IMPLIES(partition_type == GUIDED_QT_HORZ, A.size() == 2));
  assert(IMPLIES(partition_type == GUIDED_QT_VERT, A.size() == 2));
  assert(IMPLIES(partition_type == GUIDED_QT_SPLIT, A.size() == 4));

  // Compute SSE.
  const int64_t sse =
      compute_sse(out, src, src_stride, start_row, end_row, start_col, end_col);

  // Compute Rate.
  const int a_signaling_cost =
      compute_rate(A, prev_A, quadtset, norestorecosts);
  // Partition signaling cost depending on 1, 2 or 4 possible partition types.
  const int partition_signaling_cost =
      is_horz_partitioning_allowed && is_vert_partitioning_allowed
          ? quad_split_costs[partition_type]
      : (is_horz_partitioning_allowed || is_vert_partitioning_allowed)
          ? binary_split_costs[partition_type]
          : 0;
  const int bitrate = a_signaling_cost + partition_signaling_cost;

  // Compute RDCost.
  *this_rdcost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, bitrate >> 4, sse, bit_depth);
}

// Given intermediate restoration 'interm', source 'src' and degradade frame
// 'dgd', computes the best partitioning out of NONE, SPLIT, HORZ and VERT based
// on RD cost for the widthxheight unit starting at 'row' and 'col'.
// The split decisions are stored in 'split' and a0,a1 pairs are stored in 'A'.
static void select_quadtree_partitioning(
    const std::vector<std::vector<std::vector<double>>> &interm,
    const uint16_t *src, int src_stride, int start_row, int start_col,
    int width, int height, int quadtree_max_size, int max_unit_width,
    int max_unit_height, const int *quadtset, int rdmult,
    const std::pair<int, int> &prev_A, const int *quad_split_costs,
    const int *binary_split_costs, const int norestorecosts[2], int bit_depth,
    const uint16_t *dgd, int dgd_stride, std::vector<int> &split,
    std::vector<std::pair<int, int>> &A, double *rdcost) {
  const int end_row = AOMMIN(start_row + max_unit_height, height);
  const int end_col = AOMMIN(start_col + max_unit_width, width);
  // Check for special cases near boundary.
  const bool is_horz_partitioning_allowed =
      (max_unit_height >= quadtree_max_size);
  const bool is_vert_partitioning_allowed =
      (max_unit_width >= quadtree_max_size);
  const bool is_split_partitioning_allowed =
      is_horz_partitioning_allowed && is_vert_partitioning_allowed;

  auto best_rdcost = DBL_MAX;
  std::vector<std::pair<int, int>> best_A;
  std::vector<std::vector<uint16_t>> best_out(
      max_unit_height, std::vector<uint16_t>(max_unit_width));
  GuidedQuadTreePartitionType best_partition_type = GUIDED_QT_INVALID;

  for (int type = 0; type < GUIDED_QT_TYPES; ++type) {
    const auto this_partition_type = (GuidedQuadTreePartitionType)type;
    // Check for special cases near boundary.
    if (!is_horz_partitioning_allowed &&
        (this_partition_type == GUIDED_QT_HORZ)) {
      continue;
    }
    if (!is_vert_partitioning_allowed &&
        (this_partition_type == GUIDED_QT_VERT)) {
      continue;
    }
    if (!is_split_partitioning_allowed &&
        (this_partition_type == GUIDED_QT_SPLIT)) {
      continue;
    }
    // Try this partition type.
    double this_rdcost;
    std::vector<std::pair<int, int>> this_A;
    std::vector<std::vector<uint16_t>> this_out(
        max_unit_height, std::vector<uint16_t>(max_unit_width));
    try_one_partition(
        interm, this_partition_type, src, src_stride, dgd, dgd_stride,
        start_row, end_row, start_col, end_col, max_unit_width, max_unit_height,
        quadtset, rdmult, prev_A, quad_split_costs, binary_split_costs,
        norestorecosts, bit_depth, is_horz_partitioning_allowed,
        is_vert_partitioning_allowed, &this_rdcost, this_out, this_A);
    if (this_rdcost < best_rdcost) {
      best_rdcost = this_rdcost;
      best_A = this_A;
      best_out = this_out;
      best_partition_type = this_partition_type;
    }
  }

  // Save RDCost.
  *rdcost = best_rdcost;

  // Save a0, a1 pairs.
  for (auto &a0a1 : best_A) {
    A.push_back(a0a1);
  }

  // Save split decision.
  if (!is_horz_partitioning_allowed && !is_vert_partitioning_allowed) {
    // Nothing should be added to 'split' array.
    assert(best_partition_type == GUIDED_QT_NONE);
    return;
  }
  assert(best_partition_type >= 0 && best_partition_type < GUIDED_QT_TYPES);
  split.push_back(best_partition_type);
}

static void apply_quadtree_partitioning(
    const std::vector<std::vector<std::vector<double>>> &interm, int start_row,
    int start_col, int width, int height, int quadtree_max_size,
    int max_unit_width, int max_unit_height, const int *quadtset, int bit_depth,
    const std::vector<int> &split, size_t &split_index,
    const std::vector<std::pair<int, int>> &A, size_t &A_index, uint16_t *dgd,
    int dgd_stride);

// Top-level function to apply guided restoration on encoder side.
static int restore_cnn_quadtree_encode_img_tflite_highbd(
    YV12_BUFFER_CONFIG *source_frame, AV1_COMMON *cm, int superres_denom,
    int rdmult, const int *quad_split_costs, const int *binary_split_costs,
    int (*norestorecosts)[2], int num_threads, int bit_depth, int is_intra_only,
    int is_luma, int cnn_index, QUADInfo *quad_info, double *rdcost) {
  YV12_BUFFER_CONFIG *dgd_buf = &cm->cur_frame->buf;
  uint16_t *dgd = CONVERT_TO_SHORTPTR(dgd_buf->y_buffer);
  const int dgd_stride = dgd_buf->y_stride;
  const int qindex = cm->quant_params.base_qindex;
  const int width = cm->superres_upscaled_width;
  const int height = cm->superres_upscaled_height;

  // Get 2-channel intermediate restoration.
  std::vector<std::vector<std::vector<double>>> interm(
      height, std::vector<std::vector<double>>(width, std::vector<double>(2)));
  if (!generate_interm_guided_restoration(
          dgd, dgd_stride, qindex, superres_denom, width, height, num_threads,
          is_intra_only, is_luma, cnn_index, bit_depth, interm)) {
    return 0;
  }

  // Initialization.
  const uint16_t *src = CONVERT_TO_SHORTPTR(source_frame->y_buffer);
  const int src_stride = source_frame->y_stride;
  const int *quadtset = get_quadparm_from_qindex(
      qindex, superres_denom, is_intra_only, is_luma, cnn_index);
  const int A0_min = quadtset[2];
  const int A1_min = quadtset[3];
  const int norestore_ctx =
      get_guided_norestore_ctx(qindex, superres_denom, is_intra_only);
  const int null_norestorecosts[2] = { 0, 0 };
  const int *this_norestorecosts =
      norestore_ctx == -1 ? null_norestorecosts : norestorecosts[norestore_ctx];

  // Try all possible quadtree unit sizes.
  int best_unit_index = -1;
  std::vector<int> best_split;              // selected partitioning options.
  std::vector<std::pair<int, int>> best_A;  // selected a0, a1 weight pairs.
  double best_rdcost_total = DBL_MAX;
  for (int this_unit_index = 0; this_unit_index < GUIDED_QT_UNIT_SIZES;
       ++this_unit_index) {
    const int quadtree_max_size =
        quad_tree_get_unit_size(width, height, this_unit_index);
    // For each quadtree unit, compute the best partitioning out of
    // NONE, SPLIT, HORZ and VERT based on RD cost.
    std::vector<int> this_split;              // selected partitioning options.
    std::vector<std::pair<int, int>> this_A;  // selected a0, a1 weight pairs.
    double this_rdcost_total = 0.0;
    // Previous a0, a1 pair is mid-point of the range by default.
    std::pair<int, int> prev_A =
        std::make_pair(GUIDED_A_MID + A0_min, GUIDED_A_MID + A1_min);
    const int ext_size = quadtree_max_size * 3 / 2;
    for (int row = 0; row < height;) {
      const int remaining_height = height - row;
      const int this_unit_height =
          (remaining_height < ext_size) ? remaining_height : quadtree_max_size;
      for (int col = 0; col < width;) {
        const int remaining_width = width - col;
        const int this_unit_width =
            (remaining_width < ext_size) ? remaining_width : quadtree_max_size;
        double this_rdcost;
        select_quadtree_partitioning(
            interm, src, src_stride, row, col, width, height, quadtree_max_size,
            this_unit_width, this_unit_height, quadtset, rdmult, prev_A,
            quad_split_costs, binary_split_costs, this_norestorecosts,
            bit_depth, dgd, dgd_stride, this_split, this_A, &this_rdcost);
        // updates.
        this_rdcost_total += this_rdcost;
        prev_A = this_A.back();
        col += this_unit_width;
      }
      row += this_unit_height;
    }
    // Update best options.
    if (this_rdcost_total < best_rdcost_total) {
      best_unit_index = this_unit_index;
      best_split = this_split;
      best_A = this_A;
      best_rdcost_total = this_rdcost_total;
    }
  }

  // Fill in the best options.
  quad_info->unit_index = best_unit_index;
  quad_info->split_info_length = (int)best_split.size();
  quad_info->unit_info_length = (int)best_A.size();
  av1_alloc_quadtree_struct(cm, quad_info);
  for (unsigned int i = 0; i < best_split.size(); ++i) {
    quad_info->split_info[i].split = best_split[i];
  }
  for (unsigned int i = 0; i < best_A.size(); ++i) {
    quad_info->unit_info[i].xqd[0] = best_A[i].first;
    quad_info->unit_info[i].xqd[1] = best_A[i].second;
  }
  *rdcost = best_rdcost_total;

  // Apply guided restoration to 'dgd' using best options above.
  size_t split_index = 0;
  size_t A_index = 0;
  const int quadtree_max_size = quad_info->unit_size;
  const int ext_size = quadtree_max_size * 3 / 2;
  for (int row = 0; row < height;) {
    const int remaining_height = height - row;
    const int this_unit_height =
        (remaining_height < ext_size) ? remaining_height : quadtree_max_size;
    for (int col = 0; col < width;) {
      const int remaining_width = width - col;
      const int this_unit_width =
          (remaining_width < ext_size) ? remaining_width : quadtree_max_size;
      apply_quadtree_partitioning(
          interm, row, col, width, height, quadtree_max_size, this_unit_width,
          this_unit_height, quadtset, bit_depth, best_split, split_index,
          best_A, A_index, dgd, dgd_stride);
      col += this_unit_width;
    }
    row += this_unit_height;
  }

  return 1;
}

extern "C" int av1_restore_cnn_quadtree_encode_tflite(
    struct AV1Common *cm, YV12_BUFFER_CONFIG *source_frame, int RDMULT,
    int *quad_split_costs, int *binary_split_costs, int (*norestorecosts)[2],
    int num_threads, const int apply_cnn[MAX_MB_PLANE],
    const int cnn_indices[MAX_MB_PLANE], QUADInfo *quad_info, double *rdcost) {
  YV12_BUFFER_CONFIG *buf = &cm->cur_frame->buf;
  const int is_intra_only = frame_is_intra_only(cm);
  for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
    if (!apply_cnn[plane]) continue;
    const int is_luma = (plane == AOM_PLANE_Y);
    const int cnn_index = cnn_indices[plane];
    assert(cnn_index >= 0 &&
           cnn_index < av1_num_cnn_indices_for_plane(cm, plane));
    int ret = 1;
    switch (plane) {
      case AOM_PLANE_Y:
        ret = restore_cnn_quadtree_encode_img_tflite_highbd(
            source_frame, cm, cm->superres_scale_denominator, RDMULT,
            quad_split_costs, binary_split_costs, norestorecosts, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index,
            quad_info, rdcost);
        if (ret == 0) return ret;
        break;
      case AOM_PLANE_U:
        ret = av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        if (ret == 0) return ret;
        break;
      case AOM_PLANE_V:
        ret = av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        if (ret == 0) return ret;
        break;
      default: assert(0 && "Invalid plane index"); return 0;
    }
  }
  return 1;
}

// ------------------- Guided Quadtree: Decoder ------------------------------//

// Given the 2-channel intermediate output in 'interm' and weight parameters,
// restores one quadtree unit in 'dgd'.
static void apply_linear_combination(
    const std::vector<std::vector<std::vector<double>>> &interm, int start_row,
    int end_row, int start_col, int end_col, int unit_width, int unit_height,
    const int *quadtset, int bit_depth,
    const std::vector<std::pair<int, int>> &A, size_t &A_index, uint16_t *dgd,
    int dgd_stride) {
  // Get scale parameters.
  const int scale0 = quadtset[0];
  const int scale1 = quadtset[1];

  for (int row = start_row; row < end_row; row += unit_height) {
    const int this_start_row = row;
    const int this_end_row = AOMMIN(row + unit_height, end_row);
    for (int col = start_col; col < end_col; col += unit_width) {
      const int this_start_col = col;
      const int this_end_col = AOMMIN(col + unit_width, end_col);

      // Get weight parameters for this unit.
      const auto this_A = A[A_index++];
      const int a0 = this_A.first;
      const int a1 = this_A.second;

      // Restore this unit.
      for (int r = this_start_row; r < this_end_row; ++r) {
        for (int c = this_start_col; c < this_end_col; ++c) {
          const int dgd_unclipped = int(round(dgd[r * dgd_stride + c] +
                                              a0 * interm[r][c][0] / scale0 +
                                              a1 * interm[r][c][1] / scale1));
          dgd[r * dgd_stride + c] = clip_pixel_highbd(dgd_unclipped, bit_depth);
        }
      }
    }
  }
}

// Given intermediate restoration 'interm', quadtree partitioning info 'split'
// and weight parameters 'A', restores the unit starting at 'row' and 'col'
// inside 'dgd'.
static void apply_quadtree_partitioning(
    const std::vector<std::vector<std::vector<double>>> &interm, int start_row,
    int start_col, int width, int height, int quadtree_max_size,
    int max_unit_width, int max_unit_height, const int *quadtset, int bit_depth,
    const std::vector<int> &split, size_t &split_index,
    const std::vector<std::pair<int, int>> &A, size_t &A_index, uint16_t *dgd,
    int dgd_stride) {
  const int end_row = AOMMIN(start_row + max_unit_height, height);
  const int end_col = AOMMIN(start_col + max_unit_width, width);
  // Check for special cases near boundary.
  const bool is_horz_partitioning_allowed =
      (max_unit_height >= quadtree_max_size);
  const bool is_vert_partitioning_allowed =
      (max_unit_width >= quadtree_max_size);

  // Get partition type.
  GuidedQuadTreePartitionType partition_type = GUIDED_QT_NONE;
  if (is_horz_partitioning_allowed || is_vert_partitioning_allowed) {
    partition_type = (GuidedQuadTreePartitionType)split[split_index++];
  }
  assert(partition_type >= 0 && partition_type < GUIDED_QT_TYPES);

  // Get unit width and height based on partition type.
  int unit_width;
  int unit_height;
  get_unit_size(max_unit_width, max_unit_height, partition_type, &unit_width,
                &unit_height);

  // Compute restored unit, a0 and a1 with given A parameters.
  apply_linear_combination(interm, start_row, end_row, start_col, end_col,
                           unit_width, unit_height, quadtset, bit_depth, A,
                           A_index, dgd, dgd_stride);
}

// Top-level function to apply guided restoration on decoder side.
static int restore_cnn_quadtree_decode_img_tflite_highbd(
    AV1_COMMON *cm, int superres_denom, int num_threads, int bit_depth,
    int is_intra_only, int is_luma, int cnn_index) {
  YV12_BUFFER_CONFIG *dgd_buf = &cm->cur_frame->buf;
  uint16_t *dgd = CONVERT_TO_SHORTPTR(dgd_buf->y_buffer);
  const int dgd_stride = dgd_buf->y_stride;
  const int qindex = cm->quant_params.base_qindex;
  const int width = cm->superres_upscaled_width;
  const int height = cm->superres_upscaled_height;

  // Get 2-channel intermediate restoration.
  std::vector<std::vector<std::vector<double>>> interm(
      height, std::vector<std::vector<double>>(width, std::vector<double>(2)));
  if (!generate_interm_guided_restoration(
          dgd, dgd_stride, qindex, superres_denom, width, height, num_threads,
          is_intra_only, is_luma, cnn_index, bit_depth, interm)) {
    return 0;
  }

  // Get quadtree params.
  const QUADInfo *const quad_info = &cm->cnn_quad_info;
  const int quadtree_max_size = quad_info->unit_size;
  const int *quadtset = get_quadparm_from_qindex(
      qindex, superres_denom, is_intra_only, is_luma, cnn_index);

  // Get partitioning types.
  std::vector<int> split;
  split.reserve(quad_info->split_info_length);
  for (int i = 0; i < quad_info->split_info_length; ++i) {
    split.push_back(quad_info->split_info[i].split);
  }

  // Get a0,a1 pairs.
  std::vector<std::pair<int, int>> A;
  A.reserve(quad_info->unit_info_length);
  for (int i = 0; i < quad_info->unit_info_length; ++i) {
    A.emplace_back(quad_info->unit_info[i].xqd[0],
                   quad_info->unit_info[i].xqd[1]);
  }

  // For each quadtree unit, apply given quadtree partitioning.
  size_t split_index = 0;
  size_t A_index = 0;
  const int ext_size = quadtree_max_size * 3 / 2;
  for (int row = 0; row < height;) {
    const int remaining_height = height - row;
    const int this_unit_height =
        (remaining_height < ext_size) ? remaining_height : quadtree_max_size;
    for (int col = 0; col < width;) {
      const int remaining_width = width - col;
      const int this_unit_width =
          (remaining_width < ext_size) ? remaining_width : quadtree_max_size;
      apply_quadtree_partitioning(interm, row, col, width, height,
                                  quadtree_max_size, this_unit_width,
                                  this_unit_height, quadtset, bit_depth, split,
                                  split_index, A, A_index, dgd, dgd_stride);
      col += this_unit_width;
    }
    row += this_unit_height;
  }
  assert(split_index == split.size());
  assert(A_index == A.size());
  return 1;
}

extern "C" int av1_restore_cnn_quadtree_decode_tflite(
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
    int ret = 1;
    switch (plane) {
      case AOM_PLANE_Y:
        ret = restore_cnn_quadtree_decode_img_tflite_highbd(
            cm, cm->superres_scale_denominator, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        if (ret == 0) return ret;
        break;
      case AOM_PLANE_U:
        ret = av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->u_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        if (ret == 0) return ret;
        break;
      case AOM_PLANE_V:
        ret = av1_restore_cnn_img_tflite_highbd(
            cm->quant_params.base_qindex, cm->superres_scale_denominator,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_crop_width,
            buf->uv_crop_height, buf->uv_stride,
            CONVERT_TO_SHORTPTR(buf->v_buffer), buf->uv_stride, num_threads,
            cm->seq_params.bit_depth, is_intra_only, is_luma, cnn_index);
        if (ret == 0) return ret;
        break;
      default: assert(0 && "Invalid plane index"); return 0;
    }
  }
  return 1;
}
#endif  // CONFIG_CNN_GUIDED_QUADTREE

#endif  // CONFIG_CNN_RESTORATION
