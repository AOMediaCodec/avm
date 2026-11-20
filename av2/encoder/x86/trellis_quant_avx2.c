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
#include <string.h>
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

// prev_st bytes are negative or 0..7; table bytes 8..15 are never read.
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
// 8-state trellis loop that keeps the trellis state in registers. The vectors
// "a" and "b" hold the states { 0, 4, 6, 2 } and { 1, 5, 7, 3 }, so each
// state shares a lane with its partner (state ^ 1).

typedef struct {
  __m256i rd_a, rd_b;
  __m256i info_a, info_b;  // rate | absLevel << 32 | prevId << 56
  __m256i orig;            // origin state of each lane, -1 for a new eob
} TcqRegs;

typedef struct {
  __m256i rdmult, mask16, mask32;
  __m256i pid_a, pid_b;    // prevId << 56 of each lane
  __m256i pid1_a, pid1_b;  // prevId << 56 | 1 << 32
} TcqConsts;

// State of each 32-bit lane, and lane of each state.
static const int32_t kLaneState[8] = { 0, 1, 4, 5, 6, 7, 2, 3 };
static const int32_t kStateLane[8] = { 0, 1, 6, 7, 2, 3, 4, 5 };
// Moves lev_new (byte 0) and prev_st (byte 1) of each lane to the bytes
// { lev 0-7, prev 0-7 }, in state order; the other bytes become zero.
static const int8_t kLevPrevShuf[32] = { 0,  4,  -1, -1, 8,  12, -1, -1,
                                         1,  5,  -1, -1, 9,  13, -1, -1,
                                         -1, -1, 8,  12, -1, -1, 0,  4,
                                         -1, -1, 9,  13, -1, -1, 1,  5 };

// Hides v from the optimizer, which would turn the ANDs with the 16-bit mask
// into word blends (port 5 only on Intel CPUs).
static AVM_FORCE_INLINE __m256i tcq_opaque(__m256i v) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__("" : "+x"(v));
#endif
  return v;
}

static AVM_FORCE_INLINE void tcq_init_consts(int64_t rdmult, TcqConsts *c) {
  // rdmult < 2^32 (asserted by the caller): zero-extending it lets the
  // compiler emit a single vpmuludq per rate.
  c->rdmult = _mm256_set1_epi64x((int64_t)(uint32_t)rdmult);
  c->mask16 = tcq_opaque(_mm256_set1_epi64x(0xFFFF));
  c->mask32 = _mm256_set1_epi64x(0xFFFFFFFF);
  c->pid_a = _mm256_setr_epi64x(0LL << 56, 4LL << 56, 6LL << 56, 2LL << 56);
  c->pid_b = _mm256_setr_epi64x(1LL << 56, 5LL << 56, 7LL << 56, 3LL << 56);
  const __m256i one = _mm256_set1_epi64x(1LL << 32);
  c->pid1_a = _mm256_add_epi64(c->pid_a, one);
  c->pid1_b = _mm256_add_epi64(c->pid_b, one);
}

static AVM_FORCE_INLINE void tcq_load_regs(const tcq_node_t *prev,
                                           const tcq_ctx_t *tcq_ctx,
                                           TcqRegs *s) {
  const __m256i n01 = _mm256_loadu_si256((const __m256i *)&prev[0]);
  const __m256i n23 = _mm256_loadu_si256((const __m256i *)&prev[2]);
  const __m256i n45 = _mm256_loadu_si256((const __m256i *)&prev[4]);
  const __m256i n67 = _mm256_loadu_si256((const __m256i *)&prev[6]);
  const __m256i n06 = _mm256_permute2x128_si256(n01, n67, 0x20);
  const __m256i n42 = _mm256_permute2x128_si256(n45, n23, 0x20);
  const __m256i n17 = _mm256_permute2x128_si256(n01, n67, 0x31);
  const __m256i n53 = _mm256_permute2x128_si256(n45, n23, 0x31);
  s->rd_a = _mm256_unpacklo_epi64(n06, n42);
  s->info_a = _mm256_unpackhi_epi64(n06, n42);
  s->rd_b = _mm256_unpacklo_epi64(n17, n53);
  s->info_b = _mm256_unpackhi_epi64(n17, n53);
  const __m256i orig =
      _mm256_cvtepi8_epi32(_mm_loadl_epi64((const __m128i *)tcq_ctx->orig_st));
  s->orig = _mm256_permutevar8x32_epi32(
      orig, _mm256_loadu_si256((const __m256i *)kLaneState));
}

static AVM_FORCE_INLINE void tcq_store_orig(const TcqRegs *s,
                                            tcq_ctx_t *tcq_ctx) {
  const __m256i orig = _mm256_permutevar8x32_epi32(
      s->orig, _mm256_loadu_si256((const __m256i *)kStateLane));
  const __m256i w = _mm256_packs_epi32(orig, orig);
  const __m256i b = _mm256_packs_epi16(w, w);
  const __m128i st = _mm_unpacklo_epi32(_mm256_castsi256_si128(b),
                                        _mm256_extracti128_si256(b, 1));
  _mm_storel_epi64((__m128i *)tcq_ctx->orig_st, st);
}

static AVM_FORCE_INLINE void tcq_store_nodes(const TcqRegs *s,
                                             tcq_node_t *decision) {
  const __m256i n06 = _mm256_unpacklo_epi64(s->rd_a, s->info_a);
  const __m256i n42 = _mm256_unpackhi_epi64(s->rd_a, s->info_a);
  const __m256i n17 = _mm256_unpacklo_epi64(s->rd_b, s->info_b);
  const __m256i n53 = _mm256_unpackhi_epi64(s->rd_b, s->info_b);
  _mm_storeu_si128((__m128i *)&decision[0], _mm256_castsi256_si128(n06));
  _mm_storeu_si128((__m128i *)&decision[1], _mm256_castsi256_si128(n17));
  _mm_storeu_si128((__m128i *)&decision[4], _mm256_castsi256_si128(n42));
  _mm_storeu_si128((__m128i *)&decision[5], _mm256_castsi256_si128(n53));
  _mm_storeu_si128((__m128i *)&decision[6], _mm256_extracti128_si256(n06, 1));
  _mm_storeu_si128((__m128i *)&decision[7], _mm256_extracti128_si256(n17, 1));
  _mm_storeu_si128((__m128i *)&decision[2], _mm256_extracti128_si256(n42, 1));
  _mm_storeu_si128((__m128i *)&decision[3], _mm256_extracti128_si256(n53, 1));
}

// Follows the new prevIds to update the origin states, and writes lev_new /
// prev_st for update_nbr_diagonal(). Call it once the new eob is decided.
static AVM_FORCE_INLINE void tcq_update_orig(TcqRegs *s, uint8_t *lev_new,
                                             int8_t *prev_st) {
  // absLevel | prevId << 24 of each state, in lane order.
  const __m256i hi = _mm256_or_si256(
      _mm256_srli_epi64(s->info_a, 32),
      _mm256_andnot_si256(_mm256_set1_epi64x(0xFFFFFFFF), s->info_b));
  const __m256i prev = _mm256_srai_epi32(hi, 24);
  const __m256i orig_by_state = _mm256_permutevar8x32_epi32(
      s->orig, _mm256_loadu_si256((const __m256i *)kStateLane));
  const __m256i orig = _mm256_permutevar8x32_epi32(orig_by_state, prev);
  s->orig = _mm256_blendv_epi8(orig, prev, prev);

  __m256i lev =
      _mm256_min_epi32(_mm256_and_si256(hi, _mm256_set1_epi32(0xFFFFFF)),
                       _mm256_set1_epi32(MAX_VAL_BR_CTX));
  lev = _mm256_or_si256(lev, _mm256_and_si256(_mm256_srli_epi32(hi, 16),
                                              _mm256_set1_epi32(0xFF00)));
  lev = _mm256_shuffle_epi8(lev,
                            _mm256_loadu_si256((const __m256i *)kLevPrevShuf));
  const __m128i lp = _mm_or_si128(_mm256_castsi256_si128(lev),
                                  _mm256_extracti128_si256(lev, 1));
  _mm_storel_epi64((__m128i *)lev_new, lp);
  _mm_storeh_pi((__m64 *)prev_st, _mm_castsi128_ps(lp));
}

static AVM_FORCE_INLINE __m256i tcq_rdc(__m256i rate, const TcqConsts *c) {
  return tcq_rate_cost(rate, c->rdmult);
}

// Each state keeps the cheaper of its even candidate and its partner's odd
// candidate (ties go to the even one), then moves to its next state.
// With try_eob, a new eob replaces state 0 / 4 when strictly cheaper.
static AVM_FORCE_INLINE void tcq_select(TcqRegs *s, __m256i ev_rd_a,
                                        __m256i ev_info_a, __m256i ev_rd_b,
                                        __m256i ev_info_b, __m256i od_rd_a,
                                        __m256i od_info_a, __m256i od_rd_b,
                                        __m256i od_info_b, int try_eob,
                                        __m256i eob_cand_rd,
                                        __m256i eob_cand_info) {
  const __m256i use_odd_a = _mm256_cmpgt_epi64(ev_rd_a, od_rd_b);
  const __m256i use_odd_b = _mm256_cmpgt_epi64(ev_rd_b, od_rd_a);
  const __m256i ra = _mm256_blendv_epi8(ev_rd_a, od_rd_b, use_odd_a);
  const __m256i rb = _mm256_blendv_epi8(ev_rd_b, od_rd_a, use_odd_b);
  const __m256i ia = _mm256_blendv_epi8(ev_info_a, od_info_b, use_odd_a);
  const __m256i ib = _mm256_blendv_epi8(ev_info_b, od_info_a, use_odd_b);
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

// Fetch the packed rates of each lane from the per-origin tables.
static AVM_FORCE_INLINE __m256i tcq_fetch(const TcqRegs *s, __m256i t0,
                                          __m256i t1, __m256i neg) {
  const __m256i v =
      _mm256_blend_epi32(_mm256_permutevar8x32_epi32(t0, s->orig),
                         _mm256_permutevar8x32_epi32(t1, s->orig), 0xF0);
  return _mm256_blendv_epi8(v, neg, s->orig);
}

// Position with orig_qIdx < 2: candidates are zero and the odd level 1.
static AVM_FORCE_INLINE void tcq_step_q1(TcqRegs *s, const TcqConsts *c,
                                         __m256i tq0, __m256i tq1, __m256i neg,
                                         __m256i dist_odd, int try_eob,
                                         __m256i eob_cand_rd,
                                         __m256i eob_cand_info) {
  const __m256i v = tcq_fetch(s, tq0, tq1, neg);
  const __m256i za = _mm256_and_si256(v, c->mask16);
  const __m256i oa = _mm256_and_si256(_mm256_srli_epi64(v, 16), c->mask16);
  const __m256i zb = _mm256_and_si256(_mm256_srli_epi64(v, 32), c->mask16);
  const __m256i ob = _mm256_srli_epi64(v, 48);
  const __m256i rate_a = _mm256_and_si256(s->info_a, c->mask32);
  const __m256i rate_b = _mm256_and_si256(s->info_b, c->mask32);

  const __m256i ev_rd_a = _mm256_add_epi64(s->rd_a, tcq_rdc(za, c));
  const __m256i ev_rd_b = _mm256_add_epi64(s->rd_b, tcq_rdc(zb, c));
  const __m256i ev_info_a =
      _mm256_add_epi64(_mm256_add_epi64(rate_a, za), c->pid_a);
  const __m256i ev_info_b =
      _mm256_add_epi64(_mm256_add_epi64(rate_b, zb), c->pid_b);
  const __m256i od_rd_a =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(oa, c), dist_odd));
  const __m256i od_rd_b =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(ob, c), dist_odd));
  const __m256i od_info_a =
      _mm256_add_epi64(_mm256_add_epi64(rate_a, oa), c->pid1_a);
  const __m256i od_info_b =
      _mm256_add_epi64(_mm256_add_epi64(rate_b, ob), c->pid1_b);
  tcq_select(s, ev_rd_a, ev_info_a, ev_rd_b, ev_info_b, od_rd_a, od_info_a,
             od_rd_b, od_info_b, try_eob, eob_cand_rd, eob_cand_info);
}

// Position with orig_qIdx >= 2: candidates are zero, one even and one odd
// level per quantizer.
static AVM_FORCE_INLINE void tcq_step_full(TcqRegs *s, const TcqConsts *c,
                                           __m256i teo0, __m256i teo1,
                                           __m256i neg_eo, __m256i tz,
                                           __m256i neg_z, const TcqFullPos *pos,
                                           int try_eob) {
  const __m256i gol_even = pos->gol_even;
  const __m256i gol_odd = pos->gol_odd;
  const __m256i dist_even = pos->dist_even;
  const __m256i dist_odd = pos->dist_odd;
  const __m256i info_even = pos->info_even;
  const __m256i info_odd = pos->info_odd;
  // Zero rates are packed as Q0 | Q1 << 16; lanes 2 and 3 use Q1.
  const __m256i z_shift_a = _mm256_setr_epi64x(0, 0, 16, 16);
  const __m256i z_shift_b = _mm256_setr_epi64x(32, 32, 48, 48);

  const __m256i v = tcq_fetch(s, teo0, teo1, neg_eo);
  const __m256i vz = _mm256_blendv_epi8(
      _mm256_permutevar8x32_epi32(tz, s->orig), neg_z, s->orig);
  const __m256i ea = _mm256_add_epi64(_mm256_and_si256(v, c->mask16), gol_even);
  const __m256i oa = _mm256_add_epi64(
      _mm256_and_si256(_mm256_srli_epi64(v, 16), c->mask16), gol_odd);
  const __m256i eb = _mm256_add_epi64(
      _mm256_and_si256(_mm256_srli_epi64(v, 32), c->mask16), gol_even);
  const __m256i ob = _mm256_add_epi64(_mm256_srli_epi64(v, 48), gol_odd);
  const __m256i za =
      _mm256_and_si256(_mm256_srlv_epi64(vz, z_shift_a), c->mask16);
  const __m256i zb =
      _mm256_and_si256(_mm256_srlv_epi64(vz, z_shift_b), c->mask16);
  const __m256i rate_a = _mm256_and_si256(s->info_a, c->mask32);
  const __m256i rate_b = _mm256_and_si256(s->info_b, c->mask32);

  // Even candidate: the cheaper of zero and the even level (ties: level).
  const __m256i rd_ea =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(ea, c), dist_even));
  const __m256i rd_eb =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(eb, c), dist_even));
  const __m256i rd_za = _mm256_add_epi64(s->rd_a, tcq_rdc(za, c));
  const __m256i rd_zb = _mm256_add_epi64(s->rd_b, tcq_rdc(zb, c));
  const __m256i use_zero_a = _mm256_cmpgt_epi64(rd_ea, rd_za);
  const __m256i use_zero_b = _mm256_cmpgt_epi64(rd_eb, rd_zb);
  const __m256i info_ea = _mm256_add_epi64(
      _mm256_add_epi64(rate_a, ea), _mm256_add_epi64(info_even, c->pid_a));
  const __m256i info_eb = _mm256_add_epi64(
      _mm256_add_epi64(rate_b, eb), _mm256_add_epi64(info_even, c->pid_b));
  const __m256i info_za =
      _mm256_add_epi64(_mm256_add_epi64(rate_a, za), c->pid_a);
  const __m256i info_zb =
      _mm256_add_epi64(_mm256_add_epi64(rate_b, zb), c->pid_b);
  const __m256i ev_rd_a = _mm256_blendv_epi8(rd_ea, rd_za, use_zero_a);
  const __m256i ev_rd_b = _mm256_blendv_epi8(rd_eb, rd_zb, use_zero_b);
  const __m256i ev_info_a = _mm256_blendv_epi8(info_ea, info_za, use_zero_a);
  const __m256i ev_info_b = _mm256_blendv_epi8(info_eb, info_zb, use_zero_b);

  const __m256i od_rd_a =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(oa, c), dist_odd));
  const __m256i od_rd_b =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(ob, c), dist_odd));
  const __m256i od_info_a = _mm256_add_epi64(
      _mm256_add_epi64(rate_a, oa), _mm256_add_epi64(info_odd, c->pid_a));
  const __m256i od_info_b = _mm256_add_epi64(
      _mm256_add_epi64(rate_b, ob), _mm256_add_epi64(info_odd, c->pid_b));
  tcq_select(s, ev_rd_a, ev_info_a, ev_rd_b, ev_info_b, od_rd_a, od_info_a,
             od_rd_b, od_info_b, try_eob, pos->eob_rd, pos->eob_info);
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

// -----------------------------------------------------------------------------
// DC position (scan_pos 0): the per-position steps of the C loop in AVX2,
// specialized for the DC coefficient of a 2D luma block.

// tcq_dc_decide*() constants.
static const int32_t kDcShuffle[8] = { 0, 2, 1, 3, 5, 7, 4, 6 };
static const int32_t kDcShuffleOdd[8] = { 3, 1, 6, 4, 0, 0, 0, 0 };
static const int32_t kDcPrevId[TCQ_MAX_STATES / 4][8] = {
  { 0, 0 << 24, 0, 1 << 24, 0, 2 << 24, 0, 3 << 24 },
  { 0, 4 << 24, 0, 5 << 24, 0, 6 << 24, 0, 7 << 24 },
};
static const int32_t kDcPrevIdQ1[2][2][8] = {
  {
      { 0, 0 << 24, 0, 1 << 24, 0, 2 << 24, 0, 3 << 24 },
      { 0, 1 << 24, 0, 0 << 24, 0, 3 << 24, 0, 2 << 24 },
  },
  {
      { 0, 4 << 24, 0, 5 << 24, 0, 6 << 24, 0, 7 << 24 },
      { 0, 5 << 24, 0, 4 << 24, 0, 7 << 24, 0, 6 << 24 },
  },
};
// Shuffles of tcq_dc_rate_zero().
static const int8_t kDcZeroShuf[2][32] = {
  { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15,
    0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15 },
  { 0, 8,  -1, -1, 1, 9,  -1, -1, 2, 10, -1, -1, 3, 11, -1, -1,
    4, 12, -1, -1, 5, 13, -1, -1, 6, 14, -1, -1, 7, 15, -1, -1 }
};

// av2_pre_quant_c() of the DC coefficient (orig_qIdx >= 2). Unlike the C
// code, deltaDist holds the distortion changes << RDDIV_BITS.
static AVM_FORCE_INLINE void tcq_dc_pre_quant(tran_low_t tqc, prequant_t *pq,
                                              int dqv, int log_scale) {
  pq->qIdx = AVMMAX(pq->orig_qIdx - 2, 1);
  const int32_t qIdx = pq->qIdx;

  const __m128i qc_idx =
      _mm_add_epi32(_mm_set1_epi32(qIdx),
                    _mm_lddqu_si128((const __m128i *)kTcqQcInc[qIdx & 3]));
  const __m128i abslev =
      _mm_srli_epi32(_mm_add_epi32(qc_idx, _mm_set1_epi32(1)), 1);
  _mm_storeu_si128((__m128i *)pq->absLevel, abslev);

  const __m256i dist =
      tcq_dist_coef(_mm256_cvtepu32_epi64(qc_idx), abs(tqc), dqv, log_scale);
  _mm256_storeu_si256((__m256i *)pq->deltaDist, dist);
}

// av2_pre_quant_q1_c() of the DC coefficient (orig_qIdx < 2). Unlike the C
// code, deltaDist holds the distortion changes << RDDIV_BITS.
static AVM_FORCE_INLINE void tcq_dc_pre_quant_q1(tran_low_t tqc, prequant_t *pq,
                                                 int dqv, int log_scale) {
  const int abs_tc = abs(tqc);
  pq->qIdx = 1;
  pq->absLevel[1] = 1;
  pq->absLevel[2] = 1;
  pq->deltaDist[1] = tcq_dist_q1(abs_tc, dqv, 1, log_scale) * (1 << RDDIV_BITS);
  pq->deltaDist[2] = tcq_dist_q1(abs_tc, dqv, 2, log_scale) * (1 << RDDIV_BITS);
}

// Zero rates of the 8 states; returns their base contexts.
static AVM_FORCE_INLINE __m256i
tcq_dc_rate_zero(const LV_MAP_COEFF_COST *txb_costs, int base_diag_ctx,
                 const tcq_coeff_ctx_t *coeff_ctx, tcq_rate_t *rd) {
  // The 16-byte loads below read contexts base_diag_ctx to base_diag_ctx + 15.
  assert(base_diag_ctx >= 0 && base_diag_ctx + 16 <= LF_SIG_COEF_CONTEXTS);
  const uint16_t(*cost_zero)[LF_SIG_COEF_CONTEXTS] =
      txb_costs->base_lf_cost_zero;
  const __m256i shuf = _mm256_lddqu_si256((const __m256i *)kDcZeroShuf[0]);
  const __m256i cost_dq0 = _mm256_permute4x64_epi64(
      _mm256_shuffle_epi8(
          _mm256_lddqu_si256((const __m256i *)&cost_zero[0][base_diag_ctx]),
          shuf),
      0xD8);
  const __m256i cost_dq1 = _mm256_permute4x64_epi64(
      _mm256_shuffle_epi8(
          _mm256_lddqu_si256((const __m256i *)&cost_zero[1][base_diag_ctx]),
          shuf),
      0xD8);
  // Base contexts of the 8 states, in every 64-bit lane.
  const __m256i ctx = _mm256_broadcastq_epi64(_mm_loadu_si64(&coeff_ctx->coef));
  const __m256i base_ctx = _mm256_and_si256(ctx, _mm256_set1_epi8(15));
  __m256i ratez =
      _mm256_blend_epi16(_mm256_shuffle_epi8(cost_dq0, base_ctx),
                         _mm256_shuffle_epi8(cost_dq1, base_ctx), 0xAA);
  ratez = _mm256_permute4x64_epi64(ratez, 0x88);
  ratez = _mm256_shuffle_epi8(
      ratez, _mm256_lddqu_si256((const __m256i *)kDcZeroShuf[1]));
  _mm256_storeu_si256((__m256i *)&rd->rate_zero[0], ratez);
  return _mm256_add_epi8(base_ctx, _mm256_set1_epi8(base_diag_ctx));
}

// Base rates of the 8 states.
static AVM_FORCE_INLINE void tcq_dc_rate_base(
    const uint16_t (*cost_low)[TCQ_CTXS][2], __m256i base_ctx, __m128i add,
    tcq_rate_t *rd) {
  const __m128i zero = _mm_setzero_si128();
  for (int i = 0; i < (TCQ_N_STATES >> 2); i++) {
    const int ctx0 = _mm256_extract_epi8(base_ctx, 0);
    const int ctx1 = _mm256_extract_epi8(base_ctx, 1);
    const int ctx2 = _mm256_extract_epi8(base_ctx, 2);
    const int ctx3 = _mm256_extract_epi8(base_ctx, 3);
    base_ctx = _mm256_bsrli_epi128(base_ctx, 4);
    const __m128i rate_01 = _mm_loadu_si64(&cost_low[ctx0][0]);
    const __m128i rate_23 = _mm_loadu_si64(&cost_low[ctx1][0]);
    const __m128i rate_45 = _mm_loadu_si64(&cost_low[ctx2][1]);
    const __m128i rate_67 = _mm_loadu_si64(&cost_low[ctx3][1]);
    const __m128i rate_0123 = _mm_add_epi32(
        _mm_unpacklo_epi16(_mm_unpacklo_epi32(rate_01, rate_23), zero), add);
    const __m128i rate_4567 = _mm_add_epi32(
        _mm_unpacklo_epi16(_mm_unpacklo_epi32(rate_45, rate_67), zero), add);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i], rate_0123);
    _mm_storeu_si128((__m128i *)&rd->rate[8 * i + 4], rate_4567);
  }
}

// Rates of the eob candidates from the base table idx, plus add.
static AVM_FORCE_INLINE void tcq_dc_rate_eob(const LV_MAP_COEFF_COST *txb_costs,
                                             int idx, int eob_ctx, int eob_rate,
                                             __m128i add, tcq_rate_t *rd) {
  const __m128i rate_eob = _mm_add_epi32(
      tcq_eob_rate(txb_costs->base_lf_eob_cost_tbl[idx][eob_ctx], eob_rate),
      add);
  _mm_storeu_si64(&rd->rate_eob[0], rate_eob);
}

// DC sign cost minus the literal sign cost, like dc_cost_add in the C code.
static AVM_FORCE_INLINE int tcq_dc_sign_cost(const tcq_param_t *p, int sign) {
  const int dc_ph_group = 0;  // PH disabled
  return p->txb_costs->dc_sign_cost[dc_ph_group][p->dc_sign_ctx][sign] -
         av2_cost_literal(1);
}

// Sign and mid range cost of a DC level.
static AVM_FORCE_INLINE int tcq_dc_mid_cost(
    tran_low_t abs_qc, int sign_cost, int mid_ctx,
    const LV_MAP_COEFF_COST *txb_costs) {
  int cost = sign_cost;
  if (abs_qc > LF_NUM_BASE_LEVELS)
    cost += get_br_lf_cost_tcq(abs_qc, txb_costs->lps_lf_cost[mid_ctx]);
  return cost;
}

// av2_get_rate_dist_lf_luma_c() of the DC coefficient (orig_qIdx >= 2).
static AVM_FORCE_INLINE void tcq_dc_rate(const tcq_param_t *p,
                                         const prequant_t *pq,
                                         const tcq_coeff_ctx_t *coeff_ctx,
                                         int diag_ctx, int eob_rate,
                                         int coeff_sign, tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const tran_low_t *absLevel = pq->absLevel;
  // Loaded once: the stores to rd->rate below may alias the cost tables.
  const int sign_cost = tcq_dc_sign_cost(p, coeff_sign);
  const __m256i base_ctx =
      tcq_dc_rate_zero(txb_costs, get_base_diag_ctx(diag_ctx), coeff_ctx, rd);
  const int idx = AVMMIN(pq->qIdx - 1, 8);
  const __m128i zero = _mm_setzero_si128();
  tcq_dc_rate_base(txb_costs->base_lf_cost_low_tbl[idx], base_ctx, zero, rd);
  tcq_dc_rate_eob(txb_costs, idx, coeff_ctx->coef_eob, eob_rate, zero, rd);
  // The per-state sign and mid range costs are added in scalar code.
  for (int i = 0; i < TCQ_N_STATES; i++) {
    const int a0 = i & 2 ? 1 : 0;
    const int mid_ctx = coeff_ctx->coef[i] >> 4;
    rd->rate[2 * i] +=
        tcq_dc_mid_cost(absLevel[a0], sign_cost, mid_ctx, txb_costs);
    rd->rate[2 * i + 1] +=
        tcq_dc_mid_cost(absLevel[a0 + 2], sign_cost, mid_ctx, txb_costs);
  }
  const int br_ctx_eob = get_br_ctx_lf_eob(0, TX_CLASS_2D);
  rd->rate_eob[0] +=
      tcq_dc_mid_cost(absLevel[0], sign_cost, br_ctx_eob, txb_costs);
  rd->rate_eob[1] +=
      tcq_dc_mid_cost(absLevel[2], sign_cost, br_ctx_eob, txb_costs);
}

// av2_get_rate_dist_lf_luma_q1_c() of the DC coefficient (orig_qIdx < 2).
static AVM_FORCE_INLINE void tcq_dc_rate_q1(const tcq_param_t *p,
                                            const tcq_coeff_ctx_t *coeff_ctx,
                                            int diag_ctx, int eob_rate,
                                            int coeff_sign, tcq_rate_t *rd) {
  const LV_MAP_COEFF_COST *txb_costs = p->txb_costs;
  const __m256i base_ctx =
      tcq_dc_rate_zero(txb_costs, get_base_diag_ctx(diag_ctx), coeff_ctx, rd);
  // Level 1 has no mid range, so every state adds the same sign cost.
  const __m128i dc_cost = _mm_set1_epi32(tcq_dc_sign_cost(p, coeff_sign));
  tcq_dc_rate_base(txb_costs->base_lf_cost_low_tbl[0], base_ctx, dc_cost, rd);
  tcq_dc_rate_eob(txb_costs, 0, coeff_ctx->coef_eob, eob_rate, dc_cost, rd);
}

// Costs (rd) and rates (rate) of the 4 previous states prev[0] to prev[3], in
// 64-bit lanes.
static AVM_FORCE_INLINE void tcq_dc_load_prev(const tcq_node_t *prev,
                                              __m256i *rd, __m256i *rate) {
  const __m256i state01 = _mm256_lddqu_si256((const __m256i *)&prev[0]);
  const __m256i state23 = _mm256_lddqu_si256((const __m256i *)&prev[2]);
  const __m256i state02 = _mm256_permute2x128_si256(state01, state23, 0x20);
  const __m256i state13 = _mm256_permute2x128_si256(state01, state23, 0x31);
  *rd = _mm256_unpacklo_epi64(state02, state13);
  *rate = _mm256_and_si256(_mm256_unpackhi_epi64(state02, state13),
                           _mm256_set1_epi64x(0xFFFFFFFFULL));
}

// tcq_dc_decide*() write an ordinary node for every state. When the previous
// states of a state are unreachable, its cost is then above INT64_MAX >> 10,
// whereas the C code leaves { INT64_MAX >> 10, 0, -1, -2 }. This does not
// change the result: the best path starts at the cheapest state, and tcq_ctx
// is not read after the DC position.

// av2_decide_states_c() at the DC position (orig_qIdx >= 2).
static AVM_FORCE_INLINE void tcq_dc_decide(const tcq_node_t *prev,
                                           const tcq_rate_t *rd,
                                           const prequant_t *pq, int try_eob,
                                           int64_t rdmult,
                                           tcq_node_t *decision) {
  static_assert(sizeof(tcq_node_t) == 16, "");
  // rdmult < 2^32, zero-extended for tcq_rate_cost().
  const __m256i c_rdmult = _mm256_set1_epi64x((int64_t)(uint32_t)rdmult);
  const __m256i c_zero = _mm256_setzero_si256();

  // Absolute levels of the 4 quantization candidates.
  const __m128i abslev0123 = _mm_lddqu_si128((const __m128i *)pq->absLevel);
  const __m256i abslev0231 =
      _mm256_castsi128_si256(_mm_shuffle_epi32(abslev0123, 0x78));
  const __m256i abslev02023131 = _mm256_permute4x64_epi64(abslev0231, 0x50);
  const __m256i abslev00223311 = _mm256_shuffle_epi32(abslev02023131, 0x50);
  const __m256i abslev0033 = _mm256_unpacklo_epi32(c_zero, abslev00223311);
  const __m256i abslev2211 = _mm256_unpackhi_epi32(c_zero, abslev00223311);

  // pq->deltaDist is already << RDDIV_BITS.
  const __m256i dist = _mm256_lddqu_si256((const __m256i *)&pq->deltaDist[0]);
  const __m256i dist0033 = _mm256_permute4x64_epi64(dist, 0xF0);
  const __m256i dist2211 = _mm256_permute4x64_epi64(dist, 0x5A);

  // Rates and costs of the new eob candidates (states 0 and 4).
  const __m256i rate_eob = _mm256_cvtepu32_epi64(_mm_loadu_si64(rd->rate_eob));
  const __m256i rdcost_eob =
      _mm256_add_epi64(tcq_rate_cost(rate_eob, c_rdmult),
                       _mm256_unpacklo_epi64(dist0033, dist2211));

  __m256i *out_a = (__m256i *)&decision[0];
  __m256i *out_b = (__m256i *)&decision[TCQ_N_STATES >> 1];
  for (int i = 0; i < TCQ_N_STATES >> 2; i++) {
    // Costs of the even and odd candidates.
    const __m256i rates = _mm256_load_si256((const __m256i *)&rd->rate[8 * i]);
    const __m256i rate02135746 = _mm256_permutevar8x32_epi32(
        rates, _mm256_lddqu_si256((const __m256i *)kDcShuffle));
    __m256i rate0257 = _mm256_unpacklo_epi32(rate02135746, c_zero);
    __m256i rate1346 = _mm256_unpackhi_epi32(rate02135746, c_zero);
    __m256i rdcost0257 = tcq_rate_cost(rate0257, c_rdmult);
    __m256i rdcost1346 = tcq_rate_cost(rate1346, c_rdmult);
    rdcost0257 = _mm256_add_epi64(rdcost0257, dist0033);
    rdcost1346 = _mm256_add_epi64(rdcost1346, dist2211);

    // Rate-distortion cost of the zero candidate.
    __m256i ratezero = _mm256_castsi128_si256(
        _mm_load_si128((const __m128i *)&rd->rate_zero[4 * i]));
    ratezero = _mm256_permute4x64_epi64(ratezero, 0x50);
    ratezero = _mm256_unpacklo_epi32(ratezero, c_zero);
    const __m256i rdcostzero = tcq_rate_cost(ratezero, c_rdmult);

    // Costs and rates of the previous states.
    __m256i prevrd0123, prevrate0123;
    tcq_dc_load_prev(&prev[4 * i], &prevrd0123, &prevrate0123);

    // Zero vs even.
    const __m256i use_zero = _mm256_cmpgt_epi64(rdcost0257, rdcostzero);
    rdcost0257 = _mm256_blendv_epi8(rdcost0257, rdcostzero, use_zero);
    rate0257 = _mm256_blendv_epi8(rate0257, ratezero, use_zero);
    const __m256i abslev_even = _mm256_andnot_si256(use_zero, abslev0033);

    rdcost0257 = _mm256_add_epi64(rdcost0257, prevrd0123);
    rdcost1346 = _mm256_add_epi64(rdcost1346, prevrd0123);
    rate0257 = _mm256_add_epi64(rate0257, prevrate0123);
    rate1346 = _mm256_add_epi64(rate1346, prevrate0123);

    // Even vs odd.
    const __m256i rdcost3164 = _mm256_shuffle_epi32(rdcost1346, 0x4E);
    const __m256i rate3164 = _mm256_shuffle_epi32(rate1346, 0x4E);
    const __m256i use_odd = _mm256_cmpgt_epi64(rdcost0257, rdcost3164);
    __m256i prev_id =
        _mm256_xor_si256(_mm256_lddqu_si256((const __m256i *)kDcPrevId[i]),
                         _mm256_slli_epi64(_mm256_srli_epi64(use_odd, 63), 56));
    __m256i rdcost_best = _mm256_blendv_epi8(rdcost0257, rdcost3164, use_odd);
    __m256i rate_best = _mm256_blendv_epi8(rate0257, rate3164, use_odd);
    __m256i abslev_best = _mm256_blendv_epi8(abslev_even, abslev2211, use_odd);

    // Best vs new eob (states 0 and 4, first iteration only).
    const __m256i mask_eob =
        _mm256_inserti128_si256(c_zero, _mm_set1_epi64x((int64_t)-try_eob), 0);
    const __m256i use_eob =
        _mm256_and_si256(_mm256_cmpgt_epi64(rdcost_best, rdcost_eob), mask_eob);
    prev_id = _mm256_or_si256(prev_id, _mm256_slli_epi64(use_eob, 56));
    rdcost_best = _mm256_blendv_epi8(rdcost_best, rdcost_eob, use_eob);
    rate_best = _mm256_blendv_epi8(rate_best, rate_eob, use_eob);
    abslev_best = _mm256_blendv_epi8(
        abslev_best, _mm256_unpacklo_epi64(abslev0033, abslev2211), use_eob);
    try_eob = 0;

    const __m256i info_best =
        _mm256_or_si256(_mm256_or_si256(rate_best, abslev_best), prev_id);
    _mm256_storeu_si256(out_a, _mm256_unpacklo_epi64(rdcost_best, info_best));
    _mm256_storeu_si256(out_b, _mm256_unpackhi_epi64(rdcost_best, info_best));
    out_a = (__m256i *)&decision[6];
    out_b = (__m256i *)&decision[2];
  }
}

// av2_decide_states_q1_c() at the DC position (orig_qIdx < 2).
static AVM_FORCE_INLINE void tcq_dc_decide_q1(const tcq_node_t *prev,
                                              const tcq_rate_t *rd,
                                              const prequant_t *pq, int try_eob,
                                              int64_t rdmult,
                                              tcq_node_t *decision) {
  // rdmult < 2^32, zero-extended for tcq_rate_cost().
  const __m256i c_rdmult = _mm256_set1_epi64x((int64_t)(uint32_t)rdmult);
  // pq->deltaDist is already << RDDIV_BITS.
  const __m256i dist_2211 = _mm256_permute4x64_epi64(
      _mm256_castsi128_si256(
          _mm_lddqu_si128((const __m128i *)&pq->deltaDist[1])),
      0x05);

  __m256i *out_a = (__m256i *)&decision[0];
  __m256i *out_b = (__m256i *)&decision[TCQ_N_STATES >> 1];
  for (int i = 0; i < TCQ_N_STATES >> 2; i++) {
    // Costs and rates of the previous states.
    __m256i prev_rd_zero, prev_rate_zero;
    tcq_dc_load_prev(&prev[4 * i], &prev_rd_zero, &prev_rate_zero);
    const __m256i prev_rd_odd = _mm256_shuffle_epi32(prev_rd_zero, 0x4E);
    const __m256i prev_rate_odd = _mm256_shuffle_epi32(prev_rate_zero, 0x4E);

    // Zero candidate.
    const __m256i ratezero = _mm256_cvtepu32_epi64(
        _mm_load_si128((const __m128i *)&rd->rate_zero[4 * i]));
    const __m256i rdcost_zero =
        _mm256_add_epi64(tcq_rate_cost(ratezero, c_rdmult), prev_rd_zero);

    // Odd candidate.
    const __m256i rate_odd_32 = _mm256_permutevar8x32_epi32(
        _mm256_load_si256((const __m256i *)&rd->rate[8 * i]),
        _mm256_lddqu_si256((const __m256i *)kDcShuffleOdd));
    const __m256i rate_odd =
        _mm256_cvtepu32_epi64(_mm256_castsi256_si128(rate_odd_32));
    const __m256i rdcost_odd = _mm256_add_epi64(
        _mm256_add_epi64(tcq_rate_cost(rate_odd, c_rdmult), dist_2211),
        prev_rd_odd);

    // Zero vs odd.
    const __m256i use_odd = _mm256_cmpgt_epi64(rdcost_zero, rdcost_odd);
    __m256i rdcost_best = _mm256_blendv_epi8(rdcost_zero, rdcost_odd, use_odd);
    __m256i rate_best =
        _mm256_blendv_epi8(_mm256_add_epi64(ratezero, prev_rate_zero),
                           _mm256_add_epi64(rate_odd, prev_rate_odd), use_odd);
    __m256i prev_id = _mm256_blendv_epi8(
        _mm256_lddqu_si256((const __m256i *)kDcPrevIdQ1[i][0]),
        _mm256_lddqu_si256((const __m256i *)kDcPrevIdQ1[i][1]), use_odd);
    __m256i abs_best =
        _mm256_and_si256(use_odd, _mm256_set1_epi64x(1ULL << 32));

    // New eob candidate of state 4 (first iteration only).
    if (try_eob && i == 0) {
      const int rate_eob1 = rd->rate_eob[1];
      const int64_t rdcost_eob1 =
          ROUND_POWER_OF_TWO_64((int64_t)rdmult * rate_eob1,
                                AV2_PROB_COST_SHIFT) +
          pq->deltaDist[2];
      const __m256i v_rdcost_eob = _mm256_set_epi64x(0, 0, rdcost_eob1, 0);
      const __m256i use_eob =
          _mm256_and_si256(_mm256_cmpgt_epi64(rdcost_best, v_rdcost_eob),
                           _mm256_set_epi64x(0, 0, (int64_t)-1, 0));
      rdcost_best = _mm256_blendv_epi8(rdcost_best, v_rdcost_eob, use_eob);
      rate_best = _mm256_blendv_epi8(
          rate_best, _mm256_set_epi64x(0, 0, rate_eob1, 0), use_eob);
      prev_id = _mm256_blendv_epi8(
          prev_id, _mm256_set_epi64x(0, 0, 0xff00000000000000, 0), use_eob);
      abs_best = _mm256_blendv_epi8(
          abs_best, _mm256_set_epi64x(0, 0, 1ULL << 32, 0), use_eob);
    }

    const __m256i info_best =
        _mm256_or_si256(_mm256_or_si256(rate_best, abs_best), prev_id);
    _mm256_storeu_si256(out_a, _mm256_unpacklo_epi64(rdcost_best, info_best));
    _mm256_storeu_si256(out_b, _mm256_unpackhi_epi64(rdcost_best, info_best));
    out_a = (__m256i *)&decision[6];
    out_b = (__m256i *)&decision[2];
  }
}

// av2_update_states_c() at the DC position (column 0).
static AVM_FORCE_INLINE void tcq_dc_update_states(const tcq_node_t *decision,
                                                  tcq_ctx_t *tcq_ctx) {
  // prevId and absLevel of the 8 states.
  const __m256i dec01 =
      _mm256_srli_si256(_mm256_lddqu_si256((const __m256i *)&decision[0]), 12);
  const __m256i dec23 =
      _mm256_srli_si256(_mm256_lddqu_si256((const __m256i *)&decision[2]), 12);
  const __m256i dec45 =
      _mm256_srli_si256(_mm256_lddqu_si256((const __m256i *)&decision[4]), 12);
  const __m256i dec67 =
      _mm256_srli_si256(_mm256_lddqu_si256((const __m256i *)&decision[6]), 12);
  const __m256i dec02461357 = _mm256_unpacklo_epi64(
      _mm256_unpacklo_epi32(dec01, dec23), _mm256_unpacklo_epi32(dec45, dec67));
  const __m256i abs02461357 =
      _mm256_min_epi32(_mm256_srli_epi32(_mm256_slli_epi32(dec02461357, 8), 8),
                       _mm256_set1_epi32(MAX_VAL_BR_CTX));
  const __m256i abs1357 =
      _mm256_slli_epi32(_mm256_permute4x64_epi64(abs02461357, 0xEE), 16);
  __m128i abs_lev =
      _mm256_castsi256_si128(_mm256_or_si256(abs02461357, abs1357));
  abs_lev = _mm_packus_epi16(abs_lev, abs_lev);
  const __m256i prev02461357 = _mm256_srai_epi32(dec02461357, 24);
  const __m256i prev1357 =
      _mm256_slli_epi32(_mm256_permute4x64_epi64(prev02461357, 0xEE), 16);
  __m128i prev_st =
      _mm256_castsi256_si128(_mm256_blend_epi16(prev02461357, prev1357, 0xAA));
  prev_st = _mm_packs_epi16(prev_st, prev_st);
  _mm_storeu_si64(tcq_ctx->lev_new[0], abs_lev);
  _mm_storeu_si64(tcq_ctx->prev_st[0], prev_st);
  _mm_storeu_si64(tcq_ctx->orig_st,
                  map_state(_mm_loadu_si64(tcq_ctx->orig_st), prev_st));
}

static void tcq_dc_position(const tcq_param_t *p, tcq_ctx_t *tcq_ctx,
                            tcq_node_t *trellis) {
  const int log_scale = p->log_scale;
  const int try_eob = p->sharpness == 0;
  const tran_low_t *tcoeff = p->tcoeff;
  const int shift = 16 - log_scale + QUANT_FP_BITS;
  const int dc_coeff_sign = tcoeff[0] < 0;
  const int diag_ctx = get_diag_ctx(1, 0, 0, p->bwl);
  tcq_node_t *decision = trellis;
  const tcq_node_t *prev_decision = &decision[TCQ_N_STATES];
  prequant_t pq_data;
  const int temp_dqv = get_dqv(p->dequant, p->scan[0], p->iqmatrix);
  pq_data.orig_qIdx =
      (int16_t)(((int64_t)abs(tcoeff[0]) * p->quant[0]) >> shift);
  // Base / mid contexts of each state at column 0, like av2_get_coeff_ctx_c().
  // Zeroed first: the static analyzer does not see the 8-byte store fill coef.
  tcq_coeff_ctx_t coeff_ctx = { { 0 }, 0, { 0 } };
  _mm_storeu_si64(coeff_ctx.coef,
                  _mm_shuffle_epi8(_mm_loadu_si64(tcq_ctx->ctx[0]),
                                   _mm_loadu_si64(tcq_ctx->orig_st)));
  coeff_ctx.coef_eob = get_lower_levels_ctx_eob(p->bwl, p->txb_height, 0);
  const int eob_rate = p->block_eob_rate[0];
  tcq_rate_t rd;
  if (pq_data.orig_qIdx < 2) {
    tcq_dc_pre_quant_q1(tcoeff[0], &pq_data, temp_dqv, log_scale);
    tcq_dc_rate_q1(p, &coeff_ctx, diag_ctx, eob_rate, dc_coeff_sign, &rd);
    tcq_dc_decide_q1(prev_decision, &rd, &pq_data, try_eob, p->rdmult,
                     decision);
  } else {
    tcq_dc_pre_quant(tcoeff[0], &pq_data, temp_dqv, log_scale);
    tcq_dc_rate(p, &pq_data, &coeff_ctx, diag_ctx, eob_rate, dc_coeff_sign,
                &rd);
    tcq_dc_decide(prev_decision, &rd, &pq_data, try_eob, p->rdmult, decision);
  }
  tcq_dc_update_states(decision, tcq_ctx);
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

  assert(scan_hi - scan_lo < MAX_DIAG);
  assert(scan_lo >= seg_lo && scan_hi - seg_lo < TCQ_PREP_SEG);
  // Inputs of the general path, indexed by scan_hi - scan_pos.
  TcqFullPos full[MAX_DIAG];
  const int32_t *seg_qidx = &d->qidx[scan_lo - seg_lo];
  TcqFullConsts fc;
  tcq_full_consts(p, &fc);
  for (int k = 0, scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos, ++k) {
    const int qidx = seg_qidx[scan_pos - scan_lo];
    if (qidx == 0) continue;
    tcq_prepare_full(&fc, d, scan_pos - seg_lo, scan_pos, lf, qidx, &full[k]);
  }

  const __m256i inf = _mm256_set1_epi64x(INT64_MAX);
  for (int k = 0, i = scan_hi - seg_lo, scan_pos = scan_hi; scan_pos >= scan_lo;
       --scan_pos, ++k, --i, --col) {
    const __m128i cb = _mm_loadl_epi64((const __m128i *)tcq_ctx->ctx[col]);
    const __m256i nib = _mm256_cvtepu8_epi32(_mm_and_si128(cb, nib_mask));
    const int qidx = d->qidx[i];
    if (qidx == 0) {
      const __m256i dist_odd = _mm256_permute4x64_epi64(
          _mm256_castsi128_si256(_mm_load_si128((const __m128i *)d->dist[i])),
          0x50);
      tcq_step_q1(
          s, c, _mm256_permutevar8x32_epi32(t->lq0, nib),
          _mm256_permutevar8x32_epi32(t->lq1, nib), t->neg_q1, dist_odd,
          try_eob,
          _mm256_blend_epi32(inf, _mm256_set1_epi64x(d->eob1_rd[i]), 0x0C),
          _mm256_set1_epi64x(d->eob1_info[i]));
    } else {
      const TcqFullPos *ps = &full[k];
      int mid_idx;
      const int idx = tcq_range_idx(lf, qidx, &mid_idx);
      const uint16_t *low = lf ? &tc->base_lf_cost_low_tbl[idx][bd][0][0]
                               : &tc->base_cost_low_tbl[idx][bd][0][0];
      const uint16_t *mid = NULL;
      __m256i mnib = _mm256_setzero_si256();
      if (mid_idx >= 0) {
        mid = lf ? &tc->mid_lf_cost_tbl[mid_idx][md][0][0]
                 : &tc->mid_cost_tbl[mid_idx][md][0][0];
        mnib = _mm256_cvtepu8_epi32(
            _mm_and_si128(_mm_srli_epi16(cb, 4), nib_mask));
      }
      __m256i teo0, teo1, neg_eo;
      tcq_tables_full(low, mid, nib, mnib, &teo0, &teo1, &neg_eo);
      tcq_step_full(s, c, teo0, teo1, neg_eo,
                    _mm256_permutevar8x32_epi32(t->lz, nib), t->neg_z, ps,
                    try_eob);
    }
    tcq_update_orig(s, tcq_ctx->lev_new[col], tcq_ctx->prev_st[col]);
    tcq_store_nodes(s, &trellis[scan_pos << TCQ_N_STATES_LOG]);
  }
}

void av2_trellis_loop_diagonal_st8_avx2(const tcq_param_t *p, int scan_hi,
                                        int scan_lo, tcq_ctx_t *tcq_ctx,
                                        tcq_node_t *trellis) {
  (void)scan_lo;
  assert(p->plane == 0);
  assert(p->tx_class == TX_CLASS_2D);
  assert((p->rdmult >> 32) == 0);
  const int16_t *scan = p->scan;
  const int bwl = p->bwl;
  const int height = p->txb_height;

  TcqConsts c;
  tcq_init_consts(p->rdmult, &c);
  TcqSegPos d;
  TcqRegs s;
  tcq_load_regs(&trellis[(scan_hi + 1) << TCQ_N_STATES_LOG], tcq_ctx, &s);

  int seg_lo = scan_hi + 1;  // no position prepared yet
  TcqDiagTables t;
  memset(&t, 0, sizeof(t));
  int t_key = -1;  // lf << 16 | base context offset of t; -1 = none
  while (scan_hi > 0) {
    const int blk_pos = scan[scan_hi];
    const int row = blk_pos >> bwl;
    const int col = blk_pos - (row << bwl);
    const int inc = AVMMIN(height - 1 - row, col);
    const int lo = scan_hi - inc;
    assert(lo >= 1);
    assert(scan_hi < TCQ_LF_SCAN_END || lo >= TCQ_LF_SCAN_END);
    if (lo < seg_lo) {
      // Prepare the next segment, ending with this diagonal.
      seg_lo = AVMMAX(scan_hi - TCQ_PREP_SEG + 1, 1);
      tcq_prepare_segment(p, seg_lo, scan_hi, &d);
    }
    const int lf = scan_hi < TCQ_LF_SCAN_END;
    const int diag_ctx = get_diag_ctx(lf, blk_pos, lo, bwl);
    // The tables only depend on the base context offset, which is the same
    // for most diagonals.
    const int bd = get_base_diag_ctx(diag_ctx);
    const int key = (lf << 16) | bd;
    if (key != t_key) {
      tcq_diag_tables_bd(p->txb_costs, lf, bd, &t);
      t_key = key;
    }
    // A constant lf lets the compiler build one copy per region.
    if (!lf) {
      tcq_diagonal(p, &c, &d, &t, seg_lo, scan_hi, lo, col, diag_ctx, 0, &s,
                   tcq_ctx, trellis);
    } else {
      tcq_diagonal(p, &c, &d, &t, seg_lo, scan_hi, lo, col, diag_ctx, 1, &s,
                   tcq_ctx, trellis);
    }
    tcq_store_orig(&s, tcq_ctx);
    av2_update_nbr_diagonal_avx2(tcq_ctx, row + inc, col - inc, bwl);
    s.orig = _mm256_loadu_si256((const __m256i *)kLaneState);
    scan_hi = lo - 1;
  }
  assert(scan_hi == 0);
  tcq_dc_position(p, tcq_ctx, trellis);
}
