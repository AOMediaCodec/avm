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

#ifndef AVM_AV2_COMMON_IDCT_H_
#define AVM_AV2_COMMON_IDCT_H_

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/blockd.h"
#include "av2/common/common.h"
#include "av2/common/enums.h"
#include "avm_dsp/txfm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void inv_txfm_c(const tran_low_t *input, uint16_t *dest, int stride,
                const TxfmParam *txfm_param);

#define MAX_TX_SCALE 1
int av2_get_tx_scale(const TX_SIZE tx_size);

void av2_inv_cross_chroma_tx_block(tran_low_t *dqcoeff_c1,
                                   tran_low_t *dqcoeff_c2, TX_SIZE tx_size,
                                   CctxType cctx_type, const int bd);

void av2_inverse_transform_block(const MACROBLOCKD *xd,
                                 const tran_low_t *dqcoeff, int plane,
                                 TX_TYPE tx_type, TX_SIZE tx_size,
                                 uint16_t *dst, int stride, int eob,
                                 int use_ddt, int reduced_tx_set);
void av2_highbd_iwht4x4_add(const tran_low_t *input, uint16_t *dest, int stride,
                            int eob, int bd);

void av2_highbd_iwht4x4_horz_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd);

void av2_highbd_iwht4x4_vert_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd);

void av2_inv_stxfm(tran_low_t *coeff, TxfmParam *txfm_param);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_IDCT_H_
