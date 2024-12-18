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

#include "aom/aom_integer.h"
#include "aom_dsp/x86/mem_sse2.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/quant_common.h"
#include "av1/encoder/trellis_quant.h"
#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"

// av1_decide_states_*() constants.
static const int32_t kShuffle[8] = { 0, 2, 1, 3, 5, 7, 4, 6 };
static const int32_t kPrevId[TCQ_MAX_STATES / 4][8] = {
  { 0, 0 << 24, 0, 1 << 24, 0, 2 << 24, 0, 3 << 24 },
  { 0, 4 << 24, 0, 5 << 24, 0, 6 << 24, 0, 7 << 24 },
};

// av1_calc_lf_ctx_*() constants.
// Neighbor mask for calculating context sum (base/mid).
#define Z -1
#define M MAX_VAL_BR_CTX
static const int8_t kNbrMask[4][32] = {
  { 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // diag 0
    M, M, 0, M, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 5, 5, 0, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // diag 1
    0, M, M, 0, 0, M, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 5, 5, 0, 0, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0,  // diag 2
    0, 0, M, M, 0, 0, 0, M, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 5, 5, 0, 0, 0, 5, 5, 5, 0, 0, 0, 0, 0,  // diag 3
    0, 0, 0, M, M, 0, 0, 0, 0, M, 0, 0, 0, 0, 0, 0 },
};
static const int8_t kMaxCtx[16] = { 8, 6, 6, 4, 4, 4, 4, 4,
                                    4, 4, 4, 4, 4, 4, 4, 4 };
static const int8_t kScanDiag[MAX_LF_SCAN] = { 0, 1, 1, 2, 2, 2, 3, 3, 3, 3 };

static const uint8_t kGolombExp0Bits[256] = {
  0,  0,  0,  0,  1,  1,  1,  1,  3,  3,  3,  3,  3,  3,  3,  3,  5,  5,  5,
  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  7,  7,  7,  7,  7,  7,
  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
  7,  7,  7,  7,  7,  7,  7,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11,
};

static const int8_t kGolombShuf[4][16] = {
  { 0, Z, Z, Z, 2, Z, Z, Z, 1, Z, Z, Z, 3, Z, Z, Z },
  { 3, Z, Z, Z, 1, Z, Z, Z, 0, Z, Z, Z, 2, Z, Z, Z },
  { 2, Z, Z, Z, 0, Z, Z, Z, 3, Z, Z, Z, 1, Z, Z, Z },
  { 1, Z, Z, Z, 3, Z, Z, Z, 2, Z, Z, Z, 0, Z, Z, Z }
};

void av1_decide_states_avx2(const struct tcq_node_t *prev,
                            const struct tcq_rate_t *rd,
                            const struct prequant_t *pq, int limits,
                            int try_eob, int64_t rdmult,
                            struct tcq_node_t *decision) {
  (void)limits;
  assert((rdmult >> 32) == 0);
  assert(sizeof(tcq_node_t) == 16);

  __m256i c_rdmult = _mm256_set1_epi64x(rdmult);
  __m256i c_round = _mm256_set1_epi64x(1 << (AV1_PROB_COST_SHIFT - 1));
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
    rdcost0257 = _mm256_srli_epi64(rdcost0257, AV1_PROB_COST_SHIFT);
    rdcost1346 = _mm256_srli_epi64(rdcost1346, AV1_PROB_COST_SHIFT);
    rdcost0257 = _mm256_add_epi64(rdcost0257, dist0033);
    rdcost1346 = _mm256_add_epi64(rdcost1346, dist2211);

    // Calc rd-cost for zero quant.
    __m256i ratezero = _mm256_castsi128_si256(
        _mm_lddqu_si128((__m128i *)&rd->rate_zero[4 * i]));
    ratezero = _mm256_permute4x64_epi64(ratezero, 0x50);
    ratezero = _mm256_unpacklo_epi32(ratezero, c_zero);
    __m256i rdcostzero = _mm256_mul_epu32(c_rdmult, ratezero);
    rdcostzero = _mm256_add_epi64(rdcostzero, c_round);
    rdcostzero = _mm256_srli_epi64(rdcostzero, AV1_PROB_COST_SHIFT);

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
    rdcost_eob = _mm256_srli_epi64(rdcost_eob, AV1_PROB_COST_SHIFT);
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

void av1_pre_quant_avx2(tran_low_t tqc, struct prequant_t *pqData,
                        const int32_t *quant_ptr, int dqv, int log_scale,
                        int scan_pos) {
  static const int32_t kInc[4][4] = {
    { 0, 1, 2, 3 }, { 3, 0, 1, 2 }, { 2, 3, 0, 1 }, { 1, 2, 3, 0 }
  };

  // calculate qIdx
  int shift = 16 - log_scale + QUANT_FP_BITS;
  int32_t add = -((2 << shift) >> 1);
  int32_t abs_tqc = abs(tqc);

  int32_t qIdx = (int)AOMMAX(
      1, AOMMIN(((1 << 16) - 1),
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

void av1_update_states_avx2(tcq_node_t *decision, int scan_idx,
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

void av1_calc_diag_ctx_avx2(int scan_hi, int scan_lo, int bwl,
                            const uint8_t *prev_levels, const int16_t *scan,
                            uint8_t *ctx) {
#define M MAX_VAL_BR_CTX
  static const int8_t kClip[2][16] = {
    { 0, 0, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3, 0, 3, 0, 0 },
    { 0, 0, M, 0, M, M, 0, 0, M, 0, M, M, 0, 0, 0, 0 },
  };
#undef M
  int n_ctx = scan_hi - scan_lo + 1;
  __m128i zero = _mm_setzero_si128();
  __m128i one = _mm_set1_epi8(1);
  __m128i four = _mm_set1_epi8(4);
  __m128i six = _mm_set1_epi8(6);
  __m128i clip = _mm_lddqu_si128((__m128i *)&kClip[0][0]);
  __m128i clip_mid = _mm_lddqu_si128((__m128i *)&kClip[1][0]);

  int blk_pos = scan[scan_lo];
  int row_inc = (1 << bwl) + (1 << TX_PAD_HOR_LOG2) - 1;
  const uint8_t *row_ptr = prev_levels + get_padded_idx(blk_pos, bwl) + 1;
  const uint8_t *min_row_ptr = prev_levels;
  __m128i nbr2 = _mm_loadu_si64(&row_ptr[row_inc]);
  __m128i nbr3 = _mm_loadu_si64(&row_ptr[2 * row_inc]);
  __m128i nbr23 = _mm_unpacklo_epi16(nbr2, nbr3);

  for (int i = 0; i < n_ctx; i += 2) {
    const uint8_t *p1 = AOMMAX(min_row_ptr, &row_ptr[-row_inc]);
    __m128i nbr0 = _mm_loadu_si64(p1);
    __m128i nbr1 = _mm_loadu_si64(&row_ptr[0]);
    __m128i nbr01 = _mm_unpacklo_epi16(nbr0, nbr1);
    __m128i nbr0123 = _mm_unpacklo_epi32(nbr01, nbr23);
    __m128i nbr = _mm_unpacklo_epi64(nbr0123, nbr0123);
    __m128i nbr_max = _mm_min_epu8(nbr, clip);
    __m128i sum = _mm_maddubs_epi16(nbr_max, one);
    sum = _mm_hadd_epi16(sum, zero);
    sum = _mm_hadd_epi16(sum, zero);
    __m128i coeff_ctx = _mm_packs_epi16(sum, sum);
    coeff_ctx = _mm_avg_epu8(coeff_ctx, zero);
    coeff_ctx = _mm_min_epi8(coeff_ctx, four);
    __m128i nbr_max_mid = _mm_min_epu8(nbr, clip_mid);
    __m128i sum_mid = _mm_maddubs_epi16(nbr_max_mid, one);
    sum_mid = _mm_hadd_epi16(sum_mid, zero);
    sum_mid = _mm_hadd_epi16(sum_mid, zero);
    __m128i coeff_mid_ctx = _mm_packs_epi16(sum_mid, sum_mid);
    coeff_mid_ctx = _mm_avg_epu8(coeff_mid_ctx, zero);
    coeff_mid_ctx = _mm_min_epi8(coeff_mid_ctx, six);
    coeff_mid_ctx = _mm_slli_epi16(coeff_mid_ctx, 4);
    coeff_ctx = _mm_add_epi8(coeff_ctx, coeff_mid_ctx);
    uint16_t ctx01 = _mm_extract_epi16(coeff_ctx, 0);
    uint16_t ctx1 = ctx01 >> 8;
    ctx[i] = (uint8_t)ctx01;
    ctx[i + 1] = ctx1;
    row_ptr -= 2 * row_inc;
    nbr23 = nbr01;
  }
}

static INLINE int get_mid_cost_def(tran_low_t abs_qc, int coeff_ctx,
                                   const LV_MAP_COEFF_COST *txb_costs,
                                   int plane, int t_sign, int sign) {
  int cost = 0;
  if (plane == AOM_PLANE_V) {
    cost += txb_costs->v_ac_sign_cost[t_sign][sign] - av1_cost_literal(1);
  }
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
      cost -= av1_cost_literal(1);
      if (plane == AOM_PLANE_V) {
        cost += txb_costs->v_dc_sign_cost[t_sign][dc_sign_ctx][sign];
      } else {
        cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
      }
    } else {
      if (plane == AOM_PLANE_V) {
        cost += txb_costs->v_ac_sign_cost[t_sign][sign] - av1_cost_literal(1);
      }
    }
    if (plane > 0) {
      if (abs_qc > LF_NUM_BASE_LEVELS) {
        int br_ctx = get_br_ctx_lf_eob_chroma(ci, tx_class);
        cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost_uv[br_ctx]);
      }
    } else {
      if (abs_qc > LF_NUM_BASE_LEVELS) {
        int br_ctx = get_br_ctx_lf_eob(ci, tx_class);
        cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[br_ctx]);
      }
    }
  } else {
    if (plane == AOM_PLANE_V) {
      cost += txb_costs->v_ac_sign_cost[t_sign][sign] - av1_cost_literal(1);
    }
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
  cost -= av1_cost_literal(1);  // Remove previously added sign cost.
  if (plane == AOM_PLANE_V)
    cost += txb_costs->v_dc_sign_cost[tmp_sign[ci]][dc_sign_ctx][sign];
  else
    cost += txb_costs->dc_sign_cost[dc_ph_group][dc_sign_ctx][sign];
  if (plane > 0) {
    if (abs_qc > LF_NUM_BASE_LEVELS) {
      cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost_uv[mid_ctx]);
    }
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
#if 1
  assert(plane == 0);
  (void)plane;
  if (abs_qc > LF_NUM_BASE_LEVELS) {
    cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
  }
#else
  if (plane > 0) {
    if (abs_qc > LF_NUM_BASE_LEVELS) {
      cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost_uv[mid_ctx]);
    }
  } else {
    if (abs_qc > LF_NUM_BASE_LEVELS) {
      cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
    }
  }
#endif
  return cost;
}

void av1_get_rate_dist_def_luma_avx2(const struct LV_MAP_COEFF_COST *txb_costs,
                                     const struct prequant_t *pq,
                                     const tcq_coeff_ctx_t *coeff_ctx,
                                     int blk_pos, int bwl, TX_CLASS tx_class,
                                     int diag_ctx, int eob_rate,
                                     struct tcq_rate_t *rd) {
  (void)bwl;
  (void)blk_pos;
  (void)tx_class;
  const int32_t(*cost_zero)[SIG_COEF_CONTEXTS] = txb_costs->base_cost_zero;
  const uint16_t(*cost_low_tbl)[SIG_COEF_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->base_cost_low_tbl;
  const uint16_t(*cost_eob_tbl)[SIG_COEF_CONTEXTS_EOB][2] =
      txb_costs->base_eob_cost_tbl;
  const uint16_t(*cost_mid_tbl)[LEVEL_CONTEXTS][TCQ_CTXS][2] =
      txb_costs->mid_cost_tbl;
  const tran_low_t *absLevel = pq->absLevel;

  // Calc zero coeff costs.
  __m256i zero = _mm256_setzero_si256();
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][diag_ctx]);

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
  int idx = AOMMIN(qIdx - 1, 4);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi16(diag_ctx);
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
    int mid_idx = AOMMIN(qIdx - 1, 10);
    __m128i mid_rate_eob =
        _mm_loadu_si64(&txb_costs->mid_cost_tbl[mid_idx][0][0][0]);
    mid_rate_eob = _mm_unpacklo_epi16(mid_rate_eob, c_zero);
    rate_eob = _mm_add_epi32(rate_eob, mid_rate_eob);
    _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
    __m256i mid_ctx = _mm256_srli_epi16(ctx16, 4);
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

void av1_calc_lf_ctx_st8_avx2(const struct tcq_lf_ctx_t *lf_ctx, int scan_pos,
                              struct tcq_coeff_ctx_t *coeff_ctx) {
  int diag = kScanDiag[scan_pos];
  __m256i zero = _mm256_setzero_si256();
  __m256i nbr_mask = _mm256_lddqu_si256((__m256i *)kNbrMask[diag]);
  __m256i base_mask = _mm256_permute2x128_si256(nbr_mask, nbr_mask, 0);
  __m256i mid_mask = _mm256_permute2x128_si256(nbr_mask, nbr_mask, 0x11);

  for (int st = 0; st < TCQ_N_STATES; st += 4) {
    // Load previously decoded LF context values.
    __m256i last01 = _mm256_lddqu_si256((__m256i *)&lf_ctx[st]);
    __m256i last23 = _mm256_lddqu_si256((__m256i *)&lf_ctx[st + 2]);

    // Calc base ctx neighbor sum.
    __m256i base01 = _mm256_min_epu8(last01, base_mask);
    __m256i base23 = _mm256_min_epu8(last23, base_mask);
    __m256i base01_sum = _mm256_sad_epu8(base01, zero);
    __m256i base23_sum = _mm256_sad_epu8(base23, zero);
    __m256i base_sum =
        _mm256_hadd_epi32(base01_sum, base23_sum);  // B0 B0 B2 B2 B1 B1 B3 B3

    // Calc mid ctx neighbor sum.
    __m256i mid01 = _mm256_min_epu8(last01, mid_mask);
    __m256i mid23 = _mm256_min_epu8(last23, mid_mask);
    __m256i mid01_sum = _mm256_sad_epu8(mid01, zero);
    __m256i mid23_sum = _mm256_sad_epu8(mid23, zero);
    __m256i mid_sum =
        _mm256_hadd_epi32(mid01_sum, mid23_sum);  // M0 M0 M2 M2 M1 M1 M3 M3

    // Context calc; combine and reduce to 8 bits.
    __m256i base_mid =
        _mm256_hadd_epi32(base_sum, mid_sum);  // B0B2 M0M2 B1B3 M1M3
    base_mid = _mm256_hadd_epi16(
        base_mid, zero);  // reduce to 16 bits B0B2 M0M2 - - B1B3 M1M3 - -
    base_mid = _mm256_avg_epu16(base_mid, zero);  // x = (x + 1) >> 1
    base_mid = _mm256_shufflelo_epi16(
        base_mid, 0xD8);  // shuffle B0M0 B2M2 - - B1M1 B3M3 - -
    base_mid = _mm256_permute4x64_epi64(
        base_mid, 0xD8);  // pack into lower half: B0M0 B2M2 B1M1 B3M3
    base_mid = _mm256_shuffle_epi32(base_mid, 0xD8);  // B0M0 B1M1 B2M2 B3M3
    __m256i six = _mm256_set1_epi16(6);
    __m256i mid = _mm256_min_epi16(base_mid, six);
    __m256i mid_sh4 = _mm256_slli_epi16(mid, 4);
    __m256i base_max = _mm256_set1_epi16(kMaxCtx[scan_pos]);
    __m256i base = _mm256_min_epi16(base_mid, base_max);
    base_mid = _mm256_blend_epi16(base, mid_sh4, 0xAA);
    __m256i ctx16 = _mm256_hadd_epi16(base_mid, base_mid);
    __m256i mid_ctx_offset = _mm256_set1_epi16((scan_pos == 0) ? 0 : (7 << 4));
    ctx16 = _mm256_add_epi16(ctx16, mid_ctx_offset);
    __m128i ctx8 = _mm256_castsi256_si128(ctx16);
    ctx8 = _mm_packus_epi16(ctx8, ctx8);
#if 1
    // Older compilers don't implement _mm_storeu_si32()
    _mm_store_ss((float *)&coeff_ctx->coef[st], _mm_castsi128_ps(ctx8));
#else
    _mm_storeu_si32(&coeff_ctx->coef[st], ctx8);
#endif
  }
}

void av1_update_lf_ctx_avx2(const struct tcq_node_t *decision,
                            struct tcq_lf_ctx_t *lf_ctx) {
  __m256i c_zero = _mm256_setzero_si256();
  __m256i upd_last_a = c_zero;
  __m256i upd_last_b = c_zero;
  __m256i upd_last_c = c_zero;
  __m256i upd_last_d = c_zero;

  for (int st = 0; st < TCQ_N_STATES; st += 2) {
    int absLevel0 = decision[st].absLevel;
    int prevId0 = decision[st].prevId;
    int absLevel1 = decision[st + 1].absLevel;
    int prevId1 = decision[st + 1].prevId;
    __m128i upd0 = _mm_setzero_si128();
    __m128i upd1 = _mm_setzero_si128();
    if (prevId0 >= 0) {
      upd0 = _mm_lddqu_si128((__m128i *)lf_ctx[prevId0].last);
    }
    if (prevId1 >= 0) {
      upd1 = _mm_lddqu_si128((__m128i *)lf_ctx[prevId1].last);
    }
    upd0 = _mm_slli_si128(upd0, 1);
    upd1 = _mm_slli_si128(upd1, 1);
    upd0 = _mm_insert_epi8(upd0, AOMMIN(absLevel0, INT8_MAX), 0);
    upd1 = _mm_insert_epi8(upd1, AOMMIN(absLevel1, INT8_MAX), 0);
    __m256i upd01 = _mm256_castsi128_si256(upd0);
    upd01 = _mm256_inserti128_si256(upd01, upd1, 1);
    upd_last_d = upd_last_c;
    upd_last_c = upd_last_b;
    upd_last_b = upd_last_a;
    upd_last_a = upd01;
  }
  _mm256_storeu_si256((__m256i *)lf_ctx[0].last, upd_last_d);
  _mm256_storeu_si256((__m256i *)lf_ctx[2].last, upd_last_c);
  _mm256_storeu_si256((__m256i *)lf_ctx[4].last, upd_last_b);
  _mm256_storeu_si256((__m256i *)lf_ctx[6].last, upd_last_a);
}

void av1_get_rate_dist_lf_luma_avx2(const struct LV_MAP_COEFF_COST *txb_costs,
                                    const struct prequant_t *pq,
                                    const struct tcq_coeff_ctx_t *coeff_ctx,
                                    int blk_pos, int diag_ctx, int eob_rate,
                                    int dc_sign_ctx, const int32_t *tmp_sign,
                                    int bwl, TX_CLASS tx_class, int coeff_sign,
                                    struct tcq_rate_t *rd) {
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
  const int plane = 0;

  // Calc zero coeff costs.
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][diag_ctx]);
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
  int idx = AOMMIN(qIdx - 1, 8);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi8(diag_ctx);
  base_ctx = _mm256_add_epi8(base_ctx, diag);
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
    int mid_idx = AOMMIN(qIdx - 1, 14);
    int br_ctx_eob = 7;
    __m128i mid_rate_eob =
        _mm_loadu_si64(&txb_costs->mid_lf_cost_tbl[mid_idx][br_ctx_eob][0][0]);
    mid_rate_eob = _mm_unpacklo_epi16(mid_rate_eob, c_zero);
    rate_eob = _mm_add_epi32(rate_eob, mid_rate_eob);
    _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
    __m256i zero = _mm256_setzero_si256();
    __m256i mid_ctx = _mm256_unpacklo_epi8(ctx, zero);
    mid_ctx = _mm256_srli_epi16(mid_ctx, 4);
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

void av1_get_rate_dist_lf_chroma_avx2(const struct LV_MAP_COEFF_COST *txb_costs,
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

  // Calc zero coeff costs.
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][diag_ctx]);
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
  int idx = AOMMIN(pq->qIdx - 1, 8);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi8(diag_ctx);
  base_ctx = _mm256_add_epi8(base_ctx, diag);
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
#if 1
  const int is_dc_coeff = 1;
#else
  const int row = blk_pos >> bwl;
  const int col = blk_pos - (row << bwl);
  const bool dc_2dtx = (blk_pos == 0);
  const bool dc_hor = (col == 0) && tx_class == TX_CLASS_HORIZ;
  const bool dc_ver = (row == 0) && tx_class == TX_CLASS_VERT;
  const bool is_dc_coeff = dc_2dtx || dc_hor || dc_ver;
#endif
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

void av1_get_rate_dist_def_chroma_avx2(
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

  // Calc zero coeff costs.
  __m256i zero = _mm256_setzero_si256();
  __m256i cost_zero_dq0 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[0][diag_ctx]);
  __m256i cost_zero_dq1 =
      _mm256_lddqu_si256((__m256i *)&cost_zero[1][diag_ctx]);
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
  int idx = AOMMIN(pq->qIdx - 1, 4);
  __m128i c_zero = _mm_setzero_si128();
  __m256i diag = _mm256_set1_epi16(diag_ctx);
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

void av1_init_lf_ctx_avx2(const uint8_t *lev, int scan_hi, int bwl,
                          struct tcq_lf_ctx_t *lf_ctx) {
  // Sample offsets (row/col) in and around the LF region used for ctx calc.
  const uint8_t diag_scan[21] = { 0x00, 0x10, 0x01, 0x20, 0x11, 0x02, 0x30,
                                  0x21, 0x12, 0x03, 0x40, 0x31, 0x22, 0x13,
                                  0x04, 0x50, 0x41, 0x32, 0x23, 0x14, 0x05 };
  const int8_t kShuf[16] = { 8, 6, 4, 2,  0,  11, 9,  7,
                             5, 3, 1, -1, -1, -1, -1, -1 };
  __m128i zero = _mm_setzero_si128();

  int eob_inside_lf_region = scan_hi < MAX_LF_SCAN - 1;
  if (eob_inside_lf_region) {
    // Retrive the EOB value and store in LF ctx.
    int row_col = diag_scan[scan_hi + 1];
    int row = row_col >> 4;
    int col = row_col & 15;
    int blk_pos = (row << bwl) + col;
    uint8_t lev0 = lev[get_padded_idx(blk_pos, bwl)];
    __m128i last = _mm_insert_epi8(zero, lev0, 0);
    _mm_storeu_si128((__m128i *)lf_ctx->last, last);
  } else {
    // Retrieve samples in the two diagonals bordering LF region.
    int offset = (1 << bwl) + TX_PAD_HOR - 1;
    const uint8_t *p = lev + 4;
    __m128i row0 = _mm_loadu_si64(p);
    __m128i row1 = _mm_loadu_si64(p + offset);
    __m128i row2 = _mm_loadu_si64(p + 2 * offset);
    __m128i row3 = _mm_loadu_si64(p + 3 * offset);
    __m128i row4 = _mm_loadu_si64(p + 4 * offset);
    __m128i row5 = _mm_loadu_si64(p + 5 * offset);
    __m128i row01 = _mm_unpacklo_epi16(row0, row1);
    __m128i row23 = _mm_unpacklo_epi16(row2, row3);
    __m128i row45 = _mm_unpacklo_epi16(row4, row5);
    __m128i row0123 = _mm_unpacklo_epi32(row01, row23);
    __m128i row012345 = _mm_unpacklo_epi64(row0123, row45);
    __m128i shuf = _mm_lddqu_si128((__m128i *)kShuf);
    __m128i last = _mm_shuffle_epi8(row012345, shuf);
    _mm_storeu_si128((__m128i *)lf_ctx->last, last);
  }
}

// Pre-calculate eob bits (rate) for each EOB candidate position from 1
// to the initial eob location. Store rate in array block_eob_rate[],
// starting with index.
void av1_calc_block_eob_rate_avx2(struct macroblock *x, int plane,
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

#if CONFIG_EOB_POS_LUMA
  const int *tbl_eob_cost = txb_eob_costs->eob_cost[is_inter];
#else
  const int *tbl_eob_cost = txb_eob_costs->eob_cost;
#endif
  const int(*tbl_eob_extra)[2] = txb_costs->eob_extra_cost;

  static const int8_t kShuf[4][32] = {
    { -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 8, 9, 12, 13, 12, 13,
      0,  1,  0,  1,  0, 1, 0, 1, 4, 5, 4, 5, 4,  5,  4,  5 },
    { 0, 1, 4, 5, 8, 9, 8, 9, 12, 13, 12, 13, 12, 13, 12, 13,
      0, 1, 0, 1, 0, 1, 0, 1, 0,  1,  0,  1,  0,  1,  0,  1 },
    { 8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,
      12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13, 12, 13 },
  };
#define BC1 (1 << AV1_PROB_COST_SHIFT)
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
    int eob_pt_rate = tbl_eob_cost[2 + n_offset_bits];
    for (int bit = 0; bit < 2; bit++) {
      int eob_ctx = n_offset_bits;
      int extra_bit_rate = tbl_eob_extra[eob_ctx][bit];
      int eob_rate =
          eob_pt_rate + extra_bit_rate + av1_cost_literal(n_offset_bits);
      for (int i = 0; i < (1 << n_offset_bits); i += 16) {
        __m256i rate = _mm256_set1_epi16(eob_rate);
        _mm256_storeu_si256((__m256i *)&block_eob_rate[scan_pos], rate);
        scan_pos += 16;
      }
    }
    n_offset_bits++;
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

int av1_find_best_path_avx2(const struct tcq_node_t *trellis,
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
#if 1
      // Older compilers don't implement _mm_storeu_si32()
      _mm_store_ss((float *)&qcoeff[blk_pos], _mm_castsi128_ps(lev));
      _mm_store_ss((float *)&dqcoeff[blk_pos], _mm_castsi128_ps(dqc));
#else
      _mm_storeu_si32(&qcoeff[blk_pos], lev);
      _mm_storeu_si32(&dqcoeff[blk_pos], dqc);
#endif
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
