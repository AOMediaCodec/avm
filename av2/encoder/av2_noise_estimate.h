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

#ifndef AVM_AV2_ENCODER_AV2_NOISE_ESTIMATE_H_
#define AVM_AV2_ENCODER_AV2_NOISE_ESTIMATE_H_

#include "av2/encoder/block.h"
#include "avm_scale/yv12config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_VAR_HIST_BINS 20

typedef enum noise_level { kLowLow, kLow, kMedium, kHigh } NOISE_LEVEL;

typedef struct noise_estimate {
  int enabled;
  NOISE_LEVEL level;
  int value;
  int thresh;
  int adapt_thresh;
  int count;
  int last_w;
  int last_h;
  int num_frames_estimate;
} NOISE_ESTIMATE;

struct AV2_COMP;

void av2_noise_estimate_init(NOISE_ESTIMATE *const ne, int width, int height);

NOISE_LEVEL av2_noise_estimate_extract_level(NOISE_ESTIMATE *const ne);

void av2_update_noise_estimate(struct AV2_COMP *const cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_AV2_NOISE_ESTIMATE_H_
