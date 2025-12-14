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

#ifndef AVM_AV2_COMMON_COMMON_H_
#define AVM_AV2_COMMON_COMMON_H_

/* Interface header for common constant data structures and lookup tables */

#include <assert.h>

#include "avm_dsp/avm_dsp_common.h"
#include "avm_mem/avm_mem.h"
#include "avm/avm_integer.h"
#include "avm_ports/bitops.h"
#include "config/avm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PI 3.141592653589793238462643383279502884

// Only need this for fixed-size arrays, for structs just assign.
#define av2_copy(dest, src)              \
  {                                      \
    assert(sizeof(dest) == sizeof(src)); \
    memcpy(dest, src, sizeof(src));      \
  }

// Use this for variably-sized arrays.
#define av2_copy_array(dest, src, n)           \
  {                                            \
    assert(sizeof(*(dest)) == sizeof(*(src))); \
    memcpy(dest, src, n * sizeof(*(src)));     \
  }

#define av2_zero(dest) memset(&(dest), 0, sizeof(dest))
#define av2_zero_array(dest, n) memset(dest, 0, n * sizeof(*(dest)))

static INLINE int get_unsigned_bits(unsigned int num_values) {
  return num_values > 0 ? get_msb(num_values) + 1 : 0;
}

#define CHECK_MEM_ERROR(cm, lval, expr) \
  AVM_CHECK_MEM_ERROR(&cm->error, lval, expr)

#define AV2_MIN_TILE_SIZE_BYTES 1

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_COMMON_H_
