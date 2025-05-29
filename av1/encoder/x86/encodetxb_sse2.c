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

#include <assert.h>
#include <emmintrin.h>  // SSE2

#include "aom/aom_integer.h"
#include "aom_dsp/x86/mem_sse2.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/txb_common.h"

static INLINE void load_levels_4x4x5_sse2(const uint8_t *const src,
                                          const int stride,
                                          const ptrdiff_t *const offsets,
                                          __m128i *const level) {
  level[0] = load_8bit_4x4_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_4x4_to_1_reg_sse2(src + stride, stride);
  level[2] = load_8bit_4x4_to_1_reg_sse2(src + offsets[0], stride);
  level[3] = load_8bit_4x4_to_1_reg_sse2(src + offsets[1], stride);
  level[4] = load_8bit_4x4_to_1_reg_sse2(src + offsets[2], stride);
}

static INLINE void load_levels_4x4x3_sse2(const uint8_t *const src,
                                          const int stride,
                                          __m128i *const level) {
  level[0] = load_8bit_4x4_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_4x4_to_1_reg_sse2(src + stride, stride);
  level[2] = load_8bit_4x4_to_1_reg_sse2(src + stride + 1, stride);
}

static INLINE void load_levels_4x4x2_sse2(const uint8_t *const src,
                                          const int stride,
                                          __m128i *const level) {
  level[0] = load_8bit_4x4_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_4x4_to_1_reg_sse2(src + stride, stride);
}

static INLINE void load_levels_8x2x5_sse2(const uint8_t *const src,
                                          const int stride,
                                          const ptrdiff_t *const offsets,
                                          __m128i *const level) {
  level[0] = load_8bit_8x2_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_8x2_to_1_reg_sse2(src + stride, stride);
  level[2] = load_8bit_8x2_to_1_reg_sse2(src + offsets[0], stride);
  level[3] = load_8bit_8x2_to_1_reg_sse2(src + offsets[1], stride);
  level[4] = load_8bit_8x2_to_1_reg_sse2(src + offsets[2], stride);
}

static INLINE void load_levels_8x2x3_sse2(const uint8_t *const src,
                                          const int stride,
                                          __m128i *const level) {
  level[0] = load_8bit_8x2_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_8x2_to_1_reg_sse2(src + stride, stride);
  level[2] = load_8bit_8x2_to_1_reg_sse2(src + stride + 1, stride);
}

static INLINE void load_levels_8x2x2_sse2(const uint8_t *const src,
                                          const int stride,
                                          __m128i *const level) {
  level[0] = load_8bit_8x2_to_1_reg_sse2(src + 1, stride);
  level[1] = load_8bit_8x2_to_1_reg_sse2(src + stride, stride);
}

static INLINE void load_levels_16x1x5_sse2(const uint8_t *const src,
                                           const int stride,
                                           const ptrdiff_t *const offsets,
                                           __m128i *const level) {
  level[0] = _mm_loadu_si128((__m128i *)(src + 1));
  level[1] = _mm_loadu_si128((__m128i *)(src + stride));
  level[2] = _mm_loadu_si128((__m128i *)(src + offsets[0]));
  level[3] = _mm_loadu_si128((__m128i *)(src + offsets[1]));
  level[4] = _mm_loadu_si128((__m128i *)(src + offsets[2]));
}

static INLINE void load_levels_16x1x3_sse2(const uint8_t *const src,
                                           const int stride,
                                           __m128i *const level) {
  level[0] = _mm_loadu_si128((__m128i *)(src + 1));
  level[1] = _mm_loadu_si128((__m128i *)(src + stride));
  level[2] = _mm_loadu_si128((__m128i *)(src + stride + 1));
}

static INLINE void load_levels_16x1x2_sse2(const uint8_t *const src,
                                           const int stride,
                                           __m128i *const level) {
  level[0] = _mm_loadu_si128((__m128i *)(src + 1));
  level[1] = _mm_loadu_si128((__m128i *)(src + stride));
}

static INLINE __m128i get_coeff_contexts_2d_kernel_sse2(__m128i *const level,
                                                        __m128i clip1,
                                                        __m128i clip2) {
  __m128i count;

  count = _mm_min_epu8(level[0], clip1);
  level[1] = _mm_min_epu8(level[1], clip1);
  level[2] = _mm_min_epu8(level[2], clip1);
  level[3] = _mm_min_epu8(level[3], clip1);
  level[4] = _mm_min_epu8(level[4], clip1);
  count = _mm_add_epi8(count, level[1]);
  count = _mm_add_epi8(count, level[2]);
  count = _mm_add_epi8(count, level[3]);
  count = _mm_add_epi8(count, level[4]);
  count = _mm_avg_epu8(count, _mm_setzero_si128());
  count = _mm_min_epu8(count, clip2);
  return count;
}

static INLINE void get_4_nz_map_contexts_2d(const uint8_t *levels,
                                            const int height,
                                            const ptrdiff_t *const offsets,
                                            int8_t *const coeff_contexts) {
  const int stride = 4 + TX_PAD_HOR;
  __m128i pos_to_offset[3], pos_to_clip1[2], pos_to_clip2[2];
  pos_to_offset[0] =
      _mm_setr_epi8(0, 9, 16, 16, 9, 16, 16, 0, 16, 16, 0, 0, 16, 0, 0, 5);
  pos_to_offset[1] =
      _mm_setr_epi8(0, 0, 5, 5, 0, 5, 5, 10, 5, 5, 10, 10, 5, 10, 10, 10);
  pos_to_offset[2] = _mm_set1_epi8(10);
  pos_to_clip1[0] =
      _mm_setr_epi8(5, 5, 5, 5, 5, 5, 5, 3, 5, 5, 3, 3, 5, 3, 3, 3);
  pos_to_clip1[1] = _mm_set1_epi8(3);
  pos_to_clip2[0] =
      _mm_setr_epi8(8, 6, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
  pos_to_clip2[1] = _mm_set1_epi8(4);

  __m128i count;
  __m128i level[5];
  int8_t *cc = coeff_contexts;
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x5_sse2(levels, stride, offsets, level);
    count = get_coeff_contexts_2d_kernel_sse2(level, pos_to_clip1[0],
                                              pos_to_clip2[0]);
    count = _mm_add_epi8(count, pos_to_offset[0]);
    _mm_store_si128((__m128i *)cc, count);
    levels += 4 * stride;
    pos_to_offset[0] = pos_to_offset[1];
    pos_to_offset[1] = pos_to_offset[2];
    pos_to_clip1[0] = pos_to_clip1[1];
    pos_to_clip2[0] = pos_to_clip2[1];
    cc += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_2d(const uint8_t *levels,
                                           const int height,
                                           const ptrdiff_t *const offsets,
                                           int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_HOR;
  int8_t *cc = coeff_contexts;
  int row = height;
  __m128i count;
  __m128i level[5];
  __m128i pos_to_offset[5], pos_to_clip1[3], pos_to_clip2[2];
  pos_to_offset[0] =
      _mm_setr_epi8(0, 9, 16, 16, 0, 0, 5, 5, 9, 16, 16, 0, 0, 5, 5, 10);
  pos_to_offset[1] =
      _mm_setr_epi8(16, 16, 0, 0, 5, 5, 10, 10, 16, 0, 0, 5, 5, 10, 10, 10);
  pos_to_offset[2] =
      _mm_setr_epi8(0, 0, 5, 5, 10, 10, 10, 10, 0, 5, 5, 10, 10, 10, 10, 10);
  pos_to_offset[3] = _mm_setr_epi8(5, 5, 10, 10, 10, 10, 10, 10, 5, 10, 10, 10,
                                   10, 10, 10, 10);
  pos_to_offset[4] = _mm_set1_epi8(10);
  pos_to_clip1[0] =
      _mm_setr_epi8(5, 5, 5, 5, 3, 3, 3, 3, 5, 5, 5, 3, 3, 3, 3, 3);
  pos_to_clip1[1] =
      _mm_setr_epi8(5, 5, 3, 3, 3, 3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3);
  pos_to_clip1[2] = _mm_set1_epi8(3);
  pos_to_clip2[0] =
      _mm_setr_epi8(8, 6, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4);
  pos_to_clip2[1] = _mm_set1_epi8(4);

  assert(!(height % 2));

  do {
    load_levels_8x2x5_sse2(levels, stride, offsets, level);
    count = get_coeff_contexts_2d_kernel_sse2(level, pos_to_clip1[0],
                                              pos_to_clip2[0]);
    count = _mm_add_epi8(count, pos_to_offset[0]);
    _mm_store_si128((__m128i *)cc, count);
    levels += 2 * stride;
    pos_to_offset[0] = pos_to_offset[1];
    pos_to_offset[1] = pos_to_offset[2];
    pos_to_offset[2] = pos_to_offset[3];
    pos_to_offset[3] = pos_to_offset[4];
    pos_to_clip1[0] = pos_to_clip1[1];
    pos_to_clip1[1] = pos_to_clip1[2];
    pos_to_clip2[0] = pos_to_clip2[1];
    cc += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_2d(const uint8_t *levels,
                                             const int width, const int height,
                                             const ptrdiff_t *const offsets,
                                             int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_HOR;
  int8_t *cc = coeff_contexts;
  int row = height;
  __m128i count;
  __m128i level[5];
  __m128i pos_to_offset[8], pos_to_clip1[4], pos_to_clip2[2];
  pos_to_offset[0] =
      _mm_setr_epi8(0, 9, 16, 16, 0, 0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10);
  pos_to_offset[1] =
      _mm_setr_epi8(9, 16, 16, 0, 0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10);
  pos_to_offset[2] =
      _mm_setr_epi8(16, 16, 0, 0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10);
  pos_to_offset[3] =
      _mm_setr_epi8(16, 0, 0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10);
  pos_to_offset[4] =
      _mm_setr_epi8(0, 0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10);
  pos_to_offset[5] = _mm_setr_epi8(0, 5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                   10, 10, 10, 10);
  pos_to_offset[6] = _mm_setr_epi8(5, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                   10, 10, 10, 10);
  pos_to_offset[7] = _mm_setr_epi8(5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                   10, 10, 10, 10, 10);
  const __m128i pos_to_offset_large = _mm_set1_epi8(10);
  pos_to_clip1[0] =
      _mm_setr_epi8(5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  pos_to_clip1[1] =
      _mm_setr_epi8(5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  pos_to_clip1[2] =
      _mm_setr_epi8(5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  pos_to_clip1[3] =
      _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip1_large = _mm_set1_epi8(3);
  pos_to_clip2[0] =
      _mm_setr_epi8(8, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
  pos_to_clip2[1] =
      _mm_setr_epi8(6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
  const __m128i pos_to_clip2_large = _mm_set1_epi8(4);

  assert(!(width % 16));

  do {
    int w = width;

    do {
      load_levels_16x1x5_sse2(levels, stride, offsets, level);
      count = get_coeff_contexts_2d_kernel_sse2(level, pos_to_clip1[0],
                                                pos_to_clip2[0]);
      count = _mm_add_epi8(count, pos_to_offset[0]);
      _mm_store_si128((__m128i *)cc, count);
      levels += 16;
      cc += 16;
      w -= 16;
      pos_to_offset[0] = pos_to_offset_large;
      pos_to_clip1[0] = pos_to_clip1_large;
      pos_to_clip2[0] = pos_to_clip2_large;
    } while (w);
    levels += TX_PAD_HOR;
    pos_to_offset[0] = pos_to_offset[1];
    pos_to_offset[1] = pos_to_offset[2];
    pos_to_offset[2] = pos_to_offset[3];
    pos_to_offset[3] = pos_to_offset[4];
    pos_to_offset[4] = pos_to_offset[5];
    pos_to_offset[5] = pos_to_offset[6];
    pos_to_offset[6] = pos_to_offset[7];
    pos_to_offset[7] = pos_to_offset_large;
    pos_to_clip1[0] = pos_to_clip1[1];
    pos_to_clip1[1] = pos_to_clip1[2];
    pos_to_clip1[2] = pos_to_clip1[3];
    pos_to_clip1[3] = pos_to_clip1_large;
    pos_to_clip2[0] = pos_to_clip2[1];
    pos_to_clip2[1] = pos_to_clip2_large;
  } while (--row);
}

static INLINE __m128i get_coeff_contexts_1d_kernel_sse2(__m128i *const level,
                                                        __m128i clip1,
                                                        __m128i clip2) {
  const __m128i const_3 = _mm_set1_epi8(3);
  __m128i count;

  count = _mm_min_epu8(level[0], clip1);
  level[1] = _mm_min_epu8(level[1], clip1);
  level[2] = _mm_min_epu8(level[2], const_3);
  level[3] = _mm_min_epu8(level[3], const_3);
  level[4] = _mm_min_epu8(level[4], const_3);
  count = _mm_add_epi8(count, level[1]);
  count = _mm_add_epi8(count, level[2]);
  count = _mm_add_epi8(count, level[3]);
  count = _mm_add_epi8(count, level[4]);
  count = _mm_avg_epu8(count, _mm_setzero_si128());
  count = _mm_min_epu8(count, clip2);
  return count;
}

static INLINE void get_4_nz_map_contexts_hor(const uint8_t *levels,
                                             const int height,
                                             const ptrdiff_t *const offsets,
                                             int8_t *coeff_contexts) {
  const int stride = 4 + TX_PAD_HOR;
  const __m128i pos_to_clip1 =
      _mm_setr_epi8(5, 5, 3, 3, 5, 5, 3, 3, 5, 5, 3, 3, 5, 5, 3, 3);
  const __m128i pos_to_clip2 =
      _mm_setr_epi8(6, 4, 4, 4, 6, 4, 4, 4, 6, 4, 4, 4, 6, 4, 4, 4);
  const __m128i pos_to_offset = _mm_setr_epi8(
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15);

  __m128i count;
  __m128i level[5];
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x5_sse2(levels, stride, offsets, level);
    count =
        get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1, pos_to_clip2);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    levels += 4 * stride;
    coeff_contexts += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_hor(const uint8_t *levels,
                                            const int height,
                                            const ptrdiff_t *const offsets,
                                            int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_HOR;
  const __m128i pos_to_clip1 =
      _mm_setr_epi8(5, 5, 3, 3, 3, 3, 3, 3, 5, 5, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip2 =
      _mm_setr_epi8(6, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4);
  const __m128i pos_to_offset =
      _mm_setr_epi8(LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15,
                    15, 15, 15, 15, 15, LF_SIG_COEF_CONTEXTS_2D,
                    LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15, 15, 15, 15, 15);

  int row = height;
  __m128i count;
  __m128i level[5];

  assert(!(height % 2));

  do {
    load_levels_8x2x5_sse2(levels, stride, offsets, level);
    count =
        get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1, pos_to_clip2);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    levels += 2 * stride;
    coeff_contexts += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_hor(const uint8_t *levels,
                                              const int width, const int height,
                                              const ptrdiff_t *const offsets,
                                              int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_HOR;
  const __m128i pos_to_offset_large = _mm_setr_epi8(
      15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15);
  const __m128i pos_to_clip1_large =
      _mm_setr_epi8(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip2_large =
      _mm_setr_epi8(4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
  __m128i count;
  __m128i level[5];
  int row = height;

  assert(!(width % 16));

  do {
    __m128i pos_to_offset =
        _mm_setr_epi8(LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7, 15,
                      15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15);
    __m128i pos_to_clip1 =
        _mm_setr_epi8(5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
    __m128i pos_to_clip2 =
        _mm_setr_epi8(6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
    int w = width;

    do {
      load_levels_16x1x5_sse2(levels, stride, offsets, level);
      count =
          get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1, pos_to_clip2);
      count = _mm_add_epi8(count, pos_to_offset);
      _mm_store_si128((__m128i *)coeff_contexts, count);
      pos_to_offset = pos_to_offset_large;
      pos_to_clip1 = pos_to_clip1_large;
      pos_to_clip2 = pos_to_clip2_large;
      levels += 16;
      coeff_contexts += 16;
      w -= 16;
    } while (w);

    levels += TX_PAD_HOR;
  } while (--row);
}

static INLINE void get_4_nz_map_contexts_ver(const uint8_t *levels,
                                             const int height,
                                             const ptrdiff_t *const offsets,
                                             int8_t *coeff_contexts) {
  const int stride = 4 + TX_PAD_HOR;
  __m128i pos_to_clip1 =
      _mm_setr_epi8(5, 5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip1_large = _mm_set1_epi8(3);
  __m128i pos_to_clip2 =
      _mm_setr_epi8(6, 6, 6, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4);
  const __m128i pos_to_clip2_large = _mm_set1_epi8(4);
  __m128i pos_to_offset = _mm_setr_epi8(
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D + 7,
      LF_SIG_COEF_CONTEXTS_2D + 7, LF_SIG_COEF_CONTEXTS_2D + 7,
      LF_SIG_COEF_CONTEXTS_2D + 7, 15, 15, 15, 15, 15, 15, 15, 15);
  const __m128i pos_to_offset_large = _mm_set1_epi8(15);

  __m128i count;
  __m128i level[5];
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x5_sse2(levels, stride, offsets, level);
    count =
        get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1, pos_to_clip2);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    pos_to_clip1 = pos_to_clip1_large;
    pos_to_clip2 = pos_to_clip2_large;
    pos_to_offset = pos_to_offset_large;
    levels += 4 * stride;
    coeff_contexts += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_ver(const uint8_t *levels,
                                            const int height,
                                            const ptrdiff_t *const offsets,
                                            int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_HOR;
  __m128i pos_to_clip1 = _mm_set1_epi8(5);
  const __m128i pos_to_clip1_large = _mm_set1_epi8(3);
  __m128i pos_to_clip2 =
      _mm_setr_epi8(6, 6, 6, 6, 6, 6, 6, 6, 4, 4, 4, 4, 4, 4, 4, 4);
  const __m128i pos_to_clip2_large = _mm_set1_epi8(4);
  __m128i pos_to_offset = _mm_setr_epi8(
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D,
      LF_SIG_COEF_CONTEXTS_2D, LF_SIG_COEF_CONTEXTS_2D,
      LF_SIG_COEF_CONTEXTS_2D + 7, LF_SIG_COEF_CONTEXTS_2D + 7,
      LF_SIG_COEF_CONTEXTS_2D + 7, LF_SIG_COEF_CONTEXTS_2D + 7,
      LF_SIG_COEF_CONTEXTS_2D + 7, LF_SIG_COEF_CONTEXTS_2D + 7,
      LF_SIG_COEF_CONTEXTS_2D + 7, LF_SIG_COEF_CONTEXTS_2D + 7);
  const __m128i pos_to_offset_large = _mm_set1_epi8(15);
  int row = height;
  __m128i count;
  __m128i level[5];

  assert(!(height % 2));

  do {
    load_levels_8x2x5_sse2(levels, stride, offsets, level);
    count =
        get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1, pos_to_clip2);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    pos_to_clip1 = pos_to_clip1_large;
    pos_to_clip2 = pos_to_clip2_large;
    pos_to_offset = pos_to_offset_large;
    levels += 2 * stride;
    coeff_contexts += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_ver(const uint8_t *levels,
                                              const int width, const int height,
                                              const ptrdiff_t *const offsets,
                                              int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_HOR;
  __m128i pos_to_offset[3];
  __m128i pos_to_clip1[3];
  __m128i pos_to_clip2[3];
  __m128i count;
  __m128i level[5];
  int row = height;

  assert(!(width % 16));

  pos_to_offset[0] = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D);
  pos_to_offset[1] = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D + 7);
  pos_to_offset[2] = _mm_set1_epi8(15);
  pos_to_clip1[0] = _mm_set1_epi8(5);
  pos_to_clip1[1] = _mm_set1_epi8(5);
  pos_to_clip1[2] = _mm_set1_epi8(3);

  pos_to_clip2[0] = _mm_set1_epi8(6);
  pos_to_clip2[1] = _mm_set1_epi8(4);

  do {
    int w = width;

    do {
      load_levels_16x1x5_sse2(levels, stride, offsets, level);
      count = get_coeff_contexts_1d_kernel_sse2(level, pos_to_clip1[0],
                                                pos_to_clip2[0]);
      count = _mm_add_epi8(count, pos_to_offset[0]);
      _mm_store_si128((__m128i *)coeff_contexts, count);
      levels += 16;
      coeff_contexts += 16;
      w -= 16;
    } while (w);

    pos_to_offset[0] = pos_to_offset[1];
    pos_to_offset[1] = pos_to_offset[2];

    pos_to_clip1[0] = pos_to_clip1[1];
    pos_to_clip1[1] = pos_to_clip1[2];
    pos_to_clip2[0] = pos_to_clip2[1];
    levels += TX_PAD_HOR;
  } while (--row);
}

static INLINE __m128i get_coeff_contexts_2d_kernel_chroma_sse2(
    __m128i *const level, const __m128i clip) {
  const __m128i const_3 = _mm_set1_epi8(3);
  __m128i count;

  count = _mm_min_epu8(level[0], clip);
  level[1] = _mm_min_epu8(level[1], clip);
  level[2] = _mm_min_epu8(level[2], clip);
  count = _mm_add_epi8(count, level[1]);
  count = _mm_add_epi8(count, level[2]);
  count = _mm_avg_epu8(count, _mm_setzero_si128());
  count = _mm_min_epu8(count, const_3);
  return count;
}

static INLINE void get_4_nz_map_contexts_2d_chroma(const uint8_t *levels,
                                                   const int height,
                                                   int8_t *const coeff_contexts,
                                                   const int plane) {
  const int stride = 4 + TX_PAD_HOR;
  const __m128i pos_to_offset =
      (plane == AOM_PLANE_U) ? _mm_set1_epi8(0) : _mm_set1_epi8(4);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);
  __m128i count;
  __m128i level[3];
  int8_t *cc = coeff_contexts;
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x3_sse2(levels, stride, level);
    count = get_coeff_contexts_2d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)cc, count);
    pos_to_clip = pos_to_clip_large;
    levels += 4 * stride;
    cc += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_2d_chroma(const uint8_t *levels,
                                                  const int height,
                                                  int8_t *coeff_contexts,
                                                  const int plane) {
  const int stride = 8 + TX_PAD_HOR;
  const __m128i pos_to_offset =
      (plane == AOM_PLANE_U) ? _mm_set1_epi8(0) : _mm_set1_epi8(4);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);
  int8_t *cc = coeff_contexts;
  int row = height;
  __m128i count;
  __m128i level[3];

  assert(!(height % 2));

  do {
    load_levels_8x2x3_sse2(levels, stride, level);
    count = get_coeff_contexts_2d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)cc, count);
    pos_to_clip = pos_to_clip_large;
    levels += 2 * stride;
    cc += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_2d_chroma(const uint8_t *levels,
                                                    const int width,
                                                    const int height,
                                                    int8_t *coeff_contexts,
                                                    const int plane) {
  const int stride = width + TX_PAD_HOR;
  const __m128i pos_to_offset =
      (plane == AOM_PLANE_U) ? _mm_set1_epi8(0) : _mm_set1_epi8(4);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);
  int8_t *cc = coeff_contexts;
  int row = height;
  __m128i count;
  __m128i level[3];

  assert(!(width % 16));

  do {
    int w = width;

    do {
      load_levels_16x1x3_sse2(levels, stride, level);
      count = get_coeff_contexts_2d_kernel_chroma_sse2(level, pos_to_clip);
      count = _mm_add_epi8(count, pos_to_offset);
      _mm_store_si128((__m128i *)cc, count);
      pos_to_clip = pos_to_clip_large;
      levels += 16;
      cc += 16;
      w -= 16;
    } while (w);
    levels += TX_PAD_HOR;
  } while (--row);
}

static INLINE __m128i
get_coeff_contexts_1d_kernel_chroma_sse2(__m128i *const level, __m128i clip1) {
  const __m128i const_3 = _mm_set1_epi8(3);
  __m128i count;

  count = _mm_min_epu8(level[0], clip1);
  level[1] = _mm_min_epu8(level[1], clip1);
  count = _mm_add_epi8(count, level[1]);
  count = _mm_avg_epu8(count, _mm_setzero_si128());
  count = _mm_min_epu8(count, const_3);
  return count;
}

static INLINE void get_4_nz_map_contexts_hor_chroma(const uint8_t *levels,
                                                    const int height,
                                                    int8_t *coeff_contexts) {
  const int stride = 4 + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  const __m128i pos_to_clip =
      _mm_setr_epi8(5, 3, 3, 3, 5, 3, 3, 3, 5, 3, 3, 3, 5, 3, 3, 3);

  __m128i count;
  __m128i level[2];
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x2_sse2(levels, stride, level);
    count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    levels += 4 * stride;
    coeff_contexts += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_hor_chroma(const uint8_t *levels,
                                                   const int height,
                                                   int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  const __m128i pos_to_clip =
      _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3);

  __m128i count;
  __m128i level[2];
  int row = height;

  assert(!(height % 2));

  do {
    load_levels_8x2x2_sse2(levels, stride, level);
    count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    levels += 2 * stride;
    coeff_contexts += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_hor_chroma(const uint8_t *levels,
                                                     const int width,
                                                     const int height,
                                                     int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  const __m128i pos_to_clip_large =
      _mm_setr_epi8(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);

  __m128i count;
  __m128i level[2];
  int row = height;

  assert(!(width % 16));

  do {
    __m128i pos_to_clip =
        _mm_setr_epi8(5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
    int w = width;

    do {
      load_levels_16x1x2_sse2(levels, stride, level);
      count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
      count = _mm_add_epi8(count, pos_to_offset);
      _mm_store_si128((__m128i *)coeff_contexts, count);
      pos_to_clip = pos_to_clip_large;
      levels += 16;
      coeff_contexts += 16;
      w -= 16;
    } while (w);

    levels += TX_PAD_HOR;
  } while (--row);
}

static INLINE void get_4_nz_map_contexts_ver_chroma(const uint8_t *levels,
                                                    const int height,
                                                    int8_t *coeff_contexts) {
  const int stride = 4 + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);

  __m128i count;
  __m128i level[2];
  int row = height;

  assert(!(height % 4));

  do {
    load_levels_4x4x2_sse2(levels, stride, level);
    count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    pos_to_clip = pos_to_clip_large;
    levels += 4 * stride;
    coeff_contexts += 16;
    row -= 4;
  } while (row);
}

static INLINE void get_8_coeff_contexts_ver_chroma(const uint8_t *levels,
                                                   const int height,
                                                   int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);

  int row = height;
  __m128i count;
  __m128i level[2];

  assert(!(height % 2));

  do {
    load_levels_8x2x2_sse2(levels, stride, level);
    count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
    count = _mm_add_epi8(count, pos_to_offset);
    _mm_store_si128((__m128i *)coeff_contexts, count);
    pos_to_clip = pos_to_clip_large;
    levels += 2 * stride;
    coeff_contexts += 16;
    row -= 2;
  } while (row);
}

static INLINE void get_16n_coeff_contexts_ver_chroma(const uint8_t *levels,
                                                     const int width,
                                                     const int height,
                                                     int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_HOR;
  const __m128i pos_to_offset = _mm_set1_epi8(LF_SIG_COEF_CONTEXTS_2D_UV);
  __m128i pos_to_clip =
      _mm_setr_epi8(5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5);
  const __m128i pos_to_clip_large = _mm_set1_epi8(3);
  __m128i count;
  __m128i level[5];
  int row = height;

  assert(!(width % 16));

  do {
    int w = width;

    do {
      load_levels_16x1x2_sse2(levels, stride, level);
      count = get_coeff_contexts_1d_kernel_chroma_sse2(level, pos_to_clip);
      count = _mm_add_epi8(count, pos_to_offset);
      _mm_store_si128((__m128i *)coeff_contexts, count);
      levels += 16;
      coeff_contexts += 16;
      w -= 16;
    } while (w);

    pos_to_clip = pos_to_clip_large;
    levels += TX_PAD_HOR;
  } while (--row);
}

static INLINE void load_levels_4x4x5_skip_sse2(const uint8_t *const src,
                                               const int stride,
                                               __m128i *const level) {
  level[0] = load_8bit_4x4_to_1_reg_sse2(src - 1, stride);
  level[1] = load_8bit_4x4_to_1_reg_sse2(src - stride, stride);
}

static INLINE void load_levels_8x2x5_skip_sse2(const uint8_t *const src,
                                               const int stride,
                                               __m128i *const level) {
  level[0] = load_8bit_8x2_to_1_reg_sse2(src - 1, stride);
  level[1] = load_8bit_8x2_to_1_reg_sse2(src - stride, stride);
}

static INLINE void load_levels_16x1x5_skip_sse2(const uint8_t *const src,
                                                const int stride,
                                                __m128i *const level) {
  level[0] = _mm_loadu_si128((__m128i *)(src - 1));
  level[1] = _mm_loadu_si128((__m128i *)(src - stride));
}

static INLINE __m128i
get_coeff_contexts_kernel_skip_sse2(__m128i *const level) {
  const __m128i const_3 = _mm_set1_epi8(3);
  const __m128i const_6 = _mm_set1_epi8(6);
  __m128i count;
  count = _mm_min_epu8(level[0], const_3);
  level[1] = _mm_min_epu8(level[1], const_3);
  count = _mm_add_epi8(count, level[1]);
  count = _mm_min_epu8(count, const_6);
  return count;
}

static INLINE void get_4_nz_map_contexts_2d_skip_sse2(
    const uint8_t *levels, const int height, int8_t *const coeff_contexts) {
  const int stride = 4 + TX_PAD_LEFT;
  __m128i offset =
      _mm_setr_epi8(0, 0, 7, 7, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7);
  __m128i count;
  __m128i level[2];
  int8_t *cc = coeff_contexts;
  int row = 0;
  do {
    load_levels_4x4x5_skip_sse2(levels + TX_PAD_LEFT, stride, level);
    count = get_coeff_contexts_kernel_skip_sse2(level);
    count = _mm_add_epi8(count, offset);
    _mm_store_si128((__m128i *)cc, count);
    offset = _mm_set1_epi8(7);
    levels += 4 * stride;
    cc += 16;
    row += 4;
  } while (row < height);
}

static INLINE void get_8_coeff_contexts_2d_skip_sse2(const uint8_t *levels,
                                                     const int height,
                                                     int8_t *coeff_contexts) {
  const int stride = 8 + TX_PAD_LEFT;
  __m128i offset =
      _mm_setr_epi8(0, 0, 7, 7, 7, 7, 7, 7, 0, 0, 7, 7, 7, 7, 7, 7);
  __m128i count;
  __m128i level[2];
  int8_t *cc = coeff_contexts;
  int row = 0;
  do {
    load_levels_8x2x5_skip_sse2(levels + TX_PAD_LEFT, stride, level);
    count = get_coeff_contexts_kernel_skip_sse2(level);
    count = _mm_add_epi8(count, offset);
    _mm_store_si128((__m128i *)cc, count);
    offset = _mm_set1_epi8(7);
    levels += 2 * stride;
    cc += 16;
    row += 2;
  } while (row < height);
}

static INLINE void get_16n_coeff_contexts_2d_skip_sse2(const uint8_t *levels,
                                                       const int width,
                                                       const int height,
                                                       int8_t *coeff_contexts) {
  const int stride = width + TX_PAD_LEFT;
  int8_t *cc = coeff_contexts;
  __m128i pos_to_offset[2];
  __m128i pos_to_offset_large;
  pos_to_offset_large = _mm_set1_epi8(7);
  pos_to_offset[0] =
      _mm_setr_epi8(0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7);
  pos_to_offset[1] = pos_to_offset[0];
  __m128i count;
  __m128i level[2];
  int row = 0;
  do {
    int w = 0;
    do {
      load_levels_16x1x5_skip_sse2(levels + TX_PAD_LEFT, stride, level);
      count = get_coeff_contexts_kernel_skip_sse2(level);
      count = _mm_add_epi8(count, pos_to_offset[0]);
      _mm_store_si128((__m128i *)cc, count);
      levels += 16;
      cc += 16;
      w += 16;
      pos_to_offset[0] = pos_to_offset_large;
    } while (w < width);
    levels += TX_PAD_HOR;
    row++;
    pos_to_offset[0] = pos_to_offset[1];
    pos_to_offset[1] = pos_to_offset_large;
  } while (row < height);
}

// Note: levels[] must be in the range [0, 127], inclusive.
void av1_get_nz_map_contexts_skip_sse2(const uint8_t *const levels,
                                       const int16_t *const scan,
                                       const uint16_t bob, const uint16_t eob,
                                       const TX_SIZE tx_size,
                                       int8_t *const coeff_contexts) {
  (void)scan;
  (void)eob;
  (void)bob;
  const int width = get_txb_wide(tx_size);
  const int height = get_txb_high(tx_size);
  // coeff_contexts must be 16 byte aligned.
  assert(!((intptr_t)coeff_contexts & 0xf));
  if (width == 4) {
    get_4_nz_map_contexts_2d_skip_sse2(levels, height, coeff_contexts);
  } else if (width == 8) {
    get_8_coeff_contexts_2d_skip_sse2(levels, height, coeff_contexts);
  } else {
    get_16n_coeff_contexts_2d_skip_sse2(levels, width, height, coeff_contexts);
  }
}

// Note: levels[] must be in the range [0, 127], inclusive.
void av1_get_nz_map_contexts_sse2(const uint8_t *const levels,
                                  const int16_t *const scan, const uint16_t eob,
                                  const TX_SIZE tx_size,
                                  const TX_CLASS tx_class,
                                  int8_t *const coeff_contexts,
                                  const int plane) {
  const int last_idx = eob - 1;
  if (!last_idx) {
    coeff_contexts[0] = 0;
    return;
  }
  const int width = get_txb_wide(tx_size);
  const int height = get_txb_high(tx_size);
  const int stride = width + TX_PAD_HOR;
  /* coeff_contexts must be 16 byte aligned. */
  assert(!((intptr_t)coeff_contexts & 0xf));

  if (plane == 0) {
    ptrdiff_t offsets[3];

    if (tx_class == TX_CLASS_2D) {
      offsets[0] = 0 * stride + 2;
      offsets[1] = 1 * stride + 1;
      offsets[2] = 2 * stride + 0;

      if (width == 4) {
        get_4_nz_map_contexts_2d(levels, height, offsets, coeff_contexts);
      } else if (width == 8) {
        get_8_coeff_contexts_2d(levels, height, offsets, coeff_contexts);
      } else {
        get_16n_coeff_contexts_2d(levels, width, height, offsets,
                                  coeff_contexts);
      }
    } else if (tx_class == TX_CLASS_HORIZ) {
      offsets[0] = 2;
      offsets[1] = 3;
      offsets[2] = 4;
      if (width == 4) {
        get_4_nz_map_contexts_hor(levels, height, offsets, coeff_contexts);
      } else if (width == 8) {
        get_8_coeff_contexts_hor(levels, height, offsets, coeff_contexts);
      } else {
        get_16n_coeff_contexts_hor(levels, width, height, offsets,
                                   coeff_contexts);
      }
    } else {  // TX_CLASS_VERT
      offsets[0] = 2 * stride;
      offsets[1] = 3 * stride;
      offsets[2] = 4 * stride;
      if (width == 4) {
        get_4_nz_map_contexts_ver(levels, height, offsets, coeff_contexts);
      } else if (width == 8) {
        get_8_coeff_contexts_ver(levels, height, offsets, coeff_contexts);
      } else {
        get_16n_coeff_contexts_ver(levels, width, height, offsets,
                                   coeff_contexts);
      }
    }
  } else {
    if (tx_class == TX_CLASS_2D) {
      if (width == 4) {
        get_4_nz_map_contexts_2d_chroma(levels, height, coeff_contexts, plane);
      } else if (width == 8) {
        get_8_coeff_contexts_2d_chroma(levels, height, coeff_contexts, plane);
      } else {
        get_16n_coeff_contexts_2d_chroma(levels, width, height, coeff_contexts,
                                         plane);
      }
    } else if (tx_class == TX_CLASS_HORIZ) {
      if (width == 4) {
        get_4_nz_map_contexts_hor_chroma(levels, height, coeff_contexts);
      } else if (width == 8) {
        get_8_coeff_contexts_hor_chroma(levels, height, coeff_contexts);
      } else {
        get_16n_coeff_contexts_hor_chroma(levels, width, height,
                                          coeff_contexts);
      }
    } else {  // TX_CLASS_VERT
      if (width == 4) {
        get_4_nz_map_contexts_ver_chroma(levels, height, coeff_contexts);
      } else if (width == 8) {
        get_8_coeff_contexts_ver_chroma(levels, height, coeff_contexts);
      } else {
        get_16n_coeff_contexts_ver_chroma(levels, width, height,
                                          coeff_contexts);
      }
    }
  }
  const int bwl = get_txb_bwl(tx_size);
  const int pos = scan[last_idx];
  if (last_idx <= (height << bwl) / 8)
    coeff_contexts[pos] = 1;
  else if (last_idx <= (height << bwl) / 4)
    coeff_contexts[pos] = 2;
  else
    coeff_contexts[pos] = 3;
}
