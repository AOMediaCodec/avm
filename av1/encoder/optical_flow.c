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
#include <math.h>
#include <limits.h>

#include "config/aom_config.h"
#include "aom_dsp/mathutils.h"
#include "av1/common/av1_common_int.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/optical_flow.h"
#include "av1/encoder/reconinter_enc.h"
#include "aom_mem/aom_mem.h"

#if CONFIG_OPTICAL_FLOW_API

typedef struct LOCALMV {
  double row;
  double col;
} LOCALMV;

// Computes optical flow by applying algorithm at
// multiple pyramid levels of images (lower-resolution, smoothed images)
// This accounts for larger motions.
// Inputs:
//   from_frame Frame buffer.
//   to_frame: Frame buffer. MVs point from_frame -> to_frame.
//   from_frame_idx: Index of from_frame.
//   to_frame_idx: Index of to_frame. Return all zero MVs when idx are equal.
//   bit_depth:
//   opfl_params: contains algorithm-specific parameters.
//   mv_filter: MV_FILTER_NONE, MV_FILTER_SMOOTH, or MV_FILTER_MEDIAN.
//   method: LUCAS_KANADE,
//   mvs: pointer to MVs. Contains initialization, and modified
//   based on optical flow. Must have
//   dimensions = from_frame->y_crop_width * from_frame->y_crop_height
void optical_flow(const YV12_BUFFER_CONFIG *from_frame,
                  const YV12_BUFFER_CONFIG *to_frame, const int from_frame_idx,
                  const int to_frame_idx, const int bit_depth,
                  const OPFL_PARAMS *opfl_params,
                  const MV_FILTER_TYPE mv_filter, const OPTFLOW_METHOD method,
                  MV *mvs) {
  // parameters to be used in later implementations
  (void)to_frame;
  (void)bit_depth;
  (void)opfl_params;
  (void)mv_filter;
  (void)method;
  const int frame_height = from_frame->y_crop_height;
  const int frame_width = from_frame->y_crop_width;
  // TODO(any): deal with the case where frames are not of the same dimensions
  assert(frame_height == to_frame->y_crop_height &&
         frame_width == to_frame->y_crop_width);
  if (from_frame_idx == to_frame_idx) {
    // immediately return all zero mvs when frame indices are equal
    for (int yy = 0; yy < frame_height; yy++) {
      for (int xx = 0; xx < frame_width; xx++) {
        MV mv = { .row = 0, .col = 0 };
        mvs[yy * frame_width + xx] = mv;
      }
    }
    return;
  }
  // Initialize double mvs based on input parameter mvs array
  LOCALMV *localmvs = aom_malloc(frame_height * frame_width * sizeof(LOCALMV));
  for (int i = 0; i < frame_width * frame_height; i++) {
    MV mv = mvs[i];
    LOCALMV localmv = { .row = ((double)mv.row) / 8,
                        .col = ((double)mv.col) / 8 };
    localmvs[i] = localmv;
  }
  // Apply optical flow algorithm

  // Update original mvs array
  for (int j = 0; j < frame_height; j++) {
    for (int i = 0; i < frame_width; i++) {
      int idx = j * frame_width + i;
      int new_x = (int)(localmvs[idx].row + i);
      int new_y = (int)(localmvs[idx].col + j);
      if ((fabs(localmvs[idx].row) >= 0.125 ||
           fabs(localmvs[idx].col) >= 0.125)) {
        // if mv points outside of frame (lost feature), keep old mv.
        if (new_x < frame_width && new_x >= 0 && new_y < frame_height &&
            new_y >= 0) {
          MV mv = { .row = (int16_t)round(8 * localmvs[idx].row),
                    .col = (int16_t)round(8 * localmvs[idx].col) };
          mvs[idx] = mv;
        }
      }
    }
  }
  aom_free(localmvs);
}
#endif
