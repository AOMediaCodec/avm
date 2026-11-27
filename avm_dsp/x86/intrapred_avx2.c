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

#include <immintrin.h>

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"
#include "avm_dsp/x86/convolve_avx2.h"
#include "avm_dsp/x86/intrapred_x86.h"
#include "avm_dsp/x86/lpf_common_sse2.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"
#include "avm_ports/bitops.h"
#include "av2/common/reconintra.h"

static DECLARE_ALIGNED(32, uint16_t, HighbdBaseMask[17][16]) = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0,
    0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0,
    0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0,
    0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0 },
  { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff }
};

static AVM_FORCE_INLINE void highbd_transpose8x16_16x8_avx2(
    __m256i *x0, __m256i *x1, __m256i *x2, __m256i *x3, __m256i *x4,
    __m256i *x5, __m256i *x6, __m256i *x7, __m256i *d0, __m256i *d1,
    __m256i *d2, __m256i *d3, __m256i *d4, __m256i *d5, __m256i *d6,
    __m256i *d7) {
  __m256i w0 = _mm256_unpacklo_epi16(*x0, *x1);  // 00 10 01 11 02 12 03 13
  __m256i w1 = _mm256_unpacklo_epi16(*x2, *x3);  // 20 30 21 31 22 32 23 33
  __m256i w2 = _mm256_unpacklo_epi16(*x4, *x5);  // 40 50 41 51 42 52 43 53
  __m256i w3 = _mm256_unpacklo_epi16(*x6, *x7);  // 60 70 61 71 62 72 63 73

  __m256i ww0 = _mm256_unpacklo_epi32(w0, w1);  // 00 10 20 30 01 11 21 31
  __m256i ww1 = _mm256_unpacklo_epi32(w2, w3);  // 40 50 60 70 41 51 61 71

  *d0 = _mm256_unpacklo_epi64(ww0, ww1);  // 00 10 20 30 40 50 60 70
  *d1 = _mm256_unpackhi_epi64(ww0, ww1);  // 01 11 21 31 41 51 61 71

  ww0 = _mm256_unpackhi_epi32(w0, w1);  // 02 12 22 32 03 13 23 33
  ww1 = _mm256_unpackhi_epi32(w2, w3);  // 42 52 62 72 43 53 63 73

  *d2 = _mm256_unpacklo_epi64(ww0, ww1);  // 02 12 22 32 42 52 62 72
  *d3 = _mm256_unpackhi_epi64(ww0, ww1);  // 03 13 23 33 43 53 63 73

  w0 = _mm256_unpackhi_epi16(*x0, *x1);  // 04 14 05 15 06 16 07 17
  w1 = _mm256_unpackhi_epi16(*x2, *x3);  // 24 34 25 35 26 36 27 37
  w2 = _mm256_unpackhi_epi16(*x4, *x5);  // 44 54 45 55 46 56 47 57
  w3 = _mm256_unpackhi_epi16(*x6, *x7);  // 64 74 65 75 66 76 67 77

  ww0 = _mm256_unpacklo_epi32(w0, w1);  // 04 14 24 34 05 15 25 35
  ww1 = _mm256_unpacklo_epi32(w2, w3);  // 44 54 64 74 45 55 65 75

  *d4 = _mm256_unpacklo_epi64(ww0, ww1);  // 04 14 24 34 44 54 64 74
  *d5 = _mm256_unpackhi_epi64(ww0, ww1);  // 05 15 25 35 45 55 65 75

  ww0 = _mm256_unpackhi_epi32(w0, w1);  // 06 16 26 36 07 17 27 37
  ww1 = _mm256_unpackhi_epi32(w2, w3);  // 46 56 66 76 47 57 67 77

  *d6 = _mm256_unpacklo_epi64(ww0, ww1);  // 06 16 26 36 46 56 66 76
  *d7 = _mm256_unpackhi_epi64(ww0, ww1);  // 07 17 27 37 47 57 67 77
}

static AVM_FORCE_INLINE void highbd_transpose_4x4_sse2(const uint16_t *src,
                                                       ptrdiff_t src_stride,
                                                       uint16_t *dst,
                                                       ptrdiff_t dst_stride) {
  const __m128i r0 = _mm_loadl_epi64((const __m128i *)(src + 0 * src_stride));
  const __m128i r1 = _mm_loadl_epi64((const __m128i *)(src + 1 * src_stride));
  const __m128i r2 = _mm_loadl_epi64((const __m128i *)(src + 2 * src_stride));
  const __m128i r3 = _mm_loadl_epi64((const __m128i *)(src + 3 * src_stride));
  const __m128i w0 = _mm_unpacklo_epi16(r0, r1);
  const __m128i w1 = _mm_unpacklo_epi16(r2, r3);
  const __m128i ww0 = _mm_unpacklo_epi32(w0, w1);
  const __m128i ww1 = _mm_unpackhi_epi32(w0, w1);
  _mm_storel_epi64((__m128i *)(dst + 0 * dst_stride), ww0);
  _mm_storeh_pd((double *)(dst + 1 * dst_stride), _mm_castsi128_pd(ww0));
  _mm_storel_epi64((__m128i *)(dst + 2 * dst_stride), ww1);
  _mm_storeh_pd((double *)(dst + 3 * dst_stride), _mm_castsi128_pd(ww1));
}

static AVM_FORCE_INLINE void highbd_transpose_8x4_sse2(const uint16_t *src,
                                                       ptrdiff_t src_stride,
                                                       uint16_t *dst,
                                                       ptrdiff_t dst_stride) {
  const __m128i w0 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 0 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 1 * src_stride)));
  const __m128i w1 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 2 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 3 * src_stride)));
  const __m128i w2 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 4 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 5 * src_stride)));
  const __m128i w3 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 6 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 7 * src_stride)));
  const __m128i ww0 = _mm_unpacklo_epi32(w0, w1);
  const __m128i ww1 = _mm_unpackhi_epi32(w0, w1);
  const __m128i ww2 = _mm_unpacklo_epi32(w2, w3);
  const __m128i ww3 = _mm_unpackhi_epi32(w2, w3);
  _mm_storeu_si128((__m128i *)(dst + 0 * dst_stride),
                   _mm_unpacklo_epi64(ww0, ww2));
  _mm_storeu_si128((__m128i *)(dst + 1 * dst_stride),
                   _mm_unpackhi_epi64(ww0, ww2));
  _mm_storeu_si128((__m128i *)(dst + 2 * dst_stride),
                   _mm_unpacklo_epi64(ww1, ww3));
  _mm_storeu_si128((__m128i *)(dst + 3 * dst_stride),
                   _mm_unpackhi_epi64(ww1, ww3));
}

static AVM_FORCE_INLINE void highbd_transpose_16x4_avx2(const uint16_t *src,
                                                        ptrdiff_t src_stride,
                                                        uint16_t *dst,
                                                        ptrdiff_t dst_stride) {
  const __m128i r0 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 0 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 1 * src_stride)));
  const __m128i r1 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 2 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 3 * src_stride)));
  const __m128i r2 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 4 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 5 * src_stride)));
  const __m128i r3 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 6 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 7 * src_stride)));
  const __m128i r4 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 8 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 9 * src_stride)));
  const __m128i r5 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 10 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 11 * src_stride)));
  const __m128i r6 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 12 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 13 * src_stride)));
  const __m128i r7 = _mm_unpacklo_epi16(
      _mm_loadl_epi64((const __m128i *)(src + 14 * src_stride)),
      _mm_loadl_epi64((const __m128i *)(src + 15 * src_stride)));
  const __m256i v0 = yy_set_m128i(r4, r0);
  const __m256i v1 = yy_set_m128i(r5, r1);
  const __m256i v2 = yy_set_m128i(r6, r2);
  const __m256i v3 = yy_set_m128i(r7, r3);
  const __m256i u0 = _mm256_unpacklo_epi32(v0, v1);
  const __m256i u1 = _mm256_unpackhi_epi32(v0, v1);
  const __m256i u2 = _mm256_unpacklo_epi32(v2, v3);
  const __m256i u3 = _mm256_unpackhi_epi32(v2, v3);
  _mm256_storeu_si256((__m256i *)(dst + 0 * dst_stride),
                      _mm256_unpacklo_epi64(u0, u2));
  _mm256_storeu_si256((__m256i *)(dst + 1 * dst_stride),
                      _mm256_unpackhi_epi64(u0, u2));
  _mm256_storeu_si256((__m256i *)(dst + 2 * dst_stride),
                      _mm256_unpacklo_epi64(u1, u3));
  _mm256_storeu_si256((__m256i *)(dst + 3 * dst_stride),
                      _mm256_unpackhi_epi64(u1, u3));
}

static AVM_FORCE_INLINE void highbd_transpose_4x8_sse2(const uint16_t *src,
                                                       ptrdiff_t src_stride,
                                                       uint16_t *dst,
                                                       ptrdiff_t dst_stride) {
  const __m128i r0 = _mm_loadu_si128((const __m128i *)(src + 0 * src_stride));
  const __m128i r1 = _mm_loadu_si128((const __m128i *)(src + 1 * src_stride));
  const __m128i r2 = _mm_loadu_si128((const __m128i *)(src + 2 * src_stride));
  const __m128i r3 = _mm_loadu_si128((const __m128i *)(src + 3 * src_stride));
  const __m128i w0 = _mm_unpacklo_epi16(r0, r1);
  const __m128i w1 = _mm_unpacklo_epi16(r2, r3);
  const __m128i w2 = _mm_unpackhi_epi16(r0, r1);
  const __m128i w3 = _mm_unpackhi_epi16(r2, r3);
  const __m128i ww0 = _mm_unpacklo_epi32(w0, w1);
  const __m128i ww1 = _mm_unpackhi_epi32(w0, w1);
  const __m128i ww2 = _mm_unpacklo_epi32(w2, w3);
  const __m128i ww3 = _mm_unpackhi_epi32(w2, w3);
  _mm_storel_epi64((__m128i *)(dst + 0 * dst_stride), ww0);
  _mm_storeh_pd((double *)(dst + 1 * dst_stride), _mm_castsi128_pd(ww0));
  _mm_storel_epi64((__m128i *)(dst + 2 * dst_stride), ww1);
  _mm_storeh_pd((double *)(dst + 3 * dst_stride), _mm_castsi128_pd(ww1));
  _mm_storel_epi64((__m128i *)(dst + 4 * dst_stride), ww2);
  _mm_storeh_pd((double *)(dst + 5 * dst_stride), _mm_castsi128_pd(ww2));
  _mm_storel_epi64((__m128i *)(dst + 6 * dst_stride), ww3);
  _mm_storeh_pd((double *)(dst + 7 * dst_stride), _mm_castsi128_pd(ww3));
}

static AVM_FORCE_INLINE void highbd_transpose_8x8_sse2(const uint16_t *src,
                                                       ptrdiff_t src_stride,
                                                       uint16_t *dst,
                                                       ptrdiff_t dst_stride) {
  __m128i r0 = _mm_loadu_si128((const __m128i *)(src + 0 * src_stride));
  __m128i r1 = _mm_loadu_si128((const __m128i *)(src + 1 * src_stride));
  __m128i r2 = _mm_loadu_si128((const __m128i *)(src + 2 * src_stride));
  __m128i r3 = _mm_loadu_si128((const __m128i *)(src + 3 * src_stride));
  __m128i r4 = _mm_loadu_si128((const __m128i *)(src + 4 * src_stride));
  __m128i r5 = _mm_loadu_si128((const __m128i *)(src + 5 * src_stride));
  __m128i r6 = _mm_loadu_si128((const __m128i *)(src + 6 * src_stride));
  __m128i r7 = _mm_loadu_si128((const __m128i *)(src + 7 * src_stride));
  __m128i d0, d1, d2, d3, d4, d5, d6, d7;
  highbd_transpose8x8_sse2(&r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7, &d0, &d1,
                           &d2, &d3, &d4, &d5, &d6, &d7);
  _mm_storeu_si128((__m128i *)(dst + 0 * dst_stride), d0);
  _mm_storeu_si128((__m128i *)(dst + 1 * dst_stride), d1);
  _mm_storeu_si128((__m128i *)(dst + 2 * dst_stride), d2);
  _mm_storeu_si128((__m128i *)(dst + 3 * dst_stride), d3);
  _mm_storeu_si128((__m128i *)(dst + 4 * dst_stride), d4);
  _mm_storeu_si128((__m128i *)(dst + 5 * dst_stride), d5);
  _mm_storeu_si128((__m128i *)(dst + 6 * dst_stride), d6);
  _mm_storeu_si128((__m128i *)(dst + 7 * dst_stride), d7);
}

static AVM_FORCE_INLINE void highbd_transpose_16x8_avx2(const uint16_t *src,
                                                        ptrdiff_t src_stride,
                                                        uint16_t *dst,
                                                        ptrdiff_t dst_stride) {
  __m256i r0 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 8 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 0 * src_stride)));
  __m256i r1 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 9 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 1 * src_stride)));
  __m256i r2 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 10 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 2 * src_stride)));
  __m256i r3 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 11 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 3 * src_stride)));
  __m256i r4 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 12 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 4 * src_stride)));
  __m256i r5 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 13 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 5 * src_stride)));
  __m256i r6 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 14 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 6 * src_stride)));
  __m256i r7 =
      yy_set_m128i(_mm_loadu_si128((const __m128i *)(src + 15 * src_stride)),
                   _mm_loadu_si128((const __m128i *)(src + 7 * src_stride)));
  __m256i d0, d1, d2, d3, d4, d5, d6, d7;
  highbd_transpose8x16_16x8_avx2(&r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7, &d0,
                                 &d1, &d2, &d3, &d4, &d5, &d6, &d7);
  _mm256_storeu_si256((__m256i *)(dst + 0 * dst_stride), d0);
  _mm256_storeu_si256((__m256i *)(dst + 1 * dst_stride), d1);
  _mm256_storeu_si256((__m256i *)(dst + 2 * dst_stride), d2);
  _mm256_storeu_si256((__m256i *)(dst + 3 * dst_stride), d3);
  _mm256_storeu_si256((__m256i *)(dst + 4 * dst_stride), d4);
  _mm256_storeu_si256((__m256i *)(dst + 5 * dst_stride), d5);
  _mm256_storeu_si256((__m256i *)(dst + 6 * dst_stride), d6);
  _mm256_storeu_si256((__m256i *)(dst + 7 * dst_stride), d7);
}

static AVM_FORCE_INLINE void highbd_transpose_4x16_avx2(const uint16_t *src,
                                                        ptrdiff_t src_stride,
                                                        uint16_t *dst,
                                                        ptrdiff_t dst_stride) {
  const __m256i r0 =
      _mm256_loadu_si256((const __m256i *)(src + 0 * src_stride));
  const __m256i r1 =
      _mm256_loadu_si256((const __m256i *)(src + 1 * src_stride));
  const __m256i r2 =
      _mm256_loadu_si256((const __m256i *)(src + 2 * src_stride));
  const __m256i r3 =
      _mm256_loadu_si256((const __m256i *)(src + 3 * src_stride));
  const __m256i w0 = _mm256_unpacklo_epi16(r0, r1);
  const __m256i w1 = _mm256_unpacklo_epi16(r2, r3);
  const __m256i w2 = _mm256_unpackhi_epi16(r0, r1);
  const __m256i w3 = _mm256_unpackhi_epi16(r2, r3);
  const __m256i u[4] = {
    _mm256_unpacklo_epi32(w0, w1),
    _mm256_unpackhi_epi32(w0, w1),
    _mm256_unpacklo_epi32(w2, w3),
    _mm256_unpackhi_epi32(w2, w3),
  };
  for (int i = 0; i < 4; ++i) {
    const __m128i lo = _mm256_castsi256_si128(u[i]);
    const __m128i hi = _mm256_extracti128_si256(u[i], 1);
    _mm_storel_epi64((__m128i *)(dst + (2 * i + 0) * dst_stride), lo);
    _mm_storeh_pd((double *)(dst + (2 * i + 1) * dst_stride),
                  _mm_castsi128_pd(lo));
    _mm_storel_epi64((__m128i *)(dst + (2 * i + 8) * dst_stride), hi);
    _mm_storeh_pd((double *)(dst + (2 * i + 9) * dst_stride),
                  _mm_castsi128_pd(hi));
  }
}

static AVM_FORCE_INLINE void highbd_transpose_8x16_avx2(const uint16_t *src,
                                                        ptrdiff_t src_stride,
                                                        uint16_t *dst,
                                                        ptrdiff_t dst_stride) {
  __m256i r0 = _mm256_loadu_si256((const __m256i *)(src + 0 * src_stride));
  __m256i r1 = _mm256_loadu_si256((const __m256i *)(src + 1 * src_stride));
  __m256i r2 = _mm256_loadu_si256((const __m256i *)(src + 2 * src_stride));
  __m256i r3 = _mm256_loadu_si256((const __m256i *)(src + 3 * src_stride));
  __m256i r4 = _mm256_loadu_si256((const __m256i *)(src + 4 * src_stride));
  __m256i r5 = _mm256_loadu_si256((const __m256i *)(src + 5 * src_stride));
  __m256i r6 = _mm256_loadu_si256((const __m256i *)(src + 6 * src_stride));
  __m256i r7 = _mm256_loadu_si256((const __m256i *)(src + 7 * src_stride));
  __m256i d0, d1, d2, d3, d4, d5, d6, d7;
  highbd_transpose8x16_16x8_avx2(&r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7, &d0,
                                 &d1, &d2, &d3, &d4, &d5, &d6, &d7);
  const __m256i d[8] = { d0, d1, d2, d3, d4, d5, d6, d7 };
  for (int i = 0; i < 8; ++i) {
    _mm_storeu_si128((__m128i *)(dst + i * dst_stride),
                     _mm256_castsi256_si128(d[i]));
    _mm_storeu_si128((__m128i *)(dst + (i + 8) * dst_stride),
                     _mm256_extracti128_si256(d[i], 1));
  }
}

static AVM_FORCE_INLINE void highbd_transpose_16x16_avx2(const uint16_t *src,
                                                         ptrdiff_t src_stride,
                                                         uint16_t *dst,
                                                         ptrdiff_t dst_stride) {
  const __m256i x0 =
      _mm256_loadu_si256((const __m256i *)(src + 0 * src_stride));
  const __m256i x1 =
      _mm256_loadu_si256((const __m256i *)(src + 1 * src_stride));
  const __m256i x2 =
      _mm256_loadu_si256((const __m256i *)(src + 2 * src_stride));
  const __m256i x3 =
      _mm256_loadu_si256((const __m256i *)(src + 3 * src_stride));
  const __m256i x4 =
      _mm256_loadu_si256((const __m256i *)(src + 4 * src_stride));
  const __m256i x5 =
      _mm256_loadu_si256((const __m256i *)(src + 5 * src_stride));
  const __m256i x6 =
      _mm256_loadu_si256((const __m256i *)(src + 6 * src_stride));
  const __m256i x7 =
      _mm256_loadu_si256((const __m256i *)(src + 7 * src_stride));

  __m256i w0 = _mm256_unpacklo_epi16(x0, x1);
  __m256i w1 = _mm256_unpacklo_epi16(x2, x3);
  __m256i w2 = _mm256_unpacklo_epi16(x4, x5);
  __m256i w3 = _mm256_unpacklo_epi16(x6, x7);
  __m256i ww0 = _mm256_unpacklo_epi32(w0, w1);
  __m256i ww1 = _mm256_unpacklo_epi32(w2, w3);
  const __m256i dd0 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd1 = _mm256_unpackhi_epi64(ww0, ww1);
  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);
  const __m256i dd2 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd3 = _mm256_unpackhi_epi64(ww0, ww1);

  w0 = _mm256_unpackhi_epi16(x0, x1);
  w1 = _mm256_unpackhi_epi16(x2, x3);
  w2 = _mm256_unpackhi_epi16(x4, x5);
  w3 = _mm256_unpackhi_epi16(x6, x7);
  ww0 = _mm256_unpacklo_epi32(w0, w1);
  ww1 = _mm256_unpacklo_epi32(w2, w3);
  const __m256i dd4 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd5 = _mm256_unpackhi_epi64(ww0, ww1);
  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);
  const __m256i dd6 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd7 = _mm256_unpackhi_epi64(ww0, ww1);

  const __m256i x8 =
      _mm256_loadu_si256((const __m256i *)(src + 8 * src_stride));
  const __m256i x9 =
      _mm256_loadu_si256((const __m256i *)(src + 9 * src_stride));
  const __m256i x10 =
      _mm256_loadu_si256((const __m256i *)(src + 10 * src_stride));
  const __m256i x11 =
      _mm256_loadu_si256((const __m256i *)(src + 11 * src_stride));
  const __m256i x12 =
      _mm256_loadu_si256((const __m256i *)(src + 12 * src_stride));
  const __m256i x13 =
      _mm256_loadu_si256((const __m256i *)(src + 13 * src_stride));
  const __m256i x14 =
      _mm256_loadu_si256((const __m256i *)(src + 14 * src_stride));
  const __m256i x15 =
      _mm256_loadu_si256((const __m256i *)(src + 15 * src_stride));

  w0 = _mm256_unpacklo_epi16(x8, x9);
  w1 = _mm256_unpacklo_epi16(x10, x11);
  w2 = _mm256_unpacklo_epi16(x12, x13);
  w3 = _mm256_unpacklo_epi16(x14, x15);
  ww0 = _mm256_unpacklo_epi32(w0, w1);
  ww1 = _mm256_unpacklo_epi32(w2, w3);
  const __m256i dd8 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd9 = _mm256_unpackhi_epi64(ww0, ww1);
  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);
  const __m256i dd10 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd11 = _mm256_unpackhi_epi64(ww0, ww1);

  _mm256_storeu_si256((__m256i *)(dst + 0 * dst_stride),
                      _mm256_permute2x128_si256(dd0, dd8, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 8 * dst_stride),
                      _mm256_permute2x128_si256(dd0, dd8, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 1 * dst_stride),
                      _mm256_permute2x128_si256(dd1, dd9, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 9 * dst_stride),
                      _mm256_permute2x128_si256(dd1, dd9, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 2 * dst_stride),
                      _mm256_permute2x128_si256(dd2, dd10, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 10 * dst_stride),
                      _mm256_permute2x128_si256(dd2, dd10, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 3 * dst_stride),
                      _mm256_permute2x128_si256(dd3, dd11, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 11 * dst_stride),
                      _mm256_permute2x128_si256(dd3, dd11, 0x31));

  w0 = _mm256_unpackhi_epi16(x8, x9);
  w1 = _mm256_unpackhi_epi16(x10, x11);
  w2 = _mm256_unpackhi_epi16(x12, x13);
  w3 = _mm256_unpackhi_epi16(x14, x15);
  ww0 = _mm256_unpacklo_epi32(w0, w1);
  ww1 = _mm256_unpacklo_epi32(w2, w3);
  const __m256i dd12 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd13 = _mm256_unpackhi_epi64(ww0, ww1);
  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);
  const __m256i dd14 = _mm256_unpacklo_epi64(ww0, ww1);
  const __m256i dd15 = _mm256_unpackhi_epi64(ww0, ww1);

  _mm256_storeu_si256((__m256i *)(dst + 4 * dst_stride),
                      _mm256_permute2x128_si256(dd4, dd12, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 12 * dst_stride),
                      _mm256_permute2x128_si256(dd4, dd12, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 5 * dst_stride),
                      _mm256_permute2x128_si256(dd5, dd13, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 13 * dst_stride),
                      _mm256_permute2x128_si256(dd5, dd13, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 6 * dst_stride),
                      _mm256_permute2x128_si256(dd6, dd14, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 14 * dst_stride),
                      _mm256_permute2x128_si256(dd6, dd14, 0x31));
  _mm256_storeu_si256((__m256i *)(dst + 7 * dst_stride),
                      _mm256_permute2x128_si256(dd7, dd15, 0x20));
  _mm256_storeu_si256((__m256i *)(dst + 15 * dst_stride),
                      _mm256_permute2x128_si256(dd7, dd15, 0x31));
}

static AVM_FORCE_INLINE void highbd_transpose_h16_avx2(const uint16_t *src,
                                                       ptrdiff_t src_stride,
                                                       uint16_t *dst,
                                                       ptrdiff_t dst_stride,
                                                       int width, int height) {
  assert(height == 16 || height == 32 || height == 64);
  switch (width) {
    case 4:
      for (int j = 0; j < height; j += 16) {
        highbd_transpose_4x16_avx2(src + j, src_stride, dst + j * dst_stride,
                                   dst_stride);
      }
      break;
    case 8:
      for (int j = 0; j < height; j += 16) {
        highbd_transpose_8x16_avx2(src + j, src_stride, dst + j * dst_stride,
                                   dst_stride);
      }
      break;
    default:
      assert(width == 16 || width == 32 || width == 64);
      for (int j = 0; j < height; j += 16) {
        for (int i = 0; i < width; i += 16) {
          highbd_transpose_16x16_avx2(src + i * src_stride + j, src_stride,
                                      dst + j * dst_stride + i, dst_stride);
        }
      }
      break;
  }
}

static AVM_FORCE_INLINE void highbd_transpose(const uint16_t *src,
                                              ptrdiff_t src_stride,
                                              uint16_t *dst,
                                              ptrdiff_t dst_stride, int width,
                                              int height) {
  switch (height) {
    case 4:
      switch (width) {
        case 4:
          highbd_transpose_4x4_sse2(src, src_stride, dst, dst_stride);
          break;
        case 8:
          highbd_transpose_8x4_sse2(src, src_stride, dst, dst_stride);
          break;
        default:
          assert(width == 16 || width == 32 || width == 64);
          for (int i = 0; i < width; i += 16) {
            highbd_transpose_16x4_avx2(src + i * src_stride, src_stride,
                                       dst + i, dst_stride);
          }
          break;
      }
      break;
    case 8:
      switch (width) {
        case 4:
          highbd_transpose_4x8_sse2(src, src_stride, dst, dst_stride);
          break;
        case 8:
          highbd_transpose_8x8_sse2(src, src_stride, dst, dst_stride);
          break;
        default:
          assert(width == 16 || width == 32 || width == 64);
          for (int i = 0; i < width; i += 16) {
            highbd_transpose_16x8_avx2(src + i * src_stride, src_stride,
                                       dst + i, dst_stride);
          }
          break;
      }
      break;
    case 16:
    case 32:
    case 64:
      highbd_transpose_h16_avx2(src, src_stride, dst, dst_stride, width,
                                height);
      break;
    default: assert(0); break;
  }
}

DECLARE_ALIGNED(32, static const int16_t,
                c0123_2row_avx2[16]) = { 0, 1, 2, 3, 4, 5, 6, 7,
                                         0, 1, 2, 3, 4, 5, 6, 7 };

DECLARE_ALIGNED(32, static const int16_t,
                c0123_16w_avx2[16]) = { 0, 1, 2,  3,  4,  5,  6,  7,
                                        8, 9, 10, 11, 12, 13, 14, 15 };

DECLARE_ALIGNED(32, static const uint8_t, dr_interleave_epi16_avx2[32]) = {
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15
};

static AVM_FORCE_INLINE __m256i lowbd_prepare_coeffs_2tap_2rows(int shift0,
                                                                int shift1) {
  const __m128i coeff_0 =
      _mm_set1_epi16((int16_t)((shift0 << 8) | (32 - shift0)));
  const __m128i coeff_1 =
      _mm_set1_epi16((int16_t)((shift1 << 8) | (32 - shift1)));
  return yy_set_m128i(coeff_1, coeff_0);
}

static AVM_FORCE_INLINE __m256i highbd_prepare_coeffs_2tap_2rows(int shift0,
                                                                 int shift1) {
  const __m128i coeff_0 = _mm_cvtsi32_si128((shift0 << 16) | (32 - shift0));
  const __m128i coeff_1 = _mm_cvtsi32_si128((shift1 << 16) | (32 - shift1));
  return _mm256_shuffle_epi32(yy_set_m128i(coeff_1, coeff_0), 0x00);
}

static AVM_FORCE_INLINE __m256i lowbd_convolve_2tap(__m256i data,
                                                    __m256i coeffs,
                                                    __m256i filt) {
  const __m256i s = _mm256_shuffle_epi8(data, filt);
  return _mm256_maddubs_epi16(s, coeffs);
}

static AVM_FORCE_INLINE void store_above_max_base_x(uint16_t *dst,
                                                    ptrdiff_t dst_stride,
                                                    int bw, int bh,
                                                    int row_start,
                                                    __m256i above_max_base_x) {
  switch (bw) {
    case 4:
      for (int i = row_start; i < bh; ++i, dst += dst_stride) {
        _mm_storel_epi64((__m128i *)dst,
                         _mm256_castsi256_si128(above_max_base_x));
      }
      break;
    case 8:
      for (int i = row_start; i < bh; ++i, dst += dst_stride) {
        _mm_storeu_si128((__m128i *)dst,
                         _mm256_castsi256_si128(above_max_base_x));
      }
      break;
    default:
      assert(bw == 16 || bw == 32 || bw == 64);
      for (int i = row_start; i < bh; ++i, dst += dst_stride) {
        for (int j = 0; j < bw; j += 16) {
          _mm256_storeu_si256((__m256i *)(dst + j), above_max_base_x);
        }
      }
      break;
  }
}

static AVM_FORCE_INLINE __m256i
blend_out_of_bounds_pixels(__m256i res, __m256i above_max_base_x, __m256i base,
                           __m256i c0123, __m256i max_base_x256) {
  const __m256i base_inc256 = _mm256_add_epi16(base, c0123);
  const __m256i mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
  return _mm256_blendv_epi8(above_max_base_x, res, mask256);
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_4xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 4;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) >= max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const = _mm256_set1_epi16(16);
  const __m256i filt = _mm256_load_si256((const __m256i *)filt1_global_avx2);

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x, base1);

    const __m256i row01 = yy_loadu2_128(above + base1, above + base0);
    const __m256i data = _mm256_packus_epi16(row01, row01);
    const __m256i coeffs =
        lowbd_prepare_coeffs_2tap_2rows((x & 0x3F) >> 1, (x1 & 0x3F) >> 1);
    __m256i res = lowbd_convolve_2tap(data, coeffs, filt);
    res = _mm256_srli_epi16(_mm256_add_epi16(res, round_const), 5);

    if (!is_z2_mixed && (base1 + bw - 1) >= max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x256);
    }
    _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
    _mm_storel_epi64((__m128i *)(dst + stride),
                     _mm256_extracti128_si256(res, 1));
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_4xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 4;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) >= max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const = _mm256_set1_epi32(16);
  const __m256i filt = _mm256_load_si256((const __m256i *)ip_shuffle_f2f3);

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x, base1);

    const __m256i data = yy_loadu2_128(above + base1, above + base0);
    const __m256i s = _mm256_shuffle_epi8(data, filt);
    const __m256i coeffs =
        highbd_prepare_coeffs_2tap_2rows((x & 0x3F) >> 1, (x1 & 0x3F) >> 1);
    __m256i res = _mm256_madd_epi16(s, coeffs);
    res = _mm256_srli_epi32(_mm256_add_epi32(res, round_const), 5);
    res = _mm256_packus_epi32(res, res);

    if (!is_z2_mixed && (base1 + bw - 1) >= max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x256);
    }
    _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
    _mm_storel_epi64((__m128i *)(dst + stride),
                     _mm256_extracti128_si256(res, 1));
  }
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_8xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 8;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) >= max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const = _mm256_set1_epi16(16);
  const __m256i filt = _mm256_load_si256((const __m256i *)filt1_global_avx2);

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x, base1);

    const __m256i row0 = _mm256_loadu_si256((const __m256i *)(above + base0));
    const __m256i row1 = _mm256_loadu_si256((const __m256i *)(above + base1));
    const __m256i data =
        _mm256_permute4x64_epi64(_mm256_packus_epi16(row0, row1), 0xD8);
    const __m256i coeffs =
        lowbd_prepare_coeffs_2tap_2rows((x & 0x3F) >> 1, (x1 & 0x3F) >> 1);
    __m256i res = lowbd_convolve_2tap(data, coeffs, filt);
    res = _mm256_srli_epi16(_mm256_add_epi16(res, round_const), 5);

    if (!is_z2_mixed && (base1 + bw - 1) >= max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x256);
    }
    yy_storeu2_128(dst + stride, dst, res);
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_8xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 8;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) >= max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const = _mm256_set1_epi32(16);
  const __m256i interleave =
      _mm256_load_si256((const __m256i *)dr_interleave_epi16_avx2);

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x, base1);

    const __m256i row0 = _mm256_loadu_si256((const __m256i *)(above + base0));
    const __m256i row1 = _mm256_loadu_si256((const __m256i *)(above + base1));
    const __m256i r0 = _mm256_permute2x128_si256(row0, row1, 0x20);
    const __m256i r1 = _mm256_permute2x128_si256(row0, row1, 0x31);
    const __m256i s0 = r0;
    const __m256i s1 = _mm256_alignr_epi8(r1, r0, 2);
    const __m256i coeffs =
        highbd_prepare_coeffs_2tap_2rows((x & 0x3F) >> 1, (x1 & 0x3F) >> 1);
    __m256i res_even = _mm256_madd_epi16(s0, coeffs);
    __m256i res_odd = _mm256_madd_epi16(s1, coeffs);
    res_even = _mm256_srli_epi32(_mm256_add_epi32(res_even, round_const), 5);
    res_odd = _mm256_srli_epi32(_mm256_add_epi32(res_odd, round_const), 5);
    __m256i res =
        _mm256_shuffle_epi8(_mm256_packus_epi32(res_even, res_odd), interleave);

    if (!is_z2_mixed && (base1 + bw - 1) >= max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x256);
    }
    yy_storeu2_128(dst + stride, dst, res);
  }
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_MxN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  assert(bh <= 64);
  assert(dx != 0);

  const int frac_bits = 6;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_16w_avx2);
  const __m256i round_const = _mm256_set1_epi16(16);
  const __m256i filt = _mm256_load_si256((const __m256i *)filt1_global_avx2);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < bh; ++r, dst += stride, x += dx) {
    const int base = x >> frac_bits;
    if (is_z2_mixed && base + bw - 1 < min_base) return;
    if (!is_z2_mixed && base >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int shift = (x & 0x3F) >> 1;
    const __m256i coeffs =
        _mm256_set1_epi16((int16_t)((shift << 8) | (32 - shift)));
    const int j_start =
        (is_z2_mixed && min_base - base > 7) ? ((min_base - base) & ~7) : 0;
    for (int j = j_start; j < bw; j += 16) {
      __m256i res;
      if (!is_z2_mixed && base + j >= max_base_x) {
        res = above_max_base_x;
      } else {
        const __m256i r0 =
            _mm256_loadu_si256((const __m256i *)(above + base + j));
        const __m256i r1 =
            _mm256_loadu_si256((const __m256i *)(above + base + j + 8));
        const __m256i data = _mm256_packus_epi16(r0, r1);
        res = lowbd_convolve_2tap(data, coeffs, filt);
        res = _mm256_srli_epi16(_mm256_add_epi16(res, round_const), 5);
        if (!is_z2_mixed && (base + j + 15) >= max_base_x) {
          res = blend_out_of_bounds_pixels(res, above_max_base_x,
                                           _mm256_set1_epi16(base + j), c0123,
                                           max_base_x256);
        }
      }
      if (is_z2_mixed && j + 16 > bw) {
        _mm_storeu_si128((__m128i *)(dst + j), _mm256_castsi256_si128(res));
      } else {
        _mm256_storeu_si256((__m256i *)(dst + j), res);
      }
    }
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_MxN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  assert(bh <= 64);
  assert(dx != 0);

  const int frac_bits = 6;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  const __m256i max_base_x256 = _mm256_set1_epi16(max_base_x);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_16w_avx2);
  const __m256i round_const = _mm256_set1_epi32(16);
  const __m256i interleave =
      _mm256_load_si256((const __m256i *)dr_interleave_epi16_avx2);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < bh; ++r, dst += stride, x += dx) {
    const int base = x >> frac_bits;
    if (is_z2_mixed && base + bw - 1 < min_base) return;
    if (!is_z2_mixed && base >= max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int shift = (x & 0x3F) >> 1;
    const __m256i coeffs = _mm256_set1_epi32((shift << 16) | (32 - shift));
    const int j_start =
        (is_z2_mixed && min_base - base > 7) ? ((min_base - base) & ~7) : 0;
    for (int j = j_start; j < bw; j += 16) {
      __m256i res;
      if (!is_z2_mixed && base + j >= max_base_x) {
        res = above_max_base_x;
      } else {
        const __m256i s0 =
            _mm256_loadu_si256((const __m256i *)(above + base + j));
        const __m256i s1 =
            _mm256_loadu_si256((const __m256i *)(above + base + j + 1));
        __m256i res_even = _mm256_madd_epi16(s0, coeffs);
        __m256i res_odd = _mm256_madd_epi16(s1, coeffs);
        res_even =
            _mm256_srli_epi32(_mm256_add_epi32(res_even, round_const), 5);
        res_odd = _mm256_srli_epi32(_mm256_add_epi32(res_odd, round_const), 5);
        res = _mm256_shuffle_epi8(_mm256_packus_epi32(res_even, res_odd),
                                  interleave);
        if (!is_z2_mixed && (base + j + 15) >= max_base_x) {
          res = blend_out_of_bounds_pixels(res, above_max_base_x,
                                           _mm256_set1_epi16(base + j), c0123,
                                           max_base_x256);
        }
      }
      if (is_z2_mixed && j + 16 > bw) {
        _mm_storeu_si128((__m128i *)(dst + j), _mm256_castsi256_si128(res));
      } else {
        _mm256_storeu_si256((__m256i *)(dst + j), res);
      }
    }
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_4xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  if (bd == 8) {
    lowbd_dr_prediction_z1_4xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                    is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_4xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                     is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_8xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  if (bd == 8) {
    lowbd_dr_prediction_z1_8xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                    is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_8xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                     is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_MxN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int bd, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  if (bd == 8) {
    lowbd_dr_prediction_z1_MxN_avx2(dst, stride, bw, bh, above, dx, mrl_index,
                                    is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_MxN_avx2(dst, stride, bw, bh, above, dx, mrl_index,
                                     is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int bd, int mrl_index, int is_z2_mixed) {
  switch (bw) {
    case 4:
      dr_prediction_z1_4xN_avx2(dst, stride, bh, above, dx, bd, mrl_index,
                                is_z2_mixed);
      break;
    case 8:
      dr_prediction_z1_8xN_avx2(dst, stride, bh, above, dx, bd, mrl_index,
                                is_z2_mixed);
      break;
    case 16:
    case 32:
    case 64:
      dr_prediction_z1_MxN_avx2(dst, stride, bw, bh, above, dx, bd, mrl_index,
                                is_z2_mixed);
      break;
    default: assert(0); break;
  }
}

// Directional prediction, zone 1: 0 < angle < 90
void av2_highbd_dr_prediction_z1_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  (void)left;
  (void)dy;

  assert(dy == 1);
  assert(dx > 0);

  dr_prediction_z1_avx2(dst, stride, bw, bh, above, dx, bd, mrl_index, 0);
}

static void dr_prediction_z3_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                  int bh, const uint16_t *left, int dy, int bd,
                                  int mrl_index, int is_z2_mixed) {
  DECLARE_ALIGNED(32, uint16_t, temp_dst[64 * 64]);
  dr_prediction_z1_avx2(temp_dst, bh, bh, bw, left, dy, bd, mrl_index,
                        is_z2_mixed);
  highbd_transpose(temp_dst, bh, dst, stride, bw, bh);
}

static AVM_FORCE_INLINE int get_z2_left_width(int bw, int bh, int dx,
                                              int mrl_index) {
  const int max_left_cols =
      AVMMIN(bw, -(1 + mrl_index) - ((-(bh + mrl_index) * dx) >> 6));
  if (max_left_cols <= 4) return 4;
  if (max_left_cols <= 8) return 8;
  if (max_left_cols <= 16) return 16;
  if (max_left_cols <= 32) return 32;
  return 64;
}

static AVM_FORCE_INLINE void blend_z2_above_left_avx2(
    uint16_t *dst, ptrdiff_t stride, const uint16_t *left_pred, int left_width,
    int bw, int bh, int dx, int mrl_index) {
  const int min_base = -(1 + mrl_index);
  int x = -(1 + mrl_index) * dx;
  if (left_width == 4) {
    for (int r = 0; r < bh; ++r, dst += stride, left_pred += 4, x -= dx) {
      const int left_cols = AVMMIN(min_base - (x >> 6), bw);
      if (left_cols <= 0) continue;
      const __m128i l = _mm_loadl_epi64((const __m128i *)left_pred);
      if (left_cols >= 4) {
        _mm_storel_epi64((__m128i *)dst, l);
      } else {
        const __m128i a = _mm_loadl_epi64((const __m128i *)dst);
        const __m128i m =
            _mm_load_si128((const __m128i *)HighbdBaseMask[left_cols]);
        _mm_storel_epi64((__m128i *)dst, _mm_blendv_epi8(a, l, m));
      }
    }
  } else if (left_width == 8) {
    for (int r = 0; r < bh; ++r, dst += stride, left_pred += 8, x -= dx) {
      const int left_cols = AVMMIN(min_base - (x >> 6), bw);
      if (left_cols <= 0) continue;
      const __m128i l = _mm_load_si128((const __m128i *)left_pred);
      if (left_cols >= 8) {
        _mm_storeu_si128((__m128i *)dst, l);
      } else {
        const __m128i a = _mm_loadu_si128((const __m128i *)dst);
        const __m128i m =
            _mm_load_si128((const __m128i *)HighbdBaseMask[left_cols]);
        _mm_storeu_si128((__m128i *)dst, _mm_blendv_epi8(a, l, m));
      }
    }
  } else {
    for (int r = 0; r < bh;
         ++r, dst += stride, left_pred += left_width, x -= dx) {
      const int left_cols = AVMMIN(min_base - (x >> 6), bw);
      if (left_cols <= 0) continue;
      const int n16 = left_cols >> 4;
      if (n16 >= 1) {
        _mm256_storeu_si256(
            (__m256i *)(dst + 0),
            _mm256_load_si256((const __m256i *)(left_pred + 0)));
        if (n16 >= 2) {
          _mm256_storeu_si256(
              (__m256i *)(dst + 16),
              _mm256_load_si256((const __m256i *)(left_pred + 16)));
          if (n16 >= 3) {
            _mm256_storeu_si256(
                (__m256i *)(dst + 32),
                _mm256_load_si256((const __m256i *)(left_pred + 32)));
            if (n16 >= 4) {
              _mm256_storeu_si256(
                  (__m256i *)(dst + 48),
                  _mm256_load_si256((const __m256i *)(left_pred + 48)));
            }
          }
        }
      }
      const int j = n16 << 4;
      if (j < left_cols) {
        const __m256i a = _mm256_loadu_si256((const __m256i *)(dst + j));
        const __m256i l = _mm256_load_si256((const __m256i *)(left_pred + j));
        const __m256i m =
            _mm256_load_si256((const __m256i *)HighbdBaseMask[left_cols - j]);
        _mm256_storeu_si256((__m256i *)(dst + j), _mm256_blendv_epi8(a, l, m));
      }
    }
  }
}

static void dr_prediction_z2_4xN_avx2(uint16_t *dst, ptrdiff_t stride, int bh,
                                      const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_4xN_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + 3 < min_base) {
    dr_prediction_z3_avx2(dst, stride, 4, bh, left, -dy, bd, mrl_index, 0);
    return;
  }

  DECLARE_ALIGNED(32, uint16_t, left_pred[4 * 64]);
  dr_prediction_z1_4xN_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 1);
  dr_prediction_z3_avx2(left_pred, 4, 4, bh, left, -dy, bd, mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, 4, 4, bh, dx, mrl_index);
}

static void dr_prediction_z2_8xN_avx2(uint16_t *dst, ptrdiff_t stride, int bh,
                                      const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_8xN_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + 7 < min_base) {
    dr_prediction_z3_avx2(dst, stride, 8, bh, left, -dy, bd, mrl_index, 0);
    return;
  }

  const int left_width = get_z2_left_width(8, bh, dx, mrl_index);
  DECLARE_ALIGNED(32, uint16_t, left_pred[8 * 64]);
  dr_prediction_z1_8xN_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 1);
  dr_prediction_z3_avx2(left_pred, left_width, left_width, bh, left, -dy, bd,
                        mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, left_width, 8, bh, dx,
                           mrl_index);
}

static void dr_prediction_z2_MxN_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  assert(bw == 16 || bw == 32 || bw == 64);
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_MxN_avx2(dst, stride, bw, bh, above, -dx, bd, mrl_index,
                              0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + bw - 1 < min_base) {
    dr_prediction_z3_avx2(dst, stride, bw, bh, left, -dy, bd, mrl_index, 0);
    return;
  }

  const int left_width = get_z2_left_width(bw, bh, dx, mrl_index);
  DECLARE_ALIGNED(32, uint16_t, left_pred[64 * 64]);
  dr_prediction_z1_MxN_avx2(dst, stride, bw, bh, above, -dx, bd, mrl_index, 1);
  dr_prediction_z3_avx2(left_pred, left_width, left_width, bh, left, -dy, bd,
                        mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, left_width, bw, bh, dx,
                           mrl_index);
}

// Directional prediction, zone 2: 90 < angle < 180
void av2_highbd_dr_prediction_z2_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  assert(dx > 0);
  assert(dy > 0);

  switch (bw) {
    case 4:
      dr_prediction_z2_4xN_avx2(dst, stride, bh, above, left, dx, dy, bd,
                                mrl_index);
      break;
    case 8:
      dr_prediction_z2_8xN_avx2(dst, stride, bh, above, left, dx, dy, bd,
                                mrl_index);
      break;
    case 16:
    case 32:
    case 64:
      dr_prediction_z2_MxN_avx2(dst, stride, bw, bh, above, left, dx, dy, bd,
                                mrl_index);
      break;
    default: assert(0); break;
  }
}

// Directional prediction, zone 3: 180 < angle < 270
void av2_highbd_dr_prediction_z3_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int bd, int mrl_index) {
  (void)above;
  (void)dx;

  assert(dx == 1);
  assert(dy > 0);

  dr_prediction_z3_avx2(dst, stride, bw, bh, left, dy, bd, mrl_index, 0);
}

static AVM_FORCE_INLINE void lowbd_prepare_coeffs_4tap(const int16_t *filter,
                                                       __m256i *coeffs) {
  const __m256i filter_coeffs =
      _mm256_broadcastq_epi64(_mm_loadl_epi64((const __m128i *)filter));
  coeffs[0] =
      _mm256_shuffle_epi8(filter_coeffs, _mm256_set1_epi16((int16_t)0x0200));
  coeffs[1] =
      _mm256_shuffle_epi8(filter_coeffs, _mm256_set1_epi16((int16_t)0x0604));
}

static AVM_FORCE_INLINE void lowbd_prepare_coeffs_4tap_2rows(
    const int16_t *filter0, const int16_t *filter1, __m256i *coeffs) {
  const __m128i coeff_0 = _mm_loadl_epi64((const __m128i *)filter0);
  const __m128i coeff_1 = _mm_loadl_epi64((const __m128i *)filter1);
  const __m256i filter_coeffs = yy_set_m128i(coeff_1, coeff_0);
  coeffs[0] =
      _mm256_shuffle_epi8(filter_coeffs, _mm256_set1_epi16((int16_t)0x0200));
  coeffs[1] =
      _mm256_shuffle_epi8(filter_coeffs, _mm256_set1_epi16((int16_t)0x0604));
}

static AVM_FORCE_INLINE void highbd_prepare_coeffs_4tap(const int16_t *filter,
                                                        __m256i *coeffs) {
  const __m256i filter_coeffs =
      _mm256_broadcastq_epi64(_mm_loadl_epi64((const __m128i *)filter));
  coeffs[0] = _mm256_shuffle_epi32(filter_coeffs, 0x00);
  coeffs[1] = _mm256_shuffle_epi32(filter_coeffs, 0x55);
}

static AVM_FORCE_INLINE void highbd_prepare_coeffs_4tap_2rows(
    const int16_t *filter0, const int16_t *filter1, __m256i *coeffs) {
  const __m128i coeff_0 = _mm_loadl_epi64((const __m128i *)filter0);
  const __m128i coeff_1 = _mm_loadl_epi64((const __m128i *)filter1);
  const __m256i filter_coeffs = yy_set_m128i(coeff_1, coeff_0);
  coeffs[0] = _mm256_shuffle_epi32(filter_coeffs, 0x00);
  coeffs[1] = _mm256_shuffle_epi32(filter_coeffs, 0x55);
}

// At shift == 0, tap1 = 128 overflows int8_t to -128 (making coeffs[0] < 0 in
// int16_t); _mm256_sign_epi16() negates that product back to +128 * p1.
static AVM_FORCE_INLINE __m256i lowbd_convolve_4tap(__m256i data,
                                                    const __m256i *coeffs,
                                                    const __m256i *filt) {
  const __m256i s0 = _mm256_shuffle_epi8(data, filt[0]);
  const __m256i s1 = _mm256_shuffle_epi8(data, filt[1]);
  const __m256i res_01 =
      _mm256_sign_epi16(_mm256_maddubs_epi16(s0, coeffs[0]), coeffs[0]);
  const __m256i res_23 = _mm256_maddubs_epi16(s1, coeffs[1]);
  return _mm256_adds_epi16(res_01, res_23);
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_idif_4xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 4;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) > max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const =
      _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i zero = _mm256_setzero_si256();
  const __m256i filt[2] = {
    _mm256_load_si256((const __m256i *)filt1_global_avx2),
    _mm256_load_si256((const __m256i *)filt2_global_avx2),
  };

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x + 1, base1);

    const int shift0 = (x & 0x3F) >> 1;
    const int shift1 = (x1 & 0x3F) >> 1;
    const __m256i row01 = yy_loadu2_128(above + base1 - 1, above + base0 - 1);
    const __m256i data = _mm256_packus_epi16(row01, row01);
    __m256i coeffs[2];
    lowbd_prepare_coeffs_4tap_2rows(av2_dr_interp_filter[shift0],
                                    av2_dr_interp_filter[shift1], coeffs);
    __m256i res = lowbd_convolve_4tap(data, coeffs, filt);
    res = _mm256_srai_epi16(_mm256_adds_epi16(res, round_const),
                            POWER_DR_INTERP_FILTER);
    res = _mm256_max_epi16(res, zero);
    if (!is_z2_mixed && (base1 + bw - 1) > max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x_plus_1);
    }
    _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
    _mm_storel_epi64((__m128i *)(dst + stride),
                     _mm256_extracti128_si256(res, 1));
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_idif_4xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 4;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) > max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const =
      _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i max_pixel_val = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i filt[2] = {
    _mm256_load_si256((const __m256i *)ip_shuffle_f2f3),
    _mm256_load_si256((const __m256i *)ip_shuffle_f4f5),
  };

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x + 1, base1);

    const int shift0 = (x & 0x3F) >> 1;
    const int shift1 = (x1 & 0x3F) >> 1;
    const __m256i data = yy_loadu2_128(above + base1 - 1, above + base0 - 1);
    const __m256i s[2] = {
      _mm256_shuffle_epi8(data, filt[0]),
      _mm256_shuffle_epi8(data, filt[1]),
    };
    __m256i coeffs[2];
    highbd_prepare_coeffs_4tap_2rows(av2_dr_interp_filter[shift0],
                                     av2_dr_interp_filter[shift1], coeffs);
    __m256i res = convolve_4tap(s, coeffs);
    res = _mm256_srai_epi32(_mm256_add_epi32(res, round_const),
                            POWER_DR_INTERP_FILTER);
    res = _mm256_min_epu16(_mm256_packus_epi32(res, res), max_pixel_val);
    if (!is_z2_mixed && (base1 + bw - 1) > max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x_plus_1);
    }
    _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
    _mm_storel_epi64((__m128i *)(dst + stride),
                     _mm256_extracti128_si256(res, 1));
  }
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_idif_8xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 8;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) > max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const =
      _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i zero = _mm256_setzero_si256();
  const __m256i filt[2] = {
    _mm256_load_si256((const __m256i *)filt1_global_avx2),
    _mm256_load_si256((const __m256i *)filt2_global_avx2),
  };

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x + 1, base1);

    const int shift0 = (x & 0x3F) >> 1;
    const int shift1 = (x1 & 0x3F) >> 1;
    const __m256i row0 =
        _mm256_loadu_si256((const __m256i *)(above + base0 - 1));
    const __m256i row1 =
        _mm256_loadu_si256((const __m256i *)(above + base1 - 1));
    const __m256i data =
        _mm256_permute4x64_epi64(_mm256_packus_epi16(row0, row1), 0xD8);
    __m256i coeffs[2];
    lowbd_prepare_coeffs_4tap_2rows(av2_dr_interp_filter[shift0],
                                    av2_dr_interp_filter[shift1], coeffs);
    __m256i res = lowbd_convolve_4tap(data, coeffs, filt);
    res = _mm256_srai_epi16(_mm256_adds_epi16(res, round_const),
                            POWER_DR_INTERP_FILTER);
    res = _mm256_max_epi16(res, zero);
    if (!is_z2_mixed && (base1 + bw - 1) > max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x_plus_1);
    }
    yy_storeu2_128(dst + stride, dst, res);
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_idif_8xN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  assert(bh <= 64);
  assert((bh & 1) == 0);
  assert(dx != 0);

  const int frac_bits = 6;
  const int bw = 8;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  int x = dx * (1 + mrl_index);
  if (!is_z2_mixed && (x >> frac_bits) > max_base_x) {
    store_above_max_base_x(dst, stride, bw, bh, 0, above_max_base_x);
    return;
  }

  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_2row_avx2);
  const __m256i round_const =
      _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i max_pixel_val = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i interleave =
      _mm256_load_si256((const __m256i *)dr_interleave_epi16_avx2);

  for (int r = 0; r < bh; r += 2, dst += 2 * stride, x += 2 * dx) {
    int base0 = x >> frac_bits;
    if (is_z2_mixed && base0 + bw - 1 < min_base) return;
    if (!is_z2_mixed && base0 > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }
    const int x1 = x + dx;
    int base1 = x1 >> frac_bits;
    base1 = is_z2_mixed ? AVMMAX(min_base - bw + 1, base1)
                        : AVMMIN(max_base_x + 1, base1);

    const int shift0 = (x & 0x3F) >> 1;
    const int shift1 = (x1 & 0x3F) >> 1;
    const __m256i row0 =
        _mm256_loadu_si256((const __m256i *)(above + base0 - 1));
    const __m256i row1 =
        _mm256_loadu_si256((const __m256i *)(above + base1 - 1));
    const __m256i r0 = _mm256_permute2x128_si256(row0, row1, 0x20);
    const __m256i r1 = _mm256_permute2x128_si256(row0, row1, 0x31);
    __m256i coeffs[2];
    highbd_prepare_coeffs_4tap_2rows(av2_dr_interp_filter[shift0],
                                     av2_dr_interp_filter[shift1], coeffs);
    const __m256i s_even[2] = { r0, _mm256_alignr_epi8(r1, r0, 4) };
    const __m256i s_odd[2] = {
      _mm256_alignr_epi8(r1, r0, 2),
      _mm256_alignr_epi8(r1, r0, 6),
    };
    __m256i res_even = convolve_4tap(s_even, coeffs);
    __m256i res_odd = convolve_4tap(s_odd, coeffs);
    res_even = _mm256_srai_epi32(_mm256_add_epi32(res_even, round_const),
                                 POWER_DR_INTERP_FILTER);
    res_odd = _mm256_srai_epi32(_mm256_add_epi32(res_odd, round_const),
                                POWER_DR_INTERP_FILTER);
    __m256i res =
        _mm256_shuffle_epi8(_mm256_packus_epi32(res_even, res_odd), interleave);
    res = _mm256_min_epu16(res, max_pixel_val);
    if (!is_z2_mixed && (base1 + bw - 1) > max_base_x) {
      const __m256i base01 =
          yy_set_m128i(_mm_set1_epi16(base1), _mm_set1_epi16(base0));
      res = blend_out_of_bounds_pixels(res, above_max_base_x, base01, c0123,
                                       max_base_x_plus_1);
    }
    yy_storeu2_128(dst + stride, dst, res);
  }
}

static AVM_FORCE_INLINE void lowbd_dr_prediction_z1_idif_MxN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  assert(bh <= 64);
  assert(dx != 0);

  const int frac_bits = 6;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_16w_avx2);
  const __m256i round_const =
      _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i zero = _mm256_setzero_si256();
  const __m256i filt[2] = {
    _mm256_load_si256((const __m256i *)filt1_global_avx2),
    _mm256_load_si256((const __m256i *)filt2_global_avx2),
  };

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < bh; ++r, dst += stride, x += dx) {
    const int base = x >> frac_bits;
    if (is_z2_mixed && base + bw - 1 < min_base) return;
    if (!is_z2_mixed && base > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }

    const int shift = (x & 0x3F) >> 1;
    __m256i coeffs[2];
    lowbd_prepare_coeffs_4tap(av2_dr_interp_filter[shift], coeffs);
    const int j_start =
        (is_z2_mixed && min_base - base > 7) ? ((min_base - base) & ~7) : 0;
    for (int j = j_start; j < bw; j += 16) {
      __m256i res;
      if (!is_z2_mixed && base + j > max_base_x) {
        res = above_max_base_x;
      } else {
        const __m256i r0 =
            _mm256_loadu_si256((const __m256i *)(above + base - 1 + j));
        const __m256i r1 =
            _mm256_loadu_si256((const __m256i *)(above + base - 1 + j + 8));
        const __m256i data = _mm256_packus_epi16(r0, r1);
        res = lowbd_convolve_4tap(data, coeffs, filt);
        res = _mm256_srai_epi16(_mm256_adds_epi16(res, round_const),
                                POWER_DR_INTERP_FILTER);
        res = _mm256_max_epi16(res, zero);
        if (!is_z2_mixed && (base + j + 15) > max_base_x) {
          res = blend_out_of_bounds_pixels(res, above_max_base_x,
                                           _mm256_set1_epi16(base + j), c0123,
                                           max_base_x_plus_1);
        }
      }
      if (is_z2_mixed && j + 16 > bw) {
        _mm_storeu_si128((__m128i *)(dst + j), _mm256_castsi256_si128(res));
      } else {
        _mm256_storeu_si256((__m256i *)(dst + j), res);
      }
    }
  }
}

static AVM_FORCE_INLINE void highbd_dr_prediction_z1_idif_MxN_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int bd, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  assert(bh <= 64);
  assert(dx != 0);

  const int frac_bits = 6;
  const int min_base = -(1 + mrl_index);
  const int max_base_x = bw + bh - 1 + (mrl_index << 1);
  const __m256i above_max_base_x = _mm256_set1_epi16(above[max_base_x]);
  const __m256i max_base_x_plus_1 = _mm256_set1_epi16(max_base_x + 1);
  const __m256i c0123 = _mm256_load_si256((const __m256i *)c0123_16w_avx2);
  const __m256i round_const =
      _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));
  const __m256i max_pixel_val = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i interleave =
      _mm256_load_si256((const __m256i *)dr_interleave_epi16_avx2);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < bh; ++r, dst += stride, x += dx) {
    const int base = x >> frac_bits;
    if (is_z2_mixed && base + bw - 1 < min_base) return;
    if (!is_z2_mixed && base > max_base_x) {
      store_above_max_base_x(dst, stride, bw, bh, r, above_max_base_x);
      return;
    }

    const int shift = (x & 0x3F) >> 1;
    __m256i coeffs[2];
    highbd_prepare_coeffs_4tap(av2_dr_interp_filter[shift], coeffs);
    const int j_start =
        (is_z2_mixed && min_base - base > 7) ? ((min_base - base) & ~7) : 0;
    for (int j = j_start; j < bw; j += 16) {
      __m256i res;
      if (!is_z2_mixed && base + j > max_base_x) {
        res = above_max_base_x;
      } else {
        const __m256i s_even[2] = {
          _mm256_loadu_si256((const __m256i *)(above + base - 1 + j)),
          _mm256_loadu_si256((const __m256i *)(above + base + 1 + j)),
        };
        const __m256i s_odd[2] = {
          _mm256_loadu_si256((const __m256i *)(above + base + j)),
          _mm256_loadu_si256((const __m256i *)(above + base + 2 + j)),
        };
        __m256i res_even = convolve_4tap(s_even, coeffs);
        __m256i res_odd = convolve_4tap(s_odd, coeffs);
        res_even = _mm256_srai_epi32(_mm256_add_epi32(res_even, round_const),
                                     POWER_DR_INTERP_FILTER);
        res_odd = _mm256_srai_epi32(_mm256_add_epi32(res_odd, round_const),
                                    POWER_DR_INTERP_FILTER);
        res = _mm256_shuffle_epi8(_mm256_packus_epi32(res_even, res_odd),
                                  interleave);
        res = _mm256_min_epu16(res, max_pixel_val);
        if (!is_z2_mixed && (base + j + 15) > max_base_x) {
          res = blend_out_of_bounds_pixels(res, above_max_base_x,
                                           _mm256_set1_epi16(base + j), c0123,
                                           max_base_x_plus_1);
        }
      }
      if (is_z2_mixed && j + 16 > bw) {
        _mm_storeu_si128((__m128i *)(dst + j), _mm256_castsi256_si128(res));
      } else {
        _mm256_storeu_si256((__m256i *)(dst + j), res);
      }
    }
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_4xN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  if (bd == 8) {
    lowbd_dr_prediction_z1_idif_4xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                         is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_idif_4xN_avx2(dst, stride, bh, above, dx, bd,
                                          mrl_index, is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_8xN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bh, const uint16_t *above, int dx,
    int bd, int mrl_index, int is_z2_mixed) {
  if (bd == 8) {
    lowbd_dr_prediction_z1_idif_8xN_avx2(dst, stride, bh, above, dx, mrl_index,
                                         is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_idif_8xN_avx2(dst, stride, bh, above, dx, bd,
                                          mrl_index, is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_MxN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int bd, int mrl_index, int is_z2_mixed) {
  assert(bw == 16 || bw == 32 || bw == 64);
  if (bd == 8) {
    lowbd_dr_prediction_z1_idif_MxN_avx2(dst, stride, bw, bh, above, dx,
                                         mrl_index, is_z2_mixed);
  } else {
    assert(bd == 10 || bd == 12);
    highbd_dr_prediction_z1_idif_MxN_avx2(dst, stride, bw, bh, above, dx, bd,
                                          mrl_index, is_z2_mixed);
  }
}

static AVM_FORCE_INLINE void dr_prediction_z1_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    int dx, int bd, int mrl_index, int is_z2_mixed) {
  switch (bw) {
    case 4:
      dr_prediction_z1_4xN_idif_avx2(dst, stride, bh, above, dx, bd, mrl_index,
                                     is_z2_mixed);
      break;
    case 8:
      dr_prediction_z1_8xN_idif_avx2(dst, stride, bh, above, dx, bd, mrl_index,
                                     is_z2_mixed);
      break;
    case 16:
    case 32:
    case 64:
      dr_prediction_z1_MxN_idif_avx2(dst, stride, bw, bh, above, dx, bd,
                                     mrl_index, is_z2_mixed);
      break;
    default: assert(0); break;
  }
}

// Directional prediction, zone 1: 0 < angle < 90 using IDIF
void av2_highbd_dr_prediction_z1_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)left;
  (void)dy;

  assert(dy == 1);
  assert(dx > 0);

  dr_prediction_z1_idif_avx2(dst, stride, bw, bh, above, dx, bd, mrl_index, 0);
}

static void dr_prediction_z3_idif_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                       int bh, const uint16_t *left, int dy,
                                       int bd, int mrl_index, int is_z2_mixed) {
  DECLARE_ALIGNED(32, uint16_t, temp_dst[64 * 64]);
  dr_prediction_z1_idif_avx2(temp_dst, bh, bh, bw, left, dy, bd, mrl_index,
                             is_z2_mixed);
  highbd_transpose(temp_dst, bh, dst, stride, bw, bh);
}

static void dr_prediction_z2_4xN_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bh, const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_4xN_idif_avx2(dst, stride, bh, above, -dx, bd, mrl_index,
                                   0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + 3 < min_base) {
    dr_prediction_z3_idif_avx2(dst, stride, 4, bh, left, -dy, bd, mrl_index, 0);
    return;
  }

  DECLARE_ALIGNED(32, uint16_t, left_pred[4 * 64]);
  dr_prediction_z1_4xN_idif_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 1);
  dr_prediction_z3_idif_avx2(left_pred, 4, 4, bh, left, -dy, bd, mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, 4, 4, bh, dx, mrl_index);
}

static void dr_prediction_z2_8xN_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bh, const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_8xN_idif_avx2(dst, stride, bh, above, -dx, bd, mrl_index,
                                   0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + 7 < min_base) {
    dr_prediction_z3_idif_avx2(dst, stride, 8, bh, left, -dy, bd, mrl_index, 0);
    return;
  }

  const int left_width = get_z2_left_width(8, bh, dx, mrl_index);
  DECLARE_ALIGNED(32, uint16_t, left_pred[8 * 64]);
  dr_prediction_z1_8xN_idif_avx2(dst, stride, bh, above, -dx, bd, mrl_index, 1);
  dr_prediction_z3_idif_avx2(left_pred, left_width, left_width, bh, left, -dy,
                             bd, mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, left_width, 8, bh, dx,
                           mrl_index);
}

static void dr_prediction_z2_MxN_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  assert(bw == 16 || bw == 32 || bw == 64);
  const int min_base = -(1 + mrl_index);
  if (((-(bh + mrl_index) * dx) >> 6) >= min_base) {
    dr_prediction_z1_MxN_idif_avx2(dst, stride, bw, bh, above, -dx, bd,
                                   mrl_index, 0);
    return;
  }
  if (((-(1 + mrl_index) * dx) >> 6) + bw - 1 < min_base) {
    dr_prediction_z3_idif_avx2(dst, stride, bw, bh, left, -dy, bd, mrl_index,
                               0);
    return;
  }

  const int left_width = get_z2_left_width(bw, bh, dx, mrl_index);
  DECLARE_ALIGNED(32, uint16_t, left_pred[64 * 64]);
  dr_prediction_z1_MxN_idif_avx2(dst, stride, bw, bh, above, -dx, bd, mrl_index,
                                 1);
  dr_prediction_z3_idif_avx2(left_pred, left_width, left_width, bh, left, -dy,
                             bd, mrl_index, 1);
  blend_z2_above_left_avx2(dst, stride, left_pred, left_width, bw, bh, dx,
                           mrl_index);
}

// Directional prediction, zone 2: 90 < angle < 180 using IDIF
void av2_highbd_dr_prediction_z2_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  assert(dx > 0);
  assert(dy > 0);

  switch (bw) {
    case 4:
      dr_prediction_z2_4xN_idif_avx2(dst, stride, bh, above, left, dx, dy, bd,
                                     mrl_index);
      break;
    case 8:
      dr_prediction_z2_8xN_idif_avx2(dst, stride, bh, above, left, dx, dy, bd,
                                     mrl_index);
      break;
    case 16:
    case 32:
    case 64:
      dr_prediction_z2_MxN_idif_avx2(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, mrl_index);
      break;
    default: assert(0); break;
  }
}

// Directional prediction, zone 3: 180 < angle < 270 using IDIF
void av2_highbd_dr_prediction_z3_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)above;
  (void)dx;

  assert(dx == 1);
  assert(dy > 0);

  dr_prediction_z3_idif_avx2(dst, stride, bw, bh, left, dy, bd, mrl_index, 0);
}

static INLINE __m256i highbd_smooth_dist_weights_avx2(__m256i idx, int size) {
  return _mm256_or_si256(
      _mm256_sub_epi32(_mm256_set1_epi32(size - 1), idx),
      _mm256_slli_epi32(_mm256_add_epi32(idx, _mm256_set1_epi32(1)), 16));
}

static INLINE __m256i highbd_smooth_blend_weights_avx2(__m256i idx,
                                                       __m128i scale) {
  const __m256i shift = _mm256_min_epi32(
      _mm256_sra_epi32(_mm256_slli_epi32(idx, 1), scale), _mm256_set1_epi32(6));
  const __m256i weight = _mm256_srlv_epi32(_mm256_set1_epi32(32), shift);
  return _mm256_or_si256(_mm256_sub_epi32(_mm256_set1_epi32(64), weight),
                         _mm256_slli_epi32(weight, 16));
}

static AVM_FORCE_INLINE __m256i highbd_smooth_pred_avx2(
    PREDICTION_MODE mode, __m256i top_bl, __m256i top_hi, __m256i weights_v,
    __m256i s_top, __m256i left_tr, __m256i left_hi, __m256i weights_h,
    __m256i s_left, int bh_log2, int bw_log2) {
  const __m256i round_const = _mm256_set1_epi32(32);
  __m256i predv = _mm256_setzero_si256();
  __m256i predh = _mm256_setzero_si256();
  if (mode != SMOOTH_H_PRED) {
    predv = _mm256_madd_epi16(top_bl, weights_v);
    predv = _mm256_srai_epi32(
        _mm256_add_epi32(predv, _mm256_set1_epi32(1 << (bh_log2 - 1))),
        bh_log2);
    predv = _mm256_madd_epi16(_mm256_or_si256(predv, top_hi), s_top);
    predv = _mm256_srai_epi32(_mm256_add_epi32(predv, round_const), 6);
  }

  if (mode != SMOOTH_V_PRED) {
    predh = _mm256_madd_epi16(left_tr, weights_h);
    predh = _mm256_srai_epi32(
        _mm256_add_epi32(predh, _mm256_set1_epi32(1 << (bw_log2 - 1))),
        bw_log2);
    predh = _mm256_madd_epi16(_mm256_or_si256(predh, left_hi), s_left);
    predh = _mm256_srai_epi32(_mm256_add_epi32(predh, round_const), 6);
  }

  if (mode == SMOOTH_V_PRED) return predv;
  if (mode == SMOOTH_H_PRED) return predh;

  return _mm256_srai_epi32(
      _mm256_add_epi32(_mm256_add_epi32(predv, predh), _mm256_set1_epi32(1)),
      1);
}

static AVM_FORCE_INLINE void highbd_smooth_predictor_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    const uint16_t *left, PREDICTION_MODE mode) {
  const int bw_log2 = get_msb(bw);
  const int bh_log2 = get_msb(bh);
  const __m128i scale =
      _mm_cvtsi32_si128(ROUND_POWER_OF_TWO(bh_log2 - 2 + bw_log2 - 2, 2));
  const int bl = left[bh];   // estimated by bottom-left pixel
  const int tr = above[bw];  // estimated by top-right pixel
  const __m256i bl_hi = _mm256_slli_epi32(_mm256_set1_epi32(bl), 16);
  const __m256i tr_hi = _mm256_slli_epi32(_mm256_set1_epi32(tr), 16);
  const __m256i idx = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);

  if (bw == 4) {
    const __m256i col = _mm256_and_si256(idx, _mm256_set1_epi32(3));
    const __m128i above4 = _mm_loadl_epi64((const __m128i *)above);
    const __m256i top =
        _mm256_cvtepu16_epi32(_mm_unpacklo_epi64(above4, above4));
    const __m256i top_bl = _mm256_or_si256(top, bl_hi);
    const __m256i top_hi = _mm256_slli_epi32(top, 16);
    const __m256i weights_h = highbd_smooth_dist_weights_avx2(col, bw);
    const __m256i s_left = highbd_smooth_blend_weights_avx2(col, scale);
    __m256i row = _mm256_srli_epi32(idx, 2);
    for (int r = 0; r < bh; r += 2) {
      __m128i left2 = xx_loadl_32(left + r);
      left2 = _mm_unpacklo_epi16(left2, left2);
      left2 = _mm_unpacklo_epi32(left2, left2);
      const __m256i left_row = _mm256_cvtepu16_epi32(left2);
      const __m256i pred = highbd_smooth_pred_avx2(
          mode, top_bl, top_hi, highbd_smooth_dist_weights_avx2(row, bh),
          highbd_smooth_blend_weights_avx2(row, scale),
          _mm256_or_si256(left_row, tr_hi), _mm256_slli_epi32(left_row, 16),
          weights_h, s_left, bh_log2, bw_log2);
      const __m128i pred16 = _mm_packus_epi32(
          _mm256_castsi256_si128(pred), _mm256_extracti128_si256(pred, 1));
      _mm_storel_epi64((__m128i *)dst, pred16);
      _mm_storeh_pd((double *)(dst + stride), _mm_castsi128_pd(pred16));
      row = _mm256_add_epi32(row, _mm256_set1_epi32(2));
      dst += 2 * stride;
    }
    return;
  }

  DECLARE_ALIGNED(32, int32_t, weights_v_tab[64]);
  DECLARE_ALIGNED(32, int32_t, s_top_tab[64]);
  DECLARE_ALIGNED(32, int32_t, left_tr_tab[64]);
  DECLARE_ALIGNED(32, int32_t, left_hi_tab[64]);
  for (int r = 0; r < bh; r += 8) {
    const __m256i row = _mm256_add_epi32(_mm256_set1_epi32(r), idx);
    if (mode != SMOOTH_H_PRED) {
      _mm256_store_si256((__m256i *)(weights_v_tab + r),
                         highbd_smooth_dist_weights_avx2(row, bh));
      _mm256_store_si256((__m256i *)(s_top_tab + r),
                         highbd_smooth_blend_weights_avx2(row, scale));
    }
    if (mode != SMOOTH_V_PRED) {
      const __m256i left_row = _mm256_cvtepu16_epi32(
          bh == 4 ? _mm_loadl_epi64((const __m128i *)left)
                  : _mm_loadu_si128((const __m128i *)(left + r)));
      _mm256_store_si256((__m256i *)(left_tr_tab + r),
                         _mm256_or_si256(left_row, tr_hi));
      _mm256_store_si256((__m256i *)(left_hi_tab + r),
                         _mm256_slli_epi32(left_row, 16));
    }
  }

  const __m256i zero = _mm256_setzero_si256();
  const int has_v = mode != SMOOTH_H_PRED;
  const int has_h = mode != SMOOTH_V_PRED;
  for (int c = 0; c < bw; c += 8) {
    const __m256i col = _mm256_add_epi32(_mm256_set1_epi32(c), idx);
    const __m256i top =
        _mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i *)(above + c)));
    const __m256i top_bl = _mm256_or_si256(top, bl_hi);
    const __m256i top_hi = _mm256_slli_epi32(top, 16);
    const __m256i weights_h = highbd_smooth_dist_weights_avx2(col, bw);
    const __m256i s_left = highbd_smooth_blend_weights_avx2(col, scale);
    uint16_t *dst_col = dst + c;
    for (int r = 0; r < bh; ++r) {
      const __m256i pred = highbd_smooth_pred_avx2(
          mode, top_bl, top_hi,
          has_v ? _mm256_set1_epi32(weights_v_tab[r]) : zero,
          has_v ? _mm256_set1_epi32(s_top_tab[r]) : zero,
          has_h ? _mm256_set1_epi32(left_tr_tab[r]) : zero,
          has_h ? _mm256_set1_epi32(left_hi_tab[r]) : zero, weights_h, s_left,
          bh_log2, bw_log2);
      _mm_storeu_si128((__m128i *)dst_col,
                       _mm_packus_epi32(_mm256_castsi256_si128(pred),
                                        _mm256_extracti128_si256(pred, 1)));
      dst_col += stride;
    }
  }
}

#define HIGHBD_SMOOTH_PRED_AVX2(type, mode, w, h)                       \
  void avm_highbd_##type##_predictor_##w##x##h##_avx2(                  \
      uint16_t *dst, ptrdiff_t stride, const uint16_t *above,           \
      const uint16_t *left, int bd) {                                   \
    (void)bd;                                                           \
    highbd_smooth_predictor_avx2(dst, stride, w, h, above, left, mode); \
  }

#define HIGHBD_SMOOTH_PRED_AVX2_ALL_SIZES(type, mode) \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 4, 4)           \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 4, 8)           \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 4, 16)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 4, 32)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 4, 64)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 8, 4)           \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 8, 8)           \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 8, 16)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 8, 32)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 8, 64)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 16, 4)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 16, 8)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 16, 16)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 16, 32)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 16, 64)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 32, 4)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 32, 8)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 32, 16)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 32, 32)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 32, 64)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 64, 4)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 64, 8)          \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 64, 16)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 64, 32)         \
  HIGHBD_SMOOTH_PRED_AVX2(type, mode, 64, 64)

HIGHBD_SMOOTH_PRED_AVX2_ALL_SIZES(smooth, SMOOTH_PRED)
HIGHBD_SMOOTH_PRED_AVX2_ALL_SIZES(smooth_v, SMOOTH_V_PRED)
HIGHBD_SMOOTH_PRED_AVX2_ALL_SIZES(smooth_h, SMOOTH_H_PRED)

#undef HIGHBD_SMOOTH_PRED_AVX2_ALL_SIZES
#undef HIGHBD_SMOOTH_PRED_AVX2

static INLINE __m256i highbd_ibp_weight_pairs_avx2(__m256i weights) {
  return _mm256_or_si256(
      weights,
      _mm256_slli_epi32(
          _mm256_sub_epi32(_mm256_set1_epi32(IBP_WEIGHT_MAX), weights), 16));
}

static AVM_FORCE_INLINE void highbd_ibp_blend_16_avx2(
    uint16_t *dst, const uint16_t *second_pred, __m256i weights_lo,
    __m256i weights_hi) {
  const __m256i round_const = _mm256_set1_epi32(1 << (IBP_WEIGHT_SHIFT - 1));
  const __m256i dst_vec = _mm256_loadu_si256((const __m256i *)dst);
  const __m256i second_vec = _mm256_loadu_si256((const __m256i *)second_pred);
  __m256i lo =
      _mm256_madd_epi16(_mm256_unpacklo_epi16(dst_vec, second_vec), weights_lo);
  __m256i hi =
      _mm256_madd_epi16(_mm256_unpackhi_epi16(dst_vec, second_vec), weights_hi);
  lo = _mm256_srai_epi32(_mm256_add_epi32(lo, round_const), IBP_WEIGHT_SHIFT);
  hi = _mm256_srai_epi32(_mm256_add_epi32(hi, round_const), IBP_WEIGHT_SHIFT);
  _mm256_storeu_si256((__m256i *)dst, _mm256_packus_epi32(lo, hi));
}

static AVM_FORCE_INLINE __m128i highbd_ibp_blend_4_avx2(__m128i dst_second,
                                                        __m128i weights) {
  return _mm_srai_epi32(
      _mm_add_epi32(_mm_madd_epi16(dst_second, weights),
                    _mm_set1_epi32(1 << (IBP_WEIGHT_SHIFT - 1))),
      IBP_WEIGHT_SHIFT);
}

static AVM_FORCE_INLINE void highbd_ibp_dr_prediction_avx2(
    const IbpWeightsType weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90],
    int mode_idx, uint16_t *dst, ptrdiff_t stride, const uint16_t *second_pred,
    ptrdiff_t second_stride, int bw, int bh, int is_z3) {
  assert(bw >= 4 && bw <= 64 && bh >= 4 && bh <= 64);
  const int row_shift = bh >> (IBP_WEIGHT_SIZE_LOG2 + 1);
  const ptrdiff_t col_step =
      is_z3 ? IBP_WEIGHT_SIZE * DIR_MODES_0_90 : DIR_MODES_0_90;
  const ptrdiff_t row_step =
      is_z3 ? DIR_MODES_0_90 : IBP_WEIGHT_SIZE * DIR_MODES_0_90;
  const IbpWeightsType *weight_row = &weights[0][0][mode_idx];
  __m256i weight_pairs[8];
  __m128i weight_pairs_128[2];

  for (int r = 0; r < bh; ++r) {
    if ((r & ((1 << row_shift) - 1)) == 0) {
#define IBP_WEIGHT(i) ((uint8_t)weight_row[(i) * col_step])
      if (bw == 4) {
        weight_pairs_128[0] =
            _mm256_castsi256_si128(highbd_ibp_weight_pairs_avx2(
                _mm256_setr_epi32(IBP_WEIGHT(0), IBP_WEIGHT(1), IBP_WEIGHT(2),
                                  IBP_WEIGHT(3), 0, 0, 0, 0)));
      } else if (bw == 8) {
        const __m256i pairs = highbd_ibp_weight_pairs_avx2(_mm256_setr_epi32(
            IBP_WEIGHT(0), IBP_WEIGHT(1), IBP_WEIGHT(2), IBP_WEIGHT(3),
            IBP_WEIGHT(4), IBP_WEIGHT(5), IBP_WEIGHT(6), IBP_WEIGHT(7)));
        weight_pairs_128[0] = _mm256_castsi256_si128(pairs);
        weight_pairs_128[1] = _mm256_extracti128_si256(pairs, 1);
      } else if (bw == 16) {
        weight_pairs[0] = highbd_ibp_weight_pairs_avx2(_mm256_setr_epi32(
            IBP_WEIGHT(0), IBP_WEIGHT(1), IBP_WEIGHT(2), IBP_WEIGHT(3),
            IBP_WEIGHT(8), IBP_WEIGHT(9), IBP_WEIGHT(10), IBP_WEIGHT(11)));
        weight_pairs[1] = highbd_ibp_weight_pairs_avx2(_mm256_setr_epi32(
            IBP_WEIGHT(4), IBP_WEIGHT(5), IBP_WEIGHT(6), IBP_WEIGHT(7),
            IBP_WEIGHT(12), IBP_WEIGHT(13), IBP_WEIGHT(14), IBP_WEIGHT(15)));
      } else {
        const __m256i pairs_lo = highbd_ibp_weight_pairs_avx2(_mm256_setr_epi32(
            IBP_WEIGHT(0), IBP_WEIGHT(1), IBP_WEIGHT(2), IBP_WEIGHT(3),
            IBP_WEIGHT(4), IBP_WEIGHT(5), IBP_WEIGHT(6), IBP_WEIGHT(7)));
        const __m256i pairs_hi = highbd_ibp_weight_pairs_avx2(_mm256_setr_epi32(
            IBP_WEIGHT(8), IBP_WEIGHT(9), IBP_WEIGHT(10), IBP_WEIGHT(11),
            IBP_WEIGHT(12), IBP_WEIGHT(13), IBP_WEIGHT(14), IBP_WEIGHT(15)));
        if (bw == 32) {
          // Column c uses weight c >> 1.
          weight_pairs[0] = _mm256_unpacklo_epi32(pairs_lo, pairs_lo);
          weight_pairs[1] = _mm256_unpackhi_epi32(pairs_lo, pairs_lo);
          weight_pairs[2] = _mm256_unpacklo_epi32(pairs_hi, pairs_hi);
          weight_pairs[3] = _mm256_unpackhi_epi32(pairs_hi, pairs_hi);
        } else {
          // Column c uses weight c >> 2.
          const __m256i idx_lo = _mm256_setr_epi32(0, 0, 0, 0, 2, 2, 2, 2);
          const __m256i idx_hi = _mm256_setr_epi32(1, 1, 1, 1, 3, 3, 3, 3);
          const __m256i four = _mm256_set1_epi32(4);
          weight_pairs[0] = _mm256_permutevar8x32_epi32(pairs_lo, idx_lo);
          weight_pairs[1] = _mm256_permutevar8x32_epi32(pairs_lo, idx_hi);
          weight_pairs[2] = _mm256_permutevar8x32_epi32(
              pairs_lo, _mm256_add_epi32(idx_lo, four));
          weight_pairs[3] = _mm256_permutevar8x32_epi32(
              pairs_lo, _mm256_add_epi32(idx_hi, four));
          weight_pairs[4] = _mm256_permutevar8x32_epi32(pairs_hi, idx_lo);
          weight_pairs[5] = _mm256_permutevar8x32_epi32(pairs_hi, idx_hi);
          weight_pairs[6] = _mm256_permutevar8x32_epi32(
              pairs_hi, _mm256_add_epi32(idx_lo, four));
          weight_pairs[7] = _mm256_permutevar8x32_epi32(
              pairs_hi, _mm256_add_epi32(idx_hi, four));
        }
      }
#undef IBP_WEIGHT
      weight_row += row_step;
    }
    if (bw >= 16) {
      for (int i = 0; i < bw / 16; ++i) {
        highbd_ibp_blend_16_avx2(dst + 16 * i, second_pred + 16 * i,
                                 weight_pairs[2 * i], weight_pairs[2 * i + 1]);
      }
    } else if (bw == 8) {
      const __m128i dst_vec = _mm_loadu_si128((const __m128i *)dst);
      const __m128i second_vec = _mm_loadu_si128((const __m128i *)second_pred);
      const __m128i lo = highbd_ibp_blend_4_avx2(
          _mm_unpacklo_epi16(dst_vec, second_vec), weight_pairs_128[0]);
      const __m128i hi = highbd_ibp_blend_4_avx2(
          _mm_unpackhi_epi16(dst_vec, second_vec), weight_pairs_128[1]);
      _mm_storeu_si128((__m128i *)dst, _mm_packus_epi32(lo, hi));
    } else {
      const __m128i dst_vec = _mm_loadl_epi64((const __m128i *)dst);
      const __m128i second_vec = _mm_loadl_epi64((const __m128i *)second_pred);
      const __m128i lo = highbd_ibp_blend_4_avx2(
          _mm_unpacklo_epi16(dst_vec, second_vec), weight_pairs_128[0]);
      _mm_storel_epi64((__m128i *)dst, _mm_packus_epi32(lo, lo));
    }
    dst += stride;
    second_pred += second_stride;
  }
}

#define HIGHBD_IBP_DR_PREDICTION_AVX2(is_z3)                                   \
  switch (bw) {                                                                \
    case 4:                                                                    \
      highbd_ibp_dr_prediction_avx2(weights, mode_idx, dst, stride,            \
                                    second_pred, second_stride, 4, bh, is_z3); \
      break;                                                                   \
    case 8:                                                                    \
      highbd_ibp_dr_prediction_avx2(weights, mode_idx, dst, stride,            \
                                    second_pred, second_stride, 8, bh, is_z3); \
      break;                                                                   \
    case 16:                                                                   \
      highbd_ibp_dr_prediction_avx2(weights, mode_idx, dst, stride,            \
                                    second_pred, second_stride, 16, bh,        \
                                    is_z3);                                    \
      break;                                                                   \
    case 32:                                                                   \
      highbd_ibp_dr_prediction_avx2(weights, mode_idx, dst, stride,            \
                                    second_pred, second_stride, 32, bh,        \
                                    is_z3);                                    \
      break;                                                                   \
    default:                                                                   \
      assert(bw == 64);                                                        \
      highbd_ibp_dr_prediction_avx2(weights, mode_idx, dst, stride,            \
                                    second_pred, second_stride, 64, bh,        \
                                    is_z3);                                    \
      break;                                                                   \
  }

void av2_highbd_ibp_dr_prediction_z1_avx2(
    const IbpWeightsType weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90],
    int mode_idx, uint16_t *dst, ptrdiff_t stride, uint16_t *second_pred,
    ptrdiff_t second_stride, int bw, int bh) {
  HIGHBD_IBP_DR_PREDICTION_AVX2(/*is_z3=*/0)
}

void av2_highbd_ibp_dr_prediction_z3_avx2(
    const IbpWeightsType weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90],
    int mode_idx, uint16_t *dst, ptrdiff_t stride, uint16_t *second_pred,
    ptrdiff_t second_stride, int bw, int bh) {
  HIGHBD_IBP_DR_PREDICTION_AVX2(/*is_z3=*/1)
}

#undef HIGHBD_IBP_DR_PREDICTION_AVX2
