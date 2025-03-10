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

#ifndef AOM_AV1_COMMON_IDCT_H_
#define AOM_AV1_COMMON_IDCT_H_

#include "config/aom_config.h"
#if CONFIG_CORE_TX
#include "config/av1_rtcd.h"
#endif  // CONFIG_CORE_TX

#include "av1/common/blockd.h"
#include "av1/common/common.h"
#include "av1/common/enums.h"
#include "aom_dsp/txfm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*transform_1d)(const tran_low_t *, tran_low_t *);

typedef struct {
  transform_1d cols, rows;  // vertical and horizontal
} transform_2d;

#if CONFIG_CORE_TX
void inv_txfm_c(const tran_low_t *input, uint16_t *dest, int stride,
                const TxfmParam *txfm_param);
#endif  // CONFIG_CORE_TX

#define MAX_TX_SCALE 1
int av1_get_tx_scale(const TX_SIZE tx_size);

void av1_inv_cross_chroma_tx_block(tran_low_t *dqcoeff_c1,
                                   tran_low_t *dqcoeff_c2, TX_SIZE tx_size,
                                   CctxType cctx_type, const int bd);

void av1_inverse_transform_block(const MACROBLOCKD *xd,
                                 const tran_low_t *dqcoeff, int plane,
                                 TX_TYPE tx_type, TX_SIZE tx_size,
                                 uint16_t *dst, int stride, int eob,
#if CONFIG_INTER_DDT
                                 int use_ddt,
#endif  // CONFIG_INTER_DDT
                                 int reduced_tx_set);
void av1_highbd_iwht4x4_add(const tran_low_t *input, uint16_t *dest, int stride,
                            int eob, int bd);

#if CONFIG_LOSSLESS_DPCM
void av1_highbd_iwht4x4_horz_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd);

void av1_highbd_iwht4x4_vert_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd);
#endif

static INLINE const int32_t *cast_to_int32(const tran_low_t *input) {
  assert(sizeof(int32_t) == sizeof(tran_low_t));
  return (const int32_t *)input;
}

void av1_inv_stxfm(tran_low_t *coeff, TxfmParam *txfm_param);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_IDCT_H_
