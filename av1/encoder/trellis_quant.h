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
  int32_t rate[2 * TCQ_MAX_STATES];
  int32_t rate_zero[TCQ_MAX_STATES];
  int32_t rate_eob[2];
} tcq_rate_t;

typedef struct tcq_coeff_ctx_t {
  uint8_t coef[TCQ_MAX_STATES];
  uint8_t coef_eob;
  uint8_t pad[3];
} tcq_coeff_ctx_t;

typedef struct tcq_param_t {
  int n_states;
  int n_states_log2;
  int plane;
  TX_SIZE tx_size;
  TX_CLASS tx_class;
  int sharpness;
  int64_t rdmult;
  int log_scale;
  const int16_t *scan;
  const int32_t *tmp_sign;
  const tran_low_t *qcoeff;
  const tran_low_t *tcoeff;
  const int32_t *quant;
  const int32_t *dequant;
  const qm_val_t *iqmatrix;
  const uint16_t *block_eob_rate;
  const TXB_CTX *txb_ctx;
  const LV_MAP_COEFF_COST *txb_costs;
} tcq_param_t;

static AOM_FORCE_INLINE int get_low_range(int abs_qc, int lf) {
  int base_levels = lf ? 6 : 4;
  int parity = abs_qc & 1;
#if ((COEFF_BASE_RANGE & 1) == 1)
  int br_max = COEFF_BASE_RANGE + base_levels - 1 - parity;
  int low = AOMMIN(abs_qc, br_max);
  low -= base_levels - 1;
#else
  int abs2 = abs_qc & ~1;
  int low = AOMMIN(abs2, COEFF_BASE_RANGE + base_levels - 2) + parity;
  low -= base_levels - 1;
#endif
  return low;
}

static AOM_FORCE_INLINE int get_high_range(int abs_qc, int lf) {
  int base_levels = lf ? 6 : 4;
  int low_range = get_low_range(abs_qc, lf);
  int high_range = (abs_qc - low_range - (base_levels - 1)) >> 1;
  return high_range;
}

static AOM_FORCE_INLINE int get_golomb_cost_tcq(int abs_qc, int lf) {
#if NEWHR
  const int r = 1 + get_high_range(abs_qc, lf);
  const int length = get_msb(r) + 1;
  return av1_cost_literal(2 * length - 1);
#else
  int num_base_levels = lf ? LF_NUM_BASE_LEVELS : NUM_BASE_LEVELS;
  if (abs_qc >= 1 + num_base_levels + COEFF_BASE_RANGE) {
    const int r = abs_qc - COEFF_BASE_RANGE - NUM_BASE_LEVELS;
    const int length = get_msb(r) + 1;
    return av1_cost_literal(2 * length - 1);
  }
#endif  // NEWHR
  return 0;
}

static AOM_FORCE_INLINE int get_br_lf_cost_tcq(tran_low_t level,
                                               const int *coeff_lps) {
#if NEWHR
  const int base_range = get_low_range(level, 1);
  if (base_range < COEFF_BASE_RANGE - 1) return coeff_lps[base_range];
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 1);
#else
  const int base_range =
      AOMMIN(level - 1 - LF_NUM_BASE_LEVELS, COEFF_BASE_RANGE);
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 1);
#endif
}

static INLINE int get_br_cost_tcq(tran_low_t level, const int *coeff_lps) {
#if NEWHR
  const int base_range = get_low_range(level, 0);
  if (base_range < COEFF_BASE_RANGE - 1) return coeff_lps[base_range];
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 0);
#else
  const int base_range = AOMMIN(level - 1 - NUM_BASE_LEVELS, COEFF_BASE_RANGE);
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 0);
#endif
}

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
