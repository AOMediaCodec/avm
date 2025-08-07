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

#include <math.h>

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_ports/mem.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#include "av1/common/idct.h"
#include "av1/common/scan.h"
#include "av1/common/txb_common.h"

static INLINE void clamp_buf(int32_t *buf, int32_t size, int8_t bit) {
  for (int i = 0; i < size; ++i) buf[i] = clamp_value(buf[i], bit);
}

void inv_txfm_dct2_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  int j;
  int a[2], b[2];
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size4[INV_TXFM][0];

  const int nz_line = line - skip_line;
  for (j = 0; j < nz_line; j++) {
    b[0] = tx_mat[1 * 4 + 0] * src[1] + tx_mat[3 * 4 + 0] * src[3];
    b[1] = tx_mat[1 * 4 + 1] * src[1] + tx_mat[3 * 4 + 1] * src[3];
    a[0] = tx_mat[0 * 4 + 0] * src[0] + tx_mat[2 * 4 + 0] * src[2];
    a[1] = tx_mat[0 * 4 + 1] * src[0] + tx_mat[2 * 4 + 1] * src[2];

    dst[0 * line] = clamp((a[0] + b[0] + add) >> shift, coef_min, coef_max);
    dst[1 * line] = clamp((a[1] + b[1] + add) >> shift, coef_min, coef_max);
    dst[2 * line] = clamp((a[1] - b[1] + add) >> shift, coef_min, coef_max);
    dst[3 * line] = clamp((a[0] - b[0] + add) >> shift, coef_min, coef_max);

    src += 4;
    dst++;
  }

  if (skip_line) {
    for (j = 0; j < 4; j++) {
      memset(dst, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_dct2_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  int j, k;
  int a[4], b[4];
  int c[2], d[2];
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size8[INV_TXFM][0];

  const int nz_line = line - skip_line;
  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 4; k++) {
      b[k] = tx_mat[1 * 8 + k] * src[1] + tx_mat[3 * 8 + k] * src[3] +
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

    for (k = 0; k < 4; k++) {
      dst[(k)*line] = clamp((a[k] + b[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 4) * line] =
          clamp((a[3 - k] - b[3 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 8;
    dst++;
  }
  if (skip_line) {
    for (j = 0; j < 8; j++) {
      memset(dst, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_dct2_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  int j, k;
  int a[8], b[8];
  int c[4], d[4];
  int e[2], f[2];
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size16[INV_TXFM][0];

  const int nz_line = line - skip_line;

  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 8; k++) {
      b[k] = tx_mat[1 * 16 + k] * src[1] + tx_mat[3 * 16 + k] * src[3] +
             tx_mat[5 * 16 + k] * src[5] + tx_mat[7 * 16 + k] * src[7] +
             tx_mat[9 * 16 + k] * src[9] + tx_mat[11 * 16 + k] * src[11] +
             tx_mat[13 * 16 + k] * src[13] + tx_mat[15 * 16 + k] * src[15];
    }
    for (k = 0; k < 4; k++) {
      d[k] = tx_mat[2 * 16 + k] * src[2] + tx_mat[6 * 16 + k] * src[6] +
             tx_mat[10 * 16 + k] * src[10] + tx_mat[14 * 16 + k] * src[14];
    }
    f[0] = tx_mat[4 * 16] * src[4] + tx_mat[12 * 16] * src[12];
    e[0] = tx_mat[0] * src[0] + tx_mat[8 * 16] * src[8];
    f[1] = tx_mat[4 * 16 + 1] * src[4] + tx_mat[12 * 16 + 1] * src[12];
    e[1] = tx_mat[0 * 16 + 1] * src[0] + tx_mat[8 * 16 + 1] * src[8];
    for (k = 0; k < 2; k++) {
      c[k] = e[k] + f[k];
      c[k + 2] = e[1 - k] - f[1 - k];
    }
    for (k = 0; k < 4; k++) {
      a[k] = c[k] + d[k];
      a[k + 4] = c[3 - k] - d[3 - k];
    }
    for (k = 0; k < 8; k++) {
      dst[(k)*line] = clamp((a[k] + b[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 8) * line] =
          clamp((a[7 - k] - b[7 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 16;
    dst++;
  }
  if (skip_line) {
    for (j = 0; j < 16; j++) {
      memset(dst, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_dct2_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  int j, k;
  int a[16], b[16];
  int c[8], d[8];
  int e[4], f[4];
  int g[2], h[2];
  int add = 1 << (shift - 1);

  const int *tx_mat = tx_kernel_dct2_size32[INV_TXFM][0];

  const int nz_line = line - skip_line;

  for (j = 0; j < nz_line; j++) {
    for (k = 0; k < 16; k++) {
      b[k] = tx_mat[1 * 32 + k] * src[1] + tx_mat[3 * 32 + k] * src[3] +
             tx_mat[5 * 32 + k] * src[5] + tx_mat[7 * 32 + k] * src[7] +
             tx_mat[9 * 32 + k] * src[9] + tx_mat[11 * 32 + k] * src[11] +
             tx_mat[13 * 32 + k] * src[13] + tx_mat[15 * 32 + k] * src[15] +
             tx_mat[17 * 32 + k] * src[17] + tx_mat[19 * 32 + k] * src[19] +
             tx_mat[21 * 32 + k] * src[21] + tx_mat[23 * 32 + k] * src[23] +
             tx_mat[25 * 32 + k] * src[25] + tx_mat[27 * 32 + k] * src[27] +
             tx_mat[29 * 32 + k] * src[29] + tx_mat[31 * 32 + k] * src[31];
    }

    for (k = 0; k < 8; k++) {
      d[k] = tx_mat[2 * 32 + k] * src[2] + tx_mat[6 * 32 + k] * src[6] +
             tx_mat[10 * 32 + k] * src[10] + tx_mat[14 * 32 + k] * src[14] +
             tx_mat[18 * 32 + k] * src[18] + tx_mat[22 * 32 + k] * src[22] +
             tx_mat[26 * 32 + k] * src[26] + tx_mat[30 * 32 + k] * src[30];
    }
    for (k = 0; k < 4; k++) {
      f[k] = tx_mat[4 * 32 + k] * src[4] + tx_mat[12 * 32 + k] * src[12] +
             tx_mat[20 * 32 + k] * src[20] + tx_mat[28 * 32 + k] * src[28];
    }
    h[0] = tx_mat[8 * 32 + 0] * src[8] + tx_mat[24 * 32 + 0] * src[24];
    h[1] = tx_mat[8 * 32 + 1] * src[8] + tx_mat[24 * 32 + 1] * src[24];
    g[0] = tx_mat[0 * 32 + 0] * src[0] + tx_mat[16 * 32 + 0] * src[16];
    g[1] = tx_mat[0 * 32 + 1] * src[0] + tx_mat[16 * 32 + 1] * src[16];

    e[0] = g[0] + h[0];
    e[3] = g[0] - h[0];
    e[1] = g[1] + h[1];
    e[2] = g[1] - h[1];
    for (k = 0; k < 4; k++) {
      c[k] = e[k] + f[k];
      c[k + 4] = e[3 - k] - f[3 - k];
    }
    for (k = 0; k < 8; k++) {
      a[k] = c[k] + d[k];
      a[k + 8] = c[7 - k] - d[7 - k];
    }
    for (k = 0; k < 16; k++) {
      dst[(k)*line] = clamp((a[k] + b[k] + add) >> shift, coef_min, coef_max);
      dst[(k + 16) * line] =
          clamp((a[15 - k] - b[15 - k] + add) >> shift, coef_min, coef_max);
    }
    src += 32;
    dst++;
  }

  if (skip_line) {
    for (j = 0; j < 32; j++) {
      memset(dst, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

#if !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
void inv_txfm_dct2_size64_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  int offset = 1 << (shift - 1);
  const int tx1d_size = 64;
  const int *tx_mat = tx_kernel_dct2_size64[INV_TXFM][0];

  int j, k;
  int a[32], b[32];
  int c[16], d[16];
  int e[8], f[8];
  int g[4], h[4];
  int i[2], u[2];

  for (j = 0; j < line - skip_line; j++) {
    for (k = 0; k < 32; k++) {
      b[k] = tx_mat[1 * 64 + k] * src[1] + tx_mat[3 * 64 + k] * src[3] +
             tx_mat[5 * 64 + k] * src[5] + tx_mat[7 * 64 + k] * src[7] +
             tx_mat[9 * 64 + k] * src[9] + tx_mat[11 * 64 + k] * src[11] +
             tx_mat[13 * 64 + k] * src[13] + tx_mat[15 * 64 + k] * src[15] +
             tx_mat[17 * 64 + k] * src[17] + tx_mat[19 * 64 + k] * src[19] +
             tx_mat[21 * 64 + k] * src[21] + tx_mat[23 * 64 + k] * src[23] +
             tx_mat[25 * 64 + k] * src[25] + tx_mat[27 * 64 + k] * src[27] +
             tx_mat[29 * 64 + k] * src[29] + tx_mat[31 * 64 + k] * src[31];
    }
    for (k = 0; k < 16; k++) {
      d[k] = tx_mat[2 * 64 + k] * src[2] + tx_mat[6 * 64 + k] * src[6] +
             tx_mat[10 * 64 + k] * src[10] + tx_mat[14 * 64 + k] * src[14] +
             tx_mat[18 * 64 + k] * src[18] + tx_mat[22 * 64 + k] * src[22] +
             tx_mat[26 * 64 + k] * src[26] + tx_mat[30 * 64 + k] * src[30];
    }
    for (k = 0; k < 8; k++) {
      f[k] = tx_mat[4 * 64 + k] * src[4] + tx_mat[12 * 64 + k] * src[12] +
             tx_mat[20 * 64 + k] * src[20] + tx_mat[28 * 64 + k] * src[28];
    }
    for (k = 0; k < 4; k++) {
      h[k] = tx_mat[8 * 64 + k] * src[8] + tx_mat[24 * 64 + k] * src[24];
    }
    u[0] = tx_mat[16 * 64 + 0] * src[16];
    u[1] = tx_mat[16 * 64 + 1] * src[16];
    i[0] = tx_mat[0 * 64 + 0] * src[0];
    i[1] = tx_mat[0 * 64 + 1] * src[0];
    for (k = 0; k < 2; k++) {
      g[k] = i[k] + u[k];
      g[k + 2] = i[1 - k] - u[1 - k];
    }
    for (k = 0; k < 4; k++) {
      e[k] = g[k] + h[k];
      e[k + 4] = g[3 - k] - h[3 - k];
    }
    for (k = 0; k < 8; k++) {
      c[k] = e[k] + f[k];
      c[k + 8] = e[7 - k] - f[7 - k];
    }
    for (k = 0; k < 16; k++) {
      a[k] = c[k] + d[k];
      a[k + 16] = c[15 - k] - d[15 - k];
    }
    for (k = 0; k < 32; k++) {
      dst[(k)*line] =
          clamp((a[k] + b[k] + offset) >> shift, coef_min, coef_max);
      dst[(k + 32) * line] =
          clamp((a[31 - k] - b[31 - k] + offset) >> shift, coef_min, coef_max);
    }
    src += tx1d_size;
    dst++;
  }

  if (skip_line) {
    for (j = 0; j < tx1d_size; j++) {
      memset(dst, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}
#endif  // !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG

// ********************************** IDTX **********************************
void inv_txfm_idtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int scale = 128;

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      dst[j * line + i] =
          clamp((int)(src[i * tx1d_size + j] * scale + offset) >> shift,
                coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_idtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int scale = 181;

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      dst[j * line + i] =
          clamp((int)(src[i * tx1d_size + j] * scale + offset) >> shift,
                coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_idtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int scale = 256;

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      dst[j * line + i] =
          clamp((int)(src[i * tx1d_size + j] * scale + offset) >> shift,
                coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_idtx_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 32;
  const int scale = 362;

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      dst[j * line + i] =
          clamp((int)(src[i * tx1d_size + j] * scale + offset) >> shift,
                coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_adst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_adst_size4[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fdst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_fdst_size4[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_adst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_adst_size8[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fdst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_fdst_size8[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_adst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_adst_size16[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fdst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_fdst_size16[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_ddtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_ddtx_size4[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_ddtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_ddtx_size8[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_ddtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_ddtx_size16[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] * tx_mat[k * tx1d_size + j];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fddt_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 4;
  const int *tx_mat = tx_kernel_ddtx_size4[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] *
               tx_mat[k * tx1d_size + (tx1d_size - 1 - j)];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fddt_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line, const int coef_min,
                           const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 8;
  const int *tx_mat = tx_kernel_ddtx_size8[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] *
               tx_mat[k * tx1d_size + (tx1d_size - 1 - j)];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_txfm_fddt_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line, const int coef_min,
                            const int coef_max) {
  (void)zero_line;
  const int offset = 1 << (shift - 1);
  const int nz_line = line - skip_line;
  const int tx1d_size = 16;
  const int *tx_mat = tx_kernel_ddtx_size16[INV_TXFM][0];

  for (int i = 0; i < nz_line; i++) {
    for (int j = 0; j < tx1d_size; j++) {
      int sum = 0;
      for (int k = 0; k < tx1d_size; k++) {
        sum += src[i * tx1d_size + k] *
               tx_mat[k * tx1d_size + (tx1d_size - 1 - j)];
      }
      dst[j * line + i] =
          clamp((int)(sum + offset) >> shift, coef_min, coef_max);
    }
  }

  if (skip_line) {
    for (int j = 0; j < tx1d_size; j++) {
      memset(dst + nz_line, 0, sizeof(*dst) * skip_line);
      dst += line;
    }
  }
}

void inv_transform_1d_c(const int *src, int *dst, int shift, int line,
                        int skip_line, int zero_line, const int coef_min,
                        const int coef_max, const int tx_type_index,
                        const int size_index) {
  switch (size_index) {
    case 0:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size4_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 1:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size8_c(src, dst, shift, line, skip_line, zero_line,
                                coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 2:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 2:
          inv_txfm_adst_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 3:
          inv_txfm_fdst_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 4:
          inv_txfm_ddtx_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 5:
          inv_txfm_fddt_size16_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
    case 3:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size32_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        case 1:
          inv_txfm_idtx_size32_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
#if !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
    case 4:
      switch (tx_type_index) {
        case 0:
          inv_txfm_dct2_size64_c(src, dst, shift, line, skip_line, zero_line,
                                 coef_min, coef_max);
          break;
        default: assert(0); break;
      }
      break;
#endif  // !CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG
    default: assert(0); break;
  }
}

void inv_txfm_c(const tran_low_t *input, uint16_t *dest, int stride,
                const TxfmParam *txfm_param) {
  const TX_SIZE tx_size = txfm_param->tx_size;
  TX_TYPE tx_type = txfm_param->tx_type;

#if CONFIG_CHROMA_LARGE_TX
#if CONFIG_TX64
#if CONFIG_TX64_SEQ_FLAG
  const int use_resample =
      (txfm_param->plane_type == PLANE_TYPE_UV || txfm_param->t64resample) ? 1
                                                                           : 0;
  int width = AOMMIN(MAX_TX_SIZE >> use_resample, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> use_resample, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - use_resample, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - use_resample, tx_size_high_log2[tx_size]) - 2;
#else
  int width = AOMMIN(MAX_TX_SIZE >> 1, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> 1, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - 1, tx_size_high_log2[tx_size]) - 2;
#endif  // CONFIG_TX64_SEQ_FLAG
#else
  const int is_chroma = (txfm_param->plane_type == PLANE_TYPE_UV) ? 1 : 0;
  int width = AOMMIN(MAX_TX_SIZE >> is_chroma, tx_size_wide[tx_size]);
  int height = AOMMIN(MAX_TX_SIZE >> is_chroma, tx_size_high[tx_size]);
  const uint32_t tx_wide_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - is_chroma, tx_size_wide_log2[tx_size]) - 2;
  const uint32_t tx_high_index =
      AOMMIN(MAX_TX_SIZE_LOG2 - is_chroma, tx_size_high_log2[tx_size]) - 2;
#endif  // CONFIG_TX64
#else
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const uint32_t tx_wide_index = tx_size_wide_log2[tx_size] - 2;
  const uint32_t tx_high_index = tx_size_high_log2[tx_size] - 2;
#endif  // CONFIG_CHROMA_LARGE_TX || CONFIG_TX64

  const int intermediate_bitdepth = txfm_param->bd + 8;
  const int rng_min = -(1 << (intermediate_bitdepth - 1));
  const int rng_max = (1 << (intermediate_bitdepth - 1)) - 1;

#if CONFIG_CHROMA_LARGE_TX
  const int col_rng_min = -(1 << txfm_param->bd);
  const int col_rng_max = (1 << txfm_param->bd) - 1;
#endif  // CONFIG_CHROMA_LARGE_TX

  if (txfm_param->lossless) {
    assert(tx_type == DCT_DCT);
    av1_highbd_iwht4x4_add(input, dest, stride, txfm_param->eob,
                           txfm_param->bd);
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

  int skipWidth = width > 32 ? width - 32 : 0;
  int skipHeight = height > 32 ? height - 32 : 0;

  int block[MAX_TX_SQUARE] = { 0 };
  int tmp[MAX_TX_SQUARE] = { 0 };

  const int log2width = tx_size_wide_log2[tx_size];
  const int log2height = tx_size_high_log2[tx_size];
  const int sqrt2 = ((log2width + log2height) & 1) ? 1 : 0;

  if (sqrt2) {
    for (int i = 0; i < AOMMIN(1024, width * height); i++) {
      tmp[i] = round_shift((int64_t)input[i] * NewInvSqrt2, NewSqrt2Bits);
    }
  } else {
    memcpy(tmp, input, AOMMIN(1024, width * height) * sizeof(tran_low_t));
  }

  memcpy(block, tmp, AOMMIN(1024, width * height) * sizeof(*tmp));

  clamp_buf(block, AOMMIN(1024, width * height), txfm_param->bd + 8);

  if (skipWidth) {
    for (int y = 0; y < height; y++) {
      memcpy(block + y * width, tmp + y * 32, 32 * sizeof(*tmp));
      memset(block + y * width + 32, 0, 32 * sizeof(*tmp));
    }
  }

  if (skipHeight) {
    for (int y = 32; y < height; y++) {
      memset(block + y * width, 0, width * sizeof(*tmp));
    }
  }

  const int shift_1st = inv_tx_shift[tx_size][0];
  const int shift_2nd = inv_tx_shift[tx_size][1];

  assert(shift_1st >= 0);
  assert(shift_2nd >= 0);

  inv_transform_1d_c(block, tmp, shift_1st, height, skipHeight, skipWidth,
                     rng_min, rng_max, tx_type_row, tx_wide_index);

  inv_transform_1d_c(tmp, block, shift_2nd, width, 0, skipHeight
#if CONFIG_CHROMA_LARGE_TX
                     ,
                     col_rng_min, col_rng_max,
#else
                     ,
                     rng_min, rng_max,
#endif  // CONFIG_CHROMA_LARGE_TX
                     tx_type_col, tx_high_index);

#if CONFIG_CHROMA_LARGE_TX
  if (width < tx_size_wide[tx_size]) {
    assert(width == 32);
    memcpy(tmp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[y * 2 * width + 2 * x] = tmp[y * width + x];
        block[y * 2 * width + 2 * x + 1] = tmp[y * width + x];
      }
    }
    width = tx_size_wide[tx_size];
  }
  if (height < tx_size_high[tx_size]) {
    assert(height == 32);
    memcpy(tmp, block, width * height * sizeof(*block));
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        block[2 * y * width + x] = tmp[y * width + x];
        block[(2 * y + 1) * width + x] = tmp[y * width + x];
      }
    }
    height = tx_size_high[tx_size];
  }
#endif  // CONFIG_CHROMA_LARGE_TX

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      dest[y * stride + x] = highbd_clip_pixel_add(
          dest[y * stride + x], block[(y * width) + x], txfm_param->bd);
    }
  }
}

int av1_get_tx_scale(const TX_SIZE tx_size) {
  const int pels = tx_size_2d[tx_size];
  // Largest possible pels is 4096 (64x64).
  return (pels > 256) + (pels > 1024);
}

// NOTE: The implementation of all inverses need to be aware of the fact
// that input and output could be the same buffer.

void av1_lossless_inv_idtx_add_c(const tran_low_t *input, uint16_t *dest,
                                 int stride, const TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av1_get_tx_scale(txfm_param->tx_size);
  for (int i = 0; i < txh; i++) {
    for (int j = 0; j < txw; j++) {
      dest[i * stride + j] = highbd_clip_pixel_add(
          dest[i * stride + j], (input[i * txw + j] >> scale_bits),
          txfm_param->bd);
    }
  }
}

void av1_lossless_inv_idtx_add_vert_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, const TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av1_get_tx_scale(txfm_param->tx_size);
  for (int j = 0; j < txw; j++) {
    int delay = 0;
    for (int i = 0; i < txh; i++) {
      tran_low_t current_txcoeff = (input[i * txw + j] >> scale_bits);
      dest[i * stride + j] = highbd_clip_pixel_add(
          dest[i * stride + j], current_txcoeff + delay, txfm_param->bd);
      delay += current_txcoeff;
    }
  }
}

void av1_lossless_inv_idtx_add_horz_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, const TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av1_get_tx_scale(txfm_param->tx_size);
  for (int i = 0; i < txh; i++) {
    int delay = 0;
    for (int j = 0; j < txw; j++) {
      tran_low_t current_txcoeff = (input[i * txw + j] >> scale_bits);
      dest[i * stride + j] = highbd_clip_pixel_add(
          dest[i * stride + j], current_txcoeff + delay, txfm_param->bd);
      delay += current_txcoeff;
    }
  }
}

// idct
void av1_highbd_iwht4x4_add(const tran_low_t *input, uint16_t *dest, int stride,
                            int eob, int bd) {
  if (eob > 1)
    av1_highbd_iwht4x4_16_add(input, dest, stride, bd);
  else
    av1_highbd_iwht4x4_1_add(input, dest, stride, bd);
}

// inverse hadamard transform for DPCM lossless vertical mode
void av1_highbd_iwht4x4_vert_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd) {
  if (eob > 1)
    av1_highbd_iwht4x4_16_vert_add(input, dest, stride, bd);
  else
    av1_highbd_iwht4x4_1_vert_add(input, dest, stride, bd);
}

// inverse hadamard transform for DPCM lossless horizontal mode
void av1_highbd_iwht4x4_horz_add(const tran_low_t *input, uint16_t *dest,
                                 int stride, int eob, int bd) {
  if (eob > 1)
    av1_highbd_iwht4x4_16_horz_add(input, dest, stride, bd);
  else
    av1_highbd_iwht4x4_1_horz_add(input, dest, stride, bd);
}

// inverse transform for 4x4 dpcm lossless vertical mode
void av1_highbd_inv_txfm_add_4x4_vert_c(const tran_low_t *input, uint16_t *dest,
                                        int stride,
                                        const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  int eob = txfm_param->eob;
  int bd = txfm_param->bd;
  int lossless = txfm_param->lossless;
  const TX_TYPE tx_type = txfm_param->tx_type;
  if (lossless) {
    assert(tx_type == DCT_DCT);
    (void)tx_type;
    av1_highbd_iwht4x4_vert_add(input, dest, stride, eob, bd);
    return;
  }
}

// inverse transform for 4x4 dpcm lossless horizontal mode
void av1_highbd_inv_txfm_add_4x4_horz_c(const tran_low_t *input, uint16_t *dest,
                                        int stride,
                                        const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  int eob = txfm_param->eob;
  int bd = txfm_param->bd;
  int lossless = txfm_param->lossless;
  const TX_TYPE tx_type = txfm_param->tx_type;
  if (lossless) {
    assert(tx_type == DCT_DCT);
    (void)tx_type;
    av1_highbd_iwht4x4_horz_add(input, dest, stride, eob, bd);
    return;
  }
}

static void init_txfm_param(const MACROBLOCKD *xd, int plane, TX_SIZE tx_size,
                            TX_TYPE tx_type, int eob, int reduced_tx_set,
                            int use_ddt, TxfmParam *txfm_param
#if CONFIG_TX64_SEQ_FLAG
                            ,
                            int t64resample
#endif  // CONFIG_TX64_SEQ_FLAG
) {
  (void)plane;
  MB_MODE_INFO *const mbmi = xd->mi[0];
#if CONFIG_CHROMA_LARGE_TX && (!CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG)
  txfm_param->plane_type = get_plane_type(plane);
#if CONFIG_TX64_SEQ_FLAG
  txfm_param->t64resample = t64resample;
#endif  // CONFIG_TX64_SEQ_FLAG
#endif  // CONFIG_CHROMA_LARGE_TX && (!CONFIG_TX64 || CONFIG_TX64_SEQ_FLAG)
  txfm_param->tx_type = get_primary_tx_type(tx_type);
  txfm_param->sec_tx_set = 0;
  txfm_param->sec_tx_type = 0;
  txfm_param->intra_mode = get_intra_mode(mbmi, plane);
  txfm_param->is_inter = is_inter_block(xd->mi[0], xd->tree_type);
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  bool mode_dependent_condition =
      (txfm_param->is_inter
           ? (txfm_param->tx_type == DCT_DCT && width >= 16 && height >= 16)
           : txfm_param->intra_mode < PAETH_PRED);
  if (mode_dependent_condition && !xd->lossless[mbmi->segment_id]) {
    // updated EOB condition
    txfm_param->sec_tx_type = get_secondary_tx_type(tx_type);
    txfm_param->sec_tx_set = get_secondary_tx_set(tx_type);
  }
  txfm_param->tx_size = tx_size;
  // EOB needs to adjusted after inverse IST
  if (txfm_param->sec_tx_type) {
    const int st_size_class =
        (width == 8 && height == 8 && txfm_param->tx_type == DCT_DCT) ? 1
        : (width >= 8 && height >= 8) ? (txfm_param->tx_type == DCT_DCT ? 2 : 3)
                                      : 0;
    txfm_param->eob =
        (st_size_class == 0) ? IST_4x4_HEIGHT
        : (st_size_class == 1)
            ? IST_8x8_HEIGHT_RED
            : ((st_size_class == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
  } else {
    txfm_param->eob = eob;
  }
  txfm_param->use_ddt = use_ddt;
  txfm_param->lossless = xd->lossless[xd->mi[0]->segment_id];
  txfm_param->bd = xd->bd;
  txfm_param->tx_set_type = av1_get_ext_tx_set_type(
      txfm_param->tx_size, is_inter_block(xd->mi[0], xd->tree_type),
      reduced_tx_set);
}

void av1_highbd_inv_txfm_add_c(const tran_low_t *input, uint16_t *dest,
                               int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  inv_txfm(input, dest, stride, txfm_param);
}

// inverse transform for dpcm lossless horizontal mode
void av1_highbd_inv_txfm_add_horz_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const TX_SIZE tx_size = txfm_param->tx_size;
  assert(txfm_param->lossless);
  if (txfm_param->tx_type == IDTX) {
    av1_lossless_inv_idtx_add_horz(input, dest, stride, txfm_param);
    return;
  }
  switch (tx_size) {
    case TX_4X4:
      av1_highbd_inv_txfm_add_4x4_horz_c(input, dest, stride, txfm_param);
      break;
    default: assert(0 && "Invalid transform size for lossless coding"); break;
  }
}

// inverse transform for dpcm lossless vertical mode
void av1_highbd_inv_txfm_add_vert_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const TX_SIZE tx_size = txfm_param->tx_size;
  assert(txfm_param->lossless);
  if (txfm_param->tx_type == IDTX) {
    av1_lossless_inv_idtx_add_vert(input, dest, stride, txfm_param);
    return;
  }
  switch (tx_size) {
    case TX_4X4:
      av1_highbd_inv_txfm_add_4x4_vert_c(input, dest, stride, txfm_param);
      break;
    default: assert(0 && "Invalid transform size for lossless coding"); break;
  }
}

// Apply inverse cross chroma component transform
void av1_inv_cross_chroma_tx_block(tran_low_t *dqcoeff_c1,
                                   tran_low_t *dqcoeff_c2, TX_SIZE tx_size,
                                   CctxType cctx_type, const int bd) {
  if (cctx_type == CCTX_NONE) return;
  const int ncoeffs = av1_get_max_eob(tx_size);
  int32_t *src_c1 = (int32_t *)dqcoeff_c1;
  int32_t *src_c2 = (int32_t *)dqcoeff_c2;
  int64_t tmp[2] = { 0, 0 };

  const int angle_idx = cctx_type - CCTX_START;
  for (int i = 0; i < ncoeffs; i++) {
    tmp[0] = (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c1[i] -
             (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c2[i];
    tmp[1] = (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c1[i] +
             (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c2[i];
    src_c1[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[0], CCTX_PREC_BITS);
    src_c2[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[1], CCTX_PREC_BITS);
    src_c1[i] = clamp_value(src_c1[i], 8 + bd);
    src_c2[i] = clamp_value(src_c2[i], 8 + bd);
  }
}

static void av1_highbd_inv_txfm_add_master(const tran_low_t *input,
                                           uint16_t *dest, int stride,
                                           const TxfmParam *txfm_param) {
  if (txfm_param->lossless) {
    if (txfm_param->tx_type == IDTX) {
      av1_lossless_inv_idtx_add(input, dest, stride, txfm_param);
      return;
    }
  }
  av1_highbd_inv_txfm_add(input, dest, stride, txfm_param);
}

void av1_inverse_transform_block(const MACROBLOCKD *xd,
                                 const tran_low_t *dqcoeff, int plane,
                                 TX_TYPE tx_type, TX_SIZE tx_size,
                                 uint16_t *dst, int stride, int eob,
                                 int use_ddt, int reduced_tx_set
#if CONFIG_TX64_SEQ_FLAG
                                 ,
                                 int t64resample
#endif  // CONFIG_TX64_SEQ_FLAG
) {
  if (!eob) return;

  assert(eob <= av1_get_max_eob(tx_size));

  TxfmParam txfm_param;
  init_txfm_param(xd, plane, tx_size, tx_type, eob, reduced_tx_set, use_ddt,
                  &txfm_param
#if CONFIG_TX64_SEQ_FLAG
                  ,
                  t64resample
#endif  // CONFIG_TX64_SEQ_FLAG
  );
  assert(av1_ext_tx_used[txfm_param.tx_set_type][txfm_param.tx_type]);
  assert(IMPLIES(txfm_param.sec_tx_type,
                 block_signals_sec_tx_type(xd, tx_size, txfm_param.tx_type,
                                           txfm_param.eob)));

  // Work buffer for secondary transform
  DECLARE_ALIGNED(32, tran_low_t, temp_dqcoeff[MAX_TX_SQUARE]);
  memcpy(temp_dqcoeff, dqcoeff, sizeof(tran_low_t) * tx_size_2d[tx_size]);

  av1_inv_stxfm(temp_dqcoeff, &txfm_param);

  MB_MODE_INFO *const mbmi = xd->mi[0];
  if (xd->lossless[mbmi->segment_id]) {
    PREDICTION_MODE cur_pred_mode =
        (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
    int cur_dpcm_flag =
        (plane == AOM_PLANE_Y) ? mbmi->use_dpcm_y : mbmi->use_dpcm_uv;
    int cur_angle_delta = (plane == AOM_PLANE_Y) ? mbmi->angle_delta[0] : 0;
    if (cur_pred_mode == V_PRED && cur_angle_delta == 0 && cur_dpcm_flag > 0) {
      av1_highbd_inv_txfm_add_vert(temp_dqcoeff, dst, stride, &txfm_param);
    } else if (cur_pred_mode == H_PRED && cur_angle_delta == 0 &&
               cur_dpcm_flag > 0) {
      av1_highbd_inv_txfm_add_horz(temp_dqcoeff, dst, stride, &txfm_param);
    } else {
      av1_highbd_inv_txfm_add_master(temp_dqcoeff, dst, stride, &txfm_param);
    }
  } else {
    av1_highbd_inv_txfm_add_master(temp_dqcoeff, dst, stride, &txfm_param);
  }
}

// Inverse secondary transform
void inv_stxfm_c(tran_low_t *src, tran_low_t *dst, const PREDICTION_MODE mode,
                 const uint8_t stx_idx, const int size, const int bd) {
  assert(stx_idx < 4);
  const int16_t *kernel = (size == 0) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
  int *out = dst;
  const int shift = 7;

  int reduced_width, reduced_height;
  if (size == 0) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = (size == 1)
                         ? IST_8x8_HEIGHT_RED
                         : ((size == 3) ? IST_ADST_NZ_CNT : IST_8x8_HEIGHT);
    reduced_width = IST_8x8_WIDTH;
  }
  for (int j = 0; j < reduced_width; j++) {
    int32_t resi = 0;
    const int16_t *kernel_tmp = kernel;
    int *srcPtr = src;
    for (int i = 0; i < reduced_height; i++) {
      resi += *srcPtr++ * *kernel_tmp;
      kernel_tmp += reduced_width;
    }
    *out++ = clamp_value(ROUND_POWER_OF_TWO_SIGNED(resi, shift), 8 + bd);
    kernel++;
  }
}

void av1_inv_stxfm(tran_low_t *coeff, TxfmParam *txfm_param) {
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
    const int16_t *scan_order_out;
    // Align scan order of IST with primary transform scan order
    const SCAN_ORDER *scan_order_in =
        get_scan(txfm_param->tx_size, txfm_param->tx_type);
    const int16_t *const scan = scan_order_in->scan;
    tran_low_t buf0[64] = { 0 }, buf1[64] = { 0 };
    tran_low_t *tmp = buf0;
    tran_low_t *src = coeff;

    int reduced_width = sb_size == 8 ? IST_8x8_WIDTH : IST_4x4_WIDTH;
    for (int r = 0; r < reduced_width; r++) {
      // Align scan order of IST with primary transform scan order
      *tmp = src[scan[r]];
      tmp++;
    }
    int8_t transpose = 0;
    mode = AOMMIN(intra_mode, SMOOTH_H_PRED);
    if ((mode == H_PRED) || (mode == D157_PRED) || (mode == D67_PRED) ||
        (mode == SMOOTH_H_PRED))
      transpose = 1;
#if STX_COEFF_DEBUG
    fprintf(stderr,
            "[inv stx] inter %d ptx %d txs %dx%d tp %d stx_set %d stx_type %d\n"
            "(stx coeff)\n",
            txfm_param->is_inter, get_primary_tx_type(txfm_param->tx_type),
            width, height, transpose, txfm_param->sec_tx_set, stx_type);
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        fprintf(stderr, "%d,", coeff[i * width + j]);
      }
      fprintf(stderr, "\n");
    }
#endif  // STX_COEFF_DEBUG
    mode_t = txfm_param->sec_tx_set;
    assert(mode_t < IST_SET_SIZE);
    if (transpose) {
      scan_order_out = (sb_size == 4)
                           ? stx_scan_orders_transpose_4x4[log2width - 2]
                           : stx_scan_orders_transpose_8x8[log2width - 2];
    } else {
      scan_order_out = (sb_size == 4) ? stx_scan_orders_4x4[log2width - 2]
                                      : stx_scan_orders_8x8[log2width - 2];
    }
    const int st_size_class =
        (width == 8 && height == 8 && txfm_param->tx_type == DCT_DCT) ? 1
        : (width >= 8 && height >= 8) ? (txfm_param->tx_type == DCT_DCT ? 2 : 3)
                                      : 0;
    inv_stxfm(buf0, buf1, mode_t, stx_type - 1, st_size_class, txfm_param->bd);
    tmp = buf1;
    src = coeff;
    memset(src, 0, width * height * sizeof(tran_low_t));
    const int16_t *sup_reg_mapping =
        &coeff8x8_mapping[txfm_param->sec_tx_set * 3 + stx_type - 1][0];
    for (int r = 0; r < reduced_width; r++) {
      if (sb_size == 8)
        src[scan_order_out[sup_reg_mapping[r]]] = *tmp;
      else
        src[scan_order_out[r]] = *tmp;
      tmp++;
    }
#if STX_COEFF_DEBUG
    fprintf(stderr, "(ptx coeff)\n");
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        fprintf(stderr, "%d,", coeff[i * width + j]);
      }
      fprintf(stderr, "\n");
    }
#endif  // STX_COEFF_DEBUG
  }
}
