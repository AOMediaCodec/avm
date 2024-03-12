/*
 * Copyright (c) 2020, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AOM_AV1_COMMON_CNN_TFLITE_H
#define AOM_AV1_COMMON_CNN_TFLITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/common/av1_common_int.h"
#include "av1/common/resize.h"

#include "av1/encoder/rd.h"
#include "av1/encoder/ratectrl.h"
#if CONFIG_CNN_GUIDED_QUADTREE
#include "av1/common/guided_quadtree.h"
#endif  // CONFIG_CNN_GUIDED_QUADTREE

// Minimum base_qindex needed to run cnn.
#define MIN_CNN_Q_INDEX 0
// Number of options for Y channel CNN model, with chosen option signaled in the
// bitstream as an index.
#define CNN_INDICES_Y 3
// Number of bits required to signal combined code for cnn_index
// (CNN_INDICES_Y) and whether CNN is used or not (1).
#define CNN_INDEX_Y_BITS 2  // log2(CNN_INDICES_Y + 1)
#if CONFIG_EXT_SUPERRES
#define SELECT_CNN_FOR_SUPERRES 1
#endif  // CONFIG_EXT_SUPERRES

// Returns true if we are allowed to use CNN for restoration for the frame.
static INLINE bool av1_allow_cnn(const AV1_COMMON *cm) {
  return (cm->quant_params.base_qindex > MIN_CNN_Q_INDEX)
#if !CONFIG_EXT_SUPERRES
         && !av1_superres_scaled(cm)
#endif  // !CONFIG_EXT_SUPERRES
      ;
}

// Returns true if we are allowed to use CNN for restoration for the plane.
static INLINE bool av1_allow_cnn_for_plane(const AV1_COMMON *cm, int plane) {
  (void)plane;
  if (!av1_allow_cnn(cm)) return false;
  if (plane >= av1_num_planes(cm)) return false;
  if (av1_superres_scaled(cm)) {
#if CONFIG_EXT_SUPERRES
    return (plane == 0);
#else
    return false;
#endif  // CONFIG_EXT_SUPERRES
  } else {
    return (plane == 0);
  }
}

static INLINE int av1_num_cnn_indices_for_plane(const AV1_COMMON *cm,
                                                int plane) {
  if (!av1_allow_cnn_for_plane(cm, plane)) return 0;
#if CONFIG_EXT_SUPERRES && SELECT_CNN_FOR_SUPERRES
  if (av1_superres_scaled(cm)) return plane == AOM_PLANE_Y ? CNN_INDICES_Y : 1;
#else   // CONFIG_EXT_SUPERRES && SELECT_CNN_FOR_SUPERRES
  if (av1_superres_scaled(cm)) return 1;
#endif  // CONFIG_EXT_SUPERRES && SELECT_CNN_FOR_SUPERRES
  if (plane == AOM_PLANE_Y) {
    return CNN_INDICES_Y;
  } else {
    return 1;
  }
}

static INLINE int av1_num_cnn_combined_code_bits(int num_indices) {
  assert(num_indices == CNN_INDICES_Y);
  (void)num_indices;
  return CNN_INDEX_Y_BITS;
}

// Returns true if we are allowed to use CNN for restoration.
static INLINE bool av1_use_cnn_encode(const AV1_COMMON *cm,
                                      FRAME_UPDATE_TYPE update_type) {
  const bool is_overlay_update =
      (update_type == OVERLAY_UPDATE || update_type == INTNL_OVERLAY_UPDATE);

  return av1_allow_cnn(cm) && !is_overlay_update;
}

// Restores image in 'dgd' with a CNN model using TFlite and stores output in
// 'rst'. Returns true on success.
int av1_restore_cnn_img_tflite_highbd(int qindex, int superres_denom,
                                      const uint16_t *dgd, int width,
                                      int height, int dgd_stride, uint16_t *rst,
                                      int rst_stride, int num_threads,
                                      int bit_depth, int is_intra_only,
                                      int is_luma, int cnn_index);

struct AV1Common;

// Restore current frame buffer in 'cm' in-place with a CNN model using TFlite.
// Apply CNN to plane 'p' if and only if apply_cnn[p] is true.
void av1_restore_cnn_tflite(const struct AV1Common *cm, int num_threads,
                            const int apply_cnn[MAX_MB_PLANE],
                            const int cnn_indices[MAX_MB_PLANE]);

#if CONFIG_CNN_GUIDED_QUADTREE

// Apply Guided CNN restoration on encoder side.
int av1_restore_cnn_quadtree_encode_tflite(
    struct AV1Common *cm, YV12_BUFFER_CONFIG *source_frame, int RDMULT,
    int *quad_split_costs, int *binary_split_costs, int (*norestorecosts)[2],
    int num_threads, const int apply_cnn[MAX_MB_PLANE],
    const int cnn_indices[MAX_MB_PLANE], QUADInfo *quad_info, double *rdcost);

// Apply Guided CNN restoration on decoder side.
int av1_restore_cnn_quadtree_decode_tflite(struct AV1Common *cm,
                                           int num_threads, int use_quadtree,
                                           const int apply_cnn[MAX_MB_PLANE],
                                           const int cnn_indices[MAX_MB_PLANE]);
#endif  // CONFIG_CNN_GUIDED_QUADTREE

#ifdef __cplusplus
}
#endif

#endif  // AOM_AV1_COMMON_CNN_TFLITE_H
