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

#include <smmintrin.h>  // SSE4.1
#include <immintrin.h>  // AVX2

#include <assert.h>

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_dsp/aom_dsp_common.h"

#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"
#include "aom_dsp/x86/blend_sse4.h"
#include "aom_dsp/x86/blend_mask_sse4.h"

#include "config/aom_dsp_rtcd.h"

//////////////////////////////////////////////////////////////////////////////
// aom_highbd_blend_a64_d16_mask_avx2()
//////////////////////////////////////////////////////////////////////////////

static INLINE void highbd_blend_a64_d16_mask_w4_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const __m256i *mask0,
    const __m256i *round_offset, int shift, const __m256i *clip_low,
    const __m256i *clip_high, const __m256i *mask_max) {
  // Load 4x u16 pixels from each of 4 rows from each source
  const __m256i s0 = _mm256_set_epi64x(*(uint64_t *)(src0 + 3 * src0_stride),
                                       *(uint64_t *)(src0 + 2 * src0_stride),
                                       *(uint64_t *)(src0 + 1 * src0_stride),
                                       *(uint64_t *)(src0 + 0 * src0_stride));
  const __m256i s1 = _mm256_set_epi64x(*(uint64_t *)(src1 + 3 * src1_stride),
                                       *(uint64_t *)(src1 + 2 * src1_stride),
                                       *(uint64_t *)(src1 + 1 * src1_stride),
                                       *(uint64_t *)(src1 + 0 * src1_stride));
  // Generate the inverse mask
  const __m256i mask1 = _mm256_sub_epi16(*mask_max, *mask0);

  // Multiply each mask by the respective source
  const __m256i mul0_highs = _mm256_mulhi_epu16(*mask0, s0);
  const __m256i mul0_lows = _mm256_mullo_epi16(*mask0, s0);
  const __m256i mul0h = _mm256_unpackhi_epi16(mul0_lows, mul0_highs);
  const __m256i mul0l = _mm256_unpacklo_epi16(mul0_lows, mul0_highs);
  // Note that AVX2 unpack orders 64-bit words as [3 1] [2 0] to keep within
  // lanes Later, packs does the same again which cancels this out with no need
  // for a permute.  The intermediate values being reordered makes no difference

  const __m256i mul1_highs = _mm256_mulhi_epu16(mask1, s1);
  const __m256i mul1_lows = _mm256_mullo_epi16(mask1, s1);
  const __m256i mul1h = _mm256_unpackhi_epi16(mul1_lows, mul1_highs);
  const __m256i mul1l = _mm256_unpacklo_epi16(mul1_lows, mul1_highs);

  const __m256i sumh = _mm256_add_epi32(mul0h, mul1h);
  const __m256i suml = _mm256_add_epi32(mul0l, mul1l);

  const __m256i roundh =
      _mm256_srai_epi32(_mm256_sub_epi32(sumh, *round_offset), shift);
  const __m256i roundl =
      _mm256_srai_epi32(_mm256_sub_epi32(suml, *round_offset), shift);

  const __m256i pack = _mm256_packs_epi32(roundl, roundh);
  const __m256i clip =
      _mm256_min_epi16(_mm256_max_epi16(pack, *clip_low), *clip_high);

  // _mm256_extract_epi64 doesn't exist on x86, so do it the old-fashioned way:
  const __m128i cliph = _mm256_extracti128_si256(clip, 1);
  xx_storel_64(dst + 3 * dst_stride, _mm_srli_si128(cliph, 8));
  xx_storel_64(dst + 2 * dst_stride, cliph);
  const __m128i clipl = _mm256_castsi256_si128(clip);
  xx_storel_64(dst + 1 * dst_stride, _mm_srli_si128(clipl, 8));
  xx_storel_64(dst + 0 * dst_stride, clipl);
}

static INLINE void highbd_blend_a64_d16_mask_subw0_subh0_w4_avx2(
    uint16_t *dst, uint32_t dst_stride, const CONV_BUF_TYPE *src0,
    uint32_t src0_stride, const CONV_BUF_TYPE *src1, uint32_t src1_stride,
    const uint8_t *mask, uint32_t mask_stride, int h,
    const __m256i *round_offset, int shift, const __m256i *clip_low,
    const __m256i *clip_high, const __m256i *mask_max) {
  do {
    // Load 8x u8 pixels from each of 4 rows of the mask, pad each to u16
    const __m128i mask08 = _mm_set_epi32(*(uint32_t *)(mask + 3 * mask_stride),
                                         *(uint32_t *)(mask + 2 * mask_stride),
                                         *(uint32_t *)(mask + 1 * mask_stride),
                                         *(uint32_t *)(mask + 0 * mask_stride));
    const __m256i mask0 = _mm256_cvtepu8_epi16(mask08);

    highbd_blend_a64_d16_mask_w4_avx2(dst, dst_stride, src0, src0_stride, src1,
                                      src1_stride, &mask0, round_offset, shift,
                                      clip_low, clip_high, mask_max);

    dst += dst_stride * 4;
    src0 += src0_stride * 4;
    src1 += src1_stride * 4;
    mask += mask_stride * 4;
  } while (h -= 4);
}

static INLINE void highbd_blend_a64_d16_mask_subw1_subh1_w4_avx2(
    uint16_t *dst, uint32_t dst_stride, const CONV_BUF_TYPE *src0,
    uint32_t src0_stride, const CONV_BUF_TYPE *src1, uint32_t src1_stride,
    const uint8_t *mask, uint32_t mask_stride, int h,
    const __m256i *round_offset, int shift, const __m256i *clip_low,
    const __m256i *clip_high, const __m256i *mask_max) {
  const __m256i one_b = _mm256_set1_epi8(1);
  const __m256i two_w = _mm256_set1_epi16(2);
  do {
    // Load 8 pixels from each of 8 rows of mask,
    // (saturating) add together rows then use madd to add adjacent pixels
    // Finally, divide each value by 4 (with rounding)
    const __m256i m0246 =
        _mm256_set_epi64x(*(uint64_t *)(mask + 6 * mask_stride),
                          *(uint64_t *)(mask + 4 * mask_stride),
                          *(uint64_t *)(mask + 2 * mask_stride),
                          *(uint64_t *)(mask + 0 * mask_stride));
    const __m256i m1357 =
        _mm256_set_epi64x(*(uint64_t *)(mask + 7 * mask_stride),
                          *(uint64_t *)(mask + 5 * mask_stride),
                          *(uint64_t *)(mask + 3 * mask_stride),
                          *(uint64_t *)(mask + 1 * mask_stride));
    const __m256i addrows = _mm256_adds_epu8(m0246, m1357);
    const __m256i adjacent = _mm256_maddubs_epi16(addrows, one_b);
    const __m256i mask0 =
        _mm256_srli_epi16(_mm256_add_epi16(adjacent, two_w), 2);

    highbd_blend_a64_d16_mask_w4_avx2(dst, dst_stride, src0, src0_stride, src1,
                                      src1_stride, &mask0, round_offset, shift,
                                      clip_low, clip_high, mask_max);

    dst += dst_stride * 4;
    src0 += src0_stride * 4;
    src1 += src1_stride * 4;
    mask += mask_stride * 8;
  } while (h -= 4);
}

static INLINE void highbd_blend_a64_d16_mask_w8_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const __m256i *mask0a,
    const __m256i *mask0b, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  // Load 8x u16 pixels from each of 4 rows from each source
  const __m256i s0a =
      yy_loadu2_128(src0 + 0 * src0_stride, src0 + 1 * src0_stride);
  const __m256i s0b =
      yy_loadu2_128(src0 + 2 * src0_stride, src0 + 3 * src0_stride);
  const __m256i s1a =
      yy_loadu2_128(src1 + 0 * src1_stride, src1 + 1 * src1_stride);
  const __m256i s1b =
      yy_loadu2_128(src1 + 2 * src1_stride, src1 + 3 * src1_stride);

  // Generate inverse masks
  const __m256i mask1a = _mm256_sub_epi16(*mask_max, *mask0a);
  const __m256i mask1b = _mm256_sub_epi16(*mask_max, *mask0b);

  // Multiply sources by respective masks
  const __m256i mul0a_highs = _mm256_mulhi_epu16(*mask0a, s0a);
  const __m256i mul0a_lows = _mm256_mullo_epi16(*mask0a, s0a);
  const __m256i mul0ah = _mm256_unpackhi_epi16(mul0a_lows, mul0a_highs);
  const __m256i mul0al = _mm256_unpacklo_epi16(mul0a_lows, mul0a_highs);
  // Note that AVX2 unpack orders 64-bit words as [3 1] [2 0] to keep within
  // lanes Later, packs does the same again which cancels this out with no need
  // for a permute.  The intermediate values being reordered makes no difference

  const __m256i mul1a_highs = _mm256_mulhi_epu16(mask1a, s1a);
  const __m256i mul1a_lows = _mm256_mullo_epi16(mask1a, s1a);
  const __m256i mul1ah = _mm256_unpackhi_epi16(mul1a_lows, mul1a_highs);
  const __m256i mul1al = _mm256_unpacklo_epi16(mul1a_lows, mul1a_highs);

  const __m256i sumah = _mm256_add_epi32(mul0ah, mul1ah);
  const __m256i sumal = _mm256_add_epi32(mul0al, mul1al);

  const __m256i mul0b_highs = _mm256_mulhi_epu16(*mask0b, s0b);
  const __m256i mul0b_lows = _mm256_mullo_epi16(*mask0b, s0b);
  const __m256i mul0bh = _mm256_unpackhi_epi16(mul0b_lows, mul0b_highs);
  const __m256i mul0bl = _mm256_unpacklo_epi16(mul0b_lows, mul0b_highs);

  const __m256i mul1b_highs = _mm256_mulhi_epu16(mask1b, s1b);
  const __m256i mul1b_lows = _mm256_mullo_epi16(mask1b, s1b);
  const __m256i mul1bh = _mm256_unpackhi_epi16(mul1b_lows, mul1b_highs);
  const __m256i mul1bl = _mm256_unpacklo_epi16(mul1b_lows, mul1b_highs);

  const __m256i sumbh = _mm256_add_epi32(mul0bh, mul1bh);
  const __m256i sumbl = _mm256_add_epi32(mul0bl, mul1bl);

  // Divide down each result, with rounding
  const __m256i roundah =
      _mm256_srai_epi32(_mm256_sub_epi32(sumah, *round_offset), shift);
  const __m256i roundal =
      _mm256_srai_epi32(_mm256_sub_epi32(sumal, *round_offset), shift);
  const __m256i roundbh =
      _mm256_srai_epi32(_mm256_sub_epi32(sumbh, *round_offset), shift);
  const __m256i roundbl =
      _mm256_srai_epi32(_mm256_sub_epi32(sumbl, *round_offset), shift);

  // Pack each i32 down to an i16 with saturation, then clip to valid range
  const __m256i packa = _mm256_packs_epi32(roundal, roundah);
  const __m256i clipa =
      _mm256_min_epi16(_mm256_max_epi16(packa, *clip_low), *clip_high);
  const __m256i packb = _mm256_packs_epi32(roundbl, roundbh);
  const __m256i clipb =
      _mm256_min_epi16(_mm256_max_epi16(packb, *clip_low), *clip_high);

  // Store 8x u16 pixels to each of 4 rows in the destination
  yy_storeu2_128(dst + 0 * dst_stride, dst + 1 * dst_stride, clipa);
  yy_storeu2_128(dst + 2 * dst_stride, dst + 3 * dst_stride, clipb);
}

static INLINE void highbd_blend_a64_d16_mask_subw0_subh0_w8_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const uint8_t *mask,
    int mask_stride, int h, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  do {
    // Load 8x u8 pixels from each of 4 rows in the mask
    const __m128i mask0a8 =
        _mm_set_epi64x(*(uint64_t *)mask, *(uint64_t *)(mask + mask_stride));
    const __m128i mask0b8 =
        _mm_set_epi64x(*(uint64_t *)(mask + 2 * mask_stride),
                       *(uint64_t *)(mask + 3 * mask_stride));
    const __m256i mask0a = _mm256_cvtepu8_epi16(mask0a8);
    const __m256i mask0b = _mm256_cvtepu8_epi16(mask0b8);

    highbd_blend_a64_d16_mask_w8_avx2(
        dst, dst_stride, src0, src0_stride, src1, src1_stride, &mask0a, &mask0b,
        round_offset, shift, clip_low, clip_high, mask_max);

    dst += dst_stride * 4;
    src0 += src0_stride * 4;
    src1 += src1_stride * 4;
    mask += mask_stride * 4;
  } while (h -= 4);
}

static INLINE void highbd_blend_a64_d16_mask_subw1_subh1_w8_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const uint8_t *mask,
    int mask_stride, int h, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  const __m256i one_b = _mm256_set1_epi8(1);
  const __m256i two_w = _mm256_set1_epi16(2);
  do {
    // Load 16x u8 pixels from each of 8 rows in the mask,
    // (saturating) add together rows then use madd to add adjacent pixels
    // Finally, divide each value by 4 (with rounding)
    const __m256i m02 =
        yy_loadu2_128(mask + 0 * mask_stride, mask + 2 * mask_stride);
    const __m256i m13 =
        yy_loadu2_128(mask + 1 * mask_stride, mask + 3 * mask_stride);
    const __m256i m0123 =
        _mm256_maddubs_epi16(_mm256_adds_epu8(m02, m13), one_b);
    const __m256i mask_0a =
        _mm256_srli_epi16(_mm256_add_epi16(m0123, two_w), 2);
    const __m256i m46 =
        yy_loadu2_128(mask + 4 * mask_stride, mask + 6 * mask_stride);
    const __m256i m57 =
        yy_loadu2_128(mask + 5 * mask_stride, mask + 7 * mask_stride);
    const __m256i m4567 =
        _mm256_maddubs_epi16(_mm256_adds_epu8(m46, m57), one_b);
    const __m256i mask_0b =
        _mm256_srli_epi16(_mm256_add_epi16(m4567, two_w), 2);

    highbd_blend_a64_d16_mask_w8_avx2(
        dst, dst_stride, src0, src0_stride, src1, src1_stride, &mask_0a,
        &mask_0b, round_offset, shift, clip_low, clip_high, mask_max);

    dst += dst_stride * 4;
    src0 += src0_stride * 4;
    src1 += src1_stride * 4;
    mask += mask_stride * 8;
  } while (h -= 4);
}

static INLINE void highbd_blend_a64_d16_mask_w16_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const __m256i *mask0a,
    const __m256i *mask0b, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  // Load 16x pixels from each of 2 rows from each source
  const __m256i s0a = yy_loadu_256(src0);
  const __m256i s0b = yy_loadu_256(src0 + src0_stride);
  const __m256i s1a = yy_loadu_256(src1);
  const __m256i s1b = yy_loadu_256(src1 + src1_stride);

  // Calculate inverse masks
  const __m256i mask1a = _mm256_sub_epi16(*mask_max, *mask0a);
  const __m256i mask1b = _mm256_sub_epi16(*mask_max, *mask0b);

  // Multiply each source by appropriate mask
  const __m256i mul0a_highs = _mm256_mulhi_epu16(*mask0a, s0a);
  const __m256i mul0a_lows = _mm256_mullo_epi16(*mask0a, s0a);
  const __m256i mul0ah = _mm256_unpackhi_epi16(mul0a_lows, mul0a_highs);
  const __m256i mul0al = _mm256_unpacklo_epi16(mul0a_lows, mul0a_highs);
  // Note that AVX2 unpack orders 64-bit words as [3 1] [2 0] to keep within
  // lanes Later, packs does the same again which cancels this out with no need
  // for a permute.  The intermediate values being reordered makes no difference

  const __m256i mul1a_highs = _mm256_mulhi_epu16(mask1a, s1a);
  const __m256i mul1a_lows = _mm256_mullo_epi16(mask1a, s1a);
  const __m256i mul1ah = _mm256_unpackhi_epi16(mul1a_lows, mul1a_highs);
  const __m256i mul1al = _mm256_unpacklo_epi16(mul1a_lows, mul1a_highs);

  const __m256i mulah = _mm256_add_epi32(mul0ah, mul1ah);
  const __m256i mulal = _mm256_add_epi32(mul0al, mul1al);

  const __m256i mul0b_highs = _mm256_mulhi_epu16(*mask0b, s0b);
  const __m256i mul0b_lows = _mm256_mullo_epi16(*mask0b, s0b);
  const __m256i mul0bh = _mm256_unpackhi_epi16(mul0b_lows, mul0b_highs);
  const __m256i mul0bl = _mm256_unpacklo_epi16(mul0b_lows, mul0b_highs);

  const __m256i mul1b_highs = _mm256_mulhi_epu16(mask1b, s1b);
  const __m256i mul1b_lows = _mm256_mullo_epi16(mask1b, s1b);
  const __m256i mul1bh = _mm256_unpackhi_epi16(mul1b_lows, mul1b_highs);
  const __m256i mul1bl = _mm256_unpacklo_epi16(mul1b_lows, mul1b_highs);

  const __m256i mulbh = _mm256_add_epi32(mul0bh, mul1bh);
  const __m256i mulbl = _mm256_add_epi32(mul0bl, mul1bl);

  const __m256i resah =
      _mm256_srai_epi32(_mm256_sub_epi32(mulah, *round_offset), shift);
  const __m256i resal =
      _mm256_srai_epi32(_mm256_sub_epi32(mulal, *round_offset), shift);
  const __m256i resbh =
      _mm256_srai_epi32(_mm256_sub_epi32(mulbh, *round_offset), shift);
  const __m256i resbl =
      _mm256_srai_epi32(_mm256_sub_epi32(mulbl, *round_offset), shift);

  // Signed saturating pack from i32 to i16:
  const __m256i packa = _mm256_packs_epi32(resal, resah);
  const __m256i packb = _mm256_packs_epi32(resbl, resbh);

  // Clip the values to the valid range
  const __m256i clipa =
      _mm256_min_epi16(_mm256_max_epi16(packa, *clip_low), *clip_high);
  const __m256i clipb =
      _mm256_min_epi16(_mm256_max_epi16(packb, *clip_low), *clip_high);

  // Store 16 pixels
  yy_storeu_256(dst, clipa);
  yy_storeu_256(dst + dst_stride, clipb);
}

static INLINE void highbd_blend_a64_d16_mask_subw0_subh0_w16_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const uint8_t *mask,
    int mask_stride, int h, int w, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  for (int i = 0; i < h; i += 2) {
    for (int j = 0; j < w; j += 16) {
      // Load 16x u8 alpha-mask values from each of two rows and pad to u16
      const __m128i masks_a8 = xx_loadu_128(mask + j);
      const __m128i masks_b8 = xx_loadu_128(mask + mask_stride + j);
      const __m256i mask0a = _mm256_cvtepu8_epi16(masks_a8);
      const __m256i mask0b = _mm256_cvtepu8_epi16(masks_b8);

      highbd_blend_a64_d16_mask_w16_avx2(
          dst + j, dst_stride, src0 + j, src0_stride, src1 + j, src1_stride,
          &mask0a, &mask0b, round_offset, shift, clip_low, clip_high, mask_max);
    }
    dst += dst_stride * 2;
    src0 += src0_stride * 2;
    src1 += src1_stride * 2;
    mask += mask_stride * 2;
  }
}

static INLINE void highbd_blend_a64_d16_mask_subw1_subh1_w16_avx2(
    uint16_t *dst, int dst_stride, const CONV_BUF_TYPE *src0, int src0_stride,
    const CONV_BUF_TYPE *src1, int src1_stride, const uint8_t *mask,
    int mask_stride, int h, int w, const __m256i *round_offset, int shift,
    const __m256i *clip_low, const __m256i *clip_high,
    const __m256i *mask_max) {
  const __m256i one_b = _mm256_set1_epi8(1);
  const __m256i two_w = _mm256_set1_epi16(2);
  for (int i = 0; i < h; i += 2) {
    for (int j = 0; j < w; j += 16) {
      // Load 32x u8 alpha-mask values from each of four rows
      // (saturating) add pairs of rows, then use madd to add adjacent values
      // Finally, divide down each result with rounding
      const __m256i m0 = yy_loadu_256(mask + 0 * mask_stride + 2 * j);
      const __m256i m1 = yy_loadu_256(mask + 1 * mask_stride + 2 * j);
      const __m256i m2 = yy_loadu_256(mask + 2 * mask_stride + 2 * j);
      const __m256i m3 = yy_loadu_256(mask + 3 * mask_stride + 2 * j);

      const __m256i m01_8 = _mm256_adds_epu8(m0, m1);
      const __m256i m23_8 = _mm256_adds_epu8(m2, m3);

      const __m256i m01 = _mm256_maddubs_epi16(m01_8, one_b);
      const __m256i m23 = _mm256_maddubs_epi16(m23_8, one_b);

      const __m256i mask0a = _mm256_srli_epi16(_mm256_add_epi16(m01, two_w), 2);
      const __m256i mask0b = _mm256_srli_epi16(_mm256_add_epi16(m23, two_w), 2);

      highbd_blend_a64_d16_mask_w16_avx2(
          dst + j, dst_stride, src0 + j, src0_stride, src1 + j, src1_stride,
          &mask0a, &mask0b, round_offset, shift, clip_low, clip_high, mask_max);
    }
    dst += dst_stride * 2;
    src0 += src0_stride * 2;
    src1 += src1_stride * 2;
    mask += mask_stride * 4;
  }
}

void aom_highbd_blend_a64_d16_mask_avx2(
    uint16_t *dst, uint32_t dst_stride, const CONV_BUF_TYPE *src0,
    uint32_t src0_stride, const CONV_BUF_TYPE *src1, uint32_t src1_stride,
    const uint8_t *mask, uint32_t mask_stride, int w, int h, int subw, int subh,
    ConvolveParams *conv_params, const int bd) {
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int32_t round_offset =
      ((1 << (round_bits + bd)) + (1 << (round_bits + bd - 1)) -
       (1 << (round_bits - 1)))
      << AOM_BLEND_A64_ROUND_BITS;
  const __m256i v_round_offset = _mm256_set1_epi32(round_offset);
  const int shift = round_bits + AOM_BLEND_A64_ROUND_BITS;

  const __m256i clip_low = _mm256_set1_epi16(0);
  const __m256i clip_high = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i mask_max = _mm256_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);

  assert(IMPLIES((void *)src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES((void *)src1 == dst, src1_stride == dst_stride));

  assert(h >= 4);
  assert(w >= 4);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  if (subw == 0 && subh == 0) {
    switch (w) {
      case 4:
        highbd_blend_a64_d16_mask_subw0_subh0_w4_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
      case 8:
        highbd_blend_a64_d16_mask_subw0_subh0_w8_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
      default:  // >= 16
        highbd_blend_a64_d16_mask_subw0_subh0_w16_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, w, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
    }

  } else if (subw == 1 && subh == 1) {
    switch (w) {
      case 4:
        highbd_blend_a64_d16_mask_subw1_subh1_w4_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
      case 8:
        highbd_blend_a64_d16_mask_subw1_subh1_w8_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
      default:  // >= 16
        highbd_blend_a64_d16_mask_subw1_subh1_w16_avx2(
            dst, dst_stride, src0, src0_stride, src1, src1_stride, mask,
            mask_stride, h, w, &v_round_offset, shift, &clip_low, &clip_high,
            &mask_max);
        break;
    }
  } else {
    // Sub-sampling in only one axis doesn't seem to happen very much, so fall
    // back to the vanilla C implementation instead of having all the optimised
    // code for these.
    aom_highbd_blend_a64_d16_mask_c(dst, dst_stride, src0, src0_stride, src1,
                                    src1_stride, mask, mask_stride, w, h, subw,
                                    subh, conv_params, bd);
  }
}
