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

#include "aom_dsp/x86/convolve_sse4_1.h"
#include "av1/common/warped_motion.h"

#if CONFIG_EXT_WARP_FILTER
DECLARE_ALIGNED(32, static const uint8_t,
                shuffle_pattern_0[32]) = { 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6,
                                           7, 6, 7, 8, 9, 0, 1, 2, 3, 2, 3,
                                           4, 5, 4, 5, 6, 7, 6, 7, 8, 9 };

DECLARE_ALIGNED(32, static const uint8_t, shuffle_pattern_1[32]) = {
  4, 5, 6, 7, 6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13,
  4, 5, 6, 7, 6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_pattern_2[32]) = {
  6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13, 12, 13, 14, 15,
  6, 7, 8, 9, 8, 9, 10, 11, 10, 11, 12, 13, 12, 13, 14, 15
};
#endif  // CONFIG_EXT_WARP_FILTER

DECLARE_ALIGNED(32, static const uint8_t, warp_highbd_shuffle_pattern[32]) = {
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
  0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

DECLARE_ALIGNED(32, static const uint8_t, warp_highbd_arrange_bytes[32]) = {
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
  0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_input_mask[32]) = {
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
  0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_alpha0_gamma0_mask0[32]) = {
  0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_alpha0_gamma0_mask1[32]) = {
  4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7,
  4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_alpha0_gamma0_mask2[32]) = {
  8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11,
  8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11
};

DECLARE_ALIGNED(32, static const uint8_t, shuffle_alpha0_gamma0_mask3[32]) = {
  12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15,
  12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15
};

static INLINE void prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(
    int32_t s, __m256i *coeffs) {
  // c0 c1 c2 c3 c4 c5 c6 c7 | c0 c1 c2 c3 c4 c5 c6 c7
  const __m256i v_coeff = _mm256_broadcastsi128_si256(_mm_loadu_si128(
      (__m128i *)av1_warped_filter[(s) >> WARPEDDIFF_PREC_BITS]));

  coeffs[0] = _mm256_shuffle_epi8(
      v_coeff, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask0));
  coeffs[1] = _mm256_shuffle_epi8(
      v_coeff, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask1));
  coeffs[2] = _mm256_shuffle_epi8(
      v_coeff, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask2));
  coeffs[3] = _mm256_shuffle_epi8(
      v_coeff, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask3));
}

static INLINE void prepare_8tap_filter_coeffs_avx2(int32_t s, int offset,
                                                   __m256i *coeffs) {
  // c00 c01 c02 c03 c04 c05 c06 c07 | x x x x x x x x
  const __m256i v_coeff0 = _mm256_castsi128_si256(_mm_loadu_si128(
      (__m128i *)av1_warped_filter[(s) >> WARPEDDIFF_PREC_BITS]));
  // c00 c01 c02 c03 c04 c05 c06 c07 | c10 c11 c12 c13 c14 c15 c16 c17
  const __m256i v_coeff01 = _mm256_inserti128_si256(
      v_coeff0,
      _mm_loadu_si128(
          (__m128i *)av1_warped_filter[(s + offset) >> WARPEDDIFF_PREC_BITS]),
      1);
  // c20 c21 c22 c23 c24 c25 c26 c27 | x x x x x x x x
  const __m256i v_coeff2 = _mm256_castsi128_si256(_mm_loadu_si128(
      (__m128i *)av1_warped_filter[(s + 2 * offset) >> WARPEDDIFF_PREC_BITS]));
  // c20 c21 c22 c23 c24 c25 c26 c27 | c30 c31 c32 c33 c34 c35 c36 c37
  const __m256i v_coeff23 = _mm256_inserti128_si256(
      v_coeff2,
      _mm_loadu_si128(
          (__m128i *)
              av1_warped_filter[(s + 3 * offset) >> WARPEDDIFF_PREC_BITS]),
      1);
  // c40 c41 c42 c43 c44 c45 c46 c47 | x x x x x x x x
  const __m256i v_coeff4 = _mm256_castsi128_si256(_mm_loadu_si128(
      (__m128i *)av1_warped_filter[(s + 4 * offset) >> WARPEDDIFF_PREC_BITS]));
  // c40 c41 c42 c43 c44 c45 c46 c47 | c50 c51 c52 c53 c54 c55 c56 c57
  const __m256i v_coeff45 = _mm256_inserti128_si256(
      v_coeff4,
      _mm_loadu_si128(
          (__m128i *)
              av1_warped_filter[(s + 5 * offset) >> WARPEDDIFF_PREC_BITS]),
      1);
  // c60 c61 c62 c63 c64 c65 c66 c67 | x x x x x x x x
  const __m256i v_coeff6 = _mm256_castsi128_si256(_mm_loadu_si128(
      (__m128i *)av1_warped_filter[(s + 6 * offset) >> WARPEDDIFF_PREC_BITS]));
  // c60 c61 c62 c63 c64 c65 c66 c67 | c70 c71 c72 c73 c74 c75 c76 c77
  const __m256i v_coeff67 = _mm256_inserti128_si256(
      v_coeff6,
      _mm_loadu_si128(
          (__m128i *)
              av1_warped_filter[(s + 7 * offset) >> WARPEDDIFF_PREC_BITS]),
      1);

  __m256i v_c0123 = _mm256_unpacklo_epi32(v_coeff01, v_coeff23);
  __m256i v_c0123u = _mm256_unpackhi_epi32(v_coeff01, v_coeff23);
  __m256i v_c4567 = _mm256_unpacklo_epi32(v_coeff45, v_coeff67);
  __m256i v_c4567u = _mm256_unpackhi_epi32(v_coeff45, v_coeff67);

  // c00 c01 c20 c21 c40 c41 c60 c61 | c10 c11 c30 c31 c50 c51 c70 c71
  coeffs[0] = _mm256_unpacklo_epi64(v_c0123, v_c4567);
  // c02 c03 c22 c23 c06 c07 c26 c27 | c12 c13 c32 c33 c16 c17 c72 c73
  coeffs[1] = _mm256_unpackhi_epi64(v_c0123, v_c4567);
  // c04 c05 c24 c25 c44 c45 c64 c65 | c14 c15 c34 c35 c54 c55 c74 c75
  coeffs[2] = _mm256_unpacklo_epi64(v_c0123u, v_c4567u);
  // c06 c07 c26 c27 c46 c47 c66 c67 | c16 c17 c36 c37 c56 c57 c76 c77
  coeffs[3] = _mm256_unpackhi_epi64(v_c0123u, v_c4567u);
}

static INLINE void load_horiz_src_pixels_avx2(const uint16_t *ref, __m256i *r) {
  r[0] = _mm256_loadu_si256((__m256i *)ref);
  r[1] = _mm256_loadu_si256((__m256i *)(ref + 1));
}

static INLINE void prepare_8tap_horiz_src_padded_avx2(const uint16_t *ref,
                                                      int out_of_boundary_left,
                                                      int out_of_boundary_right,
                                                      __m256i *src_padded) {
  const __m256i src_0 = _mm256_loadu_si256((__m256i *)ref);

  const __m256i src_01 = _mm256_shuffle_epi8(
      src_0, _mm256_loadu_si256((__m256i *)warp_highbd_arrange_bytes));
  __m256i src_reg = _mm256_permute4x64_epi64(src_01, 0xD8);

  if (out_of_boundary_left >= 0) {
    const __m128i shuffle_left =
        _mm_loadu_si128((__m128i *)warp_pad_left[out_of_boundary_left]);
    const __m256i shuffle_reg_left = _mm256_broadcastsi128_si256(shuffle_left);
    src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_left);
  }

  if (out_of_boundary_right >= 0) {
    const __m128i shuffle_right =
        _mm_loadu_si128((__m128i *)warp_pad_right[out_of_boundary_right]);
    const __m256i shuffle_reg_right =
        _mm256_broadcastsi128_si256(shuffle_right);
    src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_right);
  }

  src_padded[0] = _mm256_shuffle_epi8(
      _mm256_permute4x64_epi64(src_reg, 0xD8),
      _mm256_loadu_si256((__m256i *)warp_highbd_shuffle_pattern));
  __m256i src_padded_hi = _mm256_permute4x64_epi64(src_padded[0], 0xEE);
  src_padded[1] = _mm256_alignr_epi8(src_padded_hi, src_padded[0], 2);
}

static INLINE void prepare_8tap_horiz_src_avx2(const __m256i *input,
                                               __m256i *src) {
  // r0 r1 r2 r3 r4 r5 r6 r7 | r1 r2 r3 r4 r5 r6 r7 r8
  const __m256i r_low = _mm256_permute2x128_si256(input[0], input[1], 0x20);
  // r8 r9 r10 r11 r12 r13 r14 r15 | r9 r10 r11 r12 r13 r14 r15 r16
  const __m256i r_high = _mm256_permute2x128_si256(input[0], input[1], 0x31);

  // r0 r1 r2 r3 r4 r5 r6 r7 | r1 r2 r3 r4 r5 r6 r7 r8
  src[0] = r_low;
  // r2 r3 r4 r5 r6 r7 r8 r9 | r3 r4 r5 r6 r7 r8 r9 r10
  src[1] = _mm256_alignr_epi8(r_high, r_low, 4);
  // r4 r5 r6 r7 r8 r9 r10 r11 | r5 r6 r7 r8 r9 r10 r11 r12
  src[2] = _mm256_alignr_epi8(r_high, r_low, 8);
  // r6 r7 r8 r9 r10 r11 r12 r13 | r7 r8 r9 r10 r11 r12 r13 r14
  src[3] = _mm256_alignr_epi8(r_high, r_low, 12);
}

static INLINE void filter_src_pixels_horiz_avx2(const __m256i *in,
                                                const __m256i *coeffs,
                                                const __m256i *offset,
                                                int shift, __m256i *out) {
  const __m256i res_0 = _mm256_madd_epi16(in[0], coeffs[0]);
  const __m256i res_1 = _mm256_madd_epi16(in[1], coeffs[1]);
  const __m256i res_2 = _mm256_madd_epi16(in[2], coeffs[2]);
  const __m256i res_3 = _mm256_madd_epi16(in[3], coeffs[3]);

  const __m256i res_4 = _mm256_add_epi32(
      res_0, _mm256_add_epi32(_mm256_add_epi32(res_2, res_3), res_1));
  const __m256i res = _mm256_add_epi32(res_4, *offset);
  *out = _mm256_srai_epi32(res, shift);
}

static INLINE void prepare_8tap_vert_src_avx2(const __m256i *input,
                                              __m256i *src) {
  __m256i input_01 = _mm256_packus_epi32(input[0], input[1]);
  __m256i input_23 = _mm256_packus_epi32(input[2], input[3]);
  __m256i input_45 = _mm256_packus_epi32(input[4], input[5]);

  __m256i input_12 = _mm256_packus_epi32(input[1], input[2]);
  __m256i input_34 = _mm256_packus_epi32(input[3], input[4]);
  __m256i input_56 = _mm256_packus_epi32(input[5], input[6]);

  src[0] = _mm256_shuffle_epi8(
      input_01, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[1] = _mm256_shuffle_epi8(
      input_23, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[2] = _mm256_shuffle_epi8(
      input_45, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[4] = _mm256_shuffle_epi8(
      input_12, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[5] = _mm256_shuffle_epi8(
      input_34, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[6] = _mm256_shuffle_epi8(
      input_56, _mm256_load_si256((__m256i *)shuffle_input_mask));
}

static INLINE void filter_src_pixels_vertical_avx2(__m256i *tmp, __m256i *src,
                                                   __m256i *coeffs,
                                                   __m256i *v_sum_r0,
                                                   __m256i *v_sum_r1, int k) {
  __m256i input_67 = _mm256_packus_epi32(tmp[k + 10], tmp[k + 11]);
  __m256i input_78 = _mm256_packus_epi32(tmp[k + 11], tmp[k + 12]);
  src[3] = _mm256_shuffle_epi8(
      input_67, _mm256_load_si256((__m256i *)shuffle_input_mask));
  src[7] = _mm256_shuffle_epi8(
      input_78, _mm256_load_si256((__m256i *)shuffle_input_mask));

  __m256i sum_r0 = _mm256_madd_epi16(src[0], coeffs[0]);
  sum_r0 = _mm256_add_epi32(sum_r0, _mm256_madd_epi16(src[1], coeffs[1]));
  sum_r0 = _mm256_add_epi32(sum_r0, _mm256_madd_epi16(src[2], coeffs[2]));
  sum_r0 = _mm256_add_epi32(sum_r0, _mm256_madd_epi16(src[3], coeffs[3]));
  *v_sum_r0 = sum_r0;

  __m256i sum_r1 = _mm256_madd_epi16(src[4], coeffs[4]);
  sum_r1 = _mm256_add_epi32(sum_r1, _mm256_madd_epi16(src[5], coeffs[5]));
  sum_r1 = _mm256_add_epi32(sum_r1, _mm256_madd_epi16(src[6], coeffs[6]));
  sum_r1 = _mm256_add_epi32(sum_r1, _mm256_madd_epi16(src[7], coeffs[7]));
  *v_sum_r1 = sum_r1;

  src[0] = src[1];
  src[1] = src[2];
  src[2] = src[3];

  src[4] = src[5];
  src[5] = src[6];
  src[6] = src[7];
}

static INLINE void store_vertical_filter_output_avx2(
    uint16_t *pred, int p_stride, ConvolveParams *conv_params, int bd,
    const __m256i *res_lo, const __m256i *res_hi, const __m256i *res_add_const,
    const __m256i *reduce_bits_vert_const, const int use_wtd_comp_avg,
    const __m256i *wt0, const __m256i *wt1, const __m256i *res_sub_const,
    const __m256i *round_bits_const, int i, int j, int k,
    const int reduce_bits_vert, const int round_bits) {
  const __m256i clip_pixel =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  __m256i v_sum = *res_lo;
  __m256i v_sum_r1 = *res_hi;

  if (conv_params->is_compound) {
    __m128i *const p =
        (__m128i *)&conv_params->dst[(i + k + 4) * conv_params->dst_stride + j];

    __m128i *const p_r1 =
        (__m128i *)&conv_params->dst[(i + k + 5) * conv_params->dst_stride + j];

    v_sum = _mm256_add_epi32(v_sum, *res_add_const);
    v_sum = _mm256_srai_epi32(_mm256_add_epi32(v_sum, *reduce_bits_vert_const),
                              reduce_bits_vert);
    v_sum_r1 = _mm256_add_epi32(v_sum_r1, *res_add_const);
    v_sum_r1 = _mm256_srai_epi32(
        _mm256_add_epi32(v_sum_r1, *reduce_bits_vert_const), reduce_bits_vert);
    if (conv_params->do_average) {
      __m128i *const dst16 = (__m128i *)&pred[(i + k + 4) * p_stride + j];
      __m128i *const dst16_r1 = (__m128i *)&pred[(i + k + 5) * p_stride + j];
      __m256i p_32 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p));
      __m256i p_32_r1 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p_r1));
      v_sum = _mm256_shuffle_epi32(_mm256_permute4x64_epi64(v_sum, 0xD8), 0xD8);
      v_sum_r1 =
          _mm256_shuffle_epi32(_mm256_permute4x64_epi64(v_sum_r1, 0xD8), 0xD8);

      if (use_wtd_comp_avg) {
        v_sum = _mm256_add_epi32(_mm256_mullo_epi32(p_32, *wt0),
                                 _mm256_mullo_epi32(v_sum, *wt1));
        v_sum = _mm256_srai_epi32(v_sum, DIST_PRECISION_BITS);

        v_sum_r1 = _mm256_add_epi32(_mm256_mullo_epi32(p_32_r1, *wt0),
                                    _mm256_mullo_epi32(v_sum_r1, *wt1));
        v_sum_r1 = _mm256_srai_epi32(v_sum_r1, DIST_PRECISION_BITS);
      } else {
        v_sum = _mm256_srai_epi32(_mm256_add_epi32(p_32, v_sum), 1);
        v_sum_r1 = _mm256_srai_epi32(_mm256_add_epi32(p_32_r1, v_sum_r1), 1);
      }

      __m256i v_sum1 = _mm256_add_epi32(v_sum, *res_sub_const);
      v_sum1 = _mm256_srai_epi32(_mm256_add_epi32(v_sum1, *round_bits_const),
                                 round_bits);
      __m256i v_sum1_r1 = _mm256_add_epi32(v_sum_r1, *res_sub_const);
      v_sum1_r1 = _mm256_srai_epi32(
          _mm256_add_epi32(v_sum1_r1, *round_bits_const), round_bits);

      __m256i v_sum16 = _mm256_packus_epi32(v_sum1, v_sum1_r1);
      v_sum16 =
          _mm256_permute4x64_epi64(_mm256_min_epi16(v_sum16, clip_pixel), 0xD8);
      _mm_storeu_si128(dst16, _mm256_castsi256_si128(v_sum16));
      _mm_storeu_si128(dst16_r1, _mm256_extracti128_si256(v_sum16, 1));
    } else {
      v_sum = _mm256_packus_epi32(v_sum, v_sum_r1);
      __m256i v_sum16 =
          _mm256_shuffle_epi8(_mm256_permute4x64_epi64(v_sum, 0xD8),
                              _mm256_load_si256((__m256i *)shuffle_input_mask));
      _mm_storeu_si128(p, _mm256_castsi256_si128(v_sum16));
      _mm_storeu_si128(p_r1, _mm256_extracti128_si256(v_sum16, 1));
    }
  } else {
    // Round and pack into 16 bits
    const __m256i round_const = _mm256_set1_epi32(
        -(1 << (bd + reduce_bits_vert - 1)) + ((1 << reduce_bits_vert) >> 1));
    const __m256i zero = _mm256_setzero_si256();

    __m256i v_sum1 = _mm256_srai_epi32(_mm256_add_epi32(v_sum, round_const),
                                       reduce_bits_vert);
    __m256i v_sum1_r1 = _mm256_srai_epi32(
        _mm256_add_epi32(v_sum_r1, round_const), reduce_bits_vert);

    v_sum1 = _mm256_packus_epi32(v_sum1, v_sum1_r1);
    v_sum1 = _mm256_permute4x64_epi64(v_sum1, 0xD8);
    __m256i v_sum16 = _mm256_shuffle_epi8(
        v_sum1, _mm256_load_si256((__m256i *)shuffle_input_mask));
    v_sum16 = _mm256_max_epi16(_mm256_min_epi16(v_sum16, clip_pixel), zero);
    __m128i *const p = (__m128i *)&pred[(i + k + 4) * p_stride + j];
    __m128i *const p_r1 = (__m128i *)&pred[(i + k + 5) * p_stride + j];

    _mm_storeu_si128(p, _mm256_castsi256_si128(v_sum16));
    _mm_storeu_si128(p_r1, _mm256_extracti128_si256(v_sum16, 1));
  }
}

void av1_highbd_warp_affine_avx2(const int32_t *mat, const uint16_t *ref,
                                 int width, int height, int stride,
                                 uint16_t *pred, int p_col, int p_row,
                                 int p_width, int p_height, int p_stride,
                                 int subsampling_x, int subsampling_y, int bd,
                                 ConvolveParams *conv_params, int16_t alpha,
                                 int16_t beta, int16_t gamma, int16_t delta) {
  const int left_limit = 0;
  const int right_limit = width - 1;
  const int top_limit = 0;
  const int bottom_limit = height - 1;
  __m256i tmp[15];
  const int reduce_bits_horiz = conv_params->round_0;
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int max_bits_horiz = bd + FILTER_BITS + 1 - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  (void)max_bits_horiz;
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));

  // Check that, even with 12-bit input, the intermediate values will fit
  // into an unsigned 16-bit intermediate array.
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const __m256i reduce_bits_vert_const =
      _mm256_set1_epi32(((1 << reduce_bits_vert) >> 1));
  const __m256i res_add_const = _mm256_set1_epi32(1 << offset_bits_vert);
  const __m256i res_sub_const =
      _mm256_set1_epi32(-(1 << (offset_bits - conv_params->round_1)) -
                        (1 << (offset_bits - conv_params->round_1 - 1)));
  __m256i round_bits_const = _mm256_set1_epi32(((1 << round_bits) >> 1));

  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);

  __m256i v_rbhoriz = _mm256_set1_epi32(1 << (reduce_bits_horiz - 1));
  int ohoriz = 1 << offset_bits_horiz;
  int mhoriz = 1 << max_bits_horiz;
  const __m256i v_offset_bits_horiz = _mm256_set1_epi32(ohoriz);
  const __m256i offset = _mm256_add_epi32(v_offset_bits_horiz, v_rbhoriz);
  (void)mhoriz;
  int sx;

  for (int i = 0; i < p_height; i += 8) {
    for (int j = 0; j < p_width; j += 8) {
      // Calculate the center of this 8x8 block,
      // project to luma coordinates (if in a subsampled chroma plane),
      // apply the affine transformation,
      // then convert back to the original coordinates (if necessary)
      const int32_t src_x = (p_col + j + 4) << subsampling_x;
      const int32_t src_y = (p_row + i + 4) << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];
      const int64_t x4 = dst_x >> subsampling_x;
      const int64_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = (int32_t)(x4 >> WARPEDMODEL_PREC_BITS);
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = (int32_t)(y4 >> WARPEDMODEL_PREC_BITS);
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

#if CONFIG_RELAX_AFFINE_CONSTRAINTS
      sx4 += alpha * (-4) + beta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             ((WARPEDPIXEL_PREC_SHIFTS * 3) << WARPEDDIFF_PREC_BITS);
      sy4 += gamma * (-4) + delta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             ((WARPEDPIXEL_PREC_SHIFTS * 3) << WARPEDDIFF_PREC_BITS);
#else
      sx4 += alpha * (-4) + beta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             (WARPEDPIXEL_PREC_SHIFTS << WARPEDDIFF_PREC_BITS);
      sy4 += gamma * (-4) + delta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             (WARPEDPIXEL_PREC_SHIFTS << WARPEDDIFF_PREC_BITS);
#endif  // CONFIG_RELAX_AFFINE_CONSTRAINTS

      sx4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);
      sy4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);

      // Horizontal filter
      if (ix4 <= left_limit - 7) {
        for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
          int iy = iy4 + k;
          iy = clamp(iy, top_limit, bottom_limit);
          tmp[k + 7] = _mm256_cvtepi16_epi32(
              _mm_set1_epi16((1 << (bd + FILTER_BITS - reduce_bits_horiz - 1)) +
                             ref[iy * stride + left_limit] *
                                 (1 << (FILTER_BITS - reduce_bits_horiz))));
        }
      } else if (ix4 >= right_limit + 7) {
        for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
          int iy = iy4 + k;
          iy = clamp(iy, top_limit, bottom_limit);
          tmp[k + 7] = _mm256_cvtepi16_epi32(
              _mm_set1_epi16((1 << (bd + FILTER_BITS - reduce_bits_horiz - 1)) +
                             ref[iy * stride + right_limit] *
                                 (1 << (FILTER_BITS - reduce_bits_horiz))));
        }
      } else if (((ix4 - 7) < left_limit) || ((ix4 + 7) > right_limit)) {
        const int out_of_boundary_left = left_limit - (ix4 - 6);
        const int out_of_boundary_right = (ix4 + 7) - right_limit;
        __m256i src_padded[2], coeffs[4], src[4];
        if (alpha == 0 && beta == 0) {
          prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sx4, &coeffs[0]);
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);

            prepare_8tap_horiz_src_padded_avx2(
                &ref[iy * stride + ix4 - 7], out_of_boundary_left,
                out_of_boundary_right, &src_padded[0]);
            prepare_8tap_horiz_src_avx2(&src_padded[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        } else if (alpha == 0) {
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);

            sx = sx4 + beta * (k + 4);
            prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sx, coeffs);
            prepare_8tap_horiz_src_padded_avx2(
                &ref[iy * stride + ix4 - 7], out_of_boundary_left,
                out_of_boundary_right, &src_padded[0]);
            prepare_8tap_horiz_src_avx2(&src_padded[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        } else if (beta == 0) {
          prepare_8tap_filter_coeffs_avx2(sx4, alpha, &coeffs[0]);
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);

            prepare_8tap_horiz_src_padded_avx2(
                &ref[iy * stride + ix4 - 7], out_of_boundary_left,
                out_of_boundary_right, &src_padded[0]);
            prepare_8tap_horiz_src_avx2(&src_padded[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        } else {
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);

            sx = sx4 + beta * (k + 4);
            prepare_8tap_filter_coeffs_avx2(sx, alpha, &coeffs[0]);
            prepare_8tap_horiz_src_padded_avx2(
                &ref[iy * stride + ix4 - 7], out_of_boundary_left,
                out_of_boundary_right, &src_padded[0]);
            prepare_8tap_horiz_src_avx2(&src_padded[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        }
      } else {
        __m256i r[2], coeffs[4], src[4];
        if (alpha == 0 && beta == 0) {
          prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sx4, &coeffs[0]);
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);
            iy = iy * stride;
            load_horiz_src_pixels_avx2(&ref[iy + ix4 - 7], &r[0]);
            prepare_8tap_horiz_src_avx2(&r[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        } else if (alpha == 0) {
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);
            iy = iy * stride;

            sx = sx4 + beta * (k + 4);
            prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sx, coeffs);
            load_horiz_src_pixels_avx2(&ref[iy + ix4 - 7], &r[0]);
            prepare_8tap_horiz_src_avx2(&r[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }

        } else if (beta == 0) {
          prepare_8tap_filter_coeffs_avx2(sx4, alpha, &coeffs[0]);
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);
            iy = iy * stride;
            load_horiz_src_pixels_avx2(&ref[iy + ix4 - 7], &r[0]);
            prepare_8tap_horiz_src_avx2(&r[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        } else {
          for (int k = -7; k < AOMMIN(8, p_height - i); ++k) {
            int iy = iy4 + k;
            iy = clamp(iy, top_limit, bottom_limit);
            iy = iy * stride;

            sx = sx4 + beta * (k + 4);
            prepare_8tap_filter_coeffs_avx2(sx, alpha, &coeffs[0]);
            load_horiz_src_pixels_avx2(&ref[iy + ix4 - 7], &r[0]);
            prepare_8tap_horiz_src_avx2(&r[0], &src[0]);
            filter_src_pixels_horiz_avx2(src, coeffs, &offset,
                                         reduce_bits_horiz, &tmp[k + 7]);
          }
        }
      }

      // Vertical filter
      if (gamma == 0 && delta == 0) {
        __m256i coeffs[8], src[8];

        prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sy4, coeffs);
        coeffs[4] = coeffs[0];
        coeffs[5] = coeffs[1];
        coeffs[6] = coeffs[2];
        coeffs[7] = coeffs[3];

        prepare_8tap_vert_src_avx2(tmp, src);

        for (int k = -4; k < AOMMIN(4, p_height - i - 4); k += 2) {
          __m256i v_sum_r0, v_sum_r1;
          filter_src_pixels_vertical_avx2(tmp, src, coeffs, &v_sum_r0,
                                          &v_sum_r1, k);

          store_vertical_filter_output_avx2(
              pred, p_stride, conv_params, bd, &v_sum_r0, &v_sum_r1,
              &res_add_const, &reduce_bits_vert_const, use_wtd_comp_avg, &wt0,
              &wt1, &res_sub_const, &round_bits_const, i, j, k,
              reduce_bits_vert, round_bits);
        }
      } else if (gamma == 0) {
        __m256i coeffs[8], src[8];
        prepare_8tap_vert_src_avx2(tmp, src);

        for (int k = -4; k < AOMMIN(4, p_height - i - 4); k += 2) {
          __m256i v_sum_r0, v_sum_r1;
          int sy_0 = sy4 + delta * (k + 4);
          prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sy_0, &coeffs[0]);

          int sy_1 = sy4 + delta * (k + 5);
          prepare_8tap_filter_coeffs_alpha0_gamma0_avx2(sy_1, &coeffs[4]);

          filter_src_pixels_vertical_avx2(tmp, src, coeffs, &v_sum_r0,
                                          &v_sum_r1, k);

          store_vertical_filter_output_avx2(
              pred, p_stride, conv_params, bd, &v_sum_r0, &v_sum_r1,
              &res_add_const, &reduce_bits_vert_const, use_wtd_comp_avg, &wt0,
              &wt1, &res_sub_const, &round_bits_const, i, j, k,
              reduce_bits_vert, round_bits);
        }
      } else if (delta == 0) {
        __m256i coeffs[8], src[8];
        prepare_8tap_filter_coeffs_avx2(sy4, gamma, &coeffs[0]);

        coeffs[4] = coeffs[0];
        coeffs[5] = coeffs[1];
        coeffs[6] = coeffs[2];
        coeffs[7] = coeffs[3];

        prepare_8tap_vert_src_avx2(tmp, src);

        for (int k = -4; k < AOMMIN(4, p_height - i - 4); k += 2) {
          __m256i v_sum_r0, v_sum_r1;
          filter_src_pixels_vertical_avx2(tmp, src, coeffs, &v_sum_r0,
                                          &v_sum_r1, k);

          store_vertical_filter_output_avx2(
              pred, p_stride, conv_params, bd, &v_sum_r0, &v_sum_r1,
              &res_add_const, &reduce_bits_vert_const, use_wtd_comp_avg, &wt0,
              &wt1, &res_sub_const, &round_bits_const, i, j, k,
              reduce_bits_vert, round_bits);
        }
      } else {
        __m256i src[8];
        prepare_8tap_vert_src_avx2(tmp, src);

        for (int k = -4; k < AOMMIN(4, p_height - i - 4); k += 2) {
          __m256i coeffs[8];
          __m256i v_sum_r0, v_sum_r1;
          int sy_0 = sy4 + delta * (k + 4);
          prepare_8tap_filter_coeffs_avx2(sy_0, gamma, &coeffs[0]);

          int sy_1 = sy4 + delta * (k + 5);
          prepare_8tap_filter_coeffs_avx2(sy_1, gamma, &coeffs[4]);

          filter_src_pixels_vertical_avx2(tmp, src, coeffs, &v_sum_r0,
                                          &v_sum_r1, k);

          store_vertical_filter_output_avx2(
              pred, p_stride, conv_params, bd, &v_sum_r0, &v_sum_r1,
              &res_add_const, &reduce_bits_vert_const, use_wtd_comp_avg, &wt0,
              &wt1, &res_sub_const, &round_bits_const, i, j, k,
              reduce_bits_vert, round_bits);
        }
      }
    }
  }
}

#if CONFIG_EXT_WARP_FILTER
static INLINE void av1_ext_highbd_filter_coeff_avx2(int offset_j0,
                                                    int offset_j4,
                                                    __m256i *coeff) {
  // c0 c1 c2 c3 c4 c5 0 0 | f0 f1 f2 f3 f4 f5 0 0
  const __m256i filt = _mm256_inserti128_si256(
      _mm256_castsi128_si256(
          _mm_loadu_si128((__m128i *)(av1_ext_warped_filter + offset_j0))),
      _mm_loadu_si128((__m128i *)(av1_ext_warped_filter + offset_j4)), 1);

  // c0 c1 c0 c1 c0 c1 c0 c1 | f0 f1 f0 f1 f0 f1 f0 f1
  coeff[0] = _mm256_shuffle_epi8(
      filt, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask0));
  // c2 c3 c2 c3 c2 c3 c2 c3 | f2 f3 f2 f3 f2 f3 f2 f3
  coeff[1] = _mm256_shuffle_epi8(
      filt, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask1));
  // c4 c5 c4 c5 c4 c5 c4 c5 | f4 f5 f4 f5 f4 f5 f4 f5
  coeff[2] = _mm256_shuffle_epi8(
      filt, _mm256_load_si256((__m256i *)shuffle_alpha0_gamma0_mask2));
}

static INLINE void ext_highbd_warp_horizontal_filter_avx2(
    const uint16_t *ref, __m256i *tmp, int stride, int32_t ix_j0, int32_t ix_j4,
    int32_t iy_j0, int32_t iy_j4, int32_t offset_j0, int32_t offset_j4,
    int height, const int offset_bits_horiz, const int reduce_bits_horiz) {
  const __m256i round_const = _mm256_set1_epi32(
      (1 << offset_bits_horiz) + ((1 << reduce_bits_horiz) >> 1));
  __m256i coeff[3];
  av1_ext_highbd_filter_coeff_avx2(offset_j0, offset_j4, coeff);

  for (int k = -4; k < 5; ++k) {
    int iy0 = iy_j0 + k;
    int iy4 = iy_j4 + k;
    iy0 = (iy0 < 0 ? 0 : ((iy0 > height - 1) ? height - 1 : iy0));
    iy4 = (iy4 < 0 ? 0 : ((iy4 > height - 1) ? height - 1 : iy4));

    // s00 s01 s02 s03 s04 s05 s06 s07
    const __m128i src00 =
        _mm_loadu_si128((__m128i *)(ref + iy0 * stride + ix_j0 - 4));
    // s01 s02 s03 s04 s05 s06 s07 s08
    const __m128i src01 =
        _mm_loadu_si128((__m128i *)(ref + iy0 * stride + ix_j0 - 3));
    // r00 r01 r02 r03 r04 r05 r06 r07
    const __m128i src10 =
        _mm_loadu_si128((__m128i *)(ref + iy4 * stride + ix_j4 - 4));
    // r01 r02 r03 r04 r05 r06 r07 r08
    const __m128i src11 =
        _mm_loadu_si128((__m128i *)(ref + iy4 * stride + ix_j4 - 3));

    // s00 s01 s02 s03 s04 s05 s06 s07 | x x x x x x x x
    __m256i src0 = _mm256_castsi128_si256(src00);
    // s00 s01 s02 s03 s04 s05 s06 s07 | r00 r01 r02 r03 r04 r05 r06 r07
    src0 = _mm256_inserti128_si256(src0, src10, 1);
    // s01 s02 s03 s04 s05 s06 s07 s08 | x x x x x x x x
    __m256i src1 = _mm256_castsi128_si256(src01);
    // s01 s02 s03 s04 s05 s06 s07 s08 | r01 r02 r03 r04 r05 r06 r07 r08
    src1 = _mm256_inserti128_si256(src1, src11, 1);

    // s00 s01 s01 s02 s02 s03 s03 s04 | r00 r01 r01 r02 r02 r03 r03 r04
    const __m256i reg_0 = _mm256_shuffle_epi8(
        src0, _mm256_load_si256((__m256i *)shuffle_pattern_0));
    // s02 s03 s03 s04 s04 s05 s05 s06 | r02 r03 r03 r04 r04 r05 r05 r06
    const __m256i reg_1 = _mm256_shuffle_epi8(
        src0, _mm256_load_si256((__m256i *)shuffle_pattern_1));
    // s04 s05 s05 s06 s06 s07 s07 s08 | r04 r05 r05 r06 r06 r07 r07 r08
    const __m256i reg_2 = _mm256_shuffle_epi8(
        src1, _mm256_load_si256((__m256i *)shuffle_pattern_2));

    const __m256i res_0 = _mm256_madd_epi16(reg_0, coeff[0]);
    const __m256i res_1 = _mm256_madd_epi16(reg_1, coeff[1]);
    const __m256i res_2 = _mm256_madd_epi16(reg_2, coeff[2]);

    __m256i res = _mm256_add_epi32(_mm256_add_epi32(res_0, res_2), res_1);

    // s00 s01 s02 s03 | r00 r01 r02 r03
    tmp[k + 4] = _mm256_sra_epi32(_mm256_add_epi32(res, round_const),
                                  _mm_cvtsi32_si128(reduce_bits_horiz));
  }
}

void av1_ext_highbd_warp_affine_avx2(const int32_t *mat, const uint16_t *ref,
                                     int width, int height, int stride,
                                     uint16_t *pred, int p_col, int p_row,
                                     int p_width, int p_height, int p_stride,
                                     int subsampling_x, int subsampling_y,
                                     int bd, ConvolveParams *conv_params
#if CONFIG_WARP_BD_BOX
                                     ,
                                     int use_warp_bd_box,
                                     WarpBoundaryBox *warp_bd_box
#endif  // CONFIG_WARP_BD_BOX
) {
  if (p_width == 4) {
    av1_ext_highbd_warp_affine_sse4_1(
        mat, ref, width, height, stride, pred, p_col, p_row, p_width, p_height,
        p_stride, subsampling_x, subsampling_y, bd, conv_params
#if CONFIG_WARP_BD_BOX
        ,
        use_warp_bd_box, warp_bd_box
#endif  // CONFIG_WARP_BD_BOX
    );
  } else {
    assert(p_width % 8 == 0);
    __m256i tmp[9];
    int i, j;
    const int reduce_bits_horiz = conv_params->round_0;
    const int reduce_bits_vert = conv_params->is_compound
                                     ? conv_params->round_1
                                     : 2 * FILTER_BITS - reduce_bits_horiz;
    const int offset_bits_horiz = bd + FILTER_BITS - 1;
    assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
    assert(!(bd == 12 && reduce_bits_horiz < 5));
    assert(IMPLIES(conv_params->do_average, conv_params->is_compound));

    // Check that, even with 12-bit input, the intermediate values will fit
    // into an unsigned 16-bit intermediate array.
    assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

    const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
    const __m256i clip_pixel =
        _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
    const __m256i reduce_bits_vert_const =
        _mm256_set1_epi32(((1 << reduce_bits_vert) >> 1));
    const __m256i res_add_const = _mm256_set1_epi32(1 << offset_bits_vert);
    const int round_bits =
        2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
    const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
    const __m256i res_sub_const =
        _mm256_set1_epi32(-(1 << (offset_bits - conv_params->round_1)) -
                          (1 << (offset_bits - conv_params->round_1 - 1)));
    __m256i round_bits_const = _mm256_set1_epi32(((1 << round_bits) >> 1));

    const int w0 = conv_params->fwd_offset;
    const int w1 = conv_params->bck_offset;
    const __m256i wt0 = _mm256_set1_epi32(w0);
    const __m256i wt1 = _mm256_set1_epi32(w1);
    const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

    for (i = 0; i < p_height; i += 4) {
      for (j = 0; j < p_width; j += 8) {
        const int32_t src_x_j0 = (p_col + j + 2) << subsampling_x;
        const int32_t src_x_j4 = (p_col + j + 6) << subsampling_x;
        const int32_t src_y = (p_row + i + 2) << subsampling_y;
        const int32_t dst_x_j0 = mat[2] * src_x_j0 + mat[3] * src_y + mat[0];
        const int32_t dst_x_j4 = mat[2] * src_x_j4 + mat[3] * src_y + mat[0];
        const int32_t dst_y_j0 = mat[4] * src_x_j0 + mat[5] * src_y + mat[1];
        const int32_t dst_y_j4 = mat[4] * src_x_j4 + mat[5] * src_y + mat[1];
        const int32_t x4_j0 = dst_x_j0 >> subsampling_x;
        const int32_t x4_j4 = dst_x_j4 >> subsampling_x;
        const int32_t y4_j0 = dst_y_j0 >> subsampling_y;
        const int32_t y4_j4 = dst_y_j4 >> subsampling_y;

        int32_t ix4_j0 = x4_j0 >> WARPEDMODEL_PREC_BITS;
        int32_t ix4_j4 = x4_j4 >> WARPEDMODEL_PREC_BITS;
        int32_t sx4_j0 = x4_j0 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
        int32_t sx4_j4 = x4_j4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
        int32_t iy4_j0 = y4_j0 >> WARPEDMODEL_PREC_BITS;
        int32_t iy4_j4 = y4_j4 >> WARPEDMODEL_PREC_BITS;
        int32_t sy4_j0 = y4_j0 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
        int32_t sy4_j4 = y4_j4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

        // Horizontal Filter
        const int offs_x_j0 = ROUND_POWER_OF_TWO(sx4_j0, WARPEDDIFF_PREC_BITS);
        const int offs_x_j4 = ROUND_POWER_OF_TWO(sx4_j4, WARPEDDIFF_PREC_BITS);
        assert(offs_x_j0 >= 0 && offs_x_j0 <= WARPEDPIXEL_PREC_SHIFTS);
        assert(offs_x_j4 >= 0 && offs_x_j4 <= WARPEDPIXEL_PREC_SHIFTS);
        if (ix4_j0 >= 4 && ix4_j0 <= width - 12 && ix4_j4 >= 4 &&
            ix4_j4 <= width - 12) {
          ext_highbd_warp_horizontal_filter_avx2(
              ref, tmp, stride, ix4_j0, ix4_j4, iy4_j0, iy4_j4, offs_x_j0,
              offs_x_j4, height, offset_bits_horiz, reduce_bits_horiz);
        } else {
          __m128i tmp_reg_lo[9], tmp_reg_hi[9];
          av1_ext_highbd_warp_horiz_sse4_1(
              ref, tmp_reg_lo, stride, ix4_j0, iy4_j0, offs_x_j0, height, width,
              bd, offset_bits_horiz, reduce_bits_horiz);
          av1_ext_highbd_warp_horiz_sse4_1(
              ref, tmp_reg_hi, stride, ix4_j4, iy4_j4, offs_x_j4, height, width,
              bd, offset_bits_horiz, reduce_bits_horiz);
          for (int k = -4; k < 5; ++k) {
            // s00 s01 s02 s03 | r00 r01 r02 r03
            tmp[k + 4] = _mm256_inserti128_si256(
                _mm256_castsi128_si256(tmp_reg_lo[k + 4]), tmp_reg_hi[k + 4],
                1);
          }
        }

        // Vertical filter
        const int offs_y_j0 = ROUND_POWER_OF_TWO(sy4_j0, WARPEDDIFF_PREC_BITS);
        const int offs_y_j4 = ROUND_POWER_OF_TWO(sy4_j4, WARPEDDIFF_PREC_BITS);
        assert(offs_y_j0 >= 0 && offs_y_j0 <= WARPEDPIXEL_PREC_SHIFTS);
        assert(offs_y_j4 >= 0 && offs_y_j4 <= WARPEDPIXEL_PREC_SHIFTS);

        __m256i coeff[3];
        av1_ext_highbd_filter_coeff_avx2(offs_y_j0, offs_y_j4, coeff);

        const __m256i *src = tmp;
        // s00 s01 s02 s03 s10 s11 s12 s13 | r00 r01 r02 r03 r10 r11 r12 r13
        const __m256i src_01 = _mm256_packs_epi32(src[0], src[1]);
        // s20 s21 s22 s23 s30 s31 s32 s33 | r20 r21 r22 r23 r30 r31 r32 r33
        const __m256i src_23 = _mm256_packs_epi32(src[2], src[3]);

        // s10 s11 s12 s13 s20 s21 s22 s23 | r10 r11 r12 r13 r20 r21 r22 r23
        const __m256i src_12 = _mm256_packs_epi32(src[1], src[2]);
        // s30 s31 s32 s33 s40 s41 s42 s43 | r30 r31 r32 r33 r40 r41 r42 r43
        const __m256i src_34 = _mm256_packs_epi32(src[3], src[4]);

        // s00 s10 s01 s11 s02 s12 s03 s13 | r00 r10 r01 r11 r02 r12 r03 r13
        __m256i src_r00 = _mm256_shuffle_epi8(
            src_01, _mm256_load_si256((__m256i *)shuffle_input_mask));
        // s20 s30 s21 s32 s22 s32 s23 s33 | r20 r30 r21 r32 r22 r32 r23 r33
        __m256i src_r01 = _mm256_shuffle_epi8(
            src_23, _mm256_load_si256((__m256i *)shuffle_input_mask));
        //
        __m256i src_r10 = _mm256_shuffle_epi8(
            src_12, _mm256_load_si256((__m256i *)shuffle_input_mask));
        //
        __m256i src_r11 = _mm256_shuffle_epi8(
            src_34, _mm256_load_si256((__m256i *)shuffle_input_mask));
        for (int k = -2; k < AOMMIN(2, p_height - i - 2); k += 2) {
          // s40 s41 s42 s43 s50 s51 s52 s53 | r40 r41 r42 r43 r50 r51 r52 r53
          const __m256i src_45 = _mm256_packs_epi32(src[k + 6], src[k + 7]);
          // s50 s51 s52 s53 s60 s61 s62 s63 | r50 r51 r52 r53 r60 r61 r62 r63
          const __m256i src_56 = _mm256_packs_epi32(src[k + 7], src[k + 8]);
          // s40 s50 s41 s51 s42 s52 s43 s53 | r40 r50 r41 r51 r42 r52 r43 r53
          __m256i src_r02 = _mm256_shuffle_epi8(
              src_45, _mm256_load_si256((__m256i *)shuffle_input_mask));
          //
          __m256i src_r12 = _mm256_shuffle_epi8(
              src_56, _mm256_load_si256((__m256i *)shuffle_input_mask));

          const __m256i res_r00 = _mm256_madd_epi16(src_r00, coeff[0]);
          const __m256i res_r01 = _mm256_madd_epi16(src_r01, coeff[1]);
          const __m256i res_r02 = _mm256_madd_epi16(src_r02, coeff[2]);
          // s00 s01 s02 s03 | r00 r01 r02 r03
          __m256i res_r0 =
              _mm256_add_epi32(_mm256_add_epi32(res_r00, res_r01), res_r02);

          const __m256i res_r10 = _mm256_madd_epi16(src_r10, coeff[0]);
          const __m256i res_r11 = _mm256_madd_epi16(src_r11, coeff[1]);
          const __m256i res_r12 = _mm256_madd_epi16(src_r12, coeff[2]);
          // s10 s11 s12 s13 | r10 r11 r12 r13
          __m256i res_r1 =
              _mm256_add_epi32(_mm256_add_epi32(res_r10, res_r11), res_r12);

          src_r00 = src_r01;
          src_r01 = src_r02;
          src_r10 = src_r11;
          src_r11 = src_r12;

          if (conv_params->is_compound) {
            __m128i *const p0_r0 =
                (__m128i *)&conv_params
                    ->dst[(i + k + 2) * conv_params->dst_stride + j];
            __m128i *const p0_r1 =
                (__m128i *)&conv_params
                    ->dst[(i + k + 3) * conv_params->dst_stride + j];

            res_r0 = _mm256_add_epi32(res_r0, res_add_const);
            res_r0 = _mm256_srai_epi32(
                _mm256_add_epi32(res_r0, reduce_bits_vert_const),
                reduce_bits_vert);
            res_r1 = _mm256_add_epi32(res_r1, res_add_const);
            res_r1 = _mm256_srai_epi32(
                _mm256_add_epi32(res_r1, reduce_bits_vert_const),
                reduce_bits_vert);

            if (conv_params->do_average) {
              __m128i *const dst16_r0 =
                  (__m128i *)&pred[(i + k + 2) * p_stride + j];
              __m128i *const dst16_r1 =
                  (__m128i *)&pred[(i + k + 3) * p_stride + j];
              // p00 p01 p02 p03 p04 p05 p06 p07
              __m256i p_r0 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p0_r0));
              // p10 p11 p12 p13 p14 p15 p16 p17
              __m256i p_r1 = _mm256_cvtepu16_epi32(_mm_loadu_si128(p0_r1));
              if (use_wtd_comp_avg) {
                res_r0 = _mm256_add_epi32(_mm256_mullo_epi32(p_r0, wt0),
                                          _mm256_mullo_epi32(res_r0, wt1));
                res_r0 = _mm256_srai_epi32(res_r0, DIST_PRECISION_BITS);
                res_r1 = _mm256_add_epi32(_mm256_mullo_epi32(p_r1, wt0),
                                          _mm256_mullo_epi32(res_r1, wt1));
                res_r1 = _mm256_srai_epi32(res_r1, DIST_PRECISION_BITS);
              } else {
                res_r0 = _mm256_srai_epi32(_mm256_add_epi32(p_r0, res_r0), 1);
                res_r1 = _mm256_srai_epi32(_mm256_add_epi32(p_r1, res_r1), 1);
              }

              __m256i res32_r0 = _mm256_add_epi32(res_r0, res_sub_const);
              res32_r0 = _mm256_srai_epi32(
                  _mm256_add_epi32(res32_r0, round_bits_const), round_bits);
              __m256i res32_r1 = _mm256_add_epi32(res_r1, res_sub_const);
              res32_r1 = _mm256_srai_epi32(
                  _mm256_add_epi32(res32_r1, round_bits_const), round_bits);

              // s00 s01 s02 s03 s10 s11 s12 s13 | r00 r01 r02 r03 r10 r11 r12
              // r13
              __m256i res16_r0 = _mm256_packus_epi32(res32_r0, res32_r1);
              res16_r0 = _mm256_min_epi16(res16_r0, clip_pixel);
              // s00 s01 s02 s03 r00 r01 r02 r03 | s10 s11 s12 s13 r10 r11 r12
              // r13
              res16_r0 = _mm256_permute4x64_epi64(res16_r0, 0xd8);

              _mm_storeu_si128(dst16_r0, _mm256_castsi256_si128(res16_r0));
              _mm_storeu_si128(dst16_r1, _mm256_extracti128_si256(res16_r0, 1));
            } else {
              const __m256i res = _mm256_permute4x64_epi64(
                  _mm256_packus_epi32(res_r0, res_r1), 0xd8);

              _mm_storeu_si128(p0_r0, _mm256_castsi256_si128(res));
              _mm_storeu_si128(p0_r1, _mm256_extracti128_si256(res, 1));
            }
          } else {
            // Round and pack into 16 bits
            const __m256i round_const =
                _mm256_set1_epi32(-(1 << (bd + reduce_bits_vert - 1)) +
                                  ((1 << reduce_bits_vert) >> 1));

            const __m256i res_r0_round = _mm256_srai_epi32(
                _mm256_add_epi32(res_r0, round_const), reduce_bits_vert);
            const __m256i res_r1_round = _mm256_srai_epi32(
                _mm256_add_epi32(res_r1, round_const), reduce_bits_vert);

            __m256i res_16bit = _mm256_packs_epi32(res_r0_round, res_r1_round);
            // Clamp res_16bit to the range [0, 2^bd - 1]
            const __m256i max_val = _mm256_set1_epi16((1 << bd) - 1);
            const __m256i zero = _mm256_setzero_si256();
            res_16bit =
                _mm256_max_epi16(_mm256_min_epi16(res_16bit, max_val), zero);

            // Store, blending with 'pred' if needed
            __m128i *const p0_r0 = (__m128i *)&pred[(i + k + 2) * p_stride + j];
            __m128i *const p0_r1 = (__m128i *)&pred[(i + k + 3) * p_stride + j];

            const __m256i res = _mm256_permute4x64_epi64(res_16bit, 0xd8);
            _mm_storeu_si128(p0_r0, _mm256_castsi256_si128(res));
            _mm_storeu_si128(p0_r1, _mm256_extracti128_si256(res, 1));
          }
        }
      }
    }
  }
}
#endif  // CONFIG_EXT_WARP_FILTER
