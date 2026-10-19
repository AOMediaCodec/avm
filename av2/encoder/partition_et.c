/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av2/encoder/partition_et.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/reconinter.h"
#include "av2/encoder/partition_et_weights.h"
#include "av2/encoder/partition_search.h"
#include "av2/encoder/partition_strategy.h"
#include "av2/encoder/rd.h"

float av2_partition_et_predict(const float *features) {
  float z[PART_ET_NUM_FEATURES];
  for (int i = 0; i < PART_ET_NUM_FEATURES; ++i) {
    z[i] = (features[i] - part_et_mean[i]) * part_et_inv_std[i];
  }
  float out = part_et_b2;
  for (int h = 0; h < PART_ET_HIDDEN; ++h) {
    float a = part_et_b1[h];
    const float *w = part_et_w1[h];
    for (int i = 0; i < PART_ET_NUM_FEATURES; ++i) a += w[i] * z[i];
    if (a > 0.0f) out += part_et_w2[h] * a;
  }
  return 1.0f / (1.0f + expf(-out));
}

static INLINE float partition_et_log_ratio(int64_t a, int64_t b) {
  if (a == INT64_MAX || b <= 0 || a <= 0) return 0.0f;
  return logf((float)a / (float)b);
}

// Whether any extended partition type is still allowed and not pruned.
static INLINE bool is_any_ext_part_allowed(const PartitionSearchState *pss) {
  for (PARTITION_TYPE t = PARTITION_HORZ_3; t <= PARTITION_VERT_4B; ++t) {
    if (pss->partition_allowed[t] && !pss->prune_partition[t]) return true;
  }
  return false;
}

// |log((1 + a) / (1 + b))|.
static INLINE float partition_et_log_asym(unsigned int a, unsigned int b) {
  return fabsf(logf((1.0f + (float)a) / (1.0f + (float)b)));
}

// log2 of the mi size of the neighbouring block recorded in a partition
// context byte (see partition_context_lookup); 6 when nothing is recorded.
static INLINE int partition_et_ctx_log2_size(PARTITION_CONTEXT c) {
  const unsigned int u = (unsigned int)(uint8_t)c & 63u;
  if (!u) return 6;
  int n = 0;
  while (!((u >> n) & 1u)) ++n;
  return n;
}

// Features of the learned post-NONE early-termination gate. Everything is
// already available at this point of the search (RD costs, the NONE mode
// decision, partition context, simple-motion-search statistics when the
// speed preset computed them); the only extra work is the source
// variance of the block when the NONE search did not compute it.
// The order must match the trained weights in partition_et_weights.h.
static void compute_partition_et_features(
    const AV2_COMP *cpi, MACROBLOCK *x, const PC_TREE *pc_tree,
    PartitionSearchState *pss, const RD_STATS *best_rdc, int64_t part_none_rd,
    int64_t part_split_rd, unsigned int *pb_source_variance,
    SB_MULTI_PASS_MODE multi_pass_mode, const SIMPLE_MOTION_DATA_TREE *sms_tree,
    PARTITION_TYPE parent_partition, float *f) {
  const AV2_COMMON *const cm = &cpi->common;
  const PartitionBlkParams *blk = &pss->part_blk_params;
  const BLOCK_SIZE bsize = blk->bsize;
  const float area = (float)(block_size_wide[bsize] * block_size_high[bsize]);
  const int qidx8 =
      AVMMAX(0, x->qindex - MAXQ_OFFSET * ((int)cm->seq_params.bit_depth - 8));
  const float qn = qidx8 / 255.0f;
  const GF_GROUP *gf = &cpi->gf_group;
  int k = 0;
  f[k++] = (float)mi_size_wide_log2[bsize];
  f[k++] = (float)mi_size_high_log2[bsize];
  f[k++] = qn;
  f[k++] = qn * qn;
  f[k++] = (float)AVMMIN(6, gf->layer_depth[gf->index]);
  f[k++] = (float)frame_is_kf_gf_arf(cpi);
  f[k++] = multi_pass_mode == SB_DRY_PASS;
  f[k++] = multi_pass_mode == SB_WET_PASS;
  f[k++] = part_none_rd < INT64_MAX;
  f[k++] = part_split_rd < INT64_MAX;
  f[k++] = pc_tree->partitioning == PARTITION_SPLIT;
  f[k++] = log1pf((float)best_rdc->rdcost / area);
  f[k++] = partition_et_log_ratio(part_none_rd, best_rdc->rdcost);
  f[k++] = partition_et_log_ratio(part_split_rd, best_rdc->rdcost);
  f[k++] = log1pf((float)AVMMAX(0, best_rdc->rate) / area);
  f[k++] = log1pf((float)AVMMAX(0, best_rdc->dist) / area);
  // Shape of the SPLIT subtree (128x128 / 256x256 only).
  float none_children = -1.0f;
  int max_depth = -1, min_depth = -1;
  if (part_split_rd < INT64_MAX) {
    int n = 0, n_none = 0;
    max_depth = 0;
    min_depth = INT_MAX;
    for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
      const PC_TREE *c = pc_tree->split[pc_tree->region_type][i];
      if (!c) continue;
      ++n;
      n_none += c->partitioning == PARTITION_NONE;
      const int d = get_partition_depth(c, 0);
      max_depth = AVMMAX(max_depth, d);
      min_depth = AVMMIN(min_depth, d);
    }
    if (n) {
      none_children = (float)n_none / n;
    } else {
      max_depth = min_depth = -1;
    }
  }
  f[k++] = none_children;
  f[k++] = (float)max_depth;
  f[k++] = (float)min_depth;
  // Cached for prune_rect_partitions().
  if (*pb_source_variance == UINT_MAX) {
    av2_setup_src_planes(x, cpi->source, blk->mi_row, blk->mi_col,
                         av2_num_planes(cm), NULL);
    *pb_source_variance = av2_high_get_sby_perpixel_variance(
        cpi, &x->plane[AVM_PLANE_Y].src, bsize, x->e_mbd.bd);
  }
  f[k++] = log1pf((float)*pb_source_variance);
  f[k++] = is_rect_part_allowed(cpi, pss, PARTITION_HORZ);
  f[k++] = is_rect_part_allowed(cpi, pss, PARTITION_VERT);
  f[k++] = log2f((float)AVMMIN(cm->width, cm->height));
  f[k++] = is_any_ext_part_allowed(pss);
  f[k++] = frame_is_intra_only(cm);
  // Encoder configuration and the rate/distortion balance of the best cost.
  // Below, missing / not applicable values are 0 with an accompanying
  // validity flag where the absence itself is informative.
  const MACROBLOCKD *const xd = &x->e_mbd;
  f[k++] = (float)cpi->speed;
  f[k++] = log2f((float)AVMMAX(1, x->rdmult));
  // Rate- vs distortion-dominated best cost so far.
  f[k++] = partition_et_log_ratio(
      RDCOST(x->rdmult, AVMMAX(0, best_rdc->rate), 0), best_rdc->dist);
  // What the PARTITION_NONE search found for this block.
  const PICK_MODE_CONTEXT *none_ctx = pc_tree->none[pc_tree->region_type];
  const int none_ready = part_none_rd < INT64_MAX && none_ctx != NULL;
  if (none_ready) {
    const MB_MODE_INFO *mi = &none_ctx->mic;
    const int inter = is_inter_block(mi, xd->tree_type);
    f[k++] = 1.0f;
    f[k++] = (float)mi->skip_txfm[av2_get_sdp_idx(xd->tree_type)];
    f[k++] = (float)none_ctx->skippable;
    f[k++] = (float)inter;
    f[k++] = inter ? (float)has_second_ref(mi) : 0.0f;
    f[k++] = inter ? (float)have_newmv_in_inter_mode(mi->mode) : 0.0f;
    f[k++] = inter ? (float)(mi->motion_mode != SIMPLE_TRANSLATION) : 0.0f;
    f[k++] = inter ? log1pf((float)(abs(mi->mv[0].as_mv.row) +
                                    abs(mi->mv[0].as_mv.col)))
                   : 0.0f;
    f[k++] = log2f(area / (float)AVMMAX(1, tx_size_2d[mi->tx_size]));
  } else {
    for (int i = 0; i < 9; ++i) f[k++] = 0.0f;
  }
  // Size of the above / left neighbours relative to this block, taken from
  // the partition context (already maintained for partition signalling).
  {
    const int plane = xd->tree_type == CHROMA_PART;
    const PARTITION_CONTEXT above =
        xd->above_partition_context[plane][blk->mi_col];
    const PARTITION_CONTEXT left =
        xd->left_partition_context[plane][blk->mi_row & MAX_MIB_MASK];
    f[k++] =
        (float)(partition_et_ctx_log2_size(above) - mi_size_wide_log2[bsize]);
    f[k++] =
        (float)(partition_et_ctx_log2_size(left) - mi_size_high_log2[bsize]);
    // Signalling cost of splitting vs. not (bits), also context dependent.
    f[k++] = (float)(pss->partition_cost[PARTITION_HORZ] -
                     pss->partition_cost[PARTITION_NONE]) /
             (float)(1 << AV2_PROB_COST_SHIFT);
  }
  // Where in the parent this node sits.
  f[k++] = parent_partition == PARTITION_HORZ ||
           parent_partition == PARTITION_HORZ_3 ||
           parent_partition == PARTITION_HORZ_4A ||
           parent_partition == PARTITION_HORZ_4B;
  f[k++] = parent_partition == PARTITION_VERT ||
           parent_partition == PARTITION_VERT_3 ||
           parent_partition == PARTITION_VERT_4A ||
           parent_partition == PARTITION_VERT_4B;
  // Simple-motion-search results when the speed preset computed them.
  {
    const int sms_none = sms_tree && sms_tree->sms_none_valid;
    const int sms_rect = sms_tree && sms_tree->sms_rect_valid;
    const int sms_uni = sms_tree && sms_tree->sms_unified_valid;
    f[k++] = (float)sms_none;
    f[k++] = sms_none ? log1pf((float)sms_tree->sms_none_feat[0] / area) : 0.0f;
    f[k++] = sms_none ? log1pf((float)sms_tree->sms_none_feat[1] / area) : 0.0f;
    f[k++] = (float)sms_rect;
    if (sms_rect && sms_none) {
      const unsigned int *r = sms_tree->sms_rect_feat;
      const unsigned int none_sse = sms_tree->sms_none_feat[0];
      f[k++] = logf((1.0f + (float)r[0] + (float)r[2]) / (1.0f + none_sse));
      f[k++] = logf((1.0f + (float)r[4] + (float)r[6]) / (1.0f + none_sse));
      f[k++] = partition_et_log_asym(r[0], r[2]);
      f[k++] = partition_et_log_asym(r[4], r[6]);
    } else {
      for (int i = 0; i < 4; ++i) f[k++] = 0.0f;
    }
    f[k++] = (float)sms_uni;
    f[k++] = sms_uni ? sms_tree->sms_unified_probs[PARTITION_NONE] : 0.0f;
  }
  assert(k == PART_ET_NUM_FEATURES);
}

// Learned early termination after NONE (+ SPLIT at 128/256). Returns true
// when the rest of the node's search should be skipped. Only nodes where the
// best-so-far RD cost is valid and NONE has already been searched (or is not
// allowed) are eligible; nodes with a forced partition, the retry pass and
// chroma-only trees are left alone.
bool av2_partition_et_after_none_and_split(
    const AV2_COMP *cpi, MACROBLOCK *x, const PC_TREE *pc_tree,
    PartitionSearchState *pss, const RD_STATS *best_rdc, int64_t part_none_rd,
    int64_t part_split_rd, unsigned int *pb_source_variance,
    SB_MULTI_PASS_MODE multi_pass_mode, bool search_none_after_rect,
    const SIMPLE_MOTION_DATA_TREE *sms_tree, PARTITION_TYPE parent_partition) {
  const int thresh = cpi->sf.part_sf.ml_post_none_early_term_thresh;
  // Conditions that the model should not be called.
  if (thresh <= 0) return false;
  if (x->e_mbd.tree_type == CHROMA_PART) return false;
  if (pss->forced_partition != PARTITION_INVALID) return false;
  if (x->must_find_valid_partition) return false;
  if (!pss->found_best_partition) return false;
  if (best_rdc->rdcost <= 0 || best_rdc->rdcost >= INT64_MAX) return false;
  // The model should be called after partition none is searched.
  if (search_none_after_rect) return false;
  if (pss->terminate_partition_search) return false;
  // Nothing left to skip.
  if (!is_rect_part_allowed(cpi, pss, PARTITION_HORZ) &&
      !is_rect_part_allowed(cpi, pss, PARTITION_VERT) &&
      !is_any_ext_part_allowed(pss))
    return false;
  // On screen content (B2), large blocks that are mostly flat background with
  // a thin text line or UI edge have low average variance and RD cost, which
  // tricks the natural-video linear model into premature early termination.
  // Only allow early termination on screen content when PARTITION_NONE is a
  // zero-residual skip, preserving the speedup on flat backgrounds while
  // splitting blocks with sharp residual edges.
  if (cpi->common.features.allow_screen_content_tools) {
    const PICK_MODE_CONTEXT *none_ctx = pc_tree->none[pc_tree->region_type];
    const int sdp_idx = av2_get_sdp_idx(x->e_mbd.tree_type);
    if (!none_ctx ||
        (!none_ctx->mic.skip_txfm[sdp_idx] && !none_ctx->skippable)) {
      return false;
    }
  }
  float features[PART_ET_NUM_FEATURES];
  compute_partition_et_features(cpi, x, pc_tree, pss, best_rdc, part_none_rd,
                                part_split_rd, pb_source_variance,
                                multi_pass_mode, sms_tree, parent_partition,
                                features);
  return av2_partition_et_predict(features) * 1000.0f < (float)thresh;
}
