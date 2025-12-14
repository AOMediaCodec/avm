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

#ifndef AVM_AV2_ENCODER_ENCODEFRAME_UTILS_H_
#define AVM_AV2_ENCODER_ENCODEFRAME_UTILS_H_

#include "avm_ports/system_state.h"

#include "av2/common/reconinter.h"

#include "av2/encoder/encoder.h"
#include "av2/encoder/partition_strategy.h"
#include "av2/encoder/rdopt.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SB_SINGLE_PASS,  // Single pass encoding: all ctxs get updated normally
  SB_DRY_PASS,     // First pass of multi-pass: does not update the ctxs
  SB_WET_PASS      // Second pass of multi-pass: finalize and update the ctx
} UENUM1BYTE(SB_MULTI_PASS_MODE);

typedef struct {
  ENTROPY_CONTEXT a[MAX_MIB_SIZE * MAX_MB_PLANE];
  ENTROPY_CONTEXT l[MAX_MIB_SIZE * MAX_MB_PLANE];
  PARTITION_CONTEXT sa[MAX_MIB_SIZE * MAX_MB_PLANE];
  PARTITION_CONTEXT sl[MAX_MIB_SIZE * MAX_MB_PLANE];

  //! The current level bank, used to restore the level bank in MACROBLOCKD.
  REF_MV_BANK curr_level_bank;
  //! The best level bank from the rdopt process.
  REF_MV_BANK best_level_bank;
#if WARP_CU_BANK
  //! The current warp, level bank, used to restore the warp level bank in
  //! MACROBLOCKD.
  WARP_PARAM_BANK curr_level_warp_bank;
  //! The best warp level bank from the rdopt process.
  WARP_PARAM_BANK best_level_warp_bank;
#endif  // WARP_CU_BANK
} RD_SEARCH_MACROBLOCK_CONTEXT;

// This struct is used to store the statistics used by sb-level multi-pass
// encoding. Currently, this is only used to make a copy of the state before we
// perform the first pass
typedef struct SB_FIRST_PASS_STATS {
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  RD_COUNTS rd_count;

  int split_count;
  FRAME_COUNTS fc;
  InterModeRdModel inter_mode_rd_models[BLOCK_SIZES_ALL];
  int thresh_freq_fact[BLOCK_SIZES_ALL][MB_MODE_COUNT];
  int current_qindex;
  REF_MV_BANK ref_mv_bank;
#if WARP_CU_BANK
  WARP_PARAM_BANK warp_param_bank;
#endif  // WARP_CU_BANK
  BLOCK_SIZE min_partition_size;
} SB_FIRST_PASS_STATS;

// This structure contains block size related
// variables for use in rd_pick_partition().
typedef struct PartitionBlkParams {
  // Half of block width to determine block edge.
  int mi_step;
  int mi_step_h;
  int mi_step_w;

  // Block row and column indices.
  int mi_row;
  int mi_col;

  // Block edge row and column indices.
  int mi_row_edge;
  int mi_col_edge;

  // Block width of current partition block.
  int width;

  // Minimum partition size allowed.
  BLOCK_SIZE min_partition_size;

  // Indicates that more than half of the rows / cols of this block are within
  // the frame.
  int has_rows;
  int has_cols;

  // Block size of current partition.
  BLOCK_SIZE bsize;

  // Size of current sub-partition.
  BLOCK_SIZE subsize;
} PartitionBlkParams;

// Structure holding state variables for partition search.
typedef struct PartitionSearchState {
  // Intra partitioning related info.
  PartitionSearchInfo *intra_part_info;

  // Parameters related to partition block size.
  PartitionBlkParams part_blk_params;

  // Win flags for HORZ and VERT partition evaluations.
  RD_RECT_PART_WIN_INFO split_part_rect_win[SUB_PARTITIONS_SPLIT];

  // RD cost for the current block of given partition type.
  RD_STATS this_rdc;

  // RD cost summed across all blocks of partition type.
  RD_STATS sum_rdc;

  // Partition costs for each type of partition.
  int partition_cost[ALL_PARTITION_TYPES];

  // Pointer to region type cost buffer
  int *region_type_cost;

  // RD costs for different partition types.
  int64_t none_rd;
  int64_t split_rd[SUB_PARTITIONS_SPLIT];
  // RD costs for rectangular partitions.
  // rect_part_rd[0][i] is the RD cost of ith partition index of PARTITION_HORZ.
  // rect_part_rd[1][i] is the RD cost of ith partition index of PARTITION_VERT.
  int64_t rect_part_rd[NUM_RECT_PARTS][SUB_PARTITIONS_RECT];

  // Flags to prune/skip particular partition size evaluation.
  int terminate_partition_search;
  int partition_none_allowed;
  int partition_rect_allowed[NUM_RECT_PARTS];
  int do_rectangular_split;
  int partition_split_allowed;
  bool prune_partition_none;
#if CONFIG_ML_PART_SPLIT
  bool prune_partition_split;
#endif  // CONFIG_ML_PART_SPLIT
  bool partition_3_allowed[NUM_RECT_PARTS];
  bool prune_partition_3[NUM_RECT_PARTS];
  bool partition_4a_allowed[NUM_RECT_PARTS];
  bool partition_4b_allowed[NUM_RECT_PARTS];
  bool prune_partition_4a[NUM_RECT_PARTS];
  bool prune_partition_4b[NUM_RECT_PARTS];
  PARTITION_TYPE forced_partition;
  // Pointer to an array that traces out the current best partition boundary.
  // Used by prune_part_h_with_partition_boundary and
  // prune_part_4_with_partition_boundary.
  bool *partition_boundaries;
  bool prune_rect_part[NUM_RECT_PARTS];
  int is_block_splittable;

  // Chroma subsampling in x and y directions.
  int ss_x;
  int ss_y;

  // This flag will be set if best partition is found from the search.
  bool found_best_partition;
} PartitionSearchState;

static AVM_INLINE void update_wedge_mode_cdf(FRAME_CONTEXT *fc,
                                             const BLOCK_SIZE bsize,
                                             const int8_t wedge_index
#if CONFIG_ENTROPY_STATS
                                             ,
                                             FRAME_COUNTS *const counts
#endif
) {
  (void)bsize;
  const int wedge_angle = wedge_index_2_angle[wedge_index];
  const int wedge_dist = wedge_index_2_dist[wedge_index];
  const int wedge_quad = (wedge_angle / QUAD_WEDGE_ANGLES);
  const int wedge_angle_in_quad = (wedge_angle % QUAD_WEDGE_ANGLES);
#if CONFIG_ENTROPY_STATS
  counts->wedge_quad_cnt[wedge_quad]++;
#endif
  update_cdf(fc->wedge_quad_cdf, wedge_quad, WEDGE_QUADS);
#if CONFIG_ENTROPY_STATS
  counts->wedge_angle_cnt[wedge_quad][wedge_angle_in_quad]++;
#endif
  update_cdf(fc->wedge_angle_cdf[wedge_quad], wedge_angle_in_quad,
             QUAD_WEDGE_ANGLES);
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_0)) {
    assert(wedge_dist != 0);
#if CONFIG_ENTROPY_STATS
    counts->wedge_dist2_cnt[wedge_dist - 1]++;
#endif
    update_cdf(fc->wedge_dist_cdf2, wedge_dist - 1, NUM_WEDGE_DIST - 1);
  } else {
#if CONFIG_ENTROPY_STATS
    counts->wedge_dist_cnt[wedge_dist]++;
#endif
    update_cdf(fc->wedge_dist_cdf, wedge_dist, NUM_WEDGE_DIST);
  }
}

static AVM_INLINE void update_filter_type_cdf(const MACROBLOCKD *xd,
                                              const MB_MODE_INFO *mbmi) {
  const int ctx = av2_get_pred_context_switchable_interp(xd, 0);
  update_cdf(xd->tile_ctx->switchable_interp_cdf[ctx], mbmi->interp_fltr,
             SWITCHABLE_FILTERS);
}

static AVM_INLINE int set_segment_rdmult(const AV2_COMP *const cpi,
                                         MACROBLOCK *const x,
                                         int8_t segment_id) {
  const AV2_COMMON *const cm = &cpi->common;
  av2_init_plane_quantizers(cpi, x, segment_id);
  avm_clear_system_state();

  int segment_qindex =
      av2_get_qindex(&cm->seg, segment_id, cm->quant_params.base_qindex,
                     cm->seq_params.bit_depth);
  return av2_compute_rd_mult(cpi,
                             segment_qindex + cm->quant_params.y_dc_delta_q);
}

static BLOCK_SIZE dim_to_size(int dim) {
  switch (dim) {
    case 4: return BLOCK_4X4;
    case 8: return BLOCK_8X8;
    case 16: return BLOCK_16X16;
    case 32: return BLOCK_32X32;
    case 64: return BLOCK_64X64;
    case 128: return BLOCK_128X128;
    case 256: return BLOCK_256X256;
    default: assert(0); return 0;
  }
}

static AVM_INLINE void set_max_min_partition_size(SuperBlockEnc *sb_enc,
                                                  AV2_COMP *cpi, MACROBLOCK *x,
                                                  const SPEED_FEATURES *sf,
                                                  BLOCK_SIZE sb_size,
                                                  int mi_row, int mi_col) {
  const AV2_COMMON *cm = &cpi->common;

  sb_enc->max_partition_size =
      AVMMIN(sf->part_sf.default_max_partition_size,
             dim_to_size(cpi->oxcf.part_cfg.max_partition_size));
  sb_enc->min_partition_size =
      AVMMAX(sf->part_sf.default_min_partition_size,
             dim_to_size(cpi->oxcf.part_cfg.min_partition_size));
  sb_enc->max_partition_size = AVMMIN(sb_enc->max_partition_size, cm->sb_size);
  sb_enc->min_partition_size = AVMMIN(sb_enc->min_partition_size, cm->sb_size);
  if (!bru_is_sb_active(cm, mi_col, mi_row)) return;

  if (use_auto_max_partition(cpi, sb_size, mi_row, mi_col)) {
    float features[FEATURE_SIZE_MAX_MIN_PART_PRED] = { 0.0f };

    av2_get_max_min_partition_features(cpi, x, mi_row, mi_col, features);
    sb_enc->max_partition_size =
        AVMMAX(AVMMIN(av2_predict_max_partition(cpi, x, features),
                      sb_enc->max_partition_size),
               sb_enc->min_partition_size);
  }
}

// Allocates memory for 'InterModesInfo' buffer, which is used during the
// evaluation of inter modes. The allocation is avoided for intra frames as
// this buffer is required only for inter frames.
static AVM_INLINE void alloc_inter_modes_info_data(AV2_COMMON *const cm,
                                                   struct macroblock *mb) {
  if (frame_is_intra_only(cm)) return;
  CHECK_MEM_ERROR(cm, mb->inter_modes_info,
                  (InterModesInfo *)avm_malloc(sizeof(*mb->inter_modes_info)));
}

// Free the memory corresponding to 'InterModesInfo' buffer.
static AVM_INLINE void dealloc_inter_modes_info_data(struct macroblock *mb) {
  avm_free(mb->inter_modes_info);
  mb->inter_modes_info = NULL;
}

int av2_get_rdmult_delta(AV2_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int orig_rdmult);

int av2_active_h_edge(const AV2_COMP *cpi, int mi_row, int mi_step);

int av2_active_v_edge(const AV2_COMP *cpi, int mi_col, int mi_step);

void av2_get_tpl_stats_sb(AV2_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                          int mi_col, SuperBlockEnc *sb_enc);

int av2_get_q_for_deltaq_objective(AV2_COMP *const cpi, BLOCK_SIZE bsize,
                                   int mi_row, int mi_col);

void av2_set_ssim_rdmult(const AV2_COMP *const cpi, MvCosts *const mv_costs,
                         const BLOCK_SIZE bsize, const int mi_row,
                         const int mi_col, int *const rdmult);

int av2_get_hier_tpl_rdmult(const AV2_COMP *const cpi, MACROBLOCK *const x,
                            const BLOCK_SIZE bsize, const int mi_row,
                            const int mi_col, int orig_rdmult);

void av2_update_state(const AV2_COMP *const cpi, ThreadData *td,
                      const PICK_MODE_CONTEXT *const ctx, int mi_row,
                      int mi_col, BLOCK_SIZE bsize, RUN_TYPE dry_run);

void av2_update_inter_mode_stats(FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                 PREDICTION_MODE mode, int16_t mode_context,
                                 const AV2_COMMON *const cm,
                                 const MACROBLOCKD *xd,
                                 const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize);

void av2_sum_intra_stats(const AV2_COMMON *const cm, FRAME_COUNTS *counts,
                         MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi);

void av2_restore_context(const AV2_COMMON *cm, MACROBLOCK *x,
                         const RD_SEARCH_MACROBLOCK_CONTEXT *ctx, int mi_row,
                         int mi_col, BLOCK_SIZE bsize, const int num_planes);

void av2_save_context(const MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *ctx,
                      int mi_row, int mi_col, BLOCK_SIZE bsize,
                      const int num_planes);

void av2_set_fixed_partitioning(AV2_COMP *cpi, const TileInfo *const tile,
                                MB_MODE_INFO **mib, int mi_row, int mi_col,
                                BLOCK_SIZE bsize);

void av2_reset_simple_motion_tree_partition(SIMPLE_MOTION_DATA_TREE *sms_tree,
                                            BLOCK_SIZE bsize);

void av2_update_picked_ref_frames_mask(MACROBLOCK *const x, int ref_type,
                                       BLOCK_SIZE bsize, int mib_size,
                                       int mi_row, int mi_col);
void av2_reset_mbmi(const CommonModeInfoParams *const mi_params,
                    BLOCK_SIZE sb_size, int mi_row, int mi_col);

void av2_backup_sb_state(SB_FIRST_PASS_STATS *sb_fp_stats, const AV2_COMP *cpi,
                         ThreadData *td, const TileDataEnc *tile_data,
                         int mi_row, int mi_col);

void av2_restore_sb_state(const SB_FIRST_PASS_STATS *sb_fp_stats, AV2_COMP *cpi,
                          ThreadData *td, TileDataEnc *tile_data, int mi_row,
                          int mi_col);

void av2_set_cost_upd_freq(AV2_COMP *cpi, ThreadData *td,
                           const TileInfo *const tile_info, const int mi_row,
                           const int mi_col);

#ifndef NDEBUG
static AVM_INLINE int is_bsize_square(BLOCK_SIZE bsize) {
  return block_size_wide[bsize] == block_size_high[bsize];
}
#endif  // NDEBUG

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ENCODEFRAME_UTILS_H_
