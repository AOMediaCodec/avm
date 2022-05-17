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
#ifndef AOM_AV1_ENCODER_ERP_TFLITE_H_
#define AOM_AV1_ENCODER_ERP_TFLITE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/common/av1_common_int.h"

int av1_erp_prune_rect(BLOCK_SIZE bsize, bool is_hd, const float *features,
                       bool *prune_horz, bool *prune_vert);

#ifdef __cplusplus
}
#endif
#endif  // AOM_AV1_ENCODER_ERP_TFLITE_H_
