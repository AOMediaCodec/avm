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
#include <string.h>

#include "config/av2_rtcd.h"

#include "av2/common/av2_txfm.h"
#include "av2/common/common_data.h"
#include "av2/common/idct.h"
#include "av2/common/txb_common.h"
#include "avm_dsp/arm/transpose_neon.h"

// IDTX scale factors indexed by size_index (0=4, 1=8, 2=16, 3=32).
static const int idtx_scale[4] = { 128, 181, 256, 362 };
static const int idtx_size[4] = { 4, 8, 16, 32 };

// Round-shift-clamp helper: vrshlq_s32 does add+(1<<(n-1)) then >>n in one
// insn.
static INLINE int32x4_t round_shift_clamp(int32x4_t v, int32x4_t neg_shift_v,
                                          int32x4_t min_v, int32x4_t max_v) {
  v = vrshlq_s32(v, neg_shift_v);
  v = vmaxq_s32(v, min_v);
  v = vminq_s32(v, max_v);
  return v;
}

// ---------------------------------------------------------------------------
// IDTX 1D NEON: dst[j*line+i] = clamp((src[i*N+j]*scale + add) >> shift)
// Processes 4 rows at a time for tx1d_size=4, per-row for larger.
// For power-of-2 scales (128, 256), the multiply is fused into the shift.
// ---------------------------------------------------------------------------
static void inv_txfm_idtx_neon(const int *src, int *dst, int shift, int line,
                               int skip_line, const int coef_min,
                               const int coef_max, int size_index) {
  const int nz_line = line - skip_line;
  const int tx1d_size = idtx_size[size_index];
  const int scale = idtx_scale[size_index];
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  if (tx1d_size == 4) {
    // scale=128=2^7: fuse multiply into shift (shift-7).
    const int32x4_t neg_shift_v = vdupq_n_s32(7 - shift);
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      int32x4_t r0 = vld1q_s32(src + 0);
      int32x4_t r1 = vld1q_s32(src + 4);
      int32x4_t r2 = vld1q_s32(src + 8);
      int32x4_t r3 = vld1q_s32(src + 12);
      r0 = round_shift_clamp(r0, neg_shift_v, min_v, max_v);
      r1 = round_shift_clamp(r1, neg_shift_v, min_v, max_v);
      r2 = round_shift_clamp(r2, neg_shift_v, min_v, max_v);
      r3 = round_shift_clamp(r3, neg_shift_v, min_v, max_v);
      transpose_elems_inplace_s32_4x4(&r0, &r1, &r2, &r3);
      vst1q_s32(dst + 0 * line + i, r0);
      vst1q_s32(dst + 1 * line + i, r1);
      vst1q_s32(dst + 2 * line + i, r2);
      vst1q_s32(dst + 3 * line + i, r3);
      src += 16;
    }
    for (; i < nz_line; i++) {
      for (int j = 0; j < 4; j++) {
        dst[j * line + i] =
            clamp((int)(src[j] * scale + (1 << (shift - 1))) >> shift, coef_min,
                  coef_max);
      }
      src += 4;
    }
    return;
  }

  if (tx1d_size == 8) {
    // scale=181: not power-of-2, need multiply.
    const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      int32x4_t r0a = vmulq_n_s32(vld1q_s32(src + 0), scale);
      int32x4_t r0b = vmulq_n_s32(vld1q_s32(src + 4), scale);
      int32x4_t r1a = vmulq_n_s32(vld1q_s32(src + 8), scale);
      int32x4_t r1b = vmulq_n_s32(vld1q_s32(src + 12), scale);
      int32x4_t r2a = vmulq_n_s32(vld1q_s32(src + 16), scale);
      int32x4_t r2b = vmulq_n_s32(vld1q_s32(src + 20), scale);
      int32x4_t r3a = vmulq_n_s32(vld1q_s32(src + 24), scale);
      int32x4_t r3b = vmulq_n_s32(vld1q_s32(src + 28), scale);
      transpose_elems_inplace_s32_4x4(&r0a, &r1a, &r2a, &r3a);
      transpose_elems_inplace_s32_4x4(&r0b, &r1b, &r2b, &r3b);
      vst1q_s32(dst + 0 * line + i,
                round_shift_clamp(r0a, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 1 * line + i,
                round_shift_clamp(r1a, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 2 * line + i,
                round_shift_clamp(r2a, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 3 * line + i,
                round_shift_clamp(r3a, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 4 * line + i,
                round_shift_clamp(r0b, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 5 * line + i,
                round_shift_clamp(r1b, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 6 * line + i,
                round_shift_clamp(r2b, neg_shift_v, min_v, max_v));
      vst1q_s32(dst + 7 * line + i,
                round_shift_clamp(r3b, neg_shift_v, min_v, max_v));
      src += 32;
    }
    for (; i < nz_line; i++) {
      for (int j = 0; j < 8; j++) {
        int val = src[j] * scale + (1 << (shift - 1));  // NOLINT
        dst[j * line + i] = clamp(val >> shift, coef_min, coef_max);
      }
      src += 8;
    }
    return;
  }

  if (tx1d_size == 16) {
    // scale=256=2^8: fuse multiply into shift (shift-8).
    const int32x4_t neg_shift_v = vdupq_n_s32(8 - shift);
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      for (int g = 0; g < 4; g++) {
        int32x4_t t0 = vld1q_s32(src + 0 * 16 + g * 4);
        int32x4_t t1 = vld1q_s32(src + 1 * 16 + g * 4);
        int32x4_t t2 = vld1q_s32(src + 2 * 16 + g * 4);
        int32x4_t t3 = vld1q_s32(src + 3 * 16 + g * 4);
        transpose_elems_inplace_s32_4x4(&t0, &t1, &t2, &t3);
        vst1q_s32(dst + (g * 4 + 0) * line + i,
                  round_shift_clamp(t0, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 1) * line + i,
                  round_shift_clamp(t1, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 2) * line + i,
                  round_shift_clamp(t2, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 3) * line + i,
                  round_shift_clamp(t3, neg_shift_v, min_v, max_v));
      }
      src += 64;
    }
    for (; i < nz_line; i++) {
      for (int j = 0; j < 16; j++) {
        int val = src[j] * scale + (1 << (shift - 1));  // NOLINT
        dst[j * line + i] = clamp(val >> shift, coef_min, coef_max);
      }
      src += 16;
    }
    return;
  }

  // Size 32: scale=362, not power-of-2, need multiply.
  {
    const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      for (int g = 0; g < 8; g++) {
        int32x4_t t0 = vmulq_n_s32(vld1q_s32(src + 0 * 32 + g * 4), scale);
        int32x4_t t1 = vmulq_n_s32(vld1q_s32(src + 1 * 32 + g * 4), scale);
        int32x4_t t2 = vmulq_n_s32(vld1q_s32(src + 2 * 32 + g * 4), scale);
        int32x4_t t3 = vmulq_n_s32(vld1q_s32(src + 3 * 32 + g * 4), scale);
        transpose_elems_inplace_s32_4x4(&t0, &t1, &t2, &t3);
        vst1q_s32(dst + (g * 4 + 0) * line + i,
                  round_shift_clamp(t0, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 1) * line + i,
                  round_shift_clamp(t1, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 2) * line + i,
                  round_shift_clamp(t2, neg_shift_v, min_v, max_v));
        vst1q_s32(dst + (g * 4 + 3) * line + i,
                  round_shift_clamp(t3, neg_shift_v, min_v, max_v));
      }
      src += 128;
    }
    for (; i < nz_line; i++) {
      for (int j = 0; j < 32; j++) {
        int val = src[j] * scale + (1 << (shift - 1));  // NOLINT
        dst[j * line + i] = clamp(val >> shift, coef_min, coef_max);
      }
      src += 32;
    }
  }
}

// ---------------------------------------------------------------------------
// DCT2 size 4 NEON butterfly, processes 4 rows at a time.
// ---------------------------------------------------------------------------
static void inv_txfm_dct2_size4_neon(const int *src, int *dst, int shift,
                                     int line, int skip_line,
                                     const int coef_min, const int coef_max) {
  const int nz_line = line - skip_line;
  const int *tx_mat = tx_kernel_dct2_size4[INV_TXFM][0];
  const int M00 = tx_mat[0 * 4 + 0], M01 = tx_mat[0 * 4 + 1];
  const int M10 = tx_mat[1 * 4 + 0], M11 = tx_mat[1 * 4 + 1];
  const int M20 = tx_mat[2 * 4 + 0], M21 = tx_mat[2 * 4 + 1];
  const int M30 = tx_mat[3 * 4 + 0], M31 = tx_mat[3 * 4 + 1];

  const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  int j = 0;
  for (; j + 3 < nz_line; j += 4) {
    int32x4_t r0 = vld1q_s32(src + 0);
    int32x4_t r1 = vld1q_s32(src + 4);
    int32x4_t r2 = vld1q_s32(src + 8);
    int32x4_t r3 = vld1q_s32(src + 12);
    transpose_elems_inplace_s32_4x4(&r0, &r1, &r2, &r3);
    // r0=col0(even), r1=col1(odd), r2=col2(even), r3=col3(odd)
    int32x4_t a0 = vaddq_s32(vmulq_n_s32(r0, M00), vmulq_n_s32(r2, M20));
    int32x4_t a1 = vaddq_s32(vmulq_n_s32(r0, M01), vmulq_n_s32(r2, M21));
    int32x4_t b0 = vaddq_s32(vmulq_n_s32(r1, M10), vmulq_n_s32(r3, M30));
    int32x4_t b1 = vaddq_s32(vmulq_n_s32(r1, M11), vmulq_n_s32(r3, M31));
    int32x4_t o0 =
        round_shift_clamp(vaddq_s32(a0, b0), neg_shift_v, min_v, max_v);
    int32x4_t o1 =
        round_shift_clamp(vaddq_s32(a1, b1), neg_shift_v, min_v, max_v);
    int32x4_t o2 =
        round_shift_clamp(vsubq_s32(a1, b1), neg_shift_v, min_v, max_v);
    int32x4_t o3 =
        round_shift_clamp(vsubq_s32(a0, b0), neg_shift_v, min_v, max_v);
    vst1q_s32(dst + 0 * line + j, o0);
    vst1q_s32(dst + 1 * line + j, o1);
    vst1q_s32(dst + 2 * line + j, o2);
    vst1q_s32(dst + 3 * line + j, o3);
    src += 16;
  }
  // Scalar tail for remaining rows.
  const int add = 1 << (shift - 1);
  for (; j < nz_line; j++) {
    int a[2], b[2];
    b[0] = tx_mat[1 * 4 + 0] * src[1] + tx_mat[3 * 4 + 0] * src[3];
    b[1] = tx_mat[1 * 4 + 1] * src[1] + tx_mat[3 * 4 + 1] * src[3];
    a[0] = tx_mat[0 * 4 + 0] * src[0] + tx_mat[2 * 4 + 0] * src[2];
    a[1] = tx_mat[0 * 4 + 1] * src[0] + tx_mat[2 * 4 + 1] * src[2];
    dst[0 * line + j] = clamp((a[0] + b[0] + add) >> shift, coef_min, coef_max);
    dst[1 * line + j] = clamp((a[1] + b[1] + add) >> shift, coef_min, coef_max);
    dst[2 * line + j] = clamp((a[1] - b[1] + add) >> shift, coef_min, coef_max);
    dst[3 * line + j] = clamp((a[0] - b[0] + add) >> shift, coef_min, coef_max);
    src += 4;
  }
}

// ---------------------------------------------------------------------------
// DCT2 size 8 NEON butterfly, processes 4 rows at a time.
// For each row: 8 inputs -> 8 outputs via 3-level butterfly.
// ---------------------------------------------------------------------------
static void inv_txfm_dct2_size8_neon(const int *src, int *dst, int shift,
                                     int line, int skip_line,
                                     const int coef_min, const int coef_max) {
  const int nz_line = line - skip_line;
  const int *tx_mat = tx_kernel_dct2_size8[INV_TXFM][0];

  const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  // Process 4 rows at a time. For size 8, each row is 8 values.
  // We extract columns by processing pairs of int32x4_t per row.
  int j = 0;
  for (; j + 3 < nz_line; j += 4) {
    // Load 4 rows (8 values each = 2 int32x4_t per row).
    int32x4_t r0a = vld1q_s32(src + 0);
    int32x4_t r0b = vld1q_s32(src + 4);
    int32x4_t r1a = vld1q_s32(src + 8);
    int32x4_t r1b = vld1q_s32(src + 12);
    int32x4_t r2a = vld1q_s32(src + 16);
    int32x4_t r2b = vld1q_s32(src + 20);
    int32x4_t r3a = vld1q_s32(src + 24);
    int32x4_t r3b = vld1q_s32(src + 28);

    // Transpose low halves (cols 0-3) and high halves (cols 4-7).
    transpose_elems_inplace_s32_4x4(&r0a, &r1a, &r2a, &r3a);
    transpose_elems_inplace_s32_4x4(&r0b, &r1b, &r2b, &r3b);
    // Now r0a=col0, r1a=col1, r2a=col2, r3a=col3
    //     r0b=col4, r1b=col5, r2b=col6, r3b=col7

    // Butterfly level 1: b[k] from odd indices (1,3,5,7).
    int32x4_t b[4];
    for (int k = 0; k < 4; k++) {
      b[k] = vmulq_n_s32(r1a, tx_mat[1 * 8 + k]);
      b[k] = vmlaq_n_s32(b[k], r3a, tx_mat[3 * 8 + k]);
      b[k] = vmlaq_n_s32(b[k], r1b, tx_mat[5 * 8 + k]);
      b[k] = vmlaq_n_s32(b[k], r3b, tx_mat[7 * 8 + k]);
    }

    // Butterfly level 2: d[] from indices 2,6; c[] from indices 0,4.
    int32x4_t d0 = vaddq_s32(vmulq_n_s32(r2a, tx_mat[2 * 8 + 0]),
                             vmulq_n_s32(r2b, tx_mat[6 * 8 + 0]));
    int32x4_t d1 = vaddq_s32(vmulq_n_s32(r2a, tx_mat[2 * 8 + 1]),
                             vmulq_n_s32(r2b, tx_mat[6 * 8 + 1]));
    int32x4_t c0 = vaddq_s32(vmulq_n_s32(r0a, tx_mat[0 * 8 + 0]),
                             vmulq_n_s32(r0b, tx_mat[4 * 8 + 0]));
    int32x4_t c1 = vaddq_s32(vmulq_n_s32(r0a, tx_mat[0 * 8 + 1]),
                             vmulq_n_s32(r0b, tx_mat[4 * 8 + 1]));

    // Butterfly level 3: a[] = c +/- d.
    int32x4_t a[4];
    a[0] = vaddq_s32(c0, d0);
    a[3] = vsubq_s32(c0, d0);
    a[1] = vaddq_s32(c1, d1);
    a[2] = vsubq_s32(c1, d1);

    // Output: out[k] = a[k]+b[k], out[k+4] = a[3-k]-b[3-k].
    for (int k = 0; k < 4; k++) {
      int32x4_t ok =
          round_shift_clamp(vaddq_s32(a[k], b[k]), neg_shift_v, min_v, max_v);
      vst1q_s32(dst + k * line + j, ok);
    }
    for (int k = 0; k < 4; k++) {
      int32x4_t ok = round_shift_clamp(vsubq_s32(a[3 - k], b[3 - k]),
                                       neg_shift_v, min_v, max_v);
      vst1q_s32(dst + (k + 4) * line + j, ok);
    }
    src += 32;
  }

  // Scalar tail.
  const int add = 1 << (shift - 1);
  for (; j < nz_line; j++) {
    int a[4], bv[4], c[2], d[2];
    for (int k = 0; k < 4; k++) {
      bv[k] = tx_mat[1 * 8 + k] * src[1] + tx_mat[3 * 8 + k] * src[3] +
              tx_mat[5 * 8 + k] * src[5] + tx_mat[7 * 8 + k] * src[7];
    }
    d[0] = tx_mat[2 * 8 + 0] * src[2] + tx_mat[6 * 8 + 0] * src[6];
    d[1] = tx_mat[2 * 8 + 1] * src[2] + tx_mat[6 * 8 + 1] * src[6];
    c[0] = tx_mat[0 * 8 + 0] * src[0] + tx_mat[4 * 8 + 0] * src[4];
    c[1] = tx_mat[0 * 8 + 1] * src[0] + tx_mat[4 * 8 + 1] * src[4];
    a[0] = c[0] + d[0];
    a[3] = c[0] - d[0];
    a[1] = c[1] + d[1];
    a[2] = c[1] - d[1];
    for (int k = 0; k < 4; k++) {
      dst[k * line + j] =
          clamp((a[k] + bv[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 4) * line + j] =
          clamp((a[3 - k] - bv[3 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 8;
  }
}

// ---------------------------------------------------------------------------
// Generic NxN matrix multiply NEON for dense kernel transforms (ADST, DST7,
// DCT8, DDTX, FDDT). Processes 4 rows at a time. Output column index can
// optionally be reversed (for FDDT: j -> tx1d_size-1-j).
// ---------------------------------------------------------------------------
static void inv_txfm_matmul_neon(const int *src, int *dst, int shift, int line,
                                 int skip_line, const int coef_min,
                                 const int coef_max, const int *tx_mat,
                                 int tx1d_size, int reverse_cols) {
  const int nz_line = line - skip_line;

  const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  if (tx1d_size == 4) {
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      int32x4_t r0 = vld1q_s32(src + 0);
      int32x4_t r1 = vld1q_s32(src + 4);
      int32x4_t r2 = vld1q_s32(src + 8);
      int32x4_t r3 = vld1q_s32(src + 12);
      transpose_elems_inplace_s32_4x4(&r0, &r1, &r2, &r3);
      // r0=col0, r1=col1, r2=col2, r3=col3 (each lane = one row).
      for (int j = 0; j < 4; j++) {
        int oj = reverse_cols ? (3 - j) : j;
        int32x4_t sum = vmulq_n_s32(r0, tx_mat[0 * 4 + oj]);
        sum = vmlaq_n_s32(sum, r1, tx_mat[1 * 4 + oj]);
        sum = vmlaq_n_s32(sum, r2, tx_mat[2 * 4 + oj]);
        sum = vmlaq_n_s32(sum, r3, tx_mat[3 * 4 + oj]);
        sum = round_shift_clamp(sum, neg_shift_v, min_v, max_v);
        vst1q_s32(dst + j * line + i, sum);
      }
      src += 16;
    }
    const int add = 1 << (shift - 1);
    for (; i < nz_line; i++) {
      for (int j = 0; j < 4; j++) {
        int oj = reverse_cols ? (3 - j) : j;
        int sum = 0;
        for (int k = 0; k < 4; k++)
          sum +=
              src[k] *
              tx_mat
                  [k * 4 +
                   oj];  // NOLINT(clang-analyzer-core.UndefinedBinaryOperatorResult)
        dst[j * line + i] = clamp((sum + add) >> shift, coef_min, coef_max);
      }
      src += 4;
    }
  } else if (tx1d_size == 8) {
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      int32x4_t r0a = vld1q_s32(src + 0);
      int32x4_t r0b = vld1q_s32(src + 4);
      int32x4_t r1a = vld1q_s32(src + 8);
      int32x4_t r1b = vld1q_s32(src + 12);
      int32x4_t r2a = vld1q_s32(src + 16);
      int32x4_t r2b = vld1q_s32(src + 20);
      int32x4_t r3a = vld1q_s32(src + 24);
      int32x4_t r3b = vld1q_s32(src + 28);
      transpose_elems_inplace_s32_4x4(&r0a, &r1a, &r2a, &r3a);  // cols 0-3
      transpose_elems_inplace_s32_4x4(&r0b, &r1b, &r2b, &r3b);  // cols 4-7
      for (int j = 0; j < 8; j++) {
        int oj = reverse_cols ? (7 - j) : j;
        int32x4_t sum = vmulq_n_s32(r0a, tx_mat[0 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r1a, tx_mat[1 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r2a, tx_mat[2 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r3a, tx_mat[3 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r0b, tx_mat[4 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r1b, tx_mat[5 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r2b, tx_mat[6 * 8 + oj]);
        sum = vmlaq_n_s32(sum, r3b, tx_mat[7 * 8 + oj]);
        sum = round_shift_clamp(sum, neg_shift_v, min_v, max_v);
        vst1q_s32(dst + j * line + i, sum);
      }
      src += 32;
    }
    const int add = 1 << (shift - 1);
    for (; i < nz_line; i++) {
      for (int j = 0; j < 8; j++) {
        int oj = reverse_cols ? (7 - j) : j;
        int sum = 0;
        for (int k = 0; k < 8; k++)
          sum +=
              src[k] *
              tx_mat
                  [k * 8 +
                   oj];  // NOLINT(clang-analyzer-core.UndefinedBinaryOperatorResult)
        dst[j * line + i] = clamp((sum + add) >> shift, coef_min, coef_max);
      }
      src += 8;
    }
  } else {
    int i = 0;
    for (; i + 3 < nz_line; i += 4) {
      int32x4_t r0a = vld1q_s32(src + 0);
      int32x4_t r0b = vld1q_s32(src + 4);
      int32x4_t r0c = vld1q_s32(src + 8);
      int32x4_t r0d = vld1q_s32(src + 12);
      int32x4_t r1a = vld1q_s32(src + 16);
      int32x4_t r1b = vld1q_s32(src + 20);
      int32x4_t r1c = vld1q_s32(src + 24);
      int32x4_t r1d = vld1q_s32(src + 28);
      int32x4_t r2a = vld1q_s32(src + 32);
      int32x4_t r2b = vld1q_s32(src + 36);
      int32x4_t r2c = vld1q_s32(src + 40);
      int32x4_t r2d = vld1q_s32(src + 44);
      int32x4_t r3a = vld1q_s32(src + 48);
      int32x4_t r3b = vld1q_s32(src + 52);
      int32x4_t r3c = vld1q_s32(src + 56);
      int32x4_t r3d = vld1q_s32(src + 60);
      transpose_elems_inplace_s32_4x4(&r0a, &r1a, &r2a, &r3a);  // cols 0-3
      transpose_elems_inplace_s32_4x4(&r0b, &r1b, &r2b, &r3b);  // cols 4-7
      transpose_elems_inplace_s32_4x4(&r0c, &r1c, &r2c, &r3c);  // cols 8-11
      transpose_elems_inplace_s32_4x4(&r0d, &r1d, &r2d, &r3d);  // cols 12-15
      for (int j = 0; j < 16; j++) {
        int oj = reverse_cols ? (15 - j) : j;
        int32x4_t sum = vmulq_n_s32(r0a, tx_mat[0 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r1a, tx_mat[1 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r2a, tx_mat[2 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r3a, tx_mat[3 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r0b, tx_mat[4 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r1b, tx_mat[5 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r2b, tx_mat[6 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r3b, tx_mat[7 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r0c, tx_mat[8 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r1c, tx_mat[9 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r2c, tx_mat[10 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r3c, tx_mat[11 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r0d, tx_mat[12 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r1d, tx_mat[13 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r2d, tx_mat[14 * 16 + oj]);
        sum = vmlaq_n_s32(sum, r3d, tx_mat[15 * 16 + oj]);
        sum = round_shift_clamp(sum, neg_shift_v, min_v, max_v);
        vst1q_s32(dst + j * line + i, sum);
      }
      src += 64;
    }
    const int add = 1 << (shift - 1);
    for (; i < nz_line; i++) {
      for (int j = 0; j < 16; j++) {
        int oj = reverse_cols ? (15 - j) : j;
        int sum = 0;
        for (int k = 0; k < 16; k++)
          sum +=
              src[k] *
              tx_mat
                  [k * 16 +
                   oj];  // NOLINT(clang-analyzer-core.UndefinedBinaryOperatorResult)
        dst[j * line + i] = clamp((sum + add) >> shift, coef_min, coef_max);
      }
      src += 16;
    }
  }
}

// ---------------------------------------------------------------------------
// DCT2 size 16 NEON butterfly, processes 4 rows at a time.
// 4-level butterfly: b[8] from odd, d[4] from even-odd, e/f from doubly-even.
// ---------------------------------------------------------------------------
static void inv_txfm_dct2_size16_neon(const int *src, int *dst, int shift,
                                      int line, int skip_line,
                                      const int coef_min, const int coef_max) {
  const int nz_line = line - skip_line;
  const int *tx_mat = tx_kernel_dct2_size16[INV_TXFM][0];

  const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  int j = 0;
  for (; j + 3 < nz_line; j += 4) {
    // Load 4 rows of 16 values each (4 int32x4_t per row).
    int32x4_t r0a = vld1q_s32(src + 0);
    int32x4_t r0b = vld1q_s32(src + 4);
    int32x4_t r0c = vld1q_s32(src + 8);
    int32x4_t r0d = vld1q_s32(src + 12);
    int32x4_t r1a = vld1q_s32(src + 16);
    int32x4_t r1b = vld1q_s32(src + 20);
    int32x4_t r1c = vld1q_s32(src + 24);
    int32x4_t r1d = vld1q_s32(src + 28);
    int32x4_t r2a = vld1q_s32(src + 32);
    int32x4_t r2b = vld1q_s32(src + 36);
    int32x4_t r2c = vld1q_s32(src + 40);
    int32x4_t r2d = vld1q_s32(src + 44);
    int32x4_t r3a = vld1q_s32(src + 48);
    int32x4_t r3b = vld1q_s32(src + 52);
    int32x4_t r3c = vld1q_s32(src + 56);
    int32x4_t r3d = vld1q_s32(src + 60);

    // Transpose each group of 4 columns to get column vectors.
    transpose_elems_inplace_s32_4x4(&r0a, &r1a, &r2a, &r3a);  // col0-3
    transpose_elems_inplace_s32_4x4(&r0b, &r1b, &r2b, &r3b);  // col4-7
    transpose_elems_inplace_s32_4x4(&r0c, &r1c, &r2c, &r3c);  // col8-11
    transpose_elems_inplace_s32_4x4(&r0d, &r1d, &r2d, &r3d);  // col12-15
    // After transpose: r0a=col0, r1a=col1, r2a=col2, r3a=col3
    //                  r0b=col4, r1b=col5, r2b=col6, r3b=col7
    //                  r0c=col8, r1c=col9, r2c=col10, r3c=col11
    //                  r0d=col12, r1d=col13, r2d=col14, r3d=col15

    // Level 1: b[k] from odd columns (1,3,5,7,9,11,13,15).
    // Column-major: process one input column at a time across all 8 outputs.
    int32x4_t b[8];
    for (int k = 0; k < 8; k++) b[k] = vmulq_n_s32(r1a, tx_mat[1 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r3a, tx_mat[3 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r1b, tx_mat[5 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r3b, tx_mat[7 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r1c, tx_mat[9 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r3c, tx_mat[11 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r1d, tx_mat[13 * 16 + k]);
    for (int k = 0; k < 8; k++)
      b[k] = vmlaq_n_s32(b[k], r3d, tx_mat[15 * 16 + k]);

    // Level 2: d[k] from columns 2,6,10,14.
    int32x4_t d[4];
    for (int k = 0; k < 4; k++) {
      d[k] = vmulq_n_s32(r2a, tx_mat[2 * 16 + k]);
      d[k] = vmlaq_n_s32(d[k], r2b, tx_mat[6 * 16 + k]);
      d[k] = vmlaq_n_s32(d[k], r2c, tx_mat[10 * 16 + k]);
      d[k] = vmlaq_n_s32(d[k], r2d, tx_mat[14 * 16 + k]);
    }

    // Level 3: e/f from columns 0,4,8,12 (like DCT4).
    int32x4_t f0 = vaddq_s32(vmulq_n_s32(r0b, tx_mat[4 * 16 + 0]),
                             vmulq_n_s32(r0d, tx_mat[12 * 16 + 0]));
    int32x4_t e0 = vaddq_s32(vmulq_n_s32(r0a, tx_mat[0 * 16 + 0]),
                             vmulq_n_s32(r0c, tx_mat[8 * 16 + 0]));
    int32x4_t f1 = vaddq_s32(vmulq_n_s32(r0b, tx_mat[4 * 16 + 1]),
                             vmulq_n_s32(r0d, tx_mat[12 * 16 + 1]));
    int32x4_t e1 = vaddq_s32(vmulq_n_s32(r0a, tx_mat[0 * 16 + 1]),
                             vmulq_n_s32(r0c, tx_mat[8 * 16 + 1]));

    // Level 4: combine butterfly.
    int32x4_t c[4];
    c[0] = vaddq_s32(e0, f0);
    c[1] = vaddq_s32(e1, f1);
    c[2] = vsubq_s32(e1, f1);
    c[3] = vsubq_s32(e0, f0);

    int32x4_t a[8];
    for (int k = 0; k < 4; k++) {
      a[k] = vaddq_s32(c[k], d[k]);
      a[k + 4] = vsubq_s32(c[3 - k], d[3 - k]);
    }

    // Output: out[k] = a[k]+b[k], out[k+8] = a[7-k]-b[7-k].
    for (int k = 0; k < 8; k++) {
      int32x4_t ok =
          round_shift_clamp(vaddq_s32(a[k], b[k]), neg_shift_v, min_v, max_v);
      vst1q_s32(dst + k * line + j, ok);
    }
    for (int k = 0; k < 8; k++) {
      int32x4_t ok = round_shift_clamp(vsubq_s32(a[7 - k], b[7 - k]),
                                       neg_shift_v, min_v, max_v);
      vst1q_s32(dst + (k + 8) * line + j, ok);
    }
    src += 64;
  }

  // Scalar tail.
  const int add = 1 << (shift - 1);
  for (; j < nz_line; j++) {
    int av[8], bv[8], cv[4], dv[4], ev[2], fv[2];
    for (int k = 0; k < 8; k++) {
      bv[k] = tx_mat[1 * 16 + k] * src[1] + tx_mat[3 * 16 + k] * src[3] +
              tx_mat[5 * 16 + k] * src[5] + tx_mat[7 * 16 + k] * src[7] +
              tx_mat[9 * 16 + k] * src[9] + tx_mat[11 * 16 + k] * src[11] +
              tx_mat[13 * 16 + k] * src[13] + tx_mat[15 * 16 + k] * src[15];
    }
    for (int k = 0; k < 4; k++) {
      dv[k] = tx_mat[2 * 16 + k] * src[2] + tx_mat[6 * 16 + k] * src[6] +
              tx_mat[10 * 16 + k] * src[10] + tx_mat[14 * 16 + k] * src[14];
    }
    fv[0] = tx_mat[4 * 16] * src[4] + tx_mat[12 * 16] * src[12];
    ev[0] = tx_mat[0] * src[0] + tx_mat[8 * 16] * src[8];
    fv[1] = tx_mat[4 * 16 + 1] * src[4] + tx_mat[12 * 16 + 1] * src[12];
    ev[1] = tx_mat[0 * 16 + 1] * src[0] + tx_mat[8 * 16 + 1] * src[8];
    for (int k = 0; k < 2; k++) {
      cv[k] = ev[k] + fv[k];
      cv[k + 2] = ev[1 - k] - fv[1 - k];
    }
    for (int k = 0; k < 4; k++) {
      av[k] = cv[k] + dv[k];
      av[k + 4] = cv[3 - k] - dv[3 - k];
    }
    for (int k = 0; k < 8; k++) {
      dst[k * line + j] =
          clamp((av[k] + bv[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 8) * line + j] =
          clamp((av[7 - k] - bv[7 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 16;
  }
}

// ---------------------------------------------------------------------------
// DCT2 size 32 NEON butterfly, processes 4 rows at a time.
// 5-level butterfly: b[16] from odd, d[8] from even-odd, f[4], g/h.
// Uses stack buffer for transposed columns since 32 > register count.
// ---------------------------------------------------------------------------
static void inv_txfm_dct2_size32_neon(const int *src, int *dst, int shift,
                                      int line, int skip_line,
                                      const int coef_min, const int coef_max) {
  const int nz_line = line - skip_line;
  const int *tx_mat = tx_kernel_dct2_size32[INV_TXFM][0];

  const int32x4_t neg_shift_v = vdupq_n_s32(-shift);
  const int32x4_t min_v = vdupq_n_s32(coef_min);
  const int32x4_t max_v = vdupq_n_s32(coef_max);

  int j = 0;
  for (; j + 3 < nz_line; j += 4) {
    // Transpose 4 rows of 32 values into 32 column vectors on stack.
    int32x4_t col[32];
    for (int g = 0; g < 8; g++) {
      int32x4_t t0 = vld1q_s32(src + 0 * 32 + g * 4);
      int32x4_t t1 = vld1q_s32(src + 1 * 32 + g * 4);
      int32x4_t t2 = vld1q_s32(src + 2 * 32 + g * 4);
      int32x4_t t3 = vld1q_s32(src + 3 * 32 + g * 4);
      transpose_elems_inplace_s32_4x4(&t0, &t1, &t2, &t3);
      col[g * 4 + 0] = t0;
      col[g * 4 + 1] = t1;
      col[g * 4 + 2] = t2;
      col[g * 4 + 3] = t3;
    }

    // Level 1: b[k] from 16 odd columns.
    // Iterate over input columns for better data reuse (each column loaded
    // once).
    int32x4_t b[16];
    for (int k = 0; k < 16; k++) b[k] = vmulq_n_s32(col[1], tx_mat[1 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[3], tx_mat[3 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[5], tx_mat[5 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[7], tx_mat[7 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[9], tx_mat[9 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[11], tx_mat[11 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[13], tx_mat[13 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[15], tx_mat[15 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[17], tx_mat[17 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[19], tx_mat[19 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[21], tx_mat[21 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[23], tx_mat[23 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[25], tx_mat[25 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[27], tx_mat[27 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[29], tx_mat[29 * 32 + k]);
    for (int k = 0; k < 16; k++)
      b[k] = vmlaq_n_s32(b[k], col[31], tx_mat[31 * 32 + k]);

    // Level 2: d[k] from 8 even-odd columns (2,6,10,14,18,22,26,30).
    int32x4_t d[8];
    for (int k = 0; k < 8; k++) d[k] = vmulq_n_s32(col[2], tx_mat[2 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[6], tx_mat[6 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[10], tx_mat[10 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[14], tx_mat[14 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[18], tx_mat[18 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[22], tx_mat[22 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[26], tx_mat[26 * 32 + k]);
    for (int k = 0; k < 8; k++)
      d[k] = vmlaq_n_s32(d[k], col[30], tx_mat[30 * 32 + k]);

    // Level 3: f[k] from columns 4,12,20,28.
    int32x4_t f[4];
    for (int k = 0; k < 4; k++) {
      f[k] = vmulq_n_s32(col[4], tx_mat[4 * 32 + k]);
      f[k] = vmlaq_n_s32(f[k], col[12], tx_mat[12 * 32 + k]);
      f[k] = vmlaq_n_s32(f[k], col[20], tx_mat[20 * 32 + k]);
      f[k] = vmlaq_n_s32(f[k], col[28], tx_mat[28 * 32 + k]);
    }

    // Level 4: g/h from columns 0,8,16,24.
    int32x4_t h0 = vaddq_s32(vmulq_n_s32(col[8], tx_mat[8 * 32 + 0]),
                             vmulq_n_s32(col[24], tx_mat[24 * 32 + 0]));
    int32x4_t h1 = vaddq_s32(vmulq_n_s32(col[8], tx_mat[8 * 32 + 1]),
                             vmulq_n_s32(col[24], tx_mat[24 * 32 + 1]));
    int32x4_t g0 = vaddq_s32(vmulq_n_s32(col[0], tx_mat[0 * 32 + 0]),
                             vmulq_n_s32(col[16], tx_mat[16 * 32 + 0]));
    int32x4_t g1 = vaddq_s32(vmulq_n_s32(col[0], tx_mat[0 * 32 + 1]),
                             vmulq_n_s32(col[16], tx_mat[16 * 32 + 1]));

    // Level 5: combine butterfly.
    int32x4_t e[4];
    e[0] = vaddq_s32(g0, h0);
    e[1] = vaddq_s32(g1, h1);
    e[2] = vsubq_s32(g1, h1);
    e[3] = vsubq_s32(g0, h0);

    int32x4_t c[8];
    for (int k = 0; k < 4; k++) {
      c[k] = vaddq_s32(e[k], f[k]);
      c[k + 4] = vsubq_s32(e[3 - k], f[3 - k]);
    }

    int32x4_t a[16];
    for (int k = 0; k < 8; k++) {
      a[k] = vaddq_s32(c[k], d[k]);
      a[k + 8] = vsubq_s32(c[7 - k], d[7 - k]);
    }

    // Output: out[k] = a[k]+b[k], out[k+16] = a[15-k]-b[15-k].
    for (int k = 0; k < 16; k++) {
      int32x4_t ok =
          round_shift_clamp(vaddq_s32(a[k], b[k]), neg_shift_v, min_v, max_v);
      vst1q_s32(dst + k * line + j, ok);
    }
    for (int k = 0; k < 16; k++) {
      int32x4_t ok = round_shift_clamp(vsubq_s32(a[15 - k], b[15 - k]),
                                       neg_shift_v, min_v, max_v);
      vst1q_s32(dst + (k + 16) * line + j, ok);
    }
    src += 128;
  }

  // Scalar tail.
  const int add = 1 << (shift - 1);
  for (; j < nz_line; j++) {
    int av[16], bv[16], cv[8], dv[8], ev[4], fv[4], gv[2], hv[2];
    for (int k = 0; k < 16; k++) {
      bv[k] = 0;
      for (int m = 0; m < 16; m++)
        bv[k] += tx_mat[(2 * m + 1) * 32 + k] * src[2 * m + 1];
    }
    for (int k = 0; k < 8; k++) {
      dv[k] = tx_mat[2 * 32 + k] * src[2] + tx_mat[6 * 32 + k] * src[6] +
              tx_mat[10 * 32 + k] * src[10] + tx_mat[14 * 32 + k] * src[14] +
              tx_mat[18 * 32 + k] * src[18] + tx_mat[22 * 32 + k] * src[22] +
              tx_mat[26 * 32 + k] * src[26] + tx_mat[30 * 32 + k] * src[30];
    }
    for (int k = 0; k < 4; k++) {
      fv[k] = tx_mat[4 * 32 + k] * src[4] + tx_mat[12 * 32 + k] * src[12] +
              tx_mat[20 * 32 + k] * src[20] + tx_mat[28 * 32 + k] * src[28];
    }
    hv[0] = tx_mat[8 * 32 + 0] * src[8] + tx_mat[24 * 32 + 0] * src[24];
    hv[1] = tx_mat[8 * 32 + 1] * src[8] + tx_mat[24 * 32 + 1] * src[24];
    gv[0] = tx_mat[0 * 32 + 0] * src[0] + tx_mat[16 * 32 + 0] * src[16];
    gv[1] = tx_mat[0 * 32 + 1] * src[0] + tx_mat[16 * 32 + 1] * src[16];
    ev[0] = gv[0] + hv[0];
    ev[1] = gv[1] + hv[1];
    ev[2] = gv[1] - hv[1];
    ev[3] = gv[0] - hv[0];
    for (int k = 0; k < 4; k++) {
      cv[k] = ev[k] + fv[k];
      cv[k + 4] = ev[3 - k] - fv[3 - k];
    }
    for (int k = 0; k < 8; k++) {
      av[k] = cv[k] + dv[k];
      av[k + 8] = cv[7 - k] - dv[7 - k];
    }
    for (int k = 0; k < 16; k++) {
      dst[k * line + j] =
          clamp((av[k] + bv[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 16) * line + j] =
          clamp((av[15 - k] - bv[15 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 32;
  }
}

// ---------------------------------------------------------------------------
// 1D dispatch for NEON-supported types.
// ---------------------------------------------------------------------------
static void inv_transform_1d_neon(const int *src, int *dst, int shift, int line,
                                  int skip_line, int zero_line,
                                  const int coef_min, const int coef_max,
                                  const int tx_type_index,
                                  const int size_index) {
  (void)zero_line;
  switch (tx_type_index) {
    case DCT2:
      if (size_index == 0)
        inv_txfm_dct2_size4_neon(src, dst, shift, line, skip_line, coef_min,
                                 coef_max);
      else if (size_index == 1)
        inv_txfm_dct2_size8_neon(src, dst, shift, line, skip_line, coef_min,
                                 coef_max);
      else if (size_index == 2)
        inv_txfm_dct2_size16_neon(src, dst, shift, line, skip_line, coef_min,
                                  coef_max);
      else
        inv_txfm_dct2_size32_neon(src, dst, shift, line, skip_line, coef_min,
                                  coef_max);
      break;
    case IDT:
      inv_txfm_idtx_neon(src, dst, shift, line, skip_line, coef_min, coef_max,
                         size_index);
      break;
    case DST7: {
      static const int *const adst_kernels[] = {
        tx_kernel_adst_size4[INV_TXFM][0],
        tx_kernel_adst_size8[INV_TXFM][0],
        tx_kernel_adst_size16[INV_TXFM][0],
      };
      static const int sizes[] = { 4, 8, 16 };
      inv_txfm_matmul_neon(src, dst, shift, line, skip_line, coef_min, coef_max,
                           adst_kernels[size_index], sizes[size_index], 0);
      break;
    }
    case DCT8: {
      static const int *const fdst_kernels[] = {
        tx_kernel_fdst_size4[INV_TXFM][0],
        tx_kernel_fdst_size8[INV_TXFM][0],
        tx_kernel_fdst_size16[INV_TXFM][0],
      };
      static const int sizes[] = { 4, 8, 16 };
      inv_txfm_matmul_neon(src, dst, shift, line, skip_line, coef_min, coef_max,
                           fdst_kernels[size_index], sizes[size_index], 0);
      break;
    }
    case DDTX: {
      static const int *const ddtx_kernels[] = {
        tx_kernel_ddtx_size4[INV_TXFM][0],
        tx_kernel_ddtx_size8[INV_TXFM][0],
        tx_kernel_ddtx_size16[INV_TXFM][0],
      };
      static const int sizes[] = { 4, 8, 16 };
      inv_txfm_matmul_neon(src, dst, shift, line, skip_line, coef_min, coef_max,
                           ddtx_kernels[size_index], sizes[size_index], 0);
      break;
    }
    case FDDT: {
      static const int *const ddtx_kernels[] = {
        tx_kernel_ddtx_size4[INV_TXFM][0],
        tx_kernel_ddtx_size8[INV_TXFM][0],
        tx_kernel_ddtx_size16[INV_TXFM][0],
      };
      static const int sizes[] = { 4, 8, 16 };
      inv_txfm_matmul_neon(src, dst, shift, line, skip_line, coef_min, coef_max,
                           ddtx_kernels[size_index], sizes[size_index], 1);
      break;
    }
    default: assert(0 && "Unsupported tx_type in NEON path"); break;
  }
}

// Check whether both axes can be handled by the NEON 1D implementations.
static int neon_path_supported(int tx_type_row, int tx_type_col,
                               int tx_wide_index, int tx_high_index) {
  // IDTX is supported at all sizes.
  // DCT2 supported at all sizes (4,8,16,32).
  // DST7/DCT8/DDTX/FDDT supported at sizes 4,8,16 (size_index <= 2).
  const int row_ok = (tx_type_row == IDT) || (tx_type_row == DCT2) ||
                     ((tx_type_row == DST7 || tx_type_row == DCT8 ||
                       tx_type_row == DDTX || tx_type_row == FDDT) &&
                      tx_wide_index <= 2);
  const int col_ok = (tx_type_col == IDT) || (tx_type_col == DCT2) ||
                     ((tx_type_col == DST7 || tx_type_col == DCT8 ||
                       tx_type_col == DDTX || tx_type_col == FDDT) &&
                      tx_high_index <= 2);
  return row_ok && col_ok;
}

// ---------------------------------------------------------------------------
// 2D entry point.
// ---------------------------------------------------------------------------
void inv_txfm_neon(const tran_low_t *input, uint16_t *dest, int stride,
                   const TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;
  const PRIM_TX_TYPE prim_tx_type = txfm_param->prim_tx_type;

  // Lossless: delegate to C (uses WHT, not DCT).
  if (txfm_param->lossless) {
    inv_txfm_c(input, dest, stride, txfm_param);
    return;
  }

  int width = AVMMIN(MAX_TX_SIZE >> 1, tx_size_wide[tx_size]);
  int height = AVMMIN(MAX_TX_SIZE >> 1, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AVMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AVMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_high_log2[tx_size]) - 2;

  int tx_type_row = g_hor_tx_type[prim_tx_type];
  int tx_type_col = g_ver_tx_type[prim_tx_type];

  // Apply DDT substitution (same logic as inv_txfm_c).
  if (txfm_param->use_ddt) {
    const int use_ddt_row = (width == 4 && REPLACE_ADST4) ||
                            (width == 8 && REPLACE_ADST8) ||
                            (width == 16 && REPLACE_ADST16);
    if (use_ddt_row && (tx_type_row == DST7 || tx_type_row == DCT8))
      tx_type_row = (tx_type_row == DST7) ? DDTX : FDDT;
    const int use_ddt_col = (height == 4 && REPLACE_ADST4) ||
                            (height == 8 && REPLACE_ADST8) ||
                            (height == 16 && REPLACE_ADST16);
    if (use_ddt_col && (tx_type_col == DST7 || tx_type_col == DCT8))
      tx_type_col = (tx_type_col == DST7) ? DDTX : FDDT;
  }

  // Check if we can handle both axes in NEON.
  if (!neon_path_supported(tx_type_row, tx_type_col, tx_wide_index,
                           tx_high_index)) {
    inv_txfm_c(input, dest, stride, txfm_param);
    return;
  }

  const int intermediate_bitdepth = txfm_param->bd + 8;
  const int rng_min = -(1 << (intermediate_bitdepth - 1));
  const int rng_max = (1 << (intermediate_bitdepth - 1)) - 1;
  const int col_rng_min = -(1 << txfm_param->bd);
  const int col_rng_max = (1 << txfm_param->bd) - 1;

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int do_sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;

  int block[MAX_TX_SQUARE];
  int tmp[MAX_TX_SQUARE];
  const int n = AVMMIN(1024, width * height);

  // Load input with optional sqrt2 scaling, then clamp to intermediate
  // bitdepth.
  const int32x4_t cmin = vdupq_n_s32(rng_min);
  const int32x4_t cmax = vdupq_n_s32(rng_max);
  if (do_sqrt2) {
    int i = 0;
    for (; i + 3 < n; i += 4) {
      int32x4_t v = vld1q_s32(input + i);
      int64x2_t lo = vmull_s32(vget_low_s32(v), vdup_n_s32(NewInvSqrt2));
      int64x2_t hi = vmull_s32(vget_high_s32(v), vdup_n_s32(NewInvSqrt2));
      int32x2_t lo_s =
          vmovn_s64(vshrq_n_s64(vaddq_s64(lo, vdupq_n_s64(1 << 11)), 12));
      int32x2_t hi_s =
          vmovn_s64(vshrq_n_s64(vaddq_s64(hi, vdupq_n_s64(1 << 11)), 12));
      v = vcombine_s32(lo_s, hi_s);
      v = vmaxq_s32(vminq_s32(v, cmax), cmin);
      vst1q_s32(block + i, v);
    }
    for (; i < n; i++) {
      int val = round_shift((int64_t)input[i] * NewInvSqrt2, NewSqrt2Bits);
      block[i] = clamp(val, rng_min, rng_max);
    }
  } else {
    int i = 0;
    for (; i + 3 < n; i += 4) {
      int32x4_t v = vld1q_s32(input + i);
      v = vmaxq_s32(vminq_s32(v, cmax), cmin);
      vst1q_s32(block + i, v);
    }
    for (; i < n; i++) block[i] = clamp_value(input[i], intermediate_bitdepth);
  }

  const int shift_1st = inv_tx_shift[tx_size][0];
  const int shift_2nd = inv_tx_shift[tx_size][1];

  // Row pass (horizontal 1D transform).
  inv_transform_1d_neon(block, tmp, shift_1st, height, 0, 0, rng_min, rng_max,
                        tx_type_row, tx_wide_index);

  // Column pass (vertical 1D transform).
  inv_transform_1d_neon(tmp, block, shift_2nd, width, 0, 0, col_rng_min,
                        col_rng_max, tx_type_col, tx_high_index);

  // Width expansion (32->64).
  if (width < tx_size_wide[tx_size]) {
    memcpy(tmp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[y * 2 * width + 2 * x] = tmp[y * width + x];
        block[y * 2 * width + 2 * x + 1] = tmp[y * width + x];
      }
    }
    width = tx_size_wide[tx_size];
  }

  // Height expansion (32->64).
  if (height < tx_size_high[tx_size]) {
    memcpy(tmp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[2 * y * width + x] = tmp[y * width + x];
        block[(2 * y + 1) * width + x] = tmp[y * width + x];
      }
    }
    height = tx_size_high[tx_size];
  }

  // Write output with NEON: vaddw_s16 + vqmovun_s32 (saturating narrow clamps
  // negatives to 0 for free, then vmin handles the bd_max upper bound).
  const uint16x8_t bd_max_u16 = vdupq_n_u16((1 << txfm_param->bd) - 1);
  for (int y = 0; y < height; y++) {
    int x = 0;
    for (; x + 7 < width; x += 8) {
      uint16x8_t dw = vld1q_u16(dest + y * stride + x);
      int32x4_t coeff_lo = vld1q_s32(block + y * width + x);
      int32x4_t coeff_hi = vld1q_s32(block + y * width + x + 4);
      int16x8_t dest_s16 = vreinterpretq_s16_u16(dw);
      int32x4_t sum_lo = vaddw_s16(coeff_lo, vget_low_s16(dest_s16));
      int32x4_t sum_hi = vaddw_s16(coeff_hi, vget_high_s16(dest_s16));
      uint16x4_t out_lo = vqmovun_s32(sum_lo);
      uint16x4_t out_hi = vqmovun_s32(sum_hi);
      uint16x8_t result = vminq_u16(vcombine_u16(out_lo, out_hi), bd_max_u16);
      vst1q_u16(dest + y * stride + x, result);
    }
    for (; x + 3 < width; x += 4) {
      uint16x4_t d = vld1_u16(dest + y * stride + x);
      int32x4_t coeff = vld1q_s32(block + y * width + x);
      int32x4_t sum = vaddw_s16(coeff, vreinterpret_s16_u16(d));
      uint16x4_t out = vqmovun_s32(sum);
      vst1_u16(dest + y * stride + x, vmin_u16(out, vget_low_u16(bd_max_u16)));
    }
    for (; x < width; x++) {
      dest[y * stride + x] = highbd_clip_pixel_add(
          dest[y * stride + x], block[y * width + x], txfm_param->bd);
    }
  }
}

void av2_highbd_inv_txfm_add_neon(const tran_low_t *input, uint16_t *dest,
                                  int stride, const TxfmParam *txfm_param) {
  assert(av2_ext_tx_used[txfm_param->tx_set_type][txfm_param->prim_tx_type]);
  inv_txfm_neon(input, dest, stride, txfm_param);
}
