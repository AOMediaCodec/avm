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

#include "config/aom_config.h"
#include "config/av1_rtcd.h"
#include "av1/common/idct.h"

void av1_highbd_inv_txfm_add_sse4_1(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  inv_txfm_c(input, dest, stride, txfm_param);
}

// 128bit intrinsic implementation of ROUND_POWER_OF_TWO_SIGNED.
static INLINE __m128i round_power_of_two_signed_epi32(__m128i val, int bits) {
  const __m128i v_bias_d = _mm_set1_epi32((1 << bits) >> 1);
  const __m128i v_sign_d = _mm_srai_epi32(val, 31);
  const __m128i v_tmp_d = _mm_add_epi32(_mm_add_epi32(val, v_bias_d), v_sign_d);
  return _mm_srai_epi32(v_tmp_d, bits);
}

// Inverse secondary transform
void inv_stxfm_sse4_1(tran_low_t *src, tran_low_t *dst,
                      const PREDICTION_MODE mode, const uint8_t stx_idx,
                      const int size, const int bd) {
  // Secondary transform kernels are stored as 32-bit integers to match SIMD
  // processing needs. This avoids on-the-fly conversion from int16_t to int32_t
  // during execution by letting SIMD variants directly load the pre-converted
  // filter weights.
  assert(stx_idx < 4);
  const int32_t *kernel = (size == 0) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];

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
    const int32_t *kernel_tmp = kernel;
    int *srcPtr = src;
    int *out = dst;
    __m128i tmpCoeff = _mm_set1_epi32(srcPtr[j]);
    __m128i *tmpBlock = (__m128i *)out;
    for (int i = 0; i < reduced_width; i += 4, tmpBlock++) {
      __m128i tmp = _mm_loadu_si128((__m128i *)(kernel_tmp + i));
      __m128i sum = _mm_loadu_si128(tmpBlock);
      tmp = _mm_mullo_epi32(tmpCoeff, tmp);
      tmp = _mm_add_epi32(tmp, sum);
      _mm_storeu_si128(tmpBlock, tmp);
    }
    kernel += reduced_width;
  }
  int *out = dst;
  __m128i *tmpBlock = (__m128i *)out;
  const __m128i max_value = _mm_set1_epi32((1 << (7 + bd)) - 1);
  const __m128i min_value = _mm_set1_epi32(-(1 << (7 + bd)));
  for (int j = 0; j < reduced_width; j += 4, tmpBlock++) {
    __m128i tmp = _mm_loadu_si128(tmpBlock);
    tmp = round_power_of_two_signed_epi32(tmp, 7);
    tmp = _mm_min_epi32(_mm_max_epi32(tmp, min_value), max_value);
    _mm_storeu_si128(tmpBlock, tmp);
  }
}
