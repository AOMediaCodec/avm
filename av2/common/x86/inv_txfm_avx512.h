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

#ifndef AVM_AV2_COMMON_X86_INV_TXFM_AVX512_H_
#define AVM_AV2_COMMON_X86_INV_TXFM_AVX512_H_

#include <immintrin.h>

#include "avm_dsp/x86/synonyms.h"

#ifdef __cplusplus
extern "C" {
#endif

// AVX2 kernels (highbd_inv_txfm_avx2.c), used for fewer than 16 lines.
void inv_txfm_dct2_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_idtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_adst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_fdst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_ddtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_fddt_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_dct2_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);
void inv_txfm_idtx_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max);

// AVX2 1D dispatcher; handles lengths 4 and 8.
void inv_transform_1d_avx2(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max, const int tx_type_index,
                           const int size_index);

// AVX-512 kernels for all bit depths (highbd_inv_txfm_avx512.c).
void inv_txfm_dct2_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_idtx_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_adst_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_fdst_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_ddtx_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_fddt_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_dct2_size32_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);
void inv_txfm_idtx_size32_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max);

// 1D dispatcher for bit depth 8 (lowbd_inv_txfm_avx512.c).
void inv_transform_1d_lowbd_avx512(const int *src, int *dst, int shift,
                                   int line, int skip_line, int zero_line,
                                   int coef_min, int coef_max,
                                   int tx_type_index, int size_index);

// Native ZMM 16x16 int32 transpose.
static INLINE void transpose_load_16x16_avx512(const int *src, __m512i *b,
                                               int size) {
  __m512i r0, r1, r2, r3, r4, r5, r6, r7;
  __m512i r8, r9, ra, rb, rc, rd, re, rf;
  __m512i t0, t1, t2, t3, t4, t5, t6, t7;
  __m512i t8, t9, ta, tb, tc, td, te, tf;

  r0 = _mm512_loadu_si512(src + 0 * size);
  r1 = _mm512_loadu_si512(src + 1 * size);
  r2 = _mm512_loadu_si512(src + 2 * size);
  r3 = _mm512_loadu_si512(src + 3 * size);
  r4 = _mm512_loadu_si512(src + 4 * size);
  r5 = _mm512_loadu_si512(src + 5 * size);
  r6 = _mm512_loadu_si512(src + 6 * size);
  r7 = _mm512_loadu_si512(src + 7 * size);
  r8 = _mm512_loadu_si512(src + 8 * size);
  r9 = _mm512_loadu_si512(src + 9 * size);
  ra = _mm512_loadu_si512(src + 10 * size);
  rb = _mm512_loadu_si512(src + 11 * size);
  rc = _mm512_loadu_si512(src + 12 * size);
  rd = _mm512_loadu_si512(src + 13 * size);
  re = _mm512_loadu_si512(src + 14 * size);
  rf = _mm512_loadu_si512(src + 15 * size);

  t0 = _mm512_unpacklo_epi32(r0, r1);
  t1 = _mm512_unpackhi_epi32(r0, r1);
  t2 = _mm512_unpacklo_epi32(r2, r3);
  t3 = _mm512_unpackhi_epi32(r2, r3);
  t4 = _mm512_unpacklo_epi32(r4, r5);
  t5 = _mm512_unpackhi_epi32(r4, r5);
  t6 = _mm512_unpacklo_epi32(r6, r7);
  t7 = _mm512_unpackhi_epi32(r6, r7);
  t8 = _mm512_unpacklo_epi32(r8, r9);
  t9 = _mm512_unpackhi_epi32(r8, r9);
  ta = _mm512_unpacklo_epi32(ra, rb);
  tb = _mm512_unpackhi_epi32(ra, rb);
  tc = _mm512_unpacklo_epi32(rc, rd);
  td = _mm512_unpackhi_epi32(rc, rd);
  te = _mm512_unpacklo_epi32(re, rf);
  tf = _mm512_unpackhi_epi32(re, rf);

  r0 = _mm512_unpacklo_epi64(t0, t2);
  r1 = _mm512_unpackhi_epi64(t0, t2);
  r2 = _mm512_unpacklo_epi64(t1, t3);
  r3 = _mm512_unpackhi_epi64(t1, t3);
  r4 = _mm512_unpacklo_epi64(t4, t6);
  r5 = _mm512_unpackhi_epi64(t4, t6);
  r6 = _mm512_unpacklo_epi64(t5, t7);
  r7 = _mm512_unpackhi_epi64(t5, t7);
  r8 = _mm512_unpacklo_epi64(t8, ta);
  r9 = _mm512_unpackhi_epi64(t8, ta);
  ra = _mm512_unpacklo_epi64(t9, tb);
  rb = _mm512_unpackhi_epi64(t9, tb);
  rc = _mm512_unpacklo_epi64(tc, te);
  rd = _mm512_unpackhi_epi64(tc, te);
  re = _mm512_unpacklo_epi64(td, tf);
  rf = _mm512_unpackhi_epi64(td, tf);

  t0 = _mm512_shuffle_i32x4(r0, r4, 0x88);
  t1 = _mm512_shuffle_i32x4(r1, r5, 0x88);
  t2 = _mm512_shuffle_i32x4(r2, r6, 0x88);
  t3 = _mm512_shuffle_i32x4(r3, r7, 0x88);
  t4 = _mm512_shuffle_i32x4(r0, r4, 0xdd);
  t5 = _mm512_shuffle_i32x4(r1, r5, 0xdd);
  t6 = _mm512_shuffle_i32x4(r2, r6, 0xdd);
  t7 = _mm512_shuffle_i32x4(r3, r7, 0xdd);
  t8 = _mm512_shuffle_i32x4(r8, rc, 0x88);
  t9 = _mm512_shuffle_i32x4(r9, rd, 0x88);
  ta = _mm512_shuffle_i32x4(ra, re, 0x88);
  tb = _mm512_shuffle_i32x4(rb, rf, 0x88);
  tc = _mm512_shuffle_i32x4(r8, rc, 0xdd);
  td = _mm512_shuffle_i32x4(r9, rd, 0xdd);
  te = _mm512_shuffle_i32x4(ra, re, 0xdd);
  tf = _mm512_shuffle_i32x4(rb, rf, 0xdd);

  b[0] = _mm512_shuffle_i32x4(t0, t8, 0x88);
  b[1] = _mm512_shuffle_i32x4(t1, t9, 0x88);
  b[2] = _mm512_shuffle_i32x4(t2, ta, 0x88);
  b[3] = _mm512_shuffle_i32x4(t3, tb, 0x88);
  b[4] = _mm512_shuffle_i32x4(t4, tc, 0x88);
  b[5] = _mm512_shuffle_i32x4(t5, td, 0x88);
  b[6] = _mm512_shuffle_i32x4(t6, te, 0x88);
  b[7] = _mm512_shuffle_i32x4(t7, tf, 0x88);
  b[8] = _mm512_shuffle_i32x4(t0, t8, 0xdd);
  b[9] = _mm512_shuffle_i32x4(t1, t9, 0xdd);
  b[10] = _mm512_shuffle_i32x4(t2, ta, 0xdd);
  b[11] = _mm512_shuffle_i32x4(t3, tb, 0xdd);
  b[12] = _mm512_shuffle_i32x4(t4, tc, 0xdd);
  b[13] = _mm512_shuffle_i32x4(t5, td, 0xdd);
  b[14] = _mm512_shuffle_i32x4(t6, te, 0xdd);
  b[15] = _mm512_shuffle_i32x4(t7, tf, 0xdd);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_X86_INV_TXFM_AVX512_H_
