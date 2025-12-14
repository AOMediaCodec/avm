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

#ifndef AVM_AVM_DSP_AVM_SIMD_H_
#define AVM_AVM_DSP_AVM_SIMD_H_

#include <stdint.h>

#if defined(_WIN32)
#include <intrin.h>
#endif

#include "config/avm_config.h"

#include "avm_dsp/avm_simd_inline.h"

#define SIMD_CHECK 1  // Sanity checks in C equivalents

#if HAVE_NEON
#include "simd/v256_intrinsics_arm.h"
// VS compiling for 32 bit targets does not support vector types in
// structs as arguments, which makes the v256 type of the intrinsics
// hard to support, so optimizations for this target are disabled.
#elif HAVE_SSE2 && (defined(_WIN64) || !defined(_MSC_VER) || defined(__clang__))
#include "simd/v256_intrinsics_x86.h"
#else
#include "simd/v256_intrinsics.h"
#endif

#endif  // AVM_AVM_DSP_AVM_SIMD_H_
