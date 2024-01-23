/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
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

void write_exp_golomb(aom_writer *w, int level, int k) {
  int x = level + (1 << k);
  int length = 0;

  length = get_msb(x) + 1;
  assert(length > k);

#if CONFIG_BYPASS_IMPROVEMENT
  aom_write_literal(w, 0, length - 1 - k);
  aom_write_literal(w, x, length);
#else
  for (int i = 0; i < length - 1 - k; ++i) aom_write_bit(w, 0);
  for (int i = length - 1; i >= 0; --i) aom_write_bit(w, (x >> i) & 0x01);
#endif  // CONFIG_BYPASS_IMPROVEMENT
}

int read_exp_golomb(MACROBLOCKD *xd, aom_reader *r, int k) {
#if CONFIG_BYPASS_IMPROVEMENT
  int length = aom_read_unary(r, 21, ACCT_INFO("hr"));
  if (length > 20) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid length in read_exp_golomb");
  }
  length += k;
  int x = 1 << length;
  x += aom_read_literal(r, length, ACCT_INFO("hr"));
#else
  int x = 1;
  int length = 0;
  int i = 0;
  while (!i) {
    i = aom_read_bit(r, ACCT_INFO("hr"));
    ++length;
    if (length > 20) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid length in read_exp_golomb");
      break;
    }
  }
  length += k;
  for (i = 0; i < length - 1; ++i) {
    x <<= 1;
    x += aom_read_bit(r, ACCT_INFO("hr"));
  }
#endif  // CONFIG_BYPASS_IMPROVEMENT

  return x - (1 << k);
}

#if CONFIG_COEFF_HR_ADAPTIVE

static int adaptive_table[] = { 4, 8, 16, 32, 64 };

static int get_adaptive_param(int ctx) {
  const int table_size = sizeof(adaptive_table) / sizeof(int);
  int m = 0;
  while (m < table_size && ctx >= adaptive_table[m]) ++m;
  return m + 1;
}

void write_truncated_rice(aom_writer *w, int level, int m, int k, int cmax) {
  int q = level >> m;

  if (q >= cmax) {
    aom_write_literal(w, 0, cmax);
    write_exp_golomb(w, level - (cmax << m), k);
  } else {
    const int mask = (1 << m) - 1;
    aom_write_literal(w, 0, q);
    aom_write_literal(w, 1, 1);
    aom_write_literal(w, level & mask, m);
  }
}

int read_truncated_rice(MACROBLOCKD *xd, aom_reader *r, int m, int k,
                        int cmax) {
  int q = aom_read_unary(r, cmax, ACCT_INFO("hr"));
  int rem = (q == cmax) ? read_exp_golomb(xd, r, k)
                        : aom_read_literal(r, m, ACCT_INFO("hr"));
  return rem + (q << m);
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

void write_adaptive_hr(aom_writer *w, int level, int ctx) {
  int m = get_adaptive_param(ctx);
  write_truncated_rice(w, level, m, m + 1, AOMMIN(m + 4, 6));
}

int read_adaptive_hr(MACROBLOCKD *xd, aom_reader *r, int ctx) {
  int m = get_adaptive_param(ctx);
  return read_truncated_rice(xd, r, m, m + 1, AOMMIN(m + 4, 6));
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
