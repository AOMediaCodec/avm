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

#include "config/aom_dsp_rtcd.h"
#include "aom_dsp/x86/intrapred_x86.h"
#include "aom_dsp/x86/lpf_common_sse2.h"
#include "aom_dsp/x86/synonyms.h"
#if CONFIG_IDIF
#include "av1/common/reconintra.h"
#endif  // CONFIG_IDIF

static DECLARE_ALIGNED(16, uint8_t, HighbdLoadMaskx[8][16]) = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 0, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 },
  { 0, 1, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 4, 5 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 },
};

static DECLARE_ALIGNED(16, uint8_t, HighbdEvenOddMaskx4[4][16]) = {
  { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 },
  { 0, 1, 2, 3, 6, 7, 10, 11, 14, 15, 4, 5, 8, 9, 12, 13 },
  { 0, 1, 0, 1, 4, 5, 8, 9, 12, 13, 0, 1, 6, 7, 10, 11 },
  { 0, 1, 0, 1, 0, 1, 6, 7, 10, 11, 14, 15, 0, 1, 8, 9 }
};

static DECLARE_ALIGNED(16, uint8_t, HighbdEvenOddMaskx[8][32]) = {
  { 0, 1, 4, 5, 8,  9,  12, 13, 16, 17, 20, 21, 24, 25, 28, 29,
    2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31 },
  { 0, 1, 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27,
    0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29 },
  { 0, 1, 0, 1, 4, 5, 8,  9,  12, 13, 16, 17, 20, 21, 24, 25,
    0, 1, 0, 1, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27 },
  { 0, 1, 0, 1, 0, 1, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23,
    0, 1, 0, 1, 0, 1, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 8,  9,  12, 13, 16, 17, 20, 21,
    0, 1, 0, 1, 0, 1, 0, 1, 10, 11, 14, 15, 18, 19, 22, 23 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 10, 11, 14, 15, 18, 19,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 12, 13, 16, 17, 20, 21 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 12, 13, 16, 17,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 14, 15, 18, 19 },
  { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 14, 15,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 16, 17 }
};

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

static INLINE void highbd_transpose4x16_avx2(__m256i *x0, __m256i *x1,
                                             __m256i *x2, __m256i *x3,
                                             __m256i *d0, __m256i *d1,
                                             __m256i *d2, __m256i *d3) {
  __m256i w0, w1, w2, w3, ww0, ww1;

  w0 = _mm256_unpacklo_epi16(*x0, *x1);  // 00 10 01 11 02 12 03 13
  w1 = _mm256_unpacklo_epi16(*x2, *x3);  // 20 30 21 31 22 32 23 33
  w2 = _mm256_unpackhi_epi16(*x0, *x1);  // 40 50 41 51 42 52 43 53
  w3 = _mm256_unpackhi_epi16(*x2, *x3);  // 60 70 61 71 62 72 63 73

  ww0 = _mm256_unpacklo_epi32(w0, w1);  // 00 10 20 30 01 11 21 31
  ww1 = _mm256_unpacklo_epi32(w2, w3);  // 40 50 60 70 41 51 61 71

  *d0 = _mm256_unpacklo_epi64(ww0, ww1);  // 00 10 20 30 40 50 60 70
  *d1 = _mm256_unpackhi_epi64(ww0, ww1);  // 01 11 21 31 41 51 61 71

  ww0 = _mm256_unpackhi_epi32(w0, w1);  // 02 12 22 32 03 13 23 33
  ww1 = _mm256_unpackhi_epi32(w2, w3);  // 42 52 62 72 43 53 63 73

  *d2 = _mm256_unpacklo_epi64(ww0, ww1);  // 02 12 22 32 42 52 62 72
  *d3 = _mm256_unpackhi_epi64(ww0, ww1);  // 03 13 23 33 43 53 63 73
}

static INLINE void highbd_transpose8x16_16x8_avx2(
    __m256i *x0, __m256i *x1, __m256i *x2, __m256i *x3, __m256i *x4,
    __m256i *x5, __m256i *x6, __m256i *x7, __m256i *d0, __m256i *d1,
    __m256i *d2, __m256i *d3, __m256i *d4, __m256i *d5, __m256i *d6,
    __m256i *d7) {
  __m256i w0, w1, w2, w3, ww0, ww1;

  w0 = _mm256_unpacklo_epi16(*x0, *x1);  // 00 10 01 11 02 12 03 13
  w1 = _mm256_unpacklo_epi16(*x2, *x3);  // 20 30 21 31 22 32 23 33
  w2 = _mm256_unpacklo_epi16(*x4, *x5);  // 40 50 41 51 42 52 43 53
  w3 = _mm256_unpacklo_epi16(*x6, *x7);  // 60 70 61 71 62 72 63 73

  ww0 = _mm256_unpacklo_epi32(w0, w1);  // 00 10 20 30 01 11 21 31
  ww1 = _mm256_unpacklo_epi32(w2, w3);  // 40 50 60 70 41 51 61 71

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

static INLINE void highbd_transpose16x16_avx2(__m256i *x, __m256i *d) {
  __m256i w0, w1, w2, w3, ww0, ww1;
  __m256i dd[16];
  w0 = _mm256_unpacklo_epi16(x[0], x[1]);
  w1 = _mm256_unpacklo_epi16(x[2], x[3]);
  w2 = _mm256_unpacklo_epi16(x[4], x[5]);
  w3 = _mm256_unpacklo_epi16(x[6], x[7]);

  ww0 = _mm256_unpacklo_epi32(w0, w1);  //
  ww1 = _mm256_unpacklo_epi32(w2, w3);  //

  dd[0] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[1] = _mm256_unpackhi_epi64(ww0, ww1);

  ww0 = _mm256_unpackhi_epi32(w0, w1);  //
  ww1 = _mm256_unpackhi_epi32(w2, w3);  //

  dd[2] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[3] = _mm256_unpackhi_epi64(ww0, ww1);

  w0 = _mm256_unpackhi_epi16(x[0], x[1]);
  w1 = _mm256_unpackhi_epi16(x[2], x[3]);
  w2 = _mm256_unpackhi_epi16(x[4], x[5]);
  w3 = _mm256_unpackhi_epi16(x[6], x[7]);

  ww0 = _mm256_unpacklo_epi32(w0, w1);  //
  ww1 = _mm256_unpacklo_epi32(w2, w3);  //

  dd[4] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[5] = _mm256_unpackhi_epi64(ww0, ww1);

  ww0 = _mm256_unpackhi_epi32(w0, w1);  //
  ww1 = _mm256_unpackhi_epi32(w2, w3);  //

  dd[6] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[7] = _mm256_unpackhi_epi64(ww0, ww1);

  w0 = _mm256_unpacklo_epi16(x[8], x[9]);
  w1 = _mm256_unpacklo_epi16(x[10], x[11]);
  w2 = _mm256_unpacklo_epi16(x[12], x[13]);
  w3 = _mm256_unpacklo_epi16(x[14], x[15]);

  ww0 = _mm256_unpacklo_epi32(w0, w1);
  ww1 = _mm256_unpacklo_epi32(w2, w3);

  dd[8] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[9] = _mm256_unpackhi_epi64(ww0, ww1);

  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);

  dd[10] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[11] = _mm256_unpackhi_epi64(ww0, ww1);

  w0 = _mm256_unpackhi_epi16(x[8], x[9]);
  w1 = _mm256_unpackhi_epi16(x[10], x[11]);
  w2 = _mm256_unpackhi_epi16(x[12], x[13]);
  w3 = _mm256_unpackhi_epi16(x[14], x[15]);

  ww0 = _mm256_unpacklo_epi32(w0, w1);
  ww1 = _mm256_unpacklo_epi32(w2, w3);

  dd[12] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[13] = _mm256_unpackhi_epi64(ww0, ww1);

  ww0 = _mm256_unpackhi_epi32(w0, w1);
  ww1 = _mm256_unpackhi_epi32(w2, w3);

  dd[14] = _mm256_unpacklo_epi64(ww0, ww1);
  dd[15] = _mm256_unpackhi_epi64(ww0, ww1);

  for (int i = 0; i < 8; i++) {
    d[i] = _mm256_insertf128_si256(dd[i], _mm256_castsi256_si128(dd[i + 8]), 1);
    d[i + 8] = _mm256_insertf128_si256(dd[i + 8],
                                       _mm256_extracti128_si256(dd[i], 1), 0);
  }
}

static AOM_FORCE_INLINE void highbd_transpose16x4_8x8_avx2(__m128i *dstvec,
                                                           __m256i *d) {
  // r0 = 00 10 01 11 02 12 03 13
  const __m128i r0 = _mm_unpacklo_epi16(dstvec[0], dstvec[1]);
  // r1 = 20 30 21 31 22 32 23 33
  const __m128i r1 = _mm_unpacklo_epi16(dstvec[2], dstvec[3]);
  // r2 = 40 50 41 51 42 52 43 53
  const __m128i r2 = _mm_unpacklo_epi16(dstvec[4], dstvec[5]);
  // r3 = 60 70 61 71 62 72 63 73
  const __m128i r3 = _mm_unpacklo_epi16(dstvec[6], dstvec[7]);
  // r4 = 80 90 81 91 82 92 83 93
  const __m128i r4 = _mm_unpacklo_epi16(dstvec[8], dstvec[9]);
  // r5 = 100 110 101 111 102 112 103 113
  const __m128i r5 = _mm_unpacklo_epi16(dstvec[10], dstvec[11]);
  // r6 = 120 130 121 131 122 132 123 133
  const __m128i r6 = _mm_unpacklo_epi16(dstvec[12], dstvec[13]);
  // r7 = 140 150 141 151 142 152 143 153
  const __m128i r7 = _mm_unpacklo_epi16(dstvec[14], dstvec[15]);

  // 00 10 01 11 02 12 03 13 | 80 90 81 91 82 92 83 93
  const __m256i dstvec256_0 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(r0), r4, 0x1);
  // 20 30 21 31 22 32 23 33 | 100 110 101 111 102 112 103 113
  const __m256i dstvec256_1 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(r1), r5, 0x1);
  // 40 50 41 51 42 52 43 53 | 120 130 121 131 122 132 123 133
  const __m256i dstvec256_2 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(r2), r6, 0x1);
  // 60 70 61 71 62 72 63 73 | 140 150 141 151 142 152 143 153
  const __m256i dstvec256_3 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(r3), r7, 0x1);

  // 00 10 20 30 01 11 21 31 | 80 90 100 110 81 91 101 111
  const __m256i r8 = _mm256_unpacklo_epi32(dstvec256_0, dstvec256_1);
  // 02 12 22 32 03 13 23 33 | 82 92 102 112 83 93 103 113
  const __m256i r9 = _mm256_unpackhi_epi32(dstvec256_0, dstvec256_1);
  // 40 50 60 70 41 51 61 71 | 120 130 140 150 121 131 141 151
  const __m256i r10 = _mm256_unpacklo_epi32(dstvec256_2, dstvec256_3);
  // 42 52 62 72 43 53 63 73 | 122 132 142 152 123 133 143 153
  const __m256i r11 = _mm256_unpackhi_epi32(dstvec256_2, dstvec256_3);

  // 00 10 20 30 40 50 60 70 | 80 90 100 110 120 130 140 150
  d[0] = _mm256_unpacklo_epi64(r8, r10);
  // 01 11 21 31 41 51 61 71 | 81 91 101 111 121 131 141 151
  d[1] = _mm256_unpackhi_epi64(r8, r10);
  // 02 12 22 32 42 52 62 72 | 82 92 102 112 122 132 142 152
  d[2] = _mm256_unpacklo_epi64(r9, r11);
  // 03 13 23 33 43 53 63 73 | 83 93 103 113 123 133 143 153
  d[3] = _mm256_unpackhi_epi64(r9, r11);
}

#define PERM4x64(c0, c1, c2, c3) c0 + (c1 << 2) + (c2 << 4) + (c3 << 6)
#define PERM2x128(c0, c1) c0 + (c1 << 4)

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_4xN_internal_avx2(
    int N, __m128i *dst, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  const int frac_bits = 6 - upsample_above;
  const int max_base_x = ((N + 4) - 1 + (mrl_index << 1)) << upsample_above;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16;
  __m256i diff, c3f;
  __m128i a_mbase_x, max_base_x128, base_inc128, mask128;
  __m128i a0_128, a1_128;
  a16 = _mm256_set1_epi16(16);
  a_mbase_x = _mm_set1_epi16(above[max_base_x]);
  max_base_x128 = _mm_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = a_mbase_x;  // save 4 values
      }
      return;
    }

    a0_128 = _mm_loadu_si128((__m128i *)(above + base));
    a1_128 = _mm_loadu_si128((__m128i *)(above + base + 1));

    if (upsample_above) {
      a0_128 = _mm_shuffle_epi8(a0_128, *(__m128i *)HighbdEvenOddMaskx4[0]);
      a1_128 = _mm_srli_si128(a0_128, 8);

      base_inc128 = _mm_setr_epi16(base, base + 2, base + 4, base + 6, base + 8,
                                   base + 10, base + 12, base + 14);
      shift = _mm256_srli_epi16(
          _mm256_and_si256(
              _mm256_slli_epi16(_mm256_set1_epi16(x), upsample_above),
              _mm256_set1_epi16(0x3f)),
          1);
    } else {
      base_inc128 = _mm_setr_epi16(base, base + 1, base + 2, base + 3, base + 4,
                                   base + 5, base + 6, base + 7);
      shift = _mm256_srli_epi16(_mm256_and_si256(_mm256_set1_epi16(x), c3f), 1);
    }
    a0 = _mm256_castsi128_si256(a0_128);
    a1 = _mm256_castsi128_si256(a1_128);
    diff = _mm256_sub_epi16(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi16(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi16(a32, a16);  // a[x] * 32 + 16

    b = _mm256_mullo_epi16(diff, shift);
    res = _mm256_add_epi16(a32, b);
    res = _mm256_srli_epi16(res, 5);
    res1 = _mm256_castsi256_si128(res);

    mask128 = _mm_cmpgt_epi16(max_base_x128, base_inc128);
    dst[r] = _mm_blendv_epi8(a_mbase_x, res1, mask128);
    x += dx;
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
    int N, __m128i *dst, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  const int frac_bits = 6 - upsample_above;
  const int max_base_x = ((N + 4) - 1 + (mrl_index << 1)) << upsample_above;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16;
  __m256i diff;
  __m128i a_mbase_x, max_base_x128, base_inc128, mask128;

  a16 = _mm256_set1_epi32(16);
  a_mbase_x = _mm_set1_epi16(above[max_base_x]);
  max_base_x128 = _mm_set1_epi32(max_base_x);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = a_mbase_x;  // save 4 values
      }
      return;
    }

    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));

    if (upsample_above) {
      a0 = _mm256_permutevar8x32_epi32(
          a0, _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0));
      a1 = _mm256_castsi128_si256(_mm256_extracti128_si256(a0, 1));
      base_inc128 = _mm_setr_epi32(base, base + 2, base + 4, base + 6);
      shift = _mm256_srli_epi32(
          _mm256_and_si256(
              _mm256_slli_epi32(_mm256_set1_epi32(x), upsample_above),
              _mm256_set1_epi32(0x3f)),
          1);
    } else {
      base_inc128 = _mm_setr_epi32(base, base + 1, base + 2, base + 3);
      shift = _mm256_srli_epi32(
          _mm256_and_si256(_mm256_set1_epi32(x), _mm256_set1_epi32(0x3f)), 1);
    }

    diff = _mm256_sub_epi32(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi32(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi32(a32, a16);  // a[x] * 32 + 16

    b = _mm256_mullo_epi32(diff, shift);
    res = _mm256_add_epi32(a32, b);
    res = _mm256_srli_epi32(res, 5);

    res1 = _mm256_castsi256_si128(res);
    res1 = _mm_packus_epi32(res1, res1);

    mask128 = _mm_cmpgt_epi32(max_base_x128, base_inc128);
    mask128 = _mm_packs_epi32(mask128, mask128);  // goto 16 bit
    dst[r] = _mm_blendv_epi8(a_mbase_x, res1, mask128);
    x += dx;
  }
}

static void highbd_dr_prediction_z1_4xN_avx2(int N, uint16_t *dst,
                                             ptrdiff_t stride,
                                             const uint16_t *above,
                                             int upsample_above, int dx, int bd,
                                             int mrl_index) {
#if CONFIG_FLEX_PARTITION
  __m128i dstvec[64];
#else
  __m128i dstvec[16];
#endif  // CONFIG_FLEX_PARTITION
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(N, dstvec, above, upsample_above,
                                              dx, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        N, dstvec, above, upsample_above, dx, mrl_index);
  }
  for (int i = 0; i < N; i++) {
    _mm_storel_epi64((__m128i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
    int N, __m128i *dst, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  const int frac_bits = 6 - upsample_above;
  const int max_base_x = ((8 + N) - 1 + (mrl_index << 1)) << upsample_above;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a0_1, a1_1, a32, a16;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi32(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi32(max_base_x);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, res1, shift;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = _mm256_castsi256_si128(a_mbase_x);  // save 8 values
      }
      return;
    }

    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));

    if (upsample_above) {
      a0 = _mm256_permutevar8x32_epi32(
          a0, _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0));
      a1 = _mm256_castsi128_si256(_mm256_extracti128_si256(a0, 1));

      a0_1 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 8)));
      a0_1 = _mm256_permutevar8x32_epi32(
          a0_1, _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0));
      a1_1 = _mm256_castsi128_si256(_mm256_extracti128_si256(a0_1, 1));

      a0 = _mm256_inserti128_si256(a0, _mm256_castsi256_si128(a0_1), 1);
      a1 = _mm256_inserti128_si256(a1, _mm256_castsi256_si128(a1_1), 1);
      base_inc256 =
          _mm256_setr_epi32(base, base + 2, base + 4, base + 6, base + 8,
                            base + 10, base + 12, base + 14);
      shift = _mm256_srli_epi32(
          _mm256_and_si256(
              _mm256_slli_epi32(_mm256_set1_epi32(x), upsample_above),
              _mm256_set1_epi32(0x3f)),
          1);
    } else {
      base_inc256 = _mm256_setr_epi32(base, base + 1, base + 2, base + 3,
                                      base + 4, base + 5, base + 6, base + 7);
      shift = _mm256_srli_epi32(
          _mm256_and_si256(_mm256_set1_epi32(x), _mm256_set1_epi32(0x3f)), 1);
    }

    diff = _mm256_sub_epi32(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi32(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi32(a32, a16);  // a[x] * 32 + 16

    b = _mm256_mullo_epi32(diff, shift);
    res = _mm256_add_epi32(a32, b);
    res = _mm256_srli_epi32(res, 5);

    res1 = _mm256_packus_epi32(
        res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1)));

    mask256 = _mm256_cmpgt_epi32(max_base_x256, base_inc256);
    mask256 = _mm256_packs_epi32(
        mask256, _mm256_castsi128_si256(
                     _mm256_extracti128_si256(mask256, 1)));  // goto 16 bit
    res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
    dst[r] = _mm256_castsi256_si128(res1);
    x += dx;
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_8xN_internal_avx2(
    int N, __m128i *dst, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  const int frac_bits = 6 - upsample_above;
  const int max_base_x = ((8 + N) - 1 + (mrl_index << 1)) << upsample_above;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16, c3f;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;
  __m128i a0_x128, a1_x128;

  a16 = _mm256_set1_epi16(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, res1, shift;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = _mm256_castsi256_si128(a_mbase_x);  // save 8 values
      }
      return;
    }

    a0_x128 = _mm_loadu_si128((__m128i *)(above + base));
    if (upsample_above) {
      __m128i mask, atmp0, atmp1, atmp2, atmp3;
      a1_x128 = _mm_loadu_si128((__m128i *)(above + base + 8));
      atmp0 = _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdEvenOddMaskx[0]);
      atmp1 = _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdEvenOddMaskx[0]);
      atmp2 =
          _mm_shuffle_epi8(a0_x128, *(__m128i *)(HighbdEvenOddMaskx[0] + 16));
      atmp3 =
          _mm_shuffle_epi8(a1_x128, *(__m128i *)(HighbdEvenOddMaskx[0] + 16));
      mask =
          _mm_cmpgt_epi8(*(__m128i *)HighbdEvenOddMaskx[0], _mm_set1_epi8(15));
      a0_x128 = _mm_blendv_epi8(atmp0, atmp1, mask);
      mask = _mm_cmpgt_epi8(*(__m128i *)(HighbdEvenOddMaskx[0] + 16),
                            _mm_set1_epi8(15));
      a1_x128 = _mm_blendv_epi8(atmp2, atmp3, mask);

      base_inc256 = _mm256_setr_epi16(base, base + 2, base + 4, base + 6,
                                      base + 8, base + 10, base + 12, base + 14,
                                      0, 0, 0, 0, 0, 0, 0, 0);
      shift = _mm256_srli_epi16(
          _mm256_and_si256(
              _mm256_slli_epi16(_mm256_set1_epi16(x), upsample_above), c3f),
          1);
    } else {
      a1_x128 = _mm_loadu_si128((__m128i *)(above + base + 1));
      base_inc256 = _mm256_setr_epi16(base, base + 1, base + 2, base + 3,
                                      base + 4, base + 5, base + 6, base + 7, 0,
                                      0, 0, 0, 0, 0, 0, 0);
      shift = _mm256_srli_epi16(_mm256_and_si256(_mm256_set1_epi16(x), c3f), 1);
    }
    a0 = _mm256_castsi128_si256(a0_x128);
    a1 = _mm256_castsi128_si256(a1_x128);

    diff = _mm256_sub_epi16(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi16(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi16(a32, a16);  // a[x] * 32 + 16

    b = _mm256_mullo_epi16(diff, shift);
    res = _mm256_add_epi16(a32, b);
    res = _mm256_srli_epi16(res, 5);

    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    res1 = _mm256_blendv_epi8(a_mbase_x, res, mask256);
    dst[r] = _mm256_castsi256_si128(res1);
    x += dx;
  }
}

static void highbd_dr_prediction_z1_8xN_avx2(int N, uint16_t *dst,
                                             ptrdiff_t stride,
                                             const uint16_t *above,
                                             int upsample_above, int dx, int bd,
                                             int mrl_index) {
#if CONFIG_FLEX_PARTITION
  __m128i dstvec[64];
#else
  __m128i dstvec[32];
#endif  // CONFIG_FLEX_PARTITION
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(N, dstvec, above, upsample_above,
                                              dx, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        N, dstvec, above, upsample_above, dx, mrl_index);
  }
  for (int i = 0; i < N; i++) {
    _mm_storeu_si128((__m128i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((16 + N) - 1 + (mrl_index << 1));
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a0_1, a1, a1_1, a32, a16;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi32(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 16 values
      }
      return;
    }
    __m256i shift = _mm256_srli_epi32(
        _mm256_and_si256(_mm256_set1_epi32(x), _mm256_set1_epi32(0x3f)), 1);

    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));

    diff = _mm256_sub_epi32(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi32(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi32(a32, a16);  // a[x] * 32 + 16
    b = _mm256_mullo_epi32(diff, shift);

    res[0] = _mm256_add_epi32(a32, b);
    res[0] = _mm256_srli_epi32(res[0], 5);
    res[0] = _mm256_packus_epi32(
        res[0], _mm256_castsi128_si256(_mm256_extracti128_si256(res[0], 1)));

    int mdif = max_base_x - base;
    if (mdif > 8) {
      a0_1 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 8)));
      a1_1 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 9)));

      diff = _mm256_sub_epi32(a1_1, a0_1);  // a[x+1] - a[x]
      a32 = _mm256_slli_epi32(a0_1, 5);     // a[x] * 32
      a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16
      b = _mm256_mullo_epi32(diff, shift);

      res[1] = _mm256_add_epi32(a32, b);
      res[1] = _mm256_srli_epi32(res[1], 5);
      res[1] = _mm256_packus_epi32(
          res[1], _mm256_castsi128_si256(_mm256_extracti128_si256(res[1], 1)));
    } else {
      res[1] = a_mbase_x;
    }
    res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                   1);  // 16 16bit values

    base_inc256 = _mm256_setr_epi16(base, base + 1, base + 2, base + 3,
                                    base + 4, base + 5, base + 6, base + 7,
                                    base + 8, base + 9, base + 10, base + 11,
                                    base + 12, base + 13, base + 14, base + 15);
    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    dstvec[r] = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
    x += dx;
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_16xN_internal_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((16 + N) - 1 + (mrl_index << 1));

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16, c3f;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi16(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 16 values
      }
      return;
    }
    __m256i shift =
        _mm256_srli_epi16(_mm256_and_si256(_mm256_set1_epi16(x), c3f), 1);

    a0 = _mm256_loadu_si256((__m256i *)(above + base));
    a1 = _mm256_loadu_si256((__m256i *)(above + base + 1));

    diff = _mm256_sub_epi16(a1, a0);   // a[x+1] - a[x]
    a32 = _mm256_slli_epi16(a0, 5);    // a[x] * 32
    a32 = _mm256_add_epi16(a32, a16);  // a[x] * 32 + 16
    b = _mm256_mullo_epi16(diff, shift);

    res = _mm256_add_epi16(a32, b);
    res = _mm256_srli_epi16(res, 5);  // 16 16bit values

    base_inc256 = _mm256_setr_epi16(base, base + 1, base + 2, base + 3,
                                    base + 4, base + 5, base + 6, base + 7,
                                    base + 8, base + 9, base + 10, base + 11,
                                    base + 12, base + 13, base + 14, base + 15);
    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    dstvec[r] = _mm256_blendv_epi8(a_mbase_x, res, mask256);
    x += dx;
  }
}

static void highbd_dr_prediction_z1_16xN_avx2(int N, uint16_t *dst,
                                              ptrdiff_t stride,
                                              const uint16_t *above,
                                              int upsample_above, int dx,
                                              int bd, int mrl_index) {
  __m256i dstvec[64];
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(N, dstvec, above, upsample_above,
                                               dx, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        N, dstvec, above, upsample_above, dx, mrl_index);
  }
  for (int i = 0; i < N; i++) {
    _mm256_storeu_si256((__m256i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((32 + N) - 1 + (mrl_index << 1));
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a0_1, a1, a1_1, a32, a16, c3f;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi32(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 32 values
        dstvec[i + N] = a_mbase_x;
      }
      return;
    }

    __m256i shift =
        _mm256_srli_epi32(_mm256_and_si256(_mm256_set1_epi32(x), c3f), 1);

    for (int j = 0; j < 32; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        res1 = a_mbase_x;
      } else {
        a0 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + j)));
        a1 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + 1 + j)));

        diff = _mm256_sub_epi32(a1, a0);   // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0, 5);    // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);  // a[x] * 32 + 16
        b = _mm256_mullo_epi32(diff, shift);

        res[0] = _mm256_add_epi32(a32, b);
        res[0] = _mm256_srli_epi32(res[0], 5);
        res[0] = _mm256_packus_epi32(
            res[0],
            _mm256_castsi128_si256(_mm256_extracti128_si256(res[0], 1)));
        if (mdif > 8) {
          a0_1 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 8 + j)));
          a1_1 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 9 + j)));

          diff = _mm256_sub_epi32(a1_1, a0_1);  // a[x+1] - a[x]
          a32 = _mm256_slli_epi32(a0_1, 5);     // a[x] * 32
          a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16
          b = _mm256_mullo_epi32(diff, shift);

          res[1] = _mm256_add_epi32(a32, b);
          res[1] = _mm256_srli_epi32(res[1], 5);
          res[1] = _mm256_packus_epi32(
              res[1],
              _mm256_castsi128_si256(_mm256_extracti128_si256(res[1], 1)));
        } else {
          res[1] = a_mbase_x;
        }
        res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                       1);  // 16 16bit values
        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
      }
      if (!j) {
        dstvec[r] = res1;
      } else {
        dstvec[r + N] = res1;
      }
    }
    x += dx;
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_32xN_internal_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int upsample_above, int dx,
    int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((32 + N) - 1 + (mrl_index << 1));
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16, c3f;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi16(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 32 values
        dstvec[i + N] = a_mbase_x;
      }
      return;
    }

    __m256i shift =
        _mm256_srli_epi16(_mm256_and_si256(_mm256_set1_epi16(x), c3f), 1);

    for (int j = 0; j < 32; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        res = a_mbase_x;
      } else {
        a0 = _mm256_loadu_si256((__m256i *)(above + base + j));
        a1 = _mm256_loadu_si256((__m256i *)(above + base + 1 + j));

        diff = _mm256_sub_epi16(a1, a0);   // a[x+1] - a[x]
        a32 = _mm256_slli_epi16(a0, 5);    // a[x] * 32
        a32 = _mm256_add_epi16(a32, a16);  // a[x] * 32 + 16
        b = _mm256_mullo_epi16(diff, shift);

        res = _mm256_add_epi16(a32, b);
        res = _mm256_srli_epi16(res, 5);

        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res = _mm256_blendv_epi8(a_mbase_x, res, mask256);
      }
      if (!j) {
        dstvec[r] = res;
      } else {
        dstvec[r + N] = res;
      }
    }
    x += dx;
  }
}

static void highbd_dr_prediction_z1_32xN_avx2(int N, uint16_t *dst,
                                              ptrdiff_t stride,
                                              const uint16_t *above,
                                              int upsample_above, int dx,
                                              int bd, int mrl_index) {
  __m256i dstvec[128];
  if (bd < 12) {
    highbd_dr_prediction_z1_32xN_internal_avx2(N, dstvec, above, upsample_above,
                                               dx, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
        N, dstvec, above, upsample_above, dx, mrl_index);
  }
  for (int i = 0; i < N; i++) {
    _mm256_storeu_si256((__m256i *)(dst + stride * i), dstvec[i]);
    _mm256_storeu_si256((__m256i *)(dst + stride * i + 16), dstvec[i + N]);
  }
}

static void highbd_dr_prediction_32bit_z1_64xN_avx2(int N, uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *above,
                                                    int upsample_above, int dx,
                                                    int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((64 + N) - 1 + (mrl_index << 1));

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a0_1, a1, a1_1, a32, a16;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi32(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++, dst += stride) {
    __m256i b, res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        _mm256_storeu_si256((__m256i *)dst, a_mbase_x);  // save 32 values
        _mm256_storeu_si256((__m256i *)(dst + 16), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 32), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 48), a_mbase_x);
        dst += stride;
      }
      return;
    }

    __m256i shift = _mm256_srli_epi32(
        _mm256_and_si256(_mm256_set1_epi32(x), _mm256_set1_epi32(0x3f)), 1);

    __m128i a0_128, a0_1_128, a1_128, a1_1_128;
    for (int j = 0; j < 64; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        _mm256_storeu_si256((__m256i *)(dst + j), a_mbase_x);
      } else {
        a0_128 = _mm_loadu_si128((__m128i *)(above + base + j));
        a1_128 = _mm_loadu_si128((__m128i *)(above + base + 1 + j));
        a0 = _mm256_cvtepu16_epi32(a0_128);
        a1 = _mm256_cvtepu16_epi32(a1_128);

        diff = _mm256_sub_epi32(a1, a0);   // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0, 5);    // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);  // a[x] * 32 + 16
        b = _mm256_mullo_epi32(diff, shift);

        res[0] = _mm256_add_epi32(a32, b);
        res[0] = _mm256_srli_epi32(res[0], 5);
        res[0] = _mm256_packus_epi32(
            res[0],
            _mm256_castsi128_si256(_mm256_extracti128_si256(res[0], 1)));
        if (mdif > 8) {
          a0_1_128 = _mm_loadu_si128((__m128i *)(above + base + 8 + j));
          a1_1_128 = _mm_loadu_si128((__m128i *)(above + base + 9 + j));
          a0_1 = _mm256_cvtepu16_epi32(a0_1_128);
          a1_1 = _mm256_cvtepu16_epi32(a1_1_128);

          diff = _mm256_sub_epi32(a1_1, a0_1);  // a[x+1] - a[x]
          a32 = _mm256_slli_epi32(a0_1, 5);     // a[x] * 32
          a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16
          b = _mm256_mullo_epi32(diff, shift);

          res[1] = _mm256_add_epi32(a32, b);
          res[1] = _mm256_srli_epi32(res[1], 5);
          res[1] = _mm256_packus_epi32(
              res[1],
              _mm256_castsi128_si256(_mm256_extracti128_si256(res[1], 1)));
        } else {
          res[1] = a_mbase_x;
        }
        res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                       1);  // 16 16bit values
        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
        _mm256_storeu_si256((__m256i *)(dst + j), res1);
      }
    }
    x += dx;
  }
}

static void highbd_dr_prediction_z1_64xN_avx2(int N, uint16_t *dst,
                                              ptrdiff_t stride,
                                              const uint16_t *above,
                                              int upsample_above, int dx,
                                              int mrl_index) {
  // here upsample_above is 0 by design of av1_use_intra_edge_upsample
  (void)upsample_above;
  const int frac_bits = 6;
  const int max_base_x = ((64 + N) - 1 + (mrl_index << 1));
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0, a1, a32, a16, c3f;
  __m256i a_mbase_x, diff, max_base_x256, base_inc256, mask256;

  a16 = _mm256_set1_epi16(16);
  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);
  c3f = _mm256_set1_epi16(0x3f);

  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++, dst += stride) {
    __m256i b, res;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        _mm256_storeu_si256((__m256i *)dst, a_mbase_x);  // save 32 values
        _mm256_storeu_si256((__m256i *)(dst + 16), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 32), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 48), a_mbase_x);
        dst += stride;
      }
      return;
    }

    __m256i shift =
        _mm256_srli_epi16(_mm256_and_si256(_mm256_set1_epi16(x), c3f), 1);

    for (int j = 0; j < 64; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        _mm256_storeu_si256((__m256i *)(dst + j), a_mbase_x);
      } else {
        a0 = _mm256_loadu_si256((__m256i *)(above + base + j));
        a1 = _mm256_loadu_si256((__m256i *)(above + base + 1 + j));

        diff = _mm256_sub_epi16(a1, a0);   // a[x+1] - a[x]
        a32 = _mm256_slli_epi16(a0, 5);    // a[x] * 32
        a32 = _mm256_add_epi16(a32, a16);  // a[x] * 32 + 16
        b = _mm256_mullo_epi16(diff, shift);

        res = _mm256_add_epi16(a32, b);
        res = _mm256_srli_epi16(res, 5);

        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res = _mm256_blendv_epi8(a_mbase_x, res, mask256);
        _mm256_storeu_si256((__m256i *)(dst + j), res);  // 16 16bit values
      }
    }
    x += dx;
  }
}

// Directional prediction, zone 1: 0 < angle < 90
void av1_highbd_dr_prediction_z1_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int upsample_above,
                                      int dx, int dy, int bd, int mrl_index) {
  (void)left;
  (void)dy;

  switch (bw) {
    case 4:
      highbd_dr_prediction_z1_4xN_avx2(bh, dst, stride, above, upsample_above,
                                       dx, bd, mrl_index);
      break;
    case 8:
      highbd_dr_prediction_z1_8xN_avx2(bh, dst, stride, above, upsample_above,
                                       dx, bd, mrl_index);
      break;
    case 16:
      highbd_dr_prediction_z1_16xN_avx2(bh, dst, stride, above, upsample_above,
                                        dx, bd, mrl_index);
      break;
    case 32:
      highbd_dr_prediction_z1_32xN_avx2(bh, dst, stride, above, upsample_above,
                                        dx, bd, mrl_index);
      break;
    case 64:
      if (bd < 12) {
        highbd_dr_prediction_z1_64xN_avx2(bh, dst, stride, above,
                                          upsample_above, dx, mrl_index);
      } else {
        highbd_dr_prediction_32bit_z1_64xN_avx2(bh, dst, stride, above,
                                                upsample_above, dx, mrl_index);
      }
      break;
    default: break;
  }
  return;
}

static void highbd_transpose_TX_16X16(const uint16_t *src, ptrdiff_t pitchSrc,
                                      uint16_t *dst, ptrdiff_t pitchDst) {
  __m256i r[16];
  __m256i d[16];
  for (int j = 0; j < 16; j++) {
    r[j] = _mm256_loadu_si256((__m256i *)(src + j * pitchSrc));
  }
  highbd_transpose16x16_avx2(r, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + j * pitchDst), d[j]);
  }
}

static void highbd_transpose(const uint16_t *src, ptrdiff_t pitchSrc,
                             uint16_t *dst, ptrdiff_t pitchDst, int width,
                             int height) {
  for (int j = 0; j < height; j += 16)
    for (int i = 0; i < width; i += 16)
      highbd_transpose_TX_16X16(src + i * pitchSrc + j, pitchSrc,
                                dst + j * pitchDst + i, pitchDst);
}

static void highbd_dr_prediction_32bit_z2_Nx4_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  const int min_base_x = -((1 + mrl_index) << upsample_above);
  const int min_base_y = -((1 + mrl_index) << upsample_left);
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0_x, a1_x, a32, a16;
  __m256i diff;
  __m128i c3f, min_base_y128;

  a16 = _mm256_set1_epi32(16);
  c3f = _mm_set1_epi32(0x3f);
  min_base_y128 = _mm_set1_epi32(min_base_y);
  __m128i cmrlIdx = _mm_set1_epi32(mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i resx, resy, resxy;
    __m128i a0_x128, a1_x128;
    int y = r + 1;
    int base_x = (-(y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1) >> upsample_above;
    }
    int base_min_diff =
        (min_base_x - base_x + upsample_above) >> upsample_above;
    if (base_min_diff > 4) {
      base_min_diff = 4;
    } else {
      if (base_min_diff < 0) base_min_diff = 0;
    }

    if (base_shift > 3) {
      a0_x = _mm256_setzero_si256();
      a1_x = _mm256_setzero_si256();
      shift = _mm256_setzero_si256();
    } else {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
      if (upsample_above) {
        a0_x128 = _mm_shuffle_epi8(a0_x128,
                                   *(__m128i *)HighbdEvenOddMaskx4[base_shift]);
        a1_x128 = _mm_srli_si128(a0_x128, 8);

        shift = _mm256_castsi128_si256(_mm_srli_epi32(
            _mm_and_si128(
                _mm_slli_epi32(_mm_setr_epi32(-(y + mrl_index) * dx,
                                              (1 << 6) - (y + mrl_index) * dx,
                                              (2 << 6) - (y + mrl_index) * dx,
                                              (3 << 6) - (y + mrl_index) * dx),
                               upsample_above),
                c3f),
            1));
      } else {
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 = _mm_srli_si128(a0_x128, 2);

        shift = _mm256_castsi128_si256(_mm_srli_epi32(
            _mm_and_si128(_mm_setr_epi32(-(y + mrl_index) * dx,
                                         (1 << 6) - (y + mrl_index) * dx,
                                         (2 << 6) - (y + mrl_index) * dx,
                                         (3 << 6) - (y + mrl_index) * dx),
                          c3f),
            1));
      }
      a0_x = _mm256_cvtepu16_epi32(a0_x128);
      a1_x = _mm256_cvtepu16_epi32(a1_x128);
    }
    // y calc
    __m128i a0_y, a1_y, shifty;
    if (base_x < min_base_x) {
      __m128i r6, c1234, dy128, y_c128, base_y_c128, mask128;
      DECLARE_ALIGNED(32, int, base_y_c[4]);
      r6 = _mm_set1_epi32(r << 6);
      dy128 = _mm_set1_epi32(dy);
      c1234 = _mm_setr_epi32(1, 2, 3, 4);
      __m128i c1234_ = _mm_add_epi32(c1234, cmrlIdx);
      y_c128 = _mm_sub_epi32(r6, _mm_mullo_epi32(c1234_, dy128));
      base_y_c128 = _mm_srai_epi32(y_c128, frac_bits_y);
      mask128 = _mm_cmpgt_epi32(min_base_y128, base_y_c128);
      base_y_c128 = _mm_andnot_si128(mask128, base_y_c128);
      _mm_store_si128((__m128i *)base_y_c, base_y_c128);

      a0_y = _mm_setr_epi32(left[base_y_c[0]], left[base_y_c[1]],
                            left[base_y_c[2]], left[base_y_c[3]]);
      a1_y = _mm_setr_epi32(left[base_y_c[0] + 1], left[base_y_c[1] + 1],
                            left[base_y_c[2] + 1], left[base_y_c[3] + 1]);

      if (upsample_left) {
        shifty = _mm_srli_epi32(
            _mm_and_si128(_mm_slli_epi32(y_c128, upsample_left), c3f), 1);
      } else {
        shifty = _mm_srli_epi32(_mm_and_si128(y_c128, c3f), 1);
      }
      a0_x = _mm256_inserti128_si256(a0_x, a0_y, 1);
      a1_x = _mm256_inserti128_si256(a1_x, a1_y, 1);
      shift = _mm256_inserti128_si256(shift, shifty, 1);
    }

    diff = _mm256_sub_epi32(a1_x, a0_x);  // a[x+1] - a[x]
    a32 = _mm256_slli_epi32(a0_x, 5);     // a[x] * 32
    a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

    b = _mm256_mullo_epi32(diff, shift);
    res = _mm256_add_epi32(a32, b);
    res = _mm256_srli_epi32(res, 5);

    resx = _mm256_castsi256_si128(res);
    resx = _mm_packus_epi32(resx, resx);

    resy = _mm256_extracti128_si256(res, 1);
    resy = _mm_packus_epi32(resy, resy);

    resxy =
        _mm_blendv_epi8(resx, resy, *(__m128i *)HighbdBaseMask[base_min_diff]);
    _mm_storel_epi64((__m128i *)(dst), resxy);
    dst += stride;
  }
}

static void highbd_dr_prediction_z2_Nx4_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  const int min_base_x = -((1 + mrl_index) << upsample_above);
  const int min_base_y = -((1 + mrl_index) << upsample_left);
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  assert(dx > 0);
  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0_x, a1_x, a32, a16;
  __m256i diff;
  __m128i c3f, min_base_y128;

  a16 = _mm256_set1_epi16(16);
  c3f = _mm_set1_epi16(0x3f);
  min_base_y128 = _mm_set1_epi16(min_base_y);
  __m128i cmrlIdx = _mm_set1_epi16(mrl_index);

  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i resx, resy, resxy;
    __m128i a0_x128, a1_x128;
    int y = r + 1;
    int base_x = (-(y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1) >> upsample_above;
    }
    int base_min_diff =
        (min_base_x - base_x + upsample_above) >> upsample_above;
    if (base_min_diff > 4) {
      base_min_diff = 4;
    } else {
      if (base_min_diff < 0) base_min_diff = 0;
    }

    if (base_shift > 3) {
      a0_x = _mm256_setzero_si256();
      a1_x = _mm256_setzero_si256();
      shift = _mm256_setzero_si256();
    } else {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
      if (upsample_above) {
        a0_x128 = _mm_shuffle_epi8(a0_x128,
                                   *(__m128i *)HighbdEvenOddMaskx4[base_shift]);
        a1_x128 = _mm_srli_si128(a0_x128, 8);

        shift = _mm256_castsi128_si256(_mm_srli_epi16(
            _mm_and_si128(
                _mm_slli_epi16(
                    _mm_setr_epi16(-(y + mrl_index) * dx,
                                   (1 << 6) - (y + mrl_index) * dx,
                                   (2 << 6) - (y + mrl_index) * dx,
                                   (3 << 6) - (y + mrl_index) * dx, 0, 0, 0, 0),
                    upsample_above),
                c3f),
            1));
      } else {
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 = _mm_srli_si128(a0_x128, 2);

        shift = _mm256_castsi128_si256(_mm_srli_epi16(
            _mm_and_si128(
                _mm_setr_epi16(-(y + mrl_index) * dx,
                               (1 << 6) - (y + mrl_index) * dx,
                               (2 << 6) - (y + mrl_index) * dx,
                               (3 << 6) - (y + mrl_index) * dx, 0, 0, 0, 0),
                c3f),
            1));
      }
      a0_x = _mm256_castsi128_si256(a0_x128);
      a1_x = _mm256_castsi128_si256(a1_x128);
    }
    // y calc
    __m128i a0_y, a1_y, shifty;
    if (base_x < min_base_x) {
      __m128i r6, c1234, dy128, y_c128, base_y_c128, mask128;
      DECLARE_ALIGNED(32, int16_t, base_y_c[8]);
      r6 = _mm_set1_epi16(r << 6);
      dy128 = _mm_set1_epi16(dy);
      c1234 = _mm_setr_epi16(1, 2, 3, 4, 0, 0, 0, 0);
      __m128i c1234_ = _mm_add_epi16(c1234, cmrlIdx);
      y_c128 = _mm_sub_epi16(r6, _mm_mullo_epi16(c1234_, dy128));
      base_y_c128 = _mm_srai_epi16(y_c128, frac_bits_y);
      mask128 = _mm_cmpgt_epi16(min_base_y128, base_y_c128);
      base_y_c128 = _mm_andnot_si128(mask128, base_y_c128);
      _mm_store_si128((__m128i *)base_y_c, base_y_c128);

      a0_y = _mm_setr_epi16(left[base_y_c[0]], left[base_y_c[1]],
                            left[base_y_c[2]], left[base_y_c[3]], 0, 0, 0, 0);
      a1_y = _mm_setr_epi16(left[base_y_c[0] + 1], left[base_y_c[1] + 1],
                            left[base_y_c[2] + 1], left[base_y_c[3] + 1], 0, 0,
                            0, 0);

      if (upsample_left) {
        shifty = _mm_srli_epi16(
            _mm_and_si128(_mm_slli_epi16(y_c128, upsample_left), c3f), 1);
      } else {
        shifty = _mm_srli_epi16(_mm_and_si128(y_c128, c3f), 1);
      }
      a0_x = _mm256_inserti128_si256(a0_x, a0_y, 1);
      a1_x = _mm256_inserti128_si256(a1_x, a1_y, 1);
      shift = _mm256_inserti128_si256(shift, shifty, 1);
    }

    diff = _mm256_sub_epi16(a1_x, a0_x);  // a[x+1] - a[x]
    a32 = _mm256_slli_epi16(a0_x, 5);     // a[x] * 32
    a32 = _mm256_add_epi16(a32, a16);     // a[x] * 32 + 16

    b = _mm256_mullo_epi16(diff, shift);
    res = _mm256_add_epi16(a32, b);
    res = _mm256_srli_epi16(res, 5);

    resx = _mm256_castsi256_si128(res);
    resy = _mm256_extracti128_si256(res, 1);
    resxy =
        _mm_blendv_epi8(resx, resy, *(__m128i *)HighbdBaseMask[base_min_diff]);
    _mm_storel_epi64((__m128i *)(dst), resxy);
    dst += stride;
  }
}

static void highbd_dr_prediction_32bit_z2_Nx8_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  const int min_base_x = -((1 + mrl_index) << upsample_above);
  const int min_base_y = -((1 + mrl_index) << upsample_left);
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0_x, a1_x, a0_y, a1_y, a32, a16, c3f, min_base_y256;
  __m256i diff;
  __m128i a0_x128, a1_x128;

  a16 = _mm256_set1_epi32(16);
  c3f = _mm256_set1_epi32(0x3f);
  min_base_y256 = _mm256_set1_epi32(min_base_y);
  __m256i cmrlIdx = _mm256_set1_epi32(mrl_index);

  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i resx, resy, resxy;
    int y = r + 1;
    int base_x = (-(y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1) >> upsample_above;
    }
    int base_min_diff =
        (min_base_x - base_x + upsample_above) >> upsample_above;
    if (base_min_diff > 8) {
      base_min_diff = 8;
    } else {
      if (base_min_diff < 0) base_min_diff = 0;
    }

    if (base_shift > 7) {
      resx = _mm_setzero_si128();
    } else {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
      if (upsample_above) {
        __m128i mask, atmp0, atmp1, atmp2, atmp3;
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + 8 + base_shift));
        atmp0 = _mm_shuffle_epi8(a0_x128,
                                 *(__m128i *)HighbdEvenOddMaskx[base_shift]);
        atmp1 = _mm_shuffle_epi8(a1_x128,
                                 *(__m128i *)HighbdEvenOddMaskx[base_shift]);
        atmp2 = _mm_shuffle_epi8(
            a0_x128, *(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16));
        atmp3 = _mm_shuffle_epi8(
            a1_x128, *(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16));
        mask = _mm_cmpgt_epi8(*(__m128i *)HighbdEvenOddMaskx[base_shift],
                              _mm_set1_epi8(15));
        a0_x128 = _mm_blendv_epi8(atmp0, atmp1, mask);
        mask = _mm_cmpgt_epi8(*(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16),
                              _mm_set1_epi8(15));
        a1_x128 = _mm_blendv_epi8(atmp2, atmp3, mask);
        shift = _mm256_srli_epi32(
            _mm256_and_si256(
                _mm256_slli_epi32(
                    _mm256_setr_epi32(-(y + mrl_index) * dx,
                                      (1 << 6) - (y + mrl_index) * dx,
                                      (2 << 6) - (y + mrl_index) * dx,
                                      (3 << 6) - (y + mrl_index) * dx,
                                      (4 << 6) - (y + mrl_index) * dx,
                                      (5 << 6) - (y + mrl_index) * dx,
                                      (6 << 6) - (y + mrl_index) * dx,
                                      (7 << 6) - (y + mrl_index) * dx),
                    upsample_above),
                c3f),
            1);
      } else {
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + 1 + base_shift));
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 =
            _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

        shift = _mm256_srli_epi32(
            _mm256_and_si256(_mm256_setr_epi32(-(y + mrl_index) * dx,
                                               (1 << 6) - (y + mrl_index) * dx,
                                               (2 << 6) - (y + mrl_index) * dx,
                                               (3 << 6) - (y + mrl_index) * dx,
                                               (4 << 6) - (y + mrl_index) * dx,
                                               (5 << 6) - (y + mrl_index) * dx,
                                               (6 << 6) - (y + mrl_index) * dx,
                                               (7 << 6) - (y + mrl_index) * dx),
                             c3f),
            1);
      }
      a0_x = _mm256_cvtepu16_epi32(a0_x128);
      a1_x = _mm256_cvtepu16_epi32(a1_x128);

      diff = _mm256_sub_epi32(a1_x, a0_x);  // a[x+1] - a[x]
      a32 = _mm256_slli_epi32(a0_x, 5);     // a[x] * 32
      a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

      b = _mm256_mullo_epi32(diff, shift);
      res = _mm256_add_epi32(a32, b);
      res = _mm256_srli_epi32(res, 5);

      resx = _mm256_castsi256_si128(_mm256_packus_epi32(
          res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1))));
    }
    // y calc
    if (base_x < min_base_x) {
      DECLARE_ALIGNED(32, int, base_y_c[8]);
      __m256i r6, c256, dy256, y_c256, base_y_c256, mask256;
      r6 = _mm256_set1_epi32(r << 6);
      dy256 = _mm256_set1_epi32(dy);
      c256 = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8);
      __m256i c256_ = _mm256_add_epi32(c256, cmrlIdx);
      y_c256 = _mm256_sub_epi32(r6, _mm256_mullo_epi32(c256_, dy256));
      base_y_c256 = _mm256_srai_epi32(y_c256, frac_bits_y);
      mask256 = _mm256_cmpgt_epi32(min_base_y256, base_y_c256);
      base_y_c256 = _mm256_andnot_si256(mask256, base_y_c256);
      _mm256_store_si256((__m256i *)base_y_c, base_y_c256);

      a0_y = _mm256_cvtepu16_epi32(_mm_setr_epi16(
          left[base_y_c[0]], left[base_y_c[1]], left[base_y_c[2]],
          left[base_y_c[3]], left[base_y_c[4]], left[base_y_c[5]],
          left[base_y_c[6]], left[base_y_c[7]]));
      a1_y = _mm256_cvtepu16_epi32(_mm_setr_epi16(
          left[base_y_c[0] + 1], left[base_y_c[1] + 1], left[base_y_c[2] + 1],
          left[base_y_c[3] + 1], left[base_y_c[4] + 1], left[base_y_c[5] + 1],
          left[base_y_c[6] + 1], left[base_y_c[7] + 1]));

      if (upsample_left) {
        shift = _mm256_srli_epi32(
            _mm256_and_si256(_mm256_slli_epi32((y_c256), upsample_left), c3f),
            1);
      } else {
        shift = _mm256_srli_epi32(_mm256_and_si256(y_c256, c3f), 1);
      }
      diff = _mm256_sub_epi32(a1_y, a0_y);  // a[x+1] - a[x]
      a32 = _mm256_slli_epi32(a0_y, 5);     // a[x] * 32
      a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

      b = _mm256_mullo_epi32(diff, shift);
      res = _mm256_add_epi32(a32, b);
      res = _mm256_srli_epi32(res, 5);

      resy = _mm256_castsi256_si128(_mm256_packus_epi32(
          res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1))));
    } else {
      resy = resx;
    }
    resxy =
        _mm_blendv_epi8(resx, resy, *(__m128i *)HighbdBaseMask[base_min_diff]);
    _mm_storeu_si128((__m128i *)(dst), resxy);
    dst += stride;
  }
}

static void highbd_dr_prediction_z2_Nx8_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  const int min_base_x = -((1 + mrl_index) << upsample_above);
  const int min_base_y = -((1 + mrl_index) << upsample_left);
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m128i c3f, min_base_y128;
  __m256i a0_x, a1_x, diff, a32, a16;
  __m128i a0_x128, a1_x128;

  a16 = _mm256_set1_epi16(16);
  c3f = _mm_set1_epi16(0x3f);
  min_base_y128 = _mm_set1_epi16(min_base_y);
  __m128i cmrlIdx = _mm_set1_epi16(mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i b, res, shift;
    __m128i resx, resy, resxy;
    int y = r + 1;
    int base_x = (-(y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1) >> upsample_above;
    }
    int base_min_diff =
        (min_base_x - base_x + upsample_above) >> upsample_above;
    if (base_min_diff > 8) {
      base_min_diff = 8;
    } else {
      if (base_min_diff < 0) base_min_diff = 0;
    }

    if (base_shift > 7) {
      a0_x = _mm256_setzero_si256();
      a1_x = _mm256_setzero_si256();
      shift = _mm256_setzero_si256();
    } else {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
      if (upsample_above) {
        __m128i mask, atmp0, atmp1, atmp2, atmp3;
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + 8 + base_shift));
        atmp0 = _mm_shuffle_epi8(a0_x128,
                                 *(__m128i *)HighbdEvenOddMaskx[base_shift]);
        atmp1 = _mm_shuffle_epi8(a1_x128,
                                 *(__m128i *)HighbdEvenOddMaskx[base_shift]);
        atmp2 = _mm_shuffle_epi8(
            a0_x128, *(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16));
        atmp3 = _mm_shuffle_epi8(
            a1_x128, *(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16));
        mask = _mm_cmpgt_epi8(*(__m128i *)HighbdEvenOddMaskx[base_shift],
                              _mm_set1_epi8(15));
        a0_x128 = _mm_blendv_epi8(atmp0, atmp1, mask);
        mask = _mm_cmpgt_epi8(*(__m128i *)(HighbdEvenOddMaskx[base_shift] + 16),
                              _mm_set1_epi8(15));
        a1_x128 = _mm_blendv_epi8(atmp2, atmp3, mask);

        shift = _mm256_castsi128_si256(_mm_srli_epi16(
            _mm_and_si128(
                _mm_slli_epi16(_mm_setr_epi16(-(y + mrl_index) * dx,
                                              (1 << 6) - (y + mrl_index) * dx,
                                              (2 << 6) - (y + mrl_index) * dx,
                                              (3 << 6) - (y + mrl_index) * dx,
                                              (4 << 6) - (y + mrl_index) * dx,
                                              (5 << 6) - (y + mrl_index) * dx,
                                              (6 << 6) - (y + mrl_index) * dx,
                                              (7 << 6) - (y + mrl_index) * dx),
                               upsample_above),
                c3f),
            1));
      } else {
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + 1 + base_shift));
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 =
            _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

        shift = _mm256_castsi128_si256(_mm_srli_epi16(
            _mm_and_si128(_mm_setr_epi16(-(y + mrl_index) * dx,
                                         (1 << 6) - (y + mrl_index) * dx,
                                         (2 << 6) - (y + mrl_index) * dx,
                                         (3 << 6) - (y + mrl_index) * dx,
                                         (4 << 6) - (y + mrl_index) * dx,
                                         (5 << 6) - (y + mrl_index) * dx,
                                         (6 << 6) - (y + mrl_index) * dx,
                                         (7 << 6) - (y + mrl_index) * dx),
                          c3f),
            1));
      }
      a0_x = _mm256_castsi128_si256(a0_x128);
      a1_x = _mm256_castsi128_si256(a1_x128);
    }

    // y calc
    __m128i a0_y, a1_y, shifty;
    if (base_x < min_base_x) {
      DECLARE_ALIGNED(32, int16_t, base_y_c[8]);
      __m128i r6, c1234, dy128, y_c128, base_y_c128, mask128;
      r6 = _mm_set1_epi16(r << 6);
      dy128 = _mm_set1_epi16(dy);
      c1234 = _mm_setr_epi16(1, 2, 3, 4, 5, 6, 7, 8);
      __m128i c1234_ = _mm_add_epi16(c1234, cmrlIdx);
      y_c128 = _mm_sub_epi16(r6, _mm_mullo_epi16(c1234_, dy128));
      base_y_c128 = _mm_srai_epi16(y_c128, frac_bits_y);
      mask128 = _mm_cmpgt_epi16(min_base_y128, base_y_c128);
      base_y_c128 = _mm_andnot_si128(mask128, base_y_c128);
      _mm_store_si128((__m128i *)base_y_c, base_y_c128);

      a0_y = _mm_setr_epi16(left[base_y_c[0]], left[base_y_c[1]],
                            left[base_y_c[2]], left[base_y_c[3]],
                            left[base_y_c[4]], left[base_y_c[5]],
                            left[base_y_c[6]], left[base_y_c[7]]);
      a1_y = _mm_setr_epi16(left[base_y_c[0] + 1], left[base_y_c[1] + 1],
                            left[base_y_c[2] + 1], left[base_y_c[3] + 1],
                            left[base_y_c[4] + 1], left[base_y_c[5] + 1],
                            left[base_y_c[6] + 1], left[base_y_c[7] + 1]);

      if (upsample_left) {
        shifty = _mm_srli_epi16(
            _mm_and_si128(_mm_slli_epi16((y_c128), upsample_left), c3f), 1);
      } else {
        shifty = _mm_srli_epi16(_mm_and_si128(y_c128, c3f), 1);
      }
      a0_x = _mm256_inserti128_si256(a0_x, a0_y, 1);
      a1_x = _mm256_inserti128_si256(a1_x, a1_y, 1);
      shift = _mm256_inserti128_si256(shift, shifty, 1);
    }

    diff = _mm256_sub_epi16(a1_x, a0_x);  // a[x+1] - a[x]
    a32 = _mm256_slli_epi16(a0_x, 5);     // a[x] * 32
    a32 = _mm256_add_epi16(a32, a16);     // a[x] * 32 + 16

    b = _mm256_mullo_epi16(diff, shift);
    res = _mm256_add_epi16(a32, b);
    res = _mm256_srli_epi16(res, 5);

    resx = _mm256_castsi256_si128(res);
    resy = _mm256_extracti128_si256(res, 1);

    resxy =
        _mm_blendv_epi8(resx, resy, *(__m128i *)HighbdBaseMask[base_min_diff]);
    _mm_storeu_si128((__m128i *)(dst), resxy);
    dst += stride;
  }
}

static void highbd_dr_prediction_32bit_z2_HxW_avx2(
    int H, int W, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  // here upsample_above and upsample_left are 0 by design of
  // av1_use_intra_edge_upsample
  const int min_base_x = -(1 + mrl_index);
  const int min_base_y = -(1 + mrl_index);
  (void)upsample_above;
  (void)upsample_left;
  const int frac_bits_x = 6;
  const int frac_bits_y = 6;

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0_x, a1_x, a0_y, a1_y, a32, a0_1_x, a1_1_x, a16, c1;
  __m256i diff, min_base_y256, c3f, dy256, c1234, c0123, c8;
  __m128i a0_x128, a1_x128, a0_1_x128, a1_1_x128;
  DECLARE_ALIGNED(32, int, base_y_c[16]);

  a16 = _mm256_set1_epi32(16);
  c1 = _mm256_srli_epi32(a16, 4);
  c8 = _mm256_srli_epi32(a16, 1);
  min_base_y256 = _mm256_set1_epi32(min_base_y);
  c3f = _mm256_set1_epi32(0x3f);
  dy256 = _mm256_set1_epi32(dy);
  c0123 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
  c1234 = _mm256_add_epi32(c0123, c1);
  __m256i cmrlIdx = _mm256_set1_epi32(mrl_index);
  for (int r = 0; r < H; r++) {
    __m256i b, res, shift, ydx;
    __m256i resx[2], resy[2];
    __m256i resxy, j256, r6;
    for (int j = 0; j < W; j += 16) {
      j256 = _mm256_set1_epi32(j);
      int y = r + 1;
      ydx = _mm256_set1_epi32((y + mrl_index) * dx);

      int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
      int base_shift = 0;
      if ((base_x) < (min_base_x - 1)) {
        base_shift = (min_base_x - base_x - 1);
      }
      int base_min_diff = (min_base_x - base_x);
      if (base_min_diff > 16) {
        base_min_diff = 16;
      } else {
        if (base_min_diff < 0) base_min_diff = 0;
      }

      if (base_shift > 7) {
        resx[0] = _mm256_setzero_si256();
      } else {
        a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 1));
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 =
            _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

        a0_x = _mm256_cvtepu16_epi32(a0_x128);
        a1_x = _mm256_cvtepu16_epi32(a1_x128);

        r6 = _mm256_slli_epi32(_mm256_add_epi32(c0123, j256), 6);
        shift = _mm256_srli_epi32(
            _mm256_and_si256(_mm256_sub_epi32(r6, ydx), c3f), 1);

        diff = _mm256_sub_epi32(a1_x, a0_x);  // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0_x, 5);     // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

        b = _mm256_mullo_epi32(diff, shift);
        res = _mm256_add_epi32(a32, b);
        res = _mm256_srli_epi32(res, 5);

        resx[0] = _mm256_packus_epi32(
            res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1)));
      }
      int base_shift8 = 0;
      if ((base_x + 8) < (min_base_x - 1)) {
        base_shift8 = (min_base_x - (base_x + 8) - 1);
      }
      if (base_shift8 > 7) {
        resx[1] = _mm256_setzero_si256();
      } else {
        a0_1_x128 =
            _mm_loadu_si128((__m128i *)(above + base_x + base_shift8 + 8));
        a1_1_x128 =
            _mm_loadu_si128((__m128i *)(above + base_x + base_shift8 + 9));
        a0_1_x128 = _mm_shuffle_epi8(a0_1_x128,
                                     *(__m128i *)HighbdLoadMaskx[base_shift8]);
        a1_1_x128 = _mm_shuffle_epi8(a1_1_x128,
                                     *(__m128i *)HighbdLoadMaskx[base_shift8]);

        a0_1_x = _mm256_cvtepu16_epi32(a0_1_x128);
        a1_1_x = _mm256_cvtepu16_epi32(a1_1_x128);

        r6 = _mm256_slli_epi32(
            _mm256_add_epi32(c0123, _mm256_add_epi32(j256, c8)), 6);
        shift = _mm256_srli_epi32(
            _mm256_and_si256(_mm256_sub_epi32(r6, ydx), c3f), 1);

        diff = _mm256_sub_epi32(a1_1_x, a0_1_x);  // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0_1_x, 5);       // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);         // a[x] * 32 + 16
        b = _mm256_mullo_epi32(diff, shift);

        resx[1] = _mm256_add_epi32(a32, b);
        resx[1] = _mm256_srli_epi32(resx[1], 5);
        resx[1] = _mm256_packus_epi32(
            resx[1],
            _mm256_castsi128_si256(_mm256_extracti128_si256(resx[1], 1)));
      }
      resx[0] =
          _mm256_inserti128_si256(resx[0], _mm256_castsi256_si128(resx[1]),
                                  1);  // 16 16bit values

      // y calc
      resy[0] = _mm256_setzero_si256();
      if ((base_x < min_base_x)) {
        __m256i c256, y_c256, y_c_1_256, base_y_c256, mask256;
        r6 = _mm256_set1_epi32(r << 6);
        c256 = _mm256_add_epi32(j256, c1234);
        __m256i c256_ = _mm256_add_epi32(c256, cmrlIdx);
        y_c256 = _mm256_sub_epi32(r6, _mm256_mullo_epi32(c256_, dy256));
        base_y_c256 = _mm256_srai_epi32(y_c256, frac_bits_y);
        mask256 = _mm256_cmpgt_epi32(min_base_y256, base_y_c256);
        base_y_c256 = _mm256_andnot_si256(mask256, base_y_c256);
        _mm256_store_si256((__m256i *)base_y_c, base_y_c256);
        c256 = _mm256_add_epi32(c256_, c8);
        y_c_1_256 = _mm256_sub_epi32(r6, _mm256_mullo_epi32(c256, dy256));
        base_y_c256 = _mm256_srai_epi32(y_c_1_256, frac_bits_y);
        mask256 = _mm256_cmpgt_epi32(min_base_y256, base_y_c256);
        base_y_c256 = _mm256_andnot_si256(mask256, base_y_c256);
        _mm256_store_si256((__m256i *)(base_y_c + 8), base_y_c256);

        a0_y = _mm256_cvtepu16_epi32(_mm_setr_epi16(
            left[base_y_c[0]], left[base_y_c[1]], left[base_y_c[2]],
            left[base_y_c[3]], left[base_y_c[4]], left[base_y_c[5]],
            left[base_y_c[6]], left[base_y_c[7]]));
        a1_y = _mm256_cvtepu16_epi32(_mm_setr_epi16(
            left[base_y_c[0] + 1], left[base_y_c[1] + 1], left[base_y_c[2] + 1],
            left[base_y_c[3] + 1], left[base_y_c[4] + 1], left[base_y_c[5] + 1],
            left[base_y_c[6] + 1], left[base_y_c[7] + 1]));

        shift = _mm256_srli_epi32(_mm256_and_si256(y_c256, c3f), 1);

        diff = _mm256_sub_epi32(a1_y, a0_y);  // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0_y, 5);     // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

        b = _mm256_mullo_epi32(diff, shift);
        res = _mm256_add_epi32(a32, b);
        res = _mm256_srli_epi32(res, 5);

        resy[0] = _mm256_packus_epi32(
            res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1)));

        a0_y = _mm256_cvtepu16_epi32(_mm_setr_epi16(
            left[base_y_c[8]], left[base_y_c[9]], left[base_y_c[10]],
            left[base_y_c[11]], left[base_y_c[12]], left[base_y_c[13]],
            left[base_y_c[14]], left[base_y_c[15]]));
        a1_y = _mm256_cvtepu16_epi32(
            _mm_setr_epi16(left[base_y_c[8] + 1], left[base_y_c[9] + 1],
                           left[base_y_c[10] + 1], left[base_y_c[11] + 1],
                           left[base_y_c[12] + 1], left[base_y_c[13] + 1],
                           left[base_y_c[14] + 1], left[base_y_c[15] + 1]));
        shift = _mm256_srli_epi32(_mm256_and_si256(y_c_1_256, c3f), 1);

        diff = _mm256_sub_epi32(a1_y, a0_y);  // a[x+1] - a[x]
        a32 = _mm256_slli_epi32(a0_y, 5);     // a[x] * 32
        a32 = _mm256_add_epi32(a32, a16);     // a[x] * 32 + 16

        b = _mm256_mullo_epi32(diff, shift);
        res = _mm256_add_epi32(a32, b);
        res = _mm256_srli_epi32(res, 5);

        resy[1] = _mm256_packus_epi32(
            res, _mm256_castsi128_si256(_mm256_extracti128_si256(res, 1)));

        resy[0] =
            _mm256_inserti128_si256(resy[0], _mm256_castsi256_si128(resy[1]),
                                    1);  // 16 16bit values
      }

      resxy = _mm256_blendv_epi8(resx[0], resy[0],
                                 *(__m256i *)HighbdBaseMask[base_min_diff]);
      _mm256_storeu_si256((__m256i *)(dst + j), resxy);
    }  // for j
    dst += stride;
  }
}

static void highbd_dr_prediction_z2_HxW_avx2(
    int H, int W, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy,
    int mrl_index) {
  // here upsample_above and upsample_left are 0 by design of
  // av1_use_intra_edge_upsample
  const int min_base_x = -(1 + mrl_index);
  const int min_base_y = -(1 + mrl_index);
  (void)upsample_above;
  (void)upsample_left;
  const int frac_bits_x = 6;
  const int frac_bits_y = 6;

  // pre-filter above pixels
  // store in temp buffers:
  //   above[x] * 32 + 16
  //   above[x+1] - above[x]
  // final pixels will be calculated as:
  //   (above[x] * 32 + 16 + (above[x+1] - above[x]) * shift) >> 5
  __m256i a0_x, a1_x, a32, a16, c3f, c1;
  __m256i diff, min_base_y256, dy256, c1234, c0123;
  DECLARE_ALIGNED(32, int16_t, base_y_c[16]);

  a16 = _mm256_set1_epi16(16);
  c1 = _mm256_srli_epi16(a16, 4);
  min_base_y256 = _mm256_set1_epi16(min_base_y);
  c3f = _mm256_set1_epi16(0x3f);
  dy256 = _mm256_set1_epi16(dy);
  c0123 =
      _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
  c1234 = _mm256_add_epi16(c0123, c1);
  __m256i cmrlIdx = _mm256_set1_epi16(mrl_index);

  for (int r = 0; r < H; r++) {
    __m256i b, res, shift;
    __m256i resx, resy, ydx;
    __m256i resxy, j256, r6;
    __m128i a0_x128, a1_x128, a0_1_x128, a1_1_x128;
    int y = r + 1;
    ydx = _mm256_set1_epi16((short)((y + mrl_index) * dx));

    for (int j = 0; j < W; j += 16) {
      j256 = _mm256_set1_epi16(j);
      int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
      int base_shift = 0;
      if ((base_x) < (min_base_x - 1)) {
        base_shift = (min_base_x - (base_x)-1);
      }
      int base_min_diff = (min_base_x - base_x);
      if (base_min_diff > 16) {
        base_min_diff = 16;
      } else {
        if (base_min_diff < 0) base_min_diff = 0;
      }

      if (base_shift < 8) {
        a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
        a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 1));
        a0_x128 =
            _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
        a1_x128 =
            _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

        a0_x = _mm256_castsi128_si256(a0_x128);
        a1_x = _mm256_castsi128_si256(a1_x128);
      } else {
        a0_x = _mm256_setzero_si256();
        a1_x = _mm256_setzero_si256();
      }

      int base_shift1 = 0;
      if (base_shift > 8) {
        base_shift1 = base_shift - 8;
      }
      if (base_shift1 < 8) {
        a0_1_x128 =
            _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 8));
        a1_1_x128 =
            _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 9));
        a0_1_x128 = _mm_shuffle_epi8(a0_1_x128,
                                     *(__m128i *)HighbdLoadMaskx[base_shift1]);
        a1_1_x128 = _mm_shuffle_epi8(a1_1_x128,
                                     *(__m128i *)HighbdLoadMaskx[base_shift1]);

        a0_x = _mm256_inserti128_si256(a0_x, a0_1_x128, 1);
        a1_x = _mm256_inserti128_si256(a1_x, a1_1_x128, 1);
      }
      r6 = _mm256_slli_epi16(_mm256_add_epi16(c0123, j256), 6);
      shift = _mm256_srli_epi16(
          _mm256_and_si256(_mm256_sub_epi16(r6, ydx), c3f), 1);

      diff = _mm256_sub_epi16(a1_x, a0_x);  // a[x+1] - a[x]
      a32 = _mm256_slli_epi16(a0_x, 5);     // a[x] * 32
      a32 = _mm256_add_epi16(a32, a16);     // a[x] * 32 + 16

      b = _mm256_mullo_epi16(diff, shift);
      res = _mm256_add_epi16(a32, b);
      resx = _mm256_srli_epi16(res, 5);  // 16 16-bit values

      // y calc
      resy = _mm256_setzero_si256();
      __m256i a0_y, a1_y, shifty;
      if ((base_x < min_base_x)) {
        __m256i c256, y_c256, base_y_c256, mask256, mul16;
        r6 = _mm256_set1_epi16(r << 6);
        c256 = _mm256_add_epi16(j256, c1234);
        __m256i c256_ = _mm256_add_epi16(c256, cmrlIdx);
        mul16 = _mm256_min_epu16(_mm256_mullo_epi16(c256_, dy256),
                                 _mm256_srli_epi16(min_base_y256, 1));
        y_c256 = _mm256_sub_epi16(r6, mul16);
        base_y_c256 = _mm256_srai_epi16(y_c256, frac_bits_y);
        mask256 = _mm256_cmpgt_epi16(min_base_y256, base_y_c256);
        base_y_c256 = _mm256_andnot_si256(mask256, base_y_c256);
        _mm256_store_si256((__m256i *)base_y_c, base_y_c256);

        a0_y = _mm256_setr_epi16(
            left[base_y_c[0]], left[base_y_c[1]], left[base_y_c[2]],
            left[base_y_c[3]], left[base_y_c[4]], left[base_y_c[5]],
            left[base_y_c[6]], left[base_y_c[7]], left[base_y_c[8]],
            left[base_y_c[9]], left[base_y_c[10]], left[base_y_c[11]],
            left[base_y_c[12]], left[base_y_c[13]], left[base_y_c[14]],
            left[base_y_c[15]]);
        base_y_c256 = _mm256_add_epi16(base_y_c256, c1);
        _mm256_store_si256((__m256i *)base_y_c, base_y_c256);

        a1_y = _mm256_setr_epi16(
            left[base_y_c[0]], left[base_y_c[1]], left[base_y_c[2]],
            left[base_y_c[3]], left[base_y_c[4]], left[base_y_c[5]],
            left[base_y_c[6]], left[base_y_c[7]], left[base_y_c[8]],
            left[base_y_c[9]], left[base_y_c[10]], left[base_y_c[11]],
            left[base_y_c[12]], left[base_y_c[13]], left[base_y_c[14]],
            left[base_y_c[15]]);

        shifty = _mm256_srli_epi16(_mm256_and_si256(y_c256, c3f), 1);

        diff = _mm256_sub_epi16(a1_y, a0_y);  // a[x+1] - a[x]
        a32 = _mm256_slli_epi16(a0_y, 5);     // a[x] * 32
        a32 = _mm256_add_epi16(a32, a16);     // a[x] * 32 + 16

        b = _mm256_mullo_epi16(diff, shifty);
        res = _mm256_add_epi16(a32, b);
        resy = _mm256_srli_epi16(res, 5);
      }

      resxy = _mm256_blendv_epi8(resx, resy,
                                 *(__m256i *)HighbdBaseMask[base_min_diff]);
      _mm256_storeu_si256((__m256i *)(dst + j), resxy);
    }  // for j
    dst += stride;
  }
}

// Directional prediction, zone 2: 90 < angle < 180
void av1_highbd_dr_prediction_z2_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int upsample_above,
                                      int upsample_left, int dx, int dy, int bd,
                                      int mrl_index) {
  (void)bd;
  assert(dx > 0);
  assert(dy > 0);
  switch (bw) {
    case 4:
      if (bd < 12) {
        highbd_dr_prediction_z2_Nx4_avx2(bh, dst, stride, above, left,
                                         upsample_above, upsample_left, dx, dy,
                                         mrl_index);
      } else {
        highbd_dr_prediction_32bit_z2_Nx4_avx2(bh, dst, stride, above, left,
                                               upsample_above, upsample_left,
                                               dx, dy, mrl_index);
      }
      break;
    case 8:
      if (bd < 12) {
        highbd_dr_prediction_z2_Nx8_avx2(bh, dst, stride, above, left,
                                         upsample_above, upsample_left, dx, dy,
                                         mrl_index);
      } else {
        highbd_dr_prediction_32bit_z2_Nx8_avx2(bh, dst, stride, above, left,
                                               upsample_above, upsample_left,
                                               dx, dy, mrl_index);
      }
      break;
    default:
      if (bd < 12) {
        highbd_dr_prediction_z2_HxW_avx2(bh, bw, dst, stride, above, left,
                                         upsample_above, upsample_left, dx, dy,
                                         mrl_index);
      } else {
        highbd_dr_prediction_32bit_z2_HxW_avx2(bh, bw, dst, stride, above, left,
                                               upsample_above, upsample_left,
                                               dx, dy, mrl_index);
      }
      break;
  }
}

//  Directional prediction, zone 3 functions
static void highbd_dr_prediction_z3_4x4_avx2(uint16_t *dst, ptrdiff_t stride,
                                             const uint16_t *left,
                                             int upsample_left, int dy, int bd,
                                             int mrl_index) {
  __m128i dstvec[4], d[4];
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(4, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        4, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose4x8_8x4_low_sse2(&dstvec[0], &dstvec[1], &dstvec[2],
                                   &dstvec[3], &d[0], &d[1], &d[2], &d[3]);
  _mm_storel_epi64((__m128i *)(dst + 0 * stride), d[0]);
  _mm_storel_epi64((__m128i *)(dst + 1 * stride), d[1]);
  _mm_storel_epi64((__m128i *)(dst + 2 * stride), d[2]);
  _mm_storel_epi64((__m128i *)(dst + 3 * stride), d[3]);
  return;
}

static void highbd_dr_prediction_z3_8x8_avx2(uint16_t *dst, ptrdiff_t stride,
                                             const uint16_t *left,
                                             int upsample_left, int dy, int bd,
                                             int mrl_index) {
  __m128i dstvec[8], d[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(8, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        8, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose8x8_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                           &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                           &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                           &d[7]);
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_4x8_avx2(uint16_t *dst, ptrdiff_t stride,
                                             const uint16_t *left,
                                             int upsample_left, int dy, int bd,
                                             int mrl_index) {
  __m128i dstvec[4], d[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(4, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        4, dstvec, left, upsample_left, dy, mrl_index);
  }

  highbd_transpose4x8_8x4_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                               &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                               &d[7]);
  for (int i = 0; i < 8; i++) {
    _mm_storel_epi64((__m128i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_8x4_avx2(uint16_t *dst, ptrdiff_t stride,
                                             const uint16_t *left,
                                             int upsample_left, int dy, int bd,
                                             int mrl_index) {
  __m128i dstvec[8], d[4];
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(8, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        8, dstvec, left, upsample_left, dy, mrl_index);
  }

  highbd_transpose8x8_low_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                               &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                               &d[0], &d[1], &d[2], &d[3]);
  _mm_storeu_si128((__m128i *)(dst + 0 * stride), d[0]);
  _mm_storeu_si128((__m128i *)(dst + 1 * stride), d[1]);
  _mm_storeu_si128((__m128i *)(dst + 2 * stride), d[2]);
  _mm_storeu_si128((__m128i *)(dst + 3 * stride), d[3]);
}

static void highbd_dr_prediction_z3_8x16_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[8], d[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(8, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        8, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose8x16_16x8_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                                 &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                                 &d[0], &d[1], &d[2], &d[3], &d[4], &d[5],
                                 &d[6], &d[7]);
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_extracti128_si256(d[i - 8], 1));
  }
}

static void highbd_dr_prediction_z3_16x8_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[16];
  __m256i dstvec256[8], d256[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(16, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        16, dstvec, left, upsample_left, dy, mrl_index);
  }
  for (int i = 0; i < 8; ++i) {
    dstvec256[i] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 0]), dstvec[i + 8], 0x1);
  }

  highbd_transpose8x16_16x8_avx2(
      &dstvec256[0], &dstvec256[1], &dstvec256[2], &dstvec256[3], &dstvec256[4],
      &dstvec256[5], &dstvec256[6], &dstvec256[7], &d256[0], &d256[1], &d256[2],
      &d256[3], &d256[4], &d256[5], &d256[6], &d256[7]);

  for (int i = 0; i < 8; ++i) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d256[i]);
  }
}

static void highbd_dr_prediction_z3_4x16_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[4], d[4], d1;
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(4, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        4, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose4x16_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                            &d[0], &d[1], &d[2], &d[3]);
  for (int i = 0; i < 4; i++) {
    _mm_storel_epi64((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
    d1 = _mm256_bsrli_epi128(d[i], 8);
    _mm_storel_epi64((__m128i *)(dst + (i + 4) * stride),
                     _mm256_castsi256_si128(d1));
    _mm_storel_epi64((__m128i *)(dst + (i + 8) * stride),
                     _mm256_extracti128_si256(d[i], 1));
    _mm_storel_epi64((__m128i *)(dst + (i + 12) * stride),
                     _mm256_extracti128_si256(d1, 1));
  }
}

static void highbd_dr_prediction_z3_16x4_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[16];
  __m256i d[4];
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(16, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        16, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose16x4_8x8_avx2(dstvec, d);

  for (int i = 0; i < 4; ++i) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_8x32_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[16], d[16];

  if (bd < 12) {
    highbd_dr_prediction_z1_32xN_internal_avx2(8, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
        8, dstvec, left, upsample_left, dy, mrl_index);
  }

  for (int i = 0; i < 16; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 1], &dstvec[i + 2], &dstvec[i + 3],
        &dstvec[i + 4], &dstvec[i + 5], &dstvec[i + 6], &dstvec[i + 7], &d[i],
        &d[i + 1], &d[i + 2], &d[i + 3], &d[i + 4], &d[i + 5], &d[i + 6],
        &d[i + 7]);
  }

  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 8) * stride),
                     _mm256_extracti128_si256(d[i], 1));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 8) * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 16) * stride),
                     _mm256_extracti128_si256(d[i], 1));
  }
}

static void highbd_dr_prediction_z3_32x8_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[32], d[32];
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(32, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        32, dstvec, left, upsample_left, dy, mrl_index);
  }

  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x8_sse2(&dstvec[0 + i], &dstvec[1 + i], &dstvec[2 + i],
                             &dstvec[3 + i], &dstvec[4 + i], &dstvec[5 + i],
                             &dstvec[6 + i], &dstvec[7 + i], &d[0 + i],
                             &d[1 + i], &d[2 + i], &d[3 + i], &d[4 + i],
                             &d[5 + i], &d[6 + i], &d[7 + i]);
  }
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride), d[i]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 8), d[i + 8]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 16), d[i + 16]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 24), d[i + 24]);
  }
}

static void highbd_dr_prediction_z3_16x16_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  __m256i dstvec[16], d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(16, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        16, dstvec, left, upsample_left, dy, mrl_index);
  }

  highbd_transpose16x16_avx2(dstvec, d);

  for (int i = 0; i < 16; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_32x32_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  __m256i dstvec[64], d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_32xN_internal_avx2(32, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
        32, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose16x16_avx2(dstvec, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + j * stride), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 16, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + j * stride + 16), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 32, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + (j + 16) * stride), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 48, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + (j + 16) * stride + 16), d[j]);
  }
}

static void highbd_dr_prediction_z3_64x64_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[64 * 64]);
  if (bd < 12) {
    highbd_dr_prediction_z1_64xN_avx2(64, dstT, 64, left, upsample_left, dy,
                                      mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_avx2(64, dstT, 64, left, upsample_left,
                                            dy, mrl_index);
  }
  highbd_transpose(dstT, 64, dst, stride, 64, 64);
}

static void highbd_dr_prediction_z3_16x32_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  __m256i dstvec[32], d[32];
  if (bd < 12) {
    highbd_dr_prediction_z1_32xN_internal_avx2(16, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
        16, dstvec, left, upsample_left, dy, mrl_index);
  }
  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 1], &dstvec[i + 2], &dstvec[i + 3],
        &dstvec[i + 4], &dstvec[i + 5], &dstvec[i + 6], &dstvec[i + 7], &d[i],
        &d[i + 1], &d[i + 2], &d[i + 3], &d[i + 4], &d[i + 5], &d[i + 6],
        &d[i + 7]);
  }
  // store
  for (int j = 0; j < 32; j += 16) {
    for (int i = 0; i < 8; i++) {
      _mm_storeu_si128((__m128i *)(dst + (i + j) * stride),
                       _mm256_castsi256_si128(d[(i + j)]));
    }
    for (int i = 0; i < 8; i++) {
      _mm_storeu_si128((__m128i *)(dst + (i + j) * stride + 8),
                       _mm256_castsi256_si128(d[(i + j) + 8]));
    }
    for (int i = 8; i < 16; i++) {
      _mm256_storeu_si256(
          (__m256i *)(dst + (i + j) * stride),
          _mm256_inserti128_si256(
              d[(i + j)], _mm256_extracti128_si256(d[(i + j) - 8], 1), 0));
    }
  }
}

static void highbd_dr_prediction_z3_32x16_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  __m256i dstvec[32], d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(32, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        32, dstvec, left, upsample_left, dy, mrl_index);
  }
  for (int i = 0; i < 32; i += 16) {
    highbd_transpose16x16_avx2((dstvec + i), d);
    for (int j = 0; j < 16; j++) {
      _mm256_storeu_si256((__m256i *)(dst + j * stride + i), d[j]);
    }
  }
}

static void highbd_dr_prediction_z3_32x64_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  uint16_t dstT[64 * 32];
  if (bd < 12) {
    highbd_dr_prediction_z1_64xN_avx2(32, dstT, 64, left, upsample_left, dy,
                                      mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_avx2(32, dstT, 64, left, upsample_left,
                                            dy, mrl_index);
  }
  highbd_transpose(dstT, 64, dst, stride, 32, 64);
}

static void highbd_dr_prediction_z3_64x32_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[32 * 64]);
  highbd_dr_prediction_z1_32xN_avx2(64, dstT, 32, left, upsample_left, dy, bd,
                                    mrl_index);
  highbd_transpose(dstT, 32, dst, stride, 64, 32);
  return;
}

static void highbd_dr_prediction_z3_16x64_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[64 * 16]);
  if (bd < 12) {
    highbd_dr_prediction_z1_64xN_avx2(16, dstT, 64, left, upsample_left, dy,
                                      mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_avx2(16, dstT, 64, left, upsample_left,
                                            dy, mrl_index);
  }
  highbd_transpose(dstT, 64, dst, stride, 16, 64);
}

static void highbd_dr_prediction_z3_64x16_avx2(uint16_t *dst, ptrdiff_t stride,
                                               const uint16_t *left,
                                               int upsample_left, int dy,
                                               int bd, int mrl_index) {
  __m256i dstvec[64], d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_16xN_internal_avx2(64, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_avx2(
        64, dstvec, left, upsample_left, dy, mrl_index);
  }
  for (int i = 0; i < 64; i += 16) {
    highbd_transpose16x16_avx2((dstvec + i), d);
    for (int j = 0; j < 16; j++) {
      _mm256_storeu_si256((__m256i *)(dst + j * stride + i), d[j]);
    }
  }
}

#if CONFIG_FLEX_PARTITION
static void highbd_dr_prediction_z3_32x4_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[32];
  __m256i d[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(32, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        32, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose16x4_8x8_avx2(dstvec, d);
  highbd_transpose16x4_8x8_avx2(dstvec + 16, d + 4);

  for (int i = 0; i < 4; ++i) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d[i + 4]);
  }
}

static void highbd_dr_prediction_z3_64x4_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[64];
  __m256i d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_4xN_internal_avx2(64, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_avx2(
        64, dstvec, left, upsample_left, dy, mrl_index);
  }
  highbd_transpose16x4_8x8_avx2(dstvec, d);
  highbd_transpose16x4_8x8_avx2(dstvec + 16, d + 4);
  highbd_transpose16x4_8x8_avx2(dstvec + 32, d + 8);
  highbd_transpose16x4_8x8_avx2(dstvec + 48, d + 12);

  for (int i = 0; i < 4; ++i) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d[i + 4]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 32), d[i + 8]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 48), d[i + 12]);
  }
}

static void highbd_dr_prediction_z3_4x32_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[8], d[8];
  if (bd < 12) {
    highbd_dr_prediction_z1_32xN_internal_avx2(4, dstvec, left, upsample_left,
                                               dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_avx2(
        4, dstvec, left, upsample_left, dy, mrl_index);
  }

  highbd_transpose8x16_16x8_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                                 &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                                 &d[0], &d[1], &d[2], &d[3], &d[4], &d[5],
                                 &d[6], &d[7]);

  for (int i = 0; i < 8; i++) {
    __m128i temp_lo = _mm256_castsi256_si128(d[i]);
    __m128i temp_hi = _mm256_extracti128_si256(d[i], 1);
    _mm_storel_epi64((__m128i *)(dst + i * stride), temp_lo);
    _mm_storel_epi64((__m128i *)(dst + (i + 16) * stride),
                     _mm_srli_si128(temp_lo, 8));
    _mm_storel_epi64((__m128i *)(dst + (i + 8) * stride), temp_hi);
    _mm_storel_epi64((__m128i *)(dst + (i + 24) * stride),
                     _mm_srli_si128(temp_hi, 8));
  }
}

static void highbd_dr_prediction_z3_64x8_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m128i dstvec[64];
  __m256i dstvec256[32], d256[32];
  if (bd < 12) {
    highbd_dr_prediction_z1_8xN_internal_avx2(64, dstvec, left, upsample_left,
                                              dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_avx2(
        64, dstvec, left, upsample_left, dy, mrl_index);
  }

  for (int i = 0; i < 8; ++i) {
    dstvec256[i] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 0]), dstvec[i + 8], 0x1);
    dstvec256[i + 8] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 16]), dstvec[i + 24], 0x1);
    dstvec256[i + 16] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 32]), dstvec[i + 40], 0x1);
    dstvec256[i + 24] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 48]), dstvec[i + 56], 0x1);
  }

  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec256[i + 0], &dstvec256[i + 1], &dstvec256[i + 2],
        &dstvec256[i + 3], &dstvec256[i + 4], &dstvec256[i + 5],
        &dstvec256[i + 6], &dstvec256[i + 7], &d256[i + 0], &d256[i + 1],
        &d256[i + 2], &d256[i + 3], &d256[i + 4], &d256[i + 5], &d256[i + 6],
        &d256[i + 7]);
  }
  for (int i = 0; i < 8; ++i) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d256[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d256[i + 8]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 32), d256[i + 16]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 48), d256[i + 24]);
  }
}

static void highbd_dr_prediction_z3_4x64_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[16], d[16];
  if (bd < 12) {
    highbd_dr_prediction_z1_64xN_avx2(4, (uint16_t *)dstvec, 64, left,
                                      upsample_left, dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_avx2(4, (uint16_t *)dstvec, 64, left,
                                            upsample_left, dy, mrl_index);
  }

  // At this point, each 4 entries of dstvec[] forms one full 64-pixel
  // row (pre transposition) / col (post transposition)
  for (int i = 0; i < 4; i++) {
    highbd_transpose4x16_avx2(&dstvec[i], &dstvec[i + 4], &dstvec[i + 8],
                              &dstvec[i + 12], &d[4 * i], &d[4 * i + 1],
                              &d[4 * i + 2], &d[4 * i + 3]);
  }

  // Now each 4 elements of d contains 16 rows of 4 output pixels,
  // ordered like:
  // {0, 4, 8, 12}, {1, 5, 9, 13}, ...
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      const __m128i temph = _mm256_extracti128_si256(d[4 * i + j], 1);
      const __m128i templ = _mm256_castsi256_si128(d[4 * i + j]);
      xx_storel_64(dst + (16 * i + j) * stride, templ);
      xx_storel_64(dst + (16 * i + j + 4) * stride, _mm_srli_si128(templ, 8));
      xx_storel_64(dst + (16 * i + j + 8) * stride, temph);
      xx_storel_64(dst + (16 * i + j + 12) * stride, _mm_srli_si128(temph, 8));
    }
  }
}

static void highbd_dr_prediction_z3_8x64_avx2(uint16_t *dst, ptrdiff_t stride,
                                              const uint16_t *left,
                                              int upsample_left, int dy, int bd,
                                              int mrl_index) {
  __m256i dstvec[32], d[32];
  if (bd < 12) {
    highbd_dr_prediction_z1_64xN_avx2(8, (uint16_t *)dstvec, 64, left,
                                      upsample_left, dy, mrl_index);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_avx2(8, (uint16_t *)dstvec, 64, left,
                                            upsample_left, dy, mrl_index);
  }

  // At this point, each 4 entries of dstvec[] forms one full 64-pixel
  // row (pre transposition) / col (post transposition)
  for (int i = 0; i < 4; i++) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 4], &dstvec[i + 8], &dstvec[i + 12],
        &dstvec[i + 16], &dstvec[i + 20], &dstvec[i + 24], &dstvec[i + 28],
        &d[8 * i], &d[8 * i + 1], &d[8 * i + 2], &d[8 * i + 3], &d[8 * i + 4],
        &d[8 * i + 5], &d[8 * i + 6], &d[8 * i + 7]);
  }

  // Now each 8 elements of d contains 16 rows of 8 output pixels,
  // ordered like:
  // {0, 8}, {1, 9}, {2, 10}, ...
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 8; j++) {
      __m128i temp_lo = _mm256_castsi256_si128(d[8 * i + j]);
      __m128i temp_hi = _mm256_extracti128_si256(d[8 * i + j], 1);
      _mm_storeu_si128((__m128i *)(dst + (16 * i + j) * stride), temp_lo);
      _mm_storeu_si128((__m128i *)(dst + (16 * i + j + 8) * stride), temp_hi);
    }
  }
}
#endif  // CONFIG_FLEX_PARTITION

void av1_highbd_dr_prediction_z3_avx2(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int upsample_left,
                                      int dx, int dy, int bd, int mrl_index) {
  (void)above;
  (void)dx;

  assert(dx == 1);
  assert(dy > 0);

  if (bw == bh) {
    switch (bw) {
      case 4:
        highbd_dr_prediction_z3_4x4_avx2(dst, stride, left, upsample_left, dy,
                                         bd, mrl_index);
        break;
      case 8:
        highbd_dr_prediction_z3_8x8_avx2(dst, stride, left, upsample_left, dy,
                                         bd, mrl_index);
        break;
      case 16:
        highbd_dr_prediction_z3_16x16_avx2(dst, stride, left, upsample_left, dy,
                                           bd, mrl_index);
        break;
      case 32:
        highbd_dr_prediction_z3_32x32_avx2(dst, stride, left, upsample_left, dy,
                                           bd, mrl_index);
        break;
      case 64:
        highbd_dr_prediction_z3_64x64_avx2(dst, stride, left, upsample_left, dy,
                                           bd, mrl_index);
        break;
    }
  } else {
    if (bw < bh) {
      if (bw + bw == bh) {
        switch (bw) {
          case 4:
            highbd_dr_prediction_z3_4x8_avx2(dst, stride, left, upsample_left,
                                             dy, bd, mrl_index);
            break;
          case 8:
            highbd_dr_prediction_z3_8x16_avx2(dst, stride, left, upsample_left,
                                              dy, bd, mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_16x32_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
          case 32:
            highbd_dr_prediction_z3_32x64_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
        }
      } else {
        switch (bw) {
          case 4:
#if CONFIG_FLEX_PARTITION
            if (bh == 32)
              highbd_dr_prediction_z3_4x32_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else if (bh == 64)
              highbd_dr_prediction_z3_4x64_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_4x16_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            break;
          case 8:
#if CONFIG_FLEX_PARTITION
            if (bh == 64)
              highbd_dr_prediction_z3_8x64_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_8x32_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_16x64_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
        }
      }
    } else {
      if (bh + bh == bw) {
        switch (bh) {
          case 4:
            highbd_dr_prediction_z3_8x4_avx2(dst, stride, left, upsample_left,
                                             dy, bd, mrl_index);
            break;
          case 8:
            highbd_dr_prediction_z3_16x8_avx2(dst, stride, left, upsample_left,
                                              dy, bd, mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_32x16_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
          case 32:
            highbd_dr_prediction_z3_64x32_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
        }
      } else {
        switch (bh) {
          case 4:
#if CONFIG_FLEX_PARTITION
            if (bw == 64)
              highbd_dr_prediction_z3_64x4_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else if (bw == 32)
              highbd_dr_prediction_z3_32x4_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_16x4_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            break;
          case 8:
#if CONFIG_FLEX_PARTITION
            if (bw == 64)
              highbd_dr_prediction_z3_64x8_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_32x8_avx2(
                  dst, stride, left, upsample_left, dy, bd, mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_64x16_avx2(dst, stride, left, upsample_left,
                                               dy, bd, mrl_index);
            break;
        }
      }
    }
  }
  return;
}

#if CONFIG_IDIF

static INLINE __m256i highbd_clamp_epi16_avx2(__m256i u, int bd) {
  const __m256i zero = _mm256_setzero_si256();
  const int max_i = ((1 << bd) - 1) << POWER_DR_INTERP_FILTER;
  const __m256i max = _mm256_set1_epi16(max_i);
  __m256i t, clamped;

  t = _mm256_max_epi16(u, zero);
  clamped = _mm256_min_epi16(t, max);

  return clamped;
}

static INLINE __m256i highbd_clamp_epi32_avx2(__m256i u, int bd) {
  const __m256i zero = _mm256_setzero_si256();
  const int max_i = ((1 << bd) - 1) << POWER_DR_INTERP_FILTER;
  const __m256i max = _mm256_set1_epi32(max_i);
  __m256i t, clamped;

  t = _mm256_max_epi32(u, zero);
  clamped = _mm256_min_epi32(t, max);

  return clamped;
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_4xN_internal_idif_avx2(
    int N, __m128i *dst, const uint16_t *above, int dx, int mrl_index, int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((N + 4) - 1 + (mrl_index << 1));

  assert(dx > 0);
  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m128i a_mbase_x, max_base_x128, base_inc128, mask128;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm_set1_epi16(above[max_base_x]);
  max_base_x128 = _mm_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m128i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = a_mbase_x;  // save 4 values
      }
      return;
    }

    // load refs
    a0 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base - 1)));
    a1 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base)));
    a2 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base + 1)));
    a3 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base + 2)));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

    // multiply and sum
    val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0, f0),
                             _mm256_mullo_epi16(a1, f1));
    val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2, f2),
                             _mm256_mullo_epi16(a3, f3));
    val0 = _mm256_adds_epi16(val0, val1);

    val0 = highbd_clamp_epi16_avx2(val0, bd);
    val0 = _mm256_adds_epi16(val0, rnding);
    val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

    // discard values
    res1 = _mm256_castsi256_si128(val0);
    base_inc128 = _mm_setr_epi16(base, base + 1, base + 2, base + 3, base + 4,
                                 base + 5, base + 6, base + 7);
    mask128 = _mm_cmpgt_epi16(max_base_x128, base_inc128);
    dst[r] = _mm_blendv_epi8(a_mbase_x, res1, mask128);
    x += dx;
  }
}

static AOM_FORCE_INLINE void
highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(int N, __m128i *dst,
                                                     const uint16_t *above,
                                                     int dx, int mrl_index,
                                                     int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((N + 4) - 1 + (mrl_index << 1));

  assert(dx > 0);
  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m128i a_mbase_x, max_base_x128, base_inc128, mask128;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm_set1_epi16(above[max_base_x]);
  max_base_x128 = _mm_set1_epi32(max_base_x);

  int x = dx * (1 + mrl_index);
  int shift_i;
  for (int r = 0; r < N; r++) {
    __m128i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = a_mbase_x;  // save 4 values
      }
      return;
    }

    // load refs
    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base - 1)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a2 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));
    a3 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 2)));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

    // multiply and sum
    val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                            _mm256_mullo_epi32(a1, f1));
    val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                            _mm256_mullo_epi32(a3, f3));
    val0 = _mm256_add_epi32(val0, val1);

    // round shift
    val0 = highbd_clamp_epi32_avx2(val0, bd);
    val0 = _mm256_add_epi32(val0, rnding);
    val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

    // discard values
    res1 = _mm256_castsi256_si128(val0);
    res1 = _mm_packus_epi32(res1, res1);

    base_inc128 = _mm_setr_epi32(base, base + 1, base + 2, base + 3);
    mask128 = _mm_cmpgt_epi32(max_base_x128, base_inc128);
    mask128 = _mm_packs_epi32(mask128, mask128);  // goto 16 bit
    dst[r] = _mm_blendv_epi8(a_mbase_x, res1, mask128);
    x += dx;
  }
}

static void highbd_dr_prediction_z1_4xN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int bd, int mrl_index) {
  (void)dy;
  (void)left;
  (void)bw;
  assert(bw == 4);
  int N = bh;

#if CONFIG_FLEX_PARTITION
  assert(bh <= 64);
  __m128i dstvec[64];
#else
  assert(bh <= 16);
  __m128i dstvec[16];
#endif  // CONFIG_FLEX_PARTITION

  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(N, dstvec, above, dx,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(N, dstvec, above, dx,
                                                         mrl_index, bd);
  }
  for (int i = 0; i < N; i++) {
    _mm_storel_epi64((__m128i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_8xN_internal_idif_avx2(
    int N, __m128i *dst, const uint16_t *above, int dx, int mrl_index, int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((N + 8) - 1 + (mrl_index << 1));

  assert(dx > 0);
  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = _mm256_castsi256_si128(a_mbase_x);  // save 8 values
      }
      return;
    }

    // load refs
    a0 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base - 1)));
    a1 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base)));
    a2 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base + 1)));
    a3 = _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)(above + base + 2)));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

    val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0, f0),
                             _mm256_mullo_epi16(a1, f1));
    val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2, f2),
                             _mm256_mullo_epi16(a3, f3));
    val0 = _mm256_adds_epi16(val0, val1);

    // round-shift
    val0 = highbd_clamp_epi16_avx2(val0, bd);
    val0 = _mm256_adds_epi16(val0, rnding);
    val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

    base_inc256 =
        _mm256_setr_epi16(base, base + 1, base + 2, base + 3, base + 4,
                          base + 5, base + 6, base + 7, 0, 0, 0, 0, 0, 0, 0, 0);

    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    res1 = _mm256_blendv_epi8(a_mbase_x, val0, mask256);
    dst[r] = _mm256_castsi256_si128(res1);
    x += dx;
  }
}

static AOM_FORCE_INLINE void
highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(int N, __m128i *dst,
                                                     const uint16_t *above,
                                                     int dx, int mrl_index,
                                                     int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((N + 8) - 1 + (mrl_index << 1));

  assert(dx > 0);
  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi32(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dst[i] = _mm256_castsi256_si128(a_mbase_x);  // save 8 values
      }
      return;
    }

    // load refs
    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base - 1)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a2 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));
    a3 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 2)));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

    // multiply and sum
    val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                            _mm256_mullo_epi32(a1, f1));
    val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                            _mm256_mullo_epi32(a3, f3));
    val0 = _mm256_add_epi32(val0, val1);

    // round shift
    val0 = highbd_clamp_epi32_avx2(val0, bd);
    val0 = _mm256_add_epi32(val0, rnding);
    val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

    res1 = _mm256_packus_epi32(
        val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));

    base_inc256 = _mm256_setr_epi32(base, base + 1, base + 2, base + 3,
                                    base + 4, base + 5, base + 6, base + 7);

    mask256 = _mm256_cmpgt_epi32(max_base_x256, base_inc256);
    mask256 = _mm256_packs_epi32(
        mask256, _mm256_castsi128_si256(
                     _mm256_extracti128_si256(mask256, 1)));  // go to 16 bit
    res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
    dst[r] = _mm256_castsi256_si128(res1);
    x += dx;
  }
}

void highbd_dr_prediction_z1_8xN_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)left;
  (void)dy;
  (void)bw;
  assert(bw == 8);
  int N = bh;

#if CONFIG_FLEX_PARTITION
  assert(bh <= 64);
  __m128i dstvec[64];
#else
  assert(bh <= 32);
  __m128i dstvec[32];
#endif  // CONFIG_FLEX_PARTITION

  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(N, dstvec, above, dx,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(N, dstvec, above, dx,
                                                         mrl_index, bd);
  }
  for (int i = 0; i < N; i++) {
    _mm_storeu_si128((__m128i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_16xN_internal_idif_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int dx, int mrl_index,
    int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((16 + N) - 1 + (mrl_index << 1));

  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 16 values
      }
      return;
    }

    // load refs
    a0 = _mm256_loadu_si256((__m256i *)(above + base - 1));
    a1 = _mm256_loadu_si256((__m256i *)(above + base));
    a2 = _mm256_loadu_si256((__m256i *)(above + base + 1));
    a3 = _mm256_loadu_si256((__m256i *)(above + base + 2));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

    val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0, f0),
                             _mm256_mullo_epi16(a1, f1));
    val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2, f2),
                             _mm256_mullo_epi16(a3, f3));
    val0 = _mm256_adds_epi16(val0, val1);

    // clamp and round-shift
    val0 = highbd_clamp_epi16_avx2(val0, bd);
    val0 = _mm256_adds_epi16(val0, rnding);
    val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

    base_inc256 = _mm256_setr_epi16(base, base + 1, base + 2, base + 3,
                                    base + 4, base + 5, base + 6, base + 7,
                                    base + 8, base + 9, base + 10, base + 11,
                                    base + 12, base + 13, base + 14, base + 15);

    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    dstvec[r] = _mm256_blendv_epi8(a_mbase_x, val0, mask256);
    x += dx;
  }
}

static AOM_FORCE_INLINE void
highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(int N, __m256i *dstvec,
                                                      const uint16_t *above,
                                                      int dx, int mrl_index,
                                                      int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((16 + N) - 1 + (mrl_index << 1));
  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i f0, f1, f2, f3;
  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 16 values
      }
      return;
    }

    a0 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base - 1)));
    a1 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base)));
    a2 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 1)));
    a3 = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 2)));

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

    // multiply and sum
    val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                            _mm256_mullo_epi32(a1, f1));
    val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                            _mm256_mullo_epi32(a3, f3));
    val0 = _mm256_add_epi32(val0, val1);

    // round shift
    val0 = highbd_clamp_epi32_avx2(val0, bd);
    val0 = _mm256_add_epi32(val0, rnding);
    val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

    res[0] = _mm256_packus_epi32(
        val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));

    int mdif = max_base_x - base;
    if (mdif > 8) {
      a0 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 7)));
      a1 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 8)));
      a2 =
          _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i *)(above + base + 9)));
      a3 = _mm256_cvtepu16_epi32(
          _mm_loadu_si128((__m128i *)(above + base + 10)));

      // multiply and sum
      val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                              _mm256_mullo_epi32(a1, f1));
      val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                              _mm256_mullo_epi32(a3, f3));
      val0 = _mm256_add_epi32(val0, val1);

      // round shift
      val0 = highbd_clamp_epi32_avx2(val0, bd);
      val0 = _mm256_add_epi32(val0, rnding);
      val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

      res[1] = _mm256_packus_epi32(
          val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));
    } else {
      res[1] = a_mbase_x;
    }
    res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                   1);  // 16 16bit values

    base_inc256 = _mm256_setr_epi16(base, base + 1, base + 2, base + 3,
                                    base + 4, base + 5, base + 6, base + 7,
                                    base + 8, base + 9, base + 10, base + 11,
                                    base + 12, base + 13, base + 14, base + 15);
    mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
    dstvec[r] = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
    x += dx;
  }
}

static void highbd_dr_prediction_z1_16xN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int bd, int mrl_index) {
  (void)left;
  (void)dy;
  (void)bw;
  assert(bw == 16);
  int N = bh;
  __m256i dstvec[64];
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(N, dstvec, above, dx,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(N, dstvec, above, dx,
                                                          mrl_index, bd);
  }
  for (int i = 0; i < N; i++) {
    _mm256_storeu_si256((__m256i *)(dst + stride * i), dstvec[i]);
  }
}

static AOM_FORCE_INLINE void highbd_dr_prediction_z1_32xN_internal_idif_avx2(
    int N, __m256i *dstvec, const uint16_t *above, int dx, int mrl_index,
    int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((32 + N) - 1 + (mrl_index << 1));

  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i res;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 32 values
        dstvec[i + N] = a_mbase_x;
      }
      return;
    }

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

    for (int j = 0; j < 32; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        res = a_mbase_x;
      } else {
        // load refs
        a0 = _mm256_loadu_si256((__m256i *)(above + base - 1 + j));
        a1 = _mm256_loadu_si256((__m256i *)(above + base + j));
        a2 = _mm256_loadu_si256((__m256i *)(above + base + 1 + j));
        a3 = _mm256_loadu_si256((__m256i *)(above + base + 2 + j));

        val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0, f0),
                                 _mm256_mullo_epi16(a1, f1));
        val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2, f2),
                                 _mm256_mullo_epi16(a3, f3));
        val0 = _mm256_adds_epi16(val0, val1);

        // clamp and round-shift
        val0 = highbd_clamp_epi16_avx2(val0, bd);
        val0 = _mm256_adds_epi16(val0, rnding);
        val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res = _mm256_blendv_epi8(a_mbase_x, val0, mask256);
      }
      if (!j) {
        dstvec[r] = res;
      } else {
        dstvec[r + N] = res;
      }
    }
    x += dx;
  }
}

static AOM_FORCE_INLINE void
highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(int N, __m256i *dstvec,
                                                      const uint16_t *above,
                                                      int dx, int mrl_index,
                                                      int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((32 + N) - 1 + (mrl_index << 1));

  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++) {
    __m256i res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        dstvec[i] = a_mbase_x;  // save 32 values
        dstvec[i + N] = a_mbase_x;
      }
      return;
    }

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

    for (int j = 0; j < 32; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        res1 = a_mbase_x;
      } else {
        a0 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base - 1 + j)));
        a1 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + j)));
        a2 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + 1 + j)));
        a3 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + 2 + j)));

        // multiply and sum
        val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                                _mm256_mullo_epi32(a1, f1));
        val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                                _mm256_mullo_epi32(a3, f3));
        val0 = _mm256_add_epi32(val0, val1);

        // round shift
        val0 = highbd_clamp_epi32_avx2(val0, bd);
        val0 = _mm256_add_epi32(val0, rnding);
        val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

        res[0] = _mm256_packus_epi32(
            val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));

        if (mdif > 8) {
          a0 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 7 + j)));
          a1 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 8 + j)));
          a2 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 9 + j)));
          a3 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 10 + j)));

          // multiply and sum
          val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                                  _mm256_mullo_epi32(a1, f1));
          val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                                  _mm256_mullo_epi32(a3, f3));
          val0 = _mm256_add_epi32(val0, val1);

          // round shift
          val0 = highbd_clamp_epi32_avx2(val0, bd);
          val0 = _mm256_add_epi32(val0, rnding);
          val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

          res[1] = _mm256_packus_epi32(
              val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));
        } else {
          res[1] = a_mbase_x;
        }
        res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                       1);  // 16 16bit values
        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
      }
      if (!j) {
        dstvec[r] = res1;
      } else {
        dstvec[r + N] = res1;
      }
    }
    x += dx;
  }
}

static void highbd_dr_prediction_z1_32xN_idif_avx2(int N, uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *above,
                                                   int dx, int bd,
                                                   int mrl_index) {
  __m256i dstvec[128];
  if (bd < 10) {
    highbd_dr_prediction_z1_32xN_internal_idif_avx2(N, dstvec, above, dx,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(N, dstvec, above, dx,
                                                          mrl_index, bd);
  }
  for (int i = 0; i < N; i++) {
    _mm256_storeu_si256((__m256i *)(dst + stride * i), dstvec[i]);
    _mm256_storeu_si256((__m256i *)(dst + stride * i + 16), dstvec[i + N]);
  }
}

static void highbd_dr_prediction_z1_64xN_internal_idif_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above, int dx,
    int mrl_index, int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((64 + N) - 1 + (mrl_index << 1));

  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i a0, a1, a2, a3;
  __m256i val0, val1;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++, dst += stride) {
    __m256i res;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        _mm256_storeu_si256((__m256i *)dst, a_mbase_x);  // save 32 values
        _mm256_storeu_si256((__m256i *)(dst + 16), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 32), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 48), a_mbase_x);
        dst += stride;
      }
      return;
    }

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

    for (int j = 0; j < 64; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        _mm256_storeu_si256((__m256i *)(dst + j), a_mbase_x);
      } else {
        // load refs
        a0 = _mm256_loadu_si256((__m256i *)(above + base - 1 + j));
        a1 = _mm256_loadu_si256((__m256i *)(above + base + j));
        a2 = _mm256_loadu_si256((__m256i *)(above + base + 1 + j));
        a3 = _mm256_loadu_si256((__m256i *)(above + base + 2 + j));

        val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0, f0),
                                 _mm256_mullo_epi16(a1, f1));
        val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2, f2),
                                 _mm256_mullo_epi16(a3, f3));
        val0 = _mm256_adds_epi16(val0, val1);

        // clamp and round-shift
        val0 = highbd_clamp_epi16_avx2(val0, bd);
        val0 = _mm256_adds_epi16(val0, rnding);
        val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res = _mm256_blendv_epi8(a_mbase_x, val0, mask256);
        _mm256_storeu_si256((__m256i *)(dst + j), res);  // 16 16bit values
      }
    }
    x += dx;
  }
}

static void highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(
    int N, uint16_t *dst, ptrdiff_t stride, const uint16_t *above, int dx,
    int mrl_index, int bd) {
  const int frac_bits = 6;
  const int max_base_x = ((64 + N) - 1 + (mrl_index << 1));

  __m256i a0, a1, a2, a3;

  __m256i a_mbase_x, max_base_x256, base_inc256, mask256;

  __m256i val0, val1;
  __m256i f0, f1, f2, f3;

  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));

  a_mbase_x = _mm256_set1_epi16(above[max_base_x]);
  max_base_x256 = _mm256_set1_epi16(max_base_x);

  int shift_i;
  int x = dx * (1 + mrl_index);
  for (int r = 0; r < N; r++, dst += stride) {
    __m256i res[2], res1;

    int base = x >> frac_bits;
    if (base >= max_base_x) {
      for (int i = r; i < N; ++i) {
        _mm256_storeu_si256((__m256i *)dst, a_mbase_x);  // save 32 values
        _mm256_storeu_si256((__m256i *)(dst + 16), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 32), a_mbase_x);
        _mm256_storeu_si256((__m256i *)(dst + 48), a_mbase_x);
        dst += stride;
      }
      return;
    }

    // load filter
    shift_i = (x & 0x3F) >> 1;
    f0 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
    f1 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
    f2 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
    f3 = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

    for (int j = 0; j < 64; j += 16) {
      int mdif = max_base_x - (base + j);
      if (mdif <= 0) {
        _mm256_storeu_si256((__m256i *)(dst + j), a_mbase_x);
      } else {
        a0 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base - 1 + j)));
        a1 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + j)));
        a2 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + 1 + j)));
        a3 = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)(above + base + 2 + j)));

        // multiply and sum
        val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                                _mm256_mullo_epi32(a1, f1));
        val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                                _mm256_mullo_epi32(a3, f3));
        val0 = _mm256_add_epi32(val0, val1);

        // round shift
        val0 = highbd_clamp_epi32_avx2(val0, bd);
        val0 = _mm256_add_epi32(val0, rnding);
        val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

        res[0] = _mm256_packus_epi32(
            val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));

        if (mdif > 8) {
          a0 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 7 + j)));
          a1 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 8 + j)));
          a2 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 9 + j)));
          a3 = _mm256_cvtepu16_epi32(
              _mm_loadu_si128((__m128i *)(above + base + 10 + j)));

          // multiply and sum
          val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0, f0),
                                  _mm256_mullo_epi32(a1, f1));
          val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2, f2),
                                  _mm256_mullo_epi32(a3, f3));
          val0 = _mm256_add_epi32(val0, val1);

          // round shift
          val0 = highbd_clamp_epi32_avx2(val0, bd);
          val0 = _mm256_add_epi32(val0, rnding);
          val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

          res[1] = _mm256_packus_epi32(
              val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));
        } else {
          res[1] = a_mbase_x;
        }
        res1 = _mm256_inserti128_si256(res[0], _mm256_castsi256_si128(res[1]),
                                       1);  // 16 16bit values
        base_inc256 = _mm256_setr_epi16(
            base + j, base + j + 1, base + j + 2, base + j + 3, base + j + 4,
            base + j + 5, base + j + 6, base + j + 7, base + j + 8,
            base + j + 9, base + j + 10, base + j + 11, base + j + 12,
            base + j + 13, base + j + 14, base + j + 15);

        mask256 = _mm256_cmpgt_epi16(max_base_x256, base_inc256);
        res1 = _mm256_blendv_epi8(a_mbase_x, res1, mask256);
        _mm256_storeu_si256((__m256i *)(dst + j), res1);
      }
    }
    x += dx;
  }
}

static void highbd_dr_prediction_z1_64xN_idif_avx2(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int bd, int mrl_index) {
  (void)left;
  (void)dy;
  (void)bw;
  assert(bw == 64);
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(bh, dst, stride, above, dx,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(
        bh, dst, stride, above, dx, mrl_index, bd);
  }
}

void av1_highbd_dr_prediction_z1_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  switch (bw) {
    case 4:
      highbd_dr_prediction_z1_4xN_idif_avx2(dst, stride, bw, bh, above, left,
                                            dx, dy, bd, mrl_index);
      break;
    case 8:
      highbd_dr_prediction_z1_8xN_idif_avx2(dst, stride, bw, bh, above, left,
                                            dx, dy, bd, mrl_index);
      break;
    case 16:
      highbd_dr_prediction_z1_16xN_idif_avx2(dst, stride, bw, bh, above, left,
                                             dx, dy, bd, mrl_index);
      break;
    case 32:
      highbd_dr_prediction_z1_32xN_idif_avx2(bh, dst, stride, above, dx, bd,
                                             mrl_index);
      break;
    case 64:
      highbd_dr_prediction_z1_64xN_idif_avx2(dst, stride, bw, bh, above, left,
                                             dx, dy, bd, mrl_index);
      break;
    default: break;
  }
  return;
}

static AOM_FORCE_INLINE __m256i highbd_dr_row8_idif_avx2(const uint16_t *above,
                                                         const __m256i *filter,
                                                         int base_x,
                                                         int base_shift,
                                                         int bd) {
  // load refs
  __m128i a0_x128, a1_x128, a2_x128, a3_x128;
  a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift - 1));
  a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
  a2_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 1));
  a3_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 2));

  // load mask
  a0_x128 = _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a1_x128 = _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a2_x128 = _mm_shuffle_epi8(a2_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a3_x128 = _mm_shuffle_epi8(a3_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

  __m256i a0_x, a1_x, a2_x, a3_x;
  a0_x = _mm256_castsi128_si256(a0_x128);
  a1_x = _mm256_castsi128_si256(a1_x128);
  a2_x = _mm256_castsi128_si256(a2_x128);
  a3_x = _mm256_castsi128_si256(a3_x128);

  // multiply and sum
  __m256i val0, val1;
  val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0_x, filter[0]),
                           _mm256_mullo_epi16(a1_x, filter[1]));
  val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2_x, filter[2]),
                           _mm256_mullo_epi16(a3_x, filter[3]));
  val0 = _mm256_adds_epi16(val0, val1);

  // round shift
  val0 = highbd_clamp_epi16_avx2(val0, bd);
  const __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));
  val0 = _mm256_adds_epi16(val0, rnding);
  val0 = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);

  return val0;
}

static AOM_FORCE_INLINE __m256i
highbd_dr_row8_32bit_idif_avx2(const uint16_t *above, const __m256i *filter,
                               int base_x, int base_shift, int bd) {
  // load refs
  __m128i a0_x128, a1_x128, a2_x128, a3_x128;
  a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift - 1));
  a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
  a2_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 1));
  a3_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 2));

  // load mask
  a0_x128 = _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a1_x128 = _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a2_x128 = _mm_shuffle_epi8(a2_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
  a3_x128 = _mm_shuffle_epi8(a3_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

  __m256i a0_x, a1_x, a2_x, a3_x;
  a0_x = _mm256_cvtepu16_epi32(a0_x128);
  a1_x = _mm256_cvtepu16_epi32(a1_x128);
  a2_x = _mm256_cvtepu16_epi32(a2_x128);
  a3_x = _mm256_cvtepu16_epi32(a3_x128);

  // multiply and sum
  __m256i val0, val1;
  val0 = _mm256_add_epi32(_mm256_mullo_epi32(a0_x, filter[0]),
                          _mm256_mullo_epi32(a1_x, filter[1]));
  val1 = _mm256_add_epi32(_mm256_mullo_epi32(a2_x, filter[2]),
                          _mm256_mullo_epi32(a3_x, filter[3]));
  val0 = _mm256_add_epi32(val0, val1);

  // round shift
  val0 = highbd_clamp_epi32_avx2(val0, bd);
  __m256i rnding = _mm256_set1_epi32(1 << (POWER_DR_INTERP_FILTER - 1));
  val0 = _mm256_add_epi32(val0, rnding);
  val0 = _mm256_srli_epi32(val0, POWER_DR_INTERP_FILTER);

  __m256i resx = _mm256_packus_epi32(
      val0, _mm256_castsi128_si256(_mm256_extracti128_si256(val0, 1)));
  return resx;
}

static INLINE void highbd_dr_z2_8x8_idif_avx2(int H, int W,
                                              const uint16_t *above,
                                              __m128i *dest, int r, int j,
                                              int dx, int mrl_index, int bd) {
  const int min_base_x = -((1 + mrl_index));
  const int frac_bits_x = 6;

  __m256i res;
  __m128i resx;
  int min_h = (H == 4) ? 4 : 8;
  int min_w = (W == 4) ? 4 : 8;

  for (int i = r; i < r + min_h; i++) {
    assert(i < H);
    assert(j < W);

    int y = i + 1;
    int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1);
    }

    if (base_shift > min_w - 1) {
      resx = _mm_setzero_si128();
    } else {
      // load filter
      int shift_i = ((-(y + mrl_index) * dx) & 0x3F) >> 1;
      __m256i f[4];
      f[0] = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][0]);
      f[1] = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][1]);
      f[2] = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][2]);
      f[3] = _mm256_set1_epi16(av1_dr_interp_filter[shift_i][3]);

      res = highbd_dr_row8_idif_avx2(above, f, base_x, base_shift, bd);
      resx = _mm256_castsi256_si128(res);
    }
    dest[i - r] = resx;
  }
}

static INLINE void highbd_dr_z2_32bit_8x8_idif_avx2(int H, int W,
                                                    const uint16_t *above,
                                                    __m128i *dest, int r, int j,
                                                    int dx, int mrl_index,
                                                    int bd) {
  const int min_base_x = -((1 + mrl_index));
  const int frac_bits_x = 6;

  __m256i res;
  __m128i resx;
  // adapt if size is 4
  int min_h = (H == 4) ? 4 : 8;
  int min_w = (W == 4) ? 4 : 8;

  for (int i = r; i < r + min_h; i++) {
    assert(i < H);
    assert(j < W);

    int y = i + 1;
    int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if (base_x < (min_base_x - 1)) {
      base_shift = (min_base_x - base_x - 1);
    }

    if (base_shift > min_w - 1) {
      resx = _mm_setzero_si128();
    } else {
      // load filter
      int shift_i = ((-(y + mrl_index) * dx) & 0x3F) >> 1;
      __m256i f[4];
      f[0] = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][0]);
      f[1] = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][1]);
      f[2] = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][2]);
      f[3] = _mm256_set1_epi32(av1_dr_interp_filter[shift_i][3]);

      res = highbd_dr_row8_32bit_idif_avx2(above, f, base_x, base_shift, bd);
      resx = _mm256_castsi256_si128(res);
    }
    dest[i - r] = resx;
  }
}

static void highbd_dr_32bit_z2_8x8_tiling_idif_avx2(
    int H, int W, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int mrl_index, int bd) {
  // Directional prediction in a 8x8 tile.
  // Sizes of 4x4, 4x8 and 8x4 are supported as well.
  // Step 1. Predict from above.
  // Step 2. Predict from left and transpose.
  // Step 3. Merge results.

  const int min_base_x = -((1 + mrl_index));
  const int frac_bits_x = 6;

  __m128i x_pred[8];
  __m128i y_pred[8];
  __m128i _y_pred[8];

  for (int i = 0; i < 8; i++) {
    x_pred[i] = _mm_setzero_si128();
    y_pred[i] = _mm_setzero_si128();
    _y_pred[i] = _mm_setzero_si128();
  }

  int min_h = (H == 4) ? 4 : 8;
  int min_w = (W == 4) ? 4 : 8;

  for (int r = 0; r < H; r += 8) {
    for (int j = 0; j < W; j += min_w) {
      assert((W - j) >= min_w);
      assert((H - r) >= min_h);

      if (bd < 10) {
        highbd_dr_z2_8x8_idif_avx2(H, W, above, x_pred, r, j, dx, mrl_index,
                                   bd);
        highbd_dr_z2_8x8_idif_avx2(W, H, left, _y_pred, j, r, dy, mrl_index,
                                   bd);
      } else {
        highbd_dr_z2_32bit_8x8_idif_avx2(H, W, above, x_pred, r, j, dx,
                                         mrl_index, bd);
        highbd_dr_z2_32bit_8x8_idif_avx2(W, H, left, _y_pred, j, r, dy,
                                         mrl_index, bd);
      }
      highbd_transpose8x8_sse2(&_y_pred[0], &_y_pred[1], &_y_pred[2],
                               &_y_pred[3], &_y_pred[4], &_y_pred[5],
                               &_y_pred[6], &_y_pred[7], &y_pred[0], &y_pred[1],
                               &y_pred[2], &y_pred[3], &y_pred[4], &y_pred[5],
                               &y_pred[6], &y_pred[7]);

      for (int k = 0; k < min_h; ++k) {
        int y = r + k + 1;
        int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
        int base_min_diff = (min_base_x - base_x);
        if (base_min_diff > min_w) {
          base_min_diff = min_w;
        } else {
          if (base_min_diff < 0) base_min_diff = 0;
        }

        __m128i resx, resy, resxy;
        resx = x_pred[k];
        resy = y_pred[k];

        resxy = _mm_blendv_epi8(resx, resy,
                                *(__m128i *)HighbdBaseMask[base_min_diff]);

        if (min_w == 8) {
          _mm_storeu_si128((__m128i *)(dst + k * stride + j), resxy);
        } else {
          _mm_storel_epi64((__m128i *)(dst + k * stride + j), resxy);
        }
      }
    }
    if (r + 8 < H) dst += 8 * stride;
  }
}

static void highbd_dr_z2_16x16_idif_avx2(int H, int W, const uint16_t *above,
                                         __m256i *dest, int r, int j, int dx,
                                         int mrl_index, int bd) {
  (void)H;
  (void)W;

  const int min_base_x = -(1 + mrl_index);
  const int frac_bits_x = 6;

  __m128i a0_x128, a1_x128, a2_x128, a3_x128;
  __m256i a0_x, a1_x, a2_x, a3_x;
  __m256i f0_x, f1_x, f2_x, f3_x;
  __m256i rnding = _mm256_set1_epi16(1 << (POWER_DR_INTERP_FILTER - 1));
  __m256i val0, val1;

  for (int i = r; i < r + 16; ++i) {
    assert(i < H);
    assert(j < W);
    int y = i + 1;

    int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if ((base_x) < (min_base_x - 1)) {
      base_shift = (min_base_x - (base_x)-1);
    }

    if (base_shift < 8) {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift - 1));
      a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift));
      a2_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 1));
      a3_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift + 2));

      a0_x128 =
          _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
      a1_x128 =
          _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
      a2_x128 =
          _mm_shuffle_epi8(a2_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);
      a3_x128 =
          _mm_shuffle_epi8(a3_x128, *(__m128i *)HighbdLoadMaskx[base_shift]);

      a0_x = _mm256_castsi128_si256(a0_x128);
      a1_x = _mm256_castsi128_si256(a1_x128);
      a2_x = _mm256_castsi128_si256(a2_x128);
      a3_x = _mm256_castsi128_si256(a3_x128);
    } else {
      a0_x = _mm256_setzero_si256();
      a1_x = _mm256_setzero_si256();
      a2_x = _mm256_setzero_si256();
      a3_x = _mm256_setzero_si256();
    }

    int base_shift1 = 0;
    if (base_shift > 8) {
      base_shift1 = base_shift - 8;
    }
    if (base_shift1 < 8) {
      a0_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 7));
      a1_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 8));
      a2_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 9));
      a3_x128 = _mm_loadu_si128((__m128i *)(above + base_x + base_shift1 + 10));

      a0_x128 =
          _mm_shuffle_epi8(a0_x128, *(__m128i *)HighbdLoadMaskx[base_shift1]);
      a1_x128 =
          _mm_shuffle_epi8(a1_x128, *(__m128i *)HighbdLoadMaskx[base_shift1]);
      a2_x128 =
          _mm_shuffle_epi8(a2_x128, *(__m128i *)HighbdLoadMaskx[base_shift1]);
      a3_x128 =
          _mm_shuffle_epi8(a3_x128, *(__m128i *)HighbdLoadMaskx[base_shift1]);

      a0_x = _mm256_inserti128_si256(a0_x, a0_x128, 1);
      a1_x = _mm256_inserti128_si256(a1_x, a1_x128, 1);
      a2_x = _mm256_inserti128_si256(a2_x, a2_x128, 1);
      a3_x = _mm256_inserti128_si256(a3_x, a3_x128, 1);
    }
    if ((base_shift < 8) || base_shift1 < 8) {
      // load filter
      int shift_x = ((-(i + 1 + mrl_index) * dx) & 0x3F) >> 1;
      f0_x = _mm256_set1_epi16(av1_dr_interp_filter[shift_x][0]);
      f1_x = _mm256_set1_epi16(av1_dr_interp_filter[shift_x][1]);
      f2_x = _mm256_set1_epi16(av1_dr_interp_filter[shift_x][2]);
      f3_x = _mm256_set1_epi16(av1_dr_interp_filter[shift_x][3]);

      val0 = _mm256_adds_epi16(_mm256_mullo_epi16(a0_x, f0_x),
                               _mm256_mullo_epi16(a1_x, f1_x));
      val1 = _mm256_adds_epi16(_mm256_mullo_epi16(a2_x, f2_x),
                               _mm256_mullo_epi16(a3_x, f3_x));
      val0 = _mm256_adds_epi16(val0, val1);

      val0 = highbd_clamp_epi16_avx2(val0, bd);
      val0 = _mm256_adds_epi16(val0, rnding);
      dest[i - r] = _mm256_srli_epi16(val0, POWER_DR_INTERP_FILTER);
    } else {
      dest[i - r] = _mm256_setzero_si256();
    }
  }
}

static void highbd_dr_prediction_z2_HxW_idif_avx2(
    int H, int W, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int mrl_index, int bd) {
  // Directional prediction in 16x16 tiles.
  // Step 1. Predict from above.
  // Step 2. Predict from left and transpose.
  // Step 3. Merge results.

  const int min_base_x = -(1 + mrl_index);
  const int frac_bits_x = 6;

  __m256i x_pred[16];
  __m256i y_pred[16];

  for (int r = 0; r < H; r += 16) {
    for (int j = 0; j < W; j += 16) {
      assert((W - j) >= 16);
      assert((H - r) >= 16);
      // x calc
      highbd_dr_z2_16x16_idif_avx2(H, W, above, x_pred, r, j, dx, mrl_index,
                                   bd);

      // y calc
      highbd_dr_z2_16x16_idif_avx2(W, H, left, y_pred, j, r, dy, mrl_index, bd);
      highbd_transpose16x16_avx2(y_pred, y_pred);

      // merge results
      for (int k = 0; k < 16; ++k) {
        int y = k + r + 1;
        int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
        int base_min_diff = (min_base_x - base_x);
        if (base_min_diff > 16) {
          base_min_diff = 16;
        } else {
          if (base_min_diff < 0) base_min_diff = 0;
        }

        __m256i resx, resy, resxy;
        resx = x_pred[k];
        resy = y_pred[k];

        resxy = _mm256_blendv_epi8(resx, resy,
                                   *(__m256i *)HighbdBaseMask[base_min_diff]);
        _mm256_storeu_si256((__m256i *)(dst + k * stride + j), resxy);
      }
    }  // for j
    if (r + 16 < H) dst += 16 * stride;
  }
}

static void highbd_dr_z2_16x16_32bit_idif_avx2(int H, int W,
                                               const uint16_t *above,
                                               __m256i *dest, int r, int j,
                                               int dx, int mrl_index, int bd) {
  (void)H;
  (void)W;
  const int min_base_x = -(1 + mrl_index);
  const int frac_bits_x = 6;
  __m256i resx[2];

  for (int i = r; i < r + 16; ++i) {
    assert(i < H);
    assert(j < W);

    int y = i + 1;

    int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
    int base_shift = 0;
    if ((base_x) < (min_base_x - 1)) {
      base_shift = (min_base_x - (base_x)-1);
    }

    // load filter
    int shift_x = ((-(i + 1 + mrl_index) * dx) & 0x3F) >> 1;
    __m256i f[4];
    f[0] = _mm256_set1_epi32(av1_dr_interp_filter[shift_x][0]);
    f[1] = _mm256_set1_epi32(av1_dr_interp_filter[shift_x][1]);
    f[2] = _mm256_set1_epi32(av1_dr_interp_filter[shift_x][2]);
    f[3] = _mm256_set1_epi32(av1_dr_interp_filter[shift_x][3]);

    if (base_shift < 8) {
      resx[0] =
          highbd_dr_row8_32bit_idif_avx2(above, f, base_x, base_shift, bd);

    } else {
      resx[0] = _mm256_setzero_si256();
    }

    int base_shift1 = 0;
    if (base_shift > 8) {
      base_shift1 = base_shift - 8;
    }
    if (base_shift1 < 8) {
      resx[1] =
          highbd_dr_row8_32bit_idif_avx2(above, f, base_x + 8, base_shift1, bd);
    }
    if ((base_shift < 8) || base_shift1 < 8) {
      dest[i - r] =
          _mm256_inserti128_si256(resx[0], _mm256_castsi256_si128(resx[1]),
                                  1);  // 16 16bit values
    } else {
      dest[i - r] = _mm256_setzero_si256();
    }
  }
}

static void highbd_dr_prediction_32bit_z2_HxW_idif_avx2(
    int H, int W, uint16_t *dst, ptrdiff_t stride, const uint16_t *above,
    const uint16_t *left, int dx, int dy, int mrl_index, int bd) {
  // Directional prediction in 16x16 tiles.
  // Step 1. Predict from above.
  // Step 2. Predict from left and transpose.
  // Step 3. Merge results.

  const int min_base_x = -(1 + mrl_index);
  const int frac_bits_x = 6;

  __m256i x_pred[16];
  __m256i y_pred[16];

  for (int r = 0; r < H; r += 16) {
    for (int j = 0; j < W; j += 16) {
      assert((W - j) >= 16);
      assert((H - r) >= 16);

      // x calc
      highbd_dr_z2_16x16_32bit_idif_avx2(H, W, above, x_pred, r, j, dx,
                                         mrl_index, bd);

      // y calc
      highbd_dr_z2_16x16_32bit_idif_avx2(W, H, left, y_pred, j, r, dy,
                                         mrl_index, bd);
      highbd_transpose16x16_avx2(y_pred, y_pred);
      // merge results
      for (int k = 0; k < 16; ++k) {
        int y = k + r + 1;
        int base_x = ((j << 6) - (y + mrl_index) * dx) >> frac_bits_x;
        int base_min_diff = (min_base_x - base_x);
        if (base_min_diff > 16) {
          base_min_diff = 16;
        } else {
          if (base_min_diff < 0) base_min_diff = 0;
        }

        __m256i resx, resy, resxy;
        resx = x_pred[k];
        resy = y_pred[k];

        resxy = _mm256_blendv_epi8(resx, resy,
                                   *(__m256i *)HighbdBaseMask[base_min_diff]);
        _mm256_storeu_si256((__m256i *)(dst + k * stride + j), resxy);
      }
    }  // for j
    if (r + 16 < H) dst += 16 * stride;
  }
}

// Directional prediction, zone 2: 90 < angle < 180 using IDIF
void av1_highbd_dr_prediction_z2_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  assert(dx > 0);
  assert(dy > 0);
  switch (bw) {
    case 4:
      highbd_dr_32bit_z2_8x8_tiling_idif_avx2(bh, bw, dst, stride, above, left,
                                              dx, dy, mrl_index, bd);
      break;
    case 8:
      highbd_dr_32bit_z2_8x8_tiling_idif_avx2(bh, bw, dst, stride, above, left,
                                              dx, dy, mrl_index, bd);
      break;
    default:
      if (bh < 16) {
        highbd_dr_32bit_z2_8x8_tiling_idif_avx2(bh, bw, dst, stride, above,
                                                left, dx, dy, mrl_index, bd);
      } else {
        if (bd < 10) {
          highbd_dr_prediction_z2_HxW_idif_avx2(bh, bw, dst, stride, above,
                                                left, dx, dy, mrl_index, bd);
        } else {
          highbd_dr_prediction_32bit_z2_HxW_idif_avx2(
              bh, bw, dst, stride, above, left, dx, dy, mrl_index, bd);
        }
      }
      break;
  }
}

//  Directional prediction, zone 3 functions
static void highbd_dr_prediction_z3_4x4_idif_avx2(uint16_t *dst,
                                                  ptrdiff_t stride,
                                                  const uint16_t *left, int dy,
                                                  int bd, int mrl_index) {
  __m128i dstvec[4], d[4];
  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(4, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(4, dstvec, left, dy,
                                                         mrl_index, bd);
  }
  highbd_transpose4x8_8x4_low_sse2(&dstvec[0], &dstvec[1], &dstvec[2],
                                   &dstvec[3], &d[0], &d[1], &d[2], &d[3]);
  _mm_storel_epi64((__m128i *)(dst + 0 * stride), d[0]);
  _mm_storel_epi64((__m128i *)(dst + 1 * stride), d[1]);
  _mm_storel_epi64((__m128i *)(dst + 2 * stride), d[2]);
  _mm_storel_epi64((__m128i *)(dst + 3 * stride), d[3]);
  return;
}

static void highbd_dr_prediction_z3_8x8_idif_avx2(uint16_t *dst,
                                                  ptrdiff_t stride,
                                                  const uint16_t *left, int dy,
                                                  int bd, int mrl_index) {
  __m128i dstvec[8], d[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(8, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(8, dstvec, left, dy,
                                                         mrl_index, bd);
  }
  highbd_transpose8x8_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                           &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                           &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                           &d[7]);
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_16x16_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  __m256i dstvec[16], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(16, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(16, dstvec, left, dy,
                                                          mrl_index, bd);
  }

  highbd_transpose16x16_avx2(dstvec, d);

  for (int i = 0; i < 16; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_32x32_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  __m256i dstvec[64], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_32xN_internal_idif_avx2(32, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(32, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  highbd_transpose16x16_avx2(dstvec, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + j * stride), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 16, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + j * stride + 16), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 32, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + (j + 16) * stride), d[j]);
  }
  highbd_transpose16x16_avx2(dstvec + 48, d);
  for (int j = 0; j < 16; j++) {
    _mm256_storeu_si256((__m256i *)(dst + (j + 16) * stride + 16), d[j]);
  }
}

static void highbd_dr_prediction_z3_64x64_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[64 * 64]);
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(64, dstT, 64, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(64, dstT, 64, left,
                                                          dy, mrl_index, bd);
  }
  highbd_transpose(dstT, 64, dst, stride, 64, 64);
}

static void highbd_dr_prediction_z3_4x8_idif_avx2(uint16_t *dst,
                                                  ptrdiff_t stride,
                                                  const uint16_t *left, int dy,
                                                  int bd, int mrl_index) {
  __m128i dstvec[4], d[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(4, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(4, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  highbd_transpose4x8_8x4_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                               &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6],
                               &d[7]);
  for (int i = 0; i < 8; i++) {
    _mm_storel_epi64((__m128i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_8x4_idif_avx2(uint16_t *dst,
                                                  ptrdiff_t stride,
                                                  const uint16_t *left, int dy,
                                                  int bd, int mrl_index) {
  __m128i dstvec[8], d[4];
  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(8, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(8, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  highbd_transpose8x8_low_sse2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                               &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                               &d[0], &d[1], &d[2], &d[3]);
  _mm_storeu_si128((__m128i *)(dst + 0 * stride), d[0]);
  _mm_storeu_si128((__m128i *)(dst + 1 * stride), d[1]);
  _mm_storeu_si128((__m128i *)(dst + 2 * stride), d[2]);
  _mm_storeu_si128((__m128i *)(dst + 3 * stride), d[3]);
}

static void highbd_dr_prediction_z3_8x16_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[8], d[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(8, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(8, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  highbd_transpose8x16_16x8_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                                 &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                                 &d[0], &d[1], &d[2], &d[3], &d[4], &d[5],
                                 &d[6], &d[7]);

  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_extracti128_si256(d[i - 8], 1));
  }
}

static void highbd_dr_prediction_z3_16x8_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[16];
  __m256i dstvec256[8], d256[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(16, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(16, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  for (int i = 0; i < 8; i++) {
    dstvec256[i] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 0]), dstvec[i + 8], 0x1);
  }

  highbd_transpose8x16_16x8_avx2(
      &dstvec256[0], &dstvec256[1], &dstvec256[2], &dstvec256[3], &dstvec256[4],
      &dstvec256[5], &dstvec256[6], &dstvec256[7], &d256[0], &d256[1], &d256[2],
      &d256[3], &d256[4], &d256[5], &d256[6], &d256[7]);

  for (int i = 0; i < 8; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d256[i]);
  }
}

static void highbd_dr_prediction_z3_4x16_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[4], d[4], d1;
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(4, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(4, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  highbd_transpose4x16_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                            &d[0], &d[1], &d[2], &d[3]);
  for (int i = 0; i < 4; i++) {
    _mm_storel_epi64((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
    d1 = _mm256_bsrli_epi128(d[i], 8);
    _mm_storel_epi64((__m128i *)(dst + (i + 4) * stride),
                     _mm256_castsi256_si128(d1));
    _mm_storel_epi64((__m128i *)(dst + (i + 8) * stride),
                     _mm256_extracti128_si256(d[i], 1));
    _mm_storel_epi64((__m128i *)(dst + (i + 12) * stride),
                     _mm256_extracti128_si256(d1, 1));
  }
}

#if CONFIG_FLEX_PARTITION

static void highbd_dr_prediction_z3_64x8_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[64];
  __m256i dstvec256[32], d256[32];
  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(64, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(64, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  for (int i = 0; i < 8; i++) {
    dstvec256[i] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 0]), dstvec[i + 8], 0x1);
    dstvec256[i + 8] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 16]), dstvec[i + 24], 0x1);
    dstvec256[i + 16] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 32]), dstvec[i + 40], 0x1);
    dstvec256[i + 24] = _mm256_insertf128_si256(
        _mm256_castsi128_si256(dstvec[i + 48]), dstvec[i + 56], 0x1);
  }

  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec256[i + 0], &dstvec256[i + 1], &dstvec256[i + 2],
        &dstvec256[i + 3], &dstvec256[i + 4], &dstvec256[i + 5],
        &dstvec256[i + 6], &dstvec256[i + 7], &d256[i + 0], &d256[i + 1],
        &d256[i + 2], &d256[i + 3], &d256[i + 4], &d256[i + 5], &d256[i + 6],
        &d256[i + 7]);
  }
  for (int i = 0; i < 8; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d256[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d256[i + 8]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 32), d256[i + 16]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 48), d256[i + 24]);
  }
}

static void highbd_dr_prediction_z3_64x4_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[64];
  __m256i d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(64, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(64, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  highbd_transpose16x4_8x8_avx2(dstvec, d);
  highbd_transpose16x4_8x8_avx2(dstvec + 16, d + 4);
  highbd_transpose16x4_8x8_avx2(dstvec + 32, d + 8);
  highbd_transpose16x4_8x8_avx2(dstvec + 48, d + 12);

  for (int i = 0; i < 4; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d[i + 4]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 32), d[i + 8]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 48), d[i + 12]);
  }
}

static void highbd_dr_prediction_z3_4x64_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[16], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(4, (uint16_t *)dstvec, 64,
                                                    left, dy, mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(
        4, (uint16_t *)dstvec, 64, left, dy, mrl_index, bd);
  }

  for (int i = 0; i < 4; i++) {
    highbd_transpose4x16_avx2(&dstvec[i], &dstvec[i + 4], &dstvec[i + 8],
                              &dstvec[i + 12], &d[4 * i], &d[4 * i + 1],
                              &d[4 * i + 2], &d[4 * i + 3]);
  }

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      const __m128i temph = _mm256_extracti128_si256(d[4 * i + j], 1);
      const __m128i templ = _mm256_castsi256_si128(d[4 * i + j]);
      xx_storel_64(dst + (16 * i + j) * stride, templ);
      xx_storel_64(dst + (16 * i + j + 4) * stride, _mm_srli_si128(templ, 8));
      xx_storel_64(dst + (16 * i + j + 8) * stride, temph);
      xx_storel_64(dst + (16 * i + j + 12) * stride, _mm_srli_si128(temph, 8));
    }
  }
}

static void highbd_dr_prediction_z3_8x64_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[32], d[32];
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(8, (uint16_t *)dstvec, 64,
                                                    left, dy, mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(
        8, (uint16_t *)dstvec, 64, left, dy, mrl_index, bd);
  }

  for (int i = 0; i < 4; i++) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 4], &dstvec[i + 8], &dstvec[i + 12],
        &dstvec[i + 16], &dstvec[i + 20], &dstvec[i + 24], &dstvec[i + 28],
        &d[8 * i], &d[8 * i + 1], &d[8 * i + 2], &d[8 * i + 3], &d[8 * i + 4],
        &d[8 * i + 5], &d[8 * i + 6], &d[8 * i + 7]);
  }

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 8; j++) {
      __m128i temp_lo = _mm256_castsi256_si128(d[8 * i + j]);
      __m128i temp_hi = _mm256_extracti128_si256(d[8 * i + j], 1);
      _mm_storeu_si128((__m128i *)(dst + (16 * i + j) * stride), temp_lo);
      _mm_storeu_si128((__m128i *)(dst + (16 * i + j + 8) * stride), temp_hi);
    }
  }
}

static void highbd_dr_prediction_z3_4x32_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[8], d[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_32xN_internal_idif_avx2(4, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(4, dstvec, left, dy,
                                                          mrl_index, bd);
  }

  highbd_transpose8x16_16x8_avx2(&dstvec[0], &dstvec[1], &dstvec[2], &dstvec[3],
                                 &dstvec[4], &dstvec[5], &dstvec[6], &dstvec[7],
                                 &d[0], &d[1], &d[2], &d[3], &d[4], &d[5],
                                 &d[6], &d[7]);

  for (int i = 0; i < 8; i++) {
    __m128i temp_lo = _mm256_castsi256_si128(d[i]);
    __m128i temp_hi = _mm256_extracti128_si256(d[i], 1);
    _mm_storel_epi64((__m128i *)(dst + i * stride), temp_lo);
    _mm_storel_epi64((__m128i *)(dst + (i + 16) * stride),
                     _mm_srli_si128(temp_lo, 8));
    _mm_storel_epi64((__m128i *)(dst + (i + 8) * stride), temp_hi);
    _mm_storel_epi64((__m128i *)(dst + (i + 24) * stride),
                     _mm_srli_si128(temp_hi, 8));
  }
}

static void highbd_dr_prediction_z3_32x4_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[32];
  __m256i d[8];
  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(32, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(32, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  highbd_transpose16x4_8x8_avx2(dstvec, d);
  highbd_transpose16x4_8x8_avx2(dstvec + 16, d + 4);

  for (int i = 0; i < 4; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
    _mm256_storeu_si256((__m256i *)(dst + i * stride + 16), d[i + 4]);
  }
}

#endif  // CONFIG_FLEX_PARTITION

static void highbd_dr_prediction_z3_16x4_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[16];
  __m256i d[4];
  if (bd < 10) {
    highbd_dr_prediction_z1_4xN_internal_idif_avx2(16, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_4xN_internal_idif_avx2(16, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  highbd_transpose16x4_8x8_avx2(dstvec, d);

  for (int i = 0; i < 4; i++) {
    _mm256_storeu_si256((__m256i *)(dst + i * stride), d[i]);
  }
}

static void highbd_dr_prediction_z3_8x32_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m256i dstvec[16], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_32xN_internal_idif_avx2(8, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(8, dstvec, left, dy,
                                                          mrl_index, bd);
  }

  for (int i = 0; i < 16; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 1], &dstvec[i + 2], &dstvec[i + 3],
        &dstvec[i + 4], &dstvec[i + 5], &dstvec[i + 6], &dstvec[i + 7], &d[i],
        &d[i + 1], &d[i + 2], &d[i + 3], &d[i + 4], &d[i + 5], &d[i + 6],
        &d[i + 7]);
  }

  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 8) * stride),
                     _mm256_extracti128_si256(d[i], 1));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 8) * stride),
                     _mm256_castsi256_si128(d[i]));
  }
  for (int i = 8; i < 16; i++) {
    _mm_storeu_si128((__m128i *)(dst + (i + 16) * stride),
                     _mm256_extracti128_si256(d[i], 1));
  }
}

static void highbd_dr_prediction_z3_32x8_idif_avx2(uint16_t *dst,
                                                   ptrdiff_t stride,
                                                   const uint16_t *left, int dy,
                                                   int bd, int mrl_index) {
  __m128i dstvec[32], d[32];
  if (bd < 10) {
    highbd_dr_prediction_z1_8xN_internal_idif_avx2(32, dstvec, left, dy,
                                                   mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_8xN_internal_idif_avx2(32, dstvec, left, dy,
                                                         mrl_index, bd);
  }

  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x8_sse2(&dstvec[0 + i], &dstvec[1 + i], &dstvec[2 + i],
                             &dstvec[3 + i], &dstvec[4 + i], &dstvec[5 + i],
                             &dstvec[6 + i], &dstvec[7 + i], &d[0 + i],
                             &d[1 + i], &d[2 + i], &d[3 + i], &d[4 + i],
                             &d[5 + i], &d[6 + i], &d[7 + i]);
  }
  for (int i = 0; i < 8; i++) {
    _mm_storeu_si128((__m128i *)(dst + i * stride), d[i]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 8), d[i + 8]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 16), d[i + 16]);
    _mm_storeu_si128((__m128i *)(dst + i * stride + 24), d[i + 24]);
  }
}

static void highbd_dr_prediction_z3_16x32_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  __m256i dstvec[32], d[32];
  if (bd < 10) {
    highbd_dr_prediction_z1_32xN_internal_idif_avx2(16, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_32xN_internal_idif_avx2(16, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  for (int i = 0; i < 32; i += 8) {
    highbd_transpose8x16_16x8_avx2(
        &dstvec[i], &dstvec[i + 1], &dstvec[i + 2], &dstvec[i + 3],
        &dstvec[i + 4], &dstvec[i + 5], &dstvec[i + 6], &dstvec[i + 7], &d[i],
        &d[i + 1], &d[i + 2], &d[i + 3], &d[i + 4], &d[i + 5], &d[i + 6],
        &d[i + 7]);
  }
  // store
  for (int j = 0; j < 32; j += 16) {
    for (int i = 0; i < 8; i++) {
      _mm_storeu_si128((__m128i *)(dst + (i + j) * stride),
                       _mm256_castsi256_si128(d[(i + j)]));
    }
    for (int i = 0; i < 8; i++) {
      _mm_storeu_si128((__m128i *)(dst + (i + j) * stride + 8),
                       _mm256_castsi256_si128(d[(i + j) + 8]));
    }
    for (int i = 8; i < 16; i++) {
      _mm256_storeu_si256(
          (__m256i *)(dst + (i + j) * stride),
          _mm256_inserti128_si256(
              d[(i + j)], _mm256_extracti128_si256(d[(i + j) - 8], 1), 0));
    }
  }
}

static void highbd_dr_prediction_z3_32x16_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  __m256i dstvec[32], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(32, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(32, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  for (int i = 0; i < 32; i += 16) {
    highbd_transpose16x16_avx2((dstvec + i), d);
    for (int j = 0; j < 16; j++) {
      _mm256_storeu_si256((__m256i *)(dst + j * stride + i), d[j]);
    }
  }
}

static void highbd_dr_prediction_z3_32x64_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  uint16_t dstT[64 * 32];
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(32, dstT, 64, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(32, dstT, 64, left,
                                                          dy, mrl_index, bd);
  }
  highbd_transpose(dstT, 64, dst, stride, 32, 64);
}

static void highbd_dr_prediction_z3_64x32_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[32 * 64]);
  highbd_dr_prediction_z1_32xN_idif_avx2(64, dstT, 32, left, dy, bd, mrl_index);
  highbd_transpose(dstT, 32, dst, stride, 64, 32);
  return;
}

static void highbd_dr_prediction_z3_16x64_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  DECLARE_ALIGNED(16, uint16_t, dstT[64 * 16]);
  if (bd < 10) {
    highbd_dr_prediction_z1_64xN_internal_idif_avx2(16, dstT, 64, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_64xN_internal_idif_avx2(16, dstT, 64, left,
                                                          dy, mrl_index, bd);
  }
  highbd_transpose(dstT, 64, dst, stride, 16, 64);
}

static void highbd_dr_prediction_z3_64x16_idif_avx2(uint16_t *dst,
                                                    ptrdiff_t stride,
                                                    const uint16_t *left,
                                                    int dy, int bd,
                                                    int mrl_index) {
  __m256i dstvec[64], d[16];
  if (bd < 10) {
    highbd_dr_prediction_z1_16xN_internal_idif_avx2(64, dstvec, left, dy,
                                                    mrl_index, bd);
  } else {
    highbd_dr_prediction_32bit_z1_16xN_internal_idif_avx2(64, dstvec, left, dy,
                                                          mrl_index, bd);
  }
  for (int i = 0; i < 64; i += 16) {
    highbd_transpose16x16_avx2((dstvec + i), d);
    for (int j = 0; j < 16; j++) {
      _mm256_storeu_si256((__m256i *)(dst + j * stride + i), d[j]);
    }
  }
}

void av1_highbd_dr_prediction_z3_idif_avx2(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)above;
  (void)dx;

  assert(dx == 1);
  assert(dy > 0);

  if (bw == bh) {
    switch (bw) {
      case 4:
        highbd_dr_prediction_z3_4x4_idif_avx2(dst, stride, left, dy, bd,
                                              mrl_index);
        break;
      case 8:
        highbd_dr_prediction_z3_8x8_idif_avx2(dst, stride, left, dy, bd,
                                              mrl_index);
        break;
      case 16:
        highbd_dr_prediction_z3_16x16_idif_avx2(dst, stride, left, dy, bd,
                                                mrl_index);
        break;
      case 32:
        highbd_dr_prediction_z3_32x32_idif_avx2(dst, stride, left, dy, bd,
                                                mrl_index);
        break;
      case 64:
        highbd_dr_prediction_z3_64x64_idif_avx2(dst, stride, left, dy, bd,
                                                mrl_index);
        break;
    }
  } else {
    if (bw < bh) {
      if (bw + bw == bh) {
        switch (bw) {
          case 4:
            highbd_dr_prediction_z3_4x8_idif_avx2(dst, stride, left, dy, bd,
                                                  mrl_index);
            break;
          case 8:
            highbd_dr_prediction_z3_8x16_idif_avx2(dst, stride, left, dy, bd,
                                                   mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_16x32_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
          case 32:
            highbd_dr_prediction_z3_32x64_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
        }
      } else {
        switch (bw) {
          case 4:
#if CONFIG_FLEX_PARTITION
            if (bh == 32)
              highbd_dr_prediction_z3_4x32_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            else if (bh == 64)
              highbd_dr_prediction_z3_4x64_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_4x16_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            break;
          case 8:
#if CONFIG_FLEX_PARTITION
            if (bh == 64)
              highbd_dr_prediction_z3_8x64_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);

            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_8x32_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_16x64_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
        }
      }
    } else {
      if (bh + bh == bw) {
        switch (bh) {
          case 4:
            highbd_dr_prediction_z3_8x4_idif_avx2(dst, stride, left, dy, bd,
                                                  mrl_index);
            break;
          case 8:
            highbd_dr_prediction_z3_16x8_idif_avx2(dst, stride, left, dy, bd,
                                                   mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_32x16_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
          case 32:
            highbd_dr_prediction_z3_64x32_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
        }
      } else {
        switch (bh) {
          case 4:
#if CONFIG_FLEX_PARTITION
            if (bw == 32)

              highbd_dr_prediction_z3_32x4_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            else if (bw == 64)
              highbd_dr_prediction_z3_64x4_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);

            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_16x4_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);

            break;
          case 8:
#if CONFIG_FLEX_PARTITION
            if (bw == 64)
              highbd_dr_prediction_z3_64x8_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            else
#endif  // CONFIG_FLEX_PARTITION
              highbd_dr_prediction_z3_32x8_idif_avx2(dst, stride, left, dy, bd,
                                                     mrl_index);
            break;
          case 16:
            highbd_dr_prediction_z3_64x16_idif_avx2(dst, stride, left, dy, bd,
                                                    mrl_index);
            break;
        }
      }
    }
  }
  return;
}
#endif  // CONFIG_IDIF
