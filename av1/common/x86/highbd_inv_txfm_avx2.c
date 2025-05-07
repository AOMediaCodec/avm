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

#include "av1/common/av1_inv_txfm1d_cfg.h"
#include "av1/common/idct.h"
#include "av1/common/x86/av1_inv_txfm_ssse3.h"
#include "av1/common/x86/highbd_txfm_utility_sse4.h"
#include "aom_dsp/x86/txfm_common_avx2.h"

#if CONFIG_CORE_TX
#include "av1/common/txb_common.h"
#endif  // CONFIG_CORE_TX

// Note:
//  Total 32x4 registers to represent 32x32 block coefficients.
//  For high bit depth, each coefficient is 4-byte.
//  Each __m256i register holds 8 coefficients.
//  So each "row" we needs 4 register. Totally 32 rows
//  Register layout:
//   v0,   v1,   v2,   v3,
//   v4,   v5,   v6,   v7,
//   ... ...
//   v124, v125, v126, v127

static INLINE __m256i highbd_clamp_epi16_avx2(__m256i u, int bd) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i one = _mm256_set1_epi16(1);
  const __m256i max = _mm256_sub_epi16(_mm256_slli_epi16(one, bd), one);
  __m256i clamped, mask;

  mask = _mm256_cmpgt_epi16(u, max);
  clamped = _mm256_andnot_si256(mask, u);
  mask = _mm256_and_si256(mask, max);
  clamped = _mm256_or_si256(mask, clamped);
  mask = _mm256_cmpgt_epi16(clamped, zero);
  clamped = _mm256_and_si256(clamped, mask);

  return clamped;
}

static INLINE void round_shift_4x4_avx2(__m256i *in, int shift) {
  if (shift != 0) {
    __m256i rnding = _mm256_set1_epi32(1 << (shift - 1));
    in[0] = _mm256_add_epi32(in[0], rnding);
    in[1] = _mm256_add_epi32(in[1], rnding);
    in[2] = _mm256_add_epi32(in[2], rnding);
    in[3] = _mm256_add_epi32(in[3], rnding);

    in[0] = _mm256_srai_epi32(in[0], shift);
    in[1] = _mm256_srai_epi32(in[1], shift);
    in[2] = _mm256_srai_epi32(in[2], shift);
    in[3] = _mm256_srai_epi32(in[3], shift);
  }
}

static INLINE void round_shift_8x8_avx2(__m256i *in, int shift) {
  round_shift_4x4_avx2(in, shift);
  round_shift_4x4_avx2(in + 4, shift);
  round_shift_4x4_avx2(in + 8, shift);
  round_shift_4x4_avx2(in + 12, shift);
}

static void highbd_clamp_epi32_avx2(__m256i *in, __m256i *out,
                                    const __m256i *clamp_lo,
                                    const __m256i *clamp_hi, int size) {
  __m256i a0, a1;
  for (int i = 0; i < size; i += 4) {
    a0 = _mm256_max_epi32(in[i], *clamp_lo);
    out[i] = _mm256_min_epi32(a0, *clamp_hi);

    a1 = _mm256_max_epi32(in[i + 1], *clamp_lo);
    out[i + 1] = _mm256_min_epi32(a1, *clamp_hi);

    a0 = _mm256_max_epi32(in[i + 2], *clamp_lo);
    out[i + 2] = _mm256_min_epi32(a0, *clamp_hi);

    a1 = _mm256_max_epi32(in[i + 3], *clamp_lo);
    out[i + 3] = _mm256_min_epi32(a1, *clamp_hi);
  }
}

static INLINE __m256i highbd_get_recon_16x8_avx2(const __m256i pred,
                                                 __m256i res0, __m256i res1,
                                                 const int bd) {
  __m256i x0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(pred));
  __m256i x1 = _mm256_cvtepi16_epi32(_mm256_extractf128_si256(pred, 1));

  x0 = _mm256_add_epi32(res0, x0);
  x1 = _mm256_add_epi32(res1, x1);
  x0 = _mm256_packus_epi32(x0, x1);
  x0 = _mm256_permute4x64_epi64(x0, 0xd8);
  x0 = highbd_clamp_epi16_avx2(x0, bd);
  return x0;
}

static INLINE void highbd_write_buffer_16xn_avx2(__m256i *in, uint16_t *output,
                                                 int stride, int flipud,
                                                 int height, const int bd) {
  int j = flipud ? (height - 1) : 0;
  const int step = flipud ? -1 : 1;
  for (int i = 0; i < height; ++i, j += step) {
    __m256i v = _mm256_loadu_si256((__m256i const *)(output + i * stride));
    __m256i u = highbd_get_recon_16x8_avx2(v, in[j], in[j + height], bd);

    _mm256_storeu_si256((__m256i *)(output + i * stride), u);
  }
}
static INLINE __m256i highbd_get_recon_8x8_avx2(const __m256i pred, __m256i res,
                                                const int bd) {
  __m256i x0 = pred;
  x0 = _mm256_add_epi32(res, x0);
  x0 = _mm256_packus_epi32(x0, x0);
  x0 = _mm256_permute4x64_epi64(x0, 0xd8);
  x0 = highbd_clamp_epi16_avx2(x0, bd);
  return x0;
}

static INLINE void highbd_write_buffer_8xn_avx2(__m256i *in, uint16_t *output,
                                                int stride, int flipud,
                                                int height, const int bd) {
  int j = flipud ? (height - 1) : 0;
  __m128i temp;
  const int step = flipud ? -1 : 1;
  for (int i = 0; i < height; ++i, j += step) {
    temp = _mm_loadu_si128((__m128i const *)(output + i * stride));
    __m256i v = _mm256_cvtepi16_epi32(temp);
    __m256i u = highbd_get_recon_8x8_avx2(v, in[j], bd);
    __m128i u1 = _mm256_castsi256_si128(u);
    _mm_storeu_si128((__m128i *)(output + i * stride), u1);
  }
}

#if !(CONFIG_ADST_TUNED && USE_TUNED_ADST16)
static void neg_shift_avx2(const __m256i in0, const __m256i in1, __m256i *out0,
                           __m256i *out1, const __m256i *clamp_lo,
                           const __m256i *clamp_hi, int shift) {
  __m256i offset = _mm256_set1_epi32((1 << shift) >> 1);
  __m256i a0 = _mm256_add_epi32(offset, in0);
  __m256i a1 = _mm256_sub_epi32(offset, in1);

  a0 = _mm256_sra_epi32(a0, _mm_cvtsi32_si128(shift));
  a1 = _mm256_sra_epi32(a1, _mm_cvtsi32_si128(shift));

  a0 = _mm256_max_epi32(a0, *clamp_lo);
  a0 = _mm256_min_epi32(a0, *clamp_hi);
  a1 = _mm256_max_epi32(a1, *clamp_lo);
  a1 = _mm256_min_epi32(a1, *clamp_hi);

  *out0 = a0;
  *out1 = a1;
}
#endif  // !(CONFIG_ADST_TUNED && USE_TUNED_ADST16)

static void transpose_8x8_avx2(const __m256i *in, __m256i *out) {
  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i x0, x1;

  u0 = _mm256_unpacklo_epi32(in[0], in[1]);
  u1 = _mm256_unpackhi_epi32(in[0], in[1]);

  u2 = _mm256_unpacklo_epi32(in[2], in[3]);
  u3 = _mm256_unpackhi_epi32(in[2], in[3]);

  u4 = _mm256_unpacklo_epi32(in[4], in[5]);
  u5 = _mm256_unpackhi_epi32(in[4], in[5]);

  u6 = _mm256_unpacklo_epi32(in[6], in[7]);
  u7 = _mm256_unpackhi_epi32(in[6], in[7]);

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

static void transpose_8x8_flip_avx2(const __m256i *in, __m256i *out) {
  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i x0, x1;

  u0 = _mm256_unpacklo_epi32(in[7], in[6]);
  u1 = _mm256_unpackhi_epi32(in[7], in[6]);

  u2 = _mm256_unpacklo_epi32(in[5], in[4]);
  u3 = _mm256_unpackhi_epi32(in[5], in[4]);

  u4 = _mm256_unpacklo_epi32(in[3], in[2]);
  u5 = _mm256_unpackhi_epi32(in[3], in[2]);

  u6 = _mm256_unpacklo_epi32(in[1], in[0]);
  u7 = _mm256_unpackhi_epi32(in[1], in[0]);

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

static void load_buffer_32x32(const int32_t *coeff, __m256i *in,
                              int input_stiride, int size) {
  int i;
  for (i = 0; i < size; ++i) {
    in[i] = _mm256_loadu_si256((const __m256i *)(coeff + i * input_stiride));
  }
}

static INLINE __m256i half_btf_0_avx2(const __m256i *w0, const __m256i *n0,
                                      const __m256i *rounding, int bit) {
  __m256i x;
  x = _mm256_mullo_epi32(*w0, *n0);
  x = _mm256_add_epi32(x, *rounding);
  x = _mm256_srai_epi32(x, bit);
  return x;
}

static INLINE __m256i half_btf_avx2(const __m256i *w0, const __m256i *n0,
                                    const __m256i *w1, const __m256i *n1,
                                    const __m256i *rounding, int bit) {
  __m256i x, y;

  x = _mm256_mullo_epi32(*w0, *n0);
  y = _mm256_mullo_epi32(*w1, *n1);
  x = _mm256_add_epi32(x, y);
  x = _mm256_add_epi32(x, *rounding);
  x = _mm256_srai_epi32(x, bit);
  return x;
}

static void addsub_avx2(const __m256i in0, const __m256i in1, __m256i *out0,
                        __m256i *out1, const __m256i *clamp_lo,
                        const __m256i *clamp_hi) {
  __m256i a0 = _mm256_add_epi32(in0, in1);
  __m256i a1 = _mm256_sub_epi32(in0, in1);

  a0 = _mm256_max_epi32(a0, *clamp_lo);
  a0 = _mm256_min_epi32(a0, *clamp_hi);
  a1 = _mm256_max_epi32(a1, *clamp_lo);
  a1 = _mm256_min_epi32(a1, *clamp_hi);

  *out0 = a0;
  *out1 = a1;
}

static INLINE void idct32_stage4_avx2(
    __m256i *bf1, const __m256i *cospim8, const __m256i *cospi56,
    const __m256i *cospi8, const __m256i *cospim56, const __m256i *cospim40,
    const __m256i *cospi24, const __m256i *cospi40, const __m256i *cospim24,
    const __m256i *rounding, int bit) {
  __m256i temp1, temp2;
  temp1 = half_btf_avx2(cospim8, &bf1[17], cospi56, &bf1[30], rounding, bit);
  bf1[30] = half_btf_avx2(cospi56, &bf1[17], cospi8, &bf1[30], rounding, bit);
  bf1[17] = temp1;

  temp2 = half_btf_avx2(cospim56, &bf1[18], cospim8, &bf1[29], rounding, bit);
  bf1[29] = half_btf_avx2(cospim8, &bf1[18], cospi56, &bf1[29], rounding, bit);
  bf1[18] = temp2;

  temp1 = half_btf_avx2(cospim40, &bf1[21], cospi24, &bf1[26], rounding, bit);
  bf1[26] = half_btf_avx2(cospi24, &bf1[21], cospi40, &bf1[26], rounding, bit);
  bf1[21] = temp1;

  temp2 = half_btf_avx2(cospim24, &bf1[22], cospim40, &bf1[25], rounding, bit);
  bf1[25] = half_btf_avx2(cospim40, &bf1[22], cospi24, &bf1[25], rounding, bit);
  bf1[22] = temp2;
}

static INLINE void idct32_stage5_avx2(
    __m256i *bf1, const __m256i *cospim16, const __m256i *cospi48,
    const __m256i *cospi16, const __m256i *cospim48, const __m256i *clamp_lo,
    const __m256i *clamp_hi, const __m256i *rounding, int bit) {
  __m256i temp1, temp2;
  temp1 = half_btf_avx2(cospim16, &bf1[9], cospi48, &bf1[14], rounding, bit);
  bf1[14] = half_btf_avx2(cospi48, &bf1[9], cospi16, &bf1[14], rounding, bit);
  bf1[9] = temp1;

  temp2 = half_btf_avx2(cospim48, &bf1[10], cospim16, &bf1[13], rounding, bit);
  bf1[13] = half_btf_avx2(cospim16, &bf1[10], cospi48, &bf1[13], rounding, bit);
  bf1[10] = temp2;

  addsub_avx2(bf1[16], bf1[19], bf1 + 16, bf1 + 19, clamp_lo, clamp_hi);
  addsub_avx2(bf1[17], bf1[18], bf1 + 17, bf1 + 18, clamp_lo, clamp_hi);
  addsub_avx2(bf1[23], bf1[20], bf1 + 23, bf1 + 20, clamp_lo, clamp_hi);
  addsub_avx2(bf1[22], bf1[21], bf1 + 22, bf1 + 21, clamp_lo, clamp_hi);
  addsub_avx2(bf1[24], bf1[27], bf1 + 24, bf1 + 27, clamp_lo, clamp_hi);
  addsub_avx2(bf1[25], bf1[26], bf1 + 25, bf1 + 26, clamp_lo, clamp_hi);
  addsub_avx2(bf1[31], bf1[28], bf1 + 31, bf1 + 28, clamp_lo, clamp_hi);
  addsub_avx2(bf1[30], bf1[29], bf1 + 30, bf1 + 29, clamp_lo, clamp_hi);
}

static INLINE void idct32_stage6_avx2(
    __m256i *bf1, const __m256i *cospim32, const __m256i *cospi32,
    const __m256i *cospim16, const __m256i *cospi48, const __m256i *cospi16,
    const __m256i *cospim48, const __m256i *clamp_lo, const __m256i *clamp_hi,
    const __m256i *rounding, int bit) {
  __m256i temp1, temp2;
  temp1 = half_btf_avx2(cospim32, &bf1[5], cospi32, &bf1[6], rounding, bit);
  bf1[6] = half_btf_avx2(cospi32, &bf1[5], cospi32, &bf1[6], rounding, bit);
  bf1[5] = temp1;

  addsub_avx2(bf1[8], bf1[11], bf1 + 8, bf1 + 11, clamp_lo, clamp_hi);
  addsub_avx2(bf1[9], bf1[10], bf1 + 9, bf1 + 10, clamp_lo, clamp_hi);
  addsub_avx2(bf1[15], bf1[12], bf1 + 15, bf1 + 12, clamp_lo, clamp_hi);
  addsub_avx2(bf1[14], bf1[13], bf1 + 14, bf1 + 13, clamp_lo, clamp_hi);

  temp1 = half_btf_avx2(cospim16, &bf1[18], cospi48, &bf1[29], rounding, bit);
  bf1[29] = half_btf_avx2(cospi48, &bf1[18], cospi16, &bf1[29], rounding, bit);
  bf1[18] = temp1;
  temp2 = half_btf_avx2(cospim16, &bf1[19], cospi48, &bf1[28], rounding, bit);
  bf1[28] = half_btf_avx2(cospi48, &bf1[19], cospi16, &bf1[28], rounding, bit);
  bf1[19] = temp2;
  temp1 = half_btf_avx2(cospim48, &bf1[20], cospim16, &bf1[27], rounding, bit);
  bf1[27] = half_btf_avx2(cospim16, &bf1[20], cospi48, &bf1[27], rounding, bit);
  bf1[20] = temp1;
  temp2 = half_btf_avx2(cospim48, &bf1[21], cospim16, &bf1[26], rounding, bit);
  bf1[26] = half_btf_avx2(cospim16, &bf1[21], cospi48, &bf1[26], rounding, bit);
  bf1[21] = temp2;
}

static INLINE void idct32_stage7_avx2(__m256i *bf1, const __m256i *cospim32,
                                      const __m256i *cospi32,
                                      const __m256i *clamp_lo,
                                      const __m256i *clamp_hi,
                                      const __m256i *rounding, int bit) {
  __m256i temp1, temp2;
  addsub_avx2(bf1[0], bf1[7], bf1 + 0, bf1 + 7, clamp_lo, clamp_hi);
  addsub_avx2(bf1[1], bf1[6], bf1 + 1, bf1 + 6, clamp_lo, clamp_hi);
  addsub_avx2(bf1[2], bf1[5], bf1 + 2, bf1 + 5, clamp_lo, clamp_hi);
  addsub_avx2(bf1[3], bf1[4], bf1 + 3, bf1 + 4, clamp_lo, clamp_hi);

  temp1 = half_btf_avx2(cospim32, &bf1[10], cospi32, &bf1[13], rounding, bit);
  bf1[13] = half_btf_avx2(cospi32, &bf1[10], cospi32, &bf1[13], rounding, bit);
  bf1[10] = temp1;
  temp2 = half_btf_avx2(cospim32, &bf1[11], cospi32, &bf1[12], rounding, bit);
  bf1[12] = half_btf_avx2(cospi32, &bf1[11], cospi32, &bf1[12], rounding, bit);
  bf1[11] = temp2;

  addsub_avx2(bf1[16], bf1[23], bf1 + 16, bf1 + 23, clamp_lo, clamp_hi);
  addsub_avx2(bf1[17], bf1[22], bf1 + 17, bf1 + 22, clamp_lo, clamp_hi);
  addsub_avx2(bf1[18], bf1[21], bf1 + 18, bf1 + 21, clamp_lo, clamp_hi);
  addsub_avx2(bf1[19], bf1[20], bf1 + 19, bf1 + 20, clamp_lo, clamp_hi);
  addsub_avx2(bf1[31], bf1[24], bf1 + 31, bf1 + 24, clamp_lo, clamp_hi);
  addsub_avx2(bf1[30], bf1[25], bf1 + 30, bf1 + 25, clamp_lo, clamp_hi);
  addsub_avx2(bf1[29], bf1[26], bf1 + 29, bf1 + 26, clamp_lo, clamp_hi);
  addsub_avx2(bf1[28], bf1[27], bf1 + 28, bf1 + 27, clamp_lo, clamp_hi);
}

static INLINE void idct32_stage8_avx2(__m256i *bf1, const __m256i *cospim32,
                                      const __m256i *cospi32,
                                      const __m256i *clamp_lo,
                                      const __m256i *clamp_hi,
                                      const __m256i *rounding, int bit) {
  __m256i temp1, temp2;
  addsub_avx2(bf1[0], bf1[15], bf1 + 0, bf1 + 15, clamp_lo, clamp_hi);
  addsub_avx2(bf1[1], bf1[14], bf1 + 1, bf1 + 14, clamp_lo, clamp_hi);
  addsub_avx2(bf1[2], bf1[13], bf1 + 2, bf1 + 13, clamp_lo, clamp_hi);
  addsub_avx2(bf1[3], bf1[12], bf1 + 3, bf1 + 12, clamp_lo, clamp_hi);
  addsub_avx2(bf1[4], bf1[11], bf1 + 4, bf1 + 11, clamp_lo, clamp_hi);
  addsub_avx2(bf1[5], bf1[10], bf1 + 5, bf1 + 10, clamp_lo, clamp_hi);
  addsub_avx2(bf1[6], bf1[9], bf1 + 6, bf1 + 9, clamp_lo, clamp_hi);
  addsub_avx2(bf1[7], bf1[8], bf1 + 7, bf1 + 8, clamp_lo, clamp_hi);

  temp1 = half_btf_avx2(cospim32, &bf1[20], cospi32, &bf1[27], rounding, bit);
  bf1[27] = half_btf_avx2(cospi32, &bf1[20], cospi32, &bf1[27], rounding, bit);
  bf1[20] = temp1;
  temp2 = half_btf_avx2(cospim32, &bf1[21], cospi32, &bf1[26], rounding, bit);
  bf1[26] = half_btf_avx2(cospi32, &bf1[21], cospi32, &bf1[26], rounding, bit);
  bf1[21] = temp2;
  temp1 = half_btf_avx2(cospim32, &bf1[22], cospi32, &bf1[25], rounding, bit);
  bf1[25] = half_btf_avx2(cospi32, &bf1[22], cospi32, &bf1[25], rounding, bit);
  bf1[22] = temp1;
  temp2 = half_btf_avx2(cospim32, &bf1[23], cospi32, &bf1[24], rounding, bit);
  bf1[24] = half_btf_avx2(cospi32, &bf1[23], cospi32, &bf1[24], rounding, bit);
  bf1[23] = temp2;
}

static INLINE void idct32_stage9_avx2(__m256i *bf1, __m256i *out,
                                      const int do_cols, const int bd,
                                      const int out_shift,
                                      const __m256i *clamp_lo,
                                      const __m256i *clamp_hi) {
  addsub_avx2(bf1[0], bf1[31], out + 0, out + 31, clamp_lo, clamp_hi);
  addsub_avx2(bf1[1], bf1[30], out + 1, out + 30, clamp_lo, clamp_hi);
  addsub_avx2(bf1[2], bf1[29], out + 2, out + 29, clamp_lo, clamp_hi);
  addsub_avx2(bf1[3], bf1[28], out + 3, out + 28, clamp_lo, clamp_hi);
  addsub_avx2(bf1[4], bf1[27], out + 4, out + 27, clamp_lo, clamp_hi);
  addsub_avx2(bf1[5], bf1[26], out + 5, out + 26, clamp_lo, clamp_hi);
  addsub_avx2(bf1[6], bf1[25], out + 6, out + 25, clamp_lo, clamp_hi);
  addsub_avx2(bf1[7], bf1[24], out + 7, out + 24, clamp_lo, clamp_hi);
  addsub_avx2(bf1[8], bf1[23], out + 8, out + 23, clamp_lo, clamp_hi);
  addsub_avx2(bf1[9], bf1[22], out + 9, out + 22, clamp_lo, clamp_hi);
  addsub_avx2(bf1[10], bf1[21], out + 10, out + 21, clamp_lo, clamp_hi);
  addsub_avx2(bf1[11], bf1[20], out + 11, out + 20, clamp_lo, clamp_hi);
  addsub_avx2(bf1[12], bf1[19], out + 12, out + 19, clamp_lo, clamp_hi);
  addsub_avx2(bf1[13], bf1[18], out + 13, out + 18, clamp_lo, clamp_hi);
  addsub_avx2(bf1[14], bf1[17], out + 14, out + 17, clamp_lo, clamp_hi);
  addsub_avx2(bf1[15], bf1[16], out + 15, out + 16, clamp_lo, clamp_hi);
  if (!do_cols) {
    const int log_range_out = AOMMAX(16, bd + 6);
    const __m256i clamp_lo_out = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    const __m256i clamp_hi_out =
        _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
    round_shift_8x8_avx2(out, out_shift);
    round_shift_8x8_avx2(out + 16, out_shift);
    highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 32);
  }
}

static void idct32_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rounding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i x;
  // stage 0
  // stage 1
  // stage 2
  // stage 3
  // stage 4
  // stage 5
  x = _mm256_mullo_epi32(in[0], cospi32);
  x = _mm256_add_epi32(x, rounding);
  x = _mm256_srai_epi32(x, bit);

  // stage 6
  // stage 7
  // stage 8
  // stage 9
  if (!do_cols) {
    const int log_range_out = AOMMAX(16, bd + 6);
    __m256i offset = _mm256_set1_epi32((1 << out_shift) >> 1);
    clamp_lo = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    clamp_hi = _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
    x = _mm256_add_epi32(offset, x);
    x = _mm256_sra_epi32(x, _mm_cvtsi32_si128(out_shift));
  }
  x = _mm256_max_epi32(x, clamp_lo);
  x = _mm256_min_epi32(x, clamp_hi);
  out[0] = x;
  out[1] = x;
  out[2] = x;
  out[3] = x;
  out[4] = x;
  out[5] = x;
  out[6] = x;
  out[7] = x;
  out[8] = x;
  out[9] = x;
  out[10] = x;
  out[11] = x;
  out[12] = x;
  out[13] = x;
  out[14] = x;
  out[15] = x;
  out[16] = x;
  out[17] = x;
  out[18] = x;
  out[19] = x;
  out[20] = x;
  out[21] = x;
  out[22] = x;
  out[23] = x;
  out[24] = x;
  out[25] = x;
  out[26] = x;
  out[27] = x;
  out[28] = x;
  out[29] = x;
  out[30] = x;
  out[31] = x;
}

static void idct32_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i rounding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i bf1[32];

  {
    // stage 0
    // stage 1
    bf1[0] = in[0];
    bf1[4] = in[4];
    bf1[8] = in[2];
    bf1[12] = in[6];
    bf1[16] = in[1];
    bf1[20] = in[5];
    bf1[24] = in[3];
    bf1[28] = in[7];

    // stage 2
    bf1[31] = half_btf_0_avx2(&cospi2, &bf1[16], &rounding, bit);
    bf1[16] = half_btf_0_avx2(&cospi62, &bf1[16], &rounding, bit);
    bf1[19] = half_btf_0_avx2(&cospim50, &bf1[28], &rounding, bit);
    bf1[28] = half_btf_0_avx2(&cospi14, &bf1[28], &rounding, bit);
    bf1[27] = half_btf_0_avx2(&cospi10, &bf1[20], &rounding, bit);
    bf1[20] = half_btf_0_avx2(&cospi54, &bf1[20], &rounding, bit);
    bf1[23] = half_btf_0_avx2(&cospim58, &bf1[24], &rounding, bit);
    bf1[24] = half_btf_0_avx2(&cospi6, &bf1[24], &rounding, bit);

    // stage 3
    bf1[15] = half_btf_0_avx2(&cospi4, &bf1[8], &rounding, bit);
    bf1[8] = half_btf_0_avx2(&cospi60, &bf1[8], &rounding, bit);

    bf1[11] = half_btf_0_avx2(&cospim52, &bf1[12], &rounding, bit);
    bf1[12] = half_btf_0_avx2(&cospi12, &bf1[12], &rounding, bit);
    bf1[17] = bf1[16];
    bf1[18] = bf1[19];
    bf1[21] = bf1[20];
    bf1[22] = bf1[23];
    bf1[25] = bf1[24];
    bf1[26] = bf1[27];
    bf1[29] = bf1[28];
    bf1[30] = bf1[31];

    // stage 4
    bf1[7] = half_btf_0_avx2(&cospi8, &bf1[4], &rounding, bit);
    bf1[4] = half_btf_0_avx2(&cospi56, &bf1[4], &rounding, bit);

    bf1[9] = bf1[8];
    bf1[10] = bf1[11];
    bf1[13] = bf1[12];
    bf1[14] = bf1[15];

    idct32_stage4_avx2(bf1, &cospim8, &cospi56, &cospi8, &cospim56, &cospim40,
                       &cospi24, &cospi40, &cospim24, &rounding, bit);

    // stage 5
    bf1[0] = half_btf_0_avx2(&cospi32, &bf1[0], &rounding, bit);
    bf1[1] = bf1[0];
    bf1[5] = bf1[4];
    bf1[6] = bf1[7];

    idct32_stage5_avx2(bf1, &cospim16, &cospi48, &cospi16, &cospim48, &clamp_lo,
                       &clamp_hi, &rounding, bit);

    // stage 6
    bf1[3] = bf1[0];
    bf1[2] = bf1[1];

    idct32_stage6_avx2(bf1, &cospim32, &cospi32, &cospim16, &cospi48, &cospi16,
                       &cospim48, &clamp_lo, &clamp_hi, &rounding, bit);

    // stage 7
    idct32_stage7_avx2(bf1, &cospim32, &cospi32, &clamp_lo, &clamp_hi,
                       &rounding, bit);

    // stage 8
    idct32_stage8_avx2(bf1, &cospim32, &cospi32, &clamp_lo, &clamp_hi,
                       &rounding, bit);

    // stage 9
    idct32_stage9_avx2(bf1, out, do_cols, bd, out_shift, &clamp_lo, &clamp_hi);
  }
}

static void idct32_low16_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospim42 = _mm256_set1_epi32(-cospi[42]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospim34 = _mm256_set1_epi32(-cospi[34]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i rounding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i bf1[32];

  {
    // stage 0
    // stage 1
    bf1[0] = in[0];
    bf1[2] = in[8];
    bf1[4] = in[4];
    bf1[6] = in[12];
    bf1[8] = in[2];
    bf1[10] = in[10];
    bf1[12] = in[6];
    bf1[14] = in[14];
    bf1[16] = in[1];
    bf1[18] = in[9];
    bf1[20] = in[5];
    bf1[22] = in[13];
    bf1[24] = in[3];
    bf1[26] = in[11];
    bf1[28] = in[7];
    bf1[30] = in[15];

    // stage 2
    bf1[31] = half_btf_0_avx2(&cospi2, &bf1[16], &rounding, bit);
    bf1[16] = half_btf_0_avx2(&cospi62, &bf1[16], &rounding, bit);
    bf1[17] = half_btf_0_avx2(&cospim34, &bf1[30], &rounding, bit);
    bf1[30] = half_btf_0_avx2(&cospi30, &bf1[30], &rounding, bit);
    bf1[29] = half_btf_0_avx2(&cospi18, &bf1[18], &rounding, bit);
    bf1[18] = half_btf_0_avx2(&cospi46, &bf1[18], &rounding, bit);
    bf1[19] = half_btf_0_avx2(&cospim50, &bf1[28], &rounding, bit);
    bf1[28] = half_btf_0_avx2(&cospi14, &bf1[28], &rounding, bit);
    bf1[27] = half_btf_0_avx2(&cospi10, &bf1[20], &rounding, bit);
    bf1[20] = half_btf_0_avx2(&cospi54, &bf1[20], &rounding, bit);
    bf1[21] = half_btf_0_avx2(&cospim42, &bf1[26], &rounding, bit);
    bf1[26] = half_btf_0_avx2(&cospi22, &bf1[26], &rounding, bit);
    bf1[25] = half_btf_0_avx2(&cospi26, &bf1[22], &rounding, bit);
    bf1[22] = half_btf_0_avx2(&cospi38, &bf1[22], &rounding, bit);
    bf1[23] = half_btf_0_avx2(&cospim58, &bf1[24], &rounding, bit);
    bf1[24] = half_btf_0_avx2(&cospi6, &bf1[24], &rounding, bit);

    // stage 3
    bf1[15] = half_btf_0_avx2(&cospi4, &bf1[8], &rounding, bit);
    bf1[8] = half_btf_0_avx2(&cospi60, &bf1[8], &rounding, bit);
    bf1[9] = half_btf_0_avx2(&cospim36, &bf1[14], &rounding, bit);
    bf1[14] = half_btf_0_avx2(&cospi28, &bf1[14], &rounding, bit);
    bf1[13] = half_btf_0_avx2(&cospi20, &bf1[10], &rounding, bit);
    bf1[10] = half_btf_0_avx2(&cospi44, &bf1[10], &rounding, bit);
    bf1[11] = half_btf_0_avx2(&cospim52, &bf1[12], &rounding, bit);
    bf1[12] = half_btf_0_avx2(&cospi12, &bf1[12], &rounding, bit);

    addsub_avx2(bf1[16], bf1[17], bf1 + 16, bf1 + 17, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[19], bf1[18], bf1 + 19, bf1 + 18, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[20], bf1[21], bf1 + 20, bf1 + 21, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[23], bf1[22], bf1 + 23, bf1 + 22, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[24], bf1[25], bf1 + 24, bf1 + 25, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[27], bf1[26], bf1 + 27, bf1 + 26, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[28], bf1[29], bf1 + 28, bf1 + 29, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[31], bf1[30], bf1 + 31, bf1 + 30, &clamp_lo, &clamp_hi);

    // stage 4
    bf1[7] = half_btf_0_avx2(&cospi8, &bf1[4], &rounding, bit);
    bf1[4] = half_btf_0_avx2(&cospi56, &bf1[4], &rounding, bit);
    bf1[5] = half_btf_0_avx2(&cospim40, &bf1[6], &rounding, bit);
    bf1[6] = half_btf_0_avx2(&cospi24, &bf1[6], &rounding, bit);

    addsub_avx2(bf1[8], bf1[9], bf1 + 8, bf1 + 9, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[11], bf1[10], bf1 + 11, bf1 + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[12], bf1[13], bf1 + 12, bf1 + 13, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[15], bf1[14], bf1 + 15, bf1 + 14, &clamp_lo, &clamp_hi);

    idct32_stage4_avx2(bf1, &cospim8, &cospi56, &cospi8, &cospim56, &cospim40,
                       &cospi24, &cospi40, &cospim24, &rounding, bit);

    // stage 5
    bf1[0] = half_btf_0_avx2(&cospi32, &bf1[0], &rounding, bit);
    bf1[1] = bf1[0];
    bf1[3] = half_btf_0_avx2(&cospi16, &bf1[2], &rounding, bit);
    bf1[2] = half_btf_0_avx2(&cospi48, &bf1[2], &rounding, bit);

    addsub_avx2(bf1[4], bf1[5], bf1 + 4, bf1 + 5, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[7], bf1[6], bf1 + 7, bf1 + 6, &clamp_lo, &clamp_hi);

    idct32_stage5_avx2(bf1, &cospim16, &cospi48, &cospi16, &cospim48, &clamp_lo,
                       &clamp_hi, &rounding, bit);

    // stage 6
    addsub_avx2(bf1[0], bf1[3], bf1 + 0, bf1 + 3, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[1], bf1[2], bf1 + 1, bf1 + 2, &clamp_lo, &clamp_hi);

    idct32_stage6_avx2(bf1, &cospim32, &cospi32, &cospim16, &cospi48, &cospi16,
                       &cospim48, &clamp_lo, &clamp_hi, &rounding, bit);

    // stage 7
    idct32_stage7_avx2(bf1, &cospim32, &cospi32, &clamp_lo, &clamp_hi,
                       &rounding, bit);

    // stage 8
    idct32_stage8_avx2(bf1, &cospim32, &cospi32, &clamp_lo, &clamp_hi,
                       &rounding, bit);

    // stage 9
    idct32_stage9_avx2(bf1, out, do_cols, bd, out_shift, &clamp_lo, &clamp_hi);
  }
}

static void idct32_avx2(__m256i *in, __m256i *out, int bit, int do_cols, int bd,
                        int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi58 = _mm256_set1_epi32(cospi[58]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi42 = _mm256_set1_epi32(cospi[42]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi50 = _mm256_set1_epi32(cospi[50]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi34 = _mm256_set1_epi32(cospi[34]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospim26 = _mm256_set1_epi32(-cospi[26]);
  const __m256i cospim42 = _mm256_set1_epi32(-cospi[42]);
  const __m256i cospim10 = _mm256_set1_epi32(-cospi[10]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospim18 = _mm256_set1_epi32(-cospi[18]);
  const __m256i cospim34 = _mm256_set1_epi32(-cospi[34]);
  const __m256i cospim2 = _mm256_set1_epi32(-cospi[2]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i rounding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i bf1[32], bf0[32];

  {
    // stage 0
    // stage 1
    bf1[0] = in[0];
    bf1[1] = in[16];
    bf1[2] = in[8];
    bf1[3] = in[24];
    bf1[4] = in[4];
    bf1[5] = in[20];
    bf1[6] = in[12];
    bf1[7] = in[28];
    bf1[8] = in[2];
    bf1[9] = in[18];
    bf1[10] = in[10];
    bf1[11] = in[26];
    bf1[12] = in[6];
    bf1[13] = in[22];
    bf1[14] = in[14];
    bf1[15] = in[30];
    bf1[16] = in[1];
    bf1[17] = in[17];
    bf1[18] = in[9];
    bf1[19] = in[25];
    bf1[20] = in[5];
    bf1[21] = in[21];
    bf1[22] = in[13];
    bf1[23] = in[29];
    bf1[24] = in[3];
    bf1[25] = in[19];
    bf1[26] = in[11];
    bf1[27] = in[27];
    bf1[28] = in[7];
    bf1[29] = in[23];
    bf1[30] = in[15];
    bf1[31] = in[31];

    // stage 2
    bf0[0] = bf1[0];
    bf0[1] = bf1[1];
    bf0[2] = bf1[2];
    bf0[3] = bf1[3];
    bf0[4] = bf1[4];
    bf0[5] = bf1[5];
    bf0[6] = bf1[6];
    bf0[7] = bf1[7];
    bf0[8] = bf1[8];
    bf0[9] = bf1[9];
    bf0[10] = bf1[10];
    bf0[11] = bf1[11];
    bf0[12] = bf1[12];
    bf0[13] = bf1[13];
    bf0[14] = bf1[14];
    bf0[15] = bf1[15];
    bf0[16] =
        half_btf_avx2(&cospi62, &bf1[16], &cospim2, &bf1[31], &rounding, bit);
    bf0[17] =
        half_btf_avx2(&cospi30, &bf1[17], &cospim34, &bf1[30], &rounding, bit);
    bf0[18] =
        half_btf_avx2(&cospi46, &bf1[18], &cospim18, &bf1[29], &rounding, bit);
    bf0[19] =
        half_btf_avx2(&cospi14, &bf1[19], &cospim50, &bf1[28], &rounding, bit);
    bf0[20] =
        half_btf_avx2(&cospi54, &bf1[20], &cospim10, &bf1[27], &rounding, bit);
    bf0[21] =
        half_btf_avx2(&cospi22, &bf1[21], &cospim42, &bf1[26], &rounding, bit);
    bf0[22] =
        half_btf_avx2(&cospi38, &bf1[22], &cospim26, &bf1[25], &rounding, bit);
    bf0[23] =
        half_btf_avx2(&cospi6, &bf1[23], &cospim58, &bf1[24], &rounding, bit);
    bf0[24] =
        half_btf_avx2(&cospi58, &bf1[23], &cospi6, &bf1[24], &rounding, bit);
    bf0[25] =
        half_btf_avx2(&cospi26, &bf1[22], &cospi38, &bf1[25], &rounding, bit);
    bf0[26] =
        half_btf_avx2(&cospi42, &bf1[21], &cospi22, &bf1[26], &rounding, bit);
    bf0[27] =
        half_btf_avx2(&cospi10, &bf1[20], &cospi54, &bf1[27], &rounding, bit);
    bf0[28] =
        half_btf_avx2(&cospi50, &bf1[19], &cospi14, &bf1[28], &rounding, bit);
    bf0[29] =
        half_btf_avx2(&cospi18, &bf1[18], &cospi46, &bf1[29], &rounding, bit);
    bf0[30] =
        half_btf_avx2(&cospi34, &bf1[17], &cospi30, &bf1[30], &rounding, bit);
    bf0[31] =
        half_btf_avx2(&cospi2, &bf1[16], &cospi62, &bf1[31], &rounding, bit);

    // stage 3
    bf1[0] = bf0[0];
    bf1[1] = bf0[1];
    bf1[2] = bf0[2];
    bf1[3] = bf0[3];
    bf1[4] = bf0[4];
    bf1[5] = bf0[5];
    bf1[6] = bf0[6];
    bf1[7] = bf0[7];
    bf1[8] =
        half_btf_avx2(&cospi60, &bf0[8], &cospim4, &bf0[15], &rounding, bit);
    bf1[9] =
        half_btf_avx2(&cospi28, &bf0[9], &cospim36, &bf0[14], &rounding, bit);
    bf1[10] =
        half_btf_avx2(&cospi44, &bf0[10], &cospim20, &bf0[13], &rounding, bit);
    bf1[11] =
        half_btf_avx2(&cospi12, &bf0[11], &cospim52, &bf0[12], &rounding, bit);
    bf1[12] =
        half_btf_avx2(&cospi52, &bf0[11], &cospi12, &bf0[12], &rounding, bit);
    bf1[13] =
        half_btf_avx2(&cospi20, &bf0[10], &cospi44, &bf0[13], &rounding, bit);
    bf1[14] =
        half_btf_avx2(&cospi36, &bf0[9], &cospi28, &bf0[14], &rounding, bit);
    bf1[15] =
        half_btf_avx2(&cospi4, &bf0[8], &cospi60, &bf0[15], &rounding, bit);

    addsub_avx2(bf0[16], bf0[17], bf1 + 16, bf1 + 17, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[19], bf0[18], bf1 + 19, bf1 + 18, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[20], bf0[21], bf1 + 20, bf1 + 21, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[23], bf0[22], bf1 + 23, bf1 + 22, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[24], bf0[25], bf1 + 24, bf1 + 25, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[27], bf0[26], bf1 + 27, bf1 + 26, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[28], bf0[29], bf1 + 28, bf1 + 29, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[31], bf0[30], bf1 + 31, bf1 + 30, &clamp_lo, &clamp_hi);

    // stage 4
    bf0[0] = bf1[0];
    bf0[1] = bf1[1];
    bf0[2] = bf1[2];
    bf0[3] = bf1[3];
    bf0[4] =
        half_btf_avx2(&cospi56, &bf1[4], &cospim8, &bf1[7], &rounding, bit);
    bf0[5] =
        half_btf_avx2(&cospi24, &bf1[5], &cospim40, &bf1[6], &rounding, bit);
    bf0[6] =
        half_btf_avx2(&cospi40, &bf1[5], &cospi24, &bf1[6], &rounding, bit);
    bf0[7] = half_btf_avx2(&cospi8, &bf1[4], &cospi56, &bf1[7], &rounding, bit);

    addsub_avx2(bf1[8], bf1[9], bf0 + 8, bf0 + 9, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[11], bf1[10], bf0 + 11, bf0 + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[12], bf1[13], bf0 + 12, bf0 + 13, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[15], bf1[14], bf0 + 15, bf0 + 14, &clamp_lo, &clamp_hi);

    bf0[16] = bf1[16];
    bf0[17] =
        half_btf_avx2(&cospim8, &bf1[17], &cospi56, &bf1[30], &rounding, bit);
    bf0[18] =
        half_btf_avx2(&cospim56, &bf1[18], &cospim8, &bf1[29], &rounding, bit);
    bf0[19] = bf1[19];
    bf0[20] = bf1[20];
    bf0[21] =
        half_btf_avx2(&cospim40, &bf1[21], &cospi24, &bf1[26], &rounding, bit);
    bf0[22] =
        half_btf_avx2(&cospim24, &bf1[22], &cospim40, &bf1[25], &rounding, bit);
    bf0[23] = bf1[23];
    bf0[24] = bf1[24];
    bf0[25] =
        half_btf_avx2(&cospim40, &bf1[22], &cospi24, &bf1[25], &rounding, bit);
    bf0[26] =
        half_btf_avx2(&cospi24, &bf1[21], &cospi40, &bf1[26], &rounding, bit);
    bf0[27] = bf1[27];
    bf0[28] = bf1[28];
    bf0[29] =
        half_btf_avx2(&cospim8, &bf1[18], &cospi56, &bf1[29], &rounding, bit);
    bf0[30] =
        half_btf_avx2(&cospi56, &bf1[17], &cospi8, &bf1[30], &rounding, bit);
    bf0[31] = bf1[31];

    // stage 5
    bf1[0] =
        half_btf_avx2(&cospi32, &bf0[0], &cospi32, &bf0[1], &rounding, bit);
    bf1[1] =
        half_btf_avx2(&cospi32, &bf0[0], &cospim32, &bf0[1], &rounding, bit);
    bf1[2] =
        half_btf_avx2(&cospi48, &bf0[2], &cospim16, &bf0[3], &rounding, bit);
    bf1[3] =
        half_btf_avx2(&cospi16, &bf0[2], &cospi48, &bf0[3], &rounding, bit);
    addsub_avx2(bf0[4], bf0[5], bf1 + 4, bf1 + 5, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[7], bf0[6], bf1 + 7, bf1 + 6, &clamp_lo, &clamp_hi);
    bf1[8] = bf0[8];
    bf1[9] =
        half_btf_avx2(&cospim16, &bf0[9], &cospi48, &bf0[14], &rounding, bit);
    bf1[10] =
        half_btf_avx2(&cospim48, &bf0[10], &cospim16, &bf0[13], &rounding, bit);
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] =
        half_btf_avx2(&cospim16, &bf0[10], &cospi48, &bf0[13], &rounding, bit);
    bf1[14] =
        half_btf_avx2(&cospi48, &bf0[9], &cospi16, &bf0[14], &rounding, bit);
    bf1[15] = bf0[15];
    addsub_avx2(bf0[16], bf0[19], bf1 + 16, bf1 + 19, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[17], bf0[18], bf1 + 17, bf1 + 18, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[23], bf0[20], bf1 + 23, bf1 + 20, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[22], bf0[21], bf1 + 22, bf1 + 21, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[24], bf0[27], bf1 + 24, bf1 + 27, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[25], bf0[26], bf1 + 25, bf1 + 26, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[31], bf0[28], bf1 + 31, bf1 + 28, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[30], bf0[29], bf1 + 30, bf1 + 29, &clamp_lo, &clamp_hi);

    // stage 6
    addsub_avx2(bf1[0], bf1[3], bf0 + 0, bf0 + 3, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[1], bf1[2], bf0 + 1, bf0 + 2, &clamp_lo, &clamp_hi);
    bf0[4] = bf1[4];
    bf0[5] =
        half_btf_avx2(&cospim32, &bf1[5], &cospi32, &bf1[6], &rounding, bit);
    bf0[6] =
        half_btf_avx2(&cospi32, &bf1[5], &cospi32, &bf1[6], &rounding, bit);
    bf0[7] = bf1[7];
    addsub_avx2(bf1[8], bf1[11], bf0 + 8, bf0 + 11, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[9], bf1[10], bf0 + 9, bf0 + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[15], bf1[12], bf0 + 15, bf0 + 12, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[14], bf1[13], bf0 + 14, bf0 + 13, &clamp_lo, &clamp_hi);
    bf0[16] = bf1[16];
    bf0[17] = bf1[17];
    bf0[18] =
        half_btf_avx2(&cospim16, &bf1[18], &cospi48, &bf1[29], &rounding, bit);
    bf0[19] =
        half_btf_avx2(&cospim16, &bf1[19], &cospi48, &bf1[28], &rounding, bit);
    bf0[20] =
        half_btf_avx2(&cospim48, &bf1[20], &cospim16, &bf1[27], &rounding, bit);
    bf0[21] =
        half_btf_avx2(&cospim48, &bf1[21], &cospim16, &bf1[26], &rounding, bit);
    bf0[22] = bf1[22];
    bf0[23] = bf1[23];
    bf0[24] = bf1[24];
    bf0[25] = bf1[25];
    bf0[26] =
        half_btf_avx2(&cospim16, &bf1[21], &cospi48, &bf1[26], &rounding, bit);
    bf0[27] =
        half_btf_avx2(&cospim16, &bf1[20], &cospi48, &bf1[27], &rounding, bit);
    bf0[28] =
        half_btf_avx2(&cospi48, &bf1[19], &cospi16, &bf1[28], &rounding, bit);
    bf0[29] =
        half_btf_avx2(&cospi48, &bf1[18], &cospi16, &bf1[29], &rounding, bit);
    bf0[30] = bf1[30];
    bf0[31] = bf1[31];

    // stage 7
    addsub_avx2(bf0[0], bf0[7], bf1 + 0, bf1 + 7, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[1], bf0[6], bf1 + 1, bf1 + 6, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[2], bf0[5], bf1 + 2, bf1 + 5, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[3], bf0[4], bf1 + 3, bf1 + 4, &clamp_lo, &clamp_hi);
    bf1[8] = bf0[8];
    bf1[9] = bf0[9];
    bf1[10] =
        half_btf_avx2(&cospim32, &bf0[10], &cospi32, &bf0[13], &rounding, bit);
    bf1[11] =
        half_btf_avx2(&cospim32, &bf0[11], &cospi32, &bf0[12], &rounding, bit);
    bf1[12] =
        half_btf_avx2(&cospi32, &bf0[11], &cospi32, &bf0[12], &rounding, bit);
    bf1[13] =
        half_btf_avx2(&cospi32, &bf0[10], &cospi32, &bf0[13], &rounding, bit);
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    addsub_avx2(bf0[16], bf0[23], bf1 + 16, bf1 + 23, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[17], bf0[22], bf1 + 17, bf1 + 22, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[18], bf0[21], bf1 + 18, bf1 + 21, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[19], bf0[20], bf1 + 19, bf1 + 20, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[31], bf0[24], bf1 + 31, bf1 + 24, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[30], bf0[25], bf1 + 30, bf1 + 25, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[29], bf0[26], bf1 + 29, bf1 + 26, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[28], bf0[27], bf1 + 28, bf1 + 27, &clamp_lo, &clamp_hi);

    // stage 8
    addsub_avx2(bf1[0], bf1[15], bf0 + 0, bf0 + 15, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[1], bf1[14], bf0 + 1, bf0 + 14, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[2], bf1[13], bf0 + 2, bf0 + 13, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[3], bf1[12], bf0 + 3, bf0 + 12, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[4], bf1[11], bf0 + 4, bf0 + 11, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[5], bf1[10], bf0 + 5, bf0 + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[6], bf1[9], bf0 + 6, bf0 + 9, &clamp_lo, &clamp_hi);
    addsub_avx2(bf1[7], bf1[8], bf0 + 7, bf0 + 8, &clamp_lo, &clamp_hi);
    bf0[16] = bf1[16];
    bf0[17] = bf1[17];
    bf0[18] = bf1[18];
    bf0[19] = bf1[19];
    bf0[20] =
        half_btf_avx2(&cospim32, &bf1[20], &cospi32, &bf1[27], &rounding, bit);
    bf0[21] =
        half_btf_avx2(&cospim32, &bf1[21], &cospi32, &bf1[26], &rounding, bit);
    bf0[22] =
        half_btf_avx2(&cospim32, &bf1[22], &cospi32, &bf1[25], &rounding, bit);
    bf0[23] =
        half_btf_avx2(&cospim32, &bf1[23], &cospi32, &bf1[24], &rounding, bit);
    bf0[24] =
        half_btf_avx2(&cospi32, &bf1[23], &cospi32, &bf1[24], &rounding, bit);
    bf0[25] =
        half_btf_avx2(&cospi32, &bf1[22], &cospi32, &bf1[25], &rounding, bit);
    bf0[26] =
        half_btf_avx2(&cospi32, &bf1[21], &cospi32, &bf1[26], &rounding, bit);
    bf0[27] =
        half_btf_avx2(&cospi32, &bf1[20], &cospi32, &bf1[27], &rounding, bit);
    bf0[28] = bf1[28];
    bf0[29] = bf1[29];
    bf0[30] = bf1[30];
    bf0[31] = bf1[31];

    // stage 9
    addsub_avx2(bf0[0], bf0[31], out + 0, out + 31, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[1], bf0[30], out + 1, out + 30, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[2], bf0[29], out + 2, out + 29, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[3], bf0[28], out + 3, out + 28, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[4], bf0[27], out + 4, out + 27, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[5], bf0[26], out + 5, out + 26, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[6], bf0[25], out + 6, out + 25, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[7], bf0[24], out + 7, out + 24, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[8], bf0[23], out + 8, out + 23, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[9], bf0[22], out + 9, out + 22, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[10], bf0[21], out + 10, out + 21, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[11], bf0[20], out + 11, out + 20, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[12], bf0[19], out + 12, out + 19, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[13], bf0[18], out + 13, out + 18, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[14], bf0[17], out + 14, out + 17, &clamp_lo, &clamp_hi);
    addsub_avx2(bf0[15], bf0[16], out + 15, out + 16, &clamp_lo, &clamp_hi);
    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
      round_shift_8x8_avx2(out, out_shift);
      round_shift_8x8_avx2(out + 16, out_shift);
      highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 32);
    }
  }
}
static void idct16_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);

  {
    // stage 0
    // stage 1
    // stage 2
    // stage 3
    // stage 4
    in[0] = _mm256_mullo_epi32(in[0], cospi32);
    in[0] = _mm256_add_epi32(in[0], rnding);
    in[0] = _mm256_srai_epi32(in[0], bit);

    // stage 5
    // stage 6
    // stage 7
    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      clamp_lo = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      clamp_hi = _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
      __m256i offset = _mm256_set1_epi32((1 << out_shift) >> 1);
      in[0] = _mm256_add_epi32(in[0], offset);
      in[0] = _mm256_sra_epi32(in[0], _mm_cvtsi32_si128(out_shift));
    }
    in[0] = _mm256_max_epi32(in[0], clamp_lo);
    in[0] = _mm256_min_epi32(in[0], clamp_hi);
    out[0] = in[0];
    out[1] = in[0];
    out[2] = in[0];
    out[3] = in[0];
    out[4] = in[0];
    out[5] = in[0];
    out[6] = in[0];
    out[7] = in[0];
    out[8] = in[0];
    out[9] = in[0];
    out[10] = in[0];
    out[11] = in[0];
    out[12] = in[0];
    out[13] = in[0];
    out[14] = in[0];
    out[15] = in[0];
  }
}

static void idct16_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u[16], x, y;

  {
    // stage 0
    // stage 1
    u[0] = in[0];
    u[2] = in[4];
    u[4] = in[2];
    u[6] = in[6];
    u[8] = in[1];
    u[10] = in[5];
    u[12] = in[3];
    u[14] = in[7];

    // stage 2
    u[15] = half_btf_0_avx2(&cospi4, &u[8], &rnding, bit);
    u[8] = half_btf_0_avx2(&cospi60, &u[8], &rnding, bit);

    u[9] = half_btf_0_avx2(&cospim36, &u[14], &rnding, bit);
    u[14] = half_btf_0_avx2(&cospi28, &u[14], &rnding, bit);

    u[13] = half_btf_0_avx2(&cospi20, &u[10], &rnding, bit);
    u[10] = half_btf_0_avx2(&cospi44, &u[10], &rnding, bit);

    u[11] = half_btf_0_avx2(&cospim52, &u[12], &rnding, bit);
    u[12] = half_btf_0_avx2(&cospi12, &u[12], &rnding, bit);

    // stage 3
    u[7] = half_btf_0_avx2(&cospi8, &u[4], &rnding, bit);
    u[4] = half_btf_0_avx2(&cospi56, &u[4], &rnding, bit);
    u[5] = half_btf_0_avx2(&cospim40, &u[6], &rnding, bit);
    u[6] = half_btf_0_avx2(&cospi24, &u[6], &rnding, bit);

    addsub_avx2(u[8], u[9], &u[8], &u[9], &clamp_lo, &clamp_hi);
    addsub_avx2(u[11], u[10], &u[11], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(u[12], u[13], &u[12], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(u[15], u[14], &u[15], &u[14], &clamp_lo, &clamp_hi);

    // stage 4
    x = _mm256_mullo_epi32(u[0], cospi32);
    u[0] = _mm256_add_epi32(x, rnding);
    u[0] = _mm256_srai_epi32(u[0], bit);
    u[1] = u[0];

    u[3] = half_btf_0_avx2(&cospi16, &u[2], &rnding, bit);
    u[2] = half_btf_0_avx2(&cospi48, &u[2], &rnding, bit);

    addsub_avx2(u[4], u[5], &u[4], &u[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[7], u[6], &u[7], &u[6], &clamp_lo, &clamp_hi);

    x = half_btf_avx2(&cospim16, &u[9], &cospi48, &u[14], &rnding, bit);
    u[14] = half_btf_avx2(&cospi48, &u[9], &cospi16, &u[14], &rnding, bit);
    u[9] = x;
    y = half_btf_avx2(&cospim48, &u[10], &cospim16, &u[13], &rnding, bit);
    u[13] = half_btf_avx2(&cospim16, &u[10], &cospi48, &u[13], &rnding, bit);
    u[10] = y;

    // stage 5
    addsub_avx2(u[0], u[3], &u[0], &u[3], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[2], &u[1], &u[2], &clamp_lo, &clamp_hi);

    x = _mm256_mullo_epi32(u[5], cospi32);
    y = _mm256_mullo_epi32(u[6], cospi32);
    u[5] = _mm256_sub_epi32(y, x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    u[5] = _mm256_srai_epi32(u[5], bit);

    u[6] = _mm256_add_epi32(y, x);
    u[6] = _mm256_add_epi32(u[6], rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    addsub_avx2(u[8], u[11], &u[8], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(u[9], u[10], &u[9], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(u[15], u[12], &u[15], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(u[14], u[13], &u[14], &u[13], &clamp_lo, &clamp_hi);

    // stage 6
    addsub_avx2(u[0], u[7], &u[0], &u[7], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[6], &u[1], &u[6], &clamp_lo, &clamp_hi);
    addsub_avx2(u[2], u[5], &u[2], &u[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[3], u[4], &u[3], &u[4], &clamp_lo, &clamp_hi);

    x = _mm256_mullo_epi32(u[10], cospi32);
    y = _mm256_mullo_epi32(u[13], cospi32);
    u[10] = _mm256_sub_epi32(y, x);
    u[10] = _mm256_add_epi32(u[10], rnding);
    u[10] = _mm256_srai_epi32(u[10], bit);

    u[13] = _mm256_add_epi32(x, y);
    u[13] = _mm256_add_epi32(u[13], rnding);
    u[13] = _mm256_srai_epi32(u[13], bit);

    x = _mm256_mullo_epi32(u[11], cospi32);
    y = _mm256_mullo_epi32(u[12], cospi32);
    u[11] = _mm256_sub_epi32(y, x);
    u[11] = _mm256_add_epi32(u[11], rnding);
    u[11] = _mm256_srai_epi32(u[11], bit);

    u[12] = _mm256_add_epi32(x, y);
    u[12] = _mm256_add_epi32(u[12], rnding);
    u[12] = _mm256_srai_epi32(u[12], bit);
    // stage 7
    addsub_avx2(u[0], u[15], out + 0, out + 15, &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[14], out + 1, out + 14, &clamp_lo, &clamp_hi);
    addsub_avx2(u[2], u[13], out + 2, out + 13, &clamp_lo, &clamp_hi);
    addsub_avx2(u[3], u[12], out + 3, out + 12, &clamp_lo, &clamp_hi);
    addsub_avx2(u[4], u[11], out + 4, out + 11, &clamp_lo, &clamp_hi);
    addsub_avx2(u[5], u[10], out + 5, out + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(u[6], u[9], out + 6, out + 9, &clamp_lo, &clamp_hi);
    addsub_avx2(u[7], u[8], out + 7, out + 8, &clamp_lo, &clamp_hi);

    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
      round_shift_8x8_avx2(out, out_shift);
      highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 16);
    }
  }
}

static void idct16_avx2(__m256i *in, __m256i *out, int bit, int do_cols, int bd,
                        int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u[16], v[16], x, y;

  {
    // stage 0
    // stage 1
    u[0] = in[0];
    u[1] = in[8];
    u[2] = in[4];
    u[3] = in[12];
    u[4] = in[2];
    u[5] = in[10];
    u[6] = in[6];
    u[7] = in[14];
    u[8] = in[1];
    u[9] = in[9];
    u[10] = in[5];
    u[11] = in[13];
    u[12] = in[3];
    u[13] = in[11];
    u[14] = in[7];
    u[15] = in[15];

    // stage 2
    v[0] = u[0];
    v[1] = u[1];
    v[2] = u[2];
    v[3] = u[3];
    v[4] = u[4];
    v[5] = u[5];
    v[6] = u[6];
    v[7] = u[7];

    v[8] = half_btf_avx2(&cospi60, &u[8], &cospim4, &u[15], &rnding, bit);
    v[9] = half_btf_avx2(&cospi28, &u[9], &cospim36, &u[14], &rnding, bit);
    v[10] = half_btf_avx2(&cospi44, &u[10], &cospim20, &u[13], &rnding, bit);
    v[11] = half_btf_avx2(&cospi12, &u[11], &cospim52, &u[12], &rnding, bit);
    v[12] = half_btf_avx2(&cospi52, &u[11], &cospi12, &u[12], &rnding, bit);
    v[13] = half_btf_avx2(&cospi20, &u[10], &cospi44, &u[13], &rnding, bit);
    v[14] = half_btf_avx2(&cospi36, &u[9], &cospi28, &u[14], &rnding, bit);
    v[15] = half_btf_avx2(&cospi4, &u[8], &cospi60, &u[15], &rnding, bit);

    // stage 3
    u[0] = v[0];
    u[1] = v[1];
    u[2] = v[2];
    u[3] = v[3];
    u[4] = half_btf_avx2(&cospi56, &v[4], &cospim8, &v[7], &rnding, bit);
    u[5] = half_btf_avx2(&cospi24, &v[5], &cospim40, &v[6], &rnding, bit);
    u[6] = half_btf_avx2(&cospi40, &v[5], &cospi24, &v[6], &rnding, bit);
    u[7] = half_btf_avx2(&cospi8, &v[4], &cospi56, &v[7], &rnding, bit);
    addsub_avx2(v[8], v[9], &u[8], &u[9], &clamp_lo, &clamp_hi);
    addsub_avx2(v[11], v[10], &u[11], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(v[12], v[13], &u[12], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(v[15], v[14], &u[15], &u[14], &clamp_lo, &clamp_hi);

    // stage 4
    x = _mm256_mullo_epi32(u[0], cospi32);
    y = _mm256_mullo_epi32(u[1], cospi32);
    v[0] = _mm256_add_epi32(x, y);
    v[0] = _mm256_add_epi32(v[0], rnding);
    v[0] = _mm256_srai_epi32(v[0], bit);

    v[1] = _mm256_sub_epi32(x, y);
    v[1] = _mm256_add_epi32(v[1], rnding);
    v[1] = _mm256_srai_epi32(v[1], bit);

    v[2] = half_btf_avx2(&cospi48, &u[2], &cospim16, &u[3], &rnding, bit);
    v[3] = half_btf_avx2(&cospi16, &u[2], &cospi48, &u[3], &rnding, bit);
    addsub_avx2(u[4], u[5], &v[4], &v[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[7], u[6], &v[7], &v[6], &clamp_lo, &clamp_hi);
    v[8] = u[8];
    v[9] = half_btf_avx2(&cospim16, &u[9], &cospi48, &u[14], &rnding, bit);
    v[10] = half_btf_avx2(&cospim48, &u[10], &cospim16, &u[13], &rnding, bit);
    v[11] = u[11];
    v[12] = u[12];
    v[13] = half_btf_avx2(&cospim16, &u[10], &cospi48, &u[13], &rnding, bit);
    v[14] = half_btf_avx2(&cospi48, &u[9], &cospi16, &u[14], &rnding, bit);
    v[15] = u[15];

    // stage 5
    addsub_avx2(v[0], v[3], &u[0], &u[3], &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[2], &u[1], &u[2], &clamp_lo, &clamp_hi);
    u[4] = v[4];

    x = _mm256_mullo_epi32(v[5], cospi32);
    y = _mm256_mullo_epi32(v[6], cospi32);
    u[5] = _mm256_sub_epi32(y, x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    u[5] = _mm256_srai_epi32(u[5], bit);

    u[6] = _mm256_add_epi32(y, x);
    u[6] = _mm256_add_epi32(u[6], rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    u[7] = v[7];
    addsub_avx2(v[8], v[11], &u[8], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(v[9], v[10], &u[9], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(v[15], v[12], &u[15], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(v[14], v[13], &u[14], &u[13], &clamp_lo, &clamp_hi);

    // stage 6
    addsub_avx2(u[0], u[7], &v[0], &v[7], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[6], &v[1], &v[6], &clamp_lo, &clamp_hi);
    addsub_avx2(u[2], u[5], &v[2], &v[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[3], u[4], &v[3], &v[4], &clamp_lo, &clamp_hi);
    v[8] = u[8];
    v[9] = u[9];

    x = _mm256_mullo_epi32(u[10], cospi32);
    y = _mm256_mullo_epi32(u[13], cospi32);
    v[10] = _mm256_sub_epi32(y, x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[13] = _mm256_add_epi32(x, y);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    x = _mm256_mullo_epi32(u[11], cospi32);
    y = _mm256_mullo_epi32(u[12], cospi32);
    v[11] = _mm256_sub_epi32(y, x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = _mm256_add_epi32(x, y);
    v[12] = _mm256_add_epi32(v[12], rnding);
    v[12] = _mm256_srai_epi32(v[12], bit);

    v[14] = u[14];
    v[15] = u[15];

    // stage 7
    addsub_avx2(v[0], v[15], out + 0, out + 15, &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[14], out + 1, out + 14, &clamp_lo, &clamp_hi);
    addsub_avx2(v[2], v[13], out + 2, out + 13, &clamp_lo, &clamp_hi);
    addsub_avx2(v[3], v[12], out + 3, out + 12, &clamp_lo, &clamp_hi);
    addsub_avx2(v[4], v[11], out + 4, out + 11, &clamp_lo, &clamp_hi);
    addsub_avx2(v[5], v[10], out + 5, out + 10, &clamp_lo, &clamp_hi);
    addsub_avx2(v[6], v[9], out + 6, out + 9, &clamp_lo, &clamp_hi);
    addsub_avx2(v[7], v[8], out + 7, out + 8, &clamp_lo, &clamp_hi);

    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
      round_shift_8x8_avx2(out, out_shift);
      highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 16);
    }
  }
}

#if CONFIG_INTER_DDT
static void iddt16_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  (void)bit;
#if CONFIG_FIX_INTER_DDT_PRECISION
  iadst_matrix_mult_avx2(in, out, INV_DDT_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 1);
#else
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 1);
#endif  // CONFIG_FIX_INTER_DDT_PRECISION
}
#endif  // CONFIG_INTER_DDT
#if CONFIG_ADST_TUNED && USE_TUNED_ADST16
static void iadst16_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  (void)bit;
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         av2_adst_kernel16[INV_TXFM], tx_size_wide[TX_16X16],
                         1);
}
#else
static void iadst16_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const __m256i zero = _mm256_setzero_si256();
  __m256i v[16], x, y, temp1, temp2;

  // Calculate the column 0, 1, 2, 3
  {
    // stage 0
    // stage 1
    // stage 2
    x = _mm256_mullo_epi32(in[0], cospi62);
    v[0] = _mm256_add_epi32(x, rnding);
    v[0] = _mm256_srai_epi32(v[0], bit);

    x = _mm256_mullo_epi32(in[0], cospi2);
    v[1] = _mm256_sub_epi32(zero, x);
    v[1] = _mm256_add_epi32(v[1], rnding);
    v[1] = _mm256_srai_epi32(v[1], bit);

    // stage 3
    v[8] = v[0];
    v[9] = v[1];

    // stage 4
    temp1 = _mm256_mullo_epi32(v[8], cospi8);
    x = _mm256_mullo_epi32(v[9], cospi56);
    temp1 = _mm256_add_epi32(temp1, x);
    temp1 = _mm256_add_epi32(temp1, rnding);
    temp1 = _mm256_srai_epi32(temp1, bit);

    temp2 = _mm256_mullo_epi32(v[8], cospi56);
    x = _mm256_mullo_epi32(v[9], cospi8);
    temp2 = _mm256_sub_epi32(temp2, x);
    temp2 = _mm256_add_epi32(temp2, rnding);
    temp2 = _mm256_srai_epi32(temp2, bit);
    v[8] = temp1;
    v[9] = temp2;

    // stage 5
    v[4] = v[0];
    v[5] = v[1];
    v[12] = v[8];
    v[13] = v[9];

    // stage 6
    temp1 = _mm256_mullo_epi32(v[4], cospi16);
    x = _mm256_mullo_epi32(v[5], cospi48);
    temp1 = _mm256_add_epi32(temp1, x);
    temp1 = _mm256_add_epi32(temp1, rnding);
    temp1 = _mm256_srai_epi32(temp1, bit);

    temp2 = _mm256_mullo_epi32(v[4], cospi48);
    x = _mm256_mullo_epi32(v[5], cospi16);
    temp2 = _mm256_sub_epi32(temp2, x);
    temp2 = _mm256_add_epi32(temp2, rnding);
    temp2 = _mm256_srai_epi32(temp2, bit);
    v[4] = temp1;
    v[5] = temp2;

    temp1 = _mm256_mullo_epi32(v[12], cospi16);
    x = _mm256_mullo_epi32(v[13], cospi48);
    temp1 = _mm256_add_epi32(temp1, x);
    temp1 = _mm256_add_epi32(temp1, rnding);
    temp1 = _mm256_srai_epi32(temp1, bit);

    temp2 = _mm256_mullo_epi32(v[12], cospi48);
    x = _mm256_mullo_epi32(v[13], cospi16);
    temp2 = _mm256_sub_epi32(temp2, x);
    temp2 = _mm256_add_epi32(temp2, rnding);
    temp2 = _mm256_srai_epi32(temp2, bit);
    v[12] = temp1;
    v[13] = temp2;

    // stage 7
    v[2] = v[0];
    v[3] = v[1];
    v[6] = v[4];
    v[7] = v[5];
    v[10] = v[8];
    v[11] = v[9];
    v[14] = v[12];
    v[15] = v[13];

    // stage 8
    y = _mm256_mullo_epi32(v[2], cospi32);
    x = _mm256_mullo_epi32(v[3], cospi32);
    v[2] = _mm256_add_epi32(y, x);
    v[2] = _mm256_add_epi32(v[2], rnding);
    v[2] = _mm256_srai_epi32(v[2], bit);

    v[3] = _mm256_sub_epi32(y, x);
    v[3] = _mm256_add_epi32(v[3], rnding);
    v[3] = _mm256_srai_epi32(v[3], bit);

    y = _mm256_mullo_epi32(v[6], cospi32);
    x = _mm256_mullo_epi32(v[7], cospi32);
    v[6] = _mm256_add_epi32(y, x);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    v[7] = _mm256_sub_epi32(y, x);
    v[7] = _mm256_add_epi32(v[7], rnding);
    v[7] = _mm256_srai_epi32(v[7], bit);

    y = _mm256_mullo_epi32(v[10], cospi32);
    x = _mm256_mullo_epi32(v[11], cospi32);
    v[10] = _mm256_add_epi32(y, x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[11] = _mm256_sub_epi32(y, x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    y = _mm256_mullo_epi32(v[14], cospi32);
    x = _mm256_mullo_epi32(v[15], cospi32);
    v[14] = _mm256_add_epi32(y, x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_sub_epi32(y, x);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 9
    if (do_cols) {
      out[0] = v[0];
      out[1] = _mm256_sub_epi32(_mm256_setzero_si256(), v[8]);
      out[2] = v[12];
      out[3] = _mm256_sub_epi32(_mm256_setzero_si256(), v[4]);
      out[4] = v[6];
      out[5] = _mm256_sub_epi32(_mm256_setzero_si256(), v[14]);
      out[6] = v[10];
      out[7] = _mm256_sub_epi32(_mm256_setzero_si256(), v[2]);
      out[8] = v[3];
      out[9] = _mm256_sub_epi32(_mm256_setzero_si256(), v[11]);
      out[10] = v[15];
      out[11] = _mm256_sub_epi32(_mm256_setzero_si256(), v[7]);
      out[12] = v[5];
      out[13] = _mm256_sub_epi32(_mm256_setzero_si256(), v[13]);
      out[14] = v[9];
      out[15] = _mm256_sub_epi32(_mm256_setzero_si256(), v[1]);
    } else {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

      neg_shift_avx2(v[0], v[8], out + 0, out + 1, &clamp_lo_out, &clamp_hi_out,
                     out_shift);
      neg_shift_avx2(v[12], v[4], out + 2, out + 3, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[6], v[14], out + 4, out + 5, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[10], v[2], out + 6, out + 7, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[3], v[11], out + 8, out + 9, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[15], v[7], out + 10, out + 11, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[5], v[13], out + 12, out + 13, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[9], v[1], out + 14, out + 15, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
    }
  }
}
#endif  // CONFIG_ADST_TUNED && USE_TUNED_ADST16

#if CONFIG_INTER_DDT
static void iddt16_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  (void)bit;
#if CONFIG_FIX_INTER_DDT_PRECISION
  iadst_matrix_mult_avx2(in, out, INV_DDT_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 8);
#else
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 8);
#endif  // CONFIG_FIX_INTER_DDT_PRECISION
}
#endif  // CONFIG_INTER_DDT

#if CONFIG_ADST_TUNED && USE_TUNED_ADST16
static void iadst16_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  (void)bit;
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         av2_adst_kernel16[INV_TXFM], tx_size_wide[TX_16X16],
                         8);
}
#else
static void iadst16_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospi34 = _mm256_set1_epi32(cospi[34]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospi42 = _mm256_set1_epi32(cospi[42]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospi50 = _mm256_set1_epi32(cospi[50]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi58 = _mm256_set1_epi32(cospi[58]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u[16], x, y;

  {
    // stage 0
    // stage 1
    // stage 2
    __m256i zero = _mm256_setzero_si256();
    x = _mm256_mullo_epi32(in[0], cospi62);
    u[0] = _mm256_add_epi32(x, rnding);
    u[0] = _mm256_srai_epi32(u[0], bit);

    x = _mm256_mullo_epi32(in[0], cospi2);
    u[1] = _mm256_sub_epi32(zero, x);
    u[1] = _mm256_add_epi32(u[1], rnding);
    u[1] = _mm256_srai_epi32(u[1], bit);

    x = _mm256_mullo_epi32(in[2], cospi54);
    u[2] = _mm256_add_epi32(x, rnding);
    u[2] = _mm256_srai_epi32(u[2], bit);

    x = _mm256_mullo_epi32(in[2], cospi10);
    u[3] = _mm256_sub_epi32(zero, x);
    u[3] = _mm256_add_epi32(u[3], rnding);
    u[3] = _mm256_srai_epi32(u[3], bit);

    x = _mm256_mullo_epi32(in[4], cospi46);
    u[4] = _mm256_add_epi32(x, rnding);
    u[4] = _mm256_srai_epi32(u[4], bit);

    x = _mm256_mullo_epi32(in[4], cospi18);
    u[5] = _mm256_sub_epi32(zero, x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    u[5] = _mm256_srai_epi32(u[5], bit);

    x = _mm256_mullo_epi32(in[6], cospi38);
    u[6] = _mm256_add_epi32(x, rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    x = _mm256_mullo_epi32(in[6], cospi26);
    u[7] = _mm256_sub_epi32(zero, x);
    u[7] = _mm256_add_epi32(u[7], rnding);
    u[7] = _mm256_srai_epi32(u[7], bit);

    u[8] = _mm256_mullo_epi32(in[7], cospi34);
    u[8] = _mm256_add_epi32(u[8], rnding);
    u[8] = _mm256_srai_epi32(u[8], bit);

    u[9] = _mm256_mullo_epi32(in[7], cospi30);
    u[9] = _mm256_add_epi32(u[9], rnding);
    u[9] = _mm256_srai_epi32(u[9], bit);

    u[10] = _mm256_mullo_epi32(in[5], cospi42);
    u[10] = _mm256_add_epi32(u[10], rnding);
    u[10] = _mm256_srai_epi32(u[10], bit);

    u[11] = _mm256_mullo_epi32(in[5], cospi22);
    u[11] = _mm256_add_epi32(u[11], rnding);
    u[11] = _mm256_srai_epi32(u[11], bit);

    u[12] = _mm256_mullo_epi32(in[3], cospi50);
    u[12] = _mm256_add_epi32(u[12], rnding);
    u[12] = _mm256_srai_epi32(u[12], bit);

    u[13] = _mm256_mullo_epi32(in[3], cospi14);
    u[13] = _mm256_add_epi32(u[13], rnding);
    u[13] = _mm256_srai_epi32(u[13], bit);

    u[14] = _mm256_mullo_epi32(in[1], cospi58);
    u[14] = _mm256_add_epi32(u[14], rnding);
    u[14] = _mm256_srai_epi32(u[14], bit);

    u[15] = _mm256_mullo_epi32(in[1], cospi6);
    u[15] = _mm256_add_epi32(u[15], rnding);
    u[15] = _mm256_srai_epi32(u[15], bit);

    // stage 3
    addsub_avx2(u[0], u[8], &u[0], &u[8], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[9], &u[1], &u[9], &clamp_lo, &clamp_hi);
    addsub_avx2(u[2], u[10], &u[2], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(u[3], u[11], &u[3], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(u[4], u[12], &u[4], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(u[5], u[13], &u[5], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(u[6], u[14], &u[6], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(u[7], u[15], &u[7], &u[15], &clamp_lo, &clamp_hi);

    // stage 4
    y = _mm256_mullo_epi32(u[8], cospi56);
    x = _mm256_mullo_epi32(u[9], cospi56);
    u[8] = _mm256_mullo_epi32(u[8], cospi8);
    u[8] = _mm256_add_epi32(u[8], x);
    u[8] = _mm256_add_epi32(u[8], rnding);
    u[8] = _mm256_srai_epi32(u[8], bit);

    x = _mm256_mullo_epi32(u[9], cospi8);
    u[9] = _mm256_sub_epi32(y, x);
    u[9] = _mm256_add_epi32(u[9], rnding);
    u[9] = _mm256_srai_epi32(u[9], bit);

    x = _mm256_mullo_epi32(u[11], cospi24);
    y = _mm256_mullo_epi32(u[10], cospi24);
    u[10] = _mm256_mullo_epi32(u[10], cospi40);
    u[10] = _mm256_add_epi32(u[10], x);
    u[10] = _mm256_add_epi32(u[10], rnding);
    u[10] = _mm256_srai_epi32(u[10], bit);

    x = _mm256_mullo_epi32(u[11], cospi40);
    u[11] = _mm256_sub_epi32(y, x);
    u[11] = _mm256_add_epi32(u[11], rnding);
    u[11] = _mm256_srai_epi32(u[11], bit);

    x = _mm256_mullo_epi32(u[13], cospi8);
    y = _mm256_mullo_epi32(u[12], cospi8);
    u[12] = _mm256_mullo_epi32(u[12], cospim56);
    u[12] = _mm256_add_epi32(u[12], x);
    u[12] = _mm256_add_epi32(u[12], rnding);
    u[12] = _mm256_srai_epi32(u[12], bit);

    x = _mm256_mullo_epi32(u[13], cospim56);
    u[13] = _mm256_sub_epi32(y, x);
    u[13] = _mm256_add_epi32(u[13], rnding);
    u[13] = _mm256_srai_epi32(u[13], bit);

    x = _mm256_mullo_epi32(u[15], cospi40);
    y = _mm256_mullo_epi32(u[14], cospi40);
    u[14] = _mm256_mullo_epi32(u[14], cospim24);
    u[14] = _mm256_add_epi32(u[14], x);
    u[14] = _mm256_add_epi32(u[14], rnding);
    u[14] = _mm256_srai_epi32(u[14], bit);

    x = _mm256_mullo_epi32(u[15], cospim24);
    u[15] = _mm256_sub_epi32(y, x);
    u[15] = _mm256_add_epi32(u[15], rnding);
    u[15] = _mm256_srai_epi32(u[15], bit);

    // stage 5
    addsub_avx2(u[0], u[4], &u[0], &u[4], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[5], &u[1], &u[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[2], u[6], &u[2], &u[6], &clamp_lo, &clamp_hi);
    addsub_avx2(u[3], u[7], &u[3], &u[7], &clamp_lo, &clamp_hi);
    addsub_avx2(u[8], u[12], &u[8], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(u[9], u[13], &u[9], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(u[10], u[14], &u[10], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(u[11], u[15], &u[11], &u[15], &clamp_lo, &clamp_hi);

    // stage 6
    x = _mm256_mullo_epi32(u[5], cospi48);
    y = _mm256_mullo_epi32(u[4], cospi48);
    u[4] = _mm256_mullo_epi32(u[4], cospi16);
    u[4] = _mm256_add_epi32(u[4], x);
    u[4] = _mm256_add_epi32(u[4], rnding);
    u[4] = _mm256_srai_epi32(u[4], bit);

    x = _mm256_mullo_epi32(u[5], cospi16);
    u[5] = _mm256_sub_epi32(y, x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    u[5] = _mm256_srai_epi32(u[5], bit);

    x = _mm256_mullo_epi32(u[7], cospi16);
    y = _mm256_mullo_epi32(u[6], cospi16);
    u[6] = _mm256_mullo_epi32(u[6], cospim48);
    u[6] = _mm256_add_epi32(u[6], x);
    u[6] = _mm256_add_epi32(u[6], rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    x = _mm256_mullo_epi32(u[7], cospim48);
    u[7] = _mm256_sub_epi32(y, x);
    u[7] = _mm256_add_epi32(u[7], rnding);
    u[7] = _mm256_srai_epi32(u[7], bit);

    x = _mm256_mullo_epi32(u[13], cospi48);
    y = _mm256_mullo_epi32(u[12], cospi48);
    u[12] = _mm256_mullo_epi32(u[12], cospi16);
    u[12] = _mm256_add_epi32(u[12], x);
    u[12] = _mm256_add_epi32(u[12], rnding);
    u[12] = _mm256_srai_epi32(u[12], bit);

    x = _mm256_mullo_epi32(u[13], cospi16);
    u[13] = _mm256_sub_epi32(y, x);
    u[13] = _mm256_add_epi32(u[13], rnding);
    u[13] = _mm256_srai_epi32(u[13], bit);

    x = _mm256_mullo_epi32(u[15], cospi16);
    y = _mm256_mullo_epi32(u[14], cospi16);
    u[14] = _mm256_mullo_epi32(u[14], cospim48);
    u[14] = _mm256_add_epi32(u[14], x);
    u[14] = _mm256_add_epi32(u[14], rnding);
    u[14] = _mm256_srai_epi32(u[14], bit);

    x = _mm256_mullo_epi32(u[15], cospim48);
    u[15] = _mm256_sub_epi32(y, x);
    u[15] = _mm256_add_epi32(u[15], rnding);
    u[15] = _mm256_srai_epi32(u[15], bit);

    // stage 7
    addsub_avx2(u[0], u[2], &u[0], &u[2], &clamp_lo, &clamp_hi);
    addsub_avx2(u[1], u[3], &u[1], &u[3], &clamp_lo, &clamp_hi);
    addsub_avx2(u[4], u[6], &u[4], &u[6], &clamp_lo, &clamp_hi);
    addsub_avx2(u[5], u[7], &u[5], &u[7], &clamp_lo, &clamp_hi);
    addsub_avx2(u[8], u[10], &u[8], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(u[9], u[11], &u[9], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(u[12], u[14], &u[12], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(u[13], u[15], &u[13], &u[15], &clamp_lo, &clamp_hi);

    // stage 8
    y = _mm256_mullo_epi32(u[2], cospi32);
    x = _mm256_mullo_epi32(u[3], cospi32);
    u[2] = _mm256_add_epi32(y, x);
    u[2] = _mm256_add_epi32(u[2], rnding);
    u[2] = _mm256_srai_epi32(u[2], bit);

    u[3] = _mm256_sub_epi32(y, x);
    u[3] = _mm256_add_epi32(u[3], rnding);
    u[3] = _mm256_srai_epi32(u[3], bit);
    y = _mm256_mullo_epi32(u[6], cospi32);
    x = _mm256_mullo_epi32(u[7], cospi32);
    u[6] = _mm256_add_epi32(y, x);
    u[6] = _mm256_add_epi32(u[6], rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    u[7] = _mm256_sub_epi32(y, x);
    u[7] = _mm256_add_epi32(u[7], rnding);
    u[7] = _mm256_srai_epi32(u[7], bit);

    y = _mm256_mullo_epi32(u[10], cospi32);
    x = _mm256_mullo_epi32(u[11], cospi32);
    u[10] = _mm256_add_epi32(y, x);
    u[10] = _mm256_add_epi32(u[10], rnding);
    u[10] = _mm256_srai_epi32(u[10], bit);

    u[11] = _mm256_sub_epi32(y, x);
    u[11] = _mm256_add_epi32(u[11], rnding);
    u[11] = _mm256_srai_epi32(u[11], bit);

    y = _mm256_mullo_epi32(u[14], cospi32);
    x = _mm256_mullo_epi32(u[15], cospi32);
    u[14] = _mm256_add_epi32(y, x);
    u[14] = _mm256_add_epi32(u[14], rnding);
    u[14] = _mm256_srai_epi32(u[14], bit);

    u[15] = _mm256_sub_epi32(y, x);
    u[15] = _mm256_add_epi32(u[15], rnding);
    u[15] = _mm256_srai_epi32(u[15], bit);

    // stage 9
    if (do_cols) {
      out[0] = u[0];
      out[1] = _mm256_sub_epi32(_mm256_setzero_si256(), u[8]);
      out[2] = u[12];
      out[3] = _mm256_sub_epi32(_mm256_setzero_si256(), u[4]);
      out[4] = u[6];
      out[5] = _mm256_sub_epi32(_mm256_setzero_si256(), u[14]);
      out[6] = u[10];
      out[7] = _mm256_sub_epi32(_mm256_setzero_si256(), u[2]);
      out[8] = u[3];
      out[9] = _mm256_sub_epi32(_mm256_setzero_si256(), u[11]);
      out[10] = u[15];
      out[11] = _mm256_sub_epi32(_mm256_setzero_si256(), u[7]);
      out[12] = u[5];
      out[13] = _mm256_sub_epi32(_mm256_setzero_si256(), u[13]);
      out[14] = u[9];
      out[15] = _mm256_sub_epi32(_mm256_setzero_si256(), u[1]);
    } else {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

      neg_shift_avx2(u[0], u[8], out + 0, out + 1, &clamp_lo_out, &clamp_hi_out,
                     out_shift);
      neg_shift_avx2(u[12], u[4], out + 2, out + 3, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[6], u[14], out + 4, out + 5, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[10], u[2], out + 6, out + 7, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[3], u[11], out + 8, out + 9, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[15], u[7], out + 10, out + 11, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[5], u[13], out + 12, out + 13, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(u[9], u[1], out + 14, out + 15, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
    }
  }
}
#endif  // CONFIG_ADST_TUNED && USE_TUNED_ADST16

#if CONFIG_INTER_DDT
static void iddt16_avx2(__m256i *in, __m256i *out, int bit, int do_cols, int bd,
                        int out_shift) {
  (void)bit;
#if CONFIG_FIX_INTER_DDT_PRECISION
  iadst_matrix_mult_avx2(in, out, INV_DDT_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 16);
#else
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         ddt16_kernel[INV_TXFM], 16, 16);
#endif  // CONFIG_FIX_INTER_DDT_PRECISION
}
#endif  // CONFIG_INTER_DDT
#if CONFIG_ADST_TUNED && USE_TUNED_ADST16
static void iadst16_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                         int bd, int out_shift) {
  (void)bit;
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         av2_adst_kernel16[INV_TXFM], tx_size_wide[TX_16X16],
                         tx_size_wide[TX_16X16]);
}
#else
static void iadst16_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                         int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospi34 = _mm256_set1_epi32(cospi[34]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospi42 = _mm256_set1_epi32(cospi[42]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospi50 = _mm256_set1_epi32(cospi[50]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi58 = _mm256_set1_epi32(cospi[58]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u[16], v[16], x, y;

  {
    // stage 0
    // stage 1
    // stage 2
    v[0] = _mm256_mullo_epi32(in[15], cospi2);
    x = _mm256_mullo_epi32(in[0], cospi62);
    v[0] = _mm256_add_epi32(v[0], x);
    v[0] = _mm256_add_epi32(v[0], rnding);
    v[0] = _mm256_srai_epi32(v[0], bit);

    v[1] = _mm256_mullo_epi32(in[15], cospi62);
    x = _mm256_mullo_epi32(in[0], cospi2);
    v[1] = _mm256_sub_epi32(v[1], x);
    v[1] = _mm256_add_epi32(v[1], rnding);
    v[1] = _mm256_srai_epi32(v[1], bit);

    v[2] = _mm256_mullo_epi32(in[13], cospi10);
    x = _mm256_mullo_epi32(in[2], cospi54);
    v[2] = _mm256_add_epi32(v[2], x);
    v[2] = _mm256_add_epi32(v[2], rnding);
    v[2] = _mm256_srai_epi32(v[2], bit);

    v[3] = _mm256_mullo_epi32(in[13], cospi54);
    x = _mm256_mullo_epi32(in[2], cospi10);
    v[3] = _mm256_sub_epi32(v[3], x);
    v[3] = _mm256_add_epi32(v[3], rnding);
    v[3] = _mm256_srai_epi32(v[3], bit);

    v[4] = _mm256_mullo_epi32(in[11], cospi18);
    x = _mm256_mullo_epi32(in[4], cospi46);
    v[4] = _mm256_add_epi32(v[4], x);
    v[4] = _mm256_add_epi32(v[4], rnding);
    v[4] = _mm256_srai_epi32(v[4], bit);

    v[5] = _mm256_mullo_epi32(in[11], cospi46);
    x = _mm256_mullo_epi32(in[4], cospi18);
    v[5] = _mm256_sub_epi32(v[5], x);
    v[5] = _mm256_add_epi32(v[5], rnding);
    v[5] = _mm256_srai_epi32(v[5], bit);

    v[6] = _mm256_mullo_epi32(in[9], cospi26);
    x = _mm256_mullo_epi32(in[6], cospi38);
    v[6] = _mm256_add_epi32(v[6], x);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    v[7] = _mm256_mullo_epi32(in[9], cospi38);
    x = _mm256_mullo_epi32(in[6], cospi26);
    v[7] = _mm256_sub_epi32(v[7], x);
    v[7] = _mm256_add_epi32(v[7], rnding);
    v[7] = _mm256_srai_epi32(v[7], bit);

    v[8] = _mm256_mullo_epi32(in[7], cospi34);
    x = _mm256_mullo_epi32(in[8], cospi30);
    v[8] = _mm256_add_epi32(v[8], x);
    v[8] = _mm256_add_epi32(v[8], rnding);
    v[8] = _mm256_srai_epi32(v[8], bit);

    v[9] = _mm256_mullo_epi32(in[7], cospi30);
    x = _mm256_mullo_epi32(in[8], cospi34);
    v[9] = _mm256_sub_epi32(v[9], x);
    v[9] = _mm256_add_epi32(v[9], rnding);
    v[9] = _mm256_srai_epi32(v[9], bit);

    v[10] = _mm256_mullo_epi32(in[5], cospi42);
    x = _mm256_mullo_epi32(in[10], cospi22);
    v[10] = _mm256_add_epi32(v[10], x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[11] = _mm256_mullo_epi32(in[5], cospi22);
    x = _mm256_mullo_epi32(in[10], cospi42);
    v[11] = _mm256_sub_epi32(v[11], x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = _mm256_mullo_epi32(in[3], cospi50);
    x = _mm256_mullo_epi32(in[12], cospi14);
    v[12] = _mm256_add_epi32(v[12], x);
    v[12] = _mm256_add_epi32(v[12], rnding);
    v[12] = _mm256_srai_epi32(v[12], bit);

    v[13] = _mm256_mullo_epi32(in[3], cospi14);
    x = _mm256_mullo_epi32(in[12], cospi50);
    v[13] = _mm256_sub_epi32(v[13], x);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    v[14] = _mm256_mullo_epi32(in[1], cospi58);
    x = _mm256_mullo_epi32(in[14], cospi6);
    v[14] = _mm256_add_epi32(v[14], x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_mullo_epi32(in[1], cospi6);
    x = _mm256_mullo_epi32(in[14], cospi58);
    v[15] = _mm256_sub_epi32(v[15], x);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 3
    addsub_avx2(v[0], v[8], &u[0], &u[8], &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[9], &u[1], &u[9], &clamp_lo, &clamp_hi);
    addsub_avx2(v[2], v[10], &u[2], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(v[3], v[11], &u[3], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(v[4], v[12], &u[4], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(v[5], v[13], &u[5], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(v[6], v[14], &u[6], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(v[7], v[15], &u[7], &u[15], &clamp_lo, &clamp_hi);

    // stage 4
    v[0] = u[0];
    v[1] = u[1];
    v[2] = u[2];
    v[3] = u[3];
    v[4] = u[4];
    v[5] = u[5];
    v[6] = u[6];
    v[7] = u[7];

    v[8] = _mm256_mullo_epi32(u[8], cospi8);
    x = _mm256_mullo_epi32(u[9], cospi56);
    v[8] = _mm256_add_epi32(v[8], x);
    v[8] = _mm256_add_epi32(v[8], rnding);
    v[8] = _mm256_srai_epi32(v[8], bit);

    v[9] = _mm256_mullo_epi32(u[8], cospi56);
    x = _mm256_mullo_epi32(u[9], cospi8);
    v[9] = _mm256_sub_epi32(v[9], x);
    v[9] = _mm256_add_epi32(v[9], rnding);
    v[9] = _mm256_srai_epi32(v[9], bit);

    v[10] = _mm256_mullo_epi32(u[10], cospi40);
    x = _mm256_mullo_epi32(u[11], cospi24);
    v[10] = _mm256_add_epi32(v[10], x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[11] = _mm256_mullo_epi32(u[10], cospi24);
    x = _mm256_mullo_epi32(u[11], cospi40);
    v[11] = _mm256_sub_epi32(v[11], x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = _mm256_mullo_epi32(u[12], cospim56);
    x = _mm256_mullo_epi32(u[13], cospi8);
    v[12] = _mm256_add_epi32(v[12], x);
    v[12] = _mm256_add_epi32(v[12], rnding);
    v[12] = _mm256_srai_epi32(v[12], bit);

    v[13] = _mm256_mullo_epi32(u[12], cospi8);
    x = _mm256_mullo_epi32(u[13], cospim56);
    v[13] = _mm256_sub_epi32(v[13], x);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    v[14] = _mm256_mullo_epi32(u[14], cospim24);
    x = _mm256_mullo_epi32(u[15], cospi40);
    v[14] = _mm256_add_epi32(v[14], x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_mullo_epi32(u[14], cospi40);
    x = _mm256_mullo_epi32(u[15], cospim24);
    v[15] = _mm256_sub_epi32(v[15], x);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 5
    addsub_avx2(v[0], v[4], &u[0], &u[4], &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[5], &u[1], &u[5], &clamp_lo, &clamp_hi);
    addsub_avx2(v[2], v[6], &u[2], &u[6], &clamp_lo, &clamp_hi);
    addsub_avx2(v[3], v[7], &u[3], &u[7], &clamp_lo, &clamp_hi);
    addsub_avx2(v[8], v[12], &u[8], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(v[9], v[13], &u[9], &u[13], &clamp_lo, &clamp_hi);
    addsub_avx2(v[10], v[14], &u[10], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(v[11], v[15], &u[11], &u[15], &clamp_lo, &clamp_hi);

    // stage 6
    v[0] = u[0];
    v[1] = u[1];
    v[2] = u[2];
    v[3] = u[3];

    v[4] = _mm256_mullo_epi32(u[4], cospi16);
    x = _mm256_mullo_epi32(u[5], cospi48);
    v[4] = _mm256_add_epi32(v[4], x);
    v[4] = _mm256_add_epi32(v[4], rnding);
    v[4] = _mm256_srai_epi32(v[4], bit);

    v[5] = _mm256_mullo_epi32(u[4], cospi48);
    x = _mm256_mullo_epi32(u[5], cospi16);
    v[5] = _mm256_sub_epi32(v[5], x);
    v[5] = _mm256_add_epi32(v[5], rnding);
    v[5] = _mm256_srai_epi32(v[5], bit);

    v[6] = _mm256_mullo_epi32(u[6], cospim48);
    x = _mm256_mullo_epi32(u[7], cospi16);
    v[6] = _mm256_add_epi32(v[6], x);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    v[7] = _mm256_mullo_epi32(u[6], cospi16);
    x = _mm256_mullo_epi32(u[7], cospim48);
    v[7] = _mm256_sub_epi32(v[7], x);
    v[7] = _mm256_add_epi32(v[7], rnding);
    v[7] = _mm256_srai_epi32(v[7], bit);

    v[8] = u[8];
    v[9] = u[9];
    v[10] = u[10];
    v[11] = u[11];

    v[12] = _mm256_mullo_epi32(u[12], cospi16);
    x = _mm256_mullo_epi32(u[13], cospi48);
    v[12] = _mm256_add_epi32(v[12], x);
    v[12] = _mm256_add_epi32(v[12], rnding);
    v[12] = _mm256_srai_epi32(v[12], bit);

    v[13] = _mm256_mullo_epi32(u[12], cospi48);
    x = _mm256_mullo_epi32(u[13], cospi16);
    v[13] = _mm256_sub_epi32(v[13], x);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    v[14] = _mm256_mullo_epi32(u[14], cospim48);
    x = _mm256_mullo_epi32(u[15], cospi16);
    v[14] = _mm256_add_epi32(v[14], x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_mullo_epi32(u[14], cospi16);
    x = _mm256_mullo_epi32(u[15], cospim48);
    v[15] = _mm256_sub_epi32(v[15], x);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 7
    addsub_avx2(v[0], v[2], &u[0], &u[2], &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[3], &u[1], &u[3], &clamp_lo, &clamp_hi);
    addsub_avx2(v[4], v[6], &u[4], &u[6], &clamp_lo, &clamp_hi);
    addsub_avx2(v[5], v[7], &u[5], &u[7], &clamp_lo, &clamp_hi);
    addsub_avx2(v[8], v[10], &u[8], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(v[9], v[11], &u[9], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(v[12], v[14], &u[12], &u[14], &clamp_lo, &clamp_hi);
    addsub_avx2(v[13], v[15], &u[13], &u[15], &clamp_lo, &clamp_hi);

    // stage 8
    v[0] = u[0];
    v[1] = u[1];

    y = _mm256_mullo_epi32(u[2], cospi32);
    x = _mm256_mullo_epi32(u[3], cospi32);
    v[2] = _mm256_add_epi32(y, x);
    v[2] = _mm256_add_epi32(v[2], rnding);
    v[2] = _mm256_srai_epi32(v[2], bit);

    v[3] = _mm256_sub_epi32(y, x);
    v[3] = _mm256_add_epi32(v[3], rnding);
    v[3] = _mm256_srai_epi32(v[3], bit);

    v[4] = u[4];
    v[5] = u[5];

    y = _mm256_mullo_epi32(u[6], cospi32);
    x = _mm256_mullo_epi32(u[7], cospi32);
    v[6] = _mm256_add_epi32(y, x);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    v[7] = _mm256_sub_epi32(y, x);
    v[7] = _mm256_add_epi32(v[7], rnding);
    v[7] = _mm256_srai_epi32(v[7], bit);

    v[8] = u[8];
    v[9] = u[9];

    y = _mm256_mullo_epi32(u[10], cospi32);
    x = _mm256_mullo_epi32(u[11], cospi32);
    v[10] = _mm256_add_epi32(y, x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[11] = _mm256_sub_epi32(y, x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = u[12];
    v[13] = u[13];

    y = _mm256_mullo_epi32(u[14], cospi32);
    x = _mm256_mullo_epi32(u[15], cospi32);
    v[14] = _mm256_add_epi32(y, x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_sub_epi32(y, x);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 9
    if (do_cols) {
      out[0] = v[0];
      out[1] = _mm256_sub_epi32(_mm256_setzero_si256(), v[8]);
      out[2] = v[12];
      out[3] = _mm256_sub_epi32(_mm256_setzero_si256(), v[4]);
      out[4] = v[6];
      out[5] = _mm256_sub_epi32(_mm256_setzero_si256(), v[14]);
      out[6] = v[10];
      out[7] = _mm256_sub_epi32(_mm256_setzero_si256(), v[2]);
      out[8] = v[3];
      out[9] = _mm256_sub_epi32(_mm256_setzero_si256(), v[11]);
      out[10] = v[15];
      out[11] = _mm256_sub_epi32(_mm256_setzero_si256(), v[7]);
      out[12] = v[5];
      out[13] = _mm256_sub_epi32(_mm256_setzero_si256(), v[13]);
      out[14] = v[9];
      out[15] = _mm256_sub_epi32(_mm256_setzero_si256(), v[1]);
    } else {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

      neg_shift_avx2(v[0], v[8], out + 0, out + 1, &clamp_lo_out, &clamp_hi_out,
                     out_shift);
      neg_shift_avx2(v[12], v[4], out + 2, out + 3, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[6], v[14], out + 4, out + 5, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[10], v[2], out + 6, out + 7, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[3], v[11], out + 8, out + 9, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[15], v[7], out + 10, out + 11, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[5], v[13], out + 12, out + 13, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
      neg_shift_avx2(v[9], v[1], out + 14, out + 15, &clamp_lo_out,
                     &clamp_hi_out, out_shift);
    }
  }
}
#endif  // CONFIG_ADST_TUNED && USE_TUNED_ADST16

static void idct8x8_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i x;

  // stage 0
  // stage 1
  // stage 2
  // stage 3
  x = _mm256_mullo_epi32(in[0], cospi32);
  x = _mm256_add_epi32(x, rnding);
  x = _mm256_srai_epi32(x, bit);

  // stage 4
  // stage 5
  if (!do_cols) {
    const int log_range_out = AOMMAX(16, bd + 6);
    __m256i offset = _mm256_set1_epi32((1 << out_shift) >> 1);
    clamp_lo = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    clamp_hi = _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
    x = _mm256_add_epi32(x, offset);
    x = _mm256_sra_epi32(x, _mm_cvtsi32_si128(out_shift));
  }
  x = _mm256_max_epi32(x, clamp_lo);
  x = _mm256_min_epi32(x, clamp_hi);
  out[0] = x;
  out[1] = x;
  out[2] = x;
  out[3] = x;
  out[4] = x;
  out[5] = x;
  out[6] = x;
  out[7] = x;
}
static void idct8x8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                         int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i v0, v1, v2, v3, v4, v5, v6, v7;
  __m256i x, y;

  // stage 0
  // stage 1
  // stage 2
  u0 = in[0];
  u1 = in[4];
  u2 = in[2];
  u3 = in[6];

  x = _mm256_mullo_epi32(in[1], cospi56);
  y = _mm256_mullo_epi32(in[7], cospim8);
  u4 = _mm256_add_epi32(x, y);
  u4 = _mm256_add_epi32(u4, rnding);
  u4 = _mm256_srai_epi32(u4, bit);

  x = _mm256_mullo_epi32(in[1], cospi8);
  y = _mm256_mullo_epi32(in[7], cospi56);
  u7 = _mm256_add_epi32(x, y);
  u7 = _mm256_add_epi32(u7, rnding);
  u7 = _mm256_srai_epi32(u7, bit);

  x = _mm256_mullo_epi32(in[5], cospi24);
  y = _mm256_mullo_epi32(in[3], cospim40);
  u5 = _mm256_add_epi32(x, y);
  u5 = _mm256_add_epi32(u5, rnding);
  u5 = _mm256_srai_epi32(u5, bit);

  x = _mm256_mullo_epi32(in[5], cospi40);
  y = _mm256_mullo_epi32(in[3], cospi24);
  u6 = _mm256_add_epi32(x, y);
  u6 = _mm256_add_epi32(u6, rnding);
  u6 = _mm256_srai_epi32(u6, bit);

  // stage 3
  x = _mm256_mullo_epi32(u0, cospi32);
  y = _mm256_mullo_epi32(u1, cospi32);
  v0 = _mm256_add_epi32(x, y);
  v0 = _mm256_add_epi32(v0, rnding);
  v0 = _mm256_srai_epi32(v0, bit);

  v1 = _mm256_sub_epi32(x, y);
  v1 = _mm256_add_epi32(v1, rnding);
  v1 = _mm256_srai_epi32(v1, bit);

  x = _mm256_mullo_epi32(u2, cospi48);
  y = _mm256_mullo_epi32(u3, cospim16);
  v2 = _mm256_add_epi32(x, y);
  v2 = _mm256_add_epi32(v2, rnding);
  v2 = _mm256_srai_epi32(v2, bit);

  x = _mm256_mullo_epi32(u2, cospi16);
  y = _mm256_mullo_epi32(u3, cospi48);
  v3 = _mm256_add_epi32(x, y);
  v3 = _mm256_add_epi32(v3, rnding);
  v3 = _mm256_srai_epi32(v3, bit);

  addsub_avx2(u4, u5, &v4, &v5, &clamp_lo, &clamp_hi);
  addsub_avx2(u7, u6, &v7, &v6, &clamp_lo, &clamp_hi);

  // stage 4
  addsub_avx2(v0, v3, &u0, &u3, &clamp_lo, &clamp_hi);
  addsub_avx2(v1, v2, &u1, &u2, &clamp_lo, &clamp_hi);
  u4 = v4;
  u7 = v7;

  x = _mm256_mullo_epi32(v5, cospi32);
  y = _mm256_mullo_epi32(v6, cospi32);
  u6 = _mm256_add_epi32(y, x);
  u6 = _mm256_add_epi32(u6, rnding);
  u6 = _mm256_srai_epi32(u6, bit);

  u5 = _mm256_sub_epi32(y, x);
  u5 = _mm256_add_epi32(u5, rnding);
  u5 = _mm256_srai_epi32(u5, bit);

  addsub_avx2(u0, u7, out + 0, out + 7, &clamp_lo, &clamp_hi);
  addsub_avx2(u1, u6, out + 1, out + 6, &clamp_lo, &clamp_hi);
  addsub_avx2(u2, u5, out + 2, out + 5, &clamp_lo, &clamp_hi);
  addsub_avx2(u3, u4, out + 3, out + 4, &clamp_lo, &clamp_hi);
  // stage 5
  if (!do_cols) {
    const int log_range_out = AOMMAX(16, bd + 6);
    const __m256i clamp_lo_out = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    const __m256i clamp_hi_out =
        _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

    round_shift_4x4_avx2(out, out_shift);
    round_shift_4x4_avx2(out + 4, out_shift);
    highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 8);
  }
}

#if CONFIG_ADST_TUNED || CONFIG_INTER_DDT
void iadst_matrix_mult_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                            int bd, int out_shift, const int32_t *kernel,
                            int kernel_size, int num_cols) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i x[16];

  for (int i = 0; i < kernel_size; ++i) {
    int row_idx = i * kernel_size;
    __m256i sum = zero;
    __m256i t;
    for (int j = 0; j < num_cols; ++j) {
      const __m256i coef = _mm256_set1_epi32(kernel[row_idx + j]);
      t = _mm256_mullo_epi32(in[j], coef);
      sum = _mm256_add_epi32(sum, t);
    }
    sum = _mm256_add_epi32(sum, rnding);
    sum = _mm256_srai_epi32(sum, bit);
    sum = _mm256_max_epi32(sum, clamp_lo);
    x[i] = _mm256_min_epi32(sum, clamp_hi);
  }

  if (!do_cols) {
    log_range = AOMMAX(16, bd + 6);
    clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
    clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
    if (out_shift != 0) {
      __m256i offset = _mm256_set1_epi32((1 << out_shift) >> 1);
      for (int i = 0; i < kernel_size; ++i) {
        x[i] = _mm256_add_epi32(x[i], offset);
        x[i] = _mm256_sra_epi32(x[i], _mm_cvtsi32_si128(out_shift));
      }
    }
  }

  for (int i = 0; i < kernel_size; ++i) {
    x[i] = _mm256_max_epi32(x[i], clamp_lo);
    out[i] = _mm256_min_epi32(x[i], clamp_hi);
  }
}
#endif  // CONFIG_ADST_TUNED || CONFIG_INTER_DDT

#if CONFIG_INTER_DDT
static void iddt8x8_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  (void)bit;
#if CONFIG_FIX_INTER_DDT_PRECISION
  iadst_matrix_mult_avx2(in, out, INV_DDT_BIT, do_cols, bd, out_shift,
                         ddt8_kernel[INV_TXFM], 8, 1);
#else
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         ddt8_kernel[INV_TXFM], 8, 1);
#endif  // CONFIG_FIX_INTER_DDT_PRECISION
}
#endif  // CONFIG_INTER_DDT

#if CONFIG_ADST_TUNED && USE_TUNED_ADST8
static void iadst8x8_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                               int bd, int out_shift) {
  (void)bit;
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         av2_adst_kernel8[INV_TXFM], tx_size_wide[TX_8X8], 1);
}
#else
static void iadst8x8_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                               int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const __m256i kZero = _mm256_setzero_si256();
  __m256i u[8], x;

  // stage 0
  // stage 1
  // stage 2

  x = _mm256_mullo_epi32(in[0], cospi60);
  u[0] = _mm256_add_epi32(x, rnding);
  u[0] = _mm256_srai_epi32(u[0], bit);

  x = _mm256_mullo_epi32(in[0], cospi4);
  u[1] = _mm256_sub_epi32(kZero, x);
  u[1] = _mm256_add_epi32(u[1], rnding);
  u[1] = _mm256_srai_epi32(u[1], bit);

  // stage 3
  // stage 4
  __m256i temp1, temp2;
  temp1 = _mm256_mullo_epi32(u[0], cospi16);
  x = _mm256_mullo_epi32(u[1], cospi48);
  temp1 = _mm256_add_epi32(temp1, x);
  temp1 = _mm256_add_epi32(temp1, rnding);
  temp1 = _mm256_srai_epi32(temp1, bit);
  u[4] = temp1;

  temp2 = _mm256_mullo_epi32(u[0], cospi48);
  x = _mm256_mullo_epi32(u[1], cospi16);
  u[5] = _mm256_sub_epi32(temp2, x);
  u[5] = _mm256_add_epi32(u[5], rnding);
  u[5] = _mm256_srai_epi32(u[5], bit);

  // stage 5
  // stage 6
  temp1 = _mm256_mullo_epi32(u[0], cospi32);
  x = _mm256_mullo_epi32(u[1], cospi32);
  u[2] = _mm256_add_epi32(temp1, x);
  u[2] = _mm256_add_epi32(u[2], rnding);
  u[2] = _mm256_srai_epi32(u[2], bit);

  u[3] = _mm256_sub_epi32(temp1, x);
  u[3] = _mm256_add_epi32(u[3], rnding);
  u[3] = _mm256_srai_epi32(u[3], bit);

  temp1 = _mm256_mullo_epi32(u[4], cospi32);
  x = _mm256_mullo_epi32(u[5], cospi32);
  u[6] = _mm256_add_epi32(temp1, x);
  u[6] = _mm256_add_epi32(u[6], rnding);
  u[6] = _mm256_srai_epi32(u[6], bit);

  u[7] = _mm256_sub_epi32(temp1, x);
  u[7] = _mm256_add_epi32(u[7], rnding);
  u[7] = _mm256_srai_epi32(u[7], bit);

  // stage 7
  if (do_cols) {
    out[0] = u[0];
    out[1] = _mm256_sub_epi32(kZero, u[4]);
    out[2] = u[6];
    out[3] = _mm256_sub_epi32(kZero, u[2]);
    out[4] = u[3];
    out[5] = _mm256_sub_epi32(kZero, u[7]);
    out[6] = u[5];
    out[7] = _mm256_sub_epi32(kZero, u[1]);
  } else {
    const int log_range_out = AOMMAX(16, bd + 6);
    const __m256i clamp_lo_out = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    const __m256i clamp_hi_out =
        _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

    neg_shift_avx2(u[0], u[4], out + 0, out + 1, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[6], u[2], out + 2, out + 3, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[3], u[7], out + 4, out + 5, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[5], u[1], out + 6, out + 7, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
  }
}
#endif  // CONFIG_ADST_TUNED && USE_TUNED_ADST8

#if CONFIG_INTER_DDT
static void iddt8x8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                         int bd, int out_shift) {
  (void)bit;
#if CONFIG_FIX_INTER_DDT_PRECISION
  iadst_matrix_mult_avx2(in, out, INV_DDT_BIT, do_cols, bd, out_shift,
                         ddt8_kernel[INV_TXFM], 8, 8);
#else
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         ddt8_kernel[INV_TXFM], 8, 8);
#endif  // CONFIG_FIX_INTER_DDT_PRECISION
}
#endif  // CONFIG_INTER_DDT

#if CONFIG_ADST_TUNED && USE_TUNED_ADST8
static void iadst8x8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                          int bd, int out_shift) {
  (void)bit;
  iadst_matrix_mult_avx2(in, out, INV_ADST_BIT, do_cols, bd, out_shift,
                         av2_adst_kernel8[INV_TXFM], tx_size_wide[TX_8X8],
                         tx_size_wide[TX_8X8]);
}
#else
static void iadst8x8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                          int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const __m256i kZero = _mm256_setzero_si256();
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);
  __m256i u[8], v[8], x;

  // stage 0
  // stage 1
  // stage 2

  u[0] = _mm256_mullo_epi32(in[7], cospi4);
  x = _mm256_mullo_epi32(in[0], cospi60);
  u[0] = _mm256_add_epi32(u[0], x);
  u[0] = _mm256_add_epi32(u[0], rnding);
  u[0] = _mm256_srai_epi32(u[0], bit);

  u[1] = _mm256_mullo_epi32(in[7], cospi60);
  x = _mm256_mullo_epi32(in[0], cospi4);
  u[1] = _mm256_sub_epi32(u[1], x);
  u[1] = _mm256_add_epi32(u[1], rnding);
  u[1] = _mm256_srai_epi32(u[1], bit);

  u[2] = _mm256_mullo_epi32(in[5], cospi20);
  x = _mm256_mullo_epi32(in[2], cospi44);
  u[2] = _mm256_add_epi32(u[2], x);
  u[2] = _mm256_add_epi32(u[2], rnding);
  u[2] = _mm256_srai_epi32(u[2], bit);

  u[3] = _mm256_mullo_epi32(in[5], cospi44);
  x = _mm256_mullo_epi32(in[2], cospi20);
  u[3] = _mm256_sub_epi32(u[3], x);
  u[3] = _mm256_add_epi32(u[3], rnding);
  u[3] = _mm256_srai_epi32(u[3], bit);

  u[4] = _mm256_mullo_epi32(in[3], cospi36);
  x = _mm256_mullo_epi32(in[4], cospi28);
  u[4] = _mm256_add_epi32(u[4], x);
  u[4] = _mm256_add_epi32(u[4], rnding);
  u[4] = _mm256_srai_epi32(u[4], bit);

  u[5] = _mm256_mullo_epi32(in[3], cospi28);
  x = _mm256_mullo_epi32(in[4], cospi36);
  u[5] = _mm256_sub_epi32(u[5], x);
  u[5] = _mm256_add_epi32(u[5], rnding);
  u[5] = _mm256_srai_epi32(u[5], bit);

  u[6] = _mm256_mullo_epi32(in[1], cospi52);
  x = _mm256_mullo_epi32(in[6], cospi12);
  u[6] = _mm256_add_epi32(u[6], x);
  u[6] = _mm256_add_epi32(u[6], rnding);
  u[6] = _mm256_srai_epi32(u[6], bit);

  u[7] = _mm256_mullo_epi32(in[1], cospi12);
  x = _mm256_mullo_epi32(in[6], cospi52);
  u[7] = _mm256_sub_epi32(u[7], x);
  u[7] = _mm256_add_epi32(u[7], rnding);
  u[7] = _mm256_srai_epi32(u[7], bit);

  // stage 3
  addsub_avx2(u[0], u[4], &v[0], &v[4], &clamp_lo, &clamp_hi);
  addsub_avx2(u[1], u[5], &v[1], &v[5], &clamp_lo, &clamp_hi);
  addsub_avx2(u[2], u[6], &v[2], &v[6], &clamp_lo, &clamp_hi);
  addsub_avx2(u[3], u[7], &v[3], &v[7], &clamp_lo, &clamp_hi);

  // stage 4
  u[0] = v[0];
  u[1] = v[1];
  u[2] = v[2];
  u[3] = v[3];

  u[4] = _mm256_mullo_epi32(v[4], cospi16);
  x = _mm256_mullo_epi32(v[5], cospi48);
  u[4] = _mm256_add_epi32(u[4], x);
  u[4] = _mm256_add_epi32(u[4], rnding);
  u[4] = _mm256_srai_epi32(u[4], bit);

  u[5] = _mm256_mullo_epi32(v[4], cospi48);
  x = _mm256_mullo_epi32(v[5], cospi16);
  u[5] = _mm256_sub_epi32(u[5], x);
  u[5] = _mm256_add_epi32(u[5], rnding);
  u[5] = _mm256_srai_epi32(u[5], bit);

  u[6] = _mm256_mullo_epi32(v[6], cospim48);
  x = _mm256_mullo_epi32(v[7], cospi16);
  u[6] = _mm256_add_epi32(u[6], x);
  u[6] = _mm256_add_epi32(u[6], rnding);
  u[6] = _mm256_srai_epi32(u[6], bit);

  u[7] = _mm256_mullo_epi32(v[6], cospi16);
  x = _mm256_mullo_epi32(v[7], cospim48);
  u[7] = _mm256_sub_epi32(u[7], x);
  u[7] = _mm256_add_epi32(u[7], rnding);
  u[7] = _mm256_srai_epi32(u[7], bit);

  // stage 5
  addsub_avx2(u[0], u[2], &v[0], &v[2], &clamp_lo, &clamp_hi);
  addsub_avx2(u[1], u[3], &v[1], &v[3], &clamp_lo, &clamp_hi);
  addsub_avx2(u[4], u[6], &v[4], &v[6], &clamp_lo, &clamp_hi);
  addsub_avx2(u[5], u[7], &v[5], &v[7], &clamp_lo, &clamp_hi);

  // stage 6
  u[0] = v[0];
  u[1] = v[1];
  u[4] = v[4];
  u[5] = v[5];

  v[0] = _mm256_mullo_epi32(v[2], cospi32);
  x = _mm256_mullo_epi32(v[3], cospi32);
  u[2] = _mm256_add_epi32(v[0], x);
  u[2] = _mm256_add_epi32(u[2], rnding);
  u[2] = _mm256_srai_epi32(u[2], bit);

  u[3] = _mm256_sub_epi32(v[0], x);
  u[3] = _mm256_add_epi32(u[3], rnding);
  u[3] = _mm256_srai_epi32(u[3], bit);

  v[0] = _mm256_mullo_epi32(v[6], cospi32);
  x = _mm256_mullo_epi32(v[7], cospi32);
  u[6] = _mm256_add_epi32(v[0], x);
  u[6] = _mm256_add_epi32(u[6], rnding);
  u[6] = _mm256_srai_epi32(u[6], bit);

  u[7] = _mm256_sub_epi32(v[0], x);
  u[7] = _mm256_add_epi32(u[7], rnding);
  u[7] = _mm256_srai_epi32(u[7], bit);

  // stage 7
  if (do_cols) {
    out[0] = u[0];
    out[1] = _mm256_sub_epi32(kZero, u[4]);
    out[2] = u[6];
    out[3] = _mm256_sub_epi32(kZero, u[2]);
    out[4] = u[3];
    out[5] = _mm256_sub_epi32(kZero, u[7]);
    out[6] = u[5];
    out[7] = _mm256_sub_epi32(kZero, u[1]);
  } else {
    const int log_range_out = AOMMAX(16, bd + 6);
    const __m256i clamp_lo_out = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    const __m256i clamp_hi_out =
        _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

    neg_shift_avx2(u[0], u[4], out + 0, out + 1, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[6], u[2], out + 2, out + 3, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[3], u[7], out + 4, out + 5, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
    neg_shift_avx2(u[5], u[1], out + 6, out + 7, &clamp_lo_out, &clamp_hi_out,
                   out_shift);
  }
}
#endif  // CONFIG_ADST_TUNED && USE_TUNED_ADST8

static INLINE void idct64_stage8_avx2(
    __m256i *u, const __m256i *cospim32, const __m256i *cospi32,
    const __m256i *cospim16, const __m256i *cospi48, const __m256i *cospi16,
    const __m256i *cospim48, const __m256i *clamp_lo, const __m256i *clamp_hi,
    const __m256i *rnding, int bit) {
  int i;
  __m256i temp1, temp2, temp3, temp4;
  temp1 = half_btf_avx2(cospim32, &u[10], cospi32, &u[13], rnding, bit);
  u[13] = half_btf_avx2(cospi32, &u[10], cospi32, &u[13], rnding, bit);
  u[10] = temp1;
  temp2 = half_btf_avx2(cospim32, &u[11], cospi32, &u[12], rnding, bit);
  u[12] = half_btf_avx2(cospi32, &u[11], cospi32, &u[12], rnding, bit);
  u[11] = temp2;

  for (i = 16; i < 20; ++i) {
    addsub_avx2(u[i], u[i ^ 7], &u[i], &u[i ^ 7], clamp_lo, clamp_hi);
    addsub_avx2(u[i ^ 15], u[i ^ 8], &u[i ^ 15], &u[i ^ 8], clamp_lo, clamp_hi);
  }

  temp1 = half_btf_avx2(cospim16, &u[36], cospi48, &u[59], rnding, bit);
  temp2 = half_btf_avx2(cospim16, &u[37], cospi48, &u[58], rnding, bit);
  temp3 = half_btf_avx2(cospim16, &u[38], cospi48, &u[57], rnding, bit);
  temp4 = half_btf_avx2(cospim16, &u[39], cospi48, &u[56], rnding, bit);
  u[56] = half_btf_avx2(cospi48, &u[39], cospi16, &u[56], rnding, bit);
  u[57] = half_btf_avx2(cospi48, &u[38], cospi16, &u[57], rnding, bit);
  u[58] = half_btf_avx2(cospi48, &u[37], cospi16, &u[58], rnding, bit);
  u[59] = half_btf_avx2(cospi48, &u[36], cospi16, &u[59], rnding, bit);
  u[36] = temp1;
  u[37] = temp2;
  u[38] = temp3;
  u[39] = temp4;

  temp1 = half_btf_avx2(cospim48, &u[40], cospim16, &u[55], rnding, bit);
  temp2 = half_btf_avx2(cospim48, &u[41], cospim16, &u[54], rnding, bit);
  temp3 = half_btf_avx2(cospim48, &u[42], cospim16, &u[53], rnding, bit);
  temp4 = half_btf_avx2(cospim48, &u[43], cospim16, &u[52], rnding, bit);
  u[52] = half_btf_avx2(cospim16, &u[43], cospi48, &u[52], rnding, bit);
  u[53] = half_btf_avx2(cospim16, &u[42], cospi48, &u[53], rnding, bit);
  u[54] = half_btf_avx2(cospim16, &u[41], cospi48, &u[54], rnding, bit);
  u[55] = half_btf_avx2(cospim16, &u[40], cospi48, &u[55], rnding, bit);
  u[40] = temp1;
  u[41] = temp2;
  u[42] = temp3;
  u[43] = temp4;
}

static INLINE void idct64_stage9_avx2(__m256i *u, const __m256i *cospim32,
                                      const __m256i *cospi32,
                                      const __m256i *clamp_lo,
                                      const __m256i *clamp_hi,
                                      const __m256i *rnding, int bit) {
  int i;
  __m256i temp1, temp2, temp3, temp4;
  for (i = 0; i < 8; ++i) {
    addsub_avx2(u[i], u[15 - i], &u[i], &u[15 - i], clamp_lo, clamp_hi);
  }

  temp1 = half_btf_avx2(cospim32, &u[20], cospi32, &u[27], rnding, bit);
  temp2 = half_btf_avx2(cospim32, &u[21], cospi32, &u[26], rnding, bit);
  temp3 = half_btf_avx2(cospim32, &u[22], cospi32, &u[25], rnding, bit);
  temp4 = half_btf_avx2(cospim32, &u[23], cospi32, &u[24], rnding, bit);
  u[24] = half_btf_avx2(cospi32, &u[23], cospi32, &u[24], rnding, bit);
  u[25] = half_btf_avx2(cospi32, &u[22], cospi32, &u[25], rnding, bit);
  u[26] = half_btf_avx2(cospi32, &u[21], cospi32, &u[26], rnding, bit);
  u[27] = half_btf_avx2(cospi32, &u[20], cospi32, &u[27], rnding, bit);
  u[20] = temp1;
  u[21] = temp2;
  u[22] = temp3;
  u[23] = temp4;
  for (i = 32; i < 40; i++) {
    addsub_avx2(u[i], u[i ^ 15], &u[i], &u[i ^ 15], clamp_lo, clamp_hi);
  }

  for (i = 48; i < 56; i++) {
    addsub_avx2(u[i ^ 15], u[i], &u[i ^ 15], &u[i], clamp_lo, clamp_hi);
  }
}

static INLINE void idct64_stage10_avx2(__m256i *u, const __m256i *cospim32,
                                       const __m256i *cospi32,
                                       const __m256i *clamp_lo,
                                       const __m256i *clamp_hi,
                                       const __m256i *rnding, int bit) {
  __m256i temp1, temp2, temp3, temp4;
  for (int i = 0; i < 16; i++) {
    addsub_avx2(u[i], u[31 - i], &u[i], &u[31 - i], clamp_lo, clamp_hi);
  }

  temp1 = half_btf_avx2(cospim32, &u[40], cospi32, &u[55], rnding, bit);
  temp2 = half_btf_avx2(cospim32, &u[41], cospi32, &u[54], rnding, bit);
  temp3 = half_btf_avx2(cospim32, &u[42], cospi32, &u[53], rnding, bit);
  temp4 = half_btf_avx2(cospim32, &u[43], cospi32, &u[52], rnding, bit);
  u[52] = half_btf_avx2(cospi32, &u[43], cospi32, &u[52], rnding, bit);
  u[53] = half_btf_avx2(cospi32, &u[42], cospi32, &u[53], rnding, bit);
  u[54] = half_btf_avx2(cospi32, &u[41], cospi32, &u[54], rnding, bit);
  u[55] = half_btf_avx2(cospi32, &u[40], cospi32, &u[55], rnding, bit);
  u[40] = temp1;
  u[41] = temp2;
  u[42] = temp3;
  u[43] = temp4;

  temp1 = half_btf_avx2(cospim32, &u[44], cospi32, &u[51], rnding, bit);
  temp2 = half_btf_avx2(cospim32, &u[45], cospi32, &u[50], rnding, bit);
  temp3 = half_btf_avx2(cospim32, &u[46], cospi32, &u[49], rnding, bit);
  temp4 = half_btf_avx2(cospim32, &u[47], cospi32, &u[48], rnding, bit);
  u[48] = half_btf_avx2(cospi32, &u[47], cospi32, &u[48], rnding, bit);
  u[49] = half_btf_avx2(cospi32, &u[46], cospi32, &u[49], rnding, bit);
  u[50] = half_btf_avx2(cospi32, &u[45], cospi32, &u[50], rnding, bit);
  u[51] = half_btf_avx2(cospi32, &u[44], cospi32, &u[51], rnding, bit);
  u[44] = temp1;
  u[45] = temp2;
  u[46] = temp3;
  u[47] = temp4;
}

static INLINE void idct64_stage11_avx2(__m256i *u, __m256i *out, int do_cols,
                                       int bd, int out_shift,
                                       const __m256i *clamp_lo,
                                       const __m256i *clamp_hi) {
  for (int i = 0; i < 32; i++) {
    addsub_avx2(u[i], u[63 - i], &out[(i)], &out[(63 - i)], clamp_lo, clamp_hi);
  }

  if (!do_cols) {
    const int log_range_out = AOMMAX(16, bd + 6);
    const __m256i clamp_lo_out = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
    const __m256i clamp_hi_out =
        _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

    round_shift_8x8_avx2(out, out_shift);
    round_shift_8x8_avx2(out + 16, out_shift);
    round_shift_8x8_avx2(out + 32, out_shift);
    round_shift_8x8_avx2(out + 48, out_shift);
    highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 64);
  }
}

static void idct64_low1_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);

  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);

  {
    __m256i x;

    // stage 1
    // stage 2
    // stage 3
    // stage 4
    // stage 5
    // stage 6
    x = half_btf_0_avx2(&cospi32, &in[0], &rnding, bit);

    // stage 8
    // stage 9
    // stage 10
    // stage 11
    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      clamp_lo = _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      clamp_hi = _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);
      if (out_shift != 0) {
        __m256i offset = _mm256_set1_epi32((1 << out_shift) >> 1);
        x = _mm256_add_epi32(x, offset);
        x = _mm256_sra_epi32(x, _mm_cvtsi32_si128(out_shift));
      }
    }
    x = _mm256_max_epi32(x, clamp_lo);
    x = _mm256_min_epi32(x, clamp_hi);
    out[0] = x;
    out[1] = x;
    out[2] = x;
    out[3] = x;
    out[4] = x;
    out[5] = x;
    out[6] = x;
    out[7] = x;
    out[8] = x;
    out[9] = x;
    out[10] = x;
    out[11] = x;
    out[12] = x;
    out[13] = x;
    out[14] = x;
    out[15] = x;
    out[16] = x;
    out[17] = x;
    out[18] = x;
    out[19] = x;
    out[20] = x;
    out[21] = x;
    out[22] = x;
    out[23] = x;
    out[24] = x;
    out[25] = x;
    out[26] = x;
    out[27] = x;
    out[28] = x;
    out[29] = x;
    out[30] = x;
    out[31] = x;
    out[32] = x;
    out[33] = x;
    out[34] = x;
    out[35] = x;
    out[36] = x;
    out[37] = x;
    out[38] = x;
    out[39] = x;
    out[40] = x;
    out[41] = x;
    out[42] = x;
    out[43] = x;
    out[44] = x;
    out[45] = x;
    out[46] = x;
    out[47] = x;
    out[48] = x;
    out[49] = x;
    out[50] = x;
    out[51] = x;
    out[52] = x;
    out[53] = x;
    out[54] = x;
    out[55] = x;
    out[56] = x;
    out[57] = x;
    out[58] = x;
    out[59] = x;
    out[60] = x;
    out[61] = x;
    out[62] = x;
    out[63] = x;
  }
}
static void idct64_low8_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                             int bd, int out_shift) {
  int i, j;
  const int32_t *cospi = cospi_arr(bit);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);

  const __m256i cospi1 = _mm256_set1_epi32(cospi[1]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi3 = _mm256_set1_epi32(cospi[3]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim12 = _mm256_set1_epi32(-cospi[12]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospim28 = _mm256_set1_epi32(-cospi[28]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospi63 = _mm256_set1_epi32(cospi[63]);
  const __m256i cospim57 = _mm256_set1_epi32(-cospi[57]);
  const __m256i cospi7 = _mm256_set1_epi32(cospi[7]);
  const __m256i cospi5 = _mm256_set1_epi32(cospi[5]);
  const __m256i cospi59 = _mm256_set1_epi32(cospi[59]);
  const __m256i cospim61 = _mm256_set1_epi32(-cospi[61]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);

  {
    __m256i u[64];

    // stage 1
    u[0] = in[0];
    u[8] = in[4];
    u[16] = in[2];
    u[24] = in[6];
    u[32] = in[1];
    u[40] = in[5];
    u[48] = in[3];
    u[56] = in[7];

    // stage 2
    u[63] = half_btf_0_avx2(&cospi1, &u[32], &rnding, bit);
    u[32] = half_btf_0_avx2(&cospi63, &u[32], &rnding, bit);
    u[39] = half_btf_0_avx2(&cospim57, &u[56], &rnding, bit);
    u[56] = half_btf_0_avx2(&cospi7, &u[56], &rnding, bit);
    u[55] = half_btf_0_avx2(&cospi5, &u[40], &rnding, bit);
    u[40] = half_btf_0_avx2(&cospi59, &u[40], &rnding, bit);
    u[47] = half_btf_0_avx2(&cospim61, &u[48], &rnding, bit);
    u[48] = half_btf_0_avx2(&cospi3, &u[48], &rnding, bit);

    // stage 3
    u[31] = half_btf_0_avx2(&cospi2, &u[16], &rnding, bit);
    u[16] = half_btf_0_avx2(&cospi62, &u[16], &rnding, bit);
    u[23] = half_btf_0_avx2(&cospim58, &u[24], &rnding, bit);
    u[24] = half_btf_0_avx2(&cospi6, &u[24], &rnding, bit);
    u[33] = u[32];
    u[38] = u[39];
    u[41] = u[40];
    u[46] = u[47];
    u[49] = u[48];
    u[54] = u[55];
    u[57] = u[56];
    u[62] = u[63];

    // stage 4
    __m256i temp1, temp2;
    u[15] = half_btf_0_avx2(&cospi4, &u[8], &rnding, bit);
    u[8] = half_btf_0_avx2(&cospi60, &u[8], &rnding, bit);
    u[17] = u[16];
    u[22] = u[23];
    u[25] = u[24];
    u[30] = u[31];

    temp1 = half_btf_avx2(&cospim4, &u[33], &cospi60, &u[62], &rnding, bit);
    u[62] = half_btf_avx2(&cospi60, &u[33], &cospi4, &u[62], &rnding, bit);
    u[33] = temp1;

    temp2 = half_btf_avx2(&cospim36, &u[38], &cospi28, &u[57], &rnding, bit);
    u[38] = half_btf_avx2(&cospim28, &u[38], &cospim36, &u[57], &rnding, bit);
    u[57] = temp2;

    temp1 = half_btf_avx2(&cospim20, &u[41], &cospi44, &u[54], &rnding, bit);
    u[54] = half_btf_avx2(&cospi44, &u[41], &cospi20, &u[54], &rnding, bit);
    u[41] = temp1;

    temp2 = half_btf_avx2(&cospim12, &u[46], &cospim52, &u[49], &rnding, bit);
    u[49] = half_btf_avx2(&cospim52, &u[46], &cospi12, &u[49], &rnding, bit);
    u[46] = temp2;

    // stage 5
    u[9] = u[8];
    u[14] = u[15];

    temp1 = half_btf_avx2(&cospim8, &u[17], &cospi56, &u[30], &rnding, bit);
    u[30] = half_btf_avx2(&cospi56, &u[17], &cospi8, &u[30], &rnding, bit);
    u[17] = temp1;

    temp2 = half_btf_avx2(&cospim24, &u[22], &cospim40, &u[25], &rnding, bit);
    u[25] = half_btf_avx2(&cospim40, &u[22], &cospi24, &u[25], &rnding, bit);
    u[22] = temp2;

    u[35] = u[32];
    u[34] = u[33];
    u[36] = u[39];
    u[37] = u[38];
    u[43] = u[40];
    u[42] = u[41];
    u[44] = u[47];
    u[45] = u[46];
    u[51] = u[48];
    u[50] = u[49];
    u[52] = u[55];
    u[53] = u[54];
    u[59] = u[56];
    u[58] = u[57];
    u[60] = u[63];
    u[61] = u[62];

    // stage 6
    temp1 = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    u[1] = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    u[0] = temp1;

    temp2 = half_btf_avx2(&cospim16, &u[9], &cospi48, &u[14], &rnding, bit);
    u[14] = half_btf_avx2(&cospi48, &u[9], &cospi16, &u[14], &rnding, bit);
    u[9] = temp2;
    u[19] = u[16];
    u[18] = u[17];
    u[20] = u[23];
    u[21] = u[22];
    u[27] = u[24];
    u[26] = u[25];
    u[28] = u[31];
    u[29] = u[30];

    temp1 = half_btf_avx2(&cospim8, &u[34], &cospi56, &u[61], &rnding, bit);
    u[61] = half_btf_avx2(&cospi56, &u[34], &cospi8, &u[61], &rnding, bit);
    u[34] = temp1;
    temp2 = half_btf_avx2(&cospim8, &u[35], &cospi56, &u[60], &rnding, bit);
    u[60] = half_btf_avx2(&cospi56, &u[35], &cospi8, &u[60], &rnding, bit);
    u[35] = temp2;
    temp1 = half_btf_avx2(&cospim56, &u[36], &cospim8, &u[59], &rnding, bit);
    u[59] = half_btf_avx2(&cospim8, &u[36], &cospi56, &u[59], &rnding, bit);
    u[36] = temp1;
    temp2 = half_btf_avx2(&cospim56, &u[37], &cospim8, &u[58], &rnding, bit);
    u[58] = half_btf_avx2(&cospim8, &u[37], &cospi56, &u[58], &rnding, bit);
    u[37] = temp2;
    temp1 = half_btf_avx2(&cospim40, &u[42], &cospi24, &u[53], &rnding, bit);
    u[53] = half_btf_avx2(&cospi24, &u[42], &cospi40, &u[53], &rnding, bit);
    u[42] = temp1;
    temp2 = half_btf_avx2(&cospim40, &u[43], &cospi24, &u[52], &rnding, bit);
    u[52] = half_btf_avx2(&cospi24, &u[43], &cospi40, &u[52], &rnding, bit);
    u[43] = temp2;
    temp1 = half_btf_avx2(&cospim24, &u[44], &cospim40, &u[51], &rnding, bit);
    u[51] = half_btf_avx2(&cospim40, &u[44], &cospi24, &u[51], &rnding, bit);
    u[44] = temp1;
    temp2 = half_btf_avx2(&cospim24, &u[45], &cospim40, &u[50], &rnding, bit);
    u[50] = half_btf_avx2(&cospim40, &u[45], &cospi24, &u[50], &rnding, bit);
    u[45] = temp2;

    // stage 7
    u[3] = u[0];
    u[2] = u[1];
    u[11] = u[8];
    u[10] = u[9];
    u[12] = u[15];
    u[13] = u[14];

    temp1 = half_btf_avx2(&cospim16, &u[18], &cospi48, &u[29], &rnding, bit);
    u[29] = half_btf_avx2(&cospi48, &u[18], &cospi16, &u[29], &rnding, bit);
    u[18] = temp1;
    temp2 = half_btf_avx2(&cospim16, &u[19], &cospi48, &u[28], &rnding, bit);
    u[28] = half_btf_avx2(&cospi48, &u[19], &cospi16, &u[28], &rnding, bit);
    u[19] = temp2;
    temp1 = half_btf_avx2(&cospim48, &u[20], &cospim16, &u[27], &rnding, bit);
    u[27] = half_btf_avx2(&cospim16, &u[20], &cospi48, &u[27], &rnding, bit);
    u[20] = temp1;
    temp2 = half_btf_avx2(&cospim48, &u[21], &cospim16, &u[26], &rnding, bit);
    u[26] = half_btf_avx2(&cospim16, &u[21], &cospi48, &u[26], &rnding, bit);
    u[21] = temp2;
    for (i = 32; i < 64; i += 16) {
      for (j = i; j < i + 4; j++) {
        addsub_avx2(u[j], u[j ^ 7], &u[j], &u[j ^ 7], &clamp_lo, &clamp_hi);
        addsub_avx2(u[j ^ 15], u[j ^ 8], &u[j ^ 15], &u[j ^ 8], &clamp_lo,
                    &clamp_hi);
      }
    }

    // stage 8
    u[7] = u[0];
    u[6] = u[1];
    u[5] = u[2];
    u[4] = u[3];

    idct64_stage8_avx2(u, &cospim32, &cospi32, &cospim16, &cospi48, &cospi16,
                       &cospim48, &clamp_lo, &clamp_hi, &rnding, bit);

    // stage 9
    idct64_stage9_avx2(u, &cospim32, &cospi32, &clamp_lo, &clamp_hi, &rnding,
                       bit);

    // stage 10
    idct64_stage10_avx2(u, &cospim32, &cospi32, &clamp_lo, &clamp_hi, &rnding,
                        bit);

    // stage 11
    idct64_stage11_avx2(u, out, do_cols, bd, out_shift, &clamp_lo, &clamp_hi);
  }
}
static void idct64_low16_avx2(__m256i *in, __m256i *out, int bit, int do_cols,
                              int bd, int out_shift) {
  int i, j;
  const int32_t *cospi = cospi_arr(bit);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);

  const __m256i cospi1 = _mm256_set1_epi32(cospi[1]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi3 = _mm256_set1_epi32(cospi[3]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi5 = _mm256_set1_epi32(cospi[5]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi7 = _mm256_set1_epi32(cospi[7]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi9 = _mm256_set1_epi32(cospi[9]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi11 = _mm256_set1_epi32(cospi[11]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi13 = _mm256_set1_epi32(cospi[13]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi15 = _mm256_set1_epi32(cospi[15]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi51 = _mm256_set1_epi32(cospi[51]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi55 = _mm256_set1_epi32(cospi[55]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi59 = _mm256_set1_epi32(cospi[59]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi63 = _mm256_set1_epi32(cospi[63]);

  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim12 = _mm256_set1_epi32(-cospi[12]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospim28 = _mm256_set1_epi32(-cospi[28]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim44 = _mm256_set1_epi32(-cospi[44]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospim49 = _mm256_set1_epi32(-cospi[49]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospim53 = _mm256_set1_epi32(-cospi[53]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim57 = _mm256_set1_epi32(-cospi[57]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospim60 = _mm256_set1_epi32(-cospi[60]);
  const __m256i cospim61 = _mm256_set1_epi32(-cospi[61]);

  {
    __m256i u[64];
    __m256i tmp1, tmp2, tmp3, tmp4;
    // stage 1
    u[0] = in[0];
    u[32] = in[1];
    u[36] = in[9];
    u[40] = in[5];
    u[44] = in[13];
    u[48] = in[3];
    u[52] = in[11];
    u[56] = in[7];
    u[60] = in[15];
    u[16] = in[2];
    u[20] = in[10];
    u[24] = in[6];
    u[28] = in[14];
    u[4] = in[8];
    u[8] = in[4];
    u[12] = in[12];

    // stage 2
    u[63] = half_btf_0_avx2(&cospi1, &u[32], &rnding, bit);
    u[32] = half_btf_0_avx2(&cospi63, &u[32], &rnding, bit);
    u[35] = half_btf_0_avx2(&cospim49, &u[60], &rnding, bit);
    u[60] = half_btf_0_avx2(&cospi15, &u[60], &rnding, bit);
    u[59] = half_btf_0_avx2(&cospi9, &u[36], &rnding, bit);
    u[36] = half_btf_0_avx2(&cospi55, &u[36], &rnding, bit);
    u[39] = half_btf_0_avx2(&cospim57, &u[56], &rnding, bit);
    u[56] = half_btf_0_avx2(&cospi7, &u[56], &rnding, bit);
    u[55] = half_btf_0_avx2(&cospi5, &u[40], &rnding, bit);
    u[40] = half_btf_0_avx2(&cospi59, &u[40], &rnding, bit);
    u[43] = half_btf_0_avx2(&cospim53, &u[52], &rnding, bit);
    u[52] = half_btf_0_avx2(&cospi11, &u[52], &rnding, bit);
    u[47] = half_btf_0_avx2(&cospim61, &u[48], &rnding, bit);
    u[48] = half_btf_0_avx2(&cospi3, &u[48], &rnding, bit);
    u[51] = half_btf_0_avx2(&cospi13, &u[44], &rnding, bit);
    u[44] = half_btf_0_avx2(&cospi51, &u[44], &rnding, bit);

    // stage 3
    u[31] = half_btf_0_avx2(&cospi2, &u[16], &rnding, bit);
    u[16] = half_btf_0_avx2(&cospi62, &u[16], &rnding, bit);
    u[19] = half_btf_0_avx2(&cospim50, &u[28], &rnding, bit);
    u[28] = half_btf_0_avx2(&cospi14, &u[28], &rnding, bit);
    u[27] = half_btf_0_avx2(&cospi10, &u[20], &rnding, bit);
    u[20] = half_btf_0_avx2(&cospi54, &u[20], &rnding, bit);
    u[23] = half_btf_0_avx2(&cospim58, &u[24], &rnding, bit);
    u[24] = half_btf_0_avx2(&cospi6, &u[24], &rnding, bit);
    u[33] = u[32];
    u[34] = u[35];
    u[37] = u[36];
    u[38] = u[39];
    u[41] = u[40];
    u[42] = u[43];
    u[45] = u[44];
    u[46] = u[47];
    u[49] = u[48];
    u[50] = u[51];
    u[53] = u[52];
    u[54] = u[55];
    u[57] = u[56];
    u[58] = u[59];
    u[61] = u[60];
    u[62] = u[63];

    // stage 4
    u[15] = half_btf_0_avx2(&cospi4, &u[8], &rnding, bit);
    u[8] = half_btf_0_avx2(&cospi60, &u[8], &rnding, bit);
    u[11] = half_btf_0_avx2(&cospim52, &u[12], &rnding, bit);
    u[12] = half_btf_0_avx2(&cospi12, &u[12], &rnding, bit);

    u[17] = u[16];
    u[18] = u[19];
    u[21] = u[20];
    u[22] = u[23];
    u[25] = u[24];
    u[26] = u[27];
    u[29] = u[28];
    u[30] = u[31];

    tmp1 = half_btf_avx2(&cospim4, &u[33], &cospi60, &u[62], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim60, &u[34], &cospim4, &u[61], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim36, &u[37], &cospi28, &u[58], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim28, &u[38], &cospim36, &u[57], &rnding, bit);
    u[57] = half_btf_avx2(&cospim36, &u[38], &cospi28, &u[57], &rnding, bit);
    u[58] = half_btf_avx2(&cospi28, &u[37], &cospi36, &u[58], &rnding, bit);
    u[61] = half_btf_avx2(&cospim4, &u[34], &cospi60, &u[61], &rnding, bit);
    u[62] = half_btf_avx2(&cospi60, &u[33], &cospi4, &u[62], &rnding, bit);
    u[33] = tmp1;
    u[34] = tmp2;
    u[37] = tmp3;
    u[38] = tmp4;

    tmp1 = half_btf_avx2(&cospim20, &u[41], &cospi44, &u[54], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim44, &u[42], &cospim20, &u[53], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim52, &u[45], &cospi12, &u[50], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim12, &u[46], &cospim52, &u[49], &rnding, bit);
    u[49] = half_btf_avx2(&cospim52, &u[46], &cospi12, &u[49], &rnding, bit);
    u[50] = half_btf_avx2(&cospi12, &u[45], &cospi52, &u[50], &rnding, bit);
    u[53] = half_btf_avx2(&cospim20, &u[42], &cospi44, &u[53], &rnding, bit);
    u[54] = half_btf_avx2(&cospi44, &u[41], &cospi20, &u[54], &rnding, bit);
    u[41] = tmp1;
    u[42] = tmp2;
    u[45] = tmp3;
    u[46] = tmp4;

    // stage 5
    u[7] = half_btf_0_avx2(&cospi8, &u[4], &rnding, bit);
    u[4] = half_btf_0_avx2(&cospi56, &u[4], &rnding, bit);

    u[9] = u[8];
    u[10] = u[11];
    u[13] = u[12];
    u[14] = u[15];

    tmp1 = half_btf_avx2(&cospim8, &u[17], &cospi56, &u[30], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim56, &u[18], &cospim8, &u[29], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim40, &u[21], &cospi24, &u[26], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim24, &u[22], &cospim40, &u[25], &rnding, bit);
    u[25] = half_btf_avx2(&cospim40, &u[22], &cospi24, &u[25], &rnding, bit);
    u[26] = half_btf_avx2(&cospi24, &u[21], &cospi40, &u[26], &rnding, bit);
    u[29] = half_btf_avx2(&cospim8, &u[18], &cospi56, &u[29], &rnding, bit);
    u[30] = half_btf_avx2(&cospi56, &u[17], &cospi8, &u[30], &rnding, bit);
    u[17] = tmp1;
    u[18] = tmp2;
    u[21] = tmp3;
    u[22] = tmp4;

    for (i = 32; i < 64; i += 8) {
      addsub_avx2(u[i + 0], u[i + 3], &u[i + 0], &u[i + 3], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 1], u[i + 2], &u[i + 1], &u[i + 2], &clamp_lo,
                  &clamp_hi);

      addsub_avx2(u[i + 7], u[i + 4], &u[i + 7], &u[i + 4], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 6], u[i + 5], &u[i + 6], &u[i + 5], &clamp_lo,
                  &clamp_hi);
    }

    // stage 6
    tmp1 = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    u[1] = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    u[0] = tmp1;
    u[5] = u[4];
    u[6] = u[7];

    tmp1 = half_btf_avx2(&cospim16, &u[9], &cospi48, &u[14], &rnding, bit);
    u[14] = half_btf_avx2(&cospi48, &u[9], &cospi16, &u[14], &rnding, bit);
    u[9] = tmp1;
    tmp2 = half_btf_avx2(&cospim48, &u[10], &cospim16, &u[13], &rnding, bit);
    u[13] = half_btf_avx2(&cospim16, &u[10], &cospi48, &u[13], &rnding, bit);
    u[10] = tmp2;

    for (i = 16; i < 32; i += 8) {
      addsub_avx2(u[i + 0], u[i + 3], &u[i + 0], &u[i + 3], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 1], u[i + 2], &u[i + 1], &u[i + 2], &clamp_lo,
                  &clamp_hi);

      addsub_avx2(u[i + 7], u[i + 4], &u[i + 7], &u[i + 4], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 6], u[i + 5], &u[i + 6], &u[i + 5], &clamp_lo,
                  &clamp_hi);
    }

    tmp1 = half_btf_avx2(&cospim8, &u[34], &cospi56, &u[61], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim8, &u[35], &cospi56, &u[60], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim56, &u[36], &cospim8, &u[59], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim56, &u[37], &cospim8, &u[58], &rnding, bit);
    u[58] = half_btf_avx2(&cospim8, &u[37], &cospi56, &u[58], &rnding, bit);
    u[59] = half_btf_avx2(&cospim8, &u[36], &cospi56, &u[59], &rnding, bit);
    u[60] = half_btf_avx2(&cospi56, &u[35], &cospi8, &u[60], &rnding, bit);
    u[61] = half_btf_avx2(&cospi56, &u[34], &cospi8, &u[61], &rnding, bit);
    u[34] = tmp1;
    u[35] = tmp2;
    u[36] = tmp3;
    u[37] = tmp4;

    tmp1 = half_btf_avx2(&cospim40, &u[42], &cospi24, &u[53], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim40, &u[43], &cospi24, &u[52], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim24, &u[44], &cospim40, &u[51], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim24, &u[45], &cospim40, &u[50], &rnding, bit);
    u[50] = half_btf_avx2(&cospim40, &u[45], &cospi24, &u[50], &rnding, bit);
    u[51] = half_btf_avx2(&cospim40, &u[44], &cospi24, &u[51], &rnding, bit);
    u[52] = half_btf_avx2(&cospi24, &u[43], &cospi40, &u[52], &rnding, bit);
    u[53] = half_btf_avx2(&cospi24, &u[42], &cospi40, &u[53], &rnding, bit);
    u[42] = tmp1;
    u[43] = tmp2;
    u[44] = tmp3;
    u[45] = tmp4;

    // stage 7
    u[3] = u[0];
    u[2] = u[1];
    tmp1 = half_btf_avx2(&cospim32, &u[5], &cospi32, &u[6], &rnding, bit);
    u[6] = half_btf_avx2(&cospi32, &u[5], &cospi32, &u[6], &rnding, bit);
    u[5] = tmp1;
    addsub_avx2(u[8], u[11], &u[8], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(u[9], u[10], &u[9], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(u[15], u[12], &u[15], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(u[14], u[13], &u[14], &u[13], &clamp_lo, &clamp_hi);

    tmp1 = half_btf_avx2(&cospim16, &u[18], &cospi48, &u[29], &rnding, bit);
    tmp2 = half_btf_avx2(&cospim16, &u[19], &cospi48, &u[28], &rnding, bit);
    tmp3 = half_btf_avx2(&cospim48, &u[20], &cospim16, &u[27], &rnding, bit);
    tmp4 = half_btf_avx2(&cospim48, &u[21], &cospim16, &u[26], &rnding, bit);
    u[26] = half_btf_avx2(&cospim16, &u[21], &cospi48, &u[26], &rnding, bit);
    u[27] = half_btf_avx2(&cospim16, &u[20], &cospi48, &u[27], &rnding, bit);
    u[28] = half_btf_avx2(&cospi48, &u[19], &cospi16, &u[28], &rnding, bit);
    u[29] = half_btf_avx2(&cospi48, &u[18], &cospi16, &u[29], &rnding, bit);
    u[18] = tmp1;
    u[19] = tmp2;
    u[20] = tmp3;
    u[21] = tmp4;

    for (i = 32; i < 64; i += 16) {
      for (j = i; j < i + 4; j++) {
        addsub_avx2(u[j], u[j ^ 7], &u[j], &u[j ^ 7], &clamp_lo, &clamp_hi);
        addsub_avx2(u[j ^ 15], u[j ^ 8], &u[j ^ 15], &u[j ^ 8], &clamp_lo,
                    &clamp_hi);
      }
    }

    // stage 8
    for (i = 0; i < 4; ++i) {
      addsub_avx2(u[i], u[7 - i], &u[i], &u[7 - i], &clamp_lo, &clamp_hi);
    }

    idct64_stage8_avx2(u, &cospim32, &cospi32, &cospim16, &cospi48, &cospi16,
                       &cospim48, &clamp_lo, &clamp_hi, &rnding, bit);

    // stage 9
    idct64_stage9_avx2(u, &cospim32, &cospi32, &clamp_lo, &clamp_hi, &rnding,
                       bit);

    // stage 10
    idct64_stage10_avx2(u, &cospim32, &cospi32, &clamp_lo, &clamp_hi, &rnding,
                        bit);

    // stage 11
    idct64_stage11_avx2(u, out, do_cols, bd, out_shift, &clamp_lo, &clamp_hi);
  }
}
static void idct64_avx2(__m256i *in, __m256i *out, int bit, int do_cols, int bd,
                        int out_shift) {
  int i, j;
  const int32_t *cospi = cospi_arr(bit);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const int log_range = AOMMAX(16, bd + (do_cols ? 6 : 8));
  const __m256i clamp_lo = _mm256_set1_epi32(-(1 << (log_range - 1)));
  const __m256i clamp_hi = _mm256_set1_epi32((1 << (log_range - 1)) - 1);

  const __m256i cospi1 = _mm256_set1_epi32(cospi[1]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi3 = _mm256_set1_epi32(cospi[3]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi5 = _mm256_set1_epi32(cospi[5]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospi7 = _mm256_set1_epi32(cospi[7]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi9 = _mm256_set1_epi32(cospi[9]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi11 = _mm256_set1_epi32(cospi[11]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi13 = _mm256_set1_epi32(cospi[13]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospi15 = _mm256_set1_epi32(cospi[15]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi17 = _mm256_set1_epi32(cospi[17]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi19 = _mm256_set1_epi32(cospi[19]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi21 = _mm256_set1_epi32(cospi[21]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospi23 = _mm256_set1_epi32(cospi[23]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi25 = _mm256_set1_epi32(cospi[25]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi27 = _mm256_set1_epi32(cospi[27]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi29 = _mm256_set1_epi32(cospi[29]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospi31 = _mm256_set1_epi32(cospi[31]);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi35 = _mm256_set1_epi32(cospi[35]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospi39 = _mm256_set1_epi32(cospi[39]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi43 = _mm256_set1_epi32(cospi[43]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospi47 = _mm256_set1_epi32(cospi[47]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi51 = _mm256_set1_epi32(cospi[51]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospi55 = _mm256_set1_epi32(cospi[55]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi59 = _mm256_set1_epi32(cospi[59]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospi63 = _mm256_set1_epi32(cospi[63]);

  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospim12 = _mm256_set1_epi32(-cospi[12]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospim28 = _mm256_set1_epi32(-cospi[28]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospim33 = _mm256_set1_epi32(-cospi[33]);
  const __m256i cospim34 = _mm256_set1_epi32(-cospi[34]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospim37 = _mm256_set1_epi32(-cospi[37]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospim41 = _mm256_set1_epi32(-cospi[41]);
  const __m256i cospim42 = _mm256_set1_epi32(-cospi[42]);
  const __m256i cospim44 = _mm256_set1_epi32(-cospi[44]);
  const __m256i cospim45 = _mm256_set1_epi32(-cospi[45]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospim49 = _mm256_set1_epi32(-cospi[49]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospim53 = _mm256_set1_epi32(-cospi[53]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim57 = _mm256_set1_epi32(-cospi[57]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i cospim60 = _mm256_set1_epi32(-cospi[60]);
  const __m256i cospim61 = _mm256_set1_epi32(-cospi[61]);

  {
    __m256i u[64], v[64];

    // stage 1
    u[32] = in[1];
    u[34] = in[17];
    u[36] = in[9];
    u[38] = in[25];
    u[40] = in[5];
    u[42] = in[21];
    u[44] = in[13];
    u[46] = in[29];
    u[48] = in[3];
    u[50] = in[19];
    u[52] = in[11];
    u[54] = in[27];
    u[56] = in[7];
    u[58] = in[23];
    u[60] = in[15];
    u[62] = in[31];

    v[16] = in[2];
    v[18] = in[18];
    v[20] = in[10];
    v[22] = in[26];
    v[24] = in[6];
    v[26] = in[22];
    v[28] = in[14];
    v[30] = in[30];

    u[8] = in[4];
    u[10] = in[20];
    u[12] = in[12];
    u[14] = in[28];

    v[4] = in[8];
    v[6] = in[24];

    u[0] = in[0];
    u[2] = in[16];

    // stage 2
    v[32] = half_btf_0_avx2(&cospi63, &u[32], &rnding, bit);
    v[33] = half_btf_0_avx2(&cospim33, &u[62], &rnding, bit);
    v[34] = half_btf_0_avx2(&cospi47, &u[34], &rnding, bit);
    v[35] = half_btf_0_avx2(&cospim49, &u[60], &rnding, bit);
    v[36] = half_btf_0_avx2(&cospi55, &u[36], &rnding, bit);
    v[37] = half_btf_0_avx2(&cospim41, &u[58], &rnding, bit);
    v[38] = half_btf_0_avx2(&cospi39, &u[38], &rnding, bit);
    v[39] = half_btf_0_avx2(&cospim57, &u[56], &rnding, bit);
    v[40] = half_btf_0_avx2(&cospi59, &u[40], &rnding, bit);
    v[41] = half_btf_0_avx2(&cospim37, &u[54], &rnding, bit);
    v[42] = half_btf_0_avx2(&cospi43, &u[42], &rnding, bit);
    v[43] = half_btf_0_avx2(&cospim53, &u[52], &rnding, bit);
    v[44] = half_btf_0_avx2(&cospi51, &u[44], &rnding, bit);
    v[45] = half_btf_0_avx2(&cospim45, &u[50], &rnding, bit);
    v[46] = half_btf_0_avx2(&cospi35, &u[46], &rnding, bit);
    v[47] = half_btf_0_avx2(&cospim61, &u[48], &rnding, bit);
    v[48] = half_btf_0_avx2(&cospi3, &u[48], &rnding, bit);
    v[49] = half_btf_0_avx2(&cospi29, &u[46], &rnding, bit);
    v[50] = half_btf_0_avx2(&cospi19, &u[50], &rnding, bit);
    v[51] = half_btf_0_avx2(&cospi13, &u[44], &rnding, bit);
    v[52] = half_btf_0_avx2(&cospi11, &u[52], &rnding, bit);
    v[53] = half_btf_0_avx2(&cospi21, &u[42], &rnding, bit);
    v[54] = half_btf_0_avx2(&cospi27, &u[54], &rnding, bit);
    v[55] = half_btf_0_avx2(&cospi5, &u[40], &rnding, bit);
    v[56] = half_btf_0_avx2(&cospi7, &u[56], &rnding, bit);
    v[57] = half_btf_0_avx2(&cospi25, &u[38], &rnding, bit);
    v[58] = half_btf_0_avx2(&cospi23, &u[58], &rnding, bit);
    v[59] = half_btf_0_avx2(&cospi9, &u[36], &rnding, bit);
    v[60] = half_btf_0_avx2(&cospi15, &u[60], &rnding, bit);
    v[61] = half_btf_0_avx2(&cospi17, &u[34], &rnding, bit);
    v[62] = half_btf_0_avx2(&cospi31, &u[62], &rnding, bit);
    v[63] = half_btf_0_avx2(&cospi1, &u[32], &rnding, bit);

    // stage 3
    u[16] = half_btf_0_avx2(&cospi62, &v[16], &rnding, bit);
    u[17] = half_btf_0_avx2(&cospim34, &v[30], &rnding, bit);
    u[18] = half_btf_0_avx2(&cospi46, &v[18], &rnding, bit);
    u[19] = half_btf_0_avx2(&cospim50, &v[28], &rnding, bit);
    u[20] = half_btf_0_avx2(&cospi54, &v[20], &rnding, bit);
    u[21] = half_btf_0_avx2(&cospim42, &v[26], &rnding, bit);
    u[22] = half_btf_0_avx2(&cospi38, &v[22], &rnding, bit);
    u[23] = half_btf_0_avx2(&cospim58, &v[24], &rnding, bit);
    u[24] = half_btf_0_avx2(&cospi6, &v[24], &rnding, bit);
    u[25] = half_btf_0_avx2(&cospi26, &v[22], &rnding, bit);
    u[26] = half_btf_0_avx2(&cospi22, &v[26], &rnding, bit);
    u[27] = half_btf_0_avx2(&cospi10, &v[20], &rnding, bit);
    u[28] = half_btf_0_avx2(&cospi14, &v[28], &rnding, bit);
    u[29] = half_btf_0_avx2(&cospi18, &v[18], &rnding, bit);
    u[30] = half_btf_0_avx2(&cospi30, &v[30], &rnding, bit);
    u[31] = half_btf_0_avx2(&cospi2, &v[16], &rnding, bit);

    for (i = 32; i < 64; i += 4) {
      addsub_avx2(v[i + 0], v[i + 1], &u[i + 0], &u[i + 1], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(v[i + 3], v[i + 2], &u[i + 3], &u[i + 2], &clamp_lo,
                  &clamp_hi);
    }

    // stage 4
    v[8] = half_btf_0_avx2(&cospi60, &u[8], &rnding, bit);
    v[9] = half_btf_0_avx2(&cospim36, &u[14], &rnding, bit);
    v[10] = half_btf_0_avx2(&cospi44, &u[10], &rnding, bit);
    v[11] = half_btf_0_avx2(&cospim52, &u[12], &rnding, bit);
    v[12] = half_btf_0_avx2(&cospi12, &u[12], &rnding, bit);
    v[13] = half_btf_0_avx2(&cospi20, &u[10], &rnding, bit);
    v[14] = half_btf_0_avx2(&cospi28, &u[14], &rnding, bit);
    v[15] = half_btf_0_avx2(&cospi4, &u[8], &rnding, bit);

    for (i = 16; i < 32; i += 4) {
      addsub_avx2(u[i + 0], u[i + 1], &v[i + 0], &v[i + 1], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 3], u[i + 2], &v[i + 3], &v[i + 2], &clamp_lo,
                  &clamp_hi);
    }

    for (i = 32; i < 64; i += 4) {
      v[i + 0] = u[i + 0];
      v[i + 3] = u[i + 3];
    }

    v[33] = half_btf_avx2(&cospim4, &u[33], &cospi60, &u[62], &rnding, bit);
    v[34] = half_btf_avx2(&cospim60, &u[34], &cospim4, &u[61], &rnding, bit);
    v[37] = half_btf_avx2(&cospim36, &u[37], &cospi28, &u[58], &rnding, bit);
    v[38] = half_btf_avx2(&cospim28, &u[38], &cospim36, &u[57], &rnding, bit);
    v[41] = half_btf_avx2(&cospim20, &u[41], &cospi44, &u[54], &rnding, bit);
    v[42] = half_btf_avx2(&cospim44, &u[42], &cospim20, &u[53], &rnding, bit);
    v[45] = half_btf_avx2(&cospim52, &u[45], &cospi12, &u[50], &rnding, bit);
    v[46] = half_btf_avx2(&cospim12, &u[46], &cospim52, &u[49], &rnding, bit);
    v[49] = half_btf_avx2(&cospim52, &u[46], &cospi12, &u[49], &rnding, bit);
    v[50] = half_btf_avx2(&cospi12, &u[45], &cospi52, &u[50], &rnding, bit);
    v[53] = half_btf_avx2(&cospim20, &u[42], &cospi44, &u[53], &rnding, bit);
    v[54] = half_btf_avx2(&cospi44, &u[41], &cospi20, &u[54], &rnding, bit);
    v[57] = half_btf_avx2(&cospim36, &u[38], &cospi28, &u[57], &rnding, bit);
    v[58] = half_btf_avx2(&cospi28, &u[37], &cospi36, &u[58], &rnding, bit);
    v[61] = half_btf_avx2(&cospim4, &u[34], &cospi60, &u[61], &rnding, bit);
    v[62] = half_btf_avx2(&cospi60, &u[33], &cospi4, &u[62], &rnding, bit);

    // stage 5
    u[4] = half_btf_0_avx2(&cospi56, &v[4], &rnding, bit);
    u[5] = half_btf_0_avx2(&cospim40, &v[6], &rnding, bit);
    u[6] = half_btf_0_avx2(&cospi24, &v[6], &rnding, bit);
    u[7] = half_btf_0_avx2(&cospi8, &v[4], &rnding, bit);

    for (i = 8; i < 16; i += 4) {
      addsub_avx2(v[i + 0], v[i + 1], &u[i + 0], &u[i + 1], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(v[i + 3], v[i + 2], &u[i + 3], &u[i + 2], &clamp_lo,
                  &clamp_hi);
    }

    for (i = 16; i < 32; i += 4) {
      u[i + 0] = v[i + 0];
      u[i + 3] = v[i + 3];
    }

    u[17] = half_btf_avx2(&cospim8, &v[17], &cospi56, &v[30], &rnding, bit);
    u[18] = half_btf_avx2(&cospim56, &v[18], &cospim8, &v[29], &rnding, bit);
    u[21] = half_btf_avx2(&cospim40, &v[21], &cospi24, &v[26], &rnding, bit);
    u[22] = half_btf_avx2(&cospim24, &v[22], &cospim40, &v[25], &rnding, bit);
    u[25] = half_btf_avx2(&cospim40, &v[22], &cospi24, &v[25], &rnding, bit);
    u[26] = half_btf_avx2(&cospi24, &v[21], &cospi40, &v[26], &rnding, bit);
    u[29] = half_btf_avx2(&cospim8, &v[18], &cospi56, &v[29], &rnding, bit);
    u[30] = half_btf_avx2(&cospi56, &v[17], &cospi8, &v[30], &rnding, bit);

    for (i = 32; i < 64; i += 8) {
      addsub_avx2(v[i + 0], v[i + 3], &u[i + 0], &u[i + 3], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(v[i + 1], v[i + 2], &u[i + 1], &u[i + 2], &clamp_lo,
                  &clamp_hi);

      addsub_avx2(v[i + 7], v[i + 4], &u[i + 7], &u[i + 4], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(v[i + 6], v[i + 5], &u[i + 6], &u[i + 5], &clamp_lo,
                  &clamp_hi);
    }

    // stage 6
    v[0] = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    v[1] = half_btf_0_avx2(&cospi32, &u[0], &rnding, bit);
    v[2] = half_btf_0_avx2(&cospi48, &u[2], &rnding, bit);
    v[3] = half_btf_0_avx2(&cospi16, &u[2], &rnding, bit);

    addsub_avx2(u[4], u[5], &v[4], &v[5], &clamp_lo, &clamp_hi);
    addsub_avx2(u[7], u[6], &v[7], &v[6], &clamp_lo, &clamp_hi);

    for (i = 8; i < 16; i += 4) {
      v[i + 0] = u[i + 0];
      v[i + 3] = u[i + 3];
    }

    v[9] = half_btf_avx2(&cospim16, &u[9], &cospi48, &u[14], &rnding, bit);
    v[10] = half_btf_avx2(&cospim48, &u[10], &cospim16, &u[13], &rnding, bit);
    v[13] = half_btf_avx2(&cospim16, &u[10], &cospi48, &u[13], &rnding, bit);
    v[14] = half_btf_avx2(&cospi48, &u[9], &cospi16, &u[14], &rnding, bit);

    for (i = 16; i < 32; i += 8) {
      addsub_avx2(u[i + 0], u[i + 3], &v[i + 0], &v[i + 3], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 1], u[i + 2], &v[i + 1], &v[i + 2], &clamp_lo,
                  &clamp_hi);

      addsub_avx2(u[i + 7], u[i + 4], &v[i + 7], &v[i + 4], &clamp_lo,
                  &clamp_hi);
      addsub_avx2(u[i + 6], u[i + 5], &v[i + 6], &v[i + 5], &clamp_lo,
                  &clamp_hi);
    }

    for (i = 32; i < 64; i += 8) {
      v[i + 0] = u[i + 0];
      v[i + 1] = u[i + 1];
      v[i + 6] = u[i + 6];
      v[i + 7] = u[i + 7];
    }

    v[34] = half_btf_avx2(&cospim8, &u[34], &cospi56, &u[61], &rnding, bit);
    v[35] = half_btf_avx2(&cospim8, &u[35], &cospi56, &u[60], &rnding, bit);
    v[36] = half_btf_avx2(&cospim56, &u[36], &cospim8, &u[59], &rnding, bit);
    v[37] = half_btf_avx2(&cospim56, &u[37], &cospim8, &u[58], &rnding, bit);
    v[42] = half_btf_avx2(&cospim40, &u[42], &cospi24, &u[53], &rnding, bit);
    v[43] = half_btf_avx2(&cospim40, &u[43], &cospi24, &u[52], &rnding, bit);
    v[44] = half_btf_avx2(&cospim24, &u[44], &cospim40, &u[51], &rnding, bit);
    v[45] = half_btf_avx2(&cospim24, &u[45], &cospim40, &u[50], &rnding, bit);
    v[50] = half_btf_avx2(&cospim40, &u[45], &cospi24, &u[50], &rnding, bit);
    v[51] = half_btf_avx2(&cospim40, &u[44], &cospi24, &u[51], &rnding, bit);
    v[52] = half_btf_avx2(&cospi24, &u[43], &cospi40, &u[52], &rnding, bit);
    v[53] = half_btf_avx2(&cospi24, &u[42], &cospi40, &u[53], &rnding, bit);
    v[58] = half_btf_avx2(&cospim8, &u[37], &cospi56, &u[58], &rnding, bit);
    v[59] = half_btf_avx2(&cospim8, &u[36], &cospi56, &u[59], &rnding, bit);
    v[60] = half_btf_avx2(&cospi56, &u[35], &cospi8, &u[60], &rnding, bit);
    v[61] = half_btf_avx2(&cospi56, &u[34], &cospi8, &u[61], &rnding, bit);

    // stage 7
    addsub_avx2(v[0], v[3], &u[0], &u[3], &clamp_lo, &clamp_hi);
    addsub_avx2(v[1], v[2], &u[1], &u[2], &clamp_lo, &clamp_hi);

    u[4] = v[4];
    u[7] = v[7];
    u[5] = half_btf_avx2(&cospim32, &v[5], &cospi32, &v[6], &rnding, bit);
    u[6] = half_btf_avx2(&cospi32, &v[5], &cospi32, &v[6], &rnding, bit);

    addsub_avx2(v[8], v[11], &u[8], &u[11], &clamp_lo, &clamp_hi);
    addsub_avx2(v[9], v[10], &u[9], &u[10], &clamp_lo, &clamp_hi);
    addsub_avx2(v[15], v[12], &u[15], &u[12], &clamp_lo, &clamp_hi);
    addsub_avx2(v[14], v[13], &u[14], &u[13], &clamp_lo, &clamp_hi);

    for (i = 16; i < 32; i += 8) {
      u[i + 0] = v[i + 0];
      u[i + 1] = v[i + 1];
      u[i + 6] = v[i + 6];
      u[i + 7] = v[i + 7];
    }

    u[18] = half_btf_avx2(&cospim16, &v[18], &cospi48, &v[29], &rnding, bit);
    u[19] = half_btf_avx2(&cospim16, &v[19], &cospi48, &v[28], &rnding, bit);
    u[20] = half_btf_avx2(&cospim48, &v[20], &cospim16, &v[27], &rnding, bit);
    u[21] = half_btf_avx2(&cospim48, &v[21], &cospim16, &v[26], &rnding, bit);
    u[26] = half_btf_avx2(&cospim16, &v[21], &cospi48, &v[26], &rnding, bit);
    u[27] = half_btf_avx2(&cospim16, &v[20], &cospi48, &v[27], &rnding, bit);
    u[28] = half_btf_avx2(&cospi48, &v[19], &cospi16, &v[28], &rnding, bit);
    u[29] = half_btf_avx2(&cospi48, &v[18], &cospi16, &v[29], &rnding, bit);

    for (i = 32; i < 64; i += 16) {
      for (j = i; j < i + 4; j++) {
        addsub_avx2(v[j], v[j ^ 7], &u[j], &u[j ^ 7], &clamp_lo, &clamp_hi);
        addsub_avx2(v[j ^ 15], v[j ^ 8], &u[j ^ 15], &u[j ^ 8], &clamp_lo,
                    &clamp_hi);
      }
    }

    // stage 8
    for (i = 0; i < 4; ++i) {
      addsub_avx2(u[i], u[7 - i], &v[i], &v[7 - i], &clamp_lo, &clamp_hi);
    }

    v[8] = u[8];
    v[9] = u[9];
    v[14] = u[14];
    v[15] = u[15];

    v[10] = half_btf_avx2(&cospim32, &u[10], &cospi32, &u[13], &rnding, bit);
    v[11] = half_btf_avx2(&cospim32, &u[11], &cospi32, &u[12], &rnding, bit);
    v[12] = half_btf_avx2(&cospi32, &u[11], &cospi32, &u[12], &rnding, bit);
    v[13] = half_btf_avx2(&cospi32, &u[10], &cospi32, &u[13], &rnding, bit);

    for (i = 16; i < 20; ++i) {
      addsub_avx2(u[i], u[i ^ 7], &v[i], &v[i ^ 7], &clamp_lo, &clamp_hi);
      addsub_avx2(u[i ^ 15], u[i ^ 8], &v[i ^ 15], &v[i ^ 8], &clamp_lo,
                  &clamp_hi);
    }

    for (i = 32; i < 36; ++i) {
      v[i] = u[i];
      v[i + 12] = u[i + 12];
      v[i + 16] = u[i + 16];
      v[i + 28] = u[i + 28];
    }

    v[36] = half_btf_avx2(&cospim16, &u[36], &cospi48, &u[59], &rnding, bit);
    v[37] = half_btf_avx2(&cospim16, &u[37], &cospi48, &u[58], &rnding, bit);
    v[38] = half_btf_avx2(&cospim16, &u[38], &cospi48, &u[57], &rnding, bit);
    v[39] = half_btf_avx2(&cospim16, &u[39], &cospi48, &u[56], &rnding, bit);
    v[40] = half_btf_avx2(&cospim48, &u[40], &cospim16, &u[55], &rnding, bit);
    v[41] = half_btf_avx2(&cospim48, &u[41], &cospim16, &u[54], &rnding, bit);
    v[42] = half_btf_avx2(&cospim48, &u[42], &cospim16, &u[53], &rnding, bit);
    v[43] = half_btf_avx2(&cospim48, &u[43], &cospim16, &u[52], &rnding, bit);
    v[52] = half_btf_avx2(&cospim16, &u[43], &cospi48, &u[52], &rnding, bit);
    v[53] = half_btf_avx2(&cospim16, &u[42], &cospi48, &u[53], &rnding, bit);
    v[54] = half_btf_avx2(&cospim16, &u[41], &cospi48, &u[54], &rnding, bit);
    v[55] = half_btf_avx2(&cospim16, &u[40], &cospi48, &u[55], &rnding, bit);
    v[56] = half_btf_avx2(&cospi48, &u[39], &cospi16, &u[56], &rnding, bit);
    v[57] = half_btf_avx2(&cospi48, &u[38], &cospi16, &u[57], &rnding, bit);
    v[58] = half_btf_avx2(&cospi48, &u[37], &cospi16, &u[58], &rnding, bit);
    v[59] = half_btf_avx2(&cospi48, &u[36], &cospi16, &u[59], &rnding, bit);

    // stage 9
    for (i = 0; i < 8; ++i) {
      addsub_avx2(v[i], v[15 - i], &u[i], &u[15 - i], &clamp_lo, &clamp_hi);
    }

    for (i = 16; i < 20; ++i) {
      u[i] = v[i];
      u[i + 12] = v[i + 12];
    }

    u[20] = half_btf_avx2(&cospim32, &v[20], &cospi32, &v[27], &rnding, bit);
    u[21] = half_btf_avx2(&cospim32, &v[21], &cospi32, &v[26], &rnding, bit);
    u[22] = half_btf_avx2(&cospim32, &v[22], &cospi32, &v[25], &rnding, bit);
    u[23] = half_btf_avx2(&cospim32, &v[23], &cospi32, &v[24], &rnding, bit);
    u[24] = half_btf_avx2(&cospi32, &v[23], &cospi32, &v[24], &rnding, bit);
    u[25] = half_btf_avx2(&cospi32, &v[22], &cospi32, &v[25], &rnding, bit);
    u[26] = half_btf_avx2(&cospi32, &v[21], &cospi32, &v[26], &rnding, bit);
    u[27] = half_btf_avx2(&cospi32, &v[20], &cospi32, &v[27], &rnding, bit);

    for (i = 32; i < 40; i++) {
      addsub_avx2(v[i], v[i ^ 15], &u[i], &u[i ^ 15], &clamp_lo, &clamp_hi);
    }

    for (i = 48; i < 56; i++) {
      addsub_avx2(v[i ^ 15], v[i], &u[i ^ 15], &u[i], &clamp_lo, &clamp_hi);
    }

    // stage 10
    for (i = 0; i < 16; i++) {
      addsub_avx2(u[i], u[31 - i], &v[i], &v[31 - i], &clamp_lo, &clamp_hi);
    }

    for (i = 32; i < 40; i++) v[i] = u[i];

    v[40] = half_btf_avx2(&cospim32, &u[40], &cospi32, &u[55], &rnding, bit);
    v[41] = half_btf_avx2(&cospim32, &u[41], &cospi32, &u[54], &rnding, bit);
    v[42] = half_btf_avx2(&cospim32, &u[42], &cospi32, &u[53], &rnding, bit);
    v[43] = half_btf_avx2(&cospim32, &u[43], &cospi32, &u[52], &rnding, bit);
    v[44] = half_btf_avx2(&cospim32, &u[44], &cospi32, &u[51], &rnding, bit);
    v[45] = half_btf_avx2(&cospim32, &u[45], &cospi32, &u[50], &rnding, bit);
    v[46] = half_btf_avx2(&cospim32, &u[46], &cospi32, &u[49], &rnding, bit);
    v[47] = half_btf_avx2(&cospim32, &u[47], &cospi32, &u[48], &rnding, bit);
    v[48] = half_btf_avx2(&cospi32, &u[47], &cospi32, &u[48], &rnding, bit);
    v[49] = half_btf_avx2(&cospi32, &u[46], &cospi32, &u[49], &rnding, bit);
    v[50] = half_btf_avx2(&cospi32, &u[45], &cospi32, &u[50], &rnding, bit);
    v[51] = half_btf_avx2(&cospi32, &u[44], &cospi32, &u[51], &rnding, bit);
    v[52] = half_btf_avx2(&cospi32, &u[43], &cospi32, &u[52], &rnding, bit);
    v[53] = half_btf_avx2(&cospi32, &u[42], &cospi32, &u[53], &rnding, bit);
    v[54] = half_btf_avx2(&cospi32, &u[41], &cospi32, &u[54], &rnding, bit);
    v[55] = half_btf_avx2(&cospi32, &u[40], &cospi32, &u[55], &rnding, bit);

    for (i = 56; i < 64; i++) v[i] = u[i];

    // stage 11
    for (i = 0; i < 32; i++) {
      addsub_avx2(v[i], v[63 - i], &out[(i)], &out[(63 - i)], &clamp_lo,
                  &clamp_hi);
    }
    if (!do_cols) {
      const int log_range_out = AOMMAX(16, bd + 6);
      const __m256i clamp_lo_out =
          _mm256_set1_epi32(-(1 << (log_range_out - 1)));
      const __m256i clamp_hi_out =
          _mm256_set1_epi32((1 << (log_range_out - 1)) - 1);

      round_shift_8x8_avx2(out, out_shift);
      round_shift_8x8_avx2(out + 16, out_shift);
      round_shift_8x8_avx2(out + 32, out_shift);
      round_shift_8x8_avx2(out + 48, out_shift);
      highbd_clamp_epi32_avx2(out, out, &clamp_lo_out, &clamp_hi_out, 64);
    }
  }
}
typedef void (*transform_1d_avx2)(__m256i *in, __m256i *out, int bit,
                                  int do_cols, int bd, int out_shift);

#if CONFIG_INTER_DDT
static const transform_1d_avx2
    highbd_txfm_all_1d_zeros_w8_arr_inter[TX_SIZES][ITX_TYPES_1D][4] = {
      {
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct8x8_low1_avx2, idct8x8_avx2, NULL, NULL },
#if REPLACE_ADST8
          { iddt8x8_low1_avx2, iddt8x8_avx2, NULL, NULL },
#else
          { iadst8x8_low1_avx2, iadst8x8_avx2, NULL, NULL },
#endif
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct16_low1_avx2, idct16_low8_avx2, idct16_avx2, NULL },
#if REPLACE_ADST16
          { iddt16_low1_avx2, iddt16_low8_avx2, iddt16_avx2, NULL },
#else
          { iadst16_low1_avx2, iadst16_low8_avx2, iadst16_avx2, NULL },
#endif
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct32_low1_avx2, idct32_low8_avx2, idct32_low16_avx2,
            idct32_avx2 },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct64_low1_avx2, idct64_low8_avx2, idct64_low16_avx2,
            idct64_avx2 },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      }
    };
#endif  // CONFIG_INTER_DDT

static const transform_1d_avx2
    highbd_txfm_all_1d_zeros_w8_arr[TX_SIZES][ITX_TYPES_1D][4] = {
      {
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct8x8_low1_avx2, idct8x8_avx2, NULL, NULL },
          { iadst8x8_low1_avx2, iadst8x8_avx2, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct16_low1_avx2, idct16_low8_avx2, idct16_avx2, NULL },
          { iadst16_low1_avx2, iadst16_low8_avx2, iadst16_avx2, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct32_low1_avx2, idct32_low8_avx2, idct32_low16_avx2,
            idct32_avx2 },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      },
      {
          { idct64_low1_avx2, idct64_low8_avx2, idct64_low16_avx2,
            idct64_avx2 },
          { NULL, NULL, NULL, NULL },
          { NULL, NULL, NULL, NULL },
      }
    };

static void highbd_inv_txfm2d_add_no_identity_avx2(const int32_t *input,
                                                   uint16_t *output, int stride,
                                                   TX_TYPE tx_type,
                                                   TX_SIZE tx_size,
#if CONFIG_INTER_DDT
                                                   int use_ddt,
#endif  // CONFIG_INTER_DDT
                                                   int eob, const int bd) {
  __m256i buf1[64 * 8];
  int eobx, eoby;
  get_eobx_eoby_scan_default(&eobx, &eoby, tx_size, eob);
  const int8_t *shift = av1_inv_txfm_shift_ls[tx_size];
  const int txw_idx = get_txw_idx(tx_size);
  const int txh_idx = get_txh_idx(tx_size);
  const int txfm_size_col = tx_size_wide[tx_size];
  const int txfm_size_row = tx_size_high[tx_size];
  const int buf_size_w_div8 = txfm_size_col >> 3;
  const int buf_size_nonzero_w_div8 = (eobx + 8) >> 3;
  const int buf_size_nonzero_h_div8 = (eoby + 8) >> 3;
  const int input_stride = AOMMIN(32, txfm_size_col);
  const int rect_type = get_rect_tx_log_ratio(txfm_size_col, txfm_size_row);
  const int fun_idx_x = highbd_txfm_all_1d_zeros_idx[eobx];
  const int fun_idx_y = highbd_txfm_all_1d_zeros_idx[eoby];
#if CONFIG_INTER_DDT
  const transform_1d_avx2 row_txfm =
      use_ddt
          ? highbd_txfm_all_1d_zeros_w8_arr_inter[txw_idx][hitx_1d_tab[tx_type]]
                                                 [fun_idx_x]
          : highbd_txfm_all_1d_zeros_w8_arr[txw_idx][hitx_1d_tab[tx_type]]
                                           [fun_idx_x];
  const transform_1d_avx2 col_txfm =
      use_ddt
          ? highbd_txfm_all_1d_zeros_w8_arr_inter[txh_idx][vitx_1d_tab[tx_type]]
                                                 [fun_idx_y]
          : highbd_txfm_all_1d_zeros_w8_arr[txh_idx][vitx_1d_tab[tx_type]]
                                           [fun_idx_y];
#else
  const transform_1d_avx2 row_txfm =
      highbd_txfm_all_1d_zeros_w8_arr[txw_idx][hitx_1d_tab[tx_type]][fun_idx_x];
  const transform_1d_avx2 col_txfm =
      highbd_txfm_all_1d_zeros_w8_arr[txh_idx][vitx_1d_tab[tx_type]][fun_idx_y];
#endif  // CONFIG_INTER_DDT

  assert(col_txfm != NULL);
  assert(row_txfm != NULL);
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);

  // 1st stage: column transform
  for (int i = 0; i < buf_size_nonzero_h_div8; i++) {
    __m256i buf0[64];
    const int32_t *input_row = input + i * input_stride * 8;
    for (int j = 0; j < buf_size_nonzero_w_div8; ++j) {
      __m256i *buf0_cur = buf0 + j * 8;
      load_buffer_32x32(input_row + j * 8, buf0_cur, input_stride, 8);

      transpose_8x8_avx2(&buf0_cur[0], &buf0_cur[0]);
    }
    // rect special code where size difference is a factor of 2
    if (abs(rect_type) % 2 == 1) {
      av1_round_shift_rect_array_32_avx2(
          buf0, buf0, buf_size_nonzero_w_div8 << 3, 0, NewInvSqrt2);
    }
    row_txfm(buf0, buf0, av1_inv_cos_bit_row[txw_idx][txh_idx], 0, bd,
             -shift[0]);

    __m256i *_buf1 = buf1 + i * 8;
    if (lr_flip) {
      for (int j = 0; j < buf_size_w_div8; ++j) {
        transpose_8x8_flip_avx2(
            &buf0[j * 8], &_buf1[(buf_size_w_div8 - 1 - j) * txfm_size_row]);
      }
    } else {
      for (int j = 0; j < buf_size_w_div8; ++j) {
        transpose_8x8_avx2(&buf0[j * 8], &_buf1[j * txfm_size_row]);
      }
    }
  }
  // 2nd stage: column transform
  for (int i = 0; i < buf_size_w_div8; i++) {
    col_txfm(buf1 + i * txfm_size_row, buf1 + i * txfm_size_row,
             av1_inv_cos_bit_col[txw_idx][txh_idx], 1, bd, 0);

    av1_round_shift_array_32_avx2(buf1 + i * txfm_size_row,
                                  buf1 + i * txfm_size_row, txfm_size_row,
                                  -shift[1]);
  }

  // write to buffer
  if (txfm_size_col >= 16) {
    for (int i = 0; i < (txfm_size_col >> 4); i++) {
      highbd_write_buffer_16xn_avx2(buf1 + i * txfm_size_row * 2,
                                    output + 16 * i, stride, ud_flip,
                                    txfm_size_row, bd);
    }
  } else if (txfm_size_col == 8) {
    highbd_write_buffer_8xn_avx2(buf1, output, stride, ud_flip, txfm_size_row,
                                 bd);
  }
}

void av1_highbd_inv_txfm2d_add_universe_avx2(const int32_t *input,
                                             uint16_t *output, int stride,
                                             TX_TYPE tx_type, TX_SIZE tx_size,
#if CONFIG_INTER_DDT
                                             int use_ddt,
#endif  // CONFIG_INTER_DDT
                                             int eob, const int bd) {
  switch (tx_type) {
    case DCT_DCT:
    case ADST_DCT:
    case DCT_ADST:
    case ADST_ADST:
    case FLIPADST_DCT:
    case DCT_FLIPADST:
    case FLIPADST_FLIPADST:
    case ADST_FLIPADST:
    case FLIPADST_ADST:
      highbd_inv_txfm2d_add_no_identity_avx2(input, output, stride, tx_type,
                                             tx_size,
#if CONFIG_INTER_DDT
                                             use_ddt,
#endif  // CONFIG_INTER_DDT
                                             eob, bd);
      break;
    case IDTX:
    case H_DCT:
    case H_ADST:
    case H_FLIPADST:
    case V_DCT:
    case V_ADST:
    case V_FLIPADST:
      av1_highbd_inv_txfm2d_add_universe_sse4_1(input, output, stride, tx_type,
                                                tx_size,
#if CONFIG_INTER_DDT
                                                use_ddt,
#endif  // CONFIG_INTER_DDT
                                                eob, bd);
      break;
    default: assert(0); break;
  }
}
void av1_highbd_inv_txfm_add_avx2(const tran_low_t *input, uint16_t *dest,
                                  int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
#if CONFIG_CORE_TX
  inv_txfm(input, dest, stride, txfm_param);
#else
  const TX_SIZE tx_size = txfm_param->tx_size;
  switch (tx_size) {
    case TX_4X8:
      av1_highbd_inv_txfm_add_4x8_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_8X4:
      av1_highbd_inv_txfm_add_8x4_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_4X4:
      av1_highbd_inv_txfm_add_4x4_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_16X4:
      av1_highbd_inv_txfm_add_16x4_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_4X16:
      av1_highbd_inv_txfm_add_4x16_sse4_1(input, dest, stride, txfm_param);
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case TX_4X32:
      av1_highbd_inv_txfm_add_4x32_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_32X4:
      av1_highbd_inv_txfm_add_32x4_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_4X64:
      av1_highbd_inv_txfm_add_4x64_sse4_1(input, dest, stride, txfm_param);
      break;
    case TX_64X4:
      av1_highbd_inv_txfm_add_64x4_sse4_1(input, dest, stride, txfm_param);
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default:
      av1_highbd_inv_txfm2d_add_universe_avx2(
          input, dest, stride, txfm_param->tx_type, txfm_param->tx_size,
#if CONFIG_INTER_DDT
          txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
          txfm_param->eob, txfm_param->bd);
      break;
  }
#endif  // CONFIG_CORE_TX
}

#if CONFIG_E194_FLEX_SECTX
// 256bit intrinsic implementation of ROUND_POWER_OF_TWO_SIGNED.
static INLINE __m256i round_power_of_two_signed_avx2(__m256i v_val_d,
                                                     int bits) {
  const __m256i v_bias_d = _mm256_set1_epi32((1 << bits) >> 1);
  const __m256i v_sign_d = _mm256_srai_epi32(v_val_d, 31);
  const __m256i v_tmp_d =
      _mm256_add_epi32(_mm256_add_epi32(v_val_d, v_bias_d), v_sign_d);
  return _mm256_srai_epi32(v_tmp_d, bits);
}
#endif  // CONFIG_E194_FLEX_SECTX

// Inverse secondary transform
void inv_stxfm_avx2(tran_low_t *src, tran_low_t *dst,
                    const PREDICTION_MODE mode, const uint8_t stx_idx,
                    const int size, const int bd) {
  assert(stx_idx < 4);
#if CONFIG_E124_IST_REDUCE_METHOD4
  const int16_t *kernel = (size == 0) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
#else
  const int16_t *kernel = (size == 4) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
#if !CONFIG_E194_FLEX_SECTX
  const int rnd_factor = 1 << (7 - 1);
  const __m256i round = _mm256_set1_epi32(rnd_factor);
#endif  // !CONFIG_E194_FLEX_SECTX

  int reduced_width, reduced_height;
#if CONFIG_E124_IST_REDUCE_METHOD4
  if (size == 0) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
#if CONFIG_F105_IST_MEM_REDUCE
    reduced_height = (size == 1)
                         ? IST_8x8_HEIGHT_RED
                         : ((size == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
#else
    reduced_height = (size == 1) ? IST_8x8_HEIGHT_RED : IST_8x8_HEIGHT;
#endif  // CONFIG_F105_IST_MEM_REDUCE
    reduced_width = IST_8x8_WIDTH;
  }
#else
  if (size == 4) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = IST_8x8_HEIGHT;
    reduced_width = IST_8x8_WIDTH;
  }
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
  for (int j = 0; j < reduced_height; j++) {
    const int16_t *kernel_tmp = kernel;
    int *srcPtr = src;
    int *out = dst;
    __m256i tmpCoeff = _mm256_set1_epi32(srcPtr[j]);
    __m256i *tmpBlock = (__m256i *)out;
    for (int i = 0; i < reduced_width; i += 8, tmpBlock++) {
      __m256i tmp =
          _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i *)(kernel_tmp + i)));
      __m256i sum = _mm256_loadu_si256(tmpBlock);
      tmp = _mm256_mullo_epi32(tmpCoeff, tmp);
      tmp = _mm256_add_epi32(tmp, sum);
      _mm256_storeu_si256(tmpBlock, tmp);
    }
#if CONFIG_E194_FLEX_SECTX || CONFIG_E124_IST_REDUCE_METHOD4
    kernel += reduced_width;
#else
    kernel += (size * size);
#endif  // CONFIG_E194_FLEX_SECTX || CONFIG_E124_IST_REDUCE_METHOD4
  }
  int *out = dst;
  __m256i *tmpBlock = (__m256i *)out;
  const __m256i max_value = _mm256_set1_epi32((1 << (7 + bd)) - 1);
  const __m256i min_value = _mm256_set1_epi32(-(1 << (7 + bd)));
  for (int j = 0; j < reduced_width; j += 8, tmpBlock++) {
    __m256i tmp = _mm256_loadu_si256(tmpBlock);
#if CONFIG_E194_FLEX_SECTX
    tmp = round_power_of_two_signed_avx2(tmp, 7);
#else
    tmp = _mm256_srai_epi32(_mm256_add_epi32(tmp, round), 7);
#endif  // CONFIG_E194_FLEX_SECTX
    tmp = _mm256_min_epi32(_mm256_max_epi32(tmp, min_value), max_value);
    _mm256_storeu_si256(tmpBlock, tmp);
  }
}

#if CONFIG_CORE_TX
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

#if CONFIG_INTER_DDT
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
#endif  // CONFIG_INTER_DDT

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
#if CONFIG_INTER_DDT
        case 4:
          inv_txfm_ddtx_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size4_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
#endif  // CONFIG_INTER_DDT
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
#if CONFIG_INTER_DDT
        case 4:
          inv_txfm_ddtx_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size8_avx2(src, dst, shift, line, skip_line, zero_line,
                                   coef_min, coef_max);
          break;
#endif  // CONFIG_INTER_DDT
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
#if CONFIG_INTER_DDT
        case 4:
          inv_txfm_ddtx_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size16_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
#endif  // CONFIG_INTER_DDT
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
    case 4:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size64_avx2(src, dst, shift, line, skip_line, zero_line,
                                    coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    default: assert(0); break;
  }
}

void inv_txfm_avx2(const tran_low_t *input, uint16_t *dest, int stride,
                   const TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;
  TX_TYPE tx_type = txfm_param->tx_type;

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  const int intermediate_bitdepth = txfm_param->bd + 8;
  const int rng_min = -(1 << (intermediate_bitdepth - 1));
  const int rng_max = (1 << (intermediate_bitdepth - 1)) - 1;

  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;

  if (txfm_param->lossless) {
#if CONFIG_LOSSLESS_DPCM
    assert(tx_type == DCT_DCT || tx_type == IDTX);
    if (tx_type == IDTX) {
      av1_inv_txfm2d_add_4x4_c(input, dest, stride, tx_type,
#if CONFIG_INTER_DDT
                               txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                               txfm_param->bd);
    } else {
      av1_highbd_iwht4x4_add(input, dest, stride, txfm_param->eob,
                             txfm_param->bd);
    }
#else   // CONFIG_LOSSLESS_DPCM
    assert(tx_type == DCT_DCT);
    av1_highbd_iwht4x4_add(input, dest, stride, eob, bd);
#endif  // CONFIG_LOSSLESS_DPCM
    return;
  }

  int tx_type_row = g_hor_tx_type[tx_type];
  int tx_type_col = g_ver_tx_type[tx_type];

#if CONFIG_INTER_DDT
  if (txfm_param->use_ddt) {
    if (tx_type_row == DST7 || tx_type_row == DCT8) {
      tx_type_row = (tx_type_row == DST7) ? DDTX : FDDT;
    }
    if (tx_type_col == DST7 || tx_type_col == DCT8) {
      tx_type_col = (tx_type_col == DST7) ? DDTX : FDDT;
    }
  }
#endif  // CONFIG_INTER_DDT

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

  inv_transform_1d_avx2(temp, block, shift_2nd, width, 0, skipHeight, rng_min,
                        rng_max, tx_type_col, tx_high_index);

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
#endif  // CONFIG_CORE_TX