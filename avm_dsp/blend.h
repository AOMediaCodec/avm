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

#ifndef AVM_AVM_DSP_BLEND_H_
#define AVM_AVM_DSP_BLEND_H_

#include "avm_ports/mem.h"

// Various blending functions and macros.
// See also the avm_blend_* functions in avm_dsp_rtcd.h

// Alpha blending with alpha values from the range [0, 64], where 64
// means use the first input and 0 means use the second input.

#define AVM_BLEND_A64_ROUND_BITS 6
#define AVM_BLEND_A64_MAX_ALPHA (1 << AVM_BLEND_A64_ROUND_BITS)  // 64

#define AVM_BLEND_A64(a, v0, v1)                                          \
  ROUND_POWER_OF_TWO((a) * (v0) + (AVM_BLEND_A64_MAX_ALPHA - (a)) * (v1), \
                     AVM_BLEND_A64_ROUND_BITS)

// Alpha blending with alpha values from the range [0, 256], where 256
// means use the first input and 0 means use the second input.
#define AVM_BLEND_A256_ROUND_BITS 8
#define AVM_BLEND_A256_MAX_ALPHA (1 << AVM_BLEND_A256_ROUND_BITS)  // 256

#define AVM_BLEND_A256(a, v0, v1)                                          \
  ROUND_POWER_OF_TWO((a) * (v0) + (AVM_BLEND_A256_MAX_ALPHA - (a)) * (v1), \
                     AVM_BLEND_A256_ROUND_BITS)

// Blending by averaging.
#define AVM_BLEND_AVG(v0, v1) ROUND_POWER_OF_TWO((v0) + (v1), 1)

#define DIFF_FACTOR_LOG2 4
#define DIFF_FACTOR (1 << DIFF_FACTOR_LOG2)

#endif  // AVM_AVM_DSP_BLEND_H_
