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

#include "config/avm_dsp_rtcd.h"
#include "avm/avm_integer.h"
#include "avm_dsp/x86/bitdepth_conversion_avx2.h"
#include "avm_ports/mem.h"

static void highbd_hadamard_col8_avx2(__m256i *in, int iter) {
  __m256i a0 = in[0];
  __m256i a1 = in[1];
  __m256i a2 = in[2];
  __m256i a3 = in[3];
  __m256i a4 = in[4];
  __m256i a5 = in[5];
  __m256i a6 = in[6];
  __m256i a7 = in[7];

  __m256i b0 = _mm256_add_epi32(a0, a1);
  __m256i b1 = _mm256_sub_epi32(a0, a1);
  __m256i b2 = _mm256_add_epi32(a2, a3);
  __m256i b3 = _mm256_sub_epi32(a2, a3);
  __m256i b4 = _mm256_add_epi32(a4, a5);
  __m256i b5 = _mm256_sub_epi32(a4, a5);
  __m256i b6 = _mm256_add_epi32(a6, a7);
  __m256i b7 = _mm256_sub_epi32(a6, a7);

  a0 = _mm256_add_epi32(b0, b2);
  a1 = _mm256_add_epi32(b1, b3);
  a2 = _mm256_sub_epi32(b0, b2);
  a3 = _mm256_sub_epi32(b1, b3);
  a4 = _mm256_add_epi32(b4, b6);
  a5 = _mm256_add_epi32(b5, b7);
  a6 = _mm256_sub_epi32(b4, b6);
  a7 = _mm256_sub_epi32(b5, b7);

  if (iter == 0) {
    b0 = _mm256_add_epi32(a0, a4);
    b7 = _mm256_add_epi32(a1, a5);
    b3 = _mm256_add_epi32(a2, a6);
    b4 = _mm256_add_epi32(a3, a7);
    b2 = _mm256_sub_epi32(a0, a4);
    b6 = _mm256_sub_epi32(a1, a5);
    b1 = _mm256_sub_epi32(a2, a6);
    b5 = _mm256_sub_epi32(a3, a7);

    a0 = _mm256_unpacklo_epi32(b0, b1);
    a1 = _mm256_unpacklo_epi32(b2, b3);
    a2 = _mm256_unpackhi_epi32(b0, b1);
    a3 = _mm256_unpackhi_epi32(b2, b3);
    a4 = _mm256_unpacklo_epi32(b4, b5);
    a5 = _mm256_unpacklo_epi32(b6, b7);
    a6 = _mm256_unpackhi_epi32(b4, b5);
    a7 = _mm256_unpackhi_epi32(b6, b7);

    b0 = _mm256_unpacklo_epi64(a0, a1);
    b1 = _mm256_unpacklo_epi64(a4, a5);
    b2 = _mm256_unpackhi_epi64(a0, a1);
    b3 = _mm256_unpackhi_epi64(a4, a5);
    b4 = _mm256_unpacklo_epi64(a2, a3);
    b5 = _mm256_unpacklo_epi64(a6, a7);
    b6 = _mm256_unpackhi_epi64(a2, a3);
    b7 = _mm256_unpackhi_epi64(a6, a7);

    in[0] = _mm256_permute2x128_si256(b0, b1, 0x20);
    in[1] = _mm256_permute2x128_si256(b0, b1, 0x31);
    in[2] = _mm256_permute2x128_si256(b2, b3, 0x20);
    in[3] = _mm256_permute2x128_si256(b2, b3, 0x31);
    in[4] = _mm256_permute2x128_si256(b4, b5, 0x20);
    in[5] = _mm256_permute2x128_si256(b4, b5, 0x31);
    in[6] = _mm256_permute2x128_si256(b6, b7, 0x20);
    in[7] = _mm256_permute2x128_si256(b6, b7, 0x31);
  } else {
    in[0] = _mm256_add_epi32(a0, a4);
    in[7] = _mm256_add_epi32(a1, a5);
    in[3] = _mm256_add_epi32(a2, a6);
    in[4] = _mm256_add_epi32(a3, a7);
    in[2] = _mm256_sub_epi32(a0, a4);
    in[6] = _mm256_sub_epi32(a1, a5);
    in[1] = _mm256_sub_epi32(a2, a6);
    in[5] = _mm256_sub_epi32(a3, a7);
  }
}

void avm_highbd_hadamard_8x8_avx2(const int16_t *src_diff, ptrdiff_t src_stride,
                                  tran_low_t *coeff) {
  __m128i src16[8];
  __m256i src32[8];

  src16[0] = _mm_loadu_si128((const __m128i *)src_diff);
  src16[1] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[2] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[3] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[4] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[5] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[6] = _mm_loadu_si128((const __m128i *)(src_diff += src_stride));
  src16[7] = _mm_loadu_si128((const __m128i *)(src_diff + src_stride));

  src32[0] = _mm256_cvtepi16_epi32(src16[0]);
  src32[1] = _mm256_cvtepi16_epi32(src16[1]);
  src32[2] = _mm256_cvtepi16_epi32(src16[2]);
  src32[3] = _mm256_cvtepi16_epi32(src16[3]);
  src32[4] = _mm256_cvtepi16_epi32(src16[4]);
  src32[5] = _mm256_cvtepi16_epi32(src16[5]);
  src32[6] = _mm256_cvtepi16_epi32(src16[6]);
  src32[7] = _mm256_cvtepi16_epi32(src16[7]);

  highbd_hadamard_col8_avx2(src32, 0);
  highbd_hadamard_col8_avx2(src32, 1);

  _mm256_storeu_si256((__m256i *)coeff, src32[0]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[1]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[2]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[3]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[4]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[5]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[6]);
  coeff += 8;
  _mm256_storeu_si256((__m256i *)coeff, src32[7]);
}

void avm_highbd_hadamard_16x16_avx2(const int16_t *src_diff,
                                    ptrdiff_t src_stride, tran_low_t *coeff) {
  int idx;
  tran_low_t *t_coeff = coeff;
  for (idx = 0; idx < 4; ++idx) {
    const int16_t *src_ptr =
        src_diff + (idx >> 1) * 8 * src_stride + (idx & 0x01) * 8;
    avm_highbd_hadamard_8x8_avx2(src_ptr, src_stride, t_coeff + idx * 64);
  }

  for (idx = 0; idx < 64; idx += 8) {
    __m256i coeff0 = _mm256_loadu_si256((const __m256i *)t_coeff);
    __m256i coeff1 = _mm256_loadu_si256((const __m256i *)(t_coeff + 64));
    __m256i coeff2 = _mm256_loadu_si256((const __m256i *)(t_coeff + 128));
    __m256i coeff3 = _mm256_loadu_si256((const __m256i *)(t_coeff + 192));

    __m256i b0 = _mm256_add_epi32(coeff0, coeff1);
    __m256i b1 = _mm256_sub_epi32(coeff0, coeff1);
    __m256i b2 = _mm256_add_epi32(coeff2, coeff3);
    __m256i b3 = _mm256_sub_epi32(coeff2, coeff3);

    b0 = _mm256_srai_epi32(b0, 1);
    b1 = _mm256_srai_epi32(b1, 1);
    b2 = _mm256_srai_epi32(b2, 1);
    b3 = _mm256_srai_epi32(b3, 1);

    coeff0 = _mm256_add_epi32(b0, b2);
    coeff1 = _mm256_add_epi32(b1, b3);
    coeff2 = _mm256_sub_epi32(b0, b2);
    coeff3 = _mm256_sub_epi32(b1, b3);

    _mm256_storeu_si256((__m256i *)coeff, coeff0);
    _mm256_storeu_si256((__m256i *)(coeff + 64), coeff1);
    _mm256_storeu_si256((__m256i *)(coeff + 128), coeff2);
    _mm256_storeu_si256((__m256i *)(coeff + 192), coeff3);

    coeff += 8;
    t_coeff += 8;
  }
}

void avm_highbd_hadamard_32x32_avx2(const int16_t *src_diff,
                                    ptrdiff_t src_stride, tran_low_t *coeff) {
  int idx;
  tran_low_t *t_coeff = coeff;
  for (idx = 0; idx < 4; ++idx) {
    const int16_t *src_ptr =
        src_diff + (idx >> 1) * 16 * src_stride + (idx & 0x01) * 16;
    avm_highbd_hadamard_16x16_avx2(src_ptr, src_stride, t_coeff + idx * 256);
  }

  for (idx = 0; idx < 256; idx += 8) {
    __m256i coeff0 = _mm256_loadu_si256((const __m256i *)t_coeff);
    __m256i coeff1 = _mm256_loadu_si256((const __m256i *)(t_coeff + 256));
    __m256i coeff2 = _mm256_loadu_si256((const __m256i *)(t_coeff + 512));
    __m256i coeff3 = _mm256_loadu_si256((const __m256i *)(t_coeff + 768));

    __m256i b0 = _mm256_add_epi32(coeff0, coeff1);
    __m256i b1 = _mm256_sub_epi32(coeff0, coeff1);
    __m256i b2 = _mm256_add_epi32(coeff2, coeff3);
    __m256i b3 = _mm256_sub_epi32(coeff2, coeff3);

    b0 = _mm256_srai_epi32(b0, 2);
    b1 = _mm256_srai_epi32(b1, 2);
    b2 = _mm256_srai_epi32(b2, 2);
    b3 = _mm256_srai_epi32(b3, 2);

    coeff0 = _mm256_add_epi32(b0, b2);
    coeff1 = _mm256_add_epi32(b1, b3);
    coeff2 = _mm256_sub_epi32(b0, b2);
    coeff3 = _mm256_sub_epi32(b1, b3);

    _mm256_storeu_si256((__m256i *)coeff, coeff0);
    _mm256_storeu_si256((__m256i *)(coeff + 256), coeff1);
    _mm256_storeu_si256((__m256i *)(coeff + 512), coeff2);
    _mm256_storeu_si256((__m256i *)(coeff + 768), coeff3);

    coeff += 8;
    t_coeff += 8;
  }
}

int avm_satd_avx2(const tran_low_t *coeff, int length) {
  __m256i accum = _mm256_setzero_si256();
  int i;

  for (i = 0; i < length; i += 8, coeff += 8) {
    const __m256i src_line = _mm256_loadu_si256((const __m256i *)coeff);
    const __m256i abs = _mm256_abs_epi32(src_line);
    accum = _mm256_add_epi32(accum, abs);
  }

  {  // 32 bit horizontal add
    const __m256i a = _mm256_srli_si256(accum, 8);
    const __m256i b = _mm256_add_epi32(accum, a);
    const __m256i c = _mm256_srli_epi64(b, 32);
    const __m256i d = _mm256_add_epi32(b, c);
    const __m128i accum_128 = _mm_add_epi32(_mm256_castsi256_si128(d),
                                            _mm256_extractf128_si256(d, 1));
    return _mm_cvtsi128_si32(accum_128);
  }
}

int avm_satd_lp_avx2(const int16_t *coeff, int length) {
  const __m256i one = _mm256_set1_epi16(1);
  __m256i accum = _mm256_setzero_si256();

  for (int i = 0; i < length; i += 16) {
    const __m256i src_line = _mm256_loadu_si256((const __m256i *)coeff);
    const __m256i abs = _mm256_abs_epi16(src_line);
    const __m256i sum = _mm256_madd_epi16(abs, one);
    accum = _mm256_add_epi32(accum, sum);
    coeff += 16;
  }

  {  // 32 bit horizontal add
    const __m256i a = _mm256_srli_si256(accum, 8);
    const __m256i b = _mm256_add_epi32(accum, a);
    const __m256i c = _mm256_srli_epi64(b, 32);
    const __m256i d = _mm256_add_epi32(b, c);
    const __m128i accum_128 = _mm_add_epi32(_mm256_castsi256_si128(d),
                                            _mm256_extractf128_si256(d, 1));
    return _mm_cvtsi128_si32(accum_128);
  }
}

void avm_int_pro_row_avx2(int16_t *hbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 16 == 0);
  assert(height % 4 == 0);
  const __m256i one = _mm256_set1_epi16(1);
  const __m128i norm = _mm_cvtsi32_si128(norm_factor);

  for (int idx = 0; idx < width; idx += 16) {
    __m256i s0 = _mm256_setzero_si256();
    __m256i s1 = _mm256_setzero_si256();
    const uint16_t *ref_tmp = ref + idx;
    for (int y = 0; y < height; y += 4) {
      const __m256i r0 = _mm256_loadu_si256((const __m256i *)ref_tmp);
      const __m256i r1 =
          _mm256_loadu_si256((const __m256i *)(ref_tmp + ref_stride));
      const __m256i r2 =
          _mm256_loadu_si256((const __m256i *)(ref_tmp + 2 * ref_stride));
      const __m256i r3 =
          _mm256_loadu_si256((const __m256i *)(ref_tmp + 3 * ref_stride));
      const __m256i r01 = _mm256_add_epi16(r0, r1);
      const __m256i r23 = _mm256_add_epi16(r2, r3);
      s0 = _mm256_add_epi32(
          s0, _mm256_madd_epi16(_mm256_unpacklo_epi16(r01, r23), one));
      s1 = _mm256_add_epi32(
          s1, _mm256_madd_epi16(_mm256_unpackhi_epi16(r01, r23), one));
      ref_tmp += 4 * ref_stride;
    }
    s0 = _mm256_sra_epi32(s0, norm);
    s1 = _mm256_sra_epi32(s1, norm);
    const __m256i res = _mm256_packs_epi32(s0, s1);
    _mm256_storeu_si256((__m256i *)(hbuf + idx), res);
  }
}

void avm_int_pro_col_avx2(int16_t *vbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 16 == 0);
  assert(height % 4 == 0);
  const __m256i one = _mm256_set1_epi16(1);
  const __m128i norm = _mm_cvtsi32_si128(norm_factor);

  for (int ht = 0; ht < height; ht += 4) {
    const uint16_t *r0 = ref;
    const uint16_t *r1 = r0 + ref_stride;
    const uint16_t *r2 = r1 + ref_stride;
    const uint16_t *r3 = r2 + ref_stride;
    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    __m256i acc2 = _mm256_setzero_si256();
    __m256i acc3 = _mm256_setzero_si256();

    for (int idx = 0; idx < width; idx += 16) {
      acc0 = _mm256_add_epi32(
          acc0, _mm256_madd_epi16(
                    _mm256_loadu_si256((const __m256i *)(r0 + idx)), one));
      acc1 = _mm256_add_epi32(
          acc1, _mm256_madd_epi16(
                    _mm256_loadu_si256((const __m256i *)(r1 + idx)), one));
      acc2 = _mm256_add_epi32(
          acc2, _mm256_madd_epi16(
                    _mm256_loadu_si256((const __m256i *)(r2 + idx)), one));
      acc3 = _mm256_add_epi32(
          acc3, _mm256_madd_epi16(
                    _mm256_loadu_si256((const __m256i *)(r3 + idx)), one));
    }

    const __m256i t01 = _mm256_hadd_epi32(acc0, acc1);
    const __m256i t23 = _mm256_hadd_epi32(acc2, acc3);
    const __m256i t0123 = _mm256_hadd_epi32(t01, t23);
    __m128i sum4 = _mm_add_epi32(_mm256_castsi256_si128(t0123),
                                 _mm256_extracti128_si256(t0123, 1));
    sum4 = _mm_sra_epi32(sum4, norm);
    const __m128i packed = _mm_packs_epi32(sum4, sum4);
    _mm_storel_epi64((__m128i *)(vbuf + ht), packed);
    ref += 4 * ref_stride;
  }
}

int avm_vector_var_avx2(const int16_t *ref, const int16_t *src, const int bwl) {
  assert(bwl >= 2 && bwl <= 6);
  const int width = 4 << bwl;
  const __m256i one = _mm256_set1_epi16(1);
  __m256i v_mean16 = _mm256_setzero_si256();
  __m256i v_sse = _mm256_setzero_si256();

  for (int i = 0; i < width; i += 16) {
    const __m256i v_ref = _mm256_loadu_si256((const __m256i *)(ref + i));
    const __m256i v_src = _mm256_loadu_si256((const __m256i *)(src + i));
    const __m256i diff = _mm256_sub_epi16(v_ref, v_src);
    v_mean16 = _mm256_add_epi16(v_mean16, diff);
    v_sse = _mm256_add_epi32(v_sse, _mm256_madd_epi16(diff, diff));
  }

  const __m256i v_mean = _mm256_madd_epi16(v_mean16, one);
  const __m256i t_mean_sse = _mm256_hadd_epi32(v_mean, v_sse);
  const __m256i t_mean_sse2 = _mm256_hadd_epi32(t_mean_sse, t_mean_sse);
  const __m128i sum_128 =
      _mm_add_epi32(_mm256_castsi256_si128(t_mean_sse2),
                    _mm256_extracti128_si256(t_mean_sse2, 1));
  const int mean = _mm_cvtsi128_si32(sum_128);
  const uint32_t sse = (uint32_t)_mm_extract_epi32(sum_128, 1);

  const uint64_t meansq = (uint64_t)abs(mean) * (uint64_t)abs(mean);
  return sse - (uint32_t)(meansq >> (bwl + 2));
}
