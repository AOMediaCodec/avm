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

#include "avm/avm_integer.h"
#include "avm_dsp/blend.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"
#include "av2/common/blockd.h"

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
  const __m256i y64 = _mm256_set1_epi16(AVM_BLEND_A64_MAX_ALPHA);
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
  }
}

static INLINE void build_compound_diffwtd_mask_d16_inv_avx2(
    uint8_t *mask, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, int h, int w, int shift) {
  const int mask_base = 38;
  const __m256i _r = _mm256_set1_epi16((1 << shift) >> 1);
  const __m256i y38 = _mm256_set1_epi16(mask_base);
  const __m256i y64 = _mm256_set1_epi16(AVM_BLEND_A64_MAX_ALPHA);
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
  }
}

void av2_build_compound_diffwtd_mask_d16_avx2(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w,
    ConvolveParams *conv_params, int bd) {
  const int shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1 + (bd - 8);
  // When rounding constant is added, there is a possibility of overflow.
  // However that much precision is not required. Code should very well work for
  // other values of DIFF_FACTOR_LOG2 and AVM_BLEND_A64_MAX_ALPHA as well. But
  // there is a possibility of corner case bugs.
  assert(DIFF_FACTOR_LOG2 == 4);
  assert(AVM_BLEND_A64_MAX_ALPHA == 64);

  if (mask_type == DIFFWTD_38) {
    build_compound_diffwtd_mask_d16_avx2(mask, src0, src0_stride, src1,
                                         src1_stride, h, w, shift);
  } else {
    build_compound_diffwtd_mask_d16_inv_avx2(mask, src0, src0_stride, src1,
                                             src1_stride, h, w, shift);
  }
}

void av2_build_compound_diffwtd_mask_highbd_avx2(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const uint16_t *ssrc0,
    int src0_stride, const uint16_t *ssrc1, int src1_stride, int h, int w,
    int bd) {
  if (w < 16) {
    av2_build_compound_diffwtd_mask_highbd_ssse3(
        mask, mask_type, ssrc0, src0_stride, ssrc1, src1_stride, h, w, bd);
  } else {
    assert(mask_type == DIFFWTD_38 || mask_type == DIFFWTD_38_INV);
    assert(bd >= 8);
    assert((w % 16) == 0);
    const __m256i y0 = _mm256_setzero_si256();
    const __m256i yAVM_BLEND_A64_MAX_ALPHA =
        _mm256_set1_epi16(AVM_BLEND_A64_MAX_ALPHA);
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
                yAVM_BLEND_A64_MAX_ALPHA);
            m = _mm256_sub_epi16(yAVM_BLEND_A64_MAX_ALPHA, m);
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
                yAVM_BLEND_A64_MAX_ALPHA);
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
                yAVM_BLEND_A64_MAX_ALPHA);
            m = _mm256_sub_epi16(yAVM_BLEND_A64_MAX_ALPHA, m);
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
                yAVM_BLEND_A64_MAX_ALPHA);
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

static const uint8_t refinemv_pad_left[14][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 3, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 4, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 5, 5, 5, 5, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 6, 6, 6, 6, 6, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 7, 7, 7, 7, 7, 7, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 11, 12, 13, 14, 15 },
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 12, 13, 14, 15 },
  { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 13, 14, 15 },
  { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 14, 15 },
  { 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 15 },
};

static const uint8_t refinemv_pad_right_bw_15[14][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 11, 11, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15 },
  { 0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 15 },
  { 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 15 },
  { 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 15 },
  { 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 15 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 15 },
};

static const uint8_t refinemv_pad_right_bw_11[10][16] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 11, 12, 13, 14, 15 },
  { 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 11, 12, 13, 14, 15 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 11, 12, 13, 14, 15 },
};

DECLARE_ALIGNED(32, static const uint8_t, pad_mc_border_shuffle_pattern[32]) = {
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

DECLARE_ALIGNED(32, static const uint8_t, pad_mc_border_arrange_bytes[32]) = {
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15
};

// AVX2 implementation for refinemv_highbd_pad_mc_border_c for b_w = 15.
void refinemv_highbd_pad_mc_border_avx2(const uint16_t *src, int src_stride,
                                        uint16_t *dst, int dst_stride, int x0,
                                        int y0, int b_w, int b_h,
                                        const ReferenceArea *ref_area) {
  if (b_w != 15 && b_w != 11) {
    refinemv_highbd_pad_mc_border_c(src, src_stride, dst, dst_stride, x0, y0,
                                    b_w, b_h, ref_area);
    return;
  }

  assert(b_w == 15 || b_w == 11);
  const int ref_x0 = ref_area->pad_block.x0;
  const int ref_y0 = ref_area->pad_block.y0;
  const int ref_x1 = ref_area->pad_block.x1;
  const int ref_y1 = ref_area->pad_block.y1;

  // Get a pointer to the start of the real data for this row.
  const uint16_t *ref_row = src - x0 - y0 * src_stride;

  if (y0 >= ref_area->pad_block.y1)
    ref_row += (ref_area->pad_block.y1 - 1) * src_stride;
  else if (y0 >= ref_area->pad_block.y0)
    ref_row += y0 * src_stride;
  else
    ref_row += ref_area->pad_block.y0 * src_stride;

  int left = x0 < ref_x0 ? ref_x0 - x0 : 0;
  if (left > b_w) left = b_w;
  int right = (x0 + b_w > ref_x1) ? (x0 + b_w - ref_x1) : 0;
  if (right > b_w) right = b_w;

  if (left < (b_w - 1) && right < (b_w - 1) && (left != 0 || right != 0)) {
    const __m128i shuffle_left =
        _mm_loadu_si128((__m128i *)refinemv_pad_left[left]);
    const __m256i shuffle_reg_left = _mm256_inserti128_si256(
        _mm256_castsi128_si256(shuffle_left), shuffle_left, 1);

    const uint8_t *refinemv_pad_right = b_w == 15
                                            ? refinemv_pad_right_bw_15[right]
                                            : refinemv_pad_right_bw_11[right];

    const __m128i shuffle_right =
        _mm_loadu_si128((__m128i *)refinemv_pad_right);
    const __m256i shuffle_reg_right = _mm256_inserti128_si256(
        _mm256_castsi128_si256(shuffle_right), shuffle_right, 1);

    const __m256i shuffle_input_reg =
        _mm256_load_si256((__m256i *)pad_mc_border_arrange_bytes);
    const __m256i shuffle_output_reg =
        _mm256_load_si256((__m256i *)pad_mc_border_shuffle_pattern);
    __m256i out_reg;
    do {
      const __m256i src_0 = _mm256_loadu_si256((__m256i *)(ref_row + x0));

      const __m256i src_01 = _mm256_shuffle_epi8(src_0, shuffle_input_reg);
      __m256i src_reg = _mm256_permute4x64_epi64(src_01, 0xD8);

      src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_left);
      src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_right);

      out_reg = _mm256_shuffle_epi8(_mm256_permute4x64_epi64(src_reg, 0xD8),
                                    shuffle_output_reg);
      do {
        _mm256_storeu_si256((__m256i *)dst, out_reg);
        dst += dst_stride;
        ++y0;
        --b_h;
      } while ((y0 <= ref_y0 || y0 >= ref_y1) && b_h);
      ref_row += src_stride;
    } while (b_h);
  } else if (left == 0 && right == 0) {
    do {
      const __m256i src_0 = _mm256_loadu_si256((__m256i *)(ref_row + x0));
      _mm256_storeu_si256((__m256i *)dst, src_0);
      dst += dst_stride;
      ++y0;
      if (y0 > ref_y0 && y0 < ref_y1) ref_row += src_stride;
    } while (--b_h);
  } else {
    const uint16_t *cur_ref_row =
        (left >= (b_w - 1)) ? (ref_row + ref_x0) : (ref_row + ref_x1 - 1);
    do {
      const __m256i src_0 = _mm256_set1_epi16(cur_ref_row[0]);
      _mm256_storeu_si256((__m256i *)dst, src_0);
      dst += dst_stride;
      ++y0;
      if (y0 > ref_y0 && y0 < ref_y1) cur_ref_row += src_stride;
    } while (--b_h);
  }
}
