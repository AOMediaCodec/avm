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

#include "config/avm_config.h"

#include "avm/avm_integer.h"
#include "avm_dsp/arm/mem_neon.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/cost.h"
#include "av2/common/quant_common.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/rd.h"
#include "av2/encoder/trellis_quant.h"
#include "config/av2_rtcd.h"

// Table lookup returning 0xFF for any index >= 8 (state indices here are
// 0..7 or negative).
static AVM_FORCE_INLINE uint8x8_t tbl8_fill_ff(uint8x8_t tab, uint8x8_t idx) {
#if AVM_ARCH_AARCH64
  return vqtbx1_u8(vdup_n_u8(0xFF), vcombine_u8(tab, vdup_n_u8(0xFF)), idx);
#else
  return vtbx1_u8(vdup_n_u8(0xFF), tab, idx);
#endif
}

// orig holds tcq_ctx->orig_st, carried in a register across the block.
static AVM_FORCE_INLINE void get_coeff_ctx_neon(const struct tcq_ctx_t *tcq_ctx,
                                                uint8x8_t orig, int col,
                                                struct tcq_coeff_ctx_t *cc) {
  // vtbl1 returns 0 for the negative (>= 0x80) dead-state indices.
  vst1_u8(cc->coef, vtbl1_u8(vld1_u8(tcq_ctx->ctx[col]), orig));
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

// The q1 candidates' dequantized values D = dqc << (log_scale - 1) of dqv,
// lane 0 dqca and lane 1 dqcb, as the distortion takes them: d2 = 2 * D and
// dsq = D * D. They depend on dqv alone, so the loops set them once per block
// for the AC dequant and recompute them only where dqv differs.
typedef struct {
  int32x2_t d2;
  int64x2_t dsq;
} q1_dq_t;

static AVM_FORCE_INLINE q1_dq_t q1_dq_neon(int dqv, int log_scale) {
  const int64_t dq_round = 1LL << (QUANT_TABLE_BITS - 1);
  const int shift = QUANT_TABLE_BITS + log_scale;
  const int32_t dqca = (int32_t)(((int64_t)dqv + dq_round) >> shift);
  const int32_t dqcb = (int32_t)((((int64_t)dqv << 1) + dq_round) >> shift);
  const int32x2_t d =
      vcreate_s32((uint32_t)(dqca << (log_scale - 1)) |
                  ((uint64_t)(uint32_t)(dqcb << (log_scale - 1)) << 32));
  q1_dq_t q;
  q.d2 = vadd_s32(d, d);
  q.dsq = vmull_s32(d, d);
  return q;
}

// Returns deltaDist[1..2] (also stored to pq) so the q1 decide can take them
// in a register instead of reloading them from the stack. With A the scaled
// abs_tqc, (D - A)^2 - A^2 = D * D - 2 * D * A, exact in 64 bits.
static AVM_FORCE_INLINE int64x2_t pre_quant_q1_neon(tran_low_t tqc,
                                                    struct prequant_t *pq,
                                                    const q1_dq_t *dq,
                                                    int log_scale) {
  int32_t abs_tqc = abs(tqc);
  pq->qIdx = 1;
  pq->absLevel[1] = 1;
  pq->absLevel[2] = 1;

  int32_t abs_tqc_sh = abs_tqc << (log_scale - 1);
  const int64x2_t dd = vmlsl_n_s32(dq->dsq, dq->d2, abs_tqc_sh);

  const int64x2_t dd_sh = vshlq_n_s64(dd, RDDIV_BITS);
  vst1q_s64(&pq->deltaDist[1], dd_sh);
  return dd_sh;
}

// The 8 nodes of the latest decision, carried in registers from one position
// to the next (the decision of scan_pos is the prev of scan_pos - 1), in
// adjacent output pairs: rd[j] and info[j] hold states 2j (lane 0) and 2j + 1
// (lane 1): rdCost, and rate | absLevel << 32 | prevId << 56, the second 8
// bytes of tcq_node_t, so { rd[j], info[j] } is decision[2j..2j + 1] as one
// interleaved pair. On AArch64, pid holds the prevId bytes of states 0..7,
// set by store_nodes_neon.
typedef struct {
  int64x2_t rd[4];
  uint64x2_t info[4];
#if AVM_ARCH_AARCH64
  uint8x8_t pid;
#endif
} st8_nodes_t;

// The prev states the decides read, in pairs k = (0, 2), (1, 3), (4, 6),
// (5, 7). Lane 0 of every pair has t = (s >> 1) & 1 = 0 and lane 1 has t = 1,
// so the per-t distortions and levels are the same vector for every pair.
// Output pair j (states 2j, 2j + 1) takes its zero / A candidates from pair
// kSrcZ[j] and its odd / B candidates from pair kSrcO[j], lane for lane: the
// zero / A prev state of output d is { 0, 2, 5, 7, 1, 3, 4, 6 }[d] and the
// other prev state is that one ^ 1. rd[k] holds the pairs' rdCost and rate[i]
// the rates of pairs 2i (low half) and 2i + 1 (high half).
typedef struct {
  int64x2_t rd[4];
  uint32x4_t rate[2];
} st8_prev_t;

static const int kSrcZ[4] = { 0, 3, 1, 2 };
static const int kSrcO[4] = { 1, 2, 0, 3 };

static AVM_FORCE_INLINE int64x2_t zip_lo_s64(int64x2_t a, int64x2_t b) {
  return vcombine_s64(vget_low_s64(a), vget_low_s64(b));
}

static AVM_FORCE_INLINE int64x2_t zip_hi_s64(int64x2_t a, int64x2_t b) {
  return vcombine_s64(vget_high_s64(a), vget_high_s64(b));
}

static AVM_FORCE_INLINE uint32x2_t half_u32(const uint32x4_t *q, int k) {
  return (k & 1) ? vget_high_u32(q[k >> 1]) : vget_low_u32(q[k >> 1]);
}

static AVM_FORCE_INLINE void get_prev_neon(const st8_nodes_t *n,
                                           st8_prev_t *pv) {
  pv->rd[0] = zip_lo_s64(n->rd[0], n->rd[1]);
  pv->rd[1] = zip_hi_s64(n->rd[0], n->rd[1]);
  pv->rd[2] = zip_lo_s64(n->rd[2], n->rd[3]);
  pv->rd[3] = zip_hi_s64(n->rd[2], n->rd[3]);
  // The low words of (2j, 2j + 1) and (2j + 2, 2j + 3): rates of states
  // 4i, 4i + 2, 4i + 1, 4i + 3.
  for (int i = 0; i < 2; i++) {
    pv->rate[i] = vtrnq_u32(vreinterpretq_u32_u64(n->info[2 * i]),
                            vreinterpretq_u32_u64(n->info[2 * i + 1]))
                      .val[0];
  }
}

// The 16 rates of rd->rate as the A (entry t) and B (entry 1 - t) rates of
// the prev pairs: a[i] / b[i] hold pairs 2i (low) and 2i + 1 (high).
static AVM_FORCE_INLINE void split_rates_neon(const int32_t *rate,
                                              uint32x4_t *a, uint32x4_t *b) {
  DECLARE_ALIGNED(16, static const uint32_t, kLane0[4]) = { 0xFFFFFFFF, 0,
                                                            0xFFFFFFFF, 0 };
  const uint32x4_t m = vld1q_u32(kLane0);
  for (int i = 0; i < 2; i++) {
    // lo = entries of states 4i, 4i + 1, hi = of states 4i + 2, 4i + 3.
    const uint32x4_t lo = vld1q_u32((const uint32_t *)&rate[8 * i]);
    const uint32x4_t hi = vld1q_u32((const uint32_t *)&rate[8 * i + 4]);
    a[i] = vbslq_u32(m, lo, hi);
    b[i] = vrev64q_u32(vbslq_u32(m, hi, lo));
  }
}

// rd->rate_zero in prev pair order: z[i] holds pairs 2i (low), 2i + 1 (high).
static AVM_FORCE_INLINE void split_rate_zero_neon(const int32_t *rate_zero,
                                                  uint32x4_t *z) {
  for (int i = 0; i < 2; i++) {
    const uint32x2x2_t v = vld2_u32((const uint32_t *)&rate_zero[4 * i]);
    z[i] = vcombine_u32(v.val[0], v.val[1]);
  }
}

static AVM_FORCE_INLINE void load_nodes_neon(const tcq_node_t *prev,
                                             st8_nodes_t *n) {
  _Static_assert(sizeof(tcq_node_t) == 16, "load_nodes_neon layout");
  for (int j = 0; j < 4; j++) {
    const int64x2_t n0 = vld1q_s64((const int64_t *)&prev[2 * j]);
    const int64x2_t n1 = vld1q_s64((const int64_t *)&prev[2 * j + 1]);
    n->rd[j] = zip_lo_s64(n0, n1);
    n->info[j] = vreinterpretq_u64_s64(zip_hi_s64(n0, n1));
  }
}

// Stores the decide's 4 output pairs (pair j holds decision[2j] in lane 0 and
// decision[2j + 1] in lane 1) and keeps them as the nodes of the next
// position.
static AVM_FORCE_INLINE void store_nodes_neon(const int64x2_t *w_rd,
                                              const uint64x2_t *w_info,
                                              tcq_node_t *decision,
                                              st8_nodes_t *n) {
  for (int j = 0; j < 4; j++) {
    const int64x2_t wi = vreinterpretq_s64_u64(w_info[j]);
#if AVM_ARCH_AARCH64
    int64x2x2_t v;
    v.val[0] = w_rd[j];
    v.val[1] = wi;
    vst2q_s64((int64_t *)&decision[2 * j], v);
#else
    vst1q_s64((int64_t *)&decision[2 * j], zip_lo_s64(w_rd[j], wi));
    vst1q_s64((int64_t *)&decision[2 * j + 1], zip_hi_s64(w_rd[j], wi));
#endif
    n->rd[j] = w_rd[j];
    n->info[j] = w_info[j];
  }
#if AVM_ARCH_AARCH64
  // Byte 7 of each info word is prevId, gathered in state order.
  DECLARE_ALIGNED(8, static const uint8_t, kPidByte[8]) = { 7,  15, 23, 31,
                                                            39, 47, 55, 63 };
  uint8x16x4_t wi;
  wi.val[0] = vreinterpretq_u8_u64(w_info[0]);
  wi.val[1] = vreinterpretq_u8_u64(w_info[1]);
  wi.val[2] = vreinterpretq_u8_u64(w_info[2]);
  wi.val[3] = vreinterpretq_u8_u64(w_info[3]);
  n->pid = vqtbl4_u8(wi, vld1_u8(kPidByte));
#endif
}

// Returns the remapped orig_st. n holds the nodes of the current decision.
static AVM_FORCE_INLINE uint8x8_t update_states_neon(
    const st8_nodes_t *n, int col, uint8x8_t orig, struct tcq_ctx_t *tcq_ctx) {
  // The high 32 bits of each info word: absLevel | prevId << 24, of states
  // 0..3 (info_lo) and 4..7 (info_hi).
  const int32x4_t info_lo =
      vreinterpretq_s32_u32(vuzpq_u32(vreinterpretq_u32_u64(n->info[0]),
                                      vreinterpretq_u32_u64(n->info[1]))
                                .val[1]);
  const int32x4_t info_hi =
      vreinterpretq_s32_u32(vuzpq_u32(vreinterpretq_u32_u64(n->info[2]),
                                      vreinterpretq_u32_u64(n->info[3]))
                                .val[1]);

  int32x4_t abs_lo = vandq_s32(info_lo, vdupq_n_s32(0x00FFFFFF));
  int32x4_t abs_hi = vandq_s32(info_hi, vdupq_n_s32(0x00FFFFFF));
  abs_lo = vminq_s32(abs_lo, vdupq_n_s32(MAX_VAL_BR_CTX));
  abs_hi = vminq_s32(abs_hi, vdupq_n_s32(MAX_VAL_BR_CTX));
  uint16x4_t abs_lo16 = vmovn_u32(vreinterpretq_u32_s32(abs_lo));
  uint16x4_t abs_hi16 = vmovn_u32(vreinterpretq_u32_s32(abs_hi));
  uint8x8_t abs_u8 = vmovn_u16(vcombine_u16(abs_lo16, abs_hi16));
  vst1_u8(tcq_ctx->lev_new[col], abs_u8);

#if AVM_ARCH_AARCH64
  vst1_u8((uint8_t *)tcq_ctx->prev_st[col], n->pid);
  return tbl8_fill_ff(orig, n->pid);
#else
  int32x4_t pid_lo = vshrq_n_s32(info_lo, 24);
  int32x4_t pid_hi = vshrq_n_s32(info_hi, 24);
  int16x4_t pid_lo16 = vmovn_s32(pid_lo);
  int16x4_t pid_hi16 = vmovn_s32(pid_hi);
  int8x8_t pid_s8 = vmovn_s16(vcombine_s16(pid_lo16, pid_hi16));
  vst1_s8(tcq_ctx->prev_st[col], pid_s8);

  return tbl8_fill_ff(orig, vreinterpret_u8_s8(pid_s8));
#endif
}

// Returns a > b per 64-bit lane. armv7 has no vcgtq_s64: the sign of the
// saturating difference b - a gives the same result.
static AVM_FORCE_INLINE uint64x2_t cgtq_s64_neon(int64x2_t a, int64x2_t b) {
#if AVM_ARCH_AARCH64
  return vcgtq_s64(a, b);
#else
  return vreinterpretq_u64_s64(vshrq_n_s64(vqsubq_s64(b, a), 63));
#endif
}

// rate * rdmult, rounded down by AV2_PROB_COST_SHIFT, plus dist.
static AVM_FORCE_INLINE int64x2_t rdcost_neon(uint32x2_t rate, uint32x2_t rdm,
                                              int64x2_t dist) {
  return vreinterpretq_s64_u64(vrsraq_n_u64(
      vreinterpretq_u64_s64(dist), vmull_u32(rate, rdm), AV2_PROB_COST_SHIFT));
}

// Branchless generic decide, one output pair per pass. Output pair j reads
// prev pairs kSrcZ[j] (zero / A candidates) and kSrcO[j] (B candidates),
// lane for lane: each output takes the zero/A candidate of one prev state
// (zero only if strictly cheaper than A) unless the B candidate of the other
// prev state is strictly cheaper, as in av2_decide_states_c. Lane 0 of a prev
// pair has A = rate[2s] with absLevel[0] and B = rate[2s+1] with absLevel[2],
// lane 1 A = rate[2s+1] with absLevel[3] and B = rate[2s] with absLevel[1].
// pq->deltaDist[] must already be scaled by (1 << RDDIV_BITS), as produced
// by pre_quant_neon().
// ra[i] / rb[i] / rz[i] hold the A, B and zero rates of prev pairs 2i (low
// half) and 2i + 1 (high half); r_e is rd->rate_eob[0..1] (unused unless
// try_eob).
static AVM_FORCE_INLINE void decide_states_neon_core(
    st8_nodes_t *nodes, const uint32x4_t *ra, const uint32x4_t *rb,
    const uint32x4_t *rz, uint32x2_t r_e, const prequant_t *pq, int try_eob,
    uint32x2_t v_rdm, tcq_node_t *decision) {
  const int64_t *ds = pq->deltaDist;

  // The prevIds of the prev pairs (0, 2), (1, 3), (4, 6), (5, 7).
  DECLARE_ALIGNED(16, static const uint64_t, kPairPid[8]) = {
    0,          2ULL << 56, 1ULL << 56, 3ULL << 56,
    4ULL << 56, 6ULL << 56, 5ULL << 56, 7ULL << 56,
  };

  st8_prev_t pv;
  get_prev_neon(nodes, &pv);

  const int64x2_t v_zero = vdupq_n_s64(0);
  const int64x2_t dist_a = vcombine_s64(vld1_s64(&ds[0]), vld1_s64(&ds[3]));
  const int64x2_t dist_b = vcombine_s64(vld1_s64(&ds[2]), vld1_s64(&ds[1]));
  const int32x4_t abs4 = vld1q_s32(pq->absLevel);
  const uint64x2_t abs01 =
      vreinterpretq_u64_s64(vshll_n_s32(vget_low_s32(abs4), 32));
  const uint64x2_t abs23 =
      vreinterpretq_u64_s64(vshll_n_s32(vget_high_s32(abs4), 32));
  const uint64x2_t abs_a =
      vcombine_u64(vget_low_u64(abs01), vget_high_u64(abs23));
  const uint64x2_t abs_b =
      vcombine_u64(vget_low_u64(abs23), vget_high_u64(abs01));

  uint32x4_t sa[2], sb[2], sz[2];
  for (int i = 0; i < 2; i++) {
    sa[i] = vaddq_u32(ra[i], pv.rate[i]);
    sb[i] = vaddq_u32(rb[i], pv.rate[i]);
    sz[i] = vaddq_u32(rz[i], pv.rate[i]);
  }

  int64x2_t c_az[4], c_b[4];
  uint64x2_t i_az[4], i_b[4];
  for (int k = 0; k < 4; k++) {
    const uint64x2_t pid = vld1q_u64(&kPairPid[2 * k]);
    const int64x2_t c_a = rdcost_neon(half_u32(ra, k), v_rdm, dist_a);
    const int64x2_t c_z = rdcost_neon(half_u32(rz, k), v_rdm, v_zero);
    const uint64x2_t use_z = cgtq_s64_neon(c_a, c_z);
    c_az[k] = vaddq_s64(
        vreinterpretq_s64_u64(vbslq_u64(use_z, vreinterpretq_u64_s64(c_z),
                                        vreinterpretq_u64_s64(c_a))),
        pv.rd[k]);
    c_b[k] = vaddq_s64(rdcost_neon(half_u32(rb, k), v_rdm, dist_b), pv.rd[k]);

    const uint64x2_t info_a = vaddw_u32(vorrq_u64(abs_a, pid), half_u32(sa, k));
    const uint64x2_t info_z = vaddw_u32(pid, half_u32(sz, k));
    i_az[k] = vbslq_u64(use_z, info_z, info_a);
    i_b[k] = vaddw_u32(vorrq_u64(abs_b, pid), half_u32(sb, k));
  }

  int64x2_t w_rd[4];
  uint64x2_t w_info[4];
  for (int j = 0; j < 4; j++) {
    const int z = kSrcZ[j];
    const int o = kSrcO[j];
    const uint64x2_t use_b = cgtq_s64_neon(c_az[z], c_b[o]);
    w_rd[j] = vreinterpretq_s64_u64(vbslq_u64(
        use_b, vreinterpretq_u64_s64(c_b[o]), vreinterpretq_u64_s64(c_az[z])));
    w_info[j] = vbslq_u64(use_b, i_b[o], i_az[z]);
  }

  if (try_eob) {
    // decision[0] (lane 0 of pair 0) tries eob with absLevel[0], decision[4]
    // (lane 0 of pair 2) with absLevel[2]. Lane 1 gets cost INT64_MAX so it
    // never takes eob.
    const int64x2_t d_e = vcombine_s64(vld1_s64(&ds[0]), vld1_s64(&ds[2]));
    const int64x2_t c_e = rdcost_neon(r_e, v_rdm, d_e);
    const uint64x2_t abs_e =
        vcombine_u64(vget_low_u64(abs01), vget_low_u64(abs23));
    const uint64x2_t info_e =
        vaddw_u32(vorrq_u64(abs_e, vdupq_n_u64(0xFFULL << 56)), r_e);
    const int64x2_t v_max = vdupq_n_s64(INT64_MAX);
    for (int j = 0; j < 4; j += 2) {
      const int64x2_t c_ej =
          j ? zip_hi_s64(c_e, v_max) : zip_lo_s64(c_e, v_max);
      const uint64x2_t info_ej = j ? vextq_u64(info_e, info_e, 1) : info_e;
      const uint64x2_t use_e = cgtq_s64_neon(w_rd[j], c_ej);
      w_rd[j] = vreinterpretq_s64_u64(vbslq_u64(
          use_e, vreinterpretq_u64_s64(c_ej), vreinterpretq_u64_s64(w_rd[j])));
      w_info[j] = vbslq_u64(use_e, info_ej, w_info[j]);
    }
  }
  store_nodes_neon(w_rd, w_info, decision, nodes);
}

// The generic decide on rates read from rd.
static AVM_FORCE_INLINE void decide_states_neon_impl(
    st8_nodes_t *nodes, const tcq_rate_t *rd, const prequant_t *pq, int try_eob,
    uint32x2_t v_rdm, tcq_node_t *decision) {
  uint32x4_t ra[2], rb[2], rz[2];
  split_rates_neon(rd->rate, ra, rb);
  split_rate_zero_neon(rd->rate_zero, rz);
  const uint32x2_t r_e =
      try_eob ? vreinterpret_u32_s32(vld1_s32((const int32_t *)rd->rate_eob))
              : vdup_n_u32(0);
  decide_states_neon_core(nodes, ra, rb, rz, r_e, pq, try_eob, v_rdm, decision);
}

static AVM_FORCE_INLINE int64x2_t rdcost_q1_neon(uint32x2_t rate,
                                                 uint32x2_t rdm,
                                                 int64x2_t dist_shifted) {
  int64x2_t product = vreinterpretq_s64_u64(vmull_u32(rate, rdm));
  return vrsraq_n_s64(dist_shifted, product, AV2_PROB_COST_SHIFT);
}

// Full NEON output path: rdcost compute, comparison, info packing, and store
// all stay in NEON registers. On armv7 the 64-bit comparison uses saturating
// subtract + arithmetic shift in place of vcgtq_s64. pq->deltaDist[] must
// already be scaled by (1 << RDDIV_BITS), as produced by pre_quant_q1_neon().
// rz[i] and ro[i] hold the zero and odd-candidate rates of prev pairs 2i (low
// half) and 2i + 1 (high half) (see st8_prev_t); rate_eob1 is
// rd->rate_eob[1]; dd12 holds pq->deltaDist[1..2]. The odd candidate of a
// prev pair's lane 0 (t = 0) has deltaDist[2], of lane 1 deltaDist[1].
// kPidInfo packs, per prev pair, its prevIds and, with absLevel 1 for the odd
// candidate (pq->absLevel[1..2] == 1), the same prevIds.
static AVM_FORCE_INLINE void decide_states_q1_neon_core(
    st8_nodes_t *nodes, const uint32x4_t *rz, const uint32x4_t *ro,
    int32_t rate_eob1, int64x2_t dd12, int try_eob, uint32x2_t v_rdm,
    tcq_node_t *decision) {
  const int64x2_t dist_o = vextq_s64(dd12, dd12, 1);
  const int64x2_t v_zero = vdupq_n_s64(0);

  DECLARE_ALIGNED(32, static const uint64_t, kPidInfo[16]) = {
    0,
    2ULL << 56,
    1ULL << 32,
    (1ULL << 32) | (2ULL << 56),
    1ULL << 56,
    3ULL << 56,
    (1ULL << 32) | (1ULL << 56),
    (1ULL << 32) | (3ULL << 56),
    4ULL << 56,
    6ULL << 56,
    (1ULL << 32) | (4ULL << 56),
    (1ULL << 32) | (6ULL << 56),
    5ULL << 56,
    7ULL << 56,
    (1ULL << 32) | (5ULL << 56),
    (1ULL << 32) | (7ULL << 56),
  };

  // Two 4-register loads instead of 8 constant-pool loads: clang folds a
  // vld1q_u64 of a static const table into a rematerialized adrp + ldr per
  // use inside the scan loop, but keeps a vld1q_u64_x4 as one ld1.
#if AVM_ARCH_AARCH64
  const uint64x2x4_t pid_info[2] = { vld1q_u64_x4(&kPidInfo[0]),
                                     vld1q_u64_x4(&kPidInfo[8]) };
#endif

  st8_prev_t pv;
  get_prev_neon(nodes, &pv);
  uint32x4_t sz[2], so[2];
  for (int i = 0; i < 2; i++) {
    sz[i] = vaddq_u32(rz[i], pv.rate[i]);
    so[i] = vaddq_u32(ro[i], pv.rate[i]);
  }

  int64x2_t c_z[4], c_o[4];
  uint64x2_t i_z[4], i_o[4];
  for (int k = 0; k < 4; k++) {
#if AVM_ARCH_AARCH64
    const uint64x2_t pid = pid_info[k >> 1].val[(k & 1) * 2];
    const uint64x2_t abs_pid = pid_info[k >> 1].val[(k & 1) * 2 + 1];
#else
    const uint64x2_t pid = vld1q_u64(&kPidInfo[k * 4]);
    const uint64x2_t abs_pid = vld1q_u64(&kPidInfo[k * 4 + 2]);
#endif
    c_z[k] =
        vaddq_s64(rdcost_q1_neon(half_u32(rz, k), v_rdm, v_zero), pv.rd[k]);
    c_o[k] =
        vaddq_s64(rdcost_q1_neon(half_u32(ro, k), v_rdm, dist_o), pv.rd[k]);
    i_z[k] = vaddw_u32(pid, half_u32(sz, k));
    i_o[k] = vaddw_u32(abs_pid, half_u32(so, k));
  }

  int64x2_t w_rd[4];
  uint64x2_t w_info[4];
  for (int j = 0; j < 4; j++) {
    const int z = kSrcZ[j];
    const int o = kSrcO[j];
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
    uint64x2_t mask = vcgtq_s64(c_z[z], c_o[o]);
    // Without the barrier clang matches the rd select as a min and lowers it
    // with a second, reversed compare.
    __asm__("" : "+w"(mask));
#else
    int64x2_t diff = vqsubq_s64(c_o[o], c_z[z]);
    uint64x2_t mask = vreinterpretq_u64_s64(vshrq_n_s64(diff, 63));
#endif
    w_rd[j] = vreinterpretq_s64_u64(vbslq_u64(
        mask, vreinterpretq_u64_s64(c_o[o]), vreinterpretq_u64_s64(c_z[z])));
    w_info[j] = vbslq_u64(mask, i_o[o], i_z[z]);
  }

  if (try_eob) {
    // decision[4] is lane 0 of output pair 2: rate_eob[1], absLevel[2] (== 1,
    // asserted above), prevId -1. Lane 1 (decision[5]) gets cost INT64_MAX
    // so it never takes eob.
    const uint32x2_t r_e = vdup_n_u32((uint32_t)rate_eob1);
    const int64x2_t c_e =
        vsetq_lane_s64(INT64_MAX, rdcost_q1_neon(r_e, v_rdm, dist_o), 1);
    const uint64x2_t info_e =
        vaddw_u32(vdupq_n_u64((1ULL << 32) | (0xFFULL << 56)), r_e);
    uint64x2_t use_e = cgtq_s64_neon(w_rd[2], c_e);
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
    __asm__("" : "+w"(use_e));  // One compare for both selects (see above).
#endif
    w_rd[2] = vreinterpretq_s64_u64(vbslq_u64(use_e, vreinterpretq_u64_s64(c_e),
                                              vreinterpretq_u64_s64(w_rd[2])));
    w_info[2] = vbslq_u64(use_e, info_e, w_info[2]);
  }
  store_nodes_neon(w_rd, w_info, decision, nodes);
}

// The q1 decide on rates read from rd.
static AVM_FORCE_INLINE void decide_states_q1_neon_impl(
    st8_nodes_t *nodes, const tcq_rate_t *rd, const prequant_t *pq, int try_eob,
    uint32x2_t v_rdm, tcq_node_t *decision) {
  // The odd candidate is rate entry 1 - t: the generic decide's B rate.
  uint32x4_t rz[2], ra[2], ro[2];
  split_rates_neon(rd->rate, ra, ro);
  split_rate_zero_neon(rd->rate_zero, rz);
  assert(pq->absLevel[1] == 1 && pq->absLevel[2] == 1);
  decide_states_q1_neon_core(nodes, rz, ro, rd->rate_eob[1],
                             vld1q_s64(&pq->deltaDist[1]), try_eob, v_rdm,
                             decision);
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
      const uint16_t(*tbl)[CTX_DIM][TCQ_CTXS][2], int idx, int ctx0, int ctx1, \
      int ctx2, int ctx3, uint32_t *out_0123, uint32_t *out_4567) {            \
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
#if !AVM_ARCH_AARCH64
DEFINE_LOAD_UNPACK_COST(load_unpack_base_cost, SIG_COEF_CONTEXTS)
DEFINE_LOAD_UNPACK_COST(load_unpack_mid_cost, LEVEL_CONTEXTS)
#endif

#if !AVM_ARCH_AARCH64
// ARMv7 has no vqtbl4q_u8: gather the default-region q1 rates per state.
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
    rd->rate_eob[1] = (int32_t)cost_eob_tbl[idx][eob_ctx][1] + eob_rate;
  } else {
    rd->rate_eob[1] = 0;
  }
}
#endif  // !AVM_ARCH_AARCH64

#if AVM_ARCH_AARCH64
// The default-region q1 rates of one diagonal as byte tables over the 8
// base contexts bdc + c, c = coef & 0x0F (c < 8, see below), both of 16-bit
// entries in one layout (t = 0 at bytes 2c, t = 1 at 16 + 2c), so one index
// vector in prev pair order serves both lookups: z holds
// base_cost_zero[t][bdc + c] narrowed to 16 bits, nz holds
// base_cost_low_tbl[0][bdc + c][t][1 - t]. State s uses t = (s >> 1) & 1, and
// its odd candidate is entry 1 - t of the pair (see
// decide_states_q1_neon_impl). Each position loads the tables from the
// stack, so no table stays live in registers across positions.
typedef struct {
  DECLARE_ALIGNED(32, uint8_t, z[32]);
  DECLARE_ALIGNED(32, uint8_t, nz[32]);
} q1_def_rate_tbl_t;

// The default region starts at scan position 10, where
// av2_update_nbr_diagonal_c() caps c at 4, so the reads of 8 contexts from
// bdc stay inside the rows of base_cost_zero and base_cost_low_tbl[0],
// addressed from the start of the array. base_cost_zero holds a symbol cost
// as base_cost_low_tbl does (rd.c fills both from base_cost[]), so it fits in
// 16 bits (asserted).
static AVM_FORCE_INLINE void load_q1_def_rate_tbl(const LV_MAP_COEFF_COST *tc,
                                                  int bdc,
                                                  q1_def_rate_tbl_t *t) {
  assert(bdc >= 0 && bdc + 8 <= SIG_COEF_CONTEXTS);
  uint8x16x2_t z;
  for (int k = 0; k < TCQ_CTXS; k++) {
    // val[0] is the low half of each 32-bit cost, val[1] the high half.
    const uint16x8x2_t w =
        vld2q_u16((const uint16_t *)&tc->base_cost_zero[k][bdc]);
    assert(vmaxvq_u16(w.val[1]) == 0);
    z.val[k] = vreinterpretq_u8_u16(w.val[0]);
  }
  vst1q_u8_x2(t->z, z);
  const uint16_t *lo =
      (const uint16_t *)((const uint8_t *)tc->base_cost_low_tbl +
                         bdc * sizeof(tc->base_cost_low_tbl[0][0]));
  const uint16x8x4_t a = vld4q_u16(lo);
  uint8x16x2_t nz;
  nz.val[0] = vreinterpretq_u8_u16(a.val[1]);
  nz.val[1] = vreinterpretq_u8_u16(a.val[2]);
  vst1q_u8_x2(t->nz, nz);
}

// The q1 decide's rz[] / ro[] (see decide_states_q1_neon_core) for the
// default region, gathered by table lookup from the raw ctx row of the
// position. The base-context scaling is applied to the row before the
// lookup by orig, so only the lookup sits on the orig_st dependency chain;
// vtbl1 still returns 0 for the dead-state (>= 0x80) indices.
static AVM_FORCE_INLINE void get_rates_def_luma_q1_tbl_neon(
    const q1_def_rate_tbl_t *t, uint8x8_t ctx_row, uint8x8_t orig,
    uint32x4_t *rz, uint32x4_t *ro) {
  // kIdx[0] lists the states in prev pair order 0, 2, 1, 3, 4, 6, 5, 7 (see
  // st8_prev_t), kIdx[1] the byte offsets of their t.
  DECLARE_ALIGNED(16, static const uint8_t, kIdx[2][16]) = {
    { 0, 0, 2, 2, 1, 1, 3, 3, 4, 4, 6, 6, 5, 5, 7, 7 },
    { 0, 1, 16, 17, 0, 1, 16, 17, 0, 1, 16, 17, 0, 1, 16, 17 },
  };
  const uint8x16x2_t idx = vld1q_u8_x2(kIdx[0]);
  const uint8x8_t c = vand_u8(ctx_row, vdup_n_u8(0x0F));
  assert(vmaxv_u8(vtbl1_u8(c, orig)) < 8);
  const uint8x16_t c2 =
      vcombine_u8(vtbl1_u8(vshl_n_u8(c, 1), orig), vdup_n_u8(0));
  const uint8x16_t ix = vorrq_u8(vqtbl1q_u8(c2, idx.val[0]), idx.val[1]);
  const uint16x8_t z = vreinterpretq_u16_u8(vqtbl2q_u8(vld1q_u8_x2(t->z), ix));
  const uint16x8_t nz =
      vreinterpretq_u16_u8(vqtbl2q_u8(vld1q_u8_x2(t->nz), ix));
  rz[0] = vmovl_u16(vget_low_u16(z));
  rz[1] = vmovl_high_u16(z);
  ro[0] = vmovl_u16(vget_low_u16(nz));
  ro[1] = vmovl_high_u16(nz);
}

// The LF-region q1 rates of one diagonal as byte tables over the 16 base
// contexts bdc + c, c = coef & 0x0F, both of 16-bit entries: z[t] points at
// base_lf_cost_zero[t][bdc] (c = 0..15), read in place, and nz holds
// base_lf_cost_low_tbl[0][bdc + c][t][1 - t]: bytes 0-15 t = 0 c = 0..7,
// 16-31 t = 0 c = 8..15, 32-47 t = 1 c = 0..7, 48-63 t = 1 c = 8..15. bdc is
// 0, 9 or 16, so bdc + 15 stays inside the LF_SIG_COEF_CONTEXTS entries of
// every row read.
typedef struct {
  const uint8_t *z[2];
  DECLARE_ALIGNED(64, uint8_t, nz[64]);
} q1_lf_rate_tbl_t;

static AVM_FORCE_INLINE void load_q1_lf_rate_tbl(const LV_MAP_COEFF_COST *tc,
                                                 int bdc, q1_lf_rate_tbl_t *t) {
  assert(bdc >= 0 && bdc + 16 <= LF_SIG_COEF_CONTEXTS);
  for (int k = 0; k < TCQ_CTXS; k++) {
    t->z[k] = (const uint8_t *)&tc->base_lf_cost_zero[k][bdc];
  }
  const uint16_t *lo =
      (const uint16_t *)((const uint8_t *)tc->base_lf_cost_low_tbl +
                         bdc * sizeof(tc->base_lf_cost_low_tbl[0][0]));
  const uint16x8x4_t a = vld4q_u16(lo);
  const uint16x8x4_t b = vld4q_u16(lo + 8 * TCQ_CTXS * 2);
  uint8x16x4_t nz;
  nz.val[0] = vreinterpretq_u8_u16(a.val[1]);
  nz.val[1] = vreinterpretq_u8_u16(b.val[1]);
  nz.val[2] = vreinterpretq_u8_u16(a.val[2]);
  nz.val[3] = vreinterpretq_u8_u16(b.val[2]);
  vst1q_u8_x4(t->nz, nz);
}

// The q1 decide's rz[] / ro[] for the LF region, gathered by table lookup
// from the raw ctx row of the position (see get_rates_def_luma_q1_tbl_neon).
// The zero and odd tables share one layout (t = 0 at bytes 2c, t = 1 at
// 32 + 2c), so one index vector in prev pair order serves both lookups.
// dc_cost is added to the odd rates only, as the C adds it to rate[] alone.
static AVM_FORCE_INLINE void get_rates_lf_luma_q1_tbl_neon(
    const q1_lf_rate_tbl_t *t, uint8x8_t ctx_row, uint8x8_t orig, int dc_cost,
    uint32x4_t *rz, uint32x4_t *ro) {
  DECLARE_ALIGNED(16, static const uint8_t, kIdx[2][16]) = {
    { 0, 0, 2, 2, 1, 1, 3, 3, 4, 4, 6, 6, 5, 5, 7, 7 },
    { 0, 1, 32, 33, 0, 1, 32, 33, 0, 1, 32, 33, 0, 1, 32, 33 },
  };
  const uint8x16x2_t idx = vld1q_u8_x2(kIdx[0]);
  const uint8x8_t c = vand_u8(ctx_row, vdup_n_u8(0x0F));
  const uint8x16_t c2 =
      vcombine_u8(vtbl1_u8(vshl_n_u8(c, 1), orig), vdup_n_u8(0));
  const uint8x16_t ix = vorrq_u8(vqtbl1q_u8(c2, idx.val[0]), idx.val[1]);
  const uint8x16x2_t z0 = vld1q_u8_x2(t->z[0]);
  const uint8x16x2_t z1 = vld1q_u8_x2(t->z[1]);
  uint8x16x4_t zt;
  zt.val[0] = z0.val[0];
  zt.val[1] = z0.val[1];
  zt.val[2] = z1.val[0];
  zt.val[3] = z1.val[1];
  const uint16x8_t z = vreinterpretq_u16_u8(vqtbl4q_u8(zt, ix));
  const uint16x8_t nz =
      vreinterpretq_u16_u8(vqtbl4q_u8(vld1q_u8_x4(t->nz), ix));
  const uint32x4_t v_dc = vdupq_n_u32((uint32_t)dc_cost);
  rz[0] = vmovl_u16(vget_low_u16(z));
  rz[1] = vmovl_high_u16(z);
  ro[0] = vaddw_u16(v_dc, vget_low_u16(nz));
  ro[1] = vaddw_high_u16(v_dc, nz);
}

// The generic decide's ra[] / rb[] / rz[] (see decide_states_neon_core) and
// rate_eob[0..1] for the default region, gathered in registers by table
// lookup from the raw ctx row of the position: no per-state scalar context
// extraction and no rate round trip through tcq_rate_t. The base pairs are
// base_cost_low_tbl[idx][bdc + c] (8 bytes per context, c <= 8, so bytes
// 0..71 from bdc: one tbl4 plus a tbx1 for c = 8), the mid pairs
// mid_cost_tbl[mid_idx][m] (the default-region mid diag ctx is 0 and m <= 6,
// so the 56-byte row: one tbl4). In prev pair order (lanes
// alternate t = 0, 1), A is entry [t][t] at byte c * 8 + 6t and B entry
// [t][1 - t] at byte c * 8 + 2 + 2t. Dead states (orig >= 0x80) read c = m = 0,
// as vtbl1 does for the coef bytes.
static AVM_FORCE_INLINE int32x2_t get_rates_def_luma_gen_tbl_neon(
    const q1_def_rate_tbl_t *t, const LV_MAP_COEFF_COST *tc, int bdc,
    const prequant_t *pq, uint8x8_t ctx_row, uint8x8_t orig, int try_eob,
    int eob_ctx, int eob_rate, uint32x4_t *ra, uint32x4_t *rb, uint32x4_t *rz) {
  // kIdx[0] lists the states in prev pair order (see st8_prev_t), kIdx[1]
  // the same for the m bytes 8..15 of cm8, kIdx[2] / kIdx[3] the A / B byte
  // offsets in a base or mid pair, kIdx[4] the z byte offsets of t.
  DECLARE_ALIGNED(16, static const uint8_t, kIdx[5][16]) = {
    { 0, 0, 2, 2, 1, 1, 3, 3, 4, 4, 6, 6, 5, 5, 7, 7 },
    { 8, 8, 10, 10, 9, 9, 11, 11, 12, 12, 14, 14, 13, 13, 15, 15 },
    { 0, 1, 6, 7, 0, 1, 6, 7, 0, 1, 6, 7, 0, 1, 6, 7 },
    { 2, 3, 4, 5, 2, 3, 4, 5, 2, 3, 4, 5, 2, 3, 4, 5 },
    { 0, 1, 16, 17, 0, 1, 16, 17, 0, 1, 16, 17, 0, 1, 16, 17 },
  };
  const uint8x16x4_t idx = vld1q_u8_x4(kIdx[0]);
  const uint8x16_t rep_m = idx.val[1];
  const uint8x16_t off_a = idx.val[2];
  const uint8x16_t off_b = idx.val[3];

  const uint8x8_t c = vand_u8(ctx_row, vdup_n_u8(0x0F));
  assert(vmaxv_u8(vtbl1_u8(c, orig)) < 8);
  const uint8x8_t m = vshr_n_u8(ctx_row, 4);
  // c * 8 of states 0..7 in bytes 0..7, m * 8 in bytes 8..15.
  const uint8x16_t cm8 =
      vqtbl1q_u8(vcombine_u8(vshl_n_u8(c, 3), vshl_n_u8(m, 3)),
                 vcombine_u8(orig, vorr_u8(orig, vdup_n_u8(8))));

  // c * 8 in prev pair order; c * 2 indexes the z table (see
  // q1_def_rate_tbl_t).
  const uint8x16_t rep_c = vqtbl1q_u8(cm8, idx.val[0]);
  const uint8x16_t iz = vorrq_u8(vshrq_n_u8(rep_c, 2), vld1q_u8(kIdx[4]));
  const uint16x8_t z = vreinterpretq_u16_u8(vqtbl2q_u8(vld1q_u8_x2(t->z), iz));
  rz[0] = vmovl_u16(vget_low_u16(z));
  rz[1] = vmovl_high_u16(z);

  assert(bdc + 10 <= SIG_COEF_CONTEXTS);
  const int qIdx = pq->qIdx;
  const int idx_lo = AVMMIN(qIdx - 1, 4);
  const uint8_t *bp =
      (const uint8_t *)tc->base_cost_low_tbl +
      (idx_lo * SIG_COEF_CONTEXTS + bdc) * sizeof(tc->base_cost_low_tbl[0][0]);
  const uint8x16x4_t bt = vld1q_u8_x4(bp);
  const uint8x16_t bt8 = vld1q_u8(bp + 64);
  const uint8x16_t ia = vorrq_u8(rep_c, off_a);
  const uint8x16_t ib = vorrq_u8(rep_c, off_b);
  const uint8x16_t k40 = vdupq_n_u8(0x40);
  const uint16x8_t base_a = vreinterpretq_u16_u8(
      vqtbx1q_u8(vqtbl4q_u8(bt, ia), bt8, veorq_u8(ia, k40)));
  const uint16x8_t base_b = vreinterpretq_u16_u8(
      vqtbx1q_u8(vqtbl4q_u8(bt, ib), bt8, veorq_u8(ib, k40)));

  int32x2_t rate_eob = vdup_n_s32(0);
  if (try_eob) {
    uint32_t eob_packed;
    memcpy(&eob_packed, &tc->base_eob_cost_tbl[idx_lo][eob_ctx][0], 4);
    rate_eob = vadd_s32(vreinterpret_s32_u32(vget_low_u32(vmovl_u16(
                            vreinterpret_u16_u32(vcreate_u32(eob_packed))))),
                        vdup_n_s32(eob_rate));
  }

  if (qIdx <= 1) {
    ra[0] = vmovl_u16(vget_low_u16(base_a));
    ra[1] = vmovl_high_u16(base_a);
    rb[0] = vmovl_u16(vget_low_u16(base_b));
    rb[1] = vmovl_high_u16(base_b);
    return rate_eob;
  }

  const int mid_idx = AVMMIN(qIdx - 1, 10);
  const uint8_t *mp =
      (const uint8_t *)tc->mid_cost_tbl + mid_idx * sizeof(tc->mid_cost_tbl[0]);
  _Static_assert(sizeof(tc->mid_cost_tbl[0]) == 56, "mid_cost_tbl row");
  const uint8x16x3_t m3 = vld1q_u8_x3(mp);
  uint8x16x4_t mt;
  mt.val[0] = m3.val[0];
  mt.val[1] = m3.val[1];
  mt.val[2] = m3.val[2];
  mt.val[3] = vcombine_u8(vld1_u8(mp + 48), vdup_n_u8(0));
  const uint8x16_t rep_mm = vqtbl1q_u8(cm8, rep_m);
  const uint16x8_t mid_a =
      vreinterpretq_u16_u8(vqtbl4q_u8(mt, vorrq_u8(rep_mm, off_a)));
  const uint16x8_t mid_b =
      vreinterpretq_u16_u8(vqtbl4q_u8(mt, vorrq_u8(rep_mm, off_b)));
  ra[0] = vaddl_u16(vget_low_u16(base_a), vget_low_u16(mid_a));
  ra[1] = vaddl_high_u16(base_a, mid_a);
  rb[0] = vaddl_u16(vget_low_u16(base_b), vget_low_u16(mid_b));
  rb[1] = vaddl_high_u16(base_b, mid_b);
  if (try_eob) {
    uint32_t mid_eob_packed;
    memcpy(&mid_eob_packed, &tc->mid_cost_tbl[mid_idx][0][0][0], 4);
    rate_eob = vadd_s32(
        rate_eob, vreinterpret_s32_u32(vget_low_u32(vmovl_u16(
                      vreinterpret_u16_u32(vcreate_u32(mid_eob_packed))))));
  }

  if (qIdx >= 6) {
    // g[t * 2 + e] is added to entry e of every t state's pair, and g[0..1]
    // to rate_eob[0..1].
    int32_t g[4];
    const int gol_idx = qIdx - 5;
    if (gol_idx <= 248) {
      const uint8_t *order = kGolombOrder[qIdx & 3];
      const uint8_t *bits = &kGolombExp0Bits[gol_idx];
      for (int k = 0; k < 4; k++) {
        g[k] = (int32_t)bits[order[k]] << AV2_PROB_COST_SHIFT;
      }
    } else {
      const tran_low_t *absLevel = pq->absLevel;
      g[0] = get_golomb_cost_tcq(absLevel[0], 0);
      g[1] = get_golomb_cost_tcq(absLevel[2], 0);
      g[2] = get_golomb_cost_tcq(absLevel[1], 0);
      g[3] = get_golomb_cost_tcq(absLevel[3], 0);
    }
    // A takes entry t of t (g0, g3), B entry 1 - t (g1, g2).
    const int32x2_t ga =
        vcreate_s32((uint32_t)g[0] | ((uint64_t)(uint32_t)g[3] << 32));
    const int32x2_t gb =
        vcreate_s32((uint32_t)g[1] | ((uint64_t)(uint32_t)g[2] << 32));
    const uint32x4_t ga4 = vreinterpretq_u32_s32(vcombine_s32(ga, ga));
    const uint32x4_t gb4 = vreinterpretq_u32_s32(vcombine_s32(gb, gb));
    for (int i = 0; i < 2; i++) {
      ra[i] = vaddq_u32(ra[i], ga4);
      rb[i] = vaddq_u32(rb[i], gb4);
    }
    if (try_eob) {
      rate_eob = vadd_s32(
          rate_eob,
          vcreate_s32((uint32_t)g[0] | ((uint64_t)(uint32_t)g[1] << 32)));
    }
  }
  return rate_eob;
}
#else   // !AVM_ARCH_AARCH64

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
#endif  // AVM_ARCH_AARCH64

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

#if !AVM_ARCH_AARCH64
// ARMv7 has no vqtbl4q_u8: gather the LF-region q1 rates per state.
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
    rd->rate_eob[1] = (int32_t)cost_eob_tbl[idx][eob_ctx][1] + eob_rate;
    if (is_dc) {
      rd->rate_eob[1] += dc_cost;
    }
  } else {
    rd->rate_eob[1] = 0;
  }
}
#endif  // !AVM_ARCH_AARCH64

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
  return tbl8_fill_ff(prev, state);
}

// vtbl1 returns 0 for the negative (>= 0x80) dead-state indices.
static AVM_FORCE_INLINE uint8x8_t remap_lev_neon(uint8x8_t lev_raw,
                                                 uint8x8_t state) {
  return vtbl1_u8(lev_raw, state);
}

// orig_st is the diagonal's final orig_st; returns the reset (identity)
// orig_st for the next diagonal.
static AVM_FORCE_INLINE uint8x8_t update_nbr_diagonal_neon(
    struct tcq_ctx_t *tcq_ctx, uint8x8_t orig_st, int row, int col, int bwl) {
  int diag = row + col;
  int idx_start = col;
  int idx_end = AVMMIN(diag + 1, 1 << bwl);
  int idx0 = AVMMAX(idx_start - 2, 0);

  int max1 = diag < 5 ? 5 : 3;
  int max2 = diag < 6 ? 5 : 3;
  static const int8_t max_tbl[4] = { 0, 8, 6, 4 };
  int base_max = max_tbl[AVMMIN(diag, 3)];

  const uint8x8_t state_id = vcreate_u8(0x0706050403020100ULL);

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
    uint8x8_t mid_ctx = vtbl1_u8(mag_mid_raw, orig_st);

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
  return state_id;
}

void av2_trellis_loop_diagonal_st8_neon(const struct tcq_param_t *p,
                                        int scan_hi, int scan_lo,
                                        struct tcq_ctx_t *tcq_ctx,
                                        struct tcq_node_t *trellis) {
  const int log_scale = p->log_scale;
  const int try_eob = p->sharpness == 0;
  const int64_t rdmult = p->rdmult;
  assert((rdmult >> 32) == 0);
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
  const int dqv_ac = dequant[1];
  const int32_t quant_ac = quant[1];
  const q1_dq_t q1_dq_ac = q1_dq_neon(dqv_ac, log_scale);
  // Every default-region position has scan_pos >= 10 > 0, so its
  // get_lower_levels_ctx_eob is 1 + (scan_pos > eob_t1) + (scan_pos > eob_t2),
  // and its q1 eob rate base_eob_cost_tbl[0][ctx][1] is 16-bit field ctx of
  // q1_eob_rates (field 0 unused).
  const int eob_t1 = (height << bwl) / 8;
  const int eob_t2 = (height << bwl) / 4;
#if AVM_ARCH_AARCH64
  uint64_t q1_eob_rates = 0;
  if (try_eob) {
    const uint16_t(*eob_tbl)[2] = p->txb_costs->base_eob_cost_tbl[0];
    q1_eob_rates = ((uint64_t)eob_tbl[1][1] << 16) |
                   ((uint64_t)eob_tbl[2][1] << 32) |
                   ((uint64_t)eob_tbl[3][1] << 48);
  }
#endif
  uint8x8_t orig_st = vld1_u8((const uint8_t *)tcq_ctx->orig_st);
  st8_nodes_t nodes;
  load_nodes_neon(&trellis[(scan_hi + 1) << TCQ_N_STATES_LOG], &nodes);

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
#if AVM_ARCH_AARCH64
    q1_def_rate_tbl_t q1_tbl;
    load_q1_def_rate_tbl(p->txb_costs, get_base_diag_ctx(diag_ctx), &q1_tbl);
#endif

    for (int scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos) {
      tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
      prequant_t pq_data;
      // scan_pos >= 10 here, so scan[scan_pos] != 0 (scan[0] is the DC):
      // get_dqv's dequant[!!coeff_idx] is dqv_ac and quant[scan_pos != 0] is
      // quant_ac. Without a quant matrix the position needs neither the scan
      // entry nor the dequant / quant loads.
      assert(scan[scan_pos] != 0);
      int temp_dqv = dqv_ac;
      if (iqmatrix != NULL) {
        temp_dqv =
            (iqmatrix[scan[scan_pos]] * dqv_ac + (1 << (AVM_QM_BITS - 1))) >>
            AVM_QM_BITS;
      }
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * quant_ac) >> shift);

      tcq_coeff_ctx_t coeff_ctx;
#if !AVM_ARCH_AARCH64
      get_coeff_ctx_neon(tcq_ctx, orig_st, col, &coeff_ctx);
#endif
      int eob_rate = 0;
      coeff_ctx.coef_eob = 0;
      if (try_eob) {
        coeff_ctx.coef_eob = 1 + (scan_pos > eob_t1) + (scan_pos > eob_t2);
        assert(coeff_ctx.coef_eob ==
               get_lower_levels_ctx_eob(bwl, height, scan_pos));
        eob_rate = block_eob_rate[scan_pos];
      }
#if !AVM_ARCH_AARCH64
      tcq_rate_t rd;
#endif

      if (pq_data.orig_qIdx < 2) {
        q1_dq_t q1_dq = q1_dq_ac;
        if (temp_dqv != dqv_ac) q1_dq = q1_dq_neon(temp_dqv, log_scale);
#if AVM_ARCH_AARCH64
        const int64x2_t dd12 =
            pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, &q1_dq, log_scale);
        uint32x4_t rz[2], ro[2];
        get_rates_def_luma_q1_tbl_neon(&q1_tbl, vld1_u8(tcq_ctx->ctx[col]),
                                       orig_st, rz, ro);
        int32_t rate_eob1 = 0;
        if (try_eob) {
          const int eob_sh = 16 * coeff_ctx.coef_eob;
          rate_eob1 = (int32_t)((q1_eob_rates >> eob_sh) & 0xFFFF) + eob_rate;
        }
        decide_states_q1_neon_core(&nodes, rz, ro, rate_eob1, dd12, try_eob,
                                   v_rdm, decision);
#else
        pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, &q1_dq, log_scale);
        get_rate_dist_def_luma_q1_neon_impl(p, &coeff_ctx, diag_ctx, eob_rate,
                                            try_eob, &rd);
        decide_states_q1_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                   decision);
#endif
      } else {
        pre_quant_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
#if AVM_ARCH_AARCH64
        uint32x4_t ra[2], rb[2], rz[2];
        const int32x2_t rate_eob = get_rates_def_luma_gen_tbl_neon(
            &q1_tbl, p->txb_costs, get_base_diag_ctx(diag_ctx), &pq_data,
            vld1_u8(tcq_ctx->ctx[col]), orig_st, try_eob, coeff_ctx.coef_eob,
            eob_rate, ra, rb, rz);
        decide_states_neon_core(&nodes, ra, rb, rz,
                                vreinterpret_u32_s32(rate_eob), &pq_data,
                                try_eob, v_rdm, decision);
#else
        get_rate_dist_def_luma_neon_impl(p, &pq_data, &coeff_ctx, diag_ctx,
                                         eob_rate, try_eob, &rd);
        decide_states_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                decision);
#endif
      }
      orig_st = update_states_neon(&nodes, col, orig_st, tcq_ctx);

      blk_pos += blk_pos_inc;
      --col;
      ++row;
    }
    orig_st = update_nbr_diagonal_neon(tcq_ctx, orig_st, row - 1, col + 1, bwl);
    scan_hi = scan_lo - 1;
  }

  // LF region: scan[scan_pos] == 0 only at scan_pos 0 (the DC), so get_dqv's
  // dequant[!!coeff_idx] and quant[scan_pos != 0] pick between the DC and AC
  // values by scan_pos alone, and get_lower_levels_ctx_eob is
  // (scan_pos > 0) + (scan_pos > eob_t1) + (scan_pos > eob_t2) (eob_t1 >= 2).
  // The q1 eob rate base_lf_eob_cost_tbl[0][ctx][1] is 16-bit field ctx of
  // q1_lf_eob_rates.
  const int dqv_dc = dequant[0];
  const int32_t quant_dc = quant[0];
#if AVM_ARCH_AARCH64
  uint64_t q1_lf_eob_rates = 0;
  if (try_eob) {
    const uint16_t(*lf_eob_tbl)[2] = p->txb_costs->base_lf_eob_cost_tbl[0];
    q1_lf_eob_rates =
        (uint64_t)lf_eob_tbl[0][1] | ((uint64_t)lf_eob_tbl[1][1] << 16) |
        ((uint64_t)lf_eob_tbl[2][1] << 32) | ((uint64_t)lf_eob_tbl[3][1] << 48);
  }
#endif

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
#if AVM_ARCH_AARCH64
    q1_lf_rate_tbl_t q1_lf_tbl;
    load_q1_lf_rate_tbl(p->txb_costs, get_base_diag_ctx(diag_ctx), &q1_lf_tbl);
#endif

    for (int scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos) {
      tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
      prequant_t pq_data;
      assert((scan[scan_pos] != 0) == (scan_pos != 0));
      const bool is_ac = scan_pos != 0;
      int temp_dqv = is_ac ? dqv_ac : dqv_dc;
      if (iqmatrix != NULL) {
        temp_dqv =
            (iqmatrix[scan[scan_pos]] * temp_dqv + (1 << (AVM_QM_BITS - 1))) >>
            AVM_QM_BITS;
      }
      const int32_t temp_quant = is_ac ? quant_ac : quant_dc;
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * temp_quant) >> shift);

      tcq_coeff_ctx_t coeff_ctx;
      int eob_rate = 0;
      coeff_ctx.coef_eob = 0;
      if (try_eob) {
        coeff_ctx.coef_eob = is_ac + (scan_pos > eob_t1) + (scan_pos > eob_t2);
        assert(coeff_ctx.coef_eob ==
               get_lower_levels_ctx_eob(bwl, height, scan_pos));
        eob_rate = block_eob_rate[scan_pos];
      }
      tcq_rate_t rd;

      if (pq_data.orig_qIdx < 2) {
        q1_dq_t q1_dq = q1_dq_ac;
        if (temp_dqv != dqv_ac) q1_dq = q1_dq_neon(temp_dqv, log_scale);
#if AVM_ARCH_AARCH64
        const int64x2_t dd12 =
            pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, &q1_dq, log_scale);
        const int dc_cost =
            blk_pos == 0
                ? p->txb_costs->dc_sign_cost[0][p->dc_sign_ctx][dc_coeff_sign] -
                      av2_cost_literal(1)
                : 0;
        uint32x4_t rz[2], ro[2];
        get_rates_lf_luma_q1_tbl_neon(&q1_lf_tbl, vld1_u8(tcq_ctx->ctx[col]),
                                      orig_st, dc_cost, rz, ro);
        int32_t rate_eob1 = 0;
        if (try_eob) {
          const int eob_sh = 16 * coeff_ctx.coef_eob;
          rate_eob1 = (int32_t)((q1_lf_eob_rates >> eob_sh) & 0xFFFF) +
                      eob_rate + dc_cost;
        }
        decide_states_q1_neon_core(&nodes, rz, ro, rate_eob1, dd12, try_eob,
                                   v_rdm, decision);
#else
        get_coeff_ctx_neon(tcq_ctx, orig_st, col, &coeff_ctx);
        pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, &q1_dq, log_scale);
        get_rate_dist_lf_luma_q1_neon_impl(p, &coeff_ctx, blk_pos, diag_ctx,
                                           eob_rate, dc_coeff_sign, try_eob,
                                           &rd);
        decide_states_q1_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                   decision);
#endif
      } else {
        get_coeff_ctx_neon(tcq_ctx, orig_st, col, &coeff_ctx);
        pre_quant_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_lf_luma_neon_impl(p, &pq_data, &coeff_ctx, blk_pos,
                                        diag_ctx, eob_rate, dc_coeff_sign,
                                        try_eob, &rd);
        decide_states_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                decision);
      }
      orig_st = update_states_neon(&nodes, col, orig_st, tcq_ctx);

      blk_pos += blk_pos_inc;
      --col;
      ++row;
    }
    if (scan_hi != 0) {
      orig_st =
          update_nbr_diagonal_neon(tcq_ctx, orig_st, row - 1, col + 1, bwl);
    }
    scan_hi = scan_lo - 1;
  }
  vst1_u8((uint8_t *)tcq_ctx->orig_st, orig_st);
}

void av2_decide_states_q1_neon(const struct tcq_node_t *prev,
                               const struct tcq_rate_t *rd,
                               const struct prequant_t *pq, int limits,
                               int try_eob, int64_t rdmult,
                               struct tcq_node_t *decision) {
  (void)limits;
  // The impl takes deltaDist pre-scaled by RDDIV_BITS (see pre_quant_q1_neon).
  prequant_t pq_scaled = *pq;
  pq_scaled.deltaDist[1] *= (1 << RDDIV_BITS);
  pq_scaled.deltaDist[2] *= (1 << RDDIV_BITS);
  st8_nodes_t nodes;
  load_nodes_neon(prev, &nodes);
  assert((rdmult >> 32) == 0);
  decide_states_q1_neon_impl(&nodes, rd, &pq_scaled, try_eob,
                             vdup_n_u32((uint32_t)rdmult), decision);
}
