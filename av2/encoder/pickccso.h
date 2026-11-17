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

#include "av2/common/ccso.h"
#include "av2/encoder/speed_features.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CCSO_MAX_ITERATIONS 15

// Number of (d0, d1, band) combinations spanned by total_class_err/cnt.
#define CCSO_CLASS_STATS_ENTRIES \
  (CCSO_INPUT_INTERVAL * CCSO_INPUT_INTERVAL * CCSO_BAND_NUM)

typedef struct {
  const uint16_t *org_uv;
  const uint16_t *ext_rec_y;
  const uint16_t *rec_uv;
  uint64_t *unfiltered_dist_block;
  int plane;
  int rdmult;
  int ccso_blk_size;
  int log2_filter_unit_size_x;
  int log2_filter_unit_size_y;
  int log2_proc_unit_size;
  int ccso_nvfb;
  int ccso_nhfb;
  int sb_count;
  uint8_t frame_bits;
  uint8_t frame_bits_bo_only;
  int check_ccso;
  int num_ref_frames;
  int early_terminate_ccso_search;
} CcsoCtxCommon;

typedef struct {
  int8_t filter_offset[CCSO_BAND_NUM * 16];
  uint64_t filtered_cost;
  int ref_idx;
  uint8_t band_log2;
  uint8_t ext_filter_support;
  uint8_t reuse_ccso;
  uint8_t sb_reuse_ccso;
  uint8_t scale_idx;
  uint8_t quant_idx;
  uint8_t ccso_bo_only;
  uint8_t edge_classifier;
} CcsoCandidate;

typedef struct {
  // Per-frame state — zeroed at the start of each av2_ccso_search call.
  CcsoCtxCommon ccso_cm;

  // Best candidate found across the whole search.
  CcsoCandidate final;

  // Best candidate found so far for the current max_band_log2 iteration.
  CcsoCandidate best;

  // Coordinates of the candidate currently under evaluation
  uint8_t scale_idx;
  uint8_t ccso_bo_only;
  uint8_t ext_filter_support;
  uint8_t quant_idx;
  uint8_t edge_clf;
  uint8_t max_band_log2;
  uint8_t reuse_ccso_idx;
  uint8_t sb_reuse_idx;
  int ref_idx;

  uint8_t max_edge_interval;
  uint8_t num_band_iter;
  bool check_sb_reuse;
  CcsoInfo *ref_frame_ccso_info;
  bool skip_filter_calculation;
  int shift_bits;
  int init_shift_bits;
  uint64_t last_best_cost;
  unsigned int checked_reuse_ref[2][7];
  int checked_reuse_ref_idx[2];
  int8_t filter_offset[CCSO_BAND_NUM * 16];

  int chroma_error[CCSO_BAND_NUM * 16];
  int chroma_count[CCSO_BAND_NUM * 16];
  int *total_class_err[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_cnt[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_err_bo[CCSO_BAND_NUM];
  int *total_class_cnt_bo[CCSO_BAND_NUM];
  int ccso_stride;
  int ccso_stride_ext;
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

void av2_ccso_ctx_free(struct AV2_COMP *cpi);

bool av2_ccso_param_search(AV2_COMMON *cm, CcsoCtx *ctx, MACROBLOCKD *xd);

void av2_ccso_search(struct AV2_COMP *cpi, const uint16_t *ext_rec_y,
                     uint16_t *rec_uv[MAX_MB_PLANE],
                     uint16_t *org_uv[MAX_MB_PLANE],
                     bool error_resilient_frame_seen,
                     int early_terminate_ccso_search, int ccso_chroma_dep);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_ENCODER_PICKCCSO_H_
