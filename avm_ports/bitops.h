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

#ifndef AVM_AVM_PORTS_BITOPS_H_
#define AVM_AVM_PORTS_BITOPS_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "avm_ports/msvc.h"
#include "config/avm_config.h"

#ifdef _MSC_VER
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#define USE_MSC_INTRINSICS
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// get_msb:
// Returns (int)floor(log2(n)). n must be > 0.
// These versions of get_msb() are only valid when n != 0 because all
// of the optimized versions are undefined when n == 0:
// https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html

// use GNU builtins where available.
#if defined(__GNUC__) && \
    ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ >= 4)
static INLINE int get_msb(unsigned int n) {
  assert(n != 0);
  return 31 ^ __builtin_clz(n);
}
#elif defined(USE_MSC_INTRINSICS)
#pragma intrinsic(_BitScanReverse)

static INLINE int get_msb(unsigned int n) {
  unsigned long first_set_bit;
  assert(n != 0);
  _BitScanReverse(&first_set_bit, n);
  return first_set_bit;
}
#undef USE_MSC_INTRINSICS
#else
static INLINE int get_msb(unsigned int n) {
  int log = 0;
  unsigned int value = n;
  int i;

  assert(n != 0);

  for (i = 4; i >= 0; --i) {
    const int shift = (1 << i);
    const unsigned int x = value >> shift;
    if (x != 0) {
      value = x;
      log += shift;
    }
  }
  return log;
}
#endif

static INLINE int get_msb_signed(int32_t n) {
  return n == 0 ? 0 : get_msb((unsigned int)abs(n));
}

static INLINE int get_msb_signed_64(int64_t n) {
  uint64_t n_abs = (uint64_t)llabs(n);
  unsigned int high32 = n_abs >> 32;
  unsigned int low32 = n_abs & 0x00000000ffffffffULL;
  if (high32 != 0) return 32 + get_msb(high32);
  return low32 == 0 ? 0 : get_msb((unsigned int)low32);
}

// Returns (int)ceil(log2(n)).
static inline int avm_ceil_log2(int n) {
  if (n < 2) return 0;
  return get_msb(n - 1) + 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_PORTS_BITOPS_H_
