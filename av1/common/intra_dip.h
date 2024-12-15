/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */
#ifndef AOM_AV1_COMMON_INTRA_DIP_H_
#define AOM_AV1_COMMON_INTRA_DIP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/common/av1_common_int.h"

#define INTRA_DIP_MODE_CNT 6
#define INTRA_DIP_HAS_TRANSPOSE 1

static INLINE int av1_intra_dip_modes(BLOCK_SIZE bsize) {
  (void)bsize;
  return INTRA_DIP_MODE_CNT;
}

static INLINE int av1_intra_dip_has_transpose(BLOCK_SIZE bsize) {
  (void)bsize;
  return INTRA_DIP_HAS_TRANSPOSE;
}

void av1_highbd_intra_dip_predictor(int mode, uint16_t *dst, int dst_stride,
                                    const uint16_t *above_row,
                                    const uint16_t *left_col, TX_SIZE tx_size,
                                    int bd);

#ifdef __cplusplus
}
#endif
#endif  // AOM_AV1_COMMON_INTRA_DIP_H_
