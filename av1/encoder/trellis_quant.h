/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_ENCODER_TRELLIS_QUANT_H_
#define AOM_AV1_ENCODER_TRELLIS_QUANT_H_

#include "config/aom_config.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/txb_common.h"
#include "av1/encoder/block.h"
#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DIAG 32
#define MAX_LF_SCAN 10

typedef struct tcq_node_t {
  int64_t rdCost : 64;
  int32_t rate : 32;
  int32_t absLevel : 24;
  int8_t prevId : 8;
} tcq_node_t;

typedef struct tcq_ctx_t {
  uint8_t ctx[MAX_DIAG];
  uint8_t lev[MAX_DIAG];
  int8_t orig_id;
} tcq_ctx_t;

typedef struct tcq_lf_ctx_t {
  uint8_t last[16];
} tcq_lf_ctx_t;

typedef struct prequant_t {
  int32_t absLevel[4];
  int64_t deltaDist[4];
  int16_t qIdx;
} prequant_t;

typedef struct tcq_rate_t {
  int32_t rate[2 * TOTALSTATES];
  int32_t rate_zero[TOTALSTATES];
  int32_t rate_eob[2];
} tcq_rate_t;

typedef struct tcq_coeff_ctx_t {
  uint8_t coef[TOTALSTATES];
  uint8_t coef_eob;
  uint8_t pad[3];
} tcq_coeff_ctx_t;

// int av1_dep_quant(const struct AV1_COMP *cpi, MACROBLOCK *x, int plane,
//                   int block, TX_SIZE tx_size, TX_TYPE tx_type,
//                   CctxType cctx_type, const TXB_CTX *const txb_ctx,
//                   int *rate_cost, int sharpness);

int av1_dep_quant(const struct AV1_COMP *cpi, MACROBLOCK *x, int plane,
                  int block, TX_SIZE tx_size, TX_TYPE tx_type,
                  CctxType cctx_type, const TXB_CTX *const txb_ctx,
                  int *rate_cost, int sharpness
#if CONFIG_TXFMBLK_LOGS || CONFIG_COEFF_LOGS
                  ,
                  int blk_row, int blk_col, BLOCK_SIZE bsize, RUN_TYPE dry_run
#endif  // CONFIG_TXFMBLK_LOGS || CONFIG_COEFF_LOGS
);

#ifdef __cplusplus
}
#endif

#endif  // AOM_AV1_ENCODER_TRELLIS_QUANT_H_
