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

#include <emmintrin.h>
#include <smmintrin.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/loopfilter.h"

// An alternative implementation for vertical filtering is available under this
// macro.
#define USE_TRANSPOSE_VERT 0

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

#if USE_TRANSPOSE_VERT
// Helper function: clip_pixel_highbd for SIMD 32-bit unsigned integers (SSE
// version)
static inline __m128i mm_clip_pixel_highbd_epu32_sse(__m128i pixels, int bd) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i max_val = _mm_set1_epi32((1 << bd) - 1);
  __m128i clipped_at_min = _mm_max_epi32(pixels, zero);
  __m128i clipped_at_max = _mm_min_epi32(clipped_at_min, max_val);
  return clipped_at_max;
}
#endif  // USE_TRANSPOSE_VERT

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
  __m128i xmm_src_p0_64 = _mm_loadl_epi64((__m128i const *)(src));
  __m128i xmm_src_m1s_64 = _mm_loadl_epi64((__m128i const *)(src - stride));
  __m128i xmm_src_p1s_64 = _mm_loadl_epi64((__m128i const *)(src + stride));
  __m128i xmm_src_m2s_64 = _mm_loadl_epi64((__m128i const *)(src - 2 * stride));

  // Convert 4 uint16_t values to 4 int32_t values.
  __m128i xmm_s0 = _mm_cvtepu16_epi32(xmm_src_p0_64);
  __m128i xmm_sm1 = _mm_cvtepu16_epi32(xmm_src_m1s_64);
  __m128i xmm_sp1 = _mm_cvtepu16_epi32(xmm_src_p1s_64);
  __m128i xmm_sm2 = _mm_cvtepu16_epi32(xmm_src_m2s_64);

  // delta_m2_tmp =
  //   (3 * (src[0] - src[-stride]) - (src[stride] - src[-2*stride]))
  __m128i xmm_diff1 = _mm_sub_epi32(xmm_s0, xmm_sm1);
  __m128i xmm_diff1_mult4 = _mm_slli_epi32(xmm_diff1, 2);
  __m128i xmm_term1 =
      _mm_sub_epi32(xmm_diff1_mult4, xmm_diff1);  // Multiply by 3
  __m128i xmm_diff2 = _mm_sub_epi32(xmm_sp1, xmm_sm2);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);

  __m128i cmp_with_zero =
      _mm_cmpeq_epi32(xmm_delta_m2_tmp, _mm_setzero_si128());
  int is_delta_m2_zero = _mm_movemask_epi8(cmp_with_zero);
  if (is_delta_m2_zero == 0xFFFF) return;

  __m128i xmm_delta_m2 = _mm_slli_epi32(xmm_delta_m2_tmp, 2);  // mutiply by 4

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);

  // Apply modifications
  __m128i xmm_adj_term_neg = _mm_setzero_si128();

  if (filter_len_neg == filter_len_pos) {
    for (int row = filter_len - 1; row >= 0; row--) {
      uint16_t *ptr_current_row_neg = src + (-row - 1) * stride;
      __m128i xmm_src_pixels_neg_64 =
          _mm_loadl_epi64((__m128i const *)ptr_current_row_neg);
      __m128i xmm_src_neg = _mm_cvtepu16_epi32(xmm_src_pixels_neg_64);

      uint16_t *ptr_current_row_pos = src + row * stride;
      __m128i xmm_src_pixels_pos_64 =
          _mm_loadl_epi64((__m128i const *)ptr_current_row_pos);
      __m128i xmm_src_pos = _mm_cvtepu16_epi32(xmm_src_pixels_pos_64);

      xmm_adj_term_neg = _mm_add_epi32(xmm_adj_term_neg, xmm_delta_m2_neg);
      __m128i xmm_adj_term_rounded =
          mm_round_power_of_two_epi32_sse(xmm_adj_term_neg, 3 + DF_SHIFT);
      __m128i xmm_result_neg = _mm_add_epi32(xmm_src_neg, xmm_adj_term_rounded);

      __m128i xmm_result_pos = _mm_sub_epi32(xmm_src_pos, xmm_adj_term_rounded);

      __m128i xmm_final_result_epi16 =
          _mm_packus_epi32(xmm_result_neg, xmm_result_pos);

      // Clipping
      xmm_final_result_epi16 =
          mm_clip_pixel_highbd_epu16_sse(xmm_final_result_epi16, bd);

      // Store the lower 64 bits (4 uint16_t pixels)
      _mm_storel_epi64((__m128i *)ptr_current_row_neg, xmm_final_result_epi16);

      _mm_storel_epi64((__m128i *)ptr_current_row_pos,
                       _mm_srli_si128(xmm_final_result_epi16, 8));
    }
  } else {
    int scalar_w_mult_pos = w_mult[filter_len_pos - 1];
    __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);
    __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

    // Negative side filtering (pixels above)
    for (int row = filter_len_neg - 1; row >= 0; row--) {
      uint16_t *ptr_current_row_neg = src + (-row - 1) * stride;
      __m128i xmm_src_pixels_neg_64 =
          _mm_loadl_epi64((__m128i const *)ptr_current_row_neg);
      __m128i xmm_src_neg = _mm_cvtepu16_epi32(xmm_src_pixels_neg_64);

      xmm_adj_term_neg = _mm_add_epi32(xmm_adj_term_neg, xmm_delta_m2_neg);
      __m128i xmm_adj_term_neg_rounded =
          mm_round_power_of_two_epi32_sse(xmm_adj_term_neg, 3 + DF_SHIFT);
      __m128i xmm_result_neg =
          _mm_add_epi32(xmm_src_neg, xmm_adj_term_neg_rounded);

      // Pack 4 int32_t results back to 4 uint16_t with saturation.
      // Resulting 4 uint16_t will be in the lower 64 bits of
      // xmm_final_result_neg_epi16.
      __m128i xmm_final_result_neg_epi16 =
          _mm_packus_epi32(xmm_result_neg, _mm_setzero_si128());

      // Clip the 4 uint16_t pixels (lower 64 bits)
      xmm_final_result_neg_epi16 =
          mm_clip_pixel_highbd_epu16_sse(xmm_final_result_neg_epi16, bd);

      // Store the lower 64 bits (4 uint16_t pixels)
      _mm_storel_epi64((__m128i *)ptr_current_row_neg,
                       xmm_final_result_neg_epi16);
    }

    __m128i xmm_adj_term_pos = _mm_setzero_si128();

    // Positive side filtering (pixels at and below)
    for (int row = filter_len_pos - 1; row >= 0; row--) {
      uint16_t *ptr_current_row_pos = src + row * stride;
      __m128i xmm_src_pixels_pos_64 =
          _mm_loadl_epi64((__m128i const *)ptr_current_row_pos);
      __m128i xmm_src_pos = _mm_cvtepu16_epi32(xmm_src_pixels_pos_64);

      xmm_adj_term_pos = _mm_add_epi32(xmm_adj_term_pos, xmm_delta_m2_pos);
      __m128i xmm_adj_term_pos_rounded =
          mm_round_power_of_two_epi32_sse(xmm_adj_term_pos, 3 + DF_SHIFT);
      __m128i xmm_result_pos =
          _mm_sub_epi32(xmm_src_pos, xmm_adj_term_pos_rounded);

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

#if USE_TRANSPOSE_VERT
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
#endif  // USE_TRANSPOSE_VERT

static const uint8_t reverse_mask[16] = { 14, 15, 12, 13, 10, 11, 8, 9,
                                          6,  7,  4,  5,  2,  3,  0, 1 };

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

  // delta_m2_tmp =
  //   (3 * (src[0] - src[-stride]) - (src[stride] - src[-2*stride]))
  __m128i xmm_diff1 = _mm_sub_epi32(xmm_s0, xmm_sm1);
  __m128i xmm_diff1_mult4 = _mm_slli_epi32(xmm_diff1, 2);
  __m128i xmm_term1 =
      _mm_sub_epi32(xmm_diff1_mult4, xmm_diff1);  // Multiply by 3
  __m128i xmm_diff2 = _mm_sub_epi32(xmm_sp1, xmm_sm2);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);

  __m128i cmp_with_zero =
      _mm_cmpeq_epi32(xmm_delta_m2_tmp, _mm_setzero_si128());
  int is_delta_m2_zero = _mm_movemask_epi8(cmp_with_zero);
  if (is_delta_m2_zero == 0xFFFF) return;

  __m128i xmm_delta_m2 = _mm_slli_epi32(xmm_delta_m2_tmp, 2);  // mutiply by 4

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);

  DECLARE_ALIGNED(16, int32_t, delta_m2_neg[4]);

  _mm_store_si128((__m128i *)delta_m2_neg, xmm_delta_m2_neg);

  // Find the adjustment values
  __m128i xmm_coeff_neg_lo, xmm_coeff_neg_hi;

  __m128i xmm_filt_len_neg = _mm_set1_epi16(filter_len_neg);
  __m128i xmm_coeff_neg =
      _mm_subs_epu16(xmm_filt_len_neg, _mm_set_epi16(0, 1, 2, 3, 4, 5, 6, 7));

  xmm_coeff_neg_lo = _mm_cvtepu16_epi32(xmm_coeff_neg);
  xmm_coeff_neg_hi = _mm_cvtepu16_epi32(_mm_srli_si128(xmm_coeff_neg, 8));

  // Apply modifications
  uint16_t *s = src;
  if (filter_len_neg == filter_len_pos) {
    __m128i xmm_reverse_mask = _mm_loadu_si128((const __m128i *)reverse_mask);
    for (int row = 0; row < 4; ++row) {
      int delta_m2_val = delta_m2_neg[row];

      // negative
      __m128i xmm_delta_m2_row = _mm_set1_epi32(delta_m2_val);
      __m128i xmm_adj_neg_hi =
          _mm_mullo_epi32(xmm_delta_m2_row, xmm_coeff_neg_hi);
      xmm_adj_neg_hi =
          mm_round_power_of_two_epi32_sse(xmm_adj_neg_hi, 3 + DF_SHIFT);

      if (filter_len > 4) {
        __m128i xmm_adj_neg_lo =
            _mm_mullo_epi32(xmm_delta_m2_row, xmm_coeff_neg_lo);
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

        // Load 8 pixels
        __m128i xmm_src_pos8 = _mm_loadu_si128((__m128i *)s);

        // positive
        __m128i xmm_adj_pos = _mm_shuffle_epi8(xmm_adj_neg, xmm_reverse_mask);

        // Add and clip
        xmm_src_pos8 = _mm_sub_epi16(xmm_src_pos8, xmm_adj_pos);
        xmm_src_pos8 = mm_clip_pixel_highbd_epu16_sse(xmm_src_pos8, bd);
        // Store
        _mm_storeu_si128((__m128i *)s, xmm_src_pos8);
      } else {
        __m128i xmm_adj_pos_lo =
            _mm_shuffle_epi32(xmm_adj_neg_hi, _MM_SHUFFLE(0, 1, 2, 3));

        xmm_adj_pos_lo = _mm_sub_epi32(_mm_setzero_si128(), xmm_adj_pos_lo);

        __m128i xmm_adj = _mm_packs_epi32(xmm_adj_neg_hi, xmm_adj_pos_lo);

        // Load 8 pixels
        __m128i xmm_src = _mm_loadu_si128((__m128i *)(s - 4));

        // Add and clip
        xmm_src = _mm_add_epi16(xmm_src, xmm_adj);
        xmm_src = mm_clip_pixel_highbd_epu16_sse(xmm_src, bd);
        // Store
        _mm_storeu_si128((__m128i *)(s - 4), xmm_src);
      }
      s += stride;
    }
  } else {
    int scalar_w_mult_pos = w_mult[filter_len_pos - 1];
    __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);
    __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

    DECLARE_ALIGNED(16, int32_t, delta_m2_pos[4]);

    _mm_store_si128((__m128i *)delta_m2_pos, xmm_delta_m2_pos);

    __m128i xmm_coeff_pos_lo, xmm_coeff_pos_hi;

    __m128i xmm_filt_len_pos = _mm_set1_epi16(filter_len_pos);
    __m128i xmm_coeff_pos =
        _mm_subs_epu16(xmm_filt_len_pos, _mm_set_epi16(7, 6, 5, 4, 3, 2, 1, 0));

    xmm_coeff_pos_lo = _mm_cvtepu16_epi32(xmm_coeff_pos);
    xmm_coeff_pos_hi = _mm_cvtepu16_epi32(_mm_srli_si128(xmm_coeff_pos, 8));

    for (int row = 0; row < 4; ++row) {
      int delta_m2_neg_val = delta_m2_neg[row];
      int delta_m2_pos_val = delta_m2_pos[row];

      // negative
      __m128i xmm_delta_m2_neg_row = _mm_set1_epi32(delta_m2_neg_val);
      __m128i xmm_adj_neg_hi =
          _mm_mullo_epi32(xmm_delta_m2_neg_row, xmm_coeff_neg_hi);
      xmm_adj_neg_hi =
          mm_round_power_of_two_epi32_sse(xmm_adj_neg_hi, 3 + DF_SHIFT);

      // positive
      __m128i xmm_delta_m2_pos_row = _mm_set1_epi32(delta_m2_pos_val);
      __m128i xmm_adj_pos_lo =
          _mm_mullo_epi32(xmm_delta_m2_pos_row, xmm_coeff_pos_lo);
      xmm_adj_pos_lo =
          mm_round_power_of_two_epi32_sse(xmm_adj_pos_lo, 3 + DF_SHIFT);

      if (filter_len > 4) {
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
      } else {
        xmm_adj_pos_lo = _mm_sub_epi32(_mm_setzero_si128(), xmm_adj_pos_lo);

        __m128i xmm_adj = _mm_packs_epi32(xmm_adj_neg_hi, xmm_adj_pos_lo);

        // Load 8 pixels
        __m128i xmm_src = _mm_loadu_si128((__m128i *)(s - 4));

        // Add and clip
        xmm_src = _mm_add_epi16(xmm_src, xmm_adj);
        xmm_src = mm_clip_pixel_highbd_epu16_sse(xmm_src, bd);
        // Store
        _mm_storeu_si128((__m128i *)(s - 4), xmm_src);
      }

      s += stride;
    }
  }
}

#if USE_TRANSPOSE_VERT
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

  // delta_m2_tmp =
  //   (3 * (src[0] - src[-stride]) - (src[stride] - src[-2*stride]))
  __m128i xmm_diff1 =
      _mm_sub_epi32(xmm_transposed_cols_32[8], xmm_transposed_cols_32[7]);
  __m128i xmm_term1 = _mm_mullo_epi32(xmm_three, xmm_diff1);
  __m128i xmm_diff2 =
      _mm_sub_epi32(xmm_transposed_cols_32[9], xmm_transposed_cols_32[6]);
  __m128i xmm_delta_m2_tmp = _mm_sub_epi32(xmm_term1, xmm_diff2);
  __m128i xmm_delta_m2 = _mm_mullo_epi32(xmm_delta_m2_tmp, xmm_four);

  // Clamp delta_m2
  int scalar_q_thresh_val = q_threshold * q_thresh_mults[filter_len - 1];

  __m128i xmm_q_clamp_val = _mm_set1_epi32(scalar_q_thresh_val);
  __m128i xmm_neg_q_clamp_val = _mm_set1_epi32(-scalar_q_thresh_val);

  xmm_delta_m2 = _mm_max_epi32(xmm_delta_m2, xmm_neg_q_clamp_val);
  xmm_delta_m2 = _mm_min_epi32(xmm_delta_m2, xmm_q_clamp_val);

  // Scaled deltas: delta_m2_neg and delta_m2_pos
  int scalar_w_mult_neg = w_mult[filter_len_neg - 1];
  int scalar_w_mult_pos = w_mult[filter_len_pos - 1];

  __m128i xmm_w_mult_neg = _mm_set1_epi32(scalar_w_mult_neg);
  __m128i xmm_w_mult_pos = _mm_set1_epi32(scalar_w_mult_pos);

  __m128i xmm_delta_m2_neg = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_neg);
  __m128i xmm_delta_m2_pos = _mm_mullo_epi32(xmm_delta_m2, xmm_w_mult_pos);

  // Apply modifications

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
#endif  // USE_TRANSPOSE_VERT

#define CALC_FILT_CHOICE_MASK_HORZ(xmm_neg, xmm_pos,                    \
                                   xmm_row_m1p0_sub_m2p1_mult, xmm_out) \
  {                                                                     \
    __m128i xmm_row_mp = _mm_unpacklo_epi64(xmm_neg, xmm_pos);          \
    xmm_out = _mm_sub_epi16(xmm_row_m1p0, xmm_row_mp);                  \
    xmm_out = _mm_sub_epi16(xmm_out, xmm_row_m1p0_sub_m2p1_mult);       \
    xmm_out = _mm_abs_epi16(xmm_out);                                   \
    xmm_out = _mm_and_si128(xmm_out, xmm_sample_mask);                  \
  }

static INLINE int filt_choice_highbd_horizontal_px4_sse4_1(
    uint16_t *s, int pitch, int max_filt_neg, int max_filt_pos,
    uint16_t q_thresh, uint16_t side_thresh) {
  const int stride = pitch;
  if (!q_thresh || !side_thresh) return 0;

  int max_samples_neg = max_filt_neg / 2 - 1;
  int max_samples_pos = max_filt_pos / 2 - 1;

  if (max_samples_pos < 1 || max_samples_pos < max_samples_neg) return 0;

  int8_t mask = 0;

  __m128i xmm_sample_mask = _mm_set_epi16(-1, 0, 0, -1, -1, 0, 0, -1);

  __m128i xmm_row_m3 = _mm_loadl_epi64((__m128i *)&s[-3 * stride]);
  __m128i xmm_row_m2 = _mm_loadl_epi64((__m128i *)&s[-2 * stride]);
  __m128i xmm_row_m1 = _mm_loadl_epi64((__m128i *)&s[-1 * stride]);
  __m128i xmm_row_p0 = _mm_loadl_epi64((__m128i *)&s[0]);
  __m128i xmm_row_p1 = _mm_loadl_epi64((__m128i *)&s[1 * stride]);
  __m128i xmm_row_p2 = _mm_loadl_epi64((__m128i *)&s[2 * stride]);

  // Put two 4x16 bits together, if we are doing the same operation

  // Calculate second derivitive at -2, -1, 0, 1:

  // For 2nd deriv at -2 and -1:
  __m128i xmm_row_m3m2 = _mm_unpacklo_epi64(xmm_row_m3, xmm_row_m2);
  __m128i xmm_row_m2m1 = _mm_unpacklo_epi64(xmm_row_m2, xmm_row_m1);
  __m128i xmm_row_m1p0 = _mm_unpacklo_epi64(xmm_row_m1, xmm_row_p0);
  __m128i xmm_sd_m2m1 =
      _mm_sub_epi16(xmm_row_m3m2, _mm_slli_epi16(xmm_row_m2m1, 1));
  xmm_sd_m2m1 = _mm_add_epi16(xmm_sd_m2m1, xmm_row_m1p0);
  xmm_sd_m2m1 = _mm_abs_epi16(xmm_sd_m2m1);
  xmm_sd_m2m1 = _mm_and_si128(xmm_sd_m2m1, xmm_sample_mask);

  // For 2nd deriv at 0 and 1:
  __m128i xmm_row_p0p1 = _mm_unpacklo_epi64(xmm_row_p0, xmm_row_p1);
  __m128i xmm_row_p1p2 = _mm_unpacklo_epi64(xmm_row_p1, xmm_row_p2);
  __m128i xmm_sd_p0p1 =
      _mm_sub_epi16(xmm_row_m1p0, _mm_slli_epi16(xmm_row_p0p1, 1));
  xmm_sd_p0p1 = _mm_add_epi16(xmm_sd_p0p1, xmm_row_p1p2);
  xmm_sd_p0p1 = _mm_abs_epi16(xmm_sd_p0p1);
  xmm_sd_p0p1 = _mm_and_si128(xmm_sd_p0p1, xmm_sample_mask);

  // Calculate diffs at 3 and 4, where
  // diff_neg(x) = s[-1] - s[-1-x] - x * (s[-1] - s[-2])
  // diff_pos(x) = s[0] - s[x] - x * (s[0] - s[1])
  __m128i xmm_row_m2p1 = _mm_unpacklo_epi64(xmm_row_m2, xmm_row_p1);
  __m128i xmm_row_m1p0_sub_m2p1 = _mm_sub_epi16(xmm_row_m1p0, xmm_row_m2p1);

  // multiply xmm_row_m1p0_sub_m2p1 by 4
  __m128i xmm_row_m1p0_sub_m2p1_mult_4 =
      _mm_slli_epi16(xmm_row_m1p0_sub_m2p1, 2);

  // multiply xmm_row_m1p0_sub_m2p1 by 3
  __m128i xmm_row_m1p0_sub_m2p1_mult_3 =
      _mm_sub_epi16(xmm_row_m1p0_sub_m2p1_mult_4, xmm_row_m1p0_sub_m2p1);

  __m128i xmm_diffs_3;
  if (max_samples_pos >= 3) {
    // For diff_neg(3) and diff_pos(3)
    __m128i xmm_row_m4 = _mm_loadl_epi64((__m128i *)&s[-4 * stride]);
    __m128i xmm_row_p3 = _mm_loadl_epi64((__m128i *)&s[3 * stride]);

    CALC_FILT_CHOICE_MASK_HORZ(xmm_row_m4, xmm_row_p3,
                               xmm_row_m1p0_sub_m2p1_mult_3, xmm_diffs_3)
  } else {
    xmm_diffs_3 = _mm_setzero_si128();
  }

  __m128i xmm_diffs_4;
  if (max_samples_pos >= 4) {
    // For diff_neg(4) and diff_pos(4)
    __m128i xmm_row_m5 = _mm_loadl_epi64((__m128i *)&s[-5 * stride]);
    __m128i xmm_row_p4 = _mm_loadl_epi64((__m128i *)&s[4 * stride]);

    CALC_FILT_CHOICE_MASK_HORZ(xmm_row_m5, xmm_row_p4,
                               xmm_row_m1p0_sub_m2p1_mult_4, xmm_diffs_4)
  } else {
    xmm_diffs_4 = _mm_setzero_si128();
  }

  // Do horizontal add to get these numbers
  __m128i xmm_sds = _mm_hadd_epi16(xmm_sd_m2m1, xmm_sd_p0p1);
  __m128i xmm_diffs = _mm_hadd_epi16(xmm_diffs_3, xmm_diffs_4);
  __m128i xmm_results = _mm_hadd_epi16(xmm_sds, xmm_diffs);

  // Do averaging
  xmm_results =
      _mm_srli_epi16(_mm_add_epi16(xmm_results, _mm_set1_epi16(1)), 1);

  // Store into memory. This array consists of:
  // { sd[-2], sd[-1], sd[0], sd[1],
  //   diff_neg[3], diff_pos[3], diff_neg[4], diff_pos[4]}
  DECLARE_ALIGNED(16, uint16_t, res_arr[8]);
  _mm_store_si128((__m128i *)res_arr, xmm_results);

  // Testing for 1 sample modification
  //-----------------------------------------------
  mask |= (res_arr[0] > side_thresh) * -1;
  mask |= (res_arr[3] > side_thresh) * -1;

  if (mask) return 0;

  if (max_samples_pos == 1) return 1;

  // Testing for 2 sample modification
  //-----------------------------------------------
  const int side_thresh2 = side_thresh >> 2;

  mask |= (res_arr[0] > side_thresh2) * -1;
  mask |= (res_arr[3] > side_thresh2) * -1;

  mask |= ((res_arr[1] + res_arr[2]) > q_thresh * DF_6_THRESH) * -1;

  if (mask) return 1;

  if (max_samples_pos == 2) return 2;

  // Testing 3 sample modification
  //-----------------------------------------------
  const int side_thresh3 = side_thresh >> FILT_8_THRESH_SHIFT;

  mask |= (res_arr[0] > side_thresh3) * -1;
  mask |= (res_arr[3] > side_thresh3) * -1;

  mask |= ((res_arr[1] + res_arr[2]) > q_thresh * DF_8_THRESH) * -1;

  int end_dir_thresh = (side_thresh * 3) >> 4;

  if (max_samples_neg > 2) mask |= (res_arr[4] > end_dir_thresh) * -1;
  mask |= (res_arr[5] > end_dir_thresh) * -1;

  if (mask) return 2;

  if (max_samples_pos == 3) return 3;

  // Testing 4 sample modification and above
  //-----------------------------------------------
  int transition = (res_arr[1] + res_arr[2]) << DF_Q_THRESH_SHIFT;

  // 4 sample modification
  {
    const int q_thresh4 = q_thresh * q_first[0];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 4) >> 4;

    if (max_samples_neg >= 4) mask |= (res_arr[6] > end_dir_thresh) * -1;
    mask |= (res_arr[7] > end_dir_thresh) * -1;

    if (mask) return 3;
    if (max_samples_pos <= 4) return 4;
  }

  // Calculate diffs at 6 and 7
  __m128i xmm_row_m7 = _mm_loadl_epi64((__m128i *)&s[-7 * stride]);
  __m128i xmm_row_p6 = _mm_loadl_epi64((__m128i *)&s[6 * stride]);

  __m128i xmm_mult_8 =
      _mm_add_epi16(xmm_row_m1p0_sub_m2p1_mult_4, xmm_row_m1p0_sub_m2p1_mult_4);
  __m128i xmm_row_m1p0_sub_m2p1_mult_7 =
      _mm_sub_epi16(xmm_mult_8, xmm_row_m1p0_sub_m2p1);
  __m128i xmm_row_m1p0_sub_m2p1_mult_6 =
      _mm_sub_epi16(xmm_row_m1p0_sub_m2p1_mult_7, xmm_row_m1p0_sub_m2p1);

  // For diff_neg(6) and diff_pos(6)
  __m128i xmm_diffs_6;
  CALC_FILT_CHOICE_MASK_HORZ(xmm_row_m7, xmm_row_p6,
                             xmm_row_m1p0_sub_m2p1_mult_6, xmm_diffs_6)

  __m128i xmm_diffs_7;
  if (max_samples_pos > 6) {
    // For diff_neg(7) and diff_pos(7)
    __m128i xmm_row_m8 = _mm_loadl_epi64((__m128i *)&s[-8 * stride]);
    __m128i xmm_row_p7 = _mm_loadl_epi64((__m128i *)&s[7 * stride]);

    CALC_FILT_CHOICE_MASK_HORZ(xmm_row_m8, xmm_row_p7,
                               xmm_row_m1p0_sub_m2p1_mult_7, xmm_diffs_7)
  } else {
    xmm_diffs_7 = _mm_setzero_si128();
  }

  // Do horizontal add to get these numbers
  __m128i xmm_diffs_67 = _mm_hadd_epi16(xmm_diffs_6, xmm_diffs_7);
  xmm_diffs_67 = _mm_hadd_epi16(xmm_diffs_67, _mm_setzero_si128());

  // Do averaging
  xmm_diffs_67 =
      _mm_srli_epi16(_mm_add_epi16(xmm_diffs_67, _mm_set1_epi16(1)), 1);

  // Store into memory. This array consists of:
  // { diff_neg[6], diff_pos[6], diff_neg[7], diff_pos[7]}
  _mm_storel_epi64((__m128i *)res_arr, xmm_diffs_67);

  // 6 sample modification
  {
    const int q_thresh4 = q_thresh * q_first[2];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 6) >> 4;

    if (max_samples_neg >= 6) mask |= (res_arr[0] > end_dir_thresh) * -1;
    mask |= (res_arr[1] > end_dir_thresh) * -1;

    if (mask) return 4;
    if (max_samples_pos <= 6) return 6;
  }

  // 8 sample modification (using diff_7 so it doesn't go out of 8 pixels)
  {
    const int q_thresh4 = q_thresh * q_first[4];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 8) >> 4;

    if (max_samples_neg >= 7) mask |= (res_arr[2] > end_dir_thresh) * -1;
    mask |= (res_arr[3] > end_dir_thresh) * -1;

    if (mask) return 6;
    if (max_samples_pos <= 8) return 8;
  }

  return MAX_DBL_FLT_LEN;
}

static const uint8_t shuffle_mask1[16] = { 0, 1, 6, 7, 12, 13, 14, 15,
                                           0, 1, 2, 3, 12, 13, 14, 15 };

static const uint8_t shuffle_mask2[16] = { 0, 1, 2, 3, 8,  9,  14, 15,
                                           0, 1, 2, 3, 12, 13, 14, 15 };

#define CALC_FILT_CHOICE_VERT(xmm_neg_mask, xmm_pos_mask, xmm_out) \
  {                                                                \
    __m128i xmm_neg = _mm_madd_epi16(xmm_neg_r0r3, xmm_neg_mask);  \
    __m128i xmm_pos = _mm_madd_epi16(xmm_pos_r0r3, xmm_pos_mask);  \
    xmm_out = _mm_packs_epi32(xmm_neg, xmm_pos);                   \
  }

// TODO: there is a chance of overflow for 12-bit pixels. Only use this for bit
// depth < 12.
static INLINE int filt_choice_highbd_vertical_px4_sse4_1(uint16_t *s, int pitch,
                                                         int max_filt_neg,
                                                         int max_filt_pos,
                                                         uint16_t q_thresh,
                                                         uint16_t side_thresh) {
  const int stride = pitch;
  if (!q_thresh || !side_thresh) return 0;

  int max_samples_neg = max_filt_neg / 2 - 1;
  int max_samples_pos = max_filt_pos / 2 - 1;

  if (max_samples_pos < 1 || max_samples_pos < max_samples_neg) return 0;

  int8_t mask = 0;

  __m128i xmm_row0_m4_to_p3 = _mm_loadu_si128((__m128i *)(s - 4));
  __m128i xmm_row3_m4_to_p3 = _mm_loadu_si128((__m128i *)(s + 3 * stride - 4));

  __m128i xmm_row03_m3_to_p0 =
      _mm_unpacklo_epi16(_mm_srli_si128(xmm_row0_m4_to_p3, 2),
                         _mm_srli_si128(xmm_row3_m4_to_p3, 2));

  __m128i xmm_row03_m2_to_p1 =
      _mm_unpacklo_epi16(_mm_srli_si128(xmm_row0_m4_to_p3, 4),
                         _mm_srli_si128(xmm_row3_m4_to_p3, 4));
  __m128i xmm_row03_m1_to_p2 =
      _mm_unpacklo_epi16(_mm_srli_si128(xmm_row0_m4_to_p3, 6),
                         _mm_srli_si128(xmm_row3_m4_to_p3, 6));
  __m128i xmm_m3p0_add_m1p2 =
      _mm_add_epi16(xmm_row03_m3_to_p0, xmm_row03_m1_to_p2);

  __m128i xmm_m3p0_add_m1p2_sub_m2p1 =
      _mm_sub_epi16(xmm_m3p0_add_m1p2, xmm_row03_m2_to_p1);

  __m128i xmm_row03_sds =
      _mm_sub_epi16(xmm_m3p0_add_m1p2_sub_m2p1, xmm_row03_m2_to_p1);
  xmm_row03_sds = _mm_abs_epi16(xmm_row03_sds);

  __m128i xmm_row03_diff3;
  if (max_samples_pos >= 3) {
    __m128i xmm_diff3_pos_mask = _mm_set_epi16(-1, 0, 3, -2, -1, 0, 3, -2);
    __m128i xmm_diff3_neg_mask = _mm_set_epi16(-2, 3, 0, -1, -2, 3, 0, -1);

    __m128i xmm_row03_m4_to_m1 =
        _mm_unpacklo_epi64(xmm_row0_m4_to_p3, xmm_row3_m4_to_p3);
    __m128i xmm_row03_p0_to_p3 =
        _mm_unpackhi_epi64(xmm_row0_m4_to_p3, xmm_row3_m4_to_p3);

    __m128i xmm_row03_diff3_pos =
        _mm_mullo_epi16(xmm_row03_p0_to_p3, xmm_diff3_pos_mask);
    __m128i xmm_row03_diff3_neg =
        _mm_mullo_epi16(xmm_row03_m4_to_m1, xmm_diff3_neg_mask);

    xmm_row03_diff3 = _mm_hadd_epi16(xmm_row03_diff3_neg, xmm_row03_diff3_pos);
    xmm_row03_diff3 = _mm_hadd_epi16(xmm_row03_diff3, _mm_setzero_si128());
    xmm_row03_diff3 = _mm_abs_epi16(xmm_row03_diff3);
  } else {
    xmm_row03_diff3 = _mm_setzero_si128();
  }

  __m128i xmm_row03_res = _mm_hadd_epi16(xmm_row03_sds, xmm_row03_diff3);
  xmm_row03_res =
      _mm_srli_epi16(_mm_add_epi16(xmm_row03_res, _mm_set1_epi16(1)), 1);

  // Store into memory. This array consists of:
  // { sd[-2], sd[-1], sd[0], sd[1], diff_neg[3], diff_pos[3], 0, 0}
  DECLARE_ALIGNED(16, uint16_t, res_arr[8]);
  _mm_store_si128((__m128i *)res_arr, xmm_row03_res);

  // Testing for 1 sample modification
  //-----------------------------------------------
  mask |= (res_arr[0] > side_thresh) * -1;
  mask |= (res_arr[3] > side_thresh) * -1;

  if (mask) return 0;

  if (max_samples_pos == 1) return 1;

  // Testing for 2 sample modification
  //-----------------------------------------------
  const int side_thresh2 = side_thresh >> 2;

  mask |= (res_arr[0] > side_thresh2) * -1;
  mask |= (res_arr[3] > side_thresh2) * -1;

  mask |= ((res_arr[1] + res_arr[2]) > q_thresh * DF_6_THRESH) * -1;

  if (mask) return 1;

  if (max_samples_pos == 2) return 2;

  // Testing 3 sample modification
  //-----------------------------------------------
  const int side_thresh3 = side_thresh >> FILT_8_THRESH_SHIFT;

  mask |= (res_arr[0] > side_thresh3) * -1;
  mask |= (res_arr[3] > side_thresh3) * -1;

  mask |= ((res_arr[1] + res_arr[2]) > q_thresh * DF_8_THRESH) * -1;

  int end_dir_thresh = (side_thresh * 3) >> 4;

  if (max_samples_neg > 2) mask |= (res_arr[4] > end_dir_thresh) * -1;
  mask |= (res_arr[5] > end_dir_thresh) * -1;

  if (mask) return 2;

  if (max_samples_pos == 3) return 3;

  // Testing 4 sample modification and above
  //-----------------------------------------------
  int transition = (res_arr[1] + res_arr[2]) << DF_Q_THRESH_SHIFT;

  __m128i xmm_row0_p0_to_p7 = _mm_loadu_si128((__m128i *)(s));
  __m128i xmm_row3_p0_to_p7 = _mm_loadu_si128((__m128i *)(s + 3 * stride));

  __m128i xmm_row0_m8_to_m1 = _mm_loadu_si128((__m128i *)(s - 8));
  __m128i xmm_row3_m8_to_m1 = _mm_loadu_si128((__m128i *)(s + 3 * stride - 8));

  __m128i xmm_diff4_neg_mask = _mm_set_epi16(-3, 4, -1, 0, -3, 4, -1, 0);
  __m128i xmm_diff4_pos_mask = _mm_set_epi16(0, -1, 4, -3, 0, -1, 4, -3);

  __m128i xmm_shuffle_mask1 = _mm_loadu_si128((const __m128i *)shuffle_mask1);
  __m128i xmm_shuffle_mask2 = _mm_loadu_si128((const __m128i *)shuffle_mask2);

  __m128i xmm_m8m5m2m1_m8m7m2m1_r0 =
      _mm_shuffle_epi8(xmm_row0_m8_to_m1, xmm_shuffle_mask1);
  __m128i xmm_m8m5m2m1_m8m7m2m1_r3 =
      _mm_shuffle_epi8(xmm_row3_m8_to_m1, xmm_shuffle_mask1);
  __m128i xmm_p0p1p4p7_p0p1p6p7_r0 =
      _mm_shuffle_epi8(xmm_row0_p0_to_p7, xmm_shuffle_mask2);
  __m128i xmm_p0p1p4p7_p0p1p6p7_r3 =
      _mm_shuffle_epi8(xmm_row3_p0_to_p7, xmm_shuffle_mask2);

  __m128i xmm_neg_r0r3 =
      _mm_unpacklo_epi64(xmm_m8m5m2m1_m8m7m2m1_r0, xmm_m8m5m2m1_m8m7m2m1_r3);
  __m128i xmm_pos_r0r3 =
      _mm_unpacklo_epi64(xmm_p0p1p4p7_p0p1p6p7_r0, xmm_p0p1p4p7_p0p1p6p7_r3);

  __m128i xmm_row03_diff4;

  CALC_FILT_CHOICE_VERT(xmm_diff4_neg_mask, xmm_diff4_pos_mask, xmm_row03_diff4)

  __m128i xmm_row03_diff6;
  __m128i xmm_row03_diff7 = _mm_setzero_si128();

  if (max_samples_pos > 4) {
    __m128i xmm_diff6_neg_mask = _mm_set_epi16(-5, 6, -1, 0, -5, 6, -1, 0);
    __m128i xmm_diff6_pos_mask = _mm_set_epi16(0, -1, 6, -5, 0, -1, 6, -5);

    xmm_neg_r0r3 =
        _mm_unpackhi_epi64(xmm_m8m5m2m1_m8m7m2m1_r0, xmm_m8m5m2m1_m8m7m2m1_r3);
    xmm_pos_r0r3 =
        _mm_unpackhi_epi64(xmm_p0p1p4p7_p0p1p6p7_r0, xmm_p0p1p4p7_p0p1p6p7_r3);

    CALC_FILT_CHOICE_VERT(xmm_diff6_neg_mask, xmm_diff6_pos_mask,
                          xmm_row03_diff6)
    if (max_samples_pos > 6) {
      __m128i xmm_diff7_neg_mask = _mm_set_epi16(-6, 7, 0, -1, -6, 7, 0, -1);
      __m128i xmm_diff7_pos_mask = _mm_set_epi16(-1, 0, 7, -6, -1, 0, 7, -6);

      CALC_FILT_CHOICE_VERT(xmm_diff7_neg_mask, xmm_diff7_pos_mask,
                            xmm_row03_diff7)
      xmm_row03_diff7 = _mm_hadd_epi16(xmm_row03_diff7, _mm_setzero_si128());
      xmm_row03_diff7 = _mm_abs_epi16(xmm_row03_diff7);
    }
  } else {
    xmm_row03_diff6 = _mm_setzero_si128();
  }

  __m128i xmm_row03_diff46 = _mm_hadd_epi16(xmm_row03_diff4, xmm_row03_diff6);
  xmm_row03_diff46 = _mm_abs_epi16(xmm_row03_diff46);

  __m128i xmm_diff467 = _mm_hadd_epi16(xmm_row03_diff46, xmm_row03_diff7);
  xmm_diff467 =
      _mm_srli_epi16(_mm_add_epi16(xmm_diff467, _mm_set1_epi16(1)), 1);

  // Store into memory. This array consists of:
  // { diff4_neg, diff4_pos, diff6_neg, diff6_pos, diff7_neg, diff7_pos, 0, 0}
  _mm_store_si128((__m128i *)res_arr, xmm_diff467);

  // 4 sample modification
  {
    const int q_thresh4 = q_thresh * q_first[0];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 4) >> 4;

    if (max_samples_neg >= 4) mask |= (res_arr[0] > end_dir_thresh) * -1;
    mask |= (res_arr[1] > end_dir_thresh) * -1;

    if (mask) return 3;
    if (max_samples_pos <= 4) return 4;
  }

  // 6 sample modification
  {
    const int q_thresh4 = q_thresh * q_first[2];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 6) >> 4;

    if (max_samples_neg >= 6) mask |= (res_arr[2] > end_dir_thresh) * -1;
    mask |= (res_arr[3] > end_dir_thresh) * -1;

    if (mask) return 4;
    if (max_samples_pos <= 6) return 6;
  }

  // 8 sample modification (using diff_7 so it doesn't go out of 8 pixels)
  {
    const int q_thresh4 = q_thresh * q_first[4];
    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * 8) >> 4;

    if (max_samples_neg >= 7) mask |= (res_arr[4] > end_dir_thresh) * -1;
    mask |= (res_arr[5] > end_dir_thresh) * -1;

    if (mask) return 6;
    if (max_samples_pos <= 8) return 8;
  }

  return MAX_DBL_FLT_LEN;
}

void aom_highbd_lpf_horizontal_generic_sse4_1(
    uint16_t *s, int pitch, int filt_width_neg, int filt_width_pos,
    const uint16_t *q_thresh, const uint16_t *side_thresh, int bd,
    int is_lossless_neg, int is_lossless_pos) {
  (void)is_lossless_neg;
  (void)is_lossless_pos;

  int count = 4;

  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter;
  if (count == 4) {
    filter = filt_choice_highbd_horizontal_px4_sse4_1(
        s, pitch, filt_width_neg, filt_width_pos, *q_thresh, *side_thresh);
  } else {
    filter = filt_choice_highbd(s, pitch, filt_width_neg, filt_width_pos,
                                *q_thresh, *side_thresh, s + count - 1);
  }

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (int i = 0; i < count; i += 4) {
    filt_generic_asym_highbd_hor_4px_sse4_1(*q_thresh, AOMMIN(filter, filt_neg),
                                            filter, s, pitch, bd);

    s += 4;
  }
}

void aom_highbd_lpf_vertical_generic_sse4_1(
    uint16_t *s, int pitch, int filt_width_neg, int filt_width_pos,
    const uint16_t *q_thresh, const uint16_t *side_thresh, int bd,
    int is_lossless_neg, int is_lossless_pos) {
  int i;

  (void)is_lossless_neg;
  (void)is_lossless_pos;

  int count = 4;

  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter;
  if (count == 4) {
    filter = filt_choice_highbd_vertical_px4_sse4_1(
        s, pitch, filt_width_neg, filt_width_pos, *q_thresh, *side_thresh);
  } else {
    filter = filt_choice_highbd(s, 1, filt_width_neg, filt_width_pos, *q_thresh,
                                *side_thresh, s + (count - 1) * pitch);
  }

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; i += 4) {
    filt_generic_asym_highbd_ver_4px_sse4_1(*q_thresh, AOMMIN(filter, filt_neg),
                                            filter, s, pitch, bd);
    s += 4 * pitch;
  }
}
