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

#ifndef AOM_AV1_ENCODER_PARTITION_STRATEGY_H_
#define AOM_AV1_ENCODER_PARTITION_STRATEGY_H_

#include "av1/encoder/block.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encoder.h"

#define FEATURE_SIZE_SMS_SPLIT_FAST 6
#define FEATURE_SIZE_SMS_SPLIT 17
#define FEATURE_SIZE_SMS_PRUNE_PART 25
#define FEATURE_SIZE_SMS_TERM_NONE 28
#define FEATURE_SIZE_FP_SMS_TERM_NONE 20
#define FEATURE_SIZE_MAX_MIN_PART_PRED 13
#define MAX_NUM_CLASSES_MAX_MIN_PART_PRED 4

#define FEATURE_SMS_NONE_FLAG 1
#define FEATURE_SMS_SPLIT_FLAG (1 << 1)
#define FEATURE_SMS_RECT_FLAG (1 << 2)

#define FEATURE_SMS_PRUNE_PART_FLAG \
  (FEATURE_SMS_NONE_FLAG | FEATURE_SMS_SPLIT_FLAG | FEATURE_SMS_RECT_FLAG)
#define FEATURE_SMS_SPLIT_MODEL_FLAG \
  (FEATURE_SMS_NONE_FLAG | FEATURE_SMS_SPLIT_FLAG)

// Number of sub-partitions in rectangular partition types.
#define SUB_PARTITIONS_RECT 2

// Number of sub-partitions in split partition type.
#define SUB_PARTITIONS_SPLIT 4

// Structure to keep win flags for HORZ and VERT partition evaluations.
typedef struct {
  int rect_part_win[NUM_RECT_PARTS];
} RD_RECT_PART_WIN_INFO;

void av1_intra_mode_cnn_partition(const AV1_COMMON *const cm, MACROBLOCK *x,
                                  BLOCK_SIZE bsize, int label_idx,
                                  int *partition_none_allowed,
                                  int *partition_horz_allowed,
                                  int *partition_vert_allowed,
                                  int *do_rectangular_split,
                                  int *do_square_split);

// Performs a simple_motion_search with a single reference frame and extract
// the variance of residues. Then use the features to determine whether we want
// to go straight to splitting without trying PARTITION_NONE
void av1_simple_motion_search_based_split(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, int *partition_none_allowed,
    int *partition_horz_allowed, int *partition_vert_allowed,
    int *do_rectangular_split, int *do_square_split);

// Performs a simple_motion_search with two reference frames and extract
// the variance of residues. Then use the features to determine whether we want
// to prune some partitions.
void av1_simple_motion_search_prune_rect(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, int partition_horz_allowed,
    int partition_vert_allowed, bool *prune_horz, bool *prune_vert);

// Early terminates PARTITION_NONE using simple_motion_search features and the
// rate, distortion, and rdcost of PARTITION_NONE. This is only called when:
//  - The frame is a show frame
//  - The frame is not intra only
//  - The current bsize is > BLOCK_8X8
//  - blk_row + blk_height/2 < total_rows and blk_col + blk_width/2 < total_cols
void av1_simple_motion_search_early_term_none(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, const RD_STATS *none_rdc,
    int *early_terminate);

// Get the features for selecting the max and min partition size. Currently this
// performs simple_motion_search on 16X16 subblocks of the current superblock,
// and then extract the statistics of sse and motion vectors as features.
void av1_get_max_min_partition_features(AV1_COMP *const cpi, MACROBLOCK *x,
                                        int mi_row, int mi_col,
                                        float *features);

// Predict the maximum BLOCK_SIZE to be used to encoder the current superblock.
BLOCK_SIZE av1_predict_max_partition(const AV1_COMP *const cpi,
                                     const MACROBLOCK *const x,
                                     const float *features);

// ML-based partition search breakout after PARTITION_NONE.
int av1_ml_predict_breakout(const AV1_COMP *const cpi, BLOCK_SIZE bsize,
                            const MACROBLOCK *const x,
                            const RD_STATS *const rd_stats,
                            unsigned int pb_source_variance);

// The first round of partition pruning determined before any partition
// has been tested. The decisions will be updated and passed back
// to the partition search function.
void av1_prune_partitions_before_search(
    AV1_COMP *const cpi, MACROBLOCK *const x, int mi_row, int mi_col,
    BLOCK_SIZE bsize, SIMPLE_MOTION_DATA_TREE *const sms_tree,
    int *partition_none_allowed, int *partition_horz_allowed,
    int *partition_vert_allowed, int *do_rectangular_split,
    int *do_square_split, bool *prune_horz, bool *prune_vert,
    const PC_TREE *pc_tree);

// Prune out partitions that lead to coding block sizes outside the min and max
// bsizes set by the encoder. Max and min square partition levels are defined as
// the partition nodes that the recursive function rd_pick_partition() can
// reach.
struct PartitionSearchState;

void av1_prune_partitions_by_max_min_bsize(
    SuperBlockEnc *sb_enc, BLOCK_SIZE bsize, int is_not_edge_block,
    struct PartitionSearchState *partition_search_state, int *do_square_split);

SimpleMotionData *av1_get_sms_data_entry(SimpleMotionDataBufs *sms_bufs,
                                         int mi_row, int mi_col,
                                         BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                                         int8_t region_type);
SimpleMotionData *av1_get_sms_data(AV1_COMP *const cpi,
                                   const TileInfo *const tile, MACROBLOCK *x,
                                   int mi_row, int mi_col, BLOCK_SIZE bsize
#if CONFIG_ML_PART_SPLIT

                                   ,
                                   ThreadData *td, bool need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
                                   ,
                                   int8_t region_type);
static AOM_INLINE void av1_add_mode_search_context_to_cache(
    SimpleMotionData *sms_data, PICK_MODE_CONTEXT *ctx) {
  if (!sms_data->mode_cache[0] ||
      sms_data->mode_cache[0]->rd_stats.rdcost > ctx->rd_stats.rdcost) {
    sms_data->mode_cache[0] = ctx;
  }
}

static INLINE void av1_set_best_mode_cache(MACROBLOCK *x,
                                           PICK_MODE_CONTEXT *mode_cache[1]) {
  if (mode_cache[0] && mode_cache[0]->rd_stats.rate != INT_MAX) {
    x->inter_mode_cache = &mode_cache[0]->mic;
  } else {
    x->inter_mode_cache = NULL;
  }
}

void av1_cache_best_partition(SimpleMotionDataBufs *sms_bufs, int mi_row,
                              int mi_col, BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                              PARTITION_TYPE partition, int8_t region_type);

// A simplified version of set_offsets meant to be used for
// simple_motion_search.
static INLINE void set_offsets_for_motion_search(const AV1_COMP *const cpi,
                                                 MACROBLOCK *const x,
                                                 int mi_row, int mi_col,
                                                 BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];

  set_mode_info_offsets(&cpi->common.mi_params, &cpi->mbmi_ext_info, x, xd,
                        mi_row, mi_col, mi_width, mi_height);

  // Set up destination pointers.
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, NULL);

  // Set up limit values for MV components.
  // Mv beyond the range do not produce new/different prediction block.
  av1_set_mv_limits(mi_params, &x->mv_limits, mi_row, mi_col, mi_height,
                    mi_width, cpi->oxcf.border_in_pixels);

  set_plane_n4(xd, mi_width, mi_height, num_planes, NULL);

  xd->mi_row = mi_row;
  xd->mi_col = mi_col;

  // Set up distance of MB to edge of frame in 1/8th pel units.
  xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_row * MI_SIZE);
  xd->mb_to_bottom_edge =
      GET_MV_SUBPEL((mi_params->mi_rows - mi_height - mi_row) * MI_SIZE);
  xd->mb_to_left_edge = -GET_MV_SUBPEL(mi_col * MI_SIZE);
  xd->mb_to_right_edge =
      GET_MV_SUBPEL((mi_params->mi_cols - mi_width - mi_col) * MI_SIZE);

  // Set up source buffers.
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, NULL);
}

static INLINE void init_simple_motion_search_mvs(
    SIMPLE_MOTION_DATA_TREE *sms_tree) {
  av1_zero(sms_tree->start_mvs);
  av1_zero(sms_tree->sms_none_feat);
  av1_zero(sms_tree->sms_rect_feat);
  av1_zero(sms_tree->sms_none_valid);
  av1_zero(sms_tree->sms_rect_valid);

  if (sms_tree->block_size >= BLOCK_8X8) {
    init_simple_motion_search_mvs(sms_tree->split[0]);
    init_simple_motion_search_mvs(sms_tree->split[1]);
    init_simple_motion_search_mvs(sms_tree->split[2]);
    init_simple_motion_search_mvs(sms_tree->split[3]);
  }
}

PARTITION_TYPE av1_get_prev_partition(MACROBLOCK *x, int mi_row, int mi_col,
                                      BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                                      int8_t region_type);

static INLINE void av1_init_sms_data_bufs(SimpleMotionDataBufs *data_bufs) {
  memset(data_bufs, 0, sizeof(*data_bufs));
}

struct PartitionSearchState;
void av1_gather_erp_rect_features(
    float *ml_features, AV1_COMP *cpi, MACROBLOCK *x, const TileInfo *tile_info,
    const PC_TREE *pc_tree,
    const struct PartitionSearchState *part_search_state, int64_t part_none_rd,
    const int (*mi_pos_rect)[SUB_PARTITIONS_RECT][2]);

static INLINE int is_full_sb(const CommonModeInfoParams *const mi_params,
                             int mi_row, int mi_col, BLOCK_SIZE sb_size) {
  const int sb_mi_wide = mi_size_wide[sb_size];
  const int sb_mi_high = mi_size_high[sb_size];

  return (mi_row + sb_mi_high) <= mi_params->mi_rows &&
         (mi_col + sb_mi_wide) <= mi_params->mi_cols;
}

// Do not use this criteria for screen content videos.
// Since screen content videos could often find good predictors and the largest
// block size is likely to be used.
static INLINE int use_auto_max_partition(const AV1_COMP *const cpi,
                                         BLOCK_SIZE sb_size, int mi_row,
                                         int mi_col) {
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));
  const AV1_COMMON *const cm = &cpi->common;
  return !frame_is_intra_only(cm) && !cpi->is_screen_content_type &&
         cpi->sf.part_sf.auto_max_partition_based_on_simple_motion !=
             NOT_IN_USE &&
         sb_size == BLOCK_128X128 &&
         is_full_sb(&cm->mi_params, mi_row, mi_col, sb_size) &&
         cpi->gf_group.update_type[cpi->gf_group.index] != OVERLAY_UPDATE &&
         cpi->gf_group.update_type[cpi->gf_group.index] !=
             KFFLT_OVERLAY_UPDATE &&
         cpi->gf_group.update_type[cpi->gf_group.index] != INTNL_OVERLAY_UPDATE;
}
#endif  // AOM_AV1_ENCODER_PARTITION_STRATEGY_H_
