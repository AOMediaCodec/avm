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

void aom_highbd_subtract_block_c(int rows, int cols, int16_t *diff,
                                 ptrdiff_t diff_stride, const uint16_t *src,
                                 ptrdiff_t src_stride, const uint16_t *pred,
                                 ptrdiff_t pred_stride, int bd) {
  int r, c;
  (void)bd;

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      diff[c] = src[c] - pred[c];
    }

    diff += diff_stride;
    pred += pred_stride;
    src += src_stride;
  }
}

#if CONFIG_LOSSLESS_DPCM
// perform vertical subtraction for DPCM lossless mode
void aom_highbd_subtract_block_vert_c(int rows, int cols, int16_t *diff,
                                      ptrdiff_t diff_stride,
                                      const uint16_t *src, ptrdiff_t src_stride,
                                      const uint16_t *pred,
                                      ptrdiff_t pred_stride, int bd) {
  int r, c;
  (void)bd;
  const uint16_t *src_delay = src;
  const uint16_t *pred_delay = pred;

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      if (r == 0) {
        diff[c] = src[c] - pred[c];
      } else {
        diff[c] = src[c] - pred[c] - (src_delay[c] - pred_delay[c]);
      }
    }

    diff += diff_stride;
    pred += pred_stride;
    src += src_stride;
    if (r != 0) {
      pred_delay += pred_stride;
      src_delay += src_stride;
    }
  }
}

// perform horizontal subtraction for DPCM lossless mode
void aom_highbd_subtract_block_horz_c(int rows, int cols, int16_t *diff,
                                      ptrdiff_t diff_stride,
                                      const uint16_t *src, ptrdiff_t src_stride,
                                      const uint16_t *pred,
                                      ptrdiff_t pred_stride, int bd) {
  int r, c;
  (void)bd;

  if (cols != rows) {
    assert(rows != cols);
  }

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      if (c == 0) {
        diff[c] = src[c] - pred[c];
      } else {
        diff[c] = (src[c] - pred[c]) - (src[c - 1] - pred[c - 1]);
      }
    }

    diff += diff_stride;
    pred += pred_stride;
    src += src_stride;
  }
}
#endif
