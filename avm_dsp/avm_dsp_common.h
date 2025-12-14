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

#ifndef AVM_AVM_DSP_AVM_DSP_COMMON_H_
#define AVM_AVM_DSP_AVM_DSP_COMMON_H_

#include "config/avm_config.h"

#include "avm/avm_integer.h"
#include "avm_ports/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVMMIN(x, y) (((x) < (y)) ? (x) : (y))
#define AVMMAX(x, y) (((x) > (y)) ? (x) : (y))
#define AVMSIGN(x) ((x) < 0 ? -1 : 0)

#define NELEMENTS(x) (int)(sizeof(x) / sizeof(x[0]))

#define IMPLIES(a, b) (!(a) || (b))  //  Logical 'a implies b' (or 'a -> b')

#define IS_POWER_OF_TWO(x) (((x) & ((x) - 1)) == 0)

/* Left shifting a negative value became undefined behavior in C99 (downgraded
   from merely implementation-defined in C89). This should still compile to the
   correct thing on any two's-complement machine, but avoid ubsan warnings.*/
#define AVM_SIGNED_SHL(x, shift) ((x) * (((x) * 0 + 1) << (shift)))

// These can be used to give a hint about branch outcomes.
// This can have an effect, even if your target processor has a
// good branch predictor, as these hints can affect basic block
// ordering by the compiler.
#ifdef __GNUC__
#define LIKELY(v) __builtin_expect(v, 1)
#define UNLIKELY(v) __builtin_expect(v, 0)
#else
#define LIKELY(v) (v)
#define UNLIKELY(v) (v)
#endif

typedef uint8_t qm_val_t;
#define AVM_QM_BITS 5

#define QUANT_TABLE_BITS 3
#define QUANT_FP_BITS 4

// Note:
// tran_low_t  is the datatype used for final transform coefficients.
// tran_high_t is the datatype used for intermediate transform stages.
typedef int64_t tran_high_t;
typedef int32_t tran_low_t;

static INLINE uint8_t clip_pixel(int val) {
  return (val > 255) ? 255 : (val < 0) ? 0 : val;
}

static INLINE int clamp(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

static INLINE int64_t clamp64(int64_t value, int64_t low, int64_t high) {
  return value < low ? low : (value > high ? high : value);
}

static INLINE int32_t clamp64_to_32(int64_t value) {
  return (int32_t)(clamp64(value, INT32_MIN, INT32_MAX));
}

static INLINE double fclamp(double value, double low, double high) {
  return value < low ? low : (value > high ? high : value);
}

static INLINE uint16_t clip_pixel_highbd(int val, int bd) {
  switch (bd) {
    case 8:
    default: return (uint16_t)clamp(val, 0, 255);
    case 10: return (uint16_t)clamp(val, 0, 1023);
    case 12: return (uint16_t)clamp(val, 0, 4095);
  }
}

// The result of this branchless code is equivalent to (value < 0 ? 0 : value)
// or max(0, value) and might be faster in some cases.
// Care should be taken since the behavior of right shifting signed type
// negative value is undefined by C standards and implementation defined,
static INLINE unsigned int negative_to_zero(int value) {
  return value & ~(value >> (sizeof(value) * 8 - 1));
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_AVM_DSP_COMMON_H_
