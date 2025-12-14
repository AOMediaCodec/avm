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

#ifndef AVM_TEST_UTIL_H_
#define AVM_TEST_UTIL_H_

#include <stdio.h>
#include <math.h>
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "avm/avm_image.h"
#include "avm_ports/avm_timer.h"

// Macros
#define GET_PARAM(k) std::get<k>(GetParam())

// Same as 'avm_sse_to_psnr'.
inline double sse_to_psnr(double samples, double peak, double sse) {
  static const double kMinSSE = 0.5;
  const bool zero_sse = (sse < kMinSSE);
  if (zero_sse) sse = kMinSSE;
  assert(sse > 0.0);
  double psnr = 10.0 * log10(samples * peak * peak / sse);
  if (zero_sse) psnr = ceil(psnr);
  return psnr;
}

inline double compute_psnr(const avm_image_t *img1, const avm_image_t *img2) {
  assert((img1->fmt == img2->fmt) && (img1->d_w == img2->d_w) &&
         (img1->d_h == img2->d_h));

  const unsigned int width_y = img1->d_w;
  const unsigned int height_y = img1->d_h;

  double sse = 0;
  for (unsigned int i = 0; i < height_y; ++i) {
    for (unsigned int j = 0; j < width_y; ++j) {
      const double d =
          img1->planes[AVM_PLANE_Y][i * img1->stride[AVM_PLANE_Y] + j] -
          img2->planes[AVM_PLANE_Y][i * img2->stride[AVM_PLANE_Y] + j];
      sse += d * d;
    }
  }
  return sse_to_psnr(width_y * height_y, 255.0, sse);
}

// Returns the expected total PSNR for the zero distortion case, based on frame
// dimensions.
// If `is_yuv444` is true: assumes YUV4:4:4 format, otherwise assumes YUV4:2:0.
inline double get_lossless_psnr(unsigned int width, unsigned int height,
                                unsigned int bit_depth, bool is_yuv444) {
  const double peak = (double)(255 << (bit_depth - 8));
  const double y_samples = width * height;
  const double uv_samples = is_yuv444 ? 2 * y_samples : 2 * y_samples / 4;
  return sse_to_psnr(y_samples + uv_samples, peak, 0);
}

static INLINE double get_time_mark(avm_usec_timer *t) {
  avm_usec_timer_mark(t);
  return static_cast<double>(avm_usec_timer_elapsed(t));
}

#endif  // AVM_TEST_UTIL_H_
