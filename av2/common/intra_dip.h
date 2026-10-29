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
#ifndef AVM_AV2_COMMON_INTRA_DIP_H_
#define AVM_AV2_COMMON_INTRA_DIP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/common/av2_common_int.h"

#define INTRA_DIP_MODE_CNT 6
#define INTRA_DIP_HAS_TRANSPOSE 1

#if CONFIG_DIP_EXT_PRUNING
/*! \brief Extra features passed to DIP pruning model. */
typedef struct DipMlInfo {
  /*! \brief Whether any intra mode beats best_rd passed to intra search. */
  int beat_best_rd;
  /*! \brief RD cost of the DC mode. */
  int64_t dc_mode_rd;
  /*! \brief The best_rd passed to intra search. */
  int64_t orig_best_rd;
  /*! \brief Best non-DIP mode found during intra search. */
  int best_mode;
} DipMlInfo;
#endif  // CONFIG_DIP_EXT_PRUNING

static INLINE int av2_intra_dip_modes(BLOCK_SIZE bsize) {
  (void)bsize;
  return INTRA_DIP_MODE_CNT;
}

static INLINE int av2_intra_dip_has_transpose(BLOCK_SIZE bsize) {
  (void)bsize;
  return INTRA_DIP_HAS_TRANSPOSE;
}

void av2_highbd_intra_dip_predictor(int mode, uint16_t *dst, int dst_stride,
                                    const uint16_t *above_row,
                                    const uint16_t *left_col, TX_SIZE tx_size,
                                    int bd
#if CONFIG_DIP_EXT_PRUNING
                                    ,
                                    uint16_t *intra_dip_features
#endif  // CONFIG_DIP_EXT_PRUNING
);

#ifdef __cplusplus
}
#endif
#endif  // AVM_AV2_COMMON_INTRA_DIP_H_
