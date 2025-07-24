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
#include <immintrin.h> /*AVX2*/

#include "config/aom_config.h"
#include "config/av1_rtcd.h"
#include "av1/common/av1_txfm.h"
#include "aom_dsp/txfm_common.h"
#include "aom_ports/mem.h"
#include "aom_dsp/x86/txfm_common_sse2.h"
#include "av1/common/txb_common.h"

static INLINE __m256i round_power_of_two_signed_avx2(__m256i v_val_d,
                                                     int bits) {
  const __m256i v_bias_d = _mm256_set1_epi32((1 << bits) >> 1);
  const __m256i v_sign_d = _mm256_srai_epi32(v_val_d, 31);
  const __m256i v_tmp_d =
      _mm256_add_epi32(_mm256_add_epi32(v_val_d, v_bias_d), v_sign_d);
  return _mm256_srai_epi32(v_tmp_d, bits);
}

static INLINE __m128i round_power_of_two_signed_sse2(__m128i v_val_d,
                                                     int bits) {
  const __m128i v_bias_d = _mm_set1_epi32((1 << bits) >> 1);
  const __m128i v_sign_d = _mm_srai_epi32(v_val_d, 31);
  const __m128i v_tmp_d =
      _mm_add_epi32(_mm_add_epi32(v_val_d, v_bias_d), v_sign_d);
  return _mm_srai_epi32(v_tmp_d, bits);
}

void av1_fwd_cross_chroma_tx_block_avx2(tran_low_t *coeff_c1,
                                        tran_low_t *coeff_c2, TX_SIZE tx_size,
                                        CctxType cctx_type, const int bd) {
  if (cctx_type == CCTX_NONE) return;
  const int ncoeffs = av1_get_max_eob(tx_size);
  int32_t *src_c1 = (int32_t *)coeff_c1;
  int32_t *src_c2 = (int32_t *)coeff_c2;

  const int angle_idx = cctx_type - CCTX_START;
  const __m256i cos_t = _mm256_set1_epi32(cctx_mtx[angle_idx][0]);
  const __m256i sin_t = _mm256_set1_epi32(cctx_mtx[angle_idx][1]);
  const __m256i max_value = _mm256_set1_epi32((1 << (7 + bd)) - 1);
  const __m256i min_value = _mm256_set1_epi32(-(1 << (7 + bd)));

  for (int i = 0; i < ncoeffs; i += 8) {
    // Load 8 elements from both coeff_c1 and coeff_c2
    const __m256i v_c1 = _mm256_loadu_si256((__m256i *)&src_c1[i]);
    const __m256i v_c2 = _mm256_loadu_si256((__m256i *)&src_c2[i]);

    // Perform matrix multiplication
    const __m256i v_tmp0 = _mm256_mullo_epi32(cos_t, v_c1);
    const __m256i v_tmp1 = _mm256_mullo_epi32(sin_t, v_c2);
    const __m256i v_tmp2 = _mm256_mullo_epi32(sin_t, v_c1);
    const __m256i v_tmp3 = _mm256_mullo_epi32(cos_t, v_c2);

    // Add and round the results to CCTX_PREC_BITS
    __m256i v_res0 = round_power_of_two_signed_avx2(
        _mm256_add_epi32(v_tmp0, v_tmp1), CCTX_PREC_BITS);
    __m256i v_res1 = round_power_of_two_signed_avx2(
        _mm256_sub_epi32(v_tmp3, v_tmp2), CCTX_PREC_BITS);

    // Clamp to valid range
    v_res0 = _mm256_min_epi32(_mm256_max_epi32(v_res0, min_value), max_value);
    v_res1 = _mm256_min_epi32(_mm256_max_epi32(v_res1, min_value), max_value);

    // Round and store the results back to src_c1 and src_c2
    _mm256_storeu_si256((__m256i *)&src_c1[i], v_res0);
    _mm256_storeu_si256((__m256i *)&src_c2[i], v_res1);
  }
}

static void fwd_stxfm_transpose_4x8_avx2(__m256i *in, __m128i *out) {
  __m256i x0, x1;

  // First step: unpack 32-bit elements within lanes
  const __m256i u0 = _mm256_unpacklo_epi32(in[0], in[1]);
  const __m256i u1 = _mm256_unpackhi_epi32(in[0], in[1]);
  const __m256i u2 = _mm256_unpacklo_epi32(in[2], in[3]);
  const __m256i u3 = _mm256_unpackhi_epi32(in[2], in[3]);

  // Second step: unpack 64-bit elements within lanes
  x0 = _mm256_unpacklo_epi64(u0, u2);  // A0 A2 B0 B2 C0 C2 D0 D2
  x1 = _mm256_unpackhi_epi64(u0, u2);  // A1 A3 B1 B3 C1 C3 D1 D3

  // Extract low and high 128-bit lanes to get the transposed 128-bit rows
  out[0] = _mm256_castsi256_si128(x0);       // A0 A2 B0 B2
  out[1] = _mm256_castsi256_si128(x1);       // A1 A3 B1 B3
  out[4] = _mm256_extracti128_si256(x0, 1);  // C0 C2 D0 D2
  out[5] = _mm256_extracti128_si256(x1, 1);  // C1 C3 D1 D3

  // Repeat for the upper half
  x0 = _mm256_unpacklo_epi64(u1, u3);  // A4 A6 B4 B6 C4 C6 D4 D6
  x1 = _mm256_unpackhi_epi64(u1, u3);  // A5 A7 B5 B7 C5 C7 D5 D7

  out[2] = _mm256_castsi256_si128(x0);       // A4 A6 B4 B6
  out[3] = _mm256_castsi256_si128(x1);       // A5 A7 B5 B7
  out[6] = _mm256_extracti128_si256(x0, 1);  // C4 C6 D4 D6
  out[7] = _mm256_extracti128_si256(x1, 1);  // C5 C7 D5 D7
}

static void fwd_stxfm_transpose_8x8_avx2(__m256i *in, __m256i *out) {
  __m256i x0, x1;

  const __m256i u0 = _mm256_unpacklo_epi32(in[0], in[1]);
  const __m256i u1 = _mm256_unpackhi_epi32(in[0], in[1]);

  const __m256i u2 = _mm256_unpacklo_epi32(in[2], in[3]);
  const __m256i u3 = _mm256_unpackhi_epi32(in[2], in[3]);

  const __m256i u4 = _mm256_unpacklo_epi32(in[4], in[5]);
  const __m256i u5 = _mm256_unpackhi_epi32(in[4], in[5]);

  const __m256i u6 = _mm256_unpacklo_epi32(in[6], in[7]);
  const __m256i u7 = _mm256_unpackhi_epi32(in[6], in[7]);

  x0 = _mm256_unpacklo_epi64(u0, u2);
  x1 = _mm256_unpacklo_epi64(u4, u6);
  out[0] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[4] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpackhi_epi64(u0, u2);
  x1 = _mm256_unpackhi_epi64(u4, u6);
  out[1] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[5] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpacklo_epi64(u1, u3);
  x1 = _mm256_unpacklo_epi64(u5, u7);
  out[2] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[6] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpackhi_epi64(u1, u3);
  x1 = _mm256_unpackhi_epi64(u5, u7);
  out[3] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[7] = _mm256_permute2f128_si256(x0, x1, 0x31);
}

// Forward secondary transform
void fwd_stxfm_avx2(tran_low_t *src, tran_low_t *dst,
                    const PREDICTION_MODE mode, const uint8_t stx_idx,
                    const int size, const int bd) {
  assert(stx_idx < 4);
  // Secondary transform kernels are stored as 32-bit integers to match SIMD
  // processing needs. This avoids on-the-fly conversion from int16_t to int32_t
  // during execution by letting SIMD variants directly load the pre-converted
  // filter weights.
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

  const int shift = 7;

  if (reduced_height == 8) {
    assert(reduced_width % 8 == 0);
    __m256i resi_vec[8];
    __m256i resi_vec_out[8];
    const __m256i max_value = _mm256_set1_epi32((1 << (7 + bd)) - 1);
    const __m256i min_value = _mm256_set1_epi32(-(1 << (7 + bd)));
    const int *out = dst;
    const int *src_ptr = src;
    const __m256i zeros = _mm256_setzero_si256();
    resi_vec[0] = zeros;
    resi_vec[1] = zeros;
    resi_vec[2] = zeros;
    resi_vec[3] = zeros;
    resi_vec[4] = zeros;
    resi_vec[5] = zeros;
    resi_vec[6] = zeros;
    resi_vec[7] = zeros;
    for (int i = 0; i < reduced_width; i += 8) {
      __m256i kernel_vec0 = _mm256_loadu_si256((__m256i *)(kernel + i));
      __m256i kernel_vec1 =
          _mm256_loadu_si256((__m256i *)(kernel + reduced_width + i));
      __m256i kernel_vec2 =
          _mm256_loadu_si256((__m256i *)(kernel + 2 * reduced_width + i));
      __m256i kernel_vec3 =
          _mm256_loadu_si256((__m256i *)(kernel + 3 * reduced_width + i));
      __m256i kernel_vec4 =
          _mm256_loadu_si256((__m256i *)(kernel + 4 * reduced_width + i));
      __m256i kernel_vec5 =
          _mm256_loadu_si256((__m256i *)(kernel + 5 * reduced_width + i));
      __m256i kernel_vec6 =
          _mm256_loadu_si256((__m256i *)(kernel + 6 * reduced_width + i));
      __m256i kernel_vec7 =
          _mm256_loadu_si256((__m256i *)(kernel + 7 * reduced_width + i));

      __m256i src_vec = _mm256_loadu_si256((__m256i *)(src_ptr + i));

      kernel_vec0 = _mm256_mullo_epi32(kernel_vec0, src_vec);
      kernel_vec1 = _mm256_mullo_epi32(kernel_vec1, src_vec);
      kernel_vec2 = _mm256_mullo_epi32(kernel_vec2, src_vec);
      kernel_vec3 = _mm256_mullo_epi32(kernel_vec3, src_vec);
      kernel_vec4 = _mm256_mullo_epi32(kernel_vec4, src_vec);
      kernel_vec5 = _mm256_mullo_epi32(kernel_vec5, src_vec);
      kernel_vec6 = _mm256_mullo_epi32(kernel_vec6, src_vec);
      kernel_vec7 = _mm256_mullo_epi32(kernel_vec7, src_vec);

      resi_vec[0] = _mm256_add_epi32(resi_vec[0], kernel_vec0);
      resi_vec[1] = _mm256_add_epi32(resi_vec[1], kernel_vec1);
      resi_vec[2] = _mm256_add_epi32(resi_vec[2], kernel_vec2);
      resi_vec[3] = _mm256_add_epi32(resi_vec[3], kernel_vec3);
      resi_vec[4] = _mm256_add_epi32(resi_vec[4], kernel_vec4);
      resi_vec[5] = _mm256_add_epi32(resi_vec[5], kernel_vec5);
      resi_vec[6] = _mm256_add_epi32(resi_vec[6], kernel_vec6);
      resi_vec[7] = _mm256_add_epi32(resi_vec[7], kernel_vec7);
    }
    fwd_stxfm_transpose_8x8_avx2(resi_vec, resi_vec_out);
    __m256i sum_vec = _mm256_setzero_si256();
    for (int i = 0; i < 8; i++) {
      sum_vec = _mm256_add_epi32(sum_vec, resi_vec_out[i]);
    }

    sum_vec = round_power_of_two_signed_avx2(sum_vec, shift);
    sum_vec = _mm256_min_epi32(_mm256_max_epi32(sum_vec, min_value), max_value);
    _mm256_storeu_si256((__m256i *)out, sum_vec);
  } else if (reduced_height % 8 != 0) {
    assert(reduced_height % 4 == 0 && reduced_width % 8 == 0);
    __m256i resi_vec[4];
    __m128i resi_vec_out[8];
    const int *src_ptr = src;
    const __m128i max_value = _mm_set1_epi32((1 << (7 + bd)) - 1);
    const __m128i min_value = _mm_set1_epi32(-(1 << (7 + bd)));
    int *out = dst;
    const __m256i zeros = _mm256_setzero_si256();
    for (int j = 0; j < reduced_height; j += 4) {
      resi_vec[0] = zeros;
      resi_vec[1] = zeros;
      resi_vec[2] = zeros;
      resi_vec[3] = zeros;
      for (int i = 0; i < reduced_width; i += 8) {
        __m256i kernel_vec0 = _mm256_loadu_si256((__m256i *)(kernel + i));
        __m256i kernel_vec1 =
            _mm256_loadu_si256((__m256i *)(kernel + reduced_width + i));
        __m256i kernel_vec2 =
            _mm256_loadu_si256((__m256i *)(kernel + 2 * reduced_width + i));
        __m256i kernel_vec3 =
            _mm256_loadu_si256((__m256i *)(kernel + 3 * reduced_width + i));

        __m256i src_vec = _mm256_loadu_si256((__m256i *)(src_ptr + i));

        kernel_vec0 = _mm256_mullo_epi32(kernel_vec0, src_vec);
        kernel_vec1 = _mm256_mullo_epi32(kernel_vec1, src_vec);
        kernel_vec2 = _mm256_mullo_epi32(kernel_vec2, src_vec);
        kernel_vec3 = _mm256_mullo_epi32(kernel_vec3, src_vec);

        resi_vec[0] = _mm256_add_epi32(resi_vec[0], kernel_vec0);
        resi_vec[1] = _mm256_add_epi32(resi_vec[1], kernel_vec1);
        resi_vec[2] = _mm256_add_epi32(resi_vec[2], kernel_vec2);
        resi_vec[3] = _mm256_add_epi32(resi_vec[3], kernel_vec3);
      }
      fwd_stxfm_transpose_4x8_avx2(resi_vec, resi_vec_out);
      __m128i sum_vec = _mm_setzero_si128();
      for (int i = 0; i < 8; i++) {
        sum_vec = _mm_add_epi32(sum_vec, resi_vec_out[i]);
      }

      sum_vec = round_power_of_two_signed_sse2(sum_vec, shift);
      sum_vec = _mm_min_epi32(_mm_max_epi32(sum_vec, min_value), max_value);
      _mm_storeu_si128((__m128i *)out, sum_vec);
      kernel += reduced_width << 2;
      out += 4;
    }
  } else {
    assert(reduced_height % 8 == 0 && reduced_width % 8 == 0);
    __m256i resi_vec[8];
    __m256i resi_vec_out[8];
    const __m256i max_value = _mm256_set1_epi32((1 << (7 + bd)) - 1);
    const __m256i min_value = _mm256_set1_epi32(-(1 << (7 + bd)));
    int *out = dst;
    int *src_ptr = src;
    int stride_width = reduced_width;
    const __m256i zeros = _mm256_setzero_si256();
    for (int j = 0; j < reduced_height; j += 8) {
      resi_vec[0] = zeros;
      resi_vec[1] = zeros;
      resi_vec[2] = zeros;
      resi_vec[3] = zeros;
      resi_vec[4] = zeros;
      resi_vec[5] = zeros;
      resi_vec[6] = zeros;
      resi_vec[7] = zeros;
      for (int i = 0; i < reduced_width; i += 8) {
        __m256i kernel_vec0 = _mm256_loadu_si256((__m256i *)(kernel + i));
        __m256i kernel_vec1 =
            _mm256_loadu_si256((__m256i *)(kernel + stride_width + i));
        __m256i kernel_vec2 =
            _mm256_loadu_si256((__m256i *)(kernel + 2 * stride_width + i));
        __m256i kernel_vec3 =
            _mm256_loadu_si256((__m256i *)(kernel + 3 * stride_width + i));
        __m256i kernel_vec4 =
            _mm256_loadu_si256((__m256i *)(kernel + 4 * stride_width + i));
        __m256i kernel_vec5 =
            _mm256_loadu_si256((__m256i *)(kernel + 5 * stride_width + i));
        __m256i kernel_vec6 =
            _mm256_loadu_si256((__m256i *)(kernel + 6 * stride_width + i));
        __m256i kernel_vec7 =
            _mm256_loadu_si256((__m256i *)(kernel + 7 * stride_width + i));

        __m256i src_vec = _mm256_loadu_si256((__m256i *)(src_ptr + i));

        kernel_vec0 = _mm256_mullo_epi32(kernel_vec0, src_vec);
        kernel_vec1 = _mm256_mullo_epi32(kernel_vec1, src_vec);
        kernel_vec2 = _mm256_mullo_epi32(kernel_vec2, src_vec);
        kernel_vec3 = _mm256_mullo_epi32(kernel_vec3, src_vec);
        kernel_vec4 = _mm256_mullo_epi32(kernel_vec4, src_vec);
        kernel_vec5 = _mm256_mullo_epi32(kernel_vec5, src_vec);
        kernel_vec6 = _mm256_mullo_epi32(kernel_vec6, src_vec);
        kernel_vec7 = _mm256_mullo_epi32(kernel_vec7, src_vec);

        resi_vec[0] = _mm256_add_epi32(resi_vec[0], kernel_vec0);
        resi_vec[1] = _mm256_add_epi32(resi_vec[1], kernel_vec1);
        resi_vec[2] = _mm256_add_epi32(resi_vec[2], kernel_vec2);
        resi_vec[3] = _mm256_add_epi32(resi_vec[3], kernel_vec3);
        resi_vec[4] = _mm256_add_epi32(resi_vec[4], kernel_vec4);
        resi_vec[5] = _mm256_add_epi32(resi_vec[5], kernel_vec5);
        resi_vec[6] = _mm256_add_epi32(resi_vec[6], kernel_vec6);
        resi_vec[7] = _mm256_add_epi32(resi_vec[7], kernel_vec7);
      }
      fwd_stxfm_transpose_8x8_avx2(resi_vec, resi_vec_out);
      __m256i sum_vec = zeros;
      for (int i = 0; i < 8; i++) {
        sum_vec = _mm256_add_epi32(sum_vec, resi_vec_out[i]);
      }
      sum_vec = round_power_of_two_signed_avx2(sum_vec, shift);
      sum_vec =
          _mm256_min_epi32(_mm256_max_epi32(sum_vec, min_value), max_value);
      _mm256_storeu_si256((__m256i *)out, sum_vec);
      kernel += stride_width << 3;
      out += 8;
    }
  }
}

void transpose_store_8x8_avx2(__m256i *a, int *dst, int size) {
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
  _mm256_storeu_si256(
      (__m256i *)(dst + 0 * size),
      _mm256_permute2x128_si256(u0, u4, 0x20));  // { A0 B0 C0 D0 E0 F0 G0 H0 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 1 * size),
      _mm256_permute2x128_si256(u1, u5, 0x20));  // { A1 B1 C1 D1 E1 F1 G1 H1 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 2 * size),
      _mm256_permute2x128_si256(u2, u6, 0x20));  // { A2 B2 C2 D2 E2 F2 G2 H2 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 3 * size),
      _mm256_permute2x128_si256(u3, u7, 0x20));  // { A3 B3 C3 D3 E3 F3 G3 H3 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 4 * size),
      _mm256_permute2x128_si256(u0, u4, 0x31));  // { A4 B4 C4 D4 E4 F4 G4 H4 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 5 * size),
      _mm256_permute2x128_si256(u1, u5, 0x31));  // { A5 B5 C5 D5 E5 F5 G5 H5 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 6 * size),
      _mm256_permute2x128_si256(u2, u6, 0x31));  // { A6 B6 C6 D6 E6 F6 G6 H6 }
  _mm256_storeu_si256(
      (__m256i *)(dst + 7 * size),
      _mm256_permute2x128_si256(u3, u7, 0x31));  // { A7 B7 C7 D7 E7 F7 G7 H7 }
}

void transpose_store_8x4_sse4(__m128i *a, int *dst, int size) {
  // Step 1: Interleave 32-bit elements
  __m128i t0 = _mm_unpacklo_epi32(a[0], a[1]);  // { A0 B0 A1 B1 }
  __m128i t1 = _mm_unpackhi_epi32(a[0], a[1]);  // { A2 B2 A3 B3 }
  __m128i t2 = _mm_unpacklo_epi32(a[2], a[3]);  // { C0 D0 C1 D1 }
  __m128i t3 = _mm_unpackhi_epi32(a[2], a[3]);  // { C2 D2 C3 D3 }
  __m128i t4 = _mm_unpacklo_epi32(a[4], a[5]);  // { E0 F0 E1 F1 }
  __m128i t5 = _mm_unpackhi_epi32(a[4], a[5]);  // { E2 F2 E3 F3 }
  __m128i t6 = _mm_unpacklo_epi32(a[6], a[7]);  // { G0 H0 G1 H1 }
  __m128i t7 = _mm_unpackhi_epi32(a[6], a[7]);  // { G2 H2 G3 H3 }

  // Step 2: Interleave 64-bit elements
  __m128i u0 = _mm_unpacklo_epi64(t0, t2);  // { A0 B0 C0 D0 }
  __m128i u1 = _mm_unpackhi_epi64(t0, t2);  // { A1 B1 C1 D1 }
  __m128i u2 = _mm_unpacklo_epi64(t1, t3);  // { A2 B2 C2 D2 }
  __m128i u3 = _mm_unpackhi_epi64(t1, t3);  // { A3 B3 C3 D3 }
  __m128i u4 = _mm_unpacklo_epi64(t4, t6);  // { E0 F0 G0 H0 }
  __m128i u5 = _mm_unpackhi_epi64(t4, t6);  // { E1 F1 G1 H1 }
  __m128i u6 = _mm_unpacklo_epi64(t5, t7);  // { E2 F2 G2 H2 }
  __m128i u7 = _mm_unpackhi_epi64(t5, t7);  // { E3 F3 G3 H3 }

  // Step 3: Store results into int dst[4][8]
  _mm_storeu_si128((__m128i *)(dst + 0 * size), u0);      // { A0 B0 C0 D0 }
  _mm_storeu_si128((__m128i *)(dst + 1 * size), u1);      // { A1 B1 C1 D1 }
  _mm_storeu_si128((__m128i *)(dst + 2 * size), u2);      // { A2 B2 C2 D2 }
  _mm_storeu_si128((__m128i *)(dst + 3 * size), u3);      // { A3 B3 C3 D3 }
  _mm_storeu_si128((__m128i *)(dst + 0 * size + 4), u4);  // { E0 F0 G0 H0 }
  _mm_storeu_si128((__m128i *)(dst + 1 * size + 4), u5);  // { E1 F1 G1 H1 }
  _mm_storeu_si128((__m128i *)(dst + 2 * size + 4), u6);  // { E2 F2 G2 H2 }
  _mm_storeu_si128((__m128i *)(dst + 3 * size + 4), u7);  // { E3 F3 G3 H3 }
}

void transpose_store_8x4_avx2(__m256i *a, int *dst, int size) {
  __m256i t0 =
      _mm256_unpacklo_epi32(a[0], a[1]);  // { A0 B0 A1 B1 A4 B4 A5 B5 }
  __m256i t1 =
      _mm256_unpackhi_epi32(a[0], a[1]);  // { A2 B2 A3 B3 A6 B6 A7 B7 }
  __m256i t2 =
      _mm256_unpacklo_epi32(a[2], a[3]);  // { C0 D0 C1 D1 C4 D4 C5 D5 }
  __m256i t3 =
      _mm256_unpackhi_epi32(a[2], a[3]);  // { C2 D2 C3 D3 C6 D6 C7 D7 }

  // Interleave 64-bit elements
  __m256i u0 = _mm256_unpacklo_epi64(t0, t2);  // { A0 B0 C0 D0 A4 B4 C4 D4 }
  __m256i u1 = _mm256_unpackhi_epi64(t0, t2);  // { A1 B1 C1 D1 A5 B5 C5 D5 }
  __m256i u2 = _mm256_unpacklo_epi64(t1, t3);  // { A2 B2 C2 D2 A6 B6 C6 D6 }
  __m256i u3 = _mm256_unpackhi_epi64(t1, t3);  // { A3 B3 C3 D3 A7 B7 C7 D7 }

  // Permute 128-bit lanes to complete the transpose
  _mm_storeu_si128((__m128i *)(dst + 0 * size),
                   _mm256_castsi256_si128(u0));  // { A0 B0 C0 D0 }
  _mm_storeu_si128((__m128i *)(dst + 1 * size),
                   _mm256_castsi256_si128(u1));  // { A1 B1 C1 D1 }
  _mm_storeu_si128((__m128i *)(dst + 2 * size),
                   _mm256_castsi256_si128(u2));  // { A2 B2 C2 D2 }
  _mm_storeu_si128((__m128i *)(dst + 3 * size),
                   _mm256_castsi256_si128(u3));  // { A3 B3 C3 D3 }
  _mm_storeu_si128((__m128i *)(dst + 4 * size),
                   _mm256_extracti128_si256(u0, 1));  // { A4 B4 C4 D4 }
  _mm_storeu_si128((__m128i *)(dst + 5 * size),
                   _mm256_extracti128_si256(u1, 1));  // { A5 B5 C5 D5 }
  _mm_storeu_si128((__m128i *)(dst + 6 * size),
                   _mm256_extracti128_si256(u2, 1));  // { A6 B6 C6 D6 }
  _mm_storeu_si128((__m128i *)(dst + 7 * size),
                   _mm256_extracti128_si256(u3, 1));  // { A7 B7 C7 D7 }
}

// ********************************** DCT-II **********************************
void fwd_txfm_dct2_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;

  const int *tx_mat = tx_kernel_dct2_size4[FWD_TXFM][0];

  const int tx1d_size = 4;

  if (nz_line >= 8) {
    __m256i a[2], b[2], aa[4];

    __m256i v_offset = _mm256_set1_epi32(offset);

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 4 + 0]);  // 85
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 4 + 1]);  // 35

    for (j = 0; j < nz_line; j += 8) {
      for (k = 0; k < 2; k++) {
        __m256i src0 = _mm256_loadu_si256((__m256i *)&src[k * line]);
        __m256i src1 =
            _mm256_loadu_si256((__m256i *)&src[(tx1d_size - 1 - k) * line]);
        a[k] = _mm256_add_epi32(src0, src1);
        b[k] = _mm256_sub_epi32(src0, src1);
      }

      aa[0] = _mm256_add_epi32(a[0], a[1]);
      aa[0] = _mm256_slli_epi32(aa[0], 6);

      aa[2] = _mm256_sub_epi32(a[0], a[1]);
      aa[2] = _mm256_slli_epi32(aa[2], 6);

      aa[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_0),
                               _mm256_mullo_epi32(b[1], txmat_1_1));

      aa[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_1),
                               _mm256_mullo_epi32(b[1], txmat_1_0));

      for (k = 0; k < 4; k++) {
        aa[k] = _mm256_add_epi32(aa[k], v_offset);
        aa[k] = _mm256_srai_epi32(aa[k], shift);
      }

      transpose_store_8x4_avx2(aa, dst, tx1d_size);

      src += 8;
      dst += tx1d_size * 8;
    }
  } else {
    __m256i v_offset = _mm256_set1_epi32(offset);
    for (j = 0; j < nz_line; j += 2) {
      __m256i sum = _mm256_set1_epi32(0);
      for (k = 0; k < tx1d_size; k++) {
        __m128i tmp_src0 = _mm_set1_epi32(src[k * line + j + 0]);
        __m128i tmp_src1 = _mm_set1_epi32(src[k * line + j + 1]);
        __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

        __m256i tmp_val = _mm256_set_epi32(
            tx_mat[3 * tx1d_size + k], tx_mat[2 * tx1d_size + k],
            tx_mat[1 * tx1d_size + k], tx_mat[0 * tx1d_size + k],
            tx_mat[3 * tx1d_size + k], tx_mat[2 * tx1d_size + k],
            tx_mat[1 * tx1d_size + k], tx_mat[0 * tx1d_size + k]);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Store results
      _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
    }
  }
}

void fwd_txfm_dct2_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;

  const int *tx_mat = tx_kernel_dct2_size8[FWD_TXFM][0];

  const int tx1d_size = 8;

  if (nz_line >= 8) {
    __m256i a[8], b[4], aa[4];
    __m256i c[2], d[2];

    __m256i v_offset = _mm256_set1_epi32(offset);

    __m256i txmat_2_0 = _mm256_set1_epi32(tx_mat[2 * 8 + 0]);  // 85
    __m256i txmat_2_1 = _mm256_set1_epi32(tx_mat[2 * 8 + 1]);  // 35

    __m256i txmat_1_0 = _mm256_set1_epi32(tx_mat[1 * 8 + 0]);  // 89
    __m256i txmat_1_1 = _mm256_set1_epi32(tx_mat[1 * 8 + 1]);  // 75
    __m256i txmat_1_2 = _mm256_set1_epi32(tx_mat[1 * 8 + 2]);  // 50
    __m256i txmat_1_3 = _mm256_set1_epi32(tx_mat[1 * 8 + 3]);  // 18

    for (j = 0; j < nz_line; j += 8) {
      for (k = 0; k < 4; k++) {
        __m256i src0 = _mm256_loadu_si256((__m256i *)&src[k * line]);
        __m256i src1 =
            _mm256_loadu_si256((__m256i *)&src[(tx1d_size - 1 - k) * line]);
        a[k] = _mm256_add_epi32(src0, src1);
        b[k] = _mm256_sub_epi32(src0, src1);
      }
      for (k = 0; k < 2; k++) {
        c[k] = _mm256_add_epi32(a[k], a[3 - k]);
        d[k] = _mm256_sub_epi32(a[k], a[3 - k]);
      }

      a[0] = _mm256_add_epi32(c[0], c[1]);
      a[0] = _mm256_slli_epi32(a[0], 6);

      a[4] = _mm256_sub_epi32(c[0], c[1]);
      a[4] = _mm256_slli_epi32(a[4], 6);

      a[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_0),
                              _mm256_mullo_epi32(d[1], txmat_2_1));

      a[6] = _mm256_sub_epi32(_mm256_mullo_epi32(d[0], txmat_2_1),
                              _mm256_mullo_epi32(d[1], txmat_2_0));

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_0);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_1);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_2);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_3);
      a[1] = _mm256_add_epi32(_mm256_add_epi32(aa[0], aa[1]),
                              _mm256_add_epi32(aa[2], aa[3]));

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_1);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_3);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_0);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_2);
      a[3] = _mm256_sub_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                              _mm256_add_epi32(aa[2], aa[3]));

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_2);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_0);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_3);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_1);
      a[5] = _mm256_add_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                              _mm256_add_epi32(aa[2], aa[3]));

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_3);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_2);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_1);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_0);
      a[7] = _mm256_add_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                              _mm256_sub_epi32(aa[2], aa[3]));

      for (k = 0; k < 8; k++) {
        a[k] = _mm256_add_epi32(a[k], v_offset);
        a[k] = _mm256_srai_epi32(a[k], shift);
      }

      transpose_store_8x8_avx2(a, dst, tx1d_size);

      src += 8;
      dst += tx1d_size * 8;
    }
  } else {
    __m128i a[8], b[4], aa[4];
    __m128i c[2], d[2];

    __m128i v_offset = _mm_set1_epi32(offset);

    __m128i txmat_2_0 = _mm_set1_epi32(tx_mat[2 * 8 + 0]);  // 85
    __m128i txmat_2_1 = _mm_set1_epi32(tx_mat[2 * 8 + 1]);  // 35

    __m128i txmat_1_0 = _mm_set1_epi32(tx_mat[1 * 8 + 0]);  // 89
    __m128i txmat_1_1 = _mm_set1_epi32(tx_mat[1 * 8 + 1]);  // 75
    __m128i txmat_1_2 = _mm_set1_epi32(tx_mat[1 * 8 + 2]);  // 50
    __m128i txmat_1_3 = _mm_set1_epi32(tx_mat[1 * 8 + 3]);  // 18

    for (k = 0; k < 4; k++) {
      __m128i src0 = _mm_loadu_si128((__m128i *)&src[k * line]);
      __m128i src1 =
          _mm_loadu_si128((__m128i *)&src[(tx1d_size - 1 - k) * line]);
      a[k] = _mm_add_epi32(src0, src1);
      b[k] = _mm_sub_epi32(src0, src1);
    }
    for (k = 0; k < 2; k++) {
      c[k] = _mm_add_epi32(a[k], a[3 - k]);
      d[k] = _mm_sub_epi32(a[k], a[3 - k]);
    }

    a[0] = _mm_add_epi32(c[0], c[1]);
    a[0] = _mm_slli_epi32(a[0], 6);

    a[4] = _mm_sub_epi32(c[0], c[1]);
    a[4] = _mm_slli_epi32(a[4], 6);

    a[2] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_0),
                         _mm_mullo_epi32(d[1], txmat_2_1));

    a[6] = _mm_sub_epi32(_mm_mullo_epi32(d[0], txmat_2_1),
                         _mm_mullo_epi32(d[1], txmat_2_0));

    aa[0] = _mm_mullo_epi32(b[0], txmat_1_0);
    aa[1] = _mm_mullo_epi32(b[1], txmat_1_1);
    aa[2] = _mm_mullo_epi32(b[2], txmat_1_2);
    aa[3] = _mm_mullo_epi32(b[3], txmat_1_3);
    a[1] =
        _mm_add_epi32(_mm_add_epi32(aa[0], aa[1]), _mm_add_epi32(aa[2], aa[3]));

    aa[0] = _mm_mullo_epi32(b[0], txmat_1_1);
    aa[1] = _mm_mullo_epi32(b[1], txmat_1_3);
    aa[2] = _mm_mullo_epi32(b[2], txmat_1_0);
    aa[3] = _mm_mullo_epi32(b[3], txmat_1_2);
    a[3] =
        _mm_sub_epi32(_mm_sub_epi32(aa[0], aa[1]), _mm_add_epi32(aa[2], aa[3]));

    aa[0] = _mm_mullo_epi32(b[0], txmat_1_2);
    aa[1] = _mm_mullo_epi32(b[1], txmat_1_0);
    aa[2] = _mm_mullo_epi32(b[2], txmat_1_3);
    aa[3] = _mm_mullo_epi32(b[3], txmat_1_1);
    a[5] =
        _mm_add_epi32(_mm_sub_epi32(aa[0], aa[1]), _mm_add_epi32(aa[2], aa[3]));

    aa[0] = _mm_mullo_epi32(b[0], txmat_1_3);
    aa[1] = _mm_mullo_epi32(b[1], txmat_1_2);
    aa[2] = _mm_mullo_epi32(b[2], txmat_1_1);
    aa[3] = _mm_mullo_epi32(b[3], txmat_1_0);
    a[7] =
        _mm_add_epi32(_mm_sub_epi32(aa[0], aa[1]), _mm_sub_epi32(aa[2], aa[3]));

    for (k = 0; k < 8; k++) {
      a[k] = _mm_add_epi32(a[k], v_offset);
      a[k] = _mm_srai_epi32(a[k], shift);
    }

    transpose_store_8x4_sse4(a, dst, tx1d_size);
  }
}

void fwd_txfm_dct2_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;

  const int *tx_mat = tx_kernel_dct2_size16[FWD_TXFM][0];

  const int tx1d_size = 16;

  if (nz_line >= 8) {
    __m256i a[16], b[8], aa[8];
    __m256i c[4], d[4];
    __m256i e[2], f[2];

    __m256i v_offset = _mm256_set1_epi32(offset);

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
      for (k = 0; k < 8; k++) {
        __m256i src0 = _mm256_loadu_si256((__m256i *)&src[k * line]);
        __m256i src1 =
            _mm256_loadu_si256((__m256i *)&src[(tx1d_size - 1 - k) * line]);
        a[k] = _mm256_add_epi32(src0, src1);
        b[k] = _mm256_sub_epi32(src0, src1);
      }
      for (k = 0; k < 4; k++) {
        c[k] = _mm256_add_epi32(a[k], a[7 - k]);
        d[k] = _mm256_sub_epi32(a[k], a[7 - k]);
      }
      for (k = 0; k < 2; k++) {
        e[k] = _mm256_add_epi32(c[k], c[3 - k]);
        f[k] = _mm256_sub_epi32(c[k], c[3 - k]);
      }

      a[0] = _mm256_add_epi32(e[0], e[1]);
      a[0] = _mm256_slli_epi32(a[0], 6);

      a[8] = _mm256_sub_epi32(e[0], e[1]);
      a[8] = _mm256_slli_epi32(a[8], 6);

      a[4] = _mm256_add_epi32(_mm256_mullo_epi32(f[0], txmat_4_0),
                              _mm256_mullo_epi32(f[1], txmat_4_1));

      a[12] = _mm256_sub_epi32(_mm256_mullo_epi32(f[0], txmat_4_1),
                               _mm256_mullo_epi32(f[1], txmat_4_0));

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_0);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_1);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_2);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_3);
      a[2] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_1);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_3);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_0);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_2);
      a[6] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_2);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_0);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_3);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_1);
      a[10] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                               _mm256_add_epi32(c[2], c[3]));

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_3);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_2);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_1);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_0);
      a[14] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                               _mm256_sub_epi32(c[2], c[3]));

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_0);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_1);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_2);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_3);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_4);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_5);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_6);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_7);
      aa[0] = _mm256_add_epi32(_mm256_add_epi32(aa[0], aa[1]),
                               _mm256_add_epi32(aa[2], aa[3]));
      aa[4] = _mm256_add_epi32(_mm256_add_epi32(aa[4], aa[5]),
                               _mm256_add_epi32(aa[6], aa[7]));
      a[1] = _mm256_add_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_1);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_4);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_7);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_5);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_2);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_0);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_3);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_6);
      aa[0] = _mm256_add_epi32(_mm256_add_epi32(aa[0], aa[1]),
                               _mm256_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm256_add_epi32(_mm256_add_epi32(aa[4], aa[5]),
                               _mm256_add_epi32(aa[6], aa[7]));
      a[3] = _mm256_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_2);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_7);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_3);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_1);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_6);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_4);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_0);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_5);
      aa[0] = _mm256_sub_epi32(_mm256_add_epi32(aa[0], aa[1]),
                               _mm256_add_epi32(aa[2], aa[3]));
      aa[4] = _mm256_sub_epi32(_mm256_sub_epi32(aa[4], aa[5]),
                               _mm256_add_epi32(aa[6], aa[7]));
      a[5] = _mm256_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_3);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_5);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_1);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_7);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_0);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_6);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_2);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_4);
      aa[0] = _mm256_sub_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                               _mm256_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm256_sub_epi32(_mm256_add_epi32(aa[4], aa[5]),
                               _mm256_add_epi32(aa[6], aa[7]));
      a[7] = _mm256_add_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_4);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_2);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_6);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_0);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_7);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_1);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_5);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_3);
      aa[0] = _mm256_sub_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                               _mm256_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm256_sub_epi32(_mm256_add_epi32(aa[4], aa[5]),
                               _mm256_add_epi32(aa[6], aa[7]));
      a[9] = _mm256_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_5);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_0);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_4);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_6);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_1);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_3);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_7);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_2);
      aa[0] = _mm256_add_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                               _mm256_add_epi32(aa[2], aa[3]));
      aa[4] = _mm256_sub_epi32(_mm256_sub_epi32(aa[4], aa[5]),
                               _mm256_sub_epi32(aa[6], aa[7]));
      a[11] = _mm256_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_6);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_3);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_0);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_2);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_5);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_7);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_4);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_1);
      aa[0] = _mm256_add_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                               _mm256_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm256_sub_epi32(_mm256_add_epi32(aa[4], aa[5]),
                               _mm256_sub_epi32(aa[6], aa[7]));
      a[13] = _mm256_add_epi32(aa[0], aa[4]);

      aa[0] = _mm256_mullo_epi32(b[0], txmat_1_7);
      aa[1] = _mm256_mullo_epi32(b[1], txmat_1_6);
      aa[2] = _mm256_mullo_epi32(b[2], txmat_1_5);
      aa[3] = _mm256_mullo_epi32(b[3], txmat_1_4);
      aa[4] = _mm256_mullo_epi32(b[4], txmat_1_3);
      aa[5] = _mm256_mullo_epi32(b[5], txmat_1_2);
      aa[6] = _mm256_mullo_epi32(b[6], txmat_1_1);
      aa[7] = _mm256_mullo_epi32(b[7], txmat_1_0);
      aa[0] = _mm256_add_epi32(_mm256_sub_epi32(aa[0], aa[1]),
                               _mm256_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm256_add_epi32(_mm256_sub_epi32(aa[4], aa[5]),
                               _mm256_sub_epi32(aa[6], aa[7]));
      a[15] = _mm256_add_epi32(aa[0], aa[4]);

      for (k = 0; k < 16; k++) {
        a[k] = _mm256_add_epi32(a[k], v_offset);
        a[k] = _mm256_srai_epi32(a[k], shift);
      }

      transpose_store_8x8_avx2(a, dst + 0, tx1d_size);
      transpose_store_8x8_avx2(a + 8, dst + 8, tx1d_size);

      src += 8;
      dst += tx1d_size * 8;
    }
  } else {
    __m128i a[16], b[8], aa[8];
    __m128i c[4], d[4];
    __m128i e[2], f[2];

    __m128i v_offset = _mm_set1_epi32(offset);

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

    for (j = 0; j < nz_line; j += 8) {
      for (k = 0; k < 8; k++) {
        __m128i src0 = _mm_loadu_si128((__m128i *)&src[k * line]);
        __m128i src1 =
            _mm_loadu_si128((__m128i *)&src[(tx1d_size - 1 - k) * line]);
        a[k] = _mm_add_epi32(src0, src1);
        b[k] = _mm_sub_epi32(src0, src1);
      }
      for (k = 0; k < 4; k++) {
        c[k] = _mm_add_epi32(a[k], a[7 - k]);
        d[k] = _mm_sub_epi32(a[k], a[7 - k]);
      }
      for (k = 0; k < 2; k++) {
        e[k] = _mm_add_epi32(c[k], c[3 - k]);
        f[k] = _mm_sub_epi32(c[k], c[3 - k]);
      }

      a[0] = _mm_add_epi32(e[0], e[1]);
      a[0] = _mm_slli_epi32(a[0], 6);

      a[8] = _mm_sub_epi32(e[0], e[1]);
      a[8] = _mm_slli_epi32(a[8], 6);

      a[4] = _mm_add_epi32(_mm_mullo_epi32(f[0], txmat_4_0),
                           _mm_mullo_epi32(f[1], txmat_4_1));

      a[12] = _mm_sub_epi32(_mm_mullo_epi32(f[0], txmat_4_1),
                            _mm_mullo_epi32(f[1], txmat_4_0));

      c[0] = _mm_mullo_epi32(d[0], txmat_2_0);
      c[1] = _mm_mullo_epi32(d[1], txmat_2_1);
      c[2] = _mm_mullo_epi32(d[2], txmat_2_2);
      c[3] = _mm_mullo_epi32(d[3], txmat_2_3);
      a[2] =
          _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

      c[0] = _mm_mullo_epi32(d[0], txmat_2_1);
      c[1] = _mm_mullo_epi32(d[1], txmat_2_3);
      c[2] = _mm_mullo_epi32(d[2], txmat_2_0);
      c[3] = _mm_mullo_epi32(d[3], txmat_2_2);
      a[6] =
          _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

      c[0] = _mm_mullo_epi32(d[0], txmat_2_2);
      c[1] = _mm_mullo_epi32(d[1], txmat_2_0);
      c[2] = _mm_mullo_epi32(d[2], txmat_2_3);
      c[3] = _mm_mullo_epi32(d[3], txmat_2_1);
      a[10] =
          _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));

      c[0] = _mm_mullo_epi32(d[0], txmat_2_3);
      c[1] = _mm_mullo_epi32(d[1], txmat_2_2);
      c[2] = _mm_mullo_epi32(d[2], txmat_2_1);
      c[3] = _mm_mullo_epi32(d[3], txmat_2_0);
      a[14] =
          _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_0);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_1);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_2);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_3);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_4);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_5);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_6);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_7);
      aa[0] = _mm_add_epi32(_mm_add_epi32(aa[0], aa[1]),
                            _mm_add_epi32(aa[2], aa[3]));
      aa[4] = _mm_add_epi32(_mm_add_epi32(aa[4], aa[5]),
                            _mm_add_epi32(aa[6], aa[7]));
      a[1] = _mm_add_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_1);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_4);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_7);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_5);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_2);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_0);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_3);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_6);
      aa[0] = _mm_add_epi32(_mm_add_epi32(aa[0], aa[1]),
                            _mm_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm_add_epi32(_mm_add_epi32(aa[4], aa[5]),
                            _mm_add_epi32(aa[6], aa[7]));
      a[3] = _mm_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_2);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_7);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_3);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_1);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_6);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_4);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_0);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_5);
      aa[0] = _mm_sub_epi32(_mm_add_epi32(aa[0], aa[1]),
                            _mm_add_epi32(aa[2], aa[3]));
      aa[4] = _mm_sub_epi32(_mm_sub_epi32(aa[4], aa[5]),
                            _mm_add_epi32(aa[6], aa[7]));
      a[5] = _mm_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_3);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_5);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_1);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_7);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_0);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_6);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_2);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_4);
      aa[0] = _mm_sub_epi32(_mm_sub_epi32(aa[0], aa[1]),
                            _mm_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm_sub_epi32(_mm_add_epi32(aa[4], aa[5]),
                            _mm_add_epi32(aa[6], aa[7]));
      a[7] = _mm_add_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_4);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_2);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_6);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_0);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_7);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_1);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_5);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_3);
      aa[0] = _mm_sub_epi32(_mm_sub_epi32(aa[0], aa[1]),
                            _mm_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm_sub_epi32(_mm_add_epi32(aa[4], aa[5]),
                            _mm_add_epi32(aa[6], aa[7]));
      a[9] = _mm_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_5);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_0);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_4);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_6);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_1);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_3);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_7);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_2);
      aa[0] = _mm_add_epi32(_mm_sub_epi32(aa[0], aa[1]),
                            _mm_add_epi32(aa[2], aa[3]));
      aa[4] = _mm_sub_epi32(_mm_sub_epi32(aa[4], aa[5]),
                            _mm_sub_epi32(aa[6], aa[7]));
      a[11] = _mm_sub_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_6);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_3);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_0);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_2);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_5);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_7);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_4);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_1);
      aa[0] = _mm_add_epi32(_mm_sub_epi32(aa[0], aa[1]),
                            _mm_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm_sub_epi32(_mm_add_epi32(aa[4], aa[5]),
                            _mm_sub_epi32(aa[6], aa[7]));
      a[13] = _mm_add_epi32(aa[0], aa[4]);

      aa[0] = _mm_mullo_epi32(b[0], txmat_1_7);
      aa[1] = _mm_mullo_epi32(b[1], txmat_1_6);
      aa[2] = _mm_mullo_epi32(b[2], txmat_1_5);
      aa[3] = _mm_mullo_epi32(b[3], txmat_1_4);
      aa[4] = _mm_mullo_epi32(b[4], txmat_1_3);
      aa[5] = _mm_mullo_epi32(b[5], txmat_1_2);
      aa[6] = _mm_mullo_epi32(b[6], txmat_1_1);
      aa[7] = _mm_mullo_epi32(b[7], txmat_1_0);
      aa[0] = _mm_add_epi32(_mm_sub_epi32(aa[0], aa[1]),
                            _mm_sub_epi32(aa[2], aa[3]));
      aa[4] = _mm_add_epi32(_mm_sub_epi32(aa[4], aa[5]),
                            _mm_sub_epi32(aa[6], aa[7]));
      a[15] = _mm_add_epi32(aa[0], aa[4]);

      for (k = 0; k < 16; k++) {
        a[k] = _mm_add_epi32(a[k], v_offset);
        a[k] = _mm_srai_epi32(a[k], shift);
      }

      transpose_store_8x4_sse4(a, dst + 0, tx1d_size);
      transpose_store_8x4_sse4(a + 8, dst + 8, tx1d_size);
    }
  }
}

void fwd_txfm_dct2_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;

  const int *tx_mat = tx_kernel_dct2_size32[FWD_TXFM][0];

  const int tx1d_size = 32;

  if (nz_line >= 8) {
    __m256i a[32], b[16];
    __m256i c[8], d[8];
    __m256i e[4], f[4];
    __m256i g[2], h[2];

    __m256i v_offset = _mm256_set1_epi32(offset);

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
      for (k = 0; k < 16; k++) {
        __m256i src0 = _mm256_loadu_si256((__m256i *)&src[k * line]);
        __m256i src1 =
            _mm256_loadu_si256((__m256i *)&src[(tx1d_size - 1 - k) * line]);
        a[k] = _mm256_add_epi32(src0, src1);
        b[k] = _mm256_sub_epi32(src0, src1);
      }
      for (k = 0; k < 8; k++) {
        c[k] = _mm256_add_epi32(a[k], a[15 - k]);
        d[k] = _mm256_sub_epi32(a[k], a[15 - k]);
      }
      for (k = 0; k < 4; k++) {
        e[k] = _mm256_add_epi32(c[k], c[7 - k]);
        f[k] = _mm256_sub_epi32(c[k], c[7 - k]);
      }
      g[0] = _mm256_add_epi32(e[0], e[3]);
      h[0] = _mm256_sub_epi32(e[0], e[3]);
      g[1] = _mm256_add_epi32(e[1], e[2]);
      h[1] = _mm256_sub_epi32(e[1], e[2]);

      a[0] = _mm256_add_epi32(g[0], g[1]);
      a[0] = _mm256_slli_epi32(a[0], 6);

      a[16] = _mm256_sub_epi32(g[0], g[1]);
      a[16] = _mm256_slli_epi32(a[16], 6);

      a[8] = _mm256_add_epi32(_mm256_mullo_epi32(h[0], txmat_8_0),
                              _mm256_mullo_epi32(h[1], txmat_8_1));

      a[24] = _mm256_sub_epi32(_mm256_mullo_epi32(h[0], txmat_8_1),
                               _mm256_mullo_epi32(h[1], txmat_8_0));

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_0);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_1);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_2);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_3);
      a[4] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_1);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_3);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_0);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_2);
      a[12] = _mm256_sub_epi32(_mm256_sub_epi32(e[0], e[1]),
                               _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_2);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_0);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_3);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_1);
      a[20] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                               _mm256_add_epi32(e[2], e[3]));

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_3);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_2);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_1);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_0);
      a[28] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                               _mm256_sub_epi32(e[2], e[3]));

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_0);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_1);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_2);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_3);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_4);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_5);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_6);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_7);
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[2] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_1);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_4);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_7);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_5);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_2);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_0);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_3);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_6);
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[6] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_2);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_7);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_3);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_1);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_6);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_4);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_0);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_5);
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[10] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_3);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_5);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_1);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_7);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_0);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_6);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_2);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_4);
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[14] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_4);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_2);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_6);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_0);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_7);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_1);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_5);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_3);
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[18] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_5);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_0);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_4);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_6);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_1);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_3);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_7);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_2);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[22] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_6);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_3);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_0);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_2);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_5);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_7);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_4);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_1);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[26] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_mullo_epi32(d[0], txmat_2_7);
      c[1] = _mm256_mullo_epi32(d[1], txmat_2_6);
      c[2] = _mm256_mullo_epi32(d[2], txmat_2_5);
      c[3] = _mm256_mullo_epi32(d[3], txmat_2_4);
      c[4] = _mm256_mullo_epi32(d[4], txmat_2_3);
      c[5] = _mm256_mullo_epi32(d[5], txmat_2_2);
      c[6] = _mm256_mullo_epi32(d[6], txmat_2_1);
      c[7] = _mm256_mullo_epi32(d[7], txmat_2_0);
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[30] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_0),
                              _mm256_mullo_epi32(b[1], txmat_1_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_2),
                              _mm256_mullo_epi32(b[3], txmat_1_3));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_4),
                              _mm256_mullo_epi32(b[5], txmat_1_5));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_6),
                              _mm256_mullo_epi32(b[7], txmat_1_7));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_8),
                              _mm256_mullo_epi32(b[9], txmat_1_9));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_10),
                              _mm256_mullo_epi32(b[11], txmat_1_11));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_12),
                              _mm256_mullo_epi32(b[13], txmat_1_13));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_14),
                              _mm256_mullo_epi32(b[15], txmat_1_15));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[1] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_1),
                              _mm256_mullo_epi32(b[1], txmat_1_4));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_7),
                              _mm256_mullo_epi32(b[3], txmat_1_10));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_13),
                              _mm256_mullo_epi32(b[5], txmat_1_15));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_12),
                              _mm256_mullo_epi32(b[7], txmat_1_9));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_6),
                              _mm256_mullo_epi32(b[9], txmat_1_3));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_0),
                              _mm256_mullo_epi32(b[11], txmat_1_2));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_5),
                              _mm256_mullo_epi32(b[13], txmat_1_8));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_11),
                              _mm256_mullo_epi32(b[15], txmat_1_14));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[3] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_2),
                              _mm256_mullo_epi32(b[1], txmat_1_7));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_12),
                              _mm256_mullo_epi32(b[3], txmat_1_14));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_9),
                              _mm256_mullo_epi32(b[5], txmat_1_4));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_0),
                              _mm256_mullo_epi32(b[7], txmat_1_5));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_10),
                              _mm256_mullo_epi32(b[9], txmat_1_15));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_11),
                              _mm256_mullo_epi32(b[11], txmat_1_6));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_1),
                              _mm256_mullo_epi32(b[13], txmat_1_3));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_8),
                              _mm256_mullo_epi32(b[15], txmat_1_13));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[5] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_3),
                              _mm256_mullo_epi32(b[1], txmat_1_10));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_14),
                              _mm256_mullo_epi32(b[3], txmat_1_7));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_0),
                              _mm256_mullo_epi32(b[5], txmat_1_6));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_13),
                              _mm256_mullo_epi32(b[7], txmat_1_11));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_4),
                              _mm256_mullo_epi32(b[9], txmat_1_2));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_9),
                              _mm256_mullo_epi32(b[11], txmat_1_15));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_8),
                              _mm256_mullo_epi32(b[13], txmat_1_1));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_5),
                              _mm256_mullo_epi32(b[15], txmat_1_12));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[7] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_4),
                              _mm256_mullo_epi32(b[1], txmat_1_13));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_9),
                              _mm256_mullo_epi32(b[3], txmat_1_0));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_8),
                              _mm256_mullo_epi32(b[5], txmat_1_14));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_5),
                              _mm256_mullo_epi32(b[7], txmat_1_3));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_12),
                              _mm256_mullo_epi32(b[9], txmat_1_10));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_1),
                              _mm256_mullo_epi32(b[11], txmat_1_7));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_15),
                              _mm256_mullo_epi32(b[13], txmat_1_6));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_2),
                              _mm256_mullo_epi32(b[15], txmat_1_11));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[9] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_5),
                              _mm256_mullo_epi32(b[1], txmat_1_15));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_4),
                              _mm256_mullo_epi32(b[3], txmat_1_6));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_14),
                              _mm256_mullo_epi32(b[5], txmat_1_3));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_7),
                              _mm256_mullo_epi32(b[7], txmat_1_13));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_2),
                              _mm256_mullo_epi32(b[9], txmat_1_8));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_12),
                              _mm256_mullo_epi32(b[11], txmat_1_1));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_9),
                              _mm256_mullo_epi32(b[13], txmat_1_11));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_0),
                              _mm256_mullo_epi32(b[15], txmat_1_10));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[11] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_6),
                              _mm256_mullo_epi32(b[1], txmat_1_12));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_0),
                              _mm256_mullo_epi32(b[3], txmat_1_13));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_5),
                              _mm256_mullo_epi32(b[5], txmat_1_7));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_11),
                              _mm256_mullo_epi32(b[7], txmat_1_1));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_14),
                              _mm256_mullo_epi32(b[9], txmat_1_4));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_8),
                              _mm256_mullo_epi32(b[11], txmat_1_10));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_2),
                              _mm256_mullo_epi32(b[13], txmat_1_15));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_3),
                              _mm256_mullo_epi32(b[15], txmat_1_9));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[13] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_7),
                              _mm256_mullo_epi32(b[1], txmat_1_9));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_5),
                              _mm256_mullo_epi32(b[3], txmat_1_11));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_3),
                              _mm256_mullo_epi32(b[5], txmat_1_13));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_1),
                              _mm256_mullo_epi32(b[7], txmat_1_15));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_0),
                              _mm256_mullo_epi32(b[9], txmat_1_14));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_2),
                              _mm256_mullo_epi32(b[11], txmat_1_12));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_4),
                              _mm256_mullo_epi32(b[13], txmat_1_10));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_6),
                              _mm256_mullo_epi32(b[15], txmat_1_8));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[15] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_8),
                              _mm256_mullo_epi32(b[1], txmat_1_6));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_10),
                              _mm256_mullo_epi32(b[3], txmat_1_4));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_12),
                              _mm256_mullo_epi32(b[5], txmat_1_2));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_14),
                              _mm256_mullo_epi32(b[7], txmat_1_0));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_15),
                              _mm256_mullo_epi32(b[9], txmat_1_1));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_13),
                              _mm256_mullo_epi32(b[11], txmat_1_3));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_11),
                              _mm256_mullo_epi32(b[13], txmat_1_5));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_9),
                              _mm256_mullo_epi32(b[15], txmat_1_7));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[17] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_9),
                              _mm256_mullo_epi32(b[1], txmat_1_3));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_15),
                              _mm256_mullo_epi32(b[3], txmat_1_2));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_10),
                              _mm256_mullo_epi32(b[5], txmat_1_8));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_4),
                              _mm256_mullo_epi32(b[7], txmat_1_14));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_1),
                              _mm256_mullo_epi32(b[9], txmat_1_11));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_7),
                              _mm256_mullo_epi32(b[11], txmat_1_5));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_13),
                              _mm256_mullo_epi32(b[13], txmat_1_0));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_12),
                              _mm256_mullo_epi32(b[15], txmat_1_6));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[19] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_10),
                              _mm256_mullo_epi32(b[1], txmat_1_0));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_11),
                              _mm256_mullo_epi32(b[3], txmat_1_9));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_1),
                              _mm256_mullo_epi32(b[5], txmat_1_12));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_8),
                              _mm256_mullo_epi32(b[7], txmat_1_2));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_13),
                              _mm256_mullo_epi32(b[9], txmat_1_7));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_3),
                              _mm256_mullo_epi32(b[11], txmat_1_14));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_6),
                              _mm256_mullo_epi32(b[13], txmat_1_4));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_15),
                              _mm256_mullo_epi32(b[15], txmat_1_5));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[21] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_11),
                              _mm256_mullo_epi32(b[1], txmat_1_2));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_6),
                              _mm256_mullo_epi32(b[3], txmat_1_15));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_7),
                              _mm256_mullo_epi32(b[5], txmat_1_1));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_10),
                              _mm256_mullo_epi32(b[7], txmat_1_12));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_3),
                              _mm256_mullo_epi32(b[9], txmat_1_5));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_14),
                              _mm256_mullo_epi32(b[11], txmat_1_8));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_0),
                              _mm256_mullo_epi32(b[13], txmat_1_9));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_13),
                              _mm256_mullo_epi32(b[15], txmat_1_4));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[23] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_12),
                              _mm256_mullo_epi32(b[1], txmat_1_5));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_1),
                              _mm256_mullo_epi32(b[3], txmat_1_8));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_15),
                              _mm256_mullo_epi32(b[5], txmat_1_9));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_2),
                              _mm256_mullo_epi32(b[7], txmat_1_4));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_11),
                              _mm256_mullo_epi32(b[9], txmat_1_13));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_6),
                              _mm256_mullo_epi32(b[11], txmat_1_0));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_7),
                              _mm256_mullo_epi32(b[13], txmat_1_14));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_10),
                              _mm256_mullo_epi32(b[15], txmat_1_3));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[25] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_13),
                              _mm256_mullo_epi32(b[1], txmat_1_8));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_3),
                              _mm256_mullo_epi32(b[3], txmat_1_1));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_6),
                              _mm256_mullo_epi32(b[5], txmat_1_11));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_15),
                              _mm256_mullo_epi32(b[7], txmat_1_10));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_5),
                              _mm256_mullo_epi32(b[9], txmat_1_0));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_4),
                              _mm256_mullo_epi32(b[11], txmat_1_9));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_14),
                              _mm256_mullo_epi32(b[13], txmat_1_12));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_7),
                              _mm256_mullo_epi32(b[15], txmat_1_2));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      a[27] = _mm256_sub_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_14),
                              _mm256_mullo_epi32(b[1], txmat_1_11));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_8),
                              _mm256_mullo_epi32(b[3], txmat_1_5));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_2),
                              _mm256_mullo_epi32(b[5], txmat_1_0));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_3),
                              _mm256_mullo_epi32(b[7], txmat_1_6));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_9),
                              _mm256_mullo_epi32(b[9], txmat_1_12));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_15),
                              _mm256_mullo_epi32(b[11], txmat_1_13));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_10),
                              _mm256_mullo_epi32(b[13], txmat_1_7));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_4),
                              _mm256_mullo_epi32(b[15], txmat_1_1));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[29] = _mm256_add_epi32(c[0], c[4]);

      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_15),
                              _mm256_mullo_epi32(b[1], txmat_1_14));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_13),
                              _mm256_mullo_epi32(b[3], txmat_1_12));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_11),
                              _mm256_mullo_epi32(b[5], txmat_1_10));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_9),
                              _mm256_mullo_epi32(b[7], txmat_1_8));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_7),
                              _mm256_mullo_epi32(b[9], txmat_1_6));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_5),
                              _mm256_mullo_epi32(b[11], txmat_1_4));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_3),
                              _mm256_mullo_epi32(b[13], txmat_1_2));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_1),
                              _mm256_mullo_epi32(b[15], txmat_1_0));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      a[31] = _mm256_add_epi32(c[0], c[4]);

      for (k = 0; k < 32; k++) {
        a[k] = _mm256_add_epi32(a[k], v_offset);
        a[k] = _mm256_srai_epi32(a[k], shift);
      }

      transpose_store_8x8_avx2(a, dst + 0, 32);
      transpose_store_8x8_avx2(a + 8, dst + 8, 32);
      transpose_store_8x8_avx2(a + 16, dst + 16, 32);
      transpose_store_8x8_avx2(a + 24, dst + 24, 32);

      src += 8;
      dst += tx1d_size * 8;
    }
  } else {
    __m128i a[32], b[16];
    __m128i c[8], d[8];
    __m128i e[4], f[4];
    __m128i g[2], h[2];

    __m128i v_offset = _mm_set1_epi32(offset);

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

    for (k = 0; k < 16; k++) {
      __m128i src0 = _mm_loadu_si128((__m128i *)&src[k * line]);
      __m128i src1 = _mm_loadu_si128((__m128i *)&src[(31 - k) * line]);
      a[k] = _mm_add_epi32(src0, src1);
      b[k] = _mm_sub_epi32(src0, src1);
    }
    for (k = 0; k < 8; k++) {
      c[k] = _mm_add_epi32(a[k], a[15 - k]);
      d[k] = _mm_sub_epi32(a[k], a[15 - k]);
    }
    for (k = 0; k < 4; k++) {
      e[k] = _mm_add_epi32(c[k], c[7 - k]);
      f[k] = _mm_sub_epi32(c[k], c[7 - k]);
    }
    g[0] = _mm_add_epi32(e[0], e[3]);
    h[0] = _mm_sub_epi32(e[0], e[3]);
    g[1] = _mm_add_epi32(e[1], e[2]);
    h[1] = _mm_sub_epi32(e[1], e[2]);

    a[0] = _mm_add_epi32(g[0], g[1]);
    a[0] = _mm_slli_epi32(a[0], 6);

    a[16] = _mm_sub_epi32(g[0], g[1]);
    a[16] = _mm_slli_epi32(a[16], 6);

    a[8] = _mm_add_epi32(_mm_mullo_epi32(h[0], txmat_8_0),
                         _mm_mullo_epi32(h[1], txmat_8_1));

    a[24] = _mm_sub_epi32(_mm_mullo_epi32(h[0], txmat_8_1),
                          _mm_mullo_epi32(h[1], txmat_8_0));

    e[0] = _mm_mullo_epi32(f[0], txmat_4_0);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_1);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_2);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_3);
    a[4] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(f[0], txmat_4_1);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_3);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_0);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_2);
    a[12] = _mm_sub_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(f[0], txmat_4_2);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_0);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_3);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_1);
    a[20] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));

    e[0] = _mm_mullo_epi32(f[0], txmat_4_3);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_2);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_1);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_0);
    a[28] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));

    c[0] = _mm_mullo_epi32(d[0], txmat_2_0);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_1);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_2);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_3);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_4);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_5);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_6);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_7);
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[2] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_1);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_4);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_7);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_5);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_2);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_0);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_3);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_6);
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[6] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_2);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_7);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_3);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_1);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_6);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_4);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_0);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_5);
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[10] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_3);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_5);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_1);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_7);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_0);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_6);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_2);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_4);
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[14] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_4);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_2);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_6);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_0);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_7);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_1);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_5);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_3);
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[18] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_5);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_0);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_4);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_6);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_1);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_3);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_7);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_2);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[22] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_6);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_3);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_0);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_2);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_5);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_7);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_4);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_1);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[26] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_mullo_epi32(d[0], txmat_2_7);
    c[1] = _mm_mullo_epi32(d[1], txmat_2_6);
    c[2] = _mm_mullo_epi32(d[2], txmat_2_5);
    c[3] = _mm_mullo_epi32(d[3], txmat_2_4);
    c[4] = _mm_mullo_epi32(d[4], txmat_2_3);
    c[5] = _mm_mullo_epi32(d[5], txmat_2_2);
    c[6] = _mm_mullo_epi32(d[6], txmat_2_1);
    c[7] = _mm_mullo_epi32(d[7], txmat_2_0);
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[30] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_0),
                         _mm_mullo_epi32(b[1], txmat_1_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_2),
                         _mm_mullo_epi32(b[3], txmat_1_3));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_4),
                         _mm_mullo_epi32(b[5], txmat_1_5));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_6),
                         _mm_mullo_epi32(b[7], txmat_1_7));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_8),
                         _mm_mullo_epi32(b[9], txmat_1_9));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_10),
                         _mm_mullo_epi32(b[11], txmat_1_11));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_12),
                         _mm_mullo_epi32(b[13], txmat_1_13));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_14),
                         _mm_mullo_epi32(b[15], txmat_1_15));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[1] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_1),
                         _mm_mullo_epi32(b[1], txmat_1_4));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_7),
                         _mm_mullo_epi32(b[3], txmat_1_10));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_13),
                         _mm_mullo_epi32(b[5], txmat_1_15));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_12),
                         _mm_mullo_epi32(b[7], txmat_1_9));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_6),
                         _mm_mullo_epi32(b[9], txmat_1_3));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_0),
                         _mm_mullo_epi32(b[11], txmat_1_2));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_5),
                         _mm_mullo_epi32(b[13], txmat_1_8));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_11),
                         _mm_mullo_epi32(b[15], txmat_1_14));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[3] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_2),
                         _mm_mullo_epi32(b[1], txmat_1_7));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_12),
                         _mm_mullo_epi32(b[3], txmat_1_14));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_9),
                         _mm_mullo_epi32(b[5], txmat_1_4));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_0),
                         _mm_mullo_epi32(b[7], txmat_1_5));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_10),
                         _mm_mullo_epi32(b[9], txmat_1_15));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_11),
                         _mm_mullo_epi32(b[11], txmat_1_6));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_1),
                         _mm_mullo_epi32(b[13], txmat_1_3));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_8),
                         _mm_mullo_epi32(b[15], txmat_1_13));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[5] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_3),
                         _mm_mullo_epi32(b[1], txmat_1_10));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_14),
                         _mm_mullo_epi32(b[3], txmat_1_7));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_0),
                         _mm_mullo_epi32(b[5], txmat_1_6));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_13),
                         _mm_mullo_epi32(b[7], txmat_1_11));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_4),
                         _mm_mullo_epi32(b[9], txmat_1_2));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_9),
                         _mm_mullo_epi32(b[11], txmat_1_15));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_8),
                         _mm_mullo_epi32(b[13], txmat_1_1));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_5),
                         _mm_mullo_epi32(b[15], txmat_1_12));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[7] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_4),
                         _mm_mullo_epi32(b[1], txmat_1_13));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_9),
                         _mm_mullo_epi32(b[3], txmat_1_0));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_8),
                         _mm_mullo_epi32(b[5], txmat_1_14));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_5),
                         _mm_mullo_epi32(b[7], txmat_1_3));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_12),
                         _mm_mullo_epi32(b[9], txmat_1_10));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_1),
                         _mm_mullo_epi32(b[11], txmat_1_7));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_15),
                         _mm_mullo_epi32(b[13], txmat_1_6));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_2),
                         _mm_mullo_epi32(b[15], txmat_1_11));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[9] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_5),
                         _mm_mullo_epi32(b[1], txmat_1_15));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_4),
                         _mm_mullo_epi32(b[3], txmat_1_6));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_14),
                         _mm_mullo_epi32(b[5], txmat_1_3));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_7),
                         _mm_mullo_epi32(b[7], txmat_1_13));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_2),
                         _mm_mullo_epi32(b[9], txmat_1_8));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_12),
                         _mm_mullo_epi32(b[11], txmat_1_1));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_9),
                         _mm_mullo_epi32(b[13], txmat_1_11));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_0),
                         _mm_mullo_epi32(b[15], txmat_1_10));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[11] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_6),
                         _mm_mullo_epi32(b[1], txmat_1_12));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_0),
                         _mm_mullo_epi32(b[3], txmat_1_13));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_5),
                         _mm_mullo_epi32(b[5], txmat_1_7));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_11),
                         _mm_mullo_epi32(b[7], txmat_1_1));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_14),
                         _mm_mullo_epi32(b[9], txmat_1_4));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_8),
                         _mm_mullo_epi32(b[11], txmat_1_10));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_2),
                         _mm_mullo_epi32(b[13], txmat_1_15));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_3),
                         _mm_mullo_epi32(b[15], txmat_1_9));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[13] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_7),
                         _mm_mullo_epi32(b[1], txmat_1_9));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_5),
                         _mm_mullo_epi32(b[3], txmat_1_11));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_3),
                         _mm_mullo_epi32(b[5], txmat_1_13));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_1),
                         _mm_mullo_epi32(b[7], txmat_1_15));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_0),
                         _mm_mullo_epi32(b[9], txmat_1_14));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_2),
                         _mm_mullo_epi32(b[11], txmat_1_12));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_4),
                         _mm_mullo_epi32(b[13], txmat_1_10));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_6),
                         _mm_mullo_epi32(b[15], txmat_1_8));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[15] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_8),
                         _mm_mullo_epi32(b[1], txmat_1_6));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_10),
                         _mm_mullo_epi32(b[3], txmat_1_4));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_12),
                         _mm_mullo_epi32(b[5], txmat_1_2));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_14),
                         _mm_mullo_epi32(b[7], txmat_1_0));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_15),
                         _mm_mullo_epi32(b[9], txmat_1_1));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_13),
                         _mm_mullo_epi32(b[11], txmat_1_3));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_11),
                         _mm_mullo_epi32(b[13], txmat_1_5));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_9),
                         _mm_mullo_epi32(b[15], txmat_1_7));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[17] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_9),
                         _mm_mullo_epi32(b[1], txmat_1_3));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_15),
                         _mm_mullo_epi32(b[3], txmat_1_2));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_10),
                         _mm_mullo_epi32(b[5], txmat_1_8));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_4),
                         _mm_mullo_epi32(b[7], txmat_1_14));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_1),
                         _mm_mullo_epi32(b[9], txmat_1_11));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_7),
                         _mm_mullo_epi32(b[11], txmat_1_5));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_13),
                         _mm_mullo_epi32(b[13], txmat_1_0));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_12),
                         _mm_mullo_epi32(b[15], txmat_1_6));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[19] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_10),
                         _mm_mullo_epi32(b[1], txmat_1_0));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_11),
                         _mm_mullo_epi32(b[3], txmat_1_9));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_1),
                         _mm_mullo_epi32(b[5], txmat_1_12));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_8),
                         _mm_mullo_epi32(b[7], txmat_1_2));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_13),
                         _mm_mullo_epi32(b[9], txmat_1_7));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_3),
                         _mm_mullo_epi32(b[11], txmat_1_14));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_6),
                         _mm_mullo_epi32(b[13], txmat_1_4));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_15),
                         _mm_mullo_epi32(b[15], txmat_1_5));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[21] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_11),
                         _mm_mullo_epi32(b[1], txmat_1_2));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_6),
                         _mm_mullo_epi32(b[3], txmat_1_15));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_7),
                         _mm_mullo_epi32(b[5], txmat_1_1));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_10),
                         _mm_mullo_epi32(b[7], txmat_1_12));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_3),
                         _mm_mullo_epi32(b[9], txmat_1_5));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_14),
                         _mm_mullo_epi32(b[11], txmat_1_8));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_0),
                         _mm_mullo_epi32(b[13], txmat_1_9));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_13),
                         _mm_mullo_epi32(b[15], txmat_1_4));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[23] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_12),
                         _mm_mullo_epi32(b[1], txmat_1_5));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_1),
                         _mm_mullo_epi32(b[3], txmat_1_8));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_15),
                         _mm_mullo_epi32(b[5], txmat_1_9));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_2),
                         _mm_mullo_epi32(b[7], txmat_1_4));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_11),
                         _mm_mullo_epi32(b[9], txmat_1_13));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_6),
                         _mm_mullo_epi32(b[11], txmat_1_0));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_7),
                         _mm_mullo_epi32(b[13], txmat_1_14));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_10),
                         _mm_mullo_epi32(b[15], txmat_1_3));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[25] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_13),
                         _mm_mullo_epi32(b[1], txmat_1_8));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_3),
                         _mm_mullo_epi32(b[3], txmat_1_1));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_6),
                         _mm_mullo_epi32(b[5], txmat_1_11));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_15),
                         _mm_mullo_epi32(b[7], txmat_1_10));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_5),
                         _mm_mullo_epi32(b[9], txmat_1_0));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_4),
                         _mm_mullo_epi32(b[11], txmat_1_9));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_14),
                         _mm_mullo_epi32(b[13], txmat_1_12));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_7),
                         _mm_mullo_epi32(b[15], txmat_1_2));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    a[27] = _mm_sub_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_14),
                         _mm_mullo_epi32(b[1], txmat_1_11));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_8),
                         _mm_mullo_epi32(b[3], txmat_1_5));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_2),
                         _mm_mullo_epi32(b[5], txmat_1_0));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_3),
                         _mm_mullo_epi32(b[7], txmat_1_6));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_9),
                         _mm_mullo_epi32(b[9], txmat_1_12));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_15),
                         _mm_mullo_epi32(b[11], txmat_1_13));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_10),
                         _mm_mullo_epi32(b[13], txmat_1_7));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_4),
                         _mm_mullo_epi32(b[15], txmat_1_1));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[29] = _mm_add_epi32(c[0], c[4]);

    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_15),
                         _mm_mullo_epi32(b[1], txmat_1_14));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_13),
                         _mm_mullo_epi32(b[3], txmat_1_12));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_11),
                         _mm_mullo_epi32(b[5], txmat_1_10));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_9),
                         _mm_mullo_epi32(b[7], txmat_1_8));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_7),
                         _mm_mullo_epi32(b[9], txmat_1_6));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_5),
                         _mm_mullo_epi32(b[11], txmat_1_4));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_3),
                         _mm_mullo_epi32(b[13], txmat_1_2));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_1),
                         _mm_mullo_epi32(b[15], txmat_1_0));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    a[31] = _mm_add_epi32(c[0], c[4]);

    for (k = 0; k < 32; k++) {
      a[k] = _mm_add_epi32(a[k], v_offset);
      a[k] = _mm_srai_epi32(a[k], shift);
    }

    transpose_store_8x4_sse4(a, dst + 0, 32);
    transpose_store_8x4_sse4(a + 8, dst + 8, 32);
    transpose_store_8x4_sse4(a + 16, dst + 16, 32);
    transpose_store_8x4_sse4(a + 24, dst + 24, 32);
  }
}

void fwd_txfm_dct2_size64_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  int m, n;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;

  const int *tx_mat = tx_kernel_dct2_size64[FWD_TXFM][0];

  const int tx1d_size = 64;

  if (nz_line >= 8) {
    __m256i a[32], b[32];
    __m256i c[16], d[16];
    __m256i e[8], f[8];
    __m256i g[4], h[4];
    __m256i i[2], j[2];

    __m256i v_offset = _mm256_set1_epi32(offset);

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

    for (m = 0; m < nz_line; m += 8) {
      for (n = 0; n < 32; n++) {
        __m256i src0 = _mm256_loadu_si256((__m256i *)&src[n * line]);
        __m256i src1 =
            _mm256_loadu_si256((__m256i *)&src[(tx1d_size - 1 - n) * line]);
        a[n] = _mm256_add_epi32(src0, src1);
        b[n] = _mm256_sub_epi32(src0, src1);
      }
      for (n = 0; n < 16; n++) {
        c[n] = _mm256_add_epi32(a[n], a[31 - n]);
        d[n] = _mm256_sub_epi32(a[n], a[31 - n]);
      }
      for (n = 0; n < 8; n++) {
        e[n] = _mm256_add_epi32(c[n], c[15 - n]);
        f[n] = _mm256_sub_epi32(c[n], c[15 - n]);
      }
      for (n = 0; n < 4; n++) {
        g[n] = _mm256_add_epi32(e[n], e[7 - n]);
        h[n] = _mm256_sub_epi32(e[n], e[7 - n]);
      }
      i[0] = _mm256_add_epi32(g[0], g[3]);
      j[0] = _mm256_sub_epi32(g[0], g[3]);
      i[1] = _mm256_add_epi32(g[1], g[2]);
      j[1] = _mm256_sub_epi32(g[1], g[2]);

      a[0] = _mm256_slli_epi32(_mm256_add_epi32(i[0], i[1]), 6);

      a[16] = _mm256_add_epi32(_mm256_mullo_epi32(j[0], txmat_16_0),
                               _mm256_mullo_epi32(j[1], txmat_16_1));

      g[0] = _mm256_mullo_epi32(h[0], txmat_8_0);
      g[1] = _mm256_mullo_epi32(h[1], txmat_8_1);
      g[2] = _mm256_mullo_epi32(h[2], txmat_8_2);
      g[3] = _mm256_mullo_epi32(h[3], txmat_8_3);
      a[8] = _mm256_add_epi32(_mm256_add_epi32(g[0], g[1]),
                              _mm256_add_epi32(g[2], g[3]));

      g[0] = _mm256_mullo_epi32(h[0], txmat_8_1);
      g[1] = _mm256_mullo_epi32(h[1], txmat_8_3);
      g[2] = _mm256_mullo_epi32(h[2], txmat_8_0);
      g[3] = _mm256_mullo_epi32(h[3], txmat_8_2);
      a[24] = _mm256_sub_epi32(_mm256_sub_epi32(g[0], g[1]),
                               _mm256_add_epi32(g[2], g[3]));

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_0);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_1);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_2);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_3);
      e[4] = _mm256_mullo_epi32(f[4], txmat_4_4);
      e[5] = _mm256_mullo_epi32(f[5], txmat_4_5);
      e[6] = _mm256_mullo_epi32(f[6], txmat_4_6);
      e[7] = _mm256_mullo_epi32(f[7], txmat_4_7);
      e[0] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[4] = _mm256_add_epi32(e[0], e[4]);

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_1);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_4);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_7);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_5);
      e[4] = _mm256_mullo_epi32(f[4], txmat_4_2);
      e[5] = _mm256_mullo_epi32(f[5], txmat_4_0);
      e[6] = _mm256_mullo_epi32(f[6], txmat_4_3);
      e[7] = _mm256_mullo_epi32(f[7], txmat_4_6);
      e[0] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[12] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_2);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_7);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_3);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_1);
      e[4] = _mm256_mullo_epi32(f[4], txmat_4_6);
      e[5] = _mm256_mullo_epi32(f[5], txmat_4_4);
      e[6] = _mm256_mullo_epi32(f[6], txmat_4_0);
      e[7] = _mm256_mullo_epi32(f[7], txmat_4_5);
      e[0] = _mm256_sub_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_sub_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[20] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_mullo_epi32(f[0], txmat_4_3);
      e[1] = _mm256_mullo_epi32(f[1], txmat_4_5);
      e[2] = _mm256_mullo_epi32(f[2], txmat_4_1);
      e[3] = _mm256_mullo_epi32(f[3], txmat_4_7);
      e[4] = _mm256_mullo_epi32(f[4], txmat_4_0);
      e[5] = _mm256_mullo_epi32(f[5], txmat_4_6);
      e[6] = _mm256_mullo_epi32(f[6], txmat_4_2);
      e[7] = _mm256_mullo_epi32(f[7], txmat_4_4);
      e[0] = _mm256_sub_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_sub_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[28] = _mm256_add_epi32(e[0], e[4]);

      e[0] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_0),
                              _mm256_mullo_epi32(d[1], txmat_2_1));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_2),
                              _mm256_mullo_epi32(d[3], txmat_2_3));
      e[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[4], txmat_2_4),
                              _mm256_mullo_epi32(d[5], txmat_2_5));
      e[3] = _mm256_add_epi32(_mm256_mullo_epi32(d[6], txmat_2_6),
                              _mm256_mullo_epi32(d[7], txmat_2_7));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_8),
                              _mm256_mullo_epi32(d[9], txmat_2_9));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_10),
                              _mm256_mullo_epi32(d[11], txmat_2_11));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_12),
                              _mm256_mullo_epi32(d[13], txmat_2_13));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_14),
                              _mm256_mullo_epi32(d[15], txmat_2_15));
      e[0] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[2] = _mm256_add_epi32(e[0], e[4]);

      e[0] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_1),
                              _mm256_mullo_epi32(d[1], txmat_2_4));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_7),
                              _mm256_mullo_epi32(d[3], txmat_2_10));
      e[2] = _mm256_sub_epi32(_mm256_mullo_epi32(d[4], txmat_2_13),
                              _mm256_mullo_epi32(d[5], txmat_2_15));
      e[3] = _mm256_add_epi32(_mm256_mullo_epi32(d[6], txmat_2_12),
                              _mm256_mullo_epi32(d[7], txmat_2_9));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_6),
                              _mm256_mullo_epi32(d[9], txmat_2_3));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_0),
                              _mm256_mullo_epi32(d[11], txmat_2_2));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_5),
                              _mm256_mullo_epi32(d[13], txmat_2_8));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_11),
                              _mm256_mullo_epi32(d[15], txmat_2_14));
      e[0] = _mm256_add_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[6] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_2),
                              _mm256_mullo_epi32(d[1], txmat_2_7));
      e[1] = _mm256_sub_epi32(_mm256_mullo_epi32(d[2], txmat_2_12),
                              _mm256_mullo_epi32(d[3], txmat_2_14));
      e[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[4], txmat_2_9),
                              _mm256_mullo_epi32(d[5], txmat_2_4));
      e[3] = _mm256_add_epi32(_mm256_mullo_epi32(d[6], txmat_2_0),
                              _mm256_mullo_epi32(d[7], txmat_2_5));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_10),
                              _mm256_mullo_epi32(d[9], txmat_2_15));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_11),
                              _mm256_mullo_epi32(d[11], txmat_2_6));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_1),
                              _mm256_mullo_epi32(d[13], txmat_2_3));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_8),
                              _mm256_mullo_epi32(d[15], txmat_2_13));
      e[0] = _mm256_sub_epi32(_mm256_add_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_sub_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[10] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_3),
                              _mm256_mullo_epi32(d[1], txmat_2_10));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_14),
                              _mm256_mullo_epi32(d[3], txmat_2_7));
      e[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[4], txmat_2_0),
                              _mm256_mullo_epi32(d[5], txmat_2_6));
      e[3] = _mm256_sub_epi32(_mm256_mullo_epi32(d[6], txmat_2_13),
                              _mm256_mullo_epi32(d[7], txmat_2_11));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_4),
                              _mm256_mullo_epi32(d[9], txmat_2_2));
      e[5] = _mm256_sub_epi32(_mm256_mullo_epi32(d[10], txmat_2_9),
                              _mm256_mullo_epi32(d[11], txmat_2_15));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_8),
                              _mm256_mullo_epi32(d[13], txmat_2_1));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_5),
                              _mm256_mullo_epi32(d[15], txmat_2_12));
      e[0] = _mm256_sub_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_sub_epi32(_mm256_add_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[14] = _mm256_add_epi32(e[0], e[4]);

      e[0] = _mm256_add_epi32(_mm256_mullo_epi32(d[0], txmat_2_4),
                              _mm256_mullo_epi32(d[1], txmat_2_13));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_9),
                              _mm256_mullo_epi32(d[3], txmat_2_0));
      e[2] = _mm256_sub_epi32(_mm256_mullo_epi32(d[4], txmat_2_8),
                              _mm256_mullo_epi32(d[5], txmat_2_14));
      e[3] = _mm256_add_epi32(_mm256_mullo_epi32(d[6], txmat_2_5),
                              _mm256_mullo_epi32(d[7], txmat_2_3));
      e[4] = _mm256_sub_epi32(_mm256_mullo_epi32(d[8], txmat_2_12),
                              _mm256_mullo_epi32(d[9], txmat_2_10));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_1),
                              _mm256_mullo_epi32(d[11], txmat_2_7));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_15),
                              _mm256_mullo_epi32(d[13], txmat_2_6));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_2),
                              _mm256_mullo_epi32(d[15], txmat_2_11));
      e[0] = _mm256_sub_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_add_epi32(e[6], e[7]));
      a[18] = _mm256_add_epi32(e[0], e[4]);

      e[0] = _mm256_sub_epi32(_mm256_mullo_epi32(d[0], txmat_2_5),
                              _mm256_mullo_epi32(d[1], txmat_2_15));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_4),
                              _mm256_mullo_epi32(d[3], txmat_2_6));
      e[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[4], txmat_2_14),
                              _mm256_mullo_epi32(d[5], txmat_2_3));
      e[3] = _mm256_sub_epi32(_mm256_mullo_epi32(d[6], txmat_2_7),
                              _mm256_mullo_epi32(d[7], txmat_2_13));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_2),
                              _mm256_mullo_epi32(d[9], txmat_2_8));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_12),
                              _mm256_mullo_epi32(d[11], txmat_2_1));
      e[6] = _mm256_sub_epi32(_mm256_mullo_epi32(d[12], txmat_2_9),
                              _mm256_mullo_epi32(d[13], txmat_2_11));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_0),
                              _mm256_mullo_epi32(d[15], txmat_2_10));
      e[0] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_add_epi32(e[2], e[3]));
      e[4] = _mm256_sub_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_sub_epi32(e[6], e[7]));
      a[22] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_sub_epi32(_mm256_mullo_epi32(d[0], txmat_2_6),
                              _mm256_mullo_epi32(d[1], txmat_2_12));
      e[1] = _mm256_add_epi32(_mm256_mullo_epi32(d[2], txmat_2_0),
                              _mm256_mullo_epi32(d[3], txmat_2_13));
      e[2] = _mm256_add_epi32(_mm256_mullo_epi32(d[4], txmat_2_5),
                              _mm256_mullo_epi32(d[5], txmat_2_7));
      e[3] = _mm256_add_epi32(_mm256_mullo_epi32(d[6], txmat_2_11),
                              _mm256_mullo_epi32(d[7], txmat_2_1));
      e[4] = _mm256_sub_epi32(_mm256_mullo_epi32(d[8], txmat_2_14),
                              _mm256_mullo_epi32(d[9], txmat_2_4));
      e[5] = _mm256_sub_epi32(_mm256_mullo_epi32(d[10], txmat_2_8),
                              _mm256_mullo_epi32(d[11], txmat_2_10));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_2),
                              _mm256_mullo_epi32(d[13], txmat_2_15));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_3),
                              _mm256_mullo_epi32(d[15], txmat_2_9));
      e[0] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_sub_epi32(e[6], e[7]));
      a[26] = _mm256_sub_epi32(e[0], e[4]);

      e[0] = _mm256_sub_epi32(_mm256_mullo_epi32(d[0], txmat_2_7),
                              _mm256_mullo_epi32(d[1], txmat_2_9));
      e[1] = _mm256_sub_epi32(_mm256_mullo_epi32(d[2], txmat_2_5),
                              _mm256_mullo_epi32(d[3], txmat_2_11));
      e[2] = _mm256_sub_epi32(_mm256_mullo_epi32(d[4], txmat_2_3),
                              _mm256_mullo_epi32(d[5], txmat_2_13));
      e[3] = _mm256_sub_epi32(_mm256_mullo_epi32(d[6], txmat_2_1),
                              _mm256_mullo_epi32(d[7], txmat_2_15));
      e[4] = _mm256_add_epi32(_mm256_mullo_epi32(d[8], txmat_2_0),
                              _mm256_mullo_epi32(d[9], txmat_2_14));
      e[5] = _mm256_add_epi32(_mm256_mullo_epi32(d[10], txmat_2_2),
                              _mm256_mullo_epi32(d[11], txmat_2_12));
      e[6] = _mm256_add_epi32(_mm256_mullo_epi32(d[12], txmat_2_4),
                              _mm256_mullo_epi32(d[13], txmat_2_10));
      e[7] = _mm256_add_epi32(_mm256_mullo_epi32(d[14], txmat_2_6),
                              _mm256_mullo_epi32(d[15], txmat_2_8));
      e[0] = _mm256_add_epi32(_mm256_sub_epi32(e[0], e[1]),
                              _mm256_sub_epi32(e[2], e[3]));
      e[4] = _mm256_add_epi32(_mm256_sub_epi32(e[4], e[5]),
                              _mm256_sub_epi32(e[6], e[7]));
      a[30] = _mm256_add_epi32(e[0], e[4]);

      // 362, 361, 359, 357,      353, 349, 344, 338,      331, 323, 315, 306,
      // 296, 285, 274, 262,      250, 236, 223, 208,      194, 178, 163, 147,
      // 130, 114,  97,  79,       62,  44,  27,   9
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_0),
                              _mm256_mullo_epi32(b[1], txmat_1_1));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_2),
                              _mm256_mullo_epi32(b[3], txmat_1_3));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_4),
                              _mm256_mullo_epi32(b[5], txmat_1_5));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_6),
                              _mm256_mullo_epi32(b[7], txmat_1_7));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_8),
                              _mm256_mullo_epi32(b[9], txmat_1_9));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_10),
                              _mm256_mullo_epi32(b[11], txmat_1_11));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_12),
                              _mm256_mullo_epi32(b[13], txmat_1_13));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_14),
                              _mm256_mullo_epi32(b[15], txmat_1_15));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_16),
                              _mm256_mullo_epi32(b[17], txmat_1_17));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_18),
                              _mm256_mullo_epi32(b[19], txmat_1_19));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_20),
                               _mm256_mullo_epi32(b[21], txmat_1_21));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_22),
                               _mm256_mullo_epi32(b[23], txmat_1_23));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_24),
                               _mm256_mullo_epi32(b[25], txmat_1_25));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_26),
                               _mm256_mullo_epi32(b[27], txmat_1_27));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_28),
                               _mm256_mullo_epi32(b[29], txmat_1_29));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_30),
                               _mm256_mullo_epi32(b[31], txmat_1_31));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[1] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[4]),
                              _mm256_add_epi32(c[8], c[12]));

      // 361, 353, 338, 315,      285, 250, 208, 163,      114,  62,   9, -44,
      // -97,-147,-194,-236,     -274,-306,-331,-349,     -359,-362,-357,-344,
      // -323,-296,-262,-223,     -178,-130, -79, -27
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_1),
                              _mm256_mullo_epi32(b[1], txmat_1_4));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_7),
                              _mm256_mullo_epi32(b[3], txmat_1_10));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_13),
                              _mm256_mullo_epi32(b[5], txmat_1_16));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_19),
                              _mm256_mullo_epi32(b[7], txmat_1_22));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_25),
                              _mm256_mullo_epi32(b[9], txmat_1_28));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_31),
                              _mm256_mullo_epi32(b[11], txmat_1_29));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_26),
                              _mm256_mullo_epi32(b[13], txmat_1_23));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_20),
                              _mm256_mullo_epi32(b[15], txmat_1_17));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_14),
                              _mm256_mullo_epi32(b[17], txmat_1_11));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_8),
                              _mm256_mullo_epi32(b[19], txmat_1_5));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_2),
                               _mm256_mullo_epi32(b[21], txmat_1_0));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_3),
                               _mm256_mullo_epi32(b[23], txmat_1_6));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_9),
                               _mm256_mullo_epi32(b[25], txmat_1_12));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_15),
                               _mm256_mullo_epi32(b[27], txmat_1_18));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_21),
                               _mm256_mullo_epi32(b[29], txmat_1_24));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_27),
                               _mm256_mullo_epi32(b[31], txmat_1_30));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[3] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[4]),
                              _mm256_add_epi32(c[8], c[12]));

      // 359, 338, 296, 236,      163,  79,  -9, -97,     -178,-250,-306,-344,
      // -361,-357,-331,-285,     -223,-147, -62,  27,      114, 194, 262, 315,
      // 349, 362, 353, 323,      274, 208, 130,  44
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_2),
                              _mm256_mullo_epi32(b[1], txmat_1_7));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_12),
                              _mm256_mullo_epi32(b[3], txmat_1_17));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_22),
                              _mm256_mullo_epi32(b[5], txmat_1_27));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_31),
                              _mm256_mullo_epi32(b[7], txmat_1_26));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_21),
                              _mm256_mullo_epi32(b[9], txmat_1_16));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_11),
                              _mm256_mullo_epi32(b[11], txmat_1_6));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_1),
                              _mm256_mullo_epi32(b[13], txmat_1_3));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_8),
                              _mm256_mullo_epi32(b[15], txmat_1_13));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_18),
                              _mm256_mullo_epi32(b[17], txmat_1_23));
      c[9] = _mm256_sub_epi32(_mm256_mullo_epi32(b[18], txmat_1_28),
                              _mm256_mullo_epi32(b[19], txmat_1_30));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_25),
                               _mm256_mullo_epi32(b[21], txmat_1_20));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_15),
                               _mm256_mullo_epi32(b[23], txmat_1_10));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_5),
                               _mm256_mullo_epi32(b[25], txmat_1_0));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_4),
                               _mm256_mullo_epi32(b[27], txmat_1_9));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_14),
                               _mm256_mullo_epi32(b[29], txmat_1_19));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_24),
                               _mm256_mullo_epi32(b[31], txmat_1_29));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[5] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[4]),
                              _mm256_sub_epi32(c[8], c[12]));

      // 357, 315, 236, 130,        9,-114,-223,-306,     -353,-359,-323,-250,
      // -147, -27,  97, 208,      296, 349, 361, 331,      262, 163,  44, -79,
      // -194,-285,-344,-362,     -338,-274,-178, -62
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_3),
                              _mm256_mullo_epi32(b[1], txmat_1_10));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_17),
                              _mm256_mullo_epi32(b[3], txmat_1_24));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_31),
                              _mm256_mullo_epi32(b[5], txmat_1_25));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_18),
                              _mm256_mullo_epi32(b[7], txmat_1_11));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_4),
                              _mm256_mullo_epi32(b[9], txmat_1_2));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_9),
                              _mm256_mullo_epi32(b[11], txmat_1_16));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_23),
                              _mm256_mullo_epi32(b[13], txmat_1_30));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_26),
                              _mm256_mullo_epi32(b[15], txmat_1_19));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_12),
                              _mm256_mullo_epi32(b[17], txmat_1_5));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_1),
                              _mm256_mullo_epi32(b[19], txmat_1_8));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_15),
                               _mm256_mullo_epi32(b[21], txmat_1_22));
      c[11] = _mm256_sub_epi32(_mm256_mullo_epi32(b[22], txmat_1_29),
                               _mm256_mullo_epi32(b[23], txmat_1_27));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_20),
                               _mm256_mullo_epi32(b[25], txmat_1_13));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_6),
                               _mm256_mullo_epi32(b[27], txmat_1_0));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_7),
                               _mm256_mullo_epi32(b[29], txmat_1_14));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_21),
                               _mm256_mullo_epi32(b[31], txmat_1_28));
      c[0] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[7] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[4]),
                              _mm256_sub_epi32(c[8], c[12]));

      // 353, 285, 163,   9,     -147,-274,-349,-357,     -296,-178, -27, 130,
      // 262, 344, 359, 306,      194,  44,-114,-250,     -338,-361,-315,-208,
      // -62,  97, 236, 331,      362, 323, 223,  79
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_4),
                              _mm256_mullo_epi32(b[1], txmat_1_13));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_22),
                              _mm256_mullo_epi32(b[3], txmat_1_31));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_23),
                              _mm256_mullo_epi32(b[5], txmat_1_14));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_5),
                              _mm256_mullo_epi32(b[7], txmat_1_3));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_12),
                              _mm256_mullo_epi32(b[9], txmat_1_21));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_30),
                              _mm256_mullo_epi32(b[11], txmat_1_24));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_15),
                              _mm256_mullo_epi32(b[13], txmat_1_6));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_2),
                              _mm256_mullo_epi32(b[15], txmat_1_11));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_20),
                              _mm256_mullo_epi32(b[17], txmat_1_29));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_25),
                              _mm256_mullo_epi32(b[19], txmat_1_16));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_7),
                               _mm256_mullo_epi32(b[21], txmat_1_1));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_10),
                               _mm256_mullo_epi32(b[23], txmat_1_19));
      c[12] = _mm256_sub_epi32(_mm256_mullo_epi32(b[24], txmat_1_28),
                               _mm256_mullo_epi32(b[25], txmat_1_26));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_17),
                               _mm256_mullo_epi32(b[27], txmat_1_8));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_0),
                               _mm256_mullo_epi32(b[29], txmat_1_9));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_18),
                               _mm256_mullo_epi32(b[31], txmat_1_27));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[9] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[4]),
                              _mm256_sub_epi32(c[8], c[12]));

      // 349, 250,  79,-114,     -274,-357,-338,-223,      -44, 147, 296, 361,
      // 323, 194,   9,-178,     -315,-362,-306,-163,       27, 208, 331, 359,
      // 285, 130, -62,-236,     -344,-353,-262, -97
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_5),
                              _mm256_mullo_epi32(b[1], txmat_1_16));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_27),
                              _mm256_mullo_epi32(b[3], txmat_1_25));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_14),
                              _mm256_mullo_epi32(b[5], txmat_1_3));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_7),
                              _mm256_mullo_epi32(b[7], txmat_1_18));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_29),
                              _mm256_mullo_epi32(b[9], txmat_1_23));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_12),
                              _mm256_mullo_epi32(b[11], txmat_1_1));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_9),
                              _mm256_mullo_epi32(b[13], txmat_1_20));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_31),
                              _mm256_mullo_epi32(b[15], txmat_1_21));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_10),
                              _mm256_mullo_epi32(b[17], txmat_1_0));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_11),
                              _mm256_mullo_epi32(b[19], txmat_1_22));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_30),
                               _mm256_mullo_epi32(b[21], txmat_1_19));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_8),
                               _mm256_mullo_epi32(b[23], txmat_1_2));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_13),
                               _mm256_mullo_epi32(b[25], txmat_1_24));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_28),
                               _mm256_mullo_epi32(b[27], txmat_1_17));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_6),
                               _mm256_mullo_epi32(b[29], txmat_1_4));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_15),
                               _mm256_mullo_epi32(b[31], txmat_1_26));
      c[0] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[11] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[4]),
                               _mm256_sub_epi32(c[8], c[12]));

      // 344, 208,  -9,-223,     -349,-338,-194,  27,      236, 353, 331, 178,
      // -44,-250,-357,-323,     -163,  62, 262, 359,      315, 147, -79,-274,
      // -361,-306,-130,  97,      285, 362, 296, 114
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_6),
                              _mm256_mullo_epi32(b[1], txmat_1_19));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_31),
                              _mm256_mullo_epi32(b[3], txmat_1_18));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_5),
                              _mm256_mullo_epi32(b[5], txmat_1_7));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_20),
                              _mm256_mullo_epi32(b[7], txmat_1_30));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_17),
                              _mm256_mullo_epi32(b[9], txmat_1_4));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_8),
                              _mm256_mullo_epi32(b[11], txmat_1_21));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_29),
                              _mm256_mullo_epi32(b[13], txmat_1_16));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_3),
                              _mm256_mullo_epi32(b[15], txmat_1_9));
      c[8] = _mm256_sub_epi32(_mm256_mullo_epi32(b[16], txmat_1_22),
                              _mm256_mullo_epi32(b[17], txmat_1_28));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_15),
                              _mm256_mullo_epi32(b[19], txmat_1_2));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_10),
                               _mm256_mullo_epi32(b[21], txmat_1_23));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_27),
                               _mm256_mullo_epi32(b[23], txmat_1_14));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_1),
                               _mm256_mullo_epi32(b[25], txmat_1_11));
      c[13] = _mm256_sub_epi32(_mm256_mullo_epi32(b[26], txmat_1_24),
                               _mm256_mullo_epi32(b[27], txmat_1_26));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_13),
                               _mm256_mullo_epi32(b[29], txmat_1_0));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_12),
                               _mm256_mullo_epi32(b[31], txmat_1_25));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[13] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      // 338, 163, -97,-306,     -357,-223,  27, 262,      362, 274,  44,-208,
      // -353,-315,-114, 147,      331, 344, 178, -79,     -296,-359,-236,   9,
      // 250, 361, 285,  62,     -194,-349,-323,-130
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_7),
                              _mm256_mullo_epi32(b[1], txmat_1_22));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_26),
                              _mm256_mullo_epi32(b[3], txmat_1_11));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_3),
                              _mm256_mullo_epi32(b[5], txmat_1_18));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_30),
                              _mm256_mullo_epi32(b[7], txmat_1_15));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_0),
                              _mm256_mullo_epi32(b[9], txmat_1_14));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_29),
                              _mm256_mullo_epi32(b[11], txmat_1_19));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_4),
                              _mm256_mullo_epi32(b[13], txmat_1_10));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_25),
                              _mm256_mullo_epi32(b[15], txmat_1_23));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_8),
                              _mm256_mullo_epi32(b[17], txmat_1_6));
      c[9] = _mm256_sub_epi32(_mm256_mullo_epi32(b[18], txmat_1_21),
                              _mm256_mullo_epi32(b[19], txmat_1_27));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_12),
                               _mm256_mullo_epi32(b[21], txmat_1_2));
      c[11] = _mm256_sub_epi32(_mm256_mullo_epi32(b[22], txmat_1_17),
                               _mm256_mullo_epi32(b[23], txmat_1_31));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_16),
                               _mm256_mullo_epi32(b[25], txmat_1_1));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_13),
                               _mm256_mullo_epi32(b[27], txmat_1_28));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_20),
                               _mm256_mullo_epi32(b[29], txmat_1_5));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_9),
                               _mm256_mullo_epi32(b[31], txmat_1_24));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[15] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      // 331, 114,-178,-353,     -296, -44, 236, 362,      250, -27,-285,-357,
      // -194,  97, 323, 338,      130,-163,-349,-306,      -62, 223, 361, 262,
      // -9,-274,-359,-208,       79, 315, 344, 147
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_8),
                              _mm256_mullo_epi32(b[1], txmat_1_25));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_21),
                              _mm256_mullo_epi32(b[3], txmat_1_4));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_12),
                              _mm256_mullo_epi32(b[5], txmat_1_29));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_17),
                              _mm256_mullo_epi32(b[7], txmat_1_0));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_16),
                              _mm256_mullo_epi32(b[9], txmat_1_30));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_13),
                              _mm256_mullo_epi32(b[11], txmat_1_3));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_20),
                              _mm256_mullo_epi32(b[13], txmat_1_26));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_9),
                              _mm256_mullo_epi32(b[15], txmat_1_7));
      c[8] = _mm256_sub_epi32(_mm256_mullo_epi32(b[16], txmat_1_24),
                              _mm256_mullo_epi32(b[17], txmat_1_22));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_5),
                              _mm256_mullo_epi32(b[19], txmat_1_11));
      c[10] = _mm256_sub_epi32(_mm256_mullo_epi32(b[20], txmat_1_28),
                               _mm256_mullo_epi32(b[21], txmat_1_18));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_1),
                               _mm256_mullo_epi32(b[23], txmat_1_15));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_31),
                               _mm256_mullo_epi32(b[25], txmat_1_14));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_2),
                               _mm256_mullo_epi32(b[27], txmat_1_19));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_27),
                               _mm256_mullo_epi32(b[29], txmat_1_10));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_6),
                               _mm256_mullo_epi32(b[31], txmat_1_23));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_add_epi32(c[14], c[15]));
      a[17] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_sub_epi32(c[8], c[12]));

      // 323,  62,-250,-359,     -178, 147, 353, 274,      -27,-306,-338, -97,
      // 223, 362, 208,-114,     -344,-296,  -9, 285,      349, 130,-194,-361,
      // -236,  79, 331, 315,       44,-262,-357,-163
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_9),
                              _mm256_mullo_epi32(b[1], txmat_1_28));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_16),
                              _mm256_mullo_epi32(b[3], txmat_1_2));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_21),
                              _mm256_mullo_epi32(b[5], txmat_1_23));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_4),
                              _mm256_mullo_epi32(b[7], txmat_1_14));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_30),
                              _mm256_mullo_epi32(b[9], txmat_1_11));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_7),
                              _mm256_mullo_epi32(b[11], txmat_1_26));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_18),
                              _mm256_mullo_epi32(b[13], txmat_1_0));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_19),
                              _mm256_mullo_epi32(b[15], txmat_1_25));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_6),
                              _mm256_mullo_epi32(b[17], txmat_1_12));
      c[9] = _mm256_sub_epi32(_mm256_mullo_epi32(b[18], txmat_1_31),
                              _mm256_mullo_epi32(b[19], txmat_1_13));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_5),
                               _mm256_mullo_epi32(b[21], txmat_1_24));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_20),
                               _mm256_mullo_epi32(b[23], txmat_1_1));
      c[12] = _mm256_sub_epi32(_mm256_mullo_epi32(b[24], txmat_1_17),
                               _mm256_mullo_epi32(b[25], txmat_1_27));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_8),
                               _mm256_mullo_epi32(b[27], txmat_1_10));
      c[14] = _mm256_sub_epi32(_mm256_mullo_epi32(b[28], txmat_1_29),
                               _mm256_mullo_epi32(b[29], txmat_1_15));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_3),
                               _mm256_mullo_epi32(b[31], txmat_1_22));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_add_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_add_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[19] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      // 315,   9,-306,-323,      -27, 296, 331,  44,     -285,-338, -62, 274,
      // 344,  79,-262,-349,      -97, 250, 353, 114,     -236,-357,-130, 223,
      // 359, 147,-208,-361,     -163, 194, 362, 178
      c[0] = _mm256_add_epi32(_mm256_mullo_epi32(b[0], txmat_1_10),
                              _mm256_mullo_epi32(b[1], txmat_1_31));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_11),
                              _mm256_mullo_epi32(b[3], txmat_1_9));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_30),
                              _mm256_mullo_epi32(b[5], txmat_1_12));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_8),
                              _mm256_mullo_epi32(b[7], txmat_1_29));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_13),
                              _mm256_mullo_epi32(b[9], txmat_1_7));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_28),
                              _mm256_mullo_epi32(b[11], txmat_1_14));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_6),
                              _mm256_mullo_epi32(b[13], txmat_1_27));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_15),
                              _mm256_mullo_epi32(b[15], txmat_1_5));
      c[8] = _mm256_sub_epi32(_mm256_mullo_epi32(b[16], txmat_1_26),
                              _mm256_mullo_epi32(b[17], txmat_1_16));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_4),
                              _mm256_mullo_epi32(b[19], txmat_1_25));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_17),
                               _mm256_mullo_epi32(b[21], txmat_1_3));
      c[11] = _mm256_sub_epi32(_mm256_mullo_epi32(b[22], txmat_1_24),
                               _mm256_mullo_epi32(b[23], txmat_1_18));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_2),
                               _mm256_mullo_epi32(b[25], txmat_1_23));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_19),
                               _mm256_mullo_epi32(b[27], txmat_1_1));
      c[14] = _mm256_sub_epi32(_mm256_mullo_epi32(b[28], txmat_1_22),
                               _mm256_mullo_epi32(b[29], txmat_1_20));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_0),
                               _mm256_mullo_epi32(b[31], txmat_1_21));
      c[0] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[21] = _mm256_sub_epi32(_mm256_sub_epi32(c[0], c[4]),
                               _mm256_sub_epi32(c[8], c[12]));

      // 306, -44,-344,-250,      130, 361, 178,-208,     -357, -97, 274, 331,
      // 9,-323,-285,  79,      353, 223,-163,-362,     -147, 236, 349,  62,
      // -296,-315,  27, 338,      262,-114,-359,-194
      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_11),
                              _mm256_mullo_epi32(b[1], txmat_1_29));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_6),
                              _mm256_mullo_epi32(b[3], txmat_1_16));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_24),
                              _mm256_mullo_epi32(b[5], txmat_1_1));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_21),
                              _mm256_mullo_epi32(b[7], txmat_1_19));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_3),
                              _mm256_mullo_epi32(b[9], txmat_1_26));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_14),
                              _mm256_mullo_epi32(b[11], txmat_1_8));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_31),
                              _mm256_mullo_epi32(b[13], txmat_1_9));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_13),
                              _mm256_mullo_epi32(b[15], txmat_1_27));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_4),
                              _mm256_mullo_epi32(b[17], txmat_1_18));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_22),
                              _mm256_mullo_epi32(b[19], txmat_1_0));
      c[10] = _mm256_sub_epi32(_mm256_mullo_epi32(b[20], txmat_1_23),
                               _mm256_mullo_epi32(b[21], txmat_1_17));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_5),
                               _mm256_mullo_epi32(b[23], txmat_1_28));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_12),
                               _mm256_mullo_epi32(b[25], txmat_1_10));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_30),
                               _mm256_mullo_epi32(b[27], txmat_1_7));
      c[14] = _mm256_sub_epi32(_mm256_mullo_epi32(b[28], txmat_1_15),
                               _mm256_mullo_epi32(b[29], txmat_1_25));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_2),
                               _mm256_mullo_epi32(b[31], txmat_1_20));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_add_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_sub_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[23] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[4]),
                               _mm256_sub_epi32(c[8], c[12]));

      // 296, -97,-361,-147,      262, 323, -44,-353,     -194, 223, 344,   9,
      // -338,-236, 178, 357,       62,-315,-274, 130,      362, 114,-285,-306,
      // 79, 359, 163,-250,     -331,  27, 349, 208
      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_12),
                              _mm256_mullo_epi32(b[1], txmat_1_26));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_1),
                              _mm256_mullo_epi32(b[3], txmat_1_23));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_15),
                              _mm256_mullo_epi32(b[5], txmat_1_9));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_29),
                              _mm256_mullo_epi32(b[7], txmat_1_4));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_20),
                              _mm256_mullo_epi32(b[9], txmat_1_18));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_6),
                              _mm256_mullo_epi32(b[11], txmat_1_31));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_7),
                              _mm256_mullo_epi32(b[13], txmat_1_17));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_21),
                              _mm256_mullo_epi32(b[15], txmat_1_3));
      c[8] = _mm256_sub_epi32(_mm256_mullo_epi32(b[16], txmat_1_28),
                              _mm256_mullo_epi32(b[17], txmat_1_10));
      c[9] = _mm256_sub_epi32(_mm256_mullo_epi32(b[18], txmat_1_14),
                              _mm256_mullo_epi32(b[19], txmat_1_24));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_0),
                               _mm256_mullo_epi32(b[21], txmat_1_25));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_13),
                               _mm256_mullo_epi32(b[23], txmat_1_11));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_27),
                               _mm256_mullo_epi32(b[25], txmat_1_2));
      c[13] = _mm256_sub_epi32(_mm256_mullo_epi32(b[26], txmat_1_22),
                               _mm256_mullo_epi32(b[27], txmat_1_16));
      c[14] = _mm256_sub_epi32(_mm256_mullo_epi32(b[28], txmat_1_8),
                               _mm256_mullo_epi32(b[29], txmat_1_30));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_5),
                               _mm256_mullo_epi32(b[31], txmat_1_19));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_sub_epi32(_mm256_add_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[25] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      // 285,-147,-357, -27,      344, 194,-250,-315,       97, 362,  79,-323,
      // -236, 208, 338, -44,     -359,-130, 296, 274,     -163,-353,  -9, 349,
      // 178,-262,-306, 114,      361,  62,-331,-223
      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_13),
                              _mm256_mullo_epi32(b[1], txmat_1_23));
      c[1] = _mm256_add_epi32(_mm256_mullo_epi32(b[2], txmat_1_3),
                              _mm256_mullo_epi32(b[3], txmat_1_30));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_6),
                              _mm256_mullo_epi32(b[5], txmat_1_20));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_16),
                              _mm256_mullo_epi32(b[7], txmat_1_10));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_26),
                              _mm256_mullo_epi32(b[9], txmat_1_0));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_27),
                              _mm256_mullo_epi32(b[11], txmat_1_9));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_17),
                              _mm256_mullo_epi32(b[13], txmat_1_19));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_7),
                              _mm256_mullo_epi32(b[15], txmat_1_29));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_2),
                              _mm256_mullo_epi32(b[17], txmat_1_24));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_12),
                              _mm256_mullo_epi32(b[19], txmat_1_14));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_22),
                               _mm256_mullo_epi32(b[21], txmat_1_4));
      c[11] = _mm256_sub_epi32(_mm256_mullo_epi32(b[22], txmat_1_31),
                               _mm256_mullo_epi32(b[23], txmat_1_5));
      c[12] = _mm256_sub_epi32(_mm256_mullo_epi32(b[24], txmat_1_21),
                               _mm256_mullo_epi32(b[25], txmat_1_15));
      c[13] = _mm256_sub_epi32(_mm256_mullo_epi32(b[26], txmat_1_11),
                               _mm256_mullo_epi32(b[27], txmat_1_25));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_1),
                               _mm256_mullo_epi32(b[29], txmat_1_28));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_8),
                               _mm256_mullo_epi32(b[31], txmat_1_18));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_sub_epi32(_mm256_add_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_add_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[27] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_sub_epi32(c[8], c[12]));

      // 274,-194,-331,  97,      359,   9,-357,-114,      323, 208,-262,-285,
      // 178, 338, -79,-361,      -27, 353, 130,-315,     -223, 250, 296,-163,
      // -344,  62, 362,  44,     -349,-147, 306, 236
      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_14),
                              _mm256_mullo_epi32(b[1], txmat_1_20));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_8),
                              _mm256_mullo_epi32(b[3], txmat_1_26));
      c[2] = _mm256_add_epi32(_mm256_mullo_epi32(b[4], txmat_1_2),
                              _mm256_mullo_epi32(b[5], txmat_1_31));
      c[3] = _mm256_add_epi32(_mm256_mullo_epi32(b[6], txmat_1_3),
                              _mm256_mullo_epi32(b[7], txmat_1_25));
      c[4] = _mm256_add_epi32(_mm256_mullo_epi32(b[8], txmat_1_9),
                              _mm256_mullo_epi32(b[9], txmat_1_19));
      c[5] = _mm256_add_epi32(_mm256_mullo_epi32(b[10], txmat_1_15),
                              _mm256_mullo_epi32(b[11], txmat_1_13));
      c[6] = _mm256_add_epi32(_mm256_mullo_epi32(b[12], txmat_1_21),
                              _mm256_mullo_epi32(b[13], txmat_1_7));
      c[7] = _mm256_add_epi32(_mm256_mullo_epi32(b[14], txmat_1_27),
                              _mm256_mullo_epi32(b[15], txmat_1_1));
      c[8] = _mm256_sub_epi32(_mm256_mullo_epi32(b[16], txmat_1_30),
                              _mm256_mullo_epi32(b[17], txmat_1_4));
      c[9] = _mm256_sub_epi32(_mm256_mullo_epi32(b[18], txmat_1_24),
                              _mm256_mullo_epi32(b[19], txmat_1_10));
      c[10] = _mm256_sub_epi32(_mm256_mullo_epi32(b[20], txmat_1_18),
                               _mm256_mullo_epi32(b[21], txmat_1_16));
      c[11] = _mm256_sub_epi32(_mm256_mullo_epi32(b[22], txmat_1_12),
                               _mm256_mullo_epi32(b[23], txmat_1_22));
      c[12] = _mm256_sub_epi32(_mm256_mullo_epi32(b[24], txmat_1_6),
                               _mm256_mullo_epi32(b[25], txmat_1_28));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_0),
                               _mm256_mullo_epi32(b[27], txmat_1_29));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_5),
                               _mm256_mullo_epi32(b[29], txmat_1_23));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_11),
                               _mm256_mullo_epi32(b[31], txmat_1_17));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[29] = _mm256_sub_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      // 262,-236,-285, 208,      306,-178,-323, 147,      338,-114,-349,  79,
      // 357, -44,-361,   9,      362,  27,-359, -62,      353,  97,-344,-130,
      // 331, 163,-315,-194,      296, 223,-274,-250
      c[0] = _mm256_sub_epi32(_mm256_mullo_epi32(b[0], txmat_1_15),
                              _mm256_mullo_epi32(b[1], txmat_1_17));
      c[1] = _mm256_sub_epi32(_mm256_mullo_epi32(b[2], txmat_1_13),
                              _mm256_mullo_epi32(b[3], txmat_1_19));
      c[2] = _mm256_sub_epi32(_mm256_mullo_epi32(b[4], txmat_1_11),
                              _mm256_mullo_epi32(b[5], txmat_1_21));
      c[3] = _mm256_sub_epi32(_mm256_mullo_epi32(b[6], txmat_1_9),
                              _mm256_mullo_epi32(b[7], txmat_1_23));
      c[4] = _mm256_sub_epi32(_mm256_mullo_epi32(b[8], txmat_1_7),
                              _mm256_mullo_epi32(b[9], txmat_1_25));
      c[5] = _mm256_sub_epi32(_mm256_mullo_epi32(b[10], txmat_1_5),
                              _mm256_mullo_epi32(b[11], txmat_1_27));
      c[6] = _mm256_sub_epi32(_mm256_mullo_epi32(b[12], txmat_1_3),
                              _mm256_mullo_epi32(b[13], txmat_1_29));
      c[7] = _mm256_sub_epi32(_mm256_mullo_epi32(b[14], txmat_1_1),
                              _mm256_mullo_epi32(b[15], txmat_1_31));
      c[8] = _mm256_add_epi32(_mm256_mullo_epi32(b[16], txmat_1_0),
                              _mm256_mullo_epi32(b[17], txmat_1_30));
      c[9] = _mm256_add_epi32(_mm256_mullo_epi32(b[18], txmat_1_2),
                              _mm256_mullo_epi32(b[19], txmat_1_28));
      c[10] = _mm256_add_epi32(_mm256_mullo_epi32(b[20], txmat_1_4),
                               _mm256_mullo_epi32(b[21], txmat_1_26));
      c[11] = _mm256_add_epi32(_mm256_mullo_epi32(b[22], txmat_1_6),
                               _mm256_mullo_epi32(b[23], txmat_1_24));
      c[12] = _mm256_add_epi32(_mm256_mullo_epi32(b[24], txmat_1_8),
                               _mm256_mullo_epi32(b[25], txmat_1_22));
      c[13] = _mm256_add_epi32(_mm256_mullo_epi32(b[26], txmat_1_10),
                               _mm256_mullo_epi32(b[27], txmat_1_20));
      c[14] = _mm256_add_epi32(_mm256_mullo_epi32(b[28], txmat_1_12),
                               _mm256_mullo_epi32(b[29], txmat_1_18));
      c[15] = _mm256_add_epi32(_mm256_mullo_epi32(b[30], txmat_1_14),
                               _mm256_mullo_epi32(b[31], txmat_1_16));
      c[0] = _mm256_add_epi32(_mm256_sub_epi32(c[0], c[1]),
                              _mm256_sub_epi32(c[2], c[3]));
      c[4] = _mm256_add_epi32(_mm256_sub_epi32(c[4], c[5]),
                              _mm256_sub_epi32(c[6], c[7]));
      c[8] = _mm256_add_epi32(_mm256_sub_epi32(c[8], c[9]),
                              _mm256_sub_epi32(c[10], c[11]));
      c[12] = _mm256_add_epi32(_mm256_sub_epi32(c[12], c[13]),
                               _mm256_sub_epi32(c[14], c[15]));
      a[31] = _mm256_add_epi32(_mm256_add_epi32(c[0], c[4]),
                               _mm256_add_epi32(c[8], c[12]));

      for (n = 0; n < 32; n++) {
        a[n] = _mm256_add_epi32(a[n], v_offset);
        a[n] = _mm256_srai_epi32(a[n], shift);
      }

      transpose_store_8x8_avx2(a, dst + 0, tx1d_size);
      transpose_store_8x8_avx2(a + 8, dst + 8, tx1d_size);
      transpose_store_8x8_avx2(a + 16, dst + 16, tx1d_size);
      transpose_store_8x8_avx2(a + 24, dst + 24, tx1d_size);

      src += 8;
      dst += tx1d_size * 8;
    }
  } else {
    __m128i a[32], b[32];
    __m128i c[16], d[16];
    __m128i e[8], f[8];
    __m128i g[4], h[4];
    __m128i i[2], j[2];

    __m128i v_offset = _mm_set1_epi32(offset);

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

    for (n = 0; n < 32; n++) {
      __m128i src0 = _mm_loadu_si128((__m128i *)&src[n * line]);
      __m128i src1 =
          _mm_loadu_si128((__m128i *)&src[(tx1d_size - 1 - n) * line]);
      a[n] = _mm_add_epi32(src0, src1);
      b[n] = _mm_sub_epi32(src0, src1);
    }
    for (n = 0; n < 16; n++) {
      c[n] = _mm_add_epi32(a[n], a[31 - n]);
      d[n] = _mm_sub_epi32(a[n], a[31 - n]);
    }
    for (n = 0; n < 8; n++) {
      e[n] = _mm_add_epi32(c[n], c[15 - n]);
      f[n] = _mm_sub_epi32(c[n], c[15 - n]);
    }
    for (n = 0; n < 4; n++) {
      g[n] = _mm_add_epi32(e[n], e[7 - n]);
      h[n] = _mm_sub_epi32(e[n], e[7 - n]);
    }
    i[0] = _mm_add_epi32(g[0], g[3]);
    j[0] = _mm_sub_epi32(g[0], g[3]);
    i[1] = _mm_add_epi32(g[1], g[2]);
    j[1] = _mm_sub_epi32(g[1], g[2]);

    a[0] = _mm_slli_epi32(_mm_add_epi32(i[0], i[1]), 6);

    a[16] = _mm_add_epi32(_mm_mullo_epi32(j[0], txmat_16_0),
                          _mm_mullo_epi32(j[1], txmat_16_1));

    g[0] = _mm_mullo_epi32(h[0], txmat_8_0);
    g[1] = _mm_mullo_epi32(h[1], txmat_8_1);
    g[2] = _mm_mullo_epi32(h[2], txmat_8_2);
    g[3] = _mm_mullo_epi32(h[3], txmat_8_3);
    a[8] = _mm_add_epi32(_mm_add_epi32(g[0], g[1]), _mm_add_epi32(g[2], g[3]));

    g[0] = _mm_mullo_epi32(h[0], txmat_8_1);
    g[1] = _mm_mullo_epi32(h[1], txmat_8_3);
    g[2] = _mm_mullo_epi32(h[2], txmat_8_0);
    g[3] = _mm_mullo_epi32(h[3], txmat_8_2);
    a[24] = _mm_sub_epi32(_mm_sub_epi32(g[0], g[1]), _mm_add_epi32(g[2], g[3]));

    e[0] = _mm_mullo_epi32(f[0], txmat_4_0);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_1);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_2);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_3);
    e[4] = _mm_mullo_epi32(f[4], txmat_4_4);
    e[5] = _mm_mullo_epi32(f[5], txmat_4_5);
    e[6] = _mm_mullo_epi32(f[6], txmat_4_6);
    e[7] = _mm_mullo_epi32(f[7], txmat_4_7);
    e[0] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[4] = _mm_add_epi32(e[0], e[4]);

    e[0] = _mm_mullo_epi32(f[0], txmat_4_1);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_4);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_7);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_5);
    e[4] = _mm_mullo_epi32(f[4], txmat_4_2);
    e[5] = _mm_mullo_epi32(f[5], txmat_4_0);
    e[6] = _mm_mullo_epi32(f[6], txmat_4_3);
    e[7] = _mm_mullo_epi32(f[7], txmat_4_6);
    e[0] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[12] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_mullo_epi32(f[0], txmat_4_2);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_7);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_3);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_1);
    e[4] = _mm_mullo_epi32(f[4], txmat_4_6);
    e[5] = _mm_mullo_epi32(f[5], txmat_4_4);
    e[6] = _mm_mullo_epi32(f[6], txmat_4_0);
    e[7] = _mm_mullo_epi32(f[7], txmat_4_5);
    e[0] = _mm_sub_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_sub_epi32(_mm_sub_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[20] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_mullo_epi32(f[0], txmat_4_3);
    e[1] = _mm_mullo_epi32(f[1], txmat_4_5);
    e[2] = _mm_mullo_epi32(f[2], txmat_4_1);
    e[3] = _mm_mullo_epi32(f[3], txmat_4_7);
    e[4] = _mm_mullo_epi32(f[4], txmat_4_0);
    e[5] = _mm_mullo_epi32(f[5], txmat_4_6);
    e[6] = _mm_mullo_epi32(f[6], txmat_4_2);
    e[7] = _mm_mullo_epi32(f[7], txmat_4_4);
    e[0] = _mm_sub_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_sub_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[28] = _mm_add_epi32(e[0], e[4]);

    e[0] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_0),
                         _mm_mullo_epi32(d[1], txmat_2_1));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_2),
                         _mm_mullo_epi32(d[3], txmat_2_3));
    e[2] = _mm_add_epi32(_mm_mullo_epi32(d[4], txmat_2_4),
                         _mm_mullo_epi32(d[5], txmat_2_5));
    e[3] = _mm_add_epi32(_mm_mullo_epi32(d[6], txmat_2_6),
                         _mm_mullo_epi32(d[7], txmat_2_7));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_8),
                         _mm_mullo_epi32(d[9], txmat_2_9));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_10),
                         _mm_mullo_epi32(d[11], txmat_2_11));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_12),
                         _mm_mullo_epi32(d[13], txmat_2_13));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_14),
                         _mm_mullo_epi32(d[15], txmat_2_15));
    e[0] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[2] = _mm_add_epi32(e[0], e[4]);

    e[0] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_1),
                         _mm_mullo_epi32(d[1], txmat_2_4));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_7),
                         _mm_mullo_epi32(d[3], txmat_2_10));
    e[2] = _mm_sub_epi32(_mm_mullo_epi32(d[4], txmat_2_13),
                         _mm_mullo_epi32(d[5], txmat_2_15));
    e[3] = _mm_add_epi32(_mm_mullo_epi32(d[6], txmat_2_12),
                         _mm_mullo_epi32(d[7], txmat_2_9));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_6),
                         _mm_mullo_epi32(d[9], txmat_2_3));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_0),
                         _mm_mullo_epi32(d[11], txmat_2_2));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_5),
                         _mm_mullo_epi32(d[13], txmat_2_8));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_11),
                         _mm_mullo_epi32(d[15], txmat_2_14));
    e[0] = _mm_add_epi32(_mm_add_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[6] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_2),
                         _mm_mullo_epi32(d[1], txmat_2_7));
    e[1] = _mm_sub_epi32(_mm_mullo_epi32(d[2], txmat_2_12),
                         _mm_mullo_epi32(d[3], txmat_2_14));
    e[2] = _mm_add_epi32(_mm_mullo_epi32(d[4], txmat_2_9),
                         _mm_mullo_epi32(d[5], txmat_2_4));
    e[3] = _mm_add_epi32(_mm_mullo_epi32(d[6], txmat_2_0),
                         _mm_mullo_epi32(d[7], txmat_2_5));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_10),
                         _mm_mullo_epi32(d[9], txmat_2_15));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_11),
                         _mm_mullo_epi32(d[11], txmat_2_6));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_1),
                         _mm_mullo_epi32(d[13], txmat_2_3));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_8),
                         _mm_mullo_epi32(d[15], txmat_2_13));
    e[0] = _mm_sub_epi32(_mm_add_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_sub_epi32(_mm_sub_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[10] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_3),
                         _mm_mullo_epi32(d[1], txmat_2_10));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_14),
                         _mm_mullo_epi32(d[3], txmat_2_7));
    e[2] = _mm_add_epi32(_mm_mullo_epi32(d[4], txmat_2_0),
                         _mm_mullo_epi32(d[5], txmat_2_6));
    e[3] = _mm_sub_epi32(_mm_mullo_epi32(d[6], txmat_2_13),
                         _mm_mullo_epi32(d[7], txmat_2_11));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_4),
                         _mm_mullo_epi32(d[9], txmat_2_2));
    e[5] = _mm_sub_epi32(_mm_mullo_epi32(d[10], txmat_2_9),
                         _mm_mullo_epi32(d[11], txmat_2_15));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_8),
                         _mm_mullo_epi32(d[13], txmat_2_1));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_5),
                         _mm_mullo_epi32(d[15], txmat_2_12));
    e[0] = _mm_sub_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_sub_epi32(_mm_add_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[14] = _mm_add_epi32(e[0], e[4]);

    e[0] = _mm_add_epi32(_mm_mullo_epi32(d[0], txmat_2_4),
                         _mm_mullo_epi32(d[1], txmat_2_13));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_9),
                         _mm_mullo_epi32(d[3], txmat_2_0));
    e[2] = _mm_sub_epi32(_mm_mullo_epi32(d[4], txmat_2_8),
                         _mm_mullo_epi32(d[5], txmat_2_14));
    e[3] = _mm_add_epi32(_mm_mullo_epi32(d[6], txmat_2_5),
                         _mm_mullo_epi32(d[7], txmat_2_3));
    e[4] = _mm_sub_epi32(_mm_mullo_epi32(d[8], txmat_2_12),
                         _mm_mullo_epi32(d[9], txmat_2_10));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_1),
                         _mm_mullo_epi32(d[11], txmat_2_7));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_15),
                         _mm_mullo_epi32(d[13], txmat_2_6));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_2),
                         _mm_mullo_epi32(d[15], txmat_2_11));
    e[0] = _mm_sub_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_sub_epi32(e[4], e[5]), _mm_add_epi32(e[6], e[7]));
    a[18] = _mm_add_epi32(e[0], e[4]);

    e[0] = _mm_sub_epi32(_mm_mullo_epi32(d[0], txmat_2_5),
                         _mm_mullo_epi32(d[1], txmat_2_15));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_4),
                         _mm_mullo_epi32(d[3], txmat_2_6));
    e[2] = _mm_add_epi32(_mm_mullo_epi32(d[4], txmat_2_14),
                         _mm_mullo_epi32(d[5], txmat_2_3));
    e[3] = _mm_sub_epi32(_mm_mullo_epi32(d[6], txmat_2_7),
                         _mm_mullo_epi32(d[7], txmat_2_13));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_2),
                         _mm_mullo_epi32(d[9], txmat_2_8));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_12),
                         _mm_mullo_epi32(d[11], txmat_2_1));
    e[6] = _mm_sub_epi32(_mm_mullo_epi32(d[12], txmat_2_9),
                         _mm_mullo_epi32(d[13], txmat_2_11));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_0),
                         _mm_mullo_epi32(d[15], txmat_2_10));
    e[0] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_add_epi32(e[2], e[3]));
    e[4] = _mm_sub_epi32(_mm_sub_epi32(e[4], e[5]), _mm_sub_epi32(e[6], e[7]));
    a[22] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_sub_epi32(_mm_mullo_epi32(d[0], txmat_2_6),
                         _mm_mullo_epi32(d[1], txmat_2_12));
    e[1] = _mm_add_epi32(_mm_mullo_epi32(d[2], txmat_2_0),
                         _mm_mullo_epi32(d[3], txmat_2_13));
    e[2] = _mm_add_epi32(_mm_mullo_epi32(d[4], txmat_2_5),
                         _mm_mullo_epi32(d[5], txmat_2_7));
    e[3] = _mm_add_epi32(_mm_mullo_epi32(d[6], txmat_2_11),
                         _mm_mullo_epi32(d[7], txmat_2_1));
    e[4] = _mm_sub_epi32(_mm_mullo_epi32(d[8], txmat_2_14),
                         _mm_mullo_epi32(d[9], txmat_2_4));
    e[5] = _mm_sub_epi32(_mm_mullo_epi32(d[10], txmat_2_8),
                         _mm_mullo_epi32(d[11], txmat_2_10));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_2),
                         _mm_mullo_epi32(d[13], txmat_2_15));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_3),
                         _mm_mullo_epi32(d[15], txmat_2_9));
    e[0] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_sub_epi32(e[4], e[5]), _mm_sub_epi32(e[6], e[7]));
    a[26] = _mm_sub_epi32(e[0], e[4]);

    e[0] = _mm_sub_epi32(_mm_mullo_epi32(d[0], txmat_2_7),
                         _mm_mullo_epi32(d[1], txmat_2_9));
    e[1] = _mm_sub_epi32(_mm_mullo_epi32(d[2], txmat_2_5),
                         _mm_mullo_epi32(d[3], txmat_2_11));
    e[2] = _mm_sub_epi32(_mm_mullo_epi32(d[4], txmat_2_3),
                         _mm_mullo_epi32(d[5], txmat_2_13));
    e[3] = _mm_sub_epi32(_mm_mullo_epi32(d[6], txmat_2_1),
                         _mm_mullo_epi32(d[7], txmat_2_15));
    e[4] = _mm_add_epi32(_mm_mullo_epi32(d[8], txmat_2_0),
                         _mm_mullo_epi32(d[9], txmat_2_14));
    e[5] = _mm_add_epi32(_mm_mullo_epi32(d[10], txmat_2_2),
                         _mm_mullo_epi32(d[11], txmat_2_12));
    e[6] = _mm_add_epi32(_mm_mullo_epi32(d[12], txmat_2_4),
                         _mm_mullo_epi32(d[13], txmat_2_10));
    e[7] = _mm_add_epi32(_mm_mullo_epi32(d[14], txmat_2_6),
                         _mm_mullo_epi32(d[15], txmat_2_8));
    e[0] = _mm_add_epi32(_mm_sub_epi32(e[0], e[1]), _mm_sub_epi32(e[2], e[3]));
    e[4] = _mm_add_epi32(_mm_sub_epi32(e[4], e[5]), _mm_sub_epi32(e[6], e[7]));
    a[30] = _mm_add_epi32(e[0], e[4]);

    // 362, 361, 359, 357,      353, 349, 344, 338,      331, 323, 315, 306,
    // 296, 285, 274, 262,      250, 236, 223, 208,      194, 178, 163, 147,
    // 130, 114,  97,  79,       62,  44,  27,   9
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_0),
                         _mm_mullo_epi32(b[1], txmat_1_1));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_2),
                         _mm_mullo_epi32(b[3], txmat_1_3));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_4),
                         _mm_mullo_epi32(b[5], txmat_1_5));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_6),
                         _mm_mullo_epi32(b[7], txmat_1_7));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_8),
                         _mm_mullo_epi32(b[9], txmat_1_9));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_10),
                         _mm_mullo_epi32(b[11], txmat_1_11));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_12),
                         _mm_mullo_epi32(b[13], txmat_1_13));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_14),
                         _mm_mullo_epi32(b[15], txmat_1_15));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_16),
                         _mm_mullo_epi32(b[17], txmat_1_17));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_18),
                         _mm_mullo_epi32(b[19], txmat_1_19));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_20),
                          _mm_mullo_epi32(b[21], txmat_1_21));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_22),
                          _mm_mullo_epi32(b[23], txmat_1_23));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_24),
                          _mm_mullo_epi32(b[25], txmat_1_25));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_26),
                          _mm_mullo_epi32(b[27], txmat_1_27));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_28),
                          _mm_mullo_epi32(b[29], txmat_1_29));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_30),
                          _mm_mullo_epi32(b[31], txmat_1_31));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[1] = _mm_add_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 361, 353, 338, 315,      285, 250, 208, 163,      114,  62,   9, -44,
    // -97,-147,-194,-236,     -274,-306,-331,-349,     -359,-362,-357,-344,
    // -323,-296,-262,-223,     -178,-130, -79, -27
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_1),
                         _mm_mullo_epi32(b[1], txmat_1_4));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_7),
                         _mm_mullo_epi32(b[3], txmat_1_10));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_13),
                         _mm_mullo_epi32(b[5], txmat_1_16));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_19),
                         _mm_mullo_epi32(b[7], txmat_1_22));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_25),
                         _mm_mullo_epi32(b[9], txmat_1_28));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_31),
                         _mm_mullo_epi32(b[11], txmat_1_29));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_26),
                         _mm_mullo_epi32(b[13], txmat_1_23));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_20),
                         _mm_mullo_epi32(b[15], txmat_1_17));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_14),
                         _mm_mullo_epi32(b[17], txmat_1_11));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_8),
                         _mm_mullo_epi32(b[19], txmat_1_5));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_2),
                          _mm_mullo_epi32(b[21], txmat_1_0));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_3),
                          _mm_mullo_epi32(b[23], txmat_1_6));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_9),
                          _mm_mullo_epi32(b[25], txmat_1_12));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_15),
                          _mm_mullo_epi32(b[27], txmat_1_18));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_21),
                          _mm_mullo_epi32(b[29], txmat_1_24));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_27),
                          _mm_mullo_epi32(b[31], txmat_1_30));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[3] = _mm_sub_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 359, 338, 296, 236,      163,  79,  -9, -97,     -178,-250,-306,-344,
    // -361,-357,-331,-285,     -223,-147, -62,  27,      114, 194, 262, 315,
    // 349, 362, 353, 323,      274, 208, 130,  44
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_2),
                         _mm_mullo_epi32(b[1], txmat_1_7));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_12),
                         _mm_mullo_epi32(b[3], txmat_1_17));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_22),
                         _mm_mullo_epi32(b[5], txmat_1_27));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_31),
                         _mm_mullo_epi32(b[7], txmat_1_26));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_21),
                         _mm_mullo_epi32(b[9], txmat_1_16));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_11),
                         _mm_mullo_epi32(b[11], txmat_1_6));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_1),
                         _mm_mullo_epi32(b[13], txmat_1_3));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_8),
                         _mm_mullo_epi32(b[15], txmat_1_13));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_18),
                         _mm_mullo_epi32(b[17], txmat_1_23));
    c[9] = _mm_sub_epi32(_mm_mullo_epi32(b[18], txmat_1_28),
                         _mm_mullo_epi32(b[19], txmat_1_30));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_25),
                          _mm_mullo_epi32(b[21], txmat_1_20));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_15),
                          _mm_mullo_epi32(b[23], txmat_1_10));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_5),
                          _mm_mullo_epi32(b[25], txmat_1_0));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_4),
                          _mm_mullo_epi32(b[27], txmat_1_9));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_14),
                          _mm_mullo_epi32(b[29], txmat_1_19));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_24),
                          _mm_mullo_epi32(b[31], txmat_1_29));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[5] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 357, 315, 236, 130,        9,-114,-223,-306,     -353,-359,-323,-250,
    // -147, -27,  97, 208,      296, 349, 361, 331,      262, 163,  44, -79,
    // -194,-285,-344,-362,     -338,-274,-178, -62
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_3),
                         _mm_mullo_epi32(b[1], txmat_1_10));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_17),
                         _mm_mullo_epi32(b[3], txmat_1_24));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_31),
                         _mm_mullo_epi32(b[5], txmat_1_25));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_18),
                         _mm_mullo_epi32(b[7], txmat_1_11));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_4),
                         _mm_mullo_epi32(b[9], txmat_1_2));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_9),
                         _mm_mullo_epi32(b[11], txmat_1_16));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_23),
                         _mm_mullo_epi32(b[13], txmat_1_30));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_26),
                         _mm_mullo_epi32(b[15], txmat_1_19));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_12),
                         _mm_mullo_epi32(b[17], txmat_1_5));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_1),
                         _mm_mullo_epi32(b[19], txmat_1_8));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_15),
                          _mm_mullo_epi32(b[21], txmat_1_22));
    c[11] = _mm_sub_epi32(_mm_mullo_epi32(b[22], txmat_1_29),
                          _mm_mullo_epi32(b[23], txmat_1_27));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_20),
                          _mm_mullo_epi32(b[25], txmat_1_13));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_6),
                          _mm_mullo_epi32(b[27], txmat_1_0));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_7),
                          _mm_mullo_epi32(b[29], txmat_1_14));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_21),
                          _mm_mullo_epi32(b[31], txmat_1_28));
    c[0] = _mm_add_epi32(_mm_add_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[7] = _mm_add_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 353, 285, 163,   9,     -147,-274,-349,-357,     -296,-178, -27, 130,
    // 262, 344, 359, 306,      194,  44,-114,-250,     -338,-361,-315,-208,
    // -62,  97, 236, 331,      362, 323, 223,  79
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_4),
                         _mm_mullo_epi32(b[1], txmat_1_13));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_22),
                         _mm_mullo_epi32(b[3], txmat_1_31));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_23),
                         _mm_mullo_epi32(b[5], txmat_1_14));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_5),
                         _mm_mullo_epi32(b[7], txmat_1_3));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_12),
                         _mm_mullo_epi32(b[9], txmat_1_21));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_30),
                         _mm_mullo_epi32(b[11], txmat_1_24));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_15),
                         _mm_mullo_epi32(b[13], txmat_1_6));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_2),
                         _mm_mullo_epi32(b[15], txmat_1_11));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_20),
                         _mm_mullo_epi32(b[17], txmat_1_29));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_25),
                         _mm_mullo_epi32(b[19], txmat_1_16));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_7),
                          _mm_mullo_epi32(b[21], txmat_1_1));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_10),
                          _mm_mullo_epi32(b[23], txmat_1_19));
    c[12] = _mm_sub_epi32(_mm_mullo_epi32(b[24], txmat_1_28),
                          _mm_mullo_epi32(b[25], txmat_1_26));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_17),
                          _mm_mullo_epi32(b[27], txmat_1_8));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_0),
                          _mm_mullo_epi32(b[29], txmat_1_9));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_18),
                          _mm_mullo_epi32(b[31], txmat_1_27));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_sub_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_sub_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[9] = _mm_add_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 349, 250,  79,-114,     -274,-357,-338,-223,      -44, 147, 296, 361,
    // 323, 194,   9,-178,     -315,-362,-306,-163,       27, 208, 331, 359,
    // 285, 130, -62,-236,     -344,-353,-262, -97
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_5),
                         _mm_mullo_epi32(b[1], txmat_1_16));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_27),
                         _mm_mullo_epi32(b[3], txmat_1_25));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_14),
                         _mm_mullo_epi32(b[5], txmat_1_3));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_7),
                         _mm_mullo_epi32(b[7], txmat_1_18));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_29),
                         _mm_mullo_epi32(b[9], txmat_1_23));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_12),
                         _mm_mullo_epi32(b[11], txmat_1_1));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_9),
                         _mm_mullo_epi32(b[13], txmat_1_20));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_31),
                         _mm_mullo_epi32(b[15], txmat_1_21));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_10),
                         _mm_mullo_epi32(b[17], txmat_1_0));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_11),
                         _mm_mullo_epi32(b[19], txmat_1_22));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_30),
                          _mm_mullo_epi32(b[21], txmat_1_19));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_8),
                          _mm_mullo_epi32(b[23], txmat_1_2));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_13),
                          _mm_mullo_epi32(b[25], txmat_1_24));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_28),
                          _mm_mullo_epi32(b[27], txmat_1_17));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_6),
                          _mm_mullo_epi32(b[29], txmat_1_4));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_15),
                          _mm_mullo_epi32(b[31], txmat_1_26));
    c[0] = _mm_sub_epi32(_mm_add_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_sub_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[11] =
        _mm_sub_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 344, 208,  -9,-223,     -349,-338,-194,  27,      236, 353, 331, 178,
    // -44,-250,-357,-323,     -163,  62, 262, 359,      315, 147, -79,-274,
    // -361,-306,-130,  97,      285, 362, 296, 114
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_6),
                         _mm_mullo_epi32(b[1], txmat_1_19));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_31),
                         _mm_mullo_epi32(b[3], txmat_1_18));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_5),
                         _mm_mullo_epi32(b[5], txmat_1_7));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_20),
                         _mm_mullo_epi32(b[7], txmat_1_30));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_17),
                         _mm_mullo_epi32(b[9], txmat_1_4));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_8),
                         _mm_mullo_epi32(b[11], txmat_1_21));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_29),
                         _mm_mullo_epi32(b[13], txmat_1_16));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_3),
                         _mm_mullo_epi32(b[15], txmat_1_9));
    c[8] = _mm_sub_epi32(_mm_mullo_epi32(b[16], txmat_1_22),
                         _mm_mullo_epi32(b[17], txmat_1_28));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_15),
                         _mm_mullo_epi32(b[19], txmat_1_2));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_10),
                          _mm_mullo_epi32(b[21], txmat_1_23));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_27),
                          _mm_mullo_epi32(b[23], txmat_1_14));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_1),
                          _mm_mullo_epi32(b[25], txmat_1_11));
    c[13] = _mm_sub_epi32(_mm_mullo_epi32(b[26], txmat_1_24),
                          _mm_mullo_epi32(b[27], txmat_1_26));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_13),
                          _mm_mullo_epi32(b[29], txmat_1_0));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_12),
                          _mm_mullo_epi32(b[31], txmat_1_25));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[13] =
        _mm_sub_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 338, 163, -97,-306,     -357,-223,  27, 262,      362, 274,  44,-208,
    // -353,-315,-114, 147,      331, 344, 178, -79,     -296,-359,-236,   9,
    // 250, 361, 285,  62,     -194,-349,-323,-130
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_7),
                         _mm_mullo_epi32(b[1], txmat_1_22));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_26),
                         _mm_mullo_epi32(b[3], txmat_1_11));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_3),
                         _mm_mullo_epi32(b[5], txmat_1_18));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_30),
                         _mm_mullo_epi32(b[7], txmat_1_15));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_0),
                         _mm_mullo_epi32(b[9], txmat_1_14));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_29),
                         _mm_mullo_epi32(b[11], txmat_1_19));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_4),
                         _mm_mullo_epi32(b[13], txmat_1_10));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_25),
                         _mm_mullo_epi32(b[15], txmat_1_23));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_8),
                         _mm_mullo_epi32(b[17], txmat_1_6));
    c[9] = _mm_sub_epi32(_mm_mullo_epi32(b[18], txmat_1_21),
                         _mm_mullo_epi32(b[19], txmat_1_27));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_12),
                          _mm_mullo_epi32(b[21], txmat_1_2));
    c[11] = _mm_sub_epi32(_mm_mullo_epi32(b[22], txmat_1_17),
                          _mm_mullo_epi32(b[23], txmat_1_31));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_16),
                          _mm_mullo_epi32(b[25], txmat_1_1));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_13),
                          _mm_mullo_epi32(b[27], txmat_1_28));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_20),
                          _mm_mullo_epi32(b[29], txmat_1_5));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_9),
                          _mm_mullo_epi32(b[31], txmat_1_24));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_add_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[15] =
        _mm_add_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 331, 114,-178,-353,     -296, -44, 236, 362,      250, -27,-285,-357,
    // -194,  97, 323, 338,      130,-163,-349,-306,      -62, 223, 361, 262,
    // -9,-274,-359,-208,       79, 315, 344, 147
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_8),
                         _mm_mullo_epi32(b[1], txmat_1_25));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_21),
                         _mm_mullo_epi32(b[3], txmat_1_4));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_12),
                         _mm_mullo_epi32(b[5], txmat_1_29));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_17),
                         _mm_mullo_epi32(b[7], txmat_1_0));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_16),
                         _mm_mullo_epi32(b[9], txmat_1_30));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_13),
                         _mm_mullo_epi32(b[11], txmat_1_3));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_20),
                         _mm_mullo_epi32(b[13], txmat_1_26));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_9),
                         _mm_mullo_epi32(b[15], txmat_1_7));
    c[8] = _mm_sub_epi32(_mm_mullo_epi32(b[16], txmat_1_24),
                         _mm_mullo_epi32(b[17], txmat_1_22));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_5),
                         _mm_mullo_epi32(b[19], txmat_1_11));
    c[10] = _mm_sub_epi32(_mm_mullo_epi32(b[20], txmat_1_28),
                          _mm_mullo_epi32(b[21], txmat_1_18));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_1),
                          _mm_mullo_epi32(b[23], txmat_1_15));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_31),
                          _mm_mullo_epi32(b[25], txmat_1_14));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_2),
                          _mm_mullo_epi32(b[27], txmat_1_19));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_27),
                          _mm_mullo_epi32(b[29], txmat_1_10));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_6),
                          _mm_mullo_epi32(b[31], txmat_1_23));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_add_epi32(c[12], c[13]), _mm_add_epi32(c[14], c[15]));
    a[17] =
        _mm_add_epi32(_mm_add_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 323,  62,-250,-359,     -178, 147, 353, 274,      -27,-306,-338, -97,
    // 223, 362, 208,-114,     -344,-296,  -9, 285,      349, 130,-194,-361,
    // -236,  79, 331, 315,       44,-262,-357,-163
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_9),
                         _mm_mullo_epi32(b[1], txmat_1_28));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_16),
                         _mm_mullo_epi32(b[3], txmat_1_2));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_21),
                         _mm_mullo_epi32(b[5], txmat_1_23));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_4),
                         _mm_mullo_epi32(b[7], txmat_1_14));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_30),
                         _mm_mullo_epi32(b[9], txmat_1_11));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_7),
                         _mm_mullo_epi32(b[11], txmat_1_26));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_18),
                         _mm_mullo_epi32(b[13], txmat_1_0));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_19),
                         _mm_mullo_epi32(b[15], txmat_1_25));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_6),
                         _mm_mullo_epi32(b[17], txmat_1_12));
    c[9] = _mm_sub_epi32(_mm_mullo_epi32(b[18], txmat_1_31),
                         _mm_mullo_epi32(b[19], txmat_1_13));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_5),
                          _mm_mullo_epi32(b[21], txmat_1_24));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_20),
                          _mm_mullo_epi32(b[23], txmat_1_1));
    c[12] = _mm_sub_epi32(_mm_mullo_epi32(b[24], txmat_1_17),
                          _mm_mullo_epi32(b[25], txmat_1_27));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_8),
                          _mm_mullo_epi32(b[27], txmat_1_10));
    c[14] = _mm_sub_epi32(_mm_mullo_epi32(b[28], txmat_1_29),
                          _mm_mullo_epi32(b[29], txmat_1_15));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_3),
                          _mm_mullo_epi32(b[31], txmat_1_22));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_add_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_add_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[19] =
        _mm_sub_epi32(_mm_sub_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 315,   9,-306,-323,      -27, 296, 331,  44,     -285,-338, -62, 274,
    // 344,  79,-262,-349,      -97, 250, 353, 114,     -236,-357,-130, 223,
    // 359, 147,-208,-361,     -163, 194, 362, 178
    c[0] = _mm_add_epi32(_mm_mullo_epi32(b[0], txmat_1_10),
                         _mm_mullo_epi32(b[1], txmat_1_31));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_11),
                         _mm_mullo_epi32(b[3], txmat_1_9));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_30),
                         _mm_mullo_epi32(b[5], txmat_1_12));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_8),
                         _mm_mullo_epi32(b[7], txmat_1_29));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_13),
                         _mm_mullo_epi32(b[9], txmat_1_7));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_28),
                         _mm_mullo_epi32(b[11], txmat_1_14));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_6),
                         _mm_mullo_epi32(b[13], txmat_1_27));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_15),
                         _mm_mullo_epi32(b[15], txmat_1_5));
    c[8] = _mm_sub_epi32(_mm_mullo_epi32(b[16], txmat_1_26),
                         _mm_mullo_epi32(b[17], txmat_1_16));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_4),
                         _mm_mullo_epi32(b[19], txmat_1_25));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_17),
                          _mm_mullo_epi32(b[21], txmat_1_3));
    c[11] = _mm_sub_epi32(_mm_mullo_epi32(b[22], txmat_1_24),
                          _mm_mullo_epi32(b[23], txmat_1_18));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_2),
                          _mm_mullo_epi32(b[25], txmat_1_23));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_19),
                          _mm_mullo_epi32(b[27], txmat_1_1));
    c[14] = _mm_sub_epi32(_mm_mullo_epi32(b[28], txmat_1_22),
                          _mm_mullo_epi32(b[29], txmat_1_20));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_0),
                          _mm_mullo_epi32(b[31], txmat_1_21));
    c[0] = _mm_sub_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_sub_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[21] =
        _mm_sub_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 306, -44,-344,-250,      130, 361, 178,-208,     -357, -97, 274, 331,
    // 9,-323,-285,  79,      353, 223,-163,-362,     -147, 236, 349,  62,
    // -296,-315,  27, 338,      262,-114,-359,-194
    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_11),
                         _mm_mullo_epi32(b[1], txmat_1_29));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_6),
                         _mm_mullo_epi32(b[3], txmat_1_16));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_24),
                         _mm_mullo_epi32(b[5], txmat_1_1));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_21),
                         _mm_mullo_epi32(b[7], txmat_1_19));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_3),
                         _mm_mullo_epi32(b[9], txmat_1_26));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_14),
                         _mm_mullo_epi32(b[11], txmat_1_8));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_31),
                         _mm_mullo_epi32(b[13], txmat_1_9));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_13),
                         _mm_mullo_epi32(b[15], txmat_1_27));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_4),
                         _mm_mullo_epi32(b[17], txmat_1_18));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_22),
                         _mm_mullo_epi32(b[19], txmat_1_0));
    c[10] = _mm_sub_epi32(_mm_mullo_epi32(b[20], txmat_1_23),
                          _mm_mullo_epi32(b[21], txmat_1_17));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_5),
                          _mm_mullo_epi32(b[23], txmat_1_28));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_12),
                          _mm_mullo_epi32(b[25], txmat_1_10));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_30),
                          _mm_mullo_epi32(b[27], txmat_1_7));
    c[14] = _mm_sub_epi32(_mm_mullo_epi32(b[28], txmat_1_15),
                          _mm_mullo_epi32(b[29], txmat_1_25));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_2),
                          _mm_mullo_epi32(b[31], txmat_1_20));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_add_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_sub_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[23] =
        _mm_add_epi32(_mm_sub_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 296, -97,-361,-147,      262, 323, -44,-353,     -194, 223, 344,   9,
    // -338,-236, 178, 357,       62,-315,-274, 130,      362, 114,-285,-306,
    // 79, 359, 163,-250,     -331,  27, 349, 208
    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_12),
                         _mm_mullo_epi32(b[1], txmat_1_26));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_1),
                         _mm_mullo_epi32(b[3], txmat_1_23));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_15),
                         _mm_mullo_epi32(b[5], txmat_1_9));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_29),
                         _mm_mullo_epi32(b[7], txmat_1_4));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_20),
                         _mm_mullo_epi32(b[9], txmat_1_18));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_6),
                         _mm_mullo_epi32(b[11], txmat_1_31));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_7),
                         _mm_mullo_epi32(b[13], txmat_1_17));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_21),
                         _mm_mullo_epi32(b[15], txmat_1_3));
    c[8] = _mm_sub_epi32(_mm_mullo_epi32(b[16], txmat_1_28),
                         _mm_mullo_epi32(b[17], txmat_1_10));
    c[9] = _mm_sub_epi32(_mm_mullo_epi32(b[18], txmat_1_14),
                         _mm_mullo_epi32(b[19], txmat_1_24));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_0),
                          _mm_mullo_epi32(b[21], txmat_1_25));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_13),
                          _mm_mullo_epi32(b[23], txmat_1_11));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_27),
                          _mm_mullo_epi32(b[25], txmat_1_2));
    c[13] = _mm_sub_epi32(_mm_mullo_epi32(b[26], txmat_1_22),
                          _mm_mullo_epi32(b[27], txmat_1_16));
    c[14] = _mm_sub_epi32(_mm_mullo_epi32(b[28], txmat_1_8),
                          _mm_mullo_epi32(b[29], txmat_1_30));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_5),
                          _mm_mullo_epi32(b[31], txmat_1_19));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_sub_epi32(_mm_add_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[25] =
        _mm_add_epi32(_mm_sub_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 285,-147,-357, -27,      344, 194,-250,-315,       97, 362,  79,-323,
    // -236, 208, 338, -44,     -359,-130, 296, 274,     -163,-353,  -9, 349,
    // 178,-262,-306, 114,      361,  62,-331,-223
    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_13),
                         _mm_mullo_epi32(b[1], txmat_1_23));
    c[1] = _mm_add_epi32(_mm_mullo_epi32(b[2], txmat_1_3),
                         _mm_mullo_epi32(b[3], txmat_1_30));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_6),
                         _mm_mullo_epi32(b[5], txmat_1_20));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_16),
                         _mm_mullo_epi32(b[7], txmat_1_10));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_26),
                         _mm_mullo_epi32(b[9], txmat_1_0));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_27),
                         _mm_mullo_epi32(b[11], txmat_1_9));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_17),
                         _mm_mullo_epi32(b[13], txmat_1_19));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_7),
                         _mm_mullo_epi32(b[15], txmat_1_29));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_2),
                         _mm_mullo_epi32(b[17], txmat_1_24));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_12),
                         _mm_mullo_epi32(b[19], txmat_1_14));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_22),
                          _mm_mullo_epi32(b[21], txmat_1_4));
    c[11] = _mm_sub_epi32(_mm_mullo_epi32(b[22], txmat_1_31),
                          _mm_mullo_epi32(b[23], txmat_1_5));
    c[12] = _mm_sub_epi32(_mm_mullo_epi32(b[24], txmat_1_21),
                          _mm_mullo_epi32(b[25], txmat_1_15));
    c[13] = _mm_sub_epi32(_mm_mullo_epi32(b[26], txmat_1_11),
                          _mm_mullo_epi32(b[27], txmat_1_25));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_1),
                          _mm_mullo_epi32(b[29], txmat_1_28));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_8),
                          _mm_mullo_epi32(b[31], txmat_1_18));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_sub_epi32(_mm_add_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_sub_epi32(c[8], c[9]), _mm_add_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[27] =
        _mm_sub_epi32(_mm_add_epi32(c[0], c[4]), _mm_sub_epi32(c[8], c[12]));

    // 274,-194,-331,  97,      359,   9,-357,-114,      323, 208,-262,-285,
    // 178, 338, -79,-361,      -27, 353, 130,-315,     -223, 250, 296,-163,
    // -344,  62, 362,  44,     -349,-147, 306, 236
    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_14),
                         _mm_mullo_epi32(b[1], txmat_1_20));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_8),
                         _mm_mullo_epi32(b[3], txmat_1_26));
    c[2] = _mm_add_epi32(_mm_mullo_epi32(b[4], txmat_1_2),
                         _mm_mullo_epi32(b[5], txmat_1_31));
    c[3] = _mm_add_epi32(_mm_mullo_epi32(b[6], txmat_1_3),
                         _mm_mullo_epi32(b[7], txmat_1_25));
    c[4] = _mm_add_epi32(_mm_mullo_epi32(b[8], txmat_1_9),
                         _mm_mullo_epi32(b[9], txmat_1_19));
    c[5] = _mm_add_epi32(_mm_mullo_epi32(b[10], txmat_1_15),
                         _mm_mullo_epi32(b[11], txmat_1_13));
    c[6] = _mm_add_epi32(_mm_mullo_epi32(b[12], txmat_1_21),
                         _mm_mullo_epi32(b[13], txmat_1_7));
    c[7] = _mm_add_epi32(_mm_mullo_epi32(b[14], txmat_1_27),
                         _mm_mullo_epi32(b[15], txmat_1_1));
    c[8] = _mm_sub_epi32(_mm_mullo_epi32(b[16], txmat_1_30),
                         _mm_mullo_epi32(b[17], txmat_1_4));
    c[9] = _mm_sub_epi32(_mm_mullo_epi32(b[18], txmat_1_24),
                         _mm_mullo_epi32(b[19], txmat_1_10));
    c[10] = _mm_sub_epi32(_mm_mullo_epi32(b[20], txmat_1_18),
                          _mm_mullo_epi32(b[21], txmat_1_16));
    c[11] = _mm_sub_epi32(_mm_mullo_epi32(b[22], txmat_1_12),
                          _mm_mullo_epi32(b[23], txmat_1_22));
    c[12] = _mm_sub_epi32(_mm_mullo_epi32(b[24], txmat_1_6),
                          _mm_mullo_epi32(b[25], txmat_1_28));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_0),
                          _mm_mullo_epi32(b[27], txmat_1_29));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_5),
                          _mm_mullo_epi32(b[29], txmat_1_23));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_11),
                          _mm_mullo_epi32(b[31], txmat_1_17));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[29] =
        _mm_sub_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    // 262,-236,-285, 208,      306,-178,-323, 147,      338,-114,-349,  79,
    // 357, -44,-361,   9,      362,  27,-359, -62,      353,  97,-344,-130,
    // 331, 163,-315,-194,      296, 223,-274,-250
    c[0] = _mm_sub_epi32(_mm_mullo_epi32(b[0], txmat_1_15),
                         _mm_mullo_epi32(b[1], txmat_1_17));
    c[1] = _mm_sub_epi32(_mm_mullo_epi32(b[2], txmat_1_13),
                         _mm_mullo_epi32(b[3], txmat_1_19));
    c[2] = _mm_sub_epi32(_mm_mullo_epi32(b[4], txmat_1_11),
                         _mm_mullo_epi32(b[5], txmat_1_21));
    c[3] = _mm_sub_epi32(_mm_mullo_epi32(b[6], txmat_1_9),
                         _mm_mullo_epi32(b[7], txmat_1_23));
    c[4] = _mm_sub_epi32(_mm_mullo_epi32(b[8], txmat_1_7),
                         _mm_mullo_epi32(b[9], txmat_1_25));
    c[5] = _mm_sub_epi32(_mm_mullo_epi32(b[10], txmat_1_5),
                         _mm_mullo_epi32(b[11], txmat_1_27));
    c[6] = _mm_sub_epi32(_mm_mullo_epi32(b[12], txmat_1_3),
                         _mm_mullo_epi32(b[13], txmat_1_29));
    c[7] = _mm_sub_epi32(_mm_mullo_epi32(b[14], txmat_1_1),
                         _mm_mullo_epi32(b[15], txmat_1_31));
    c[8] = _mm_add_epi32(_mm_mullo_epi32(b[16], txmat_1_0),
                         _mm_mullo_epi32(b[17], txmat_1_30));
    c[9] = _mm_add_epi32(_mm_mullo_epi32(b[18], txmat_1_2),
                         _mm_mullo_epi32(b[19], txmat_1_28));
    c[10] = _mm_add_epi32(_mm_mullo_epi32(b[20], txmat_1_4),
                          _mm_mullo_epi32(b[21], txmat_1_26));
    c[11] = _mm_add_epi32(_mm_mullo_epi32(b[22], txmat_1_6),
                          _mm_mullo_epi32(b[23], txmat_1_24));
    c[12] = _mm_add_epi32(_mm_mullo_epi32(b[24], txmat_1_8),
                          _mm_mullo_epi32(b[25], txmat_1_22));
    c[13] = _mm_add_epi32(_mm_mullo_epi32(b[26], txmat_1_10),
                          _mm_mullo_epi32(b[27], txmat_1_20));
    c[14] = _mm_add_epi32(_mm_mullo_epi32(b[28], txmat_1_12),
                          _mm_mullo_epi32(b[29], txmat_1_18));
    c[15] = _mm_add_epi32(_mm_mullo_epi32(b[30], txmat_1_14),
                          _mm_mullo_epi32(b[31], txmat_1_16));
    c[0] = _mm_add_epi32(_mm_sub_epi32(c[0], c[1]), _mm_sub_epi32(c[2], c[3]));
    c[4] = _mm_add_epi32(_mm_sub_epi32(c[4], c[5]), _mm_sub_epi32(c[6], c[7]));
    c[8] =
        _mm_add_epi32(_mm_sub_epi32(c[8], c[9]), _mm_sub_epi32(c[10], c[11]));
    c[12] =
        _mm_add_epi32(_mm_sub_epi32(c[12], c[13]), _mm_sub_epi32(c[14], c[15]));
    a[31] =
        _mm_add_epi32(_mm_add_epi32(c[0], c[4]), _mm_add_epi32(c[8], c[12]));

    for (n = 0; n < 32; n++) {
      a[n] = _mm_add_epi32(a[n], v_offset);
      a[n] = _mm_srai_epi32(a[n], shift);
    }

    transpose_store_8x4_sse4(a, dst + 0, tx1d_size);
    transpose_store_8x4_sse4(a + 8, dst + 8, tx1d_size);
    transpose_store_8x4_sse4(a + 16, dst + 16, tx1d_size);
    transpose_store_8x4_sse4(a + 24, dst + 24, tx1d_size);
  }
}

// ********************************** DST-VII **********************************
void fwd_txfm_idtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  __m128i v_offset = _mm_set1_epi32(offset);
  for (int i = 0; i < nz_line; i += 2, src += 2) {
    __m128i v_src0 =
        _mm_set_epi32(src[3 * line], src[2 * line], src[line], src[0]);
    __m128i v_src1 = _mm_set_epi32(src[3 * line + 1], src[2 * line + 1],
                                   src[line + 1], src[1]);

    // Multiply by scale and add offset
    __m128i v_scaled0 = _mm_slli_epi32(v_src0, 7);
    __m128i v_offsetted0 = _mm_add_epi32(v_scaled0, v_offset);
    __m128i v_shifted0 = _mm_srai_epi32(v_offsetted0, shift);
    __m128i v_scaled1 = _mm_slli_epi32(v_src1, 7);
    __m128i v_offsetted1 = _mm_add_epi32(v_scaled1, v_offset);
    __m128i v_shifted1 = _mm_srai_epi32(v_offsetted1, shift);

    // Right shift by shift
    _mm256_storeu_si256((__m256i *)(dst + (i * tx1d_size)),
                        _mm256_set_m128i(v_shifted1, v_shifted0));
  }
}

void fwd_txfm_idtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int scale = 181;

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_scale = _mm256_set1_epi32(scale);
  for (int i = 0; i < nz_line; i++, src++) {
    __m256i v_src = _mm256_set_epi32(
        src[7 * line], src[6 * line], src[5 * line], src[4 * line],
        src[3 * line], src[2 * line], src[line], src[0]);

    // Multiply by scale and add offset
    __m256i v_scaled = _mm256_mullo_epi32(v_src, v_scale);
    __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);
    __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

    // Right shift by shift
    _mm256_storeu_si256((__m256i *)(dst + (i * tx1d_size)), v_shifted);
  }
}

void fwd_txfm_idtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int i = 0; i < nz_line; i++, src++) {
    for (int j = 0; j < tx1d_size; j += 8) {
      __m256i v_src = _mm256_set_epi32(
          src[(7 + j) * line], src[(6 + j) * line], src[(5 + j) * line],
          src[(4 + j) * line], src[(3 + j) * line], src[(2 + j) * line],
          src[(1 + j) * line], src[(0 + j) * line]);

      // Multiply by scale and add offset
      __m256i v_scaled = _mm256_slli_epi32(v_src, 8);
      __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);
      __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

      // Right shift by shift
      _mm256_storeu_si256((__m256i *)(dst + (i * tx1d_size) + j), v_shifted);
    }
  }
}

void fwd_txfm_idtx_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  const int scale = 362;

  __m256i v_offset = _mm256_set1_epi32(offset);
  __m256i v_scale = _mm256_set1_epi32(scale);
  for (int i = 0; i < nz_line; i++, src++) {
    for (int j = 0; j < tx1d_size; j += 8) {
      __m256i v_src = _mm256_set_epi32(
          src[(7 + j) * line], src[(6 + j) * line], src[(5 + j) * line],
          src[(4 + j) * line], src[(3 + j) * line], src[(2 + j) * line],
          src[(1 + j) * line], src[(0 + j) * line]);

      // Multiply by scale and add offset
      __m256i v_scaled = _mm256_mullo_epi32(v_src, v_scale);
      __m256i v_offsetted = _mm256_add_epi32(v_scaled, v_offset);
      __m256i v_shifted = _mm256_srai_epi32(v_offsetted, shift);

      // Right shift by shift
      _mm256_storeu_si256((__m256i *)(dst + (i * tx1d_size) + j), v_shifted);
    }
  }
}

void fwd_txfm_adst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  DECLARE_ALIGNED(32, static const int, tx_kernel_adst_size4_avx2[4][8]) = {
    { 18, 50, 75, 89, 18, 50, 75, 89 },
    { 50, 89, 18, -75, 50, 89, 18, -75 },
    { 75, 18, -89, 50, 75, 18, -89, 50 },
    { 89, -75, 50, -18, 89, -75, 50, -18 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[k * line + j + 0]);
      __m128i tmp_src1 = _mm_set1_epi32(src[k * line + j + 1]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_adst_size4_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_adst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;

  DECLARE_ALIGNED(32, static const int, tx_kernel_adst_size8_avx2[8][8]) = {
    { 11, 34, 54, 71, 84, 88, 79, 50 },
    { 28, 74, 89, 68, 17, -44, -83, -69 },
    { 44, 89, 48, -41, -89, -44, 50, 81 },
    { 58, 76, -34, -86, 10, 88, 6, -84 },
    { 70, 39, -87, 1, 86, -44, -59, 78 },
    { 79, -12, -66, 87, -35, -44, 86, -62 },
    { 86, -58, 12, 38, -75, 88, -74, 40 },
    { 89, -86, 79, -70, 58, -44, 29, -14 },
  };

  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_adst_size8_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_adst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  DECLARE_ALIGNED(32, static const int, tx_kernel_adst_size16_avx2[32][8]) = {
    { 8, 25, 41, 55, 67, 77, 84, 88 },
    { 17, 48, 73, 87, 88, 77, 55, 25 },
    { 25, 67, 88, 81, 48, 0, -48, -81 },
    { 33, 81, 84, 41, -25, -77, -87, -48 },
    { 41, 88, 62, -17, -81, -77, -8, 67 },
    { 48, 88, 25, -67, -81, 0, 81, 67 },
    { 55, 81, -17, -89, -25, 77, 62, -48 },
    { 62, 67, -55, -73, 48, 77, -41, -81 },
    { 67, 48, -81, -25, 88, 0, -88, 25 },
    { 73, 25, -89, 33, 67, -77, -17, 88 },
    { 77, 0, -77, 77, 0, -77, 77, 0 },
    { 81, -25, -48, 88, -67, 0, 67, -88 },
    { 84, -48, -8, 62, -88, 77, -33, -25 },
    { 87, -67, 33, 8, -48, 77, -89, 81 },
    { 88, -81, 67, -48, 25, 0, -25, 48 },
    { 89, -88, 87, -84, 81, -77, 73, -67 },
    { 89, 87, 81, 73, 62, 48, 33, 17 },
    { -8, -41, -67, -84, -89, -81, -62, -33 },
    { -88, -67, -25, 25, 67, 88, 81, 48 },
    { 17, 73, 88, 55, -8, -67, -89, -62 },
    { 87, 33, -48, -89, -55, 25, 84, 73 },
    { -25, -88, -48, 48, 88, 25, -67, -81 },
    { -84, 8, 88, 33, -73, -67, 41, 87 },
    { 33, 84, -25, -87, 17, 88, -8, -89 },
    { 81, -48, -67, 67, 48, -81, -25, 88 },
    { -41, -62, 81, 8, -87, 48, 55, -84 },
    { -77, 77, 0, -77, 77, 0, -77, 77 },
    { 48, 25, -81, 81, -25, -48, 88, -67 },
    { 73, -89, 67, -17, -41, 81, -87, 55 },
    { -55, 17, 25, -62, 84, -88, 73, -41 },
    { -67, 81, -88, 88, -81, 67, -48, 25 },
    { 62, -55, 48, -41, 33, -25, 17, -8 },
  };

  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

        __m256i tmp_val =
            _mm256_load_si256((__m256i *)tx_kernel_adst_size16_avx2[2 * i + k]);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Store results
      _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size) + i), sum);
    }
  }
}

void fwd_txfm_fdst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  DECLARE_ALIGNED(32, static const int, tx_kernel_fdst_size4_avx2[4][8]) = {
    { 89, 75, 50, 18, 89, 75, 50, 18 },
    { 75, -18, -89, -50, 75, -18, -89, -50 },
    { 50, -89, 18, 75, 50, -89, 18, 75 },
    { 18, -50, 75, -89, 18, -50, 75, -89 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[k * line + j + 0]);
      __m128i tmp_src1 = _mm_set1_epi32(src[k * line + j + 1]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_fdst_size4_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_fdst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;

  DECLARE_ALIGNED(32, static const int, tx_kernel_fdst_size8_avx2[8][8]) = {
    { 89, -86, 79, -70, 58, -44, 29, -14 },
    { 86, -58, 12, 38, -75, 88, -74, 40 },
    { 79, -12, -66, 87, -35, -44, 86, -62 },
    { 70, 39, -87, 1, 86, -44, -59, 78 },
    { 58, 76, -34, -86, 10, 88, 6, -84 },
    { 44, 89, 48, -41, -89, -44, 50, 81 },
    { 28, 74, 89, 68, 17, -44, -83, -69 },
    { 11, 34, 54, 71, 84, 88, 79, 50 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_fdst_size8_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_fdst_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  DECLARE_ALIGNED(32, static const int, tx_kernel_fdst_size16_avx2[32][8]) = {
    { 89, 88, 87, 84, 81, 77, 73, 67 },
    { 88, 81, 67, 48, 25, 0, -25, -48 },
    { 87, 67, 33, -8, -48, -77, -89, -81 },
    { 84, 48, -8, -62, -88, -77, -33, 25 },
    { 81, 25, -48, -88, -67, 0, 67, 88 },
    { 77, 0, -77, -77, 0, 77, 77, 0 },
    { 73, -25, -89, -33, 67, 77, -17, -88 },
    { 67, -48, -81, 25, 88, 0, -88, -25 },
    { 62, -67, -55, 73, 48, -77, -41, 81 },
    { 55, -81, -17, 89, -25, -77, 62, 48 },
    { 48, -88, 25, 67, -81, 0, 81, -67 },
    { 41, -88, 62, 17, -81, 77, -8, -67 },
    { 33, -81, 84, -41, -25, 77, -87, 48 },
    { 25, -67, 88, -81, 48, 0, -48, 81 },
    { 17, -48, 73, -87, 88, -77, 55, -25 },
    { 8, -25, 41, -55, 67, -77, 84, -88 },
    { 62, 55, 48, 41, 33, 25, 17, 8 },
    { -67, -81, -88, -88, -81, -67, -48, -25 },
    { -55, -17, 25, 62, 84, 88, 73, 41 },
    { 73, 89, 67, 17, -41, -81, -87, -55 },
    { 48, -25, -81, -81, -25, 48, 88, 67 },
    { -77, -77, 0, 77, 77, 0, -77, -77 },
    { -41, 62, 81, -8, -87, -48, 55, 84 },
    { 81, 48, -67, -67, 48, 81, -25, -88 },
    { 33, -84, -25, 87, 17, -88, -8, 89 },
    { -84, -8, 88, -33, -73, 67, 41, -87 },
    { -25, 88, -48, -48, 88, -25, -67, 81 },
    { 87, -33, -48, 89, -55, -25, 84, -73 },
    { 17, -73, 88, -55, -8, 67, -89, 62 },
    { -88, 67, -25, -25, 67, -88, 81, -48 },
    { -8, 41, -67, 84, -89, 81, -62, 33 },
    { 89, -87, 81, -73, 62, -48, 33, -17 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

        __m256i tmp_val =
            _mm256_load_si256((__m256i *)tx_kernel_fdst_size16_avx2[2 * i + k]);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Store results
      _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size) + i), sum);
    }
  }
}

void fwd_txfm_ddtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  DECLARE_ALIGNED(32, static const int, tx_kernel_ddtx_size4_avx2[4][8]) = {
    { 2, 14, 67, 108, 2, 14, 67, 108 },
    { 20, 68, 86, -61, 20, 68, 86, -61 },
    { 72, 81, -61, 27, 72, 81, -61, 27 },
    { 104, -69, 25, -8, 104, -69, 25, -8 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[k * line + j + 0]);
      __m128i tmp_src1 = _mm_set1_epi32(src[k * line + j + 1]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_ddtx_size4_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_ddtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;

  DECLARE_ALIGNED(32, static const int, tx_kernel_ddtx_size8_avx2[8][8]) = {
    { 4, 6, 22, 57, 96, 103, 78, 56 },
    { 7, 14, 48, 94, 73, -17, -79, -96 },
    { 15, 36, 85, 76, -43, -80, 7, 98 },
    { 33, 77, 88, -26, -69, 56, 56, -77 },
    { 65, 100, 0, -73, 55, 15, -82, 54 },
    { 98, 45, -86, 34, 20, -66, 79, -33 },
    { 106, -57, -23, 54, -71, 75, -56, 19 },
    { 80, -98, 82, -66, 53, -41, 26, -6 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_ddtx_size8_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_ddtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  DECLARE_ALIGNED(32, static const int, tx_kernel_ddtx_size16_avx2[32][8]) = {
    { 12, 17, 37, 45, 47, 60, 64, 82 },
    { 15, 23, 49, 60, 60, 74, 70, 73 },
    { 19, 30, 60, 69, 61, 64, 40, 3 },
    { 23, 38, 69, 73, 49, 28, -19, -80 },
    { 30, 48, 75, 66, 19, -31, -79, -91 },
    { 39, 61, 75, 40, -29, -87, -78, 10 },
    { 51, 76, 61, -8, -77, -82, 11, 94 },
    { 66, 87, 29, -65, -83, 4, 92, 18 },
    { 78, 83, -18, -91, -16, 88, 28, -84 },
    { 88, 59, -67, -57, 75, 54, -85, -5 },
    { 94, 19, -96, 21, 93, -55, -41, 80 },
    { 97, -30, -83, 86, 3, -77, 82, -17 },
    { 93, -73, -28, 81, -92, 29, 39, -70 },
    { 83, -99, 40, 8, -74, 88, -83, 47 },
    { 68, -99, 84, -69, 32, 3, -37, 55 },
    { 50, -76, 83, -90, 97, -86, 83, -68 },
    { 89, 100, 92, 84, 69, 50, 51, 44 },
    { 48, 9, -35, -71, -83, -79, -89, -95 },
    { -53, -99, -91, -46, 2, 47, 73, 124 },
    { -96, -45, 42, 88, 75, 14, -17, -126 },
    { -5, 84, 71, -16, -78, -60, -45, 108 },
    { 89, 36, -69, -67, 18, 67, 89, -81 },
    { 16, -81, -22, 79, 50, -37, -103, 54 },
    { -83, 4, 85, -22, -85, -6, 97, -30 },
    { 12, 73, -60, -46, 81, 49, -83, 16 },
    { 75, -60, -17, 84, -43, -80, 71, -6 },
    { -51, -17, 77, -68, -6, 98, -56, 1 },
    { -43, 76, -70, 15, 53, -99, 44, 3 },
    { 81, -55, 11, 46, -81, 90, -31, -4 },
    { -14, -21, 56, -83, 88, -71, 22, 5 },
    { -75, 81, -83, 82, -69, 48, -11, -3 },
    { 67, -56, 49, -40, 32, -19, 5, 2 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

        __m256i tmp_val =
            _mm256_load_si256((__m256i *)tx_kernel_ddtx_size16_avx2[2 * i + k]);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Store results
      _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size) + i), sum);
    }
  }
}

void fwd_txfm_fddt_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;

  DECLARE_ALIGNED(32, static const int, tx_kernel_ddtx_size4_avx2[4][8]) = {
    { 104, -69, 25, -8, 104, -69, 25, -8 },
    { 72, 81, -61, 27, 72, 81, -61, 27 },
    { 20, 68, 86, -61, 20, 68, 86, -61 },
    { 2, 14, 67, 108, 2, 14, 67, 108 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j += 2) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m128i tmp_src0 = _mm_set1_epi32(src[k * line + j + 0]);
      __m128i tmp_src1 = _mm_set1_epi32(src[k * line + j + 1]);
      __m256i tmp_src = _mm256_set_m128i(tmp_src1, tmp_src0);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_ddtx_size4_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_fddt_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;

  DECLARE_ALIGNED(32, static const int, tx_kernel_fddtx_size8_avx2[8][8]) = {
    { 80, -98, 82, -66, 53, -41, 26, -6 },
    { 106, -57, -23, 54, -71, 75, -56, 19 },
    { 98, 45, -86, 34, 20, -66, 79, -33 },
    { 65, 100, 0, -73, 55, 15, -82, 54 },
    { 33, 77, 88, -26, -69, 56, 56, -77 },
    { 15, 36, 85, 76, -43, -80, 7, 98 },
    { 7, 14, 48, 94, 73, -17, -79, -96 },
    { 4, 6, 22, 57, 96, 103, 78, 56 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    __m256i sum = _mm256_set1_epi32(0);
    for (int k = 0; k < tx1d_size; k++) {
      __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

      __m256i tmp_val =
          _mm256_load_si256((__m256i *)tx_kernel_fddtx_size8_avx2[k]);

      tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
      sum = _mm256_add_epi32(tmp_val, sum);
    }
    // Multiply by scale and add offset
    sum = _mm256_add_epi32(sum, v_offset);

    // Right shift by shift
    sum = _mm256_srai_epi32(sum, shift);

    // Store results
    _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size)), sum);
  }
}

void fwd_txfm_fddt_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;

  DECLARE_ALIGNED(32, static const int, tx_kernel_fddtx_size16_avx2[32][8]) = {
    { 50, -76, 83, -90, 97, -86, 83, -68 },
    { 68, -99, 84, -69, 32, 3, -37, 55 },
    { 83, -99, 40, 8, -74, 88, -83, 47 },
    { 93, -73, -28, 81, -92, 29, 39, -70 },
    { 97, -30, -83, 86, 3, -77, 82, -17 },
    { 94, 19, -96, 21, 93, -55, -41, 80 },
    { 88, 59, -67, -57, 75, 54, -85, -5 },
    { 78, 83, -18, -91, -16, 88, 28, -84 },
    { 66, 87, 29, -65, -83, 4, 92, 18 },
    { 51, 76, 61, -8, -77, -82, 11, 94 },
    { 39, 61, 75, 40, -29, -87, -78, 10 },
    { 30, 48, 75, 66, 19, -31, -79, -91 },
    { 23, 38, 69, 73, 49, 28, -19, -80 },
    { 19, 30, 60, 69, 61, 64, 40, 3 },
    { 15, 23, 49, 60, 60, 74, 70, 73 },
    { 12, 17, 37, 45, 47, 60, 64, 82 },
    { 67, -56, 49, -40, 32, -19, 5, 2 },
    { -75, 81, -83, 82, -69, 48, -11, -3 },
    { -14, -21, 56, -83, 88, -71, 22, 5 },
    { 81, -55, 11, 46, -81, 90, -31, -4 },
    { -43, 76, -70, 15, 53, -99, 44, 3 },
    { -51, -17, 77, -68, -6, 98, -56, 1 },
    { 75, -60, -17, 84, -43, -80, 71, -6 },
    { 12, 73, -60, -46, 81, 49, -83, 16 },
    { -83, 4, 85, -22, -85, -6, 97, -30 },
    { 16, -81, -22, 79, 50, -37, -103, 54 },
    { 89, 36, -69, -67, 18, 67, 89, -81 },
    { -5, 84, 71, -16, -78, -60, -45, 108 },
    { -96, -45, 42, 88, 75, 14, -17, -126 },
    { -53, -99, -91, -46, 2, 47, 73, 124 },
    { 48, 9, -35, -71, -83, -79, -89, -95 },
    { 89, 100, 92, 84, 69, 50, 51, 44 },
  };
  __m256i v_offset = _mm256_set1_epi32(offset);
  for (int j = 0; j < nz_line; j++) {
    for (int i = 0; i < tx1d_size; i += 8) {
      __m256i sum = _mm256_set1_epi32(0);
      for (int k = 0; k < tx1d_size; k++) {
        __m256i tmp_src = _mm256_set1_epi32(src[k * line + j]);

        __m256i tmp_val = _mm256_load_si256(
            (__m256i *)tx_kernel_fddtx_size16_avx2[2 * i + k]);

        tmp_val = _mm256_mullo_epi32(tmp_src, tmp_val);
        sum = _mm256_add_epi32(tmp_val, sum);
      }
      // Multiply by scale and add offset
      sum = _mm256_add_epi32(sum, v_offset);

      // Right shift by shift
      sum = _mm256_srai_epi32(sum, shift);

      // Store results
      _mm256_storeu_si256((__m256i *)(dst + (j * tx1d_size) + i), sum);
    }
  }
}

void fwd_transform_1d_avx2(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line,
                           const int tx_type_index, const int size_index) {
  switch (size_index) {
    case 0:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 2:
          fwd_txfm_adst_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 4:
          fwd_txfm_ddtx_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size4_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        default: assert(0); break;
      }
      break;
    case 1:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 2:
          fwd_txfm_adst_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 4:
          fwd_txfm_ddtx_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size8_avx2(src, dst, shift, line, skip_line, zero_line);
          break;
        default: assert(0); break;
      }
      break;
    case 2:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 2:
          fwd_txfm_adst_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 4:
          fwd_txfm_ddtx_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size16_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        default: assert(0); break;
      }
      break;
    case 3:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size32_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size32_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        default: assert(0); break;
      }
      break;
    case 4:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size64_avx2(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        default: assert(0); break;
      }
      break;
    default: assert(0); break;
  }
}

void fwd_txfm_avx2(const int16_t *resi, tran_low_t *coeff, int diff_stride,
                   TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;

  TX_TYPE tx_type = txfm_param->tx_type;

  if (txfm_param->lossless) {
#if CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
    assert(width == 4 && height == 4);
    assert(tx_type == DCT_DCT || tx_type == IDTX);
    if (tx_type == IDTX) {
      av1_fwd_txfm2d_4x4(resi, coeff, diff_stride, tx_type, txfm_param->use_ddt,
                         txfm_param->bd);
    } else {
      av1_highbd_fwht4x4(resi, coeff, diff_stride);
    }
#else   // CONFIG_LOSSLESS_DPCM
    assert(tx_type == DCT_DCT);
    av1_highbd_fwht4x4(resi, coeff, diff_stride);
#endif  // CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
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

  int buf[MAX_TX_SQUARE] = { 0 };

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      coeff[(y * width) + x] = resi[(y * diff_stride) + x];
    }
  }

  const int shift_1st = fwd_tx_shift[tx_size][0];
  const int shift_2nd = fwd_tx_shift[tx_size][1];

  fwd_transform_1d_avx2(coeff, buf, shift_1st, width, 0, skipHeight,
                        tx_type_col, tx_high_index);

  fwd_transform_1d_avx2(buf, coeff, shift_2nd, height, skipHeight, skipWidth,
                        tx_type_row, tx_wide_index);

  // Re-pack non-zero coeffs in the first 32x32 indices.
  if (skipWidth) {
    for (int row = 1; row < height; ++row) {
      __m256i data0 = _mm256_loadu_si256((__m256i *)(coeff + row * width));
      __m256i data1 = _mm256_loadu_si256((__m256i *)(coeff + row * width + 8));
      __m256i data2 = _mm256_loadu_si256((__m256i *)(coeff + row * width + 16));
      __m256i data3 = _mm256_loadu_si256((__m256i *)(coeff + row * width + 24));
      _mm256_storeu_si256((__m256i *)(coeff + row * 32), data0);
      _mm256_storeu_si256((__m256i *)(coeff + row * 32 + 8), data1);
      _mm256_storeu_si256((__m256i *)(coeff + row * 32 + 16), data2);
      _mm256_storeu_si256((__m256i *)(coeff + row * 32 + 24), data3);
    }
  }

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;
  if (sqrt2) {
    __m256i scale_vector = _mm256_set1_epi64x((int64_t)NewSqrt2);
    __m128i shift_bits = _mm_set1_epi64x(NewSqrt2Bits);
    __m256i round_offset = _mm256_set1_epi64x(1LL << (NewSqrt2Bits - 1));
    __m256i idx = _mm256_set_epi32(6, 4, 2, 0, 6, 4, 2, 0);
    for (int i = 0; i < AOMMIN(1024, width * height); i += 8) {
      __m256i data = _mm256_loadu_si256((__m256i *)(coeff + i));

      __m256i data0 = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 0));
      data0 = _mm256_mul_epi32(data0, scale_vector);
      data0 = _mm256_add_epi64(data0, round_offset);
      data0 = _mm256_srl_epi64(data0, shift_bits);
      data0 = _mm256_permutevar8x32_epi32(data0, idx);

      __m256i data1 = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(data, 1));
      data1 = _mm256_mul_epi32(data1, scale_vector);
      data1 = _mm256_add_epi64(data1, round_offset);
      data1 = _mm256_srl_epi64(data1, shift_bits);
      data1 = _mm256_permutevar8x32_epi32(data1, idx);

      data = _mm256_blend_epi32(data0, data1, 0b11110000);

      _mm256_storeu_si256((__m256i *)(coeff + i), data);
    }
  }
}
