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
#include "av2/encoder/x86/trellis_quant_avx2.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"

static const uint8_t kConst[4][16] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
};

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
  __m128i prev_st0 = _mm_loadu_si64(tcq_ctx->prev_st[idx0]);
  __m128i prev_st1 = _mm_loadu_si64(tcq_ctx->prev_st[idx0 + 1]);
  __m128i state1 = map_state(prev_st0, state0);
  __m128i state2 = map_state(prev_st1, state1);
  __m128i state01 = _mm_unpacklo_epi64(state0, state1);
  state01 = _mm_or_si128(state01, _mm256_castsi256_si128(mask8));
  lev01 = _mm_shuffle_epi8(lev01, state01);
  __m256i lev__01 = _mm256_set_m128i(lev01, lev01);

  for (int i = idx0; i < idx_end; i += 4) {
    // Track state transitions.
    // One load per column: each matches one 8-byte store.
    __m128i prev_st2 = _mm_loadu_si64(tcq_ctx->prev_st[i + 2]);
    __m128i prev_st3 = _mm_loadu_si64(tcq_ctx->prev_st[i + 3]);
    __m128i prev_st4 = _mm_loadu_si64(tcq_ctx->prev_st[i + 4]);
    __m128i prev_st5 = _mm_loadu_si64(tcq_ctx->prev_st[i + 5]);
    // Composed maps from column i + 2, off the state chain.
    __m128i map3 = map_state(prev_st3, prev_st2);
    __m128i map4 = map_state(prev_st4, map3);
    __m128i map5 = map_state(prev_st5, map4);
    __m128i state3 = map_state(prev_st2, state2);
    __m128i state4 = map_state(map3, state2);
    __m128i state5 = map_state(map4, state2);
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
    state2 = map_state(map5, state2);
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

// One backtrack step: writes the levels at scan_pos, returns the previous
// state.
static AVM_FORCE_INLINE int find_best_path_step(
    const tcq_node_t *trellis, const int16_t *scan, const tran_low_t *tcoeff,
    int scan_pos, int prev_id, __m128i dqv, __m128i round, int shift,
    tran_low_t *qcoeff, tran_low_t *dqcoeff) {
  const __m128i zero = _mm_setzero_si128();
  const uint8_t *decision =
      (const uint8_t *)&trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
  __m128i info = xx_loadl_32(decision + 12);
  int blk_pos = scan[scan_pos];
  __m128i sign = _mm_cvtsi32_si128(tcoeff[blk_pos]);
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
  __m128i prevId = _mm_srai_epi32(info, 24);
  return _mm_extract_epi32(prevId, 0);
}

// Backtracking reads one trellis row (8 nodes, 128 bytes) per scan position,
// starting at DC. The rows near DC were written last, so paths of up to
// TCQ_BT_L1_ROWS rows (32 KB, the L1 size) are not prefetched. In longer
// paths only the first TCQ_BT_PF_ROW rows (20 KB) are taken to be in L1, and
// the rows from TCQ_BT_PF_ROW on are prefetched TCQ_BT_PF_DIST positions
// ahead.
#define TCQ_BT_L1_ROWS 256
#define TCQ_BT_PF_ROW 160
#define TCQ_BT_PF_DIST 8

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
    const __m128i dqv_dc = _mm_loadu_si64(dequant);
    const __m128i dqv_ac = _mm_srli_si128(dqv_dc, 4);
    const __m128i round = _mm_set1_epi64x(1 << (QUANT_TABLE_BITS - 1));
    const int shift = QUANT_TABLE_BITS + log_scale;
    __m128i dqv = dqv_dc;
    if (first_scan_pos < TCQ_BT_L1_ROWS) {
      for (; prev_id >= 0; scan_pos++) {
        prev_id = find_best_path_step(trellis, scan, tcoeff, scan_pos, prev_id,
                                      dqv, round, shift, qcoeff, dqcoeff);
        dqv = dqv_ac;
      }
    } else {
      // Prefetching starts TCQ_BT_PF_DIST positions before TCQ_BT_PF_ROW.
      for (; prev_id >= 0 && scan_pos < TCQ_BT_PF_ROW - TCQ_BT_PF_DIST;
           scan_pos++) {
        prev_id = find_best_path_step(trellis, scan, tcoeff, scan_pos, prev_id,
                                      dqv, round, shift, qcoeff, dqcoeff);
        dqv = dqv_ac;
      }
      for (; prev_id >= 0 && scan_pos <= first_scan_pos - TCQ_BT_PF_DIST;
           scan_pos++) {
        const tcq_node_t *ahead =
            &trellis[(scan_pos + TCQ_BT_PF_DIST) << TCQ_N_STATES_LOG];
        _mm_prefetch((const char *)ahead, _MM_HINT_T0);
        _mm_prefetch((const char *)&ahead[TCQ_N_STATES - 1], _MM_HINT_T0);
        prev_id = find_best_path_step(trellis, scan, tcoeff, scan_pos, prev_id,
                                      dqv_ac, round, shift, qcoeff, dqcoeff);
      }
      // The last rows are already prefetched.
      for (; prev_id >= 0; scan_pos++) {
        prev_id = find_best_path_step(trellis, scan, tcoeff, scan_pos, prev_id,
                                      dqv_ac, round, shift, qcoeff, dqcoeff);
      }
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

// -----------------------------------------------------------------------------

// absLevel | prevId << 24 of each state, lane order.
static AVM_FORCE_INLINE __m256i tcq_regs_hi(const TcqRegs *s) {
  return _mm256_or_si256(
      _mm256_srli_epi64(s->info_a, 32),
      _mm256_andnot_si256(_mm256_set1_epi64x(0xFFFFFFFF), s->info_b));
}

static AVM_FORCE_INLINE __m256i tcq_gt(__m256i a, __m256i b) {
  return _mm256_cmpgt_epi64(a, b);
}

static AVM_FORCE_INLINE __m256i tcq_sel(__m256i m, __m256i a, __m256i b) {
  return _mm256_blendv_epi8(a, b, m);
}

// No vpminsq: blend on m.
static AVM_FORCE_INLINE __m256i tcq_min(__m256i m, __m256i a, __m256i b) {
  return _mm256_blendv_epi8(a, b, m);
}

// Each state keeps the cheaper of its even candidate and its partner's odd
// candidate (ties go to the even one), then moves to its next state.
// With try_eob, a new eob replaces state 0 / 4 when strictly cheaper.
static AVM_FORCE_INLINE void tcq_select(TcqRegs *s, const TcqCand *k,
                                        int try_eob, __m256i eob_cand_rd,
                                        __m256i eob_cand_info) {
  const __m256i use_odd_a = _mm256_cmpgt_epi64(k->ev_rd_a, k->od_rd_b);
  const __m256i use_odd_b = _mm256_cmpgt_epi64(k->ev_rd_b, k->od_rd_a);
  const __m256i ra = _mm256_blendv_epi8(k->ev_rd_a, k->od_rd_b, use_odd_a);
  const __m256i rb = _mm256_blendv_epi8(k->ev_rd_b, k->od_rd_a, use_odd_b);
  const __m256i ia = _mm256_blendv_epi8(k->ev_info_a, k->od_info_b, use_odd_a);
  const __m256i ib = _mm256_blendv_epi8(k->ev_info_b, k->od_info_a, use_odd_b);
  __m256i rlo = _mm256_unpacklo_epi64(ra, rb);
  const __m256i rhi = _mm256_unpackhi_epi64(ra, rb);
  __m256i ilo = _mm256_unpacklo_epi64(ia, ib);
  const __m256i ihi = _mm256_unpackhi_epi64(ia, ib);
  if (try_eob) {
    // New eob of states 0 and 4: lanes 0 and 1 of rlo / ilo. Lanes 2 and 3
    // feed rd_b, so eob_cand_rd must be INT64_MAX there.
    const __m256i use = _mm256_cmpgt_epi64(rlo, eob_cand_rd);
    rlo = _mm256_blendv_epi8(rlo, eob_cand_rd, use);
    ilo = _mm256_blendv_epi8(ilo, eob_cand_info, use);
  }
  s->rd_a = _mm256_permute2x128_si256(rlo, rhi, 0x20);
  s->rd_b = _mm256_permute2x128_si256(rhi, rlo, 0x31);
  s->info_a = _mm256_permute2x128_si256(ilo, ihi, 0x20);
  s->info_b = _mm256_permute2x128_si256(ihi, ilo, 0x31);
}

// Per-origin tables of the general path, even | odd << 16 for Q0 and Q1. The
// context offsets are at most 6, so the lookups read entries 0 to 6 only.

// 64-bit entries of the origins { 0, 1, 4, 5 } (ga) / { 2, 3, 6, 7 } (gb), in
// the order that the 32-bit shuffles of tcq_tables_full() expect.
static AVM_FORCE_INLINE void tcq_lookup_origins(const uint16_t *t, __m256i ia,
                                                __m256i ib, __m256i ha,
                                                __m256i hb, __m256i *ga,
                                                __m256i *gb) {
  const __m256i t0 = _mm256_loadu_si256((const __m256i *)t);  // entries 0-3
  const __m256i t3 =
      _mm256_loadu_si256((const __m256i *)(t + 12));  // entries 3-6
  // Index 2 * n of entry n in t0 is 2 * n - 6 in t3 (mod 8).
  const __m256i six = _mm256_set1_epi32(6);
  *ga = _mm256_blendv_epi8(
      _mm256_permutevar8x32_epi32(t0, ia),
      _mm256_permutevar8x32_epi32(t3, _mm256_sub_epi32(ia, six)), ha);
  *gb = _mm256_blendv_epi8(
      _mm256_permutevar8x32_epi32(t0, ib),
      _mm256_permutevar8x32_epi32(t3, _mm256_sub_epi32(ib, six)), hb);
}

// 32-bit indices (2 * n, 2 * n + 1) of the entries n = nib[o] of the origins
// { 0, 1, 4, 5 } (ia) and { 2, 3, 6, 7 } (ib), and the masks of n >= 4.
static AVM_FORCE_INLINE void tcq_origin_index(__m256i nib, __m256i *ia,
                                              __m256i *ib, __m256i *ha,
                                              __m256i *hb) {
  const __m256i odd = _mm256_setr_epi32(0, 1, 0, 1, 0, 1, 0, 1);
  const __m256i three = _mm256_set1_epi32(3);
  const __m256i na = _mm256_permutevar8x32_epi32(
      nib, _mm256_setr_epi32(0, 0, 1, 1, 4, 4, 5, 5));
  const __m256i nb = _mm256_permutevar8x32_epi32(
      nib, _mm256_setr_epi32(2, 2, 3, 3, 6, 6, 7, 7));
  *ia = _mm256_add_epi32(_mm256_add_epi32(na, na), odd);
  *ib = _mm256_add_epi32(_mm256_add_epi32(nb, nb), odd);
  *ha = _mm256_cmpgt_epi32(na, three);
  *hb = _mm256_cmpgt_epi32(nb, three);
}

static AVM_FORCE_INLINE void tcq_tables_full(const uint16_t *low,
                                             const uint16_t *mid, __m256i nib,
                                             __m256i mnib, __m256i *teo0,
                                             __m256i *teo1, __m256i *neg_eo) {
  __m256i ia, ib, ha, hb;
  tcq_origin_index(nib, &ia, &ib, &ha, &hb);
  __m256i ga, gb;
  tcq_lookup_origins(low, ia, ib, ha, hb, &ga, &gb);
  __m128i n0 = _mm_loadl_epi64((const __m128i *)low);
  if (mid) {
    tcq_origin_index(mnib, &ia, &ib, &ha, &hb);
    __m256i ma, mb;
    tcq_lookup_origins(mid, ia, ib, ha, hb, &ma, &mb);
    // Each entry is at most one symbol plus one bit (below 2^14), so the sums
    // fit in 16 bits.
    ga = _mm256_add_epi16(ga, ma);
    gb = _mm256_add_epi16(gb, mb);
    n0 = _mm_add_epi16(n0, _mm_loadl_epi64((const __m128i *)mid));
  }
  *teo0 = _mm256_castps_si256(_mm256_shuffle_ps(_mm256_castsi256_ps(ga),
                                                _mm256_castsi256_ps(gb), 0x88));
  const __m256i hi = _mm256_castps_si256(_mm256_shuffle_ps(
      _mm256_castsi256_ps(ga), _mm256_castsi256_ps(gb), 0xDD));
  *teo1 = _mm256_or_si256(_mm256_slli_epi32(hi, 16), _mm256_srli_epi32(hi, 16));
  // n0 = { Q0 even | Q0 odd << 16, Q1 even | Q1 odd << 16 }.
  n0 = _mm_shufflelo_epi16(n0, 0xB4);
  const __m256i nb = _mm256_broadcastq_epi64(n0);
  *neg_eo = _mm256_permutevar8x32_epi32(
      nb, _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1));
}

// Process one diagonal, from scan_hi down to scan_lo. lf selects the
// low-frequency region tables; the DC position is not handled here.
static AVM_FORCE_INLINE void tcq_diagonal(
    const tcq_param_t *p, const TcqConsts *c, const TcqSegPos *d,
    const TcqDiagTables *t, int seg_lo, int scan_hi, int scan_lo, int col,
    int diag_ctx, const int lf, TcqRegs *s, tcq_ctx_t *tcq_ctx,
    tcq_node_t *trellis) {
  const LV_MAP_COEFF_COST *tc = p->txb_costs;
  const int try_eob = p->sharpness == 0;
  const int bd = get_base_diag_ctx(diag_ctx);
  const int md = get_mid_diag_ctx(diag_ctx);
  const __m128i nib_mask = _mm_set1_epi8(15);

  // Inputs of the general path, indexed by scan_hi - scan_pos.
  TcqFullPos full[MAX_DIAG];
  tcq_prepare_diagonal(p, d, seg_lo, scan_hi, scan_lo, lf, full);

  const __m256i inf = _mm256_set1_epi64x(INT64_MAX);
  for (int k = 0, i = scan_hi - seg_lo, scan_pos = scan_hi; scan_pos >= scan_lo;
       --scan_pos, ++k, --i, --col) {
    const __m128i cb = _mm_loadl_epi64((const __m128i *)tcq_ctx->ctx[col]);
    const __m256i nib = _mm256_cvtepu8_epi32(_mm_and_si128(cb, nib_mask));
    const int qidx = d->qidx[i];
    TcqCand cand;
    if (qidx == 0) {
      const __m256i dist_odd = _mm256_permute4x64_epi64(
          _mm256_castsi128_si256(_mm_load_si128((const __m128i *)d->dist[i])),
          0x50);
      tcq_cand_q1(s, c, _mm256_permutevar8x32_epi32(t->lq0, nib),
                  _mm256_permutevar8x32_epi32(t->lq1, nib), t->neg_q1, dist_odd,
                  &cand);
      tcq_select(
          s, &cand, try_eob,
          _mm256_blend_epi32(inf, _mm256_set1_epi64x(d->eob1_rd[i]), 0x0C),
          _mm256_set1_epi64x(d->eob1_info[i]));
    } else {
      const TcqFullPos *ps = &full[k];
      const uint16_t *mid;
      const uint16_t *low = tcq_full_tbl_ptrs(tc, lf, qidx, bd, md, &mid);
      __m256i mnib = _mm256_setzero_si256();
      if (mid) {
        mnib = _mm256_cvtepu8_epi32(
            _mm_and_si128(_mm_srli_epi16(cb, 4), nib_mask));
      }
      __m256i teo0, teo1, neg_eo;
      tcq_tables_full(low, mid, nib, mnib, &teo0, &teo1, &neg_eo);
      tcq_cand_full(s, c, teo0, teo1, neg_eo,
                    _mm256_permutevar8x32_epi32(t->lz, nib), t->neg_z, ps,
                    &cand);
      tcq_select(s, &cand, try_eob, ps->eob_rd, ps->eob_info);
    }
    tcq_update_orig(s, tcq_regs_hi(s), tcq_ctx->lev_new[col],
                    tcq_ctx->prev_st[col]);
    tcq_store_nodes(s, &trellis[scan_pos << TCQ_N_STATES_LOG]);
  }
}

void av2_trellis_loop_diagonal_st8_avx2(const tcq_param_t *p, int scan_hi,
                                        int scan_lo, tcq_ctx_t *tcq_ctx,
                                        tcq_node_t *trellis) {
  (void)scan_lo;
  tcq_trellis_loop(p, scan_hi, tcq_ctx, trellis);
}
