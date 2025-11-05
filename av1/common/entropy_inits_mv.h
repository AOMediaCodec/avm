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
{ AOM_CDF2(24576),   0 },
{
{ AOM_CDF5( 6847, 15990, 24873, 32100),   0 },
{ AOM_CDF6( 8452, 19730, 26138, 30154, 32100),   0 },
{ AOM_CDF6( 6553, 13106, 19659, 26212, 32100),   0 },
{ AOM_CDF7( 5062, 12676, 19127, 24565, 29511, 32100),   0 },
{ AOM_CDF7( 4553, 16572, 24700, 28964, 31428, 32100),   0 },
{ AOM_CDF8( 2750, 12194, 20615, 25661, 28862, 31157, 32100),   0 },
{ AOM_CDF8( 7886, 19300, 26400, 29900, 31400, 32100, 32740),   0 },
},
{
{ AOM_CDF6(17356, 28590, 32415, 32740, 32760),   0 },
{ AOM_CDF6(21505, 30000, 31700, 31819, 32100),   0 },
{ AOM_CDF7( 5461, 10922, 16383, 21844, 27305, 32100),   0 },
{ AOM_CDF7(21567, 30194, 32730, 32755, 32760, 32764),   0 },
{ AOM_CDF8(20234, 28560, 30530, 31246, 31694, 32141, 32740),   0 },
{ AOM_CDF8(18126, 26500, 30750, 32100, 32185, 32400, 32740),   0 },
{ AOM_CDF8(16384, 24576, 28672, 29696, 29970, 30244, 30518),   0 },
},
{ AOM_CDF2(16384),   0 },
{
  { AOM_CDF2( 3268),   1 },
  { AOM_CDF2(17309),  75 },
},
{ AOM_CDF2(16384),  75 },
{
  {
    { AOM_CDF2(16786),  75 },
    { AOM_CDF2(19319),  78 },
    { AOM_CDF2(18504),  93 },
    { AOM_CDF2(18606),  93 },
    { AOM_CDF2(19609),  93 },
    { AOM_CDF2(20222),  93 },
    { AOM_CDF2(20715),  93 },
    { AOM_CDF2(22309),  93 },
    { AOM_CDF2(22194),  93 },
    { AOM_CDF2(23081),  95 },
    { AOM_CDF2(25072),   1 },
    { AOM_CDF2(29343),  50 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
},
{
  { AOM_CDF2( 3371),  78 },
  { AOM_CDF2( 5706),  93 },
},
{
  { AOM_CDF2(13012),  75 },
  { AOM_CDF2(13771),   0 },
  { AOM_CDF2(13429),   1 },
  { AOM_CDF2(14771),   1 },
},
{ AOM_CDF4(    4, 19409, 32748),   1 },
  {
    {
      { AOM_CDF8( 7804, 11354, 12626, 18581, 24598, 29144, 31608),   1 },
    },
    {
      { AOM_CDF8( 7392, 11106, 12422, 18167, 24480, 29230, 31714),   1 },
    },
  },
};

/* clang-format on */
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AOM_AV1_COMMON_ENTROPY_INITS_MV_H_
