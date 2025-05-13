/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "config/aom_config.h"
#include "config/av1_rtcd.h"
#include "config/aom_dsp_rtcd.h"

#include "av1/common/idct.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/common/scan.h"
#include "av1/common/secondary_tx.h"

#if CONFIG_IMPROVE_LOSSLESS_TXM
void av1_lossless_fwd_idtx_c(const int16_t *src_diff, tran_low_t *coeff,
                             int diff_stride, TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av1_get_tx_scale(txfm_param->tx_size);
  for (int i = 0; i < txh; i++) {
    for (int j = 0; j < txw; j++) {
      coeff[i * txw + j] = src_diff[i * diff_stride + j] * (1 << scale_bits);
    }
  }
}
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM

#if CONFIG_CORE_TX
#include "av1/common/txb_common.h"
#endif  // CONFIG_CORE_TX

#if CONFIG_CORE_TX

// ********************************** DCT-II **********************************
void fwd_txfm_dct2_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  int j;
  int a[2], b[2];
  int add = (shift > 0) ? (1 << (shift - 1)) : 0;

  const int *tx_mat = tx_kernel_dct2_size4[FWD_TXFM][0];

  const int nz_line = line - skip_line;
  for (j = 0; j < nz_line; j++) {
    a[0] = src[0 * line] + src[3 * line];
    b[0] = src[0 * line] - src[3 * line];
    a[1] = src[1 * line] + src[2 * line];
    b[1] = src[1 * line] - src[2 * line];

    dst[0] = (tx_mat[0] * a[0] + tx_mat[1] * a[1] + add) >> shift;
    dst[2] = (tx_mat[8] * a[0] + tx_mat[9] * a[1] + add) >> shift;
    dst[1] = (tx_mat[4] * b[0] + tx_mat[5] * b[1] + add) >> shift;
    dst[3] = (tx_mat[12] * b[0] + tx_mat[13] * b[1] + add) >> shift;

    src++;
    dst += 4;
  }
  if (skip_line) {
    memset(dst, 0, sizeof(int) * 4 * skip_line);
  }
}

void fwd_txfm_dct2_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  int a[4], b[4];
  int c[2], d[2];
  int add = (shift > 0) ? (1 << (shift - 1)) : 0;

  const int *tx_mat = tx_kernel_dct2_size8[FWD_TXFM][0];

  const int nz_line = line - skip_line;
  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 4; k++) {
      a[k] = src[k * line] + src[(7 - k) * line];
      b[k] = src[k * line] - src[(7 - k) * line];
    }
    c[0] = a[0] + a[3];
    d[0] = a[0] - a[3];
    c[1] = a[1] + a[2];
    d[1] = a[1] - a[2];

    dst[0] = (tx_mat[0] * c[0] + tx_mat[1] * c[1] + add) >> shift;
    dst[4] = (tx_mat[32] * c[0] + tx_mat[33] * c[1] + add) >> shift;
    dst[2] = (tx_mat[16] * d[0] + tx_mat[17] * d[1] + add) >> shift;
    dst[6] = (tx_mat[48] * d[0] + tx_mat[49] * d[1] + add) >> shift;

    dst[1] = (tx_mat[8] * b[0] + tx_mat[9] * b[1] + tx_mat[10] * b[2] +
              tx_mat[11] * b[3] + add) >>
             shift;
    dst[3] = (tx_mat[24] * b[0] + tx_mat[25] * b[1] + tx_mat[26] * b[2] +
              tx_mat[27] * b[3] + add) >>
             shift;
    dst[5] = (tx_mat[40] * b[0] + tx_mat[41] * b[1] + tx_mat[42] * b[2] +
              tx_mat[43] * b[3] + add) >>
             shift;
    dst[7] = (tx_mat[56] * b[0] + tx_mat[57] * b[1] + tx_mat[58] * b[2] +
              tx_mat[59] * b[3] + add) >>
             shift;

    src++;
    dst += 8;
  }
  if (skip_line) {
    memset(dst, 0, sizeof(int) * 8 * skip_line);
  }
}

void fwd_txfm_dct2_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  int a[8], b[8];
  int c[4], d[4];
  int e[2], f[2];
  int add = (shift > 0) ? (1 << (shift - 1)) : 0;

  const int *tx_mat = tx_kernel_dct2_size16[FWD_TXFM][0];

  const int nz_line = line - skip_line;

  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 8; k++) {
      a[k] = src[k * line] + src[(15 - k) * line];
      b[k] = src[k * line] - src[(15 - k) * line];
    }
    for (k = 0; k < 4; k++) {
      c[k] = a[k] + a[7 - k];
      d[k] = a[k] - a[7 - k];
    }
    e[0] = c[0] + c[3];
    f[0] = c[0] - c[3];
    e[1] = c[1] + c[2];
    f[1] = c[1] - c[2];

    dst[0] = (tx_mat[0] * e[0] + tx_mat[1] * e[1] + add) >> shift;
    dst[8] = (tx_mat[8 * 16] * e[0] + tx_mat[8 * 16 + 1] * e[1] + add) >> shift;
    dst[4] = (tx_mat[4 * 16] * f[0] + tx_mat[4 * 16 + 1] * f[1] + add) >> shift;
    dst[12] =
        (tx_mat[12 * 16] * f[0] + tx_mat[12 * 16 + 1] * f[1] + add) >> shift;

    for (k = 2; k < 16; k += 4) {
      dst[k] = (tx_mat[k * 16] * d[0] + tx_mat[k * 16 + 1] * d[1] +
                tx_mat[k * 16 + 2] * d[2] + tx_mat[k * 16 + 3] * d[3] + add) >>
               shift;
    }

    for (k = 1; k < 16; k += 2) {
      dst[k] = (tx_mat[k * 16] * b[0] + tx_mat[k * 16 + 1] * b[1] +
                tx_mat[k * 16 + 2] * b[2] + tx_mat[k * 16 + 3] * b[3] +
                tx_mat[k * 16 + 4] * b[4] + tx_mat[k * 16 + 5] * b[5] +
                tx_mat[k * 16 + 6] * b[6] + tx_mat[k * 16 + 7] * b[7] + add) >>
               shift;
    }

    src++;
    dst += 16;
  }
  if (skip_line) {
    memset(dst, 0, sizeof(int) * 16 * skip_line);
  }
}

void fwd_txfm_dct2_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  int j, k;
  int a[16], b[16];
  int c[8], d[8];
  int e[4], f[4];
  int g[2], h[2];
  int add = (shift > 0) ? (1 << (shift - 1)) : 0;

  const int *tx_mat = tx_kernel_dct2_size32[FWD_TXFM][0];

  const int nz_line = line - skip_line;

  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 16; k++) {
      a[k] = src[k * line] + src[(31 - k) * line];
      b[k] = src[k * line] - src[(31 - k) * line];
    }
    for (k = 0; k < 8; k++) {
      c[k] = a[k] + a[15 - k];
      d[k] = a[k] - a[15 - k];
    }
    for (k = 0; k < 4; k++) {
      e[k] = c[k] + c[7 - k];
      f[k] = c[k] - c[7 - k];
    }
    g[0] = e[0] + e[3];
    h[0] = e[0] - e[3];
    g[1] = e[1] + e[2];
    h[1] = e[1] - e[2];

    dst[0] =
        (tx_mat[0 * 32 + 0] * g[0] + tx_mat[0 * 32 + 1] * g[1] + add) >> shift;
    dst[16] = (tx_mat[16 * 32 + 0] * g[0] + tx_mat[16 * 32 + 1] * g[1] + add) >>
              shift;
    dst[8] =
        (tx_mat[8 * 32 + 0] * h[0] + tx_mat[8 * 32 + 1] * h[1] + add) >> shift;
    dst[24] = (tx_mat[24 * 32 + 0] * h[0] + tx_mat[24 * 32 + 1] * h[1] + add) >>
              shift;
    for (k = 4; k < 32; k += 8) {
      dst[k] = (tx_mat[k * 32 + 0] * f[0] + tx_mat[k * 32 + 1] * f[1] +
                tx_mat[k * 32 + 2] * f[2] + tx_mat[k * 32 + 3] * f[3] + add) >>
               shift;
    }
    for (k = 2; k < 32; k += 4) {
      dst[k] = (tx_mat[k * 32 + 0] * d[0] + tx_mat[k * 32 + 1] * d[1] +
                tx_mat[k * 32 + 2] * d[2] + tx_mat[k * 32 + 3] * d[3] +
                tx_mat[k * 32 + 4] * d[4] + tx_mat[k * 32 + 5] * d[5] +
                tx_mat[k * 32 + 6] * d[6] + tx_mat[k * 32 + 7] * d[7] + add) >>
               shift;
    }

    for (k = 1; k < 32; k += 2) {
      dst[k] =
          (tx_mat[k * 32 + 0] * b[0] + tx_mat[k * 32 + 1] * b[1] +
           tx_mat[k * 32 + 2] * b[2] + tx_mat[k * 32 + 3] * b[3] +
           tx_mat[k * 32 + 4] * b[4] + tx_mat[k * 32 + 5] * b[5] +
           tx_mat[k * 32 + 6] * b[6] + tx_mat[k * 32 + 7] * b[7] +
           tx_mat[k * 32 + 8] * b[8] + tx_mat[k * 32 + 9] * b[9] +
           tx_mat[k * 32 + 10] * b[10] + tx_mat[k * 32 + 11] * b[11] +
           tx_mat[k * 32 + 12] * b[12] + tx_mat[k * 32 + 13] * b[13] +
           tx_mat[k * 32 + 14] * b[14] + tx_mat[k * 32 + 15] * b[15] + add) >>
          shift;
    }
    src++;
    dst += 32;
  }

  if (skip_line) {
    memset(dst, 0, sizeof(int) * 32 * skip_line);
  }
}

void fwd_txfm_dct2_size64_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  int offset = shift > 0 ? 1 << (shift - 1) : 0;

  const int tx1d_size = 64;
  const int *tx_mat = tx_kernel_dct2_size64[FWD_TXFM][0];

  int j, k;
  int a[32], b[32];
  int c[16], d[16];
  int e[8], f[8];
  int g[4], h[4];
  int i[2], u[2];
  int *tmp = dst;

  bool zo = zero_line != 0;

  for (j = 0; j < line - skip_line; j++) {
    for (k = 0; k < 32; k++) {
      a[k] = src[k * line] + src[(63 - k) * line];
      b[k] = src[k * line] - src[(63 - k) * line];
    }

    for (k = 0; k < 16; k++) {
      c[k] = a[k] + a[31 - k];
      d[k] = a[k] - a[31 - k];
    }

    for (k = 0; k < 8; k++) {
      e[k] = c[k] + c[15 - k];
      f[k] = c[k] - c[15 - k];
    }

    for (k = 0; k < 4; k++) {
      g[k] = e[k] + e[7 - k];
      h[k] = e[k] - e[7 - k];
    }

    i[0] = g[0] + g[3];
    u[0] = g[0] - g[3];
    i[1] = g[1] + g[2];
    u[1] = g[1] - g[2];

    dst[0] = (tx_mat[0 * 64 + 0] * i[0] + tx_mat[0 * 64 + 1] * i[1] + offset) >>
             shift;
    dst[16] =
        (tx_mat[16 * 64 + 0] * u[0] + tx_mat[16 * 64 + 1] * u[1] + offset) >>
        shift;

    if (!zo) {
      dst[32] =
          (tx_mat[32 * 64 + 0] * i[0] + tx_mat[32 * 64 + 1] * i[1] + offset) >>
          shift;
      dst[48] =
          (tx_mat[48 * 64 + 0] * u[0] + tx_mat[48 * 64 + 1] * u[1] + offset) >>
          shift;
    }
    for (k = 8; k < (zo ? 32 : 64); k += 16) {
      dst[k] =
          (tx_mat[k * 64 + 0] * h[0] + tx_mat[k * 64 + 1] * h[1] +
           tx_mat[k * 64 + 2] * h[2] + tx_mat[k * 64 + 3] * h[3] + offset) >>
          shift;
    }
    for (k = 4; k < (zo ? 32 : 64); k += 8) {
      dst[k] =
          (tx_mat[k * 64 + 0] * f[0] + tx_mat[k * 64 + 1] * f[1] +
           tx_mat[k * 64 + 2] * f[2] + tx_mat[k * 64 + 3] * f[3] +
           tx_mat[k * 64 + 4] * f[4] + tx_mat[k * 64 + 5] * f[5] +
           tx_mat[k * 64 + 6] * f[6] + tx_mat[k * 64 + 7] * f[7] + offset) >>
          shift;
    }
    for (k = 2; k < (zo ? 32 : 64); k += 4) {
      dst[k] = (tx_mat[k * 64 + 0] * d[0] + tx_mat[k * 64 + 1] * d[1] +
                tx_mat[k * 64 + 2] * d[2] + tx_mat[k * 64 + 3] * d[3] +
                tx_mat[k * 64 + 4] * d[4] + tx_mat[k * 64 + 5] * d[5] +
                tx_mat[k * 64 + 6] * d[6] + tx_mat[k * 64 + 7] * d[7] +
                tx_mat[k * 64 + 8] * d[8] + tx_mat[k * 64 + 9] * d[9] +
                tx_mat[k * 64 + 10] * d[10] + tx_mat[k * 64 + 11] * d[11] +
                tx_mat[k * 64 + 12] * d[12] + tx_mat[k * 64 + 13] * d[13] +
                tx_mat[k * 64 + 14] * d[14] + tx_mat[k * 64 + 15] * d[15] +
                offset) >>
               shift;
    }
    for (k = 1; k < (zo ? 32 : 64); k += 2) {
      dst[k] = (tx_mat[k * 64 + 0] * b[0] + tx_mat[k * 64 + 1] * b[1] +
                tx_mat[k * 64 + 2] * b[2] + tx_mat[k * 64 + 3] * b[3] +
                tx_mat[k * 64 + 4] * b[4] + tx_mat[k * 64 + 5] * b[5] +
                tx_mat[k * 64 + 6] * b[6] + tx_mat[k * 64 + 7] * b[7] +
                tx_mat[k * 64 + 8] * b[8] + tx_mat[k * 64 + 9] * b[9] +
                tx_mat[k * 64 + 10] * b[10] + tx_mat[k * 64 + 11] * b[11] +
                tx_mat[k * 64 + 12] * b[12] + tx_mat[k * 64 + 13] * b[13] +
                tx_mat[k * 64 + 14] * b[14] + tx_mat[k * 64 + 15] * b[15] +
                tx_mat[k * 64 + 16] * b[16] + tx_mat[k * 64 + 17] * b[17] +
                tx_mat[k * 64 + 18] * b[18] + tx_mat[k * 64 + 19] * b[19] +
                tx_mat[k * 64 + 20] * b[20] + tx_mat[k * 64 + 21] * b[21] +
                tx_mat[k * 64 + 22] * b[22] + tx_mat[k * 64 + 23] * b[23] +
                tx_mat[k * 64 + 24] * b[24] + tx_mat[k * 64 + 25] * b[25] +
                tx_mat[k * 64 + 26] * b[26] + tx_mat[k * 64 + 27] * b[27] +
                tx_mat[k * 64 + 28] * b[28] + tx_mat[k * 64 + 29] * b[29] +
                tx_mat[k * 64 + 30] * b[30] + tx_mat[k * 64 + 31] * b[31] +
                offset) >>
               shift;
    }
    src++;
    dst += tx1d_size;
  }

  const int nz_line = line - skip_line;
  const int cutoff = tx1d_size - zero_line;
  if (skip_line) {
    memset(dst, 0, sizeof(int) * 64 * skip_line);
  }
  if (zero_line) {
    dst = tmp + cutoff;
    for (j = 0; j < nz_line; j++) {
      memset(dst, 0, sizeof(int) * zero_line);
      dst += tx1d_size;
    }
  }
}

// ********************************** DST-VII **********************************
void fwd_txfm_idtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int scale = 128;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    for (int j = 0; j < tx1d_size; j++) {
      coef[j] = (src[j * line] * scale + offset) >> shift;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_idtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int scale = 181;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    for (int j = 0; j < tx1d_size; j++) {
      coef[j] = (src[j * line] * scale + offset) >> shift;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_idtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int scale = 256;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    for (int j = 0; j < tx1d_size; j++) {
      coef[j] = (src[j * line] * scale + offset) >> shift;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_idtx_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  const int scale = 362;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    for (int j = 0; j < tx1d_size; j++) {
      coef[j] = (src[j * line] * scale + offset) >> shift;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_adst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_adst_size4[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_adst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_adst_size8[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_adst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_adst_size16[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fdst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_fdst_size4[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fdst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_fdst_size8[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fdst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_fdst_size16[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

#if CONFIG_INTER_DDT
void fwd_txfm_ddtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size4[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_ddtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size8[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_ddtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size16[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fddt_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size4[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[tx1d_size - 1 - k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fddt_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size8[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[tx1d_size - 1 - k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}

void fwd_txfm_fddt_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line) {
  (void)zero_line;
  const int offset = shift > 0 ? 1 << (shift - 1) : 0;
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  int *coef;

  for (int i = 0; i < nz_line; i++) {
    coef = dst + (i * tx1d_size);
    const int *tx_mat = tx_kernel_ddtx_size16[FWD_TXFM][0];
    for (int j = 0; j < tx1d_size; j++) {
      int iSum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        iSum += src[k * line] * tx_mat[tx1d_size - 1 - k];
      }
      coef[j] = (iSum + offset) >> shift;
      tx_mat += tx1d_size;
    }
    src++;
  }

  if (skip_line) {
    coef = dst + nz_line * tx1d_size;
    memset(coef, 0, sizeof(int) * tx1d_size * skip_line);
  }
}
#endif
#endif  // CONFIG_CORE_TX

/* 4-point reversible, orthonormal Walsh-Hadamard in 3.5 adds, 0.5 shifts per
   pixel. */
void av1_fwht4x4_c(const int16_t *input, tran_low_t *output, int stride) {
  int i;
  tran_high_t a1, b1, c1, d1, e1;
  const int16_t *ip_pass0 = input;
  const tran_low_t *ip = NULL;
  tran_low_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip_pass0[0 * stride];
    b1 = ip_pass0[1 * stride];
    c1 = ip_pass0[2 * stride];
    d1 = ip_pass0[3 * stride];

    a1 += b1;
    d1 = d1 - c1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= c1;
    d1 += b1;
    op[0] = (tran_low_t)a1;
    op[4] = (tran_low_t)c1;
    op[8] = (tran_low_t)d1;
    op[12] = (tran_low_t)b1;

    ip_pass0++;
    op++;
  }
  ip = output;
  op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0];
    b1 = ip[1];
    c1 = ip[2];
    d1 = ip[3];

    a1 += b1;
    d1 -= c1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= c1;
    d1 += b1;
    op[0] = (tran_low_t)(a1 * UNIT_QUANT_FACTOR);
    op[1] = (tran_low_t)(c1 * UNIT_QUANT_FACTOR);
    op[2] = (tran_low_t)(d1 * UNIT_QUANT_FACTOR);
    op[3] = (tran_low_t)(b1 * UNIT_QUANT_FACTOR);

    ip += 4;
    op += 4;
  }
}

void av1_highbd_fwht4x4_c(const int16_t *input, tran_low_t *output,
                          int stride) {
  av1_fwht4x4_c(input, output, stride);
}

static void highbd_fwd_txfm_4x4(const int16_t *src_diff, tran_low_t *coeff,
                                int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  if (txfm_param->lossless) {
#if CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
    assert(tx_type == DCT_DCT || tx_type == IDTX);
    if (tx_type == IDTX) {
      av1_fwd_txfm2d_4x4(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                         txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                         bd);
    } else {
      av1_highbd_fwht4x4(src_diff, coeff, diff_stride);
    }
#else
    assert(tx_type == DCT_DCT);
    av1_highbd_fwht4x4(src_diff, coeff, diff_stride);
#endif  // CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
    return;
  }
  av1_fwd_txfm2d_4x4(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                     txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                     bd);
}

static void highbd_fwd_txfm_4x8(const int16_t *src_diff, tran_low_t *coeff,
                                int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_4x8(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                     txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                     txfm_param->bd);
}

static void highbd_fwd_txfm_8x4(const int16_t *src_diff, tran_low_t *coeff,
                                int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_8x4(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                     txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                     txfm_param->bd);
}

static void highbd_fwd_txfm_8x16(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_8x16(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_16x8(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_16x8(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_16x32(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_16x32(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       txfm_param->bd);
}

static void highbd_fwd_txfm_32x16(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_32x16(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       txfm_param->bd);
}

static void highbd_fwd_txfm_16x4(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_16x4(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      txfm_param->bd);
}

static void highbd_fwd_txfm_4x16(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_4x16(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      txfm_param->bd);
}

static void highbd_fwd_txfm_32x8(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_32x8(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      txfm_param->bd);
}

static void highbd_fwd_txfm_8x32(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  av1_fwd_txfm2d_8x32(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      txfm_param->bd);
}

static void highbd_fwd_txfm_8x8(const int16_t *src_diff, tran_low_t *coeff,
                                int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_8x8(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                     txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                     bd);
}

static void highbd_fwd_txfm_16x16(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_16x16(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_32x32(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_32x32(src_diff, dst_coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_32x64(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  assert(txfm_param->tx_type == DCT_DCT);
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_32x64(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_64x32(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  assert(txfm_param->tx_type == DCT_DCT);
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_64x32(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_16x64(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_16x64(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_64x16(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_64x16(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

static void highbd_fwd_txfm_64x64(const int16_t *src_diff, tran_low_t *coeff,
                                  int diff_stride, TxfmParam *txfm_param) {
  assert(txfm_param->tx_type == DCT_DCT);
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_64x64(src_diff, dst_coeff, diff_stride, DCT_DCT,
#if CONFIG_INTER_DDT
                       txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                       bd);
}

#if CONFIG_EXT_RECUR_PARTITIONS
static void highbd_fwd_txfm_4x32(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_4x32(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_32x4(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_32x4(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_8x64(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_8x64(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_64x8(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_64x8(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_4x64(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_4x64(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}

static void highbd_fwd_txfm_64x4(const int16_t *src_diff, tran_low_t *coeff,
                                 int diff_stride, TxfmParam *txfm_param) {
  int32_t *dst_coeff = (int32_t *)coeff;
  const int bd = txfm_param->bd;
  av1_fwd_txfm2d_64x4(src_diff, dst_coeff, diff_stride, txfm_param->tx_type,
#if CONFIG_INTER_DDT
                      txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                      bd);
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_CORE_TX
void fwd_transform_1d_c(const int *src, int *dst, int shift, int line,
                        int skip_line, int zero_line, const int tx_type_index,
                        const int size_index) {
  switch (size_index) {
    case 0:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 2:
          fwd_txfm_adst_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
#if CONFIG_INTER_DDT
        case 4:
          fwd_txfm_ddtx_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size4_c(src, dst, shift, line, skip_line, zero_line);
          break;
#endif  // CONFIG_INTER_DDT
        default: assert(0); break;
      }
      break;
    case 1:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 2:
          fwd_txfm_adst_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
#if CONFIG_INTER_DDT
        case 4:
          fwd_txfm_ddtx_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size8_c(src, dst, shift, line, skip_line, zero_line);
          break;
#endif  // CONFIG_INTER_DDT
        default: assert(0); break;
      }
      break;
    case 2:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 2:
          fwd_txfm_adst_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 3:
          fwd_txfm_fdst_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
#if CONFIG_INTER_DDT
        case 4:
          fwd_txfm_ddtx_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 5:
          fwd_txfm_fddt_size16_c(src, dst, shift, line, skip_line, zero_line);
          break;
#endif  // CONFIG_INTER_DDT
        default: assert(0); break;
      }
      break;
    case 3:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size32_c(src, dst, shift, line, skip_line, zero_line);
          break;
        case 1:
          fwd_txfm_idtx_size32_c(src, dst, shift, line, skip_line, zero_line);
          break;
        default: assert(0); break;
      }
      break;
    case 4:
      switch (tx_type_index) {
        case 0:
          fwd_txfm_dct2_size64_c(src, dst, shift, line, skip_line, zero_line);
          break;
        default: assert(0); break;
      }
      break;
    default: assert(0); break;
  }
}

void fwd_txfm_c(const int16_t *resi, tran_low_t *coeff, int diff_stride,
                TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;

  TX_TYPE tx_type = txfm_param->tx_type;

  if (txfm_param->lossless) {
#if CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
    assert(width == 4 && height == 4);
    assert(tx_type == DCT_DCT || tx_type == IDTX);
    if (tx_type == IDTX) {
      av1_fwd_txfm2d_4x4(resi, coeff, diff_stride, tx_type,
#if CONFIG_INTER_DDT
                         txfm_param->use_ddt,
#endif  // CONFIG_INTER_DDT
                         txfm_param->bd);
    } else {
      av1_highbd_fwht4x4(resi, coeff, diff_stride);
    }
#else
    assert(tx_type == DCT_DCT);
    av1_highbd_fwht4x4(resi, coeff, diff_stride);
#endif  // CONFIG_LOSSLESS_DPCM && !CONFIG_IMPROVE_LOSSLESS_TXM
    return;
  }

  int tx_type_row = g_hor_tx_type[tx_type];
  int tx_type_col = g_ver_tx_type[tx_type];

#if CONFIG_INTER_DDT
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
#endif

  int skipWidth = width > 32 ? width - 32 : 0;
  int skipHeight = height > 32 ? height - 32 : 0;

  int buf[MAX_TX_SQUARE] = { 0 };

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      coeff[(y * width) + x] = resi[(y * diff_stride) + x];
    }
  }

  const int shift_1st = fwd_tx_shift[tx_size][0];
  const int shift_2nd = fwd_tx_shift[tx_size][1];

  fwd_transform_1d_c(coeff, buf, shift_1st, width, 0, skipHeight, tx_type_col,
                     tx_high_index);
  fwd_transform_1d_c(buf, coeff, shift_2nd, height, skipHeight, skipWidth,
                     tx_type_row, tx_wide_index);

  // Re-pack non-zero coeffs in the first 32x32 indices.
  if (skipWidth) {
    for (int row = 1; row < height; ++row) {
      memcpy(coeff + row * 32, coeff + row * width, 32 * sizeof(*coeff));
    }
  }

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;
  if (sqrt2) {
    for (int i = 0; i < AOMMIN(1024, width * height); i++) {
      coeff[i] = round_shift((int64_t)coeff[i] * NewSqrt2, NewSqrt2Bits);
    }
  }
}
#endif  // CONFIG_CORE_TX

void av1_fwd_txfm(const int16_t *src_diff, tran_low_t *coeff, int diff_stride,
                  TxfmParam *txfm_param) {
#if CONFIG_IMPROVE_LOSSLESS_TXM
  if (txfm_param->lossless) {
    if (txfm_param->tx_type == IDTX) {
      av1_lossless_fwd_idtx(src_diff, coeff, diff_stride, txfm_param);
      return;
    }
  }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
#if CONFIG_CORE_TX
  fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
#else
  if (txfm_param->bd == 8) {
    av1_lowbd_fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
  } else {
    av1_highbd_fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
  }
#endif  // CONFIG_CORE_TX
}

void av1_lowbd_fwd_txfm_c(const int16_t *src_diff, tran_low_t *coeff,
                          int diff_stride, TxfmParam *txfm_param) {
  av1_highbd_fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
}

void av1_highbd_fwd_txfm(const int16_t *src_diff, tran_low_t *coeff,
                         int diff_stride, TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const TX_SIZE tx_size = txfm_param->tx_size;
  switch (tx_size) {
    case TX_64X64:
      highbd_fwd_txfm_64x64(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_32X64:
      highbd_fwd_txfm_32x64(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_64X32:
      highbd_fwd_txfm_64x32(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_16X64:
      highbd_fwd_txfm_16x64(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_64X16:
      highbd_fwd_txfm_64x16(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_32X32:
      highbd_fwd_txfm_32x32(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_16X16:
      highbd_fwd_txfm_16x16(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_8X8:
      highbd_fwd_txfm_8x8(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_4X8:
      highbd_fwd_txfm_4x8(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_8X4:
      highbd_fwd_txfm_8x4(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_8X16:
      highbd_fwd_txfm_8x16(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_16X8:
      highbd_fwd_txfm_16x8(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_16X32:
      highbd_fwd_txfm_16x32(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_32X16:
      highbd_fwd_txfm_32x16(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_4X4:
      highbd_fwd_txfm_4x4(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_4X16:
      highbd_fwd_txfm_4x16(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_16X4:
      highbd_fwd_txfm_16x4(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_8X32:
      highbd_fwd_txfm_8x32(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_32X8:
      highbd_fwd_txfm_32x8(src_diff, coeff, diff_stride, txfm_param);
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case TX_4X32:
      highbd_fwd_txfm_4x32(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_32X4:
      highbd_fwd_txfm_32x4(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_8X64:
      highbd_fwd_txfm_8x64(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_64X8:
      highbd_fwd_txfm_64x8(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_4X64:
      highbd_fwd_txfm_4x64(src_diff, coeff, diff_stride, txfm_param);
      break;
    case TX_64X4:
      highbd_fwd_txfm_64x4(src_diff, coeff, diff_stride, txfm_param);
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0); break;
  }
}

// Apply forward cross chroma component transform
void av1_fwd_cross_chroma_tx_block_c(tran_low_t *coeff_c1, tran_low_t *coeff_c2,
                                     TX_SIZE tx_size, CctxType cctx_type,
                                     const int bd) {
  if (cctx_type == CCTX_NONE) return;
  const int ncoeffs = av1_get_max_eob(tx_size);
  int32_t *src_c1 = (int32_t *)coeff_c1;
  int32_t *src_c2 = (int32_t *)coeff_c2;
  int64_t tmp[2] = { 0, 0 };

  const int angle_idx = cctx_type - CCTX_START;
  for (int i = 0; i < ncoeffs; i++) {
    tmp[0] = (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c1[i] +
             (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c2[i];
    tmp[1] = (int64_t)-cctx_mtx[angle_idx][1] * (int64_t)src_c1[i] +
             (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c2[i];
    src_c1[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[0], CCTX_PREC_BITS);
    src_c2[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[1], CCTX_PREC_BITS);
    src_c1[i] = clamp_value(src_c1[i], 8 + bd);
    src_c2[i] = clamp_value(src_c2[i], 8 + bd);
  }
}

void av1_fwd_stxfm(tran_low_t *coeff, TxfmParam *txfm_param,
                   int64_t *sec_tx_sse) {
  const TX_TYPE stx_type = txfm_param->sec_tx_type;

  const int width = tx_size_wide[txfm_param->tx_size] <= 32
                        ? tx_size_wide[txfm_param->tx_size]
                        : 32;
  const int height = tx_size_high[txfm_param->tx_size] <= 32
                         ? tx_size_high[txfm_param->tx_size]
                         : 32;

  if ((width >= 4 && height >= 4) && stx_type) {
    const PREDICTION_MODE intra_mode =
        (txfm_param->is_inter ? DC_PRED : txfm_param->intra_mode);
    PREDICTION_MODE mode = 0, mode_t = 0;
    const int log2width = tx_size_wide_log2[txfm_param->tx_size];
    const int sb_size = (width >= 8 && height >= 8) ? 8 : 4;
    const int16_t *scan_order_in;
    // Align scan order of IST with primary transform scan order
    const SCAN_ORDER *scan_order_out =
        get_scan(txfm_param->tx_size, txfm_param->tx_type);
    const int16_t *const scan = scan_order_out->scan;
    tran_low_t buf0[64] = { 0 }, buf1[64] = { 0 };
    tran_low_t *tmp = buf0;
    tran_low_t *src = coeff;
    int8_t transpose = 0;
    mode = AOMMIN(intra_mode, SMOOTH_H_PRED);
    if ((mode == H_PRED) || (mode == D157_PRED) || (mode == D67_PRED) ||
        (mode == SMOOTH_H_PRED))
      transpose = 1;
#if CONFIG_IST_SET_FLAG
    mode_t = txfm_param->sec_tx_set;
    assert(mode_t < IST_SET_SIZE);
// If in debug mode, verify whether txfm_param->sec_tx_set == intra pred dir
// based tx set id
#if !CONFIG_IST_ANY_SET && !defined(NDEBUG)
    {
      int mode_t2 = (txfm_param->tx_type == ADST_ADST)
                        ? stx_transpose_mapping[mode] + IST_DIR_SIZE
                        : stx_transpose_mapping[mode];
      assert(mode_t == mode_t2);
    }
#endif  // !CONFIG_IST_ANY_SET && !defined(NDEBUG)
#else   // CONFIG_IST_SET_FLAG
    mode_t = (txfm_param->tx_type == ADST_ADST)
                 ? stx_transpose_mapping[mode] + 7
                 : stx_transpose_mapping[mode];
#endif  // CONFIG_IST_SET_FLAG
#if STX_COEFF_DEBUG
    fprintf(stderr,
            "[fwd stx] inter %d ptx %d txs %dx%d tp %d stx_set %d stx_type %d\n"
            "(ptx coeff)\n",
            txfm_param->is_inter, get_primary_tx_type(txfm_param->tx_type),
            width, height, transpose, txfm_param->sec_tx_set, stx_type);
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        fprintf(stderr, "%d,", coeff[i * width + j]);
      }
      fprintf(stderr, "\n");
    }
#endif  // STX_COEFF_DEBUG
    if (transpose) {
      scan_order_in = (sb_size == 4)
                          ? stx_scan_orders_transpose_4x4[log2width - 2]
                          : stx_scan_orders_transpose_8x8[log2width - 2];
    } else {
      scan_order_in = (sb_size == 4) ? stx_scan_orders_4x4[log2width - 2]
                                     : stx_scan_orders_8x8[log2width - 2];
    }
#if CONFIG_E194_FLEX_SECTX
    int reduced_width = sb_size == 8 ? IST_8x8_WIDTH : IST_4x4_WIDTH;
    const int16_t *sup_reg_mapping =
        &coeff8x8_mapping[txfm_param->sec_tx_set * 3 + stx_type - 1][0];
    for (int r = 0; r < reduced_width; r++) {
      *tmp = sb_size == 8 ? src[scan_order_in[sup_reg_mapping[r]]]
                          : src[scan_order_in[r]];
      tmp++;
    }
#else
    for (int r = 0; r < sb_size * sb_size; r++) {
      *tmp = src[scan_order_in[r]];
      tmp++;
    }
#endif  // CONFIG_E194_FLEX_SECTX
#if CONFIG_E124_IST_REDUCE_METHOD4
#if CONFIG_F105_IST_MEM_REDUCE
    const int st_size_class =
        (width == 8 && height == 8 && txfm_param->tx_type == DCT_DCT) ? 1
        : (width >= 8 && height >= 8) ? (txfm_param->tx_type == DCT_DCT ? 2 : 3)
#else
    const int st_size_class = (width == 8 && height == 8)   ? 1
                              : (width >= 8 && height >= 8) ? 2
#endif  // CONFIG_F105_IST_MEM_REDUCE
                                      : 0;
#else
    const int st_size_class = sb_size;
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
    fwd_stxfm(buf0, buf1, mode_t, stx_type - 1, st_size_class, txfm_param->bd);
    if (sec_tx_sse != NULL) {
#if CONFIG_E124_IST_REDUCE_METHOD4
      const int reduced_height =
          (st_size_class == 0) ? IST_4x4_HEIGHT
          : (st_size_class == 1)
              ? IST_8x8_HEIGHT_RED
#if CONFIG_F105_IST_MEM_REDUCE
              : ((st_size_class == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
#else
              : IST_8x8_HEIGHT;
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
      const int reduced_height =
          (sb_size == 4) ? IST_4x4_HEIGHT : IST_8x8_HEIGHT;
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
      // SIMD implementation of aom_sum_squares_i32() only supports if n value
      // is multiple of 16. Hence, the n value is ensured to be at least 16
      // since the remaining elements of buf1[] are initialized with zero.
      uint64_t sec_tx_coeff_energy =
          aom_sum_squares_i32(buf1, ALIGN_POWER_OF_TWO(reduced_height, 4));
      const int bd_shift = 2 * (txfm_param->bd - 8);
      const int rounding = bd_shift > 0 ? 1 << (bd_shift - 1) : 0;
      sec_tx_coeff_energy = (sec_tx_coeff_energy + rounding) >> bd_shift;
      const int tx_shift =
          (MAX_TX_SCALE - av1_get_tx_scale(txfm_param->tx_size)) * 2;
      sec_tx_coeff_energy = RIGHT_SIGNED_SHIFT(sec_tx_coeff_energy, tx_shift);
      *sec_tx_sse = sec_tx_coeff_energy;
    }
    memset(coeff, 0, width * height * sizeof(tran_low_t));
    tmp = buf1;
#if CONFIG_E194_FLEX_SECTX
    for (int i = 0; i < reduced_width; i++) {
#else
    for (int i = 0; i < sb_size * sb_size; i++) {
#endif  // CONFIG_E194_FLEX_SECTX
      // Align scan order of IST with primary transform scan order
      coeff[scan[i]] = *tmp++;
    }
#if STX_COEFF_DEBUG
    fprintf(stderr, "(stx coeff)\n");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        fprintf(stderr, "%d,", coeff[i * width + j]);
      }
      fprintf(stderr, "\n");
    }
#endif  // STX_COEFF_DEBUG
  }
}
