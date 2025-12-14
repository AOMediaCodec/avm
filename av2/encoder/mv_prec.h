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

#ifndef AVM_AV2_ENCODER_MV_PREC_H_
#define AVM_AV2_ENCODER_MV_PREC_H_

#include "av2/encoder/encoder.h"
#include "av2/encoder/speed_features.h"

// Q threshold for high precision mv.
#define HIGH_PRECISION_MV_QTHRESH 128
void av2_collect_mv_stats(AV2_COMP *cpi, int current_q);

static AVM_INLINE int av2_frame_allows_smart_mv(const AV2_COMP *cpi) {
  const int gf_group_index = cpi->gf_group.index;
  const int gf_update_type = cpi->gf_group.update_type[gf_group_index];
  return !frame_is_intra_only(&cpi->common) &&
         !(gf_update_type == INTNL_OVERLAY_UPDATE ||
           gf_update_type == KFFLT_OVERLAY_UPDATE ||
           gf_update_type == OVERLAY_UPDATE);
}

static AVM_INLINE void av2_set_high_precision_mv(AV2_COMP *cpi,
                                                 MvSubpelPrecision precision) {
  FeatureFlags *features = &cpi->common.features;
  features->fr_mv_precision = precision;
  features->use_pb_mv_precision = 0;
}

void av2_pick_and_set_high_precision_mv(AV2_COMP *cpi, int qindex);

#endif  // AVM_AV2_ENCODER_MV_PREC_H_
