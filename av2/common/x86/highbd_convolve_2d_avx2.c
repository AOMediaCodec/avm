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

#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/x86/convolve_avx2.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"
#include "av2/common/convolve.h"

// One pass writes AVX2_NARROW_COLS. The wide path loads two of those strips.
// A block narrower than AVX2_WIDE_COLS uses the narrow function.
#define AVX2_NARROW_COLS 8
#define AVX2_WIDE_COLS 16

// Round two vertical filter sums, pack to 16-bit, clip, and store two output
// rows. Each input holds 4 pixels of row 0 in the low half and 4 pixels of
// row 1 in the high half. remaining_w > 4 writes 8 pixels per row from both
// inputs. remaining_w == 4 writes 4, and anything smaller writes 2. res_b is
// read only when remaining_w > 4.
static INLINE void highbd_sr_round_pack_clip_store_avx2(
    const __m256i res_a, const __m256i res_b, const __m256i round_const_y,
    const __m128i round_shift_y, const __m256i clip_pixel, const __m256i zero,
    uint16_t *dst, int dst_stride, int remaining_w) {
  __m256i res_a_round =
      _mm256_sra_epi32(_mm256_add_epi32(res_a, round_const_y), round_shift_y);

  if (remaining_w > 4) {
    const __m256i res_b_round =
        _mm256_sra_epi32(_mm256_add_epi32(res_b, round_const_y), round_shift_y);
    __m256i res_16bit = _mm256_packs_epi32(res_a_round, res_b_round);
    res_16bit = _mm256_min_epi16(res_16bit, clip_pixel);
    res_16bit = _mm256_max_epi16(res_16bit, zero);

    _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(res_16bit));
    _mm_storeu_si128((__m128i *)(dst + dst_stride),
                     _mm256_extracti128_si256(res_16bit, 1));
  } else {
    res_a_round = _mm256_packs_epi32(res_a_round, res_a_round);
    res_a_round = _mm256_min_epi16(res_a_round, clip_pixel);
    res_a_round = _mm256_max_epi16(res_a_round, zero);

    if (remaining_w == 4) {
      _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res_a_round));
      _mm_storel_epi64((__m128i *)(dst + dst_stride),
                       _mm256_extracti128_si256(res_a_round, 1));
    } else {
      xx_storel_32((__m128i *)dst, _mm256_castsi256_si128(res_a_round));
      xx_storel_32((__m128i *)(dst + dst_stride),
                   _mm256_extracti128_si256(res_a_round, 1));
    }
  }
}

// Takes the column data already unpacked, so callers can carry s[0]/s[2] from
// the previous row pair and only build s[1]/s[3] each iteration.
static INLINE void highbd_sr_vert_4tap_unpacked_avx2(
    const __m256i *s, const __m256i *coeffs_y, const __m256i round_const_y,
    const __m128i round_shift_y, const __m256i clip_pixel, const __m256i zero,
    uint16_t *dst, int dst_stride, int remaining_w) {
  const __m256i res_a = convolve_4tap(s, coeffs_y);
  const __m256i res_b =
      (remaining_w > 4) ? convolve_4tap(s + 2, coeffs_y) : res_a;
  highbd_sr_round_pack_clip_store_avx2(res_a, res_b, round_const_y,
                                       round_shift_y, clip_pixel, zero, dst,
                                       dst_stride, remaining_w);
}

static INLINE void highbd_sr_vert_2tap_pair_avx2(
    const __m256i row0, const __m256i row1, const __m256i coeffs_y,
    const __m256i round_const_y, const __m128i round_shift_y,
    const __m256i clip_pixel, const __m256i zero, uint16_t *dst, int dst_stride,
    int remaining_w) {
  const __m256i s0 = _mm256_unpacklo_epi16(row0, row1);
  const __m256i s1 = _mm256_unpackhi_epi16(row0, row1);

  const __m256i res_a = _mm256_madd_epi16(s0, coeffs_y);
  const __m256i res_b = _mm256_madd_epi16(s1, coeffs_y);
  highbd_sr_round_pack_clip_store_avx2(res_a, res_b, round_const_y,
                                       round_shift_y, clip_pixel, zero, dst,
                                       dst_stride, remaining_w);
}

#define CONVOLVE_SR_VERT_FILTER_8TAP                                          \
  const __m256i s0 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));              \
  const __m256i s1 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));              \
  const __m256i s2 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 2 * im_stride));              \
  const __m256i s3 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 3 * im_stride));              \
  const __m256i s4 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 4 * im_stride));              \
  const __m256i s5 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 5 * im_stride));              \
                                                                              \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                       \
  s[1] = _mm256_unpacklo_epi16(s2, s3);                                       \
  s[2] = _mm256_unpacklo_epi16(s4, s5);                                       \
                                                                              \
  s[4] = _mm256_unpackhi_epi16(s0, s1);                                       \
  s[5] = _mm256_unpackhi_epi16(s2, s3);                                       \
  s[6] = _mm256_unpackhi_epi16(s4, s5);                                       \
                                                                              \
  for (i = 0; i < h; i += 2) {                                                \
    const int16_t *data = &im_block[i * im_stride];                           \
                                                                              \
    const __m256i s6 = _mm256_loadu_si256((__m256i *)(data + 6 * im_stride)); \
    const __m256i s7 = _mm256_loadu_si256((__m256i *)(data + 7 * im_stride)); \
                                                                              \
    s[3] = _mm256_unpacklo_epi16(s6, s7);                                     \
    s[7] = _mm256_unpackhi_epi16(s6, s7);                                     \
                                                                              \
    const __m256i res_a = convolve(s, coeffs_y);                              \
    const __m256i res_b = (w - j > 4) ? convolve(s + 4, coeffs_y) : res_a;    \
    highbd_sr_round_pack_clip_store_avx2(                                     \
        res_a, res_b, round_const_y, round_shift_y, clip_pixel, zero,         \
        &dst[i * dst_stride + j], dst_stride, w - j);                         \
                                                                              \
    s[0] = s[1];                                                              \
    s[1] = s[2];                                                              \
    s[2] = s[3];                                                              \
                                                                              \
    s[4] = s[5];                                                              \
    s[5] = s[6];                                                              \
    s[6] = s[7];                                                              \
  }

#define CONVOLVE_SR_VERT_FILTER_6TAP                                          \
  const __m256i s0 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));              \
  const __m256i s1 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));              \
  const __m256i s2 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 2 * im_stride));              \
  const __m256i s3 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 3 * im_stride));              \
                                                                              \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                       \
  s[1] = _mm256_unpacklo_epi16(s2, s3);                                       \
                                                                              \
  s[3] = _mm256_unpackhi_epi16(s0, s1);                                       \
  s[4] = _mm256_unpackhi_epi16(s2, s3);                                       \
                                                                              \
  for (i = 0; i < h; i += 2) {                                                \
    const int16_t *data = &im_block[i * im_stride];                           \
                                                                              \
    const __m256i s6 = _mm256_loadu_si256((__m256i *)(data + 4 * im_stride)); \
    const __m256i s7 = _mm256_loadu_si256((__m256i *)(data + 5 * im_stride)); \
                                                                              \
    s[2] = _mm256_unpacklo_epi16(s6, s7);                                     \
    s[5] = _mm256_unpackhi_epi16(s6, s7);                                     \
                                                                              \
    const __m256i res_a = convolve_6tap(s, coeffs_y);                         \
    const __m256i res_b =                                                     \
        (w - j > 4) ? convolve_6tap(s + 3, coeffs_y) : res_a;                 \
    highbd_sr_round_pack_clip_store_avx2(                                     \
        res_a, res_b, round_const_y, round_shift_y, clip_pixel, zero,         \
        &dst[i * dst_stride + j], dst_stride, w - j);                         \
                                                                              \
    s[0] = s[1];                                                              \
    s[1] = s[2];                                                              \
                                                                              \
    s[3] = s[4];                                                              \
    s[4] = s[5];                                                              \
  }

#define CONVOLVE_SR_VERT_FILTER_4TAP                                          \
  const __m256i s0 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 0 * im_stride));              \
  const __m256i s1 =                                                          \
      _mm256_loadu_si256((__m256i *)(im_block + 1 * im_stride));              \
  s[0] = _mm256_unpacklo_epi16(s0, s1);                                       \
  s[2] = _mm256_unpackhi_epi16(s0, s1);                                       \
                                                                              \
  for (i = 0; i < h; i += 2) {                                                \
    const int16_t *data = &im_block[i * im_stride];                           \
                                                                              \
    const __m256i s6 = _mm256_loadu_si256((__m256i *)(data + 2 * im_stride)); \
    const __m256i s7 = _mm256_loadu_si256((__m256i *)(data + 3 * im_stride)); \
                                                                              \
    s[1] = _mm256_unpacklo_epi16(s6, s7);                                     \
    s[3] = _mm256_unpackhi_epi16(s6, s7);                                     \
                                                                              \
    highbd_sr_vert_4tap_unpacked_avx2(                                        \
        s, coeffs_y, round_const_y, round_shift_y, clip_pixel, zero,          \
        &dst[i * dst_stride + j], dst_stride, w - j);                         \
                                                                              \
    s[0] = s[1];                                                              \
    s[2] = s[3];                                                              \
  }

#define CONVOLVE_SR_VERT_FILTER_2TAP                                       \
  for (i = 0; i < h; i += 2) {                                             \
    const int16_t *data = &im_block[i * im_stride];                        \
    const __m256i row0 =                                                   \
        _mm256_loadu_si256((__m256i *)(data + 0 * im_stride));             \
    const __m256i row1 =                                                   \
        _mm256_loadu_si256((__m256i *)(data + 1 * im_stride));             \
    highbd_sr_vert_2tap_pair_avx2(                                         \
        row0, row1, coeffs_y[0], round_const_y, round_shift_y, clip_pixel, \
        zero, &dst[i * dst_stride + j], dst_stride, w - j);                \
  }

#if defined(_MSC_VER)
#define CONVOLVE_NOINLINE __declspec(noinline)
#else
#define CONVOLVE_NOINLINE __attribute__((noinline))
#endif

// Blocks narrower than AVX2_WIDE_COLS. One scratch buffer, kept out of the
// wide function so that function's second buffer is not on the stack.
CONVOLVE_NOINLINE void highbd_convolve_2d_sr_narrow_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const int32_t tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int32_t tap_y = get_filter_tap(filter_params_y, subpel_y_qn);

  const int im_h = h + tap_y - 1;
  const int fo_vert = tap_y / 2 - 1;
  const int fo_horiz = tap_x / 2 - 1;
  const uint16_t *const src_ptr = src - fo_vert * src_stride - fo_horiz;
  int i, j;

  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m256i coeffs_y[4] = { 0 };
  __m256i coeffs_x[4] = { 0 };

  const __m256i round_const_x = _mm256_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const __m256i round_const_y = _mm256_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);

  const __m256i clip_pixel = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i zero = _mm256_setzero_si256();

  if (tap_x == 8)
    prepare_coeffs(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 6)
    prepare_coeffs_6t(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 4)
    prepare_coeffs_4t(filter_params_x, subpel_x_qn, coeffs_x);
  else {
    assert(tap_x == 2);
    coeffs_x[0] = prepare_coeffs_bilinear(filter_params_x, subpel_x_qn);
  }

  if (tap_y == 8)
    prepare_coeffs(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 6)
    prepare_coeffs_6t(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 4)
    prepare_coeffs_4t(filter_params_y, subpel_y_qn, coeffs_y);
  else {
    assert(tap_y == 2);
    coeffs_y[0] = prepare_coeffs_bilinear(filter_params_y, subpel_y_qn);
  }

  DECLARE_ALIGNED(
      32, int16_t,
      im_block_buf[(MAX_SB_SIZE + MAX_FILTER_TAP) * AVX2_NARROW_COLS]);
  // The filter macros read and write through `im_block`.
  int16_t *im_block = im_block_buf;
  const int im_stride = AVX2_NARROW_COLS;
  __m256i s[8];

  for (j = 0; j < w; j += AVX2_NARROW_COLS) {
    if (tap_x == 8) {
      CONVOLVE_HORIZ_FILTER_8TAP
    } else if (tap_x == 6) {
      CONVOLVE_HORIZ_FILTER_6TAP
    } else if (tap_x == 4) {
      CONVOLVE_HORIZ_FILTER_4TAP
    } else {
      assert(tap_x == 2);
      CONVOLVE_HORIZ_FILTER_2TAP
    }

    if (tap_y == 8) {
      CONVOLVE_SR_VERT_FILTER_8TAP
    } else if (tap_y == 6) {
      CONVOLVE_SR_VERT_FILTER_6TAP
    } else if (tap_y == 4) {
      CONVOLVE_SR_VERT_FILTER_4TAP
    } else {
      assert(tap_y == 2);
      CONVOLVE_SR_VERT_FILTER_2TAP
    }
  }
}

void av2_highbd_convolve_2d_sr_specialized_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const int32_t tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int32_t tap_y = get_filter_tap(filter_params_y, subpel_y_qn);

  const int im_h = h + tap_y - 1;
  const int fo_vert = tap_y / 2 - 1;
  const int fo_horiz = tap_x / 2 - 1;
  const uint16_t *const src_ptr = src - fo_vert * src_stride - fo_horiz;
  int i, j;

  // Check that, even with 12-bit input, the intermediate values will fit
  // into an unsigned 16-bit intermediate array.
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m256i coeffs_y[4] = { 0 };
  __m256i coeffs_x[4] = { 0 };

  const __m256i round_const_x = _mm256_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const __m256i round_const_y = _mm256_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);

  const __m256i clip_pixel = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i zero = _mm256_setzero_si256();

  if (tap_x == 8)
    prepare_coeffs(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 6)
    prepare_coeffs_6t(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 4)
    prepare_coeffs_4t(filter_params_x, subpel_x_qn, coeffs_x);
  else {
    assert(tap_x == 2);
    coeffs_x[0] = prepare_coeffs_bilinear(filter_params_x, subpel_x_qn);
  }

  if (tap_y == 8)
    prepare_coeffs(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 6)
    prepare_coeffs_6t(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 4)
    prepare_coeffs_4t(filter_params_y, subpel_y_qn, coeffs_y);
  else {
    assert(tap_y == 2);
    coeffs_y[0] = prepare_coeffs_bilinear(filter_params_y, subpel_y_qn);
  }

  DECLARE_ALIGNED(
      32, int16_t,
      im_block_left[(MAX_SB_SIZE + MAX_FILTER_TAP) * AVX2_NARROW_COLS]);
  // The filter macros read and write through `im_block`.
  int16_t *im_block = im_block_left;
  const int im_stride = AVX2_NARROW_COLS;
  __m256i s[8];

  j = 0;
  if (w >= AVX2_WIDE_COLS) {
    DECLARE_ALIGNED(
        32, int16_t,
        im_block_right[(MAX_SB_SIZE + MAX_FILTER_TAP) * AVX2_NARROW_COLS]);
    for (; j + AVX2_WIDE_COLS <= w; j += AVX2_WIDE_COLS) {
      highbd_convolve_horiz_16w_loadonly_avx2(
          src_ptr, src_stride, im_h, j, im_block_left, im_block_right, tap_x,
          coeffs_x, round_const_x, round_shift_x);

      if (tap_y == 8) {
        CONVOLVE_SR_VERT_FILTER_8TAP
      } else if (tap_y == 6) {
        CONVOLVE_SR_VERT_FILTER_6TAP
      } else if (tap_y == 4) {
        CONVOLVE_SR_VERT_FILTER_4TAP
      } else {
        assert(tap_y == 2);
        CONVOLVE_SR_VERT_FILTER_2TAP
      }

      {
        const int j_left = j;
        j += AVX2_NARROW_COLS;
        im_block = im_block_right;
        if (tap_y == 8) {
          CONVOLVE_SR_VERT_FILTER_8TAP
        } else if (tap_y == 6) {
          CONVOLVE_SR_VERT_FILTER_6TAP
        } else if (tap_y == 4) {
          CONVOLVE_SR_VERT_FILTER_4TAP
        } else {
          assert(tap_y == 2);
          CONVOLVE_SR_VERT_FILTER_2TAP
        }
        j = j_left;
        im_block = im_block_left;
      }
    }
  }

  if (j < w) {
    highbd_convolve_2d_sr_narrow_avx2(
        src + j, src_stride, dst + j, dst_stride, w - j, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
  }
}

#define CONVOLVE_SR_2D_FILTER_4TAP                                           \
  for (j = 0; j < w; j += AVX2_NARROW_COLS) {                                \
    __m256i s[4];                                                            \
    __m256i row[2];                                                          \
    row[0] = highbd_sr_horiz_4tap_pair_avx2(src_ptr, src_stride, 0, j, im_h, \
                                            coeffs_x, round_const_x,         \
                                            round_shift_x);                  \
    row[1] = highbd_sr_horiz_4tap_pair_avx2(src_ptr, src_stride, 2, j, im_h, \
                                            coeffs_x, round_const_x,         \
                                            round_shift_x);                  \
    const __m256i s0 = row[0];                                               \
    const __m256i s1 = _mm256_permute2x128_si256(row[0], row[1], 0x21);      \
    s[0] = _mm256_unpacklo_epi16(s0, s1);                                    \
    s[2] = _mm256_unpackhi_epi16(s0, s1);                                    \
                                                                             \
    for (i = 0; i < h; i += 2) {                                             \
      const __m256i next = highbd_sr_horiz_4tap_pair_avx2(                   \
          src_ptr, src_stride, i + 4, j, im_h, coeffs_x, round_const_x,      \
          round_shift_x);                                                    \
      const __m256i s6 = row[1];                                             \
      const __m256i s7 = _mm256_permute2x128_si256(row[1], next, 0x21);      \
      s[1] = _mm256_unpacklo_epi16(s6, s7);                                  \
      s[3] = _mm256_unpackhi_epi16(s6, s7);                                  \
                                                                             \
      highbd_sr_vert_4tap_unpacked_avx2(                                     \
          s, coeffs_y, round_const_y, round_shift_y, clip_pixel, zero,       \
          &dst[i * dst_stride + j], dst_stride, w - j);                      \
                                                                             \
      s[0] = s[1];                                                           \
      s[2] = s[3];                                                           \
      row[1] = next;                                                         \
    }                                                                        \
  }

CONVOLVE_NOINLINE void highbd_convolve_2d_sr_xy_4tap_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const uint16_t *const src_ptr = src - src_stride - 1;
  const int im_h = h + 3;
  int i, j;

  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m256i coeffs_x[4] = { 0 };
  __m256i coeffs_y[4] = { 0 };
  prepare_coeffs_4t(filter_params_x, subpel_x_qn, coeffs_x);
  prepare_coeffs_4t(filter_params_y, subpel_y_qn, coeffs_y);

  const __m256i round_const_x = _mm256_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const __m256i round_const_y = _mm256_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);

  const __m256i clip_pixel = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i zero = _mm256_setzero_si256();

  CONVOLVE_SR_2D_FILTER_4TAP
}

void av2_highbd_convolve_2d_sr_bilinear_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  if (h % 2 != 0 || w < 4) {
    if (w < AVX2_WIDE_COLS) {
      highbd_convolve_2d_sr_narrow_avx2(
          src, src_stride, dst, dst_stride, w, h, filter_params_x,
          filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    } else {
      av2_highbd_convolve_2d_sr_specialized_avx2(
          src, src_stride, dst, dst_stride, w, h, filter_params_x,
          filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    }
    return;
  }

  // Ensure the intermediate values fit in unsigned 16-bit array for
  // bitdepth 12.
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);
  assert((FILTER_BITS * 2 - conv_params->round_0 - conv_params->round_1) == 0);

  const int im_h = h + 2 - 1;

  const __m256i round_const_x = _mm256_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  const __m256i round_const_y = _mm256_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);

  const __m256i clip_pixel = _mm256_set1_epi16((1 << bd) - 1);
  const __m256i zero = _mm256_setzero_si256();

  const __m256i coeffs_x_bilinear =
      prepare_coeffs_bilinear(filter_params_x, subpel_x_qn);
  const __m256i coeffs_y_bilinear =
      prepare_coeffs_bilinear(filter_params_y, subpel_y_qn);

  const int8_t reorder_pixels[32] = { 0,  1,  8, 9,  2,  3,  10, 11, 4,  5, 12,
                                      13, 6,  7, 14, 15, 0,  1,  8,  9,  2, 3,
                                      10, 11, 4, 5,  12, 13, 6,  7,  14, 15 };
  const __m256i shuffle_mask0 = _mm256_loadu_si256((__m256i *)reorder_pixels);
  int remain_wd = w;

  for (int j = 0; (j < w) && (remain_wd >= AVX2_NARROW_COLS);
       j += AVX2_NARROW_COLS) {
    __m256i row[2];
    /* Horizontal filter */
    const __m256i a = _mm256_loadu_si256((__m256i *)&src[0 * src_stride + j]);
    const __m256i b = _mm256_loadu_si256((__m256i *)&src[1 * src_stride + j]);
    __m256i rw0 = _mm256_permute2x128_si256(a, b, 0x20);
    __m256i rw1 = _mm256_permute2x128_si256(a, b, 0x31);

    rw1 = _mm256_alignr_epi8(rw1, rw0, 2);
    __m256i res0 = _mm256_madd_epi16(rw0, coeffs_x_bilinear);
    __m256i res1 = _mm256_madd_epi16(rw1, coeffs_x_bilinear);
    res0 =
        _mm256_sra_epi32(_mm256_add_epi32(res0, round_const_x), round_shift_x);
    res1 =
        _mm256_sra_epi32(_mm256_add_epi32(res1, round_const_x), round_shift_x);
    // a0 a2 a4 a6 a1 a3 a5 a7 | b0 b2 b4 b6 b1 b3 b5 b7
    row[0] = _mm256_packs_epi32(res0, res1);

    for (int i = 2; i < im_h; i += 2) {
      const __m256i row0 =
          _mm256_loadu_si256((__m256i *)&src[i * src_stride + j]);
      __m256i row1 = _mm256_setzero_si256();
      if (i + 1 < im_h)
        row1 = _mm256_loadu_si256((__m256i *)&src[(i + 1) * src_stride + j]);
      const __m256i r0 = _mm256_permute2x128_si256(row0, row1, 0x20);
      __m256i r1 = _mm256_permute2x128_si256(row0, row1, 0x31);

      r1 = _mm256_alignr_epi8(r1, r0, 2);
      __m256i res_0 = _mm256_madd_epi16(r0, coeffs_x_bilinear);
      __m256i res_1 = _mm256_madd_epi16(r1, coeffs_x_bilinear);
      res_0 = _mm256_sra_epi32(_mm256_add_epi32(res_0, round_const_x),
                               round_shift_x);
      res_1 = _mm256_sra_epi32(_mm256_add_epi32(res_1, round_const_x),
                               round_shift_x);
      // c0 c2 c4 c6 c1 c3 c5 c7 | d0 d2 d4 d6 d1 d3 d5 d7
      row[1] = _mm256_packs_epi32(res_0, res_1);

      /* Vertical filter */
      // b0 b2 b4 b6 b1 b3 b5 b7 | c0 c2 c4 c6 c1 c3 c5 c7
      const __m256i row2 = _mm256_permute2x128_si256(row[0], row[1], 0x21);
      // a0 b0 a2 b2 a4 b4 a6 b6 | b0 c0 b2 c2 b4 c4 b6 c6
      const __m256i odd = _mm256_unpacklo_epi16(row[0], row2);
      // a1 b1 a3 b3 a5 b5 a7 b7 | b1 c1 b3 c3 b5 c5 b7 c7
      const __m256i even = _mm256_unpackhi_epi16(row[0], row2);

      const __m256i res_0_v = _mm256_madd_epi16(odd, coeffs_y_bilinear);
      const __m256i res_1_v = _mm256_madd_epi16(even, coeffs_y_bilinear);

      const __m256i res_a_round = _mm256_sra_epi32(
          _mm256_add_epi32(res_0_v, round_const_y), round_shift_y);
      const __m256i res_b_round = _mm256_sra_epi32(
          _mm256_add_epi32(res_1_v, round_const_y), round_shift_y);

      // a0 a2 a4 a6 a1 a3 a5 a7 | b0 b2 b4 b6 b1 b3 b5 b7
      __m256i res_16bit = _mm256_packs_epi32(res_a_round, res_b_round);
      // a0 ... a7 | b0 ... b7
      res_16bit = _mm256_shuffle_epi8(res_16bit, shuffle_mask0);
      res_16bit = _mm256_min_epi16(res_16bit, clip_pixel);
      res_16bit = _mm256_max_epi16(res_16bit, zero);
      _mm_storeu_si128((__m128i *)&dst[(i - 2) * dst_stride + j],
                       _mm256_castsi256_si128(res_16bit));
      _mm_storeu_si128((__m128i *)&dst[(i - 1) * dst_stride + j],
                       _mm256_extracti128_si256(res_16bit, 1));

      row[0] = row[1];
    }
    remain_wd -= AVX2_NARROW_COLS;
  }

  if (remain_wd == 0) return;

  if (remain_wd == 4) {
    /* Horizontal filter */
    __m256i row[2];
    const __m128i a =
        _mm_loadu_si128((__m128i *)&src[0 * src_stride + (w - remain_wd)]);
    // b0 b1 ... b7
    const __m128i b =
        _mm_loadu_si128((__m128i *)&src[1 * src_stride + (w - remain_wd)]);
    // a0 ... a7 b0 ... b7
    __m256i read = _mm256_insertf128_si256(_mm256_castsi128_si256(a), b, 0x1);
    const int8_t reorder_pixels_wd4[32] = { 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6,
                                            7, 6, 7, 8, 9, 0, 1, 2, 3, 2, 3,
                                            4, 5, 4, 5, 6, 7, 6, 7, 8, 9 };
    const __m256i shuffle_mask1 =
        _mm256_loadu_si256((__m256i *)reorder_pixels_wd4);
    // a0 a1 a1 a2 a2 a3 a3 a4 b0 b1 b1 b2 b2 b3 b3 b4
    read = _mm256_shuffle_epi8(read, shuffle_mask1);

    // a0 a1 a2 a3 | b0 b1 b2 b3
    __m256i res0 = _mm256_madd_epi16(read, coeffs_x_bilinear);
    res0 =
        _mm256_sra_epi32(_mm256_add_epi32(res0, round_const_x), round_shift_x);
    // a0 a1 a2 a3 x x x x | b0 b1 b2 b3 x x x x
    row[0] = _mm256_packs_epi32(res0, res0);

    for (int i = 2; i < im_h; i += 2) {
      // c0 ... c7
      const __m128i row0 =
          _mm_loadu_si128((__m128i *)&src[i * src_stride + (w - remain_wd)]);
      // d0 ... d7
      __m128i row1 = _mm_setzero_si128();
      if (i + 1 < im_h)
        row1 = _mm_loadu_si128(
            (__m128i *)&src[(i + 1) * src_stride + (w - remain_wd)]);

      // c0 ... c7 d0 ... d7
      __m256i read1 =
          _mm256_insertf128_si256(_mm256_castsi128_si256(row0), row1, 0x1);
      read1 = _mm256_shuffle_epi8(read1, shuffle_mask1);

      __m256i res = _mm256_madd_epi16(read1, coeffs_x_bilinear);
      res =
          _mm256_sra_epi32(_mm256_add_epi32(res, round_const_x), round_shift_x);
      // c0 c1 c2 c3 x x x x | d0 d1 d2 d3 x x x x
      row[1] = _mm256_packs_epi32(res, res);

      /* Vertical filter */
      const __m256i s = _mm256_unpacklo_epi16(
          row[0], _mm256_permute2x128_si256(row[0], row[1], 0x21));

      const __m256i res_0 = _mm256_madd_epi16(s, coeffs_y_bilinear);
      // a0 a1 a2 a3 | b0 b1 b2 b3
      const __m256i res_a_round = _mm256_sra_epi32(
          _mm256_add_epi32(res_0, round_const_y), round_shift_y);

      // a0 a1 a2 a3 x x x x | b0 b1 b2 b3 x x x x
      __m256i res_16bit = _mm256_packs_epi32(res_a_round, res_a_round);
      res_16bit = _mm256_min_epi16(res_16bit, clip_pixel);
      res_16bit = _mm256_max_epi16(res_16bit, zero);
      _mm_storel_epi64((__m128i *)&dst[(i - 2) * dst_stride + (w - remain_wd)],
                       _mm256_castsi256_si128(res_16bit));
      _mm_storel_epi64((__m128i *)&dst[(i - 1) * dst_stride + (w - remain_wd)],
                       _mm256_extracti128_si256(res_16bit, 1));
      row[0] = row[1];
    }
  } else {
    // This code should not be invoked as width is multiple of 8 or 4.
    assert(0);
  }
}

void av2_highbd_convolve_2d_sr_avx2(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_x_qn,
                                    const int subpel_y_qn,
                                    ConvolveParams *conv_params, int bd) {
  // Both axes bilinear is the only production 2-tap x 2-tap case, and it
  // stays here. A fused xy 2-tap kernel was slower at subpel 8,8.
  if ((filter_params_x->interp_filter == BILINEAR) &&
      (filter_params_y->interp_filter == BILINEAR)) {
    av2_highbd_convolve_2d_sr_bilinear_avx2(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    return;
  }

  if ((h & 1) == 0) {
    const int32_t tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
    const int32_t tap_y = get_filter_tap(filter_params_y, subpel_y_qn);
    if (tap_x == 4 && tap_y == 4 && h <= 8) {
      highbd_convolve_2d_sr_xy_4tap_avx2(
          src, src_stride, dst, dst_stride, w, h, filter_params_x,
          filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
      return;
    }
  }

  if (w < AVX2_WIDE_COLS) {
    highbd_convolve_2d_sr_narrow_avx2(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    return;
  }

  av2_highbd_convolve_2d_sr_specialized_avx2(
      src, src_stride, dst, dst_stride, w, h, filter_params_x, filter_params_y,
      subpel_x_qn, subpel_y_qn, conv_params, bd);
}
