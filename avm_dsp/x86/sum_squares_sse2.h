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

#ifndef AVM_DSP_X86_SUM_SQUARES_SSE2_H_
#define AVM_DSP_X86_SUM_SQUARES_SSE2_H_

uint64_t avm_sum_squares_2d_i16_nxn_sse2(const int16_t *src, int stride,
                                         int width, int height);

uint64_t avm_sum_squares_2d_i16_4xn_sse2(const int16_t *src, int stride,
                                         int height);
uint64_t avm_sum_squares_2d_i16_4x4_sse2(const int16_t *src, int stride);

uint64_t avm_sum_sse_2d_i16_4x4_sse2(const int16_t *src, int stride, int *sum);
uint64_t avm_sum_sse_2d_i16_4xn_sse2(const int16_t *src, int stride, int height,
                                     int *sum);
uint64_t avm_sum_sse_2d_i16_nxn_sse2(const int16_t *src, int stride, int width,
                                     int height, int *sum);

#endif  // AVM_DSP_X86_SUM_SQUARES_SSE2_H_
