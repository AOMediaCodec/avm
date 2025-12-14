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

#ifndef AVM_AV2_COMMON_COST_H_
#define AVM_AV2_COMMON_COST_H_

#include "avm_dsp/prob.h"
#include "avm/avm_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t av2_prob_cost[128];

// The factor to scale from cost in bits to cost in av2_prob_cost units.
#define AV2_PROB_COST_SHIFT 9

// Cost of coding an n bit literal, using 128 (i.e. 50%) probability
// for each bit.
#define av2_cost_literal(n) ((n) * (1 << AV2_PROB_COST_SHIFT))

// Calculate the cost of a symbol with probability p15 / 2^15
static INLINE int av2_cost_symbol(avm_cdf_prob p15) {
  // p15 can be out of range [1, CDF_PROB_TOP - 1]. Clamping it, so that the
  // following cost calculation works correctly. Otherwise, if p15 =
  // CDF_PROB_TOP, shift would be -1, and "p15 << shift" would be wrong.
  p15 = (avm_cdf_prob)clamp(p15, 1, CDF_PROB_TOP - 1);
  assert(0 < p15 && p15 < CDF_PROB_TOP);
  const int shift = CDF_PROB_BITS - 1 - get_msb(p15);
  const int prob = get_prob(p15 << shift, CDF_PROB_TOP);
  assert(prob >= 128);
  return av2_prob_cost[prob - 128] + av2_cost_literal(shift);
}

void av2_cost_tokens_from_cdf(int *costs, const avm_cdf_prob *cdf,
                              const int nsym, const int *inv_map);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_COST_H_
