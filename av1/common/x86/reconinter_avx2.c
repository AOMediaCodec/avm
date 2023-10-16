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

#include "config/av1_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/blend.h"
#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"
#include "av1/common/blockd.h"

static INLINE __m256i calc_mask_d16_avx2(const __m256i *data_src0,
                                         const __m256i *data_src1,
                                         const __m256i *round_const,
                                         const __m256i *mask_base_16,
                                         const __m256i *clip_diff, int round) {
  const __m256i diffa = _mm256_subs_epu16(*data_src0, *data_src1);
  const __m256i diffb = _mm256_subs_epu16(*data_src1, *data_src0);
  const __m256i diff = _mm256_max_epu16(diffa, diffb);
  const __m256i diff_round =
      _mm256_srli_epi16(_mm256_adds_epu16(diff, *round_const), round);
  const __m256i diff_factor = _mm256_srli_epi16(diff_round, DIFF_FACTOR_LOG2);
  const __m256i diff_mask = _mm256_adds_epi16(diff_factor, *mask_base_16);
  const __m256i diff_clamp = _mm256_min_epi16(diff_mask, *clip_diff);
  return diff_clamp;
}

static INLINE __m256i calc_mask_d16_inv_avx2(const __m256i *data_src0,
                                             const __m256i *data_src1,
                                             const __m256i *round_const,
                                             const __m256i *mask_base_16,
                                             const __m256i *clip_diff,
                                             int round) {
  const __m256i diffa = _mm256_subs_epu16(*data_src0, *data_src1);
  const __m256i diffb = _mm256_subs_epu16(*data_src1, *data_src0);
  const __m256i diff = _mm256_max_epu16(diffa, diffb);
  const __m256i diff_round =
      _mm256_srli_epi16(_mm256_adds_epu16(diff, *round_const), round);
  const __m256i diff_factor = _mm256_srli_epi16(diff_round, DIFF_FACTOR_LOG2);
  const __m256i diff_mask = _mm256_adds_epi16(diff_factor, *mask_base_16);
  const __m256i diff_clamp = _mm256_min_epi16(diff_mask, *clip_diff);
  const __m256i diff_const_16 = _mm256_sub_epi16(*clip_diff, diff_clamp);
  return diff_const_16;
}

static INLINE void build_compound_diffwtd_mask_d16_avx2(
    uint8_t *mask, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, int h, int w, int shift) {
  const int mask_base = 38;
  const __m256i _r = _mm256_set1_epi16((1 << shift) >> 1);
  const __m256i y38 = _mm256_set1_epi16(mask_base);
  const __m256i y64 = _mm256_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);
  int i = 0;
  if (w == 4) {
    do {
      const __m128i s0A = xx_loadl_64(src0);
      const __m128i s0B = xx_loadl_64(src0 + src0_stride);
      const __m128i s0C = xx_loadl_64(src0 + src0_stride * 2);
      const __m128i s0D = xx_loadl_64(src0 + src0_stride * 3);
      const __m128i s1A = xx_loadl_64(src1);
      const __m128i s1B = xx_loadl_64(src1 + src1_stride);
      const __m128i s1C = xx_loadl_64(src1 + src1_stride * 2);
      const __m128i s1D = xx_loadl_64(src1 + src1_stride * 3);
      const __m256i s0 = yy_set_m128i(_mm_unpacklo_epi64(s0C, s0D),
                                      _mm_unpacklo_epi64(s0A, s0B));
      const __m256i s1 = yy_set_m128i(_mm_unpacklo_epi64(s1C, s1D),
                                      _mm_unpacklo_epi64(s1A, s1B));
      const __m256i m16 = calc_mask_d16_avx2(&s0, &s1, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16, _mm256_setzero_si256());
      xx_storeu_128(mask,
                    _mm256_castsi256_si128(_mm256_permute4x64_epi64(m8, 0xd8)));
      src0 += src0_stride << 2;
      src1 += src1_stride << 2;
      mask += 16;
      i += 4;
    } while (i < h);
  } else if (w == 8) {
    do {
      const __m256i s0AB = yy_loadu2_128(src0 + src0_stride, src0);
      const __m256i s0CD =
          yy_loadu2_128(src0 + src0_stride * 3, src0 + src0_stride * 2);
      const __m256i s1AB = yy_loadu2_128(src1 + src1_stride, src1);
      const __m256i s1CD =
          yy_loadu2_128(src1 + src1_stride * 3, src1 + src1_stride * 2);
      const __m256i m16AB =
          calc_mask_d16_avx2(&s0AB, &s1AB, &_r, &y38, &y64, shift);
      const __m256i m16CD =
          calc_mask_d16_avx2(&s0CD, &s1CD, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16AB, m16CD);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride << 2;
      src1 += src1_stride << 2;
      mask += 32;
      i += 4;
    } while (i < h);
  } else if (w == 16) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + src0_stride);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + src1_stride);
      const __m256i m16A =
          calc_mask_d16_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16A, m16B);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride << 1;
      src1 += src1_stride << 1;
      mask += 32;
      i += 2;
    } while (i < h);
  } else if (w == 32) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i m16A =
          calc_mask_d16_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16A, m16B);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 32;
      i += 1;
    } while (i < h);
  } else if (w == 64) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s0C = yy_loadu_256(src0 + 32);
      const __m256i s0D = yy_loadu_256(src0 + 48);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i s1C = yy_loadu_256(src1 + 32);
      const __m256i s1D = yy_loadu_256(src1 + 48);
      const __m256i m16A =
          calc_mask_d16_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m16C =
          calc_mask_d16_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
      const __m256i m16D =
          calc_mask_d16_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
      const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
      const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
      yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 64;
      i += 1;
    } while (i < h);
  } else if (w == 128) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s0C = yy_loadu_256(src0 + 32);
      const __m256i s0D = yy_loadu_256(src0 + 48);
      const __m256i s0E = yy_loadu_256(src0 + 64);
      const __m256i s0F = yy_loadu_256(src0 + 80);
      const __m256i s0G = yy_loadu_256(src0 + 96);
      const __m256i s0H = yy_loadu_256(src0 + 112);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i s1C = yy_loadu_256(src1 + 32);
      const __m256i s1D = yy_loadu_256(src1 + 48);
      const __m256i s1E = yy_loadu_256(src1 + 64);
      const __m256i s1F = yy_loadu_256(src1 + 80);
      const __m256i s1G = yy_loadu_256(src1 + 96);
      const __m256i s1H = yy_loadu_256(src1 + 112);
      const __m256i m16A =
          calc_mask_d16_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m16C =
          calc_mask_d16_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
      const __m256i m16D =
          calc_mask_d16_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
      const __m256i m16E =
          calc_mask_d16_avx2(&s0E, &s1E, &_r, &y38, &y64, shift);
      const __m256i m16F =
          calc_mask_d16_avx2(&s0F, &s1F, &_r, &y38, &y64, shift);
      const __m256i m16G =
          calc_mask_d16_avx2(&s0G, &s1G, &_r, &y38, &y64, shift);
      const __m256i m16H =
          calc_mask_d16_avx2(&s0H, &s1H, &_r, &y38, &y64, shift);
      const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
      const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
      const __m256i m8EF = _mm256_packus_epi16(m16E, m16F);
      const __m256i m8GH = _mm256_packus_epi16(m16G, m16H);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
      yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
      yy_storeu_256(mask + 64, _mm256_permute4x64_epi64(m8EF, 0xd8));
      yy_storeu_256(mask + 96, _mm256_permute4x64_epi64(m8GH, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 128;
      i += 1;
    } while (i < h);
  } else {
#if CONFIG_BLOCK_256
    assert(w == 256);
    do {
      const CONV_BUF_TYPE *src0_ptr = src0;
      const CONV_BUF_TYPE *src1_ptr = src1;
      for (int loop = 0; loop < 2; loop++) {
        const __m256i s0A = yy_loadu_256(src0_ptr);
        const __m256i s0B = yy_loadu_256(src0_ptr + 16);
        const __m256i s0C = yy_loadu_256(src0_ptr + 32);
        const __m256i s0D = yy_loadu_256(src0_ptr + 48);
        const __m256i s0E = yy_loadu_256(src0_ptr + 64);
        const __m256i s0F = yy_loadu_256(src0_ptr + 80);
        const __m256i s0G = yy_loadu_256(src0_ptr + 96);
        const __m256i s0H = yy_loadu_256(src0_ptr + 112);
        const __m256i s1A = yy_loadu_256(src1_ptr);
        const __m256i s1B = yy_loadu_256(src1_ptr + 16);
        const __m256i s1C = yy_loadu_256(src1_ptr + 32);
        const __m256i s1D = yy_loadu_256(src1_ptr + 48);
        const __m256i s1E = yy_loadu_256(src1_ptr + 64);
        const __m256i s1F = yy_loadu_256(src1_ptr + 80);
        const __m256i s1G = yy_loadu_256(src1_ptr + 96);
        const __m256i s1H = yy_loadu_256(src1_ptr + 112);
        const __m256i m16A =
            calc_mask_d16_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
        const __m256i m16B =
            calc_mask_d16_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
        const __m256i m16C =
            calc_mask_d16_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
        const __m256i m16D =
            calc_mask_d16_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
        const __m256i m16E =
            calc_mask_d16_avx2(&s0E, &s1E, &_r, &y38, &y64, shift);
        const __m256i m16F =
            calc_mask_d16_avx2(&s0F, &s1F, &_r, &y38, &y64, shift);
        const __m256i m16G =
            calc_mask_d16_avx2(&s0G, &s1G, &_r, &y38, &y64, shift);
        const __m256i m16H =
            calc_mask_d16_avx2(&s0H, &s1H, &_r, &y38, &y64, shift);
        const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
        const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
        const __m256i m8EF = _mm256_packus_epi16(m16E, m16F);
        const __m256i m8GH = _mm256_packus_epi16(m16G, m16H);
        yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
        yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
        yy_storeu_256(mask + 64, _mm256_permute4x64_epi64(m8EF, 0xd8));
        yy_storeu_256(mask + 96, _mm256_permute4x64_epi64(m8GH, 0xd8));
        src0_ptr += 128;
        src1_ptr += 128;
        mask += 128;
      }
      src0 += src0_stride;
      src1 += src1_stride;
      i += 1;
    } while (i < h);
#else
    assert(0);
#endif  // CONFIG_BLOCK_256
  }
}

static INLINE void build_compound_diffwtd_mask_d16_inv_avx2(
    uint8_t *mask, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, int h, int w, int shift) {
  const int mask_base = 38;
  const __m256i _r = _mm256_set1_epi16((1 << shift) >> 1);
  const __m256i y38 = _mm256_set1_epi16(mask_base);
  const __m256i y64 = _mm256_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);
  int i = 0;
  if (w == 4) {
    do {
      const __m128i s0A = xx_loadl_64(src0);
      const __m128i s0B = xx_loadl_64(src0 + src0_stride);
      const __m128i s0C = xx_loadl_64(src0 + src0_stride * 2);
      const __m128i s0D = xx_loadl_64(src0 + src0_stride * 3);
      const __m128i s1A = xx_loadl_64(src1);
      const __m128i s1B = xx_loadl_64(src1 + src1_stride);
      const __m128i s1C = xx_loadl_64(src1 + src1_stride * 2);
      const __m128i s1D = xx_loadl_64(src1 + src1_stride * 3);
      const __m256i s0 = yy_set_m128i(_mm_unpacklo_epi64(s0C, s0D),
                                      _mm_unpacklo_epi64(s0A, s0B));
      const __m256i s1 = yy_set_m128i(_mm_unpacklo_epi64(s1C, s1D),
                                      _mm_unpacklo_epi64(s1A, s1B));
      const __m256i m16 =
          calc_mask_d16_inv_avx2(&s0, &s1, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16, _mm256_setzero_si256());
      xx_storeu_128(mask,
                    _mm256_castsi256_si128(_mm256_permute4x64_epi64(m8, 0xd8)));
      src0 += src0_stride << 2;
      src1 += src1_stride << 2;
      mask += 16;
      i += 4;
    } while (i < h);
  } else if (w == 8) {
    do {
      const __m256i s0AB = yy_loadu2_128(src0 + src0_stride, src0);
      const __m256i s0CD =
          yy_loadu2_128(src0 + src0_stride * 3, src0 + src0_stride * 2);
      const __m256i s1AB = yy_loadu2_128(src1 + src1_stride, src1);
      const __m256i s1CD =
          yy_loadu2_128(src1 + src1_stride * 3, src1 + src1_stride * 2);
      const __m256i m16AB =
          calc_mask_d16_inv_avx2(&s0AB, &s1AB, &_r, &y38, &y64, shift);
      const __m256i m16CD =
          calc_mask_d16_inv_avx2(&s0CD, &s1CD, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16AB, m16CD);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride << 2;
      src1 += src1_stride << 2;
      mask += 32;
      i += 4;
    } while (i < h);
  } else if (w == 16) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + src0_stride);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + src1_stride);
      const __m256i m16A =
          calc_mask_d16_inv_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_inv_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16A, m16B);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride << 1;
      src1 += src1_stride << 1;
      mask += 32;
      i += 2;
    } while (i < h);
  } else if (w == 32) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i m16A =
          calc_mask_d16_inv_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_inv_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m8 = _mm256_packus_epi16(m16A, m16B);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 32;
      i += 1;
    } while (i < h);
  } else if (w == 64) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s0C = yy_loadu_256(src0 + 32);
      const __m256i s0D = yy_loadu_256(src0 + 48);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i s1C = yy_loadu_256(src1 + 32);
      const __m256i s1D = yy_loadu_256(src1 + 48);
      const __m256i m16A =
          calc_mask_d16_inv_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_inv_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m16C =
          calc_mask_d16_inv_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
      const __m256i m16D =
          calc_mask_d16_inv_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
      const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
      const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
      yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 64;
      i += 1;
    } while (i < h);
  } else if (w == 128) {
    do {
      const __m256i s0A = yy_loadu_256(src0);
      const __m256i s0B = yy_loadu_256(src0 + 16);
      const __m256i s0C = yy_loadu_256(src0 + 32);
      const __m256i s0D = yy_loadu_256(src0 + 48);
      const __m256i s0E = yy_loadu_256(src0 + 64);
      const __m256i s0F = yy_loadu_256(src0 + 80);
      const __m256i s0G = yy_loadu_256(src0 + 96);
      const __m256i s0H = yy_loadu_256(src0 + 112);
      const __m256i s1A = yy_loadu_256(src1);
      const __m256i s1B = yy_loadu_256(src1 + 16);
      const __m256i s1C = yy_loadu_256(src1 + 32);
      const __m256i s1D = yy_loadu_256(src1 + 48);
      const __m256i s1E = yy_loadu_256(src1 + 64);
      const __m256i s1F = yy_loadu_256(src1 + 80);
      const __m256i s1G = yy_loadu_256(src1 + 96);
      const __m256i s1H = yy_loadu_256(src1 + 112);
      const __m256i m16A =
          calc_mask_d16_inv_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
      const __m256i m16B =
          calc_mask_d16_inv_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
      const __m256i m16C =
          calc_mask_d16_inv_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
      const __m256i m16D =
          calc_mask_d16_inv_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
      const __m256i m16E =
          calc_mask_d16_inv_avx2(&s0E, &s1E, &_r, &y38, &y64, shift);
      const __m256i m16F =
          calc_mask_d16_inv_avx2(&s0F, &s1F, &_r, &y38, &y64, shift);
      const __m256i m16G =
          calc_mask_d16_inv_avx2(&s0G, &s1G, &_r, &y38, &y64, shift);
      const __m256i m16H =
          calc_mask_d16_inv_avx2(&s0H, &s1H, &_r, &y38, &y64, shift);
      const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
      const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
      const __m256i m8EF = _mm256_packus_epi16(m16E, m16F);
      const __m256i m8GH = _mm256_packus_epi16(m16G, m16H);
      yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
      yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
      yy_storeu_256(mask + 64, _mm256_permute4x64_epi64(m8EF, 0xd8));
      yy_storeu_256(mask + 96, _mm256_permute4x64_epi64(m8GH, 0xd8));
      src0 += src0_stride;
      src1 += src1_stride;
      mask += 128;
      i += 1;
    } while (i < h);
  } else {
#if CONFIG_BLOCK_256
    assert(w == 256);
    do {
      const CONV_BUF_TYPE *src0_ptr = src0;
      const CONV_BUF_TYPE *src1_ptr = src1;
      for (int loop = 0; loop < 2; loop++) {
        const __m256i s0A = yy_loadu_256(src0_ptr);
        const __m256i s0B = yy_loadu_256(src0_ptr + 16);
        const __m256i s0C = yy_loadu_256(src0_ptr + 32);
        const __m256i s0D = yy_loadu_256(src0_ptr + 48);
        const __m256i s0E = yy_loadu_256(src0_ptr + 64);
        const __m256i s0F = yy_loadu_256(src0_ptr + 80);
        const __m256i s0G = yy_loadu_256(src0_ptr + 96);
        const __m256i s0H = yy_loadu_256(src0_ptr + 112);
        const __m256i s1A = yy_loadu_256(src1_ptr);
        const __m256i s1B = yy_loadu_256(src1_ptr + 16);
        const __m256i s1C = yy_loadu_256(src1_ptr + 32);
        const __m256i s1D = yy_loadu_256(src1_ptr + 48);
        const __m256i s1E = yy_loadu_256(src1_ptr + 64);
        const __m256i s1F = yy_loadu_256(src1_ptr + 80);
        const __m256i s1G = yy_loadu_256(src1_ptr + 96);
        const __m256i s1H = yy_loadu_256(src1_ptr + 112);
        const __m256i m16A =
            calc_mask_d16_inv_avx2(&s0A, &s1A, &_r, &y38, &y64, shift);
        const __m256i m16B =
            calc_mask_d16_inv_avx2(&s0B, &s1B, &_r, &y38, &y64, shift);
        const __m256i m16C =
            calc_mask_d16_inv_avx2(&s0C, &s1C, &_r, &y38, &y64, shift);
        const __m256i m16D =
            calc_mask_d16_inv_avx2(&s0D, &s1D, &_r, &y38, &y64, shift);
        const __m256i m16E =
            calc_mask_d16_inv_avx2(&s0E, &s1E, &_r, &y38, &y64, shift);
        const __m256i m16F =
            calc_mask_d16_inv_avx2(&s0F, &s1F, &_r, &y38, &y64, shift);
        const __m256i m16G =
            calc_mask_d16_inv_avx2(&s0G, &s1G, &_r, &y38, &y64, shift);
        const __m256i m16H =
            calc_mask_d16_inv_avx2(&s0H, &s1H, &_r, &y38, &y64, shift);
        const __m256i m8AB = _mm256_packus_epi16(m16A, m16B);
        const __m256i m8CD = _mm256_packus_epi16(m16C, m16D);
        const __m256i m8EF = _mm256_packus_epi16(m16E, m16F);
        const __m256i m8GH = _mm256_packus_epi16(m16G, m16H);
        yy_storeu_256(mask, _mm256_permute4x64_epi64(m8AB, 0xd8));
        yy_storeu_256(mask + 32, _mm256_permute4x64_epi64(m8CD, 0xd8));
        yy_storeu_256(mask + 64, _mm256_permute4x64_epi64(m8EF, 0xd8));
        yy_storeu_256(mask + 96, _mm256_permute4x64_epi64(m8GH, 0xd8));
        src0_ptr += 128;
        src1_ptr += 128;
        mask += 128;
      }
      src0 += src0_stride;
      src1 += src1_stride;
      i += 1;
    } while (i < h);
#else
    assert(0);
#endif  // CONFIG_BLOCK_256
  }
}

void av1_build_compound_diffwtd_mask_d16_avx2(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w,
    ConvolveParams *conv_params, int bd) {
  const int shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1 + (bd - 8);
  // When rounding constant is added, there is a possibility of overflow.
  // However that much precision is not required. Code should very well work for
  // other values of DIFF_FACTOR_LOG2 and AOM_BLEND_A64_MAX_ALPHA as well. But
  // there is a possibility of corner case bugs.
  assert(DIFF_FACTOR_LOG2 == 4);
  assert(AOM_BLEND_A64_MAX_ALPHA == 64);

  if (mask_type == DIFFWTD_38) {
    build_compound_diffwtd_mask_d16_avx2(mask, src0, src0_stride, src1,
                                         src1_stride, h, w, shift);
  } else {
    build_compound_diffwtd_mask_d16_inv_avx2(mask, src0, src0_stride, src1,
                                             src1_stride, h, w, shift);
  }
}

void av1_build_compound_diffwtd_mask_highbd_avx2(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const uint16_t *ssrc0,
    int src0_stride, const uint16_t *ssrc1, int src1_stride, int h, int w,
    int bd) {
  if (w < 16) {
    av1_build_compound_diffwtd_mask_highbd_ssse3(
        mask, mask_type, ssrc0, src0_stride, ssrc1, src1_stride, h, w, bd);
  } else {
    assert(mask_type == DIFFWTD_38 || mask_type == DIFFWTD_38_INV);
    assert(bd >= 8);
    assert((w % 16) == 0);
    const __m256i y0 = _mm256_setzero_si256();
    const __m256i yAOM_BLEND_A64_MAX_ALPHA =
        _mm256_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);
    const int mask_base = 38;
    const __m256i ymask_base = _mm256_set1_epi16(mask_base);
    if (bd == 8) {
      if (mask_type == DIFFWTD_38_INV) {
        for (int i = 0; i < h; ++i) {
          for (int j = 0; j < w; j += 16) {
            __m256i s0 = _mm256_loadu_si256((const __m256i *)&ssrc0[j]);
            __m256i s1 = _mm256_loadu_si256((const __m256i *)&ssrc1[j]);
            __m256i diff = _mm256_srai_epi16(
                _mm256_abs_epi16(_mm256_sub_epi16(s0, s1)), DIFF_FACTOR_LOG2);
            __m256i m = _mm256_min_epi16(
                _mm256_max_epi16(y0, _mm256_add_epi16(diff, ymask_base)),
                yAOM_BLEND_A64_MAX_ALPHA);
            m = _mm256_sub_epi16(yAOM_BLEND_A64_MAX_ALPHA, m);
            m = _mm256_packus_epi16(m, m);
            m = _mm256_permute4x64_epi64(m, _MM_SHUFFLE(0, 0, 2, 0));
            __m128i m0 = _mm256_castsi256_si128(m);
            _mm_storeu_si128((__m128i *)&mask[j], m0);
          }
          ssrc0 += src0_stride;
          ssrc1 += src1_stride;
          mask += w;
        }
      } else {
        for (int i = 0; i < h; ++i) {
          for (int j = 0; j < w; j += 16) {
            __m256i s0 = _mm256_loadu_si256((const __m256i *)&ssrc0[j]);
            __m256i s1 = _mm256_loadu_si256((const __m256i *)&ssrc1[j]);
            __m256i diff = _mm256_srai_epi16(
                _mm256_abs_epi16(_mm256_sub_epi16(s0, s1)), DIFF_FACTOR_LOG2);
            __m256i m = _mm256_min_epi16(
                _mm256_max_epi16(y0, _mm256_add_epi16(diff, ymask_base)),
                yAOM_BLEND_A64_MAX_ALPHA);
            m = _mm256_packus_epi16(m, m);
            m = _mm256_permute4x64_epi64(m, _MM_SHUFFLE(0, 0, 2, 0));
            __m128i m0 = _mm256_castsi256_si128(m);
            _mm_storeu_si128((__m128i *)&mask[j], m0);
          }
          ssrc0 += src0_stride;
          ssrc1 += src1_stride;
          mask += w;
        }
      }
    } else {
      const __m128i xshift = xx_set1_64_from_32i(bd - 8 + DIFF_FACTOR_LOG2);
      if (mask_type == DIFFWTD_38_INV) {
        for (int i = 0; i < h; ++i) {
          for (int j = 0; j < w; j += 16) {
            __m256i s0 = _mm256_loadu_si256((const __m256i *)&ssrc0[j]);
            __m256i s1 = _mm256_loadu_si256((const __m256i *)&ssrc1[j]);
            __m256i diff = _mm256_sra_epi16(
                _mm256_abs_epi16(_mm256_sub_epi16(s0, s1)), xshift);
            __m256i m = _mm256_min_epi16(
                _mm256_max_epi16(y0, _mm256_add_epi16(diff, ymask_base)),
                yAOM_BLEND_A64_MAX_ALPHA);
            m = _mm256_sub_epi16(yAOM_BLEND_A64_MAX_ALPHA, m);
            m = _mm256_packus_epi16(m, m);
            m = _mm256_permute4x64_epi64(m, _MM_SHUFFLE(0, 0, 2, 0));
            __m128i m0 = _mm256_castsi256_si128(m);
            _mm_storeu_si128((__m128i *)&mask[j], m0);
          }
          ssrc0 += src0_stride;
          ssrc1 += src1_stride;
          mask += w;
        }
      } else {
        for (int i = 0; i < h; ++i) {
          for (int j = 0; j < w; j += 16) {
            __m256i s0 = _mm256_loadu_si256((const __m256i *)&ssrc0[j]);
            __m256i s1 = _mm256_loadu_si256((const __m256i *)&ssrc1[j]);
            __m256i diff = _mm256_sra_epi16(
                _mm256_abs_epi16(_mm256_sub_epi16(s0, s1)), xshift);
            __m256i m = _mm256_min_epi16(
                _mm256_max_epi16(y0, _mm256_add_epi16(diff, ymask_base)),
                yAOM_BLEND_A64_MAX_ALPHA);
            m = _mm256_packus_epi16(m, m);
            m = _mm256_permute4x64_epi64(m, _MM_SHUFFLE(0, 0, 2, 0));
            __m128i m0 = _mm256_castsi256_si128(m);
            _mm_storeu_si128((__m128i *)&mask[j], m0);
          }
          ssrc0 += src0_stride;
          ssrc1 += src1_stride;
          mask += w;
        }
      }
    }
  }
}
