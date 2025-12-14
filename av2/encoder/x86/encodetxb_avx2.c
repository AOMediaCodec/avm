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
#include <immintrin.h>  /* AVX2 */

#include "avm/avm_integer.h"
#include "avm_dsp/x86/mem_sse2.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/txb_common.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"

static INLINE void _yy256_fill_buffer(__m256i *buff, __m256i *end,
                                      __m256i zeros) {
  do {
    _mm256_storeu_si256(buff, zeros);
    buff++;
  } while (buff < end);
}

void av2_txb_init_levels_skip_avx2(const tran_low_t *const coeff,
                                   const int width, const int height,
                                   uint8_t *const levels) {
  const int stride = width + TX_PAD_LEFT;
  const __m256i y_zeros = _mm256_setzero_si256();
  const __m128i x_zeros = _mm_setzero_si128();
  uint8_t *lvl_top_buf = levels;
  uint8_t *lvl_top_buf_end =
      lvl_top_buf + sizeof(*levels) * (TX_PAD_TOP * stride);
  _yy256_fill_buffer((__m256i *)lvl_top_buf, (__m256i *)lvl_top_buf_end,
                     y_zeros);
  int i = 0;
  uint8_t *ls = levels;
  const tran_low_t *cf = coeff;
  ls += TX_PAD_TOP * stride;
  if (width == 4) {
    do {
      const __m256i c0 = yy_loadu_256(cf);
      const __m256i c1 = yy_loadu_256(cf + 8);
      const __m256i abs01 = _mm256_abs_epi16(_mm256_packs_epi32(c0, c1));
      const __m256i abs01_8 = _mm256_packs_epi16(y_zeros, abs01);
      const __m256i res_ = _mm256_shuffle_epi32(abs01_8, 0xd8);
      const __m256i res = _mm256_permute4x64_epi64(res_, 0xd8);
      yy_storeu_256(ls, res);
      ls += 32;
      cf += 16;
      i += 4;
    } while (i < height);
  } else if (width == 8) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      const __m128i res0 = _mm256_castsi256_si128(res);
      const __m128i res1 = _mm256_extracti128_si256(res, 1);
      xx_storel_32(ls, x_zeros);
      xx_storel_64(ls + TX_PAD_LEFT, res0);
      xx_storel_32(ls + stride, x_zeros);
      xx_storel_64(ls + stride + TX_PAD_LEFT, _mm_srli_si128(res0, 8));
      xx_storel_32(ls + stride * 2, x_zeros);
      xx_storel_64(ls + stride * 2 + TX_PAD_LEFT, res1);
      xx_storel_32(ls + stride * 3, x_zeros);
      xx_storel_64(ls + stride * 3 + TX_PAD_LEFT, _mm_srli_si128(res1, 8));
      cf += 32;
      ls += stride << 2;
      i += 4;
    } while (i < height);
  } else if (width == 16) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      xx_storel_32(ls, x_zeros);
      xx_storeu_128(ls + TX_PAD_LEFT, _mm256_castsi256_si128(res));
      xx_storel_32(ls + stride, x_zeros);
      xx_storeu_128(ls + stride + TX_PAD_LEFT,
                    _mm256_extracti128_si256(res, 1));
      cf += 32;
      ls += stride << 1;
      i += 2;
    } while (i < height);
  } else {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      xx_storel_32(ls, x_zeros);
      yy_storeu_256(ls + TX_PAD_LEFT, res);
      cf += 32;
      ls += stride;
      i += 1;
    } while (i < height);
  }
}

void av2_txb_init_levels_signs_avx2(const tran_low_t *const coeff,
                                    const int width, const int height,
                                    uint8_t *const levels,
                                    int8_t *const signs) {
  const int stride = width + TX_PAD_HOR;
  const __m256i y_zeros = _mm256_setzero_si256();
  const __m128i x_zeros = _mm_setzero_si128();
  const __m256i one16 = _mm256_set1_epi16(1);
  uint8_t *lvl_top_buf = levels;
  uint8_t *lvl_top_buf_end =
      lvl_top_buf + sizeof(*levels) * (TX_PAD_TOP * stride);
  _yy256_fill_buffer((__m256i *)lvl_top_buf, (__m256i *)lvl_top_buf_end,
                     y_zeros);
  int8_t *si_bot_buf =
      signs + stride * height + sizeof(*signs) * (TX_PAD_TOP * stride);
  int8_t *si_bot_buf_end =
      si_bot_buf + sizeof(*signs) * (TX_PAD_BOTTOM * stride);
  _yy256_fill_buffer((__m256i *)si_bot_buf, (__m256i *)si_bot_buf_end, y_zeros);

  const int32_t lvl_bot_len = sizeof(*levels) * (TX_PAD_BOTTOM * stride);
  uint8_t *lvl_bot_buf_end = levels + (height + TX_PAD_VER) * stride;
  uint8_t *lvl_bot_buf = lvl_bot_buf_end - ((lvl_bot_len + 31) & (~31));
  do {
    yy_storeu_256(lvl_bot_buf, y_zeros);
    lvl_bot_buf += 32;
  } while (lvl_bot_buf < lvl_bot_buf_end);

  int i = 0;
  uint8_t *ls = levels;
  int8_t *si = signs;
  const tran_low_t *cf = coeff;

  ls += TX_PAD_TOP * stride;
  si += TX_PAD_TOP * stride;

  if (width == 4) {
    do {
      const __m256i c0 = yy_loadu_256(cf);
      const __m256i c1 = yy_loadu_256(cf + 8);
      const __m256i c0c1 = _mm256_packs_epi32(c0, c1);
      const __m256i abs01 = _mm256_abs_epi16(c0c1);
      const __m256i abs01_8 = _mm256_packs_epi16(y_zeros, abs01);
      const __m256i sig01 = _mm256_sign_epi16(one16, c0c1);
      const __m256i sig01_8 = _mm256_packs_epi16(y_zeros, sig01);
      const __m256i res_ = _mm256_shuffle_epi32(abs01_8, 0xd8);
      const __m256i res = _mm256_permute4x64_epi64(res_, 0xd8);
      const __m256i res_sig_ = _mm256_shuffle_epi32(sig01_8, 0xd8);
      const __m256i res_sig = _mm256_permute4x64_epi64(res_sig_, 0xd8);
      yy_storeu_256(ls, res);
      yy_storeu_256(si, res_sig);
      ls += 32;
      si += 32;
      cf += 16;
      i += 4;
    } while (i < height);
  } else if (width == 8) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      const __m128i res0 = _mm256_castsi256_si128(res);
      const __m128i res1 = _mm256_extracti128_si256(res, 1);
      const __m256i sigAB = _mm256_sign_epi16(one16, coeffAB);
      const __m256i sigCD = _mm256_sign_epi16(one16, coeffCD);
      const __m256i sigABCD = _mm256_packs_epi16(sigAB, sigCD);
      const __m256i sig_ = _mm256_permute4x64_epi64(sigABCD, 0xd8);
      const __m256i sig = _mm256_shuffle_epi32(sig_, 0xd8);
      const __m128i sig0 = _mm256_castsi256_si128(sig);
      const __m128i sig1 = _mm256_extracti128_si256(sig, 1);
      xx_storel_32(ls, x_zeros);
      xx_storel_64(ls + TX_PAD_LEFT, res0);
      xx_storel_32(ls + stride, x_zeros);
      xx_storel_64(ls + stride + TX_PAD_LEFT, _mm_srli_si128(res0, 8));
      xx_storel_32(ls + stride * 2, x_zeros);
      xx_storel_64(ls + stride * 2 + TX_PAD_LEFT, res1);
      xx_storel_32(ls + stride * 3, x_zeros);
      xx_storel_64(ls + stride * 3 + TX_PAD_LEFT, _mm_srli_si128(res1, 8));
      xx_storel_32(si, x_zeros);
      xx_storel_64(si + TX_PAD_LEFT, sig0);
      xx_storel_32(si + stride, x_zeros);
      xx_storel_64(si + stride + TX_PAD_LEFT, _mm_srli_si128(sig0, 8));
      xx_storel_32(si + stride * 2, x_zeros);
      xx_storel_64(si + stride * 2 + TX_PAD_LEFT, sig1);
      xx_storel_32(si + stride * 3, x_zeros);
      xx_storel_64(si + stride * 3 + TX_PAD_LEFT, _mm_srli_si128(sig1, 8));
      cf += 32;
      ls += stride << 2;
      si += stride << 2;
      i += 4;
    } while (i < height);
  } else if (width == 16) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      const __m256i sigAB = _mm256_sign_epi16(one16, coeffAB);
      const __m256i sigCD = _mm256_sign_epi16(one16, coeffCD);
      const __m256i sigABCD = _mm256_packs_epi16(sigAB, sigCD);
      const __m256i sig_ = _mm256_permute4x64_epi64(sigABCD, 0xd8);
      const __m256i sig = _mm256_shuffle_epi32(sig_, 0xd8);
      xx_storel_32(ls, x_zeros);
      xx_storeu_128(ls + TX_PAD_LEFT, _mm256_castsi256_si128(res));
      xx_storel_32(ls + stride, x_zeros);
      xx_storeu_128(ls + stride + TX_PAD_LEFT,
                    _mm256_extracti128_si256(res, 1));
      xx_storel_32(si, x_zeros);
      xx_storeu_128(si + TX_PAD_LEFT, _mm256_castsi256_si128(sig));
      xx_storel_32(si + stride, x_zeros);
      xx_storeu_128(si + stride + TX_PAD_LEFT,
                    _mm256_extracti128_si256(sig, 1));
      cf += 32;
      ls += stride << 1;
      si += stride << 1;
      i += 2;
    } while (i < height);
  } else {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      const __m256i sigAB = _mm256_sign_epi16(one16, coeffAB);
      const __m256i sigCD = _mm256_sign_epi16(one16, coeffCD);
      const __m256i sigABCD = _mm256_packs_epi16(sigAB, sigCD);
      const __m256i sig_ = _mm256_permute4x64_epi64(sigABCD, 0xd8);
      const __m256i sig = _mm256_shuffle_epi32(sig_, 0xd8);
      xx_storel_32(ls, x_zeros);
      yy_storeu_256(ls + TX_PAD_LEFT, res);
      xx_storel_32(si, x_zeros);
      yy_storeu_256(si + TX_PAD_LEFT, sig);
      cf += 32;
      ls += stride;
      si += stride;
      i += 1;
    } while (i < height);
  }
}

void av2_txb_init_levels_avx2(const tran_low_t *const coeff, const int width,
                              const int height, uint8_t *const levels) {
  const int stride = width + TX_PAD_HOR;
  const __m256i y_zeros = _mm256_setzero_si256();

  const int32_t bottom_len = sizeof(*levels) * (TX_PAD_BOTTOM * stride);
  uint8_t *bottom_buf_end = levels + (height + TX_PAD_BOTTOM) * stride;
  uint8_t *bottom_buf = bottom_buf_end - ((bottom_len + 31) & (~31));

  do {
    yy_storeu_256(bottom_buf, y_zeros);
    bottom_buf += 32;
  } while (bottom_buf < bottom_buf_end);

  int i = 0;
  uint8_t *ls = levels;
  const tran_low_t *cf = coeff;
  if (width == 4) {
    do {
      const __m256i c0 = yy_loadu_256(cf);
      const __m256i c1 = yy_loadu_256(cf + 8);
      const __m256i abs01 = _mm256_abs_epi16(_mm256_packs_epi32(c0, c1));
      const __m256i abs01_8 = _mm256_packs_epi16(abs01, y_zeros);
      const __m256i res_ = _mm256_shuffle_epi32(abs01_8, 0xd8);
      const __m256i res = _mm256_permute4x64_epi64(res_, 0xd8);
      yy_storeu_256(ls, res);
      ls += 32;
      cf += 16;
      i += 4;
    } while (i < height);
  } else if (width == 8) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      const __m128i res0 = _mm256_castsi256_si128(res);
      const __m128i res1 = _mm256_extracti128_si256(res, 1);
      xx_storel_64(ls, res0);
      *(int32_t *)(ls + width) = 0;
      xx_storel_64(ls + stride, _mm_srli_si128(res0, 8));
      *(int32_t *)(ls + width + stride) = 0;
      xx_storel_64(ls + stride * 2, res1);
      *(int32_t *)(ls + width + stride * 2) = 0;
      xx_storel_64(ls + stride * 3, _mm_srli_si128(res1, 8));
      *(int32_t *)(ls + width + stride * 3) = 0;
      cf += 32;
      ls += stride << 2;
      i += 4;
    } while (i < height);
  } else if (width == 16) {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      xx_storeu_128(ls, _mm256_castsi256_si128(res));
      xx_storeu_128(ls + stride, _mm256_extracti128_si256(res, 1));
      cf += 32;
      *(int32_t *)(ls + width) = 0;
      *(int32_t *)(ls + stride + width) = 0;
      ls += stride << 1;
      i += 2;
    } while (i < height);
  } else {
    do {
      const __m256i coeffA = yy_loadu_256(cf);
      const __m256i coeffB = yy_loadu_256(cf + 8);
      const __m256i coeffC = yy_loadu_256(cf + 16);
      const __m256i coeffD = yy_loadu_256(cf + 24);
      const __m256i coeffAB = _mm256_packs_epi32(coeffA, coeffB);
      const __m256i coeffCD = _mm256_packs_epi32(coeffC, coeffD);
      const __m256i absAB = _mm256_abs_epi16(coeffAB);
      const __m256i absCD = _mm256_abs_epi16(coeffCD);
      const __m256i absABCD = _mm256_packs_epi16(absAB, absCD);
      const __m256i res_ = _mm256_permute4x64_epi64(absABCD, 0xd8);
      const __m256i res = _mm256_shuffle_epi32(res_, 0xd8);
      yy_storeu_256(ls, res);
      cf += 32;
      *(int32_t *)(ls + width) = 0;
      ls += stride;
      i += 1;
    } while (i < height);
  }
}
