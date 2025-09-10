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

#include "av1/common/cfl.h"

#include "av1/common/x86/cfl_simd.h"

#define CFL_GET_SUBSAMPLE_FUNCTION_AVX2(sub, bd)                        \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 32)                                  \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 16)                                  \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 8)                                   \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 4)                                   \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 64)                                  \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 32)                                  \
  CFL_SUBSAMPLE(avx2, sub, bd, 32, 64)                                  \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 4)                                   \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 8)                                   \
  CFL_SUBSAMPLE(avx2, sub, bd, 64, 16)                                  \
  cfl_subsample_##bd##_fn cfl_get_luma_subsampling_##sub##_##bd##_avx2( \
      TX_SIZE tx_size) {                                                \
    static const cfl_subsample_##bd##_fn subfn_##sub[TX_SIZES_ALL] = {  \
      cfl_subsample_##bd##_##sub##_4x4_ssse3,   /* 4x4 */               \
      cfl_subsample_##bd##_##sub##_8x8_ssse3,   /* 8x8 */               \
      cfl_subsample_##bd##_##sub##_16x16_ssse3, /* 16x16 */             \
      cfl_subsample_##bd##_##sub##_32x32_avx2,  /* 32x32 */             \
      cfl_subsample_##bd##_##sub##_64x64_avx2,  /* 64x64 */             \
      cfl_subsample_##bd##_##sub##_4x8_ssse3,   /* 4x8 */               \
      cfl_subsample_##bd##_##sub##_8x4_ssse3,   /* 8x4 */               \
      cfl_subsample_##bd##_##sub##_8x16_ssse3,  /* 8x16 */              \
      cfl_subsample_##bd##_##sub##_16x8_ssse3,  /* 16x8 */              \
      cfl_subsample_##bd##_##sub##_16x32_ssse3, /* 16x32 */             \
      cfl_subsample_##bd##_##sub##_32x16_avx2,  /* 32x16 */             \
      cfl_subsample_##bd##_##sub##_32x64_avx2,  /* 32x64 */             \
      cfl_subsample_##bd##_##sub##_64x32_avx2,  /* 64x32 */             \
      cfl_subsample_##bd##_##sub##_4x16_ssse3,  /* 4x16  */             \
      cfl_subsample_##bd##_##sub##_16x4_ssse3,  /* 16x4  */             \
      cfl_subsample_##bd##_##sub##_8x32_ssse3,  /* 8x32  */             \
      cfl_subsample_##bd##_##sub##_32x8_avx2,   /* 32x8  */             \
      cfl_subsample_##bd##_##sub##_16x64_ssse3, /* 16x64 */             \
      cfl_subsample_##bd##_##sub##_64x16_avx2,  /* 64x16 */             \
      cfl_subsample_##bd##_##sub##_4x32_ssse3,  /* 4x32  */             \
      cfl_subsample_##bd##_##sub##_32x4_avx2,   /* 32x4  */             \
      cfl_subsample_##bd##_##sub##_8x64_ssse3,  /* 8x64 */              \
      cfl_subsample_##bd##_##sub##_64x8_avx2,   /* 64x8 */              \
      cfl_subsample_##bd##_##sub##_4x64_ssse3,  /* 4x64 */              \
      cfl_subsample_##bd##_##sub##_64x4_avx2,   /* 64x4 */              \
    };                                                                  \
    return subfn_##sub[tx_size];                                        \
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

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // 1. Load 8 pixels for left, center, and right taps.
    const __m128i top_left = _mm_loadu_si128((const __m128i *)(top - 1));
    const __m128i top_center = _mm_loadu_si128((const __m128i *)top);
    const __m128i top_right = _mm_loadu_si128((const __m128i *)(top + 1));

    const __m128i bot_left = _mm_loadu_si128((const __m128i *)(bot - 1));
    const __m128i bot_center = _mm_loadu_si128((const __m128i *)bot);
    const __m128i bot_right = _mm_loadu_si128((const __m128i *)(bot + 1));

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

    // 6. Correct the first pixel for the boundary condition.
    output_q3[0] = 3 * top[0] + top[1] + 3 * bot[0] + bot[1];

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

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // 1. Load 16 pixels for left, center, and right taps for top and bottom.
    // Using 256-bit registers allows us to process the entire width at once.
    const __m256i top_left = _mm256_loadu_si256((const __m256i *)(top - 1));
    const __m256i top_center = _mm256_loadu_si256((const __m256i *)top);
    const __m256i top_right = _mm256_loadu_si256((const __m256i *)(top + 1));

    const __m256i bot_left = _mm256_loadu_si256((const __m256i *)(bot - 1));
    const __m256i bot_center = _mm256_loadu_si256((const __m256i *)bot);
    const __m256i bot_right = _mm256_loadu_si256((const __m256i *)(bot + 1));

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
    // Extract the low and high 128-bit lanes from the 256-bit result.
    const __m128i sum_lo = _mm256_extracti128_si256(sum_unpacked, 0);
    const __m128i sum_hi = _mm256_extracti128_si256(sum_unpacked, 1);

    // Shuffle each lane to select the even-indexed elements.
    const __m128i subsampled_lo = _mm_shuffle_epi8(sum_lo, subsample_mask);
    const __m128i subsampled_hi = _mm_shuffle_epi8(sum_hi, subsample_mask);

    // Combine the two 64-bit results into one 128-bit vector.
    const __m128i final_sum = _mm_unpacklo_epi64(subsampled_lo, subsampled_hi);

    // 5. Store the final 8 subsampled pixels.
    _mm_storeu_si128((__m128i *)output_q3, final_sum);

    // 6. Correct the first pixel to handle the boundary condition, just as
    // the C code does.
    output_q3[0] = 3 * top[0] + top[1] + 3 * bot[0] + bot[1];

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

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // Process first 16 pixels
    {
      const __m256i top_left = _mm256_loadu_si256((const __m256i *)(top - 1));
      const __m256i top_center = _mm256_loadu_si256((const __m256i *)top);
      const __m256i top_right = _mm256_loadu_si256((const __m256i *)(top + 1));
      const __m256i bot_left = _mm256_loadu_si256((const __m256i *)(bot - 1));
      const __m256i bot_center = _mm256_loadu_si256((const __m256i *)bot);
      const __m256i bot_right = _mm256_loadu_si256((const __m256i *)(bot + 1));

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

      _mm_storeu_si128((__m128i *)output_q3, final_sum);
    }

    // Process second 16 pixels
    {
      const __m256i top_left = _mm256_loadu_si256((const __m256i *)(top + 15));
      const __m256i top_center =
          _mm256_loadu_si256((const __m256i *)(top + 16));
      const __m256i top_right = _mm256_loadu_si256((const __m256i *)(top + 17));
      const __m256i bot_left = _mm256_loadu_si256((const __m256i *)(bot + 15));
      const __m256i bot_center =
          _mm256_loadu_si256((const __m256i *)(bot + 16));
      const __m256i bot_right = _mm256_loadu_si256((const __m256i *)(bot + 17));

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

    // Correct the first pixel for the boundary condition.
    output_q3[0] = 3 * top[0] + top[1] + 3 * bot[0] + bot[1];

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

  for (int j = 0; j < height; j += 2) {
    const uint16_t *top = input;
    const uint16_t *bot = input + input_stride;

    // Process four 16-pixel chunks
    for (int i = 0; i < 4; ++i) {
      const int offset = i * 16;
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

      _mm_storeu_si128((__m128i *)(output_q3 + (i * 8)), final_sum);
    }

    // Correct the first pixel for the boundary condition.
    output_q3[0] = 3 * top[0] + top[1] + 3 * bot[0] + bot[1];

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

static INLINE void cfl_predict_hbd_avx2(const int16_t *pred_buf_q3,
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

cfl_predict_hbd_fn cfl_get_predict_hbd_fn_avx2(TX_SIZE tx_size) {
  static const cfl_predict_hbd_fn pred[TX_SIZES_ALL] = {
    cfl_predict_hbd_4x4_ssse3,  /* 4x4 */
    cfl_predict_hbd_8x8_ssse3,  /* 8x8 */
    cfl_predict_hbd_16x16_avx2, /* 16x16 */
    cfl_predict_hbd_32x32_avx2, /* 32x32 */
    NULL,                       /* 64x64 (invalid CFL size) */
    cfl_predict_hbd_4x8_ssse3,  /* 4x8 */
    cfl_predict_hbd_8x4_ssse3,  /* 8x4 */
    cfl_predict_hbd_8x16_ssse3, /* 8x16 */
    cfl_predict_hbd_16x8_avx2,  /* 16x8 */
    cfl_predict_hbd_16x32_avx2, /* 16x32 */
    cfl_predict_hbd_32x16_avx2, /* 32x16 */
    NULL,                       /* 32x64 (invalid CFL size) */
    NULL,                       /* 64x32 (invalid CFL size) */
    cfl_predict_hbd_4x16_ssse3, /* 4x16  */
    cfl_predict_hbd_16x4_avx2,  /* 16x4  */
    cfl_predict_hbd_8x32_ssse3, /* 8x32  */
    cfl_predict_hbd_32x8_avx2,  /* 32x8  */
    NULL,                       /* 16x64 (invalid CFL size) */
    NULL,                       /* 64x16 (invalid CFL size) */
    cfl_predict_hbd_4x32_ssse3, /* 4x32  */
    cfl_predict_hbd_32x4_avx2,  /* 32x4  */
    NULL,                       /* 8x64 (invalid CFL size) */
    NULL,                       /* 64x8 (invalid CFL size) */
    NULL,                       /* 4x64 (invalid CFL size) */
    NULL,                       /* 64x4 (invalid CFL size) */
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
cfl_subtract_average_fn cfl_get_subtract_average_fn_avx2(TX_SIZE tx_size) {
  static const cfl_subtract_average_fn sub_avg[TX_SIZES_ALL] = {
    cfl_subtract_average_4x4_sse2,   /* 4x4 */
    cfl_subtract_average_8x8_sse2,   /* 8x8 */
    cfl_subtract_average_16x16_avx2, /* 16x16 */
    cfl_subtract_average_32x32_avx2, /* 32x32 */
    NULL,                            /* 64x64 (invalid CFL size) */
    cfl_subtract_average_4x8_sse2,   /* 4x8 */
    cfl_subtract_average_8x4_sse2,   /* 8x4 */
    cfl_subtract_average_8x16_sse2,  /* 8x16 */
    cfl_subtract_average_16x8_avx2,  /* 16x8 */
    cfl_subtract_average_16x32_avx2, /* 16x32 */
    cfl_subtract_average_32x16_avx2, /* 32x16 */
    NULL,                            /* 32x64 (invalid CFL size) */
    NULL,                            /* 64x32 (invalid CFL size) */
    cfl_subtract_average_4x16_sse2,  /* 4x16 */
    cfl_subtract_average_16x4_avx2,  /* 16x4 */
    cfl_subtract_average_8x32_sse2,  /* 8x32 */
    cfl_subtract_average_32x8_avx2,  /* 32x8 */
    NULL,                            /* 16x64 (invalid CFL size) */
    NULL,                            /* 64x16 (invalid CFL size) */
    cfl_subtract_average_4x32_sse2,  /* 4x32  */
    cfl_subtract_average_32x4_avx2,  /* 32x4  */
    NULL,                            /* 8x64 (invalid CFL size) */
    NULL,                            /* 64x8 (invalid CFL size) */
    NULL,                            /* 4x64 (invalid CFL size) */
    NULL,                            /* 64x4 (invalid CFL size) */
  };
  // Modulo TX_SIZES_ALL to ensure that an attacker won't be able to
  // index the function pointer array out of bounds.
  return sub_avg[tx_size % TX_SIZES_ALL];
}
