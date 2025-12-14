/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h> /* AVX2 */
#include <tmmintrin.h> /* SSSE3 */

#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/intra_matrix.h"

// Multiply 11 element feature vector with matrix to generate 8x8 prediction.
// A - pointer to matrix
// B - pointer to feature vector
// C - 8x8 output prediction
// bd - bit depth
void av2_dip_matrix_multiplication_avx2(const uint16_t *A, const uint16_t *B,
                                        uint16_t *C, int bd) {
  static const uint16_t mask[16] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, 0,  0,  0,  0,  0 };

  __m256i in0 = _mm256_lddqu_si256((__m256i *)B);
  __m256i in_mask = _mm256_lddqu_si256((__m256i *)mask);
  in0 = _mm256_and_si256(in0, in_mask);
  // in0 = { B0, B1, B2, B3, B4, B5, B6, B7 | B8, B9, B10, 0, 0, 0, 0, 0 }
  __m256i negsum = _mm256_madd_epi16(in0, in_mask);
  negsum = _mm256_hadd_epi32(negsum, negsum);
  negsum = _mm256_hadd_epi32(negsum, negsum);
  negsum = _mm256_slli_epi32(negsum, DIP_BITS - 2);
  __m128i offset = _mm_set1_epi32(DIP_OFFSET >> 2);
  __m128i maxval = _mm_set1_epi32((1 << bd) - 1);
  __m128i zero = _mm_setzero_si128();

  for (int i = 0; i < DIP_ROWS; i += 4) {
    __m256i row0 = _mm256_lddqu_si256((__m256i *)&A[i * DIP_COLS]);
    __m256i row1 = _mm256_lddqu_si256((__m256i *)&A[(i + 1) * DIP_COLS]);
    __m256i row2 = _mm256_lddqu_si256((__m256i *)&A[(i + 2) * DIP_COLS]);
    __m256i row3 = _mm256_lddqu_si256((__m256i *)&A[(i + 3) * DIP_COLS]);
    __m256i m0 = _mm256_madd_epi16(row0, in0);
    __m256i m1 = _mm256_madd_epi16(row1, in0);
    __m256i m2 = _mm256_madd_epi16(row2, in0);
    __m256i m3 = _mm256_madd_epi16(row3, in0);
    __m256i m01 = _mm256_hadd_epi32(m0, m1);
    __m256i m23 = _mm256_hadd_epi32(m2, m3);
    __m256i m0123 = _mm256_hadd_epi32(m01, m23);
    __m256i sum0 = _mm256_add_epi32(m0123, negsum);
    __m128i sum0_lo = _mm256_castsi256_si128(sum0);
    __m128i sum0_hi = _mm256_extracti128_si256(sum0, 1);
    __m128i sum1 = _mm_add_epi32(sum0_lo, sum0_hi);
    sum1 = _mm_add_epi32(sum1, offset);
    sum1 = _mm_srai_epi32(sum1, DIP_BITS - 2);
    sum1 = _mm_min_epi32(sum1, maxval);
    sum1 = _mm_max_epi32(sum1, zero);
    __m128i out0 = _mm_packus_epi32(sum1, sum1);
    _mm_storeu_si64(&C[i], out0);
  }
}

// Processes 8 pixels at a time using SSSE3.
static INLINE void resample_vert_w8_ssse3(uint16_t *dst, const uint16_t *p0_row,
                                          const uint16_t *p1_row, const int w0,
                                          const int w1, const int upy_log2) {
  const __m128i p0 = _mm_loadu_si128((const __m128i *)p0_row);
  const __m128i p1 = _mm_loadu_si128((const __m128i *)p1_row);

  const __m128i p0p1_lo = _mm_unpacklo_epi16(p0, p1);
  const __m128i p0p1_hi = _mm_unpackhi_epi16(p0, p1);

  const __m128i weights = _mm_set_epi16(w1, w0, w1, w0, w1, w0, w1, w0);

  const __m128i res_lo = _mm_madd_epi16(p0p1_lo, weights);
  const __m128i res_hi = _mm_madd_epi16(p0p1_hi, weights);

  const __m128i shift = _mm_cvtsi32_si128(upy_log2);
  const __m128i shifted_lo = _mm_sra_epi32(res_lo, shift);
  const __m128i shifted_hi = _mm_sra_epi32(res_hi, shift);

  const __m128i result = _mm_packus_epi32(shifted_lo, shifted_hi);

  _mm_storeu_si128((__m128i *)dst, result);
}

// Processes 16 pixels at a time using AVX2.
static INLINE void resample_vert_w16_avx2(uint16_t *dst, const uint16_t *p0_row,
                                          const uint16_t *p1_row, const int w0,
                                          const int w1, const int upy_log2) {
  const __m256i p0 = _mm256_loadu_si256((const __m256i *)p0_row);
  const __m256i p1 = _mm256_loadu_si256((const __m256i *)p1_row);

  const __m256i p0p1_lo = _mm256_unpacklo_epi16(p0, p1);
  const __m256i p0p1_hi = _mm256_unpackhi_epi16(p0, p1);

  const __m256i weights = _mm256_set_epi16(w1, w0, w1, w0, w1, w0, w1, w0, w1,
                                           w0, w1, w0, w1, w0, w1, w0);

  const __m256i res_lo = _mm256_madd_epi16(p0p1_lo, weights);
  const __m256i res_hi = _mm256_madd_epi16(p0p1_hi, weights);

  const __m128i shift = _mm_cvtsi32_si128(upy_log2);

  const __m256i shifted_lo = _mm256_sra_epi32(res_lo, shift);
  const __m256i shifted_hi = _mm256_sra_epi32(res_hi, shift);

  const __m256i result = _mm256_packus_epi32(shifted_lo, shifted_hi);

  _mm256_storeu_si256((__m256i *)dst, result);
}

// Processes 32 pixels at a time using AVX2.
static INLINE void resample_vert_w32_avx2(uint16_t *dst, const uint16_t *p0_row,
                                          const uint16_t *p1_row, const int w0,
                                          const int w1, const int upy_log2) {
  resample_vert_w16_avx2(dst, p0_row, p1_row, w0, w1, upy_log2);
  resample_vert_w16_avx2(dst + 16, p0_row + 16, p1_row + 16, w0, w1, upy_log2);
}

// Processes 64 pixels at a time using AVX2.
static INLINE void resample_vert_w64_avx2(uint16_t *dst, const uint16_t *p0_row,
                                          const uint16_t *p1_row, const int w0,
                                          const int w1, const int upy_log2) {
  resample_vert_w32_avx2(dst, p0_row, p1_row, w0, w1, upy_log2);
  resample_vert_w32_avx2(dst + 32, p0_row + 32, p1_row + 32, w0, w1, upy_log2);
}

void resample_output_avx2(uint16_t *dst, int dst_stride,
                          const uint16_t *above_row, const uint16_t *left_col,
                          uint16_t *ml_output, int bw_log2, int bh_log2,
                          int transpose) {
  // AVM_SIMD_CONV_FN_W_FN
  typedef void (*resample_vert_fn)(uint16_t *dst, const uint16_t *p0_row,
                                   const uint16_t *p1_row, const int w0,
                                   const int w1, const int upy_log2);
  // up/down sampling factors
  int pred_x = 8;
  int pred_y = 8;
  int upx_log2 = bw_log2 - 3;
  int upy_log2 = bh_log2 - 3;
  int downx_log2 = 0;
  int downy_log2 = 0;
  if (upx_log2 < 0) {
    downx_log2 = -upx_log2;
    upx_log2 = 0;
  }
  if (upy_log2 < 0) {
    downy_log2 = -upy_log2;
    upy_log2 = 0;
  }
  int mx = 1 << upx_log2;
  int my = 1 << upy_log2;
  int downx = 1 << downx_log2;
  int downy = 1 << downy_log2;
  int bw = 1 << bw_log2;
  // Copy ml_output[] into dst[]
  for (int i = 0; i < pred_y >> downy_log2; i++) {
    for (int j = 0; j < pred_x >> downx_log2; j++) {
      int x = j * mx + (mx - 1);
      int y = i * my + (my - 1);
      int i1 = i * downy;
      int j1 = j * downx;
      int ii = transpose ? j1 : i1;
      int jj = transpose ? i1 : j1;
      dst[y * dst_stride + x] = ml_output[ii * pred_x + jj];
    }
  }
  // Interpolate horizontally.
  if (upx_log2 > 2) {  // For width > 32
    for (int i = 0; i < pred_y >> downy_log2; i++) {
      int y = i * my + (my - 1);
      int p0 = 0;
      int p1 = left_col[y];
      for (int j = 0; j < pred_x >> downx_log2; j++) {
        int x = j * mx;
        p0 = p1;
        p1 = dst[y * dst_stride + x + mx - 1];
        const __m256i p0_32 = _mm256_set1_epi32(p0);
        const __m256i p1_32 = _mm256_set1_epi32(p1);
        const __m256i mx_32 = _mm256_set1_epi32(mx);
        const __m128i shift = _mm_cvtsi32_si128(upx_log2);
        int k = 0;
        for (; k <= mx - 1 - 8; k += 8) {
          const __m256i k1_32 = _mm256_setr_epi32(k + 1, k + 2, k + 3, k + 4,
                                                  k + 5, k + 6, k + 7, k + 8);
          const __m256i w1_32 = k1_32;
          const __m256i w0_32 = _mm256_sub_epi32(mx_32, w1_32);
          const __m256i p0w0 = _mm256_mullo_epi32(p0_32, w0_32);
          const __m256i p1w1 = _mm256_mullo_epi32(p1_32, w1_32);
          const __m256i sum = _mm256_add_epi32(p0w0, p1w1);
          const __m256i shifted = _mm256_sra_epi32(sum, shift);

          const __m128i lo = _mm256_castsi256_si128(shifted);
          const __m128i hi = _mm256_extracti128_si256(shifted, 1);
          const __m128i packed = _mm_packus_epi32(lo, hi);
          _mm_storeu_si128((__m128i *)(dst + y * dst_stride + x + k), packed);
        }
        // Remainder loop
        for (; k < mx - 1; ++k) {
          const int k1 = k + 1;
          dst[y * dst_stride + x + k] = (p0 * (mx - k1) + p1 * k1) >> upx_log2;
        }
      }
    }
  } else {  // For width <= 32
    for (int i = 0; i < pred_y >> downy_log2; i++) {
      int y = i * my + (my - 1);
      int p0 = 0;
      int p1 = left_col[y];
      for (int j = 0; j < pred_x >> downx_log2; j++) {
        int x = j * mx;
        p0 = p1;
        p1 = dst[y * dst_stride + x + mx - 1];
        for (int k = 0; k < mx - 1; k++) {
          int k1 = k + 1;
          dst[y * dst_stride + x + k] =
              (p0 * (mx - k1) + (p1 * k1)) >> upx_log2;
        }
      }
    }
  }

  // OPTIMIZED Interpolate vertically.
  // A function pointer is used to select the SIMD level just once.
  resample_vert_fn fn = NULL;
  switch (bw) {
    case 64: fn = resample_vert_w64_avx2; break;
    case 32: fn = resample_vert_w32_avx2; break;
    case 16: fn = resample_vert_w16_avx2; break;
    case 8: fn = resample_vert_w8_ssse3; break;
  }

  if (fn) {
    for (int i = 0; i < pred_y >> downy_log2; i++) {
      const int y = i * my;
      const uint16_t *p0_row =
          (y == 0) ? above_row : &dst[(y - 1) * dst_stride];
      const uint16_t *p1_row = &dst[(y + my - 1) * dst_stride];
      for (int k = 0; k < my - 1; k++) {
        const int k1 = k + 1;
        const int w0 = my - k1;
        const int w1 = k1;
        uint16_t *dst_row = &dst[(y + k) * dst_stride];
        fn(dst_row, p0_row, p1_row, w0, w1, upy_log2);
      }
    }
  } else {
    // C fallback for other widths.
    // This loop is restructured to be more cache-friendly.
    // The original C code loops column by column. Here we change it to
    // row by row to make it friendly for SIMD.
    for (int i = 0; i < pred_y >> downy_log2; i++) {
      const int y = i * my;
      const uint16_t *p0_row =
          (y == 0) ? above_row : &dst[(y - 1) * dst_stride];
      const uint16_t *p1_row = &dst[(y + my - 1) * dst_stride];
      for (int k = 0; k < my - 1; k++) {
        const int k1 = k + 1;
        const int w0 = my - k1;
        const int w1 = k1;
        uint16_t *dst_row = &dst[(y + k) * dst_stride];
        for (int x = 0; x < bw; x++) {
          const int p0 = p0_row[x];
          const int p1 = p1_row[x];
          dst_row[x] = (uint16_t)((p0 * w0 + p1 * w1) >> upy_log2);
        }
      }
    }
  }
}
