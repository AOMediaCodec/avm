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

#ifndef AVM_AV2_ENCODER_PICKCCSO_H_
#define AVM_AV2_ENCODER_PICKCCSO_H_

#define CCSO_MAX_ITERATIONS 15

#include "av2/common/ccso.h"
#include "av2/encoder/speed_features.h"

// Number of (d0, d1, band) combinations spanned by total_class_err/cnt.
#define CCSO_CLASS_STATS_ENTRIES \
  (CCSO_INPUT_INTERVAL * CCSO_INPUT_INTERVAL * CCSO_BAND_NUM)

typedef struct {
  // Per-frame state — zeroed at the start of each av2_ccso_search call.
  uint8_t final_band_log2;
  int8_t best_filter_offset[CCSO_BAND_NUM * 16];
  int8_t final_filter_offset[CCSO_BAND_NUM * 16];
  bool best_filter_enabled;
  bool final_filter_enabled;
  uint8_t final_ext_filter_support;
  int final_reuse_ccso;
  int final_sb_reuse_ccso;
  uint8_t final_scale_idx;
  uint8_t final_quant_idx;
  uint8_t final_ccso_bo_only;
  int chroma_error[CCSO_BAND_NUM * 16];
  int chroma_count[CCSO_BAND_NUM * 16];
  int *total_class_err[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_cnt[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_err_bo[CCSO_BAND_NUM];
  int *total_class_cnt_bo[CCSO_BAND_NUM];
  int ccso_stride;
  int ccso_stride_ext;
  uint64_t unfiltered_dist_frame;
  uint64_t filtered_dist_frame;
  int *reuse_total_class_err[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL]
                            [CCSO_BAND_NUM];
  int *reuse_total_class_cnt[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL]
                            [CCSO_BAND_NUM];

  // Persistent fields — survive across frames; ccso_ctx_reset zeros everything
  // above this boundary via offsetof(CcsoCtx, class_err_slab).
  // Adding a new allocated pointer: place it here and free it in
  // av2_ccso_ctx_free. Adding a new per-frame field: place it above.
  int *class_err_slab;        // backs total_class_err
  int *class_cnt_slab;        // backs total_class_cnt
  int *class_err_bo_slab;     // backs total_class_err_bo
  int *class_cnt_bo_slab;     // backs total_class_cnt_bo
  int *reuse_class_err_slab;  // backs reuse_total_class_err
  int *reuse_class_cnt_slab;  // backs reuse_total_class_cnt
  uint64_t *unfiltered_dist_block;
  uint64_t *training_dist_block;
  bool *filter_control;
  bool *best_filter_control;
  bool *final_filter_control;
  uint16_t *temp_rec_uv_buf;
  uint8_t *src_cls0;
  uint8_t *src_cls1;
  int alloc_sb_count;
  int alloc_reuse_sb_count;
  size_t alloc_luma_size;
} CcsoCtx;

#ifdef __cplusplus
extern "C" {
#endif

struct AV2_COMP;
struct ThreadData;

void av2_ccso_search(AV2_COMMON *cm, MACROBLOCKD *xd, int rdmult,
                     const uint16_t *ext_rec_y, uint16_t *rec_uv[MAX_MB_PLANE],
                     uint16_t *org_uv[MAX_MB_PLANE],
                     bool error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                     ,
                     struct ThreadData *td
#endif
                     ,
                     int early_terminate_ccso_search, int ccso_chroma_dep,
                     CcsoCtx *ctx);

void av2_ccso_ctx_free(struct AV2_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_ENCODER_PICKCCSO_H_
