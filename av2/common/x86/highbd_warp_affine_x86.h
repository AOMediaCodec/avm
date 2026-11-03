/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_COMMON_X86_HIGHBD_WARP_AFFINE_X86_H_
#define AVM_AV2_COMMON_X86_HIGHBD_WARP_AFFINE_X86_H_

#include <immintrin.h>

#include "avm/avm_integer.h"
#include "avm_ports/mem.h"
#include "av2/common/convolve.h"

// Shared by AVX2 / AVX-512 warp; 64-byte aligned for both.

DECLARE_ALIGNED(64, static const uint8_t, warp_highbd_shuffle_pattern[32]) = {
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

DECLARE_ALIGNED(64, static const uint8_t, warp_highbd_arrange_bytes[32]) = {
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15
};

DECLARE_ALIGNED(64, static const uint8_t, shuffle_input_mask[32]) = {
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15
};

DECLARE_ALIGNED(64, static const uint8_t, shuffle_alpha0_gamma0_mask0[32]) = {
  0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3
};

DECLARE_ALIGNED(64, static const uint8_t, shuffle_alpha0_gamma0_mask1[32]) = {
  4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7,
  4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7
};

DECLARE_ALIGNED(64, static const uint8_t, shuffle_alpha0_gamma0_mask2[32]) = {
  8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11,
  8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11
};

DECLARE_ALIGNED(64, static const uint8_t, shuffle_alpha0_gamma0_mask3[32]) = {
  12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15,
  12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15
};

// Vertical store for a row pair; shared by AVX2 and AVX-512 (YMM only).
// conv_params fields by value: avoids reloads after aliasing stores.
static INLINE void highbd_warp_store_vertical_2rows(
    uint16_t *pred, int p_stride, const int is_compound, const int do_average,
    CONV_BUF_TYPE *const dst, const int dst_stride, const __m256i *res_lo,
    const __m256i *res_hi, const __m256i *res_add_const,
    const __m256i *reduce_bits_vert_const, const int use_wtd_comp_avg,
    const __m256i *wt0, const __m256i *wt1, const __m256i *res_sub_const,
    const __m256i *round_bits_const, int i, int j, int k,
    const int reduce_bits_vert, const int round_bits, const __m256i clip_pixel,
    const __m256i zero, const __m256i round_const_single,
    const __m256i v_input_mask) {
  __m256i v_sum = *res_lo;
  __m256i v_sum_r1 = *res_hi;

  if (is_compound) {
    __m128i *const p = (__m128i *)&dst[(i + k + 4) * dst_stride + j];
    __m128i *const p_r1 = (__m128i *)&dst[(i + k + 5) * dst_stride + j];

    v_sum = _mm256_add_epi32(v_sum, *res_add_const);
    v_sum = _mm256_srai_epi32(_mm256_add_epi32(v_sum, *reduce_bits_vert_const),
                              reduce_bits_vert);
    v_sum_r1 = _mm256_add_epi32(v_sum_r1, *res_add_const);
    v_sum_r1 = _mm256_srai_epi32(
        _mm256_add_epi32(v_sum_r1, *reduce_bits_vert_const), reduce_bits_vert);
    if (do_average) {
      __m128i *const dst16 = (__m128i *)&pred[(i + k + 4) * p_stride + j];
      __m128i *const dst16_r1 = (__m128i *)&pred[(i + k + 5) * p_stride + j];
      __m256i p_32 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p));
      __m256i p_32_r1 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p_r1));
      v_sum = _mm256_shuffle_epi32(_mm256_permute4x64_epi64(v_sum, 0xD8), 0xD8);
      v_sum_r1 =
          _mm256_shuffle_epi32(_mm256_permute4x64_epi64(v_sum_r1, 0xD8), 0xD8);

      if (use_wtd_comp_avg) {
        v_sum = _mm256_add_epi32(_mm256_mullo_epi32(p_32, *wt0),
                                 _mm256_mullo_epi32(v_sum, *wt1));
        v_sum = _mm256_srai_epi32(v_sum, DIST_PRECISION_BITS);

        v_sum_r1 = _mm256_add_epi32(_mm256_mullo_epi32(p_32_r1, *wt0),
                                    _mm256_mullo_epi32(v_sum_r1, *wt1));
        v_sum_r1 = _mm256_srai_epi32(v_sum_r1, DIST_PRECISION_BITS);
      } else {
        v_sum = _mm256_srai_epi32(_mm256_add_epi32(p_32, v_sum), 1);
        v_sum_r1 = _mm256_srai_epi32(_mm256_add_epi32(p_32_r1, v_sum_r1), 1);
      }

      __m256i v_sum1 = _mm256_add_epi32(v_sum, *res_sub_const);
      v_sum1 = _mm256_srai_epi32(_mm256_add_epi32(v_sum1, *round_bits_const),
                                 round_bits);
      __m256i v_sum1_r1 = _mm256_add_epi32(v_sum_r1, *res_sub_const);
      v_sum1_r1 = _mm256_srai_epi32(
          _mm256_add_epi32(v_sum1_r1, *round_bits_const), round_bits);

      __m256i v_sum16 = _mm256_packus_epi32(v_sum1, v_sum1_r1);
      v_sum16 =
          _mm256_permute4x64_epi64(_mm256_min_epi16(v_sum16, clip_pixel), 0xD8);
      _mm_storeu_si128(dst16, _mm256_castsi256_si128(v_sum16));
      _mm_storeu_si128(dst16_r1, _mm256_extracti128_si256(v_sum16, 1));
    } else {
      v_sum = _mm256_packus_epi32(v_sum, v_sum_r1);
      __m256i v_sum16 = _mm256_shuffle_epi8(
          _mm256_permute4x64_epi64(v_sum, 0xD8), v_input_mask);
      _mm_storeu_si128(p, _mm256_castsi256_si128(v_sum16));
      _mm_storeu_si128(p_r1, _mm256_extracti128_si256(v_sum16, 1));
    }
  } else {
    __m256i v_sum1 = _mm256_srai_epi32(
        _mm256_add_epi32(v_sum, round_const_single), reduce_bits_vert);
    __m256i v_sum1_r1 = _mm256_srai_epi32(
        _mm256_add_epi32(v_sum_r1, round_const_single), reduce_bits_vert);

    v_sum1 = _mm256_packus_epi32(v_sum1, v_sum1_r1);
    v_sum1 = _mm256_permute4x64_epi64(v_sum1, 0xD8);
    __m256i v_sum16 = _mm256_shuffle_epi8(v_sum1, v_input_mask);
    v_sum16 = _mm256_max_epi16(_mm256_min_epi16(v_sum16, clip_pixel), zero);
    __m128i *const p = (__m128i *)&pred[(i + k + 4) * p_stride + j];
    __m128i *const p_r1 = (__m128i *)&pred[(i + k + 5) * p_stride + j];

    _mm_storeu_si128(p, _mm256_castsi256_si128(v_sum16));
    _mm_storeu_si128(p_r1, _mm256_extracti128_si256(v_sum16, 1));
  }
}

#endif  // AVM_AV2_COMMON_X86_HIGHBD_WARP_AFFINE_X86_H_
