/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
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

#include "config/aom_config.h"

#include "av1/common/reconinter.h"

DECLARE_ALIGNED(32, static const uint16_t,
                col_8_vector[16]) = { 0, 1, 2, 3, 4, 5, 6, 7,
                                      0, 1, 2, 3, 4, 5, 6, 7 };

DECLARE_ALIGNED(32, static const uint16_t,
                col_16_vector[16]) = { 0, 1, 2,  3,  4,  5,  6,  7,
                                       8, 9, 10, 11, 12, 13, 14, 15 };

DECLARE_ALIGNED(32, static const uint32_t, col_vector[256]) = {
  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,
  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104,
  105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
  120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
  135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
  150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
  165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
  180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
  195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
  210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
  225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
  240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
  255
};

DECLARE_ALIGNED(32, static const uint8_t,
                shuffle_mask_avx2[32]) = { 0,  1,  4, 5, 8, 9, 12, 13, 2, 3, 2,
                                           3,  2,  3, 2, 3, 0, 1,  4,  5, 8, 9,
                                           12, 13, 2, 3, 2, 3, 2,  3,  2, 3 };

static INLINE __m256i round_power_of_two_avx2(__m256i in_vec, int n) {
  __m256i add_round_factor = _mm256_set1_epi32(1 << (n - 1));
  in_vec = _mm256_add_epi32(in_vec, add_round_factor);
  __m256i round_vec = _mm256_srai_epi32(in_vec, n);
  return round_vec;
}

static INLINE __m256i clamp_vector_avx2(__m256i in_vec, __m256i max_vec,
                                        __m256i min_vec) {
  in_vec = _mm256_max_epi32(in_vec, min_vec);
  __m256i clamp_vec = _mm256_min_epi32(in_vec, max_vec);
  return clamp_vec;
}

#if CONFIG_AFFINE_REFINEMENT
void av1_warp_plane_bilinear_avx2(WarpedMotionParams *wm, int bd,
                                  const uint16_t *ref, int width, int height,
                                  int stride, uint16_t *pred, int p_col,
                                  int p_row, int p_width, int p_height,
                                  int p_stride, int subsampling_x,
                                  int subsampling_y,
                                  ConvolveParams *conv_params) {
  (void)conv_params;
#if AFFINE_FAST_WARP_METHOD == 3
  assert(wm->wmtype <= AFFINE);
  assert(!is_uneven_wtd_comp_avg(conv_params));
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
  const int32_t *const mat = wm->wmmat;
  __m256i mat_0 = _mm256_set1_epi32(mat[0]);
  __m256i mat_1 = _mm256_set1_epi32(mat[1]);
  __m256i mat_2 = _mm256_set1_epi32(mat[2]);
  __m256i mat_3 = _mm256_set1_epi32(mat[3]);
  __m256i mat_4 = _mm256_set1_epi32(mat[4]);
  __m256i mat_5 = _mm256_set1_epi32(mat[5]);

  __m256i zeros = _mm256_setzero_si256();
  __m256i ones = _mm256_set1_epi32(1);
  __m256i p_row_vec = _mm256_set1_epi32(p_row);
  __m256i p_col_vec = _mm256_set1_epi32(p_col);
  __m256i width_minus_1_vec = _mm256_set1_epi32(width - 1);
  __m256i height_minus_1_vec = _mm256_set1_epi32(height - 1);
  __m256i stride_vec = _mm256_set1_epi32(stride);

  __m256i warpmodel_prec_bits =
      _mm256_set1_epi32(((1 << WARPEDMODEL_PREC_BITS) - 1));
  __m256i unit_offset = _mm256_set1_epi32(1 << BILINEAR_WARP_PREC_BITS);

  const __m256i clip_pixel =
      _mm256_set1_epi32(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));

  for (int i = 0; i < p_height; ++i) {
    __m256i i_base_vec = _mm256_set1_epi32(i);
    __m256i i_vec = _mm256_add_epi32(p_row_vec, i_base_vec);
    __m256i src_y = _mm256_slli_epi32(i_vec, subsampling_y);

    __m256i m3_y = _mm256_mullo_epi32(mat_3, src_y);
    __m256i m3_y_m0 = _mm256_add_epi32(m3_y, mat_0);

    __m256i m5_y = _mm256_mullo_epi32(mat_5, src_y);
    __m256i m5_y_m1 = _mm256_add_epi32(m5_y, mat_1);
    for (int j = 0; j < p_width; j += 8) {
      __m256i col_add_vec =
          _mm256_load_si256((const __m256i *)(&col_vector[j]));
      __m256i j_vec = _mm256_add_epi32(p_col_vec, col_add_vec);
      __m256i src_x = _mm256_slli_epi32(j_vec, subsampling_x);

      __m256i m2_x = _mm256_mullo_epi32(mat_2, src_x);
      __m256i dst_x = _mm256_add_epi32(m2_x, m3_y_m0);

      __m256i m4_x = _mm256_mullo_epi32(mat_4, src_x);
      __m256i dst_y = _mm256_add_epi32(m4_x, m5_y_m1);

      __m256i x = _mm256_srai_epi32(dst_x, subsampling_x);
      __m256i y = _mm256_srai_epi32(dst_y, subsampling_y);

      __m256i ix = _mm256_srai_epi32(x, WARPEDMODEL_PREC_BITS);
      __m256i iy = _mm256_srai_epi32(y, WARPEDMODEL_PREC_BITS);

      __m256i ix0 = clamp_vector_avx2(ix, width_minus_1_vec, zeros);
      __m256i iy0 = clamp_vector_avx2(iy, height_minus_1_vec, zeros);

      __m256i ix1 = _mm256_add_epi32(ix, ones);
      __m256i iy1 = _mm256_add_epi32(iy, ones);

      ix1 = clamp_vector_avx2(ix1, width_minus_1_vec, zeros);
      iy1 = clamp_vector_avx2(iy1, height_minus_1_vec, zeros);

      __m256i iy0_stride = _mm256_mullo_epi32(iy0, stride_vec);
      __m256i iy0_stride_ix0 = _mm256_add_epi32(iy0_stride, ix0);
      __m256i iy0_stride_ix1 = _mm256_add_epi32(iy0_stride, ix1);

      __m256i iy1_stride = _mm256_mullo_epi32(iy1, stride_vec);
      __m256i iy1_stride_ix0 = _mm256_add_epi32(iy1_stride, ix0);
      __m256i iy1_stride_ix1 = _mm256_add_epi32(iy1_stride, ix1);

      __m256i sx = _mm256_and_si256(x, warpmodel_prec_bits);
      __m256i sy = _mm256_and_si256(y, warpmodel_prec_bits);

      __m256i coeff_x = round_power_of_two_avx2(
          sx, WARPEDMODEL_PREC_BITS - BILINEAR_WARP_PREC_BITS);
      __m256i coeff_y = round_power_of_two_avx2(
          sy, WARPEDMODEL_PREC_BITS - BILINEAR_WARP_PREC_BITS);

      __m256i coeff_x_tmp = _mm256_bslli_epi128(coeff_x, 2);
      __m256i coeff_y_tmp = _mm256_bslli_epi128(coeff_y, 2);

      __m256i offset_minus_coeffx = _mm256_sub_epi32(unit_offset, coeff_x);
      __m256i offset_minus_coeffy = _mm256_sub_epi32(unit_offset, coeff_y);

      __m256i blend_coeffx =
          _mm256_blend_epi16(offset_minus_coeffx, coeff_x_tmp, 0xAA);
      __m256i blend_coeffy =
          _mm256_blend_epi16(offset_minus_coeffy, coeff_y_tmp, 0xAA);

      __m256i ref_ix0_iy0 =
          _mm256_i32gather_epi32((int *)ref, iy0_stride_ix0, 2);
      __m256i ref_ix1_iy0 =
          _mm256_i32gather_epi32((int *)ref, iy0_stride_ix1, 2);
      __m256i ref_ix1_iy0_tmp = _mm256_bslli_epi128(ref_ix1_iy0, 2);
      __m256i ref_iy0 = _mm256_blend_epi16(ref_ix0_iy0, ref_ix1_iy0_tmp, 0xAA);
      ref_iy0 = _mm256_madd_epi16(ref_iy0, blend_coeffx);

      __m256i ref_ix0_iy1 =
          _mm256_i32gather_epi32((int *)ref, iy1_stride_ix0, 2);
      __m256i ref_ix1_iy1 =
          _mm256_i32gather_epi32((int *)ref, iy1_stride_ix1, 2);
      __m256i ref_ix1_iy1_tmp = _mm256_bslli_epi128(ref_ix1_iy1, 2);
      __m256i ref_y1 = _mm256_blend_epi16(ref_ix0_iy1, ref_ix1_iy1_tmp, 0xAA);
      ref_y1 = _mm256_madd_epi16(ref_y1, blend_coeffx);

      __m256i tmp0 = round_power_of_two_avx2(ref_iy0, BILINEAR_WARP_PREC_BITS);
      __m256i tmp1 = round_power_of_two_avx2(ref_y1, BILINEAR_WARP_PREC_BITS);

      __m256i tmp1_tmp = _mm256_bslli_epi128(tmp1, 2);
      __m256i tmp_final = _mm256_blend_epi16(tmp0, tmp1_tmp, 0xAA);

      __m256i sum = _mm256_madd_epi16(tmp_final, blend_coeffy);
      sum = round_power_of_two_avx2(sum, BILINEAR_WARP_PREC_BITS);

      __m256i result = clamp_vector_avx2(sum, clip_pixel, zeros);
      result = _mm256_shuffle_epi8(
          result, _mm256_load_si256((__m256i *)shuffle_mask_avx2));
      __m256i pred_vec = _mm256_permute4x64_epi64(result, 0x08);
      _mm_storeu_si128((__m128i *)&pred[i * p_stride + j],
                       _mm256_castsi256_si128(pred_vec));
    }
  }
#else
  (void)wm;
  (void)bd;
  (void)ref;
  (void)width;
  (void)height;
  (void)stride;
  (void)pred;
  (void)p_col;
  (void)p_row;
  (void)p_width;
  (void)p_height;
  (void)p_stride;
  (void)subsampling_x;
  (void)subsampling_y;
#endif  // AFFINE_FAST_WARP_METHOD == 3
}
#endif  // CONFIG_AFFINE_REFINEMENT

static INLINE void sign_extend_16bit_to_32bit(__m256i in, __m256i zero,
                                              __m256i *out_lo,
                                              __m256i *out_hi) {
  const __m256i sign_bits = _mm256_cmpgt_epi16(zero, in);
  *out_lo = _mm256_unpacklo_epi16(in, sign_bits);
  *out_hi = _mm256_unpackhi_epi16(in, sign_bits);
}

static INLINE void sign_extend_32bit_to_64bit(__m256i in, __m256i zero,
                                              __m256i *out_lo,
                                              __m256i *out_hi) {
  const __m256i sign_bits = _mm256_cmpgt_epi32(zero, in);
  *out_lo = _mm256_unpacklo_epi32(in, sign_bits);
  *out_hi = _mm256_unpackhi_epi32(in, sign_bits);
}

static INLINE __m256i highbd_clamp_epi64(__m256i in, int64_t max_value,
                                         int64_t min_value) {
  __m256i clamp_min = _mm256_set1_epi64x(min_value);
  __m256i clamp_max = _mm256_set1_epi64x(max_value);

  // Compare to create masks
  __m256i greater_than_min_mask = _mm256_cmpgt_epi64(in, clamp_min);
  __m256i less_than_max_mask = _mm256_cmpgt_epi64(clamp_max, in);

  // vec = MAX(vec, min_value)
  in = _mm256_or_si256(_mm256_and_si256(greater_than_min_mask, in),
                       _mm256_andnot_si256(greater_than_min_mask, clamp_min));

  // vec = MIN(vec, max_value)
  in = _mm256_or_si256(_mm256_and_si256(less_than_max_mask, in),
                       _mm256_andnot_si256(less_than_max_mask, clamp_max));

  return in;
}

static INLINE __m256i round_power_of_two_epi32(__m256i in, int reduce_bits) {
  __m256i rounding_offset = _mm256_set1_epi32((1 << (reduce_bits)) >> 1);

  // add_vec = vec + (2 ^ n) / 2
  __m256i add_vec = _mm256_add_epi32(in, rounding_offset);

  // round_vec = add_vec >> n
  __m256i rounded_vec = _mm256_srai_epi32(add_vec, reduce_bits);
  return rounded_vec;
}

static INLINE __m256i round_power_of_two_signed_epi32(__m256i in,
                                                      int reduce_bits) {
  // Create a mask for the sign bits of the input vector
  __m256i sign_mask = _mm256_srai_epi32(in, 31);

  __m256i abs_vec = _mm256_abs_epi32(in);

  __m256i rounded_vec = round_power_of_two_epi32(abs_vec, reduce_bits);

  // Restore the sign
  rounded_vec = _mm256_xor_si256(rounded_vec, sign_mask);
  rounded_vec = _mm256_sub_epi32(rounded_vec, sign_mask);

  return rounded_vec;
}

static INLINE int64_t horiz_sum_epi64(__m256i vec) {
  int64_t sum;
  __m256i vec_low = _mm256_srli_si256(vec, 8);
  __m256i accum = _mm256_add_epi64(vec, vec_low);
  accum = _mm256_add_epi64(accum, _mm256_permute4x64_epi64(accum, 0x02));
#if ARCH_X86_64
  sum = _mm_cvtsi128_si64(_mm256_castsi256_si128(accum));
#else
  _mm_storel_epi64((__m128i *)&sum, _mm256_castsi256_si128(accum));
#endif
  return sum;
}

static INLINE __m256i add_epi32_as_epi64(__m256i a, __m256i b) {
  const __m256i zeros = _mm256_setzero_si256();
  __m256i a_lo, a_hi, b_lo, b_hi;
  sign_extend_32bit_to_64bit(a, zeros, &a_lo, &a_hi);
  sign_extend_32bit_to_64bit(b, zeros, &b_lo, &b_hi);

  __m256i sum_lo = _mm256_add_epi64(a_lo, b_lo);
  __m256i sum_hi = _mm256_add_epi64(a_hi, b_hi);
  __m256i sum = _mm256_add_epi64(sum_hi, sum_lo);

  return sum;
}

#if CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_AFFINE_REFINEMENT
static INLINE void calc_max_vector(__m256i *gx_vec, __m256i *gy_vec,
                                   __m256i *pdiff_vec, __m256i *max_vec) {
  *gx_vec = _mm256_abs_epi16(*gx_vec);
  *gy_vec = _mm256_abs_epi16(*gy_vec);
  *pdiff_vec = _mm256_abs_epi16(*pdiff_vec);

  __m256i max_abs_vec =
      _mm256_max_epu16(_mm256_max_epu16(*gx_vec, *gy_vec), *pdiff_vec);

  // Update the maximum vector
  *max_vec = _mm256_max_epu16(*max_vec, max_abs_vec);
}

static INLINE int64_t find_max_matrix_element_interp_grad(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int bw, int bh) {
  __m256i max_vec = _mm256_setzero_si256();
  const int height_loop = AOMMIN(bh, (1 << (7 - AFFINE_AVERAGING_BITS)));
  if (bw == 8) {
    for (int i = 0; i < height_loop; i += 2) {
      // Load 8 elements from gx, gy, and pdiff
      __m128i gx_vec0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
      __m128i gy_vec0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);
      __m128i pdiff_vec0 = _mm_loadu_si128((__m128i *)&pdiff[i * pstride]);
      __m128i gx_vec1 = _mm_loadu_si128((__m128i *)&gx[(i + 1) * gstride]);
      __m128i gy_vec1 = _mm_loadu_si128((__m128i *)&gy[(i + 1) * gstride]);
      __m128i pdiff_vec1 =
          _mm_loadu_si128((__m128i *)&pdiff[(i + 1) * pstride]);

      __m256i gx_vec =
          _mm256_inserti128_si256(_mm256_castsi128_si256(gx_vec0), gx_vec1, 1);
      __m256i gy_vec =
          _mm256_inserti128_si256(_mm256_castsi128_si256(gy_vec0), gy_vec1, 1);
      __m256i pdiff_vec = _mm256_inserti128_si256(
          _mm256_castsi128_si256(pdiff_vec0), pdiff_vec1, 1);

      calc_max_vector(&gx_vec, &gy_vec, &pdiff_vec, &max_vec);
    }
  } else {
    for (int i = 0; i < height_loop; i++) {
      // Load 16 elements from gx, gy, and pdiff
      __m256i gx_vec = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
      __m256i gy_vec = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
      __m256i pdiff_vec = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);

      calc_max_vector(&gx_vec, &gy_vec, &pdiff_vec, &max_vec);
    }
  }
  // Find the maximum element in max_vec
  __m128i lo = _mm256_castsi256_si128(max_vec);
  __m128i hi = _mm256_extracti128_si256(max_vec, 1);
  __m128i max_vector = _mm_max_epu16(lo, hi);
  // Perform pairwise max
  __m128i max1 = _mm_max_epu16(max_vector, _mm_srli_si128(max_vector, 8));
  max1 = _mm_max_epu16(max1, _mm_srli_si128(max1, 4));
  max1 = _mm_max_epu16(max1, _mm_srli_si128(max1, 2));

  // Extract the maximum value
  uint16_t max_el = (uint16_t)_mm_extract_epi16(max1, 0);
  return max_el;
}

static INLINE void multiply_and_accumulate(__m256i *gx_vec, __m256i *gy_vec,
                                           __m256i *x_vec, __m256i *y_vec,
                                           __m256i *pdiff_vec,
                                           const int coords_bits,
                                           const int grad_bits, __m256i *a_mat,
                                           __m256i *b_vec) {
  __m256i zeros = _mm256_setzero_si256();
  __m256i minus_y_vec = _mm256_sub_epi16(zeros, *y_vec);
  __m256i gx_gy_lo = _mm256_unpacklo_epi16(*gx_vec, *gy_vec);
  __m256i x_y_lo = _mm256_unpacklo_epi16(*x_vec, *y_vec);
  __m256i a1_temp_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_gy_lo, x_y_lo), coords_bits);

  __m256i gx_gy_hi = _mm256_unpackhi_epi16(*gx_vec, *gy_vec);
  __m256i x_y_hi = _mm256_unpackhi_epi16(*x_vec, *y_vec);
  __m256i a1_temp_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_gy_hi, x_y_hi), coords_bits);

  __m256i x_minus_y_lo = _mm256_unpacklo_epi16(minus_y_vec, *x_vec);
  __m256i a0_temp_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_gy_lo, x_minus_y_lo), coords_bits);

  __m256i x_minus_y_hi = _mm256_unpackhi_epi16(minus_y_vec, *x_vec);
  __m256i a0_temp_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_gy_hi, x_minus_y_hi), coords_bits);

  assert(AFFINE_CLAMP_VAL == (1 << 15));
  // Clip the values of a[] to [-AFFINE_CLAMP_VAL, AFFINE_CLAMP_VAL-1] (i.e., to
  // 16-bit signed range). Here, using the instruction _mm256_packs_epi32() to
  // clip 32-bit signed values to 16-bit signed range.
  const __m256i a0_temp_0 = _mm256_packs_epi32(a0_temp_lo, a0_temp_hi);
  const __m256i a1_temp_0 = _mm256_packs_epi32(a1_temp_lo, a1_temp_hi);
  const __m256i a0_lo = _mm256_unpacklo_epi16(a0_temp_0, zeros);
  const __m256i a0_hi = _mm256_unpackhi_epi16(a0_temp_0, zeros);
  const __m256i a1_lo = _mm256_unpacklo_epi16(a1_temp_0, zeros);
  const __m256i a1_hi = _mm256_unpackhi_epi16(a1_temp_0, zeros);

  __m256i gx_a2_lo = _mm256_unpacklo_epi16(*gx_vec, zeros);
  __m256i gx_a2_hi = _mm256_unpackhi_epi16(*gx_vec, zeros);
  __m256i gy_a3_lo = _mm256_unpacklo_epi16(*gy_vec, zeros);
  __m256i gy_a3_hi = _mm256_unpackhi_epi16(*gy_vec, zeros);

  // Diagonal elements
  __m256i a00_lo =
      round_power_of_two_epi32(_mm256_madd_epi16(a0_lo, a0_lo), grad_bits);
  __m256i a00_hi =
      round_power_of_two_epi32(_mm256_madd_epi16(a0_hi, a0_hi), grad_bits);
  __m256i a11_lo =
      round_power_of_two_epi32(_mm256_madd_epi16(a1_lo, a1_lo), grad_bits);
  __m256i a11_hi =
      round_power_of_two_epi32(_mm256_madd_epi16(a1_hi, a1_hi), grad_bits);
  __m256i a22_lo = round_power_of_two_epi32(
      _mm256_madd_epi16(gx_a2_lo, gx_a2_lo), grad_bits);
  __m256i a22_hi = round_power_of_two_epi32(
      _mm256_madd_epi16(gx_a2_hi, gx_a2_hi), grad_bits);
  __m256i a33_lo = round_power_of_two_epi32(
      _mm256_madd_epi16(gy_a3_lo, gy_a3_lo), grad_bits);
  __m256i a33_hi = round_power_of_two_epi32(
      _mm256_madd_epi16(gy_a3_hi, gy_a3_hi), grad_bits);

  // Non-diagonal elements
  // Row0->
  __m256i a01_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_lo, a1_lo), grad_bits);
  __m256i a01_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_hi, a1_hi), grad_bits);
  __m256i a02_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_lo, gx_a2_lo), grad_bits);
  __m256i a02_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_hi, gx_a2_hi), grad_bits);
  __m256i a03_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_lo, gy_a3_lo), grad_bits);
  __m256i a03_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_hi, gy_a3_hi), grad_bits);
  // Row1->
  __m256i a12_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_lo, gx_a2_lo), grad_bits);
  __m256i a12_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_hi, gx_a2_hi), grad_bits);
  __m256i a13_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_lo, gy_a3_lo), grad_bits);
  __m256i a13_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_hi, gy_a3_hi), grad_bits);
  // Row2->
  __m256i a23_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_a2_lo, gy_a3_lo), grad_bits);
  __m256i a23_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_a2_hi, gy_a3_hi), grad_bits);

  // a00
  a_mat[0] = _mm256_add_epi64(a_mat[0], add_epi32_as_epi64(a00_lo, a00_hi));
  // a01
  a_mat[1] = _mm256_add_epi64(a_mat[1], add_epi32_as_epi64(a01_lo, a01_hi));
  // a02
  a_mat[2] = _mm256_add_epi64(a_mat[2], add_epi32_as_epi64(a02_lo, a02_hi));
  // a03
  a_mat[3] = _mm256_add_epi64(a_mat[3], add_epi32_as_epi64(a03_lo, a03_hi));
  // a11
  a_mat[4] = _mm256_add_epi64(a_mat[4], add_epi32_as_epi64(a11_lo, a11_hi));
  // a12
  a_mat[5] = _mm256_add_epi64(a_mat[5], add_epi32_as_epi64(a12_lo, a12_hi));
  // a13
  a_mat[6] = _mm256_add_epi64(a_mat[6], add_epi32_as_epi64(a13_lo, a13_hi));
  // a22
  a_mat[7] = _mm256_add_epi64(a_mat[7], add_epi32_as_epi64(a22_lo, a22_hi));
  // a23
  a_mat[8] = _mm256_add_epi64(a_mat[8], add_epi32_as_epi64(a23_lo, a23_hi));
  // a33
  a_mat[9] = _mm256_add_epi64(a_mat[9], add_epi32_as_epi64(a33_lo, a33_hi));

  // Compute vec_b
  __m256i pdiff_vec_lo = _mm256_unpacklo_epi16(*pdiff_vec, zeros);
  __m256i pdiff_vec_hi = _mm256_unpackhi_epi16(*pdiff_vec, zeros);

  __m256i v0_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_lo, pdiff_vec_lo), grad_bits);
  __m256i v0_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a0_hi, pdiff_vec_hi), grad_bits);
  __m256i v1_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_lo, pdiff_vec_lo), grad_bits);
  __m256i v1_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(a1_hi, pdiff_vec_hi), grad_bits);
  __m256i v2_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_a2_lo, pdiff_vec_lo), grad_bits);
  __m256i v2_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gx_a2_hi, pdiff_vec_hi), grad_bits);
  __m256i v3_lo = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gy_a3_lo, pdiff_vec_lo), grad_bits);
  __m256i v3_hi = round_power_of_two_signed_epi32(
      _mm256_madd_epi16(gy_a3_hi, pdiff_vec_hi), grad_bits);

  b_vec[0] = _mm256_add_epi64(b_vec[0], add_epi32_as_epi64(v0_lo, v0_hi));
  b_vec[1] = _mm256_add_epi64(b_vec[1], add_epi32_as_epi64(v1_lo, v1_hi));
  b_vec[2] = _mm256_add_epi64(b_vec[2], add_epi32_as_epi64(v2_lo, v2_hi));
  b_vec[3] = _mm256_add_epi64(b_vec[3], add_epi32_as_epi64(v3_lo, v3_hi));
}

static INLINE void calc_mat_a_and_vec_b(const int16_t *pdiff, int pstride,
                                        const int16_t *gx, const int16_t *gy,
                                        int gstride, int bw, int bh,
                                        const int coords_bits,
                                        const int grad_bits, int64_t *mat_a,
                                        int64_t *vec_b) {
  int16_t step_h = AOMMAX(1, bh >> (7 - AFFINE_AVERAGING_BITS));
  int16_t step_w = AOMMAX(1, bw >> (7 - AFFINE_AVERAGING_BITS));

  __m256i step_w_vec = _mm256_set1_epi16(step_w);
  __m256i x_offset_vec = _mm256_set1_epi16(1 - (bw / 2));
  __m256i y_offset_vec = _mm256_set1_epi16(1 - (bh / 2));
  __m256i zeros = _mm256_setzero_si256();

  __m256i a_mat[10] = { zeros, zeros, zeros, zeros, zeros,
                        zeros, zeros, zeros, zeros, zeros };
  __m256i b_vec[4] = { zeros, zeros, zeros, zeros };
  int height_loop = AOMMIN(bh, (1 << (7 - AFFINE_AVERAGING_BITS)));

  if (bw == 8) {
    const __m256i step_h_vec = _mm256_set1_epi16(step_h);
    __m256i j_vec = _mm256_load_si256((__m256i *)col_8_vector);
    __m256i x_vec =
        _mm256_add_epi16(_mm256_mullo_epi16(step_w_vec, j_vec), x_offset_vec);
    __m256i i_vec = _mm256_inserti128_si256(zeros, _mm_set1_epi16(1), 1);

    for (int i = 0; i < height_loop; i += 2) {
      __m256i y_vec =
          _mm256_add_epi16(_mm256_mullo_epi16(step_h_vec, i_vec), y_offset_vec);

      __m128i gx_vec_r0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
      __m128i gy_vec_r0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);

      __m128i gx_vec_r1 = _mm_loadu_si128((__m128i *)&gx[(i + 1) * gstride]);
      __m128i gy_vec_r1 = _mm_loadu_si128((__m128i *)&gy[(i + 1) * gstride]);

      __m256i gx_vec = _mm256_inserti128_si256(
          _mm256_castsi128_si256(gx_vec_r0), gx_vec_r1, 1);
      __m256i gy_vec = _mm256_inserti128_si256(
          _mm256_castsi128_si256(gy_vec_r0), gy_vec_r1, 1);

      __m128i pdiff_vec_lo =
          _mm_loadu_si128((const __m128i *)&pdiff[i * pstride]);
      __m128i pdiff_vec_hi =
          _mm_loadu_si128((const __m128i *)&pdiff[(i + 1) * pstride]);
      __m256i pdiff_vec = _mm256_inserti128_si256(
          _mm256_castsi128_si256(pdiff_vec_lo), pdiff_vec_hi, 1);

      multiply_and_accumulate(&gx_vec, &gy_vec, &x_vec, &y_vec, &pdiff_vec,
                              coords_bits, grad_bits, a_mat, b_vec);
      i_vec = _mm256_add_epi16(i_vec, _mm256_set1_epi16(2));
    }
  } else {
    __m256i j_vec = _mm256_load_si256((__m256i *)col_16_vector);
    __m256i x_vec =
        _mm256_add_epi16(_mm256_mullo_epi16(step_w_vec, j_vec), x_offset_vec);
    for (int i = 0; i < height_loop; ++i) {
      __m256i y_vec =
          _mm256_add_epi16(_mm256_set1_epi16(i * step_h), y_offset_vec);

      __m256i gx_vec = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
      __m256i gy_vec = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);

      __m256i pdiff_vec =
          _mm256_loadu_si256((const __m256i *)&pdiff[i * pstride]);

      multiply_and_accumulate(&gx_vec, &gy_vec, &x_vec, &y_vec, &pdiff_vec,
                              coords_bits, grad_bits, a_mat, b_vec);
    }
  }
  int index = 0;
  // Sum the individual values in mat_a[]
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (j >= i) {
        // Upper triangle
        mat_a[i * 4 + j] = horiz_sum_epi64(a_mat[index++]);
      }
    }
  }
  for (int s = 0; s < 4; ++s) {
    for (int t = s + 1; t < 4; ++t) mat_a[t * 4 + s] = mat_a[s * 4 + t];
  }
  const int rls_alpha = (bw * bh >> 4) * AFFINE_RLS_PARAM;
  mat_a[0] += rls_alpha;
  mat_a[5] += rls_alpha;
  mat_a[10] += rls_alpha;
  mat_a[15] += rls_alpha;

  // Sum the individual values in vec_b
  for (int l = 0; l < 4; ++l) {
    vec_b[l] = horiz_sum_epi64(b_vec[l]);
  }

  __m256i ret[5];
  __m256i val = _mm256_loadu_si256((__m256i *)&mat_a[0]);
  ret[0] = highbd_clamp_epi64(val, AFFINE_COV_CLAMP_VAL, -AFFINE_COV_CLAMP_VAL);
  val = _mm256_loadu_si256((__m256i *)&mat_a[4]);
  ret[1] = highbd_clamp_epi64(val, AFFINE_COV_CLAMP_VAL, -AFFINE_COV_CLAMP_VAL);
  val = _mm256_loadu_si256((__m256i *)&mat_a[8]);
  ret[2] = highbd_clamp_epi64(val, AFFINE_COV_CLAMP_VAL, -AFFINE_COV_CLAMP_VAL);
  val = _mm256_loadu_si256((__m256i *)&mat_a[12]);
  ret[3] = highbd_clamp_epi64(val, AFFINE_COV_CLAMP_VAL, -AFFINE_COV_CLAMP_VAL);

  val = _mm256_loadu_si256((__m256i *)vec_b);
  ret[4] = highbd_clamp_epi64(val, AFFINE_COV_CLAMP_VAL, -AFFINE_COV_CLAMP_VAL);

  _mm256_storeu_si256((__m256i *)&mat_a[0], ret[0]);
  _mm256_storeu_si256((__m256i *)&mat_a[4], ret[1]);
  _mm256_storeu_si256((__m256i *)&mat_a[8], ret[2]);
  _mm256_storeu_si256((__m256i *)&mat_a[12], ret[3]);

  _mm256_storeu_si256((__m256i *)vec_b, ret[4]);
}

void av1_calc_affine_autocorrelation_matrix_avx2(const int16_t *pdiff,
                                                 int pstride, const int16_t *gx,
                                                 const int16_t *gy, int gstride,
                                                 int bw, int bh, int64_t *mat_a,
                                                 int64_t *vec_b) {
#if OPFL_COMBINE_INTERP_GRAD_LS
#if !OPFL_DOWNSAMP_QUINCUNX && AFFINE_AVERAGING_BITS > 0
  int x_range_log2 = get_msb_signed(bw);
  int y_range_log2 = get_msb_signed(bh);
  int npel_log2 = AOMMIN(7 - AFFINE_AVERAGING_BITS, get_msb_signed(bw)) +
                  AOMMIN(7 - AFFINE_AVERAGING_BITS, get_msb_signed(bh));

  int64_t max_el = find_max_matrix_element_interp_grad(pdiff, pstride, gx, gy,
                                                       gstride, bw, bh);

  int max_diff_bits = get_msb_signed_64(max_el);
  const int grad_bits =
      AOMMAX(0, max_diff_bits * 2 + npel_log2 +
                    AOMMAX(x_range_log2, y_range_log2) - AFFINE_GRAD_BITS_THR);
  const int coords_bits = AOMMAX(
      0, ((x_range_log2 + y_range_log2) >> 1) - AFFINE_COORDS_OFFSET_BITS);

  calc_mat_a_and_vec_b(pdiff, pstride, gx, gy, gstride, bw, bh, coords_bits,
                       grad_bits, mat_a, vec_b);
#else
  av1_calc_affine_autocorrelation_matrix_c(pdiff, pstride, gx, gy, gstride, bw,
                                           bh, mat_a, vec_b);
#endif  // !OPFL_DOWNSAMP_QUINCUNX && AFFINE_AVERAGING_BITS > 0
#else
  (void)pdiff;
  (void)pstride;
  (void)gx;
  (void)gy;
  (void)gstride;
  (void)bw;
  (void)bh;
  (void)mat_a;
  (void)vec_b;
#endif  // OPFL_COMBINE_INTERP_GRAD_LS
}
#endif  // CONFIG_AFFINE_REFINEMENT

#if CONFIG_OPFL_MV_SEARCH || CONFIG_AFFINE_REFINEMENT
static INLINE void avg_pool_pdiff_grad_8_avx2(int16_t *pdiff, const int pstride,
                                              int16_t *gx, int16_t *gy,
                                              const int gstride, const int bh) {
  int avg_stride = 8;
  for (int i = 0; i < bh; i++) {
    __m128i pd0 = _mm_loadu_si128((__m128i *)&pdiff[i * pstride]);
    __m128i gx0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
    __m128i gy0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);

    _mm_storeu_si128((__m128i *)(pdiff + (i * avg_stride)), pd0);
    _mm_storeu_si128((__m128i *)(gx + (i * avg_stride)), gx0);
    _mm_storeu_si128((__m128i *)(gy + (i * avg_stride)), gy0);
  }
}

static INLINE void avg_pool_pdiff_grad_8xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 8;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m128i pd0 = _mm_loadu_si128((__m128i *)&pdiff[i * pstride]);
    __m128i pd1 = _mm_loadu_si128((__m128i *)&pdiff[(i + 1) * pstride]);
    __m128i gx0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
    __m128i gx1 = _mm_loadu_si128((__m128i *)&gx[(i + 1) * gstride]);
    __m128i gy0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);
    __m128i gy1 = _mm_loadu_si128((__m128i *)&gy[(i + 1) * gstride]);

    // Add values of two corresponding rows since scaling step_h=2
    __m256i addpd0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(pd0),
                                      _mm256_cvtepi16_epi32(pd1));
    __m256i addgx0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(gx0),
                                      _mm256_cvtepi16_epi32(gx1));
    __m256i addgy0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(gy0),
                                      _mm256_cvtepi16_epi32(gy1));

    for (int m = 2; m < step_h; m++) {
      pd0 = _mm_loadu_si128((__m128i *)&pdiff[(i + m) * pstride]);
      gx0 = _mm_loadu_si128((__m128i *)&gx[(i + m) * gstride]);
      gy0 = _mm_loadu_si128((__m128i *)&gy[(i + m) * gstride]);

      addpd0 = _mm256_add_epi32(addpd0, _mm256_cvtepi16_epi32(pd0));
      addgx0 = _mm256_add_epi32(addgx0, _mm256_cvtepi16_epi32(gx0));
      addgy0 = _mm256_add_epi32(addgy0, _mm256_cvtepi16_epi32(gy0));
    }

    addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
    addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
    addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);

    _mm_storeu_si128((__m128i *)(pdiff + (k * avg_stride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addpd0),
                                     _mm256_extractf128_si256(addpd0, 1)));
    _mm_storeu_si128((__m128i *)(gx + (k * avg_stride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addgx0),
                                     _mm256_extractf128_si256(addgx0, 1)));
    _mm_storeu_si128((__m128i *)(gy + (k * avg_stride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addgy0),
                                     _mm256_extractf128_si256(addgy0, 1)));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_8xbh_avx2(int16_t *pdiff,
                                                 const int pstride, int16_t *gx,
                                                 int16_t *gy, const int gstride,
                                                 const int bh, int step_w,
                                                 int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_8_avx2(pdiff, pstride, gx, gy, gstride, bh);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_8xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_16_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh) {
  int avg_stride = 16;
  for (int i = 0; i < bh; i++) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);

    _mm256_storeu_si256((__m256i *)(pdiff + (i * avg_stride)), pd0);
    _mm256_storeu_si256((__m256i *)(gx + (i * avg_stride)), gx0);
    _mm256_storeu_si256((__m256i *)(gy + (i * avg_stride)), gy0);
  }
}

static INLINE void avg_pool_pdiff_grad_16xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 16;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd0_lo, pd1_lo, gx0_lo, gx1_lo, gy0_lo, gy1_lo;
  __m256i pd0_hi, pd1_hi, gx0_hi, gx1_hi, gy0_hi, gy1_hi;
  __m256i pd2_lo, gx2_lo, gy2_lo;
  __m256i pd2_hi, gx2_hi, gy2_hi;
  const __m256i zero = _mm256_setzero_si256();
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd0, zero, &pd0_lo, &pd0_hi);
    sign_extend_16bit_to_32bit(pd1, zero, &pd1_lo, &pd1_hi);
    sign_extend_16bit_to_32bit(gx0, zero, &gx0_lo, &gx0_hi);
    sign_extend_16bit_to_32bit(gx1, zero, &gx1_lo, &gx1_hi);
    sign_extend_16bit_to_32bit(gy0, zero, &gy0_lo, &gy0_hi);
    sign_extend_16bit_to_32bit(gy1, zero, &gy1_lo, &gy1_hi);

    // Add values of four corresponding rows since scaling step_h=4
    __m256i addpd_lo = _mm256_add_epi32(pd0_lo, pd1_lo);
    __m256i addpd_hi = _mm256_add_epi32(pd0_hi, pd1_hi);
    __m256i addgx_lo = _mm256_add_epi32(gx0_lo, gx1_lo);
    __m256i addgx_hi = _mm256_add_epi32(gx0_hi, gx1_hi);
    __m256i addgy_lo = _mm256_add_epi32(gy0_lo, gy1_lo);
    __m256i addgy_hi = _mm256_add_epi32(gy0_hi, gy1_hi);

    for (int m = 2; m < step_h; m++) {
      __m256i pd2 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride]);
      __m256i gx2 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride]);
      __m256i gy2 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd2, zero, &pd2_lo, &pd2_hi);
      sign_extend_16bit_to_32bit(gx2, zero, &gx2_lo, &gx2_hi);
      sign_extend_16bit_to_32bit(gy2, zero, &gy2_lo, &gy2_hi);

      // Add values of four corresponding rows since scaling step_h=4
      addpd_lo = _mm256_add_epi32(addpd_lo, pd2_lo);
      addpd_hi = _mm256_add_epi32(addpd_hi, pd2_hi);
      addgx_lo = _mm256_add_epi32(addgx_lo, gx2_lo);
      addgx_hi = _mm256_add_epi32(addgx_hi, gx2_hi);
      addgy_lo = _mm256_add_epi32(addgy_lo, gy2_lo);
      addgy_hi = _mm256_add_epi32(addgy_hi, gy2_hi);
    }

    addpd_lo = round_power_of_two_signed_epi32(addpd_lo, avg_bits);
    addpd_hi = round_power_of_two_signed_epi32(addpd_hi, avg_bits);
    addgx_lo = round_power_of_two_signed_epi32(addgx_lo, avg_bits);
    addgx_hi = round_power_of_two_signed_epi32(addgx_hi, avg_bits);
    addgy_lo = round_power_of_two_signed_epi32(addgy_lo, avg_bits);
    addgy_hi = round_power_of_two_signed_epi32(addgy_hi, avg_bits);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * avg_stride)),
                        _mm256_packs_epi32(addpd_lo, addpd_hi));
    _mm256_storeu_si256((__m256i *)(gx + (k * avg_stride)),
                        _mm256_packs_epi32(addgx_lo, addgx_hi));
    _mm256_storeu_si256((__m256i *)(gy + (k * avg_stride)),
                        _mm256_packs_epi32(addgy_lo, addgy_hi));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_16xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_16_avx2(pdiff, pstride, gx, gy, gstride, bh);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_16xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_32_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh, int step_w,
                                               int step_h) {
  int avg_stride = 32;
  int k = 0;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);
  __m256i one_mask = _mm256_set1_epi16(1);
  for (int i = 0; i < bh; i++) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[(i * pstride) + 16]);
    __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[(i * gstride) + 16]);
    __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[(i * gstride) + 16]);

    __m256i addpd0 = _mm256_madd_epi16(pd0, one_mask);
    __m256i addgx0 = _mm256_madd_epi16(gx0, one_mask);
    __m256i addgy0 = _mm256_madd_epi16(gy0, one_mask);
    __m256i addpd1 = _mm256_madd_epi16(pd1, one_mask);
    __m256i addgx1 = _mm256_madd_epi16(gx1, one_mask);
    __m256i addgy1 = _mm256_madd_epi16(gy1, one_mask);

    addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
    addpd1 = round_power_of_two_signed_epi32(addpd1, avg_bits);
    addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
    addgx1 = round_power_of_two_signed_epi32(addgx1, avg_bits);
    addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);
    addgy1 = round_power_of_two_signed_epi32(addgy1, avg_bits);

    addpd0 = _mm256_packs_epi32(addpd0, addpd1);
    addgx0 = _mm256_packs_epi32(addgx0, addgx1);
    addgy0 = _mm256_packs_epi32(addgy0, addgy1);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addpd0, reg_mask));
    _mm256_storeu_si256((__m256i *)(gx + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addgx0, reg_mask));
    _mm256_storeu_si256((__m256i *)(gy + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addgy0, reg_mask));

    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_32xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 32;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i pd10 = _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride]);
    __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride]);
    __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd00, zero, &pd00_lo, &pd00_hi);
    sign_extend_16bit_to_32bit(pd10, zero, &pd10_lo, &pd10_hi);
    sign_extend_16bit_to_32bit(gx00, zero, &gx00_lo, &gx00_hi);
    sign_extend_16bit_to_32bit(gx10, zero, &gx10_lo, &gx10_hi);
    sign_extend_16bit_to_32bit(gy00, zero, &gy00_lo, &gy00_hi);
    sign_extend_16bit_to_32bit(gy10, zero, &gy10_lo, &gy10_hi);

    // Add values of four corresponding rows since scaling step_h=4
    __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
    __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
    __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
    __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
    __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
    __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

    __m256i pd01 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16]);
    __m256i pd11 =
        _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16]);
    __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16]);
    __m256i gx11 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16]);
    __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16]);
    __m256i gy11 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd01, zero, &pd01_lo, &pd01_hi);
    sign_extend_16bit_to_32bit(pd11, zero, &pd11_lo, &pd11_hi);
    sign_extend_16bit_to_32bit(gx01, zero, &gx01_lo, &gx01_hi);
    sign_extend_16bit_to_32bit(gx11, zero, &gx11_lo, &gx11_hi);
    sign_extend_16bit_to_32bit(gy01, zero, &gy01_lo, &gy01_hi);
    sign_extend_16bit_to_32bit(gy11, zero, &gy11_lo, &gy11_hi);

    __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
    __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
    __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
    __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
    __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
    __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

    for (int m = 2; m < step_h; m++) {
      pd00 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride]);
      gx00 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride]);
      gy00 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero, &gy00_lo, &gy00_hi);

      // Add values of four corresponding rows since scaling step_h=4
      addpd0_lo = _mm256_add_epi32(addpd0_lo, pd00_lo);
      addpd0_hi = _mm256_add_epi32(addpd0_hi, pd00_hi);
      addgx0_lo = _mm256_add_epi32(addgx0_lo, gx00_lo);
      addgx0_hi = _mm256_add_epi32(addgx0_hi, gx00_hi);
      addgy0_lo = _mm256_add_epi32(addgy0_lo, gy00_lo);
      addgy0_hi = _mm256_add_epi32(addgy0_hi, gy00_hi);

      pd01 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16]);
      gx01 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16]);
      gy01 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero, &gy01_lo, &gy01_hi);

      addpd1_lo = _mm256_add_epi32(addpd1_lo, pd01_lo);
      addpd1_hi = _mm256_add_epi32(addpd1_hi, pd01_hi);
      addgx1_lo = _mm256_add_epi32(addgx1_lo, gx01_lo);
      addgx1_hi = _mm256_add_epi32(addgx1_hi, gx01_hi);
      addgy1_lo = _mm256_add_epi32(addgy1_lo, gy01_lo);
      addgy1_hi = _mm256_add_epi32(addgy1_hi, gy01_hi);
    }

    addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
    addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
    addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
    addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
    addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
    addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

    addpd0_hi = round_power_of_two_signed_epi32(addpd0_lo, avg_bits);
    addpd1_hi = round_power_of_two_signed_epi32(addpd1_lo, avg_bits);
    addgx0_hi = round_power_of_two_signed_epi32(addgx0_lo, avg_bits);
    addgx1_hi = round_power_of_two_signed_epi32(addgx1_lo, avg_bits);
    addgy0_hi = round_power_of_two_signed_epi32(addgy0_lo, avg_bits);
    addgy1_hi = round_power_of_two_signed_epi32(addgy1_lo, avg_bits);

    addpd0_lo = _mm256_packs_epi32(addpd0_hi, addpd1_hi);
    addgx0_lo = _mm256_packs_epi32(addgx0_hi, addgx1_hi);
    addgy0_lo = _mm256_packs_epi32(addgy0_hi, addgy1_hi);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask));
    _mm256_storeu_si256((__m256i *)(gx + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask));
    _mm256_storeu_si256((__m256i *)(gy + (k * avg_stride)),
                        _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_32xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                step_h);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_32xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_64_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh, int step_w,
                                               int step_h) {
  int avg_stride = 64;
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i reg_mask = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
  __m256i one_mask = _mm256_set1_epi16(1);
  k = 0;
  for (int i = 0; i < bh; i++) {
    l = 0;
    for (int j = 0; j < 64; j += 32) {
      __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      __m256i addpd0 = _mm256_madd_epi16(pd0, one_mask);
      __m256i addpd2 = _mm256_madd_epi16(pd1, one_mask);
      __m256i addgx0 = _mm256_madd_epi16(gx0, one_mask);
      __m256i addgx2 = _mm256_madd_epi16(gx1, one_mask);
      __m256i addgy0 = _mm256_madd_epi16(gy0, one_mask);
      __m256i addgy2 = _mm256_madd_epi16(gy1, one_mask);

      addpd0 = _mm256_hadd_epi32(addpd0, addpd2);
      addgx0 = _mm256_hadd_epi32(addgx0, addgx2);
      addgy0 = _mm256_hadd_epi32(addgy0, addgy2);

      addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
      addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
      addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);

      _mm_storeu_si128((__m128i *)(pdiff + (k * avg_stride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addpd0, addpd0), reg_mask)));
      _mm_storeu_si128((__m128i *)(gx + (k * avg_stride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addgx0, addgx0), reg_mask)));
      _mm_storeu_si128((__m128i *)(gy + (k * avg_stride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addgy0, addgy0), reg_mask)));
      l += 8;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_64xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 64;
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 64; j += 32) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;
        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd0_hi = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_hi = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_hi = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storeu_si128(
          (__m128i *)(pdiff + (k * avg_stride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addpd0_hi, addpd0_hi), reg_mask)));
      _mm_storeu_si128(
          (__m128i *)(gx + (k * avg_stride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addgx0_hi, addgx0_hi), reg_mask)));
      _mm_storeu_si128(
          (__m128i *)(gy + (k * avg_stride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addgy0_hi, addgy0_hi), reg_mask)));
      l += 8;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_64xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_64_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                step_h);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_64xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_128xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 128;
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 1, 4, 0);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 128; j += 32) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;
        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd0_lo = _mm256_hadd_epi32(addpd0_hi, addpd0_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_hi, addgx0_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_hi, addgy0_hi);

      addpd0_hi = _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask);
      addgx0_hi = _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask);
      addgy0_hi = _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask);

      addpd0_lo = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_lo = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_lo = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storel_epi64((__m128i *)(pdiff + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addpd0_lo),
                                       _mm256_castsi256_si128(addpd0_lo)));
      _mm_storel_epi64((__m128i *)(gx + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgx0_lo),
                                       _mm256_castsi256_si128(addgx0_lo)));
      _mm_storel_epi64((__m128i *)(gy + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgy0_lo),
                                       _mm256_castsi256_si128(addgy0_lo)));
      l += 4;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_256xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_stride = 256;
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);

  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(3, 2, 1, 0, 7, 6, 5, 4);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 256; j += 64) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      __m256i pd02 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 32 + j]);
      __m256i pd12 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 32 + j]);
      __m256i gx02 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 32 + j]);
      __m256i gx12 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 32 + j]);
      __m256i gy02 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 32 + j]);
      __m256i gy12 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 32 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd02, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx02, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy02, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd12, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx12, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy12, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd2_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd2_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx2_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx2_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy2_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy2_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd03 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 48 + j]);
      __m256i pd13 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 48 + j]);
      __m256i gx03 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 48 + j]);
      __m256i gx13 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 48 + j]);
      __m256i gy03 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 48 + j]);
      __m256i gy13 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 48 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd03, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx03, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy03, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd13, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx13, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy13, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd3_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd3_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx3_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx3_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy3_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy3_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;

        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);

        __m256i pd22 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 32 + j]);
        __m256i gx22 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 32 + j]);
        __m256i gy22 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 32 + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd22, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx22, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy22, zero_mask, &gy20_lo, &gy20_hi);

        addpd2_lo = _mm256_add_epi32(addpd2_lo, pd20_lo);
        addpd2_hi = _mm256_add_epi32(addpd2_hi, pd20_hi);
        addgx2_lo = _mm256_add_epi32(addgx2_lo, gx20_lo);
        addgx2_hi = _mm256_add_epi32(addgx2_hi, gx20_hi);
        addgy2_lo = _mm256_add_epi32(addgy2_lo, gy20_lo);
        addgy2_hi = _mm256_add_epi32(addgy2_hi, gy20_hi);

        __m256i pd23 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 48 + j]);
        __m256i gx23 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 48 + j]);
        __m256i gy23 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 48 + j]);
        sign_extend_16bit_to_32bit(pd23, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx23, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy23, zero_mask, &gy21_lo, &gy21_hi);

        addpd3_lo = _mm256_add_epi32(addpd3_lo, pd21_lo);
        addpd3_hi = _mm256_add_epi32(addpd3_hi, pd21_hi);
        addgx3_lo = _mm256_add_epi32(addgx3_lo, gx21_lo);
        addgx3_hi = _mm256_add_epi32(addgx3_hi, gx21_hi);
        addgy3_lo = _mm256_add_epi32(addgy3_lo, gy21_lo);
        addgy3_hi = _mm256_add_epi32(addgy3_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd2_lo = _mm256_hadd_epi32(addpd2_lo, addpd2_hi);
      addpd3_lo = _mm256_hadd_epi32(addpd3_lo, addpd3_hi);
      addgx2_lo = _mm256_hadd_epi32(addgx2_lo, addgx2_hi);
      addgx3_lo = _mm256_hadd_epi32(addgx3_lo, addgx3_hi);
      addgy2_lo = _mm256_hadd_epi32(addgy2_lo, addgy2_hi);
      addgy3_lo = _mm256_hadd_epi32(addgy3_lo, addgy3_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd2_hi = _mm256_hadd_epi32(addpd2_lo, addpd3_lo);
      addgx2_hi = _mm256_hadd_epi32(addgx2_lo, addgx3_lo);
      addgy2_hi = _mm256_hadd_epi32(addgy2_lo, addgy3_lo);

      addpd0_lo = _mm256_hadd_epi32(addpd0_hi, addpd2_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_hi, addgx2_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_hi, addgy2_hi);

      addpd0_hi = _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask);
      addgx0_hi = _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask);
      addgy0_hi = _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask);

      addpd0_hi = _mm256_add_epi32(addpd0_hi, addpd0_lo);
      addgx0_hi = _mm256_add_epi32(addgx0_hi, addgx0_lo);
      addgy0_hi = _mm256_add_epi32(addgy0_hi, addgy0_lo);

      addpd0_lo = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_lo = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_lo = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storel_epi64((__m128i *)(pdiff + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addpd0_lo),
                                       _mm256_castsi256_si128(addpd0_lo)));
      _mm_storel_epi64((__m128i *)(gx + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgx0_lo),
                                       _mm256_castsi256_si128(addgx0_lo)));
      _mm_storel_epi64((__m128i *)(gy + (k * avg_stride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgy0_lo),
                                       _mm256_castsi256_si128(addgy0_lo)));
      l += 4;
    }
    k++;
  }
}

void av1_avg_pooling_pdiff_gradients_avx2(int16_t *pdiff, const int pstride,
                                          int16_t *gx, int16_t *gy,
                                          const int gstride, const int bw,
                                          const int bh, const int n) {
#if CONFIG_OPFL_MV_SEARCH || \
    (AFFINE_AVERAGING_BITS > 0 && OPFL_COMBINE_INTERP_GRAD_LS)
#if !OPFL_DOWNSAMP_QUINCUNX
  const int bh_low = AOMMIN(bh, n);
  const int bw_low = AOMMIN(bw, n);
  const int step_h = bh / bh_low;
  const int step_w = bw / bw_low;
  if (bw == 8) {
    avg_pool_pdiff_grad_8xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                  step_h);
  } else if (bw == 16) {
    avg_pool_pdiff_grad_16xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 32) {
    avg_pool_pdiff_grad_32xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 64) {
    avg_pool_pdiff_grad_64xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 128) {
    avg_pool_pdiff_grad_128xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  } else if (bw == 256) {
    avg_pool_pdiff_grad_256xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  } else {
    av1_avg_pooling_pdiff_gradients_c(pdiff, pstride, gx, gy, gstride, bw, bh,
                                      n);
  }
#else
  av1_avg_pooling_pdiff_gradients_c(pdiff, pstride, gx, gy, gstride, bw, bh, n);
#endif  // !OPFL_DOWNSAMP_QUINCUNX
#else
  (void)pdiff;
  (void)pstride;
  (void)gx;
  (void)gy;
  (void)gstride;
  (void)bw;
  (void)bh;
  (void)n;
#endif  // CONFIG_OPFL_MV_SEARCH || (AFFINE_AVERAGING_BITS > 0 &&
        // OPFL_COMBINE_INTERP_GRAD_LS)
}
#endif  // CONFIG_OPFL_MV_SEARCH || CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_OPTFLOW_REFINEMENT
