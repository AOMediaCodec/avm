/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_COMMON_PREDEFINED_QM_H_
#define AOM_AV1_COMMON_PREDEFINED_QM_H_

#include "av1/common/quant_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_QM_REVERT
extern const qm_val_t predefined_iwt_matrix_ref[NUM_QM_LEVELS - 1][2]
                                               [QM_TOTAL_SIZE];
extern const qm_val_t predefined_wt_matrix_ref[NUM_QM_LEVELS - 1][2]
                                              [QM_TOTAL_SIZE];
#endif  // CONFIG_QM_REVERT

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_PREDEFINED_QM_H_