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

#ifndef AOM_AV1_ENCODER_SCALE_H_
#define AOM_AV1_ENCODER_SCALE_H_

#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av1_setup_frame_size(AV1_COMP *cpi);

// Returns 1 if a frame is scaled and 0 otherwise.
static INLINE int av1_resize_scaled(const AV1_COMMON *cm) {
  return !(cm->width == cm->render_width && cm->height == cm->render_height);
}

static INLINE int av1_frame_scaled(const AV1_COMMON *cm) {
  return av1_resize_scaled(cm);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_SCALE_H_
