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

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "av1/common/convolve.h"
#include "av1/common/restoration.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/aom_filter.h"
#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"

// 128-bit xmmwords are written as [ ... ] with the MSB on the left.
// 256-bit ymmwords are written as two xmmwords, [ ... ][ ... ] with the MSB
// on the left.
// A row of, say, 16-bit pixels with values p0, p1, p2, ..., p14, p15 will be
// loaded and stored as [ p15 ... p9 p8 ][ p7 ... p1 p0 ].
void av1_highbd_wiener_convolve_add_src_avx2(
    const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,
    ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4,
    const int16_t *filter_y, int y_step_q4, int w, int h,
    const WienerConvolveParams *conv_params, int bd) {
  assert(x_step_q4 == 16 && y_step_q4 == 16);
  assert(!(w & 7));
  assert(bd + FILTER_BITS - conv_params->round_0 + 2 <= 16);
  (void)x_step_q4;
  (void)y_step_q4;

  DECLARE_ALIGNED(32, uint16_t,
                  temp[(MAX_SB_SIZE + SUBPEL_TAPS - 1) * MAX_SB_SIZE]);
  int intermediate_height = h + SUBPEL_TAPS - 1;
  const int center_tap = ((SUBPEL_TAPS - 1) / 2);
  const uint16_t *const src_ptr = src - center_tap * src_stride - center_tap;

  const __m128i zero_128 = _mm_setzero_si128();
  const __m256i zero_256 = _mm256_setzero_si256();

  // Add an offset to account for the "add_src" part of the convolve function.
  const __m128i offset = _mm_insert_epi16(zero_128, 1 << FILTER_BITS, 3);

  const __m256i clamp_low = zero_256;

  /* Horizontal filter */
  {
    const __m256i clamp_high_ep =
        _mm256_set1_epi16(WIENER_CLAMP_LIMIT(conv_params->round_0, bd) - 1);

    // coeffs [ f7 f6 f5 f4 f3 f2 f1 f0 ]
    const __m128i coeffs_x = _mm_add_epi16(xx_loadu_128(filter_x), offset);

    // coeffs [ f3 f2 f3 f2 f1 f0 f1 f0 ]
    const __m128i coeffs_0123 = _mm_unpacklo_epi32(coeffs_x, coeffs_x);
    // coeffs [ f7 f6 f7 f6 f5 f4 f5 f4 ]
    const __m128i coeffs_4567 = _mm_unpackhi_epi32(coeffs_x, coeffs_x);

    // coeffs [ f1 f0 f1 f0 f1 f0 f1 f0 ]
    const __m128i coeffs_01_128 = _mm_unpacklo_epi64(coeffs_0123, coeffs_0123);
    // coeffs [ f3 f2 f3 f2 f3 f2 f3 f2 ]
    const __m128i coeffs_23_128 = _mm_unpackhi_epi64(coeffs_0123, coeffs_0123);
    // coeffs [ f5 f4 f5 f4 f5 f4 f5 f4 ]
    const __m128i coeffs_45_128 = _mm_unpacklo_epi64(coeffs_4567, coeffs_4567);
    // coeffs [ f7 f6 f7 f6 f7 f6 f7 f6 ]
    const __m128i coeffs_67_128 = _mm_unpackhi_epi64(coeffs_4567, coeffs_4567);

    // coeffs [ f1 f0 f1 f0 f1 f0 f1 f0 ][ f1 f0 f1 f0 f1 f0 f1 f0 ]
    const __m256i coeffs_01 = yy_set_m128i(coeffs_01_128, coeffs_01_128);
    // coeffs [ f3 f2 f3 f2 f3 f2 f3 f2 ][ f3 f2 f3 f2 f3 f2 f3 f2 ]
    const __m256i coeffs_23 = yy_set_m128i(coeffs_23_128, coeffs_23_128);
    // coeffs [ f5 f4 f5 f4 f5 f4 f5 f4 ][ f5 f4 f5 f4 f5 f4 f5 f4 ]
    const __m256i coeffs_45 = yy_set_m128i(coeffs_45_128, coeffs_45_128);
    // coeffs [ f7 f6 f7 f6 f7 f6 f7 f6 ][ f7 f6 f7 f6 f7 f6 f7 f6 ]
    const __m256i coeffs_67 = yy_set_m128i(coeffs_67_128, coeffs_67_128);

    const __m256i round_const = _mm256_set1_epi32(
        (1 << (conv_params->round_0 - 1)) + (1 << (bd + FILTER_BITS - 1)));

    for (int i = 0; i < intermediate_height; ++i) {
      for (int j = 0; j < w; j += 16) {
        const uint16_t *src_ij = src_ptr + i * src_stride + j;

        // Load 16-bit src data
        const __m256i src_0 = yy_loadu_256(src_ij + 0);
        const __m256i src_1 = yy_loadu_256(src_ij + 1);
        const __m256i src_2 = yy_loadu_256(src_ij + 2);
        const __m256i src_3 = yy_loadu_256(src_ij + 3);
        const __m256i src_4 = yy_loadu_256(src_ij + 4);
        const __m256i src_5 = yy_loadu_256(src_ij + 5);
        const __m256i src_6 = yy_loadu_256(src_ij + 6);
        const __m256i src_7 = yy_loadu_256(src_ij + 7);

        // Multiply src data by filter coeffs and sum pairs
        const __m256i res_0 = _mm256_madd_epi16(src_0, coeffs_01);
        const __m256i res_1 = _mm256_madd_epi16(src_1, coeffs_01);
        const __m256i res_2 = _mm256_madd_epi16(src_2, coeffs_23);
        const __m256i res_3 = _mm256_madd_epi16(src_3, coeffs_23);
        const __m256i res_4 = _mm256_madd_epi16(src_4, coeffs_45);
        const __m256i res_5 = _mm256_madd_epi16(src_5, coeffs_45);
        const __m256i res_6 = _mm256_madd_epi16(src_6, coeffs_67);
        const __m256i res_7 = _mm256_madd_epi16(src_7, coeffs_67);

        // Calculate scalar product for even- and odd-indices separately,
        // increasing to 32-bit precision
        const __m256i res_even_sum = _mm256_add_epi32(
            _mm256_add_epi32(res_0, res_4), _mm256_add_epi32(res_2, res_6));
        const __m256i res_even = _mm256_srai_epi32(
            _mm256_add_epi32(res_even_sum, round_const), conv_params->round_0);

        const __m256i res_odd_sum = _mm256_add_epi32(
            _mm256_add_epi32(res_1, res_5), _mm256_add_epi32(res_3, res_7));
        const __m256i res_odd = _mm256_srai_epi32(
            _mm256_add_epi32(res_odd_sum, round_const), conv_params->round_0);

        // Reduce to 16-bit precision and pack even- and odd-index results
        // back into one register. The _mm256_packs_epi32 intrinsic returns
        // a register with the pixels ordered as follows:
        // [ 15 13 11 9 14 12 10 8 ] [ 7 5 3 1 6 4 2 0 ]
        const __m256i res = _mm256_packs_epi32(res_even, res_odd);
        const __m256i res_clamped =
            _mm256_min_epi16(_mm256_max_epi16(res, clamp_low), clamp_high_ep);

        // Store in a temporary array
        yy_storeu_256(temp + i * MAX_SB_SIZE + j, res_clamped);
      }
    }
  }

  /* Vertical filter */
  {
    const __m256i clamp_high = _mm256_set1_epi16((1 << bd) - 1);

    // coeffs [ f7 f6 f5 f4 f3 f2 f1 f0 ]
    const __m128i coeffs_y = _mm_add_epi16(xx_loadu_128(filter_y), offset);

    // coeffs [ f3 f2 f3 f2 f1 f0 f1 f0 ]
    const __m128i coeffs_0123 = _mm_unpacklo_epi32(coeffs_y, coeffs_y);
    // coeffs [ f7 f6 f7 f6 f5 f4 f5 f4 ]
    const __m128i coeffs_4567 = _mm_unpackhi_epi32(coeffs_y, coeffs_y);

    // coeffs [ f1 f0 f1 f0 f1 f0 f1 f0 ]
    const __m128i coeffs_01_128 = _mm_unpacklo_epi64(coeffs_0123, coeffs_0123);
    // coeffs [ f3 f2 f3 f2 f3 f2 f3 f2 ]
    const __m128i coeffs_23_128 = _mm_unpackhi_epi64(coeffs_0123, coeffs_0123);
    // coeffs [ f5 f4 f5 f4 f5 f4 f5 f4 ]
    const __m128i coeffs_45_128 = _mm_unpacklo_epi64(coeffs_4567, coeffs_4567);
    // coeffs [ f7 f6 f7 f6 f7 f6 f7 f6 ]
    const __m128i coeffs_67_128 = _mm_unpackhi_epi64(coeffs_4567, coeffs_4567);

    // coeffs [ f1 f0 f1 f0 f1 f0 f1 f0 ][ f1 f0 f1 f0 f1 f0 f1 f0 ]
    const __m256i coeffs_01 = yy_set_m128i(coeffs_01_128, coeffs_01_128);
    // coeffs [ f3 f2 f3 f2 f3 f2 f3 f2 ][ f3 f2 f3 f2 f3 f2 f3 f2 ]
    const __m256i coeffs_23 = yy_set_m128i(coeffs_23_128, coeffs_23_128);
    // coeffs [ f5 f4 f5 f4 f5 f4 f5 f4 ][ f5 f4 f5 f4 f5 f4 f5 f4 ]
    const __m256i coeffs_45 = yy_set_m128i(coeffs_45_128, coeffs_45_128);
    // coeffs [ f7 f6 f7 f6 f7 f6 f7 f6 ][ f7 f6 f7 f6 f7 f6 f7 f6 ]
    const __m256i coeffs_67 = yy_set_m128i(coeffs_67_128, coeffs_67_128);

    const __m256i round_const =
        _mm256_set1_epi32((1 << (conv_params->round_1 - 1)) -
                          (1 << (bd + conv_params->round_1 - 1)));

    for (int i = 0; i < h; ++i) {
      for (int j = 0; j < w; j += 16) {
        const uint16_t *temp_ij = temp + i * MAX_SB_SIZE + j;

        // Load 16-bit data from the output of the horizontal filter in
        // which the pixels are ordered as follows:
        // [ 15 13 11 9 14 12 10 8 ] [ 7 5 3 1 6 4 2 0 ]
        const __m256i data_0 = yy_loadu_256(temp_ij + 0 * MAX_SB_SIZE);
        const __m256i data_1 = yy_loadu_256(temp_ij + 1 * MAX_SB_SIZE);
        const __m256i data_2 = yy_loadu_256(temp_ij + 2 * MAX_SB_SIZE);
        const __m256i data_3 = yy_loadu_256(temp_ij + 3 * MAX_SB_SIZE);
        const __m256i data_4 = yy_loadu_256(temp_ij + 4 * MAX_SB_SIZE);
        const __m256i data_5 = yy_loadu_256(temp_ij + 5 * MAX_SB_SIZE);
        const __m256i data_6 = yy_loadu_256(temp_ij + 6 * MAX_SB_SIZE);
        const __m256i data_7 = yy_loadu_256(temp_ij + 7 * MAX_SB_SIZE);

        // Filter the even-indices, increasing to 32-bit precision
        const __m256i src_0 = _mm256_unpacklo_epi16(data_0, data_1);
        const __m256i src_2 = _mm256_unpacklo_epi16(data_2, data_3);
        const __m256i src_4 = _mm256_unpacklo_epi16(data_4, data_5);
        const __m256i src_6 = _mm256_unpacklo_epi16(data_6, data_7);

        const __m256i res_0 = _mm256_madd_epi16(src_0, coeffs_01);
        const __m256i res_2 = _mm256_madd_epi16(src_2, coeffs_23);
        const __m256i res_4 = _mm256_madd_epi16(src_4, coeffs_45);
        const __m256i res_6 = _mm256_madd_epi16(src_6, coeffs_67);

        const __m256i res_even = _mm256_add_epi32(
            _mm256_add_epi32(res_0, res_2), _mm256_add_epi32(res_4, res_6));

        // Filter the odd-indices, increasing to 32-bit precision
        const __m256i src_1 = _mm256_unpackhi_epi16(data_0, data_1);
        const __m256i src_3 = _mm256_unpackhi_epi16(data_2, data_3);
        const __m256i src_5 = _mm256_unpackhi_epi16(data_4, data_5);
        const __m256i src_7 = _mm256_unpackhi_epi16(data_6, data_7);

        const __m256i res_1 = _mm256_madd_epi16(src_1, coeffs_01);
        const __m256i res_3 = _mm256_madd_epi16(src_3, coeffs_23);
        const __m256i res_5 = _mm256_madd_epi16(src_5, coeffs_45);
        const __m256i res_7 = _mm256_madd_epi16(src_7, coeffs_67);

        const __m256i res_odd = _mm256_add_epi32(
            _mm256_add_epi32(res_1, res_3), _mm256_add_epi32(res_5, res_7));

        // Pixels are currently in the following order:
        // res_even order: [ 14 12 10 8 ] [ 6 4 2 0 ]
        // res_odd order:  [ 15 13 11 9 ] [ 7 5 3 1 ]
        //
        // Rearrange the pixels into the following order:
        // res_lo order: [ 11 10  9  8 ] [ 3 2 1 0 ]
        // res_hi order: [ 15 14 13 12 ] [ 7 6 5 4 ]
        const __m256i res_lo = _mm256_unpacklo_epi32(res_even, res_odd);
        const __m256i res_hi = _mm256_unpackhi_epi32(res_even, res_odd);

        const __m256i res_lo_round = _mm256_srai_epi32(
            _mm256_add_epi32(res_lo, round_const), conv_params->round_1);
        const __m256i res_hi_round = _mm256_srai_epi32(
            _mm256_add_epi32(res_hi, round_const), conv_params->round_1);

        // Reduce to 16-bit precision and pack into the correct order:
        // [ 15 14 13 12 11 10 9 8 ][ 7 6 5 4 3 2 1 0 ]
        const __m256i res_16bit =
            _mm256_packs_epi32(res_lo_round, res_hi_round);
        const __m256i res_16bit_clamped = _mm256_min_epi16(
            _mm256_max_epi16(res_16bit, clamp_low), clamp_high);

        // Store in the dst array
        yy_storeu_256(dst + i * dst_stride + j, res_16bit_clamped);
      }
    }
  }
}

// 256bit intrinsic implementation of ROUND_POWER_OF_TWO_SIGNED.
static INLINE __m256i round_power_of_two_signed_avx2(__m256i v_val_d,
                                                     int bits) {
  const __m256i v_bias_d = _mm256_set1_epi32((1 << bits) >> 1);
  const __m256i v_sign_d = _mm256_srai_epi32(v_val_d, 31);
  const __m256i v_tmp_d =
      _mm256_add_epi32(_mm256_add_epi32(v_val_d, v_bias_d), v_sign_d);
  return _mm256_srai_epi32(v_tmp_d, bits);
}

// Clip the pixel values to zero/max.
static __m256i highbd_clamp_epi16(__m256i u, __m256i zero, __m256i max) {
  return _mm256_max_epi16(_mm256_min_epi16(u, max), zero);
}

// Downcast 256bit register to 128bit by considering lower 128bit.
#define CAST_LOW(a) _mm256_castsi128_si256(a)

// Load source data required for chroma filtering into registers.
#define LOAD_SRC_DATA_FOR_CHROMA_FILTERING(src, stride)                        \
  const __m128i src_a0 = _mm_loadu_si128((__m128i const *)src);                \
  const __m128i src_b0 = _mm_loadu_si128((__m128i const *)(src + stride));     \
  const __m128i src_c0 = _mm_loadu_si128((__m128i const *)(src + 2 * stride)); \
  const __m128i src_d0 = _mm_loadu_si128((__m128i const *)(src + 3 * stride)); \
  const __m128i src_e0 = _mm_loadu_si128((__m128i const *)(src + 4 * stride)); \
  const __m128i src_f0 = _mm_loadu_si128((__m128i const *)(src + 5 * stride)); \
  const __m128i src_g0 = _mm_loadu_si128((__m128i const *)(src + 6 * stride)); \
  const __m128i src_h0 = _mm_loadu_si128((__m128i const *)(src + 7 * stride));

// Forms 256bit source registers from loaded 128bit registers and pack them
// accordingly for filtering process.
#define FORM_AND_PACK_REG_FOR_CHROMA_FILTERING()                               \
  /* Form 256bit source registers.*/                                           \
  /* a0 a1 a2 a3 a4 a5 a6 a7 | b0 b1 b2 b3 b4 b5 b6 b7*/                       \
  const __m256i src_ab = _mm256_inserti128_si256(CAST_LOW(src_a0), src_b0, 1); \
  /* c0 c1 c2 c3 c4 c5 c6 c7 | d0 d1 d2 d3 d4 d5 d6 d7*/                       \
  const __m256i src_cd = _mm256_inserti128_si256(CAST_LOW(src_c0), src_d0, 1); \
  /* e0 e1 e2 e3 e4 e5 e6 e7 | f0 f1 f2 f3 f4 f5 f6 f7*/                       \
  const __m256i src_ef = _mm256_inserti128_si256(CAST_LOW(src_e0), src_f0, 1); \
  /* g0 g1 g2 g3 g4 g5 g6 g7 | h0 h1 h2 h3 h4 h5 h6 h7*/                       \
  const __m256i src_gh = _mm256_inserti128_si256(CAST_LOW(src_g0), src_h0, 1); \
  /* b0 b1 b2 b3 b4 b5 b6 b7 | c0 c1 c2 c3 c4 c5 c6 c7*/                       \
  const __m256i src_bc = _mm256_inserti128_si256(CAST_LOW(src_b0), src_c0, 1); \
  /* d0 d1 d2 d3 d4 d5 d6 d7 | e0 e1 e2 e3 e4 e5 e6 e7*/                       \
  const __m256i src_de = _mm256_inserti128_si256(CAST_LOW(src_d0), src_e0, 1); \
  /* f0 f1 f2 f3 f4 f5 f6 f7 | g0 g1 g2 g3 g4 g5 g6 g7*/                       \
  const __m256i src_fg = _mm256_inserti128_si256(CAST_LOW(src_f0), src_g0, 1); \
  /* Packing the source rows.*/                                                \
  /* a0 e0 a1 e1 a2 e2 a3 e3 | b0 f0 b1 f1 b2 f2 b3 f3*/                       \
  const __m256i ru0 = _mm256_unpacklo_epi16(src_ab, src_ef);                   \
  /* a4 e4 a5 e5 a6 e6 a7 e7 | b4 f4 b5 f5 b6 f6 b7 f7*/                       \
  const __m256i ru1 = _mm256_unpackhi_epi16(src_ab, src_ef);                   \
  /* c0 g0 c1 g1 c2 g2 c3 g3 | d0 h0 d1 h1 d2 h2 d3 h3*/                       \
  const __m256i ru2 = _mm256_unpacklo_epi16(src_cd, src_gh);                   \
  /* c4 g4 c5 g5 c6 g6 c7 g7 | d4 h4 d5 h5 d6 h6 d7 h7*/                       \
  const __m256i ru3 = _mm256_unpackhi_epi16(src_cd, src_gh);                   \
  /* b0 d0 b1 d1 b2 d2 b3 d3 | c0 e0 c1 e1 c2 e2 c3 e3*/                       \
  const __m256i ru4 = _mm256_unpacklo_epi16(src_bc, src_de);                   \
  /* b4 d4 b5 d5 b6 d6 b7 d7 | c4 e4 c5 e5 c6 e6 c7 e7*/                       \
  const __m256i ru5 = _mm256_unpackhi_epi16(src_bc, src_de);                   \
  /* d0 f0 d1 f1 d2 f2 d3 f3 | e0 g0 e1 g1 e2 g2 e3 g3*/                       \
  const __m256i ru6 = _mm256_unpacklo_epi16(src_de, src_fg);                   \
  /* d4 f4 d5 f5 d6 f6 d7 f7 | e4 g4 e5 g5 e6 g6 e7 g7*/                       \
  const __m256i ru7 = _mm256_unpackhi_epi16(src_de, src_fg);

// Multiply the formed registers with filter and add the result to accumulate
// registers.
#define MADD_AND_ACCUM_FOR_CHROMA_FILTERING(f0, f1, f2)         \
  {                                                             \
    const __m256i res_0 = _mm256_madd_epi16(ru10, f0);          \
    const __m256i res_1 = _mm256_madd_epi16(ru11, f0);          \
    const __m256i res_2 = _mm256_madd_epi16(ru12, f1);          \
    const __m256i res_3 = _mm256_madd_epi16(ru13, f1);          \
    const __m256i res_4 = _mm256_madd_epi16(ru14, f2);          \
    const __m256i res_5 = _mm256_madd_epi16(ru15, f2);          \
    /* r00 r01 r02 r03 | r10 r11 r12 r13 */                     \
    const __m256i out_0 = _mm256_add_epi32(res_0, res_2);       \
    const __m256i out_1 = _mm256_add_epi32(res_4, out_0);       \
    *accum_out_r0r1 = _mm256_add_epi32(out_1, *accum_out_r0r1); \
    /* r20 r21 r22 r23 | r30 r31 r32 r33 */                     \
    const __m256i out_2 = _mm256_add_epi32(res_1, res_3);       \
    const __m256i out_3 = _mm256_add_epi32(res_5, out_2);       \
    *accum_out_r2r3 = _mm256_add_epi32(out_3, *accum_out_r2r3); \
  }

// Variant with only two sets of pixels and coefficients
#define MADD_AND_ACCUM_FOR_CHROMA_FILTERING_2(f0, f1)           \
  {                                                             \
    const __m256i res_0 = _mm256_madd_epi16(ru10, f0);          \
    const __m256i res_1 = _mm256_madd_epi16(ru11, f0);          \
    const __m256i res_2 = _mm256_madd_epi16(ru12, f1);          \
    const __m256i res_3 = _mm256_madd_epi16(ru13, f1);          \
    /* r00 r01 r02 r03 | r10 r11 r12 r13 */                     \
    const __m256i out_0 = _mm256_add_epi32(res_0, res_2);       \
    *accum_out_r0r1 = _mm256_add_epi32(out_0, *accum_out_r0r1); \
    /* r20 r21 r22 r23 | r30 r31 r32 r33 */                     \
    const __m256i out_1 = _mm256_add_epi32(res_1, res_3);       \
    *accum_out_r2r3 = _mm256_add_epi32(out_1, *accum_out_r2r3); \
  }

// Implementation of DIAMOND shaped 7-tap filtering for block size of 4x4.
// The output for a particular pixel in a 4x4 block is calculated by considering
// a 5x5 grid surrounded by that pixel. The registers accum_out_r0r1 and
// accum_out_r2r3 are used to store the output. The following describes the
// algorithm briefly.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 fc6 x
// Load Source Data:
// src_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// src_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// src_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// src_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// src_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// src_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// src_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at c2 position is calculated as below.
// Filtered_c2 = (a2+e2)*fc4 + (b1+d3)*fc2 + (b2+d2)*fc0 + (b3+d1)*fc3 +
// (c0+c4)*fc5 + (c1+c3)*fc1 + c2*fc6
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc4 of rows 0 and 1 is achieved like below.
// __m256i src_reg3 = a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
// __m256i filter_4 = fc4 fc4 fc4 fc4 fc4 fc4 fc4 fc4 | fc4 fc4 fc4 fc4 fc4 fc4
// fc4 fc4
//  __m256i out_f4_01 = _mm256_madd_epi16(src_reg3, filter_4);
//                   = (a2*fc4+e2*fc4) (a3*fc4+e3*fc4) .. | (b2*fc4+f2*fc4) . .
// Here, out_f4_01 contains partial output of rows 0 and 1 corresponding to fc4.
static INLINE void apply_7tap_filtering_with_subtract_center_off(
    const uint16_t *dgd, int stride, const __m128i filt_coeff,
    __m256i *accum_out_r0r1, __m256i *accum_out_r2r3, int block_row_begin,
    int block_col_begin) {
  // Load source data
  const int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 2 * stride - 2;

  // Load source data required for chroma filtering into registers.
  LOAD_SRC_DATA_FOR_CHROMA_FILTERING(src_ptr, stride)

  // Forms 256bit source registers from loaded 128bit registers and pack them
  // accordingly for filtering process.
  FORM_AND_PACK_REG_FOR_CHROMA_FILTERING()

  // Output corresponding to filter coefficient f4.
  // a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
  const __m256i ru8 = _mm256_alignr_epi8(ru1, ru0, 8);
  // c2 g2 c3 g3 c4 g4 c5 g5 | d2 h2 d3 h3 d4 h4 d5 h5
  const __m256i ru9 = _mm256_alignr_epi8(ru3, ru2, 8);

  // f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4
  const __m256i fc4 =
      _mm256_broadcastd_epi32(_mm_unpackhi_epi16(filt_coeff, filt_coeff));
  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_f4_r0r1 = _mm256_madd_epi16(ru8, fc4);
  *accum_out_r0r1 = _mm256_add_epi32(out_f4_r0r1, *accum_out_r0r1);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_f4_r2r3 = _mm256_madd_epi16(ru9, fc4);
  *accum_out_r2r3 = _mm256_add_epi32(out_f4_r2r3, *accum_out_r2r3);

  // Output corresponding to filter coefficient 2, 0, 3.
  // b1 d1 b2 d2 b3 d3 b4 d4 | c1 e1 c2 e2 c3 e3 c4 e4
  __m256i ru10 = _mm256_alignr_epi8(ru5, ru4, 4);
  // d1 f1 d2 f2 d3 f3 d4 f4 | e1 g1 e2 g2 e3 g3 e4 g4
  __m256i ru11 = _mm256_alignr_epi8(ru7, ru6, 4);
  // b2 d2 b3 d3 b4 d4 b5 d5 | c2 e2 c3 e3 c4 e4 c5 e5
  __m256i ru12 = _mm256_alignr_epi8(ru5, ru4, 8);
  // d2 f2 d3 f3 d4 f4 d5 f5 | e2 g2 e3 g3 e4 g4 e5 g5
  __m256i ru13 = _mm256_alignr_epi8(ru7, ru6, 8);
  // b3 d3 b4 d4 b5 d5 b6 d6 | c3 e3 c4 e4 c5 e5 c6 e6
  __m256i ru14 = _mm256_alignr_epi8(ru5, ru4, 12);
  // d3 f3 d4 f4 d5 f5 d6 f6 | e3 g3 e4 g4 e5 g5 e6 g6
  __m256i ru15 = _mm256_alignr_epi8(ru7, ru6, 12);

  // f2 f3 f2 f3 - - - -
  const __m256i fc23 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff, 0x0E));
  // f0 f0 f0 f0 - - - -
  const __m256i fc00 = _mm256_broadcastw_epi16(filt_coeff);
  // f3 f2 f3 f2 - - - -
  const __m256i fc32 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff, 0x0B));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc23, fc00, fc32)

  // Output corresponding to filter coefficient 5, 1, 6.
  const __m256i zero = _mm256_set1_epi16(0x0);
  // c0 c1 c1 c2 c2 c3 c3 c4 || d0 d1 d1 d2 d2 d3 d3 d4
  ru10 = _mm256_unpacklo_epi16(src_cd, _mm256_bsrli_epi128(src_cd, 2));
  // e0 e1 e1 e2 e2 e3 e3 e4 || f0 f1 f1 f2 f2 f3 f3 f4
  ru11 = _mm256_unpacklo_epi16(src_ef, _mm256_bsrli_epi128(src_ef, 2));
  // c2 c3 c3 c4 c4 c5 c5 c6 || d2 d3 d3 d4 d4 d5 d5 d6
  ru12 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_cd, 4),
                               _mm256_bsrli_epi128(src_cd, 6));
  // e2 e3 e3 e4 e4 e5 e5 e6 || f2 f3 f3 f4 f4 f5 f5 f6
  ru13 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_ef, 4),
                               _mm256_bsrli_epi128(src_ef, 6));
  // c4 0 c5 0 c6 0 c7 0 || d4 0 d5 0 d6 0 d7 0
  ru14 = _mm256_unpackhi_epi16(src_cd, zero);
  // e4 0 e5 0 e6 0 e7 0 || f4 0 f5 0 f6 0 f7 0
  ru15 = _mm256_unpackhi_epi16(src_ef, zero);

  // f5 f1 f5 f1 - - - -
  const __m128i filt51 =
      _mm_blend_epi16(filt_coeff, _mm_bsrli_si128(filt_coeff, 4), 0x08);
  const __m256i fc51 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt51, 0x07));
  // f6 f1 f6 f1 - - - -
  const __m128i filt61 =
      _mm_blend_epi16(filt_coeff, _mm_bsrli_si128(filt_coeff, 6), 0x08);
  const __m256i fc61 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt61, 0x07));
  // f5 0 f5 0 f5 0 f5 0 - -
  const __m256i fc5z = _mm256_blend_epi16(fc51, zero, 0xAA);

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc51, fc61, fc5z)
}

// Similar to apply_7tap_filtering_with_subtract_center_off, but allow a generic
// asymmetric filter with 13 independent taps. The only difference is in how
// the filter taps are matched up with source pixels.
static INLINE void apply_asym_13tap_filtering_with_subtract_center_off(
    const uint16_t *dgd, int stride, const __m128i filt_coeff_lo,
    const __m128i filt_coeff_hi, __m256i *accum_out_r0r1,
    __m256i *accum_out_r2r3, int block_row_begin, int block_col_begin) {
  // Load source data
  const int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 2 * stride - 2;

  // Load source data required for chroma filtering into registers.
  LOAD_SRC_DATA_FOR_CHROMA_FILTERING(src_ptr, stride)

  // Forms 256bit source registers from loaded 128bit registers and pack them
  // accordingly for filtering process.
  FORM_AND_PACK_REG_FOR_CHROMA_FILTERING()

  // Output corresponding to filter coefficient f8,f9.
  // a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
  const __m256i ru8 = _mm256_alignr_epi8(ru1, ru0, 8);
  // c2 g2 c3 g3 c4 g4 c5 g5 | d2 h2 d3 h3 d4 h4 d5 h5
  const __m256i ru9 = _mm256_alignr_epi8(ru3, ru2, 8);

  // f9 f8 f9 f8 f9 f8 f9 f8 | f9 f8 f9 f8 f9 f8 f9 f8
  const __m256i fc98 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_hi, 0x01));
  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_f98_r0r1 = _mm256_madd_epi16(ru8, fc98);
  *accum_out_r0r1 = _mm256_add_epi32(out_f98_r0r1, *accum_out_r0r1);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_f98_r2r3 = _mm256_madd_epi16(ru9, fc98);
  *accum_out_r2r3 = _mm256_add_epi32(out_f98_r2r3, *accum_out_r2r3);

  // Output corresponding to filter coefficient 5, 7, 0, 1, 6, 4
  // b1 d1 b2 d2 b3 d3 b4 d4 | c1 e1 c2 e2 c3 e3 c4 e4
  __m256i ru10 = _mm256_alignr_epi8(ru5, ru4, 4);
  // d1 f1 d2 f2 d3 f3 d4 f4 | e1 g1 e2 g2 e3 g3 e4 g4
  __m256i ru11 = _mm256_alignr_epi8(ru7, ru6, 4);
  // b2 d2 b3 d3 b4 d4 b5 d5 | c2 e2 c3 e3 c4 e4 c5 e5
  __m256i ru12 = _mm256_alignr_epi8(ru5, ru4, 8);
  // d2 f2 d3 f3 d4 f4 d5 f5 | e2 g2 e3 g3 e4 g4 e5 g5
  __m256i ru13 = _mm256_alignr_epi8(ru7, ru6, 8);
  // b3 d3 b4 d4 b5 d5 b6 d6 | c3 e3 c4 e4 c5 e5 c6 e6
  __m256i ru14 = _mm256_alignr_epi8(ru5, ru4, 12);
  // d3 f3 d4 f4 d5 f5 d6 f6 | e3 g3 e4 g4 e5 g5 e6 g6
  __m256i ru15 = _mm256_alignr_epi8(ru7, ru6, 12);

  // f5 f7 f5 f7 - - - -
  const __m256i fc57 = _mm256_broadcastd_epi32(
      _mm_shufflelo_epi16(_mm_bsrli_si128(filt_coeff_lo, 8), 0x0D));
  // f1 f0 f1 f0 - - - -
  const __m256i fc10 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_lo, 0x01));
  // f6 f4 f6 f4 - - - -
  const __m256i fc64 = _mm256_broadcastd_epi32(
      _mm_shufflelo_epi16(_mm_bsrli_si128(filt_coeff_lo, 8), 0x02));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc57, fc10, fc64)

  // Output corresponding to filter coefficient 5, 1, 6.
  const __m256i zero = _mm256_set1_epi16(0x0);
  // c0 c1 c1 c2 c2 c3 c3 c4 || d0 d1 d1 d2 d2 d3 d3 d4
  ru10 = _mm256_unpacklo_epi16(src_cd, _mm256_bsrli_epi128(src_cd, 2));
  // e0 e1 e1 e2 e2 e3 e3 e4 || f0 f1 f1 f2 f2 f3 f3 f4
  ru11 = _mm256_unpacklo_epi16(src_ef, _mm256_bsrli_epi128(src_ef, 2));
  // c2  0 c3  0 c4  0 c5  0 || d2  0 d3  0 d4  0 d5  0
  ru12 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_cd, 4), zero);
  // e2  0 e3  0 e4  0 e5  0 || f2  0 f3  0 f4  0 f5  0
  ru13 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_ef, 4), zero);
  // c3 c4 c4 c5 c5 c6 c6 c7 || d3 d4 d4 d5 d5 d6 d6 d7
  ru14 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_cd, 6),
                               _mm256_bsrli_epi128(src_cd, 8));
  // e3 e4 e4 e5 e5 e6 e6 e7 || f3 f4 f4 f5 f5 f6 f6 f7
  ru15 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_ef, 6),
                               _mm256_bsrli_epi128(src_ef, 8));

  // f11  f3 f11  f3 - - - -
  const __m256i fc113 = _mm256_broadcastd_epi32(_mm_unpacklo_epi16(
      _mm_bsrli_si128(filt_coeff_hi, 6), _mm_bsrli_si128(filt_coeff_lo, 6)));

  // f12 f12 f12 f12 - - - -
  // Every second coefficient is multiplied by 0, so is ignored
  const __m256i fc12z =
      _mm256_broadcastw_epi16(_mm_bsrli_si128(filt_coeff_hi, 8));

  //  f2 f10  f2 f10 - - - -
  const __m256i fc210 = _mm256_broadcastd_epi32(_mm_unpacklo_epi16(
      _mm_bsrli_si128(filt_coeff_lo, 4), _mm_bsrli_si128(filt_coeff_hi, 4)));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc113, fc12z, fc210)
}

// The registers accum_out_r0r1 and accum_out_r2r3 holds the filtered output.
// This function performs round, clip operations on filtered output before
// storing it to the destination.
static INLINE void round_and_store_avx2(const uint16_t *dst, int dst_stride,
                                        const NonsepFilterConfig *filter_config,
                                        int bit_depth, __m256i accum_out_r0r1,
                                        __m256i accum_out_r2r3,
                                        int block_row_begin,
                                        int block_col_begin) {
  // Rounding and clipping.
  accum_out_r0r1 =
      round_power_of_two_signed_avx2(accum_out_r0r1, filter_config->prec_bits);
  accum_out_r2r3 =
      round_power_of_two_signed_avx2(accum_out_r2r3, filter_config->prec_bits);
  // r00 r01 r02 r03 | r20 r21 r22 r23 | r10 r11 r12 r13 | r30 r31 r32 r33
  __m256i out_r0r2r1r3 = _mm256_packs_epi32(accum_out_r0r1, accum_out_r2r3);
  const __m256i max = _mm256_set1_epi16((1 << bit_depth) - 1);
  out_r0r2r1r3 = highbd_clamp_epi16(out_r0r2r1r3, _mm256_setzero_si256(), max);

  // Store the output.
  const int dst_id = block_row_begin * dst_stride + block_col_begin;
  const __m128i out_r1r3 = _mm256_extractf128_si256(out_r0r2r1r3, 1);
  _mm_storel_epi64((__m128i *)(dst + dst_id),
                   _mm256_castsi256_si128(out_r0r2r1r3));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (1 * dst_stride)), out_r1r3);

  _mm_storel_epi64((__m128i *)(dst + dst_id + (2 * dst_stride)),
                   _mm_bsrli_si128(_mm256_castsi256_si128(out_r0r2r1r3), 8));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (3 * dst_stride)),
                   _mm_bsrli_si128(out_r1r3, 8));
}

// Implementation of DIAMOND shaped 7-tap (6 symmetric + 1) filtering for block
// size of 4x4. The output for a particular pixel in a 4x4 block is calculated
// by considering a 5x5 grid surrounded by that pixel. The filter taps are
// expected to be passed as symmetric taps followed by center tap. The exact
// order of filter taps needed is shown below.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 fc
// Here, 'fc0-fc5' corresponds to symmetric taps and 'fc' is the center tap.
static AOM_INLINE void av1_convolve_symmetric_highbd_7tap_avx2(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  // Derive singleton_tap.
  // TODO(rachelbarker): Set up the singleton tap fully in
  // adjust_filter_and_config, so that we don't have to modify it here
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  if (filter_config->num_pixels % 2) {
    const int singleton_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[singleton_tap_index];
  }

  // Load filter tap values.
  // fc0 fc1 fc2 fc3 fc4 fc5 center_tap x
  const __m128i filt_coeff_0 = _mm_loadu_si128((__m128i const *)(filter));
  // Replace the center_tap with derived singleton_tap.
  const __m128i center_tap = _mm_set1_epi16(singleton_tap);
  const __m128i filt_coeff = _mm_blend_epi16(filt_coeff_0, center_tap, 0x40);

  // Initializing the output registers with zero
  __m256i accum_out_r0r1 = _mm256_setzero_si256();
  __m256i accum_out_r2r3 = _mm256_setzero_si256();

  // Perform 7-tap filtering on source buffer
  apply_7tap_filtering_with_subtract_center_off(
      dgd, stride, filt_coeff, &accum_out_r0r1, &accum_out_r2r3,
      block_row_begin, block_col_begin);

  // Store the output after rounding and clipping
  round_and_store_avx2(dst, dst_stride, filter_config, bit_depth,
                       accum_out_r0r1, accum_out_r2r3, block_row_begin,
                       block_col_begin);
}
/* Not used. Can be deprecated - END */

// AVX2 intrinsic to convolve a block of pixels with origin-symmetric,
// non-separable filters. The output for a particular pixel in a 4x4 block is
// calculated with DIAMOND shaped filter considering a 7x7 grid surrounded by
// that pixel. DIAMOND shape uses 13-tap filter for convolution. The filter taps
// are expected to be passed as symmetric taps followed by center tap. The exact
// order of filter taps needed is shown below.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 fc6 fc7 fc8 fc9 fc10 fc11 fc
// Here, 'fc0-fc11' corresponds to symmetric taps and 'fc' is the center tap.
// The following describes the design considered for intrinsic implementation.
// Load Source Data:
// src_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// src_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// src_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// src_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// src_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// src_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// src_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at d3 position is calculated as below.
// Filtered_d3 = (a3+g3)*fc10 + (b2+f4)*fc6 + (b3+f3)*fc2 + (b4+f5)*fc7 +
//        (c1+e5)*fc8 + (c2+e4)*fc4 + (c3+e3)*fc0 + (c4+e2)*fc5 + (c5+e1)*fc9 +
//        (d0+d6)*fc11 + (d1+d5)*fc3 + (d2+d4)*fc1 + d3*fc.
// The source registers are unpacked such that the output corresponding
// to 2 rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc0 of rows 0 and 1 is achieved like below.
// __m256i src_rag3 = a3 g3 a4 g4 a5 g5 a6 g6 | b3 h3 b4 h4 b5 h5 b6 h6
// __m256i filter_0 = f0 f0 f0 f0 f0 f0 f0 f0 | f0 f0 f0 f0 f0 f0 f0 f0
//  __m256i out_f0_0 = _mm256_madd_epi16(src_rag3, filter_0);
//                   = (a3*f0+g3*f0) (a4*f0+g4*f0) .. | (b3*f0+h3*f0) . .
// Here, out_f0_0 contains partial output of rows 1 and 2 corresponding to fc0.
static AOM_INLINE void av1_convolve_symmetric_highbd_13tap_avx2(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter_, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  /*
  Note the original SIMD code here was designed for the configuration below,
  but is being changed to the one in config_13tap_avx2 above to align
  pc_wiener and wienerns configurations. However a permute of the coefficients
  is now necessary before the rest of the code can be used.
  { -3, 0, 0 },  { 3, 0, 0 },  { -2, -1, 1 }, { 2, 1, 1 },   { -2, 0, 2 },
  { 2, 0, 2 },   { -2, 1, 3 }, { 2, -1, 3 },  { -1, -2, 4 }, { 1, 2, 4 },
  { -1, -1, 5 }, { 1, 1, 5 },  { -1, 0, 6 },  { 1, 0, 6 },   { -1, 1, 7 },
  { 1, -1, 7 },  { -1, 2, 8 }, { 1, -2, 8 },  { 0, -3, 9 },  { 0, 3, 9 },
  { 0, -2, 10 }, { 0, 2, 10 }, { 0, -1, 11 }, { 0, 1, 11 },  { 0, 0, 12 },
  */
  // Begin permute
  int16_t filter[WIENERNS_TAPS_MAX];
  filter[6] = filter_[0];
  filter[11] = filter_[1];
  filter[2] = filter_[2];
  filter[10] = filter_[3];
  filter[5] = filter_[4];
  filter[7] = filter_[5];
  filter[1] = filter_[6];
  filter[3] = filter_[7];
  filter[4] = filter_[8];
  filter[8] = filter_[9];
  filter[0] = filter_[10];
  filter[9] = filter_[11];
  filter[12] = filter_[12];
  // End permute

  // Derive singleton_tap.
  // TODO(rachelbarker): Set up the singleton tap fully in
  // adjust_filter_and_config, so that we don't have to modify it here
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  if (filter_config->num_pixels % 2) {
    const int singleton_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[singleton_tap_index];
  }

  // Load source data.
  int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 3 * stride - 3;
  const __m128i src_a0 = _mm_loadu_si128((__m128i const *)src_ptr);
  const __m128i src_b0 = _mm_loadu_si128((__m128i const *)(src_ptr + stride));
  const __m128i src_c0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 2 * stride));
  const __m128i src_d0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 3 * stride));
  const __m128i src_e0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 4 * stride));
  const __m128i src_f0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 5 * stride));
  const __m128i src_g0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 6 * stride));
  const __m128i src_h0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 7 * stride));
  const __m128i src_i0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 8 * stride));
  const __m128i src_j0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 9 * stride));

  // Load filter tap values.
  // fc0 fc1 fc2 fc3 fc4 fc5 fc6 fc7
  const __m128i filt_coeff_0 = _mm_loadu_si128((__m128i const *)(filter));
  // fc5 fc6 fc7 fc8 fc9 fc10 fc11 fc12
  __m128i temp = _mm_loadu_si128((__m128i const *)(filter + 5));
  // Replace the fc12 with derived singleton_tap.
  const __m128i center_tap = _mm_set1_epi16(singleton_tap);
  const __m128i filt_coeff_1 = _mm_blend_epi16(temp, center_tap, 0x80);

  // Form 256bit source registers.
  // a0 a1 a2 a3 a4 a5 a6 a7 | b0 b1 b2 b3 b4 b5 b6 b7
  const __m256i src_ab =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_a0), src_b0, 1);
  // c0 c1 c2 c3 c4 c5 c6 c7 | d0 d1 d2 d3 d4 d5 d6 d7
  const __m256i src_cd =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_c0), src_d0, 1);
  // e0 e1 e2 e3 e4 e5 e6 e7 | f0 f1 f2 f3 f4 f5 f6 f7
  const __m256i src_ef =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_e0), src_f0, 1);
  // g0 g1 g2 g3 g4 g5 g6 g7 | h0 h1 h2 h3 h4 h5 h6 h7
  const __m256i src_gh =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_g0), src_h0, 1);
  // i0 i1 i2 i3 i4 i5 i6 i7 | j0 j1 j2 j3 j4 j5 j6 j7
  const __m256i src_ij =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_i0), src_j0, 1);

  // Packing the source rows.
  // a0 g0 a1 g1 a2 g2 a3 g3 | b0 h0 b1 h1 b2 h2 b3 h3
  const __m256i ru0 = _mm256_unpacklo_epi16(src_ab, src_gh);
  // a4 g4 a5 g5 a6 g6 a7 g7 | b4 h4 b5 h5 b6 h6 b7 h7
  const __m256i ru1 = _mm256_unpackhi_epi16(src_ab, src_gh);
  // c0 e0 c1 e1 c2 e2 c3 e3 | d0 f0 d1 f1 d2 f2 d3 f3
  const __m256i ru2 = _mm256_unpacklo_epi16(src_cd, src_ef);
  // c4 e4 c5 e5 c6 e6 c7 e7 | d4 f4 d5 f5 d6 f6 d7 f7
  const __m256i ru3 = _mm256_unpackhi_epi16(src_cd, src_ef);
  // c0 i0 c1 i1 c2 i2 c3 i3 | d0 j0 d1 j1 d2 j2 d3 j3
  const __m256i ru4 = _mm256_unpacklo_epi16(src_cd, src_ij);
  // c4 i4 c5 i5 c6 i6 c7 i7 | d4 j4 d5 j5 d6 j6 d7 j7
  const __m256i ru5 = _mm256_unpackhi_epi16(src_cd, src_ij);
  // e0 g0 e1 g1 e2 g2 e3 g3 | f0 h0 f1 h1 f2 h2 f3 h3
  const __m256i ru6 = _mm256_unpacklo_epi16(src_ef, src_gh);
  // e4 g4 e5 g5 e6 g6 e7 g7 | f4 h4 f5 h5 f6 h6 f7 h7
  const __m256i ru7 = _mm256_unpackhi_epi16(src_ef, src_gh);

  // Output corresponding to filter coefficient 0.
  // a3 g3 a4 g4 a5 g5 a6 g6 | b3 h3 b4 h4 b5 h5 b6 h6
  const __m256i ru8 = _mm256_alignr_epi8(ru1, ru0, 12);
  // c3 i3 c4 i4 c5 i5 c6 i6 | d3 j3 d4 j4 d5 j5 d6 j6
  const __m256i ru9 = _mm256_alignr_epi8(ru5, ru4, 12);
  // f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0 f0
  const __m256i fc0 = _mm256_broadcastw_epi16(filt_coeff_0);

  // r00 r01 r02 r03 | r10 r11 r12 r13
  __m256i accum_out_r0r1 = _mm256_madd_epi16(ru8, fc0);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  __m256i accum_out_r2r3 = _mm256_madd_epi16(ru9, fc0);

  // Output corresponding to filter coefficients 4,5,6,7.
  // c1 e1 c2 e2 c3 e3 c4 e4 | d1 f1 d2 f2 d3 f3 d4 f4
  const __m256i ru10 = _mm256_alignr_epi8(ru3, ru2, 4);
  // e1 g1 e2 g2 e3 g3 e4 g4 | f1 h1 f2 h2 f3 h3 f4 h4
  const __m256i ru11 = _mm256_alignr_epi8(ru7, ru6, 4);
  // c2 e2 c3 e3 c4 e4 c5 e5 | d2 f2 d3 f3 d4 f4 d5 f5
  const __m256i ru12 = _mm256_alignr_epi8(ru3, ru2, 8);
  // e2 g2 e3 g3 e4 g4 e5 g5 | f2 h2 f3 h3 f4 h4 f5 h5
  const __m256i ru13 = _mm256_alignr_epi8(ru7, ru6, 8);
  // c3 e3 c4 e4 c5 e5 c6 e6 | d3 f3 d4 f4 d5 f5 d6 f6
  const __m256i ru14 = _mm256_alignr_epi8(ru3, ru2, 12);
  // e3 g3 e4 g4 e5 g5 e6 g6 | f3 h3 f4 h4 f5 h5 f6 h6
  const __m256i ru15 = _mm256_alignr_epi8(ru7, ru6, 12);

  // 0 fc5 fc6 fc7 fc8 fc9 fc10 fc11
  temp = _mm_bslli_si128(filt_coeff_1, 2);
  // fc4 fc8 fc5 fc9 fc6 fc10 fc7 fc11
  temp = _mm_unpackhi_epi16(filt_coeff_0, temp);
  // fc4 fc8 fc4 fc8 fc4 fc8 fc4 fc8 | fc4 fc8 fc4 fc8 fc4 fc8 fc4 fc8
  const __m256i filt48 = _mm256_broadcastd_epi32(temp);
  // fc5 fc7 fc5 fc7 fc5 fc7 fc5 fc7 | fc5 fc7 fc5 fc7 fc5 fc7 fc5 fc7
  const __m256i filt57 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_1, 0x08));
  // fc6 fc6 fc6 fc6 fc6 fc6 fc6 fc6 | fc6 fc6 fc6 fc6 fc6 fc6 fc6 fc6
  const __m256i filt66 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_1, 0x55));
  // fc7 fc5 fc7 fc5 fc7 fc5 fc7 fc5 | fc7 fc5 fc7 fc5 fc7 fc5 fc7 fc5
  const __m256i filt75 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_1, 0x22));

  const __m256i res_0 = _mm256_madd_epi16(ru10, filt48);
  const __m256i res_1 = _mm256_madd_epi16(ru11, filt48);
  const __m256i res_2 = _mm256_madd_epi16(ru12, filt57);
  const __m256i res_3 = _mm256_madd_epi16(ru13, filt57);
  const __m256i res_4 = _mm256_madd_epi16(ru14, filt66);
  const __m256i res_5 = _mm256_madd_epi16(ru15, filt66);
  const __m256i res_6 = _mm256_madd_epi16(ru3, filt75);
  const __m256i res_7 = _mm256_madd_epi16(ru7, filt75);

  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_0 = _mm256_add_epi32(res_0, res_2);
  const __m256i out_1 = _mm256_add_epi32(res_4, res_6);
  const __m256i out_2 = _mm256_add_epi32(out_0, out_1);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, out_2);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_3 = _mm256_add_epi32(res_1, res_3);
  const __m256i out_4 = _mm256_add_epi32(res_5, res_7);
  const __m256i out_5 = _mm256_add_epi32(out_3, out_4);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, out_5);

  // Output corresponding to filter coefficients 9,10,11,12.
  // d2 d3 d4	d5 d6 d7 d8 d9
  const __m128i src_d2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 3 * stride + 2));
  // e2 e3 e4 e5 e6 e7 e8 e9
  const __m128i src_e2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 4 * stride + 2));
  // f2 f3 f4 f5 f6 f7 f8 f9
  const __m128i src_f2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 5 * stride + 2));
  // g2 g3 g4	g5 g6 g7 g8 g9
  const __m128i src_g2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 6 * stride + 2));

  // d0 d1 d2 d3 d4 d5 d6 d7 | e0 e1 e2 e3 e4 e5 e6 e7
  const __m256i rm0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d0), src_e0, 1);
  // d2 d3 d4 d5 d6 d7 d8 d9 | e2 e3 e4 e5 e6 e7 e8 e9
  const __m256i rm2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d2), src_e2, 1);
  // f0 f1 f2 f3 f4 f5 f6 f7 | g0 g1 g2 g3 g4 g5 g6 g7
  const __m256i rm00 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f0), src_g0, 1);
  // f2 f3 f4 f5 f6 f7 f8 f9 | g2 g3 g4 g5 g6 g7 g8 g9
  const __m256i rm22 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f2), src_g2, 1);
  // d1 d2 d3 d4 d5 d6 d7 d8 | e1 e2 e3 e4 e5 e6 e7 e8
  const __m256i rm1 = _mm256_alignr_epi8(_mm256_bsrli_epi128(rm2, 12), rm0, 2);
  const __m256i rm11 =
      _mm256_alignr_epi8(_mm256_bsrli_epi128(rm22, 12), rm00, 2);
  // d3 d4 d5 d6 d7 d8 d9 0 | e3 e4 e5 e6 e7 e8 e9 0
  const __m256i rm3 = _mm256_bsrli_epi128(rm2, 2);
  const __m256i rm33 = _mm256_bsrli_epi128(rm22, 2);
  // d0 d1 d1 d2 d2 d3 d3 d4 | e0 e1 e1 e2 e2 e3 e3 e4
  __m256i rm4 = _mm256_unpacklo_epi16(rm0, rm1);
  // d2 d3 d3 d4 d4 d5 d5 d6 | e2 e3 e3 e4 e4 e5 e5 e6
  __m256i rm5 = _mm256_unpacklo_epi16(rm2, rm3);
  // d4 d5 d5 d6 d6 d7 d7 d8 | e4 e5 e5 e6 e6 e7 e7 e8
  __m256i rm6 = _mm256_unpackhi_epi16(rm0, rm1);
  // d6 0 d7 0 d8 0 d9 0 | e6 0 e7 0 e8 0 e9 0
  __m256i rm7 = _mm256_unpackhi_epi16(rm2, _mm256_set1_epi16(0));

  __m256i rm44 = _mm256_unpacklo_epi16(rm00, rm11);
  __m256i rm55 = _mm256_unpacklo_epi16(rm22, rm33);
  __m256i rm66 = _mm256_unpackhi_epi16(rm00, rm11);
  __m256i rm77 = _mm256_unpackhi_epi16(rm22, _mm256_set1_epi16(0));
  // fc9 fc10 - - - - - - | fc9 fc10 - - - - - -
  const __m256i fc910 =
      _mm256_broadcastd_epi32(_mm_bsrli_si128(filt_coeff_1, 8));
  // fc11 fc12 - - - - - - | fc11 fc12 - - - - - -
  const __m256i fc1112 =
      _mm256_broadcastd_epi32(_mm_bsrli_si128(filt_coeff_1, 12));
  // fc11 fc10  - - - - - - | fc11 fc10 - - - - - -
  const __m256i fc1110 = _mm256_broadcastd_epi32(
      _mm_bsrli_si128(_mm_shufflehi_epi16(filt_coeff_1, 0x06), 8));

  rm4 = _mm256_madd_epi16(rm4, fc910);
  rm5 = _mm256_madd_epi16(rm5, fc1112);
  rm6 = _mm256_madd_epi16(rm6, fc1110);
  rm7 = _mm256_madd_epi16(rm7, fc910);
  rm44 = _mm256_madd_epi16(rm44, fc910);
  rm55 = _mm256_madd_epi16(rm55, fc1112);
  rm66 = _mm256_madd_epi16(rm66, fc1110);
  rm77 = _mm256_madd_epi16(rm77, fc910);

  // r00 r01 r02 r03 | r10 r11 r12 r13
  rm4 = _mm256_add_epi32(rm4, rm5);
  rm6 = _mm256_add_epi32(rm6, rm7);
  rm4 = _mm256_add_epi32(rm4, rm6);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, rm4);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  rm44 = _mm256_add_epi32(rm44, rm55);
  rm66 = _mm256_add_epi32(rm66, rm77);
  rm44 = _mm256_add_epi32(rm44, rm66);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, rm44);

  // Output corresponding to filter coefficients 1,2,3,8.
  const __m128i src_b2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 1 * stride + 2));
  const __m128i src_c2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 2 * stride + 2));
  const __m256i rn0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_b2), src_c2, 1);
  // b2 b3 b3 b4 b4 b5 b5 b6 | c2 c3 c3 c4 c4 c5 c5 c6
  __m256i r0 = _mm256_unpacklo_epi16(rn0, _mm256_bsrli_epi128(rn0, 2));

  const __m256i rcd2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_c2), src_d2, 1);
  // b4 c5 b5 c6 b6 c7 b7 c8 | c4 d5 c5 d6 c6 d7 c7 d8
  __m256i r1 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rn0, 4),
                                     _mm256_bsrli_epi128(rcd2, 6));

  const __m256i rfg2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f2), src_g2, 1);
  // f2 f3 f3 f4 f4 f5 f5 f6 | g2 g3 g3 g4 g4 g5 g5 g6
  __m256i r2 = _mm256_unpacklo_epi16(rfg2, _mm256_bsrli_epi128(rfg2, 2));

  const __m256i ref2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_e2), src_f2, 1);
  // f4 e5 f5 e6 f6 e7 f7 e8 | g4 f5 g5 f6 g6 f7 g7 f8
  __m256i r3 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rfg2, 4),
                                     _mm256_bsrli_epi128(ref2, 6));

  const __m128i tempn =
      _mm_blend_epi16(_mm_bsrli_si128(filt_coeff_0, 2), filt_coeff_1, 0x08);
  __m128i tempn2 = _mm_bsrli_si128(filt_coeff_0, 2);
  tempn2 = _mm_shufflelo_epi16(tempn2, 0x0c);
  // fc1 fc2 - - - - - - | fc1 fc2 - - - - - -
  const __m256i fc12 = _mm256_broadcastd_epi32(tempn);
  // fc1 fc4 - - - - - - | fc1 fc4 - - - - - -
  const __m256i fc14 = _mm256_broadcastd_epi32(tempn2);
  tempn2 = _mm_shufflelo_epi16(tempn, 0x06);
  // fc3 fc8 - - - - - - | fc3 fc8 - - - - - -
  const __m256i fc38 = _mm256_broadcastd_epi32(_mm_bsrli_si128(tempn, 4));
  // fc3 fc2 - - - - - - | fc3 fc2 - - - - - -
  const __m256i fc32 = _mm256_broadcastd_epi32(tempn2);

  r0 = _mm256_madd_epi16(r0, fc12);
  r1 = _mm256_madd_epi16(r1, fc38);
  r2 = _mm256_madd_epi16(r2, fc32);
  r3 = _mm256_madd_epi16(r3, fc14);

  // r00 r01 r02 r03 | r10 r11 r12 r13
  r0 = _mm256_add_epi32(r0, r1);
  r2 = _mm256_add_epi32(r2, r3);
  r0 = _mm256_add_epi32(r0, r2);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, r0);

  const __m256i rn1 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d2), src_e2, 1);
  // d2 d3 d3 d4 d4 d5 d5 d6 | e2 e3 e3 e4 e4 e5 e5 e6
  __m256i r00 = _mm256_unpacklo_epi16(rn1, _mm256_bsrli_epi128(rn1, 2));
  // d4 e5 d5 e6 d6 e7 d7 e8 | e4 f5 e5 f6 e6 f7 e7 f8
  __m256i r11 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rn1, 4),
                                      _mm256_bsrli_epi128(ref2, 6));

  const __m128i src_h2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 7 * stride + 2));
  const __m128i src_i2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 8 * stride + 2));
  // h2 h3 h4 h5 h6 h7 h8 h9 | i2 i3 i4 i5 i6 i7 i8 i9
  __m256i rhi2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_h2), src_i2, 1);
  // h2 h3 h3 h4 h4 h5 h5 h6 | i2 i3 i3 i4 i4 i5 i5 i6
  __m256i r22 = _mm256_unpacklo_epi16(rhi2, _mm256_bsrli_epi128(rhi2, 2));
  // g2 g3 g4 g5 g6 g7 g8 g9 | h2 h3 h4 h5 h6 h7 h8 h9
  __m256i rgh2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_g2), src_h2, 1);
  __m256i r33 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rhi2, 4),
                                      _mm256_bsrli_epi128(rgh2, 6));
  r00 = _mm256_madd_epi16(r00, fc12);
  r11 = _mm256_madd_epi16(r11, fc38);
  r22 = _mm256_madd_epi16(r22, fc32);
  r33 = _mm256_madd_epi16(r33, fc14);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  r00 = _mm256_add_epi32(r00, r11);
  r22 = _mm256_add_epi32(r22, r33);
  r00 = _mm256_add_epi32(r00, r22);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, r00);

  // Rounding and clipping.
  accum_out_r0r1 =
      round_power_of_two_signed_avx2(accum_out_r0r1, filter_config->prec_bits);
  accum_out_r2r3 =
      round_power_of_two_signed_avx2(accum_out_r2r3, filter_config->prec_bits);
  // r00 r01 r02 r03 | r20 r21 r22 r23 | r10 r11 r12 r13 | r30 r31 r32 r33
  __m256i out_r0r2r1r3 = _mm256_packs_epi32(accum_out_r0r1, accum_out_r2r3);
  const __m256i max = _mm256_set1_epi16((1 << bit_depth) - 1);
  out_r0r2r1r3 = highbd_clamp_epi16(out_r0r2r1r3, _mm256_setzero_si256(), max);

  // Store the output.
  const int dst_id = block_row_begin * dst_stride + block_col_begin;
  const __m128i out_r1r3 = _mm256_extractf128_si256(out_r0r2r1r3, 1);
  _mm_storel_epi64((__m128i *)(dst + dst_id),
                   _mm256_castsi256_si128(out_r0r2r1r3));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (1 * dst_stride)), out_r1r3);

  _mm_storel_epi64((__m128i *)(dst + dst_id + (2 * dst_stride)),
                   _mm_bsrli_si128(_mm256_castsi256_si128(out_r0r2r1r3), 8));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (3 * dst_stride)),
                   _mm_bsrli_si128(out_r1r3, 8));
}

// SIMD implementation to convolve a block of pixels with origin-symmetric, pc
// wiener filter corresponds to CONFIG_PC_WIENER loop restoration. DIAMOND shape
// with 13-tap (12 symmetric+1) or 7-tap (6 symmetric + 1) filter is used for
// convolution.
void av1_convolve_symmetric_highbd_avx2(const uint16_t *dgd, int stride,
                                        const NonsepFilterConfig *filter_config,
                                        const int16_t *filter, uint16_t *dst,
                                        int dst_stride, int bit_depth,
                                        int block_row_begin, int block_row_end,
                                        int block_col_begin,
                                        int block_col_end) {
  assert(!filter_config->subtract_center);

  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;

  const int num_sym_taps = filter_config->num_pixels / 2;

  // Dispatch to the appropriate SIMD function for the selected filter shape
  // and block size. If none are applicable, fall back to C
  // TODO(rachelbarker): Add plumbing logic to adjust_filter_and_config so that
  // the 6-tap branch here can actually be used for reduced-length filters
  if (num_rows == 4 && num_cols == 4 &&
      (filter_config->config == wienerns_simd_config_y ||
       !memcmp(wienerns_simd_config_y, filter_config->config,
               filter_config->num_pixels * 3 *
                   sizeof(filter_config->config[0][0])))) {
    av1_convolve_symmetric_highbd_13tap_avx2(dgd, stride, filter_config, filter,
                                             dst, dst_stride, bit_depth,
                                             block_row_begin, block_col_begin);
  } else if (num_rows == 4 && num_cols == 4 && num_sym_taps == 6 &&
             (filter_config->config == wienerns_simd_config_uv_from_uvonly ||
              !memcmp(wienerns_simd_config_uv_from_uvonly,
                      filter_config->config,
                      filter_config->num_pixels * 3 *
                          sizeof(filter_config->config[0][0])))) {
    av1_convolve_symmetric_highbd_7tap_avx2(dgd, stride, filter_config, filter,
                                            dst, dst_stride, bit_depth,
                                            block_row_begin, block_col_begin);
  } else {
    av1_convolve_symmetric_highbd_c(
        dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
        block_row_begin, block_row_end, block_col_begin, block_col_end);
  }
}

// TODO(Arun Negi): Difference of source and center pixel needs to go through
// clip_base(). Implement clip_base() in intrinsic once the support is added.
//
// Implementation of DIAMOND shaped 6-tap filtering for block size of 4x4.
// The output for a particular pixel in a 4x4 block is calculated by considering
// a 5x5 grid surrounded by that pixel. The registers accum_out_r0r1 and
// accum_out_r2r3 are used to store the output. The following describes the
// algorithm briefly.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 x x
// Load Source Data:
// src_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// src_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// src_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// src_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// src_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// src_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// src_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at c2 position is calculated as below.
// Filtered_c2 = ((a2-c2)+(e2-c2))*fc4 + ((b1-c2+d3-c2))*fc2 +
// (b2-c2+d2-c2)*fc0 + (b3-c2+d1-c2)*fc3 + (c0-c2+c4-c2)*fc5 +
// (c1-c2+c3-c2)*fc1 + c2*singleton_tap + dc_offset
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc4 of rows 0 and 1 is achieved like below.
// __m256i centerpixel_row01 = c2 c2 c3 c3 c4 c4 c5 c5 | d2 d2 d3 d3 d4 d4 d5 d5
// __m256i src_reg3 = a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
// __m256i filter_4 = fc4 fc4 fc4 fc4 fc4 fc4 fc4 fc4 | fc4 fc4 fc4 fc4 fc4 fc4
// fc4 fc4
// __m256 src_reg3 = _mm256_sub_epi16(src_reg3, centerpixel_row01);
//  __m256i out_f4_01 = _mm256_madd_epi16(src_reg3, filter_4);
//                   = ((a2-c2)*fc4+(e2-c2)*fc4) (a3-c3)*fc4+(e3-c3)*fc4) .. |
//                   (b2-d2)*fc4 +(f2-d2)*fc4) . .
// Here, out_f4_01 contains partial output of rows 0 and 1 corresponding to fc4.
static INLINE void apply_6tap_filtering(const uint16_t *dgd, int stride,
                                        const __m128i filt_coeff,
                                        __m256i *accum_out_r0r1,
                                        __m256i *accum_out_r2r3,
                                        int block_row_begin,
                                        int block_col_begin) {
  // Load source data
  const int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 2 * stride - 2;

  // Load source data required for chroma filtering into registers.
  LOAD_SRC_DATA_FOR_CHROMA_FILTERING(src_ptr, stride)

  // Forms 256bit source registers from loaded 128bit registers and pack them
  // accordingly for filtering process.
  FORM_AND_PACK_REG_FOR_CHROMA_FILTERING()

  // Derive registers to hold center pixel
  // c2 c2 c3 c3 c4 c4 c5 c5 | d2 d2 d3 d3 d4 d4 d5 d5
  const __m256i cp0 = _mm256_bslli_epi128(src_cd, 4);
  const __m256i center_pixel_row01_0 = _mm256_unpackhi_epi16(cp0, cp0);
  // e2 e2 e3 e3 e4 e4 e5 e5 | f2 f2 f3 f3 f4 f4 f5 f5
  const __m256i cp1 = _mm256_bslli_epi128(src_ef, 4);
  const __m256i center_pixel_row23_0 = _mm256_unpackhi_epi16(cp1, cp1);
  const __m256i zero = _mm256_set1_epi16(0x0);
  // 0 c2 0 c3 0 c4 0 c5 | 0 d2 0 d3 0 d4 0 d5
  const __m256i center_pixel_row01_1 = _mm256_unpackhi_epi16(zero, cp0);
  // 0 e2 0 e3 0 e4 0 e5 | 0 f2 0 f3 0 f4 0 f5
  const __m256i center_pixel_row23_1 = _mm256_unpackhi_epi16(zero, cp1);

  // Output corresponding to filter coefficient f4.
  // a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
  const __m256i ru8_0 = _mm256_alignr_epi8(ru1, ru0, 8);
  const __m256i ru8 = _mm256_sub_epi16(ru8_0, center_pixel_row01_0);
  // c2 g2 c3 g3 c4 g4 c5 g5 | d2 h2 d3 h3 d4 h4 d5 h5
  const __m256i ru9_0 = _mm256_alignr_epi8(ru3, ru2, 8);
  const __m256i ru9 = _mm256_sub_epi16(ru9_0, center_pixel_row23_0);

  // f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4 f4
  const __m256i fc4 =
      _mm256_broadcastd_epi32(_mm_unpackhi_epi16(filt_coeff, filt_coeff));
  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_f4_r0r1 = _mm256_madd_epi16(ru8, fc4);
  *accum_out_r0r1 = _mm256_add_epi32(out_f4_r0r1, *accum_out_r0r1);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_f4_r2r3 = _mm256_madd_epi16(ru9, fc4);
  *accum_out_r2r3 = _mm256_add_epi32(out_f4_r2r3, *accum_out_r2r3);

  // Output corresponding to filter coefficient 2, 0, 3.
  // b1 d1 b2 d2 b3 d3 b4 d4 | c1 e1 c2 e2 c3 e3 c4 e4
  const __m256i ru10_0 = _mm256_alignr_epi8(ru5, ru4, 4);
  __m256i ru10 = _mm256_sub_epi16(ru10_0, center_pixel_row01_0);
  // d1 f1 d2 f2 d3 f3 d4 f4 | e1 g1 e2 g2 e3 g3 e4 g4
  const __m256i ru11_0 = _mm256_alignr_epi8(ru7, ru6, 4);
  __m256i ru11 = _mm256_sub_epi16(ru11_0, center_pixel_row23_0);

  // b2 d2 b3 d3 b4 d4 b5 d5 | c2 e2 c3 e3 c4 e4 c5 e5
  const __m256i ru12_0 = _mm256_alignr_epi8(ru5, ru4, 8);
  __m256i ru12 = _mm256_sub_epi16(ru12_0, center_pixel_row01_0);
  // d2 f2 d3 f3 d4 f4 d5 f5 | e2 g2 e3 g3 e4 g4 e5 g5
  const __m256i ru13_0 = _mm256_alignr_epi8(ru7, ru6, 8);
  __m256i ru13 = _mm256_sub_epi16(ru13_0, center_pixel_row23_0);

  // b3 d3 b4 d4 b5 d5 b6 d6 | c3 e3 c4 e4 c5 e5 c6 e6
  const __m256i ru14_0 = _mm256_alignr_epi8(ru5, ru4, 12);
  __m256i ru14 = _mm256_sub_epi16(ru14_0, center_pixel_row01_0);
  // d3 f3 d4 f4 d5 f5 d6 f6 | e3 g3 e4 g4 e5 g5 e6 g6
  const __m256i ru15_0 = _mm256_alignr_epi8(ru7, ru6, 12);
  __m256i ru15 = _mm256_sub_epi16(ru15_0, center_pixel_row23_0);

  // f2 f3 f2 f3 - - - -
  const __m256i fc23 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff, 0x0E));
  // f0 f0 f0 f0 - - - -
  const __m256i fc00 = _mm256_broadcastw_epi16(filt_coeff);
  // f3 f2 f3 f2 - - - -
  const __m256i fc32 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff, 0x0B));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc23, fc00, fc32)

  // Output corresponding to filter coefficient 5, 1, 6.
  // c0 c1 c1 c2 c2 c3 c3 c4 || d0 d1 d1 d2 d2 d3 d3 d4
  ru10 = _mm256_unpacklo_epi16(src_cd, _mm256_bsrli_epi128(src_cd, 2));
  ru10 = _mm256_sub_epi16(ru10, center_pixel_row01_0);
  // e0 e1 e1 e2 e2 e3 e3 e4 || f0 f1 f1 f2 f2 f3 f3 f4
  ru11 = _mm256_unpacklo_epi16(src_ef, _mm256_bsrli_epi128(src_ef, 2));
  ru11 = _mm256_sub_epi16(ru11, center_pixel_row23_0);
  // c2 c3 c3 c4 c4 c5 c5 c6 || d2 d3 d3 d4 d4 d5 d5 d6
  ru12 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_cd, 4),
                               _mm256_bsrli_epi128(src_cd, 6));
  ru12 = _mm256_sub_epi16(ru12, center_pixel_row01_1);
  // e2 e3 e3 e4 e4 e5 e5 e6 || f2 f3 f3 f4 f4 f5 f5 f6
  ru13 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_ef, 4),
                               _mm256_bsrli_epi128(src_ef, 6));
  ru13 = _mm256_sub_epi16(ru13, center_pixel_row23_1);
  // c4 0 c5 0 c6 0 c7 0 || d4 0 d5 0 d6 0 d7 0
  ru14 = _mm256_unpackhi_epi16(src_cd, zero);
  ru14 = _mm256_sub_epi16(ru14, center_pixel_row01_0);
  // e4 0 e5 0 e6 0 e7 0 || f4 0 f5 0 f6 0 f7 0
  ru15 = _mm256_unpackhi_epi16(src_ef, zero);
  ru15 = _mm256_sub_epi16(ru15, center_pixel_row23_0);

  // f5 f1 f5 f1 - - - -
  const __m128i filt51 =
      _mm_blend_epi16(filt_coeff, _mm_bsrli_si128(filt_coeff, 4), 0x08);
  const __m256i fc51 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt51, 0x07));
  // f6 f1 f6 f1 - - - -
  const __m128i filt61 =
      _mm_blend_epi16(filt_coeff, _mm_bsrli_si128(filt_coeff, 6), 0x08);
  const __m256i fc61 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt61, 0x07));
  // f5 0 f5 0 f5 0 f5 0 - -
  const __m256i fc5z = _mm256_blend_epi16(fc51, zero, 0xAA);

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc51, fc61, fc5z)
}

static INLINE void apply_asym_12tap_filtering(
    const uint16_t *dgd, int stride, const __m128i filt_coeff_lo,
    const __m128i filt_coeff_hi, __m256i *accum_out_r0r1,
    __m256i *accum_out_r2r3, int block_row_begin, int block_col_begin) {
  // Load source data
  const int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 2 * stride - 2;

  // Load source data required for chroma filtering into registers.
  LOAD_SRC_DATA_FOR_CHROMA_FILTERING(src_ptr, stride)

  // Forms 256bit source registers from loaded 128bit registers and pack them
  // accordingly for filtering process.
  FORM_AND_PACK_REG_FOR_CHROMA_FILTERING()

  // Derive registers to hold center pixel
  // c2 c2 c3 c3 c4 c4 c5 c5 | d2 d2 d3 d3 d4 d4 d5 d5
  const __m256i cp0 = _mm256_bslli_epi128(src_cd, 4);
  const __m256i center_pixel_row01_0 = _mm256_unpackhi_epi16(cp0, cp0);
  // e2 e2 e3 e3 e4 e4 e5 e5 | f2 f2 f3 f3 f4 f4 f5 f5
  const __m256i cp1 = _mm256_bslli_epi128(src_ef, 4);
  const __m256i center_pixel_row23_0 = _mm256_unpackhi_epi16(cp1, cp1);

  // Output corresponding to filter coefficient 8 and 9.
  // a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
  const __m256i ru8_0 = _mm256_alignr_epi8(ru1, ru0, 8);
  const __m256i ru8 = _mm256_sub_epi16(ru8_0, center_pixel_row01_0);
  // c2 g2 c3 g3 c4 g4 c5 g5 | d2 h2 d3 h3 d4 h4 d5 h5
  const __m256i ru9_0 = _mm256_alignr_epi8(ru3, ru2, 8);
  const __m256i ru9 = _mm256_sub_epi16(ru9_0, center_pixel_row23_0);

  // f9 f8 f9 f8 f9 f8 f9 f8 | f9 f8 f9 f8 f9 f8 f9 f8
  const __m256i fc98 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_hi, 0x01));
  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_f98_r0r1 = _mm256_madd_epi16(ru8, fc98);
  *accum_out_r0r1 = _mm256_add_epi32(out_f98_r0r1, *accum_out_r0r1);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_f98_r2r3 = _mm256_madd_epi16(ru9, fc98);
  *accum_out_r2r3 = _mm256_add_epi32(out_f98_r2r3, *accum_out_r2r3);

  // Output corresponding to filter coefficient 2, 0, 3.
  // b1 d1 b2 d2 b3 d3 b4 d4 | c1 e1 c2 e2 c3 e3 c4 e4
  const __m256i ru10_0 = _mm256_alignr_epi8(ru5, ru4, 4);
  __m256i ru10 = _mm256_sub_epi16(ru10_0, center_pixel_row01_0);
  // d1 f1 d2 f2 d3 f3 d4 f4 | e1 g1 e2 g2 e3 g3 e4 g4
  const __m256i ru11_0 = _mm256_alignr_epi8(ru7, ru6, 4);
  __m256i ru11 = _mm256_sub_epi16(ru11_0, center_pixel_row23_0);

  // b2 d2 b3 d3 b4 d4 b5 d5 | c2 e2 c3 e3 c4 e4 c5 e5
  const __m256i ru12_0 = _mm256_alignr_epi8(ru5, ru4, 8);
  __m256i ru12 = _mm256_sub_epi16(ru12_0, center_pixel_row01_0);
  // d2 f2 d3 f3 d4 f4 d5 f5 | e2 g2 e3 g3 e4 g4 e5 g5
  const __m256i ru13_0 = _mm256_alignr_epi8(ru7, ru6, 8);
  __m256i ru13 = _mm256_sub_epi16(ru13_0, center_pixel_row23_0);

  // b3 d3 b4 d4 b5 d5 b6 d6 | c3 e3 c4 e4 c5 e5 c6 e6
  const __m256i ru14_0 = _mm256_alignr_epi8(ru5, ru4, 12);
  __m256i ru14 = _mm256_sub_epi16(ru14_0, center_pixel_row01_0);
  // d3 f3 d4 f4 d5 f5 d6 f6 | e3 g3 e4 g4 e5 g5 e6 g6
  const __m256i ru15_0 = _mm256_alignr_epi8(ru7, ru6, 12);
  __m256i ru15 = _mm256_sub_epi16(ru15_0, center_pixel_row23_0);

  // f5 f7 f5 f7 - - - -
  const __m256i fc57 = _mm256_broadcastd_epi32(
      _mm_shufflelo_epi16(_mm_bsrli_si128(filt_coeff_lo, 8), 0x0D));
  // f1 f0 f1 f0 - - - -
  const __m256i fc10 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_lo, 0x01));
  // f6 f4 f6 f4 - - - -
  const __m256i fc64 = _mm256_broadcastd_epi32(
      _mm_shufflelo_epi16(_mm_bsrli_si128(filt_coeff_lo, 8), 0x02));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING(fc57, fc10, fc64)

  // Output corresponding to filter coefficient 5, 1, 6.
  // c0 c1 c1 c2 c2 c3 c3 c4 || d0 d1 d1 d2 d2 d3 d3 d4
  ru10 = _mm256_unpacklo_epi16(src_cd, _mm256_bsrli_epi128(src_cd, 2));
  ru10 = _mm256_sub_epi16(ru10, center_pixel_row01_0);
  // e0 e1 e1 e2 e2 e3 e3 e4 || f0 f1 f1 f2 f2 f3 f3 f4
  ru11 = _mm256_unpacklo_epi16(src_ef, _mm256_bsrli_epi128(src_ef, 2));
  ru11 = _mm256_sub_epi16(ru11, center_pixel_row23_0);

  // c3 c4 c4 c5 c5 c6 c6 c7 || d3 d4 d4 d5 d5 d6 d6 d7
  ru12 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_cd, 6),
                               _mm256_bsrli_epi128(src_cd, 8));
  ru12 = _mm256_sub_epi16(ru12, center_pixel_row01_0);
  // e3 e4 e4 e5 e5 e6 e6 e7 || f3 f4 f4 f5 f5 f6 f6 f7
  ru13 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(src_ef, 6),
                               _mm256_bsrli_epi128(src_ef, 8));
  ru13 = _mm256_sub_epi16(ru13, center_pixel_row23_0);

  // f11  f3 f11  f3 - - - -
  const __m256i fc113 = _mm256_broadcastd_epi32(_mm_unpacklo_epi16(
      _mm_bsrli_si128(filt_coeff_hi, 6), _mm_bsrli_si128(filt_coeff_lo, 6)));

  //  f2 f10  f2 f10 - - - -
  const __m256i fc210 = _mm256_broadcastd_epi32(_mm_unpacklo_epi16(
      _mm_bsrli_si128(filt_coeff_lo, 4), _mm_bsrli_si128(filt_coeff_hi, 4)));

  // Multiply the formed registers with filter and add the result to accumulate
  // registers.
  MADD_AND_ACCUM_FOR_CHROMA_FILTERING_2(fc113, fc210)
}

// AVX2 intrinsic for convolve wiener non-separable loop restoration with 6-tap
// filtering. The output for a particular pixel in a 4x4 block is calculated
// with DIAMOND shaped filter considering a 5x5 grid surrounded by that pixel.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 x x
// Load Source Data:
// src_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// src_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// src_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// src_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// src_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// src_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// src_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at c2 position is calculated as below.
// Filtered_c2 = ((a2-c2)+(e2-c2))*fc4 + ((b1-c2+d3-c2))*fc2 +
// (b2-c2+d2-c2)*fc0 + (b3-c2+d1-c2)*fc3 + (c0-c2+c4-c2)*fc5 +
// (c1-c2+c3-c2)*fc1 + c2*singleton_tap + dc_offset
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc4 of rows 0 and 1 is achieved like below.
// __m256i centerpixel_row01 = c2 c2 c3 c3 c4 c4 c5 c5 | d2 d2 d3 d3 d4 d4 d5 d5
// __m256i src_reg3 = a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
// __m256i filter_4 = fc4 fc4 fc4 fc4 fc4 fc4 fc4 fc4 | fc4 fc4 fc4 fc4 fc4 fc4
// fc4 fc4
// __m256 src_reg3 = _mm256_sub_epi16(src_reg3, centerpixel_row01);
//  __m256i out_f4_01 = _mm256_madd_epi16(src_reg3, filter_4);
//                   = ((a2-c2)*fc4+(e2-c2)*fc4) (a3-c3)*fc4+(e3-c3)*fc4) .. |
//                   (b2-d2)*fc4 +(f2-d2)*fc4) . .
// Here, out_f4_01 contains partial output of rows 0 and 1 corresponding to fc4.
void av1_convolve_symmetric_subtract_center_highbd_6tap_avx2(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  // Derive singleton_tap and dc_offset.
  const int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t dc_offset = 0;
  if (filter_config->num_pixels % 2) {
    const int dc_offset_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    dc_offset = filter[dc_offset_tap_index];
  }

  // Load filter tap values.
  // fc0 fc1 fc2 fc3 fc4 fc5 center_tap x
  const __m128i filt_coeff_0 = _mm_loadu_si128((__m128i const *)(filter));
  // Replace the center_tap with derived singleton_tap.
  const __m128i center_tap = _mm_set1_epi16(singleton_tap);
  const __m128i filt_coeff = _mm_blend_epi16(filt_coeff_0, center_tap, 0x40);

  // Initializing the output registers with zero
  __m256i accum_out_r0r1 = _mm256_setzero_si256();
  __m256i accum_out_r2r3 = _mm256_setzero_si256();

  // Perform 6-tap filtering on source buffer
  apply_6tap_filtering(dgd, stride, filt_coeff, &accum_out_r0r1,
                       &accum_out_r2r3, block_row_begin, block_col_begin);

  // Offset addition
  const __m128i offset_reg = _mm_set1_epi32(dc_offset);
  const __m256i ofs = _mm256_inserti128_si256(
      _mm256_castsi128_si256(offset_reg), offset_reg, 1);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, ofs);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, ofs);

  // Store the output after rounding and clipping
  round_and_store_avx2(dst, dst_stride, filter_config, bit_depth,
                       accum_out_r0r1, accum_out_r2r3, block_row_begin,
                       block_col_begin);
}

// TODO(Arun Negi): Difference of source and center pixel needs to go through
// clip_base(). Implement clip_base() in intrinsic once the support is added.
//
// AVX2 intrinsic for convolve wiener non-separable loop restoration with
// 12/13-tap filtering. The output for a particular pixel in a 4x4 block is
// calculated with DIAMOND shaped filter considering a 7x7 grid surrounded by
// that pixel.
// The following describes the design considered for intrinsic
// implementation.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 fc6 fc7 fc8 fc9 fc10 fc11 fc12
// Load Source Data:
// src_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// src_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// src_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// src_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// src_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// src_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// src_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at d3 position is calculated as below.
// Filtered_d3 = ((a3-d3)+(g3-d3))*fc10 +
// ((b2-d3+f4-d3))*fc6 + (b3-d3+f3-d3)*fc2 + (b4-d3+f5-d3)*fc7 +
// (c1-d3+e5-d3)*fc8 + (c2-d3+e4)*fc4 + (c3-d3+e3-d3)*fc0 + (c4-d3+e2-d3)*fc5 +
// (c4-d3+e2-d3)*fc5 + (c5-d3+e1-d3)*fc9 + (d0-d3+d6-d3)*fc11 +
// (d1-d3+d5-d3)*fc03 + (d2-d3+d4-d3)*fc01 + d3*f12 + dc_offset
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc10 of rows 0 and 1 is achieved like below.
// __m256i centerpixel_row01 = d3 d3 d4 d4 d5 d5 d6 d6 | e3 e3 e4 e4 e5 e5 e6 e6
// __m256i src_reg3 = a3 g3 a4 g4 a5 g5 a6 g6 | b3 h3 b4 h4 b5 h5 b6 h6
// __m256i filter_10 = f10 f10 f10 f10 f10 f10 f10 f10 | f10 f10 f10 f10 f10 f10
// f10 f10
// __m256 src_reg3 = _mm256_sub_epi16(src_reg3, centerpixel_row01);
// __m256i out_f10_01 = _mm256_madd_epi16(src_reg3, filter_10);
//                   = ((a3-d3)*f10+(g3-d3)*f10) (a4-d4)*f10+(g4-d4)*f10) .. |
//                   (b3-e3)*f10+(h3-e3)*f10) . .
// Here, out_f10_01 contains partial output of rows 0 and 1 corresponding to
// fc10.
void av1_convolve_symmetric_subtract_center_highbd_12tap_avx2(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  // Derive singleton_tap and dc_offset.
  const int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t dc_offset = 0;
  if (filter_config->num_pixels % 2) {
    const int dc_offset_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    dc_offset = filter[dc_offset_tap_index];
  }

  // Load source data.
  int src_index_start = block_row_begin * stride + block_col_begin;
  const uint16_t *src_ptr = dgd + src_index_start - 3 * stride - 3;
  const __m128i src_a0 = _mm_loadu_si128((__m128i const *)src_ptr);
  const __m128i src_b0 = _mm_loadu_si128((__m128i const *)(src_ptr + stride));
  const __m128i src_c0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 2 * stride));
  const __m128i src_d0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 3 * stride));
  const __m128i src_e0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 4 * stride));
  const __m128i src_f0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 5 * stride));
  const __m128i src_g0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 6 * stride));
  const __m128i src_h0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 7 * stride));
  const __m128i src_i0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 8 * stride));
  const __m128i src_j0 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 9 * stride));

  // Load filter tap values.
  // fc0 fc1 fc2 fc3 fc4 fc5 fc6 fc7
  const __m128i filt_coeff_0 = _mm_loadu_si128((__m128i const *)(filter));
  // fc5 fc6 fc7 fc8 fc9 fc10 fc11 fc12
  const __m128i filta = _mm_loadu_si128((__m128i const *)(filter + 5));
  // Replace the fc12 with derived singleton_tap.
  const __m128i center_tap = _mm_set1_epi16(singleton_tap);
  const __m128i filt_coeff_1 = _mm_blend_epi16(filta, center_tap, 0x80);

  // Form 256bit source registers.
  // a0 a1 a2 a3 a4 a5 a6 a7 | b0 b1 b2 b3 b4 b5 b6 b7
  const __m256i src_ab =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_a0), src_b0, 1);
  // c0 c1 c2 c3 c4 c5 c6 c7 | d0 d1 d2 d3 d4 d5 d6 d7
  const __m256i src_cd =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_c0), src_d0, 1);
  // e0 e1 e2 e3 e4 e5 e6 e7 | f0 f1 f2 f3 f4 f5 f6 f7
  const __m256i src_ef =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_e0), src_f0, 1);
  // g0 g1 g2 g3 g4 g5 g6 g7 | h0 h1 h2 h3 h4 h5 h6 h7
  const __m256i src_gh =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_g0), src_h0, 1);
  // i0 i1 i2 i3 i4 i5 i6 i7 | j0 j1 j2 j3 j4 j5 j6 j7
  const __m256i src_ij =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_i0), src_j0, 1);

  // Derive registers to hold center pixel
  const __m128i center_pixel_d = _mm_bslli_si128(src_d0, 2);
  const __m128i cp_d_high = _mm_unpackhi_epi16(center_pixel_d, center_pixel_d);
  const __m128i center_pixel_e = _mm_bslli_si128(src_e0, 2);
  const __m128i cp_e_high = _mm_unpackhi_epi16(center_pixel_e, center_pixel_e);
  // d3 d3 d4 d4 d5 d5 d6 d6 | e3 e3 e4 e4 e5 e5 e6 e6
  const __m256i center_pixel_row01_0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(cp_d_high), cp_e_high, 1);
  const __m128i center_pixel_f = _mm_bslli_si128(src_f0, 2);
  const __m128i cp_f_high = _mm_unpackhi_epi16(center_pixel_f, center_pixel_f);
  const __m128i center_pixel_g = _mm_bslli_si128(src_g0, 2);
  const __m128i cp_g_high = _mm_unpackhi_epi16(center_pixel_g, center_pixel_g);
  // f3 f3 f4 f4 f5 f5 f6 f6 | g3 g3 g4 g4 g5 g5 g6 g6
  const __m256i center_pixel_row23_0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(cp_f_high), cp_g_high, 1);

  const __m128i zero = _mm_set1_epi16(0x0);
  const __m128i cp_d0_high = _mm_unpackhi_epi16(center_pixel_d, zero);
  const __m128i cp_e0_high = _mm_unpackhi_epi16(center_pixel_e, zero);
  // d3 0 d4 0 d5 0 d6 0 | e3 0 e4 0 e5 0 e6 0
  const __m256i center_pixel_row01_1 = _mm256_inserti128_si256(
      _mm256_castsi128_si256(cp_d0_high), cp_e0_high, 1);
  const __m128i cp_f0_high = _mm_unpackhi_epi16(center_pixel_f, zero);
  const __m128i cp_g0_high = _mm_unpackhi_epi16(center_pixel_g, zero);
  // f3 0 f4 0 f5 0 f6 0 | g3 0 g4 0 g5 0 g6 0
  const __m256i center_pixel_row23_1 = _mm256_inserti128_si256(
      _mm256_castsi128_si256(cp_f0_high), cp_g0_high, 1);

  // Packing the source rows.
  // a0 g0 a1 g1 a2 g2 a3 g3 | b0 h0 b1 h1 b2 h2 b3 h3
  const __m256i ru0 = _mm256_unpacklo_epi16(src_ab, src_gh);
  // a4 g4 a5 g5 a6 g6 a7 g7 | b4 h4 b5 h5 b6 h6 b7 h7
  const __m256i ru1 = _mm256_unpackhi_epi16(src_ab, src_gh);
  // c0 e0 c1 e1 c2 e2 c3 e3 | d0 f0 d1 f1 d2 f2 d3 f3
  const __m256i ru2 = _mm256_unpacklo_epi16(src_cd, src_ef);
  // c4 e4 c5 e5 c6 e6 c7 e7 | d4 f4 d5 f5 d6 f6 d7 f7
  __m256i ru3 = _mm256_unpackhi_epi16(src_cd, src_ef);
  // c0 i0 c1 i1 c2 i2 c3 i3 | d0 j0 d1 j1 d2 j2 d3 j3
  const __m256i ru4 = _mm256_unpacklo_epi16(src_cd, src_ij);
  // c4 i4 c5 i5 c6 i6 c7 i7 | d4 j4 d5 j5 d6 j6 d7 j7
  const __m256i ru5 = _mm256_unpackhi_epi16(src_cd, src_ij);
  // e0 g0 e1 g1 e2 g2 e3 g3 | f0 h0 f1 h1 f2 h2 f3 h3
  const __m256i ru6 = _mm256_unpacklo_epi16(src_ef, src_gh);
  // e4 g4 e5 g5 e6 g6 e7 g7 | f4 h4 f5 h5 f6 h6 f7 h7
  __m256i ru7 = _mm256_unpackhi_epi16(src_ef, src_gh);

  // Output corresponding to filter coefficient 10.
  // a3 g3 a4 g4 a5 g5 a6 g6 | b3 h3 b4 h4 b5 h5 b6 h6
  const __m256i ru8_0 = _mm256_alignr_epi8(ru1, ru0, 12);
  const __m256i ru8 = _mm256_sub_epi16(ru8_0, center_pixel_row01_0);
  // c3 i3 c4 i4 c5 i5 c6 i6 | d3 j3 d4 j4 d5 j5 d6 j6
  const __m256i ru9_0 = _mm256_alignr_epi8(ru5, ru4, 12);
  const __m256i ru9 = _mm256_sub_epi16(ru9_0, center_pixel_row23_0);
  // f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10 f10
  const __m128i fil10 = _mm_bsrli_si128(filt_coeff_1, 4);
  const __m256i fc10 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(fil10, 0x0F));
  // r00 r01 r02 r03 | r10 r11 r12 r13
  __m256i accum_out_r0r1 = _mm256_madd_epi16(ru8, fc10);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  __m256i accum_out_r2r3 = _mm256_madd_epi16(ru9, fc10);

  // Output corresponding to filter coefficients 8,4,0,5.
  // c1 e1 c2 e2 c3 e3 c4 e4 | d1 f1 d2 f2 d3 f3 d4 f4
  const __m256i ru10_0 = _mm256_alignr_epi8(ru3, ru2, 4);
  const __m256i ru10 = _mm256_sub_epi16(ru10_0, center_pixel_row01_0);

  // e1 g1 e2 g2 e3 g3 e4 g4 | f1 h1 f2 h2 f3 h3 f4 h4
  const __m256i ru11_0 = _mm256_alignr_epi8(ru7, ru6, 4);
  const __m256i ru11 = _mm256_sub_epi16(ru11_0, center_pixel_row23_0);

  // c2 e2 c3 e3 c4 e4 c5 e5 | d2 f2 d3 f3 d4 f4 d5 f5
  const __m256i ru12_0 = _mm256_alignr_epi8(ru3, ru2, 8);
  const __m256i ru12 = _mm256_sub_epi16(ru12_0, center_pixel_row01_0);

  // e2 g2 e3 g3 e4 g4 e5 g5 | f2 h2 f3 h3 f4 h4 f5 h5
  const __m256i ru13_0 = _mm256_alignr_epi8(ru7, ru6, 8);
  const __m256i ru13 = _mm256_sub_epi16(ru13_0, center_pixel_row23_0);

  // c3 e3 c4 e4 c5 e5 c6 e6 | d3 f3 d4 f4 d5 f5 d6 f6
  const __m256i ru14_0 = _mm256_alignr_epi8(ru3, ru2, 12);
  const __m256i ru14 = _mm256_sub_epi16(ru14_0, center_pixel_row01_0);

  // e3 g3 e4 g4 e5 g5 e6 g6 | f3 h3 f4 h4 f5 h5 f6 h6
  const __m256i ru15_0 = _mm256_alignr_epi8(ru7, ru6, 12);
  const __m256i ru15 = _mm256_sub_epi16(ru15_0, center_pixel_row23_0);

  // fc6 fc7 fc8 fc9 fc10 fc11 fc12 0
  const __m128i filt89 = _mm_bsrli_si128(filt_coeff_1, 6);
  const __m256i fc89 = _mm256_broadcastd_epi32(filt89);
  const __m128i filt45 = _mm_bsrli_si128(filt_coeff_0, 8);
  const __m256i fc45 = _mm256_broadcastd_epi32(filt45);
  const __m256i fc00 = _mm256_broadcastw_epi16(filt_coeff_0);
  const __m128i filt54 = _mm_bsrli_si128(filt_coeff_0, 4);
  const __m256i fc54 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt54, 0x0B));

  ru3 = _mm256_sub_epi16(ru3, center_pixel_row01_0);
  ru7 = _mm256_sub_epi16(ru7, center_pixel_row23_0);
  const __m256i res_0 = _mm256_madd_epi16(ru10, fc89);
  const __m256i res_1 = _mm256_madd_epi16(ru11, fc89);
  const __m256i res_2 = _mm256_madd_epi16(ru12, fc45);
  const __m256i res_3 = _mm256_madd_epi16(ru13, fc45);
  const __m256i res_4 = _mm256_madd_epi16(ru14, fc00);
  const __m256i res_5 = _mm256_madd_epi16(ru15, fc00);
  const __m256i res_6 = _mm256_madd_epi16(ru3, fc54);
  const __m256i res_7 = _mm256_madd_epi16(ru7, fc54);
  // r00 r01 r02 r03 | r10 r11 r12 r13
  const __m256i out_0 = _mm256_add_epi32(res_0, res_2);
  const __m256i out_1 = _mm256_add_epi32(res_4, res_6);
  const __m256i out_2 = _mm256_add_epi32(out_0, out_1);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, out_2);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  const __m256i out_3 = _mm256_add_epi32(res_1, res_3);
  const __m256i out_4 = _mm256_add_epi32(res_5, res_7);
  const __m256i out_5 = _mm256_add_epi32(out_3, out_4);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, out_5);

  // Output corresponding to filter coefficients 11,3,1,12.
  // d2 d3 d4	d5 d6 d7 d8 d9
  const __m128i src_d2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 3 * stride + 2));
  // e2 e3 e4 e5 e6 e7 e8 e9
  const __m128i src_e2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 4 * stride + 2));
  // f2 f3 f4 f5 f6 f7 f8 f9
  const __m128i src_f2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 5 * stride + 2));
  // g2 g3 g4	g5 g6 g7 g8 g9
  const __m128i src_g2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 6 * stride + 2));

  // d0 d1 d2 d3 d4 d5 d6 d7 | e0 e1 e2 e3 e4 e5 e6 e7
  const __m256i rm0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d0), src_e0, 1);
  // d2 d3 d4 d5 d6 d7 d8 d9 | e2 e3 e4 e5 e6 e7 e8 e9
  const __m256i rm2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d2), src_e2, 1);
  // f0 f1 f2 f3 f4 f5 f6 f7 | g0 g1 g2 g3 g4 g5 g6 g7
  const __m256i rm00 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f0), src_g0, 1);
  // f2 f3 f4 f5 f6 f7 f8 f9 | g2 g3 g4 g5 g6 g7 g8 g9
  const __m256i rm22 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f2), src_g2, 1);
  // d1 d2 d3 d4 d5 d6 d7 d8 | e1 e2 e3 e4 e5 e6 e7 e8
  const __m256i rm1 = _mm256_alignr_epi8(_mm256_bsrli_epi128(rm2, 12), rm0, 2);
  const __m256i rm11 =
      _mm256_alignr_epi8(_mm256_bsrli_epi128(rm22, 12), rm00, 2);
  // d3 d4 d5 d6 d7 d8 d9 0 | e3 e4 e5 e6 e7 e8 e9 0
  const __m256i rm3 = _mm256_bsrli_epi128(rm2, 2);
  const __m256i rm33 = _mm256_bsrli_epi128(rm22, 2);
  // d0 d1 d1 d2 d2 d3 d3 d4 | e0 e1 e1 e2 e2 e3 e3 e4
  __m256i rm4 = _mm256_unpacklo_epi16(rm0, rm1);
  rm4 = _mm256_sub_epi16(rm4, center_pixel_row01_0);
  // d2 d3 d3 d4 d4 d5 d5 d6 | e2 e3 e3 e4 e4 e5 e5 e6
  __m256i rm5 = _mm256_unpacklo_epi16(rm2, rm3);
  rm5 = _mm256_sub_epi16(rm5, center_pixel_row01_1);
  // d4 d5 d5 d6 d6 d7 d7 d8 | e4 e5 e5 e6 e6 e7 e7 e8
  __m256i rm6 = _mm256_unpackhi_epi16(rm0, rm1);
  rm6 = _mm256_sub_epi16(rm6, center_pixel_row01_0);
  // d6 0 d7 0 d8 0 d9 0 | e6 0 e7 0 e8 0 e9 0
  __m256i rm7 = _mm256_unpackhi_epi16(rm2, _mm256_set1_epi16(0));
  rm7 = _mm256_sub_epi16(rm7, center_pixel_row01_0);

  __m256i rm44 = _mm256_unpacklo_epi16(rm00, rm11);
  rm44 = _mm256_sub_epi16(rm44, center_pixel_row23_0);
  __m256i rm55 = _mm256_unpacklo_epi16(rm22, rm33);
  rm55 = _mm256_sub_epi16(rm55, center_pixel_row23_1);
  __m256i rm66 = _mm256_unpackhi_epi16(rm00, rm11);
  rm66 = _mm256_sub_epi16(rm66, center_pixel_row23_0);
  __m256i rm77 = _mm256_unpackhi_epi16(rm22, _mm256_set1_epi16(0));
  rm77 = _mm256_sub_epi16(rm77, center_pixel_row23_0);

  // fc11 fc03 fc11 fc03 - -
  const __m128i fc11_fc03 =
      _mm_blend_epi16(_mm_bsrli_si128(filt_coeff_1, 8), filt_coeff_0, 0x08);
  const __m256i filt1103 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(fc11_fc03, 0x0E));

  // fc01 fc12 fc01 fc12 - -
  const __m128i fc01_fc12 =
      _mm_blend_epi16(_mm_bsrli_si128(filt_coeff_1, 8), filt_coeff_0, 0x02);
  const __m256i filt0112 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(fc01_fc12, 0x0D));

  // fc01 fc03 fc01 fc03 - -
  const __m256i fc0103 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_0, 0x0D));

  // f11 0 f11 0 f11 0 f11 0 - -
  const __m256i fc11z = _mm256_broadcastd_epi32(_mm_shufflelo_epi16(
      _mm_bsrli_si128(_mm_unpackhi_epi16(filt_coeff_1, zero), 4), 0x0E));

  rm4 = _mm256_madd_epi16(rm4, filt1103);
  rm5 = _mm256_madd_epi16(rm5, filt0112);
  rm6 = _mm256_madd_epi16(rm6, fc0103);
  rm7 = _mm256_madd_epi16(rm7, fc11z);
  rm44 = _mm256_madd_epi16(rm44, filt1103);
  rm55 = _mm256_madd_epi16(rm55, filt0112);
  rm66 = _mm256_madd_epi16(rm66, fc0103);
  rm77 = _mm256_madd_epi16(rm77, fc11z);

  // r00 r01 r02 r03 | r10 r11 r12 r13
  rm4 = _mm256_add_epi32(rm4, rm5);
  rm6 = _mm256_add_epi32(rm6, rm7);
  rm4 = _mm256_add_epi32(rm4, rm6);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, rm4);

  // r20 r21 r22 r23 | r30 r31 r32 r33
  rm44 = _mm256_add_epi32(rm44, rm55);
  rm66 = _mm256_add_epi32(rm66, rm77);
  rm44 = _mm256_add_epi32(rm44, rm66);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, rm44);

  // Output corresponding to filter coefficients 6,2,7,9.
  const __m128i src_b2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 1 * stride + 2));
  const __m128i src_c2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 2 * stride + 2));
  const __m256i rn0 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_b2), src_c2, 1);
  // b2 b3 b3 b4 b4 b5 b5 b6 | c2 c3 c3 c4 c4 c5 c5 c6
  __m256i r0 = _mm256_unpacklo_epi16(rn0, _mm256_bsrli_epi128(rn0, 2));
  r0 = _mm256_sub_epi16(r0, center_pixel_row01_0);

  const __m256i rcd2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_c2), src_d2, 1);
  // b4 c5 b5 c6 b6 c7 b7 c8 | c4 d5 c5 d6 c6 d7 c7 d8
  __m256i r1 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rn0, 4),
                                     _mm256_bsrli_epi128(rcd2, 6));
  r1 = _mm256_sub_epi16(r1, center_pixel_row01_0);

  const __m256i rfg2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_f2), src_g2, 1);
  // f2 f3 f3 f4 f4 f5 f5 f6 | g2 g3 g3 g4 g4 g5 g5 g6
  __m256i r2 = _mm256_unpacklo_epi16(rfg2, _mm256_bsrli_epi128(rfg2, 2));
  r2 = _mm256_sub_epi16(r2, center_pixel_row01_0);

  const __m256i ref2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_e2), src_f2, 1);
  // f4 e5 f5 e6 f6 e7 f7 e8 | g4 f5 g5 f6 g6 f7 g7 f8
  __m256i r3 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rfg2, 4),
                                     _mm256_bsrli_epi128(ref2, 6));
  r3 = _mm256_sub_epi16(r3, center_pixel_row01_0);

  const __m128i filt62 = _mm_blend_epi16(filt_coeff_1, filt_coeff_0, 0x04);
  // fc6 fc2 - - - - - - | fc6 fc2 - - - - - -
  const __m256i fc62 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt62, 0x09));
  // fc7 fc9 - - - - - - | fc7 fc9 - - - - - -
  const __m256i fc79 = _mm256_broadcastd_epi32(
      _mm_shufflelo_epi16(_mm_bsrli_si128(filt_coeff_1, 2), 0x0D));
  // fc7 fc2 - - - - - - | fc7 fc2 - - - - - -
  const __m128i filt72 =
      _mm_blend_epi16(_mm_bsrli_si128(filt_coeff_1, 2), filt_coeff_0, 0x04);
  const __m256i fc72 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt72, 0x09));
  // fc6 fc8 - - - - - - | fc6 fc8 - - - - - -
  const __m256i fc68 =
      _mm256_broadcastd_epi32(_mm_shufflelo_epi16(filt_coeff_1, 0x0D));

  r0 = _mm256_madd_epi16(r0, fc62);
  r1 = _mm256_madd_epi16(r1, fc79);
  r2 = _mm256_madd_epi16(r2, fc72);
  r3 = _mm256_madd_epi16(r3, fc68);
  // r00 r01 r02 r03 | r10 r11 r12 r13
  r0 = _mm256_add_epi32(r0, r1);
  r2 = _mm256_add_epi32(r2, r3);
  r0 = _mm256_add_epi32(r0, r2);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, r0);

  const __m256i rn1 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_d2), src_e2, 1);
  // d2 d3 d3 d4 d4 d5 d5 d6 | e2 e3 e3 e4 e4 e5 e5 e6
  __m256i r00 = _mm256_unpacklo_epi16(rn1, _mm256_bsrli_epi128(rn1, 2));
  r00 = _mm256_sub_epi16(r00, center_pixel_row23_0);
  // d4 e5 d5 e6 d6 e7 d7 e8 | e4 f5 e5 f6 e6 f7 e7 f8
  __m256i r11 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rn1, 4),
                                      _mm256_bsrli_epi128(ref2, 6));
  r11 = _mm256_sub_epi16(r11, center_pixel_row23_0);

  const __m128i src_h2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 7 * stride + 2));
  const __m128i src_i2 =
      _mm_loadu_si128((__m128i const *)(src_ptr + 8 * stride + 2));
  // h2 h3 h4 h5 h6 h7 h8 h9 | i2 i3 i4 i5 i6 i7 i8 i9
  __m256i rhi2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_h2), src_i2, 1);
  // h2 h3 h3 h4 h4 h5 h5 h6 | i2 i3 i3 i4 i4 i5 i5 i6
  __m256i r22 = _mm256_unpacklo_epi16(rhi2, _mm256_bsrli_epi128(rhi2, 2));
  r22 = _mm256_sub_epi16(r22, center_pixel_row23_0);
  // g2 g3 g4 g5 g6 g7 g8 g9 | h2 h3 h4 h5 h6 h7 h8 h9
  __m256i rgh2 =
      _mm256_inserti128_si256(_mm256_castsi128_si256(src_g2), src_h2, 1);
  __m256i r33 = _mm256_unpacklo_epi16(_mm256_bsrli_epi128(rhi2, 4),
                                      _mm256_bsrli_epi128(rgh2, 6));
  r33 = _mm256_sub_epi16(r33, center_pixel_row23_0);

  r00 = _mm256_madd_epi16(r00, fc62);
  r11 = _mm256_madd_epi16(r11, fc79);
  r22 = _mm256_madd_epi16(r22, fc72);
  r33 = _mm256_madd_epi16(r33, fc68);
  // r20 r21 r22 r23 | r30 r31 r32 r33
  r00 = _mm256_add_epi32(r00, r11);
  r22 = _mm256_add_epi32(r22, r33);
  r00 = _mm256_add_epi32(r00, r22);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, r00);

  // Offset addition
  const __m128i offset_reg = _mm_set1_epi32(dc_offset);
  const __m256i ofs = _mm256_inserti128_si256(
      _mm256_castsi128_si256(offset_reg), offset_reg, 1);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, ofs);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, ofs);

  // Rounding and clipping.
  accum_out_r0r1 =
      round_power_of_two_signed_avx2(accum_out_r0r1, filter_config->prec_bits);
  accum_out_r2r3 =
      round_power_of_two_signed_avx2(accum_out_r2r3, filter_config->prec_bits);
  // r00 r01 r02 r03 | r20 r21 r22 r23 | r10 r11 r12 r13 | r30 r31 r32 r33
  __m256i out_r0r2r1r3 = _mm256_packs_epi32(accum_out_r0r1, accum_out_r2r3);
  const __m256i max = _mm256_set1_epi16((1 << bit_depth) - 1);
  out_r0r2r1r3 = highbd_clamp_epi16(out_r0r2r1r3, _mm256_setzero_si256(), max);

  // Store the output.
  const int dst_id = block_row_begin * dst_stride + block_col_begin;
  const __m128i out_r1r3 = _mm256_extractf128_si256(out_r0r2r1r3, 1);
  _mm_storel_epi64((__m128i *)(dst + dst_id),
                   _mm256_castsi256_si128(out_r0r2r1r3));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (1 * dst_stride)), out_r1r3);

  _mm_storel_epi64((__m128i *)(dst + dst_id + (2 * dst_stride)),
                   _mm_bsrli_si128(_mm256_castsi256_si128(out_r0r2r1r3), 8));
  _mm_storel_epi64((__m128i *)(dst + dst_id + (3 * dst_stride)),
                   _mm_bsrli_si128(out_r1r3, 8));
}

// SIMD implementation to convolve a block of pixels with origin-symmetric,
// wiener non-separable filter corresponds to CONFIG_WIENER_NONSEP loop
// restoration. DIAMOND shape with 13/12-tap or 6-tap filter is used for
// convolution.

// TODO(rachelbarker): Standardize on non-subtract-center code path,
// so that this one can be removed
void av1_convolve_symmetric_subtract_center_highbd_avx2(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(filter_config->subtract_center);
  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;
  const int num_sym_taps = filter_config->num_pixels / 2;

  // Dispatch to the appropriate SIMD function for the selected filter shape
  // and block size. If none are applicable, fall back to C
  if (num_rows == 4 && num_cols == 4 && num_sym_taps == 6 &&
      (filter_config->config == wienerns_simd_config_uv_from_uv ||
       !memcmp(wienerns_simd_config_uv_from_uv, filter_config->config,
               filter_config->num_pixels * 3 *
                   sizeof(filter_config->config[0][0])))) {
    av1_convolve_symmetric_subtract_center_highbd_6tap_avx2(
        dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  } else if (num_rows == 4 && num_cols == 4 && num_sym_taps == 12 &&
             (filter_config->config == wienerns_simd_config_y ||
              !memcmp(wienerns_simd_config_y, filter_config->config,
                      filter_config->num_pixels * 3 *
                          sizeof(filter_config->config[0][0])))) {
    av1_convolve_symmetric_subtract_center_highbd_12tap_avx2(
        dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  } else {
    av1_convolve_symmetric_subtract_center_highbd_c(
        dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
        block_row_begin, block_row_end, block_col_begin, block_col_end);
  }
}

// AVX2 intrinsic for convolve wiener non-separable dual loop restoration
// filtering with subtract center off. Two buffers (dgd and dgd_dual) are
// considered for dual filtering, where each buffer goes through 7-tap
// filtering. The output for a particular pixel in a 4x4 block is calculated
// with DIAMOND shaped filter considering a 5x5 grid surrounded by that pixel.
// A total of 14 filter taps required and are expected to be passed as filter
// taps required for dgd(first) buffer followed by dgd_dual(second) buffer
// filter taps. The exact order of filter taps needed is shown below.
// Filter Taps: fc0 fc1 fc2 fc3 fc4 fc5 fc6 fc7 fc8 fc9 fc10 fc11 fc12 fc13.
// Here, 'fc0-fc6' and 'fc7-fc13' are corresponding to filter taps used for
// dgd and dgd_dual buffers respectively. Also, 'fc0-fc5' and 'fc7-fc12' are the
// symmetric taps and 'fc6' and 'fc13' are the center taps correspond to dgd and
// dgd_dual buffers respectively.
//
// The following describes the algorithm briefly.
// 7-tap filtering for dgd (first) buffer:
// Load Source Data:
// dgd_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// dgd_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// dgd_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// dgd_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// dgd_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// dgd_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// dgd_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// The output for a pixel located at c2 position is calculated as below.
// dgd_output_c2 = ((a2+e2)*fc4 + (b1+d3)*fc2 + (b2+d2)*fc0 + (b3+d1)*fc3 +
// (c0+c4)*fc5 + (c1+c3)*fc1 + c2*fc6(singleton_tap)
//
// 7-tap filtering for dgd_dual (second) buffer:
// Load Source Data:
// dgd_dual_ra0 = a0 a1 a2 a3 a4 a5 a6 a7
// dgd_dual_rb0 = b0 b1 b2 b3 b4 b5 b6 b7
// dgd_dual_rc0 = c0 c1 c2 c3 c4 c5 c6 c7
// dgd_dual_rd0 = d0 d1 d2 d3 d4 d5 d6 d7
// dgd_dual_re0 = e0 e1 e2 e3 e4 e5 e6 e7
// dgd_dual_rf0 = f0 f1 f2 f3 f4 f5 f6 f7
// dgd_dual_rg0 = g0 g1 g2 g3 g4 g5 g6 g7
// dgd_dual_output_c2 = (a2+e2)*fc11 + (b1+d3)*fc9 + (b2+d2)*fc7 + (b3+d1)*fc10
// + (c0+c4)*fc12 + (c1+c3)*fc8 + c2*fc13((singleton_tap_dual)
// The final output corresponding to c2 is calculated as below.
// output_c2 = dgd_output_c2 + dgd_dual_output_c2
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The dgd(first) buffer output corresponding to fc4 of rows 0 and 1 is achieved
// like below.
// __m256i src_reg3 = a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
// __m256i filter_4 = fc4 fc4 fc4 fc4 fc4 fc4 fc4 fc4 | fc4 fc4 fc4 fc4 fc4
// fc4 fc4 fc4
//  __m256i out_f4_01 = _mm256_madd_epi16(src_reg3, filter_4);
//                   = (a2*fc4+e2*fc4) (a3*fc4+e3*fc4) .. | (b2*fc4+f2*fc4)
//                   . .
// Here, out_f4_01 contains partial output of rows 0 and 1 corresponding to
// fc4.
//
/*
// Should be same as wienerns_simd_config_uv_from_uv
const int config_7tap_uv_from_uv_avx2[][3] = {
  { 1, 0, 0 },   { -1, 0, 0 }, { 0, 1, 1 },  { 0, -1, 1 }, { 1, 1, 2 },
  { -1, -1, 2 }, { -1, 1, 3 }, { 1, -1, 3 }, { 2, 0, 4 },  { -2, 0, 4 },
  { 0, 2, 5 },   { 0, -2, 5 }, { 0, 0, 18 },
};

// Should be same as wienerns_simd_config_uv_from_y
const int config_13tap_uv_from_y_avx2[][3] = {
  { 1, 0, 6 },    { -1, 0, 7 },  { 0, 1, 8 },   { 0, -1, 9 }, { 1, 1, 10 },
  { -1, -1, 11 }, { -1, 1, 12 }, { 1, -1, 13 }, { 2, 0, 14 },  { -2, 0, 15 },
  { 0, 2, 16 },   { 0, -2, 17 }, { 0, 0, 19 },
};
*/

void av1_convolve_symmetric_dual_highbd_7plus13tap_avx2(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  // Derive singleton_tap and singleton_tap_dual and repalce center tap filter
  // values with the same.
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t singleton_tap_dual = 0;
  if (filter_config->num_pixels % 2) {
    const int singleton_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[singleton_tap_index];
  }

  if (filter_config->num_pixels2 % 2) {
    const int last_config = filter_config->num_pixels2 - 1;
    const int singleton_tap_index =
        filter_config->config2[last_config][NONSEP_BUF_POS];
    singleton_tap_dual += filter[singleton_tap_index];
  }

  // Prepare filter coefficients for dgd(first) buffer 7-tap filtering
  // fc0 fc1 fc2 fc3 fc4 fc5 fc18(center_tap) x
  __m128i filter_coeff = _mm_loadu_si128((__m128i const *)(filter));
  // Replace the center_tap with derived singleton_tap.
  const __m128i center_tap1 = _mm_set1_epi16(singleton_tap);
  const __m128i filter_coeff_dgd =
      _mm_blend_epi16(filter_coeff, center_tap1, 0x40);

  // Prepare filter coefficients for dgd_dual(second) buffer 13-tap filtering
  // Currently we only support the case where there is a center tap in the
  // in-plane filter, so that the dual filter taps are at indices:
  // fc6 fc7 fc8 fc9 fc10 fc11 fc12 fc13 | fc14 fc15 fc16 fc17 center x x x
  assert(filter_config->num_pixels == 13);
  const __m128i filter_coeff_dual_lo =
      _mm_loadu_si128((__m128i const *)(filter + 6));
  __m128i filter_coeff_dual_hi =
      _mm_bsrli_si128(_mm_loadu_si128((__m128i const *)(filter + 12)), 4);
  const __m128i center_tap2 = _mm_set1_epi16(singleton_tap_dual);
  filter_coeff_dual_hi =
      _mm_blend_epi16(filter_coeff_dual_hi, center_tap2, 0x10);

  // Initialize the output registers with zero.
  __m256i accum_out_r0r1 = _mm256_setzero_si256();
  __m256i accum_out_r2r3 = _mm256_setzero_si256();

  // 7-tap filtering for dgd (first) buffer.
  apply_7tap_filtering_with_subtract_center_off(
      dgd, dgd_stride, filter_coeff_dgd, &accum_out_r0r1, &accum_out_r2r3,
      block_row_begin, block_col_begin);

  // 7-tap filtering for dgd_dual (second) buffer.
  apply_asym_13tap_filtering_with_subtract_center_off(
      dgd_dual, dgd_dual_stride, filter_coeff_dual_lo, filter_coeff_dual_hi,
      &accum_out_r0r1, &accum_out_r2r3, block_row_begin, block_col_begin);

  // Store the output after rounding and clipping.
  round_and_store_avx2(dst, dst_stride, filter_config, bit_depth,
                       accum_out_r0r1, accum_out_r2r3, block_row_begin,
                       block_col_begin);
}

void av1_convolve_symmetric_dual_highbd_avx2(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(!filter_config->subtract_center);

  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;
  assert(num_rows >= 0 && num_cols >= 0);

  // const int num_sym_taps = filter_config->num_pixels / 2;
  // const int num_taps_dual = filter_config->num_pixels2;

  // Dispatch to the appropriate SIMD function for the selected filter shape
  // and block size. If none are applicable, fall back to C
  // TODO(rachelbarker): Add fast path for reduced-length filters which don't
  // use any cross-plane filtering
  if (num_rows == 4 && num_cols == 4 &&
      ((wienerns_simd_config_uv_from_uv == filter_config->config &&
        wienerns_simd_config_uv_from_y == filter_config->config2) ||
       (!memcmp(wienerns_simd_config_uv_from_uv, filter_config->config,
                filter_config->num_pixels * 3 *
                    sizeof(filter_config->config[0][0])) &&
        !memcmp(wienerns_simd_config_uv_from_y, filter_config->config,
                filter_config->num_pixels2 * 3 *
                    sizeof(filter_config->config2[0][0]))))) {
    av1_convolve_symmetric_dual_highbd_7plus13tap_avx2(
        dgd, dgd_stride, dgd_dual, dgd_dual_stride, filter_config, filter, dst,
        dst_stride, bit_depth, block_row_begin, block_col_begin);
  } else {
    av1_convolve_symmetric_dual_highbd_c(
        dgd, dgd_stride, dgd_dual, dgd_dual_stride, filter_config, filter, dst,
        dst_stride, bit_depth, block_row_begin, block_row_end, block_col_begin,
        block_col_end);
  }
}

// AVX2 intrinsic for convolve wiener non-separable dual loop restoration
// filtering. The output for a particular pixel in a 4x4 block is calculated
// with DIAMOND shaped filter considering a 5x5 grid surrounded by that pixel.
// Filter Coefficients: fc0 fc1 fc2 fc3 fc4 fc5 f6 f7 f8 f9 f10 f11
// 6-tap filtering for dgd (first) buffer:
// dgd_reg_a = a0 a1 a2 a3 a4 a5 a6 a7
// dgd_reg_b = b0 b1 b2 b3 b4 b5 b6 b7
// dgd_reg_c = c0 c1 c2 c3 c4 c5 c6 c7
// dgd_reg_d = d0 d1 d2 d3 d4 d5 d6 d7
// dgd_reg_e = e0 e1 e2 e3 e4 e5 e6 e7
// The output for a pixel located at c2 position is calculated as below.
// dgd_output_c2 = ((a2-c2)+(e2-c2))*fc4 + ((b1-c2+d3-c2))*fc2 +
// (b2-c2+d2-c2)*fc0 + (b3-c2+d1-c2)*fc3 + (c0-c2+c4-c2)*fc5 +
// (c1-c2+c3-c2)*fc1 + c2*singleton_tap + dc_offset
//
// 6-tap filtering for dgd_dual (second) buffer:
// dgd_dual_reg_a = a0 a1 a2 a3 a4 a5 a6 a7
// dgd_dual_reg_b = b0 b1 b2 b3 b4 b5 b6 b7
// dgd_dual_reg_c = c0 c1 c2 c3 c4 c5 c6 c7
// dgd_dual_reg_d = d0 d1 d2 d3 d4 d5 d6 d7
// dgd_dual_reg_e = e0 e1 e2 e3 e4 e5 e6 e7
// dgd_dual_output_c2 = ((a2-c2)+(e2-c2))*fc10 + ((b1-c2+d3-c2))*fc8 +
// (b2-c2+d2-c2)*fc6 + (b3-c2+d1-c2)*fc9 + (c0-c2+c4-c2)*fc11 +
// (c1-c2+c3-c2)*fc7
// output_c2 = dgd_output_c2 + dgd_dual_output_c2
// The source registers are unpacked such that the output corresponding to 2
// rows will be produced in a single register (i.e., processing 2 rows
// simultaneously).
//
// Example:
// The output corresponding to fc4 of rows 0 and 1 is achieved like below.
// __m256i centerpixel_row01 = c2 c2 c3 c3 c4 c4 c5 c5 | d2 d2 d3 d3 d4 d4 d5 d5
// __m256i src_reg3 = a2 e2 a3 e3 a4 e4 a5 e5 | b2 f2 b3 f3 b4 f4 b5 f5
// __m256i filter_4 = fc4 fc4 fc4 fc4 fc4 fc4 fc4 fc4 | fc4 fc4 fc4 fc4 fc4 fc4
// fc4 fc4
// __m256 src_reg3 = _mm256_sub_epi16(src_reg3, centerpixel_row01);
//  __m256i out_f4_01 = _mm256_madd_epi16(src_reg3, filter_4);
//                   = ((a2-c2)*fc4+(e2-c2)*fc4) (a3-c3)*fc4+(e3-c3)*fc4) .. |
//                   (b2-d2)*fc4 +(f2-d2)*fc4) . .
// Here, out_f4_01 contains partial output of rows 0 and 1 corresponding to fc4.

void av1_convolve_symmetric_dual_subtract_center_highbd_6plus12tap_avx2(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  const int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t dc_offset = 0;
  if (filter_config->num_pixels % 2) {
    const int dc_offset_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    dc_offset = filter[dc_offset_tap_index];
  }

  // Prepare filter coefficients for dgd buffer 6-tap filtering
  // fc0 fc1 fc2 fc3 fc4 fc5 center_tap x
  __m128i filter_coeff = _mm_loadu_si128((__m128i const *)(filter));
  // Replace the center_tap with derived singleton_tap.
  const __m128i center_tap = _mm_set1_epi16(singleton_tap);
  const __m128i filter_coeff_dgd =
      _mm_blend_epi16(filter_coeff, center_tap, 0x40);

  // Prepare filter coefficients for dgd_dual buffer 12-tap filtering
  // Currently we only support the case without a DC coefficient, so that the
  // dual filter taps are at indices:
  // fc6 fc7 fc8 fc9 fc10 fc11 fc12 fc13 | fc14 fc15 fc16 fc17 x x x x
  assert(filter_config->num_pixels == 12);
  const __m128i filter_coeff_dual_lo =
      _mm_loadu_si128((__m128i const *)(filter + 6));
  const __m128i filter_coeff_dual_hi =
      _mm_bsrli_si128(_mm_loadu_si128((__m128i const *)(filter + 10)), 8);

  // Initialize the output registers with zero
  __m256i accum_out_r0r1 = _mm256_setzero_si256();
  __m256i accum_out_r2r3 = _mm256_setzero_si256();

  // 6-tap filtering for dgd (first) buffer
  apply_6tap_filtering(dgd, dgd_stride, filter_coeff_dgd, &accum_out_r0r1,
                       &accum_out_r2r3, block_row_begin, block_col_begin);

  // 6-tap filtering for dgd_dual (second) buffer
  apply_asym_12tap_filtering(dgd_dual, dgd_dual_stride, filter_coeff_dual_lo,
                             filter_coeff_dual_hi, &accum_out_r0r1,
                             &accum_out_r2r3, block_row_begin, block_col_begin);

  // Offset addition
  const __m128i offset_reg = _mm_set1_epi32(dc_offset);
  const __m256i ofs = _mm256_inserti128_si256(
      _mm256_castsi128_si256(offset_reg), offset_reg, 1);
  accum_out_r0r1 = _mm256_add_epi32(accum_out_r0r1, ofs);
  accum_out_r2r3 = _mm256_add_epi32(accum_out_r2r3, ofs);

  // Store the output after rounding and clipping
  round_and_store_avx2(dst, dst_stride, filter_config, bit_depth,
                       accum_out_r0r1, accum_out_r2r3, block_row_begin,
                       block_col_begin);
}

// TODO(rachelbarker): Standardize on non-subtract-center code path,
// so that this one can be removed
void av1_convolve_symmetric_dual_subtract_center_highbd_avx2(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(filter_config->subtract_center);

  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;
  const int num_sym_taps = filter_config->num_pixels / 2;
  const int num_taps_dual = filter_config->num_pixels2;

  // Dispatch to the appropriate SIMD function for the selected filter shape
  // and block size. If none are applicable, fall back to C
  if (num_rows == 4 && num_cols == 4 && num_sym_taps == 6 &&
      num_taps_dual == 12 &&
      (filter_config->config == wienerns_simd_config_uv_from_uv ||
       !memcmp(wienerns_simd_config_uv_from_uv, filter_config->config,
               filter_config->num_pixels * 3 *
                   sizeof(filter_config->config[0][0]))) &&
      (filter_config->config == wienerns_simd_config_uv_from_y ||
       !memcmp(wienerns_simd_config_uv_from_y, filter_config->config2,
               filter_config->num_pixels2 * 3 *
                   sizeof(filter_config->config2[0][0])))) {
    av1_convolve_symmetric_dual_subtract_center_highbd_6plus12tap_avx2(
        dgd, dgd_stride, dgd_dual, dgd_dual_stride, filter_config, filter, dst,
        dst_stride, bit_depth, block_row_begin, block_col_begin);
  } else {
    av1_convolve_symmetric_dual_subtract_center_highbd_c(
        dgd, dgd_stride, dgd_dual, dgd_dual_stride, filter_config, filter, dst,
        dst_stride, bit_depth, block_row_begin, block_row_end, block_col_begin,
        block_col_end);
  }
}

/* Computes the gradient across different directions for a given pixel using 3
 * tap filter. The following shows gradient calculation
 * A0 V0 D0
 * H0 C  H1
 * D1 V1 A1
 * At a given pixel position C,
 * Horzontal gradient = H0 - 2*C + H1
 * Vertical gradient = V0 - 2*C + V1
 * Diagonal gradient = D0 - 2*C + D1
 * Anti diagonal gradient = A0 - 2*C + A1
 * Feature lines buffers are updated here by taking the absolute of these
 * gradient information.
 */
void calc_gradient_in_various_directions_avx2(
    int16_t *feature_line_buffers[], int row, int buffer_row,
    const uint16_t *dgd, int dgd_stride, int width, int col_begin, int col_end,
    int feature_length, int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;
  const int16_t *line_buf0 = feature_line_buffers[buffer_row_0];
  const int16_t *line_buf1 = feature_line_buffers[buffer_row_1];
  const int16_t *line_buf2 = feature_line_buffers[buffer_row_2];
  const int16_t *line_buf3 = feature_line_buffers[buffer_row_3];
  const int tot_width = col_end - col_begin;
  // Width for which SIMD is possible due to constrains of odd width.
  const int pos_simd_width = AOMMIN(tot_width, width + 3 - 2);
  const uint16_t *cur_row = dgd + (row * dgd_stride) + col_begin;
  const uint16_t *prev_row = cur_row - dgd_stride;
  const uint16_t *next_row = cur_row + dgd_stride;

  // Process width multiples of 16.
  for (int wd = 0; wd + 16 < pos_simd_width; wd += 16) {
    // p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15 p16
    const __m256i src_p = _mm256_loadu_si256((__m256i const *)(prev_row));
    // c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11 c12 c13 c14 c15 c16
    const __m256i src_c = _mm256_loadu_si256((__m256i const *)(cur_row));
    // n1 n2 n3 n4 n5 n6 n7 n8 n9 n10 n11 n12 n13 n14 n15 n16
    const __m256i src_n = _mm256_loadu_si256((__m256i const *)(next_row));

    // p0 p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15
    const __m256i src_pm1 = _mm256_loadu_si256((__m256i const *)(prev_row - 1));
    // c0 c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11 c12 c13 c14 c15
    const __m256i src_cm1 = _mm256_loadu_si256((__m256i const *)(cur_row - 1));
    // n0 n1 n2 n3 n4 n5 n6 n7 n8 n9 n10 n11 n12 n13 n14 n15
    const __m256i src_nm1 = _mm256_loadu_si256((__m256i const *)(next_row - 1));

    // p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15 p16 p17
    const __m256i src_pp1 = _mm256_loadu_si256((__m256i const *)(prev_row + 1));
    // c2 c3 c4 c5 c6 c7 c8 c9 c10 c11 c12 c13 c14 c15 c16 c17
    const __m256i src_cp1 = _mm256_loadu_si256((__m256i const *)(cur_row + 1));
    // n2 n3 n4 n5 n6 n7 n8 n9 n10 n11 n12 n13 n14 n15 n16 n17
    const __m256i src_np1 = _mm256_loadu_si256((__m256i const *)(next_row + 1));

    // Horizontal Gradient calculation
    const __m256i base_val = _mm256_add_epi16(src_c, src_c);
    const __m256i partial_horz_diff = _mm256_add_epi16(src_cm1, src_cp1);
    const __m256i horz_diff = _mm256_sub_epi16(partial_horz_diff, base_val);
    const __m256i abs_horz_diff = _mm256_abs_epi16(horz_diff);
    _mm256_storeu_si256((__m256i *)(line_buf0 + wd), abs_horz_diff);

    // Vertical Gradient calculation
    const __m256i partial_vert_diff = _mm256_add_epi16(src_p, src_n);
    const __m256i vert_diff = _mm256_sub_epi16(partial_vert_diff, base_val);
    const __m256i abs_vert_diff = _mm256_abs_epi16(vert_diff);
    _mm256_storeu_si256((__m256i *)(line_buf1 + wd), abs_vert_diff);

    // Diagonal Gradient calculation
    const __m256i partial_diag_diff = _mm256_add_epi16(src_pm1, src_np1);
    const __m256i diag_diff = _mm256_sub_epi16(partial_diag_diff, base_val);
    const __m256i abs_diag_diff = _mm256_abs_epi16(diag_diff);
    _mm256_storeu_si256((__m256i *)(line_buf3 + wd), abs_diag_diff);

    // Anti-Diagonal Gradient calculation
    const __m256i partial_anti_diag_diff = _mm256_add_epi16(src_pp1, src_nm1);
    const __m256i anti_diag_diff =
        _mm256_sub_epi16(partial_anti_diag_diff, base_val);
    const __m256i abs_anti_diag_diff = _mm256_abs_epi16(anti_diag_diff);
    _mm256_storeu_si256((__m256i *)(line_buf2 + wd), abs_anti_diag_diff);

    cur_row += 16;
    prev_row += 16;
    next_row += 16;
    buffer_col += 16;
  }

  // Invoke C function to process remaining width.
  const int remaining_width = tot_width - buffer_col;
  if (remaining_width)
    calc_gradient_in_various_directions_c(
        feature_line_buffers, row, buffer_row, dgd, dgd_stride, width,
        buffer_col - 1, col_end, feature_length, buffer_col);
}

void prepare_feature_sum_bufs_avx2(int *feature_sum_buffers[],
                                   int16_t *feature_line_buffers[],
                                   int feature_length, int buffer_row,
                                   int col_begin, int col_end, int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;
  const int16_t *line_buf0 = feature_line_buffers[buffer_row_0];
  const int16_t *line_buf1 = feature_line_buffers[buffer_row_1];
  const int16_t *line_buf2 = feature_line_buffers[buffer_row_2];
  const int16_t *line_buf3 = feature_line_buffers[buffer_row_3];
  int *sum_buf0 = feature_sum_buffers[0];
  int *sum_buf1 = feature_sum_buffers[1];
  int *sum_buf2 = feature_sum_buffers[2];
  int *sum_buf3 = feature_sum_buffers[3];
  const int tot_width = col_end - col_begin;

  // Process width multiples of 8.
  for (int wd = 0; wd + 8 < tot_width; wd += 8) {
    const __m128i line0 = _mm_loadu_si128((__m128i const *)(line_buf0 + wd));
    const __m128i line1 = _mm_loadu_si128((__m128i const *)(line_buf1 + wd));
    const __m128i line2 = _mm_loadu_si128((__m128i const *)(line_buf2 + wd));
    const __m128i line3 = _mm_loadu_si128((__m128i const *)(line_buf3 + wd));
    const __m256i sum0 = _mm256_loadu_si256((__m256i const *)(sum_buf0 + wd));
    const __m256i sum1 = _mm256_loadu_si256((__m256i const *)(sum_buf1 + wd));
    const __m256i sum2 = _mm256_loadu_si256((__m256i const *)(sum_buf2 + wd));
    const __m256i sum3 = _mm256_loadu_si256((__m256i const *)(sum_buf3 + wd));

    const __m256i line00 = _mm256_cvtepi16_epi32(line0);
    const __m256i line11 = _mm256_cvtepi16_epi32(line1);
    const __m256i line22 = _mm256_cvtepi16_epi32(line2);
    const __m256i line33 = _mm256_cvtepi16_epi32(line3);

    const __m256i sub0 = _mm256_sub_epi32(sum0, line00);
    const __m256i sub1 = _mm256_sub_epi32(sum1, line11);
    const __m256i sub2 = _mm256_sub_epi32(sum2, line22);
    const __m256i sub3 = _mm256_sub_epi32(sum3, line33);

    _mm256_storeu_si256((__m256i *)(sum_buf0 + wd), sub0);
    _mm256_storeu_si256((__m256i *)(sum_buf1 + wd), sub1);
    _mm256_storeu_si256((__m256i *)(sum_buf2 + wd), sub2);
    _mm256_storeu_si256((__m256i *)(sum_buf3 + wd), sub3);
    buffer_col += 8;
  }

  // Invoke C function to process remaining width.
  const int remaining_width = tot_width - buffer_col;
  if (remaining_width)
    prepare_feature_sum_bufs_c(feature_sum_buffers, feature_line_buffers,
                               feature_length, buffer_row, 0, remaining_width,
                               buffer_col);
}

static void update_feature_sum_bufs_avx2(int *feature_sum_buffers[],
                                         int16_t *feature_line_buffers[],
                                         int feature_length, int buffer_row,
                                         int col_begin, int col_end,
                                         int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;
  const int16_t *line_buf0 = feature_line_buffers[buffer_row_0];
  const int16_t *line_buf1 = feature_line_buffers[buffer_row_1];
  const int16_t *line_buf2 = feature_line_buffers[buffer_row_2];
  const int16_t *line_buf3 = feature_line_buffers[buffer_row_3];
  int *sum_buf0 = feature_sum_buffers[0];
  int *sum_buf1 = feature_sum_buffers[1];
  int *sum_buf2 = feature_sum_buffers[2];
  int *sum_buf3 = feature_sum_buffers[3];
  const int tot_width = col_end - col_begin;

  for (int wd = 0; wd + 8 < tot_width; wd += 8) {
    const __m128i line0 = _mm_loadu_si128((__m128i const *)(line_buf0 + wd));
    const __m128i line1 = _mm_loadu_si128((__m128i const *)(line_buf1 + wd));
    const __m128i line2 = _mm_loadu_si128((__m128i const *)(line_buf2 + wd));
    const __m128i line3 = _mm_loadu_si128((__m128i const *)(line_buf3 + wd));
    const __m256i fsum0 = _mm256_loadu_si256((__m256i const *)(sum_buf0 + wd));
    const __m256i fsum1 = _mm256_loadu_si256((__m256i const *)(sum_buf1 + wd));
    const __m256i fsum2 = _mm256_loadu_si256((__m256i const *)(sum_buf2 + wd));
    const __m256i fsum3 = _mm256_loadu_si256((__m256i const *)(sum_buf3 + wd));

    const __m256i line00 = _mm256_cvtepi16_epi32(line0);
    const __m256i line11 = _mm256_cvtepi16_epi32(line1);
    const __m256i line22 = _mm256_cvtepi16_epi32(line2);
    const __m256i line33 = _mm256_cvtepi16_epi32(line3);

    const __m256i sub0 = _mm256_add_epi32(fsum0, line00);
    const __m256i sub1 = _mm256_add_epi32(fsum1, line11);
    const __m256i sub2 = _mm256_add_epi32(fsum2, line22);
    const __m256i sub3 = _mm256_add_epi32(fsum3, line33);

    _mm256_storeu_si256((__m256i *)(sum_buf0 + wd), sub0);
    _mm256_storeu_si256((__m256i *)(sum_buf1 + wd), sub1);
    _mm256_storeu_si256((__m256i *)(sum_buf2 + wd), sub2);
    _mm256_storeu_si256((__m256i *)(sum_buf3 + wd), sub3);
    buffer_col += 8;
  }

  // Invoke C function to process remaining width.
  const int remaining_width = tot_width - buffer_col;
  if (remaining_width)
    update_feature_sum_bufs_c(feature_sum_buffers, feature_line_buffers,
                              feature_length, buffer_row, 0, remaining_width,
                              buffer_col);
}

void fill_directional_feature_buffers_highbd_avx2(
    int *feature_sum_buffers[], int16_t *feature_line_buffers[], int row,
    int buffer_row, const uint16_t *dgd, int dgd_stride, int width,
    int feature_lead, int feature_lag) {
  const int feature_length = feature_lead + feature_lag + 1;
  const int col_begin = -feature_lead;
  const int col_end = width + feature_lag;
  int buffer_col = 0;
  // In the below AVX2 functions, total width to be processed is calculated
  // using col_end-col_begin. Hence, this assert is needed for width to be >= 0
  // always.
  assert(col_begin <= col_end);

  // Preparation of feature sum buffers by subtracting the feature line buffers.
  prepare_feature_sum_bufs_avx2(feature_sum_buffers, feature_line_buffers,
                                feature_length, buffer_row, col_begin, col_end,
                                buffer_col);

  // Compute the gradient across different directions.
  calc_gradient_in_various_directions_avx2(
      feature_line_buffers, row, buffer_row, dgd, dgd_stride, width, col_begin,
      col_end, feature_length, buffer_col);

  // Update the feature sum buffers with updated feature line buffers.
  update_feature_sum_bufs_avx2(feature_sum_buffers, feature_line_buffers,
                               feature_length, buffer_row, col_begin, col_end,
                               buffer_col);
}

static const uint8_t shuffle_mask_low[32] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
                                              5, 5, 6, 6, 6, 6, 7, 7, 7, 7 };

static const uint8_t shuffle_mask_high[32] = { 8,  8,  8,  8,  9,  9,  9,  9,
                                               10, 10, 10, 10, 11, 11, 11, 11,
                                               12, 12, 12, 12, 13, 13, 13, 13,
                                               14, 14, 14, 14, 15, 15, 15, 15 };

static AOM_INLINE void set_left_and_right_tskip_sum(int8_t *tskip_sum_buffer,
                                                    int col_begin, int width,
                                                    int col_end,
                                                    int8_t left_tskip,
                                                    int8_t right_tskip) {
  int buffer_col = 0;
  for (int col = col_begin; col < 0; ++col) {
    tskip_sum_buffer[buffer_col] = left_tskip;
    ++buffer_col;
  }

  buffer_col += width;
  for (int col = width; col < col_end; ++col) {
    tskip_sum_buffer[buffer_col] = right_tskip;
    ++buffer_col;
  }
}

void av1_fill_tskip_sum_buffer_avx2(int row, const uint8_t *tskip,
                                    int tskip_stride, int8_t *tskip_sum_buffer,
                                    int width, int height, int tskip_lead,
                                    int tskip_lag, bool use_strict_bounds) {
  if ((width != 64) && (width != 32)) {
    av1_fill_tskip_sum_buffer_c(row, tskip, tskip_stride, tskip_sum_buffer,
                                width, height, tskip_lead, tskip_lag,
                                use_strict_bounds);
    return;
  }
  // TODO(oguleryuz): tskip needs boundary extension.
  assert(use_strict_bounds == true);
  const int col_begin = -tskip_lead;
  const int col_end = width + tskip_lag;

  const int clamped_row = AOMMAX(AOMMIN(row, height - 1), 0);
  const int tskip_id_base_add = (clamped_row >> MI_SIZE_LOG2) * tskip_stride;

  const int tskip_length = tskip_lead + tskip_lag + 1;
  int subtract_row = row - tskip_length;
  int tskip_id_base_sub = 0;

  if (subtract_row >= -tskip_lead) {
    assert(subtract_row <= height - 1);
    subtract_row = subtract_row >= 0 ? subtract_row : 0;
    tskip_id_base_sub = (subtract_row >> MI_SIZE_LOG2) * tskip_stride;
  }

  if (width == 64) {
    __m128i tx_skip_add =
        _mm_loadu_si128((__m128i *)(tskip + tskip_id_base_add));

    if (subtract_row >= -tskip_lead) {
      const __m128i tx_skip_sub =
          _mm_loadu_si128((__m128i *)(tskip + tskip_id_base_sub));
      tx_skip_add = _mm_sub_epi8(tx_skip_add, tx_skip_sub);
    }

    // tx_skip_add
    // a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15
    const __m256i tx_skip_add_temp = _mm256_broadcastsi128_si256(tx_skip_add);
    // tx_skip_add_low
    // a0 a0 a0 a0 a1 a1 a1 a1 a2 a2 a2 a2 a3 a3 a3 a3
    // a4 a4 a4 a4 a5 a5 a5 a5 a6 a6 a6 a6 a7 a7 a7 a7
    const __m256i tx_skip_add_low = _mm256_shuffle_epi8(
        tx_skip_add_temp, _mm256_loadu_si256((__m256i *)shuffle_mask_low));
    // tx_skip_add_high
    // a8  a8  a8  a8  a9  a9  a9  a9  a10 a10 a10 a10 a11 a11 a11 a11
    // a12 a12 a12 a12 a13 a13 a13 a13 a14 a14 a14 a14 a15 a15 a15 a15
    const __m256i tx_skip_add_high = _mm256_shuffle_epi8(
        tx_skip_add_temp, _mm256_loadu_si256((__m256i *)shuffle_mask_high));

    const __m256i tskip_0 =
        _mm256_loadu_si256((__m256i *)(tskip_sum_buffer - col_begin));
    const __m256i tskip_1 =
        _mm256_loadu_si256((__m256i *)(tskip_sum_buffer - col_begin + 32));

    const __m256i tskip_sum_low = _mm256_add_epi8(tskip_0, tx_skip_add_low);
    const __m256i tskip_sum_high = _mm256_add_epi8(tskip_1, tx_skip_add_high);

    _mm256_storeu_si256((__m256i *)(tskip_sum_buffer - col_begin),
                        tskip_sum_low);
    _mm256_storeu_si256((__m256i *)(tskip_sum_buffer - col_begin + 32),
                        tskip_sum_high);

    // Extend data from [col_begin, 0) and [width, col_end).
    const int8_t left_tskip = _mm256_extract_epi8(tskip_sum_low, 0);
    if (col_begin == -1 && (col_end == (width + 4))) {
      *((int8_t *)tskip_sum_buffer) = left_tskip;
      int32_t col_end_value = _mm256_extract_epi32(tskip_sum_high, 7);
      memcpy((tskip_sum_buffer - col_begin + width), &col_end_value,
             sizeof(int32_t));
    } else {
      const int8_t right_tskip = _mm256_extract_epi8(tskip_sum_high, 31);
      set_left_and_right_tskip_sum(tskip_sum_buffer, col_begin, width, col_end,
                                   left_tskip, right_tskip);
    }
  } else if (width == 32) {
    __m128i tx_skip_add =
        _mm_loadl_epi64((__m128i *)(tskip + tskip_id_base_add));

    if (subtract_row >= -tskip_lead) {
      const __m128i tx_skip_sub =
          _mm_loadl_epi64((__m128i *)(tskip + tskip_id_base_sub));
      tx_skip_add = _mm_sub_epi8(tx_skip_add, tx_skip_sub);
    }

    const __m256i tx_skip_add_temp = _mm256_broadcastsi128_si256(tx_skip_add);
    // tx_skip_add_low
    // a0 a0 a0 a0 a1 a1 a1 a1 a2 a2 a2 a2 a3 a3 a3 a3
    // a4 a4 a4 a4 a5 a5 a5 a5 a6 a6 a6 a6 a7 a7 a7 a7
    const __m256i tx_skip_add_low = _mm256_shuffle_epi8(
        tx_skip_add_temp, _mm256_loadu_si256((__m256i *)shuffle_mask_low));

    const __m256i tskip_0 =
        _mm256_loadu_si256((__m256i *)(tskip_sum_buffer - col_begin));
    const __m256i tskip_sum_low = _mm256_add_epi8(tskip_0, tx_skip_add_low);

    _mm256_storeu_si256((__m256i *)(tskip_sum_buffer - col_begin),
                        tskip_sum_low);

    // Extend data from [col_begin, 0) and [width, col_end).
    const int8_t left_tskip = _mm256_extract_epi8(tskip_sum_low, 0);
    const int8_t right_tskip = _mm256_extract_epi8(tskip_sum_low, 31);
    if (col_begin == -1 && (col_end == (width + 1))) {
      *((int8_t *)tskip_sum_buffer) = left_tskip;
      *((int8_t *)(tskip_sum_buffer - col_begin + width)) = right_tskip;
    } else {
      set_left_and_right_tskip_sum(tskip_sum_buffer, col_begin, width, col_end,
                                   left_tskip, right_tskip);
    }
  }
}

#define LOAD_INPUT_DATA(cb, cl)                      \
  /*A0 A1 A2 A3 A4 A5 A6 A7*/                        \
  src_A = _mm256_loadu_si256((__m256i *)(lb0 + cb)); \
  /* B0 B1 B2 B3 B4 B5 B6 B7 */                      \
  src_B = _mm256_loadu_si256((__m256i *)(lb1 + cb)); \
  /* C0 C1 C2 C3 C4 C5 C6 C7 */                      \
  src_C = _mm256_loadu_si256((__m256i *)(lb2 + cb)); \
  /* D0 D1 D2 D3 D4 D5 D6 D7 */                      \
  src_D = _mm256_loadu_si256((__m256i *)(lb3 + cb)); \
  /* a0 a1 a2 a3 a4 a5 a6 a7 */                      \
  src_a = _mm256_loadu_si256((__m256i *)(lb0 + cl)); \
  /* b0 b1 b2 b3 b4 b5 b6 b7*/                       \
  src_b = _mm256_loadu_si256((__m256i *)(lb1 + cl)); \
  /* c0 c1 c2 c3 c4 c5 c6 c7 */                      \
  src_c = _mm256_loadu_si256((__m256i *)(lb2 + cl)); \
  /* d0 d1 d2 d3 d4 d5 d6 d7 */                      \
  src_d = _mm256_loadu_si256((__m256i *)(lb3 + cl));

static AOM_INLINE void process_feature_accum_wd8(__m256i src_A, __m256i src_B,
                                                 __m256i src_C, __m256i src_D,
                                                 __m256i src_a, __m256i src_b,
                                                 __m256i src_c, __m256i src_d,
                                                 __m256i *result) {
  // ra0 ra1 ra2 ra3 ra4 ra5 ra6 ra7
  const __m256i diff_Aa = _mm256_sub_epi32(src_A, src_a);
  // rb0 rb1 rb2 rb3 rb4 rb5 rb6 rb7
  const __m256i diff_Bb = _mm256_sub_epi32(src_B, src_b);
  // rc0 rc1 rc2 rc3 rc4 rc5 rc6 rc7
  const __m256i diff_Cc = _mm256_sub_epi32(src_C, src_c);
  // rd0 rd1 rd2 rd3 rd4 rd5 rd6 rd7
  const __m256i diff_Dd = _mm256_sub_epi32(src_D, src_d);
  // ra0+ra1 ra2+ra3 rb0+rb1 rb2+rb3 ra4+ra5 ra6+ra7 rb4+rb5 rb6+rb7
  const __m256i result0 = _mm256_hadd_epi32(diff_Aa, diff_Bb);
  // rc0+rc1 rc2+rc3 rd0+rd1 rd2+rd3 rc4+rc5 rc6+rc7 rd4+rd5 rd6+rd7
  const __m256i result1 = _mm256_hadd_epi32(diff_Cc, diff_Dd);
  // ra0+ra1+ra2+ra3 rb0+rb1+rb2+rb3 rc0+rc1+rc2+rc3 rd0+rd1+rd2+rd3  --
  // ra4+ra5+ra6+ra7 rb4+rb5+rb6+rb7 rc4+rc5+rc6+rc7 rd4+rd5+rd6+rd7
  *result = _mm256_hadd_epi32(result0, result1);
}

static AOM_INLINE void unpack_results_and_store(
    int dir_feature_accum[][PC_WIENER_FEATURE_ACC_SIZE], __m256i result0,
    __m256i result1, int cur_col) {
  const int cur_idx = cur_col / 4;
  const int *sb0 = dir_feature_accum[0];
  const int *sb1 = dir_feature_accum[1];
  const int *sb2 = dir_feature_accum[2];
  const int *sb3 = dir_feature_accum[3];

  const __m128i accumu_r0 = _mm_set1_epi32(dir_feature_accum[0][cur_idx]);
  const __m128i accumu_r1 = _mm_set1_epi32(dir_feature_accum[1][cur_idx]);
  const __m128i accumu_r2 = _mm_set1_epi32(dir_feature_accum[2][cur_idx]);
  const __m128i accumu_r3 = _mm_set1_epi32(dir_feature_accum[3][cur_idx]);

  // ra0+ra1+ra2+ra3 rb0+rb1+rb2+rb3 rc0+rc1+rc2+rc3 rd0+rd1+rd2+rd3
  const __m128i f1 = _mm256_castsi256_si128(result0);
  // ra4+ra5+ra6+ra7 rb4+rb5+rb6+rb7 rc4+rc5+rc6+rc7 rd4+rd5+rd6+rd7
  const __m128i fresult01 = _mm256_extractf128_si256(result0, 1);
  // a0-a7 b0-b7 c0-c7 d0-d7
  const __m128i f2 = _mm_add_epi32(f1, fresult01);
  // ra8+ra9+ra10+ra11 rb8+rb9+rb10+rb11 rc8+rc9+rc10+rc11 rd8+rd9+rd10+rd11
  const __m128i fresult10 = _mm256_castsi256_si128(result1);
  // a0-a11 b0-b11 c0-c11 d0-d11
  const __m128i f3 = _mm_add_epi32(f2, fresult10);
  // ra12+ra13+ra14+ra15 rb12+rb13+rb14+rb15 rc12+rc13+rc14+rc15
  // rd12+rd13+rd14+rd15
  const __m128i fresult11 = _mm256_extractf128_si256(result1, 1);
  // a0-a15 b0-b15 c0-c15 d0-d15
  const __m128i f4 = _mm_add_epi32(f3, fresult11);

  // ra0-ra3 ra0-ra7 rb0-rb3 rb0-rb7
  const __m128i r00 = _mm_unpacklo_epi32(f1, f2);
  // rc0-rc3 rc0-rc7 rd0-rd3 rd0-rd7
  const __m128i r20 = _mm_unpackhi_epi32(f1, f2);
  // ra0-ra11  ra0-ra15 rb0-rb11 rb0-rb15
  const __m128i r01 = _mm_unpacklo_epi32(f3, f4);
  // rc0-rc11  rc0-rc15 rd0-rd11 rd0-rd15
  const __m128i r21 = _mm_unpackhi_epi32(f3, f4);
  // ra0-ra3 ra0-ra7 ra0-ra11  ra0-ra15
  __m128i r0 = _mm_unpacklo_epi64(r00, r01);
  // rb0-rb3 rb0-rb7 rb0-rb11  rb0-rb15
  __m128i r1 = _mm_unpackhi_epi64(r00, r01);
  // rc0-rc3 rc0-rc7 rc0-rc11  rc0-rc15
  __m128i r2 = _mm_unpacklo_epi64(r20, r21);
  // rd0-rd3 rd0-rd7 rd0-rd11  rd0-rd15
  __m128i r3 = _mm_unpackhi_epi64(r20, r21);

  r0 = _mm_add_epi32(r0, accumu_r0);
  r1 = _mm_add_epi32(r1, accumu_r1);
  r2 = _mm_add_epi32(r2, accumu_r2);
  r3 = _mm_add_epi32(r3, accumu_r3);

  _mm_storeu_si128((__m128i *)(sb0 + cur_idx + 1), r0);
  _mm_storeu_si128((__m128i *)(sb1 + cur_idx + 1), r1);
  _mm_storeu_si128((__m128i *)(sb2 + cur_idx + 1), r2);
  _mm_storeu_si128((__m128i *)(sb3 + cur_idx + 1), r3);
}

static AOM_INLINE void process_feature_accum_wd16(
    int dir_feature_accum[][PC_WIENER_FEATURE_ACC_SIZE], int *feature_sum_buf[],
    int cur_col, int cb, int feature_length) {
  const int cl = cb - feature_length;
  const int *lb0 = feature_sum_buf[0];
  const int *lb1 = feature_sum_buf[1];
  const int *lb2 = feature_sum_buf[2];
  const int *lb3 = feature_sum_buf[3];
  __m256i src_A, src_B, src_C, src_D, src_a, src_b, src_c, src_d;

  // Process the first 8 pixels.
  LOAD_INPUT_DATA(cb, cl)
  __m256i result0 = _mm256_set1_epi16(0);
  process_feature_accum_wd8(src_A, src_B, src_C, src_D, src_a, src_b, src_c,
                            src_d, &result0);

  // Process the next 8 pixels.
  LOAD_INPUT_DATA(cb + 8, cl + 8)
  __m256i result1 = _mm256_set1_epi16(0);
  process_feature_accum_wd8(src_A, src_B, src_C, src_D, src_a, src_b, src_c,
                            src_d, &result1);

  unpack_results_and_store(dir_feature_accum, result0, result1, cur_col);
}

static AOM_INLINE void process_feature_accum_wd15(
    int dir_feature_accum[][PC_WIENER_FEATURE_ACC_SIZE], int *feature_sum_buf[],
    int cur_col, int cb, int feature_length) {
  const int cl = cb - feature_length;
  const int *lb0 = feature_sum_buf[0];
  const int *lb1 = feature_sum_buf[1];
  const int *lb2 = feature_sum_buf[2];
  const int *lb3 = feature_sum_buf[3];
  __m256i src_A, src_B, src_C, src_D, src_a, src_b, src_c, src_d;

  // Process the first 8 pixels.
  LOAD_INPUT_DATA(cb, cl);
  __m256i result0 = _mm256_set1_epi16(0);
  process_feature_accum_wd8(src_A, src_B, src_C, src_D, src_a, src_b, src_c,
                            src_d, &result0);

  // Process the next 8 pixels.
  LOAD_INPUT_DATA(cb + 8, cl + 8);
  const __m256i zero = _mm256_setzero_si256();
  src_A = _mm256_blend_epi32(src_A, zero, 0x80);
  src_B = _mm256_blend_epi32(src_B, zero, 0x80);
  src_C = _mm256_blend_epi32(src_C, zero, 0x80);
  src_D = _mm256_blend_epi32(src_D, zero, 0x80);
  src_a = _mm256_blend_epi32(src_a, zero, 0x80);
  src_b = _mm256_blend_epi32(src_b, zero, 0x80);
  src_c = _mm256_blend_epi32(src_c, zero, 0x80);
  src_d = _mm256_blend_epi32(src_d, zero, 0x80);
  __m256i result1 = zero;
  process_feature_accum_wd8(src_A, src_B, src_C, src_D, src_a, src_b, src_c,
                            src_d, &result1);

  unpack_results_and_store(dir_feature_accum, result0, result1, cur_col);
}

void av1_fill_directional_feature_accumulators_avx2(
    int dir_feature_accum[NUM_PC_WIENER_FEATURES][PC_WIENER_FEATURE_ACC_SIZE],
    int *feature_sum_buf[NUM_PC_WIENER_FEATURES], int width, int col_offset,
    int feature_lead, int feature_lag) {
  // SIMD is specifically implemented for pc_wiener block size equal to 4x4 and
  // restoration unit size must be 64x64 for luma or 32x32 for chroma plane.
  if ((PC_WIENER_BLOCK_SIZE != 4) || ((width != 64) && (width != 32))) {
    // TODO(oguleryuz): Reduce the cases where we punt to c-code.
    av1_fill_directional_feature_accumulators_c(
        dir_feature_accum, feature_sum_buf, width, col_offset, feature_lead,
        feature_lag);
    return;
  }

  // Process width equal to 0 case here.
  int cur_col = 0;
  const int feature_length = feature_lead + feature_lag + 1;
  int cb = cur_col + col_offset + feature_lead;
  for (int k = 0; k < NUM_PC_WIENER_FEATURES; k++) {
    dir_feature_accum[k][0] += feature_sum_buf[k][cb];
  }
  ++cb;

  // Process the remaining width here.
  if (width == 64) {
    // Process next 16 (i.e., 1 to 16) pixels here.
    cur_col = 1;
    process_feature_accum_wd16(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);

    // Process next 16 (i.e., 17 to 32) pixels here.
    cur_col += 16;
    cb += 16;
    process_feature_accum_wd16(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);

    // Process next 16 (i.e., 33 to 48) pixels here.
    cur_col += 16;
    cb += 16;
    process_feature_accum_wd16(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);

    // Process remaining 15 (i.e., 49 to 63) pixels here.
    cur_col += 16;
    cb += 16;
    process_feature_accum_wd15(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);
  } else if (width == 32) {
    // Process next 16 pixels here.
    cur_col = 1;
    process_feature_accum_wd16(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);

    // Process remaining 15 pixels here.
    cur_col += 16;
    cb += 16;
    process_feature_accum_wd15(dir_feature_accum, feature_sum_buf, cur_col, cb,
                               feature_length);
  } else {
    // For any other case, C support is added. So this assert should not be
    // invoked.
    assert(0);
  }
}

// To make last 8bit element of 256bit register to zero.
static const int8_t blend_mask[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, -1 };

static AOM_INLINE void process_accumulation_wd32(const __m256i src_A,
                                                 const __m256i src_a,
                                                 const __m128i accum_reg,
                                                 int16_t *tskip_out) {
  // d1 - - - - d32
  const __m256i diff_Aa = _mm256_sub_epi8(src_A, src_a);
  const __m256i diff0 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(diff_Aa));
  const __m256i diff1 =
      _mm256_cvtepi8_epi16(_mm256_extractf128_si256(diff_Aa, 1));
  // d1+d2 d3+d4 d5+d6 d7+d8 d17+d18 d19+d20 d21+d22 d23+d24 |
  // d9+d10 d11+d12 d13+d14 d15+d16 d25+d26 d27+d28 d29+d30 d31+d32
  const __m256i r1 = _mm256_hadd_epi16(diff0, diff1);
  // d1+d2 d3+d4 d5+d6 d7+d8 d9+d10 d11+d12 d13+d14 d15+d16 |
  // d17+d18 d19+d20 d21+d22 d23+d24 d25+d26 d27+d28 d29+d30 d31+d32
  __m256i r2 = _mm256_permute4x64_epi64(r1, 0xd8);
  // sum of: d1d4 d5d8 d9d12 d13d16 d17d20 d21d24 d25d28 d29d32
  r2 = _mm256_hadd_epi16(
      r2, _mm256_castsi128_si256(_mm256_extractf128_si256(r2, 1)));
  // sum of: d1d4 d5d8 d9d12 d13d16 d17d20 d21d24 d25d28 d29d32
  // const __m256i r2 = _mm256_permute4x64_epi64(r1, 0x54);
  // d1d4 d5d8  d9d12 d13d16 d17d20 d21d24 d25d28 d29d32 | x
  const __m128i low128 = _mm256_castsi256_si128(r2);
  //  0   d1d4  d5d8   d9d12  d13d16 d17d20 d21d24 d25d28
  __m128i tmp_shift = _mm_bslli_si128(low128, 2);
  // d1d4 d1d8  d5d12  d9d16  d13d20 d17d24 d21d28 d25d32
  const __m128i result0 = _mm_add_epi16(low128, tmp_shift);
  //   0    0   d1d4   d1d8   d5d12  d9d16  d13d20 d17d24
  tmp_shift = _mm_bslli_si128(result0, 4);
  // d1d4 d1d8  d1d12  d1d16  d5d20  d9d24  d13d28 d17d32
  const __m128i result1 = _mm_add_epi16(result0, tmp_shift);
  //  0     0     0      0    d1d4   d1d8   d1d12  d1d16
  tmp_shift = _mm_bslli_si128(result1, 8);
  // d1d4 d1d8  d1d12  d1d16  d1d20  d1d24  d1d28 d1d32
  const __m128i result2 = _mm_add_epi16(result1, tmp_shift);
  // Add the 0th result to all the values
  const __m128i result = _mm_add_epi16(accum_reg, result2);
  _mm_storeu_si128((__m128i *)(tskip_out), result);
}

void av1_fill_tskip_feature_accumulator_avx2(
    int16_t tskip_feature_accum[PC_WIENER_FEATURE_ACC_SIZE],
    int8_t *tskip_sum_buf, int width, int col_offset, int tskip_lead,
    int tskip_lag) {
  // SIMD is specifically implemented for pc_wiener block size equal to 4x4 and
  // restoration unit size must be 64x64 for luma or 32x32 for chroma plane.
  if ((PC_WIENER_BLOCK_SIZE != 4) || ((width != 64) && (width != 32))) {
    // TODO(oguleryuz): Reduce the cases where we punt to c-code.
    av1_fill_tskip_feature_accumulator_c(tskip_feature_accum, tskip_sum_buf,
                                         width, col_offset, tskip_lead,
                                         tskip_lag);
    return;
  }

  // Process pixel 0 case here.
  int col_base = col_offset + tskip_lead;
  const int tskip_length = tskip_lead + tskip_lag + 1;
  assert(col_base >= 0);
  tskip_feature_accum[0] += tskip_sum_buf[col_base];
  col_base++;
  int cl = col_base - tskip_length;

  // Process the remaining width here.
  if (width == 64) {
    // Processing of the next 32 pixels.
    const __m256i src_A =
        _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + col_base));
    const __m256i src_a =
        _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + cl));
    __m128i accum_reg = _mm_set1_epi16(tskip_feature_accum[0]);
    process_accumulation_wd32(src_A, src_a, accum_reg, &tskip_feature_accum[1]);

    // Process the remianing 31 pixels here. Eventhough 31 pixels are processed
    // here, load 32 here and make the last 8bit of 256 bit register to zero.
    // Load will not overflow as enough elements will be there in the
    // tskip_sum_buffer.
    col_base += 32;
    cl = col_base - tskip_length;
    __m256i src_B =
        _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + col_base));
    __m256i src_b = _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + cl));
    const __m256i blend_reg = _mm256_loadu_si256((__m256i *)blend_mask);
    const __m256i zero = _mm256_setzero_si256();
    src_B = _mm256_blendv_epi8(src_B, zero, blend_reg);
    src_b = _mm256_blendv_epi8(src_b, zero, blend_reg);
    accum_reg = _mm_set1_epi16(tskip_feature_accum[8]);
    process_accumulation_wd32(src_B, src_b, accum_reg, &tskip_feature_accum[9]);
  } else if (width == 32) {
    // Process the remianing 31 pixels here. Eventhough 31 pixels are processed
    // here, load 32 here and make the last 8bit of 256 bit register to zero.
    // Load will not overflow as enough elements will be there in the
    // tskip_sum_buffer.
    __m256i src_A =
        _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + col_base));
    __m256i src_a = _mm256_loadu_si256((__m256i const *)(tskip_sum_buf + cl));
    const __m256i blend_reg = _mm256_loadu_si256((__m256i *)blend_mask);
    const __m256i zero = _mm256_setzero_si256();
    __m128i accum_reg = _mm_set1_epi16(tskip_feature_accum[0]);

    src_A = _mm256_blendv_epi8(src_A, zero, blend_reg);
    src_a = _mm256_blendv_epi8(src_a, zero, blend_reg);
    process_accumulation_wd32(src_A, src_a, accum_reg, &tskip_feature_accum[1]);
  } else {
    // For any other case, C support is added. So this assert should not be
    // invoked.
    assert(0);
  }
}
