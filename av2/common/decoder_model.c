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
#include <string.h>

#include "avm_mem/avm_mem.h"

_Static_assert(sizeof(uint32_t) * CHAR_BIT == 32, "uint32_t must be 32 bits");
_Static_assert(sizeof(uint64_t) * CHAR_BIT == 64, "uint64_t must be 64 bits");
_Static_assert(sizeof(Av2DmUnsignedWide) * CHAR_BIT == 256,
               "Av2DmUnsignedWide must be 256 bits");

#define AV2_DM_WIDE_LIMBS 4
#define AV2_DM_PRODUCT_LIMBS (2 * AV2_DM_WIDE_LIMBS)

typedef struct Av2DmUnsignedProduct {
  uint64_t limbs[AV2_DM_PRODUCT_LIMBS];
} Av2DmUnsignedProduct;

static Av2DmUnsignedWide wide_from_u64(uint64_t value) {
  Av2DmUnsignedWide result = { { value, 0, 0, 0 } };
  return result;
}

static bool wide_is_zero(Av2DmUnsignedWide value) {
  for (uint32_t i = 0; i < AV2_DM_WIDE_LIMBS; ++i) {
    if (value.limbs[i] != 0) return false;
  }
  return true;
}

static bool wide_equals_u64(Av2DmUnsignedWide value, uint64_t expected) {
  return value.limbs[0] == expected && value.limbs[1] == 0 &&
         value.limbs[2] == 0 && value.limbs[3] == 0;
}

static bool wide_fits_u64(Av2DmUnsignedWide value) {
  return value.limbs[1] == 0 && value.limbs[2] == 0 && value.limbs[3] == 0;
}

static int wide_compare(Av2DmUnsignedWide left, Av2DmUnsignedWide right) {
  for (int i = AV2_DM_WIDE_LIMBS - 1; i >= 0; --i) {
    if (left.limbs[i] != right.limbs[i]) {
      return left.limbs[i] < right.limbs[i] ? -1 : 1;
    }
  }
  return 0;
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

AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK static bool wide_add(
    Av2DmUnsignedWide left, Av2DmUnsignedWide right,
    Av2DmUnsignedWide *result) {
  uint64_t carry = 0;
  for (uint32_t i = 0; i < AV2_DM_WIDE_LIMBS; ++i) {
    const uint64_t partial = left.limbs[i] + right.limbs[i];
    const uint64_t partial_carry = partial < left.limbs[i];
    const uint64_t sum = partial + carry;
    const uint64_t carry_carry = sum < partial;
    result->limbs[i] = sum;
    carry = partial_carry | carry_carry;
  }
  return carry == 0;
}

// Subtraction is modulo 2^256. Callers either establish left >= right or use
// the wraparound result as one step of long division with a 257th carry bit.
AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK static Av2DmUnsignedWide wide_subtract(
    Av2DmUnsignedWide left, Av2DmUnsignedWide right) {
  Av2DmUnsignedWide result;
  uint64_t borrow = 0;
  for (uint32_t i = 0; i < AV2_DM_WIDE_LIMBS; ++i) {
    const uint64_t partial = left.limbs[i] - right.limbs[i];
    const uint64_t partial_borrow = left.limbs[i] < right.limbs[i];
    result.limbs[i] = partial - borrow;
    const uint64_t borrow_borrow = partial < borrow;
    borrow = partial_borrow | borrow_borrow;
  }
  return result;
}

static uint64_t wide_get_bit(Av2DmUnsignedWide value, uint32_t bit_index) {
  return (value.limbs[bit_index / 64] >> (bit_index % 64)) & 1;
}

static void wide_set_bit(Av2DmUnsignedWide *value, uint32_t bit_index) {
  value->limbs[bit_index / 64] |= UINT64_C(1) << (bit_index % 64);
}

AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK static bool wide_shift_left_one(
    Av2DmUnsignedWide *value) {
  const bool overflow = (value->limbs[AV2_DM_WIDE_LIMBS - 1] >> 63) != 0;
  for (int i = AV2_DM_WIDE_LIMBS - 1; i > 0; --i) {
    value->limbs[i] = (value->limbs[i] << 1) | (value->limbs[i - 1] >> 63);
  }
  value->limbs[0] <<= 1;
  return overflow;
}

// Binary long division over the complete two-limb-independent representation.
static bool wide_divide(Av2DmUnsignedWide dividend, Av2DmUnsignedWide divisor,
                        Av2DmUnsignedWide *quotient,
                        Av2DmUnsignedWide *remainder) {
  if (wide_is_zero(divisor)) return false;
  if (wide_fits_u64(dividend) && wide_fits_u64(divisor)) {
    const uint64_t divisor_low = divisor.limbs[0];
    if (divisor_low == 0) return false;
    *quotient = wide_from_u64(dividend.limbs[0] / divisor_low);
    *remainder = wide_from_u64(dividend.limbs[0] % divisor_low);
    return true;
  }
  Av2DmUnsignedWide result = { { 0, 0, 0, 0 } };
  Av2DmUnsignedWide rem = { { 0, 0, 0, 0 } };
  for (int bit_index = 255; bit_index >= 0; --bit_index) {
    const bool overflow = wide_shift_left_one(&rem);
    rem.limbs[0] |= wide_get_bit(dividend, (uint32_t)bit_index);
    if (overflow || wide_compare(rem, divisor) >= 0) {
      rem = wide_subtract(rem, divisor);
      wide_set_bit(&result, (uint32_t)bit_index);
    }
  }
  *quotient = result;
  *remainder = rem;
  return true;
}

static Av2DmUnsignedWide wide_gcd(Av2DmUnsignedWide left,
                                  Av2DmUnsignedWide right) {
  if (wide_fits_u64(left) && wide_fits_u64(right)) {
    uint64_t a = left.limbs[0];
    uint64_t b = right.limbs[0];
    while (b != 0) {
      const uint64_t remainder = a % b;
      a = b;
      b = remainder;
    }
    return wide_from_u64(a);
  }
  while (!wide_is_zero(right)) {
    Av2DmUnsignedWide quotient;
    Av2DmUnsignedWide remainder;
    if (!wide_divide(left, right, &quotient, &remainder)) {
      return wide_from_u64(0);
    }
    left = right;
    right = remainder;
  }
  return left;
}

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

AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK static bool product_add_at(
    Av2DmUnsignedProduct *product, uint32_t index, uint64_t low,
    uint64_t high) {
  if (index >= AV2_DM_PRODUCT_LIMBS) return low == 0 && high == 0;
  const uint64_t old_low = product->limbs[index];
  product->limbs[index] += low;
  uint64_t carry = product->limbs[index] < old_low;
  ++index;
  if (index >= AV2_DM_PRODUCT_LIMBS) return high == 0 && carry == 0;

  const uint64_t old_high = product->limbs[index];
  product->limbs[index] += high;
  const uint64_t high_carry = product->limbs[index] < old_high;
  const uint64_t partial = product->limbs[index];
  product->limbs[index] += carry;
  const uint64_t carry_carry = product->limbs[index] < partial;
  carry = high_carry | carry_carry;
  ++index;
  while (carry != 0 && index < AV2_DM_PRODUCT_LIMBS) {
    ++product->limbs[index];
    carry = product->limbs[index] == 0;
    ++index;
  }
  return carry == 0;
}

#undef AV2_DM_NO_UNSIGNED_OVERFLOW_CHECK

static bool wide_multiply(Av2DmUnsignedWide left, Av2DmUnsignedWide right,
                          Av2DmUnsignedProduct *product) {
  memset(product, 0, sizeof(*product));
  for (uint32_t i = 0; i < AV2_DM_WIDE_LIMBS; ++i) {
    for (uint32_t j = 0; j < AV2_DM_WIDE_LIMBS; ++j) {
      uint64_t low;
      uint64_t high;
      multiply_64(left.limbs[i], right.limbs[j], &low, &high);
      if (!product_add_at(product, i + j, low, high)) return false;
    }
  }
  return true;
}

static int product_compare(Av2DmUnsignedProduct left,
                           Av2DmUnsignedProduct right) {
  for (int i = AV2_DM_PRODUCT_LIMBS - 1; i >= 0; --i) {
    if (left.limbs[i] != right.limbs[i]) {
      return left.limbs[i] < right.limbs[i] ? -1 : 1;
    }
  }
  return 0;
}

static bool product_to_wide(Av2DmUnsignedProduct product,
                            Av2DmUnsignedWide *result) {
  for (uint32_t i = AV2_DM_WIDE_LIMBS; i < AV2_DM_PRODUCT_LIMBS; ++i) {
    if (product.limbs[i] != 0) return false;
  }
  for (uint32_t i = 0; i < AV2_DM_WIDE_LIMBS; ++i) {
    result->limbs[i] = product.limbs[i];
  }
  return true;
}

static bool wide_multiply_checked(Av2DmUnsignedWide left,
                                  Av2DmUnsignedWide right,
                                  Av2DmUnsignedWide *result) {
  if (wide_fits_u64(left) && wide_fits_u64(right)) {
    memset(result, 0, sizeof(*result));
    multiply_64(left.limbs[0], right.limbs[0], &result->limbs[0],
                &result->limbs[1]);
    return true;
  }
  Av2DmUnsignedProduct product;
  return wide_multiply(left, right, &product) &&
         product_to_wide(product, result);
}

static bool rational_normalize(Av2DmRational *value) {
  if (wide_is_zero(value->denominator)) return false;
  if (wide_is_zero(value->magnitude)) {
    value->denominator = wide_from_u64(1);
    value->negative = false;
    return true;
  }

  const Av2DmUnsignedWide divisor =
      wide_gcd(value->magnitude, value->denominator);
  if (wide_is_zero(divisor)) return false;
  if (!wide_equals_u64(divisor, 1)) {
    Av2DmUnsignedWide remainder;
    Av2DmUnsignedWide reduced;
    if (!wide_divide(value->magnitude, divisor, &reduced, &remainder) ||
        !wide_is_zero(remainder)) {
      return false;
    }
    value->magnitude = reduced;
    if (!wide_divide(value->denominator, divisor, &reduced, &remainder) ||
        !wide_is_zero(remainder)) {
      return false;
    }
    value->denominator = reduced;
  }
  return true;
}

bool av2_dm_rational_make(uint64_t numerator, uint64_t denominator,
                          Av2DmRational *result) {
  return av2_dm_rational_make_wide(wide_from_u64(numerator), denominator, false,
                                   result);
}

bool av2_dm_rational_make_wide(Av2DmUnsignedWide numerator,
                               uint64_t denominator, bool negative,
                               Av2DmRational *result) {
  if (result == NULL || denominator == 0) return false;
  result->magnitude = numerator;
  result->denominator = wide_from_u64(denominator);
  result->negative = negative;
  return rational_normalize(result);
}

static bool rational_compare_magnitudes(const Av2DmRational *left,
                                        const Av2DmRational *right,
                                        int *comparison) {
  // Cancel factors common to both numerators and both denominators before
  // forming the exact cross-products. The products are retained in 512 bits.
  const Av2DmUnsignedWide numerator_gcd =
      wide_gcd(left->magnitude, right->magnitude);
  const Av2DmUnsignedWide denominator_gcd =
      wide_gcd(left->denominator, right->denominator);
  if (wide_is_zero(numerator_gcd) || wide_is_zero(denominator_gcd)) {
    return false;
  }
  Av2DmUnsignedWide left_numerator;
  Av2DmUnsignedWide right_numerator;
  Av2DmUnsignedWide left_denominator;
  Av2DmUnsignedWide right_denominator;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(left->magnitude, numerator_gcd, &left_numerator,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(right->magnitude, numerator_gcd, &right_numerator,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(left->denominator, denominator_gcd, &left_denominator,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(right->denominator, denominator_gcd, &right_denominator,
                   &remainder) ||
      !wide_is_zero(remainder)) {
    return false;
  }
  Av2DmUnsignedProduct left_product;
  Av2DmUnsignedProduct right_product;
  if (!wide_multiply(left_numerator, right_denominator, &left_product) ||
      !wide_multiply(right_numerator, left_denominator, &right_product)) {
    return false;
  }
  *comparison = product_compare(left_product, right_product);
  return true;
}

bool av2_dm_rational_add(const Av2DmRational *left, const Av2DmRational *right,
                         Av2DmRational *result) {
  if (left == NULL || right == NULL || result == NULL ||
      wide_is_zero(left->denominator) || wide_is_zero(right->denominator)) {
    return false;
  }
  Av2DmRational normalized_left = *left;
  Av2DmRational normalized_right = *right;
  if (!rational_normalize(&normalized_left) ||
      !rational_normalize(&normalized_right)) {
    return false;
  }

  const Av2DmUnsignedWide denominator_gcd =
      wide_gcd(normalized_left.denominator, normalized_right.denominator);
  if (wide_is_zero(denominator_gcd)) return false;
  Av2DmUnsignedWide left_multiplier;
  Av2DmUnsignedWide right_multiplier;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(normalized_right.denominator, denominator_gcd,
                   &left_multiplier, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(normalized_left.denominator, denominator_gcd,
                   &right_multiplier, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_multiply_checked(normalized_left.denominator, left_multiplier,
                             &result->denominator)) {
    return false;
  }
  Av2DmUnsignedWide scaled_left;
  Av2DmUnsignedWide scaled_right;
  if (!wide_multiply_checked(normalized_left.magnitude, left_multiplier,
                             &scaled_left) ||
      !wide_multiply_checked(normalized_right.magnitude, right_multiplier,
                             &scaled_right)) {
    return false;
  }

  if (normalized_left.negative == normalized_right.negative) {
    if (!wide_add(scaled_left, scaled_right, &result->magnitude)) return false;
    result->negative = normalized_left.negative;
  } else {
    const int comparison = wide_compare(scaled_left, scaled_right);
    if (comparison >= 0) {
      result->magnitude = wide_subtract(scaled_left, scaled_right);
      result->negative = normalized_left.negative;
    } else {
      result->magnitude = wide_subtract(scaled_right, scaled_left);
      result->negative = normalized_right.negative;
    }
  }
  return rational_normalize(result);
}

bool av2_dm_rational_subtract(const Av2DmRational *left,
                              const Av2DmRational *right,
                              Av2DmRational *result) {
  if (right == NULL) return false;
  Av2DmRational negated_right = *right;
  if (!wide_is_zero(negated_right.magnitude)) {
    negated_right.negative = !negated_right.negative;
  }
  return av2_dm_rational_add(left, &negated_right, result);
}

bool av2_dm_rational_multiply_u64(const Av2DmRational *value,
                                  uint64_t multiplier, Av2DmRational *result) {
  if (value == NULL || result == NULL || wide_is_zero(value->denominator)) {
    return false;
  }
  Av2DmRational normalized = *value;
  if (!rational_normalize(&normalized)) return false;
  const Av2DmUnsignedWide wide_multiplier = wide_from_u64(multiplier);
  const Av2DmUnsignedWide divisor =
      wide_gcd(wide_multiplier, normalized.denominator);
  Av2DmUnsignedWide reduced_multiplier;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(wide_multiplier, divisor, &reduced_multiplier, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(normalized.denominator, divisor, &normalized.denominator,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_multiply_checked(normalized.magnitude, reduced_multiplier,
                             &normalized.magnitude)) {
    return false;
  }
  *result = normalized;
  return rational_normalize(result);
}

bool av2_dm_rational_divide_u64(const Av2DmRational *value, uint64_t divisor,
                                Av2DmRational *result) {
  if (value == NULL || result == NULL || wide_is_zero(value->denominator) ||
      divisor == 0) {
    return false;
  }
  Av2DmRational normalized = *value;
  if (!rational_normalize(&normalized)) return false;
  const Av2DmUnsignedWide wide_divisor = wide_from_u64(divisor);
  const Av2DmUnsignedWide common_divisor =
      wide_gcd(normalized.magnitude, wide_divisor);
  Av2DmUnsignedWide reduced_divisor;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(normalized.magnitude, common_divisor, &normalized.magnitude,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(wide_divisor, common_divisor, &reduced_divisor,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_multiply_checked(normalized.denominator, reduced_divisor,
                             &normalized.denominator)) {
    return false;
  }
  *result = normalized;
  return rational_normalize(result);
}

bool av2_dm_rational_compare(const Av2DmRational *left,
                             const Av2DmRational *right, int *comparison) {
  if (left == NULL || right == NULL || comparison == NULL ||
      wide_is_zero(left->denominator) || wide_is_zero(right->denominator)) {
    return false;
  }
  Av2DmRational normalized_left = *left;
  Av2DmRational normalized_right = *right;
  if (!rational_normalize(&normalized_left) ||
      !rational_normalize(&normalized_right)) {
    return false;
  }
  if (wide_is_zero(normalized_left.magnitude) &&
      wide_is_zero(normalized_right.magnitude)) {
    *comparison = 0;
    return true;
  }
  if (wide_is_zero(normalized_left.magnitude)) {
    *comparison = normalized_right.negative ? 1 : -1;
    return true;
  }
  if (wide_is_zero(normalized_right.magnitude)) {
    *comparison = normalized_left.negative ? -1 : 1;
    return true;
  }
  if (normalized_left.negative != normalized_right.negative) {
    *comparison = normalized_left.negative ? -1 : 1;
    return true;
  }
  if (!rational_compare_magnitudes(&normalized_left, &normalized_right,
                                   comparison)) {
    return false;
  }
  if (normalized_left.negative) *comparison = -*comparison;
  return true;
}

bool av2_dm_rational_rebase(Av2DmRational *values, uint32_t value_count,
                            const Av2DmRational *origin) {
  if ((values == NULL && value_count != 0) || origin == NULL) return false;
  Av2DmRational fixed_origin = *origin;
  if (!rational_normalize(&fixed_origin)) return false;

  // Preflight every subtraction so arithmetic failure cannot leave the array
  // containing a mixture of old and new time origins.
  for (uint32_t i = 0; i < value_count; ++i) {
    Av2DmRational rebased;
    if (!av2_dm_rational_subtract(&values[i], &fixed_origin, &rebased)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < value_count; ++i) {
    Av2DmRational rebased;
    if (!av2_dm_rational_subtract(&values[i], &fixed_origin, &rebased)) {
      return false;
    }
    values[i] = rebased;
  }
  return true;
}

bool av2_dm_rational_is_zero(const Av2DmRational *value) {
  return value != NULL && !wide_is_zero(value->denominator) &&
         wide_is_zero(value->magnitude);
}

static void buffer_reset(Av2DmBuffer *buffer) {
  memset(buffer, 0, sizeof(*buffer));
  buffer->display_index = -1;
  av2_dm_rational_make(0, 1, &buffer->presentation_time);
  av2_dm_rational_make(0, 1, &buffer->decode_completion_time);
}

static bool valid_buffer_index(const Av2DmBufferPool *pool,
                               uint32_t buffer_index) {
  return pool != NULL && buffer_index < pool->pool_size;
}

bool av2_dm_buffer_pool_initialize(Av2DmBufferPool *pool,
                                   uint32_t num_ref_frames) {
  if (pool == NULL || num_ref_frames == 0 ||
      num_ref_frames > AV2_DM_MAX_REF_FRAMES) {
    return false;
  }
  memset(pool, 0, sizeof(*pool));
  pool->num_ref_frames = num_ref_frames;
  pool->pool_size = num_ref_frames + 2;
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) pool->vbi[i] = -1;
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    buffer_reset(&pool->buffers[i]);
  }
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
  if (!valid_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->decoder_ref_count != 0 || buffer->player_ref_count != 0) {
    return false;
  }
  buffer_reset(buffer);
  return true;
}

bool av2_dm_buffer_pool_add_decoder_ref(Av2DmBufferPool *pool,
                                        uint32_t buffer_index) {
  if (!valid_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->decoder_ref_count == UINT32_MAX) return false;
  ++buffer->decoder_ref_count;
  return true;
}

bool av2_dm_buffer_pool_remove_decoder_ref(Av2DmBufferPool *pool,
                                           uint32_t buffer_index) {
  if (!valid_buffer_index(pool, buffer_index)) return false;
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
  if (!valid_buffer_index(pool, buffer_index)) return false;
  Av2DmBuffer *const buffer = &pool->buffers[buffer_index];
  if (buffer->player_ref_count == UINT32_MAX) return false;
  ++buffer->player_ref_count;
  return true;
}

bool av2_dm_buffer_pool_remove_player_ref(Av2DmBufferPool *pool,
                                          uint32_t buffer_index) {
  if (!valid_buffer_index(pool, buffer_index)) return false;
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
      (buffer_index >= 0 && (uint32_t)buffer_index >= pool->pool_size)) {
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

typedef struct Av2DmLevelRow {
  uint64_t max_picture_size;
  uint32_t max_dimension;
  uint64_t max_display_rate;
  uint64_t max_decode_rate;
  uint32_t max_header_rate;
  uint32_t main_kbps;
  uint32_t high_kbps;
  uint32_t main_cr;
  uint32_t high_cr;
  uint32_t max_tiles;
  uint32_t max_tile_columns;
} Av2DmLevelRow;

// Annex A Tables A-2 and A-3. Integer kilobits per second preserve the table
// values exactly and deliberately avoid the encoder's legacy double table.
static const Av2DmLevelRow decoder_model_level_rows[22] = {
  { 147456, 640, 4423680, 5529600, 150, 1500, 0, 2, 0, 8, 4 },
  { 278784, 880, 8363520, 10454400, 150, 3000, 0, 2, 0, 8, 4 },
  { 665856, 1360, 19975680, 24969600, 150, 6000, 0, 2, 0, 16, 6 },
  { 1065024, 1720, 31950720, 39938400, 150, 10000, 0, 2, 0, 16, 6 },
  { 2359296, 2560, 70778880, 77856768, 300, 12000, 30000, 4, 4, 32, 8 },
  { 2359296, 2560, 141557760, 155713536, 300, 20000, 50000, 4, 4, 32, 8 },
  { 8912896, 4975, 267386880, 273715200, 300, 30000, 100000, 6, 4, 64, 8 },
  { 8912896, 4975, 534773760, 547430400, 300, 40000, 160000, 8, 4, 64, 8 },
  { 8912896, 4975, 1069547520, 1094860800, 300, 60000, 240000, 8, 4, 64, 8 },
  { 8912896, 4975, 1069547520, 1176502272, 300, 60000, 240000, 8, 4, 64, 8 },
  { 35651584, 9951, 1069547520, 1176502272, 300, 60000, 240000, 8, 4, 128, 16 },
  { 35651584, 9951, 2139095040, 2189721600, 300, 100000, 480000, 8, 4, 128,
    16 },
  { 35651584, 9951, 4278190080, 4379443200, 300, 160000, 800000, 8, 4, 128,
    16 },
  { 35651584, 9951, 4278190080, 4706009088, 300, 160000, 800000, 8, 4, 128,
    16 },
  { 142606336, 19902, 4278190080, 4706009088, 960, 160000, 800000, 8, 4, 256,
    32 },
  { 142606336, 19902, 8556380160, 8758886400, 960, 200000, 960000, 8, 4, 256,
    32 },
  { 142606336, 19902, 17112760320, 17517772800, 960, 320000, 1600000, 8, 4, 256,
    32 },
  { 142606336, 19902, 17112760320, 18824036352, 960, 320000, 1600000, 8, 4, 256,
    32 },
  { 530841600, 38400, 17112760320, 18824036352, 960, 320000, 1600000, 8, 4, 512,
    64 },
  { 530841600, 38400, 34225520640, 34910031052, 960, 400000, 1920000, 8, 4, 512,
    64 },
  { 530841600, 38400, 68451041280, 69820062105, 960, 640000, 3200000, 8, 4, 512,
    64 },
  { 530841600, 38400, 68451041280, 75296145408, 960, 640000, 3200000, 8, 4, 512,
    64 },
};

static const uint8_t decoder_model_tile_width_scale[2][22] = {
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8 },
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 16, 16, 16, 16 },
};

static const uint8_t decoder_model_tile_area_scale[2][22] = {
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 16, 16, 16, 16 },
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 16, 16, 16, 16, 32, 32, 32, 32 },
};

typedef struct Av2DmDfgRecord {
  uint64_t event_index;
  uint64_t temporal_unit_index;
  uint64_t generation;
  uint64_t coded_bits;
  uint64_t decode_order;
  uint64_t rap_epoch;
  uint64_t smoothing_epoch;
  Av2DmLevelLimits limits;
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
  uint64_t output_luma_samples;
  uint32_t output_frames;
  uint32_t frame_headers;
  bool header_complete;
  bool header_window_checked;
  bool header_rate_reported;
  bool tile_header_rate_reported;
  bool output_time_valid;
  bool presentation_time_valid;
  bool prior_presentation_interval_checked;
  Av2DmRational output_time;
  Av2DmRational presentation_time;
  uint64_t header_window_headers;
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

struct Av2DecoderModel {
  Av2DmConfig config;
  Av2DmLevelLimits limits;
  Av2DmRational decoder_buffer_delay;
  Av2DmRational encoder_buffer_delay;
  uint32_t decoder_buffer_delay_ticks;
  bool low_delay_mode;
  Av2DmRational dec_ct;
  Av2DmRational disp_ct;
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
  bool previous_dfg_valid;
  Av2DmDfgRecord previous_dfg;
  Av2DmTuRecord *tus;
  uint32_t tu_count;
  uint32_t tu_capacity;
  uint64_t frame_number;
  uint64_t shown_frame_number;
  uint64_t rap_epoch;
  uint64_t smoothing_epoch;
  bool smoothing_epoch_prepared;
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
  bool retired_unresolved_tu;
  Av2DmPendingOutputWitness pending_display_late;
  Av2DmPendingOutputWitness pending_decode_deadline;
  uint64_t maximum_tile_area;
  bool any_decode_count_two_requires_reserved_buffer;
  bool max_reference_frames_checked;
  bool max_reference_frames_reserved;
  bool max_reference_frames_violated;
  bool processing_stopped;
  uint64_t model_events;
  bool violation_seen[AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE + 1];
  Av2DmStorageStats storage;
};

static bool invalidate_lane_reference_buffers(Av2DmLane *lane,
                                              uint32_t ref_valid_mask);
static void check_smoothing_buffer_overflow(Av2DecoderModel *model,
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
static void retire_unresolvable_tus(Av2DecoderModel *model);
static void restart_tu_history(Av2DecoderModel *model,
                               uint64_t temporal_unit_index);
static bool update_latest_timed_tu(Av2DecoderModel *model,
                                   const Av2DmRational *output_time);

static bool rational_zero(Av2DmRational *value) {
  return av2_dm_rational_make(0, 1, value);
}

static bool rational_from_product(uint64_t left, uint64_t right,
                                  Av2DmRational *value) {
  Av2DmUnsignedWide product;
  if (!wide_multiply_checked(wide_from_u64(left), wide_from_u64(right),
                             &product)) {
    return false;
  }
  return av2_dm_rational_make_wide(product, 1, false, value);
}

static bool rational_multiply(const Av2DmRational *left,
                              const Av2DmRational *right,
                              Av2DmRational *result) {
  if (left == NULL || right == NULL || result == NULL ||
      wide_is_zero(left->denominator) || wide_is_zero(right->denominator)) {
    return false;
  }
  Av2DmRational a = *left;
  Av2DmRational b = *right;
  if (!rational_normalize(&a) || !rational_normalize(&b)) return false;
  const Av2DmUnsignedWide cross_a = wide_gcd(a.magnitude, b.denominator);
  const Av2DmUnsignedWide cross_b = wide_gcd(b.magnitude, a.denominator);
  Av2DmUnsignedWide remainder;
  if (!wide_divide(a.magnitude, cross_a, &a.magnitude, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(b.denominator, cross_a, &b.denominator, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(b.magnitude, cross_b, &b.magnitude, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(a.denominator, cross_b, &a.denominator, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_multiply_checked(a.magnitude, b.magnitude, &result->magnitude) ||
      !wide_multiply_checked(a.denominator, b.denominator,
                             &result->denominator)) {
    return false;
  }
  result->negative = a.negative != b.negative;
  return rational_normalize(result);
}

static bool rational_max(const Av2DmRational *left, const Av2DmRational *right,
                         Av2DmRational *result) {
  int comparison;
  if (!av2_dm_rational_compare(left, right, &comparison)) return false;
  *result = comparison >= 0 ? *left : *right;
  return true;
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
  if (count < *capacity) return true;
  const uint32_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
  if (new_capacity < *capacity || element_size > SIZE_MAX / new_capacity) {
    return false;
  }
  void *const replacement = avm_calloc(new_capacity, element_size);
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
  if (limits == NULL || level_idx >= 22 || tier > 1 || profile > 4) {
    return false;
  }
  const Av2DmLevelRow *const row = &decoder_model_level_rows[level_idx];
  const uint32_t kbps = tier == 0 ? row->main_kbps : row->high_kbps;
  const uint32_t compression = tier == 0 ? row->main_cr : row->high_cr;
  if (kbps == 0 || compression == 0) return false;

  uint32_t profile_numerator = 1;
  uint32_t profile_denominator = 1;
  uint32_t picture_factor = 15;
  if (profile == 3) {
    profile_numerator = 1667;
    profile_denominator = 1000;
    picture_factor = 20;
  } else if (profile == 4) {
    profile_numerator = 5;
    profile_denominator = 2;
    picture_factor = 30;
  }

  memset(limits, 0, sizeof(*limits));
  limits->max_picture_size = row->max_picture_size;
  limits->max_horizontal_size = row->max_dimension;
  limits->max_vertical_size = row->max_dimension;
  limits->max_display_rate = row->max_display_rate;
  limits->max_decode_rate = row->max_decode_rate;
  limits->max_header_rate = row->max_header_rate;
  limits->max_tiles = row->max_tiles;
  limits->max_tile_columns = row->max_tile_columns;
  limits->max_tile_width =
      (uint64_t)decoder_model_tile_width_scale[tier][level_idx] * 4096 / 4;
  limits->max_tile_area =
      (uint64_t)decoder_model_tile_area_scale[tier][level_idx] * 4096 * 2304 /
      4;
  limits->max_tile_size_header_rate_product =
      (uint64_t)decoder_model_tile_area_scale[tier][level_idx] * 547430400 / 4;
  limits->picture_size_profile_factor = picture_factor;
  limits->min_compression_basis = compression;

  Av2DmRational base_rate;
  Av2DmRational profile_factor;
  if (!rational_from_product(kbps, 1000, &base_rate) ||
      !av2_dm_rational_make(profile_numerator, profile_denominator,
                            &profile_factor) ||
      !rational_multiply(&base_rate, &profile_factor, &limits->bit_rate)) {
    return false;
  }
  // Annex A defines MaxBufferSize as one second of MaxBitrate.
  limits->buffer_size = limits->bit_rate;
  return true;
}

typedef struct Av2DmSubstreamRow {
  uint32_t max_horizontal_size;
  uint32_t max_vertical_size;
  uint32_t max_tile_columns;
} Av2DmSubstreamRow;

static const Av2DmSubstreamRow decoder_model_substream_rows[5][3] = {
  { { 896, 1600, 7 }, { 576, 960, 4 }, { 384, 640, 3 } },
  { { 1472, 2560, 7 }, { 1088, 1920, 4 }, { 768, 1280, 3 } },
  { { 2280, 5120, 13 }, { 2176, 3840, 8 }, { 1472, 2560, 5 } },
  { { 5760, 10240, 26 }, { 4320, 7680, 16 }, { 2880, 5120, 11 } },
  { { 11520, 20480, 52 }, { 8640, 15360, 32 }, { 5760, 10240, 21 } },
};

static bool scaled_integer(uint64_t value, uint32_t scale_numerator,
                           uint32_t scale_denominator, uint64_t *scaled) {
  Av2DmRational rational;
  if (!av2_dm_rational_make(value, 1, &rational) ||
      !av2_dm_rational_multiply_u64(&rational, scale_denominator, &rational) ||
      !av2_dm_rational_divide_u64(&rational, scale_numerator, &rational) ||
      rational.negative || !wide_equals_u64(rational.denominator, 1) ||
      rational.magnitude.limbs[1] != 0 || rational.magnitude.limbs[2] != 0 ||
      rational.magnitude.limbs[3] != 0) {
    return false;
  }
  *scaled = rational.magnitude.limbs[0];
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
  uint32_t scale_index;
  if (scale_numerator == 3 && scale_denominator == 2) {
    scale_index = 0;
  } else if (scale_numerator == 4 && scale_denominator == 1) {
    scale_index = 1;
  } else if (scale_numerator == 9 && scale_denominator == 1) {
    scale_index = 2;
  } else {
    return false;
  }
  const uint32_t group =
      level_idx < 6 ? 0 : (level_idx < 10 ? 1 : (level_idx - 10) / 4 + 2);
  if (group >= 5) return false;
  const Av2DmSubstreamRow *const row =
      &decoder_model_substream_rows[group][scale_index];
  Av2DmLevelLimits multistream;
  if (!av2_dm_get_level_limits(level_idx, tier, profile, &multistream)) {
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
    return false;
  }
  multistream.max_picture_size =
      (uint64_t)row->max_horizontal_size * row->max_vertical_size;
  multistream.max_horizontal_size = row->max_horizontal_size;
  multistream.max_vertical_size = row->max_vertical_size;
  multistream.max_display_rate = scaled_display;
  multistream.max_decode_rate = scaled_decode;
  multistream.max_header_rate = 132;
  multistream.max_tiles = (uint32_t)((uint64_t)multistream.max_tiles *
                                     scale_denominator / scale_numerator);
  multistream.max_tile_columns = row->max_tile_columns;

#define MIN_LIMIT(member)                    \
  do {                                       \
    if (multistream.member < limits->member) \
      limits->member = multistream.member;   \
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
  if (!av2_dm_rational_compare(&multistream.bit_rate, &limits->bit_rate,
                               &comparison)) {
    return false;
  }
  if (comparison < 0) limits->bit_rate = multistream.bit_rate;
  if (!av2_dm_rational_compare(&multistream.buffer_size, &limits->buffer_size,
                               &comparison)) {
    return false;
  }
  if (comparison < 0) limits->buffer_size = multistream.buffer_size;
  if (multistream.min_compression_basis > limits->min_compression_basis) {
    limits->min_compression_basis = multistream.min_compression_basis;
  }
  return true;
}

static void update_result_status(Av2DecoderModel *model) {
  if (model->result.applicability == AV2_DM_NOT_APPLICABLE) {
    model->result.status = AV2_DM_RESULT_NOT_APPLICABLE;
  } else if (model->result.violations != 0) {
    model->result.status = AV2_DM_RESULT_NON_CONFORMANT;
  } else if (model->result.arithmetic_failed ||
             model->result.missing_required_input ||
             model->result.applicability == AV2_DM_MISSING_REQUIRED_INPUT) {
    model->result.status = AV2_DM_RESULT_INDETERMINATE;
  } else {
    model->result.status = AV2_DM_RESULT_CONFORMANT;
  }
}

static void arithmetic_failure(Av2DecoderModel *model) {
  model->result.arithmetic_failed = true;
  model->processing_stopped = true;
  update_result_status(model);
}

void av2_decoder_model_fail_arithmetic_for_testing(Av2DecoderModel *model) {
  if (model != NULL) arithmetic_failure(model);
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
    memset(&violation, 0, sizeof(violation));
    violation.code = code;
    violation.scope = model->config.scope;
    violation.event_index = event_index;
    violation.affected_kind = affected_kind;
    violation.affected_index = affected_index;
    violation.observed_present = observed != NULL;
    violation.limit_present = limit != NULL;
    if (observed != NULL) violation.observed = *observed;
    if (limit != NULL) violation.limit = *limit;
    if (detail != NULL) violation.detail = *detail;
    model->report(model->report_opaque, &violation);
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
  memset(lane, 0, sizeof(*lane));
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
    if (!av2_dm_buffer_pool_set_vbi(&lane->pool, seed->ref_index,
                                    buffer_index)) {
      return false;
    }
  }
  return true;
}

static bool resolve_parameters(const Av2DmConfig *config,
                               Av2DmResolvedParameters *parameters) {
  memset(parameters, 0, sizeof(*parameters));
  if (config->level_limits_present) {
    parameters->limits = config->level_limits;
  } else if (!av2_dm_get_level_limits(config->level_idx, config->tier,
                                      config->profile, &parameters->limits)) {
    return false;
  }
  if (!rational_normalize(&parameters->limits.bit_rate) ||
      !rational_normalize(&parameters->limits.buffer_size) ||
      wide_is_zero(parameters->limits.bit_rate.magnitude) ||
      parameters->limits.max_decode_rate == 0 ||
      parameters->limits.max_display_rate == 0 ||
      parameters->limits.max_header_rate == 0 ||
      parameters->limits.picture_size_profile_factor == 0 ||
      parameters->limits.min_compression_basis == 0 ||
      !config->timing_info_present || config->time_scale == 0 ||
      config->num_units_in_display_tick == 0 ||
      (config->mode == AV2_DM_DECODING_SCHEDULE_MODE &&
       config->num_units_in_decoding_tick == 0) ||
      (config->equal_picture_interval && config->ticks_per_picture == 0) ||
      config->initial_display_delay == 0 ||
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
    } else {
      return false;
    }
  } else if (!config->equal_picture_interval) {
    return false;
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

static void apply_parameters(Av2DecoderModel *model, const Av2DmConfig *config,
                             const Av2DmResolvedParameters *parameters) {
  model->config = *config;
  model->limits = parameters->limits;
  model->decoder_buffer_delay = parameters->decoder_buffer_delay;
  model->encoder_buffer_delay = parameters->encoder_buffer_delay;
  model->decoder_buffer_delay_ticks = parameters->decoder_buffer_delay_ticks;
  model->low_delay_mode = parameters->low_delay_mode;
  model->dec_ct = parameters->dec_ct;
  model->disp_ct = parameters->disp_ct;
}

Av2DecoderModel *av2_decoder_model_create(const Av2DmConfig *config,
                                          Av2DmReportFn report,
                                          void *report_opaque) {
  if (config == NULL) return NULL;
  Av2DecoderModel *const model = avm_calloc(1, sizeof(*model));
  if (model == NULL) return NULL;
  model->config = *config;
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
  Av2DmResolvedParameters parameters;
  if (!resolve_parameters(config, &parameters)) {
    missing_input(model);
  } else {
    apply_parameters(model, config, &parameters);
  }

  if (config->ras_start) {
    if (!config->ras_seed_complete || !seed_ras_buffers(model, &model->lane) ||
        !seed_ras_buffers(model, &model->resource_lane)) {
      // DM-SPEC-6: a RAS run is provable only when all established long-term
      // slot/generation relationships can be reconstructed.
      missing_input(model);
    }
  }
  update_result_status(model);
  update_storage_stats(model);
  return model;
}

void av2_decoder_model_destroy(Av2DecoderModel *model) {
  if (model == NULL) return;
  avm_free(model->dfgs);
  avm_free(model->tus);
  avm_free(model);
}

static bool rational_multiply_wide(const Av2DmRational *value,
                                   Av2DmUnsignedWide multiplier,
                                   Av2DmRational *result) {
  if (value == NULL || result == NULL || wide_is_zero(value->denominator)) {
    return false;
  }
  Av2DmRational normalized = *value;
  if (!rational_normalize(&normalized)) return false;
  const Av2DmUnsignedWide divisor =
      wide_gcd(multiplier, normalized.denominator);
  Av2DmUnsignedWide remainder;
  if (!wide_divide(multiplier, divisor, &multiplier, &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_divide(normalized.denominator, divisor, &normalized.denominator,
                   &remainder) ||
      !wide_is_zero(remainder) ||
      !wide_multiply_checked(normalized.magnitude, multiplier,
                             &normalized.magnitude)) {
    return false;
  }
  *result = normalized;
  return rational_normalize(result);
}

static bool rational_ceil_ratio_to_tick(const Av2DmRational *time,
                                        const Av2DmRational *tick,
                                        Av2DmRational *result) {
  if (time->negative || tick->negative || wide_is_zero(tick->magnitude)) {
    return false;
  }
  Av2DmRational reciprocal;
  reciprocal.magnitude = tick->denominator;
  reciprocal.denominator = tick->magnitude;
  reciprocal.negative = false;
  Av2DmRational ratio;
  if (!rational_multiply(time, &reciprocal, &ratio)) return false;
  Av2DmUnsignedWide quotient;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(ratio.magnitude, ratio.denominator, &quotient, &remainder)) {
    return false;
  }
  if (!wide_is_zero(remainder)) {
    const Av2DmUnsignedWide one = wide_from_u64(1);
    if (!wide_add(quotient, one, &quotient)) return false;
  }
  return rational_multiply_wide(tick, quotient, result);
}

static bool rational_ceil_to_integer(const Av2DmRational *value,
                                     Av2DmRational *result) {
  if (value == NULL || result == NULL || wide_is_zero(value->denominator)) {
    return false;
  }
  Av2DmUnsignedWide quotient;
  Av2DmUnsignedWide remainder;
  if (!wide_divide(value->magnitude, value->denominator, &quotient,
                   &remainder)) {
    return false;
  }
  if (!value->negative && !wide_is_zero(remainder)) {
    if (!wide_add(quotient, wide_from_u64(1), &quotient)) return false;
  }
  return av2_dm_rational_make_wide(quotient, 1, value->negative, result);
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
  detail.value.buffer_pool.frames_in_use =
      av2_dm_buffer_pool_frames_in_use(pool);
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
  memset(tu, 0, sizeof(*tu));
  tu->temporal_unit_index = temporal_unit_index;
  tu->event_index = event_index;
  return tu;
}

static bool update_latest_timed_tu(Av2DecoderModel *model,
                                   const Av2DmRational *output_time) {
  if (!model->latest_timed_tu_valid) {
    model->latest_timed_tu_output_time = *output_time;
    model->latest_timed_tu_valid = true;
    return true;
  }
  int comparison;
  if (!av2_dm_rational_compare(output_time, &model->latest_timed_tu_output_time,
                               &comparison)) {
    return false;
  }
  if (comparison > 0) model->latest_timed_tu_output_time = *output_time;
  return true;
}

static void check_static_level_limits(Av2DecoderModel *model,
                                      const Av2DmFrameEvent *event) {
  Av2DmRational observed;
  Av2DmRational limit;
  if (!rational_from_product(event->frame_width, event->frame_height,
                             &observed) ||
      !av2_dm_rational_make(model->limits.max_picture_size, 1, &limit)) {
    arithmetic_failure(model);
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
    av2_dm_rational_make(event->frame_width, 1, &observed);
    av2_dm_rational_make(16, 1, &limit);
    report_violation(model, AV2_DM_VIOLATION_MIN_HORIZONTAL_SIZE,
                     event->event_index, &observed, &limit);
  }
  if (event->frame_height < 16) {
    av2_dm_rational_make(event->frame_height, 1, &observed);
    av2_dm_rational_make(16, 1, &limit);
    report_violation(model, AV2_DM_VIOLATION_MIN_VERTICAL_SIZE,
                     event->event_index, &observed, &limit);
  }
  if (!event->non_rightmost_tile_width_valid) {
    report_violation(model, AV2_DM_VIOLATION_MIN_TILE_WIDTH, event->event_index,
                     NULL, NULL);
  }
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
    *removal = model->decoder_buffer_delay;
    return true;
  }
  if (!release_presented_buffers(lane, &lane->time)) return false;
  if (av2_dm_buffer_pool_get_free_buffer(&lane->pool) >= 0) {
    *removal = lane->time;
    return true;
  }
  bool found = false;
  Av2DmRational earliest;
  for (uint32_t i = 0; i < lane->pool.pool_size; ++i) {
    const Av2DmBuffer *const buffer = &lane->pool.buffers[i];
    if (buffer->decoder_ref_count != 0 || buffer->player_ref_count == 0) {
      continue;
    }
    if (!buffer->presentation_time_valid) {
      missing_input(model);
      return false;
    }
    if (!found) {
      earliest = buffer->presentation_time;
      found = true;
    } else {
      bool less;
      if (!rational_less(&buffer->presentation_time, &earliest, &less)) {
        return false;
      }
      if (less) earliest = buffer->presentation_time;
    }
  }
  if (!found) return false;
  *removal = earliest;
  return true;
}

static bool lane_start_decode(Av2DmLane *lane, const Av2DmRational *removal,
                              const Av2DmRational *decode_time,
                              uint64_t generation, int32_t *buffer_index) {
  if (!release_presented_buffers(lane, removal)) return false;
  lane->time = *removal;
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
  buffer->decode_completion_time = lane->time;
  buffer->decode_completion_time_valid = true;
  return true;
}

static bool calculate_decode_time(Av2DecoderModel *model,
                                  const Av2DmFrameEvent *event,
                                  uint64_t *luma_samples,
                                  Av2DmRational *decode_time) {
  uint64_t samples;
  if (event->frame_is_intra) {
    Av2DmRational product;
    if (!rational_from_product(event->frame_width, event->frame_height,
                               &product) ||
        product.magnitude.limbs[1] != 0 || product.magnitude.limbs[2] != 0 ||
        product.magnitude.limbs[3] != 0) {
      return false;
    }
    samples = product.magnitude.limbs[0];
    if (event->allow_global_intrabc && event->inloop_filtering_enabled) {
      if (samples > UINT64_MAX / 2) return false;
      samples *= 2;
    }
  } else {
    Av2DmRational product;
    if (!rational_from_product(model->config.max_frame_width,
                               model->config.max_frame_height, &product) ||
        product.magnitude.limbs[1] != 0 || product.magnitude.limbs[2] != 0 ||
        product.magnitude.limbs[3] != 0) {
      return false;
    }
    samples = product.magnitude.limbs[0];
  }
  *luma_samples = samples;
  return av2_dm_rational_make(samples, model->limits.max_decode_rate,
                              decode_time);
}

static void check_frame_parsing_constraints(Av2DecoderModel *model,
                                            Av2DmDfgRecord *dfg,
                                            const Av2DmRational *interval,
                                            uint64_t proving_event_index) {
  if (model->config.still_picture) return;
  Av2DmViolationDetail detail;
  memset(&detail, 0, sizeof(detail));
  detail.kind = AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL;
  detail.value.frame_interval = *interval;
  const Av2DmLevelLimits *const limits = &dfg->limits;
  Av2DmRational limit;
  Av2DmRational observed;
  if (!av2_dm_rational_multiply_u64(interval, limits->max_decode_rate,
                                    &limit) ||
      !av2_dm_rational_make(dfg->luma_samples, 1, &observed)) {
    arithmetic_failure(model);
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_FRAME_DECODE_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);

  Av2DmRational dynamic_tiles;
  Av2DmRational one;
  Av2DmRational max_tiles;
  if (!av2_dm_rational_multiply_u64(interval, (uint64_t)limits->max_tiles * 120,
                                    &dynamic_tiles) ||
      !av2_dm_rational_make(1, 1, &one) ||
      !av2_dm_rational_make(limits->max_tiles, 1, &max_tiles) ||
      !rational_max(&dynamic_tiles, &one, &dynamic_tiles)) {
    arithmetic_failure(model);
    return;
  }
  bool greater;
  if (!rational_greater(&dynamic_tiles, &max_tiles, &greater)) {
    arithmetic_failure(model);
    return;
  }
  if (greater) dynamic_tiles = max_tiles;
  if (!av2_dm_rational_make(dfg->num_tiles, 1, &observed)) {
    arithmetic_failure(model);
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_FRAME_TILE_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed,
      &dynamic_tiles, &detail);

  Av2DmRational compressed_limit_1;
  Av2DmRational compressed_limit_2;
  if (dfg->luma_samples > UINT64_MAX / limits->picture_size_profile_factor) {
    arithmetic_failure(model);
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
    return;
  }
  bool first_is_greater;
  if (!rational_greater(&compressed_limit_1, &compressed_limit_2,
                        &first_is_greater)) {
    arithmetic_failure(model);
    return;
  }
  limit = first_is_greater ? compressed_limit_2 : compressed_limit_1;
  if (!av2_dm_rational_make(dfg->compressed_size, 1, &observed)) {
    arithmetic_failure(model);
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);

  Av2DmRational symbol_factor_a;
  Av2DmRational symbol_factor_b;
  Av2DmRational symbol_factor;
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
    return;
  }
  compare_upper_limit_for_affected_with_detail(
      model, AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_DFG, dfg->event_index, &observed, &limit,
      &detail);
}

static void check_previous_dfg_interval(Av2DecoderModel *model,
                                        Av2DmDfgRecord *previous,
                                        const Av2DmDfgRecord *current) {
  Av2DmRational interval;
  if (!av2_dm_rational_subtract(&current->removal, &previous->removal,
                                &interval) ||
      !av2_dm_rational_divide_u64(&interval, previous->decode_count_two ? 2 : 1,
                                  &interval)) {
    arithmetic_failure(model);
    return;
  }
  model->last_frame_parsing_time = interval;
  model->last_frame_parsing_time_valid = true;
  // The previous DFG retains the affected frame/generation identity; the
  // current DFG supplies the removal interval that proves these constraints.
  check_frame_parsing_constraints(model, previous, &interval,
                                  current->event_index);

  if (previous->mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    Av2DmRational available;
    Av2DmRational one_header_time;
    Av2DmRational required;
    const uint64_t max_headers = (uint64_t)previous->limits.max_header_rate *
                                 (1 + ((uint64_t)previous->tier << 1));
    if (!av2_dm_rational_subtract(&current->scheduled_removal,
                                  &previous->removal, &available) ||
        !av2_dm_rational_make(1, max_headers, &one_header_time) ||
        !rational_max(&previous->decode_time, &one_header_time, &required)) {
      arithmetic_failure(model);
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
  }
}

static bool calculate_arrival_times(Av2DecoderModel *model,
                                    Av2DmDfgRecord *dfg) {
  if (model->dfg_number == 1 || dfg->parameters_updated) {
    if (!rational_zero(&dfg->first_arrival)) return false;
  } else {
    if (!model->previous_dfg_valid) return false;
    Av2DmRational total_delay;
    Av2DmRational latest;
    if (!av2_dm_rational_add(&model->encoder_buffer_delay,
                             &model->decoder_buffer_delay, &total_delay) ||
        !av2_dm_rational_subtract(&dfg->scheduled_removal, &total_delay,
                                  &latest) ||
        !rational_max(&model->previous_dfg.last_arrival, &latest,
                      &dfg->first_arrival)) {
      return false;
    }
  }
  Av2DmRational coded_bits;
  Av2DmRational reciprocal_rate;
  Av2DmRational arrival_duration;
  if (wide_is_zero(model->limits.bit_rate.magnitude) ||
      !av2_dm_rational_make(dfg->coded_bits, 1, &coded_bits)) {
    return false;
  }
  reciprocal_rate.magnitude = model->limits.bit_rate.denominator;
  reciprocal_rate.denominator = model->limits.bit_rate.magnitude;
  reciprocal_rate.negative = false;
  return rational_multiply(&coded_bits, &reciprocal_rate, &arrival_duration) &&
         av2_dm_rational_add(&dfg->first_arrival, &arrival_duration,
                             &dfg->last_arrival);
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
    dfg->scheduled_removal = model->decoder_buffer_delay;
    return true;
  }
  if (!model->most_recent_rap_removal_valid) {
    missing_input(model);
    return false;
  }
  Av2DmRational offset;
  return av2_dm_rational_multiply_u64(&model->dec_ct,
                                      event->buffer_removal_time, &offset) &&
         av2_dm_rational_add(&model->most_recent_rap_scheduled_removal, &offset,
                             &dfg->scheduled_removal);
}

static void check_schedule_delay_limits(Av2DecoderModel *model,
                                        uint64_t event_index) {
  if (model->dfg_number != 1 ||
      model->config.mode != AV2_DM_DECODING_SCHEDULE_MODE) {
    return;
  }
  Av2DmRational zero;
  rational_zero(&zero);
  if (av2_dm_rational_is_zero(&model->decoder_buffer_delay)) {
    report_violation(model, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_ZERO,
                     event_index, &model->decoder_buffer_delay, &zero);
  }
  Av2DmRational reciprocal_rate;
  Av2DmRational maximum_delay;
  reciprocal_rate.magnitude = model->limits.bit_rate.denominator;
  reciprocal_rate.denominator = model->limits.bit_rate.magnitude;
  reciprocal_rate.negative = false;
  if (!rational_multiply(&model->limits.buffer_size, &reciprocal_rate,
                         &maximum_delay)) {
    arithmetic_failure(model);
    return;
  }
  compare_upper_limit(model, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_TOO_LARGE,
                      event_index, &model->decoder_buffer_delay,
                      &maximum_delay);
}

static void check_delay_consistency(Av2DecoderModel *model,
                                    Av2DmDfgRecord *dfg) {
  if (model->config.mode != AV2_DM_DECODING_SCHEDULE_MODE ||
      !dfg->random_access_point || !model->previous_dfg_valid) {
    return;
  }
  Av2DmRational time_delta;
  Av2DmRational threshold;
  if (!av2_dm_rational_subtract(&dfg->scheduled_removal,
                                &model->previous_dfg.last_arrival,
                                &time_delta) ||
      !av2_dm_rational_multiply_u64(&time_delta, 90000, &time_delta) ||
      model->decoder_buffer_delay_ticks == 0 ||
      !av2_dm_rational_make(model->decoder_buffer_delay_ticks - 1, 1,
                            &threshold)) {
    arithmetic_failure(model);
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
  }
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
  prepared->values[prepared->count] = *target;
  ++prepared->count;
  return true;
}

static bool prepare_lane_rebase(Av2DecoderModel *model, Av2DmLane *lane,
                                bool primary, const Av2DmRational *origin,
                                Av2DmPreparedRebase *prepared) {
  uint64_t capacity = 2 + 2 * lane->pool.pool_size;
  if (primary) {
    capacity += (uint64_t)5 * model->dfg_count + 16;
  }
  if (capacity > UINT32_MAX ||
      capacity > SIZE_MAX / sizeof(*prepared->values)) {
    return false;
  }
  prepared->targets = avm_calloc((size_t)capacity, sizeof(*prepared->targets));
  prepared->values = avm_calloc((size_t)capacity, sizeof(*prepared->values));
  if (prepared->targets == NULL || prepared->values == NULL) return false;
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
  avm_free(prepared->targets);
  avm_free(prepared->values);
  memset(prepared, 0, sizeof(*prepared));
}

static void commit_prepared_rebase(const Av2DmPreparedRebase *prepared) {
  for (uint32_t i = 0; i < prepared->count; ++i) {
    *prepared->targets[i] = prepared->values[i];
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
  const Av2DmRational origin = model->lane.time;
  if (!prepare_lane_rebase(model, &model->lane, true, &origin, &primary) ||
      !prepare_lane_rebase(model, &model->resource_lane, false, &origin,
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

void av2_decoder_model_start_frame(Av2DecoderModel *model,
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
  model->latest_frame_event_index = event->event_index;
  // Annex E synchronizes VBI with RefValid at every start_frame_decode(),
  // before FrameNum advances or a current buffer is selected.
  if (!invalidate_lane_reference_buffers(&model->lane, event->ref_valid_mask) ||
      !invalidate_lane_reference_buffers(&model->resource_lane,
                                         event->ref_valid_mask)) {
    arithmetic_failure(model);
    return;
  }
  ++model->frame_number;
  if (model->coded_tu_valid && model->coded_tu != event->temporal_unit_index) {
    Av2DmTuRecord *const previous_coded = find_tu(model, model->coded_tu);
    if (previous_coded == NULL) {
      arithmetic_failure(model);
      return;
    }
    previous_coded->header_complete = true;
  }
  model->coded_tu = event->temporal_unit_index;
  model->coded_tu_valid = true;
  Av2DmTuRecord *const tu =
      get_tu(model, event->temporal_unit_index, event->event_index);
  if (tu != NULL && event->temporal_unit_output_time_present) {
    tu->output_time = event->temporal_unit_output_time;
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
  if (model->smoothing_epoch_prepared &&
      !event->decoder_model_parameters_updated) {
    arithmetic_failure(model);
    return;
  }
  if (event->decoder_model_parameters_updated && model->dfg_number != 0 &&
      !model->smoothing_epoch_prepared) {
    // FirstBitArrival restarts at zero when decoder-model parameters change.
    // Close the prior smoothing epoch before accepting the new epoch so its
    // occupancy cannot be combined with the reset timeline.
    if (!model->config.defer_nonterminal_checks_for_testing) {
      check_smoothing_buffer_overflow(model, event->event_index);
      model->dfg_count = 0;
    }
    if (model->smoothing_epoch == UINT64_MAX) {
      arithmetic_failure(model);
      return;
    }
    ++model->smoothing_epoch;
  }
  model->smoothing_epoch_prepared = false;
  if (model->dfg_count == UINT32_MAX || model->dfg_number == UINT64_MAX ||
      (event->random_access_point && model->rap_epoch == UINT64_MAX) ||
      !grow_array((void **)&model->dfgs, &model->dfg_capacity, model->dfg_count,
                  sizeof(*model->dfgs))) {
    arithmetic_failure(model);
    return;
  }
  Av2DmDfgRecord *const dfg = &model->dfgs[model->dfg_count++];
  memset(dfg, 0, sizeof(*dfg));
  dfg->event_index = event->event_index;
  dfg->temporal_unit_index = event->temporal_unit_index;
  dfg->generation = event->generation;
  dfg->coded_bits = event->coded_bits;
  dfg->decode_order = model->result.decoded_frames;
  dfg->smoothing_epoch = model->smoothing_epoch;
  dfg->limits = model->limits;
  dfg->tier = model->config.tier;
  dfg->mode = model->config.mode;
  dfg->random_access_point = event->random_access_point;
  dfg->parameters_updated = event->decoder_model_parameters_updated;
  dfg->count_frame_header = event->count_frame_header;
  dfg->decode_count_two =
      event->allow_global_intrabc && event->inloop_filtering_enabled;
  dfg->coded_as_closed_loop_key = event->coded_as_closed_loop_key;
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
  dfg->removal = dfg->scheduled_removal;
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
             !rational_ceil_ratio_to_tick(&dfg->last_arrival, &model->dec_ct,
                                          &dfg->removal)) {
    arithmetic_failure(model);
    return;
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_smoothing_buffer_overflow(model, event->event_index);
  }
  if (model->processing_stopped) return;

  Av2DmRational resource_removal;
  if (!next_resource_removal(model, &model->resource_lane,
                             model->dfg_number - 1, &resource_removal)) {
    if (!model->result.missing_required_input) arithmetic_failure(model);
    return;
  }
  int32_t resource_buffer_index;
  if (!lane_start_decode(&model->resource_lane, &resource_removal,
                         &dfg->decode_time, event->generation,
                         &resource_buffer_index)) {
    arithmetic_failure(model);
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
  dfg->decode_completion = model->lane.time;
  if (buffer_index >= 0) {
    Av2DmBuffer *const buffer = &model->lane.pool.buffers[buffer_index];
    buffer->decode_order = dfg->decode_order;
    buffer->rap_epoch = dfg->rap_epoch;
    buffer->random_access_point = dfg->random_access_point;
    buffer->coded_temporal_unit_index = dfg->temporal_unit_index;
    buffer->coded_temporal_unit_valid = true;
  }
  if (resource_buffer_index >= 0) {
    Av2DmBuffer *const buffer =
        &model->resource_lane.pool.buffers[resource_buffer_index];
    buffer->decode_order = dfg->decode_order;
    buffer->rap_epoch = dfg->rap_epoch;
    buffer->random_access_point = dfg->random_access_point;
    buffer->coded_temporal_unit_index = dfg->temporal_unit_index;
    buffer->coded_temporal_unit_valid = true;
  }

  if (model->config.mode == AV2_DM_DECODING_SCHEDULE_MODE) {
    compare_lower_limit(
        model, AV2_DM_VIOLATION_SCHEDULE_BEFORE_RESOURCE_REMOVAL,
        event->event_index, &dfg->scheduled_removal, &resource_removal);
  }
  if (model->previous_dfg_valid) {
    check_previous_dfg_interval(model, &model->previous_dfg, dfg);
  }
  check_schedule_delay_limits(model, event->event_index);
  check_delay_consistency(model, dfg);

  if (model->dfg_number == 1 || event->random_access_point) {
    model->most_recent_rap_scheduled_removal = dfg->scheduled_removal;
    model->most_recent_rap_removal_valid = true;
  }
  model->previous_dfg = *dfg;
  model->previous_dfg_valid = true;
  if (!model->config.defer_nonterminal_checks_for_testing) {
    retire_closed_smoothing_records(model, &dfg->last_arrival);
  }
  retire_unresolvable_tus(model);
  if (!increment_model_u64(model, &model->result.decoded_frames)) return;
  model_event_complete(model);
  update_result_status(model);
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
  if (!update_lane_reference_buffers(&model->lane, event) ||
      !update_lane_reference_buffers(&model->resource_lane, event)) {
    arithmetic_failure(model);
  }
  retire_unresolvable_tus(model);
  model_event_complete(model);
}

static bool invalidate_lane_reference_buffers(Av2DmLane *lane,
                                              uint32_t ref_valid_mask) {
  for (uint32_t i = 0; i < lane->pool.num_ref_frames; ++i) {
    if (((ref_valid_mask >> i) & 1) == 0 && lane->pool.vbi[i] != -1 &&
        !av2_dm_buffer_pool_set_vbi(&lane->pool, i, -1)) {
      return false;
    }
  }
  return true;
}

void av2_decoder_model_invalidate_olk_reference_buffers(
    Av2DecoderModel *model, uint32_t ref_valid_mask) {
  if (model == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  // DM-SPEC-5 / Annex E invalidate_olk_ref_buffers(): RefValid has already
  // been updated by frame_header_info(), so every invalid slot is mirrored,
  // including slots absent from the current refresh_frame_flags.
  if (!invalidate_lane_reference_buffers(&model->lane, ref_valid_mask) ||
      !invalidate_lane_reference_buffers(&model->resource_lane,
                                         ref_valid_mask)) {
    arithmetic_failure(model);
  }
  model_event_complete(model);
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
  Av2DmRational threshold;
  if (!av2_dm_rational_subtract(observed, presentation_offset, &threshold)) {
    return false;
  }
  if (pending->valid) {
    int comparison;
    if (!av2_dm_rational_compare(&threshold, &pending->threshold,
                                 &comparison)) {
      return false;
    }
    if (comparison <= 0) return true;
  }
  pending->valid = true;
  pending->event_index = event_index;
  pending->threshold = threshold;
  pending->observed = *observed;
  pending->presentation_offset = *presentation_offset;
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
    Av2DmRational presentation;
    if (!av2_dm_rational_add(&pending->presentation_offset, initial_delay,
                             &presentation)) {
      return false;
    }
    report_violation_for_affected(
        model, code, proving_event_index, AV2_DM_VIOLATION_AFFECTED_OUTPUT,
        pending->event_index, &pending->observed, &presentation, NULL);
  }
  pending->valid = false;
  return true;
}

static bool set_lane_initial_presentation_delay(Av2DecoderModel *model,
                                                Av2DmLane *lane,
                                                bool primary_lane,
                                                uint64_t proving_event_index) {
  if (lane->initial_presentation_delay_known ||
      av2_dm_buffer_pool_frames_in_use(&lane->pool) <
          model->config.initial_display_delay) {
    return true;
  }
  lane->initial_presentation_delay = lane->time;
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

void av2_decoder_model_set_initial_presentation_delay(Av2DecoderModel *model,
                                                      uint64_t event_index) {
  if (model == NULL || model->result.finished ||
      model->result.applicability == AV2_DM_NOT_APPLICABLE ||
      model->processing_stopped) {
    return;
  }
  if (!set_lane_initial_presentation_delay(model, &model->lane, true,
                                           event_index) ||
      !set_lane_initial_presentation_delay(model, &model->resource_lane, false,
                                           event_index)) {
    arithmetic_failure(model);
  }
  model_event_complete(model);
}

static void check_tu_display_rate(Av2DecoderModel *model, Av2DmTuRecord *tu,
                                  const Av2DmRational *duration,
                                  uint64_t proving_event_index) {
  if (model->config.still_picture) return;
  Av2DmRational observed;
  Av2DmRational capacity;
  if (!av2_dm_rational_multiply_u64(duration, model->limits.max_display_rate,
                                    &capacity) ||
      !av2_dm_rational_make(tu->output_luma_samples, 1, &observed)) {
    arithmetic_failure(model);
    return;
  }
  compare_upper_limit_for_affected(
      model, AV2_DM_VIOLATION_MAX_DISPLAY_RATE, proving_event_index,
      AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, tu->temporal_unit_index,
      &observed, &capacity);
}

static void check_tu_minimum_presentation_interval(
    Av2DecoderModel *model, Av2DmTuRecord *tu, const Av2DmRational *interval,
    uint64_t proving_event_index) {
  if (model->config.still_picture) return;
  Av2DmRational limit;
  const uint64_t max_headers = (uint64_t)model->limits.max_header_rate *
                               (1 + ((uint64_t)model->config.tier << 1));
  Av2DmRational sample_interval;
  Av2DmRational min_frame_time;
  if (!rational_from_product(model->config.max_frame_width,
                             model->config.max_frame_height,
                             &sample_interval) ||
      !av2_dm_rational_multiply_u64(&sample_interval, tu->output_frames,
                                    &sample_interval) ||
      !av2_dm_rational_divide_u64(
          &sample_interval, model->limits.max_display_rate, &sample_interval) ||
      !av2_dm_rational_make(model->limits.max_decode_rate,
                            model->limits.max_display_rate, &min_frame_time) ||
      !av2_dm_rational_divide_u64(&min_frame_time, max_headers,
                                  &min_frame_time) ||
      !rational_max(&sample_interval, &min_frame_time, &limit)) {
    arithmetic_failure(model);
    return;
  }
  compare_lower_limit_for_affected(
      model, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL,
      proving_event_index, AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT,
      tu->temporal_unit_index, interval, &limit);
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
  tu->output_luma_samples += event->output_luma_samples;
  ++tu->output_frames;
  if (!tu->presentation_time_valid) {
    tu->presentation_time = *presentation_offset;
    tu->presentation_time_valid = true;
  }
  if (!tu->output_time_valid) {
    // When no external TU output time was supplied, the first actual output
    // event establishes the TU output time in display order. Coding-order TU
    // indices are identifiers and are not timestamps.
    tu->output_time = *presentation_offset;
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
      Av2DmRational presentation_interval;
      if (!av2_dm_rational_subtract(presentation_offset,
                                    &previous->presentation_time,
                                    &presentation_interval)) {
        arithmetic_failure(model);
        return;
      }
      check_tu_minimum_presentation_interval(
          model, previous, &presentation_interval, event->event_index);
      previous->prior_presentation_interval_checked = true;
    }
    if (previous->output_time_valid && tu->output_time_valid) {
      int ordering;
      if (!av2_dm_rational_compare(&tu->output_time, &previous->output_time,
                                   &ordering)) {
        arithmetic_failure(model);
        return;
      }
      output_time_regressed = ordering <= 0;
      Av2DmRational display_duration;
      if (!av2_dm_rational_subtract(&tu->output_time, &previous->output_time,
                                    &display_duration)) {
        arithmetic_failure(model);
        return;
      }
      check_tu_display_rate(model, previous, &display_duration,
                            event->event_index);
      model->last_display_duration = display_duration;
      model->last_display_duration_valid = true;
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
      model->rap_presentation_anchors[i].presentation_offset = *offset;
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
  free_anchor->valid = true;
  free_anchor->rap_epoch = rap_epoch;
  free_anchor->presentation_offset = *offset;
}

static bool calculate_presentation_offset(Av2DecoderModel *model,
                                          const Av2DmOutputEvent *event,
                                          const Av2DmBuffer *buffer,
                                          Av2DmRational *offset) {
  if (model->config.equal_picture_interval) {
    if (!model->last_presentation_offset_valid) return rational_zero(offset);
    if (event->temporal_unit_index == model->last_output_temporal_unit) {
      *offset = model->last_presentation_offset;
      return true;
    }
    Av2DmRational increment;
    return av2_dm_rational_multiply_u64(
               &model->disp_ct, model->config.ticks_per_picture, &increment) &&
           av2_dm_rational_add(&model->last_presentation_offset, &increment,
                               offset);
  }
  if (!event->presentation_time_present) {
    missing_input(model);
    return false;
  }
  if (model->shown_frame_number == 0) return rational_zero(offset);
  Av2DmRational base;
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
        base = anchor->presentation_offset;
        base_found = true;
      }
    }
  }
  if (!base_found && event->presentation_base_offset_present) {
    // Externally seeded RAS frames have no decode record in this model run.
    base = event->presentation_base_offset;
    base_found = true;
  }
  if (!base_found) {
    missing_input(model);
    return false;
  }
  Av2DmRational increment;
  return av2_dm_rational_multiply_u64(
             &model->disp_ct, event->presentation_time_ticks, &increment) &&
         av2_dm_rational_add(&base, &increment, offset);
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

void av2_decoder_model_output_frame(Av2DecoderModel *model,
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
  Av2DmRational presentation_offset;
  if (!calculate_presentation_offset(model, event, buffer,
                                     &presentation_offset)) {
    if (!model->result.missing_required_input) arithmetic_failure(model);
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
  Av2DmRational presentation = presentation_offset;
  if (model->lane.initial_presentation_delay_known) {
    if (!av2_dm_rational_add(&presentation,
                             &model->lane.initial_presentation_delay,
                             &presentation)) {
      arithmetic_failure(model);
      return;
    }
  }
  buffer->presentation_time = presentation;
  buffer->presentation_time_valid =
      model->lane.initial_presentation_delay_known;
  if (!av2_dm_buffer_pool_add_player_ref(&model->lane.pool,
                                         (uint32_t)buffer_index)) {
    arithmetic_failure(model);
    return;
  }

  resource_buffer->presentation_time = presentation_offset;
  resource_buffer->presentation_time_valid = false;
  if (model->resource_lane.initial_presentation_delay_known) {
    if (!av2_dm_rational_add(&resource_buffer->presentation_time,
                             &model->resource_lane.initial_presentation_delay,
                             &resource_buffer->presentation_time)) {
      arithmetic_failure(model);
      return;
    }
    resource_buffer->presentation_time_valid = true;
  }
  if (!av2_dm_buffer_pool_add_player_ref(&model->resource_lane.pool,
                                         (uint32_t)resource_buffer_index)) {
    arithmetic_failure(model);
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
        return;
      }
    }
    model->previous_output_decode_order = buffer->decode_order;
    model->previous_output_order_valid = true;
  }
  model->previous_output_presentation_offset = presentation_offset;
  model->previous_output_presentation_valid = true;
  model->previous_output_rap_epoch = output_rap_epoch;
  model->last_presentation_offset = presentation_offset;
  model->last_presentation_offset_valid = true;
  if (model->lane.initial_presentation_delay_known) {
    model->last_presentation = presentation;
    model->last_presentation_valid = true;
  }
  model->last_output_temporal_unit = event->temporal_unit_index;
  if (random_access_point) {
    store_rap_presentation_anchor(model, output_rap_epoch,
                                  &presentation_offset);
  }
  update_tu_for_output(model, event, &presentation_offset);
  if (model->processing_stopped) return;
  if (!model->config.defer_nonterminal_checks_for_testing) {
    check_header_rate_windows(model, false, event->event_index);
  }
  if (model->processing_stopped) return;
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
    return;
  }

  if (!increment_output_count(model)) return;
  model_event_complete(model);
  update_result_status(model);
}

static void check_smoothing_fullness_at(Av2DecoderModel *model,
                                        const Av2DmRational *time,
                                        Av2DmDfgRecord *breakpoint,
                                        uint64_t proving_event_index) {
  Av2DmRational fullness;
  if (!rational_zero(&fullness)) {
    arithmetic_failure(model);
    return;
  }
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    const Av2DmDfgRecord *const dfg = &model->dfgs[i];
    if (dfg->smoothing_epoch != breakpoint->smoothing_epoch) continue;
    int before_first;
    int after_removal;
    if (!av2_dm_rational_compare(time, &dfg->first_arrival, &before_first) ||
        !av2_dm_rational_compare(time, &dfg->removal, &after_removal)) {
      arithmetic_failure(model);
      return;
    }
    if (before_first < 0 || after_removal > 0) continue;
    Av2DmRational duration;
    Av2DmRational arrived;
    Av2DmRational coded_bits;
    if (!av2_dm_rational_subtract(time, &dfg->first_arrival, &duration) ||
        !rational_multiply(&duration, &breakpoint->limits.bit_rate, &arrived) ||
        !av2_dm_rational_make(dfg->coded_bits, 1, &coded_bits)) {
      arithmetic_failure(model);
      return;
    }
    bool too_many;
    if (!rational_greater(&arrived, &coded_bits, &too_many)) {
      arithmetic_failure(model);
      return;
    }
    if (too_many) arrived = coded_bits;
    if (!av2_dm_rational_add(&fullness, &arrived, &fullness)) {
      arithmetic_failure(model);
      return;
    }
  }
  bool overflow;
  if (!rational_greater(&fullness, &breakpoint->limits.buffer_size,
                        &overflow)) {
    arithmetic_failure(model);
    return;
  }
  if (overflow && !breakpoint->smoothing_overflow_reported) {
    breakpoint->smoothing_overflow_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_DFG, breakpoint->event_index, &fullness,
        &breakpoint->limits.buffer_size, NULL);
  }
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
    if (comparison < 0) continue;
    if (write_index != i) model->dfgs[write_index] = *dfg;
    ++write_index;
  }
  model->dfg_count = write_index;
}

static void check_smoothing_buffer_overflow(Av2DecoderModel *model,
                                            uint64_t proving_event_index) {
  if (violation_seen(model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW)) {
    model->dfg_count = 0;
    return;
  }
  for (uint32_t i = 0; i < model->dfg_count; ++i) {
    Av2DmDfgRecord *const breakpoint = &model->dfgs[i];
    check_smoothing_fullness_at(model, &breakpoint->last_arrival, breakpoint,
                                proving_event_index);
    check_smoothing_fullness_at(model, &breakpoint->removal, breakpoint,
                                proving_event_index);
  }
  if (violation_seen(model, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW)) {
    // Fullness history cannot prove a different code after overflow has made
    // this CVS non-conformant. Adjacent-DFG and per-frame checks retain their
    // independent scalar state and continue online.
    model->dfg_count = 0;
  }
}

static bool same_scope(const Av2DmScope *a, const Av2DmScope *b) {
  return a->xlayer_id == b->xlayer_id && a->ops_xlayer_id == b->ops_xlayer_id &&
         a->ops_id == b->ops_id && a->operating_point == b->operating_point &&
         a->whole_xlayer == b->whole_xlayer;
}

static bool same_model_topology_and_clock(const Av2DmConfig *a,
                                          const Av2DmConfig *b) {
  // Section 7 keeps the active sequence header fixed until the next CLK,
  // which starts a new CVS and therefore a new model. In-place RAP updates
  // can replace OPS parameters, but not the active sequence-level fallback.
  return same_scope(&a->scope, &b->scope) &&
         a->num_ref_frames == b->num_ref_frames &&
         a->max_frame_width == b->max_frame_width &&
         a->max_frame_height == b->max_frame_height &&
         a->max_mlayer_id == b->max_mlayer_id &&
         a->still_picture == b->still_picture &&
         a->explicit_num_ref_frames == b->explicit_num_ref_frames &&
         a->timing_info_present == b->timing_info_present &&
         a->num_units_in_display_tick == b->num_units_in_display_tick &&
         a->time_scale == b->time_scale &&
         a->num_units_in_decoding_tick == b->num_units_in_decoding_tick &&
         a->equal_picture_interval == b->equal_picture_interval &&
         a->ticks_per_picture == b->ticks_per_picture &&
         a->sequence_parameters_present == b->sequence_parameters_present &&
         a->sequence_decoder_buffer_delay == b->sequence_decoder_buffer_delay &&
         a->sequence_encoder_buffer_delay == b->sequence_encoder_buffer_delay &&
         a->sequence_low_delay_mode == b->sequence_low_delay_mode &&
         a->rebase_interval_events == b->rebase_interval_events &&
         a->defer_nonterminal_checks_for_testing ==
             b->defer_nonterminal_checks_for_testing &&
         a->stop_after_first_violation == b->stop_after_first_violation;
}

bool av2_decoder_model_update_parameters(Av2DecoderModel *model,
                                         const Av2DmConfig *config,
                                         uint64_t event_index) {
  if (model == NULL || config == NULL || model->result.finished ||
      model->processing_stopped) {
    return false;
  }
  if (model->result.applicability != AV2_DM_APPLICABLE ||
      config->applicability != AV2_DM_APPLICABLE ||
      !same_model_topology_and_clock(&model->config, config)) {
    missing_input(model);
    return false;
  }

  Av2DmResolvedParameters parameters;
  if (!resolve_parameters(config, &parameters)) {
    missing_input(model);
    return false;
  }
  if (!model->config.defer_nonterminal_checks_for_testing) {
    // The old smoothing epoch is evaluated with the old BitRate and
    // BufferSize before the replacement parameters take effect.
    check_smoothing_buffer_overflow(model, event_index);
    model->dfg_count = 0;
  }
  if (model->processing_stopped) return false;
  if (model->smoothing_epoch == UINT64_MAX) {
    arithmetic_failure(model);
    return false;
  }
  ++model->smoothing_epoch;
  model->smoothing_epoch_prepared = true;

  Av2DmConfig updated = *config;
  updated.initial_display_delay = model->config.initial_display_delay;
  updated.ras_start = model->config.ras_start;
  updated.ras_seed_complete = model->config.ras_seed_complete;
  updated.ras_seed_count = model->config.ras_seed_count;
  memcpy(updated.ras_seeds, model->config.ras_seeds, sizeof(updated.ras_seeds));
  const Av2DmMode old_mode = model->config.mode;
  apply_parameters(model, &updated, &parameters);
  if (!model->max_reference_frames_violated) {
    // The maximum depends on the active level limits and must be reconsidered
    // for the RAP frame after an OPS parameter update.
    model->max_reference_frames_checked = false;
  }
  if (old_mode == AV2_DM_DECODING_SCHEDULE_MODE &&
      updated.mode == AV2_DM_RESOURCE_AVAILABILITY_MODE) {
    // The resource lane is maintained for every event. It is therefore the
    // continuous resource-availability state when that mode becomes active.
    model->lane = model->resource_lane;
  }
  model->result.mode = updated.mode;
  update_result_status(model);
  update_storage_stats(model);
  return true;
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
  Av2DmRational observed;
  Av2DmRational limit;
  if (!av2_dm_rational_make(model->config.num_ref_frames, 1, &observed) ||
      !av2_dm_rational_make(maximum, 1, &limit)) {
    arithmetic_failure(model);
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
}

static void check_header_rate_at(Av2DecoderModel *model, Av2DmTuRecord *end_tu,
                                 uint64_t frame_headers,
                                 uint64_t maximum_headers,
                                 uint64_t proving_event_index) {
  Av2DmRational observed;
  Av2DmRational limit;
  if (!av2_dm_rational_make(frame_headers, 1, &observed) ||
      !av2_dm_rational_make(maximum_headers, 1, &limit)) {
    arithmetic_failure(model);
    return;
  }
  bool violated;
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    return;
  }
  if (violated && !end_tu->header_rate_reported) {
    end_tu->header_rate_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_MAX_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, end_tu->temporal_unit_index,
        &observed, &limit, NULL);
  }
  if (!rational_from_product(model->maximum_tile_area, frame_headers,
                             &observed) ||
      !av2_dm_rational_make(model->limits.max_tile_size_header_rate_product, 1,
                            &limit)) {
    arithmetic_failure(model);
    return;
  }
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    return;
  }
  if (violated && !end_tu->tile_header_rate_reported) {
    end_tu->tile_header_rate_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT, end_tu->temporal_unit_index,
        &observed, &limit, NULL);
  }
}

static void check_retired_tile_header_summary(Av2DecoderModel *model,
                                              uint64_t proving_event_index) {
  if (violation_seen(model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) ||
      !model->retired_header_summary_valid ||
      model->retired_header_summary_reported) {
    return;
  }
  Av2DmRational observed;
  Av2DmRational limit;
  if (!rational_from_product(model->maximum_tile_area,
                             model->retired_max_frame_headers, &observed) ||
      !av2_dm_rational_make(model->limits.max_tile_size_header_rate_product, 1,
                            &limit)) {
    arithmetic_failure(model);
    return;
  }
  bool violated;
  if (!rational_greater(&observed, &limit, &violated)) {
    arithmetic_failure(model);
    return;
  }
  if (violated) {
    model->retired_header_summary_reported = true;
    report_violation_for_affected(
        model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE, proving_event_index,
        AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT,
        model->retired_header_event_index, &observed, &limit, NULL);
  }
}

static bool order_tus_by_output_time(const Av2DecoderModel *model,
                                     uint32_t **ordered_tus,
                                     uint32_t *ordered_tu_count) {
  *ordered_tus = NULL;
  *ordered_tu_count = 0;
  if (model->tu_count == 0) return true;
  const uint64_t capacity = model->tu_count;
  if (capacity > SIZE_MAX / sizeof(**ordered_tus)) return false;
  const size_t allocation_size = (size_t)capacity * sizeof(**ordered_tus);
  uint32_t *source = avm_malloc(allocation_size);
  uint32_t *destination = avm_malloc(allocation_size);
  if (source == NULL || destination == NULL) {
    avm_free(source);
    avm_free(destination);
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
        int comparison;
        if (!av2_dm_rational_compare(&model->tus[source[first]].output_time,
                                     &model->tus[source[second]].output_time,
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
    uint64_t maximum_headers, uint64_t proving_event_index) {
  uint32_t first_tu = 0;
  uint64_t frame_headers = 0;
  for (uint32_t i = 0; i < ordered_tu_count; ++i) {
    Av2DmTuRecord *const end_tu = &model->tus[ordered_tus[i]];
    if (UINT64_MAX - frame_headers < end_tu->frame_headers) {
      arithmetic_failure(model);
      return;
    }
    frame_headers += end_tu->frame_headers;
    Av2DmRational window_start;
    if (!av2_dm_rational_subtract(&end_tu->output_time, one_second,
                                  &window_start)) {
      arithmetic_failure(model);
      return;
    }
    while (first_tu <= i) {
      const Av2DmTuRecord *const candidate = &model->tus[ordered_tus[first_tu]];
      int comparison;
      if (!av2_dm_rational_compare(&candidate->output_time, &window_start,
                                   &comparison)) {
        arithmetic_failure(model);
        return;
      }
      if (comparison >= 0) break;
      if (frame_headers < candidate->frame_headers) {
        arithmetic_failure(model);
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
                         maximum_headers, proving_event_index);
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
      !tu->header_window_checked || tu->tile_header_rate_reported) {
    return;
  }
  if (!model->retired_header_summary_valid ||
      tu->header_window_headers > model->retired_max_frame_headers) {
    model->retired_header_summary_valid = true;
    model->retired_max_frame_headers = tu->header_window_headers;
    model->retired_header_event_index = tu->temporal_unit_index;
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
      continue;
    }
    if (write_index != i) model->tus[write_index] = *tu;
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
      continue;
    }
    if (write_index != i) model->tus[write_index] = *tu;
    ++write_index;
  }
  model->tu_count = write_index;
  const Av2DmTuRecord *const current = find_tu(model, temporal_unit_index);
  if (current != NULL && current->output_time_valid) {
    model->latest_timed_tu_output_time = current->output_time;
    model->latest_timed_tu_valid = true;
  }
}

static void retire_closed_tus(Av2DecoderModel *model,
                              const Av2DmRational *one_second) {
  const bool header_history_proven =
      violation_seen(model, AV2_DM_VIOLATION_MAX_HEADER_RATE) &&
      violation_seen(model, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE);
  if (!header_history_proven && !model->latest_timed_tu_valid) return;
  Av2DmRational frontier;
  if (!header_history_proven &&
      !av2_dm_rational_subtract(&model->latest_timed_tu_output_time, one_second,
                                &frontier)) {
    arithmetic_failure(model);
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
          return;
        }
        retire = comparison < 0;
      }
    }
    if (retire) {
      remember_retired_tu(model, tu);
      continue;
    }
    if (write_index != i) model->tus[write_index] = *tu;
    ++write_index;
  }
  model->tu_count = write_index;
}

static void check_header_rate_windows(Av2DecoderModel *model,
                                      bool require_complete,
                                      uint64_t proving_event_index) {
  if (model->config.still_picture) return;
  if (!require_complete) {
    model->latest_header_check_event_index = proving_event_index;
  }
  if (require_complete) {
    for (uint32_t i = 0; i < model->tu_count; ++i) {
      if (model->tus[i].frame_headers != 0 &&
          !model->tus[i].output_time_valid) {
        incomplete_verification(model);
        return;
      }
    }
  }
  Av2DmRational one_second;
  if (!av2_dm_rational_make(1, 1, &one_second)) {
    arithmetic_failure(model);
    return;
  }
  const uint64_t maximum_headers = (uint64_t)model->limits.max_header_rate *
                                   (1 + ((uint64_t)model->config.tier << 1));
  uint32_t *ordered_tus;
  uint32_t ordered_tu_count;
  if (!order_tus_by_output_time(model, &ordered_tus, &ordered_tu_count)) {
    arithmetic_failure(model);
    return;
  }
  check_header_rate_windows_in_output_order(
      model, ordered_tus, ordered_tu_count, &one_second, maximum_headers,
      proving_event_index);
  avm_free(ordered_tus);
  if (!model->processing_stopped && !require_complete) {
    retire_closed_tus(model, &one_second);
  }
}

void av2_decoder_model_finish(Av2DecoderModel *model) {
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
    if (!model->processing_stopped && !model->config.still_picture &&
        model->previous_dfg_valid) {
      if (model->last_frame_parsing_time_valid) {
        check_frame_parsing_constraints(model, &model->previous_dfg,
                                        &model->last_frame_parsing_time,
                                        model->previous_dfg.event_index);
      } else {
        incomplete_verification(model);
      }
      if (!model->processing_stopped) {
        Av2DmTuRecord *const last_output_tu =
            model->last_output_tu_valid ? find_tu(model, model->last_output_tu)
                                        : NULL;
        if (last_output_tu != NULL) {
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
    }
    if (!model->processing_stopped &&
        model->config.defer_nonterminal_checks_for_testing) {
      check_smoothing_buffer_overflow(model, model->latest_frame_event_index);
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

bool av2_decoder_model_get_result(const Av2DecoderModel *model,
                                  Av2DmResult *result) {
  if (model == NULL || result == NULL) return false;
  *result = model->result;
  return true;
}

bool av2_decoder_model_get_state(const Av2DecoderModel *model,
                                 Av2DmState *state) {
  if (model == NULL || state == NULL) return false;
  memset(state, 0, sizeof(*state));
  state->time = model->lane.time;
  state->initial_presentation_delay_known =
      model->lane.initial_presentation_delay_known;
  state->initial_presentation_delay = model->lane.initial_presentation_delay;
  state->current_buffer_index = model->lane.current_buffer_index;
  state->frame_number = model->frame_number;
  state->dfg_number = model->dfg_number;
  state->shown_frame_number = model->shown_frame_number;
  state->buffer_pool = model->lane.pool;
  if (model->previous_dfg_valid) {
    const Av2DmDfgRecord *const dfg = &model->previous_dfg;
    state->last_dfg_valid = true;
    state->first_bit_arrival = dfg->first_arrival;
    state->last_bit_arrival = dfg->last_arrival;
    state->scheduled_removal = dfg->scheduled_removal;
    state->removal = dfg->removal;
    state->time_to_decode = dfg->decode_time;
    state->decode_completion = dfg->decode_completion;
  }
  if (model->shown_frame_number != 0) {
    state->last_presentation_valid = model->last_presentation_valid;
    state->last_presentation = model->last_presentation;
    state->last_presentation_offset_valid =
        model->last_presentation_offset_valid;
    state->last_presentation_offset = model->last_presentation_offset;
    state->last_output_temporal_unit_valid = true;
    state->last_output_temporal_unit = model->last_output_temporal_unit;
  }
  if (model->last_output_tu_valid) {
    const Av2DmTuRecord *tu = NULL;
    for (uint32_t i = model->tu_count; i > 0; --i) {
      if (model->tus[i - 1].temporal_unit_index == model->last_output_tu) {
        tu = &model->tus[i - 1];
        break;
      }
    }
    if (tu == NULL) return false;
    state->last_temporal_unit_output_time_valid = tu->output_time_valid;
    state->last_temporal_unit_output_time = tu->output_time;
    state->last_temporal_unit_output_luma_samples = tu->output_luma_samples;
    state->last_temporal_unit_output_frames = tu->output_frames;
  }
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
