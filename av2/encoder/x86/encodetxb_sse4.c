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
#include <emmintrin.h>  // SSE2
#include <smmintrin.h>  /* SSE4.1 */

#include "avm/avm_integer.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/txb_common.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE void _xx_fill_buffer(__m128i *buff, __m128i *end, __m128i zeros) {
  do {
    _mm_storeu_si128(buff, zeros);
    buff++;
  } while (buff < end);
}

void av2_txb_init_levels_skip_sse4_1(const tran_low_t *const coeff,
                                     const int width, const int height,
                                     uint8_t *const levels) {
  const int stride = width + TX_PAD_LEFT;
  const __m128i zeros = _mm_setzero_si128();

  const int32_t bottom_len = sizeof(*levels) * (TX_PAD_BOTTOM * stride);
  uint8_t *bottom_buf = levels + stride * height;
  uint8_t *bottom_buf_end = bottom_buf + bottom_len;

  do {
    _mm_storeu_si128((__m128i *)(bottom_buf), zeros);
    bottom_buf += 16;
  } while (bottom_buf < bottom_buf_end);

  const int32_t top_len = sizeof(*levels) * (TX_PAD_TOP * stride);
  uint8_t *top_buf = levels;
  uint8_t *top_buf_end = top_buf + top_len;
  do {
    _mm_storeu_si128((__m128i *)(top_buf), zeros);
    top_buf += 16;
  } while (top_buf < top_buf_end);

  int i = 0;
  uint8_t *ls = levels;
  const tran_low_t *cf = coeff;
  ls += TX_PAD_TOP * stride;
  if (width == 4) {
    do {
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i coeffZA = _mm_packs_epi32(zeros, coeffA);
      const __m128i absZA = _mm_abs_epi16(coeffZA);
      const __m128i coeffZB = _mm_packs_epi32(zeros, coeffB);
      const __m128i absZB = _mm_abs_epi16(coeffZB);
      const __m128i coeffAB = _mm_packs_epi16(absZA, absZB);
      xx_storeu_128(ls, coeffAB);
      ls += ((stride) << 1);
      cf += (width << 1);
      i += 2;
    } while (i < height);
  } else if (width == 8) {
    do {
      xx_storeu_128(ls, zeros);
      ls += TX_PAD_LEFT;
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
      const __m128i absAB = _mm_abs_epi16(coeffAB);
      const __m128i absAB8 = _mm_packs_epi16(absAB, zeros);
      xx_storeu_128(ls, absAB8);
      ls += stride - TX_PAD_LEFT;
      cf += width;
      i += 1;
    } while (i < height);
  } else {
    do {
      xx_storeu_128(ls, zeros);
      ls += TX_PAD_LEFT;
      int j = 0;
      do {
        const __m128i coeffA = xx_loadu_128(cf);
        const __m128i coeffB = xx_loadu_128(cf + 4);
        const __m128i coeffC = xx_loadu_128(cf + 8);
        const __m128i coeffD = xx_loadu_128(cf + 12);
        const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
        const __m128i coeffCD = _mm_packs_epi32(coeffC, coeffD);
        const __m128i absAB = _mm_abs_epi16(coeffAB);
        const __m128i absCD = _mm_abs_epi16(coeffCD);
        const __m128i absABCD = _mm_packs_epi16(absAB, absCD);
        xx_storeu_128(ls + j, absABCD);
        j += 16;
        cf += 16;
      } while (j < width);
      *(int32_t *)(ls + width) = 0;
      ls += stride - TX_PAD_LEFT;
      i += 1;
    } while (i < height);
  }
}

void av2_txb_init_levels_signs_sse4_1(const tran_low_t *const coeff,
                                      const int width, const int height,
                                      uint8_t *const levels,
                                      int8_t *const signs) {
  const int stride = width + TX_PAD_HOR;
  const __m128i zeros = _mm_setzero_si128();
  const __m128i one16 = _mm_set1_epi16(1);

  uint8_t *lvl_top_buf = levels;
  uint8_t *lvl_top_buf_end =
      lvl_top_buf + sizeof(*levels) * (TX_PAD_TOP * stride);
  _xx_fill_buffer((__m128i *)lvl_top_buf, (__m128i *)lvl_top_buf_end, zeros);

  const int32_t lvl_bot_len = sizeof(*levels) * (TX_PAD_BOTTOM * stride);
  uint8_t *lvl_bot_buf = levels + stride * (TX_PAD_TOP + height);
  uint8_t *lvl_bot_buf_end = lvl_bot_buf + lvl_bot_len;
  do {
    _mm_storeu_si128((__m128i *)(lvl_bot_buf), zeros);
    lvl_bot_buf += 16;
  } while (lvl_bot_buf < lvl_bot_buf_end);

  int8_t *si_bot_buf =
      signs + stride * height + sizeof(*signs) * (TX_PAD_TOP * stride);
  int8_t *si_bot_buf_end =
      si_bot_buf + sizeof(*signs) * (TX_PAD_BOTTOM * stride);
  _xx_fill_buffer((__m128i *)si_bot_buf, (__m128i *)si_bot_buf_end, zeros);

  int i = 0;
  int8_t *si = signs;
  uint8_t *ls = levels;
  const tran_low_t *cf = coeff;

  ls += TX_PAD_TOP * stride;
  si += TX_PAD_TOP * stride;

  if (width == 4) {
    do {
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i absZA = _mm_abs_epi16(_mm_packs_epi32(zeros, coeffA));
      const __m128i absZB = _mm_abs_epi16(_mm_packs_epi32(zeros, coeffB));
      const __m128i coeffAB = _mm_packs_epi16(absZA, absZB);
      const __m128i signZA =
          _mm_sign_epi16(one16, _mm_packs_epi32(zeros, coeffA));
      const __m128i signZB =
          _mm_sign_epi16(one16, _mm_packs_epi32(zeros, coeffB));
      const __m128i signAB = _mm_packs_epi16(signZA, signZB);
      xx_storeu_128(ls, coeffAB);
      xx_storeu_128(si, signAB);
      ls += ((stride) << 1);
      si += ((stride) << 1);
      cf += (width << 1);
      i += 2;
    } while (i < height);
  } else if (width == 8) {
    do {
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
      const __m128i absAB = _mm_abs_epi16(coeffAB);
      const __m128i absAB8 = _mm_packs_epi16(absAB, zeros);
      const __m128i signAB = _mm_sign_epi16(one16, coeffAB);
      const __m128i signAB8 = _mm_packs_epi16(signAB, zeros);
      xx_storeu_128(ls, zeros);
      xx_storeu_128(ls + TX_PAD_LEFT, absAB8);
      xx_storeu_128(si, zeros);
      xx_storeu_128(si + TX_PAD_LEFT, signAB8);
      si += stride;
      ls += stride;
      cf += width;
      i += 1;
    } while (i < height);
  } else {
    do {
      xx_storeu_128(ls, zeros);
      xx_storeu_128(si, zeros);
      si += TX_PAD_LEFT;
      ls += TX_PAD_LEFT;
      int j = 0;
      do {
        const __m128i coeffA = xx_loadu_128(cf);
        const __m128i coeffB = xx_loadu_128(cf + 4);
        const __m128i coeffC = xx_loadu_128(cf + 8);
        const __m128i coeffD = xx_loadu_128(cf + 12);
        const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
        const __m128i coeffCD = _mm_packs_epi32(coeffC, coeffD);
        const __m128i absAB = _mm_abs_epi16(coeffAB);
        const __m128i absCD = _mm_abs_epi16(coeffCD);
        const __m128i absABCD = _mm_packs_epi16(absAB, absCD);
        const __m128i signAB = _mm_sign_epi16(one16, coeffAB);
        const __m128i signCD = _mm_sign_epi16(one16, coeffCD);
        const __m128i signABCD = _mm_packs_epi16(signAB, signCD);
        xx_storeu_128(ls + j, absABCD);
        xx_storeu_128(si + j, signABCD);
        j += 16;
        cf += 16;
      } while (j < width);
      *(int32_t *)(si + width) = 0;
      si += stride - TX_PAD_LEFT;
      *(int32_t *)(ls + width) = 0;
      ls += stride - TX_PAD_LEFT;
      i += 1;
    } while (i < height);
  }
}

void av2_txb_init_levels_sse4_1(const tran_low_t *const coeff, const int width,
                                const int height, uint8_t *const levels) {
  const int stride = width + TX_PAD_HOR;
  const __m128i zeros = _mm_setzero_si128();

  const int32_t bottom_len = sizeof(*levels) * (TX_PAD_BOTTOM * stride);
  uint8_t *bottom_buf = levels + stride * height;
  uint8_t *bottom_buf_end = bottom_buf + bottom_len;
  do {
    _mm_storeu_si128((__m128i *)(bottom_buf), zeros);
    bottom_buf += 16;
  } while (bottom_buf < bottom_buf_end);

  int i = 0;
  uint8_t *ls = levels;
  const tran_low_t *cf = coeff;
  if (width == 4) {
    do {
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
      const __m128i absAB = _mm_abs_epi16(coeffAB);
      const __m128i absAB8 = _mm_packs_epi16(absAB, zeros);
      const __m128i lsAB = _mm_unpacklo_epi32(absAB8, zeros);
      xx_storeu_128(ls, lsAB);
      ls += (stride << 1);
      cf += (width << 1);
      i += 2;
    } while (i < height);
  } else if (width == 8) {
    do {
      const __m128i coeffA = xx_loadu_128(cf);
      const __m128i coeffB = xx_loadu_128(cf + 4);
      const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
      const __m128i absAB = _mm_abs_epi16(coeffAB);
      const __m128i absAB8 = _mm_packs_epi16(absAB, zeros);
      xx_storeu_128(ls, absAB8);
      ls += stride;
      cf += width;
      i += 1;
    } while (i < height);
  } else {
    do {
      int j = 0;
      do {
        const __m128i coeffA = xx_loadu_128(cf);
        const __m128i coeffB = xx_loadu_128(cf + 4);
        const __m128i coeffC = xx_loadu_128(cf + 8);
        const __m128i coeffD = xx_loadu_128(cf + 12);
        const __m128i coeffAB = _mm_packs_epi32(coeffA, coeffB);
        const __m128i coeffCD = _mm_packs_epi32(coeffC, coeffD);
        const __m128i absAB = _mm_abs_epi16(coeffAB);
        const __m128i absCD = _mm_abs_epi16(coeffCD);
        const __m128i absABCD = _mm_packs_epi16(absAB, absCD);
        xx_storeu_128(ls + j, absABCD);
        j += 16;
        cf += 16;
      } while (j < width);
      *(int32_t *)(ls + width) = 0;
      ls += stride;
      i += 1;
    } while (i < height);
  }
}
