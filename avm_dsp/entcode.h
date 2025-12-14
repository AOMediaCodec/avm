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

#ifndef AVM_AVM_DSP_ENTCODE_H_
#define AVM_AVM_DSP_ENTCODE_H_

#include <limits.h>
#include <stddef.h>
#include "av2/common/odintrin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*OPT: od_ec_window must be at least 32 bits, but if you have fast arithmetic
   on a larger type, you can speed up the decoder by using it here.*/
typedef uint64_t od_ec_window;

/*The size in bits of od_ec_window.*/
#define OD_EC_WINDOW_SIZE ((int)sizeof(od_ec_window) * CHAR_BIT)

/*The resolution of fractional-precision bit usage measurements, i.e.,
   16 => 1/65536th bits.*/
#define OD_BITRES (16)

#define OD_ICDF AVM_ICDF

extern const uint16_t av2_prob_inc_tbl[15][16];

/*See entcode.c for further documentation.*/

OD_WARN_UNUSED_RESULT uint64_t od_ec_tell_frac(uint32_t nbits_total,
                                               uint32_t rng);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_ENTCODE_H_
