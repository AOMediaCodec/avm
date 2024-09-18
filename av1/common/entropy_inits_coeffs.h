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

#ifndef AOM_AV1_COMMON_ENTROPY_INITS_COEFFS_H_
#define AOM_AV1_COMMON_ENTROPY_INITS_COEFFS_H_

#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

static const aom_cdf_prob
    av1_default_eob_multi16_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        5)] = {
      {
          { AOM_CDF5(1413, 1933, 3768, 9455), 5 },
          { AOM_CDF5(1954, 2400, 4205, 7753), 5 },
          { AOM_CDF5(9359, 11741, 16061, 22179), 31 },
      },
      {
          { AOM_CDF5(2832, 4201, 8578, 17754), 30 },
          { AOM_CDF5(4563, 5208, 7444, 11962), 5 },
          { AOM_CDF5(10524, 13197, 18032, 24922), 0 },
      },
      {
          { AOM_CDF5(4390, 6907, 13987, 24674), 55 },
          { AOM_CDF5(1870, 2463, 3813, 9299), 30 },
          { AOM_CDF5(15137, 18012, 23056, 29705), 6 },
      },
      {
          { AOM_CDF5(5508, 11837, 26327, 32095), 56 },
          { AOM_CDF5(6554, 8738, 10923, 24030), 0 },
          { AOM_CDF5(28607, 29647, 32421, 32595), 50 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi32_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        6)] = {
      {
          { AOM_CDF6(1183, 1539, 2981, 7359, 12851), 31 },
          { AOM_CDF6(1847, 2098, 2631, 4422, 9368), 5 },
          { AOM_CDF6(14803, 16649, 20616, 25021, 29117), 6 },
      },
      {
          { AOM_CDF6(2170, 3095, 6309, 12580, 18493), 31 },
          { AOM_CDF6(1194, 1592, 2551, 4712, 9835), 6 },
          { AOM_CDF6(12842, 15056, 19310, 24033, 29143), 6 },
      },
      {
          { AOM_CDF6(3673, 5100, 10624, 18431, 23892), 31 },
          { AOM_CDF6(1891, 2179, 3130, 6874, 14672), 0 },
          { AOM_CDF6(17990, 20534, 24659, 28946, 31883), 6 },
      },
      {
          { AOM_CDF6(6158, 10781, 23027, 30726, 32275), 36 },
          { AOM_CDF6(971, 6554, 17719, 26336, 29370), 32 },
          { AOM_CDF6(15245, 19009, 26979, 32073, 32710), 5 },
      },
    };

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPY_INITS_COEFFS_H_
