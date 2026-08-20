/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av2/common/decoder_model.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#include "avm_mem/avm_mem.h"
#include "av2/common/annexA.h"
#include "av2/common/enums.h"
#include "av2/common/level.h"
#include "av2/common/tile_common.h"
#include "av2/common/timing.h"

_Static_assert(sizeof(uint32_t) * CHAR_BIT == 32, "uint32_t must be 32 bits");
_Static_assert(sizeof(uint64_t) * CHAR_BIT == 64, "uint64_t must be 64 bits");
_Static_assert(sizeof(Av2DmUnsignedWide) * CHAR_BIT == 256,
               "Av2DmUnsignedWide must be 256 bits");
_Static_assert(AV2_DM_MAX_REF_FRAMES == REF_FRAMES,
               "decoder-model VBI capacity must match REF_FRAMES");
_Static_assert(AV2_DM_MAX_BUFFER_POOL_SIZE >= REF_FRAMES + 2,
               "decoder-model BufferPool must hold REF_FRAMES + 2 buffers");

#define AV2_DM_WIDE_LIMBS 4
#define AV2_DM_BIG_UINT_INLINE_LIMBS (AV2_DM_WIDE_LIMBS * 2)

static Av2DmUnsignedWide wide_from_u64(uint64_t value) {
  Av2DmUnsignedWide result = { { value, 0, 0, 0 } };
  return result;
}

#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK \
  __attribute__((                         \
      no_sanitize("unsigned-integer-overflow", "unsigned-shift-base")))
#endif
#endif

#ifndef AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK
#define AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK
#endif

// Computes the complete 64-by-64-bit product using only fixed-width portable
// C arithmetic. Unsigned wraparound in the 32-bit partial-product assembly is
// intentional and defined by the C language.
AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK static void multiply_64(
    uint64_t left, uint64_t right, uint64_t *product_low,
    uint64_t *product_high) {
  const uint64_t mask = UINT32_MAX;
  const uint64_t left_low = left & mask;
  const uint64_t left_high = left >> 32;
  const uint64_t right_low = right & mask;
  const uint64_t right_high = right >> 32;
  const uint64_t low = left_low * right_low;
  const uint64_t middle_1 = left_high * right_low + (low >> 32);
  const uint64_t middle_2 = left_low * right_high + (middle_1 & mask);
  *product_high = left_high * right_high + (middle_1 >> 32) + (middle_2 >> 32);
  *product_low = (middle_2 << 32) | (low & mask);
}

#undef AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK

typedef struct Av2DmBigUInt {
  uint64_t *limbs;
  uint32_t count;
  uint32_t capacity;
  uint64_t inline_limbs[AV2_DM_BIG_UINT_INLINE_LIMBS];
} Av2DmBigUInt;

static _Thread_local int64_t rational_allocations_before_failure = -1;
static _Thread_local bool rational_allocation_failed;
static _Thread_local int64_t internal_allocations_before_failure = -1;
static _Thread_local bool internal_allocation_failed;
static atomic_uint_fast64_t rational_active_allocations = ATOMIC_VAR_INIT(0);

static void *internal_allocate(size_t count, size_t size, bool clear) {
  internal_allocation_failed = false;
  if (size != 0 && count > SIZE_MAX / size) return NULL;
  if (internal_allocations_before_failure >= 0) {
    if (internal_allocations_before_failure == 0) {
      internal_allocation_failed = true;
      return NULL;
    }
    --internal_allocations_before_failure;
  }
  void *const allocation =
      clear ? avm_calloc(count, size) : avm_malloc(count * size);
  if (allocation == NULL) internal_allocation_failed = true;
  return allocation;
}

static void *internal_calloc(size_t count, size_t size) {
  return internal_allocate(count, size, true);
}

static void *internal_malloc(size_t size) {
  return internal_allocate(1, size, false);
}

static void *rational_allocate(size_t size) {
  if (rational_allocations_before_failure >= 0) {
    if (rational_allocations_before_failure == 0) {
      rational_allocation_failed = true;
      return NULL;
    }
    --rational_allocations_before_failure;
  }
  void *const allocation = avm_malloc(size);
  if (allocation == NULL) rational_allocation_failed = true;
  if (allocation != NULL) atomic_fetch_add(&rational_active_allocations, 1);
  return allocation;
}

static void rational_deallocate(void *allocation) {
  if (allocation == NULL) return;
  avm_free(allocation);
  atomic_fetch_sub(&rational_active_allocations, 1);
}

void av2_dm_rational_set_allocation_failure_after_for_testing(
    int64_t successful_allocations) {
  rational_allocations_before_failure = successful_allocations;
  rational_allocation_failed = false;
}

void av2_dm_set_internal_allocation_failure_after_for_testing(
    int64_t successful_allocations) {
  internal_allocations_before_failure = successful_allocations;
  internal_allocation_failed = false;
}

uint64_t av2_dm_rational_allocation_count_for_testing(void) {
  return atomic_load(&rational_active_allocations);
}

bool av2_dm_rational_last_failure_was_allocation(void) {
  return rational_allocation_failed;
}

bool av2_dm_last_failure_was_allocation(void) {
  return rational_allocation_failed || internal_allocation_failed;
}

static void rational_begin_operation(void) {
  rational_allocation_failed = false;
  internal_allocation_failed = false;
}

static void big_uint_destroy(Av2DmBigUInt *value) {
  if (value->limbs != value->inline_limbs) {
    rational_deallocate(value->limbs);
  }
  memset(value, 0, sizeof(*value));
}

static void big_uint_trim(Av2DmBigUInt *value) {
  while (value->count != 0 && value->limbs[value->count - 1] == 0) {
    --value->count;
  }
}

static bool big_uint_allocate(Av2DmBigUInt *value, uint32_t capacity) {
  if (capacity == 0) return true;
  if ((uint64_t)capacity > SIZE_MAX / sizeof(*value->limbs)) return false;
  if (capacity <= AV2_DM_BIG_UINT_INLINE_LIMBS) {
    value->limbs = value->inline_limbs;
  } else {
    value->limbs = rational_allocate((size_t)capacity * sizeof(*value->limbs));
    if (value->limbs == NULL) return false;
  }
  memset(value->limbs, 0, (size_t)capacity * sizeof(*value->limbs));
  value->capacity = capacity;
  return true;
}

static bool big_uint_from_limbs(Av2DmBigUInt *value, const uint64_t *limbs,
                                uint32_t count) {
  while (count != 0 && limbs[count - 1] == 0) --count;
  if (!big_uint_allocate(value, count)) return false;
  if (count != 0) {
    memcpy(value->limbs, limbs, (size_t)count * sizeof(*limbs));
  }
  value->count = count;
  return true;
}

static bool big_uint_from_u64(Av2DmBigUInt *value, uint64_t scalar) {
  return scalar == 0 || (big_uint_allocate(value, 1) &&
                         (value->limbs[0] = scalar, value->count = 1, true));
}

static bool big_uint_copy(Av2DmBigUInt *destination,
                          const Av2DmBigUInt *source) {
  return big_uint_from_limbs(destination, source->limbs, source->count);
}

static void big_uint_move(Av2DmBigUInt *destination, Av2DmBigUInt *source) {
  big_uint_destroy(destination);
  if (source->limbs == source->inline_limbs) {
    destination->limbs = destination->inline_limbs;
    destination->count = source->count;
    destination->capacity = source->capacity;
    memcpy(destination->inline_limbs, source->inline_limbs,
           (size_t)source->capacity * sizeof(*source->inline_limbs));
  } else {
    *destination = *source;
  }
  memset(source, 0, sizeof(*source));
}

static bool big_uint_is_zero(const Av2DmBigUInt *value) {
  return value->count == 0;
}

static int big_uint_compare(const Av2DmBigUInt *left,
                            const Av2DmBigUInt *right) {
  if (left->count != right->count) return left->count < right->count ? -1 : 1;
  for (uint32_t i = left->count; i > 0; --i) {
    if (left->limbs[i - 1] != right->limbs[i - 1]) {
      return left->limbs[i - 1] < right->limbs[i - 1] ? -1 : 1;
    }
  }
  return 0;
}

static bool big_uint_add(const Av2DmBigUInt *left, const Av2DmBigUInt *right,
                         Av2DmBigUInt *result) {
  const uint32_t maximum =
      left->count > right->count ? left->count : right->count;
  if (maximum == UINT32_MAX || !big_uint_allocate(result, maximum + 1)) {
    return false;
  }
  uint64_t carry = 0;
  for (uint32_t i = 0; i < maximum; ++i) {
    const uint64_t left_limb = i < left->count ? left->limbs[i] : 0;
    const uint64_t right_limb = i < right->count ? right->limbs[i] : 0;
    const uint64_t partial = left_limb + right_limb;
    const uint64_t partial_carry = partial < left_limb;
    const uint64_t sum = partial + carry;
    const uint64_t carry_carry = sum < partial;
    result->limbs[i] = sum;
    carry = partial_carry | carry_carry;
  }
  result->limbs[maximum] = carry;
  result->count = maximum + (carry != 0);
  return true;
}

static bool big_uint_subtract(const Av2DmBigUInt *left,
                              const Av2DmBigUInt *right, Av2DmBigUInt *result) {
  if (big_uint_compare(left, right) < 0 ||
      !big_uint_allocate(result, left->count)) {
    return false;
  }
  uint64_t borrow = 0;
  for (uint32_t i = 0; i < left->count; ++i) {
    const uint64_t right_limb = i < right->count ? right->limbs[i] : 0;
    const uint64_t partial = left->limbs[i] - right_limb;
    const uint64_t partial_borrow = left->limbs[i] < right_limb;
    result->limbs[i] = partial - borrow;
    const uint64_t borrow_borrow = partial < borrow;
    borrow = partial_borrow | borrow_borrow;
  }
  if (borrow != 0) return false;
  result->count = left->count;
  big_uint_trim(result);
  return true;
}

static bool big_uint_multiply(const Av2DmBigUInt *left,
                              const Av2DmBigUInt *right, Av2DmBigUInt *result) {
  if (big_uint_is_zero(left) || big_uint_is_zero(right)) return true;
  if (UINT32_MAX - left->count < right->count ||
      !big_uint_allocate(result, left->count + right->count)) {
    return false;
  }
  for (uint32_t i = 0; i < left->count; ++i) {
    for (uint32_t j = 0; j < right->count; ++j) {
      uint64_t low;
      uint64_t high;
      multiply_64(left->limbs[i], right->limbs[j], &low, &high);
      uint32_t index = i + j;
      uint64_t old = result->limbs[index];
      result->limbs[index] += low;
      uint64_t carry = result->limbs[index] < old;
      ++index;
      old = result->limbs[index];
      result->limbs[index] += high;
      uint64_t next_carry = result->limbs[index] < old;
      old = result->limbs[index];
      result->limbs[index] += carry;
      carry = next_carry | (result->limbs[index] < old);
      ++index;
      while (carry != 0 && index < result->capacity) {
        ++result->limbs[index];
        carry = result->limbs[index] == 0;
        ++index;
      }
      if (carry != 0) return false;
    }
  }
  result->count = result->capacity;
  big_uint_trim(result);
  return true;
}

static uint32_t big_uint_bit_count(const Av2DmBigUInt *value) {
  if (value->count == 0) return 0;
  uint64_t high = value->limbs[value->count - 1];
  uint32_t high_bits = 0;
  while (high != 0) {
    ++high_bits;
    high >>= 1;
  }
  if (value->count - 1 > (UINT32_MAX - high_bits) / 64) return UINT32_MAX;
  return (value->count - 1) * 64 + high_bits;
}

static bool big_uint_shift_add_bit(Av2DmBigUInt *value, uint64_t bit) {
  uint64_t carry = bit;
  for (uint32_t i = 0; i < value->count; ++i) {
    const uint64_t next_carry = value->limbs[i] >> 63;
    value->limbs[i] = (value->limbs[i] << 1) | carry;
    carry = next_carry;
  }
  if (carry != 0) {
    if (value->count == value->capacity) return false;
    value->limbs[value->count++] = carry;
  } else if (value->count == 0 && bit != 0) {
    if (value->capacity == 0) return false;
    value->limbs[0] = bit;
    value->count = 1;
  }
  return true;
}

static bool big_uint_subtract_in_place(Av2DmBigUInt *left,
                                       const Av2DmBigUInt *right) {
  uint64_t borrow = 0;
  for (uint32_t i = 0; i < left->count; ++i) {
    const uint64_t right_limb = i < right->count ? right->limbs[i] : 0;
    const uint64_t partial = left->limbs[i] - right_limb;
    const uint64_t partial_borrow = left->limbs[i] < right_limb;
    left->limbs[i] = partial - borrow;
    const uint64_t borrow_borrow = partial < borrow;
    borrow = partial_borrow | borrow_borrow;
  }
  if (borrow != 0) return false;
  big_uint_trim(left);
  return true;
}

static bool big_uint_divide(const Av2DmBigUInt *dividend,
                            const Av2DmBigUInt *divisor, Av2DmBigUInt *quotient,
                            Av2DmBigUInt *remainder) {
  if (big_uint_is_zero(divisor)) return false;
  if (big_uint_compare(dividend, divisor) < 0) {
    return big_uint_copy(remainder, dividend);
  }
  if (!big_uint_allocate(quotient, dividend->count) ||
      divisor->count == UINT32_MAX ||
      !big_uint_allocate(remainder, divisor->count + 1)) {
    return false;
  }
  const uint32_t bits = big_uint_bit_count(dividend);
  if (bits == UINT32_MAX) return false;
  for (uint32_t bit_offset = bits; bit_offset > 0; --bit_offset) {
    const uint32_t bit = bit_offset - 1;
    if (!big_uint_shift_add_bit(
            remainder,
            (dividend->limbs[bit / 64] >> (bit % 64)) & UINT64_C(1))) {
      return false;
    }
    if (big_uint_compare(remainder, divisor) >= 0) {
      if (!big_uint_subtract_in_place(remainder, divisor)) return false;
      quotient->limbs[bit / 64] |= UINT64_C(1) << (bit % 64);
    }
  }
  quotient->count = quotient->capacity;
  big_uint_trim(quotient);
  return true;
}

static bool big_uint_divide_exact(const Av2DmBigUInt *dividend,
                                  const Av2DmBigUInt *divisor,
                                  Av2DmBigUInt *quotient) {
  Av2DmBigUInt remainder = { 0 };
  const bool divided = big_uint_divide(dividend, divisor, quotient, &remainder);
  const bool exact = divided && big_uint_is_zero(&remainder);
  big_uint_destroy(&remainder);
  return exact;
}

static bool big_uint_gcd(const Av2DmBigUInt *left, const Av2DmBigUInt *right,
                         Av2DmBigUInt *result) {
  Av2DmBigUInt a = { 0 };
  Av2DmBigUInt b = { 0 };
  bool ok = big_uint_copy(&a, left) && big_uint_copy(&b, right);
  while (ok && !big_uint_is_zero(&b)) {
    Av2DmBigUInt quotient = { 0 };
    Av2DmBigUInt remainder = { 0 };
    ok = big_uint_divide(&a, &b, &quotient, &remainder);
    big_uint_destroy(&quotient);
    if (ok) {
      big_uint_move(&a, &b);
      big_uint_move(&b, &remainder);
    }
    big_uint_destroy(&remainder);
  }
  if (ok) big_uint_move(result, &a);
  big_uint_destroy(&a);
  big_uint_destroy(&b);
  return ok;
}

static bool rational_component_view(const Av2DmRational *value,
                                    bool denominator, const uint64_t **limbs,
                                    uint32_t *count) {
  if (value->dynamic_limbs != NULL) {
    if (value->magnitude_limb_count == 0 ||
        value->denominator_limb_count == 0) {
      return false;
    }
    *limbs = value->dynamic_limbs;
    *count = value->magnitude_limb_count;
    if (denominator) {
      *limbs += value->magnitude_limb_count;
      *count = value->denominator_limb_count;
    }
    return true;
  }
  *limbs = denominator ? value->denominator.limbs : value->magnitude.limbs;
  *count = AV2_DM_WIDE_LIMBS;
  while (*count != 0 && (*limbs)[*count - 1] == 0) --*count;
  return true;
}

bool av2_dm_rational_get_component(const Av2DmRational *value, bool denominator,
                                   const uint64_t **limbs,
                                   uint32_t *limb_count) {
  return value != NULL && limbs != NULL && limb_count != NULL &&
         rational_component_view(value, denominator, limbs, limb_count);
}

static bool big_uint_from_rational(const Av2DmRational *value, bool denominator,
                                   Av2DmBigUInt *result) {
  const uint64_t *limbs;
  uint32_t count;
  return rational_component_view(value, denominator, &limbs, &count) &&
         big_uint_from_limbs(result, limbs, count);
}

void av2_dm_rational_init(Av2DmRational *value) {
  if (value == NULL) return;
  memset(value, 0, sizeof(*value));
  value->denominator.limbs[0] = 1;
  value->magnitude_limb_count = 1;
  value->denominator_limb_count = 1;
}

void av2_dm_rational_destroy(Av2DmRational *value) {
  if (value == NULL) return;
  rational_deallocate(value->dynamic_limbs);
  av2_dm_rational_init(value);
}

static bool rational_store(Av2DmRational *result, const Av2DmBigUInt *magnitude,
                           const Av2DmBigUInt *denominator, bool negative) {
  if (big_uint_is_zero(denominator)) return false;
  Av2DmRational temporary;
  av2_dm_rational_init(&temporary);
  const uint32_t magnitude_count = magnitude->count == 0 ? 1 : magnitude->count;
  const uint32_t denominator_count = denominator->count;
  temporary.magnitude_limb_count = magnitude_count;
  temporary.denominator_limb_count = denominator_count;
  temporary.negative = !big_uint_is_zero(magnitude) && negative;
  const uint32_t magnitude_copy = magnitude->count < AV2_DM_WIDE_LIMBS
                                      ? magnitude->count
                                      : AV2_DM_WIDE_LIMBS;
  const uint32_t denominator_copy = denominator->count < AV2_DM_WIDE_LIMBS
                                        ? denominator->count
                                        : AV2_DM_WIDE_LIMBS;
  if (magnitude_copy != 0) {
    memcpy(temporary.magnitude.limbs, magnitude->limbs,
           (size_t)magnitude_copy * sizeof(*magnitude->limbs));
  }
  memset(temporary.denominator.limbs, 0, sizeof(temporary.denominator.limbs));
  memcpy(temporary.denominator.limbs, denominator->limbs,
         (size_t)denominator_copy * sizeof(*denominator->limbs));
  if (magnitude->count > AV2_DM_WIDE_LIMBS ||
      denominator->count > AV2_DM_WIDE_LIMBS) {
    if (UINT32_MAX - magnitude_count < denominator_count ||
        (uint64_t)magnitude_count + denominator_count >
            SIZE_MAX / sizeof(*temporary.dynamic_limbs)) {
      return false;
    }
    const uint32_t total_count = magnitude_count + denominator_count;
    temporary.dynamic_limbs = rational_allocate(
        (size_t)total_count * sizeof(*temporary.dynamic_limbs));
    if (temporary.dynamic_limbs == NULL) return false;
    memset(temporary.dynamic_limbs, 0,
           (size_t)total_count * sizeof(*temporary.dynamic_limbs));
    if (magnitude->count != 0) {
      memcpy(temporary.dynamic_limbs, magnitude->limbs,
             (size_t)magnitude->count * sizeof(*magnitude->limbs));
    }
    memcpy(temporary.dynamic_limbs + magnitude_count, denominator->limbs,
           (size_t)denominator_count * sizeof(*denominator->limbs));
  }
  av2_dm_rational_move(result, &temporary);
  return true;
}

void av2_dm_rational_move(Av2DmRational *destination, Av2DmRational *source) {
  if (destination == NULL || source == NULL || destination == source) return;
  av2_dm_rational_destroy(destination);
  *destination = *source;
  av2_dm_rational_init(source);
}

bool av2_dm_rational_copy(Av2DmRational *destination,
                          const Av2DmRational *source) {
  rational_begin_operation();
  if (destination == NULL || source == NULL) return false;
  if (destination == source) return true;
  if (source->dynamic_limbs == NULL) {
    uint32_t magnitude_count = AV2_DM_WIDE_LIMBS;
    uint32_t denominator_count = AV2_DM_WIDE_LIMBS;
    while (magnitude_count != 0 &&
           source->magnitude.limbs[magnitude_count - 1] == 0) {
      --magnitude_count;
    }
    while (denominator_count != 0 &&
           source->denominator.limbs[denominator_count - 1] == 0) {
      --denominator_count;
    }
    if (denominator_count == 0) return false;
    Av2DmRational temporary = *source;
    temporary.magnitude_limb_count = magnitude_count == 0 ? 1 : magnitude_count;
    temporary.denominator_limb_count = denominator_count;
    temporary.negative = magnitude_count != 0 && source->negative;
    av2_dm_rational_move(destination, &temporary);
    return true;
  }
  Av2DmBigUInt magnitude = { 0 };
  Av2DmBigUInt denominator = { 0 };
  Av2DmRational temporary;
  av2_dm_rational_init(&temporary);
  const bool copied =
      big_uint_from_rational(source, false, &magnitude) &&
      big_uint_from_rational(source, true, &denominator) &&
      rational_store(&temporary, &magnitude, &denominator, source->negative);
  if (copied) av2_dm_rational_move(destination, &temporary);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  return copied;
}

static bool rational_normalize(Av2DmRational *value) {
  Av2DmBigUInt magnitude = { 0 };
  Av2DmBigUInt denominator = { 0 };
  Av2DmBigUInt divisor = { 0 };
  Av2DmBigUInt reduced_magnitude = { 0 };
  Av2DmBigUInt reduced_denominator = { 0 };
  bool ok = big_uint_from_rational(value, false, &magnitude) &&
            big_uint_from_rational(value, true, &denominator) &&
            !big_uint_is_zero(&denominator);
  if (ok && big_uint_is_zero(&magnitude)) {
    big_uint_destroy(&denominator);
    ok = big_uint_from_u64(&denominator, 1);
  } else if (ok) {
    ok = big_uint_gcd(&magnitude, &denominator, &divisor) &&
         big_uint_divide_exact(&magnitude, &divisor, &reduced_magnitude) &&
         big_uint_divide_exact(&denominator, &divisor, &reduced_denominator);
    if (ok) {
      big_uint_move(&magnitude, &reduced_magnitude);
      big_uint_move(&denominator, &reduced_denominator);
    }
  }
  if (ok) ok = rational_store(value, &magnitude, &denominator, value->negative);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  big_uint_destroy(&divisor);
  big_uint_destroy(&reduced_magnitude);
  big_uint_destroy(&reduced_denominator);
  return ok;
}

bool av2_dm_rational_make(uint64_t numerator, uint64_t denominator,
                          Av2DmRational *result) {
  return av2_dm_rational_make_wide(wide_from_u64(numerator), denominator, false,
                                   result);
}

bool av2_dm_rational_make_wide(Av2DmUnsignedWide numerator,
                               uint64_t denominator, bool negative,
                               Av2DmRational *result) {
  rational_begin_operation();
  if (result == NULL || denominator == 0) return false;
  Av2DmBigUInt magnitude = { 0 };
  Av2DmBigUInt rational_denominator = { 0 };
  Av2DmRational temporary;
  av2_dm_rational_init(&temporary);
  const bool made =
      big_uint_from_limbs(&magnitude, numerator.limbs, AV2_DM_WIDE_LIMBS) &&
      big_uint_from_u64(&rational_denominator, denominator) &&
      rational_store(&temporary, &magnitude, &rational_denominator, negative) &&
      rational_normalize(&temporary);
  if (made) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&rational_denominator);
  return made;
}

static bool rational_normalized_copy(const Av2DmRational *source,
                                     Av2DmRational *destination) {
  return av2_dm_rational_copy(destination, source) &&
         rational_normalize(destination);
}

bool av2_dm_rational_add(const Av2DmRational *left, const Av2DmRational *right,
                         Av2DmRational *result) {
  rational_begin_operation();
  if (left == NULL || right == NULL || result == NULL) return false;
  Av2DmRational normalized_left;
  Av2DmRational normalized_right;
  Av2DmRational temporary;
  av2_dm_rational_init(&normalized_left);
  av2_dm_rational_init(&normalized_right);
  av2_dm_rational_init(&temporary);
  Av2DmBigUInt left_magnitude = { 0 }, right_magnitude = { 0 };
  Av2DmBigUInt left_denominator = { 0 }, right_denominator = { 0 };
  Av2DmBigUInt denominator_gcd = { 0 }, left_multiplier = { 0 };
  Av2DmBigUInt right_multiplier = { 0 }, denominator = { 0 };
  Av2DmBigUInt scaled_left = { 0 }, scaled_right = { 0 }, magnitude = { 0 };
  bool negative = false;
  bool ok =
      rational_normalized_copy(left, &normalized_left) &&
      rational_normalized_copy(right, &normalized_right) &&
      big_uint_from_rational(&normalized_left, false, &left_magnitude) &&
      big_uint_from_rational(&normalized_right, false, &right_magnitude) &&
      big_uint_from_rational(&normalized_left, true, &left_denominator) &&
      big_uint_from_rational(&normalized_right, true, &right_denominator) &&
      big_uint_gcd(&left_denominator, &right_denominator, &denominator_gcd) &&
      big_uint_divide_exact(&right_denominator, &denominator_gcd,
                            &left_multiplier) &&
      big_uint_divide_exact(&left_denominator, &denominator_gcd,
                            &right_multiplier) &&
      big_uint_multiply(&left_denominator, &left_multiplier, &denominator) &&
      big_uint_multiply(&left_magnitude, &left_multiplier, &scaled_left) &&
      big_uint_multiply(&right_magnitude, &right_multiplier, &scaled_right);
  if (ok && normalized_left.negative == normalized_right.negative) {
    ok = big_uint_add(&scaled_left, &scaled_right, &magnitude);
    negative = normalized_left.negative;
  } else if (ok && big_uint_compare(&scaled_left, &scaled_right) >= 0) {
    ok = big_uint_subtract(&scaled_left, &scaled_right, &magnitude);
    negative = normalized_left.negative;
  } else if (ok) {
    ok = big_uint_subtract(&scaled_right, &scaled_left, &magnitude);
    negative = normalized_right.negative;
  }
  if (ok) {
    ok = rational_store(&temporary, &magnitude, &denominator, negative) &&
         rational_normalize(&temporary);
  }
  if (ok) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&normalized_left);
  av2_dm_rational_destroy(&normalized_right);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&left_magnitude);
  big_uint_destroy(&right_magnitude);
  big_uint_destroy(&left_denominator);
  big_uint_destroy(&right_denominator);
  big_uint_destroy(&denominator_gcd);
  big_uint_destroy(&left_multiplier);
  big_uint_destroy(&right_multiplier);
  big_uint_destroy(&denominator);
  big_uint_destroy(&scaled_left);
  big_uint_destroy(&scaled_right);
  big_uint_destroy(&magnitude);
  return ok;
}

bool av2_dm_rational_subtract(const Av2DmRational *left,
                              const Av2DmRational *right,
                              Av2DmRational *result) {
  rational_begin_operation();
  if (right == NULL) return false;
  Av2DmRational negated_right;
  av2_dm_rational_init(&negated_right);
  const bool copied = av2_dm_rational_copy(&negated_right, right);
  if (copied && !av2_dm_rational_is_zero(&negated_right)) {
    negated_right.negative = !negated_right.negative;
  }
  const bool subtracted =
      copied && av2_dm_rational_add(left, &negated_right, result);
  av2_dm_rational_destroy(&negated_right);
  return subtracted;
}

bool av2_dm_rational_multiply_u64(const Av2DmRational *value,
                                  uint64_t multiplier, Av2DmRational *result) {
  rational_begin_operation();
  if (value == NULL || result == NULL) return false;
  Av2DmRational normalized;
  Av2DmRational temporary;
  av2_dm_rational_init(&normalized);
  av2_dm_rational_init(&temporary);
  Av2DmBigUInt magnitude = { 0 }, denominator = { 0 }, factor = { 0 };
  Av2DmBigUInt divisor = { 0 }, reduced_denominator = { 0 };
  Av2DmBigUInt reduced_factor = { 0 }, product = { 0 };
  bool ok =
      rational_normalized_copy(value, &normalized) &&
      big_uint_from_rational(&normalized, false, &magnitude) &&
      big_uint_from_rational(&normalized, true, &denominator) &&
      big_uint_from_u64(&factor, multiplier) &&
      big_uint_gcd(&factor, &denominator, &divisor) &&
      big_uint_divide_exact(&denominator, &divisor, &reduced_denominator) &&
      big_uint_divide_exact(&factor, &divisor, &reduced_factor) &&
      big_uint_multiply(&magnitude, &reduced_factor, &product) &&
      rational_store(&temporary, &product, &reduced_denominator,
                     normalized.negative);
  if (ok) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&normalized);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  big_uint_destroy(&factor);
  big_uint_destroy(&divisor);
  big_uint_destroy(&reduced_denominator);
  big_uint_destroy(&reduced_factor);
  big_uint_destroy(&product);
  return ok;
}

bool av2_dm_rational_divide_u64(const Av2DmRational *value, uint64_t divisor,
                                Av2DmRational *result) {
  rational_begin_operation();
  if (value == NULL || result == NULL || divisor == 0) return false;
  Av2DmRational normalized;
  Av2DmRational temporary;
  av2_dm_rational_init(&normalized);
  av2_dm_rational_init(&temporary);
  Av2DmBigUInt magnitude = { 0 }, denominator = { 0 }, factor = { 0 };
  Av2DmBigUInt common = { 0 }, reduced_magnitude = { 0 };
  Av2DmBigUInt reduced_factor = { 0 }, product = { 0 };
  bool ok = rational_normalized_copy(value, &normalized) &&
            big_uint_from_rational(&normalized, false, &magnitude) &&
            big_uint_from_rational(&normalized, true, &denominator) &&
            big_uint_from_u64(&factor, divisor) &&
            big_uint_gcd(&magnitude, &factor, &common) &&
            big_uint_divide_exact(&magnitude, &common, &reduced_magnitude) &&
            big_uint_divide_exact(&factor, &common, &reduced_factor) &&
            big_uint_multiply(&denominator, &reduced_factor, &product) &&
            rational_store(&temporary, &reduced_magnitude, &product,
                           normalized.negative);
  if (ok) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&normalized);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  big_uint_destroy(&factor);
  big_uint_destroy(&common);
  big_uint_destroy(&reduced_magnitude);
  big_uint_destroy(&reduced_factor);
  big_uint_destroy(&product);
  return ok;
}

bool av2_dm_rational_compare(const Av2DmRational *left,
                             const Av2DmRational *right, int *comparison) {
  rational_begin_operation();
  if (left == NULL || right == NULL || comparison == NULL) return false;
  Av2DmBigUInt left_magnitude = { 0 }, right_magnitude = { 0 };
  Av2DmBigUInt left_denominator = { 0 }, right_denominator = { 0 };
  Av2DmBigUInt left_product = { 0 }, right_product = { 0 };
  bool ok = big_uint_from_rational(left, false, &left_magnitude) &&
            big_uint_from_rational(right, false, &right_magnitude) &&
            big_uint_from_rational(left, true, &left_denominator) &&
            big_uint_from_rational(right, true, &right_denominator) &&
            !big_uint_is_zero(&left_denominator) &&
            !big_uint_is_zero(&right_denominator);
  if (ok && big_uint_is_zero(&left_magnitude) &&
      big_uint_is_zero(&right_magnitude)) {
    *comparison = 0;
  } else if (ok && big_uint_is_zero(&left_magnitude)) {
    *comparison = right->negative ? 1 : -1;
  } else if (ok && big_uint_is_zero(&right_magnitude)) {
    *comparison = left->negative ? -1 : 1;
  } else if (ok && left->negative != right->negative) {
    *comparison = left->negative ? -1 : 1;
  } else if (ok) {
    ok =
        big_uint_multiply(&left_magnitude, &right_denominator, &left_product) &&
        big_uint_multiply(&right_magnitude, &left_denominator, &right_product);
    if (ok) {
      *comparison = big_uint_compare(&left_product, &right_product);
      if (left->negative) *comparison = -*comparison;
    }
  }
  big_uint_destroy(&left_magnitude);
  big_uint_destroy(&right_magnitude);
  big_uint_destroy(&left_denominator);
  big_uint_destroy(&right_denominator);
  big_uint_destroy(&left_product);
  big_uint_destroy(&right_product);
  return ok;
}

bool av2_dm_rational_rebase(Av2DmRational *values, uint32_t value_count,
                            const Av2DmRational *origin) {
  rational_begin_operation();
  if ((values == NULL && value_count != 0) || origin == NULL ||
      (uint64_t)value_count > SIZE_MAX / sizeof(*values)) {
    return false;
  }
  Av2DmRational fixed_origin;
  av2_dm_rational_init(&fixed_origin);
  Av2DmRational *rebased = NULL;
  bool ok = av2_dm_rational_copy(&fixed_origin, origin);
  if (ok && value_count != 0) {
    rebased = rational_allocate((size_t)value_count * sizeof(*rebased));
    ok = rebased != NULL;
    if (ok) {
      memset(rebased, 0, (size_t)value_count * sizeof(*rebased));
      for (uint32_t i = 0; i < value_count; ++i) {
        av2_dm_rational_init(&rebased[i]);
      }
    }
  }
  for (uint32_t i = 0; ok && i < value_count; ++i) {
    ok = av2_dm_rational_subtract(&values[i], &fixed_origin, &rebased[i]);
  }
  if (ok) {
    for (uint32_t i = 0; i < value_count; ++i) {
      av2_dm_rational_move(&values[i], &rebased[i]);
    }
  }
  if (rebased != NULL) {
    for (uint32_t i = 0; i < value_count; ++i) {
      av2_dm_rational_destroy(&rebased[i]);
    }
  }
  rational_deallocate(rebased);
  av2_dm_rational_destroy(&fixed_origin);
  return ok;
}

bool av2_dm_rational_is_zero(const Av2DmRational *value) {
  if (value == NULL) return false;
  const uint64_t *magnitude;
  const uint64_t *denominator;
  uint32_t magnitude_count;
  uint32_t denominator_count;
  return rational_component_view(value, false, &magnitude, &magnitude_count) &&
         rational_component_view(value, true, &denominator,
                                 &denominator_count) &&
         denominator_count != 0 && magnitude_count == 0;
}

static void buffer_reset(Av2DmBuffer *buffer) {
  av2_dm_rational_destroy(&buffer->presentation_time);
  av2_dm_rational_destroy(&buffer->decode_completion_time);
  av2_dm_rational_destroy(&buffer->disp_ct);
  memset(buffer, 0, sizeof(*buffer));
  buffer->display_index = -1;
  av2_dm_rational_init(&buffer->presentation_time);
  av2_dm_rational_init(&buffer->decode_completion_time);
  av2_dm_rational_init(&buffer->disp_ct);
}

static bool valid_active_buffer_index(const Av2DmBufferPool *pool,
                                      uint32_t buffer_index) {
  return pool != NULL && buffer_index < pool->pool_size;
}

void av2_dm_buffer_pool_init(Av2DmBufferPool *pool) {
  if (pool != NULL) memset(pool, 0, sizeof(*pool));
}

bool av2_dm_buffer_pool_initialize(Av2DmBufferPool *pool,
                                   uint32_t num_ref_frames) {
  if (pool == NULL || num_ref_frames == 0 ||
      num_ref_frames > AV2_DM_MAX_REF_FRAMES) {
    return false;
  }
  if (pool->initialized) av2_dm_buffer_pool_destroy(pool);
  memset(pool, 0, sizeof(*pool));
  pool->num_ref_frames = num_ref_frames;
  pool->pool_size = num_ref_frames + 2;
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) pool->vbi[i] = -1;
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    buffer_reset(&pool->buffers[i]);
  }
  pool->initialized = true;
  return true;
}

void av2_dm_buffer_pool_destroy(Av2DmBufferPool *pool) {
  if (pool == NULL || !pool->initialized) return;
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    av2_dm_rational_destroy(&pool->buffers[i].presentation_time);
    av2_dm_rational_destroy(&pool->buffers[i].decode_completion_time);
    av2_dm_rational_destroy(&pool->buffers[i].disp_ct);
  }
  memset(pool, 0, sizeof(*pool));
}

static bool buffer_copy(Av2DmBuffer *destination, const Av2DmBuffer *source) {
  Av2DmBuffer temporary = *source;
  av2_dm_rational_init(&temporary.presentation_time);
  av2_dm_rational_init(&temporary.decode_completion_time);
  av2_dm_rational_init(&temporary.disp_ct);
  if (!av2_dm_rational_copy(&temporary.presentation_time,
                            &source->presentation_time) ||
      !av2_dm_rational_copy(&temporary.decode_completion_time,
                            &source->decode_completion_time) ||
      !av2_dm_rational_copy(&temporary.disp_ct, &source->disp_ct)) {
    av2_dm_rational_destroy(&temporary.presentation_time);
    av2_dm_rational_destroy(&temporary.decode_completion_time);
    av2_dm_rational_destroy(&temporary.disp_ct);
    return false;
  }
  buffer_reset(destination);
  *destination = temporary;
  return true;
}

static bool buffer_pool_copy(Av2DmBufferPool *destination,
                             const Av2DmBufferPool *source) {
  Av2DmBufferPool temporary = { 0 };
  if (!source->initialized ||
      !av2_dm_buffer_pool_initialize(&temporary, source->num_ref_frames)) {
    return false;
  }
  memcpy(temporary.vbi, source->vbi, sizeof(temporary.vbi));
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    if (!buffer_copy(&temporary.buffers[i], &source->buffers[i])) {
      av2_dm_buffer_pool_destroy(&temporary);
      return false;
    }
  }
  av2_dm_buffer_pool_destroy(destination);
  *destination = temporary;
  return true;
}

int32_t av2_dm_buffer_pool_get_free_buffer(const Av2DmBufferPool *pool) {
  if (pool == NULL) return -1;
  for (uint32_t i = 0; i < pool->pool_size; ++i) {
    const Av2DmBuffer *const buffer = &pool->buffers[i];
    if (buffer->decoder_ref_count == 0 && buffer->player_ref_count == 0) {
      return (int32_t)i;
    }
  }
  return -1;
}

bool av2_dm_buffer_pool_release(Av2DmBufferPool *pool, uint32_t buffer_index) {
  if (!valid_active_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->decoder_ref_count != 0 || buffer->player_ref_count != 0) {
    return false;
  }
  buffer_reset(buffer);
  return true;
}

bool av2_dm_buffer_pool_add_decoder_ref(Av2DmBufferPool *pool,
                                        uint32_t buffer_index) {
  if (!valid_active_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->decoder_ref_count == UINT32_MAX) return false;
  ++buffer->decoder_ref_count;
  return true;
}

bool av2_dm_buffer_pool_remove_decoder_ref(Av2DmBufferPool *pool,
                                           uint32_t buffer_index) {
  if (!valid_active_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->decoder_ref_count == 0) return false;
  --buffer->decoder_ref_count;
  if (buffer->decoder_ref_count == 0 && buffer->player_ref_count == 0) {
    buffer_reset(buffer);
  }
  return true;
}

bool av2_dm_buffer_pool_add_player_ref(Av2DmBufferPool *pool,
                                       uint32_t buffer_index) {
  if (!valid_active_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->player_ref_count == UINT32_MAX) return false;
  ++buffer->player_ref_count;
  return true;
}

bool av2_dm_buffer_pool_remove_player_ref(Av2DmBufferPool *pool,
                                          uint32_t buffer_index) {
  if (!valid_active_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->player_ref_count == 0) return false;
  --buffer->player_ref_count;
  if (buffer->decoder_ref_count == 0 && buffer->player_ref_count == 0) {
    buffer_reset(buffer);
  }
  return true;
}

bool av2_dm_buffer_pool_set_vbi(Av2DmBufferPool *pool, uint32_t ref_index,
                                int32_t buffer_index) {
  if (pool == NULL || ref_index >= pool->num_ref_frames || buffer_index < -1 ||
      (buffer_index >= 0 &&
       !valid_active_buffer_index(pool, (uint32_t)buffer_index))) {
    return false;
  }
  const int32_t old_buffer_index = pool->vbi[ref_index];
  if (old_buffer_index == buffer_index) return true;
  if (buffer_index >= 0 &&
      pool->buffers[buffer_index].decoder_ref_count == UINT32_MAX) {
    return false;
  }
  if (old_buffer_index >= 0 && !av2_dm_buffer_pool_remove_decoder_ref(
                                   pool, (uint32_t)old_buffer_index)) {
    return false;
  }
  if (buffer_index >= 0 &&
      !av2_dm_buffer_pool_add_decoder_ref(pool, (uint32_t)buffer_index)) {
    return false;
  }
  pool->vbi[ref_index] = buffer_index;
  return true;
}

uint32_t av2_dm_buffer_pool_frames_in_use(const Av2DmBufferPool *pool) {
  if (pool == NULL) return 0;
  uint32_t frames_in_use = 0;
  for (uint32_t i = 0; i < pool->pool_size; ++i) {
    if (pool->buffers[i].decoder_ref_count != 0 ||
        pool->buffers[i].player_ref_count != 0) {
      ++frames_in_use;
    }
  }
  return frames_in_use;
}

typedef struct Av2DmDfgRecord {
  uint64_t event_index;
  uint64_t temporal_unit_index;
  uint64_t generation;
  uint64_t coded_bits;
  uint64_t decode_order;
  uint64_t rap_epoch;
  Av2DmLevelLimits limits;
  Av2DmRational buffer_size_at_last_arrival;
  Av2DmRational buffer_size_before_removal;
  Av2DmRational buffer_size_after_removal;
  uint32_t tier;
  Av2DmMode mode;
  Av2DmRational first_arrival;
  Av2DmRational last_arrival;
  Av2DmRational scheduled_removal;
  Av2DmRational removal;
  Av2DmRational decode_time;
  Av2DmRational decode_completion;
  bool random_access_point;
  bool parameters_updated;
  bool count_frame_header;
  bool decode_count_two;
  bool coded_as_closed_loop_key;
  bool first_dfg_of_cvs;
  bool still_picture;
  bool buffer_size_decreases_after_removal;
  bool smoothing_overflow_reported;
  uint64_t luma_samples;
  uint32_t num_tiles;
  uint64_t max_tile_area;
  uint64_t compressed_size;
  uint64_t frame_symbol_count;
} Av2DmDfgRecord;

typedef struct Av2DmTuRecord {
  uint64_t temporal_unit_index;
  uint64_t event_index;
  uint64_t cvs_number;
  uint64_t output_luma_samples;
  uint32_t output_frames;
  uint32_t frame_headers;
  bool header_complete;
  bool header_window_checked;
  bool header_rate_reported;
  bool tile_header_rate_reported;
  bool maximum_tile_area_finalized;
  bool output_time_valid;
  bool presentation_time_valid;
  bool prior_presentation_interval_checked;
  bool still_picture;
  Av2DmRational output_time;
  Av2DmRational presentation_time;
  uint64_t header_window_headers;
  uint64_t maximum_tile_area;
  Av2DmLevelLimits limits;
  uint32_t tier;
  uint32_t max_frame_width;
  uint32_t max_frame_height;
} Av2DmTuRecord;

typedef struct Av2DmLane {
  Av2DmBufferPool pool;
  Av2DmRational time;
  Av2DmRational initial_presentation_delay;
  bool initial_presentation_delay_known;
  int32_t current_buffer_index;
} Av2DmLane;

typedef struct Av2DmPendingOutputWitness {
  bool valid;
  uint64_t event_index;
  Av2DmRational threshold;
  Av2DmRational observed;
  Av2DmRational presentation_offset;
} Av2DmPendingOutputWitness;

typedef struct Av2DmRapPresentationAnchor {
  bool valid;
  uint64_t rap_epoch;
  Av2DmRational presentation_offset;
} Av2DmRapPresentationAnchor;

typedef struct Av2DmResolvedParameters {
  Av2DmLevelLimits limits;
  Av2DmRational decoder_buffer_delay;
  Av2DmRational encoder_buffer_delay;
  uint32_t decoder_buffer_delay_ticks;
  bool low_delay_mode;
  Av2DmRational dec_ct;
  Av2DmRational disp_ct;
} Av2DmResolvedParameters;

typedef struct Av2DmBufferSizeTransition {
  Av2DmRational time;
  Av2DmRational size;
  uint64_t dfg_number;
  bool after_removal;
} Av2DmBufferSizeTransition;

static void buffer_size_transition_destroy(
    Av2DmBufferSizeTransition *transition) {
  av2_dm_rational_destroy(&transition->time);
  av2_dm_rational_destroy(&transition->size);
  memset(transition, 0, sizeof(*transition));
}

static void buffer_size_transition_move(Av2DmBufferSizeTransition *destination,
                                        Av2DmBufferSizeTransition *source) {
  if (destination == source) return;
  buffer_size_transition_destroy(destination);
  *destination = *source;
  memset(source, 0, sizeof(*source));
}

struct Av2DecoderModel {
  Av2DmConfig config;
  Av2DmLevelLimits limits;
  Av2DmRational decoder_buffer_delay;
  Av2DmRational encoder_buffer_delay;
  uint32_t decoder_buffer_delay_ticks;
  bool low_delay_mode;
  Av2DmRational dec_ct;
  Av2DmRational disp_ct;
  Av2DmRational buffer_size_base;
  Av2DmRational pending_buffer_size;
  int pending_buffer_size_change;
  Av2DmBufferSizeTransition *buffer_size_transitions;
  uint32_t buffer_size_transition_count;
  uint32_t buffer_size_transition_capacity;
  Av2DmLane lane;
  Av2DmLane resource_lane;
  Av2DmResult result;
  Av2DmReportFn report;
  void *report_opaque;
  // Live smoothing/fullness records only. The adjacent parsing DFG and
  // generation output metadata have separate bounded homes below.
  Av2DmDfgRecord *dfgs;
  uint32_t dfg_count;
  uint32_t dfg_capacity;
  uint64_t dfg_number;
  uint64_t cvs_number;
  bool previous_dfg_valid;
  Av2DmDfgRecord previous_dfg;
  Av2DmTuRecord *tus;
  uint32_t tu_count;
  uint32_t tu_capacity;
  uint64_t output_tu_count;
  uint64_t frame_number;
  uint64_t shown_frame_number;
  uint64_t rap_epoch;
  bool most_recent_rap_removal_valid;
  Av2DmRational most_recent_rap_scheduled_removal;
  bool previous_output_order_valid;
  uint64_t previous_output_decode_order;
  bool previous_output_presentation_valid;
  Av2DmRational previous_output_presentation_offset;
  uint64_t previous_output_rap_epoch;
  bool last_presentation_offset_valid;
  Av2DmRational last_presentation_offset;
  bool last_presentation_valid;
  Av2DmRational last_presentation;
  Av2DmRapPresentationAnchor
      rap_presentation_anchors[AV2_DM_MAX_BUFFER_POOL_SIZE + 2];
  uint64_t last_output_temporal_unit;
  uint64_t latest_frame_event_index;
  uint64_t latest_header_check_event_index;
  bool last_frame_parsing_time_valid;
  Av2DmRational last_frame_parsing_time;
  bool last_display_duration_valid;
  Av2DmRational last_display_duration;
  bool last_output_tu_valid;
  uint64_t last_output_tu;
  bool latest_timed_tu_valid;
  Av2DmRational latest_timed_tu_output_time;
  bool coded_tu_valid;
  uint64_t coded_tu;
  bool retired_header_summary_valid;
  bool retired_header_summary_reported;
  uint64_t retired_max_frame_headers;
  uint64_t retired_header_event_index;
  uint64_t retired_header_limit;
  bool retired_unresolved_tu;
  Av2DmPendingOutputWitness pending_display_late;
  Av2DmPendingOutputWitness pending_decode_deadline;
  uint64_t maximum_tile_area;
  bool tile_cvs_finalized;
  bool any_decode_count_two_requires_reserved_buffer;
  bool max_reference_frames_checked;
  bool max_reference_frames_reserved;
  bool max_reference_frames_violated;
  bool processing_stopped;
  uint64_t model_events;
  bool violation_seen[AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE + 1];
  Av2DmStorageStats storage;
};

void av2_dm_level_limits_init(Av2DmLevelLimits *limits) {
  if (limits == NULL) return;
  memset(limits, 0, sizeof(*limits));
  av2_dm_rational_init(&limits->bit_rate);
  av2_dm_rational_init(&limits->buffer_size);
}

void av2_dm_level_limits_destroy(Av2DmLevelLimits *limits) {
  if (limits == NULL) return;
  av2_dm_rational_destroy(&limits->bit_rate);
  av2_dm_rational_destroy(&limits->buffer_size);
  memset(limits, 0, sizeof(*limits));
}

bool av2_dm_level_limits_copy(Av2DmLevelLimits *destination,
                              const Av2DmLevelLimits *source) {
  Av2DmLevelLimits temporary = *source;
  av2_dm_rational_init(&temporary.bit_rate);
  av2_dm_rational_init(&temporary.buffer_size);
  if (!av2_dm_rational_copy(&temporary.bit_rate, &source->bit_rate) ||
      !av2_dm_rational_copy(&temporary.buffer_size, &source->buffer_size)) {
    av2_dm_level_limits_destroy(&temporary);
    return false;
  }
  av2_dm_level_limits_destroy(destination);
  *destination = temporary;
  return true;
}

void av2_dm_config_destroy(Av2DmConfig *config) {
  if (config == NULL) return;
  av2_dm_level_limits_destroy(&config->level_limits);
  memset(config, 0, sizeof(*config));
}

void av2_dm_config_init(Av2DmConfig *config) {
  if (config == NULL) return;
  memset(config, 0, sizeof(*config));
  av2_dm_level_limits_init(&config->level_limits);
}

bool av2_dm_config_copy(Av2DmConfig *destination, const Av2DmConfig *source) {
  if (destination == NULL || source == NULL) return false;
  Av2DmConfig temporary = *source;
  memset(&temporary.level_limits.bit_rate, 0,
         sizeof(temporary.level_limits.bit_rate));
  memset(&temporary.level_limits.buffer_size, 0,
         sizeof(temporary.level_limits.buffer_size));
  av2_dm_rational_init(&temporary.level_limits.bit_rate);
  av2_dm_rational_init(&temporary.level_limits.buffer_size);
  if (source->level_limits_present &&
      !av2_dm_level_limits_copy(&temporary.level_limits,
                                &source->level_limits)) {
    av2_dm_config_destroy(&temporary);
    return false;
  }
  av2_dm_config_destroy(destination);
  *destination = temporary;
  return true;
}

static void dfg_record_destroy(Av2DmDfgRecord *dfg) {
  av2_dm_level_limits_destroy(&dfg->limits);
  av2_dm_rational_destroy(&dfg->buffer_size_at_last_arrival);
  av2_dm_rational_destroy(&dfg->buffer_size_before_removal);
  av2_dm_rational_destroy(&dfg->buffer_size_after_removal);
  av2_dm_rational_destroy(&dfg->first_arrival);
  av2_dm_rational_destroy(&dfg->last_arrival);
  av2_dm_rational_destroy(&dfg->scheduled_removal);
  av2_dm_rational_destroy(&dfg->removal);
  av2_dm_rational_destroy(&dfg->decode_time);
  av2_dm_rational_destroy(&dfg->decode_completion);
  memset(dfg, 0, sizeof(*dfg));
}

static void dfg_record_init(Av2DmDfgRecord *dfg) {
  memset(dfg, 0, sizeof(*dfg));
  av2_dm_rational_init(&dfg->limits.bit_rate);
  av2_dm_rational_init(&dfg->limits.buffer_size);
  av2_dm_rational_init(&dfg->buffer_size_at_last_arrival);
  av2_dm_rational_init(&dfg->buffer_size_before_removal);
  av2_dm_rational_init(&dfg->buffer_size_after_removal);
  av2_dm_rational_init(&dfg->first_arrival);
  av2_dm_rational_init(&dfg->last_arrival);
  av2_dm_rational_init(&dfg->scheduled_removal);
  av2_dm_rational_init(&dfg->removal);
  av2_dm_rational_init(&dfg->decode_time);
  av2_dm_rational_init(&dfg->decode_completion);
}

static bool dfg_record_copy(Av2DmDfgRecord *destination,
                            const Av2DmDfgRecord *source) {
  Av2DmDfgRecord temporary = *source;
  memset(&temporary.limits.bit_rate, 0, sizeof(temporary.limits.bit_rate));
  memset(&temporary.limits.buffer_size, 0,
         sizeof(temporary.limits.buffer_size));
  memset(&temporary.buffer_size_at_last_arrival, 0,
         sizeof(temporary.buffer_size_at_last_arrival));
  memset(&temporary.buffer_size_before_removal, 0,
         sizeof(temporary.buffer_size_before_removal));
  memset(&temporary.buffer_size_after_removal, 0,
         sizeof(temporary.buffer_size_after_removal));
  memset(&temporary.first_arrival, 0, sizeof(temporary.first_arrival));
  memset(&temporary.last_arrival, 0, sizeof(temporary.last_arrival));
  memset(&temporary.scheduled_removal, 0, sizeof(temporary.scheduled_removal));
  memset(&temporary.removal, 0, sizeof(temporary.removal));
  memset(&temporary.decode_time, 0, sizeof(temporary.decode_time));
  memset(&temporary.decode_completion, 0, sizeof(temporary.decode_completion));
  const bool copied =
      av2_dm_level_limits_copy(&temporary.limits, &source->limits) &&
      av2_dm_rational_copy(&temporary.buffer_size_at_last_arrival,
                           &source->buffer_size_at_last_arrival) &&
      av2_dm_rational_copy(&temporary.buffer_size_before_removal,
                           &source->buffer_size_before_removal) &&
      av2_dm_rational_copy(&temporary.buffer_size_after_removal,
                           &source->buffer_size_after_removal) &&
      av2_dm_rational_copy(&temporary.first_arrival, &source->first_arrival) &&
      av2_dm_rational_copy(&temporary.last_arrival, &source->last_arrival) &&
      av2_dm_rational_copy(&temporary.scheduled_removal,
                           &source->scheduled_removal) &&
      av2_dm_rational_copy(&temporary.removal, &source->removal) &&
      av2_dm_rational_copy(&temporary.decode_time, &source->decode_time) &&
      av2_dm_rational_copy(&temporary.decode_completion,
                           &source->decode_completion);
  if (!copied) {
    dfg_record_destroy(&temporary);
    return false;
  }
  dfg_record_destroy(destination);
  *destination = temporary;
  return true;
}

static void tu_record_destroy(Av2DmTuRecord *tu) {
  av2_dm_rational_destroy(&tu->output_time);
  av2_dm_rational_destroy(&tu->presentation_time);
  av2_dm_level_limits_destroy(&tu->limits);
  memset(tu, 0, sizeof(*tu));
}

static void tu_record_init(Av2DmTuRecord *tu) {
  memset(tu, 0, sizeof(*tu));
  av2_dm_rational_init(&tu->output_time);
  av2_dm_rational_init(&tu->presentation_time);
  av2_dm_rational_init(&tu->limits.bit_rate);
  av2_dm_rational_init(&tu->limits.buffer_size);
}

static bool tu_record_copy(Av2DmTuRecord *destination,
                           const Av2DmTuRecord *source) {
  Av2DmTuRecord temporary = *source;
  av2_dm_rational_init(&temporary.output_time);
  av2_dm_rational_init(&temporary.presentation_time);
  av2_dm_level_limits_init(&temporary.limits);
  if (!av2_dm_rational_copy(&temporary.output_time, &source->output_time) ||
      !av2_dm_rational_copy(&temporary.presentation_time,
                            &source->presentation_time) ||
      !av2_dm_level_limits_copy(&temporary.limits, &source->limits)) {
    tu_record_destroy(&temporary);
    return false;
  }
  tu_record_destroy(destination);
  *destination = temporary;
  return true;
}

static void tu_record_move(Av2DmTuRecord *destination, Av2DmTuRecord *source) {
  if (destination == source) return;
  tu_record_destroy(destination);
  *destination = *source;
  memset(source, 0, sizeof(*source));
}

static void lane_destroy(Av2DmLane *lane) {
  av2_dm_buffer_pool_destroy(&lane->pool);
  av2_dm_rational_destroy(&lane->time);
  av2_dm_rational_destroy(&lane->initial_presentation_delay);
  memset(lane, 0, sizeof(*lane));
}

static bool lane_copy(Av2DmLane *destination, const Av2DmLane *source) {
  Av2DmLane temporary = { 0 };
  temporary.current_buffer_index = source->current_buffer_index;
  temporary.initial_presentation_delay_known =
      source->initial_presentation_delay_known;
  if (!buffer_pool_copy(&temporary.pool, &source->pool) ||
      !av2_dm_rational_copy(&temporary.time, &source->time) ||
      !av2_dm_rational_copy(&temporary.initial_presentation_delay,
                            &source->initial_presentation_delay)) {
    lane_destroy(&temporary);
    return false;
  }
  lane_destroy(destination);
  *destination = temporary;
  return true;
}

static bool invalidate_lane_reference_buffers(Av2DmLane *lane,
                                              uint32_t ref_valid_mask,
                                              bool closed_loop_key);
static void check_smoothing_buffer_overflow(Av2DecoderModel *model,
                                            const Av2DmRational *frontier,
                                            uint64_t proving_event_index);
static void retire_closed_smoothing_records(Av2DecoderModel *model,
                                            const Av2DmRational *frontier);
static void check_max_reference_frames(Av2DecoderModel *model,
                                       uint64_t event_index);
static void check_header_rate_windows(Av2DecoderModel *model,
                                      bool require_complete,
                                      uint64_t proving_event_index);
static void update_storage_stats(Av2DecoderModel *model);
static void check_retired_tile_header_summary(Av2DecoderModel *model,
                                              uint64_t proving_event_index);
static void finalize_tile_cvs(Av2DecoderModel *model,
                              uint64_t proving_event_index);
static void retire_unresolvable_tus(Av2DecoderModel *model);
static void restart_tu_history(Av2DecoderModel *model,
                               uint64_t temporal_unit_index);
static bool update_latest_timed_tu(Av2DecoderModel *model,
                                   const Av2DmRational *output_time);
static bool set_lane_initial_presentation_delay(Av2DecoderModel *model,
                                                Av2DmLane *lane,
                                                bool primary_lane,
                                                bool end_of_bitstream,
                                                uint64_t proving_event_index);

static bool rational_zero(Av2DmRational *value) {
  return av2_dm_rational_make(0, 1, value);
}

static bool rational_from_product(uint64_t left, uint64_t right,
                                  Av2DmRational *value) {
  Av2DmRational factor = { 0 };
  const bool made = av2_dm_rational_make(left, 1, &factor) &&
                    av2_dm_rational_multiply_u64(&factor, right, value);
  av2_dm_rational_destroy(&factor);
  return made;
}

static bool rational_multiply(const Av2DmRational *left,
                              const Av2DmRational *right,
                              Av2DmRational *result) {
  rational_begin_operation();
  if (left == NULL || right == NULL || result == NULL) return false;
  Av2DmBigUInt left_magnitude = { 0 }, right_magnitude = { 0 };
  Av2DmBigUInt left_denominator = { 0 }, right_denominator = { 0 };
  Av2DmBigUInt magnitude = { 0 }, denominator = { 0 };
  Av2DmRational temporary = { 0 };
  bool ok =
      big_uint_from_rational(left, false, &left_magnitude) &&
      big_uint_from_rational(right, false, &right_magnitude) &&
      big_uint_from_rational(left, true, &left_denominator) &&
      big_uint_from_rational(right, true, &right_denominator) &&
      !big_uint_is_zero(&left_denominator) &&
      !big_uint_is_zero(&right_denominator) &&
      big_uint_multiply(&left_magnitude, &right_magnitude, &magnitude) &&
      big_uint_multiply(&left_denominator, &right_denominator, &denominator) &&
      rational_store(&temporary, &magnitude, &denominator,
                     left->negative != right->negative) &&
      rational_normalize(&temporary);
  if (ok) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&left_magnitude);
  big_uint_destroy(&right_magnitude);
  big_uint_destroy(&left_denominator);
  big_uint_destroy(&right_denominator);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  return ok;
}

static bool rational_max(const Av2DmRational *left, const Av2DmRational *right,
                         Av2DmRational *result) {
  int comparison;
  if (!av2_dm_rational_compare(left, right, &comparison)) return false;
  return av2_dm_rational_copy(result, comparison >= 0 ? left : right);
}

static bool rational_reciprocal(const Av2DmRational *value,
                                Av2DmRational *result) {
  rational_begin_operation();
  Av2DmBigUInt magnitude = { 0 };
  Av2DmBigUInt denominator = { 0 };
  Av2DmRational temporary = { 0 };
  const bool made =
      big_uint_from_rational(value, false, &magnitude) &&
      !big_uint_is_zero(&magnitude) &&
      big_uint_from_rational(value, true, &denominator) &&
      rational_store(&temporary, &denominator, &magnitude, value->negative);
  if (made) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  return made;
}

static bool rational_to_u64(const Av2DmRational *value, uint64_t *result) {
  rational_begin_operation();
  Av2DmBigUInt magnitude = { 0 };
  Av2DmBigUInt denominator = { 0 };
  const bool converted = !value->negative &&
                         big_uint_from_rational(value, false, &magnitude) &&
                         big_uint_from_rational(value, true, &denominator) &&
                         denominator.count == 1 && denominator.limbs[0] == 1 &&
                         magnitude.count <= 1;
  if (converted) *result = magnitude.count == 0 ? 0 : magnitude.limbs[0];
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  return converted;
}

static bool rational_less(const Av2DmRational *left, const Av2DmRational *right,
                          bool *is_less) {
  int comparison;
  if (!av2_dm_rational_compare(left, right, &comparison)) return false;
  *is_less = comparison < 0;
  return true;
}

static bool rational_greater(const Av2DmRational *left,
                             const Av2DmRational *right, bool *is_greater) {
  int comparison;
  if (!av2_dm_rational_compare(left, right, &comparison)) return false;
  *is_greater = comparison > 0;
  return true;
}

static bool grow_array(void **array, uint32_t *capacity, uint32_t count,
                       size_t element_size) {
  internal_allocation_failed = false;
  if (count < *capacity) return true;
  const uint32_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
  if (new_capacity < *capacity || element_size > SIZE_MAX / new_capacity) {
    return false;
  }
  void *const replacement = internal_calloc(new_capacity, element_size);
  if (replacement == NULL) return false;
  if (*array != NULL) {
    memcpy(replacement, *array, (size_t)count * element_size);
    avm_free(*array);
  }
  *array = replacement;
  *capacity = new_capacity;
  return true;
}

bool av2_dm_get_level_limits(uint32_t level_idx, uint32_t tier,
                             uint32_t profile, Av2DmLevelLimits *limits) {
  if (limits == NULL || level_idx >= 22 || tier > 1 || profile > 5) {
    return false;
  }
  const AV2LevelSpec *const row = &av2_level_defs[level_idx];
  uint32_t kbps;
  uint32_t compression;
  if (!av2_get_level_base_bitrate_kbps((int)level_idx, (int)tier, &kbps) ||
      !av2_get_level_compression_basis((int)level_idx, (int)tier,
                                       &compression)) {
    return false;
  }

  AV2ProfileLevelFactors factors;
  if (!av2_get_profile_level_factors((int)profile, &factors)) return false;

  Av2DmLevelLimits computed;
  av2_dm_level_limits_init(&computed);
  computed.max_picture_size = (uint64_t)row->max_picture_size;
  computed.max_horizontal_size = (uint32_t)row->max_h_size;
  computed.max_vertical_size = (uint32_t)row->max_v_size;
  computed.max_display_rate = (uint64_t)row->max_display_rate;
  computed.max_decode_rate = (uint64_t)row->max_decode_rate;
  computed.max_header_rate = (uint32_t)row->max_header_rate;
  computed.max_tiles = (uint32_t)row->max_tiles;
  computed.max_tile_columns = (uint32_t)row->max_tile_cols;
  computed.max_tile_width =
      (uint64_t)av2_tile_width_scaling_factor[tier][level_idx] *
      MAX_TILE_WIDTH / 4;
  computed.max_tile_area =
      (uint64_t)av2_tile_area_scaling_factor[tier][level_idx] * MAX_TILE_AREA /
      4;
  computed.max_tile_size_header_rate_product =
      (uint64_t)av2_tile_area_scaling_factor[tier][level_idx] *
      MAX_TILE_SIZE_HEADER_RATE_PRODUCT / 4;
  computed.picture_size_profile_factor = factors.picture_size_profile_factor;
  computed.min_compression_basis = compression;

  Av2DmRational base_rate = { 0 };
  Av2DmRational profile_factor = { 0 };
  if (!rational_from_product(kbps, 1000, &base_rate) ||
      !av2_dm_rational_make(factors.bitrate_factor_numerator,
                            factors.bitrate_factor_denominator,
                            &profile_factor) ||
      !rational_multiply(&base_rate, &profile_factor, &computed.bit_rate) ||
      !av2_dm_rational_copy(&computed.buffer_size, &computed.bit_rate)) {
    av2_dm_rational_destroy(&base_rate);
    av2_dm_rational_destroy(&profile_factor);
    av2_dm_level_limits_destroy(&computed);
    return false;
  }
  // Annex A defines MaxBufferSize as one second of MaxBitrate.
  av2_dm_rational_destroy(&base_rate);
  av2_dm_rational_destroy(&profile_factor);
  av2_dm_level_limits_destroy(limits);
  *limits = computed;
  return true;
}

static bool scaled_integer(uint64_t value, uint32_t scale_numerator,
                           uint32_t scale_denominator, uint64_t *scaled) {
  Av2DmRational rational = { 0 };
  if (!av2_dm_rational_make(value, 1, &rational) ||
      !av2_dm_rational_multiply_u64(&rational, scale_denominator, &rational) ||
      !av2_dm_rational_divide_u64(&rational, scale_numerator, &rational) ||
      !rational_to_u64(&rational, scaled)) {
    av2_dm_rational_destroy(&rational);
    return false;
  }
  av2_dm_rational_destroy(&rational);
  return true;
}

bool av2_dm_apply_multistream_limits(uint32_t level_idx, uint32_t tier,
                                     uint32_t profile, uint32_t scale_numerator,
                                     uint32_t scale_denominator,
                                     Av2DmLevelLimits *limits) {
  // Annex A does not define substream limits below Level 4.0.
  if (limits == NULL || scale_denominator == 0 || level_idx < 4 ||
      level_idx >= 22) {
    return false;
  }
  AV2SubstreamLevelSpec row;
  if (!av2_get_substream_level_spec((int)level_idx, scale_numerator,
                                    scale_denominator, &row)) {
    return false;
  }
  Av2DmLevelLimits multistream = { 0 };
  if (!av2_dm_get_level_limits(level_idx, tier, profile, &multistream)) {
    return false;
  }
  Av2DmLevelLimits updated = { 0 };
  if (!av2_dm_level_limits_copy(&updated, limits)) {
    av2_dm_level_limits_destroy(&multistream);
    return false;
  }
  uint64_t scaled_display;
  uint64_t scaled_decode;
  if (!scaled_integer(multistream.max_display_rate, scale_numerator,
                      scale_denominator, &scaled_display) ||
      !scaled_integer(multistream.max_decode_rate, scale_numerator,
                      scale_denominator, &scaled_decode) ||
      !av2_dm_rational_multiply_u64(&multistream.bit_rate, scale_denominator,
                                    &multistream.bit_rate) ||
      !av2_dm_rational_divide_u64(&multistream.bit_rate, scale_numerator,
                                  &multistream.bit_rate) ||
      !av2_dm_rational_multiply_u64(&multistream.buffer_size, scale_denominator,
                                    &multistream.buffer_size) ||
      !av2_dm_rational_divide_u64(&multistream.buffer_size, scale_numerator,
                                  &multistream.buffer_size)) {
    av2_dm_level_limits_destroy(&multistream);
    av2_dm_level_limits_destroy(&updated);
    return false;
  }
  multistream.max_picture_size =
      (uint64_t)row.max_h_size_x * (uint32_t)row.max_v_size_x;
  multistream.max_horizontal_size = (uint32_t)row.max_h_size_x;
  multistream.max_vertical_size = (uint32_t)row.max_v_size_x;
  multistream.max_display_rate = scaled_display;
  multistream.max_decode_rate = scaled_decode;
  multistream.max_header_rate = 132;
  multistream.max_tiles = (uint32_t)((uint64_t)multistream.max_tiles *
                                     scale_denominator / scale_numerator);
  multistream.max_tile_columns = (uint32_t)row.max_tile_cols_x;

#define MIN_LIMIT(member)                    \
  do {                                       \
    if (multistream.member < updated.member) \
      updated.member = multistream.member;   \
  } while (0)
  MIN_LIMIT(max_picture_size);
  MIN_LIMIT(max_horizontal_size);
  MIN_LIMIT(max_vertical_size);
  MIN_LIMIT(max_display_rate);
  MIN_LIMIT(max_decode_rate);
  MIN_LIMIT(max_header_rate);
  MIN_LIMIT(max_tiles);
  MIN_LIMIT(max_tile_columns);
#undef MIN_LIMIT
  int comparison;
  if (!av2_dm_rational_compare(&multistream.bit_rate, &updated.bit_rate,
                               &comparison)) {
    av2_dm_level_limits_destroy(&multistream);
    av2_dm_level_limits_destroy(&updated);
    return false;
  }
  if (comparison < 0 &&
      !av2_dm_rational_copy(&updated.bit_rate, &multistream.bit_rate)) {
    av2_dm_level_limits_destroy(&multistream);
    av2_dm_level_limits_destroy(&updated);
    return false;
  }
  if (!av2_dm_rational_compare(&multistream.buffer_size, &updated.buffer_size,
                               &comparison)) {
    av2_dm_level_limits_destroy(&multistream);
    av2_dm_level_limits_destroy(&updated);
    return false;
  }
  if (comparison < 0 &&
      !av2_dm_rational_copy(&updated.buffer_size, &multistream.buffer_size)) {
    av2_dm_level_limits_destroy(&multistream);
    av2_dm_level_limits_destroy(&updated);
    return false;
  }
  if (multistream.min_compression_basis > updated.min_compression_basis) {
    updated.min_compression_basis = multistream.min_compression_basis;
  }
  av2_dm_level_limits_destroy(&multistream);
  av2_dm_level_limits_destroy(limits);
  *limits = updated;
  return true;
}

static void update_result_status(Av2DecoderModel *model) {
  if (model->result.applicability == AV2_DM_NOT_APPLICABLE) {
    model->result.status = AV2_DM_RESULT_NOT_APPLICABLE;
  } else if (model->result.violations != 0) {
    model->result.status = AV2_DM_RESULT_NON_CONFORMANT;
  } else if (model->result.allocation_failed ||
             model->result.arithmetic_failed ||
             model->result.missing_required_input ||
             model->result.applicability == AV2_DM_MISSING_REQUIRED_INPUT) {
    model->result.status = AV2_DM_RESULT_INDETERMINATE;
  } else {
    model->result.status = AV2_DM_RESULT_CONFORMANT;
  }
}

static void arithmetic_failure(Av2DecoderModel *model) {
  if (av2_dm_last_failure_was_allocation()) {
    model->result.allocation_failed = true;
  } else {
    model->result.arithmetic_failed = true;
  }
  model->processing_stopped = true;
  update_result_status(model);
}

static void allocation_failure(Av2DecoderModel *model) {
  model->result.allocation_failed = true;
  model->processing_stopped = true;
  update_result_status(model);
}

void av2_decoder_model_fail_arithmetic_for_testing(Av2DecoderModel *model) {
  if (model != NULL) arithmetic_failure(model);
}

void av2_decoder_model_set_defer_nonterminal_checks_for_testing(
    Av2DecoderModel *model, bool defer) {
  if (model != NULL && !model->result.finished && !model->processing_stopped) {
    model->config.defer_nonterminal_checks_for_testing = defer;
  }
}

static bool increment_model_u64(Av2DecoderModel *model, uint64_t *value) {
  if (*value == UINT64_MAX) {
    arithmetic_failure(model);
    return false;
  }
  ++*value;
  return true;
}

static bool increment_output_count(Av2DecoderModel *model) {
  if (model->shown_frame_number == UINT64_MAX ||
      model->result.output_frames == UINT64_MAX) {
    arithmetic_failure(model);
    return false;
  }
  ++model->shown_frame_number;
  ++model->result.output_frames;
  return true;
}

static void missing_input(Av2DecoderModel *model) {
  model->result.missing_required_input = true;
  model->processing_stopped = true;
  if (model->result.applicability == AV2_DM_APPLICABLE) {
    model->result.applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  update_result_status(model);
}

static void incomplete_verification(Av2DecoderModel *model) {
  model->result.missing_required_input = true;
  if (model->result.applicability == AV2_DM_APPLICABLE) {
    model->result.applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  update_result_status(model);
}

static bool violation_seen(const Av2DecoderModel *model,
                           Av2DmViolationCode code) {
  return code <= AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE &&
         model->violation_seen[code];
}

static void violation_detail_destroy(Av2DmViolationDetail *detail) {
  switch (detail->kind) {
    case AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY:
      av2_dm_rational_destroy(
          &detail->value.delay_consistency.ceil_time_delta_ticks);
      break;
    case AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME:
      av2_dm_rational_destroy(
          &detail->value.minimum_decode_time.frame_decode_time);
      av2_dm_rational_destroy(
          &detail->value.minimum_decode_time.one_header_time);
      break;
    case AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL:
      av2_dm_rational_destroy(&detail->value.frame_interval);
      break;
    default: break;
  }
  memset(detail, 0, sizeof(*detail));
}

static bool violation_detail_copy(Av2DmViolationDetail *destination,
                                  const Av2DmViolationDetail *source) {
  Av2DmViolationDetail temporary = *source;
  bool copied = true;
  switch (source->kind) {
    case AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY:
      memset(&temporary.value.delay_consistency.ceil_time_delta_ticks, 0,
             sizeof(temporary.value.delay_consistency.ceil_time_delta_ticks));
      av2_dm_rational_init(
          &temporary.value.delay_consistency.ceil_time_delta_ticks);
      copied = !source->value.delay_consistency.ceil_time_delta_present ||
               av2_dm_rational_copy(
                   &temporary.value.delay_consistency.ceil_time_delta_ticks,
                   &source->value.delay_consistency.ceil_time_delta_ticks);
      break;
    case AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME:
      memset(&temporary.value.minimum_decode_time.frame_decode_time, 0,
             sizeof(temporary.value.minimum_decode_time.frame_decode_time));
      memset(&temporary.value.minimum_decode_time.one_header_time, 0,
             sizeof(temporary.value.minimum_decode_time.one_header_time));
      copied = av2_dm_rational_copy(
                   &temporary.value.minimum_decode_time.frame_decode_time,
                   &source->value.minimum_decode_time.frame_decode_time) &&
               av2_dm_rational_copy(
                   &temporary.value.minimum_decode_time.one_header_time,
                   &source->value.minimum_decode_time.one_header_time);
      break;
    case AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL:
      memset(&temporary.value.frame_interval, 0,
             sizeof(temporary.value.frame_interval));
      copied = av2_dm_rational_copy(&temporary.value.frame_interval,
                                    &source->value.frame_interval);
      break;
    default: break;
  }
  if (!copied) {
    violation_detail_destroy(&temporary);
    return false;
  }
  violation_detail_destroy(destination);
  *destination = temporary;
  return true;
}

void av2_dm_violation_init(Av2DmViolation *violation) {
  if (violation == NULL) return;
  memset(violation, 0, sizeof(*violation));
  av2_dm_rational_init(&violation->observed);
  av2_dm_rational_init(&violation->limit);
}

void av2_dm_violation_destroy(Av2DmViolation *violation) {
  if (violation == NULL) return;
  av2_dm_rational_destroy(&violation->observed);
  av2_dm_rational_destroy(&violation->limit);
  violation_detail_destroy(&violation->detail);
  memset(violation, 0, sizeof(*violation));
}

bool av2_dm_violation_copy(Av2DmViolation *destination,
                           const Av2DmViolation *source) {
  if (destination == NULL || source == NULL) return false;
  if (destination == source) return true;
  Av2DmViolation temporary = *source;
  memset(&temporary.observed, 0, sizeof(temporary.observed));
  memset(&temporary.limit, 0, sizeof(temporary.limit));
  memset(&temporary.detail, 0, sizeof(temporary.detail));
  if (!av2_dm_rational_copy(&temporary.observed, &source->observed) ||
      !av2_dm_rational_copy(&temporary.limit, &source->limit) ||
      !violation_detail_copy(&temporary.detail, &source->detail)) {
    av2_dm_violation_destroy(&temporary);
    return false;
  }
  av2_dm_violation_destroy(destination);
  *destination = temporary;
  return true;
}

static void report_violation_for_affected(
    Av2DecoderModel *model, Av2DmViolationCode code, uint64_t event_index,
    Av2DmViolationAffectedKind affected_kind, uint64_t affected_index,
    const Av2DmRational *observed, const Av2DmRational *limit,
    const Av2DmViolationDetail *detail) {
  if (model->processing_stopped) return;
  if (code > AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) {
    arithmetic_failure(model);
    return;
  }
  model->violation_seen[code] = true;
  if (model->result.violations != UINT64_MAX) {
    ++model->result.violations;
  }
  model->result.status = AV2_DM_RESULT_NON_CONFORMANT;
  if (code == AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) {
    // Once this code is proven, no retained header-window witness can change
    // the CVS or bitstream verdict. Direct later occurrences may still be
    // reported without retaining the old windows.
    model->retired_header_summary_valid = false;
  }
  if (model->report != NULL) {
    Av2DmViolation violation;
    av2_dm_violation_init(&violation);
    violation.code = code;
    violation.scope = model->config.scope;
    violation.event_index = event_index;
    violation.affected_kind = affected_kind;
    violation.affected_index = affected_index;
    violation.observed_present = observed != NULL;
    violation.limit_present = limit != NULL;
    if ((observed == NULL ||
         av2_dm_rational_copy(&violation.observed, observed)) &&
        (limit == NULL || av2_dm_rational_copy(&violation.limit, limit)) &&
        (detail == NULL || violation_detail_copy(&violation.detail, detail))) {
      model->report(model->report_opaque, &violation);
    } else {
      arithmetic_failure(model);
    }
    av2_dm_violation_destroy(&violation);
  }
  if (model->config.stop_after_first_violation) {
    model->processing_stopped = true;
  }
}

static void report_violation(Av2DecoderModel *model, Av2DmViolationCode code,
                             uint64_t event_index,
                             const Av2DmRational *observed,
                             const Av2DmRational *limit) {
  report_violation_for_affected(model, code, event_index,
                                AV2_DM_VIOLATION_AFFECTED_EVENT, event_index,
                                observed, limit, NULL);
}

static bool lane_initialize(Av2DmLane *lane, uint32_t num_ref_frames) {
  lane_destroy(lane);
  lane->current_buffer_index = -1;
  return av2_dm_buffer_pool_initialize(&lane->pool, num_ref_frames) &&
         rational_zero(&lane->time) &&
         rational_zero(&lane->initial_presentation_delay);
}

static bool seed_ras_buffers(Av2DecoderModel *model, Av2DmLane *lane) {
  for (uint32_t i = 0; i < model->config.ras_seed_count; ++i) {
    const Av2DmRasSeed *const seed = &model->config.ras_seeds[i];
    if (seed->ref_index >= lane->pool.num_ref_frames) return false;
    int32_t buffer_index = -1;
    for (uint32_t j = 0; j < lane->pool.pool_size; ++j) {
      if (lane->pool.buffers[j].generation_valid &&
          lane->pool.buffers[j].generation == seed->generation) {
        buffer_index = (int32_t)j;
        break;
      }
    }
    if (buffer_index == -1) {
      buffer_index = av2_dm_buffer_pool_get_free_buffer(&lane->pool);
      if (buffer_index == -1) return false;
      lane->pool.buffers[buffer_index].generation_valid = true;
      lane->pool.buffers[buffer_index].generation = seed->generation;
    }
    lane->pool.buffers[buffer_index].equal_picture_interval =
        model->config.equal_picture_interval;
    lane->pool.buffers[buffer_index].ticks_per_picture =
        model->config.ticks_per_picture;
    if (!av2_dm_rational_copy(&lane->pool.buffers[buffer_index].disp_ct,
                              &model->disp_ct)) {
      return false;
    }
    if (!av2_dm_buffer_pool_set_vbi(&lane->pool, seed->ref_index,
                                    buffer_index)) {
      return false;
    }
  }
  return true;
}

static bool rational_representation_valid(const Av2DmRational *value) {
  const uint64_t *magnitude;
  const uint64_t *denominator;
  uint32_t magnitude_count;
  uint32_t denominator_count;
  return av2_dm_rational_get_component(value, false, &magnitude,
                                       &magnitude_count) &&
         av2_dm_rational_get_component(value, true, &denominator,
                                       &denominator_count) &&
         denominator_count != 0 && denominator[denominator_count - 1] != 0;
}

static bool parameter_inputs_valid(const Av2DmConfig *config) {
  if (config->num_ref_frames == 0 ||
      config->num_ref_frames > AV2_DM_MAX_REF_FRAMES ||
      (config->mode != AV2_DM_RESOURCE_AVAILABILITY_MODE &&
       config->mode != AV2_DM_DECODING_SCHEDULE_MODE) ||
      !config->timing_info_present || config->time_scale == 0 ||
      config->num_units_in_display_tick == 0 ||
      (config->mode == AV2_DM_DECODING_SCHEDULE_MODE &&
       config->num_units_in_decoding_tick == 0) ||
      (config->equal_picture_interval && config->ticks_per_picture == 0) ||
      config->initial_display_delay == 0) {
    return false;
  }
  if (config->mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    if ((!config->scope.whole_xlayer &&
         config->operating_point_parameters_present) ||
        config->sequence_parameters_present) {
      // The selected delay source is present.
    } else {
      return false;
    }
  } else if (!config->equal_picture_interval) {
    return false;
  }
  if (config->level_limits_present) {
    return rational_representation_valid(&config->level_limits.bit_rate) &&
           rational_representation_valid(&config->level_limits.buffer_size) &&
           !av2_dm_rational_is_zero(&config->level_limits.bit_rate) &&
           config->level_limits.max_decode_rate != 0 &&
           config->level_limits.max_display_rate != 0 &&
           config->level_limits.max_header_rate != 0 &&
           config->level_limits.picture_size_profile_factor != 0 &&
           config->level_limits.min_compression_basis != 0;
  }
  uint32_t kbps;
  uint32_t compression;
  AV2ProfileLevelFactors factors;
  return av2_get_level_base_bitrate_kbps((int)config->level_idx,
                                         (int)config->tier, &kbps) &&
         av2_get_level_compression_basis((int)config->level_idx,
                                         (int)config->tier, &compression) &&
         av2_get_profile_level_factors((int)config->profile, &factors);
}

static bool resolve_parameters(const Av2DmConfig *config,
                               Av2DmResolvedParameters *parameters) {
  memset(parameters, 0, sizeof(*parameters));
  av2_dm_rational_init(&parameters->limits.bit_rate);
  av2_dm_rational_init(&parameters->limits.buffer_size);
  av2_dm_rational_init(&parameters->decoder_buffer_delay);
  av2_dm_rational_init(&parameters->encoder_buffer_delay);
  av2_dm_rational_init(&parameters->dec_ct);
  av2_dm_rational_init(&parameters->disp_ct);
  if (config->level_limits_present) {
    if (!av2_dm_level_limits_copy(&parameters->limits, &config->level_limits)) {
      return false;
    }
  } else if (!av2_dm_get_level_limits(config->level_idx, config->tier,
                                      config->profile, &parameters->limits)) {
    return false;
  }
  if (!rational_normalize(&parameters->limits.bit_rate) ||
      !rational_normalize(&parameters->limits.buffer_size) ||
      !av2_dm_rational_make(config->num_units_in_display_tick,
                            config->time_scale, &parameters->disp_ct)) {
    return false;
  }
  if (config->mode == AV2_DM_DECODING_SCHEDULE_MODE &&
      !av2_dm_rational_make(config->num_units_in_decoding_tick,
                            config->time_scale, &parameters->dec_ct)) {
    return false;
  }

  uint32_t decoder_delay = 70000;
  uint32_t encoder_delay = 20000;
  if (config->mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    if (!config->scope.whole_xlayer &&
        config->operating_point_parameters_present) {
      decoder_delay = config->operating_point_decoder_buffer_delay;
      encoder_delay = config->operating_point_encoder_buffer_delay;
      parameters->low_delay_mode = config->operating_point_low_delay_mode;
    } else if (config->sequence_parameters_present) {
      // DM-SPEC-1: operating-point selection falls back to the associated
      // sequence-header parameters when OP parameters are absent.
      decoder_delay = config->sequence_decoder_buffer_delay;
      encoder_delay = config->sequence_encoder_buffer_delay;
      parameters->low_delay_mode = config->sequence_low_delay_mode;
    }
  }
  if (!av2_dm_rational_make(decoder_delay, 90000,
                            &parameters->decoder_buffer_delay) ||
      !av2_dm_rational_make(encoder_delay, 90000,
                            &parameters->encoder_buffer_delay)) {
    return false;
  }
  parameters->decoder_buffer_delay_ticks = decoder_delay;
  return true;
}

static void resolved_parameters_destroy(Av2DmResolvedParameters *parameters) {
  av2_dm_level_limits_destroy(&parameters->limits);
  av2_dm_rational_destroy(&parameters->decoder_buffer_delay);
  av2_dm_rational_destroy(&parameters->encoder_buffer_delay);
  av2_dm_rational_destroy(&parameters->dec_ct);
  av2_dm_rational_destroy(&parameters->disp_ct);
  memset(parameters, 0, sizeof(*parameters));
}

static bool apply_parameters(Av2DecoderModel *model, const Av2DmConfig *config,
                             const Av2DmResolvedParameters *parameters) {
  Av2DmConfig updated_config = { 0 };
  Av2DmLevelLimits updated_limits = { 0 };
  Av2DmRational decoder_buffer_delay = { 0 };
  Av2DmRational encoder_buffer_delay = { 0 };
  Av2DmRational dec_ct = { 0 };
  Av2DmRational disp_ct = { 0 };
  const bool copied =
      av2_dm_config_copy(&updated_config, config) &&
      av2_dm_level_limits_copy(&updated_limits, &parameters->limits) &&
      av2_dm_rational_copy(&decoder_buffer_delay,
                           &parameters->decoder_buffer_delay) &&
      av2_dm_rational_copy(&encoder_buffer_delay,
                           &parameters->encoder_buffer_delay) &&
      av2_dm_rational_copy(&dec_ct, &parameters->dec_ct) &&
      av2_dm_rational_copy(&disp_ct, &parameters->disp_ct);
  if (!copied) {
    av2_dm_config_destroy(&updated_config);
    av2_dm_level_limits_destroy(&updated_limits);
    av2_dm_rational_destroy(&decoder_buffer_delay);
    av2_dm_rational_destroy(&encoder_buffer_delay);
    av2_dm_rational_destroy(&dec_ct);
    av2_dm_rational_destroy(&disp_ct);
    return false;
  }
  av2_dm_config_destroy(&model->config);
  model->config = updated_config;
  av2_dm_level_limits_destroy(&model->limits);
  model->limits = updated_limits;
  av2_dm_rational_move(&model->decoder_buffer_delay, &decoder_buffer_delay);
  av2_dm_rational_move(&model->encoder_buffer_delay, &encoder_buffer_delay);
  model->decoder_buffer_delay_ticks = parameters->decoder_buffer_delay_ticks;
  model->low_delay_mode = parameters->low_delay_mode;
  av2_dm_rational_move(&model->dec_ct, &dec_ct);
  av2_dm_rational_move(&model->disp_ct, &disp_ct);
  return true;
}

Av2DecoderModel *av2_decoder_model_create(const Av2DmConfig *config,
                                          Av2DmReportFn report,
                                          void *report_opaque) {
  if (config == NULL) return NULL;
  Av2DecoderModel *const model = internal_calloc(1, sizeof(*model));
  if (model == NULL) return NULL;
  av2_dm_level_limits_init(&model->config.level_limits);
  av2_dm_level_limits_init(&model->limits);
  av2_dm_rational_init(&model->decoder_buffer_delay);
  av2_dm_rational_init(&model->encoder_buffer_delay);
  av2_dm_rational_init(&model->dec_ct);
  av2_dm_rational_init(&model->disp_ct);
  av2_dm_rational_init(&model->buffer_size_base);
  av2_dm_rational_init(&model->pending_buffer_size);
  dfg_record_init(&model->previous_dfg);
  av2_dm_rational_init(&model->most_recent_rap_scheduled_removal);
  av2_dm_rational_init(&model->previous_output_presentation_offset);
  av2_dm_rational_init(&model->last_presentation_offset);
  av2_dm_rational_init(&model->last_presentation);
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    av2_dm_rational_init(
        &model->rap_presentation_anchors[i].presentation_offset);
  }
  av2_dm_rational_init(&model->last_frame_parsing_time);
  av2_dm_rational_init(&model->last_display_duration);
  av2_dm_rational_init(&model->latest_timed_tu_output_time);
  av2_dm_rational_init(&model->pending_display_late.threshold);
  av2_dm_rational_init(&model->pending_display_late.observed);
  av2_dm_rational_init(&model->pending_display_late.presentation_offset);
  av2_dm_rational_init(&model->pending_decode_deadline.threshold);
  av2_dm_rational_init(&model->pending_decode_deadline.observed);
  av2_dm_rational_init(&model->pending_decode_deadline.presentation_offset);
  if (!av2_dm_config_copy(&model->config, config)) {
    av2_decoder_model_destroy(model);
    return NULL;
  }
  model->report = report;
  model->report_opaque = report_opaque;
  model->result.status = AV2_DM_RESULT_CONFORMANT;
  model->result.applicability = config->applicability;
  model->result.mode = config->mode;
  model->result.scope = config->scope;

  if (config->level_idx == 31 ||
      config->applicability == AV2_DM_NOT_APPLICABLE) {
    model->result.applicability = AV2_DM_NOT_APPLICABLE;
    update_result_status(model);
    return model;
  }
  if (config->applicability == AV2_DM_MISSING_REQUIRED_INPUT) {
    missing_input(model);
    return model;
  }
  if (config->num_ref_frames == 0 ||
      config->num_ref_frames > AV2_DM_MAX_REF_FRAMES ||
      !lane_initialize(&model->lane, config->num_ref_frames) ||
      !lane_initialize(&model->resource_lane, config->num_ref_frames)) {
    av2_decoder_model_destroy(model);
    return NULL;
  }
  Av2DmResolvedParameters parameters = { 0 };
  if (!parameter_inputs_valid(config)) {
    missing_input(model);
  } else if (!resolve_parameters(config, &parameters)) {
    arithmetic_failure(model);
  } else if (!apply_parameters(model, config, &parameters) ||
             !av2_dm_rational_copy(&model->buffer_size_base,
                                   &parameters.limits.buffer_size)) {
    arithmetic_failure(model);
  }
  resolved_parameters_destroy(&parameters);

  if (config->ras_start && !model->processing_stopped) {
    const bool seeded = config->ras_seed_complete &&
                        seed_ras_buffers(model, &model->lane) &&
                        seed_ras_buffers(model, &model->resource_lane);
    if (!seeded) {
      (void)av2_dm_buffer_pool_initialize(&model->lane.pool,
                                          config->num_ref_frames);
      (void)av2_dm_buffer_pool_initialize(&model->resource_lane.pool,
                                          config->num_ref_frames);
      model->lane.current_buffer_index = -1;
      model->resource_lane.current_buffer_index = -1;
      // DM-SPEC-6: a RAS run is provable only when all established long-term
      // slot/generation relationships can be reconstructed.
      if (av2_dm_last_failure_was_allocation()) {
        allocation_failure(model);
      } else {
        missing_input(model);
      }
    }
  }
  update_result_status(model);
  update_storage_stats(model);
  return model;
}

void av2_decoder_model_destroy(Av2DecoderModel *model) {
  if (model == NULL) return;
  av2_dm_config_destroy(&model->config);
  av2_dm_level_limits_destroy(&model->limits);
  av2_dm_rational_destroy(&model->decoder_buffer_delay);
  av2_dm_rational_destroy(&model->encoder_buffer_delay);
  av2_dm_rational_destroy(&model->dec_ct);
  av2_dm_rational_destroy(&model->disp_ct);
  av2_dm_rational_destroy(&model->buffer_size_base);
  av2_dm_rational_destroy(&model->pending_buffer_size);
  for (uint32_t i = 0; i < model->buffer_size_transition_count; ++i) {
    av2_dm_rational_destroy(&model->buffer_size_transitions[i].time);
    av2_dm_rational_destroy(&model->buffer_size_transitions[i].size);
  }
  avm_free(model->buffer_size_transitions);
  lane_destroy(&model->lane);
  lane_destroy(&model->resource_lane);
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    dfg_record_destroy(&model->dfgs[i]);
  }
  avm_free(model->dfgs);
  dfg_record_destroy(&model->previous_dfg);
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    tu_record_destroy(&model->tus[i]);
  }
  avm_free(model->tus);
  av2_dm_rational_destroy(&model->most_recent_rap_scheduled_removal);
  av2_dm_rational_destroy(&model->previous_output_presentation_offset);
  av2_dm_rational_destroy(&model->last_presentation_offset);
  av2_dm_rational_destroy(&model->last_presentation);
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    av2_dm_rational_destroy(
        &model->rap_presentation_anchors[i].presentation_offset);
  }
  av2_dm_rational_destroy(&model->last_frame_parsing_time);
  av2_dm_rational_destroy(&model->last_display_duration);
  av2_dm_rational_destroy(&model->latest_timed_tu_output_time);
  av2_dm_rational_destroy(&model->pending_display_late.threshold);
  av2_dm_rational_destroy(&model->pending_display_late.observed);
  av2_dm_rational_destroy(&model->pending_display_late.presentation_offset);
  av2_dm_rational_destroy(&model->pending_decode_deadline.threshold);
  av2_dm_rational_destroy(&model->pending_decode_deadline.observed);
  av2_dm_rational_destroy(&model->pending_decode_deadline.presentation_offset);
  avm_free(model);
}

static Av2DecoderModel *decoder_model_clone(const Av2DecoderModel *source,
                                            bool *allocation_failed) {
  *allocation_failed = false;
  Av2DecoderModel *const copy = internal_calloc(1, sizeof(*copy));
  if (copy == NULL) {
    *allocation_failed = true;
    return NULL;
  }
  *copy = *source;
  av2_dm_level_limits_init(&copy->config.level_limits);
  av2_dm_level_limits_init(&copy->limits);
  av2_dm_rational_init(&copy->decoder_buffer_delay);
  av2_dm_rational_init(&copy->encoder_buffer_delay);
  av2_dm_rational_init(&copy->dec_ct);
  av2_dm_rational_init(&copy->disp_ct);
  av2_dm_rational_init(&copy->buffer_size_base);
  av2_dm_rational_init(&copy->pending_buffer_size);
  copy->buffer_size_transitions = NULL;
  copy->buffer_size_transition_count = 0;
  copy->buffer_size_transition_capacity = 0;
  memset(&copy->lane, 0, sizeof(copy->lane));
  memset(&copy->resource_lane, 0, sizeof(copy->resource_lane));
  copy->dfgs = NULL;
  copy->dfg_count = 0;
  copy->dfg_capacity = 0;
  dfg_record_init(&copy->previous_dfg);
  copy->tus = NULL;
  copy->tu_count = 0;
  copy->tu_capacity = 0;
  av2_dm_rational_init(&copy->most_recent_rap_scheduled_removal);
  av2_dm_rational_init(&copy->previous_output_presentation_offset);
  av2_dm_rational_init(&copy->last_presentation_offset);
  av2_dm_rational_init(&copy->last_presentation);
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    av2_dm_rational_init(
        &copy->rap_presentation_anchors[i].presentation_offset);
  }
  av2_dm_rational_init(&copy->last_frame_parsing_time);
  av2_dm_rational_init(&copy->last_display_duration);
  av2_dm_rational_init(&copy->latest_timed_tu_output_time);
  av2_dm_rational_init(&copy->pending_display_late.threshold);
  av2_dm_rational_init(&copy->pending_display_late.observed);
  av2_dm_rational_init(&copy->pending_display_late.presentation_offset);
  av2_dm_rational_init(&copy->pending_decode_deadline.threshold);
  av2_dm_rational_init(&copy->pending_decode_deadline.observed);
  av2_dm_rational_init(&copy->pending_decode_deadline.presentation_offset);

  if (!av2_dm_config_copy(&copy->config, &source->config) ||
      !av2_dm_level_limits_copy(&copy->limits, &source->limits) ||
      !av2_dm_rational_copy(&copy->decoder_buffer_delay,
                            &source->decoder_buffer_delay) ||
      !av2_dm_rational_copy(&copy->encoder_buffer_delay,
                            &source->encoder_buffer_delay) ||
      !av2_dm_rational_copy(&copy->dec_ct, &source->dec_ct) ||
      !av2_dm_rational_copy(&copy->disp_ct, &source->disp_ct) ||
      !av2_dm_rational_copy(&copy->buffer_size_base,
                            &source->buffer_size_base) ||
      !av2_dm_rational_copy(&copy->pending_buffer_size,
                            &source->pending_buffer_size) ||
      !lane_copy(&copy->lane, &source->lane) ||
      !lane_copy(&copy->resource_lane, &source->resource_lane)) {
    goto failure;
  }

  if (source->buffer_size_transition_capacity != 0) {
    copy->buffer_size_transitions =
        internal_calloc(source->buffer_size_transition_capacity,
                        sizeof(*copy->buffer_size_transitions));
    if (copy->buffer_size_transitions == NULL) {
      *allocation_failed = true;
      goto failure;
    }
    copy->buffer_size_transition_capacity =
        source->buffer_size_transition_capacity;
  }
  for (uint32_t i = 0; i < source->buffer_size_transition_count; ++i) {
    Av2DmBufferSizeTransition *const destination =
        &copy->buffer_size_transitions[i];
    const Av2DmBufferSizeTransition *const existing =
        &source->buffer_size_transitions[i];
    destination->dfg_number = existing->dfg_number;
    destination->after_removal = existing->after_removal;
    ++copy->buffer_size_transition_count;
    if (!av2_dm_rational_copy(&destination->time, &existing->time) ||
        !av2_dm_rational_copy(&destination->size, &existing->size)) {
      goto failure;
    }
  }

  if (source->dfg_capacity != 0) {
    copy->dfgs = internal_calloc(source->dfg_capacity, sizeof(*copy->dfgs));
    if (copy->dfgs == NULL) {
      *allocation_failed = true;
      goto failure;
    }
    copy->dfg_capacity = source->dfg_capacity;
  }
  for (uint32_t i = 0; i < source->dfg_count; ++i) {
    dfg_record_init(&copy->dfgs[i]);
    if (!dfg_record_copy(&copy->dfgs[i], &source->dfgs[i])) goto failure;
    ++copy->dfg_count;
  }
  if (source->previous_dfg_valid &&
      !dfg_record_copy(&copy->previous_dfg, &source->previous_dfg)) {
    goto failure;
  }

  if (source->tu_capacity != 0) {
    copy->tus = internal_calloc(source->tu_capacity, sizeof(*copy->tus));
    if (copy->tus == NULL) {
      *allocation_failed = true;
      goto failure;
    }
    copy->tu_capacity = source->tu_capacity;
  }
  for (uint32_t i = 0; i < source->tu_count; ++i) {
    tu_record_init(&copy->tus[i]);
    if (!tu_record_copy(&copy->tus[i], &source->tus[i])) goto failure;
    ++copy->tu_count;
  }

  if (!av2_dm_rational_copy(&copy->most_recent_rap_scheduled_removal,
                            &source->most_recent_rap_scheduled_removal) ||
      !av2_dm_rational_copy(&copy->previous_output_presentation_offset,
                            &source->previous_output_presentation_offset) ||
      !av2_dm_rational_copy(&copy->last_presentation_offset,
                            &source->last_presentation_offset) ||
      !av2_dm_rational_copy(&copy->last_presentation,
                            &source->last_presentation) ||
      !av2_dm_rational_copy(&copy->last_frame_parsing_time,
                            &source->last_frame_parsing_time) ||
      !av2_dm_rational_copy(&copy->last_display_duration,
                            &source->last_display_duration) ||
      !av2_dm_rational_copy(&copy->latest_timed_tu_output_time,
                            &source->latest_timed_tu_output_time)) {
    goto failure;
  }
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    if (!av2_dm_rational_copy(
            &copy->rap_presentation_anchors[i].presentation_offset,
            &source->rap_presentation_anchors[i].presentation_offset)) {
      goto failure;
    }
  }
  if (!av2_dm_rational_copy(&copy->pending_display_late.threshold,
                            &source->pending_display_late.threshold) ||
      !av2_dm_rational_copy(&copy->pending_display_late.observed,
                            &source->pending_display_late.observed) ||
      !av2_dm_rational_copy(
          &copy->pending_display_late.presentation_offset,
          &source->pending_display_late.presentation_offset) ||
      !av2_dm_rational_copy(&copy->pending_decode_deadline.threshold,
                            &source->pending_decode_deadline.threshold) ||
      !av2_dm_rational_copy(&copy->pending_decode_deadline.observed,
                            &source->pending_decode_deadline.observed) ||
      !av2_dm_rational_copy(
          &copy->pending_decode_deadline.presentation_offset,
          &source->pending_decode_deadline.presentation_offset)) {
    goto failure;
  }
  return copy;

failure:
  if (av2_dm_last_failure_was_allocation()) {
    *allocation_failed = true;
  }
  av2_decoder_model_destroy(copy);
  return NULL;
}

typedef struct Av2DmModelTransaction {
  Av2DecoderModel *snapshot;
  Av2DmReportFn report;
  void *report_opaque;
  Av2DmViolation *violations;
  size_t violation_count;
  size_t violation_capacity;
  bool allocation_failed;
  bool arithmetic_failed;
} Av2DmModelTransaction;

static void transaction_report(void *opaque, const Av2DmViolation *violation) {
  Av2DmModelTransaction *const transaction = opaque;
  if (transaction->allocation_failed || transaction->arithmetic_failed) return;
  if (transaction->violation_count == transaction->violation_capacity) {
    const size_t new_capacity = transaction->violation_capacity == 0
                                    ? 4
                                    : transaction->violation_capacity * 2;
    if (new_capacity < transaction->violation_capacity ||
        new_capacity > SIZE_MAX / sizeof(*transaction->violations)) {
      transaction->arithmetic_failed = true;
      return;
    }
    Av2DmViolation *const replacement =
        internal_calloc(new_capacity, sizeof(*replacement));
    if (replacement == NULL) {
      transaction->allocation_failed = true;
      return;
    }
    if (transaction->violations != NULL) {
      memcpy(replacement, transaction->violations,
             transaction->violation_count * sizeof(*replacement));
      avm_free(transaction->violations);
    }
    transaction->violations = replacement;
    transaction->violation_capacity = new_capacity;
  }
  Av2DmViolation *const stored =
      &transaction->violations[transaction->violation_count++];
  av2_dm_violation_init(stored);
  if (!av2_dm_violation_copy(stored, violation)) {
    if (av2_dm_last_failure_was_allocation()) {
      transaction->allocation_failed = true;
    } else {
      transaction->arithmetic_failed = true;
    }
  }
}

static void destroy_model_transaction(Av2DmModelTransaction *transaction) {
  for (size_t i = 0; i < transaction->violation_count; ++i) {
    av2_dm_violation_destroy(&transaction->violations[i]);
  }
  avm_free(transaction->violations);
  memset(transaction, 0, sizeof(*transaction));
}

static bool begin_model_transaction(Av2DecoderModel *model,
                                    Av2DmModelTransaction *transaction) {
  memset(transaction, 0, sizeof(*transaction));
  transaction->snapshot =
      decoder_model_clone(model, &transaction->allocation_failed);
  if (transaction->snapshot == NULL) {
    if (transaction->allocation_failed) {
      allocation_failure(model);
    } else {
      arithmetic_failure(model);
    }
    return false;
  }
  transaction->report = model->report;
  transaction->report_opaque = model->report_opaque;
  model->report = transaction_report;
  model->report_opaque = transaction;
  return true;
}

static void end_model_transaction(Av2DecoderModel *model,
                                  Av2DmModelTransaction *transaction) {
  Av2DecoderModel *const snapshot = transaction->snapshot;
  const bool allocation_failed =
      transaction->allocation_failed ||
      (model->result.allocation_failed && !snapshot->result.allocation_failed);
  const bool arithmetic_failed =
      transaction->arithmetic_failed ||
      (model->result.arithmetic_failed && !snapshot->result.arithmetic_failed);
  if (allocation_failed || arithmetic_failed) {
    Av2DecoderModel failed = *model;
    *model = *snapshot;
    *snapshot = failed;
    av2_decoder_model_destroy(snapshot);
    transaction->snapshot = NULL;
    if (allocation_failed) {
      allocation_failure(model);
    } else {
      model->result.arithmetic_failed = true;
      model->processing_stopped = true;
      update_result_status(model);
    }
    destroy_model_transaction(transaction);
    return;
  }
  model->report = transaction->report;
  model->report_opaque = transaction->report_opaque;
  av2_decoder_model_destroy(snapshot);
  transaction->snapshot = NULL;
  if (transaction->report != NULL) {
    for (size_t i = 0; i < transaction->violation_count; ++i) {
      transaction->report(transaction->report_opaque,
                          &transaction->violations[i]);
    }
  }
  destroy_model_transaction(transaction);
}

static bool rational_ceil_to_integer(const Av2DmRational *value,
                                     Av2DmRational *result);

static bool rational_ceil_ratio_to_tick(const Av2DmRational *time,
                                        const Av2DmRational *tick,
                                        Av2DmRational *result) {
  if (time->negative || tick->negative || av2_dm_rational_is_zero(tick)) {
    return false;
  }
  Av2DmRational reciprocal = { 0 };
  Av2DmRational ratio = { 0 };
  Av2DmRational quotient = { 0 };
  const bool rounded = rational_reciprocal(tick, &reciprocal) &&
                       rational_multiply(time, &reciprocal, &ratio) &&
                       rational_ceil_to_integer(&ratio, &quotient) &&
                       rational_multiply(tick, &quotient, result);
  av2_dm_rational_destroy(&reciprocal);
  av2_dm_rational_destroy(&ratio);
  av2_dm_rational_destroy(&quotient);
  return rounded;
}

static bool rational_ceil_from_anchor(const Av2DmRational *time,
                                      const Av2DmRational *anchor,
                                      const Av2DmRational *tick,
                                      Av2DmRational *result) {
  Av2DmRational delta = { 0 };
  Av2DmRational rounded = { 0 };
  const bool calculated = av2_dm_rational_subtract(time, anchor, &delta) &&
                          rational_ceil_ratio_to_tick(&delta, tick, &rounded) &&
                          av2_dm_rational_add(anchor, &rounded, result);
  av2_dm_rational_destroy(&delta);
  av2_dm_rational_destroy(&rounded);
  return calculated;
}

static bool rational_ceil_to_integer(const Av2DmRational *value,
                                     Av2DmRational *result) {
  rational_begin_operation();
  if (value == NULL || result == NULL) return false;
  Av2DmBigUInt magnitude = { 0 }, denominator = { 0 };
  Av2DmBigUInt quotient = { 0 }, remainder = { 0 }, one = { 0 };
  Av2DmBigUInt rounded = { 0 };
  Av2DmRational temporary = { 0 };
  bool ok = big_uint_from_rational(value, false, &magnitude) &&
            big_uint_from_rational(value, true, &denominator) &&
            !big_uint_is_zero(&denominator) &&
            big_uint_divide(&magnitude, &denominator, &quotient, &remainder);
  if (ok && !value->negative && !big_uint_is_zero(&remainder)) {
    ok = big_uint_from_u64(&one, 1) && big_uint_add(&quotient, &one, &rounded);
    if (ok) big_uint_move(&quotient, &rounded);
  }
  if (ok) {
    if (big_uint_is_zero(&one)) ok = big_uint_from_u64(&one, 1);
    if (ok) ok = rational_store(&temporary, &quotient, &one, value->negative);
  }
  if (ok) av2_dm_rational_move(result, &temporary);
  av2_dm_rational_destroy(&temporary);
  big_uint_destroy(&magnitude);
  big_uint_destroy(&denominator);
  big_uint_destroy(&quotient);
  big_uint_destroy(&remainder);
  big_uint_destroy(&one);
  big_uint_destroy(&rounded);
  return ok;
}

static void compare_upper_limit(Av2DecoderModel *model, Av2DmViolationCode code,
                                uint64_t event_index,
                                const Av2DmRational *observed,
                                const Av2DmRational *limit) {
  bool greater;
  if (!rational_greater(observed, limit, &greater)) {
    arithmetic_failure(model);
  } else if (greater) {
    report_violation(model, code, event_index, observed, limit);
  }
}

static void compare_upper_limit_for_affected(
    Av2DecoderModel *model, Av2DmViolationCode code, uint64_t event_index,
    Av2DmViolationAffectedKind affected_kind, uint64_t affected_index,
    const Av2DmRational *observed, const Av2DmRational *limit) {
  bool greater;
  if (!rational_greater(observed, limit, &greater)) {
    arithmetic_failure(model);
  } else if (greater) {
    report_violation_for_affected(model, code, event_index, affected_kind,
                                  affected_index, observed, limit, NULL);
  }
}

static void compare_upper_limit_for_affected_with_detail(
    Av2DecoderModel *model, Av2DmViolationCode code, uint64_t event_index,
    Av2DmViolationAffectedKind affected_kind, uint64_t affected_index,
    const Av2DmRational *observed, const Av2DmRational *limit,
    const Av2DmViolationDetail *detail) {
  bool greater;
  if (!rational_greater(observed, limit, &greater)) {
    arithmetic_failure(model);
  } else if (greater) {
    report_violation_for_affected(model, code, event_index, affected_kind,
                                  affected_index, observed, limit, detail);
  }
}

static void compare_lower_limit(Av2DecoderModel *model, Av2DmViolationCode code,
                                uint64_t event_index,
                                const Av2DmRational *observed,
                                const Av2DmRational *limit) {
  bool less;
  if (!rational_less(observed, limit, &less)) {
    arithmetic_failure(model);
  } else if (less) {
    report_violation(model, code, event_index, observed, limit);
  }
}

static void compare_lower_limit_for_affected(
    Av2DecoderModel *model, Av2DmViolationCode code, uint64_t event_index,
    Av2DmViolationAffectedKind affected_kind, uint64_t affected_index,
    const Av2DmRational *observed, const Av2DmRational *limit) {
  bool less;
  if (!rational_less(observed, limit, &less)) {
    arithmetic_failure(model);
  } else if (less) {
    report_violation_for_affected(model, code, event_index, affected_kind,
                                  affected_index, observed, limit, NULL);
  }
}

static void compare_lower_limit_for_affected_with_detail(
    Av2DecoderModel *model, Av2DmViolationCode code, uint64_t event_index,
    Av2DmViolationAffectedKind affected_kind, uint64_t affected_index,
    const Av2DmRational *observed, const Av2DmRational *limit,
    const Av2DmViolationDetail *detail) {
  bool less;
  if (!rational_less(observed, limit, &less)) {
    arithmetic_failure(model);
  } else if (less) {
    report_violation_for_affected(model, code, event_index, affected_kind,
                                  affected_index, observed, limit, detail);
  }
}

static Av2DmViolationDetail buffer_pool_violation_detail(
    const Av2DmBufferPool *pool, bool resource_lane) {
  Av2DmViolationDetail detail;
  memset(&detail, 0, sizeof(detail));
  detail.kind = AV2_DM_VIOLATION_DETAIL_BUFFER_POOL;
  detail.value.buffer_pool.resource_lane = resource_lane;
  detail.value.buffer_pool.pool_size = pool->pool_size;
  detail.value.buffer_pool.frames_in_use = 0;
  for (uint32_t i = 0; i < pool->pool_size; ++i) {
    if (pool->buffers[i].decoder_ref_count != 0 ||
        pool->buffers[i].player_ref_count != 0) {
      ++detail.value.buffer_pool.frames_in_use;
    }
  }
  detail.value.buffer_pool.free_buffers =
      pool->pool_size - detail.value.buffer_pool.frames_in_use;
  for (uint32_t i = 0; i < pool->pool_size; ++i) {
    if (pool->buffers[i].decoder_ref_count != 0) {
      ++detail.value.buffer_pool.decoder_held_buffers;
    }
    if (pool->buffers[i].player_ref_count != 0) {
      ++detail.value.buffer_pool.player_held_buffers;
    }
  }
  return detail;
}

static Av2DmTuRecord *find_tu(Av2DecoderModel *model,
                              uint64_t temporal_unit_index) {
  for (uint32_t i = model->tu_count; i > 0; --i) {
    if (model->tus[i - 1].temporal_unit_index == temporal_unit_index) {
      return &model->tus[i - 1];
    }
  }
  return NULL;
}

static Av2DmTuRecord *get_tu(Av2DecoderModel *model,
                             uint64_t temporal_unit_index,
                             uint64_t event_index) {
  Av2DmTuRecord *const existing = find_tu(model, temporal_unit_index);
  if (existing != NULL) return existing;
  if (model->tu_count == UINT32_MAX ||
      !grow_array((void **)&model->tus, &model->tu_capacity, model->tu_count,
                  sizeof(*model->tus))) {
    arithmetic_failure(model);
    return NULL;
  }
  Av2DmTuRecord *const tu = &model->tus[model->tu_count++];
  tu_record_init(tu);
  tu->temporal_unit_index = temporal_unit_index;
  tu->event_index = event_index;
  tu->cvs_number = model->cvs_number;
  if (!av2_dm_level_limits_copy(&tu->limits, &model->limits)) {
    tu_record_destroy(tu);
    --model->tu_count;
    arithmetic_failure(model);
    return NULL;
  }
  tu->tier = model->config.tier;
  tu->still_picture = model->config.still_picture;
  tu->max_frame_width = model->config.max_frame_width;
  tu->max_frame_height = model->config.max_frame_height;
  return tu;
}

static bool update_latest_timed_tu(Av2DecoderModel *model,
                                   const Av2DmRational *output_time) {
  if (!model->latest_timed_tu_valid) {
    if (!av2_dm_rational_copy(&model->latest_timed_tu_output_time,
                              output_time)) {
      return false;
    }
    model->latest_timed_tu_valid = true;
    return true;
  }
  int comparison;
  if (!av2_dm_rational_compare(output_time, &model->latest_timed_tu_output_time,
                               &comparison)) {
    return false;
  }
  if (comparison > 0 &&
      !av2_dm_rational_copy(&model->latest_timed_tu_output_time, output_time)) {
    return false;
  }
  return true;
}

static void check_static_level_limits(Av2DecoderModel *model,
                                      const Av2DmFrameEvent *event) {
  Av2DmRational observed = { 0 };
  Av2DmRational limit = { 0 };
  if (!rational_from_product(event->frame_width, event->frame_height,
                             &observed) ||
      !av2_dm_rational_make(model->limits.max_picture_size, 1, &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  compare_upper_limit(model, AV2_DM_VIOLATION_MAX_PICTURE_SIZE,
                      event->event_index, &observed, &limit);

#define CHECK_INTEGER_LIMIT(field, member, violation_code)             \
  do {                                                                 \
    if (!av2_dm_rational_make((field), 1, &observed) ||                \
        !av2_dm_rational_make(model->limits.member, 1, &limit)) {      \
      arithmetic_failure(model);                                       \
    } else {                                                           \
      compare_upper_limit(model, (violation_code), event->event_index, \
                          &observed, &limit);                          \
    }                                                                  \
  } while (0)
  CHECK_INTEGER_LIMIT(event->frame_width, max_horizontal_size,
                      AV2_DM_VIOLATION_MAX_HORIZONTAL_SIZE);
  CHECK_INTEGER_LIMIT(event->frame_height, max_vertical_size,
                      AV2_DM_VIOLATION_MAX_VERTICAL_SIZE);
  CHECK_INTEGER_LIMIT(event->num_tiles, max_tiles, AV2_DM_VIOLATION_MAX_TILES);
  CHECK_INTEGER_LIMIT(event->tile_columns, max_tile_columns,
                      AV2_DM_VIOLATION_MAX_TILE_COLUMNS);
  CHECK_INTEGER_LIMIT(event->max_tile_width, max_tile_width,
                      AV2_DM_VIOLATION_MAX_TILE_WIDTH);
  CHECK_INTEGER_LIMIT(event->max_tile_area, max_tile_area,
                      AV2_DM_VIOLATION_MAX_TILE_AREA);
#undef CHECK_INTEGER_LIMIT

  if (event->frame_width < 16) {
    if (!av2_dm_rational_make(event->frame_width, 1, &observed) ||
        !av2_dm_rational_make(16, 1, &limit)) {
      arithmetic_failure(model);
    }
    report_violation(model, AV2_DM_VIOLATION_MIN_HORIZONTAL_SIZE,
                     event->event_index, &observed, &limit);
  }
  if (event->frame_height < 16) {
    if (!av2_dm_rational_make(event->frame_height, 1, &observed) ||
        !av2_dm_rational_make(16, 1, &limit)) {
      arithmetic_failure(model);
    }
    report_violation(model, AV2_DM_VIOLATION_MIN_VERTICAL_SIZE,
                     event->event_index, &observed, &limit);
  }
  if (!event->non_rightmost_tile_width_valid) {
    report_violation(model, AV2_DM_VIOLATION_MIN_TILE_WIDTH, event->event_index,
                     NULL, NULL);
  }
  av2_dm_rational_destroy(&observed);
  av2_dm_rational_destroy(&limit);
}

static bool release_presented_buffers(Av2DmLane *lane,
                                      const Av2DmRational *removal) {
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (buffer->player_ref_count == 0 || !buffer->presentation_time_valid) {
      continue;
    }
    int comparison;
    if (!av2_dm_rational_compare(&buffer->presentation_time, removal,
                                 &comparison)) {
      return false;
    }
    if (comparison <= 0) {
      buffer->player_ref_count = 0;
      if (buffer->decoder_ref_count == 0) buffer_reset(buffer);
    }
  }
  return true;
}

static bool next_resource_removal(Av2DecoderModel *model, Av2DmLane *lane,
                                  uint64_t dfg_index, Av2DmRational *removal) {
  if (dfg_index == 0) {
    return av2_dm_rational_copy(removal, &model->decoder_buffer_delay);
  }
  if (!release_presented_buffers(lane, &lane->time)) return false;
  if (av2_dm_buffer_pool_get_free_buffer(&lane->pool) >= 0) {
    return av2_dm_rational_copy(removal, &lane->time);
  }
  bool found = false;
  Av2DmRational earliest = { 0 };
  bool copied = false;
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    const Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (buffer->decoder_ref_count != 0 || buffer->player_ref_count == 0) {
      continue;
    }
    if (!buffer->presentation_time_valid) {
      missing_input(model);
      goto cleanup;
    }
    if (!found) {
      if (!av2_dm_rational_copy(&earliest, &buffer->presentation_time)) {
        goto cleanup;
      }
      found = true;
    } else {
      bool less;
      if (!rational_less(&buffer->presentation_time, &earliest, &less)) {
        goto cleanup;
      }
      if (less &&
          !av2_dm_rational_copy(&earliest, &buffer->presentation_time)) {
        goto cleanup;
      }
    }
  }
  copied = found && av2_dm_rational_copy(removal, &earliest);

cleanup:
  av2_dm_rational_destroy(&earliest);
  return copied;
}

static bool lane_start_decode(Av2DmLane *lane, const Av2DmRational *removal,
                              const Av2DmRational *decode_time,
                              uint64_t generation, int32_t *buffer_index) {
  if (!release_presented_buffers(lane, removal)) return false;
  if (!av2_dm_rational_copy(&lane->time, removal)) return false;
  const int32_t free_buffer = av2_dm_buffer_pool_get_free_buffer(&lane->pool);
  *buffer_index = free_buffer;
  lane->current_buffer_index = free_buffer;
  if (!av2_dm_rational_add(&lane->time, decode_time, &lane->time)) {
    return false;
  }
  if (free_buffer < 0) return true;
  Av2DmBuffer *const buffer = &lane->pool.buffers[free_buffer];
  buffer_reset(buffer);
  buffer->generation_valid = true;
  buffer->generation = generation;
  if (!av2_dm_rational_copy(&buffer->decode_completion_time, &lane->time)) {
    buffer_reset(buffer);
    return false;
  }
  buffer->decode_completion_time_valid = true;
  return true;
}

static bool calculate_decode_time(Av2DecoderModel *model,
                                  const Av2DmFrameEvent *event,
                                  uint64_t *luma_samples,
                                  Av2DmRational *decode_time) {
  uint64_t samples;
  if (event->frame_is_intra) {
    Av2DmRational product = { 0 };
    if (!rational_from_product(event->frame_width, event->frame_height,
                               &product) ||
        !rational_to_u64(&product, &samples)) {
      av2_dm_rational_destroy(&product);
      return false;
    }
    av2_dm_rational_destroy(&product);
    if (event->allow_global_intrabc && event->inloop_filtering_enabled) {
      if (samples > UINT64_MAX / 2) return false;
      samples *= 2;
    }
  } else {
    Av2DmRational product = { 0 };
    if (!rational_from_product(model->config.max_frame_width,
                               model->config.max_frame_height, &product) ||
        !rational_to_u64(&product, &samples)) {
      av2_dm_rational_destroy(&product);
      return false;
    }
    av2_dm_rational_destroy(&product);
  }
  *luma_samples = samples;
  return av2_dm_rational_make(samples, model->limits.max_decode_rate,
                              decode_time);
}

static void check_frame_parsing_constraints(Av2DecoderModel *model,
                                            Av2DmDfgRecord *dfg,
                                            const Av2DmRational *interval,
                                            uint64_t proving_event_index) {
  if (dfg->still_picture) return;
  Av2DmViolationDetail detail;
  memset(&detail, 0, sizeof(detail));
  detail.kind = AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL;
  detail.value.frame_interval = *interval;
  const Av2DmLevelLimits *const limits = &dfg->limits;
  Av2DmRational limit = { 0 };
  Av2DmRational observed = { 0 };
  Av2DmRational dynamic_tiles = { 0 };
  Av2DmRational one = { 0 };
  Av2DmRational max_tiles = { 0 };
  Av2DmRational compressed_limit_1 = { 0 };
  Av2DmRational compressed_limit_2 = { 0 };
  Av2DmRational symbol_factor_a = { 0 };
  Av2DmRational symbol_factor_b = { 0 };
  Av2DmRational symbol_factor = { 0 };
#define CLEANUP_FRAME_PARSING_RATIONALS()         \
  do {                                            \
    av2_dm_rational_destroy(&limit);              \
    av2_dm_rational_destroy(&observed);           \
    av2_dm_rational_destroy(&dynamic_tiles);      \
    av2_dm_rational_destroy(&one);                \
    av2_dm_rational_destroy(&max_tiles);          \
    av2_dm_rational_destroy(&compressed_limit_1); \
    av2_dm_rational_destroy(&compressed_limit_2); \
    av2_dm_rational_destroy(&symbol_factor_a);    \
    av2_dm_rational_destroy(&symbol_factor_b);    \
    av2_dm_rational_destroy(&symbol_factor);      \
  } while (0)
  if (!av2_dm_rational_multiply_u64(interval, limits->max_decode_rate,
                                    &limit) ||
      !av2_dm_rational_make(dfg->luma_samples, 1, &observed)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_FRAME_DECODE_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);

  if (!av2_dm_rational_multiply_u64(interval, (uint64_t)limits->max_tiles * 120,
                                    &dynamic_tiles) ||
      !av2_dm_rational_make(1, 1, &one) ||
      !av2_dm_rational_make(limits->max_tiles, 1, &max_tiles) ||
      !rational_max(&dynamic_tiles, &one, &dynamic_tiles)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  bool greater;
  if (!rational_greater(&dynamic_tiles, &max_tiles, &greater)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  if (greater && !av2_dm_rational_copy(&dynamic_tiles, &max_tiles)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  if (!av2_dm_rational_make(dfg->num_tiles, 1, &observed)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_FRAME_TILE_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed,
      &dynamic_tiles, &detail);

  if (dfg->luma_samples > UINT64_MAX / limits->picture_size_profile_factor) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  const uint64_t picture_units =
      dfg->luma_samples * limits->picture_size_profile_factor / 8;
  if (!av2_dm_rational_make(picture_units, 1, &compressed_limit_1) ||
      !av2_dm_rational_multiply_u64(&compressed_limit_1, 5,
                                    &compressed_limit_1) ||
      !av2_dm_rational_divide_u64(&compressed_limit_1, 4,
                                  &compressed_limit_1) ||
      !av2_dm_rational_multiply_u64(interval, limits->max_decode_rate,
                                    &compressed_limit_2) ||
      !av2_dm_rational_multiply_u64(&compressed_limit_2,
                                    limits->picture_size_profile_factor,
                                    &compressed_limit_2) ||
      !av2_dm_rational_divide_u64(&compressed_limit_2,
                                  (uint64_t)8 * limits->min_compression_basis,
                                  &compressed_limit_2)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  bool first_is_greater;
  if (!rational_greater(&compressed_limit_1, &compressed_limit_2,
                        &first_is_greater)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  if (!av2_dm_rational_copy(&limit, first_is_greater ? &compressed_limit_2
                                                     : &compressed_limit_1)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  if (!av2_dm_rational_make(dfg->compressed_size, 1, &observed)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);

  if (!av2_dm_rational_make(8, (uint64_t)9 * limits->min_compression_basis,
                            &symbol_factor_a) ||
      !av2_dm_rational_make(1, 48, &symbol_factor_b) ||
      !av2_dm_rational_add(&symbol_factor_a, &symbol_factor_b,
                           &symbol_factor) ||
      !av2_dm_rational_multiply_u64(interval, limits->max_decode_rate,
                                    &limit) ||
      !av2_dm_rational_multiply_u64(&limit, limits->picture_size_profile_factor,
                                    &limit) ||
      !rational_multiply(&limit, &symbol_factor, &limit) ||
      !av2_dm_rational_make(dfg->frame_symbol_count, 1, &observed)) {
    arithmetic_failure(model);
    CLEANUP_FRAME_PARSING_RATIONALS();
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);
  CLEANUP_FRAME_PARSING_RATIONALS();
#undef CLEANUP_FRAME_PARSING_RATIONALS
}

static void check_previous_dfg_interval(Av2DecoderModel *model,
                                        Av2DmDfgRecord *previous,
                                        const Av2DmDfgRecord *current) {
  Av2DmRational interval = { 0 };
  if (!av2_dm_rational_subtract(&current->removal, &previous->removal,
                                &interval) ||
      !av2_dm_rational_divide_u64(&interval, previous->decode_count_two ? 2 : 1,
                                  &interval)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&interval);
    return;
  }
  if (!av2_dm_rational_copy(&model->last_frame_parsing_time, &interval)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&interval);
    return;
  }
  model->last_frame_parsing_time_valid = true;
  // The previous DFG retains the affected frame/generation identity; the
  // current DFG supplies the removal interval that proves these constraints.
  check_frame_parsing_constraints(model, previous, &interval,
                                  current->event_index);

  if (current->mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    Av2DmRational available = { 0 };
    Av2DmRational one_header_time = { 0 };
    Av2DmRational required = { 0 };
    const uint64_t max_headers = (uint64_t)previous->limits.max_header_rate *
                                 (1 + ((uint64_t)previous->tier << 1));
    if (!av2_dm_rational_subtract(&current->scheduled_removal,
                                  &previous->removal, &available) ||
        !av2_dm_rational_make(1, max_headers, &one_header_time) ||
        !rational_max(&previous->decode_time, &one_header_time, &required)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&available);
      av2_dm_rational_destroy(&one_header_time);
      av2_dm_rational_destroy(&required);
      av2_dm_rational_destroy(&interval);
      return;
    }
    Av2DmViolationDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.kind = AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME;
    detail.value.minimum_decode_time.frame_decode_time = previous->decode_time;
    detail.value.minimum_decode_time.one_header_time = one_header_time;
    compare_lower_limit_for_affected_with_detail(
        model, AV2_DM_VIOLATION_MINIMUM_DECODE_TIME, current->event_index,
        AV2_DM_VIOLATION_AFFECTED_DFG, previous->event_index, &available,
        &required, &detail);
    av2_dm_rational_destroy(&available);
    av2_dm_rational_destroy(&one_header_time);
    av2_dm_rational_destroy(&required);
  }
  av2_dm_rational_destroy(&interval);
}

static bool buffer_size_transition_applies(
    const Av2DmBufferSizeTransition *transition, const Av2DmRational *time,
    uint64_t dfg_number, bool after_own_removal, bool at_last_arrival,
    bool *applies) {
  int comparison;
  if (!av2_dm_rational_compare(&transition->time, time, &comparison)) {
    return false;
  }
  *applies =
      comparison < 0 ||
      (comparison == 0 &&
       (!transition->after_removal
            ? !at_last_arrival || transition->dfg_number <= dfg_number
            : transition->dfg_number < dfg_number ||
                  (transition->dfg_number == dfg_number && after_own_removal)));
  return true;
}

static bool buffer_size_at(Av2DecoderModel *model, const Av2DmRational *time,
                           uint64_t dfg_number, bool after_own_removal,
                           bool at_last_arrival, Av2DmRational *size) {
  if (!av2_dm_rational_copy(size, &model->buffer_size_base)) return false;
  for (uint32_t i = 0; i < model->buffer_size_transition_count; ++i) {
    const Av2DmBufferSizeTransition *const transition =
        &model->buffer_size_transitions[i];
    bool applies;
    if (!buffer_size_transition_applies(transition, time, dfg_number,
                                        after_own_removal, at_last_arrival,
                                        &applies)) {
      return false;
    }
    if (applies) {
      if (!av2_dm_rational_copy(size, &transition->size)) return false;
    }
  }
  return true;
}

static bool apply_buffer_size_transition_to_breakpoint(
    const Av2DmBufferSizeTransition *transition, const Av2DmRational *time,
    uint64_t dfg_number, bool after_own_removal, bool at_last_arrival,
    Av2DmRational *size) {
  bool applies;
  return buffer_size_transition_applies(transition, time, dfg_number,
                                        after_own_removal, at_last_arrival,
                                        &applies) &&
         (!applies || av2_dm_rational_copy(size, &transition->size));
}

static bool add_pending_buffer_size_transition(Av2DecoderModel *model,
                                               const Av2DmDfgRecord *dfg) {
  if (model->pending_buffer_size_change == 0) return true;
  if (model->buffer_size_transition_count == UINT32_MAX ||
      !grow_array((void **)&model->buffer_size_transitions,
                  &model->buffer_size_transition_capacity,
                  model->buffer_size_transition_count,
                  sizeof(*model->buffer_size_transitions))) {
    return false;
  }
  Av2DmBufferSizeTransition *transition =
      &model->buffer_size_transitions[model->buffer_size_transition_count];
  memset(transition, 0, sizeof(*transition));
  transition->after_removal = model->pending_buffer_size_change < 0;
  if (!av2_dm_rational_copy(&transition->time, transition->after_removal
                                                   ? &dfg->removal
                                                   : &dfg->first_arrival) ||
      !av2_dm_rational_copy(&transition->size, &model->pending_buffer_size)) {
    buffer_size_transition_destroy(transition);
    return false;
  }
  transition->dfg_number = model->dfg_number;
  ++model->buffer_size_transition_count;
  uint32_t write_index = 0;
  for (uint32_t i = 0; i + 1 < model->buffer_size_transition_count; ++i) {
    Av2DmBufferSizeTransition *const older = &model->buffer_size_transitions[i];
    int comparison;
    if (!av2_dm_rational_compare(&older->time, &transition->time,
                                 &comparison)) {
      return false;
    }
    // A later CVS value that becomes effective first supersedes an older
    // pending value. Equal-time changes remain in signaling order so their
    // before-arrival and after-removal phases remain distinct.
    if (comparison > 0) {
      buffer_size_transition_destroy(older);
      continue;
    }
    if (write_index != i) {
      buffer_size_transition_move(&model->buffer_size_transitions[write_index],
                                  older);
    }
    ++write_index;
  }
  if (write_index + 1 != model->buffer_size_transition_count) {
    buffer_size_transition_move(&model->buffer_size_transitions[write_index],
                                transition);
    model->buffer_size_transition_count = write_index + 1;
    transition = &model->buffer_size_transitions[write_index];
  }
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    Av2DmDfgRecord *const retained = &model->dfgs[i];
    const uint64_t retained_dfg_number = retained->decode_order + 1;
    if (!apply_buffer_size_transition_to_breakpoint(
            transition, &retained->last_arrival, retained_dfg_number, false,
            true, &retained->buffer_size_at_last_arrival) ||
        !apply_buffer_size_transition_to_breakpoint(
            transition, &retained->removal, retained_dfg_number, false, false,
            &retained->buffer_size_before_removal) ||
        !apply_buffer_size_transition_to_breakpoint(
            transition, &retained->removal, retained_dfg_number, true, false,
            &retained->buffer_size_after_removal)) {
      return false;
    }
  }
  model->pending_buffer_size_change = 0;
  return true;
}

static bool retire_buffer_size_transitions(Av2DecoderModel *model,
                                           const Av2DmDfgRecord *dfg) {
  Av2DmRational base = { 0 };
  if (!buffer_size_at(model, &dfg->last_arrival, model->dfg_number, true, false,
                      &base)) {
    return false;
  }
  uint32_t write_index = 0;
  for (uint32_t i = 0; i < model->buffer_size_transition_count; ++i) {
    Av2DmBufferSizeTransition *const transition =
        &model->buffer_size_transitions[i];
    int comparison;
    if (!av2_dm_rational_compare(&transition->time, &dfg->last_arrival,
                                 &comparison)) {
      av2_dm_rational_destroy(&base);
      return false;
    }
    if (comparison <= 0) {
      buffer_size_transition_destroy(transition);
      continue;
    }
    if (write_index != i) {
      buffer_size_transition_move(&model->buffer_size_transitions[write_index],
                                  transition);
    }
    ++write_index;
  }
  av2_dm_rational_move(&model->buffer_size_base, &base);
  model->buffer_size_transition_count = write_index;
  av2_dm_rational_destroy(&base);
  return true;
}

static bool calculate_arrival_times(Av2DecoderModel *model,
                                    Av2DmDfgRecord *dfg) {
  Av2DmRational total_delay = { 0 };
  Av2DmRational latest = { 0 };
  Av2DmRational coded_bits = { 0 };
  Av2DmRational reciprocal_rate = { 0 };
  Av2DmRational arrival_duration = { 0 };
  bool calculated = false;
  if (model->dfg_number == 1) {
    if (!rational_zero(&dfg->first_arrival)) goto cleanup;
  } else {
    if (!model->previous_dfg_valid) goto cleanup;
    const Av2DmRational *delay = &model->decoder_buffer_delay;
    if (!dfg->first_dfg_of_cvs) {
      if (!av2_dm_rational_add(&model->encoder_buffer_delay,
                               &model->decoder_buffer_delay, &total_delay)) {
        goto cleanup;
      }
      delay = &total_delay;
    }
    if (!av2_dm_rational_subtract(&dfg->scheduled_removal, delay, &latest) ||
        !rational_max(&model->previous_dfg.last_arrival, &latest,
                      &dfg->first_arrival)) {
      goto cleanup;
    }
  }
  if (av2_dm_rational_is_zero(&dfg->limits.bit_rate) ||
      !av2_dm_rational_make(dfg->coded_bits, 1, &coded_bits)) {
    goto cleanup;
  }
  calculated =
      rational_reciprocal(&dfg->limits.bit_rate, &reciprocal_rate) &&
      rational_multiply(&coded_bits, &reciprocal_rate, &arrival_duration) &&
      av2_dm_rational_add(&dfg->first_arrival, &arrival_duration,
                          &dfg->last_arrival);
cleanup:
  av2_dm_rational_destroy(&total_delay);
  av2_dm_rational_destroy(&latest);
  av2_dm_rational_destroy(&coded_bits);
  av2_dm_rational_destroy(&reciprocal_rate);
  av2_dm_rational_destroy(&arrival_duration);
  return calculated;
}

static bool calculate_scheduled_removal(Av2DecoderModel *model,
                                        const Av2DmFrameEvent *event,
                                        Av2DmDfgRecord *dfg) {
  if (model->config.mode == AV2_DM_RESOURCE_AVAILABILITY_MODE) {
    return next_resource_removal(model, &model->lane, model->dfg_number - 1,
                                 &dfg->scheduled_removal);
  }
  if (!event->buffer_removal_time_present) {
    missing_input(model);
    return false;
  }
  if (model->dfg_number == 1) {
    return av2_dm_rational_copy(&dfg->scheduled_removal,
                                &model->decoder_buffer_delay);
  }
  if (!model->most_recent_rap_removal_valid) {
    missing_input(model);
    return false;
  }
  Av2DmRational offset = { 0 };
  const bool calculated =
      av2_dm_rational_multiply_u64(&model->dec_ct, event->buffer_removal_time,
                                   &offset) &&
      av2_dm_rational_add(&model->most_recent_rap_scheduled_removal, &offset,
                          &dfg->scheduled_removal);
  av2_dm_rational_destroy(&offset);
  return calculated;
}

static void check_schedule_delay_limits(Av2DecoderModel *model,
                                        const Av2DmDfgRecord *dfg) {
  if (model->config.mode != AV2_DM_DECODING_SCHEDULE_MODE ||
      (model->dfg_number != 1 && !dfg->first_dfg_of_cvs)) {
    return;
  }
  Av2DmRational zero = { 0 };
  rational_zero(&zero);
  if (av2_dm_rational_is_zero(&model->decoder_buffer_delay)) {
    report_violation(model, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_ZERO,
                     dfg->event_index, &model->decoder_buffer_delay, &zero);
  }
  Av2DmRational reciprocal_rate = { 0 };
  Av2DmRational maximum_delay = { 0 };
  if (!rational_reciprocal(&model->limits.bit_rate, &reciprocal_rate) ||
      !rational_multiply(&model->limits.buffer_size, &reciprocal_rate,
                         &maximum_delay)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&zero);
    av2_dm_rational_destroy(&reciprocal_rate);
    av2_dm_rational_destroy(&maximum_delay);
    return;
  }
  compare_upper_limit(model, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_TOO_LARGE,
                      dfg->event_index, &model->decoder_buffer_delay,
                      &maximum_delay);
  av2_dm_rational_destroy(&zero);
  av2_dm_rational_destroy(&reciprocal_rate);
  av2_dm_rational_destroy(&maximum_delay);
}

static void check_delay_consistency(Av2DecoderModel *model,
                                    Av2DmDfgRecord *dfg) {
  if (!model->previous_dfg_valid ||
      (!dfg->first_dfg_of_cvs &&
       (model->config.mode != AV2_DM_DECODING_SCHEDULE_MODE ||
        !dfg->random_access_point))) {
    return;
  }
  Av2DmRational time_delta = { 0 };
  Av2DmRational delay = { 0 };
  Av2DmRational one = { 0 };
  Av2DmRational threshold = { 0 };
  if (!av2_dm_rational_subtract(&dfg->scheduled_removal,
                                &model->previous_dfg.last_arrival,
                                &time_delta) ||
      !av2_dm_rational_multiply_u64(&time_delta, 90000, &time_delta) ||
      !av2_dm_rational_make(model->decoder_buffer_delay_ticks, 1, &delay) ||
      !av2_dm_rational_make(1, 1, &one) ||
      !av2_dm_rational_subtract(&delay, &one, &threshold)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&time_delta);
    av2_dm_rational_destroy(&delay);
    av2_dm_rational_destroy(&one);
    av2_dm_rational_destroy(&threshold);
    return;
  }
  int comparison;
  if (!av2_dm_rational_compare(&time_delta, &threshold, &comparison)) {
    arithmetic_failure(model);
  } else if (comparison <= 0) {
    Av2DmViolationDetail detail;
    memset(&detail, 0, sizeof(detail));
    detail.kind = AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY;
    detail.value.delay_consistency.decoder_buffer_delay_ticks =
        model->decoder_buffer_delay_ticks;
    detail.value.delay_consistency.ceil_time_delta_present =
        rational_ceil_to_integer(
            &time_delta, &detail.value.delay_consistency.ceil_time_delta_ticks);
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT,
        dfg->event_index, AV2_DM_VIOLATION_AFFECTED_EVENT, dfg->event_index,
        &time_delta, &threshold, &detail);
    violation_detail_destroy(&detail);
  }
  av2_dm_rational_destroy(&time_delta);
  av2_dm_rational_destroy(&delay);
  av2_dm_rational_destroy(&one);
  av2_dm_rational_destroy(&threshold);
}

typedef struct Av2DmPreparedRebase {
  Av2DmRational **targets;
  Av2DmRational *values;
  uint32_t count;
} Av2DmPreparedRebase;

static bool add_rebase_target(Av2DmPreparedRebase *prepared,
                              Av2DmRational *target, uint32_t capacity) {
  if (prepared->count >= capacity) return false;
  prepared->targets[prepared->count] = target;
  if (!av2_dm_rational_copy(&prepared->values[prepared->count], target)) {
    return false;
  }
  ++prepared->count;
  return true;
}

static bool prepare_lane_rebase(Av2DecoderModel *model, Av2DmLane *lane,
                                bool primary, const Av2DmRational *origin,
                                Av2DmPreparedRebase *prepared) {
  internal_allocation_failed = false;
  uint64_t capacity = 2 + 2 * lane->pool.pool_size;
  if (primary) {
    capacity += (uint64_t)5 * model->dfg_count +
                model->buffer_size_transition_count + 16;
  }
  if (capacity > UINT32_MAX ||
      capacity > SIZE_MAX / sizeof(*prepared->values)) {
    return false;
  }
  prepared->targets =
      internal_calloc((size_t)capacity, sizeof(*prepared->targets));
  if (prepared->targets == NULL) return false;
  prepared->values =
      internal_calloc((size_t)capacity, sizeof(*prepared->values));
  if (prepared->values == NULL) return false;
  const uint32_t count_limit = (uint32_t)capacity;
  if (!add_rebase_target(prepared, &lane->time, count_limit) ||
      !add_rebase_target(prepared, &lane->initial_presentation_delay,
                         count_limit)) {
    return false;
  }
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (buffer->presentation_time_valid &&
        !add_rebase_target(prepared, &buffer->presentation_time, count_limit)) {
      return false;
    }
    if (buffer->decode_completion_time_valid &&
        !add_rebase_target(prepared, &buffer->decode_completion_time,
                           count_limit)) {
      return false;
    }
  }
  if (primary) {
    for (uint32_t i = 0; i < model->dfg_count; ++i) {
      Av2DmDfgRecord *const dfg = &model->dfgs[i];
      if (!add_rebase_target(prepared, &dfg->first_arrival, count_limit) ||
          !add_rebase_target(prepared, &dfg->last_arrival, count_limit) ||
          !add_rebase_target(prepared, &dfg->scheduled_removal, count_limit) ||
          !add_rebase_target(prepared, &dfg->removal, count_limit) ||
          !add_rebase_target(prepared, &dfg->decode_completion, count_limit)) {
        return false;
      }
    }
    if (model->previous_dfg_valid) {
      Av2DmDfgRecord *const dfg = &model->previous_dfg;
      if (!add_rebase_target(prepared, &dfg->first_arrival, count_limit) ||
          !add_rebase_target(prepared, &dfg->last_arrival, count_limit) ||
          !add_rebase_target(prepared, &dfg->scheduled_removal, count_limit) ||
          !add_rebase_target(prepared, &dfg->removal, count_limit) ||
          !add_rebase_target(prepared, &dfg->decode_completion, count_limit)) {
        return false;
      }
    }
    for (uint32_t i = 0; i < model->buffer_size_transition_count; ++i) {
      if (!add_rebase_target(prepared, &model->buffer_size_transitions[i].time,
                             count_limit)) {
        return false;
      }
    }
    if (model->most_recent_rap_removal_valid &&
        !add_rebase_target(prepared, &model->most_recent_rap_scheduled_removal,
                           count_limit)) {
      return false;
    }
    if (model->last_presentation_valid &&
        !add_rebase_target(prepared, &model->last_presentation, count_limit)) {
      return false;
    }
    if (model->pending_display_late.valid &&
        (!add_rebase_target(prepared, &model->pending_display_late.threshold,
                            count_limit) ||
         !add_rebase_target(prepared, &model->pending_display_late.observed,
                            count_limit))) {
      return false;
    }
    if (model->pending_decode_deadline.valid &&
        (!add_rebase_target(prepared, &model->pending_decode_deadline.threshold,
                            count_limit) ||
         !add_rebase_target(prepared, &model->pending_decode_deadline.observed,
                            count_limit))) {
      return false;
    }
  }
  return av2_dm_rational_rebase(prepared->values, prepared->count, origin);
}

static void free_prepared_rebase(Av2DmPreparedRebase *prepared) {
  for (uint32_t i = 0; i < prepared->count; ++i) {
    av2_dm_rational_destroy(&prepared->values[i]);
  }
  avm_free(prepared->targets);
  avm_free(prepared->values);
  memset(prepared, 0, sizeof(*prepared));
}

static void commit_prepared_rebase(Av2DmPreparedRebase *prepared) {
  for (uint32_t i = 0; i < prepared->count; ++i) {
    av2_dm_rational_move(prepared->targets[i], &prepared->values[i]);
  }
}

static void maybe_rebase_model(Av2DecoderModel *model) {
  const uint32_t interval = model->config.rebase_interval_events == 0
                                ? 4096
                                : model->config.rebase_interval_events;
  if (model->model_events == 0 || model->model_events % interval != 0 ||
      !model->lane.initial_presentation_delay_known ||
      !model->resource_lane.initial_presentation_delay_known) {
    return;
  }
  Av2DmPreparedRebase primary = { 0 };
  Av2DmPreparedRebase resource = { 0 };
  // Both lanes participate in the Annex E schedule-vs-resource ordering
  // comparison, so every absolute lane time must retain one shared origin.
  const Av2DmRational *const origin = &model->lane.time;
  if (!prepare_lane_rebase(model, &model->lane, true, origin, &primary) ||
      !prepare_lane_rebase(model, &model->resource_lane, false, origin,
                           &resource)) {
    free_prepared_rebase(&primary);
    free_prepared_rebase(&resource);
    arithmetic_failure(model);
    return;
  }
  commit_prepared_rebase(&primary);
  commit_prepared_rebase(&resource);
  free_prepared_rebase(&primary);
  free_prepared_rebase(&resource);
}

static bool lane_buffer_is_live(const Av2DmLane *lane, uint32_t buffer_index) {
  const Av2DmBuffer *const buffer = &lane->pool.buffers[buffer_index];
  return buffer->generation_valid &&
         (lane->current_buffer_index == (int32_t)buffer_index ||
          buffer->decoder_ref_count != 0 || buffer->player_ref_count != 0);
}

static bool earlier_lane_has_generation(const Av2DmLane *const lanes[2],
                                        uint32_t lane_index,
                                        uint32_t buffer_index,
                                        uint64_t generation) {
  for (uint32_t i = 0; i <= lane_index; ++i) {
    const uint32_t limit =
        i == lane_index ? buffer_index : lanes[i]->pool.pool_size;
    for (uint32_t j = 0; j < limit; ++j) {
      const Av2DmBuffer *const buffer = &lanes[i]->pool.buffers[j];
      if (lane_buffer_is_live(lanes[i], j) &&
          buffer->generation == generation) {
        return true;
      }
    }
  }
  return false;
}

static uint32_t active_generation_count(const Av2DecoderModel *model) {
  const Av2DmLane *const lanes[2] = { &model->lane, &model->resource_lane };
  uint32_t count = 0;
  for (uint32_t i = 0; i < 2; ++i) {
    for (uint32_t j = 0; j < lanes[i]->pool.pool_size; ++j) {
      const Av2DmBuffer *const buffer = &lanes[i]->pool.buffers[j];
      if (lane_buffer_is_live(lanes[i], j) &&
          !earlier_lane_has_generation(lanes, i, j, buffer->generation)) {
        ++count;
      }
    }
  }
  return count;
}

static void update_storage_high_water(uint32_t active, uint32_t *current,
                                      uint32_t *high_water) {
  *current = active;
  if (active > *high_water) *high_water = active;
}

static void update_storage_stats(Av2DecoderModel *model) {
  if (model->result.finished) {
    model->storage.active_dfgs = 0;
    model->storage.active_outputs = 0;
    model->storage.active_tus = 0;
    model->storage.active_generations = 0;
    model->storage.active_cvs = 0;
    model->storage.active_rap_runs = 0;
    return;
  }
  const uint32_t active_outputs =
      (uint32_t)model->pending_display_late.valid +
      (uint32_t)model->pending_decode_deadline.valid;
  const uint32_t active_run =
      !model->result.finished &&
              model->result.applicability == AV2_DM_APPLICABLE
          ? 1
          : 0;
  uint32_t active_dfgs = model->dfg_count;
  if (model->previous_dfg_valid && active_dfgs != UINT32_MAX) ++active_dfgs;
  update_storage_high_water(active_dfgs, &model->storage.active_dfgs,
                            &model->storage.high_water_dfgs);
  update_storage_high_water(active_outputs, &model->storage.active_outputs,
                            &model->storage.high_water_outputs);
  update_storage_high_water(model->tu_count, &model->storage.active_tus,
                            &model->storage.high_water_tus);
  update_storage_high_water(active_generation_count(model),
                            &model->storage.active_generations,
                            &model->storage.high_water_generations);
  update_storage_high_water(active_run, &model->storage.active_cvs,
                            &model->storage.high_water_cvs);
  update_storage_high_water(active_run, &model->storage.active_rap_runs,
                            &model->storage.high_water_rap_runs);
}

static void model_event_complete(Av2DecoderModel *model) {
  if (!increment_model_u64(model, &model->model_events)) return;
  maybe_rebase_model(model);
  update_storage_stats(model);
}

static void decoder_model_start_frame_internal(Av2DecoderModel *model,
                                               const Av2DmFrameEvent *event) {
  if (model == NULL || event == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  if (model->frame_number == UINT64_MAX) {
    arithmetic_failure(model);
    return;
  }
  const bool first_dfg_of_cvs =
      model->dfg_number != 0 && event->coded_as_closed_loop_key &&
      (!model->previous_dfg_valid ||
       model->previous_dfg.temporal_unit_index != event->temporal_unit_index);
  model->latest_frame_event_index = event->event_index;
  ++model->frame_number;
  if (model->coded_tu_valid && model->coded_tu != event->temporal_unit_index) {
    Av2DmTuRecord *const previous_coded = find_tu(model, model->coded_tu);
    if (previous_coded == NULL) {
      arithmetic_failure(model);
      return;
    }
    previous_coded->header_complete = true;
  }
  if (first_dfg_of_cvs) {
    const bool old_cvs_already_finalized = model->tile_cvs_finalized;
    finalize_tile_cvs(model, event->event_index);
    if (model->processing_stopped) return;
    if (model->config.defer_nonterminal_checks_for_testing &&
        !old_cvs_already_finalized) {
      check_max_reference_frames(model, event->event_index);
      if (model->processing_stopped) return;
    }
    model->any_decode_count_two_requires_reserved_buffer = false;
    model->max_reference_frames_checked = false;
    model->max_reference_frames_reserved = false;
    model->max_reference_frames_violated = false;
    model->maximum_tile_area = 0;
    model->retired_header_summary_valid = false;
    model->retired_header_summary_reported = false;
    model->retired_max_frame_headers = 0;
    model->retired_header_event_index = 0;
    model->retired_header_limit = 0;
    model->tile_cvs_finalized = false;
    if (model->cvs_number == UINT64_MAX) {
      arithmetic_failure(model);
      return;
    }
    ++model->cvs_number;
  }
  model->coded_tu = event->temporal_unit_index;
  model->coded_tu_valid = true;
  Av2DmTuRecord *const tu =
      get_tu(model, event->temporal_unit_index, event->event_index);
  if (tu != NULL && event->temporal_unit_output_time_present) {
    if (!av2_dm_rational_copy(&tu->output_time,
                              &event->temporal_unit_output_time)) {
      arithmetic_failure(model);
      return;
    }
    tu->output_time_valid = true;
    if (!update_latest_timed_tu(model, &tu->output_time)) {
      arithmetic_failure(model);
      return;
    }
  }
  if (tu != NULL && event->count_frame_header) {
    if (tu->frame_headers == UINT32_MAX) {
      arithmetic_failure(model);
      return;
    }
    ++tu->frame_headers;
  }
  if (event->show_existing_frame) {
    if (!model->config.defer_nonterminal_checks_for_testing) {
      check_header_rate_windows(model, false, event->event_index);
    }
    retire_unresolvable_tus(model);
    model_event_complete(model);
    return;
  }
  if (event->max_tile_area > model->maximum_tile_area) {
    // Annex A MaxTileSizeInLumaSamples covers every tile in the coded video
    // sequence, independently of CountFrameHeaderForLevelConstraint.
    model->maximum_tile_area = event->max_tile_area;
    check_retired_tile_header_summary(model, event->event_index);
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_header_rate_windows(model, false, event->event_index);
  }
  if (model->processing_stopped) return;
  if (model->dfg_count == UINT32_MAX || model->dfg_number == UINT64_MAX ||
      (event->random_access_point && model->rap_epoch == UINT64_MAX) ||
      !grow_array((void **)&model->dfgs, &model->dfg_capacity, model->dfg_count,
                  sizeof(*model->dfgs))) {
    arithmetic_failure(model);
    return;
  }
  Av2DmDfgRecord *const dfg = &model->dfgs[model->dfg_count++];
  dfg_record_init(dfg);
  dfg->event_index = event->event_index;
  dfg->temporal_unit_index = event->temporal_unit_index;
  dfg->generation = event->generation;
  dfg->coded_bits = event->coded_bits;
  dfg->decode_order = model->result.decoded_frames;
  if (!av2_dm_level_limits_copy(&dfg->limits, &model->limits)) {
    dfg_record_destroy(dfg);
    --model->dfg_count;
    arithmetic_failure(model);
    return;
  }
  dfg->tier = model->config.tier;
  dfg->mode = model->config.mode;
  dfg->random_access_point = event->random_access_point;
  dfg->parameters_updated = event->decoder_model_parameters_updated;
  dfg->count_frame_header = event->count_frame_header;
  dfg->decode_count_two =
      event->allow_global_intrabc && event->inloop_filtering_enabled;
  dfg->coded_as_closed_loop_key = event->coded_as_closed_loop_key;
  dfg->first_dfg_of_cvs = first_dfg_of_cvs;
  dfg->still_picture = model->config.still_picture;
  dfg->num_tiles = event->num_tiles;
  dfg->max_tile_area = event->max_tile_area;
  dfg->compressed_size = event->compressed_size_bytes > 128
                             ? event->compressed_size_bytes - 128
                             : 0;
  dfg->frame_symbol_count = event->frame_symbol_count;
  if (event->random_access_point) ++model->rap_epoch;
  dfg->rap_epoch = model->rap_epoch;
  ++model->dfg_number;

  check_static_level_limits(model, event);
  if (model->processing_stopped) return;
  uint64_t decode_luma_samples;
  if (!calculate_decode_time(model, event, &decode_luma_samples,
                             &dfg->decode_time)) {
    arithmetic_failure(model);
    return;
  }
  dfg->luma_samples =
      dfg->decode_count_two ? decode_luma_samples / 2 : decode_luma_samples;
  if (dfg->decode_count_two &&
      (model->config.max_mlayer_id != 0 || !dfg->coded_as_closed_loop_key)) {
    model->any_decode_count_two_requires_reserved_buffer = true;
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_max_reference_frames(model, event->event_index);
  }
  if (model->processing_stopped) return;
  if (!calculate_scheduled_removal(model, event, dfg) ||
      !calculate_arrival_times(model, dfg)) {
    if (!model->result.missing_required_input) arithmetic_failure(model);
    return;
  }
  if (!av2_dm_rational_copy(&dfg->removal, &dfg->scheduled_removal)) {
    arithmetic_failure(model);
    return;
  }
  bool scheduled_before_arrival;
  if (!rational_less(&dfg->scheduled_removal, &dfg->last_arrival,
                     &scheduled_before_arrival)) {
    arithmetic_failure(model);
    return;
  }
  if (scheduled_before_arrival && !model->low_delay_mode) {
    // DM-SPEC-2: availability at the scheduled time is required only in
    // strict-arrival mode.
    report_violation(model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW,
                     event->event_index, &dfg->scheduled_removal,
                     &dfg->last_arrival);
  } else if (scheduled_before_arrival &&
             model->config.mode == AV2_DM_DECODING_SCHEDULE_MODE &&
             !rational_ceil_from_anchor(&dfg->last_arrival,
                                        &dfg->scheduled_removal, &model->dec_ct,
                                        &dfg->removal)) {
    arithmetic_failure(model);
    return;
  }
  dfg->buffer_size_decreases_after_removal =
      model->pending_buffer_size_change < 0;
  if (!add_pending_buffer_size_transition(model, dfg) ||
      !buffer_size_at(model, &dfg->last_arrival, model->dfg_number, false, true,
                      &dfg->buffer_size_at_last_arrival) ||
      !buffer_size_at(model, &dfg->removal, model->dfg_number, false, false,
                      &dfg->buffer_size_before_removal) ||
      !buffer_size_at(model, &dfg->removal, model->dfg_number, true, false,
                      &dfg->buffer_size_after_removal)) {
    arithmetic_failure(model);
    return;
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_smoothing_buffer_overflow(model, &dfg->last_arrival,
                                    event->event_index);
  }
  if (model->processing_stopped) return;
  if (!retire_buffer_size_transitions(model, dfg)) {
    arithmetic_failure(model);
    return;
  }

  Av2DmRational resource_removal = { 0 };
  if (!next_resource_removal(model, &model->resource_lane,
                             model->dfg_number - 1, &resource_removal)) {
    if (!model->result.missing_required_input) arithmetic_failure(model);
    av2_dm_rational_destroy(&resource_removal);
    return;
  }
  int32_t resource_buffer_index;
  if (!lane_start_decode(&model->resource_lane, &resource_removal,
                         &dfg->decode_time, event->generation,
                         &resource_buffer_index)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&resource_removal);
    return;
  }
  if (resource_buffer_index < 0 &&
      model->config.mode == AV2_DM_RESOURCE_AVAILABILITY_MODE) {
    const Av2DmViolationDetail detail =
        buffer_pool_violation_detail(&model->resource_lane.pool, true);
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE,
        event->event_index, AV2_DM_VIOLATION_AFFECTED_EVENT, event->event_index,
        NULL, NULL, &detail);
  }

  int32_t buffer_index;
  if (!lane_start_decode(&model->lane, &dfg->removal, &dfg->decode_time,
                         event->generation, &buffer_index)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&resource_removal);
    return;
  }
  if (buffer_index < 0) {
    const Av2DmViolationDetail detail =
        buffer_pool_violation_detail(&model->lane.pool, false);
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE,
        event->event_index, AV2_DM_VIOLATION_AFFECTED_EVENT, event->event_index,
        NULL, NULL, &detail);
  }
  if (!av2_dm_rational_copy(&dfg->decode_completion, &model->lane.time)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&resource_removal);
    return;
  }
  if (buffer_index >= 0) {
    Av2DmBuffer *const buffer = &model->lane.pool.buffers[buffer_index];
    buffer->decode_order = dfg->decode_order;
    buffer->rap_epoch = dfg->rap_epoch;
    buffer->random_access_point = dfg->random_access_point;
    buffer->coded_temporal_unit_index = dfg->temporal_unit_index;
    buffer->coded_temporal_unit_valid = true;
    buffer->equal_picture_interval = model->config.equal_picture_interval;
    buffer->ticks_per_picture = model->config.ticks_per_picture;
    if (!av2_dm_rational_copy(&buffer->disp_ct, &model->disp_ct)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&resource_removal);
      return;
    }
  }
  if (resource_buffer_index >= 0) {
    Av2DmBuffer *const buffer =
        &model->resource_lane.pool.buffers[resource_buffer_index];
    buffer->decode_order = dfg->decode_order;
    buffer->rap_epoch = dfg->rap_epoch;
    buffer->random_access_point = dfg->random_access_point;
    buffer->coded_temporal_unit_index = dfg->temporal_unit_index;
    buffer->coded_temporal_unit_valid = true;
    buffer->equal_picture_interval = model->config.equal_picture_interval;
    buffer->ticks_per_picture = model->config.ticks_per_picture;
    if (!av2_dm_rational_copy(&buffer->disp_ct, &model->disp_ct)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&resource_removal);
      return;
    }
  }

  if (model->config.mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    compare_lower_limit(
        model, AV2_DM_VIOLATION_SCHEDULE_BEFORE_RESOURCE_REMOVAL,
        event->event_index, &dfg->scheduled_removal, &resource_removal);
  }
  if (model->previous_dfg_valid) {
    check_previous_dfg_interval(model, &model->previous_dfg, dfg);
  }
  check_schedule_delay_limits(model, dfg);
  check_delay_consistency(model, dfg);

  if (model->dfg_number == 1 || event->random_access_point) {
    if (!av2_dm_rational_copy(&model->most_recent_rap_scheduled_removal,
                              &dfg->scheduled_removal)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&resource_removal);
      return;
    }
    model->most_recent_rap_removal_valid = true;
  }
  if (!dfg_record_copy(&model->previous_dfg, dfg)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&resource_removal);
    return;
  }
  model->previous_dfg_valid = true;
  if (!model->config.defer_nonterminal_checks_for_testing) {
    if (violation_seen(model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW)) {
      // The current DFG must remain live until all start-frame processing that
      // reads it has completed. Once overflow is proven, no retained fullness
      // record can change the CVS verdict.
      for (uint32_t i = 0; i < model->dfg_count; ++i) {
        dfg_record_destroy(&model->dfgs[i]);
      }
      model->dfg_count = 0;
    } else {
      retire_closed_smoothing_records(model, &dfg->last_arrival);
    }
  }
  retire_unresolvable_tus(model);
  av2_dm_rational_destroy(&resource_removal);
  if (!increment_model_u64(model, &model->result.decoded_frames)) return;
  model_event_complete(model);
  update_result_status(model);
}

void av2_decoder_model_start_frame(Av2DecoderModel *model,
                                   const Av2DmFrameEvent *event) {
  if (model == NULL || event == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  decoder_model_start_frame_internal(model, event);
  end_model_transaction(model, &transaction);
}

static bool update_lane_reference_buffers(
    Av2DmLane *lane, const Av2DmReferenceUpdateEvent *event) {
  for (uint32_t i = 0; i < lane->pool.num_ref_frames; ++i) {
    if (((event->refresh_frame_flags >> i) & 1) == 0) continue;
    int32_t buffer_index = -1;
    if (((event->ref_valid_mask >> i) & 1) != 0) {
      buffer_index = lane->current_buffer_index;
    }
    if (!av2_dm_buffer_pool_set_vbi(&lane->pool, i, buffer_index)) {
      return false;
    }
  }
  return true;
}

void av2_decoder_model_update_reference_buffers(
    Av2DecoderModel *model, const Av2DmReferenceUpdateEvent *event) {
  if (model == NULL || event == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  if (model->shown_frame_number == UINT64_MAX ||
      model->result.output_frames == UINT64_MAX) {
    arithmetic_failure(model);
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  if (!update_lane_reference_buffers(&model->lane, event) ||
      !update_lane_reference_buffers(&model->resource_lane, event)) {
    arithmetic_failure(model);
  }
  retire_unresolvable_tus(model);
  model_event_complete(model);
  end_model_transaction(model, &transaction);
}

static bool invalidate_lane_reference_buffers(Av2DmLane *lane,
                                              uint32_t ref_valid_mask,
                                              bool closed_loop_key) {
  const uint32_t limit = lane->pool.num_ref_frames;
  for (uint32_t i = 0; i < limit; ++i) {
    if ((closed_loop_key || ((ref_valid_mask >> i) & 1) == 0) &&
        lane->pool.vbi[i] != -1 &&
        !av2_dm_buffer_pool_set_vbi(&lane->pool, i, -1)) {
      return false;
    }
  }
  return true;
}

void av2_decoder_model_invalidate_reference_buffers(Av2DecoderModel *model,
                                                    uint32_t ref_valid_mask,
                                                    bool closed_loop_key) {
  if (model == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  // Annex E invalidate_ref_buffers() operates on the active VBI range.
  if (!invalidate_lane_reference_buffers(&model->lane, ref_valid_mask,
                                         closed_loop_key) ||
      !invalidate_lane_reference_buffers(&model->resource_lane, ref_valid_mask,
                                         closed_loop_key)) {
    arithmetic_failure(model);
  }
  model_event_complete(model);
  end_model_transaction(model, &transaction);
}

static void complete_output_checks(Av2DecoderModel *model,
                                   uint64_t affected_event_index,
                                   uint64_t proving_event_index,
                                   const Av2DmRational *output_time,
                                   const Av2DmRational *decode_completion,
                                   const Av2DmRational *presentation) {
  bool late;
  if (!rational_greater(output_time, presentation, &late)) {
    arithmetic_failure(model);
    return;
  }
  if (late) {
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_OUTPUT, affected_event_index, output_time,
        presentation, NULL);
  }
  if (decode_completion == NULL) return;
  bool missed_deadline;
  if (!rational_greater(decode_completion, presentation, &missed_deadline)) {
    arithmetic_failure(model);
    return;
  }
  if (missed_deadline) {
    // DM-SPEC-3 associates the three times by decoded generation, not by
    // their positions in the decode- and presentation-order arrays.
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_DECODE_DEADLINE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_OUTPUT, affected_event_index,
        decode_completion, presentation, NULL);
  }
}

static bool update_pending_output_witness(
    Av2DmPendingOutputWitness *pending, uint64_t event_index,
    const Av2DmRational *observed, const Av2DmRational *presentation_offset) {
  Av2DmRational threshold = { 0 };
  if (!av2_dm_rational_subtract(observed, presentation_offset, &threshold)) {
    av2_dm_rational_destroy(&threshold);
    return false;
  }
  if (pending->valid) {
    int comparison;
    if (!av2_dm_rational_compare(&threshold, &pending->threshold,
                                 &comparison)) {
      av2_dm_rational_destroy(&threshold);
      return false;
    }
    if (comparison <= 0) {
      av2_dm_rational_destroy(&threshold);
      return true;
    }
  }
  Av2DmRational copied_observed = { 0 };
  Av2DmRational copied_offset = { 0 };
  if (!av2_dm_rational_copy(&copied_observed, observed) ||
      !av2_dm_rational_copy(&copied_offset, presentation_offset)) {
    av2_dm_rational_destroy(&threshold);
    av2_dm_rational_destroy(&copied_observed);
    av2_dm_rational_destroy(&copied_offset);
    return false;
  }
  pending->valid = true;
  pending->event_index = event_index;
  av2_dm_rational_move(&pending->threshold, &threshold);
  av2_dm_rational_move(&pending->observed, &copied_observed);
  av2_dm_rational_move(&pending->presentation_offset, &copied_offset);
  av2_dm_rational_destroy(&threshold);
  av2_dm_rational_destroy(&copied_observed);
  av2_dm_rational_destroy(&copied_offset);
  return true;
}

static bool complete_pending_output_check(Av2DecoderModel *model,
                                          Av2DmPendingOutputWitness *pending,
                                          Av2DmViolationCode code,
                                          const Av2DmRational *initial_delay,
                                          uint64_t proving_event_index) {
  if (!pending->valid) return true;
  bool violated;
  if (!rational_greater(&pending->threshold, initial_delay, &violated)) {
    return false;
  }
  if (violated) {
    Av2DmRational presentation = { 0 };
    if (!av2_dm_rational_add(&pending->presentation_offset, initial_delay,
                             &presentation)) {
      av2_dm_rational_destroy(&presentation);
      return false;
    }
    report_violation_for_affected(
        model, code, proving_event_index, AV2_DM_VIOLATION_AFFECTED_OUTPUT,
        pending->event_index, &pending->observed, &presentation, NULL);
    av2_dm_rational_destroy(&presentation);
  }
  pending->valid = false;
  av2_dm_rational_destroy(&pending->threshold);
  av2_dm_rational_destroy(&pending->observed);
  av2_dm_rational_destroy(&pending->presentation_offset);
  return true;
}

static bool set_lane_initial_presentation_delay(Av2DecoderModel *model,
                                                Av2DmLane *lane,
                                                bool primary_lane,
                                                bool end_of_bitstream,
                                                uint64_t proving_event_index) {
  if (lane->initial_presentation_delay_known ||
      (!end_of_bitstream && av2_dm_buffer_pool_frames_in_use(&lane->pool) <
                                model->config.initial_display_delay)) {
    return true;
  }
  if (!av2_dm_rational_copy(&lane->initial_presentation_delay, &lane->time)) {
    return false;
  }
  lane->initial_presentation_delay_known = true;
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (buffer->player_ref_count != 0 && !buffer->presentation_time_valid) {
      if (!av2_dm_rational_add(&buffer->presentation_time,
                               &lane->initial_presentation_delay,
                               &buffer->presentation_time)) {
        return false;
      }
      buffer->presentation_time_valid = true;
    }
  }
  if (primary_lane &&
      (!complete_pending_output_check(model, &model->pending_display_late,
                                      AV2_DM_VIOLATION_DISPLAY_FRAME_LATE,
                                      &lane->initial_presentation_delay,
                                      proving_event_index) ||
       !complete_pending_output_check(model, &model->pending_decode_deadline,
                                      AV2_DM_VIOLATION_DECODE_DEADLINE,
                                      &lane->initial_presentation_delay,
                                      proving_event_index))) {
    return false;
  }
  if (primary_lane && model->last_presentation_offset_valid) {
    if (!av2_dm_rational_add(&model->last_presentation_offset,
                             &lane->initial_presentation_delay,
                             &model->last_presentation)) {
      return false;
    }
    model->last_presentation_valid = true;
  }
  return true;
}

static void decoder_model_set_initial_presentation_delay_internal(
    Av2DecoderModel *model, bool end_of_bitstream, uint64_t event_index) {
  if (model == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  if (!set_lane_initial_presentation_delay(model, &model->lane, true,
                                           end_of_bitstream, event_index) ||
      !set_lane_initial_presentation_delay(model, &model->resource_lane, false,
                                           end_of_bitstream, event_index)) {
    arithmetic_failure(model);
  }
  model_event_complete(model);
}

void av2_decoder_model_set_initial_presentation_delay(Av2DecoderModel *model,
                                                      bool end_of_bitstream,
                                                      uint64_t event_index) {
  if (model == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  decoder_model_set_initial_presentation_delay_internal(model, end_of_bitstream,
                                                        event_index);
  end_model_transaction(model, &transaction);
}

static void check_tu_display_rate(Av2DecoderModel *model, Av2DmTuRecord *tu,
                                  const Av2DmRational *duration,
                                  uint64_t proving_event_index) {
  if (tu->still_picture) return;
  Av2DmRational observed = { 0 };
  Av2DmRational capacity = { 0 };
  if (!av2_dm_rational_multiply_u64(duration, tu->limits.max_display_rate,
                                    &capacity) ||
      !av2_dm_rational_make(tu->output_luma_samples, 1, &observed)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&capacity);
    return;
  }
  compare_upper_limit_for_affected(
      model, AV2_DM_VIOLATION_MAX_DISPLAY_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, tu->temporal_unit_index,
      &observed, &capacity);
  av2_dm_rational_destroy(&observed);
  av2_dm_rational_destroy(&capacity);
}

static void check_tu_minimum_presentation_interval(
    Av2DecoderModel *model, Av2DmTuRecord *tu, const Av2DmRational *interval,
    uint64_t proving_event_index) {
  if (tu->still_picture) return;
  Av2DmRational limit = { 0 };
  const uint64_t max_headers =
      (uint64_t)tu->limits.max_header_rate * (1 + ((uint64_t)tu->tier << 1));
  Av2DmRational sample_interval = { 0 };
  Av2DmRational min_frame_time = { 0 };
  if (!rational_from_product(tu->max_frame_width, tu->max_frame_height,
                             &sample_interval) ||
      !av2_dm_rational_multiply_u64(&sample_interval, tu->output_frames,
                                    &sample_interval) ||
      !av2_dm_rational_divide_u64(&sample_interval, tu->limits.max_display_rate,
                                  &sample_interval) ||
      !av2_dm_rational_make(tu->limits.max_decode_rate,
                            tu->limits.max_display_rate, &min_frame_time) ||
      !av2_dm_rational_divide_u64(&min_frame_time, max_headers,
                                  &min_frame_time) ||
      !rational_max(&sample_interval, &min_frame_time, &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&limit);
    av2_dm_rational_destroy(&sample_interval);
    av2_dm_rational_destroy(&min_frame_time);
    return;
  }
  compare_lower_limit_for_affected(
      model, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL,
      proving_event_index, AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT,
      tu->temporal_unit_index, interval, &limit);
  av2_dm_rational_destroy(&limit);
  av2_dm_rational_destroy(&sample_interval);
  av2_dm_rational_destroy(&min_frame_time);
}

static void update_tu_for_output(Av2DecoderModel *model,
                                 const Av2DmOutputEvent *event,
                                 const Av2DmRational *presentation_offset) {
  Av2DmTuRecord *const tu =
      get_tu(model, event->temporal_unit_index, event->event_index);
  if (tu == NULL) return;
  bool output_time_regressed = false;
  if (UINT64_MAX - tu->output_luma_samples < event->output_luma_samples ||
      tu->output_frames == UINT32_MAX) {
    arithmetic_failure(model);
    return;
  }
  if (tu->output_frames == 0 &&
      !increment_model_u64(model, &model->output_tu_count)) {
    return;
  }
  tu->output_luma_samples += event->output_luma_samples;
  ++tu->output_frames;
  if (!tu->presentation_time_valid) {
    if (!av2_dm_rational_copy(&tu->presentation_time, presentation_offset)) {
      arithmetic_failure(model);
      return;
    }
    tu->presentation_time_valid = true;
  }
  if (!tu->output_time_valid) {
    // When no external TU output time was supplied, the first actual output
    // event establishes the TU output time in display order. Coding-order TU
    // indices are identifiers and are not timestamps.
    if (!av2_dm_rational_copy(&tu->output_time, presentation_offset)) {
      arithmetic_failure(model);
      return;
    }
    tu->output_time_valid = true;
  }
  if (!update_latest_timed_tu(model, &tu->output_time)) {
    arithmetic_failure(model);
    return;
  }
  if (model->last_output_tu_valid &&
      model->last_output_tu != tu->temporal_unit_index) {
    Av2DmTuRecord *const previous = find_tu(model, model->last_output_tu);
    if (previous == NULL) {
      arithmetic_failure(model);
      return;
    }
    if (previous->presentation_time_valid) {
      Av2DmRational presentation_interval = { 0 };
      if (!av2_dm_rational_subtract(presentation_offset,
                                    &previous->presentation_time,
                                    &presentation_interval)) {
        arithmetic_failure(model);
        av2_dm_rational_destroy(&presentation_interval);
        return;
      }
      check_tu_minimum_presentation_interval(
          model, previous, &presentation_interval, event->event_index);
      previous->prior_presentation_interval_checked = true;
      av2_dm_rational_destroy(&presentation_interval);
    }
    if (previous->output_time_valid && tu->output_time_valid) {
      int ordering;
      if (!av2_dm_rational_compare(&tu->output_time, &previous->output_time,
                                   &ordering)) {
        arithmetic_failure(model);
        return;
      }
      output_time_regressed = ordering <= 0;
      Av2DmRational display_duration = { 0 };
      if (!av2_dm_rational_subtract(&tu->output_time, &previous->output_time,
                                    &display_duration)) {
        arithmetic_failure(model);
        av2_dm_rational_destroy(&display_duration);
        return;
      }
      check_tu_display_rate(model, previous, &display_duration,
                            event->event_index);
      if (!av2_dm_rational_copy(&model->last_display_duration,
                                &display_duration)) {
        arithmetic_failure(model);
        av2_dm_rational_destroy(&display_duration);
        return;
      }
      model->last_display_duration_valid = true;
      av2_dm_rational_destroy(&display_duration);
    }
  }
  model->last_output_tu = tu->temporal_unit_index;
  model->last_output_tu_valid = true;
  if (output_time_regressed &&
      (violation_seen(model, AV2_DM_VIOLATION_MAX_DISPLAY_RATE) ||
       violation_seen(model, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL) ||
       violation_seen(model, AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE))) {
    // A non-increasing output timeline has already proven conformance failure.
    // Start a new bounded rate-window segment while retaining any generations
    // that can still be output and checked for other violation classes.
    restart_tu_history(model, tu->temporal_unit_index);
  }
}

static const Av2DmRapPresentationAnchor *find_rap_presentation_anchor(
    const Av2DecoderModel *model, uint64_t rap_epoch) {
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    if (model->rap_presentation_anchors[i].valid &&
        model->rap_presentation_anchors[i].rap_epoch == rap_epoch) {
      return &model->rap_presentation_anchors[i];
    }
  }
  return NULL;
}

static void store_rap_presentation_anchor(Av2DecoderModel *model,
                                          uint64_t rap_epoch,
                                          const Av2DmRational *offset) {
  Av2DmRapPresentationAnchor *free_anchor = NULL;
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
    if (model->rap_presentation_anchors[i].valid &&
        model->rap_presentation_anchors[i].rap_epoch == rap_epoch) {
      if (!av2_dm_rational_copy(
              &model->rap_presentation_anchors[i].presentation_offset,
              offset)) {
        arithmetic_failure(model);
      }
      return;
    }
    if (!model->rap_presentation_anchors[i].valid && free_anchor == NULL) {
      free_anchor = &model->rap_presentation_anchors[i];
    }
  }
  if (free_anchor == NULL) {
    // At most one anchor is needed per DPB generation epoch, plus the current
    // and immediately preceding RAP. Reclaim an epoch that no live generation
    // can present again before treating exhaustion as an internal failure.
    for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE + 2; ++i) {
      Av2DmRapPresentationAnchor *const candidate =
          &model->rap_presentation_anchors[i];
      bool live = candidate->rap_epoch == model->rap_epoch ||
                  (model->rap_epoch != 0 &&
                   candidate->rap_epoch == model->rap_epoch - 1);
      for (uint32_t j = 0; j < model->lane.pool.pool_size && !live; ++j) {
        const Av2DmBuffer *const buffer = &model->lane.pool.buffers[j];
        live = buffer->generation_valid &&
               buffer->rap_epoch == candidate->rap_epoch;
      }
      if (!live) {
        free_anchor = candidate;
        break;
      }
    }
  }
  if (free_anchor == NULL) {
    arithmetic_failure(model);
    return;
  }
  if (!av2_dm_rational_copy(&free_anchor->presentation_offset, offset)) {
    arithmetic_failure(model);
    return;
  }
  free_anchor->valid = true;
  free_anchor->rap_epoch = rap_epoch;
}

static bool calculate_presentation_offset(Av2DecoderModel *model,
                                          const Av2DmOutputEvent *event,
                                          const Av2DmBuffer *buffer,
                                          Av2DmRational *offset) {
  if (buffer->equal_picture_interval) {
    if (!model->last_presentation_offset_valid) return rational_zero(offset);
    if (event->temporal_unit_index == model->last_output_temporal_unit) {
      return av2_dm_rational_copy(offset, &model->last_presentation_offset);
    }
    Av2DmRational increment = { 0 };
    const bool calculated =
        av2_dm_rational_multiply_u64(&buffer->disp_ct,
                                     buffer->ticks_per_picture, &increment) &&
        av2_dm_rational_add(&model->last_presentation_offset, &increment,
                            offset);
    av2_dm_rational_destroy(&increment);
    return calculated;
  }
  if (!event->presentation_time_present) {
    missing_input(model);
    return false;
  }
  if (model->shown_frame_number == 0) return rational_zero(offset);
  Av2DmRational base = { 0 };
  bool base_found = false;
  uint64_t presentation_epoch = model->rap_epoch;
  bool random_access_point = event->presentation_random_access_point;
  if (!event->presentation_uses_current_frame) {
    presentation_epoch = buffer->rap_epoch;
    random_access_point = buffer->random_access_point;
  }
  if (buffer->generation_valid || event->presentation_uses_current_frame ||
      model->config.ras_start) {
    const uint64_t base_epoch =
        (random_access_point || event->leading_frame)
            ? (presentation_epoch == 0 ? 0 : presentation_epoch - 1)
            : presentation_epoch;
    if (base_epoch == 0) {
      base_found = rational_zero(&base);
    } else {
      const Av2DmRapPresentationAnchor *const anchor =
          find_rap_presentation_anchor(model, base_epoch);
      if (anchor != NULL) {
        if (!av2_dm_rational_copy(&base, &anchor->presentation_offset)) {
          av2_dm_rational_destroy(&base);
          return false;
        }
        base_found = true;
      }
    }
  }
  if (!base_found && event->presentation_base_offset_present) {
    // Externally seeded RAS frames have no decode record in this model run.
    if (!av2_dm_rational_copy(&base, &event->presentation_base_offset)) {
      av2_dm_rational_destroy(&base);
      return false;
    }
    base_found = true;
  }
  if (!base_found) {
    missing_input(model);
    av2_dm_rational_destroy(&base);
    return false;
  }
  Av2DmRational increment = { 0 };
  const bool calculated =
      av2_dm_rational_multiply_u64(
          &buffer->disp_ct, event->presentation_time_ticks, &increment) &&
      av2_dm_rational_add(&base, &increment, offset);
  av2_dm_rational_destroy(&base);
  av2_dm_rational_destroy(&increment);
  return calculated;
}

static int32_t select_output_buffer(Av2DecoderModel *model, Av2DmLane *lane,
                                    const Av2DmOutputEvent *event,
                                    bool report_error) {
  if (event->frame_to_show_map_idx == -1) {
    return lane->current_buffer_index;
  }
  if (event->frame_to_show_map_idx < 0 ||
      (uint32_t)event->frame_to_show_map_idx >= lane->pool.num_ref_frames ||
      ((event->ref_valid_mask >> event->frame_to_show_map_idx) & 1) == 0 ||
      lane->pool.vbi[event->frame_to_show_map_idx] == -1) {
    if (report_error) {
      Av2DmViolationDetail detail;
      memset(&detail, 0, sizeof(detail));
      detail.kind = AV2_DM_VIOLATION_DETAIL_REFERENCE_SLOT;
      Av2DmReferenceSlotViolationDetail *const slot =
          &detail.value.reference_slot;
      slot->requested_slot = event->frame_to_show_map_idx;
      slot->slot_in_range =
          event->frame_to_show_map_idx >= 0 &&
          (uint32_t)event->frame_to_show_map_idx < lane->pool.num_ref_frames;
      slot->buffer_index = -1;
      if (slot->slot_in_range) {
        slot->reference_valid =
            ((event->ref_valid_mask >> event->frame_to_show_map_idx) & 1) != 0;
        slot->buffer_index = lane->pool.vbi[event->frame_to_show_map_idx];
      }
      slot->pool =
          buffer_pool_violation_detail(&lane->pool, false).value.buffer_pool;
      report_violation_for_affected(
          model, AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY,
          event->event_index, AV2_DM_VIOLATION_AFFECTED_OUTPUT,
          event->event_index, NULL, NULL, &detail);
    }
    return -1;
  }
  return lane->pool.vbi[event->frame_to_show_map_idx];
}

static void decoder_model_output_frame_internal(Av2DecoderModel *model,
                                                const Av2DmOutputEvent *event) {
  if (model == NULL || event == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  const int32_t buffer_index =
      select_output_buffer(model, &model->lane, event, true);
  const int32_t resource_buffer_index =
      select_output_buffer(model, &model->resource_lane, event, false);
  if (buffer_index < 0 || resource_buffer_index < 0) {
    if (!increment_output_count(model)) return;
    model_event_complete(model);
    update_result_status(model);
    return;
  }
  Av2DmBuffer *const buffer = &model->lane.pool.buffers[buffer_index];
  Av2DmBuffer *const resource_buffer =
      &model->resource_lane.pool.buffers[resource_buffer_index];
  if (!buffer->generation_valid || !resource_buffer->generation_valid ||
      buffer->generation != event->generation ||
      resource_buffer->generation != event->generation) {
    missing_input(model);
    return;
  }
  Av2DmRational presentation_offset = { 0 };
  if (!calculate_presentation_offset(model, event, buffer,
                                     &presentation_offset)) {
    if (!model->result.missing_required_input) arithmetic_failure(model);
    av2_dm_rational_destroy(&presentation_offset);
    return;
  }
  const uint64_t presentation_epoch = event->presentation_uses_current_frame
                                          ? model->rap_epoch
                                          : buffer->rap_epoch;
  const uint64_t output_rap_epoch =
      event->leading_frame && presentation_epoch != 0 ? presentation_epoch - 1
                                                      : presentation_epoch;
  const bool random_access_point = event->presentation_uses_current_frame
                                       ? event->presentation_random_access_point
                                       : buffer->random_access_point;
  Av2DmRational presentation = { 0 };
  if (!av2_dm_rational_copy(&presentation, &presentation_offset)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&presentation_offset);
    av2_dm_rational_destroy(&presentation);
    return;
  }
#define CLEANUP_OUTPUT_RATIONALS()                 \
  do {                                             \
    av2_dm_rational_destroy(&presentation_offset); \
    av2_dm_rational_destroy(&presentation);        \
  } while (0)
  if (model->lane.initial_presentation_delay_known) {
    if (!av2_dm_rational_add(&presentation,
                             &model->lane.initial_presentation_delay,
                             &presentation)) {
      arithmetic_failure(model);
      CLEANUP_OUTPUT_RATIONALS();
      return;
    }
  }
  if (!av2_dm_rational_copy(&buffer->presentation_time, &presentation)) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }
  buffer->presentation_time_valid =
      model->lane.initial_presentation_delay_known;
  if (!av2_dm_buffer_pool_add_player_ref(&model->lane.pool,
                                         (uint32_t)buffer_index)) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }

  if (!av2_dm_rational_copy(&resource_buffer->presentation_time,
                            &presentation_offset)) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }
  resource_buffer->presentation_time_valid = false;
  if (model->resource_lane.initial_presentation_delay_known) {
    if (!av2_dm_rational_add(&resource_buffer->presentation_time,
                             &model->resource_lane.initial_presentation_delay,
                             &resource_buffer->presentation_time)) {
      arithmetic_failure(model);
      CLEANUP_OUTPUT_RATIONALS();
      return;
    }
    resource_buffer->presentation_time_valid = true;
  }
  if (!av2_dm_buffer_pool_add_player_ref(&model->resource_lane.pool,
                                         (uint32_t)resource_buffer_index)) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }

  if (model->previous_output_presentation_valid &&
      model->previous_output_rap_epoch == output_rap_epoch) {
    compare_lower_limit(model, AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE,
                        event->event_index, &presentation_offset,
                        &model->previous_output_presentation_offset);
  }
  if (buffer->decode_completion_time_valid) {
    if (model->previous_output_order_valid &&
        buffer->decode_order < model->previous_output_decode_order) {
      if (!increment_model_u64(model, &model->result.reordered_outputs)) {
        CLEANUP_OUTPUT_RATIONALS();
        return;
      }
    }
    model->previous_output_decode_order = buffer->decode_order;
    model->previous_output_order_valid = true;
  }
  if (!av2_dm_rational_copy(&model->previous_output_presentation_offset,
                            &presentation_offset) ||
      !av2_dm_rational_copy(&model->last_presentation_offset,
                            &presentation_offset) ||
      (model->lane.initial_presentation_delay_known &&
       !av2_dm_rational_copy(&model->last_presentation, &presentation))) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }
  model->previous_output_presentation_valid = true;
  model->previous_output_rap_epoch = output_rap_epoch;
  model->last_presentation_offset_valid = true;
  if (model->lane.initial_presentation_delay_known) {
    model->last_presentation_valid = true;
  }
  model->last_output_temporal_unit = event->temporal_unit_index;
  if (random_access_point) {
    store_rap_presentation_anchor(model, output_rap_epoch,
                                  &presentation_offset);
  }
  update_tu_for_output(model, event, &presentation_offset);
  if (model->processing_stopped) {
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_header_rate_windows(model, false, event->event_index);
  }
  if (model->processing_stopped) {
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }
  if (model->lane.initial_presentation_delay_known) {
    complete_output_checks(
        model, event->event_index, event->event_index, &model->lane.time,
        buffer->decode_completion_time_valid ? &buffer->decode_completion_time
                                             : NULL,
        &presentation);
  } else if (!update_pending_output_witness(
                 &model->pending_display_late, event->event_index,
                 &model->lane.time, &presentation_offset) ||
             (buffer->decode_completion_time_valid &&
              !update_pending_output_witness(
                  &model->pending_decode_deadline, event->event_index,
                  &buffer->decode_completion_time, &presentation_offset))) {
    arithmetic_failure(model);
    CLEANUP_OUTPUT_RATIONALS();
    return;
  }

  CLEANUP_OUTPUT_RATIONALS();
#undef CLEANUP_OUTPUT_RATIONALS
  if (!increment_output_count(model)) return;
  model_event_complete(model);
  update_result_status(model);
}

void av2_decoder_model_output_frame(Av2DecoderModel *model,
                                    const Av2DmOutputEvent *event) {
  if (model == NULL || event == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  decoder_model_output_frame_internal(model, event);
  end_model_transaction(model, &transaction);
}

static void check_smoothing_fullness_at(Av2DecoderModel *model,
                                        const Av2DmRational *time,
                                        Av2DmDfgRecord *breakpoint,
                                        const Av2DmRational *buffer_size,
                                        bool after_removal,
                                        uint64_t proving_event_index) {
  Av2DmRational fullness = { 0 };
  if (!rational_zero(&fullness)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&fullness);
    return;
  }
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    const Av2DmDfgRecord *const dfg = &model->dfgs[i];
    int before_first;
    int removal_order;
    if (!av2_dm_rational_compare(time, &dfg->first_arrival, &before_first) ||
        !av2_dm_rational_compare(time, &dfg->removal, &removal_order)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&fullness);
      return;
    }
    if (before_first < 0 || removal_order > 0 ||
        (after_removal && removal_order == 0)) {
      continue;
    }
    Av2DmRational duration = { 0 };
    Av2DmRational arrived = { 0 };
    Av2DmRational coded_bits = { 0 };
    if (!av2_dm_rational_subtract(time, &dfg->first_arrival, &duration) ||
        !rational_multiply(&duration, &dfg->limits.bit_rate, &arrived) ||
        !av2_dm_rational_make(dfg->coded_bits, 1, &coded_bits)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&duration);
      av2_dm_rational_destroy(&arrived);
      av2_dm_rational_destroy(&coded_bits);
      av2_dm_rational_destroy(&fullness);
      return;
    }
    bool too_many;
    if (!rational_greater(&arrived, &coded_bits, &too_many)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&duration);
      av2_dm_rational_destroy(&arrived);
      av2_dm_rational_destroy(&coded_bits);
      av2_dm_rational_destroy(&fullness);
      return;
    }
    if (too_many && !av2_dm_rational_copy(&arrived, &coded_bits)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&duration);
      av2_dm_rational_destroy(&arrived);
      av2_dm_rational_destroy(&coded_bits);
      av2_dm_rational_destroy(&fullness);
      return;
    }
    if (!av2_dm_rational_add(&fullness, &arrived, &fullness)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&duration);
      av2_dm_rational_destroy(&arrived);
      av2_dm_rational_destroy(&coded_bits);
      av2_dm_rational_destroy(&fullness);
      return;
    }
    av2_dm_rational_destroy(&duration);
    av2_dm_rational_destroy(&arrived);
    av2_dm_rational_destroy(&coded_bits);
  }
  bool overflow;
  if (!rational_greater(&fullness, buffer_size, &overflow)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&fullness);
    return;
  }
  if (overflow && !breakpoint->smoothing_overflow_reported) {
    breakpoint->smoothing_overflow_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_DFG, breakpoint->event_index, &fullness,
        buffer_size, NULL);
  }
  av2_dm_rational_destroy(&fullness);
}

static void retire_closed_smoothing_records(Av2DecoderModel *model,
                                            const Av2DmRational *frontier) {
  uint32_t write_index = 0;
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    Av2DmDfgRecord *const dfg = &model->dfgs[i];
    int comparison;
    if (!av2_dm_rational_compare(&dfg->removal, frontier, &comparison)) {
      arithmetic_failure(model);
      return;
    }
    // Equality remains live because a later DFG may start arriving at exactly
    // this frontier and introduce another breakpoint at the same instant.
    if (comparison < 0) {
      dfg_record_destroy(dfg);
      continue;
    }
    if (write_index != i) {
      dfg_record_destroy(&model->dfgs[write_index]);
      model->dfgs[write_index] = *dfg;
      memset(dfg, 0, sizeof(*dfg));
    }
    ++write_index;
  }
  model->dfg_count = write_index;
}

static void check_smoothing_buffer_overflow(Av2DecoderModel *model,
                                            const Av2DmRational *frontier,
                                            uint64_t proving_event_index) {
  if (violation_seen(model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW)) {
    return;
  }
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    Av2DmDfgRecord *const breakpoint = &model->dfgs[i];
    bool last_arrival_reached = true;
    bool removal_reached = true;
    if (frontier != NULL) {
      int comparison;
      if (!av2_dm_rational_compare(&breakpoint->last_arrival, frontier,
                                   &comparison)) {
        arithmetic_failure(model);
        return;
      }
      last_arrival_reached = comparison <= 0;
      if (!av2_dm_rational_compare(&breakpoint->removal, frontier,
                                   &comparison)) {
        arithmetic_failure(model);
        return;
      }
      // Equality remains open until the arrival frontier advances: another
      // DFG may begin arriving at that instant with a new effective capacity.
      removal_reached = comparison < 0;
    }
    if (last_arrival_reached) {
      check_smoothing_fullness_at(model, &breakpoint->last_arrival, breakpoint,
                                  &breakpoint->buffer_size_at_last_arrival,
                                  false, proving_event_index);
    }
    if (removal_reached) {
      check_smoothing_fullness_at(model, &breakpoint->removal, breakpoint,
                                  &breakpoint->buffer_size_before_removal,
                                  false, proving_event_index);
    }
    if (removal_reached && breakpoint->buffer_size_decreases_after_removal) {
      check_smoothing_fullness_at(model, &breakpoint->removal, breakpoint,
                                  &breakpoint->buffer_size_after_removal, true,
                                  proving_event_index);
    }
  }
}

static bool same_scope(const Av2DmScope *a, const Av2DmScope *b) {
  return a->xlayer_id == b->xlayer_id && a->ops_xlayer_id == b->ops_xlayer_id &&
         a->ops_id == b->ops_id && a->operating_point == b->operating_point &&
         a->whole_xlayer == b->whole_xlayer;
}

static bool same_dpb_configuration(const Av2DmConfig *a, const Av2DmConfig *b) {
  return a->num_ref_frames == b->num_ref_frames &&
         a->max_frame_width == b->max_frame_width &&
         a->max_frame_height == b->max_frame_height &&
         a->chroma_format_idc == b->chroma_format_idc &&
         a->bit_depth == b->bit_depth;
}

Av2DmParameterUpdateDisposition av2_decoder_model_classify_parameter_update(
    const Av2DecoderModel *model, const Av2DmConfig *config,
    bool closed_loop_key_transition) {
  if (model == NULL || config == NULL || model->result.finished ||
      model->processing_stopped ||
      model->result.applicability != AV2_DM_APPLICABLE ||
      config->applicability != AV2_DM_APPLICABLE ||
      !parameter_inputs_valid(config)) {
    return AV2_DM_PARAMETER_UPDATE_MISSING_REQUIRED_INPUT;
  }
  const bool incompatible =
      !same_scope(&model->config.scope, &config->scope) ||
      (!closed_loop_key_transition && model->config.mode != config->mode) ||
      (!closed_loop_key_transition &&
       !same_dpb_configuration(&model->config, config));
  if (incompatible) {
    return AV2_DM_PARAMETER_UPDATE_INCOMPATIBLE_CONFIGURATION;
  }
  Av2DmResolvedParameters parameters = { 0 };
  if (!resolve_parameters(config, &parameters)) {
    resolved_parameters_destroy(&parameters);
    return AV2_DM_PARAMETER_UPDATE_INTERNAL_FAILURE;
  }
  resolved_parameters_destroy(&parameters);
  return AV2_DM_PARAMETER_UPDATE_ALLOWED;
}

static bool decoder_model_update_parameters_internal(
    Av2DecoderModel *model, const Av2DmConfig *config, uint64_t event_index,
    bool closed_loop_key_transition) {
  if (model == NULL || config == NULL || model->result.finished ||
      model->processing_stopped) {
    return false;
  }
  if (model->result.applicability != AV2_DM_APPLICABLE ||
      config->applicability != AV2_DM_APPLICABLE ||
      !parameter_inputs_valid(config)) {
    missing_input(model);
    return false;
  }
  if (!same_scope(&model->config.scope, &config->scope) ||
      (!closed_loop_key_transition && model->config.mode != config->mode) ||
      (!closed_loop_key_transition &&
       !same_dpb_configuration(&model->config, config))) {
    missing_input(model);
    return false;
  }

  Av2DmResolvedParameters parameters = { 0 };
  if (!resolve_parameters(config, &parameters)) {
    resolved_parameters_destroy(&parameters);
    arithmetic_failure(model);
    return false;
  }
  if (closed_loop_key_transition) {
    finalize_tile_cvs(model, event_index);
  }
  if (model->processing_stopped) {
    resolved_parameters_destroy(&parameters);
    return false;
  }
  if (closed_loop_key_transition &&
      model->config.defer_nonterminal_checks_for_testing) {
    check_max_reference_frames(model, event_index);
    if (model->processing_stopped) {
      resolved_parameters_destroy(&parameters);
      return false;
    }
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    // Close every evaluable old-parameter breakpoint before prospective
    // values take effect. The still-live smoothing history remains continuous.
    if (model->previous_dfg_valid) {
      check_smoothing_buffer_overflow(model, &model->previous_dfg.last_arrival,
                                      event_index);
    }
  }
  if (model->processing_stopped) {
    resolved_parameters_destroy(&parameters);
    return false;
  }
  const bool dpb_compatible = same_dpb_configuration(&model->config, config);
  if (!dpb_compatible && model->shown_frame_number != 0 &&
      (!set_lane_initial_presentation_delay(model, &model->lane, true, true,
                                            event_index) ||
       !set_lane_initial_presentation_delay(model, &model->resource_lane, false,
                                            true, event_index))) {
    arithmetic_failure(model);
    resolved_parameters_destroy(&parameters);
    return false;
  }
  if (model->processing_stopped) {
    resolved_parameters_destroy(&parameters);
    return false;
  }

  int buffer_size_comparison;
  if (!av2_dm_rational_compare(&parameters.limits.buffer_size,
                               &model->limits.buffer_size,
                               &buffer_size_comparison)) {
    arithmetic_failure(model);
    resolved_parameters_destroy(&parameters);
    return false;
  }
  Av2DmRational pending_buffer_size = { 0 };
  Av2DmConfig updated = { 0 };
  Av2DmLane copied_lane = { 0 };
  const Av2DmMode old_mode = model->config.mode;
  if (!av2_dm_rational_copy(&pending_buffer_size,
                            &parameters.limits.buffer_size) ||
      !av2_dm_config_copy(&updated, config) ||
      (old_mode == AV2_DM_DECODING_SCHEDULE_MODE &&
       updated.mode == AV2_DM_RESOURCE_AVAILABILITY_MODE &&
       !lane_copy(&copied_lane, &model->lane))) {
    av2_dm_rational_destroy(&pending_buffer_size);
    av2_dm_config_destroy(&updated);
    lane_destroy(&copied_lane);
    resolved_parameters_destroy(&parameters);
    arithmetic_failure(model);
    return false;
  }
  updated.initial_display_delay = model->config.initial_display_delay;
  updated.ras_start = model->config.ras_start;
  updated.ras_seed_complete = model->config.ras_seed_complete;
  updated.ras_seed_count = model->config.ras_seed_count;
  memcpy(updated.ras_seeds, model->config.ras_seeds, sizeof(updated.ras_seeds));
  if (!apply_parameters(model, &updated, &parameters)) {
    av2_dm_rational_destroy(&pending_buffer_size);
    av2_dm_config_destroy(&updated);
    lane_destroy(&copied_lane);
    resolved_parameters_destroy(&parameters);
    arithmetic_failure(model);
    return false;
  }
  model->pending_buffer_size_change = buffer_size_comparison;
  av2_dm_rational_move(&model->pending_buffer_size, &pending_buffer_size);
  if (!dpb_compatible) {
    if (!av2_dm_buffer_pool_initialize(&model->lane.pool,
                                       config->num_ref_frames) ||
        !av2_dm_buffer_pool_initialize(&model->resource_lane.pool,
                                       config->num_ref_frames)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&pending_buffer_size);
      av2_dm_config_destroy(&updated);
      lane_destroy(&copied_lane);
      resolved_parameters_destroy(&parameters);
      return false;
    }
    model->lane.current_buffer_index = -1;
    model->resource_lane.current_buffer_index = -1;
  }
  if (!closed_loop_key_transition && !model->max_reference_frames_violated) {
    // The maximum depends on the active level limits and must be reconsidered
    // for the RAP frame after an OPS parameter update.
    model->max_reference_frames_checked = false;
  }
  if (old_mode == AV2_DM_DECODING_SCHEDULE_MODE &&
      updated.mode == AV2_DM_RESOURCE_AVAILABILITY_MODE) {
    // Resource removal starts from the actual continuing decoder state.
    if (dpb_compatible) {
      lane_destroy(&model->resource_lane);
      model->resource_lane = copied_lane;
      memset(&copied_lane, 0, sizeof(copied_lane));
    } else {
      model->resource_lane.initial_presentation_delay_known =
          copied_lane.initial_presentation_delay_known;
      av2_dm_rational_move(&model->resource_lane.time, &copied_lane.time);
      av2_dm_rational_move(&model->resource_lane.initial_presentation_delay,
                           &copied_lane.initial_presentation_delay);
    }
  }
  model->result.mode = updated.mode;
  update_result_status(model);
  update_storage_stats(model);
  av2_dm_rational_destroy(&pending_buffer_size);
  av2_dm_config_destroy(&updated);
  lane_destroy(&copied_lane);
  resolved_parameters_destroy(&parameters);
  return true;
}

bool av2_decoder_model_update_parameters(Av2DecoderModel *model,
                                         const Av2DmConfig *config,
                                         uint64_t event_index,
                                         bool closed_loop_key_transition) {
  if (model == NULL || config == NULL || model->result.finished ||
      model->processing_stopped) {
    return false;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return false;
  const bool updated = decoder_model_update_parameters_internal(
      model, config, event_index, closed_loop_key_transition);
  end_model_transaction(model, &transaction);
  return updated && !model->result.allocation_failed &&
         !model->result.arithmetic_failed;
}

void av2_decoder_model_mark_incomplete(Av2DecoderModel *model) {
  if (model == NULL || model->result.finished || model->processing_stopped) {
    return;
  }
  missing_input(model);
}

static void check_max_reference_frames(Av2DecoderModel *model,
                                       uint64_t event_index) {
  if (model->config.still_picture) return;
  if (model->max_reference_frames_violated) return;
  if (model->max_reference_frames_checked &&
      model->max_reference_frames_reserved ==
          model->any_decode_count_two_requires_reserved_buffer) {
    return;
  }
  model->max_reference_frames_checked = true;
  model->max_reference_frames_reserved =
      model->any_decode_count_two_requires_reserved_buffer;
  const uint64_t frame_size =
      (uint64_t)model->config.max_frame_width * model->config.max_frame_height;
  if (frame_size == 0) {
    missing_input(model);
    return;
  }
  if (model->limits.max_picture_size > UINT64_MAX / 8) {
    arithmetic_failure(model);
    return;
  }
  uint64_t maximum = 8 * model->limits.max_picture_size / frame_size;
  if (model->any_decode_count_two_requires_reserved_buffer && maximum != 0) {
    --maximum;
  }
  const uint64_t syntax_maximum =
      model->config.explicit_num_ref_frames ? 16 : 8;
  if (maximum > syntax_maximum) maximum = syntax_maximum;
  Av2DmRational observed = { 0 };
  Av2DmRational limit = { 0 };
  if (!av2_dm_rational_make(model->config.num_ref_frames, 1, &observed) ||
      !av2_dm_rational_make(maximum, 1, &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  bool too_many_reference_frames;
  if (!rational_greater(&observed, &limit, &too_many_reference_frames)) {
    arithmetic_failure(model);
  } else if (too_many_reference_frames) {
    model->max_reference_frames_violated = true;
    report_violation(model, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES, event_index,
                     &observed, &limit);
  }
  av2_dm_rational_destroy(&observed);
  av2_dm_rational_destroy(&limit);
}

static void check_header_rate_at(Av2DecoderModel *model, Av2DmTuRecord *end_tu,
                                 uint64_t frame_headers,
                                 uint64_t proving_event_index) {
  if (end_tu->still_picture) return;
  const uint64_t maximum_headers = (uint64_t)end_tu->limits.max_header_rate *
                                   (1 + ((uint64_t)end_tu->tier << 1));
  Av2DmRational observed = { 0 };
  Av2DmRational limit = { 0 };
  if (!av2_dm_rational_make(frame_headers, 1, &observed) ||
      !av2_dm_rational_make(maximum_headers, 1, &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  bool violated;
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  if (violated && !end_tu->header_rate_reported) {
    end_tu->header_rate_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_MAX_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, end_tu->temporal_unit_index,
        &observed, &limit, NULL);
  }
  const uint64_t maximum_tile_area = end_tu->maximum_tile_area_finalized
                                         ? end_tu->maximum_tile_area
                                         : model->maximum_tile_area;
  if (!rational_from_product(maximum_tile_area, frame_headers, &observed) ||
      !av2_dm_rational_make(end_tu->limits.max_tile_size_header_rate_product, 1,
                            &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  if (violated && !end_tu->tile_header_rate_reported) {
    end_tu->tile_header_rate_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, end_tu->temporal_unit_index,
        &observed, &limit, NULL);
  }
  av2_dm_rational_destroy(&observed);
  av2_dm_rational_destroy(&limit);
}

static void check_retired_tile_header_summary(Av2DecoderModel *model,
                                              uint64_t proving_event_index) {
  if (violation_seen(model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) ||
      !model->retired_header_summary_valid ||
      model->retired_header_summary_reported) {
    return;
  }
  Av2DmRational observed = { 0 };
  Av2DmRational limit = { 0 };
  if (!rational_from_product(model->maximum_tile_area,
                             model->retired_max_frame_headers, &observed) ||
      !av2_dm_rational_make(model->retired_header_limit, 1, &limit)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  bool violated;
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&observed);
    av2_dm_rational_destroy(&limit);
    return;
  }
  if (violated) {
    model->retired_header_summary_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT,
        model->retired_header_event_index, &observed, &limit, NULL);
  }
  av2_dm_rational_destroy(&observed);
  av2_dm_rational_destroy(&limit);
}

static void finalize_tile_cvs(Av2DecoderModel *model,
                              uint64_t proving_event_index) {
  if (model->tile_cvs_finalized) return;
  check_retired_tile_header_summary(model, proving_event_index);
  if (model->processing_stopped) return;
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    Av2DmTuRecord *const tu = &model->tus[i];
    if (!tu->maximum_tile_area_finalized) {
      tu->maximum_tile_area = model->maximum_tile_area;
      tu->maximum_tile_area_finalized = true;
    }
  }
  model->tile_cvs_finalized = true;
}

static bool order_tus_by_output_time(const Av2DecoderModel *model,
                                     uint32_t **ordered_tus,
                                     uint32_t *ordered_tu_count) {
  internal_allocation_failed = false;
  *ordered_tus = NULL;
  *ordered_tu_count = 0;
  if (model->tu_count == 0) return true;
  const uint64_t capacity = model->tu_count;
  if (capacity > SIZE_MAX / sizeof(**ordered_tus)) return false;
  const size_t allocation_size = (size_t)capacity * sizeof(**ordered_tus);
  uint32_t *source = internal_malloc(allocation_size);
  if (source == NULL) return false;
  uint32_t *destination = internal_malloc(allocation_size);
  if (destination == NULL) {
    avm_free(source);
    return false;
  }
  uint32_t count = 0;
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    if (model->tus[i].output_time_valid) source[count++] = i;
  }
  for (size_t width = 1; width < count; width *= 2) {
    const size_t block_width = 2 * width;
    for (size_t left = 0; left < count; left += block_width) {
      const size_t middle = left + width < count ? left + width : count;
      const size_t right =
          left + block_width < count ? left + block_width : count;
      size_t first = left;
      size_t second = middle;
      size_t output = left;
      while (first < middle && second < right) {
        const Av2DmTuRecord *const first_tu = &model->tus[source[first]];
        const Av2DmTuRecord *const second_tu = &model->tus[source[second]];
        int comparison;
        if (first_tu->cvs_number < second_tu->cvs_number) {
          comparison = -1;
        } else if (first_tu->cvs_number > second_tu->cvs_number) {
          comparison = 1;
        } else if (!av2_dm_rational_compare(&first_tu->output_time,
                                            &second_tu->output_time,
                                            &comparison)) {
          avm_free(source);
          avm_free(destination);
          return false;
        }
        destination[output++] =
            comparison <= 0 ? source[first++] : source[second++];
      }
      while (first < middle) destination[output++] = source[first++];
      while (second < right) destination[output++] = source[second++];
    }
    uint32_t *const swap = source;
    source = destination;
    destination = swap;
    if (width > count / 2) break;
  }
  avm_free(destination);
  *ordered_tus = source;
  *ordered_tu_count = count;
  return true;
}

static void check_header_rate_windows_in_output_order(
    Av2DecoderModel *model, const uint32_t *ordered_tus,
    uint32_t ordered_tu_count, const Av2DmRational *one_second,
    uint64_t proving_event_index) {
  uint32_t first_tu = 0;
  uint64_t frame_headers = 0;
  uint64_t cvs_number = 0;
  for (uint32_t i = 0; i < ordered_tu_count; ++i) {
    Av2DmTuRecord *const end_tu = &model->tus[ordered_tus[i]];
    if (i == 0 || end_tu->cvs_number != cvs_number) {
      first_tu = i;
      frame_headers = 0;
      cvs_number = end_tu->cvs_number;
    }
    if (UINT64_MAX - frame_headers < end_tu->frame_headers) {
      arithmetic_failure(model);
      return;
    }
    frame_headers += end_tu->frame_headers;
    Av2DmRational window_start = { 0 };
    if (!av2_dm_rational_subtract(&end_tu->output_time, one_second,
                                  &window_start)) {
      arithmetic_failure(model);
      av2_dm_rational_destroy(&window_start);
      return;
    }
    while (first_tu <= i) {
      const Av2DmTuRecord *const candidate = &model->tus[ordered_tus[first_tu]];
      int comparison;
      if (!av2_dm_rational_compare(&candidate->output_time, &window_start,
                                   &comparison)) {
        arithmetic_failure(model);
        av2_dm_rational_destroy(&window_start);
        return;
      }
      if (comparison >= 0) break;
      if (frame_headers < candidate->frame_headers) {
        arithmetic_failure(model);
        av2_dm_rational_destroy(&window_start);
        return;
      }
      frame_headers -= candidate->frame_headers;
      ++first_tu;
    }
    if (end_tu->header_complete && !end_tu->header_window_checked) {
      end_tu->header_window_checked = true;
      end_tu->header_window_headers = frame_headers;
    }
    check_header_rate_at(model, end_tu,
                         end_tu->header_window_checked
                             ? end_tu->header_window_headers
                             : frame_headers,
                         proving_event_index);
    av2_dm_rational_destroy(&window_start);
    if (model->processing_stopped) return;
  }
}

static bool lane_has_live_coded_tu(const Av2DmLane *lane,
                                   uint64_t temporal_unit_index) {
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    const Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (lane_buffer_is_live(lane, i) && buffer->coded_temporal_unit_valid &&
        buffer->coded_temporal_unit_index == temporal_unit_index) {
      return true;
    }
  }
  return false;
}

static bool tu_has_live_generation(const Av2DecoderModel *model,
                                   uint64_t temporal_unit_index) {
  return lane_has_live_coded_tu(&model->lane, temporal_unit_index) ||
         lane_has_live_coded_tu(&model->resource_lane, temporal_unit_index);
}

static void remember_retired_tu(Av2DecoderModel *model,
                                const Av2DmTuRecord *tu) {
  if (!tu->output_time_valid &&
      (tu->frame_headers != 0 || tu->output_frames != 0)) {
    model->retired_unresolved_tu = true;
  }
  if (violation_seen(model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) ||
      !tu->header_window_checked || tu->tile_header_rate_reported ||
      tu->maximum_tile_area_finalized) {
    return;
  }
  if (!model->retired_header_summary_valid ||
      tu->header_window_headers > model->retired_max_frame_headers) {
    model->retired_header_summary_valid = true;
    model->retired_max_frame_headers = tu->header_window_headers;
    model->retired_header_event_index = tu->temporal_unit_index;
    model->retired_header_limit = tu->limits.max_tile_size_header_rate_product;
  }
}

static void retire_unresolvable_tus(Av2DecoderModel *model) {
  uint32_t write_index = 0;
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    Av2DmTuRecord *const tu = &model->tus[i];
    const bool current_coded =
        model->coded_tu_valid && tu->temporal_unit_index == model->coded_tu;
    const bool retire = tu->header_complete && !tu->output_time_valid &&
                        !current_coded &&
                        !tu_has_live_generation(model, tu->temporal_unit_index);
    if (retire) {
      remember_retired_tu(model, tu);
      tu_record_destroy(tu);
      continue;
    }
    if (write_index != i) tu_record_move(&model->tus[write_index], tu);
    ++write_index;
  }
  model->tu_count = write_index;
}

static void restart_tu_history(Av2DecoderModel *model,
                               uint64_t temporal_unit_index) {
  uint32_t write_index = 0;
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    Av2DmTuRecord *const tu = &model->tus[i];
    const bool keep_current =
        tu->temporal_unit_index == temporal_unit_index ||
        (model->coded_tu_valid && tu->temporal_unit_index == model->coded_tu);
    const bool keep_pending =
        !tu->output_time_valid &&
        tu_has_live_generation(model, tu->temporal_unit_index);
    if (!keep_current && !keep_pending) {
      remember_retired_tu(model, tu);
      tu_record_destroy(tu);
      continue;
    }
    if (write_index != i) tu_record_move(&model->tus[write_index], tu);
    ++write_index;
  }
  model->tu_count = write_index;
  const Av2DmTuRecord *const current = find_tu(model, temporal_unit_index);
  if (current != NULL && current->output_time_valid) {
    if (!av2_dm_rational_copy(&model->latest_timed_tu_output_time,
                              &current->output_time)) {
      arithmetic_failure(model);
    } else {
      model->latest_timed_tu_valid = true;
    }
  }
}

static void retire_closed_tus(Av2DecoderModel *model,
                              const Av2DmRational *one_second) {
  const bool header_history_proven =
      violation_seen(model, AV2_DM_VIOLATION_MAX_HEADER_RATE) &&
      violation_seen(model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE);
  if (!header_history_proven && !model->latest_timed_tu_valid) return;
  Av2DmRational frontier = { 0 };
  if (!header_history_proven &&
      !av2_dm_rational_subtract(&model->latest_timed_tu_output_time, one_second,
                                &frontier)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&frontier);
    return;
  }
  uint32_t write_index = 0;
  for (uint32_t i = 0; i < model->tu_count; ++i) {
    Av2DmTuRecord *const tu = &model->tus[i];
    bool retire = false;
    if (tu->header_complete &&
        (!model->coded_tu_valid ||
         tu->temporal_unit_index != model->coded_tu) &&
        tu->temporal_unit_index != model->last_output_tu &&
        (tu->output_time_valid ||
         !tu_has_live_generation(model, tu->temporal_unit_index))) {
      if (header_history_proven) {
        retire = true;
      } else if (tu->header_window_checked && tu->output_time_valid) {
        int comparison;
        if (!av2_dm_rational_compare(&tu->output_time, &frontier,
                                     &comparison)) {
          arithmetic_failure(model);
          av2_dm_rational_destroy(&frontier);
          return;
        }
        retire = comparison < 0;
      }
    }
    if (retire) {
      remember_retired_tu(model, tu);
      tu_record_destroy(tu);
      continue;
    }
    if (write_index != i) tu_record_move(&model->tus[write_index], tu);
    ++write_index;
  }
  model->tu_count = write_index;
  av2_dm_rational_destroy(&frontier);
}

static void check_header_rate_windows(Av2DecoderModel *model,
                                      bool require_complete,
                                      uint64_t proving_event_index) {
  if (!require_complete) {
    model->latest_header_check_event_index = proving_event_index;
  }
  if (require_complete) {
    for (uint32_t i = 0; i < model->tu_count; ++i) {
      if (!model->tus[i].still_picture && model->tus[i].frame_headers != 0 &&
          !model->tus[i].output_time_valid) {
        incomplete_verification(model);
        return;
      }
    }
  }
  Av2DmRational one_second = { 0 };
  if (!av2_dm_rational_make(1, 1, &one_second)) {
    arithmetic_failure(model);
    return;
  }
  uint32_t *ordered_tus;
  uint32_t ordered_tu_count;
  if (!order_tus_by_output_time(model, &ordered_tus, &ordered_tu_count)) {
    arithmetic_failure(model);
    av2_dm_rational_destroy(&one_second);
    return;
  }
  check_header_rate_windows_in_output_order(
      model, ordered_tus, ordered_tu_count, &one_second, proving_event_index);
  avm_free(ordered_tus);
  if (!model->processing_stopped && !require_complete) {
    retire_closed_tus(model, &one_second);
  }
  av2_dm_rational_destroy(&one_second);
}

static void decoder_model_finish_internal(Av2DecoderModel *model) {
  if (model == NULL || model->result.finished) return;
  if (model->result.applicability != AV2_DM_NOT_APPLICABLE &&
      !model->processing_stopped) {
    if (model->coded_tu_valid) {
      Av2DmTuRecord *const coded_tu = find_tu(model, model->coded_tu);
      if (coded_tu == NULL) {
        arithmetic_failure(model);
      } else {
        coded_tu->header_complete = true;
      }
    }
    if (!model->lane.initial_presentation_delay_known &&
        model->shown_frame_number != 0) {
      incomplete_verification(model);
    }
    if (!model->processing_stopped && model->previous_dfg_valid &&
        !model->previous_dfg.still_picture && model->dfg_number == 1) {
      Av2DmRational frame_parsing_time = { 0 };
      if (!av2_dm_rational_make(model->previous_dfg.limits.max_picture_size,
                                model->previous_dfg.limits.max_decode_rate,
                                &frame_parsing_time)) {
        arithmetic_failure(model);
      } else {
        check_frame_parsing_constraints(model, &model->previous_dfg,
                                        &frame_parsing_time,
                                        model->previous_dfg.event_index);
      }
      av2_dm_rational_destroy(&frame_parsing_time);
    }
    if (!model->processing_stopped &&
        (!model->previous_dfg_valid || !model->previous_dfg.still_picture) &&
        model->dfg_number > 1) {
      if (!model->previous_dfg_valid || !model->last_frame_parsing_time_valid) {
        incomplete_verification(model);
      } else {
        // Annex A reuses the preceding non-show-existing frame's parsing time
        // for the last DFG when such a predecessor is present.
        check_frame_parsing_constraints(model, &model->previous_dfg,
                                        &model->last_frame_parsing_time,
                                        model->previous_dfg.event_index);
      }
    }
    if (!model->processing_stopped && model->output_tu_count > 1) {
      Av2DmTuRecord *const last_output_tu =
          model->last_output_tu_valid ? find_tu(model, model->last_output_tu)
                                      : NULL;
      if (last_output_tu == NULL) {
        incomplete_verification(model);
      } else if (!last_output_tu->still_picture) {
        if (model->last_display_duration_valid) {
          // Annex A reuses the preceding output duration for the last TU.
          // Annex E does not synthesize another presentation interval.
          check_tu_display_rate(model, last_output_tu,
                                &model->last_display_duration,
                                last_output_tu->event_index);
        } else {
          incomplete_verification(model);
        }
      }
    }
    if (!model->processing_stopped) {
      check_smoothing_buffer_overflow(model, NULL,
                                      model->latest_frame_event_index);
    }
    if (!model->processing_stopped &&
        model->config.defer_nonterminal_checks_for_testing) {
      check_max_reference_frames(model, model->latest_frame_event_index);
    }
    if (!model->processing_stopped && model->retired_unresolved_tu) {
      incomplete_verification(model);
    }
    if (!model->processing_stopped) {
      check_header_rate_windows(model, true,
                                model->latest_header_check_event_index);
    }
  }
  model->result.finished = true;
  update_result_status(model);
  update_storage_stats(model);
}

void av2_decoder_model_finish(Av2DecoderModel *model) {
  if (model == NULL || model->result.finished) return;
  if (model->processing_stopped ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE) {
    decoder_model_finish_internal(model);
    return;
  }
  Av2DmModelTransaction transaction;
  if (!begin_model_transaction(model, &transaction)) return;
  decoder_model_finish_internal(model);
  end_model_transaction(model, &transaction);
}

bool av2_decoder_model_get_result(const Av2DecoderModel *model,
                                  Av2DmResult *result) {
  if (model == NULL || result == NULL) return false;
  *result = model->result;
  return true;
}

void av2_dm_state_init(Av2DmState *state) {
  if (state == NULL) return;
  memset(state, 0, sizeof(*state));
  av2_dm_rational_init(&state->time);
  av2_dm_rational_init(&state->first_bit_arrival);
  av2_dm_rational_init(&state->last_bit_arrival);
  av2_dm_rational_init(&state->scheduled_removal);
  av2_dm_rational_init(&state->removal);
  av2_dm_rational_init(&state->time_to_decode);
  av2_dm_rational_init(&state->decode_completion);
  av2_dm_rational_init(&state->last_presentation);
  av2_dm_rational_init(&state->last_presentation_offset);
  av2_dm_rational_init(&state->last_temporal_unit_output_time);
  av2_dm_rational_init(&state->initial_presentation_delay);
  state->current_buffer_index = -1;
}

void av2_dm_state_destroy(Av2DmState *state) {
  if (state == NULL) return;
  av2_dm_rational_destroy(&state->time);
  av2_dm_rational_destroy(&state->first_bit_arrival);
  av2_dm_rational_destroy(&state->last_bit_arrival);
  av2_dm_rational_destroy(&state->scheduled_removal);
  av2_dm_rational_destroy(&state->removal);
  av2_dm_rational_destroy(&state->time_to_decode);
  av2_dm_rational_destroy(&state->decode_completion);
  av2_dm_rational_destroy(&state->last_presentation);
  av2_dm_rational_destroy(&state->last_presentation_offset);
  av2_dm_rational_destroy(&state->last_temporal_unit_output_time);
  av2_dm_rational_destroy(&state->initial_presentation_delay);
  av2_dm_buffer_pool_destroy(&state->buffer_pool);
  memset(state, 0, sizeof(*state));
}

bool av2_decoder_model_get_state(const Av2DecoderModel *model,
                                 Av2DmState *state) {
  if (model == NULL || state == NULL) return false;
  Av2DmState temporary;
  av2_dm_state_init(&temporary);
  temporary.initial_presentation_delay_known =
      model->lane.initial_presentation_delay_known;
  temporary.current_buffer_index = model->lane.current_buffer_index;
  temporary.frame_number = model->frame_number;
  temporary.dfg_number = model->dfg_number;
  temporary.shown_frame_number = model->shown_frame_number;
  bool copied = av2_dm_rational_copy(&temporary.time, &model->lane.time) &&
                av2_dm_rational_copy(&temporary.initial_presentation_delay,
                                     &model->lane.initial_presentation_delay) &&
                (!model->lane.pool.initialized ||
                 buffer_pool_copy(&temporary.buffer_pool, &model->lane.pool));
  if (model->previous_dfg_valid) {
    const Av2DmDfgRecord *const dfg = &model->previous_dfg;
    temporary.last_dfg_valid = true;
    copied =
        copied &&
        av2_dm_rational_copy(&temporary.first_bit_arrival,
                             &dfg->first_arrival) &&
        av2_dm_rational_copy(&temporary.last_bit_arrival, &dfg->last_arrival) &&
        av2_dm_rational_copy(&temporary.scheduled_removal,
                             &dfg->scheduled_removal) &&
        av2_dm_rational_copy(&temporary.removal, &dfg->removal) &&
        av2_dm_rational_copy(&temporary.time_to_decode, &dfg->decode_time) &&
        av2_dm_rational_copy(&temporary.decode_completion,
                             &dfg->decode_completion);
  }
  if (model->shown_frame_number != 0) {
    temporary.last_presentation_valid = model->last_presentation_valid;
    temporary.last_presentation_offset_valid =
        model->last_presentation_offset_valid;
    temporary.last_output_temporal_unit_valid = true;
    temporary.last_output_temporal_unit = model->last_output_temporal_unit;
    copied = copied &&
             (!model->last_presentation_valid ||
              av2_dm_rational_copy(&temporary.last_presentation,
                                   &model->last_presentation)) &&
             (!model->last_presentation_offset_valid ||
              av2_dm_rational_copy(&temporary.last_presentation_offset,
                                   &model->last_presentation_offset));
  }
  if (model->last_output_tu_valid) {
    const Av2DmTuRecord *tu = NULL;
    for (uint32_t i = model->tu_count; i > 0; --i) {
      if (model->tus[i - 1].temporal_unit_index == model->last_output_tu) {
        tu = &model->tus[i - 1];
        break;
      }
    }
    if (tu == NULL) copied = false;
    if (tu != NULL) {
      temporary.last_temporal_unit_output_time_valid = tu->output_time_valid;
      temporary.last_temporal_unit_output_luma_samples =
          tu->output_luma_samples;
      temporary.last_temporal_unit_output_frames = tu->output_frames;
      copied = copied &&
               (!tu->output_time_valid ||
                av2_dm_rational_copy(&temporary.last_temporal_unit_output_time,
                                     &tu->output_time));
    }
  }
  if (!copied) {
    av2_dm_state_destroy(&temporary);
    return false;
  }
  av2_dm_state_destroy(state);
  *state = temporary;
  return true;
}

bool av2_decoder_model_get_storage_stats(const Av2DecoderModel *model,
                                         Av2DmStorageStats *stats) {
  if (model == NULL || stats == NULL) return false;
  *stats = model->storage;
  return true;
}

const char *av2_dm_violation_code_name(Av2DmViolationCode code) {
  static const char *const names[] = {
    "DECODE_FRAME_BUFFER_UNAVAILABLE",
    "DECODE_EXISTING_FRAME_BUFFER_EMPTY",
    "DISPLAY_FRAME_LATE",
    "SMOOTHING_BUFFER_UNDERFLOW",
    "SMOOTHING_BUFFER_OVERFLOW",
    "PRESENTATION_TIME_DECREASE",
    "SCHEDULE_BEFORE_RESOURCE_REMOVAL",
    "DECODER_BUFFER_DELAY_INCONSISTENT",
    "MINIMUM_DECODE_TIME",
    "MINIMUM_PRESENTATION_INTERVAL",
    "DECODE_DEADLINE",
    "DECODER_BUFFER_DELAY_ZERO",
    "DECODER_BUFFER_DELAY_TOO_LARGE",
    "MAX_PICTURE_SIZE",
    "MAX_HORIZONTAL_SIZE",
    "MAX_VERTICAL_SIZE",
    "MIN_HORIZONTAL_SIZE",
    "MIN_VERTICAL_SIZE",
    "MAX_TILES",
    "MAX_TILE_COLUMNS",
    "MAX_TILE_WIDTH",
    "MIN_TILE_WIDTH",
    "MAX_TILE_AREA",
    "MAX_DISPLAY_RATE",
    "MAX_HEADER_RATE",
    "MAX_REFERENCE_FRAMES",
    "FRAME_DECODE_RATE",
    "FRAME_TILE_RATE",
    "MAX_COMPRESSED_SIZE",
    "MAX_FRAME_SYMBOLS",
    "TILE_SIZE_HEADER_RATE",
  };
  if (code > AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) {
    return "UNKNOWN";
  }
  return names[code];
}
