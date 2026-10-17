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

#ifndef AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_
#define AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_

#include <immintrin.h>
#include <assert.h>

#include "av2/common/filter.h"
#include "avm_dsp/x86/synonyms.h"

static INLINE void prepare_coeffs_avx512(
    const InterpFilterParams *const filter_params, const int subpel_q4,
    __m512i *const coeffs /* [4] */) {
  const int16_t *filter = av2_get_interp_filter_subpel_kernel(
      filter_params, subpel_q4 & SUBPEL_MASK);
  const __m128i coeff_8 = _mm_loadu_si128((__m128i *)filter);
  const __m512i coeff = _mm512_broadcast_i32x4(coeff_8);
  coeffs[0] = _mm512_shuffle_epi32(coeff, 0x00);  // 0 1 0 1 ...
  coeffs[1] = _mm512_shuffle_epi32(coeff, 0x55);  // 2 3 2 3 ...
  coeffs[2] = _mm512_shuffle_epi32(coeff, 0xaa);  // 4 5 4 5 ...
  coeffs[3] = _mm512_shuffle_epi32(coeff, 0xff);  // 6 7 6 7 ...
}

static INLINE __m512i convolve_avx512(const __m512i *const s,
                                      const __m512i *const c) {
  const __m512i r0 = _mm512_madd_epi16(s[0], c[0]);
  const __m512i r1 = _mm512_madd_epi16(s[1], c[1]);
  const __m512i r2 = _mm512_madd_epi16(s[2], c[2]);
  const __m512i r3 = _mm512_madd_epi16(s[3], c[3]);
  return _mm512_add_epi32(_mm512_add_epi32(r0, r1), _mm512_add_epi32(r2, r3));
}

// 6-tap kernel stored in an 8-tap slot: skip the leading zero tap.
static INLINE void prepare_coeffs_6t_avx512(
    const InterpFilterParams *const filter_params, const int subpel_q4,
    __m512i *const coeffs /* [4] */) {
  const int16_t *filter = av2_get_interp_filter_subpel_kernel(
      filter_params, subpel_q4 & SUBPEL_MASK);
  const __m128i coeff_8 = _mm_loadu_si128((const __m128i *)(filter + 1));
  const __m512i coeff = _mm512_broadcast_i32x4(coeff_8);
  coeffs[0] = _mm512_shuffle_epi32(coeff, 0x00);  // 1 2 1 2 ...
  coeffs[1] = _mm512_shuffle_epi32(coeff, 0x55);  // 3 4 3 4 ...
  coeffs[2] = _mm512_shuffle_epi32(coeff, 0xaa);  // 5 6 5 6 ...
}

// Center 4 taps of an 8-tap slot.
static INLINE void prepare_coeffs_4t_avx512(
    const InterpFilterParams *const filter_params, const int subpel_q4,
    __m512i *const coeffs /* [4] */) {
  const int16_t *filter = av2_get_interp_filter_subpel_kernel(
      filter_params, subpel_q4 & SUBPEL_MASK);
  const __m128i coeff_8 = _mm_loadu_si128((const __m128i *)filter);
  const __m512i coeff = _mm512_broadcast_i32x4(coeff_8);
  coeffs[0] = _mm512_shuffle_epi32(coeff, 0x55);  // 2 3 2 3 ...
  coeffs[1] = _mm512_shuffle_epi32(coeff, 0xaa);  // 4 5 4 5 ...
}

static INLINE __m512i prepare_coeffs_bilinear_avx512(
    const InterpFilterParams *const filter_params, const int subpel_q4) {
  const int16_t *filter = av2_get_interp_filter_subpel_kernel(
      filter_params, subpel_q4 & SUBPEL_MASK);
  const int16_t *filter_pos = filter;
  if (filter_params->taps == 8) filter_pos = filter + 3;
  const int filter_coeff = ((filter_pos[1] << 16) | filter_pos[0]);
  return _mm512_set1_epi32(filter_coeff);
}

static INLINE __m512i convolve_6tap_avx512(const __m512i *const s,
                                           const __m512i *const c) {
  const __m512i r0 = _mm512_madd_epi16(s[0], c[0]);
  const __m512i r1 = _mm512_madd_epi16(s[1], c[1]);
  const __m512i r2 = _mm512_madd_epi16(s[2], c[2]);
  return _mm512_add_epi32(_mm512_add_epi32(r0, r1), r2);
}

static INLINE __m512i convolve_4tap_avx512(const __m512i *const s,
                                           const __m512i *const c) {
  const __m512i r0 = _mm512_madd_epi16(s[0], c[0]);
  const __m512i r1 = _mm512_madd_epi16(s[1], c[1]);
  return _mm512_add_epi32(r0, r1);
}

static INLINE __m512i pack_rows256(const __m256i row_a, const __m256i row_b) {
  return _mm512_inserti64x4(_mm512_castsi256_si512(row_a), row_b, 1);
}

// Vertical 8-tap: 16 columns, sliding 9-row window (512-bit sig layout).
static INLINE void pack_16x9_init(const uint16_t *src, ptrdiff_t pitch,
                                  __m512i *sig) {
  const __m256i r0 = _mm256_loadu_si256((const __m256i *)(src + 0 * pitch));
  const __m256i r1 = _mm256_loadu_si256((const __m256i *)(src + 1 * pitch));
  const __m256i r2 = _mm256_loadu_si256((const __m256i *)(src + 2 * pitch));
  const __m256i r3 = _mm256_loadu_si256((const __m256i *)(src + 3 * pitch));
  const __m256i r4 = _mm256_loadu_si256((const __m256i *)(src + 4 * pitch));
  const __m256i r5 = _mm256_loadu_si256((const __m256i *)(src + 5 * pitch));
  const __m256i r6 = _mm256_loadu_si256((const __m256i *)(src + 6 * pitch));

  const __m512i s01 = pack_rows256(r0, r1);
  const __m512i s12 = pack_rows256(r1, r2);
  const __m512i s23 = pack_rows256(r2, r3);
  const __m512i s34 = pack_rows256(r3, r4);
  const __m512i s45 = pack_rows256(r4, r5);
  const __m512i s56 = pack_rows256(r5, r6);

  sig[0] = _mm512_unpacklo_epi16(s01, s12);
  sig[4] = _mm512_unpackhi_epi16(s01, s12);
  sig[1] = _mm512_unpacklo_epi16(s23, s34);
  sig[5] = _mm512_unpackhi_epi16(s23, s34);
  sig[2] = _mm512_unpacklo_epi16(s45, s56);
  sig[6] = _mm512_unpackhi_epi16(s45, s56);
  sig[8] = _mm512_castsi256_si512(r6);
}

static INLINE void pack_16x9_pixels(const uint16_t *src, ptrdiff_t pitch,
                                    __m512i *sig) {
  const __m256i s7 = _mm256_loadu_si256((const __m256i *)(src + 7 * pitch));
  const __m256i s8 = _mm256_loadu_si256((const __m256i *)(src + 8 * pitch));
  const __m512i s2 = _mm512_inserti64x4(sig[8], s7, 1);
  const __m512i s3 = pack_rows256(s7, s8);
  sig[3] = _mm512_unpacklo_epi16(s2, s3);
  sig[7] = _mm512_unpackhi_epi16(s2, s3);
  sig[8] = _mm512_castsi256_si512(s8);
}

static INLINE void update_pixels_16x9(__m512i *sig) {
  for (int i = 0; i < 3; ++i) {
    sig[i] = sig[i + 1];
    sig[i + 4] = sig[i + 5];
  }
}

// Vertical 6-tap: 16 columns. Two output rows need rows 0..6.
// sig[0..2] are the low 8 columns, sig[3..5] the high 8. sig[6] keeps the
// last loaded row for the next pair.
static INLINE void pack_16x7_init(const uint16_t *src, ptrdiff_t pitch,
                                  __m512i *sig) {
  const __m256i r0 = _mm256_loadu_si256((const __m256i *)(src + 0 * pitch));
  const __m256i r1 = _mm256_loadu_si256((const __m256i *)(src + 1 * pitch));
  const __m256i r2 = _mm256_loadu_si256((const __m256i *)(src + 2 * pitch));
  const __m256i r3 = _mm256_loadu_si256((const __m256i *)(src + 3 * pitch));
  const __m256i r4 = _mm256_loadu_si256((const __m256i *)(src + 4 * pitch));

  const __m512i s01 = pack_rows256(r0, r1);
  const __m512i s12 = pack_rows256(r1, r2);
  const __m512i s23 = pack_rows256(r2, r3);
  const __m512i s34 = pack_rows256(r3, r4);

  sig[0] = _mm512_unpacklo_epi16(s01, s12);
  sig[3] = _mm512_unpackhi_epi16(s01, s12);
  sig[1] = _mm512_unpacklo_epi16(s23, s34);
  sig[4] = _mm512_unpackhi_epi16(s23, s34);
  sig[6] = _mm512_castsi256_si512(r4);
}

static INLINE void pack_16x7_pixels(const uint16_t *src, ptrdiff_t pitch,
                                    __m512i *sig) {
  const __m256i s5 = _mm256_loadu_si256((const __m256i *)(src + 5 * pitch));
  const __m256i s6 = _mm256_loadu_si256((const __m256i *)(src + 6 * pitch));
  const __m512i s2 = _mm512_inserti64x4(sig[6], s5, 1);
  const __m512i s3 = pack_rows256(s5, s6);
  sig[2] = _mm512_unpacklo_epi16(s2, s3);
  sig[5] = _mm512_unpackhi_epi16(s2, s3);
  sig[6] = _mm512_castsi256_si512(s6);
}

static INLINE void update_pixels_16x7(__m512i *sig) {
  for (int i = 0; i < 2; ++i) {
    sig[i] = sig[i + 1];
    sig[i + 3] = sig[i + 4];
  }
}

// Vertical 4-tap: 16 columns. Two output rows need rows 0..4.
static INLINE void pack_16x5_init(const uint16_t *src, ptrdiff_t pitch,
                                  __m512i *sig) {
  const __m256i r0 = _mm256_loadu_si256((const __m256i *)(src + 0 * pitch));
  const __m256i r1 = _mm256_loadu_si256((const __m256i *)(src + 1 * pitch));
  const __m256i r2 = _mm256_loadu_si256((const __m256i *)(src + 2 * pitch));

  const __m512i s01 = pack_rows256(r0, r1);
  const __m512i s12 = pack_rows256(r1, r2);

  sig[0] = _mm512_unpacklo_epi16(s01, s12);
  sig[2] = _mm512_unpackhi_epi16(s01, s12);
  sig[4] = _mm512_castsi256_si512(r2);
}

static INLINE void pack_16x5_pixels(const uint16_t *src, ptrdiff_t pitch,
                                    __m512i *sig) {
  const __m256i s3 = _mm256_loadu_si256((const __m256i *)(src + 3 * pitch));
  const __m256i s4 = _mm256_loadu_si256((const __m256i *)(src + 4 * pitch));
  const __m512i s1 = _mm512_inserti64x4(sig[4], s3, 1);
  const __m512i s2 = pack_rows256(s3, s4);
  sig[1] = _mm512_unpacklo_epi16(s1, s2);
  sig[3] = _mm512_unpackhi_epi16(s1, s2);
  sig[4] = _mm512_castsi256_si512(s4);
}

static INLINE void update_pixels_16x5(__m512i *sig) {
  sig[0] = sig[1];
  sig[2] = sig[3];
}

// Round, pack, and split one 32-pixel horizontal result into two 16-wide
// intermediate rows. Mirrors highbd_sr_horiz_16w_round_store, which splits
// one 16-pixel result into two 8-wide rows.
static INLINE void highbd_sr_horiz_32w_round_store(
    __m512i res_even, __m512i res_odd, const __m512i round_const_x,
    const __m128i round_shift_x, int16_t *im_left, int16_t *im_right, int i) {
  res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_x),
                              round_shift_x);
  res_odd =
      _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_x), round_shift_x);
  const __m512i res =
      _mm512_unpacklo_epi16(_mm512_packs_epi32(res_even, res_even),
                            _mm512_packs_epi32(res_odd, res_odd));
  _mm256_store_si256((__m256i *)&im_left[i * 16], _mm512_castsi512_si256(res));
  _mm256_store_si256((__m256i *)&im_right[i * 16],
                     _mm512_extracti64x4_epi64(res, 1));
}

// Produce two adjacent 16-wide intermediate blocks with load-only horizontal.
// tap_x is constant for the call, so each tap length gets its own loop.
static INLINE void highbd_convolve_horiz_32w_loadonly_avx512(
    const uint16_t *src_ptr, int src_stride, int im_h, int j, int16_t *im_left,
    int16_t *im_right, int tap_x, const __m512i *coeffs_x,
    const __m512i round_const_x, const __m128i round_shift_x) {
  if (tap_x == 8) {
    for (int i = 0; i < im_h; ++i) {
      const uint16_t *const p = &src_ptr[i * src_stride + j];
      __m512i s[4];
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 0));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 2));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 4));
      s[3] = _mm512_loadu_si512((const __m512i *)(p + 6));
      const __m512i res_even = convolve_avx512(s, coeffs_x);
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 1));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 3));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 5));
      s[3] = _mm512_loadu_si512((const __m512i *)(p + 7));
      const __m512i res_odd = convolve_avx512(s, coeffs_x);
      highbd_sr_horiz_32w_round_store(res_even, res_odd, round_const_x,
                                      round_shift_x, im_left, im_right, i);
    }
  } else if (tap_x == 6) {
    for (int i = 0; i < im_h; ++i) {
      const uint16_t *const p = &src_ptr[i * src_stride + j];
      __m512i s[3];
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 0));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 2));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 4));
      const __m512i res_even = convolve_6tap_avx512(s, coeffs_x);
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 1));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 3));
      s[2] = _mm512_loadu_si512((const __m512i *)(p + 5));
      const __m512i res_odd = convolve_6tap_avx512(s, coeffs_x);
      highbd_sr_horiz_32w_round_store(res_even, res_odd, round_const_x,
                                      round_shift_x, im_left, im_right, i);
    }
  } else if (tap_x == 4) {
    for (int i = 0; i < im_h; ++i) {
      const uint16_t *const p = &src_ptr[i * src_stride + j];
      __m512i s[2];
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 0));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 2));
      const __m512i res_even = convolve_4tap_avx512(s, coeffs_x);
      s[0] = _mm512_loadu_si512((const __m512i *)(p + 1));
      s[1] = _mm512_loadu_si512((const __m512i *)(p + 3));
      const __m512i res_odd = convolve_4tap_avx512(s, coeffs_x);
      highbd_sr_horiz_32w_round_store(res_even, res_odd, round_const_x,
                                      round_shift_x, im_left, im_right, i);
    }
  } else {
    assert(tap_x == 2);
    for (int i = 0; i < im_h; ++i) {
      const uint16_t *const p = &src_ptr[i * src_stride + j];
      const __m512i s0 = _mm512_loadu_si512((const __m512i *)(p + 0));
      const __m512i s1 = _mm512_loadu_si512((const __m512i *)(p + 1));
      const __m512i res_even = _mm512_madd_epi16(s0, coeffs_x[0]);
      const __m512i res_odd = _mm512_madd_epi16(s1, coeffs_x[0]);
      highbd_sr_horiz_32w_round_store(res_even, res_odd, round_const_x,
                                      round_shift_x, im_left, im_right, i);
    }
  }
}

// Pack two source rows. Low 16 lanes are row 0, high 16 lanes are row 1.
// A missing second row (row1 == NULL) is zeros, same as the AVX2 8-wide path.
static INLINE __m512i horiz16_2row_load(const uint16_t *row0,
                                        const uint16_t *row1, int off) {
  const __m256i a0 = _mm256_loadu_si256((const __m256i *)(row0 + off));
  const __m256i a1 = row1 ? _mm256_loadu_si256((const __m256i *)(row1 + off))
                          : _mm256_setzero_si256();
  return _mm512_inserti64x4(_mm512_castsi256_si512(a0), a1, 1);
}

// Round one pair of rows. Low 16 lanes are row 0, high 16 lanes are row 1.
static INLINE __m512i horiz16_round_pack(__m512i res_even, __m512i res_odd,
                                         const __m512i round_const_x,
                                         const __m128i round_shift_x) {
  res_even = _mm512_sra_epi32(_mm512_add_epi32(res_even, round_const_x),
                              round_shift_x);
  res_odd =
      _mm512_sra_epi32(_mm512_add_epi32(res_odd, round_const_x), round_shift_x);
  return _mm512_unpacklo_epi16(_mm512_packs_epi32(res_even, res_even),
                               _mm512_packs_epi32(res_odd, res_odd));
}

// 4-tap horizontal filter for two source rows, 16 outputs each.
// Same offsets as the AVX2 load-only 4-tap path: even windows at 0 and 2,
// odd windows at 1 and 3.
static INLINE __m512i highbd_sr_horiz_4tap_pair_avx512(
    const uint16_t *src_ptr, int src_stride, int src_i, int j, int im_h,
    const __m512i *coeffs_x, const __m512i round_const_x,
    const __m128i round_shift_x) {
  const uint16_t *const p0 = src_ptr + src_i * src_stride + j;
  const uint16_t *const p1 = (src_i + 1 < im_h) ? p0 + src_stride : NULL;
  __m512i s[2];

  s[0] = horiz16_2row_load(p0, p1, 0);
  s[1] = horiz16_2row_load(p0, p1, 2);
  const __m512i res_even = convolve_4tap_avx512(s, coeffs_x);

  s[0] = horiz16_2row_load(p0, p1, 1);
  s[1] = horiz16_2row_load(p0, p1, 3);
  const __m512i res_odd = convolve_4tap_avx512(s, coeffs_x);
  return horiz16_round_pack(res_even, res_odd, round_const_x, round_shift_x);
}

// 2-tap horizontal filter for two source rows, 16 outputs each.
// Same role as highbd_sr_horiz_2tap_pair_avx2.
static INLINE __m512i highbd_sr_horiz_2tap_pair_avx512(
    const uint16_t *src_ptr, int src_stride, int src_i, int j, int im_h,
    const __m512i *coeffs_x, const __m512i round_const_x,
    const __m128i round_shift_x) {
  const uint16_t *const p0 = src_ptr + src_i * src_stride + j;
  const uint16_t *const p1 = (src_i + 1 < im_h) ? p0 + src_stride : NULL;
  const __m512i r0 = horiz16_2row_load(p0, p1, 0);
  const __m512i r1 = horiz16_2row_load(p0, p1, 1);
  const __m512i res_even = _mm512_madd_epi16(r0, coeffs_x[0]);
  const __m512i res_odd = _mm512_madd_epi16(r1, coeffs_x[0]);
  return horiz16_round_pack(res_even, res_odd, round_const_x, round_shift_x);
}

// 16-wide load-only horizontal filters. Two source rows share one register.
// Each tap window is its own load, at the same offsets as
// highbd_convolve_horiz_16w_loadonly_avx2. An odd last row uses a zero partner.
#define CONVOLVE_HORIZ_FILTER_8TAP                                           \
  for (i = 0; i < im_h; i += 2) {                                            \
    const uint16_t *const p0 = src_ptr + i * src_stride + j;                 \
    const uint16_t *const p1 = (i + 1 < im_h) ? p0 + src_stride : NULL;      \
    __m512i s[4];                                                            \
                                                                             \
    s[0] = horiz16_2row_load(p0, p1, 0);                                     \
    s[1] = horiz16_2row_load(p0, p1, 2);                                     \
    s[2] = horiz16_2row_load(p0, p1, 4);                                     \
    s[3] = horiz16_2row_load(p0, p1, 6);                                     \
    const __m512i res_even = convolve_avx512(s, coeffs_x);                   \
                                                                             \
    s[0] = horiz16_2row_load(p0, p1, 1);                                     \
    s[1] = horiz16_2row_load(p0, p1, 3);                                     \
    s[2] = horiz16_2row_load(p0, p1, 5);                                     \
    s[3] = horiz16_2row_load(p0, p1, 7);                                     \
    const __m512i res_odd = convolve_avx512(s, coeffs_x);                    \
                                                                             \
    const __m512i res =                                                      \
        horiz16_round_pack(res_even, res_odd, round_const_x, round_shift_x); \
    _mm512_store_si512((__m512i *)&im_block[i * im_stride], res);            \
  }

#define CONVOLVE_HORIZ_FILTER_6TAP                                           \
  for (i = 0; i < im_h; i += 2) {                                            \
    const uint16_t *const p0 = src_ptr + i * src_stride + j;                 \
    const uint16_t *const p1 = (i + 1 < im_h) ? p0 + src_stride : NULL;      \
    __m512i s[3];                                                            \
                                                                             \
    s[0] = horiz16_2row_load(p0, p1, 0);                                     \
    s[1] = horiz16_2row_load(p0, p1, 2);                                     \
    s[2] = horiz16_2row_load(p0, p1, 4);                                     \
    const __m512i res_even = convolve_6tap_avx512(s, coeffs_x);              \
                                                                             \
    s[0] = horiz16_2row_load(p0, p1, 1);                                     \
    s[1] = horiz16_2row_load(p0, p1, 3);                                     \
    s[2] = horiz16_2row_load(p0, p1, 5);                                     \
    const __m512i res_odd = convolve_6tap_avx512(s, coeffs_x);               \
                                                                             \
    const __m512i res =                                                      \
        horiz16_round_pack(res_even, res_odd, round_const_x, round_shift_x); \
    _mm512_store_si512((__m512i *)&im_block[i * im_stride], res);            \
  }

#define CONVOLVE_HORIZ_FILTER_4TAP                                \
  for (i = 0; i < im_h; i += 2) {                                 \
    const __m512i res = highbd_sr_horiz_4tap_pair_avx512(         \
        src_ptr, src_stride, i, j, im_h, coeffs_x, round_const_x, \
        round_shift_x);                                           \
    _mm512_store_si512((__m512i *)&im_block[i * im_stride], res); \
  }

#define CONVOLVE_HORIZ_FILTER_2TAP                                \
  for (i = 0; i < im_h; i += 2) {                                 \
    const __m512i res = highbd_sr_horiz_2tap_pair_avx512(         \
        src_ptr, src_stride, i, j, im_h, coeffs_x, round_const_x, \
        round_shift_x);                                           \
    _mm512_store_si512((__m512i *)&im_block[i * im_stride], res); \
  }

static INLINE void load_4rows(const uint16_t *src_ptr, int src_stride, int off,
                              __m512i *r0, __m512i *r1) {
  const __m128i a0 = _mm_loadu_si128((__m128i *)&src_ptr[0 * src_stride + off]);
  const __m128i b0 = _mm_loadu_si128((__m128i *)&src_ptr[1 * src_stride + off]);
  const __m128i c0 = _mm_loadu_si128((__m128i *)&src_ptr[2 * src_stride + off]);
  const __m128i d0 = _mm_loadu_si128((__m128i *)&src_ptr[3 * src_stride + off]);
  const __m128i a1 =
      _mm_loadu_si128((__m128i *)&src_ptr[0 * src_stride + off + 8]);
  const __m128i b1 =
      _mm_loadu_si128((__m128i *)&src_ptr[1 * src_stride + off + 8]);
  const __m128i c1 =
      _mm_loadu_si128((__m128i *)&src_ptr[2 * src_stride + off + 8]);
  const __m128i d1 =
      _mm_loadu_si128((__m128i *)&src_ptr[3 * src_stride + off + 8]);
  __m512i t0 = _mm512_castsi128_si512(a0);
  t0 = _mm512_inserti32x4(t0, b0, 1);
  t0 = _mm512_inserti32x4(t0, c0, 2);
  t0 = _mm512_inserti32x4(t0, d0, 3);
  __m512i t1 = _mm512_castsi128_si512(a1);
  t1 = _mm512_inserti32x4(t1, b1, 1);
  t1 = _mm512_inserti32x4(t1, c1, 2);
  t1 = _mm512_inserti32x4(t1, d1, 3);
  *r0 = t0;
  *r1 = t1;
}

#endif  // AVM_AVM_DSP_X86_CONVOLVE_AVX512_H_
