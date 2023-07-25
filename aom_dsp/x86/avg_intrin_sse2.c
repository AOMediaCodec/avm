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

#include "config/aom_dsp_rtcd.h"
#include "aom/aom_integer.h"
#include "aom_dsp/x86/bitdepth_conversion_sse2.h"
#include "aom_ports/mem.h"

int aom_satd_sse2(const tran_low_t *coeff, int length) {
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
