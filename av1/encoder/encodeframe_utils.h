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

#ifndef AOM_AV1_ENCODER_ENCODEFRAME_UTILS_H_
#define AOM_AV1_ENCODER_ENCODEFRAME_UTILS_H_

#include "aom_ports/system_state.h"

#include "av1/common/reconinter.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/rdopt.h"

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
  TXFM_CONTEXT *p_ta;
  TXFM_CONTEXT *p_tl;
  TXFM_CONTEXT ta[MAX_MIB_SIZE];
  TXFM_CONTEXT tl[MAX_MIB_SIZE];
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
#if CONFIG_NEW_REF_SIGNALING
  int thresh_freq_fact[BLOCK_SIZES_ALL][MB_MODE_COUNT];
#else
  int thresh_freq_fact[BLOCK_SIZES_ALL][MAX_MODES];
#endif  // CONFIG_NEW_REF_SIGNALING
  int current_qindex;
#if CONFIG_C043_MVP_IMPROVEMENTS
  REF_MV_BANK ref_mv_bank;
#endif  // CONFIG_C043_MVP_IMPROVEMENTS
#if WARP_CU_BANK
  WARP_PARAM_BANK warp_param_bank;
#endif  // WARP_CU_BANK
#if CONFIG_EXT_RECUR_PARTITIONS
  BLOCK_SIZE min_partition_size;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_INTERNAL_STATS && !CONFIG_NEW_REF_SIGNALING
  unsigned int mode_chosen_counts[MAX_MODES];
#endif  // CONFIG_INTERNAL_STATS && !CONFIG_NEW_REF_SIGNALING
} SB_FIRST_PASS_STATS;

// This structure contains block size related
// variables for use in rd_pick_partition().
typedef struct PartitionBlkParams {
  // Half of block width to determine block edge.
  int mi_step;
#if CONFIG_EXT_RECUR_PARTITIONS
  int mi_step_h;
  int mi_step_w;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Block row and column indices.
  int mi_row;
  int mi_col;

  // Block edge row and column indices.
  int mi_row_edge;
  int mi_col_edge;

  // Block width of current partition block.
  int width;

#if CONFIG_EXT_RECUR_PARTITIONS
  // Minimum partition size allowed.
  BLOCK_SIZE min_partition_size;
#else
  // Block width of minimum partition size allowed.
  int min_partition_size_1d;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if !CONFIG_EXT_RECUR_PARTITIONS
  // Flag to indicate if partition is 8x8 or higher size.
  int bsize_at_least_8x8;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  // Indicates edge blocks in frame.
  int has_rows;
  int has_cols;

  // Block size of current partition.
  BLOCK_SIZE bsize;

  // Size of current sub-partition.
  BLOCK_SIZE subsize;

  // Size of split partition.
  BLOCK_SIZE split_bsize2;
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

  // Array holding partition type cost.
  int tmp_partition_cost[PARTITION_TYPES];
#if CONFIG_EXT_RECUR_PARTITIONS
  int partition_cost_table[EXT_PARTITION_TYPES];
#endif

  // Pointer to partition cost buffer
  int *partition_cost;

  // RD costs for different partition types.
  int64_t none_rd;
  int64_t split_rd[SUB_PARTITIONS_SPLIT];
  // RD costs for rectangular partitions.
  // rect_part_rd[0][i] is the RD cost of ith partition index of PARTITION_HORZ.
  // rect_part_rd[1][i] is the RD cost of ith partition index of PARTITION_VERT.
  int64_t rect_part_rd[NUM_RECT_PARTS][SUB_PARTITIONS_RECT];

#if CONFIG_EXT_RECUR_PARTITIONS
  // New Simple Motion Result for PARTITION_NONE
  SMSPartitionStats none_data;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  // Flags indicating if the corresponding partition was winner or not.
  // Used to bypass similar blocks during AB partition evaluation.
  int is_split_ctx_is_ready[2];
  int is_rect_ctx_is_ready[NUM_RECT_PARTS];

  // Flags to prune/skip particular partition size evaluation.
  int terminate_partition_search;
  int partition_none_allowed;
  int partition_rect_allowed[NUM_RECT_PARTS];
  int do_rectangular_split;
#if !CONFIG_EXT_RECUR_PARTITIONS
  int do_square_split;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  int prune_rect_part[NUM_RECT_PARTS];
  int is_block_splittable;

  // Chroma subsampling in x and y directions.
  int ss_x;
  int ss_y;

  // Partition plane context index.
  int pl_ctx_idx;

  // This flag will be set if best partition is found from the search.
  bool found_best_partition;
} PartitionSearchState;

static AOM_INLINE void update_global_motion_used(PREDICTION_MODE mode,
                                                 BLOCK_SIZE bsize,
                                                 const MB_MODE_INFO *mbmi,
                                                 RD_COUNTS *rdc) {
  if (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) {
    const int num_4x4s = mi_size_wide[bsize] * mi_size_high[bsize];
    int ref;
    for (ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
      rdc->global_motion_used[mbmi->ref_frame[ref]] += num_4x4s;
    }
  }
}

#if CONFIG_WEDGE_MOD_EXT
static AOM_INLINE void update_wedge_mode_cdf(FRAME_CONTEXT *fc,
                                             const BLOCK_SIZE bsize,
                                             const int8_t wedge_index
#if CONFIG_ENTROPY_STATS
                                             ,
                                             FRAME_COUNTS *const counts
#endif
) {
  const int wedge_angle = wedge_index_2_angle[wedge_index];
  const int wedge_dist = wedge_index_2_dist[wedge_index];
  const int wedge_angle_dir = (wedge_angle >= H_WEDGE_ANGLES);
#if CONFIG_ENTROPY_STATS
  counts->wedge_angle_dir_cnt[bsize][wedge_angle_dir]++;
#endif
  update_cdf(fc->wedge_angle_dir_cdf[bsize], wedge_angle_dir, 2);
  if (wedge_angle_dir == 0) {
#if CONFIG_ENTROPY_STATS
    counts->wedge_angle_0_cnt[bsize][wedge_angle]++;
#endif
    update_cdf(fc->wedge_angle_0_cdf[bsize], wedge_angle, H_WEDGE_ANGLES);
  } else {
#if CONFIG_ENTROPY_STATS
    counts->wedge_angle_1_cnt[bsize][wedge_angle - H_WEDGE_ANGLES]++;
#endif
    update_cdf(fc->wedge_angle_1_cdf[bsize], wedge_angle - H_WEDGE_ANGLES,
               H_WEDGE_ANGLES);
  }
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_180)) {
    assert(wedge_dist != 0);
#if CONFIG_ENTROPY_STATS
    counts->wedge_dist_3_cnt[bsize][wedge_dist - 1]++;
#endif
    update_cdf(fc->wedge_dist_cdf2[bsize], wedge_dist - 1, NUM_WEDGE_DIST - 1);
  } else {
#if CONFIG_ENTROPY_STATS
    counts->wedge_dist_cnt[bsize][wedge_dist]++;
#endif
    update_cdf(fc->wedge_dist_cdf[bsize], wedge_dist, NUM_WEDGE_DIST);
  }
}
#endif  // CONFIG_WEDGE_MOD_EXT

static AOM_INLINE void update_filter_type_cdf(const MACROBLOCKD *xd,
                                              const MB_MODE_INFO *mbmi) {
  const int ctx = av1_get_pred_context_switchable_interp(xd, 0);
  update_cdf(xd->tile_ctx->switchable_interp_cdf[ctx], mbmi->interp_fltr,
             SWITCHABLE_FILTERS);
}

static AOM_INLINE int set_segment_rdmult(const AV1_COMP *const cpi,
                                         MACROBLOCK *const x,
                                         int8_t segment_id) {
  const AV1_COMMON *const cm = &cpi->common;
  av1_init_plane_quantizers(cpi, x, segment_id);
  aom_clear_system_state();

  int segment_qindex =
      av1_get_qindex(&cm->seg, segment_id, cm->quant_params.base_qindex,
                     cm->seq_params.bit_depth);
  return av1_compute_rd_mult(cpi,
                             segment_qindex + cm->quant_params.y_dc_delta_q);
}

static AOM_INLINE int do_slipt_check(BLOCK_SIZE bsize) {
  return (bsize == BLOCK_16X16 || bsize == BLOCK_32X32);
}

static AOM_INLINE const FIRSTPASS_STATS *read_one_frame_stats(const TWO_PASS *p,
                                                              int frm) {
  assert(frm >= 0);
  if (frm < 0 ||
      p->stats_buf_ctx->stats_in_start + frm > p->stats_buf_ctx->stats_in_end) {
    return NULL;
  }

  return &p->stats_buf_ctx->stats_in_start[frm];
}

static BLOCK_SIZE dim_to_size(int dim) {
  switch (dim) {
    case 4: return BLOCK_4X4;
    case 8: return BLOCK_8X8;
    case 16: return BLOCK_16X16;
    case 32: return BLOCK_32X32;
    case 64: return BLOCK_64X64;
    case 128: return BLOCK_128X128;
    default: assert(0); return 0;
  }
}

static AOM_INLINE void set_max_min_partition_size(SuperBlockEnc *sb_enc,
                                                  AV1_COMP *cpi, MACROBLOCK *x,
                                                  const SPEED_FEATURES *sf,
                                                  BLOCK_SIZE sb_size,
                                                  int mi_row, int mi_col) {
  const AV1_COMMON *cm = &cpi->common;

  sb_enc->max_partition_size =
      AOMMIN(sf->part_sf.default_max_partition_size,
             dim_to_size(cpi->oxcf.part_cfg.max_partition_size));
  sb_enc->min_partition_size =
      AOMMAX(sf->part_sf.default_min_partition_size,
             dim_to_size(cpi->oxcf.part_cfg.min_partition_size));
  sb_enc->max_partition_size =
      AOMMIN(sb_enc->max_partition_size, cm->seq_params.sb_size);
  sb_enc->min_partition_size =
      AOMMIN(sb_enc->min_partition_size, cm->seq_params.sb_size);

  if (use_auto_max_partition(cpi, sb_size, mi_row, mi_col)) {
    float features[FEATURE_SIZE_MAX_MIN_PART_PRED] = { 0.0f };

    av1_get_max_min_partition_features(cpi, x, mi_row, mi_col, features);
    sb_enc->max_partition_size =
        AOMMAX(AOMMIN(av1_predict_max_partition(cpi, x, features),
                      sb_enc->max_partition_size),
               sb_enc->min_partition_size);
  }
}

int av1_get_rdmult_delta(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int orig_rdmult);

int av1_active_h_edge(const AV1_COMP *cpi, int mi_row, int mi_step);

int av1_active_v_edge(const AV1_COMP *cpi, int mi_col, int mi_step);

void av1_get_tpl_stats_sb(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                          int mi_col, SuperBlockEnc *sb_enc);

int av1_get_q_for_deltaq_objective(AV1_COMP *const cpi, BLOCK_SIZE bsize,
                                   int mi_row, int mi_col);

void av1_set_ssim_rdmult(const AV1_COMP *const cpi, MvCosts *const mv_costs,
                         const BLOCK_SIZE bsize, const int mi_row,
                         const int mi_col, int *const rdmult);

int av1_get_hier_tpl_rdmult(const AV1_COMP *const cpi, MACROBLOCK *const x,
                            const BLOCK_SIZE bsize, const int mi_row,
                            const int mi_col, int orig_rdmult);

void av1_update_state(const AV1_COMP *const cpi, ThreadData *td,
                      const PICK_MODE_CONTEXT *const ctx, int mi_row,
                      int mi_col, BLOCK_SIZE bsize, RUN_TYPE dry_run);

void av1_update_inter_mode_stats(FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                 PREDICTION_MODE mode, int16_t mode_context
#if CONFIG_WARPMV
                                 ,
                                 const AV1_COMMON *const cm,
                                 const MACROBLOCKD *xd,
                                 const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize
#endif  // CONFIG_WARPMV
);

void av1_sum_intra_stats(const AV1_COMMON *const cm, FRAME_COUNTS *counts,
                         MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi);

void av1_restore_context(const AV1_COMMON *cm, MACROBLOCK *x,
                         const RD_SEARCH_MACROBLOCK_CONTEXT *ctx, int mi_row,
                         int mi_col, BLOCK_SIZE bsize, const int num_planes);

void av1_save_context(const MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *ctx,
                      int mi_row, int mi_col, BLOCK_SIZE bsize,
                      const int num_planes);

void av1_set_fixed_partitioning(AV1_COMP *cpi, const TileInfo *const tile,
                                MB_MODE_INFO **mib, int mi_row, int mi_col,
                                BLOCK_SIZE bsize);
int av1_is_leaf_split_partition(AV1_COMMON *cm, MACROBLOCKD *const xd,
                                int mi_row, int mi_col, BLOCK_SIZE bsize);

void av1_reset_simple_motion_tree_partition(SIMPLE_MOTION_DATA_TREE *sms_tree,
                                            BLOCK_SIZE bsize);

void av1_update_picked_ref_frames_mask(MACROBLOCK *const x, int ref_type,
                                       BLOCK_SIZE bsize, int mib_size,
                                       int mi_row, int mi_col);

void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr);
#if CONFIG_FLEX_MVRES
void av1_reset_mbmi(const CommonModeInfoParams *const mi_params,
                    BLOCK_SIZE sb_size, int mi_row, int mi_col);
#else
void av1_reset_mbmi(CommonModeInfoParams *const mi_params, BLOCK_SIZE sb_size,
                    int mi_row, int mi_col);
#endif

void av1_backup_sb_state(SB_FIRST_PASS_STATS *sb_fp_stats, const AV1_COMP *cpi,
                         ThreadData *td, const TileDataEnc *tile_data,
                         int mi_row, int mi_col);

void av1_restore_sb_state(const SB_FIRST_PASS_STATS *sb_fp_stats, AV1_COMP *cpi,
                          ThreadData *td, TileDataEnc *tile_data, int mi_row,
                          int mi_col);

void av1_set_cost_upd_freq(AV1_COMP *cpi, ThreadData *td,
                           const TileInfo *const tile_info, const int mi_row,
                           const int mi_col);

#ifndef NDEBUG
static AOM_INLINE int is_bsize_square(BLOCK_SIZE bsize) {
  return block_size_wide[bsize] == block_size_high[bsize];
}
#endif  // NDEBUG

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENCODEFRAME_UTILS_H_
