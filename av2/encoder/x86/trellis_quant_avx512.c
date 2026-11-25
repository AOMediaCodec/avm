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

#include <assert.h>
#include <immintrin.h>

#include "config/av2_rtcd.h"

#include "avm/avm_integer.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/quant_common.h"
#include "av2/encoder/trellis_quant.h"
#include "av2/encoder/x86/trellis_quant_avx2.h"

// Pair winners before the state shuffle, from lane j of a and b.
typedef struct {
  __m256i ra, rb, ia, ib;
} TcqSel;

// High dwords in lane order, from ia / ib (0-7 / 8-15).
static const int32_t kHiSel[8] = { 1, 7, 9, 15, 3, 5, 11, 13 };
// Same, from unpacklo / unpackhi of ia, ib.
static const int32_t kHiUnpack[8] = { 1, 13, 3, 15, 9, 5, 11, 7 };

// a > b as a vector mask. Opaque: else a 4-cycle k compare (VEX: 2).
static AVM_FORCE_INLINE __m256i tcq_gt(__m256i a, __m256i b) {
  return tcq_opaque(_mm256_cmpgt_epi64(a, b));
}

// m ? b : a.
static AVM_FORCE_INLINE __m256i tcq_sel(__m256i m, __m256i a, __m256i b) {
  return _mm256_ternarylogic_epi64(m, b, a, 0xCA);
}

// vpminsq; m unused.
static AVM_FORCE_INLINE __m256i tcq_min(__m256i m, __m256i a, __m256i b) {
  (void)m;
  return _mm256_min_epi64(a, b);
}

// Even vs partner's odd (ties: even). ia / ib: states { 0, 6, 7, 1 } /
// { 4, 2, 3, 5 }.
static AVM_FORCE_INLINE void tcq_select_pairs(TcqSel *o, const TcqCand *k) {
  o->ra = _mm256_min_epi64(k->ev_rd_a, k->od_rd_b);
  o->rb = _mm256_min_epi64(k->ev_rd_b, k->od_rd_a);
  o->ia = tcq_sel(tcq_gt(k->ev_rd_a, k->od_rd_b), k->ev_info_a, k->od_info_b);
  o->ib = tcq_sel(tcq_gt(k->ev_rd_b, k->od_rd_a), k->ev_info_b, k->od_info_a);
}

// Shuffle stage 1: ra / ia = unpacklo, rb / ib = unpackhi. Lanes 0 / 1 of
// unpacklo: states 0 / 4.
static AVM_FORCE_INLINE void tcq_unpack(const TcqSel *o, TcqSel *u) {
  u->ra = _mm256_unpacklo_epi64(o->ra, o->rb);
  u->rb = _mm256_unpackhi_epi64(o->ra, o->rb);
  u->ia = _mm256_unpacklo_epi64(o->ia, o->ib);
  u->ib = _mm256_unpackhi_epi64(o->ia, o->ib);
}

// Shuffle stage 2: next-state lanes.
static AVM_FORCE_INLINE void tcq_arrange(TcqRegs *s, const TcqSel *u) {
  s->rd_a = _mm256_permute2x128_si256(u->ra, u->rb, 0x20);
  s->rd_b = _mm256_permute2x128_si256(u->rb, u->ra, 0x31);
  s->info_a = _mm256_permute2x128_si256(u->ia, u->ib, 0x20);
  s->info_b = _mm256_permute2x128_si256(u->ib, u->ia, 0x31);
}

// New eob; strictly cheaper wins. Unused lanes of eob_cand_rd are INT64_MAX.
static AVM_FORCE_INLINE void tcq_try_eob(__m256i *rd, __m256i *info,
                                         __m256i eob_cand_rd,
                                         __m256i eob_node_info) {
  const __m256i use = tcq_gt(*rd, eob_cand_rd);
  *rd = _mm256_min_epi64(*rd, eob_cand_rd);
  *info = tcq_sel(use, *info, eob_node_info);
}

// General-path tables: even | odd << 16, Q0 and Q1. Context offsets <= 6, so
// entries 0-6 only.

// Word offset of entry n in { t[0..15], t[12..27] }: 4 * (n + (n >> 2)).
static const int8_t kTblWordOff[16] = { 0, 4, 8, 12, 20, 24, 28, 0,
                                        0, 0, 0, 0,  0,  0,  0,  0 };

// Two word indices per origin; nib: one byte per origin.
static AVM_FORCE_INLINE __m256i tcq_word_index(__m128i nib) {
  const __m128i dup =
      _mm_setr_epi8(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
  const __m128i off =
      _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)kTblWordOff),
                       _mm_shuffle_epi8(nib, dup));
  return _mm256_cvtepu8_epi16(off);
}

// e0 / e1: Q0 / Q1 even | odd << 16 (table words 0, 1 / 3, 2). Origin order.
static AVM_FORCE_INLINE void tcq_lookup_words(const uint16_t *t, __m256i w,
                                              __m256i *e0, __m256i *e1) {
  const __m256i t0 = _mm256_loadu_si256((const __m256i *)t);
  const __m256i t3 = _mm256_loadu_si256((const __m256i *)(t + 12));
  *e0 = _mm256_permutex2var_epi16(
      t0, _mm256_add_epi16(w, _mm256_set1_epi32(0x00010000)), t3);
  *e1 = _mm256_permutex2var_epi16(
      t0, _mm256_add_epi16(w, _mm256_set1_epi32(0x00020003)), t3);
}

static AVM_FORCE_INLINE void tcq_tables_full(const uint16_t *low,
                                             const uint16_t *mid, __m128i nib,
                                             __m128i mnib, __m256i *teo0,
                                             __m256i *teo1, __m256i *neg_eo) {
  tcq_lookup_words(low, tcq_word_index(nib), teo0, teo1);
  __m128i n0 = _mm_loadl_epi64((const __m128i *)low);
  if (mid) {
    __m256i m0, m1;
    tcq_lookup_words(mid, tcq_word_index(mnib), &m0, &m1);
    // Entries < 2^14: 16-bit sums fit.
    *teo0 = _mm256_add_epi16(*teo0, m0);
    *teo1 = _mm256_add_epi16(*teo1, m1);
    n0 = _mm_add_epi16(n0, _mm_loadl_epi64((const __m128i *)mid));
  }
  // n0 = { Q0 even | Q0 odd << 16, Q1 even | Q1 odd << 16 }.
  n0 = _mm_shufflelo_epi16(n0, 0xB4);
  const __m256i nb = _mm256_broadcastq_epi64(n0);
  *neg_eo = _mm256_permutevar8x32_epi32(
      nb, _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1));
}

// scan_hi down to scan_lo; lf: low-frequency tables. Excludes DC.
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

  // Indexed by scan_hi - scan_pos.
  TcqFullPos full[MAX_DIAG];
  tcq_prepare_diagonal(p, d, seg_lo, scan_hi, scan_lo, lf, full);

  const __m256i inf = _mm256_set1_epi64x(INT64_MAX);
  const __m256i hi_sel = _mm256_loadu_si256((const __m256i *)kHiSel);
  const __m256i hi_unpack = _mm256_loadu_si256((const __m256i *)kHiUnpack);
  for (int k = 0, i = scan_hi - seg_lo, scan_pos = scan_hi; scan_pos >= scan_lo;
       --scan_pos, ++k, --i, --col) {
    const __m128i cb = _mm_loadl_epi64((const __m128i *)tcq_ctx->ctx[col]);
    const __m128i nibb = _mm_and_si128(cb, nib_mask);
    const __m256i nib = _mm256_cvtepu8_epi32(nibb);
    const int qidx = d->qidx[i];
    TcqSel o, u;
    TcqCand cand;
    __m256i hi;
    // prevIds gathered before tcq_arrange: off the chain.
    if (qidx == 0) {
      const __m256i dist_odd = _mm256_permute4x64_epi64(
          _mm256_castsi128_si256(_mm_load_si128((const __m128i *)d->dist[i])),
          0x50);
      tcq_cand_q1(s, c, _mm256_permutevar8x32_epi32(t->lq0, nib),
                  _mm256_permutevar8x32_epi32(t->lq1, nib), t->neg_q1, dist_odd,
                  &cand);
      tcq_select_pairs(&o, &cand);
      // State 4 = ib lane 0.
      if (try_eob) {
        tcq_try_eob(
            &o.rb, &o.ib,
            _mm256_blend_epi32(inf, _mm256_set1_epi64x(d->eob1_rd[i]), 0x03),
            _mm256_set1_epi64x(d->eob1_info[i]));
      }
      hi = _mm256_permutex2var_epi32(o.ia, hi_sel, o.ib);
      tcq_unpack(&o, &u);
    } else {
      const TcqFullPos *ps = &full[k];
      const uint16_t *mid;
      const uint16_t *low = tcq_full_tbl_ptrs(tc, lf, qidx, bd, md, &mid);
      __m128i mnib = _mm_setzero_si128();
      if (mid) {
        mnib = _mm_and_si128(_mm_srli_epi16(cb, 4), nib_mask);
      }
      __m256i teo0, teo1, neg_eo;
      tcq_tables_full(low, mid, nibb, mnib, &teo0, &teo1, &neg_eo);
      tcq_cand_full(s, c, teo0, teo1, neg_eo,
                    _mm256_permutevar8x32_epi32(t->lz, nib), t->neg_z, ps,
                    &cand);
      tcq_select_pairs(&o, &cand);
      // States 0 and 4 = unpacklo lanes 0 and 1, as in eob_rd.
      tcq_unpack(&o, &u);
      if (try_eob) tcq_try_eob(&u.ra, &u.ia, ps->eob_rd, ps->eob_info);
      hi = _mm256_permutex2var_epi32(u.ia, hi_unpack, u.ib);
    }
    tcq_arrange(s, &u);
    tcq_update_orig(s, hi, tcq_ctx->lev_new[col], tcq_ctx->prev_st[col]);
    tcq_store_nodes(s, &trellis[scan_pos << TCQ_N_STATES_LOG]);
  }
}

void av2_trellis_loop_diagonal_st8_avx512(const tcq_param_t *p, int scan_hi,
                                          int scan_lo, tcq_ctx_t *tcq_ctx,
                                          tcq_node_t *trellis) {
  (void)scan_lo;
  tcq_trellis_loop(p, scan_hi, tcq_ctx, trellis);
}
