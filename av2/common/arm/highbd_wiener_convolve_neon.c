/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.
 * If the Alliance for Open Media Patent License 1.0 was not distributed with
 * this source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>
#include <assert.h>
#include <string.h>

#include "config/av2_rtcd.h"

#include "av2/common/convolve.h"
#include "av2/common/restoration.h"
#include "avm_dsp/avm_dsp_common.h"

#if defined(_MSC_VER)
#define WIENER_NOINLINE __declspec(noinline)
#else
#define WIENER_NOINLINE __attribute__((noinline))
#endif

// sqrshrun saturates a negative sum to 0; C rounds it then clips to 0, so the
// results match.
static INLINE uint16x8_t round_clip_narrow_pair(int32x4_t lo, int32x4_t hi,
                                                uint16x8_t max_u16,
                                                int prec_bits) {
  uint16x4_t lo_u16, hi_u16;
  switch (prec_bits) {
    case 12:
      lo_u16 = vqrshrun_n_s32(lo, 12);
      hi_u16 = vqrshrun_n_s32(hi, 12);
      break;
    default:
      assert(prec_bits == 7);
      lo_u16 = vqrshrun_n_s32(lo, 7);
      hi_u16 = vqrshrun_n_s32(hi, 7);
      break;
  }
  return vminq_u16(vcombine_u16(lo_u16, hi_u16), max_u16);
}

static INLINE uint16x4_t round_clip_narrow_4(int32x4_t acc, uint16x4_t max_u16,
                                             int prec_bits) {
  uint16x4_t r;
  switch (prec_bits) {
    case 12: r = vqrshrun_n_s32(acc, 12); break;
    default:
      assert(prec_bits == 7);
      r = vqrshrun_n_s32(acc, 7);
      break;
  }
  return vmin_u16(r, max_u16);
}

#define SYM4_PAIR(acc, pos_ptr, neg_ptr, taps_reg, lane)                  \
  acc = vmlal_lane_s16((acc),                                             \
                       vadd_s16(vreinterpret_s16_u16(vld1_u16(neg_ptr)),  \
                                vreinterpret_s16_u16(vld1_u16(pos_ptr))), \
                       (taps_reg), (lane))

static AVM_FORCE_INLINE void sym_diamond3_4_pairs(
    const uint16_t *m3, const uint16_t *m2, const uint16_t *m1,
    const uint16_t *c, const uint16_t *p1, const uint16_t *p2,
    const uint16_t *p3, int16x4_t taps_0_3, int16x4_t taps_4_7,
    int16x4_t taps_8_11, int32x4_t *a, int32x4_t *b) {
  SYM4_PAIR(*a, c + 1, c - 1, taps_0_3, 1);
  SYM4_PAIR(*b, p2, m2, taps_0_3, 2);
  SYM4_PAIR(*a, c + 2, c - 2, taps_0_3, 3);
  SYM4_PAIR(*b, p1 + 1, m1 - 1, taps_4_7, 0);
  SYM4_PAIR(*a, m1 + 1, p1 - 1, taps_4_7, 1);
  SYM4_PAIR(*b, p2 + 1, m2 - 1, taps_4_7, 2);
  SYM4_PAIR(*a, p2 - 1, m2 + 1, taps_4_7, 3);
  SYM4_PAIR(*b, p1 + 2, m1 - 2, taps_8_11, 0);
  SYM4_PAIR(*a, p1 - 2, m1 + 2, taps_8_11, 1);
  SYM4_PAIR(*b, p3, m3, taps_8_11, 2);
  SYM4_PAIR(*a, c + 3, c - 3, taps_8_11, 3);
}

// One chain: with four rows live, two chains per row would spill.
static AVM_FORCE_INLINE int32x4_t
sym_diamond3_4(const uint16_t *m3, const uint16_t *m2, const uint16_t *m1,
               const uint16_t *c, const uint16_t *p1, const uint16_t *p2,
               const uint16_t *p3, int16_t singleton, int16x4_t taps_0_3,
               int16x4_t taps_4_7, int16x4_t taps_8_11) {
  int32x4_t acc = vmull_n_s16(vreinterpret_s16_u16(vld1_u16(c)), singleton);
  SYM4_PAIR(acc, p1, m1, taps_0_3, 0);
  sym_diamond3_4_pairs(m3, m2, m1, c, p1, p2, p3, taps_0_3, taps_4_7, taps_8_11,
                       &acc, &acc);
  return acc;
}

// Two chains (smlal latency), joined by vqaddq because vaddq lets the compiler
// reassociate them into one. It cannot saturate: the sum and both partial sums
// fit in 32 bits (the filter precision contract of the C reference).
static AVM_FORCE_INLINE int32x4_t
sym_diamond4_4(const uint16_t *m4, const uint16_t *m3, const uint16_t *m2,
               const uint16_t *m1, const uint16_t *c, const uint16_t *p1,
               const uint16_t *p2, const uint16_t *p3, const uint16_t *p4,
               int16_t singleton, int16x4_t taps_0_3, int16x4_t taps_4_7,
               int16x4_t taps_8_11, int16x4_t taps_12_15) {
  int32x4_t acc_a = vmull_n_s16(vreinterpret_s16_u16(vld1_u16(c)), singleton);
  int32x4_t acc_b = vmull_lane_s16(vadd_s16(vreinterpret_s16_u16(vld1_u16(m1)),
                                            vreinterpret_s16_u16(vld1_u16(p1))),
                                   taps_0_3, 0);
  sym_diamond3_4_pairs(m3, m2, m1, c, p1, p2, p3, taps_0_3, taps_4_7, taps_8_11,
                       &acc_a, &acc_b);
  SYM4_PAIR(acc_b, p4, m4, taps_12_15, 0);
  SYM4_PAIR(acc_a, c + 4, c - 4, taps_12_15, 1);
  SYM4_PAIR(acc_b, p3 + 3, m3 - 3, taps_12_15, 2);
  SYM4_PAIR(acc_a, p3 - 3, m3 + 3, taps_12_15, 3);
  return vqaddq_s32(acc_a, acc_b);
}

#undef SYM4_PAIR

// prec_bits 7 output is clip(c + ((R + 64) >> 7)) with R = center_tap x c +
// sum tap x (neg + pos), as 128 x c is a multiple of 128. int16_t
// multiply-accumulates wrap, so every partial sum of R must fit, not just R.
// Any subset of R's terms lies in [-neg x max_pixel, pos x max_pixel] (pos /
// neg: sums of the positive / |negative| weights, pair taps twice), so
// max(pos, neg) x max_pixel <= INT16_MAX covers every accumulation order and
// chain join. Lane sums saturate only far above the bound, and then reject.
static AVM_FORCE_INLINE int sym_fits_s16(int16x8_t taps_lo, int16x8_t taps_hi,
                                         int16_t center_tap, int bit_depth) {
  const int16x8_t zero = vdupq_n_s16(0);
  const int16x8_t pos_lo = vmaxq_s16(taps_lo, zero);
  const int16x8_t pos_hi = vmaxq_s16(taps_hi, zero);
  const int16x8_t neg_lo = vqsubq_s16(pos_lo, taps_lo);
  const int16x8_t neg_hi = vqsubq_s16(pos_hi, taps_hi);
  const int16x8_t pos8 = vqaddq_s16(pos_lo, pos_hi);
  const int16x8_t neg8 = vqaddq_s16(neg_lo, neg_hi);
#if AVM_ARCH_AARCH64
  const int32_t pos_pairs = vaddlvq_s16(pos8);
  const int32_t neg_pairs = vaddlvq_s16(neg8);
#else
  const int64x2_t pos2 = vpaddlq_s32(vpaddlq_s16(pos8));
  const int64x2_t neg2 = vpaddlq_s32(vpaddlq_s16(neg8));
  const int32_t pos_pairs =
      (int32_t)(vgetq_lane_s64(pos2, 0) + vgetq_lane_s64(pos2, 1));
  const int32_t neg_pairs =
      (int32_t)(vgetq_lane_s64(neg2, 0) + vgetq_lane_s64(neg2, 1));
#endif
  const int64_t pos =
      2 * (int64_t)pos_pairs + (center_tap > 0 ? center_tap : 0);
  const int64_t neg =
      2 * (int64_t)neg_pairs + (center_tap < 0 ? -center_tap : 0);
  return (pos > neg ? pos : neg) * ((1 << bit_depth) - 1) <= INT16_MAX;
}

// srsra is exact for int16_t R and |(R + 64) >> 7| <= 256, so adding it to
// c <= 4095 cannot wrap.
static AVM_FORCE_INLINE uint16x8_t round_clip_s16_p7_v(int16x8_t c, int16x8_t r,
                                                       uint16x8_t max_u16) {
  return vminq_u16(vqshluq_n_s16(vrsraq_n_s16(c, r, 7), 0), max_u16);
}

// Rows a and a + gap in one vector (gap 2 for 4x4: rows two apart share more
// views). AArch64 builds dx != 0 views from 8-column loads (zip1 for dx < 0,
// zip2 at dx - 4 for dx > 0), one load pair serving two views; those loads
// stay in the tap window only for dx != 0 at radius 3, and any dx at radius 4
// (wide).
static AVM_FORCE_INLINE int16x8_t sym4x2_view(const uint16_t *r0, ptrdiff_t st,
                                              int a, int dx, int wide,
                                              int gap) {
  const uint16_t *ra = r0 + a * st;
  const uint16_t *rb = ra + gap * st;
#if AVM_ARCH_AARCH64
  if (dx < 0 || (dx == 0 && wide)) {
    return vreinterpretq_s16_u64(
        vzip1q_u64(vreinterpretq_u64_u16(vld1q_u16(ra + dx)),
                   vreinterpretq_u64_u16(vld1q_u16(rb + dx))));
  }
  if (dx > 0) {
    return vreinterpretq_s16_u64(
        vzip2q_u64(vreinterpretq_u64_u16(vld1q_u16(ra + dx - 4)),
                   vreinterpretq_u64_u16(vld1q_u16(rb + dx - 4))));
  }
#else
  (void)wide;
#endif
  return vreinterpretq_s16_u16(
      vcombine_u16(vld1_u16(ra + dx), vld1_u16(rb + dx)));
}

#define SYM4X2_PAIR_SUM16(a, dy, dx)                           \
  vaddq_s16(sym4x2_view(r0, st, (a) - (dy), -(dx), wide, gap), \
            sym4x2_view(r0, st, (a) + (dy), (dx), wide, gap))
#define SYM4X2_PAIR16(ch, dy, dx, taps_reg, lane)                             \
  do {                                                                        \
    acc[2 * (ch)] = vmlaq_lane_s16(                                           \
        acc[2 * (ch)], SYM4X2_PAIR_SUM16(0, (dy), (dx)), (taps_reg), (lane)); \
    if (nvec == 2) {                                                          \
      acc[2 * (ch) + 1] =                                                     \
          vmlaq_lane_s16(acc[2 * (ch) + 1], SYM4X2_PAIR_SUM16(1, (dy), (dx)), \
                         (taps_reg), (lane));                                 \
    }                                                                         \
  } while (0)
#define SYM4X2_PAIR16_FIRST(dy, dx, taps_reg, lane)                           \
  do {                                                                        \
    acc[2] =                                                                  \
        vmulq_lane_s16(SYM4X2_PAIR_SUM16(0, (dy), (dx)), (taps_reg), (lane)); \
    if (nvec == 2) {                                                          \
      acc[3] = vmulq_lane_s16(SYM4X2_PAIR_SUM16(1, (dy), (dx)), (taps_reg),   \
                              (lane));                                        \
    }                                                                         \
  } while (0)

#define SYM4X2_CENTER16(ch)                                                    \
  do {                                                                         \
    acc[2 * (ch)] = vmlaq_n_s16(acc[2 * (ch)],                                 \
                                sym4x2_view(r0, st, 0, 0, wide, gap), center); \
    if (nvec == 2) {                                                           \
      acc[2 * (ch) + 1] = vmlaq_n_s16(                                         \
          acc[2 * (ch) + 1], sym4x2_view(r0, st, 1, 0, wide, gap), center);    \
    }                                                                          \
  } while (0)

// Each chain's running value is a subset sum of R, so sym_fits_s16 bounds it.
static AVM_FORCE_INLINE void sym_diamond_4x2_s16(
    const uint16_t *r0, ptrdiff_t st, int wide, const int nvec, const int gap,
    int16_t center, int16x4_t taps_0_3, int16x4_t taps_4_7, int16x4_t taps_8_11,
    int16x4_t taps_12_15, int16x8_t acc[4]) {
  acc[0] = vmulq_lane_s16(SYM4X2_PAIR_SUM16(0, 0, 2), taps_0_3, 3);
  if (nvec == 2) {
    acc[1] = vmulq_lane_s16(SYM4X2_PAIR_SUM16(1, 0, 2), taps_0_3, 3);
  }
  SYM4X2_PAIR16_FIRST(1, 2, taps_8_11, 0);
  SYM4X2_PAIR16(0, 1, -2, taps_8_11, 1);
  SYM4X2_PAIR16(1, 0, 1, taps_0_3, 1);
  SYM4X2_PAIR16(0, 0, 3, taps_8_11, 3);
  SYM4X2_PAIR16(1, 1, 1, taps_4_7, 0);
  SYM4X2_PAIR16(0, -1, 1, taps_4_7, 1);
  SYM4X2_PAIR16(1, 2, 1, taps_4_7, 2);
  SYM4X2_PAIR16(0, 2, -1, taps_4_7, 3);
  if (wide) {
    SYM4X2_PAIR16(1, 3, 3, taps_12_15, 2);
    SYM4X2_PAIR16(0, 3, -3, taps_12_15, 3);
  }
  SYM4X2_PAIR16(1, 1, 0, taps_0_3, 0);
  SYM4X2_PAIR16(0, 2, 0, taps_0_3, 2);
  SYM4X2_PAIR16(1, 3, 0, taps_8_11, 2);
  if (wide) {
    SYM4X2_PAIR16(0, 4, 0, taps_12_15, 0);
    SYM4X2_PAIR16(1, 0, 4, taps_12_15, 1);
  }
  SYM4X2_CENTER16(0);
}

#undef SYM4X2_PAIR_SUM16
#undef SYM4X2_PAIR16
#undef SYM4X2_PAIR16_FIRST
#undef SYM4X2_CENTER16

// vqaddq never saturates (the sum is R) and stops reassociation of the chains.
// Rows 0 / 2 are stored before reloading the (1, 0) view, else the 16-tap
// kernel spills.
static AVM_FORCE_INLINE void store_4x4_s16(const uint16_t *r0, ptrdiff_t st,
                                           int wide, const int16x8_t acc[4],
                                           uint16_t *d, int dst_stride,
                                           uint16x8_t max_u16) {
  const uint16x8_t out02 = round_clip_s16_p7_v(
      sym4x2_view(r0, st, 0, 0, wide, 2), vqaddq_s16(acc[0], acc[2]), max_u16);
  vst1_u16(d, vget_low_u16(out02));
  vst1_u16(d + 2 * dst_stride, vget_high_u16(out02));
  const uint16x8_t out13 = round_clip_s16_p7_v(
      sym4x2_view(r0, st, 1, 0, wide, 2), vqaddq_s16(acc[1], acc[3]), max_u16);
  vst1_u16(d + dst_stride, vget_low_u16(out13));
  vst1_u16(d + 3 * dst_stride, vget_high_u16(out13));
}

static AVM_FORCE_INLINE void store_4x2_s16(const uint16_t *r0, ptrdiff_t st,
                                           int wide, const int16x8_t acc[4],
                                           uint16_t *d, int dst_stride,
                                           uint16x8_t max_u16) {
  const uint16x8_t out01 = round_clip_s16_p7_v(
      sym4x2_view(r0, st, 0, 0, wide, 1), vqaddq_s16(acc[0], acc[2]), max_u16);
  vst1_u16(d, vget_low_u16(out01));
  vst1_u16(d + dst_stride, vget_high_u16(out01));
}

// wide 0: wide loads would read outside config_y's radius-3 window.
static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x4_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  const int wide = 0;
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  int16x8_t acc[4];
  sym_diamond_4x2_s16(r0, st, wide, 2, 2, center_tap, taps_0_3, taps_4_7,
                      taps_8_11, taps_8_11, acc);
  store_4x4_s16(r0, st, wide, acc, d, dst_stride, max_u16);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x2_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  const int wide = 0;
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  int16x8_t acc[4];
  sym_diamond_4x2_s16(r0, st, wide, 1, 1, center_tap, taps_0_3, taps_4_7,
                      taps_8_11, taps_8_11, acc);
  store_4x2_s16(r0, st, wide, acc, d, dst_stride, max_u16);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x4_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16_t center_tap = filter[16];
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const int16x4_t taps_12_15 = vld1_s16(&filter[12]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  int16x8_t acc[4];
  sym_diamond_4x2_s16(r0, st, 1, 2, 2, center_tap, taps_0_3, taps_4_7,
                      taps_8_11, taps_12_15, acc);
  store_4x4_s16(r0, st, 1, acc, d, dst_stride, max_u16);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x2_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16_t center_tap = filter[16];
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const int16x4_t taps_12_15 = vld1_s16(&filter[12]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  int16x8_t acc[4];
  sym_diamond_4x2_s16(r0, st, 1, 1, 1, center_tap, taps_0_3, taps_4_7,
                      taps_8_11, taps_12_15, acc);
  store_4x2_s16(r0, st, 1, acc, d, dst_stride, max_u16);
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_13tap_4x4_body(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    const int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin, const int h) {
  const int16_t singleton_s16 = (int16_t)((1 << prec_bits) + center_tap);
  const uint16x4_t max_u16 = vdup_n_u16((uint16_t)((1 << bit_depth) - 1));

  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);

  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  const uint16_t *rm1 = r0 - st;
  const uint16_t *rm2 = rm1 - st;
  const uint16_t *rm3 = rm2 - st;
  const uint16_t *r1 = r0 + st;
  const uint16_t *r2 = r1 + st;
  const uint16_t *r3 = r2 + st;
  const uint16_t *r4 = r3 + st;
  const uint16_t *r5 = r4 + st;
  const uint16_t *r6 = r5 + st;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;

  // Stores last: dst may alias dgd, so an earlier store would force reloading
  // every shared source view.
  const int32x4_t acc0 =
      sym_diamond3_4(rm3, rm2, rm1, r0, r1, r2, r3, singleton_s16, taps_0_3,
                     taps_4_7, taps_8_11);
  const int32x4_t acc1 =
      sym_diamond3_4(rm2, rm1, r0, r1, r2, r3, r4, singleton_s16, taps_0_3,
                     taps_4_7, taps_8_11);
  if (h == 2) {
    vst1_u16(d, round_clip_narrow_4(acc0, max_u16, prec_bits));
    vst1_u16(d + dst_stride, round_clip_narrow_4(acc1, max_u16, prec_bits));
    return;
  }
  const int32x4_t acc2 =
      sym_diamond3_4(rm1, r0, r1, r2, r3, r4, r5, singleton_s16, taps_0_3,
                     taps_4_7, taps_8_11);
  const int32x4_t acc3 = sym_diamond3_4(
      r0, r1, r2, r3, r4, r5, r6, singleton_s16, taps_0_3, taps_4_7, taps_8_11);
  vst1_u16(d, round_clip_narrow_4(acc0, max_u16, prec_bits));
  vst1_u16(d + dst_stride, round_clip_narrow_4(acc1, max_u16, prec_bits));
  vst1_u16(d + 2 * dst_stride, round_clip_narrow_4(acc2, max_u16, prec_bits));
  vst1_u16(d + 3 * dst_stride, round_clip_narrow_4(acc3, max_u16, prec_bits));
}

// One function per prec_bits: in a single function the compiler hoists the
// bodies' shared loads above the branch and spills them.
static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x4_p7_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_4x4_body(dgd, stride, filter, center_tap, 7,
                                           dst, dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 4);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x4_p12_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_4x4_body(dgd, stride, filter, center_tap, 12,
                                           dst, dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 4);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x2_p7_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_4x4_body(dgd, stride, filter, center_tap, 7,
                                           dst, dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 2);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_4x2_p12_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_4x4_body(dgd, stride, filter, center_tap, 12,
                                           dst, dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 2);
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_13tap_4xh_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin, const int h) {
  if (prec_bits == 7 &&
      sym_fits_s16(vld1q_s16(filter),
                   vcombine_s16(vld1_s16(filter + 8), vdup_n_s16(0)),
                   center_tap, bit_depth)) {
    if (h == 4) {
      convolve_symmetric_highbd_13tap_4x4_s16_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    } else {
      convolve_symmetric_highbd_13tap_4x2_s16_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    }
  } else if (prec_bits == 12) {
    if (h == 4) {
      convolve_symmetric_highbd_13tap_4x4_p12_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    } else {
      convolve_symmetric_highbd_13tap_4x2_p12_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    }
  } else {
    assert(prec_bits == 7);
    if (h == 4) {
      convolve_symmetric_highbd_13tap_4x4_p7_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    } else {
      convolve_symmetric_highbd_13tap_4x2_p7_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    }
  }
}

static WIENER_NOINLINE void convolve_symmetric_highbd_7tap_4x4_neon(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, int prec_bits, uint16_t *dst, int dst_stride,
    int bit_depth, int block_row_begin, int block_col_begin) {
  const int num_sym_taps = filter_config->num_pixels / 2;
  assert(num_sym_taps == 6);

  int32_t singleton_tap = 1 << prec_bits;
  if (filter_config->num_pixels % 2) {
    const int idx =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[idx];
  }
  const int16_t singleton_s16 = (int16_t)singleton_tap;

  const uint16x4_t max_u16 = vdup_n_u16((uint16_t)((1 << bit_depth) - 1));

  int16_t taps[6];
  for (int k = 0; k < num_sym_taps; ++k) {
    taps[k] = filter[filter_config->config[2 * k][NONSEP_BUF_POS]];
  }

  const int16x4_t taps_0_3 = vld1_s16(&taps[0]);
  int16_t taps_4_5_padded[4] = { taps[4], taps[5], 0, 0 };
  const int16x4_t taps_4_5 = vld1_s16(taps_4_5_padded);

  const int off0 = 1 * stride;
  const int off1 = 1;
  const int off2 = 1 * stride + 1;
  const int off3 = -1 * stride + 1;
  const int off4 = 2 * stride;
  const int off5 = 2;

  for (int r = 0; r < 4; ++r) {
    const int row = block_row_begin + r;
    const uint16_t *center = dgd + row * stride + block_col_begin;

    int16x4_t cen = vreinterpret_s16_u16(vld1_u16(center));
    int32x4_t acc = vmull_n_s16(cen, singleton_s16);

#define PAIR_7T_LANE(off, taps_reg, lane)                           \
  do {                                                              \
    int16x4_t neg = vreinterpret_s16_u16(vld1_u16(center - (off))); \
    int16x4_t pos = vreinterpret_s16_u16(vld1_u16(center + (off))); \
    int16x4_t sum = vadd_s16(neg, pos);                             \
    acc = vmlal_lane_s16(acc, sum, (taps_reg), (lane));             \
  } while (0)

    PAIR_7T_LANE(off0, taps_0_3, 0);
    PAIR_7T_LANE(off1, taps_0_3, 1);
    PAIR_7T_LANE(off2, taps_0_3, 2);
    PAIR_7T_LANE(off3, taps_0_3, 3);
    PAIR_7T_LANE(off4, taps_4_5, 0);
    PAIR_7T_LANE(off5, taps_4_5, 1);

#undef PAIR_7T_LANE

    vst1_u16(dst + row * dst_stride + block_col_begin,
             round_clip_narrow_4(acc, max_u16, prec_bits));
  }
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_16tap_4x4_body(
    const uint16_t *dgd, int stride, const int16_t *filter, const int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin, const int h) {
  const int16_t singleton_s16 = (int16_t)((1 << prec_bits) + filter[16]);
  const uint16x4_t max_u16 = vdup_n_u16((uint16_t)((1 << bit_depth) - 1));

  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const int16x4_t taps_12_15 = vld1_s16(&filter[12]);

  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  const uint16_t *rm1 = r0 - st;
  const uint16_t *rm2 = rm1 - st;
  const uint16_t *rm3 = rm2 - st;
  const uint16_t *rm4 = rm3 - st;
  const uint16_t *r1 = r0 + st;
  const uint16_t *r2 = r1 + st;
  const uint16_t *r3 = r2 + st;
  const uint16_t *r4 = r3 + st;
  const uint16_t *r5 = r4 + st;
  const uint16_t *r6 = r5 + st;
  const uint16_t *r7 = r6 + st;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;

  // Stores after each row pair: deferring all four shares more views but
  // spills.
  const int32x4_t acc0 =
      sym_diamond4_4(rm4, rm3, rm2, rm1, r0, r1, r2, r3, r4, singleton_s16,
                     taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  const int32x4_t acc1 =
      sym_diamond4_4(rm3, rm2, rm1, r0, r1, r2, r3, r4, r5, singleton_s16,
                     taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  vst1_u16(d, round_clip_narrow_4(acc0, max_u16, prec_bits));
  vst1_u16(d + dst_stride, round_clip_narrow_4(acc1, max_u16, prec_bits));
  if (h == 2) return;
  const int32x4_t acc2 =
      sym_diamond4_4(rm2, rm1, r0, r1, r2, r3, r4, r5, r6, singleton_s16,
                     taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  const int32x4_t acc3 =
      sym_diamond4_4(rm1, r0, r1, r2, r3, r4, r5, r6, r7, singleton_s16,
                     taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  vst1_u16(d + 2 * dst_stride, round_clip_narrow_4(acc2, max_u16, prec_bits));
  vst1_u16(d + 3 * dst_stride, round_clip_narrow_4(acc3, max_u16, prec_bits));
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x4_p7_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_4x4_body(dgd, stride, filter, 7, dst,
                                           dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 4);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x4_p12_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_4x4_body(dgd, stride, filter, 12, dst,
                                           dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 4);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x2_p7_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_4x4_body(dgd, stride, filter, 7, dst,
                                           dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 2);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_4x2_p12_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_4x4_body(dgd, stride, filter, 12, dst,
                                           dst_stride, bit_depth,
                                           block_row_begin, block_col_begin, 2);
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_16tap_4xh_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin, const int h) {
  if (prec_bits == 7 && sym_fits_s16(vld1q_s16(filter), vld1q_s16(filter + 8),
                                     filter[16], bit_depth)) {
    if (h == 4) {
      convolve_symmetric_highbd_16tap_4x4_s16_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    } else {
      convolve_symmetric_highbd_16tap_4x2_s16_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    }
  } else if (prec_bits == 12) {
    if (h == 4) {
      convolve_symmetric_highbd_16tap_4x4_p12_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    } else {
      convolve_symmetric_highbd_16tap_4x2_p12_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    }
  } else {
    assert(prec_bits == 7);
    if (h == 4) {
      convolve_symmetric_highbd_16tap_4x4_p7_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    } else {
      convolve_symmetric_highbd_16tap_4x2_p7_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    }
  }
}

// Out of line so the 4x4 paths do not pay its stack frame.
static WIENER_NOINLINE void convolve_symmetric_highbd_generic_neon(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  const int num_sym_taps = filter_config->num_pixels / 2;

  int32_t singleton_tap = 1 << filter_config->prec_bits;
  if (filter_config->num_pixels % 2) {
    const int idx =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[idx];
  }

  assert(num_sym_taps <= 24);
  intptr_t byte_offs[48];
  int16_t filter_taps[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    intptr_t elem_off = (intptr_t)r * stride + c;
    byte_offs[2 * k] = -elem_off * (intptr_t)sizeof(uint16_t);
    byte_offs[2 * k + 1] = elem_off * (intptr_t)sizeof(uint16_t);
    filter_taps[k] = filter[filter_config->config[2 * k][NONSEP_BUF_POS]];
  }

  const int prec_bits = filter_config->prec_bits;
  const int16_t singleton_tap_s16 = (int16_t)singleton_tap;
  const uint16x8_t max_pixel_u16 =
      vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));

#define LOAD_POS(base, k)          \
  vreinterpretq_s16_u16(vld1q_u16( \
      (const uint16_t *)((const char *)(base) + byte_offs[2 * (k) + 1])))
#define LOAD_NEG(base, k)          \
  vreinterpretq_s16_u16(vld1q_u16( \
      (const uint16_t *)((const char *)(base) + byte_offs[2 * (k)])))

  for (int r = block_row_begin; r < block_row_end; ++r) {
    int c = block_col_begin;
    const int dgd_row = r * stride;
    const int dst_row = r * dst_stride;

    for (; c + 32 <= block_col_end; c += 32) {
      const uint16_t *center0 = dgd + dgd_row + c;
      const uint16_t *center1 = center0 + 8;
      const uint16_t *center2 = center0 + 16;
      const uint16_t *center3 = center0 + 24;
      int16x8_t cen0_s16 = vreinterpretq_s16_u16(vld1q_u16(center0));
      int16x8_t cen1_s16 = vreinterpretq_s16_u16(vld1q_u16(center1));
      int16x8_t cen2_s16 = vreinterpretq_s16_u16(vld1q_u16(center2));
      int16x8_t cen3_s16 = vreinterpretq_s16_u16(vld1q_u16(center3));

      int32x4_t a0_lo = vmull_n_s16(vget_low_s16(cen0_s16), singleton_tap_s16);
      int32x4_t a0_hi = vmull_n_s16(vget_high_s16(cen0_s16), singleton_tap_s16);
      int32x4_t a1_lo = vmull_n_s16(vget_low_s16(cen1_s16), singleton_tap_s16);
      int32x4_t a1_hi = vmull_n_s16(vget_high_s16(cen1_s16), singleton_tap_s16);
      int32x4_t a2_lo = vmull_n_s16(vget_low_s16(cen2_s16), singleton_tap_s16);
      int32x4_t a2_hi = vmull_n_s16(vget_high_s16(cen2_s16), singleton_tap_s16);
      int32x4_t a3_lo = vmull_n_s16(vget_low_s16(cen3_s16), singleton_tap_s16);
      int32x4_t a3_hi = vmull_n_s16(vget_high_s16(cen3_s16), singleton_tap_s16);

      int k = 0;
      for (; k + 3 < num_sym_taps; k += 4) {
        int16x4_t taps = vld1_s16(&filter_taps[k]);

        int16x8_t sum0 = vaddq_s16(LOAD_NEG(center0, k), LOAD_POS(center0, k));
        int16x8_t sum1 = vaddq_s16(LOAD_NEG(center1, k), LOAD_POS(center1, k));
        int16x8_t sum2 = vaddq_s16(LOAD_NEG(center2, k), LOAD_POS(center2, k));
        int16x8_t sum3 = vaddq_s16(LOAD_NEG(center3, k), LOAD_POS(center3, k));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0), taps, 0);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0), taps, 0);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1), taps, 0);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1), taps, 0);
        a2_lo = vmlal_lane_s16(a2_lo, vget_low_s16(sum2), taps, 0);
        a2_hi = vmlal_lane_s16(a2_hi, vget_high_s16(sum2), taps, 0);
        a3_lo = vmlal_lane_s16(a3_lo, vget_low_s16(sum3), taps, 0);
        a3_hi = vmlal_lane_s16(a3_hi, vget_high_s16(sum3), taps, 0);

        sum0 = vaddq_s16(LOAD_NEG(center0, k + 1), LOAD_POS(center0, k + 1));
        sum1 = vaddq_s16(LOAD_NEG(center1, k + 1), LOAD_POS(center1, k + 1));
        sum2 = vaddq_s16(LOAD_NEG(center2, k + 1), LOAD_POS(center2, k + 1));
        sum3 = vaddq_s16(LOAD_NEG(center3, k + 1), LOAD_POS(center3, k + 1));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0), taps, 1);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0), taps, 1);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1), taps, 1);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1), taps, 1);
        a2_lo = vmlal_lane_s16(a2_lo, vget_low_s16(sum2), taps, 1);
        a2_hi = vmlal_lane_s16(a2_hi, vget_high_s16(sum2), taps, 1);
        a3_lo = vmlal_lane_s16(a3_lo, vget_low_s16(sum3), taps, 1);
        a3_hi = vmlal_lane_s16(a3_hi, vget_high_s16(sum3), taps, 1);

        sum0 = vaddq_s16(LOAD_NEG(center0, k + 2), LOAD_POS(center0, k + 2));
        sum1 = vaddq_s16(LOAD_NEG(center1, k + 2), LOAD_POS(center1, k + 2));
        sum2 = vaddq_s16(LOAD_NEG(center2, k + 2), LOAD_POS(center2, k + 2));
        sum3 = vaddq_s16(LOAD_NEG(center3, k + 2), LOAD_POS(center3, k + 2));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0), taps, 2);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0), taps, 2);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1), taps, 2);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1), taps, 2);
        a2_lo = vmlal_lane_s16(a2_lo, vget_low_s16(sum2), taps, 2);
        a2_hi = vmlal_lane_s16(a2_hi, vget_high_s16(sum2), taps, 2);
        a3_lo = vmlal_lane_s16(a3_lo, vget_low_s16(sum3), taps, 2);
        a3_hi = vmlal_lane_s16(a3_hi, vget_high_s16(sum3), taps, 2);

        sum0 = vaddq_s16(LOAD_NEG(center0, k + 3), LOAD_POS(center0, k + 3));
        sum1 = vaddq_s16(LOAD_NEG(center1, k + 3), LOAD_POS(center1, k + 3));
        sum2 = vaddq_s16(LOAD_NEG(center2, k + 3), LOAD_POS(center2, k + 3));
        sum3 = vaddq_s16(LOAD_NEG(center3, k + 3), LOAD_POS(center3, k + 3));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0), taps, 3);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0), taps, 3);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1), taps, 3);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1), taps, 3);
        a2_lo = vmlal_lane_s16(a2_lo, vget_low_s16(sum2), taps, 3);
        a2_hi = vmlal_lane_s16(a2_hi, vget_high_s16(sum2), taps, 3);
        a3_lo = vmlal_lane_s16(a3_lo, vget_low_s16(sum3), taps, 3);
        a3_hi = vmlal_lane_s16(a3_hi, vget_high_s16(sum3), taps, 3);
      }
      for (; k < num_sym_taps; ++k) {
        const int16_t tap = filter_taps[k];
        int16x8_t sum0 = vaddq_s16(LOAD_NEG(center0, k), LOAD_POS(center0, k));
        int16x8_t sum1 = vaddq_s16(LOAD_NEG(center1, k), LOAD_POS(center1, k));
        int16x8_t sum2 = vaddq_s16(LOAD_NEG(center2, k), LOAD_POS(center2, k));
        int16x8_t sum3 = vaddq_s16(LOAD_NEG(center3, k), LOAD_POS(center3, k));
        a0_lo = vmlal_n_s16(a0_lo, vget_low_s16(sum0), tap);
        a0_hi = vmlal_n_s16(a0_hi, vget_high_s16(sum0), tap);
        a1_lo = vmlal_n_s16(a1_lo, vget_low_s16(sum1), tap);
        a1_hi = vmlal_n_s16(a1_hi, vget_high_s16(sum1), tap);
        a2_lo = vmlal_n_s16(a2_lo, vget_low_s16(sum2), tap);
        a2_hi = vmlal_n_s16(a2_hi, vget_high_s16(sum2), tap);
        a3_lo = vmlal_n_s16(a3_lo, vget_low_s16(sum3), tap);
        a3_hi = vmlal_n_s16(a3_hi, vget_high_s16(sum3), tap);
      }

      vst1q_u16(dst + dst_row + c,
                round_clip_narrow_pair(a0_lo, a0_hi, max_pixel_u16, prec_bits));
      vst1q_u16(dst + dst_row + c + 8,
                round_clip_narrow_pair(a1_lo, a1_hi, max_pixel_u16, prec_bits));
      vst1q_u16(dst + dst_row + c + 16,
                round_clip_narrow_pair(a2_lo, a2_hi, max_pixel_u16, prec_bits));
      vst1q_u16(dst + dst_row + c + 24,
                round_clip_narrow_pair(a3_lo, a3_hi, max_pixel_u16, prec_bits));
    }

    for (; c + 16 <= block_col_end; c += 16) {
      const uint16_t *center0 = dgd + dgd_row + c;
      const uint16_t *center1 = center0 + 8;
      int16x8_t cen0_s16 = vreinterpretq_s16_u16(vld1q_u16(center0));
      int16x8_t cen1_s16 = vreinterpretq_s16_u16(vld1q_u16(center1));

      int32x4_t a0_lo = vmull_n_s16(vget_low_s16(cen0_s16), singleton_tap_s16);
      int32x4_t a0_hi = vmull_n_s16(vget_high_s16(cen0_s16), singleton_tap_s16);
      int32x4_t a1_lo = vmull_n_s16(vget_low_s16(cen1_s16), singleton_tap_s16);
      int32x4_t a1_hi = vmull_n_s16(vget_high_s16(cen1_s16), singleton_tap_s16);

      int k = 0;
      for (; k + 3 < num_sym_taps; k += 4) {
        int16x4_t taps = vld1_s16(&filter_taps[k]);

        int16x8_t sum0_0 =
            vaddq_s16(LOAD_NEG(center0, k), LOAD_POS(center0, k));
        int16x8_t sum1_0 =
            vaddq_s16(LOAD_NEG(center1, k), LOAD_POS(center1, k));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0_0), taps, 0);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0_0), taps, 0);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1_0), taps, 0);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1_0), taps, 0);

        int16x8_t sum0_1 =
            vaddq_s16(LOAD_NEG(center0, k + 1), LOAD_POS(center0, k + 1));
        int16x8_t sum1_1 =
            vaddq_s16(LOAD_NEG(center1, k + 1), LOAD_POS(center1, k + 1));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0_1), taps, 1);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0_1), taps, 1);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1_1), taps, 1);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1_1), taps, 1);

        int16x8_t sum0_2 =
            vaddq_s16(LOAD_NEG(center0, k + 2), LOAD_POS(center0, k + 2));
        int16x8_t sum1_2 =
            vaddq_s16(LOAD_NEG(center1, k + 2), LOAD_POS(center1, k + 2));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0_2), taps, 2);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0_2), taps, 2);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1_2), taps, 2);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1_2), taps, 2);

        int16x8_t sum0_3 =
            vaddq_s16(LOAD_NEG(center0, k + 3), LOAD_POS(center0, k + 3));
        int16x8_t sum1_3 =
            vaddq_s16(LOAD_NEG(center1, k + 3), LOAD_POS(center1, k + 3));
        a0_lo = vmlal_lane_s16(a0_lo, vget_low_s16(sum0_3), taps, 3);
        a0_hi = vmlal_lane_s16(a0_hi, vget_high_s16(sum0_3), taps, 3);
        a1_lo = vmlal_lane_s16(a1_lo, vget_low_s16(sum1_3), taps, 3);
        a1_hi = vmlal_lane_s16(a1_hi, vget_high_s16(sum1_3), taps, 3);
      }
      for (; k < num_sym_taps; ++k) {
        const int16_t tap = filter_taps[k];
        int16x8_t sum0 = vaddq_s16(LOAD_NEG(center0, k), LOAD_POS(center0, k));
        int16x8_t sum1 = vaddq_s16(LOAD_NEG(center1, k), LOAD_POS(center1, k));
        a0_lo = vmlal_n_s16(a0_lo, vget_low_s16(sum0), tap);
        a0_hi = vmlal_n_s16(a0_hi, vget_high_s16(sum0), tap);
        a1_lo = vmlal_n_s16(a1_lo, vget_low_s16(sum1), tap);
        a1_hi = vmlal_n_s16(a1_hi, vget_high_s16(sum1), tap);
      }

      vst1q_u16(dst + dst_row + c,
                round_clip_narrow_pair(a0_lo, a0_hi, max_pixel_u16, prec_bits));
      vst1q_u16(dst + dst_row + c + 8,
                round_clip_narrow_pair(a1_lo, a1_hi, max_pixel_u16, prec_bits));
    }

    for (; c + 8 <= block_col_end; c += 8) {
      const uint16_t *center = dgd + dgd_row + c;
      int16x8_t center_s16 = vreinterpretq_s16_u16(vld1q_u16(center));

      int32x4_t acc_lo =
          vmull_n_s16(vget_low_s16(center_s16), singleton_tap_s16);
      int32x4_t acc_hi =
          vmull_n_s16(vget_high_s16(center_s16), singleton_tap_s16);

      for (int k = 0; k < num_sym_taps; ++k) {
        int16x8_t sum = vaddq_s16(LOAD_NEG(center, k), LOAD_POS(center, k));
        const int16_t tap = filter_taps[k];
        acc_lo = vmlal_n_s16(acc_lo, vget_low_s16(sum), tap);
        acc_hi = vmlal_n_s16(acc_hi, vget_high_s16(sum), tap);
      }

      vst1q_u16(
          dst + dst_row + c,
          round_clip_narrow_pair(acc_lo, acc_hi, max_pixel_u16, prec_bits));
    }

    for (; c < block_col_end; ++c) {
      const uint16_t *cen_ptr = dgd + dgd_row + c;
      int32_t tmp = singleton_tap * (int32_t)(*cen_ptr);
      for (int k = 0; k < num_sym_taps; ++k) {
        const uint16_t *neg_ptr =
            (const uint16_t *)((const char *)cen_ptr + byte_offs[2 * k]);
        const uint16_t *pos_ptr =
            (const uint16_t *)((const char *)cen_ptr + byte_offs[2 * k + 1]);
        tmp +=
            (int32_t)filter_taps[k] * ((int16_t)*neg_ptr + (int16_t)*pos_ptr);
      }
      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, prec_bits);
      dst[dst_row + c] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
#undef LOAD_POS
#undef LOAD_NEG
}

// Out of line so the pointer-matched fast path has no call or stack frame.
static WIENER_NOINLINE void convolve_symmetric_highbd_dispatch_neon(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;

  // large_config_y first: config_y is its prefix, so a memcmp of num_pixels >
  // 25 would read past config_y.
  if (LIKELY(num_rows == 4 && num_cols == 4)) {
    const int num_sym_taps = filter_config->num_pixels / 2;

    if (num_sym_taps == 16 &&
        (filter_config->config == wienerns_simd_large_config_y ||
         !memcmp(wienerns_simd_large_config_y, filter_config->config,
                 filter_config->num_pixels * 3 *
                     sizeof(filter_config->config[0][0])))) {
      if (vget_lane_u64(vreinterpret_u64_s16(vld1_s16(filter + 12)), 0) == 0) {
        convolve_symmetric_highbd_13tap_4xh_neon(
            dgd, stride, filter, filter[16], filter_config->prec_bits, dst,
            dst_stride, bit_depth, block_row_begin, block_col_begin, 4);
        return;
      }
      convolve_symmetric_highbd_16tap_4xh_neon(
          dgd, stride, filter, filter_config->prec_bits, dst, dst_stride,
          bit_depth, block_row_begin, block_col_begin, 4);
      return;
    }
    if (filter_config->config == wienerns_simd_config_y ||
        !memcmp(wienerns_simd_config_y, filter_config->config,
                filter_config->num_pixels * 3 *
                    sizeof(filter_config->config[0][0]))) {
      convolve_symmetric_highbd_13tap_4xh_neon(
          dgd, stride, filter, filter[12], filter_config->prec_bits, dst,
          dst_stride, bit_depth, block_row_begin, block_col_begin, 4);
      return;
    }
    if (num_sym_taps == 6 &&
        (filter_config->config == wienerns_simd_config_uv_from_uvonly ||
         !memcmp(wienerns_simd_config_uv_from_uvonly, filter_config->config,
                 filter_config->num_pixels * 3 *
                     sizeof(filter_config->config[0][0])))) {
      convolve_symmetric_highbd_7tap_4x4_neon(
          dgd, stride, filter_config, filter, filter_config->prec_bits, dst,
          dst_stride, bit_depth, block_row_begin, block_col_begin);
      return;
    }
  }

  if (num_rows == 2 && num_cols == 4) {
    if (filter_config->num_pixels / 2 == 16 &&
        (filter_config->config == wienerns_simd_large_config_y ||
         !memcmp(wienerns_simd_large_config_y, filter_config->config,
                 filter_config->num_pixels * 3 *
                     sizeof(filter_config->config[0][0])))) {
      if (vget_lane_u64(vreinterpret_u64_s16(vld1_s16(filter + 12)), 0) == 0) {
        convolve_symmetric_highbd_13tap_4xh_neon(
            dgd, stride, filter, filter[16], filter_config->prec_bits, dst,
            dst_stride, bit_depth, block_row_begin, block_col_begin, 2);
        return;
      }
      convolve_symmetric_highbd_16tap_4xh_neon(
          dgd, stride, filter, filter_config->prec_bits, dst, dst_stride,
          bit_depth, block_row_begin, block_col_begin, 2);
      return;
    }
    if (filter_config->config == wienerns_simd_config_y ||
        !memcmp(wienerns_simd_config_y, filter_config->config,
                filter_config->num_pixels * 3 *
                    sizeof(filter_config->config[0][0]))) {
      convolve_symmetric_highbd_13tap_4xh_neon(
          dgd, stride, filter, filter[12], filter_config->prec_bits, dst,
          dst_stride, bit_depth, block_row_begin, block_col_begin, 2);
      return;
    }
  }

  convolve_symmetric_highbd_generic_neon(
      dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
      block_row_begin, block_row_end, block_col_begin, block_col_end);
}

void av2_convolve_symmetric_highbd_neon(const uint16_t *dgd, int stride,
                                        const NonsepFilterConfig *filter_config,
                                        const int16_t *filter, uint16_t *dst,
                                        int dst_stride, int bit_depth,
                                        int block_row_begin, int block_row_end,
                                        int block_col_begin,
                                        int block_col_end) {
  assert(!filter_config->subtract_center);

  if (LIKELY(block_row_end - block_row_begin == 4 &&
             block_col_end - block_col_begin == 4)) {
    const int num_sym_taps = filter_config->num_pixels / 2;
    const int(*const config)[3] = filter_config->config;
    if (num_sym_taps == 16 && config == wienerns_simd_large_config_y) {
      if (vget_lane_u64(vreinterpret_u64_s16(vld1_s16(filter + 12)), 0) == 0) {
        convolve_symmetric_highbd_13tap_4xh_neon(
            dgd, stride, filter, filter[16], filter_config->prec_bits, dst,
            dst_stride, bit_depth, block_row_begin, block_col_begin, 4);
        return;
      }
      convolve_symmetric_highbd_16tap_4xh_neon(
          dgd, stride, filter, filter_config->prec_bits, dst, dst_stride,
          bit_depth, block_row_begin, block_col_begin, 4);
      return;
    }
    if (config == wienerns_simd_config_y) {
      convolve_symmetric_highbd_13tap_4xh_neon(
          dgd, stride, filter, filter[12], filter_config->prec_bits, dst,
          dst_stride, bit_depth, block_row_begin, block_col_begin, 4);
      return;
    }
  }

  convolve_symmetric_highbd_dispatch_neon(
      dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
      block_row_begin, block_row_end, block_col_begin, block_col_end);
}

#define SYM8_PAIR(pos_ptr, neg_ptr, taps_reg, lane)                            \
  do {                                                                         \
    const int16x8_t sum =                                                      \
        vaddq_s16(vreinterpretq_s16_u16(vld1q_u16(neg_ptr)),                   \
                  vreinterpretq_s16_u16(vld1q_u16(pos_ptr)));                  \
    *acc_lo = vmlal_lane_s16(*acc_lo, vget_low_s16(sum), (taps_reg), (lane));  \
    *acc_hi = vmlal_lane_s16(*acc_hi, vget_high_s16(sum), (taps_reg), (lane)); \
  } while (0)

static AVM_FORCE_INLINE void sym_diamond3_8(
    const uint16_t *m3, const uint16_t *m2, const uint16_t *m1,
    const uint16_t *c, const uint16_t *p1, const uint16_t *p2,
    const uint16_t *p3, int16_t singleton, int16x4_t taps_0_3,
    int16x4_t taps_4_7, int16x4_t taps_8_11, int32x4_t *acc_lo,
    int32x4_t *acc_hi) {
  const int16x8_t c8 = vreinterpretq_s16_u16(vld1q_u16(c));
  *acc_lo = vmull_n_s16(vget_low_s16(c8), singleton);
  *acc_hi = vmull_n_s16(vget_high_s16(c8), singleton);
  SYM8_PAIR(p1, m1, taps_0_3, 0);
  SYM8_PAIR(c + 1, c - 1, taps_0_3, 1);
  SYM8_PAIR(p2, m2, taps_0_3, 2);
  SYM8_PAIR(c + 2, c - 2, taps_0_3, 3);
  SYM8_PAIR(p1 + 1, m1 - 1, taps_4_7, 0);
  SYM8_PAIR(m1 + 1, p1 - 1, taps_4_7, 1);
  SYM8_PAIR(p2 + 1, m2 - 1, taps_4_7, 2);
  SYM8_PAIR(p2 - 1, m2 + 1, taps_4_7, 3);
  SYM8_PAIR(p1 + 2, m1 - 2, taps_8_11, 0);
  SYM8_PAIR(p1 - 2, m1 + 2, taps_8_11, 1);
  SYM8_PAIR(p3, m3, taps_8_11, 2);
  SYM8_PAIR(c + 3, c - 3, taps_8_11, 3);
}

static AVM_FORCE_INLINE void sym_diamond4_8(
    const uint16_t *m4, const uint16_t *m3, const uint16_t *m2,
    const uint16_t *m1, const uint16_t *c, const uint16_t *p1,
    const uint16_t *p2, const uint16_t *p3, const uint16_t *p4,
    int16_t singleton, int16x4_t taps_0_3, int16x4_t taps_4_7,
    int16x4_t taps_8_11, int16x4_t taps_12_15, int32x4_t *acc_lo,
    int32x4_t *acc_hi) {
  sym_diamond3_8(m3, m2, m1, c, p1, p2, p3, singleton, taps_0_3, taps_4_7,
                 taps_8_11, acc_lo, acc_hi);
  SYM8_PAIR(p4, m4, taps_12_15, 0);
  SYM8_PAIR(c + 4, c - 4, taps_12_15, 1);
  SYM8_PAIR(p3 + 3, m3 - 3, taps_12_15, 2);
  SYM8_PAIR(p3 - 3, m3 + 3, taps_12_15, 3);
}

#undef SYM8_PAIR

static AVM_FORCE_INLINE void convolve_symmetric_highbd_13tap_8xh_s32_body(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin, const int h) {
  const int16_t singleton_s16 = (int16_t)((1 << prec_bits) + center_tap);
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));

  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);

  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;

  for (int r = 0; r < h; r += 2) {
    const uint16_t *rm1 = r0 - st;
    const uint16_t *rm2 = rm1 - st;
    const uint16_t *rm3 = rm2 - st;
    const uint16_t *r1 = r0 + st;
    const uint16_t *r2 = r1 + st;
    const uint16_t *r3 = r2 + st;
    const uint16_t *r4 = r3 + st;
    int32x4_t lo0, hi0, lo1, hi1;
    sym_diamond3_8(rm3, rm2, rm1, r0, r1, r2, r3, singleton_s16, taps_0_3,
                   taps_4_7, taps_8_11, &lo0, &hi0);
    sym_diamond3_8(rm2, rm1, r0, r1, r2, r3, r4, singleton_s16, taps_0_3,
                   taps_4_7, taps_8_11, &lo1, &hi1);
    vst1q_u16(d, round_clip_narrow_pair(lo0, hi0, max_u16, prec_bits));
    vst1q_u16(d + dst_stride,
              round_clip_narrow_pair(lo1, hi1, max_u16, prec_bits));
    r0 += 2 * st;
    d += 2 * dst_stride;
  }
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_16tap_8xh_s32_body(
    const uint16_t *dgd, int stride, const int16_t *filter, int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin, const int h) {
  const int16_t singleton_s16 = (int16_t)((1 << prec_bits) + filter[16]);
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));

  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const int16x4_t taps_12_15 = vld1_s16(&filter[12]);

  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;

  for (int r = 0; r < h; r += 2) {
    const uint16_t *rm1 = r0 - st;
    const uint16_t *rm2 = rm1 - st;
    const uint16_t *rm3 = rm2 - st;
    const uint16_t *rm4 = rm3 - st;
    const uint16_t *r1 = r0 + st;
    const uint16_t *r2 = r1 + st;
    const uint16_t *r3 = r2 + st;
    const uint16_t *r4 = r3 + st;
    const uint16_t *r5 = r4 + st;
    int32x4_t lo0, hi0, lo1, hi1;
    sym_diamond4_8(rm4, rm3, rm2, rm1, r0, r1, r2, r3, r4, singleton_s16,
                   taps_0_3, taps_4_7, taps_8_11, taps_12_15, &lo0, &hi0);
    sym_diamond4_8(rm3, rm2, rm1, r0, r1, r2, r3, r4, r5, singleton_s16,
                   taps_0_3, taps_4_7, taps_8_11, taps_12_15, &lo1, &hi1);
    vst1q_u16(d, round_clip_narrow_pair(lo0, hi0, max_u16, prec_bits));
    vst1q_u16(d + dst_stride,
              round_clip_narrow_pair(lo1, hi1, max_u16, prec_bits));
    r0 += 2 * st;
    d += 2 * dst_stride;
  }
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_8x8_s32_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_13tap_8xh_s32_body(
      dgd, stride, filter, center_tap, prec_bits, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 8);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_8x6_s32_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_13tap_8xh_s32_body(
      dgd, stride, filter, center_tap, prec_bits, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 6);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_8x8_s32_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_16tap_8xh_s32_body(
      dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 8);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_8x6_s32_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_16tap_8xh_s32_body(
      dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 6);
}

static AVM_FORCE_INLINE int16x8_t sym8_pair_sum(const uint16_t *pos_ptr,
                                                const uint16_t *neg_ptr) {
  return vaddq_s16(vreinterpretq_s16_u16(vld1q_u16(neg_ptr)),
                   vreinterpretq_s16_u16(vld1q_u16(pos_ptr)));
}

#define SYM8_PAIR16(acc, pos_ptr, neg_ptr, taps_reg, lane)                     \
  acc = vmlaq_lane_s16((acc), sym8_pair_sum((pos_ptr), (neg_ptr)), (taps_reg), \
                       (lane))

// Chains joined by vqaddq as in store_4x4_s16; sym_fits_s16 bounds each.
static AVM_FORCE_INLINE int16x8_t
sym_diamond3_8_s16(const uint16_t *m3, const uint16_t *m2, const uint16_t *m1,
                   const uint16_t *c, const uint16_t *p1, const uint16_t *p2,
                   const uint16_t *p3, int16_t center_tap, int16x4_t taps_0_3,
                   int16x4_t taps_4_7, int16x4_t taps_8_11) {
  int16x8_t a = vmulq_n_s16(vreinterpretq_s16_u16(vld1q_u16(c)), center_tap);
  int16x8_t b = vmulq_lane_s16(sym8_pair_sum(p1 + 1, m1 - 1), taps_4_7, 0);
  SYM8_PAIR16(a, p1, m1, taps_0_3, 0);
  SYM8_PAIR16(b, m1 + 1, p1 - 1, taps_4_7, 1);
  SYM8_PAIR16(a, c + 1, c - 1, taps_0_3, 1);
  SYM8_PAIR16(b, p2 + 1, m2 - 1, taps_4_7, 2);
  SYM8_PAIR16(a, p2, m2, taps_0_3, 2);
  SYM8_PAIR16(b, p2 - 1, m2 + 1, taps_4_7, 3);
  SYM8_PAIR16(a, c + 2, c - 2, taps_0_3, 3);
  SYM8_PAIR16(b, p1 + 2, m1 - 2, taps_8_11, 0);
  SYM8_PAIR16(a, p3, m3, taps_8_11, 2);
  SYM8_PAIR16(b, p1 - 2, m1 + 2, taps_8_11, 1);
  SYM8_PAIR16(a, c + 3, c - 3, taps_8_11, 3);
  return vqaddq_s16(a, b);
}

static AVM_FORCE_INLINE int16x8_t
sym_diamond4_8_s16(const uint16_t *m4, const uint16_t *m3, const uint16_t *m2,
                   const uint16_t *m1, const uint16_t *c, const uint16_t *p1,
                   const uint16_t *p2, const uint16_t *p3, const uint16_t *p4,
                   int16_t center_tap, int16x4_t taps_0_3, int16x4_t taps_4_7,
                   int16x4_t taps_8_11, int16x4_t taps_12_15) {
  int16x8_t a = vmulq_n_s16(vreinterpretq_s16_u16(vld1q_u16(c)), center_tap);
  int16x8_t b = vmulq_lane_s16(sym8_pair_sum(p1 + 1, m1 - 1), taps_4_7, 0);
  SYM8_PAIR16(a, p1, m1, taps_0_3, 0);
  SYM8_PAIR16(b, m1 + 1, p1 - 1, taps_4_7, 1);
  SYM8_PAIR16(a, c + 1, c - 1, taps_0_3, 1);
  SYM8_PAIR16(b, p2 + 1, m2 - 1, taps_4_7, 2);
  SYM8_PAIR16(a, p2, m2, taps_0_3, 2);
  SYM8_PAIR16(b, p2 - 1, m2 + 1, taps_4_7, 3);
  SYM8_PAIR16(a, c + 2, c - 2, taps_0_3, 3);
  SYM8_PAIR16(b, p1 + 2, m1 - 2, taps_8_11, 0);
  SYM8_PAIR16(a, p3, m3, taps_8_11, 2);
  SYM8_PAIR16(b, p1 - 2, m1 + 2, taps_8_11, 1);
  SYM8_PAIR16(a, c + 3, c - 3, taps_8_11, 3);
  SYM8_PAIR16(b, p3 + 3, m3 - 3, taps_12_15, 2);
  SYM8_PAIR16(a, p4, m4, taps_12_15, 0);
  SYM8_PAIR16(b, p3 - 3, m3 + 3, taps_12_15, 3);
  SYM8_PAIR16(a, c + 4, c - 4, taps_12_15, 1);
  return vqaddq_s16(a, b);
}

#undef SYM8_PAIR16

static AVM_FORCE_INLINE uint16x8_t round_clip_s16_p7(const uint16_t *c,
                                                     int16x8_t r,
                                                     uint16x8_t max_u16) {
  return round_clip_s16_p7_v(vreinterpretq_s16_u16(vld1q_u16(c)), r, max_u16);
}

static AVM_FORCE_INLINE void sym13_8x2_s16(
    const uint16_t *r0, ptrdiff_t st, uint16_t *d, int dst_stride,
    int16_t center_tap, int16x4_t taps_0_3, int16x4_t taps_4_7,
    int16x4_t taps_8_11, uint16x8_t max_u16) {
  const uint16_t *rm1 = r0 - st;
  const uint16_t *rm2 = rm1 - st;
  const uint16_t *rm3 = rm2 - st;
  const uint16_t *r1 = r0 + st;
  const uint16_t *r2 = r1 + st;
  const uint16_t *r3 = r2 + st;
  const uint16_t *r4 = r3 + st;
  const int16x8_t acc0 = sym_diamond3_8_s16(
      rm3, rm2, rm1, r0, r1, r2, r3, center_tap, taps_0_3, taps_4_7, taps_8_11);
  const int16x8_t acc1 = sym_diamond3_8_s16(
      rm2, rm1, r0, r1, r2, r3, r4, center_tap, taps_0_3, taps_4_7, taps_8_11);
  const uint16x8_t out0 = round_clip_s16_p7(r0, acc0, max_u16);
  const uint16x8_t out1 = round_clip_s16_p7(r1, acc1, max_u16);
  vst1q_u16(d, out0);
  vst1q_u16(d + dst_stride, out1);
}

static AVM_FORCE_INLINE void sym16_8x2_s16(
    const uint16_t *r0, ptrdiff_t st, uint16_t *d, int dst_stride,
    int16_t center_tap, int16x4_t taps_0_3, int16x4_t taps_4_7,
    int16x4_t taps_8_11, int16x4_t taps_12_15, uint16x8_t max_u16) {
  const uint16_t *rm1 = r0 - st;
  const uint16_t *rm2 = rm1 - st;
  const uint16_t *rm3 = rm2 - st;
  const uint16_t *rm4 = rm3 - st;
  const uint16_t *r1 = r0 + st;
  const uint16_t *r2 = r1 + st;
  const uint16_t *r3 = r2 + st;
  const uint16_t *r4 = r3 + st;
  const uint16_t *r5 = r4 + st;
  const int16x8_t acc0 =
      sym_diamond4_8_s16(rm4, rm3, rm2, rm1, r0, r1, r2, r3, r4, center_tap,
                         taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  const int16x8_t acc1 =
      sym_diamond4_8_s16(rm3, rm2, rm1, r0, r1, r2, r3, r4, r5, center_tap,
                         taps_0_3, taps_4_7, taps_8_11, taps_12_15);
  const uint16x8_t out0 = round_clip_s16_p7(r0, acc0, max_u16);
  const uint16x8_t out1 = round_clip_s16_p7(r1, acc1, max_u16);
  vst1q_u16(d, out0);
  vst1q_u16(d + dst_stride, out1);
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_13tap_8xh_s16_body(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin, const int h) {
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  sym13_8x2_s16(r0 + 0 * st, st, d + 0 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, max_u16);
  sym13_8x2_s16(r0 + 2 * st, st, d + 2 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, max_u16);
  sym13_8x2_s16(r0 + 4 * st, st, d + 4 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, max_u16);
  if (h == 8) {
    sym13_8x2_s16(r0 + 6 * st, st, d + 6 * dst_stride, dst_stride, center_tap,
                  taps_0_3, taps_4_7, taps_8_11, max_u16);
  }
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_8x8_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_8xh_s16_body(
      dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 8);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_13tap_8x6_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin) {
  convolve_symmetric_highbd_13tap_8xh_s16_body(
      dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
      block_row_begin, block_col_begin, 6);
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_16tap_8xh_s16_body(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin,
    const int h) {
  const uint16x8_t max_u16 = vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));
  const int16_t center_tap = filter[16];
  const int16x4_t taps_0_3 = vld1_s16(&filter[0]);
  const int16x4_t taps_4_7 = vld1_s16(&filter[4]);
  const int16x4_t taps_8_11 = vld1_s16(&filter[8]);
  const int16x4_t taps_12_15 = vld1_s16(&filter[12]);
  const ptrdiff_t st = stride;
  const uint16_t *r0 = dgd + block_row_begin * st + block_col_begin;
  uint16_t *d = dst + block_row_begin * dst_stride + block_col_begin;
  sym16_8x2_s16(r0 + 0 * st, st, d + 0 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, taps_12_15, max_u16);
  sym16_8x2_s16(r0 + 2 * st, st, d + 2 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, taps_12_15, max_u16);
  sym16_8x2_s16(r0 + 4 * st, st, d + 4 * dst_stride, dst_stride, center_tap,
                taps_0_3, taps_4_7, taps_8_11, taps_12_15, max_u16);
  if (h == 8) {
    sym16_8x2_s16(r0 + 6 * st, st, d + 6 * dst_stride, dst_stride, center_tap,
                  taps_0_3, taps_4_7, taps_8_11, taps_12_15, max_u16);
  }
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_8x8_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_8xh_s16_body(
      dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
      block_col_begin, 8);
}

static WIENER_NOINLINE void convolve_symmetric_highbd_16tap_8x6_s16_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, uint16_t *dst,
    int dst_stride, int bit_depth, int block_row_begin, int block_col_begin) {
  convolve_symmetric_highbd_16tap_8xh_s16_body(
      dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
      block_col_begin, 6);
}

// Separate tail calls so neither path pays the other's register saves.
static AVM_FORCE_INLINE void convolve_symmetric_highbd_13tap_8xh_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int16_t center_tap,
    int prec_bits, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_col_begin, const int h) {
  if (prec_bits == 7 &&
      sym_fits_s16(vld1q_s16(filter),
                   vcombine_s16(vld1_s16(filter + 8), vdup_n_s16(0)),
                   center_tap, bit_depth)) {
    if (h == 8) {
      convolve_symmetric_highbd_13tap_8x8_s16_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    } else {
      convolve_symmetric_highbd_13tap_8x6_s16_neon(
          dgd, stride, filter, center_tap, dst, dst_stride, bit_depth,
          block_row_begin, block_col_begin);
    }
    return;
  }
  if (h == 8) {
    convolve_symmetric_highbd_13tap_8x8_s32_neon(
        dgd, stride, filter, center_tap, prec_bits, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  } else {
    convolve_symmetric_highbd_13tap_8x6_s32_neon(
        dgd, stride, filter, center_tap, prec_bits, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  }
}

static AVM_FORCE_INLINE void convolve_symmetric_highbd_16tap_8xh_neon(
    const uint16_t *dgd, int stride, const int16_t *filter, int prec_bits,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_col_begin, const int h) {
  if (prec_bits == 7 && sym_fits_s16(vld1q_s16(filter), vld1q_s16(filter + 8),
                                     filter[16], bit_depth)) {
    if (h == 8) {
      convolve_symmetric_highbd_16tap_8x8_s16_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    } else {
      convolve_symmetric_highbd_16tap_8x6_s16_neon(
          dgd, stride, filter, dst, dst_stride, bit_depth, block_row_begin,
          block_col_begin);
    }
    return;
  }
  if (h == 8) {
    convolve_symmetric_highbd_16tap_8x8_s32_neon(
        dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  } else {
    convolve_symmetric_highbd_16tap_8x6_s32_neon(
        dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
        block_row_begin, block_col_begin);
  }
}

// Out of line so the fast path has no call or stack frame.
static WIENER_NOINLINE void convolve_symmetric_blk8x8_highbd_dispatch_neon(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;
  const int prec_bits = filter_config->prec_bits;

  if (num_cols == 8 && (num_rows == 8 || num_rows == 6)) {
    // large_config_y first (see convolve_symmetric_highbd_dispatch_neon).
    if (filter_config->num_pixels / 2 == 16 &&
        (filter_config->config == wienerns_simd_large_config_y ||
         !memcmp(wienerns_simd_large_config_y, filter_config->config,
                 filter_config->num_pixels * 3 *
                     sizeof(filter_config->config[0][0])))) {
      if (vget_lane_u64(vreinterpret_u64_s16(vld1_s16(filter + 12)), 0) == 0) {
        if (num_rows == 8) {
          convolve_symmetric_highbd_13tap_8xh_neon(
              dgd, stride, filter, filter[16], prec_bits, dst, dst_stride,
              bit_depth, block_row_begin, block_col_begin, 8);
        } else {
          convolve_symmetric_highbd_13tap_8xh_neon(
              dgd, stride, filter, filter[16], prec_bits, dst, dst_stride,
              bit_depth, block_row_begin, block_col_begin, 6);
        }
        return;
      }
      if (num_rows == 8) {
        convolve_symmetric_highbd_16tap_8xh_neon(
            dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
            block_row_begin, block_col_begin, 8);
      } else {
        convolve_symmetric_highbd_16tap_8xh_neon(
            dgd, stride, filter, prec_bits, dst, dst_stride, bit_depth,
            block_row_begin, block_col_begin, 6);
      }
      return;
    }
    if (filter_config->config == wienerns_simd_config_y ||
        !memcmp(wienerns_simd_config_y, filter_config->config,
                filter_config->num_pixels * 3 *
                    sizeof(filter_config->config[0][0]))) {
      if (num_rows == 8) {
        convolve_symmetric_highbd_13tap_8xh_neon(
            dgd, stride, filter, filter[12], prec_bits, dst, dst_stride,
            bit_depth, block_row_begin, block_col_begin, 8);
      } else {
        convolve_symmetric_highbd_13tap_8xh_neon(
            dgd, stride, filter, filter[12], prec_bits, dst, dst_stride,
            bit_depth, block_row_begin, block_col_begin, 6);
      }
      return;
    }
  }

  convolve_symmetric_highbd_dispatch_neon(
      dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
      block_row_begin, block_row_end, block_col_begin, block_col_end);
}

void av2_convolve_symmetric_blk8x8_highbd_neon(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(!filter_config->subtract_center);

  if (LIKELY(block_row_end - block_row_begin == 8 &&
             block_col_end - block_col_begin == 8)) {
    const int(*const config)[3] = filter_config->config;
    if (config == wienerns_simd_config_y) {
      convolve_symmetric_highbd_13tap_8xh_neon(
          dgd, stride, filter, filter[12], filter_config->prec_bits, dst,
          dst_stride, bit_depth, block_row_begin, block_col_begin, 8);
      return;
    }
    if (config == wienerns_simd_large_config_y) {
      if (vget_lane_u64(vreinterpret_u64_s16(vld1_s16(filter + 12)), 0) == 0) {
        convolve_symmetric_highbd_13tap_8xh_neon(
            dgd, stride, filter, filter[16], filter_config->prec_bits, dst,
            dst_stride, bit_depth, block_row_begin, block_col_begin, 8);
        return;
      }
      convolve_symmetric_highbd_16tap_8xh_neon(
          dgd, stride, filter, filter_config->prec_bits, dst, dst_stride,
          bit_depth, block_row_begin, block_col_begin, 8);
      return;
    }
  }

  convolve_symmetric_blk8x8_highbd_dispatch_neon(
      dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
      block_row_begin, block_row_end, block_col_begin, block_col_end);
}

static void convolve_symmetric_dual_highbd_7plus13tap_4x4_neon(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, int prec_bits, uint16_t *dst, int dst_stride,
    int bit_depth, int block_row_begin, int block_col_begin) {
  const int num_sym_taps = filter_config->num_pixels / 2;
  assert(num_sym_taps == 6);
  const int num_taps_dual = filter_config->num_pixels2 & ~1;
  assert(num_taps_dual == 12);

  int32_t singleton_tap = 1 << prec_bits;
  if (filter_config->num_pixels % 2) {
    const int idx =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[idx];
  }
  const int16_t singleton_s16 = (int16_t)singleton_tap;

  int32_t singleton_tap_dual = 0;
  if (filter_config->num_pixels2 % 2) {
    const int last = filter_config->num_pixels2 - 1;
    const int idx = filter_config->config2[last][NONSEP_BUF_POS];
    singleton_tap_dual = filter[idx];
  }

  int16_t taps_main[6];
  for (int k = 0; k < num_sym_taps; ++k) {
    taps_main[k] = filter[filter_config->config[2 * k][NONSEP_BUF_POS]];
  }

  int16_t taps_dual[12];
  for (int k = 0; k < num_taps_dual; ++k) {
    taps_dual[k] = filter[filter_config->config2[k][NONSEP_BUF_POS]];
  }

  const uint16x4_t max_u16 = vdup_n_u16((uint16_t)((1 << bit_depth) - 1));

  const int16x4_t main_0_3 = vld1_s16(&taps_main[0]);
  int16_t main_4_5_padded[4] = { taps_main[4], taps_main[5], 0, 0 };
  const int16x4_t main_4_5 = vld1_s16(main_4_5_padded);

  const int16x4_t dual_0_3 = vld1_s16(&taps_dual[0]);
  const int16x4_t dual_4_7 = vld1_s16(&taps_dual[4]);
  const int16x4_t dual_8_11 = vld1_s16(&taps_dual[8]);

  const int m_off0 = 1 * dgd_stride;
  const int m_off1 = 1;
  const int m_off2 = 1 * dgd_stride + 1;
  const int m_off3 = -1 * dgd_stride + 1;
  const int m_off4 = 2 * dgd_stride;
  const int m_off5 = 2;

  const int d_off0 = 1 * dgd_dual_stride;
  const int d_off1 = -1 * dgd_dual_stride;
  const int d_off2 = 1;
  const int d_off3 = -1;
  const int d_off4 = 1 * dgd_dual_stride + 1;
  const int d_off5 = -1 * dgd_dual_stride - 1;
  const int d_off6 = -1 * dgd_dual_stride + 1;
  const int d_off7 = 1 * dgd_dual_stride - 1;
  const int d_off8 = 2 * dgd_dual_stride;
  const int d_off9 = -2 * dgd_dual_stride;
  const int d_off10 = 2;
  const int d_off11 = -2;

  for (int r = 0; r < 4; ++r) {
    const int row = block_row_begin + r;
    const uint16_t *center = dgd + row * dgd_stride + block_col_begin;
    const uint16_t *center_dual =
        dgd_dual + row * dgd_dual_stride + block_col_begin;

    int16x4_t cen = vreinterpret_s16_u16(vld1_u16(center));
    int32x4_t acc_main = vmull_n_s16(cen, singleton_s16);

#define DUAL_MAIN(off, taps_reg, lane)                              \
  do {                                                              \
    int16x4_t neg = vreinterpret_s16_u16(vld1_u16(center - (off))); \
    int16x4_t pos = vreinterpret_s16_u16(vld1_u16(center + (off))); \
    int16x4_t sum = vadd_s16(neg, pos);                             \
    acc_main = vmlal_lane_s16(acc_main, sum, (taps_reg), (lane));   \
  } while (0)

    DUAL_MAIN(m_off0, main_0_3, 0);
    DUAL_MAIN(m_off1, main_0_3, 1);
    DUAL_MAIN(m_off2, main_0_3, 2);
    DUAL_MAIN(m_off3, main_0_3, 3);
    DUAL_MAIN(m_off4, main_4_5, 0);
    DUAL_MAIN(m_off5, main_4_5, 1);

#undef DUAL_MAIN

    int16x4_t cen_d = vreinterpret_s16_u16(vld1_u16(center_dual));
    int32x4_t acc_dual = vmull_n_s16(cen_d, (int16_t)singleton_tap_dual);

#define DUAL_ASYM(off, taps_reg, lane)                                      \
  do {                                                                      \
    int16x4_t sample = vreinterpret_s16_u16(vld1_u16(center_dual + (off))); \
    acc_dual = vmlal_lane_s16(acc_dual, sample, (taps_reg), (lane));        \
  } while (0)

    DUAL_ASYM(d_off0, dual_0_3, 0);
    DUAL_ASYM(d_off1, dual_0_3, 1);
    DUAL_ASYM(d_off2, dual_0_3, 2);
    DUAL_ASYM(d_off3, dual_0_3, 3);
    DUAL_ASYM(d_off4, dual_4_7, 0);
    DUAL_ASYM(d_off5, dual_4_7, 1);
    DUAL_ASYM(d_off6, dual_4_7, 2);
    DUAL_ASYM(d_off7, dual_4_7, 3);
    DUAL_ASYM(d_off8, dual_8_11, 0);
    DUAL_ASYM(d_off9, dual_8_11, 1);
    DUAL_ASYM(d_off10, dual_8_11, 2);
    DUAL_ASYM(d_off11, dual_8_11, 3);

#undef DUAL_ASYM

    int32x4_t acc = vaddq_s32(acc_main, acc_dual);

    vst1_u16(dst + row * dst_stride + block_col_begin,
             round_clip_narrow_4(acc, max_u16, prec_bits));
  }
}

void av2_convolve_symmetric_dual_highbd_neon(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(!filter_config->subtract_center);
  const int num_sym_taps = filter_config->num_pixels / 2;
  const int num_taps_dual = filter_config->num_pixels2 & ~1;
  const int num_rows = block_row_end - block_row_begin;
  const int num_cols = block_col_end - block_col_begin;

  if (LIKELY(num_rows == 4 && num_cols == 4) && num_sym_taps == 6 &&
      num_taps_dual == 12 &&
      (filter_config->config == wienerns_simd_config_uv_from_uv ||
       !memcmp(wienerns_simd_config_uv_from_uv, filter_config->config,
               filter_config->num_pixels * 3 *
                   sizeof(filter_config->config[0][0]))) &&
      (filter_config->config2 == wienerns_simd_config_uv_from_y ||
       !memcmp(wienerns_simd_config_uv_from_y, filter_config->config2,
               filter_config->num_pixels2 * 3 *
                   sizeof(filter_config->config2[0][0])))) {
    convolve_symmetric_dual_highbd_7plus13tap_4x4_neon(
        dgd, dgd_stride, dgd_dual, dgd_dual_stride, filter_config, filter,
        filter_config->prec_bits, dst, dst_stride, bit_depth, block_row_begin,
        block_col_begin);
    return;
  }

  int32_t singleton_tap = 1 << filter_config->prec_bits;
  if (filter_config->num_pixels % 2) {
    const int idx =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[idx];
  }
  int32_t singleton_tap_dual = 0;
  if (filter_config->num_pixels2 % 2) {
    const int last = filter_config->num_pixels2 - 1;
    const int idx = filter_config->config2[last][NONSEP_BUF_POS];
    singleton_tap_dual = filter[idx];
  }

  assert(num_sym_taps <= 24);
  int pixel_offset_diffs[24];
  int16_t filter_taps_main[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    pixel_offset_diffs[k] = r * dgd_stride + c;
    filter_taps_main[k] = filter[filter_config->config[2 * k][NONSEP_BUF_POS]];
  }

  assert(num_taps_dual <= 24);
  int pixel_offset_diffs_dual[24];
  int16_t filter_taps_dual[24];
  for (int k = 0; k < num_taps_dual; ++k) {
    const int r = filter_config->config2[k][NONSEP_ROW_ID];
    const int c = filter_config->config2[k][NONSEP_COL_ID];
    pixel_offset_diffs_dual[k] = r * dgd_dual_stride + c;
    filter_taps_dual[k] = filter[filter_config->config2[k][NONSEP_BUF_POS]];
  }

  const int prec_bits = filter_config->prec_bits;
  const int32x4_t v_singleton = vdupq_n_s32(singleton_tap);
  const int32x4_t v_singleton_dual = vdupq_n_s32(singleton_tap_dual);
  const uint16x8_t max_pixel_u16 =
      vdupq_n_u16((uint16_t)((1 << bit_depth) - 1));

  for (int r = block_row_begin; r < block_row_end; ++r) {
    int c = block_col_begin;
    const int dgd_row = r * dgd_stride;
    const int dgd_dual_row = r * dgd_dual_stride;
    const int dst_row = r * dst_stride;

    for (; c + 8 <= block_col_end; c += 8) {
      const uint16_t *center = dgd + dgd_row + c;
      const uint16_t *center_dual = dgd_dual + dgd_dual_row + c;

      int16x8_t center_s16 = vreinterpretq_s16_u16(vld1q_u16(center));
      int16x8_t center_dual_s16 = vreinterpretq_s16_u16(vld1q_u16(center_dual));

      int32x4_t acc_lo =
          vmulq_s32(vmovl_s16(vget_low_s16(center_s16)), v_singleton);
      int32x4_t acc_hi =
          vmulq_s32(vmovl_s16(vget_high_s16(center_s16)), v_singleton);

      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        int16x8_t neg = vreinterpretq_s16_u16(vld1q_u16(center - diff));
        int16x8_t pos = vreinterpretq_s16_u16(vld1q_u16(center + diff));
        int16x8_t sum = vaddq_s16(neg, pos);
        const int16_t tap = filter_taps_main[k];
        acc_lo = vmlal_n_s16(acc_lo, vget_low_s16(sum), tap);
        acc_hi = vmlal_n_s16(acc_hi, vget_high_s16(sum), tap);
      }

      acc_lo = vmlaq_s32(acc_lo, vmovl_s16(vget_low_s16(center_dual_s16)),
                         v_singleton_dual);
      acc_hi = vmlaq_s32(acc_hi, vmovl_s16(vget_high_s16(center_dual_s16)),
                         v_singleton_dual);

      for (int k = 0; k < num_taps_dual; ++k) {
        const int diff = pixel_offset_diffs_dual[k];
        int16x8_t sample = vreinterpretq_s16_u16(vld1q_u16(center_dual + diff));
        const int16_t tap = filter_taps_dual[k];
        acc_lo = vmlal_n_s16(acc_lo, vget_low_s16(sample), tap);
        acc_hi = vmlal_n_s16(acc_hi, vget_high_s16(sample), tap);
      }

      vst1q_u16(
          dst + dst_row + c,
          round_clip_narrow_pair(acc_lo, acc_hi, max_pixel_u16, prec_bits));
    }

    for (; c < block_col_end; ++c) {
      int dgd_id = dgd_row + c;
      int dgd_dual_id = dgd_dual_row + c;
      int16_t compute_buffer[24], compute_buffer_dual[24];

      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        compute_buffer[k] = (int16_t)dgd[dgd_id - diff];
      }
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        compute_buffer[k] += (int16_t)dgd[dgd_id + diff];
      }
      for (int k = 0; k < num_taps_dual; ++k) {
        compute_buffer_dual[k] =
            (int16_t)dgd_dual[dgd_dual_id + pixel_offset_diffs_dual[k]];
      }

      int32_t tmp = singleton_tap * (int32_t)dgd[dgd_id];
      for (int k = 0; k < num_sym_taps; ++k) {
        tmp += (int32_t)filter_taps_main[k] * compute_buffer[k];
      }
      tmp += singleton_tap_dual * (int32_t)dgd_dual[dgd_dual_id];
      for (int k = 0; k < num_taps_dual; ++k) {
        tmp += (int32_t)filter_taps_dual[k] * compute_buffer_dual[k];
      }
      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, prec_bits);
      dst[dst_row + c] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}
