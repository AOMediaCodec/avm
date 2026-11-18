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

#ifndef AVM_AV2_COMMON_X86_HIGHBD_JNT_CONVOLVE_AVX2_H_
#define AVM_AV2_COMMON_X86_HIGHBD_JNT_CONVOLVE_AVX2_H_

#include <immintrin.h>

#include "av2/common/convolve.h"

#ifdef __cplusplus
extern "C" {
#endif

static INLINE void highbd_cwp_convolve_2d_copy_do_average(
    __m256i data, __m256i src, const __m256i *weights, const __m256i *c3,
    int fused_shift, __m256i clip_pixel_to_bd, __m256i *res_clip) {
  // madd_epi16 requires signed 16-bit lanes. D - 32768 maps unsigned D to a
  // signed lane. Hence the fused equation becomes
  // clip((w0*(D-32768) + (w1*2^b)*s + fused_c3) >> (b+4)).
  // See highbd_cwp_copy_scale() for fused_c3.
  const __m256i bias = _mm256_set1_epi16((short)0x8000);
  const __m256i d_signed = _mm256_xor_si256(data, bias);
  const __m256i lo_pairs = _mm256_unpacklo_epi16(d_signed, src);
  const __m256i hi_pairs = _mm256_unpackhi_epi16(d_signed, src);
  const __m256i lo = _mm256_srai_epi32(
      _mm256_add_epi32(_mm256_madd_epi16(lo_pairs, *weights), *c3),
      fused_shift);
  const __m256i hi = _mm256_srai_epi32(
      _mm256_add_epi32(_mm256_madd_epi16(hi_pairs, *weights), *c3),
      fused_shift);
  *res_clip = _mm256_min_epu16(_mm256_packus_epi32(lo, hi), clip_pixel_to_bd);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_X86_HIGHBD_JNT_CONVOLVE_AVX2_H_
