/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <cstdio>
#include <memory>

#include "aom_dsp/aom_dsp_common.h"
#include "av1/common/av1_common_int.h"

extern "C" {
#include "av1/common/intra_matrix.h"
}

#include "av1/common/intra_dip.h"

#define INPUT_FEATURES 11

// Resample output from DIP prediction (8x8) to the actual transform
// block size. This is some by upsampling if dimension is > 8, or
// downsampling/decimation if dimension is < 8. Resampling is done
// in the horizontal dimension first followed by vertical.
static void resample_output(uint16_t *dst, int dst_stride,
                            const uint16_t *above_row, const uint16_t *left_col,
                            uint16_t *ml_output, int bw_log2, int bh_log2,
                            int transpose) {
  // up/down sampling factors
  int pred_x = 8;
  int pred_y = 8;
  int upx_log2 = bw_log2 - 3;
  int upy_log2 = bh_log2 - 3;
  int downx_log2 = 0;
  int downy_log2 = 0;
  if (upx_log2 < 0) {
    downx_log2 = -upx_log2;
    upx_log2 = 0;
  }
  if (upy_log2 < 0) {
    downy_log2 = -upy_log2;
    upy_log2 = 0;
  }
  int mx = 1 << upx_log2;
  int my = 1 << upy_log2;
  int downx = 1 << downx_log2;
  int downy = 1 << downy_log2;
  int bw = 1 << bw_log2;
  // Copy ml_output[] into dst[]
  for (int i = 0; i < pred_y >> downy_log2; i++) {
    for (int j = 0; j < pred_x >> downx_log2; j++) {
      int x = j * mx + (mx - 1);
      int y = i * my + (my - 1);
      int i1 = i * downy;
      int j1 = j * downx;
      int ii = transpose ? j1 : i1;
      int jj = transpose ? i1 : j1;
      dst[y * dst_stride + x] = ml_output[ii * pred_x + jj];
    }
  }
  // Interpolate horizontally.
  for (int i = 0; i < pred_y >> downy_log2; i++) {
    int y = i * my + (my - 1);
    int p0 = 0;
    int p1 = left_col[y];
    for (int j = 0; j < pred_x >> downx_log2; j++) {
      int x = j * mx;
      p0 = p1;
      p1 = dst[y * dst_stride + x + mx - 1];
      for (int k = 0; k < mx - 1; k++) {
        int k1 = k + 1;
        dst[y * dst_stride + x + k] = (p0 * (mx - k1) + (p1 * k1)) >> upx_log2;
      }
    }
  }
  // Interpolate vertically.
  for (int x = 0; x < bw; x++) {
    int p0 = 0;
    int p1 = above_row[x];
    for (int i = 0; i < pred_y >> downy_log2; i++) {
      int y = i * my;
      p0 = p1;
      p1 = dst[(y + my - 1) * dst_stride + x];
      for (int k = 0; k < my - 1; k++) {
        int k1 = k + 1;
        dst[(y + k) * dst_stride + x] =
            (p0 * (my - k1) + (p1 * k1)) >> upy_log2;
      }
    }
  }
}

// Create intra ML prediction for large blocks
extern "C" void av1_highbd_intra_dip_predictor(int mode, uint16_t *dst,
                                               int dst_stride,
                                               const uint16_t *above_row,
                                               const uint16_t *left_col,
                                               TX_SIZE tx_size, int bd) {
  uint16_t ml_input[INPUT_FEATURES];
  uint16_t ml_output[64];
  const int bw_log2 = tx_size_wide_log2[tx_size];
  const int bh_log2 = tx_size_high_log2[tx_size];
  int transpose = mode >> 4;
  int iml_mode = mode & 15;
  int down_x_log2 = bw_log2 - 2;
  int down_y_log2 = bh_log2 - 2;
  const int down0_log2 = transpose ? down_y_log2 : down_x_log2;
  const int down1_log2 = transpose ? down_x_log2 : down_y_log2;
  const int down0 = 1 << down0_log2;
  const int down1 = 1 << down1_log2;
  int rnd0 = down0 >> 1;
  int rnd1 = down1 >> 1;
  const uint16_t *edge0 = transpose ? left_col : above_row;
  const uint16_t *edge1 = transpose ? above_row : left_col;

  int ml_i = 0;
  // Corner
  ml_input[ml_i++] = edge0[-1];

  // Calc above input
  for (int i = 0; i < 4; i++) {
    int sum = 0;
    for (int j = 0; j < down0; j++) {
      sum += *edge0++;
    }
    ml_input[ml_i++] = (sum + rnd0) >> down0_log2;
  }

  // Calc left input
  for (int i = 0; i < 4; i++) {
    int sum = 0;
    for (int j = 0; j < down1; j++) {
      sum += *edge1++;
    }
    ml_input[ml_i++] = (sum + rnd1) >> down1_log2;
  }

  int n_overhang = 1;
  // Calc above_left input
  for (int i = 0; i < n_overhang; i++) {
    int sum = 0;
    for (int j = 0; j < down0; j++) {
      sum += *edge0++;
    }
    ml_input[ml_i++] = (sum + rnd0) >> down0_log2;
  }

  // Calc bottom_left input
  for (int i = 0; i < n_overhang; i++) {
    int sum = 0;
    for (int j = 0; j < down1; j++) {
      sum += *edge1++;
    }
    ml_input[ml_i++] = (sum + rnd1) >> down1_log2;
  }

  av1_intra_matrix_pred(ml_input, iml_mode, ml_output, bd);

  resample_output(dst, dst_stride, above_row, left_col, ml_output, bw_log2,
                  bh_log2, transpose);
}
