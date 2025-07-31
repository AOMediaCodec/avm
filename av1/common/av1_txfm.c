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

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "av1/common/av1_txfm.h"

/*!
 * Secondary transform coeffs in 32-bit precision
 * 16-bit buffers are sufficient to store the secondary transform kernels of
 * 4x4 and 8x8. However, the 16-bit kernel is converted and maintained in a
 * 32-bit buffer to improve the performance of load operation in x86 SIMD.
 */
#if CONFIG_IST_ANY_SET
#if CONFIG_F105_IST_MEM_REDUCE
DECLARE_ALIGNED(
    32, int32_t,
    ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1][16][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT_MAX][IST_8x8_WIDTH]);
#else
DECLARE_ALIGNED(32, int32_t,
                ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_4x4_HEIGHT][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT_MAX][IST_8x8_WIDTH]);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else   // CONFIG_IST_ANY_SET
DECLARE_ALIGNED(32, int32_t,
                ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_4x4_HEIGHT][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT][IST_8x8_WIDTH]);
#endif  // CONFIG_IST_ANY_SET

void av1_init_stxfm_kernels(void) {
  const int set_max = IST_SET_SIZE;
  const int type_max = STX_TYPES - 1;

#if CONFIG_IST_ANY_SET
#if CONFIG_F105_IST_MEM_REDUCE

  const int row_max_4x4 = 16;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT_MAX;
  const int col_max_8x8 = IST_8x8_WIDTH;

#else
  const int row_max_4x4 = IST_4x4_HEIGHT;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT_MAX;
  const int col_max_8x8 = IST_8x8_WIDTH;

#endif  // CONFIG_F105_IST_MEM_REDUCE
#else   // CONFIG_IST_ANY_SET

  const int row_max_4x4 = IST_4x4_HEIGHT;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT;
  const int col_max_8x8 = IST_8x8_WIDTH;

#endif  // CONFIG_IST_ANY_SET
  for (int set = 0; set < set_max; ++set) {
    for (int type = 0; type < type_max; ++type) {
      for (int row = 0; row < row_max_4x4; ++row) {
        for (int col = 0; col < col_max_4x4; ++col) {
          ist_4x4_kernel_int32[set][type][row][col] =
              (int32_t)ist_4x4_kernel[set][type][row][col];
        }
      }
      for (int row = 0; row < row_max_8x8; ++row) {
        for (int col = 0; col < col_max_8x8; ++col) {
          ist_8x8_kernel_int32[set][type][row][col] =
              (int32_t)ist_8x8_kernel[set][type][row][col];
        }
      }
    }
  }
}

// Given a rotation angle t, the CCTX transform matrix is defined as
// [cos(t), sin(t); -sin(t), cos(t)] * 1<<CCTX_PREC_BITS). The array below only
// stores two values: cos(t) and sin(t) for each rotation angle.
const int32_t cctx_mtx[CCTX_TYPES - 1][2] = {
  { 181, 181 },   // t = 45 degrees
  { 222, 128 },   // t = 30 degrees
  { 128, 222 },   // t = 60 degrees
  { 181, -181 },  // t = -45 degrees
  { 222, -128 },  // t = -30 degrees
  { 128, -222 },  // t = -60 degrees
};
