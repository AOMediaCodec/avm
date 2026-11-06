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

#include "config/av2_rtcd.h"

#include "avm_dsp/x86/convolve_sse4_1.h"
#include "av2/common/warped_motion.h"
#include "av2/common/x86/highbd_warp_affine_x86.h"

// Edge-padded p0..p15 == AVX2 src_padded[0]
static INLINE __m256i load_horiz_src_padded_avx512(
    const uint16_t *ref, int out_of_boundary_left, int out_of_boundary_right,
    const __m256i v_arrange, const __m256i v_shuf_pattern) {
  const __m256i src_0 = _mm256_loadu_si256((__m256i *)ref);

  const __m256i src_01 = _mm256_shuffle_epi8(src_0, v_arrange);
  __m256i src_reg = _mm256_permute4x64_epi64(src_01, 0xD8);

  if (out_of_boundary_left >= 0) {
    const __m128i shuffle_left =
        _mm_loadu_si128((__m128i *)warp_pad_left[out_of_boundary_left]);
    const __m256i shuffle_reg_left = _mm256_broadcastsi128_si256(shuffle_left);
    src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_left);
  }

  if (out_of_boundary_right >= 0) {
    const __m128i shuffle_right =
        _mm_loadu_si128((__m128i *)warp_pad_right[out_of_boundary_right]);
    const __m256i shuffle_reg_right =
        _mm256_broadcastsi128_si256(shuffle_right);
    src_reg = _mm256_shuffle_epi8(src_reg, shuffle_reg_right);
  }

  return _mm256_shuffle_epi8(_mm256_permute4x64_epi64(src_reg, 0xD8),
                             v_shuf_pattern);
}

// Tap pair t, per half: p[2t..2t+7] | p[2t+1..2t+8] (== AVX2 src[t]).
// +16 = row b.
DECLARE_ALIGNED(64, static const uint16_t, warp_horiz_2row_idx[4][32]) = {
  { 0,  1,  2,  3,  4,  5,  6,  7,  1,  2,  3,  4,  5,  6,  7,  8,
    16, 17, 18, 19, 20, 21, 22, 23, 17, 18, 19, 20, 21, 22, 23, 24 },
  { 2,  3,  4,  5,  6,  7,  8,  9,  3,  4,  5,  6,  7,  8,  9,  10,
    18, 19, 20, 21, 22, 23, 24, 25, 19, 20, 21, 22, 23, 24, 25, 26 },
  { 4,  5,  6,  7,  8,  9,  10, 11, 5,  6,  7,  8,  9,  10, 11, 12,
    20, 21, 22, 23, 24, 25, 26, 27, 21, 22, 23, 24, 25, 26, 27, 28 },
  { 6,  7,  8,  9,  10, 11, 12, 13, 7,  8,  9,  10, 11, 12, 13, 14,
    22, 23, 24, 25, 26, 27, 28, 29, 23, 24, 25, 26, 27, 28, 29, 30 },
};

// (hn, hn+1) -> lo: rows (2n, 2n+1), hi: (2n+1, 2n+2) word pairs
// (== AVX2 packus + shuffle_input_mask). Low word == packus: horiz out is u16.
DECLARE_ALIGNED(64, static const uint16_t, warp_vert_pair_idx[32]) = {
  0,  16, 2,  18, 4,  20, 6,  22, 8,  24, 10, 26, 12, 28, 14, 30,
  16, 32, 18, 34, 20, 36, 22, 38, 24, 40, 26, 42, 28, 44, 30, 46,
};

// o = byte offset into av2_warped_filter
#define WARP_FILTER_128(o) \
  _mm_loadu_si128((const __m128i *)((const uint8_t *)av2_warped_filter + (o)))

// Compiler barrier: blocks clang folding / sinking of v.
#if defined(__GNUC__)
#define WARP_OPAQUE(v) __asm__("" : "+v"(v))
#else
#define WARP_OPAQUE(v) (void)0
#endif

// u = (sa >> 6) | (sb >> 6) << 32; steps are multiples of 64, phases >= 0:
// one add steps both. Byte offset 16 * (s >> 10) == (s >> 6) & ~15.
static INLINE int64_t warp_pack_phase(int32_t sa, int32_t sb) {
  return (int64_t)(sa >> 6) + (int64_t)(sb >> 6) * ((int64_t)1 << 32);
}
#define WARP_OFF_A(u) ((uint32_t)(u) & ~15u)
#define WARP_OFF_B(u) ((uint32_t)((uint64_t)(u) >> 32) & ~15u)

// [f(o0) | f(o1)]; broadcast (load-only) + blend beats vinserti128
static INLINE __m256i load_2filters_avx512(uint32_t o0, uint32_t o1) {
  __m256i hi = _mm256_broadcastsi128_si256(WARP_FILTER_128(o1));
  // keep broadcast + blend; else clang refolds to vinserti128
  WARP_OPAQUE(hi);
  return _mm256_blend_epi32(_mm256_castsi128_si256(WARP_FILTER_128(o0)), hi,
                            0xF0);
}

// lo: row a filters at u0, u1; hi: row b (!has_b: hi = lo)
static INLINE __m512i load_4filters_avx512(int64_t u0, int64_t u1, int has_b) {
  const __m256i a = load_2filters_avx512(WARP_OFF_A(u0), WARP_OFF_A(u1));
  if (!has_b) return _mm512_broadcast_i64x4(a);
  return _mm512_inserti64x4(
      _mm512_castsi256_si512(a),
      load_2filters_avx512(WARP_OFF_B(u0), WARP_OFF_B(u1)), 1);
}

// lo 256 = row a, hi 256 = row b (!has_b: row a); col n phase u + n * du.
// Each half == AVX2 coeffs[0..3]. Horiz (alpha) and vert (gamma).
static INLINE void prepare_coeffs_2rows_avx512(int64_t u, int64_t du, int has_b,
                                               int step_zero,
                                               __m512i *coeffs_z) {
  if (step_zero) {
    const __m512i f =
        has_b ? _mm512_inserti64x4(
                    _mm512_castsi256_si512(_mm256_broadcastsi128_si256(
                        WARP_FILTER_128(WARP_OFF_A(u)))),
                    _mm256_broadcastsi128_si256(WARP_FILTER_128(WARP_OFF_B(u))),
                    1)
              : _mm512_broadcast_i32x4(WARP_FILTER_128(WARP_OFF_A(u)));
    // == shuffle_alpha0_gamma0_mask0..3, imm vpshufd: no mask loads
    coeffs_z[0] = _mm512_shuffle_epi32(f, (_MM_PERM_ENUM)0x00);
    coeffs_z[1] = _mm512_shuffle_epi32(f, (_MM_PERM_ENUM)0x55);
    coeffs_z[2] = _mm512_shuffle_epi32(f, (_MM_PERM_ENUM)0xAA);
    coeffs_z[3] = _mm512_shuffle_epi32(f, (_MM_PERM_ENUM)0xFF);
    return;
  }
  const int64_t u1 = u + du, u2 = u1 + du, u3 = u2 + du, u4 = u3 + du;
  const int64_t u5 = u4 + du, u6 = u5 + du, u7 = u6 + du;
  const __m512i v01 = load_4filters_avx512(u, u1, has_b);
  const __m512i v23 = load_4filters_avx512(u2, u3, has_b);
  const __m512i v45 = load_4filters_avx512(u4, u5, has_b);
  const __m512i v67 = load_4filters_avx512(u6, u7, has_b);

  const __m512i c0123 = _mm512_unpacklo_epi32(v01, v23);
  const __m512i c0123u = _mm512_unpackhi_epi32(v01, v23);
  const __m512i c4567 = _mm512_unpacklo_epi32(v45, v67);
  const __m512i c4567u = _mm512_unpackhi_epi32(v45, v67);

  coeffs_z[0] = _mm512_unpacklo_epi64(c0123, c4567);
  coeffs_z[1] = _mm512_unpackhi_epi64(c0123, c4567);
  coeffs_z[2] = _mm512_unpacklo_epi64(c0123u, c4567u);
  coeffs_z[3] = _mm512_unpackhi_epi64(c0123u, c4567u);
}

// lo 256 = row a, hi 256 = row b; each half == filter_src_pixels_horiz_avx2
static INLINE __m512i filter_horiz_2rows_avx512(const __m256i ra,
                                                const __m256i rb,
                                                const __m512i *coeffs_z,
                                                const __m512i offset_z,
                                                const __m128i shift) {
  // ra | rb packed once; each tap-pair window is a 1-src vpermw
  const __m512i zab = _mm512_inserti64x4(_mm512_castsi256_si512(ra), rb, 1);

  const __m512i p0 = _mm512_madd_epi16(
      _mm512_permutexvar_epi16(
          _mm512_load_si512((const __m512i *)warp_horiz_2row_idx[0]), zab),
      coeffs_z[0]);
  const __m512i p1 = _mm512_madd_epi16(
      _mm512_permutexvar_epi16(
          _mm512_load_si512((const __m512i *)warp_horiz_2row_idx[1]), zab),
      coeffs_z[1]);
  const __m512i p2 = _mm512_madd_epi16(
      _mm512_permutexvar_epi16(
          _mm512_load_si512((const __m512i *)warp_horiz_2row_idx[2]), zab),
      coeffs_z[2]);
  const __m512i p3 = _mm512_madd_epi16(
      _mm512_permutexvar_epi16(
          _mm512_load_si512((const __m512i *)warp_horiz_2row_idx[3]), zab),
      coeffs_z[3]);

  const __m512i p01 = _mm512_add_epi32(p0, p1);
  const __m512i p23 = _mm512_add_epi32(p2, p3);
  const __m512i res = _mm512_add_epi32(_mm512_add_epi32(p01, p23), offset_z);
  return _mm512_sra_epi32(res, shift);
}

enum { WARP_H_INTERIOR, WARP_H_PADDED, WARP_H_CONST };

// Horiz rows k (lo), k+1 (hi); !has_b: hi = row k, unused.
// beta_nz: phase *hu += hstep2. !clamp_rows: rows from *rp, *rp += 2 rows.
// Force-inlined: flat args, no call cost. mode/clamp_rows/beta_nz/has_b/k are
// literals per chain so branches fold away; a context struct would undo that.
static AVM_FORCE_INLINE __m512i warp_horiz_pair_avx512(
    const uint16_t *ref, int stride, const uint16_t **rp, int iy4, int k,
    int has_b, int clamp_rows, int top_limit, int bottom_limit, int col,
    int mode, int out_of_boundary_left, int out_of_boundary_right, int beta_nz,
    int alpha_zero, int64_t *hu, int64_t hdu, int64_t hstep2,
    const __m512i *hcoeffs, const __m512i offset_z, const __m128i shift,
    int const_base, int const_scale, const __m256i v_arrange,
    const __m256i v_shuf_pattern) {
  const uint16_t *ref_a, *ref_b;
  if (clamp_rows) {
    const int iy_a = clamp(iy4 + k, top_limit, bottom_limit);
    const int iy_b = clamp(iy4 + k + has_b, top_limit, bottom_limit);
    ref_a = &ref[iy_a * stride + col];
    ref_b = &ref[iy_b * stride + col];
  } else {
    ref_a = *rp;
    ref_b = has_b ? ref_a + stride : ref_a;
    *rp = ref_a + 2 * stride;
  }
  if (mode == WARP_H_CONST) {
    // int16 wrap, as AVX2 cvtepi16_epi32(set1_epi16)
    const int va = (int16_t)(const_base + ref_a[0] * const_scale);
    const int vb = (int16_t)(const_base + ref_b[0] * const_scale);
    return _mm512_inserti64x4(_mm512_set1_epi32(va), _mm256_set1_epi32(vb), 1);
  }
  // copy, not pointer select: keeps coeffs in regs
  __m512i coeffs_z[4];
  if (beta_nz) {
    // !has_b: skip row 8, its phase may be out of table range
    prepare_coeffs_2rows_avx512(*hu, hdu, has_b, alpha_zero, coeffs_z);
    *hu += hstep2;
  } else {
    coeffs_z[0] = hcoeffs[0];
    coeffs_z[1] = hcoeffs[1];
    coeffs_z[2] = hcoeffs[2];
    coeffs_z[3] = hcoeffs[3];
  }
  __m256i ra, rb;
  if (mode == WARP_H_PADDED) {
    ra = load_horiz_src_padded_avx512(ref_a, out_of_boundary_left,
                                      out_of_boundary_right, v_arrange,
                                      v_shuf_pattern);
    rb = has_b ? load_horiz_src_padded_avx512(ref_b, out_of_boundary_left,
                                              out_of_boundary_right, v_arrange,
                                              v_shuf_pattern)
               : ra;
  } else {
    ra = _mm256_loadu_si256((const __m256i *)ref_a);
    rb = _mm256_loadu_si256((const __m256i *)ref_b);
  }
  __m512i h = filter_horiz_2rows_avx512(ra, rb, coeffs_z, offset_z, shift);
  // stops clang sinking chains into a shared tail that spills ZMMs
  WARP_OPAQUE(h);
  return h;
}

// 2 rows/ZMM (lo 256 = row k, hi 256 = row k+1); no tmp[], rows stay in regs.
void av2_highbd_warp_affine_avx512(const int32_t *mat, const uint16_t *ref,
                                   int width, int height, int stride,
                                   uint16_t *pred, int p_col, int p_row,
                                   int p_width, int p_height, int p_stride,
                                   int subsampling_x, int subsampling_y, int bd,
                                   ConvolveParams *conv_params, int16_t alpha,
                                   int16_t beta, int16_t gamma, int16_t delta) {
  // full 8-row blocks only (codec: p_height >= 8)
  if (p_height & 7) {
    av2_highbd_warp_affine_avx2(mat, ref, width, height, stride, pred, p_col,
                                p_row, p_width, p_height, p_stride,
                                subsampling_x, subsampling_y, bd, conv_params,
                                alpha, beta, gamma, delta);
    return;
  }

  const int left_limit = 0;
  const int right_limit = width - 1;
  const int top_limit = 0;
  const int bottom_limit = height - 1;
  const int reduce_bits_horiz = conv_params->round_0;
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));

  // 12-bit input must fit u16 intermediate
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const __m256i reduce_bits_vert_const =
      _mm256_set1_epi32(((1 << reduce_bits_vert) >> 1));
  const __m256i res_add_const = _mm256_set1_epi32(1 << offset_bits_vert);
  const __m256i res_sub_const =
      _mm256_set1_epi32(-(1 << (offset_bits - conv_params->round_1)) -
                        (1 << (offset_bits - conv_params->round_1 - 1)));
  __m256i round_bits_const = _mm256_set1_epi32(((1 << round_bits) >> 1));

  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  // locals: __m128i stores may alias conv_params / mat
  const int is_compound = conv_params->is_compound;
  const int do_average = conv_params->do_average;
  CONV_BUF_TYPE *const dst = conv_params->dst;
  const int dst_stride = conv_params->dst_stride;
  const int64_t m0 = mat[0], m1 = mat[1], m2 = mat[2];
  const int64_t m3 = mat[3], m4 = mat[4], m5 = mat[5];
  const int w0 = conv_params->fwd_offset;
  const int w1 = conv_params->bck_offset;
  const __m256i wt0 = _mm256_set1_epi32(w0);
  const __m256i wt1 = _mm256_set1_epi32(w1);

  const __m512i offset_z = _mm512_set1_epi32((1 << offset_bits_horiz) +
                                             (1 << (reduce_bits_horiz - 1)));
  const __m128i hshift = _mm_cvtsi32_si128(reduce_bits_horiz);
  // packed column / row-pair phase steps
  const int64_t hdu = warp_pack_phase(alpha, alpha);
  const int64_t vdu = warp_pack_phase(gamma, gamma);
  const int64_t hstep2 = warp_pack_phase(2 * beta, 2 * beta);
  const int64_t vstep2 = warp_pack_phase(2 * delta, 2 * delta);
  const int const_base = 1 << (bd + FILTER_BITS - reduce_bits_horiz - 1);
  const int const_scale = 1 << (FILTER_BITS - reduce_bits_horiz);

  const __m256i v_input_mask =
      _mm256_load_si256((const __m256i *)shuffle_input_mask);
  const __m256i v_arrange =
      _mm256_load_si256((const __m256i *)warp_highbd_arrange_bytes);
  const __m256i v_shuf_pattern =
      _mm256_load_si256((const __m256i *)warp_highbd_shuffle_pattern);
  const __m256i clip_pixel =
      _mm256_set1_epi16(bd == 10 ? 1023 : (bd == 12 ? 4095 : 255));
  const __m256i zero = _mm256_setzero_si256();
  const __m256i round_const_single = _mm256_set1_epi32(
      -(1 << (bd + reduce_bits_vert - 1)) + ((1 << reduce_bits_vert) >> 1));

  for (int i = 0; i < p_height; i += 8) {
    const int32_t src_y = (p_row + i + 4) << subsampling_y;
    const int64_t row_x = m3 * src_y + m0;
    const int64_t row_y = m5 * src_y + m1;
    for (int j = 0; j < p_width; j += 8) {
      const int32_t src_x = (p_col + j + 4) << subsampling_x;
      const int64_t dst_x = m2 * src_x + row_x;
      const int64_t dst_y = m4 * src_x + row_y;
      const int64_t x4 = dst_x >> subsampling_x;
      const int64_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = (int32_t)(x4 >> WARPEDMODEL_PREC_BITS);
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = (int32_t)(y4 >> WARPEDMODEL_PREC_BITS);
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      sx4 += alpha * (-4) + beta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             ((WARPEDPIXEL_PREC_SHIFTS * 3) << WARPEDDIFF_PREC_BITS);
      sy4 += gamma * (-4) + delta * (-4) + (1 << (WARPEDDIFF_PREC_BITS - 1)) +
             ((WARPEDPIXEL_PREC_SHIFTS * 3) << WARPEDDIFF_PREC_BITS);

      sx4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);
      sy4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);

      // iy4 + k in bounds for k in [-7, 7]: skip clamp
      const int clamp_rows =
          !((iy4 - 7 >= top_limit) && (iy4 + 7 <= bottom_limit));

      int mode = WARP_H_INTERIOR;
      int col = ix4 - 7;
      if (ix4 <= left_limit - 7) {
        mode = WARP_H_CONST;
        col = left_limit;
      } else if (ix4 >= right_limit + 7) {
        mode = WARP_H_CONST;
        col = right_limit;
      } else if (((ix4 - 7) < left_limit) || ((ix4 + 7) > right_limit)) {
        mode = WARP_H_PADDED;
      }
      const int out_of_boundary_left = left_limit - (ix4 - 6);
      const int out_of_boundary_right = (ix4 + 7) - right_limit;

      // phases of row pair k = -7
      int64_t hu = warp_pack_phase(sx4 - 3 * beta, sx4 - 2 * beta);
      __m512i hcoeffs[4];
      if (beta == 0 && mode != WARP_H_CONST) {
        prepare_coeffs_2rows_avx512(hu, hdu, 0, alpha == 0, hcoeffs);
      } else {
        hcoeffs[0] = hcoeffs[1] = hcoeffs[2] = hcoeffs[3] =
            _mm512_setzero_si512();
      }
      const uint16_t *rp = clamp_rows ? ref : &ref[(iy4 - 7) * stride + col];

      // hn = window rows 2n (lo), 2n+1 (hi); h7 hi unused
#define WARP_HPAIR(k, has_b, md, cr, beta_nz)                                  \
  warp_horiz_pair_avx512(ref, stride, &rp, iy4, (k), (has_b), (cr), top_limit, \
                         bottom_limit, col, (md), out_of_boundary_left,        \
                         out_of_boundary_right, (beta_nz), alpha == 0, &hu,    \
                         hdu, hstep2, hcoeffs, offset_z, hshift, const_base,   \
                         const_scale, v_arrange, v_shuf_pattern)
      // all 8 pairs per (mode, clamp, beta) case: branch once, not per pair
#define WARP_HPAIRS(md, cr, beta_nz)               \
  do {                                             \
    h0 = WARP_HPAIR(-7, 1, (md), (cr), (beta_nz)); \
    h1 = WARP_HPAIR(-5, 1, (md), (cr), (beta_nz)); \
    h2 = WARP_HPAIR(-3, 1, (md), (cr), (beta_nz)); \
    h3 = WARP_HPAIR(-1, 1, (md), (cr), (beta_nz)); \
    h4 = WARP_HPAIR(1, 1, (md), (cr), (beta_nz));  \
    h5 = WARP_HPAIR(3, 1, (md), (cr), (beta_nz));  \
    h6 = WARP_HPAIR(5, 1, (md), (cr), (beta_nz));  \
    h7 = WARP_HPAIR(7, 0, (md), (cr), (beta_nz));  \
  } while (0)
      __m512i h0, h1, h2, h3, h4, h5, h6, h7;
      // interior split out so mode/clamp pass as literals: the inlined per-pair
      // mode/clamp branches then fold at compile time.
      if (mode == WARP_H_CONST) {
        WARP_HPAIRS(WARP_H_CONST, clamp_rows, 0);
      } else if (mode == WARP_H_INTERIOR && !clamp_rows) {
        if (beta != 0) {
          WARP_HPAIRS(WARP_H_INTERIOR, 0, 1);
        } else {
          WARP_HPAIRS(WARP_H_INTERIOR, 0, 0);
        }
      } else {
        // edge: select keeps WARP_H_CONST provably false
        const int emode =
            mode == WARP_H_PADDED ? WARP_H_PADDED : WARP_H_INTERIOR;
        if (beta != 0) {
          WARP_HPAIRS(emode, clamp_rows, 1);
        } else {
          WARP_HPAIRS(emode, clamp_rows, 0);
        }
      }
#undef WARP_HPAIRS
#undef WARP_HPAIR

      // phases of output pair k = -4
      int64_t vu = warp_pack_phase(sy4, sy4 + delta);
      // delta != 0: vcoeffs set per pair in WARP_VOUT; zero-init guards warn
      __m512i vcoeffs[4] = { _mm512_setzero_si512(), _mm512_setzero_si512(),
                             _mm512_setzero_si512(), _mm512_setzero_si512() };
      if (delta == 0)
        prepare_coeffs_2rows_avx512(vu, vdu, 0, gamma == 0, vcoeffs);

        // out rows k (lo), k+1 (hi); za..zd = z(m..m+3)
#define WARP_VOUT(k, za, zb, zc, zd)                                          \
  do {                                                                        \
    if (delta != 0) {                                                         \
      prepare_coeffs_2rows_avx512(vu, vdu, 1, gamma == 0, vcoeffs);           \
      vu += vstep2;                                                           \
    }                                                                         \
    const __m512i s01 = _mm512_add_epi32(_mm512_madd_epi16(za, vcoeffs[0]),   \
                                         _mm512_madd_epi16(zb, vcoeffs[1]));  \
    const __m512i s23 = _mm512_add_epi32(_mm512_madd_epi16(zc, vcoeffs[2]),   \
                                         _mm512_madd_epi16(zd, vcoeffs[3]));  \
    const __m512i sum = _mm512_add_epi32(s01, s23);                           \
    const __m256i v_sum_r0 = _mm512_castsi512_si256(sum);                     \
    const __m256i v_sum_r1 = _mm512_extracti64x4_epi64(sum, 1);               \
    highbd_warp_store_vertical_2rows(                                         \
        pred, p_stride, is_compound, do_average, dst, dst_stride, &v_sum_r0,  \
        &v_sum_r1, &res_add_const, &reduce_bits_vert_const, use_wtd_comp_avg, \
        &wt0, &wt1, &res_sub_const, &round_bits_const, i, j, (k),             \
        reduce_bits_vert, round_bits, clip_pixel, zero, round_const_single,   \
        v_input_mask);                                                        \
  } while (0)

      // zm = rows (2m, 2m+1) | (2m+1, 2m+2) as madd word pairs
      const __m512i vidx =
          _mm512_load_si512((const __m512i *)warp_vert_pair_idx);
      const __m512i z0 = _mm512_permutex2var_epi16(h0, vidx, h1);
      const __m512i z1 = _mm512_permutex2var_epi16(h1, vidx, h2);
      const __m512i z2 = _mm512_permutex2var_epi16(h2, vidx, h3);
      const __m512i z3 = _mm512_permutex2var_epi16(h3, vidx, h4);
      WARP_VOUT(-4, z0, z1, z2, z3);
      const __m512i z4 = _mm512_permutex2var_epi16(h4, vidx, h5);
      WARP_VOUT(-2, z1, z2, z3, z4);
      const __m512i z5 = _mm512_permutex2var_epi16(h5, vidx, h6);
      WARP_VOUT(0, z2, z3, z4, z5);
      const __m512i z6 = _mm512_permutex2var_epi16(h6, vidx, h7);
      WARP_VOUT(2, z3, z4, z5, z6);
#undef WARP_VOUT
    }
  }
}
