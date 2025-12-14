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

#ifndef AVM_AV2_ENCODER_SEGMENTATION_H_
#define AVM_AV2_ENCODER_SEGMENTATION_H_

#include "av2/common/blockd.h"
#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av2_enable_segmentation(struct segmentation *seg);
void av2_disable_segmentation(struct segmentation *seg);

void av2_disable_segfeature(struct segmentation *seg, int segment_id,
                            SEG_LVL_FEATURES feature_id);
void av2_choose_segmap_coding_method(AV2_COMMON *cm, MACROBLOCKD *xd);

void av2_reset_segment_features(AV2_COMMON *cm);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_SEGMENTATION_H_
