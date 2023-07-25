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

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/blend.h"

#include "config/aom_dsp_rtcd.h"

void aom_highbd_blend_a64_vmask_c(uint16_t *dst, uint32_t dst_stride,
                                  const uint16_t *src0, uint32_t src0_stride,
                                  const uint16_t *src1, uint32_t src1_stride,
                                  const uint8_t *mask, int w, int h, int bd) {
  int i, j;
  (void)bd;

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 1);
  assert(w >= 1);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  assert(bd == 8 || bd == 10 || bd == 12);

  for (i = 0; i < h; ++i) {
    const int m = mask[i];
    for (j = 0; j < w; ++j) {
      dst[i * dst_stride + j] = AOM_BLEND_A64(m, src0[i * src0_stride + j],
                                              src1[i * src1_stride + j]);
    }
  }
}
