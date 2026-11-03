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

#ifndef AVM_AV2_ENCODER_X86_HIGHBD_FWD_TXFM_X86_H_
#define AVM_AV2_ENCODER_X86_HIGHBD_FWD_TXFM_X86_H_

#include "config/avm_config.h"

#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

// AVX2 1D kernels, also used by the AVX-512 forward transform.
void fwd_txfm_dct2_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_dct2_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_dct2_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line);
void fwd_txfm_dct2_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line);
void fwd_txfm_idtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_idtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_idtx_size16_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line);
void fwd_txfm_idtx_size32_avx2(const int *src, int *dst, int shift, int line,
                               int skip_line, int zero_line);
void fwd_txfm_adst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_adst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_fdst_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_fdst_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_ddtx_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_ddtx_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_fddt_size4_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);
void fwd_txfm_fddt_size8_avx2(const int *src, int *dst, int shift, int line,
                              int skip_line, int zero_line);

// AVX2 row pass with 64-bit sums, for the sizes in row_pass_needs_64bit().
void fwd_transform_1d_64bit_avx2(const int *src, int *dst, int shift, int line,
                                 int skip_line, int zero_line,
                                 const int tx_type_index, const int size_index);

// At 12 bits the row-pass sums of these sizes exceed 32 bits.
static INLINE int row_pass_needs_64bit(TX_SIZE tx_size, int bd) {
  if (bd <= 10) return 0;
  return tx_size == TX_64X64 || tx_size == TX_64X32 || tx_size == TX_32X64 ||
         tx_size == TX_16X64;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_X86_HIGHBD_FWD_TXFM_X86_H_
