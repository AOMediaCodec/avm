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

#ifndef AVM_DSP_AVM_CONVOLVE_COMMON_H_
#define AVM_DSP_AVM_CONVOLVE_COMMON_H_

#include <stdint.h>

#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/avm_filter.h"

static INLINE const InterpKernel *get_filter_base(const int16_t *filter) {
  return (const InterpKernel *)(((intptr_t)filter) & ~((intptr_t)0xFF));
}

static INLINE int get_filter_offset(const int16_t *f,
                                    const InterpKernel *base) {
  return (int)((const InterpKernel *)(intptr_t)f - base);
}

#endif  // AVM_DSP_AVM_CONVOLVE_COMMON_H_
