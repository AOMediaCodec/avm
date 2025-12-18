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

#include "config/av2_rtcd.h"

#include "av2/common/cfl.h"

#include "av2/common/x86/cfl_simd.h"

#define CFL_GET_SUBSAMPLE_FUNCTION_AVX2(sub, bd)                           \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 32)                                     \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 16)                                     \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 8)                                      \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 4)                                      \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 64)                                     \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 32)                                     \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 64)                                     \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 4)                                      \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 8)                                      \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 16)                                     \
  av2_cfl_subsample_##bd##_fn                                              \
      av2_cfl_get_luma_subsampling_##sub##_##bd##_avx2(TX_SIZE tx_size) {  \
    static const av2_cfl_subsample_##bd##_fn subfn_##sub[TX_SIZES_ALL] = { \
      av2_cfl_subsample_##bd##_##sub##_4x4_ssse3,   /* 4x4 */              \
      av2_cfl_subsample_##bd##_##sub##_8x8_ssse3,   /* 8x8 */              \
      av2_cfl_subsample_##bd##_##sub##_16x16_ssse3, /* 16x16 */            \
      av2_cfl_subsample_##bd##_##sub##_32x32_avx2,  /* 32x32 */            \
      av2_cfl_subsample_##bd##_##sub##_64x64_avx2,  /* 64x64 */            \
      av2_cfl_subsample_##bd##_##sub##_4x8_ssse3,   /* 4x8 */              \
      av2_cfl_subsample_##bd##_##sub##_8x4_ssse3,   /* 8x4 */              \
      av2_cfl_subsample_##bd##_##sub##_8x16_ssse3,  /* 8x16 */             \
      av2_cfl_subsample_##bd##_##sub##_16x8_ssse3,  /* 16x8 */             \
      av2_cfl_subsample_##bd##_##sub##_16x32_ssse3, /* 16x32 */            \
      av2_cfl_subsample_##bd##_##sub##_32x16_avx2,  /* 32x16 */            \
      av2_cfl_subsample_##bd##_##sub##_32x64_avx2,  /* 32x64 */            \
      av2_cfl_subsample_##bd##_##sub##_64x32_avx2,  /* 64x32 */            \
      av2_cfl_subsample_##bd##_##sub##_4x16_ssse3,  /* 4x16  */            \
      av2_cfl_subsample_##bd##_##sub##_16x4_ssse3,  /* 16x4  */            \
      av2_cfl_subsample_##bd##_##sub##_8x32_ssse3,  /* 8x32  */            \
      av2_cfl_subsample_##bd##_##sub##_32x8_avx2,   /* 32x8  */            \
      av2_cfl_subsample_##bd##_##sub##_16x64_ssse3, /* 16x64 */            \
      av2_cfl_subsample_##bd##_##sub##_64x16_avx2,  /* 64x16 */            \
      av2_cfl_subsample_##bd##_##sub##_4x32_ssse3,  /* 4x32  */            \
      av2_cfl_subsample_##bd##_##sub##_32x4_avx2,   /* 32x4  */            \
      av2_cfl_subsample_##bd##_##sub##_8x64_ssse3,  /* 8x64 */             \
      av2_cfl_subsample_##bd##_##sub##_64x8_avx2,   /* 64x8 */             \
      av2_cfl_subsample_##bd##_##sub##_4x64_ssse3,  /* 4x64 */             \
      av2_cfl_subsample_##bd##_##sub##_64x4_avx2,   /* 64x4 */             \
    };                                                                     \
    return subfn_##sub[tx_size];                                           \
  }

#define ACCUMULATE_SUMS_64(a0, a1, a2, y)                                      \
  {                                                                            \
    const __m256i prod00 = _mm256_mullo_epi32(a0, a0);                         \
    const __m256i prod01 = _mm256_mullo_epi32(a0, a1);                         \
    const __m256i prod02 = _mm256_mullo_epi32(a0, a2);                         \
    const __m256i prod11 = _mm256_mullo_epi32(a1, a1);                         \
    const __m256i prod12 = _mm256_mullo_epi32(a1, a2);                         \
    const __m256i prod22 = _mm256_mullo_epi32(a2, a2);                         \
    const __m256i prody0 = _mm256_mullo_epi32(a0, y);                          \
    const __m256i prody1 = _mm256_mullo_epi32(a1, y);                          \
    const __m256i prody2 = _mm256_mullo_epi32(a2, y);                          \
                                                                               \
    sum00_lo = _mm256_add_epi64(                                               \
        sum00_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod00)));      \
    sum00_hi = _mm256_add_epi64(                                               \
        sum00_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod00, 1))); \
                                                                               \
    sum01_lo = _mm256_add_epi64(                                               \
        sum01_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod01)));      \
    sum01_hi = _mm256_add_epi64(                                               \
        sum01_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod01, 1))); \
                                                                               \
    sum02_lo = _mm256_add_epi64(                                               \
        sum02_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod02)));      \
    sum02_hi = _mm256_add_epi64(                                               \
        sum02_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod02, 1))); \
                                                                               \
    sum11_lo = _mm256_add_epi64(                                               \
        sum11_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod11)));      \
    sum11_hi = _mm256_add_epi64(                                               \
        sum11_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod11, 1))); \
                                                                               \
    sum12_lo = _mm256_add_epi64(                                               \
        sum12_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod12)));      \
    sum12_hi = _mm256_add_epi64(                                               \
        sum12_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod12, 1))); \
                                                                               \
    sum22_lo = _mm256_add_epi64(                                               \
        sum22_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prod22)));      \
    sum22_hi = _mm256_add_epi64(                                               \
        sum22_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prod22, 1))); \
                                                                               \
    ty0_lo = _mm256_add_epi64(                                                 \
        ty0_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prody0)));        \
    ty0_hi = _mm256_add_epi64(                                                 \
        ty0_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prody0, 1)));   \
                                                                               \
    ty1_lo = _mm256_add_epi64(                                                 \
        ty1_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prody1)));        \
    ty1_hi = _mm256_add_epi64(                                                 \
        ty1_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prody1, 1)));   \
                                                                               \
    ty2_lo = _mm256_add_epi64(                                                 \
        ty2_lo, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(prody2)));        \
    ty2_hi = _mm256_add_epi64(                                                 \
        ty2_hi, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(prody2, 1)));   \
  }

/**
 * Adds 4 pixels (in a 2x2 grid) and multiplies them by 2. Resulting in a more
 * precise version of a box filter 4:2:0 pixel subsampling in Q3.
 *
 * The CfL prediction buffer is always of size CFL_BUF_SQUARE. However, the
 * active area is specified using width and height.
 *
 * Note: We don't need to worry about going over the active area, as long as we
 * stay inside the CfL prediction buffer.
 *
 * Note: For 4:2:0 luma subsampling, the width will never be greater than 16.
 */
static void cfl_luma_subsampling_420_hbd_avx2(const uint16_t *input,
                                              int input_stride,
                                              uint16_t *pred_buf_q3, int width,
                                              int height) {
  const int luma_stride = input_stride << 1;
  __m256i *row = (__m256i *)pred_buf_q3;
  const __m256i *row_end = row + (height >> 1) * CFL_BUF_LINE_I256;
  do {
    if (width == 32) {
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i bot = _mm256_loadu_si256((__m256i *)(input + input_stride));
      __m256i sum = _mm256_add_epi16(top, bot);

      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      __m256i bot_1 =
          _mm256_loadu_si256((__m256i *)(input + 16 + input_stride));
      __m256i sum_1 = _mm256_add_epi16(top_1, bot_1);

      __m256i hsum = _mm256_hadd_epi16(sum, sum_1);
      hsum = _mm256_permute4x64_epi64(hsum, _MM_SHUFFLE(3, 1, 2, 0));
      hsum = _mm256_add_epi16(hsum, hsum);
      _mm256_storeu_si256(row, hsum);
    } else {  // width == 64
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i bot = _mm256_loadu_si256((__m256i *)(input + input_stride));
      __m256i sum = _mm256_add_epi16(top, bot);

      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      __m256i bot_1 =
          _mm256_loadu_si256((__m256i *)(input + 16 + input_stride));
      __m256i sum_1 = _mm256_add_epi16(top_1, bot_1);

      __m256i hsum = _mm256_hadd_epi16(sum, sum_1);
      hsum = _mm256_permute4x64_epi64(hsum, _MM_SHUFFLE(3, 1, 2, 0));
      hsum = _mm256_add_epi16(hsum, hsum);
      _mm256_storeu_si256(row, hsum);

      __m256i top_2 = _mm256_loadu_si256((__m256i *)(input + 32));
      __m256i bot_2 =
          _mm256_loadu_si256((__m256i *)(input + 32 + input_stride));
      __m256i sum_2 = _mm256_add_epi16(top_2, bot_2);

      __m256i top_3 = _mm256_loadu_si256((__m256i *)(input + 48));
      __m256i bot_3 =
          _mm256_loadu_si256((__m256i *)(input + 48 + input_stride));
      __m256i sum_3 = _mm256_add_epi16(top_3, bot_3);

      __m256i hsum_1 = _mm256_hadd_epi16(sum_2, sum_3);
      hsum_1 = _mm256_permute4x64_epi64(hsum_1, _MM_SHUFFLE(3, 1, 2, 0));
      hsum_1 = _mm256_add_epi16(hsum_1, hsum_1);
      _mm256_storeu_si256(row + 1, hsum_1);
    }
    input += luma_stride;
  } while ((row += CFL_BUF_LINE_I256) < row_end);
}

static void cfl_luma_subsampling_420_hbd_121_avx2_w4(const uint16_t *input,
                                                     int input_stride,
                                                     uint16_t *output_q3,
                                                     int height) {
  // Shuffle mask to get {p2, p0} from a 4-element vector.
  const __m128i center_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, 4, 1, 0);
  // Shuffle mask for left taps {p1, p0}, handling the i=0 boundary.
  const __m128i left_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, 2, 1, 0);
  // Shuffle mask for right taps {p3, p1}.
  const __m128i right_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, 6, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const __m128i top = _mm_loadl_epi64((const __m128i *)input);
    const __m128i bot =
        _mm_loadl_epi64((const __m128i *)(input + input_stride));

    const __m128i top_center = _mm_shuffle_epi8(top, center_mask);
    const __m128i top_left = _mm_shuffle_epi8(top, left_mask);
    const __m128i top_right = _mm_shuffle_epi8(top, right_mask);
    __m128i top_sum = _mm_add_epi16(top_left, top_right);
    top_sum = _mm_add_epi16(top_sum, _mm_add_epi16(top_center, top_center));

    const __m128i bot_center = _mm_shuffle_epi8(bot, center_mask);
    const __m128i bot_left = _mm_shuffle_epi8(bot, left_mask);
    const __m128i bot_right = _mm_shuffle_epi8(bot, right_mask);
    __m128i bot_sum = _mm_add_epi16(bot_left, bot_right);
    bot_sum = _mm_add_epi16(bot_sum, _mm_add_epi16(bot_center, bot_center));

    const __m128i final_sum = _mm_add_epi16(top_sum, bot_sum);

    _mm_storeu_si32((void *)output_q3, final_sum);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_121_avx2_w8(const uint16_t *input,
                                                     int input_stride,
                                                     uint16_t *output_q3,
                                                     int height) {
  // Shuffle mask to select {p6, p4, p2, p0} from a 128-bit vector.
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Shuffle mask to clamp left edge: [p0, p0, p1, p2, p3, p4, p5, p6]
  // This mimics the C code's 'left = AVMMAX(0, i - 1)' for i=0
  const __m128i left_edge_mask =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Shuffle mask to clamp right edge: [p1, p2, p3, p4, p5, p6, p7, p7]
  // This mimics the C code's filter for the last pixel (i=6)
  const __m128i right_edge_mask =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // 1. Load 8 center pixels (SAFE).
    const __m128i top_center = _mm_loadu_si128((const __m128i *)top);
    const __m128i bot_center = _mm_loadu_si128((const __m128i *)bot);

    // 1a. Construct left/right taps by shuffling center (SAFE).
    // This avoids reading from (top - 1) and (top + 8).
    const __m128i top_left = _mm_shuffle_epi8(top_center, left_edge_mask);
    const __m128i top_right = _mm_shuffle_epi8(top_center, right_edge_mask);

    const __m128i bot_left = _mm_shuffle_epi8(bot_center, left_edge_mask);
    const __m128i bot_right = _mm_shuffle_epi8(bot_center, right_edge_mask);

    // 2. Apply the horizontal 1-2-1 filter.
    const __m128i center_2x = _mm_add_epi16(top_center, top_center);
    const __m128i top_sum_unpacked =
        _mm_add_epi16(_mm_add_epi16(top_left, top_right), center_2x);

    const __m128i bot_center_2x = _mm_add_epi16(bot_center, bot_center);
    const __m128i bot_sum_unpacked =
        _mm_add_epi16(_mm_add_epi16(bot_left, bot_right), bot_center_2x);

    // 3. Apply the vertical filter.
    const __m128i sum_unpacked =
        _mm_add_epi16(top_sum_unpacked, bot_sum_unpacked);

    // 4. Subsample: Select every other pixel.
    const __m128i final_sum = _mm_shuffle_epi8(sum_unpacked, subsample_mask);

    // 5. Store the final 4 subsampled pixels (64 bits).
    _mm_storel_epi64((__m128i *)output_q3, final_sum);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_121_avx2_w16(const uint16_t *input,
                                                      int input_stride,
                                                      uint16_t *output_q3,
                                                      int height) {
  // SSSE3 shuffle mask to select {p6, p4, p2, p0} from a 128-bit lane.
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // 128-bit shuffle mask to clamp left edge: [p0, p0, p1, p2, p3, p4, p5, p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // 128-bit shuffle mask to clamp right edge: [p9, p10, p11, p12, p13, p14,
  // p15, p15] (relative to the high 128-bit lane)
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // 1. Load 16 center pixels (SAFE).
    const __m256i top_center = _mm256_loadu_si256((const __m256i *)top);
    const __m256i bot_center = _mm256_loadu_si256((const __m256i *)bot);

    // 1a. Extract 128-bit lanes to perform shuffles and alignments.
    const __m128i top_lo =
        _mm256_extracti128_si256(top_center, 0);  // [t7...t0]
    const __m128i top_hi =
        _mm256_extracti128_si256(top_center, 1);  // [t15...t8]
    const __m128i bot_lo =
        _mm256_extracti128_si256(bot_center, 0);  // [b7...b0]
    const __m128i bot_hi =
        _mm256_extracti128_si256(bot_center, 1);  // [b15...b8]

    // 1b. Construct 'left' vectors
    // top_left_lo = [t0, t0, t1, t2, t3, t4, t5, t6]
    const __m128i top_left_lo = _mm_shuffle_epi8(top_lo, left_edge_mask_lo);
    // top_left_hi = [t7, t8, t9, t10, t11, t12, t13, t14]
    const __m128i top_left_hi = _mm_alignr_epi8(top_hi, top_lo, 14);
    const __m256i top_left = _mm256_set_m128i(top_left_hi, top_left_lo);

    const __m128i bot_left_lo = _mm_shuffle_epi8(bot_lo, left_edge_mask_lo);
    const __m128i bot_left_hi = _mm_alignr_epi8(bot_hi, bot_lo, 14);
    const __m256i bot_left = _mm256_set_m128i(bot_left_hi, bot_left_lo);

    // 1c. Construct 'right' vectors
    // top_right_lo = [t1, t2, t3, t4, t5, t6, t7, t8]
    const __m128i top_right_lo = _mm_alignr_epi8(top_hi, top_lo, 2);
    // top_right_hi = [t9, t10, t11, t12, t13, t14, t15, t15]
    const __m128i top_right_hi = _mm_shuffle_epi8(top_hi, right_edge_mask_hi);
    const __m256i top_right = _mm256_set_m128i(top_right_hi, top_right_lo);

    const __m128i bot_right_lo = _mm_alignr_epi8(bot_hi, bot_lo, 2);
    const __m128i bot_right_hi = _mm_shuffle_epi8(bot_hi, right_edge_mask_hi);
    const __m256i bot_right = _mm256_set_m128i(bot_right_hi, bot_right_lo);

    // 2. Apply the horizontal 1-2-1 filter (left + 2*center + right).
    const __m256i center_2x = _mm256_add_epi16(top_center, top_center);
    const __m256i top_sum_unpacked =
        _mm256_add_epi16(_mm256_add_epi16(top_left, top_right), center_2x);

    const __m256i bot_center_2x = _mm256_add_epi16(bot_center, bot_center);
    const __m256i bot_sum_unpacked =
        _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right), bot_center_2x);

    // 3. Apply the vertical filter by adding the top and bottom results.
    const __m256i sum_unpacked =
        _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

    // 4. Subsample: Select every other pixel from the 16 results.
    const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
    const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);

    const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
    const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);

    const __m128i final_sum = _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

    // 5. Store the final 8 subsampled pixels.
    _mm_storeu_si128((__m128i *)output_q3, final_sum);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_121_avx2_w32(const uint16_t *input,
                                                      int input_stride,
                                                      uint16_t *output_q3,
                                                      int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Mask to clamp left edge of the first 16-pixel block: [p0, p0, p1...p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Mask to clamp right edge of the second 16-pixel block: [p25...p31,p31]
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // Process first 16 pixels (Left-clamped)
    {
      // 1. Load center and right taps (SAFE).
      const __m256i top_center = _mm256_loadu_si256((const __m256i *)top);
      const __m256i top_right = _mm256_loadu_si256((const __m256i *)(top + 1));
      const __m256i bot_center = _mm256_loadu_si256((const __m256i *)bot);
      const __m256i bot_right = _mm256_loadu_si256((const __m256i *)(bot + 1));

      // 1a. Extract lanes to build the clamped left-tap vectors.
      const __m128i top_lo =
          _mm256_extracti128_si256(top_center, 0);  // [t7...t0]
      const __m128i top_hi =
          _mm256_extracti128_si256(top_center, 1);  // [t15...t8]
      const __m128i bot_lo = _mm256_extracti128_si256(bot_center, 0);
      const __m128i bot_hi = _mm256_extracti128_si256(bot_center, 1);

      // 1b. Construct 'left' vectors (SAFE)
      // top_left_lo = [t0, t0, t1, t2, t3, t4, t5, t6]
      const __m128i top_left_lo = _mm_shuffle_epi8(top_lo, left_edge_mask_lo);
      // top_left_hi = [t7, t8, t9, t10, t11, t12, t13, t14]
      const __m128i top_left_hi = _mm_alignr_epi8(top_hi, top_lo, 14);
      const __m256i top_left = _mm256_set_m128i(top_left_hi, top_left_lo);

      const __m128i bot_left_lo = _mm_shuffle_epi8(bot_lo, left_edge_mask_lo);
      const __m128i bot_left_hi = _mm_alignr_epi8(bot_hi, bot_lo, 14);
      const __m256i bot_left = _mm256_set_m128i(bot_left_hi, bot_left_lo);

      // 2. Calculate sum
      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)output_q3, final_sum);
    }

    // Process second 16 pixels (Right-clamped)
    {
      // 1. Load left and center taps (SAFE).
      const __m256i top_left = _mm256_loadu_si256((const __m256i *)(top + 15));
      const __m256i top_center =
          _mm256_loadu_si256((const __m256i *)(top + 16));
      const __m256i bot_left = _mm256_loadu_si256((const __m256i *)(bot + 15));
      const __m256i bot_center =
          _mm256_loadu_si256((const __m256i *)(bot + 16));

      // 1a. Extract lanes to build the clamped right-tap vectors.
      const __m128i top_lo =
          _mm256_extracti128_si256(top_center, 0);  // [t23...t16]
      const __m128i top_hi =
          _mm256_extracti128_si256(top_center, 1);  // [t31...t24]
      const __m128i bot_lo = _mm256_extracti128_si256(bot_center, 0);
      const __m128i bot_hi = _mm256_extracti128_si256(bot_center, 1);

      // 1b. Construct 'right' vectors (SAFE)
      // top_right_lo = [t17, t18, t19, t20, t21, t22, t23, t24]
      const __m128i top_right_lo = _mm_alignr_epi8(top_hi, top_lo, 2);
      // top_right_hi = [t25, t26, t27, t28, t29, t30, t31, t31]
      const __m128i top_right_hi = _mm_shuffle_epi8(top_hi, right_edge_mask_hi);
      const __m256i top_right = _mm256_set_m128i(top_right_hi, top_right_lo);

      const __m128i bot_right_lo = _mm_alignr_epi8(bot_hi, bot_lo, 2);
      const __m128i bot_right_hi = _mm_shuffle_epi8(bot_hi, right_edge_mask_hi);
      const __m256i bot_right = _mm256_set_m128i(bot_right_hi, bot_right_lo);

      // 2. Calculate sum
      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)(output_q3 + 8), final_sum);
    }

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_121_avx2_w64(const uint16_t *input,
                                                      int input_stride,
                                                      uint16_t *output_q3,
                                                      int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Mask to clamp left edge of the first 16-pixel block: [p0, p0, p1...p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Mask to clamp right edge of the last 16-pixel block: [p57...p63,p63]
  // (relative to the high 128-bit lane of the last 16-pixel chunk)
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // --- Block 0 (i=0, pixels 0-15): Left-clamped ---
    {
      // 1. Load center and right taps (SAFE).
      const __m256i top_center = _mm256_loadu_si256((const __m256i *)top);
      const __m256i top_right = _mm256_loadu_si256((const __m256i *)(top + 1));
      const __m256i bot_center = _mm256_loadu_si256((const __m256i *)bot);
      const __m256i bot_right = _mm256_loadu_si256((const __m256i *)(bot + 1));

      // 1a. Extract lanes to build the clamped left-tap vectors.
      const __m128i top_lo = _mm256_extracti128_si256(top_center, 0);
      const __m128i top_hi = _mm256_extracti128_si256(top_center, 1);
      const __m128i bot_lo = _mm256_extracti128_si256(bot_center, 0);
      const __m128i bot_hi = _mm256_extracti128_si256(bot_center, 1);

      // 1b. Construct 'left' vectors (SAFE)
      const __m128i top_left_lo = _mm_shuffle_epi8(top_lo, left_edge_mask_lo);
      const __m128i top_left_hi = _mm_alignr_epi8(top_hi, top_lo, 14);
      const __m256i top_left = _mm256_set_m128i(top_left_hi, top_left_lo);

      const __m128i bot_left_lo = _mm_shuffle_epi8(bot_lo, left_edge_mask_lo);
      const __m128i bot_left_hi = _mm_alignr_epi8(bot_hi, bot_lo, 14);
      const __m256i bot_left = _mm256_set_m128i(bot_left_hi, bot_left_lo);

      // 2. Calculate sum
      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)output_q3, final_sum);
    }

    // --- Block 1 (i=1, pixels 16-31): Middle (SAFE) ---
    {
      const int offset = 16;
      const __m256i top_left =
          _mm256_loadu_si256((const __m256i *)(top + offset - 1));
      const __m256i top_center =
          _mm256_loadu_si256((const __m256i *)(top + offset));
      const __m256i top_right =
          _mm256_loadu_si256((const __m256i *)(top + offset + 1));
      const __m256i bot_left =
          _mm256_loadu_si256((const __m256i *)(bot + offset - 1));
      const __m256i bot_center =
          _mm256_loadu_si256((const __m256i *)(bot + offset));
      const __m256i bot_right =
          _mm256_loadu_si256((const __m256i *)(bot + offset + 1));

      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)(output_q3 + 8), final_sum);
    }

    // --- Block 2 (i=2, pixels 32-47): Middle (SAFE) ---
    {
      const int offset = 32;
      const __m256i top_left =
          _mm256_loadu_si256((const __m256i *)(top + offset - 1));
      const __m256i top_center =
          _mm256_loadu_si256((const __m256i *)(top + offset));
      const __m256i top_right =
          _mm256_loadu_si256((const __m256i *)(top + offset + 1));
      const __m256i bot_left =
          _mm256_loadu_si256((const __m256i *)(bot + offset - 1));
      const __m256i bot_center =
          _mm256_loadu_si256((const __m256i *)(bot + offset));
      const __m256i bot_right =
          _mm256_loadu_si256((const __m256i *)(bot + offset + 1));

      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)(output_q3 + 16), final_sum);
    }

    // --- Block 3 (i=3, pixels 48-63): Right-clamped ---
    {
      const int offset = 48;
      // 1. Load left and center taps (SAFE).
      const __m256i top_left =
          _mm256_loadu_si256((const __m256i *)(top + offset - 1));
      const __m256i top_center =
          _mm256_loadu_si256((const __m256i *)(top + offset));
      const __m256i bot_left =
          _mm256_loadu_si256((const __m256i *)(bot + offset - 1));
      const __m256i bot_center =
          _mm256_loadu_si256((const __m256i *)(bot + offset));

      // 1a. Extract lanes to build the clamped right-tap vectors.
      const __m128i top_lo =
          _mm256_extracti128_si256(top_center, 0);  // [t55...t48]
      const __m128i top_hi =
          _mm256_extracti128_si256(top_center, 1);  // [t63...t56]
      const __m128i bot_lo = _mm256_extracti128_si256(bot_center, 0);
      const __m128i bot_hi = _mm256_extracti128_si256(bot_center, 1);

      // 1b. Construct 'right' vectors (SAFE)
      // top_right_lo = [t49, t50, t51, t52, t53, t54, t55, t56]
      const __m128i top_right_lo = _mm_alignr_epi8(top_hi, top_lo, 2);
      // top_right_hi = [t57, t58, t59, t60, t61, t62, t63, t63]
      const __m128i top_right_hi = _mm_shuffle_epi8(top_hi, right_edge_mask_hi);
      const __m256i top_right = _mm256_set_m128i(top_right_hi, top_right_lo);

      const __m128i bot_right_lo = _mm_alignr_epi8(bot_hi, bot_lo, 2);
      const __m128i bot_right_hi = _mm_shuffle_epi8(bot_hi, right_edge_mask_hi);
      const __m256i bot_right = _mm256_set_m128i(bot_right_hi, bot_right_lo);

      // 2. Calculate sum
      const __m256i top_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(top_left, top_right),
                           _mm256_add_epi16(top_center, top_center));
      const __m256i bot_sum_unpacked =
          _mm256_add_epi16(_mm256_add_epi16(bot_left, bot_right),
                           _mm256_add_epi16(bot_center, bot_center));
      const __m256i sum_unpacked =
          _mm256_add_epi16(top_sum_unpacked, bot_sum_unpacked);

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);
      const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
      const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
      const __m128i final_sum =
          _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

      _mm_storeu_si128((__m128i *)(output_q3 + 24), final_sum);
    }

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_121_avx2(const uint16_t *input,
                                                  int input_stride,
                                                  uint16_t *output_q3,
                                                  int width, int height) {
  switch (width) {
    case 4:
      cfl_luma_subsampling_420_hbd_121_avx2_w4(input, input_stride, output_q3,
                                               height);
      break;
    case 8:
      cfl_luma_subsampling_420_hbd_121_avx2_w8(input, input_stride, output_q3,
                                               height);
      break;
    case 16:
      cfl_luma_subsampling_420_hbd_121_avx2_w16(input, input_stride, output_q3,
                                                height);
      break;
    case 32:
      cfl_luma_subsampling_420_hbd_121_avx2_w32(input, input_stride, output_q3,
                                                height);
      break;
    case 64:
      cfl_luma_subsampling_420_hbd_121_avx2_w64(input, input_stride, output_q3,
                                                height);
      break;
    default: assert(0 && "Invalid width.");
  }
}

CFL_GET_SUBSAMPLE_FUNCTION_AVX2(420, hbd)

// AVX2 implementation for cfl_luma_subsampling_420_hbd_colocated_c with width=4
static void cfl_luma_subsampling_420_hbd_colocated_avx2_w4(
    const uint16_t *input, int input_stride, uint16_t *output_q3, int height) {
  // Masks to construct {p2, p0} and {p1, p0} etc. from a 4-pixel vector
  const __m128i center_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, 4, 1, 0);
  const __m128i left_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, 2, 1, 0);
  const __m128i right_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, 6, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top_ptr = ((j & 63) == 0) ? input : (input - input_stride);
    const uint16_t *bot_ptr = input + input_stride;

    const __m128i v_in = _mm_loadl_epi64((const __m128i *)input);
    const __m128i v_top = _mm_loadl_epi64((const __m128i *)top_ptr);
    const __m128i v_bot = _mm_loadl_epi64((const __m128i *)bot_ptr);

    // left + right
    __m128i sum = _mm_add_epi16(_mm_shuffle_epi8(v_in, left_mask),
                                _mm_shuffle_epi8(v_in, right_mask));
    // + top + bottom
    sum = _mm_add_epi16(sum, _mm_shuffle_epi8(v_top, center_mask));
    sum = _mm_add_epi16(sum, _mm_shuffle_epi8(v_bot, center_mask));
    // + 4 * center
    const __m128i center4x =
        _mm_slli_epi16(_mm_shuffle_epi8(v_in, center_mask), 2);
    sum = _mm_add_epi16(sum, center4x);

    _mm_storeu_si32((void *)output_q3, sum);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

// AVX2 implementation for cfl_luma_subsampling_420_hbd_colocated_c with width=8
static void cfl_luma_subsampling_420_hbd_colocated_avx2_w8(
    const uint16_t *input, int input_stride, uint16_t *output_q3, int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Shuffle mask to clamp left edge: [p0, p0, p1, p2, p3, p4, p5, p6]
  const __m128i left_edge_mask =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Shuffle mask to clamp right edge: [p1, p2, p3, p4, p5, p6, p7, p7]
  const __m128i right_edge_mask =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top_ptr = ((j & 63) == 0) ? input : (input - input_stride);
    const uint16_t *bot_ptr = input + input_stride;

    // 1. Load center, top, and bottom taps (SAFE).
    const __m128i center = _mm_loadu_si128((const __m128i *)input);
    const __m128i top = _mm_loadu_si128((const __m128i *)top_ptr);
    const __m128i bottom = _mm_loadu_si128((const __m128i *)bot_ptr);

    // 2. Construct left and right taps from center (SAFE).
    const __m128i left = _mm_shuffle_epi8(center, left_edge_mask);
    const __m128i right = _mm_shuffle_epi8(center, right_edge_mask);

    // 3. Perform filter calculation.
    __m128i sum = _mm_add_epi16(left, right);
    sum = _mm_add_epi16(sum, top);
    sum = _mm_add_epi16(sum, bottom);
    const __m128i center4x = _mm_slli_epi16(center, 2);  // 4 * center
    sum = _mm_add_epi16(sum, center4x);

    // 4. Subsample and store.
    const __m128i subsampled = _mm_shuffle_epi8(sum, subsample_mask);
    _mm_storel_epi64((__m128i *)output_q3, subsampled);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

// AVX2 implementation for cfl_luma_subsampling_420_hbd_colocated_c with
// width=16
static void cfl_luma_subsampling_420_hbd_colocated_avx2_w16(
    const uint16_t *input, int input_stride, uint16_t *output_q3, int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // 128-bit shuffle mask to clamp left edge: [p0, p0, p1, p2, p3, p4, p5, p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // 128-bit shuffle mask to clamp right edge: [p9, p10, p11, p12, p13, p14,
  // p15, p15] (relative to the high 128-bit lane)
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top_ptr = ((j & 63) == 0) ? input : (input - input_stride);
    const uint16_t *bot_ptr = input + input_stride;

    // 1. Load center, top, and bottom taps (SAFE).
    const __m256i center = _mm256_loadu_si256((const __m256i *)input);
    const __m256i top = _mm256_loadu_si256((const __m256i *)top_ptr);
    const __m256i bottom = _mm256_loadu_si256((const __m256i *)bot_ptr);

    // 1a. Extract 128-bit lanes from center to build left/right taps.
    const __m128i center_lo = _mm256_extracti128_si256(center, 0);  // [t7...t0]
    const __m128i center_hi =
        _mm256_extracti128_si256(center, 1);  // [t15...t8]

    // 1b. Construct 'left' vector (SAFE)
    // left_lo = [t0, t0, t1, t2, t3, t4, t5, t6]
    const __m128i left_lo = _mm_shuffle_epi8(center_lo, left_edge_mask_lo);
    // left_hi = [t7, t8, t9, t10, t11, t12, t13, t14]
    const __m128i left_hi = _mm_alignr_epi8(center_hi, center_lo, 14);
    const __m256i left = _mm256_set_m128i(left_hi, left_lo);

    // 1c. Construct 'right' vector (SAFE)
    // right_lo = [t1, t2, t3, t4, t5, t6, t7, t8]
    const __m128i right_lo = _mm_alignr_epi8(center_hi, center_lo, 2);
    // right_hi = [t9, t10, t11, t12, t13, t14, t15, t15]
    const __m128i right_hi = _mm_shuffle_epi8(center_hi, right_edge_mask_hi);
    const __m256i right = _mm256_set_m128i(right_hi, right_lo);

    // 2. Apply the filter.
    __m256i sum = _mm256_add_epi16(left, right);
    sum = _mm256_add_epi16(sum, top);
    sum = _mm256_add_epi16(sum, bottom);
    const __m256i center4x = _mm256_slli_epi16(center, 2);  // 4 * center
    sum = _mm256_add_epi16(sum, center4x);

    // 3. Subsample and store.
    const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
    const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
    const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
    const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);
    const __m128i final_sum = _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

    _mm_storeu_si128((__m128i *)output_q3, final_sum);

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

// AVX2 implementation for cfl_luma_subsampling_420_hbd_colocated_c with
// width=32
static void cfl_luma_subsampling_420_hbd_colocated_avx2_w32(
    const uint16_t *input, int input_stride, uint16_t *output_q3, int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Mask to clamp left edge: [p0, p0, p1...p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Mask to clamp right edge: [p25...p31,p31]
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top_ptr = ((j & 63) == 0) ? input : (input - input_stride);
    const uint16_t *bot_ptr = input + input_stride;

    // Process first 16 pixels (Left-clamped)
    {
      // 1. Load center, right, top, bottom (all SAFE).
      const __m256i center = _mm256_loadu_si256((const __m256i *)input);
      const __m256i right = _mm256_loadu_si256((const __m256i *)(input + 1));
      const __m256i top = _mm256_loadu_si256((const __m256i *)top_ptr);
      const __m256i bot = _mm256_loadu_si256((const __m256i *)bot_ptr);

      // 1a. Extract lanes from center to build 'left' vector.
      const __m128i center_lo = _mm256_extracti128_si256(center, 0);
      const __m128i center_hi = _mm256_extracti128_si256(center, 1);

      // 1b. Construct 'left' vector (SAFE)
      const __m128i left_lo = _mm_shuffle_epi8(center_lo, left_edge_mask_lo);
      const __m128i left_hi = _mm_alignr_epi8(center_hi, center_lo, 14);
      const __m256i left = _mm256_set_m128i(left_hi, left_lo);

      // 2. Calculate sum
      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)output_q3, subsampled);
    }

    // Process second 16 pixels (Right-clamped)
    {
      // 1. Load left, center, top, bottom (all SAFE).
      const __m256i left = _mm256_loadu_si256((const __m256i *)(input + 15));
      const __m256i center = _mm256_loadu_si256((const __m256i *)(input + 16));
      const __m256i top = _mm256_loadu_si256((const __m256i *)(top_ptr + 16));
      const __m256i bot = _mm256_loadu_si256((const __m256i *)(bot_ptr + 16));

      // 1a. Extract lanes from center to build 'right' vector.
      const __m128i center_lo =
          _mm256_extracti128_si256(center, 0);  // [t23...t16]
      const __m128i center_hi =
          _mm256_extracti128_si256(center, 1);  // [t31...t24]

      // 1b. Construct 'right' vector (SAFE)
      const __m128i right_lo = _mm_alignr_epi8(center_hi, center_lo, 2);
      const __m128i right_hi = _mm_shuffle_epi8(center_hi, right_edge_mask_hi);
      const __m256i right = _mm256_set_m128i(right_hi, right_lo);

      // 2. Calculate sum
      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)(output_q3 + 8), subsampled);
    }

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

// AVX2 implementation for cfl_luma_subsampling_420_hbd_colocated_c with
// width=64
static void cfl_luma_subsampling_420_hbd_colocated_avx2_w64(
    const uint16_t *input, int input_stride, uint16_t *output_q3, int height) {
  const __m128i subsample_mask =
      _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);

  // Mask to clamp left edge of the first 16-pixel block: [p0, p0, p1...p6]
  const __m128i left_edge_mask_lo =
      _mm_set_epi8(13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 0);

  // Mask to clamp right edge of the last 16-pixel block: [p57...p63,p63]
  // (relative to the high 128-bit lane of the last 16-pixel chunk)
  const __m128i right_edge_mask_hi =
      _mm_set_epi8(15, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top_ptr = ((j & 63) == 0) ? input : (input - input_stride);
    const uint16_t *bot_ptr = input + input_stride;

    // --- Block 0 (i=0, pixels 0-15): Left-clamped ---
    {
      // 1. Load center, right, top, bottom (all SAFE).
      const __m256i center = _mm256_loadu_si256((const __m256i *)input);
      const __m256i right = _mm256_loadu_si256((const __m256i *)(input + 1));
      const __m256i top = _mm256_loadu_si256((const __m256i *)top_ptr);
      const __m256i bot = _mm256_loadu_si256((const __m256i *)bot_ptr);

      // 1a. Extract lanes to build the clamped left-tap vector.
      const __m128i center_lo = _mm256_extracti128_si256(center, 0);
      const __m128i center_hi = _mm256_extracti128_si256(center, 1);

      // 1b. Construct 'left' vector (SAFE)
      const __m128i left_lo = _mm_shuffle_epi8(center_lo, left_edge_mask_lo);
      const __m128i left_hi = _mm_alignr_epi8(center_hi, center_lo, 14);
      const __m256i left = _mm256_set_m128i(left_hi, left_lo);

      // 2. Calculate sum
      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)output_q3, subsampled);
    }

    // --- Block 1 (i=1, pixels 16-31): Middle (SAFE) ---
    {
      const int offset = 16;
      const __m256i left =
          _mm256_loadu_si256((const __m256i *)(input + offset - 1));
      const __m256i center =
          _mm256_loadu_si256((const __m256i *)(input + offset));
      const __m256i right =
          _mm256_loadu_si256((const __m256i *)(input + offset + 1));
      const __m256i top =
          _mm256_loadu_si256((const __m256i *)(top_ptr + offset));
      const __m256i bot =
          _mm256_loadu_si256((const __m256i *)(bot_ptr + offset));

      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)(output_q3 + 8), subsampled);
    }

    // --- Block 2 (i=2, pixels 32-47): Middle (SAFE) ---
    {
      const int offset = 32;
      const __m256i left =
          _mm256_loadu_si256((const __m256i *)(input + offset - 1));
      const __m256i center =
          _mm256_loadu_si256((const __m256i *)(input + offset));
      const __m256i right =
          _mm256_loadu_si256((const __m256i *)(input + offset + 1));
      const __m256i top =
          _mm256_loadu_si256((const __m256i *)(top_ptr + offset));
      const __m256i bot =
          _mm256_loadu_si256((const __m256i *)(bot_ptr + offset));

      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)(output_q3 + 16), subsampled);
    }

    // --- Block 3 (i=3, pixels 48-63): Right-clamped ---
    {
      const int offset = 48;
      // 1. Load left, center, top, bottom (all SAFE).
      const __m256i left =
          _mm256_loadu_si256((const __m256i *)(input + offset - 1));
      const __m256i center =
          _mm256_loadu_si256((const __m256i *)(input + offset));
      const __m256i top =
          _mm256_loadu_si256((const __m256i *)(top_ptr + offset));
      const __m256i bot =
          _mm256_loadu_si256((const __m256i *)(bot_ptr + offset));

      // 1a. Extract lanes to build the clamped right-tap vector.
      const __m128i center_lo =
          _mm256_extracti128_si256(center, 0);  // [t55...t48]
      const __m128i center_hi =
          _mm256_extracti128_si256(center, 1);  // [t63...t56]

      // 1b. Construct 'right' vector (SAFE)
      const __m128i right_lo = _mm_alignr_epi8(center_hi, center_lo, 2);
      const __m128i right_hi = _mm_shuffle_epi8(center_hi, right_edge_mask_hi);
      const __m256i right = _mm256_set_m128i(right_hi, right_lo);

      // 2. Calculate sum
      __m256i sum = _mm256_add_epi16(left, right);
      sum = _mm256_add_epi16(sum, top);
      sum = _mm256_add_epi16(sum, bot);
      sum = _mm256_add_epi16(sum, _mm256_slli_epi16(center, 2));

      // 3. Subsample and store
      const __m128i sum_lo = _mm256_extracti128_si256(sum, 0);
      const __m128i sum_hi = _mm256_extracti128_si256(sum, 1);
      const __m128i subsampled =
          _mm_unpacklo_epi64(_mm_shuffle_epi8(sum_lo, subsample_mask),
                             _mm_shuffle_epi8(sum_hi, subsample_mask));
      _mm_storeu_si128((__m128i *)(output_q3 + 24), subsampled);
    }

    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_colocated_avx2(const uint16_t *input,
                                                        int input_stride,
                                                        uint16_t *output_q3,
                                                        int width, int height) {
  switch (width) {
    case 4:
      cfl_luma_subsampling_420_hbd_colocated_avx2_w4(input, input_stride,
                                                     output_q3, height);
      break;
    case 8:
      cfl_luma_subsampling_420_hbd_colocated_avx2_w8(input, input_stride,
                                                     output_q3, height);
      break;
    case 16:
      cfl_luma_subsampling_420_hbd_colocated_avx2_w16(input, input_stride,
                                                      output_q3, height);
      break;
    case 32:
      cfl_luma_subsampling_420_hbd_colocated_avx2_w32(input, input_stride,
                                                      output_q3, height);
      break;
    case 64:
      cfl_luma_subsampling_420_hbd_colocated_avx2_w64(input, input_stride,
                                                      output_q3, height);
      break;
    default: assert(0 && "Invalid width."); break;
  }
}

/**
 * Adds 2 pixels (in a 2x1 grid) and multiplies them by 4. Resulting in a more
 * precise version of a box filter 4:2:2 pixel subsampling in Q3.
 *
 * The CfL prediction buffer is always of size CFL_BUF_SQUARE. However, the
 * active area is specified using width and height.
 *
 * Note: We don't need to worry about going over the active area, as long as we
 * stay inside the CfL prediction buffer.
 *
 */
static void cfl_luma_subsampling_422_hbd_avx2(const uint16_t *input,
                                              int input_stride,
                                              uint16_t *pred_buf_q3, int width,
                                              int height) {
  __m256i *row = (__m256i *)pred_buf_q3;
  const __m256i *row_end = row + height * CFL_BUF_LINE_I256;
  do {
    if (width == 32) {
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      __m256i hsum = _mm256_hadd_epi16(top, top_1);
      hsum = _mm256_permute4x64_epi64(hsum, _MM_SHUFFLE(3, 1, 2, 0));
      hsum = _mm256_slli_epi16(hsum, 2);
      _mm256_storeu_si256(row, hsum);
    } else {  // width == 64
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      __m256i hsum = _mm256_hadd_epi16(top, top_1);
      hsum = _mm256_permute4x64_epi64(hsum, _MM_SHUFFLE(3, 1, 2, 0));
      hsum = _mm256_slli_epi16(hsum, 2);
      _mm256_storeu_si256(row, hsum);

      __m256i top_2 = _mm256_loadu_si256((__m256i *)(input + 32));
      __m256i top_3 = _mm256_loadu_si256((__m256i *)(input + 48));
      __m256i hsum_1 = _mm256_hadd_epi16(top_2, top_3);
      hsum_1 = _mm256_permute4x64_epi64(hsum_1, _MM_SHUFFLE(3, 1, 2, 0));
      hsum_1 = _mm256_slli_epi16(hsum_1, 2);
      _mm256_storeu_si256(row + 1, hsum_1);
    }
    input += input_stride;
  } while ((row += CFL_BUF_LINE_I256) < row_end);
}

CFL_GET_SUBSAMPLE_FUNCTION_AVX2(422, hbd)

static void cfl_luma_subsampling_444_hbd_avx2(const uint16_t *input,
                                              int input_stride,
                                              uint16_t *pred_buf_q3, int width,
                                              int height) {
  __m256i *row = (__m256i *)pred_buf_q3;
  const __m256i *row_end = row + height * CFL_BUF_LINE_I256;
  do {
    if (width == 32) {
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      _mm256_storeu_si256(row, _mm256_slli_epi16(top, 3));
      _mm256_storeu_si256(row + 1, _mm256_slli_epi16(top_1, 3));
    } else {  // width == 64
      __m256i top = _mm256_loadu_si256((__m256i *)input);
      __m256i top_1 = _mm256_loadu_si256((__m256i *)(input + 16));
      _mm256_storeu_si256(row, _mm256_slli_epi16(top, 3));
      _mm256_storeu_si256(row + 1, _mm256_slli_epi16(top_1, 3));
      __m256i top_2 = _mm256_loadu_si256((__m256i *)(input + 32));
      __m256i top_3 = _mm256_loadu_si256((__m256i *)(input + 48));
      _mm256_storeu_si256(row + 2, _mm256_slli_epi16(top_2, 3));
      _mm256_storeu_si256(row + 3, _mm256_slli_epi16(top_3, 3));
    }
    input += input_stride;
  } while ((row += CFL_BUF_LINE_I256) < row_end);
}

CFL_GET_SUBSAMPLE_FUNCTION_AVX2(444, hbd)

CFL_GET_SUBSAMPLE_121_FUNCTION(avx2)

CFL_GET_SUBSAMPLE_COLOCATED_FUNCTION(avx2)

static INLINE __m256i predict_unclipped(const __m256i *input, __m256i alpha_q12,
                                        __m256i alpha_sign, __m256i dc_q0) {
  __m256i ac_q3 = _mm256_loadu_si256(input);
  __m256i ac_sign = _mm256_sign_epi16(alpha_sign, ac_q3);
  __m256i scaled_luma_q0 =
      _mm256_mulhrs_epi16(_mm256_abs_epi16(ac_q3), alpha_q12);
  scaled_luma_q0 = _mm256_sign_epi16(scaled_luma_q0, ac_sign);
  return _mm256_add_epi16(scaled_luma_q0, dc_q0);
}

static __m256i highbd_max_epi16(int bd) {
  const __m256i neg_one = _mm256_set1_epi16(-1);
  // (1 << bd) - 1 => -(-1 << bd) -1 => -1 - (-1 << bd) => -1 ^ (-1 << bd)
  return _mm256_xor_si256(_mm256_slli_epi16(neg_one, bd), neg_one);
}

static __m256i highbd_clamp_epi16(__m256i u, __m256i zero, __m256i max) {
  return _mm256_max_epi16(_mm256_min_epi16(u, max), zero);
}

static INLINE void av2_cfl_predict_hbd_avx2(const int16_t *pred_buf_q3,
                                            uint16_t *dst, int dst_stride,
                                            int alpha_q3, int bd, int width,
                                            int height) {
  // Use SSSE3 version for smaller widths
  assert(width == 16 || width == 32);
  const __m256i alpha_sign = _mm256_set1_epi16(alpha_q3);
  const __m256i alpha_q12 =
      _mm256_slli_epi16(_mm256_abs_epi16(alpha_sign), (9 - CFL_ADD_BITS_ALPHA));
  const __m256i dc_q0 = _mm256_loadu_si256((__m256i *)dst);
  const __m256i max = highbd_max_epi16(bd);

  __m256i *row = (__m256i *)pred_buf_q3;
  const __m256i *row_end = row + height * CFL_BUF_LINE_I256;
  do {
    const __m256i res = predict_unclipped(row, alpha_q12, alpha_sign, dc_q0);
    _mm256_storeu_si256((__m256i *)dst,
                        highbd_clamp_epi16(res, _mm256_setzero_si256(), max));
    if (width == 32) {
      const __m256i res_1 =
          predict_unclipped(row + 1, alpha_q12, alpha_sign, dc_q0);
      _mm256_storeu_si256(
          (__m256i *)(dst + 16),
          highbd_clamp_epi16(res_1, _mm256_setzero_si256(), max));
    }
    dst += dst_stride;
  } while ((row += CFL_BUF_LINE_I256) < row_end);
}

CFL_PREDICT_X(avx2, 16, 4, hbd)
CFL_PREDICT_X(avx2, 16, 8, hbd)
CFL_PREDICT_X(avx2, 16, 16, hbd)
CFL_PREDICT_X(avx2, 16, 32, hbd)
CFL_PREDICT_X(avx2, 32, 8, hbd)
CFL_PREDICT_X(avx2, 32, 16, hbd)
CFL_PREDICT_X(avx2, 32, 32, hbd)
CFL_PREDICT_X(avx2, 32, 4, hbd)

av2_cfl_predict_hbd_fn av2_cfl_get_predict_hbd_fn_avx2(TX_SIZE tx_size) {
  static const av2_cfl_predict_hbd_fn pred[TX_SIZES_ALL] = {
    av2_cfl_predict_hbd_4x4_ssse3,  /* 4x4 */
    av2_cfl_predict_hbd_8x8_ssse3,  /* 8x8 */
    av2_cfl_predict_hbd_16x16_avx2, /* 16x16 */
    av2_cfl_predict_hbd_32x32_avx2, /* 32x32 */
    NULL,                           /* 64x64 (invalid CFL size) */
    av2_cfl_predict_hbd_4x8_ssse3,  /* 4x8 */
    av2_cfl_predict_hbd_8x4_ssse3,  /* 8x4 */
    av2_cfl_predict_hbd_8x16_ssse3, /* 8x16 */
    av2_cfl_predict_hbd_16x8_avx2,  /* 16x8 */
    av2_cfl_predict_hbd_16x32_avx2, /* 16x32 */
    av2_cfl_predict_hbd_32x16_avx2, /* 32x16 */
    NULL,                           /* 32x64 (invalid CFL size) */
    NULL,                           /* 64x32 (invalid CFL size) */
    av2_cfl_predict_hbd_4x16_ssse3, /* 4x16  */
    av2_cfl_predict_hbd_16x4_avx2,  /* 16x4  */
    av2_cfl_predict_hbd_8x32_ssse3, /* 8x32  */
    av2_cfl_predict_hbd_32x8_avx2,  /* 32x8  */
    NULL,                           /* 16x64 (invalid CFL size) */
    NULL,                           /* 64x16 (invalid CFL size) */
    av2_cfl_predict_hbd_4x32_ssse3, /* 4x32  */
    av2_cfl_predict_hbd_32x4_avx2,  /* 32x4  */
    NULL,                           /* 8x64 (invalid CFL size) */
    NULL,                           /* 64x8 (invalid CFL size) */
    NULL,                           /* 4x64 (invalid CFL size) */
    NULL,                           /* 64x4 (invalid CFL size) */
  };
  // Modulo TX_SIZES_ALL to ensure that an attacker won't be able to index the
  // function pointer array out of bounds.
  return pred[tx_size % TX_SIZES_ALL];
}

// Returns a vector where all the (32-bits) elements are the sum of all the
// lanes in a.
static INLINE __m256i fill_sum_epi32(__m256i a) {
  // Given that a == [A, B, C, D, E, F, G, H]
  a = _mm256_hadd_epi32(a, a);
  // Given that A' == A + B, C' == C + D, E' == E + F, G' == G + H
  // a == [A', C', A', C', E', G', E', G']
  a = _mm256_permute4x64_epi64(a, _MM_SHUFFLE(3, 1, 2, 0));
  // a == [A', C', E', G', A', C', E', G']
  a = _mm256_hadd_epi32(a, a);
  // Given that A'' == A' + C' and E'' == E' + G'
  // a == [A'', E'', A'', E'', A'', E'', A'', E'']
  return _mm256_hadd_epi32(a, a);
  // Given that A''' == A'' + E''
  // a == [A''', A''', A''', A''', A''', A''', A''', A''']
}

static INLINE __m256i _mm256_addl_epi16(__m256i a) {
  return _mm256_add_epi32(_mm256_unpacklo_epi16(a, _mm256_setzero_si256()),
                          _mm256_unpackhi_epi16(a, _mm256_setzero_si256()));
}

static INLINE void subtract_average_avx2(const uint16_t *src_ptr,
                                         int16_t *dst_ptr, int width,
                                         int height, int round_offset,
                                         int num_pel_log2) {
  // Use SSE2 version for smaller widths
  assert(width == 16 || width == 32);

  const __m256i *src = (__m256i *)src_ptr;
  const __m256i *const end = src + height * CFL_BUF_LINE_I256;
  // To maximize usage of the AVX2 registers, we sum two rows per loop
  // iteration
  const int step = 2 * CFL_BUF_LINE_I256;

  __m256i sum = _mm256_setzero_si256();
  // For width 32, we use a second sum accumulator to reduce accumulator
  // dependencies in the loop.
  __m256i sum2;
  if (width == 32) sum2 = _mm256_setzero_si256();

  do {
    // Add top row to the bottom row
    __m256i l0 = _mm256_add_epi16(_mm256_loadu_si256(src),
                                  _mm256_loadu_si256(src + CFL_BUF_LINE_I256));
    sum = _mm256_add_epi32(sum, _mm256_addl_epi16(l0));
    if (width == 32) { /* Don't worry, this if it gets optimized out. */
      // Add the second part of the top row to the second part of the bottom row
      __m256i l1 =
          _mm256_add_epi16(_mm256_loadu_si256(src + 1),
                           _mm256_loadu_si256(src + 1 + CFL_BUF_LINE_I256));
      sum2 = _mm256_add_epi32(sum2, _mm256_addl_epi16(l1));
    }
    src += step;
  } while (src < end);
  // Combine both sum accumulators
  if (width == 32) sum = _mm256_add_epi32(sum, sum2);

  __m256i fill = fill_sum_epi32(sum);

  __m256i avg_epi16 = _mm256_srli_epi32(
      _mm256_add_epi32(fill, _mm256_set1_epi32(round_offset)), num_pel_log2);
  avg_epi16 = _mm256_packs_epi32(avg_epi16, avg_epi16);

  // Store and subtract loop
  src = (__m256i *)src_ptr;
  __m256i *dst = (__m256i *)dst_ptr;
  do {
    _mm256_storeu_si256(dst,
                        _mm256_sub_epi16(_mm256_loadu_si256(src), avg_epi16));
    if (width == 32) {
      _mm256_storeu_si256(
          dst + 1, _mm256_sub_epi16(_mm256_loadu_si256(src + 1), avg_epi16));
    }
    src += CFL_BUF_LINE_I256;
    dst += CFL_BUF_LINE_I256;
  } while (src < end);
}

// Declare wrappers for AVX2 sizes
CFL_SUB_AVG_X(avx2, 16, 4, 32, 6)
CFL_SUB_AVG_X(avx2, 16, 8, 64, 7)
CFL_SUB_AVG_X(avx2, 16, 16, 128, 8)
CFL_SUB_AVG_X(avx2, 16, 32, 256, 9)
CFL_SUB_AVG_X(avx2, 32, 8, 128, 8)
CFL_SUB_AVG_X(avx2, 32, 16, 256, 9)
CFL_SUB_AVG_X(avx2, 32, 32, 512, 10)
CFL_SUB_AVG_X(avx2, 32, 4, 64, 7)

// Based on the observation that for small blocks AVX2 does not outperform
// SSE2, we call the SSE2 code for block widths 4 and 8.
av2_cfl_subtract_average_fn av2_cfl_get_subtract_average_fn_avx2(
    TX_SIZE tx_size) {
  static const av2_cfl_subtract_average_fn sub_avg[TX_SIZES_ALL] = {
    av2_cfl_subtract_average_4x4_sse2,   /* 4x4 */
    av2_cfl_subtract_average_8x8_sse2,   /* 8x8 */
    av2_cfl_subtract_average_16x16_avx2, /* 16x16 */
    av2_cfl_subtract_average_32x32_avx2, /* 32x32 */
    NULL,                                /* 64x64 (invalid CFL size) */
    av2_cfl_subtract_average_4x8_sse2,   /* 4x8 */
    av2_cfl_subtract_average_8x4_sse2,   /* 8x4 */
    av2_cfl_subtract_average_8x16_sse2,  /* 8x16 */
    av2_cfl_subtract_average_16x8_avx2,  /* 16x8 */
    av2_cfl_subtract_average_16x32_avx2, /* 16x32 */
    av2_cfl_subtract_average_32x16_avx2, /* 32x16 */
    NULL,                                /* 32x64 (invalid CFL size) */
    NULL,                                /* 64x32 (invalid CFL size) */
    av2_cfl_subtract_average_4x16_sse2,  /* 4x16 */
    av2_cfl_subtract_average_16x4_avx2,  /* 16x4 */
    av2_cfl_subtract_average_8x32_sse2,  /* 8x32 */
    av2_cfl_subtract_average_32x8_avx2,  /* 32x8 */
    NULL,                                /* 16x64 (invalid CFL size) */
    NULL,                                /* 64x16 (invalid CFL size) */
    av2_cfl_subtract_average_4x32_sse2,  /* 4x32  */
    av2_cfl_subtract_average_32x4_avx2,  /* 32x4  */
    NULL,                                /* 8x64 (invalid CFL size) */
    NULL,                                /* 64x8 (invalid CFL size) */
    NULL,                                /* 4x64 (invalid CFL size) */
    NULL,                                /* 64x4 (invalid CFL size) */
  };
  // Modulo TX_SIZES_ALL to ensure that an attacker won't be able to
  // index the function pointer array out of bounds.
  return sub_avg[tx_size % TX_SIZES_ALL];
}

static INLINE __m256i non_linear_avx2(__m256i v, __m256i mid, int bit_depth) {
  __m256i v2 = _mm256_mullo_epi32(v, v);
  v2 = _mm256_add_epi32(v2, mid);
  v2 = _mm256_srai_epi32(v2, bit_depth);
  return v2;
}

static INLINE __m256i convert_int64_to_int32_avx2(__m256i a, __m256i b) {
  // Extract low 32-bit  of each 64-bit element from a and b
  const __m256 low32 = _mm256_shuffle_ps(
      _mm256_castsi256_ps(a), _mm256_castsi256_ps(b), _MM_SHUFFLE(2, 0, 2, 0));

  // a0 a1 a2 a3 b0 b1 b2 b3
  const __m256d ordered =
      _mm256_permute4x64_pd(_mm256_castps_pd(low32), _MM_SHUFFLE(3, 1, 2, 0));

  return _mm256_castpd_si256(ordered);
}

static INLINE __m256i floor_log2_uint32_avx2(__m256i x) {
  const __m256i zero_mask = _mm256_cmpeq_epi32(x, _mm256_setzero_si256());

  const __m128i x_lo = _mm256_castsi256_si128(x);
  const __m128i x_hi = _mm256_extracti128_si256(x, 1);

  // Convert int32 to double to access exponent bits
  const __m256d d_lo = _mm256_cvtepi32_pd(x_lo);
  const __m256d d_hi = _mm256_cvtepi32_pd(x_hi);

  const __m256i bits_lo = _mm256_castpd_si256(d_lo);
  const __m256i bits_hi = _mm256_castpd_si256(d_hi);

  // Extract exponent bits [52..62]
  const __m256i exp_lo = _mm256_srli_epi64(bits_lo, 52);
  const __m256i exp_hi = _mm256_srli_epi64(bits_hi, 52);

  __m256i exp = convert_int64_to_int32_avx2(exp_lo, exp_hi);
  // Subtract exponent bias
  exp = _mm256_sub_epi32(exp, _mm256_set1_epi32(1023));
  // Set result to 0 for input x = 0
  exp = _mm256_andnot_si256(zero_mask, exp);
  return exp;
}

static INLINE __m256i mul_fixed32_adapt_avx2(__m256i a, __m256i b,
                                             __m256i shift, __m256i bits_a) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i one = _mm256_set1_epi32(1);

  // Compute the bit-width of |b|
  __m256i bits_b = floor_log2_uint32_avx2(_mm256_abs_epi32(b));
  bits_b = _mm256_add_epi32(bits_b, one);

  const int bits_limit = 29;
  // Decide how many bits to drop in total to avoid mul overflow
  __m256i need = _mm256_sub_epi32(_mm256_add_epi32(bits_a, bits_b),
                                  _mm256_set1_epi32(bits_limit));
  need = _mm256_max_epi32(need, zero);

  // Split the drop across a and b to minimize error
  const __m256i s1 = _mm256_srai_epi32(need, 1);
  const __m256i s2 = _mm256_sub_epi32(need, s1);

  // Adjust dropped bits in final shift
  const __m256i adj = _mm256_sub_epi32(shift, need);

  // Safe 32-bit product
  __m256i prod =
      _mm256_mullo_epi32(_mm256_srav_epi32(a, s1), _mm256_srav_epi32(b, s2));

  // Compute bias and bias=0 when adj is 0
  const __m256i bias_mask = _mm256_cmpgt_epi32(adj, zero);
  const __m256i adj_minus_1 =
      _mm256_max_epi32(_mm256_sub_epi32(adj, one), zero);
  const __m256i bias =
      _mm256_and_si256(bias_mask, _mm256_sllv_epi32(one, adj_minus_1));

  // Final right shift with symmetric rounding to nearest
  const __m256i sign = _mm256_srai_epi32(prod, 31);
  prod = _mm256_add_epi32(_mm256_add_epi32(prod, bias), sign);
  return _mm256_srav_epi32(prod, adj);
}

#define CONVOLVE_FIXED32_AND_STORE                                            \
  {                                                                           \
    __m256i sum =                                                             \
        mul_fixed32_adapt_avx2(alpha[0], v, mhccp_decim_bits, bits_alpha[0]); \
    sum = _mm256_add_epi32(                                                   \
        sum, mul_fixed32_adapt_avx2(alpha[1], v2, mhccp_decim_bits,           \
                                    bits_alpha[1]));                          \
    sum = _mm256_add_epi32(                                                   \
        sum, mul_fixed32_adapt_avx2(alpha[2], mid, mhccp_decim_bits,          \
                                    bits_alpha[2]));                          \
    sum = _mm256_max_epi32(sum, zero);                                        \
    sum = _mm256_min_epi32(sum, pixel_max);                                   \
                                                                              \
    const __m128i res_lo = _mm256_castsi256_si128(sum);                       \
    const __m128i res_hi = _mm256_extracti128_si256(sum, 1);                  \
    const __m128i res = _mm_packs_epi32(res_lo, res_hi);                      \
                                                                              \
    if (width > 4)                                                            \
      _mm_storeu_si128((__m128i *)(dst + i), res);                            \
    else                                                                      \
      _mm_storeu_si64((__m128i *)(dst + i), res);                             \
  }

static INLINE __m256i load_and_shift(const uint16_t *ptr) {
  __m256i v = _mm256_cvtepu16_epi32(_mm_loadu_si128((__m128i const *)ptr));
  v = _mm256_srli_epi32(v, 3);
  return v;
}

void mhccp_predict_hv_hbd_avx2(const uint16_t *input, uint16_t *dst,
                               bool have_top, bool have_left, int dst_stride,
                               int32_t *alpha_q3, int bit_depth, int width,
                               int height, int dir) {
  const __m256i mid = _mm256_set1_epi32(1 << (bit_depth - 1));
  const __m256i pixel_max = _mm256_set1_epi32((1 << bit_depth) - 1);
  const __m256i mhccp_decim_bits = _mm256_set1_epi32(MHCCP_DECIM_BITS);
  const __m256i zero = _mm256_setzero_si256();
  assert(MHCCP_NUM_PARAMS == 3);

  __m256i alpha[MHCCP_NUM_PARAMS];
  __m256i bits_alpha[MHCCP_NUM_PARAMS];

  for (int i = 0; i < MHCCP_NUM_PARAMS; i++) {
    alpha[i] = _mm256_set1_epi32(alpha_q3[i]);

    const uint32_t abs_a =
        (uint32_t)(alpha_q3[i] < 0 ? -alpha_q3[i] : alpha_q3[i]);
    const int bits_a = ilog2_32(abs_a) + 1;
    bits_alpha[i] = _mm256_set1_epi32(bits_a);
  }

  if (dir == 0) {
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i += 8) {
        const __m256i v = load_and_shift(input + i);

        const __m256i v2 = non_linear_avx2(v, mid, bit_depth);

        CONVOLVE_FIXED32_AND_STORE
      }
      input += CFL_BUF_LINE * 2;
      dst += dst_stride;
    }
  } else if (dir == 1) {
    const uint16_t *top_row = !have_top ? input : &input[-2 * CFL_BUF_LINE];
    const uint16_t *cur_row = input;

    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i += 8) {
        const __m256i v = load_and_shift(top_row + i);

        const __m256i v2 =
            non_linear_avx2(load_and_shift(cur_row + i), mid, bit_depth);

        CONVOLVE_FIXED32_AND_STORE
      }
      top_row = cur_row;
      cur_row += CFL_BUF_LINE * 2;
      dst += dst_stride;
    }
  } else {
    assert(dir == 2);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i += 8) {
        const __m256i cur = load_and_shift(input + i);
        __m256i v;
        if (i == 0 && !have_left) {
          const __m256i idx = _mm256_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6);
          v = _mm256_permutevar8x32_epi32(cur, idx);
        } else {
          v = load_and_shift(input + i - 1);
        }

        const __m256i v2 = non_linear_avx2(cur, mid, bit_depth);

        CONVOLVE_FIXED32_AND_STORE
      }
      input += CFL_BUF_LINE * 2;
      dst += dst_stride;
    }
  }
}

// Horizontal sum of 4x 64-bit integers
static INLINE int32_t hsum_epi64_to_i32_avx2(__m256i v) {
  __m128i lo128 = _mm256_castsi256_si128(v);
  __m128i hi128 = _mm256_extracti128_si256(v, 1);
  __m128i sum128 = _mm_add_epi64(lo128, hi128);
  __m128i shuf = _mm_shuffle_epi32(sum128, _MM_SHUFFLE(1, 0, 3, 2));
  sum128 = _mm_add_epi64(sum128, shuf);
  return _mm_cvtsi128_si32(sum128);
}

#define NON_LINEAR(V, M, BD) ((V * V + M) >> BD)
// Derives multi-parameter chroma prediction coefficients from neighboring luma
// and chroma reference samples.
void av2_mhccp_derive_multi_param_hv_avx2(MACROBLOCKD *const xd, int plane,
                                          int above_lines, int left_lines,
                                          int ref_width, int ref_height,
                                          int dir, int is_top_sb_boundary) {
  const CFL_CTX *const cfl = &xd->cfl;
  MB_MODE_INFO *mbmi = xd->mi[0];

  int count = 0;
  int16_t A[MHCCP_NUM_PARAMS][MHCCP_MAX_REF_SAMPLES];
  uint16_t YCb[MHCCP_MAX_REF_SAMPLES];

  if (above_lines || left_lines) {
    const int16_t mid = 1 << (xd->bd - 1);
    const uint16_t *l = cfl->mhccp_ref_buf_q3[AVM_PLANE_Y];
    const uint16_t *c = cfl->mhccp_ref_buf_q3[plane];
    const int ref_stride = CFL_BUF_LINE * 2;
    assert(dir >= 0 && dir <= 2);
    const int dir_offset = (dir == 0) ? 0 : (dir == 1 ? -ref_stride : -1);

    const __m256i mid32 = _mm256_set1_epi32(mid);
    const __m256i mid16 = _mm256_set1_epi16(mid);

    for (int j = 1; j < ref_height - 1; ++j) {
      int ref_h_offset = 0;
      if (is_top_sb_boundary && above_lines == (LINE_NUM + 1) &&
          j < above_lines)
        ref_h_offset = above_lines - 1 - j;

      const int base = (j + ref_h_offset) * ref_stride;
      int lines = ref_width - 1;
      if (j >= above_lines) lines = AVMMIN(lines, left_lines);

      const uint16_t *ptrl = l + base + 1;                   // for NON_LINEAR
      const uint16_t *ptrl_dir = l + base + 1 + dir_offset;  // for A0
      const uint16_t *ptrc = c + base + 1;

      int i = 1;
      for (; i < lines;) {
        const int remaining = lines - i;
        if (remaining >= 16) {
          const __m256i l_vec =
              _mm256_loadu_si256((const __m256i *)(ptrl_dir + i - 1));
          const __m256i l_ref_vec =
              _mm256_loadu_si256((const __m256i *)(ptrl + i - 1));
          const __m256i c_vec =
              _mm256_loadu_si256((const __m256i *)(ptrc + i - 1));

          const __m256i a0 = _mm256_srli_epi16(l_vec, 3);
          _mm256_storeu_si256((__m256i *)(A[0] + count), a0);
          _mm256_storeu_si256((__m256i *)(YCb + count), c_vec);

          const __m256i v16 = _mm256_srli_epi16(l_ref_vec, 3);

          const __m128i v_lo = _mm256_castsi256_si128(v16);
          const __m128i v_hi = _mm256_extracti128_si256(v16, 1);

          const __m256i v_lo32 = _mm256_cvtepi16_epi32(v_lo);
          const __m256i v_hi32 = _mm256_cvtepi16_epi32(v_hi);

          const __m256i v_lo_sq = non_linear_avx2(v_lo32, mid32, xd->bd);
          const __m256i v_hi_sq = non_linear_avx2(v_hi32, mid32, xd->bd);

          const __m256i mask = _mm256_set1_epi32(0xFFFF);
          const __m256i v_lo_masked = _mm256_and_si256(v_lo_sq, mask);
          const __m256i v_hi_masked = _mm256_and_si256(v_hi_sq, mask);
          const __m128i v_lo_lo128 = _mm256_castsi256_si128(v_lo_masked);
          const __m128i v_lo_hi128 = _mm256_extracti128_si256(v_lo_masked, 1);
          const __m128i packed_lo = _mm_packus_epi32(v_lo_lo128, v_lo_hi128);

          const __m128i v_hi_lo128 = _mm256_castsi256_si128(v_hi_masked);
          const __m128i v_hi_hi128 = _mm256_extracti128_si256(v_hi_masked, 1);
          const __m128i packed_hi = _mm_packus_epi32(v_hi_lo128, v_hi_hi128);
          const __m256i result = _mm256_set_m128i(packed_hi, packed_lo);

          _mm256_storeu_si256((__m256i *)&A[1][count], result);
          _mm256_storeu_si256((__m256i *)(A[2] + count), mid16);

          count += 16;
          i += 16;
        } else {
          for (; i < lines; ++i) {
            A[0][count] = ptrl_dir[i - 1] >> 3;
            A[1][count] = NON_LINEAR((ptrl[i - 1] >> 3), mid, xd->bd);
            A[2][count] = mid;
            YCb[count] = ptrc[i - 1];
            ++count;
          }
        }
      }
    }
  }

  if (count > 0) {
    int32_t ATA[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS];
    int32_t Ty[MHCCP_NUM_PARAMS];
    // One more column is added to store the derived parameters
    int32_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1];

    __m256i sum00_lo = _mm256_setzero_si256(), sum00_hi = sum00_lo;
    __m256i sum01_lo = sum00_lo, sum01_hi = sum00_lo;
    __m256i sum02_lo = sum00_lo, sum02_hi = sum00_lo;
    __m256i sum11_lo = sum00_lo, sum11_hi = sum00_lo;
    __m256i sum12_lo = sum00_lo, sum12_hi = sum00_lo;
    __m256i sum22_lo = sum00_lo, sum22_hi = sum00_lo;

    __m256i ty0_lo = sum00_lo, ty0_hi = sum00_lo;
    __m256i ty1_lo = sum00_lo, ty1_hi = sum00_lo;
    __m256i ty2_lo = sum00_lo, ty2_hi = sum00_lo;

    int k = 0;
    for (; k + 15 < count; k += 16) {
      const __m256i v0_256 = _mm256_loadu_si256((const __m256i *)&A[0][k]);
      const __m256i v1_256 = _mm256_loadu_si256((const __m256i *)&A[1][k]);
      const __m256i v2_256 = _mm256_loadu_si256((const __m256i *)&A[2][k]);
      const __m256i y_256 = _mm256_loadu_si256((const __m256i *)&YCb[k]);

      // Unpack low/high halves to 32-bit
      const __m256i v0_lo =
          _mm256_cvtepi16_epi32(_mm256_castsi256_si128(v0_256));
      const __m256i v0_hi =
          _mm256_cvtepi16_epi32(_mm256_extracti128_si256(v0_256, 1));
      const __m256i v1_lo =
          _mm256_cvtepi16_epi32(_mm256_castsi256_si128(v1_256));
      const __m256i v1_hi =
          _mm256_cvtepi16_epi32(_mm256_extracti128_si256(v1_256, 1));
      const __m256i v2_lo =
          _mm256_cvtepi16_epi32(_mm256_castsi256_si128(v2_256));
      const __m256i v2_hi =
          _mm256_cvtepi16_epi32(_mm256_extracti128_si256(v2_256, 1));

      const __m256i y_lo = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(y_256));
      const __m256i y_hi =
          _mm256_cvtepu16_epi32(_mm256_extracti128_si256(y_256, 1));

      ACCUMULATE_SUMS_64(v0_lo, v1_lo, v2_lo, y_lo)
      ACCUMULATE_SUMS_64(v0_hi, v1_hi, v2_hi, y_hi)
    }

    ATA[0][0] =
        hsum_epi64_to_i32_avx2(sum00_lo) + hsum_epi64_to_i32_avx2(sum00_hi);
    ATA[0][1] =
        hsum_epi64_to_i32_avx2(sum01_lo) + hsum_epi64_to_i32_avx2(sum01_hi);
    ATA[0][2] =
        hsum_epi64_to_i32_avx2(sum02_lo) + hsum_epi64_to_i32_avx2(sum02_hi);
    ATA[1][1] =
        hsum_epi64_to_i32_avx2(sum11_lo) + hsum_epi64_to_i32_avx2(sum11_hi);
    ATA[1][2] =
        hsum_epi64_to_i32_avx2(sum12_lo) + hsum_epi64_to_i32_avx2(sum12_hi);
    ATA[2][2] =
        hsum_epi64_to_i32_avx2(sum22_lo) + hsum_epi64_to_i32_avx2(sum22_hi);
    ATA[1][0] = 0;
    ATA[2][0] = 0;
    ATA[2][1] = 0;

    Ty[0] = hsum_epi64_to_i32_avx2(ty0_lo) + hsum_epi64_to_i32_avx2(ty0_hi);
    Ty[1] = hsum_epi64_to_i32_avx2(ty1_lo) + hsum_epi64_to_i32_avx2(ty1_hi);
    Ty[2] = hsum_epi64_to_i32_avx2(ty2_lo) + hsum_epi64_to_i32_avx2(ty2_hi);

    for (; k < count; ++k) {
      const int16_t a0 = A[0][k];
      const int16_t a1 = A[1][k];
      const int16_t a2 = A[2][k];

      ATA[0][0] += a0 * a0;
      ATA[0][1] += a0 * a1;
      ATA[0][2] += a0 * a2;
      ATA[1][1] += a1 * a1;
      ATA[1][2] += a1 * a2;
      ATA[2][2] += a2 * a2;

      Ty[0] += a0 * YCb[k];
      Ty[1] += a1 * YCb[k];
      Ty[2] += a2 * YCb[k];
    }

    // Scale the matrix and vector to selected dynamic range
    const int matrix_shift =
        (MHCCP_DECIM_BITS + 6) - 2 * xd->bd - (int)ceil(log2(count));

    if (matrix_shift > 0) {
      for (int i = 0; i < MHCCP_NUM_PARAMS; ++i) {
        Ty[i] <<= matrix_shift;
        for (int j = 0; j < MHCCP_NUM_PARAMS; ++j) ATA[i][j] <<= matrix_shift;
      }
    } else if (matrix_shift < 0) {
      for (int i = 0; i < MHCCP_NUM_PARAMS; ++i) {
        Ty[i] >>= -matrix_shift;
        for (int j = 0; j < MHCCP_NUM_PARAMS; ++j) ATA[i][j] >>= -matrix_shift;
      }
    }

    gauss_elimination_mhccp(ATA, C, Ty, mbmi->mhccp_implicit_param[plane - 1],
                            MHCCP_NUM_PARAMS, xd->bd);
  } else {
    for (int i = 0; i < MHCCP_NUM_PARAMS - 1; ++i)
      mbmi->mhccp_implicit_param[plane - 1][i] = 0;
    mbmi->mhccp_implicit_param[plane - 1][MHCCP_NUM_PARAMS - 1] =
        1 << MHCCP_DECIM_BITS;
  }
}
