/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AVM_AVM_DSP_FLOW_ESTIMATION_RANSAC_H_
#define AVM_AVM_DSP_FLOW_ESTIMATION_RANSAC_H_

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include <stdbool.h>

#include "avm_dsp/flow_estimation/flow_estimation.h"

#ifdef __cplusplus
extern "C" {
#endif

bool avm_ransac(const Correspondence *matched_points, int npoints,
                TransformationType type, MotionModel *motion_models,
                int num_desired_motions, bool *mem_alloc_failed);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AVM_DSP_FLOW_ESTIMATION_RANSAC_H_
