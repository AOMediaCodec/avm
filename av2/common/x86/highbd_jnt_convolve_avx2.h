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

#include <assert.h>
#include <immintrin.h>

#include "config/avm_config.h"

#include "av2/common/convolve.h"
#include "avm_dsp/avm_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int bits;
  int offset;
  int32_t weight_pair;
  int fused_c3;
  int fused_shift;
} HighbdCwpCopyScale;

static INLINE HighbdCwpCopyScale
highbd_cwp_copy_scale(const ConvolveParams *conv_params, int bd) {
  HighbdCwpCopyScale s;
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  s.bits = FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;
  s.offset = 3 * (1 << (bd + s.bits - 1));
  const int src_weight = w1 * (1 << s.bits);
  s.fused_shift = s.bits + 4;
  s.weight_pair =
      (int32_t)(((uint32_t)(uint16_t)src_weight << 16) | (uint16_t)w0);
  s.fused_c3 = w0 * 32768 + (w1 - 16) * s.offset + (((1 << s.bits) >> 1) << 4);
  assert(s.bits <= 4);
  if (conv_params->do_average) {
    assert(w0 >= -32768 && w0 <= 32767);
    assert(src_weight >= -32768 && src_weight <= 32767);
  }
  return s;
}

// Fused compound averaging:
// clip((w0*(D-32768) + (w1*2^b)*s + C) >> (b+4)); D-32768 maps D into the
// signed lane.
static INLINE void highbd_cwp_convolve_2d_copy_do_average(
    __m256i data, __m256i src, const __m256i *weights, const __m256i *c3,
    int fused_shift, __m256i clip_pixel_to_bd, __m256i *res_clip) {
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
