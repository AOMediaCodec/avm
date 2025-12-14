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

#include <assert.h>
#include <immintrin.h> /* AVX2 */

#include "avm/avm_integer.h"
#include "avm_dsp/x86/mem_sse2.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/quant_common.h"
#include "av2/encoder/trellis_quant.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"

// av2_decide_states_*() constants.
static const int32_t kShuffle[8] = { 0, 2, 1, 3, 5, 7, 4, 6 };
static const int32_t kPrevId[TCQ_MAX_STATES / 4][8] = {
  { 0, 0 << 24, 0, 1 << 24, 0, 2 << 24, 0, 3 << 24 },
  { 0, 4 << 24, 0, 5 << 24, 0, 6 << 24, 0, 7 << 24 },
};

// clang-format off
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

#define Z -1
static const int8_t kGolombShuf[4][16] = {
  { 0, Z, Z, Z, 2, Z, Z, Z, 1, Z, Z, Z, 3, Z, Z, Z },
  { 3, Z, Z, Z, 1, Z, Z, Z, 0, Z, Z, Z, 2, Z, Z, Z },
  { 2, Z, Z, Z, 0, Z, Z, Z, 3, Z, Z, Z, 1, Z, Z, Z },
  { 1, Z, Z, Z, 3, Z, Z, Z, 2, Z, Z, Z, 0, Z, Z, Z }
};

static const uint8_t kConst[4][16] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
};

void av2_decide_states_avx2(const struct tcq_node_t *prev,
                            const struct tcq_rate_t *rd,
                            const struct prequant_t *pq, int limits,
                            int try_eob, int64_t rdmult,
                            struct tcq_node_t *decision) {
  (void)limits;
  assert((rdmult >> 32) == 0);
  assert(sizeof(tcq_node_t) == 16);

  __m256i c_rdmult = _mm256_set1_epi64x(rdmult);
  __m256i c_round = _mm256_set1_epi64x(1 << (AV2_PROB_COST_SHIFT - 1));
  __m256i c_zero = _mm256_setzero_si256();

  // Gather absolute coeff level for 4 possible quant options.
  __m128i abslev0123 = _mm_lddqu_si128((__m128i *)pq->absLevel);
  __m256i abslev0231 =
      _mm256_castsi128_si256(_mm_shuffle_epi32(abslev0123, 0x78));
  __m256i abslev02023131 = _mm256_permute4x64_epi64(abslev0231, 0x50);
  __m256i abslev00223311 = _mm256_shuffle_epi32(abslev02023131, 0x50);
  __m256i abslev0033 = _mm256_unpacklo_epi32(c_zero, abslev00223311);
  __m256i abslev2211 = _mm256_unpackhi_epi32(c_zero, abslev00223311);

  __m256i *out_a = (__m256i *)&decision[0];
  __m256i *out_b = (__m256i *)&decision[TCQ_N_STATES >> 1];

  for (int i = 0; i < TCQ_N_STATES >> 2; i++) {
    // Load distortion.
    __m256i dist = _mm256_lddqu_si256((__m256i *)&pq->deltaDist[0]);
    dist = _mm256_slli_epi64(dist, RDDIV_BITS);
    __m256i dist0033 = _mm256_permute4x64_epi64(dist, 0xF0);
    __m256i dist2211 = _mm256_permute4x64_epi64(dist, 0x5A);

    // Calc rate-distortion costs for each pair of even/odd quant.
    // Separate candidates into even and odd quant decisions
    // Even indexes: { 0, 2, 5, 7 }. Odd: { 1, 3, 4, 6 }.
    __m256i rates = _mm256_lddqu_si256((__m256i *)&rd->rate[8 * i]);
    __m256i permute_mask = _mm256_lddqu_si256((__m256i *)kShuffle);
    __m256i rate02135746 = _mm256_permutevar8x32_epi32(rates, permute_mask);
    __m256i rate0257 = _mm256_unpacklo_epi32(rate02135746, c_zero);
    __m256i rate1346 = _mm256_unpackhi_epi32(rate02135746, c_zero);
    __m256i rdcost0257 = _mm256_mul_epu32(c_rdmult, rate0257);
    __m256i rdcost1346 = _mm256_mul_epu32(c_rdmult, rate1346);
    rdcost0257 = _mm256_add_epi64(rdcost0257, c_round);
    rdcost1346 = _mm256_add_epi64(rdcost1346, c_round);
    rdcost0257 = _mm256_srli_epi64(rdcost0257, AV2_PROB_COST_SHIFT);
    rdcost1346 = _mm256_srli_epi64(rdcost1346, AV2_PROB_COST_SHIFT);
    rdcost0257 = _mm256_add_epi64(rdcost0257, dist0033);
    rdcost1346 = _mm256_add_epi64(rdcost1346, dist2211);

    // Calc rd-cost for zero quant.
    __m256i ratezero = _mm256_castsi128_si256(
        _mm_lddqu_si128((__m128i *)&rd->rate_zero[4 * i]));
    ratezero = _mm256_permute4x64_epi64(ratezero, 0x50);
    ratezero = _mm256_unpacklo_epi32(ratezero, c_zero);
    __m256i rdcostzero = _mm256_mul_epu32(c_rdmult, ratezero);
    rdcostzero = _mm256_add_epi64(rdcostzero, c_round);
    rdcostzero = _mm256_srli_epi64(rdcostzero, AV2_PROB_COST_SHIFT);

    // Add previous state rdCost to rdcostzero
    __m256i state01 = _mm256_lddqu_si256((__m256i *)&prev[4 * i]);
    __m256i state23 = _mm256_lddqu_si256((__m256i *)&prev[4 * i + 2]);
    __m256i state02 = _mm256_permute2x128_si256(state01, state23, 0x20);
    __m256i state13 = _mm256_permute2x128_si256(state01, state23, 0x31);
    __m256i prevrd0123 = _mm256_unpacklo_epi64(state02, state13);
    __m256i prevrate0123 = _mm256_unpackhi_epi64(state02, state13);
    prevrate0123 = _mm256_slli_epi64(prevrate0123, 32);
    prevrate0123 = _mm256_srli_epi64(prevrate0123, 32);

    // Compare rd costs (Zero vs Even).
    __m256i use_zero = _mm256_cmpgt_epi64(rdcost0257, rdcostzero);
    rdcost0257 = _mm256_blendv_epi8(rdcost0257, rdcostzero, use_zero);
    rate0257 = _mm256_blendv_epi8(rate0257, ratezero, use_zero);
    __m256i abslev_even = _mm256_andnot_si256(use_zero, abslev0033);

    // Add previous state rdCost to current rdcost
    rdcost0257 = _mm256_add_epi64(rdcost0257, prevrd0123);
    rdcost1346 = _mm256_add_epi64(rdcost1346, prevrd0123);
    rate0257 = _mm256_add_epi64(rate0257, prevrate0123);
    rate1346 = _mm256_add_epi64(rate1346, prevrate0123);

    // Compare rd costs (Even vs Odd).
    __m256i rdcost3164 = _mm256_shuffle_epi32(rdcost1346, 0x4E);
    __m256i rate3164 = _mm256_shuffle_epi32(rate1346, 0x4E);
    __m256i use_odd = _mm256_cmpgt_epi64(rdcost0257, rdcost3164);
    __m256i use_odd_1 = _mm256_slli_epi64(_mm256_srli_epi64(use_odd, 63), 56);
    __m256i prev_id = _mm256_lddqu_si256((__m256i *)kPrevId[i]);
    prev_id = _mm256_xor_si256(prev_id, use_odd_1);
    __m256i rdcost_best = _mm256_blendv_epi8(rdcost0257, rdcost3164, use_odd);
    __m256i rate_best = _mm256_blendv_epi8(rate0257, rate3164, use_odd);
    __m256i abslev_best = _mm256_blendv_epi8(abslev_even, abslev2211, use_odd);

    // Compare rd costs (best vs new eob).
    __m256i rate_eob = _mm256_castsi128_si256(_mm_loadu_si64(rd->rate_eob));
    rate_eob = _mm256_unpacklo_epi32(rate_eob, c_zero);
    __m256i rdcost_eob = _mm256_mul_epu32(c_rdmult, rate_eob);
    rdcost_eob = _mm256_add_epi64(rdcost_eob, c_round);
    rdcost_eob = _mm256_srli_epi64(rdcost_eob, AV2_PROB_COST_SHIFT);
    __m256i dist_eob = _mm256_unpacklo_epi64(dist0033, dist2211);
    rdcost_eob = _mm256_add_epi64(rdcost_eob, dist_eob);
    __m128i mask_eob0 = _mm_set1_epi64x((int64_t)-try_eob);
    __m256i mask_eob = _mm256_inserti128_si256(c_zero, mask_eob0, 0);
    __m256i use_eob = _mm256_cmpgt_epi64(rdcost_best, rdcost_eob);
    use_eob = _mm256_and_si256(use_eob, mask_eob);
    __m256i use_eob_1 = _mm256_slli_epi64(use_eob, 56);
    prev_id = _mm256_or_si256(prev_id, use_eob_1);
    rdcost_best = _mm256_blendv_epi8(rdcost_best, rdcost_eob, use_eob);
    rate_best = _mm256_blendv_epi8(rate_best, rate_eob, use_eob);
    __m256i abslev_eob = _mm256_unpacklo_epi64(abslev0033, abslev2211);
    abslev_best = _mm256_blendv_epi8(abslev_best, abslev_eob, use_eob);
    try_eob = 0;

    // Pack and store state info.
    __m256i info_best = _mm256_or_si256(rate_best, abslev_best);
    info_best = _mm256_or_si256(info_best, prev_id);
    __m256i info01 = _mm256_unpacklo_epi64(rdcost_best, info_best);
    __m256i info23 = _mm256_unpackhi_epi64(rdcost_best, info_best);
    _mm256_storeu_si256(out_a, info01);
    _mm256_storeu_si256(out_b, info23);
    out_a = (__m256i *)&decision[6];
    out_b = (__m256i *)&decision[2];
  }
}

void av2_pre_quant_avx2(tran_low_t tqc, struct prequant_t *pqData,
                        const int32_t *quant_ptr, int dqv, int log_scale,
                        int scan_pos) {
  static const int32_t kInc[4][4] = {
    { 0, 1, 2, 3 }, { 3, 0, 1, 2 }, { 2, 3, 0, 1 }, { 1, 2, 3, 0 }
  };

  // calculate qIdx
  int shift = 16 - log_scale + QUANT_FP_BITS;
  int32_t add = -((2 << shift) >> 1);
  int32_t abs_tqc = abs(tqc);

  int32_t qIdx = (int)AVMMAX(
      1, AVMMIN(((1 << 16) - 1),
                ((int64_t)abs_tqc * quant_ptr[scan_pos != 0] + add) >> shift));
  pqData->qIdx = qIdx;

  __m256i c_zero = _mm256_setzero_si256();
  __m128i base_qc = _mm_set1_epi32(qIdx);
  __m128i qc_inc = _mm_lddqu_si128((__m128i *)kInc[qIdx & 3]);
  __m128i qc_idx = _mm_add_epi32(base_qc, qc_inc);
  __m128i one = _mm_set1_epi32(1);
  __m128i abslev = _mm_add_epi32(qc_idx, one);
  abslev = _mm_srli_epi32(abslev, 1);
  _mm_storeu_si128((__m128i *)pqData->absLevel, abslev);

  __m256i qc_idx1 = _mm256_castsi128_si256(qc_idx);
  __m256i qc_idx_01012323 = _mm256_permute4x64_epi64(qc_idx1, 0x50);
  __m256i qc_idx_0123 = _mm256_unpacklo_epi32(qc_idx_01012323, c_zero);
  __m256i c_dqv = _mm256_set1_epi64x(dqv);
  __m256i qc_mul_dqv = _mm256_mul_epu32(qc_idx_0123, c_dqv);
  __m256i dq_round = _mm256_set1_epi64x(1 << (QUANT_TABLE_BITS - 1));
  __m256i qc_mul_dqv_rnd = _mm256_add_epi64(qc_mul_dqv, dq_round);
  __m256i dq_shift = _mm256_set1_epi64x(log_scale + QUANT_TABLE_BITS);
  __m256i dqc = _mm256_srlv_epi64(qc_mul_dqv_rnd, dq_shift);

  __m256i abs_tqc_sh = _mm256_set1_epi64x(abs_tqc << (log_scale - 1));
  __m256i dist0 = _mm256_mul_epi32(abs_tqc_sh, abs_tqc_sh);
  __m256i scale_shift = _mm256_set1_epi64x(log_scale - 1);
  __m256i dqc_sh = _mm256_sllv_epi32(dqc, scale_shift);
  __m256i diff = _mm256_sub_epi32(dqc_sh, abs_tqc_sh);
  __m256i dist = _mm256_mul_epi32(diff, diff);
  dist = _mm256_sub_epi64(dist, dist0);
  _mm256_storeu_si256((__m256i *)pqData->deltaDist, dist);
}

void av2_update_states_avx2(const tcq_node_t *decision, int col,
                            struct tcq_ctx_t *tcq_ctx) {
  // Extract prevId, absLevel from decision[]
  __m256i dec01 = _mm256_lddqu_si256((__m256i *)&decision[0]);
  __m256i dec23 = _mm256_lddqu_si256((__m256i *)&decision[2]);
  __m256i dec45 = _mm256_lddqu_si256((__m256i *)&decision[4]);
  __m256i dec67 = _mm256_lddqu_si256((__m256i *)&decision[6]);
  dec01 = _mm256_srli_si256(dec01, 12);
  dec23 = _mm256_srli_si256(dec23, 12);
  dec45 = _mm256_srli_si256(dec45, 12);
  dec67 = _mm256_srli_si256(dec67, 12);
  __m256i dec0213 = _mm256_unpacklo_epi32(dec01, dec23);
  __m256i dec4657 = _mm256_unpacklo_epi32(dec45, dec67);
  __m256i dec02461357 = _mm256_unpacklo_epi64(dec0213, dec4657);
  __m256i abs02461357 = _mm256_slli_epi32(dec02461357, 8);
  abs02461357 = _mm256_srli_epi32(abs02461357, 8);
  __m256i max_val_br_ctx = _mm256_set1_epi32(MAX_VAL_BR_CTX);
  abs02461357 = _mm256_min_epi32(abs02461357, max_val_br_ctx);
  __m256i abs1357 = _mm256_permute4x64_epi64(abs02461357, 0xEE);
  abs1357 = _mm256_slli_epi32(abs1357, 16);
  __m128i abs_lev =
      _mm256_castsi256_si128(_mm256_or_si256(abs02461357, abs1357));
  abs_lev = _mm_packus_epi16(abs_lev, abs_lev);
  __m256i prev02461357 = _mm256_srai_epi32(dec02461357, 24);
  __m256i prev1357 = _mm256_permute4x64_epi64(prev02461357, 0xEE);
  prev1357 = _mm256_slli_epi32(prev1357, 16);
  __m128i prev_st =
      _mm256_castsi256_si128(_mm256_blend_epi16(prev02461357, prev1357, 0xAA));
  prev_st = _mm_packs_epi16(prev_st, prev_st);
  _mm_storeu_si64(tcq_ctx->lev_new[col], abs_lev);
  _mm_storeu_si64(tcq_ctx->prev_st[col], prev_st);
  __m128i orig_st = _mm_loadu_si64(tcq_ctx->orig_st);
  orig_st = _mm_shuffle_epi8(orig_st, prev_st);
  orig_st = _mm_blendv_epi8(orig_st, prev_st, prev_st);
  _mm_storeu_si64(tcq_ctx->orig_st, orig_st);
}

void av2_get_coeff_ctx_avx2(const struct tcq_ctx_t *tcq_ctx, int col,
                            struct tcq_coeff_ctx_t *coeff_ctx) {
  __m128i orig_st = _mm_loadu_si64(tcq_ctx->orig_st);
  __m128i mag = _mm_loadu_si64(tcq_ctx->ctx[col]);
  __m128i ctx = _mm_shuffle_epi8(mag, orig_st);
  _mm_storeu_si64(coeff_ctx->coef, ctx);
}

static INLINE int get_mid_cost_def(tran_low_t abs_qc, int coeff_ctx,
                                   const LV_MAP_COEFF_COST *txb_costs,
                                   int plane, int t_sign, int sign) {
  int cost = 0;
  (void)t_sign;
  (void)sign;
  cost += av2_cost_literal(1);
  if (abs_qc > NUM_BASE_LEVELS) {
    int mid_ctx = coeff_ctx >> 4;
    if (plane == 0) {
      cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[mid_ctx]);
    } else {
      cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost_uv[mid_ctx]);
    }
  }
  return cost;
}

static INLINE int get_mid_cost_eob(int ci, int limits, int is_dc,
                                   tran_low_t abs_qc, int sign, int dc_sign_ctx,
                                   const LV_MAP_COEFF_COST *txb_costs,
                                   TX_CLASS tx_class, int32_t t_sign,
                                   int plane) {
  int cost = 0;
  const int dc_ph_group = 0;  // PH disabled

  if (limits) {
    if (is_dc) {
      cost -= av2_cost_literal(1);
      if (plane == AVM_PLANE_V) {
        cost += txb_costs->v_dc_sign_cost[t_sign][dc_sign_ctx][sign];
      } else {
        cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
      }
    } else {
      cost += av2_cost_literal(1);
    }
    if (plane > 0) {
      if (abs_qc > LF_NUM_BASE_LEVELS) {
        cost += get_br_lf_cost_tcq_uv(abs_qc);
      }
    } else {
      if (abs_qc > LF_NUM_BASE_LEVELS) {
        int br_ctx = get_br_ctx_lf_eob(ci, tx_class);
        cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[br_ctx]);
      }
    }
  } else {
    cost += av2_cost_literal(1);
    if (plane > 0) {
      if (abs_qc > NUM_BASE_LEVELS) {
        int br_ctx = 0; /* get_br_ctx_eob_chroma */
        cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost_uv[br_ctx]);
      }
    } else {
      if (abs_qc > NUM_BASE_LEVELS) {
        int br_ctx = 0; /* get_br_ctx_eob */
        cost += get_br_cost_tcq(abs_qc, txb_costs->lps_cost[br_ctx]);
      }
    }
  }
  return cost;
}

static int get_mid_cost_lf_dc(int ci, tran_low_t abs_qc, int sign,
                              int coeff_ctx, int dc_sign_ctx,
                              const LV_MAP_COEFF_COST *txb_costs,
                              const int32_t *tmp_sign, int plane) {
  int cost = 0;
  int mid_ctx = coeff_ctx >> 4;
  const int dc_ph_group = 0;    // PH disabled
  cost -= av2_cost_literal(1);  // Remove previously added sign cost.
  if (plane == AVM_PLANE_V)
    cost += txb_costs->v_dc_sign_cost[tmp_sign[ci]][dc_sign_ctx][sign];
  else
    cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
  if (plane > 0) {
    cost += get_br_lf_cost_tcq_uv(abs_qc);
  } else {
    if (abs_qc > LF_NUM_BASE_LEVELS) {
      cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
    }
  }
  return cost;
}

static int get_mid_cost_lf(tran_low_t abs_qc, int coeff_ctx,
                           const LV_MAP_COEFF_COST *txb_costs, int plane) {
  int cost = 0;
  int mid_ctx = coeff_ctx >> 4;
  assert(plane == 0);
  (void)plane;
  if (abs_qc > LF_NUM_BASE_LEVELS) {
    cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
  }
  return cost;
}

void av2_get_rate_dist_def_luma_avx2(const struct tcq_param_t *p,
                                     const struct prequant_t *pq,
                                     const struct tcq_coeff_ctx_t *coeff_ctx,
                                     int blk_pos, int diag_ctx, int eob_rate,
                                     struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  (void)blk_pos;
  const int32_t(*cost_zero)[SIG_COEF_CONTEXTS] = txb_costs->base_cost_zero;
  const uint16_t(*cost_low_tbl)[SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_cost_low_tbl;
  const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
      txb_costs->base_eob_cost_tbl;
  const uint16_t(*cost_mid_tbl)[LEVEL_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->mid_cost_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);
  int mid_diag_ctx = get_mid_diag_ctx(diag_ctx);

  // Calc zero coeff costs.
  __m256i zero = _mm256_setzero_si256();
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][base_diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][base_diag_ctx]);

  __m256i coef_ctx = _mm256_castsi128_si256(_mm_loadu_si64(&coeff_ctx->coef));
  __m256i ctx16 = _mm256_unpacklo_epi8(coef_ctx, zero);
  __m256i ctx = _mm256_shuffle_epi32(ctx16, 0xD8);
  __m256i ctx_dq0 = _mm256_unpacklo_epi16(ctx, zero);
  __m256i ctx_dq1 = _mm256_unpackhi_epi16(ctx, zero);
  __m256i ratez_dq0 = _mm256_permutevar8x32_epi32(cost_zero_dq0, ctx_dq0);
  __m256i ratez_dq1 = _mm256_permutevar8x32_epi32(cost_zero_dq1, ctx_dq1);
  __m256i ratez_0123 = _mm256_unpacklo_epi64(ratez_dq0, ratez_dq1);
  _mm_storeu_si128((__m128i *)&rd->rate_zero[0],
                   _mm256_castsi256_si128(ratez_0123));
  __m256i ratez_4567 = _mm256_unpackhi_epi64(ratez_dq0, ratez_dq1);
  _mm_storeu_si128((__m128i *)&rd->rate_zero[4],
                   _mm256_castsi256_si128(ratez_4567));

  // Calc coeff_base rate.
  int qIdx = pq->qIdx;
  int idx = AVMMIN(qIdx - 1, 4);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi16(base_diag_ctx);
  __m256i base_ctx = _mm256_slli_epi16(ctx16, 12);
  base_ctx = _mm256_srli_epi16(base_ctx, 12);
  base_ctx = _mm256_add_epi16(base_ctx, diag);
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    int ctx0 = _mm256_extract_epi16(base_ctx, 0);
    int ctx1 = _mm256_extract_epi16(base_ctx, 1);
    int ctx2 = _mm256_extract_epi16(base_ctx, 2);
    int ctx3 = _mm256_extract_epi16(base_ctx, 3);
    base_ctx = _mm256_bsrli_epi128(base_ctx, 8);
    __m128i rate_01 = _mm_loadu_si64(&cost_low_tbl[idx][ctx0][0]);
    __m128i rate_23 = _mm_loadu_si64(&cost_low_tbl[idx][ctx1][0]);
    __m128i rate_45 = _mm_loadu_si64(&cost_low_tbl[idx][ctx2][1]);
    __m128i rate_67 = _mm_loadu_si64(&cost_low_tbl[idx][ctx3][1]);
    __m128i rate_0123 = _mm_unpacklo_epi32(rate_01, rate_23);
    __m128i rate_4567 = _mm_unpacklo_epi32(rate_45, rate_67);
    rate_0123 = _mm_unpacklo_epi16(rate_0123, c_zero);
    rate_4567 = _mm_unpacklo_epi16(rate_4567, c_zero);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
  }

  // Calc coeff/eob cost.
  int eob_ctx = coeff_ctx->coef_eob;
  __m128i rate_eob_coef = _mm_loadu_si64(&cost_eob_tbl[idx][eob_ctx][0]);
  rate_eob_coef = _mm_unpacklo_epi16(rate_eob_coef, c_zero);
  __m128i rate_eob_position = _mm_set1_epi32(eob_rate);
  __m128i rate_eob = _mm_add_epi32(rate_eob_coef, rate_eob_position);
  _mm_storeu_si64(&rd->rate_eob[0], rate_eob);

  // Calc coeff mid and high range cost.
  if (qIdx > 1) {
    // Estimate mid range coef bits.
    int mid_idx = AVMMIN(qIdx - 1, 10);
    __m128i mid_rate_eob =
        _mm_loadu_si64(&txb_costs->mid_cost_tbl[mid_idx][0][0][0]);
    mid_rate_eob = _mm_unpacklo_epi16(mid_rate_eob, c_zero);
    rate_eob = _mm_add_epi32(rate_eob, mid_rate_eob);
    _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
    __m256i mid_ctx = _mm256_srli_epi16(ctx16, 4);
    __m256i mid_diag = _mm256_set1_epi16(mid_diag_ctx);
    mid_ctx = _mm256_add_epi16(mid_ctx, mid_diag);
    for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
      int ctx0 = _mm256_extract_epi16(mid_ctx, 0);
      int ctx1 = _mm256_extract_epi16(mid_ctx, 1);
      int ctx2 = _mm256_extract_epi16(mid_ctx, 2);
      int ctx3 = _mm256_extract_epi16(mid_ctx, 3);
      mid_ctx = _mm256_bsrli_epi128(mid_ctx, 8);
      __m128i mid_rate_01 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx0][0]);
      __m128i mid_rate_23 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx1][0]);
      __m128i mid_rate_45 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx2][1]);
      __m128i mid_rate_67 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx3][1]);
      __m128i mid_rate_0123 = _mm_unpacklo_epi32(mid_rate_01, mid_rate_23);
      __m128i mid_rate_4567 = _mm_unpacklo_epi32(mid_rate_45, mid_rate_67);
      mid_rate_0123 = _mm_unpacklo_epi16(mid_rate_0123, c_zero);
      mid_rate_4567 = _mm_unpacklo_epi16(mid_rate_4567, c_zero);
      __m128i rate_0123 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i]);
      __m128i rate_4567 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i + 4]);
      rate_0123 = _mm_add_epi32(rate_0123, mid_rate_0123);
      rate_4567 = _mm_add_epi32(rate_4567, mid_rate_4567);
      _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
      _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
    }
    if (qIdx >= 6) {
      if (qIdx - 5 <= 248) {
        // Add high range (golomb) bits.
        int gol_idx = qIdx - 5;
        if (gol_idx <= 248) {
          __m128i rate_hr =
              _mm_loadu_si64((__m128i *)&kGolombExp0Bits[gol_idx]);
          __m128i shuf = _mm_loadu_si128((__m128i *)&kGolombShuf[qIdx & 3]);
          rate_hr = _mm_shuffle_epi8(rate_hr, shuf);
          rate_hr = _mm_slli_epi32(rate_hr, 9);
          __m128i rate_hr_0123 = _mm_unpacklo_epi64(rate_hr, rate_hr);
          __m128i rate_hr_4567 = _mm_unpackhi_epi64(rate_hr, rate_hr);
          rate_eob = _mm_add_epi32(rate_eob, rate_hr_0123);
          _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
          for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
            __m128i rate_0123 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i]);
            __m128i rate_4567 =
                _mm_lddqu_si128((__m128i *)&rd->rate[8 * i + 4]);
            rate_0123 = _mm_add_epi32(rate_0123, rate_hr_0123);
            rate_4567 = _mm_add_epi32(rate_4567, rate_hr_4567);
            _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
            _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
          }
        }
      } else {  // qIdx - 5 > 248
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
        rd->rate_eob[0] += mid_cost0;
        rd->rate_eob[1] += mid_cost2;
      }
    }
  }
}

static __m128i map_state(__m128i state, __m128i prev_st) {
  // Track previous states.
  __m128i map_st = _mm_shuffle_epi8(state, prev_st);
  // Set state to -1 to indicate eob truncation.
  map_st = _mm_blendv_epi8(map_st, prev_st, prev_st);
  return map_st;
}

// Update neighbor coeff magnitudes state for current diagonal.
// Diagonal is indicated by its last (col, row) position.
void av2_update_nbr_diagonal_avx2(struct tcq_ctx_t *tcq_ctx, int row, int col,
                                  int bwl) {
  int diag = row + col;
  int idx_start = col;
  int idx_end = AVMMIN(diag + 1, 1 << bwl);
  __m256i zero = _mm256_setzero_si256();

  __m256i orig_st = _mm256_castsi128_si256(_mm_loadu_si64(tcq_ctx->orig_st));
  orig_st = _mm256_permute4x64_epi64(orig_st, 0);
  __m256i mask8 = _mm256_lddqu_si256((__m256i *)kConst[0]);
  orig_st = _mm256_or_si256(orig_st, mask8);

  // Update upcoming context and coeff magnitudes.
  static const int8_t max_tbl[4] = { 0, 8, 6, 4 };
  int max1 = diag < 5 ? 5 : 3;
  int max2 = diag < 6 ? 5 : 3;
  int base_max = max_tbl[AVMMIN(diag, 3)];
  int idx0 = AVMMAX(idx_start - 2, 0);
  __m128i state_id = _mm_lddqu_si128((__m128i *)kConst[2]);
  _mm_storeu_si64(&tcq_ctx->orig_st, state_id);
  __m128i state0 = state_id;
  __m128i lev01 = _mm_lddqu_si128((__m128i *)&tcq_ctx->lev_new[idx0]);
  __m128i prev_st0 = _mm_lddqu_si128((__m128i *)&tcq_ctx->prev_st[idx0]);
  __m128i prev_st1 = _mm_srli_si128(prev_st0, 8);
  __m128i state1 = map_state(prev_st0, state0);
  __m128i state2 = map_state(prev_st1, state1);
  __m128i state01 = _mm_unpacklo_epi64(state0, state1);
  state01 = _mm_or_si128(state01, _mm256_castsi256_si128(mask8));
  lev01 = _mm_shuffle_epi8(lev01, state01);
  __m256i lev__01 = _mm256_set_m128i(lev01, lev01);

  for (int i = idx0; i < idx_end; i += 4) {
    // Track state transitions.
    __m128i prev_st2 = _mm_lddqu_si128((__m128i *)&tcq_ctx->prev_st[i + 2]);
    __m128i prev_st3 = _mm_srli_si128(prev_st2, 8);
    __m128i prev_st4 = _mm_lddqu_si128((__m128i *)&tcq_ctx->prev_st[i + 4]);
    __m128i prev_st5 = _mm_srli_si128(prev_st4, 8);
    __m128i state3 = map_state(prev_st2, state2);
    __m128i state4 = map_state(prev_st3, state3);
    __m128i state5 = map_state(prev_st4, state4);
    __m128i state23 = _mm_unpacklo_epi64(state2, state3);
    __m128i state45 = _mm_unpacklo_epi64(state4, state5);
    __m256i state2345 = _mm256_set_m128i(state45, state23);
    __m256i lev2345 = _mm256_lddqu_si256((__m256i *)&tcq_ctx->lev_new[i + 2]);
    state2345 = _mm256_or_si256(state2345, mask8);
    lev2345 = _mm256_shuffle_epi8(lev2345, state2345);
    __m256i lev0123 = _mm256_permute2x128_si256(lev__01, lev2345, 0x21);
    __m256i lev1234 = _mm256_alignr_epi8(lev2345, lev0123, 8);

    // Calculate base/mid contexts for next diagonal.
    __m256i base = _mm256_lddqu_si256((__m256i *)tcq_ctx->mag_base[i]);
    __m256i mid = _mm256_lddqu_si256((__m256i *)tcq_ctx->mag_mid[i]);
    base = _mm256_shuffle_epi8(base, orig_st);
    mid = _mm256_shuffle_epi8(mid, orig_st);
    __m256i lev_max1 = _mm256_set1_epi8(max1);
    __m256i lev0123_max1 = _mm256_min_epu8(lev0123, lev_max1);
    __m256i lev1234_max1 = _mm256_min_epu8(lev1234, lev_max1);
    __m256i base_sum2 = _mm256_adds_epu8(lev0123_max1, lev1234_max1);
    base = _mm256_adds_epu8(base, base_sum2);
    base = _mm256_avg_epu8(base, zero);
    __m256i base_ctx_max = _mm256_set1_epi8(base_max);
    base = _mm256_min_epu8(base, base_ctx_max);
    __m256i mid_sum2 = _mm256_adds_epu8(lev0123, lev1234);
    mid = _mm256_adds_epu8(mid, mid_sum2);
    mid = _mm256_avg_epu8(mid, zero);
    __m256i mid_ctx_max = _mm256_set1_epi8(6);
    mid = _mm256_min_epu8(mid, mid_ctx_max);
    mid = _mm256_slli_epi16(mid, 4);
    __m256i ctx = _mm256_or_si256(base, mid);
    _mm256_storeu_si256((__m256i *)tcq_ctx->ctx[i], ctx);

    // Update base/mid range context for next-next diagonal.
    __m256i lev_max2 = _mm256_set1_epi8(max2);
    __m256i lev0123_max2 = _mm256_min_epu8(lev0123, lev_max2);
    __m256i lev1234_max2 = _mm256_min_epu8(lev1234, lev_max2);
    __m256i lev2345_max2 = _mm256_min_epu8(lev2345, lev_max2);
    __m256i base_sum3 = _mm256_adds_epu8(lev0123_max2, lev1234_max2);
    base_sum3 = _mm256_adds_epu8(base_sum3, lev2345_max2);
    _mm256_storeu_si256((__m256i *)tcq_ctx->mag_base[i], base_sum3);
    _mm256_storeu_si256((__m256i *)tcq_ctx->mag_mid[i], lev1234);

    // Prepare for next iteration.
    lev__01 = lev2345;
    state2 = map_state(prev_st5, state5);
  }
}

void av2_get_rate_dist_lf_luma_avx2(const struct tcq_param_t *p,
                                    const struct prequant_t *pq,
                                    const struct tcq_coeff_ctx_t *coeff_ctx,
                                    int blk_pos, int diag_ctx, int eob_rate,
                                    int coeff_sign, struct tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  static const int8_t kShuf[2][32] = {
    { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
      0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15 },
    { 0, 8,  Z, Z, 1, 9,  Z, Z, 2, 10, Z, Z, 3, 11, Z, Z,
      4, 12, Z, Z, 5, 13, Z, Z, 6, 14, Z, Z, 7, 15, Z, Z }
  };
  const uint16_t(*cost_zero)[LF_SIG_COEF_CONTEXTS] =
      txb_costs->base_lf_cost_zero;
  const uint16_t(*cost_low_tbl)[LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_lf_cost_low_tbl;
  const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
      txb_costs->base_lf_eob_cost_tbl;
  const uint16_t(*cost_mid_tbl)[LF_LEVEL_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->mid_lf_cost_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  const int32_t *tmp_sign = p->tmp_sign;
  int bwl = p->bwl;
  TX_CLASS tx_class = p->tx_class;
  int dc_sign_ctx = p->dc_sign_ctx;
  int plane = 0;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);
  int mid_diag_ctx = get_mid_diag_ctx(diag_ctx);

  // Calc zero coeff costs.
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][base_diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][base_diag_ctx]);
  __m256i shuf = _mm256_lddqu_si256((__m256i *)kShuf[0]);
  cost_zero_dq0 = _mm256_shuffle_epi8(cost_zero_dq0, shuf);
  cost_zero_dq1 = _mm256_shuffle_epi8(cost_zero_dq1, shuf);
  __m256i cost_dq0 = _mm256_permute4x64_epi64(cost_zero_dq0, 0xD8);
  __m256i cost_dq1 = _mm256_permute4x64_epi64(cost_zero_dq1, 0xD8);
  __m256i ctx = _mm256_castsi128_si256(_mm_loadu_si64(&coeff_ctx->coef));
  __m256i fifteen = _mm256_set1_epi8(15);
  __m256i base_ctx = _mm256_and_si256(ctx, fifteen);
  __m256i base_ctx1 = _mm256_permute4x64_epi64(base_ctx, 0);
  __m256i ratez_dq0 = _mm256_shuffle_epi8(cost_dq0, base_ctx1);
  __m256i ratez_dq1 = _mm256_shuffle_epi8(cost_dq1, base_ctx1);
  __m256i ratez = _mm256_blend_epi16(ratez_dq0, ratez_dq1, 0xAA);
  ratez = _mm256_permute4x64_epi64(ratez, 0x88);
  __m256i shuf1 = _mm256_lddqu_si256((__m256i *)kShuf[1]);
  ratez = _mm256_shuffle_epi8(ratez, shuf1);
  _mm256_storeu_si256((__m256i *)&rd->rate_zero[0], ratez);

  // Calc coeff_base rate.
  int qIdx = pq->qIdx;
  int idx = AVMMIN(qIdx - 1, 8);
  __m256i zero = _mm256_setzero_si256();
  __m128i c_zero = _mm256_castsi256_si128(zero);
  __m256i base_diag = _mm256_set1_epi8(base_diag_ctx);
  base_ctx = _mm256_add_epi8(base_ctx, base_diag);
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    int ctx0 = _mm256_extract_epi8(base_ctx, 0);
    int ctx1 = _mm256_extract_epi8(base_ctx, 1);
    int ctx2 = _mm256_extract_epi8(base_ctx, 2);
    int ctx3 = _mm256_extract_epi8(base_ctx, 3);
    base_ctx = _mm256_bsrli_epi128(base_ctx, 4);
    __m128i rate_01 = _mm_loadu_si64(&cost_low_tbl[idx][ctx0][0]);
    __m128i rate_23 = _mm_loadu_si64(&cost_low_tbl[idx][ctx1][0]);
    __m128i rate_45 = _mm_loadu_si64(&cost_low_tbl[idx][ctx2][1]);
    __m128i rate_67 = _mm_loadu_si64(&cost_low_tbl[idx][ctx3][1]);
    __m128i rate_0123 = _mm_unpacklo_epi32(rate_01, rate_23);
    __m128i rate_4567 = _mm_unpacklo_epi32(rate_45, rate_67);
    rate_0123 = _mm_unpacklo_epi16(rate_0123, c_zero);
    rate_4567 = _mm_unpacklo_epi16(rate_4567, c_zero);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
  }

  // Calc coeff/eob cost.
  int eob_ctx = coeff_ctx->coef_eob;
  __m128i rate_eob_coef = _mm_loadu_si64(&cost_eob_tbl[idx][eob_ctx][0]);
  rate_eob_coef = _mm_unpacklo_epi16(rate_eob_coef, c_zero);
  __m128i rate_eob_position = _mm_set1_epi32(eob_rate);
  __m128i rate_eob = _mm_add_epi32(rate_eob_coef, rate_eob_position);
  _mm_storeu_si64(&rd->rate_eob[0], rate_eob);

  const int row = blk_pos >> bwl;
  const int col = blk_pos - (row << bwl);
  const bool dc_2dtx = (blk_pos == 0);
  const bool dc_hor = (col == 0) && tx_class == TX_CLASS_HORIZ;
  const bool dc_ver = (row == 0) && tx_class == TX_CLASS_VERT;
  const bool is_dc_coeff = dc_2dtx || dc_hor || dc_ver;
  if (is_dc_coeff) {
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int a0 = i & 2 ? 1 : 0;
      int a1 = a0 + 2;
      int mid_cost0 = get_mid_cost_lf_dc(blk_pos, absLevel[a0], coeff_sign,
                                         coeff_ctx->coef[i], dc_sign_ctx,
                                         txb_costs, tmp_sign, plane);
      int mid_cost1 = get_mid_cost_lf_dc(blk_pos, absLevel[a1], coeff_sign,
                                         coeff_ctx->coef[i], dc_sign_ctx,
                                         txb_costs, tmp_sign, plane);
      rd->rate[2 * i] += mid_cost0;
      rd->rate[2 * i + 1] += mid_cost1;
    }
    int t_sign = tmp_sign[blk_pos];
    int eob_mid_cost0 =
        get_mid_cost_eob(blk_pos, 1, 1, absLevel[0], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, 0);
    int eob_mid_cost1 =
        get_mid_cost_eob(blk_pos, 1, 1, absLevel[2], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, 0);
    rd->rate_eob[0] += eob_mid_cost0;
    rd->rate_eob[1] += eob_mid_cost1;
  } else if (qIdx > 5) {
    // Estimate mid range coef bits.
    int mid_idx = AVMMIN(qIdx - 1, 14);
    int br_ctx_eob = 7;
    __m128i mid_rate_eob =
        _mm_loadu_si64(&txb_costs->mid_lf_cost_tbl[mid_idx][br_ctx_eob][0][0]);
    mid_rate_eob = _mm_unpacklo_epi16(mid_rate_eob, c_zero);
    rate_eob = _mm_add_epi32(rate_eob, mid_rate_eob);
    _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
    __m256i mid_ctx = _mm256_unpacklo_epi8(ctx, zero);
    mid_ctx = _mm256_srli_epi16(mid_ctx, 4);
    __m256i mid_diag = _mm256_set1_epi16(mid_diag_ctx);
    mid_ctx = _mm256_add_epi16(mid_ctx, mid_diag);
    for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
      int ctx0 = _mm256_extract_epi16(mid_ctx, 0);
      int ctx1 = _mm256_extract_epi16(mid_ctx, 1);
      int ctx2 = _mm256_extract_epi16(mid_ctx, 2);
      int ctx3 = _mm256_extract_epi16(mid_ctx, 3);
      mid_ctx = _mm256_bsrli_epi128(mid_ctx, 8);
      __m128i mid_rate_01 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx0][0]);
      __m128i mid_rate_23 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx1][0]);
      __m128i mid_rate_45 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx2][1]);
      __m128i mid_rate_67 = _mm_loadu_si64(&cost_mid_tbl[mid_idx][ctx3][1]);
      __m128i mid_rate_0123 = _mm_unpacklo_epi32(mid_rate_01, mid_rate_23);
      __m128i mid_rate_4567 = _mm_unpacklo_epi32(mid_rate_45, mid_rate_67);
      mid_rate_0123 = _mm_unpacklo_epi16(mid_rate_0123, c_zero);
      mid_rate_4567 = _mm_unpacklo_epi16(mid_rate_4567, c_zero);
      __m128i rate_0123 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i]);
      __m128i rate_4567 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i + 4]);
      rate_0123 = _mm_add_epi32(rate_0123, mid_rate_0123);
      rate_4567 = _mm_add_epi32(rate_4567, mid_rate_4567);
      _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
      _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
    }
    if (qIdx >= 10) {
      // Add high range (golomb) bits.
      int gol_idx = qIdx - 9;
      if (gol_idx <= 248) {
        __m128i rate_hr = _mm_loadu_si64((__m128i *)&kGolombExp0Bits[gol_idx]);
        __m128i shufg = _mm_loadu_si128((__m128i *)&kGolombShuf[qIdx & 3]);
        rate_hr = _mm_shuffle_epi8(rate_hr, shufg);
        rate_hr = _mm_slli_epi32(rate_hr, 9);
        __m128i rate_hr_0123 = _mm_unpacklo_epi64(rate_hr, rate_hr);
        __m128i rate_hr_4567 = _mm_unpackhi_epi64(rate_hr, rate_hr);
        rate_eob = _mm_add_epi32(rate_eob, rate_hr_0123);
        _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
        for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
          __m128i rate_0123 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i]);
          __m128i rate_4567 = _mm_lddqu_si128((__m128i *)&rd->rate[8 * i + 4]);
          rate_0123 = _mm_add_epi32(rate_0123, rate_hr_0123);
          rate_4567 = _mm_add_epi32(rate_4567, rate_hr_4567);
          _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
          _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
        }
      } else {  // qIdx - 9 > 248
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
        rd->rate_eob[0] += mid_cost0;
        rd->rate_eob[1] += mid_cost2;
      }
    }
  }
}

void av2_get_rate_dist_lf_chroma_avx2(const struct LV_MAP_COEFF_COST *txb_costs,
                                      const struct prequant_t *pq,
                                      const struct tcq_coeff_ctx_t *coeff_ctx,
                                      int blk_pos, int diag_ctx, int eob_rate,
                                      int dc_sign_ctx, const int32_t *tmp_sign,
                                      int bwl, TX_CLASS tx_class, int plane,
                                      int coeff_sign, struct tcq_rate_t *rd) {
  (void)bwl;
#define Z -1
  static const int8_t kShuf[2][32] = {
    { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
      0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15 },
    { 0, 8,  Z, Z, 1, 9,  Z, Z, 2, 10, Z, Z, 3, 11, Z, Z,
      4, 12, Z, Z, 5, 13, Z, Z, 6, 14, Z, Z, 7, 15, Z, Z }
  };
  const uint16_t(*cost_zero)[LF_SIG_COEF_CONTEXTS] =
      plane ? txb_costs->base_lf_cost_uv_zero : txb_costs->base_lf_cost_zero;
  const uint16_t(*cost_low_tbl)[LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      plane ? txb_costs->base_lf_cost_uv_low_tbl
            : txb_costs->base_lf_cost_low_tbl;
  const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
      txb_costs->base_lf_eob_cost_uv_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);

  // Calc zero coeff costs.
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][base_diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][base_diag_ctx]);
  __m256i shuf = _mm256_lddqu_si256((__m256i *)kShuf[0]);
  cost_zero_dq0 = _mm256_shuffle_epi8(cost_zero_dq0, shuf);
  cost_zero_dq1 = _mm256_shuffle_epi8(cost_zero_dq1, shuf);
  __m256i cost_dq0 = _mm256_permute4x64_epi64(cost_zero_dq0, 0xD8);
  __m256i cost_dq1 = _mm256_permute4x64_epi64(cost_zero_dq1, 0xD8);
  __m256i ctx = _mm256_castsi128_si256(_mm_loadu_si64(&coeff_ctx->coef));
  __m256i fifteen = _mm256_set1_epi8(15);
  __m256i base_ctx = _mm256_and_si256(ctx, fifteen);
  __m256i base_ctx1 = _mm256_permute4x64_epi64(base_ctx, 0);
  __m256i ratez_dq0 = _mm256_shuffle_epi8(cost_dq0, base_ctx1);
  __m256i ratez_dq1 = _mm256_shuffle_epi8(cost_dq1, base_ctx1);
  __m256i ratez = _mm256_blend_epi16(ratez_dq0, ratez_dq1, 0xAA);
  ratez = _mm256_permute4x64_epi64(ratez, 0x88);
  __m256i shuf1 = _mm256_lddqu_si256((__m256i *)kShuf[1]);
  ratez = _mm256_shuffle_epi8(ratez, shuf1);
  _mm256_storeu_si256((__m256i *)&rd->rate_zero[0], ratez);

  // Calc coeff_base rate.
  int idx = AVMMIN(pq->qIdx - 1, 8);
  __m128i c_zero = _mm_setzero_si128();
  __m256i base_diag = _mm256_set1_epi8(base_diag_ctx);
  base_ctx = _mm256_add_epi8(base_ctx, base_diag);
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    int ctx0 = _mm256_extract_epi8(base_ctx, 0);
    int ctx1 = _mm256_extract_epi8(base_ctx, 1);
    int ctx2 = _mm256_extract_epi8(base_ctx, 2);
    int ctx3 = _mm256_extract_epi8(base_ctx, 3);
    base_ctx = _mm256_bsrli_epi128(base_ctx, 4);
    __m128i rate_01 = _mm_loadu_si64(&cost_low_tbl[idx][ctx0][0]);
    __m128i rate_23 = _mm_loadu_si64(&cost_low_tbl[idx][ctx1][0]);
    __m128i rate_45 = _mm_loadu_si64(&cost_low_tbl[idx][ctx2][1]);
    __m128i rate_67 = _mm_loadu_si64(&cost_low_tbl[idx][ctx3][1]);
    __m128i rate_0123 = _mm_unpacklo_epi32(rate_01, rate_23);
    __m128i rate_4567 = _mm_unpacklo_epi32(rate_45, rate_67);
    rate_0123 = _mm_unpacklo_epi16(rate_0123, c_zero);
    rate_4567 = _mm_unpacklo_epi16(rate_4567, c_zero);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
  }

  // Calc coeff/eob cost.
  int eob_ctx = coeff_ctx->coef_eob;
  __m128i rate_eob_coef = _mm_loadu_si64(&cost_eob_tbl[idx][eob_ctx][0]);
  rate_eob_coef = _mm_unpacklo_epi16(rate_eob_coef, c_zero);
  __m128i rate_eob_position = _mm_set1_epi32(eob_rate);
  __m128i rate_eob = _mm_add_epi32(rate_eob_coef, rate_eob_position);
  _mm_storeu_si64(&rd->rate_eob[0], rate_eob);

  // Chroma LF region consists of only DC coeffs.
  const int is_dc_coeff = 1;
  if (is_dc_coeff) {
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int a0 = i & 2 ? 1 : 0;
      int a1 = a0 + 2;
      int mid_cost0 = get_mid_cost_lf_dc(blk_pos, absLevel[a0], coeff_sign,
                                         coeff_ctx->coef[i], dc_sign_ctx,
                                         txb_costs, tmp_sign, plane);
      int mid_cost1 = get_mid_cost_lf_dc(blk_pos, absLevel[a1], coeff_sign,
                                         coeff_ctx->coef[i], dc_sign_ctx,
                                         txb_costs, tmp_sign, plane);
      rd->rate[2 * i] += mid_cost0;
      rd->rate[2 * i + 1] += mid_cost1;
    }
    int t_sign = tmp_sign[blk_pos];
    int eob_mid_cost0 =
        get_mid_cost_eob(blk_pos, 1, 1, absLevel[0], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, plane);
    int eob_mid_cost1 =
        get_mid_cost_eob(blk_pos, 1, 1, absLevel[2], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, plane);
    rd->rate_eob[0] += eob_mid_cost0;
    rd->rate_eob[1] += eob_mid_cost1;
  } else if (idx > 4) {
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int a0 = i & 2 ? 1 : 0;
      int a1 = a0 + 2;
      int mid_cost0 =
          get_mid_cost_lf(absLevel[a0], coeff_ctx->coef[i], txb_costs, plane);
      int mid_cost1 =
          get_mid_cost_lf(absLevel[a1], coeff_ctx->coef[i], txb_costs, plane);
      rd->rate[2 * i] += mid_cost0;
      rd->rate[2 * i + 1] += mid_cost1;
    }
    int t_sign = tmp_sign[blk_pos];
    int eob_mid_cost0 =
        get_mid_cost_eob(blk_pos, 1, 0, absLevel[0], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, plane);
    int eob_mid_cost1 =
        get_mid_cost_eob(blk_pos, 1, 0, absLevel[2], coeff_sign, dc_sign_ctx,
                         txb_costs, tx_class, t_sign, plane);
    rd->rate_eob[0] += eob_mid_cost0;
    rd->rate_eob[1] += eob_mid_cost1;
  }
}

void av2_get_rate_dist_def_chroma_avx2(
    const struct LV_MAP_COEFF_COST *txb_costs, const struct prequant_t *pq,
    const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int bwl,
    TX_CLASS tx_class, int diag_ctx, int eob_rate, int plane, int t_sign,
    int sign, struct tcq_rate_t *rd) {
  (void)bwl;
  const int32_t(*cost_zero)[SIG_COEF_CONTEXTS] = txb_costs->base_cost_uv_zero;
  const uint16_t(*cost_low_tbl)[SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_cost_uv_low_tbl;
  const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
      txb_costs->base_eob_cost_uv_tbl;
  const tran_low_t *absLevel = pq->absLevel;
  int base_diag_ctx = get_base_diag_ctx(diag_ctx);

  // Calc zero coeff costs.
  __m256i zero = _mm256_setzero_si256();
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][base_diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][base_diag_ctx]);
  __m256i ctx = _mm256_castsi128_si256(_mm_loadu_si64(&coeff_ctx->coef));
  __m256i ctx16 = _mm256_unpacklo_epi8(ctx, zero);
  __m256i ctx16sh = _mm256_shuffle_epi32(ctx16, 0xD8);
  __m256i ctx_dq0 = _mm256_unpacklo_epi16(ctx16sh, zero);
  __m256i ctx_dq1 = _mm256_unpackhi_epi16(ctx16sh, zero);
  __m256i ratez_dq0 = _mm256_permutevar8x32_epi32(cost_zero_dq0, ctx_dq0);
  __m256i ratez_dq1 = _mm256_permutevar8x32_epi32(cost_zero_dq1, ctx_dq1);
  __m256i ratez_0123 = _mm256_unpacklo_epi64(ratez_dq0, ratez_dq1);
  _mm_storeu_si128((__m128i *)&rd->rate_zero[0],
                   _mm256_castsi256_si128(ratez_0123));
  __m256i ratez_4567 = _mm256_unpackhi_epi64(ratez_dq0, ratez_dq1);
  _mm_storeu_si128((__m128i *)&rd->rate_zero[4],
                   _mm256_castsi256_si128(ratez_4567));

  // Calc coeff_base rate.
  int idx = AVMMIN(pq->qIdx - 1, 4);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi16(base_diag_ctx);
  __m256i base_ctx = _mm256_slli_epi16(ctx16, 12);
  base_ctx = _mm256_srli_epi16(base_ctx, 12);
  base_ctx = _mm256_add_epi16(base_ctx, diag);
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    int ctx0 = _mm256_extract_epi16(base_ctx, 0);
    int ctx1 = _mm256_extract_epi16(base_ctx, 1);
    int ctx2 = _mm256_extract_epi16(base_ctx, 2);
    int ctx3 = _mm256_extract_epi16(base_ctx, 3);
    base_ctx = _mm256_bsrli_epi128(base_ctx, 8);
    __m128i rate_01 = _mm_loadu_si64(&cost_low_tbl[idx][ctx0][0]);
    __m128i rate_23 = _mm_loadu_si64(&cost_low_tbl[idx][ctx1][0]);
    __m128i rate_45 = _mm_loadu_si64(&cost_low_tbl[idx][ctx2][1]);
    __m128i rate_67 = _mm_loadu_si64(&cost_low_tbl[idx][ctx3][1]);
    __m128i rate_0123 = _mm_unpacklo_epi32(rate_01, rate_23);
    __m128i rate_4567 = _mm_unpacklo_epi32(rate_45, rate_67);
    rate_0123 = _mm_unpacklo_epi16(rate_0123, c_zero);
    rate_4567 = _mm_unpacklo_epi16(rate_4567, c_zero);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
  }

  // Calc coeff/eob cost.
  int eob_ctx = coeff_ctx->coef_eob;
  __m128i rate_eob_coef = _mm_loadu_si64(&cost_eob_tbl[idx][eob_ctx][0]);
  rate_eob_coef = _mm_unpacklo_epi16(rate_eob_coef, c_zero);
  __m128i rate_eob_position = _mm_set1_epi32(eob_rate);
  __m128i rate_eob = _mm_add_epi32(rate_eob_coef, rate_eob_position);
  _mm_storeu_si64(&rd->rate_eob[0], rate_eob);

  // Calc coeff mid and high range cost.
  if (idx > 0 || plane) {
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int a0 = i & 2 ? 1 : 0;
      int a1 = a0 + 2;
      int mid_cost0 = get_mid_cost_def(absLevel[a0], coeff_ctx->coef[i],
                                       txb_costs, plane, t_sign, sign);
      int mid_cost1 = get_mid_cost_def(absLevel[a1], coeff_ctx->coef[i],
                                       txb_costs, plane, t_sign, sign);
      rd->rate[2 * i] += mid_cost0;
      rd->rate[2 * i + 1] += mid_cost1;
    }
    int eob_mid_cost0 = get_mid_cost_eob(blk_pos, 0, 0, absLevel[0], sign, 0,
                                         txb_costs, tx_class, t_sign, plane);
    int eob_mid_cost1 = get_mid_cost_eob(blk_pos, 0, 0, absLevel[2], sign, 0,
                                         txb_costs, tx_class, t_sign, plane);
    rd->rate_eob[0] += eob_mid_cost0;
    rd->rate_eob[1] += eob_mid_cost1;
  }
}

// Pre-calculate eob bits (rate) for each EOB candidate position from 1
// to the initial eob location. Store rate in array block_eob_rate[],
// starting with index.
void av2_calc_block_eob_rate_avx2(struct macroblock *x, int plane,
                                  TX_SIZE tx_size, int eob,
                                  uint16_t *block_eob_rate) {
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

  const int *tbl_eob_cost = txb_eob_costs->eob_cost[is_inter];
  const int(*tbl_eob_extra)[2] = txb_costs->eob_extra_cost;

  static const int8_t kShuf[4][32] = {
    { -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 8, 9, 12, 13, 12, 13,
      0,  1,  0,  1,  0, 1, 0, 1, 4, 5, 4, 5, 4,  5,  4,  5 },
    { 0, 1, 4, 5, 8, 9, 8, 9, 12, 13, 12, 13, 12, 13, 12, 13,
      0, 1, 0, 1, 0, 1, 0, 1, 0,  1,  0,  1,  0,  1,  0,  1 },
    { 8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,
      12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13 },
  };
#define BC1 (1 << AV2_PROB_COST_SHIFT)
#define BC2 (2 * BC1)
  static const uint16_t kBitCost[16] = {
    0, 0, 0, 0, BC1, BC1, BC1, BC1, BC2, BC2, BC2, BC2, BC2, BC2, BC2, BC2
  };

  // Write first 16 costs, block_eob_rate[0:15]
  // Convert 32-bit eob_pt costs { 0 1 2 3 4 5 6 7 } + eob_extra_cost
  // to expanded 16-bit costs { 0 1 2 2 3 3 3 3 4 4 4 4 4 4 4 4 }.
  __m256i eob_cost0_7 = _mm256_lddqu_si256((__m256i *)tbl_eob_cost);
  __m256i eob_extra0_7 = _mm256_lddqu_si256((__m256i *)tbl_eob_extra);
  __m256i shuf0 = _mm256_lddqu_si256((__m256i *)kShuf[0]);
  __m256i shuf1 = _mm256_lddqu_si256((__m256i *)kShuf[1]);
  __m256i eob_extra = _mm256_shuffle_epi8(eob_extra0_7, shuf0);
  __m256i eob_rate0_15 = _mm256_shuffle_epi8(eob_cost0_7, shuf1);
  eob_rate0_15 = _mm256_add_epi16(eob_rate0_15, eob_extra);
  __m256i bit_cost = _mm256_lddqu_si256((__m256i *)kBitCost);
  eob_rate0_15 = _mm256_add_epi16(eob_rate0_15, bit_cost);
  _mm256_storeu_si256((__m256i *)&block_eob_rate[0], eob_rate0_15);

  // Write second 16 costs, block_eob_rate[16:31]
  __m256i eob_cost4_7 = _mm256_permute4x64_epi64(eob_cost0_7, 0xEE);
  __m256i eob_extra4_7 = _mm256_permute4x64_epi64(eob_extra0_7, 0xEE);
  __m256i shuf2 = _mm256_lddqu_si256((__m256i *)kShuf[2]);
  __m256i shuf3 = _mm256_set1_epi16(0x0504);
  __m256i eob_extra16_31 = _mm256_shuffle_epi8(eob_extra4_7, shuf2);
  __m256i eob_rate16_31 = _mm256_shuffle_epi8(eob_cost4_7, shuf3);
  eob_rate16_31 = _mm256_add_epi16(eob_rate16_31, eob_extra16_31);
  __m256i bit_cost16_31 = _mm256_set1_epi16(3 * BC1);
  eob_rate16_31 = _mm256_add_epi16(eob_rate16_31, bit_cost16_31);
  _mm256_storeu_si256((__m256i *)&block_eob_rate[16], eob_rate16_31);

  // Write costs beyond position 32, block_eob_rate[32+]
  int scan_pos = 32;
  int n_offset_bits = 4;
  while (scan_pos < eob) {
    int eob_pt_low = AVMMIN(2 + n_offset_bits, EOB_PT_INDEX_COUNT - 1);
    int eob_pt_rate = tbl_eob_cost[eob_pt_low];
    if (eob_multi_size == 4 && (eob_pt_low == EOB_PT_INDEX_COUNT - 1))
      eob_pt_rate += av2_cost_literal(1);
    else if (eob_multi_size > 4 && (eob_pt_low == EOB_PT_INDEX_COUNT - 1))
      eob_pt_rate += av2_cost_literal(2);
    for (int bit = 0; bit < 2; bit++) {
      int eob_ctx = n_offset_bits;
      int extra_bit_rate = tbl_eob_extra[eob_ctx][bit];
      int eob_rate =
          eob_pt_rate + extra_bit_rate + av2_cost_literal(n_offset_bits);
      for (int i = 0; i < (1 << n_offset_bits); i += 16) {
        __m256i rate = _mm256_set1_epi16(eob_rate);
        _mm256_storeu_si256((__m256i *)&block_eob_rate[scan_pos], rate);
        scan_pos += 16;
      }
    }
    n_offset_bits++;
  }
}

static AVM_FORCE_INLINE int get_dqv(const int32_t *dequant, int coeff_idx,
                                    const qm_val_t *iqmatrix) {
  int dqv = dequant[!!coeff_idx];
  if (iqmatrix != NULL)
    dqv =
        ((iqmatrix[coeff_idx] * dqv) + (1 << (AVM_QM_BITS - 1))) >> AVM_QM_BITS;
  return dqv;
}

int av2_find_best_path_avx2(const struct tcq_node_t *trellis,
                            const int16_t *scan, const int32_t *dequant,
                            const qm_val_t *iqmatrix, const tran_low_t *tcoeff,
                            int first_scan_pos, int log_scale,
                            tran_low_t *qcoeff, tran_low_t *dqcoeff,
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
    __m128i dqv = _mm_loadu_si64(dequant);
    __m128i dqv_ac = _mm_srli_si128(dqv, 4);
    __m128i zero = _mm_setzero_si128();
    __m128i round = _mm_set1_epi64x(1 << (QUANT_TABLE_BITS - 1));
    int shift = QUANT_TABLE_BITS + log_scale;
    for (; prev_id >= 0; scan_pos++) {
      const int32_t *decision =
          (int32_t *)&trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
      __m128i info = _mm_loadu_si64(&decision[3]);
      int blk_pos = scan[scan_pos];
      __m128i sign = _mm_loadu_si64(&tcoeff[blk_pos]);
      sign = _mm_srai_epi32(sign, 31);
      __m128i abs_lev = _mm_slli_epi32(info, 8);
      __m128i abs_lev2 = _mm_srli_epi32(abs_lev, 7);
      abs_lev = _mm_srli_epi32(abs_lev, 8);
      __m128i dq = _mm_slli_epi32(info, 6);
      dq = _mm_srli_epi32(dq, 31);
      __m128i dq_mask = _mm_srai_epi32(info, 31);
      dq = _mm_andnot_si128(dq_mask, dq);
      abs_lev2 = _mm_sub_epi32(abs_lev2, dq);
      abs_lev2 = _mm_max_epi32(abs_lev2, zero);
      __m128i dqc = _mm_mul_epi32(abs_lev2, dqv);
      dqc = _mm_add_epi64(dqc, round);
      dqc = _mm_srli_epi64(dqc, shift);
      dqc = _mm_xor_si128(dqc, sign);
      dqc = _mm_sub_epi32(dqc, sign);
      __m128i lev = _mm_xor_si128(abs_lev, sign);
      lev = _mm_sub_epi32(lev, sign);

      // Older compilers don't implement _mm_storeu_si32()
      _mm_store_ss((float *)&qcoeff[blk_pos], _mm_castsi128_ps(lev));
      _mm_store_ss((float *)&dqcoeff[blk_pos], _mm_castsi128_ps(dqc));
      dqv = dqv_ac;
      __m128i prevId = _mm_srai_epi32(info, 24);
      prev_id = _mm_extract_epi32(prevId, 0);
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
      int dq = prev_id >= 0 ? tcq_quant(prev_id) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - dq);
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
