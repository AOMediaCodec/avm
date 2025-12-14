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
#ifndef AVM_AVM_AVM_INTEGER_H_
#define AVM_AVM_AVM_INTEGER_H_

/* get ptrdiff_t, size_t, wchar_t, NULL */
#include <stddef.h>

#if defined(_MSC_VER)
#define AVM_FORCE_INLINE __forceinline
#define AVM_INLINE __inline
#else
#define AVM_FORCE_INLINE __inline__ __attribute__((always_inline))
#define AVM_INLINE inline
#endif

/* Assume platforms have the C99 standard integer types. */

#if defined(__cplusplus)
#if !defined(__STDC_FORMAT_MACROS)
#define __STDC_FORMAT_MACROS
#endif
#if !defined(__STDC_LIMIT_MACROS)
#define __STDC_LIMIT_MACROS
#endif
#endif  // __cplusplus

#include <stdint.h>
#include <inttypes.h>

#if defined(__cplusplus)
extern "C" {
#endif  // __cplusplus

// Returns size of uint64_t when encoded using LEB128.
size_t avm_uleb_size_in_bytes(uint64_t value);

// Returns 0 on success, -1 on decode failure.
// On success, 'value' stores the decoded LEB128 value and 'length' stores
// the number of bytes decoded.
int avm_uleb_decode(const uint8_t *buffer, size_t available, uint64_t *value,
                    size_t *length);

// Encodes LEB128 integer. Returns 0 when successful, and -1 upon failure.
int avm_uleb_encode(uint64_t value, size_t available, uint8_t *coded_value,
                    size_t *coded_size);

// Encodes LEB128 integer to size specified. Returns 0 when successful, and -1
// upon failure.
// Note: This will write exactly pad_to_size bytes; if the value cannot be
// encoded in this many bytes, then this will fail.
int avm_uleb_encode_fixed_size(uint64_t value, size_t available,
                               size_t pad_to_size, uint8_t *coded_value,
                               size_t *coded_size);

#if defined(__cplusplus)
}  // extern "C"
#endif  // __cplusplus

#endif  // AVM_AVM_AVM_INTEGER_H_
