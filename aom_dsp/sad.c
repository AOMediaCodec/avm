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

#include <stdlib.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_dsp/blend.h"

static INLINE unsigned int highbd_sad(const uint16_t *a, int a_stride,
                                      const uint16_t *b, int b_stride,
                                      int width, int height) {
  int y, x;
  unsigned int sad = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      sad += abs(a[x] - b[x]);
    }

    a += a_stride;
    b += b_stride;
  }
  return sad;
}

#define highbd_sadMxN(m, n)                                     \
  unsigned int aom_highbd_sad##m##x##n##_c(                     \
      const uint16_t *src, int src_stride, const uint16_t *ref, \
      int ref_stride) {                                         \
    return highbd_sad(src, src_stride, ref, ref_stride, m, n);  \
  }

// 128x128
highbd_sadMxN(128, 128);

// 128x64
highbd_sadMxN(128, 64);

// 64x128
highbd_sadMxN(64, 128);

// 64x64
highbd_sadMxN(64, 64);

// 64x32
highbd_sadMxN(64, 32);

// 32x64
highbd_sadMxN(32, 64);

// 32x32
highbd_sadMxN(32, 32);

// 32x16
highbd_sadMxN(32, 16);

// 16x32
highbd_sadMxN(16, 32);

// 16x16
highbd_sadMxN(16, 16);

// 16x8
highbd_sadMxN(16, 8);

// 8x16
highbd_sadMxN(8, 16);

// 8x8
highbd_sadMxN(8, 8);

// 8x4
highbd_sadMxN(8, 4);

// 4x8
highbd_sadMxN(4, 8);

// 4x4
highbd_sadMxN(4, 4);

highbd_sadMxN(4, 16);
highbd_sadMxN(16, 4);
highbd_sadMxN(8, 32);
highbd_sadMxN(32, 8);
highbd_sadMxN(16, 64);
highbd_sadMxN(64, 16);
