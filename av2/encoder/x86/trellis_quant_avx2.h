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

// Parts of the 8-state TCQ trellis loop: the per-position inputs computed
// before the recursion and the per-diagonal rate tables (AVX2 intrinsics).

#ifndef AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_
#define AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_

#include <immintrin.h>

#include "avm_dsp/x86/mem_sse2.h"
#include "av2/encoder/trellis_quant.h"

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

// Per-diagonal tables, indexed by the base context offset of the state.
typedef struct {
  __m256i lq0, lq1;  // q1 path: zero rate | odd rate << 16, for Q0 / Q1
  __m256i lz;        // zero rate of Q0 | zero rate of Q1 << 16
  __m256i neg_q1;    // q1 values of a new-eob origin (context 0), lane order
  __m256i neg_z;     // lz value of a new-eob origin (context 0)
} TcqDiagTables;

// Inputs of the general path (orig_qIdx >= 2) at one position. The 64-bit
// lanes follow the a / b vectors (Q0, Q0, Q1, Q1).
typedef struct {
  __m256i dist_odd;
  __m256i eob_rd;  // new eob candidates of states 0 and 4, else INT64_MAX
  __m256i eob_info;
  __m256i dist_even;
  __m256i gol_even, gol_odd;    // golomb rate of the even / odd level
  __m256i info_even, info_odd;  // absLevel << 32
} TcqFullPos;

// Number of scan positions prepared at once. It is at least MAX_DIAG, so a
// segment holds at least one whole diagonal.
#define TCQ_PREP_SEG 256

// Inputs of the q1 path, indexed by scan_pos - seg_lo. They are computed 4
// positions at a time, so there is room for 3 more positions at the end.
typedef struct {
  // Odd candidate distortion << RDDIV_BITS of Q0 and Q1.
  DECLARE_ALIGNED(32, int64_t, dist[TCQ_PREP_SEG + 4][2]);
  // RD cost and node info of the new eob candidate of state 4.
  DECLARE_ALIGNED(32, int64_t, eob1_rd[TCQ_PREP_SEG + 4]);
  DECLARE_ALIGNED(32, int64_t, eob1_info[TCQ_PREP_SEG + 4]);
  // qIdx of the general path (orig_qIdx - 2, at least 1), or 0 for the q1
  // path. Set for every position.
  DECLARE_ALIGNED(32, int32_t, qidx[TCQ_PREP_SEG + 4]);
} TcqSegPos;

// prevId -1 of a new eob, in bits 56 to 63 of the node info.
#define TCQ_NEW_EOB_PREV_ID (-(INT64_C(1) << 56))
// The low-frequency region is the first 4 diagonals, i.e. the scan positions
// below 10 (every 2D transform block is at least 4 wide and 4 high).
#define TCQ_LF_SCAN_END 10

// Limits of the general path for the default (0) and low-frequency (1)
// regions: the last index of the low-range and mid-range cost tables, and the
// first qidx that has a mid range and Golomb bits.
typedef struct {
  int low_max, mid_max, mid_start, gol_start;
} TcqRangeLimits;
static const TcqRangeLimits kTcqRange[2] = { { 4, 10, 2, 6 },
                                             { 8, 14, 6, 10 } };

// Returns the low-range table index of the general path (qidx >= 1) and sets
// *mid_idx to the mid-range table index, or to -1 when no mid range is coded.
static INLINE int tcq_range_idx(int lf, int qidx, int *mid_idx) {
  const TcqRangeLimits *r = &kTcqRange[lf];
  *mid_idx = qidx >= r->mid_start ? AVMMIN(qidx - 1, r->mid_max) : -1;
  return AVMMIN(qidx - 1, r->low_max);
}

// _mm256_shuffle_ps() of two vectors of 4 entries yields the entries
// { 0, 1, 4, 5, 2, 3, 6, 7 }; permuting by the same order restores 0 to 7.
static const int32_t kShufOrder[8] = { 0, 1, 4, 5, 2, 3, 6, 7 };

// Build the per-diagonal tables from the zero costs and the qIdx = 1 entries
// of the base cost table (8 contexts starting at the diagonal offset).
static AVM_FORCE_INLINE void tcq_diag_tables(__m256i z0, __m256i z1,
                                             const uint16_t *low,
                                             TcqDiagTables *t) {
  const __m256i mask16 = _mm256_set1_epi32(0xFFFF);
  const __m256i order = _mm256_loadu_si256((const __m256i *)kShufOrder);
  // Each entry holds { Q0 even, Q0 odd, Q1 odd, Q1 even }.
  const __m256i la = _mm256_loadu_si256((const __m256i *)low);
  const __m256i lb = _mm256_loadu_si256((const __m256i *)(low + 16));
  const __m256i lo = _mm256_permutevar8x32_epi32(
      _mm256_castps_si256(_mm256_shuffle_ps(_mm256_castsi256_ps(la),
                                            _mm256_castsi256_ps(lb), 0x88)),
      order);
  const __m256i hi = _mm256_permutevar8x32_epi32(
      _mm256_castps_si256(_mm256_shuffle_ps(_mm256_castsi256_ps(la),
                                            _mm256_castsi256_ps(lb), 0xDD)),
      order);
  z0 = _mm256_and_si256(z0, mask16);
  z1 = _mm256_and_si256(z1, mask16);
  t->lq0 = _mm256_or_si256(z0, _mm256_andnot_si256(mask16, lo));
  t->lq1 = _mm256_or_si256(z1, _mm256_slli_epi32(hi, 16));
  t->lz = _mm256_or_si256(z0, _mm256_slli_epi32(z1, 16));
  const __m256i zero = _mm256_setzero_si256();
  t->neg_q1 =
      _mm256_blend_epi32(_mm256_permutevar8x32_epi32(t->lq0, zero),
                         _mm256_permutevar8x32_epi32(t->lq1, zero), 0xF0);
  t->neg_z = _mm256_permutevar8x32_epi32(t->lz, zero);
}

// Rate cost of the 64-bit lanes of rate, with rdmult zero-extended to 64 bits.
static AVM_FORCE_INLINE __m256i tcq_rate_cost(__m256i rate, __m256i rdmult) {
  const __m256i round = _mm256_set1_epi64x(1 << (AV2_PROB_COST_SHIFT - 1));
  return _mm256_srli_epi64(
      _mm256_add_epi64(_mm256_mul_epu32(rate, rdmult), round),
      AV2_PROB_COST_SHIFT);
}

// Distortion change (vs. zero) << RDDIV_BITS of quantization indices qc
// (64-bit lanes), with the same 32-bit arithmetic as av2_pre_quant_c().
static AVM_FORCE_INLINE __m256i tcq_dist(__m256i qc, __m256i dqv,
                                         __m256i abs_sh, __m256i dist0,
                                         __m256i dq_shift, __m256i sh_shift) {
  const __m256i round = _mm256_set1_epi64x(1 << (QUANT_TABLE_BITS - 1));
  const __m256i dqc = _mm256_srlv_epi64(
      _mm256_add_epi64(_mm256_mul_epu32(qc, dqv), round), dq_shift);
  const __m256i diff =
      _mm256_sub_epi32(_mm256_sllv_epi32(dqc, sh_shift), abs_sh);
  const __m256i dist = _mm256_sub_epi64(_mm256_mul_epi32(diff, diff), dist0);
  return _mm256_slli_epi64(dist, RDDIV_BITS);
}

// Permutes the 4 candidate levels qIdx + j (j = 0..3) of the general path into
// the even and odd lanes of the a / b vectors, per qIdx & 3.
DECLARE_ALIGNED(32, static const int32_t, kFullPerm[4][2][8]) = {
  { { 0, 1, 0, 1, 6, 7, 6, 7 }, { 4, 5, 4, 5, 2, 3, 2, 3 } },
  { { 6, 7, 6, 7, 4, 5, 4, 5 }, { 2, 3, 2, 3, 0, 1, 0, 1 } },
  { { 4, 5, 4, 5, 2, 3, 2, 3 }, { 0, 1, 0, 1, 6, 7, 6, 7 } },
  { { 2, 3, 2, 3, 0, 1, 0, 1 }, { 6, 7, 6, 7, 4, 5, 4, 5 } },
};

// Inputs of the general path at one position.
static AVM_FORCE_INLINE void tcq_prepare_full(const tcq_param_t *p,
                                              int scan_pos, int blk_pos,
                                              const int lf, const int qidx,
                                              TcqFullPos *pos) {
  const LV_MAP_COEFF_COST *tc = p->txb_costs;
  const int log_scale = p->log_scale;
  const __m256i dq_shift = _mm256_set1_epi64x(QUANT_TABLE_BITS + log_scale);
  // Shift only the low 32 bits of each lane, like av2_pre_quant_c().
  const __m256i sh_shift = _mm256_set1_epi64x(log_scale - 1);
  const int abs_tc = abs(p->tcoeff[blk_pos]);
  const int dqv = get_dqv(p->dequant, blk_pos, p->iqmatrix);
  const int32_t abs_sh = abs_tc * (1 << (log_scale - 1));
  // Zero-extended so that the compiler emits single vpmul[u]dq.
  const __m256i v_abs_sh = _mm256_set1_epi64x((uint32_t)abs_sh);
  const __m256i v_dqv = _mm256_set1_epi64x((uint32_t)dqv);
  const __m256i dist0 = _mm256_mul_epi32(v_abs_sh, v_abs_sh);
  const __m256i qc = _mm256_add_epi64(_mm256_set1_epi64x(qidx),
                                      _mm256_setr_epi64x(0, 1, 2, 3));
  const __m256i dist = tcq_dist(qc, v_dqv, v_abs_sh, dist0, dq_shift, sh_shift);
  // absLevel << 32.
  const __m256i lev = _mm256_slli_epi64(
      _mm256_srli_epi64(_mm256_add_epi64(qc, _mm256_set1_epi64x(1)), 1), 32);
  const __m256i perm_even =
      _mm256_loadu_si256((const __m256i *)kFullPerm[qidx & 3][0]);
  const __m256i perm_odd =
      _mm256_loadu_si256((const __m256i *)kFullPerm[qidx & 3][1]);
  pos->dist_even = _mm256_permutevar8x32_epi32(dist, perm_even);
  pos->dist_odd = _mm256_permutevar8x32_epi32(dist, perm_odd);
  pos->info_even = _mm256_permutevar8x32_epi32(lev, perm_even);
  pos->info_odd = _mm256_permutevar8x32_epi32(lev, perm_odd);

  // Rates of the new eob candidates: state 0 with the Q0 even level (lane 0)
  // and state 4 with the Q0 odd level (lane 1).
  int mid_idx;
  const int idx = tcq_range_idx(lf, qidx, &mid_idx);
  const int eob_ctx = get_lower_levels_ctx_eob(p->bwl, p->txb_height, scan_pos);
  const uint16_t *eob_tbl = lf ? tc->base_lf_eob_cost_tbl[idx][eob_ctx]
                               : tc->base_eob_cost_tbl[idx][eob_ctx];
  __m128i rate_eob = _mm_add_epi32(
      _mm_cvtepu16_epi32(_mm_cvtsi32_si128((int)loadu_uint32(eob_tbl))),
      _mm_set1_epi32(p->block_eob_rate[scan_pos]));
  __m256i gol = _mm256_setzero_si256();
  if (mid_idx >= 0) {
    const uint16_t *mid_eob = lf ? tc->mid_lf_cost_tbl[mid_idx][7][0]
                                 : tc->mid_cost_tbl[mid_idx][0][0];
    rate_eob = _mm_add_epi32(
        rate_eob,
        _mm_cvtepu16_epi32(_mm_cvtsi32_si128((int)loadu_uint32(mid_eob))));
    const int gol_start = kTcqRange[lf].gol_start;
    if (qidx >= gol_start) {
      // Golomb (high range) bits of the 4 level candidates.
      const int gol_idx = qidx - gol_start + 1;
      if (gol_idx <= 248) {
        gol = _mm256_slli_epi64(
            _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(
                (int)loadu_uint32(&kGolombExp0Bits[gol_idx]))),
            AV2_PROB_COST_SHIFT);
      } else {
        gol = _mm256_setr_epi64x(get_golomb_cost_tcq((qidx + 1) >> 1, lf),
                                 get_golomb_cost_tcq((qidx + 2) >> 1, lf),
                                 get_golomb_cost_tcq((qidx + 3) >> 1, lf),
                                 get_golomb_cost_tcq((qidx + 4) >> 1, lf));
      }
    }
  }
  pos->gol_even = _mm256_permutevar8x32_epi32(gol, perm_even);
  pos->gol_odd = _mm256_permutevar8x32_epi32(gol, perm_odd);
  if (p->sharpness == 0) {
    // { gol_even[0], gol_odd[0] } of the eob candidates.
    const __m128i gol_eob =
        _mm_unpacklo_epi32(_mm256_castsi256_si128(pos->gol_even),
                           _mm256_castsi256_si128(pos->gol_odd));
    rate_eob = _mm_add_epi32(rate_eob, gol_eob);
    const __m256i rate = _mm256_cvtepu32_epi64(rate_eob);
    const __m256i rd = _mm256_add_epi64(
        tcq_rate_cost(rate, _mm256_set1_epi64x((uint32_t)p->rdmult)),
        _mm256_unpacklo_epi64(pos->dist_even, pos->dist_odd));
    pos->eob_rd = _mm256_blend_epi32(_mm256_set1_epi64x(INT64_MAX), rd, 0x0F);
    pos->eob_info = _mm256_or_si256(
        _mm256_or_si256(_mm256_unpacklo_epi64(pos->info_even, pos->info_odd),
                        rate),
        _mm256_set1_epi64x(TCQ_NEW_EOB_PREV_ID));
  } else {
    pos->eob_rd = _mm256_set1_epi64x(INT64_MAX);
  }
}

// Dequantized level of quantization index qc (1: level 1 of Q1, 2: level 1
// of Q0), scaled like abs_sh.
static AVM_FORCE_INLINE int64_t tcq_dq_level(int dqv, int qc, int log_scale) {
  return (int64_t)(uint32_t)(((uint64_t)(uint32_t)dqv * qc +
                              (1 << (QUANT_TABLE_BITS - 1))) >>
                             (QUANT_TABLE_BITS + log_scale))
         << (log_scale - 1);
}

// Inputs of the q1 path at one position (scalar code, used with quantization
// matrices).
static AVM_FORCE_INLINE void tcq_prepare_q1(const tcq_param_t *p, int scan_pos,
                                            int blk_pos, const int lf, int i,
                                            TcqSegPos *d) {
  const LV_MAP_COEFF_COST *tc = p->txb_costs;
  const int log_scale = p->log_scale;
  const int abs_tc = abs(p->tcoeff[blk_pos]);
  const int dqv = get_dqv(p->dequant, blk_pos, p->iqmatrix);
  const int32_t abs_sh = abs_tc * (1 << (log_scale - 1));
  // (dqc - a)^2 - a^2 = dqc * (dqc - 2a).
  const int64_t dqc1 = tcq_dq_level(dqv, 1, log_scale);
  const int64_t dqc2 = tcq_dq_level(dqv, 2, log_scale);
  const int64_t dist2 = dqc2 * (dqc2 - 2 * (int64_t)abs_sh);
  d->dist[i][0] = dist2 * (1 << RDDIV_BITS);
  d->dist[i][1] = dqc1 * (dqc1 - 2 * (int64_t)abs_sh) * (1 << RDDIV_BITS);
  if (p->sharpness == 0) {
    const int eob_ctx =
        get_lower_levels_ctx_eob(p->bwl, p->txb_height, scan_pos);
    const uint16_t *eob_tbl = lf ? tc->base_lf_eob_cost_tbl[0][eob_ctx]
                                 : tc->base_eob_cost_tbl[0][eob_ctx];
    const int rate = eob_tbl[1] + p->block_eob_rate[scan_pos];
    d->eob1_rd[i] = RDCOST(p->rdmult, rate, dist2);
    d->eob1_info[i] = rate | (INT64_C(1) << 32) | TCQ_NEW_EOB_PREV_ID;
  }
}

// Computes qidx and the q1 path inputs for scan positions seg_lo to seg_hi.
// Without a quantization matrix it does 4 positions at a time.
static AVM_FORCE_INLINE void tcq_prepare_segment(const tcq_param_t *p,
                                                 int seg_lo, int seg_hi,
                                                 TcqSegPos *d) {
  const int log_scale = p->log_scale;
  const int shift = 16 - log_scale + QUANT_FP_BITS;
  const tran_low_t *tcoeff = p->tcoeff;
  const int16_t *scan = p->scan;

  if (p->iqmatrix != NULL) {
    for (int scan_pos = seg_lo; scan_pos <= seg_hi; scan_pos++) {
      const int i = scan_pos - seg_lo;
      const int bp = scan[scan_pos];
      // int16_t like orig_qIdx in the C code.
      const int orig_qidx =
          (int16_t)(((int64_t)abs(tcoeff[bp]) * p->quant[1]) >> shift);
      d->qidx[i] = orig_qidx < 2 ? 0 : AVMMAX(orig_qidx - 2, 1);
      if (orig_qidx < 2)
        tcq_prepare_q1(p, scan_pos, bp, scan_pos < TCQ_LF_SCAN_END, i, d);
    }
    return;
  }

  const LV_MAP_COEFF_COST *tc = p->txb_costs;
  const __m256i dqc1 =
      _mm256_set1_epi64x(tcq_dq_level(p->dequant[1], 1, log_scale));
  const __m256i dqc2 =
      _mm256_set1_epi64x(tcq_dq_level(p->dequant[1], 2, log_scale));
  const __m256i quant = _mm256_set1_epi64x((uint32_t)p->quant[1]);
  const __m128i v_shift = _mm_cvtsi32_si128(shift);
  const __m128i v_log_scale = _mm_cvtsi32_si128(log_scale);
  const __m128i two = _mm_set1_epi32(2);
  const __m128i one = _mm_set1_epi32(1);
  const __m256i pick_lo = _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6);
  const int try_eob = p->sharpness == 0;
  // Rates of the eob candidates (base level 1) of the eob contexts 1 to 3,
  // default region | low-frequency region << 16.
  const __m128i eob_rate1 = _mm_set1_epi32(
      (int)(tc->base_eob_cost_tbl[0][1][1] |
            ((uint32_t)tc->base_lf_eob_cost_tbl[0][1][1] << 16)));
  const __m128i eob_rate2 = _mm_set1_epi32(
      (int)(tc->base_eob_cost_tbl[0][2][1] |
            ((uint32_t)tc->base_lf_eob_cost_tbl[0][2][1] << 16)));
  const __m128i eob_rate3 = _mm_set1_epi32(
      (int)(tc->base_eob_cost_tbl[0][3][1] |
            ((uint32_t)tc->base_lf_eob_cost_tbl[0][3][1] << 16)));
  // Last scan positions of the eob contexts 1 and 2, see
  // get_lower_levels_ctx_eob().
  const int num = p->txb_height << p->bwl;
  const __m128i eob_ctx1_end = _mm_set1_epi32(num / 8);
  const __m128i eob_ctx2_end = _mm_set1_epi32(num / 4);
  const __m128i lf_end = _mm_set1_epi32(TCQ_LF_SCAN_END);
  const __m128i lane_inc = _mm_setr_epi32(0, 1, 2, 3);
  const __m128i mask16 = _mm_set1_epi32(0xFFFF);
  const __m256i rdmult = _mm256_set1_epi64x((uint32_t)p->rdmult);
  const __m256i eob1_bits =
      _mm256_set1_epi64x((INT64_C(1) << 32) | TCQ_NEW_EOB_PREV_ID);
  const uint16_t *ber = p->block_eob_rate;

  for (int sp = seg_lo; sp <= seg_hi; sp += 4) {
    const int i = sp - seg_lo;
    // Positions past seg_hi repeat it.
    const int sp1 = AVMMIN(sp + 1, seg_hi), sp2 = AVMMIN(sp + 2, seg_hi);
    const int sp3 = AVMMIN(sp + 3, seg_hi);
    const __m128i t = _mm_setr_epi32(tcoeff[scan[sp]], tcoeff[scan[sp1]],
                                     tcoeff[scan[sp2]], tcoeff[scan[sp3]]);
    const __m256i a = _mm256_cvtepu32_epi64(_mm_abs_epi32(t));
    const __m256i oq64 = _mm256_srl_epi64(_mm256_mul_epu32(a, quant), v_shift);
    // Wrap to int16_t like orig_qIdx in the C code.
    const __m128i oq = _mm_srai_epi32(
        _mm_slli_epi32(
            _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(oq64, pick_lo)),
            16),
        16);
    const __m128i is_q1 = _mm_cmpgt_epi32(two, oq);
    const __m128i qidx =
        _mm_andnot_si128(is_q1, _mm_max_epi32(_mm_sub_epi32(oq, two), one));
    _mm_storeu_si128((__m128i *)&d->qidx[i], qidx);
    if (!_mm_movemask_epi8(is_q1)) continue;
    // 2 * abs_sh, with abs_sh = abs << (log_scale - 1).
    const __m256i abs_sh2 = _mm256_sll_epi64(a, v_log_scale);
    // (dqc - a)^2 - a^2 = dqc * (dqc - 2a).
    const __m256i dist2 = _mm256_slli_epi64(
        _mm256_mul_epi32(dqc2, _mm256_sub_epi64(dqc2, abs_sh2)), RDDIV_BITS);
    const __m256i dist1 = _mm256_slli_epi64(
        _mm256_mul_epi32(dqc1, _mm256_sub_epi64(dqc1, abs_sh2)), RDDIV_BITS);
    // Interleave the Q0 / Q1 distortions of each position.
    const __m256i lo = _mm256_unpacklo_epi64(dist2, dist1);
    const __m256i hi = _mm256_unpackhi_epi64(dist2, dist1);
    _mm256_storeu_si256((__m256i *)&d->dist[i][0],
                        _mm256_permute2x128_si256(lo, hi, 0x20));
    _mm256_storeu_si256((__m256i *)&d->dist[i + 2][0],
                        _mm256_permute2x128_si256(lo, hi, 0x31));
    if (try_eob) {
      const __m128i scan_pos = _mm_add_epi32(_mm_set1_epi32(sp), lane_inc);
      __m128i e = _mm_blendv_epi8(eob_rate1, eob_rate2,
                                  _mm_cmpgt_epi32(scan_pos, eob_ctx1_end));
      e = _mm_blendv_epi8(e, eob_rate3,
                          _mm_cmpgt_epi32(scan_pos, eob_ctx2_end));
      // The low-frequency rates are in the high 16 bits.
      e = _mm_blendv_epi8(_mm_and_si128(e, mask16), _mm_srli_epi32(e, 16),
                          _mm_cmpgt_epi32(lf_end, scan_pos));
      const __m128i rate = _mm_add_epi32(
          e, _mm_setr_epi32(ber[sp], ber[sp1], ber[sp2], ber[sp3]));
      const __m256i rate64 = _mm256_cvtepu32_epi64(rate);
      _mm256_storeu_si256((__m256i *)&d->eob1_info[i],
                          _mm256_or_si256(rate64, eob1_bits));
      _mm256_storeu_si256(
          (__m256i *)&d->eob1_rd[i],
          _mm256_add_epi64(tcq_rate_cost(rate64, rdmult), dist2));
    }
  }
}

// Per-diagonal tables of the base context offset bd (low-frequency region
// tables if lf).
static AVM_FORCE_INLINE void tcq_diag_tables_bd(const LV_MAP_COEFF_COST *tc,
                                                int lf, int bd,
                                                TcqDiagTables *t) {
  if (lf) {
    tcq_diag_tables(_mm256_cvtepu16_epi32(_mm_loadu_si128(
                        (const __m128i *)&tc->base_lf_cost_zero[0][bd])),
                    _mm256_cvtepu16_epi32(_mm_loadu_si128(
                        (const __m128i *)&tc->base_lf_cost_zero[1][bd])),
                    &tc->base_lf_cost_low_tbl[0][bd][0][0], t);
  } else {
    tcq_diag_tables(
        _mm256_loadu_si256((const __m256i *)&tc->base_cost_zero[0][bd]),
        _mm256_loadu_si256((const __m256i *)&tc->base_cost_zero[1][bd]),
        &tc->base_cost_low_tbl[0][bd][0][0], t);
  }
}

#endif  // AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_
