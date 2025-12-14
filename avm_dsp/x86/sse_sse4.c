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
#include <smmintrin.h>

#include "config/avm_config.h"

#include "avm_ports/mem.h"
#include "avm/avm_integer.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE int64_t summary_all_sse4(const __m128i *sum_all) {
  int64_t sum;
  const __m128i sum0 = _mm_cvtepu32_epi64(*sum_all);
  const __m128i sum1 = _mm_cvtepu32_epi64(_mm_srli_si128(*sum_all, 8));
  const __m128i sum_2x64 = _mm_add_epi64(sum0, sum1);
  const __m128i sum_1x64 = _mm_add_epi64(sum_2x64, _mm_srli_si128(sum_2x64, 8));
  xx_storel_64(&sum, sum_1x64);
  return sum;
}

static INLINE void summary_32_sse4(const __m128i *sum32, __m128i *sum64) {
  const __m128i sum0 = _mm_cvtepu32_epi64(*sum32);
  const __m128i sum1 = _mm_cvtepu32_epi64(_mm_srli_si128(*sum32, 8));
  *sum64 = _mm_add_epi64(sum0, *sum64);
  *sum64 = _mm_add_epi64(sum1, *sum64);
}

static INLINE void highbd_sse_w4x2_sse4_1(__m128i *sum, const uint16_t *a,
                                          int a_stride, const uint16_t *b,
                                          int b_stride) {
  const __m128i v_a0 = xx_loadl_64(a);
  const __m128i v_a1 = xx_loadl_64(a + a_stride);
  const __m128i v_b0 = xx_loadl_64(b);
  const __m128i v_b1 = xx_loadl_64(b + b_stride);
  const __m128i v_a_w = _mm_unpacklo_epi64(v_a0, v_a1);
  const __m128i v_b_w = _mm_unpacklo_epi64(v_b0, v_b1);
  const __m128i v_d_w = _mm_sub_epi16(v_a_w, v_b_w);
  *sum = _mm_add_epi32(*sum, _mm_madd_epi16(v_d_w, v_d_w));
}

static INLINE void highbd_sse_w8_sse4_1(__m128i *sum, const uint16_t *a,
                                        const uint16_t *b) {
  const __m128i v_a_w = xx_loadu_128(a);
  const __m128i v_b_w = xx_loadu_128(b);
  const __m128i v_d_w = _mm_sub_epi16(v_a_w, v_b_w);
  *sum = _mm_add_epi32(*sum, _mm_madd_epi16(v_d_w, v_d_w));
}

int64_t avm_highbd_sse_sse4_1(const uint16_t *a, int a_stride,
                              const uint16_t *b, int b_stride, int width,
                              int height) {
  int32_t y = 0;
  int64_t sse = 0;
  __m128i sum = _mm_setzero_si128();
  switch (width) {
    case 4:
      do {
        highbd_sse_w4x2_sse4_1(&sum, a, a_stride, b, b_stride);
        a += a_stride << 1;
        b += b_stride << 1;
        y += 2;
      } while (y < height);
      sse = summary_all_sse4(&sum);
      break;
    case 8:
      do {
        highbd_sse_w8_sse4_1(&sum, a, b);
        a += a_stride;
        b += b_stride;
        y += 1;
      } while (y < height);
      sse = summary_all_sse4(&sum);
      break;
    case 16:
      do {
        int l = 0;
        __m128i sum32 = _mm_setzero_si128();
        do {
          highbd_sse_w8_sse4_1(&sum32, a, b);
          highbd_sse_w8_sse4_1(&sum32, a + 8, b + 8);
          a += a_stride;
          b += b_stride;
          l += 1;
        } while (l < 64 && l < (height - y));
        summary_32_sse4(&sum32, &sum);
        y += 64;
      } while (y < height);
      xx_storel_64(&sse, _mm_add_epi64(sum, _mm_srli_si128(sum, 8)));
      break;
    case 32:
      do {
        int l = 0;
        __m128i sum32 = _mm_setzero_si128();
        do {
          highbd_sse_w8_sse4_1(&sum32, a, b);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 1, b + 8 * 1);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 2, b + 8 * 2);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 3, b + 8 * 3);
          a += a_stride;
          b += b_stride;
          l += 1;
        } while (l < 32 && l < (height - y));
        summary_32_sse4(&sum32, &sum);
        y += 32;
      } while (y < height);
      xx_storel_64(&sse, _mm_add_epi64(sum, _mm_srli_si128(sum, 8)));
      break;
    case 64:
      do {
        int l = 0;
        __m128i sum32 = _mm_setzero_si128();
        do {
          highbd_sse_w8_sse4_1(&sum32, a, b);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 1, b + 8 * 1);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 2, b + 8 * 2);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 3, b + 8 * 3);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 4, b + 8 * 4);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 5, b + 8 * 5);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 6, b + 8 * 6);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 7, b + 8 * 7);
          a += a_stride;
          b += b_stride;
          l += 1;
        } while (l < 16 && l < (height - y));
        summary_32_sse4(&sum32, &sum);
        y += 16;
      } while (y < height);
      xx_storel_64(&sse, _mm_add_epi64(sum, _mm_srli_si128(sum, 8)));
      break;
    case 128:
      do {
        int l = 0;
        __m128i sum32 = _mm_setzero_si128();
        do {
          highbd_sse_w8_sse4_1(&sum32, a, b);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 1, b + 8 * 1);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 2, b + 8 * 2);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 3, b + 8 * 3);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 4, b + 8 * 4);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 5, b + 8 * 5);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 6, b + 8 * 6);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 7, b + 8 * 7);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 8, b + 8 * 8);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 9, b + 8 * 9);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 10, b + 8 * 10);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 11, b + 8 * 11);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 12, b + 8 * 12);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 13, b + 8 * 13);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 14, b + 8 * 14);
          highbd_sse_w8_sse4_1(&sum32, a + 8 * 15, b + 8 * 15);
          a += a_stride;
          b += b_stride;
          l += 1;
        } while (l < 8 && l < (height - y));
        summary_32_sse4(&sum32, &sum);
        y += 8;
      } while (y < height);
      xx_storel_64(&sse, _mm_add_epi64(sum, _mm_srli_si128(sum, 8)));
      break;
    default:
      if (width & 0x7) {
        do {
          __m128i sum32 = _mm_setzero_si128();
          int i = 0;
          do {
            highbd_sse_w8_sse4_1(&sum32, a + i, b + i);
            highbd_sse_w8_sse4_1(&sum32, a + i + a_stride, b + i + b_stride);
            i += 8;
          } while (i + 4 < width);
          highbd_sse_w4x2_sse4_1(&sum32, a + i, a_stride, b + i, b_stride);
          a += (a_stride << 1);
          b += (b_stride << 1);
          y += 2;
          summary_32_sse4(&sum32, &sum);
        } while (y < height);
      } else {
        do {
          int l = 0;
          __m128i sum32 = _mm_setzero_si128();
          do {
            int i = 0;
            do {
              highbd_sse_w8_sse4_1(&sum32, a + i, b + i);
              i += 8;
            } while (i < width);
            a += a_stride;
            b += b_stride;
            l += 1;
          } while (l < 8 && l < (height - y));
          summary_32_sse4(&sum32, &sum);
          y += 8;
        } while (y < height);
      }
      xx_storel_64(&sse, _mm_add_epi64(sum, _mm_srli_si128(sum, 8)));
      break;
  }
  return sse;
}
