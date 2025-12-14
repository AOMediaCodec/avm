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

#include <immintrin.h>

#include "config/avm_dsp_rtcd.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/fft_common.h"

extern void avm_transpose_float_sse2(const float *A, float *B, int n);
extern void avm_fft_unpack_2d_output_sse2(const float *col_fft, float *output,
                                          int n);

// Generate the 1d forward transforms for float using _mm256
GEN_FFT_8(static INLINE void, avx2, float, __m256, _mm256_load_ps,
          _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
          _mm256_mul_ps);
GEN_FFT_16(static INLINE void, avx2, float, __m256, _mm256_load_ps,
           _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
           _mm256_mul_ps);
GEN_FFT_32(static INLINE void, avx2, float, __m256, _mm256_load_ps,
           _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
           _mm256_mul_ps);

void avm_fft8x8_float_avx2(const float *input, float *temp, float *output) {
  avm_fft_2d_gen(input, temp, output, 8, avm_fft1d_8_avx2,
                 avm_transpose_float_sse2, avm_fft_unpack_2d_output_sse2, 8);
}

void avm_fft16x16_float_avx2(const float *input, float *temp, float *output) {
  avm_fft_2d_gen(input, temp, output, 16, avm_fft1d_16_avx2,
                 avm_transpose_float_sse2, avm_fft_unpack_2d_output_sse2, 8);
}

void avm_fft32x32_float_avx2(const float *input, float *temp, float *output) {
  avm_fft_2d_gen(input, temp, output, 32, avm_fft1d_32_avx2,
                 avm_transpose_float_sse2, avm_fft_unpack_2d_output_sse2, 8);
}

// Generate the 1d inverse transforms for float using _mm256
GEN_IFFT_8(static INLINE void, avx2, float, __m256, _mm256_load_ps,
           _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
           _mm256_mul_ps);
GEN_IFFT_16(static INLINE void, avx2, float, __m256, _mm256_load_ps,
            _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
            _mm256_mul_ps);
GEN_IFFT_32(static INLINE void, avx2, float, __m256, _mm256_load_ps,
            _mm256_store_ps, _mm256_set1_ps, _mm256_add_ps, _mm256_sub_ps,
            _mm256_mul_ps);

void avm_ifft8x8_float_avx2(const float *input, float *temp, float *output) {
  avm_ifft_2d_gen(input, temp, output, 8, avm_fft1d_8_float, avm_fft1d_8_avx2,
                  avm_ifft1d_8_avx2, avm_transpose_float_sse2, 8);
}

void avm_ifft16x16_float_avx2(const float *input, float *temp, float *output) {
  avm_ifft_2d_gen(input, temp, output, 16, avm_fft1d_16_float,
                  avm_fft1d_16_avx2, avm_ifft1d_16_avx2,
                  avm_transpose_float_sse2, 8);
}

void avm_ifft32x32_float_avx2(const float *input, float *temp, float *output) {
  avm_ifft_2d_gen(input, temp, output, 32, avm_fft1d_32_float,
                  avm_fft1d_32_avx2, avm_ifft1d_32_avx2,
                  avm_transpose_float_sse2, 8);
}
