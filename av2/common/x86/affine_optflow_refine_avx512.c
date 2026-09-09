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

#include <immintrin.h>

#include "config/avm_config.h"

#include "av2/common/reconinter.h"

// Pairwise int16 multiply-add into an int32 accumulator. Assumes grad_bits==0
// (C ref hard-codes it): no per-element rounding, so summing adjacent products
// via vpmaddwd is bit-exact. Used by the 8x8/16x8/32x8 cores alike.
static AVM_FORCE_INLINE __m512i madd_acc_512(__m512i acc, __m512i a,
                                             __m512i b) {
  return _mm512_add_epi32(acc, _mm512_madd_epi16(a, b));
}

static AVM_FORCE_INLINE __m256i madd_acc_256(__m256i acc, __m256i a,
                                             __m256i b) {
  return _mm256_add_epi32(acc, _mm256_madd_epi16(a, b));
}

static AVM_FORCE_INLINE __m512i load_4rows_zmm(const int16_t *p, int stride) {
  const __m128i r0 = _mm_loadu_si128((const __m128i *)(p));
  const __m128i r1 = _mm_loadu_si128((const __m128i *)(p + stride));
  const __m128i r2 = _mm_loadu_si128((const __m128i *)(p + 2 * stride));
  const __m128i r3 = _mm_loadu_si128((const __m128i *)(p + 3 * stride));
  const __m256i lo = _mm256_inserti128_si256(_mm256_castsi128_si256(r0), r1, 1);
  const __m256i hi = _mm256_inserti128_si256(_mm256_castsi128_si256(r2), r3, 1);
  return _mm512_inserti64x4(_mm512_castsi256_si512(lo), hi, 1);
}

// Solve one 2x2 system per subblock from reduced int32 sums. For a group of
// g adjacent 8x8 subblocks, each subblock owns one contiguous 128-bit lane
// (4 int32 partials) of every accumulator.
static AVM_FORCE_INLINE void solve_group(int g, const int32_t *u2,
                                         const int32_t *v2, const int32_t *uv,
                                         const int32_t *uw, const int32_t *vw,
                                         int d0, int d1, int bits,
                                         int rls_alpha, int *vx0, int *vy0,
                                         int *vx1, int *vy1) {
  for (int s = 0; s < g; ++s) {
    const int b = 4 * s;
    const int32_t su2 = u2[b] + u2[b + 1] + u2[b + 2] + u2[b + 3];
    const int32_t sv2 = v2[b] + v2[b + 1] + v2[b + 2] + v2[b + 3];
    const int32_t suv = uv[b] + uv[b + 1] + uv[b + 2] + uv[b + 3];
    const int32_t suw = uw[b] + uw[b + 1] + uw[b + 2] + uw[b + 3];
    const int32_t svw = vw[b] + vw[b + 1] + vw[b + 2] + vw[b + 3];
    calc_mv_process(su2, sv2, suv, suw, svw, d0, d1, bits, rls_alpha, vx0 + s,
                    vy0 + s, vx1 + s, vy1 + s);
  }
}

// One 8x8 subblock: pack 4 rows/iteration into a ZMM, accumulate, reduce once.
static AVM_FORCE_INLINE void opfl_mv_refinement_8x8_avx512(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int d0, int d1, int grad_prec_bits, int mv_prec_bits, int *vx0,
    int *vy0, int *vx1, int *vy1) {
  const int rls_alpha = 4 * OPFL_RLS_PARAM;
  const int bits = mv_prec_bits + grad_prec_bits;
  __m512i au2 = _mm512_setzero_si512();
  __m512i av2 = _mm512_setzero_si512();
  __m512i auv = _mm512_setzero_si512();
  __m512i auw = _mm512_setzero_si512();
  __m512i avw = _mm512_setzero_si512();
  for (int r = 0; r < 8; r += 4) {
    const __m512i gX = load_4rows_zmm(gx + r * gstride, gstride);
    const __m512i gY = load_4rows_zmm(gy + r * gstride, gstride);
    const __m512i pd = load_4rows_zmm(pdiff + r * pstride, pstride);
    au2 = madd_acc_512(au2, gX, gX);
    av2 = madd_acc_512(av2, gY, gY);
    auv = madd_acc_512(auv, gX, gY);
    auw = madd_acc_512(auw, gX, pd);
    avw = madd_acc_512(avw, gY, pd);
  }
  const int32_t su2 = _mm512_reduce_add_epi32(au2);
  const int32_t sv2 = _mm512_reduce_add_epi32(av2);
  const int32_t suv = _mm512_reduce_add_epi32(auv);
  const int32_t suw = _mm512_reduce_add_epi32(auw);
  const int32_t svw = _mm512_reduce_add_epi32(avw);
  calc_mv_process(su2, sv2, suv, suw, svw, d0, d1, bits, rls_alpha, vx0, vy0,
                  vx1, vy1);
}

// Two adjacent 8x8 subblocks (16-wide row/iteration) in one YMM.
static AVM_FORCE_INLINE void opfl_mv_refinement_16x8_avx512(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int d0, int d1, int grad_prec_bits, int mv_prec_bits, int *vx0,
    int *vy0, int *vx1, int *vy1) {
  const int rls_alpha = 4 * OPFL_RLS_PARAM;
  const int bits = mv_prec_bits + grad_prec_bits;
  __m256i au2 = _mm256_setzero_si256();
  __m256i av2 = _mm256_setzero_si256();
  __m256i auv = _mm256_setzero_si256();
  __m256i auw = _mm256_setzero_si256();
  __m256i avw = _mm256_setzero_si256();
  for (int r = 0; r < 8; ++r) {
    const __m256i gX = _mm256_loadu_si256((const __m256i *)(gx + r * gstride));
    const __m256i gY = _mm256_loadu_si256((const __m256i *)(gy + r * gstride));
    const __m256i pd =
        _mm256_loadu_si256((const __m256i *)(pdiff + r * pstride));
    au2 = madd_acc_256(au2, gX, gX);
    av2 = madd_acc_256(av2, gY, gY);
    auv = madd_acc_256(auv, gX, gY);
    auw = madd_acc_256(auw, gX, pd);
    avw = madd_acc_256(avw, gY, pd);
  }
  int32_t u2[8], v2[8], uv[8], uw[8], vw[8];
  _mm256_storeu_si256((__m256i *)u2, au2);
  _mm256_storeu_si256((__m256i *)v2, av2);
  _mm256_storeu_si256((__m256i *)uv, auv);
  _mm256_storeu_si256((__m256i *)uw, auw);
  _mm256_storeu_si256((__m256i *)vw, avw);
  solve_group(2, u2, v2, uv, uw, vw, d0, d1, bits, rls_alpha, vx0, vy0, vx1,
              vy1);
}

// Four adjacent 8x8 subblocks (32-wide row/iteration) in one ZMM.
static AVM_FORCE_INLINE void opfl_mv_refinement_32x8_avx512(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int d0, int d1, int grad_prec_bits, int mv_prec_bits, int *vx0,
    int *vy0, int *vx1, int *vy1) {
  const int rls_alpha = 4 * OPFL_RLS_PARAM;
  const int bits = mv_prec_bits + grad_prec_bits;
  __m512i au2 = _mm512_setzero_si512();
  __m512i av2 = _mm512_setzero_si512();
  __m512i auv = _mm512_setzero_si512();
  __m512i auw = _mm512_setzero_si512();
  __m512i avw = _mm512_setzero_si512();
  for (int r = 0; r < 8; ++r) {
    const __m512i gX = _mm512_loadu_si512((const void *)(gx + r * gstride));
    const __m512i gY = _mm512_loadu_si512((const void *)(gy + r * gstride));
    const __m512i pd = _mm512_loadu_si512((const void *)(pdiff + r * pstride));
    au2 = madd_acc_512(au2, gX, gX);
    av2 = madd_acc_512(av2, gY, gY);
    auv = madd_acc_512(auv, gX, gY);
    auw = madd_acc_512(auw, gX, pd);
    avw = madd_acc_512(avw, gY, pd);
  }
  int32_t u2[16], v2[16], uv[16], uw[16], vw[16];
  _mm512_storeu_si512((void *)u2, au2);
  _mm512_storeu_si512((void *)v2, av2);
  _mm512_storeu_si512((void *)uv, auv);
  _mm512_storeu_si512((void *)uw, auw);
  _mm512_storeu_si512((void *)vw, avw);
  solve_group(4, u2, v2, uv, uw, vw, d0, d1, bits, rls_alpha, vx0, vy0, vx1,
              vy1);
}

int av2_opfl_mv_refinement_nxn_avx512(
    const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy,
    int gstride, int bw, int bh, int n, int d0, int d1, int grad_prec_bits,
    int mv_prec_bits, int mi_x, int mi_y, int mi_cols, int mi_rows,
    int build_for_decode, int *vx0, int *vy0, int *vx1, int *vy1) {
  // Only bw>=16 (n==8) shapes run on the ZMM cores below; everything else goes
  // to AVX2: n==4 (<=8x8 luma with use_4x4), and bw==8 (8x8/8x16/8x32/8x64,
  // one 8x8 subblock per row).
  if (n != 8 || bw == 8) {
    return av2_opfl_mv_refinement_nxn_avx2(
        pdiff, pstride, gx, gy, gstride, bw, bh, n, d0, d1, grad_prec_bits,
        mv_prec_bits, mi_x, mi_y, mi_cols, mi_rows, build_for_decode, vx0, vy0,
        vx1, vy1);
  }
  int n_blocks = 0;
  assert(n == 8 && bw % n == 0 && bh % n == 0);
  for (int i = 0; i < bh; i += n) {
    // is_subblock_outside is monotonic in the column, so the inside subblocks
    // of this row form a prefix. Inside prefix is refined first, then advance
    // n_blocks over the skipped (outside) suffix.
    const int nsub = bw / n;
    int ninside = 0;
    while (ninside < nsub &&
           !is_subblock_outside(mi_x + ninside * n, mi_y + i, mi_cols, mi_rows,
                                build_for_decode)) {
      ninside++;
    }
    int s = 0, j = 0;
    while (s < ninside) {
      const int g = (ninside - s) >= 4 ? 4 : ((ninside - s) >= 2 ? 2 : 1);
      const int16_t *pd = pdiff + i * pstride + j;
      const int16_t *px = gx + i * gstride + j;
      const int16_t *py = gy + i * gstride + j;
      if (g == 4) {
        opfl_mv_refinement_32x8_avx512(
            pd, pstride, px, py, gstride, d0, d1, grad_prec_bits, mv_prec_bits,
            vx0 + n_blocks, vy0 + n_blocks, vx1 + n_blocks, vy1 + n_blocks);
      } else if (g == 2) {
        opfl_mv_refinement_16x8_avx512(
            pd, pstride, px, py, gstride, d0, d1, grad_prec_bits, mv_prec_bits,
            vx0 + n_blocks, vy0 + n_blocks, vx1 + n_blocks, vy1 + n_blocks);
      } else {
        opfl_mv_refinement_8x8_avx512(
            pd, pstride, px, py, gstride, d0, d1, grad_prec_bits, mv_prec_bits,
            vx0 + n_blocks, vy0 + n_blocks, vx1 + n_blocks, vy1 + n_blocks);
      }
      n_blocks += g;
      s += g;
      j += n * g;
    }
    n_blocks += (nsub - ninside);
  }
  return n_blocks;
}

// ---------------------------------------------------------------------------
// av2_bicubic_grad_interpolation_highbd
// ---------------------------------------------------------------------------
#if OPFL_BICUBIC_GRAD

// ROUND_POWER_OF_TWO_SIGNED on 16 int32 lanes.
// |in| <= (|c0|+|c1|) * 2*OPFL_PRED_MAX < 2^20, so abs()+bias and
// the sign-restore sub will not overflow.
static AVM_FORCE_INLINE __m512i round_pow2_signed_epi32_512(__m512i in,
                                                            int bits) {
  const __m512i sign = _mm512_srai_epi32(in, 31);
  const __m512i bias = _mm512_set1_epi32((1 << bits) >> 1);
  const __m512i mag =
      _mm512_srai_epi32(_mm512_add_epi32(_mm512_abs_epi32(in), bias), bits);
  // Restore the sign
  return _mm512_sub_epi32(_mm512_xor_si512(mag, sign), sign);
}

// temp (16 int32) -> round(bicubic_bits) -> clamp[-CLAMP, CLAMP] -> 16 int16.
// The clamp is applied in int32 so the final narrowing is always exact.
static AVM_FORCE_INLINE __m256i finalize_grad_epi32_512(__m512i temp) {
  const __m512i lo = _mm512_set1_epi32(-OPFL_GRAD_CLAMP_VAL);
  const __m512i hi = _mm512_set1_epi32(OPFL_GRAD_CLAMP_VAL);
  __m512i r = round_pow2_signed_epi32_512(temp, bicubic_bits);
  r = _mm512_min_epi32(_mm512_max_epi32(r, lo), hi);
  return _mm512_cvtepi32_epi16(r);
}

static AVM_FORCE_INLINE __m256i load_row16(const int16_t *p) {
  return _mm256_loadu_si256((const __m256i *)p);
}

static AVM_FORCE_INLINE __m256i load_row16_mask(__mmask16 m, const int16_t *p) {
  return _mm256_maskz_loadu_epi16(m, p);
}

static AVM_FORCE_INLINE int pack_taps(int c0, int c1) {
  return (int)((uint16_t)c0 | ((uint32_t)(uint16_t)c1 << 16));
}

// c0*d0 + c1*d1 via one vpmaddwd (d0,d1 packed as low/high int16 per lane),
// then round/clamp/narrow. d fits int16 (<=12-bit samples).
static AVM_FORCE_INLINE __m256i grad_pair_finalize(__m256i d0_16, __m256i d1_16,
                                                   __m512i coeff_pair) {
  const __m512i d0 = _mm512_cvtepi16_epi32(d0_16);
  const __m512i d1 = _mm512_cvtepi16_epi32(d1_16);
  const __m512i pair =
      _mm512_or_si512(_mm512_and_si512(d0, _mm512_set1_epi32(0xFFFF)),
                      _mm512_slli_epi32(d1, 16));
  return finalize_grad_epi32_512(_mm512_madd_epi16(pair, coeff_pair));
}

// Scalar bicubic x-gradient for a single column.
// Used only to fix the four boundary columns (0, 1, bw-2, bw-1) whose
// clamped neighbor indices / boundary taps differ from the vectorized interior.
static AVM_FORCE_INLINE int16_t bicubic_x_grad_scalar(const int16_t *s, int j,
                                                      int bw, int c0_in,
                                                      int c1_in, int c0_bd,
                                                      int c1_bd) {
  const int idprev = j > 0 ? j - 1 : 0;
  const int idprev2 = j > 1 ? j - 2 : 0;
  const int idnext = j + 1 < bw ? j + 1 : bw - 1;
  const int idnext2 = j + 2 < bw ? j + 2 : bw - 1;
  const int is_bd = (j == 0 || j == bw - 1);
  const int c0 = is_bd ? c0_bd : c0_in;
  const int c1 = is_bd ? c1_bd : c1_in;
  const int32_t t = c0 * (int32_t)(s[idnext] - s[idprev]) +
                    c1 * (int32_t)(s[idnext2] - s[idprev2]);
  const int bias = (1 << bicubic_bits) >> 1;
  int32_t r =
      (t < 0) ? -(((-t) + bias) >> bicubic_bits) : ((t + bias) >> bicubic_bits);
  if (r < -OPFL_GRAD_CLAMP_VAL) r = -OPFL_GRAD_CLAMP_VAL;
  if (r > OPFL_GRAD_CLAMP_VAL) r = OPFL_GRAD_CLAMP_VAL;
  return (int16_t)r;
}

#endif  // OPFL_BICUBIC_GRAD (helpers)

void av2_bicubic_grad_interpolation_highbd_avx512(const int16_t *pred_src,
                                                  int16_t *x_grad,
                                                  int16_t *y_grad,
                                                  const int stride,
                                                  const int bw, const int bh) {
#if OPFL_BICUBIC_GRAD
  assert(bw % 8 == 0);
  assert(bh % 8 == 0);

  if (bw < 32) {
    av2_bicubic_grad_interpolation_highbd_avx2(pred_src, x_grad, y_grad, stride,
                                               bw, bh);
    return;
  }

  // Bicubic derivative taps for the active delta. Interior columns/rows use the
  // [*][*][0] pair, the two boundary columns/rows the [*][*][1] pair.
  const int c0_in = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][0];
  const int c1_in = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][0];
  const int c0_bd = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][0][1];
  const int c1_bd = coeffs_bicubic[SUBPEL_GRAD_DELTA_BITS][1][1];
  const __m512i cpair_in = _mm512_set1_epi32(pack_taps(c0_in, c1_in));
  const __m512i cpair_bd = _mm512_set1_epi32(pack_taps(c0_bd, c1_bd));

  // ---- x-gradient ----
  // Neighbors are plain unaligned column-shifted loads straight from the source
  // (no per-row copy). The last chunk's right loads (up to s[bw+1]) use an
  // in-bounds base pointer with the out-of-row lanes masked off. The first
  // chunk's left neighbors (s[-1], s[-2]) would instead require a pre-row base
  // pointer, so they are built by
  // shifting the in-bounds s[0..15] window right by 1/2 lanes with zero fill.
  const __m256i xshift1 =
      _mm256_setr_epi16(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
  const __m256i xshift2 =
      _mm256_setr_epi16(0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
  for (int i = 0; i < bh; ++i) {
    const int16_t *s = pred_src + i * stride;
    int16_t *xg = x_grad + i * stride;
    for (int j0 = 0; j0 < bw; j0 += 16) {
      const int is_first = (j0 == 0);
      const int is_last = (j0 + 16 >= bw);
      // First chunk: shift s[0..15] right by 1/2 lanes (zero fill) instead of
      // loading from s-1 / s-2.
      // Result is bit-identical to the masked left loads used elsewhere.
      const __m256i cur0 = is_first ? load_row16(s) : _mm256_setzero_si256();
      const __m256i prev =
          is_first ? _mm256_maskz_permutexvar_epi16(0xFFFE, xshift1, cur0)
                   : load_row16(s + j0 - 1);
      const __m256i prev2 =
          is_first ? _mm256_maskz_permutexvar_epi16(0xFFFC, xshift2, cur0)
                   : load_row16(s + j0 - 2);
      const __m256i next = is_last ? load_row16_mask(0x7FFF, s + j0 + 1)
                                   : load_row16(s + j0 + 1);
      const __m256i next2 = is_last ? load_row16_mask(0x3FFF, s + j0 + 2)
                                    : load_row16(s + j0 + 2);
      const __m256i d0 = _mm256_sub_epi16(next, prev);
      const __m256i d1 = _mm256_sub_epi16(next2, prev2);
      _mm256_storeu_si256((__m256i *)(xg + j0),
                          grad_pair_finalize(d0, d1, cpair_in));
    }
    // Fix the four boundary columns
    xg[0] = bicubic_x_grad_scalar(s, 0, bw, c0_in, c1_in, c0_bd, c1_bd);
    xg[1] = bicubic_x_grad_scalar(s, 1, bw, c0_in, c1_in, c0_bd, c1_bd);
    xg[bw - 2] =
        bicubic_x_grad_scalar(s, bw - 2, bw, c0_in, c1_in, c0_bd, c1_bd);
    xg[bw - 1] =
        bicubic_x_grad_scalar(s, bw - 1, bw, c0_in, c1_in, c0_bd, c1_bd);
  }

  // ---- y-gradient ----
  for (int i = 0; i < bh; ++i) {
    const int is_bd = (i == 0 || i == bh - 1);
    __m512i cpair = cpair_in;
    if (is_bd) cpair = cpair_bd;
    const int16_t *sp = pred_src + (i > 0 ? i - 1 : 0) * stride;
    const int16_t *sp2 = pred_src + (i > 1 ? i - 2 : 0) * stride;
    const int16_t *sn = pred_src + (i + 1 < bh ? i + 1 : bh - 1) * stride;
    const int16_t *sn2 = pred_src + (i + 2 < bh ? i + 2 : bh - 1) * stride;
    int16_t *yg = y_grad + i * stride;
    for (int j0 = 0; j0 < bw; j0 += 16) {
      const __m256i prev = load_row16(sp + j0);
      const __m256i prev2 = load_row16(sp2 + j0);
      const __m256i next = load_row16(sn + j0);
      const __m256i next2 = load_row16(sn2 + j0);
      const __m256i d0 = _mm256_sub_epi16(next, prev);
      const __m256i d1 = _mm256_sub_epi16(next2, prev2);
      _mm256_storeu_si256((__m256i *)(yg + j0),
                          grad_pair_finalize(d0, d1, cpair));
    }
  }
#else
  (void)pred_src;
  (void)x_grad;
  (void)y_grad;
  (void)stride;
  (void)bw;
  (void)bh;
#endif  // OPFL_BICUBIC_GRAD
}

// ---------------------------------------------------------------------------
// av2_copy_pred_array_highbd
// ---------------------------------------------------------------------------

// ROUND_POWER_OF_TWO_SIGNED on 32 int16 lanes.
// k-mask restore (vpcmpgtw + vpsub{k}) is intentional: the mask-free
// vpsraw/xor/sub idiom round_pow2_signed_epi32_512 uses measured ~10%
// slower here. So the two rounders are not unified.
static AVM_FORCE_INLINE __m512i round_pow2_signed_epi16_512(__m512i x,
                                                            const int bits) {
  const __m512i zero = _mm512_setzero_si512();
  const __mmask32 neg = _mm512_cmpgt_epi16_mask(zero, x);
  const __m512i bias = _mm512_set1_epi16((int16_t)((1 << bits) >> 1));
  __m512i mag =
      _mm512_srli_epi16(_mm512_add_epi16(_mm512_abs_epi16(x), bias), bits);
  return _mm512_mask_sub_epi16(mag, neg, zero, mag);
}

static AVM_FORCE_INLINE __m512i clamp_epi16_512(__m512i v, int lo, int hi) {
  return _mm512_min_epi16(_mm512_max_epi16(v, _mm512_set1_epi16((int16_t)lo)),
                          _mm512_set1_epi16((int16_t)hi));
}

static AVM_FORCE_INLINE __m512i copy_pred_mul_val(int d0, int d1) {
  const __m512i mul1 = _mm512_set1_epi16((int16_t)d0);
  const __m512i mul2 =
      _mm512_sub_epi16(_mm512_setzero_si512(), _mm512_set1_epi16((int16_t)d1));
  return _mm512_unpacklo_epi16(mul1, mul2);
}

// One ZMM (32 int16) of source pairs -> dst1 = round(d0*P0 - d1*P1), dst2 =
// round(P0 - P1). interleave + vpmaddwd + vpackssdw is lane-local, so the
// packed result stays in natural order.
// Invariant (bd<=12): vpackssdw narrows to int16 before the shifts, but any
// value that would saturate exceeds OPFL_PRED_MAX*2^shift, so the final clamp
// yields the same result as the C int32 path. Breaks if bd>12 or the clamp
// bound grows.
static AVM_FORCE_INLINE void copy_pred_store32(__m512i s1, __m512i s2,
                                               __m512i mul_val, int bd,
                                               int centered, int16_t *out1,
                                               int16_t *out2) {
  const __m512i reg1 = _mm512_unpacklo_epi16(s1, s2);
  const __m512i reg2 = _mm512_unpackhi_epi16(s1, s2);
  __m512i r = _mm512_packs_epi32(_mm512_madd_epi16(reg1, mul_val),
                                 _mm512_madd_epi16(reg2, mul_val));
  if (centered) r = round_pow2_signed_epi16_512(r, 1);
  r = round_pow2_signed_epi16_512(r, bd - 8);
  r = clamp_epi16_512(r, -OPFL_PRED_MAX, OPFL_PRED_MAX);
  _mm512_storeu_si512((void *)out1, r);

  if (out2) {
    // Samples are <=12 bits, so the difference cannot overflow int16.
    __m512i d = _mm512_sub_epi16(s1, s2);
    d = round_pow2_signed_epi16_512(d, bd - 8);
    d = clamp_epi16_512(d, -OPFL_PRED_MAX, OPFL_PRED_MAX);
    _mm512_storeu_si512((void *)out2, d);
  }
}

static AVM_FORCE_INLINE __m512i load_2rows_u16_zmm(const uint16_t *p,
                                                   int stride) {
  const __m256i r0 = _mm256_loadu_si256((const __m256i *)p);
  const __m256i r1 = _mm256_loadu_si256((const __m256i *)(p + stride));
  return _mm512_inserti64x4(_mm512_castsi256_si512(r0), r1, 1);
}

static AVM_FORCE_INLINE __m512i load_4rows_u16_zmm(const uint16_t *p,
                                                   int stride) {
  const __m128i r0 = _mm_loadu_si128((const __m128i *)p);
  const __m128i r1 = _mm_loadu_si128((const __m128i *)(p + stride));
  const __m128i r2 = _mm_loadu_si128((const __m128i *)(p + 2 * stride));
  const __m128i r3 = _mm_loadu_si128((const __m128i *)(p + 3 * stride));
  const __m256i lo = _mm256_inserti128_si256(_mm256_castsi128_si256(r0), r1, 1);
  const __m256i hi = _mm256_inserti128_si256(_mm256_castsi128_si256(r2), r3, 1);
  return _mm512_inserti64x4(_mm512_castsi256_si512(lo), hi, 1);
}

// bw >= 32 (multiple of 32): 32 int16 per iteration, one full ZMM per column
// chunk of a single row.
static AVM_FORCE_INLINE void copy_pred_highbd_w32_avx512(
    const uint16_t *src1, const uint16_t *src2, int src_stride, int16_t *dst1,
    int16_t *dst2, int bw, int bh, int d0, int d1, int bd, int centered) {
  const __m512i mul_val = copy_pred_mul_val(d0, d1);
  for (int i = 0; i < bh; ++i) {
    const uint16_t *inp1 = src1 + i * src_stride;
    const uint16_t *inp2 = src2 + i * src_stride;
    int16_t *out1 = dst1 + i * bw;
    int16_t *out2 = dst2 ? dst2 + i * bw : NULL;
    for (int j = 0; j < bw; j += 32) {
      const __m512i s1 = _mm512_loadu_si512((const void *)(inp1 + j));
      const __m512i s2 = _mm512_loadu_si512((const void *)(inp2 + j));
      copy_pred_store32(s1, s2, mul_val, bd, centered, out1 + j,
                        out2 ? out2 + j : NULL);
    }
  }
}

// bw == 16: half a ZMM per row, so pack 2 rows (32 int16) per iteration.
static AVM_FORCE_INLINE void copy_pred_highbd_w16_avx512(
    const uint16_t *src1, const uint16_t *src2, int src_stride, int16_t *dst1,
    int16_t *dst2, int bw, int bh, int d0, int d1, int bd, int centered) {
  const __m512i mul_val = copy_pred_mul_val(d0, d1);
  for (int i = 0; i < bh; i += 2) {
    const __m512i s1 = load_2rows_u16_zmm(src1 + i * src_stride, src_stride);
    const __m512i s2 = load_2rows_u16_zmm(src2 + i * src_stride, src_stride);
    int16_t *out1 = dst1 + i * bw;
    int16_t *out2 = dst2 ? dst2 + i * bw : NULL;
    copy_pred_store32(s1, s2, mul_val, bd, centered, out1, out2);
  }
}

// bw == 8: quarter of a ZMM per row, so pack 4 rows (32 int16) per iteration.
static AVM_FORCE_INLINE void copy_pred_highbd_w8_avx512(
    const uint16_t *src1, const uint16_t *src2, int src_stride, int16_t *dst1,
    int16_t *dst2, int bw, int bh, int d0, int d1, int bd, int centered) {
  const __m512i mul_val = copy_pred_mul_val(d0, d1);
  for (int i = 0; i < bh; i += 4) {
    const __m512i s1 = load_4rows_u16_zmm(src1 + i * src_stride, src_stride);
    const __m512i s2 = load_4rows_u16_zmm(src2 + i * src_stride, src_stride);
    int16_t *out1 = dst1 + i * bw;
    int16_t *out2 = dst2 ? dst2 + i * bw : NULL;
    copy_pred_store32(s1, s2, mul_val, bd, centered, out1, out2);
  }
}

void av2_copy_pred_array_highbd_avx512(const uint16_t *src1,
                                       const uint16_t *src2, int src_stride,
                                       int16_t *dst1, int16_t *dst2, int bw,
                                       int bh, int d0, int d1, int bd,
                                       int centered) {
  if (bw == 8 && (bh & 3) == 0) {
    copy_pred_highbd_w8_avx512(src1, src2, src_stride, dst1, dst2, bw, bh, d0,
                               d1, bd, centered);
  } else if (bw == 16 && (bh & 1) == 0) {
    copy_pred_highbd_w16_avx512(src1, src2, src_stride, dst1, dst2, bw, bh, d0,
                                d1, bd, centered);
  } else if (bw >= 32 && (bw & 31) == 0) {
    copy_pred_highbd_w32_avx512(src1, src2, src_stride, dst1, dst2, bw, bh, d0,
                                d1, bd, centered);
  } else {
    // Odd bh at bw 8/16, or any other width: fall back to the tuned AVX2 path.
    av2_copy_pred_array_highbd_avx2(src1, src2, src_stride, dst1, dst2, bw, bh,
                                    d0, d1, bd, centered);
  }
}
