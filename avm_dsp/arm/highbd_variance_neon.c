/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
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
#include <assert.h>
#include <stdint.h>

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"

#include "av2/common/av2_common_int.h"

#if CONFIG_AV2_ENCODER

void avm_highbd_comp_avg_upsampled_pred_neon(
    MACROBLOCKD *xd, const struct AV2Common *const cm, int mi_row, int mi_col,
    const MV *const mv, uint16_t *comp_pred, const uint16_t *pred, int width,
    int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref,
    int ref_stride, int bd, int subpel_search, int is_scaled_ref) {
  avm_highbd_upsampled_pred(xd, cm, mi_row, mi_col, mv, comp_pred, width,
                            height, subpel_x_q3, subpel_y_q3, ref, ref_stride,
                            bd, subpel_search, is_scaled_ref);
  assert(!(width * height & 7));
  const int n = width * height;
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    const uint16x8_t s = vld1q_u16(comp_pred + i);
    const uint16x8_t p = vld1q_u16(pred + i);
    vst1q_u16(comp_pred + i, vrhaddq_u16(s, p));
  }
  for (; i < n; ++i) {
    comp_pred[i] = ROUND_POWER_OF_TWO(pred[i] + comp_pred[i], 1);
  }
}

void avm_highbd_dist_wtd_comp_avg_upsampled_pred_neon(
    MACROBLOCKD *xd, const struct AV2Common *const cm, int mi_row, int mi_col,
    const MV *const mv, uint16_t *comp_pred, const uint16_t *pred, int width,
    int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref,
    int ref_stride, int bd, const DIST_WTD_COMP_PARAMS *jcp_param,
    int subpel_search, int is_scaled_ref) {
  avm_highbd_upsampled_pred(xd, cm, mi_row, mi_col, mv, comp_pred, width,
                            height, subpel_x_q3, subpel_y_q3, ref, ref_stride,
                            bd, subpel_search, is_scaled_ref);
  assert(!(width * height & 7));
  const int fwd_offset = jcp_param->fwd_offset;
  const int bck_offset = jcp_param->bck_offset;
  const int16x4_t fwd = vdup_n_s16((int16_t)fwd_offset);
  const int16x4_t bck = vdup_n_s16((int16_t)bck_offset);
  const int32x4_t round = vdupq_n_s32(1 << (DIST_PRECISION_BITS - 1));
  const int n = width * height;
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    const int16x8_t sv = vreinterpretq_s16_u16(vld1q_u16(comp_pred + i));
    const int16x8_t pv = vreinterpretq_s16_u16(vld1q_u16(pred + i));

    int32x4_t lo = vmull_s16(vget_low_s16(pv), bck);
    lo = vmlal_s16(lo, vget_low_s16(sv), fwd);
    lo = vshrq_n_s32(vaddq_s32(lo, round), DIST_PRECISION_BITS);

    int32x4_t hi = vmull_s16(vget_high_s16(pv), bck);
    hi = vmlal_s16(hi, vget_high_s16(sv), fwd);
    hi = vshrq_n_s32(vaddq_s32(hi, round), DIST_PRECISION_BITS);

    const uint16x8_t result =
        vcombine_u16(vmovn_u32(vreinterpretq_u32_s32(lo)),
                     vmovn_u32(vreinterpretq_u32_s32(hi)));
    vst1q_u16(comp_pred + i, result);
  }
  for (; i < n; ++i) {
    int tmp = pred[i] * bck_offset + comp_pred[i] * fwd_offset;
    comp_pred[i] = (uint16_t)ROUND_POWER_OF_TWO(tmp, DIST_PRECISION_BITS);
  }
}

#endif  // CONFIG_AV2_ENCODER
