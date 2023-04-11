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

#ifndef AOM_AOM_DSP_FLOW_ESTIMATION_RANSAC_H_
#define AOM_AOM_DSP_FLOW_ESTIMATION_RANSAC_H_

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include <stdbool.h>

#include "aom_dsp/flow_estimation/flow_estimation.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ransac(const Correspondence *matched_points, int npoints,
            TransformationType type, MotionModel *motion_models,
            int num_desired_motions);

bool aom_fit_motion_model(TransformationType type, int np, double *pts1,
                          double *pts2, double *mat);

// Using 8-point algorithm to find the fundamental matrix F such that
// [x1 y1 1] F [x2 y2 1]T = 0, where [x1 y1] and [x2 y2] are the corresponding
// points between two images
bool find_fundamental_matrix(int np, const double *cpts1, const double *cpts2,
                             double *F);

#ifdef __cplusplus
}
#endif

#endif  // AOM_AOM_DSP_FLOW_ESTIMATION_RANSAC_H_
