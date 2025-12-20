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

#include <assert.h>

#include "avm_dsp/flow_estimation/corner_detect.h"
#include "avm_dsp/flow_estimation/corner_match.h"
#include "avm_dsp/flow_estimation/disflow.h"
#include "avm_dsp/flow_estimation/flow_estimation.h"
#include "avm_ports/mem.h"
#include "avm_scale/yv12config.h"

// Compute a global motion model between the given source and ref frames.
//
// As is standard for video codecs, the resulting model maps from (x, y)
// coordinates in `src` to the corresponding points in `ref`, regardless
// of the temporal order of the two frames.
//
// Returns true if global motion estimation succeeded, false if not.
// The output models should only be used if this function succeeds.
bool avm_compute_global_motion(TransformationType type, YV12_BUFFER_CONFIG *src,
                               YV12_BUFFER_CONFIG *ref, int bit_depth,
                               GlobalMotionMethod gm_method,
                               int downsample_level, MotionModel *motion_models,
                               int num_motion_models, bool *mem_alloc_failed) {
  switch (gm_method) {
    case GLOBAL_MOTION_METHOD_FEATURE_MATCH:
      return av2_compute_global_motion_feature_match(
          type, src, ref, bit_depth, downsample_level, motion_models,
          num_motion_models, mem_alloc_failed);
    case GLOBAL_MOTION_METHOD_DISFLOW:
      return av2_compute_global_motion_disflow(
          type, src, ref, bit_depth, downsample_level, motion_models,
          num_motion_models, mem_alloc_failed);
    default: assert(0 && "Unknown global motion estimation type");
  }
  return false;
}
