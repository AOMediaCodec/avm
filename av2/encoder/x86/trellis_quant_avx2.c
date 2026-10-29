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
static AVM_FORCE_INLINE void tcq_select(TcqRegs *s, __m256i ev_rd_a,
                                        __m256i ev_info_a, __m256i ev_rd_b,
                                        __m256i ev_info_b, __m256i od_rd_a,
                                        __m256i od_info_a, __m256i od_rd_b,
                                        __m256i od_info_b) {
  const __m256i use_odd_a = _mm256_cmpgt_epi64(ev_rd_a, od_rd_b);
  const __m256i use_odd_b = _mm256_cmpgt_epi64(ev_rd_b, od_rd_a);
  const __m256i ra = _mm256_blendv_epi8(ev_rd_a, od_rd_b, use_odd_a);
  const __m256i rb = _mm256_blendv_epi8(ev_rd_b, od_rd_a, use_odd_b);
  const __m256i ia = _mm256_blendv_epi8(ev_info_a, od_info_b, use_odd_a);
  const __m256i ib = _mm256_blendv_epi8(ev_info_b, od_info_a, use_odd_b);
  const __m256i rlo = _mm256_unpacklo_epi64(ra, rb);
  const __m256i rhi = _mm256_unpackhi_epi64(ra, rb);
  const __m256i ilo = _mm256_unpacklo_epi64(ia, ib);
  const __m256i ihi = _mm256_unpackhi_epi64(ia, ib);
  s->rd_a = _mm256_permute2x128_si256(rlo, rhi, 0x20);
  s->rd_b = _mm256_permute2x128_si256(rhi, rlo, 0x31);
  s->info_a = _mm256_permute2x128_si256(ilo, ihi, 0x20);
  s->info_b = _mm256_permute2x128_si256(ihi, ilo, 0x31);
}

// New eob candidates for states 0 (a lane 0) and 4 (a lane 1). Unused lanes
// of eob_rd are INT64_MAX.
static AVM_FORCE_INLINE void tcq_try_eob(TcqRegs *s, __m256i eob_rd,
                                         __m256i eob_node_info) {
  const __m256i use = _mm256_cmpgt_epi64(s->rd_a, eob_rd);
  s->rd_a = _mm256_blendv_epi8(s->rd_a, eob_rd, use);
  s->info_a = _mm256_blendv_epi8(s->info_a, eob_node_info, use);
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
                                         __m256i dist_odd) {
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
             od_rd_b, od_info_b);
}

// Position with orig_qIdx >= 2: candidates are zero, one even and one odd
// level per quantizer.
static AVM_FORCE_INLINE void tcq_step_full(TcqRegs *s, const TcqConsts *c,
                                           __m256i teo0, __m256i teo1,
                                           __m256i neg_eo, __m256i tz,
                                           __m256i neg_z,
                                           const TcqFullPos *pos) {
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
             od_rd_b, od_info_b);
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

// The DC position, with the C per-position functions as in the C loop.
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
  tcq_coeff_ctx_t coeff_ctx;
  av2_get_coeff_ctx_c(tcq_ctx, 0, &coeff_ctx);
  coeff_ctx.coef_eob = get_lower_levels_ctx_eob(p->bwl, p->txb_height, 0);
  const int eob_rate = p->block_eob_rate[0];
  tcq_rate_t rd;
  if (pq_data.orig_qIdx < 2) {
    av2_pre_quant_q1_c(tcoeff[0], &pq_data, p->quant, temp_dqv, log_scale, 0);
    av2_get_rate_dist_lf_luma_q1_c(p, &pq_data, &coeff_ctx, 0, diag_ctx,
                                   eob_rate, dc_coeff_sign, &rd);
    av2_decide_states_q1_c(prev_decision, &rd, &pq_data, 1, try_eob, p->rdmult,
                           decision);
  } else {
    av2_pre_quant_c(tcoeff[0], &pq_data, p->quant, temp_dqv, log_scale, 0);
    av2_get_rate_dist_lf_luma_c(p, &pq_data, &coeff_ctx, 0, diag_ctx, eob_rate,
                                dc_coeff_sign, &rd);
    av2_decide_states_c(prev_decision, &rd, &pq_data, 1, try_eob, p->rdmult,
                        decision);
  }
  av2_update_states_c(decision, 0, tcq_ctx);
}

// Process one diagonal, from scan_hi down to scan_lo. lf selects the
// low-frequency region tables; the DC position is not handled here.
static AVM_FORCE_INLINE void tcq_diagonal(
    const tcq_param_t *p, const TcqConsts *c, const TcqSegPos *d,
    const TcqDiagTables *t, int seg_lo, int scan_hi, int scan_lo, int blk_pos,
    int col, int diag_ctx, const int lf, TcqRegs *s, tcq_ctx_t *tcq_ctx,
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
  const int blk_pos_inc = (1 << p->bwl) - 1;
  const int32_t *seg_qidx = &d->qidx[scan_lo - seg_lo];
  for (int k = 0, scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos, ++k) {
    const int qidx = seg_qidx[scan_pos - scan_lo];
    if (qidx == 0) continue;
    tcq_prepare_full(p, scan_pos, blk_pos + k * blk_pos_inc, lf, qidx,
                     &full[k]);
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
      tcq_step_q1(s, c, _mm256_permutevar8x32_epi32(t->lq0, nib),
                  _mm256_permutevar8x32_epi32(t->lq1, nib), t->neg_q1,
                  dist_odd);
      if (try_eob) {
        tcq_try_eob(
            s, _mm256_blend_epi32(inf, _mm256_set1_epi64x(d->eob1_rd[i]), 0x0C),
            _mm256_set1_epi64x(d->eob1_info[i]));
      }
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
                    _mm256_permutevar8x32_epi32(t->lz, nib), t->neg_z, ps);
      if (try_eob) tcq_try_eob(s, ps->eob_rd, ps->eob_info);
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
      tcq_diagonal(p, &c, &d, &t, seg_lo, scan_hi, lo, blk_pos, col, diag_ctx,
                   0, &s, tcq_ctx, trellis);
    } else {
      tcq_diagonal(p, &c, &d, &t, seg_lo, scan_hi, lo, blk_pos, col, diag_ctx,
                   1, &s, tcq_ctx, trellis);
    }
    tcq_store_orig(&s, tcq_ctx);
    av2_update_nbr_diagonal_avx2(tcq_ctx, row + inc, col - inc, bwl);
    s.orig = _mm256_loadu_si256((const __m256i *)kLaneState);
    scan_hi = lo - 1;
  }
  assert(scan_hi == 0);
  tcq_dc_position(p, tcq_ctx, trellis);
}
