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

#include <arm_neon.h>

#include "config/avm_config.h"

#include "avm_dsp/txfm_common.h"
#include "avm_dsp/arm/mem_neon.h"
#include "avm_dsp/arm/transpose_neon.h"

static void avm_fdct4x4_helper(const int16_t *input, int stride,
                               int16x4_t *input_0, int16x4_t *input_1,
                               int16x4_t *input_2, int16x4_t *input_3) {
  *input_0 = vshl_n_s16(vld1_s16(input + 0 * stride), 4);
  *input_1 = vshl_n_s16(vld1_s16(input + 1 * stride), 4);
  *input_2 = vshl_n_s16(vld1_s16(input + 2 * stride), 4);
  *input_3 = vshl_n_s16(vld1_s16(input + 3 * stride), 4);
  // If the very first value != 0, then add 1.
  if (input[0] != 0) {
    const int16x4_t one = vreinterpret_s16_s64(vdup_n_s64(1));
    *input_0 = vadd_s16(*input_0, one);
  }

  for (int i = 0; i < 2; ++i) {
    const int16x8_t input_01 = vcombine_s16(*input_0, *input_1);
    const int16x8_t input_32 = vcombine_s16(*input_3, *input_2);

    // in_0 +/- in_3, in_1 +/- in_2
    const int16x8_t s_01 = vaddq_s16(input_01, input_32);
    const int16x8_t s_32 = vsubq_s16(input_01, input_32);

    // step_0 +/- step_1, step_2 +/- step_3
    const int16x4_t s_0 = vget_low_s16(s_01);
    const int16x4_t s_1 = vget_high_s16(s_01);
    const int16x4_t s_2 = vget_high_s16(s_32);
    const int16x4_t s_3 = vget_low_s16(s_32);

    // (s_0 +/- s_1) * cospi_16_64
    // Must expand all elements to s32. See 'needs32' comment in fwd_txfm.c.
    const int32x4_t s_0_p_s_1 = vaddl_s16(s_0, s_1);
    const int32x4_t s_0_m_s_1 = vsubl_s16(s_0, s_1);
    const int32x4_t temp1 = vmulq_n_s32(s_0_p_s_1, (int32_t)cospi_16_64);
    const int32x4_t temp2 = vmulq_n_s32(s_0_m_s_1, (int32_t)cospi_16_64);

    // fdct_round_shift
    int16x4_t out_0 = vrshrn_n_s32(temp1, DCT_CONST_BITS);
    int16x4_t out_2 = vrshrn_n_s32(temp2, DCT_CONST_BITS);

    // s_3 * cospi_8_64 + s_2 * cospi_24_64
    // s_3 * cospi_24_64 - s_2 * cospi_8_64
    const int32x4_t s_3_cospi_8_64 = vmull_n_s16(s_3, (int32_t)cospi_8_64);
    const int32x4_t s_3_cospi_24_64 = vmull_n_s16(s_3, (int32_t)cospi_24_64);

    const int32x4_t temp3 =
        vmlal_n_s16(s_3_cospi_8_64, s_2, (int32_t)cospi_24_64);
    const int32x4_t temp4 =
        vmlsl_n_s16(s_3_cospi_24_64, s_2, (int32_t)cospi_8_64);

    // fdct_round_shift
    int16x4_t out_1 = vrshrn_n_s32(temp3, DCT_CONST_BITS);
    int16x4_t out_3 = vrshrn_n_s32(temp4, DCT_CONST_BITS);

    transpose_elems_inplace_s16_4x4(&out_0, &out_1, &out_2, &out_3);

    *input_0 = out_0;
    *input_1 = out_1;
    *input_2 = out_2;
    *input_3 = out_3;
  }
}

void avm_fdct4x4_neon(const int16_t *input, tran_low_t *final_output,
                      int stride) {
  // input[M * stride] * 16
  int16x4_t input_0, input_1, input_2, input_3;

  avm_fdct4x4_helper(input, stride, &input_0, &input_1, &input_2, &input_3);

  // Not quite a rounding shift. Only add 1 despite shifting by 2.
  const int16x8_t one = vdupq_n_s16(1);
  int16x8_t out_01 = vcombine_s16(input_0, input_1);
  int16x8_t out_23 = vcombine_s16(input_2, input_3);
  out_01 = vshrq_n_s16(vaddq_s16(out_01, one), 2);
  out_23 = vshrq_n_s16(vaddq_s16(out_23, one), 2);
  store_s16q_to_tran_low(final_output + 0 * 8, out_01);
  store_s16q_to_tran_low(final_output + 1 * 8, out_23);
}

void avm_fdct4x4_lp_neon(const int16_t *input, int16_t *final_output,
                         int stride) {
  // input[M * stride] * 16
  int16x4_t input_0, input_1, input_2, input_3;

  avm_fdct4x4_helper(input, stride, &input_0, &input_1, &input_2, &input_3);

  // Not quite a rounding shift. Only add 1 despite shifting by 2.
  const int16x8_t one = vdupq_n_s16(1);
  int16x8_t out_01 = vcombine_s16(input_0, input_1);
  int16x8_t out_23 = vcombine_s16(input_2, input_3);
  out_01 = vshrq_n_s16(vaddq_s16(out_01, one), 2);
  out_23 = vshrq_n_s16(vaddq_s16(out_23, one), 2);
  vst1q_s16(final_output + 0 * 8, out_01);
  vst1q_s16(final_output + 1 * 8, out_23);
}

// Perform one pass of 8-point DCT (Loeffler butterfly) on 8 columns
// stored as int32x4_t pairs (lo/hi for 8 elements).
// Input: 8 rows of 8 int32 values. Output: 8 DCT coefficients per column.
static void fdct8_pass_neon(const int32x4_t *in_lo, const int32x4_t *in_hi,
                            int32x4_t *out_lo, int32x4_t *out_hi) {
  // Stage 1: butterfly
  int32x4_t s0_lo = vaddq_s32(in_lo[0], in_lo[7]);
  int32x4_t s0_hi = vaddq_s32(in_hi[0], in_hi[7]);
  int32x4_t s1_lo = vaddq_s32(in_lo[1], in_lo[6]);
  int32x4_t s1_hi = vaddq_s32(in_hi[1], in_hi[6]);
  int32x4_t s2_lo = vaddq_s32(in_lo[2], in_lo[5]);
  int32x4_t s2_hi = vaddq_s32(in_hi[2], in_hi[5]);
  int32x4_t s3_lo = vaddq_s32(in_lo[3], in_lo[4]);
  int32x4_t s3_hi = vaddq_s32(in_hi[3], in_hi[4]);
  int32x4_t s4_lo = vsubq_s32(in_lo[3], in_lo[4]);
  int32x4_t s4_hi = vsubq_s32(in_hi[3], in_hi[4]);
  int32x4_t s5_lo = vsubq_s32(in_lo[2], in_lo[5]);
  int32x4_t s5_hi = vsubq_s32(in_hi[2], in_hi[5]);
  int32x4_t s6_lo = vsubq_s32(in_lo[1], in_lo[6]);
  int32x4_t s6_hi = vsubq_s32(in_hi[1], in_hi[6]);
  int32x4_t s7_lo = vsubq_s32(in_lo[0], in_lo[7]);
  int32x4_t s7_hi = vsubq_s32(in_hi[0], in_hi[7]);

  // Even part: fdct4(s0..s3)
  int32x4_t x0_lo = vaddq_s32(s0_lo, s3_lo);
  int32x4_t x0_hi = vaddq_s32(s0_hi, s3_hi);
  int32x4_t x1_lo = vaddq_s32(s1_lo, s2_lo);
  int32x4_t x1_hi = vaddq_s32(s1_hi, s2_hi);
  int32x4_t x2_lo = vsubq_s32(s1_lo, s2_lo);
  int32x4_t x2_hi = vsubq_s32(s1_hi, s2_hi);
  int32x4_t x3_lo = vsubq_s32(s0_lo, s3_lo);
  int32x4_t x3_hi = vsubq_s32(s0_hi, s3_hi);

  // out[0] = (x0+x1)*cospi16 >> DCT_CONST_BITS
  const int32_t c16 = (int32_t)cospi_16_64;
  int64x2_t t0_0 = vmull_n_s32(vget_low_s32(vaddq_s32(x0_lo, x1_lo)), c16);
  int64x2_t t0_1 = vmull_n_s32(vget_high_s32(vaddq_s32(x0_lo, x1_lo)), c16);
  int64x2_t t0_2 = vmull_n_s32(vget_low_s32(vaddq_s32(x0_hi, x1_hi)), c16);
  int64x2_t t0_3 = vmull_n_s32(vget_high_s32(vaddq_s32(x0_hi, x1_hi)), c16);
  out_lo[0] = vcombine_s32(vrshrn_n_s64(t0_0, DCT_CONST_BITS),
                           vrshrn_n_s64(t0_1, DCT_CONST_BITS));
  out_hi[0] = vcombine_s32(vrshrn_n_s64(t0_2, DCT_CONST_BITS),
                           vrshrn_n_s64(t0_3, DCT_CONST_BITS));

  // out[4] = (x0-x1)*cospi16 >> DCT_CONST_BITS
  int64x2_t t4_0 = vmull_n_s32(vget_low_s32(vsubq_s32(x0_lo, x1_lo)), c16);
  int64x2_t t4_1 = vmull_n_s32(vget_high_s32(vsubq_s32(x0_lo, x1_lo)), c16);
  int64x2_t t4_2 = vmull_n_s32(vget_low_s32(vsubq_s32(x0_hi, x1_hi)), c16);
  int64x2_t t4_3 = vmull_n_s32(vget_high_s32(vsubq_s32(x0_hi, x1_hi)), c16);
  out_lo[4] = vcombine_s32(vrshrn_n_s64(t4_0, DCT_CONST_BITS),
                           vrshrn_n_s64(t4_1, DCT_CONST_BITS));
  out_hi[4] = vcombine_s32(vrshrn_n_s64(t4_2, DCT_CONST_BITS),
                           vrshrn_n_s64(t4_3, DCT_CONST_BITS));

  // out[2] = x2*cospi24 + x3*cospi8 >> DCT_CONST_BITS
  const int32_t c24 = (int32_t)cospi_24_64;
  const int32_t c8 = (int32_t)cospi_8_64;
  int64x2_t t2_0 = vmlal_n_s32(vmull_n_s32(vget_low_s32(x2_lo), c24),
                               vget_low_s32(x3_lo), c8);
  int64x2_t t2_1 = vmlal_n_s32(vmull_n_s32(vget_high_s32(x2_lo), c24),
                               vget_high_s32(x3_lo), c8);
  int64x2_t t2_2 = vmlal_n_s32(vmull_n_s32(vget_low_s32(x2_hi), c24),
                               vget_low_s32(x3_hi), c8);
  int64x2_t t2_3 = vmlal_n_s32(vmull_n_s32(vget_high_s32(x2_hi), c24),
                               vget_high_s32(x3_hi), c8);
  out_lo[2] = vcombine_s32(vrshrn_n_s64(t2_0, DCT_CONST_BITS),
                           vrshrn_n_s64(t2_1, DCT_CONST_BITS));
  out_hi[2] = vcombine_s32(vrshrn_n_s64(t2_2, DCT_CONST_BITS),
                           vrshrn_n_s64(t2_3, DCT_CONST_BITS));

  // out[6] = -x2*cospi8 + x3*cospi24 >> DCT_CONST_BITS
  int64x2_t t6_0 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(x3_lo), c24),
                               vget_low_s32(x2_lo), c8);
  int64x2_t t6_1 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(x3_lo), c24),
                               vget_high_s32(x2_lo), c8);
  int64x2_t t6_2 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(x3_hi), c24),
                               vget_low_s32(x2_hi), c8);
  int64x2_t t6_3 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(x3_hi), c24),
                               vget_high_s32(x2_hi), c8);
  out_lo[6] = vcombine_s32(vrshrn_n_s64(t6_0, DCT_CONST_BITS),
                           vrshrn_n_s64(t6_1, DCT_CONST_BITS));
  out_hi[6] = vcombine_s32(vrshrn_n_s64(t6_2, DCT_CONST_BITS),
                           vrshrn_n_s64(t6_3, DCT_CONST_BITS));

  // Odd part: Stage 2 -- t2 = (s6-s5)*cospi16, t3 = (s6+s5)*cospi16
  int64x2_t u0_0 = vmull_n_s32(vget_low_s32(vsubq_s32(s6_lo, s5_lo)), c16);
  int64x2_t u0_1 = vmull_n_s32(vget_high_s32(vsubq_s32(s6_lo, s5_lo)), c16);
  int64x2_t u0_2 = vmull_n_s32(vget_low_s32(vsubq_s32(s6_hi, s5_hi)), c16);
  int64x2_t u0_3 = vmull_n_s32(vget_high_s32(vsubq_s32(s6_hi, s5_hi)), c16);
  int32x4_t t2r_lo = vcombine_s32(vrshrn_n_s64(u0_0, DCT_CONST_BITS),
                                  vrshrn_n_s64(u0_1, DCT_CONST_BITS));
  int32x4_t t2r_hi = vcombine_s32(vrshrn_n_s64(u0_2, DCT_CONST_BITS),
                                  vrshrn_n_s64(u0_3, DCT_CONST_BITS));

  int64x2_t u1_0 = vmull_n_s32(vget_low_s32(vaddq_s32(s6_lo, s5_lo)), c16);
  int64x2_t u1_1 = vmull_n_s32(vget_high_s32(vaddq_s32(s6_lo, s5_lo)), c16);
  int64x2_t u1_2 = vmull_n_s32(vget_low_s32(vaddq_s32(s6_hi, s5_hi)), c16);
  int64x2_t u1_3 = vmull_n_s32(vget_high_s32(vaddq_s32(s6_hi, s5_hi)), c16);
  int32x4_t t3r_lo = vcombine_s32(vrshrn_n_s64(u1_0, DCT_CONST_BITS),
                                  vrshrn_n_s64(u1_1, DCT_CONST_BITS));
  int32x4_t t3r_hi = vcombine_s32(vrshrn_n_s64(u1_2, DCT_CONST_BITS),
                                  vrshrn_n_s64(u1_3, DCT_CONST_BITS));

  // Stage 3
  int32x4_t y0_lo = vaddq_s32(s4_lo, t2r_lo);
  int32x4_t y0_hi = vaddq_s32(s4_hi, t2r_hi);
  int32x4_t y1_lo = vsubq_s32(s4_lo, t2r_lo);
  int32x4_t y1_hi = vsubq_s32(s4_hi, t2r_hi);
  int32x4_t y2_lo = vsubq_s32(s7_lo, t3r_lo);
  int32x4_t y2_hi = vsubq_s32(s7_hi, t3r_hi);
  int32x4_t y3_lo = vaddq_s32(s7_lo, t3r_lo);
  int32x4_t y3_hi = vaddq_s32(s7_hi, t3r_hi);

  // Stage 4
  const int32_t c28 = (int32_t)cospi_28_64;
  const int32_t c4 = (int32_t)cospi_4_64;
  const int32_t c12 = (int32_t)cospi_12_64;
  const int32_t c20 = (int32_t)cospi_20_64;

  // out[1] = y0*cospi28 + y3*cospi4
  int64x2_t w0 = vmlal_n_s32(vmull_n_s32(vget_low_s32(y0_lo), c28),
                             vget_low_s32(y3_lo), c4);
  int64x2_t w1 = vmlal_n_s32(vmull_n_s32(vget_high_s32(y0_lo), c28),
                             vget_high_s32(y3_lo), c4);
  int64x2_t w2 = vmlal_n_s32(vmull_n_s32(vget_low_s32(y0_hi), c28),
                             vget_low_s32(y3_hi), c4);
  int64x2_t w3 = vmlal_n_s32(vmull_n_s32(vget_high_s32(y0_hi), c28),
                             vget_high_s32(y3_hi), c4);
  out_lo[1] = vcombine_s32(vrshrn_n_s64(w0, DCT_CONST_BITS),
                           vrshrn_n_s64(w1, DCT_CONST_BITS));
  out_hi[1] = vcombine_s32(vrshrn_n_s64(w2, DCT_CONST_BITS),
                           vrshrn_n_s64(w3, DCT_CONST_BITS));

  // out[3] = y2*cospi12 - y1*cospi20
  w0 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(y2_lo), c12), vget_low_s32(y1_lo),
                   c20);
  w1 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(y2_lo), c12), vget_high_s32(y1_lo),
                   c20);
  w2 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(y2_hi), c12), vget_low_s32(y1_hi),
                   c20);
  w3 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(y2_hi), c12), vget_high_s32(y1_hi),
                   c20);
  out_lo[3] = vcombine_s32(vrshrn_n_s64(w0, DCT_CONST_BITS),
                           vrshrn_n_s64(w1, DCT_CONST_BITS));
  out_hi[3] = vcombine_s32(vrshrn_n_s64(w2, DCT_CONST_BITS),
                           vrshrn_n_s64(w3, DCT_CONST_BITS));

  // out[5] = y1*cospi12 + y2*cospi20
  w0 = vmlal_n_s32(vmull_n_s32(vget_low_s32(y1_lo), c12), vget_low_s32(y2_lo),
                   c20);
  w1 = vmlal_n_s32(vmull_n_s32(vget_high_s32(y1_lo), c12), vget_high_s32(y2_lo),
                   c20);
  w2 = vmlal_n_s32(vmull_n_s32(vget_low_s32(y1_hi), c12), vget_low_s32(y2_hi),
                   c20);
  w3 = vmlal_n_s32(vmull_n_s32(vget_high_s32(y1_hi), c12), vget_high_s32(y2_hi),
                   c20);
  out_lo[5] = vcombine_s32(vrshrn_n_s64(w0, DCT_CONST_BITS),
                           vrshrn_n_s64(w1, DCT_CONST_BITS));
  out_hi[5] = vcombine_s32(vrshrn_n_s64(w2, DCT_CONST_BITS),
                           vrshrn_n_s64(w3, DCT_CONST_BITS));

  // out[7] = y3*cospi28 - y0*cospi4
  w0 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(y3_lo), c28), vget_low_s32(y0_lo),
                   c4);
  w1 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(y3_lo), c28), vget_high_s32(y0_lo),
                   c4);
  w2 = vmlsl_n_s32(vmull_n_s32(vget_low_s32(y3_hi), c28), vget_low_s32(y0_hi),
                   c4);
  w3 = vmlsl_n_s32(vmull_n_s32(vget_high_s32(y3_hi), c28), vget_high_s32(y0_hi),
                   c4);
  out_lo[7] = vcombine_s32(vrshrn_n_s64(w0, DCT_CONST_BITS),
                           vrshrn_n_s64(w1, DCT_CONST_BITS));
  out_hi[7] = vcombine_s32(vrshrn_n_s64(w2, DCT_CONST_BITS),
                           vrshrn_n_s64(w3, DCT_CONST_BITS));
}

void avm_highbd_fdct8x8_neon(const int16_t *input, tran_low_t *final_output,
                             int stride) {
  // Pass 1: column DCT. Load 8x8 input, multiply by 4, transform columns.
  // col_lo[row] = columns 0-3, col_hi[row] = columns 4-7.
  int32x4_t col_lo[8], col_hi[8];
  for (int i = 0; i < 8; i++) {
    const int16x8_t row = vld1q_s16(input + i * stride);
    col_lo[i] = vshll_n_s16(vget_low_s16(row), 2);
    col_hi[i] = vshll_n_s16(vget_high_s16(row), 2);
  }

  int32x4_t tmp_lo[8], tmp_hi[8];
  fdct8_pass_neon(col_lo, col_hi, tmp_lo, tmp_hi);

  // After pass 1: tmp_lo[vfreq] has cols 0-3, tmp_hi[vfreq] has cols 4-7.
  // For pass 2 (row DCT), we need in[col] indexed by column, with lanes
  // holding different vfreqs. This requires transposing both halves.
  int32x4_t in2_lo[8], in2_hi[8];
  transpose_arrays_s32_4x4(tmp_lo, in2_lo);
  transpose_arrays_s32_4x4(tmp_lo + 4, in2_hi);
  transpose_arrays_s32_4x4(tmp_hi, in2_lo + 4);
  transpose_arrays_s32_4x4(tmp_hi + 4, in2_hi + 4);

  int32x4_t res_lo[8], res_hi[8];
  fdct8_pass_neon(in2_lo, in2_hi, res_lo, res_hi);

  // After pass 2: res_lo[hfreq] has lanes = vfreqs 0-3,
  //               res_hi[hfreq] has lanes = vfreqs 4-7.
  // Need to store as final_output[vfreq*8 + hfreq], so transpose output.
  int32x4_t fin_lo[8], fin_hi[8];
  transpose_arrays_s32_4x4(res_lo, fin_lo);
  transpose_arrays_s32_4x4(res_lo + 4, fin_lo + 4);
  transpose_arrays_s32_4x4(res_hi, fin_hi);
  transpose_arrays_s32_4x4(res_hi + 4, fin_hi + 4);

  // fin_lo[vf] (vf<4) = hfreqs 0-3 for vfreq vf
  // fin_lo[vf+4] = hfreqs 4-7 for vfreq vf
  // fin_hi[vf] (vf<4) = hfreqs 0-3 for vfreq vf+4
  // fin_hi[vf+4] = hfreqs 4-7 for vfreq vf+4
  for (int vf = 0; vf < 4; vf++) {
    int32x4_t v0 = fin_lo[vf];
    int32x4_t v1 = fin_lo[vf + 4];
    uint32x4_t s0 = vshrq_n_u32(vreinterpretq_u32_s32(v0), 31);
    uint32x4_t s1 = vshrq_n_u32(vreinterpretq_u32_s32(v1), 31);
    vst1q_s32(final_output + vf * 8,
              vshrq_n_s32(vaddq_s32(v0, vreinterpretq_s32_u32(s0)), 1));
    vst1q_s32(final_output + vf * 8 + 4,
              vshrq_n_s32(vaddq_s32(v1, vreinterpretq_s32_u32(s1)), 1));
  }
  for (int vf = 0; vf < 4; vf++) {
    int32x4_t v0 = fin_hi[vf];
    int32x4_t v1 = fin_hi[vf + 4];
    uint32x4_t s0 = vshrq_n_u32(vreinterpretq_u32_s32(v0), 31);
    uint32x4_t s1 = vshrq_n_u32(vreinterpretq_u32_s32(v1), 31);
    vst1q_s32(final_output + (4 + vf) * 8,
              vshrq_n_s32(vaddq_s32(v0, vreinterpretq_s32_u32(s0)), 1));
    vst1q_s32(final_output + (4 + vf) * 8 + 4,
              vshrq_n_s32(vaddq_s32(v1, vreinterpretq_s32_u32(s1)), 1));
  }
}
