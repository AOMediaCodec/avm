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

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "avm_dsp/x86/synonyms.h"
#include "av2/common/idct.h"
#include "av2/common/txb_common.h"
#include "av2/common/x86/inv_txfm_avx512.h"

// AVX-512 inverse DCT2, length 16 (16 lines/iter). Falls back to AVX2 for <16.
void inv_txfm_dct2_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  if (nz_line < 16) {
    inv_txfm_dct2_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }

  int j, k;
  const int add = 1 << (shift - 1);
  const int *tx_mat = tx_kernel_dct2_size16[INV_TXFM][0];

  __m512i a[8], b[8];
  __m512i c[4], d[4];
  __m512i e[2], f[2];

  const __m512i v_offset = _mm512_set1_epi32(add);
  const __m512i v_coef_min = _mm512_set1_epi32(coef_min);
  const __m512i v_coef_max = _mm512_set1_epi32(coef_max);

  __m512i s[16];

  const __m512i txmat_4_0 = _mm512_set1_epi32(tx_mat[4 * 16 + 0]);
  const __m512i txmat_4_1 = _mm512_set1_epi32(tx_mat[4 * 16 + 1]);

  const __m512i txmat_2_0 = _mm512_set1_epi32(tx_mat[2 * 16 + 0]);
  const __m512i txmat_2_1 = _mm512_set1_epi32(tx_mat[2 * 16 + 1]);
  const __m512i txmat_2_2 = _mm512_set1_epi32(tx_mat[2 * 16 + 2]);
  const __m512i txmat_2_3 = _mm512_set1_epi32(tx_mat[2 * 16 + 3]);

  const __m512i txmat_1_0 = _mm512_set1_epi32(tx_mat[1 * 16 + 0]);
  const __m512i txmat_1_1 = _mm512_set1_epi32(tx_mat[1 * 16 + 1]);
  const __m512i txmat_1_2 = _mm512_set1_epi32(tx_mat[1 * 16 + 2]);
  const __m512i txmat_1_3 = _mm512_set1_epi32(tx_mat[1 * 16 + 3]);
  const __m512i txmat_1_4 = _mm512_set1_epi32(tx_mat[1 * 16 + 4]);
  const __m512i txmat_1_5 = _mm512_set1_epi32(tx_mat[1 * 16 + 5]);
  const __m512i txmat_1_6 = _mm512_set1_epi32(tx_mat[1 * 16 + 6]);
  const __m512i txmat_1_7 = _mm512_set1_epi32(tx_mat[1 * 16 + 7]);

  for (j = 0; j < nz_line; j += 16) {
    transpose_load_16x16_avx512(src, s, tx1d_size);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_0);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_1);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_2);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_3);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_4);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_5);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_6);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_7);
    a[0] = _mm512_add_epi32(_mm512_add_epi32(a[0], a[1]),
                            _mm512_add_epi32(a[2], a[3]));
    a[4] = _mm512_add_epi32(_mm512_add_epi32(a[4], a[5]),
                            _mm512_add_epi32(a[6], a[7]));
    b[0] = _mm512_add_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_1);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_4);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_7);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_5);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_2);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_0);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_3);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_6);
    a[0] = _mm512_add_epi32(_mm512_add_epi32(a[0], a[1]),
                            _mm512_sub_epi32(a[2], a[3]));
    a[4] = _mm512_add_epi32(_mm512_add_epi32(a[4], a[5]),
                            _mm512_add_epi32(a[6], a[7]));
    b[1] = _mm512_sub_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_2);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_7);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_3);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_1);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_6);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_4);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_0);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_5);
    a[0] = _mm512_sub_epi32(_mm512_add_epi32(a[0], a[1]),
                            _mm512_add_epi32(a[2], a[3]));
    a[4] = _mm512_sub_epi32(_mm512_sub_epi32(a[4], a[5]),
                            _mm512_add_epi32(a[6], a[7]));
    b[2] = _mm512_sub_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_3);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_5);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_1);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_7);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_0);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_6);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_2);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_4);
    a[0] = _mm512_sub_epi32(_mm512_sub_epi32(a[0], a[1]),
                            _mm512_sub_epi32(a[2], a[3]));
    a[4] = _mm512_sub_epi32(_mm512_add_epi32(a[4], a[5]),
                            _mm512_add_epi32(a[6], a[7]));
    b[3] = _mm512_add_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_4);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_2);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_6);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_0);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_7);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_1);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_5);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_3);
    a[0] = _mm512_sub_epi32(_mm512_sub_epi32(a[0], a[1]),
                            _mm512_sub_epi32(a[2], a[3]));
    a[4] = _mm512_sub_epi32(_mm512_add_epi32(a[4], a[5]),
                            _mm512_add_epi32(a[6], a[7]));
    b[4] = _mm512_sub_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_5);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_0);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_4);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_6);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_1);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_3);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_7);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_2);
    a[0] = _mm512_add_epi32(_mm512_sub_epi32(a[0], a[1]),
                            _mm512_add_epi32(a[2], a[3]));
    a[4] = _mm512_sub_epi32(_mm512_sub_epi32(a[4], a[5]),
                            _mm512_sub_epi32(a[6], a[7]));
    b[5] = _mm512_sub_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_6);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_3);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_0);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_2);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_5);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_7);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_4);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_1);
    a[0] = _mm512_add_epi32(_mm512_sub_epi32(a[0], a[1]),
                            _mm512_sub_epi32(a[2], a[3]));
    a[4] = _mm512_sub_epi32(_mm512_add_epi32(a[4], a[5]),
                            _mm512_sub_epi32(a[6], a[7]));
    b[6] = _mm512_add_epi32(a[0], a[4]);

    a[0] = _mm512_mullo_epi32(s[1], txmat_1_7);
    a[1] = _mm512_mullo_epi32(s[3], txmat_1_6);
    a[2] = _mm512_mullo_epi32(s[5], txmat_1_5);
    a[3] = _mm512_mullo_epi32(s[7], txmat_1_4);
    a[4] = _mm512_mullo_epi32(s[9], txmat_1_3);
    a[5] = _mm512_mullo_epi32(s[11], txmat_1_2);
    a[6] = _mm512_mullo_epi32(s[13], txmat_1_1);
    a[7] = _mm512_mullo_epi32(s[15], txmat_1_0);
    a[0] = _mm512_add_epi32(_mm512_sub_epi32(a[0], a[1]),
                            _mm512_sub_epi32(a[2], a[3]));
    a[4] = _mm512_add_epi32(_mm512_sub_epi32(a[4], a[5]),
                            _mm512_sub_epi32(a[6], a[7]));
    b[7] = _mm512_add_epi32(a[0], a[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_0);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_1);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_2);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_3);
    d[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_1);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_3);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_0);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_2);
    d[1] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_2);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_0);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_3);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_1);
    d[2] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_3);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_2);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_1);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_0);
    d[3] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));

    f[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[4], txmat_4_0),
                            _mm512_mullo_epi32(s[12], txmat_4_1));
    f[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[4], txmat_4_1),
                            _mm512_mullo_epi32(s[12], txmat_4_0));
    e[0] = _mm512_add_epi32(s[0], s[8]);
    e[0] = _mm512_slli_epi32(e[0], 6);
    e[1] = _mm512_sub_epi32(s[0], s[8]);
    e[1] = _mm512_slli_epi32(e[1], 6);

    c[0] = _mm512_add_epi32(e[0], f[0]);
    c[3] = _mm512_sub_epi32(e[0], f[0]);
    c[1] = _mm512_add_epi32(e[1], f[1]);
    c[2] = _mm512_sub_epi32(e[1], f[1]);

    for (k = 0; k < 4; k++) {
      a[k] = _mm512_add_epi32(c[k], d[k]);
      a[k + 4] = _mm512_sub_epi32(c[3 - k], d[3 - k]);
    }
    for (k = 0; k < 8; k++) {
      e[0] = _mm512_add_epi32(v_offset, _mm512_add_epi32(a[k], b[k]));
      e[1] = _mm512_add_epi32(v_offset, _mm512_sub_epi32(a[7 - k], b[7 - k]));

      e[0] = _mm512_srai_epi32(e[0], shift);
      e[1] = _mm512_srai_epi32(e[1], shift);

      e[0] = _mm512_min_epi32(e[0], v_coef_max);
      e[0] = _mm512_max_epi32(e[0], v_coef_min);
      e[1] = _mm512_min_epi32(e[1], v_coef_max);
      e[1] = _mm512_max_epi32(e[1], v_coef_min);

      _mm512_storeu_si512((__m512i *)(dst + k * line), e[0]);
      _mm512_storeu_si512((__m512i *)(dst + (k + 8) * line), e[1]);
    }
    src += 16 * tx1d_size;
    dst += 16;
  }
}

// AVX-512 inverse identity transform, length 16.
void inv_txfm_idtx_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  if (nz_line < 16) {
    inv_txfm_idtx_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }

  const int offset = 1 << (shift - 1);
  const __m512i v_offset = _mm512_set1_epi32(offset);
  const __m512i v_coef_min = _mm512_set1_epi32(coef_min);
  const __m512i v_coef_max = _mm512_set1_epi32(coef_max);

  __m512i s[16];
  for (int j = 0; j < nz_line; j += 16) {
    transpose_load_16x16_avx512(src, s, tx1d_size);
    for (int c = 0; c < 16; c++) {
      __m512i v = _mm512_slli_epi32(s[c], 8);  // scale by 256
      v = _mm512_add_epi32(v, v_offset);
      v = _mm512_srai_epi32(v, shift);
      v = _mm512_min_epi32(v, v_coef_max);
      v = _mm512_max_epi32(v, v_coef_min);
      _mm512_storeu_si512((__m512i *)(dst + c * line), v);
    }
    src += 16 * tx1d_size;
    dst += 16;
  }
}

// Generic length-16 matrix-multiply for ADST/FDST/DDTX/FDDT (16 lines/iter).
static INLINE void matmul_size16_avx512(const int *src, int *dst, int shift,
                                        int line, int nz_line, int coef_min,
                                        int coef_max, const int *tx_mat,
                                        int reverse_out) {
  const int tx1d_size = 16;
  const __m512i v_offset = _mm512_set1_epi32(1 << (shift - 1));
  const __m512i v_coef_min = _mm512_set1_epi32(coef_min);
  const __m512i v_coef_max = _mm512_set1_epi32(coef_max);
  __m512i s[16];

  for (int j = 0; j < nz_line; j += 16) {
    transpose_load_16x16_avx512(src, s, tx1d_size);
    for (int i = 0; i < tx1d_size; i++) {
      __m512i sum = _mm512_setzero_si512();
      for (int k = 0; k < tx1d_size; k++) {
        const __m512i coef = _mm512_set1_epi32(tx_mat[k * tx1d_size + i]);
        sum = _mm512_add_epi32(sum, _mm512_mullo_epi32(s[k], coef));
      }
      sum = _mm512_add_epi32(sum, v_offset);
      sum = _mm512_srai_epi32(sum, shift);
      sum = _mm512_min_epi32(sum, v_coef_max);
      sum = _mm512_max_epi32(sum, v_coef_min);
      const int out_row = reverse_out ? (tx1d_size - 1 - i) : i;
      _mm512_storeu_si512((__m512i *)(dst + out_row * line), sum);
    }
    src += 16 * tx1d_size;
    dst += 16;
  }
}

void inv_txfm_adst_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  if (nz_line < 16) {
    inv_txfm_adst_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  matmul_size16_avx512(src, dst, shift, line, nz_line, coef_min, coef_max,
                       tx_kernel_adst_size16[INV_TXFM][0], 0);
}

void inv_txfm_fdst_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  if (nz_line < 16) {
    inv_txfm_fdst_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  matmul_size16_avx512(src, dst, shift, line, nz_line, coef_min, coef_max,
                       tx_kernel_fdst_size16[INV_TXFM][0], 0);
}

void inv_txfm_ddtx_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  if (nz_line < 16) {
    inv_txfm_ddtx_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  matmul_size16_avx512(src, dst, shift, line, nz_line, coef_min, coef_max,
                       tx_kernel_ddtx_size16[INV_TXFM][0], 0);
}

// FDDT: DDTX kernel with reversed output rows.
void inv_txfm_fddt_size16_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  if (nz_line < 16) {
    inv_txfm_fddt_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  matmul_size16_avx512(src, dst, shift, line, nz_line, coef_min, coef_max,
                       tx_kernel_ddtx_size16[INV_TXFM][0], 1);
}

// AVX-512 1D dispatcher. Lengths 4/8 fall back to AVX2.
void inv_transform_1d_avx512(const int *src, int *dst, int shift, int line,
                             int skip_line, int zero_line, int coef_min,
                             int coef_max, int tx_type_index, int size_index) {
  if (size_index == 3) {
    // Length 32: only DCT2 (0) and IDTX (1) are valid transform types.
    if (tx_type_index == 0)
      inv_txfm_dct2_size32_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
    else if (tx_type_index == 1)
      inv_txfm_idtx_size32_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
    else
      assert(0);
    return;
  }
  if (size_index != 2) {
    inv_transform_1d_avx2(src, dst, shift, line, skip_line, zero_line, coef_min,
                          coef_max, tx_type_index, size_index);
    return;
  }
  switch (tx_type_index) {
    case 0:
      inv_txfm_dct2_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    case 1:
      inv_txfm_idtx_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    case 2:
      inv_txfm_adst_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    case 3:
      inv_txfm_fdst_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    case 4:
      inv_txfm_ddtx_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    case 5:
      inv_txfm_fddt_size16_avx512(src, dst, shift, line, skip_line, zero_line,
                                  coef_min, coef_max);
      break;
    default: assert(0); break;
  }
}

typedef void (*inv_transform_1d_fn)(const int *src, int *dst, int shift,
                                    int line, int skip_line, int zero_line,
                                    int coef_min, int coef_max,
                                    int tx_type_index, int size_index);

// AVX-512 2D driver.
static void inv_txfm_2d_avx512(const tran_low_t *input, uint16_t *dest,
                               int stride, const TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;
  PRIMARY_TX_TYPE primary_tx_type = txfm_param->primary_tx_type;

  int width = AVMMIN(MAX_TX_SIZE >> 1, tx_size_wide[tx_size]);
  int height = AVMMIN(MAX_TX_SIZE >> 1, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AVMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AVMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_high_log2[tx_size]) - 2;

  if (width <= 0 || height <= 0) return;

  const int intermediate_bitdepth = txfm_param->bd + 8;
  const int rng_min = -(1 << (intermediate_bitdepth - 1));
  const int rng_max = (1 << (intermediate_bitdepth - 1)) - 1;

  const int col_rng_min = -(1 << txfm_param->bd);
  const int col_rng_max = (1 << txfm_param->bd) - 1;

  if (txfm_param->lossless) {
    assert(primary_tx_type == DCT_DCT);
    av2_highbd_iwht4x4_add(input, dest, stride, txfm_param->eob,
                           txfm_param->bd);
    return;
  }

  int tx_type_row = g_hor_tx_type[primary_tx_type];
  int tx_type_col = g_ver_tx_type[primary_tx_type];

  if (txfm_param->use_ddt) {
    const int use_ddt_row = (width == 4 && REPLACE_ADST4) ||
                            (width == 8 && REPLACE_ADST8) ||
                            (width == 16 && REPLACE_ADST16);
    if (use_ddt_row && (tx_type_row == DST7 || tx_type_row == DCT8)) {
      tx_type_row = (tx_type_row == DST7) ? DDTX : FDDT;
    }
    const int use_ddt_col = (height == 4 && REPLACE_ADST4) ||
                            (height == 8 && REPLACE_ADST8) ||
                            (height == 16 && REPLACE_ADST16);
    if (use_ddt_col && (tx_type_col == DST7 || tx_type_col == DCT8)) {
      tx_type_col = (tx_type_col == DST7) ? DDTX : FDDT;
    }
  }

  int block[MAX_TX_SQUARE];
  int temp[MAX_TX_SQUARE];

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;

  assert(width * height > 0);

  const __m512i vcoefmin512 = _mm512_set1_epi32(-(1 << (txfm_param->bd + 7)));
  const __m512i vcoefmax512 =
      _mm512_set1_epi32((1 << (txfm_param->bd + 7)) - 1);
  if (sqrt2) {
    const __m512i v_scale = _mm512_set1_epi64((int64_t)NewInvSqrt2);
    const __m512i v_round = _mm512_set1_epi64(1LL << (NewSqrt2Bits - 1));
    const __m128i v_shift = _mm_set1_epi64x(NewSqrt2Bits);
    for (int i = 0; i < AVMMIN(1024, width * height); i += 16) {
      __m512i data = _mm512_loadu_si512((const void *)(input + i));
      __m512i lo = _mm512_mul_epi32(data, v_scale);
      __m512i hi = _mm512_mul_epi32(_mm512_srli_epi64(data, 32), v_scale);
      lo = _mm512_srl_epi64(_mm512_add_epi64(lo, v_round), v_shift);
      hi = _mm512_srl_epi64(_mm512_add_epi64(hi, v_round), v_shift);
      lo = _mm512_and_si512(lo, _mm512_set1_epi64(0xFFFFFFFF));
      data = _mm512_or_si512(lo, _mm512_slli_epi64(hi, 32));
      data = _mm512_min_epi32(_mm512_max_epi32(data, vcoefmin512), vcoefmax512);
      _mm512_storeu_si512((void *)(block + i), data);
    }
  } else {
    for (int i = 0; i < AVMMIN(1024, width * height); i += 16) {
      __m512i data = _mm512_loadu_si512((const void *)(input + i));
      data = _mm512_min_epi32(_mm512_max_epi32(data, vcoefmin512), vcoefmax512);
      _mm512_storeu_si512((void *)(block + i), data);
    }
  }

  const int shift_1st = inv_tx_shift[tx_size][0];
  const int shift_2nd = inv_tx_shift[tx_size][1];

  assert(shift_1st >= 0);
  assert(shift_2nd >= 0);

  // At bd == 8 the clamps above bound both passes to +/-(1 << 15), so every
  // coefficient fits in int16 and the packed-int16 kernels are usable.
  const inv_transform_1d_fn inv_transform_1d =
      (txfm_param->bd == 8) ? inv_transform_1d_lowbd_avx512
                            : inv_transform_1d_avx512;

  inv_transform_1d(block, temp, shift_1st, height, 0, 0, rng_min, rng_max,
                   tx_type_row, tx_wide_index);

  inv_transform_1d(temp, block, shift_2nd, width, 0, 0, col_rng_min,
                   col_rng_max, tx_type_col, tx_high_index);

  if (width < tx_size_wide[tx_size]) {
    assert(width == 32);
    memcpy(temp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[y * 2 * width + 2 * x] = temp[y * width + x];
        block[y * 2 * width + 2 * x + 1] = temp[y * width + x];
      }
    }
    width = tx_size_wide[tx_size];
  }
  if (height < tx_size_high[tx_size]) {
    assert(height == 32);
    // Duplicate each row in place, bottom up so no source row is overwritten
    // before it is read. Explicit copies: with AVX-512 enabled the compiler
    // leaves a per-element loop scalar for 8-wide rows.
    for (int y = height - 1; y >= 0; y--) {
      const int *src = block + y * width;
      int *dst0 = block + 2 * y * width;
      int *dst1 = dst0 + width;
      if (width == 4) {
        const __m128i v = _mm_loadu_si128((const __m128i *)src);
        _mm_storeu_si128((__m128i *)dst1, v);
        _mm_storeu_si128((__m128i *)dst0, v);
      } else {
        for (int x = 0; x < width; x += 8) {
          const __m256i v = _mm256_loadu_si256((const __m256i *)(src + x));
          _mm256_storeu_si256((__m256i *)(dst1 + x), v);
          _mm256_storeu_si256((__m256i *)(dst0 + x), v);
        }
      }
    }
    height = tx_size_high[tx_size];
  }

  __m256i vpixmin = _mm256_setzero_si256();
  __m256i vpixmax = _mm256_set1_epi32((1 << txfm_param->bd) - 1);
  if ((width & 15) == 0) {
    // AVX-512: 16 pixels per iteration (widths 16 and 32). Values are clamped
    // into [0, 2^bd-1], so the int32->uint16 conversion is exact.
    const __m512i vpmin = _mm512_setzero_si512();
    const __m512i vpmax = _mm512_set1_epi32((1 << txfm_param->bd) - 1);
    for (int y = 0; y < height; y++) {
      int row_offset = y * stride;
      for (int x = 0; x < width; x += 16) {
        __m512i vdest = _mm512_cvtepu16_epi32(
            _mm256_loadu_si256((const __m256i *)&dest[row_offset + x]));
        __m512i vblock =
            _mm512_loadu_si512((const void *)&block[y * width + x]);
        __m512i vsum = _mm512_add_epi32(vdest, vblock);
        __m512i vclamped =
            _mm512_min_epi32(_mm512_max_epi32(vsum, vpmin), vpmax);
        _mm256_storeu_si256((__m256i *)&dest[row_offset + x],
                            _mm512_cvtusepi32_epi16(vclamped));
      }
    }
  } else if ((width & 7) == 0) {
    for (int y = 0; y < height; y++) {
      int row_offset = y * stride;
      for (int x = 0; x < width; x += 8) {
        __m256i vdest = _mm256_cvtepu16_epi32(
            _mm_loadu_si128((__m128i *)&dest[row_offset + x]));
        __m256i vblock = _mm256_loadu_si256((__m256i *)&block[y * width + x]);
        __m256i vsum = _mm256_add_epi32(vdest, vblock);
        __m256i vclamped =
            _mm256_min_epi32(_mm256_max_epi32(vsum, vpixmin), vpixmax);
        __m256i vresult = _mm256_permute4x64_epi64(
            _mm256_packus_epi32(vclamped, vclamped), 0xd8);
        _mm_storeu_si128((__m128i *)&dest[row_offset + x],
                         _mm256_castsi256_si128(vresult));
      }
    }
  } else {
    for (int y = 0; y < height; y += 2) {
      int row_offset = y * stride;
      for (int x = 0; x < width; x += 4) {
        __m128i vdest0 =
            _mm_cvtepu16_epi32(_mm_loadu_si64(&dest[row_offset + x]));
        __m128i vdest1 =
            _mm_cvtepu16_epi32(_mm_loadu_si64(&dest[row_offset + stride + x]));
        __m256i vdest = _mm256_set_m128i(vdest1, vdest0);
        __m256i vblock = _mm256_loadu_si256((__m256i *)&block[y * width + x]);
        __m256i vsum = _mm256_add_epi32(vdest, vblock);
        __m256i vclamped =
            _mm256_min_epi32(_mm256_max_epi32(vsum, vpixmin), vpixmax);
        __m256i vresult = _mm256_packus_epi32(vclamped, vclamped);
        __m128i vresult_lo = _mm256_extracti128_si256(vresult, 0);
        __m128i vresult_hi = _mm256_extracti128_si256(vresult, 1);
#if ARCH_X86_64
        *((int64_t *)&dest[row_offset + x]) = _mm_extract_epi64(vresult_lo, 0);
        *((int64_t *)&dest[row_offset + stride + x]) =
            _mm_extract_epi64(vresult_hi, 0);
#else
        xx_storel_64(&dest[row_offset + x], vresult_lo);
        xx_storel_64(&dest[row_offset + stride + x], vresult_hi);
#endif
      }
    }
  }
}

void av2_highbd_inv_txfm_add_avx512(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  assert(av2_ext_tx_used[txfm_param->tx_set_type][txfm_param->primary_tx_type]);
  inv_txfm(input, dest, stride, txfm_param);
}

// AVX-512 inverse DCT2, length 32 (16 lines/iter).
void inv_txfm_dct2_size32_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  if (nz_line < 16) {
    inv_txfm_dct2_size32_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  int j, k;
  int add = 1 << (shift - 1);
  const int *tx_mat = tx_kernel_dct2_size32[INV_TXFM][0];
  __m512i a[16], b[16];
  __m512i c[8], d[8];
  __m512i e[4], f[4];
  __m512i g[2], h[2];

  __m512i v_offset = _mm512_set1_epi32(add);
  __m512i v_coef_min = _mm512_set1_epi32(coef_min);
  __m512i v_coef_max = _mm512_set1_epi32(coef_max);

  __m512i s[32];

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
    // Transpose input
    transpose_load_16x16_avx512(src, s, tx1d_size);
    transpose_load_16x16_avx512(src + 16, s + 16, tx1d_size);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[1], txmat_1_0),
                            _mm512_mullo_epi32(s[3], txmat_1_1));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_2),
                            _mm512_mullo_epi32(s[7], txmat_1_3));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_4),
                            _mm512_mullo_epi32(s[11], txmat_1_5));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_6),
                            _mm512_mullo_epi32(s[15], txmat_1_7));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_8),
                            _mm512_mullo_epi32(s[19], txmat_1_9));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_10),
                            _mm512_mullo_epi32(s[23], txmat_1_11));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_12),
                            _mm512_mullo_epi32(s[27], txmat_1_13));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_14),
                            _mm512_mullo_epi32(s[31], txmat_1_15));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[0] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[1], txmat_1_1),
                            _mm512_mullo_epi32(s[3], txmat_1_4));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_7),
                            _mm512_mullo_epi32(s[7], txmat_1_10));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_13),
                            _mm512_mullo_epi32(s[11], txmat_1_15));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_12),
                            _mm512_mullo_epi32(s[15], txmat_1_9));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_6),
                            _mm512_mullo_epi32(s[19], txmat_1_3));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_0),
                            _mm512_mullo_epi32(s[23], txmat_1_2));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_5),
                            _mm512_mullo_epi32(s[27], txmat_1_8));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_11),
                            _mm512_mullo_epi32(s[31], txmat_1_14));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[1] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[1], txmat_1_2),
                            _mm512_mullo_epi32(s[3], txmat_1_7));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_12),
                            _mm512_mullo_epi32(s[7], txmat_1_14));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_9),
                            _mm512_mullo_epi32(s[11], txmat_1_4));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_0),
                            _mm512_mullo_epi32(s[15], txmat_1_5));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_10),
                            _mm512_mullo_epi32(s[19], txmat_1_15));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_11),
                            _mm512_mullo_epi32(s[23], txmat_1_6));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_1),
                            _mm512_mullo_epi32(s[27], txmat_1_3));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_8),
                            _mm512_mullo_epi32(s[31], txmat_1_13));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[2] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[1], txmat_1_3),
                            _mm512_mullo_epi32(s[3], txmat_1_10));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_14),
                            _mm512_mullo_epi32(s[7], txmat_1_7));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_0),
                            _mm512_mullo_epi32(s[11], txmat_1_6));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_13),
                            _mm512_mullo_epi32(s[15], txmat_1_11));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_4),
                            _mm512_mullo_epi32(s[19], txmat_1_2));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_9),
                            _mm512_mullo_epi32(s[23], txmat_1_15));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_8),
                            _mm512_mullo_epi32(s[27], txmat_1_1));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_5),
                            _mm512_mullo_epi32(s[31], txmat_1_12));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[3] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[1], txmat_1_4),
                            _mm512_mullo_epi32(s[3], txmat_1_13));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_9),
                            _mm512_mullo_epi32(s[7], txmat_1_0));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_8),
                            _mm512_mullo_epi32(s[11], txmat_1_14));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_5),
                            _mm512_mullo_epi32(s[15], txmat_1_3));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_12),
                            _mm512_mullo_epi32(s[19], txmat_1_10));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_1),
                            _mm512_mullo_epi32(s[23], txmat_1_7));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_15),
                            _mm512_mullo_epi32(s[27], txmat_1_6));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_2),
                            _mm512_mullo_epi32(s[31], txmat_1_11));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[4] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_5),
                            _mm512_mullo_epi32(s[3], txmat_1_15));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_4),
                            _mm512_mullo_epi32(s[7], txmat_1_6));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_14),
                            _mm512_mullo_epi32(s[11], txmat_1_3));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_7),
                            _mm512_mullo_epi32(s[15], txmat_1_13));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_2),
                            _mm512_mullo_epi32(s[19], txmat_1_8));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_12),
                            _mm512_mullo_epi32(s[23], txmat_1_1));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_9),
                            _mm512_mullo_epi32(s[27], txmat_1_11));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_0),
                            _mm512_mullo_epi32(s[31], txmat_1_10));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[5] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_6),
                            _mm512_mullo_epi32(s[3], txmat_1_12));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_0),
                            _mm512_mullo_epi32(s[7], txmat_1_13));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_5),
                            _mm512_mullo_epi32(s[11], txmat_1_7));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_11),
                            _mm512_mullo_epi32(s[15], txmat_1_1));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_14),
                            _mm512_mullo_epi32(s[19], txmat_1_4));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_8),
                            _mm512_mullo_epi32(s[23], txmat_1_10));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_2),
                            _mm512_mullo_epi32(s[27], txmat_1_15));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_3),
                            _mm512_mullo_epi32(s[31], txmat_1_9));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[6] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_7),
                            _mm512_mullo_epi32(s[3], txmat_1_9));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_5),
                            _mm512_mullo_epi32(s[7], txmat_1_11));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_3),
                            _mm512_mullo_epi32(s[11], txmat_1_13));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_1),
                            _mm512_mullo_epi32(s[15], txmat_1_15));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_0),
                            _mm512_mullo_epi32(s[19], txmat_1_14));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_2),
                            _mm512_mullo_epi32(s[23], txmat_1_12));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_4),
                            _mm512_mullo_epi32(s[27], txmat_1_10));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_6),
                            _mm512_mullo_epi32(s[31], txmat_1_8));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[7] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_8),
                            _mm512_mullo_epi32(s[3], txmat_1_6));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_10),
                            _mm512_mullo_epi32(s[7], txmat_1_4));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_12),
                            _mm512_mullo_epi32(s[11], txmat_1_2));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_14),
                            _mm512_mullo_epi32(s[15], txmat_1_0));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_15),
                            _mm512_mullo_epi32(s[19], txmat_1_1));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_13),
                            _mm512_mullo_epi32(s[23], txmat_1_3));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_11),
                            _mm512_mullo_epi32(s[27], txmat_1_5));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_9),
                            _mm512_mullo_epi32(s[31], txmat_1_7));
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[8] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_9),
                            _mm512_mullo_epi32(s[3], txmat_1_3));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_15),
                            _mm512_mullo_epi32(s[7], txmat_1_2));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_10),
                            _mm512_mullo_epi32(s[11], txmat_1_8));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_4),
                            _mm512_mullo_epi32(s[15], txmat_1_14));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_1),
                            _mm512_mullo_epi32(s[19], txmat_1_11));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_7),
                            _mm512_mullo_epi32(s[23], txmat_1_5));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_13),
                            _mm512_mullo_epi32(s[27], txmat_1_0));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_12),
                            _mm512_mullo_epi32(s[31], txmat_1_6));
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[9] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_10),
                            _mm512_mullo_epi32(s[3], txmat_1_0));
    c[1] = _mm512_add_epi32(_mm512_mullo_epi32(s[5], txmat_1_11),
                            _mm512_mullo_epi32(s[7], txmat_1_9));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_1),
                            _mm512_mullo_epi32(s[11], txmat_1_12));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_8),
                            _mm512_mullo_epi32(s[15], txmat_1_2));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_13),
                            _mm512_mullo_epi32(s[19], txmat_1_7));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_3),
                            _mm512_mullo_epi32(s[23], txmat_1_14));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_6),
                            _mm512_mullo_epi32(s[27], txmat_1_4));
    c[7] = _mm512_add_epi32(_mm512_mullo_epi32(s[29], txmat_1_15),
                            _mm512_mullo_epi32(s[31], txmat_1_5));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[10] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_11),
                            _mm512_mullo_epi32(s[3], txmat_1_2));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_6),
                            _mm512_mullo_epi32(s[7], txmat_1_15));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_7),
                            _mm512_mullo_epi32(s[11], txmat_1_1));
    c[3] = _mm512_add_epi32(_mm512_mullo_epi32(s[13], txmat_1_10),
                            _mm512_mullo_epi32(s[15], txmat_1_12));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_3),
                            _mm512_mullo_epi32(s[19], txmat_1_5));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_14),
                            _mm512_mullo_epi32(s[23], txmat_1_8));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_0),
                            _mm512_mullo_epi32(s[27], txmat_1_9));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(s[29], txmat_1_13),
                            _mm512_mullo_epi32(s[31], txmat_1_4));
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[11] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_12),
                            _mm512_mullo_epi32(s[3], txmat_1_5));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_1),
                            _mm512_mullo_epi32(s[7], txmat_1_8));
    c[2] = _mm512_add_epi32(_mm512_mullo_epi32(s[9], txmat_1_15),
                            _mm512_mullo_epi32(s[11], txmat_1_9));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_2),
                            _mm512_mullo_epi32(s[15], txmat_1_4));
    c[4] = _mm512_add_epi32(_mm512_mullo_epi32(s[17], txmat_1_11),
                            _mm512_mullo_epi32(s[19], txmat_1_13));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_6),
                            _mm512_mullo_epi32(s[23], txmat_1_0));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_7),
                            _mm512_mullo_epi32(s[27], txmat_1_14));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(s[29], txmat_1_10),
                            _mm512_mullo_epi32(s[31], txmat_1_3));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[12] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_13),
                            _mm512_mullo_epi32(s[3], txmat_1_8));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_3),
                            _mm512_mullo_epi32(s[7], txmat_1_1));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_6),
                            _mm512_mullo_epi32(s[11], txmat_1_11));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_15),
                            _mm512_mullo_epi32(s[15], txmat_1_10));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_5),
                            _mm512_mullo_epi32(s[19], txmat_1_0));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_4),
                            _mm512_mullo_epi32(s[23], txmat_1_9));
    c[6] = _mm512_add_epi32(_mm512_mullo_epi32(s[25], txmat_1_14),
                            _mm512_mullo_epi32(s[27], txmat_1_12));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(s[29], txmat_1_7),
                            _mm512_mullo_epi32(s[31], txmat_1_2));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    b[13] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_14),
                            _mm512_mullo_epi32(s[3], txmat_1_11));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_8),
                            _mm512_mullo_epi32(s[7], txmat_1_5));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_2),
                            _mm512_mullo_epi32(s[11], txmat_1_0));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_3),
                            _mm512_mullo_epi32(s[15], txmat_1_6));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_9),
                            _mm512_mullo_epi32(s[19], txmat_1_12));
    c[5] = _mm512_add_epi32(_mm512_mullo_epi32(s[21], txmat_1_15),
                            _mm512_mullo_epi32(s[23], txmat_1_13));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_10),
                            _mm512_mullo_epi32(s[27], txmat_1_7));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(s[29], txmat_1_4),
                            _mm512_mullo_epi32(s[31], txmat_1_1));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[14] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_sub_epi32(_mm512_mullo_epi32(s[1], txmat_1_15),
                            _mm512_mullo_epi32(s[3], txmat_1_14));
    c[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[5], txmat_1_13),
                            _mm512_mullo_epi32(s[7], txmat_1_12));
    c[2] = _mm512_sub_epi32(_mm512_mullo_epi32(s[9], txmat_1_11),
                            _mm512_mullo_epi32(s[11], txmat_1_10));
    c[3] = _mm512_sub_epi32(_mm512_mullo_epi32(s[13], txmat_1_9),
                            _mm512_mullo_epi32(s[15], txmat_1_8));
    c[4] = _mm512_sub_epi32(_mm512_mullo_epi32(s[17], txmat_1_7),
                            _mm512_mullo_epi32(s[19], txmat_1_6));
    c[5] = _mm512_sub_epi32(_mm512_mullo_epi32(s[21], txmat_1_5),
                            _mm512_mullo_epi32(s[23], txmat_1_4));
    c[6] = _mm512_sub_epi32(_mm512_mullo_epi32(s[25], txmat_1_3),
                            _mm512_mullo_epi32(s[27], txmat_1_2));
    c[7] = _mm512_sub_epi32(_mm512_mullo_epi32(s[29], txmat_1_1),
                            _mm512_mullo_epi32(s[31], txmat_1_0));
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    b[15] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_0);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_1);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_2);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_3);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_4);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_5);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_6);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_7);
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    d[0] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_1);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_4);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_7);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_5);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_2);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_0);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_3);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_6);
    c[0] = _mm512_add_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    d[1] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_2);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_7);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_3);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_1);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_6);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_4);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_0);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_5);
    c[0] = _mm512_sub_epi32(_mm512_add_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    d[2] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_3);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_5);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_1);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_7);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_0);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_6);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_2);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_4);
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    d[3] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_4);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_2);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_6);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_0);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_7);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_1);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_5);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_3);
    c[0] = _mm512_sub_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_add_epi32(c[6], c[7]));
    d[4] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_5);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_0);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_4);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_6);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_1);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_3);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_7);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_2);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_add_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    d[5] = _mm512_sub_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_6);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_3);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_0);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_2);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_5);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_7);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_4);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_1);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_sub_epi32(_mm512_add_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    d[6] = _mm512_add_epi32(c[0], c[4]);

    c[0] = _mm512_mullo_epi32(s[2], txmat_2_7);
    c[1] = _mm512_mullo_epi32(s[6], txmat_2_6);
    c[2] = _mm512_mullo_epi32(s[10], txmat_2_5);
    c[3] = _mm512_mullo_epi32(s[14], txmat_2_4);
    c[4] = _mm512_mullo_epi32(s[18], txmat_2_3);
    c[5] = _mm512_mullo_epi32(s[22], txmat_2_2);
    c[6] = _mm512_mullo_epi32(s[26], txmat_2_1);
    c[7] = _mm512_mullo_epi32(s[30], txmat_2_0);
    c[0] = _mm512_add_epi32(_mm512_sub_epi32(c[0], c[1]),
                            _mm512_sub_epi32(c[2], c[3]));
    c[4] = _mm512_add_epi32(_mm512_sub_epi32(c[4], c[5]),
                            _mm512_sub_epi32(c[6], c[7]));
    d[7] = _mm512_add_epi32(c[0], c[4]);

    e[0] = _mm512_mullo_epi32(s[4], txmat_4_0);
    e[1] = _mm512_mullo_epi32(s[12], txmat_4_1);
    e[2] = _mm512_mullo_epi32(s[20], txmat_4_2);
    e[3] = _mm512_mullo_epi32(s[28], txmat_4_3);
    f[0] = _mm512_add_epi32(_mm512_add_epi32(e[0], e[1]),
                            _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(s[4], txmat_4_1);
    e[1] = _mm512_mullo_epi32(s[12], txmat_4_3);
    e[2] = _mm512_mullo_epi32(s[20], txmat_4_0);
    e[3] = _mm512_mullo_epi32(s[28], txmat_4_2);
    f[1] = _mm512_sub_epi32(_mm512_sub_epi32(e[0], e[1]),
                            _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(s[4], txmat_4_2);
    e[1] = _mm512_mullo_epi32(s[12], txmat_4_0);
    e[2] = _mm512_mullo_epi32(s[20], txmat_4_3);
    e[3] = _mm512_mullo_epi32(s[28], txmat_4_1);
    f[2] = _mm512_add_epi32(_mm512_sub_epi32(e[0], e[1]),
                            _mm512_add_epi32(e[2], e[3]));

    e[0] = _mm512_mullo_epi32(s[4], txmat_4_3);
    e[1] = _mm512_mullo_epi32(s[12], txmat_4_2);
    e[2] = _mm512_mullo_epi32(s[20], txmat_4_1);
    e[3] = _mm512_mullo_epi32(s[28], txmat_4_0);
    f[3] = _mm512_add_epi32(_mm512_sub_epi32(e[0], e[1]),
                            _mm512_sub_epi32(e[2], e[3]));

    h[0] = _mm512_add_epi32(_mm512_mullo_epi32(s[8], txmat_8_0),
                            _mm512_mullo_epi32(s[24], txmat_8_1));
    h[1] = _mm512_sub_epi32(_mm512_mullo_epi32(s[8], txmat_8_1),
                            _mm512_mullo_epi32(s[24], txmat_8_0));
    g[0] = _mm512_add_epi32(s[0], s[16]);
    g[0] = _mm512_slli_epi32(g[0], 6);
    g[1] = _mm512_sub_epi32(s[0], s[16]);
    g[1] = _mm512_slli_epi32(g[1], 6);

    e[0] = _mm512_add_epi32(g[0], h[0]);
    e[3] = _mm512_sub_epi32(g[0], h[0]);
    e[1] = _mm512_add_epi32(g[1], h[1]);
    e[2] = _mm512_sub_epi32(g[1], h[1]);

    for (k = 0; k < 4; k++) {
      c[k] = _mm512_add_epi32(e[k], f[k]);
      c[k + 4] = _mm512_sub_epi32(e[3 - k], f[3 - k]);
    }
    for (k = 0; k < 8; k++) {
      a[k] = _mm512_add_epi32(c[k], d[k]);
      a[k + 8] = _mm512_sub_epi32(c[7 - k], d[7 - k]);
    }
    for (k = 0; k < 16; k++) {
      g[0] = _mm512_add_epi32(v_offset, _mm512_add_epi32(a[k], b[k]));
      g[1] = _mm512_add_epi32(v_offset, _mm512_sub_epi32(a[15 - k], b[15 - k]));

      g[0] = _mm512_srai_epi32(g[0], shift);
      g[1] = _mm512_srai_epi32(g[1], shift);

      g[0] = _mm512_min_epi32(g[0], v_coef_max);
      g[0] = _mm512_max_epi32(g[0], v_coef_min);
      g[1] = _mm512_min_epi32(g[1], v_coef_max);
      g[1] = _mm512_max_epi32(g[1], v_coef_min);

      _mm512_storeu_si512((__m512i *)(dst + k * line), g[0]);
      _mm512_storeu_si512((__m512i *)(dst + (k + 16) * line), g[1]);
    }
    src += 16 * tx1d_size;
    dst += 16;
  }
}

// Blocks of up to 64 coefficients only run AVX2 1D kernels, so hand them to the
// AVX2 driver. Keep this entry point free of the large stack frame of
// inv_txfm_2d_avx512() so these sizes run at AVX2 speed.
void inv_txfm_avx512(const tran_low_t *input, uint16_t *dest, int stride,
                     const TxfmParam *txfm_param) {
  if (tx_size_2d[txfm_param->tx_size] <= 64) {
    inv_txfm_avx2(input, dest, stride, txfm_param);
    return;
  }
  inv_txfm_2d_avx512(input, dest, stride, txfm_param);
}

// AVX-512 inverse identity transform, length 32.
void inv_txfm_idtx_size32_avx512(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line, int coef_min,
                                 int coef_max) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  if (nz_line < 16) {
    inv_txfm_idtx_size32_avx2(src, dst, shift, line, skip_line, zero_line,
                              coef_min, coef_max);
    return;
  }
  const int offset = 1 << (shift - 1);
  const __m512i v_offset = _mm512_set1_epi32(offset);
  const __m512i v_scale = _mm512_set1_epi32(362);
  const __m512i v_coef_min = _mm512_set1_epi32(coef_min);
  const __m512i v_coef_max = _mm512_set1_epi32(coef_max);
  __m512i s[32];
  for (int j = 0; j < nz_line; j += 16) {
    transpose_load_16x16_avx512(src, s, tx1d_size);
    transpose_load_16x16_avx512(src + 16, s + 16, tx1d_size);
    for (int c = 0; c < 32; c++) {
      __m512i v = _mm512_mullo_epi32(s[c], v_scale);
      v = _mm512_add_epi32(v, v_offset);
      v = _mm512_srai_epi32(v, shift);
      v = _mm512_min_epi32(v, v_coef_max);
      v = _mm512_max_epi32(v, v_coef_min);
      _mm512_storeu_si512((__m512i *)(dst + c * line), v);
    }
    src += 16 * tx1d_size;
    dst += 16;
  }
}
