/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>
#include <assert.h>
#include <string.h>

#include "avm/avm_integer.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/cost.h"
#include "av2/common/quant_common.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/rd.h"
#include "av2/encoder/trellis_quant.h"
#include "config/av2_rtcd.h"

static AVM_FORCE_INLINE void get_coeff_ctx_neon(const struct tcq_ctx_t *tcq_ctx,
                                                int col,
                                                struct tcq_coeff_ctx_t *cc) {
  uint8x8_t orig = vld1_u8((const uint8_t *)tcq_ctx->orig_st);
  uint8x8_t mag = vld1_u8(tcq_ctx->ctx[col]);
  uint8x8_t neg_mask = vcge_u8(orig, vdup_n_u8(0x80));
  uint8x8_t ctx = vtbl1_u8(mag, orig);
  ctx = vbic_u8(ctx, neg_mask);
  vst1_u8(cc->coef, ctx);
}

static AVM_FORCE_INLINE void pre_quant_neon(tran_low_t tqc,
                                            struct prequant_t *pq, int dqv,
                                            int log_scale) {
  static const int32_t kInc[4][4] = {
    { 0, 1, 2, 3 }, { 3, 0, 1, 2 }, { 2, 3, 0, 1 }, { 1, 2, 3, 0 }
  };

  int32_t abs_tqc = abs(tqc);
  pq->qIdx = AVMMAX(pq->orig_qIdx - 2, 1);
  int32_t qIdx = pq->qIdx;

  int32x4_t base_qc = vdupq_n_s32(qIdx);
  int32x4_t qc_inc = vld1q_s32(kInc[qIdx & 3]);
  int32x4_t qc_idx = vaddq_s32(base_qc, qc_inc);
  int32x4_t abslev = vshrq_n_s32(vaddq_s32(qc_idx, vdupq_n_s32(1)), 1);
  vst1q_s32(pq->absLevel, abslev);

  int32_t abs_tqc_sh = abs_tqc << (log_scale - 1);
  int shift = log_scale + QUANT_TABLE_BITS;
  int64_t dq_round = 1LL << (QUANT_TABLE_BITS - 1);

  int32x2_t v_dqv = vdup_n_s32(dqv);
  int64x2_t v_round = vdupq_n_s64(dq_round);
  int64x2_t v_neg_shift = vdupq_n_s64(-shift);

  int64x2_t prod01 = vaddq_s64(vmull_s32(vget_low_s32(qc_idx), v_dqv), v_round);
  int64x2_t prod23 =
      vaddq_s64(vmull_s32(vget_high_s32(qc_idx), v_dqv), v_round);
  prod01 = vshlq_s64(prod01, v_neg_shift);
  prod23 = vshlq_s64(prod23, v_neg_shift);

  int32x4_t dqc = vcombine_s32(vmovn_s64(prod01), vmovn_s64(prod23));
  int32x4_t diff = vsubq_s32(vshlq_s32(dqc, vdupq_n_s32(log_scale - 1)),
                             vdupq_n_s32(abs_tqc_sh));

  int32x2_t d01 = vget_low_s32(diff);
  int32x2_t d23 = vget_high_s32(diff);
  int32x2_t at_lo = vdup_n_s32(abs_tqc_sh);

  int64x2_t dd01 = vsubq_s64(vmull_s32(d01, d01), vmull_s32(at_lo, at_lo));
  int64x2_t dd23 = vsubq_s64(vmull_s32(d23, d23), vmull_s32(at_lo, at_lo));

  vst1q_s64(&pq->deltaDist[0], vshlq_n_s64(dd01, RDDIV_BITS));
  vst1q_s64(&pq->deltaDist[2], vshlq_n_s64(dd23, RDDIV_BITS));
}

static AVM_FORCE_INLINE void pre_quant_q1_neon(tran_low_t tqc,
                                               struct prequant_t *pq, int dqv,
                                               int log_scale) {
  int32_t abs_tqc = abs(tqc);
  pq->qIdx = 1;
  pq->absLevel[1] = 1;
  pq->absLevel[2] = 1;

  int64_t dq_round = 1LL << (QUANT_TABLE_BITS - 1);
  int shift = QUANT_TABLE_BITS + log_scale;
  int32_t dqca = (int32_t)(((int64_t)dqv + dq_round) >> shift);
  int32_t dqcb = (int32_t)((((int64_t)dqv << 1) + dq_round) >> shift);

  int32_t abs_tqc_sh = abs_tqc << (log_scale - 1);
  int32_t diff_a = (dqca << (log_scale - 1)) - abs_tqc_sh;
  int32_t diff_b = (dqcb << (log_scale - 1)) - abs_tqc_sh;

  int32x2_t diffs =
      vcreate_s32((uint32_t)diff_a | ((uint64_t)(uint32_t)diff_b << 32));
  int32x2_t at_lo = vdup_n_s32(abs_tqc_sh);

  int64x2_t dd = vsubq_s64(vmull_s32(diffs, diffs), vmull_s32(at_lo, at_lo));

  vst1q_s64(&pq->deltaDist[1], vshlq_n_s64(dd, RDDIV_BITS));
}

static AVM_FORCE_INLINE void update_states_neon(const tcq_node_t *decision,
                                                int col,
                                                struct tcq_ctx_t *tcq_ctx) {
  _Static_assert(sizeof(tcq_node_t) == 16, "update_states_neon layout");
  const int32_t *d = (const int32_t *)decision;
  int32x4_t info_lo = vcombine_s32(
      vcreate_s32((uint32_t)d[3] | ((uint64_t)(uint32_t)d[7] << 32)),
      vcreate_s32((uint32_t)d[11] | ((uint64_t)(uint32_t)d[15] << 32)));
  int32x4_t info_hi = vcombine_s32(
      vcreate_s32((uint32_t)d[19] | ((uint64_t)(uint32_t)d[23] << 32)),
      vcreate_s32((uint32_t)d[27] | ((uint64_t)(uint32_t)d[31] << 32)));

  int32x4_t abs_lo = vandq_s32(info_lo, vdupq_n_s32(0x00FFFFFF));
  int32x4_t abs_hi = vandq_s32(info_hi, vdupq_n_s32(0x00FFFFFF));
  abs_lo = vminq_s32(abs_lo, vdupq_n_s32(MAX_VAL_BR_CTX));
  abs_hi = vminq_s32(abs_hi, vdupq_n_s32(MAX_VAL_BR_CTX));
  uint16x4_t abs_lo16 = vmovn_u32(vreinterpretq_u32_s32(abs_lo));
  uint16x4_t abs_hi16 = vmovn_u32(vreinterpretq_u32_s32(abs_hi));
  uint8x8_t abs_u8 = vmovn_u16(vcombine_u16(abs_lo16, abs_hi16));
  vst1_u8(tcq_ctx->lev_new[col], abs_u8);

  int32x4_t pid_lo = vshrq_n_s32(info_lo, 24);
  int32x4_t pid_hi = vshrq_n_s32(info_hi, 24);
  int16x4_t pid_lo16 = vmovn_s32(pid_lo);
  int16x4_t pid_hi16 = vmovn_s32(pid_hi);
  int8x8_t pid_s8 = vmovn_s16(vcombine_s16(pid_lo16, pid_hi16));
  vst1_s8(tcq_ctx->prev_st[col], pid_s8);

  uint8x8_t orig = vld1_u8((const uint8_t *)tcq_ctx->orig_st);
  uint8x8_t upid = vreinterpret_u8_s8(pid_s8);
  uint8x8_t neg_mask = vcge_u8(upid, vdup_n_u8(0x80));
  uint8x8_t mapped = vtbl1_u8(orig, upid);
  mapped = vbic_u8(mapped, neg_mask);
  mapped = vorr_u8(mapped, neg_mask);
  vst1_u8((uint8_t *)tcq_ctx->orig_st, mapped);
}

static AVM_FORCE_INLINE int64_t rdcost1(int64_t rdmult, int32_t rate) {
  return (((int64_t)(uint32_t)rate * rdmult) +
          (1 << (AV2_PROB_COST_SHIFT - 1))) >>
         AV2_PROB_COST_SHIFT;
}

static AVM_FORCE_INLINE void node_general(int64_t costA, int64_t costB,
                                          int64_t cost_zero, int32_t rateA,
                                          int32_t rateB, int32_t rate_zero,
                                          int32_t absA, int32_t absB,
                                          int32_t prev_rate, int pid,
                                          tcq_node_t *d0, tcq_node_t *d1) {
  assert(tcq_parity(absA) == 0);
  assert(tcq_parity(absB) == 1);
  rateA += prev_rate;
  rateB += prev_rate;
  rate_zero += prev_rate;
  if (cost_zero < costA && cost_zero < d0->rdCost + 1) {
    d0->rdCost = cost_zero;
    d0->rate = rate_zero;
    d0->prevId = pid;
    d0->absLevel = 0;
  } else if (costA < d0->rdCost + 1) {
    d0->rdCost = costA;
    d0->rate = rateA;
    d0->prevId = pid;
    d0->absLevel = absA;
  }
  if (costB < d1->rdCost) {
    d1->rdCost = costB;
    d1->rate = rateB;
    d1->prevId = pid;
    d1->absLevel = absB;
  }
}

static AVM_FORCE_INLINE void node_q1(int64_t costB, int64_t cost_zero,
                                     int32_t rateB, int32_t rate_zero,
                                     int32_t absB, int32_t prev_rate, int pid,
                                     tcq_node_t *d0, tcq_node_t *d1) {
  assert(tcq_parity(absB) == 1);
  rateB += prev_rate;
  rate_zero += prev_rate;
  if (cost_zero < d0->rdCost + 1) {
    d0->rdCost = cost_zero;
    d0->rate = rate_zero;
    d0->prevId = pid;
    d0->absLevel = 0;
  }
  if (costB < d1->rdCost) {
    d1->rdCost = costB;
    d1->rate = rateB;
    d1->prevId = pid;
    d1->absLevel = absB;
  }
}

static AVM_FORCE_INLINE void init_decision_8(tcq_node_t *decision) {
  static const tcq_node_t def = { INT64_MAX >> 10, 0, -1, -2 };
  uint8x16_t def_vec = vld1q_u8((const uint8_t *)&def);
  vst1q_u8((uint8_t *)&decision[0], def_vec);
  vst1q_u8((uint8_t *)&decision[1], def_vec);
  vst1q_u8((uint8_t *)&decision[2], def_vec);
  vst1q_u8((uint8_t *)&decision[3], def_vec);
  vst1q_u8((uint8_t *)&decision[4], def_vec);
  vst1q_u8((uint8_t *)&decision[5], def_vec);
  vst1q_u8((uint8_t *)&decision[6], def_vec);
  vst1q_u8((uint8_t *)&decision[7], def_vec);
}

static AVM_FORCE_INLINE void decide_states_neon_impl(
    const tcq_node_t *prev, const tcq_rate_t *rd, const prequant_t *pq,
    int try_eob, int64_t rdmult, uint32x2_t v_rdm, uint64x2_t v_rnd,
    tcq_node_t *decision) {
  assert((rdmult >> 32) == 0);
  const int32_t *rate = rd->rate;
  const int32_t *rz = rd->rate_zero;
  const int32_t *re = rd->rate_eob;
  int64_t rc[16], rcz[8];
  init_decision_8(decision);

  const int64_t *ds = pq->deltaDist;

  for (int i = 0; i < 8; i += 2) {
    int a0 = (i & 2) ? 1 : 0;

    uint32x2x2_t r_pair = vld2_u32((const uint32_t *)&rate[2 * i]);
    uint32x2_t r_ev = r_pair.val[0];
    uint32x2_t r_od = r_pair.val[1];
    uint32x2_t r_zr = vld1_u32((const uint32_t *)&rz[i]);

    int64x2_t c_ev = vreinterpretq_s64_u64(vshrq_n_u64(
        vaddq_u64(vmull_u32(r_ev, v_rdm), v_rnd), AV2_PROB_COST_SHIFT));
    int64x2_t c_od = vreinterpretq_s64_u64(vshrq_n_u64(
        vaddq_u64(vmull_u32(r_od, v_rdm), v_rnd), AV2_PROB_COST_SHIFT));
    int64x2_t c_zr = vreinterpretq_s64_u64(vshrq_n_u64(
        vaddq_u64(vmull_u32(r_zr, v_rdm), v_rnd), AV2_PROB_COST_SHIFT));

    int64x2_t d_ev = vdupq_n_s64(ds[a0]);
    int64x2_t d_od = vdupq_n_s64(ds[a0 + 2]);

    c_ev = vaddq_s64(c_ev, d_ev);
    c_od = vaddq_s64(c_od, d_od);

    int64x2_t prd = vcombine_s64(vcreate_s64(prev[i].rdCost),
                                 vcreate_s64(prev[i + 1].rdCost));
    c_ev = vaddq_s64(c_ev, prd);
    c_od = vaddq_s64(c_od, prd);
    c_zr = vaddq_s64(c_zr, prd);

    rc[2 * i] = vgetq_lane_s64(c_ev, 0);
    rc[2 * (i + 1)] = vgetq_lane_s64(c_ev, 1);
    rc[2 * i + 1] = vgetq_lane_s64(c_od, 0);
    rc[2 * (i + 1) + 1] = vgetq_lane_s64(c_od, 1);
    rcz[i] = vgetq_lane_s64(c_zr, 0);
    rcz[i + 1] = vgetq_lane_s64(c_zr, 1);
  }

  node_general(rc[0], rc[1], rcz[0], rate[0], rate[1], rz[0], pq->absLevel[0],
               pq->absLevel[2], prev[0].rate, 0, &decision[0], &decision[4]);
  node_general(rc[2], rc[3], rcz[1], rate[2], rate[3], rz[1], pq->absLevel[0],
               pq->absLevel[2], prev[1].rate, 1, &decision[4], &decision[0]);
  node_general(rc[5], rc[4], rcz[2], rate[5], rate[4], rz[2], pq->absLevel[3],
               pq->absLevel[1], prev[2].rate, 2, &decision[1], &decision[5]);
  node_general(rc[7], rc[6], rcz[3], rate[7], rate[6], rz[3], pq->absLevel[3],
               pq->absLevel[1], prev[3].rate, 3, &decision[5], &decision[1]);
  node_general(rc[8], rc[9], rcz[4], rate[8], rate[9], rz[4], pq->absLevel[0],
               pq->absLevel[2], prev[4].rate, 4, &decision[6], &decision[2]);
  node_general(rc[10], rc[11], rcz[5], rate[10], rate[11], rz[5],
               pq->absLevel[0], pq->absLevel[2], prev[5].rate, 5, &decision[2],
               &decision[6]);
  node_general(rc[13], rc[12], rcz[6], rate[13], rate[12], rz[6],
               pq->absLevel[3], pq->absLevel[1], prev[6].rate, 6, &decision[7],
               &decision[3]);
  node_general(rc[15], rc[14], rcz[7], rate[15], rate[14], rz[7],
               pq->absLevel[3], pq->absLevel[1], prev[7].rate, 7, &decision[3],
               &decision[7]);

  if (try_eob) {
    int64_t re0 = rdcost1(rdmult, re[0]) + pq->deltaDist[0];
    int64_t re1 = rdcost1(rdmult, re[1]) + pq->deltaDist[2];
    if (re0 < decision[0].rdCost) {
      decision[0].rdCost = re0;
      decision[0].rate = re[0];
      decision[0].prevId = -1;
      decision[0].absLevel = pq->absLevel[0];
    }
    if (re1 < decision[4].rdCost) {
      decision[4].rdCost = re1;
      decision[4].rate = re[1];
      decision[4].prevId = -1;
      decision[4].absLevel = pq->absLevel[2];
    }
  }
}

static AVM_FORCE_INLINE void decide_states_q1_neon_impl(
    const tcq_node_t *prev, const tcq_rate_t *rd, const prequant_t *pq,
    int try_eob, int64_t rdmult, uint32x2_t v_rdm, uint64x2_t v_rnd,
    tcq_node_t *decision) {
  assert((rdmult >> 32) == 0);
  const int32_t *rate = rd->rate;
  const int32_t *rz = rd->rate_zero;
  const int32_t *re = rd->rate_eob;
  init_decision_8(decision);

  int64_t d1s = pq->deltaDist[1];
  int64_t d2s = pq->deltaDist[2];

  int64_t rc[16];
  int64_t rcz[8];
  for (int i = 0; i < 8; i += 2) {
    uint32x2_t r_zr = vld1_u32((const uint32_t *)&rz[i]);
    int64x2_t c_zr = vreinterpretq_s64_u64(vshrq_n_u64(
        vaddq_u64(vmull_u32(r_zr, v_rdm), v_rnd), AV2_PROB_COST_SHIFT));
    int64x2_t prd = vcombine_s64(vcreate_s64(prev[i].rdCost),
                                 vcreate_s64(prev[i + 1].rdCost));
    c_zr = vaddq_s64(c_zr, prd);
    rcz[i] = vgetq_lane_s64(c_zr, 0);
    rcz[i + 1] = vgetq_lane_s64(c_zr, 1);

    int a0 = (i & 2) ? 1 : 0;
    int ri0, ri1;
    int64_t dist;
    if (a0 == 1) {
      ri0 = 2 * i;
      ri1 = 2 * (i + 1);
      dist = d1s;
    } else {
      ri0 = 2 * i + 1;
      ri1 = 2 * (i + 1) + 1;
      dist = d2s;
    }
    uint32x2_t r_nz = vcreate_u32((uint32_t)rate[ri0] |
                                  ((uint64_t)(uint32_t)rate[ri1] << 32));
    int64x2_t c_nz = vreinterpretq_s64_u64(vshrq_n_u64(
        vaddq_u64(vmull_u32(r_nz, v_rdm), v_rnd), AV2_PROB_COST_SHIFT));
    c_nz = vaddq_s64(c_nz, vdupq_n_s64(dist));
    c_nz = vaddq_s64(c_nz, prd);
    rc[ri0] = vgetq_lane_s64(c_nz, 0);
    rc[ri1] = vgetq_lane_s64(c_nz, 1);
  }

  node_q1(rc[1], rcz[0], rate[1], rz[0], pq->absLevel[2], prev[0].rate, 0,
          &decision[0], &decision[4]);
  node_q1(rc[3], rcz[1], rate[3], rz[1], pq->absLevel[2], prev[1].rate, 1,
          &decision[4], &decision[0]);
  node_q1(rc[4], rcz[2], rate[4], rz[2], pq->absLevel[1], prev[2].rate, 2,
          &decision[1], &decision[5]);
  node_q1(rc[6], rcz[3], rate[6], rz[3], pq->absLevel[1], prev[3].rate, 3,
          &decision[5], &decision[1]);
  node_q1(rc[9], rcz[4], rate[9], rz[4], pq->absLevel[2], prev[4].rate, 4,
          &decision[6], &decision[2]);
  node_q1(rc[11], rcz[5], rate[11], rz[5], pq->absLevel[2], prev[5].rate, 5,
          &decision[2], &decision[6]);
  node_q1(rc[12], rcz[6], rate[12], rz[6], pq->absLevel[1], prev[6].rate, 6,
          &decision[7], &decision[3]);
  node_q1(rc[14], rcz[7], rate[14], rz[7], pq->absLevel[1], prev[7].rate, 7,
          &decision[3], &decision[7]);

  if (try_eob) {
    int64_t re1 = rdcost1(rdmult, re[1]) + pq->deltaDist[2];
    if (re1 < decision[4].rdCost) {
      decision[4].rdCost = re1;
      decision[4].rate = re[1];
      decision[4].prevId = -1;
      decision[4].absLevel = pq->absLevel[2];
    }
  }
}

// clang-format off
static const uint8_t kGolombOrder[4][4] = {
  { 0, 2, 1, 3 },
  { 3, 1, 0, 2 },
  { 2, 0, 3, 1 },
  { 1, 3, 2, 0 },
};

static const uint8_t kGolombExp0Bits[256] = {
  0,  0,  0,  0,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,
  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,
  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  8,  8,  8,  8,
  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
};
// clang-format on

#define DEFINE_LOAD_UNPACK_COST(name, CTX_DIM)                                 \
  static AVM_FORCE_INLINE void name(                                           \
      const uint16_t(*tbl)[CTX_DIM][TCQ_CTXS][2], int idx, int ctx0,          \
      int ctx1, int ctx2, int ctx3, uint32_t *out_0123,                        \
      uint32_t *out_4567) {                                                    \
    uint32x2_t p02 = vdup_n_u32(0);                                            \
    p02 = vld1_lane_u32((const uint32_t *)&tbl[idx][ctx0][0], p02, 0);         \
    p02 = vld1_lane_u32((const uint32_t *)&tbl[idx][ctx1][0], p02, 1);         \
    uint32x2_t p46 = vdup_n_u32(0);                                            \
    p46 = vld1_lane_u32((const uint32_t *)&tbl[idx][ctx2][1], p46, 0);         \
    p46 = vld1_lane_u32((const uint32_t *)&tbl[idx][ctx3][1], p46, 1);         \
    vst1q_u32(out_0123, vmovl_u16(vreinterpret_u16_u32(p02)));                 \
    vst1q_u32(out_4567, vmovl_u16(vreinterpret_u16_u32(p46)));                 \
  }

DEFINE_LOAD_UNPACK_COST(load_unpack_lf_base_cost, LF_SIG_COEF_CONTEXTS)
DEFINE_LOAD_UNPACK_COST(load_unpack_lf_mid_cost, LF_LEVEL_CONTEXTS)
DEFINE_LOAD_UNPACK_COST(load_unpack_base_cost, SIG_COEF_CONTEXTS)
DEFINE_LOAD_UNPACK_COST(load_unpack_mid_cost, LEVEL_CONTEXTS)

static AVM_FORCE_INLINE void get_rate_dist_def_luma_q1_neon_impl(
    const struct tcq_param_t *p, const struct tcq_coeff_ctx_t *coeff_ctx,
    int diag_ctx, int eob_rate, int try_eob, struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int32_t(*cost_zero)[SIG_COEF_CONTEXTS] = txb_costs->base_cost_zero;
  const uint16_t(*cost_low_tbl)[SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_cost_low_tbl;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);

  const uint8_t *coef = coeff_ctx->coef;
  int base_ctxs[TCQ_N_STATES];
  for (int i = 0; i < TCQ_N_STATES; i++) {
    base_ctxs[i] = (coef[i] & 0x0F) + base_diag_ctx;
    rd->rate_zero[i] = cost_zero[(i >> 1) & 1][base_ctxs[i]];
  }

  const int idx = 0;
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    load_unpack_base_cost(cost_low_tbl, idx, base_ctxs[4 * i],
                          base_ctxs[4 * i + 1], base_ctxs[4 * i + 2],
                          base_ctxs[4 * i + 3], (uint32_t *)&rd->rate[8 * i],
                          (uint32_t *)&rd->rate[8 * i + 4]);
  }

  if (try_eob) {
    const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
        txb_costs->base_eob_cost_tbl;
    int eob_ctx = coeff_ctx->coef_eob;
    rd->rate_eob[1] =
        (int32_t)cost_eob_tbl[idx][eob_ctx][1] + eob_rate;
  }
}

static AVM_FORCE_INLINE void get_rate_dist_def_luma_neon_impl(
    const struct tcq_param_t *p, const struct prequant_t *pq,
    const struct tcq_coeff_ctx_t *coeff_ctx, int diag_ctx, int eob_rate,
    int try_eob, struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const int32_t(*cost_zero)[SIG_COEF_CONTEXTS] = txb_costs->base_cost_zero;
  const uint16_t(*cost_low_tbl)[SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_cost_low_tbl;
  const uint16_t(*cost_mid_tbl)[LEVEL_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->mid_cost_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);
  int mid_diag_ctx = get_mid_diag_ctx(diag_ctx);

  const uint8_t *coef = coeff_ctx->coef;
  int base_ctxs[TCQ_N_STATES];
  for (int i = 0; i < TCQ_N_STATES; i++) {
    base_ctxs[i] = (coef[i] & 0x0F) + base_diag_ctx;
    rd->rate_zero[i] = cost_zero[(i >> 1) & 1][base_ctxs[i]];
  }

  int qIdx = pq->qIdx;
  int idx = AVMMIN(qIdx - 1, 4);

  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    load_unpack_base_cost(cost_low_tbl, idx, base_ctxs[4 * i],
                          base_ctxs[4 * i + 1], base_ctxs[4 * i + 2],
                          base_ctxs[4 * i + 3], (uint32_t *)&rd->rate[8 * i],
                          (uint32_t *)&rd->rate[8 * i + 4]);
  }

  int32x2_t rate_eob = vdup_n_s32(0);
  if (try_eob) {
    const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
        txb_costs->base_eob_cost_tbl;
    int eob_ctx = coeff_ctx->coef_eob;
    uint32_t eob_packed;
    memcpy(&eob_packed, &cost_eob_tbl[idx][eob_ctx][0], 4);
    uint32x4_t eob_wide =
        vmovl_u16(vreinterpret_u16_u32(vcreate_u32(eob_packed)));
    rate_eob = vadd_s32(vget_low_s32(vreinterpretq_s32_u32(eob_wide)),
                        vdup_n_s32(eob_rate));
    vst1_s32(&rd->rate_eob[0], rate_eob);
  }

  if (qIdx > 1) {
    int mid_idx = AVMMIN(qIdx - 1, 10);
    if (try_eob) {
      uint32_t mid_eob_packed;
      memcpy(&mid_eob_packed, &cost_mid_tbl[mid_idx][0][0][0], 4);
      uint32x4_t mid_eob_wide =
          vmovl_u16(vreinterpret_u16_u32(vcreate_u32(mid_eob_packed)));
      rate_eob =
          vadd_s32(rate_eob, vget_low_s32(vreinterpretq_s32_u32(mid_eob_wide)));
      vst1_s32(&rd->rate_eob[0], rate_eob);
    }

    int mid_ctxs[TCQ_N_STATES];
    for (int i = 0; i < TCQ_N_STATES; i++)
      mid_ctxs[i] = (coef[i] >> 4) + mid_diag_ctx;

    for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
      uint32_t mid_0123[4], mid_4567[4];
      load_unpack_mid_cost(cost_mid_tbl, mid_idx, mid_ctxs[4 * i],
                           mid_ctxs[4 * i + 1], mid_ctxs[4 * i + 2],
                           mid_ctxs[4 * i + 3], mid_0123, mid_4567);
      uint32x4_t cur_0123 = vld1q_u32((uint32_t *)&rd->rate[8 * i]);
      uint32x4_t cur_4567 = vld1q_u32((uint32_t *)&rd->rate[8 * i + 4]);
      vst1q_u32((uint32_t *)&rd->rate[8 * i],
                vaddq_u32(cur_0123, vld1q_u32(mid_0123)));
      vst1q_u32((uint32_t *)&rd->rate[8 * i + 4],
                vaddq_u32(cur_4567, vld1q_u32(mid_4567)));
    }
    if (qIdx >= 6) {
      int gol_idx = qIdx - 5;
      if (gol_idx <= 248) {
        const uint8_t *order = kGolombOrder[qIdx & 3];
        const uint8_t *bits = &kGolombExp0Bits[gol_idx];
        int32_t hr0 = (int32_t)bits[order[0]] << AV2_PROB_COST_SHIFT;
        int32_t hr1 = (int32_t)bits[order[1]] << AV2_PROB_COST_SHIFT;
        int32_t hr2 = (int32_t)bits[order[2]] << AV2_PROB_COST_SHIFT;
        int32_t hr3 = (int32_t)bits[order[3]] << AV2_PROB_COST_SHIFT;

        int32x2_t hr_lo =
            vcreate_s32((uint32_t)hr0 | ((int64_t)(uint32_t)hr1 << 32));
        int32x2_t hr_hi =
            vcreate_s32((uint32_t)hr2 | ((int64_t)(uint32_t)hr3 << 32));
        int32x4_t rate_hr_0123 = vcombine_s32(hr_lo, hr_lo);
        int32x4_t rate_hr_4567 = vcombine_s32(hr_hi, hr_hi);

        if (try_eob) {
          rate_eob = vadd_s32(rate_eob, hr_lo);
          vst1_s32(&rd->rate_eob[0], rate_eob);
        }
        for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
          int32x4_t cur_0123 = vld1q_s32(&rd->rate[8 * i]);
          int32x4_t cur_4567 = vld1q_s32(&rd->rate[8 * i + 4]);
          vst1q_s32(&rd->rate[8 * i], vaddq_s32(cur_0123, rate_hr_0123));
          vst1q_s32(&rd->rate[8 * i + 4], vaddq_s32(cur_4567, rate_hr_4567));
        }
      } else {
        int mid_cost0 = get_golomb_cost_tcq(absLevel[0], 0);
        int mid_cost1 = get_golomb_cost_tcq(absLevel[1], 0);
        int mid_cost2 = get_golomb_cost_tcq(absLevel[2], 0);
        int mid_cost3 = get_golomb_cost_tcq(absLevel[3], 0);
        for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
          rd->rate[8 * i] += mid_cost0;
          rd->rate[8 * i + 1] += mid_cost2;
          rd->rate[8 * i + 2] += mid_cost0;
          rd->rate[8 * i + 3] += mid_cost2;
          rd->rate[8 * i + 4] += mid_cost1;
          rd->rate[8 * i + 5] += mid_cost3;
          rd->rate[8 * i + 6] += mid_cost1;
          rd->rate[8 * i + 7] += mid_cost3;
        }
        if (try_eob) {
          rd->rate_eob[0] += mid_cost0;
          rd->rate_eob[1] += mid_cost2;
        }
      }
    }
  }
}

static AVM_FORCE_INLINE int get_mid_cost_lf_dc_neon(
    tran_low_t abs_qc, int sign, int coeff_ctx, int dc_sign_ctx,
    const LV_MAP_COEFF_COST *txb_costs) {
  int cost = 0;
  int mid_ctx = coeff_ctx >> 4;
  const int dc_ph_group = 0;
  cost -= av2_cost_literal(1);
  cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
  if (abs_qc > LF_NUM_BASE_LEVELS)
    cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
  return cost;
}

static AVM_FORCE_INLINE int get_mid_cost_eob_lf_dc_neon(
    int ci, tran_low_t abs_qc, int sign, int dc_sign_ctx,
    const LV_MAP_COEFF_COST *txb_costs) {
  int cost = 0;
  cost -= av2_cost_literal(1);
  cost += txb_costs->dc_sign_cost[0][dc_sign_ctx][sign];
  if (abs_qc > LF_NUM_BASE_LEVELS) {
    int br_ctx = get_br_ctx_lf_eob(ci, TX_CLASS_2D);
    cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[br_ctx]);
  }
  return cost;
}

static AVM_FORCE_INLINE void get_rate_dist_lf_luma_q1_neon_impl(
    const struct tcq_param_t *p, const struct tcq_coeff_ctx_t *coeff_ctx,
    int blk_pos, int diag_ctx, int eob_rate, int coeff_sign, int try_eob,
    struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const uint16_t(*cost_zero)[LF_SIG_COEF_CONTEXTS] =
      txb_costs->base_lf_cost_zero;
  const uint16_t(*cost_low_tbl)[LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_lf_cost_low_tbl;
  int dc_sign_ctx = p->dc_sign_ctx;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);

  const uint8_t *coef = coeff_ctx->coef;
  int base_ctxs[TCQ_N_STATES];
  for (int i = 0; i < TCQ_N_STATES; i++) {
    int ctx = (coef[i] & 0x0F) + base_diag_ctx;
    base_ctxs[i] = ctx;
    int qi = (i >> 1) & 1;
    rd->rate_zero[i] = (int32_t)cost_zero[qi][ctx];
  }

  const int idx = 0;
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    load_unpack_lf_base_cost(cost_low_tbl, idx, base_ctxs[4 * i],
                             base_ctxs[4 * i + 1], base_ctxs[4 * i + 2],
                             base_ctxs[4 * i + 3], (uint32_t *)&rd->rate[8 * i],
                             (uint32_t *)&rd->rate[8 * i + 4]);
  }

  const int is_dc = (blk_pos == 0);
  int dc_cost = 0;
  if (is_dc) {
    const int dc_ph_group = 0;
    dc_cost = txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][coeff_sign] -
              av2_cost_literal(1);
    int32x4_t v_dc_cost = vdupq_n_s32(dc_cost);
    for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
      int32x4_t cur_0123 = vld1q_s32(&rd->rate[8 * i]);
      int32x4_t cur_4567 = vld1q_s32(&rd->rate[8 * i + 4]);
      vst1q_s32(&rd->rate[8 * i], vaddq_s32(cur_0123, v_dc_cost));
      vst1q_s32(&rd->rate[8 * i + 4], vaddq_s32(cur_4567, v_dc_cost));
    }
  }

  if (try_eob) {
    const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
        txb_costs->base_lf_eob_cost_tbl;
    int eob_ctx = coeff_ctx->coef_eob;
    rd->rate_eob[1] =
        (int32_t)cost_eob_tbl[idx][eob_ctx][1] + eob_rate;
    if (is_dc) {
      rd->rate_eob[1] += dc_cost;
    }
  }
}

static AVM_FORCE_INLINE void get_rate_dist_lf_luma_neon_impl(
    const struct tcq_param_t *p, const struct prequant_t *pq,
    const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int diag_ctx,
    int eob_rate, int coeff_sign, int try_eob, struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const uint16_t(*cost_zero)[LF_SIG_COEF_CONTEXTS] =
      txb_costs->base_lf_cost_zero;
  const uint16_t(*cost_low_tbl)[LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_lf_cost_low_tbl;
  const uint16_t(*cost_mid_tbl)[LF_LEVEL_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->mid_lf_cost_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  int dc_sign_ctx = p->dc_sign_ctx;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);
  int mid_diag_ctx = get_mid_diag_ctx(diag_ctx);

  const uint8_t *coef = coeff_ctx->coef;
  int base_ctxs[TCQ_N_STATES];
  for (int i = 0; i < TCQ_N_STATES; i++) {
    int ctx = (coef[i] & 0x0F) + base_diag_ctx;
    base_ctxs[i] = ctx;
    int qi = (i >> 1) & 1;
    rd->rate_zero[i] = (int32_t)cost_zero[qi][ctx];
  }

  int qIdx = pq->qIdx;
  int idx = AVMMIN(qIdx - 1, 8);

  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    load_unpack_lf_base_cost(cost_low_tbl, idx, base_ctxs[4 * i],
                             base_ctxs[4 * i + 1], base_ctxs[4 * i + 2],
                             base_ctxs[4 * i + 3], (uint32_t *)&rd->rate[8 * i],
                             (uint32_t *)&rd->rate[8 * i + 4]);
  }

  int32x2_t rate_eob = vdup_n_s32(0);
  if (try_eob) {
    const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
        txb_costs->base_lf_eob_cost_tbl;
    int eob_ctx = coeff_ctx->coef_eob;
    uint32_t eob_packed;
    memcpy(&eob_packed, &cost_eob_tbl[idx][eob_ctx][0], 4);
    uint32x4_t eob_wide =
        vmovl_u16(vreinterpret_u16_u32(vcreate_u32(eob_packed)));
    rate_eob = vadd_s32(vget_low_s32(vreinterpretq_s32_u32(eob_wide)),
                        vdup_n_s32(eob_rate));
    vst1_s32(&rd->rate_eob[0], rate_eob);
  }

  const int is_dc = (blk_pos == 0);
  if (is_dc) {
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int a0 = i & 2 ? 1 : 0;
      int a1 = a0 + 2;
      int mid_cost0 = get_mid_cost_lf_dc_neon(absLevel[a0], coeff_sign, coef[i],
                                              dc_sign_ctx, txb_costs);
      int mid_cost1 = get_mid_cost_lf_dc_neon(absLevel[a1], coeff_sign, coef[i],
                                              dc_sign_ctx, txb_costs);
      rd->rate[2 * i] += mid_cost0;
      rd->rate[2 * i + 1] += mid_cost1;
    }
    if (try_eob) {
      int eob_mid_cost0 = get_mid_cost_eob_lf_dc_neon(
          blk_pos, absLevel[0], coeff_sign, dc_sign_ctx, txb_costs);
      int eob_mid_cost1 = get_mid_cost_eob_lf_dc_neon(
          blk_pos, absLevel[2], coeff_sign, dc_sign_ctx, txb_costs);
      rd->rate_eob[0] += eob_mid_cost0;
      rd->rate_eob[1] += eob_mid_cost1;
    }
  } else if (qIdx > 5) {
    int mid_idx = AVMMIN(qIdx - 1, 14);
    if (try_eob) {
      int br_ctx_eob = 7;
      uint32_t mid_eob_packed;
      memcpy(&mid_eob_packed,
             &txb_costs->mid_lf_cost_tbl[mid_idx][br_ctx_eob][0][0], 4);
      uint32x4_t mid_eob_wide =
          vmovl_u16(vreinterpret_u16_u32(vcreate_u32(mid_eob_packed)));
      rate_eob =
          vadd_s32(rate_eob, vget_low_s32(vreinterpretq_s32_u32(mid_eob_wide)));
      vst1_s32(&rd->rate_eob[0], rate_eob);
    }

    int mid_ctxs[TCQ_N_STATES];
    for (int i = 0; i < TCQ_N_STATES; i++)
      mid_ctxs[i] = (coef[i] >> 4) + mid_diag_ctx;

    for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
      uint32_t mid_0123[4], mid_4567[4];
      load_unpack_lf_mid_cost(cost_mid_tbl, mid_idx, mid_ctxs[4 * i],
                              mid_ctxs[4 * i + 1], mid_ctxs[4 * i + 2],
                              mid_ctxs[4 * i + 3], mid_0123, mid_4567);
      uint32x4_t cur_0123 = vld1q_u32((uint32_t *)&rd->rate[8 * i]);
      uint32x4_t cur_4567 = vld1q_u32((uint32_t *)&rd->rate[8 * i + 4]);
      vst1q_u32((uint32_t *)&rd->rate[8 * i],
                vaddq_u32(cur_0123, vld1q_u32(mid_0123)));
      vst1q_u32((uint32_t *)&rd->rate[8 * i + 4],
                vaddq_u32(cur_4567, vld1q_u32(mid_4567)));
    }
    if (qIdx >= 10) {
      int gol_idx = qIdx - 9;
      if (gol_idx <= 248) {
        const uint8_t *order = kGolombOrder[qIdx & 3];
        const uint8_t *bits = &kGolombExp0Bits[gol_idx];
        int32_t hr0 = (int32_t)bits[order[0]] << AV2_PROB_COST_SHIFT;
        int32_t hr1 = (int32_t)bits[order[1]] << AV2_PROB_COST_SHIFT;
        int32_t hr2 = (int32_t)bits[order[2]] << AV2_PROB_COST_SHIFT;
        int32_t hr3 = (int32_t)bits[order[3]] << AV2_PROB_COST_SHIFT;

        int32x2_t hr_lo =
            vcreate_s32((uint32_t)hr0 | ((int64_t)(uint32_t)hr1 << 32));
        int32x2_t hr_hi =
            vcreate_s32((uint32_t)hr2 | ((int64_t)(uint32_t)hr3 << 32));
        int32x4_t rate_hr_0123 = vcombine_s32(hr_lo, hr_lo);
        int32x4_t rate_hr_4567 = vcombine_s32(hr_hi, hr_hi);

        if (try_eob) {
          rate_eob = vadd_s32(rate_eob, hr_lo);
          vst1_s32(&rd->rate_eob[0], rate_eob);
        }
        for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
          int32x4_t cur_0123 = vld1q_s32(&rd->rate[8 * i]);
          int32x4_t cur_4567 = vld1q_s32(&rd->rate[8 * i + 4]);
          vst1q_s32(&rd->rate[8 * i], vaddq_s32(cur_0123, rate_hr_0123));
          vst1q_s32(&rd->rate[8 * i + 4], vaddq_s32(cur_4567, rate_hr_4567));
        }
      } else {
        int mid_cost0 = get_golomb_cost_tcq(absLevel[0], 1);
        int mid_cost1 = get_golomb_cost_tcq(absLevel[1], 1);
        int mid_cost2 = get_golomb_cost_tcq(absLevel[2], 1);
        int mid_cost3 = get_golomb_cost_tcq(absLevel[3], 1);
        for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
          rd->rate[8 * i] += mid_cost0;
          rd->rate[8 * i + 1] += mid_cost2;
          rd->rate[8 * i + 2] += mid_cost0;
          rd->rate[8 * i + 3] += mid_cost2;
          rd->rate[8 * i + 4] += mid_cost1;
          rd->rate[8 * i + 5] += mid_cost3;
          rd->rate[8 * i + 6] += mid_cost1;
          rd->rate[8 * i + 7] += mid_cost3;
        }
        if (try_eob) {
          rd->rate_eob[0] += mid_cost0;
          rd->rate_eob[1] += mid_cost2;
        }
      }
    }
  }
}

static AVM_FORCE_INLINE uint8x8_t map_state_neon(uint8x8_t prev,
                                                 uint8x8_t state) {
  uint8x8_t neg = vcge_u8(state, vdup_n_u8(0x80));
  uint8x8_t mapped = vtbl1_u8(prev, state);
  mapped = vbic_u8(mapped, neg);
  mapped = vorr_u8(mapped, neg);
  return mapped;
}

static AVM_FORCE_INLINE uint8x8_t remap_lev_neon(uint8x8_t lev_raw,
                                                 uint8x8_t state) {
  uint8x8_t neg = vcge_u8(state, vdup_n_u8(0x80));
  uint8x8_t mapped = vtbl1_u8(lev_raw, state);
  return vbic_u8(mapped, neg);
}

static AVM_FORCE_INLINE void update_nbr_diagonal_neon(struct tcq_ctx_t *tcq_ctx,
                                                      int row, int col,
                                                      int bwl) {
  int diag = row + col;
  int idx_start = col;
  int idx_end = AVMMIN(diag + 1, 1 << bwl);
  int idx0 = AVMMAX(idx_start - 2, 0);

  int max1 = diag < 5 ? 5 : 3;
  int max2 = diag < 6 ? 5 : 3;
  static const int8_t max_tbl[4] = { 0, 8, 6, 4 };
  int base_max = max_tbl[AVMMIN(diag, 3)];

  uint8x8_t orig_st = vld1_u8((const uint8_t *)tcq_ctx->orig_st);
  uint8x8_t orig_neg = vcge_u8(orig_st, vdup_n_u8(0x80));

  uint8x8_t state_id = vcreate_u8(0x0706050403020100ULL);
  vst1_u8((uint8_t *)tcq_ctx->orig_st, state_id);

  uint8x8_t st0 = state_id;
  uint8x8_t prev0 = vld1_u8((const uint8_t *)tcq_ctx->prev_st[idx0]);
  uint8x8_t prev1 = vld1_u8((const uint8_t *)tcq_ctx->prev_st[idx0 + 1]);
  uint8x8_t st1 = map_state_neon(prev0, st0);
  uint8x8_t st2 = map_state_neon(prev1, st1);

  uint8x8_t lev0 = remap_lev_neon(vld1_u8(tcq_ctx->lev_new[idx0]), st0);
  uint8x8_t lev1 = remap_lev_neon(vld1_u8(tcq_ctx->lev_new[idx0 + 1]), st1);

  uint8x8_t v_max1 = vdup_n_u8(max1);
  uint8x8_t v_max2 = vdup_n_u8(max2);
  uint8x8_t v_base_max = vdup_n_u8(base_max);
  uint8x8_t v_mid_max = vdup_n_u8(6);
  uint8x8_t v_zero = vdup_n_u8(0);

  for (int i = idx0; i < idx_end; i++) {
    uint8x8_t lev2 = remap_lev_neon(vld1_u8(tcq_ctx->lev_new[i + 2]), st2);

    uint8x8_t mag_base_raw = vld1_u8(tcq_ctx->mag_base[i]);
    uint8x8_t mag_mid_raw = vld1_u8(tcq_ctx->mag_mid[i]);
    uint8x8_t base_ctx = vtbl1_u8(mag_base_raw, orig_st);
    base_ctx = vbic_u8(base_ctx, orig_neg);
    uint8x8_t mid_ctx = vtbl1_u8(mag_mid_raw, orig_st);
    mid_ctx = vbic_u8(mid_ctx, orig_neg);

    uint8x8_t lev0_m1 = vmin_u8(lev0, v_max1);
    uint8x8_t lev1_m1 = vmin_u8(lev1, v_max1);
    uint8x8_t base_sum2 = vqadd_u8(lev0_m1, lev1_m1);
    base_ctx = vqadd_u8(base_ctx, base_sum2);
    base_ctx = vrhadd_u8(base_ctx, v_zero);
    base_ctx = vmin_u8(base_ctx, v_base_max);

    uint8x8_t mid_sum2 = vqadd_u8(lev0, lev1);
    mid_ctx = vqadd_u8(mid_ctx, mid_sum2);
    mid_ctx = vrhadd_u8(mid_ctx, v_zero);
    mid_ctx = vmin_u8(mid_ctx, v_mid_max);

    uint8x8_t mid_shifted = vshl_n_u8(mid_ctx, 4);
    uint8x8_t ctx_out = vorr_u8(base_ctx, mid_shifted);
    vst1_u8(tcq_ctx->ctx[i], ctx_out);

    uint8x8_t lev0_m2 = vmin_u8(lev0, v_max2);
    uint8x8_t lev1_m2 = vmin_u8(lev1, v_max2);
    uint8x8_t lev2_m2 = vmin_u8(lev2, v_max2);
    uint8x8_t base_sum3 = vqadd_u8(lev0_m2, lev1_m2);
    base_sum3 = vqadd_u8(base_sum3, lev2_m2);
    vst1_u8(tcq_ctx->mag_base[i], base_sum3);
    vst1_u8(tcq_ctx->mag_mid[i], lev1);

    uint8x8_t next_prev = vld1_u8((const uint8_t *)tcq_ctx->prev_st[i + 2]);
    uint8x8_t st3 = map_state_neon(next_prev, st2);
    st0 = st1;
    st1 = st2;
    st2 = st3;
    lev0 = lev1;
    lev1 = lev2;
  }
}

void av2_trellis_loop_diagonal_st8_neon(const struct tcq_param_t *p,
                                        int scan_hi, int scan_lo,
                                        struct tcq_ctx_t *tcq_ctx,
                                        struct tcq_node_t *trellis) {
  const int log_scale = p->log_scale;
  const int try_eob = p->sharpness == 0;
  const int64_t rdmult = p->rdmult;
  const int16_t *scan = p->scan;
  const tran_low_t *tcoeff = p->tcoeff;
  const int32_t *quant = p->quant;
  const int32_t *dequant = p->dequant;
  const qm_val_t *iqmatrix = p->iqmatrix;
  const uint16_t *block_eob_rate = p->block_eob_rate;
  const int bwl = p->bwl;
  const int height = p->txb_height;
  assert(p->plane == 0);
  assert(p->tx_class == TX_CLASS_2D);

  const int dc_coeff_sign = tcoeff[0] < 0;
  const int blk_pos_inc = (1 << bwl) - 1;
  const int shift = 16 - log_scale + QUANT_FP_BITS;
  int blk_pos, row, col;

  while (scan_hi >= 10) {
    blk_pos = scan[scan_hi];
    row = blk_pos >> bwl;
    col = blk_pos - (row << bwl);
    const int inc = AVMMIN(height - 1 - row, col);
    scan_lo = scan_hi - inc;
    const int lf = 0;
    const int diag_ctx = get_diag_ctx(lf, blk_pos, scan_lo, bwl);
    assert(scan_lo >= 0);

    const uint32x2_t v_rdm = vdup_n_u32((uint32_t)rdmult);
    const uint64x2_t v_rnd = vdupq_n_u64(1u << (AV2_PROB_COST_SHIFT - 1));

    for (int scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos) {
      tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
      const tcq_node_t *prev_decision = &decision[TCQ_N_STATES];
      prequant_t pq_data;
      const int temp_dqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * quant[scan_pos != 0]) >>
                       shift);

      tcq_coeff_ctx_t coeff_ctx;
      get_coeff_ctx_neon(tcq_ctx, col, &coeff_ctx);
      int eob_rate = 0;
      if (try_eob) {
        coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
        eob_rate = block_eob_rate[scan_pos];
      }
      tcq_rate_t rd;

      if (pq_data.orig_qIdx < 2) {
        pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_def_luma_q1_neon_impl(p, &coeff_ctx, diag_ctx, eob_rate,
                                            try_eob, &rd);
        decide_states_q1_neon_impl(prev_decision, &rd, &pq_data, try_eob,
                                   rdmult, v_rdm, v_rnd, decision);
      } else {
        pre_quant_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_def_luma_neon_impl(p, &pq_data, &coeff_ctx, diag_ctx,
                                         eob_rate, try_eob, &rd);
        decide_states_neon_impl(prev_decision, &rd, &pq_data, try_eob, rdmult,
                                v_rdm, v_rnd, decision);
      }
      update_states_neon(decision, col, tcq_ctx);

      blk_pos += blk_pos_inc;
      --col;
      ++row;
    }
    update_nbr_diagonal_neon(tcq_ctx, row - 1, col + 1, bwl);
    scan_hi = scan_lo - 1;
  }

  while (scan_hi >= 0) {
    blk_pos = scan[scan_hi];
    row = blk_pos >> bwl;
    col = blk_pos - (row << bwl);
    const int inc = AVMMIN(height - 1 - row, col);
    scan_lo = scan_hi - inc;
    const int lf = 1;
    const int diag_ctx = get_diag_ctx(lf, blk_pos, scan_lo, bwl);
    assert(scan_lo >= 0);

    const uint32x2_t v_rdm = vdup_n_u32((uint32_t)rdmult);
    const uint64x2_t v_rnd = vdupq_n_u64(1u << (AV2_PROB_COST_SHIFT - 1));

    for (int scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos) {
      tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
      const tcq_node_t *prev_decision = &decision[TCQ_N_STATES];
      prequant_t pq_data;
      const int temp_dqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * quant[scan_pos != 0]) >>
                       shift);

      tcq_coeff_ctx_t coeff_ctx;
      get_coeff_ctx_neon(tcq_ctx, col, &coeff_ctx);
      int eob_rate = 0;
      if (try_eob) {
        coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
        eob_rate = block_eob_rate[scan_pos];
      }
      tcq_rate_t rd;

      if (pq_data.orig_qIdx < 2) {
        pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_lf_luma_q1_neon_impl(p, &coeff_ctx, blk_pos, diag_ctx,
                                           eob_rate, dc_coeff_sign, try_eob,
                                           &rd);
        decide_states_q1_neon_impl(prev_decision, &rd, &pq_data, try_eob,
                                   rdmult, v_rdm, v_rnd, decision);
      } else {
        pre_quant_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_lf_luma_neon_impl(p, &pq_data, &coeff_ctx, blk_pos,
                                        diag_ctx, eob_rate, dc_coeff_sign,
                                        try_eob, &rd);
        decide_states_neon_impl(prev_decision, &rd, &pq_data, try_eob, rdmult,
                                v_rdm, v_rnd, decision);
      }
      update_states_neon(decision, col, tcq_ctx);

      blk_pos += blk_pos_inc;
      --col;
      ++row;
    }
    if (scan_hi != 0) update_nbr_diagonal_neon(tcq_ctx, row - 1, col + 1, bwl);
    scan_hi = scan_lo - 1;
  }
}
