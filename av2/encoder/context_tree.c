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

#include "av2/encoder/context_tree.h"
#include "av2/common/av2_common_int.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/rd.h"

static const BLOCK_SIZE square[MAX_SB_SIZE_LOG2 - 1] = {
  BLOCK_4X4,   BLOCK_8X8,     BLOCK_16X16,   BLOCK_32X32,
  BLOCK_64X64, BLOCK_128X128, BLOCK_256X256,
};

/*!\brief Retrieves the array of child PC_TREE nodes corresponding to a specific
 * partition type and region. */
PC_TREE *const *get_child_pc_trees(const PC_TREE *pc_tree,
                                   PARTITION_TYPE partition, REGION_TYPE region,
                                   int *num_sub_parts) {
  if (partition >= ALL_PARTITION_TYPES) {
    assert(0 && "Unexpected partition type");
    return NULL;
  }

  PC_TREE *const *child_nodes = NULL;
  switch (partition) {
    case PARTITION_NONE: break;
    case PARTITION_HORZ: child_nodes = pc_tree->horizontal[region]; break;
    case PARTITION_VERT: child_nodes = pc_tree->vertical[region]; break;
    case PARTITION_HORZ_3: child_nodes = pc_tree->horizontal3[region]; break;
    case PARTITION_VERT_3: child_nodes = pc_tree->vertical3[region]; break;
    case PARTITION_HORZ_4A: child_nodes = pc_tree->horizontal4a[region]; break;
    case PARTITION_HORZ_4B: child_nodes = pc_tree->horizontal4b[region]; break;
    case PARTITION_VERT_4A: child_nodes = pc_tree->vertical4a[region]; break;
    case PARTITION_VERT_4B: child_nodes = pc_tree->vertical4b[region]; break;
    case PARTITION_SPLIT: child_nodes = pc_tree->split[region]; break;
  }
  // No child blocks for partition none.
  *num_sub_parts =
      partition == PARTITION_NONE ? 0 : get_subblock_count(partition);

  return child_nodes;
}

void av2_copy_tree_context(PICK_MODE_CONTEXT *dst_ctx,
                           PICK_MODE_CONTEXT *src_ctx, int num_planes) {
  dst_ctx->mic = src_ctx->mic;
  dst_ctx->mbmi_ext_best = src_ctx->mbmi_ext_best;

  assert(dst_ctx->num_4x4_blk == src_ctx->num_4x4_blk);
  assert(dst_ctx->num_4x4_blk_chroma == src_ctx->num_4x4_blk_chroma);
  dst_ctx->skippable = src_ctx->skippable;

  for (int i = 0; i < num_planes; ++i) {
    const int num_blk_plane =
        (i == 0) ? src_ctx->num_4x4_blk : src_ctx->num_4x4_blk_chroma;
    memcpy(dst_ctx->blk_skip[i], src_ctx->blk_skip[i],
           sizeof(*src_ctx->blk_skip[i]) * num_blk_plane);
  }
  av2_copy_array(dst_ctx->tx_type_map, src_ctx->tx_type_map,
                 src_ctx->num_4x4_blk);
  av2_copy_array(dst_ctx->cctx_type_map, src_ctx->cctx_type_map,
                 src_ctx->num_4x4_blk_chroma);

  dst_ctx->hybrid_pred_diff = src_ctx->hybrid_pred_diff;
  dst_ctx->comp_pred_diff = src_ctx->comp_pred_diff;
  dst_ctx->single_pred_diff = src_ctx->single_pred_diff;

  dst_ctx->rd_stats = src_ctx->rd_stats;
  dst_ctx->rd_mode_is_ready = src_ctx->rd_mode_is_ready;
  const int num_pix = src_ctx->num_4x4_blk * 16;
  if (num_pix <= MAX_PALETTE_SQUARE) {
    for (int i = 0; i < 2; ++i) {
      const int num_blk =
          (i == 0) ? src_ctx->num_4x4_blk : src_ctx->num_4x4_blk_chroma;
      const int color_map_size = num_blk * 16;
      memcpy(dst_ctx->color_index_map[i], src_ctx->color_index_map[i],
             sizeof(src_ctx->color_index_map[i][0]) * color_map_size);
    }
  }
}

void av2_setup_shared_coeff_buffer(AV2_COMMON *cm,
                                   PC_TREE_SHARED_BUFFERS *shared_bufs) {
  for (int i = 0; i < 3; i++) {
    const int max_num_pix = MAX_SB_SIZE * MAX_SB_SIZE;
    CHECK_MEM_ERROR(cm, shared_bufs->coeff_buf[i],
                    avm_memalign(32, max_num_pix * sizeof(tran_low_t)));
    CHECK_MEM_ERROR(cm, shared_bufs->qcoeff_buf[i],
                    avm_memalign(32, max_num_pix * sizeof(tran_low_t)));
    CHECK_MEM_ERROR(cm, shared_bufs->dqcoeff_buf[i],
                    avm_memalign(32, max_num_pix * sizeof(tran_low_t)));
  }
}

void av2_free_shared_coeff_buffer(PC_TREE_SHARED_BUFFERS *shared_bufs) {
  for (int i = 0; i < 3; i++) {
    avm_free(shared_bufs->coeff_buf[i]);
    avm_free(shared_bufs->qcoeff_buf[i]);
    avm_free(shared_bufs->dqcoeff_buf[i]);
    shared_bufs->coeff_buf[i] = NULL;
    shared_bufs->qcoeff_buf[i] = NULL;
    shared_bufs->dqcoeff_buf[i] = NULL;
  }
}

// Get base block size for pick mode context allocation.
static INLINE int get_num_pix_bsize_base(
    BLOCK_SIZE bsize, TREE_TYPE tree_type,
    const CHROMA_REF_INFO *const chroma_ref_info, int num_planes) {
  if (num_planes == 1 || tree_type == LUMA_PART) {
    // No chroma pixels required in this case. But we return value 1 to avoid
    // special cases for malloc(), memcpy() etc.
    return 1;
  }
  BLOCK_SIZE bsize_base = BLOCK_INVALID;
  if (tree_type == SHARED_PART) {
    bsize_base = chroma_ref_info->bsize_base;
  } else {
    assert(tree_type == CHROMA_PART);
    bsize_base = bsize;
  }
  assert(bsize_base != BLOCK_INVALID);
  return block_size_wide[bsize_base] * block_size_high[bsize_base];
}

PICK_MODE_CONTEXT *av2_alloc_pmc(const AV2_COMMON *cm, TREE_TYPE tree_type,
                                 int mi_row, int mi_col, BLOCK_SIZE bsize,
                                 PC_TREE *parent,
                                 PARTITION_TYPE parent_partition, int index,
                                 int subsampling_x, int subsampling_y,
                                 PC_TREE_SHARED_BUFFERS *shared_bufs) {
  PICK_MODE_CONTEXT *ctx = NULL;
  struct avm_internal_error_info error;

  AVM_CHECK_MEM_ERROR(&error, ctx, avm_calloc(1, sizeof(*ctx)));
  ctx->rd_mode_is_ready = 0;
  set_chroma_ref_info(tree_type, mi_row, mi_col, index, bsize,
                      &ctx->chroma_ref_info,
                      parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->block_size : BLOCK_INVALID,
                      parent_partition, subsampling_x, subsampling_y);
  ctx->mic.chroma_ref_info = ctx->chroma_ref_info;

  const int num_planes = av2_num_planes(cm);
  const int num_pix = block_size_wide[bsize] * block_size_high[bsize];
  const int num_blk = num_pix / 16;

  // We need to get actual chroma block size due to possible sub-8x8 luma block
  // sizes.
  const int num_pix_bize_base = get_num_pix_bsize_base(
      bsize, tree_type, &ctx->chroma_ref_info, num_planes);
  ctx->num_4x4_blk = num_blk;
  ctx->num_4x4_blk_chroma = num_pix_bize_base / 16;

  AVM_CHECK_MEM_ERROR(&error, ctx->tx_type_map,
                      avm_calloc(num_blk, sizeof(*ctx->tx_type_map)));
  AVM_CHECK_MEM_ERROR(
      &error, ctx->cctx_type_map,
      avm_calloc(ctx->num_4x4_blk_chroma, sizeof(*ctx->cctx_type_map)));

  for (int i = 0; i < num_planes; ++i) {
    ctx->coeff[i] = shared_bufs->coeff_buf[i];
    ctx->qcoeff[i] = shared_bufs->qcoeff_buf[i];
    ctx->dqcoeff[i] = shared_bufs->dqcoeff_buf[i];
    const int num_blk_plane =
        (i == 0) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    AVM_CHECK_MEM_ERROR(&error, ctx->blk_skip[i],
                        avm_calloc(num_blk_plane, sizeof(*ctx->blk_skip[i])));
    AVM_CHECK_MEM_ERROR(
        &error, ctx->eobs[i],
        avm_memalign(32, num_blk_plane * sizeof(*ctx->eobs[i])));
    AVM_CHECK_MEM_ERROR(
        &error, ctx->bobs[i],
        avm_memalign(32, num_blk_plane * sizeof(*ctx->bobs[i])));
    AVM_CHECK_MEM_ERROR(
        &error, ctx->txb_entropy_ctx[i],
        avm_memalign(32, num_blk_plane * sizeof(*ctx->txb_entropy_ctx[i])));
  }

  if (num_pix <= MAX_PALETTE_SQUARE) {
    for (int i = 0; i < 2; ++i) {
      const int color_map_size = (i == 0) ? num_pix : num_pix_bize_base;
      AVM_CHECK_MEM_ERROR(
          &error, ctx->color_index_map[i],
          avm_memalign(32, color_map_size * sizeof(*ctx->color_index_map[i])));
    }
  }
  av2_invalid_rd_stats(&ctx->rd_stats);
  return ctx;
}

// Resets the state of a PICK_MODE_CONTEXT node so it can be reused across
// blocks/superblocks without re-allocation.
void av2_reset_pmc(PICK_MODE_CONTEXT *ctx) {
  if (ctx == NULL) return;
  ctx->rd_mode_is_ready = 0;
  av2_invalid_rd_stats(&ctx->rd_stats);
  if (ctx->tx_type_map) av2_zero_array(ctx->tx_type_map, ctx->num_4x4_blk);
  if (ctx->cctx_type_map)
    av2_zero_array(ctx->cctx_type_map, ctx->num_4x4_blk_chroma);
  ctx->skippable = 0;
  ctx->hybrid_pred_diff = 0;
  ctx->comp_pred_diff = 0;
  ctx->single_pred_diff = 0;
}

void av2_free_pmc(PICK_MODE_CONTEXT *ctx, int num_planes) {
  if (ctx == NULL) return;

  for (int i = 0; i < MAX_MB_PLANE; ++i) {
    avm_free(ctx->blk_skip[i]);
    ctx->blk_skip[i] = NULL;
  }
  avm_free(ctx->tx_type_map);
  ctx->tx_type_map = NULL;
  avm_free(ctx->cctx_type_map);
  ctx->cctx_type_map = NULL;
  for (int i = 0; i < num_planes; ++i) {
    ctx->coeff[i] = NULL;
    ctx->qcoeff[i] = NULL;
    ctx->dqcoeff[i] = NULL;
    avm_free(ctx->eobs[i]);
    ctx->eobs[i] = NULL;
    avm_free(ctx->bobs[i]);
    ctx->bobs[i] = NULL;
    avm_free(ctx->txb_entropy_ctx[i]);
    ctx->txb_entropy_ctx[i] = NULL;
  }

  for (int i = 0; i < 2; ++i) {
    avm_free(ctx->color_index_map[i]);
    ctx->color_index_map[i] = NULL;
  }

  avm_free(ctx);
}

PC_TREE *av2_alloc_pc_tree_node(TREE_TYPE tree_type, int mi_row, int mi_col,
                                BLOCK_SIZE sb_size, BLOCK_SIZE bsize,
                                PC_TREE *parent,
                                PARTITION_TYPE parent_partition, int index,
                                int is_last, int subsampling_x,
                                int subsampling_y) {
  PC_TREE *pc_tree = NULL;
  struct avm_internal_error_info error;

  AVM_CHECK_MEM_ERROR(&error, pc_tree, avm_calloc(1, sizeof(*pc_tree)));

  pc_tree->mi_row = mi_row;
  pc_tree->mi_col = mi_col;
  pc_tree->parent = parent;
  pc_tree->index = index;
  pc_tree->partitioning = PARTITION_NONE;
  if (parent) {
    if (parent->block_size == sb_size) {
      if (parent_partition == PARTITION_VERT)
        pc_tree->sb_root_partition_info = SB_VERT_PARTITION;
      else if (parent_partition == PARTITION_HORZ ||
               parent_partition == PARTITION_SPLIT)
        pc_tree->sb_root_partition_info = SB_HORZ_OR_QUAD_PARTITION;
      else
        pc_tree->sb_root_partition_info = INVALID_INTRABC_SB_PARTITION;
    } else {
      pc_tree->sb_root_partition_info = pc_tree->parent->sb_root_partition_info;
    }
    pc_tree->region_type = parent->region_type;
    if (parent->extended_sdp_allowed_flag)
      pc_tree->extended_sdp_allowed_flag =
          is_extended_sdp_allowed(1, parent->block_size, parent_partition);
    else
      pc_tree->extended_sdp_allowed_flag = 0;
  } else {
    pc_tree->extended_sdp_allowed_flag = 1;
    pc_tree->sb_root_partition_info = INVALID_INTRABC_SB_PARTITION;
  }
  pc_tree->block_size = bsize;
  pc_tree->is_last_subblock = is_last;
  av2_invalid_rd_stats(&pc_tree->rd_cost);
  av2_invalid_rd_stats(&pc_tree->none_rd);
  pc_tree->skippable = false;
  set_chroma_ref_info(tree_type, mi_row, mi_col, index, bsize,
                      &pc_tree->chroma_ref_info,
                      parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->block_size : BLOCK_INVALID,
                      parent_partition, subsampling_x, subsampling_y);
  for (REGION_TYPE cur_region_type = INTRA_REGION;
       cur_region_type < REGION_TYPES; ++cur_region_type) {
    pc_tree->none[cur_region_type] = NULL;
    for (PARTITION_TYPE partition_type = PARTITION_HORZ;
         partition_type < ALL_PARTITION_TYPES; ++partition_type) {
      int num_sub_parts = 0;
      PC_TREE **child_nodes = (PC_TREE **)get_child_pc_trees(
          pc_tree, partition_type, cur_region_type, &num_sub_parts);
      for (int sub_idx = 0; sub_idx < num_sub_parts; ++sub_idx) {
        child_nodes[sub_idx] = NULL;
      }
    }
  }
  pc_tree->none_chroma = NULL;
  return pc_tree;
}

#define FREE_PMC_NODE(CTX)         \
  do {                             \
    av2_free_pmc(CTX, num_planes); \
    CTX = NULL;                    \
  } while (0)

void av2_free_pc_tree_recursive(PC_TREE *pc_tree, int num_planes, int keep_best,
                                int keep_none) {
  if (pc_tree == NULL) return;

  const PARTITION_TYPE partition = pc_tree->partitioning;

  PC_TREE *parent = pc_tree->parent;
  if (!keep_none &&
      (!keep_best || (pc_tree->region_type != INTRA_REGION ||
                      (parent && parent->region_type == INTRA_REGION))))
    FREE_PMC_NODE(pc_tree->none_chroma);

  for (int cur_region_type = INTRA_REGION; cur_region_type < REGION_TYPES;
       ++cur_region_type) {
    if (!keep_none && (!keep_best || (partition != PARTITION_NONE)))
      FREE_PMC_NODE(pc_tree->none[cur_region_type]);

    for (PARTITION_TYPE partition_type = PARTITION_HORZ;
         partition_type < ALL_PARTITION_TYPES; ++partition_type) {
      if (!keep_best || (partition != partition_type) ||
          (cur_region_type != pc_tree->region_type)) {
        int num_sub_parts = 0;
        PC_TREE **child_nodes = (PC_TREE **)get_child_pc_trees(
            pc_tree, partition_type, cur_region_type, &num_sub_parts);
        for (int sub_idx = 0; sub_idx < num_sub_parts; ++sub_idx) {
          if (child_nodes[sub_idx] != NULL) {
            av2_free_pc_tree_recursive(child_nodes[sub_idx], num_planes, 0, 0);
            child_nodes[sub_idx] = NULL;
          }
        }
      }
    }
  }
  if (!keep_best && !keep_none) avm_free(pc_tree);
}

void av2_copy_pc_tree_recursive(MACROBLOCKD *xd, const AV2_COMMON *cm,
                                PC_TREE *dst, PC_TREE *src, int ss_x, int ss_y,
                                PC_TREE_SHARED_BUFFERS *shared_bufs,
                                TREE_TYPE tree_type, int num_planes) {
  // Copy the best partition type. For basic information like bsize and index,
  // we assume they have been set properly when initializing the dst PC_TREE
  dst->partitioning = src->partitioning;
  dst->region_type = src->region_type;
  dst->extended_sdp_allowed_flag = src->extended_sdp_allowed_flag;
  REGION_TYPE cur_region_type = src->region_type;
  dst->is_cfl_allowed_for_this_chroma =
      src->parent->is_cfl_allowed_for_this_chroma;
  dst->rd_cost = src->rd_cost;
  dst->none_rd = src->none_rd;
  dst->skippable = src->skippable;

  const BLOCK_SIZE bsize = dst->block_size;
  const int mi_row = src->mi_row;
  const int mi_col = src->mi_col;

  PC_TREE *src_parent = src->parent;
  if ((src->region_type == INTRA_REGION &&
       (src_parent && src_parent->region_type == MIXED_INTER_INTRA_REGION))) {
    if (dst->none_chroma) av2_free_pmc(dst->none_chroma, num_planes);
    dst->none_chroma = NULL;
    if (src->none_chroma) {
      dst->none_chroma =
          av2_alloc_pmc(cm, CHROMA_PART, mi_row, mi_col, bsize, dst,
                        PARTITION_NONE, 0, ss_x, ss_y, shared_bufs);
      av2_copy_tree_context(dst->none_chroma, src->none_chroma, num_planes);
    }
  }

  if (src->partitioning == PARTITION_NONE) {
    if (dst->none[cur_region_type])
      av2_free_pmc(dst->none[cur_region_type], num_planes);
    dst->none[cur_region_type] = NULL;
    if (src->none[cur_region_type]) {
      dst->none[cur_region_type] = av2_alloc_pmc(
          cm,
          !frame_is_intra_only(cm) && cur_region_type == INTRA_REGION
              ? LUMA_PART
              : tree_type,
          mi_row, mi_col, bsize, dst, PARTITION_NONE, 0, ss_x, ss_y,
          shared_bufs);
      av2_copy_tree_context(dst->none[cur_region_type],
                            src->none[cur_region_type], num_planes);
      xd->mi_row = mi_row;
      xd->mi_col = mi_col;
      if (is_inter_block(&src->none[cur_region_type]->mic, xd->tree_type)) {
        av2_update_warp_param_bank(cm, xd, 0, &dst->none[cur_region_type]->mic);
        if (cm->seq_params.enable_refmvbank) {
          av2_update_ref_mv_bank(cm, xd, 1, &dst->none[cur_region_type]->mic);
        }
      } else {
        decide_rmb_unit_update_count(cm, xd, &dst->none[cur_region_type]->mic);
      }
    }
  } else {
    if (is_partition_valid(bsize, src->partitioning)) {
      int num_sub_parts_dst = 0;
      PC_TREE **dst_child_nodes = (PC_TREE **)get_child_pc_trees(
          dst, src->partitioning, cur_region_type, &num_sub_parts_dst);
      int num_sub_parts_src = 0;
      PC_TREE **src_child_nodes = (PC_TREE **)get_child_pc_trees(
          src, src->partitioning, cur_region_type, &num_sub_parts_src);
      assert(num_sub_parts_src == num_sub_parts_dst);
      int mi_rows[4], mi_cols[4];
      BLOCK_SIZE subblock_sizes[4];
      get_partition_subblock_layout(src->partitioning, bsize, mi_row, mi_col,
                                    subblock_sizes, mi_rows, mi_cols);
      for (int sub_idx = 0; sub_idx < num_sub_parts_src; ++sub_idx) {
        if (dst_child_nodes[sub_idx]) {
          av2_free_pc_tree_recursive(dst_child_nodes[sub_idx], num_planes, 0,
                                     0);
          dst_child_nodes[sub_idx] = NULL;
        }
        if (src_child_nodes[sub_idx]) {
          dst_child_nodes[sub_idx] = av2_alloc_pc_tree_node(
              tree_type, mi_rows[sub_idx], mi_cols[sub_idx], cm->sb_size,
              subblock_sizes[sub_idx], dst, src->partitioning, sub_idx,
              sub_idx == (num_sub_parts_dst - 1), ss_x, ss_y);
          av2_copy_pc_tree_recursive(xd, cm, dst_child_nodes[sub_idx],
                                     src_child_nodes[sub_idx], ss_x, ss_y,
                                     shared_bufs, tree_type, num_planes);
        }
      }
    }
  }
}

static AVM_INLINE int get_pc_tree_nodes(const BLOCK_SIZE sb_size,
                                        int stat_generation_stage) {
  const int is_sb_size_128 = sb_size == BLOCK_128X128;
  const int is_sb_size_256 = sb_size == BLOCK_256X256;
  const int tree_nodes_inc = is_sb_size_256   ? (1024 + 4 * 1024)
                             : is_sb_size_128 ? 1024
                                              : 0;
  const int tree_nodes =
      stat_generation_stage ? 1 : (tree_nodes_inc + 256 + 64 + 16 + 4 + 1);
  return tree_nodes;
}

void av2_setup_sms_tree(AV2_COMP *const cpi, ThreadData *td) {
  AV2_COMMON *const cm = &cpi->common;
  const int stat_generation_stage = is_stat_generation_stage(cpi);
  const int is_sb_size_128 = cm->sb_size == BLOCK_128X128;
  const int is_sb_size_256 = cm->sb_size == BLOCK_256X256;
  const int tree_nodes = get_pc_tree_nodes(cm->sb_size, stat_generation_stage);
  int sms_tree_index = 0;
  SIMPLE_MOTION_DATA_TREE *this_sms;
  int square_index = 1;
  int nodes;

  avm_free(td->sms_tree);
  CHECK_MEM_ERROR(cm, td->sms_tree,
                  avm_calloc(tree_nodes, sizeof(*td->sms_tree)));
  this_sms = &td->sms_tree[0];

  if (!stat_generation_stage) {
    const int leaf_factor = is_sb_size_256 ? 16 : is_sb_size_128 ? 4 : 1;

    const int leaf_nodes = 256 * leaf_factor;

    // Sets up all the leaf nodes in the tree.
    for (sms_tree_index = 0; sms_tree_index < leaf_nodes; ++sms_tree_index) {
      SIMPLE_MOTION_DATA_TREE *const tree = &td->sms_tree[sms_tree_index];
      tree->block_size = square[0];
    }

    // Each node has 4 leaf nodes, fill each block_size level of the tree
    // from leafs to the root.
    for (nodes = leaf_nodes >> 2; nodes > 0; nodes >>= 2) {
      for (int i = 0; i < nodes; ++i) {
        SIMPLE_MOTION_DATA_TREE *const tree = &td->sms_tree[sms_tree_index];
        tree->block_size = square[square_index];
        for (int j = 0; j < 4; j++) tree->split[j] = this_sms++;
        ++sms_tree_index;
      }
      ++square_index;
    }
  } else {
    // Allocation for firstpass/LAP stage
    // TODO(Mufaddal): refactor square_index to use a common block_size macro
    // from firstpass.c
    SIMPLE_MOTION_DATA_TREE *const tree = &td->sms_tree[sms_tree_index];
    square_index = 2;
    tree->block_size = square[square_index];
  }

  // Set up the root node for the largest superblock size
  td->sms_root = &td->sms_tree[tree_nodes - 1];
}

void av2_free_sms_tree(ThreadData *td) {
  if (td->sms_tree != NULL) {
    avm_free(td->sms_tree);
    td->sms_tree = NULL;
  }
}

void av2_setup_sms_bufs(AV2_COMMON *cm, ThreadData *td) {
  CHECK_MEM_ERROR(cm, td->sms_bufs, avm_malloc(sizeof(*td->sms_bufs)));
}

void av2_free_sms_bufs(ThreadData *td) {
  if (td->sms_bufs != NULL) {
    avm_free(td->sms_bufs);
    td->sms_bufs = NULL;
  }
}

PC_TREE *counterpart_from_different_partition(PC_TREE *pc_tree,
                                              const PC_TREE *target);

static PC_TREE *look_for_counterpart_helper(PC_TREE *cur,
                                            const PC_TREE *target) {
  if (cur == NULL || cur == target) return NULL;

  const BLOCK_SIZE current_bsize = cur->block_size;
  const BLOCK_SIZE target_bsize = target->block_size;
  // Note: To find the counterpart block, we don't actually need to check the
  // whole chroma_ref_info -- checking bsize_base should suffice due to
  // constraints in the partitioning scheme. However, we are checking the whole
  // struct for now as we are still experimenting with new partition schemes.
  if (current_bsize == target_bsize &&
      memcmp(&cur->chroma_ref_info, &target->chroma_ref_info,
             sizeof(cur->chroma_ref_info)) == 0) {
    return cur;
  } else {
    if (mi_size_wide[current_bsize] >= mi_size_wide[target_bsize] &&
        mi_size_high[current_bsize] >= mi_size_high[target_bsize]) {
      return counterpart_from_different_partition(cur, target);
    } else {
      return NULL;
    }
  }
}

/*! \brief Searches for a partition tree node that does not change any context
 * and has the same position and bsize as the current target. */
PC_TREE *counterpart_from_different_partition(PC_TREE *pc_tree,
                                              const PC_TREE *target) {
  if (pc_tree == NULL || pc_tree == target) return NULL;

  PC_TREE *result;
  REGION_TYPE cur_region_type = pc_tree->region_type;

  for (PARTITION_TYPE partition_type = PARTITION_HORZ;
       partition_type < ALL_PARTITION_TYPES; ++partition_type) {
    int num_sub_parts = 0;
    PC_TREE **child_nodes = (PC_TREE **)get_child_pc_trees(
        pc_tree, partition_type, cur_region_type, &num_sub_parts);
    result = look_for_counterpart_helper(child_nodes[0], target);
    if (result) return result;
  }
  return NULL;
}

/*! \brief Searches for a partition tree node with the same context, position,
 * and bsize as the current node. */
PC_TREE *av2_look_for_counterpart_block(PC_TREE *pc_tree) {
  if (!pc_tree) return 0;

  // Find the highest possible common parent node
  PC_TREE *current = pc_tree;
  while (current->index == 0 && current->parent) {
    current = current->parent;
  }

  // Search from the highest common ancestor
  return counterpart_from_different_partition(current, pc_tree);
}
