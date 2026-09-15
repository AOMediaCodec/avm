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

#include "config/av2_rtcd.h"

#include "av2/common/av2_txfm.h"
#include "av2/common/enums.h"
#include "avm_dsp/txfm_common.h"

static void transpose4x4(int16x8_t in[2], int16x4_t out[4]) {
  int32x4x2_t b0 =
      vtrnq_s32(vreinterpretq_s32_s16(in[0]), vreinterpretq_s32_s16(in[1]));
  int16x4x2_t c0 = vtrn_s16(vreinterpret_s16_s32(vget_low_s32(b0.val[0])),
                            vreinterpret_s16_s32(vget_high_s32(b0.val[0])));
  int16x4x2_t c1 = vtrn_s16(vreinterpret_s16_s32(vget_low_s32(b0.val[1])),
                            vreinterpret_s16_s32(vget_high_s32(b0.val[1])));
  out[0] = c0.val[0];
  out[1] = c0.val[1];
  out[2] = c1.val[0];
  out[3] = c1.val[1];
}

void av2_fwht4x4_neon(const int16_t *input, tran_low_t *output, int stride) {
  // Load the 4x4 source in transposed form.
  int16x4_t a1, b1, c1, d1, e;
  a1 = vld1_s16(&input[0]);
  b1 = vld1_s16(&input[1 * stride]);
  c1 = vld1_s16(&input[2 * stride]);
  d1 = vld1_s16(&input[3 * stride]);

  // WHT.

  // Row transforms.
  a1 = vadd_s16(a1, b1);
  d1 = vsub_s16(d1, c1);
  e = vhsub_s16(a1, d1);
  b1 = vsub_s16(e, b1);
  c1 = vsub_s16(e, c1);
  a1 = vsub_s16(a1, c1);
  d1 = vadd_s16(d1, b1);

  int16x8_t x[2];
  x[0] = vcombine_s16(a1, c1);
  x[1] = vcombine_s16(d1, b1);

  int16x4_t s[4];
  transpose4x4(x, s);

  a1 = s[0];
  b1 = s[1];
  c1 = s[2];
  d1 = s[3];

  // Row transforms.
  a1 = vadd_s16(a1, b1);
  d1 = vsub_s16(d1, c1);
  e = vhsub_s16(a1, d1);
  b1 = vsub_s16(e, b1);
  c1 = vsub_s16(e, c1);
  a1 = vsub_s16(a1, c1);
  d1 = vadd_s16(d1, b1);

  x[0] = vcombine_s16(a1, c1);
  x[1] = vcombine_s16(d1, b1);

  transpose4x4(x, s);

  vst1q_s32(&output[0], vshll_n_s16(s[0], UNIT_QUANT_SHIFT));
  vst1q_s32(&output[4], vshll_n_s16(s[1], UNIT_QUANT_SHIFT));
  vst1q_s32(&output[8], vshll_n_s16(s[2], UNIT_QUANT_SHIFT));
  vst1q_s32(&output[12], vshll_n_s16(s[3], UNIT_QUANT_SHIFT));
}

void av2_highbd_fwht4x4_neon(const int16_t *input, tran_low_t *output,
                             int stride) {
  av2_fwht4x4_neon(input, output, stride);
}

static INLINE int32x4_t round_power_of_two_signed_neon(int32x4_t v, int bits) {
  const int32x4_t bias = vdupq_n_s32((1 << bits) >> 1);
  const int32x4_t sign = vshrq_n_s32(v, 31);
  return vshlq_s32(vaddq_s32(vaddq_s32(v, bias), sign), vdupq_n_s32(-bits));
}

void av2_fwd_cross_chroma_tx_block_neon(tran_low_t *coeff_c1,
                                        tran_low_t *coeff_c2, TX_SIZE tx_size,
                                        CctxType cctx_type, const int bd) {
  if (cctx_type == CCTX_NONE) return;
  assert(bd <= 14);
  const int ncoeffs = av2_get_max_eob(tx_size);
  int32_t *src_c1 = (int32_t *)coeff_c1;
  int32_t *src_c2 = (int32_t *)coeff_c2;

  const int angle_idx = cctx_type - CCTX_START;
  const int32x4_t cos_t = vdupq_n_s32(cctx_mtx[angle_idx][0]);
  const int32x4_t sin_t = vdupq_n_s32(cctx_mtx[angle_idx][1]);
  const int32x4_t max_val = vdupq_n_s32((1 << (7 + bd)) - 1);
  const int32x4_t min_val = vdupq_n_s32(-(1 << (7 + bd)));

  int i = 0;
  for (; i + 4 <= ncoeffs; i += 4) {
    const int32x4_t c1 = vld1q_s32(&src_c1[i]);
    const int32x4_t c2 = vld1q_s32(&src_c2[i]);

    const int32x4_t t0 = vaddq_s32(vmulq_s32(cos_t, c1), vmulq_s32(sin_t, c2));
    const int32x4_t t1 = vsubq_s32(vmulq_s32(cos_t, c2), vmulq_s32(sin_t, c1));

    int32x4_t r0 = round_power_of_two_signed_neon(t0, CCTX_PREC_BITS);
    int32x4_t r1 = round_power_of_two_signed_neon(t1, CCTX_PREC_BITS);

    r0 = vminq_s32(vmaxq_s32(r0, min_val), max_val);
    r1 = vminq_s32(vmaxq_s32(r1, min_val), max_val);

    vst1q_s32(&src_c1[i], r0);
    vst1q_s32(&src_c2[i], r1);
  }
  for (; i < ncoeffs; i++) {
    int64_t tmp0 = (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c1[i] +
                   (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c2[i];
    int64_t tmp1 = (int64_t)-cctx_mtx[angle_idx][1] * (int64_t)src_c1[i] +
                   (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c2[i];
    src_c1[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp0, CCTX_PREC_BITS);
    src_c2[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp1, CCTX_PREC_BITS);
    src_c1[i] = clamp_value(src_c1[i], 8 + bd);
    src_c2[i] = clamp_value(src_c2[i], 8 + bd);
  }
}
