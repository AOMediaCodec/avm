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

#include <arm_neon.h>
#include <assert.h>
#include <string.h>

#include "config/av2_rtcd.h"

#include "av2/common/av2_txfm.h"
#include "av2/common/common_data.h"
#include "av2/common/enums.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/fwd_txfm_internal.h"
#include "avm_dsp/txfm_common.h"

static INLINE void transpose_store_4x4_s32(int32x4_t r0, int32x4_t r1,
                                           int32x4_t r2, int32x4_t r3, int *dst,
                                           int dst_stride) {
  int32x4x2_t t01 = vtrnq_s32(r0, r1);
  int32x4x2_t t23 = vtrnq_s32(r2, r3);
  vst1q_s32(dst,
            vcombine_s32(vget_low_s32(t01.val[0]), vget_low_s32(t23.val[0])));
  vst1q_s32(dst + dst_stride,
            vcombine_s32(vget_low_s32(t01.val[1]), vget_low_s32(t23.val[1])));
  vst1q_s32(dst + 2 * dst_stride,
            vcombine_s32(vget_high_s32(t01.val[0]), vget_high_s32(t23.val[0])));
  vst1q_s32(dst + 3 * dst_stride,
            vcombine_s32(vget_high_s32(t01.val[1]), vget_high_s32(t23.val[1])));
}

static void fwd_txfm_dct2_size4_neon(const int *src, int *dst, int shift,
                                     int line, int skip_line, int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int *tx_mat = tx_kernel_dct2_size4[FWD_TXFM][0];
  const int32x4_t v_add =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int j = 0; j < nz_line; j += 4) {
    int32x4_t s0 = vld1q_s32(src + 0 * line + j);
    int32x4_t s1 = vld1q_s32(src + 1 * line + j);
    int32x4_t s2 = vld1q_s32(src + 2 * line + j);
    int32x4_t s3 = vld1q_s32(src + 3 * line + j);

    int32x4_t a0 = vaddq_s32(s0, s3);
    int32x4_t b0 = vsubq_s32(s0, s3);
    int32x4_t a1 = vaddq_s32(s1, s2);
    int32x4_t b1 = vsubq_s32(s1, s2);

    int32x4_t r0 = vmlaq_n_s32(v_add, a0, tx_mat[0]);
    r0 = vmlaq_n_s32(r0, a1, tx_mat[1]);
    r0 = vshlq_s32(r0, v_shift);

    int32x4_t r2 = vmlaq_n_s32(v_add, a0, tx_mat[8]);
    r2 = vmlaq_n_s32(r2, a1, tx_mat[9]);
    r2 = vshlq_s32(r2, v_shift);

    int32x4_t r1 = vmlaq_n_s32(v_add, b0, tx_mat[4]);
    r1 = vmlaq_n_s32(r1, b1, tx_mat[5]);
    r1 = vshlq_s32(r1, v_shift);

    int32x4_t r3 = vmlaq_n_s32(v_add, b0, tx_mat[12]);
    r3 = vmlaq_n_s32(r3, b1, tx_mat[13]);
    r3 = vshlq_s32(r3, v_shift);

    transpose_store_4x4_s32(r0, r1, r2, r3, dst + j * 4, 4);
  }
  if (skip_line) {
    memset(dst + nz_line * 4, 0, sizeof(int) * 4 * skip_line);
  }
}

static void fwd_txfm_dct2_size8_neon(const int *src, int *dst, int shift,
                                     int line, int skip_line, int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int *tx_mat = tx_kernel_dct2_size8[FWD_TXFM][0];
  const int32x4_t v_add =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int j = 0; j < nz_line; j += 4) {
    int32x4_t s[8];
    for (int k = 0; k < 8; k++) {
      s[k] = vld1q_s32(src + k * line + j);
    }
    int32x4_t a[4], b[4];
    for (int k = 0; k < 4; k++) {
      a[k] = vaddq_s32(s[k], s[7 - k]);
      b[k] = vsubq_s32(s[k], s[7 - k]);
    }
    int32x4_t c0 = vaddq_s32(a[0], a[3]);
    int32x4_t d0 = vsubq_s32(a[0], a[3]);
    int32x4_t c1 = vaddq_s32(a[1], a[2]);
    int32x4_t d1 = vsubq_s32(a[1], a[2]);

    int32x4_t out[8];
    out[0] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, c0, tx_mat[0]), c1, tx_mat[1]), v_shift);
    out[4] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, c0, tx_mat[32]), c1, tx_mat[33]),
        v_shift);
    out[2] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, d0, tx_mat[16]), d1, tx_mat[17]),
        v_shift);
    out[6] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, d0, tx_mat[48]), d1, tx_mat[49]),
        v_shift);

    for (int idx = 1; idx < 8; idx += 2) {
      const int *row = tx_mat + idx * 8;
      int32x4_t acc = vmlaq_n_s32(v_add, b[0], row[0]);
      acc = vmlaq_n_s32(acc, b[1], row[1]);
      acc = vmlaq_n_s32(acc, b[2], row[2]);
      acc = vmlaq_n_s32(acc, b[3], row[3]);
      out[idx] = vshlq_s32(acc, v_shift);
    }

    for (int blk = 0; blk < 8; blk += 4) {
      transpose_store_4x4_s32(out[blk + 0], out[blk + 1], out[blk + 2],
                              out[blk + 3], dst + j * 8 + blk, 8);
    }
  }
  if (skip_line) {
    memset(dst + nz_line * 8, 0, sizeof(int) * 8 * skip_line);
  }
}

static void fwd_txfm_dct2_size16_neon(const int *src, int *dst, int shift,
                                      int line, int skip_line, int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int *tx_mat = tx_kernel_dct2_size16[FWD_TXFM][0];
  const int32x4_t v_add =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int j = 0; j < nz_line; j += 4) {
    int32x4_t s[16];
    for (int k = 0; k < 16; k++) {
      s[k] = vld1q_s32(src + k * line + j);
    }
    int32x4_t a[8], b[8];
    for (int k = 0; k < 8; k++) {
      a[k] = vaddq_s32(s[k], s[15 - k]);
      b[k] = vsubq_s32(s[k], s[15 - k]);
    }
    int32x4_t c[4], d[4];
    for (int k = 0; k < 4; k++) {
      c[k] = vaddq_s32(a[k], a[7 - k]);
      d[k] = vsubq_s32(a[k], a[7 - k]);
    }
    int32x4_t e0 = vaddq_s32(c[0], c[3]);
    int32x4_t f0 = vsubq_s32(c[0], c[3]);
    int32x4_t e1 = vaddq_s32(c[1], c[2]);
    int32x4_t f1 = vsubq_s32(c[1], c[2]);

    int32x4_t out[16];
    out[0] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, e0, tx_mat[0]), e1, tx_mat[1]), v_shift);
    out[8] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, e0, tx_mat[8 * 16]), e1,
                                   tx_mat[8 * 16 + 1]),
                       v_shift);
    out[4] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, f0, tx_mat[4 * 16]), f1,
                                   tx_mat[4 * 16 + 1]),
                       v_shift);
    out[12] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, f0, tx_mat[12 * 16]), f1,
                                    tx_mat[12 * 16 + 1]),
                        v_shift);

    for (int k = 2; k < 16; k += 4) {
      const int *row = tx_mat + k * 16;
      int32x4_t acc = vmlaq_n_s32(v_add, d[0], row[0]);
      acc = vmlaq_n_s32(acc, d[1], row[1]);
      acc = vmlaq_n_s32(acc, d[2], row[2]);
      acc = vmlaq_n_s32(acc, d[3], row[3]);
      out[k] = vshlq_s32(acc, v_shift);
    }

    for (int k = 1; k < 16; k += 4) {
      const int *row0 = tx_mat + k * 16;
      const int *row1 = tx_mat + (k + 2) * 16;
      int32x4_t acc0 = vmlaq_n_s32(v_add, b[0], row0[0]);
      int32x4_t acc1 = vmlaq_n_s32(v_add, b[0], row1[0]);
      acc0 = vmlaq_n_s32(acc0, b[1], row0[1]);
      acc1 = vmlaq_n_s32(acc1, b[1], row1[1]);
      acc0 = vmlaq_n_s32(acc0, b[2], row0[2]);
      acc1 = vmlaq_n_s32(acc1, b[2], row1[2]);
      acc0 = vmlaq_n_s32(acc0, b[3], row0[3]);
      acc1 = vmlaq_n_s32(acc1, b[3], row1[3]);
      acc0 = vmlaq_n_s32(acc0, b[4], row0[4]);
      acc1 = vmlaq_n_s32(acc1, b[4], row1[4]);
      acc0 = vmlaq_n_s32(acc0, b[5], row0[5]);
      acc1 = vmlaq_n_s32(acc1, b[5], row1[5]);
      acc0 = vmlaq_n_s32(acc0, b[6], row0[6]);
      acc1 = vmlaq_n_s32(acc1, b[6], row1[6]);
      acc0 = vmlaq_n_s32(acc0, b[7], row0[7]);
      acc1 = vmlaq_n_s32(acc1, b[7], row1[7]);
      out[k] = vshlq_s32(acc0, v_shift);
      out[k + 2] = vshlq_s32(acc1, v_shift);
    }

    // Transpose 4x16 and store: process in 4x4 blocks
    for (int blk = 0; blk < 16; blk += 4) {
      transpose_store_4x4_s32(out[blk + 0], out[blk + 1], out[blk + 2],
                              out[blk + 3], dst + j * 16 + blk, 16);
    }
  }
  if (skip_line) {
    memset(dst + nz_line * 16, 0, sizeof(int) * 16 * skip_line);
  }
}

static void fwd_txfm_dct2_size32_neon(const int *src, int *dst, int shift,
                                      int line, int skip_line, int zero_line) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int *tx_mat = tx_kernel_dct2_size32[FWD_TXFM][0];
  const int32x4_t v_add =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int j = 0; j < nz_line; j += 4) {
    int32x4_t s[32];
    for (int k = 0; k < 32; k++) {
      s[k] = vld1q_s32(src + k * line + j);
    }
    int32x4_t a[16], b[16];
    for (int k = 0; k < 16; k++) {
      a[k] = vaddq_s32(s[k], s[31 - k]);
      b[k] = vsubq_s32(s[k], s[31 - k]);
    }
    int32x4_t c[8], d[8];
    for (int k = 0; k < 8; k++) {
      c[k] = vaddq_s32(a[k], a[15 - k]);
      d[k] = vsubq_s32(a[k], a[15 - k]);
    }
    int32x4_t e[4], f[4];
    for (int k = 0; k < 4; k++) {
      e[k] = vaddq_s32(c[k], c[7 - k]);
      f[k] = vsubq_s32(c[k], c[7 - k]);
    }
    int32x4_t g0 = vaddq_s32(e[0], e[3]);
    int32x4_t h0 = vsubq_s32(e[0], e[3]);
    int32x4_t g1 = vaddq_s32(e[1], e[2]);
    int32x4_t h1 = vsubq_s32(e[1], e[2]);

    int32x4_t out[32];
    out[0] = vshlq_s32(
        vmlaq_n_s32(vmlaq_n_s32(v_add, g0, tx_mat[0]), g1, tx_mat[1]), v_shift);
    out[16] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, g0, tx_mat[16 * 32]), g1,
                                    tx_mat[16 * 32 + 1]),
                        v_shift);
    out[8] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, h0, tx_mat[8 * 32]), h1,
                                   tx_mat[8 * 32 + 1]),
                       v_shift);
    out[24] = vshlq_s32(vmlaq_n_s32(vmlaq_n_s32(v_add, h0, tx_mat[24 * 32]), h1,
                                    tx_mat[24 * 32 + 1]),
                        v_shift);

    for (int k = 4; k < 32; k += 8) {
      const int *row = tx_mat + k * 32;
      int32x4_t acc = vmlaq_n_s32(v_add, f[0], row[0]);
      acc = vmlaq_n_s32(acc, f[1], row[1]);
      acc = vmlaq_n_s32(acc, f[2], row[2]);
      acc = vmlaq_n_s32(acc, f[3], row[3]);
      out[k] = vshlq_s32(acc, v_shift);
    }

    for (int k = 2; k < 32; k += 8) {
      const int *row0 = tx_mat + k * 32;
      const int *row1 = tx_mat + (k + 4) * 32;
      int32x4_t acc0 = vmlaq_n_s32(v_add, d[0], row0[0]);
      int32x4_t acc1 = vmlaq_n_s32(v_add, d[0], row1[0]);
      for (int m = 1; m < 8; m++) {
        acc0 = vmlaq_n_s32(acc0, d[m], row0[m]);
        acc1 = vmlaq_n_s32(acc1, d[m], row1[m]);
      }
      out[k] = vshlq_s32(acc0, v_shift);
      out[k + 4] = vshlq_s32(acc1, v_shift);
    }

    for (int k = 1; k < 32; k += 4) {
      const int *row0 = tx_mat + k * 32;
      const int *row1 = tx_mat + (k + 2) * 32;
      int32x4_t acc0 = vmlaq_n_s32(v_add, b[0], row0[0]);
      int32x4_t acc1 = vmlaq_n_s32(v_add, b[0], row1[0]);
      for (int m = 1; m < 16; m++) {
        acc0 = vmlaq_n_s32(acc0, b[m], row0[m]);
        acc1 = vmlaq_n_s32(acc1, b[m], row1[m]);
      }
      out[k] = vshlq_s32(acc0, v_shift);
      out[k + 2] = vshlq_s32(acc1, v_shift);
    }

    for (int blk = 0; blk < 32; blk += 4) {
      transpose_store_4x4_s32(out[blk + 0], out[blk + 1], out[blk + 2],
                              out[blk + 3], dst + j * 32 + blk, 32);
    }
  }
  if (skip_line) {
    memset(dst + nz_line * 32, 0, sizeof(int) * 32 * skip_line);
  }
}

static void fwd_txfm_matmul_size4_neon(const int *src, int *dst, int shift,
                                       int line, int skip_line, int zero_line,
                                       const int *tx_mat, int reverse) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int32x4_t v_offset =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int i = 0; i < nz_line; i += 4) {
    int32x4_t s0 = vld1q_s32(src + (reverse ? 3 : 0) * line + i);
    int32x4_t s1 = vld1q_s32(src + (reverse ? 2 : 1) * line + i);
    int32x4_t s2 = vld1q_s32(src + (reverse ? 1 : 2) * line + i);
    int32x4_t s3 = vld1q_s32(src + (reverse ? 0 : 3) * line + i);

    int32x4_t r0 = vmlaq_n_s32(v_offset, s0, tx_mat[0]);
    r0 = vmlaq_n_s32(r0, s1, tx_mat[1]);
    r0 = vmlaq_n_s32(r0, s2, tx_mat[2]);
    r0 = vmlaq_n_s32(r0, s3, tx_mat[3]);
    r0 = vshlq_s32(r0, v_shift);

    int32x4_t r1 = vmlaq_n_s32(v_offset, s0, tx_mat[4]);
    r1 = vmlaq_n_s32(r1, s1, tx_mat[5]);
    r1 = vmlaq_n_s32(r1, s2, tx_mat[6]);
    r1 = vmlaq_n_s32(r1, s3, tx_mat[7]);
    r1 = vshlq_s32(r1, v_shift);

    int32x4_t r2 = vmlaq_n_s32(v_offset, s0, tx_mat[8]);
    r2 = vmlaq_n_s32(r2, s1, tx_mat[9]);
    r2 = vmlaq_n_s32(r2, s2, tx_mat[10]);
    r2 = vmlaq_n_s32(r2, s3, tx_mat[11]);
    r2 = vshlq_s32(r2, v_shift);

    int32x4_t r3 = vmlaq_n_s32(v_offset, s0, tx_mat[12]);
    r3 = vmlaq_n_s32(r3, s1, tx_mat[13]);
    r3 = vmlaq_n_s32(r3, s2, tx_mat[14]);
    r3 = vmlaq_n_s32(r3, s3, tx_mat[15]);
    r3 = vshlq_s32(r3, v_shift);

    transpose_store_4x4_s32(r0, r1, r2, r3, dst + i * 4, 4);
  }

  if (skip_line) {
    memset(dst + nz_line * 4, 0, sizeof(int) * 4 * skip_line);
  }
}

static void fwd_txfm_matmul_size8_neon(const int *src, int *dst, int shift,
                                       int line, int skip_line, int zero_line,
                                       const int *tx_mat, int reverse) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int32x4_t v_offset =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int i = 0; i < nz_line; i += 4) {
    int32x4_t s[8];
    for (int k = 0; k < 8; k++) {
      s[k] = vld1q_s32(src + (reverse ? 7 - k : k) * line + i);
    }

    int32x4_t out[8];
    for (int j = 0; j < 8; j += 2) {
      const int *row0 = tx_mat + j * 8;
      const int *row1 = tx_mat + (j + 1) * 8;
      int32x4_t acc0 = vmlaq_n_s32(v_offset, s[0], row0[0]);
      int32x4_t acc1 = vmlaq_n_s32(v_offset, s[0], row1[0]);
      acc0 = vmlaq_n_s32(acc0, s[1], row0[1]);
      acc1 = vmlaq_n_s32(acc1, s[1], row1[1]);
      acc0 = vmlaq_n_s32(acc0, s[2], row0[2]);
      acc1 = vmlaq_n_s32(acc1, s[2], row1[2]);
      acc0 = vmlaq_n_s32(acc0, s[3], row0[3]);
      acc1 = vmlaq_n_s32(acc1, s[3], row1[3]);
      acc0 = vmlaq_n_s32(acc0, s[4], row0[4]);
      acc1 = vmlaq_n_s32(acc1, s[4], row1[4]);
      acc0 = vmlaq_n_s32(acc0, s[5], row0[5]);
      acc1 = vmlaq_n_s32(acc1, s[5], row1[5]);
      acc0 = vmlaq_n_s32(acc0, s[6], row0[6]);
      acc1 = vmlaq_n_s32(acc1, s[6], row1[6]);
      acc0 = vmlaq_n_s32(acc0, s[7], row0[7]);
      acc1 = vmlaq_n_s32(acc1, s[7], row1[7]);
      out[j] = vshlq_s32(acc0, v_shift);
      out[j + 1] = vshlq_s32(acc1, v_shift);
    }

    for (int blk = 0; blk < 8; blk += 4) {
      transpose_store_4x4_s32(out[blk + 0], out[blk + 1], out[blk + 2],
                              out[blk + 3], dst + i * 8 + blk, 8);
    }
  }

  if (skip_line) {
    memset(dst + nz_line * 8, 0, sizeof(int) * 8 * skip_line);
  }
}

static void fwd_txfm_matmul_size16_neon(const int *src, int *dst, int shift,
                                        int line, int skip_line, int zero_line,
                                        const int *tx_mat, int reverse) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int32x4_t v_offset =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int i = 0; i < nz_line; i += 4) {
    int32x4_t s[16];
    for (int k = 0; k < 16; k++) {
      s[k] = vld1q_s32(src + (reverse ? 15 - k : k) * line + i);
    }

    int32x4_t out[16];
    for (int j = 0; j < 16; j += 2) {
      const int *row0 = tx_mat + j * 16;
      const int *row1 = tx_mat + (j + 1) * 16;
      int32x4_t acc0 = vmlaq_n_s32(v_offset, s[0], row0[0]);
      int32x4_t acc1 = vmlaq_n_s32(v_offset, s[0], row1[0]);
      for (int k = 1; k < 16; k++) {
        acc0 = vmlaq_n_s32(acc0, s[k], row0[k]);
        acc1 = vmlaq_n_s32(acc1, s[k], row1[k]);
      }
      out[j] = vshlq_s32(acc0, v_shift);
      out[j + 1] = vshlq_s32(acc1, v_shift);
    }

    for (int blk = 0; blk < 16; blk += 4) {
      transpose_store_4x4_s32(out[blk + 0], out[blk + 1], out[blk + 2],
                              out[blk + 3], dst + i * 16 + blk, 16);
    }
  }

  if (skip_line) {
    memset(dst + nz_line * 16, 0, sizeof(int) * 16 * skip_line);
  }
}

static void fwd_txfm_idtx_neon(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line, int tx1d_size,
                               int scale) {
  (void)zero_line;
  const int nz_line = line - skip_line;
  assert((nz_line & 3) == 0);
  const int32x4_t v_scale = vdupq_n_s32(scale);
  const int32x4_t v_offset =
      shift > 0 ? vdupq_n_s32(1 << (shift - 1)) : vdupq_n_s32(0);
  const int32x4_t v_shift = vdupq_n_s32(-shift);

  for (int i = 0; i < nz_line; i += 4) {
    for (int j = 0; j < tx1d_size; j += 4) {
      int32x4_t r0 = vshlq_s32(
          vmlaq_s32(v_offset, vld1q_s32(src + (j + 0) * line + i), v_scale),
          v_shift);
      int32x4_t r1 = vshlq_s32(
          vmlaq_s32(v_offset, vld1q_s32(src + (j + 1) * line + i), v_scale),
          v_shift);
      int32x4_t r2 = vshlq_s32(
          vmlaq_s32(v_offset, vld1q_s32(src + (j + 2) * line + i), v_scale),
          v_shift);
      int32x4_t r3 = vshlq_s32(
          vmlaq_s32(v_offset, vld1q_s32(src + (j + 3) * line + i), v_scale),
          v_shift);
      transpose_store_4x4_s32(r0, r1, r2, r3, dst + i * tx1d_size + j,
                              tx1d_size);
    }
  }

  if (skip_line) {
    memset(dst + nz_line * tx1d_size, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

static void fwd_transform_1d_neon(const int *src, int *dst, int shift, int line,
                                  int skip_line, int zero_line,
                                  const int tx_type_index,
                                  const int size_index) {
  switch (size_index) {
    case 0:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size4_neon(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_neon(src, dst, shift, line, skip_line, zero_line, 4,
                             128);
          break;
        case 2:
          fwd_txfm_matmul_size4_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_adst_size4[FWD_TXFM][0], 0);
          break;
        case 3:
          fwd_txfm_matmul_size4_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_fdst_size4[FWD_TXFM][0], 0);
          break;
        case 4:
          fwd_txfm_matmul_size4_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_ddtx_size4[FWD_TXFM][0], 0);
          break;
        case 5:
          fwd_txfm_matmul_size4_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_ddtx_size4[FWD_TXFM][0], 1);
          break;
        default:
          assert(0);
          __builtin_unreachable();
          break;
      }
      break;
    case 1:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size8_neon(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_neon(src, dst, shift, line, skip_line, zero_line, 8,
                             181);
          break;
        case 2:
          fwd_txfm_matmul_size8_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_adst_size8[FWD_TXFM][0], 0);
          break;
        case 3:
          fwd_txfm_matmul_size8_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_fdst_size8[FWD_TXFM][0], 0);
          break;
        case 4:
          fwd_txfm_matmul_size8_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_ddtx_size8[FWD_TXFM][0], 0);
          break;
        case 5:
          fwd_txfm_matmul_size8_neon(src, dst, shift, line, skip_line,
                                     zero_line,
                                     tx_kernel_ddtx_size8[FWD_TXFM][0], 1);
          break;
        default:
          assert(0);
          __builtin_unreachable();
          break;
      }
      break;
    case 2:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size16_neon(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 1:
          fwd_txfm_idtx_neon(src, dst, shift, line, skip_line, zero_line, 16,
                             256);
          break;
        case 2:
          fwd_txfm_matmul_size16_neon(src, dst, shift, line, skip_line,
                                      zero_line,
                                      tx_kernel_adst_size16[FWD_TXFM][0], 0);
          break;
        case 3:
          fwd_txfm_matmul_size16_neon(src, dst, shift, line, skip_line,
                                      zero_line,
                                      tx_kernel_fdst_size16[FWD_TXFM][0], 0);
          break;
        case 4:
          fwd_txfm_matmul_size16_neon(src, dst, shift, line, skip_line,
                                      zero_line,
                                      tx_kernel_ddtx_size16[FWD_TXFM][0], 0);
          break;
        case 5:
          fwd_txfm_matmul_size16_neon(src, dst, shift, line, skip_line,
                                      zero_line,
                                      tx_kernel_ddtx_size16[FWD_TXFM][0], 1);
          break;
        default:
          assert(0);
          __builtin_unreachable();
          break;
      }
      break;
    case 3:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size32_neon(src, dst, shift, line, skip_line,
                                    zero_line);
          break;
        case 1:
          fwd_txfm_idtx_neon(src, dst, shift, line, skip_line, zero_line, 32,
                             362);
          break;
        default:
          assert(0);
          __builtin_unreachable();
          break;
      }
      break;
    case 4:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size64_c(src, dst, shift, line, skip_line, zero_line);
          break;
        default:
          assert(0);
          __builtin_unreachable();
          break;
      }
      break;
    default:
      assert(0);
      __builtin_unreachable();
      break;
  }
}

void fwd_txfm_neon(const int16_t *resi, tran_low_t *coeff, int diff_stride,
                   TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  assert(width >= 4 && height >= 4);

  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;

  TX_TYPE tx_type = txfm_param->tx_type;

  if (txfm_param->lossless) {
    assert(tx_type == DCT_DCT);
    av2_highbd_fwht4x4(resi, coeff, diff_stride);
    return;
  }

  int tx_type_row = g_hor_tx_type[tx_type];
  int tx_type_col = g_ver_tx_type[tx_type];

  if (txfm_param->use_ddt) {
    const int use_ddt_row = (width == 4 && REPLACE_ADST4) ||
                            (width == 8 && REPLACE_ADST8) ||
                            (width == 16 && REPLACE_ADST16);
    if (use_ddt_row && (tx_type_row == DST7 || tx_type_row == DCT8)) {
      tx_type_row = (tx_type_row == DST7) ? DDTX : FDDT;
    }
    const int use_ddt_col = (height == 4 && REPLACE_ADST4) ||
                            (height == 8 && REPLACE_ADST8) ||
                            (height == 16 && REPLACE_ADST16);
    if (use_ddt_col && (tx_type_col == DST7 || tx_type_col == DCT8)) {
      tx_type_col = (tx_type_col == DST7) ? DDTX : FDDT;
    }
  }

  int skip_width = width > 32 ? width - 32 : 0;
  int skip_height = height > 32 ? height - 32 : 0;

  int buf[MAX_TX_SQUARE];

  // Copy residuals (int16) to coeff buffer (int32) using NEON
  if (diff_stride == width) {
    const int total = width * height;
    assert((total & 7) == 0);
    int i = 0;
    for (; i + 8 <= total; i += 8) {
      int16x8_t r = vld1q_s16(resi + i);
      vst1q_s32(coeff + i, vmovl_s16(vget_low_s16(r)));
      vst1q_s32(coeff + i + 4, vmovl_s16(vget_high_s16(r)));
    }
  } else {
    for (int y = 0; y < height; y++) {
      int x = 0;
      for (; x + 8 <= width; x += 8) {
        int16x8_t r = vld1q_s16(resi + y * diff_stride + x);
        vst1q_s32(coeff + y * width + x, vmovl_s16(vget_low_s16(r)));
        vst1q_s32(coeff + y * width + x + 4, vmovl_s16(vget_high_s16(r)));
      }
      for (; x + 4 <= width; x += 4) {
        vst1q_s32(coeff + y * width + x,
                  vmovl_s16(vld1_s16(resi + y * diff_stride + x)));
      }
    }
  }

  const int shift_1st = fwd_tx_shift[tx_size][0];
  const int shift_2nd = fwd_tx_shift[tx_size][1];

  fwd_transform_1d_neon(coeff, buf, shift_1st, width, 0, skip_height,
                        tx_type_col, tx_high_index);
  fwd_transform_1d_neon(buf, coeff, shift_2nd, height, skip_height, skip_width,
                        tx_type_row, tx_wide_index);

  // Re-pack non-zero coeffs in the first 32x32 indices.
  if (skip_width) {
    for (int row = 1; row < height; ++row) {
      int32x4_t d0 = vld1q_s32(coeff + row * width);
      int32x4_t d1 = vld1q_s32(coeff + row * width + 4);
      int32x4_t d2 = vld1q_s32(coeff + row * width + 8);
      int32x4_t d3 = vld1q_s32(coeff + row * width + 12);
      int32x4_t d4 = vld1q_s32(coeff + row * width + 16);
      int32x4_t d5 = vld1q_s32(coeff + row * width + 20);
      int32x4_t d6 = vld1q_s32(coeff + row * width + 24);
      int32x4_t d7 = vld1q_s32(coeff + row * width + 28);
      vst1q_s32(coeff + row * 32, d0);
      vst1q_s32(coeff + row * 32 + 4, d1);
      vst1q_s32(coeff + row * 32 + 8, d2);
      vst1q_s32(coeff + row * 32 + 12, d3);
      vst1q_s32(coeff + row * 32 + 16, d4);
      vst1q_s32(coeff + row * 32 + 20, d5);
      vst1q_s32(coeff + row * 32 + 24, d6);
      vst1q_s32(coeff + row * 32 + 28, d7);
    }
  }

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;
  if (sqrt2) {
    const int count = AVMMIN(1024, width * height);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
      int32x4_t v = vld1q_s32(coeff + i);
      int64x2_t lo = vmull_s32(vget_low_s32(v), vdup_n_s32(NewSqrt2));
      int64x2_t hi = vmull_s32(vget_high_s32(v), vdup_n_s32(NewSqrt2));
      int32x2_t r_lo = vrshrn_n_s64(lo, NewSqrt2Bits);
      int32x2_t r_hi = vrshrn_n_s64(hi, NewSqrt2Bits);
      vst1q_s32(coeff + i, vcombine_s32(r_lo, r_hi));
    }
    for (; i < count; i++) {
      coeff[i] =
          (int32_t)round_shift((int64_t)coeff[i] * NewSqrt2, NewSqrt2Bits);
    }
  }
}
