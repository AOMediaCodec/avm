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

#include <assert.h>
#include <emmintrin.h>
#include "avm_dsp/x86/synonyms.h"

#include "config/av2_rtcd.h"
#include "av2/common/restoration.h"
#include "av2/encoder/pickrst.h"

void av2_accumulate_wienerns_correlation_sse4_1(double *A_base, double *b_base,
                                                const int16_t *buf, int16_t y,
                                                int num_feat) {
  double buf_d[WIENERNS_TAPS_MAX];
  for (int k = 0; k < num_feat; ++k) {
    buf_d[k] = (double)buf[k];
  }
  const double yd = (double)y;
  const __m128d vyd = _mm_set1_pd(yd);

  int k = 0;
  for (; k + 1 < num_feat; k += 2) {
    __m128d vb = _mm_loadu_pd(b_base + k);
    __m128d vbuf = _mm_loadu_pd(buf_d + k);
    vb = _mm_add_pd(vb, _mm_mul_pd(vbuf, vyd));
    _mm_storeu_pd(b_base + k, vb);
  }
  for (; k < num_feat; ++k) {
    b_base[k] += buf_d[k] * yd;
  }

  for (int i = 0; i < num_feat; ++i) {
    double *A_row = A_base + i * num_feat;
    const double bi = buf_d[i];
    const __m128d vbi = _mm_set1_pd(bi);
    int j = 0;
    for (; j + 1 <= i; j += 2) {
      __m128d vA = _mm_loadu_pd(A_row + j);
      __m128d vbj = _mm_loadu_pd(buf_d + j);
      vA = _mm_add_pd(vA, _mm_mul_pd(vbi, vbj));
      _mm_storeu_pd(A_row + j, vA);
    }
    for (; j <= i; ++j) {
      A_row[j] += bi * buf_d[j];
    }
  }
}
