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

#ifndef AVM_AVM_PORTS_SANITIZER_H_
#define AVM_AVM_PORTS_SANITIZER_H_

// AddressSanitizer support.

// Define AVM_ADDRESS_SANITIZER if AddressSanitizer is used.
// Clang.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AVM_ADDRESS_SANITIZER 1
#endif
#endif  // defined(__has_feature)
// GCC.
#if defined(__SANITIZE_ADDRESS__)
#define AVM_ADDRESS_SANITIZER 1
#endif  // defined(__SANITIZE_ADDRESS__)

// Define the macros for AddressSanitizer manual memory poisoning. See
// https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning.
#if defined(AVM_ADDRESS_SANITIZER)
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

#endif  // AVM_AVM_PORTS_SANITIZER_H_
