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
#include <immintrin.h>

#include "config/aom_config.h"
#include "config/av1_rtcd.h"
#include "aom_dsp/x86/synonyms.h"
#include "av1/common/idct.h"
#include "av1/common/txb_common.h"

void av1_highbd_inv_txfm_add_avx2(const tran_low_t *input, uint16_t *dest,
                                  int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  inv_txfm(input, dest, stride, txfm_param);
}

// 256bit intrinsic implementation of ROUND_POWER_OF_TWO_SIGNED.
static INLINE __m256i round_power_of_two_signed_avx2(__m256i v_val_d,
                                                     int bits) {
  const __m256i v_bias_d = _mm256_set1_epi32((1 << bits) >> 1);
  const __m256i v_sign_d = _mm256_srai_epi32(v_val_d, 31);
  const __m256i v_tmp_d =
      _mm256_add_epi32(_mm256_add_epi32(v_val_d, v_bias_d), v_sign_d);
  return _mm256_srai_epi32(v_tmp_d, bits);
}

// Inverse secondary transform
void inv_stxfm_avx2(tran_low_t *src, tran_low_t *dst,
                    const PREDICTION_MODE mode, const uint8_t stx_idx,
                    const int size, const int bd) {
  assert(stx_idx < 4);
  const int32_t *kernel = (size == 0) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];

  int reduced_width, reduced_height;
  if (size == 0) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = (size == 1)
                         ? IST_8x8_HEIGHT_RED
                         : ((size == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
    reduced_width = IST_8x8_WIDTH;
  }
  for (int j = 0; j < reduced_height; j++) {
    const int32_t *kernel_tmp = kernel;
    int *srcPtr = src;
    int *out = dst;
    __m256i tmpCoeff = _mm256_set1_epi32(srcPtr[j]);
    __m256i *tmpBlock = (__m256i *)out;
    for (int i = 0; i < reduced_width; i += 8, tmpBlock++) {
      __m256i tmp = _mm256_loadu_si256((__m256i *)(kernel_tmp + i));
      __m256i sum = _mm256_loadu_si256(tmpBlock);
      tmp = _mm256_mullo_epi32(tmpCoeff, tmp);
      tmp = _mm256_add_epi32(tmp, sum);
      _mm256_storeu_si256(tmpBlock, tmp);
    }
    kernel += reduced_width;
  }
  int *out = dst;
  __m256i *tmpBlock = (__m256i *)out;
  const __m256i max_value = _mm256_set1_epi32((1 << (7 + bd)) - 1);
  const __m256i min_value = _mm256_set1_epi32(-(1 << (7 + bd)));
  for (int j = 0; j < reduced_width; j += 8, tmpBlock++) {
    __m256i tmp = _mm256_loadu_si256(tmpBlock);
    tmp = round_power_of_two_signed_avx2(tmp, 7);
    tmp = _mm256_min_epi32(_mm256_max_epi32(tmp, min_value), max_value);
    _mm256_storeu_si256(tmpBlock, tmp);
  }
}

void transpose_load_8x8_avx2(const int *src, __m256i *b, int size) {
  __m256i a[8];

  a[0] = _mm256_loadu_si256((__m256i *)(src + 0 * size));
  a[1] = _mm256_loadu_si256((__m256i *)(src + 1 * size));
  a[2] = _mm256_loadu_si256((__m256i *)(src + 2 * size));
  a[3] = _mm256_loadu_si256((__m256i *)(src + 3 * size));
  a[4] = _mm256_loadu_si256((__m256i *)(src + 4 * size));
  a[5] = _mm256_loadu_si256((__m256i *)(src + 5 * size));
  a[6] = _mm256_loadu_si256((__m256i *)(src + 6 * size));
  a[7] = _mm256_loadu_si256((__m256i *)(src + 7 * size));

  __m256i t0 =
      _mm256_unpacklo_epi32(a[0], a[1]);  // { A0 B0 A1 B1 A4 B4 A5 B5 }
  __m256i t1 =
      _mm256_unpackhi_epi32(a[0], a[1]);  // { A2 B2 A3 B3 A6 B6 A7 B7 }
  __m256i t2 =
      _mm256_unpacklo_epi32(a[2], a[3]);  // { C0 D0 C1 D1 C4 D4 C5 D5 }
  __m256i t3 =
      _mm256_unpackhi_epi32(a[2], a[3]);  // { C2 D2 C3 D3 C6 D6 C7 D7 }
  __m256i t4 =
      _mm256_unpacklo_epi32(a[4], a[5]);  // { E0 F0 E1 F1 E4 F4 E5 F5 }
  __m256i t5 =
      _mm256_unpackhi_epi32(a[4], a[5]);  // { E2 F2 E3 F3 E6 F6 E7 F7 }
  __m256i t6 =
      _mm256_unpacklo_epi32(a[6], a[7]);  // { G0 H0 G1 H1 G4 H4 G5 H5 }
  __m256i t7 =
      _mm256_unpackhi_epi32(a[6], a[7]);  // { G2 H2 G3 H3 G6 H6 G7 H7 }

  // Interleave 64-bit elements
  __m256i u0 = _mm256_unpacklo_epi64(t0, t2);  // { A0 B0 C0 D0 A4 B4 C4 D4 }
  __m256i u1 = _mm256_unpackhi_epi64(t0, t2);  // { A1 B1 C1 D1 A5 B5 C5 D5 }
  __m256i u2 = _mm256_unpacklo_epi64(t1, t3);  // { A2 B2 C2 D2 A6 B6 C6 D6 }
  __m256i u3 = _mm256_unpackhi_epi64(t1, t3);  // { A3 B3 C3 D3 A7 B7 C7 D7 }
  __m256i u4 = _mm256_unpacklo_epi64(t4, t6);  // { E0 F0 G0 H0 E4 F4 G4 H4 }
  __m256i u5 = _mm256_unpackhi_epi64(t4, t6);  // { E1 F1 G1 H1 E5 F5 G5 H5 }
  __m256i u6 = _mm256_unpacklo_epi64(t5, t7);  // { E2 F2 G2 H2 E6 F6 G6 H6 }
  __m256i u7 = _mm256_unpackhi_epi64(t5, t7);  // { E3 F3 G3 H3 E7 F7 G7 H7 }

  // Permute 128-bit lanes to complete the transpose
  b[0] =
      _mm256_permute2x128_si256(u0, u4, 0x20);  // { A0 B0 C0 D0 E0 F0 G0 H0 }
  b[1] =
      _mm256_permute2x128_si256(u1, u5, 0x20);  // { A1 B1 C1 D1 E1 F1 G1 H1 }
  b[2] =
      _mm256_permute2x128_si256(u2, u6, 0x20);  // { A2 B2 C2 D2 E2 F2 G2 H2 }
  b[3] =
      _mm256_permute2x128_si256(u3, u7, 0x20);  // { A3 B3 C3 D3 E3 F3 G3 H3 }
  b[4] =
      _mm256_permute2x128_si256(u0, u4, 0x31);  // { A4 B4 C4 D4 E4 F4 G4 H4 }
  b[5] =
      _mm256_permute2x128_si256(u1, u5, 0x31);  // { A5 B5 C5 D5 E5 F5 G5 H5 }
  b[6] =
      _mm256_permute2x128_si256(u2, u6, 0x31);  // { A6 B6 C6 D6 E6 F6 G6 H6 }
  b[7] =
      _mm256_permute2x128_si256(u3, u7, 0x31);  // { A7 B7 C7 D7 E7 F7 G7 H7 }
}

void transpose_load_8x4_sse4(const int *src, __m128i *b, int size) {
  __m128i a[8];

  a[0] = _mm_loadu_si128((__m128i *)(src + 0 * size));      // { A0 A1 A2 A3 }
  a[1] = _mm_loadu_si128((__m128i *)(src + 1 * size));      // { B0 B1 B2 B3 }
  a[2] = _mm_loadu_si128((__m128i *)(src + 2 * size));      // { C0 C1 C2 C3 }
  a[3] = _mm_loadu_si128((__m128i *)(src + 3 * size));      // { D0 D1 D2 D3 }
  a[4] = _mm_loadu_si128((__m128i *)(src + 0 * size + 4));  // { A4 A5 A6 A7 }
  a[5] = _mm_loadu_si128((__m128i *)(src + 1 * size + 4));  // { B4 B5 B6 B7 }
  a[6] = _mm_loadu_si128((__m128i *)(src + 2 * size + 4));  // { C4 C5 C6 C7 }
  a[7] = _mm_loadu_si128((__m128i *)(src + 3 * size + 4));  // { D4 D5 D6 D7 }

  // Step 1: Interleave 32-bit elements
  __m128i t0 = _mm_unpacklo_epi32(a[0], a[1]);  // { A0 B0 A1 B1 }
  __m128i t1 = _mm_unpackhi_epi32(a[0], a[1]);  // { A2 B2 A3 B3 }
  __m128i t2 = _mm_unpacklo_epi32(a[2], a[3]);  // { C0 D0 C1 D1 }
  __m128i t3 = _mm_unpackhi_epi32(a[2], a[3]);  // { C2 D2 C3 D3 }
  __m128i t4 = _mm_unpacklo_epi32(a[4], a[5]);  // { A4 B4 A5 B5 }
  __m128i t5 = _mm_unpackhi_epi32(a[4], a[5]);  // { A6 B6 A7 B7 }
  __m128i t6 = _mm_unpacklo_epi32(a[6], a[7]);  // { C4 D4 C5 D5 }
  __m128i t7 = _mm_unpackhi_epi32(a[6], a[7]);  // { C6 D6 C7 D7 }

  // Step 2: Interleave 64-bit elements
  b[0] = _mm_unpacklo_epi64(t0, t2);  // { A0 B0 C0 D0 }
  b[1] = _mm_unpackhi_epi64(t0, t2);  // { A1 B1 C1 D1 }
  b[2] = _mm_unpacklo_epi64(t1, t3);  // { A2 B2 C2 D2 }
  b[3] = _mm_unpackhi_epi64(t1, t3);  // { A3 B3 C3 D3 }
  b[4] = _mm_unpacklo_epi64(t4, t6);  // { A4 B4 C4 D4 }
  b[5] = _mm_unpackhi_epi64(t4, t6);  // { A5 B5 C5 D5 }
  b[6] = _mm_unpacklo_epi64(t5, t7);  // { A6 B6 C6 D6 }
  b[7] = _mm_unpackhi_epi64(t5, t7);  // { A7 B7 C7 D7 }
}

void transpose_load_4x8_avx2(const int *src, __m256i *b, int size) {
  __m128i a[8];

  a[0] = _mm_loadu_si128((__m128i *)(src + 0 * size));  // { A0 A1 A2 A3 }
  a[1] = _mm_loadu_si128((__m128i *)(src + 1 * size));  // { B0 B1 B2 B3 }
  a[2] = _mm_loadu_si128((__m128i *)(src + 2 * size));  // { C0 C1 C2 C3 }
  a[3] = _mm_loadu_si128((__m128i *)(src + 3 * size));  // { D0 D1 D2 D3 }
  a[4] = _mm_loadu_si128((__m128i *)(src + 4 * size));  // { E0 E1 E2 E3 }
  a[5] = _mm_loadu_si128((__m128i *)(src + 5 * size));  // { F0 F1 F2 F3 }
  a[6] = _mm_loadu_si128((__m128i *)(src + 6 * size));  // { G0 G1 G2 G3 }
  a[7] = _mm_loadu_si128((__m128i *)(src + 7 * size));  // { H0 H1 H2 H3 }

  __m128i t0 = _mm_unpacklo_epi32(a[0], a[1]);  // { A0 B0 A1 B1 }
  __m128i t1 = _mm_unpackhi_epi32(a[0], a[1]);  // { A2 B2 A3 B3 }
  __m128i t2 = _mm_unpacklo_epi32(a[2], a[3]);  // { C0 D0 C1 D1 }
  __m128i t3 = _mm_unpackhi_epi32(a[2], a[3]);  // { C2 D2 C3 D3 }
  __m128i t4 = _mm_unpacklo_epi32(a[4], a[5]);  // { E0 F0 E1 F1 }
  __m128i t5 = _mm_unpackhi_epi32(a[4], a[5]);  // { E2 F2 E3 F3 }
  __m128i t6 = _mm_unpacklo_epi32(a[6], a[7]);  // { G0 H0 G1 H1 }
  __m128i t7 = _mm_unpackhi_epi32(a[6], a[7]);  // { G2 H2 G3 H3 }

  // Interleave 64-bit elements
  __m128i u0 = _mm_unpacklo_epi64(t0, t2);  // { A0 B0 C0 D0 }
  __m128i u1 = _mm_unpackhi_epi64(t0, t2);  // { A1 B1 C1 D1 }
  __m128i u2 = _mm_unpacklo_epi64(t1, t3);  // { A2 B2 C2 D2 }
  __m128i u3 = _mm_unpackhi_epi64(t1, t3);  // { A3 B3 C3 D3 }
  __m128i u4 = _mm_unpacklo_epi64(t4, t6);  // { E0 F0 G0 H0 }
  __m128i u5 = _mm_unpackhi_epi64(t4, t6);  // { E1 F1 G1 H1 }
  __m128i u6 = _mm_unpacklo_epi64(t5, t7);  // { E2 F2 G2 H2 }
  __m128i u7 = _mm_unpackhi_epi64(t5, t7);  // { E3 F3 G3 H3 }

  // Permute 128-bit lanes to complete the transpose
  b[0] = _mm256_set_m128i(u4, u0);  // { A0 B0 C0 D0 E0 F0 G0 H0 }
  b[1] = _mm256_set_m128i(u5, u1);  // { A1 B1 C1 D1 E1 F1 G1 H1 }
  b[2] = _mm256_set_m128i(u6, u2);  // { A2 B2 C2 D2 E2 F2 G2 H2 }
  b[3] = _mm256_set_m128i(u7, u3);  // { A3 B3 C3 D3 E3 F3 G3 H3 }
}

void inv_txfm_dct2_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  int j;

  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size4[INV_TXFM][0];

  const int nz_line = line - skip_line;

  const int tx1d_size = 4;
  if (nz_line >= 8) {
    __m256i a[2], b[2];

    __m256i v_offset = _mm256_set1_epi32(add);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    __m256i s[4];

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 4 + 0]);  // 85
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 4 + 1]);  // 35

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_4x8_avx2(src, s, tx1d_size);

      b[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_0),
                              _mm256_mullo_epi32(s[3], txmat_1_1));
      b[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_1),
                              _mm256_mullo_epi32(s[3], txmat_1_0));
      a[0] = _mm256_add_epi32(s[0], s[2]);
      a[0] = _mm256_slli_epi32(a[0], 6);
      a[1] = _mm256_sub_epi32(s[0], s[2]);
      a[1] = _mm256_slli_epi32(a[1], 6);

      s[0] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[0], b[0]));
      s[0] = _mm256_srai_epi32(s[0], shift);
      s[0] = _mm256_min_epi32(s[0], v_coef_max);
      s[0] = _mm256_max_epi32(s[0], v_coef_min);
      _mm256_storeu_si256((__m256i *)(dst + 0 * line), s[0]);

      s[1] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[1], b[1]));
      s[1] = _mm256_srai_epi32(s[1], shift);
      s[1] = _mm256_min_epi32(s[1], v_coef_max);
      s[1] = _mm256_max_epi32(s[1], v_coef_min);
      _mm256_storeu_si256((__m256i *)(dst + 1 * line), s[1]);

      s[2] = _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[1], b[1]));
      s[2] = _mm256_srai_epi32(s[2], shift);
      s[2] = _mm256_min_epi32(s[2], v_coef_max);
      s[2] = _mm256_max_epi32(s[2], v_coef_min);
      _mm256_storeu_si256((__m256i *)(dst + 2 * line), s[2]);

      s[3] = _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[0], b[0]));
      s[3] = _mm256_srai_epi32(s[3], shift);
      s[3] = _mm256_min_epi32(s[3], v_coef_max);
      s[3] = _mm256_max_epi32(s[3], v_coef_min);
      _mm256_storeu_si256((__m256i *)(dst + 3 * line), s[3]);

      src += 8 * tx1d_size;
      dst += 8;
    }
  } else {
    __m256i v_offset = _mm256_set1_epi32(add);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    for (j = 0; j < nz_line; j += 2) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m128i tmp_src0 = _mm_set1_epi32(src[(j + 0) * tx1d_size + k]);
        __m128i tmp_src1 = _mm_set1_epi32(src[(j + 1) * tx1d_size + k]);
        __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

        __m128i tmp_val0 = _mm_loadu_si128((__m128i *)(tx_mat + k * tx1d_size));
        __m256i tmp_val = _mm256_set_m128i(tmp_val0, tmp_val0);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }

      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Clamp to coef_min and coef_max
      sum = _mm256_min_epi32(sum, v_coef_max);
      sum = _mm256_max_epi32(sum, v_coef_min);

      // Store results
      dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
      dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
      dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
      dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
      dst[0 * line + j + 1] = _mm256_extract_epi32(sum, 4);
      dst[1 * line + j + 1] = _mm256_extract_epi32(sum, 5);
      dst[2 * line + j + 1] = _mm256_extract_epi32(sum, 6);
      dst[3 * line + j + 1] = _mm256_extract_epi32(sum, 7);
    }
  }
}

void inv_txfm_dct2_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  int j, k;

  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size8[INV_TXFM][0];

  const int nz_line = line - skip_line;

  const int tx1d_size = 8;
  if (nz_line >= 8) {
    __m256i a[4], b[4];
    __m256i c[2], d[2];

    __m256i v_offset = _mm256_set1_epi32(add);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    __m256i s[8];

    __m256i txmat_2_0 = _mm256_set1_epi32(tx_mat[2 * 8 + 0]);  // 85
    __m256i txmat_2_1 = _mm256_set1_epi32(tx_mat[2 * 8 + 1]);  // 35

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 8 + 0]);  // 89
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 8 + 1]);  // 75
    __m256i txmat_1_2 = _mm256_set1_epi32(tx_mat[1 * 8 + 2]);  // 50
    __m256i txmat_1_3 = _mm256_set1_epi32(tx_mat[1 * 8 + 3]);  // 18

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_8x8_avx2(src, s, tx1d_size);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_0);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_1);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_2);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_3);
      b[0] = _mm256_add_epi32(_mm256_add_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_1);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_3);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_0);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_2);
      b[1] = _mm256_sub_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_2);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_0);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_3);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_1);
      b[2] = _mm256_add_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_3);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_2);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_1);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_0);
      b[3] = _mm256_add_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));

      d[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_0),
                              _mm256_mullo_epi32(s[6], txmat_2_1));
      d[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_1),
                              _mm256_mullo_epi32(s[6], txmat_2_0));
      c[0] = _mm256_add_epi32(s[0], s[4]);
      c[0] = _mm256_slli_epi32(c[0], 6);
      c[1] = _mm256_sub_epi32(s[0], s[4]);
      c[1] = _mm256_slli_epi32(c[1], 6);

      a[0] = _mm256_add_epi32(c[0], d[0]);
      a[3] = _mm256_sub_epi32(c[0], d[0]);
      a[1] = _mm256_add_epi32(c[1], d[1]);
      a[2] = _mm256_sub_epi32(c[1], d[1]);

      for (k = 0; k < 4; k++) {
        c[0] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[k], b[k]));
        c[1] = _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[3 - k], b[3 - k]));

        c[0] = _mm256_srai_epi32(c[0], shift);
        c[1] = _mm256_srai_epi32(c[1], shift);

        c[0] = _mm256_min_epi32(c[0], v_coef_max);
        c[0] = _mm256_max_epi32(c[0], v_coef_min);
        c[1] = _mm256_min_epi32(c[1], v_coef_max);
        c[1] = _mm256_max_epi32(c[1], v_coef_min);

        _mm256_storeu_si256((__m256i *)(dst + k * line), c[0]);
        _mm256_storeu_si256((__m256i *)(dst + (k + 4) * line), c[1]);
      }
      src += 8 * tx1d_size;
      dst += 8;
    }
  } else {
    __m128i a[4], b[4];
    __m128i c[2], d[2];

    __m128i v_offset = _mm_set1_epi32(add);
    __m128i v_coef_min = _mm_set1_epi32(coef_min);
    __m128i v_coef_max = _mm_set1_epi32(coef_max);

    __m128i s[8];

    __m128i txmat_2_0 = _mm_set1_epi32(tx_mat[2 * 8 + 0]);  // 85
    __m128i txmat_2_1 = _mm_set1_epi32(tx_mat[2 * 8 + 1]);  // 35

    __m128i txmat_1_0 = _mm_set1_epi32(tx_mat[1 * 8 + 0]);  // 89
    __m128i txmat_1_1 = _mm_set1_epi32(tx_mat[1 * 8 + 1]);  // 75
    __m128i txmat_1_2 = _mm_set1_epi32(tx_mat[1 * 8 + 2]);  // 50
    __m128i txmat_1_3 = _mm_set1_epi32(tx_mat[1 * 8 + 3]);  // 18

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_8x4_sse4(src, s, tx1d_size);

      a[0] = _mm_mullo_epi32(s[1], txmat_1_0);
      a[1] = _mm_mullo_epi32(s[3], txmat_1_1);
      a[2] = _mm_mullo_epi32(s[5], txmat_1_2);
      a[3] = _mm_mullo_epi32(s[7], txmat_1_3);
      b[0] =
          _mm_add_epi32(_mm_add_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));

      a[0] = _mm_mullo_epi32(s[1], txmat_1_1);
      a[1] = _mm_mullo_epi32(s[3], txmat_1_3);
      a[2] = _mm_mullo_epi32(s[5], txmat_1_0);
      a[3] = _mm_mullo_epi32(s[7], txmat_1_2);
      b[1] =
          _mm_sub_epi32(_mm_sub_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));

      a[0] = _mm_mullo_epi32(s[1], txmat_1_2);
      a[1] = _mm_mullo_epi32(s[3], txmat_1_0);
      a[2] = _mm_mullo_epi32(s[5], txmat_1_3);
      a[3] = _mm_mullo_epi32(s[7], txmat_1_1);
      b[2] =
          _mm_add_epi32(_mm_sub_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));

      a[0] = _mm_mullo_epi32(s[1], txmat_1_3);
      a[1] = _mm_mullo_epi32(s[3], txmat_1_2);
      a[2] = _mm_mullo_epi32(s[5], txmat_1_1);
      a[3] = _mm_mullo_epi32(s[7], txmat_1_0);
      b[3] =
          _mm_add_epi32(_mm_sub_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));

      d[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_0),
                           _mm_mullo_epi32(s[6], txmat_2_1));
      d[1] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_1),
                           _mm_mullo_epi32(s[6], txmat_2_0));
      c[0] = _mm_add_epi32(s[0], s[4]);
      c[0] = _mm_slli_epi32(c[0], 6);
      c[1] = _mm_sub_epi32(s[0], s[4]);
      c[1] = _mm_slli_epi32(c[1], 6);

      a[0] = _mm_add_epi32(c[0], d[0]);
      a[3] = _mm_sub_epi32(c[0], d[0]);
      a[1] = _mm_add_epi32(c[1], d[1]);
      a[2] = _mm_sub_epi32(c[1], d[1]);

      for (k = 0; k < 4; k++) {
        c[0] = _mm_add_epi32(v_offset, _mm_add_epi32(a[k], b[k]));
        c[1] = _mm_add_epi32(v_offset, _mm_sub_epi32(a[3 - k], b[3 - k]));

        c[0] = _mm_srai_epi32(c[0], shift);
        c[1] = _mm_srai_epi32(c[1], shift);

        c[0] = _mm_min_epi32(c[0], v_coef_max);
        c[0] = _mm_max_epi32(c[0], v_coef_min);
        c[1] = _mm_min_epi32(c[1], v_coef_max);
        c[1] = _mm_max_epi32(c[1], v_coef_min);

        _mm_storeu_si128((__m128i *)(dst + k * line), c[0]);
        _mm_storeu_si128((__m128i *)(dst + (k + 4) * line), c[1]);
      }
      src += 8 * tx1d_size;
      dst += 8;
    }
  }
}

void inv_txfm_dct2_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  int j, k;
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size16[INV_TXFM][0];

  const int nz_line = line - skip_line;

  const int tx1d_size = 16;
  if (nz_line >= 8) {
    __m256i a[8], b[8];
    __m256i c[4], d[4];
    __m256i e[2], f[2];

    __m256i v_offset = _mm256_set1_epi32(add);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    __m256i s[16];

    __m256i txmat_4_0 = _mm256_set1_epi32(tx_mat[4 * 16 + 0]);  // 85
    __m256i txmat_4_1 = _mm256_set1_epi32(tx_mat[4 * 16 + 1]);  // 35

    __m256i txmat_2_0 = _mm256_set1_epi32(tx_mat[2 * 16 + 0]);  // 89
    __m256i txmat_2_1 = _mm256_set1_epi32(tx_mat[2 * 16 + 1]);  // 75
    __m256i txmat_2_2 = _mm256_set1_epi32(tx_mat[2 * 16 + 2]);  // 50
    __m256i txmat_2_3 = _mm256_set1_epi32(tx_mat[2 * 16 + 3]);  // 18

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 16 + 0]);  // 90
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 16 + 1]);  // 87
    __m256i txmat_1_2 = _mm256_set1_epi32(tx_mat[1 * 16 + 2]);  // 80
    __m256i txmat_1_3 = _mm256_set1_epi32(tx_mat[1 * 16 + 3]);  // 70
    __m256i txmat_1_4 = _mm256_set1_epi32(tx_mat[1 * 16 + 4]);  // 57
    __m256i txmat_1_5 = _mm256_set1_epi32(tx_mat[1 * 16 + 5]);  // 43
    __m256i txmat_1_6 = _mm256_set1_epi32(tx_mat[1 * 16 + 6]);  // 26
    __m256i txmat_1_7 = _mm256_set1_epi32(tx_mat[1 * 16 + 7]);  //  9

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_8x8_avx2(src, s, tx1d_size);
      transpose_load_8x8_avx2(src + 8, s + 8, tx1d_size);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_0);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_1);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_2);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_3);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_4);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_5);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_6);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_7);
      a[0] = _mm256_add_epi32(_mm256_add_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));
      a[4] = _mm256_add_epi32(_mm256_add_epi32(a[4], a[5]),
                              _mm256_add_epi32(a[6], a[7]));
      b[0] = _mm256_add_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_1);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_4);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_7);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_5);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_2);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_0);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_3);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_6);
      a[0] = _mm256_add_epi32(_mm256_add_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));
      a[4] = _mm256_add_epi32(_mm256_add_epi32(a[4], a[5]),
                              _mm256_add_epi32(a[6], a[7]));
      b[1] = _mm256_sub_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_2);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_7);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_3);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_1);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_6);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_4);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_0);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_5);
      a[0] = _mm256_sub_epi32(_mm256_add_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));
      a[4] = _mm256_sub_epi32(_mm256_sub_epi32(a[4], a[5]),
                              _mm256_add_epi32(a[6], a[7]));
      b[2] = _mm256_sub_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_3);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_5);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_1);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_7);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_0);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_6);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_2);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_4);
      a[0] = _mm256_sub_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));
      a[4] = _mm256_sub_epi32(_mm256_add_epi32(a[4], a[5]),
                              _mm256_add_epi32(a[6], a[7]));
      b[3] = _mm256_add_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_4);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_2);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_6);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_0);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_7);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_1);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_5);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_3);
      a[0] = _mm256_sub_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));
      a[4] = _mm256_sub_epi32(_mm256_add_epi32(a[4], a[5]),
                              _mm256_add_epi32(a[6], a[7]));
      b[4] = _mm256_sub_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_5);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_0);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_4);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_6);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_1);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_3);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_7);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_2);
      a[0] = _mm256_add_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_add_epi32(a[2], a[3]));
      a[4] = _mm256_sub_epi32(_mm256_sub_epi32(a[4], a[5]),
                              _mm256_sub_epi32(a[6], a[7]));
      b[5] = _mm256_sub_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_6);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_3);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_0);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_2);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_5);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_7);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_4);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_1);
      a[0] = _mm256_add_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));
      a[4] = _mm256_sub_epi32(_mm256_add_epi32(a[4], a[5]),
                              _mm256_sub_epi32(a[6], a[7]));
      b[6] = _mm256_add_epi32(a[0], a[4]);

      a[0] = _mm256_mullo_epi32(s[1], txmat_1_7);
      a[1] = _mm256_mullo_epi32(s[3], txmat_1_6);
      a[2] = _mm256_mullo_epi32(s[5], txmat_1_5);
      a[3] = _mm256_mullo_epi32(s[7], txmat_1_4);
      a[4] = _mm256_mullo_epi32(s[9], txmat_1_3);
      a[5] = _mm256_mullo_epi32(s[11], txmat_1_2);
      a[6] = _mm256_mullo_epi32(s[13], txmat_1_1);
      a[7] = _mm256_mullo_epi32(s[15], txmat_1_0);
      a[0] = _mm256_add_epi32(_mm256_sub_epi32(a[0], a[1]),
                              _mm256_sub_epi32(a[2], a[3]));
      a[4] = _mm256_add_epi32(_mm256_sub_epi32(a[4], a[5]),
                              _mm256_sub_epi32(a[6], a[7]));
      b[7] = _mm256_add_epi32(a[0], a[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_0);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_1);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_2);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_3);
      d[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_1);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_3);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_0);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_2);
      d[1] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_2);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_0);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_3);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_1);
      d[2] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_3);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_2);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_1);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_0);
      d[3] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      f[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[4], txmat_4_0),
                              _mm256_mullo_epi32(s[12], txmat_4_1));
      f[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_1),
                              _mm256_mullo_epi32(s[12], txmat_4_0));
      e[0] = _mm256_add_epi32(s[0], s[8]);
      e[0] = _mm256_slli_epi32(e[0], 6);
      e[1] = _mm256_sub_epi32(s[0], s[8]);
      e[1] = _mm256_slli_epi32(e[1], 6);

      c[0] = _mm256_add_epi32(e[0], f[0]);
      c[3] = _mm256_sub_epi32(e[0], f[0]);
      c[1] = _mm256_add_epi32(e[1], f[1]);
      c[2] = _mm256_sub_epi32(e[1], f[1]);

      for (k = 0; k < 4; k++) {
        a[k] = _mm256_add_epi32(c[k], d[k]);
        a[k + 4] = _mm256_sub_epi32(c[3 - k], d[3 - k]);
      }
      for (k = 0; k < 8; k++) {
        e[0] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[k], b[k]));
        e[1] = _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[7 - k], b[7 - k]));

        e[0] = _mm256_srai_epi32(e[0], shift);
        e[1] = _mm256_srai_epi32(e[1], shift);

        e[0] = _mm256_min_epi32(e[0], v_coef_max);
        e[0] = _mm256_max_epi32(e[0], v_coef_min);
        e[1] = _mm256_min_epi32(e[1], v_coef_max);
        e[1] = _mm256_max_epi32(e[1], v_coef_min);

        _mm256_storeu_si256((__m256i *)(dst + k * line), e[0]);
        _mm256_storeu_si256((__m256i *)(dst + (k + 8) * line), e[1]);
      }
      src += 8 * tx1d_size;
      dst += 8;
    }
  } else {
    __m128i a[8], b[8];
    __m128i c[4], d[4];
    __m128i e[2], f[2];

    __m128i v_offset = _mm_set1_epi32(add);
    __m128i v_coef_min = _mm_set1_epi32(coef_min);
    __m128i v_coef_max = _mm_set1_epi32(coef_max);

    __m128i s[16];

    __m128i txmat_4_0 = _mm_set1_epi32(tx_mat[4 * 16 + 0]);  // 85
    __m128i txmat_4_1 = _mm_set1_epi32(tx_mat[4 * 16 + 1]);  // 35

    __m128i txmat_2_0 = _mm_set1_epi32(tx_mat[2 * 16 + 0]);  // 89
    __m128i txmat_2_1 = _mm_set1_epi32(tx_mat[2 * 16 + 1]);  // 75
    __m128i txmat_2_2 = _mm_set1_epi32(tx_mat[2 * 16 + 2]);  // 50
    __m128i txmat_2_3 = _mm_set1_epi32(tx_mat[2 * 16 + 3]);  // 18

    __m128i txmat_1_0 = _mm_set1_epi32(tx_mat[1 * 16 + 0]);  // 90
    __m128i txmat_1_1 = _mm_set1_epi32(tx_mat[1 * 16 + 1]);  // 87
    __m128i txmat_1_2 = _mm_set1_epi32(tx_mat[1 * 16 + 2]);  // 80
    __m128i txmat_1_3 = _mm_set1_epi32(tx_mat[1 * 16 + 3]);  // 70
    __m128i txmat_1_4 = _mm_set1_epi32(tx_mat[1 * 16 + 4]);  // 57
    __m128i txmat_1_5 = _mm_set1_epi32(tx_mat[1 * 16 + 5]);  // 43
    __m128i txmat_1_6 = _mm_set1_epi32(tx_mat[1 * 16 + 6]);  // 26
    __m128i txmat_1_7 = _mm_set1_epi32(tx_mat[1 * 16 + 7]);  //  9

    // Transpose input
    transpose_load_8x4_sse4(src, s, tx1d_size);
    transpose_load_8x4_sse4(src + 8, s + 8, tx1d_size);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_0);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_1);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_2);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_3);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_4);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_5);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_6);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_7);
    a[0] = _mm_add_epi32(_mm_add_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));
    a[4] = _mm_add_epi32(_mm_add_epi32(a[4], a[5]), _mm_add_epi32(a[6], a[7]));
    b[0] = _mm_add_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_1);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_4);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_7);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_5);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_2);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_0);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_3);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_6);
    a[0] = _mm_add_epi32(_mm_add_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));
    a[4] = _mm_add_epi32(_mm_add_epi32(a[4], a[5]), _mm_add_epi32(a[6], a[7]));
    b[1] = _mm_sub_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_2);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_7);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_3);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_1);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_6);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_4);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_0);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_5);
    a[0] = _mm_sub_epi32(_mm_add_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));
    a[4] = _mm_sub_epi32(_mm_sub_epi32(a[4], a[5]), _mm_add_epi32(a[6], a[7]));
    b[2] = _mm_sub_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_3);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_5);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_1);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_7);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_0);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_6);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_2);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_4);
    a[0] = _mm_sub_epi32(_mm_sub_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));
    a[4] = _mm_sub_epi32(_mm_add_epi32(a[4], a[5]), _mm_add_epi32(a[6], a[7]));
    b[3] = _mm_add_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_4);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_2);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_6);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_0);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_7);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_1);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_5);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_3);
    a[0] = _mm_sub_epi32(_mm_sub_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));
    a[4] = _mm_sub_epi32(_mm_add_epi32(a[4], a[5]), _mm_add_epi32(a[6], a[7]));
    b[4] = _mm_sub_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_5);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_0);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_4);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_6);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_1);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_3);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_7);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_2);
    a[0] = _mm_add_epi32(_mm_sub_epi32(a[0], a[1]), _mm_add_epi32(a[2], a[3]));
    a[4] = _mm_sub_epi32(_mm_sub_epi32(a[4], a[5]), _mm_sub_epi32(a[6], a[7]));
    b[5] = _mm_sub_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_6);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_3);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_0);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_2);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_5);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_7);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_4);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_1);
    a[0] = _mm_add_epi32(_mm_sub_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));
    a[4] = _mm_sub_epi32(_mm_add_epi32(a[4], a[5]), _mm_sub_epi32(a[6], a[7]));
    b[6] = _mm_add_epi32(a[0], a[4]);

    a[0] = _mm_mullo_epi32(s[1], txmat_1_7);
    a[1] = _mm_mullo_epi32(s[3], txmat_1_6);
    a[2] = _mm_mullo_epi32(s[5], txmat_1_5);
    a[3] = _mm_mullo_epi32(s[7], txmat_1_4);
    a[4] = _mm_mullo_epi32(s[9], txmat_1_3);
    a[5] = _mm_mullo_epi32(s[11], txmat_1_2);
    a[6] = _mm_mullo_epi32(s[13], txmat_1_1);
    a[7] = _mm_mullo_epi32(s[15], txmat_1_0);
    a[0] = _mm_add_epi32(_mm_sub_epi32(a[0], a[1]), _mm_sub_epi32(a[2], a[3]));
    a[4] = _mm_add_epi32(_mm_sub_epi32(a[4], a[5]), _mm_sub_epi32(a[6], a[7]));
    b[7] = _mm_add_epi32(a[0], a[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_0);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_1);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_2);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_3);
    d[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_mullo_epi32(s[2], txmat_2_1);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_3);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_0);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_2);
    d[1] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_mullo_epi32(s[2], txmat_2_2);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_0);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_3);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_1);
    d[2] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_mullo_epi32(s[2], txmat_2_3);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_2);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_1);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_0);
    d[3] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    f[0] = _mm_add_epi32(_mm_mullo_epi32(s[4], txmat_4_0),
                         _mm_mullo_epi32(s[12], txmat_4_1));
    f[1] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_1),
                         _mm_mullo_epi32(s[12], txmat_4_0));
    e[0] = _mm_add_epi32(s[0], s[8]);
    e[0] = _mm_slli_epi32(e[0], 6);
    e[1] = _mm_sub_epi32(s[0], s[8]);
    e[1] = _mm_slli_epi32(e[1], 6);

    c[0] = _mm_add_epi32(e[0], f[0]);
    c[3] = _mm_sub_epi32(e[0], f[0]);
    c[1] = _mm_add_epi32(e[1], f[1]);
    c[2] = _mm_sub_epi32(e[1], f[1]);

    for (k = 0; k < 4; k++) {
      a[k] = _mm_add_epi32(c[k], d[k]);
      a[k + 4] = _mm_sub_epi32(c[3 - k], d[3 - k]);
    }
    for (k = 0; k < 8; k++) {
      e[0] = _mm_add_epi32(v_offset, _mm_add_epi32(a[k], b[k]));
      e[1] = _mm_add_epi32(v_offset, _mm_sub_epi32(a[7 - k], b[7 - k]));

      e[0] = _mm_srai_epi32(e[0], shift);
      e[1] = _mm_srai_epi32(e[1], shift);

      e[0] = _mm_min_epi32(e[0], v_coef_max);
      e[0] = _mm_max_epi32(e[0], v_coef_min);
      e[1] = _mm_min_epi32(e[1], v_coef_max);
      e[1] = _mm_max_epi32(e[1], v_coef_min);

      _mm_storeu_si128((__m128i *)(dst + k * line), e[0]);
      _mm_storeu_si128((__m128i *)(dst + (k + 8) * line), e[1]);
    }
  }
}

void inv_txfm_dct2_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  int j, k;
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size32[INV_TXFM][0];

  const int nz_line = line - skip_line;

  const int tx1d_size = 32;
  if (nz_line >= 8) {
    __m256i a[16], b[16];
    __m256i c[8], d[8];
    __m256i e[4], f[4];
    __m256i g[2], h[2];

    __m256i v_offset = _mm256_set1_epi32(add);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    __m256i s[32];

    __m256i txmat_8_0 = _mm256_set1_epi32(tx_mat[8 * 32 + 0]);  // 85
    __m256i txmat_8_1 = _mm256_set1_epi32(tx_mat[8 * 32 + 1]);  // 35

    __m256i txmat_4_0 = _mm256_set1_epi32(tx_mat[4 * 32 + 0]);  // 89
    __m256i txmat_4_1 = _mm256_set1_epi32(tx_mat[4 * 32 + 1]);  // 75
    __m256i txmat_4_2 = _mm256_set1_epi32(tx_mat[4 * 32 + 2]);  // 50
    __m256i txmat_4_3 = _mm256_set1_epi32(tx_mat[4 * 32 + 3]);  // 18

    __m256i txmat_2_0 = _mm256_set1_epi32(tx_mat[2 * 32 + 0]);  // 90
    __m256i txmat_2_1 = _mm256_set1_epi32(tx_mat[2 * 32 + 1]);  // 87
    __m256i txmat_2_2 = _mm256_set1_epi32(tx_mat[2 * 32 + 2]);  // 80
    __m256i txmat_2_3 = _mm256_set1_epi32(tx_mat[2 * 32 + 3]);  // 70
    __m256i txmat_2_4 = _mm256_set1_epi32(tx_mat[2 * 32 + 4]);  // 57
    __m256i txmat_2_5 = _mm256_set1_epi32(tx_mat[2 * 32 + 5]);  // 43
    __m256i txmat_2_6 = _mm256_set1_epi32(tx_mat[2 * 32 + 6]);  // 26
    __m256i txmat_2_7 = _mm256_set1_epi32(tx_mat[2 * 32 + 7]);  //  9

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 32 + 0]);    // 90
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 32 + 1]);    // 90
    __m256i txmat_1_2 = _mm256_set1_epi32(tx_mat[1 * 32 + 2]);    // 88
    __m256i txmat_1_3 = _mm256_set1_epi32(tx_mat[1 * 32 + 3]);    // 85
    __m256i txmat_1_4 = _mm256_set1_epi32(tx_mat[1 * 32 + 4]);    // 82
    __m256i txmat_1_5 = _mm256_set1_epi32(tx_mat[1 * 32 + 5]);    // 78
    __m256i txmat_1_6 = _mm256_set1_epi32(tx_mat[1 * 32 + 6]);    // 73
    __m256i txmat_1_7 = _mm256_set1_epi32(tx_mat[1 * 32 + 7]);    // 67
    __m256i txmat_1_8 = _mm256_set1_epi32(tx_mat[1 * 32 + 8]);    // 61
    __m256i txmat_1_9 = _mm256_set1_epi32(tx_mat[1 * 32 + 9]);    // 54
    __m256i txmat_1_10 = _mm256_set1_epi32(tx_mat[1 * 32 + 10]);  // 47
    __m256i txmat_1_11 = _mm256_set1_epi32(tx_mat[1 * 32 + 11]);  // 39
    __m256i txmat_1_12 = _mm256_set1_epi32(tx_mat[1 * 32 + 12]);  // 30
    __m256i txmat_1_13 = _mm256_set1_epi32(tx_mat[1 * 32 + 13]);  // 22
    __m256i txmat_1_14 = _mm256_set1_epi32(tx_mat[1 * 32 + 14]);  // 13
    __m256i txmat_1_15 = _mm256_set1_epi32(tx_mat[1 * 32 + 15]);  //  4

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_8x8_avx2(src, s, tx1d_size);
      transpose_load_8x8_avx2(src + 8, s + 8, tx1d_size);
      transpose_load_8x8_avx2(src + 16, s + 16, tx1d_size);
      transpose_load_8x8_avx2(src + 24, s + 24, tx1d_size);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_0),
                              _mm256_mullo_epi32(s[3], txmat_1_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_2),
                              _mm256_mullo_epi32(s[7], txmat_1_3));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_4),
                              _mm256_mullo_epi32(s[11], txmat_1_5));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_6),
                              _mm256_mullo_epi32(s[15], txmat_1_7));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_8),
                              _mm256_mullo_epi32(s[19], txmat_1_9));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_10),
                              _mm256_mullo_epi32(s[23], txmat_1_11));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_12),
                              _mm256_mullo_epi32(s[27], txmat_1_13));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_14),
                              _mm256_mullo_epi32(s[31], txmat_1_15));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[0] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_1),
                              _mm256_mullo_epi32(s[3], txmat_1_4));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_7),
                              _mm256_mullo_epi32(s[7], txmat_1_10));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_13),
                              _mm256_mullo_epi32(s[11], txmat_1_15));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_12),
                              _mm256_mullo_epi32(s[15], txmat_1_9));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_6),
                              _mm256_mullo_epi32(s[19], txmat_1_3));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_0),
                              _mm256_mullo_epi32(s[23], txmat_1_2));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_5),
                              _mm256_mullo_epi32(s[27], txmat_1_8));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_11),
                              _mm256_mullo_epi32(s[31], txmat_1_14));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[1] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_2),
                              _mm256_mullo_epi32(s[3], txmat_1_7));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_12),
                              _mm256_mullo_epi32(s[7], txmat_1_14));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_9),
                              _mm256_mullo_epi32(s[11], txmat_1_4));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_0),
                              _mm256_mullo_epi32(s[15], txmat_1_5));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_10),
                              _mm256_mullo_epi32(s[19], txmat_1_15));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_11),
                              _mm256_mullo_epi32(s[23], txmat_1_6));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_1),
                              _mm256_mullo_epi32(s[27], txmat_1_3));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_8),
                              _mm256_mullo_epi32(s[31], txmat_1_13));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[2] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_3),
                              _mm256_mullo_epi32(s[3], txmat_1_10));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_14),
                              _mm256_mullo_epi32(s[7], txmat_1_7));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_0),
                              _mm256_mullo_epi32(s[11], txmat_1_6));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_13),
                              _mm256_mullo_epi32(s[15], txmat_1_11));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_4),
                              _mm256_mullo_epi32(s[19], txmat_1_2));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_9),
                              _mm256_mullo_epi32(s[23], txmat_1_15));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_8),
                              _mm256_mullo_epi32(s[27], txmat_1_1));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_5),
                              _mm256_mullo_epi32(s[31], txmat_1_12));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[3] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_4),
                              _mm256_mullo_epi32(s[3], txmat_1_13));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_9),
                              _mm256_mullo_epi32(s[7], txmat_1_0));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_8),
                              _mm256_mullo_epi32(s[11], txmat_1_14));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_5),
                              _mm256_mullo_epi32(s[15], txmat_1_3));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_12),
                              _mm256_mullo_epi32(s[19], txmat_1_10));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_1),
                              _mm256_mullo_epi32(s[23], txmat_1_7));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_15),
                              _mm256_mullo_epi32(s[27], txmat_1_6));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_2),
                              _mm256_mullo_epi32(s[31], txmat_1_11));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[4] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_5),
                              _mm256_mullo_epi32(s[3], txmat_1_15));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_4),
                              _mm256_mullo_epi32(s[7], txmat_1_6));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_14),
                              _mm256_mullo_epi32(s[11], txmat_1_3));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_7),
                              _mm256_mullo_epi32(s[15], txmat_1_13));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_2),
                              _mm256_mullo_epi32(s[19], txmat_1_8));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_12),
                              _mm256_mullo_epi32(s[23], txmat_1_1));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_9),
                              _mm256_mullo_epi32(s[27], txmat_1_11));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_0),
                              _mm256_mullo_epi32(s[31], txmat_1_10));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[5] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_6),
                              _mm256_mullo_epi32(s[3], txmat_1_12));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_0),
                              _mm256_mullo_epi32(s[7], txmat_1_13));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_5),
                              _mm256_mullo_epi32(s[11], txmat_1_7));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_11),
                              _mm256_mullo_epi32(s[15], txmat_1_1));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_14),
                              _mm256_mullo_epi32(s[19], txmat_1_4));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_8),
                              _mm256_mullo_epi32(s[23], txmat_1_10));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_2),
                              _mm256_mullo_epi32(s[27], txmat_1_15));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_3),
                              _mm256_mullo_epi32(s[31], txmat_1_9));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[6] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_7),
                              _mm256_mullo_epi32(s[3], txmat_1_9));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_5),
                              _mm256_mullo_epi32(s[7], txmat_1_11));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_3),
                              _mm256_mullo_epi32(s[11], txmat_1_13));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_1),
                              _mm256_mullo_epi32(s[15], txmat_1_15));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_0),
                              _mm256_mullo_epi32(s[19], txmat_1_14));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_2),
                              _mm256_mullo_epi32(s[23], txmat_1_12));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_4),
                              _mm256_mullo_epi32(s[27], txmat_1_10));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_6),
                              _mm256_mullo_epi32(s[31], txmat_1_8));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[7] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_8),
                              _mm256_mullo_epi32(s[3], txmat_1_6));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_10),
                              _mm256_mullo_epi32(s[7], txmat_1_4));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_12),
                              _mm256_mullo_epi32(s[11], txmat_1_2));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_14),
                              _mm256_mullo_epi32(s[15], txmat_1_0));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_15),
                              _mm256_mullo_epi32(s[19], txmat_1_1));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_13),
                              _mm256_mullo_epi32(s[23], txmat_1_3));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_11),
                              _mm256_mullo_epi32(s[27], txmat_1_5));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_9),
                              _mm256_mullo_epi32(s[31], txmat_1_7));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[8] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_9),
                              _mm256_mullo_epi32(s[3], txmat_1_3));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_15),
                              _mm256_mullo_epi32(s[7], txmat_1_2));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_10),
                              _mm256_mullo_epi32(s[11], txmat_1_8));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_4),
                              _mm256_mullo_epi32(s[15], txmat_1_14));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_1),
                              _mm256_mullo_epi32(s[19], txmat_1_11));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_7),
                              _mm256_mullo_epi32(s[23], txmat_1_5));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_13),
                              _mm256_mullo_epi32(s[27], txmat_1_0));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_12),
                              _mm256_mullo_epi32(s[31], txmat_1_6));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[9] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_10),
                              _mm256_mullo_epi32(s[3], txmat_1_0));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_11),
                              _mm256_mullo_epi32(s[7], txmat_1_9));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_1),
                              _mm256_mullo_epi32(s[11], txmat_1_12));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_8),
                              _mm256_mullo_epi32(s[15], txmat_1_2));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_13),
                              _mm256_mullo_epi32(s[19], txmat_1_7));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_3),
                              _mm256_mullo_epi32(s[23], txmat_1_14));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_6),
                              _mm256_mullo_epi32(s[27], txmat_1_4));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_15),
                              _mm256_mullo_epi32(s[31], txmat_1_5));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[10] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_11),
                              _mm256_mullo_epi32(s[3], txmat_1_2));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_6),
                              _mm256_mullo_epi32(s[7], txmat_1_15));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_7),
                              _mm256_mullo_epi32(s[11], txmat_1_1));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_10),
                              _mm256_mullo_epi32(s[15], txmat_1_12));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_3),
                              _mm256_mullo_epi32(s[19], txmat_1_5));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_14),
                              _mm256_mullo_epi32(s[23], txmat_1_8));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_0),
                              _mm256_mullo_epi32(s[27], txmat_1_9));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_13),
                              _mm256_mullo_epi32(s[31], txmat_1_4));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[11] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_12),
                              _mm256_mullo_epi32(s[3], txmat_1_5));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_1),
                              _mm256_mullo_epi32(s[7], txmat_1_8));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_15),
                              _mm256_mullo_epi32(s[11], txmat_1_9));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_2),
                              _mm256_mullo_epi32(s[15], txmat_1_4));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_11),
                              _mm256_mullo_epi32(s[19], txmat_1_13));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_6),
                              _mm256_mullo_epi32(s[23], txmat_1_0));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_7),
                              _mm256_mullo_epi32(s[27], txmat_1_14));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_10),
                              _mm256_mullo_epi32(s[31], txmat_1_3));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[12] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_13),
                              _mm256_mullo_epi32(s[3], txmat_1_8));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_3),
                              _mm256_mullo_epi32(s[7], txmat_1_1));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_6),
                              _mm256_mullo_epi32(s[11], txmat_1_11));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_15),
                              _mm256_mullo_epi32(s[15], txmat_1_10));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_5),
                              _mm256_mullo_epi32(s[19], txmat_1_0));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_4),
                              _mm256_mullo_epi32(s[23], txmat_1_9));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_14),
                              _mm256_mullo_epi32(s[27], txmat_1_12));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_7),
                              _mm256_mullo_epi32(s[31], txmat_1_2));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[13] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_14),
                              _mm256_mullo_epi32(s[3], txmat_1_11));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_8),
                              _mm256_mullo_epi32(s[7], txmat_1_5));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_2),
                              _mm256_mullo_epi32(s[11], txmat_1_0));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_3),
                              _mm256_mullo_epi32(s[15], txmat_1_6));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_9),
                              _mm256_mullo_epi32(s[19], txmat_1_12));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_15),
                              _mm256_mullo_epi32(s[23], txmat_1_13));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_10),
                              _mm256_mullo_epi32(s[27], txmat_1_7));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_4),
                              _mm256_mullo_epi32(s[31], txmat_1_1));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[14] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_15),
                              _mm256_mullo_epi32(s[3], txmat_1_14));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_13),
                              _mm256_mullo_epi32(s[7], txmat_1_12));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_11),
                              _mm256_mullo_epi32(s[11], txmat_1_10));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_9),
                              _mm256_mullo_epi32(s[15], txmat_1_8));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_7),
                              _mm256_mullo_epi32(s[19], txmat_1_6));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_5),
                              _mm256_mullo_epi32(s[23], txmat_1_4));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_3),
                              _mm256_mullo_epi32(s[27], txmat_1_2));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_1),
                              _mm256_mullo_epi32(s[31], txmat_1_0));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[15] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_0);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_1);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_2);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_3);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_4);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_5);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_6);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_7);
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      d[0] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_1);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_4);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_7);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_5);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_2);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_0);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_3);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_6);
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      d[1] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_2);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_7);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_3);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_1);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_6);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_4);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_0);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_5);
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      d[2] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_3);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_5);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_1);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_7);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_0);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_6);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_2);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_4);
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      d[3] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_4);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_2);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_6);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_0);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_7);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_1);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_5);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_3);
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      d[4] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_5);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_0);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_4);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_6);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_1);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_3);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_7);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_2);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      d[5] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_6);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_3);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_0);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_2);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_5);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_7);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_4);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_1);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      d[6] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(s[2], txmat_2_7);
      c[1] = _mm256_mullo_epi32(s[6], txmat_2_6);
      c[2] = _mm256_mullo_epi32(s[10], txmat_2_5);
      c[3] = _mm256_mullo_epi32(s[14], txmat_2_4);
      c[4] = _mm256_mullo_epi32(s[18], txmat_2_3);
      c[5] = _mm256_mullo_epi32(s[22], txmat_2_2);
      c[6] = _mm256_mullo_epi32(s[26], txmat_2_1);
      c[7] = _mm256_mullo_epi32(s[30], txmat_2_0);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      d[7] = _mm256_add_epi32(c[0], c[4]);

      e[0] = _mm256_mullo_epi32(s[4], txmat_4_0);
      e[1] = _mm256_mullo_epi32(s[12], txmat_4_1);
      e[2] = _mm256_mullo_epi32(s[20], txmat_4_2);
      e[3] = _mm256_mullo_epi32(s[28], txmat_4_3);
      f[0] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(s[4], txmat_4_1);
      e[1] = _mm256_mullo_epi32(s[12], txmat_4_3);
      e[2] = _mm256_mullo_epi32(s[20], txmat_4_0);
      e[3] = _mm256_mullo_epi32(s[28], txmat_4_2);
      f[1] = _mm256_sub_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(s[4], txmat_4_2);
      e[1] = _mm256_mullo_epi32(s[12], txmat_4_0);
      e[2] = _mm256_mullo_epi32(s[20], txmat_4_3);
      e[3] = _mm256_mullo_epi32(s[28], txmat_4_1);
      f[2] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(s[4], txmat_4_3);
      e[1] = _mm256_mullo_epi32(s[12], txmat_4_2);
      e[2] = _mm256_mullo_epi32(s[20], txmat_4_1);
      e[3] = _mm256_mullo_epi32(s[28], txmat_4_0);
      f[3] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));

      h[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[8], txmat_8_0),
                              _mm256_mullo_epi32(s[24], txmat_8_1));
      h[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[8], txmat_8_1),
                              _mm256_mullo_epi32(s[24], txmat_8_0));
      g[0] = _mm256_add_epi32(s[0], s[16]);
      g[0] = _mm256_slli_epi32(g[0], 6);
      g[1] = _mm256_sub_epi32(s[0], s[16]);
      g[1] = _mm256_slli_epi32(g[1], 6);

      e[0] = _mm256_add_epi32(g[0], h[0]);
      e[3] = _mm256_sub_epi32(g[0], h[0]);
      e[1] = _mm256_add_epi32(g[1], h[1]);
      e[2] = _mm256_sub_epi32(g[1], h[1]);

      for (k = 0; k < 4; k++) {
        c[k] = _mm256_add_epi32(e[k], f[k]);
        c[k + 4] = _mm256_sub_epi32(e[3 - k], f[3 - k]);
      }
      for (k = 0; k < 8; k++) {
        a[k] = _mm256_add_epi32(c[k], d[k]);
        a[k + 8] = _mm256_sub_epi32(c[7 - k], d[7 - k]);
      }
      for (k = 0; k < 16; k++) {
        g[0] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[k], b[k]));
        g[1] =
            _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[15 - k], b[15 - k]));

        g[0] = _mm256_srai_epi32(g[0], shift);
        g[1] = _mm256_srai_epi32(g[1], shift);

        g[0] = _mm256_min_epi32(g[0], v_coef_max);
        g[0] = _mm256_max_epi32(g[0], v_coef_min);
        g[1] = _mm256_min_epi32(g[1], v_coef_max);
        g[1] = _mm256_max_epi32(g[1], v_coef_min);

        _mm256_storeu_si256((__m256i *)(dst + k * line), g[0]);
        _mm256_storeu_si256((__m256i *)(dst + (k + 16) * line), g[1]);
      }
      src += 8 * tx1d_size;
      dst += 8;
    }
  } else {
    __m128i a[16], b[16];
    __m128i c[8], d[8];
    __m128i e[4], f[4];
    __m128i g[2], h[2];

    __m128i v_offset = _mm_set1_epi32(add);
    __m128i v_coef_min = _mm_set1_epi32(coef_min);
    __m128i v_coef_max = _mm_set1_epi32(coef_max);

    __m128i s[32];

    __m128i txmat_8_0 = _mm_set1_epi32(tx_mat[8 * 32 + 0]);  // 85
    __m128i txmat_8_1 = _mm_set1_epi32(tx_mat[8 * 32 + 1]);  // 35

    __m128i txmat_4_0 = _mm_set1_epi32(tx_mat[4 * 32 + 0]);  // 89
    __m128i txmat_4_1 = _mm_set1_epi32(tx_mat[4 * 32 + 1]);  // 75
    __m128i txmat_4_2 = _mm_set1_epi32(tx_mat[4 * 32 + 2]);  // 50
    __m128i txmat_4_3 = _mm_set1_epi32(tx_mat[4 * 32 + 3]);  // 18

    __m128i txmat_2_0 = _mm_set1_epi32(tx_mat[2 * 32 + 0]);  // 90
    __m128i txmat_2_1 = _mm_set1_epi32(tx_mat[2 * 32 + 1]);  // 87
    __m128i txmat_2_2 = _mm_set1_epi32(tx_mat[2 * 32 + 2]);  // 80
    __m128i txmat_2_3 = _mm_set1_epi32(tx_mat[2 * 32 + 3]);  // 70
    __m128i txmat_2_4 = _mm_set1_epi32(tx_mat[2 * 32 + 4]);  // 57
    __m128i txmat_2_5 = _mm_set1_epi32(tx_mat[2 * 32 + 5]);  // 43
    __m128i txmat_2_6 = _mm_set1_epi32(tx_mat[2 * 32 + 6]);  // 26
    __m128i txmat_2_7 = _mm_set1_epi32(tx_mat[2 * 32 + 7]);  //  9

    __m128i txmat_1_0 = _mm_set1_epi32(tx_mat[1 * 32 + 0]);    // 90
    __m128i txmat_1_1 = _mm_set1_epi32(tx_mat[1 * 32 + 1]);    // 90
    __m128i txmat_1_2 = _mm_set1_epi32(tx_mat[1 * 32 + 2]);    // 88
    __m128i txmat_1_3 = _mm_set1_epi32(tx_mat[1 * 32 + 3]);    // 85
    __m128i txmat_1_4 = _mm_set1_epi32(tx_mat[1 * 32 + 4]);    // 82
    __m128i txmat_1_5 = _mm_set1_epi32(tx_mat[1 * 32 + 5]);    // 78
    __m128i txmat_1_6 = _mm_set1_epi32(tx_mat[1 * 32 + 6]);    // 73
    __m128i txmat_1_7 = _mm_set1_epi32(tx_mat[1 * 32 + 7]);    // 67
    __m128i txmat_1_8 = _mm_set1_epi32(tx_mat[1 * 32 + 8]);    // 61
    __m128i txmat_1_9 = _mm_set1_epi32(tx_mat[1 * 32 + 9]);    // 54
    __m128i txmat_1_10 = _mm_set1_epi32(tx_mat[1 * 32 + 10]);  // 47
    __m128i txmat_1_11 = _mm_set1_epi32(tx_mat[1 * 32 + 11]);  // 39
    __m128i txmat_1_12 = _mm_set1_epi32(tx_mat[1 * 32 + 12]);  // 30
    __m128i txmat_1_13 = _mm_set1_epi32(tx_mat[1 * 32 + 13]);  // 22
    __m128i txmat_1_14 = _mm_set1_epi32(tx_mat[1 * 32 + 14]);  // 13
    __m128i txmat_1_15 = _mm_set1_epi32(tx_mat[1 * 32 + 15]);  //  4

    // Transpose input
    transpose_load_8x4_sse4(src, s, tx1d_size);
    transpose_load_8x4_sse4(src + 8, s + 8, tx1d_size);
    transpose_load_8x4_sse4(src + 16, s + 16, tx1d_size);
    transpose_load_8x4_sse4(src + 24, s + 24, tx1d_size);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_0),
                         _mm_mullo_epi32(s[3], txmat_1_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_2),
                         _mm_mullo_epi32(s[7], txmat_1_3));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_4),
                         _mm_mullo_epi32(s[11], txmat_1_5));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_6),
                         _mm_mullo_epi32(s[15], txmat_1_7));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_8),
                         _mm_mullo_epi32(s[19], txmat_1_9));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_10),
                         _mm_mullo_epi32(s[23], txmat_1_11));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_12),
                         _mm_mullo_epi32(s[27], txmat_1_13));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_14),
                         _mm_mullo_epi32(s[31], txmat_1_15));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[0] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_1),
                         _mm_mullo_epi32(s[3], txmat_1_4));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_7),
                         _mm_mullo_epi32(s[7], txmat_1_10));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_13),
                         _mm_mullo_epi32(s[11], txmat_1_15));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_12),
                         _mm_mullo_epi32(s[15], txmat_1_9));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_6),
                         _mm_mullo_epi32(s[19], txmat_1_3));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_0),
                         _mm_mullo_epi32(s[23], txmat_1_2));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_5),
                         _mm_mullo_epi32(s[27], txmat_1_8));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_11),
                         _mm_mullo_epi32(s[31], txmat_1_14));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[1] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_2),
                         _mm_mullo_epi32(s[3], txmat_1_7));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_12),
                         _mm_mullo_epi32(s[7], txmat_1_14));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_9),
                         _mm_mullo_epi32(s[11], txmat_1_4));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_0),
                         _mm_mullo_epi32(s[15], txmat_1_5));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_10),
                         _mm_mullo_epi32(s[19], txmat_1_15));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_11),
                         _mm_mullo_epi32(s[23], txmat_1_6));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_1),
                         _mm_mullo_epi32(s[27], txmat_1_3));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_8),
                         _mm_mullo_epi32(s[31], txmat_1_13));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[2] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_3),
                         _mm_mullo_epi32(s[3], txmat_1_10));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_14),
                         _mm_mullo_epi32(s[7], txmat_1_7));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_0),
                         _mm_mullo_epi32(s[11], txmat_1_6));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_13),
                         _mm_mullo_epi32(s[15], txmat_1_11));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_4),
                         _mm_mullo_epi32(s[19], txmat_1_2));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_9),
                         _mm_mullo_epi32(s[23], txmat_1_15));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_8),
                         _mm_mullo_epi32(s[27], txmat_1_1));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_5),
                         _mm_mullo_epi32(s[31], txmat_1_12));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[3] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_4),
                         _mm_mullo_epi32(s[3], txmat_1_13));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_9),
                         _mm_mullo_epi32(s[7], txmat_1_0));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_8),
                         _mm_mullo_epi32(s[11], txmat_1_14));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_5),
                         _mm_mullo_epi32(s[15], txmat_1_3));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_12),
                         _mm_mullo_epi32(s[19], txmat_1_10));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_1),
                         _mm_mullo_epi32(s[23], txmat_1_7));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_15),
                         _mm_mullo_epi32(s[27], txmat_1_6));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_2),
                         _mm_mullo_epi32(s[31], txmat_1_11));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[4] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_5),
                         _mm_mullo_epi32(s[3], txmat_1_15));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_4),
                         _mm_mullo_epi32(s[7], txmat_1_6));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_14),
                         _mm_mullo_epi32(s[11], txmat_1_3));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_7),
                         _mm_mullo_epi32(s[15], txmat_1_13));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_2),
                         _mm_mullo_epi32(s[19], txmat_1_8));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_12),
                         _mm_mullo_epi32(s[23], txmat_1_1));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_9),
                         _mm_mullo_epi32(s[27], txmat_1_11));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_0),
                         _mm_mullo_epi32(s[31], txmat_1_10));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[5] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_6),
                         _mm_mullo_epi32(s[3], txmat_1_12));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_0),
                         _mm_mullo_epi32(s[7], txmat_1_13));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_5),
                         _mm_mullo_epi32(s[11], txmat_1_7));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_11),
                         _mm_mullo_epi32(s[15], txmat_1_1));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_14),
                         _mm_mullo_epi32(s[19], txmat_1_4));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_8),
                         _mm_mullo_epi32(s[23], txmat_1_10));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_2),
                         _mm_mullo_epi32(s[27], txmat_1_15));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_3),
                         _mm_mullo_epi32(s[31], txmat_1_9));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[6] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_7),
                         _mm_mullo_epi32(s[3], txmat_1_9));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_5),
                         _mm_mullo_epi32(s[7], txmat_1_11));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_3),
                         _mm_mullo_epi32(s[11], txmat_1_13));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_1),
                         _mm_mullo_epi32(s[15], txmat_1_15));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_0),
                         _mm_mullo_epi32(s[19], txmat_1_14));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_2),
                         _mm_mullo_epi32(s[23], txmat_1_12));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_4),
                         _mm_mullo_epi32(s[27], txmat_1_10));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_6),
                         _mm_mullo_epi32(s[31], txmat_1_8));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[7] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_8),
                         _mm_mullo_epi32(s[3], txmat_1_6));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_10),
                         _mm_mullo_epi32(s[7], txmat_1_4));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_12),
                         _mm_mullo_epi32(s[11], txmat_1_2));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_14),
                         _mm_mullo_epi32(s[15], txmat_1_0));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_15),
                         _mm_mullo_epi32(s[19], txmat_1_1));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_13),
                         _mm_mullo_epi32(s[23], txmat_1_3));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_11),
                         _mm_mullo_epi32(s[27], txmat_1_5));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_9),
                         _mm_mullo_epi32(s[31], txmat_1_7));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[8] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_9),
                         _mm_mullo_epi32(s[3], txmat_1_3));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_15),
                         _mm_mullo_epi32(s[7], txmat_1_2));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_10),
                         _mm_mullo_epi32(s[11], txmat_1_8));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_4),
                         _mm_mullo_epi32(s[15], txmat_1_14));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_1),
                         _mm_mullo_epi32(s[19], txmat_1_11));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_7),
                         _mm_mullo_epi32(s[23], txmat_1_5));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_13),
                         _mm_mullo_epi32(s[27], txmat_1_0));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_12),
                         _mm_mullo_epi32(s[31], txmat_1_6));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[9] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_10),
                         _mm_mullo_epi32(s[3], txmat_1_0));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_11),
                         _mm_mullo_epi32(s[7], txmat_1_9));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_1),
                         _mm_mullo_epi32(s[11], txmat_1_12));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_8),
                         _mm_mullo_epi32(s[15], txmat_1_2));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_13),
                         _mm_mullo_epi32(s[19], txmat_1_7));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_3),
                         _mm_mullo_epi32(s[23], txmat_1_14));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_6),
                         _mm_mullo_epi32(s[27], txmat_1_4));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_15),
                         _mm_mullo_epi32(s[31], txmat_1_5));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[10] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_11),
                         _mm_mullo_epi32(s[3], txmat_1_2));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_6),
                         _mm_mullo_epi32(s[7], txmat_1_15));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_7),
                         _mm_mullo_epi32(s[11], txmat_1_1));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_10),
                         _mm_mullo_epi32(s[15], txmat_1_12));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_3),
                         _mm_mullo_epi32(s[19], txmat_1_5));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_14),
                         _mm_mullo_epi32(s[23], txmat_1_8));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_0),
                         _mm_mullo_epi32(s[27], txmat_1_9));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_13),
                         _mm_mullo_epi32(s[31], txmat_1_4));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[11] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_12),
                         _mm_mullo_epi32(s[3], txmat_1_5));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_1),
                         _mm_mullo_epi32(s[7], txmat_1_8));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_15),
                         _mm_mullo_epi32(s[11], txmat_1_9));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_2),
                         _mm_mullo_epi32(s[15], txmat_1_4));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_11),
                         _mm_mullo_epi32(s[19], txmat_1_13));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_6),
                         _mm_mullo_epi32(s[23], txmat_1_0));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_7),
                         _mm_mullo_epi32(s[27], txmat_1_14));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_10),
                         _mm_mullo_epi32(s[31], txmat_1_3));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[12] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_13),
                         _mm_mullo_epi32(s[3], txmat_1_8));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_3),
                         _mm_mullo_epi32(s[7], txmat_1_1));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_6),
                         _mm_mullo_epi32(s[11], txmat_1_11));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_15),
                         _mm_mullo_epi32(s[15], txmat_1_10));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_5),
                         _mm_mullo_epi32(s[19], txmat_1_0));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_4),
                         _mm_mullo_epi32(s[23], txmat_1_9));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_14),
                         _mm_mullo_epi32(s[27], txmat_1_12));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_7),
                         _mm_mullo_epi32(s[31], txmat_1_2));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[13] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_14),
                         _mm_mullo_epi32(s[3], txmat_1_11));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_8),
                         _mm_mullo_epi32(s[7], txmat_1_5));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_2),
                         _mm_mullo_epi32(s[11], txmat_1_0));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_3),
                         _mm_mullo_epi32(s[15], txmat_1_6));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_9),
                         _mm_mullo_epi32(s[19], txmat_1_12));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_15),
                         _mm_mullo_epi32(s[23], txmat_1_13));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_10),
                         _mm_mullo_epi32(s[27], txmat_1_7));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_4),
                         _mm_mullo_epi32(s[31], txmat_1_1));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[14] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_15),
                         _mm_mullo_epi32(s[3], txmat_1_14));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_13),
                         _mm_mullo_epi32(s[7], txmat_1_12));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_11),
                         _mm_mullo_epi32(s[11], txmat_1_10));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_9),
                         _mm_mullo_epi32(s[15], txmat_1_8));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_7),
                         _mm_mullo_epi32(s[19], txmat_1_6));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_5),
                         _mm_mullo_epi32(s[23], txmat_1_4));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_3),
                         _mm_mullo_epi32(s[27], txmat_1_2));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_1),
                         _mm_mullo_epi32(s[31], txmat_1_0));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[15] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_0);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_1);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_2);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_3);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_4);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_5);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_6);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_7);
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    d[0] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_1);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_4);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_7);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_5);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_2);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_0);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_3);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_6);
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    d[1] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_2);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_7);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_3);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_1);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_6);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_4);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_0);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_5);
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    d[2] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_3);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_5);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_1);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_7);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_0);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_6);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_2);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_4);
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    d[3] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_4);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_2);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_6);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_0);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_7);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_1);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_5);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_3);
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    d[4] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_5);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_0);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_4);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_6);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_1);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_3);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_7);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_2);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    d[5] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_6);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_3);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_0);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_2);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_5);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_7);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_4);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_1);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    d[6] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(s[2], txmat_2_7);
    c[1] = _mm_mullo_epi32(s[6], txmat_2_6);
    c[2] = _mm_mullo_epi32(s[10], txmat_2_5);
    c[3] = _mm_mullo_epi32(s[14], txmat_2_4);
    c[4] = _mm_mullo_epi32(s[18], txmat_2_3);
    c[5] = _mm_mullo_epi32(s[22], txmat_2_2);
    c[6] = _mm_mullo_epi32(s[26], txmat_2_1);
    c[7] = _mm_mullo_epi32(s[30], txmat_2_0);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    d[7] = _mm_add_epi32(c[0], c[4]);

    e[0] = _mm_mullo_epi32(s[4], txmat_4_0);
    e[1] = _mm_mullo_epi32(s[12], txmat_4_1);
    e[2] = _mm_mullo_epi32(s[20], txmat_4_2);
    e[3] = _mm_mullo_epi32(s[28], txmat_4_3);
    f[0] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(s[4], txmat_4_1);
    e[1] = _mm_mullo_epi32(s[12], txmat_4_3);
    e[2] = _mm_mullo_epi32(s[20], txmat_4_0);
    e[3] = _mm_mullo_epi32(s[28], txmat_4_2);
    f[1] = _mm_sub_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(s[4], txmat_4_2);
    e[1] = _mm_mullo_epi32(s[12], txmat_4_0);
    e[2] = _mm_mullo_epi32(s[20], txmat_4_3);
    e[3] = _mm_mullo_epi32(s[28], txmat_4_1);
    f[2] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(s[4], txmat_4_3);
    e[1] = _mm_mullo_epi32(s[12], txmat_4_2);
    e[2] = _mm_mullo_epi32(s[20], txmat_4_1);
    e[3] = _mm_mullo_epi32(s[28], txmat_4_0);
    f[3] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));

    h[0] = _mm_add_epi32(_mm_mullo_epi32(s[8], txmat_8_0),
                         _mm_mullo_epi32(s[24], txmat_8_1));
    h[1] = _mm_sub_epi32(_mm_mullo_epi32(s[8], txmat_8_1),
                         _mm_mullo_epi32(s[24], txmat_8_0));
    g[0] = _mm_add_epi32(s[0], s[16]);
    g[0] = _mm_slli_epi32(g[0], 6);
    g[1] = _mm_sub_epi32(s[0], s[16]);
    g[1] = _mm_slli_epi32(g[1], 6);

    e[0] = _mm_add_epi32(g[0], h[0]);
    e[3] = _mm_sub_epi32(g[0], h[0]);
    e[1] = _mm_add_epi32(g[1], h[1]);
    e[2] = _mm_sub_epi32(g[1], h[1]);

    for (k = 0; k < 4; k++) {
      c[k] = _mm_add_epi32(e[k], f[k]);
      c[k + 4] = _mm_sub_epi32(e[3 - k], f[3 - k]);
    }
    for (k = 0; k < 8; k++) {
      a[k] = _mm_add_epi32(c[k], d[k]);
      a[k + 8] = _mm_sub_epi32(c[7 - k], d[7 - k]);
    }
    for (k = 0; k < 16; k++) {
      g[0] = _mm_add_epi32(v_offset, _mm_add_epi32(a[k], b[k]));
      g[1] = _mm_add_epi32(v_offset, _mm_sub_epi32(a[15 - k], b[15 - k]));

      g[0] = _mm_srai_epi32(g[0], shift);
      g[1] = _mm_srai_epi32(g[1], shift);

      g[0] = _mm_min_epi32(g[0], v_coef_max);
      g[0] = _mm_max_epi32(g[0], v_coef_min);
      g[1] = _mm_min_epi32(g[1], v_coef_max);
      g[1] = _mm_max_epi32(g[1], v_coef_min);

      _mm_storeu_si128((__m128i *)(dst + k * line), g[0]);
      _mm_storeu_si128((__m128i *)(dst + (k + 16) * line), g[1]);
    }
  }
}

#if !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
void inv_txfm_dct2_size64_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  int offset = 1 << (shift - 1);
  const int tx1d_size = 64;
  const int *tx_mat = tx_kernel_dct2_size64[INV_TXFM][0];

  int j, k;
  const int nz_line = line - skip_line;

  if (nz_line >= 8) {
    __m256i a[32], b[32];
    __m256i c[16], d[16];
    __m256i e[8], f[8];
    __m256i g[4], h[4];
    __m256i u[2], v[2];

    __m256i v_offset = _mm256_set1_epi32(offset);
    __m256i v_coef_min = _mm256_set1_epi32(coef_min);
    __m256i v_coef_max = _mm256_set1_epi32(coef_max);

    __m256i s[64];

    __m256i txmat_16_0 = _mm256_set1_epi32(tx_mat[16 * 64 + 0]);  // 85
    __m256i txmat_16_1 = _mm256_set1_epi32(tx_mat[16 * 64 + 1]);  // 35

    __m256i txmat_8_0 = _mm256_set1_epi32(tx_mat[8 * 64 + 0]);  // 89
    __m256i txmat_8_1 = _mm256_set1_epi32(tx_mat[8 * 64 + 1]);  // 75
    __m256i txmat_8_2 = _mm256_set1_epi32(tx_mat[8 * 64 + 2]);  // 50
    __m256i txmat_8_3 = _mm256_set1_epi32(tx_mat[8 * 64 + 3]);  // 18

    __m256i txmat_4_0 = _mm256_set1_epi32(tx_mat[4 * 64 + 0]);  // 90
    __m256i txmat_4_1 = _mm256_set1_epi32(tx_mat[4 * 64 + 1]);  // 87
    __m256i txmat_4_2 = _mm256_set1_epi32(tx_mat[4 * 64 + 2]);  // 80
    __m256i txmat_4_3 = _mm256_set1_epi32(tx_mat[4 * 64 + 3]);  // 70
    __m256i txmat_4_4 = _mm256_set1_epi32(tx_mat[4 * 64 + 4]);  // 57
    __m256i txmat_4_5 = _mm256_set1_epi32(tx_mat[4 * 64 + 5]);  // 43
    __m256i txmat_4_6 = _mm256_set1_epi32(tx_mat[4 * 64 + 6]);  // 26
    __m256i txmat_4_7 = _mm256_set1_epi32(tx_mat[4 * 64 + 7]);  //  9

    __m256i txmat_2_0 = _mm256_set1_epi32(tx_mat[2 * 64 + 0]);    // 90
    __m256i txmat_2_1 = _mm256_set1_epi32(tx_mat[2 * 64 + 1]);    // 90
    __m256i txmat_2_2 = _mm256_set1_epi32(tx_mat[2 * 64 + 2]);    // 88
    __m256i txmat_2_3 = _mm256_set1_epi32(tx_mat[2 * 64 + 3]);    // 85
    __m256i txmat_2_4 = _mm256_set1_epi32(tx_mat[2 * 64 + 4]);    // 82
    __m256i txmat_2_5 = _mm256_set1_epi32(tx_mat[2 * 64 + 5]);    // 78
    __m256i txmat_2_6 = _mm256_set1_epi32(tx_mat[2 * 64 + 6]);    // 73
    __m256i txmat_2_7 = _mm256_set1_epi32(tx_mat[2 * 64 + 7]);    // 67
    __m256i txmat_2_8 = _mm256_set1_epi32(tx_mat[2 * 64 + 8]);    // 61
    __m256i txmat_2_9 = _mm256_set1_epi32(tx_mat[2 * 64 + 9]);    // 54
    __m256i txmat_2_10 = _mm256_set1_epi32(tx_mat[2 * 64 + 10]);  // 47
    __m256i txmat_2_11 = _mm256_set1_epi32(tx_mat[2 * 64 + 11]);  // 39
    __m256i txmat_2_12 = _mm256_set1_epi32(tx_mat[2 * 64 + 12]);  // 30
    __m256i txmat_2_13 = _mm256_set1_epi32(tx_mat[2 * 64 + 13]);  // 22
    __m256i txmat_2_14 = _mm256_set1_epi32(tx_mat[2 * 64 + 14]);  // 13
    __m256i txmat_2_15 = _mm256_set1_epi32(tx_mat[2 * 64 + 15]);  //  4

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 64 + 0]);    // 90
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 64 + 1]);    // 90
    __m256i txmat_1_2 = _mm256_set1_epi32(tx_mat[1 * 64 + 2]);    // 90
    __m256i txmat_1_3 = _mm256_set1_epi32(tx_mat[1 * 64 + 3]);    // 89
    __m256i txmat_1_4 = _mm256_set1_epi32(tx_mat[1 * 64 + 4]);    // 88
    __m256i txmat_1_5 = _mm256_set1_epi32(tx_mat[1 * 64 + 5]);    // 87
    __m256i txmat_1_6 = _mm256_set1_epi32(tx_mat[1 * 64 + 6]);    // 86
    __m256i txmat_1_7 = _mm256_set1_epi32(tx_mat[1 * 64 + 7]);    // 84
    __m256i txmat_1_8 = _mm256_set1_epi32(tx_mat[1 * 64 + 8]);    // 83
    __m256i txmat_1_9 = _mm256_set1_epi32(tx_mat[1 * 64 + 9]);    // 81
    __m256i txmat_1_10 = _mm256_set1_epi32(tx_mat[1 * 64 + 10]);  // 79
    __m256i txmat_1_11 = _mm256_set1_epi32(tx_mat[1 * 64 + 11]);  // 76
    __m256i txmat_1_12 = _mm256_set1_epi32(tx_mat[1 * 64 + 12]);  // 74
    __m256i txmat_1_13 = _mm256_set1_epi32(tx_mat[1 * 64 + 13]);  // 71
    __m256i txmat_1_14 = _mm256_set1_epi32(tx_mat[1 * 64 + 14]);  // 69
    __m256i txmat_1_15 = _mm256_set1_epi32(tx_mat[1 * 64 + 15]);  // 66
    __m256i txmat_1_16 = _mm256_set1_epi32(tx_mat[1 * 64 + 16]);  // 62
    __m256i txmat_1_17 = _mm256_set1_epi32(tx_mat[1 * 64 + 17]);  // 59
    __m256i txmat_1_18 = _mm256_set1_epi32(tx_mat[1 * 64 + 18]);  // 56
    __m256i txmat_1_19 = _mm256_set1_epi32(tx_mat[1 * 64 + 19]);  // 52
    __m256i txmat_1_20 = _mm256_set1_epi32(tx_mat[1 * 64 + 20]);  // 48
    __m256i txmat_1_21 = _mm256_set1_epi32(tx_mat[1 * 64 + 21]);  // 45
    __m256i txmat_1_22 = _mm256_set1_epi32(tx_mat[1 * 64 + 22]);  // 41
    __m256i txmat_1_23 = _mm256_set1_epi32(tx_mat[1 * 64 + 23]);  // 37
    __m256i txmat_1_24 = _mm256_set1_epi32(tx_mat[1 * 64 + 24]);  // 33
    __m256i txmat_1_25 = _mm256_set1_epi32(tx_mat[1 * 64 + 25]);  // 28
    __m256i txmat_1_26 = _mm256_set1_epi32(tx_mat[1 * 64 + 26]);  // 24
    __m256i txmat_1_27 = _mm256_set1_epi32(tx_mat[1 * 64 + 27]);  // 20
    __m256i txmat_1_28 = _mm256_set1_epi32(tx_mat[1 * 64 + 28]);  // 15
    __m256i txmat_1_29 = _mm256_set1_epi32(tx_mat[1 * 64 + 29]);  // 11
    __m256i txmat_1_30 = _mm256_set1_epi32(tx_mat[1 * 64 + 30]);  //  7
    __m256i txmat_1_31 = _mm256_set1_epi32(tx_mat[1 * 64 + 31]);  //  2

    for (j = 0; j < nz_line; j += 8) {
      // Transpose input
      transpose_load_8x8_avx2(src, s, tx1d_size);
      transpose_load_8x8_avx2(src + 8, s + 8, tx1d_size);
      transpose_load_8x8_avx2(src + 16, s + 16, tx1d_size);
      transpose_load_8x8_avx2(src + 24, s + 24, tx1d_size);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_0),
                              _mm256_mullo_epi32(s[3], txmat_1_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_2),
                              _mm256_mullo_epi32(s[7], txmat_1_3));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_4),
                              _mm256_mullo_epi32(s[11], txmat_1_5));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_6),
                              _mm256_mullo_epi32(s[15], txmat_1_7));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_8),
                              _mm256_mullo_epi32(s[19], txmat_1_9));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_10),
                              _mm256_mullo_epi32(s[23], txmat_1_11));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_12),
                              _mm256_mullo_epi32(s[27], txmat_1_13));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_14),
                              _mm256_mullo_epi32(s[31], txmat_1_15));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[0] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_1),
                              _mm256_mullo_epi32(s[3], txmat_1_4));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_7),
                              _mm256_mullo_epi32(s[7], txmat_1_10));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_13),
                              _mm256_mullo_epi32(s[11], txmat_1_16));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_19),
                              _mm256_mullo_epi32(s[15], txmat_1_22));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_25),
                              _mm256_mullo_epi32(s[19], txmat_1_28));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_31),
                              _mm256_mullo_epi32(s[23], txmat_1_29));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_26),
                              _mm256_mullo_epi32(s[27], txmat_1_23));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_20),
                              _mm256_mullo_epi32(s[31], txmat_1_17));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[1] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_2),
                              _mm256_mullo_epi32(s[3], txmat_1_7));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_12),
                              _mm256_mullo_epi32(s[7], txmat_1_17));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_22),
                              _mm256_mullo_epi32(s[11], txmat_1_27));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_31),
                              _mm256_mullo_epi32(s[15], txmat_1_26));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_21),
                              _mm256_mullo_epi32(s[19], txmat_1_16));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_11),
                              _mm256_mullo_epi32(s[23], txmat_1_6));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_1),
                              _mm256_mullo_epi32(s[27], txmat_1_3));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_8),
                              _mm256_mullo_epi32(s[31], txmat_1_13));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[2] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_3),
                              _mm256_mullo_epi32(s[3], txmat_1_10));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_17),
                              _mm256_mullo_epi32(s[7], txmat_1_24));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_31),
                              _mm256_mullo_epi32(s[11], txmat_1_25));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_18),
                              _mm256_mullo_epi32(s[15], txmat_1_11));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_4),
                              _mm256_mullo_epi32(s[19], txmat_1_2));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_9),
                              _mm256_mullo_epi32(s[23], txmat_1_16));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_23),
                              _mm256_mullo_epi32(s[27], txmat_1_30));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_26),
                              _mm256_mullo_epi32(s[31], txmat_1_19));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[3] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_4),
                              _mm256_mullo_epi32(s[3], txmat_1_13));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_22),
                              _mm256_mullo_epi32(s[7], txmat_1_31));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_23),
                              _mm256_mullo_epi32(s[11], txmat_1_14));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_5),
                              _mm256_mullo_epi32(s[15], txmat_1_3));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_12),
                              _mm256_mullo_epi32(s[19], txmat_1_21));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_30),
                              _mm256_mullo_epi32(s[23], txmat_1_24));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_15),
                              _mm256_mullo_epi32(s[27], txmat_1_6));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_2),
                              _mm256_mullo_epi32(s[31], txmat_1_11));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[4] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_5),
                              _mm256_mullo_epi32(s[3], txmat_1_16));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_27),
                              _mm256_mullo_epi32(s[7], txmat_1_25));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_14),
                              _mm256_mullo_epi32(s[11], txmat_1_3));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_7),
                              _mm256_mullo_epi32(s[15], txmat_1_18));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_29),
                              _mm256_mullo_epi32(s[19], txmat_1_23));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_12),
                              _mm256_mullo_epi32(s[23], txmat_1_1));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_9),
                              _mm256_mullo_epi32(s[27], txmat_1_20));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_31),
                              _mm256_mullo_epi32(s[31], txmat_1_21));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[5] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_6),
                              _mm256_mullo_epi32(s[3], txmat_1_19));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_31),
                              _mm256_mullo_epi32(s[7], txmat_1_18));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_5),
                              _mm256_mullo_epi32(s[11], txmat_1_7));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_20),
                              _mm256_mullo_epi32(s[15], txmat_1_30));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_17),
                              _mm256_mullo_epi32(s[19], txmat_1_4));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_8),
                              _mm256_mullo_epi32(s[23], txmat_1_21));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_29),
                              _mm256_mullo_epi32(s[27], txmat_1_16));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_3),
                              _mm256_mullo_epi32(s[31], txmat_1_9));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[6] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_7),
                              _mm256_mullo_epi32(s[3], txmat_1_22));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_26),
                              _mm256_mullo_epi32(s[7], txmat_1_11));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_3),
                              _mm256_mullo_epi32(s[11], txmat_1_18));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_30),
                              _mm256_mullo_epi32(s[15], txmat_1_15));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_0),
                              _mm256_mullo_epi32(s[19], txmat_1_14));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_29),
                              _mm256_mullo_epi32(s[23], txmat_1_19));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_4),
                              _mm256_mullo_epi32(s[27], txmat_1_10));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_25),
                              _mm256_mullo_epi32(s[31], txmat_1_23));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[7] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_8),
                              _mm256_mullo_epi32(s[3], txmat_1_25));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_21),
                              _mm256_mullo_epi32(s[7], txmat_1_4));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_12),
                              _mm256_mullo_epi32(s[11], txmat_1_29));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_17),
                              _mm256_mullo_epi32(s[15], txmat_1_0));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_16),
                              _mm256_mullo_epi32(s[19], txmat_1_30));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_13),
                              _mm256_mullo_epi32(s[23], txmat_1_3));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_20),
                              _mm256_mullo_epi32(s[27], txmat_1_26));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_9),
                              _mm256_mullo_epi32(s[31], txmat_1_7));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[8] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_9),
                              _mm256_mullo_epi32(s[3], txmat_1_28));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_16),
                              _mm256_mullo_epi32(s[7], txmat_1_2));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_21),
                              _mm256_mullo_epi32(s[11], txmat_1_23));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_4),
                              _mm256_mullo_epi32(s[15], txmat_1_14));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_30),
                              _mm256_mullo_epi32(s[19], txmat_1_11));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_7),
                              _mm256_mullo_epi32(s[23], txmat_1_26));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_18),
                              _mm256_mullo_epi32(s[27], txmat_1_0));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_19),
                              _mm256_mullo_epi32(s[31], txmat_1_25));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[9] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[1], txmat_1_10),
                              _mm256_mullo_epi32(s[3], txmat_1_31));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_11),
                              _mm256_mullo_epi32(s[7], txmat_1_9));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_30),
                              _mm256_mullo_epi32(s[11], txmat_1_12));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_8),
                              _mm256_mullo_epi32(s[15], txmat_1_29));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_13),
                              _mm256_mullo_epi32(s[19], txmat_1_7));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_28),
                              _mm256_mullo_epi32(s[23], txmat_1_14));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_6),
                              _mm256_mullo_epi32(s[27], txmat_1_27));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_15),
                              _mm256_mullo_epi32(s[31], txmat_1_5));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[10] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_11),
                              _mm256_mullo_epi32(s[3], txmat_1_29));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_6),
                              _mm256_mullo_epi32(s[7], txmat_1_16));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_24),
                              _mm256_mullo_epi32(s[11], txmat_1_1));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_21),
                              _mm256_mullo_epi32(s[15], txmat_1_19));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_3),
                              _mm256_mullo_epi32(s[19], txmat_1_26));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_14),
                              _mm256_mullo_epi32(s[23], txmat_1_8));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_31),
                              _mm256_mullo_epi32(s[27], txmat_1_9));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_13),
                              _mm256_mullo_epi32(s[31], txmat_1_27));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[11] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_12),
                              _mm256_mullo_epi32(s[3], txmat_1_26));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_1),
                              _mm256_mullo_epi32(s[7], txmat_1_23));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_15),
                              _mm256_mullo_epi32(s[11], txmat_1_9));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_29),
                              _mm256_mullo_epi32(s[15], txmat_1_4));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_20),
                              _mm256_mullo_epi32(s[19], txmat_1_18));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_6),
                              _mm256_mullo_epi32(s[23], txmat_1_31));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_7),
                              _mm256_mullo_epi32(s[27], txmat_1_17));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_21),
                              _mm256_mullo_epi32(s[31], txmat_1_3));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[12] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_13),
                              _mm256_mullo_epi32(s[3], txmat_1_23));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_3),
                              _mm256_mullo_epi32(s[7], txmat_1_30));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_6),
                              _mm256_mullo_epi32(s[11], txmat_1_20));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_16),
                              _mm256_mullo_epi32(s[15], txmat_1_10));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_26),
                              _mm256_mullo_epi32(s[19], txmat_1_0));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_27),
                              _mm256_mullo_epi32(s[23], txmat_1_9));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_17),
                              _mm256_mullo_epi32(s[27], txmat_1_19));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_7),
                              _mm256_mullo_epi32(s[31], txmat_1_29));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[13] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_14),
                              _mm256_mullo_epi32(s[3], txmat_1_20));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_8),
                              _mm256_mullo_epi32(s[7], txmat_1_26));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_2),
                              _mm256_mullo_epi32(s[11], txmat_1_31));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_3),
                              _mm256_mullo_epi32(s[15], txmat_1_25));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_9),
                              _mm256_mullo_epi32(s[19], txmat_1_19));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_15),
                              _mm256_mullo_epi32(s[23], txmat_1_13));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_21),
                              _mm256_mullo_epi32(s[27], txmat_1_7));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_27),
                              _mm256_mullo_epi32(s[31], txmat_1_1));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[14] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_15),
                              _mm256_mullo_epi32(s[3], txmat_1_17));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_13),
                              _mm256_mullo_epi32(s[7], txmat_1_19));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_11),
                              _mm256_mullo_epi32(s[11], txmat_1_21));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_9),
                              _mm256_mullo_epi32(s[15], txmat_1_23));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_7),
                              _mm256_mullo_epi32(s[19], txmat_1_25));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_5),
                              _mm256_mullo_epi32(s[23], txmat_1_27));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_3),
                              _mm256_mullo_epi32(s[27], txmat_1_29));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_1),
                              _mm256_mullo_epi32(s[31], txmat_1_31));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[15] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_16),
                              _mm256_mullo_epi32(s[3], txmat_1_14));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_18),
                              _mm256_mullo_epi32(s[7], txmat_1_12));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_20),
                              _mm256_mullo_epi32(s[11], txmat_1_10));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_22),
                              _mm256_mullo_epi32(s[15], txmat_1_8));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_24),
                              _mm256_mullo_epi32(s[19], txmat_1_6));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_26),
                              _mm256_mullo_epi32(s[23], txmat_1_4));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_28),
                              _mm256_mullo_epi32(s[27], txmat_1_2));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_30),
                              _mm256_mullo_epi32(s[31], txmat_1_0));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[16] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_17),
                              _mm256_mullo_epi32(s[3], txmat_1_11));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_23),
                              _mm256_mullo_epi32(s[7], txmat_1_5));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_29),
                              _mm256_mullo_epi32(s[11], txmat_1_0));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_28),
                              _mm256_mullo_epi32(s[15], txmat_1_6));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_22),
                              _mm256_mullo_epi32(s[19], txmat_1_12));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_16),
                              _mm256_mullo_epi32(s[23], txmat_1_18));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_10),
                              _mm256_mullo_epi32(s[27], txmat_1_24));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_4),
                              _mm256_mullo_epi32(s[31], txmat_1_30));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[17] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_18),
                              _mm256_mullo_epi32(s[3], txmat_1_8));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_28),
                              _mm256_mullo_epi32(s[7], txmat_1_1));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_25),
                              _mm256_mullo_epi32(s[11], txmat_1_11));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_15),
                              _mm256_mullo_epi32(s[15], txmat_1_21));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_5),
                              _mm256_mullo_epi32(s[19], txmat_1_31));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_4),
                              _mm256_mullo_epi32(s[23], txmat_1_22));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_14),
                              _mm256_mullo_epi32(s[27], txmat_1_12));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_24),
                              _mm256_mullo_epi32(s[31], txmat_1_2));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[18] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_19),
                              _mm256_mullo_epi32(s[3], txmat_1_5));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_30),
                              _mm256_mullo_epi32(s[7], txmat_1_8));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_16),
                              _mm256_mullo_epi32(s[11], txmat_1_22));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_2),
                              _mm256_mullo_epi32(s[15], txmat_1_27));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_11),
                              _mm256_mullo_epi32(s[19], txmat_1_13));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_25),
                              _mm256_mullo_epi32(s[23], txmat_1_0));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_24),
                              _mm256_mullo_epi32(s[27], txmat_1_14));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_10),
                              _mm256_mullo_epi32(s[31], txmat_1_28));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[19] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_20),
                              _mm256_mullo_epi32(s[3], txmat_1_2));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_25),
                              _mm256_mullo_epi32(s[7], txmat_1_15));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_7),
                              _mm256_mullo_epi32(s[11], txmat_1_30));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_10),
                              _mm256_mullo_epi32(s[15], txmat_1_12));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_28),
                              _mm256_mullo_epi32(s[19], txmat_1_5));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_17),
                              _mm256_mullo_epi32(s[23], txmat_1_23));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_0),
                              _mm256_mullo_epi32(s[27], txmat_1_22));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_18),
                              _mm256_mullo_epi32(s[31], txmat_1_4));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[20] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_21),
                              _mm256_mullo_epi32(s[3], txmat_1_0));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_20),
                              _mm256_mullo_epi32(s[7], txmat_1_22));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_1),
                              _mm256_mullo_epi32(s[11], txmat_1_19));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_23),
                              _mm256_mullo_epi32(s[15], txmat_1_2));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_18),
                              _mm256_mullo_epi32(s[19], txmat_1_24));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_3),
                              _mm256_mullo_epi32(s[23], txmat_1_17));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_25),
                              _mm256_mullo_epi32(s[27], txmat_1_4));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_16),
                              _mm256_mullo_epi32(s[31], txmat_1_26));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[21] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_22),
                              _mm256_mullo_epi32(s[3], txmat_1_3));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[5], txmat_1_15),
                              _mm256_mullo_epi32(s[7], txmat_1_29));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_10),
                              _mm256_mullo_epi32(s[11], txmat_1_8));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_27),
                              _mm256_mullo_epi32(s[15], txmat_1_17));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_1),
                              _mm256_mullo_epi32(s[19], txmat_1_20));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_24),
                              _mm256_mullo_epi32(s[23], txmat_1_5));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_13),
                              _mm256_mullo_epi32(s[27], txmat_1_31));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_12),
                              _mm256_mullo_epi32(s[31], txmat_1_6));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[22] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_23),
                              _mm256_mullo_epi32(s[3], txmat_1_6));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_10),
                              _mm256_mullo_epi32(s[7], txmat_1_27));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_19),
                              _mm256_mullo_epi32(s[11], txmat_1_2));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_14),
                              _mm256_mullo_epi32(s[15], txmat_1_31));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_15),
                              _mm256_mullo_epi32(s[19], txmat_1_1));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(s[21], txmat_1_18),
                              _mm256_mullo_epi32(s[23], txmat_1_28));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_11),
                              _mm256_mullo_epi32(s[27], txmat_1_5));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_22),
                              _mm256_mullo_epi32(s[31], txmat_1_24));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[23] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_24),
                              _mm256_mullo_epi32(s[3], txmat_1_9));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_5),
                              _mm256_mullo_epi32(s[7], txmat_1_20));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_28),
                              _mm256_mullo_epi32(s[11], txmat_1_13));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_1),
                              _mm256_mullo_epi32(s[15], txmat_1_16));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_31),
                              _mm256_mullo_epi32(s[19], txmat_1_17));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_2),
                              _mm256_mullo_epi32(s[23], txmat_1_12));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_27),
                              _mm256_mullo_epi32(s[27], txmat_1_21));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_6),
                              _mm256_mullo_epi32(s[31], txmat_1_8));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[24] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_25),
                              _mm256_mullo_epi32(s[3], txmat_1_12));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_0),
                              _mm256_mullo_epi32(s[7], txmat_1_13));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[9], txmat_1_26),
                              _mm256_mullo_epi32(s[11], txmat_1_24));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_11),
                              _mm256_mullo_epi32(s[15], txmat_1_1));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_14),
                              _mm256_mullo_epi32(s[19], txmat_1_27));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_23),
                              _mm256_mullo_epi32(s[23], txmat_1_10));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_2),
                              _mm256_mullo_epi32(s[27], txmat_1_15));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(s[29], txmat_1_28),
                              _mm256_mullo_epi32(s[31], txmat_1_22));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[25] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_26),
                              _mm256_mullo_epi32(s[3], txmat_1_15));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_4),
                              _mm256_mullo_epi32(s[7], txmat_1_6));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_17),
                              _mm256_mullo_epi32(s[11], txmat_1_28));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_24),
                              _mm256_mullo_epi32(s[15], txmat_1_13));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_2),
                              _mm256_mullo_epi32(s[19], txmat_1_8));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_19),
                              _mm256_mullo_epi32(s[23], txmat_1_30));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_22),
                              _mm256_mullo_epi32(s[27], txmat_1_11));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_0),
                              _mm256_mullo_epi32(s[31], txmat_1_10));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[26] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_27),
                              _mm256_mullo_epi32(s[3], txmat_1_18));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_9),
                              _mm256_mullo_epi32(s[7], txmat_1_0));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_8),
                              _mm256_mullo_epi32(s[11], txmat_1_17));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[13], txmat_1_26),
                              _mm256_mullo_epi32(s[15], txmat_1_28));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_19),
                              _mm256_mullo_epi32(s[19], txmat_1_10));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_1),
                              _mm256_mullo_epi32(s[23], txmat_1_7));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_16),
                              _mm256_mullo_epi32(s[27], txmat_1_25));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_29),
                              _mm256_mullo_epi32(s[31], txmat_1_20));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[27] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_28),
                              _mm256_mullo_epi32(s[3], txmat_1_21));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_14),
                              _mm256_mullo_epi32(s[7], txmat_1_7));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_0),
                              _mm256_mullo_epi32(s[11], txmat_1_6));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_13),
                              _mm256_mullo_epi32(s[15], txmat_1_20));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(s[17], txmat_1_27),
                              _mm256_mullo_epi32(s[19], txmat_1_29));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_22),
                              _mm256_mullo_epi32(s[23], txmat_1_15));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_8),
                              _mm256_mullo_epi32(s[27], txmat_1_1));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_5),
                              _mm256_mullo_epi32(s[31], txmat_1_12));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[28] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_29),
                              _mm256_mullo_epi32(s[3], txmat_1_24));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_19),
                              _mm256_mullo_epi32(s[7], txmat_1_14));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_9),
                              _mm256_mullo_epi32(s[11], txmat_1_4));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_0),
                              _mm256_mullo_epi32(s[15], txmat_1_5));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_10),
                              _mm256_mullo_epi32(s[19], txmat_1_15));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_20),
                              _mm256_mullo_epi32(s[23], txmat_1_25));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(s[25], txmat_1_30),
                              _mm256_mullo_epi32(s[27], txmat_1_28));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_23),
                              _mm256_mullo_epi32(s[31], txmat_1_18));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      b[29] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_30),
                              _mm256_mullo_epi32(s[3], txmat_1_27));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_24),
                              _mm256_mullo_epi32(s[7], txmat_1_21));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_18),
                              _mm256_mullo_epi32(s[11], txmat_1_15));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_12),
                              _mm256_mullo_epi32(s[15], txmat_1_9));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_6),
                              _mm256_mullo_epi32(s[19], txmat_1_3));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_0),
                              _mm256_mullo_epi32(s[23], txmat_1_2));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_5),
                              _mm256_mullo_epi32(s[27], txmat_1_8));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_11),
                              _mm256_mullo_epi32(s[31], txmat_1_14));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[30] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[1], txmat_1_31),
                              _mm256_mullo_epi32(s[3], txmat_1_30));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[5], txmat_1_29),
                              _mm256_mullo_epi32(s[7], txmat_1_28));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[9], txmat_1_27),
                              _mm256_mullo_epi32(s[11], txmat_1_26));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[13], txmat_1_25),
                              _mm256_mullo_epi32(s[15], txmat_1_24));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(s[17], txmat_1_23),
                              _mm256_mullo_epi32(s[19], txmat_1_22));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(s[21], txmat_1_21),
                              _mm256_mullo_epi32(s[23], txmat_1_20));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(s[25], txmat_1_19),
                              _mm256_mullo_epi32(s[27], txmat_1_18));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(s[29], txmat_1_17),
                              _mm256_mullo_epi32(s[31], txmat_1_16));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      b[31] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_0),
                              _mm256_mullo_epi32(s[6], txmat_2_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_2),
                              _mm256_mullo_epi32(s[14], txmat_2_3));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_4),
                              _mm256_mullo_epi32(s[22], txmat_2_5));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_6),
                              _mm256_mullo_epi32(s[30], txmat_2_7));
      d[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_1),
                              _mm256_mullo_epi32(s[6], txmat_2_4));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_7),
                              _mm256_mullo_epi32(s[14], txmat_2_10));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_13),
                              _mm256_mullo_epi32(s[22], txmat_2_15));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_12),
                              _mm256_mullo_epi32(s[30], txmat_2_9));
      d[1] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_2),
                              _mm256_mullo_epi32(s[6], txmat_2_7));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_12),
                              _mm256_mullo_epi32(s[14], txmat_2_14));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_9),
                              _mm256_mullo_epi32(s[22], txmat_2_4));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_0),
                              _mm256_mullo_epi32(s[30], txmat_2_5));
      d[2] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_3),
                              _mm256_mullo_epi32(s[6], txmat_2_10));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_14),
                              _mm256_mullo_epi32(s[14], txmat_2_7));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_0),
                              _mm256_mullo_epi32(s[22], txmat_2_6));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_13),
                              _mm256_mullo_epi32(s[30], txmat_2_11));
      d[3] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[2], txmat_2_4),
                              _mm256_mullo_epi32(s[6], txmat_2_13));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_9),
                              _mm256_mullo_epi32(s[14], txmat_2_0));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_8),
                              _mm256_mullo_epi32(s[22], txmat_2_14));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_5),
                              _mm256_mullo_epi32(s[30], txmat_2_3));
      d[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_5),
                              _mm256_mullo_epi32(s[6], txmat_2_15));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_4),
                              _mm256_mullo_epi32(s[14], txmat_2_6));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_14),
                              _mm256_mullo_epi32(s[22], txmat_2_3));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_7),
                              _mm256_mullo_epi32(s[30], txmat_2_13));
      d[5] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_6),
                              _mm256_mullo_epi32(s[6], txmat_2_12));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_0),
                              _mm256_mullo_epi32(s[14], txmat_2_13));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_5),
                              _mm256_mullo_epi32(s[22], txmat_2_7));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_11),
                              _mm256_mullo_epi32(s[30], txmat_2_1));
      d[6] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_7),
                              _mm256_mullo_epi32(s[6], txmat_2_9));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_5),
                              _mm256_mullo_epi32(s[14], txmat_2_11));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_3),
                              _mm256_mullo_epi32(s[22], txmat_2_13));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_1),
                              _mm256_mullo_epi32(s[30], txmat_2_15));
      d[7] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_8),
                              _mm256_mullo_epi32(s[6], txmat_2_6));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_10),
                              _mm256_mullo_epi32(s[14], txmat_2_4));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_12),
                              _mm256_mullo_epi32(s[22], txmat_2_2));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_14),
                              _mm256_mullo_epi32(s[30], txmat_2_0));
      d[8] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_9),
                              _mm256_mullo_epi32(s[6], txmat_2_3));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_15),
                              _mm256_mullo_epi32(s[14], txmat_2_2));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_10),
                              _mm256_mullo_epi32(s[22], txmat_2_8));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_4),
                              _mm256_mullo_epi32(s[30], txmat_2_14));
      d[9] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_10),
                              _mm256_mullo_epi32(s[6], txmat_2_0));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[10], txmat_2_11),
                              _mm256_mullo_epi32(s[14], txmat_2_9));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_1),
                              _mm256_mullo_epi32(s[22], txmat_2_12));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_8),
                              _mm256_mullo_epi32(s[30], txmat_2_2));
      d[10] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_11),
                              _mm256_mullo_epi32(s[6], txmat_2_2));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_6),
                              _mm256_mullo_epi32(s[14], txmat_2_15));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_7),
                              _mm256_mullo_epi32(s[22], txmat_2_1));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(s[26], txmat_2_10),
                              _mm256_mullo_epi32(s[30], txmat_2_12));
      d[11] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_12),
                              _mm256_mullo_epi32(s[6], txmat_2_5));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_1),
                              _mm256_mullo_epi32(s[14], txmat_2_8));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(s[18], txmat_2_15),
                              _mm256_mullo_epi32(s[22], txmat_2_9));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_2),
                              _mm256_mullo_epi32(s[30], txmat_2_4));
      d[12] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_13),
                              _mm256_mullo_epi32(s[6], txmat_2_8));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_3),
                              _mm256_mullo_epi32(s[14], txmat_2_1));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_6),
                              _mm256_mullo_epi32(s[22], txmat_2_11));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_15),
                              _mm256_mullo_epi32(s[30], txmat_2_10));
      d[13] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_sub_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_14),
                              _mm256_mullo_epi32(s[6], txmat_2_11));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_8),
                              _mm256_mullo_epi32(s[14], txmat_2_5));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_2),
                              _mm256_mullo_epi32(s[22], txmat_2_0));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_3),
                              _mm256_mullo_epi32(s[30], txmat_2_6));
      d[14] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[2], txmat_2_15),
                              _mm256_mullo_epi32(s[6], txmat_2_14));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[10], txmat_2_13),
                              _mm256_mullo_epi32(s[14], txmat_2_12));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[18], txmat_2_11),
                              _mm256_mullo_epi32(s[22], txmat_2_10));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[26], txmat_2_9),
                              _mm256_mullo_epi32(s[30], txmat_2_8));
      d[15] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                               _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[4], txmat_4_0),
                              _mm256_mullo_epi32(s[12], txmat_4_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[20], txmat_4_2),
                              _mm256_mullo_epi32(s[28], txmat_4_3));
      f[0] = _mm256_add_epi32(c[0], c[1]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[4], txmat_4_1),
                              _mm256_mullo_epi32(s[12], txmat_4_4));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[20], txmat_4_7),
                              _mm256_mullo_epi32(s[28], txmat_4_5));
      f[1] = _mm256_add_epi32(c[0], c[1]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[4], txmat_4_2),
                              _mm256_mullo_epi32(s[12], txmat_4_7));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[20], txmat_4_3),
                              _mm256_mullo_epi32(s[28], txmat_4_1));
      f[2] = _mm256_sub_epi32(c[0], c[1]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_3),
                              _mm256_mullo_epi32(s[12], txmat_4_5));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[20], txmat_4_1),
                              _mm256_mullo_epi32(s[28], txmat_4_7));
      f[3] = _mm256_sub_epi32(c[0], c[1]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_4),
                              _mm256_mullo_epi32(s[12], txmat_4_2));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[20], txmat_4_6),
                              _mm256_mullo_epi32(s[28], txmat_4_0));
      f[4] = _mm256_sub_epi32(c[0], c[1]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_5),
                              _mm256_mullo_epi32(s[12], txmat_4_0));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(s[20], txmat_4_4),
                              _mm256_mullo_epi32(s[28], txmat_4_6));
      f[5] = _mm256_add_epi32(c[0], c[1]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_6),
                              _mm256_mullo_epi32(s[12], txmat_4_3));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[20], txmat_4_0),
                              _mm256_mullo_epi32(s[28], txmat_4_2));
      f[6] = _mm256_add_epi32(c[0], c[1]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(s[4], txmat_4_7),
                              _mm256_mullo_epi32(s[12], txmat_4_6));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[20], txmat_4_5),
                              _mm256_mullo_epi32(s[28], txmat_4_4));
      f[7] = _mm256_add_epi32(c[0], c[1]);

      h[0] = _mm256_add_epi32(_mm256_mullo_epi32(s[8], txmat_8_0),
                              _mm256_mullo_epi32(s[24], txmat_8_1));
      h[1] = _mm256_sub_epi32(_mm256_mullo_epi32(s[8], txmat_8_1),
                              _mm256_mullo_epi32(s[24], txmat_8_3));
      h[2] = _mm256_sub_epi32(_mm256_mullo_epi32(s[8], txmat_8_2),
                              _mm256_mullo_epi32(s[24], txmat_8_0));
      h[3] = _mm256_sub_epi32(_mm256_mullo_epi32(s[8], txmat_8_3),
                              _mm256_mullo_epi32(s[24], txmat_8_2));

      u[0] = _mm256_mullo_epi32(s[16], txmat_16_0);
      u[1] = _mm256_mullo_epi32(s[16], txmat_16_1);
      v[0] = _mm256_slli_epi32(s[0], 6);
      v[1] = _mm256_slli_epi32(s[0], 6);

      g[0] = _mm256_add_epi32(v[0], u[0]);
      g[2] = _mm256_sub_epi32(v[1], u[1]);
      g[1] = _mm256_add_epi32(v[1], u[1]);
      g[3] = _mm256_sub_epi32(v[0], u[0]);

      for (k = 0; k < 4; k++) {
        e[k] = _mm256_add_epi32(g[k], h[k]);
        e[k + 4] = _mm256_sub_epi32(g[3 - k], h[3 - k]);
      }

      for (k = 0; k < 8; k++) {
        c[k] = _mm256_add_epi32(e[k], f[k]);
        c[k + 8] = _mm256_sub_epi32(e[7 - k], f[7 - k]);
      }

      for (k = 0; k < 16; k++) {
        a[k] = _mm256_add_epi32(c[k], d[k]);
        a[k + 16] = _mm256_sub_epi32(c[15 - k], d[15 - k]);
      }

      for (k = 0; k < 32; k++) {
        g[0] = _mm256_add_epi32(v_offset, _mm256_add_epi32(a[k], b[k]));
        g[1] =
            _mm256_add_epi32(v_offset, _mm256_sub_epi32(a[31 - k], b[31 - k]));

        g[0] = _mm256_srai_epi32(g[0], shift);
        g[1] = _mm256_srai_epi32(g[1], shift);

        g[0] = _mm256_min_epi32(g[0], v_coef_max);
        g[0] = _mm256_max_epi32(g[0], v_coef_min);
        g[1] = _mm256_min_epi32(g[1], v_coef_max);
        g[1] = _mm256_max_epi32(g[1], v_coef_min);

        _mm256_storeu_si256((__m256i *)(dst + k * line), g[0]);
        _mm256_storeu_si256((__m256i *)(dst + (k + 32) * line), g[1]);
      }
      src += 8 * tx1d_size;
      dst += 8;
    }
  } else {
    __m128i a[32], b[32];
    __m128i c[16], d[16];
    __m128i e[8], f[8];
    __m128i g[4], h[4];
    __m128i u[2], v[2];

    __m128i v_offset = _mm_set1_epi32(offset);
    __m128i v_coef_min = _mm_set1_epi32(coef_min);
    __m128i v_coef_max = _mm_set1_epi32(coef_max);

    __m128i s[64];

    __m128i txmat_16_0 = _mm_set1_epi32(tx_mat[16 * 64 + 0]);  // 85
    __m128i txmat_16_1 = _mm_set1_epi32(tx_mat[16 * 64 + 1]);  // 35

    __m128i txmat_8_0 = _mm_set1_epi32(tx_mat[8 * 64 + 0]);  // 89
    __m128i txmat_8_1 = _mm_set1_epi32(tx_mat[8 * 64 + 1]);  // 75
    __m128i txmat_8_2 = _mm_set1_epi32(tx_mat[8 * 64 + 2]);  // 50
    __m128i txmat_8_3 = _mm_set1_epi32(tx_mat[8 * 64 + 3]);  // 18

    __m128i txmat_4_0 = _mm_set1_epi32(tx_mat[4 * 64 + 0]);  // 90
    __m128i txmat_4_1 = _mm_set1_epi32(tx_mat[4 * 64 + 1]);  // 87
    __m128i txmat_4_2 = _mm_set1_epi32(tx_mat[4 * 64 + 2]);  // 80
    __m128i txmat_4_3 = _mm_set1_epi32(tx_mat[4 * 64 + 3]);  // 70
    __m128i txmat_4_4 = _mm_set1_epi32(tx_mat[4 * 64 + 4]);  // 57
    __m128i txmat_4_5 = _mm_set1_epi32(tx_mat[4 * 64 + 5]);  // 43
    __m128i txmat_4_6 = _mm_set1_epi32(tx_mat[4 * 64 + 6]);  // 26
    __m128i txmat_4_7 = _mm_set1_epi32(tx_mat[4 * 64 + 7]);  //  9

    __m128i txmat_2_0 = _mm_set1_epi32(tx_mat[2 * 64 + 0]);    // 90
    __m128i txmat_2_1 = _mm_set1_epi32(tx_mat[2 * 64 + 1]);    // 90
    __m128i txmat_2_2 = _mm_set1_epi32(tx_mat[2 * 64 + 2]);    // 88
    __m128i txmat_2_3 = _mm_set1_epi32(tx_mat[2 * 64 + 3]);    // 85
    __m128i txmat_2_4 = _mm_set1_epi32(tx_mat[2 * 64 + 4]);    // 82
    __m128i txmat_2_5 = _mm_set1_epi32(tx_mat[2 * 64 + 5]);    // 78
    __m128i txmat_2_6 = _mm_set1_epi32(tx_mat[2 * 64 + 6]);    // 73
    __m128i txmat_2_7 = _mm_set1_epi32(tx_mat[2 * 64 + 7]);    // 67
    __m128i txmat_2_8 = _mm_set1_epi32(tx_mat[2 * 64 + 8]);    // 61
    __m128i txmat_2_9 = _mm_set1_epi32(tx_mat[2 * 64 + 9]);    // 54
    __m128i txmat_2_10 = _mm_set1_epi32(tx_mat[2 * 64 + 10]);  // 47
    __m128i txmat_2_11 = _mm_set1_epi32(tx_mat[2 * 64 + 11]);  // 39
    __m128i txmat_2_12 = _mm_set1_epi32(tx_mat[2 * 64 + 12]);  // 30
    __m128i txmat_2_13 = _mm_set1_epi32(tx_mat[2 * 64 + 13]);  // 22
    __m128i txmat_2_14 = _mm_set1_epi32(tx_mat[2 * 64 + 14]);  // 13
    __m128i txmat_2_15 = _mm_set1_epi32(tx_mat[2 * 64 + 15]);  //  4

    __m128i txmat_1_0 = _mm_set1_epi32(tx_mat[1 * 64 + 0]);    // 90
    __m128i txmat_1_1 = _mm_set1_epi32(tx_mat[1 * 64 + 1]);    // 90
    __m128i txmat_1_2 = _mm_set1_epi32(tx_mat[1 * 64 + 2]);    // 90
    __m128i txmat_1_3 = _mm_set1_epi32(tx_mat[1 * 64 + 3]);    // 89
    __m128i txmat_1_4 = _mm_set1_epi32(tx_mat[1 * 64 + 4]);    // 88
    __m128i txmat_1_5 = _mm_set1_epi32(tx_mat[1 * 64 + 5]);    // 87
    __m128i txmat_1_6 = _mm_set1_epi32(tx_mat[1 * 64 + 6]);    // 86
    __m128i txmat_1_7 = _mm_set1_epi32(tx_mat[1 * 64 + 7]);    // 84
    __m128i txmat_1_8 = _mm_set1_epi32(tx_mat[1 * 64 + 8]);    // 83
    __m128i txmat_1_9 = _mm_set1_epi32(tx_mat[1 * 64 + 9]);    // 81
    __m128i txmat_1_10 = _mm_set1_epi32(tx_mat[1 * 64 + 10]);  // 79
    __m128i txmat_1_11 = _mm_set1_epi32(tx_mat[1 * 64 + 11]);  // 76
    __m128i txmat_1_12 = _mm_set1_epi32(tx_mat[1 * 64 + 12]);  // 74
    __m128i txmat_1_13 = _mm_set1_epi32(tx_mat[1 * 64 + 13]);  // 71
    __m128i txmat_1_14 = _mm_set1_epi32(tx_mat[1 * 64 + 14]);  // 69
    __m128i txmat_1_15 = _mm_set1_epi32(tx_mat[1 * 64 + 15]);  // 66
    __m128i txmat_1_16 = _mm_set1_epi32(tx_mat[1 * 64 + 16]);  // 62
    __m128i txmat_1_17 = _mm_set1_epi32(tx_mat[1 * 64 + 17]);  // 59
    __m128i txmat_1_18 = _mm_set1_epi32(tx_mat[1 * 64 + 18]);  // 56
    __m128i txmat_1_19 = _mm_set1_epi32(tx_mat[1 * 64 + 19]);  // 52
    __m128i txmat_1_20 = _mm_set1_epi32(tx_mat[1 * 64 + 20]);  // 48
    __m128i txmat_1_21 = _mm_set1_epi32(tx_mat[1 * 64 + 21]);  // 45
    __m128i txmat_1_22 = _mm_set1_epi32(tx_mat[1 * 64 + 22]);  // 41
    __m128i txmat_1_23 = _mm_set1_epi32(tx_mat[1 * 64 + 23]);  // 37
    __m128i txmat_1_24 = _mm_set1_epi32(tx_mat[1 * 64 + 24]);  // 33
    __m128i txmat_1_25 = _mm_set1_epi32(tx_mat[1 * 64 + 25]);  // 28
    __m128i txmat_1_26 = _mm_set1_epi32(tx_mat[1 * 64 + 26]);  // 24
    __m128i txmat_1_27 = _mm_set1_epi32(tx_mat[1 * 64 + 27]);  // 20
    __m128i txmat_1_28 = _mm_set1_epi32(tx_mat[1 * 64 + 28]);  // 15
    __m128i txmat_1_29 = _mm_set1_epi32(tx_mat[1 * 64 + 29]);  // 11
    __m128i txmat_1_30 = _mm_set1_epi32(tx_mat[1 * 64 + 30]);  //  7
    __m128i txmat_1_31 = _mm_set1_epi32(tx_mat[1 * 64 + 31]);  //  2

    // Transpose input
    transpose_load_8x4_sse4(src, s, tx1d_size);
    transpose_load_8x4_sse4(src + 8, s + 8, tx1d_size);
    transpose_load_8x4_sse4(src + 16, s + 16, tx1d_size);
    transpose_load_8x4_sse4(src + 24, s + 24, tx1d_size);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_0),
                         _mm_mullo_epi32(s[3], txmat_1_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_2),
                         _mm_mullo_epi32(s[7], txmat_1_3));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_4),
                         _mm_mullo_epi32(s[11], txmat_1_5));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_6),
                         _mm_mullo_epi32(s[15], txmat_1_7));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_8),
                         _mm_mullo_epi32(s[19], txmat_1_9));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_10),
                         _mm_mullo_epi32(s[23], txmat_1_11));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_12),
                         _mm_mullo_epi32(s[27], txmat_1_13));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_14),
                         _mm_mullo_epi32(s[31], txmat_1_15));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[0] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_1),
                         _mm_mullo_epi32(s[3], txmat_1_4));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_7),
                         _mm_mullo_epi32(s[7], txmat_1_10));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_13),
                         _mm_mullo_epi32(s[11], txmat_1_16));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_19),
                         _mm_mullo_epi32(s[15], txmat_1_22));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_25),
                         _mm_mullo_epi32(s[19], txmat_1_28));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_31),
                         _mm_mullo_epi32(s[23], txmat_1_29));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_26),
                         _mm_mullo_epi32(s[27], txmat_1_23));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_20),
                         _mm_mullo_epi32(s[31], txmat_1_17));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[1] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_2),
                         _mm_mullo_epi32(s[3], txmat_1_7));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_12),
                         _mm_mullo_epi32(s[7], txmat_1_17));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_22),
                         _mm_mullo_epi32(s[11], txmat_1_27));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_31),
                         _mm_mullo_epi32(s[15], txmat_1_26));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_21),
                         _mm_mullo_epi32(s[19], txmat_1_16));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_11),
                         _mm_mullo_epi32(s[23], txmat_1_6));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_1),
                         _mm_mullo_epi32(s[27], txmat_1_3));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_8),
                         _mm_mullo_epi32(s[31], txmat_1_13));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[2] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_3),
                         _mm_mullo_epi32(s[3], txmat_1_10));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_17),
                         _mm_mullo_epi32(s[7], txmat_1_24));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_31),
                         _mm_mullo_epi32(s[11], txmat_1_25));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_18),
                         _mm_mullo_epi32(s[15], txmat_1_11));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_4),
                         _mm_mullo_epi32(s[19], txmat_1_2));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_9),
                         _mm_mullo_epi32(s[23], txmat_1_16));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_23),
                         _mm_mullo_epi32(s[27], txmat_1_30));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_26),
                         _mm_mullo_epi32(s[31], txmat_1_19));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[3] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_4),
                         _mm_mullo_epi32(s[3], txmat_1_13));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_22),
                         _mm_mullo_epi32(s[7], txmat_1_31));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_23),
                         _mm_mullo_epi32(s[11], txmat_1_14));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_5),
                         _mm_mullo_epi32(s[15], txmat_1_3));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_12),
                         _mm_mullo_epi32(s[19], txmat_1_21));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_30),
                         _mm_mullo_epi32(s[23], txmat_1_24));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_15),
                         _mm_mullo_epi32(s[27], txmat_1_6));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_2),
                         _mm_mullo_epi32(s[31], txmat_1_11));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[4] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_5),
                         _mm_mullo_epi32(s[3], txmat_1_16));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_27),
                         _mm_mullo_epi32(s[7], txmat_1_25));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_14),
                         _mm_mullo_epi32(s[11], txmat_1_3));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_7),
                         _mm_mullo_epi32(s[15], txmat_1_18));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_29),
                         _mm_mullo_epi32(s[19], txmat_1_23));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_12),
                         _mm_mullo_epi32(s[23], txmat_1_1));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_9),
                         _mm_mullo_epi32(s[27], txmat_1_20));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_31),
                         _mm_mullo_epi32(s[31], txmat_1_21));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[5] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_6),
                         _mm_mullo_epi32(s[3], txmat_1_19));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_31),
                         _mm_mullo_epi32(s[7], txmat_1_18));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_5),
                         _mm_mullo_epi32(s[11], txmat_1_7));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_20),
                         _mm_mullo_epi32(s[15], txmat_1_30));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_17),
                         _mm_mullo_epi32(s[19], txmat_1_4));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_8),
                         _mm_mullo_epi32(s[23], txmat_1_21));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_29),
                         _mm_mullo_epi32(s[27], txmat_1_16));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_3),
                         _mm_mullo_epi32(s[31], txmat_1_9));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[6] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_7),
                         _mm_mullo_epi32(s[3], txmat_1_22));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_26),
                         _mm_mullo_epi32(s[7], txmat_1_11));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_3),
                         _mm_mullo_epi32(s[11], txmat_1_18));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_30),
                         _mm_mullo_epi32(s[15], txmat_1_15));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_0),
                         _mm_mullo_epi32(s[19], txmat_1_14));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_29),
                         _mm_mullo_epi32(s[23], txmat_1_19));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_4),
                         _mm_mullo_epi32(s[27], txmat_1_10));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_25),
                         _mm_mullo_epi32(s[31], txmat_1_23));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[7] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_8),
                         _mm_mullo_epi32(s[3], txmat_1_25));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_21),
                         _mm_mullo_epi32(s[7], txmat_1_4));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_12),
                         _mm_mullo_epi32(s[11], txmat_1_29));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_17),
                         _mm_mullo_epi32(s[15], txmat_1_0));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_16),
                         _mm_mullo_epi32(s[19], txmat_1_30));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_13),
                         _mm_mullo_epi32(s[23], txmat_1_3));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_20),
                         _mm_mullo_epi32(s[27], txmat_1_26));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_9),
                         _mm_mullo_epi32(s[31], txmat_1_7));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[8] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_9),
                         _mm_mullo_epi32(s[3], txmat_1_28));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_16),
                         _mm_mullo_epi32(s[7], txmat_1_2));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_21),
                         _mm_mullo_epi32(s[11], txmat_1_23));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_4),
                         _mm_mullo_epi32(s[15], txmat_1_14));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_30),
                         _mm_mullo_epi32(s[19], txmat_1_11));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_7),
                         _mm_mullo_epi32(s[23], txmat_1_26));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_18),
                         _mm_mullo_epi32(s[27], txmat_1_0));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_19),
                         _mm_mullo_epi32(s[31], txmat_1_25));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[9] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[1], txmat_1_10),
                         _mm_mullo_epi32(s[3], txmat_1_31));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_11),
                         _mm_mullo_epi32(s[7], txmat_1_9));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_30),
                         _mm_mullo_epi32(s[11], txmat_1_12));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_8),
                         _mm_mullo_epi32(s[15], txmat_1_29));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_13),
                         _mm_mullo_epi32(s[19], txmat_1_7));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_28),
                         _mm_mullo_epi32(s[23], txmat_1_14));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_6),
                         _mm_mullo_epi32(s[27], txmat_1_27));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_15),
                         _mm_mullo_epi32(s[31], txmat_1_5));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[10] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_11),
                         _mm_mullo_epi32(s[3], txmat_1_29));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_6),
                         _mm_mullo_epi32(s[7], txmat_1_16));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_24),
                         _mm_mullo_epi32(s[11], txmat_1_1));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_21),
                         _mm_mullo_epi32(s[15], txmat_1_19));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_3),
                         _mm_mullo_epi32(s[19], txmat_1_26));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_14),
                         _mm_mullo_epi32(s[23], txmat_1_8));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_31),
                         _mm_mullo_epi32(s[27], txmat_1_9));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_13),
                         _mm_mullo_epi32(s[31], txmat_1_27));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[11] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_12),
                         _mm_mullo_epi32(s[3], txmat_1_26));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_1),
                         _mm_mullo_epi32(s[7], txmat_1_23));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_15),
                         _mm_mullo_epi32(s[11], txmat_1_9));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_29),
                         _mm_mullo_epi32(s[15], txmat_1_4));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_20),
                         _mm_mullo_epi32(s[19], txmat_1_18));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_6),
                         _mm_mullo_epi32(s[23], txmat_1_31));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_7),
                         _mm_mullo_epi32(s[27], txmat_1_17));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_21),
                         _mm_mullo_epi32(s[31], txmat_1_3));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[12] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_13),
                         _mm_mullo_epi32(s[3], txmat_1_23));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_3),
                         _mm_mullo_epi32(s[7], txmat_1_30));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_6),
                         _mm_mullo_epi32(s[11], txmat_1_20));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_16),
                         _mm_mullo_epi32(s[15], txmat_1_10));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_26),
                         _mm_mullo_epi32(s[19], txmat_1_0));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_27),
                         _mm_mullo_epi32(s[23], txmat_1_9));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_17),
                         _mm_mullo_epi32(s[27], txmat_1_19));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_7),
                         _mm_mullo_epi32(s[31], txmat_1_29));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[13] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_14),
                         _mm_mullo_epi32(s[3], txmat_1_20));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_8),
                         _mm_mullo_epi32(s[7], txmat_1_26));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_2),
                         _mm_mullo_epi32(s[11], txmat_1_31));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_3),
                         _mm_mullo_epi32(s[15], txmat_1_25));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_9),
                         _mm_mullo_epi32(s[19], txmat_1_19));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_15),
                         _mm_mullo_epi32(s[23], txmat_1_13));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_21),
                         _mm_mullo_epi32(s[27], txmat_1_7));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_27),
                         _mm_mullo_epi32(s[31], txmat_1_1));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[14] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_15),
                         _mm_mullo_epi32(s[3], txmat_1_17));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_13),
                         _mm_mullo_epi32(s[7], txmat_1_19));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_11),
                         _mm_mullo_epi32(s[11], txmat_1_21));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_9),
                         _mm_mullo_epi32(s[15], txmat_1_23));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_7),
                         _mm_mullo_epi32(s[19], txmat_1_25));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_5),
                         _mm_mullo_epi32(s[23], txmat_1_27));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_3),
                         _mm_mullo_epi32(s[27], txmat_1_29));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_1),
                         _mm_mullo_epi32(s[31], txmat_1_31));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[15] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_16),
                         _mm_mullo_epi32(s[3], txmat_1_14));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_18),
                         _mm_mullo_epi32(s[7], txmat_1_12));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_20),
                         _mm_mullo_epi32(s[11], txmat_1_10));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_22),
                         _mm_mullo_epi32(s[15], txmat_1_8));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_24),
                         _mm_mullo_epi32(s[19], txmat_1_6));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_26),
                         _mm_mullo_epi32(s[23], txmat_1_4));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_28),
                         _mm_mullo_epi32(s[27], txmat_1_2));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_30),
                         _mm_mullo_epi32(s[31], txmat_1_0));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[16] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_17),
                         _mm_mullo_epi32(s[3], txmat_1_11));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_23),
                         _mm_mullo_epi32(s[7], txmat_1_5));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_29),
                         _mm_mullo_epi32(s[11], txmat_1_0));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_28),
                         _mm_mullo_epi32(s[15], txmat_1_6));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_22),
                         _mm_mullo_epi32(s[19], txmat_1_12));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_16),
                         _mm_mullo_epi32(s[23], txmat_1_18));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_10),
                         _mm_mullo_epi32(s[27], txmat_1_24));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_4),
                         _mm_mullo_epi32(s[31], txmat_1_30));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[17] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_18),
                         _mm_mullo_epi32(s[3], txmat_1_8));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_28),
                         _mm_mullo_epi32(s[7], txmat_1_1));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_25),
                         _mm_mullo_epi32(s[11], txmat_1_11));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_15),
                         _mm_mullo_epi32(s[15], txmat_1_21));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_5),
                         _mm_mullo_epi32(s[19], txmat_1_31));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_4),
                         _mm_mullo_epi32(s[23], txmat_1_22));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_14),
                         _mm_mullo_epi32(s[27], txmat_1_12));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_24),
                         _mm_mullo_epi32(s[31], txmat_1_2));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[18] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_19),
                         _mm_mullo_epi32(s[3], txmat_1_5));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_30),
                         _mm_mullo_epi32(s[7], txmat_1_8));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_16),
                         _mm_mullo_epi32(s[11], txmat_1_22));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_2),
                         _mm_mullo_epi32(s[15], txmat_1_27));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_11),
                         _mm_mullo_epi32(s[19], txmat_1_13));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_25),
                         _mm_mullo_epi32(s[23], txmat_1_0));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_24),
                         _mm_mullo_epi32(s[27], txmat_1_14));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_10),
                         _mm_mullo_epi32(s[31], txmat_1_28));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[19] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_20),
                         _mm_mullo_epi32(s[3], txmat_1_2));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_25),
                         _mm_mullo_epi32(s[7], txmat_1_15));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_7),
                         _mm_mullo_epi32(s[11], txmat_1_30));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_10),
                         _mm_mullo_epi32(s[15], txmat_1_12));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_28),
                         _mm_mullo_epi32(s[19], txmat_1_5));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_17),
                         _mm_mullo_epi32(s[23], txmat_1_23));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_0),
                         _mm_mullo_epi32(s[27], txmat_1_22));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_18),
                         _mm_mullo_epi32(s[31], txmat_1_4));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[20] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_21),
                         _mm_mullo_epi32(s[3], txmat_1_0));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_20),
                         _mm_mullo_epi32(s[7], txmat_1_22));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_1),
                         _mm_mullo_epi32(s[11], txmat_1_19));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_23),
                         _mm_mullo_epi32(s[15], txmat_1_2));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_18),
                         _mm_mullo_epi32(s[19], txmat_1_24));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_3),
                         _mm_mullo_epi32(s[23], txmat_1_17));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_25),
                         _mm_mullo_epi32(s[27], txmat_1_4));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_16),
                         _mm_mullo_epi32(s[31], txmat_1_26));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[21] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_22),
                         _mm_mullo_epi32(s[3], txmat_1_3));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[5], txmat_1_15),
                         _mm_mullo_epi32(s[7], txmat_1_29));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_10),
                         _mm_mullo_epi32(s[11], txmat_1_8));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_27),
                         _mm_mullo_epi32(s[15], txmat_1_17));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_1),
                         _mm_mullo_epi32(s[19], txmat_1_20));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_24),
                         _mm_mullo_epi32(s[23], txmat_1_5));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_13),
                         _mm_mullo_epi32(s[27], txmat_1_31));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_12),
                         _mm_mullo_epi32(s[31], txmat_1_6));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[22] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_23),
                         _mm_mullo_epi32(s[3], txmat_1_6));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_10),
                         _mm_mullo_epi32(s[7], txmat_1_27));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_19),
                         _mm_mullo_epi32(s[11], txmat_1_2));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_14),
                         _mm_mullo_epi32(s[15], txmat_1_31));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_15),
                         _mm_mullo_epi32(s[19], txmat_1_1));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(s[21], txmat_1_18),
                         _mm_mullo_epi32(s[23], txmat_1_28));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_11),
                         _mm_mullo_epi32(s[27], txmat_1_5));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_22),
                         _mm_mullo_epi32(s[31], txmat_1_24));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[23] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_24),
                         _mm_mullo_epi32(s[3], txmat_1_9));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_5),
                         _mm_mullo_epi32(s[7], txmat_1_20));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_28),
                         _mm_mullo_epi32(s[11], txmat_1_13));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_1),
                         _mm_mullo_epi32(s[15], txmat_1_16));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_31),
                         _mm_mullo_epi32(s[19], txmat_1_17));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_2),
                         _mm_mullo_epi32(s[23], txmat_1_12));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_27),
                         _mm_mullo_epi32(s[27], txmat_1_21));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_6),
                         _mm_mullo_epi32(s[31], txmat_1_8));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[24] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_25),
                         _mm_mullo_epi32(s[3], txmat_1_12));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_0),
                         _mm_mullo_epi32(s[7], txmat_1_13));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[9], txmat_1_26),
                         _mm_mullo_epi32(s[11], txmat_1_24));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_11),
                         _mm_mullo_epi32(s[15], txmat_1_1));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_14),
                         _mm_mullo_epi32(s[19], txmat_1_27));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_23),
                         _mm_mullo_epi32(s[23], txmat_1_10));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_2),
                         _mm_mullo_epi32(s[27], txmat_1_15));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(s[29], txmat_1_28),
                         _mm_mullo_epi32(s[31], txmat_1_22));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[25] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_26),
                         _mm_mullo_epi32(s[3], txmat_1_15));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_4),
                         _mm_mullo_epi32(s[7], txmat_1_6));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_17),
                         _mm_mullo_epi32(s[11], txmat_1_28));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_24),
                         _mm_mullo_epi32(s[15], txmat_1_13));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_2),
                         _mm_mullo_epi32(s[19], txmat_1_8));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_19),
                         _mm_mullo_epi32(s[23], txmat_1_30));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_22),
                         _mm_mullo_epi32(s[27], txmat_1_11));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_0),
                         _mm_mullo_epi32(s[31], txmat_1_10));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[26] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_27),
                         _mm_mullo_epi32(s[3], txmat_1_18));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_9),
                         _mm_mullo_epi32(s[7], txmat_1_0));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_8),
                         _mm_mullo_epi32(s[11], txmat_1_17));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[13], txmat_1_26),
                         _mm_mullo_epi32(s[15], txmat_1_28));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_19),
                         _mm_mullo_epi32(s[19], txmat_1_10));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_1),
                         _mm_mullo_epi32(s[23], txmat_1_7));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_16),
                         _mm_mullo_epi32(s[27], txmat_1_25));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_29),
                         _mm_mullo_epi32(s[31], txmat_1_20));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[27] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_28),
                         _mm_mullo_epi32(s[3], txmat_1_21));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_14),
                         _mm_mullo_epi32(s[7], txmat_1_7));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_0),
                         _mm_mullo_epi32(s[11], txmat_1_6));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_13),
                         _mm_mullo_epi32(s[15], txmat_1_20));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(s[17], txmat_1_27),
                         _mm_mullo_epi32(s[19], txmat_1_29));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_22),
                         _mm_mullo_epi32(s[23], txmat_1_15));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_8),
                         _mm_mullo_epi32(s[27], txmat_1_1));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_5),
                         _mm_mullo_epi32(s[31], txmat_1_12));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[28] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_29),
                         _mm_mullo_epi32(s[3], txmat_1_24));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_19),
                         _mm_mullo_epi32(s[7], txmat_1_14));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_9),
                         _mm_mullo_epi32(s[11], txmat_1_4));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_0),
                         _mm_mullo_epi32(s[15], txmat_1_5));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_10),
                         _mm_mullo_epi32(s[19], txmat_1_15));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_20),
                         _mm_mullo_epi32(s[23], txmat_1_25));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(s[25], txmat_1_30),
                         _mm_mullo_epi32(s[27], txmat_1_28));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_23),
                         _mm_mullo_epi32(s[31], txmat_1_18));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    b[29] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_30),
                         _mm_mullo_epi32(s[3], txmat_1_27));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_24),
                         _mm_mullo_epi32(s[7], txmat_1_21));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_18),
                         _mm_mullo_epi32(s[11], txmat_1_15));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_12),
                         _mm_mullo_epi32(s[15], txmat_1_9));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_6),
                         _mm_mullo_epi32(s[19], txmat_1_3));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_0),
                         _mm_mullo_epi32(s[23], txmat_1_2));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_5),
                         _mm_mullo_epi32(s[27], txmat_1_8));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_11),
                         _mm_mullo_epi32(s[31], txmat_1_14));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[30] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[1], txmat_1_31),
                         _mm_mullo_epi32(s[3], txmat_1_30));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[5], txmat_1_29),
                         _mm_mullo_epi32(s[7], txmat_1_28));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[9], txmat_1_27),
                         _mm_mullo_epi32(s[11], txmat_1_26));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[13], txmat_1_25),
                         _mm_mullo_epi32(s[15], txmat_1_24));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(s[17], txmat_1_23),
                         _mm_mullo_epi32(s[19], txmat_1_22));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(s[21], txmat_1_21),
                         _mm_mullo_epi32(s[23], txmat_1_20));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(s[25], txmat_1_19),
                         _mm_mullo_epi32(s[27], txmat_1_18));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(s[29], txmat_1_17),
                         _mm_mullo_epi32(s[31], txmat_1_16));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    b[31] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_0),
                         _mm_mullo_epi32(s[6], txmat_2_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_2),
                         _mm_mullo_epi32(s[14], txmat_2_3));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_4),
                         _mm_mullo_epi32(s[22], txmat_2_5));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_6),
                         _mm_mullo_epi32(s[30], txmat_2_7));
    d[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_1),
                         _mm_mullo_epi32(s[6], txmat_2_4));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_7),
                         _mm_mullo_epi32(s[14], txmat_2_10));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_13),
                         _mm_mullo_epi32(s[22], txmat_2_15));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_12),
                         _mm_mullo_epi32(s[30], txmat_2_9));
    d[1] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_2),
                         _mm_mullo_epi32(s[6], txmat_2_7));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_12),
                         _mm_mullo_epi32(s[14], txmat_2_14));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_9),
                         _mm_mullo_epi32(s[22], txmat_2_4));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_0),
                         _mm_mullo_epi32(s[30], txmat_2_5));
    d[2] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_3),
                         _mm_mullo_epi32(s[6], txmat_2_10));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_14),
                         _mm_mullo_epi32(s[14], txmat_2_7));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_0),
                         _mm_mullo_epi32(s[22], txmat_2_6));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_13),
                         _mm_mullo_epi32(s[30], txmat_2_11));
    d[3] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[2], txmat_2_4),
                         _mm_mullo_epi32(s[6], txmat_2_13));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_9),
                         _mm_mullo_epi32(s[14], txmat_2_0));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_8),
                         _mm_mullo_epi32(s[22], txmat_2_14));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_5),
                         _mm_mullo_epi32(s[30], txmat_2_3));
    d[4] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_5),
                         _mm_mullo_epi32(s[6], txmat_2_15));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_4),
                         _mm_mullo_epi32(s[14], txmat_2_6));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_14),
                         _mm_mullo_epi32(s[22], txmat_2_3));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_7),
                         _mm_mullo_epi32(s[30], txmat_2_13));
    d[5] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_6),
                         _mm_mullo_epi32(s[6], txmat_2_12));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_0),
                         _mm_mullo_epi32(s[14], txmat_2_13));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_5),
                         _mm_mullo_epi32(s[22], txmat_2_7));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_11),
                         _mm_mullo_epi32(s[30], txmat_2_1));
    d[6] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_7),
                         _mm_mullo_epi32(s[6], txmat_2_9));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_5),
                         _mm_mullo_epi32(s[14], txmat_2_11));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_3),
                         _mm_mullo_epi32(s[22], txmat_2_13));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_1),
                         _mm_mullo_epi32(s[30], txmat_2_15));
    d[7] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_8),
                         _mm_mullo_epi32(s[6], txmat_2_6));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_10),
                         _mm_mullo_epi32(s[14], txmat_2_4));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_12),
                         _mm_mullo_epi32(s[22], txmat_2_2));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_14),
                         _mm_mullo_epi32(s[30], txmat_2_0));
    d[8] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_9),
                         _mm_mullo_epi32(s[6], txmat_2_3));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_15),
                         _mm_mullo_epi32(s[14], txmat_2_2));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_10),
                         _mm_mullo_epi32(s[22], txmat_2_8));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_4),
                         _mm_mullo_epi32(s[30], txmat_2_14));
    d[9] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_10),
                         _mm_mullo_epi32(s[6], txmat_2_0));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[10], txmat_2_11),
                         _mm_mullo_epi32(s[14], txmat_2_9));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_1),
                         _mm_mullo_epi32(s[22], txmat_2_12));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_8),
                         _mm_mullo_epi32(s[30], txmat_2_2));
    d[10] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_11),
                         _mm_mullo_epi32(s[6], txmat_2_2));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_6),
                         _mm_mullo_epi32(s[14], txmat_2_15));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_7),
                         _mm_mullo_epi32(s[22], txmat_2_1));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(s[26], txmat_2_10),
                         _mm_mullo_epi32(s[30], txmat_2_12));
    d[11] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_12),
                         _mm_mullo_epi32(s[6], txmat_2_5));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_1),
                         _mm_mullo_epi32(s[14], txmat_2_8));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(s[18], txmat_2_15),
                         _mm_mullo_epi32(s[22], txmat_2_9));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_2),
                         _mm_mullo_epi32(s[30], txmat_2_4));
    d[12] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_13),
                         _mm_mullo_epi32(s[6], txmat_2_8));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_3),
                         _mm_mullo_epi32(s[14], txmat_2_1));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_6),
                         _mm_mullo_epi32(s[22], txmat_2_11));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_15),
                         _mm_mullo_epi32(s[30], txmat_2_10));
    d[13] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_14),
                         _mm_mullo_epi32(s[6], txmat_2_11));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_8),
                         _mm_mullo_epi32(s[14], txmat_2_5));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_2),
                         _mm_mullo_epi32(s[22], txmat_2_0));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_3),
                         _mm_mullo_epi32(s[30], txmat_2_6));
    d[14] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[2], txmat_2_15),
                         _mm_mullo_epi32(s[6], txmat_2_14));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[10], txmat_2_13),
                         _mm_mullo_epi32(s[14], txmat_2_12));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(s[18], txmat_2_11),
                         _mm_mullo_epi32(s[22], txmat_2_10));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(s[26], txmat_2_9),
                         _mm_mullo_epi32(s[30], txmat_2_8));
    d[15] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[4], txmat_4_0),
                         _mm_mullo_epi32(s[12], txmat_4_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[20], txmat_4_2),
                         _mm_mullo_epi32(s[28], txmat_4_3));
    f[0] = _mm_add_epi32(c[0], c[1]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[4], txmat_4_1),
                         _mm_mullo_epi32(s[12], txmat_4_4));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[20], txmat_4_7),
                         _mm_mullo_epi32(s[28], txmat_4_5));
    f[1] = _mm_add_epi32(c[0], c[1]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(s[4], txmat_4_2),
                         _mm_mullo_epi32(s[12], txmat_4_7));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[20], txmat_4_3),
                         _mm_mullo_epi32(s[28], txmat_4_1));
    f[2] = _mm_sub_epi32(c[0], c[1]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_3),
                         _mm_mullo_epi32(s[12], txmat_4_5));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[20], txmat_4_1),
                         _mm_mullo_epi32(s[28], txmat_4_7));
    f[3] = _mm_sub_epi32(c[0], c[1]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_4),
                         _mm_mullo_epi32(s[12], txmat_4_2));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[20], txmat_4_6),
                         _mm_mullo_epi32(s[28], txmat_4_0));
    f[4] = _mm_sub_epi32(c[0], c[1]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_5),
                         _mm_mullo_epi32(s[12], txmat_4_0));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(s[20], txmat_4_4),
                         _mm_mullo_epi32(s[28], txmat_4_6));
    f[5] = _mm_add_epi32(c[0], c[1]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_6),
                         _mm_mullo_epi32(s[12], txmat_4_3));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[20], txmat_4_0),
                         _mm_mullo_epi32(s[28], txmat_4_2));
    f[6] = _mm_add_epi32(c[0], c[1]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(s[4], txmat_4_7),
                         _mm_mullo_epi32(s[12], txmat_4_6));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(s[20], txmat_4_5),
                         _mm_mullo_epi32(s[28], txmat_4_4));
    f[7] = _mm_add_epi32(c[0], c[1]);

    h[0] = _mm_add_epi32(_mm_mullo_epi32(s[8], txmat_8_0),
                         _mm_mullo_epi32(s[24], txmat_8_1));
    h[1] = _mm_sub_epi32(_mm_mullo_epi32(s[8], txmat_8_1),
                         _mm_mullo_epi32(s[24], txmat_8_3));
    h[2] = _mm_sub_epi32(_mm_mullo_epi32(s[8], txmat_8_2),
                         _mm_mullo_epi32(s[24], txmat_8_0));
    h[3] = _mm_sub_epi32(_mm_mullo_epi32(s[8], txmat_8_3),
                         _mm_mullo_epi32(s[24], txmat_8_2));

    u[0] = _mm_mullo_epi32(s[16], txmat_16_0);
    u[1] = _mm_mullo_epi32(s[16], txmat_16_1);
    v[0] = _mm_slli_epi32(s[0], 6);
    v[1] = _mm_slli_epi32(s[0], 6);

    g[0] = _mm_add_epi32(v[0], u[0]);
    g[2] = _mm_sub_epi32(v[1], u[1]);
    g[1] = _mm_add_epi32(v[1], u[1]);
    g[3] = _mm_sub_epi32(v[0], u[0]);

    for (k = 0; k < 4; k++) {
      e[k] = _mm_add_epi32(g[k], h[k]);
      e[k + 4] = _mm_sub_epi32(g[3 - k], h[3 - k]);
    }

    for (k = 0; k < 8; k++) {
      c[k] = _mm_add_epi32(e[k], f[k]);
      c[k + 8] = _mm_sub_epi32(e[7 - k], f[7 - k]);
    }

    for (k = 0; k < 16; k++) {
      a[k] = _mm_add_epi32(c[k], d[k]);
      a[k + 16] = _mm_sub_epi32(c[15 - k], d[15 - k]);
    }

    for (k = 0; k < 32; k++) {
      g[0] = _mm_add_epi32(v_offset, _mm_add_epi32(a[k], b[k]));
      g[1] = _mm_add_epi32(v_offset, _mm_sub_epi32(a[31 - k], b[31 - k]));

      g[0] = _mm_srai_epi32(g[0], shift);
      g[1] = _mm_srai_epi32(g[1], shift);

      g[0] = _mm_min_epi32(g[0], v_coef_max);
      g[0] = _mm_max_epi32(g[0], v_coef_min);
      g[1] = _mm_min_epi32(g[1], v_coef_max);
      g[1] = _mm_max_epi32(g[1], v_coef_min);

      _mm_storeu_si128((__m128i *)(dst + k * line), g[0]);
      _mm_storeu_si128((__m128i *)(dst + (k + 32) * line), g[1]);
    }
  }
}
#endif  // !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG

void inv_txfm_idtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  __m128i v_offset = _mm_set1_epi32(offset);
  __m128i v_coef_min = _mm_set1_epi32(coef_min);
  __m128i v_coef_max = _mm_set1_epi32(coef_max);
  for (int i = 0; i < nz_line; i++) {
    // Load 8 int16_t values and extend to int32_t
    __m128i v_src = _mm_lddqu_si128((__m128i *)&src[i * tx1d_size]);

    // Multiply by scale and add offset
    __m128i v_scaled = _mm_slli_epi32(v_src, 7);
    __m128i v_offsetted = _mm_add_epi32(v_scaled, v_offset);

    // Right shift by shift
    __m128i v_shifted = _mm_srai_epi32(v_offsetted, shift);

    // Clamp to coef_min and coef_max
    v_shifted = _mm_min_epi32(v_shifted, v_coef_max);
    v_shifted = _mm_max_epi32(v_shifted, v_coef_min);

    // Extract 16bit integer and store
    dst[0 * line + i] = _mm_extract_epi32(v_shifted, 0);
    dst[1 * line + i] = _mm_extract_epi32(v_shifted, 1);
    dst[2 * line + i] = _mm_extract_epi32(v_shifted, 2);
    dst[3 * line + i] = _mm_extract_epi32(v_shifted, 3);
  }
}

void inv_txfm_idtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int scale = 181;

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_scale = _mm256_set1_epi32(scale);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int i = 0; i < nz_line; i++) {
    // Load 8 int16_t values and extend to int32_t
    __m256i v_src = _mm256_loadu_si256((__m256i *)&src[i * tx1d_size]);

    // Multiply by scale and add offset
    __m256i v_scaled = _mm256_mullo_epi32(v_src, v_scale);
    __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);

    // Right shift by shift
    __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

    // Clamp to coef_min and coef_max
    v_shifted = _mm256_min_epi32(v_shifted, v_coef_max);
    v_shifted = _mm256_max_epi32(v_shifted, v_coef_min);

    // Extract 16bit integer and store
    dst[0 * line + i] = _mm256_extract_epi32(v_shifted, 0);
    dst[1 * line + i] = _mm256_extract_epi32(v_shifted, 1);
    dst[2 * line + i] = _mm256_extract_epi32(v_shifted, 2);
    dst[3 * line + i] = _mm256_extract_epi32(v_shifted, 3);
    dst[4 * line + i] = _mm256_extract_epi32(v_shifted, 4);
    dst[5 * line + i] = _mm256_extract_epi32(v_shifted, 5);
    dst[6 * line + i] = _mm256_extract_epi32(v_shifted, 6);
    dst[7 * line + i] = _mm256_extract_epi32(v_shifted, 7);
  }
}

void inv_txfm_idtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j += 8) {
      // Load 8 int16_t values and extend to int32_t
      __m256i v_src = _mm256_loadu_si256((__m256i *)&src[i * tx1d_size + j]);

      // Multiply by scale and add offset
      __m256i v_scaled = _mm256_slli_epi32(v_src, 8);
      __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);

      // Right shift by shift
      __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

      // Clamp to coef_min and coef_max
      v_shifted = _mm256_min_epi32(v_shifted, v_coef_max);
      v_shifted = _mm256_max_epi32(v_shifted, v_coef_min);

      // Extract 16bit integer and store
      dst[(0 + j) * line + i] = _mm256_extract_epi32(v_shifted, 0);
      dst[(1 + j) * line + i] = _mm256_extract_epi32(v_shifted, 1);
      dst[(2 + j) * line + i] = _mm256_extract_epi32(v_shifted, 2);
      dst[(3 + j) * line + i] = _mm256_extract_epi32(v_shifted, 3);
      dst[(4 + j) * line + i] = _mm256_extract_epi32(v_shifted, 4);
      dst[(5 + j) * line + i] = _mm256_extract_epi32(v_shifted, 5);
      dst[(6 + j) * line + i] = _mm256_extract_epi32(v_shifted, 6);
      dst[(7 + j) * line + i] = _mm256_extract_epi32(v_shifted, 7);
    }
  }
}

void inv_txfm_idtx_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  const int scale = 362;

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_scale = _mm256_set1_epi32(scale);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j += 8) {
      // Load 8 int16_t values and extend to int32_t
      __m256i v_src = _mm256_loadu_si256((__m256i *)&src[i * tx1d_size + j]);

      // Multiply by scale and add offset
      __m256i v_scaled = _mm256_mullo_epi32(v_src, v_scale);
      __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);

      // Right shift by shift
      __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

      // Clamp to coef_min and coef_max
      v_shifted = _mm256_min_epi32(v_shifted, v_coef_max);
      v_shifted = _mm256_max_epi32(v_shifted, v_coef_min);

      // Extract 16bit integer and store
      dst[(0 + j) * line + i] = _mm256_extract_epi32(v_shifted, 0);
      dst[(1 + j) * line + i] = _mm256_extract_epi32(v_shifted, 1);
      dst[(2 + j) * line + i] = _mm256_extract_epi32(v_shifted, 2);
      dst[(3 + j) * line + i] = _mm256_extract_epi32(v_shifted, 3);
      dst[(4 + j) * line + i] = _mm256_extract_epi32(v_shifted, 4);
      dst[(5 + j) * line + i] = _mm256_extract_epi32(v_shifted, 5);
      dst[(6 + j) * line + i] = _mm256_extract_epi32(v_shifted, 6);
      dst[(7 + j) * line + i] = _mm256_extract_epi32(v_shifted, 7);
    }
  }
}

void inv_txfm_adst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_adst_size4[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[(j + 0) * tx1d_size + k]);
      __m128i tmp_src1 = _mm_set1_epi32(src[(j + 1) * tx1d_size + k]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m128i tmp_val0 = _mm_loadu_si128((__m128i *)(tx_mat + k * tx1d_size));
      __m256i tmp_val = _mm256_set_m128i(tmp_val0, tmp_val0);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[0 * line + j + 1] = _mm256_extract_epi32(sum, 4);
    dst[1 * line + j + 1] = _mm256_extract_epi32(sum, 5);
    dst[2 * line + j + 1] = _mm256_extract_epi32(sum, 6);
    dst[3 * line + j + 1] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_fdst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_fdst_size4[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[(j + 0) * tx1d_size + k]);
      __m128i tmp_src1 = _mm_set1_epi32(src[(j + 1) * tx1d_size + k]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m128i tmp_val0 = _mm_loadu_si128((__m128i *)(tx_mat + k * tx1d_size));
      __m256i tmp_val = _mm256_set_m128i(tmp_val0, tmp_val0);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[0 * line + j + 1] = _mm256_extract_epi32(sum, 4);
    dst[1 * line + j + 1] = _mm256_extract_epi32(sum, 5);
    dst[2 * line + j + 1] = _mm256_extract_epi32(sum, 6);
    dst[3 * line + j + 1] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_adst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_adst_size8[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
      __m256i tmp_val = _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size));
      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[4 * line + j] = _mm256_extract_epi32(sum, 4);
    dst[5 * line + j] = _mm256_extract_epi32(sum, 5);
    dst[6 * line + j] = _mm256_extract_epi32(sum, 6);
    dst[7 * line + j] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_fdst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_fdst_size8[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
      __m256i tmp_val = _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size));
      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[4 * line + j] = _mm256_extract_epi32(sum, 4);
    dst[5 * line + j] = _mm256_extract_epi32(sum, 5);
    dst[6 * line + j] = _mm256_extract_epi32(sum, 6);
    dst[7 * line + j] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_adst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_adst_size16[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
        __m256i tmp_val =
            _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size + i));
        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Clamp to coef_min and coef_max
      sum = _mm256_min_epi32(sum, v_coef_max);
      sum = _mm256_max_epi32(sum, v_coef_min);

      // Store results
      dst[(i + 0) * line + j] = _mm256_extract_epi32(sum, 0);
      dst[(i + 1) * line + j] = _mm256_extract_epi32(sum, 1);
      dst[(i + 2) * line + j] = _mm256_extract_epi32(sum, 2);
      dst[(i + 3) * line + j] = _mm256_extract_epi32(sum, 3);
      dst[(i + 4) * line + j] = _mm256_extract_epi32(sum, 4);
      dst[(i + 5) * line + j] = _mm256_extract_epi32(sum, 5);
      dst[(i + 6) * line + j] = _mm256_extract_epi32(sum, 6);
      dst[(i + 7) * line + j] = _mm256_extract_epi32(sum, 7);
    }
  }
}

void inv_txfm_fdst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_fdst_size16[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
        __m256i tmp_val =
            _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size + i));
        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Clamp to coef_min and coef_max
      sum = _mm256_min_epi32(sum, v_coef_max);
      sum = _mm256_max_epi32(sum, v_coef_min);

      // Store results
      dst[(i + 0) * line + j] = _mm256_extract_epi32(sum, 0);
      dst[(i + 1) * line + j] = _mm256_extract_epi32(sum, 1);
      dst[(i + 2) * line + j] = _mm256_extract_epi32(sum, 2);
      dst[(i + 3) * line + j] = _mm256_extract_epi32(sum, 3);
      dst[(i + 4) * line + j] = _mm256_extract_epi32(sum, 4);
      dst[(i + 5) * line + j] = _mm256_extract_epi32(sum, 5);
      dst[(i + 6) * line + j] = _mm256_extract_epi32(sum, 6);
      dst[(i + 7) * line + j] = _mm256_extract_epi32(sum, 7);
    }
  }
}

void inv_txfm_ddtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_ddtx_size4[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[(j + 0) * tx1d_size + k]);
      __m128i tmp_src1 = _mm_set1_epi32(src[(j + 1) * tx1d_size + k]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m128i tmp_val0 = _mm_loadu_si128((__m128i *)(tx_mat + k * tx1d_size));
      __m256i tmp_val = _mm256_set_m128i(tmp_val0, tmp_val0);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[0 * line + j + 1] = _mm256_extract_epi32(sum, 4);
    dst[1 * line + j + 1] = _mm256_extract_epi32(sum, 5);
    dst[2 * line + j + 1] = _mm256_extract_epi32(sum, 6);
    dst[3 * line + j + 1] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_ddtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_ddtx_size8[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
      __m256i tmp_val = _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size));
      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[0 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[4 * line + j] = _mm256_extract_epi32(sum, 4);
    dst[5 * line + j] = _mm256_extract_epi32(sum, 5);
    dst[6 * line + j] = _mm256_extract_epi32(sum, 6);
    dst[7 * line + j] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_ddtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_ddtx_size16[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
        __m256i tmp_val =
            _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size + i));
        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Clamp to coef_min and coef_max
      sum = _mm256_min_epi32(sum, v_coef_max);
      sum = _mm256_max_epi32(sum, v_coef_min);

      // Store results
      dst[(i + 0) * line + j] = _mm256_extract_epi32(sum, 0);
      dst[(i + 1) * line + j] = _mm256_extract_epi32(sum, 1);
      dst[(i + 2) * line + j] = _mm256_extract_epi32(sum, 2);
      dst[(i + 3) * line + j] = _mm256_extract_epi32(sum, 3);
      dst[(i + 4) * line + j] = _mm256_extract_epi32(sum, 4);
      dst[(i + 5) * line + j] = _mm256_extract_epi32(sum, 5);
      dst[(i + 6) * line + j] = _mm256_extract_epi32(sum, 6);
      dst[(i + 7) * line + j] = _mm256_extract_epi32(sum, 7);
    }
  }
}

void inv_txfm_fddt_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_ddtx_size4[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[(j + 0) * tx1d_size + k]);
      __m128i tmp_src1 = _mm_set1_epi32(src[(j + 1) * tx1d_size + k]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m128i tmp_val0 = _mm_loadu_si128((__m128i *)(tx_mat + k * tx1d_size));
      __m256i tmp_val = _mm256_set_m128i(tmp_val0, tmp_val0);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[3 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[0 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[3 * line + j + 1] = _mm256_extract_epi32(sum, 4);
    dst[2 * line + j + 1] = _mm256_extract_epi32(sum, 5);
    dst[1 * line + j + 1] = _mm256_extract_epi32(sum, 6);
    dst[0 * line + j + 1] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_fddt_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line, const int coef_min,
                              const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_ddtx_size8[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
      __m256i tmp_val = _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size));
      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Clamp to coef_min and coef_max
    sum = _mm256_min_epi32(sum, v_coef_max);
    sum = _mm256_max_epi32(sum, v_coef_min);

    // Store results
    dst[7 * line + j] = _mm256_extract_epi32(sum, 0);
    dst[6 * line + j] = _mm256_extract_epi32(sum, 1);
    dst[5 * line + j] = _mm256_extract_epi32(sum, 2);
    dst[4 * line + j] = _mm256_extract_epi32(sum, 3);
    dst[3 * line + j] = _mm256_extract_epi32(sum, 4);
    dst[2 * line + j] = _mm256_extract_epi32(sum, 5);
    dst[1 * line + j] = _mm256_extract_epi32(sum, 6);
    dst[0 * line + j] = _mm256_extract_epi32(sum, 7);
  }
}

void inv_txfm_fddt_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, const int coef_min,
                               const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_ddtx_size16[INV_TXFM][0];

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_coef_min = _mm256_set1_epi32(coef_min);
  __m256i v_coef_max = _mm256_set1_epi32(coef_max);

  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[j * tx1d_size + k]);
        __m256i tmp_val =
            _mm256_loadu_si256((__m256i *)(tx_mat + k * tx1d_size + i));
        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Clamp to coef_min and coef_max
      sum = _mm256_min_epi32(sum, v_coef_max);
      sum = _mm256_max_epi32(sum, v_coef_min);

      // Store results
      dst[(15 - i) * line + j] = _mm256_extract_epi32(sum, 0);
      dst[(14 - i) * line + j] = _mm256_extract_epi32(sum, 1);
      dst[(13 - i) * line + j] = _mm256_extract_epi32(sum, 2);
      dst[(12 - i) * line + j] = _mm256_extract_epi32(sum, 3);
      dst[(11 - i) * line + j] = _mm256_extract_epi32(sum, 4);
      dst[(10 - i) * line + j] = _mm256_extract_epi32(sum, 5);
      dst[(9 - i) * line + j] = _mm256_extract_epi32(sum, 6);
      dst[(8 - i) * line + j] = _mm256_extract_epi32(sum, 7);
    }
  }
}

void inv_transform_1d_avx2(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max, const int tx_type_index,
                           const int size_index) {
  switch (size_index) {
    case 0:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 1:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 2:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 3:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size32_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size32_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
#if !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
    case 4:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size64_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
#endif  // !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
    default: assert(0); break;
  }
}

void inv_txfm_avx2(const tran_low_t *input, uint16_t *dest, int stride,
                   const TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;
  TX_TYPE tx_type = txfm_param->tx_type;

#if CONFIG_CHROMA_LARGE_TX
#if CONFIG_TX64
#if CONFIG_TX64_SEQ_FLAG
  const int use_resample =
      (txfm_param->plane_type == PLANE_TYPE_UV || txfm_param->t64resample) ? 1
                                                                           : 0;
  int width = AOMMIN(MAX_TX_SIZE >> use_resample, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> use_resample, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - use_resample, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - use_resample, tx_size_high_log2[tx_size]) - 2;
#else
  int width = AOMMIN(MAX_TX_SIZE >> 1, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> 1, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_high_log2[tx_size]) - 2;
#endif  // CONFIG_TX64_SEQ_FLAG
#else
  const int is_chroma = (txfm_param->plane_type == PLANE_TYPE_UV) ? 1 : 0;
  int width = AOMMIN(MAX_TX_SIZE >> is_chroma, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> is_chroma, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - is_chroma, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - is_chroma, tx_size_high_log2[tx_size]) - 2;
#endif  // CONFIG_TX64
#else
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;
#endif  // CONFIG_CHROMA_LARGE_TX || CONFIG_TX64

  const int intermediate_bitdepth = txfm_param->bd + 8;
  const int rng_min = -(1 << (intermediate_bitdepth - 1));
  const int rng_max = (1 << (intermediate_bitdepth - 1)) - 1;

#if CONFIG_CHROMA_LARGE_TX
  const int col_rng_min = -(1 << txfm_param->bd);
  const int col_rng_max = (1 << txfm_param->bd) - 1;
#endif  // CONFIG_CHROMA_LARGE_TX

  if (txfm_param->lossless) {
    assert(tx_type == DCT_DCT);
    av1_highbd_iwht4x4_add(input, dest, stride, txfm_param->eob,
                           txfm_param->bd);
    return;
  }

  int tx_type_row = g_hor_tx_type[tx_type];
  int tx_type_col = g_ver_tx_type[tx_type];

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

  int skipWidth = width > 32 ? width - 32 : 0;
  int skipHeight = height > 32 ? height - 32 : 0;

  int block[MAX_TX_SQUARE] = { 0 };
  int temp[MAX_TX_SQUARE] = { 0 };

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;

  // Load clamp boundaries into SIMD registers
  __m256i vcoefmin = _mm256_set1_epi32(-(1 << (txfm_param->bd + 7)));
  __m256i vcoefmax = _mm256_set1_epi32((1 << (txfm_param->bd + 7)) - 1);
  if (skipWidth) {
    for (int y = 0; y < height; y++) {
      if (sqrt2) {
        __m256i scale_vector = _mm256_set1_epi64x((int64_t)NewInvSqrt2);
        __m128i shift_bits = _mm_set1_epi64x(NewSqrt2Bits);
        __m256i round_offset = _mm256_set1_epi64x(1LL << (NewSqrt2Bits - 1));
        __m256i idx = _mm256_set_epi32(6, 4, 2, 0, 6, 4, 2, 0);
        for (int i = 0; i < 32; i += 8) {
          __m256i data = _mm256_loadu_si256((__m256i *)(input + y * 32 + i));

          __m256i data0 =
              _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 0));
          data0 = _mm256_mul_epi32(data0, scale_vector);
          data0 = _mm256_add_epi64(data0, round_offset);
          data0 = _mm256_srl_epi64(data0, shift_bits);
          data0 = _mm256_permutevar8x32_epi32(data0, idx);

          __m256i data1 =
              _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 1));
          data1 = _mm256_mul_epi32(data1, scale_vector);
          data1 = _mm256_add_epi64(data1, round_offset);
          data1 = _mm256_srl_epi64(data1, shift_bits);
          data1 = _mm256_permutevar8x32_epi32(data1, idx);

          data = _mm256_blend_epi32(data0, data1, 0b11110000);

          data = _mm256_min_epi32(_mm256_max_epi32(data, vcoefmin), vcoefmax);
          _mm256_storeu_si256((__m256i *)(block + y * width + i), data);
        }
      } else {
        for (int i = 0; i < 32; i += 8) {
          __m256i data = _mm256_loadu_si256((__m256i *)(input + y * 32 + i));
          data = _mm256_min_epi32(_mm256_max_epi32(data, vcoefmin), vcoefmax);
          _mm256_storeu_si256((__m256i *)(block + y * width + i), data);
        }
      }
    }
  } else {
    if (sqrt2) {
      __m256i scale_vector = _mm256_set1_epi64x((int64_t)NewInvSqrt2);
      __m128i shift_bits = _mm_set1_epi64x(NewSqrt2Bits);
      __m256i round_offset = _mm256_set1_epi64x(1LL << (NewSqrt2Bits - 1));
      __m256i idx = _mm256_set_epi32(6, 4, 2, 0, 6, 4, 2, 0);
      for (int i = 0; i < AOMMIN(1024, width * height); i += 8) {
        __m256i data = _mm256_loadu_si256((__m256i *)(input + i));

        __m256i data0 =
            _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 0));
        data0 = _mm256_mul_epi32(data0, scale_vector);
        data0 = _mm256_add_epi64(data0, round_offset);
        data0 = _mm256_srl_epi64(data0, shift_bits);
        data0 = _mm256_permutevar8x32_epi32(data0, idx);

        __m256i data1 =
            _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 1));
        data1 = _mm256_mul_epi32(data1, scale_vector);
        data1 = _mm256_add_epi64(data1, round_offset);
        data1 = _mm256_srl_epi64(data1, shift_bits);
        data1 = _mm256_permutevar8x32_epi32(data1, idx);

        data = _mm256_blend_epi32(data0, data1, 0b11110000);

        data = _mm256_min_epi32(_mm256_max_epi32(data, vcoefmin), vcoefmax);
        _mm256_storeu_si256((__m256i *)(block + i), data);
      }
    } else {
      for (int i = 0; i < AOMMIN(1024, width * height); i += 8) {
        __m256i data = _mm256_loadu_si256((__m256i *)(input + i));
        data = _mm256_min_epi32(_mm256_max_epi32(data, vcoefmin), vcoefmax);
        _mm256_storeu_si256((__m256i *)(block + i), data);
      }
    }
  }

  const int shift_1st = inv_tx_shift[tx_size][0];
  const int shift_2nd = inv_tx_shift[tx_size][1];

  assert(shift_1st >= 0);
  assert(shift_2nd >= 0);

  inv_transform_1d_avx2(block, temp, shift_1st, height, skipHeight, skipWidth,
                        rng_min, rng_max, tx_type_row, tx_wide_index);

  inv_transform_1d_avx2(temp, block, shift_2nd, width, 0, skipHeight,
#if CONFIG_CHROMA_LARGE_TX
                        col_rng_min, col_rng_max,
#else
                        rng_min, rng_max,
#endif  // CONFIG_CHROMA_LARGE_TX
                        tx_type_col, tx_high_index);

#if CONFIG_CHROMA_LARGE_TX
  // TODO(any): optimize with AVX2 SIMD
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
    memcpy(temp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[2 * y * width + x] = temp[y * width + x];
        block[(2 * y + 1) * width + x] = temp[y * width + x];
      }
    }
    height = tx_size_high[tx_size];
  }
#endif  // CONFIG_CHROMA_LARGE_TX

  // Load clamp boundaries into SIMD registers
  __m256i vpixmin = _mm256_setzero_si256();
  __m256i vpixmax = _mm256_set1_epi32((1 << txfm_param->bd) - 1);
  if ((width & 7) == 0) {
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

void process_inv_idtx_add_4x4_avx2(const tran_low_t *input, int in_stride,
                                   uint16_t *dst, int out_stride,
                                   int scale_bits, int bd) {
  const int32_t max_val = (1 << bd) - 1;
  const int in_stride_bytes = in_stride * 4;
  const int out_stride_bytes = out_stride * 2;

  // === Part 1: Load and Process Source (int32_t) ===
  const uint8_t *src_bytes = (const uint8_t *)input;
  __m128i s_r0 =
      _mm_loadu_si128((const __m128i *)(src_bytes + 0 * in_stride_bytes));
  __m128i s_r1 =
      _mm_loadu_si128((const __m128i *)(src_bytes + 1 * in_stride_bytes));
  __m128i s_r2 =
      _mm_loadu_si128((const __m128i *)(src_bytes + 2 * in_stride_bytes));
  __m128i s_r3 =
      _mm_loadu_si128((const __m128i *)(src_bytes + 3 * in_stride_bytes));
  __m256i src_data_01 = _mm256_set_m128i(s_r1, s_r0);  // els 0..7
  __m256i src_data_23 = _mm256_set_m128i(s_r3, s_r2);  // els 8..15
  __m256i shifted_01;
  __m256i shifted_23;
  if (scale_bits == 0) {
    shifted_01 = src_data_01;
    shifted_23 = src_data_23;
  } else {
    shifted_01 = _mm256_srai_epi32(src_data_01, scale_bits);
    shifted_23 = _mm256_srai_epi32(src_data_23, scale_bits);
  }

  // === Part 2: Load and Widen Destination (uint16_t -> int32_t)
  // --- 2a. Load dst (4x4 uint16) ---
  const uint8_t *dst_read_bytes =
      (const uint8_t *)dst;  // Use const for reading phase
  __m128i d_r0 = _mm_loadl_epi64(
      (const __m128i *)(dst_read_bytes +
                        0 * out_stride_bytes));  // Load Row 0 (8 bytes)
  __m128i d_r1 = _mm_loadl_epi64(
      (const __m128i *)(dst_read_bytes + 1 * out_stride_bytes));  // Load Row 1
  __m128i d_r2 = _mm_loadl_epi64(
      (const __m128i *)(dst_read_bytes + 2 * out_stride_bytes));  // Load Row 2
  __m128i d_r3 = _mm_loadl_epi64(
      (const __m128i *)(dst_read_bytes + 3 * out_stride_bytes));  // Load Row 3
  // Combine pairs into 128-bit lanes (maintains row order within lanes)
  __m128i d_lane01 =
      _mm_unpacklo_epi64(d_r0, d_r1);  // [Row1 | Row0] (16-bit elements)
  __m128i d_lane23 =
      _mm_unpacklo_epi64(d_r2, d_r3);  // [Row3 | Row2] (16-bit elements)

  // --- 2b. Widen dst uint16 -> int32 (ZERO-extend) --- *MODIFIED*
  // Use _mm256_cvtepu16_epi32 for zero extension
  __m256i dst_data_01_s32 =
      _mm256_cvtepu16_epi32(d_lane01);  // Zero-extends elements 0..7
  __m256i dst_data_23_s32 =
      _mm256_cvtepu16_epi32(d_lane23);  // Zero-extends elements 8..15

  // === Part 3: Add, Clamp [0, max_val], Pack/Saturate (to uint16_t) ===

  // --- 3a. Add ---
  // Addition is still performed using 32-bit signed arithmetic
  __m256i sum_01 = _mm256_add_epi32(shifted_01, dst_data_01_s32);
  __m256i sum_23 = _mm256_add_epi32(shifted_23, dst_data_23_s32);

  // --- 3b. Clamp [0, max_val] ---
  // (Clamping logic remains the same on the int32 sum)
  const __m256i v_zero = _mm256_setzero_si256();
  const __m256i v_max_val = _mm256_set1_epi32(max_val);
  __m256i clamped_01_tmp = _mm256_max_epi32(sum_01, v_zero);
  __m256i clamped_23_tmp = _mm256_max_epi32(sum_23, v_zero);
  __m256i clamped_01 =
      _mm256_min_epi32(clamped_01_tmp, v_max_val);  // els 0..7 clamped
  __m256i clamped_23 =
      _mm256_min_epi32(clamped_23_tmp, v_max_val);  // els 8..15 clamped
  // clamped_01, clamped_23 contain int32 values guaranteed >= 0

  // --- 3c. Pack clamped int32 -> uint16 with UNSIGNED Saturation ---
  // *MODIFIED* Use _mm_packus_epi32 which saturates int32 to [0, 65535]
  // (uint16_t range)
  __m128i packed_clamped_r0r1 =
      _mm_packus_epi32(  // <-- pack*us* for unsigned saturation
          _mm256_castsi256_si128(
              clamped_01),  // Low lane of clamped_01 (els 0..3)
          _mm256_extracti128_si256(clamped_01,
                                   1)  // High lane of clamped_01 (els 4..7)
      );  // Result: [el7..el4 | el3..el0] (saturated uint16_t)

  __m128i packed_clamped_r2r3 = _mm_packus_epi32(  // <-- pack*us*
      _mm256_castsi256_si128(clamped_23),  // Low lane of clamped_23 (els 8..11)
      _mm256_extracti128_si256(clamped_23,
                               1)  // High lane of clamped_23 (els 12..15)
  );  // Result: [el15..el12 | el11..el8] (saturated uint16_t)

  // Combine into final 256-bit register holding uint16_t results
  // Layout: [Row3 | Row2 | Row1 | Row0] (16-bit elements)
  __m256i final_result_u16 =
      _mm256_set_m128i(packed_clamped_r2r3, packed_clamped_r0r1);

  // === Part 4: Store Result back to dst (uint16_t) ===
  // (Store mechanism identical, pointer type `uint16_t*` is correct)
  uint8_t *dst_write_bytes = (uint8_t *)dst;  // Cast for byte arithmetic
  __m128i res_lane01 = _mm256_castsi256_si128(final_result_u16);
  __m128i res_lane23 = _mm256_extracti128_si256(final_result_u16, 1);
  // Store Row 0 (8 bytes)
  _mm_storel_epi64((__m128i *)(dst_write_bytes + 0 * out_stride_bytes),
                   res_lane01);
  // Extract and Store Row 1 (8 bytes)
  __m128i res_row1 = _mm_bsrli_si128(res_lane01, 8);
  _mm_storel_epi64((__m128i *)(dst_write_bytes + 1 * out_stride_bytes),
                   res_row1);
  // Store Row 2 (8 bytes)
  _mm_storel_epi64((__m128i *)(dst_write_bytes + 2 * out_stride_bytes),
                   res_lane23);
  // Extract and Store Row 3 (8 bytes)
  __m128i res_row3 = _mm_bsrli_si128(res_lane23, 8);
  _mm_storel_epi64((__m128i *)(dst_write_bytes + 3 * out_stride_bytes),
                   res_row3);
}

void av1_lossless_inv_idtx_add_avx2(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av1_get_tx_scale(txfm_param->tx_size);
  const int bd = txfm_param->bd;

  for (int i = 0; i < txh; i += MI_SIZE) {
    for (int j = 0; j < txw; j += MI_SIZE) {
      process_inv_idtx_add_4x4_avx2(input + i * txw + j, txw,
                                    dest + i * stride + j, stride, scale_bits,
                                    bd);
    }
  }
}
