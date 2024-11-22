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

#ifndef AOM_AV1_ENCODER_PICKCCSO_H_
#define AOM_AV1_ENCODER_PICKCCSO_H_

#define CCSO_MAX_ITERATIONS 15

#include "av1/common/ccso.h"
#include "av1/encoder/speed_features.h"

#ifdef __cplusplus
extern "C" {
#endif

void ccso_search(AV1_COMMON *cm, MACROBLOCKD *xd, int rdmult,
                 const uint16_t *ext_rec_y, uint16_t *rec_uv[3],
                 uint16_t *org_uv[3]
#if CONFIG_ENTROPY_STATS
                 ,
                 ThreadData *td
#endif
);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AOM_AV1_ENCODER_PICKCCSO_H_
