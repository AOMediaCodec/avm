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

#include <assert.h>

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/txfm_common.h"
#include "av1/common/enums.h"
#include "av1/common/av1_txfm.h"

void fwd_stxfm_c(tran_low_t *src, tran_low_t *dst, const PREDICTION_MODE mode,
                 const uint8_t stx_idx, const int size, const int bd) {
  assert(stx_idx < 4);
#if CONFIG_E124_IST_REDUCE_METHOD4
  const int16_t *kernel = (size == 0) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
#else
  const int16_t *kernel = (size == 4) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
  int coef;
  int *out = dst;
  int shift = 7;

  int reduced_width, reduced_height;
#if CONFIG_E124_IST_REDUCE_METHOD4
  if (size == 0) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
#if CONFIG_F105_IST_MEM_REDUCE
    reduced_height = (size == 1)
                         ? IST_8x8_HEIGHT_RED
                         : ((size == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
#else
    reduced_height = (size == 1) ? IST_8x8_HEIGHT_RED : IST_8x8_HEIGHT;
#endif  // CONFIG_F105_IST_MEM_REDUCE
    reduced_width = IST_8x8_WIDTH;
  }
#else
  if (size == 4) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = IST_8x8_HEIGHT;
    reduced_width = IST_8x8_WIDTH;
  }
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
  for (int j = 0; j < reduced_height; j++) {
    int *srcPtr = src;
    const int16_t *kernel_tmp = kernel;
    coef = 0;
    for (int i = 0; i < reduced_width; i++) {
      coef += *srcPtr++ * *kernel_tmp++;
    }
    *out++ = clamp_value(ROUND_POWER_OF_TWO_SIGNED(coef, shift), 8 + bd);
    kernel += reduced_width;
  }
}
