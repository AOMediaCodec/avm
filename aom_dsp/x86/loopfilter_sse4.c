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

#include <smmintrin.h>
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/loopfilter.h"

// Helper function: ROUND_POWER_OF_TWO for SIMD 32-bit integers (SSE version)
static inline __m128i mm_round_power_of_two_epi32_sse(__m128i value, int n) {
  if (n <= 0) return value;
  __m128i offset = _mm_set1_epi32((1 << (n - 1)));
  __m128i summed = _mm_add_epi32(value, offset);
  return _mm_srai_epi32(summed, n);
}

// Helper function: clip_pixel_highbd for SIMD 16-bit unsigned integers (SSE
// version)
static inline __m128i mm_clip_pixel_highbd_epu16_sse(__m128i pixels, int bd) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i max_val = _mm_set1_epi16((1 << bd) - 1);
  __m128i clipped_at_min = _mm_max_epi16(pixels, zero);
  __m128i clipped_at_max = _mm_min_epi16(clipped_at_min, max_val);
  return clipped_at_max;
}

// Helper function: clip_pixel_highbd for SIMD 32-bit unsigned integers (SSE
// version)
static inline __m128i mm_clip_pixel_highbd_epu32_sse(__m128i pixels, int bd) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i max_val = _mm_set1_epi32((1 << bd) - 1);
  __m128i clipped_at_min = _mm_max_epi32(pixels, zero);
  __m128i clipped_at_max = _mm_min_epi32(clipped_at_min, max_val);
  return clipped_at_max;
}

static void filt_generic_asym_highbd_hor_4px_sse4_1(int q_threshold,
                                                    int filter_len_neg,
                                                    int filter_len_pos,
                                                    uint16_t *src,
                                                    const int stride, int bd) {
  if (filter_len_neg < 1 || filter_len_pos < 1) {
    return;
  }
  int filter_len = AOMMAX(filter_len_neg, filter_len_pos);

  // --- Calculate delta_m2 for 4 horizontal pixels ---
  // Load 4 uint16_t pixels (64 bits) into the lower part of XMM registers.
  // These represent src[0,1,2,3], (src-stride)[0,1,2,3], etc.
  __m128i xmm_src_p0_64 = _mm_loadl_epi64((__m128i const *)(src));
  __m128i xmm_src_m1s_64 = _mm_loadl_epi64((__m128i const *)(src - stride));
  __m128i xmm_src_p1s_64 = _mm_loadl_epi64((__m128i const *)(src + stride));
  __m128i xmm_src_m2s_64 = _mm_loadl_epi64((__m128i const *)(src - 2 * stride));

  // Convert 4 uint16_t values to 4 int32_t values.
  // _mm_cvtepu16_epi32 (SSE4.1) operates on the lower 64 bits of the source.
  __m128i xmm_s0 = _mm_cvtepu16_epi32(xmm_src_p0_64);
  __m128i xmm_sm1 = _mm_cvtepu16_epi32(xmm_src_m1s_64);
  __m128i xmm_sp1 = _mm_cvtepu16_epi32(xmm_src_p1s_64);
  __m128i xmm_sm2 = _mm_cvtepu16_epi32(xmm_src_m2s_64);

  // Constants for calculation: 3 and 4
  __m128i xmm_three = _mm_set1_epi32(3);
  __m128i xmm_four = _mm_set1_epi32(4);

  // delta_m2_tmp = (3 * (src[0] - src[-stride]) - (src[stride] -
  // src[-2*stride]))
  __m128i xmm_diff1 = _mm_sub_epi32(xmm_s0, xmm_sm1);
  __m128i xmm_term1 = _mm_mullo_epi32(xmm_three, xmm_diff1);  // SSE2
  __m128i xmm_diff2 = _mm_sub_epi32(xmm_sp1, xmm_sm2);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);
  __m128i xmm_delta_m2 = _mm_mullo_epi32(xmm_delta_m2_tmp, xmm_four);

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  // SSE4.1: _mm_min_epi32, _mm_max_epi32
  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  int scalar_w_mult_pos = w_mult[filter_len_pos - 1];

  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);

  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);
  __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

  // --- Inner loops: Apply modifications ---

  // Negative side filtering (pixels above)
  for (int row = 0; row < filter_len_neg; ++row) {
    uint16_t *ptr_current_row_neg = src + (-row - 1) * stride;
    __m128i xmm_src_pixels_neg_64 =
        _mm_loadl_epi64((__m128i const *)ptr_current_row_neg);
    __m128i xmm_src_neg = _mm_cvtepu16_epi32(xmm_src_pixels_neg_64);

    __m128i xmm_filter_coeff = _mm_set1_epi32(filter_len_neg - row);
    __m128i xmm_adj_term_neg =
        _mm_mullo_epi32(xmm_delta_m2_neg, xmm_filter_coeff);
    xmm_adj_term_neg =
        mm_round_power_of_two_epi32_sse(xmm_adj_term_neg, 3 + DF_SHIFT);
    __m128i xmm_result_neg = _mm_add_epi32(xmm_src_neg, xmm_adj_term_neg);

    // Pack 4 int32_t results back to 4 uint16_t with saturation.
    // Resulting 4 uint16_t will be in the lower 64 bits of
    // xmm_final_result_neg_epi16.
    __m128i xmm_final_result_neg_epi16 =
        _mm_packus_epi32(xmm_result_neg, _mm_setzero_si128());  // SSE2

    // Clip the 4 uint16_t pixels (lower 64 bits)
    xmm_final_result_neg_epi16 =
        mm_clip_pixel_highbd_epu16_sse(xmm_final_result_neg_epi16, bd);

    // Store the lower 64 bits (4 uint16_t pixels)
    _mm_storel_epi64((__m128i *)ptr_current_row_neg,
                     xmm_final_result_neg_epi16);
  }

  // Positive side filtering (pixels at and below)
  for (int row = 0; row < filter_len_pos; ++row) {
    uint16_t *ptr_current_row_pos = src + row * stride;
    __m128i xmm_src_pixels_pos_64 =
        _mm_loadl_epi64((__m128i const *)ptr_current_row_pos);
    __m128i xmm_src_pos = _mm_cvtepu16_epi32(xmm_src_pixels_pos_64);

    __m128i xmm_filter_coeff = _mm_set1_epi32(filter_len_pos - row);
    __m128i xmm_adj_term_pos =
        _mm_mullo_epi32(xmm_delta_m2_pos, xmm_filter_coeff);
    xmm_adj_term_pos =
        mm_round_power_of_two_epi32_sse(xmm_adj_term_pos, 3 + DF_SHIFT);
    __m128i xmm_result_pos = _mm_sub_epi32(xmm_src_pos, xmm_adj_term_pos);

    // Pack and clip
    __m128i xmm_final_result_pos_epi16 =
        _mm_packus_epi32(xmm_result_pos, _mm_setzero_si128());
    xmm_final_result_pos_epi16 =
        mm_clip_pixel_highbd_epu16_sse(xmm_final_result_pos_epi16, bd);

    // Store the lower 64 bits (4 uint16_t pixels)
    _mm_storel_epi64((__m128i *)ptr_current_row_pos,
                     xmm_final_result_pos_epi16);
  }
}

// Transpose a 4x4 matrix of __m128i registers where each holds 4 epi32
// elements. Input: r0, r1, r2, r3 are rows. Output: c0, c1, c2, c3 are columns.
static inline void transpose_4x4_epi32(__m128i r0, __m128i r1, __m128i r2,
                                       __m128i r3, __m128i *c0, __m128i *c1,
                                       __m128i *c2, __m128i *c3) {
  __m128i tmp0 = _mm_unpacklo_epi32(r0, r1);  // [r0e0, r1e0, r0e1, r1e1]
  __m128i tmp1 = _mm_unpacklo_epi32(r2, r3);  // [r2e0, r3e0, r2e1, r3e1]
  __m128i tmp2 = _mm_unpackhi_epi32(r0, r1);  // [r0e2, r1e2, r0e3, r1e3]
  __m128i tmp3 = _mm_unpackhi_epi32(r2, r3);  // [r2e2, r3e2, r2e3, r3e3]

  *c0 = _mm_unpacklo_epi64(tmp0, tmp1);  // [r0e0, r1e0, r2e0, r3e0] (Column 0)
  *c1 = _mm_unpackhi_epi64(tmp0, tmp1);  // [r0e1, r1e1, r2e1, r3e1] (Column 1)
  *c2 = _mm_unpacklo_epi64(tmp2, tmp3);  // [r0e2, r1e2, r2e2, r3e2] (Column 2)
  *c3 = _mm_unpackhi_epi64(tmp2, tmp3);  // [r0e3, r1e3, r2e3, r3e3] (Column 3)
}

// Transpose a 4x4 matrix of __m128i registers where each holds 4 epi16
// elements. Input: r0, r1, r2, r3 are rows. Output: c01, c23 are columns.
static inline void transpose_4x4_epi16_to_epi32(__m128i r0, __m128i r1,
                                                __m128i r2, __m128i r3,
                                                __m128i *c0, __m128i *c1,
                                                __m128i *c2, __m128i *c3) {
  __m128i tmp0 = _mm_unpacklo_epi16(r0, r1);  // [r0e0, r1e0, r0e1, r1e1]
  __m128i tmp1 = _mm_unpacklo_epi16(r2, r3);  // [r2e0, r3e0, r2e1, r3e1]

  __m128i c01 = _mm_unpacklo_epi32(tmp0, tmp1);  // Column 0 and 1
  __m128i c23 = _mm_unpackhi_epi32(tmp0, tmp1);  // Column 2 and 3

  *c0 = _mm_cvtepu16_epi32(c01);
  *c1 = _mm_cvtepu16_epi32(_mm_srli_si128(c01, 8));
  *c2 = _mm_cvtepu16_epi32(c23);
  *c3 = _mm_cvtepu16_epi32(_mm_srli_si128(c23, 8));
}
static inline void transpose_4x4_epi32_to_packed_epi16(__m128i r0, __m128i r1,
                                                       __m128i r2, __m128i r3,
                                                       __m128i *c01,
                                                       __m128i *c23) {
  __m128i packed_01 = _mm_packs_epi32(r0, r1);
  __m128i packed_23 = _mm_packs_epi32(r2, r3);

  __m128i tmp0 = _mm_unpacklo_epi16(packed_01, _mm_srli_si128(packed_01, 8));
  __m128i tmp1 = _mm_unpacklo_epi16(packed_23, _mm_srli_si128(packed_23, 8));

  *c01 = _mm_unpacklo_epi32(tmp0, tmp1);  // Column 0 and 1
  *c23 = _mm_unpackhi_epi32(tmp0, tmp1);  // Column 2 and 3
}

static INLINE void filt_generic_asym_highbd_ver_4px_sse4_1(
    int q_threshold, int filter_len_neg, int filter_len_pos, uint16_t *src,
    const int stride, int bd) {
  if (filter_len_neg < 1 || filter_len_pos < 1) {
    return;
  }
  int filter_len = AOMMAX(filter_len_neg, filter_len_pos);

  uint16_t *row_ptr[4];
  row_ptr[0] = src;
  row_ptr[1] = src + stride;
  row_ptr[2] = src + 2 * stride;
  row_ptr[3] = src + 3 * stride;

  __m128i xmm_L_r0 = _mm_loadl_epi64((__m128i const *)(row_ptr[0] - 2));
  __m128i xmm_L_r1 = _mm_loadl_epi64((__m128i const *)(row_ptr[1] - 2));
  __m128i xmm_L_r2 = _mm_loadl_epi64((__m128i const *)(row_ptr[2] - 2));
  __m128i xmm_L_r3 = _mm_loadl_epi64((__m128i const *)(row_ptr[3] - 2));

  // Convert to 4 registers of 4x int32_t values each
  __m128i r0_epi32 = _mm_cvtepu16_epi32(xmm_L_r0);
  __m128i r1_epi32 = _mm_cvtepu16_epi32(xmm_L_r1);
  __m128i r2_epi32 = _mm_cvtepu16_epi32(xmm_L_r2);
  __m128i r3_epi32 = _mm_cvtepu16_epi32(xmm_L_r3);

  // Transpose these 4 registers to get columns of pixel data
  __m128i xmm_sm2, xmm_sm1, xmm_s0, xmm_sp1;
  transpose_4x4_epi32(r0_epi32, r1_epi32, r2_epi32, r3_epi32, &xmm_sm2,
                      &xmm_sm1, &xmm_s0, &xmm_sp1);

  // Constants for calculation: 3 and 4
  __m128i xmm_three = _mm_set1_epi32(3);
  __m128i xmm_four = _mm_set1_epi32(4);

  // delta_m2_tmp = (3 * (src[0] - src[-stride]) - (src[stride] -
  // src[-2*stride]))
  __m128i xmm_diff1 = _mm_sub_epi32(xmm_s0, xmm_sm1);
  __m128i xmm_term1 = _mm_mullo_epi32(xmm_three, xmm_diff1);  // SSE2
  __m128i xmm_diff2 = _mm_sub_epi32(xmm_sp1, xmm_sm2);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);
  __m128i xmm_delta_m2 = _mm_mullo_epi32(xmm_delta_m2_tmp, xmm_four);

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  // SSE4.1: _mm_min_epi32, _mm_max_epi32
  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  int scalar_w_mult_pos = w_mult[filter_len_pos - 1];

  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);

  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);
  __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

  DECLARE_ALIGNED(16, int32_t, delta_m2_neg[4]);
  DECLARE_ALIGNED(16, int32_t, delta_m2_pos[4]);

  _mm_store_si128((__m128i *)delta_m2_neg, xmm_delta_m2_neg);
  _mm_store_si128((__m128i *)delta_m2_pos, xmm_delta_m2_pos);

  /* adjustment by sse4*/
  // Find the adjustment values
  __m128i xmm_coeff_neg_lo, xmm_coeff_neg_hi;

  __m128i xmm_filt_len_neg = _mm_set1_epi16(filter_len_neg);
  __m128i xmm_coeff_neg =
      _mm_subs_epu16(xmm_filt_len_neg, _mm_set_epi16(0, 1, 2, 3, 4, 5, 6, 7));

  xmm_coeff_neg_lo = _mm_cvtepu16_epi32(xmm_coeff_neg);
  xmm_coeff_neg_hi = _mm_cvtepu16_epi32(_mm_srli_si128(xmm_coeff_neg, 8));

  __m128i xmm_coeff_pos_lo, xmm_coeff_pos_hi;

  __m128i xmm_filt_len_pos = _mm_set1_epi16(filter_len_pos);
  __m128i xmm_coeff_pos =
      _mm_subs_epu16(xmm_filt_len_pos, _mm_set_epi16(7, 6, 5, 4, 3, 2, 1, 0));

  xmm_coeff_pos_lo = _mm_cvtepu16_epi32(xmm_coeff_pos);
  xmm_coeff_pos_hi = _mm_cvtepu16_epi32(_mm_srli_si128(xmm_coeff_pos, 8));

  // --- Inner loops: Apply modifications ---
  uint16_t *s = src;
  for (int row = 0; row < 4; ++row) {
    int delta_m2_neg_val = delta_m2_neg[row];
    int delta_m2_pos_val = delta_m2_pos[row];

    /* calculate src by sse4 */
    // negative
    __m128i xmm_delta_m2_neg_row = _mm_set1_epi32(delta_m2_neg_val);
    __m128i xmm_adj_neg_hi =
        _mm_mullo_epi32(xmm_delta_m2_neg_row, xmm_coeff_neg_hi);
    xmm_adj_neg_hi =
        mm_round_power_of_two_epi32_sse(xmm_adj_neg_hi, 3 + DF_SHIFT);

    __m128i xmm_adj_neg_lo =
        _mm_mullo_epi32(xmm_delta_m2_neg_row, xmm_coeff_neg_lo);
    xmm_adj_neg_lo =
        mm_round_power_of_two_epi32_sse(xmm_adj_neg_lo, 3 + DF_SHIFT);

    __m128i xmm_adj_neg = _mm_packs_epi32(xmm_adj_neg_lo, xmm_adj_neg_hi);

    // Load 8 pixels
    __m128i xmm_src_neg8 = _mm_loadu_si128((__m128i *)(s - 8));

    // Add and clip
    xmm_src_neg8 = _mm_add_epi16(xmm_src_neg8, xmm_adj_neg);
    xmm_src_neg8 = mm_clip_pixel_highbd_epu16_sse(xmm_src_neg8, bd);
    // Store
    _mm_storeu_si128((__m128i *)(s - 8), xmm_src_neg8);
    /**/

    /* calculate src by sse4 */
    // positive
    __m128i xmm_delta_m2_pos_row = _mm_set1_epi32(delta_m2_pos_val);
    __m128i xmm_adj_pos_lo =
        _mm_mullo_epi32(xmm_delta_m2_pos_row, xmm_coeff_pos_lo);
    xmm_adj_pos_lo =
        mm_round_power_of_two_epi32_sse(xmm_adj_pos_lo, 3 + DF_SHIFT);

    // Load 8 pixels
    __m128i xmm_src_pos8 = _mm_loadu_si128((__m128i *)s);
    __m128i xmm_adj_pos_hi =
        _mm_mullo_epi32(xmm_delta_m2_pos_row, xmm_coeff_pos_hi);
    xmm_adj_pos_hi =
        mm_round_power_of_two_epi32_sse(xmm_adj_pos_hi, 3 + DF_SHIFT);

    __m128i xmm_adj_pos = _mm_packs_epi32(xmm_adj_pos_lo, xmm_adj_pos_hi);

    // Add and clip
    xmm_src_pos8 = _mm_sub_epi16(xmm_src_pos8, xmm_adj_pos);
    xmm_src_pos8 = mm_clip_pixel_highbd_epu16_sse(xmm_src_pos8, bd);
    // Store
    _mm_storeu_si128((__m128i *)s, xmm_src_pos8);
    /* */

    s += stride;
  }
}

static INLINE void transpose_filt_generic_asym_highbd_ver_4px_sse4_1(
    int q_threshold, int filter_len_neg, int filter_len_pos, uint16_t *src,
    const int stride, int bd) {
  if (filter_len_neg < 1 || filter_len_pos < 1) {
    return;
  }
  int filter_len = AOMMAX(filter_len_neg, filter_len_pos);

  // Transpose the rows into columns
  __m128i xmm_transposed_cols_32[16];

  int num_neg = (filter_len_neg + 3) / 4 * 4;
  int num_pos = (filter_len_pos + 3) / 4 * 4;

  for (int col = -num_neg; col < num_pos; col += 4) {
    __m128i xmm_r0 = _mm_loadl_epi64((__m128i *)(src + col));
    __m128i xmm_r1 = _mm_loadl_epi64((__m128i *)(src + stride + col));
    __m128i xmm_r2 = _mm_loadl_epi64((__m128i *)(src + 2 * stride + col));
    __m128i xmm_r3 = _mm_loadl_epi64((__m128i *)(src + 3 * stride + col));

    transpose_4x4_epi16_to_epi32(
        xmm_r0, xmm_r1, xmm_r2, xmm_r3, &xmm_transposed_cols_32[col + 8],
        &xmm_transposed_cols_32[col + 9], &xmm_transposed_cols_32[col + 10],
        &xmm_transposed_cols_32[col + 11]);
  }

  // Constants for calculation: 3 and 4
  __m128i xmm_three = _mm_set1_epi32(3);
  __m128i xmm_four = _mm_set1_epi32(4);

  // delta_m2_tmp = (3 * (src[0] - src[-stride]) - (src[stride] -
  // src[-2*stride]))
  __m128i xmm_diff1 =
      _mm_sub_epi32(xmm_transposed_cols_32[8], xmm_transposed_cols_32[7]);
  __m128i xmm_term1 = _mm_mullo_epi32(xmm_three, xmm_diff1);  // SSE2
  __m128i xmm_diff2 =
      _mm_sub_epi32(xmm_transposed_cols_32[9], xmm_transposed_cols_32[6]);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);
  __m128i xmm_delta_m2 = _mm_mullo_epi32(xmm_delta_m2_tmp, xmm_four);

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  // SSE4.1: _mm_min_epi32, _mm_max_epi32
  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  int scalar_w_mult_pos = w_mult[filter_len_pos - 1];

  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);

  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);
  __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

  // --- Inner loops: Apply modifications ---

  // Negative side filtering
  for (int col = 0; col < filter_len_neg; ++col) {
    __m128i xmm_filter_coeff = _mm_set1_epi32(filter_len_neg - col);
    __m128i xmm_adj_term_neg =
        _mm_mullo_epi32(xmm_delta_m2_neg, xmm_filter_coeff);
    xmm_adj_term_neg =
        mm_round_power_of_two_epi32_sse(xmm_adj_term_neg, 3 + DF_SHIFT);
    __m128i xmm_result_neg =
        _mm_add_epi32(xmm_transposed_cols_32[8 - col - 1], xmm_adj_term_neg);

    xmm_transposed_cols_32[8 - col - 1] =
        mm_clip_pixel_highbd_epu32_sse(xmm_result_neg, bd);
  }

  // Positive side filtering
  for (int col = 0; col < filter_len_pos; ++col) {
    __m128i xmm_filter_coeff = _mm_set1_epi32(filter_len_pos - col);
    __m128i xmm_adj_term_pos =
        _mm_mullo_epi32(xmm_delta_m2_pos, xmm_filter_coeff);
    xmm_adj_term_pos =
        mm_round_power_of_two_epi32_sse(xmm_adj_term_pos, 3 + DF_SHIFT);
    __m128i xmm_result_pos =
        _mm_sub_epi32(xmm_transposed_cols_32[8 + col], xmm_adj_term_pos);

    xmm_transposed_cols_32[8 + col] =
        mm_clip_pixel_highbd_epu32_sse(xmm_result_pos, bd);
  }

  for (int col = -num_neg; col < num_pos; col += 4) {
    __m128i xmm_r01, xmm_r23;
    transpose_4x4_epi32_to_packed_epi16(
        xmm_transposed_cols_32[col + 8], xmm_transposed_cols_32[col + 9],
        xmm_transposed_cols_32[col + 10], xmm_transposed_cols_32[col + 11],
        &xmm_r01, &xmm_r23);

    _mm_storel_epi64((__m128i *)(src + col), xmm_r01);
    _mm_storel_epi64((__m128i *)(src + stride + col),
                     _mm_srli_si128(xmm_r01, 8));
    _mm_storel_epi64((__m128i *)(src + 2 * stride + col), xmm_r23);
    _mm_storel_epi64((__m128i *)(src + 3 * stride + col),
                     _mm_srli_si128(xmm_r23, 8));
  }
}

void aom_highbd_lpf_horizontal_generic_sse4_1(uint16_t *s, int pitch,
#if CONFIG_ASYM_DF
                                              int filt_width_neg,
                                              int filt_width_pos,
#else
                                              int filt_width,
#endif
                                              const uint16_t *q_thresh,
                                              const uint16_t *side_thresh,
                                              int bd
#if CONFIG_LF_SUB_PU
                                              ,
                                              const int count
#endif  // CONFIG_LF_SUB_PU
) {
#if !CONFIG_LF_SUB_PU
  int count = 4;
#endif  // !CONFIG_LF_SUB_PU

#if EDGE_DECISION
#if CONFIG_ASYM_DF
  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter = filt_choice_highbd(s, pitch, filt_width_neg, filt_width_pos,
                                  *q_thresh, *side_thresh, s + count - 1);
#else
  const int filter0 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s += count - 1;
  const int filter3 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s -= count - 1;

  int filter = AOMMIN(filter0, filter3);
#endif  // CONFIG_ASYM_DF
#endif  // EDGE_DECISION
  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (int i = 0; i < count; i += 4) {
    filt_generic_asym_highbd_hor_4px_sse4_1(*q_thresh, AOMMIN(filter, filt_neg),
                                            filter, s, pitch, bd);
    s += 4;
  }
}

void aom_highbd_lpf_vertical_generic_sse4_1(uint16_t *s, int pitch,
#if CONFIG_ASYM_DF
                                            int filt_width_neg,
                                            int filt_width_pos,
#else
                                            int filt_width,
#endif
                                            const uint16_t *q_thresh,
                                            const uint16_t *side_thresh, int bd
#if CONFIG_LF_SUB_PU
                                            ,
                                            const int count
#endif  // CONFIG_LF_SUB_PU
) {
  int i;
#if !CONFIG_LF_SUB_PU
  int count = 4;
#endif  // CONFIG_LF_SUB_PU

#if EDGE_DECISION
#if CONFIG_ASYM_DF
  int filt_neg = (filt_width_neg >> 1) - 1;

  int filter =
      filt_choice_highbd(s, 1, filt_width_neg, filt_width_pos, *q_thresh,
                         *side_thresh, s + (count - 1) * pitch);
#else
  int filt_neg = (filt_width_neg >> 1) - 1;
  int filt_pos = (filt_width_pos >> 1) - 1;
  const int filter0 = filt_choice_highbd(
      s, 1, AOMMAX(filt_width_pos, filt_width_neg), *q_thresh, *side_thresh);
  const int filter3 = filt_choice_highbd(s + (count - 1) * pitch, 1, filt_width,
                                         *q_thresh, *side_thresh);
  int filter = AOMMIN(filter0, filter3);
#endif  // CONFIG_ASYM_DF
#endif  // EDGE_DECISION

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; i += 4) {
    filt_generic_asym_highbd_ver_4px_sse4_1(*q_thresh, AOMMIN(filter, filt_neg),
                                            filter, s, pitch, bd);
    s += 4 * pitch;
  }
}
