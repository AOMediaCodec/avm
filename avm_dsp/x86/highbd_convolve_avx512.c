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
#include <immintrin.h>
#include <string.h>

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/x86/convolve.h"
#include "avm_dsp/x86/convolve_avx512.h"
#include "avm_dsp/x86/synonyms.h"

static void highbd_convolve_x_sr_avx512_shuffle(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn,
    ConvolveParams *conv_params, int bd) {
  int i = 0;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const uint16_t *const src_ptr = src - fo_horiz;

  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m512i s[4], coeffs_x[4];

  const __m512i round_const_x =
      _mm512_set1_epi32(((1 << conv_params->round_0) >> 1));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const int bits = FILTER_BITS - conv_params->round_0;
  const __m128i round_shift_bits = _mm_cvtsi32_si128(bits);
  const __m512i round_const_bits = _mm512_set1_epi32((1 << bits) >> 1);
  const __m512i clip_pixel =
      _mm512_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  const __m512i zero = _mm512_setzero_si512();

  assert(bits >= 0);

  prepare_coeffs_avx512(filter_params_x, subpel_x_qn, coeffs_x);

  for (; i + 4 <= h; i += 4) {
    for (int j = 0; j < w; j += 8) {
      __m512i r0, r1;
      load_4rows(&src_ptr[i * src_stride], src_stride, j, &r0, &r1);

      // Even output pixels.
      s[0] = r0;
      s[1] = _mm512_alignr_epi8(r1, r0, 4);
      s[2] = _mm512_alignr_epi8(r1, r0, 8);
      s[3] = _mm512_alignr_epi8(r1, r0, 12);

      __m512i res_even = convolve_avx512(s, coeffs_x);
      res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_x),
                                  round_shift_x);

      // Odd output pixels.
      s[0] = _mm512_alignr_epi8(r1, r0, 2);
      s[1] = _mm512_alignr_epi8(r1, r0, 6);
      s[2] = _mm512_alignr_epi8(r1, r0, 10);
      s[3] = _mm512_alignr_epi8(r1, r0, 14);

      __m512i res_odd = convolve_avx512(s, coeffs_x);
      res_odd = _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_x),
                                 round_shift_x);

      res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_bits),
                                  round_shift_bits);
      res_odd = _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_bits),
                                 round_shift_bits);

      const __m512i res_even1 = _mm512_packs_epi32(res_even, res_even);
      const __m512i res_odd1 = _mm512_packs_epi32(res_odd, res_odd);

      __m512i res = _mm512_unpacklo_epi16(res_even1, res_odd1);
      res = _mm512_min_epi16(res, clip_pixel);
      res = _mm512_max_epi16(res, zero);

      const __m128i out0 = _mm512_extracti32x4_epi32(res, 0);
      const __m128i out1 = _mm512_extracti32x4_epi32(res, 1);
      const __m128i out2 = _mm512_extracti32x4_epi32(res, 2);
      const __m128i out3 = _mm512_extracti32x4_epi32(res, 3);
      if (w - j > 4) {
        _mm_storeu_si128((__m128i *)&dst[(i + 0) * dst_stride + j], out0);
        _mm_storeu_si128((__m128i *)&dst[(i + 1) * dst_stride + j], out1);
        _mm_storeu_si128((__m128i *)&dst[(i + 2) * dst_stride + j], out2);
        _mm_storeu_si128((__m128i *)&dst[(i + 3) * dst_stride + j], out3);
      } else if (w - j == 4) {
        _mm_storel_epi64((__m128i *)&dst[(i + 0) * dst_stride + j], out0);
        _mm_storel_epi64((__m128i *)&dst[(i + 1) * dst_stride + j], out1);
        _mm_storel_epi64((__m128i *)&dst[(i + 2) * dst_stride + j], out2);
        _mm_storel_epi64((__m128i *)&dst[(i + 3) * dst_stride + j], out3);
      } else {
        xx_storel_32((__m128i *)&dst[(i + 0) * dst_stride + j], out0);
        xx_storel_32((__m128i *)&dst[(i + 1) * dst_stride + j], out1);
        xx_storel_32((__m128i *)&dst[(i + 2) * dst_stride + j], out2);
        xx_storel_32((__m128i *)&dst[(i + 3) * dst_stride + j], out3);
      }
    }
  }

  // Handle the tail rows.
  if (i < h) {
    av2_highbd_convolve_x_sr_avx2(
        src + i * src_stride, src_stride, dst + i * dst_stride, dst_stride, w,
        h - i, filter_params_x, subpel_x_qn, conv_params, bd);
  }
}

static void highbd_convolve_x_sr_avx512_loadonly(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn,
    ConvolveParams *conv_params, int bd) {
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const uint16_t *const src_ptr = src - fo_horiz;

  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m512i s[4], coeffs_x[4];

  const __m512i round_const_x =
      _mm512_set1_epi32(((1 << conv_params->round_0) >> 1));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const int bits = FILTER_BITS - conv_params->round_0;
  const __m128i round_shift_bits = _mm_cvtsi32_si128(bits);
  const __m512i round_const_bits = _mm512_set1_epi32((1 << bits) >> 1);
  const __m512i clip_pixel =
      _mm512_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  const __m512i zero = _mm512_setzero_si512();

  assert(bits >= 0);
  assert((FILTER_BITS - conv_params->round_1) >= 0 ||
         ((conv_params->round_0 + conv_params->round_1) == 2 * FILTER_BITS));

  prepare_coeffs_avx512(filter_params_x, subpel_x_qn, coeffs_x);

  for (int i = 0; i < h; ++i) {
    for (int jc = 0; jc < w; jc += 32) {
      const int j = (jc + 32 <= w) ? jc : (w - 32);
      const uint16_t *const p = &src_ptr[i * src_stride + j];

      // Even output pixels: windows at pixel offsets 0, 2, 4, 6.
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 0));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 2));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 4));
      s[3] = _mm512_loadu_si512((const __m512i *)(p + 6));
      __m512i res_even = convolve_avx512(s, coeffs_x);
      res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_x),
                                  round_shift_x);

      // Odd output pixels: windows at pixel offsets 1, 3, 5, 7.
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 1));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 3));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 5));
      s[3] = _mm512_loadu_si512((const __m512i *)(p + 7));
      __m512i res_odd = convolve_avx512(s, coeffs_x);
      res_odd = _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_x),
                                 round_shift_x);

      res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_bits),
                                  round_shift_bits);
      res_odd = _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_bits),
                                 round_shift_bits);

      const __m512i res_even1 = _mm512_packs_epi32(res_even, res_even);
      const __m512i res_odd1 = _mm512_packs_epi32(res_odd, res_odd);
      __m512i res = _mm512_unpacklo_epi16(res_even1, res_odd1);
      res = _mm512_min_epi16(res, clip_pixel);
      res = _mm512_max_epi16(res, zero);

      _mm512_storeu_si512((__m512i *)&dst[i * dst_stride + j], res);
    }
  }
}

void av2_highbd_convolve_x_sr_avx512(const uint16_t *src, int src_stride,
                                     uint16_t *dst, int dst_stride, int w,
                                     int h,
                                     const InterpFilterParams *filter_params_x,
                                     const int subpel_x_qn,
                                     ConvolveParams *conv_params, int bd) {
  if (w >= 32) {
    highbd_convolve_x_sr_avx512_loadonly(src, src_stride, dst, dst_stride, w, h,
                                         filter_params_x, subpel_x_qn,
                                         conv_params, bd);
    return;
  }
  if (w <= 8 && h <= 4) {
    av2_highbd_convolve_x_sr_avx2(src, src_stride, dst, dst_stride, w, h,
                                  filter_params_x, subpel_x_qn, conv_params,
                                  bd);
    return;
  }
  if (w == 16) {
    av2_highbd_convolve_x_sr_avx2(src, src_stride, dst, dst_stride, w, h,
                                  filter_params_x, subpel_x_qn, conv_params,
                                  bd);
    return;
  }
  highbd_convolve_x_sr_avx512_shuffle(src, src_stride, dst, dst_stride, w, h,
                                      filter_params_x, subpel_x_qn, conv_params,
                                      bd);
}
