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
#include <immintrin.h>  // AVX2

#include "aom_ports/mem.h"
#include "config/aom_dsp_rtcd.h"
#include "aom_dsp/aom_filter.h"
#include "aom_dsp/x86/synonyms.h"

typedef void (*high_variance_fn_t)(const uint16_t *src, int src_stride,
                                   const uint16_t *ref, int ref_stride,
                                   uint32_t *sse, int *sum, int n);

// TODO(any): need to support 12-bit
static AOM_FORCE_INLINE void aom_highbd_var_filter_block2d_bil_avx2(
    const uint16_t *src_ptr, unsigned int src_pixels_per_line, int pixel_step,
    unsigned int output_height, unsigned int output_width,
    const uint32_t xoffset, const uint32_t yoffset, const uint16_t *dst_ptr,
    int dst_stride, uint64_t *sse, int64_t *sum) {
  const __m256i filter1 =
      _mm256_set1_epi32((uint32_t)(bilinear_filters_2t[xoffset][1] << 16) |
                        bilinear_filters_2t[xoffset][0]);
  const __m256i filter2 =
      _mm256_set1_epi32((uint32_t)(bilinear_filters_2t[yoffset][1] << 16) |
                        bilinear_filters_2t[yoffset][0]);
  const __m256i one = _mm256_set1_epi16(1);
  const uint32_t bitshift = (uint32_t)0x40;
  (void)pixel_step;
  unsigned int i, j, prev = 0, curr = 2;
  uint16_t *src_ptr_ref = (uint16_t *)src_ptr;
  uint16_t *dst_ptr_ref = (uint16_t *)dst_ptr;
  int64_t sum_long = 0;
  uint64_t sse_long = 0;
  unsigned int inc = 1;
  __m256i rbias = _mm256_set1_epi32(bitshift);
  __m256i opointer[8];
  unsigned int range;
  if (xoffset == 0) {
    if (yoffset == 0) {  // xoffset==0 && yoffset==0
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;
        }
        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();
        for (i = 0; i < 16 / inc; ++i) {
          __m256i V_S_SRC = _mm256_loadu_si256((const __m256i *)src_ptr);
          src_ptr += src_pixels_per_line;
          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;

          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    } else if (yoffset == 4) {  // xoffset==0 && yoffset==4
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;

          opointer[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
          src_ptr += src_pixels_per_line;
          curr = 0;
        }

        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();

        for (i = 0; i < 16 / inc; ++i) {
          prev = curr;
          curr = (curr == 0) ? 1 : 0;
          opointer[curr] = _mm256_loadu_si256((const __m256i *)src_ptr);
          src_ptr += src_pixels_per_line;

          __m256i V_S_SRC = _mm256_avg_epu16(opointer[curr], opointer[prev]);

          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;
          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);
          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    } else {  // xoffset==0 && yoffset==1,2,3,5,6,7
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;

          opointer[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
          src_ptr += src_pixels_per_line;
          curr = 0;
        }

        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();

        for (i = 0; i < 16 / inc; ++i) {
          prev = curr;
          curr = (curr == 0) ? 1 : 0;
          opointer[curr] = _mm256_loadu_si256((const __m256i *)src_ptr);
          src_ptr += src_pixels_per_line;

          __m256i V_S_M1 =
              _mm256_unpacklo_epi16(opointer[prev], opointer[curr]);
          __m256i V_S_M2 =
              _mm256_unpackhi_epi16(opointer[prev], opointer[curr]);

          __m256i V_S_MAD1 = _mm256_madd_epi16(V_S_M1, filter2);
          __m256i V_S_MAD2 = _mm256_madd_epi16(V_S_M2, filter2);

          __m256i V_S_S1 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD1, rbias), 7);
          __m256i V_S_S2 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD2, rbias), 7);

          __m256i V_S_SRC = _mm256_packus_epi32(V_S_S1, V_S_S2);

          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;

          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    }
  } else if (xoffset == 4) {
    if (yoffset == 0) {  // xoffset==4 && yoffset==0
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;
          __m256i V_H_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_H_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;

          opointer[0] = _mm256_avg_epu16(V_H_D1, V_H_D2);

          curr = 0;
        }

        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();

        for (i = 0; i < 16 / inc; ++i) {
          prev = curr;
          curr = (curr == 0) ? 1 : 0;
          __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;

          opointer[curr] = _mm256_avg_epu16(V_V_D1, V_V_D2);

          __m256i V_S_M1 =
              _mm256_unpacklo_epi16(opointer[prev], opointer[curr]);
          __m256i V_S_M2 =
              _mm256_unpackhi_epi16(opointer[prev], opointer[curr]);

          __m256i V_S_MAD1 = _mm256_madd_epi16(V_S_M1, filter2);
          __m256i V_S_MAD2 = _mm256_madd_epi16(V_S_M2, filter2);

          __m256i V_S_S1 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD1, rbias), 7);
          __m256i V_S_S2 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD2, rbias), 7);

          __m256i V_S_SRC = _mm256_packus_epi32(V_S_S1, V_S_S2);

          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;

          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    } else if (yoffset == 4) {  // xoffset==4 && yoffset==4
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;

          __m256i V_H_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_H_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;
          opointer[0] = _mm256_avg_epu16(V_H_D1, V_H_D2);
          curr = 0;
        }

        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();

        for (i = 0; i < 16 / inc; ++i) {
          prev = curr;
          curr = (curr == 0) ? 1 : 0;
          __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;
          opointer[curr] = _mm256_avg_epu16(V_V_D1, V_V_D2);
          __m256i V_S_SRC = _mm256_avg_epu16(opointer[curr], opointer[prev]);

          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;
          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);
          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    } else {  // xoffset==4 && yoffset==1,2,3,5,6,7
      range = output_width / 16;
      if (output_height == 8) inc = 2;
      if (output_height == 4) inc = 4;
      for (j = 0; j < range * output_height * inc / 16; j++) {
        if (j % (output_height * inc / 16) == 0) {
          src_ptr = src_ptr_ref;
          src_ptr_ref += 16;
          dst_ptr = dst_ptr_ref;
          dst_ptr_ref += 16;

          __m256i V_H_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_H_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;
          opointer[0] = _mm256_avg_epu16(V_H_D1, V_H_D2);
          curr = 0;
        }

        __m256i sum1 = _mm256_setzero_si256();
        __m256i sse1 = _mm256_setzero_si256();

        for (i = 0; i < 16 / inc; ++i) {
          prev = curr;
          curr = (curr == 0) ? 1 : 0;
          __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
          __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
          src_ptr += src_pixels_per_line;
          opointer[curr] = _mm256_avg_epu16(V_V_D1, V_V_D2);

          __m256i V_S_M1 =
              _mm256_unpacklo_epi16(opointer[prev], opointer[curr]);
          __m256i V_S_M2 =
              _mm256_unpackhi_epi16(opointer[prev], opointer[curr]);

          __m256i V_S_MAD1 = _mm256_madd_epi16(V_S_M1, filter2);
          __m256i V_S_MAD2 = _mm256_madd_epi16(V_S_M2, filter2);

          __m256i V_S_S1 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD1, rbias), 7);
          __m256i V_S_S2 =
              _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD2, rbias), 7);

          __m256i V_S_SRC = _mm256_packus_epi32(V_S_S1, V_S_S2);

          __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
          dst_ptr += dst_stride;

          __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
          __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

          sum1 = _mm256_add_epi16(sum1, V_R_SUB);
          sse1 = _mm256_add_epi32(sse1, V_R_MAD);
        }

        __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
        __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
        __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
        __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
        const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
        const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
        __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
        v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
        sum_long += _mm_extract_epi32(v_d, 0);
        sse_long += _mm_extract_epi32(v_d, 1);
      }
    }
  } else if (yoffset == 0) {  // xoffset==1,2,3,5,6,7 && yoffset==0
    range = output_width / 16;
    if (output_height == 8) inc = 2;
    if (output_height == 4) inc = 4;
    for (j = 0; j < range * output_height * inc / 16; j++) {
      if (j % (output_height * inc / 16) == 0) {
        src_ptr = src_ptr_ref;
        src_ptr_ref += 16;
        dst_ptr = dst_ptr_ref;
        dst_ptr_ref += 16;

        curr = 0;
      }

      __m256i sum1 = _mm256_setzero_si256();
      __m256i sse1 = _mm256_setzero_si256();

      for (i = 0; i < 16 / inc; ++i) {
        __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
        __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
        src_ptr += src_pixels_per_line;
        __m256i V_V_M1 = _mm256_unpacklo_epi16(V_V_D1, V_V_D2);
        __m256i V_V_M2 = _mm256_unpackhi_epi16(V_V_D1, V_V_D2);
        __m256i V_V_MAD1 = _mm256_madd_epi16(V_V_M1, filter1);
        __m256i V_V_MAD2 = _mm256_madd_epi16(V_V_M2, filter1);
        __m256i V_V_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD1, rbias), 7);
        __m256i V_V_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD2, rbias), 7);
        opointer[curr] = _mm256_packus_epi32(V_V_S1, V_V_S2);

        __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
        dst_ptr += dst_stride;
        __m256i V_R_SUB = _mm256_sub_epi16(opointer[curr], V_D_DST);
        __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

        sum1 = _mm256_add_epi16(sum1, V_R_SUB);
        sse1 = _mm256_add_epi32(sse1, V_R_MAD);
      }

      __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
      __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
      __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
      __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
      const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
      const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
      __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
      v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
      sum_long += _mm_extract_epi32(v_d, 0);
      sse_long += _mm_extract_epi32(v_d, 1);
    }
  } else if (yoffset == 4) {  // xoffset==1,2,3,5,6,7 && yoffset==4

    range = output_width / 16;
    if (output_height == 8) inc = 2;
    if (output_height == 4) inc = 4;
    for (j = 0; j < range * output_height * inc / 16; j++) {
      if (j % (output_height * inc / 16) == 0) {
        src_ptr = src_ptr_ref;
        src_ptr_ref += 16;
        dst_ptr = dst_ptr_ref;
        dst_ptr_ref += 16;

        __m256i V_H_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
        __m256i V_H_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
        src_ptr += src_pixels_per_line;

        __m256i V_H_M1 = _mm256_unpacklo_epi16(V_H_D1, V_H_D2);
        __m256i V_H_M2 = _mm256_unpackhi_epi16(V_H_D1, V_H_D2);

        __m256i V_H_MAD1 = _mm256_madd_epi16(V_H_M1, filter1);
        __m256i V_H_MAD2 = _mm256_madd_epi16(V_H_M2, filter1);

        __m256i V_H_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_H_MAD1, rbias), 7);
        __m256i V_H_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_H_MAD2, rbias), 7);

        opointer[0] = _mm256_packus_epi32(V_H_S1, V_H_S2);

        curr = 0;
      }

      __m256i sum1 = _mm256_setzero_si256();
      __m256i sse1 = _mm256_setzero_si256();

      for (i = 0; i < 16 / inc; ++i) {
        prev = curr;
        curr = (curr == 0) ? 1 : 0;
        __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
        __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
        src_ptr += src_pixels_per_line;
        __m256i V_V_M1 = _mm256_unpacklo_epi16(V_V_D1, V_V_D2);
        __m256i V_V_M2 = _mm256_unpackhi_epi16(V_V_D1, V_V_D2);
        __m256i V_V_MAD1 = _mm256_madd_epi16(V_V_M1, filter1);
        __m256i V_V_MAD2 = _mm256_madd_epi16(V_V_M2, filter1);
        __m256i V_V_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD1, rbias), 7);
        __m256i V_V_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD2, rbias), 7);
        opointer[curr] = _mm256_packus_epi32(V_V_S1, V_V_S2);

        __m256i V_S_SRC = _mm256_avg_epu16(opointer[prev], opointer[curr]);

        __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
        dst_ptr += dst_stride;

        __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
        __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

        sum1 = _mm256_add_epi16(sum1, V_R_SUB);
        sse1 = _mm256_add_epi32(sse1, V_R_MAD);
      }

      __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
      __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
      __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
      __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
      const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
      const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
      __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
      v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
      sum_long += _mm_extract_epi32(v_d, 0);
      sse_long += _mm_extract_epi32(v_d, 1);
    }
  } else {  // xoffset==1,2,3,5,6,7 && yoffset==1,2,3,5,6,7
    range = output_width / 16;
    if (output_height == 8) inc = 2;
    if (output_height == 4) inc = 4;
    unsigned int nloop = 16 / inc;
    for (j = 0; j < range * output_height * inc / 16; j++) {
      if (j % (output_height * inc / 16) == 0) {
        src_ptr = src_ptr_ref;
        src_ptr_ref += 16;
        dst_ptr = dst_ptr_ref;
        dst_ptr_ref += 16;

        __m256i V_H_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
        __m256i V_H_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
        src_ptr += src_pixels_per_line;

        __m256i V_H_M1 = _mm256_unpacklo_epi16(V_H_D1, V_H_D2);
        __m256i V_H_M2 = _mm256_unpackhi_epi16(V_H_D1, V_H_D2);

        __m256i V_H_MAD1 = _mm256_madd_epi16(V_H_M1, filter1);
        __m256i V_H_MAD2 = _mm256_madd_epi16(V_H_M2, filter1);

        __m256i V_H_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_H_MAD1, rbias), 7);
        __m256i V_H_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_H_MAD2, rbias), 7);

        opointer[0] = _mm256_packus_epi32(V_H_S1, V_H_S2);

        curr = 0;
      }

      __m256i sum1 = _mm256_setzero_si256();
      __m256i sse1 = _mm256_setzero_si256();

      for (i = 0; i < nloop; ++i) {
        prev = curr;
        curr = !curr;
        __m256i V_V_D1 = _mm256_loadu_si256((const __m256i *)src_ptr);
        __m256i V_V_D2 = _mm256_loadu_si256((const __m256i *)(src_ptr + 1));
        src_ptr += src_pixels_per_line;
        __m256i V_V_M1 = _mm256_unpacklo_epi16(V_V_D1, V_V_D2);
        __m256i V_V_M2 = _mm256_unpackhi_epi16(V_V_D1, V_V_D2);
        __m256i V_V_MAD1 = _mm256_madd_epi16(V_V_M1, filter1);
        __m256i V_V_MAD2 = _mm256_madd_epi16(V_V_M2, filter1);
        __m256i V_V_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD1, rbias), 7);
        __m256i V_V_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_V_MAD2, rbias), 7);
        opointer[curr] = _mm256_packus_epi32(V_V_S1, V_V_S2);

        __m256i V_S_M1 = _mm256_unpacklo_epi16(opointer[prev], opointer[curr]);
        __m256i V_S_M2 = _mm256_unpackhi_epi16(opointer[prev], opointer[curr]);

        __m256i V_S_MAD1 = _mm256_madd_epi16(V_S_M1, filter2);
        __m256i V_S_MAD2 = _mm256_madd_epi16(V_S_M2, filter2);

        __m256i V_S_S1 =
            _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD1, rbias), 7);
        __m256i V_S_S2 =
            _mm256_srli_epi32(_mm256_add_epi32(V_S_MAD2, rbias), 7);

        __m256i V_S_SRC = _mm256_packus_epi32(V_S_S1, V_S_S2);

        __m256i V_D_DST = _mm256_loadu_si256((const __m256i *)dst_ptr);
        dst_ptr += dst_stride;

        __m256i V_R_SUB = _mm256_sub_epi16(V_S_SRC, V_D_DST);
        __m256i V_R_MAD = _mm256_madd_epi16(V_R_SUB, V_R_SUB);

        sum1 = _mm256_add_epi16(sum1, V_R_SUB);
        sse1 = _mm256_add_epi32(sse1, V_R_MAD);
      }

      __m256i v_sum0 = _mm256_madd_epi16(sum1, one);
      __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, sse1);
      __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, sse1);
      __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
      const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
      const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
      __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
      v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
      sum_long += _mm_extract_epi32(v_d, 0);
      sse_long += _mm_extract_epi32(v_d, 1);
    }
  }

  *sse = sse_long;
  *sum = sum_long;
}

void aom_highbd_calc8xNvar_avx2(const uint16_t *src, int src_stride,
                                const uint16_t *ref, int ref_stride,
                                uint32_t *sse, int *sum, int n) {
  __m256i v_sum_d = _mm256_setzero_si256();
  __m256i v_sse_d = _mm256_setzero_si256();
  for (int i = 0; i < n; i += 2) {
    const __m128i v_p_a0 = _mm_loadu_si128((const __m128i *)src);
    const __m128i v_p_a1 = _mm_loadu_si128((const __m128i *)(src + src_stride));
    const __m128i v_p_b0 = _mm_loadu_si128((const __m128i *)ref);
    const __m128i v_p_b1 = _mm_loadu_si128((const __m128i *)(ref + ref_stride));
    __m256i v_p_a = _mm256_castsi128_si256(v_p_a0);
    __m256i v_p_b = _mm256_castsi128_si256(v_p_b0);
    v_p_a = _mm256_inserti128_si256(v_p_a, v_p_a1, 1);
    v_p_b = _mm256_inserti128_si256(v_p_b, v_p_b1, 1);
    const __m256i v_diff = _mm256_sub_epi16(v_p_a, v_p_b);
    const __m256i v_sqrdiff = _mm256_madd_epi16(v_diff, v_diff);
    v_sum_d = _mm256_add_epi16(v_sum_d, v_diff);
    v_sse_d = _mm256_add_epi32(v_sse_d, v_sqrdiff);
    src += src_stride * 2;
    ref += ref_stride * 2;
  }
  __m256i v_sum00 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(v_sum_d));
  __m256i v_sum01 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(v_sum_d, 1));
  __m256i v_sum0 = _mm256_add_epi32(v_sum00, v_sum01);
  __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, v_sse_d);
  __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, v_sse_d);
  __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
  const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
  const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
  __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
  v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
  *sum = _mm_extract_epi32(v_d, 0);
  *sse = _mm_extract_epi32(v_d, 1);
}

// TODO(any): Rewrite this function to make it work for 12-bit input
// Overflows for 12-bit inputs
void aom_highbd_calc16xNvar_avx2(const uint16_t *src, int src_stride,
                                 const uint16_t *ref, int ref_stride,
                                 uint32_t *sse, int *sum, int n) {
  __m256i v_sum_d = _mm256_setzero_si256();
  __m256i v_sse_d = _mm256_setzero_si256();
  const __m256i one = _mm256_set1_epi16(1);
  for (int i = 0; i < n; ++i) {
    const __m256i v_p_a = _mm256_loadu_si256((const __m256i *)src);
    const __m256i v_p_b = _mm256_loadu_si256((const __m256i *)ref);
    const __m256i v_diff = _mm256_sub_epi16(v_p_a, v_p_b);
    const __m256i v_sqrdiff = _mm256_madd_epi16(v_diff, v_diff);
    v_sum_d = _mm256_add_epi16(v_sum_d, v_diff);
    v_sse_d = _mm256_add_epi32(v_sse_d, v_sqrdiff);
    src += src_stride;
    ref += ref_stride;
  }
  __m256i v_sum0 = _mm256_madd_epi16(v_sum_d, one);
  __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, v_sse_d);
  __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, v_sse_d);
  __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
  const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
  const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
  __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
  v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
  *sum = _mm_extract_epi32(v_d, 0);
  *sse = _mm_extract_epi32(v_d, 1);
}

static AOM_INLINE void aom_highbd_calc4xNvar_avx2(const uint16_t *src,
                                                  int src_stride,
                                                  const uint16_t *ref,
                                                  int ref_stride, int h,
                                                  uint32_t *sse, int *sum) {
  __m256i v_sum_d = _mm256_setzero_si256();
  __m256i v_sse_d = _mm256_setzero_si256();
  const __m256i one = _mm256_set1_epi16(1);
  for (int j = 0; j < h; j += 8) {
    const __m128i a0 = _mm_loadl_epi64((__m128i const *)(src + 0 * src_stride));
    const __m128i a1 = _mm_loadl_epi64((__m128i const *)(src + 1 * src_stride));
    const __m128i a2 = _mm_loadl_epi64((__m128i const *)(src + 2 * src_stride));
    const __m128i a3 = _mm_loadl_epi64((__m128i const *)(src + 3 * src_stride));
    const __m128i a4 = _mm_loadl_epi64((__m128i const *)(src + 4 * src_stride));
    const __m128i a5 = _mm_loadl_epi64((__m128i const *)(src + 5 * src_stride));
    const __m128i a6 = _mm_loadl_epi64((__m128i const *)(src + 6 * src_stride));
    const __m128i a7 = _mm_loadl_epi64((__m128i const *)(src + 7 * src_stride));

    const __m128i b0 = _mm_loadl_epi64((__m128i const *)(ref + 0 * ref_stride));
    const __m128i b1 = _mm_loadl_epi64((__m128i const *)(ref + 1 * ref_stride));
    const __m128i b2 = _mm_loadl_epi64((__m128i const *)(ref + 2 * ref_stride));
    const __m128i b3 = _mm_loadl_epi64((__m128i const *)(ref + 3 * ref_stride));
    const __m128i b4 = _mm_loadl_epi64((__m128i const *)(ref + 4 * ref_stride));
    const __m128i b5 = _mm_loadl_epi64((__m128i const *)(ref + 5 * ref_stride));
    const __m128i b6 = _mm_loadl_epi64((__m128i const *)(ref + 6 * ref_stride));
    const __m128i b7 = _mm_loadl_epi64((__m128i const *)(ref + 7 * ref_stride));

    // a00 a10 a01 a11 a02 a12 a03 a13
    const __m128i u0 = _mm_unpacklo_epi16(a0, a1);
    // a20 a30 a21 a31 a22 a32 a23 a33
    const __m128i u1 = _mm_unpacklo_epi16(a2, a3);
    // a00 a10 a01 a11 a02 a12 a03 a13
    const __m128i u2 = _mm_unpacklo_epi16(a4, a5);
    // a20 a30 a21 a31 a22 a32 a23 a33
    const __m128i u3 = _mm_unpacklo_epi16(a6, a7);

    // b00 b10 b01 b11 b02 b12 b03 b13
    const __m128i u4 = _mm_unpacklo_epi16(b0, b1);
    // b20 b30 b21 b31 b22 b32 b23 b33
    const __m128i u5 = _mm_unpacklo_epi16(b2, b3);
    // b00 b10 b01 b11 b02 b12 b03 b13
    const __m128i u6 = _mm_unpacklo_epi16(b4, b5);
    // b20 b30 b21 b31 b22 b32 b23 b33
    const __m128i u7 = _mm_unpacklo_epi16(b6, b7);

    const __m256i x1 =
        _mm256_insertf128_si256(_mm256_castsi128_si256(u0), u1, 0x1);
    const __m256i x2 =
        _mm256_insertf128_si256(_mm256_castsi128_si256(u2), u3, 0x1);
    const __m256i y1 =
        _mm256_insertf128_si256(_mm256_castsi128_si256(u4), u5, 0x1);
    const __m256i y2 =
        _mm256_insertf128_si256(_mm256_castsi128_si256(u6), u7, 0x1);

    const __m256i v_diff1 = _mm256_sub_epi16(x1, y1);
    const __m256i v_diff2 = _mm256_sub_epi16(x2, y2);

    const __m256i v_sqrdiff1 = _mm256_madd_epi16(v_diff1, v_diff1);
    const __m256i v_sqrdiff2 = _mm256_madd_epi16(v_diff2, v_diff2);

    v_sum_d = _mm256_add_epi16(v_sum_d, _mm256_add_epi16(v_diff1, v_diff2));

    v_sse_d =
        _mm256_add_epi32(v_sse_d, _mm256_add_epi32(v_sqrdiff1, v_sqrdiff2));

    src += 8 * src_stride;
    ref += 8 * ref_stride;
  }
  const __m256i v_sum0 = _mm256_madd_epi16(v_sum_d, one);
  const __m256i v_d_l = _mm256_unpacklo_epi32(v_sum0, v_sse_d);
  const __m256i v_d_h = _mm256_unpackhi_epi32(v_sum0, v_sse_d);
  const __m256i v_d_lh = _mm256_add_epi32(v_d_l, v_d_h);
  const __m128i v_d0_d = _mm256_castsi256_si128(v_d_lh);
  const __m128i v_d1_d = _mm256_extracti128_si256(v_d_lh, 1);
  __m128i v_d = _mm_add_epi32(v_d0_d, v_d1_d);
  v_d = _mm_add_epi32(v_d, _mm_srli_si128(v_d, 8));
  *sum = _mm_extract_epi32(v_d, 0);
  *sse = _mm_extract_epi32(v_d, 1);
}

static AOM_FORCE_INLINE void highbd_variance_avx2(
    const uint16_t *src, int src_stride, const uint16_t *ref, int ref_stride,
    int w, int h, uint64_t *sse, int64_t *sum, high_variance_fn_t var_fn,
    int block_size) {
  int i, j;
  uint64_t sse_long = 0;
  int64_t sum_long = 0;
  const int tmp_h = h < block_size ? h : block_size;
  for (i = 0; i < h; i += tmp_h) {
    for (j = 0; j < w; j += block_size) {
      unsigned int sse0;
      int sum0;
      var_fn(src + src_stride * i + j, src_stride, ref + ref_stride * i + j,
             ref_stride, &sse0, &sum0, tmp_h);
      sse_long += sse0;
      sum_long += sum0;
    }
  }
  *sum = sum_long;
  *sse = sse_long;
}

static AOM_INLINE void highbd_12_variance_avx2(
    const uint16_t *src, int src_stride, const uint16_t *ref, int ref_stride,
    int w, int h, uint32_t *sse, int *sum, high_variance_fn_t var_fn,
    int block_size) {
  uint64_t sse_long = 0;
  int64_t sum_long = 0;

  highbd_variance_avx2(src, src_stride, ref, ref_stride, w, h, &sse_long,
                       &sum_long, var_fn, block_size);

  *sum = (int)ROUND_POWER_OF_TWO(sum_long, 4);
  *sse = (uint32_t)ROUND_POWER_OF_TWO(sse_long, 8);
}

static AOM_INLINE void highbd_10_variance_avx2(
    const uint16_t *src, int src_stride, const uint16_t *ref, int ref_stride,
    int w, int h, uint32_t *sse, int *sum, high_variance_fn_t var_fn,
    int block_size) {
  uint64_t sse_long = 0;
  int64_t sum_long = 0;

  highbd_variance_avx2(src, src_stride, ref, ref_stride, w, h, &sse_long,
                       &sum_long, var_fn, block_size);

  *sum = (int)ROUND_POWER_OF_TWO(sum_long, 2);
  *sse = (uint32_t)ROUND_POWER_OF_TWO(sse_long, 4);
}

static AOM_INLINE void highbd_8_variance_avx2(
    const uint16_t *src, int src_stride, const uint16_t *ref, int ref_stride,
    int w, int h, uint32_t *sse, int *sum, high_variance_fn_t var_fn,
    int block_size) {
  uint64_t sse_long = 0;
  int64_t sum_long = 0;

  highbd_variance_avx2(src, src_stride, ref, ref_stride, w, h, &sse_long,
                       &sum_long, var_fn, block_size);

  *sum = (int)sum_long;
  *sse = (uint32_t)sse_long;
}
// Specialized functions to handle width equal to 4 case.
#define VAR_FN_WD4(h, shift)                                               \
  uint32_t aom_highbd_10_variance4x##h##_avx2(                             \
      const uint16_t *src, int src_stride, const uint16_t *ref,            \
      int ref_stride, uint32_t *sse) {                                     \
    int sum;                                                               \
    aom_highbd_calc4xNvar_avx2(src, src_stride, ref, ref_stride, h, sse,   \
                               &sum);                                      \
    sum = (int)ROUND_POWER_OF_TWO(sum, 2);                                 \
    *sse = (uint32_t)ROUND_POWER_OF_TWO(*sse, 4);                          \
    const int64_t var = (int64_t)(*sse) - (((int64_t)sum * sum) >> shift); \
    return (var >= 0) ? (uint32_t)var : 0;                                 \
  }                                                                        \
  uint32_t aom_highbd_8_variance4x##h##_avx2(                              \
      const uint16_t *src, int src_stride, const uint16_t *ref,            \
      int ref_stride, uint32_t *sse) {                                     \
    int sum;                                                               \
    int64_t var;                                                           \
    aom_highbd_calc4xNvar_avx2(src, src_stride, ref, ref_stride, h, sse,   \
                               &sum);                                      \
    var = (int64_t)(*sse) - (((int64_t)sum * sum) >> shift);               \
    return (var >= 0) ? (uint32_t)var : 0;                                 \
  }

// The 12-bit function is separated out because aom_highbd_calc16x16var_avx2
// currently cannot handle 12-bit inputs
#define VAR_FN_BD12(w, h, block_size, shift)                                   \
  uint32_t aom_highbd_12_variance##w##x##h##_avx2(                             \
      const uint16_t *src, int src_stride, const uint16_t *ref,                \
      int ref_stride, uint32_t *sse) {                                         \
    int sum;                                                                   \
    int64_t var;                                                               \
    highbd_12_variance_avx2(src, src_stride, ref, ref_stride, w, h, sse, &sum, \
                            aom_highbd_calc##block_size##xNvar_avx2,           \
                            block_size);                                       \
    var = (int64_t)(*sse) - (((int64_t)sum * sum) >> shift);                   \
    return (var >= 0) ? (uint32_t)var : 0;                                     \
  }

#define VAR_FN(w, h, block_size, shift)                                        \
  uint32_t aom_highbd_10_variance##w##x##h##_avx2(                             \
      const uint16_t *src, int src_stride, const uint16_t *ref,                \
      int ref_stride, uint32_t *sse) {                                         \
    int sum;                                                                   \
    int64_t var;                                                               \
    highbd_10_variance_avx2(src, src_stride, ref, ref_stride, w, h, sse, &sum, \
                            aom_highbd_calc##block_size##xNvar_avx2,           \
                            block_size);                                       \
    var = (int64_t)(*sse) - (((int64_t)sum * sum) >> shift);                   \
    return (var >= 0) ? (uint32_t)var : 0;                                     \
  }                                                                            \
  uint32_t aom_highbd_8_variance##w##x##h##_avx2(                              \
      const uint16_t *src, int src_stride, const uint16_t *ref,                \
      int ref_stride, uint32_t *sse) {                                         \
    int sum;                                                                   \
    int64_t var;                                                               \
    highbd_8_variance_avx2(src, src_stride, ref, ref_stride, w, h, sse, &sum,  \
                           aom_highbd_calc##block_size##xNvar_avx2,            \
                           block_size);                                        \
    var = (int64_t)(*sse) - (((int64_t)sum * sum) >> shift);                   \
    return (var >= 0) ? (uint32_t)var : 0;                                     \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
VAR_FN(256, 256, 16, 16);
VAR_FN(256, 128, 16, 15);
VAR_FN(128, 256, 16, 15);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
VAR_FN(128, 128, 16, 14);
VAR_FN(128, 64, 16, 13);
VAR_FN(64, 128, 16, 13);
VAR_FN(64, 64, 16, 12);
VAR_FN(64, 32, 16, 11);
VAR_FN(32, 64, 16, 11);
VAR_FN(32, 32, 16, 10);
VAR_FN(32, 16, 16, 9);
VAR_FN(16, 32, 16, 9);
VAR_FN(16, 16, 16, 8);
VAR_FN(16, 8, 16, 7);
VAR_FN(8, 8, 8, 6);

VAR_FN(8, 32, 8, 8);
VAR_FN(32, 8, 16, 8);
VAR_FN(16, 64, 16, 10);
VAR_FN(64, 16, 16, 10);
VAR_FN(8, 16, 8, 7);
VAR_FN(16, 4, 16, 6);
VAR_FN(8, 4, 8, 5);
VAR_FN_WD4(16, 6);
VAR_FN_WD4(8, 5);
#if CONFIG_EXT_RECUR_PARTITIONS
VAR_FN(64, 8, 16, 9);
VAR_FN(8, 64, 8, 9);
VAR_FN(64, 4, 16, 8);
VAR_FN(32, 4, 16, 7);
VAR_FN_WD4(64, 8);
VAR_FN_WD4(32, 7);

VAR_FN_BD12(256, 256, 8, 16);
VAR_FN_BD12(256, 128, 8, 15);
VAR_FN_BD12(128, 256, 8, 15);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
VAR_FN_BD12(128, 128, 8, 14);
VAR_FN_BD12(128, 64, 8, 13);
VAR_FN_BD12(64, 128, 8, 13);
VAR_FN_BD12(64, 64, 8, 12);
VAR_FN_BD12(64, 32, 8, 11);
VAR_FN_BD12(32, 64, 8, 11);
VAR_FN_BD12(32, 32, 8, 10);
VAR_FN_BD12(32, 16, 8, 9);
VAR_FN_BD12(16, 32, 8, 9);
VAR_FN_BD12(16, 16, 8, 8);
VAR_FN_BD12(16, 8, 8, 7);
VAR_FN_BD12(8, 8, 8, 6);

VAR_FN_BD12(8, 32, 8, 8);
VAR_FN_BD12(32, 8, 8, 8);
VAR_FN_BD12(16, 64, 8, 10);
VAR_FN_BD12(64, 16, 8, 10);
VAR_FN_BD12(8, 16, 8, 7);
#if CONFIG_EXT_RECUR_PARTITIONS
VAR_FN_BD12(64, 8, 8, 9);
VAR_FN_BD12(8, 64, 8, 9);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#undef VAR_FN
#undef VAR_FN_BD12

// The 12-bit function is separated out because
// aom_highbd_var_filter_block2d_bil_avx2 overflows when bsize \geq 16X16
#define HIGHBD_SUBPIX_VAR_BD12(W, H, rshift)                                  \
  uint32_t aom_highbd_12_sub_pixel_variance##W##x##H##_avx2(                  \
      const uint16_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint16_t *dst, int dst_stride, uint32_t *sse) {                   \
    uint64_t sse_long = 0;                                                    \
    int64_t sum = 0;                                                          \
                                                                              \
    aom_highbd_var_filter_block2d_bil_avx2(src, src_stride, 1, H, W, xoffset, \
                                           yoffset, dst, dst_stride,          \
                                           &sse_long, &sum);                  \
                                                                              \
    *sse = (uint32_t)ROUND_POWER_OF_TWO(sse_long, 8);                         \
    sum = ROUND_POWER_OF_TWO(sum, 4);                                         \
                                                                              \
    int32_t var = *sse - (uint32_t)((sum * sum) >> rshift);                   \
                                                                              \
    return (var > 0) ? var : 0;                                               \
  }

#define HIGHBD_SUBPIX_VAR(W, H, rshift)                                       \
  uint32_t aom_highbd_10_sub_pixel_variance##W##x##H##_avx2(                  \
      const uint16_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint16_t *dst, int dst_stride, uint32_t *sse) {                   \
    uint64_t sse_long = 0;                                                    \
    int64_t sum = 0;                                                          \
                                                                              \
    aom_highbd_var_filter_block2d_bil_avx2(src, src_stride, 1, H, W, xoffset, \
                                           yoffset, dst, dst_stride,          \
                                           &sse_long, &sum);                  \
                                                                              \
    *sse = (uint32_t)ROUND_POWER_OF_TWO(sse_long, 4);                         \
    sum = ROUND_POWER_OF_TWO(sum, 2);                                         \
                                                                              \
    int32_t var = *sse - (uint32_t)((sum * sum) >> rshift);                   \
                                                                              \
    return (var > 0) ? var : 0;                                               \
  }                                                                           \
  uint32_t aom_highbd_8_sub_pixel_variance##W##x##H##_avx2(                   \
      const uint16_t *src, int src_stride, int xoffset, int yoffset,          \
      const uint16_t *dst, int dst_stride, uint32_t *sse) {                   \
    uint64_t sse_long = 0;                                                    \
    int64_t sum = 0;                                                          \
                                                                              \
    aom_highbd_var_filter_block2d_bil_avx2(src, src_stride, 1, H, W, xoffset, \
                                           yoffset, dst, dst_stride,          \
                                           &sse_long, &sum);                  \
                                                                              \
    *sse = (uint32_t)sse_long;                                                \
    int32_t var = *sse - (uint32_t)((sum * sum) >> rshift);                   \
                                                                              \
    return (var > 0) ? var : 0;                                               \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
// HIGHBD_SUBPIX_VAR(256, 256, 16);
HIGHBD_SUBPIX_VAR(256, 128, 15);
HIGHBD_SUBPIX_VAR(128, 256, 15);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_SUBPIX_VAR(128, 128, 14);
HIGHBD_SUBPIX_VAR(128, 64, 13);
HIGHBD_SUBPIX_VAR(64, 128, 13);
HIGHBD_SUBPIX_VAR(64, 64, 12);
HIGHBD_SUBPIX_VAR(64, 32, 11);
HIGHBD_SUBPIX_VAR(32, 64, 11);
HIGHBD_SUBPIX_VAR(32, 32, 10);
HIGHBD_SUBPIX_VAR(32, 16, 9);
HIGHBD_SUBPIX_VAR(16, 32, 9);
HIGHBD_SUBPIX_VAR(16, 16, 8);
HIGHBD_SUBPIX_VAR(16, 8, 7);

HIGHBD_SUBPIX_VAR(64, 16, 10);
HIGHBD_SUBPIX_VAR(16, 64, 10);
HIGHBD_SUBPIX_VAR(32, 8, 8);
HIGHBD_SUBPIX_VAR(16, 4, 6);

#if CONFIG_EXT_RECUR_PARTITIONS
// HIGHBD_SUBPIX_VAR_BD12(256, 256, 16);
// HIGHBD_SUBPIX_VAR_BD12(256, 128, 15);
// HIGHBD_SUBPIX_VAR_BD12(128, 256, 15);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
// HIGHBD_SUBPIX_VAR_BD12(128, 128, 14);
// HIGHBD_SUBPIX_VAR_BD12(128, 64, 13);
// HIGHBD_SUBPIX_VAR_BD12(64, 128, 13);
// HIGHBD_SUBPIX_VAR_BD12(64, 64, 12);
// HIGHBD_SUBPIX_VAR_BD12(64, 32, 11);
// HIGHBD_SUBPIX_VAR_BD12(32, 64, 11);
// HIGHBD_SUBPIX_VAR_BD12(32, 32, 10);
// HIGHBD_SUBPIX_VAR_BD12(32, 16, 9);
// HIGHBD_SUBPIX_VAR_BD12(16, 32, 9);
// HIGHBD_SUBPIX_VAR_BD12(16, 16, 8);
HIGHBD_SUBPIX_VAR_BD12(16, 8, 7);

// HIGHBD_SUBPIX_VAR_BD12(64, 16, 10);
// HIGHBD_SUBPIX_VAR_BD12(16, 64, 10);
// HIGHBD_SUBPIX_VAR_BD12(32, 8, 8);
HIGHBD_SUBPIX_VAR_BD12(16, 4, 6);
#undef HIGHBD_SUBPIX_VAR
#undef HIGHBD_SUBPIX_VAR_BD12

uint64_t aom_mse_4xh_16bit_highbd_avx2(uint16_t *dst, int dstride,
                                       uint16_t *src, int sstride, int h) {
  uint64_t sum = 0;
  __m128i reg0_4x16, reg1_4x16, reg2_4x16, reg3_4x16;
  __m256i src0_8x16, src1_8x16, src_16x16;
  __m256i dst0_8x16, dst1_8x16, dst_16x16;
  __m256i res0_4x64, res1_4x64, res2_4x64, res3_4x64;
  __m256i sub_result;
  const __m256i zeros = _mm256_broadcastsi128_si256(_mm_setzero_si128());
  __m256i square_result = _mm256_broadcastsi128_si256(_mm_setzero_si128());
  for (int i = 0; i < h; i += 4) {
    reg0_4x16 = _mm_loadl_epi64((__m128i const *)(&dst[(i + 0) * dstride]));
    reg1_4x16 = _mm_loadl_epi64((__m128i const *)(&dst[(i + 1) * dstride]));
    reg2_4x16 = _mm_loadl_epi64((__m128i const *)(&dst[(i + 2) * dstride]));
    reg3_4x16 = _mm_loadl_epi64((__m128i const *)(&dst[(i + 3) * dstride]));
    dst0_8x16 =
        _mm256_castsi128_si256(_mm_unpacklo_epi64(reg0_4x16, reg1_4x16));
    dst1_8x16 =
        _mm256_castsi128_si256(_mm_unpacklo_epi64(reg2_4x16, reg3_4x16));
    dst_16x16 = _mm256_permute2x128_si256(dst0_8x16, dst1_8x16, 0x20);

    reg0_4x16 = _mm_loadl_epi64((__m128i const *)(&src[(i + 0) * sstride]));
    reg1_4x16 = _mm_loadl_epi64((__m128i const *)(&src[(i + 1) * sstride]));
    reg2_4x16 = _mm_loadl_epi64((__m128i const *)(&src[(i + 2) * sstride]));
    reg3_4x16 = _mm_loadl_epi64((__m128i const *)(&src[(i + 3) * sstride]));
    src0_8x16 =
        _mm256_castsi128_si256(_mm_unpacklo_epi64(reg0_4x16, reg1_4x16));
    src1_8x16 =
        _mm256_castsi128_si256(_mm_unpacklo_epi64(reg2_4x16, reg3_4x16));
    src_16x16 = _mm256_permute2x128_si256(src0_8x16, src1_8x16, 0x20);

    sub_result = _mm256_abs_epi16(_mm256_sub_epi16(src_16x16, dst_16x16));

    src_16x16 = _mm256_unpacklo_epi16(sub_result, zeros);
    dst_16x16 = _mm256_unpackhi_epi16(sub_result, zeros);

    src_16x16 = _mm256_madd_epi16(src_16x16, src_16x16);
    dst_16x16 = _mm256_madd_epi16(dst_16x16, dst_16x16);

    res0_4x64 = _mm256_unpacklo_epi32(src_16x16, zeros);
    res1_4x64 = _mm256_unpackhi_epi32(src_16x16, zeros);
    res2_4x64 = _mm256_unpacklo_epi32(dst_16x16, zeros);
    res3_4x64 = _mm256_unpackhi_epi32(dst_16x16, zeros);

    square_result = _mm256_add_epi64(
        square_result,
        _mm256_add_epi64(
            _mm256_add_epi64(_mm256_add_epi64(res0_4x64, res1_4x64), res2_4x64),
            res3_4x64));
  }
  const __m128i sum_2x64 =
      _mm_add_epi64(_mm256_castsi256_si128(square_result),
                    _mm256_extracti128_si256(square_result, 1));
  const __m128i sum_1x64 = _mm_add_epi64(sum_2x64, _mm_srli_si128(sum_2x64, 8));
  xx_storel_64(&sum, sum_1x64);
  return sum;
}

uint64_t aom_mse_8xh_16bit_highbd_avx2(uint16_t *dst, int dstride,
                                       uint16_t *src, int sstride, int h) {
  uint64_t sum = 0;
  __m256i src0_8x16, src1_8x16, src_16x16;
  __m256i dst0_8x16, dst1_8x16, dst_16x16;
  __m256i res0_4x64, res1_4x64, res2_4x64, res3_4x64;
  __m256i sub_result;
  const __m256i zeros = _mm256_broadcastsi128_si256(_mm_setzero_si128());
  __m256i square_result = _mm256_broadcastsi128_si256(_mm_setzero_si128());

  for (int i = 0; i < h; i += 2) {
    dst0_8x16 =
        _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)&dst[i * dstride]));
    dst1_8x16 = _mm256_castsi128_si256(
        _mm_loadu_si128((__m128i *)&dst[(i + 1) * dstride]));
    dst_16x16 = _mm256_permute2x128_si256(dst0_8x16, dst1_8x16, 0x20);

    src0_8x16 =
        _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)&src[i * sstride]));
    src1_8x16 = _mm256_castsi128_si256(
        _mm_loadu_si128((__m128i *)&src[(i + 1) * sstride]));
    src_16x16 = _mm256_permute2x128_si256(src0_8x16, src1_8x16, 0x20);

    sub_result = _mm256_abs_epi16(_mm256_sub_epi16(src_16x16, dst_16x16));

    src_16x16 = _mm256_unpacklo_epi16(sub_result, zeros);
    dst_16x16 = _mm256_unpackhi_epi16(sub_result, zeros);

    src_16x16 = _mm256_madd_epi16(src_16x16, src_16x16);
    dst_16x16 = _mm256_madd_epi16(dst_16x16, dst_16x16);

    res0_4x64 = _mm256_unpacklo_epi32(src_16x16, zeros);
    res1_4x64 = _mm256_unpackhi_epi32(src_16x16, zeros);
    res2_4x64 = _mm256_unpacklo_epi32(dst_16x16, zeros);
    res3_4x64 = _mm256_unpackhi_epi32(dst_16x16, zeros);

    square_result = _mm256_add_epi64(
        square_result,
        _mm256_add_epi64(
            _mm256_add_epi64(_mm256_add_epi64(res0_4x64, res1_4x64), res2_4x64),
            res3_4x64));
  }

  const __m128i sum_2x64 =
      _mm_add_epi64(_mm256_castsi256_si128(square_result),
                    _mm256_extracti128_si256(square_result, 1));
  const __m128i sum_1x64 = _mm_add_epi64(sum_2x64, _mm_srli_si128(sum_2x64, 8));
  xx_storel_64(&sum, sum_1x64);
  return sum;
}

uint64_t aom_mse_wxh_16bit_highbd_avx2(uint16_t *dst, int dstride,
                                       uint16_t *src, int sstride, int w,
                                       int h) {
  assert((w == 8 || w == 4) && (h == 8 || h == 4) &&
         "w=8/4 and h=8/4 must satisfy");
  switch (w) {
    case 4: return aom_mse_4xh_16bit_highbd_avx2(dst, dstride, src, sstride, h);
    case 8: return aom_mse_8xh_16bit_highbd_avx2(dst, dstride, src, sstride, h);
    default: assert(0 && "unsupported width"); return -1;
  }
}
