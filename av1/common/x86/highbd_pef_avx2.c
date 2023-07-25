/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
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
#include "config/av1_rtcd.h"
#include "av1/common/pef.h"
#include "aom_dsp/x86/lpf_common_sse2.h"

static INLINE void highbd_filt_pred_generic_avx2(__m128i *p2, __m128i *p1,
                                                 __m128i *p0, __m128i *m2,
                                                 __m128i *m1, __m128i *m0,
                                                 int bd, uint16_t q_thresh,
                                                 uint16_t side_thresh,
                                                 int q_mult, int w_mult) {
  __m128i s_m1 = _mm_slli_epi16(*m1, 1);
  __m128i s_p1 = _mm_slli_epi16(*p1, 1);
  __m128i d_m1 = _mm_sub_epi16(*m2, s_m1);
  d_m1 = _mm_add_epi16(d_m1, *m0);
  d_m1 = _mm_abs_epi16(d_m1);
  __m128i d_p1 = _mm_sub_epi16(*p0, s_p1);
  d_p1 = _mm_add_epi16(d_p1, *p2);
  d_p1 = _mm_abs_epi16(d_p1);

  __m128i side_thr = _mm_set1_epi16(side_thresh);
  __m128i mask_0 = _mm_cmpgt_epi16(d_m1, side_thr);
  __m128i mask_1 = _mm_cmpgt_epi16(d_p1, side_thr);
  __m128i mask = _mm_or_si128(mask_0, mask_1);
  mask = _mm_add_epi16(mask, _mm_set1_epi16(1));

  __m128i delta = _mm_sub_epi16(*p0, *m0);
  delta = _mm_mullo_epi16(delta, _mm_set1_epi16(3));
  __m128i delta_1 = _mm_sub_epi16(*p1, *m1);
  delta = _mm_sub_epi16(delta, delta_1);
  delta = _mm_slli_epi16(delta, 2);
  delta = _mm_mullo_epi16(delta, mask);

  int q_thr = q_thresh * q_mult;
  __m128i min_q_thr = _mm_set1_epi16(q_thr * -1);
  __m128i max_q_thr = _mm_set1_epi16(q_thr);
  delta = _mm_min_epi16(delta, max_q_thr);
  delta = _mm_max_epi16(delta, min_q_thr);
  __m256i delta_256 = _mm256_mullo_epi32(_mm256_cvtepi16_epi32(delta),
                                         _mm256_set1_epi32(w_mult));

  __m256i rounding = _mm256_set1_epi32(1 << PEF_SHIFT >> 1);
  __m128i pel_min = _mm_set1_epi16(0);
  __m128i pel_max = _mm_set1_epi16((1 << bd) - 1);

  __m256i offset;
  __m128i offset_128;
  // derive offset and filter m0 and p0
  offset = _mm256_add_epi32(delta_256, delta_256);
  offset = _mm256_add_epi32(offset, delta_256);
  offset = _mm256_add_epi32(offset, rounding);
  offset = _mm256_srai_epi32(offset, PEF_SHIFT);
  offset = _mm256_packs_epi32(offset, offset);
  offset = _mm256_permute4x64_epi64(offset, 0xD8);
  offset_128 = _mm256_extractf128_si256(offset, 0);
  *m0 = _mm_add_epi16(*m0, offset_128);
  *m0 = _mm_min_epi16(*m0, pel_max);
  *m0 = _mm_max_epi16(*m0, pel_min);
  *p0 = _mm_sub_epi16(*p0, offset_128);
  *p0 = _mm_min_epi16(*p0, pel_max);
  *p0 = _mm_max_epi16(*p0, pel_min);

  // derive offset and filter m1 and p1
  offset = _mm256_add_epi32(delta_256, delta_256);
  offset = _mm256_add_epi32(offset, rounding);
  offset = _mm256_srai_epi32(offset, PEF_SHIFT);
  offset = _mm256_packs_epi32(offset, offset);
  offset = _mm256_permute4x64_epi64(offset, 0xD8);
  offset_128 = _mm256_extractf128_si256(offset, 0);
  *m1 = _mm_add_epi16(*m1, offset_128);
  *m1 = _mm_min_epi16(*m1, pel_max);
  *m1 = _mm_max_epi16(*m1, pel_min);
  *p1 = _mm_sub_epi16(*p1, offset_128);
  *p1 = _mm_min_epi16(*p1, pel_max);
  *p1 = _mm_max_epi16(*p1, pel_min);

  // derive offset and filter m2 and p2
  offset = _mm256_add_epi32(delta_256, rounding);
  offset = _mm256_srai_epi32(offset, PEF_SHIFT);
  offset = _mm256_packs_epi32(offset, offset);
  offset = _mm256_permute4x64_epi64(offset, 0xD8);
  offset_128 = _mm256_extractf128_si256(offset, 0);
  *m2 = _mm_add_epi16(*m2, offset_128);
  *m2 = _mm_min_epi16(*m2, pel_max);
  *m2 = _mm_max_epi16(*m2, pel_min);
  *p2 = _mm_sub_epi16(*p2, offset_128);
  *p2 = _mm_min_epi16(*p2, pel_max);
  *p2 = _mm_max_epi16(*p2, pel_min);
  return;
}

void highbd_filt_vert_pred_avx2(uint16_t *s, int stride, int bd,
                                uint16_t q_thresh, uint16_t side_thresh,
                                int q_mult, int w_mult, int n, int filt_len) {
  if (n == 0 || filt_len == 0) return;
  __m128i m2, m1, m0, p0, p1, p2, dum0, dum1;
  __m128i x0, x1, x2, x3, x4, x5, x6, x7;
  x0 = _mm_loadu_si128((__m128i *)(s - 3 + 0 * stride));
  x1 = _mm_loadu_si128((__m128i *)(s - 3 + 1 * stride));
  x2 = _mm_loadu_si128((__m128i *)(s - 3 + 2 * stride));
  x3 = _mm_loadu_si128((__m128i *)(s - 3 + 3 * stride));
  x4 = _mm_loadu_si128((__m128i *)(s - 3 + 4 * stride));
  x5 = _mm_loadu_si128((__m128i *)(s - 3 + 5 * stride));
  x6 = _mm_loadu_si128((__m128i *)(s - 3 + 6 * stride));
  x7 = _mm_loadu_si128((__m128i *)(s - 3 + 7 * stride));

  highbd_transpose8x8_sse2(&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7, &m2, &m1,
                           &m0, &p0, &p1, &p2, &dum0, &dum1);

  highbd_filt_pred_generic_avx2(&p2, &p1, &p0, &m2, &m1, &m0, bd, q_thresh,
                                side_thresh, q_mult, w_mult);

  highbd_transpose8x8_sse2(&m2, &m1, &m0, &p0, &p1, &p2, &dum0, &dum1, &x0, &x1,
                           &x2, &x3, &x4, &x5, &x6, &x7);

  _mm_storeu_si128((__m128i *)(s - 3 + 0 * stride), x0);
  _mm_storeu_si128((__m128i *)(s - 3 + 1 * stride), x1);
  _mm_storeu_si128((__m128i *)(s - 3 + 2 * stride), x2);
  _mm_storeu_si128((__m128i *)(s - 3 + 3 * stride), x3);
  _mm_storeu_si128((__m128i *)(s - 3 + 4 * stride), x4);
  _mm_storeu_si128((__m128i *)(s - 3 + 5 * stride), x5);
  _mm_storeu_si128((__m128i *)(s - 3 + 6 * stride), x6);
  _mm_storeu_si128((__m128i *)(s - 3 + 7 * stride), x7);

  return;
}

void highbd_filt_horz_pred_avx2(uint16_t *s, int stride, int bd,
                                uint16_t q_thresh, uint16_t side_thresh,
                                int q_mult, int w_mult, int n, int filt_len) {
  if (n == 0 || filt_len == 0) return;
  __m128i p0 = _mm_loadu_si128((__m128i *)(s + 0 * stride));
  __m128i p1 = _mm_loadu_si128((__m128i *)(s + 1 * stride));
  __m128i p2 = _mm_loadu_si128((__m128i *)(s + 2 * stride));
  __m128i m0 = _mm_loadu_si128((__m128i *)(s - 1 * stride));
  __m128i m1 = _mm_loadu_si128((__m128i *)(s - 2 * stride));
  __m128i m2 = _mm_loadu_si128((__m128i *)(s - 3 * stride));

  highbd_filt_pred_generic_avx2(&p2, &p1, &p0, &m2, &m1, &m0, bd, q_thresh,
                                side_thresh, q_mult, w_mult);

  _mm_storeu_si128((__m128i *)(s + 0 * stride), p0);
  _mm_storeu_si128((__m128i *)(s + 1 * stride), p1);
  _mm_storeu_si128((__m128i *)(s + 2 * stride), p2);
  _mm_storeu_si128((__m128i *)(s - 1 * stride), m0);
  _mm_storeu_si128((__m128i *)(s - 2 * stride), m1);
  _mm_storeu_si128((__m128i *)(s - 3 * stride), m2);

  return;
}
