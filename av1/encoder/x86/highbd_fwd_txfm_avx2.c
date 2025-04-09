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
#include "av1/encoder/av1_fwd_txfm1d_cfg.h"
#include "aom_dsp/txfm_common.h"
#include "aom_ports/mem.h"
#include "aom_dsp/x86/txfm_common_sse2.h"
#include "aom_dsp/x86/txfm_common_avx2.h"
#if CONFIG_CORE_TX
#include "av1/common/txb_common.h"
#endif  // CONFIG_CORE_TX

static INLINE void load_buffer_8x8_avx2(const int16_t *input, __m256i *out,
                                        int stride, int flipud, int fliplr,
                                        int shift) {
  __m128i out1[8];
  if (!flipud) {
    out1[0] = _mm_loadu_si128((const __m128i *)(input + 0 * stride));
    out1[1] = _mm_loadu_si128((const __m128i *)(input + 1 * stride));
    out1[2] = _mm_loadu_si128((const __m128i *)(input + 2 * stride));
    out1[3] = _mm_loadu_si128((const __m128i *)(input + 3 * stride));
    out1[4] = _mm_loadu_si128((const __m128i *)(input + 4 * stride));
    out1[5] = _mm_loadu_si128((const __m128i *)(input + 5 * stride));
    out1[6] = _mm_loadu_si128((const __m128i *)(input + 6 * stride));
    out1[7] = _mm_loadu_si128((const __m128i *)(input + 7 * stride));

  } else {
    out1[7] = _mm_loadu_si128((const __m128i *)(input + 0 * stride));
    out1[6] = _mm_loadu_si128((const __m128i *)(input + 1 * stride));
    out1[5] = _mm_loadu_si128((const __m128i *)(input + 2 * stride));
    out1[4] = _mm_loadu_si128((const __m128i *)(input + 3 * stride));
    out1[3] = _mm_loadu_si128((const __m128i *)(input + 4 * stride));
    out1[2] = _mm_loadu_si128((const __m128i *)(input + 5 * stride));
    out1[1] = _mm_loadu_si128((const __m128i *)(input + 6 * stride));
    out1[0] = _mm_loadu_si128((const __m128i *)(input + 7 * stride));
  }

  if (!fliplr) {
    out[0] = _mm256_cvtepi16_epi32(out1[0]);
    out[1] = _mm256_cvtepi16_epi32(out1[1]);
    out[2] = _mm256_cvtepi16_epi32(out1[2]);
    out[3] = _mm256_cvtepi16_epi32(out1[3]);
    out[4] = _mm256_cvtepi16_epi32(out1[4]);
    out[5] = _mm256_cvtepi16_epi32(out1[5]);
    out[6] = _mm256_cvtepi16_epi32(out1[6]);
    out[7] = _mm256_cvtepi16_epi32(out1[7]);

  } else {
    out[0] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[0]));
    out[1] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[1]));
    out[2] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[2]));
    out[3] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[3]));
    out[4] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[4]));
    out[5] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[5]));
    out[6] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[6]));
    out[7] = _mm256_cvtepi16_epi32(mm_reverse_epi16(out1[7]));
  }
  out[0] = _mm256_slli_epi32(out[0], shift);
  out[1] = _mm256_slli_epi32(out[1], shift);
  out[2] = _mm256_slli_epi32(out[2], shift);
  out[3] = _mm256_slli_epi32(out[3], shift);
  out[4] = _mm256_slli_epi32(out[4], shift);
  out[5] = _mm256_slli_epi32(out[5], shift);
  out[6] = _mm256_slli_epi32(out[6], shift);
  out[7] = _mm256_slli_epi32(out[7], shift);
}
static INLINE void col_txfm_8x8_rounding(__m256i *in, int shift) {
  const __m256i rounding = _mm256_set1_epi32(1 << (shift - 1));

  in[0] = _mm256_add_epi32(in[0], rounding);
  in[1] = _mm256_add_epi32(in[1], rounding);
  in[2] = _mm256_add_epi32(in[2], rounding);
  in[3] = _mm256_add_epi32(in[3], rounding);
  in[4] = _mm256_add_epi32(in[4], rounding);
  in[5] = _mm256_add_epi32(in[5], rounding);
  in[6] = _mm256_add_epi32(in[6], rounding);
  in[7] = _mm256_add_epi32(in[7], rounding);

  in[0] = _mm256_srai_epi32(in[0], shift);
  in[1] = _mm256_srai_epi32(in[1], shift);
  in[2] = _mm256_srai_epi32(in[2], shift);
  in[3] = _mm256_srai_epi32(in[3], shift);
  in[4] = _mm256_srai_epi32(in[4], shift);
  in[5] = _mm256_srai_epi32(in[5], shift);
  in[6] = _mm256_srai_epi32(in[6], shift);
  in[7] = _mm256_srai_epi32(in[7], shift);
}
static INLINE void load_buffer_8x16_avx2(const int16_t *input, __m256i *out,
                                         int stride, int flipud, int fliplr,
                                         int shift) {
  const int16_t *topL = input;
  const int16_t *botL = input + 8 * stride;

  const int16_t *tmp;

  if (flipud) {
    tmp = topL;
    topL = botL;
    botL = tmp;
  }
  load_buffer_8x8_avx2(topL, out, stride, flipud, fliplr, shift);
  load_buffer_8x8_avx2(botL, out + 8, stride, flipud, fliplr, shift);
}
static INLINE void load_buffer_16xn_avx2(const int16_t *input, __m256i *out,
                                         int stride, int height, int outstride,
                                         int flipud, int fliplr) {
  __m256i out1[64];
  if (!flipud) {
    for (int i = 0; i < height; i++) {
      out1[i] = _mm256_loadu_si256((const __m256i *)(input + i * stride));
    }
  } else {
    for (int i = 0; i < height; i++) {
      out1[(height - 1) - i] =
          _mm256_loadu_si256((const __m256i *)(input + i * stride));
    }
  }
  if (!fliplr) {
    for (int i = 0; i < height; i++) {
      out[i * outstride] =
          _mm256_cvtepi16_epi32(_mm256_castsi256_si128(out1[i]));
      out[i * outstride + 1] =
          _mm256_cvtepi16_epi32(_mm256_extractf128_si256(out1[i], 1));
    }
  } else {
    for (int i = 0; i < height; i++) {
      out[i * outstride + 1] = _mm256_cvtepi16_epi32(
          mm_reverse_epi16(_mm256_castsi256_si128(out1[i])));
      out[i * outstride + 0] = _mm256_cvtepi16_epi32(
          mm_reverse_epi16(_mm256_extractf128_si256(out1[i], 1)));
    }
  }
}

static void fwd_txfm_transpose_8x8_avx2(const __m256i *in, __m256i *out,
                                        const int instride,
                                        const int outstride) {
  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i x0, x1;

  u0 = _mm256_unpacklo_epi32(in[0 * instride], in[1 * instride]);
  u1 = _mm256_unpackhi_epi32(in[0 * instride], in[1 * instride]);

  u2 = _mm256_unpacklo_epi32(in[2 * instride], in[3 * instride]);
  u3 = _mm256_unpackhi_epi32(in[2 * instride], in[3 * instride]);

  u4 = _mm256_unpacklo_epi32(in[4 * instride], in[5 * instride]);
  u5 = _mm256_unpackhi_epi32(in[4 * instride], in[5 * instride]);

  u6 = _mm256_unpacklo_epi32(in[6 * instride], in[7 * instride]);
  u7 = _mm256_unpackhi_epi32(in[6 * instride], in[7 * instride]);

  x0 = _mm256_unpacklo_epi64(u0, u2);
  x1 = _mm256_unpacklo_epi64(u4, u6);
  out[0 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[4 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpackhi_epi64(u0, u2);
  x1 = _mm256_unpackhi_epi64(u4, u6);
  out[1 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[5 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpacklo_epi64(u1, u3);
  x1 = _mm256_unpacklo_epi64(u5, u7);
  out[2 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[6 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x31);

  x0 = _mm256_unpackhi_epi64(u1, u3);
  x1 = _mm256_unpackhi_epi64(u5, u7);
  out[3 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x20);
  out[7 * outstride] = _mm256_permute2f128_si256(x0, x1, 0x31);
}
static INLINE void round_shift_32_8xn_avx2(__m256i *in, int size, int bit,
                                           int stride) {
  if (bit < 0) {
    bit = -bit;
    __m256i round = _mm256_set1_epi32(1 << (bit - 1));
    for (int i = 0; i < size; ++i) {
      in[stride * i] = _mm256_add_epi32(in[stride * i], round);
      in[stride * i] = _mm256_srai_epi32(in[stride * i], bit);
    }
  } else if (bit > 0) {
    for (int i = 0; i < size; ++i) {
      in[stride * i] = _mm256_slli_epi32(in[stride * i], bit);
    }
  }
}
static INLINE void store_buffer_avx2(const __m256i *const in, int32_t *out,
                                     const int stride, const int out_size) {
  for (int i = 0; i < out_size; ++i) {
    _mm256_store_si256((__m256i *)(out), in[i]);
    out += stride;
  }
}
static INLINE void fwd_txfm_transpose_16x16_avx2(const __m256i *in,
                                                 __m256i *out) {
  fwd_txfm_transpose_8x8_avx2(&in[0], &out[0], 2, 2);
  fwd_txfm_transpose_8x8_avx2(&in[1], &out[16], 2, 2);
  fwd_txfm_transpose_8x8_avx2(&in[16], &out[1], 2, 2);
  fwd_txfm_transpose_8x8_avx2(&in[17], &out[17], 2, 2);
}

#if !USE_TUNED_ADST16
static INLINE __m256i av1_half_btf_avx2(const __m256i *w0, const __m256i *n0,
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
#endif  // !USE_TUNED_ADST16

#define TRANSPOSE_4X4_AVX2(x0, x1, x2, x3, y0, y1, y2, y3) \
  do {                                                     \
    __m256i u0, u1, u2, u3;                                \
    u0 = _mm256_unpacklo_epi32(x0, x1);                    \
    u1 = _mm256_unpackhi_epi32(x0, x1);                    \
    u2 = _mm256_unpacklo_epi32(x2, x3);                    \
    u3 = _mm256_unpackhi_epi32(x2, x3);                    \
    y0 = _mm256_unpacklo_epi64(u0, u2);                    \
    y1 = _mm256_unpackhi_epi64(u0, u2);                    \
    y2 = _mm256_unpacklo_epi64(u1, u3);                    \
    y3 = _mm256_unpackhi_epi64(u1, u3);                    \
  } while (0)

#define btf_32_avx2_type0_new(w0, w1, in0, in1, out0, out1, bit, round) \
  do {                                                                  \
    const __m256i ww0 = _mm256_set1_epi32(w0);                          \
    const __m256i ww1 = _mm256_set1_epi32(w1);                          \
    const __m256i in0_w0 = _mm256_mullo_epi32(in0, ww0);                \
    const __m256i in1_w1 = _mm256_mullo_epi32(in1, ww1);                \
    out0 = _mm256_add_epi32(in0_w0, in1_w1);                            \
    out0 = _mm256_srai_epi32(_mm256_add_epi32(out0, round), bit);       \
    const __m256i in0_w1 = _mm256_mullo_epi32(in0, ww1);                \
    const __m256i in1_w0 = _mm256_mullo_epi32(in1, ww0);                \
    out1 = _mm256_sub_epi32(in0_w1, in1_w0);                            \
    out1 = _mm256_srai_epi32(_mm256_add_epi32(out1, round), bit);       \
  } while (0)

#define btf_32_type0_avx2_new(ww0, ww1, in0, in1, out0, out1, r, bit) \
  do {                                                                \
    const __m256i in0_w0 = _mm256_mullo_epi32(in0, ww0);              \
    const __m256i in1_w1 = _mm256_mullo_epi32(in1, ww1);              \
    out0 = _mm256_add_epi32(in0_w0, in1_w1);                          \
    out0 = _mm256_add_epi32(out0, r);                                 \
    out0 = _mm256_srai_epi32(out0, bit);                              \
    const __m256i in0_w1 = _mm256_mullo_epi32(in0, ww1);              \
    const __m256i in1_w0 = _mm256_mullo_epi32(in1, ww0);              \
    out1 = _mm256_sub_epi32(in0_w1, in1_w0);                          \
    out1 = _mm256_add_epi32(out1, r);                                 \
    out1 = _mm256_srai_epi32(out1, bit);                              \
  } while (0)

typedef void (*transform_1d_avx2)(__m256i *in, __m256i *out,
                                  const int8_t cos_bit, int instride,
                                  int outstride);
static void fdct8_avx2(__m256i *in, __m256i *out, const int8_t bit,
                       const int col_num, const int outstride) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  __m256i u[8], v[8];
  for (int col = 0; col < col_num; ++col) {
    u[0] = _mm256_add_epi32(in[0 * col_num + col], in[7 * col_num + col]);
    v[7] = _mm256_sub_epi32(in[0 * col_num + col], in[7 * col_num + col]);
    u[1] = _mm256_add_epi32(in[1 * col_num + col], in[6 * col_num + col]);
    u[6] = _mm256_sub_epi32(in[1 * col_num + col], in[6 * col_num + col]);
    u[2] = _mm256_add_epi32(in[2 * col_num + col], in[5 * col_num + col]);
    u[5] = _mm256_sub_epi32(in[2 * col_num + col], in[5 * col_num + col]);
    u[3] = _mm256_add_epi32(in[3 * col_num + col], in[4 * col_num + col]);
    v[4] = _mm256_sub_epi32(in[3 * col_num + col], in[4 * col_num + col]);
    v[0] = _mm256_add_epi32(u[0], u[3]);
    v[3] = _mm256_sub_epi32(u[0], u[3]);
    v[1] = _mm256_add_epi32(u[1], u[2]);
    v[2] = _mm256_sub_epi32(u[1], u[2]);

    v[5] = _mm256_mullo_epi32(u[5], cospim32);
    v[6] = _mm256_mullo_epi32(u[6], cospi32);
    v[5] = _mm256_add_epi32(v[5], v[6]);
    v[5] = _mm256_add_epi32(v[5], rnding);
    v[5] = _mm256_srai_epi32(v[5], bit);

    u[0] = _mm256_mullo_epi32(u[5], cospi32);
    v[6] = _mm256_mullo_epi32(u[6], cospim32);
    v[6] = _mm256_sub_epi32(u[0], v[6]);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    // stage 3
    // type 0
    v[0] = _mm256_mullo_epi32(v[0], cospi32);
    v[1] = _mm256_mullo_epi32(v[1], cospi32);
    u[0] = _mm256_add_epi32(v[0], v[1]);
    u[0] = _mm256_add_epi32(u[0], rnding);
    u[0] = _mm256_srai_epi32(u[0], bit);

    u[1] = _mm256_sub_epi32(v[0], v[1]);
    u[1] = _mm256_add_epi32(u[1], rnding);
    u[1] = _mm256_srai_epi32(u[1], bit);

    // type 1
    v[0] = _mm256_mullo_epi32(v[2], cospi48);
    v[1] = _mm256_mullo_epi32(v[3], cospi16);
    u[2] = _mm256_add_epi32(v[0], v[1]);
    u[2] = _mm256_add_epi32(u[2], rnding);
    u[2] = _mm256_srai_epi32(u[2], bit);

    v[0] = _mm256_mullo_epi32(v[2], cospi16);
    v[1] = _mm256_mullo_epi32(v[3], cospi48);
    u[3] = _mm256_sub_epi32(v[1], v[0]);
    u[3] = _mm256_add_epi32(u[3], rnding);
    u[3] = _mm256_srai_epi32(u[3], bit);

    u[4] = _mm256_add_epi32(v[4], v[5]);
    u[5] = _mm256_sub_epi32(v[4], v[5]);
    u[6] = _mm256_sub_epi32(v[7], v[6]);
    u[7] = _mm256_add_epi32(v[7], v[6]);

    // stage 4
    // stage 5
    v[0] = _mm256_mullo_epi32(u[4], cospi56);
    v[1] = _mm256_mullo_epi32(u[7], cospi8);
    v[0] = _mm256_add_epi32(v[0], v[1]);
    v[0] = _mm256_add_epi32(v[0], rnding);
    out[1 * outstride + col] = _mm256_srai_epi32(v[0], bit);  // buf0[4]

    v[0] = _mm256_mullo_epi32(u[4], cospi8);
    v[1] = _mm256_mullo_epi32(u[7], cospi56);
    v[0] = _mm256_sub_epi32(v[1], v[0]);
    v[0] = _mm256_add_epi32(v[0], rnding);
    out[7 * outstride + col] = _mm256_srai_epi32(v[0], bit);  // buf0[7]

    v[0] = _mm256_mullo_epi32(u[5], cospi24);
    v[1] = _mm256_mullo_epi32(u[6], cospi40);
    v[0] = _mm256_add_epi32(v[0], v[1]);
    v[0] = _mm256_add_epi32(v[0], rnding);
    out[5 * outstride + col] = _mm256_srai_epi32(v[0], bit);  // buf0[5]

    v[0] = _mm256_mullo_epi32(u[5], cospi40);
    v[1] = _mm256_mullo_epi32(u[6], cospi24);
    v[0] = _mm256_sub_epi32(v[1], v[0]);
    v[0] = _mm256_add_epi32(v[0], rnding);
    out[3 * outstride + col] = _mm256_srai_epi32(v[0], bit);  // buf0[6]

    out[0 * outstride + col] = u[0];  // buf0[0]
    out[4 * outstride + col] = u[1];  // buf0[1]
    out[2 * outstride + col] = u[2];  // buf0[2]
    out[6 * outstride + col] = u[3];  // buf0[3]
  }
}

static void fddt8_avx2(__m256i *in, __m256i *out, const int8_t bit,
                       const int col_num, const int outstride) {
  (void)bit;
  const int32_t *kernel = ddt8_kernel[FWD_TXFM];
  const int size = 8;
  const __m256i zero = _mm256_setzero_si256();
  const __m256i rnding = _mm256_set1_epi32(1 << (FWD_ADST_BIT - 1));
  __m256i x[8];
  int col;
  for (col = 0; col < col_num; ++col) {
    for (int i = 0; i < 8; ++i) {
      int row_idx = i * size;
      __m256i sum = zero;
      __m256i t;
      for (int j = 0; j < 8; ++j) {
        const __m256i coef = _mm256_set1_epi32(kernel[row_idx + j]);
        t = _mm256_mullo_epi32(in[j * col_num + col], coef);
        sum = _mm256_add_epi32(sum, t);
      }
      sum = _mm256_add_epi32(sum, rnding);
      x[i] = _mm256_srai_epi32(sum, FWD_ADST_BIT);
    }
    for (int i = 0; i < 8; ++i) out[i * outstride + col] = x[i];
  }
}

#if USE_TUNED_ADST8
static void fadst8_avx2(__m256i *in, __m256i *out, const int8_t bit,
                        const int col_num, const int outstride) {
  (void)bit;
  const int32_t *kernel = av2_adst_kernel8[FWD_TXFM];
  const int size = tx_size_wide[TX_8X8];
  const __m256i zero = _mm256_setzero_si256();
  const __m256i rnding = _mm256_set1_epi32(1 << (FWD_ADST_BIT - 1));
  __m256i x[8];
  int col;
  for (col = 0; col < col_num; ++col) {
    for (int i = 0; i < 8; ++i) {
      int row_idx = i * size;
      __m256i sum = zero;
      __m256i t;
      for (int j = 0; j < 8; ++j) {
        const __m256i coef = _mm256_set1_epi32(kernel[row_idx + j]);
        t = _mm256_mullo_epi32(in[j * col_num + col], coef);
        sum = _mm256_add_epi32(sum, t);
      }
      sum = _mm256_add_epi32(sum, rnding);
      x[i] = _mm256_srai_epi32(sum, FWD_ADST_BIT);
    }
    for (int i = 0; i < 8; ++i) out[i * outstride + col] = x[i];
  }
}
#else
static void fadst8_avx2(__m256i *in, __m256i *out, const int8_t bit,
                        const int col_num, const int outstirde) {
  (void)col_num;
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospim4 = _mm256_set1_epi32(-cospi[4]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospim20 = _mm256_set1_epi32(-cospi[20]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospim36 = _mm256_set1_epi32(-cospi[36]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i cospim52 = _mm256_set1_epi32(-cospi[52]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const __m256i zero = _mm256_setzero_si256();
  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i v0, v1, v2, v3, v4, v5, v6, v7;
  __m256i x, y;
  for (int col = 0; col < col_num; ++col) {
    u0 = in[0 * col_num + col];
    u1 = _mm256_sub_epi32(zero, in[7 * col_num + col]);
    u2 = _mm256_sub_epi32(zero, in[3 * col_num + col]);
    u3 = in[4 * col_num + col];
    u4 = _mm256_sub_epi32(zero, in[1 * col_num + col]);
    u5 = in[6 * col_num + col];
    u6 = in[2 * col_num + col];
    u7 = _mm256_sub_epi32(zero, in[5 * col_num + col]);

    // stage 2
    v0 = u0;
    v1 = u1;

    x = _mm256_mullo_epi32(u2, cospi32);
    y = _mm256_mullo_epi32(u3, cospi32);
    v2 = _mm256_add_epi32(x, y);
    v2 = _mm256_add_epi32(v2, rnding);
    v2 = _mm256_srai_epi32(v2, bit);

    v3 = _mm256_sub_epi32(x, y);
    v3 = _mm256_add_epi32(v3, rnding);
    v3 = _mm256_srai_epi32(v3, bit);

    v4 = u4;
    v5 = u5;

    x = _mm256_mullo_epi32(u6, cospi32);
    y = _mm256_mullo_epi32(u7, cospi32);
    v6 = _mm256_add_epi32(x, y);
    v6 = _mm256_add_epi32(v6, rnding);
    v6 = _mm256_srai_epi32(v6, bit);

    v7 = _mm256_sub_epi32(x, y);
    v7 = _mm256_add_epi32(v7, rnding);
    v7 = _mm256_srai_epi32(v7, bit);

    // stage 3
    u0 = _mm256_add_epi32(v0, v2);
    u1 = _mm256_add_epi32(v1, v3);
    u2 = _mm256_sub_epi32(v0, v2);
    u3 = _mm256_sub_epi32(v1, v3);
    u4 = _mm256_add_epi32(v4, v6);
    u5 = _mm256_add_epi32(v5, v7);
    u6 = _mm256_sub_epi32(v4, v6);
    u7 = _mm256_sub_epi32(v5, v7);

    // stage 4
    v0 = u0;
    v1 = u1;
    v2 = u2;
    v3 = u3;

    x = _mm256_mullo_epi32(u4, cospi16);
    y = _mm256_mullo_epi32(u5, cospi48);
    v4 = _mm256_add_epi32(x, y);
    v4 = _mm256_add_epi32(v4, rnding);
    v4 = _mm256_srai_epi32(v4, bit);

    x = _mm256_mullo_epi32(u4, cospi48);
    y = _mm256_mullo_epi32(u5, cospim16);
    v5 = _mm256_add_epi32(x, y);
    v5 = _mm256_add_epi32(v5, rnding);
    v5 = _mm256_srai_epi32(v5, bit);

    x = _mm256_mullo_epi32(u6, cospim48);
    y = _mm256_mullo_epi32(u7, cospi16);
    v6 = _mm256_add_epi32(x, y);
    v6 = _mm256_add_epi32(v6, rnding);
    v6 = _mm256_srai_epi32(v6, bit);

    x = _mm256_mullo_epi32(u6, cospi16);
    y = _mm256_mullo_epi32(u7, cospi48);
    v7 = _mm256_add_epi32(x, y);
    v7 = _mm256_add_epi32(v7, rnding);
    v7 = _mm256_srai_epi32(v7, bit);

    // stage 5
    u0 = _mm256_add_epi32(v0, v4);
    u1 = _mm256_add_epi32(v1, v5);
    u2 = _mm256_add_epi32(v2, v6);
    u3 = _mm256_add_epi32(v3, v7);
    u4 = _mm256_sub_epi32(v0, v4);
    u5 = _mm256_sub_epi32(v1, v5);
    u6 = _mm256_sub_epi32(v2, v6);
    u7 = _mm256_sub_epi32(v3, v7);

    // stage 6
    x = _mm256_mullo_epi32(u0, cospi4);
    y = _mm256_mullo_epi32(u1, cospi60);
    v0 = _mm256_add_epi32(x, y);
    v0 = _mm256_add_epi32(v0, rnding);
    v0 = _mm256_srai_epi32(v0, bit);

    x = _mm256_mullo_epi32(u0, cospi60);
    y = _mm256_mullo_epi32(u1, cospim4);
    v1 = _mm256_add_epi32(x, y);
    v1 = _mm256_add_epi32(v1, rnding);
    v1 = _mm256_srai_epi32(v1, bit);

    x = _mm256_mullo_epi32(u2, cospi20);
    y = _mm256_mullo_epi32(u3, cospi44);
    v2 = _mm256_add_epi32(x, y);
    v2 = _mm256_add_epi32(v2, rnding);
    v2 = _mm256_srai_epi32(v2, bit);

    x = _mm256_mullo_epi32(u2, cospi44);
    y = _mm256_mullo_epi32(u3, cospim20);
    v3 = _mm256_add_epi32(x, y);
    v3 = _mm256_add_epi32(v3, rnding);
    v3 = _mm256_srai_epi32(v3, bit);

    x = _mm256_mullo_epi32(u4, cospi36);
    y = _mm256_mullo_epi32(u5, cospi28);
    v4 = _mm256_add_epi32(x, y);
    v4 = _mm256_add_epi32(v4, rnding);
    v4 = _mm256_srai_epi32(v4, bit);

    x = _mm256_mullo_epi32(u4, cospi28);
    y = _mm256_mullo_epi32(u5, cospim36);
    v5 = _mm256_add_epi32(x, y);
    v5 = _mm256_add_epi32(v5, rnding);
    v5 = _mm256_srai_epi32(v5, bit);

    x = _mm256_mullo_epi32(u6, cospi52);
    y = _mm256_mullo_epi32(u7, cospi12);
    v6 = _mm256_add_epi32(x, y);
    v6 = _mm256_add_epi32(v6, rnding);
    v6 = _mm256_srai_epi32(v6, bit);

    x = _mm256_mullo_epi32(u6, cospi12);
    y = _mm256_mullo_epi32(u7, cospim52);
    v7 = _mm256_add_epi32(x, y);
    v7 = _mm256_add_epi32(v7, rnding);
    v7 = _mm256_srai_epi32(v7, bit);

    // stage 7
    out[0 * outstirde + col] = v1;
    out[1 * outstirde + col] = v6;
    out[2 * outstirde + col] = v3;
    out[3 * outstirde + col] = v4;
    out[4 * outstirde + col] = v5;
    out[5 * outstirde + col] = v2;
    out[6 * outstirde + col] = v7;
    out[7 * outstirde + col] = v0;
  }
}
#endif  // USE_TUNED_ADST8
static void idtx8_avx2(__m256i *in, __m256i *out, const int8_t bit, int col_num,
                       int outstride) {
  (void)bit;
  (void)outstride;
  int num_iters = 8 * col_num;
  for (int i = 0; i < num_iters; i += 8) {
    out[i] = _mm256_add_epi32(in[i], in[i]);
    out[i + 1] = _mm256_add_epi32(in[i + 1], in[i + 1]);
    out[i + 2] = _mm256_add_epi32(in[i + 2], in[i + 2]);
    out[i + 3] = _mm256_add_epi32(in[i + 3], in[i + 3]);
    out[i + 4] = _mm256_add_epi32(in[i + 4], in[i + 4]);
    out[i + 5] = _mm256_add_epi32(in[i + 5], in[i + 5]);
    out[i + 6] = _mm256_add_epi32(in[i + 6], in[i + 6]);
    out[i + 7] = _mm256_add_epi32(in[i + 7], in[i + 7]);
  }
}
void av1_fwd_txfm2d_8x8_avx2(const int16_t *input, int32_t *coeff, int stride,
                             TX_TYPE tx_type, int use_ddt, int bd) {
  __m256i in[8], out[8];
  const TX_SIZE tx_size = TX_8X8;
  const int8_t *shift = av1_fwd_txfm_shift_ls[tx_size];
  const int txw_idx = get_txw_idx(tx_size);
  const int txh_idx = get_txh_idx(tx_size);
  const int width = tx_size_wide[tx_size];
  const int width_div8 = (width >> 3);

  switch (tx_type) {
    case DCT_DCT:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      fdct8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      fdct8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                 width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case ADST_DCT:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      fdct8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                 width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case DCT_ADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      fdct8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case ADST_ADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case FLIPADST_DCT:
      load_buffer_8x8_avx2(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      fdct8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                 width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case DCT_FLIPADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 1, shift[0]);
      fdct8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case FLIPADST_FLIPADST:
      load_buffer_8x8_avx2(input, in, stride, 1, 1, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case ADST_FLIPADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 1, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case FLIPADST_ADST:
      load_buffer_8x8_avx2(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case IDTX:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      idtx8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      idtx8_avx2(out, in, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case V_DCT:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      fdct8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      idtx8_avx2(out, in, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case H_DCT:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      idtx8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      fdct8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case V_ADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      idtx8_avx2(out, in, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case H_ADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 0, shift[0]);
      idtx8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case V_FLIPADST:
      load_buffer_8x8_avx2(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      idtx8_avx2(out, in, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    case H_FLIPADST:
      load_buffer_8x8_avx2(input, in, stride, 0, 1, shift[0]);
      idtx8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                 width_div8);
      col_txfm_8x8_rounding(out, -shift[1]);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      if (use_ddt && REPLACE_ADST8)
        fddt8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                   width_div8);
      else
        fadst8_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      fwd_txfm_transpose_8x8_avx2(out, in, width_div8, width_div8);
      store_buffer_avx2(in, coeff, 8, 8);
      break;
    default: assert(0);
  }
  (void)bd;
}

static void fdct16_avx2(__m256i *in, __m256i *out, const int8_t bit,
                        const int col_num, const int outstride) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospim32 = _mm256_set1_epi32(-cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi60 = _mm256_set1_epi32(cospi[60]);
  const __m256i cospi4 = _mm256_set1_epi32(cospi[4]);
  const __m256i cospi28 = _mm256_set1_epi32(cospi[28]);
  const __m256i cospi36 = _mm256_set1_epi32(cospi[36]);
  const __m256i cospi44 = _mm256_set1_epi32(cospi[44]);
  const __m256i cospi20 = _mm256_set1_epi32(cospi[20]);
  const __m256i cospi12 = _mm256_set1_epi32(cospi[12]);
  const __m256i cospi52 = _mm256_set1_epi32(cospi[52]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  __m256i u[16], v[16], x;
  int col;

  // Calculate the column 0, 1, 2, 3
  for (col = 0; col < col_num; ++col) {
    // stage 0
    // stage 1
    u[0] = _mm256_add_epi32(in[0 * col_num + col], in[15 * col_num + col]);
    u[15] = _mm256_sub_epi32(in[0 * col_num + col], in[15 * col_num + col]);
    u[1] = _mm256_add_epi32(in[1 * col_num + col], in[14 * col_num + col]);
    u[14] = _mm256_sub_epi32(in[1 * col_num + col], in[14 * col_num + col]);
    u[2] = _mm256_add_epi32(in[2 * col_num + col], in[13 * col_num + col]);
    u[13] = _mm256_sub_epi32(in[2 * col_num + col], in[13 * col_num + col]);
    u[3] = _mm256_add_epi32(in[3 * col_num + col], in[12 * col_num + col]);
    u[12] = _mm256_sub_epi32(in[3 * col_num + col], in[12 * col_num + col]);
    u[4] = _mm256_add_epi32(in[4 * col_num + col], in[11 * col_num + col]);
    u[11] = _mm256_sub_epi32(in[4 * col_num + col], in[11 * col_num + col]);
    u[5] = _mm256_add_epi32(in[5 * col_num + col], in[10 * col_num + col]);
    u[10] = _mm256_sub_epi32(in[5 * col_num + col], in[10 * col_num + col]);
    u[6] = _mm256_add_epi32(in[6 * col_num + col], in[9 * col_num + col]);
    u[9] = _mm256_sub_epi32(in[6 * col_num + col], in[9 * col_num + col]);
    u[7] = _mm256_add_epi32(in[7 * col_num + col], in[8 * col_num + col]);
    u[8] = _mm256_sub_epi32(in[7 * col_num + col], in[8 * col_num + col]);

    // stage 2
    v[0] = _mm256_add_epi32(u[0], u[7]);
    v[7] = _mm256_sub_epi32(u[0], u[7]);
    v[1] = _mm256_add_epi32(u[1], u[6]);
    v[6] = _mm256_sub_epi32(u[1], u[6]);
    v[2] = _mm256_add_epi32(u[2], u[5]);
    v[5] = _mm256_sub_epi32(u[2], u[5]);
    v[3] = _mm256_add_epi32(u[3], u[4]);
    v[4] = _mm256_sub_epi32(u[3], u[4]);

    v[10] = _mm256_mullo_epi32(u[10], cospim32);
    x = _mm256_mullo_epi32(u[13], cospi32);
    v[10] = _mm256_add_epi32(v[10], x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[13] = _mm256_mullo_epi32(u[10], cospi32);
    x = _mm256_mullo_epi32(u[13], cospim32);
    v[13] = _mm256_sub_epi32(v[13], x);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    v[11] = _mm256_mullo_epi32(u[11], cospim32);
    x = _mm256_mullo_epi32(u[12], cospi32);
    v[11] = _mm256_add_epi32(v[11], x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = _mm256_mullo_epi32(u[11], cospi32);
    x = _mm256_mullo_epi32(u[12], cospim32);
    v[12] = _mm256_sub_epi32(v[12], x);
    v[12] = _mm256_add_epi32(v[12], rnding);
    v[12] = _mm256_srai_epi32(v[12], bit);

    // stage 3
    u[0] = _mm256_add_epi32(v[0], v[3]);
    u[3] = _mm256_sub_epi32(v[0], v[3]);
    u[1] = _mm256_add_epi32(v[1], v[2]);
    u[2] = _mm256_sub_epi32(v[1], v[2]);

    u[5] = _mm256_mullo_epi32(v[5], cospim32);
    x = _mm256_mullo_epi32(v[6], cospi32);
    u[5] = _mm256_add_epi32(u[5], x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    u[5] = _mm256_srai_epi32(u[5], bit);

    u[6] = _mm256_mullo_epi32(v[5], cospi32);
    x = _mm256_mullo_epi32(v[6], cospim32);
    u[6] = _mm256_sub_epi32(u[6], x);
    u[6] = _mm256_add_epi32(u[6], rnding);
    u[6] = _mm256_srai_epi32(u[6], bit);

    u[11] = _mm256_sub_epi32(u[8], v[11]);
    u[8] = _mm256_add_epi32(u[8], v[11]);
    u[10] = _mm256_sub_epi32(u[9], v[10]);
    u[9] = _mm256_add_epi32(u[9], v[10]);
    u[12] = _mm256_sub_epi32(u[15], v[12]);
    u[15] = _mm256_add_epi32(u[15], v[12]);
    u[13] = _mm256_sub_epi32(u[14], v[13]);
    u[14] = _mm256_add_epi32(u[14], v[13]);

    // stage 4
    u[0] = _mm256_mullo_epi32(u[0], cospi32);
    u[1] = _mm256_mullo_epi32(u[1], cospi32);
    v[0] = _mm256_add_epi32(u[0], u[1]);
    v[0] = _mm256_add_epi32(v[0], rnding);
    out[0 * outstride + col] = _mm256_srai_epi32(v[0], bit);

    v[1] = _mm256_sub_epi32(u[0], u[1]);
    v[1] = _mm256_add_epi32(v[1], rnding);
    out[8 * outstride + col] = _mm256_srai_epi32(v[1], bit);

    v[2] = _mm256_mullo_epi32(u[2], cospi48);
    x = _mm256_mullo_epi32(u[3], cospi16);
    v[2] = _mm256_add_epi32(v[2], x);
    v[2] = _mm256_add_epi32(v[2], rnding);
    out[4 * outstride + col] = _mm256_srai_epi32(v[2], bit);

    v[3] = _mm256_mullo_epi32(u[2], cospi16);
    x = _mm256_mullo_epi32(u[3], cospi48);
    v[3] = _mm256_sub_epi32(x, v[3]);
    v[3] = _mm256_add_epi32(v[3], rnding);
    out[12 * outstride + col] = _mm256_srai_epi32(v[3], bit);

    v[5] = _mm256_sub_epi32(v[4], u[5]);
    v[4] = _mm256_add_epi32(v[4], u[5]);
    v[6] = _mm256_sub_epi32(v[7], u[6]);
    v[7] = _mm256_add_epi32(v[7], u[6]);

    v[9] = _mm256_mullo_epi32(u[9], cospim16);
    x = _mm256_mullo_epi32(u[14], cospi48);
    v[9] = _mm256_add_epi32(v[9], x);
    v[9] = _mm256_add_epi32(v[9], rnding);
    v[9] = _mm256_srai_epi32(v[9], bit);

    v[14] = _mm256_mullo_epi32(u[9], cospi48);
    x = _mm256_mullo_epi32(u[14], cospim16);
    v[14] = _mm256_sub_epi32(v[14], x);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[10] = _mm256_mullo_epi32(u[10], cospim48);
    x = _mm256_mullo_epi32(u[13], cospim16);
    v[10] = _mm256_add_epi32(v[10], x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[13] = _mm256_mullo_epi32(u[10], cospim16);
    x = _mm256_mullo_epi32(u[13], cospim48);
    v[13] = _mm256_sub_epi32(v[13], x);
    v[13] = _mm256_add_epi32(v[13], rnding);
    v[13] = _mm256_srai_epi32(v[13], bit);

    // stage 5
    u[4] = _mm256_mullo_epi32(v[4], cospi56);
    x = _mm256_mullo_epi32(v[7], cospi8);
    u[4] = _mm256_add_epi32(u[4], x);
    u[4] = _mm256_add_epi32(u[4], rnding);
    out[2 * outstride + col] = _mm256_srai_epi32(u[4], bit);

    u[7] = _mm256_mullo_epi32(v[4], cospi8);
    x = _mm256_mullo_epi32(v[7], cospi56);
    u[7] = _mm256_sub_epi32(x, u[7]);
    u[7] = _mm256_add_epi32(u[7], rnding);
    out[14 * outstride + col] = _mm256_srai_epi32(u[7], bit);

    u[5] = _mm256_mullo_epi32(v[5], cospi24);
    x = _mm256_mullo_epi32(v[6], cospi40);
    u[5] = _mm256_add_epi32(u[5], x);
    u[5] = _mm256_add_epi32(u[5], rnding);
    out[10 * outstride + col] = _mm256_srai_epi32(u[5], bit);

    u[6] = _mm256_mullo_epi32(v[5], cospi40);
    x = _mm256_mullo_epi32(v[6], cospi24);
    u[6] = _mm256_sub_epi32(x, u[6]);
    u[6] = _mm256_add_epi32(u[6], rnding);
    out[6 * outstride + col] = _mm256_srai_epi32(u[6], bit);

    u[9] = _mm256_sub_epi32(u[8], v[9]);
    u[8] = _mm256_add_epi32(u[8], v[9]);
    u[10] = _mm256_sub_epi32(u[11], v[10]);
    u[11] = _mm256_add_epi32(u[11], v[10]);

    u[13] = _mm256_sub_epi32(u[12], v[13]);
    u[12] = _mm256_add_epi32(u[12], v[13]);
    u[14] = _mm256_sub_epi32(u[15], v[14]);
    u[15] = _mm256_add_epi32(u[15], v[14]);

    // stage 6
    v[8] = _mm256_mullo_epi32(u[8], cospi60);
    x = _mm256_mullo_epi32(u[15], cospi4);
    v[8] = _mm256_add_epi32(v[8], x);
    v[8] = _mm256_add_epi32(v[8], rnding);
    out[1 * outstride + col] = _mm256_srai_epi32(v[8], bit);

    v[15] = _mm256_mullo_epi32(u[8], cospi4);
    x = _mm256_mullo_epi32(u[15], cospi60);
    v[15] = _mm256_sub_epi32(x, v[15]);
    v[15] = _mm256_add_epi32(v[15], rnding);
    out[15 * outstride + col] = _mm256_srai_epi32(v[15], bit);

    v[9] = _mm256_mullo_epi32(u[9], cospi28);
    x = _mm256_mullo_epi32(u[14], cospi36);
    v[9] = _mm256_add_epi32(v[9], x);
    v[9] = _mm256_add_epi32(v[9], rnding);
    out[9 * outstride + col] = _mm256_srai_epi32(v[9], bit);

    v[14] = _mm256_mullo_epi32(u[9], cospi36);
    x = _mm256_mullo_epi32(u[14], cospi28);
    v[14] = _mm256_sub_epi32(x, v[14]);
    v[14] = _mm256_add_epi32(v[14], rnding);
    out[7 * outstride + col] = _mm256_srai_epi32(v[14], bit);

    v[10] = _mm256_mullo_epi32(u[10], cospi44);
    x = _mm256_mullo_epi32(u[13], cospi20);
    v[10] = _mm256_add_epi32(v[10], x);
    v[10] = _mm256_add_epi32(v[10], rnding);
    out[5 * outstride + col] = _mm256_srai_epi32(v[10], bit);

    v[13] = _mm256_mullo_epi32(u[10], cospi20);
    x = _mm256_mullo_epi32(u[13], cospi44);
    v[13] = _mm256_sub_epi32(x, v[13]);
    v[13] = _mm256_add_epi32(v[13], rnding);
    out[11 * outstride + col] = _mm256_srai_epi32(v[13], bit);

    v[11] = _mm256_mullo_epi32(u[11], cospi12);
    x = _mm256_mullo_epi32(u[12], cospi52);
    v[11] = _mm256_add_epi32(v[11], x);
    v[11] = _mm256_add_epi32(v[11], rnding);
    out[13 * outstride + col] = _mm256_srai_epi32(v[11], bit);

    v[12] = _mm256_mullo_epi32(u[11], cospi52);
    x = _mm256_mullo_epi32(u[12], cospi12);
    v[12] = _mm256_sub_epi32(x, v[12]);
    v[12] = _mm256_add_epi32(v[12], rnding);
    out[3 * outstride + col] = _mm256_srai_epi32(v[12], bit);
  }
}

static void fddt16_avx2(__m256i *in, __m256i *out, const int8_t bit,
                        const int num_cols, const int outstride) {
  (void)bit;
  const int32_t *kernel = ddt16_kernel[FWD_TXFM];
  const int size = 16;
  const __m256i zero = _mm256_setzero_si256();
  const __m256i rnding = _mm256_set1_epi32(1 << (FWD_ADST_BIT - 1));
  __m256i x[16];
  int col;
  for (col = 0; col < num_cols; ++col) {
    for (int i = 0; i < 16; ++i) {
      int row_idx = i * size;
      __m256i sum = zero;
      __m256i t;
      for (int j = 0; j < 16; ++j) {
        const __m256i coef = _mm256_set1_epi32(kernel[row_idx + j]);
        t = _mm256_mullo_epi32(in[j * num_cols + col], coef);
        sum = _mm256_add_epi32(sum, t);
      }
      sum = _mm256_add_epi32(sum, rnding);
      x[i] = _mm256_srai_epi32(sum, FWD_ADST_BIT);
    }
    for (int i = 0; i < 16; ++i) out[i * outstride + col] = x[i];
  }
}

#if USE_TUNED_ADST16
static void fadst16_avx2(__m256i *in, __m256i *out, const int8_t bit,
                         const int num_cols, const int outstride) {
  (void)bit;
  const int32_t *kernel = av2_adst_kernel16[FWD_TXFM];
  const int size = tx_size_wide[TX_16X16];
  const __m256i zero = _mm256_setzero_si256();
  const __m256i rnding = _mm256_set1_epi32(1 << (FWD_ADST_BIT - 1));
  __m256i x[16];
  int col;
  for (col = 0; col < num_cols; ++col) {
    for (int i = 0; i < 16; ++i) {
      int row_idx = i * size;
      __m256i sum = zero;
      __m256i t;
      for (int j = 0; j < 16; ++j) {
        const __m256i coef = _mm256_set1_epi32(kernel[row_idx + j]);
        t = _mm256_mullo_epi32(in[j * num_cols + col], coef);
        sum = _mm256_add_epi32(sum, t);
      }
      sum = _mm256_add_epi32(sum, rnding);
      x[i] = _mm256_srai_epi32(sum, FWD_ADST_BIT);
    }
    for (int i = 0; i < 16; ++i) out[i * outstride + col] = x[i];
  }
}
#else
static void fadst16_avx2(__m256i *in, __m256i *out, const int8_t bit,
                         const int num_cols, const int outstride) {
  const int32_t *cospi = cospi_arr(bit);
  const __m256i cospi32 = _mm256_set1_epi32(cospi[32]);
  const __m256i cospi48 = _mm256_set1_epi32(cospi[48]);
  const __m256i cospi16 = _mm256_set1_epi32(cospi[16]);
  const __m256i cospim16 = _mm256_set1_epi32(-cospi[16]);
  const __m256i cospim48 = _mm256_set1_epi32(-cospi[48]);
  const __m256i cospi8 = _mm256_set1_epi32(cospi[8]);
  const __m256i cospi56 = _mm256_set1_epi32(cospi[56]);
  const __m256i cospim56 = _mm256_set1_epi32(-cospi[56]);
  const __m256i cospim8 = _mm256_set1_epi32(-cospi[8]);
  const __m256i cospi24 = _mm256_set1_epi32(cospi[24]);
  const __m256i cospim24 = _mm256_set1_epi32(-cospi[24]);
  const __m256i cospim40 = _mm256_set1_epi32(-cospi[40]);
  const __m256i cospi40 = _mm256_set1_epi32(cospi[40]);
  const __m256i cospi2 = _mm256_set1_epi32(cospi[2]);
  const __m256i cospi62 = _mm256_set1_epi32(cospi[62]);
  const __m256i cospim2 = _mm256_set1_epi32(-cospi[2]);
  const __m256i cospi10 = _mm256_set1_epi32(cospi[10]);
  const __m256i cospi54 = _mm256_set1_epi32(cospi[54]);
  const __m256i cospim10 = _mm256_set1_epi32(-cospi[10]);
  const __m256i cospi18 = _mm256_set1_epi32(cospi[18]);
  const __m256i cospi46 = _mm256_set1_epi32(cospi[46]);
  const __m256i cospim18 = _mm256_set1_epi32(-cospi[18]);
  const __m256i cospi26 = _mm256_set1_epi32(cospi[26]);
  const __m256i cospi38 = _mm256_set1_epi32(cospi[38]);
  const __m256i cospim26 = _mm256_set1_epi32(-cospi[26]);
  const __m256i cospi34 = _mm256_set1_epi32(cospi[34]);
  const __m256i cospi30 = _mm256_set1_epi32(cospi[30]);
  const __m256i cospim34 = _mm256_set1_epi32(-cospi[34]);
  const __m256i cospi42 = _mm256_set1_epi32(cospi[42]);
  const __m256i cospi22 = _mm256_set1_epi32(cospi[22]);
  const __m256i cospim42 = _mm256_set1_epi32(-cospi[42]);
  const __m256i cospi50 = _mm256_set1_epi32(cospi[50]);
  const __m256i cospi14 = _mm256_set1_epi32(cospi[14]);
  const __m256i cospim50 = _mm256_set1_epi32(-cospi[50]);
  const __m256i cospi58 = _mm256_set1_epi32(cospi[58]);
  const __m256i cospi6 = _mm256_set1_epi32(cospi[6]);
  const __m256i cospim58 = _mm256_set1_epi32(-cospi[58]);
  const __m256i rnding = _mm256_set1_epi32(1 << (bit - 1));
  const __m256i zero = _mm256_setzero_si256();

  __m256i u[16], v[16], x, y;
  int col;

  for (col = 0; col < num_cols; ++col) {
    // stage 0
    // stage 1
    u[0] = in[0 * num_cols + col];
    u[1] = _mm256_sub_epi32(zero, in[15 * num_cols + col]);
    u[2] = _mm256_sub_epi32(zero, in[7 * num_cols + col]);
    u[3] = in[8 * num_cols + col];
    u[4] = _mm256_sub_epi32(zero, in[3 * num_cols + col]);
    u[5] = in[12 * num_cols + col];
    u[6] = in[4 * num_cols + col];
    u[7] = _mm256_sub_epi32(zero, in[11 * num_cols + col]);
    u[8] = _mm256_sub_epi32(zero, in[1 * num_cols + col]);
    u[9] = in[14 * num_cols + col];
    u[10] = in[6 * num_cols + col];
    u[11] = _mm256_sub_epi32(zero, in[9 * num_cols + col]);
    u[12] = in[2 * num_cols + col];
    u[13] = _mm256_sub_epi32(zero, in[13 * num_cols + col]);
    u[14] = _mm256_sub_epi32(zero, in[5 * num_cols + col]);
    u[15] = in[10 * num_cols + col];

    // stage 2
    v[0] = u[0];
    v[1] = u[1];

    x = _mm256_mullo_epi32(u[2], cospi32);
    y = _mm256_mullo_epi32(u[3], cospi32);
    v[2] = _mm256_add_epi32(x, y);
    v[2] = _mm256_add_epi32(v[2], rnding);
    v[2] = _mm256_srai_epi32(v[2], bit);

    v[3] = _mm256_sub_epi32(x, y);
    v[3] = _mm256_add_epi32(v[3], rnding);
    v[3] = _mm256_srai_epi32(v[3], bit);

    v[4] = u[4];
    v[5] = u[5];

    x = _mm256_mullo_epi32(u[6], cospi32);
    y = _mm256_mullo_epi32(u[7], cospi32);
    v[6] = _mm256_add_epi32(x, y);
    v[6] = _mm256_add_epi32(v[6], rnding);
    v[6] = _mm256_srai_epi32(v[6], bit);

    v[7] = _mm256_sub_epi32(x, y);
    v[7] = _mm256_add_epi32(v[7], rnding);
    v[7] = _mm256_srai_epi32(v[7], bit);

    v[8] = u[8];
    v[9] = u[9];

    x = _mm256_mullo_epi32(u[10], cospi32);
    y = _mm256_mullo_epi32(u[11], cospi32);
    v[10] = _mm256_add_epi32(x, y);
    v[10] = _mm256_add_epi32(v[10], rnding);
    v[10] = _mm256_srai_epi32(v[10], bit);

    v[11] = _mm256_sub_epi32(x, y);
    v[11] = _mm256_add_epi32(v[11], rnding);
    v[11] = _mm256_srai_epi32(v[11], bit);

    v[12] = u[12];
    v[13] = u[13];

    x = _mm256_mullo_epi32(u[14], cospi32);
    y = _mm256_mullo_epi32(u[15], cospi32);
    v[14] = _mm256_add_epi32(x, y);
    v[14] = _mm256_add_epi32(v[14], rnding);
    v[14] = _mm256_srai_epi32(v[14], bit);

    v[15] = _mm256_sub_epi32(x, y);
    v[15] = _mm256_add_epi32(v[15], rnding);
    v[15] = _mm256_srai_epi32(v[15], bit);

    // stage 3
    u[0] = _mm256_add_epi32(v[0], v[2]);
    u[1] = _mm256_add_epi32(v[1], v[3]);
    u[2] = _mm256_sub_epi32(v[0], v[2]);
    u[3] = _mm256_sub_epi32(v[1], v[3]);
    u[4] = _mm256_add_epi32(v[4], v[6]);
    u[5] = _mm256_add_epi32(v[5], v[7]);
    u[6] = _mm256_sub_epi32(v[4], v[6]);
    u[7] = _mm256_sub_epi32(v[5], v[7]);
    u[8] = _mm256_add_epi32(v[8], v[10]);
    u[9] = _mm256_add_epi32(v[9], v[11]);
    u[10] = _mm256_sub_epi32(v[8], v[10]);
    u[11] = _mm256_sub_epi32(v[9], v[11]);
    u[12] = _mm256_add_epi32(v[12], v[14]);
    u[13] = _mm256_add_epi32(v[13], v[15]);
    u[14] = _mm256_sub_epi32(v[12], v[14]);
    u[15] = _mm256_sub_epi32(v[13], v[15]);

    // stage 4
    v[0] = u[0];
    v[1] = u[1];
    v[2] = u[2];
    v[3] = u[3];
    v[4] = av1_half_btf_avx2(&cospi16, &u[4], &cospi48, &u[5], &rnding, bit);
    v[5] = av1_half_btf_avx2(&cospi48, &u[4], &cospim16, &u[5], &rnding, bit);
    v[6] = av1_half_btf_avx2(&cospim48, &u[6], &cospi16, &u[7], &rnding, bit);
    v[7] = av1_half_btf_avx2(&cospi16, &u[6], &cospi48, &u[7], &rnding, bit);
    v[8] = u[8];
    v[9] = u[9];
    v[10] = u[10];
    v[11] = u[11];
    v[12] = av1_half_btf_avx2(&cospi16, &u[12], &cospi48, &u[13], &rnding, bit);
    v[13] =
        av1_half_btf_avx2(&cospi48, &u[12], &cospim16, &u[13], &rnding, bit);
    v[14] =
        av1_half_btf_avx2(&cospim48, &u[14], &cospi16, &u[15], &rnding, bit);
    v[15] = av1_half_btf_avx2(&cospi16, &u[14], &cospi48, &u[15], &rnding, bit);

    // stage 5
    u[0] = _mm256_add_epi32(v[0], v[4]);
    u[1] = _mm256_add_epi32(v[1], v[5]);
    u[2] = _mm256_add_epi32(v[2], v[6]);
    u[3] = _mm256_add_epi32(v[3], v[7]);
    u[4] = _mm256_sub_epi32(v[0], v[4]);
    u[5] = _mm256_sub_epi32(v[1], v[5]);
    u[6] = _mm256_sub_epi32(v[2], v[6]);
    u[7] = _mm256_sub_epi32(v[3], v[7]);
    u[8] = _mm256_add_epi32(v[8], v[12]);
    u[9] = _mm256_add_epi32(v[9], v[13]);
    u[10] = _mm256_add_epi32(v[10], v[14]);
    u[11] = _mm256_add_epi32(v[11], v[15]);
    u[12] = _mm256_sub_epi32(v[8], v[12]);
    u[13] = _mm256_sub_epi32(v[9], v[13]);
    u[14] = _mm256_sub_epi32(v[10], v[14]);
    u[15] = _mm256_sub_epi32(v[11], v[15]);

    // stage 6
    v[0] = u[0];
    v[1] = u[1];
    v[2] = u[2];
    v[3] = u[3];
    v[4] = u[4];
    v[5] = u[5];
    v[6] = u[6];
    v[7] = u[7];
    v[8] = av1_half_btf_avx2(&cospi8, &u[8], &cospi56, &u[9], &rnding, bit);
    v[9] = av1_half_btf_avx2(&cospi56, &u[8], &cospim8, &u[9], &rnding, bit);
    v[10] = av1_half_btf_avx2(&cospi40, &u[10], &cospi24, &u[11], &rnding, bit);
    v[11] =
        av1_half_btf_avx2(&cospi24, &u[10], &cospim40, &u[11], &rnding, bit);
    v[12] = av1_half_btf_avx2(&cospim56, &u[12], &cospi8, &u[13], &rnding, bit);
    v[13] = av1_half_btf_avx2(&cospi8, &u[12], &cospi56, &u[13], &rnding, bit);
    v[14] =
        av1_half_btf_avx2(&cospim24, &u[14], &cospi40, &u[15], &rnding, bit);
    v[15] = av1_half_btf_avx2(&cospi40, &u[14], &cospi24, &u[15], &rnding, bit);

    // stage 7
    u[0] = _mm256_add_epi32(v[0], v[8]);
    u[1] = _mm256_add_epi32(v[1], v[9]);
    u[2] = _mm256_add_epi32(v[2], v[10]);
    u[3] = _mm256_add_epi32(v[3], v[11]);
    u[4] = _mm256_add_epi32(v[4], v[12]);
    u[5] = _mm256_add_epi32(v[5], v[13]);
    u[6] = _mm256_add_epi32(v[6], v[14]);
    u[7] = _mm256_add_epi32(v[7], v[15]);
    u[8] = _mm256_sub_epi32(v[0], v[8]);
    u[9] = _mm256_sub_epi32(v[1], v[9]);
    u[10] = _mm256_sub_epi32(v[2], v[10]);
    u[11] = _mm256_sub_epi32(v[3], v[11]);
    u[12] = _mm256_sub_epi32(v[4], v[12]);
    u[13] = _mm256_sub_epi32(v[5], v[13]);
    u[14] = _mm256_sub_epi32(v[6], v[14]);
    u[15] = _mm256_sub_epi32(v[7], v[15]);

    // stage 8
    v[0] = av1_half_btf_avx2(&cospi2, &u[0], &cospi62, &u[1], &rnding, bit);
    v[1] = av1_half_btf_avx2(&cospi62, &u[0], &cospim2, &u[1], &rnding, bit);
    v[2] = av1_half_btf_avx2(&cospi10, &u[2], &cospi54, &u[3], &rnding, bit);
    v[3] = av1_half_btf_avx2(&cospi54, &u[2], &cospim10, &u[3], &rnding, bit);
    v[4] = av1_half_btf_avx2(&cospi18, &u[4], &cospi46, &u[5], &rnding, bit);
    v[5] = av1_half_btf_avx2(&cospi46, &u[4], &cospim18, &u[5], &rnding, bit);
    v[6] = av1_half_btf_avx2(&cospi26, &u[6], &cospi38, &u[7], &rnding, bit);
    v[7] = av1_half_btf_avx2(&cospi38, &u[6], &cospim26, &u[7], &rnding, bit);
    v[8] = av1_half_btf_avx2(&cospi34, &u[8], &cospi30, &u[9], &rnding, bit);
    v[9] = av1_half_btf_avx2(&cospi30, &u[8], &cospim34, &u[9], &rnding, bit);
    v[10] = av1_half_btf_avx2(&cospi42, &u[10], &cospi22, &u[11], &rnding, bit);
    v[11] =
        av1_half_btf_avx2(&cospi22, &u[10], &cospim42, &u[11], &rnding, bit);
    v[12] = av1_half_btf_avx2(&cospi50, &u[12], &cospi14, &u[13], &rnding, bit);
    v[13] =
        av1_half_btf_avx2(&cospi14, &u[12], &cospim50, &u[13], &rnding, bit);
    v[14] = av1_half_btf_avx2(&cospi58, &u[14], &cospi6, &u[15], &rnding, bit);
    v[15] = av1_half_btf_avx2(&cospi6, &u[14], &cospim58, &u[15], &rnding, bit);

    // stage 9
    out[0 * outstride + col] = v[1];
    out[1 * outstride + col] = v[14];
    out[2 * outstride + col] = v[3];
    out[3 * outstride + col] = v[12];
    out[4 * outstride + col] = v[5];
    out[5 * outstride + col] = v[10];
    out[6 * outstride + col] = v[7];
    out[7 * outstride + col] = v[8];
    out[8 * outstride + col] = v[9];
    out[9 * outstride + col] = v[6];
    out[10 * outstride + col] = v[11];
    out[11 * outstride + col] = v[4];
    out[12 * outstride + col] = v[13];
    out[13 * outstride + col] = v[2];
    out[14 * outstride + col] = v[15];
    out[15 * outstride + col] = v[0];
  }
}
#endif  // USE_TUNED_ADST16

static void idtx16_avx2(__m256i *in, __m256i *out, const int8_t bit,
                        int col_num, const int outstride) {
  (void)bit;
  (void)outstride;
  __m256i fact = _mm256_set1_epi32(2 * NewSqrt2);
  __m256i offset = _mm256_set1_epi32(1 << (NewSqrt2Bits - 1));
  __m256i a_low;

  int num_iters = 16 * col_num;
  for (int i = 0; i < num_iters; i++) {
    a_low = _mm256_mullo_epi32(in[i], fact);
    a_low = _mm256_add_epi32(a_low, offset);
    out[i] = _mm256_srai_epi32(a_low, NewSqrt2Bits);
  }
}
static const transform_1d_avx2 col_highbd_txfm8x16_arr_inter[TX_TYPES] = {
  fdct16_avx2,  // DCT_DCT
  fddt16_avx2,  // ADST_DCT
  fdct16_avx2,  // DCT_ADST
  fddt16_avx2,  // ADST_ADST
  fddt16_avx2,  // FLIPADST_DCT
  fdct16_avx2,  // DCT_FLIPADST
  fddt16_avx2,  // FLIPADST_FLIPADST
  fddt16_avx2,  // ADST_FLIPADST
  fddt16_avx2,  // FLIPADST_ADST
  idtx16_avx2,  // IDTX
  fdct16_avx2,  // V_DCT
  idtx16_avx2,  // H_DCT
  fddt16_avx2,  // V_ADST
  idtx16_avx2,  // H_ADST
  fddt16_avx2,  // V_FLIPADST
  idtx16_avx2,  // H_FLIPADST
};
static const transform_1d_avx2 row_highbd_txfm8x8_arr_inter[TX_TYPES] = {
  fdct8_avx2,  // DCT_DCT
  fdct8_avx2,  // ADST_DCT
  fddt8_avx2,  // DCT_ADST
  fddt8_avx2,  // ADST_ADST
  fdct8_avx2,  // FLIPADST_DCT
  fddt8_avx2,  // DCT_FLIPADST
  fddt8_avx2,  // FLIPADST_FLIPADST
  fddt8_avx2,  // ADST_FLIPADST
  fddt8_avx2,  // FLIPADST_ADST
  idtx8_avx2,  // IDTX
  idtx8_avx2,  // V_DCT
  fdct8_avx2,  // H_DCT
  idtx8_avx2,  // V_ADST
  fddt8_avx2,  // H_ADST
  idtx8_avx2,  // V_FLIPADST
  fddt8_avx2,  // H_FLIPADST
};
static const transform_1d_avx2 col_highbd_txfm8x8_arr_inter[TX_TYPES] = {
  fdct8_avx2,  // DCT_DCT
  fddt8_avx2,  // ADST_DCT
  fdct8_avx2,  // DCT_ADST
  fddt8_avx2,  // ADST_ADST
  fddt8_avx2,  // FLIPADST_DCT
  fdct8_avx2,  // DCT_FLIPADST
  fddt8_avx2,  // FLIPADST_FLIPADST
  fddt8_avx2,  // ADST_FLIPADST
  fddt8_avx2,  // FLIPADST_ADST
  idtx8_avx2,  // IDTX
  fdct8_avx2,  // V_DCT
  idtx8_avx2,  // H_DCT
  fddt8_avx2,  // V_ADST
  idtx8_avx2,  // H_ADST
  fddt8_avx2,  // V_FLIPADST
  idtx8_avx2,  // H_FLIPADST
};
static const transform_1d_avx2 row_highbd_txfm8x16_arr_inter[TX_TYPES] = {
  fdct16_avx2,  // DCT_DCT
  fdct16_avx2,  // ADST_DCT
  fddt16_avx2,  // DCT_ADST
  fddt16_avx2,  // ADST_ADST
  fdct16_avx2,  // FLIPADST_DCT
  fddt16_avx2,  // DCT_FLIPADST
  fddt16_avx2,  // FLIPADST_FLIPADST
  fddt16_avx2,  // ADST_FLIPADST
  fddt16_avx2,  // FLIPADST_ADST
  idtx16_avx2,  // IDTX
  idtx16_avx2,  // V_DCT
  fdct16_avx2,  // H_DCT
  idtx16_avx2,  // V_ADST
  fddt16_avx2,  // H_ADST
  idtx16_avx2,  // V_FLIPADST
  fddt16_avx2,  // H_FLIPADST
};
static const transform_1d_avx2 col_highbd_txfm8x16_arr[TX_TYPES] = {
  fdct16_avx2,   // DCT_DCT
  fadst16_avx2,  // ADST_DCT
  fdct16_avx2,   // DCT_ADST
  fadst16_avx2,  // ADST_ADST
  fadst16_avx2,  // FLIPADST_DCT
  fdct16_avx2,   // DCT_FLIPADST
  fadst16_avx2,  // FLIPADST_FLIPADST
  fadst16_avx2,  // ADST_FLIPADST
  fadst16_avx2,  // FLIPADST_ADST
  idtx16_avx2,   // IDTX
  fdct16_avx2,   // V_DCT
  idtx16_avx2,   // H_DCT
  fadst16_avx2,  // V_ADST
  idtx16_avx2,   // H_ADST
  fadst16_avx2,  // V_FLIPADST
  idtx16_avx2,   // H_FLIPADST
};
static const transform_1d_avx2 row_highbd_txfm8x8_arr[TX_TYPES] = {
  fdct8_avx2,   // DCT_DCT
  fdct8_avx2,   // ADST_DCT
  fadst8_avx2,  // DCT_ADST
  fadst8_avx2,  // ADST_ADST
  fdct8_avx2,   // FLIPADST_DCT
  fadst8_avx2,  // DCT_FLIPADST
  fadst8_avx2,  // FLIPADST_FLIPADST
  fadst8_avx2,  // ADST_FLIPADST
  fadst8_avx2,  // FLIPADST_ADST
  idtx8_avx2,   // IDTX
  idtx8_avx2,   // V_DCT
  fdct8_avx2,   // H_DCT
  idtx8_avx2,   // V_ADST
  fadst8_avx2,  // H_ADST
  idtx8_avx2,   // V_FLIPADST
  fadst8_avx2,  // H_FLIPADST
};
void av1_fwd_txfm2d_8x16_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  __m256i in[16], out[16];
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_8X16];
  const int txw_idx = get_txw_idx(TX_8X16);
  const int txh_idx = get_txh_idx(TX_8X16);
  const transform_1d_avx2 col_txfm =
      (use_ddt && REPLACE_ADST16) ? col_highbd_txfm8x16_arr_inter[tx_type]
                                  : col_highbd_txfm8x16_arr[tx_type];
  const transform_1d_avx2 row_txfm = (use_ddt && REPLACE_ADST8)
                                         ? row_highbd_txfm8x8_arr_inter[tx_type]
                                         : row_highbd_txfm8x8_arr[tx_type];
  const int8_t bit = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);

  load_buffer_8x16_avx2(input, in, stride, ud_flip, lr_flip, shift[0]);
  col_txfm(in, out, bit, 1, 1);
  col_txfm_8x8_rounding(out, -shift[1]);
  col_txfm_8x8_rounding(&out[8], -shift[1]);
  fwd_txfm_transpose_8x8_avx2(out, in, 1, 2);
  fwd_txfm_transpose_8x8_avx2(&out[8], &in[1], 1, 2);
  row_txfm(in, out, bit, 2, 2);
  fwd_txfm_transpose_8x8_avx2(out, in, 2, 1);
  fwd_txfm_transpose_8x8_avx2(&out[1], &in[8], 2, 1);
  av1_round_shift_rect_array_32_avx2(in, in, 16, -shift[2], NewSqrt2);
  store_buffer_avx2(in, coeff, 8, 16);
  (void)bd;
}
static const transform_1d_avx2 col_highbd_txfm8x8_arr[TX_TYPES] = {
  fdct8_avx2,   // DCT_DCT
  fadst8_avx2,  // ADST_DCT
  fdct8_avx2,   // DCT_ADST
  fadst8_avx2,  // ADST_ADST
  fadst8_avx2,  // FLIPADST_DCT
  fdct8_avx2,   // DCT_FLIPADST
  fadst8_avx2,  // FLIPADST_FLIPADST
  fadst8_avx2,  // ADST_FLIPADST
  fadst8_avx2,  // FLIPADST_ADST
  idtx8_avx2,   // IDTX
  fdct8_avx2,   // V_DCT
  idtx8_avx2,   // H_DCT
  fadst8_avx2,  // V_ADST
  idtx8_avx2,   // H_ADST
  fadst8_avx2,  // V_FLIPADST
  idtx8_avx2,   // H_FLIPADST
};
static const transform_1d_avx2 row_highbd_txfm8x16_arr[TX_TYPES] = {
  fdct16_avx2,   // DCT_DCT
  fdct16_avx2,   // ADST_DCT
  fadst16_avx2,  // DCT_ADST
  fadst16_avx2,  // ADST_ADST
  fdct16_avx2,   // FLIPADST_DCT
  fadst16_avx2,  // DCT_FLIPADST
  fadst16_avx2,  // FLIPADST_FLIPADST
  fadst16_avx2,  // ADST_FLIPADST
  fadst16_avx2,  // FLIPADST_ADST
  idtx16_avx2,   // IDTX
  idtx16_avx2,   // V_DCT
  fdct16_avx2,   // H_DCT
  idtx16_avx2,   // V_ADST
  fadst16_avx2,  // H_ADST
  idtx16_avx2,   // V_FLIPADST
  fadst16_avx2,  // H_FLIPADST
};
void av1_fwd_txfm2d_16x8_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  __m256i in[16], out[16];
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_16X8];
  const int txw_idx = get_txw_idx(TX_16X8);
  const int txh_idx = get_txh_idx(TX_16X8);
  const transform_1d_avx2 col_txfm = (use_ddt && REPLACE_ADST8)
                                         ? col_highbd_txfm8x8_arr_inter[tx_type]
                                         : col_highbd_txfm8x8_arr[tx_type];
  const transform_1d_avx2 row_txfm =
      (use_ddt && REPLACE_ADST16) ? row_highbd_txfm8x16_arr_inter[tx_type]
                                  : row_highbd_txfm8x16_arr[tx_type];
  const int8_t bit = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_16xn_avx2(input, in, stride, 8, 2, ud_flip, lr_flip);
  round_shift_32_8xn_avx2(in, 16, shift[0], 1);
  col_txfm(in, out, bit, 2, 2);
  round_shift_32_8xn_avx2(out, 16, shift[1], 1);
  fwd_txfm_transpose_8x8_avx2(out, in, 2, 1);
  fwd_txfm_transpose_8x8_avx2(&out[1], &in[8], 2, 1);
  // row transform
  row_txfm(in, out, bit, 1, 1);
  fwd_txfm_transpose_8x8_avx2(out, in, 1, 2);
  fwd_txfm_transpose_8x8_avx2(&out[8], &in[1], 1, 2);
  av1_round_shift_rect_array_32_avx2(in, in, 16, -shift[2], NewSqrt2);
  store_buffer_avx2(in, coeff, 8, 16);
  (void)bd;
}

static INLINE void convert_8x8_to_16x16(const __m256i *in, __m256i *out) {
  int32_t row_index = 0;
  int32_t dst_index = 0;
  int32_t src_index = 0;

  // row 0, 1, .., 7
  do {
    out[dst_index] = in[src_index];
    out[dst_index + 1] = in[src_index + 8];
    dst_index += 2;
    src_index++;
    row_index++;
  } while (row_index < 8);

  // row 8, 9, ..., 15
  src_index += 8;
  do {
    out[dst_index] = in[src_index];
    out[dst_index + 1] = in[src_index + 8];
    dst_index += 2;
    src_index++;
    row_index++;
  } while (row_index < 16);
}

static INLINE void load_buffer_8x8(const int16_t *input, __m256i *in,
                                   int stride, int flipud, int fliplr,
                                   int8_t shift) {
  __m128i temp[8];
  if (!flipud) {
    temp[0] = _mm_loadu_si128((const __m128i *)(input + 0 * stride));
    temp[1] = _mm_loadu_si128((const __m128i *)(input + 1 * stride));
    temp[2] = _mm_loadu_si128((const __m128i *)(input + 2 * stride));
    temp[3] = _mm_loadu_si128((const __m128i *)(input + 3 * stride));
    temp[4] = _mm_loadu_si128((const __m128i *)(input + 4 * stride));
    temp[5] = _mm_loadu_si128((const __m128i *)(input + 5 * stride));
    temp[6] = _mm_loadu_si128((const __m128i *)(input + 6 * stride));
    temp[7] = _mm_loadu_si128((const __m128i *)(input + 7 * stride));
  } else {
    temp[0] = _mm_loadu_si128((const __m128i *)(input + 7 * stride));
    temp[1] = _mm_loadu_si128((const __m128i *)(input + 6 * stride));
    temp[2] = _mm_loadu_si128((const __m128i *)(input + 5 * stride));
    temp[3] = _mm_loadu_si128((const __m128i *)(input + 4 * stride));
    temp[4] = _mm_loadu_si128((const __m128i *)(input + 3 * stride));
    temp[5] = _mm_loadu_si128((const __m128i *)(input + 2 * stride));
    temp[6] = _mm_loadu_si128((const __m128i *)(input + 1 * stride));
    temp[7] = _mm_loadu_si128((const __m128i *)(input + 0 * stride));
  }

  if (fliplr) {
    temp[0] = mm_reverse_epi16(temp[0]);
    temp[1] = mm_reverse_epi16(temp[1]);
    temp[2] = mm_reverse_epi16(temp[2]);
    temp[3] = mm_reverse_epi16(temp[3]);
    temp[4] = mm_reverse_epi16(temp[4]);
    temp[5] = mm_reverse_epi16(temp[5]);
    temp[6] = mm_reverse_epi16(temp[6]);
    temp[7] = mm_reverse_epi16(temp[7]);
  }

  in[0] = _mm256_cvtepi16_epi32(temp[0]);
  in[1] = _mm256_cvtepi16_epi32(temp[1]);
  in[2] = _mm256_cvtepi16_epi32(temp[2]);
  in[3] = _mm256_cvtepi16_epi32(temp[3]);
  in[4] = _mm256_cvtepi16_epi32(temp[4]);
  in[5] = _mm256_cvtepi16_epi32(temp[5]);
  in[6] = _mm256_cvtepi16_epi32(temp[6]);
  in[7] = _mm256_cvtepi16_epi32(temp[7]);

  in[0] = _mm256_slli_epi32(in[0], shift);
  in[1] = _mm256_slli_epi32(in[1], shift);
  in[2] = _mm256_slli_epi32(in[2], shift);
  in[3] = _mm256_slli_epi32(in[3], shift);
  in[4] = _mm256_slli_epi32(in[4], shift);
  in[5] = _mm256_slli_epi32(in[5], shift);
  in[6] = _mm256_slli_epi32(in[6], shift);
  in[7] = _mm256_slli_epi32(in[7], shift);
}

static INLINE void load_buffer_16x16(const int16_t *input, __m256i *out,
                                     int stride, int flipud, int fliplr,
                                     int8_t shift) {
  __m256i in[32];
  // Load 4 8x8 blocks
  const int16_t *top_l = input;
  const int16_t *top_r = input + 8;
  const int16_t *bot_l = input + 8 * stride;
  const int16_t *bot_r = input + 8 * stride + 8;

  const int16_t *tmp;

  if (flipud) {
    // Swap left columns
    tmp = top_l;
    top_l = bot_l;
    bot_l = tmp;
    // Swap right columns
    tmp = top_r;
    top_r = bot_r;
    bot_r = tmp;
  }

  if (fliplr) {
    // Swap top rows
    tmp = top_l;
    top_l = top_r;
    top_r = tmp;
    // Swap bottom rows
    tmp = bot_l;
    bot_l = bot_r;
    bot_r = tmp;
  }

  // load first 8 columns
  load_buffer_8x8(top_l, &in[0], stride, flipud, fliplr, shift);
  load_buffer_8x8(bot_l, &in[16], stride, flipud, fliplr, shift);

  // load second 8 columns
  load_buffer_8x8(top_r, &in[8], stride, flipud, fliplr, shift);
  load_buffer_8x8(bot_r, &in[24], stride, flipud, fliplr, shift);

  convert_8x8_to_16x16(in, out);
}

static INLINE void write_buffer_16x16(const __m256i *res, int32_t *output) {
  int32_t fact = 0, index = 0;
  for (int32_t i = 0; i < 8; ++i) {
    _mm256_storeu_si256((__m256i *)(output + fact * 16), res[index++]);
    _mm256_storeu_si256((__m256i *)(output + fact * 16 + 8), res[index++]);
    ++fact;
    _mm256_storeu_si256((__m256i *)(output + fact * 16), res[index++]);
    _mm256_storeu_si256((__m256i *)(output + fact * 16 + 8), res[index++]);
    ++fact;
  }
}

void av1_fwd_txfm2d_16x16_avx2(const int16_t *input, int32_t *coeff, int stride,
                               TX_TYPE tx_type, int use_ddt, int bd) {
  __m256i in[32], out[32];
  const TX_SIZE tx_size = TX_16X16;
  const int8_t *shift = av1_fwd_txfm_shift_ls[tx_size];
  const int txw_idx = get_txw_idx(tx_size);
  const int txh_idx = get_txh_idx(tx_size);
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const int width_div8 = (width >> 3);
  const int width_div16 = (width >> 4);
  const int size = (height << 1);

  switch (tx_type) {
    case DCT_DCT:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      fdct16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      fdct16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case ADST_DCT:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      fdct16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case DCT_ADST:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      fdct16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case ADST_ADST:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case FLIPADST_DCT:
      load_buffer_16x16(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      fdct16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case DCT_FLIPADST:
      load_buffer_16x16(input, in, stride, 0, 1, shift[0]);
      fdct16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case FLIPADST_FLIPADST:
      load_buffer_16x16(input, in, stride, 1, 1, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case ADST_FLIPADST:
      load_buffer_16x16(input, in, stride, 0, 1, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case FLIPADST_ADST:
      load_buffer_16x16(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case IDTX:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      idtx16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      idtx16_avx2(out, in, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      write_buffer_16x16(in, coeff);
      break;
    case V_DCT:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      fdct16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      idtx16_avx2(out, in, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      write_buffer_16x16(in, coeff);
      break;
    case H_DCT:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      idtx16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      fdct16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case V_ADST:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      idtx16_avx2(out, in, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      write_buffer_16x16(in, coeff);
      break;
    case H_ADST:
      load_buffer_16x16(input, in, stride, 0, 0, shift[0]);
      idtx16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    case V_FLIPADST:
      load_buffer_16x16(input, in, stride, 1, 0, shift[0]);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                     width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      idtx16_avx2(out, in, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                  width_div8);
      write_buffer_16x16(in, coeff);
      break;
    case H_FLIPADST:
      load_buffer_16x16(input, in, stride, 0, 1, shift[0]);
      idtx16_avx2(in, out, av1_fwd_cos_bit_col[txw_idx][txh_idx], width_div8,
                  width_div8);
      round_shift_32_8xn_avx2(out, size, shift[1], width_div16);
      fwd_txfm_transpose_16x16_avx2(out, in);
      if (use_ddt && REPLACE_ADST16)
        fddt16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                    width_div8);
      else
        fadst16_avx2(in, out, av1_fwd_cos_bit_row[txw_idx][txh_idx], width_div8,
                     width_div8);
      fwd_txfm_transpose_16x16_avx2(out, in);
      write_buffer_16x16(in, coeff);
      break;
    default: assert(0);
  }
  (void)bd;
}

static INLINE void fdct32_avx2(__m256i *input, __m256i *output,
                               const int8_t cos_bit, const int instride,
                               const int outstride) {
  __m256i buf0[32];
  __m256i buf1[32];
  const int32_t *cospi;
  int startidx = 0 * instride;
  int endidx = 31 * instride;
  // stage 0
  // stage 1
  buf1[0] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[31] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[1] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[30] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[2] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[29] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[3] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[28] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[4] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[27] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[5] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[26] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[6] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[25] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[7] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[24] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[8] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[23] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[9] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[22] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[10] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[21] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[11] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[20] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[12] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[19] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[13] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[18] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[14] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[17] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  buf1[15] = _mm256_add_epi32(input[startidx], input[endidx]);
  buf1[16] = _mm256_sub_epi32(input[startidx], input[endidx]);

  // stage 2
  cospi = cospi_arr(cos_bit);
  buf0[0] = _mm256_add_epi32(buf1[0], buf1[15]);
  buf0[15] = _mm256_sub_epi32(buf1[0], buf1[15]);
  buf0[1] = _mm256_add_epi32(buf1[1], buf1[14]);
  buf0[14] = _mm256_sub_epi32(buf1[1], buf1[14]);
  buf0[2] = _mm256_add_epi32(buf1[2], buf1[13]);
  buf0[13] = _mm256_sub_epi32(buf1[2], buf1[13]);
  buf0[3] = _mm256_add_epi32(buf1[3], buf1[12]);
  buf0[12] = _mm256_sub_epi32(buf1[3], buf1[12]);
  buf0[4] = _mm256_add_epi32(buf1[4], buf1[11]);
  buf0[11] = _mm256_sub_epi32(buf1[4], buf1[11]);
  buf0[5] = _mm256_add_epi32(buf1[5], buf1[10]);
  buf0[10] = _mm256_sub_epi32(buf1[5], buf1[10]);
  buf0[6] = _mm256_add_epi32(buf1[6], buf1[9]);
  buf0[9] = _mm256_sub_epi32(buf1[6], buf1[9]);
  buf0[7] = _mm256_add_epi32(buf1[7], buf1[8]);
  buf0[8] = _mm256_sub_epi32(buf1[7], buf1[8]);

  const __m256i round = _mm256_set1_epi32(1 << (cos_bit - 1));
  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf1[20], buf1[27], buf0[20],
                        buf0[27], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf1[21], buf1[26], buf0[21],
                        buf0[26], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf1[22], buf1[25], buf0[22],
                        buf0[25], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf1[23], buf1[24], buf0[23],
                        buf0[24], cos_bit, round);

  // stage 3
  buf1[0] = _mm256_add_epi32(buf0[0], buf0[7]);
  buf1[7] = _mm256_sub_epi32(buf0[0], buf0[7]);
  buf1[1] = _mm256_add_epi32(buf0[1], buf0[6]);
  buf1[6] = _mm256_sub_epi32(buf0[1], buf0[6]);
  buf1[2] = _mm256_add_epi32(buf0[2], buf0[5]);
  buf1[5] = _mm256_sub_epi32(buf0[2], buf0[5]);
  buf1[3] = _mm256_add_epi32(buf0[3], buf0[4]);
  buf1[4] = _mm256_sub_epi32(buf0[3], buf0[4]);

  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf0[10], buf0[13], buf1[10],
                        buf1[13], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf0[11], buf0[12], buf1[11],
                        buf1[12], cos_bit, round);

  buf1[23] = _mm256_sub_epi32(buf1[16], buf0[23]);
  buf1[16] = _mm256_add_epi32(buf1[16], buf0[23]);
  buf1[22] = _mm256_sub_epi32(buf1[17], buf0[22]);
  buf1[17] = _mm256_add_epi32(buf1[17], buf0[22]);
  buf1[21] = _mm256_sub_epi32(buf1[18], buf0[21]);
  buf1[18] = _mm256_add_epi32(buf1[18], buf0[21]);
  buf1[20] = _mm256_sub_epi32(buf1[19], buf0[20]);
  buf1[19] = _mm256_add_epi32(buf1[19], buf0[20]);
  buf1[24] = _mm256_sub_epi32(buf1[31], buf0[24]);
  buf1[31] = _mm256_add_epi32(buf1[31], buf0[24]);
  buf1[25] = _mm256_sub_epi32(buf1[30], buf0[25]);
  buf1[30] = _mm256_add_epi32(buf1[30], buf0[25]);
  buf1[26] = _mm256_sub_epi32(buf1[29], buf0[26]);
  buf1[29] = _mm256_add_epi32(buf1[29], buf0[26]);
  buf1[27] = _mm256_sub_epi32(buf1[28], buf0[27]);
  buf1[28] = _mm256_add_epi32(buf1[28], buf0[27]);

  // stage 4
  buf0[0] = _mm256_add_epi32(buf1[0], buf1[3]);
  buf0[3] = _mm256_sub_epi32(buf1[0], buf1[3]);
  buf0[1] = _mm256_add_epi32(buf1[1], buf1[2]);
  buf0[2] = _mm256_sub_epi32(buf1[1], buf1[2]);

  btf_32_avx2_type0_new(-cospi[32], cospi[32], buf1[5], buf1[6], buf0[5],
                        buf0[6], cos_bit, round);

  buf0[11] = _mm256_sub_epi32(buf0[8], buf1[11]);
  buf0[8] = _mm256_add_epi32(buf0[8], buf1[11]);
  buf0[10] = _mm256_sub_epi32(buf0[9], buf1[10]);
  buf0[9] = _mm256_add_epi32(buf0[9], buf1[10]);
  buf0[12] = _mm256_sub_epi32(buf0[15], buf1[12]);
  buf0[15] = _mm256_add_epi32(buf0[15], buf1[12]);
  buf0[13] = _mm256_sub_epi32(buf0[14], buf1[13]);
  buf0[14] = _mm256_add_epi32(buf0[14], buf1[13]);

  btf_32_avx2_type0_new(-cospi[16], cospi[48], buf1[18], buf1[29], buf0[18],
                        buf0[29], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[16], cospi[48], buf1[19], buf1[28], buf0[19],
                        buf0[28], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[48], -cospi[16], buf1[20], buf1[27], buf0[20],
                        buf0[27], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[48], -cospi[16], buf1[21], buf1[26], buf0[21],
                        buf0[26], cos_bit, round);

  // stage 5
  btf_32_avx2_type0_new(cospi[32], cospi[32], buf0[0], buf0[1], buf1[0],
                        buf1[1], cos_bit, round);
  btf_32_avx2_type0_new(cospi[16], cospi[48], buf0[3], buf0[2], buf1[2],
                        buf1[3], cos_bit, round);
  buf1[5] = _mm256_sub_epi32(buf1[4], buf0[5]);
  buf1[4] = _mm256_add_epi32(buf1[4], buf0[5]);
  buf1[6] = _mm256_sub_epi32(buf1[7], buf0[6]);
  buf1[7] = _mm256_add_epi32(buf1[7], buf0[6]);
  btf_32_avx2_type0_new(-cospi[16], cospi[48], buf0[9], buf0[14], buf1[9],
                        buf1[14], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[48], -cospi[16], buf0[10], buf0[13], buf1[10],
                        buf1[13], cos_bit, round);

  buf1[19] = _mm256_sub_epi32(buf1[16], buf0[19]);
  buf1[16] = _mm256_add_epi32(buf1[16], buf0[19]);
  buf1[18] = _mm256_sub_epi32(buf1[17], buf0[18]);
  buf1[17] = _mm256_add_epi32(buf1[17], buf0[18]);
  buf1[20] = _mm256_sub_epi32(buf1[23], buf0[20]);
  buf1[23] = _mm256_add_epi32(buf1[23], buf0[20]);
  buf1[21] = _mm256_sub_epi32(buf1[22], buf0[21]);
  buf1[22] = _mm256_add_epi32(buf1[22], buf0[21]);
  buf1[27] = _mm256_sub_epi32(buf1[24], buf0[27]);
  buf1[24] = _mm256_add_epi32(buf1[24], buf0[27]);
  buf1[26] = _mm256_sub_epi32(buf1[25], buf0[26]);
  buf1[25] = _mm256_add_epi32(buf1[25], buf0[26]);
  buf1[28] = _mm256_sub_epi32(buf1[31], buf0[28]);
  buf1[31] = _mm256_add_epi32(buf1[31], buf0[28]);
  buf1[29] = _mm256_sub_epi32(buf1[30], buf0[29]);
  buf1[30] = _mm256_add_epi32(buf1[30], buf0[29]);

  // stage 6
  btf_32_avx2_type0_new(cospi[8], cospi[56], buf1[7], buf1[4], buf0[4], buf0[7],
                        cos_bit, round);
  btf_32_avx2_type0_new(cospi[40], cospi[24], buf1[6], buf1[5], buf0[5],
                        buf0[6], cos_bit, round);
  buf0[9] = _mm256_sub_epi32(buf0[8], buf1[9]);
  buf0[8] = _mm256_add_epi32(buf0[8], buf1[9]);
  buf0[10] = _mm256_sub_epi32(buf0[11], buf1[10]);
  buf0[11] = _mm256_add_epi32(buf0[11], buf1[10]);
  buf0[13] = _mm256_sub_epi32(buf0[12], buf1[13]);
  buf0[12] = _mm256_add_epi32(buf0[12], buf1[13]);
  buf0[14] = _mm256_sub_epi32(buf0[15], buf1[14]);
  buf0[15] = _mm256_add_epi32(buf0[15], buf1[14]);
  btf_32_avx2_type0_new(-cospi[8], cospi[56], buf1[17], buf1[30], buf0[17],
                        buf0[30], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[56], -cospi[8], buf1[18], buf1[29], buf0[18],
                        buf0[29], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[40], cospi[24], buf1[21], buf1[26], buf0[21],
                        buf0[26], cos_bit, round);
  btf_32_avx2_type0_new(-cospi[24], -cospi[40], buf1[22], buf1[25], buf0[22],
                        buf0[25], cos_bit, round);

  // stage 7
  btf_32_avx2_type0_new(cospi[4], cospi[60], buf0[15], buf0[8], buf1[8],
                        buf1[15], cos_bit, round);
  btf_32_avx2_type0_new(cospi[36], cospi[28], buf0[14], buf0[9], buf1[9],
                        buf1[14], cos_bit, round);
  btf_32_avx2_type0_new(cospi[20], cospi[44], buf0[13], buf0[10], buf1[10],
                        buf1[13], cos_bit, round);
  btf_32_avx2_type0_new(cospi[52], cospi[12], buf0[12], buf0[11], buf1[11],
                        buf1[12], cos_bit, round);
  buf1[17] = _mm256_sub_epi32(buf1[16], buf0[17]);
  buf1[16] = _mm256_add_epi32(buf1[16], buf0[17]);
  buf1[18] = _mm256_sub_epi32(buf1[19], buf0[18]);
  buf1[19] = _mm256_add_epi32(buf1[19], buf0[18]);
  buf1[21] = _mm256_sub_epi32(buf1[20], buf0[21]);
  buf1[20] = _mm256_add_epi32(buf1[20], buf0[21]);
  buf1[22] = _mm256_sub_epi32(buf1[23], buf0[22]);
  buf1[23] = _mm256_add_epi32(buf1[23], buf0[22]);
  buf1[25] = _mm256_sub_epi32(buf1[24], buf0[25]);
  buf1[24] = _mm256_add_epi32(buf1[24], buf0[25]);
  buf1[26] = _mm256_sub_epi32(buf1[27], buf0[26]);
  buf1[27] = _mm256_add_epi32(buf1[27], buf0[26]);
  buf1[29] = _mm256_sub_epi32(buf1[28], buf0[29]);
  buf1[28] = _mm256_add_epi32(buf1[28], buf0[29]);
  buf1[30] = _mm256_sub_epi32(buf1[31], buf0[30]);
  buf1[31] = _mm256_add_epi32(buf1[31], buf0[30]);

  // stage 8
  btf_32_avx2_type0_new(cospi[2], cospi[62], buf1[31], buf1[16], buf0[16],
                        buf0[31], cos_bit, round);
  btf_32_avx2_type0_new(cospi[34], cospi[30], buf1[30], buf1[17], buf0[17],
                        buf0[30], cos_bit, round);
  btf_32_avx2_type0_new(cospi[18], cospi[46], buf1[29], buf1[18], buf0[18],
                        buf0[29], cos_bit, round);
  btf_32_avx2_type0_new(cospi[50], cospi[14], buf1[28], buf1[19], buf0[19],
                        buf0[28], cos_bit, round);
  btf_32_avx2_type0_new(cospi[10], cospi[54], buf1[27], buf1[20], buf0[20],
                        buf0[27], cos_bit, round);
  btf_32_avx2_type0_new(cospi[42], cospi[22], buf1[26], buf1[21], buf0[21],
                        buf0[26], cos_bit, round);
  btf_32_avx2_type0_new(cospi[26], cospi[38], buf1[25], buf1[22], buf0[22],
                        buf0[25], cos_bit, round);
  btf_32_avx2_type0_new(cospi[58], cospi[6], buf1[24], buf1[23], buf0[23],
                        buf0[24], cos_bit, round);

  startidx = 0 * outstride;
  endidx = 31 * outstride;
  // stage 9
  output[startidx] = buf1[0];
  output[endidx] = buf0[31];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[16];
  output[endidx] = buf1[15];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf1[8];
  output[endidx] = buf0[23];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[24];
  output[endidx] = buf0[7];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[4];
  output[endidx] = buf0[27];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[20];
  output[endidx] = buf1[11];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf1[12];
  output[endidx] = buf0[19];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[28];
  output[endidx] = buf1[3];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf1[2];
  output[endidx] = buf0[29];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[18];
  output[endidx] = buf1[13];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf1[10];
  output[endidx] = buf0[21];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[26];
  output[endidx] = buf0[5];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[6];
  output[endidx] = buf0[25];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[22];
  output[endidx] = buf1[9];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf1[14];
  output[endidx] = buf0[17];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = buf0[30];
  output[endidx] = buf1[1];
}
static INLINE void idtx32x32_avx2(__m256i *input, __m256i *output,
                                  const int8_t cos_bit, int instride,
                                  int outstride) {
  (void)cos_bit;
  for (int i = 0; i < 32; i += 8) {
    output[i * outstride] = _mm256_slli_epi32(input[i * instride], 2);
    output[(i + 1) * outstride] =
        _mm256_slli_epi32(input[(i + 1) * instride], 2);
    output[(i + 2) * outstride] =
        _mm256_slli_epi32(input[(i + 2) * instride], 2);
    output[(i + 3) * outstride] =
        _mm256_slli_epi32(input[(i + 3) * instride], 2);
    output[(i + 4) * outstride] =
        _mm256_slli_epi32(input[(i + 4) * instride], 2);
    output[(i + 5) * outstride] =
        _mm256_slli_epi32(input[(i + 5) * instride], 2);
    output[(i + 6) * outstride] =
        _mm256_slli_epi32(input[(i + 6) * instride], 2);
    output[(i + 7) * outstride] =
        _mm256_slli_epi32(input[(i + 7) * instride], 2);
  }
}

static INLINE void idtx32_avx2(__m256i *input, __m256i *output, int8_t cos_bit,
                               int32_t col_num, int32_t col) {
  (void)cos_bit;
  (void)col;
  for (int32_t i = 0; i < 32; i++)
    output[i * col_num] = _mm256_slli_epi32(input[i * col_num], 2);
}

static const transform_1d_avx2 highbd_txfm8_arr[TX_TYPES_1D] = {
  fdct8_avx2,   // DCT_1D
  fadst8_avx2,  // ADST_1D
  fadst8_avx2,  // FLIPADST_1D
  idtx8_avx2,   // IDTX_1D
  fddt8_avx2,   // DDT_1D
};

static const transform_1d_avx2 highbd_txfm16_arr[TX_TYPES_1D] = {
  fdct16_avx2,   // DCT_1D
  fadst16_avx2,  // ADST_1D
  fadst16_avx2,  // FLIPADST_1D
  idtx16_avx2,   // IDTX_1D
  fddt16_avx2,   // DDT_1D
};

static const transform_1d_avx2 highbd_txfm32_arr[TX_TYPES_1D] = {
  fdct32_avx2,  // DCT_1D
  NULL,         // ADST_1D
  NULL,         // FLIPADST_1D
  idtx32_avx2,  // IDTX_1D
  NULL,         // DDT_1D
};

static const transform_1d_avx2 col_txfm8x32_arr[TX_TYPES] = {
  fdct32_avx2,     // DCT_DCT
  NULL,            // ADST_DCT
  NULL,            // DCT_ADST
  NULL,            // ADST_ADST
  NULL,            // FLIPADST_DCT
  NULL,            // DCT_FLIPADST
  NULL,            // FLIPADST_FLIPADST
  NULL,            // ADST_FLIPADST
  NULL,            // FLIPADST_ADST
  idtx32x32_avx2,  // IDTX
  NULL,            // V_DCT
  NULL,            // H_DCT
  NULL,            // V_ADST
  NULL,            // H_ADST
  NULL,            // V_FLIPADST
  NULL,            // H_FLIPADST
};

static const transform_1d_avx2 row_txfm8x32_arr[TX_TYPES] = {
  fdct32_avx2,     // DCT_DCT
  NULL,            // ADST_DCT
  NULL,            // DCT_ADST
  NULL,            // ADST_ADST
  NULL,            // FLIPADST_DCT
  NULL,            // DCT_FLIPADST
  NULL,            // FLIPADST_FLIPADST
  NULL,            // ADST_FLIPADST
  NULL,            // FLIPADST_ADST
  idtx32x32_avx2,  // IDTX
  NULL,            // V_DCT
  NULL,            // H_DCT
  NULL,            // V_ADST
  NULL,            // H_ADST
  NULL,            // V_FLIPADST
  NULL,            // H_FLIPADST
};

static INLINE void round_shift_32_64xn_avx2(__m256i *in, int size, int8_t bit,
                                            int width) {
  if (bit < 0) {
    bit = -bit;
    const __m256i round = _mm256_set1_epi32(1 << (bit - 1));
    for (int i = 0; i < size; ++i) {
      const int idx = (i * width);
      in[idx] = _mm256_add_epi32(in[idx], round);
      in[idx] = _mm256_srai_epi32(in[idx], bit);
      in[idx + 1] = _mm256_add_epi32(in[idx + 1], round);
      in[idx + 1] = _mm256_srai_epi32(in[idx + 1], bit);
      in[idx + 2] = _mm256_add_epi32(in[idx + 2], round);
      in[idx + 2] = _mm256_srai_epi32(in[idx + 2], bit);
      in[idx + 3] = _mm256_add_epi32(in[idx + 3], round);
      in[idx + 3] = _mm256_srai_epi32(in[idx + 3], bit);
    }
  } else if (bit > 0) {
    for (int i = 0; i < size; ++i) {
      const int idx = (i * width);
      in[idx] = _mm256_slli_epi32(in[idx], bit);
      in[idx + 1] = _mm256_slli_epi32(in[idx + 1], bit);
      in[idx + 2] = _mm256_slli_epi32(in[idx + 2], bit);
      in[idx + 3] = _mm256_slli_epi32(in[idx + 3], bit);
    }
  }
}

static INLINE void load_buffer_32xn_avx2(const int16_t *input, __m256i *in,
                                         int stride, int8_t shift) {
  in[0] = _mm256_cvtepi16_epi32(
      _mm_loadu_si128((const __m128i *)(input + 0 * stride)));
  in[1] = _mm256_cvtepi16_epi32(
      _mm_loadu_si128((const __m128i *)(input + 1 * stride)));
  in[2] = _mm256_cvtepi16_epi32(
      _mm_loadu_si128((const __m128i *)(input + 2 * stride)));
  in[3] = _mm256_cvtepi16_epi32(
      _mm_loadu_si128((const __m128i *)(input + 3 * stride)));

  in[0] = _mm256_slli_epi32(in[0], shift);
  in[1] = _mm256_slli_epi32(in[1], shift);
  in[2] = _mm256_slli_epi32(in[2], shift);
  in[3] = _mm256_slli_epi32(in[3], shift);
}

void av1_fwd_txfm2d_32x32_avx2(const int16_t *input, int32_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  (void)bd;
  (void)use_ddt;
  __m256i buf0[128], buf1[128];
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_32X32];
  const int txw_idx = get_txw_idx(TX_32X32);
  const int txh_idx = get_txh_idx(TX_32X32);
  const int8_t cos_bit_col = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t cos_bit_row = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int width = tx_size_wide[TX_32X32];
  const int height = tx_size_high[TX_32X32];
  const transform_1d_avx2 col_txfm = col_txfm8x32_arr[tx_type];
  const transform_1d_avx2 row_txfm = row_txfm8x32_arr[tx_type];
  const int width_div16 = width >> 4;
  const int width_div8 = width >> 3;
  // column transform
  for (int i = 0; i < height; ++i)
    load_buffer_32xn_avx2(input + i * stride, buf0 + i * 4, 8, shift[0]);

  for (int i = 0; i < width_div16; ++i) {
    col_txfm(&buf0[i << 1], &buf0[i << 1], cos_bit_col, width_div8, width_div8);
    col_txfm(&buf0[(i << 1) + 1], &buf0[(i << 1) + 1], cos_bit_col, width_div8,
             width_div8);
  }
  round_shift_32_64xn_avx2(&buf0[0], height, shift[1], width_div8);
  for (int r = 0; r < height; r += 8) {
    for (int c = 0; c < width_div8; ++c) {
      fwd_txfm_transpose_8x8_avx2(&buf0[r * width_div8 + c],
                                  &buf1[c * 8 * width_div8 + (r >> 3)],
                                  width_div8, width_div8);
    }
  }
  // row transform
  for (int i = 0; i < width_div16; ++i) {
    row_txfm(&buf1[i << 1], &buf1[i << 1], cos_bit_row, width_div8, width_div8);
    row_txfm(&buf1[(i << 1) + 1], &buf1[(i << 1) + 1], cos_bit_row, width_div8,
             width_div8);
  }
  round_shift_32_64xn_avx2(&buf1[0], height, shift[2], width_div8);
  for (int r = 0; r < height; r += 8) {
    for (int c = 0; c < width_div8; ++c) {
      fwd_txfm_transpose_8x8_avx2(&buf1[r * width_div8 + c],
                                  &buf0[c * 8 * width_div8 + (r >> 3)],
                                  width_div8, width_div8);
    }
  }

  store_buffer_avx2(buf0, output, 8, 128);
}

static INLINE void load_buffer_32x8n(const int16_t *input, __m256i *out,
                                     int stride, int flipud, int fliplr,
                                     int8_t shift, int height) {
  (void)fliplr;
  if (flipud) {
    for (int32_t col = 0; col < height; ++col) {
      const int16_t *in = input + (height - 1 - col) * stride;
      __m256i *output = out + col * 4;
      load_buffer_32xn_avx2(in, output, 8, shift);
    }
  } else {
    for (int32_t col = 0; col < height; ++col) {
      const int16_t *in = input + col * stride;
      __m256i *output = out + col * 4;
      load_buffer_32xn_avx2(in, output, 8, shift);
    }
  }
}

static INLINE void col_txfm_16x16_rounding(__m256i *in, int8_t shift) {
  col_txfm_8x8_rounding(&in[0], shift);
  col_txfm_8x8_rounding(&in[8], shift);
  col_txfm_8x8_rounding(&in[16], shift);
  col_txfm_8x8_rounding(&in[24], shift);
}

static INLINE void transpose_8nx8n(const __m256i *input, __m256i *output,
                                   int width, int height) {
  const int32_t numcol = height >> 3;
  const int32_t numrow = width >> 3;
  __m256i out1[8];
  for (int32_t j = 0; j < numrow; ++j) {
    for (int32_t i = 0; i < numcol; ++i) {
      TRANSPOSE_4X4_AVX2(input[i * width + j + (numrow * 0)],
                         input[i * width + j + (numrow * 1)],
                         input[i * width + j + (numrow * 2)],
                         input[i * width + j + (numrow * 3)], out1[0], out1[1],
                         out1[4], out1[5]);
      TRANSPOSE_4X4_AVX2(input[i * width + j + (numrow * 4)],
                         input[i * width + j + (numrow * 5)],
                         input[i * width + j + (numrow * 6)],
                         input[i * width + j + (numrow * 7)], out1[2], out1[3],
                         out1[6], out1[7]);
      output[j * height + i + (numcol * 0)] =
          _mm256_permute2x128_si256(out1[0], out1[2], 0x20);
      output[j * height + i + (numcol * 1)] =
          _mm256_permute2x128_si256(out1[1], out1[3], 0x20);
      output[j * height + i + (numcol * 2)] =
          _mm256_permute2x128_si256(out1[4], out1[6], 0x20);
      output[j * height + i + (numcol * 3)] =
          _mm256_permute2x128_si256(out1[5], out1[7], 0x20);
      output[j * height + i + (numcol * 4)] =
          _mm256_permute2x128_si256(out1[0], out1[2], 0x31);
      output[j * height + i + (numcol * 5)] =
          _mm256_permute2x128_si256(out1[1], out1[3], 0x31);
      output[j * height + i + (numcol * 6)] =
          _mm256_permute2x128_si256(out1[4], out1[6], 0x31);
      output[j * height + i + (numcol * 7)] =
          _mm256_permute2x128_si256(out1[5], out1[7], 0x31);
    }
  }
}

void av1_fwd_txfm2d_32x16_avx2(const int16_t *input, int32_t *coeff, int stride,
                               const TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[64];
  __m256i *outcoef128 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_32X16];
  const int txw_idx = get_txw_idx(TX_32X16);
  const int txh_idx = get_txh_idx(TX_32X16);
  const transform_1d_avx2 col_txfm =
      (use_ddt && REPLACE_ADST16 &&
       (vtx_tab[tx_type] == ADST_1D || vtx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm16_arr[DDT_1D]
          : highbd_txfm16_arr[vtx_tab[tx_type]];
  const transform_1d_avx2 row_txfm = highbd_txfm32_arr[htx_tab[tx_type]];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int txfm_size_col = tx_size_wide[TX_32X16];
  const int txfm_size_row = tx_size_high[TX_32X16];
  const int num_row = txfm_size_row >> 3;
  const int num_col = txfm_size_col >> 3;
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_32x8n(input, in, stride, ud_flip, lr_flip, shift[0],
                    txfm_size_row);
  col_txfm(in, in, bitcol, num_col, num_col);
  col_txfm_16x16_rounding(&in[0], -shift[1]);
  col_txfm_16x16_rounding(&in[32], -shift[1]);
  transpose_8nx8n(in, outcoef128, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    row_txfm(outcoef128 + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoef128, txfm_size_row, txfm_size_col);
  av1_round_shift_rect_array_32_avx2(outcoef128, outcoef128, 64, -shift[2],
                                     NewSqrt2);
}

void av1_fwd_txfm2d_16x32_avx2(const int16_t *input, int32_t *coeff, int stride,
                               const TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[64];
  __m256i *outcoef256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_16X32];
  const int txw_idx = get_txw_idx(TX_16X32);
  const int txh_idx = get_txh_idx(TX_16X32);
  const int8_t cos_bit_col = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t cos_bit_row = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int txfm_size_col = tx_size_wide[TX_16X32];
  const int txfm_size_row = tx_size_high[TX_16X32];
  const transform_1d_avx2 col_txfm = highbd_txfm32_arr[vtx_tab[tx_type]];
  const transform_1d_avx2 row_txfm =
      (use_ddt && REPLACE_ADST16 &&
       (htx_tab[tx_type] == ADST_1D || htx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm16_arr[DDT_1D]
          : highbd_txfm16_arr[htx_tab[tx_type]];
  const int width_div8 = (txfm_size_col >> 3);
  const int height_div8 = (txfm_size_row >> 3);
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_16x16(input, in, stride, ud_flip, lr_flip, shift[0]);
  load_buffer_16x16(input + 16 * stride, in + 32, stride, ud_flip, lr_flip,
                    shift[0]);
  col_txfm(&in[0], &in[0], cos_bit_col, width_div8, width_div8);
  col_txfm(&in[1], &in[1], cos_bit_col, width_div8, width_div8);
  col_txfm_16x16_rounding(&in[0], -shift[1]);
  col_txfm_16x16_rounding(&in[32], -shift[1]);
  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  row_txfm(outcoef256, in, cos_bit_row, height_div8, height_div8);
  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col);
  av1_round_shift_rect_array_32_avx2(outcoef256, outcoef256, 64, -shift[2],
                                     NewSqrt2);
}

void av1_fwd_txfm2d_32x8_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[32];
  __m256i *outcoef256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_32X8];
  const int txw_idx = get_txw_idx(TX_32X8);
  const int txh_idx = get_txh_idx(TX_32X8);
  const transform_1d_avx2 col_txfm =
      (use_ddt && REPLACE_ADST16 &&
       (vtx_tab[tx_type] == ADST_1D || vtx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm8_arr[DDT_1D]
          : highbd_txfm8_arr[vtx_tab[tx_type]];
  const transform_1d_avx2 row_txfm = highbd_txfm32_arr[htx_tab[tx_type]];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int txfm_size_col = tx_size_wide[TX_32X8];
  const int txfm_size_row = tx_size_high[TX_32X8];
  const int num_row = txfm_size_row >> 3;
  const int num_col = txfm_size_col >> 3;
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_32x8n(input, in, stride, ud_flip, lr_flip, shift[0],
                    txfm_size_row);
  col_txfm(in, in, bitcol, num_col, num_col);
  col_txfm_16x16_rounding(&in[0], -shift[1]);
  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    row_txfm(outcoef256 + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col);
}

void av1_fwd_txfm2d_8x32_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[32];
  __m256i *outcoef256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_8X32];
  const int txw_idx = get_txw_idx(TX_8X32);
  const int txh_idx = get_txh_idx(TX_8X32);
  const transform_1d_avx2 col_txfm = highbd_txfm32_arr[vtx_tab[tx_type]];
  const transform_1d_avx2 row_txfm =
      (use_ddt && REPLACE_ADST8 &&
       (htx_tab[tx_type] == ADST_1D || htx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm8_arr[DDT_1D]
          : highbd_txfm8_arr[htx_tab[tx_type]];
  const int txfm_size_col = tx_size_wide[TX_8X32];
  const int txfm_size_row = tx_size_high[TX_8X32];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int num_row = txfm_size_row >> 3;
  const int num_col = txfm_size_col >> 3;
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_8x16_avx2(input, in, stride, ud_flip, lr_flip, shift[0]);
  load_buffer_8x16_avx2(input + (txfm_size_row >> 1) * stride, in + 16, stride,
                        ud_flip, lr_flip, shift[0]);
  col_txfm(in, in, bitcol, num_col, num_col);
  col_txfm_16x16_rounding(in, -shift[1]);
  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  row_txfm(outcoef256, in, bitrow, num_row, num_row);
  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col);
}

static INLINE void fdct64_stage2_avx2(__m256i *x1, __m256i *x2,
                                      __m256i *cospi_m32, __m256i *cospi_p32,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  x2[0] = _mm256_add_epi32(x1[0], x1[31]);
  x2[31] = _mm256_sub_epi32(x1[0], x1[31]);
  x2[1] = _mm256_add_epi32(x1[1], x1[30]);
  x2[30] = _mm256_sub_epi32(x1[1], x1[30]);
  x2[2] = _mm256_add_epi32(x1[2], x1[29]);
  x2[29] = _mm256_sub_epi32(x1[2], x1[29]);
  x2[3] = _mm256_add_epi32(x1[3], x1[28]);
  x2[28] = _mm256_sub_epi32(x1[3], x1[28]);
  x2[4] = _mm256_add_epi32(x1[4], x1[27]);
  x2[27] = _mm256_sub_epi32(x1[4], x1[27]);
  x2[5] = _mm256_add_epi32(x1[5], x1[26]);
  x2[26] = _mm256_sub_epi32(x1[5], x1[26]);
  x2[6] = _mm256_add_epi32(x1[6], x1[25]);
  x2[25] = _mm256_sub_epi32(x1[6], x1[25]);
  x2[7] = _mm256_add_epi32(x1[7], x1[24]);
  x2[24] = _mm256_sub_epi32(x1[7], x1[24]);
  x2[8] = _mm256_add_epi32(x1[8], x1[23]);
  x2[23] = _mm256_sub_epi32(x1[8], x1[23]);
  x2[9] = _mm256_add_epi32(x1[9], x1[22]);
  x2[22] = _mm256_sub_epi32(x1[9], x1[22]);
  x2[10] = _mm256_add_epi32(x1[10], x1[21]);
  x2[21] = _mm256_sub_epi32(x1[10], x1[21]);
  x2[11] = _mm256_add_epi32(x1[11], x1[20]);
  x2[20] = _mm256_sub_epi32(x1[11], x1[20]);
  x2[12] = _mm256_add_epi32(x1[12], x1[19]);
  x2[19] = _mm256_sub_epi32(x1[12], x1[19]);
  x2[13] = _mm256_add_epi32(x1[13], x1[18]);
  x2[18] = _mm256_sub_epi32(x1[13], x1[18]);
  x2[14] = _mm256_add_epi32(x1[14], x1[17]);
  x2[17] = _mm256_sub_epi32(x1[14], x1[17]);
  x2[15] = _mm256_add_epi32(x1[15], x1[16]);
  x2[16] = _mm256_sub_epi32(x1[15], x1[16]);

  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[40], x1[55], x2[40], x2[55],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[41], x1[54], x2[41], x2[54],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[42], x1[53], x2[42], x2[53],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[43], x1[52], x2[43], x2[52],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[44], x1[51], x2[44], x2[51],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[45], x1[50], x2[45], x2[50],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[46], x1[49], x2[46], x2[49],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x1[47], x1[48], x2[47], x2[48],
                        *__rounding, cos_bit);
}
static INLINE void fdct64_stage3_avx2(__m256i *x2, __m256i *x3,
                                      __m256i *cospi_m32, __m256i *cospi_p32,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  x3[0] = _mm256_add_epi32(x2[0], x2[15]);
  x3[15] = _mm256_sub_epi32(x2[0], x2[15]);
  x3[1] = _mm256_add_epi32(x2[1], x2[14]);
  x3[14] = _mm256_sub_epi32(x2[1], x2[14]);
  x3[2] = _mm256_add_epi32(x2[2], x2[13]);
  x3[13] = _mm256_sub_epi32(x2[2], x2[13]);
  x3[3] = _mm256_add_epi32(x2[3], x2[12]);
  x3[12] = _mm256_sub_epi32(x2[3], x2[12]);
  x3[4] = _mm256_add_epi32(x2[4], x2[11]);
  x3[11] = _mm256_sub_epi32(x2[4], x2[11]);
  x3[5] = _mm256_add_epi32(x2[5], x2[10]);
  x3[10] = _mm256_sub_epi32(x2[5], x2[10]);
  x3[6] = _mm256_add_epi32(x2[6], x2[9]);
  x3[9] = _mm256_sub_epi32(x2[6], x2[9]);
  x3[7] = _mm256_add_epi32(x2[7], x2[8]);
  x3[8] = _mm256_sub_epi32(x2[7], x2[8]);

  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x2[20], x2[27], x3[20], x3[27],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x2[21], x2[26], x3[21], x3[26],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x2[22], x2[25], x3[22], x3[25],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x2[23], x2[24], x3[23], x3[24],
                        *__rounding, cos_bit);

  x3[47] = _mm256_sub_epi32(x3[32], x2[47]);
  x3[32] = _mm256_add_epi32(x3[32], x2[47]);
  x3[46] = _mm256_sub_epi32(x3[33], x2[46]);
  x3[33] = _mm256_add_epi32(x3[33], x2[46]);
  x3[45] = _mm256_sub_epi32(x3[34], x2[45]);
  x3[34] = _mm256_add_epi32(x3[34], x2[45]);
  x3[44] = _mm256_sub_epi32(x3[35], x2[44]);
  x3[35] = _mm256_add_epi32(x3[35], x2[44]);
  x3[43] = _mm256_sub_epi32(x3[36], x2[43]);
  x3[36] = _mm256_add_epi32(x3[36], x2[43]);
  x3[42] = _mm256_sub_epi32(x3[37], x2[42]);
  x3[37] = _mm256_add_epi32(x3[37], x2[42]);
  x3[41] = _mm256_sub_epi32(x3[38], x2[41]);
  x3[38] = _mm256_add_epi32(x3[38], x2[41]);
  x3[40] = _mm256_sub_epi32(x3[39], x2[40]);
  x3[39] = _mm256_add_epi32(x3[39], x2[40]);
  x3[48] = _mm256_sub_epi32(x3[63], x2[48]);
  x3[63] = _mm256_add_epi32(x3[63], x2[48]);
  x3[49] = _mm256_sub_epi32(x3[62], x2[49]);
  x3[62] = _mm256_add_epi32(x3[62], x2[49]);
  x3[50] = _mm256_sub_epi32(x3[61], x2[50]);
  x3[61] = _mm256_add_epi32(x3[61], x2[50]);
  x3[51] = _mm256_sub_epi32(x3[60], x2[51]);
  x3[60] = _mm256_add_epi32(x3[60], x2[51]);
  x3[52] = _mm256_sub_epi32(x3[59], x2[52]);
  x3[59] = _mm256_add_epi32(x3[59], x2[52]);
  x3[53] = _mm256_sub_epi32(x3[58], x2[53]);
  x3[58] = _mm256_add_epi32(x3[58], x2[53]);
  x3[54] = _mm256_sub_epi32(x3[57], x2[54]);
  x3[57] = _mm256_add_epi32(x3[57], x2[54]);
  x3[55] = _mm256_sub_epi32(x3[56], x2[55]);
  x3[56] = _mm256_add_epi32(x3[56], x2[55]);
}
static INLINE void fdct64_stage4_avx2(__m256i *x3, __m256i *x4,
                                      __m256i *cospi_m32, __m256i *cospi_p32,
                                      __m256i *cospi_m16, __m256i *cospi_p48,
                                      __m256i *cospi_m48,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  x4[0] = _mm256_add_epi32(x3[0], x3[7]);
  x4[7] = _mm256_sub_epi32(x3[0], x3[7]);
  x4[1] = _mm256_add_epi32(x3[1], x3[6]);
  x4[6] = _mm256_sub_epi32(x3[1], x3[6]);
  x4[2] = _mm256_add_epi32(x3[2], x3[5]);
  x4[5] = _mm256_sub_epi32(x3[2], x3[5]);
  x4[3] = _mm256_add_epi32(x3[3], x3[4]);
  x4[4] = _mm256_sub_epi32(x3[3], x3[4]);

  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x3[10], x3[13], x4[10], x4[13],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x3[11], x3[12], x4[11], x4[12],
                        *__rounding, cos_bit);

  x4[23] = _mm256_sub_epi32(x4[16], x3[23]);
  x4[16] = _mm256_add_epi32(x4[16], x3[23]);
  x4[22] = _mm256_sub_epi32(x4[17], x3[22]);
  x4[17] = _mm256_add_epi32(x4[17], x3[22]);
  x4[21] = _mm256_sub_epi32(x4[18], x3[21]);
  x4[18] = _mm256_add_epi32(x4[18], x3[21]);
  x4[20] = _mm256_sub_epi32(x4[19], x3[20]);
  x4[19] = _mm256_add_epi32(x4[19], x3[20]);
  x4[24] = _mm256_sub_epi32(x4[31], x3[24]);
  x4[31] = _mm256_add_epi32(x4[31], x3[24]);
  x4[25] = _mm256_sub_epi32(x4[30], x3[25]);
  x4[30] = _mm256_add_epi32(x4[30], x3[25]);
  x4[26] = _mm256_sub_epi32(x4[29], x3[26]);
  x4[29] = _mm256_add_epi32(x4[29], x3[26]);
  x4[27] = _mm256_sub_epi32(x4[28], x3[27]);
  x4[28] = _mm256_add_epi32(x4[28], x3[27]);

  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x3[36], x3[59], x4[36], x4[59],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x3[37], x3[58], x4[37], x4[58],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x3[38], x3[57], x4[38], x4[57],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x3[39], x3[56], x4[39], x4[56],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x3[40], x3[55], x4[40], x4[55],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x3[41], x3[54], x4[41], x4[54],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x3[42], x3[53], x4[42], x4[53],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x3[43], x3[52], x4[43], x4[52],
                        *__rounding, cos_bit);
}
static INLINE void fdct64_stage5_avx2(__m256i *x4, __m256i *x5,
                                      __m256i *cospi_m32, __m256i *cospi_p32,
                                      __m256i *cospi_m16, __m256i *cospi_p48,
                                      __m256i *cospi_m48,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  x5[0] = _mm256_add_epi32(x4[0], x4[3]);
  x5[3] = _mm256_sub_epi32(x4[0], x4[3]);
  x5[1] = _mm256_add_epi32(x4[1], x4[2]);
  x5[2] = _mm256_sub_epi32(x4[1], x4[2]);

  btf_32_type0_avx2_new(*cospi_m32, *cospi_p32, x4[5], x4[6], x5[5], x5[6],
                        *__rounding, cos_bit);

  x5[11] = _mm256_sub_epi32(x5[8], x4[11]);
  x5[8] = _mm256_add_epi32(x5[8], x4[11]);
  x5[10] = _mm256_sub_epi32(x5[9], x4[10]);
  x5[9] = _mm256_add_epi32(x5[9], x4[10]);
  x5[12] = _mm256_sub_epi32(x5[15], x4[12]);
  x5[15] = _mm256_add_epi32(x5[15], x4[12]);
  x5[13] = _mm256_sub_epi32(x5[14], x4[13]);
  x5[14] = _mm256_add_epi32(x5[14], x4[13]);

  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x4[18], x4[29], x5[18], x5[29],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x4[19], x4[28], x5[19], x5[28],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x4[20], x4[27], x5[20], x5[27],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x4[21], x4[26], x5[21], x5[26],
                        *__rounding, cos_bit);

  x5[39] = _mm256_sub_epi32(x5[32], x4[39]);
  x5[32] = _mm256_add_epi32(x5[32], x4[39]);
  x5[38] = _mm256_sub_epi32(x5[33], x4[38]);
  x5[33] = _mm256_add_epi32(x5[33], x4[38]);
  x5[37] = _mm256_sub_epi32(x5[34], x4[37]);
  x5[34] = _mm256_add_epi32(x5[34], x4[37]);
  x5[36] = _mm256_sub_epi32(x5[35], x4[36]);
  x5[35] = _mm256_add_epi32(x5[35], x4[36]);
  x5[40] = _mm256_sub_epi32(x5[47], x4[40]);
  x5[47] = _mm256_add_epi32(x5[47], x4[40]);
  x5[41] = _mm256_sub_epi32(x5[46], x4[41]);
  x5[46] = _mm256_add_epi32(x5[46], x4[41]);
  x5[42] = _mm256_sub_epi32(x5[45], x4[42]);
  x5[45] = _mm256_add_epi32(x5[45], x4[42]);
  x5[43] = _mm256_sub_epi32(x5[44], x4[43]);
  x5[44] = _mm256_add_epi32(x5[44], x4[43]);
  x5[55] = _mm256_sub_epi32(x5[48], x4[55]);
  x5[48] = _mm256_add_epi32(x5[48], x4[55]);
  x5[54] = _mm256_sub_epi32(x5[49], x4[54]);
  x5[49] = _mm256_add_epi32(x5[49], x4[54]);
  x5[53] = _mm256_sub_epi32(x5[50], x4[53]);
  x5[50] = _mm256_add_epi32(x5[50], x4[53]);
  x5[52] = _mm256_sub_epi32(x5[51], x4[52]);
  x5[51] = _mm256_add_epi32(x5[51], x4[52]);
  x5[56] = _mm256_sub_epi32(x5[63], x4[56]);
  x5[63] = _mm256_add_epi32(x5[63], x4[56]);
  x5[57] = _mm256_sub_epi32(x5[62], x4[57]);
  x5[62] = _mm256_add_epi32(x5[62], x4[57]);
  x5[58] = _mm256_sub_epi32(x5[61], x4[58]);
  x5[61] = _mm256_add_epi32(x5[61], x4[58]);
  x5[59] = _mm256_sub_epi32(x5[60], x4[59]);
  x5[60] = _mm256_add_epi32(x5[60], x4[59]);
}
static INLINE void fdct64_stage6_avx2(
    __m256i *x5, __m256i *x6, __m256i *cospi_p16, __m256i *cospi_p32,
    __m256i *cospi_m16, __m256i *cospi_p48, __m256i *cospi_m48,
    __m256i *cospi_m08, __m256i *cospi_p56, __m256i *cospi_m56,
    __m256i *cospi_m40, __m256i *cospi_p24, __m256i *cospi_m24,
    const __m256i *__rounding, int8_t cos_bit) {
  btf_32_type0_avx2_new(*cospi_p32, *cospi_p32, x5[0], x5[1], x6[0], x6[1],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_p16, *cospi_p48, x5[3], x5[2], x6[2], x6[3],
                        *__rounding, cos_bit);
  x6[5] = _mm256_sub_epi32(x6[4], x5[5]);
  x6[4] = _mm256_add_epi32(x6[4], x5[5]);
  x6[6] = _mm256_sub_epi32(x6[7], x5[6]);
  x6[7] = _mm256_add_epi32(x6[7], x5[6]);

  btf_32_type0_avx2_new(*cospi_m16, *cospi_p48, x5[9], x5[14], x6[9], x6[14],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m48, *cospi_m16, x5[10], x5[13], x6[10], x6[13],
                        *__rounding, cos_bit);

  x6[19] = _mm256_sub_epi32(x6[16], x5[19]);
  x6[16] = _mm256_add_epi32(x6[16], x5[19]);
  x6[18] = _mm256_sub_epi32(x6[17], x5[18]);
  x6[17] = _mm256_add_epi32(x6[17], x5[18]);
  x6[20] = _mm256_sub_epi32(x6[23], x5[20]);
  x6[23] = _mm256_add_epi32(x6[23], x5[20]);
  x6[21] = _mm256_sub_epi32(x6[22], x5[21]);
  x6[22] = _mm256_add_epi32(x6[22], x5[21]);
  x6[27] = _mm256_sub_epi32(x6[24], x5[27]);
  x6[24] = _mm256_add_epi32(x6[24], x5[27]);
  x6[26] = _mm256_sub_epi32(x6[25], x5[26]);
  x6[25] = _mm256_add_epi32(x6[25], x5[26]);
  x6[28] = _mm256_sub_epi32(x6[31], x5[28]);
  x6[31] = _mm256_add_epi32(x6[31], x5[28]);
  x6[29] = _mm256_sub_epi32(x6[30], x5[29]);
  x6[30] = _mm256_add_epi32(x6[30], x5[29]);

  btf_32_type0_avx2_new(*cospi_m08, *cospi_p56, x5[34], x5[61], x6[34], x6[61],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m08, *cospi_p56, x5[35], x5[60], x6[35], x6[60],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m56, *cospi_m08, x5[36], x5[59], x6[36], x6[59],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m56, *cospi_m08, x5[37], x5[58], x6[37], x6[58],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m40, *cospi_p24, x5[42], x5[53], x6[42], x6[53],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m40, *cospi_p24, x5[43], x5[52], x6[43], x6[52],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m24, *cospi_m40, x5[44], x5[51], x6[44], x6[51],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m24, *cospi_m40, x5[45], x5[50], x6[45], x6[50],
                        *__rounding, cos_bit);
}
static INLINE void fdct64_stage7_avx2(__m256i *x6, __m256i *x7,
                                      __m256i *cospi_p08, __m256i *cospi_p56,
                                      __m256i *cospi_p40, __m256i *cospi_p24,
                                      __m256i *cospi_m08, __m256i *cospi_m56,
                                      __m256i *cospi_m40, __m256i *cospi_m24,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  btf_32_type0_avx2_new(*cospi_p08, *cospi_p56, x6[7], x6[4], x7[4], x7[7],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_p40, *cospi_p24, x6[6], x6[5], x7[5], x7[6],
                        *__rounding, cos_bit);
  x7[9] = _mm256_sub_epi32(x7[8], x6[9]);
  x7[8] = _mm256_add_epi32(x7[8], x6[9]);
  x7[10] = _mm256_sub_epi32(x7[11], x6[10]);
  x7[11] = _mm256_add_epi32(x7[11], x6[10]);
  x7[13] = _mm256_sub_epi32(x7[12], x6[13]);
  x7[12] = _mm256_add_epi32(x7[12], x6[13]);
  x7[14] = _mm256_sub_epi32(x7[15], x6[14]);
  x7[15] = _mm256_add_epi32(x7[15], x6[14]);

  btf_32_type0_avx2_new(*cospi_m08, *cospi_p56, x6[17], x6[30], x7[17], x7[30],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m56, *cospi_m08, x6[18], x6[29], x7[18], x7[29],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m40, *cospi_p24, x6[21], x6[26], x7[21], x7[26],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(*cospi_m24, *cospi_m40, x6[22], x6[25], x7[22], x7[25],
                        *__rounding, cos_bit);

  x7[35] = _mm256_sub_epi32(x7[32], x6[35]);
  x7[32] = _mm256_add_epi32(x7[32], x6[35]);
  x7[34] = _mm256_sub_epi32(x7[33], x6[34]);
  x7[33] = _mm256_add_epi32(x7[33], x6[34]);
  x7[36] = _mm256_sub_epi32(x7[39], x6[36]);
  x7[39] = _mm256_add_epi32(x7[39], x6[36]);
  x7[37] = _mm256_sub_epi32(x7[38], x6[37]);
  x7[38] = _mm256_add_epi32(x7[38], x6[37]);
  x7[43] = _mm256_sub_epi32(x7[40], x6[43]);
  x7[40] = _mm256_add_epi32(x7[40], x6[43]);
  x7[42] = _mm256_sub_epi32(x7[41], x6[42]);
  x7[41] = _mm256_add_epi32(x7[41], x6[42]);
  x7[44] = _mm256_sub_epi32(x7[47], x6[44]);
  x7[47] = _mm256_add_epi32(x7[47], x6[44]);
  x7[45] = _mm256_sub_epi32(x7[46], x6[45]);
  x7[46] = _mm256_add_epi32(x7[46], x6[45]);
  x7[51] = _mm256_sub_epi32(x7[48], x6[51]);
  x7[48] = _mm256_add_epi32(x7[48], x6[51]);
  x7[50] = _mm256_sub_epi32(x7[49], x6[50]);
  x7[49] = _mm256_add_epi32(x7[49], x6[50]);
  x7[52] = _mm256_sub_epi32(x7[55], x6[52]);
  x7[55] = _mm256_add_epi32(x7[55], x6[52]);
  x7[53] = _mm256_sub_epi32(x7[54], x6[53]);
  x7[54] = _mm256_add_epi32(x7[54], x6[53]);
  x7[59] = _mm256_sub_epi32(x7[56], x6[59]);
  x7[56] = _mm256_add_epi32(x7[56], x6[59]);
  x7[58] = _mm256_sub_epi32(x7[57], x6[58]);
  x7[57] = _mm256_add_epi32(x7[57], x6[58]);
  x7[60] = _mm256_sub_epi32(x7[63], x6[60]);
  x7[63] = _mm256_add_epi32(x7[63], x6[60]);
  x7[61] = _mm256_sub_epi32(x7[62], x6[61]);
  x7[62] = _mm256_add_epi32(x7[62], x6[61]);
}
static INLINE void fdct64_stage8_avx2(__m256i *x7, __m256i *x8,
                                      const int32_t *cospi,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  __m256i cospi_p60 = _mm256_set1_epi32(cospi[60]);
  __m256i cospi_p04 = _mm256_set1_epi32(cospi[4]);
  __m256i cospi_p28 = _mm256_set1_epi32(cospi[28]);
  __m256i cospi_p36 = _mm256_set1_epi32(cospi[36]);
  __m256i cospi_p44 = _mm256_set1_epi32(cospi[44]);
  __m256i cospi_p20 = _mm256_set1_epi32(cospi[20]);
  __m256i cospi_p12 = _mm256_set1_epi32(cospi[12]);
  __m256i cospi_p52 = _mm256_set1_epi32(cospi[52]);
  __m256i cospi_m04 = _mm256_set1_epi32(-cospi[4]);
  __m256i cospi_m60 = _mm256_set1_epi32(-cospi[60]);
  __m256i cospi_m36 = _mm256_set1_epi32(-cospi[36]);
  __m256i cospi_m28 = _mm256_set1_epi32(-cospi[28]);
  __m256i cospi_m20 = _mm256_set1_epi32(-cospi[20]);
  __m256i cospi_m44 = _mm256_set1_epi32(-cospi[44]);
  __m256i cospi_m52 = _mm256_set1_epi32(-cospi[52]);
  __m256i cospi_m12 = _mm256_set1_epi32(-cospi[12]);

  btf_32_type0_avx2_new(cospi_p04, cospi_p60, x7[15], x7[8], x8[8], x8[15],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p36, cospi_p28, x7[14], x7[9], x8[9], x8[14],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p20, cospi_p44, x7[13], x7[10], x8[10], x8[13],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p52, cospi_p12, x7[12], x7[11], x8[11], x8[12],
                        *__rounding, cos_bit);
  x8[17] = _mm256_sub_epi32(x8[16], x7[17]);
  x8[16] = _mm256_add_epi32(x8[16], x7[17]);
  x8[18] = _mm256_sub_epi32(x8[19], x7[18]);
  x8[19] = _mm256_add_epi32(x8[19], x7[18]);
  x8[21] = _mm256_sub_epi32(x8[20], x7[21]);
  x8[20] = _mm256_add_epi32(x8[20], x7[21]);
  x8[22] = _mm256_sub_epi32(x8[23], x7[22]);
  x8[23] = _mm256_add_epi32(x8[23], x7[22]);
  x8[25] = _mm256_sub_epi32(x8[24], x7[25]);
  x8[24] = _mm256_add_epi32(x8[24], x7[25]);
  x8[26] = _mm256_sub_epi32(x8[27], x7[26]);
  x8[27] = _mm256_add_epi32(x8[27], x7[26]);
  x8[29] = _mm256_sub_epi32(x8[28], x7[29]);
  x8[28] = _mm256_add_epi32(x8[28], x7[29]);
  x8[30] = _mm256_sub_epi32(x8[31], x7[30]);
  x8[31] = _mm256_add_epi32(x8[31], x7[30]);

  btf_32_type0_avx2_new(cospi_m04, cospi_p60, x7[33], x7[62], x8[33], x8[62],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m60, cospi_m04, x7[34], x7[61], x8[34], x8[61],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m36, cospi_p28, x7[37], x7[58], x8[37], x8[58],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m28, cospi_m36, x7[38], x7[57], x8[38], x8[57],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m20, cospi_p44, x7[41], x7[54], x8[41], x8[54],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m44, cospi_m20, x7[42], x7[53], x8[42], x8[53],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m52, cospi_p12, x7[45], x7[50], x8[45], x8[50],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_m12, cospi_m52, x7[46], x7[49], x8[46], x8[49],
                        *__rounding, cos_bit);
}
static INLINE void fdct64_stage9_avx2(__m256i *x8, __m256i *x9,
                                      const int32_t *cospi,
                                      const __m256i *__rounding,
                                      int8_t cos_bit) {
  __m256i cospi_p62 = _mm256_set1_epi32(cospi[62]);
  __m256i cospi_p02 = _mm256_set1_epi32(cospi[2]);
  __m256i cospi_p30 = _mm256_set1_epi32(cospi[30]);
  __m256i cospi_p34 = _mm256_set1_epi32(cospi[34]);
  __m256i cospi_p46 = _mm256_set1_epi32(cospi[46]);
  __m256i cospi_p18 = _mm256_set1_epi32(cospi[18]);
  __m256i cospi_p14 = _mm256_set1_epi32(cospi[14]);
  __m256i cospi_p50 = _mm256_set1_epi32(cospi[50]);
  __m256i cospi_p54 = _mm256_set1_epi32(cospi[54]);
  __m256i cospi_p10 = _mm256_set1_epi32(cospi[10]);
  __m256i cospi_p22 = _mm256_set1_epi32(cospi[22]);
  __m256i cospi_p42 = _mm256_set1_epi32(cospi[42]);
  __m256i cospi_p38 = _mm256_set1_epi32(cospi[38]);
  __m256i cospi_p26 = _mm256_set1_epi32(cospi[26]);
  __m256i cospi_p06 = _mm256_set1_epi32(cospi[6]);
  __m256i cospi_p58 = _mm256_set1_epi32(cospi[58]);

  btf_32_type0_avx2_new(cospi_p02, cospi_p62, x8[31], x8[16], x9[16], x9[31],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p34, cospi_p30, x8[30], x8[17], x9[17], x9[30],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p18, cospi_p46, x8[29], x8[18], x9[18], x9[29],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p50, cospi_p14, x8[28], x8[19], x9[19], x9[28],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p10, cospi_p54, x8[27], x8[20], x9[20], x9[27],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p42, cospi_p22, x8[26], x8[21], x9[21], x9[26],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p26, cospi_p38, x8[25], x8[22], x9[22], x9[25],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p58, cospi_p06, x8[24], x8[23], x9[23], x9[24],
                        *__rounding, cos_bit);
  x9[33] = _mm256_sub_epi32(x9[32], x8[33]);
  x9[32] = _mm256_add_epi32(x9[32], x8[33]);
  x9[34] = _mm256_sub_epi32(x9[35], x8[34]);
  x9[35] = _mm256_add_epi32(x9[35], x8[34]);
  x9[37] = _mm256_sub_epi32(x9[36], x8[37]);
  x9[36] = _mm256_add_epi32(x9[36], x8[37]);
  x9[38] = _mm256_sub_epi32(x9[39], x8[38]);
  x9[39] = _mm256_add_epi32(x9[39], x8[38]);
  x9[41] = _mm256_sub_epi32(x9[40], x8[41]);
  x9[40] = _mm256_add_epi32(x9[40], x8[41]);
  x9[42] = _mm256_sub_epi32(x9[43], x8[42]);
  x9[43] = _mm256_add_epi32(x9[43], x8[42]);
  x9[45] = _mm256_sub_epi32(x9[44], x8[45]);
  x9[44] = _mm256_add_epi32(x9[44], x8[45]);
  x9[46] = _mm256_sub_epi32(x9[47], x8[46]);
  x9[47] = _mm256_add_epi32(x9[47], x8[46]);
  x9[49] = _mm256_sub_epi32(x9[48], x8[49]);
  x9[48] = _mm256_add_epi32(x9[48], x8[49]);
  x9[50] = _mm256_sub_epi32(x9[51], x8[50]);
  x9[51] = _mm256_add_epi32(x9[51], x8[50]);
  x9[53] = _mm256_sub_epi32(x9[52], x8[53]);
  x9[52] = _mm256_add_epi32(x9[52], x8[53]);
  x9[54] = _mm256_sub_epi32(x9[55], x8[54]);
  x9[55] = _mm256_add_epi32(x9[55], x8[54]);
  x9[57] = _mm256_sub_epi32(x9[56], x8[57]);
  x9[56] = _mm256_add_epi32(x9[56], x8[57]);
  x9[58] = _mm256_sub_epi32(x9[59], x8[58]);
  x9[59] = _mm256_add_epi32(x9[59], x8[58]);
  x9[61] = _mm256_sub_epi32(x9[60], x8[61]);
  x9[60] = _mm256_add_epi32(x9[60], x8[61]);
  x9[62] = _mm256_sub_epi32(x9[63], x8[62]);
  x9[63] = _mm256_add_epi32(x9[63], x8[62]);
}
static INLINE void fdct64_stage10_avx2(__m256i *x9, __m256i *x10,
                                       const int32_t *cospi,
                                       const __m256i *__rounding,
                                       int8_t cos_bit) {
  __m256i cospi_p63 = _mm256_set1_epi32(cospi[63]);
  __m256i cospi_p01 = _mm256_set1_epi32(cospi[1]);
  __m256i cospi_p31 = _mm256_set1_epi32(cospi[31]);
  __m256i cospi_p33 = _mm256_set1_epi32(cospi[33]);
  __m256i cospi_p47 = _mm256_set1_epi32(cospi[47]);
  __m256i cospi_p17 = _mm256_set1_epi32(cospi[17]);
  __m256i cospi_p15 = _mm256_set1_epi32(cospi[15]);
  __m256i cospi_p49 = _mm256_set1_epi32(cospi[49]);
  __m256i cospi_p55 = _mm256_set1_epi32(cospi[55]);
  __m256i cospi_p09 = _mm256_set1_epi32(cospi[9]);
  __m256i cospi_p23 = _mm256_set1_epi32(cospi[23]);
  __m256i cospi_p41 = _mm256_set1_epi32(cospi[41]);
  __m256i cospi_p39 = _mm256_set1_epi32(cospi[39]);
  __m256i cospi_p25 = _mm256_set1_epi32(cospi[25]);
  __m256i cospi_p07 = _mm256_set1_epi32(cospi[7]);
  __m256i cospi_p57 = _mm256_set1_epi32(cospi[57]);
  __m256i cospi_p59 = _mm256_set1_epi32(cospi[59]);
  __m256i cospi_p05 = _mm256_set1_epi32(cospi[5]);
  __m256i cospi_p27 = _mm256_set1_epi32(cospi[27]);
  __m256i cospi_p37 = _mm256_set1_epi32(cospi[37]);
  __m256i cospi_p43 = _mm256_set1_epi32(cospi[43]);
  __m256i cospi_p21 = _mm256_set1_epi32(cospi[21]);
  __m256i cospi_p11 = _mm256_set1_epi32(cospi[11]);
  __m256i cospi_p53 = _mm256_set1_epi32(cospi[53]);
  __m256i cospi_p51 = _mm256_set1_epi32(cospi[51]);
  __m256i cospi_p13 = _mm256_set1_epi32(cospi[13]);
  __m256i cospi_p19 = _mm256_set1_epi32(cospi[19]);
  __m256i cospi_p45 = _mm256_set1_epi32(cospi[45]);
  __m256i cospi_p35 = _mm256_set1_epi32(cospi[35]);
  __m256i cospi_p29 = _mm256_set1_epi32(cospi[29]);
  __m256i cospi_p03 = _mm256_set1_epi32(cospi[3]);
  __m256i cospi_p61 = _mm256_set1_epi32(cospi[61]);

  btf_32_type0_avx2_new(cospi_p01, cospi_p63, x9[63], x9[32], x10[32], x10[63],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p33, cospi_p31, x9[62], x9[33], x10[33], x10[62],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p17, cospi_p47, x9[61], x9[34], x10[34], x10[61],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p49, cospi_p15, x9[60], x9[35], x10[35], x10[60],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p09, cospi_p55, x9[59], x9[36], x10[36], x10[59],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p41, cospi_p23, x9[58], x9[37], x10[37], x10[58],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p25, cospi_p39, x9[57], x9[38], x10[38], x10[57],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p57, cospi_p07, x9[56], x9[39], x10[39], x10[56],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p05, cospi_p59, x9[55], x9[40], x10[40], x10[55],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p37, cospi_p27, x9[54], x9[41], x10[41], x10[54],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p21, cospi_p43, x9[53], x9[42], x10[42], x10[53],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p53, cospi_p11, x9[52], x9[43], x10[43], x10[52],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p13, cospi_p51, x9[51], x9[44], x10[44], x10[51],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p45, cospi_p19, x9[50], x9[45], x10[45], x10[50],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p29, cospi_p35, x9[49], x9[46], x10[46], x10[49],
                        *__rounding, cos_bit);
  btf_32_type0_avx2_new(cospi_p61, cospi_p03, x9[48], x9[47], x10[47], x10[48],
                        *__rounding, cos_bit);
}
static void fdct64_avx2(__m256i *input, __m256i *output, int8_t cos_bit,
                        const int instride, const int outstride) {
  const int32_t *cospi = cospi_arr(cos_bit);
  const __m256i __rounding = _mm256_set1_epi32(1 << (cos_bit - 1));
  __m256i cospi_m32 = _mm256_set1_epi32(-cospi[32]);
  __m256i cospi_p32 = _mm256_set1_epi32(cospi[32]);
  __m256i cospi_m16 = _mm256_set1_epi32(-cospi[16]);
  __m256i cospi_p48 = _mm256_set1_epi32(cospi[48]);
  __m256i cospi_m48 = _mm256_set1_epi32(-cospi[48]);
  __m256i cospi_p16 = _mm256_set1_epi32(cospi[16]);
  __m256i cospi_m08 = _mm256_set1_epi32(-cospi[8]);
  __m256i cospi_p56 = _mm256_set1_epi32(cospi[56]);
  __m256i cospi_m56 = _mm256_set1_epi32(-cospi[56]);
  __m256i cospi_m40 = _mm256_set1_epi32(-cospi[40]);
  __m256i cospi_p24 = _mm256_set1_epi32(cospi[24]);
  __m256i cospi_m24 = _mm256_set1_epi32(-cospi[24]);
  __m256i cospi_p08 = _mm256_set1_epi32(cospi[8]);
  __m256i cospi_p40 = _mm256_set1_epi32(cospi[40]);

  int startidx = 0 * instride;
  int endidx = 63 * instride;
  // stage 1
  __m256i x1[64];
  x1[0] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[63] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[1] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[62] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[2] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[61] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[3] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[60] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[4] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[59] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[5] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[58] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[6] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[57] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[7] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[56] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[8] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[55] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[9] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[54] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[10] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[53] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[11] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[52] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[12] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[51] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[13] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[50] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[14] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[49] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[15] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[48] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[16] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[47] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[17] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[46] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[18] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[45] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[19] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[44] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[20] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[43] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[21] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[42] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[22] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[41] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[23] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[40] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[24] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[39] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[25] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[38] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[26] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[37] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[27] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[36] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[28] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[35] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[29] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[34] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[30] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[33] = _mm256_sub_epi32(input[startidx], input[endidx]);
  startidx += instride;
  endidx -= instride;
  x1[31] = _mm256_add_epi32(input[startidx], input[endidx]);
  x1[32] = _mm256_sub_epi32(input[startidx], input[endidx]);

  // stage 2
  __m256i x2[64];
  fdct64_stage2_avx2(x1, x2, &cospi_m32, &cospi_p32, &__rounding, cos_bit);
  // stage 3
  fdct64_stage3_avx2(x2, x1, &cospi_m32, &cospi_p32, &__rounding, cos_bit);
  // stage 4
  fdct64_stage4_avx2(x1, x2, &cospi_m32, &cospi_p32, &cospi_m16, &cospi_p48,
                     &cospi_m48, &__rounding, cos_bit);
  // stage 5
  fdct64_stage5_avx2(x2, x1, &cospi_m32, &cospi_p32, &cospi_m16, &cospi_p48,
                     &cospi_m48, &__rounding, cos_bit);
  // stage 6
  fdct64_stage6_avx2(x1, x2, &cospi_p16, &cospi_p32, &cospi_m16, &cospi_p48,
                     &cospi_m48, &cospi_m08, &cospi_p56, &cospi_m56, &cospi_m40,
                     &cospi_p24, &cospi_m24, &__rounding, cos_bit);
  // stage 7
  fdct64_stage7_avx2(x2, x1, &cospi_p08, &cospi_p56, &cospi_p40, &cospi_p24,
                     &cospi_m08, &cospi_m56, &cospi_m40, &cospi_m24,
                     &__rounding, cos_bit);
  // stage 8
  fdct64_stage8_avx2(x1, x2, cospi, &__rounding, cos_bit);
  // stage 9
  fdct64_stage9_avx2(x2, x1, cospi, &__rounding, cos_bit);
  // stage 10
  fdct64_stage10_avx2(x1, x2, cospi, &__rounding, cos_bit);

  startidx = 0 * outstride;
  endidx = 63 * outstride;

  // stage 11
  output[startidx] = x2[0];
  output[endidx] = x2[63];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[32];
  output[endidx] = x2[31];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[16];
  output[endidx] = x2[47];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[48];
  output[endidx] = x2[15];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[8];
  output[endidx] = x2[55];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[40];
  output[endidx] = x2[23];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[24];
  output[endidx] = x2[39];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[56];
  output[endidx] = x2[7];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[4];
  output[endidx] = x2[59];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[36];
  output[endidx] = x2[27];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[20];
  output[endidx] = x2[43];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[52];
  output[endidx] = x2[11];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[12];
  output[endidx] = x2[51];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[44];
  output[endidx] = x2[19];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[28];
  output[endidx] = x2[35];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[60];
  output[endidx] = x2[3];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[2];
  output[endidx] = x2[61];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[34];
  output[endidx] = x2[29];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[18];
  output[endidx] = x2[45];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[50];
  output[endidx] = x2[13];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[10];
  output[endidx] = x2[53];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[42];
  output[endidx] = x2[21];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[26];
  output[endidx] = x2[37];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[58];
  output[endidx] = x2[5];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[6];
  output[endidx] = x2[57];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[38];
  output[endidx] = x2[25];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[22];
  output[endidx] = x2[41];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[54];
  output[endidx] = x2[9];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[14];
  output[endidx] = x2[49];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[46];
  output[endidx] = x2[17];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x1[30];
  output[endidx] = x2[33];
  startidx += outstride;
  endidx -= outstride;
  output[startidx] = x2[62];
  output[endidx] = x2[1];
}

static INLINE void load_buffer_64x64_round_avx2(const int16_t *input,
                                                int32_t stride, __m256i *output,
                                                int size, int bit) {
  for (int i = 0; i < 64; ++i) {
    const __m128i x0 = _mm_loadu_si128((const __m128i *)(input + 0 * 8));
    const __m128i x1 = _mm_loadu_si128((const __m128i *)(input + 1 * 8));
    const __m128i x2 = _mm_loadu_si128((const __m128i *)(input + 2 * 8));
    const __m128i x3 = _mm_loadu_si128((const __m128i *)(input + 3 * 8));
    const __m128i x4 = _mm_loadu_si128((const __m128i *)(input + 4 * 8));
    const __m128i x5 = _mm_loadu_si128((const __m128i *)(input + 5 * 8));
    const __m128i x6 = _mm_loadu_si128((const __m128i *)(input + 6 * 8));
    const __m128i x7 = _mm_loadu_si128((const __m128i *)(input + 7 * 8));

    const __m256i v0 = _mm256_cvtepi16_epi32(x0);
    const __m256i v1 = _mm256_cvtepi16_epi32(x1);
    const __m256i v2 = _mm256_cvtepi16_epi32(x2);
    const __m256i v3 = _mm256_cvtepi16_epi32(x3);
    const __m256i v4 = _mm256_cvtepi16_epi32(x4);
    const __m256i v5 = _mm256_cvtepi16_epi32(x5);
    const __m256i v6 = _mm256_cvtepi16_epi32(x6);
    const __m256i v7 = _mm256_cvtepi16_epi32(x7);

    _mm256_storeu_si256(output + 0, v0);
    _mm256_storeu_si256(output + 1, v1);
    _mm256_storeu_si256(output + 2, v2);
    _mm256_storeu_si256(output + 3, v3);
    _mm256_storeu_si256(output + 4, v4);
    _mm256_storeu_si256(output + 5, v5);
    _mm256_storeu_si256(output + 6, v6);
    _mm256_storeu_si256(output + 7, v7);

    input += stride;
    output += 8;
  }

  round_shift_32_64xn_avx2(output, size, bit, stride);
}

void av1_fwd_txfm2d_64x64_avx2(const int16_t *input, int32_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  (void)bd;
  (void)tx_type;
  (void)use_ddt;
  assert(tx_type == DCT_DCT);
  __m256i buf0[512], buf1[512];
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_64X64];
  const int txw_idx = get_txw_idx(TX_64X64);
  const int txh_idx = get_txh_idx(TX_64X64);
  const int8_t cos_bit_col = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t cos_bit_row = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int width = tx_size_wide[TX_64X64];
  const int height = tx_size_high[TX_64X64];
  const transform_1d_avx2 col_txfm = fdct64_avx2;
  const transform_1d_avx2 row_txfm = fdct64_avx2;
  const int width_div16 = (width >> 4);
  const int width_div8 = (width >> 3);
  // column transform
  load_buffer_64x64_round_avx2(input, stride, buf0, height, shift[0]);
  for (int i = 0; i < width_div16; ++i) {
    col_txfm(&buf0[i << 1], &buf0[i << 1], cos_bit_col, width_div8, width_div8);
    col_txfm(&buf0[(i << 1) + 1], &buf0[(i << 1) + 1], cos_bit_col, width_div8,
             width_div8);
  }
  round_shift_32_64xn_avx2(&buf0[0], height, shift[1], width_div16);
  for (int r = 0; r < height; r += 8) {
    for (int c = 0; c < width_div8; c++) {
      fwd_txfm_transpose_8x8_avx2(&buf0[r * width_div8 + c],
                                  &buf1[c * 8 * width_div8 + (r >> 3)],
                                  width_div8, width_div8);
    }
  }
  // row transform
  for (int i = 0; i < 2; i++) {
    row_txfm(&buf1[i << 1], &buf0[i << 1], cos_bit_row, width_div8,
             width_div16);
    row_txfm(&buf1[(i << 1) + 1], &buf0[(i << 1) + 1], cos_bit_row, width_div8,
             width_div16);
  }
  round_shift_32_64xn_avx2(&buf0[0], height, shift[2], width_div16);
  for (int r = 0; r < (height >> 1); r += 8) {
    for (int c = 0; c < width_div16; c++) {
      fwd_txfm_transpose_8x8_avx2(&buf0[r * width_div16 + c],
                                  &buf1[c * 8 * width_div16 + (r >> 3)],
                                  width_div16, width_div16);
    }
  }
  store_buffer_avx2(buf1, output, 8, 128);
}

void av1_fwd_txfm2d_64x16_avx2(const int16_t *input, int32_t *coeff, int stride,
                               TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[128];
  __m256i *outcoeff128 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_64X16];
  const int txw_idx = get_txw_idx(TX_64X16);
  const int txh_idx = get_txh_idx(TX_64X16);
  const int txfm_size_col = tx_size_wide[TX_64X16];
  const int txfm_size_row = tx_size_high[TX_64X16];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int num_col = txfm_size_col >> 3;
  const int num_row = txfm_size_row >> 3;
  const transform_1d_avx2 col_txfm =
      (use_ddt && REPLACE_ADST16 &&
       (vtx_tab[tx_type] == ADST_1D || vtx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm16_arr[DDT_1D]
          : highbd_txfm16_arr[vtx_tab[tx_type]];
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  if (ud_flip) {
    int j = 0;
    for (int i = txfm_size_row - 1; i >= 0; --i) {
      load_buffer_32xn_avx2(input + 0 + i * stride, in + 0 + j * 8, 8,
                            shift[0]);
      load_buffer_32xn_avx2(input + 32 + i * stride, in + 4 + j * 8, 8,
                            shift[0]);
      j++;
    }
  } else {
    assert(lr_flip == 0);
    for (int i = 0; i < txfm_size_row; ++i) {
      load_buffer_32xn_avx2(input + 0 + i * stride, in + 0 + i * 8, 8,
                            shift[0]);
      load_buffer_32xn_avx2(input + 32 + i * stride, in + 4 + i * 8, 8,
                            shift[0]);
    }
  }
  col_txfm(in, outcoeff128, bitcol, num_col, num_col);
  col_txfm_16x16_rounding(outcoeff128, -shift[1]);
  col_txfm_16x16_rounding(outcoeff128 + 32, -shift[1]);
  col_txfm_16x16_rounding(outcoeff128 + 64, -shift[1]);
  col_txfm_16x16_rounding(outcoeff128 + 96, -shift[1]);
  transpose_8nx8n(outcoeff128, in, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    fdct64_avx2(in + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoeff128, txfm_size_row, txfm_size_col >> 1);
}

void av1_fwd_txfm2d_64x8_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[64];
  __m256i *outcoeff256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_64X8];
  const int txw_idx = get_txw_idx(TX_64X8);
  const int txh_idx = get_txh_idx(TX_64X8);
  const int txfm_size_col = tx_size_wide[TX_64X8];
  const int txfm_size_row = tx_size_high[TX_64X8];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const transform_1d_avx2 col_txfm =
      (use_ddt && REPLACE_ADST8 &&
       (vtx_tab[tx_type] == ADST_1D || vtx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm8_arr[DDT_1D]
          : highbd_txfm8_arr[vtx_tab[tx_type]];
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  const int num_col = txfm_size_col >> 3;
  const int num_row = txfm_size_row >> 3;
  // column transform
  if (ud_flip) {
    int j = 0;
    for (int i = num_col - 1; i >= 0; --i) {
      load_buffer_32xn_avx2(input + 0 + i * stride, in + 0 + j * 8, 8,
                            shift[0]);
      load_buffer_32xn_avx2(input + 32 + i * stride, in + 4 + j * 8, 8,
                            shift[0]);
      j++;
    }
  } else {
    assert(lr_flip == 0);
    for (int i = 0; i < num_col; ++i) {
      load_buffer_32xn_avx2(input + 0 + i * stride, in + 0 + i * 8, 8,
                            shift[0]);
      load_buffer_32xn_avx2(input + 32 + i * stride, in + 4 + i * 8, 8,
                            shift[0]);
    }
  }

  col_txfm(in, outcoeff256, bitcol, num_col, num_col);
  col_txfm_16x16_rounding(outcoeff256, -shift[1]);
  col_txfm_16x16_rounding(outcoeff256 + 32, -shift[1]);
  transpose_8nx8n(outcoeff256, in, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    fdct64_avx2(in + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoeff256, txfm_size_row, txfm_size_col >> 1);
  av1_round_shift_rect_array_32_avx2(outcoeff256, outcoeff256, 64, -shift[2],
                                     NewSqrt2);
}

void av1_fwd_txfm2d_8x64_avx2(const int16_t *input, int32_t *coeff, int stride,
                              TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[64];
  __m256i *outcoef256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_8X64];
  const int txw_idx = get_txw_idx(TX_8X64);
  const int txh_idx = get_txh_idx(TX_8X64);
  const transform_1d_avx2 row_txfm =
      (use_ddt && REPLACE_ADST8 &&
       (htx_tab[tx_type] == ADST_1D || htx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm8_arr[DDT_1D]
          : highbd_txfm8_arr[htx_tab[tx_type]];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int txfm_size_col = tx_size_wide[TX_8X64];
  const int txfm_size_row = tx_size_high[TX_8X64];
  const int num_col = txfm_size_col >> 3;
  const int num_row = txfm_size_row >> 3;
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);

  // column transform
  load_buffer_8x16_avx2(input, in, stride, ud_flip, lr_flip, shift[0]);
  load_buffer_8x16_avx2(input + 16 * stride, in + 16, stride, ud_flip, lr_flip,
                        shift[0]);
  load_buffer_8x16_avx2(input + 32 * stride, in + 2 * 16, stride, ud_flip,
                        lr_flip, shift[0]);
  load_buffer_8x16_avx2(input + 48 * stride, in + 3 * 16, stride, ud_flip,
                        lr_flip, shift[0]);

  for (int i = 0; i < num_col; ++i)
    fdct64_avx2(in + i, in + i, bitcol, num_col, num_col);

  col_txfm_16x16_rounding(in, -shift[1]);
  col_txfm_16x16_rounding(in + 32, -shift[1]);
  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  row_txfm(outcoef256, in, bitrow, num_row, num_row);
  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col);
  av1_round_shift_rect_array_32_avx2(outcoef256, outcoef256, 64, -shift[2],
                                     NewSqrt2);
  memset(coeff + txfm_size_col * 32, 0, txfm_size_col * 32 * sizeof(*coeff));
}

void av1_fwd_txfm2d_16x64_avx2(const int16_t *input, int32_t *coeff, int stride,
                               TX_TYPE tx_type, int use_ddt, int bd) {
  (void)bd;
  __m256i in[128];
  __m256i *outcoeff256 = (__m256i *)coeff;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_16X64];
  const int txw_idx = get_txw_idx(TX_16X64);
  const int txh_idx = get_txh_idx(TX_16X64);
  const int txfm_size_col = tx_size_wide[TX_16X64];
  const int txfm_size_row = tx_size_high[TX_16X64];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const transform_1d_avx2 row_txfm =
      (use_ddt && REPLACE_ADST16 &&
       (htx_tab[tx_type] == ADST_1D || htx_tab[tx_type] == FLIPADST_1D))
          ? highbd_txfm16_arr[DDT_1D]
          : highbd_txfm16_arr[htx_tab[tx_type]];
  const int num_col = txfm_size_col >> 3;
  const int num_row = txfm_size_row >> 3;
  int ud_flip, lr_flip;
  get_flip_cfg(tx_type, &ud_flip, &lr_flip);
  // column transform
  load_buffer_16xn_avx2(input, in, stride, txfm_size_row, 2, ud_flip, lr_flip);
  for (int i = 0; i < num_col; ++i)
    fdct64_avx2(in + i, outcoeff256 + i, bitcol, num_col, num_col);

  col_txfm_16x16_rounding(outcoeff256, -shift[1]);
  col_txfm_16x16_rounding(outcoeff256 + 32, -shift[1]);
  col_txfm_16x16_rounding(outcoeff256 + 64, -shift[1]);
  col_txfm_16x16_rounding(outcoeff256 + 96, -shift[1]);
  transpose_8nx8n(outcoeff256, in, txfm_size_col, txfm_size_row);
  // row transform
  row_txfm(in, in, bitrow, num_row, num_row);
  transpose_8nx8n(in, outcoeff256, txfm_size_row, txfm_size_col);
  memset(coeff + txfm_size_col * 32, 0, txfm_size_col * 32 * sizeof(*coeff));
}

void av1_fwd_txfm2d_32x64_avx2(const int16_t *input, int32_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  (void)bd;
  (void)tx_type;
  (void)use_ddt;
  __m256i in[256];
  __m256i *outcoef256 = (__m256i *)output;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_32X64];
  const int txw_idx = get_txw_idx(TX_32X64);
  const int txh_idx = get_txh_idx(TX_32X64);
  const int txfm_size_col = tx_size_wide[TX_32X64];
  const int txfm_size_row = tx_size_high[TX_32X64];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int num_row = txfm_size_row >> 3;
  const int num_col = txfm_size_col >> 3;
  // column transform
  load_buffer_32x8n(input, in, stride, 0, 0, shift[0], txfm_size_row);
  for (int i = 0; i < num_col; ++i)
    fdct64_avx2(in + i, in + i, bitcol, num_col, num_col);

  for (int i = 0; i < num_row; ++i)
    col_txfm_16x16_rounding(in + i * txfm_size_col, -shift[1]);

  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    fdct32_avx2(outcoef256 + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col);
  av1_round_shift_rect_array_32_avx2(outcoef256, outcoef256, 256, -shift[2],
                                     NewSqrt2);
}

void av1_fwd_txfm2d_64x32_avx2(const int16_t *input, int32_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  (void)bd;
  (void)tx_type;
  (void)use_ddt;
  __m256i in[256];
  __m256i *outcoef256 = (__m256i *)output;
  const int8_t *shift = av1_fwd_txfm_shift_ls[TX_64X32];
  const int txw_idx = get_txw_idx(TX_64X32);
  const int txh_idx = get_txh_idx(TX_64X32);
  const int txfm_size_col = tx_size_wide[TX_64X32];
  const int txfm_size_row = tx_size_high[TX_64X32];
  const int8_t bitcol = av1_fwd_cos_bit_col[txw_idx][txh_idx];
  const int8_t bitrow = av1_fwd_cos_bit_row[txw_idx][txh_idx];
  const int num_row = txfm_size_row >> 3;
  const int num_col = txfm_size_col >> 3;
  // column transform
  for (int i = 0; i < txfm_size_row; ++i) {
    load_buffer_32xn_avx2(input + 0 + i * stride, in + 0 + i * 8, 8, shift[0]);
    load_buffer_32xn_avx2(input + 32 + i * stride, in + 4 + i * 8, 8, shift[0]);
  }

  for (int i = 0; i < num_col; ++i)
    fdct32_avx2(in + i, in + i, bitcol, num_col, num_col);

  for (int i = 0; i < num_col; ++i)
    col_txfm_16x16_rounding(in + i * txfm_size_row, -shift[1]);

  transpose_8nx8n(in, outcoef256, txfm_size_col, txfm_size_row);
  // row transform
  for (int i = 0; i < num_row; ++i)
    fdct64_avx2(outcoef256 + i, in + i, bitrow, num_row, num_row);

  transpose_8nx8n(in, outcoef256, txfm_size_row, txfm_size_col >> 1);
  av1_round_shift_rect_array_32_avx2(outcoef256, outcoef256, 256, -shift[2],
                                     NewSqrt2);
}

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
#if CONFIG_E124_IST_REDUCE_METHOD4
  const int32_t *kernel = (size == 0) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];
#else
  const int32_t *kernel = (size == 4) ? ist_4x4_kernel_int32[mode][stx_idx][0]
                                      : ist_8x8_kernel_int32[mode][stx_idx][0];
#endif  // CONFIG_E124_IST_REDUCE_METHOD4

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

  const int shift = 7;

#if CONFIG_E124_IST_REDUCE_METHOD4
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
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
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
#if CONFIG_E124_IST_REDUCE_METHOD4
  }
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
}

#if CONFIG_CORE_TX
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
#endif  // CONFIG_CORE_TX
