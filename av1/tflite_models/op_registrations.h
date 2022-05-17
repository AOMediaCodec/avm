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

#ifndef AOM_AV1_TFLITE_MODELS_OP_REGISTRATIONS_H_
#define AOM_AV1_TFLITE_MODELS_OP_REGISTRATIONS_H_

#include <assert.h>

// TensorFlow Lite has several unused parameters that are
// exposed as part of the API. In the AOM build process, this
// will cause failures when -Wunused-parameter is set.
// Since TF Lite is external code, instruct the compiler to
// ignore this warning when including it.
// Note that Clang supports this GCC pragma.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "tensorflow/lite/kernels/builtin_op_kernels.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/op_resolver.h"

#pragma GCC diagnostic pop

// Returns super-set of TF-lite ops required by CNN models for all QPs.
void RegisterSelectedOpsAllQps(::tflite::MutableOpResolver *resolver);

#endif  // AOM_AV1_TFLITE_MODELS_OP_REGISTRATIONS_H_
