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

#include "avm/avm_integer.h"

#include "config/avm_dsp_rtcd.h"

// To start out, just dispatch to the function using the 2D mask and
// pass mask stride as 0. This can be improved upon if necessary.

void avm_highbd_blend_a64_hmask_sse4_1(
    uint16_t *dst, uint32_t dst_stride, const uint16_t *src0,
    uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int w, int h, int bd) {
  avm_highbd_blend_a64_mask_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                                   src1_stride, mask, 0, w, h, 0, 0, bd);
}
