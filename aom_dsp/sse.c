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

/* Sum the difference between every corresponding element of the buffers. */

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"

int64_t aom_highbd_sse_c(const uint16_t *a, int a_stride, const uint16_t *b,
                         int b_stride, int width, int height) {
  int y, x;
  int64_t sse = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const int32_t diff = (int32_t)(a[x]) - (int32_t)(b[x]);
      sse += diff * diff;
    }

    a += a_stride;
    b += b_stride;
  }
  return sse;
}

#if CONFIG_MRSSE

// Applies a Mean removed SSE (MRSSE) to the every corresponding element of the
// buffers to calculate distortion of the block.
//
// The original MRSSE calculates a formula of [(a - b - mean)^2] for each
// element. The above formula is summarized as [sse - sum^2 / (w * h)].
int64_t aom_highbd_mrsse_c(const uint16_t *a, int a_stride, const uint16_t *b,
                           int b_stride, int width, int height) {
  int y, x;
  int64_t sse = 0;
  int64_t sum = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const int32_t diff = (int32_t)(a[x]) - (int32_t)(b[x]);
      sum += diff;
      sse += diff * diff;
    }

    a += a_stride;
    b += b_stride;
  }
  return sse - ((sum * sum) / (width * height));
}

#endif  // CONFIG_MRSSE
