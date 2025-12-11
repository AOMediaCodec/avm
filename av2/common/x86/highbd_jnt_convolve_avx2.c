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
#include <assert.h>

#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/x86/convolve_avx2.h"
#include "avm_dsp/x86/convolve_common_intrin.h"
#include "avm_dsp/x86/convolve_sse4_1.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"
#include "av2/common/convolve.h"

#define CONVOLVE_DIST_WTD_VERT_FILTER_8TAP                                     \
  const __m256i s0 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));               \
  const __m256i s1 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));               \
  const __m256i s2 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 2 * im_stride));               \
  const __m256i s3 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 3 * im_stride));               \
  const __m256i s4 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 4 * im_stride));               \
  const __m256i s5 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 5 * im_stride));               \
                                                                               \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                        \
  s[1] = _mm256_unpacklo_epi16(s2, s3);                                        \
  s[2] = _mm256_unpacklo_epi16(s4, s5);                                        \
                                                                               \
  s[4] = _mm256_unpackhi_epi16(s0, s1);                                        \
  s[5] = _mm256_unpackhi_epi16(s2, s3);                                        \
  s[6] = _mm256_unpackhi_epi16(s4, s5);                                        \
                                                                               \
  if (w - j < 8) {                                                             \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 6 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 7 * im_stride));               \
                                                                               \
      s[3] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[7] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve(s, coeffs_y);                             \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);       \
                                                                               \
        const __m256i comp_avg_res = highbd_comp_avg(                          \
            &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);      \
                                                                               \
        const __m256i round_result = highbd_convolve_rounding(                 \
            &comp_avg_res, &offset_const, &rounding_const, rounding_shift);    \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result, round_result);                   \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storel_epi64(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[1] = s[2];                                                             \
      s[2] = s[3];                                                             \
      s[4] = s[5];                                                             \
      s[5] = s[6];                                                             \
      s[6] = s[7];                                                             \
    }                                                                          \
  } else {                                                                     \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 6 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 7 * im_stride));               \
                                                                               \
      s[3] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[7] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve(s, coeffs_y);                             \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      const __m256i res_b = convolve(s + 4, coeffs_y);                         \
      const __m256i res_b_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_b, round_const_y), round_shift_y);              \
                                                                               \
      __m256i res_unsigned_hi = _mm256_add_epi32(res_b_round, offset_const);   \
                                                                               \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);    \
        const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);    \
                                                                               \
        const __m256i comp_avg_res_lo = highbd_comp_avg(                       \
            &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);   \
        const __m256i comp_avg_res_hi = highbd_comp_avg(                       \
            &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);   \
                                                                               \
        const __m256i round_result_lo = highbd_convolve_rounding(              \
            &comp_avg_res_lo, &offset_const, &rounding_const, rounding_shift); \
        const __m256i round_result_hi = highbd_convolve_rounding(              \
            &comp_avg_res_hi, &offset_const, &rounding_const, rounding_shift); \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result_lo, round_result_hi);             \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storeu_si128(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[1] = s[2];                                                             \
      s[2] = s[3];                                                             \
      s[4] = s[5];                                                             \
      s[5] = s[6];                                                             \
      s[6] = s[7];                                                             \
    }                                                                          \
  }

#define CONVOLVE_DIST_WTD_VERT_FILTER_6TAP                                     \
  const __m256i s0 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));               \
  const __m256i s1 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));               \
  const __m256i s2 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 2 * im_stride));               \
  const __m256i s3 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 3 * im_stride));               \
                                                                               \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                        \
  s[1] = _mm256_unpacklo_epi16(s2, s3);                                        \
                                                                               \
  s[3] = _mm256_unpackhi_epi16(s0, s1);                                        \
  s[4] = _mm256_unpackhi_epi16(s2, s3);                                        \
                                                                               \
  if (w - j < 8) {                                                             \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 4 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 5 * im_stride));               \
                                                                               \
      s[2] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[5] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve_6tap(s, coeffs_y);                        \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);       \
                                                                               \
        const __m256i comp_avg_res = highbd_comp_avg(                          \
            &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);      \
                                                                               \
        const __m256i round_result = highbd_convolve_rounding(                 \
            &comp_avg_res, &offset_const, &rounding_const, rounding_shift);    \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result, round_result);                   \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storel_epi64(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[1] = s[2];                                                             \
      s[3] = s[4];                                                             \
      s[4] = s[5];                                                             \
    }                                                                          \
  } else {                                                                     \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 4 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 5 * im_stride));               \
                                                                               \
      s[2] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[5] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve_6tap(s, coeffs_y);                        \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      const __m256i res_b = convolve_6tap(s + 3, coeffs_y);                    \
      const __m256i res_b_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_b, round_const_y), round_shift_y);              \
                                                                               \
      __m256i res_unsigned_hi = _mm256_add_epi32(res_b_round, offset_const);   \
                                                                               \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);    \
        const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);    \
                                                                               \
        const __m256i comp_avg_res_lo = highbd_comp_avg(                       \
            &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);   \
        const __m256i comp_avg_res_hi = highbd_comp_avg(                       \
            &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);   \
                                                                               \
        const __m256i round_result_lo = highbd_convolve_rounding(              \
            &comp_avg_res_lo, &offset_const, &rounding_const, rounding_shift); \
        const __m256i round_result_hi = highbd_convolve_rounding(              \
            &comp_avg_res_hi, &offset_const, &rounding_const, rounding_shift); \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result_lo, round_result_hi);             \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storeu_si128(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[1] = s[2];                                                             \
      s[3] = s[4];                                                             \
      s[4] = s[5];                                                             \
    }                                                                          \
  }

#define CONVOLVE_DIST_WTD_VERT_FILTER_4TAP                                     \
  const __m256i s0 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));               \
  const __m256i s1 =                                                           \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));               \
                                                                               \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                        \
                                                                               \
  s[2] = _mm256_unpackhi_epi16(s0, s1);                                        \
                                                                               \
  if (w - j < 8) {                                                             \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 2 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 3 * im_stride));               \
                                                                               \
      s[1] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[3] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve_4tap(s, coeffs_y);                        \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);       \
                                                                               \
        const __m256i comp_avg_res = highbd_comp_avg(                          \
            &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);      \
                                                                               \
        const __m256i round_result = highbd_convolve_rounding(                 \
            &comp_avg_res, &offset_const, &rounding_const, rounding_shift);    \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result, round_result);                   \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storel_epi64(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[2] = s[3];                                                             \
    }                                                                          \
  } else {                                                                     \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 2 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 3 * im_stride));               \
                                                                               \
      s[1] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[3] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = convolve_4tap(s, coeffs_y);                        \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      const __m256i res_b = convolve_4tap(s + 2, coeffs_y);                    \
      const __m256i res_b_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_b, round_const_y), round_shift_y);              \
                                                                               \
      __m256i res_unsigned_hi = _mm256_add_epi32(res_b_round, offset_const);   \
                                                                               \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);    \
        const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);    \
                                                                               \
        const __m256i comp_avg_res_lo = highbd_comp_avg(                       \
            &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);   \
        const __m256i comp_avg_res_hi = highbd_comp_avg(                       \
            &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);   \
                                                                               \
        const __m256i round_result_lo = highbd_convolve_rounding(              \
            &comp_avg_res_lo, &offset_const, &rounding_const, rounding_shift); \
        const __m256i round_result_hi = highbd_convolve_rounding(              \
            &comp_avg_res_hi, &offset_const, &rounding_const, rounding_shift); \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result_lo, round_result_hi);             \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storeu_si128(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
      s[0] = s[1];                                                             \
      s[2] = s[3];                                                             \
    }                                                                          \
  }

#define CONVOLVE_DIST_WTD_VERT_FILTER_2TAP                                     \
  if (w - j < 8) {                                                             \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 0 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 1 * im_stride));               \
                                                                               \
      s[0] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[1] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = _mm256_madd_epi16(s[0], coeffs_y[0]);              \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);       \
                                                                               \
        const __m256i comp_avg_res = highbd_comp_avg(                          \
            &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);      \
                                                                               \
        const __m256i round_result = highbd_convolve_rounding(                 \
            &comp_avg_res, &offset_const, &rounding_const, rounding_shift);    \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result, round_result);                   \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storel_epi64(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
    }                                                                          \
  } else {                                                                     \
    for (i = 0; i < h; i += 2) {                                               \
      const int16_t *data = &im_block[i * im_stride];                          \
                                                                               \
      const __m256i s6 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 0 * im_stride));               \
      const __m256i s7 =                                                       \
          _mm256_loadu_si256((__m256i *)(data + 1 * im_stride));               \
                                                                               \
      s[0] = _mm256_unpacklo_epi16(s6, s7);                                    \
      s[1] = _mm256_unpackhi_epi16(s6, s7);                                    \
                                                                               \
      const __m256i res_a = _mm256_madd_epi16(s[0], coeffs_y[0]);              \
                                                                               \
      const __m256i res_a_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_a, round_const_y), round_shift_y);              \
                                                                               \
      const __m256i res_unsigned_lo =                                          \
          _mm256_add_epi32(res_a_round, offset_const);                         \
      const __m256i res_b = _mm256_madd_epi16(s[1], coeffs_y[0]);              \
      const __m256i res_b_round = _mm256_sra_epi32(                            \
          _mm256_add_epi32(res_b, round_const_y), round_shift_y);              \
                                                                               \
      __m256i res_unsigned_hi = _mm256_add_epi32(res_b_round, offset_const);   \
                                                                               \
      if (do_average) {                                                        \
        const __m256i data_0 = _mm256_castsi128_si256(                         \
            _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));           \
        const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(         \
            (__m128i *)(&dst[i * dst_stride + j + dst_stride])));              \
        const __m256i data_01 =                                                \
            _mm256_permute2x128_si256(data_0, data_1, 0x20);                   \
                                                                               \
        const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);    \
        const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);    \
                                                                               \
        const __m256i comp_avg_res_lo = highbd_comp_avg(                       \
            &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);   \
        const __m256i comp_avg_res_hi = highbd_comp_avg(                       \
            &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);   \
                                                                               \
        const __m256i round_result_lo = highbd_convolve_rounding(              \
            &comp_avg_res_lo, &offset_const, &rounding_const, rounding_shift); \
        const __m256i round_result_hi = highbd_convolve_rounding(              \
            &comp_avg_res_hi, &offset_const, &rounding_const, rounding_shift); \
                                                                               \
        const __m256i res_16b =                                                \
            _mm256_packus_epi32(round_result_lo, round_result_hi);             \
        const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);  \
                                                                               \
        const __m128i res_0 = _mm256_castsi256_si128(res_clip);                \
        const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);           \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);      \
        _mm_storeu_si128(                                                      \
            (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);     \
      } else {                                                                 \
        __m256i res_16b =                                                      \
            _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);             \
        const __m128i res_0 = _mm256_castsi256_si128(res_16b);                 \
        const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);            \
                                                                               \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);        \
        _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),   \
                         res_1);                                               \
      }                                                                        \
    }                                                                          \
  }

static INLINE __m128i loadh_epi64(const void *const src, __m128i s) {
  return _mm_castps_si128(
      _mm_loadh_pi(_mm_castsi128_ps(s), (const __m64 *)src));
}

static INLINE void _mm_storeh_epi64(__m128i *const d, __m128i s) {
  _mm_storeh_pi((__m64 *)d, _mm_castsi128_ps(s));
}

static INLINE void highbd_dist_wtd_convolve_2d_copy_do_average(
    __m256i data, __m256i zero, __m256i res, const __m256i *offset_const,
    const __m256i *wt0, const __m256i *wt1, int use_wtd_comp_avg,
    const __m256i *const rounding_const, int rounding_shift,
    __m256i clip_pixel_to_bd, __m256i *res_clip) {
  const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data, zero);
  const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data, zero);

  const __m256i res_32b_lo = _mm256_unpacklo_epi16(res, zero);
  const __m256i res_unsigned_lo = _mm256_add_epi32(res_32b_lo, *offset_const);

  const __m256i comp_avg_res_lo = highbd_comp_avg(
      &data_ref_0_lo, &res_unsigned_lo, wt0, wt1, use_wtd_comp_avg);

  const __m256i res_32b_hi = _mm256_unpackhi_epi16(res, zero);
  const __m256i res_unsigned_hi = _mm256_add_epi32(res_32b_hi, *offset_const);

  const __m256i comp_avg_res_hi = highbd_comp_avg(
      &data_ref_0_hi, &res_unsigned_hi, wt0, wt1, use_wtd_comp_avg);

  const __m256i round_result_lo = highbd_convolve_rounding(
      &comp_avg_res_lo, offset_const, rounding_const, rounding_shift);
  const __m256i round_result_hi = highbd_convolve_rounding(
      &comp_avg_res_hi, offset_const, rounding_const, rounding_shift);

  const __m256i res_16b = _mm256_packus_epi32(round_result_lo, round_result_hi);
  *res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);
}

void av2_highbd_dist_wtd_convolve_2d_copy_avx2(const uint16_t *src,
                                               int src_stride, uint16_t *dst0,
                                               int dst_stride0, int w, int h,
                                               ConvolveParams *conv_params,
                                               int bd) {
  CONV_BUF_TYPE *dst = conv_params->dst;
  int dst_stride = conv_params->dst_stride;

  const int bits =
      FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;
  const __m128i left_shift = _mm_cvtsi32_si128(bits);
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);
  const __m256i zero = _mm256_setzero_si256();
  int i, j;

  const int offset_0 =
      bd + 2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset = (1 << offset_0) + (1 << (offset_0 - 1));
  const __m256i offset_const = _mm256_set1_epi32(offset);
  const __m256i offset_const_16b = _mm256_set1_epi16(offset);
  const int rounding_shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const __m256i rounding_const = _mm256_set1_epi32((1 << rounding_shift) >> 1);
  const __m256i clip_pixel_to_bd =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));

  assert(bits <= 4);
  assert((w % 4) == 0);

  if (do_average) {
    if (!(w % 16)) {
      __m256i res_clip;
      for (i = 0; i < h; i += 1) {
        for (j = 0; j < w; j += 16) {
          const __m256i src_16bit =
              _mm256_loadu_si256((__m256i *)(&src[i * src_stride + j]));

          const __m256i res = _mm256_sll_epi16(src_16bit, left_shift);
          const __m256i data_0 =
              _mm256_loadu_si256((__m256i *)(&dst[i * dst_stride + j]));

          highbd_dist_wtd_convolve_2d_copy_do_average(
              data_0, zero, res, &offset_const, &wt0, &wt1, use_wtd_comp_avg,
              &rounding_const, rounding_shift, clip_pixel_to_bd, &res_clip);

          _mm256_storeu_si256((__m256i *)(&dst0[i * dst_stride0 + j]),
                              res_clip);
        }
      }
    } else if (!(w % 8)) {
      __m256i res_clip;
      for (i = 0; i < h; i += 2) {
        for (j = 0; j < w; j += 8) {
          const __m128i src_row_0 =
              _mm_loadu_si128((__m128i *)(&src[i * src_stride + j]));
          const __m128i src_row_1 = _mm_loadu_si128(
              (__m128i *)(&src[i * src_stride + j + src_stride]));
          // since not all compilers yet support _mm256_set_m128i()
          const __m256i src_10 = _mm256_insertf128_si256(
              _mm256_castsi128_si256(src_row_0), src_row_1, 1);

          const __m256i res = _mm256_sll_epi16(src_10, left_shift);

          const __m256i data_0 = _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));
          const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(
              (__m128i *)(&dst[i * dst_stride + j + dst_stride])));
          const __m256i data_01 =
              _mm256_permute2x128_si256(data_0, data_1, 0x20);

          highbd_dist_wtd_convolve_2d_copy_do_average(
              data_01, zero, res, &offset_const, &wt0, &wt1, use_wtd_comp_avg,
              &rounding_const, rounding_shift, clip_pixel_to_bd, &res_clip);

          const __m128i res_0 = _mm256_castsi256_si128(res_clip);
          const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

          _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
          _mm_storeu_si128(
              (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);
        }
      }
    } else {
      // !(w % 4)
      __m256i res_clip;
      for (i = 0; i < h; i += 4) {
        for (j = 0; j < w; j += 8) {
          const __m128i src_row_0 = loadh_epi64(
              (__m128i *)(&src[(i + 1) * src_stride + j]),
              _mm_loadl_epi64((__m128i *)(&src[i * src_stride + j])));
          const __m128i src_row_1 = loadh_epi64(
              (__m128i *)(&src[(i + 3) * src_stride + j]),
              _mm_loadl_epi64((__m128i *)(&src[(i + 2) * src_stride + j])));

          // since not all compilers yet support _mm256_set_m128i()
          const __m256i src_10 = _mm256_insertf128_si256(
              _mm256_castsi128_si256(src_row_0), src_row_1, 1);

          const __m256i res = _mm256_sll_epi16(src_10, left_shift);

          const __m256i data_0 = _mm256_castsi128_si256(loadh_epi64(
              (__m128i *)(&dst[(i + 1) * dst_stride + j]),
              _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j]))));
          const __m256i data_1 = _mm256_castsi128_si256(loadh_epi64(
              (__m128i *)(&dst[(i + 3) * dst_stride + j]),
              _mm_loadl_epi64((__m128i *)(&dst[(i + 2) * dst_stride + j]))));

          const __m256i data_01 =
              _mm256_permute2x128_si256(data_0, data_1, 0x20);

          highbd_dist_wtd_convolve_2d_copy_do_average(
              data_01, zero, res, &offset_const, &wt0, &wt1, use_wtd_comp_avg,
              &rounding_const, rounding_shift, clip_pixel_to_bd, &res_clip);

          const __m128i res_0 = _mm256_castsi256_si128(res_clip);
          const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

          _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
          _mm_storeh_epi64((__m128i *)(&dst0[(i + 1) * dst_stride0 + j]),
                           res_0);
          _mm_storel_epi64((__m128i *)(&dst0[(i + 2) * dst_stride0 + j]),
                           res_1);
          _mm_storeh_epi64((__m128i *)(&dst0[(i + 3) * dst_stride0 + j]),
                           res_1);
        }
      }
    }
  } else {
    // do_average = 0
    if (!(w % 16)) {
      for (i = 0; i < h; i += 1) {
        for (j = 0; j < w; j += 16) {
          const __m256i src_16bit =
              _mm256_loadu_si256((__m256i *)(&src[i * src_stride + j]));

          const __m256i res = _mm256_sll_epi16(src_16bit, left_shift);
          const __m256i res_unsigned_16b =
              _mm256_adds_epu16(res, offset_const_16b);

          _mm256_store_si256((__m256i *)(&dst[i * dst_stride + j]),
                             res_unsigned_16b);
        }
      }
    } else if (!(w % 8)) {
      for (i = 0; i < h; i += 2) {
        for (j = 0; j < w; j += 8) {
          const __m128i src_row_0 =
              _mm_loadu_si128((__m128i *)(&src[i * src_stride + j]));
          const __m128i src_row_1 = _mm_loadu_si128(
              (__m128i *)(&src[i * src_stride + j + src_stride]));
          // since not all compilers yet support _mm256_set_m128i()
          const __m256i src_10 = _mm256_insertf128_si256(
              _mm256_castsi128_si256(src_row_0), src_row_1, 1);

          const __m256i res = _mm256_sll_epi16(src_10, left_shift);
          const __m256i res_unsigned_16b =
              _mm256_adds_epu16(res, offset_const_16b);
          const __m128i res_0 = _mm256_castsi256_si128(res_unsigned_16b);
          const __m128i res_1 = _mm256_extracti128_si256(res_unsigned_16b, 1);

          _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);
          _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),
                           res_1);
        }
      }
    } else {
      // !(w % 4)
      for (i = 0; i < h; i += 4) {
        for (j = 0; j < w; j += 8) {
          const __m128i src_row_0 = loadh_epi64(
              (__m128i *)(&src[(i + 1) * src_stride + j]),
              _mm_loadl_epi64((__m128i *)(&src[i * src_stride + j])));
          const __m128i src_row_1 = loadh_epi64(
              (__m128i *)(&src[(i + 3) * src_stride + j]),
              _mm_loadl_epi64((__m128i *)(&src[(i + 2) * src_stride + j])));

          // since not all compilers yet support _mm256_set_m128i()
          const __m256i src_10 = _mm256_insertf128_si256(
              _mm256_castsi128_si256(src_row_0), src_row_1, 1);

          const __m256i res = _mm256_sll_epi16(src_10, left_shift);

          const __m256i res_unsigned_16b =
              _mm256_adds_epu16(res, offset_const_16b);
          const __m128i res_0 = _mm256_castsi256_si128(res_unsigned_16b);
          const __m128i res_1 = _mm256_extracti128_si256(res_unsigned_16b, 1);
          _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);
          _mm_storeh_epi64((__m128i *)(&dst[(i + 1) * dst_stride + j]), res_0);
          _mm_storel_epi64((__m128i *)(&dst[(i + 2) * dst_stride + j]), res_1);
          _mm_storeh_epi64((__m128i *)(&dst[(i + 3) * dst_stride + j]), res_1);
        }
      }
    }
  }
}

DECLARE_ALIGNED(32, static const uint8_t,
                shuffle_mask0[32]) = { 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6,
                                       7, 6, 7, 8, 9, 0, 1, 2, 3, 2, 3,
                                       4, 5, 4, 5, 6, 7, 6, 7, 8, 9 };

DECLARE_ALIGNED(32, static const uint8_t, shuffle_mask1[32]) = {
  4, 5, 6, 7, 6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13,
  4, 5, 6, 7, 6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13
};

static INLINE void dist_wtd_convolve_horiz_w4(
    const uint16_t *src_ptr, int src_stride, const __m256i *const coeffs,
    int im_h, int16_t *im_block, int im_stride, const __m256i *round_const_x,
    const __m128i *round_shift_x) {
  __m256i s[2];
  for (int i = 0; i < im_h; i += 2) {
    const __m256i row0 =
        _mm256_loadu_si256((__m256i *)&src_ptr[i * src_stride]);
    __m256i row1 = _mm256_set1_epi16(0);
    if (i + 1 < im_h)
      row1 = _mm256_loadu_si256((__m256i *)&src_ptr[(i + 1) * src_stride]);

    const __m256i r0 = _mm256_permute2x128_si256(row0, row1, 0x20);

    s[0] = _mm256_shuffle_epi8(r0, _mm256_load_si256((__m256i *)shuffle_mask0));
    s[1] = _mm256_shuffle_epi8(r0, _mm256_load_si256((__m256i *)shuffle_mask1));

    const __m256i res_0 = _mm256_madd_epi16(s[0], coeffs[0]);
    const __m256i res_1 = _mm256_madd_epi16(s[1], coeffs[1]);
    __m256i res = _mm256_add_epi32(res_0, res_1);
    res =
        _mm256_sra_epi32(_mm256_add_epi32(res, *round_const_x), *round_shift_x);
    _mm256_store_si256((__m256i *)(im_block + i * im_stride),
                       _mm256_packs_epi32(res, res));
  }
}

void av2_highbd_dist_wtd_convolve_2d_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst0, int dst_stride0, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const int32_t tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int32_t tap_y = get_filter_tap(filter_params_y, subpel_y_qn);
  DECLARE_ALIGNED(32, int16_t, im_block[(MAX_SB_SIZE + MAX_FILTER_TAP) * 8]);
  CONV_BUF_TYPE *dst = conv_params->dst;
  int dst_stride = conv_params->dst_stride;
  const int im_h = h + tap_y - 1;
  const int im_stride = 8;
  const int fo_vert = tap_y / 2 - 1;
  const int fo_horiz = tap_x / 2 - 1;
  const uint16_t *const src_ptr = src - fo_vert * src_stride - fo_horiz;
  int i, j;

  // Check that, even with 12-bit input, the intermediate values will fit
  // into an unsigned 16-bit intermediate array.
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m256i s[8];
  __m256i coeffs_y[4] = { 0 };
  __m256i coeffs_x[4] = { 0 };
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);

  const __m256i zero = _mm256_setzero_si256();

  const __m256i round_const_x = _mm256_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const __m256i round_const_y = _mm256_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);

  const int offset_0 =
      bd + 2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset = (1 << offset_0) + (1 << (offset_0 - 1));
  const __m256i offset_const = _mm256_set1_epi32(offset);
  const int rounding_shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const __m256i rounding_const = _mm256_set1_epi32((1 << rounding_shift) >> 1);

  const __m256i clip_pixel_to_bd =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));

  if (tap_x == 8) {
    prepare_coeffs(filter_params_x, subpel_x_qn, coeffs_x);
  } else if (tap_x == 6) {
    prepare_coeffs_6t(filter_params_x, subpel_x_qn, coeffs_x);
  } else if (tap_x == 4) {
    prepare_coeffs_4t(filter_params_x, subpel_x_qn, coeffs_x);
  } else {
    assert(tap_x == 2);
    coeffs_x[0] = prepare_coeffs_bilinear(filter_params_x, subpel_x_qn);
  }

  if (tap_y == 8) {
    prepare_coeffs(filter_params_y, subpel_y_qn, coeffs_y);
  } else if (tap_y == 6) {
    prepare_coeffs_6t(filter_params_y, subpel_y_qn, coeffs_y);
  } else if (tap_y == 4) {
    prepare_coeffs_4t(filter_params_y, subpel_y_qn, coeffs_y);
  } else {
    assert(tap_y == 2);
    coeffs_y[0] = prepare_coeffs_bilinear(filter_params_y, subpel_y_qn);
  }

  for (j = 0; j < w; j += 8) {
    /* Horizontal filter */
    if (w == 4) {
      assert(tap_x == 4);
      dist_wtd_convolve_horiz_w4(src_ptr, src_stride, coeffs_x, im_h, im_block,
                                 im_stride, &round_const_x, &round_shift_x);
    } else {
      if (tap_x == 8) {
        CONVOLVE_HORIZ_FILTER_8TAP
      } else if (tap_x == 6) {
        CONVOLVE_HORIZ_FILTER_6TAP
      } else if (tap_x == 4) {
        CONVOLVE_HORIZ_FILTER_4TAP
      } else {
        CONVOLVE_HORIZ_FILTER_2TAP
      }
    }

    /* Vertical filter */
    if (tap_y == 8) {
      CONVOLVE_DIST_WTD_VERT_FILTER_8TAP
    } else if (tap_y == 6) {
      CONVOLVE_DIST_WTD_VERT_FILTER_6TAP
    } else if (tap_y == 4) {
      CONVOLVE_DIST_WTD_VERT_FILTER_4TAP
    } else {
      CONVOLVE_DIST_WTD_VERT_FILTER_2TAP
    }
  }
}

void av2_highbd_dist_wtd_convolve_x_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst0, int dst_stride0, int w,
    int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn,
    ConvolveParams *conv_params, int bd) {
  CONV_BUF_TYPE *dst = conv_params->dst;
  int dst_stride = conv_params->dst_stride;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const uint16_t *const src_ptr = src - fo_horiz;
  const int bits = FILTER_BITS - conv_params->round_1;

  int i, j;
  __m256i s[4], coeffs_x[4];

  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);
  const __m256i zero = _mm256_setzero_si256();

  const __m256i round_const_x =
      _mm256_set1_epi32(((1 << conv_params->round_0) >> 1));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);
  const __m128i round_shift_bits = _mm_cvtsi32_si128(bits);

  const int offset_0 =
      bd + 2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset = (1 << offset_0) + (1 << (offset_0 - 1));
  const __m256i offset_const = _mm256_set1_epi32(offset);
  const int rounding_shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const __m256i rounding_const = _mm256_set1_epi32((1 << rounding_shift) >> 1);
  const __m256i clip_pixel_to_bd =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));

  assert(bits >= 0);
  prepare_coeffs(filter_params_x, subpel_x_qn, coeffs_x);

  for (j = 0; j < w; j += 8) {
    /* Horizontal filter */
    for (i = 0; i < h; i += 2) {
      const __m256i row0 =
          _mm256_loadu_si256((__m256i *)&src_ptr[i * src_stride + j]);
      __m256i row1 =
          _mm256_loadu_si256((__m256i *)&src_ptr[(i + 1) * src_stride + j]);

      const __m256i r0 = _mm256_permute2x128_si256(row0, row1, 0x20);
      const __m256i r1 = _mm256_permute2x128_si256(row0, row1, 0x31);

      // even pixels
      s[0] = _mm256_alignr_epi8(r1, r0, 0);
      s[1] = _mm256_alignr_epi8(r1, r0, 4);
      s[2] = _mm256_alignr_epi8(r1, r0, 8);
      s[3] = _mm256_alignr_epi8(r1, r0, 12);

      __m256i res_even = convolve(s, coeffs_x);
      res_even = _mm256_sra_epi32(_mm256_add_epi32(res_even, round_const_x),
                                  round_shift_x);

      // odd pixels
      s[0] = _mm256_alignr_epi8(r1, r0, 2);
      s[1] = _mm256_alignr_epi8(r1, r0, 6);
      s[2] = _mm256_alignr_epi8(r1, r0, 10);
      s[3] = _mm256_alignr_epi8(r1, r0, 14);

      __m256i res_odd = convolve(s, coeffs_x);
      res_odd = _mm256_sra_epi32(_mm256_add_epi32(res_odd, round_const_x),
                                 round_shift_x);

      res_even = _mm256_sll_epi32(res_even, round_shift_bits);
      res_odd = _mm256_sll_epi32(res_odd, round_shift_bits);

      __m256i res1 = _mm256_unpacklo_epi32(res_even, res_odd);

      __m256i res_unsigned_lo = _mm256_add_epi32(res1, offset_const);

      if (w - j < 8) {
        if (do_average) {
          const __m256i data_0 = _mm256_castsi128_si256(
              _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));
          const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(
              (__m128i *)(&dst[i * dst_stride + j + dst_stride])));
          const __m256i data_01 =
              _mm256_permute2x128_si256(data_0, data_1, 0x20);

          const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);

          const __m256i comp_avg_res = highbd_comp_avg(
              &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);

          const __m256i round_result = highbd_convolve_rounding(
              &comp_avg_res, &offset_const, &rounding_const, rounding_shift);

          const __m256i res_16b =
              _mm256_packus_epi32(round_result, round_result);
          const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);

          const __m128i res_0 = _mm256_castsi256_si128(res_clip);
          const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

          _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
          _mm_storel_epi64(
              (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);
        } else {
          __m256i res_16b =
              _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);
          const __m128i res_0 = _mm256_castsi256_si128(res_16b);
          const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);

          _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);
          _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),
                           res_1);
        }
      } else {
        __m256i res2 = _mm256_unpackhi_epi32(res_even, res_odd);
        __m256i res_unsigned_hi = _mm256_add_epi32(res2, offset_const);

        if (do_average) {
          const __m256i data_0 = _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));
          const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(
              (__m128i *)(&dst[i * dst_stride + j + dst_stride])));
          const __m256i data_01 =
              _mm256_permute2x128_si256(data_0, data_1, 0x20);

          const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);
          const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);

          const __m256i comp_avg_res_lo = highbd_comp_avg(
              &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);
          const __m256i comp_avg_res_hi = highbd_comp_avg(
              &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);

          const __m256i round_result_lo = highbd_convolve_rounding(
              &comp_avg_res_lo, &offset_const, &rounding_const, rounding_shift);
          const __m256i round_result_hi = highbd_convolve_rounding(
              &comp_avg_res_hi, &offset_const, &rounding_const, rounding_shift);

          const __m256i res_16b =
              _mm256_packus_epi32(round_result_lo, round_result_hi);
          const __m256i res_clip = _mm256_min_epi16(res_16b, clip_pixel_to_bd);

          const __m128i res_0 = _mm256_castsi256_si128(res_clip);
          const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

          _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
          _mm_storeu_si128(
              (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);
        } else {
          __m256i res_16b =
              _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);
          const __m128i res_0 = _mm256_castsi256_si128(res_16b);
          const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);

          _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);
          _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),
                           res_1);
        }
      }
    }
  }
}

void av2_highbd_dist_wtd_convolve_y_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst0, int dst_stride0, int w,
    int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn,
    ConvolveParams *conv_params, int bd) {
  CONV_BUF_TYPE *dst = conv_params->dst;
  int dst_stride = conv_params->dst_stride;
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const uint16_t *const src_ptr = src - fo_vert * src_stride;
  const int bits = FILTER_BITS - conv_params->round_0;

  assert(bits >= 0);
  int i, j;
  __m256i s[8], coeffs_y[4];
  const int do_average = conv_params->do_average;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);

  const __m256i round_const_y =
      _mm256_set1_epi32(((1 << conv_params->round_1) >> 1));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);
  const __m128i round_shift_bits = _mm_cvtsi32_si128(bits);

  const int offset_0 =
      bd + 2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset = (1 << offset_0) + (1 << (offset_0 - 1));
  const __m256i offset_const = _mm256_set1_epi32(offset);
  const int rounding_shift =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const __m256i rounding_const = _mm256_set1_epi32((1 << rounding_shift) >> 1);
  const __m256i clip_pixel_to_bd =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  const __m256i zero = _mm256_setzero_si256();

  prepare_coeffs(filter_params_y, subpel_y_qn, coeffs_y);

  for (j = 0; j < w; j += 8) {
    const uint16_t *data = &src_ptr[j];
    /* Vertical filter */
    {
      __m256i src6;
      __m256i s01 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 0 * src_stride))),
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 1 * src_stride))),
          0x20);
      __m256i s12 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 1 * src_stride))),
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 2 * src_stride))),
          0x20);
      __m256i s23 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 2 * src_stride))),
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 3 * src_stride))),
          0x20);
      __m256i s34 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 3 * src_stride))),
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 4 * src_stride))),
          0x20);
      __m256i s45 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 4 * src_stride))),
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 5 * src_stride))),
          0x20);
      src6 = _mm256_castsi128_si256(
          _mm_loadu_si128((__m128i *)(data + 6 * src_stride)));
      __m256i s56 = _mm256_permute2x128_si256(
          _mm256_castsi128_si256(
              _mm_loadu_si128((__m128i *)(data + 5 * src_stride))),
          src6, 0x20);

      s[0] = _mm256_unpacklo_epi16(s01, s12);
      s[1] = _mm256_unpacklo_epi16(s23, s34);
      s[2] = _mm256_unpacklo_epi16(s45, s56);

      s[4] = _mm256_unpackhi_epi16(s01, s12);
      s[5] = _mm256_unpackhi_epi16(s23, s34);
      s[6] = _mm256_unpackhi_epi16(s45, s56);

      for (i = 0; i < h; i += 2) {
        data = &src_ptr[i * src_stride + j];

        const __m256i s67 = _mm256_permute2x128_si256(
            src6,
            _mm256_castsi128_si256(
                _mm_loadu_si128((__m128i *)(data + 7 * src_stride))),
            0x20);

        src6 = _mm256_castsi128_si256(
            _mm_loadu_si128((__m128i *)(data + 8 * src_stride)));

        const __m256i s78 = _mm256_permute2x128_si256(
            _mm256_castsi128_si256(
                _mm_loadu_si128((__m128i *)(data + 7 * src_stride))),
            src6, 0x20);

        s[3] = _mm256_unpacklo_epi16(s67, s78);
        s[7] = _mm256_unpackhi_epi16(s67, s78);

        const __m256i res_a = convolve(s, coeffs_y);

        __m256i res_a_round = _mm256_sll_epi32(res_a, round_shift_bits);
        res_a_round = _mm256_sra_epi32(
            _mm256_add_epi32(res_a_round, round_const_y), round_shift_y);

        __m256i res_unsigned_lo = _mm256_add_epi32(res_a_round, offset_const);

        if (w - j < 8) {
          if (do_average) {
            const __m256i data_0 = _mm256_castsi128_si256(
                _mm_loadl_epi64((__m128i *)(&dst[i * dst_stride + j])));
            const __m256i data_1 = _mm256_castsi128_si256(_mm_loadl_epi64(
                (__m128i *)(&dst[i * dst_stride + j + dst_stride])));
            const __m256i data_01 =
                _mm256_permute2x128_si256(data_0, data_1, 0x20);

            const __m256i data_ref_0 = _mm256_unpacklo_epi16(data_01, zero);

            const __m256i comp_avg_res = highbd_comp_avg(
                &data_ref_0, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);

            const __m256i round_result = highbd_convolve_rounding(
                &comp_avg_res, &offset_const, &rounding_const, rounding_shift);

            const __m256i res_16b =
                _mm256_packus_epi32(round_result, round_result);
            const __m256i res_clip =
                _mm256_min_epi16(res_16b, clip_pixel_to_bd);

            const __m128i res_0 = _mm256_castsi256_si128(res_clip);
            const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

            _mm_storel_epi64((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
            _mm_storel_epi64(
                (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);
          } else {
            __m256i res_16b =
                _mm256_packus_epi32(res_unsigned_lo, res_unsigned_lo);
            const __m128i res_0 = _mm256_castsi256_si128(res_16b);
            const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);

            _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j]), res_0);
            _mm_storel_epi64((__m128i *)(&dst[i * dst_stride + j + dst_stride]),
                             res_1);
          }
        } else {
          const __m256i res_b = convolve(s + 4, coeffs_y);
          __m256i res_b_round = _mm256_sll_epi32(res_b, round_shift_bits);
          res_b_round = _mm256_sra_epi32(
              _mm256_add_epi32(res_b_round, round_const_y), round_shift_y);

          __m256i res_unsigned_hi = _mm256_add_epi32(res_b_round, offset_const);

          if (do_average) {
            const __m256i data_0 = _mm256_castsi128_si256(
                _mm_loadu_si128((__m128i *)(&dst[i * dst_stride + j])));
            const __m256i data_1 = _mm256_castsi128_si256(_mm_loadu_si128(
                (__m128i *)(&dst[i * dst_stride + j + dst_stride])));
            const __m256i data_01 =
                _mm256_permute2x128_si256(data_0, data_1, 0x20);

            const __m256i data_ref_0_lo = _mm256_unpacklo_epi16(data_01, zero);
            const __m256i data_ref_0_hi = _mm256_unpackhi_epi16(data_01, zero);

            const __m256i comp_avg_res_lo = highbd_comp_avg(
                &data_ref_0_lo, &res_unsigned_lo, &wt0, &wt1, use_wtd_comp_avg);
            const __m256i comp_avg_res_hi = highbd_comp_avg(
                &data_ref_0_hi, &res_unsigned_hi, &wt0, &wt1, use_wtd_comp_avg);

            const __m256i round_result_lo =
                highbd_convolve_rounding(&comp_avg_res_lo, &offset_const,
                                         &rounding_const, rounding_shift);
            const __m256i round_result_hi =
                highbd_convolve_rounding(&comp_avg_res_hi, &offset_const,
                                         &rounding_const, rounding_shift);

            const __m256i res_16b =
                _mm256_packus_epi32(round_result_lo, round_result_hi);
            const __m256i res_clip =
                _mm256_min_epi16(res_16b, clip_pixel_to_bd);

            const __m128i res_0 = _mm256_castsi256_si128(res_clip);
            const __m128i res_1 = _mm256_extracti128_si256(res_clip, 1);

            _mm_storeu_si128((__m128i *)(&dst0[i * dst_stride0 + j]), res_0);
            _mm_storeu_si128(
                (__m128i *)(&dst0[i * dst_stride0 + j + dst_stride0]), res_1);
          } else {
            __m256i res_16b =
                _mm256_packus_epi32(res_unsigned_lo, res_unsigned_hi);
            const __m128i res_0 = _mm256_castsi256_si128(res_16b);
            const __m128i res_1 = _mm256_extracti128_si256(res_16b, 1);

            _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j]), res_0);
            _mm_storeu_si128((__m128i *)(&dst[i * dst_stride + j + dst_stride]),
                             res_1);
          }
        }
        s[0] = s[1];
        s[1] = s[2];
        s[2] = s[3];

        s[4] = s[5];
        s[5] = s[6];
        s[6] = s[7];
      }
    }
  }
}
