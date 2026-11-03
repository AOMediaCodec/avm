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
#include <assert.h>
#include <immintrin.h>

#include "config/av2_rtcd.h"
#include "config/avm_config.h"

#include "av2/common/av2_txfm.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/x86/highbd_fwd_txfm_x86.h"
#include "avm_ports/mem.h"

// Transposes 16 vectors of 16 values: out[i] holds lane i of a[0..15].
static INLINE void transpose_16x16_avx512(const __m512i *a, __m512i *out) {
  __m512i r0, r1, r2, r3, r4, r5, r6, r7;
  __m512i r8, r9, ra, rb, rc, rd, re, rf;
  __m512i t0, t1, t2, t3, t4, t5, t6, t7;
  __m512i t8, t9, ta, tb, tc, td, te, tf;

  t0 = _mm512_unpacklo_epi32(a[0], a[1]);
  t1 = _mm512_unpackhi_epi32(a[0], a[1]);
  t2 = _mm512_unpacklo_epi32(a[2], a[3]);
  t3 = _mm512_unpackhi_epi32(a[2], a[3]);
  t4 = _mm512_unpacklo_epi32(a[4], a[5]);
  t5 = _mm512_unpackhi_epi32(a[4], a[5]);
  t6 = _mm512_unpacklo_epi32(a[6], a[7]);
  t7 = _mm512_unpackhi_epi32(a[6], a[7]);
  t8 = _mm512_unpacklo_epi32(a[8], a[9]);
  t9 = _mm512_unpackhi_epi32(a[8], a[9]);
  ta = _mm512_unpacklo_epi32(a[10], a[11]);
  tb = _mm512_unpackhi_epi32(a[10], a[11]);
  tc = _mm512_unpacklo_epi32(a[12], a[13]);
  td = _mm512_unpackhi_epi32(a[12], a[13]);
  te = _mm512_unpacklo_epi32(a[14], a[15]);
  tf = _mm512_unpackhi_epi32(a[14], a[15]);

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

  out[0] = _mm512_shuffle_i32x4(t0, t8, 0x88);
  out[1] = _mm512_shuffle_i32x4(t1, t9, 0x88);
  out[2] = _mm512_shuffle_i32x4(t2, ta, 0x88);
  out[3] = _mm512_shuffle_i32x4(t3, tb, 0x88);
  out[4] = _mm512_shuffle_i32x4(t4, tc, 0x88);
  out[5] = _mm512_shuffle_i32x4(t5, td, 0x88);
  out[6] = _mm512_shuffle_i32x4(t6, te, 0x88);
  out[7] = _mm512_shuffle_i32x4(t7, tf, 0x88);
  out[8] = _mm512_shuffle_i32x4(t0, t8, 0xdd);
  out[9] = _mm512_shuffle_i32x4(t1, t9, 0xdd);
  out[10] = _mm512_shuffle_i32x4(t2, ta, 0xdd);
  out[11] = _mm512_shuffle_i32x4(t3, tb, 0xdd);
  out[12] = _mm512_shuffle_i32x4(t4, tc, 0xdd);
  out[13] = _mm512_shuffle_i32x4(t5, td, 0xdd);
  out[14] = _mm512_shuffle_i32x4(t6, te, 0xdd);
  out[15] = _mm512_shuffle_i32x4(t7, tf, 0xdd);
}

// Transposes 16 vectors of 16 lines and stores line i at dst + i * size.
static INLINE void transpose_store_16x16_avx512(const __m512i *a, int *dst,
                                                int size) {
  __m512i t[16];
  transpose_16x16_avx512(a, t);
  for (int i = 0; i < 16; i++) _mm512_storeu_si512(dst + i * size, t[i]);
}

static void fwd_txfm_dct2_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert(nz_line >= 16 && (nz_line & 15) == 0);

  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int *tx_mat = tx_kernel_dct2_size16[FWD_TXFM][0];
  const int tx1d_size = 16;

  __m512i a[16], b[8], aa[8];
  __m512i c[4], d[4];
  __m512i e[2], f[2];

  __m512i v_offset = _mm512_set1_epi32(offset);

  __m512i txmat_4_0 = _mm512_set1_epi32(tx_mat[4 * 16 + 0]);  // 83
  __m512i txmat_4_1 = _mm512_set1_epi32(tx_mat[4 * 16 + 1]);  // 35

  __m512i txmat_2_0 = _mm512_set1_epi32(tx_mat[2 * 16 + 0]);  // 89
  __m512i txmat_2_1 = _mm512_set1_epi32(tx_mat[2 * 16 + 1]);  // 75
  __m512i txmat_2_2 = _mm512_set1_epi32(tx_mat[2 * 16 + 2]);  // 50
  __m512i txmat_2_3 = _mm512_set1_epi32(tx_mat[2 * 16 + 3]);  // 18

  __m512i txmat_1_0 = _mm512_set1_epi32(tx_mat[1 * 16 + 0]);  // 90
  __m512i txmat_1_1 = _mm512_set1_epi32(tx_mat[1 * 16 + 1]);  // 87
  __m512i txmat_1_2 = _mm512_set1_epi32(tx_mat[1 * 16 + 2]);  // 80
  __m512i txmat_1_3 = _mm512_set1_epi32(tx_mat[1 * 16 + 3]);  // 70
  __m512i txmat_1_4 = _mm512_set1_epi32(tx_mat[1 * 16 + 4]);  // 57
  __m512i txmat_1_5 = _mm512_set1_epi32(tx_mat[1 * 16 + 5]);  // 43
  __m512i txmat_1_6 = _mm512_set1_epi32(tx_mat[1 * 16 + 6]);  // 26
  __m512i txmat_1_7 = _mm512_set1_epi32(tx_mat[1 * 16 + 7]);  //  9

  for (j = 0; j < nz_line; j += 16) {
    for (k = 0; k < 8; k++) {
      __m512i src0 = _mm512_loadu_si512((const __m512i *)&src[k * line]);
      __m512i src1 =
          _mm512_loadu_si512((const __m512i *)&src[(tx1d_size - 1 - k) * line]);
      a[k] = _mm512_add_epi32(src0, src1);
      b[k] = _mm512_sub_epi32(src0, src1);
    }
    for (k = 0; k < 4; k++) {
      c[k] = _mm512_add_epi32(a[k], a[7 - k]);
      d[k] = _mm512_sub_epi32(a[k], a[7 - k]);
    }
    for (k = 0; k < 2; k++) {
      e[k] = _mm512_add_epi32(c[k], c[3 - k]);
      f[k] = _mm512_sub_epi32(c[k], c[3 - k]);
    }

    a[0] = _mm512_add_epi32(e[0], e[1]);
    a[0] = _mm512_slli_epi32(a[0], 6);

    a[8] = _mm512_sub_epi32(e[0], e[1]);
    a[8] = _mm512_slli_epi32(a[8], 6);

    a[4] = _mm512_add_epi32(_mm512_mullo_epi32(f[0], txmat_4_0),
                            _mm512_mullo_epi32(f[1], txmat_4_1));

    a[12] = _mm512_sub_epi32(_mm512_mullo_epi32(f[0], txmat_4_1),
                             _mm512_mullo_epi32(f[1], txmat_4_0));

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_0);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_1);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_2);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_3);
    a[2] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_1);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_3);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_0);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_2);
    a[6] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_2);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_0);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_3);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_1);
    a[10] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                             _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_3);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_2);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_1);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_0);
    a[14] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                             _mm512_sub_epi32(c[2], c[3]));

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_0);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_1);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_2);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_3);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_4);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_5);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_6);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_7);
    aa[0] = _mm512_add_epi32(_mm512_add_epi32(aa[0], aa[1]),
                             _mm512_add_epi32(aa[2], aa[3]));
    aa[4] = _mm512_add_epi32(_mm512_add_epi32(aa[4], aa[5]),
                             _mm512_add_epi32(aa[6], aa[7]));
    a[1] = _mm512_add_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_1);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_4);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_7);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_5);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_2);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_0);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_3);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_6);
    aa[0] = _mm512_add_epi32(_mm512_add_epi32(aa[0], aa[1]),
                             _mm512_sub_epi32(aa[2], aa[3]));
    aa[4] = _mm512_add_epi32(_mm512_add_epi32(aa[4], aa[5]),
                             _mm512_add_epi32(aa[6], aa[7]));
    a[3] = _mm512_sub_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_2);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_7);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_3);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_1);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_6);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_4);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_0);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_5);
    aa[0] = _mm512_sub_epi32(_mm512_add_epi32(aa[0], aa[1]),
                             _mm512_add_epi32(aa[2], aa[3]));
    aa[4] = _mm512_sub_epi32(_mm512_sub_epi32(aa[4], aa[5]),
                             _mm512_add_epi32(aa[6], aa[7]));
    a[5] = _mm512_sub_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_3);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_5);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_1);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_7);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_0);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_6);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_2);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_4);
    aa[0] = _mm512_sub_epi32(_mm512_sub_epi32(aa[0], aa[1]),
                             _mm512_sub_epi32(aa[2], aa[3]));
    aa[4] = _mm512_sub_epi32(_mm512_add_epi32(aa[4], aa[5]),
                             _mm512_add_epi32(aa[6], aa[7]));
    a[7] = _mm512_add_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_4);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_2);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_6);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_0);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_7);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_1);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_5);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_3);
    aa[0] = _mm512_sub_epi32(_mm512_sub_epi32(aa[0], aa[1]),
                             _mm512_sub_epi32(aa[2], aa[3]));
    aa[4] = _mm512_sub_epi32(_mm512_add_epi32(aa[4], aa[5]),
                             _mm512_add_epi32(aa[6], aa[7]));
    a[9] = _mm512_sub_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_5);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_0);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_4);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_6);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_1);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_3);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_7);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_2);
    aa[0] = _mm512_add_epi32(_mm512_sub_epi32(aa[0], aa[1]),
                             _mm512_add_epi32(aa[2], aa[3]));
    aa[4] = _mm512_sub_epi32(_mm512_sub_epi32(aa[4], aa[5]),
                             _mm512_sub_epi32(aa[6], aa[7]));
    a[11] = _mm512_sub_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_6);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_3);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_0);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_2);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_5);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_7);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_4);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_1);
    aa[0] = _mm512_add_epi32(_mm512_sub_epi32(aa[0], aa[1]),
                             _mm512_sub_epi32(aa[2], aa[3]));
    aa[4] = _mm512_sub_epi32(_mm512_add_epi32(aa[4], aa[5]),
                             _mm512_sub_epi32(aa[6], aa[7]));
    a[13] = _mm512_add_epi32(aa[0], aa[4]);

    aa[0] = _mm512_mullo_epi32(b[0], txmat_1_7);
    aa[1] = _mm512_mullo_epi32(b[1], txmat_1_6);
    aa[2] = _mm512_mullo_epi32(b[2], txmat_1_5);
    aa[3] = _mm512_mullo_epi32(b[3], txmat_1_4);
    aa[4] = _mm512_mullo_epi32(b[4], txmat_1_3);
    aa[5] = _mm512_mullo_epi32(b[5], txmat_1_2);
    aa[6] = _mm512_mullo_epi32(b[6], txmat_1_1);
    aa[7] = _mm512_mullo_epi32(b[7], txmat_1_0);
    aa[0] = _mm512_add_epi32(_mm512_sub_epi32(aa[0], aa[1]),
                             _mm512_sub_epi32(aa[2], aa[3]));
    aa[4] = _mm512_add_epi32(_mm512_sub_epi32(aa[4], aa[5]),
                             _mm512_sub_epi32(aa[6], aa[7]));
    a[15] = _mm512_add_epi32(aa[0], aa[4]);

    for (k = 0; k < 16; k++) {
      a[k] = _mm512_add_epi32(a[k], v_offset);
      a[k] = _mm512_srai_epi32(a[k], shift);
    }

    transpose_store_16x16_avx512(a, dst, tx1d_size);

    src += 16;
    dst += tx1d_size * 16;
  }
}

static void fwd_txfm_dct2_size32_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert(nz_line >= 16 && (nz_line & 15) == 0);

  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int *tx_mat = tx_kernel_dct2_size32[FWD_TXFM][0];
  const int tx1d_size = 32;

  __m512i a[32], b[16];
  __m512i c[8], d[8];
  __m512i e[4], f[4];
  __m512i g[2], h[2];

  __m512i v_offset = _mm512_set1_epi32(offset);

  __m512i txmat_8_0 = _mm512_set1_epi32(tx_mat[8 * 32 + 0]);  // 83
  __m512i txmat_8_1 = _mm512_set1_epi32(tx_mat[8 * 32 + 1]);  // 35

  __m512i txmat_4_0 = _mm512_set1_epi32(tx_mat[4 * 32 + 0]);  // 89
  __m512i txmat_4_1 = _mm512_set1_epi32(tx_mat[4 * 32 + 1]);  // 75
  __m512i txmat_4_2 = _mm512_set1_epi32(tx_mat[4 * 32 + 2]);  // 50
  __m512i txmat_4_3 = _mm512_set1_epi32(tx_mat[4 * 32 + 3]);  // 18

  __m512i txmat_2_0 = _mm512_set1_epi32(tx_mat[2 * 32 + 0]);  // 90
  __m512i txmat_2_1 = _mm512_set1_epi32(tx_mat[2 * 32 + 1]);  // 87
  __m512i txmat_2_2 = _mm512_set1_epi32(tx_mat[2 * 32 + 2]);  // 80
  __m512i txmat_2_3 = _mm512_set1_epi32(tx_mat[2 * 32 + 3]);  // 70
  __m512i txmat_2_4 = _mm512_set1_epi32(tx_mat[2 * 32 + 4]);  // 57
  __m512i txmat_2_5 = _mm512_set1_epi32(tx_mat[2 * 32 + 5]);  // 43
  __m512i txmat_2_6 = _mm512_set1_epi32(tx_mat[2 * 32 + 6]);  // 26
  __m512i txmat_2_7 = _mm512_set1_epi32(tx_mat[2 * 32 + 7]);  //  9

  __m512i txmat_1_0 = _mm512_set1_epi32(tx_mat[1 * 32 + 0]);    // 90
  __m512i txmat_1_1 = _mm512_set1_epi32(tx_mat[1 * 32 + 1]);    // 90
  __m512i txmat_1_2 = _mm512_set1_epi32(tx_mat[1 * 32 + 2]);    // 88
  __m512i txmat_1_3 = _mm512_set1_epi32(tx_mat[1 * 32 + 3]);    // 85
  __m512i txmat_1_4 = _mm512_set1_epi32(tx_mat[1 * 32 + 4]);    // 82
  __m512i txmat_1_5 = _mm512_set1_epi32(tx_mat[1 * 32 + 5]);    // 78
  __m512i txmat_1_6 = _mm512_set1_epi32(tx_mat[1 * 32 + 6]);    // 73
  __m512i txmat_1_7 = _mm512_set1_epi32(tx_mat[1 * 32 + 7]);    // 67
  __m512i txmat_1_8 = _mm512_set1_epi32(tx_mat[1 * 32 + 8]);    // 61
  __m512i txmat_1_9 = _mm512_set1_epi32(tx_mat[1 * 32 + 9]);    // 54
  __m512i txmat_1_10 = _mm512_set1_epi32(tx_mat[1 * 32 + 10]);  // 47
  __m512i txmat_1_11 = _mm512_set1_epi32(tx_mat[1 * 32 + 11]);  // 39
  __m512i txmat_1_12 = _mm512_set1_epi32(tx_mat[1 * 32 + 12]);  // 30
  __m512i txmat_1_13 = _mm512_set1_epi32(tx_mat[1 * 32 + 13]);  // 22
  __m512i txmat_1_14 = _mm512_set1_epi32(tx_mat[1 * 32 + 14]);  // 13
  __m512i txmat_1_15 = _mm512_set1_epi32(tx_mat[1 * 32 + 15]);  //  4

  for (j = 0; j < nz_line; j += 16) {
    for (k = 0; k < 16; k++) {
      __m512i src0 = _mm512_loadu_si512((const __m512i *)&src[k * line]);
      __m512i src1 =
          _mm512_loadu_si512((const __m512i *)&src[(tx1d_size - 1 - k) * line]);
      a[k] = _mm512_add_epi32(src0, src1);
      b[k] = _mm512_sub_epi32(src0, src1);
    }
    for (k = 0; k < 8; k++) {
      c[k] = _mm512_add_epi32(a[k], a[15 - k]);
      d[k] = _mm512_sub_epi32(a[k], a[15 - k]);
    }
    for (k = 0; k < 4; k++) {
      e[k] = _mm512_add_epi32(c[k], c[7 - k]);
      f[k] = _mm512_sub_epi32(c[k], c[7 - k]);
    }
    g[0] = _mm512_add_epi32(e[0], e[3]);
    h[0] = _mm512_sub_epi32(e[0], e[3]);
    g[1] = _mm512_add_epi32(e[1], e[2]);
    h[1] = _mm512_sub_epi32(e[1], e[2]);

    a[0] = _mm512_add_epi32(g[0], g[1]);
    a[0] = _mm512_slli_epi32(a[0], 6);

    a[16] = _mm512_sub_epi32(g[0], g[1]);
    a[16] = _mm512_slli_epi32(a[16], 6);

    a[8] = _mm512_add_epi32(_mm512_mullo_epi32(h[0], txmat_8_0),
                            _mm512_mullo_epi32(h[1], txmat_8_1));

    a[24] = _mm512_sub_epi32(_mm512_mullo_epi32(h[0], txmat_8_1),
                             _mm512_mullo_epi32(h[1], txmat_8_0));

    e[0] = _mm512_mullo_epi32(f[0], txmat_4_0);
    e[1] = _mm512_mullo_epi32(f[1], txmat_4_1);
    e[2] = _mm512_mullo_epi32(f[2], txmat_4_2);
    e[3] = _mm512_mullo_epi32(f[3], txmat_4_3);
    a[4] = _mm512_add_epi32(_mm512_add_epi32(e[0], e[1]),
                            _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(f[0], txmat_4_1);
    e[1] = _mm512_mullo_epi32(f[1], txmat_4_3);
    e[2] = _mm512_mullo_epi32(f[2], txmat_4_0);
    e[3] = _mm512_mullo_epi32(f[3], txmat_4_2);
    a[12] = _mm512_sub_epi32(_mm512_sub_epi32(e[0], e[1]),
                             _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(f[0], txmat_4_2);
    e[1] = _mm512_mullo_epi32(f[1], txmat_4_0);
    e[2] = _mm512_mullo_epi32(f[2], txmat_4_3);
    e[3] = _mm512_mullo_epi32(f[3], txmat_4_1);
    a[20] = _mm512_add_epi32(_mm512_sub_epi32(e[0], e[1]),
                             _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(f[0], txmat_4_3);
    e[1] = _mm512_mullo_epi32(f[1], txmat_4_2);
    e[2] = _mm512_mullo_epi32(f[2], txmat_4_1);
    e[3] = _mm512_mullo_epi32(f[3], txmat_4_0);
    a[28] = _mm512_add_epi32(_mm512_sub_epi32(e[0], e[1]),
                             _mm512_sub_epi32(e[2], e[3]));

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_0);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_1);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_2);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_3);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_4);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_5);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_6);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_7);
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[2] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_1);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_4);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_7);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_5);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_2);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_0);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_3);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_6);
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[6] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_2);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_7);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_3);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_1);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_6);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_4);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_0);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_5);
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[10] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_3);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_5);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_1);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_7);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_0);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_6);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_2);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_4);
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[14] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_4);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_2);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_6);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_0);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_7);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_1);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_5);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_3);
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[18] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_5);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_0);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_4);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_6);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_1);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_3);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_7);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_2);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[22] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_6);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_3);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_0);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_2);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_5);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_7);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_4);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_1);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[26] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(d[0], txmat_2_7);
    c[1] = _mm512_mullo_epi32(d[1], txmat_2_6);
    c[2] = _mm512_mullo_epi32(d[2], txmat_2_5);
    c[3] = _mm512_mullo_epi32(d[3], txmat_2_4);
    c[4] = _mm512_mullo_epi32(d[4], txmat_2_3);
    c[5] = _mm512_mullo_epi32(d[5], txmat_2_2);
    c[6] = _mm512_mullo_epi32(d[6], txmat_2_1);
    c[7] = _mm512_mullo_epi32(d[7], txmat_2_0);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[30] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(b[0], txmat_1_0),
                            _mm512_mullo_epi32(b[1], txmat_1_1));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_2),
                            _mm512_mullo_epi32(b[3], txmat_1_3));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_4),
                            _mm512_mullo_epi32(b[5], txmat_1_5));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_6),
                            _mm512_mullo_epi32(b[7], txmat_1_7));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_8),
                            _mm512_mullo_epi32(b[9], txmat_1_9));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_10),
                            _mm512_mullo_epi32(b[11], txmat_1_11));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_12),
                            _mm512_mullo_epi32(b[13], txmat_1_13));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_14),
                            _mm512_mullo_epi32(b[15], txmat_1_15));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[1] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(b[0], txmat_1_1),
                            _mm512_mullo_epi32(b[1], txmat_1_4));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_7),
                            _mm512_mullo_epi32(b[3], txmat_1_10));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_13),
                            _mm512_mullo_epi32(b[5], txmat_1_15));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_12),
                            _mm512_mullo_epi32(b[7], txmat_1_9));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_6),
                            _mm512_mullo_epi32(b[9], txmat_1_3));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_0),
                            _mm512_mullo_epi32(b[11], txmat_1_2));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_5),
                            _mm512_mullo_epi32(b[13], txmat_1_8));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_11),
                            _mm512_mullo_epi32(b[15], txmat_1_14));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[3] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(b[0], txmat_1_2),
                            _mm512_mullo_epi32(b[1], txmat_1_7));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_12),
                            _mm512_mullo_epi32(b[3], txmat_1_14));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_9),
                            _mm512_mullo_epi32(b[5], txmat_1_4));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_0),
                            _mm512_mullo_epi32(b[7], txmat_1_5));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_10),
                            _mm512_mullo_epi32(b[9], txmat_1_15));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_11),
                            _mm512_mullo_epi32(b[11], txmat_1_6));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_1),
                            _mm512_mullo_epi32(b[13], txmat_1_3));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_8),
                            _mm512_mullo_epi32(b[15], txmat_1_13));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[5] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(b[0], txmat_1_3),
                            _mm512_mullo_epi32(b[1], txmat_1_10));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_14),
                            _mm512_mullo_epi32(b[3], txmat_1_7));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_0),
                            _mm512_mullo_epi32(b[5], txmat_1_6));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_13),
                            _mm512_mullo_epi32(b[7], txmat_1_11));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_4),
                            _mm512_mullo_epi32(b[9], txmat_1_2));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_9),
                            _mm512_mullo_epi32(b[11], txmat_1_15));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_8),
                            _mm512_mullo_epi32(b[13], txmat_1_1));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_5),
                            _mm512_mullo_epi32(b[15], txmat_1_12));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[7] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(b[0], txmat_1_4),
                            _mm512_mullo_epi32(b[1], txmat_1_13));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_9),
                            _mm512_mullo_epi32(b[3], txmat_1_0));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_8),
                            _mm512_mullo_epi32(b[5], txmat_1_14));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_5),
                            _mm512_mullo_epi32(b[7], txmat_1_3));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_12),
                            _mm512_mullo_epi32(b[9], txmat_1_10));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_1),
                            _mm512_mullo_epi32(b[11], txmat_1_7));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_15),
                            _mm512_mullo_epi32(b[13], txmat_1_6));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_2),
                            _mm512_mullo_epi32(b[15], txmat_1_11));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[9] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_5),
                            _mm512_mullo_epi32(b[1], txmat_1_15));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_4),
                            _mm512_mullo_epi32(b[3], txmat_1_6));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_14),
                            _mm512_mullo_epi32(b[5], txmat_1_3));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_7),
                            _mm512_mullo_epi32(b[7], txmat_1_13));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_2),
                            _mm512_mullo_epi32(b[9], txmat_1_8));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_12),
                            _mm512_mullo_epi32(b[11], txmat_1_1));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_9),
                            _mm512_mullo_epi32(b[13], txmat_1_11));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_0),
                            _mm512_mullo_epi32(b[15], txmat_1_10));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[11] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_6),
                            _mm512_mullo_epi32(b[1], txmat_1_12));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_0),
                            _mm512_mullo_epi32(b[3], txmat_1_13));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_5),
                            _mm512_mullo_epi32(b[5], txmat_1_7));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_11),
                            _mm512_mullo_epi32(b[7], txmat_1_1));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_14),
                            _mm512_mullo_epi32(b[9], txmat_1_4));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_8),
                            _mm512_mullo_epi32(b[11], txmat_1_10));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_2),
                            _mm512_mullo_epi32(b[13], txmat_1_15));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_3),
                            _mm512_mullo_epi32(b[15], txmat_1_9));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[13] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_7),
                            _mm512_mullo_epi32(b[1], txmat_1_9));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_5),
                            _mm512_mullo_epi32(b[3], txmat_1_11));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_3),
                            _mm512_mullo_epi32(b[5], txmat_1_13));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_1),
                            _mm512_mullo_epi32(b[7], txmat_1_15));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_0),
                            _mm512_mullo_epi32(b[9], txmat_1_14));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_2),
                            _mm512_mullo_epi32(b[11], txmat_1_12));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_4),
                            _mm512_mullo_epi32(b[13], txmat_1_10));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_6),
                            _mm512_mullo_epi32(b[15], txmat_1_8));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[15] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_8),
                            _mm512_mullo_epi32(b[1], txmat_1_6));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_10),
                            _mm512_mullo_epi32(b[3], txmat_1_4));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_12),
                            _mm512_mullo_epi32(b[5], txmat_1_2));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_14),
                            _mm512_mullo_epi32(b[7], txmat_1_0));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_15),
                            _mm512_mullo_epi32(b[9], txmat_1_1));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_13),
                            _mm512_mullo_epi32(b[11], txmat_1_3));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_11),
                            _mm512_mullo_epi32(b[13], txmat_1_5));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_9),
                            _mm512_mullo_epi32(b[15], txmat_1_7));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[17] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_9),
                            _mm512_mullo_epi32(b[1], txmat_1_3));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_15),
                            _mm512_mullo_epi32(b[3], txmat_1_2));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_10),
                            _mm512_mullo_epi32(b[5], txmat_1_8));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_4),
                            _mm512_mullo_epi32(b[7], txmat_1_14));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_1),
                            _mm512_mullo_epi32(b[9], txmat_1_11));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_7),
                            _mm512_mullo_epi32(b[11], txmat_1_5));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_13),
                            _mm512_mullo_epi32(b[13], txmat_1_0));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_12),
                            _mm512_mullo_epi32(b[15], txmat_1_6));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[19] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_10),
                            _mm512_mullo_epi32(b[1], txmat_1_0));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(b[2], txmat_1_11),
                            _mm512_mullo_epi32(b[3], txmat_1_9));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_1),
                            _mm512_mullo_epi32(b[5], txmat_1_12));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_8),
                            _mm512_mullo_epi32(b[7], txmat_1_2));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_13),
                            _mm512_mullo_epi32(b[9], txmat_1_7));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_3),
                            _mm512_mullo_epi32(b[11], txmat_1_14));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_6),
                            _mm512_mullo_epi32(b[13], txmat_1_4));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(b[14], txmat_1_15),
                            _mm512_mullo_epi32(b[15], txmat_1_5));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[21] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_11),
                            _mm512_mullo_epi32(b[1], txmat_1_2));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_6),
                            _mm512_mullo_epi32(b[3], txmat_1_15));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_7),
                            _mm512_mullo_epi32(b[5], txmat_1_1));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(b[6], txmat_1_10),
                            _mm512_mullo_epi32(b[7], txmat_1_12));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_3),
                            _mm512_mullo_epi32(b[9], txmat_1_5));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_14),
                            _mm512_mullo_epi32(b[11], txmat_1_8));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_0),
                            _mm512_mullo_epi32(b[13], txmat_1_9));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(b[14], txmat_1_13),
                            _mm512_mullo_epi32(b[15], txmat_1_4));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[23] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_12),
                            _mm512_mullo_epi32(b[1], txmat_1_5));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_1),
                            _mm512_mullo_epi32(b[3], txmat_1_8));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(b[4], txmat_1_15),
                            _mm512_mullo_epi32(b[5], txmat_1_9));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_2),
                            _mm512_mullo_epi32(b[7], txmat_1_4));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(b[8], txmat_1_11),
                            _mm512_mullo_epi32(b[9], txmat_1_13));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_6),
                            _mm512_mullo_epi32(b[11], txmat_1_0));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_7),
                            _mm512_mullo_epi32(b[13], txmat_1_14));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(b[14], txmat_1_10),
                            _mm512_mullo_epi32(b[15], txmat_1_3));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[25] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_13),
                            _mm512_mullo_epi32(b[1], txmat_1_8));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_3),
                            _mm512_mullo_epi32(b[3], txmat_1_1));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_6),
                            _mm512_mullo_epi32(b[5], txmat_1_11));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_15),
                            _mm512_mullo_epi32(b[7], txmat_1_10));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_5),
                            _mm512_mullo_epi32(b[9], txmat_1_0));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_4),
                            _mm512_mullo_epi32(b[11], txmat_1_9));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(b[12], txmat_1_14),
                            _mm512_mullo_epi32(b[13], txmat_1_12));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(b[14], txmat_1_7),
                            _mm512_mullo_epi32(b[15], txmat_1_2));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    a[27] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_14),
                            _mm512_mullo_epi32(b[1], txmat_1_11));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_8),
                            _mm512_mullo_epi32(b[3], txmat_1_5));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_2),
                            _mm512_mullo_epi32(b[5], txmat_1_0));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_3),
                            _mm512_mullo_epi32(b[7], txmat_1_6));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_9),
                            _mm512_mullo_epi32(b[9], txmat_1_12));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(b[10], txmat_1_15),
                            _mm512_mullo_epi32(b[11], txmat_1_13));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_10),
                            _mm512_mullo_epi32(b[13], txmat_1_7));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(b[14], txmat_1_4),
                            _mm512_mullo_epi32(b[15], txmat_1_1));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[29] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(b[0], txmat_1_15),
                            _mm512_mullo_epi32(b[1], txmat_1_14));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(b[2], txmat_1_13),
                            _mm512_mullo_epi32(b[3], txmat_1_12));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(b[4], txmat_1_11),
                            _mm512_mullo_epi32(b[5], txmat_1_10));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(b[6], txmat_1_9),
                            _mm512_mullo_epi32(b[7], txmat_1_8));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(b[8], txmat_1_7),
                            _mm512_mullo_epi32(b[9], txmat_1_6));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(b[10], txmat_1_5),
                            _mm512_mullo_epi32(b[11], txmat_1_4));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(b[12], txmat_1_3),
                            _mm512_mullo_epi32(b[13], txmat_1_2));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(b[14], txmat_1_1),
                            _mm512_mullo_epi32(b[15], txmat_1_0));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    a[31] = _mm512_add_epi32(c[0], c[4]);

    for (k = 0; k < 32; k++) {
      a[k] = _mm512_add_epi32(a[k], v_offset);
      a[k] = _mm512_srai_epi32(a[k], shift);
    }

    transpose_store_16x16_avx512(a, dst + 0, tx1d_size);
    transpose_store_16x16_avx512(a + 16, dst + 16, tx1d_size);

    src += 16;
    dst += tx1d_size * 16;
  }
}

// Weights of the 8-line DCT2 kernels, from tx_kernel_dct2_size{32,16}
// [FWD_TXFM]. Row k holds the weight of input k for one output in lanes 0-7
// and for the output half a transform size above it in lanes 8-15.

// Output pairs (j, j + 16) for j = 1, 3, 5, 7, 9, 11, 13, 15, from b[0..15].
DECLARE_ALIGNED(64, static const int, dct32_line8_b[8][16][16]) = {
  {
      { 90, 90, 90, 90, 90, 90, 90, 90, 61, 61, 61, 61, 61, 61, 61, 61 },
      { 90, 90, 90, 90, 90, 90, 90, 90, -73, -73, -73, -73, -73, -73, -73,
        -73 },
      { 88, 88, 88, 88, 88, 88, 88, 88, -47, -47, -47, -47, -47, -47, -47,
        -47 },
      { 85, 85, 85, 85, 85, 85, 85, 85, 82, 82, 82, 82, 82, 82, 82, 82 },
      { 82, 82, 82, 82, 82, 82, 82, 82, 30, 30, 30, 30, 30, 30, 30, 30 },
      { 78, 78, 78, 78, 78, 78, 78, 78, -88, -88, -88, -88, -88, -88, -88,
        -88 },
      { 73, 73, 73, 73, 73, 73, 73, 73, -13, -13, -13, -13, -13, -13, -13,
        -13 },
      { 67, 67, 67, 67, 67, 67, 67, 67, 90, 90, 90, 90, 90, 90, 90, 90 },
      { 61, 61, 61, 61, 61, 61, 61, 61, -4, -4, -4, -4, -4, -4, -4, -4 },
      { 54, 54, 54, 54, 54, 54, 54, 54, -90, -90, -90, -90, -90, -90, -90,
        -90 },
      { 47, 47, 47, 47, 47, 47, 47, 47, 22, 22, 22, 22, 22, 22, 22, 22 },
      { 39, 39, 39, 39, 39, 39, 39, 39, 85, 85, 85, 85, 85, 85, 85, 85 },
      { 30, 30, 30, 30, 30, 30, 30, 30, -39, -39, -39, -39, -39, -39, -39,
        -39 },
      { 22, 22, 22, 22, 22, 22, 22, 22, -78, -78, -78, -78, -78, -78, -78,
        -78 },
      { 13, 13, 13, 13, 13, 13, 13, 13, 54, 54, 54, 54, 54, 54, 54, 54 },
      { 4, 4, 4, 4, 4, 4, 4, 4, 67, 67, 67, 67, 67, 67, 67, 67 },
  },
  {
      { 90, 90, 90, 90, 90, 90, 90, 90, 54, 54, 54, 54, 54, 54, 54, 54 },
      { 82, 82, 82, 82, 82, 82, 82, 82, -85, -85, -85, -85, -85, -85, -85,
        -85 },
      { 67, 67, 67, 67, 67, 67, 67, 67, -4, -4, -4, -4, -4, -4, -4, -4 },
      { 47, 47, 47, 47, 47, 47, 47, 47, 88, 88, 88, 88, 88, 88, 88, 88 },
      { 22, 22, 22, 22, 22, 22, 22, 22, -47, -47, -47, -47, -47, -47, -47,
        -47 },
      { -4, -4, -4, -4, -4, -4, -4, -4, -61, -61, -61, -61, -61, -61, -61,
        -61 },
      { -30, -30, -30, -30, -30, -30, -30, -30, 82, 82, 82, 82, 82, 82, 82,
        82 },
      { -54, -54, -54, -54, -54, -54, -54, -54, 13, 13, 13, 13, 13, 13, 13,
        13 },
      { -73, -73, -73, -73, -73, -73, -73, -73, -90, -90, -90, -90, -90, -90,
        -90, -90 },
      { -85, -85, -85, -85, -85, -85, -85, -85, 39, 39, 39, 39, 39, 39, 39,
        39 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 67, 67, 67, 67, 67, 67, 67,
        67 },
      { -88, -88, -88, -88, -88, -88, -88, -88, -78, -78, -78, -78, -78, -78,
        -78, -78 },
      { -78, -78, -78, -78, -78, -78, -78, -78, -22, -22, -22, -22, -22, -22,
        -22, -22 },
      { -61, -61, -61, -61, -61, -61, -61, -61, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -39, -39, -39, -39, -39, -39, -39, -39, -30, -30, -30, -30, -30, -30,
        -30, -30 },
      { -13, -13, -13, -13, -13, -13, -13, -13, -73, -73, -73, -73, -73, -73,
        -73, -73 },
  },
  {
      { 88, 88, 88, 88, 88, 88, 88, 88, 47, 47, 47, 47, 47, 47, 47, 47 },
      { 67, 67, 67, 67, 67, 67, 67, 67, -90, -90, -90, -90, -90, -90, -90,
        -90 },
      { 30, 30, 30, 30, 30, 30, 30, 30, 39, 39, 39, 39, 39, 39, 39, 39 },
      { -13, -13, -13, -13, -13, -13, -13, -13, 54, 54, 54, 54, 54, 54, 54,
        54 },
      { -54, -54, -54, -54, -54, -54, -54, -54, -90, -90, -90, -90, -90, -90,
        -90, -90 },
      { -82, -82, -82, -82, -82, -82, -82, -82, 30, 30, 30, 30, 30, 30, 30,
        30 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 61, 61, 61, 61, 61, 61, 61,
        61 },
      { -78, -78, -78, -78, -78, -78, -78, -78, -88, -88, -88, -88, -88, -88,
        -88, -88 },
      { -47, -47, -47, -47, -47, -47, -47, -47, 22, 22, 22, 22, 22, 22, 22,
        22 },
      { -4, -4, -4, -4, -4, -4, -4, -4, 67, 67, 67, 67, 67, 67, 67, 67 },
      { 39, 39, 39, 39, 39, 39, 39, 39, -85, -85, -85, -85, -85, -85, -85,
        -85 },
      { 73, 73, 73, 73, 73, 73, 73, 73, 13, 13, 13, 13, 13, 13, 13, 13 },
      { 90, 90, 90, 90, 90, 90, 90, 90, 73, 73, 73, 73, 73, 73, 73, 73 },
      { 85, 85, 85, 85, 85, 85, 85, 85, -82, -82, -82, -82, -82, -82, -82,
        -82 },
      { 61, 61, 61, 61, 61, 61, 61, 61, 4, 4, 4, 4, 4, 4, 4, 4 },
      { 22, 22, 22, 22, 22, 22, 22, 22, 78, 78, 78, 78, 78, 78, 78, 78 },
  },
  {
      { 85, 85, 85, 85, 85, 85, 85, 85, 39, 39, 39, 39, 39, 39, 39, 39 },
      { 47, 47, 47, 47, 47, 47, 47, 47, -88, -88, -88, -88, -88, -88, -88,
        -88 },
      { -13, -13, -13, -13, -13, -13, -13, -13, 73, 73, 73, 73, 73, 73, 73,
        73 },
      { -67, -67, -67, -67, -67, -67, -67, -67, -4, -4, -4, -4, -4, -4, -4,
        -4 },
      { -90, -90, -90, -90, -90, -90, -90, -90, -67, -67, -67, -67, -67, -67,
        -67, -67 },
      { -73, -73, -73, -73, -73, -73, -73, -73, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -22, -22, -22, -22, -22, -22, -22, -22, -47, -47, -47, -47, -47, -47,
        -47, -47 },
      { 39, 39, 39, 39, 39, 39, 39, 39, -30, -30, -30, -30, -30, -30, -30,
        -30 },
      { 82, 82, 82, 82, 82, 82, 82, 82, 85, 85, 85, 85, 85, 85, 85, 85 },
      { 88, 88, 88, 88, 88, 88, 88, 88, -78, -78, -78, -78, -78, -78, -78,
        -78 },
      { 54, 54, 54, 54, 54, 54, 54, 54, 13, 13, 13, 13, 13, 13, 13, 13 },
      { -4, -4, -4, -4, -4, -4, -4, -4, 61, 61, 61, 61, 61, 61, 61, 61 },
      { -61, -61, -61, -61, -61, -61, -61, -61, -90, -90, -90, -90, -90, -90,
        -90, -90 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 54, 54, 54, 54, 54, 54, 54,
        54 },
      { -78, -78, -78, -78, -78, -78, -78, -78, 22, 22, 22, 22, 22, 22, 22,
        22 },
      { -30, -30, -30, -30, -30, -30, -30, -30, -82, -82, -82, -82, -82, -82,
        -82, -82 },
  },
  {
      { 82, 82, 82, 82, 82, 82, 82, 82, 30, 30, 30, 30, 30, 30, 30, 30 },
      { 22, 22, 22, 22, 22, 22, 22, 22, -78, -78, -78, -78, -78, -78, -78,
        -78 },
      { -54, -54, -54, -54, -54, -54, -54, -54, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -90, -90, -90, -90, -90, -90, -90, -90, -61, -61, -61, -61, -61, -61,
        -61, -61 },
      { -61, -61, -61, -61, -61, -61, -61, -61, 4, 4, 4, 4, 4, 4, 4, 4 },
      { 13, 13, 13, 13, 13, 13, 13, 13, 54, 54, 54, 54, 54, 54, 54, 54 },
      { 78, 78, 78, 78, 78, 78, 78, 78, -88, -88, -88, -88, -88, -88, -88,
        -88 },
      { 85, 85, 85, 85, 85, 85, 85, 85, 82, 82, 82, 82, 82, 82, 82, 82 },
      { 30, 30, 30, 30, 30, 30, 30, 30, -39, -39, -39, -39, -39, -39, -39,
        -39 },
      { -47, -47, -47, -47, -47, -47, -47, -47, -22, -22, -22, -22, -22, -22,
        -22, -22 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 73, 73, 73, 73, 73, 73, 73,
        73 },
      { -67, -67, -67, -67, -67, -67, -67, -67, -90, -90, -90, -90, -90, -90,
        -90, -90 },
      { 4, 4, 4, 4, 4, 4, 4, 4, 67, 67, 67, 67, 67, 67, 67, 67 },
      { 73, 73, 73, 73, 73, 73, 73, 73, -13, -13, -13, -13, -13, -13, -13,
        -13 },
      { 88, 88, 88, 88, 88, 88, 88, 88, -47, -47, -47, -47, -47, -47, -47,
        -47 },
      { 39, 39, 39, 39, 39, 39, 39, 39, 85, 85, 85, 85, 85, 85, 85, 85 },
  },
  {
      { 78, 78, 78, 78, 78, 78, 78, 78, 22, 22, 22, 22, 22, 22, 22, 22 },
      { -4, -4, -4, -4, -4, -4, -4, -4, -61, -61, -61, -61, -61, -61, -61,
        -61 },
      { -82, -82, -82, -82, -82, -82, -82, -82, 85, 85, 85, 85, 85, 85, 85,
        85 },
      { -73, -73, -73, -73, -73, -73, -73, -73, -90, -90, -90, -90, -90, -90,
        -90, -90 },
      { 13, 13, 13, 13, 13, 13, 13, 13, 73, 73, 73, 73, 73, 73, 73, 73 },
      { 85, 85, 85, 85, 85, 85, 85, 85, -39, -39, -39, -39, -39, -39, -39,
        -39 },
      { 67, 67, 67, 67, 67, 67, 67, 67, -4, -4, -4, -4, -4, -4, -4, -4 },
      { -22, -22, -22, -22, -22, -22, -22, -22, 47, 47, 47, 47, 47, 47, 47,
        47 },
      { -88, -88, -88, -88, -88, -88, -88, -88, -78, -78, -78, -78, -78, -78,
        -78, -78 },
      { -61, -61, -61, -61, -61, -61, -61, -61, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { 30, 30, 30, 30, 30, 30, 30, 30, -82, -82, -82, -82, -82, -82, -82,
        -82 },
      { 90, 90, 90, 90, 90, 90, 90, 90, 54, 54, 54, 54, 54, 54, 54, 54 },
      { 54, 54, 54, 54, 54, 54, 54, 54, -13, -13, -13, -13, -13, -13, -13,
        -13 },
      { -39, -39, -39, -39, -39, -39, -39, -39, -30, -30, -30, -30, -30, -30,
        -30, -30 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 67, 67, 67, 67, 67, 67, 67,
        67 },
      { -47, -47, -47, -47, -47, -47, -47, -47, -88, -88, -88, -88, -88, -88,
        -88, -88 },
  },
  {
      { 73, 73, 73, 73, 73, 73, 73, 73, 13, 13, 13, 13, 13, 13, 13, 13 },
      { -30, -30, -30, -30, -30, -30, -30, -30, -39, -39, -39, -39, -39, -39,
        -39, -39 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 61, 61, 61, 61, 61, 61, 61,
        61 },
      { -22, -22, -22, -22, -22, -22, -22, -22, -78, -78, -78, -78, -78, -78,
        -78, -78 },
      { 78, 78, 78, 78, 78, 78, 78, 78, 88, 88, 88, 88, 88, 88, 88, 88 },
      { 67, 67, 67, 67, 67, 67, 67, 67, -90, -90, -90, -90, -90, -90, -90,
        -90 },
      { -39, -39, -39, -39, -39, -39, -39, -39, 85, 85, 85, 85, 85, 85, 85,
        85 },
      { -90, -90, -90, -90, -90, -90, -90, -90, -73, -73, -73, -73, -73, -73,
        -73, -73 },
      { -13, -13, -13, -13, -13, -13, -13, -13, 54, 54, 54, 54, 54, 54, 54,
        54 },
      { 82, 82, 82, 82, 82, 82, 82, 82, -30, -30, -30, -30, -30, -30, -30,
        -30 },
      { 61, 61, 61, 61, 61, 61, 61, 61, 4, 4, 4, 4, 4, 4, 4, 4 },
      { -47, -47, -47, -47, -47, -47, -47, -47, 22, 22, 22, 22, 22, 22, 22,
        22 },
      { -88, -88, -88, -88, -88, -88, -88, -88, -47, -47, -47, -47, -47, -47,
        -47, -47 },
      { -4, -4, -4, -4, -4, -4, -4, -4, 67, 67, 67, 67, 67, 67, 67, 67 },
      { 85, 85, 85, 85, 85, 85, 85, 85, -82, -82, -82, -82, -82, -82, -82,
        -82 },
      { 54, 54, 54, 54, 54, 54, 54, 54, 90, 90, 90, 90, 90, 90, 90, 90 },
  },
  {
      { 67, 67, 67, 67, 67, 67, 67, 67, 4, 4, 4, 4, 4, 4, 4, 4 },
      { -54, -54, -54, -54, -54, -54, -54, -54, -13, -13, -13, -13, -13, -13,
        -13, -13 },
      { -78, -78, -78, -78, -78, -78, -78, -78, 22, 22, 22, 22, 22, 22, 22,
        22 },
      { 39, 39, 39, 39, 39, 39, 39, 39, -30, -30, -30, -30, -30, -30, -30,
        -30 },
      { 85, 85, 85, 85, 85, 85, 85, 85, 39, 39, 39, 39, 39, 39, 39, 39 },
      { -22, -22, -22, -22, -22, -22, -22, -22, -47, -47, -47, -47, -47, -47,
        -47, -47 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 54, 54, 54, 54, 54, 54, 54,
        54 },
      { 4, 4, 4, 4, 4, 4, 4, 4, -61, -61, -61, -61, -61, -61, -61, -61 },
      { 90, 90, 90, 90, 90, 90, 90, 90, 67, 67, 67, 67, 67, 67, 67, 67 },
      { 13, 13, 13, 13, 13, 13, 13, 13, -73, -73, -73, -73, -73, -73, -73,
        -73 },
      { -88, -88, -88, -88, -88, -88, -88, -88, 78, 78, 78, 78, 78, 78, 78,
        78 },
      { -30, -30, -30, -30, -30, -30, -30, -30, -82, -82, -82, -82, -82, -82,
        -82, -82 },
      { 82, 82, 82, 82, 82, 82, 82, 82, 85, 85, 85, 85, 85, 85, 85, 85 },
      { 47, 47, 47, 47, 47, 47, 47, 47, -88, -88, -88, -88, -88, -88, -88,
        -88 },
      { -73, -73, -73, -73, -73, -73, -73, -73, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -61, -61, -61, -61, -61, -61, -61, -61, -90, -90, -90, -90, -90, -90,
        -90, -90 },
  },
};

// Output pairs (j, j + 16) for j = 2, 6, 10, 14, from d[0..7].
DECLARE_ALIGNED(64, static const int, dct32_line8_d[4][8][16]) = {
  {
      { 90, 90, 90, 90, 90, 90, 90, 90, 57, 57, 57, 57, 57, 57, 57, 57 },
      { 87, 87, 87, 87, 87, 87, 87, 87, -80, -80, -80, -80, -80, -80, -80,
        -80 },
      { 80, 80, 80, 80, 80, 80, 80, 80, -26, -26, -26, -26, -26, -26, -26,
        -26 },
      { 70, 70, 70, 70, 70, 70, 70, 70, 90, 90, 90, 90, 90, 90, 90, 90 },
      { 57, 57, 57, 57, 57, 57, 57, 57, -9, -9, -9, -9, -9, -9, -9, -9 },
      { 43, 43, 43, 43, 43, 43, 43, 43, -87, -87, -87, -87, -87, -87, -87,
        -87 },
      { 26, 26, 26, 26, 26, 26, 26, 26, 43, 43, 43, 43, 43, 43, 43, 43 },
      { 9, 9, 9, 9, 9, 9, 9, 9, 70, 70, 70, 70, 70, 70, 70, 70 },
  },
  {
      { 87, 87, 87, 87, 87, 87, 87, 87, 43, 43, 43, 43, 43, 43, 43, 43 },
      { 57, 57, 57, 57, 57, 57, 57, 57, -90, -90, -90, -90, -90, -90, -90,
        -90 },
      { 9, 9, 9, 9, 9, 9, 9, 9, 57, 57, 57, 57, 57, 57, 57, 57 },
      { -43, -43, -43, -43, -43, -43, -43, -43, 26, 26, 26, 26, 26, 26, 26,
        26 },
      { -80, -80, -80, -80, -80, -80, -80, -80, -87, -87, -87, -87, -87, -87,
        -87, -87 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 70, 70, 70, 70, 70, 70, 70,
        70 },
      { -70, -70, -70, -70, -70, -70, -70, -70, 9, 9, 9, 9, 9, 9, 9, 9 },
      { -26, -26, -26, -26, -26, -26, -26, -26, -80, -80, -80, -80, -80, -80,
        -80, -80 },
  },
  {
      { 80, 80, 80, 80, 80, 80, 80, 80, 26, 26, 26, 26, 26, 26, 26, 26 },
      { 9, 9, 9, 9, 9, 9, 9, 9, -70, -70, -70, -70, -70, -70, -70, -70 },
      { -70, -70, -70, -70, -70, -70, -70, -70, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -87, -87, -87, -87, -87, -87, -87, -87, -80, -80, -80, -80, -80, -80,
        -80, -80 },
      { -26, -26, -26, -26, -26, -26, -26, -26, 43, 43, 43, 43, 43, 43, 43,
        43 },
      { 57, 57, 57, 57, 57, 57, 57, 57, 9, 9, 9, 9, 9, 9, 9, 9 },
      { 90, 90, 90, 90, 90, 90, 90, 90, -57, -57, -57, -57, -57, -57, -57,
        -57 },
      { 43, 43, 43, 43, 43, 43, 43, 43, 87, 87, 87, 87, 87, 87, 87, 87 },
  },
  {
      { 70, 70, 70, 70, 70, 70, 70, 70, 9, 9, 9, 9, 9, 9, 9, 9 },
      { -43, -43, -43, -43, -43, -43, -43, -43, -26, -26, -26, -26, -26, -26,
        -26, -26 },
      { -87, -87, -87, -87, -87, -87, -87, -87, 43, 43, 43, 43, 43, 43, 43,
        43 },
      { 9, 9, 9, 9, 9, 9, 9, 9, -57, -57, -57, -57, -57, -57, -57, -57 },
      { 90, 90, 90, 90, 90, 90, 90, 90, 70, 70, 70, 70, 70, 70, 70, 70 },
      { 26, 26, 26, 26, 26, 26, 26, 26, -80, -80, -80, -80, -80, -80, -80,
        -80 },
      { -80, -80, -80, -80, -80, -80, -80, -80, 87, 87, 87, 87, 87, 87, 87,
        87 },
      { -57, -57, -57, -57, -57, -57, -57, -57, -90, -90, -90, -90, -90, -90,
        -90, -90 },
  },
};

// Output pairs (j, j + 16) for j = 4, 12, from f[0..3].
DECLARE_ALIGNED(64, static const int, dct32_line8_f[2][4][16]) = {
  {
      { 89, 89, 89, 89, 89, 89, 89, 89, 50, 50, 50, 50, 50, 50, 50, 50 },
      { 75, 75, 75, 75, 75, 75, 75, 75, -89, -89, -89, -89, -89, -89, -89,
        -89 },
      { 50, 50, 50, 50, 50, 50, 50, 50, 18, 18, 18, 18, 18, 18, 18, 18 },
      { 18, 18, 18, 18, 18, 18, 18, 18, 75, 75, 75, 75, 75, 75, 75, 75 },
  },
  {
      { 75, 75, 75, 75, 75, 75, 75, 75, 18, 18, 18, 18, 18, 18, 18, 18 },
      { -18, -18, -18, -18, -18, -18, -18, -18, -50, -50, -50, -50, -50, -50,
        -50, -50 },
      { -89, -89, -89, -89, -89, -89, -89, -89, 75, 75, 75, 75, 75, 75, 75,
        75 },
      { -50, -50, -50, -50, -50, -50, -50, -50, -89, -89, -89, -89, -89, -89,
        -89, -89 },
  },
};

// Output pairs (j, j + 16) for j = 8, from h[0..1].
DECLARE_ALIGNED(64, static const int, dct32_line8_h[1][2][16]) = {
  {
      { 83, 83, 83, 83, 83, 83, 83, 83, 35, 35, 35, 35, 35, 35, 35, 35 },
      { 35, 35, 35, 35, 35, 35, 35, 35, -83, -83, -83, -83, -83, -83, -83,
        -83 },
  },
};

// Output pairs (j, j + 16) for j = 0, from g[0..1].
DECLARE_ALIGNED(64, static const int, dct32_line8_g[1][2][16]) = {
  {
      { 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
      { 64, 64, 64, 64, 64, 64, 64, 64, -64, -64, -64, -64, -64, -64, -64,
        -64 },
  },
};

// Output pairs (j, j + 8) for j = 1, 3, 5, 7, from b[0..7].
DECLARE_ALIGNED(64, static const int, dct16_line8_b[4][8][16]) = {
  {
      { 90, 90, 90, 90, 90, 90, 90, 90, 57, 57, 57, 57, 57, 57, 57, 57 },
      { 87, 87, 87, 87, 87, 87, 87, 87, -80, -80, -80, -80, -80, -80, -80,
        -80 },
      { 80, 80, 80, 80, 80, 80, 80, 80, -26, -26, -26, -26, -26, -26, -26,
        -26 },
      { 70, 70, 70, 70, 70, 70, 70, 70, 90, 90, 90, 90, 90, 90, 90, 90 },
      { 57, 57, 57, 57, 57, 57, 57, 57, -9, -9, -9, -9, -9, -9, -9, -9 },
      { 43, 43, 43, 43, 43, 43, 43, 43, -87, -87, -87, -87, -87, -87, -87,
        -87 },
      { 26, 26, 26, 26, 26, 26, 26, 26, 43, 43, 43, 43, 43, 43, 43, 43 },
      { 9, 9, 9, 9, 9, 9, 9, 9, 70, 70, 70, 70, 70, 70, 70, 70 },
  },
  {
      { 87, 87, 87, 87, 87, 87, 87, 87, 43, 43, 43, 43, 43, 43, 43, 43 },
      { 57, 57, 57, 57, 57, 57, 57, 57, -90, -90, -90, -90, -90, -90, -90,
        -90 },
      { 9, 9, 9, 9, 9, 9, 9, 9, 57, 57, 57, 57, 57, 57, 57, 57 },
      { -43, -43, -43, -43, -43, -43, -43, -43, 26, 26, 26, 26, 26, 26, 26,
        26 },
      { -80, -80, -80, -80, -80, -80, -80, -80, -87, -87, -87, -87, -87, -87,
        -87, -87 },
      { -90, -90, -90, -90, -90, -90, -90, -90, 70, 70, 70, 70, 70, 70, 70,
        70 },
      { -70, -70, -70, -70, -70, -70, -70, -70, 9, 9, 9, 9, 9, 9, 9, 9 },
      { -26, -26, -26, -26, -26, -26, -26, -26, -80, -80, -80, -80, -80, -80,
        -80, -80 },
  },
  {
      { 80, 80, 80, 80, 80, 80, 80, 80, 26, 26, 26, 26, 26, 26, 26, 26 },
      { 9, 9, 9, 9, 9, 9, 9, 9, -70, -70, -70, -70, -70, -70, -70, -70 },
      { -70, -70, -70, -70, -70, -70, -70, -70, 90, 90, 90, 90, 90, 90, 90,
        90 },
      { -87, -87, -87, -87, -87, -87, -87, -87, -80, -80, -80, -80, -80, -80,
        -80, -80 },
      { -26, -26, -26, -26, -26, -26, -26, -26, 43, 43, 43, 43, 43, 43, 43,
        43 },
      { 57, 57, 57, 57, 57, 57, 57, 57, 9, 9, 9, 9, 9, 9, 9, 9 },
      { 90, 90, 90, 90, 90, 90, 90, 90, -57, -57, -57, -57, -57, -57, -57,
        -57 },
      { 43, 43, 43, 43, 43, 43, 43, 43, 87, 87, 87, 87, 87, 87, 87, 87 },
  },
  {
      { 70, 70, 70, 70, 70, 70, 70, 70, 9, 9, 9, 9, 9, 9, 9, 9 },
      { -43, -43, -43, -43, -43, -43, -43, -43, -26, -26, -26, -26, -26, -26,
        -26, -26 },
      { -87, -87, -87, -87, -87, -87, -87, -87, 43, 43, 43, 43, 43, 43, 43,
        43 },
      { 9, 9, 9, 9, 9, 9, 9, 9, -57, -57, -57, -57, -57, -57, -57, -57 },
      { 90, 90, 90, 90, 90, 90, 90, 90, 70, 70, 70, 70, 70, 70, 70, 70 },
      { 26, 26, 26, 26, 26, 26, 26, 26, -80, -80, -80, -80, -80, -80, -80,
        -80 },
      { -80, -80, -80, -80, -80, -80, -80, -80, 87, 87, 87, 87, 87, 87, 87,
        87 },
      { -57, -57, -57, -57, -57, -57, -57, -57, -90, -90, -90, -90, -90, -90,
        -90, -90 },
  },
};

// Output pairs (j, j + 8) for j = 2, 6, from d[0..3].
DECLARE_ALIGNED(64, static const int, dct16_line8_d[2][4][16]) = {
  {
      { 89, 89, 89, 89, 89, 89, 89, 89, 50, 50, 50, 50, 50, 50, 50, 50 },
      { 75, 75, 75, 75, 75, 75, 75, 75, -89, -89, -89, -89, -89, -89, -89,
        -89 },
      { 50, 50, 50, 50, 50, 50, 50, 50, 18, 18, 18, 18, 18, 18, 18, 18 },
      { 18, 18, 18, 18, 18, 18, 18, 18, 75, 75, 75, 75, 75, 75, 75, 75 },
  },
  {
      { 75, 75, 75, 75, 75, 75, 75, 75, 18, 18, 18, 18, 18, 18, 18, 18 },
      { -18, -18, -18, -18, -18, -18, -18, -18, -50, -50, -50, -50, -50, -50,
        -50, -50 },
      { -89, -89, -89, -89, -89, -89, -89, -89, 75, 75, 75, 75, 75, 75, 75,
        75 },
      { -50, -50, -50, -50, -50, -50, -50, -50, -89, -89, -89, -89, -89, -89,
        -89, -89 },
  },
};

// Output pairs (j, j + 8) for j = 4, from f[0..1].
DECLARE_ALIGNED(64, static const int, dct16_line8_f[1][2][16]) = {
  {
      { 83, 83, 83, 83, 83, 83, 83, 83, 35, 35, 35, 35, 35, 35, 35, 35 },
      { 35, 35, 35, 35, 35, 35, 35, 35, -83, -83, -83, -83, -83, -83, -83,
        -83 },
  },
};

// Output pairs (j, j + 8) for j = 0, from e[0..1].
DECLARE_ALIGNED(64, static const int, dct16_line8_e[1][2][16]) = {
  {
      { 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
      { 64, 64, 64, 64, 64, 64, 64, 64, -64, -64, -64, -64, -64, -64, -64,
        -64 },
  },
};

// Rows of 8 lines, in both halves of a vector.
static INLINE __m512i load_line8_avx512(const int *src) {
  return _mm512_broadcast_i64x4(_mm256_loadu_si256((const __m256i *)src));
}

// Sum of x[k] * cols[k] for k < n.
static INLINE __m512i dot_line8_avx512(const __m512i *x, const int (*cols)[16],
                                       int n) {
  __m512i sum = _mm512_mullo_epi32(x[0], _mm512_load_si512(cols[0]));
  for (int k = 1; k < n; k++) {
    sum = _mm512_add_epi32(
        sum, _mm512_mullo_epi32(x[k], _mm512_load_si512(cols[k])));
  }
  return sum;
}

// 32-point DCT2 over 8 lines. Vector j holds output j of the 8 lines in lanes
// 0-7 and output j + 16 in lanes 8-15, so each multiply serves 2 outputs.
static void fwd_txfm_dct2_size32_line8_avx512(const int *src, int *dst,
                                              int shift, int line,
                                              int skip_line, int zero_line) {
  (void)line;
  (void)skip_line;
  (void)zero_line;
  assert(line == 8 && skip_line == 0);
  const __m512i v_offset = _mm512_set1_epi32(shift > 0 ? 1 << (shift - 1) : 0);
  __m512i a[16], b[16], c[8], d[8], e[4], f[4], g[2], h[2], r[16], t[16];

  for (int k = 0; k < 16; k++) {
    const __m512i s0 = load_line8_avx512(src + k * 8);
    const __m512i s1 = load_line8_avx512(src + (31 - k) * 8);
    a[k] = _mm512_add_epi32(s0, s1);
    b[k] = _mm512_sub_epi32(s0, s1);
  }
  for (int k = 0; k < 8; k++) {
    c[k] = _mm512_add_epi32(a[k], a[15 - k]);
    d[k] = _mm512_sub_epi32(a[k], a[15 - k]);
  }
  for (int k = 0; k < 4; k++) {
    e[k] = _mm512_add_epi32(c[k], c[7 - k]);
    f[k] = _mm512_sub_epi32(c[k], c[7 - k]);
  }
  for (int k = 0; k < 2; k++) {
    g[k] = _mm512_add_epi32(e[k], e[3 - k]);
    h[k] = _mm512_sub_epi32(e[k], e[3 - k]);
  }

  for (int p = 0; p < 8; p++) {
    r[2 * p + 1] = dot_line8_avx512(b, dct32_line8_b[p], 16);
  }
  for (int p = 0; p < 4; p++) {
    r[4 * p + 2] = dot_line8_avx512(d, dct32_line8_d[p], 8);
  }
  for (int p = 0; p < 2; p++) {
    r[8 * p + 4] = dot_line8_avx512(f, dct32_line8_f[p], 4);
  }
  r[8] = dot_line8_avx512(h, dct32_line8_h[0], 2);
  r[0] = dot_line8_avx512(g, dct32_line8_g[0], 2);

  for (int j = 0; j < 16; j++) {
    r[j] = _mm512_srai_epi32(_mm512_add_epi32(r[j], v_offset), shift);
  }
  // t[i] holds outputs 0-15 of line i for i < 8, outputs 16-31 of line i - 8
  // otherwise.
  transpose_16x16_avx512(r, t);
  for (int i = 0; i < 16; i++) {
    _mm512_storeu_si512(dst + (i & 7) * 32 + (i >> 3) * 16, t[i]);
  }
}

// 16-point DCT2 over 8 lines. Vector j holds output j of the 8 lines in lanes
// 0-7 and output j + 8 in lanes 8-15.
static void fwd_txfm_dct2_size16_line8_avx512(const int *src, int *dst,
                                              int shift, int line,
                                              int skip_line, int zero_line) {
  (void)line;
  (void)skip_line;
  (void)zero_line;
  assert(line == 8 && skip_line == 0);
  const __m512i v_offset = _mm512_set1_epi32(shift > 0 ? 1 << (shift - 1) : 0);
  __m512i a[8], b[8], c[4], d[4], e[2], f[2], r[16], t[16];

  for (int k = 0; k < 8; k++) {
    const __m512i s0 = load_line8_avx512(src + k * 8);
    const __m512i s1 = load_line8_avx512(src + (15 - k) * 8);
    a[k] = _mm512_add_epi32(s0, s1);
    b[k] = _mm512_sub_epi32(s0, s1);
  }
  for (int k = 0; k < 4; k++) {
    c[k] = _mm512_add_epi32(a[k], a[7 - k]);
    d[k] = _mm512_sub_epi32(a[k], a[7 - k]);
  }
  for (int k = 0; k < 2; k++) {
    e[k] = _mm512_add_epi32(c[k], c[3 - k]);
    f[k] = _mm512_sub_epi32(c[k], c[3 - k]);
  }

  for (int p = 0; p < 4; p++) {
    r[2 * p + 1] = dot_line8_avx512(b, dct16_line8_b[p], 8);
  }
  for (int p = 0; p < 2; p++) {
    r[4 * p + 2] = dot_line8_avx512(d, dct16_line8_d[p], 4);
  }
  r[4] = dot_line8_avx512(f, dct16_line8_f[0], 2);
  r[0] = dot_line8_avx512(e, dct16_line8_e[0], 2);

  // r[j + 8] is r[j] with its halves swapped, so t[i] for i < 8 holds the 16
  // outputs of line i.
  for (int j = 0; j < 8; j++) {
    r[j] = _mm512_srai_epi32(_mm512_add_epi32(r[j], v_offset), shift);
    r[j + 8] = _mm512_shuffle_i64x2(r[j], r[j], 0x4e);
  }
  transpose_16x16_avx512(r, t);
  for (int i = 0; i < 8; i++) _mm512_storeu_si512(dst + i * 16, t[i]);
}

// Transposed tx_kernel_{adst,fdst,ddtx}_size16[FWD_TXFM], indexed by
// tx_type_index - DST7: row k holds the weights of input k for the 16 outputs.
// FDDT is DDTX with the inputs reversed.
DECLARE_ALIGNED(64, static const int, matrix16_cols[4][16][16]) = {
  {
      // DST7
      { 8, 25, 41, 55, 67, 77, 84, 88, 89, 87, 81, 73, 62, 48, 33, 17 },
      { 17, 48, 73, 87, 88, 77, 55, 25, -8, -41, -67, -84, -89, -81, -62, -33 },
      { 25, 67, 88, 81, 48, 0, -48, -81, -88, -67, -25, 25, 67, 88, 81, 48 },
      { 33, 81, 84, 41, -25, -77, -87, -48, 17, 73, 88, 55, -8, -67, -89, -62 },
      { 41, 88, 62, -17, -81, -77, -8, 67, 87, 33, -48, -89, -55, 25, 84, 73 },
      { 48, 88, 25, -67, -81, 0, 81, 67, -25, -88, -48, 48, 88, 25, -67, -81 },
      { 55, 81, -17, -89, -25, 77, 62, -48, -84, 8, 88, 33, -73, -67, 41, 87 },
      { 62, 67, -55, -73, 48, 77, -41, -81, 33, 84, -25, -87, 17, 88, -8, -89 },
      { 67, 48, -81, -25, 88, 0, -88, 25, 81, -48, -67, 67, 48, -81, -25, 88 },
      { 73, 25, -89, 33, 67, -77, -17, 88, -41, -62, 81, 8, -87, 48, 55, -84 },
      { 77, 0, -77, 77, 0, -77, 77, 0, -77, 77, 0, -77, 77, 0, -77, 77 },
      { 81, -25, -48, 88, -67, 0, 67, -88, 48, 25, -81, 81, -25, -48, 88, -67 },
      { 84, -48, -8, 62, -88, 77, -33, -25, 73, -89, 67, -17, -41, 81, -87,
        55 },
      { 87, -67, 33, 8, -48, 77, -89, 81, -55, 17, 25, -62, 84, -88, 73, -41 },
      { 88, -81, 67, -48, 25, 0, -25, 48, -67, 81, -88, 88, -81, 67, -48, 25 },
      { 89, -88, 87, -84, 81, -77, 73, -67, 62, -55, 48, -41, 33, -25, 17, -8 },
  },
  {
      // DCT8
      { 89, 88, 87, 84, 81, 77, 73, 67, 62, 55, 48, 41, 33, 25, 17, 8 },
      { 88, 81, 67, 48, 25, 0, -25, -48, -67, -81, -88, -88, -81, -67, -48,
        -25 },
      { 87, 67, 33, -8, -48, -77, -89, -81, -55, -17, 25, 62, 84, 88, 73, 41 },
      { 84, 48, -8, -62, -88, -77, -33, 25, 73, 89, 67, 17, -41, -81, -87,
        -55 },
      { 81, 25, -48, -88, -67, 0, 67, 88, 48, -25, -81, -81, -25, 48, 88, 67 },
      { 77, 0, -77, -77, 0, 77, 77, 0, -77, -77, 0, 77, 77, 0, -77, -77 },
      { 73, -25, -89, -33, 67, 77, -17, -88, -41, 62, 81, -8, -87, -48, 55,
        84 },
      { 67, -48, -81, 25, 88, 0, -88, -25, 81, 48, -67, -67, 48, 81, -25, -88 },
      { 62, -67, -55, 73, 48, -77, -41, 81, 33, -84, -25, 87, 17, -88, -8, 89 },
      { 55, -81, -17, 89, -25, -77, 62, 48, -84, -8, 88, -33, -73, 67, 41,
        -87 },
      { 48, -88, 25, 67, -81, 0, 81, -67, -25, 88, -48, -48, 88, -25, -67, 81 },
      { 41, -88, 62, 17, -81, 77, -8, -67, 87, -33, -48, 89, -55, -25, 84,
        -73 },
      { 33, -81, 84, -41, -25, 77, -87, 48, 17, -73, 88, -55, -8, 67, -89, 62 },
      { 25, -67, 88, -81, 48, 0, -48, 81, -88, 67, -25, -25, 67, -88, 81, -48 },
      { 17, -48, 73, -87, 88, -77, 55, -25, -8, 41, -67, 84, -89, 81, -62, 33 },
      { 8, -25, 41, -55, 67, -77, 84, -88, 89, -87, 81, -73, 62, -48, 33, -17 },
  },
  {
      // DDTX
      { 12, 17, 37, 45, 47, 60, 64, 82, 89, 100, 92, 84, 69, 50, 51, 44 },
      { 15, 23, 49, 60, 60, 74, 70, 73, 48, 9, -35, -71, -83, -79, -89, -95 },
      { 19, 30, 60, 69, 61, 64, 40, 3, -53, -99, -91, -46, 2, 47, 73, 124 },
      { 23, 38, 69, 73, 49, 28, -19, -80, -96, -45, 42, 88, 75, 14, -17, -126 },
      { 30, 48, 75, 66, 19, -31, -79, -91, -5, 84, 71, -16, -78, -60, -45,
        108 },
      { 39, 61, 75, 40, -29, -87, -78, 10, 89, 36, -69, -67, 18, 67, 89, -81 },
      { 51, 76, 61, -8, -77, -82, 11, 94, 16, -81, -22, 79, 50, -37, -103, 54 },
      { 66, 87, 29, -65, -83, 4, 92, 18, -83, 4, 85, -22, -85, -6, 97, -30 },
      { 78, 83, -18, -91, -16, 88, 28, -84, 12, 73, -60, -46, 81, 49, -83, 16 },
      { 88, 59, -67, -57, 75, 54, -85, -5, 75, -60, -17, 84, -43, -80, 71, -6 },
      { 94, 19, -96, 21, 93, -55, -41, 80, -51, -17, 77, -68, -6, 98, -56, 1 },
      { 97, -30, -83, 86, 3, -77, 82, -17, -43, 76, -70, 15, 53, -99, 44, 3 },
      { 93, -73, -28, 81, -92, 29, 39, -70, 81, -55, 11, 46, -81, 90, -31, -4 },
      { 83, -99, 40, 8, -74, 88, -83, 47, -14, -21, 56, -83, 88, -71, 22, 5 },
      { 68, -99, 84, -69, 32, 3, -37, 55, -75, 81, -83, 82, -69, 48, -11, -3 },
      { 50, -76, 83, -90, 97, -86, 83, -68, 67, -56, 49, -40, 32, -19, 5, 2 },
  },
  {
      // FDDT
      { 50, -76, 83, -90, 97, -86, 83, -68, 67, -56, 49, -40, 32, -19, 5, 2 },
      { 68, -99, 84, -69, 32, 3, -37, 55, -75, 81, -83, 82, -69, 48, -11, -3 },
      { 83, -99, 40, 8, -74, 88, -83, 47, -14, -21, 56, -83, 88, -71, 22, 5 },
      { 93, -73, -28, 81, -92, 29, 39, -70, 81, -55, 11, 46, -81, 90, -31, -4 },
      { 97, -30, -83, 86, 3, -77, 82, -17, -43, 76, -70, 15, 53, -99, 44, 3 },
      { 94, 19, -96, 21, 93, -55, -41, 80, -51, -17, 77, -68, -6, 98, -56, 1 },
      { 88, 59, -67, -57, 75, 54, -85, -5, 75, -60, -17, 84, -43, -80, 71, -6 },
      { 78, 83, -18, -91, -16, 88, 28, -84, 12, 73, -60, -46, 81, 49, -83, 16 },
      { 66, 87, 29, -65, -83, 4, 92, 18, -83, 4, 85, -22, -85, -6, 97, -30 },
      { 51, 76, 61, -8, -77, -82, 11, 94, 16, -81, -22, 79, 50, -37, -103, 54 },
      { 39, 61, 75, 40, -29, -87, -78, 10, 89, 36, -69, -67, 18, 67, 89, -81 },
      { 30, 48, 75, 66, 19, -31, -79, -91, -5, 84, 71, -16, -78, -60, -45,
        108 },
      { 23, 38, 69, 73, 49, 28, -19, -80, -96, -45, 42, 88, 75, 14, -17, -126 },
      { 19, 30, 60, 69, 61, 64, 40, 3, -53, -99, -91, -46, 2, 47, 73, 124 },
      { 15, 23, 49, 60, 60, 74, 70, 73, 48, 9, -35, -71, -83, -79, -89, -95 },
      { 12, 17, 37, 45, 47, 60, 64, 82, 89, 100, 92, 84, 69, 50, 51, 44 },
  },
};

// 16-point matrix product, one line at a time with the 16 outputs in one
// vector.
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void
fwd_txfm_matrix16_avx512(const int *src, int *dst, int shift, int line,
                         int skip_line, const int (*tx_cols)[16]) {
  const int nz_line = line - skip_line;
  const __m512i v_offset = _mm512_set1_epi32(shift > 0 ? 1 << (shift - 1) : 0);
  __m512i t[16];

  for (int k = 0; k < 16; k++) {
    t[k] = _mm512_load_si512(tx_cols[k]);
  }
  for (int j = 0; j < nz_line; j++) {
    __m512i sum = v_offset;
    for (int k = 0; k < 16; k++) {
      const __m512i s = _mm512_set1_epi32(src[k * line + j]);
      sum = _mm512_add_epi32(sum, _mm512_mullo_epi32(t[k], s));
    }
    _mm512_storeu_si512(dst + j * 16, _mm512_srai_epi32(sum, shift));
  }
}

// Identity of size 16 or 32 over 16 lines at a time. The size-16 scale of 256
// gives the same result as a left shift by 8.
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void
fwd_txfm_idtx_avx512(const int *src, int *dst, int shift, int line,
                     int skip_line, int size) {
  const int nz_line = line - skip_line;
  assert(nz_line >= 16 && (nz_line & 15) == 0);
  const __m512i v_scale = _mm512_set1_epi32(size == 16 ? 256 : 362);
  const __m512i v_offset = _mm512_set1_epi32(shift > 0 ? 1 << (shift - 1) : 0);
  __m512i a[16];

  for (int i = 0; i < nz_line; i += 16) {
    for (int j = 0; j < size; j += 16) {
      for (int t = 0; t < 16; t++) {
        const __m512i v = _mm512_mullo_epi32(
            _mm512_loadu_si512(src + (j + t) * line + i), v_scale);
        a[t] = _mm512_srai_epi32(_mm512_add_epi32(v, v_offset), shift);
      }
      transpose_store_16x16_avx512(a, dst + i * size + j, size);
    }
  }
}

// Wrappers with the common 1D kernel signature.
static void fwd_txfm_adst_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_matrix16_avx512(src, dst, shift, line, skip_line, matrix16_cols[0]);
}

static void fwd_txfm_fdst_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_matrix16_avx512(src, dst, shift, line, skip_line, matrix16_cols[1]);
}

static void fwd_txfm_ddtx_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_matrix16_avx512(src, dst, shift, line, skip_line, matrix16_cols[2]);
}

static void fwd_txfm_fddt_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_matrix16_avx512(src, dst, shift, line, skip_line, matrix16_cols[3]);
}

static void fwd_txfm_idtx_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_idtx_avx512(src, dst, shift, line, skip_line, 16);
}

static void fwd_txfm_idtx_size32_avx512(const int *src, int *dst, int shift,
                                        int line, int skip_line,
                                        int zero_line) {
  (void)zero_line;
  fwd_txfm_idtx_avx512(src, dst, shift, line, skip_line, 32);
}

typedef void (*FwdTxfm1dFunc)(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);

// 1D kernels indexed by [tx_type_index][size_index], for passes of 4 lines, 8
// lines and 16 lines or more (see line_class()). The DCT2 and identity kernels
// of AVX-512 process 16 lines at a time, or exactly 8 for the line8 ones.
static const FwdTxfm1dFunc fwd_txfm_1d_avx512[3][FDDT + 1][4] = {
  {
      { fwd_txfm_dct2_size4_avx2, fwd_txfm_dct2_size8_avx2,
        fwd_txfm_dct2_size16_avx2, fwd_txfm_dct2_size32_avx2 },
      { fwd_txfm_idtx_size4_avx2, fwd_txfm_idtx_size8_avx2,
        fwd_txfm_idtx_size16_avx2, fwd_txfm_idtx_size32_avx2 },
      { fwd_txfm_adst_size4_avx2, fwd_txfm_adst_size8_avx2,
        fwd_txfm_adst_size16_avx512, NULL },
      { fwd_txfm_fdst_size4_avx2, fwd_txfm_fdst_size8_avx2,
        fwd_txfm_fdst_size16_avx512, NULL },
      { fwd_txfm_ddtx_size4_avx2, fwd_txfm_ddtx_size8_avx2,
        fwd_txfm_ddtx_size16_avx512, NULL },
      { fwd_txfm_fddt_size4_avx2, fwd_txfm_fddt_size8_avx2,
        fwd_txfm_fddt_size16_avx512, NULL },
  },
  {
      { fwd_txfm_dct2_size4_avx2, fwd_txfm_dct2_size8_avx2,
        fwd_txfm_dct2_size16_line8_avx512, fwd_txfm_dct2_size32_line8_avx512 },
      { fwd_txfm_idtx_size4_avx2, fwd_txfm_idtx_size8_avx2,
        fwd_txfm_idtx_size16_avx2, fwd_txfm_idtx_size32_avx2 },
      { fwd_txfm_adst_size4_avx2, fwd_txfm_adst_size8_avx2,
        fwd_txfm_adst_size16_avx512, NULL },
      { fwd_txfm_fdst_size4_avx2, fwd_txfm_fdst_size8_avx2,
        fwd_txfm_fdst_size16_avx512, NULL },
      { fwd_txfm_ddtx_size4_avx2, fwd_txfm_ddtx_size8_avx2,
        fwd_txfm_ddtx_size16_avx512, NULL },
      { fwd_txfm_fddt_size4_avx2, fwd_txfm_fddt_size8_avx2,
        fwd_txfm_fddt_size16_avx512, NULL },
  },
  {
      { fwd_txfm_dct2_size4_avx2, fwd_txfm_dct2_size8_avx2,
        fwd_txfm_dct2_size16_avx512, fwd_txfm_dct2_size32_avx512 },
      { fwd_txfm_idtx_size4_avx2, fwd_txfm_idtx_size8_avx2,
        fwd_txfm_idtx_size16_avx512, fwd_txfm_idtx_size32_avx512 },
      { fwd_txfm_adst_size4_avx2, fwd_txfm_adst_size8_avx2,
        fwd_txfm_adst_size16_avx512, NULL },
      { fwd_txfm_fdst_size4_avx2, fwd_txfm_fdst_size8_avx2,
        fwd_txfm_fdst_size16_avx512, NULL },
      { fwd_txfm_ddtx_size4_avx2, fwd_txfm_ddtx_size8_avx2,
        fwd_txfm_ddtx_size16_avx512, NULL },
      { fwd_txfm_fddt_size4_avx2, fwd_txfm_fddt_size8_avx2,
        fwd_txfm_fddt_size16_avx512, NULL },
  },
};

// Row of fwd_txfm_1d_avx512[] for a pass over 4, 8 or 16+ lines.
static INLINE int line_class(int lines) { return (lines >= 8) + (lines >= 16); }

// Same as av2_fwd_txfm_downsample_input(): sums each pair of samples along
// the 64-length dimensions into a packed block of at most 32x32.
static void fwd_txfm_downsample_input_avx512(const int16_t *resi, int stride,
                                             int width, int height,
                                             int32_t *out) {
  const int ds_y = height > 32;
  if (width == 64) {
    const __m512i ones = _mm512_set1_epi16(1);
    for (int y = 0; y < (height >> ds_y); y++) {
      const int16_t *r = resi + (y << ds_y) * stride;
      for (int x = 0; x < 32; x += 16) {
        __m512i s = _mm512_madd_epi16(_mm512_loadu_si512(r + 2 * x), ones);
        if (ds_y) {
          s = _mm512_add_epi32(
              s,
              _mm512_madd_epi16(_mm512_loadu_si512(r + stride + 2 * x), ones));
        }
        _mm512_storeu_si512(out + y * 32 + x, s);
      }
    }
    return;
  }

  // Only the height is 64.
  for (int y = 0; y < 32; y++) {
    const int16_t *r0 = resi + 2 * y * stride;
    const int16_t *r1 = r0 + stride;
    int32_t *c = out + y * width;
    if (width == 4) {
      const __m128i a =
          _mm_cvtepi16_epi32(_mm_loadl_epi64((const __m128i *)r0));
      const __m128i b =
          _mm_cvtepi16_epi32(_mm_loadl_epi64((const __m128i *)r1));
      _mm_storeu_si128((__m128i *)c, _mm_add_epi32(a, b));
    } else if (width == 8) {
      const __m256i a =
          _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)r0));
      const __m256i b =
          _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)r1));
      _mm256_storeu_si256((__m256i *)c, _mm256_add_epi32(a, b));
    } else {
      for (int x = 0; x < width; x += 16) {
        const __m512i a = _mm512_cvtepi16_epi32(
            _mm256_loadu_si256((const __m256i *)(r0 + x)));
        const __m512i b = _mm512_cvtepi16_epi32(
            _mm256_loadu_si256((const __m256i *)(r1 + x)));
        _mm512_storeu_si512(c + x, _mm512_add_epi32(a, b));
      }
    }
  }
}

// round_shift(x * NewSqrt2, NewSqrt2Bits) for 16 values.
static INLINE __m512i sqrt2_scale_avx512(__m512i x) {
  const __m512i scale = _mm512_set1_epi64((int64_t)NewSqrt2);
  const __m512i rnd = _mm512_set1_epi64(1LL << (NewSqrt2Bits - 1));
  __m512i even = _mm512_mul_epi32(x, scale);
  __m512i odd = _mm512_mul_epi32(_mm512_srli_epi64(x, 32), scale);
  even = _mm512_srli_epi64(_mm512_add_epi64(even, rnd), NewSqrt2Bits);
  odd = _mm512_srli_epi64(_mm512_add_epi64(odd, rnd), NewSqrt2Bits);
  return _mm512_mask_blend_epi32(0xAAAA, even, _mm512_slli_epi64(odd, 32));
}

// Rows y to y + 3 of a 4-wide residual block, widened to 32 bits.
static INLINE __m512i load_resi_4x4_avx512(const int16_t *resi, int stride) {
  const __m128i r01 =
      _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)resi),
                         _mm_loadl_epi64((const __m128i *)(resi + stride)));
  const __m128i r23 =
      _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(resi + 2 * stride)),
                         _mm_loadl_epi64((const __m128i *)(resi + 3 * stride)));
  return _mm512_cvtepi16_epi32(_mm256_set_m128i(r23, r01));
}

// Rows y and y + 1 of an 8-wide residual block, widened to 32 bits.
static INLINE __m512i load_resi_8x2_avx512(const int16_t *resi, int stride) {
  return _mm512_cvtepi16_epi32(
      _mm256_set_m128i(_mm_loadu_si128((const __m128i *)(resi + stride)),
                       _mm_loadu_si128((const __m128i *)resi)));
}

static const int idtx_scale[4] = { 128, 181, 256, 362 };

typedef struct {
  __m512i scale_col, offset_col, scale_row, offset_row;
  int shift_col, shift_row, sqrt2;
} IdtxParams;

// Column scale, row scale and the sqrt2 step of 2:1 blocks, each rounded.
static INLINE __m512i idtx_2d_scale_avx512(__m512i x, const IdtxParams *p) {
  x = _mm512_mullo_epi32(x, p->scale_col);
  x = _mm512_srai_epi32(_mm512_add_epi32(x, p->offset_col), p->shift_col);
  x = _mm512_mullo_epi32(x, p->scale_row);
  x = _mm512_srai_epi32(_mm512_add_epi32(x, p->offset_row), p->shift_row);
  return p->sqrt2 ? sqrt2_scale_avx512(x) : x;
}

// Forward 2D identity transform, 16 coefficients at a time: one row for widths
// of 16 or more, otherwise 2 rows of 8 or 4 rows of 4.
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void
fwd_txfm_idtx_2d_avx512(const int16_t *resi, int stride, tran_low_t *coeff,
                        TX_SIZE tx_size) {
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  // IDTX is not allowed when transform width or height is greater than 32.
  assert(width <= 32 && height <= 32);
  const int shift_col = fwd_tx_shift[tx_size][0];
  const int shift_row = fwd_tx_shift[tx_size][1];
  IdtxParams p;
  p.scale_col = _mm512_set1_epi32(idtx_scale[tx_size_high_log2[tx_size] - 2]);
  p.scale_row = _mm512_set1_epi32(idtx_scale[tx_size_wide_log2[tx_size] - 2]);
  p.offset_col = _mm512_set1_epi32(shift_col > 0 ? 1 << (shift_col - 1) : 0);
  p.offset_row = _mm512_set1_epi32(shift_row > 0 ? 1 << (shift_row - 1) : 0);
  p.shift_col = shift_col;
  p.shift_row = shift_row;
  p.sqrt2 = (tx_size_wide_log2[tx_size] + tx_size_high_log2[tx_size]) & 1;

  if (width >= 16) {
    for (int y = 0; y < height; y++) {
      const int16_t *r = resi + y * stride;
      for (int x = 0; x < width; x += 16) {
        const __m512i v =
            _mm512_cvtepi16_epi32(_mm256_loadu_si256((const __m256i *)(r + x)));
        _mm512_storeu_si512(coeff + y * width + x, idtx_2d_scale_avx512(v, &p));
      }
    }
  } else if (width == 8) {
    assert((height & 1) == 0);
    for (int y = 0; y < height; y += 2) {
      _mm512_storeu_si512(
          coeff + y * 8,
          idtx_2d_scale_avx512(load_resi_8x2_avx512(resi + y * stride, stride),
                               &p));
    }
  } else {
    assert(width == 4 && (height & 3) == 0);
    for (int y = 0; y < height; y += 4) {
      _mm512_storeu_si512(
          coeff + y * 4,
          idtx_2d_scale_avx512(load_resi_4x4_avx512(resi + y * stride, stride),
                               &p));
    }
  }
}

// Per-size parameters of fwd_txfm_2d_avx512().
typedef struct {
  // 1D kernel sizes as indices of 4, 8, 16 and 32. 64-length sides are
  // downsampled and use the 32-point kernels.
  uint8_t wide_index, high_index;
  uint8_t sqrt2;
  // Whether DST7 and DCT8 become DDTX and FDDT for the row and column.
  uint8_t ddt_row, ddt_col;
} Txfm2dSize;

// From log2 of the width and height.
#define TXFM_2D_SIZE(wl, hl)                                                  \
  AVMMIN(wl, MAX_TX_SIZE_LOG2 - 1) - 2, AVMMIN(hl, MAX_TX_SIZE_LOG2 - 1) - 2, \
      ((wl) + (hl)) & 1,                                                      \
      ((wl) == 2 && REPLACE_ADST4) || ((wl) == 3 && REPLACE_ADST8) ||         \
          ((wl) == 4 && REPLACE_ADST16),                                      \
      ((hl) == 2 && REPLACE_ADST4) || ((hl) == 3 && REPLACE_ADST8) ||         \
          ((hl) == 4 && REPLACE_ADST16)

// In TX_SIZE order.
static const Txfm2dSize txfm_2d_size[TX_SIZES_ALL] = {
  { TXFM_2D_SIZE(2, 2) }, { TXFM_2D_SIZE(3, 3) }, { TXFM_2D_SIZE(4, 4) },
  { TXFM_2D_SIZE(5, 5) }, { TXFM_2D_SIZE(6, 6) }, { TXFM_2D_SIZE(2, 3) },
  { TXFM_2D_SIZE(3, 2) }, { TXFM_2D_SIZE(3, 4) }, { TXFM_2D_SIZE(4, 3) },
  { TXFM_2D_SIZE(4, 5) }, { TXFM_2D_SIZE(5, 4) }, { TXFM_2D_SIZE(5, 6) },
  { TXFM_2D_SIZE(6, 5) }, { TXFM_2D_SIZE(2, 4) }, { TXFM_2D_SIZE(4, 2) },
  { TXFM_2D_SIZE(3, 5) }, { TXFM_2D_SIZE(5, 3) }, { TXFM_2D_SIZE(4, 6) },
  { TXFM_2D_SIZE(6, 4) }, { TXFM_2D_SIZE(2, 5) }, { TXFM_2D_SIZE(5, 2) },
  { TXFM_2D_SIZE(3, 6) }, { TXFM_2D_SIZE(6, 3) }, { TXFM_2D_SIZE(2, 6) },
  { TXFM_2D_SIZE(6, 2) },
};

// DST7 and DCT8 replaced by DDTX and FDDT.
static const uint8_t ddt_tx_type[FDDT + 1] = {
  DCT2, IDT, DDTX, FDDT, DDTX, FDDT
};

// Kept out of line so that lossless and IDTX blocks skip its stack frame.
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void
fwd_txfm_2d_avx512(const int16_t *resi, tran_low_t *coeff, int diff_stride,
                   TxfmParam *txfm_param) {
  const PRIMARY_TX_TYPE primary_tx_type = txfm_param->primary_tx_type;
  const TX_SIZE tx_size = txfm_param->tx_size;
  const Txfm2dSize *size = &txfm_2d_size[tx_size];

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  int tx_type_row = g_hor_tx_type[primary_tx_type];
  int tx_type_col = g_ver_tx_type[primary_tx_type];

  if (txfm_param->use_ddt) {
    if (size->ddt_row) tx_type_row = ddt_tx_type[tx_type_row];
    if (size->ddt_col) tx_type_col = ddt_tx_type[tx_type_col];
  }

  DECLARE_ALIGNED(64, int, buf[MAX_TX_SQUARE]);
  assert(width >= 4 && height >= 4);

  const int tx_wide_index = size->wide_index;
  const int tx_high_index = size->high_index;

  // Note: Shifts are still based on the original tx_size (see comment in
  // av2_fwd_txfm_downsample_input()).
  const int shift_1st = fwd_tx_shift[tx_size][0];
  const int shift_2nd = fwd_tx_shift[tx_size][1];

  const int w = AVMMIN(width, 32);
  const int h = AVMMIN(height, 32);
  if (width > 32 || height > 32) {
    // For 64-length dimensions, downsample the residual by 2 along such
    // dimensions, then apply 32-point kernels.
    fwd_txfm_downsample_input_avx512(resi, diff_stride, width, height, coeff);
  } else if (width == 4) {
    assert((height & 3) == 0);
    for (int y = 0; y < height; y += 4) {
      _mm512_storeu_si512(
          coeff + y * 4,
          load_resi_4x4_avx512(resi + y * diff_stride, diff_stride));
    }
  } else if (width == 8) {
    assert((height & 1) == 0);
    for (int y = 0; y < height; y += 2) {
      _mm512_storeu_si512(
          coeff + y * 8,
          load_resi_8x2_avx512(resi + y * diff_stride, diff_stride));
    }
  } else {
    for (int y = 0; y < height; y++) {
      const int16_t *r = resi + y * diff_stride;
      for (int x = 0; x < width; x += 16) {
        _mm512_storeu_si512(coeff + y * width + x,
                            _mm512_cvtepi16_epi32(
                                _mm256_loadu_si256((const __m256i *)(r + x))));
      }
    }
  }

  const FwdTxfm1dFunc col_txfm =
      fwd_txfm_1d_avx512[line_class(w)][tx_type_col][tx_high_index];
  assert(col_txfm != NULL);
  col_txfm(coeff, buf, shift_1st, w, 0, 0);

  if (row_pass_needs_64bit(tx_size, txfm_param->bd)) {
    fwd_transform_1d_64bit_avx2(buf, coeff, shift_2nd, h, 0, 0, tx_type_row,
                                tx_wide_index);
  } else {
    const FwdTxfm1dFunc row_txfm =
        fwd_txfm_1d_avx512[line_class(h)][tx_type_row][tx_wide_index];
    assert(row_txfm != NULL);
    row_txfm(buf, coeff, shift_2nd, h, 0, 0);
  }

  if (size->sqrt2) {
    assert((w * h) % 16 == 0);
    for (int i = 0; i < w * h; i += 16) {
      _mm512_storeu_si512(coeff + i,
                          sqrt2_scale_avx512(_mm512_loadu_si512(coeff + i)));
    }
  }
}

void fwd_txfm_avx512(const int16_t *resi, tran_low_t *coeff, int diff_stride,
                     TxfmParam *txfm_param) {
  if (txfm_param->lossless) {
    assert(txfm_param->primary_tx_type == DCT_DCT);
    av2_highbd_fwht4x4(resi, coeff, diff_stride);
    return;
  }
  if (txfm_param->primary_tx_type == IDTX) {
    fwd_txfm_idtx_2d_avx512(resi, diff_stride, coeff, txfm_param->tx_size);
    return;
  }
  fwd_txfm_2d_avx512(resi, coeff, diff_stride, txfm_param);
}
