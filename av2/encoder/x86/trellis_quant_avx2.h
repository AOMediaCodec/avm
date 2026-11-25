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

// Parts of the 8-state TCQ trellis loop shared by the AVX2 and AVX-512
// kernels: per-position inputs, per-diagonal rate tables, recursion state,
// candidate steps and the block driver (AVX2 intrinsics).

#ifndef AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_
#define AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_

#include <string.h>
#include <immintrin.h>

#include "config/av2_rtcd.h"

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

// Per-position inputs, indexed by scan_pos - seg_lo. They are computed 4
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
  // |tcoeff| << (log_scale - 1) and dqv, for the general path.
  DECLARE_ALIGNED(32, int32_t, abs_sh[TCQ_PREP_SEG + 4]);
  DECLARE_ALIGNED(32, int32_t, dqv[TCQ_PREP_SEG + 4]);
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

// 8-state trellis in registers: a = states { 0, 4, 6, 2 }, b = { 1, 5, 7, 3 };
// partner (state ^ 1) shares the lane.
typedef struct {
  __m256i rd_a, rd_b;
  __m256i info_a, info_b;  // rate | absLevel << 32 | prevId << 56
  __m256i orig;            // origin state, -1 = new eob
} TcqRegs;

typedef struct {
  __m256i rdmult, mask16, mask32;
  __m256i pid_a, pid_b;    // prevId << 56
  __m256i pid1_a, pid1_b;  // prevId << 56 | 1 << 32
} TcqConsts;

// Even / odd candidates of the a / b lanes.
typedef struct {
  __m256i ev_rd_a, ev_info_a, ev_rd_b, ev_info_b;
  __m256i od_rd_a, od_info_a, od_rd_b, od_info_b;
} TcqCand;

// Dword lane -> state, state -> lane.
static const int32_t kLaneState[8] = { 0, 1, 4, 5, 6, 7, 2, 3 };
static const int32_t kStateLane[8] = { 0, 1, 6, 7, 2, 3, 4, 5 };
// Bytes 0 / 1 of each lane -> { lev_new 0-7, prev_st 0-7 }, state order.
static const int8_t kLevPrevShuf[32] = { 0,  4,  -1, -1, 8,  12, -1, -1,
                                         1,  5,  -1, -1, 9,  13, -1, -1,
                                         -1, -1, 8,  12, -1, -1, 0,  4,
                                         -1, -1, 9,  13, -1, -1, 1,  5 };

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

// tcq_dist() of a coefficient of magnitude abs_tc.
static AVM_FORCE_INLINE __m256i tcq_dist_coef(__m256i qc, int abs_tc, int dqv,
                                              int log_scale) {
  const __m256i dq_shift = _mm256_set1_epi64x(QUANT_TABLE_BITS + log_scale);
  // Shift only the low 32 bits of each lane, like av2_pre_quant_c().
  const __m256i sh_shift = _mm256_set1_epi64x(log_scale - 1);
  const int32_t abs_sh = abs_tc * (1 << (log_scale - 1));
  // Zero-extended so that the compiler emits single vpmul[u]dq.
  const __m256i v_abs_sh = _mm256_set1_epi64x((uint32_t)abs_sh);
  const __m256i v_dqv = _mm256_set1_epi64x((uint32_t)dqv);
  const __m256i dist0 = _mm256_mul_epi32(v_abs_sh, v_abs_sh);
  return tcq_dist(qc, v_dqv, v_abs_sh, dist0, dq_shift, sh_shift);
}

// Rates of the 2 new eob candidates from the base eob table eob_tbl.
static AVM_FORCE_INLINE __m128i tcq_eob_rate(const uint16_t *eob_tbl,
                                             int block_eob_rate) {
  return _mm_add_epi32(
      _mm_cvtepu16_epi32(_mm_cvtsi32_si128((int)loadu_uint32(eob_tbl))),
      _mm_set1_epi32(block_eob_rate));
}

// Permutes the 4 candidate levels qIdx + j (j = 0..3) of the general path into
// the even and odd lanes of the a / b vectors, per qIdx & 3.
DECLARE_ALIGNED(32, static const int32_t, kFullPerm[4][2][8]) = {
  { { 0, 1, 0, 1, 6, 7, 6, 7 }, { 4, 5, 4, 5, 2, 3, 2, 3 } },
  { { 6, 7, 6, 7, 4, 5, 4, 5 }, { 2, 3, 2, 3, 0, 1, 0, 1 } },
  { { 4, 5, 4, 5, 2, 3, 2, 3 }, { 0, 1, 0, 1, 6, 7, 6, 7 } },
  { { 2, 3, 2, 3, 0, 1, 0, 1 }, { 6, 7, 6, 7, 4, 5, 4, 5 } },
};

// Golomb rate for gol_idx > 248 (hr >= 61): 2 * msb(hr - 6) + 4 bits,
// hr = ((qc + 3) >> 2) - (lf ? 4 : 3).
static AVM_FORCE_INLINE __m256i tcq_gol_large(__m256i qc, int lf) {
  const __m256i h = _mm256_sub_epi64(
      _mm256_srli_epi64(_mm256_add_epi64(qc, _mm256_set1_epi64x(3)), 2),
      _mm256_set1_epi64x(lf ? 10 : 9));
  // h < 2^24, high dword 0: float exponent = 127 + msb(h).
  const __m256i e =
      _mm256_srli_epi64(_mm256_castps_si256(_mm256_cvtepi32_ps(h)), 23);
  return _mm256_add_epi64(
      _mm256_slli_epi64(e, AV2_PROB_COST_SHIFT + 1),
      _mm256_set1_epi64x((4 << AV2_PROB_COST_SHIFT) -
                         (127 << (AV2_PROB_COST_SHIFT + 1))));
}

// Per-diagonal inputs of tcq_prepare_full().
typedef struct {
  const LV_MAP_COEFF_COST *tc;
  const uint16_t *block_eob_rate;
  __m256i dq_shift, sh_shift, rdmult;
  int try_eob;
  int eob_thr1, eob_thr2;  // num / 8, num / 4
} TcqFullConsts;

static AVM_FORCE_INLINE void tcq_full_consts(const tcq_param_t *p,
                                             TcqFullConsts *f) {
  const int num = p->txb_height << p->bwl;
  f->tc = p->txb_costs;
  f->block_eob_rate = p->block_eob_rate;
  f->dq_shift = _mm256_set1_epi64x(QUANT_TABLE_BITS + p->log_scale);
  // Shift only the low 32 bits of each lane, like av2_pre_quant_c().
  f->sh_shift = _mm256_set1_epi64x(p->log_scale - 1);
  f->rdmult = _mm256_set1_epi64x((uint32_t)p->rdmult);
  f->try_eob = p->sharpness == 0;
  f->eob_thr1 = num / 8;
  f->eob_thr2 = num / 4;
}

// Inputs of the general path at one position.
static AVM_FORCE_INLINE void tcq_prepare_full(const TcqFullConsts *f,
                                              const TcqSegPos *d, int i,
                                              int scan_pos, const int lf,
                                              const int qidx, TcqFullPos *pos) {
  const LV_MAP_COEFF_COST *tc = f->tc;
  const int32_t abs_sh = d->abs_sh[i];
  const int dqv = d->dqv[i];
  // Zero-extended so that the compiler emits single vpmul[u]dq.
  const __m256i v_abs_sh = _mm256_set1_epi64x((uint32_t)abs_sh);
  const __m256i v_dqv = _mm256_set1_epi64x((uint32_t)dqv);
  const __m256i dist0 = _mm256_mul_epi32(v_abs_sh, v_abs_sh);
  const __m256i qc = _mm256_add_epi64(_mm256_set1_epi64x(qidx),
                                      _mm256_setr_epi64x(0, 1, 2, 3));
  const __m256i dist =
      tcq_dist(qc, v_dqv, v_abs_sh, dist0, f->dq_shift, f->sh_shift);
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
  // get_lower_levels_ctx_eob() for scan_pos > 0.
  assert(scan_pos > 0);
  const int eob_ctx = 1 + (scan_pos > f->eob_thr1) + (scan_pos > f->eob_thr2);
  const uint16_t *eob_tbl = lf ? tc->base_lf_eob_cost_tbl[idx][eob_ctx]
                               : tc->base_eob_cost_tbl[idx][eob_ctx];
  __m128i rate_eob = tcq_eob_rate(eob_tbl, f->block_eob_rate[scan_pos]);
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
        gol = tcq_gol_large(qc, lf);
      }
    }
  }
  pos->gol_even = _mm256_permutevar8x32_epi32(gol, perm_even);
  pos->gol_odd = _mm256_permutevar8x32_epi32(gol, perm_odd);
  if (f->try_eob) {
    // { gol_even[0], gol_odd[0] } of the eob candidates.
    const __m128i gol_eob =
        _mm_unpacklo_epi32(_mm256_castsi256_si128(pos->gol_even),
                           _mm256_castsi256_si128(pos->gol_odd));
    rate_eob = _mm_add_epi32(rate_eob, gol_eob);
    const __m256i rate = _mm256_cvtepu32_epi64(rate_eob);
    const __m256i rd =
        _mm256_add_epi64(tcq_rate_cost(rate, f->rdmult),
                         _mm256_unpacklo_epi64(pos->dist_even, pos->dist_odd));
    pos->eob_rd = _mm256_blend_epi32(_mm256_set1_epi64x(INT64_MAX), rd, 0x0F);
    pos->eob_info = _mm256_or_si256(
        _mm256_or_si256(_mm256_unpacklo_epi64(pos->info_even, pos->info_odd),
                        rate),
        _mm256_set1_epi64x(TCQ_NEW_EOB_PREV_ID));
  } else {
    pos->eob_rd = _mm256_set1_epi64x(INT64_MAX);
    pos->eob_info = _mm256_setzero_si256();
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

// Distortion change (vs. zero) of quantization index qc (1 or 2) of a
// coefficient of magnitude abs_tc.
static AVM_FORCE_INLINE int64_t tcq_dist_q1(int abs_tc, int dqv, int qc,
                                            int log_scale) {
  const int32_t abs_sh = abs_tc * (1 << (log_scale - 1));
  // (dqc - a)^2 - a^2 = dqc * (dqc - 2a).
  const int64_t dqc = tcq_dq_level(dqv, qc, log_scale);
  return dqc * (dqc - 2 * (int64_t)abs_sh);
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
  const int64_t dist2 = tcq_dist_q1(abs_tc, dqv, 2, log_scale);
  d->dist[i][0] = dist2 * (1 << RDDIV_BITS);
  d->dist[i][1] = tcq_dist_q1(abs_tc, dqv, 1, log_scale) * (1 << RDDIV_BITS);
  if (p->sharpness == 0) {
    const int eob_ctx =
        get_lower_levels_ctx_eob(p->bwl, p->txb_height, scan_pos);
    const uint16_t *eob_tbl = lf ? tc->base_lf_eob_cost_tbl[0][eob_ctx]
                                 : tc->base_eob_cost_tbl[0][eob_ctx];
    const int rate = eob_tbl[1] + p->block_eob_rate[scan_pos];
    d->eob1_rd[i] = RDCOST(p->rdmult, rate, dist2);
    d->eob1_info[i] = rate | (INT64_C(1) << 32) | TCQ_NEW_EOB_PREV_ID;
  } else {
    d->eob1_rd[i] = INT64_MAX;
    d->eob1_info[i] = 0;
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
      const int abs_tc = abs(tcoeff[bp]);
      // int16_t like orig_qIdx in the C code.
      const int orig_qidx = (int16_t)(((int64_t)abs_tc * p->quant[1]) >> shift);
      d->qidx[i] = orig_qidx < 2 ? 0 : AVMMAX(orig_qidx - 2, 1);
      d->abs_sh[i] = abs_tc * (1 << (log_scale - 1));
      d->dqv[i] = get_dqv(p->dequant, bp, p->iqmatrix);
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
  const __m128i v_ls1 = _mm_cvtsi32_si128(log_scale - 1);
  const __m128i dqv1 = _mm_set1_epi32(p->dequant[1]);  // blk_pos > 0
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
    const __m128i abs_t = _mm_abs_epi32(t);
    const __m256i a = _mm256_cvtepu32_epi64(abs_t);
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
    _mm_storeu_si128((__m128i *)&d->abs_sh[i], _mm_sll_epi32(abs_t, v_ls1));
    _mm_storeu_si128((__m128i *)&d->dqv[i], dqv1);
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
    } else {
      _mm256_storeu_si256((__m256i *)&d->eob1_rd[i],
                          _mm256_set1_epi64x(INT64_MAX));
      _mm256_storeu_si256((__m256i *)&d->eob1_info[i], _mm256_setzero_si256());
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

// Compiler barrier: mask16 ANDs stay ANDs (no port-5 word blends); AVX-512
// tcq_gt stays a VEX compare.
static AVM_FORCE_INLINE __m256i tcq_opaque(__m256i v) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__("" : "+x"(v));
#endif
  return v;
}

static AVM_FORCE_INLINE void tcq_init_consts(int64_t rdmult, TcqConsts *c) {
  // rdmult < 2^32: one vpmuludq per rate.
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

// Origins via the new prevIds, plus lev_new / prev_st.
// hi: absLevel | prevId << 24, lane order, after try_eob.
static AVM_FORCE_INLINE void tcq_update_orig(TcqRegs *s, __m256i hi,
                                             uint8_t *lev_new,
                                             int8_t *prev_st) {
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

// Per-origin rates; new-eob lanes take neg.
static AVM_FORCE_INLINE __m256i tcq_fetch(const TcqRegs *s, __m256i t0,
                                          __m256i t1, __m256i neg) {
  const __m256i v =
      _mm256_blend_epi32(_mm256_permutevar8x32_epi32(t0, s->orig),
                         _mm256_permutevar8x32_epi32(t1, s->orig), 0xF0);
  return _mm256_blendv_epi8(v, neg, s->orig);
}

// Per kernel file. tcq_gt: a > b mask; tcq_sel: m ? b : a; tcq_min: min of
// a, b; m = tcq_gt(a, b), for ISAs without vpminsq.
static AVM_FORCE_INLINE __m256i tcq_gt(__m256i a, __m256i b);
static AVM_FORCE_INLINE __m256i tcq_sel(__m256i m, __m256i a, __m256i b);
static AVM_FORCE_INLINE __m256i tcq_min(__m256i m, __m256i a, __m256i b);

// orig_qIdx < 2: zero or odd level 1.
static AVM_FORCE_INLINE void tcq_cand_q1(const TcqRegs *s, const TcqConsts *c,
                                         __m256i tq0, __m256i tq1, __m256i neg,
                                         __m256i dist_odd, TcqCand *k) {
  const __m256i v = tcq_fetch(s, tq0, tq1, neg);
  const __m256i za = _mm256_and_si256(v, c->mask16);
  const __m256i oa = _mm256_and_si256(_mm256_srli_epi64(v, 16), c->mask16);
  const __m256i zb = _mm256_and_si256(_mm256_srli_epi64(v, 32), c->mask16);
  const __m256i ob = _mm256_srli_epi64(v, 48);
  const __m256i rate_a = _mm256_and_si256(s->info_a, c->mask32);
  const __m256i rate_b = _mm256_and_si256(s->info_b, c->mask32);

  k->ev_rd_a = _mm256_add_epi64(s->rd_a, tcq_rdc(za, c));
  k->ev_rd_b = _mm256_add_epi64(s->rd_b, tcq_rdc(zb, c));
  k->ev_info_a = _mm256_add_epi64(_mm256_add_epi64(rate_a, za), c->pid_a);
  k->ev_info_b = _mm256_add_epi64(_mm256_add_epi64(rate_b, zb), c->pid_b);
  k->od_rd_a =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(oa, c), dist_odd));
  k->od_rd_b =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(ob, c), dist_odd));
  k->od_info_a = _mm256_add_epi64(_mm256_add_epi64(rate_a, oa), c->pid1_a);
  k->od_info_b = _mm256_add_epi64(_mm256_add_epi64(rate_b, ob), c->pid1_b);
}

// orig_qIdx >= 2: zero, one even and one odd level per quantizer.
static AVM_FORCE_INLINE void tcq_cand_full(const TcqRegs *s, const TcqConsts *c,
                                           __m256i teo0, __m256i teo1,
                                           __m256i neg_eo, __m256i tz,
                                           __m256i neg_z, const TcqFullPos *pos,
                                           TcqCand *k) {
  const __m256i gol_even = pos->gol_even;
  const __m256i gol_odd = pos->gol_odd;
  const __m256i dist_even = pos->dist_even;
  const __m256i dist_odd = pos->dist_odd;
  const __m256i info_even = pos->info_even;
  const __m256i info_odd = pos->info_odd;
  // Zero rates: Q0 | Q1 << 16; lanes 2-3 use Q1.
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

  // Even: zero vs even level (ties: level).
  const __m256i rd_ea =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(ea, c), dist_even));
  const __m256i rd_eb =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(eb, c), dist_even));
  const __m256i rd_za = _mm256_add_epi64(s->rd_a, tcq_rdc(za, c));
  const __m256i rd_zb = _mm256_add_epi64(s->rd_b, tcq_rdc(zb, c));
  const __m256i use_zero_a = tcq_gt(rd_ea, rd_za);
  const __m256i use_zero_b = tcq_gt(rd_eb, rd_zb);
  const __m256i info_ea = _mm256_add_epi64(
      _mm256_add_epi64(rate_a, ea), _mm256_add_epi64(info_even, c->pid_a));
  const __m256i info_eb = _mm256_add_epi64(
      _mm256_add_epi64(rate_b, eb), _mm256_add_epi64(info_even, c->pid_b));
  const __m256i info_za =
      _mm256_add_epi64(_mm256_add_epi64(rate_a, za), c->pid_a);
  const __m256i info_zb =
      _mm256_add_epi64(_mm256_add_epi64(rate_b, zb), c->pid_b);
  k->ev_rd_a = tcq_min(use_zero_a, rd_ea, rd_za);
  k->ev_rd_b = tcq_min(use_zero_b, rd_eb, rd_zb);
  k->ev_info_a = tcq_sel(use_zero_a, info_ea, info_za);
  k->ev_info_b = tcq_sel(use_zero_b, info_eb, info_zb);

  k->od_rd_a =
      _mm256_add_epi64(s->rd_a, _mm256_add_epi64(tcq_rdc(oa, c), dist_odd));
  k->od_rd_b =
      _mm256_add_epi64(s->rd_b, _mm256_add_epi64(tcq_rdc(ob, c), dist_odd));
  k->od_info_a = _mm256_add_epi64(_mm256_add_epi64(rate_a, oa),
                                  _mm256_add_epi64(info_odd, c->pid_a));
  k->od_info_b = _mm256_add_epi64(_mm256_add_epi64(rate_b, ob),
                                  _mm256_add_epi64(info_odd, c->pid_b));
}

// prev_st bytes are negative or 0..7; table bytes 8..15 are never read.
static INLINE __m128i map_state(__m128i state, __m128i prev_st) {
  // Track previous states.
  __m128i map_st = _mm_shuffle_epi8(state, prev_st);
  // Set state to -1 to indicate eob truncation.
  map_st = _mm_blendv_epi8(map_st, prev_st, prev_st);
  return map_st;
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

static INLINE void tcq_dc_position(const tcq_param_t *p, tcq_ctx_t *tcq_ctx,
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

// full[k]: general-path inputs of scan_hi - k (qidx > 0 only).
static AVM_FORCE_INLINE void tcq_prepare_diagonal(const tcq_param_t *p,
                                                  const TcqSegPos *d,
                                                  int seg_lo, int scan_hi,
                                                  int scan_lo, int lf,
                                                  TcqFullPos *full) {
  assert(scan_hi - scan_lo < MAX_DIAG);
  assert(scan_lo >= seg_lo && scan_hi - seg_lo < TCQ_PREP_SEG);
  const int32_t *seg_qidx = &d->qidx[scan_lo - seg_lo];
  TcqFullConsts fc;
  tcq_full_consts(p, &fc);
  for (int k = 0, scan_pos = scan_hi; scan_pos >= scan_lo; --scan_pos, ++k) {
    const int qidx = seg_qidx[scan_pos - scan_lo];
    if (qidx == 0) continue;
    tcq_prepare_full(&fc, d, scan_pos - seg_lo, scan_pos, lf, qidx, &full[k]);
  }
}

// General-path low table; *mid: mid table, NULL below the mid range.
static AVM_FORCE_INLINE const uint16_t *tcq_full_tbl_ptrs(
    const LV_MAP_COEFF_COST *tc, int lf, int qidx, int bd, int md,
    const uint16_t **mid) {
  int mid_idx;
  const int idx = tcq_range_idx(lf, qidx, &mid_idx);
  *mid = NULL;
  if (mid_idx >= 0) {
    *mid = lf ? &tc->mid_lf_cost_tbl[mid_idx][md][0][0]
              : &tc->mid_cost_tbl[mid_idx][md][0][0];
  }
  return lf ? &tc->base_lf_cost_low_tbl[idx][bd][0][0]
            : &tc->base_cost_low_tbl[idx][bd][0][0];
}

// Per kernel file.
static AVM_FORCE_INLINE void tcq_diagonal(
    const tcq_param_t *p, const TcqConsts *c, const TcqSegPos *d,
    const TcqDiagTables *t, int seg_lo, int scan_hi, int scan_lo, int col,
    int diag_ctx, const int lf, TcqRegs *s, tcq_ctx_t *tcq_ctx,
    tcq_node_t *trellis);

static AVM_FORCE_INLINE void tcq_trellis_loop(const tcq_param_t *p, int scan_hi,
                                              tcq_ctx_t *tcq_ctx,
                                              tcq_node_t *trellis) {
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
  int t_key = -1;  // lf << 16 | bd of t; -1 = none
  while (scan_hi > 0) {
    const int blk_pos = scan[scan_hi];
    const int row = blk_pos >> bwl;
    const int col = blk_pos - (row << bwl);
    const int inc = AVMMIN(height - 1 - row, col);
    const int lo = scan_hi - inc;
    assert(lo >= 1);
    assert(scan_hi < TCQ_LF_SCAN_END || lo >= TCQ_LF_SCAN_END);
    if (lo < seg_lo) {
      // Next segment: from this diagonal down.
      seg_lo = AVMMAX(scan_hi - TCQ_PREP_SEG + 1, 1);
      tcq_prepare_segment(p, seg_lo, scan_hi, &d);
    }
    const int lf = scan_hi < TCQ_LF_SCAN_END;
    const int diag_ctx = get_diag_ctx(lf, blk_pos, lo, bwl);
    // Tables depend on lf and bd only; mostly unchanged.
    const int bd = get_base_diag_ctx(diag_ctx);
    const int key = (lf << 16) | bd;
    if (key != t_key) {
      tcq_diag_tables_bd(p->txb_costs, lf, bd, &t);
      t_key = key;
    }
    // Constant lf: one copy per region.
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

#endif  // AVM_AV2_ENCODER_X86_TRELLIS_QUANT_AVX2_H_
