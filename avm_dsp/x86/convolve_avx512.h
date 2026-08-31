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

#ifndef AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_
#define AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_

#include <immintrin.h>

#include "av2/common/filter.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE void prepare_coeffs(const InterpFilterParams *const filter_params,
                                  const int subpel_q4,
                                  __m512i *const coeffs /* [4] */) {
  const int16_t *filter = av2_get_interp_filter_subpel_kernel(
      filter_params, subpel_q4 & SUBPEL_MASK);
  const __m128i coeff_8 = _mm_loadu_si128((__m128i *)filter);
  const __m512i coeff = _mm512_broadcast_i32x4(coeff_8);
  coeffs[0] = _mm512_shuffle_epi32(coeff, 0x00);  // 0 1 0 1 ...
  coeffs[1] = _mm512_shuffle_epi32(coeff, 0x55);  // 2 3 2 3 ...
  coeffs[2] = _mm512_shuffle_epi32(coeff, 0xaa);  // 4 5 4 5 ...
  coeffs[3] = _mm512_shuffle_epi32(coeff, 0xff);  // 6 7 6 7 ...
}

static INLINE __m512i convolve(const __m512i *const s, const __m512i *const c) {
  const __m512i r0 = _mm512_madd_epi16(s[0], c[0]);
  const __m512i r1 = _mm512_madd_epi16(s[1], c[1]);
  const __m512i r2 = _mm512_madd_epi16(s[2], c[2]);
  const __m512i r3 = _mm512_madd_epi16(s[3], c[3]);
  return _mm512_add_epi32(_mm512_add_epi32(r0, r1), _mm512_add_epi32(r2, r3));
}

static INLINE void load_4rows(const uint16_t *src_ptr, int src_stride, int off,
                              __m512i *r0, __m512i *r1) {
  const __m128i a0 = _mm_loadu_si128((__m128i *)&src_ptr[0 * src_stride + off]);
  const __m128i b0 = _mm_loadu_si128((__m128i *)&src_ptr[1 * src_stride + off]);
  const __m128i c0 = _mm_loadu_si128((__m128i *)&src_ptr[2 * src_stride + off]);
  const __m128i d0 = _mm_loadu_si128((__m128i *)&src_ptr[3 * src_stride + off]);
  const __m128i a1 =
      _mm_loadu_si128((__m128i *)&src_ptr[0 * src_stride + off + 8]);
  const __m128i b1 =
      _mm_loadu_si128((__m128i *)&src_ptr[1 * src_stride + off + 8]);
  const __m128i c1 =
      _mm_loadu_si128((__m128i *)&src_ptr[2 * src_stride + off + 8]);
  const __m128i d1 =
      _mm_loadu_si128((__m128i *)&src_ptr[3 * src_stride + off + 8]);
  __m512i t0 = _mm512_castsi128_si512(a0);
  t0 = _mm512_inserti32x4(t0, b0, 1);
  t0 = _mm512_inserti32x4(t0, c0, 2);
  t0 = _mm512_inserti32x4(t0, d0, 3);
  __m512i t1 = _mm512_castsi128_si512(a1);
  t1 = _mm512_inserti32x4(t1, b1, 1);
  t1 = _mm512_inserti32x4(t1, c1, 2);
  t1 = _mm512_inserti32x4(t1, d1, 3);
  *r0 = t0;
  *r1 = t1;
}

#endif  // AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_
