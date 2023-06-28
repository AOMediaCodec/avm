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

#ifndef INCLUDE_LIBYUV_ROTATE_ARGB_H_  // NOLINT
#define INCLUDE_LIBYUV_ROTATE_ARGB_H_

#include "libyuv/basic_types.h"
#include "libyuv/rotate.h"  // For RotationMode.

#ifdef __cplusplus
namespace libyuv {
extern "C" {
#endif

// Rotate ARGB frame
LIBYUV_API
int ARGBRotate(const uint8 *src_argb, int src_stride_argb, uint8 *dst_argb,
               int dst_stride_argb, int src_width, int src_height,
               enum RotationMode mode);

#ifdef __cplusplus
}  // extern "C"
}  // namespace libyuv
#endif

#endif  // INCLUDE_LIBYUV_ROTATE_ARGB_H_  NOLINT
