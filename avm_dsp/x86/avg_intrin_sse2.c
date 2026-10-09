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
#include <stdlib.h>

#include "config/avm_dsp_rtcd.h"
#include "avm/avm_integer.h"
#include "avm_dsp/x86/bitdepth_conversion_sse2.h"
#include "avm_ports/mem.h"

int avm_satd_sse2(const tran_low_t *coeff, int length) {
  int i;
  const __m128i zero = _mm_setzero_si128();
  __m128i accum = zero;

  for (i = 0; i < length; i += 8) {
    const __m128i src_line = load_tran_low(coeff);
    const __m128i inv = _mm_sub_epi16(zero, src_line);
    const __m128i abs = _mm_max_epi16(src_line, inv);  // abs(src_line)
    const __m128i abs_lo = _mm_unpacklo_epi16(abs, zero);
    const __m128i abs_hi = _mm_unpackhi_epi16(abs, zero);
    const __m128i sum = _mm_add_epi32(abs_lo, abs_hi);
    accum = _mm_add_epi32(accum, sum);
    coeff += 8;
  }

  {  // cascading summation of accum
    __m128i hi = _mm_srli_si128(accum, 8);
    accum = _mm_add_epi32(accum, hi);
    hi = _mm_srli_epi64(accum, 32);
    accum = _mm_add_epi32(accum, hi);
  }

  return _mm_cvtsi128_si32(accum);
}

void avm_int_pro_row_sse2(int16_t *hbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 8 == 0);
  assert(height % 4 == 0);
  const __m128i one = _mm_set1_epi16(1);
  const __m128i norm = _mm_cvtsi32_si128(norm_factor);

  for (int idx = 0; idx < width; idx += 8) {
    __m128i s0 = _mm_setzero_si128();
    __m128i s1 = _mm_setzero_si128();
    const uint16_t *ref_tmp = ref + idx;
    for (int y = 0; y < height; y += 4) {
      const __m128i r0 = _mm_loadu_si128((const __m128i *)ref_tmp);
      const __m128i r1 =
          _mm_loadu_si128((const __m128i *)(ref_tmp + ref_stride));
      const __m128i r2 =
          _mm_loadu_si128((const __m128i *)(ref_tmp + 2 * ref_stride));
      const __m128i r3 =
          _mm_loadu_si128((const __m128i *)(ref_tmp + 3 * ref_stride));
      const __m128i r01 = _mm_add_epi16(r0, r1);
      const __m128i r23 = _mm_add_epi16(r2, r3);
      s0 = _mm_add_epi32(s0, _mm_madd_epi16(_mm_unpacklo_epi16(r01, r23), one));
      s1 = _mm_add_epi32(s1, _mm_madd_epi16(_mm_unpackhi_epi16(r01, r23), one));
      ref_tmp += 4 * ref_stride;
    }
    s0 = _mm_sra_epi32(s0, norm);
    s1 = _mm_sra_epi32(s1, norm);
    const __m128i res = _mm_packs_epi32(s0, s1);
    _mm_storeu_si128((__m128i *)(hbuf + idx), res);
  }
}

void avm_int_pro_col_sse2(int16_t *vbuf, const uint16_t *ref,
                          const int ref_stride, const int width,
                          const int height, int norm_factor) {
  assert(width % 8 == 0);
  assert(height % 4 == 0);
  const __m128i one = _mm_set1_epi16(1);
  const __m128i norm = _mm_cvtsi32_si128(norm_factor);

  for (int ht = 0; ht < height; ht += 4) {
    const uint16_t *r0 = ref;
    const uint16_t *r1 = r0 + ref_stride;
    const uint16_t *r2 = r1 + ref_stride;
    const uint16_t *r3 = r2 + ref_stride;
    __m128i acc0 = _mm_setzero_si128();
    __m128i acc1 = _mm_setzero_si128();
    __m128i acc2 = _mm_setzero_si128();
    __m128i acc3 = _mm_setzero_si128();

    for (int idx = 0; idx < width; idx += 8) {
      acc0 = _mm_add_epi32(
          acc0,
          _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(r0 + idx)), one));
      acc1 = _mm_add_epi32(
          acc1,
          _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(r1 + idx)), one));
      acc2 = _mm_add_epi32(
          acc2,
          _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(r2 + idx)), one));
      acc3 = _mm_add_epi32(
          acc3,
          _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(r3 + idx)), one));
    }

    const __m128i t01 = _mm_add_epi32(_mm_unpacklo_epi32(acc0, acc1),
                                      _mm_unpackhi_epi32(acc0, acc1));
    const __m128i t23 = _mm_add_epi32(_mm_unpacklo_epi32(acc2, acc3),
                                      _mm_unpackhi_epi32(acc2, acc3));
    __m128i sum4 = _mm_add_epi32(_mm_unpacklo_epi64(t01, t23),
                                 _mm_unpackhi_epi64(t01, t23));
    sum4 = _mm_sra_epi32(sum4, norm);
    const __m128i packed = _mm_packs_epi32(sum4, sum4);
    _mm_storel_epi64((__m128i *)(vbuf + ht), packed);
    ref += 4 * ref_stride;
  }
}

int avm_vector_var_sse2(const int16_t *ref, const int16_t *src, const int bwl) {
  assert(bwl >= 2 && bwl <= 6);
  const int width = 4 << bwl;
  const __m128i one = _mm_set1_epi16(1);
  __m128i v_mean16 = _mm_setzero_si128();
  __m128i v_sse = _mm_setzero_si128();

  for (int i = 0; i < width; i += 8) {
    const __m128i v_ref = _mm_loadu_si128((const __m128i *)(ref + i));
    const __m128i v_src = _mm_loadu_si128((const __m128i *)(src + i));
    const __m128i diff = _mm_sub_epi16(v_ref, v_src);
    v_mean16 = _mm_add_epi16(v_mean16, diff);
    v_sse = _mm_add_epi32(v_sse, _mm_madd_epi16(diff, diff));
  }

  __m128i v_mean = _mm_madd_epi16(v_mean16, one);
  __m128i mean_hi = _mm_srli_si128(v_mean, 8);
  v_mean = _mm_add_epi32(v_mean, mean_hi);
  mean_hi = _mm_srli_epi64(v_mean, 32);
  v_mean = _mm_add_epi32(v_mean, mean_hi);
  const int mean = _mm_cvtsi128_si32(v_mean);

  __m128i sse_hi = _mm_srli_si128(v_sse, 8);
  v_sse = _mm_add_epi32(v_sse, sse_hi);
  sse_hi = _mm_srli_epi64(v_sse, 32);
  v_sse = _mm_add_epi32(v_sse, sse_hi);
  const uint32_t sse = (uint32_t)_mm_cvtsi128_si32(v_sse);

  const uint64_t meansq = (uint64_t)abs(mean) * (uint64_t)abs(mean);
  return sse - (uint32_t)(meansq >> (bwl + 2));
}
