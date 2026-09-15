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

#ifndef AVM_AV2_ENCODER_FWD_TXFM_INTERNAL_H_
#define AVM_AV2_ENCODER_FWD_TXFM_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

void fwd_txfm_dct2_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_dct2_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_dct2_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_dct2_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_dct2_size64_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_idtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_idtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_idtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_idtx_size32_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_adst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_adst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_adst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_fdst_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_fdst_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_fdst_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_ddtx_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_ddtx_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_ddtx_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);
void fwd_txfm_fddt_size4_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_fddt_size8_c(const int *src, int *dst, int shift, int line,
                           int skip_line, int zero_line);
void fwd_txfm_fddt_size16_c(const int *src, int *dst, int shift, int line,
                            int skip_line, int zero_line);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AV2_ENCODER_FWD_TXFM_INTERNAL_H_
