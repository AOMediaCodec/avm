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

#include <limits.h>

#include "aom_mem/aom_mem.h"

#include "av1/common/pred_common.h"
#include "av1/common/tile_common.h"

#include "av1/common/cost.h"
#include "av1/encoder/segmentation.h"

void av1_enable_segmentation(struct segmentation *seg) {
  seg->enabled = 1;
  seg->update_map = 1;
  seg->update_data = 1;
  seg->temporal_update = 0;
}

void av1_disable_segmentation(struct segmentation *seg) {
  seg->enabled = 0;
  seg->update_map = 0;
  seg->update_data = 0;
  seg->temporal_update = 0;
}

void av1_disable_segfeature(struct segmentation *seg, int segment_id,
                            SEG_LVL_FEATURES feature_id) {
  seg->feature_mask[segment_id] &= ~(1 << feature_id);
}

void av1_clear_segdata(struct segmentation *seg, int segment_id,
                       SEG_LVL_FEATURES feature_id) {
  seg->feature_data[segment_id][feature_id] = 0;
}

static void count_segs(const AV1_COMMON *cm, MACROBLOCKD *xd,
                       const TileInfo *tile, MB_MODE_INFO **mi,
                       unsigned *no_pred_segcounts,
                       unsigned (*temporal_predictor_count)[2],
                       unsigned *t_unpred_seg_counts, int bw, int bh,
                       int mi_row, int mi_col) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

  xd->mi = mi;
  assert(xd->mi && xd->mi[0]);
  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
                 mi_params->mi_cols, &xd->mi[0]->chroma_ref_info);

  // Count the number of hits on each segment with no prediction
  const int segment_id = xd->mi[0]->segment_id;
  no_pred_segcounts[segment_id]++;

  // Temporal prediction not allowed on key frames
  if (cm->current_frame.frame_type != KEY_FRAME &&
      xd->mi[0]->region_type != INTRA_REGION) {
    const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];
    // Test to see if the segment id matches the predicted value.
    const int pred_segment_id =
        cm->last_frame_seg_map
            ? get_segment_id(mi_params, cm->last_frame_seg_map, bsize, mi_row,
                             mi_col)
            : 0;
    const int pred_flag = pred_segment_id == segment_id;
    const int pred_context = av1_get_pred_context_seg_id(xd);

    // Store the prediction status for this mb and update counts
    // as appropriate
    xd->mi[0]->seg_id_predicted = pred_flag;
    temporal_predictor_count[pred_context][pred_flag]++;

    // Update the "unpredicted" segment count
    if (!pred_flag) t_unpred_seg_counts[segment_id]++;
  }
}

static void count_segs_sb(const AV1_COMMON *cm, MACROBLOCKD *xd,
                          const TileInfo *tile, MB_MODE_INFO **mi,
                          unsigned *no_pred_segcounts,
                          unsigned (*temporal_predictor_count)[2],
                          unsigned *t_unpred_seg_counts, int mi_row, int mi_col,
#if !CONFIG_EXT_RECUR_PARTITIONS
                          BLOCK_SIZE bsize,
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
                          const PARTITION_TREE *ptree) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mis = mi_params->mi_stride;
#if CONFIG_EXT_RECUR_PARTITIONS
  BLOCK_SIZE bsize = ptree->bsize;
  const int bw = mi_size_wide[bsize], bh = mi_size_high[bsize];
  const int hbw = bw / 2, hbh = bh / 2;
  const int qbw = bw / 4, qbh = bh / 4;
  const int ebw = bw / 8, ebh = bh / 8;
#else
  const int bs = mi_size_wide[bsize], hbs = bs / 2;
  const int qbs = bs / 4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

#define CSEGS(cs_bw, cs_bh, cs_rowoff, cs_coloff)                              \
  count_segs(cm, xd, tile, mi + mis * (cs_rowoff) + (cs_coloff),               \
             no_pred_segcounts, temporal_predictor_count, t_unpred_seg_counts, \
             (cs_bw), (cs_bh), mi_row + (cs_rowoff), mi_col + (cs_coloff));
#if CONFIG_EXT_RECUR_PARTITIONS
#define CSEGS_RECURSIVE(cs_rowoff, cs_coloff, subtree)              \
  count_segs_sb(cm, xd, tile, mi + mis * (cs_rowoff) + (cs_coloff), \
                no_pred_segcounts, temporal_predictor_count,        \
                t_unpred_seg_counts, mi_row + (cs_rowoff),          \
                mi_col + (cs_coloff), subtree);

  int tree_idx = 0;
  const PARTITION_TYPE partition = ptree->partition;
#else
  PARTITION_TYPE partition;
  if (bsize == BLOCK_8X8)
    partition = PARTITION_NONE;
  else
    partition =
        get_partition(cm, xd->tree_type == CHROMA_PART, mi_row, mi_col, bsize);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  switch (partition) {
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_NONE: CSEGS(bw, bh, 0, 0); break;
    case PARTITION_HORZ:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(hbh, 0, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_VERT:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(0, hbw, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_HORZ_3:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(qbh, 0, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(qbh, hbw, ptree->sub_tree[tree_idx++]);
      if (mi_row + 3 * qbh < mi_params->mi_rows)
        CSEGS_RECURSIVE(3 * qbh, 0, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_VERT_3:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(0, qbw, ptree->sub_tree[tree_idx++]);
      CSEGS_RECURSIVE(hbh, qbw, ptree->sub_tree[tree_idx++]);
      if (mi_col + 3 * qbw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, 3 * qbw, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_HORZ_4A:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(ebh, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + 3 * ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(3 * ebh, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + 7 * ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(7 * ebh, 0, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_HORZ_4B:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(ebh, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + 5 * ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(5 * ebh, 0, ptree->sub_tree[tree_idx++]);
      if (mi_row + 7 * ebh < mi_params->mi_rows)
        CSEGS_RECURSIVE(7 * ebh, 0, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_VERT_4A:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      if (mi_col + ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, ebw, ptree->sub_tree[tree_idx++]);
      if (mi_col + 3 * ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, 3 * ebw, ptree->sub_tree[tree_idx++]);
      if (mi_col + 7 * ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, 7 * ebw, ptree->sub_tree[tree_idx++]);
      break;
    case PARTITION_VERT_4B:
      CSEGS_RECURSIVE(0, 0, ptree->sub_tree[tree_idx++]);
      if (mi_col + ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, ebw, ptree->sub_tree[tree_idx++]);
      if (mi_col + 5 * ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, 5 * ebw, ptree->sub_tree[tree_idx++]);
      if (mi_col + 7 * ebw < mi_params->mi_cols)
        CSEGS_RECURSIVE(0, 7 * ebw, ptree->sub_tree[tree_idx++]);
      break;
#else   // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_NONE: CSEGS(bs, bs, 0, 0); break;
    case PARTITION_HORZ:
      CSEGS(bs, hbs, 0, 0);
      CSEGS(bs, hbs, hbs, 0);
      break;
    case PARTITION_VERT:
      CSEGS(hbs, bs, 0, 0);
      CSEGS(hbs, bs, 0, hbs);
      break;
    case PARTITION_HORZ_A:
      CSEGS(hbs, hbs, 0, 0);
      CSEGS(hbs, hbs, 0, hbs);
      CSEGS(bs, hbs, hbs, 0);
      break;
    case PARTITION_HORZ_B:
      CSEGS(bs, hbs, 0, 0);
      CSEGS(hbs, hbs, hbs, 0);
      CSEGS(hbs, hbs, hbs, hbs);
      break;
    case PARTITION_VERT_A:
      CSEGS(hbs, hbs, 0, 0);
      CSEGS(hbs, hbs, hbs, 0);
      CSEGS(hbs, bs, 0, hbs);
      break;
    case PARTITION_VERT_B:
      CSEGS(hbs, bs, 0, 0);
      CSEGS(hbs, hbs, 0, hbs);
      CSEGS(hbs, hbs, hbs, hbs);
      break;
    case PARTITION_HORZ_4:
      CSEGS(bs, qbs, 0, 0);
      CSEGS(bs, qbs, qbs, 0);
      CSEGS(bs, qbs, 2 * qbs, 0);
      if (mi_row + 3 * qbs < mi_params->mi_rows) CSEGS(bs, qbs, 3 * qbs, 0);
      break;
    case PARTITION_VERT_4:
      CSEGS(qbs, bs, 0, 0);
      CSEGS(qbs, bs, 0, qbs);
      CSEGS(qbs, bs, 0, 2 * qbs);
      if (mi_col + 3 * qbs < mi_params->mi_cols) CSEGS(qbs, bs, 0, 3 * qbs);
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT: {
#if !CONFIG_EXT_RECUR_PARTITIONS
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
      assert(subsize < BLOCK_SIZES_ALL);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

      for (int n = 0; n < 4; n++) {
#if CONFIG_EXT_RECUR_PARTITIONS
        const int mi_dc = hbw * (n & 1);
        const int mi_dr = hbh * (n >> 1);
        count_segs_sb(cm, xd, tile, &mi[mi_dr * mis + mi_dc], no_pred_segcounts,
                      temporal_predictor_count, t_unpred_seg_counts,
                      mi_row + mi_dr, mi_col + mi_dc, ptree->sub_tree[n]);
#else
        const int mi_dc = hbs * (n & 1);
        const int mi_dr = hbs * (n >> 1);
        count_segs_sb(cm, xd, tile, &mi[mi_dr * mis + mi_dc], no_pred_segcounts,
                      temporal_predictor_count, t_unpred_seg_counts,
                      mi_row + mi_dr, mi_col + mi_dc, subsize,
                      ptree->sub_tree[n]);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      }
    } break;
    default: assert(0);
  }

#undef CSEGS
}

void av1_choose_segmap_coding_method(AV1_COMMON *cm, MACROBLOCKD *xd) {
  struct segmentation *seg = &cm->seg;
  struct segmentation_probs *segp = &cm->fc->seg;
  int no_pred_cost;
  int t_pred_cost = INT_MAX;
  int tile_col, tile_row, mi_row, mi_col;
  unsigned temporal_predictor_count[SEG_TEMPORAL_PRED_CTXS][2] = { { 0 } };
  unsigned no_pred_segcounts[MAX_SEGMENTS] = { 0 };
  unsigned t_unpred_seg_counts[MAX_SEGMENTS] = { 0 };
  (void)xd;
  int scale_up = cm->prev_frame && (cm->width > cm->prev_frame->width ||
                                    cm->height > cm->prev_frame->height);
  // First of all generate stats regarding how well the last segment map
  // predicts this one
  if (!scale_up) {
    for (tile_row = 0; tile_row < cm->tiles.rows; tile_row++) {
      TileInfo tile_info;
      av1_tile_set_row(&tile_info, cm, tile_row);
      for (tile_col = 0; tile_col < cm->tiles.cols; tile_col++) {
        MB_MODE_INFO **mi_ptr;
        av1_tile_set_col(&tile_info, cm, tile_col);
        mi_ptr = cm->mi_params.mi_grid_base +
                 tile_info.mi_row_start * cm->mi_params.mi_stride +
                 tile_info.mi_col_start;
        for (mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
             mi_row += cm->mib_size,
            mi_ptr += cm->mib_size * cm->mi_params.mi_stride) {
          MB_MODE_INFO **mi = mi_ptr;
          for (mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
               mi_col += cm->mib_size, mi += cm->mib_size) {
            const SB_INFO *sbi = av1_get_sb_info(cm, mi_row, mi_col);
            const PARTITION_TREE *ptree = sbi->ptree_root[AOM_PLANE_Y];
            count_segs_sb(cm, xd, &tile_info, mi, no_pred_segcounts,
                          temporal_predictor_count, t_unpred_seg_counts, mi_row,
                          mi_col,
#if !CONFIG_EXT_RECUR_PARTITIONS
                          cm->sb_size,
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
                          ptree);
          }
        }
      }
    }
  }

  int seg_id_cost[MAX_SEGMENTS];
  av1_cost_tokens_from_cdf(seg_id_cost, segp->tree_cdf, NULL);
  no_pred_cost = 0;
  for (int i = 0; i < MAX_SEGMENTS; ++i)
    no_pred_cost += no_pred_segcounts[i] * seg_id_cost[i];

  // Frames without past dependency cannot use temporal prediction
  if (cm->features.primary_ref_frame != PRIMARY_REF_NONE) {
    int pred_flag_cost[SEG_TEMPORAL_PRED_CTXS][2];
    for (int i = 0; i < SEG_TEMPORAL_PRED_CTXS; ++i)
      av1_cost_tokens_from_cdf(pred_flag_cost[i], segp->pred_cdf[i], NULL);
    t_pred_cost = 0;
    // Cost for signaling the prediction flag.
    for (int i = 0; i < SEG_TEMPORAL_PRED_CTXS; ++i) {
      for (int j = 0; j < 2; ++j)
        t_pred_cost += temporal_predictor_count[i][j] * pred_flag_cost[i][j];
    }
    // Cost for signaling the unpredicted segment id.
    for (int i = 0; i < MAX_SEGMENTS; ++i)
      t_pred_cost += t_unpred_seg_counts[i] * seg_id_cost[i];
  }

  // Now choose which coding method to use.
  if (t_pred_cost < no_pred_cost) {
    assert(!cm->features.error_resilient_mode);
    seg->temporal_update = 1;
  } else {
    seg->temporal_update = 0;
  }
}

void av1_reset_segment_features(AV1_COMMON *cm) {
  struct segmentation *seg = &cm->seg;

  // Set up default state for MB feature flags
  seg->enabled = 0;
  seg->update_map = 0;
  seg->update_data = 0;
  av1_clearall_segfeatures(seg);
}
