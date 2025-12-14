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
#include <smmintrin.h> /* SSE4.1 */

#include "av2/common/common_data.h"
#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/av2_txfm.h"
#include "avm_dsp/txfm_common.h"
#include "avm_dsp/x86/txfm_common_sse2.h"
#include "avm_ports/mem.h"

// Forward secondary transform
void fwd_stxfm_sse4_1(tran_low_t *src, tran_low_t *dst,
                      const PREDICTION_MODE mode, const uint8_t stx_idx,
                      const int size, const int bd) {
  // Secondary transform kernels are stored as 32-bit integers to match SIMD
  // processing needs. This avoids on-the-fly conversion from int16_t to int32_t
  // during execution by letting SIMD variants directly load the pre-converted
  // filter weights.
  assert(stx_idx < 4);
  const int32_t *kernel = (size == 0) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];
  int coef;
  int *out = dst;
  int shift = 7;

  int reduced_width, reduced_height;
  if (size == 0) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = (size == 1)
                         ? IST_8x8_HEIGHT_RED
                         : ((size == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
    reduced_width = IST_8x8_WIDTH;
  }
  for (int j = 0; j < reduced_height; j++) {
    int *srcPtr = src;
    const int32_t *kernel_tmp = kernel;
    __m128i tmpSum = _mm_setzero_si128();
    for (int i = 0; i < reduced_width; i += 4) {
      __m128i tmpBlk = _mm_loadu_si128((__m128i *)(srcPtr + i));
      __m128i tmpT = _mm_loadu_si128((__m128i *)(kernel_tmp + i));
      tmpSum = _mm_add_epi32(tmpSum, _mm_mullo_epi32(tmpBlk, tmpT));
    }
    tmpSum = _mm_add_epi32(tmpSum,
                           _mm_shuffle_epi32(tmpSum, _MM_SHUFFLE(2, 3, 0, 1)));
    tmpSum = _mm_add_epi32(tmpSum,
                           _mm_shuffle_epi32(tmpSum, _MM_SHUFFLE(1, 0, 3, 2)));
    coef = _mm_cvtsi128_si32(tmpSum);
    *out++ = clamp_value(ROUND_POWER_OF_TWO_SIGNED(coef, shift), 8 + bd);
    kernel += reduced_width;
  }
}
