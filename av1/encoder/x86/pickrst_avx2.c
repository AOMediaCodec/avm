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

#include <immintrin.h>  // AVX2
#include "aom_dsp/x86/mem_sse2.h"
#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"
#include "aom_dsp/x86/transpose_sse2.h"

#include "config/av1_rtcd.h"
#include "av1/common/restoration.h"
#include "av1/encoder/pickrst.h"

static INLINE void acc_stat_highbd_avx2(int64_t *dst, const uint16_t *dgd,
                                        const __m256i *shuffle,
                                        const __m256i *dgd_ijkl) {
  // Load two 128-bit chunks from dgd
  const __m256i s0 = _mm256_inserti128_si256(
      _mm256_castsi128_si256(_mm_loadu_si128((__m128i *)dgd)),
      _mm_loadu_si128((__m128i *)(dgd + 4)), 1);
  // s0 = [11 10 9 8 7 6 5 4] [7 6 5 4 3 2 1 0] as u16 (values are dgd indices)
  // The weird order is so the shuffle stays within 128-bit lanes

  // Shuffle 16x u16 values within lanes according to the mask:
  // [0 1 1 2 2 3 3 4] [0 1 1 2 2 3 3 4]
  // (Actually we shuffle u8 values as there's no 16-bit shuffle)
  const __m256i s1 = _mm256_shuffle_epi8(s0, *shuffle);
  // s1 = [8 7 7 6 6 5 5 4] [4 3 3 2 2 1 1 0] as u16 (values are dgd indices)

  // Multiply 16x 16-bit integers in dgd_ijkl and s1, resulting in 16x 32-bit
  // integers then horizontally add pairs of these integers resulting in 8x
  // 32-bit integers
  const __m256i d0 = _mm256_madd_epi16(*dgd_ijkl, s1);
  // d0 = [a b c d] [e f g h] as u32

  // Take the lower-half of d0, extend to u64, add it on to dst (H)
  const __m256i d0l = _mm256_cvtepu32_epi64(_mm256_extracti128_si256(d0, 0));
  // d0l = [a b] [c d] as u64
  const __m256i dst0 = yy_load_256(dst);
  yy_store_256(dst, _mm256_add_epi64(d0l, dst0));

  // Take the upper-half of d0, extend to u64, add it on to dst (H)
  const __m256i d0h = _mm256_cvtepu32_epi64(_mm256_extracti128_si256(d0, 1));
  // d0h = [e f] [g h] as u64
  const __m256i dst1 = yy_load_256(dst + 4);
  yy_store_256(dst + 4, _mm256_add_epi64(d0h, dst1));
}

static INLINE void acc_stat_highbd_win7_one_line_avx2(
    const uint16_t *dgd, const uint16_t *src, int h_start, int h_end,
    int dgd_stride, const __m256i *shuffle, int32_t *sumX,
    int32_t sumY[WIENER_WIN][WIENER_WIN], int64_t M_int[WIENER_WIN][WIENER_WIN],
    int64_t H_int[WIENER_WIN2][WIENER_WIN * 8]) {
  int j, k, l;
  const int wiener_win = WIENER_WIN;
  // Main loop handles two pixels at a time
  // We can assume that h_start is even, since it will always be aligned to
  // a tile edge + some number of restoration units, and both of those will
  // be 64-pixel aligned.
  // However, at the edge of the image, h_end may be odd, so we need to handle
  // that case correctly.
  assert(h_start % 2 == 0);
  const int h_end_even = h_end & ~1;
  const int has_odd_pixel = h_end & 1;
  for (j = h_start; j < h_end_even; j += 2) {
    const uint16_t X1 = src[j];
    const uint16_t X2 = src[j + 1];
    *sumX += X1 + X2;
    const uint16_t *dgd_ij = dgd + j;
    for (k = 0; k < wiener_win; k++) {
      const uint16_t *dgd_ijk = dgd_ij + k * dgd_stride;
      for (l = 0; l < wiener_win; l++) {
        int64_t *H_ = &H_int[(l * wiener_win + k)][0];
        const uint16_t D1 = dgd_ijk[l];
        const uint16_t D2 = dgd_ijk[l + 1];
        sumY[k][l] += D1 + D2;
        M_int[k][l] += D1 * X1 + D2 * X2;

        // Load two u16 values from dgd_ijkl combined as a u32,
        // then broadcast to 8x u32 slots of a 256
        const __m256i dgd_ijkl = _mm256_set1_epi32(loadu_uint32(dgd_ijk + l));
        // dgd_ijkl = [y x y x y x y x] [y x y x y x y x] where each is a u16

        acc_stat_highbd_avx2(H_ + 0 * 8, dgd_ij + 0 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 1 * 8, dgd_ij + 1 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 2 * 8, dgd_ij + 2 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 3 * 8, dgd_ij + 3 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 4 * 8, dgd_ij + 4 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 5 * 8, dgd_ij + 5 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 6 * 8, dgd_ij + 6 * dgd_stride, shuffle,
                             &dgd_ijkl);
      }
    }
  }
  // If the width is odd, add in the final pixel
  if (has_odd_pixel) {
    const uint16_t X1 = src[j];
    *sumX += X1;
    const uint16_t *dgd_ij = dgd + j;
    for (k = 0; k < wiener_win; k++) {
      const uint16_t *dgd_ijk = dgd_ij + k * dgd_stride;
      for (l = 0; l < wiener_win; l++) {
        int64_t *H_ = &H_int[(l * wiener_win + k)][0];
        const uint16_t D1 = dgd_ijk[l];
        sumY[k][l] += D1;
        M_int[k][l] += D1 * X1;

        // The `acc_stat_highbd_avx2` function wants its input to have
        // interleaved copies of two pixels, but we only have one. However, the
        // pixels are (effectively) used as inputs to a multiply-accumulate. So
        // if we set the extra pixel slot to 0, then it is effectively ignored.
        const __m256i dgd_ijkl = _mm256_set1_epi32((uint32_t)D1);

        acc_stat_highbd_avx2(H_ + 0 * 8, dgd_ij + 0 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 1 * 8, dgd_ij + 1 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 2 * 8, dgd_ij + 2 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 3 * 8, dgd_ij + 3 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 4 * 8, dgd_ij + 4 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 5 * 8, dgd_ij + 5 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 6 * 8, dgd_ij + 6 * dgd_stride, shuffle,
                             &dgd_ijkl);
      }
    }
  }
}

static INLINE void compute_stats_highbd_win7_opt_avx2(
    const uint16_t *dgd, const uint16_t *src, int h_start, int h_end,
    int v_start, int v_end, int dgd_stride, int src_stride, int64_t *M,
    int64_t *H, aom_bit_depth_t bit_depth) {
  int i, j, k, l, m, n;
  const int wiener_win = WIENER_WIN;
  const int pixel_count = (h_end - h_start) * (v_end - v_start);
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  const uint16_t avg =
      find_average_highbd(dgd, h_start, h_end, v_start, v_end, dgd_stride);

  int64_t M_int[WIENER_WIN][WIENER_WIN] = { { 0 } };
  DECLARE_ALIGNED(32, int64_t, H_int[WIENER_WIN2][WIENER_WIN * 8]) = { { 0 } };
  int32_t sumY[WIENER_WIN][WIENER_WIN] = { { 0 } };
  int32_t sumX = 0;
  const uint16_t *dgd_win = dgd - wiener_halfwin * dgd_stride - wiener_halfwin;

  const __m256i shuffle = yy_loadu_256(g_shuffle_stats_highbd_data);
  for (j = v_start; j < v_end; j += 64) {
    const int vert_end = AOMMIN(64, v_end - j) + j;
    for (i = j; i < vert_end; i++) {
      acc_stat_highbd_win7_one_line_avx2(
          dgd_win + i * dgd_stride, src + i * src_stride, h_start, h_end,
          dgd_stride, &shuffle, &sumX, sumY, M_int, H_int);
    }
  }

  uint8_t bit_depth_divider = 1;
  if (bit_depth == AOM_BITS_12)
    bit_depth_divider = 16;
  else if (bit_depth == AOM_BITS_10)
    bit_depth_divider = 4;

  const int64_t avg_square_sum = (int64_t)avg * (int64_t)avg * pixel_count;
  for (k = 0; k < wiener_win; k++) {
    for (l = 0; l < wiener_win; l++) {
      const int32_t idx0 = l * wiener_win + k;
      M[idx0] = (M_int[k][l] +
                 (avg_square_sum - (int64_t)avg * (sumX + sumY[k][l]))) /
                bit_depth_divider;
      int64_t *H_ = H + idx0 * wiener_win2;
      int64_t *H_int_ = &H_int[idx0][0];
      for (m = 0; m < wiener_win; m++) {
        for (n = 0; n < wiener_win; n++) {
          H_[m * wiener_win + n] =
              (H_int_[n * 8 + m] +
               (avg_square_sum - (int64_t)avg * (sumY[k][l] + sumY[n][m]))) /
              bit_depth_divider;
        }
      }
    }
  }
}

static INLINE void acc_stat_highbd_win5_one_line_avx2(
    const uint16_t *dgd, const uint16_t *src, int h_start, int h_end,
    int dgd_stride, const __m256i *shuffle, int32_t *sumX,
    int32_t sumY[WIENER_WIN_CHROMA][WIENER_WIN_CHROMA],
    int64_t M_int[WIENER_WIN_CHROMA][WIENER_WIN_CHROMA],
    int64_t H_int[WIENER_WIN2_CHROMA][WIENER_WIN_CHROMA * 8]) {
  int j, k, l;
  const int wiener_win = WIENER_WIN_CHROMA;
  // Main loop handles two pixels at a time
  // We can assume that h_start is even, since it will always be aligned to
  // a tile edge + some number of restoration units, and both of those will
  // be 64-pixel aligned.
  // However, at the edge of the image, h_end may be odd, so we need to handle
  // that case correctly.
  assert(h_start % 2 == 0);
  const int h_end_even = h_end & ~1;
  const int has_odd_pixel = h_end & 1;
  for (j = h_start; j < h_end_even; j += 2) {
    const uint16_t X1 = src[j];
    const uint16_t X2 = src[j + 1];
    *sumX += X1 + X2;
    const uint16_t *dgd_ij = dgd + j;
    for (k = 0; k < wiener_win; k++) {
      const uint16_t *dgd_ijk = dgd_ij + k * dgd_stride;
      for (l = 0; l < wiener_win; l++) {
        int64_t *H_ = &H_int[(l * wiener_win + k)][0];
        const uint16_t D1 = dgd_ijk[l];
        const uint16_t D2 = dgd_ijk[l + 1];
        sumY[k][l] += D1 + D2;
        M_int[k][l] += D1 * X1 + D2 * X2;

        // Load two u16 values from dgd_ijkl combined as a u32,
        // then broadcast to 8x u32 slots of a 256
        const __m256i dgd_ijkl = _mm256_set1_epi32(loadu_uint32(dgd_ijk + l));
        // dgd_ijkl = [x y x y x y x y] [x y x y x y x y] where each is a u16

        acc_stat_highbd_avx2(H_ + 0 * 8, dgd_ij + 0 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 1 * 8, dgd_ij + 1 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 2 * 8, dgd_ij + 2 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 3 * 8, dgd_ij + 3 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 4 * 8, dgd_ij + 4 * dgd_stride, shuffle,
                             &dgd_ijkl);
      }
    }
  }
  // If the width is odd, add in the final pixel
  if (has_odd_pixel) {
    const uint16_t X1 = src[j];
    *sumX += X1;
    const uint16_t *dgd_ij = dgd + j;
    for (k = 0; k < wiener_win; k++) {
      const uint16_t *dgd_ijk = dgd_ij + k * dgd_stride;
      for (l = 0; l < wiener_win; l++) {
        int64_t *H_ = &H_int[(l * wiener_win + k)][0];
        const uint16_t D1 = dgd_ijk[l];
        sumY[k][l] += D1;
        M_int[k][l] += D1 * X1;

        // The `acc_stat_highbd_avx2` function wants its input to have
        // interleaved copies of two pixels, but we only have one. However, the
        // pixels are (effectively) used as inputs to a multiply-accumulate. So
        // if we set the extra pixel slot to 0, then it is effectively ignored.
        const __m256i dgd_ijkl = _mm256_set1_epi32((uint32_t)D1);

        acc_stat_highbd_avx2(H_ + 0 * 8, dgd_ij + 0 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 1 * 8, dgd_ij + 1 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 2 * 8, dgd_ij + 2 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 3 * 8, dgd_ij + 3 * dgd_stride, shuffle,
                             &dgd_ijkl);
        acc_stat_highbd_avx2(H_ + 4 * 8, dgd_ij + 4 * dgd_stride, shuffle,
                             &dgd_ijkl);
      }
    }
  }
}

static INLINE void compute_stats_highbd_win5_opt_avx2(
    const uint16_t *dgd, const uint16_t *src, int h_start, int h_end,
    int v_start, int v_end, int dgd_stride, int src_stride, int64_t *M,
    int64_t *H, aom_bit_depth_t bit_depth) {
  int i, j, k, l, m, n;
  const int wiener_win = WIENER_WIN_CHROMA;
  const int pixel_count = (h_end - h_start) * (v_end - v_start);
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  const uint16_t avg =
      find_average_highbd(dgd, h_start, h_end, v_start, v_end, dgd_stride);

  int64_t M_int64[WIENER_WIN_CHROMA][WIENER_WIN_CHROMA] = { { 0 } };
  DECLARE_ALIGNED(
      32, int64_t,
      H_int64[WIENER_WIN2_CHROMA][WIENER_WIN_CHROMA * 8]) = { { 0 } };
  int32_t sumY[WIENER_WIN_CHROMA][WIENER_WIN_CHROMA] = { { 0 } };
  int32_t sumX = 0;
  const uint16_t *dgd_win = dgd - wiener_halfwin * dgd_stride - wiener_halfwin;

  const __m256i shuffle = yy_loadu_256(g_shuffle_stats_highbd_data);
  for (j = v_start; j < v_end; j += 64) {
    const int vert_end = AOMMIN(64, v_end - j) + j;
    for (i = j; i < vert_end; i++) {
      acc_stat_highbd_win5_one_line_avx2(
          dgd_win + i * dgd_stride, src + i * src_stride, h_start, h_end,
          dgd_stride, &shuffle, &sumX, sumY, M_int64, H_int64);
    }
  }

  uint8_t bit_depth_divider = 1;
  if (bit_depth == AOM_BITS_12)
    bit_depth_divider = 16;
  else if (bit_depth == AOM_BITS_10)
    bit_depth_divider = 4;

  const int64_t avg_square_sum = (int64_t)avg * (int64_t)avg * pixel_count;
  for (k = 0; k < wiener_win; k++) {
    for (l = 0; l < wiener_win; l++) {
      const int32_t idx0 = l * wiener_win + k;
      M[idx0] = (M_int64[k][l] +
                 (avg_square_sum - (int64_t)avg * (sumX + sumY[k][l]))) /
                bit_depth_divider;
      int64_t *H_ = H + idx0 * wiener_win2;
      int64_t *H_int_ = &H_int64[idx0][0];
      for (m = 0; m < wiener_win; m++) {
        for (n = 0; n < wiener_win; n++) {
          H_[m * wiener_win + n] =
              (H_int_[n * 8 + m] +
               (avg_square_sum - (int64_t)avg * (sumY[k][l] + sumY[n][m]))) /
              bit_depth_divider;
        }
      }
    }
  }
}

void av1_compute_stats_highbd_avx2(int wiener_win, const uint16_t *dgd,
                                   const uint16_t *src, int h_start, int h_end,
                                   int v_start, int v_end, int dgd_stride,
                                   int src_stride, int64_t *M, int64_t *H,
                                   aom_bit_depth_t bit_depth) {
  if (wiener_win == WIENER_WIN) {
    compute_stats_highbd_win7_opt_avx2(dgd, src, h_start, h_end, v_start, v_end,
                                       dgd_stride, src_stride, M, H, bit_depth);
  } else if (wiener_win == WIENER_WIN_CHROMA) {
    compute_stats_highbd_win5_opt_avx2(dgd, src, h_start, h_end, v_start, v_end,
                                       dgd_stride, src_stride, M, H, bit_depth);
  } else {
    av1_compute_stats_highbd_c(wiener_win, dgd, src, h_start, h_end, v_start,
                               v_end, dgd_stride, src_stride, M, H, bit_depth);
  }
}
