/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
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

#include "config/aom_config.h"

#include "av1/common/reconinter.h"

static INLINE void sign_extend_16bit_to_32bit(__m256i in, __m256i zero,
                                              __m256i *out_lo,
                                              __m256i *out_hi) {
  const __m256i sign_bits = _mm256_cmpgt_epi16(zero, in);
  *out_lo = _mm256_unpacklo_epi16(in, sign_bits);
  *out_hi = _mm256_unpackhi_epi16(in, sign_bits);
}

static INLINE __m256i round_power_of_two_epi32(__m256i in, int reduce_bits) {
  __m256i rounding_offset = _mm256_set1_epi32((1 << (reduce_bits)) >> 1);

  // add_vec = vec + (2 ^ n) / 2
  __m256i add_vec = _mm256_add_epi32(in, rounding_offset);

  // round_vec = add_vec >> n
  __m256i rounded_vec = _mm256_srai_epi32(add_vec, reduce_bits);
  return rounded_vec;
}

static INLINE __m256i round_power_of_two_signed_epi32(__m256i in,
                                                      int reduce_bits) {
  // Create a mask for the sign bits of the input vector
  __m256i sign_mask = _mm256_srai_epi32(in, 31);

  __m256i abs_vec = _mm256_abs_epi32(in);

  __m256i rounded_vec = round_power_of_two_epi32(abs_vec, reduce_bits);

  // Restore the sign
  rounded_vec = _mm256_xor_si256(rounded_vec, sign_mask);
  rounded_vec = _mm256_sub_epi32(rounded_vec, sign_mask);

  return rounded_vec;
}

#if CONFIG_OPFL_MV_SEARCH
static INLINE void avg_pool_pdiff_grad_8_avx2(int16_t *pdiff, const int pstride,
                                              int16_t *gx, int16_t *gy,
                                              const int gstride, const int bh) {
  for (int i = 0; i < bh; i++) {
    __m128i pd0 = _mm_loadu_si128((__m128i *)&pdiff[i * pstride]);
    __m128i gx0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
    __m128i gy0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);

    _mm_storeu_si128((__m128i *)(pdiff + (i * pstride)), pd0);
    _mm_storeu_si128((__m128i *)(gx + (i * gstride)), gx0);
    _mm_storeu_si128((__m128i *)(gy + (i * gstride)), gy0);
  }
}

static INLINE void avg_pool_pdiff_grad_8xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m128i pd0 = _mm_loadu_si128((__m128i *)&pdiff[i * pstride]);
    __m128i pd1 = _mm_loadu_si128((__m128i *)&pdiff[(i + 1) * pstride]);
    __m128i gx0 = _mm_loadu_si128((__m128i *)&gx[i * gstride]);
    __m128i gx1 = _mm_loadu_si128((__m128i *)&gx[(i + 1) * gstride]);
    __m128i gy0 = _mm_loadu_si128((__m128i *)&gy[i * gstride]);
    __m128i gy1 = _mm_loadu_si128((__m128i *)&gy[(i + 1) * gstride]);

    // Add values of two corresponding rows since scaling step_h=2
    __m256i addpd0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(pd0),
                                      _mm256_cvtepi16_epi32(pd1));
    __m256i addgx0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(gx0),
                                      _mm256_cvtepi16_epi32(gx1));
    __m256i addgy0 = _mm256_add_epi32(_mm256_cvtepi16_epi32(gy0),
                                      _mm256_cvtepi16_epi32(gy1));

    for (int m = 2; m < step_h; m++) {
      pd0 = _mm_loadu_si128((__m128i *)&pdiff[(i + m) * pstride]);
      gx0 = _mm_loadu_si128((__m128i *)&gx[(i + m) * gstride]);
      gy0 = _mm_loadu_si128((__m128i *)&gy[(i + m) * gstride]);

      addpd0 = _mm256_add_epi32(addpd0, _mm256_cvtepi16_epi32(pd0));
      addgx0 = _mm256_add_epi32(addgx0, _mm256_cvtepi16_epi32(gx0));
      addgy0 = _mm256_add_epi32(addgy0, _mm256_cvtepi16_epi32(gy0));
    }

    addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
    addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
    addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);

    _mm_storeu_si128((__m128i *)(pdiff + (k * pstride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addpd0),
                                     _mm256_extractf128_si256(addpd0, 1)));
    _mm_storeu_si128((__m128i *)(gx + (k * gstride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addgx0),
                                     _mm256_extractf128_si256(addgx0, 1)));
    _mm_storeu_si128((__m128i *)(gy + (k * gstride)),
                     _mm_packs_epi32(_mm256_castsi256_si128(addgy0),
                                     _mm256_extractf128_si256(addgy0, 1)));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_8xbh_avx2(int16_t *pdiff,
                                                 const int pstride, int16_t *gx,
                                                 int16_t *gy, const int gstride,
                                                 const int bh, int step_w,
                                                 int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_8_avx2(pdiff, pstride, gx, gy, gstride, bh);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_8xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_16_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh) {
  for (int i = 0; i < bh; i++) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);

    _mm256_storeu_si256((__m256i *)(pdiff + (i * pstride)), pd0);
    _mm256_storeu_si256((__m256i *)(gx + (i * gstride)), gx0);
    _mm256_storeu_si256((__m256i *)(gy + (i * gstride)), gy0);
  }
}

static INLINE void avg_pool_pdiff_grad_16xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd0_lo, pd1_lo, gx0_lo, gx1_lo, gy0_lo, gy1_lo;
  __m256i pd0_hi, pd1_hi, gx0_hi, gx1_hi, gy0_hi, gy1_hi;
  __m256i pd2_lo, gx2_lo, gy2_lo;
  __m256i pd2_hi, gx2_hi, gy2_hi;
  const __m256i zero = _mm256_setzero_si256();
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd0, zero, &pd0_lo, &pd0_hi);
    sign_extend_16bit_to_32bit(pd1, zero, &pd1_lo, &pd1_hi);
    sign_extend_16bit_to_32bit(gx0, zero, &gx0_lo, &gx0_hi);
    sign_extend_16bit_to_32bit(gx1, zero, &gx1_lo, &gx1_hi);
    sign_extend_16bit_to_32bit(gy0, zero, &gy0_lo, &gy0_hi);
    sign_extend_16bit_to_32bit(gy1, zero, &gy1_lo, &gy1_hi);

    // Add values of four corresponding rows since scaling step_h=4
    __m256i addpd_lo = _mm256_add_epi32(pd0_lo, pd1_lo);
    __m256i addpd_hi = _mm256_add_epi32(pd0_hi, pd1_hi);
    __m256i addgx_lo = _mm256_add_epi32(gx0_lo, gx1_lo);
    __m256i addgx_hi = _mm256_add_epi32(gx0_hi, gx1_hi);
    __m256i addgy_lo = _mm256_add_epi32(gy0_lo, gy1_lo);
    __m256i addgy_hi = _mm256_add_epi32(gy0_hi, gy1_hi);

    for (int m = 2; m < step_h; m++) {
      __m256i pd2 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride]);
      __m256i gx2 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride]);
      __m256i gy2 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd2, zero, &pd2_lo, &pd2_hi);
      sign_extend_16bit_to_32bit(gx2, zero, &gx2_lo, &gx2_hi);
      sign_extend_16bit_to_32bit(gy2, zero, &gy2_lo, &gy2_hi);

      // Add values of four corresponding rows since scaling step_h=4
      addpd_lo = _mm256_add_epi32(addpd_lo, pd2_lo);
      addpd_hi = _mm256_add_epi32(addpd_hi, pd2_hi);
      addgx_lo = _mm256_add_epi32(addgx_lo, gx2_lo);
      addgx_hi = _mm256_add_epi32(addgx_hi, gx2_hi);
      addgy_lo = _mm256_add_epi32(addgy_lo, gy2_lo);
      addgy_hi = _mm256_add_epi32(addgy_hi, gy2_hi);
    }

    addpd_lo = round_power_of_two_signed_epi32(addpd_lo, avg_bits);
    addpd_hi = round_power_of_two_signed_epi32(addpd_hi, avg_bits);
    addgx_lo = round_power_of_two_signed_epi32(addgx_lo, avg_bits);
    addgx_hi = round_power_of_two_signed_epi32(addgx_hi, avg_bits);
    addgy_lo = round_power_of_two_signed_epi32(addgy_lo, avg_bits);
    addgy_hi = round_power_of_two_signed_epi32(addgy_hi, avg_bits);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * pstride)),
                        _mm256_packs_epi32(addpd_lo, addpd_hi));
    _mm256_storeu_si256((__m256i *)(gx + (k * gstride)),
                        _mm256_packs_epi32(addgx_lo, addgx_hi));
    _mm256_storeu_si256((__m256i *)(gy + (k * gstride)),
                        _mm256_packs_epi32(addgy_lo, addgy_hi));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_16xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_16_avx2(pdiff, pstride, gx, gy, gstride, bh);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_16xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_32_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh, int step_w,
                                               int step_h) {
  int k = 0;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);
  __m256i one_mask = _mm256_set1_epi16(1);
  for (int i = 0; i < bh; i++) {
    __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[(i * pstride) + 16]);
    __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[(i * gstride) + 16]);
    __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[(i * gstride) + 16]);

    __m256i addpd0 = _mm256_madd_epi16(pd0, one_mask);
    __m256i addgx0 = _mm256_madd_epi16(gx0, one_mask);
    __m256i addgy0 = _mm256_madd_epi16(gy0, one_mask);
    __m256i addpd1 = _mm256_madd_epi16(pd1, one_mask);
    __m256i addgx1 = _mm256_madd_epi16(gx1, one_mask);
    __m256i addgy1 = _mm256_madd_epi16(gy1, one_mask);

    addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
    addpd1 = round_power_of_two_signed_epi32(addpd1, avg_bits);
    addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
    addgx1 = round_power_of_two_signed_epi32(addgx1, avg_bits);
    addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);
    addgy1 = round_power_of_two_signed_epi32(addgy1, avg_bits);

    addpd0 = _mm256_packs_epi32(addpd0, addpd1);
    addgx0 = _mm256_packs_epi32(addgx0, addgx1);
    addgy0 = _mm256_packs_epi32(addgy0, addgy1);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * pstride)),
                        _mm256_permutevar8x32_epi32(addpd0, reg_mask));
    _mm256_storeu_si256((__m256i *)(gx + (k * gstride)),
                        _mm256_permutevar8x32_epi32(addgx0, reg_mask));
    _mm256_storeu_si256((__m256i *)(gy + (k * gstride)),
                        _mm256_permutevar8x32_epi32(addgy0, reg_mask));

    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_32xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);
  int k = 0;
  for (int i = 0; i < bh; i += step_h) {
    __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride]);
    __m256i pd10 = _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride]);
    __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride]);
    __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride]);
    __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride]);
    __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd00, zero, &pd00_lo, &pd00_hi);
    sign_extend_16bit_to_32bit(pd10, zero, &pd10_lo, &pd10_hi);
    sign_extend_16bit_to_32bit(gx00, zero, &gx00_lo, &gx00_hi);
    sign_extend_16bit_to_32bit(gx10, zero, &gx10_lo, &gx10_hi);
    sign_extend_16bit_to_32bit(gy00, zero, &gy00_lo, &gy00_hi);
    sign_extend_16bit_to_32bit(gy10, zero, &gy10_lo, &gy10_hi);

    // Add values of four corresponding rows since scaling step_h=4
    __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
    __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
    __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
    __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
    __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
    __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

    __m256i pd01 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16]);
    __m256i pd11 =
        _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16]);
    __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16]);
    __m256i gx11 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16]);
    __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16]);
    __m256i gy11 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16]);

    // Sign extend 16 bit to 32 bit.
    sign_extend_16bit_to_32bit(pd01, zero, &pd01_lo, &pd01_hi);
    sign_extend_16bit_to_32bit(pd11, zero, &pd11_lo, &pd11_hi);
    sign_extend_16bit_to_32bit(gx01, zero, &gx01_lo, &gx01_hi);
    sign_extend_16bit_to_32bit(gx11, zero, &gx11_lo, &gx11_hi);
    sign_extend_16bit_to_32bit(gy01, zero, &gy01_lo, &gy01_hi);
    sign_extend_16bit_to_32bit(gy11, zero, &gy11_lo, &gy11_hi);

    __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
    __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
    __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
    __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
    __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
    __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

    for (int m = 2; m < step_h; m++) {
      pd00 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride]);
      gx00 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride]);
      gy00 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero, &gy00_lo, &gy00_hi);

      // Add values of four corresponding rows since scaling step_h=4
      addpd0_lo = _mm256_add_epi32(addpd0_lo, pd00_lo);
      addpd0_hi = _mm256_add_epi32(addpd0_hi, pd00_hi);
      addgx0_lo = _mm256_add_epi32(addgx0_lo, gx00_lo);
      addgx0_hi = _mm256_add_epi32(addgx0_hi, gx00_hi);
      addgy0_lo = _mm256_add_epi32(addgy0_lo, gy00_lo);
      addgy0_hi = _mm256_add_epi32(addgy0_hi, gy00_hi);

      pd01 = _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16]);
      gx01 = _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16]);
      gy01 = _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero, &gy01_lo, &gy01_hi);

      addpd1_lo = _mm256_add_epi32(addpd1_lo, pd01_lo);
      addpd1_hi = _mm256_add_epi32(addpd1_hi, pd01_hi);
      addgx1_lo = _mm256_add_epi32(addgx1_lo, gx01_lo);
      addgx1_hi = _mm256_add_epi32(addgx1_hi, gx01_hi);
      addgy1_lo = _mm256_add_epi32(addgy1_lo, gy01_lo);
      addgy1_hi = _mm256_add_epi32(addgy1_hi, gy01_hi);
    }

    addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
    addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
    addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
    addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
    addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
    addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

    addpd0_hi = round_power_of_two_signed_epi32(addpd0_lo, avg_bits);
    addpd1_hi = round_power_of_two_signed_epi32(addpd1_lo, avg_bits);
    addgx0_hi = round_power_of_two_signed_epi32(addgx0_lo, avg_bits);
    addgx1_hi = round_power_of_two_signed_epi32(addgx1_lo, avg_bits);
    addgy0_hi = round_power_of_two_signed_epi32(addgy0_lo, avg_bits);
    addgy1_hi = round_power_of_two_signed_epi32(addgy1_lo, avg_bits);

    addpd0_lo = _mm256_packs_epi32(addpd0_hi, addpd1_hi);
    addgx0_lo = _mm256_packs_epi32(addgx0_hi, addgx1_hi);
    addgy0_lo = _mm256_packs_epi32(addgy0_hi, addgy1_hi);

    _mm256_storeu_si256((__m256i *)(pdiff + (k * pstride)),
                        _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask));
    _mm256_storeu_si256((__m256i *)(gx + (k * gstride)),
                        _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask));
    _mm256_storeu_si256((__m256i *)(gy + (k * gstride)),
                        _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask));
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_32xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                step_h);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_32xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_64_avx2(int16_t *pdiff,
                                               const int pstride, int16_t *gx,
                                               int16_t *gy, const int gstride,
                                               const int bh, int step_w,
                                               int step_h) {
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i reg_mask = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
  __m256i one_mask = _mm256_set1_epi16(1);
  k = 0;
  for (int i = 0; i < bh; i++) {
    l = 0;
    for (int j = 0; j < 64; j += 32) {
      __m256i pd0 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i gx0 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gy0 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i pd1 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i gx1 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gy1 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      __m256i addpd0 = _mm256_madd_epi16(pd0, one_mask);
      __m256i addpd2 = _mm256_madd_epi16(pd1, one_mask);
      __m256i addgx0 = _mm256_madd_epi16(gx0, one_mask);
      __m256i addgx2 = _mm256_madd_epi16(gx1, one_mask);
      __m256i addgy0 = _mm256_madd_epi16(gy0, one_mask);
      __m256i addgy2 = _mm256_madd_epi16(gy1, one_mask);

      addpd0 = _mm256_hadd_epi32(addpd0, addpd2);
      addgx0 = _mm256_hadd_epi32(addgx0, addgx2);
      addgy0 = _mm256_hadd_epi32(addgy0, addgy2);

      addpd0 = round_power_of_two_signed_epi32(addpd0, avg_bits);
      addgx0 = round_power_of_two_signed_epi32(addgx0, avg_bits);
      addgy0 = round_power_of_two_signed_epi32(addgy0, avg_bits);

      _mm_storeu_si128((__m128i *)(pdiff + (k * pstride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addpd0, addpd0), reg_mask)));
      _mm_storeu_si128((__m128i *)(gx + (k * gstride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addgx0, addgx0), reg_mask)));
      _mm_storeu_si128((__m128i *)(gy + (k * gstride) + l),
                       _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
                           _mm256_packs_epi32(addgy0, addgy0), reg_mask)));
      l += 8;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_64xg32_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 64; j += 32) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;
        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd0_hi = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_hi = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_hi = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storeu_si128(
          (__m128i *)(pdiff + (k * pstride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addpd0_hi, addpd0_hi), reg_mask)));
      _mm_storeu_si128(
          (__m128i *)(gx + (k * gstride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addgx0_hi, addgx0_hi), reg_mask)));
      _mm_storeu_si128(
          (__m128i *)(gy + (k * gstride) + l),
          _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
              _mm256_packs_epi32(addgy0_hi, addgy0_hi), reg_mask)));
      l += 8;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_64xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  if (bh <= 16) {
    avg_pool_pdiff_grad_64_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                step_h);
  } else if (bh >= 32) {
    avg_pool_pdiff_grad_64xg32_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  }
}

static INLINE void avg_pool_pdiff_grad_128xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);
  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(7, 6, 3, 2, 5, 1, 4, 0);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 128; j += 32) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;
        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd0_lo = _mm256_hadd_epi32(addpd0_hi, addpd0_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_hi, addgx0_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_hi, addgy0_hi);

      addpd0_hi = _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask);
      addgx0_hi = _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask);
      addgy0_hi = _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask);

      addpd0_lo = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_lo = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_lo = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storel_epi64((__m128i *)(pdiff + (k * pstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addpd0_lo),
                                       _mm256_castsi256_si128(addpd0_lo)));
      _mm_storel_epi64((__m128i *)(gx + (k * gstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgx0_lo),
                                       _mm256_castsi256_si128(addgx0_lo)));
      _mm_storel_epi64((__m128i *)(gy + (k * gstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgy0_lo),
                                       _mm256_castsi256_si128(addgy0_lo)));
      l += 4;
    }
    k++;
  }
}

static INLINE void avg_pool_pdiff_grad_256xbh_avx2(
    int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy,
    const int gstride, const int bh, int step_w, int step_h) {
  int k, l;
  int avg_bits = get_msb_signed(step_h) + get_msb_signed(step_w);

  __m256i pd00_lo, pd10_lo, gx00_lo, gx10_lo, gy00_lo, gy10_lo;
  __m256i pd00_hi, pd10_hi, gx00_hi, gx10_hi, gy00_hi, gy10_hi;
  __m256i pd01_lo, pd11_lo, gx01_lo, gx11_lo, gy01_lo, gy11_lo;
  __m256i pd01_hi, pd11_hi, gx01_hi, gx11_hi, gy01_hi, gy11_hi;
  const __m256i zero_mask = _mm256_setzero_si256();
  __m256i reg_mask = _mm256_set_epi32(3, 2, 1, 0, 7, 6, 5, 4);
  k = 0;
  for (int i = 0; i < bh; i += step_h) {
    l = 0;
    for (int j = 0; j < 256; j += 64) {
      __m256i pd00 = _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + j]);
      __m256i pd10 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + j]);
      __m256i gx00 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + j]);
      __m256i gx10 = _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + j]);
      __m256i gy00 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + j]);
      __m256i gy10 = _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd00, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx00, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy00, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd10, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx10, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy10, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd0_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd0_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx0_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx0_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy0_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy0_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd01 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 16 + j]);
      __m256i pd11 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 16 + j]);
      __m256i gx01 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 16 + j]);
      __m256i gx11 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 16 + j]);
      __m256i gy01 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 16 + j]);
      __m256i gy11 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 16 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd01, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx01, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy01, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd11, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx11, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy11, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd1_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd1_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx1_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx1_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy1_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy1_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      __m256i pd02 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 32 + j]);
      __m256i pd12 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 32 + j]);
      __m256i gx02 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 32 + j]);
      __m256i gx12 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 32 + j]);
      __m256i gy02 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 32 + j]);
      __m256i gy12 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 32 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd02, zero_mask, &pd00_lo, &pd00_hi);
      sign_extend_16bit_to_32bit(gx02, zero_mask, &gx00_lo, &gx00_hi);
      sign_extend_16bit_to_32bit(gy02, zero_mask, &gy00_lo, &gy00_hi);
      sign_extend_16bit_to_32bit(pd12, zero_mask, &pd10_lo, &pd10_hi);
      sign_extend_16bit_to_32bit(gx12, zero_mask, &gx10_lo, &gx10_hi);
      sign_extend_16bit_to_32bit(gy12, zero_mask, &gy10_lo, &gy10_hi);

      // Add values of 8 corresponding rows since scaling step_h=8
      __m256i addpd2_lo = _mm256_add_epi32(pd00_lo, pd10_lo);
      __m256i addpd2_hi = _mm256_add_epi32(pd00_hi, pd10_hi);
      __m256i addgx2_lo = _mm256_add_epi32(gx00_lo, gx10_lo);
      __m256i addgx2_hi = _mm256_add_epi32(gx00_hi, gx10_hi);
      __m256i addgy2_lo = _mm256_add_epi32(gy00_lo, gy10_lo);
      __m256i addgy2_hi = _mm256_add_epi32(gy00_hi, gy10_hi);

      __m256i pd03 =
          _mm256_loadu_si256((__m256i *)&pdiff[i * pstride + 48 + j]);
      __m256i pd13 =
          _mm256_loadu_si256((__m256i *)&pdiff[(i + 1) * pstride + 48 + j]);
      __m256i gx03 = _mm256_loadu_si256((__m256i *)&gx[i * gstride + 48 + j]);
      __m256i gx13 =
          _mm256_loadu_si256((__m256i *)&gx[(i + 1) * gstride + 48 + j]);
      __m256i gy03 = _mm256_loadu_si256((__m256i *)&gy[i * gstride + 48 + j]);
      __m256i gy13 =
          _mm256_loadu_si256((__m256i *)&gy[(i + 1) * gstride + 48 + j]);

      // Sign extend 16 bit to 32 bit.
      sign_extend_16bit_to_32bit(pd03, zero_mask, &pd01_lo, &pd01_hi);
      sign_extend_16bit_to_32bit(gx03, zero_mask, &gx01_lo, &gx01_hi);
      sign_extend_16bit_to_32bit(gy03, zero_mask, &gy01_lo, &gy01_hi);
      sign_extend_16bit_to_32bit(pd13, zero_mask, &pd11_lo, &pd11_hi);
      sign_extend_16bit_to_32bit(gx13, zero_mask, &gx11_lo, &gx11_hi);
      sign_extend_16bit_to_32bit(gy13, zero_mask, &gy11_lo, &gy11_hi);

      __m256i addpd3_lo = _mm256_add_epi32(pd01_lo, pd11_lo);
      __m256i addpd3_hi = _mm256_add_epi32(pd01_hi, pd11_hi);
      __m256i addgx3_lo = _mm256_add_epi32(gx01_lo, gx11_lo);
      __m256i addgx3_hi = _mm256_add_epi32(gx01_hi, gx11_hi);
      __m256i addgy3_lo = _mm256_add_epi32(gy01_lo, gy11_lo);
      __m256i addgy3_hi = _mm256_add_epi32(gy01_hi, gy11_hi);

      for (int m = 2; m < step_h; m++) {
        __m256i pd20_lo, gx20_lo, gy20_lo, pd20_hi, gx20_hi, gy20_hi;
        __m256i pd21_lo, gx21_lo, gy21_lo, pd21_hi, gx21_hi, gy21_hi;

        __m256i pd20 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + j]);
        __m256i gx20 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + j]);
        __m256i gy20 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd20, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx20, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy20, zero_mask, &gy20_lo, &gy20_hi);

        addpd0_lo = _mm256_add_epi32(addpd0_lo, pd20_lo);
        addpd0_hi = _mm256_add_epi32(addpd0_hi, pd20_hi);
        addgx0_lo = _mm256_add_epi32(addgx0_lo, gx20_lo);
        addgx0_hi = _mm256_add_epi32(addgx0_hi, gx20_hi);
        addgy0_lo = _mm256_add_epi32(addgy0_lo, gy20_lo);
        addgy0_hi = _mm256_add_epi32(addgy0_hi, gy20_hi);

        __m256i pd21 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 16 + j]);
        __m256i gx21 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 16 + j]);
        __m256i gy21 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 16 + j]);
        sign_extend_16bit_to_32bit(pd21, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx21, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy21, zero_mask, &gy21_lo, &gy21_hi);

        addpd1_lo = _mm256_add_epi32(addpd1_lo, pd21_lo);
        addpd1_hi = _mm256_add_epi32(addpd1_hi, pd21_hi);
        addgx1_lo = _mm256_add_epi32(addgx1_lo, gx21_lo);
        addgx1_hi = _mm256_add_epi32(addgx1_hi, gx21_hi);
        addgy1_lo = _mm256_add_epi32(addgy1_lo, gy21_lo);
        addgy1_hi = _mm256_add_epi32(addgy1_hi, gy21_hi);

        __m256i pd22 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 32 + j]);
        __m256i gx22 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 32 + j]);
        __m256i gy22 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 32 + j]);
        // Sign extend 16 bit to 32 bit.
        sign_extend_16bit_to_32bit(pd22, zero_mask, &pd20_lo, &pd20_hi);
        sign_extend_16bit_to_32bit(gx22, zero_mask, &gx20_lo, &gx20_hi);
        sign_extend_16bit_to_32bit(gy22, zero_mask, &gy20_lo, &gy20_hi);

        addpd2_lo = _mm256_add_epi32(addpd2_lo, pd20_lo);
        addpd2_hi = _mm256_add_epi32(addpd2_hi, pd20_hi);
        addgx2_lo = _mm256_add_epi32(addgx2_lo, gx20_lo);
        addgx2_hi = _mm256_add_epi32(addgx2_hi, gx20_hi);
        addgy2_lo = _mm256_add_epi32(addgy2_lo, gy20_lo);
        addgy2_hi = _mm256_add_epi32(addgy2_hi, gy20_hi);

        __m256i pd23 =
            _mm256_loadu_si256((__m256i *)&pdiff[(i + m) * pstride + 48 + j]);
        __m256i gx23 =
            _mm256_loadu_si256((__m256i *)&gx[(i + m) * gstride + 48 + j]);
        __m256i gy23 =
            _mm256_loadu_si256((__m256i *)&gy[(i + m) * gstride + 48 + j]);
        sign_extend_16bit_to_32bit(pd23, zero_mask, &pd21_lo, &pd21_hi);
        sign_extend_16bit_to_32bit(gx23, zero_mask, &gx21_lo, &gx21_hi);
        sign_extend_16bit_to_32bit(gy23, zero_mask, &gy21_lo, &gy21_hi);

        addpd3_lo = _mm256_add_epi32(addpd3_lo, pd21_lo);
        addpd3_hi = _mm256_add_epi32(addpd3_hi, pd21_hi);
        addgx3_lo = _mm256_add_epi32(addgx3_lo, gx21_lo);
        addgx3_hi = _mm256_add_epi32(addgx3_hi, gx21_hi);
        addgy3_lo = _mm256_add_epi32(addgy3_lo, gy21_lo);
        addgy3_hi = _mm256_add_epi32(addgy3_hi, gy21_hi);
      }

      addpd0_lo = _mm256_hadd_epi32(addpd0_lo, addpd0_hi);
      addpd1_lo = _mm256_hadd_epi32(addpd1_lo, addpd1_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_lo, addgx0_hi);
      addgx1_lo = _mm256_hadd_epi32(addgx1_lo, addgx1_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_lo, addgy0_hi);
      addgy1_lo = _mm256_hadd_epi32(addgy1_lo, addgy1_hi);

      addpd2_lo = _mm256_hadd_epi32(addpd2_lo, addpd2_hi);
      addpd3_lo = _mm256_hadd_epi32(addpd3_lo, addpd3_hi);
      addgx2_lo = _mm256_hadd_epi32(addgx2_lo, addgx2_hi);
      addgx3_lo = _mm256_hadd_epi32(addgx3_lo, addgx3_hi);
      addgy2_lo = _mm256_hadd_epi32(addgy2_lo, addgy2_hi);
      addgy3_lo = _mm256_hadd_epi32(addgy3_lo, addgy3_hi);

      addpd0_hi = _mm256_hadd_epi32(addpd0_lo, addpd1_lo);
      addgx0_hi = _mm256_hadd_epi32(addgx0_lo, addgx1_lo);
      addgy0_hi = _mm256_hadd_epi32(addgy0_lo, addgy1_lo);

      addpd2_hi = _mm256_hadd_epi32(addpd2_lo, addpd3_lo);
      addgx2_hi = _mm256_hadd_epi32(addgx2_lo, addgx3_lo);
      addgy2_hi = _mm256_hadd_epi32(addgy2_lo, addgy3_lo);

      addpd0_lo = _mm256_hadd_epi32(addpd0_hi, addpd2_hi);
      addgx0_lo = _mm256_hadd_epi32(addgx0_hi, addgx2_hi);
      addgy0_lo = _mm256_hadd_epi32(addgy0_hi, addgy2_hi);

      addpd0_hi = _mm256_permutevar8x32_epi32(addpd0_lo, reg_mask);
      addgx0_hi = _mm256_permutevar8x32_epi32(addgx0_lo, reg_mask);
      addgy0_hi = _mm256_permutevar8x32_epi32(addgy0_lo, reg_mask);

      addpd0_hi = _mm256_add_epi32(addpd0_hi, addpd0_lo);
      addgx0_hi = _mm256_add_epi32(addgx0_hi, addgx0_lo);
      addgy0_hi = _mm256_add_epi32(addgy0_hi, addgy0_lo);

      addpd0_lo = round_power_of_two_signed_epi32(addpd0_hi, avg_bits);
      addgx0_lo = round_power_of_two_signed_epi32(addgx0_hi, avg_bits);
      addgy0_lo = round_power_of_two_signed_epi32(addgy0_hi, avg_bits);

      _mm_storel_epi64((__m128i *)(pdiff + (k * pstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addpd0_lo),
                                       _mm256_castsi256_si128(addpd0_lo)));
      _mm_storel_epi64((__m128i *)(gx + (k * gstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgx0_lo),
                                       _mm256_castsi256_si128(addgx0_lo)));
      _mm_storel_epi64((__m128i *)(gy + (k * gstride) + l),
                       _mm_packs_epi32(_mm256_castsi256_si128(addgy0_lo),
                                       _mm256_castsi256_si128(addgy0_lo)));
      l += 4;
    }
    k++;
  }
}

void av1_avg_pooling_pdiff_gradients_avx2(int16_t *pdiff, const int pstride,
                                          int16_t *gx, int16_t *gy,
                                          const int gstride, const int bw,
                                          const int bh, const int n) {
  const int bh_low = AOMMIN(bh, n);
  const int bw_low = AOMMIN(bw, n);
  if (bh == bh_low && bw == bw_low) return;
  const int step_h = bh / bh_low;
  const int step_w = bw / bw_low;
  if (bw == 8) {
    avg_pool_pdiff_grad_8xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                  step_h);
  } else if (bw == 16) {
    avg_pool_pdiff_grad_16xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 32) {
    avg_pool_pdiff_grad_32xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 64) {
    avg_pool_pdiff_grad_64xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                   step_h);
  } else if (bw == 128) {
    avg_pool_pdiff_grad_128xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  } else if (bw == 256) {
    avg_pool_pdiff_grad_256xbh_avx2(pdiff, pstride, gx, gy, gstride, bh, step_w,
                                    step_h);
  } else {
    av1_avg_pooling_pdiff_gradients_c(pdiff, pstride, gx, gy, gstride, bw, bh,
                                      n);
  }
}
#endif  // CONFIG_OPFL_MV_SEARCH

// Masks used to reorder the pixels at the block boundary during gradient
// calculation.
DECLARE_ALIGNED(32, static const uint8_t,
                prev_pixel_mask[32]) = { 0, 1, 2, 3,  0,  1, 2, 3, 4,  5, 6,
                                         7, 8, 9, 10, 11, 0, 1, 2, 3,  0, 1,
                                         2, 3, 4, 5,  6,  7, 8, 9, 10, 11 };

DECLARE_ALIGNED(32, static const uint8_t,
                prev2_pixel_mask[32]) = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2,
                                          3, 4, 5, 6, 7, 0, 1, 2, 3, 0, 1,
                                          2, 3, 0, 1, 2, 3, 4, 5, 6, 7 };

DECLARE_ALIGNED(32, static const uint8_t, next_pixel_mask[32]) = {
  4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15,
  4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15
};

DECLARE_ALIGNED(32, static const uint8_t, next2_pixel_mask[32]) = {
  8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15,
  8, 9, 10, 11, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15
};

static AOM_INLINE void sub_mul_add(const __m256i *id_next,
                                   const __m256i *id_prev,
                                   const __m256i *id_next2,
                                   const __m256i *id_prev2,
                                   const __m256i *coeff_0,
                                   const __m256i *coeff_1, __m256i *temp_reg) {
  const __m256i sub_0 = _mm256_sub_epi32(*id_next, *id_prev);
  const __m256i sub_1 = _mm256_sub_epi32(*id_next2, *id_prev2);
  __m256i temp = _mm256_add_epi32(_mm256_mullo_epi32(sub_0, *coeff_0),
                                  _mm256_mullo_epi32(sub_1, *coeff_1));
  *temp_reg = round_power_of_two_signed_epi32(temp, bicubic_bits);
}

void av1_bicubic_grad_interpolation_highbd_avx2(const int16_t *pred_src,
                                                int16_t *x_grad,
                                                int16_t *y_grad,
                                                const int stride, const int bw,
                                                const int bh) {
#if OPFL_BICUBIC_GRAD
  assert(bw % 8 == 0);
  assert(bh % 8 == 0);
  const int16_t *p_src = pred_src;
  // c00 c01 c10 c11
  const __m128i coeff_128bit =
      _mm_loadu_si128((__m128i *)(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS]));
  // c00 c01 c10 c11 | c00 c01 c10 c11
  const __m256i coeff_256bit = _mm256_insertf128_si256(
      _mm256_castsi128_si256(coeff_128bit), coeff_128bit, 1);

  if (bw == 8) {
    // Calculation of x_grad.
    {
      // c00 c00 c00 c01 | c00 c00 c00 c01
      const __m256i coeffs_0 = _mm256_shuffle_epi32(coeff_256bit, 0x40);
      // c01 c00 c00 c00 | c01 c00 c00 c00
      const __m256i coeffs_1 = _mm256_shuffle_epi32(coeff_256bit, 0x01);
      // c10 c10 c10 c11 | c10 c10 c10 c11
      const __m256i coeffs_2 = _mm256_shuffle_epi32(coeff_256bit, 0xea);
      // c11 c10 c10 c10 | c11 c10 c10 c10
      const __m256i coeffs_3 = _mm256_shuffle_epi32(coeff_256bit, 0xab);
      const __m256i prev_mask = _mm256_load_si256((__m256i *)prev_pixel_mask);
      const __m256i prev2_mask = _mm256_load_si256((__m256i *)prev2_pixel_mask);
      const __m256i next_mask = _mm256_load_si256((__m256i *)next_pixel_mask);
      const __m256i next2_mask = _mm256_load_si256((__m256i *)next2_pixel_mask);
      for (int col = 0; col < bh; col += 2) {
        // s00 s01 s02 s03 s04 s05 s06 s07
        const __m128i src_128_0 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col));
        // s10 s11 s12 s13 s14 s15 s16 s17
        const __m128i src_128_1 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col + stride));
        // s00 s01 s02 s03 s10 s11 s12 s13
        const __m128i up_lo = _mm_unpacklo_epi64(src_128_0, src_128_1);
        // s04 s05 s06 s07 s14 s15 s16 s17
        const __m128i up_hi = _mm_unpackhi_epi64(src_128_0, src_128_1);

        // s00 s01 s02 s03 s10 s11 s12 s13
        const __m256i src_lo = _mm256_cvtepi16_epi32(up_lo);
        // s04 s05 s06 s07 s14 s15 s16 s17
        const __m256i src_hi = _mm256_cvtepi16_epi32(up_hi);

        // s00 s00 s01 s02 s10 s10 s11 s12
        __m256i id_prev_0 = _mm256_shuffle_epi8(src_lo, prev_mask);
        // s01 s02 s03 s04 s11 s12 s13 s14
        __m256i id_next_0 = _mm256_alignr_epi8(src_hi, src_lo, 4);
        // s00 s00 s00 s01 s10 s10 s10 s11
        __m256i id_prev2_0 = _mm256_shuffle_epi8(src_lo, prev2_mask);
        // s02 s03 s04 s05 s12 s13 s14 s15
        __m256i id_next2_0 = _mm256_alignr_epi8(src_hi, src_lo, 8);

        // s03 s04 s05 s06 s13 s14 s15 s16
        __m256i id_prev_1 = _mm256_alignr_epi8(src_hi, src_lo, 12);
        // s05 s06 s07 s07 s15 s16 s17 s17
        __m256i id_next_1 = _mm256_shuffle_epi8(src_hi, next_mask);
        // s02 s03 s04 s05 s12 s13 s14 s15
        __m256i id_prev2_1 = id_next2_0;
        // s06 s07 s07 s07 s16 s17 s17 s17
        __m256i id_next2_1 = _mm256_shuffle_epi8(src_hi, next2_mask);
        __m256i temp_0, temp_1;
        sub_mul_add(&id_next_0, &id_prev_0, &id_next2_0, &id_prev2_0, &coeffs_1,
                    &coeffs_3, &temp_0);
        sub_mul_add(&id_next_1, &id_prev_1, &id_next2_1, &id_prev2_1, &coeffs_0,
                    &coeffs_2, &temp_1);
        // t00 t01 t02 t03 t04 t05 t06 t07 | t10 t11 t12 t13 t14 t15 t16 t17
        _mm256_storeu_si256((__m256i *)(&x_grad[stride * col]),
                            _mm256_packs_epi32(temp_0, temp_1));
      }
    }
    // Calculation of y_grad.
    {
      const __m256i coeffs_0 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1]);
      const __m256i coeffs_1 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
      const __m256i coeffs_2 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1]);
      const __m256i coeffs_3 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
      // s00 s01 s02 s03 s04 s05 s06 s07
      const __m128i src_128_0 = _mm_loadu_si128((__m128i *)(p_src));
      // s10 s11 s12 s13 s14 s15 s16 s17
      __m128i src_128_1 = _mm_loadu_si128((__m128i *)(p_src + stride));
      // s20 s21 s22 s23 s24 s25 s26 s27
      __m128i src_128_2 = _mm_loadu_si128((__m128i *)(p_src + 2 * stride));
      __m256i src_prev2 = _mm256_cvtepi16_epi32(src_128_0);
      __m256i src_next = _mm256_cvtepi16_epi32(src_128_1);
      __m256i src_next2 = _mm256_cvtepi16_epi32(src_128_2);
      __m256i src_prev = src_prev2;
      __m256i temp_0, temp_1;
      sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_0,
                  &coeffs_2, &temp_0);
      src_next = src_next2;
      // s30 s31 s32 s33 s34 s35 s36 s37
      __m128i src_128_3 = _mm_loadu_si128((__m128i *)(p_src + 3 * stride));
      src_next2 = _mm256_cvtepi16_epi32(src_128_3);
      sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_1,
                  &coeffs_3, &temp_1);
      // t00 t01 t02 t03 t04 t05 t06 t07 | t10 t11 t12 t13 t14 t15 t16 t17
      const __m256i temp =
          _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_0, temp_1), 0xd8);
      _mm256_storeu_si256((__m256i *)y_grad, temp);
      for (int col = 2; col < bh - 2; col += 2) {
        // s00 s01 s02 s03 s04 s05 s06 s07
        src_prev2 = src_prev;
        // s10 s11 s12 s13 s14 s15 s16 s17
        src_128_1 = _mm_loadu_si128((__m128i *)(p_src + (col - 1) * stride));
        src_prev = _mm256_cvtepi16_epi32(src_128_1);
        // s30 s31 s32 s33 s34 s35 s36 s37
        src_next = src_next2;
        // s40 s41 s42 s43 s44 s45 s46 s47
        src_128_2 = _mm_loadu_si128((__m128i *)(p_src + (col + 2) * stride));
        src_next2 = _mm256_cvtepi16_epi32(src_128_2);
        __m256i temp_2, temp_3;
        sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_1,
                    &coeffs_3, &temp_2);
        // s10 s11 s12 s13 s14 s15 s16 s17
        src_prev2 = src_prev;
        // s10 s11 s12 s13 s14 s15 s16 s17
        src_128_3 = _mm_loadu_si128((__m128i *)(p_src + col * stride));
        src_prev = _mm256_cvtepi16_epi32(src_128_3);
        // s40 s41 s42 s43 s44 s45 s46 s47
        src_next = src_next2;
        // s50 s51 s52 s53 s54 s55 s56 s57
        const __m128i src_128_4 =
            _mm_loadu_si128((__m128i *)(p_src + (col + 3) * stride));
        src_next2 = _mm256_cvtepi16_epi32(src_128_4);
        sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_1,
                    &coeffs_3, &temp_3);
        // t00 t01 t02 t03 t04 t05 t06 t07 | t10 t11 t12 t13 t14 t15 t16 t17
        const __m256i temp0 =
            _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_2, temp_3), 0xd8);
        _mm256_storeu_si256((__m256i *)(&y_grad[col * stride]), temp0);
      }
      // s40 s41 s42 s43 s44 s45 s46 s47
      src_prev2 = src_prev;
      // s50 s51 s52 s53 s54 s55 s56 s57
      const __m128i src_128_5 =
          _mm_loadu_si128((__m128i *)(p_src + (bh - 3) * stride));
      src_prev = _mm256_cvtepi16_epi32(src_128_5);
      // s70 s71 s72 s73 s74 s75 s76 s77
      src_next = src_next2;
      __m256i temp_4, temp_5;
      sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_1,
                  &coeffs_3, &temp_4);
      // s50 s51 s52 s53 s54 s55 s56 s57
      src_prev2 = src_prev;
      // s60 s61 s62 s63 s64 s65 s66 s67
      const __m128i src_128_6 =
          _mm_loadu_si128((__m128i *)(p_src + (bh - 2) * stride));
      src_prev = _mm256_cvtepi16_epi32(src_128_6);
      sub_mul_add(&src_next, &src_prev, &src_next2, &src_prev2, &coeffs_0,
                  &coeffs_2, &temp_5);
      // t60 t61 t62 t63 t64 t65 t67 | t70 t71 t72 t73 t74 t75 t77
      const __m256i temp0 =
          _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_4, temp_5), 0xd8);
      _mm256_storeu_si256((__m256i *)(&y_grad[(bh - 2) * stride]), temp0);
    }
  } else {
    // Calculation of x_grad.
    const int for_loop_iter = bw / 8;
    {
      // c01 c00 c00 c00 | c01 c00 c00 c00
      const __m256i coeffs_1 = _mm256_shuffle_epi32(coeff_256bit, 0x01);
      // c00 c00 c00 c00 | c00 c00 c00 c00
      const __m256i coeffs_4 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
      // c11 c10 c10 c10 | c11 c10 c10 c10
      const __m256i coeffs_3 = _mm256_shuffle_epi32(coeff_256bit, 0xab);
      // c10 c10 c10 c10 | c10 c10 c10 c10
      const __m256i coeffs_5 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
      const __m256i prev_mask = _mm256_load_si256((__m256i *)prev_pixel_mask);
      const __m256i prev2_mask = _mm256_load_si256((__m256i *)prev2_pixel_mask);
      for (int col = 0; col < bh; col += 2) {
        // s00 s01 s02 s03 s04 s05 s06 s07
        const __m128i src_128_0 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col));
        // s08 s09 s010 s011 s012 s013 s014 s015
        const __m128i src_128_1 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col + 8));
        // s10 s11 s12 s13 s14 s15 s16 s17
        const __m128i src_128_2 =
            _mm_loadu_si128((__m128i *)(p_src + stride * (col + 1)));
        // s18 s19 s110 s111 s112 s113 s114 s115
        const __m128i src_128_3 =
            _mm_loadu_si128((__m128i *)(p_src + stride * (col + 1) + 8));
        // s00 s01 s02 s03 s10 s11 s12 s13
        const __m128i up_0 = _mm_unpacklo_epi64(src_128_0, src_128_2);
        // s04 s05 s06 s07 s14 s15 s16 s17
        const __m128i up_1 = _mm_unpackhi_epi64(src_128_0, src_128_2);
        // s08 s09 s010 s011 s18 s19 s110 s111
        const __m128i up_2 = _mm_unpacklo_epi64(src_128_1, src_128_3);

        // s00 s01 s02 s03 s10 s11 s12 s13
        const __m256i src_0 = _mm256_cvtepi16_epi32(up_0);
        // s04 s05 s06 s07 s14 s15 s16 s17
        const __m256i src_1 = _mm256_cvtepi16_epi32(up_1);
        // s08 s09 s010 s011 s18 s19 s110 s111
        const __m256i src_2 = _mm256_cvtepi16_epi32(up_2);

        // s00 s00 s01 s02 s10 s10 s11 s12
        const __m256i id_prev_0 = _mm256_shuffle_epi8(src_0, prev_mask);
        // s01 s02 s03 s04 s11 s12 s13 s14
        const __m256i id_next_0 = _mm256_alignr_epi8(src_1, src_0, 4);
        // s00 s00 s00 s01 s10 s10 s10 s11
        const __m256i id_prev2_0 = _mm256_shuffle_epi8(src_0, prev2_mask);
        // s02 s03 s04 s05 s12 s13 s14 s15
        const __m256i id_next2_0 = _mm256_alignr_epi8(src_1, src_0, 8);

        // s03 s04 s05 s06 s13 s14 s15 s16
        const __m256i id_prev_1 = _mm256_alignr_epi8(src_1, src_0, 12);
        // s05 s06 s07 s08 s15 s16 s17 s18
        const __m256i id_next_1 = _mm256_alignr_epi8(src_2, src_1, 4);
        // s02 s03 s04 s05 s12 s13 s14 s15
        const __m256i id_prev2_1 = id_next2_0;
        // s06 s07 s08 s09 s16 s17 s18 s19
        const __m256i id_next2_1 = _mm256_alignr_epi8(src_2, src_1, 8);
        __m256i temp_0, temp_1;
        sub_mul_add(&id_next_0, &id_prev_0, &id_next2_0, &id_prev2_0, &coeffs_1,
                    &coeffs_3, &temp_0);
        sub_mul_add(&id_next_1, &id_prev_1, &id_next2_1, &id_prev2_1, &coeffs_4,
                    &coeffs_5, &temp_1);
        // t00 t01 t02 t03 t04 t05 t06 t07 | t10 t11 t12 t13 t14 t15 t16 t17
        const __m256i x_reg_0 = _mm256_packs_epi32(temp_0, temp_1);

        // t00 t01 t02 t03 t04 t05 t06 t07
        _mm_storeu_si128((__m128i *)(&x_grad[stride * col]),
                         _mm256_castsi256_si128(x_reg_0));
        // t10 t11 t12 t13 t14 t15 t16 t17
        _mm_storeu_si128((__m128i *)(&x_grad[stride * (col + 1)]),
                         _mm256_extracti128_si256(x_reg_0, 1));
      }
    }
    for (int counter = 0; counter < for_loop_iter - 2; counter++) {
      const __m256i coeffs_4 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
      const __m256i coeffs_5 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
      for (int col = 0; col < bh; col += 2) {
        __m256i src_0, src_1, src_2;
        // s06 s07 s08 s09 s010 s011 s012 s013
        const __m128i src_128_0 = _mm_loadu_si128(
            (__m128i *)(p_src + stride * col + 6 + counter * 8));
        // s010 s011 s012 s013 s014 s015 s016 s017
        const __m128i src_128_1 = _mm_loadu_si128(
            (__m128i *)(p_src + stride * col + 10 + counter * 8));
        // s16 s17 s18 s19 s110 s111 s112 s113
        const __m128i src_128_2 = _mm_loadu_si128(
            (__m128i *)(p_src + stride * (col + 1) + 6 + counter * 8));
        // s110 s111 s112 s113 s114 s115 s116 s117
        const __m128i src_128_3 = _mm_loadu_si128(
            (__m128i *)(p_src + stride * (col + 1) + 10 + counter * 8));
        // s06 s07 s08 s09 s16 s17 s18 s19
        const __m128i up_0 = _mm_unpacklo_epi64(src_128_0, src_128_2);
        // s010 s011 s012 s013 s110 s111 s112 s113
        const __m128i up_1 = _mm_unpackhi_epi64(src_128_0, src_128_2);
        // s014 s015 s016 s017 s114 s115 s116 s117
        const __m128i up_2 = _mm_unpackhi_epi64(src_128_1, src_128_3);

        // s06 s07 s08 s09 s16 s17 s18 s19
        src_0 = _mm256_cvtepi16_epi32(up_0);
        // s010 s011 s012 s013 s110 s111 s112 s113
        src_1 = _mm256_cvtepi16_epi32(up_1);
        // s014 s015 s016 s017 s114 s115 s116 s117
        src_2 = _mm256_cvtepi16_epi32(up_2);

        // s07 s08 s09 s010 s17 s18 s19 s110
        const __m256i id_prev_0 = _mm256_alignr_epi8(src_1, src_0, 4);
        // s09 s010 s011 s012 s19 s110 s111 s112
        const __m256i id_next_0 = _mm256_alignr_epi8(src_1, src_0, 12);
        // s06 s07 s08 s09 s16 s17 s18 s19
        const __m256i id_prev2_0 = src_0;
        // s010 s011 s012 s013 s110 s111 s112 s113
        const __m256i id_next2_0 = src_1;

        // s011 s012 s013 s014 s111 s112 s113 s114
        const __m256i id_prev_1 = _mm256_alignr_epi8(src_2, src_1, 4);
        // s013 s014 s015 s016 s113 s114 s115 s116
        const __m256i id_next_1 = _mm256_alignr_epi8(src_2, src_1, 12);
        // s010 s011 s012 s013 s110 s111 s112 s113
        const __m256i id_prev2_1 = id_next2_0;
        // s06 s07 s08 s09 s16 s17 s18 s19
        const __m256i id_next2_1 = src_2;
        __m256i temp_0, temp_1;
        sub_mul_add(&id_next_0, &id_prev_0, &id_next2_0, &id_prev2_0, &coeffs_4,
                    &coeffs_5, &temp_0);
        sub_mul_add(&id_next_1, &id_prev_1, &id_next2_1, &id_prev2_1, &coeffs_4,
                    &coeffs_5, &temp_1);
        // t00 t01 t02 t03 t04 t05 t06 t07 | t10 t11 t12 t13 t14 t15 t16 t17
        const __m256i x_reg_0 = _mm256_packs_epi32(temp_0, temp_1);

        // t00 t01 t02 t03 t04 t05 t06 t07
        _mm_storeu_si128((__m128i *)(&x_grad[stride * col + (counter + 1) * 8]),
                         _mm256_castsi256_si128(x_reg_0));
        // t10 t11 t12 t13 t14 t15 t16 t17
        _mm_storeu_si128(
            (__m128i *)(&x_grad[stride * (col + 1) + (counter + 1) * 8]),
            _mm256_extracti128_si256(x_reg_0, 1));
      }
    }
    {
      const __m256i coeffs_0 = _mm256_shuffle_epi32(coeff_256bit, 0x40);
      const __m256i coeffs_4 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
      const __m256i coeffs_2 = _mm256_shuffle_epi32(coeff_256bit, 0xea);
      const __m256i coeffs_5 =
          _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
      const __m256i next_mask = _mm256_load_si256((__m256i *)next_pixel_mask);
      const __m256i next2_mask = _mm256_load_si256((__m256i *)next2_pixel_mask);
      for (int col = 0; col < bh; col += 2) {
        __m256i src_0, src_1, src_3;
        // s022 s023 s024 s025 s026 s027 s028 s029
        const __m128i src_128_0 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col + bw - 10));
        // s024 s025 s026 s027 s028 s029 s030 s031
        const __m128i src_128_1 =
            _mm_loadu_si128((__m128i *)(p_src + stride * col + bw - 8));
        // s122 s123 s124 s125 s126 s127 s128 s129
        const __m128i src_128_2 =
            _mm_loadu_si128((__m128i *)(p_src + stride * (col + 1) + bw - 10));
        // s124 s125 s126 s127 s128 s129 s130 s131
        const __m128i src_128_3 =
            _mm_loadu_si128((__m128i *)(p_src + stride * (col + 1) + bw - 8));
        // s022 s023 s024 s025 s122 s123 s124 s125
        const __m128i up_0 = _mm_unpacklo_epi64(src_128_0, src_128_2);
        // s026 s027 s028 s029 s126 s127 s128 s129
        const __m128i up_1 = _mm_unpackhi_epi64(src_128_0, src_128_2);
        // s028 s029 s030 s031 s128 s129 s130 s131
        const __m128i up_3 = _mm_unpackhi_epi64(src_128_1, src_128_3);

        // s022 s023 s024 s025 s122 s123 s124 s125
        src_0 = _mm256_cvtepi16_epi32(up_0);
        // s026 s027 s028 s029 s126 s127 s128 s129
        src_1 = _mm256_cvtepi16_epi32(up_1);
        // s028 s029 s030 s031 s128 s129 s130 s131
        src_3 = _mm256_cvtepi16_epi32(up_3);

        // s023 s024 s025 s026 s023 s024 s025 s026
        const __m256i id_prev_0 = _mm256_alignr_epi8(src_1, src_0, 4);
        // s025 s026 s027 s028 s125 s126 s127 s128
        const __m256i id_next_0 = _mm256_alignr_epi8(src_1, src_0, 12);
        // s022 s023 s024 s025 s122 s123 s124 s125
        const __m256i id_prev2_0 = src_0;
        // s026 s027 s028 s029 s126 s127 s128 s129
        const __m256i id_next2_0 = src_1;

        //  s030 s031 s031 s031 s130 s131 s131 s131
        const __m256i id_next2_1 = _mm256_shuffle_epi8(src_3, next2_mask);
        // s027 s028 s029 s030 s127 s128 s129 s130
        const __m256i id_prev_1 = _mm256_alignr_epi8(id_next2_1, src_1, 4);
        // s029 s030 s031 s031 s129 s130 s131 s131
        const __m256i id_next_1 = _mm256_shuffle_epi8(src_3, next_mask);
        // s026 s027 s028 s029 s126 s127 s128 s129
        const __m256i id_prev2_1 = id_next2_0;
        __m256i temp_0, temp_1;
        sub_mul_add(&id_next_0, &id_prev_0, &id_next2_0, &id_prev2_0, &coeffs_4,
                    &coeffs_5, &temp_0);
        sub_mul_add(&id_next_1, &id_prev_1, &id_next2_1, &id_prev2_1, &coeffs_0,
                    &coeffs_2, &temp_1);
        // t024 t025 t026 t027 t028 t029 t030 t031 | t124 t125 t126 t127 t128
        // t129 t130 t131
        const __m256i x_reg_0 = _mm256_packs_epi32(temp_0, temp_1);

        // t024 t025 t026 t027 t028 t029 t030 t031
        _mm_storeu_si128((__m128i *)(&x_grad[stride * col + bw - 8]),
                         _mm256_castsi256_si128(x_reg_0));
        // t10 t11 t12 t13 t14 t15 t16 t17
        _mm_storeu_si128((__m128i *)(&x_grad[stride * (col + 1) + bw - 8]),
                         _mm256_extracti128_si256(x_reg_0, 1));
      }
    }
    // Calculation of y_grad.
    int inc = 0;
    do {
      {
        const __m256i coeffs_0 =
            _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1]);
        const __m256i coeffs_1 =
            _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0]);
        const __m256i coeffs_2 =
            _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1]);
        const __m256i coeffs_3 =
            _mm256_set1_epi32(coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0]);
        // s00 s01 s02 s03 s04 s05 s06 s07
        const __m128i src_0_0 = _mm_loadu_si128((__m128i *)(p_src + inc));
        // s08 s09 s010 s011 s012 s013 s014 s015
        const __m128i src_0_1 = _mm_loadu_si128((__m128i *)(p_src + inc + 8));
        // s10 s11 s12 s13 s14 s15 s16 s17
        const __m128i src_1_0 =
            _mm_loadu_si128((__m128i *)(p_src + inc + stride));
        // s18 s19 s110 s111 s112 s113 s114 s115
        const __m128i src_1_1 =
            _mm_loadu_si128((__m128i *)(p_src + inc + stride + 8));
        // s20 s21 s22 s23 s24 s25 s26 s27
        const __m128i src_2_0 =
            _mm_loadu_si128((__m128i *)(p_src + inc + 2 * stride));
        // s28 s29 s210 s211 s212 s213 s214 s215
        const __m128i src_2_1 =
            _mm_loadu_si128((__m128i *)(p_src + inc + 2 * stride + 8));

        __m256i src_prev2_0, src_prev2_1, src_prev_0, src_prev_1, src_next_0,
            src_next_1, src_next2_0, src_next2_1;
        src_prev_0 = _mm256_cvtepi16_epi32(src_0_0);
        src_prev_1 = _mm256_cvtepi16_epi32(src_0_1);
        src_next_0 = _mm256_cvtepi16_epi32(src_1_0);
        src_next_1 = _mm256_cvtepi16_epi32(src_1_1);
        src_next2_0 = _mm256_cvtepi16_epi32(src_2_0);
        src_next2_1 = _mm256_cvtepi16_epi32(src_2_1);
        src_prev2_0 = src_prev_0;
        src_prev2_1 = src_prev_1;

        __m256i temp_0, temp_1, temp_2, temp_3;
        sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                    &coeffs_0, &coeffs_2, &temp_0);
        sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                    &coeffs_0, &coeffs_2, &temp_1);
        src_next_0 = src_next2_0;
        src_next_1 = src_next2_1;
        // s30 s31 s32 s33 s34 s35 s36 s37
        const __m128i src_3_0 =
            _mm_loadu_si128((__m128i *)(p_src + 3 * stride + inc));
        // s38 s39 s310 s311 s312 s313 s314 s315
        const __m128i src_3_1 =
            _mm_loadu_si128((__m128i *)(p_src + 3 * stride + 8 + inc));
        src_next2_0 = _mm256_cvtepi16_epi32(src_3_0);
        src_next2_1 = _mm256_cvtepi16_epi32(src_3_1);
        sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                    &coeffs_1, &coeffs_3, &temp_2);
        sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                    &coeffs_1, &coeffs_3, &temp_3);
        // t00 t01 t02 t03 t08 t09 t010 t011 | t04 t05 t06 t07 t14 t15 t16 t17
        const __m256i y_reg_0 =
            _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_0, temp_1), 0xd8);
        // t08 t09 t010 t011 t18 t19 t110 t111 | t012 t013 t014 t015 t112 t113
        // t114 t115
        const __m256i y_reg_1 =
            _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_2, temp_3), 0xd8);
        _mm256_storeu_si256((__m256i *)&y_grad[0 + inc], y_reg_0);
        _mm256_storeu_si256((__m256i *)&y_grad[stride + inc], y_reg_1);
        for (int col = 2; col < bh - 2; col += 2) {
          // s00 s01 s02 s03 s04 s05 s06 s07
          src_prev2_0 = src_prev_0;
          // s08 s09 s010 s011 s012 s013 s014 s015
          src_prev2_1 = src_prev_1;
          // s10 s11 s12 s13 s14 s15 s16 s17
          const __m128i src_128_0 =
              _mm_loadu_si128((__m128i *)(p_src + (col - 1) * stride + inc));
          // s18 s19 s110 s111 s112 s113 s114 s115
          const __m128i src_128_1 = _mm_loadu_si128(
              (__m128i *)(p_src + (col - 1) * stride + 8 + inc));
          src_prev_0 = _mm256_cvtepi16_epi32(src_128_0);
          src_prev_1 = _mm256_cvtepi16_epi32(src_128_1);
          // s30 s31 s32 s33 s34 s35 s36 s37
          src_next_0 = src_next2_0;
          // s38 s39 s310 s311 s312 s313 s314 s315
          src_next_1 = src_next2_1;
          // s40 s41 s42 s43 s44 s45 s46 s47
          const __m128i src_128_2 =
              _mm_loadu_si128((__m128i *)(p_src + (col + 2) * stride + inc));
          // s48 s49 s410 s411 s412 s413 s414 s415
          const __m128i src_128_3 = _mm_loadu_si128(
              (__m128i *)(p_src + (col + 2) * stride + 8 + inc));
          src_next2_0 = _mm256_cvtepi16_epi32(src_128_2);
          src_next2_1 = _mm256_cvtepi16_epi32(src_128_3);
          __m256i temp_5, temp_6, temp_7, temp_8;
          sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                      &coeffs_1, &coeffs_3, &temp_5);
          sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                      &coeffs_1, &coeffs_3, &temp_6);
          // s10 s11 s12 s13 s14 s15 s16 s17
          src_prev2_0 = src_prev_0;
          // s18 s19 s110 s111 s112 s113 s114 s115
          src_prev2_1 = src_prev_1;
          // s20 s21 s22 s23 s24 s25 s26 s27
          const __m128i src_128_4 =
              _mm_loadu_si128((__m128i *)(p_src + (col)*stride + inc));
          // s28 s29 s210 s211 s212 s213 s214 s215
          const __m128i src_128_5 =
              _mm_loadu_si128((__m128i *)(p_src + (col)*stride + 8 + inc));
          src_prev_0 = _mm256_cvtepi16_epi32(src_128_4);
          src_prev_1 = _mm256_cvtepi16_epi32(src_128_5);
          // s40 s41 s42 s43 s44 s45 s46 s47
          src_next_0 = src_next2_0;
          // s48 s49 s410 s411 s412 s413 s414 s415
          src_next_1 = src_next2_1;
          // s50 s51 s52 s53 s54 s55 s56 s57
          const __m128i src_128_6 =
              _mm_loadu_si128((__m128i *)(p_src + (col + 3) * stride + inc));
          // s58 s59 s510 s511 s512 s513 s514 s515
          const __m128i src_128_7 = _mm_loadu_si128(
              (__m128i *)(p_src + (col + 3) * stride + 8 + inc));
          src_next2_0 = _mm256_cvtepi16_epi32(src_128_6);
          src_next2_1 = _mm256_cvtepi16_epi32(src_128_7);
          sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                      &coeffs_1, &coeffs_3, &temp_7);
          sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                      &coeffs_1, &coeffs_3, &temp_8);
          // t20 t21 t22 t23 t24 t25 t26 t27 | t30 t31 t32 t33 t34 t35 t36 t37
          const __m256i y_reg_2 = _mm256_permute4x64_epi64(
              _mm256_packs_epi32(temp_5, temp_6), 0xd8);
          // t28 t29 t210 t211 t212 t213 t214 t215 | t38 t39 t310 t311 t312 t313
          // t314 t315
          const __m256i y_reg_3 = _mm256_permute4x64_epi64(
              _mm256_packs_epi32(temp_7, temp_8), 0xd8);
          _mm256_storeu_si256((__m256i *)(&y_grad[col * stride + inc]),
                              y_reg_2);
          _mm256_storeu_si256((__m256i *)(&y_grad[(col + 1) * stride + inc]),
                              y_reg_3);
        }
        // s40 s41 s42 s43 s44 s45 s46 s47
        src_prev2_0 = src_prev_0;
        src_prev2_1 = src_prev_1;
        // s50 s51 s52 s53 s54 s55 s56 s57
        const __m128i src_128_8 =
            _mm_loadu_si128((__m128i *)(p_src + (bh - 3) * stride + inc));
        // s58 s59 s510 s511 s512 s513 s514 s515
        const __m128i src_128_9 =
            _mm_loadu_si128((__m128i *)(p_src + (bh - 3) * stride + 8 + inc));
        src_prev_0 = _mm256_cvtepi16_epi32(src_128_8);
        src_prev_1 = _mm256_cvtepi16_epi32(src_128_9);
        // s70 s71 s72 s73 s74 s75 s76 s77
        src_next_0 = src_next2_0;
        // s78 s79 s710 s711 s712 s713 s714 s715
        src_next_1 = src_next2_1;
        __m256i temp_9, temp_09, temp_10, temp_11;
        sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                    &coeffs_1, &coeffs_3, &temp_9);
        sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                    &coeffs_1, &coeffs_3, &temp_09);
        // s50 s51 s52 s53 s54 s55 s56 s57
        src_prev2_0 = src_prev_0;
        // s58 s59 s510 s511 s512 s513 s514 s515
        src_prev2_1 = src_prev_1;
        // s60 s61 s62 s63 s64 s65 s66 s67
        const __m128i src_128_10 =
            _mm_loadu_si128((__m128i *)(p_src + (bh - 2) * stride + inc));
        // s68 s69 s610 s611 s612 s613 s614 s615
        const __m128i src_128_11 =
            _mm_loadu_si128((__m128i *)(p_src + (bh - 2) * stride + 8 + inc));
        src_prev_0 = _mm256_cvtepi16_epi32(src_128_10);
        src_prev_1 = _mm256_cvtepi16_epi32(src_128_11);
        sub_mul_add(&src_next_0, &src_prev_0, &src_next2_0, &src_prev2_0,
                    &coeffs_0, &coeffs_2, &temp_10);
        sub_mul_add(&src_next_1, &src_prev_1, &src_next2_1, &src_prev2_1,
                    &coeffs_0, &coeffs_2, &temp_11);
        // t60 t61 t62 t63 t64 t65 t67 | t70 t71 t72 t73 t74 t75 t77
        const __m256i y_reg_4 =
            _mm256_permute4x64_epi64(_mm256_packs_epi32(temp_9, temp_09), 0xd8);
        const __m256i y_reg_5 = _mm256_permute4x64_epi64(
            _mm256_packs_epi32(temp_10, temp_11), 0xd8);
        _mm256_storeu_si256((__m256i *)(&y_grad[(bh - 2) * stride + inc]),
                            y_reg_4);
        _mm256_storeu_si256((__m256i *)(&y_grad[(bh - 1) * stride + inc]),
                            y_reg_5);
      }
      inc += 16;
    } while (inc < bw);
  }
#else
  (void)pred_src;
  (void)x_grad;
  (void)y_grad;
  (void)bw;
  (void)bh;
#endif  // OPFL_BICUBIC_GRAD
}

static AOM_FORCE_INLINE void multiply(const __m256i a, const __m256i b,
                                      __m256i *t1, __m256i *t2) {
  const __m256i lo = _mm256_mullo_epi16(a, b);
  const __m256i hi = _mm256_mulhi_epi16(a, b);
  *t1 = _mm256_unpacklo_epi16(lo, hi);
  *t2 = _mm256_unpackhi_epi16(lo, hi);
}

#define OPFL_OUTPUT_RANGE_CHECK(su2, sv2, suv, suw, svw) \
  {                                                      \
    su2 = ROUND_POWER_OF_TWO_SIGNED(su2, 1);             \
    sv2 = ROUND_POWER_OF_TWO_SIGNED(sv2, 1);             \
    suv = ROUND_POWER_OF_TWO_SIGNED(suv, 1);             \
    suw = ROUND_POWER_OF_TWO_SIGNED(suw, 1);             \
    svw = ROUND_POWER_OF_TWO_SIGNED(svw, 1);             \
  }

static AOM_FORCE_INLINE void xx256_storel_32(int32_t *store_lo,
                                             int32_t *store_hi, const __m256i a,
                                             const __m256i b) {
  __m256i sum = _mm256_add_epi32(a, b);
  sum = _mm256_add_epi32(sum, _mm256_srli_si256(sum, 8));
  sum = _mm256_add_epi32(sum, _mm256_srli_si256(sum, 4));
  *store_lo = _mm256_extract_epi32(sum, 0);
  *store_hi = _mm256_extract_epi32(sum, 4);
}

static AOM_FORCE_INLINE __m256i round_power_of_two_signed_avx2(
    const __m256i in, const __m256i rounding_offset, const __m256i round_bits) {
  // Create a mask for the sign bits of the input vector
  const __m256i sign_mask = _mm256_srai_epi32(in, 31);

  const __m256i abs_vec = _mm256_abs_epi32(in);
  const __m256i add_vec = _mm256_add_epi32(abs_vec, rounding_offset);
  __m256i rounded_vec = _mm256_srav_epi32(add_vec, round_bits);

  // Restore the sign
  rounded_vec = _mm256_xor_si256(rounded_vec, sign_mask);
  rounded_vec = _mm256_sub_epi32(rounded_vec, sign_mask);
  return rounded_vec;
}

static AOM_FORCE_INLINE void multiply_and_round(const __m256i a,
                                                const __m256i b,
                                                __m256i rounding_offset,
                                                __m256i round_bits, __m256i *t1,
                                                __m256i *t2) {
  multiply(a, b, t1, t2);
  *t1 = round_power_of_two_signed_avx2(*t1, rounding_offset, round_bits);
  *t2 = round_power_of_two_signed_avx2(*t2, rounding_offset, round_bits);
}

static void opfl_mv_refinement_16x8_avx2(const int16_t *pdiff, int pstride,
                                         const int16_t *gx, const int16_t *gy,
                                         int gstride, int d0, int d1,
                                         int grad_prec_bits, int mv_prec_bits,
                                         int *vx0, int *vy0, int *vx1,
                                         int *vy1) {
  int bHeight = 8;
  int step_size = 1;
  const int rls_alpha = 4 * OPFL_RLS_PARAM;
  const int bits = mv_prec_bits + grad_prec_bits;
  __m256i u2_0, v2_0, uv_0, uw_0, vw_0;
  __m256i u2_1, v2_1, uv_1, uw_1, vw_1;
  int32_t su2_hi = 0;
  int32_t sv2_hi = 0;
  int32_t suv_hi = 0;
  int32_t suw_hi = 0;
  int32_t svw_hi = 0;
  int32_t su2_lo = 0;
  int32_t sv2_lo = 0;
  int32_t suv_lo = 0;
  int32_t suw_lo = 0;
  int32_t svw_lo = 0;
  // TODO(kslu) clean up all grad_bits if later it is still not needed
  int grad_bits_lo = 0;
  int grad_bits_hi = 0;
  const __m256i opfl_samp_min = _mm256_set1_epi16(-OPFL_SAMP_CLAMP_VAL);
  const __m256i opfl_samp_max = _mm256_set1_epi16(OPFL_SAMP_CLAMP_VAL);
  do {
    __m256i gradX = _mm256_loadu_si256((const __m256i *)gx);
    __m256i gradY = _mm256_loadu_si256((const __m256i *)gy);
    __m256i pred = _mm256_loadu_si256((const __m256i *)pdiff);
    gradX =
        _mm256_max_epi16(_mm256_min_epi16(gradX, opfl_samp_max), opfl_samp_min);
    gradY =
        _mm256_max_epi16(_mm256_min_epi16(gradY, opfl_samp_max), opfl_samp_min);
    pred =
        _mm256_max_epi16(_mm256_min_epi16(pred, opfl_samp_max), opfl_samp_min);

    // Avoid the rounding operation for the cases where both 'grad_bits_lo' and
    // 'grad_bits_hi' are zeros.
    if (grad_bits_lo || grad_bits_hi) {
      const __m256i round_bits = _mm256_setr_epi32(
          grad_bits_lo, grad_bits_lo, grad_bits_lo, grad_bits_lo, grad_bits_hi,
          grad_bits_hi, grad_bits_hi, grad_bits_hi);
      __m256i rounding_offset =
          _mm256_sllv_epi32(_mm256_set1_epi32(1), round_bits);
      rounding_offset = _mm256_srai_epi32(rounding_offset, 1);

      multiply_and_round(gradX, gradX, rounding_offset, round_bits, &u2_0,
                         &u2_1);
      multiply_and_round(gradY, gradY, rounding_offset, round_bits, &v2_0,
                         &v2_1);
      multiply_and_round(gradX, gradY, rounding_offset, round_bits, &uv_0,
                         &uv_1);
      multiply_and_round(gradX, pred, rounding_offset, round_bits, &uw_0,
                         &uw_1);
      multiply_and_round(gradY, pred, rounding_offset, round_bits, &vw_0,
                         &vw_1);
    } else {
      multiply(gradX, gradX, &u2_0, &u2_1);
      multiply(gradY, gradY, &v2_0, &v2_1);
      multiply(gradX, gradY, &uv_0, &uv_1);
      multiply(gradX, pred, &uw_0, &uw_1);
      multiply(gradY, pred, &vw_0, &vw_1);
    }

    int32_t temp_lo, temp_hi;
    xx256_storel_32(&temp_lo, &temp_hi, u2_0, u2_1);
    su2_lo += temp_lo;
    su2_hi += temp_hi;
    xx256_storel_32(&temp_lo, &temp_hi, v2_0, v2_1);
    sv2_lo += temp_lo;
    sv2_hi += temp_hi;
    xx256_storel_32(&temp_lo, &temp_hi, uv_0, uv_1);
    suv_lo += temp_lo;
    suv_hi += temp_hi;
    xx256_storel_32(&temp_lo, &temp_hi, uw_0, uw_1);
    suw_lo += temp_lo;
    suw_hi += temp_hi;
    xx256_storel_32(&temp_lo, &temp_hi, vw_0, vw_1);
    svw_lo += temp_lo;
    svw_hi += temp_hi;

#if !CONFIG_F107_GRADIENT_SIMPLIFY
    // For every 8 pixels, do a range check and add a downshift if range is
    // getting close to the max allowed bit depth
    if (get_msb_signed(
            AOMMAX(AOMMAX(su2_lo, sv2_lo), AOMMAX(abs(suw_lo), abs(svw_lo)))) >=
        MAX_OPFL_AUTOCORR_BITS - 2) {
      OPFL_OUTPUT_RANGE_CHECK(su2_lo, sv2_lo, suv_lo, suw_lo, svw_lo)
      grad_bits_lo++;
    }
    if (get_msb_signed(
            AOMMAX(AOMMAX(su2_hi, sv2_hi), AOMMAX(abs(suw_hi), abs(svw_hi)))) >=
        MAX_OPFL_AUTOCORR_BITS - 2) {
      OPFL_OUTPUT_RANGE_CHECK(su2_hi, sv2_hi, suv_hi, suw_hi, svw_hi)
      grad_bits_hi++;
    }
#endif  // !CONFIG_F107_GRADIENT_SIMPLIFY

    gx += gstride * step_size;
    gy += gstride * step_size;
    pdiff += pstride * step_size;
    bHeight -= step_size;

  } while (bHeight != 0);
  calc_mv_process(su2_lo, sv2_lo, suv_lo, suw_lo, svw_lo, d0, d1, bits,
                  rls_alpha, vx0, vy0, vx1, vy1);
  calc_mv_process(su2_hi, sv2_hi, suv_hi, suw_hi, svw_hi, d0, d1, bits,
                  rls_alpha, vx0 + 1, vy0 + 1, vx1 + 1, vy1 + 1);
}

int av1_opfl_mv_refinement_nxn_avx2(const int16_t *pdiff, int pstride,
                                    const int16_t *gx, const int16_t *gy,
                                    int gstride, int bw, int bh, int n, int d0,
                                    int d1, int grad_prec_bits,
                                    int mv_prec_bits,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
                                    int mi_x, int mi_y, int mi_cols,
                                    int mi_rows, int build_for_decode,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
                                    int *vx0, int *vy0, int *vx1, int *vy1) {
  int n_blocks = 0;
  // Invoke SSE4_1 implementation for blocks with width < 16 and for other block
  // sizes use AVX2 by processing two 8x8 blocks parallelly.
  if (bw < 16) {
    n_blocks = av1_opfl_mv_refinement_nxn_sse4_1(
        pdiff, pstride, gx, gy, gstride, bw, bh, n, d0, d1, grad_prec_bits,
        mv_prec_bits,
#if CONFIG_E191_OFS_PRED_RES_HANDLE
        mi_x, mi_y, mi_cols, mi_rows, build_for_decode,
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
        vx0, vy0, vx1, vy1);
  } else {
    assert(n == 8 && bw % n == 0 && bh % n == 0);
    for (int i = 0; i < bh; i += n) {
      for (int j = 0; j < bw; j += 16) {
#if CONFIG_E191_OFS_PRED_RES_HANDLE
        if (is_subblock_outside(mi_x + j, mi_y + i, mi_cols, mi_rows,
                                build_for_decode)) {
          const int num_blocks = 2;
          for (int idx = 0; idx < num_blocks; idx++) {
            *(vx0 + n_blocks + idx) = 0;
            *(vy0 + n_blocks + idx) = 0;
            *(vx1 + n_blocks + idx) = 0;
            *(vy1 + n_blocks + idx) = 0;
          }
          n_blocks += 2;
          continue;
        }
#endif  // CONFIG_E191_OFS_PRED_RES_HANDLE
        opfl_mv_refinement_16x8_avx2(
            pdiff + (i * pstride + j), pstride, gx + (i * gstride + j),
            gy + (i * gstride + j), gstride, d0, d1, grad_prec_bits,
            mv_prec_bits, vx0 + n_blocks, vy0 + n_blocks, vx1 + n_blocks,
            vy1 + n_blocks);
        n_blocks += 2;
      }
    }
  }
  return n_blocks;
}
