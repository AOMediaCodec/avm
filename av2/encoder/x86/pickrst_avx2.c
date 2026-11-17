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

#include <immintrin.h>  // AVX2
#include "avm_dsp/x86/mem_sse2.h"
#include "avm_dsp/x86/synonyms.h"
#include "avm_dsp/x86/synonyms_avx2.h"
#include "avm_dsp/x86/transpose_sse2.h"

#include "config/av2_rtcd.h"
#include "av2/common/restoration.h"
#include "av2/encoder/pickrst.h"

void av2_accumulate_wienerns_correlation_avx2(double *A_base, double *b_base,
                                              const int16_t *buf, int16_t y,
                                              int num_feat) {
  double buf_d[WIENERNS_TAPS_MAX];
  for (int k = 0; k < num_feat; ++k) {
    buf_d[k] = (double)buf[k];
  }
  const double yd = (double)y;
  const __m256d vyd = _mm256_set1_pd(yd);

  int k = 0;
  for (; k + 3 < num_feat; k += 4) {
    __m256d vb = _mm256_loadu_pd(b_base + k);
    __m256d vbuf = _mm256_loadu_pd(buf_d + k);
    vb = _mm256_add_pd(vb, _mm256_mul_pd(vbuf, vyd));
    _mm256_storeu_pd(b_base + k, vb);
  }
  for (; k < num_feat; ++k) {
    b_base[k] += buf_d[k] * yd;
  }

  for (int i = 0; i < num_feat; ++i) {
    double *A_row = A_base + i * num_feat;
    const double bi = buf_d[i];
    const __m256d vbi = _mm256_set1_pd(bi);
    int j = 0;
    for (; j + 3 <= i; j += 4) {
      __m256d vA = _mm256_loadu_pd(A_row + j);
      __m256d vbj = _mm256_loadu_pd(buf_d + j);
      vA = _mm256_add_pd(vA, _mm256_mul_pd(vbi, vbj));
      _mm256_storeu_pd(A_row + j, vA);
    }
    for (; j <= i; ++j) {
      A_row[j] += bi * buf_d[j];
    }
  }
}
