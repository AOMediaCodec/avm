/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */
#ifndef AVM_AV2_ENCODER_ERP_ML_H_
#define AVM_AV2_ENCODER_ERP_ML_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/common/av2_common_int.h"

int av2_erp_prune_rect(BLOCK_SIZE bsize, bool is_hd, const float *features,
                       bool *prune_horz, bool *prune_vert);

#ifdef __cplusplus
}
#endif
#endif  // AVM_AV2_ENCODER_ERP_ML_H_
