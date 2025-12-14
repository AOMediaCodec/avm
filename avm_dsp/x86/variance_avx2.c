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

#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/x86/masked_variance_intrin_ssse3.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE __m256i mm256_loadu2(const uint8_t *p0, const uint8_t *p1) {
  const __m256i d =
      _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)p1));
  return _mm256_insertf128_si256(d, _mm_loadu_si128((const __m128i *)p0), 1);
}

static INLINE __m256i mm256_loadu2_16(const uint16_t *p0, const uint16_t *p1) {
  const __m256i d =
      _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)p1));
  return _mm256_insertf128_si256(d, _mm_loadu_si128((const __m128i *)p0), 1);
}

static INLINE void comp_mask_pred_line_avx2(const __m256i s0, const __m256i s1,
                                            const __m256i a,
                                            uint8_t *comp_pred) {
  const __m256i alpha_max = _mm256_set1_epi8(AVM_BLEND_A64_MAX_ALPHA);
  const int16_t round_bits = 15 - AVM_BLEND_A64_ROUND_BITS;
  const __m256i round_offset = _mm256_set1_epi16(1 << (round_bits));

  const __m256i ma = _mm256_sub_epi8(alpha_max, a);

  const __m256i ssAL = _mm256_unpacklo_epi8(s0, s1);
  const __m256i aaAL = _mm256_unpacklo_epi8(a, ma);
  const __m256i ssAH = _mm256_unpackhi_epi8(s0, s1);
  const __m256i aaAH = _mm256_unpackhi_epi8(a, ma);

  const __m256i blendAL = _mm256_maddubs_epi16(ssAL, aaAL);
  const __m256i blendAH = _mm256_maddubs_epi16(ssAH, aaAH);
  const __m256i roundAL = _mm256_mulhrs_epi16(blendAL, round_offset);
  const __m256i roundAH = _mm256_mulhrs_epi16(blendAH, round_offset);

  const __m256i roundA = _mm256_packus_epi16(roundAL, roundAH);
  _mm256_storeu_si256((__m256i *)(comp_pred), roundA);
}

void avm_comp_mask_pred_avx2(uint8_t *comp_pred, const uint8_t *pred, int width,
                             int height, const uint8_t *ref, int ref_stride,
                             const uint8_t *mask, int mask_stride,
                             int invert_mask) {
  int i = 0;
  const uint8_t *src0 = invert_mask ? pred : ref;
  const uint8_t *src1 = invert_mask ? ref : pred;
  const int stride0 = invert_mask ? width : ref_stride;
  const int stride1 = invert_mask ? ref_stride : width;
  if (width == 8) {
    comp_mask_pred_8_ssse3(comp_pred, height, src0, stride0, src1, stride1,
                           mask, mask_stride);
  } else if (width == 16) {
    do {
      const __m256i sA0 = mm256_loadu2(src0 + stride0, src0);
      const __m256i sA1 = mm256_loadu2(src1 + stride1, src1);
      const __m256i aA = mm256_loadu2(mask + mask_stride, mask);
      src0 += (stride0 << 1);
      src1 += (stride1 << 1);
      mask += (mask_stride << 1);
      const __m256i sB0 = mm256_loadu2(src0 + stride0, src0);
      const __m256i sB1 = mm256_loadu2(src1 + stride1, src1);
      const __m256i aB = mm256_loadu2(mask + mask_stride, mask);
      src0 += (stride0 << 1);
      src1 += (stride1 << 1);
      mask += (mask_stride << 1);
      // comp_pred's stride == width == 16
      comp_mask_pred_line_avx2(sA0, sA1, aA, comp_pred);
      comp_mask_pred_line_avx2(sB0, sB1, aB, comp_pred + 32);
      comp_pred += (16 << 2);
      i += 4;
    } while (i < height);
  } else {
    do {
      for (int x = 0; x < width / 32; x++) {
        const __m256i sA0 =
            _mm256_lddqu_si256((const __m256i *)(src0 + x * 32));
        const __m256i sA1 =
            _mm256_lddqu_si256((const __m256i *)(src1 + x * 32));
        const __m256i aA = _mm256_lddqu_si256((const __m256i *)(mask + x * 32));

        comp_mask_pred_line_avx2(sA0, sA1, aA, comp_pred);
        comp_pred += 32;
      }
      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      i++;
    } while (i < height);
  }
}

static INLINE __m256i highbd_comp_mask_pred_line_avx2(const __m256i s0,
                                                      const __m256i s1,
                                                      const __m256i a) {
  const __m256i alpha_max = _mm256_set1_epi16((1 << AVM_BLEND_A64_ROUND_BITS));
  const __m256i round_const =
      _mm256_set1_epi32((1 << AVM_BLEND_A64_ROUND_BITS) >> 1);
  const __m256i a_inv = _mm256_sub_epi16(alpha_max, a);

  const __m256i s_lo = _mm256_unpacklo_epi16(s0, s1);
  const __m256i a_lo = _mm256_unpacklo_epi16(a, a_inv);
  const __m256i pred_lo = _mm256_madd_epi16(s_lo, a_lo);
  const __m256i pred_l = _mm256_srai_epi32(
      _mm256_add_epi32(pred_lo, round_const), AVM_BLEND_A64_ROUND_BITS);

  const __m256i s_hi = _mm256_unpackhi_epi16(s0, s1);
  const __m256i a_hi = _mm256_unpackhi_epi16(a, a_inv);
  const __m256i pred_hi = _mm256_madd_epi16(s_hi, a_hi);
  const __m256i pred_h = _mm256_srai_epi32(
      _mm256_add_epi32(pred_hi, round_const), AVM_BLEND_A64_ROUND_BITS);

  const __m256i comp = _mm256_packs_epi32(pred_l, pred_h);

  return comp;
}

void avm_highbd_comp_mask_pred_avx2(uint16_t *comp_pred, const uint16_t *pred,
                                    int width, int height, const uint16_t *ref,
                                    int ref_stride, const uint8_t *mask,
                                    int mask_stride, int invert_mask) {
  int i = 0;
  const uint16_t *src0 = invert_mask ? pred : ref;
  const uint16_t *src1 = invert_mask ? ref : pred;
  const int stride0 = invert_mask ? width : ref_stride;
  const int stride1 = invert_mask ? ref_stride : width;
  const __m256i zero = _mm256_setzero_si256();

  if (width == 8) {
    do {
      const __m256i s0 = mm256_loadu2_16(src0 + stride0, src0);
      const __m256i s1 = mm256_loadu2_16(src1 + stride1, src1);

      const __m128i m_l = _mm_loadl_epi64((const __m128i *)mask);
      const __m128i m_h = _mm_loadl_epi64((const __m128i *)(mask + 8));

      __m256i m = _mm256_castsi128_si256(m_l);
      m = _mm256_insertf128_si256(m, m_h, 1);
      const __m256i m_16 = _mm256_unpacklo_epi8(m, zero);

      const __m256i comp = highbd_comp_mask_pred_line_avx2(s0, s1, m_16);

      _mm_storeu_si128((__m128i *)(comp_pred), _mm256_castsi256_si128(comp));

      _mm_storeu_si128((__m128i *)(comp_pred + width),
                       _mm256_extractf128_si256(comp, 1));

      src0 += (stride0 << 1);
      src1 += (stride1 << 1);
      mask += (mask_stride << 1);
      comp_pred += (width << 1);
      i += 2;
    } while (i < height);
  } else if (width == 16) {
    do {
      const __m256i s0 = _mm256_loadu_si256((const __m256i *)(src0));
      const __m256i s1 = _mm256_loadu_si256((const __m256i *)(src1));
      const __m256i m_16 =
          _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)mask));

      const __m256i comp = highbd_comp_mask_pred_line_avx2(s0, s1, m_16);

      _mm256_storeu_si256((__m256i *)comp_pred, comp);

      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
  } else if (width == 32) {
    do {
      const __m256i s0 = _mm256_loadu_si256((const __m256i *)src0);
      const __m256i s2 = _mm256_loadu_si256((const __m256i *)(src0 + 16));
      const __m256i s1 = _mm256_loadu_si256((const __m256i *)src1);
      const __m256i s3 = _mm256_loadu_si256((const __m256i *)(src1 + 16));

      const __m256i m01_16 =
          _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)mask));
      const __m256i m23_16 =
          _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)(mask + 16)));

      const __m256i comp = highbd_comp_mask_pred_line_avx2(s0, s1, m01_16);
      const __m256i comp1 = highbd_comp_mask_pred_line_avx2(s2, s3, m23_16);

      _mm256_storeu_si256((__m256i *)comp_pred, comp);
      _mm256_storeu_si256((__m256i *)(comp_pred + 16), comp1);

      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
  } else if (width > 32) {
    do {
      const int num_32_subs = (width >> 5);
      for (int j = 0; j < num_32_subs; j++) {
        const __m256i s0 = _mm256_loadu_si256((const __m256i *)(src0 + j * 32));
        const __m256i s2 =
            _mm256_loadu_si256((const __m256i *)(src0 + 16 + j * 32));
        const __m256i s1 = _mm256_loadu_si256((const __m256i *)(src1 + j * 32));
        const __m256i s3 =
            _mm256_loadu_si256((const __m256i *)(src1 + 16 + j * 32));

        const __m256i m01_16 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i *)(mask + j * 32)));
        const __m256i m23_16 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i *)(mask + 16 + j * 32)));

        const __m256i comp = highbd_comp_mask_pred_line_avx2(s0, s1, m01_16);
        const __m256i comp1 = highbd_comp_mask_pred_line_avx2(s2, s3, m23_16);

        _mm256_storeu_si256((__m256i *)(comp_pred + j * 32), comp);
        _mm256_storeu_si256((__m256i *)(comp_pred + 16 + j * 32), comp1);
      }

      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
  }
}
