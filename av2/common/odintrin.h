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

/* clang-format off */

#ifndef AVM_AV2_COMMON_ODINTRIN_H_
#define AVM_AV2_COMMON_ODINTRIN_H_

#include <stdlib.h>
#include <string.h>

#include "avm/avm_integer.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/bitops.h"
#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int od_coeff;

#define OD_DIVU_DMAX (1024)

extern uint32_t AVM_OD_DIVU_SMALL_CONSTS[OD_DIVU_DMAX][2];

/*Translate unsigned division by small divisors into multiplications.*/
#define OD_DIVU_SMALL(_x, _d)                                     \
  ((uint32_t)((AVM_OD_DIVU_SMALL_CONSTS[(_d)-1][0] * (uint64_t)(_x) + \
               AVM_OD_DIVU_SMALL_CONSTS[(_d)-1][1]) >>                \
              32) >>                                              \
   (OD_ILOG_NZ(_d) - 1))

#define OD_DIVU(_x, _d) \
  (((_d) < OD_DIVU_DMAX) ? (OD_DIVU_SMALL((_x), (_d))) : ((_x) / (_d)))

#define OD_MINI AVMMIN
#define OD_MAXI AVMMAX
#define OD_CLAMPI(min, val, max) (OD_MAXI(min, OD_MINI(val, max)))

/*Integer logarithm (base 2) of a nonzero unsigned 32-bit integer.
  OD_ILOG_NZ(x) = (int)floor(log2(x)) + 1.*/
#define OD_ILOG_NZ(x) (1 + get_msb(x))

/*Enable special features for gcc and compatible compilers.*/
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
#define OD_GNUC_PREREQ(maj, min, pat)                                \
  ((__GNUC__ << 16) + (__GNUC_MINOR__ << 8) + __GNUC_PATCHLEVEL__ >= \
   ((maj) << 16) + ((min) << 8) + pat)  // NOLINT
#else
#define OD_GNUC_PREREQ(maj, min, pat) (0)
#endif

#if OD_GNUC_PREREQ(3, 4, 0)
#define OD_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define OD_WARN_UNUSED_RESULT
#endif

#if OD_GNUC_PREREQ(3, 4, 0)
#define OD_ARG_NONNULL(x) __attribute__((__nonnull__(x)))
#else
#define OD_ARG_NONNULL(x)
#endif

/** Copy n elements of memory from src to dst. The 0* term provides
    compile-time type checking  */
#if !defined(OVERRIDE_OD_COPY)
#define OD_COPY(dst, src, n) \
  (memcpy((dst), (src), sizeof(*(dst)) * (n) + 0 * ((dst) - (src))))
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_ODINTRIN_H_
