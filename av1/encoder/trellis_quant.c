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

#include "av1/encoder/trellis_quant.h"

#include "aom_ports/mem.h"
#include "av1/common/blockd.h"
#include "av1/common/cost.h"
#include "av1/common/idct.h"
#include "av1/common/pred_common.h"
#include "av1/common/reconintra.h"
#include "av1/common/scan.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/tokenize.h"

typedef void (*DecideStateFnc)(const struct tcq_node_t *prev,
                               const struct tcq_rate_t *rd,
                               const struct prequant_t *pq, int limits,
                               int try_eob, int64_t rdmult,
                               struct tcq_node_t *decision);
typedef void (*GetLfLumaRateDistFnc)(const struct LV_MAP_COEFF_COST *txb_costs,
                                     const struct prequant_t *pq,
                                     const struct tcq_coeff_ctx_t *coeff_ctx,
                                     int blk_pos, int diag_ctx, int eob_rate,
                                     int dc_sign_ctx, const int32_t *tmp_sign,
                                     int bwl, TX_CLASS tx_class, int coeff_sign,
                                     struct tcq_rate_t *rd);
typedef void (*GetDefLumaRateDistFnc)(const struct LV_MAP_COEFF_COST *txb_costs,
                                      const struct prequant_t *pq,
                                      const struct tcq_coeff_ctx_t *coeff_ctx,
                                      int blk_pos, int bwl, TX_CLASS tx_class,
                                      int diag_ctx, int eob_rate,
                                      struct tcq_rate_t *rd);

typedef struct {
  uint8_t *base;
  int bufsize;
  int idx;
} tcq_levels_t;

static void tcq_levels_init(tcq_levels_t *lev, uint8_t *mem_tcq, int bufsize) {
  lev->base = mem_tcq;
  lev->idx = 0;
  lev->bufsize = bufsize;
}

static void tcq_levels_swap(tcq_levels_t *lev) { lev->idx ^= 1; }

static uint8_t *tcq_levels_prev(const tcq_levels_t *lev, int st) {
  return &lev->base[(2 * st + lev->idx) * lev->bufsize];
}

static uint8_t *tcq_levels_cur(const tcq_levels_t *lev, int st) {
  return &lev->base[(2 * st + !lev->idx) * lev->bufsize];
}

static AOM_INLINE void init_tcq_decision(tcq_node_t *decision) {
  static const tcq_node_t def = { INT64_MAX >> 10, INT32_MAX >> 1, -1, -2 };
  for (int state = 0; state < TCQ_N_STATES; state++) {
    memcpy(&decision[state], &def, sizeof(def));
  }
}

// Update context buffer for the current node
static AOM_INLINE void set_levels_buf(int prevId, int absLevel, uint8_t *levels,
                                      const int16_t *scan, const int eob_minus1,
                                      const int scan_pos, const int bwl,
                                      const int sharpness) {
  if (prevId == -2) {
    return;
  }
  // update current abs level
  levels[get_padded_idx(scan[scan_pos], bwl)] = AOMMIN(absLevel, INT8_MAX);
  // check current node is a new start position? if so, set all previous
  // position to 0. prevId == -1 means a new start, prevId == -2 ?
  bool new_eob = prevId < 0 && scan_pos + 1 <= eob_minus1 && sharpness == 0;
  if (new_eob) {
    for (int si = scan_pos + 1; si <= eob_minus1; si++) {
      levels[get_padded_idx(scan[si], bwl)] = 0;
    }
  }
}

static AOM_FORCE_INLINE int get_dqv(const int32_t *dequant, int coeff_idx,
                                    const qm_val_t *iqmatrix) {
  int dqv = dequant[!!coeff_idx];
  if (iqmatrix != NULL)
    dqv =
        ((iqmatrix[coeff_idx] * dqv) + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;
  return dqv;
}

static AOM_FORCE_INLINE int64_t get_coeff_dist(tran_low_t tcoeff,
                                               tran_low_t dqcoeff, int shift) {
  const int64_t diff = (tcoeff - dqcoeff) * (1 << shift);
  const int64_t error = diff * diff;
  return error;
}

static INLINE int get_coeff_cost_eob(int ci, tran_low_t abs_qc, int sign,
                                     int coeff_ctx, int dc_sign_ctx,
                                     const LV_MAP_COEFF_COST *txb_costs,
                                     int bwl, TX_CLASS tx_class
#if CONFIG_CONTEXT_DERIVATION
                                     ,
                                     int32_t t_sign
#endif  // CONFIG_CONTEXT_DERIVATION
                                     ,
                                     int plane) {
  int cost = 0;
  const int row = ci >> bwl;
  const int col = ci - (row << bwl);
  int limits = get_lf_limits(row, col, tx_class, plane);
#if CONFIG_CHROMA_CODING
  const int(*base_lf_eob_cost_ptr)[LF_BASE_SYMBOLS - 1] =
      plane > 0 ? txb_costs->base_lf_eob_cost_uv : txb_costs->base_lf_eob_cost;
  const int(*base_eob_cost_ptr)[3] =
      plane > 0 ? txb_costs->base_eob_cost_uv : txb_costs->base_eob_cost;

  cost += limits ? base_lf_eob_cost_ptr[coeff_ctx]
                                       [AOMMIN(abs_qc, LF_BASE_SYMBOLS - 1) - 1]
                 : base_eob_cost_ptr[coeff_ctx][AOMMIN(abs_qc, 3) - 1];
#else
  if (limits) {
    cost +=
        txb_costs->base_lf_eob_cost[coeff_ctx]
                                   [AOMMIN(abs_qc, LF_BASE_SYMBOLS - 1) - 1];
  } else {
    cost += txb_costs->base_eob_cost[coeff_ctx][AOMMIN(abs_qc, 3) - 1];
  }
#endif  // CONFIG_CHROMA_CODING
  if (abs_qc != 0) {
#if CONFIG_IMPROVEIDTX
    const int dc_ph_group = 0;  // PH disabled
    const bool dc_2dtx = (ci == 0);
    const bool dc_hor = (col == 0) && tx_class == TX_CLASS_HORIZ;
    const bool dc_ver = (row == 0) && tx_class == TX_CLASS_VERT;
    if (dc_2dtx || dc_hor || dc_ver) {
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_dc_sign_cost[t_sign][dc_sign_ctx][sign];
      else
        cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
    } else {
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_ac_sign_cost[t_sign][sign];
      else
        cost += av1_cost_literal(1);
    }
#else
    if (ci == 0) {
#if CONFIG_CONTEXT_DERIVATION
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_dc_sign_cost[t_sign][dc_sign_ctx][sign];
      else
        cost += txb_costs->dc_sign_cost[dc_sign_ctx][sign];
#else
      cost += txb_costs->dc_sign_cost[dc_sign_ctx][sign];
#endif  // CONFIG_CONTEXT_DERIVATION
    } else {
#if CONFIG_CONTEXT_DERIVATION
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_ac_sign_cost[t_sign][sign];
      else
        cost += av1_cost_literal(1);
#else
      cost += av1_cost_literal(1);
#endif  // CONFIG_CONTEXT_DERIVATION
    }
#endif  // CONFIG_IMPROVEIDTX
#if CONFIG_CHROMA_CODING
    if (plane > 0) {
      if (limits) {
        if (abs_qc > LF_NUM_BASE_LEVELS) {
          int br_ctx = get_br_ctx_lf_eob_chroma(ci, tx_class);
          cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost_uv[br_ctx]);
        }
      } else {
        if (abs_qc > NUM_BASE_LEVELS) {
          int br_ctx = 0; /* get_br_ctx_eob_chroma */
          cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost_uv[br_ctx]);
        }
      }
    } else {
      if (limits) {
        if (abs_qc > LF_NUM_BASE_LEVELS) {
          int br_ctx = get_br_ctx_lf_eob(ci, tx_class);
          cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[br_ctx]);
        }
      } else {
        if (abs_qc > NUM_BASE_LEVELS) {
          int br_ctx = 0; /* get_br_ctx_eob */
          cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[br_ctx]);
        }
      }
    }
#else
    if (limits) {
      if (abs_qc > LF_NUM_BASE_LEVELS) {
        int br_ctx = get_br_ctx_lf_eob(ci, tx_class);
        cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[br_ctx]);
      }
    } else {
      if (abs_qc > NUM_BASE_LEVELS) {
        int br_ctx = 0; /* get_br_ctx_eob */
        cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[br_ctx]);
      }
    }
#endif  // CONFIG_CHROMA_CODING
  }
  return cost;
}

static INLINE int get_coeff_cost_def(tran_low_t abs_qc, int coeff_ctx,
                                     int diag_ctx, int plane,
                                     const LV_MAP_COEFF_COST *txb_costs,
                                     int q_i, int t_sign, int sign) {
  int base_ctx = diag_ctx + (coeff_ctx & 15);
  int mid_ctx = coeff_ctx >> 4;
  const int(*base_cost_ptr)[TCQ_CTXS][8] =
      plane > 0 ? txb_costs->base_cost_uv : txb_costs->base_cost;
  int cost = base_cost_ptr[base_ctx][q_i][AOMMIN(abs_qc, 3)];
  if (abs_qc != 0) {
    if (plane == AOM_PLANE_V)
      cost += txb_costs->v_ac_sign_cost[t_sign][sign];
    else
      cost += av1_cost_literal(1);
    if (abs_qc > NUM_BASE_LEVELS) {
      if (plane == 0) {
        cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[mid_ctx]);
      } else {
        cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost_uv[mid_ctx]);
      }
    }
  }
  return cost;
}

static INLINE int get_coeff_cost_general(int ci, tran_low_t abs_qc, int sign,
                                         int coeff_ctx, int mid_ctx,
                                         int dc_sign_ctx,
                                         const LV_MAP_COEFF_COST *txb_costs,
                                         int bwl, TX_CLASS tx_class,
                                         const int32_t *tmp_sign, int plane,
                                         int limits, int q_i) {
  int cost = 0;
  const int(*base_lf_cost_ptr)[TCQ_CTXS][LF_BASE_SYMBOLS * 2] =
      plane > 0 ? txb_costs->base_lf_cost_uv : txb_costs->base_lf_cost;
  const int(*base_cost_ptr)[TCQ_CTXS][8] =
      plane > 0 ? txb_costs->base_cost_uv : txb_costs->base_cost;
  cost += limits ? base_lf_cost_ptr[coeff_ctx][q_i]
                                   [AOMMIN(abs_qc, LF_BASE_SYMBOLS - 1)]
                 : base_cost_ptr[coeff_ctx][q_i][AOMMIN(abs_qc, 3)];
  if (abs_qc != 0) {
    const int dc_ph_group = 0;  // PH disabled
    const int row = ci >> bwl;
    const int col = ci - (row << bwl);
    const bool dc_2dtx = (ci == 0);
    const bool dc_hor = (col == 0) && tx_class == TX_CLASS_HORIZ;
    const bool dc_ver = (row == 0) && tx_class == TX_CLASS_VERT;
    if (limits && (dc_2dtx || dc_hor || dc_ver)) {
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_dc_sign_cost[tmp_sign[ci]][dc_sign_ctx][sign];
      else
        cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
    } else {
      if (plane == AOM_PLANE_V)
        cost += txb_costs->v_ac_sign_cost[tmp_sign[ci]][sign];
      else
        cost += av1_cost_literal(1);
    }
    if (plane > 0) {
      if (limits) {
        if (abs_qc > LF_NUM_BASE_LEVELS) {
          cost +=
              get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost_uv[mid_ctx]);
        }
      } else {
        if (abs_qc > NUM_BASE_LEVELS) {
          cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost_uv[mid_ctx]);
        }
      }
    } else {
      if (limits) {
        if (abs_qc > LF_NUM_BASE_LEVELS) {
          cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
        }
      } else {
        if (abs_qc > NUM_BASE_LEVELS) {
          cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[mid_ctx]);
        }
      }
    }
  }
  return cost;
}

// Compare and update nodes info for current position, only used by pos eob - 1
static void update_node_eob(int64_t rdCost, int64_t distA, int64_t distB,
                            int64_t rdmult, int rateA, int rateB, int rate_zero,
                            tran_low_t absA, tran_low_t absB, int limits,
                            int prev_rate, int prev_state,
                            tcq_node_t *decision_02, tcq_node_t *decision_1) {
  (void)limits;
  int parityA = 0;
  int parityB = 1;
  assert(parityA == tcq_parity(absA));
  assert(parityB == tcq_parity(absB));
  int64_t costA = rdCost + RDCOST(rdmult, rateA, distA);
  int64_t costB = rdCost + RDCOST(rdmult, rateB, distB);
  int64_t cost_zero = rdCost + RDCOST(rdmult, rate_zero, 0);
  rateA += prev_rate;
  rateB += prev_rate;
  rate_zero += prev_rate;
  if (parityA) {
    if (costA < decision_1->rdCost) {
      decision_1->rdCost = costA;
      decision_1->rate = rateA;
      decision_1->prevId = prev_state;
      decision_1->absLevel = absA;
    }
  } else {
    if (costA < decision_02->rdCost) {
      decision_02->rdCost = costA;
      decision_02->rate = rateA;
      decision_02->prevId = prev_state;
      decision_02->absLevel = absA;
    }
  }

  if (parityB) {
    if (costB < decision_1->rdCost) {
      decision_1->rdCost = costB;
      decision_1->rate = rateB;
      decision_1->prevId = prev_state;
      decision_1->absLevel = absB;
    }
  } else {
    if (costB < decision_02->rdCost) {
      decision_02->rdCost = costB;
      decision_02->rate = rateB;
      decision_02->prevId = prev_state;
      decision_02->absLevel = absB;
    }
  }
  if (cost_zero < decision_02->rdCost) {
    decision_02->rdCost = cost_zero;
    decision_02->rate = rate_zero;
    decision_02->prevId = prev_state;
    decision_02->absLevel = 0;
  }
}

// Compare and update nodes info for current position
static void update_node_general(int64_t costA, int64_t costB, int64_t cost_zero,
                                int rateA, int rateB, int rate_zero,
                                tran_low_t absA, tran_low_t absB, int limits,
                                int prev_rate, int prev_state,
                                tcq_node_t *decision_02,
                                tcq_node_t *decision_1) {
  assert(tcq_parity(absA) == 0);
  assert(tcq_parity(absB) == 1);

  (void)limits;
  int even_bias = 1;
  rateA += prev_rate;
  rateB += prev_rate;
  rate_zero += prev_rate;

  if (cost_zero < costA && cost_zero < decision_02->rdCost + even_bias) {
    decision_02->rdCost = cost_zero;
    decision_02->rate = rate_zero;
    decision_02->prevId = prev_state;
    decision_02->absLevel = 0;
  } else if (costA < decision_02->rdCost + even_bias) {
    decision_02->rdCost = costA;
    decision_02->rate = rateA;
    decision_02->prevId = prev_state;
    decision_02->absLevel = absA;
  }

  if (costB < decision_1->rdCost) {
    decision_1->rdCost = costB;
    decision_1->rate = rateB;
    decision_1->prevId = prev_state;
    decision_1->absLevel = absB;
  }
}

// Evaluate NEW_EOB at the current position
static void decide_eob(int64_t costA, int64_t costB, int rateA, int rateB,
                       tran_low_t absA, tran_low_t absB,
                       tcq_node_t *decision_02, tcq_node_t *decision_1) {
  if (costA < decision_02->rdCost) {
    decision_02->rdCost = costA;
    decision_02->rate = rateA;
    decision_02->prevId = -1;
    decision_02->absLevel = absA;
  }
  if (costB < decision_1->rdCost) {
    decision_1->rdCost = costB;
    decision_1->rate = rateB;
    decision_1->prevId = -1;
    decision_1->absLevel = absB;
  }
}

// Populate the trellis from current position to next
void av1_decide_states_c(const struct tcq_node_t *prev,
                         const struct tcq_rate_t *rd,
                         const struct prequant_t *pq, int limits, int try_eob,
                         int64_t rdmult, struct tcq_node_t *decision) {
  const int32_t *rate = rd->rate;
  const int32_t *rate_zero = rd->rate_zero;
  const int32_t *rate_eob = rd->rate_eob;
  int64_t rdCost[2 * TCQ_MAX_STATES];
  int64_t rdCost_zero[TCQ_MAX_STATES];
  int64_t rdCost_eob[2];

  // Init to 0 to avoid ASAN uninitialization warnings
  memset(rdCost, 0, sizeof(rdCost));
  memset(rdCost_zero, 0, sizeof(rdCost_zero));

  for (int i = 0; i < TCQ_N_STATES; i++) {
    int a0 = tcq_quant(i);
    int a1 = a0 + 2;
    int64_t dist0 = pq->deltaDist[a0];
    int64_t dist1 = pq->deltaDist[a1];
    rdCost[2 * i] = prev[i].rdCost + RDCOST(rdmult, rate[2 * i], dist0);
    rdCost[2 * i + 1] = prev[i].rdCost + RDCOST(rdmult, rate[2 * i + 1], dist1);
    rdCost_zero[i] = prev[i].rdCost + RDCOST(rdmult, rate_zero[i], 0);
  }
  rdCost_eob[0] = RDCOST(rdmult, rate_eob[0], pq->deltaDist[0]);
  rdCost_eob[1] = RDCOST(rdmult, rate_eob[1], pq->deltaDist[2]);

  update_node_general(rdCost[0], rdCost[1], rdCost_zero[0], rate[0], rate[1],
                      rate_zero[0], pq->absLevel[0], pq->absLevel[2], limits,
                      prev[0].rate, 0, &decision[0], &decision[4]);
  update_node_general(rdCost[2], rdCost[3], rdCost_zero[1], rate[2], rate[3],
                      rate_zero[1], pq->absLevel[0], pq->absLevel[2], limits,
                      prev[1].rate, 1, &decision[4], &decision[0]);
  update_node_general(rdCost[5], rdCost[4], rdCost_zero[2], rate[5], rate[4],
                      rate_zero[2], pq->absLevel[3], pq->absLevel[1], limits,
                      prev[2].rate, 2, &decision[1], &decision[5]);
  update_node_general(rdCost[7], rdCost[6], rdCost_zero[3], rate[7], rate[6],
                      rate_zero[3], pq->absLevel[3], pq->absLevel[1], limits,
                      prev[3].rate, 3, &decision[5], &decision[1]);
  update_node_general(rdCost[8], rdCost[9], rdCost_zero[4], rate[8], rate[9],
                      rate_zero[4], pq->absLevel[0], pq->absLevel[2], limits,
                      prev[4].rate, 4, &decision[6], &decision[2]);
  update_node_general(rdCost[10], rdCost[11], rdCost_zero[5], rate[10],
                      rate[11], rate_zero[5], pq->absLevel[0], pq->absLevel[2],
                      limits, prev[5].rate, 5, &decision[2], &decision[6]);
  update_node_general(rdCost[13], rdCost[12], rdCost_zero[6], rate[13],
                      rate[12], rate_zero[6], pq->absLevel[3], pq->absLevel[1],
                      limits, prev[6].rate, 6, &decision[7], &decision[3]);
  update_node_general(rdCost[15], rdCost[14], rdCost_zero[7], rate[15],
                      rate[14], rate_zero[7], pq->absLevel[3], pq->absLevel[1],
                      limits, prev[7].rate, 7, &decision[3], &decision[7]);

  if (try_eob) {
    const int state0 = 0;
    const int state1 = 4;
    decide_eob(rdCost_eob[0], rdCost_eob[1], rate_eob[0], rate_eob[1],
               pq->absLevel[0], pq->absLevel[2], &decision[state0],
               &decision[state1]);
  }
}

// Prepare 4 quant candidates for each coeff
// [0] and [2] are for Q0. One is even and the other is odd
// [1] and [3] are for Q1. One is even and the other is odd
void av1_pre_quant_c(tran_low_t tqc, struct prequant_t *pqData,
                     const int32_t *quant_ptr, int dqv, int log_scale,
                     int scan_pos) {
  // calculate qIdx
  const int shift = 16 - log_scale + QUANT_FP_BITS;
  tran_low_t add = -((2 << shift) >> 1);
  tran_low_t abs_tqc = abs(tqc);

  tran_low_t qIdx = (int)AOMMAX(
      1, AOMMIN(((1 << 16) - 1),
                ((int64_t)abs_tqc * quant_ptr[scan_pos != 0] + add) >> shift));
  pqData->qIdx = qIdx;

  const int64_t dist0 = get_coeff_dist(abs_tqc, 0, log_scale - 1);

  int Idx_a = qIdx & 3;

  tran_low_t dqca = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qIdx * dqv,
                                                      QUANT_TABLE_BITS) >>
                    log_scale;

  pqData->absLevel[Idx_a] = (++qIdx) >> 1;
  pqData->deltaDist[Idx_a] =
      get_coeff_dist(abs_tqc, dqca, log_scale - 1) - dist0;

  int Idx_b = qIdx & 3;

  tran_low_t dqcb = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qIdx * dqv,
                                                      QUANT_TABLE_BITS) >>
                    log_scale;

  pqData->absLevel[Idx_b] = (++qIdx) >> 1;
  pqData->deltaDist[Idx_b] =
      get_coeff_dist(abs_tqc, dqcb, log_scale - 1) - dist0;

  int Idx_c = qIdx & 3;

  tran_low_t dqcc = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qIdx * dqv,
                                                      QUANT_TABLE_BITS) >>
                    log_scale;

  pqData->absLevel[Idx_c] = (++qIdx) >> 1;
  pqData->deltaDist[Idx_c] =
      get_coeff_dist(abs_tqc, dqcc, log_scale - 1) - dist0;

  int Idx_d = qIdx & 3;

  tran_low_t dqcd = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qIdx * dqv,
                                                      QUANT_TABLE_BITS) >>
                    log_scale;

  pqData->absLevel[Idx_d] = (++qIdx) >> 1;
  pqData->deltaDist[Idx_d] =
      get_coeff_dist(abs_tqc, dqcd, log_scale - 1) - dist0;
}

static int get_coeff_cost(int ci, tran_low_t abs_qc, int sign, int coeff_ctx,
                          int mid_ctx, int dc_sign_ctx,
                          const LV_MAP_COEFF_COST *txb_costs, int bwl,
                          TX_CLASS tx_class, const int32_t *tmp_sign, int plane,
                          int limits, int q_i) {
  return get_coeff_cost_general(ci, abs_qc, sign, coeff_ctx, mid_ctx,
                                dc_sign_ctx, txb_costs, bwl, tx_class,
#if CONFIG_CONTEXT_DERIVATION
                                tmp_sign,
#endif  // CONFIG_CONTEXT_DERIVATION
                                plane, limits, q_i);
}

// Process the first position
void trellis_first_pos(const tcq_param_t *p, int scan_pos,
                       tcq_levels_t *tcq_lev, tcq_node_t *trellis) {
  int plane = p->plane;
  TX_SIZE tx_size = p->tx_size;
  TX_CLASS tx_class = p->tx_class;
  int log_scale = p->log_scale;
  int sharpness = p->sharpness;
  int64_t rdmult = p->rdmult;
  const int16_t *scan = p->scan;
  const int32_t *tmp_sign = p->tmp_sign;
  const tran_low_t *qcoeff = p->qcoeff;
  const tran_low_t *tcoeff = p->tcoeff;
  const int32_t *quant = p->quant;
  const int32_t *dequant = p->dequant;
  const qm_val_t *iqmatrix = p->iqmatrix;
  const uint16_t *block_eob_rate = p->block_eob_rate;
  const TXB_CTX *txb_ctx = p->txb_ctx;
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int bwl = get_txb_bwl(tx_size);
  const int height = get_txb_high(tx_size);

  int blk_pos = scan[scan_pos];
  tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];

  prequant_t pqData;
  int tempdqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
  av1_pre_quant(tcoeff[blk_pos], &pqData, quant, tempdqv, log_scale, scan_pos);

  // init state
  init_tcq_decision(decision);

  const int row = blk_pos >> bwl;
  const int col = blk_pos - (row << bwl);
  int limits = get_lf_limits(row, col, tx_class, plane);

  // calculate rate distortion
  // try to quantize first coeff to nzcoeff
  int coeff_ctx = get_lower_levels_ctx_eob(bwl, height, scan_pos);
  int eob_rate = block_eob_rate[scan_pos];
  int dc_sign_ctx = txb_ctx->dc_sign_ctx;
  int t_sign = tmp_sign[blk_pos];
  int rate_Q0_a =
      get_coeff_cost_eob(blk_pos, pqData.absLevel[0], (qcoeff[blk_pos] < 0),
                         coeff_ctx, dc_sign_ctx, txb_costs, bwl, tx_class
#if CONFIG_CONTEXT_DERIVATION
                         ,
                         t_sign
#endif  // CONFIG_CONTEXT_DERIVATION
                         ,
                         plane) +
      eob_rate;
  int rate_Q0_b =
      get_coeff_cost_eob(blk_pos, pqData.absLevel[2], (qcoeff[blk_pos] < 0),
                         coeff_ctx, dc_sign_ctx, txb_costs, bwl, tx_class
#if CONFIG_CONTEXT_DERIVATION
                         ,
                         t_sign
#endif  // CONFIG_CONTEXT_DERIVATION
                         ,
                         plane) +
      eob_rate;
  const int state0 = 0;
  const int state1 = 4;
  update_node_eob(0, pqData.deltaDist[0], pqData.deltaDist[2], rdmult,
                  rate_Q0_a, rate_Q0_b, INT32_MAX >> 1, pqData.absLevel[0],
                  pqData.absLevel[2], limits, 0, -1, &decision[state0],
                  &decision[state1]);

  uint8_t *levels0 = tcq_levels_cur(tcq_lev, state0);
  uint8_t *levels1 = tcq_levels_cur(tcq_lev, state1);
  set_levels_buf(decision[state0].prevId, decision[state0].absLevel, levels0,
                 scan, scan_pos, scan_pos, bwl, sharpness);
  set_levels_buf(decision[state1].prevId, decision[state1].absLevel, levels1,
                 scan, scan_pos, scan_pos, bwl, sharpness);
}

void av1_get_rate_dist_def_luma_c(const struct LV_MAP_COEFF_COST *txb_costs,
                                  const struct prequant_t *pq,
                                  const struct tcq_coeff_ctx_t *coeff_ctx,
                                  int blk_pos, int bwl, TX_CLASS tx_class,
                                  int diag_ctx, int eob_rate,
                                  struct tcq_rate_t *rd) {
  const int plane = 0;
  const int t_sign = 0;
  const int sign = 0;
  const int dc_sign_ctx = 0;
  const tran_low_t *absLevel = pq->absLevel;

  for (int i = 0; i < TCQ_N_STATES; i++) {
    int q_i = tcq_quant(i);
    int a0 = q_i;
    int a1 = a0 + 2;
    int base_ctx = diag_ctx + (coeff_ctx->coef[i] & 15);
    int cost0 = get_coeff_cost_def(absLevel[a0], coeff_ctx->coef[i], diag_ctx,
                                   plane, txb_costs, q_i, t_sign, sign);
    int cost1 = get_coeff_cost_def(absLevel[a1], coeff_ctx->coef[i], diag_ctx,
                                   plane, txb_costs, q_i, t_sign, sign);
    rd->rate_zero[i] = txb_costs->base_cost[base_ctx][q_i][0];
    rd->rate[2 * i] = cost0;
    rd->rate[2 * i + 1] = cost1;
  }
  rd->rate_eob[0] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[0], sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
  rd->rate_eob[1] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[2], sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
}

void av1_get_rate_dist_def_chroma_c(const struct LV_MAP_COEFF_COST *txb_costs,
                                    const struct prequant_t *pq,
                                    const struct tcq_coeff_ctx_t *coeff_ctx,
                                    int blk_pos, int bwl, TX_CLASS tx_class,
                                    int diag_ctx, int eob_rate, int plane,
                                    int t_sign, int sign,
                                    struct tcq_rate_t *rd) {
  const tran_low_t *absLevel = pq->absLevel;
  const int dc_sign_ctx = 0;

  for (int i = 0; i < TCQ_N_STATES; i++) {
    int q_i = tcq_quant(i);
    int a0 = q_i;
    int a1 = a0 + 2;
    int base_ctx = diag_ctx + (coeff_ctx->coef[i] & 15);
    int cost0 = get_coeff_cost_def(absLevel[a0], coeff_ctx->coef[i], diag_ctx,
                                   plane, txb_costs, q_i, t_sign, sign);
    int cost1 = get_coeff_cost_def(absLevel[a1], coeff_ctx->coef[i], diag_ctx,
                                   plane, txb_costs, q_i, t_sign, sign);
    rd->rate_zero[i] = txb_costs->base_cost_uv[base_ctx][q_i][0];
    rd->rate[2 * i] = cost0;
    rd->rate[2 * i + 1] = cost1;
  }
  rd->rate_eob[0] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[0], sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
  rd->rate_eob[1] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[2], sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
}

void av1_get_rate_dist_lf_luma_c(const struct LV_MAP_COEFF_COST *txb_costs,
                                 const struct prequant_t *pq,
                                 const struct tcq_coeff_ctx_t *coeff_ctx,
                                 int blk_pos, int diag_ctx, int eob_rate,
                                 int dc_sign_ctx, const int32_t *tmp_sign,
                                 int bwl, TX_CLASS tx_class, int coeff_sign,
                                 struct tcq_rate_t *rd) {
  const tran_low_t *absLevel = pq->absLevel;
  uint8_t base_ctx;
  uint8_t mid_ctx;
  int t_sign = tmp_sign[blk_pos];
  int plane = 0;

  for (int i = 0; i < TCQ_N_STATES; i++) {
    int q_i = tcq_quant(i);
    int a0 = q_i;
    int a1 = a0 + 2;
    base_ctx = (coeff_ctx->coef[i] & 15) + diag_ctx;
    mid_ctx = coeff_ctx->coef[i] >> 4;
    int cost0 = get_coeff_cost(blk_pos, absLevel[a0], coeff_sign, base_ctx,
                               mid_ctx, dc_sign_ctx, txb_costs, bwl, tx_class,
                               tmp_sign, plane, 1, q_i);
    int cost1 = get_coeff_cost(blk_pos, absLevel[a1], coeff_sign, base_ctx,
                               mid_ctx, dc_sign_ctx, txb_costs, bwl, tx_class,
                               tmp_sign, plane, 1, q_i);
    rd->rate_zero[i] = txb_costs->base_lf_cost[base_ctx][q_i][0];
    rd->rate[2 * i] = cost0;
    rd->rate[2 * i + 1] = cost1;
  }
  rd->rate_eob[0] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[0], coeff_sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
  rd->rate_eob[1] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[2], coeff_sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
}

void av1_get_rate_dist_lf_chroma_c(const struct LV_MAP_COEFF_COST *txb_costs,
                                   const struct prequant_t *pq,
                                   const struct tcq_coeff_ctx_t *coeff_ctx,
                                   int blk_pos, int diag_ctx, int eob_rate,
                                   int dc_sign_ctx, const int32_t *tmp_sign,
                                   int bwl, TX_CLASS tx_class, int plane,
                                   int coeff_sign, struct tcq_rate_t *rd) {
  const tran_low_t *absLevel = pq->absLevel;
  uint8_t base_ctx;
  uint8_t mid_ctx;
  int t_sign = tmp_sign[blk_pos];

  for (int i = 0; i < TCQ_N_STATES; i++) {
    int q_i = tcq_quant(i);
    int a0 = q_i;
    int a1 = a0 + 2;
    base_ctx = (coeff_ctx->coef[i] & 15) + diag_ctx;
    mid_ctx = coeff_ctx->coef[i] >> 4;
    int cost0 = get_coeff_cost(blk_pos, absLevel[a0], coeff_sign, base_ctx,
                               mid_ctx, dc_sign_ctx, txb_costs, bwl, tx_class,
                               tmp_sign, plane, 1, q_i);
    int cost1 = get_coeff_cost(blk_pos, absLevel[a1], coeff_sign, base_ctx,
                               mid_ctx, dc_sign_ctx, txb_costs, bwl, tx_class,
                               tmp_sign, plane, 1, q_i);
    rd->rate_zero[i] = txb_costs->base_lf_cost_uv[base_ctx][q_i][0];
    rd->rate[2 * i] = cost0;
    rd->rate[2 * i + 1] = cost1;
  }
  rd->rate_eob[0] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[0], coeff_sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
  rd->rate_eob[1] =
      eob_rate + get_coeff_cost_eob(blk_pos, absLevel[2], coeff_sign,
                                    coeff_ctx->coef_eob, dc_sign_ctx, txb_costs,
                                    bwl, tx_class, t_sign, plane);
}

void av1_calc_diag_ctx_c(int scan_hi, int scan_lo, int bwl,
                         const uint8_t *prev_levels, const int16_t *scan,
                         uint8_t *ctx) {
  for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
    int blk_pos = scan[scan_pos];
    int coeff_mag = get_nz_mag(prev_levels + get_padded_idx(blk_pos, bwl), bwl,
                               TX_CLASS_2D);
    int coeff_ctx = AOMMIN((coeff_mag + 1) >> 1, 4);
    int br_ctx = get_br_ctx(prev_levels, blk_pos, bwl, 0);
    ctx[scan_pos - scan_lo] = (br_ctx << 4) | coeff_ctx;
  }
}

void av1_update_states_c(tcq_node_t *decision, int scan_idx,
                         const struct tcq_ctx_t *cur_ctx,
                         struct tcq_ctx_t *nxt_ctx) {
  for (int i = 0; i < TCQ_N_STATES; i++) {
    int prevId = decision[i].prevId;
    int absLevel = decision[i].absLevel;
    if (prevId >= 0) {
      memcpy(&nxt_ctx[i], &cur_ctx[prevId], sizeof(tcq_ctx_t));
    } else {
      // New EOB; reset contexts
      memset(&nxt_ctx[i], 0, sizeof(tcq_ctx_t));
      nxt_ctx[i].orig_id = -1;
    }
    nxt_ctx[i].lev[scan_idx] = AOMMIN(absLevel, INT8_MAX);
  }
}

static void update_levels_diagonal(tcq_levels_t *tcq_lev, const int16_t *scan,
                                   int bufsize, int bwl, int scan_hi,
                                   int scan_lo, const tcq_ctx_t *tcq_ctx) {
  for (int i = 0; i < TCQ_N_STATES; i++) {
    int orig_id = tcq_ctx[i].orig_id;
    uint8_t *cur_lev = tcq_levels_cur(tcq_lev, i);
    uint8_t *prev_lev = tcq_levels_prev(tcq_lev, orig_id);
    if (orig_id >= 0) {
      memcpy(cur_lev, prev_lev, bufsize);
    } else {
      memset(cur_lev, 0, bufsize);
    }
    for (int sc = scan_lo; sc <= scan_hi; sc++) {
      int lev = tcq_ctx[i].lev[sc - scan_lo];
      cur_lev[get_padded_idx(scan[sc], bwl)] = lev;
    }
  }
}

// TCQ 8-state for a diagonal in non-lf region for 2D Luma
static void trellis_loop_diagonal_st8(const tcq_param_t *p, int scan_hi,
                                      int scan_lo, tcq_levels_t *tcq_lev,
                                      tcq_ctx_t tcq_ctx[2 * TCQ_MAX_STATES],
                                      tcq_node_t *trellis) {
  int plane = p->plane;
  TX_SIZE tx_size = p->tx_size;
  TX_CLASS tx_class = p->tx_class;
  int log_scale = p->log_scale;
  int try_eob = p->sharpness == 0;
  int64_t rdmult = p->rdmult;
  const int16_t *scan = p->scan;
  const tran_low_t *tcoeff = p->tcoeff;
  const int32_t *quant = p->quant;
  const int32_t *dequant = p->dequant;
  const qm_val_t *iqmatrix = p->iqmatrix;
  const uint16_t *block_eob_rate = p->block_eob_rate;
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int bwl = get_txb_bwl(tx_size);
  const int height = get_txb_high(tx_size);
  const int pos0 = scan[scan_hi];
  const int diag_ctx = get_nz_map_ctx_from_stats(0, pos0, bwl, TX_CLASS_2D, 0);

  assert(plane == 0);
  assert(tx_class == TX_CLASS_2D);
  (void)plane;

  // Precompute base and mid ctx values, as they are independent across
  // the diagonal pass.
  tcq_levels_swap(tcq_lev);

  int i_ctx = scan_hi & 1;
  tcq_ctx_t *cur_ctx = &tcq_ctx[i_ctx ? TCQ_MAX_STATES : 0];
  tcq_ctx_t *nxt_ctx = &tcq_ctx[i_ctx ? 0 : TCQ_MAX_STATES];

  for (int i = 0; i < TCQ_N_STATES; i++) {
    uint8_t *prev_levels = tcq_levels_prev(tcq_lev, i);
    av1_calc_diag_ctx(scan_hi, scan_lo, bwl, prev_levels, scan, cur_ctx[i].ctx);
    cur_ctx[i].orig_id = i;
  }

  for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
    const int blk_pos = scan[scan_pos];
    tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
    tcq_node_t *prev_decision = &decision[TCQ_N_STATES];

    prequant_t pqData;
    int tempdqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
    av1_pre_quant(tcoeff[blk_pos], &pqData, quant, tempdqv, log_scale,
                  scan_pos);

    // init state
    init_tcq_decision(decision);
    const int limits = 0;

    // calculate rate distortion
    tcq_coeff_ctx_t coeff_ctx;
    for (int i = 0; i < TCQ_N_STATES; i++) {
      coeff_ctx.coef[i] = cur_ctx[i].ctx[scan_pos - scan_lo];
    }
    int eob_ctx = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    coeff_ctx.coef_eob = eob_ctx;
    int eob_rate = block_eob_rate[scan_pos];

    tcq_rate_t rd;
    av1_get_rate_dist_def_luma(txb_costs, &pqData, &coeff_ctx, blk_pos, bwl,
                               tx_class, diag_ctx, eob_rate, &rd);

    av1_decide_states(prev_decision, &rd, &pqData, limits, try_eob, rdmult,
                      decision);

    av1_update_states(decision, scan_pos - scan_lo, cur_ctx, nxt_ctx);

    // Swap cur/nxt context.
    tcq_ctx_t *tmp = cur_ctx;
    cur_ctx = nxt_ctx;
    nxt_ctx = tmp;
  }

  update_levels_diagonal(tcq_lev, scan, tcq_lev->bufsize, bwl, scan_hi, scan_lo,
                         cur_ctx);
}

void av1_init_lf_ctx_c(const uint8_t *lev, int scan_hi, int bwl,
                       struct tcq_lf_ctx_t *lf_ctx) {
  // Sample locations outside of the LF region that are needed
  // to calculate LF neighbor contexts.
  const uint8_t diag_scan[21] = { 0x00, 0x10, 0x01, 0x20, 0x11, 0x02, 0x30,
                                  0x21, 0x12, 0x03, 0x40, 0x31, 0x22, 0x13,
                                  0x04, 0x50, 0x41, 0x32, 0x23, 0x14, 0x05 };

  for (int st = 0; st < 1; st++) {
    memset(lf_ctx[st].last, 0, sizeof(lf_ctx[st].last));
    for (int i = 0; i < 11; i++) {
      int row_col = diag_scan[scan_hi + 1 + i];
      int row = row_col >> 4;
      int col = row_col & 15;
      int blk_pos = (row << bwl) + col;
      lf_ctx[st].last[i] = lev[get_padded_idx(blk_pos, bwl)];
    }
  }
}

void av1_calc_lf_ctx_st8_c(const struct tcq_lf_ctx_t *lf_ctx, int scan_pos,
                           struct tcq_coeff_ctx_t *coeff_ctx) {
  static const int8_t kMaxCtx[16] = { 8, 6, 6, 4, 4, 4, 4, 4,
                                      4, 4, 4, 4, 4, 4, 4, 4 };
  static const int8_t kScanDiag[MAX_LF_SCAN] = { 0, 1, 1, 2, 2, 2, 3, 3, 3, 3 };
  static const int8_t kNbrMask[4][11] = {
    { 3, 3, 1, 3, 1, 0, 0, 0, 0, 0, 0 },  // diag 0
    { 0, 3, 3, 0, 1, 3, 1, 0, 0, 0, 0 },  // diag 1
    { 0, 0, 3, 3, 0, 0, 1, 3, 1, 0, 0 },  // diag 2
    { 0, 0, 0, 3, 3, 0, 0, 0, 1, 3, 1 },  // diag 3
  };

  for (int st = 0; st < TCQ_N_STATES; st++) {
    int diag = kScanDiag[scan_pos];
    int base = 0;
    int mid = 0;
    for (int i = 0; i < 11; i++) {
      int mask = kNbrMask[diag][i];
      if (mask) {
        base += AOMMIN(lf_ctx[st].last[i], 5);
        if (mask >> 1) {
          mid += AOMMIN(lf_ctx[st].last[i], MAX_VAL_BR_CTX);
        }
      }
    }
    int base_ctx = AOMMIN((base + 1) >> 1, kMaxCtx[scan_pos]);
    int mid_ctx = AOMMIN((mid + 1) >> 1, 6) + ((scan_pos == 0) ? 0 : 7);
    coeff_ctx->coef[st] = base_ctx + (mid_ctx << 4);
  }
}

void av1_update_lf_ctx_c(const struct tcq_node_t *decision,
                         struct tcq_lf_ctx_t *lf_ctx) {
  tcq_lf_ctx_t save[TCQ_MAX_STATES];
  memcpy(save, lf_ctx, sizeof(tcq_lf_ctx_t) * TCQ_N_STATES);

  for (int st = 0; st < TCQ_N_STATES; st++) {
    int absLevel = decision[st].absLevel;
    int prevId = decision[st].prevId;
    int new_eob = prevId < 0;
    if (new_eob) {
      memset(lf_ctx[st].last, 0, sizeof(lf_ctx[st].last));
    } else {
      for (int i = 15; i > 0; i--) {
        lf_ctx[st].last[i] = save[prevId].last[i - 1];
      }
    }
    lf_ctx[st].last[0] = AOMMIN(absLevel, INT8_MAX);
  }
}

// TCQ 8-state in lf region for 2D Luma
static void trellis_loop_lf_st8(const tcq_param_t *p, int scan_hi, int scan_lo,
                                tcq_levels_t *tcq_lev, tcq_node_t *trellis) {
  TX_SIZE tx_size = p->tx_size;
  TX_CLASS tx_class = p->tx_class;
  int log_scale = p->log_scale;
  int try_eob = p->sharpness == 0;
  int64_t rdmult = p->rdmult;
  const int16_t *scan = p->scan;
  const int32_t *tmp_sign = p->tmp_sign;
  const tran_low_t *tcoeff = p->tcoeff;
  const int32_t *quant = p->quant;
  const int32_t *dequant = p->dequant;
  const qm_val_t *iqmatrix = p->iqmatrix;
  const uint16_t *block_eob_rate = p->block_eob_rate;
  const TXB_CTX *txb_ctx = p->txb_ctx;
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int bwl = get_txb_bwl(tx_size);
  const int height = get_txb_high(tx_size);
  assert(p->plane == 0);
  assert(tx_class == TX_CLASS_2D);

  tcq_lf_ctx_t lf_ctx[TCQ_MAX_STATES];
  for (int i = 0; i < TCQ_N_STATES; i++) {
    uint8_t *lev = tcq_levels_cur(tcq_lev, i);
    av1_init_lf_ctx(lev, scan_hi, bwl, &lf_ctx[i]);
  }

  for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
    int blk_pos = scan[scan_pos];

    tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
    tcq_node_t *prd = &decision[TCQ_N_STATES];

    prequant_t pqData;
    int tempdqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
    av1_pre_quant(tcoeff[blk_pos], &pqData, quant, tempdqv, log_scale,
                  scan_pos);

    // init state
    init_tcq_decision(decision);
    const int coeff_sign = tcoeff[blk_pos] < 0;
    const int limits = 1;  // Always in LF region.

    // calculate contexts
    tcq_coeff_ctx_t coeff_ctx;
    int diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, tx_class);
    int eob_ctx = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    int eob_rate = block_eob_rate[scan_pos];
    coeff_ctx.coef_eob = eob_ctx;
    av1_calc_lf_ctx_st8(lf_ctx, scan_pos, &coeff_ctx);

    // calculate rate distortion
    tcq_rate_t rd;
    av1_get_rate_dist_lf_luma(txb_costs, &pqData, &coeff_ctx, blk_pos, diag_ctx,
                              eob_rate, txb_ctx->dc_sign_ctx, tmp_sign, bwl,
                              tx_class, coeff_sign, &rd);

    av1_decide_states(prd, &rd, &pqData, limits, try_eob, rdmult, decision);

    av1_update_lf_ctx(decision, lf_ctx);
  }
}

// General TCQ 8-state, used by non 2D Luma
void trellis_loop(const tcq_param_t *p, int first_scan_pos, int scan_hi,
                  int scan_lo, tcq_levels_t *tcq_lev, tcq_node_t *trellis) {
  int plane = p->plane;
  TX_SIZE tx_size = p->tx_size;
  TX_CLASS tx_class = p->tx_class;
  int log_scale = p->log_scale;
  int sharpness = p->sharpness;
  int try_eob = sharpness == 0;
  int64_t rdmult = p->rdmult;
  const int16_t *scan = p->scan;
  const int32_t *tmp_sign = p->tmp_sign;
  const tran_low_t *tcoeff = p->tcoeff;
  const int32_t *quant = p->quant;
  const int32_t *dequant = p->dequant;
  const qm_val_t *iqmatrix = p->iqmatrix;
  const uint16_t *block_eob_rate = p->block_eob_rate;
  const TXB_CTX *txb_ctx = p->txb_ctx;
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int bwl = get_txb_bwl(tx_size);
  const int height = get_txb_high(tx_size);
  DecideStateFnc f_decide_states = av1_decide_states;
  GetDefLumaRateDistFnc f_get_rate_dist_def_luma = av1_get_rate_dist_def_luma;
  GetLfLumaRateDistFnc f_get_rate_dist_lf_luma = av1_get_rate_dist_lf_luma;

  for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
    tcq_levels_swap(tcq_lev);
    uint8_t *levels[TCQ_MAX_STATES];
    uint8_t *prev_levels[TCQ_MAX_STATES];
    for (int i = 0; i < TCQ_N_STATES; i++) {
      prev_levels[i] = tcq_levels_prev(tcq_lev, i);
      levels[i] = tcq_levels_cur(tcq_lev, i);
    }

    int blk_pos = scan[scan_pos];
    int row = blk_pos >> bwl;
    int col = blk_pos - (row << bwl);
    int limits = get_lf_limits(row, col, tx_class, plane);

    tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
    tcq_node_t *prd = &decision[TCQ_N_STATES];

    prequant_t pqData;
    int tempdqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
    av1_pre_quant(tcoeff[blk_pos], &pqData, quant, tempdqv, log_scale,
                  scan_pos);

    // init state
    init_tcq_decision(decision);
    const int coeff_sign = tcoeff[blk_pos] < 0;

    // calculate contexts

    tcq_coeff_ctx_t coeff_ctx;
    int eob_ctx = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    int eob_rate = block_eob_rate[scan_pos];
    coeff_ctx.coef_eob = eob_ctx;

    tcq_rate_t rd;

    // Calculate contexts and rate distortion
    if (limits) {
      if (plane == 0) {
        int diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, tx_class);
        for (int i = 0; i < TCQ_N_STATES; i++) {
          int base_ctx =
              get_lower_levels_lf_ctx(prev_levels[i], blk_pos, bwl, tx_class);
          int br_ctx = get_br_lf_ctx(prev_levels[i], blk_pos, bwl, tx_class);
          coeff_ctx.coef[i] = base_ctx - diag_ctx + (br_ctx << 4);
        }
        f_get_rate_dist_lf_luma(txb_costs, &pqData, &coeff_ctx, blk_pos,
                                diag_ctx, eob_rate, txb_ctx->dc_sign_ctx,
                                tmp_sign, bwl, tx_class, coeff_sign, &rd);
      } else {
        int diag_ctx = get_nz_map_ctx_from_stats_lf_chroma(0, tx_class, plane);
        for (int i = 0; i < TCQ_N_STATES; i++) {
          int base_ctx = get_lower_levels_lf_ctx_chroma(prev_levels[i], blk_pos,
                                                        bwl, tx_class, plane);
          int br_ctx =
              get_br_lf_ctx_chroma(prev_levels[i], blk_pos, bwl, tx_class);
          coeff_ctx.coef[i] = base_ctx - diag_ctx + (br_ctx << 4);
        }
        av1_get_rate_dist_lf_chroma(txb_costs, &pqData, &coeff_ctx, blk_pos,
                                    diag_ctx, eob_rate, txb_ctx->dc_sign_ctx,
                                    tmp_sign, bwl, tx_class, plane, coeff_sign,
                                    &rd);
      }
    } else {
      if (plane == 0) {
        int diag_ctx = get_nz_map_ctx_from_stats(0, blk_pos, bwl, tx_class, 0);
        for (int i = 0; i < TCQ_N_STATES; i++) {
          int base_ctx = get_lower_levels_ctx(prev_levels[i], blk_pos, bwl,
                                              tx_class, plane);
          int br_ctx = get_br_ctx(prev_levels[i], blk_pos, bwl, tx_class);
          coeff_ctx.coef[i] = base_ctx - diag_ctx + (br_ctx << 4);
        }
        f_get_rate_dist_def_luma(txb_costs, &pqData, &coeff_ctx, blk_pos, bwl,
                                 tx_class, diag_ctx, eob_rate, &rd);
      } else {
        int diag_ctx =
            get_nz_map_ctx_from_stats_chroma(0, blk_pos, tx_class, plane);
        for (int i = 0; i < TCQ_N_STATES; i++) {
          int base_ctx = get_lower_levels_ctx_chroma(prev_levels[i], blk_pos,
                                                     bwl, tx_class, plane);
          int br_ctx =
              get_br_ctx_chroma(prev_levels[i], blk_pos, bwl, tx_class);
          coeff_ctx.coef[i] = base_ctx - diag_ctx + (br_ctx << 4);
        }
        av1_get_rate_dist_def_chroma(txb_costs, &pqData, &coeff_ctx, blk_pos,
                                     bwl, tx_class, diag_ctx, eob_rate, plane,
                                     tmp_sign[blk_pos], coeff_sign, &rd);
      }
    }

    f_decide_states(prd, &rd, &pqData, limits, try_eob, rdmult, decision);

    // copy corresponding context from previous level buffer
    for (int state = 0; state < TCQ_N_STATES && scan_pos != first_scan_pos;
         state++) {
      int prevId = decision[state].prevId;
      if (prevId >= 0)
        memcpy(levels[state], prev_levels[prevId],
               sizeof(uint8_t) * tcq_lev->bufsize);
    }

    // update levels_buf
    for (int state = 0; state < TCQ_N_STATES && scan_pos != 0; state++) {
      set_levels_buf(decision[state].prevId, decision[state].absLevel,
                     levels[state], scan, first_scan_pos, scan_pos, bwl,
                     sharpness);
    }
  }
}

// Pre-calculate eob bits (rate) for each EOB candidate position from 1
// to the initial eob location. Store rate in array block_eob_rate[],
// starting with index.
void av1_calc_block_eob_rate_c(struct macroblock *x, int plane, TX_SIZE tx_size,
                               int eob, uint16_t *block_eob_rate) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  const CoeffCosts *coeff_costs = &x->coeff_costs;
  const LV_MAP_COEFF_COST *txb_costs =
      &coeff_costs->coeff_costs[txs_ctx][plane_type];
  const int eob_multi_size = txsize_log2_minus4[tx_size];
  const LV_MAP_EOB_COST *txb_eob_costs =
      &coeff_costs->eob_costs[eob_multi_size][plane_type];

#if CONFIG_EOB_POS_LUMA
  const int *tbl_eob_cost = txb_eob_costs->eob_cost[is_inter];
#else
  const int *tbl_eob_cost = txb_eob_costs->eob_cost;
#endif

  block_eob_rate[0] = tbl_eob_cost[0];
  block_eob_rate[1] = tbl_eob_cost[1];
  int scan_pos = 2;
  int n_offset_bits = 0;
  while (scan_pos < eob) {
    int eob_pt_rate = tbl_eob_cost[2 + n_offset_bits];
    for (int bit = 0; bit < 2; bit++) {
      int eob_ctx = n_offset_bits;
      int extra_bit_rate = txb_costs->eob_extra_cost[eob_ctx][bit];
      int eob_rate =
          eob_pt_rate + extra_bit_rate + av1_cost_literal(n_offset_bits);
      for (int i = 0; i < (1 << n_offset_bits); i++) {
        block_eob_rate[scan_pos++] = eob_rate;
      }
    }
    n_offset_bits++;
  }
}

// Determine the best quantization option for each coeff from DC to EOB
int av1_find_best_path_c(const struct tcq_node_t *trellis, const int16_t *scan,
                         const int32_t *dequant, const qm_val_t *iqmatrix,
                         const tran_low_t *tcoeff, int first_scan_pos,
                         int log_scale, tran_low_t *qcoeff, tran_low_t *dqcoeff,
                         int *min_rate, int64_t *min_cost) {
  int64_t min_path_cost = INT64_MAX;
  int trel_min_rate = INT32_MAX;
  int prev_id = -2;
  for (int state = 0; state < TCQ_N_STATES; state++) {
    const tcq_node_t *decision = &trellis[state];
    if (decision->rdCost < min_path_cost) {
      prev_id = state;
      min_path_cost = decision->rdCost;
      trel_min_rate = decision->rate;
    }
  }

  // Backtrack to reconstruct qcoeff / dqcoeff blocks.
  int scan_pos = 0;
  if (!iqmatrix) {
    int dqv = dequant[0];
    int dqv_ac = dequant[1];
    for (; prev_id >= 0; scan_pos++) {
      const tcq_node_t *decision =
          &trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
      prev_id = decision->prevId;
      int abs_level = decision->absLevel;
      int blk_pos = scan[scan_pos];
      int sign = -(tcoeff[blk_pos] < 0);
      int q_i = prev_id >= 0 ? tcq_quant(prev_id) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - q_i);
      int dqc = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qc * dqv,
                                                  QUANT_TABLE_BITS) >>
                log_scale;
      qcoeff[blk_pos] = (abs_level ^ sign) - sign;
      dqcoeff[blk_pos] = (dqc ^ sign) - sign;
      dqv = dqv_ac;
    }
  } else {
    for (; prev_id >= 0; scan_pos++) {
      const tcq_node_t *decision =
          &trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
      prev_id = decision->prevId;
      int abs_level = decision->absLevel;
      int blk_pos = scan[scan_pos];
      int sign = tcoeff[blk_pos] < 0;
      qcoeff[blk_pos] = sign ? -abs_level : abs_level;
      int dqv = get_dqv(dequant, blk_pos, iqmatrix);
      int q_i = prev_id >= 0 ? tcq_quant(prev_id) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - q_i);
      int dqc = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qc * dqv,
                                                  QUANT_TABLE_BITS) >>
                log_scale;
      dqcoeff[blk_pos] = sign ? -dqc : dqc;
    }
  }
  int eob = scan_pos;

  for (; scan_pos <= first_scan_pos; scan_pos++) {
    int blk_pos = scan[scan_pos];
    qcoeff[blk_pos] = 0;
    dqcoeff[blk_pos] = 0;
  }

  *min_rate = trel_min_rate;
  *min_cost = min_path_cost;
  return eob;
}

/*
Algorithem description:
Unlike regular scaler quantization, trellis coded quant has dependency on
already coded coeffs in the decoder side. In order to correctly build the
dependency, the encoder creates a trellis and implements dependency in it.

The encoder flow:
1. Start at the first candidate EOB position and proceed towards DC (coeff[0])

2. For each of the states, the RD cost is calculated for each state transition,
keeping the best option out of the 2 (or 3) incoming candidates at each state.
the state quantizer mapping and state transition table are shown below. In
addition, it checks NEW_EOB at each of the coeff position and the next state is
0/4 in this case.

3. After the last coeff is processed, pick the lowest RD cost out of states 0-3
and all_zero, back track until it reaches eob.

* State-quantizer mapping and state transition table *
---------------------------------------------------------
| Current state | Quantizer | Parity of Qk | Next state |
|--------------------------------------------------------
|               |           |    Even      |     0      |
|       0       |     0     |----------------------------
|               |           |    Odd       |     4      |
|--------------------------------------------------------
|               |           |    Even      |     4      |
|       1       |     0     |----------------------------
|               |           |    Odd       |     0      |
|--------------------------------------------------------
|               |           |    Even      |     1      |
|       2       |     1     |----------------------------
|               |           |    Odd       |     5      |
|--------------------------------------------------------
|               |           |    Even      |     5      |
|       3       |     1     |----------------------------
|               |           |    Odd       |     1      |
|--------------------------------------------------------
|               |           |    Even      |     6      |
|       4       |     0     |----------------------------
|               |           |    Odd       |     2      |
|--------------------------------------------------------
|               |           |    Even      |     2      |
|       5       |     0     |----------------------------
|               |           |    Odd       |     6      |
|--------------------------------------------------------
|               |           |    Even      |     7      |
|       6       |     1     |----------------------------
|               |           |    Odd       |     3      |
|--------------------------------------------------------
|               |           |    Even      |     3      |
|       7       |     1     |----------------------------
|               |           |    Odd       |     7      |
|--------------------------------------------------------
*/
int av1_trellis_quant(const struct AV1_COMP *cpi, MACROBLOCK *x, int plane,
                      int block, TX_SIZE tx_size, TX_TYPE tx_type,
                      CctxType cctx_type, const TXB_CTX *const txb_ctx,
                      int *rate_cost, int sharpness) {
  MACROBLOCKD *xd = &x->e_mbd;
  const struct macroblock_plane *p = &x->plane[plane];

  const SCAN_ORDER *scan_order =
      get_scan(tx_size, get_primary_tx_type(tx_type));

  const int16_t *scan = scan_order->scan;
  int eob = p->eobs[block];

  const int32_t *dequant = p->dequant_QTX;
  const int32_t *quant = p->quant_fp_QTX;  // quant_QTX

  const qm_val_t *iqmatrix =
      av1_get_iqmatrix(&cpi->common.quant_params, xd, plane, tx_size, tx_type);
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *qcoeff = p->qcoeff + block_offset;
  tran_low_t *dqcoeff = p->dqcoeff + block_offset;
  const tran_low_t *tcoeff = p->coeff + block_offset;
  const CoeffCosts *coeff_costs = &x->coeff_costs;

  // This function is not called if eob = 0.
  assert(eob > 0);

  const AV1_COMMON *cm = &cpi->common;
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);

  const TX_CLASS tx_class = tx_type_to_class[get_primary_tx_type(tx_type)];

  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int bwl = get_txb_bwl(tx_size);
  const int width = get_txb_wide(tx_size);
  const int height = get_txb_high(tx_size);
  assert(width == (1 << bwl));

  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int bob_code = p->bobs[block];
  const int is_fsc = (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      plane == PLANE_TYPE_Y) ||
                     use_inter_fsc(&cpi->common, plane, tx_type, is_inter);
  const LV_MAP_COEFF_COST *txb_costs =
      &coeff_costs->coeff_costs[txs_ctx][plane_type];

  const int rshift =
      (sharpness +
       (cpi->oxcf.q_cfg.aq_mode == VARIANCE_AQ && mbmi->segment_id < 4
            ? 7 - mbmi->segment_id
            : 2) +
       (cpi->oxcf.q_cfg.aq_mode != VARIANCE_AQ &&
                cpi->oxcf.q_cfg.deltaq_mode == DELTA_Q_PERCEPTUAL &&
                cm->delta_q_info.delta_q_present_flag && x->sb_energy_level < 0
            ? (3 - x->sb_energy_level)
            : 0));
  int64_t rdmult = (((int64_t)x->rdmult * (plane_rd_mult[is_inter][plane_type]
                                           << (2 * (xd->bd - 8)))) +
                    2) >>
                   rshift;

  // getting context from previous level buf, updating levels on current level
  // buf. initialization all value by 0, since we update every position.
  int bufsize = (width + 4) * (height + 4) + TX_PAD_END;
  int mem_tcq_sz = sizeof(uint8_t) * bufsize * (2 << TCQ_N_STATES_LOG);
  uint8_t *mem_tcq = (uint8_t *)malloc(mem_tcq_sz);
  if (!mem_tcq) {
    exit(1);
  }
  if (eob > 1) {
    memset(mem_tcq, 0, mem_tcq_sz);
  }
  tcq_levels_t tcq_lev;
  tcq_levels_init(&tcq_lev, mem_tcq, bufsize);

  int si = eob - 1;
  // populate trellis
  assert(si < MAX_TRELLIS);
  tcq_node_t trellis[MAX_TRELLIS * TCQ_MAX_STATES];

  // Ping-pong buffers for diagonal contexts.
  tcq_ctx_t tcq_ctx[2 * TCQ_MAX_STATES];

  // Precalc block eob rate.
  uint16_t block_eob_rate[MAX_TRELLIS];
  av1_calc_block_eob_rate(x, plane, tx_size, eob, block_eob_rate);

  // Collect TCQ related parameters.
  tcq_param_t param;
  int log_scale = av1_get_tx_scale(tx_size) + 1;
  param.plane = plane;
  param.tx_size = tx_size;
  param.tx_class = tx_class;
  param.sharpness = sharpness;
  param.rdmult = rdmult;
  param.log_scale = log_scale;
  param.scan = scan;
  param.tmp_sign = xd->tmp_sign;
  param.qcoeff = qcoeff;
  param.tcoeff = tcoeff;
  param.quant = quant;
  param.dequant = dequant;
  param.iqmatrix = iqmatrix;
  param.block_eob_rate = block_eob_rate;
  param.txb_ctx = txb_ctx;
  param.txb_costs = txb_costs;

  // Start of TCQ
  int first_scan_pos = si;
  // Process the first position
  trellis_first_pos(&param, first_scan_pos, &tcq_lev, trellis);

  int scan_hi = first_scan_pos - 1;

  if (scan_hi >= 0) {
    if (plane == 0 && tx_class == TX_CLASS_2D) {
      // Speed-up version for 2D Luma by exploiting parallelism
      // Process non-lf region diagonal-by-diagonal followed by lf regsion
      const int scan_lf_start = 9;
      while (scan_hi > scan_lf_start) {
        int blk_pos = scan[scan_hi];
        int row = blk_pos >> bwl;
        int col = blk_pos - (row << bwl);
        int inc = AOMMIN(height - 1 - row, col);
        int scan_lo = AOMMAX(scan_lf_start + 1, scan_hi - inc);
        trellis_loop_diagonal_st8(&param, scan_hi, scan_lo, &tcq_lev, tcq_ctx,
                                  trellis);
        scan_hi = scan_lo - 1;
      }
      trellis_loop_lf_st8(&param, scan_hi, 0, &tcq_lev, trellis);
    } else {
      trellis_loop(&param, first_scan_pos, scan_hi, 0, &tcq_lev, trellis);
    }
  }

  free(mem_tcq);

  // find best path
  int min_rate = INT32_MAX;
  int64_t min_path_cost = INT64_MAX;
  eob = av1_find_best_path(trellis, scan, dequant, iqmatrix, tcoeff,
                           first_scan_pos, log_scale, qcoeff, dqcoeff,
                           &min_rate, &min_path_cost);

#if CONFIG_CONTEXT_DERIVATION
  int txb_skip_ctx = txb_ctx->txb_skip_ctx;
  int non_skip_cost = 0;
  int skip_cost = 0;
  if (plane == AOM_PLANE_V) {
    txb_skip_ctx +=
        (x->plane[AOM_PLANE_U].eobs[block] ? V_TXB_SKIP_CONTEXT_OFFSET : 0);
    non_skip_cost = txb_costs->v_txb_skip_cost[txb_skip_ctx][0];
    skip_cost = txb_costs->v_txb_skip_cost[txb_skip_ctx][1];
  } else {
#if CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
    const int pred_mode_ctx =
        (is_inter || mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) ? 1 : 0;
    non_skip_cost = txb_costs->txb_skip_cost[pred_mode_ctx][txb_skip_ctx][0];
    skip_cost = txb_costs->txb_skip_cost[pred_mode_ctx][txb_skip_ctx][1];
#else
    non_skip_cost = txb_costs->txb_skip_cost[txb_skip_ctx][0];
    skip_cost = txb_costs->txb_skip_cost[txb_skip_ctx][1];
#endif  // CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
  }
#else
  const int non_skip_cost = txb_costs->txb_skip_cost[txb_ctx->txb_skip_ctx][0];
  const int skip_cost = txb_costs->txb_skip_cost[txb_ctx->txb_skip_ctx][1];
#endif  // CONFIG_CONTEXT_DERIVATION

  int accu_rate = 0;
  set_bob(x, plane, block, tx_size, tx_type);

  if (eob == 0)
    assert(0);  // in current implementation, this could not happen.
  else {
    const int tx_type_cost = get_tx_type_cost(x, xd, plane, tx_size, tx_type,
                                              cm->features.reduced_tx_set_used,
                                              eob, bob_code, is_fsc);
    int64_t rd_cost_skip = RDCOST(rdmult, skip_cost, 0);
    accu_rate = non_skip_cost + tx_type_cost + min_rate;
    int64_t rd_cost_coded =
        min_path_cost +
        (int64_t)RDCOST(rdmult, non_skip_cost + tx_type_cost, 0);
    // skip block
    if ((rd_cost_coded > rd_cost_skip) && sharpness == 0) {
      for (int scan_idx = 0; scan_idx <= first_scan_pos; scan_idx++) {
        int blk_idx = scan[scan_idx];
        qcoeff[blk_idx] = 0;
        dqcoeff[blk_idx] = 0;
      }
      accu_rate = skip_cost;
      eob = 0;
    }
  }

  p->eobs[block] = eob;
  p->txb_entropy_ctx[block] =
      av1_get_txb_entropy_context(qcoeff, scan_order, p->eobs[block]);

  accu_rate += get_cctx_type_cost(cm, x, xd, plane, tx_size, block, cctx_type);
  *rate_cost = accu_rate;

  return eob;
}
