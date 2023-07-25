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

#ifndef AOM_AV1_ENCODER_GLOBAL_MOTION_FACADE_H_
#define AOM_AV1_ENCODER_GLOBAL_MOTION_FACADE_H_

#ifdef __cplusplus
extern "C" {
#endif
struct yv12_buffer_config;
struct AV1_COMP;

void av1_compute_gm_for_valid_ref_frames(
    struct AV1_COMP *cpi, YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME],
    int frame, int num_src_corners, int *src_corners, unsigned char *src_buffer,
    MotionModel *params_by_motion, uint8_t *segment_map, int segment_map_w,
    int segment_map_h);
void av1_compute_global_motion_facade(struct AV1_COMP *cpi);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_GLOBAL_MOTION_FACADE_H_
