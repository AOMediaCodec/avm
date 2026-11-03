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
