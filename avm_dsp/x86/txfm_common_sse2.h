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

#ifndef AVM_AVM_DSP_X86_TXFM_COMMON_SSE2_H_
#define AVM_AVM_DSP_X86_TXFM_COMMON_SSE2_H_

#include <emmintrin.h>
#include "avm/avm_integer.h"
#include "avm_dsp/x86/synonyms.h"

#define pair_set_epi16(a, b) \
  _mm_set1_epi32((int32_t)(((uint16_t)(a)) | (((uint32_t)(b)) << 16)))

// Reverse the 8 16 bit words in __m128i
static INLINE __m128i mm_reverse_epi16(const __m128i x) {
  const __m128i a = _mm_shufflelo_epi16(x, 0x1b);
  const __m128i b = _mm_shufflehi_epi16(a, 0x1b);
  return _mm_shuffle_epi32(b, 0x4e);
}

#define octa_set_epi16(a, b, c, d, e, f, g, h)                           \
  _mm_setr_epi16((int16_t)(a), (int16_t)(b), (int16_t)(c), (int16_t)(d), \
                 (int16_t)(e), (int16_t)(f), (int16_t)(g), (int16_t)(h))

#endif  // AVM_AVM_DSP_X86_TXFM_COMMON_SSE2_H_
