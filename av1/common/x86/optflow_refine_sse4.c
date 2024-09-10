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
#include <emmintrin.h>  // SSE2
#include <smmintrin.h>  /* SSE4.1 */

#include "aom/aom_integer.h"
#include "av1/common/blockd.h"
#include "av1/common/reconinter.h"
#include "aom_dsp/x86/synonyms.h"

static INLINE __m128i round_power_of_two_signed_epi64(__m128i in, int n) {
  __m128i sign_mask = _mm_srai_epi32(in, 31);
  sign_mask = _mm_or_si128(sign_mask, _mm_srli_epi64(sign_mask, 4));
  __m128i abs_vec = _mm_xor_si128(in, sign_mask);
  abs_vec = _mm_sub_epi64(abs_vec, sign_mask);

  __m128i rounding_offset = _mm_set_epi32(0, (1 << n) >> 1, 0, (1 << n) >> 1);
  __m128i add_vec = _mm_add_epi64(abs_vec, rounding_offset);
  __m128i rounded_vec = _mm_srli_epi64(add_vec, n);

  // Restore the sign
  rounded_vec = _mm_xor_si128(rounded_vec, sign_mask);
  rounded_vec = _mm_sub_epi64(rounded_vec, sign_mask);
  return rounded_vec;
}

static INLINE __m128i pack_and_round_epi32(__m128i temp1, __m128i temp2,
                                           const __m128i v_bias_d,
                                           const __m128i ones, const int bits) {
  __m128i v_sign_d = _mm_sign_epi32(ones, temp1);
  __m128i reg = _mm_mullo_epi32(temp1, v_sign_d);
  reg = _mm_srli_epi32(_mm_add_epi32(reg, v_bias_d), bits);
  temp1 = _mm_mullo_epi32(reg, v_sign_d);

  v_sign_d = _mm_sign_epi32(ones, temp2);
  reg = _mm_mullo_epi32(temp2, v_sign_d);
  reg = _mm_srli_epi32(_mm_add_epi32(reg, v_bias_d), bits);
  temp2 = _mm_mullo_epi32(reg, v_sign_d);
  return (_mm_packs_epi32(temp1, temp2));
}

void av1_bicubic_grad_interpolation_highbd_sse4_1(const int16_t *pred_src,
                                                  int16_t *x_grad,
                                                  int16_t *y_grad, const int bw,
                                                  const int bh) {
#if OPFL_BICUBIC_GRAD
  assert(bw % 8 == 0);
  assert(bh % 8 == 0);

  __m128i coeff_bi[4][2];
  coeff_bi[0][0] = _mm_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
  coeff_bi[0][1] = _mm_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
  coeff_bi[1][0] = _mm_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1]);
  coeff_bi[1][1] = _mm_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1]);
  coeff_bi[2][0] = _mm_insert_epi32(
      coeff_bi[0][0], coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1], 0);
  coeff_bi[2][1] = _mm_insert_epi32(
      coeff_bi[0][1], coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1], 0);
  coeff_bi[3][0] = _mm_insert_epi32(
      coeff_bi[0][0], coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1], 3);
  coeff_bi[3][1] = _mm_insert_epi32(
      coeff_bi[0][1], coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1], 3);
  const __m128i v_bias_d = _mm_set1_epi32((1 << bicubic_bits) >> 1);
  const __m128i ones = _mm_set1_epi32(1);

#if OPFL_DOWNSAMP_QUINCUNX
  __m128i mask_val[2] = { _mm_set_epi32(0, 1, 0, 1),
                          _mm_set_epi32(1, 0, 1, 0) };
#endif

  if (bw < 16) {
    for (int col = 0; col < bh; col++) {
      const int is_y_boundary = (col + 1 > bh - 1 || col - 1 < 0);
      const int id_prev1 = AOMMAX(col - 1, 0);
      const int id_prev2 = AOMMAX(col - 2, 0);
      const int id_next1 = AOMMIN(col + 1, bh - 1);
      const int id_next2 = AOMMIN(col + 2, bh - 1);

#if OPFL_DOWNSAMP_QUINCUNX
      __m128i mask = mask_val[col & 0x1];
#endif
      for (int row = 0; row < bw; row += 8) {
        __m128i vpred_next1, vpred_prev1, vpred_next2, vpred_prev2;
        __m128i temp1, temp2, sub1, sub2, sub3, sub4;
        const int16_t *src = &pred_src[col * bw + row];
        vpred_prev1 =
            _mm_set_epi16(*(src + 6), *(src + 5), *(src + 4), *(src + 3),
                          *(src + 2), *(src + 1), *src, *src);
        vpred_prev2 = _mm_set_epi16(*(src + 5), *(src + 4), *(src + 3),
                                    *(src + 2), *(src + 1), *src, *src, *src);
        vpred_next1 =
            _mm_set_epi16(*(src + 7), *(src + 7), *(src + 6), *(src + 5),
                          *(src + 4), *(src + 3), *(src + 2), *(src + 1));
        vpred_next2 =
            _mm_set_epi16(*(src + 7), *(src + 7), *(src + 7), *(src + 6),
                          *(src + 5), *(src + 4), *(src + 3), *(src + 2));

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1),
                             _mm_cvtepi16_epi32(vpred_prev1));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2),
                             _mm_cvtepi16_epi32(vpred_prev2));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2, 8)));

        temp1 = _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[2][0]),
                              _mm_mullo_epi32(sub3, coeff_bi[2][1]));
        temp2 = _mm_add_epi32(_mm_mullo_epi32(sub2, coeff_bi[3][0]),
                              _mm_mullo_epi32(sub4, coeff_bi[3][1]));

#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif
        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);

        const int idx = col * bw + row;
        xx_storeu_128(x_grad + idx, temp1);

        src = pred_src + row;
        vpred_prev1 = xx_loadu_128(src + id_prev1 * bw);
        vpred_prev2 = xx_loadu_128(src + id_prev2 * bw);
        vpred_next1 = xx_loadu_128(src + id_next1 * bw);
        vpred_next2 = xx_loadu_128(src + id_next2 * bw);

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1),
                             _mm_cvtepi16_epi32(vpred_prev1));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2),
                             _mm_cvtepi16_epi32(vpred_prev2));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2, 8)));

        temp1 =
            _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub3, coeff_bi[is_y_boundary][1]));
        temp2 =
            _mm_add_epi32(_mm_mullo_epi32(sub2, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub4, coeff_bi[is_y_boundary][1]));

#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif
        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);
        xx_storeu_128(y_grad + idx, temp1);
      }
    }
  } else {
    for (int col = 0; col < bh; col++) {
      const int is_y_boundary = (col + 1 > bh - 1 || col - 1 < 0);
      const int id_prev = AOMMAX(col - 1, 0);
      const int id_prev2 = AOMMAX(col - 2, 0);
      const int id_next = AOMMIN(col + 1, bh - 1);
      const int id_next2 = AOMMIN(col + 2, bh - 1);
#if OPFL_DOWNSAMP_QUINCUNX
      __m128i mask = mask_val[col & 0x1];
#endif
      for (int row = 0; row < bw; row += 16) {
        __m128i vpred_next1_1, vpred_prev1_1, vpred_next2_1, vpred_prev2_1;
        __m128i vpred_next1_2, vpred_prev1_2, vpred_next2_2, vpred_prev2_2;
        __m128i temp1, temp2;
        __m128i sub1, sub2, sub3, sub4;

        const int16_t *src = &pred_src[col * bw + row];

        if (row - 1 < 0) {
          vpred_prev1_1 =
              _mm_set_epi16(*(src + 6), *(src + 5), *(src + 4), *(src + 3),
                            *(src + 2), *(src + 1), *src, *src);
          vpred_prev2_1 =
              _mm_set_epi16(*(src + 5), *(src + 4), *(src + 3), *(src + 2),
                            *(src + 1), *src, *src, *src);
        } else {
          vpred_prev1_1 = xx_loadu_128((__m128i *)(src - 1));
          vpred_prev2_1 = xx_loadu_128((__m128i *)(src - 2));
        }
        if (row + 16 > bw - 1) {
          vpred_next1_2 =
              _mm_set_epi16(*(src + 15), *(src + 15), *(src + 14), *(src + 13),
                            *(src + 12), *(src + 11), *(src + 10), *(src + 9));
          vpred_next2_2 =
              _mm_set_epi16(*(src + 15), *(src + 15), *(src + 15), *(src + 14),
                            *(src + 13), *(src + 12), *(src + 11), *(src + 10));
        } else {
          vpred_next1_2 = xx_loadu_128(src + 9);
          vpred_next2_2 = xx_loadu_128(src + 10);
        }
        vpred_prev1_2 = xx_loadu_128(src + 7);
        vpred_prev2_2 = xx_loadu_128(src + 6);
        vpred_next1_1 = xx_loadu_128(src + 1);
        vpred_next2_1 = xx_loadu_128(src + 2);

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1_1),
                             _mm_cvtepi16_epi32(vpred_prev1_1));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1_1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1_1, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2_1),
                             _mm_cvtepi16_epi32(vpred_prev2_1));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2_1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2_1, 8)));

        const int is_left_boundary = row - 1 < 0 ? 2 : 0;
        const int is_right_boundary = row + 16 > bw - 1 ? 3 : 0;
        temp1 =
            _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[is_left_boundary][0]),
                          _mm_mullo_epi32(sub3, coeff_bi[is_left_boundary][1]));
        temp2 = _mm_add_epi32(_mm_mullo_epi32(sub2, coeff_bi[0][0]),
                              _mm_mullo_epi32(sub4, coeff_bi[0][1]));

#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif
        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);

        const int idx = col * bw + row;
        xx_storeu_128(x_grad + idx, temp1);

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1_2),
                             _mm_cvtepi16_epi32(vpred_prev1_2));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1_2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1_2, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2_2),
                             _mm_cvtepi16_epi32(vpred_prev2_2));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2_2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2_2, 8)));

        temp1 = _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[0][0]),
                              _mm_mullo_epi32(sub3, coeff_bi[0][1]));
        temp2 = _mm_add_epi32(
            _mm_mullo_epi32(sub2, coeff_bi[is_right_boundary][0]),
            _mm_mullo_epi32(sub4, coeff_bi[is_right_boundary][1]));

#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif
        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);
        xx_storeu_128(x_grad + idx + 8, temp1);

        src = pred_src + row;
        vpred_prev1_1 = xx_loadu_128(src + bw * id_prev);
        vpred_prev2_1 = xx_loadu_128(src + bw * id_prev2);
        vpred_next1_1 = xx_loadu_128(src + id_next * bw);
        vpred_next2_1 = xx_loadu_128(src + id_next2 * bw);

        vpred_prev1_2 = xx_loadu_128(src + bw * id_prev + 8);
        vpred_prev2_2 = xx_loadu_128(src + bw * id_prev2 + 8);
        vpred_next1_2 = xx_loadu_128(src + id_next * bw + 8);
        vpred_next2_2 = xx_loadu_128(src + id_next2 * bw + 8);

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1_1),
                             _mm_cvtepi16_epi32(vpred_prev1_1));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1_1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1_1, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2_1),
                             _mm_cvtepi16_epi32(vpred_prev2_1));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2_1, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2_1, 8)));

        temp1 =
            _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub3, coeff_bi[is_y_boundary][1]));
        temp2 =
            _mm_add_epi32(_mm_mullo_epi32(sub2, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub4, coeff_bi[is_y_boundary][1]));
#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif

        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);
        xx_storeu_128(y_grad + idx, temp1);

        sub1 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next1_2),
                             _mm_cvtepi16_epi32(vpred_prev1_2));
        sub2 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next1_2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev1_2, 8)));
        sub3 = _mm_sub_epi32(_mm_cvtepi16_epi32(vpred_next2_2),
                             _mm_cvtepi16_epi32(vpred_prev2_2));
        sub4 =
            _mm_sub_epi32(_mm_cvtepi16_epi32(_mm_srli_si128(vpred_next2_2, 8)),
                          _mm_cvtepi16_epi32(_mm_srli_si128(vpred_prev2_2, 8)));
        temp1 =
            _mm_add_epi32(_mm_mullo_epi32(sub1, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub3, coeff_bi[is_y_boundary][1]));
        temp2 =
            _mm_add_epi32(_mm_mullo_epi32(sub2, coeff_bi[is_y_boundary][0]),
                          _mm_mullo_epi32(sub4, coeff_bi[is_y_boundary][1]));
#if OPFL_DOWNSAMP_QUINCUNX
        temp1 = _mm_mullo_epi32(temp1, mask);
        temp2 = _mm_mullo_epi32(temp2, mask);
#endif
        temp1 =
            pack_and_round_epi32(temp1, temp2, v_bias_d, ones, bicubic_bits);
        xx_storeu_128(y_grad + idx + 8, temp1);
      }
    }
  }
#else
  (void)pred_src;
  (void)x_grad;
  (void)y_grad;
  (void)bw;
  (void)bh;
#endif  // OPFL_BICUBIC_GRAD
}

static INLINE __m128i LoadUnaligned16(const void *a) {
  return _mm_loadu_si128((const __m128i *)a);
}

#if OPFL_DOWNSAMP_QUINCUNX
static INLINE __m128i LoadAligned16(const void *a) {
  return _mm_load_si128((const __m128i *)a);
}

static AOM_FORCE_INLINE void down_sample(
    __m128i *gradX0, __m128i *gradX1, __m128i *gradY0, __m128i *gradY1,
    __m128i *pred0, __m128i *pred1, const __m128i *pred0_odd,
    const __m128i *pred1_odd, const int16_t *gx0, const int16_t *gx1,
    const int16_t *gy0, const int16_t *gy1, int gstride) {
  const __m128i odd = _mm_set_epi16(0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0);
  const __m128i even =
      _mm_set_epi16(0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF);

  const __m128i gradX01 = LoadAligned16(gx0 + gstride);
  const __m128i gradX11 = LoadAligned16(gx1 + gstride);
  const __m128i gradY01 = LoadAligned16(gy0 + gstride);
  const __m128i gradY11 = LoadAligned16(gy1 + gstride);

  gradX0[0] =
      _mm_or_si128(_mm_and_si128(gradX0[0], even), _mm_and_si128(gradX01, odd));
  gradX1[0] =
      _mm_or_si128(_mm_and_si128(gradX1[0], even), _mm_and_si128(gradX11, odd));
  gradY0[0] =
      _mm_or_si128(_mm_and_si128(gradY0[0], even), _mm_and_si128(gradY01, odd));
  gradY1[0] =
      _mm_or_si128(_mm_and_si128(gradY1[0], even), _mm_and_si128(gradY11, odd));

  pred0[0] = _mm_or_si128(_mm_and_si128(pred0[0], even),
                          _mm_and_si128(pred0_odd[0], odd));
  pred1[0] = _mm_or_si128(_mm_and_si128(pred1[0], even),
                          _mm_and_si128(pred1_odd[0], odd));
}
#endif  // OPFL_DOWNSAMP_QUINCUNX

void calc_mv_process(int64_t su2, int64_t sv2, int64_t suv, int64_t suw,
                     int64_t svw, const int d0, const int d1, const int bits,
                     const int rls_alpha, int *vx0, int *vy0, int *vx1,
                     int *vy1) {
#if OPFL_REGULARIZED_LS
  su2 += rls_alpha;
  sv2 += rls_alpha;
#else
  (void)rls_alpha;
#endif

  // Solve 2x2 matrix inverse: [ su2  suv ]   [ vx0 ]     [ -suw ]
  //                           [ suv  sv2 ] * [ vy0 ]  =  [ -svw ]
  int shifts[2] = { bits, bits };
  int msb_su2 = 1 + get_msb_signed_64(su2);
  int msb_sv2 = 1 + get_msb_signed_64(sv2);
  int msb_suv = 1 + get_msb_signed_64(suv);
  int msb_suw = 1 + get_msb_signed_64(suw);
  int msb_svw = 1 + get_msb_signed_64(svw);
  // Make sure the max bit depth of det, sol[0], and sol[1] are within
  // MAX_LS_BITS
  int max_mult_msb = AOMMAX(
      msb_su2 + msb_sv2, AOMMAX(AOMMAX(msb_sv2 + msb_suw, msb_suv + msb_svw),
                                AOMMAX(msb_su2 + msb_svw, msb_suv + msb_suw)));
  int redbit = AOMMAX(0, max_mult_msb - MAX_LS_BITS + 3) >> 1;

  su2 = ROUND_POWER_OF_TWO_SIGNED_64(su2, redbit);
  sv2 = ROUND_POWER_OF_TWO_SIGNED_64(sv2, redbit);
  suv = ROUND_POWER_OF_TWO_SIGNED_64(suv, redbit);
  suw = ROUND_POWER_OF_TWO_SIGNED_64(suw, redbit);
  svw = ROUND_POWER_OF_TWO_SIGNED_64(svw, redbit);
  const int64_t det = su2 * sv2 - suv * suv;
  if (det <= 0) {
    *vx0 = 0;
    *vy0 = 0;
    *vx1 = 0;
    *vy1 = 0;
    return;
  }

  int64_t sol[2] = { sv2 * suw - suv * svw, su2 * svw - suv * suw };

  divide_and_round_array(sol, det, 2, shifts);
  *vx0 = (int)-sol[0];
  *vy0 = (int)-sol[1];
  *vx1 = (*vx0) * d1;
  *vy1 = (*vy0) * d1;
  *vx0 = (*vx0) * d0;
  *vy0 = (*vy0) * d0;
}

static AOM_FORCE_INLINE void multiply_and_accum(
    __m128i a_lo_0, __m128i b_lo_0, __m128i a_hi_0, __m128i b_hi_0,
    __m128i a_lo1, __m128i b_lo1, __m128i a_hi1, __m128i b_hi1,
    const int grad_bits_lo, const int grad_bits_hi, __m128i *t1, __m128i *t2) {
  const __m128i reg_lo_0 = round_power_of_two_signed_epi64(
      _mm_mul_epi32(a_lo_0, b_lo_0), grad_bits_lo);
  const __m128i reg_hi_0 = round_power_of_two_signed_epi64(
      _mm_mul_epi32(a_hi_0, b_hi_0), grad_bits_hi);
  const __m128i reg_lo1 = round_power_of_two_signed_epi64(
      _mm_mul_epi32(a_lo1, b_lo1), grad_bits_lo);
  const __m128i reg_hi1 = round_power_of_two_signed_epi64(
      _mm_mul_epi32(a_hi1, b_hi1), grad_bits_hi);
  *t1 = _mm_add_epi64(reg_lo_0, reg_lo1);
  *t2 = _mm_add_epi64(reg_hi_0, reg_hi1);
}

static void opfl_mv_refinement_8x4_sse4_1(const int16_t *pdiff, int pstride,
                                          const int16_t *gx, const int16_t *gy,
                                          int gstride, int d0, int d1,
                                          int grad_prec_bits, int mv_prec_bits,
                                          int *vx0, int *vy0, int *vx1,
                                          int *vy1) {
  int bHeight = 4;
  const int bits = mv_prec_bits + grad_prec_bits;
  const int rls_alpha = OPFL_RLS_PARAM;
  __m128i u2_lo, v2_lo, uv_lo, uw_lo, vw_lo;
  __m128i u2_hi, v2_hi, uv_hi, uw_hi, vw_hi;
  int64_t su2_hi = 0;
  int64_t sv2_hi = 0;
  int64_t suv_hi = 0;
  int64_t suw_hi = 0;
  int64_t svw_hi = 0;
  int64_t su2_lo = 0;
  int64_t sv2_lo = 0;
  int64_t suv_lo = 0;
  int64_t suw_lo = 0;
  int64_t svw_lo = 0;
  int grad_bits_lo = 0;
  int grad_bits_hi = 0;
  __m128i opfl_samp_min = _mm_set1_epi16(-OPFL_SAMP_CLAMP_VAL);
  __m128i opfl_samp_max = _mm_set1_epi16(OPFL_SAMP_CLAMP_VAL);
#if OPFL_DOWNSAMP_QUINCUNX
  const __m128i even_row =
      _mm_set_epi16(0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF);
  const __m128i odd_row =
      _mm_set_epi16(0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0);
#endif
  do {
    __m128i gradX = LoadUnaligned16(gx);
    __m128i gradY = LoadUnaligned16(gy);
    __m128i pred = LoadUnaligned16(pdiff);
#if OPFL_DOWNSAMP_QUINCUNX
    const __m128i gradX1 = LoadUnaligned16(gx + gstride);
    const __m128i gradY1 = LoadUnaligned16(gy + gstride);
    const __m128i pred1 = LoadUnaligned16(pdiff + pstride);
    gradX = _mm_or_si128(_mm_and_si128(gradX, even_row),
                         _mm_and_si128(gradX1, odd_row));
    gradY = _mm_or_si128(_mm_and_si128(gradY, even_row),
                         _mm_and_si128(gradY1, odd_row));
    pred = _mm_or_si128(_mm_and_si128(pred, even_row),
                        _mm_and_si128(pred1, odd_row));
#endif
    // The precision of gx, gy and pred (i.e. d0*p0-d1*p1) buffers is signed
    // 16bit and there are cases where these buffers can be filled with extreme
    // values. Hence, the accumulation here needs to be done at 64-bit precision
    // to avoid overflow issues.
    gradX = _mm_max_epi16(_mm_min_epi16(gradX, opfl_samp_max), opfl_samp_min);
    gradY = _mm_max_epi16(_mm_min_epi16(gradY, opfl_samp_max), opfl_samp_min);
    pred = _mm_max_epi16(_mm_min_epi16(pred, opfl_samp_max), opfl_samp_min);
    const __m128i gradX_lo_0 = _mm_cvtepi16_epi32(gradX);
    const __m128i gradY_lo_0 = _mm_cvtepi16_epi32(gradY);
    const __m128i pred_lo_0 = _mm_cvtepi16_epi32(pred);
    const __m128i gradX_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(gradX, 8));
    const __m128i gradY_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(gradY, 8));
    const __m128i pred_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(pred, 8));

    const __m128i gradX_lo1 = _mm_srli_si128(gradX_lo_0, 4);
    const __m128i gradX_hi1 = _mm_srli_si128(gradX_hi_0, 4);
    const __m128i gradY_lo1 = _mm_srli_si128(gradY_lo_0, 4);
    const __m128i gradY_hi1 = _mm_srli_si128(gradY_hi_0, 4);
    const __m128i pred_lo1 = _mm_srli_si128(pred_lo_0, 4);
    const __m128i pred_hi1 = _mm_srli_si128(pred_hi_0, 4);

    multiply_and_accum(gradX_lo_0, gradX_lo_0, gradX_hi_0, gradX_hi_0,
                       gradX_lo1, gradX_lo1, gradX_hi1, gradX_hi1, grad_bits_lo,
                       grad_bits_hi, &u2_lo, &u2_hi);
    multiply_and_accum(gradY_lo_0, gradY_lo_0, gradY_hi_0, gradY_hi_0,
                       gradY_lo1, gradY_lo1, gradY_hi1, gradY_hi1, grad_bits_lo,
                       grad_bits_hi, &v2_lo, &v2_hi);
    multiply_and_accum(gradX_lo_0, gradY_lo_0, gradX_hi_0, gradY_hi_0,
                       gradX_lo1, gradY_lo1, gradX_hi1, gradY_hi1, grad_bits_lo,
                       grad_bits_hi, &uv_lo, &uv_hi);
    multiply_and_accum(gradX_lo_0, pred_lo_0, gradX_hi_0, pred_hi_0, gradX_lo1,
                       pred_lo1, gradX_hi1, pred_hi1, grad_bits_lo,
                       grad_bits_hi, &uw_lo, &uw_hi);
    multiply_and_accum(gradY_lo_0, pred_lo_0, gradY_hi_0, pred_hi_0, gradY_lo1,
                       pred_lo1, gradY_hi1, pred_hi1, grad_bits_lo,
                       grad_bits_hi, &vw_lo, &vw_hi);

#if OPFL_DOWNSAMP_QUINCUNX
    gx += gstride << 1;
    gy += gstride << 1;
    pdiff += pstride << 1;
    bHeight -= 2;
#else
    gx += gstride;
    gy += gstride;
    pdiff += pstride;
    bHeight -= 1;
#endif
    int64_t temp;
    xx_storel_64(&temp, _mm_add_epi64(u2_lo, _mm_srli_si128(u2_lo, 8)));
    su2_lo += temp;
    xx_storel_64(&temp, _mm_add_epi64(v2_lo, _mm_srli_si128(v2_lo, 8)));
    sv2_lo += temp;
    xx_storel_64(&temp, _mm_add_epi64(uv_lo, _mm_srli_si128(uv_lo, 8)));
    suv_lo += temp;
    xx_storel_64(&temp, _mm_add_epi64(uw_lo, _mm_srli_si128(uw_lo, 8)));
    suw_lo += temp;
    xx_storel_64(&temp, _mm_add_epi64(vw_lo, _mm_srli_si128(vw_lo, 8)));
    svw_lo += temp;
    xx_storel_64(&temp, _mm_add_epi64(u2_hi, _mm_srli_si128(u2_hi, 8)));
    su2_hi += temp;
    xx_storel_64(&temp, _mm_add_epi64(v2_hi, _mm_srli_si128(v2_hi, 8)));
    sv2_hi += temp;
    xx_storel_64(&temp, _mm_add_epi64(uv_hi, _mm_srli_si128(uv_hi, 8)));
    suv_hi += temp;
    xx_storel_64(&temp, _mm_add_epi64(uw_hi, _mm_srli_si128(uw_hi, 8)));
    suw_hi += temp;
    xx_storel_64(&temp, _mm_add_epi64(vw_hi, _mm_srli_si128(vw_hi, 8)));
    svw_hi += temp;
    // Do a range check and add a downshift if range is getting close to the bit
    // depth cap.
    if (bHeight % 2 == 0) {
      if (get_msb_signed_64(AOMMAX(AOMMAX(su2_lo, sv2_lo),
                                   AOMMAX(llabs(suw_lo), llabs(svw_lo)))) >=
          MAX_OPFL_AUTOCORR_BITS - 2) {
        su2_lo = ROUND_POWER_OF_TWO_SIGNED_64(su2_lo, 1);
        sv2_lo = ROUND_POWER_OF_TWO_SIGNED_64(sv2_lo, 1);
        suv_lo = ROUND_POWER_OF_TWO_SIGNED_64(suv_lo, 1);
        suw_lo = ROUND_POWER_OF_TWO_SIGNED_64(suw_lo, 1);
        svw_lo = ROUND_POWER_OF_TWO_SIGNED_64(svw_lo, 1);
        grad_bits_lo++;
      }
      if (get_msb_signed_64(AOMMAX(AOMMAX(su2_hi, sv2_hi),
                                   AOMMAX(llabs(suw_hi), llabs(svw_hi)))) >=
          MAX_OPFL_AUTOCORR_BITS - 2) {
        su2_hi = ROUND_POWER_OF_TWO_SIGNED_64(su2_hi, 1);
        sv2_hi = ROUND_POWER_OF_TWO_SIGNED_64(sv2_hi, 1);
        suv_hi = ROUND_POWER_OF_TWO_SIGNED_64(suv_hi, 1);
        suw_hi = ROUND_POWER_OF_TWO_SIGNED_64(suw_hi, 1);
        svw_hi = ROUND_POWER_OF_TWO_SIGNED_64(svw_hi, 1);
        grad_bits_hi++;
      }
    }
  } while (bHeight != 0);

  calc_mv_process(su2_lo, sv2_lo, suv_lo, suw_lo, svw_lo, d0, d1, bits,
                  rls_alpha, vx0, vy0, vx1, vy1);
  calc_mv_process(su2_hi, sv2_hi, suv_hi, suw_hi, svw_hi, d0, d1, bits,
                  rls_alpha, vx0 + 1, vy0 + 1, vx1 + 1, vy1 + 1);
}

static void opfl_mv_refinement_8x8_sse4_1(const int16_t *pdiff, int pstride,
                                          const int16_t *gx, const int16_t *gy,
                                          int gstride, int d0, int d1,
                                          int grad_prec_bits, int mv_prec_bits,
                                          int *vx0, int *vy0, int *vx1,
                                          int *vy1) {
  int bHeight = 8;
  const int rls_alpha = 4 * OPFL_RLS_PARAM;
  const int bits = mv_prec_bits + grad_prec_bits;
  __m128i u2, v2, uv, uw, vw;
  int64_t su2 = 0;
  int64_t sv2 = 0;
  int64_t suv = 0;
  int64_t suw = 0;
  int64_t svw = 0;
  int grad_bits = 0;
  __m128i opfl_samp_min = _mm_set1_epi16(-OPFL_SAMP_CLAMP_VAL);
  __m128i opfl_samp_max = _mm_set1_epi16(OPFL_SAMP_CLAMP_VAL);
#if OPFL_DOWNSAMP_QUINCUNX
  const __m128i even_row =
      _mm_set_epi16(0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF);
  const __m128i odd_row =
      _mm_set_epi16(0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0);
#endif
  do {
    __m128i gradX = LoadUnaligned16(gx);
    __m128i gradY = LoadUnaligned16(gy);
    __m128i pred = LoadUnaligned16(pdiff);
#if OPFL_DOWNSAMP_QUINCUNX
    const __m128i gradX1 = LoadUnaligned16(gx + gstride);
    const __m128i gradY1 = LoadUnaligned16(gy + gstride);
    const __m128i pred1 = LoadUnaligned16(pdiff + pstride);
    gradX = _mm_or_si128(_mm_and_si128(gradX, even_row),
                         _mm_and_si128(gradX1, odd_row));
    gradY = _mm_or_si128(_mm_and_si128(gradY, even_row),
                         _mm_and_si128(gradY1, odd_row));
    pred = _mm_or_si128(_mm_and_si128(pred, even_row),
                        _mm_and_si128(pred1, odd_row));
#endif
    // The precision of gx, gy and pred (i.e. d0*p0-d1*p1) buffers is signed
    // 16bit and there are cases where these buffers can be filled with extreme
    // values. Hence, the accumulation here needs to be done at 64bit to avoid
    // overflow issues.
    gradX = _mm_max_epi16(_mm_min_epi16(gradX, opfl_samp_max), opfl_samp_min);
    gradY = _mm_max_epi16(_mm_min_epi16(gradY, opfl_samp_max), opfl_samp_min);
    pred = _mm_max_epi16(_mm_min_epi16(pred, opfl_samp_max), opfl_samp_min);
    const __m128i gradX_lo_0 = _mm_cvtepi16_epi32(gradX);
    const __m128i gradY_lo_0 = _mm_cvtepi16_epi32(gradY);
    const __m128i pred_lo_0 = _mm_cvtepi16_epi32(pred);
    const __m128i gradX_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(gradX, 8));
    const __m128i gradY_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(gradY, 8));
    const __m128i pred_hi_0 = _mm_cvtepi16_epi32(_mm_srli_si128(pred, 8));

    const __m128i gradX_lo1 = _mm_srli_si128(gradX_lo_0, 4);
    const __m128i gradX_hi1 = _mm_srli_si128(gradX_hi_0, 4);
    const __m128i gradY_lo1 = _mm_srli_si128(gradY_lo_0, 4);
    const __m128i gradY_hi1 = _mm_srli_si128(gradY_hi_0, 4);
    const __m128i pred_lo1 = _mm_srli_si128(pred_lo_0, 4);
    const __m128i pred_hi1 = _mm_srli_si128(pred_hi_0, 4);
    __m128i t1, t2;

    multiply_and_accum(gradX_lo_0, gradX_lo_0, gradX_hi_0, gradX_hi_0,
                       gradX_lo1, gradX_lo1, gradX_hi1, gradX_hi1, grad_bits,
                       grad_bits, &t1, &t2);
    u2 = _mm_add_epi64(t1, t2);

    multiply_and_accum(gradY_lo_0, gradY_lo_0, gradY_hi_0, gradY_hi_0,
                       gradY_lo1, gradY_lo1, gradY_hi1, gradY_hi1, grad_bits,
                       grad_bits, &t1, &t2);
    v2 = _mm_add_epi64(t1, t2);

    multiply_and_accum(gradX_lo_0, gradY_lo_0, gradX_hi_0, gradY_hi_0,
                       gradX_lo1, gradY_lo1, gradX_hi1, gradY_hi1, grad_bits,
                       grad_bits, &t1, &t2);
    uv = _mm_add_epi64(t1, t2);

    multiply_and_accum(gradX_lo_0, pred_lo_0, gradX_hi_0, pred_hi_0, gradX_lo1,
                       pred_lo1, gradX_hi1, pred_hi1, grad_bits, grad_bits, &t1,
                       &t2);
    uw = _mm_add_epi64(t1, t2);

    multiply_and_accum(gradY_lo_0, pred_lo_0, gradY_hi_0, pred_hi_0, gradY_lo1,
                       pred_lo1, gradY_hi1, pred_hi1, grad_bits, grad_bits, &t1,
                       &t2);
    vw = _mm_add_epi64(t1, t2);
#if OPFL_DOWNSAMP_QUINCUNX
    gx += gstride << 1;
    gy += gstride << 1;
    pdiff += pstride << 1;
    bHeight -= 2;
#else
    gx += gstride;
    gy += gstride;
    pdiff += pstride;
    bHeight -= 1;
#endif
    int64_t temp;
    xx_storel_64(&temp, _mm_add_epi64(u2, _mm_srli_si128(u2, 8)));
    su2 += temp;
    xx_storel_64(&temp, _mm_add_epi64(v2, _mm_srli_si128(v2, 8)));
    sv2 += temp;
    xx_storel_64(&temp, _mm_add_epi64(uv, _mm_srli_si128(uv, 8)));
    suv += temp;
    xx_storel_64(&temp, _mm_add_epi64(uw, _mm_srli_si128(uw, 8)));
    suw += temp;
    xx_storel_64(&temp, _mm_add_epi64(vw, _mm_srli_si128(vw, 8)));
    svw += temp;
    // For every 8 pixels, do a range check and add a downshift if range is
    // getting close to the max allowed bit depth
    if (get_msb_signed_64(
            AOMMAX(AOMMAX(su2, sv2), AOMMAX(llabs(suw), llabs(svw)))) >=
        MAX_OPFL_AUTOCORR_BITS - 2) {
      su2 = ROUND_POWER_OF_TWO_SIGNED_64(su2, 1);
      sv2 = ROUND_POWER_OF_TWO_SIGNED_64(sv2, 1);
      suv = ROUND_POWER_OF_TWO_SIGNED_64(suv, 1);
      suw = ROUND_POWER_OF_TWO_SIGNED_64(suw, 1);
      svw = ROUND_POWER_OF_TWO_SIGNED_64(svw, 1);
      grad_bits++;
    }
  } while (bHeight != 0);

  calc_mv_process(su2, sv2, suv, suw, svw, d0, d1, bits, rls_alpha, vx0, vy0,
                  vx1, vy1);
}

static AOM_INLINE void opfl_mv_refinement_sse4_1(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int bw, int bh, int d0, int d1, int grad_prec_bits,
    int mv_prec_bits, int *vx0, int *vy0, int *vx1, int *vy1) {
  (void)bh;
  if (bw == 4)
    opfl_mv_refinement_8x4_sse4_1(pdiff, pstride, gx, gy, gstride, d0, d1,
                                  grad_prec_bits, mv_prec_bits, vx0, vy0, vx1,
                                  vy1);
  else
    opfl_mv_refinement_8x8_sse4_1(pdiff, pstride, gx, gy, gstride, d0, d1,
                                  grad_prec_bits, mv_prec_bits, vx0, vy0, vx1,
                                  vy1);
}

// Function to compute optical flow offsets in nxn blocks
int av1_opfl_mv_refinement_nxn_sse4_1(const int16_t *pdiff, int pstride,
                                      const int16_t *gx, const int16_t *gy,
                                      int gstride, int bw, int bh, int n,
                                      int d0, int d1, int grad_prec_bits,
                                      int mv_prec_bits, int *vx0, int *vy0,
                                      int *vx1, int *vy1) {
  assert(bw % n == 0 && bh % n == 0);
  int n_blocks = 0;
  for (int i = 0; i < bh; i += n) {
    for (int j = 0; j < bw; j += 8) {
      opfl_mv_refinement_sse4_1(pdiff + (i * pstride + j), pstride,
                                gx + (i * gstride + j), gy + (i * gstride + j),
                                gstride, n, n, d0, d1, grad_prec_bits,
                                mv_prec_bits, vx0 + n_blocks, vy0 + n_blocks,
                                vx1 + n_blocks, vy1 + n_blocks);
      n_blocks += (n == 4) ? 2 : 1;
    }
  }
  return n_blocks;
}

// This round shift function has only been tested for the case d0 = 1, d1 = -1
// that is used in CONFIG_OPFL_MV_SEARCH. To use centered=1 option for more
// general d0 and d1, this function needs to be extended.
static INLINE __m128i round_power_of_two_signed_epi16(__m128i temp1,
                                                      const int bits) {
  __m128i ones = _mm_set1_epi16(1);
  __m128i v_sign_d = _mm_sign_epi16(ones, temp1);
  __m128i v_bias_d = _mm_set1_epi16((1 << bits) >> 1);
  __m128i reg = _mm_mullo_epi16(temp1, v_sign_d);
  reg = _mm_srli_epi16(_mm_add_epi16(reg, v_bias_d), bits);
  return _mm_mullo_epi16(reg, v_sign_d);
}

static AOM_FORCE_INLINE void compute_pred_using_interp_grad_highbd_sse4_1(
    const uint16_t *src1, const uint16_t *src2, int16_t *dst1, int16_t *dst2,
    int bw, int bh, int d0, int d1, int centered) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i mul_one = _mm_set1_epi16(1);
  const __m128i mul1 = _mm_set1_epi16(d0);
  const __m128i mul2 = _mm_sub_epi16(zero, _mm_set1_epi16(d1));
  const __m128i mul_val1 = _mm_unpacklo_epi16(mul1, mul2);
  const __m128i mul_val2 =
      _mm_unpacklo_epi16(mul_one, _mm_sub_epi16(zero, mul_one));

  for (int i = 0; i < bh; i++) {
    const uint16_t *inp1 = src1 + i * bw;
    const uint16_t *inp2 = src2 + i * bw;
    int16_t *out1 = dst1 + i * bw;
    int16_t *out2 = dst2 ? (dst2 + i * bw) : NULL;
    for (int j = 0; j < bw; j = j + 8) {
      const __m128i src_buf1 = xx_load_128(inp1 + j);
      const __m128i src_buf2 = xx_load_128(inp2 + j);

      __m128i temp1, temp2;
      __m128i reg1 = _mm_unpacklo_epi16(src_buf1, src_buf2);
      __m128i reg2 = _mm_unpackhi_epi16(src_buf1, src_buf2);

      temp1 = _mm_madd_epi16(reg1, mul_val1);
      temp2 = _mm_madd_epi16(reg2, mul_val1);
      temp1 = _mm_packs_epi32(temp1, temp2);
      if (centered) temp1 = round_power_of_two_signed_epi16(temp1, 1);

      reg1 = _mm_madd_epi16(reg1, mul_val2);
      reg2 = _mm_madd_epi16(reg2, mul_val2);
      temp2 = _mm_packs_epi32(reg1, reg2);

      xx_store_128(out1 + j, temp1);
      if (dst2) xx_store_128(out2 + j, temp2);
    }
  }
}

void av1_copy_pred_array_highbd_sse4_1(const uint16_t *src1,
                                       const uint16_t *src2, int16_t *dst1,
                                       int16_t *dst2, int bw, int bh, int d0,
                                       int d1, int centered) {
  compute_pred_using_interp_grad_highbd_sse4_1(src1, src2, dst1, dst2, bw, bh,
                                               d0, d1, centered);
}
