/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_COMMON_TF_LITE_INCLUDES_H_
#define AOM_COMMON_TF_LITE_INCLUDES_H_

#include <assert.h>

// TensorFlow Lite has several unused parameters that are
// exposed as part of the API. In the AOM build process, this
// will cause failures when -Wunused-parameter is set.
// Since TF Lite is external code, instruct the compiler to
// ignore this warning when including it.
// Note that Clang supports this GCC pragma.
#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wcomment"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunused-parameter"
#pragma clang diagnostic warning "-Wcomment"
#endif

#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#include "tensorflow/lite/kernels/builtin_op_kernels.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/op_resolver.h"

#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif  // AOM_COMMON_TF_LITE_INCLUDES_H_
