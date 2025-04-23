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

#include <stdlib.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_dsp/blend.h"

/* clang-format on */
static INLINE unsigned int highbd_masked_sad(const uint16_t *src,
                                             int src_stride, const uint16_t *a,
                                             int a_stride, const uint16_t *b,
                                             int b_stride, const uint8_t *m,
                                             int m_stride, int width,
                                             int height) {
  int y, x;
  unsigned int sad = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const uint16_t pred = AOM_BLEND_A64(m[x], a[x], b[x]);
      sad += abs(pred - src[x]);
    }

    src += src_stride;
    a += a_stride;
    b += b_stride;
    m += m_stride;
  }

  return sad;
}

#define HIGHBD_MASKSADMXN(m, n)                                               \
  unsigned int aom_highbd_masked_sad##m##x##n##_c(                            \
      const uint16_t *src, int src_stride, const uint16_t *ref,               \
      int ref_stride, const uint16_t *second_pred, const uint8_t *msk,        \
      int msk_stride, int invert_mask) {                                      \
    if (!invert_mask)                                                         \
      return highbd_masked_sad(src, src_stride, ref, ref_stride, second_pred, \
                               m, msk, msk_stride, m, n);                     \
    else                                                                      \
      return highbd_masked_sad(src, src_stride, second_pred, m, ref,          \
                               ref_stride, msk, msk_stride, m, n);            \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_MASKSADMXN(256, 256)
HIGHBD_MASKSADMXN(256, 128)
HIGHBD_MASKSADMXN(128, 256)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_MASKSADMXN(128, 128)
HIGHBD_MASKSADMXN(128, 64)
HIGHBD_MASKSADMXN(64, 128)
HIGHBD_MASKSADMXN(64, 64)
HIGHBD_MASKSADMXN(64, 32)
HIGHBD_MASKSADMXN(32, 64)
HIGHBD_MASKSADMXN(32, 32)
HIGHBD_MASKSADMXN(32, 16)
HIGHBD_MASKSADMXN(16, 32)
HIGHBD_MASKSADMXN(16, 16)
HIGHBD_MASKSADMXN(16, 8)
HIGHBD_MASKSADMXN(8, 16)
HIGHBD_MASKSADMXN(8, 8)
HIGHBD_MASKSADMXN(8, 4)
HIGHBD_MASKSADMXN(4, 8)
HIGHBD_MASKSADMXN(4, 4)
HIGHBD_MASKSADMXN(4, 16)
HIGHBD_MASKSADMXN(16, 4)
HIGHBD_MASKSADMXN(8, 32)
HIGHBD_MASKSADMXN(32, 8)
HIGHBD_MASKSADMXN(16, 64)
HIGHBD_MASKSADMXN(64, 16)
#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_MASKSADMXN(4, 32)
HIGHBD_MASKSADMXN(32, 4)
HIGHBD_MASKSADMXN(8, 64)
HIGHBD_MASKSADMXN(64, 8)
HIGHBD_MASKSADMXN(4, 64)
HIGHBD_MASKSADMXN(64, 4)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
