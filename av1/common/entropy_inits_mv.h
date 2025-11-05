/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_COMMON_ENTROPY_INITS_MV_H_
#define AOM_AV1_COMMON_ENTROPY_INITS_MV_H_

#include "config/aom_config.h"
#include "av1/common/entropymv.h"

#ifdef __cplusplus
extern "C" {
#endif
/* clang-format off */

static const nmv_context default_nmv_context = {
{ AOM_CDF2(31579),  25 },
{
{ AOM_CDF5( 4460, 12999, 22505, 30840),  38 },
{ AOM_CDF6( 7519, 18907, 25563, 29875, 31983),  27 },
{ AOM_CDF6( 5461, 10923, 16384, 21845, 27307),   0 },
{ AOM_CDF7( 8680, 13723, 18208, 22686, 26722, 30020),   5 },
{ AOM_CDF7( 4324, 15300, 23690, 28697, 31282, 32359),   1 },
{ AOM_CDF8( 7497, 17301, 23848, 27438, 29395, 30879, 32003),  31 },
{ AOM_CDF8(10667, 20239, 25883, 29670, 31400, 32153, 32579),   0 },
},
{
{ AOM_CDF6(21329, 30564, 32589, 32649, 32708),  50 },
{ AOM_CDF6(24250, 31806, 32676, 32722, 32732),  50 },
{ AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
{ AOM_CDF7(19978, 30160, 32564, 32732, 32736, 32740),   1 },
{ AOM_CDF8(19707, 28414, 31240, 31648, 32692, 32717, 32721),  25 },
{ AOM_CDF8(18469, 27427, 31562, 32652, 32724, 32728, 32732),  31 },
{ AOM_CDF8(17810, 25196, 29372, 31953, 32564, 32720, 32724),  55 },
},
{ AOM_CDF2(16384),   0 },
{
  { AOM_CDF2(14587),  36 },
  { AOM_CDF2(20966),  75 },
},
{ AOM_CDF2(13189),   0 },
{
  {
    { AOM_CDF2(17943),  93 },
    { AOM_CDF2(18934),  93 },
    { AOM_CDF2(18928),  93 },
    { AOM_CDF2(18696),  93 },
    { AOM_CDF2(19044),  93 },
    { AOM_CDF2(20362),  93 },
    { AOM_CDF2(20426),  93 },
    { AOM_CDF2(22563),  93 },
    { AOM_CDF2(22190),  93 },
    { AOM_CDF2(23458),  90 },
    { AOM_CDF2(26227),   2 },
    { AOM_CDF2(30765),  50 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
},
{
  { AOM_CDF2( 5663),  25 },
  { AOM_CDF2( 4856),  90 },
},
{
  { AOM_CDF2(13445),   1 },
  { AOM_CDF2(13541),   1 },
  { AOM_CDF2(14045),   1 },
  { AOM_CDF2(12888),  31 },
},
{ AOM_CDF4(    4, 17705, 32748),   1 },
  {
    {
      { AOM_CDF8(10549, 15298, 16241, 22533, 27449, 30520, 32080),  26 },
    },
    {
      { AOM_CDF8( 9414, 14965, 15966, 22465, 27468, 30628, 32144),  26 },
    },
  },
};

/* clang-format on */
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AOM_AV1_COMMON_ENTROPY_INITS_MV_H_
