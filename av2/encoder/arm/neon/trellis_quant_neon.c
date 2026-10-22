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
#include "avm_dsp/arm/transpose_neon.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/cost.h"
#include "av2/common/quant_common.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/rd.h"
#include "av2/encoder/trellis_quant.h"
#include "config/av2_rtcd.h"

// Returns 0xFF for indices >= 8.
static AVM_FORCE_INLINE uint8x8_t tbl8_fill_ff(uint8x8_t tab, uint8x8_t idx) {
#if AVM_ARCH_AARCH64
  return vqtbx1_u8(vdup_n_u8(0xFF), vcombine_u8(tab, vdup_n_u8(0xFF)), idx);
#else
  return vtbx1_u8(vdup_n_u8(0xFF), tab, idx);
#endif
}

static AVM_FORCE_INLINE void get_coeff_ctx_neon(const struct tcq_ctx_t *tcq_ctx,
                                                uint8x8_t orig, int col,
                                                struct tcq_coeff_ctx_t *cc) {
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

static AVM_FORCE_INLINE int64x2_t pre_quant_q1_neon(tran_low_t tqc,
                                                    struct prequant_t *pq,
                                                    int dqv, int log_scale) {
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

  const int64x2_t dd_sh = vshlq_n_s64(dd, RDDIV_BITS);
  vst1q_s64(&pq->deltaDist[1], dd_sh);
  return dd_sh;
}

// rd[k], info[k]: states 2k, 2k+1; info = rate | absLevel << 32 | prevId << 56.
typedef struct {
  int64x2_t rd[4];
  uint64x2_t info[4];
#if AVM_ARCH_AARCH64
  uint8x8_t pid;
#endif
} st8_nodes_t;

static AVM_FORCE_INLINE void load_nodes_neon(const tcq_node_t *prev,
                                             st8_nodes_t *n) {
  _Static_assert(sizeof(tcq_node_t) == 16, "load_nodes_neon layout");
  for (int k = 0; k < 4; k++) {
#if AVM_ARCH_AARCH64
    // One ld1x2, not two loads: clang matches a load pair as ld2 and then
    // spills the nodes as register pairs.
    const int64x2x2_t n01 = vld1q_s64_x2((const int64_t *)&prev[2 * k]);
    const int64x2_t n0 = n01.val[0];
    const int64x2_t n1 = n01.val[1];
#else
    const int64x2_t n0 = vld1q_s64((const int64_t *)&prev[2 * k]);
    const int64x2_t n1 = vld1q_s64((const int64_t *)&prev[2 * k + 1]);
#endif
    n->rd[k] = avm_vtrn1q_s64(n0, n1);
    n->info[k] = vreinterpretq_u64_s64(avm_vtrn2q_s64(n0, n1));
  }
}

static AVM_FORCE_INLINE void store_nodes_neon(const int64x2_t *w_rd,
                                              const uint64x2_t *w_info,
                                              tcq_node_t *decision,
                                              st8_nodes_t *n) {
  static const int kDstA[4] = { 0, 1, 6, 7 };
  static const int kDstB[4] = { 4, 5, 2, 3 };
  for (int p = 0; p < 4; p++) {
    const int64x2_t wi = vreinterpretq_s64_u64(w_info[p]);
    vst1q_s64((int64_t *)&decision[kDstA[p]],
              vcombine_s64(vget_low_s64(w_rd[p]), vget_low_s64(wi)));
    vst1q_s64((int64_t *)&decision[kDstB[p]],
              vcombine_s64(vget_high_s64(w_rd[p]), vget_high_s64(wi)));
  }
  n->rd[0] = vcombine_s64(vget_low_s64(w_rd[0]), vget_low_s64(w_rd[1]));
  n->rd[1] = vcombine_s64(vget_high_s64(w_rd[2]), vget_high_s64(w_rd[3]));
  n->rd[2] = vcombine_s64(vget_high_s64(w_rd[0]), vget_high_s64(w_rd[1]));
  n->rd[3] = vcombine_s64(vget_low_s64(w_rd[2]), vget_low_s64(w_rd[3]));
  n->info[0] = vcombine_u64(vget_low_u64(w_info[0]), vget_low_u64(w_info[1]));
  n->info[1] = vcombine_u64(vget_high_u64(w_info[2]), vget_high_u64(w_info[3]));
  n->info[2] = vcombine_u64(vget_high_u64(w_info[0]), vget_high_u64(w_info[1]));
  n->info[3] = vcombine_u64(vget_low_u64(w_info[2]), vget_low_u64(w_info[3]));
#if AVM_ARCH_AARCH64
  static const uint8_t kPidByte[8] = { 7, 23, 47, 63, 15, 31, 39, 55 };
  uint8x16x4_t wi;
  wi.val[0] = vreinterpretq_u8_u64(w_info[0]);
  wi.val[1] = vreinterpretq_u8_u64(w_info[1]);
  wi.val[2] = vreinterpretq_u8_u64(w_info[2]);
  wi.val[3] = vreinterpretq_u8_u64(w_info[3]);
  n->pid = vqtbl4_u8(wi, vld1_u8(kPidByte));
#endif
}

static AVM_FORCE_INLINE uint8x8_t update_states_neon(
    const st8_nodes_t *n, int col, uint8x8_t orig, struct tcq_ctx_t *tcq_ctx) {
  int32x4_t info_lo =
      vreinterpretq_s32_u32(vuzpq_u32(vreinterpretq_u32_u64(n->info[0]),
                                      vreinterpretq_u32_u64(n->info[1]))
                                .val[1]);
  int32x4_t info_hi =
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

// armv7 has no vcgtq_s64: use the sign of the saturating b - a.
static AVM_FORCE_INLINE uint64x2_t cgtq_s64_neon(int64x2_t a, int64x2_t b) {
#if AVM_ARCH_AARCH64
  return vcgtq_s64(a, b);
#else
  return vreinterpretq_u64_s64(vshrq_n_s64(vqsubq_s64(b, a), 63));
#endif
}

static AVM_FORCE_INLINE int64x2_t rdcost_neon(uint32x2_t rate, uint32x2_t rdm,
                                              int64x2_t dist) {
  return vreinterpretq_s64_u64(vrsraq_n_u64(
      vreinterpretq_u64_s64(dist), vmull_u32(rate, rdm), AV2_PROB_COST_SHIFT));
}

// Lanes of node 0 where eob is cheaper (c_e < rd) take the eob candidate.
// barrier != 0 puts the AArch64 asm barrier on the mask (q1 core).
static AVM_FORCE_INLINE void select_eob_neon(int64x2_t c_e, uint64x2_t info_e,
                                             int barrier, int64x2_t *w_rd,
                                             uint64x2_t *w_info) {
  uint64x2_t use_e = cgtq_s64_neon(w_rd[0], c_e);
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
  if (barrier) __asm__("" : "+w"(use_e));
#else
  (void)barrier;
#endif
  w_rd[0] = vreinterpretq_s64_u64(vbslq_u64(use_e, vreinterpretq_u64_s64(c_e),
                                            vreinterpretq_u64_s64(w_rd[0])));
  w_info[0] = vbslq_u64(use_e, info_e, w_info[0]);
}

// Matches av2_decide_states_c. pq->deltaDist[] is pre-scaled by RDDIV_BITS.
static AVM_FORCE_INLINE void decide_states_neon_impl(
    st8_nodes_t *nodes, const tcq_rate_t *rd, const prequant_t *pq, int try_eob,
    uint32x2_t v_rdm, tcq_node_t *decision) {
  const int32_t *rate = rd->rate;
  const int32_t *rate_zero = rd->rate_zero;
  const int64_t *ds = pq->deltaDist;
  assert(tcq_parity(pq->absLevel[0]) == 0 && tcq_parity(pq->absLevel[3]) == 0);
  assert(tcq_parity(pq->absLevel[2]) == 1 && tcq_parity(pq->absLevel[1]) == 1);

  static const uint64_t kPrevId[8] = {
    0,          1ULL << 56, 2ULL << 56, 3ULL << 56,
    4ULL << 56, 5ULL << 56, 6ULL << 56, 7ULL << 56,
  };

  const int64x2_t v_zero = vdupq_n_s64(0);
  const int64x2_t v_dist_a[2] = { vdupq_n_s64(ds[0]), vdupq_n_s64(ds[3]) };
  const int64x2_t v_dist_b[2] = { vdupq_n_s64(ds[2]), vdupq_n_s64(ds[1]) };
  const int32x4_t abs4 = vld1q_s32(pq->absLevel);
  const uint64x2_t abs01 =
      vreinterpretq_u64_s64(vshll_n_s32(vget_low_s32(abs4), 32));
  const uint64x2_t abs23 =
      vreinterpretq_u64_s64(vshll_n_s32(vget_high_s32(abs4), 32));
  const uint64x2_t v_abs_a[2] = {
    vcombine_u64(vget_low_u64(abs01), vget_low_u64(abs01)),
    vcombine_u64(vget_high_u64(abs23), vget_high_u64(abs23))
  };
  const uint64x2_t v_abs_b[2] = {
    vcombine_u64(vget_low_u64(abs23), vget_low_u64(abs23)),
    vcombine_u64(vget_high_u64(abs01), vget_high_u64(abs01))
  };

  int64x2_t w_rd[4];
  uint64x2_t w_info[4];
  for (int p = 0; p < 4; p++) {
    const int s = p * 2;
    const int q = p & 1;

    const uint64x2_t pid = vld1q_u64(&kPrevId[s]);
    const int64x2_t prev_rd = nodes->rd[p];
    const uint32x2_t prev_rate = vmovn_u64(nodes->info[p]);

    const int32x4_t r4 = vld1q_s32(&rate[2 * s]);
    const int32x2x2_t uzp = vuzp_s32(vget_low_s32(r4), vget_high_s32(r4));
    const uint32x2_t r_a = vreinterpret_u32_s32(q ? uzp.val[1] : uzp.val[0]);
    const uint32x2_t r_b = vreinterpret_u32_s32(q ? uzp.val[0] : uzp.val[1]);
    const uint32x2_t r_z = vreinterpret_u32_s32(vld1_s32(&rate_zero[s]));

    const int64x2_t c_a = rdcost_neon(r_a, v_rdm, v_dist_a[q]);
    const int64x2_t c_z = rdcost_neon(r_z, v_rdm, v_zero);
    const uint64x2_t use_z = cgtq_s64_neon(c_a, c_z);
    const int64x2_t c_az = vaddq_s64(
        vreinterpretq_s64_u64(vbslq_u64(use_z, vreinterpretq_u64_s64(c_z),
                                        vreinterpretq_u64_s64(c_a))),
        prev_rd);
    const int64x2_t c_b =
        vaddq_s64(rdcost_neon(r_b, v_rdm, v_dist_b[q]), prev_rd);
    const int64x2_t c_b_x = vextq_s64(c_b, c_b, 1);

    const uint64x2_t info_a =
        vaddw_u32(vorrq_u64(v_abs_a[q], pid), vadd_u32(r_a, prev_rate));
    const uint64x2_t info_z = vaddw_u32(pid, vadd_u32(r_z, prev_rate));
    const uint64x2_t info_b =
        vaddw_u32(vorrq_u64(v_abs_b[q], pid), vadd_u32(r_b, prev_rate));
    const uint64x2_t info_az = vbslq_u64(use_z, info_z, info_a);
    const uint64x2_t info_b_x = vextq_u64(info_b, info_b, 1);

    const uint64x2_t use_b = cgtq_s64_neon(c_az, c_b_x);
    w_rd[p] = vreinterpretq_s64_u64(vbslq_u64(
        use_b, vreinterpretq_u64_s64(c_b_x), vreinterpretq_u64_s64(c_az)));
    w_info[p] = vbslq_u64(use_b, info_b_x, info_az);
  }

  if (try_eob) {
    const uint32x2_t r_e =
        vreinterpret_u32_s32(vld1_s32((const int32_t *)rd->rate_eob));
    const int64x2_t d_e = vcombine_s64(vcreate_s64((uint64_t)ds[0]),
                                       vcreate_s64((uint64_t)ds[2]));
    const int64x2_t c_e = rdcost_neon(r_e, v_rdm, d_e);
    const uint64x2_t abs_e =
        vcombine_u64(vget_low_u64(abs01), vget_low_u64(abs23));
    const uint64x2_t info_e =
        vaddw_u32(vorrq_u64(abs_e, vdupq_n_u64(0xFFULL << 56)), r_e);
    select_eob_neon(c_e, info_e, 0, w_rd, w_info);
  }
  store_nodes_neon(w_rd, w_info, decision, nodes);
}

static AVM_FORCE_INLINE int64x2_t rdcost_q1_neon(uint32x2_t rate,
                                                 uint32x2_t rdm,
                                                 int64x2_t dist_shifted) {
  int64x2_t product = vreinterpretq_s64_u64(vmull_u32(rate, rdm));
  return vrsraq_n_s64(dist_shifted, product, AV2_PROB_COST_SHIFT);
}

// pq->deltaDist[] is pre-scaled by RDDIV_BITS.
static AVM_FORCE_INLINE void decide_states_q1_neon_core(
    st8_nodes_t *nodes, const uint32x2_t *rz_v, const uint32x2_t *ro_v,
    int32_t rate_eob1, int64x2_t dd12, int try_eob, uint32x2_t v_rdm,
    tcq_node_t *decision) {
  const int64x2_t v_dist1 =
      vcombine_s64(vget_low_s64(dd12), vget_low_s64(dd12));
  const int64x2_t v_dist2 =
      vcombine_s64(vget_high_s64(dd12), vget_high_s64(dd12));
  const int64x2_t v_zero = vdupq_n_s64(0);

  DECLARE_ALIGNED(32, static const uint64_t, kPidInfo[16]) = {
    0,
    1ULL << 56,
    (1ULL << 32),
    (1ULL << 32) | (1ULL << 56),
    (2ULL << 56),
    (3ULL << 56),
    (1ULL << 32) | (2ULL << 56),
    (1ULL << 32) | (3ULL << 56),
    (4ULL << 56),
    (5ULL << 56),
    (1ULL << 32) | (4ULL << 56),
    (1ULL << 32) | (5ULL << 56),
    (6ULL << 56),
    (7ULL << 56),
    (1ULL << 32) | (6ULL << 56),
    (1ULL << 32) | (7ULL << 56),
  };

  // vld1q_u64_x4 stays one ld1; a vld1q_u64 of a const table becomes
  // adrp + ldr at every use.
#if AVM_ARCH_AARCH64
  const uint64x2x4_t pid_info[2] = { vld1q_u64_x4(&kPidInfo[0]),
                                     vld1q_u64_x4(&kPidInfo[8]) };
#endif

  int64x2_t w_rd[4];
  uint64x2_t w_info[4];
  for (int p = 0; p < 4; p++) {
    const int64x2_t dist_v = (p & 1) ? v_dist1 : v_dist2;

#if AVM_ARCH_AARCH64
    uint64x2_t pid = pid_info[p >> 1].val[(p & 1) * 2];
    uint64x2_t abs_pid = pid_info[p >> 1].val[(p & 1) * 2 + 1];
#else
    uint64x2_t pid = vld1q_u64(&kPidInfo[p * 4]);
    uint64x2_t abs_pid = vld1q_u64(&kPidInfo[p * 4 + 2]);
#endif

    int64x2_t prev_rd = nodes->rd[p];
    int32x2_t prev_rate = vreinterpret_s32_u32(vmovn_u64(nodes->info[p]));

    uint32x2_t rz = rz_v[p];
    int64x2_t rdc_z = vaddq_s64(rdcost_q1_neon(rz, v_rdm, v_zero), prev_rd);

    uint32x2_t r_odd = ro_v[p];
    int32x2_t r_odd_s = vreinterpret_s32_u32(r_odd);
    int64x2_t rdc_o = vaddq_s64(rdcost_q1_neon(r_odd, v_rdm, dist_v), prev_rd);

    int64x2_t rdc_o_x = vextq_s64(rdc_o, rdc_o, 1);
    uint64x2_t mask = cgtq_s64_neon(rdc_z, rdc_o_x);
#if AVM_ARCH_AARCH64 && (defined(__GNUC__) || defined(__clang__))
    // Without the barrier clang matches the rd select as a min and lowers it
    // with a second, reversed compare.
    __asm__("" : "+w"(mask));
#endif

    int64x2_t win_rd = vreinterpretq_s64_u64(vbslq_u64(
        mask, vreinterpretq_u64_s64(rdc_o_x), vreinterpretq_u64_s64(rdc_z)));

    int32x2_t rz_sum = vadd_s32(vreinterpret_s32_u32(rz), prev_rate);
    int32x2_t ro_sum = vadd_s32(r_odd_s, prev_rate);

    uint64x2_t info_z = vaddw_u32(pid, vreinterpret_u32_s32(rz_sum));
    uint64x2_t info_o = vaddw_u32(abs_pid, vreinterpret_u32_s32(ro_sum));

    uint64x2_t info_o_x = vextq_u64(info_o, info_o, 1);
    w_rd[p] = win_rd;
    w_info[p] = vbslq_u64(mask, info_o_x, info_z);
  }

  if (try_eob) {
    // Lane 0 (decision[0]) never takes eob.
    const uint32x2_t r_e = vdup_n_u32((uint32_t)rate_eob1);
    const int64x2_t c_e =
        vsetq_lane_s64(INT64_MAX, rdcost_q1_neon(r_e, v_rdm, v_dist2), 0);
    const uint64x2_t info_e =
        vaddw_u32(vdupq_n_u64((1ULL << 32) | (0xFFULL << 56)), r_e);
    select_eob_neon(c_e, info_e, 1, w_rd, w_info);
  }
  store_nodes_neon(w_rd, w_info, decision, nodes);
}

static AVM_FORCE_INLINE void decide_states_q1_neon_impl(
    st8_nodes_t *nodes, const tcq_rate_t *rd, const prequant_t *pq, int try_eob,
    uint32x2_t v_rdm, tcq_node_t *decision) {
  uint32x2_t rz[4], ro[4];
  for (int p = 0; p < 4; p++) {
    rz[p] = vreinterpret_u32_s32(vld1_s32(&rd->rate_zero[2 * p]));
    const int32x4_t r4 = vld1q_s32(&rd->rate[4 * p]);
    const int32x2x2_t uzp = vuzp_s32(vget_low_s32(r4), vget_high_s32(r4));
    ro[p] = vreinterpret_u32_s32((p & 1) ? uzp.val[0] : uzp.val[1]);
  }
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
DEFINE_LOAD_UNPACK_COST(load_unpack_base_cost, SIG_COEF_CONTEXTS)
DEFINE_LOAD_UNPACK_COST(load_unpack_mid_cost, LEVEL_CONTEXTS)

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
  }
}
#endif  // !AVM_ARCH_AARCH64

#if AVM_ARCH_AARCH64
typedef struct {
  const uint8_t *z[2];
  DECLARE_ALIGNED(64, uint8_t, nz[64]);
} q1_def_rate_tbl_t;

// bdc + 15 stays inside base_cost_low_tbl; z[1] lanes c >= 10 may read past
// base_cost_zero but a valid context (c <= 8) never selects them.
static AVM_FORCE_INLINE void load_q1_def_rate_tbl(const LV_MAP_COEFF_COST *tc,
                                                  int bdc,
                                                  q1_def_rate_tbl_t *t) {
  for (int k = 0; k < TCQ_CTXS; k++) {
    t->z[k] = (const uint8_t *)&tc->base_cost_zero[k][bdc];
  }
  const uint16_t *lo = &tc->base_cost_low_tbl[0][bdc][0][0];
  const uint16x8x4_t a = vld4q_u16(lo);
  const uint16x8x4_t b = vld4q_u16(lo + 8 * TCQ_CTXS * 2);
  uint8x16x4_t nz;
  nz.val[0] = vreinterpretq_u8_u16(a.val[1]);
  nz.val[1] = vreinterpretq_u8_u16(b.val[1]);
  nz.val[2] = vreinterpretq_u8_u16(a.val[2]);
  nz.val[3] = vreinterpretq_u8_u16(b.val[2]);
  vst1q_u8_x4(t->nz, nz);
}

static AVM_FORCE_INLINE void get_rates_def_luma_q1_tbl_neon(
    const q1_def_rate_tbl_t *t, uint8x8_t ctx_row, uint8x8_t orig,
    uint32x2_t *rz, uint32x2_t *ro) {
  DECLARE_ALIGNED(16, static const uint8_t, kIdx[5][16]) = {
    { 0, 0, 0, 0, 1, 1, 1, 1, 4, 4, 4, 4, 5, 5, 5, 5 },
    { 2, 2, 2, 2, 3, 3, 3, 3, 6, 6, 6, 6, 7, 7, 7, 7 },
    { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 },
    { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7 },
    { 0, 1, 0, 1, 32, 33, 32, 33, 0, 1, 0, 1, 32, 33, 32, 33 },
  };
  const uint8x16x4_t idx = vld1q_u8_x4(kIdx[0]);
  const uint8x8_t c = vand_u8(ctx_row, vdup_n_u8(0x0F));
  const uint8x16_t c4 =
      vcombine_u8(vtbl1_u8(vshl_n_u8(c, 2), orig), vdup_n_u8(0));
  const uint8x16_t c2 =
      vcombine_u8(vtbl1_u8(vshl_n_u8(c, 1), orig), vdup_n_u8(0));
  const uint8x16_t zb = idx.val[2];
  const uint8x16_t iz0 = vorrq_u8(vqtbl1q_u8(c4, idx.val[0]), zb);
  const uint8x16_t iz1 = vorrq_u8(vqtbl1q_u8(c4, idx.val[1]), zb);
  const uint8x16_t inz =
      vorrq_u8(vqtbl1q_u8(c2, idx.val[3]), vld1q_u8(kIdx[4]));
  const uint32x4_t z0 =
      vreinterpretq_u32_u8(vqtbl4q_u8(vld1q_u8_x4(t->z[0]), iz0));
  const uint32x4_t z1 =
      vreinterpretq_u32_u8(vqtbl4q_u8(vld1q_u8_x4(t->z[1]), iz1));
  const uint16x8_t nz =
      vreinterpretq_u16_u8(vqtbl4q_u8(vld1q_u8_x4(t->nz), inz));
  const uint32x4_t nz_lo = vmovl_u16(vget_low_u16(nz));
  const uint32x4_t nz_hi = vmovl_u16(vget_high_u16(nz));
  rz[0] = vget_low_u32(z0);
  rz[1] = vget_low_u32(z1);
  rz[2] = vget_high_u32(z0);
  rz[3] = vget_high_u32(z1);
  ro[0] = vget_low_u32(nz_lo);
  ro[1] = vget_high_u32(nz_lo);
  ro[2] = vget_low_u32(nz_hi);
  ro[3] = vget_high_u32(nz_hi);
}
#endif  // AVM_ARCH_AARCH64

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
    rd->rate_eob[1] = (int32_t)cost_eob_tbl[idx][eob_ctx][1] + eob_rate;
    if (is_dc) {
      rd->rate_eob[1] += dc_cost;
    }
  } else {
    rd->rate_eob[1] = 0;
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
  return tbl8_fill_ff(prev, state);
}

static AVM_FORCE_INLINE uint8x8_t remap_lev_neon(uint8x8_t lev_raw,
                                                 uint8x8_t state) {
  return vtbl1_u8(lev_raw, state);
}

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
      const int temp_dqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * quant[scan_pos != 0]) >>
                       shift);

      tcq_coeff_ctx_t coeff_ctx;
      get_coeff_ctx_neon(tcq_ctx, orig_st, col, &coeff_ctx);
      int eob_rate = 0;
      if (try_eob) {
        coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
        eob_rate = block_eob_rate[scan_pos];
      }
      tcq_rate_t rd;

      if (pq_data.orig_qIdx < 2) {
#if AVM_ARCH_AARCH64
        const int64x2_t dd12 =
            pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        assert(blk_pos != 0 && "def region never contains DC");
        assert(pq_data.qIdx == 1);
        uint32x2_t rz[4], ro[4];
        get_rates_def_luma_q1_tbl_neon(&q1_tbl, vld1_u8(tcq_ctx->ctx[col]),
                                       orig_st, rz, ro);
        int32_t rate_eob1 = 0;
        if (try_eob) {
          const uint16_t(*eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
              p->txb_costs->base_eob_cost_tbl;
          rate_eob1 = (int32_t)eob_tbl[0][coeff_ctx.coef_eob][1] + eob_rate;
        }
        decide_states_q1_neon_core(&nodes, rz, ro, rate_eob1, dd12, try_eob,
                                   v_rdm, decision);
#else
        pre_quant_q1_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        assert(blk_pos != 0 && "def region never contains DC");
        assert(pq_data.qIdx == 1);
        get_rate_dist_def_luma_q1_neon_impl(p, &coeff_ctx, diag_ctx, eob_rate,
                                            try_eob, &rd);
        decide_states_q1_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                   decision);
#endif
      } else {
        pre_quant_neon(tcoeff[blk_pos], &pq_data, temp_dqv, log_scale);
        get_rate_dist_def_luma_neon_impl(p, &pq_data, &coeff_ctx, diag_ctx,
                                         eob_rate, try_eob, &rd);
        decide_states_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                decision);
      }
      orig_st = update_states_neon(&nodes, col, orig_st, tcq_ctx);

      blk_pos += blk_pos_inc;
      --col;
      ++row;
    }
    orig_st = update_nbr_diagonal_neon(tcq_ctx, orig_st, row - 1, col + 1, bwl);
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

    for (int scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos) {
      tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
      prequant_t pq_data;
      const int temp_dqv = get_dqv(dequant, scan[scan_pos], iqmatrix);
      pq_data.orig_qIdx =
          (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) * quant[scan_pos != 0]) >>
                       shift);

      tcq_coeff_ctx_t coeff_ctx;
      get_coeff_ctx_neon(tcq_ctx, orig_st, col, &coeff_ctx);
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
        decide_states_q1_neon_impl(&nodes, &rd, &pq_data, try_eob, v_rdm,
                                   decision);
      } else {
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
