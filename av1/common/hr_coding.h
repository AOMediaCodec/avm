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

#ifndef AOM_AV1_COMMON_HR_CODING_H_
#define AOM_AV1_COMMON_HR_CODING_H_

#include "config/aom_config.h"

#include "aom_dsp/bitreader.h"
#include "aom_dsp/bitwriter.h"
#include "av1/common/blockd.h"

static INLINE int get_exp_golomb_length(int level, int k) {
  return 2 * get_msb(level + (1 << k)) + 1 - k;
}

static INLINE int get_exp_golomb_length_diff(int level, int k, int *diff) {
  if (level == 0) {
    *diff = k + 1;
    return k + 1;
  }

  int x = level + (1 << k);
  *diff = (x & (x - 1)) == 0 ? 2 : 0;
  return 2 * get_msb(x) + 1 - k;
}

#if CONFIG_COEFF_HR_ADAPTIVE
int get_adaptive_param(int ctx);
int get_truncated_rice_length(int level, int m, int k, int cmax);
int get_truncated_rice_length_diff(int level, int m, int k, int cmax,
                                   int *diff);

int get_adaptive_hr_length(int level, int ctx);
int get_adaptive_hr_length_diff(int level, int ctx, int *diff);

#endif  // CONFIG_COEFF_HR_ADAPTIVE

#endif  // AOM_AV1_COMMON_HR_CODING_H_
