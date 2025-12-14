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

#ifndef AVM_AVM_PORTS_ARM_H_
#define AVM_AVM_PORTS_ARM_H_
#include <stdlib.h>

#include "config/avm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*ARMv5TE "Enhanced DSP" instructions.*/
#define HAS_EDSP 0x01
/*ARMv6 "Parallel" or "Media" instructions.*/
#define HAS_MEDIA 0x02
/*ARMv7 optional NEON instructions.*/
#define HAS_NEON 0x04

int avm_arm_cpu_caps(void);

// Earlier gcc compilers have issues with some neon intrinsics
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ == 4 && \
    __GNUC_MINOR__ <= 6
#define AVM_INCOMPATIBLE_GCC
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_PORTS_ARM_H_
