/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
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

#include "config/av2_rtcd.h"

#include "avm_dsp/x86/convolve_avx512.h"
#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/convolve.h"
#include "av2/common/filter.h"

// One pass writes AVX512_NARROW_COLS. The wide path loads two of those strips.
#define AVX512_NARROW_COLS 16
#define AVX512_WIDE_COLS 32

void av2_highbd_convolve_2d_sr_bilinear_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);
void highbd_convolve_2d_sr_xy_4tap_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);
void highbd_convolve_2d_sr_narrow_avx2(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);

// Bit i of `store_mask` set means pixel i of each 16-pixel row is written.
// 0xFFFF writes the whole row; a smaller mask lets blocks narrower than 16
// columns write only their own pixels and leave the neighbours untouched.
#define AVX512_FULL_STORE_MASK ((__mmask16)0xFFFF)

static INLINE void highbd_sr_round_pack_clip_store_avx512(
    const __m512i res_a, const __m512i res_b, uint16_t *dst, int dst_stride,
    const __mmask16 store_mask, const ConvolveParams *restrict conv_params,
    int bd) {
  const __m512i round_const_y = _mm512_set1_epi32(
      ((1 << conv_params->round_1) >> 1) -
      (1 << (bd + 2 * FILTER_BITS - conv_params->round_0 - 1)));
  const __m128i round_shift_y = _mm_cvtsi32_si128(conv_params->round_1);
  const __m512i clip_pixel = _mm512_set1_epi16((1 << bd) - 1);
  const __m512i res_a_round =
      _mm512_sra_epi32(_mm512_add_epi32(res_a, round_const_y), round_shift_y);
  const __m512i res_b_round =
      _mm512_sra_epi32(_mm512_add_epi32(res_b, round_const_y), round_shift_y);
  __m512i res_16bit = _mm512_packs_epi32(res_a_round, res_b_round);
  res_16bit = _mm512_min_epi16(res_16bit, clip_pixel);
  res_16bit = _mm512_max_epi16(res_16bit, _mm512_setzero_si512());

  const __m256i row0 = _mm512_castsi512_si256(res_16bit);
  const __m256i row1 = _mm512_extracti64x4_epi64(res_16bit, 1);
  if (store_mask == AVX512_FULL_STORE_MASK) {
    _mm256_storeu_si256((__m256i *)dst, row0);
    _mm256_storeu_si256((__m256i *)(dst + dst_stride), row1);
  } else {
    _mm256_mask_storeu_epi16(dst, store_mask, row0);
    _mm256_mask_storeu_epi16(dst + dst_stride, store_mask, row1);
  }
}

// Takes the column data already packed, so callers can carry signal[0]/
// signal[2] from the previous row pair and only build signal[1]/signal[3]
static INLINE void highbd_sr_vert_4tap_unpacked_avx512(
    const __m512i *signal, const __m512i *coeffs_y, uint16_t *dst,
    int dst_stride, const __mmask16 store_mask,
    const ConvolveParams *restrict conv_params, int bd) {
  const __m512i res_a = convolve_4tap_avx512(signal, coeffs_y);
  const __m512i res_b = convolve_4tap_avx512(signal + 2, coeffs_y);
  highbd_sr_round_pack_clip_store_avx512(res_a, res_b, dst, dst_stride,
                                         store_mask, conv_params, bd);
}

static INLINE void highbd_sr_vert_2tap_pair_avx512(
    const __m512i row01, const __m512i row12, const __m512i coeffs_y,
    uint16_t *dst, int dst_stride, const __mmask16 store_mask,
    const ConvolveParams *restrict conv_params, int bd) {
  const __m512i s0 = _mm512_unpacklo_epi16(row01, row12);
  const __m512i s1 = _mm512_unpackhi_epi16(row01, row12);

  const __m512i res_a = _mm512_madd_epi16(s0, coeffs_y);
  const __m512i res_b = _mm512_madd_epi16(s1, coeffs_y);
  highbd_sr_round_pack_clip_store_avx512(res_a, res_b, dst, dst_stride,
                                         store_mask, conv_params, bd);
}

#define CONVOLVE_SR_VERT_FILTER_8TAP                                      \
  {                                                                       \
    const uint16_t *const im = (const uint16_t *)im_block;                \
    __m512i signal[9];                                                    \
    pack_16x9_init(im, im_stride, signal);                                \
    for (i = 0; i < h; i += 2) {                                          \
      pack_16x9_pixels(im + i * im_stride, im_stride, signal);            \
      const __m512i res_a = convolve_avx512(signal, coeffs_y);            \
      const __m512i res_b = convolve_avx512(signal + 4, coeffs_y);        \
      highbd_sr_round_pack_clip_store_avx512(                             \
          res_a, res_b, &dst[i * dst_stride + j], dst_stride, store_mask, \
          conv_params, bd);                                               \
      update_pixels_16x9(signal);                                         \
    }                                                                     \
  }

#define CONVOLVE_SR_VERT_FILTER_6TAP                                      \
  {                                                                       \
    const uint16_t *const im = (const uint16_t *)im_block;                \
    __m512i signal[7];                                                    \
    pack_16x7_init(im, im_stride, signal);                                \
    for (i = 0; i < h; i += 2) {                                          \
      pack_16x7_pixels(im + i * im_stride, im_stride, signal);            \
      const __m512i res_a = convolve_6tap_avx512(signal, coeffs_y);       \
      const __m512i res_b = convolve_6tap_avx512(signal + 3, coeffs_y);   \
      highbd_sr_round_pack_clip_store_avx512(                             \
          res_a, res_b, &dst[i * dst_stride + j], dst_stride, store_mask, \
          conv_params, bd);                                               \
      update_pixels_16x7(signal);                                         \
    }                                                                     \
  }

#define CONVOLVE_SR_VERT_FILTER_4TAP                                          \
  {                                                                           \
    const uint16_t *const im = (const uint16_t *)im_block;                    \
    __m512i signal[5];                                                        \
    pack_16x5_init(im, im_stride, signal);                                    \
    for (i = 0; i < h; i += 2) {                                              \
      pack_16x5_pixels(im + i * im_stride, im_stride, signal);                \
      highbd_sr_vert_4tap_unpacked_avx512(                                    \
          signal, coeffs_y, &dst[i * dst_stride + j], dst_stride, store_mask, \
          conv_params, bd);                                                   \
      update_pixels_16x5(signal);                                             \
    }                                                                         \
  }

#define CONVOLVE_SR_VERT_FILTER_2TAP                                          \
  {                                                                           \
    const uint16_t *const im = (const uint16_t *)im_block;                    \
    __m256i r0 = _mm256_loadu_si256((const __m256i *)im);                     \
    for (i = 0; i < h; i += 2) {                                              \
      const __m256i r1 =                                                      \
          _mm256_loadu_si256((const __m256i *)(im + (i + 1) * im_stride));    \
      const __m256i r2 =                                                      \
          _mm256_loadu_si256((const __m256i *)(im + (i + 2) * im_stride));    \
      highbd_sr_vert_2tap_pair_avx512(                                        \
          pack_rows256(r0, r1), pack_rows256(r1, r2), coeffs_y[0],            \
          &dst[i * dst_stride + j], dst_stride, store_mask, conv_params, bd); \
      r0 = r2;                                                                \
    }                                                                         \
  }

#define CONVOLVE_SR_HORIZ_FILTER_16 \
  if (tap_x == 8)                   \
    CONVOLVE_HORIZ_FILTER_8TAP      \
  else if (tap_x == 6)              \
    CONVOLVE_HORIZ_FILTER_6TAP      \
  else if (tap_x == 4)              \
    CONVOLVE_HORIZ_FILTER_4TAP      \
  else {                            \
    assert(tap_x == 2);             \
    CONVOLVE_HORIZ_FILTER_2TAP      \
  }

#define CONVOLVE_SR_VERT_FILTER  \
  if (tap_y == 8)                \
    CONVOLVE_SR_VERT_FILTER_8TAP \
  else if (tap_y == 6)           \
    CONVOLVE_SR_VERT_FILTER_6TAP \
  else if (tap_y == 4)           \
    CONVOLVE_SR_VERT_FILTER_4TAP \
  else {                         \
    assert(tap_y == 2);          \
    CONVOLVE_SR_VERT_FILTER_2TAP \
  }

static void av2_highbd_convolve_2d_sr_specialized_avx512(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *restrict conv_params, int bd) {
  const int32_t tap_x = get_filter_tap(filter_params_x, subpel_x_qn);
  const int32_t tap_y = get_filter_tap(filter_params_y, subpel_y_qn);

  const int im_h = h + tap_y - 1;
  const int fo_vert = tap_y / 2 - 1;
  const int fo_horiz = tap_x / 2 - 1;
  const uint16_t *const src_ptr = src - fo_vert * src_stride - fo_horiz;
  int i, j;

  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  __m512i coeffs_y[4] = { 0 };
  __m512i coeffs_x[4] = { 0 };

  const __m512i round_const_x = _mm512_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);

  // Blocks narrower than 16 columns still run one full 16-column pass from
  // column 0, but only the first `w` pixels of each row are written.
  const __mmask16 store_mask = (w < AVX512_NARROW_COLS)
                                   ? (__mmask16)((1u << w) - 1)
                                   : AVX512_FULL_STORE_MASK;

  if (tap_x == 8)
    prepare_coeffs_avx512(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 6)
    prepare_coeffs_6t_avx512(filter_params_x, subpel_x_qn, coeffs_x);
  else if (tap_x == 4)
    prepare_coeffs_4t_avx512(filter_params_x, subpel_x_qn, coeffs_x);
  else {
    assert(tap_x == 2);
    coeffs_x[0] = prepare_coeffs_bilinear_avx512(filter_params_x, subpel_x_qn);
  }

  if (tap_y == 8)
    prepare_coeffs_avx512(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 6)
    prepare_coeffs_6t_avx512(filter_params_y, subpel_y_qn, coeffs_y);
  else if (tap_y == 4)
    prepare_coeffs_4t_avx512(filter_params_y, subpel_y_qn, coeffs_y);
  else {
    assert(tap_y == 2);
    coeffs_y[0] = prepare_coeffs_bilinear_avx512(filter_params_y, subpel_y_qn);
  }

  DECLARE_ALIGNED(
      64, int16_t,
      im_block_left[(MAX_SB_SIZE + MAX_FILTER_TAP) * AVX512_NARROW_COLS]);
  // The filter macros read and write through `im_block`.
  int16_t *im_block;
  const int im_stride = AVX512_NARROW_COLS;

  j = 0;
  DECLARE_ALIGNED(
      64, int16_t,
      im_block_right[(MAX_SB_SIZE + MAX_FILTER_TAP) * AVX512_NARROW_COLS]);
  for (; j + AVX512_WIDE_COLS <= w; j += AVX512_WIDE_COLS) {
    highbd_convolve_horiz_32w_loadonly_avx512(
        src_ptr, src_stride, im_h, j, im_block_left, im_block_right, tap_x,
        coeffs_x, round_const_x, round_shift_x);

    im_block = im_block_left;
    CONVOLVE_SR_VERT_FILTER

    {
      const int j_left = j;
      j += AVX512_NARROW_COLS;
      im_block = im_block_right;
      CONVOLVE_SR_VERT_FILTER
      j = j_left;
    }
  }
  for (; j + AVX512_NARROW_COLS <= w; j += AVX512_NARROW_COLS) {
    im_block = im_block_left;
    CONVOLVE_SR_HORIZ_FILTER_16
    CONVOLVE_SR_VERT_FILTER
  }
  if (j < w) {
    // Wider blocks redo the last 16 columns (the overlap is inside the block).
    // Narrower blocks cannot, so they start at 0 and use `store_mask` instead.
    j = (w < AVX512_NARROW_COLS) ? 0 : w - AVX512_NARROW_COLS;
    im_block = im_block_left;
    CONVOLVE_SR_HORIZ_FILTER_16
    CONVOLVE_SR_VERT_FILTER
  }
}

static INLINE __m512i highbd_sr_bilinear_horiz_16_avx512(
    const uint16_t *row0, const uint16_t *row1, const __m512i coeffs_x,
    const __m512i round_const_x, const __m128i round_shift_x,
    const __m512i shuffle_mask) {
  const __m256i a0 = _mm256_loadu_si256((const __m256i *)row0);
  const __m256i a1 = _mm256_loadu_si256((const __m256i *)(row0 + 8));
  const __m256i b0 =
      row1 ? _mm256_loadu_si256((const __m256i *)row1) : _mm256_setzero_si256();
  const __m256i b1 = row1 ? _mm256_loadu_si256((const __m256i *)(row1 + 8))
                          : _mm256_setzero_si256();

  const __m256i rw0_lo = _mm256_permute2x128_si256(a0, b0, 0x20);
  const __m256i rw1_lo = _mm256_permute2x128_si256(a0, b0, 0x31);
  const __m256i rw0_hi = _mm256_permute2x128_si256(a1, b1, 0x20);
  const __m256i rw1_hi = _mm256_permute2x128_si256(a1, b1, 0x31);

  const __m512i rw0 =
      _mm512_inserti64x4(_mm512_castsi256_si512(rw0_lo), rw0_hi, 1);
  __m512i rw1 = _mm512_inserti64x4(_mm512_castsi256_si512(rw1_lo), rw1_hi, 1);
  rw1 = _mm512_alignr_epi8(rw1, rw0, 2);

  __m512i res0 = _mm512_madd_epi16(rw0, coeffs_x);
  __m512i res1 = _mm512_madd_epi16(rw1, coeffs_x);
  res0 = _mm512_sra_epi32(_mm512_add_epi32(res0, round_const_x), round_shift_x);
  res1 = _mm512_sra_epi32(_mm512_add_epi32(res1, round_const_x), round_shift_x);
  // Each lane is a0 a2 a4 a6 a1 a3 a5 a7 for one row of one 8-column group.
  const __m512i packed =
      _mm512_shuffle_epi8(_mm512_packs_epi32(res0, res1), shuffle_mask);
  // Lanes are A0..A7, B0..B7, A8..A15, B8..B15. Swap the middle two so each
  // source row is 16 contiguous pixels.
  return _mm512_shuffle_i64x2(packed, packed, 0xD8);
}

static INLINE void highbd_sr_bilinear_2d_16_avx512(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int h,
    const __m512i coeffs_x, const __m512i coeffs_y, const __mmask16 store_mask,
    const ConvolveParams *restrict conv_params, int bd) {
  // A 2-tap vertical filter needs one source row past the block.
  const int im_h = h + 1;
  const __m512i round_const_x = _mm512_set1_epi32(
      ((1 << conv_params->round_0) >> 1) + (1 << (bd + FILTER_BITS - 1)));
  const __m128i round_shift_x = _mm_cvtsi32_si128(conv_params->round_0);
  const __m128i reorder =
      _mm_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
  const __m512i shuffle_mask = _mm512_broadcast_i32x4(reorder);
  __m512i row[2];
  row[0] = highbd_sr_bilinear_horiz_16_avx512(src, src + src_stride, coeffs_x,
                                              round_const_x, round_shift_x,
                                              shuffle_mask);

  for (int i = 2; i < im_h; i += 2) {
    const uint16_t *const row0 = src + i * src_stride;
    const uint16_t *const row1 = (i + 1 < im_h) ? row0 + src_stride : NULL;
    row[1] = highbd_sr_bilinear_horiz_16_avx512(
        row0, row1, coeffs_x, round_const_x, round_shift_x, shuffle_mask);
    const __m512i row2 = _mm512_shuffle_i64x2(row[0], row[1], 0x4E);
    highbd_sr_vert_2tap_pair_avx512(row[0], row2, coeffs_y,
                                    dst + (i - 2) * dst_stride, dst_stride,
                                    store_mask, conv_params, bd);
    row[0] = row[1];
  }
}

static INLINE void av2_highbd_convolve_2d_sr_bilinear_avx512(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *restrict conv_params, int bd) {
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);
  assert((FILTER_BITS * 2 - conv_params->round_0 - conv_params->round_1) == 0);
  assert((h & 1) == 0 && w > AVX512_NARROW_COLS / 2);

  // Blocks narrower than 16 columns run one 16-column pass from column 0 and
  // write only their first `w` pixels of each row.
  const __mmask16 store_mask = (w < AVX512_NARROW_COLS)
                                   ? (__mmask16)((1u << w) - 1)
                                   : AVX512_FULL_STORE_MASK;

  const __m512i coeffs_x =
      prepare_coeffs_bilinear_avx512(filter_params_x, subpel_x_qn);
  const __m512i coeffs_y =
      prepare_coeffs_bilinear_avx512(filter_params_y, subpel_y_qn);

  int j = 0;
  for (; j + AVX512_NARROW_COLS <= w; j += AVX512_NARROW_COLS) {
    highbd_sr_bilinear_2d_16_avx512(src + j, src_stride, dst + j, dst_stride, h,
                                    coeffs_x, coeffs_y, store_mask, conv_params,
                                    bd);
  }
  if (j < w) {
    j = (w < AVX512_NARROW_COLS) ? 0 : w - AVX512_NARROW_COLS;
    highbd_sr_bilinear_2d_16_avx512(src + j, src_stride, dst + j, dst_stride, h,
                                    coeffs_x, coeffs_y, store_mask, conv_params,
                                    bd);
  }
}

void av2_highbd_convolve_2d_sr_avx512(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *restrict conv_params, int bd) {
  if (w <= AVX512_NARROW_COLS / 2) {
    if ((filter_params_x->interp_filter == BILINEAR) &&
        (filter_params_y->interp_filter == BILINEAR)) {
      av2_highbd_convolve_2d_sr_bilinear_avx2(
          src, src_stride, dst, dst_stride, w, h, filter_params_x,
          filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
      return;
    }
    if ((h & 1) == 0) {
      const int32_t tap_x4 = get_filter_tap(filter_params_x, subpel_x_qn);
      const int32_t tap_y4 = get_filter_tap(filter_params_y, subpel_y_qn);
      if (tap_x4 == 4 && tap_y4 == 4 && h <= 8) {
        highbd_convolve_2d_sr_xy_4tap_avx2(
            src, src_stride, dst, dst_stride, w, h, filter_params_x,
            filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
        return;
      }
    }
    highbd_convolve_2d_sr_narrow_avx2(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    return;
  }

  if (filter_params_x->interp_filter == BILINEAR &&
      filter_params_y->interp_filter == BILINEAR && (h & 1) == 0) {
    av2_highbd_convolve_2d_sr_bilinear_avx512(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
    return;
  }

  av2_highbd_convolve_2d_sr_specialized_avx512(
      src, src_stride, dst, dst_stride, w, h, filter_params_x, filter_params_y,
      subpel_x_qn, subpel_y_qn, conv_params, bd);
}
