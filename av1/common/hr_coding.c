/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av1/common/hr_coding.h"
#include "aom/internal/aom_codec_internal.h"

#if CONFIG_COEFF_HR_ADAPTIVE

/*
 * This is a table hosting the threshold values for deriving the
 * Rice parameter m based on input context value ctx. For context
 * value between two adjacent threshold values, the Rice parameter
 * m corresponds to the table index i, m=i+1, such that:
 *
 * adaptive_table[i] <= ctx < adaptive_table[i+1]
 *
 * For context values greater than 64, the Rice parameter stays at m=6.
 *
 */
static int adaptive_table[] = { 4, 8, 16, 32, 64 };

int get_adaptive_param(int ctx) {
  const int table_size = sizeof(adaptive_table) / sizeof(int);
  int m = 0;
  while (m < table_size && ctx >= adaptive_table[m]) ++m;
  return m + 1;
}

int get_truncated_rice_length(int level, int m, int k, int cmax) {
  int q = level >> m;
  if (q >= cmax) return cmax + get_exp_golomb_length(level - (cmax << m), k);

  return q + 1 + m;
}

int get_truncated_rice_length_diff(int level, int m, int k, int cmax,
                                   int *diff) {
  int q = level >> m;

  if (q >= cmax) {
    int lshifted = level - (cmax << m);
    if (lshifted == 0) {
      int golomb_len0 = k + 1;
      // diff = (cmax + golomb_len0) - (cmax - 1 + 1 + m)
      *diff = golomb_len0 - m;
      return cmax + golomb_len0;
    }
    return cmax + get_exp_golomb_length_diff(lshifted, k, diff);
  }

  if (level == 0) {
    *diff = m + 1;
    return m + 1;
  }

  *diff = level == (q << m);
  return q + 1 + m;
}

int get_adaptive_hr_length(int level, int ctx) {
  int m = get_adaptive_param(ctx);
  return get_truncated_rice_length(level, m, m + 1, AOMMIN(m + 4, 6));
}

int get_adaptive_hr_length_diff(int level, int ctx, int *diff) {
  int m = get_adaptive_param(ctx);
  return get_truncated_rice_length_diff(level, m, m + 1, AOMMIN(m + 4, 6),
                                        diff);
}

#endif  // CONFIG_COEFF_HR_ADAPTIVE
