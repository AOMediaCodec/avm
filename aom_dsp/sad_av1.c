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

// pre: predictor being evaluated
// wsrc: target weighted prediction (has been *4096 to keep precision)
// mask: 2d weights (scaled by 4096)

/* clang-format on */

static INLINE unsigned int highbd_obmc_sad(const uint16_t *pre, int pre_stride,
                                           const int32_t *wsrc,
                                           const int32_t *mask, int width,
                                           int height) {
  int y, x;
  unsigned int sad = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++)
      sad += ROUND_POWER_OF_TWO(abs(wsrc[x] - pre[x] * mask[x]), 12);

    pre += pre_stride;
    wsrc += width;
    mask += width;
  }

  return sad;
}

#define HIGHBD_OBMCSADMXN(m, n)                                 \
  unsigned int aom_highbd_obmc_sad##m##x##n##_c(                \
      const uint16_t *ref, int ref_stride, const int32_t *wsrc, \
      const int32_t *mask) {                                    \
    return highbd_obmc_sad(ref, ref_stride, wsrc, mask, m, n);  \
  }

/* clang-format off */
HIGHBD_OBMCSADMXN(128, 128)
HIGHBD_OBMCSADMXN(128, 64)
HIGHBD_OBMCSADMXN(64, 128)
HIGHBD_OBMCSADMXN(64, 64)
HIGHBD_OBMCSADMXN(64, 32)
HIGHBD_OBMCSADMXN(32, 64)
HIGHBD_OBMCSADMXN(32, 32)
HIGHBD_OBMCSADMXN(32, 16)
HIGHBD_OBMCSADMXN(16, 32)
HIGHBD_OBMCSADMXN(16, 16)
HIGHBD_OBMCSADMXN(16, 8)
HIGHBD_OBMCSADMXN(8, 16)
HIGHBD_OBMCSADMXN(8, 8)
HIGHBD_OBMCSADMXN(8, 4)
HIGHBD_OBMCSADMXN(4, 8)
HIGHBD_OBMCSADMXN(4, 4)
HIGHBD_OBMCSADMXN(4, 16)
HIGHBD_OBMCSADMXN(16, 4)
HIGHBD_OBMCSADMXN(8, 32)
HIGHBD_OBMCSADMXN(32, 8)
HIGHBD_OBMCSADMXN(16, 64)
HIGHBD_OBMCSADMXN(64, 16)
/* clang-format on */
