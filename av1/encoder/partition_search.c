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

#include "aom/aom_codec.h"
#include "aom_ports/system_state.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/common_data.h"
#include "av1/common/enums.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/block.h"
#include "av1/encoder/context_tree.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/partition_search.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tokenize.h"
#include "av1/common/reconinter.h"
#if CONFIG_EXT_RECUR_PARTITIONS
#include "av1/encoder/erp_tflite.h"
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#include "aom_util/debug_util.h"

#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

#if CONFIG_NEW_TX_PARTITION
static void update_partition_cdfs_and_counts(MACROBLOCKD *xd, int blk_col,
                                             int blk_row, TX_SIZE max_tx_size,
                                             int allow_update_cdf,
                                             FRAME_COUNTS *counts) {
  (void)counts;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int txb_size_index =
      is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
#if !CONFIG_TX_PARTITION_CTX
  const int is_rect = is_rect_tx(max_tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
  const TX_PARTITION_TYPE partition = mbmi->tx_partition_type[txb_size_index];
  const int allow_horz = allow_tx_horz_split(max_tx_size);
  const int allow_vert = allow_tx_vert_split(max_tx_size);
#if CONFIG_IMPROVEIDTX_CTXS
  const int plane_type = xd->tree_type == CHROMA_PART;
  const int is_fsc = (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      plane_type == PLANE_TYPE_Y);
#endif  // CONFIG_IMPROVEIDTX_CTXS
#if CONFIG_TX_PARTITION_CTX
#if CONFIG_TX_PARTITION_TYPE_EXT
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
  const int txsize_group = size_to_tx_type_group_lookup[bsize];
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    do_partition = (partition != TX_PARTITION_NONE);
    if (allow_update_cdf) {
      aom_cdf_prob *do_partition_cdf =
#if CONFIG_IMPROVEIDTX_CTXS
          xd->tile_ctx->txfm_do_partition_cdf[is_fsc][is_inter][bsize_group];
#else
          xd->tile_ctx->txfm_do_partition_cdf[is_inter][bsize_group];
#endif  // CONFIG_IMPROVEIDTX_CTXS
      update_cdf(do_partition_cdf, do_partition, 2);
    }
#if CONFIG_ENTROPY_STATS
#if CONFIG_IMPROVEIDTX_CTXS
    ++counts->txfm_do_partition[is_fsc][is_inter][bsize_group][do_partition];
#else
    ++counts->txfm_do_partition[is_inter][bsize_group][do_partition];
#endif
#endif  // CONFIG_ENTROPY_STATS
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
      assert(txsize_group > 0);
      const TX_PARTITION_TYPE split4_partition =
          get_split4_partition(partition);
      if (allow_update_cdf) {
        aom_cdf_prob *partition_type_cdf =
#if CONFIG_IMPROVEIDTX_CTXS
            xd->tile_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                      [txsize_group - 1];
#else
            xd->tile_ctx
                ->txfm_4way_partition_type_cdf[is_inter][txsize_group - 1];
#endif  // CONFIG_IMPROVEIDTX_CTXS
        update_cdf(partition_type_cdf, split4_partition - 1,
                   TX_PARTITION_TYPE_NUM);
      }
#if CONFIG_ENTROPY_STATS
#if CONFIG_IMPROVEIDTX_CTXS
      ++counts->txfm_4way_partition_type[is_fsc][is_inter][txsize_group - 1]
                                        [split4_partition - 1];
#else
      ++counts->txfm_4way_partition_type[is_inter][txsize_group - 1]
                                        [split4_partition - 1];
#endif  // CONFIG_IMPROVEIDTX_CTXS
#endif  // CONFIG_ENTROPY_STATS
    } else if (allow_horz || allow_vert) {
      int has_first_split = 0;
      if (partition == TX_PARTITION_VERT_M || partition == TX_PARTITION_HORZ_M)
        has_first_split = 1;

      if (allow_update_cdf && txsize_group) {
        aom_cdf_prob *partition_type_cdf =
#if CONFIG_IMPROVEIDTX_CTXS
            xd->tile_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                      [txsize_group - 1];
#else
            xd->tile_ctx
                ->txfm_4way_partition_type_cdf[is_inter][txsize_group - 1];
#endif  // CONFIG_IMPROVEIDTX_CTXS
        update_cdf(partition_type_cdf, has_first_split, TX_PARTITION_TYPE_NUM);
      }
#if CONFIG_ENTROPY_STATS
      if (txsize_group) {
#if CONFIG_IMPROVEIDTX_CTXS
        ++counts->txfm_4way_partition_type[is_fsc][is_inter][txsize_group - 1]
                                          [has_first_split];
#else
        ++counts->txfm_4way_partition_type[is_inter][txsize_group - 1]
                                          [has_first_split];
#endif  // CONFIG_IMPROVEIDTX_CTXS
      }
#endif  // CONFIG_ENTROPY_STATS
    }
  }
#else
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    do_partition = (partition != TX_PARTITION_NONE);
    if (allow_update_cdf) {
      aom_cdf_prob *do_partition_cdf =
#if CONFIG_IMPROVEIDTX_CTXS
          xd->tile_ctx->txfm_do_partition_cdf[is_fsc][is_inter][bsize_group];
#else
          xd->tile_ctx->txfm_do_partition_cdf[is_inter][bsize_group];
#endif  // CONFIG_IMPROVEIDTX_CTXS
      update_cdf(do_partition_cdf, do_partition, 2);
    }
#if CONFIG_ENTROPY_STATS
#if CONFIG_IMPROVEIDTX_CTXS
    ++counts->txfm_do_partition[is_fsc][is_inter][bsize_group][do_partition];
#else
    ++counts->txfm_do_partition[is_inter][bsize_group][do_partition];
#endif  // CONFIG_IMPROVEIDTX_CTXS
#endif  // CONFIG_ENTROPY_STATS
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
      assert(bsize_group > 0);
      const TX_PARTITION_TYPE split4_partition =
          get_split4_partition(partition);
      if (allow_update_cdf) {
        aom_cdf_prob *partition_type_cdf =
#if CONFIG_IMPROVEIDTX_CTXS
            xd->tile_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                      [bsize_group - 1];
#else
            xd->tile_ctx
                ->txfm_4way_partition_type_cdf[is_inter][bsize_group - 1];
#endif  // CONFIG_IMPROVEIDTX_CTXS
        update_cdf(partition_type_cdf, split4_partition - 1, 3);
      }
#if CONFIG_ENTROPY_STATS
#if CONFIG_IMPROVEIDTX_CTXS
      ++counts->txfm_4way_partition_type[is_fsc][is_inter][bsize_group - 1]
                                        [split4_partition - 1];
#else
      ++counts->txfm_4way_partition_type[is_inter][bsize_group - 1]
                                        [split4_partition - 1];
#endif  // CONFIG_IMPROVEIDTX_CTXS
#endif  // CONFIG_ENTROPY_STATS
    }
  }
#endif  // CONFIG_TX_PARTITION_TYPE_EXT
#else
  if (allow_horz && allow_vert) {
    const TX_PARTITION_TYPE split4_partition = get_split4_partition(partition);
    const int split4_ctx =
        is_inter ? txfm_partition_split4_inter_context(
                       xd->above_txfm_context + blk_col,
                       xd->left_txfm_context + blk_row, bsize, max_tx_size)
                 : get_tx_size_context(xd);
    aom_cdf_prob *split4_cdf =
        is_inter
            ? xd->tile_ctx->inter_4way_txfm_partition_cdf[is_rect][split4_ctx]
            : xd->tile_ctx->intra_4way_txfm_partition_cdf[is_rect][split4_ctx];
    if (allow_update_cdf) {
      update_cdf(split4_cdf, split4_partition, 4);
    }
#if CONFIG_ENTROPY_STATS
    if (is_inter)
      ++counts
            ->inter_4way_txfm_partition[is_rect][split4_ctx][split4_partition];
    else
      ++counts
            ->intra_4way_txfm_partition[is_rect][split4_ctx][split4_partition];
#endif  // CONFIG_ENTROPY_STATS
  } else if (allow_horz || allow_vert) {
    const int has_first_split = partition != TX_PARTITION_NONE;
    if (allow_update_cdf) {
      aom_cdf_prob *split2_cdf =
          is_inter ? xd->tile_ctx->inter_2way_txfm_partition_cdf
                   : xd->tile_ctx->intra_2way_txfm_partition_cdf;
      update_cdf(split2_cdf, has_first_split, 2);
    }
#if CONFIG_ENTROPY_STATS
    if (is_inter)
      ++counts->inter_2way_txfm_partition[has_first_split];
    else
      ++counts->intra_2way_txfm_partition[has_first_split];
#endif  // CONFIG_ENTROPY_STATS

  } else {
    assert(!allow_horz && !allow_vert);
    assert(partition == PARTITION_NONE);
  }
#endif  // CONFIG_TX_PARTITION_CTX
}
#endif  // CONFIG_NEW_TX_PARTITION

static void update_txfm_count(MACROBLOCK *x, MACROBLOCKD *xd,
                              FRAME_COUNTS *counts, TX_SIZE tx_size, int depth,
                              int blk_row, int blk_col,
                              uint8_t allow_update_cdf) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  const int txb_size_index = av1_get_txb_size_index(bsize, blk_row, blk_col);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  assert(tx_size > TX_4X4);
#if CONFIG_NEW_TX_PARTITION
  (void)depth;
#if CONFIG_TX_PARTITION_TYPE_EXT
  int num_txfm_blocks =
      get_tx_partition_sizes(mbmi->tx_partition_type[txb_size_index], tx_size,
                             &mbmi->txb_pos, mbmi->sub_txs);
  TX_SIZE this_size = mbmi->sub_txs[num_txfm_blocks - 1];
#else
  TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
  get_tx_partition_sizes(mbmi->tx_partition_type[txb_size_index], tx_size,
                         sub_txs);
  // TODO(sarahparker) This assumes all of the tx sizes in the partition scheme
  // are the same size. This will need to be adjusted to deal with the case
  // where they can be different.
  TX_SIZE this_size = sub_txs[0];
  assert(mbmi->inter_tx_size[txb_size_index] == this_size);
#endif  // CONFIG_TX_PARTITION_TYPE_EXT
  if (mbmi->tx_partition_type[txb_size_index] != TX_PARTITION_NONE)
    ++x->txfm_search_info.txb_split_count;

  update_partition_cdfs_and_counts(xd, blk_col, blk_row, tx_size,
                                   allow_update_cdf, counts);
  mbmi->tx_size = this_size;
#if !CONFIG_TX_PARTITION_CTX
  txfm_partition_update(xd->above_txfm_context + blk_col,
                        xd->left_txfm_context + blk_row, this_size, tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  int ctx = txfm_partition_context(
      xd->above_txfm_context + blk_col, xd->left_txfm_context + blk_row,
      mbmi->sb_type[xd->tree_type == CHROMA_PART], tx_size);
  const TX_SIZE plane_tx_size = mbmi->inter_tx_size[txb_size_index];
  if (depth == MAX_VARTX_DEPTH) {
    // Don't add to counts in this case
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
    return;
  }

  if (tx_size == plane_tx_size) {
#if CONFIG_ENTROPY_STATS
    ++counts->txfm_partition[ctx][0];
#endif
    if (allow_update_cdf)
      update_cdf(xd->tile_ctx->txfm_partition_cdf[ctx], 0, 2);
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
  } else {
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];

#if CONFIG_ENTROPY_STATS
    ++counts->txfm_partition[ctx][1];
#endif
    if (allow_update_cdf)
      update_cdf(xd->tile_ctx->txfm_partition_cdf[ctx], 1, 2);
    ++x->txfm_search_info.txb_split_count;

    if (sub_txs == TX_4X4) {
      mbmi->inter_tx_size[txb_size_index] = TX_4X4;
      mbmi->tx_size = TX_4X4;
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, TX_4X4, tx_size);
      return;
    }

    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        int offsetr = row;
        int offsetc = col;

        update_txfm_count(x, xd, counts, sub_txs, depth + 1, blk_row + offsetr,
                          blk_col + offsetc, allow_update_cdf);
      }
    }
  }
#endif  // CONFIG_NEW_TX_PARTITION
}

static void tx_partition_count_update(
#if !CONFIG_TX_PARTITION_CTX
    const AV1_COMMON *const cm,
#endif  // !CONFIG_TX_PARTITION_CTX
    MACROBLOCK *x, BLOCK_SIZE plane_bsize, FRAME_COUNTS *td_counts,
    uint8_t allow_update_cdf) {
  MACROBLOCKD *xd = &x->e_mbd;
  const int mi_width = mi_size_wide[plane_bsize];
  const int mi_height = mi_size_high[plane_bsize];
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, 0);
  const int bh = tx_size_high_unit[max_tx_size];
  const int bw = tx_size_wide_unit[max_tx_size];

#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context =
      cm->above_contexts.txfm[xd->tile.tile_row] + xd->mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (xd->mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX

  for (int idy = 0; idy < mi_height; idy += bh) {
    for (int idx = 0; idx < mi_width; idx += bw) {
      update_txfm_count(x, xd, td_counts, max_tx_size, 0, idy, idx,
                        allow_update_cdf);
    }
  }
}

#if !CONFIG_TX_PARTITION_CTX
static void set_txfm_context(MACROBLOCKD *xd, TX_SIZE tx_size, int blk_row,
                             int blk_col) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  const int txb_size_index = av1_get_txb_size_index(bsize, blk_row, blk_col);
  const TX_SIZE plane_tx_size = mbmi->inter_tx_size[txb_size_index];

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  if (tx_size == plane_tx_size) {
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);

  } else {
#if CONFIG_NEW_TX_PARTITION
    TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
    const int index = av1_get_txb_size_index(bsize, blk_row, blk_col);
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, sub_txs);
    int cur_partition = 0;
    int bsw = 0, bsh = 0;
    for (int r = 0; r < tx_size_high_unit[tx_size]; r += bsh) {
      for (int c = 0; c < tx_size_wide_unit[tx_size]; c += bsw) {
        const TX_SIZE sub_tx = sub_txs[cur_partition];
        bsw = tx_size_wide_unit[sub_tx];
        bsh = tx_size_high_unit[sub_tx];
        const int offsetr = blk_row + r;
        const int offsetc = blk_col + c;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
        mbmi->tx_size = sub_tx;
        txfm_partition_update(xd->above_txfm_context + blk_col,
                              xd->left_txfm_context + blk_row, sub_tx, sub_tx);
        cur_partition++;
      }
    }
#else
    if (tx_size == TX_8X8) {
      mbmi->inter_tx_size[txb_size_index] = TX_4X4;
      mbmi->tx_size = TX_4X4;
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, TX_4X4, tx_size);
      return;
    }
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        const int offsetr = blk_row + row;
        const int offsetc = blk_col + col;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
        set_txfm_context(xd, sub_txs, offsetr, offsetc);
      }
    }
#endif  // CONFIG_NEW_TX_PARTITION
  }
}

static void tx_partition_set_contexts(const AV1_COMMON *const cm,
                                      MACROBLOCKD *xd, BLOCK_SIZE plane_bsize) {
  const int mi_width = mi_size_wide[plane_bsize];
  const int mi_height = mi_size_high[plane_bsize];
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, 0);
  const int bh = tx_size_high_unit[max_tx_size];
  const int bw = tx_size_wide_unit[max_tx_size];

  xd->above_txfm_context =
      cm->above_contexts.txfm[xd->tile.tile_row] + xd->mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (xd->mi_row & MAX_MIB_MASK);

  for (int idy = 0; idy < mi_height; idy += bh) {
    for (int idx = 0; idx < mi_width; idx += bw) {
      set_txfm_context(xd, max_tx_size, idy, idx);
    }
  }
}
#endif  // !CONFIG_TX_PARTITION_CTX

static void encode_superblock(const AV1_COMP *const cpi, TileDataEnc *tile_data,
                              ThreadData *td, TokenExtra **t, RUN_TYPE dry_run,
                              BLOCK_SIZE bsize, int plane_start, int plane_end,
                              int *rate) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO **mi_4x4 = xd->mi;
  MB_MODE_INFO *mbmi = mi_4x4[0];
  const int seg_skip =
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP);
  const int mis = cm->mi_params.mi_stride;
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  xd->cfl.use_dc_pred_cache = 0;
  xd->cfl.dc_pred_is_cached[0] = 0;
  xd->cfl.dc_pred_is_cached[1] = 0;
  // Initialize tx_mode and tx_size_search_method
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  set_tx_size_search_method(
      cm, &cpi->winner_mode_params, txfm_params,
      cpi->sf.winner_mode_sf.enable_winner_mode_for_tx_size_srch, 1
#if CONFIG_EXT_RECUR_PARTITIONS
      ,
      x, cpi->sf.tx_sf.use_largest_tx_size_for_small_bsize
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  );

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  if (!is_inter) {
    if (xd->tree_type != LUMA_PART) {
      xd->cfl.store_y = store_cfl_required(cm, xd);
    }
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;
    for (int plane = plane_start; plane < plane_end; ++plane) {
      if (plane == AOM_PLANE_Y || !is_cctx_allowed(cm, xd))
        av1_encode_intra_block_plane(cpi, x, bsize, plane, dry_run,
                                     cpi->optimize_seg_arr[mbmi->segment_id]);
      else if (plane == AOM_PLANE_U)
        av1_encode_intra_block_joint_uv(
            cpi, x, bsize, dry_run, cpi->optimize_seg_arr[mbmi->segment_id]);
    }

    // If there is at least one lossless segment, force the skip for intra
    // block to be 0, in order to avoid the segment_id to be changed by in
    // write_segment_id().
    if (!cpi->common.seg.segid_preskip && cpi->common.seg.update_map &&
        cpi->enc_seg.has_lossless_segment)
      mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;

    xd->cfl.store_y = 0;
    if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
      for (int plane = plane_start; plane < AOMMIN(2, plane_end); ++plane) {
        if (mbmi->palette_mode_info.palette_size[plane] > 0) {
          if (!dry_run) {
            av1_tokenize_color_map(x, plane, t, bsize, mbmi->tx_size,
                                   PALETTE_MAP, tile_data->allow_update_cdf,
                                   td->counts);
          } else if (dry_run == DRY_RUN_COSTCOEFFS) {
            rate +=
                av1_cost_color_map(x, plane, bsize, mbmi->tx_size, PALETTE_MAP);
          }
        }
      }
    }

    av1_update_intra_mb_txb_context(cpi, td, dry_run, bsize,
                                    tile_data->allow_update_cdf);
  } else {
    int ref;
    const int is_compound = has_second_ref(mbmi);

    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    for (ref = 0; ref < 1 + is_compound; ++ref) {
      const YV12_BUFFER_CONFIG *cfg =
          get_ref_frame_yv12_buf(cm, mbmi->ref_frame[ref]);
      assert(IMPLIES(!is_intrabc_block(mbmi, xd->tree_type), cfg));
      av1_setup_pre_planes(xd, ref, cfg, mi_row, mi_col,
                           xd->block_ref_scale_factors[ref], num_planes,
                           &mbmi->chroma_ref_info);
    }
    int start_plane = 0;
#if CONFIG_BAWP
    struct macroblockd_plane *p = xd->plane;
    const BUFFER_SET orig_dst = {
      { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
      { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
    };
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize,
#else
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
#endif
                                  start_plane, av1_num_planes(cm) - 1);
    if (mbmi->motion_mode == OBMC_CAUSAL) {
#if CONFIG_EXTENDED_WARP_PREDICTION
      assert(cm->features.enabled_motion_modes & (1 << OBMC_CAUSAL));
#else
      assert(cpi->oxcf.motion_mode_cfg.enable_obmc);
#endif
      av1_build_obmc_inter_predictors_sb(cm, xd);
    }

#if CONFIG_MISMATCH_DEBUG
    if (dry_run == OUTPUT_ENABLED) {
      for (int plane = plane_start; plane < plane_end; ++plane) {
        const struct macroblockd_plane *pd = &xd->plane[plane];
        int pixel_c, pixel_r;
        if (plane && !xd->is_chroma_ref) continue;
        if (plane) {
          mi_to_pixel_loc(&pixel_c, &pixel_r,
                          mbmi->chroma_ref_info.mi_col_chroma_base,
                          mbmi->chroma_ref_info.mi_row_chroma_base, 0, 0,
                          pd->subsampling_x, pd->subsampling_y);
        } else {
          mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, 0, 0,
                          pd->subsampling_x, pd->subsampling_y);
        }
        mismatch_record_block_pre(pd->dst.buf, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                                  cm->current_frame.display_order_hint,
#else
                                  cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                                  plane, pixel_c, pixel_r, pd->width,
                                  pd->height);
      }
    }
#else
    (void)num_planes;
#endif  // CONFIG_MISMATCH_DEBUG

    av1_encode_sb(cpi, x, bsize, dry_run, plane_start, plane_end);
    av1_tokenize_sb_vartx(cpi, td, dry_run, bsize, rate,
                          tile_data->allow_update_cdf, plane_start, plane_end);
  }

  if (!dry_run) {
    if (av1_allow_intrabc(cm, xd) && is_intrabc_block(mbmi, xd->tree_type))
      td->intrabc_used = 1;
    if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
        !xd->lossless[mbmi->segment_id] &&
        mbmi->sb_type[xd->tree_type == CHROMA_PART] > BLOCK_4X4 &&
        !(is_inter &&
          (mbmi->skip_txfm[xd->tree_type == CHROMA_PART] || seg_skip))) {
      if (is_inter) {
        tx_partition_count_update(
#if !CONFIG_TX_PARTITION_CTX
            cm,
#endif  // !CONFIG_TX_PARTITION_CTX
            x, bsize, td->counts, tile_data->allow_update_cdf);
      } else {
#if CONFIG_TX_PARTITION_TYPE_EXT
        if (mbmi->tx_partition_type[0] != TX_PARTITION_NONE &&
#else
        if (mbmi->tx_size != max_txsize_rect_lookup[bsize] &&
#endif  // CONFIG_TX_PARTITION_TYPE_EXT
            xd->tree_type != CHROMA_PART)
          ++x->txfm_search_info.txb_split_count;
        if (block_signals_txsize(bsize) && xd->tree_type != CHROMA_PART) {
#if CONFIG_NEW_TX_PARTITION
          const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
          update_partition_cdfs_and_counts(
              xd, 0, 0, max_tx_size, tile_data->allow_update_cdf, td->counts);
#else  // CONFIG_NEW_TX_PARTITION
          const int tx_size_ctx = get_tx_size_context(xd);
          const int32_t tx_size_cat = bsize_to_tx_size_cat(bsize);
          const int depth = tx_size_to_depth(mbmi->tx_size, bsize);
          const int max_depths = bsize_to_max_depth(bsize);
          if (tile_data->allow_update_cdf)
            update_cdf(xd->tile_ctx->tx_size_cdf[tx_size_cat][tx_size_ctx],
                       depth, max_depths + 1);
#if CONFIG_ENTROPY_STATS
          ++td->counts->intra_tx_size[tx_size_cat][tx_size_ctx][depth];
#endif
#endif  // CONFIG_NEW_TX_PARTITION
        }
      }
      if (xd->tree_type != CHROMA_PART)
        assert(
            IMPLIES(is_rect_tx(mbmi->tx_size), is_rect_tx_allowed(xd, mbmi)));
    } else {
#if CONFIG_TX_PARTITION_TYPE_EXT
      if (mbmi->tx_partition_type[0] != TX_PARTITION_NONE)
        ++x->txfm_search_info.txb_split_count;
#else
      int i, j;
      TX_SIZE intra_tx_size;
      // The new intra coding scheme requires no change of transform size
      if (is_inter) {
        if (xd->lossless[mbmi->segment_id]) {
          intra_tx_size = TX_4X4;
        } else {
          intra_tx_size =
              tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type);
        }
      } else {
        intra_tx_size = mbmi->tx_size;
      }
#if CONFIG_EXTENDED_SDP
      // Since transform partitioning is only allowed for luma component,
      // and tx_size variable represents the transform size of the luma
      // component in one coded block, so chroma block should not change the
      // tx_size.
      if (xd->tree_type != CHROMA_PART || frame_is_intra_only(cm)) {
#endif  // CONFIG_EXTENDED_SDP
        for (j = 0; j < mi_height; j++)
          for (i = 0; i < mi_width; i++)
            if (mi_col + i < cm->mi_params.mi_cols &&
                mi_row + j < cm->mi_params.mi_rows)
              mi_4x4[mis * j + i]->tx_size = intra_tx_size;
#if CONFIG_EXTENDED_SDP
      }
#endif  // CONFIG_EXTENDED_SDP

      if (intra_tx_size != max_txsize_rect_lookup[bsize])
        ++x->txfm_search_info.txb_split_count;
#endif  // CONFIG_TX_PARTITION_TYPE_EXT
    }
#if !CONFIG_MVP_IMPROVEMENT
#if CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
    if (cm->seq_params.enable_refmvbank && is_inter &&
        !is_intrabc_block(mbmi, xd->tree_type))
#else
    if (cm->seq_params.enable_refmvbank && is_inter)
#endif  // CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
      av1_update_ref_mv_bank(cm, xd, mbmi);
#endif  // !CONFIG_MVP_IMPROVEMENT

#if CONFIG_EXTENDED_WARP_PREDICTION && !WARP_CU_BANK
    if (is_inter) av1_update_warp_param_bank(cm, xd, mbmi);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION && !WARP_CU_BANK
  }
#if !CONFIG_TX_PARTITION_TYPE_EXT
  if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
      block_signals_txsize(mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
      is_inter &&
      !(mbmi->skip_txfm[xd->tree_type == CHROMA_PART] || seg_skip) &&
      !xd->lossless[mbmi->segment_id]) {
#if !CONFIG_TX_PARTITION_CTX
    if (dry_run) tx_partition_set_contexts(cm, xd, bsize);
#endif  // !CONFIG_TX_PARTITION_CTX
  } else {
    TX_SIZE tx_size = mbmi->tx_size;
    // The new intra coding scheme requires no change of transform size
    if (is_inter) {
      if (xd->lossless[mbmi->segment_id]) {
        tx_size = TX_4X4;
      } else {
        tx_size = tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type);
      }
    } else {
      tx_size = (bsize > BLOCK_4X4) ? tx_size : TX_4X4;
    }
    mbmi->tx_size = tx_size;
#if !CONFIG_TX_PARTITION_CTX
    set_txfm_ctxs(tx_size, xd->width, xd->height,
                  (mbmi->skip_txfm[xd->tree_type == CHROMA_PART] || seg_skip) &&
                      is_inter_block(mbmi, xd->tree_type),
                  xd);
#endif  // !CONFIG_TX_PARTITION_CTX
  }
#endif  //! CONFIG_TX_PARTITION_TYPE_EXT

  if (is_inter_block(mbmi, xd->tree_type) && !xd->is_chroma_ref &&
      is_cfl_allowed(xd)) {
#if CONFIG_IMPROVED_CFL
    cfl_store_block(xd, mbmi->sb_type[xd->tree_type == CHROMA_PART],
                    mbmi->tx_size, cm->seq_params.enable_cfl_ds_filter);
#else
    cfl_store_block(xd, mbmi->sb_type[xd->tree_type == CHROMA_PART],
                    mbmi->tx_size);
#endif  // CONFIG_IMPROVED_CFL
  }
  if (xd->tree_type == LUMA_PART) {
    const CommonModeInfoParams *const mi_params = &cm->mi_params;
    for (int y = 0; y < mi_height; y++) {
      for (int x_idx = 0; x_idx < mi_width; x_idx++) {
        if ((xd->mb_to_right_edge >> (3 + MI_SIZE_LOG2)) + mi_width > x_idx &&
            (xd->mb_to_bottom_edge >> (3 + MI_SIZE_LOG2)) + mi_height > y) {
          if (y == 0 && x_idx == 0) continue;
          const int mi_idx =
              get_alloc_mi_idx(mi_params, mi_row + y, mi_col + x_idx);
          xd->mi[x_idx + y * mis] = &mi_params->mi_alloc[mi_idx];
          xd->mi[x_idx + y * mis]->skip_txfm[PLANE_TYPE_Y] =
              xd->mi[0]->skip_txfm[PLANE_TYPE_Y];
        }
      }
    }
  }

  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}

void setup_block_rdmult(const AV1_COMP *const cpi, MACROBLOCK *const x,
                        int mi_row, int mi_col, BLOCK_SIZE bsize,
                        AQ_MODE aq_mode, MB_MODE_INFO *mbmi) {
  x->rdmult = cpi->rd.RDMULT;
  MACROBLOCKD *const xd = &x->e_mbd;
  if (aq_mode != NO_AQ && xd->tree_type == SHARED_PART) {
    assert(mbmi != NULL);
    if (aq_mode == VARIANCE_AQ) {
      if (cpi->vaq_refresh) {
        const int energy = bsize <= BLOCK_16X16
                               ? x->mb_energy
                               : av1_log_block_var(cpi, x, bsize);
        mbmi->segment_id = energy;
      }
      x->rdmult = set_segment_rdmult(cpi, x, mbmi->segment_id);
    } else if (aq_mode == COMPLEXITY_AQ) {
      x->rdmult = set_segment_rdmult(cpi, x, mbmi->segment_id);
    } else if (aq_mode == CYCLIC_REFRESH_AQ) {
      // If segment is boosted, use rdmult for that segment.
      if (cyclic_refresh_segment_id_boosted(mbmi->segment_id))
        x->rdmult = av1_cyclic_refresh_get_rdmult(cpi->cyclic_refresh);
    }
  }

  const AV1_COMMON *const cm = &cpi->common;
  if (cm->delta_q_info.delta_q_present_flag) {
    x->rdmult =
        av1_get_hier_tpl_rdmult(cpi, x, bsize, mi_row, mi_col, x->rdmult);
  }

  if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIM) {
    av1_set_ssim_rdmult(cpi, &x->mv_costs, bsize, mi_row, mi_col, &x->rdmult);
  }
#if CONFIG_TUNE_VMAF
  if (cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_WITHOUT_PREPROCESSING ||
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN ||
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_set_vmaf_rdmult(cpi, x, bsize, mi_row, mi_col, &x->rdmult);
  }
#endif
}

void av1_set_offsets_without_segment_id(
    const AV1_COMP *const cpi, const TileInfo *const tile, MACROBLOCK *const x,
    int mi_row, int mi_col, BLOCK_SIZE bsize,
    const CHROMA_REF_INFO *chroma_ref_info) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];

  set_mode_info_offsets(&cpi->common.mi_params, &cpi->mbmi_ext_info, x, xd,
                        mi_row, mi_col
#if CONFIG_C071_SUBBLK_WARPMV
                        ,
                        mi_width, mi_height
#endif  // CONFIG_C071_SUBBLK_WARPMV
  );

  set_entropy_context(xd, mi_row, mi_col, num_planes, chroma_ref_info);
#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context = cm->above_contexts.txfm[tile->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX

  // Set up destination pointers.
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);

  // Set up limit values for MV components.
  // Mv beyond the range do not produce new/different prediction block.
  av1_set_mv_limits(&cm->mi_params, &x->mv_limits, mi_row, mi_col, mi_height,
                    mi_width, cpi->oxcf.border_in_pixels);

  set_plane_n4(xd, mi_width, mi_height, num_planes, chroma_ref_info);

  // Set up distance of MB to edge of frame in 1/8th pel units.
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(!(mi_col & (mi_width - 1)) && !(mi_row & (mi_height - 1)));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  set_mi_row_col(xd, tile, mi_row, mi_height, mi_col, mi_width,
                 cm->mi_params.mi_rows, cm->mi_params.mi_cols, chroma_ref_info);

  // Set up source buffers.
  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes,
                       chroma_ref_info);

  // required by av1_append_sub8x8_mvs_for_idx() and av1_find_best_ref_mvs()
  xd->tile = *tile;
}

void av1_set_offsets(const AV1_COMP *const cpi, const TileInfo *const tile,
                     MACROBLOCK *const x, int mi_row, int mi_col,
                     BLOCK_SIZE bsize, const CHROMA_REF_INFO *chroma_ref_info) {
  const AV1_COMMON *const cm = &cpi->common;
  const struct segmentation *const seg = &cm->seg;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;

  av1_set_offsets_without_segment_id(cpi, tile, x, mi_row, mi_col, bsize,
                                     chroma_ref_info);

#if CONFIG_EXTENDED_SDP
  // Don't set up segment ID for chroma part in SDP of inter frame
  if (!frame_is_intra_only(cm) && xd->tree_type == CHROMA_PART) return;
#endif  // CONFIG_EXTENDED_SDP

  // Setup segment ID.
  mbmi = xd->mi[0];
  mbmi->segment_id = 0;
  if (seg->enabled) {
    if (seg->enabled && !cpi->vaq_refresh) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      mbmi->segment_id =
          map ? get_segment_id(&cm->mi_params, map, bsize, mi_row, mi_col) : 0;
    }
    av1_init_plane_quantizers(cpi, x, mbmi->segment_id);
  }
}

/*!\brief Interface for AV1 mode search for an individual coding block
 *
 * \ingroup partition_search
 * \callgraph
 * \callergraph
 * Searches prediction modes, transform, and coefficient coding modes for an
 * individual coding block. This function is the top-level interface that
 * directs the encoder to the proper mode search function, among these
 * implemented for inter/intra + rd/non-rd + non-skip segment/skip segment.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    tile_data      Pointer to struct holding adaptive
 *                              data/contexts/models for the tile during
 *                              encoding
 * \param[in]    x              Pointer to structure holding all the data for
 *                              the current macroblock
 * \param[in]    mi_row         Row coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    mi_col         Column coordinate of the block in a step size of
 *                              MI_SIZE
 * \param[in]    rd_cost        Pointer to structure holding rate and distortion
 *                              stats for the current block
 * \param[in]    partition      Partition mode of the parent block
 * \param[in]    cur_region_type      Region type of the current block
 * \param[in]    bsize          Current block size
 * \param[in]    ctx            Pointer to structure holding coding contexts and
 *                              chosen modes for the current block
 * \param[in]    best_rd        Upper bound of rd cost of a valid partition
 *
 * Nothing is returned. Instead, the chosen modes and contexts necessary
 * for reconstruction are stored in ctx, the rate-distortion stats are stored in
 * rd_cost. If no valid mode leading to rd_cost <= best_rd, the status will be
 * signalled by an INT64_MAX rd_cost->rdcost.
 */
static void pick_sb_modes(AV1_COMP *const cpi, TileDataEnc *tile_data,
                          MACROBLOCK *const x, int mi_row, int mi_col,
                          RD_STATS *rd_cost, PARTITION_TYPE partition,
#if CONFIG_EXTENDED_SDP
                          REGION_TYPE cur_region_type,
#endif  // CONFIG_EXTENDED_SDP
                          BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                          RD_STATS best_rd) {
  if (best_rd.rdcost < 0) {
    ctx->rd_stats.rdcost = INT64_MAX;
    ctx->rd_stats.skip_txfm = 0;
    av1_invalid_rd_stats(rd_cost);
    return;
  }

  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  int plane_type = (xd->tree_type == CHROMA_PART);
  assert(is_bsize_geq(bsize, cpi->common.mi_params.mi_alloc_bsize));

  av1_set_offsets(cpi, &tile_data->tile_info, x, mi_row, mi_col, bsize,
                  &ctx->chroma_ref_info);

#if CONFIG_EXTENDED_SDP
  xd->mi[0]->region_type = cur_region_type;
#endif  // CONFIG_EXTENDED_SDP

  if (ctx->rd_mode_is_ready) {
    assert(ctx->mic.sb_type[plane_type] == bsize);
    assert(ctx->mic.partition == partition);
    rd_cost->rate = ctx->rd_stats.rate;
    rd_cost->dist = ctx->rd_stats.dist;
    rd_cost->rdcost = ctx->rd_stats.rdcost;
#if CONFIG_MVP_IMPROVEMENT
    const int is_inter = is_inter_block(&ctx->mic, xd->tree_type);
#if CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
    if (cm->seq_params.enable_refmvbank && is_inter &&
        !is_intrabc_block(&ctx->mic, xd->tree_type))
#else
    if (cm->seq_params.enable_refmvbank && is_inter)
#endif  // CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
      av1_update_ref_mv_bank(cm, xd, &ctx->mic);
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
    if (is_inter) av1_update_warp_param_bank(cm, xd, &ctx->mic);
#endif  // WARP_CU_BANK
    return;
  }

  MB_MODE_INFO *mbmi;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const AQ_MODE aq_mode = cpi->oxcf.q_cfg.aq_mode;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  int i;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, rd_pick_sb_modes_time);
#endif

  aom_clear_system_state();

  mbmi = xd->mi[0];
  mbmi->sb_type[plane_type] = bsize;
  if (xd->tree_type == SHARED_PART) mbmi->sb_type[PLANE_TYPE_UV] = bsize;
  mbmi->partition = partition;
  mbmi->chroma_ref_info = ctx->chroma_ref_info;

#if CONFIG_RD_DEBUG
  mbmi->mi_row = mi_row;
  mbmi->mi_col = mi_col;
#endif

  // Sets up the tx_type_map buffer in MACROBLOCKD.
  xd->tx_type_map = txfm_info->tx_type_map_;
  xd->tx_type_map_stride = mi_size_wide[bsize];
  const BLOCK_SIZE chroma_bsize = get_bsize_base(xd, &ctx->mic, AOM_PLANE_U);
  xd->cctx_type_map = txfm_info->cctx_type_map_;
  xd->cctx_type_map_stride = mi_size_wide[chroma_bsize];

  for (i = 0; i < num_planes; ++i) {
    p[i].coeff = ctx->coeff[i];
    p[i].qcoeff = ctx->qcoeff[i];
    p[i].dqcoeff = ctx->dqcoeff[i];
    p[i].eobs = ctx->eobs[i];
    p[i].bobs = ctx->bobs[i];
    p[i].txb_entropy_ctx = ctx->txb_entropy_ctx[i];
  }

  for (i = 0; i < 2; ++i) pd[i].color_index_map = ctx->color_index_map[i];

  ctx->skippable = 0;
  // Set to zero to make sure we do not use the previous encoded frame stats
  mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
  // Reset skip mode flag.
  mbmi->skip_mode = 0;

  x->source_variance =
      av1_high_get_sby_perpixel_variance(cpi, &x->plane[0].src, bsize, xd->bd);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, aq_mode, mbmi);
  // Set error per bit for current rdmult
  av1_set_error_per_bit(&x->mv_costs, x->rdmult);
  av1_rd_cost_update(x->rdmult, &best_rd);

  // Find best coding mode & reconstruct the MB so it is available
  // as a predictor for MBs that follow in the SB
  if (frame_is_intra_only(cm)
#if CONFIG_EXTENDED_SDP
      || mbmi->region_type == INTRA_REGION
#endif  // CONFIG_EXTENDED_SDP
  ) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_rd_pick_intra_mode_sb_time);
#endif
    av1_rd_pick_intra_mode_sb(cpi, x, rd_cost, bsize, ctx, best_rd.rdcost);
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_rd_pick_intra_mode_sb_time);
#endif
  } else {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_rd_pick_inter_mode_sb_time);
#endif
    if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
      av1_rd_pick_inter_mode_sb_seg_skip(cpi, tile_data, x, mi_row, mi_col,
                                         rd_cost, bsize, ctx, best_rd.rdcost);
    } else {
      av1_rd_pick_inter_mode_sb(cpi, tile_data, x, rd_cost, bsize, ctx,
                                best_rd.rdcost);
    }
#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_rd_pick_inter_mode_sb_time);
#endif
  }

#if CONFIG_MVP_IMPROVEMENT
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
#if CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
  if (cm->seq_params.enable_refmvbank && is_inter &&
      !is_intrabc_block(mbmi, xd->tree_type))
#else
  if (cm->seq_params.enable_refmvbank && is_inter)
#endif  // CONFIG_IBC_SR_EXT && !CONFIG_IBC_BV_IMPROVEMENT
    av1_update_ref_mv_bank(cm, xd, mbmi);
#endif  // CONFIG_MVP_IMPROVEMENT

#if WARP_CU_BANK
  if (is_inter) av1_update_warp_param_bank(cm, xd, mbmi);
#endif  // WARP_CU_BANK

  // Examine the resulting rate and for AQ mode 2 make a segment choice.
  if (rd_cost->rate != INT_MAX && aq_mode == COMPLEXITY_AQ &&
      bsize >= BLOCK_16X16) {
    av1_caq_select_segment(cpi, x, bsize, mi_row, mi_col, rd_cost->rate);
  }

  x->rdmult = orig_rdmult;

  // TODO(jingning) The rate-distortion optimization flow needs to be
  // refactored to provide proper exit/return handle.
  if (rd_cost->rate == INT_MAX) rd_cost->rdcost = INT64_MAX;

  ctx->rd_stats.rate = rd_cost->rate;
  ctx->rd_stats.dist = rd_cost->dist;
  ctx->rd_stats.rdcost = rd_cost->rdcost;

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, rd_pick_sb_modes_time);
#endif
}

static void update_drl_index_stats(int max_drl_bits, const int16_t mode_ctx,
                                   FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                   const MB_MODE_INFO *mbmi,
                                   const MB_MODE_INFO_EXT *mbmi_ext) {
#if !CONFIG_ENTROPY_STATS
  (void)counts;
#endif  // !CONFIG_ENTROPY_STATS
  assert(have_drl_index(mbmi->mode));
#if CONFIG_EXTENDED_WARP_PREDICTION
  assert(IMPLIES(mbmi->mode == WARPMV, 0));
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  if (mbmi->mode == AMVDNEWMV) max_drl_bits = AOMMIN(max_drl_bits, 1);
  uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
#if CONFIG_SEP_COMP_DRL
  assert(mbmi->ref_mv_idx[0] < max_drl_bits + 1);
  assert(mbmi->ref_mv_idx[1] < max_drl_bits + 1);
  for (int ref = 0; ref < 1 + has_second_drl(mbmi); ++ref) {
    for (int idx = 0; idx < max_drl_bits; ++idx) {
      const uint16_t *weight = has_second_drl(mbmi)
                                   ? mbmi_ext->weight[mbmi->ref_frame[ref]]
                                   : mbmi_ext->weight[ref_frame_type];
      aom_cdf_prob *drl_cdf = av1_get_drl_cdf(fc, weight, mode_ctx, idx);
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
      if (ref && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
          mbmi->mode == NEAR_NEARMV && idx <= mbmi->ref_mv_idx[0])
        continue;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
#if CONFIG_ENTROPY_STATS
      int drl_ctx = av1_drl_ctx(mode_ctx);
      switch (idx) {
        case 0:
          counts->drl_mode[0][drl_ctx][mbmi->ref_mv_idx[ref] != idx]++;
          break;
        case 1:
          counts->drl_mode[1][drl_ctx][mbmi->ref_mv_idx[ref] != idx]++;
          break;
        default:
          counts->drl_mode[2][drl_ctx][mbmi->ref_mv_idx[ref] != idx]++;
          break;
      }
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(drl_cdf, mbmi->ref_mv_idx[ref] != idx, 2);
      if (mbmi->ref_mv_idx[ref] == idx) break;
    }
  }
#else
  assert(mbmi->ref_mv_idx < max_drl_bits + 1);
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    aom_cdf_prob *drl_cdf =
        av1_get_drl_cdf(fc, mbmi_ext->weight[ref_frame_type], mode_ctx, idx);
#if CONFIG_ENTROPY_STATS
    int drl_ctx = av1_drl_ctx(mode_ctx);
    switch (idx) {
      case 0: counts->drl_mode[0][drl_ctx][mbmi->ref_mv_idx != idx]++; break;
      case 1: counts->drl_mode[1][drl_ctx][mbmi->ref_mv_idx != idx]++; break;
      default: counts->drl_mode[2][drl_ctx][mbmi->ref_mv_idx != idx]++; break;
    }
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(drl_cdf, mbmi->ref_mv_idx != idx, 2);
    if (mbmi->ref_mv_idx == idx) break;
  }
#endif  // CONFIG_SEP_COMP_DRL
}

#if CONFIG_IBC_BV_IMPROVEMENT
static void update_intrabc_drl_idx_stats(int max_ref_bv_num, FRAME_CONTEXT *fc,
                                         FRAME_COUNTS *counts,
                                         const MB_MODE_INFO *mbmi) {
#if !CONFIG_ENTROPY_STATS
  (void)counts;
#endif  // !CONFIG_ENTROPY_STATS
  assert(mbmi->intrabc_drl_idx < max_ref_bv_num);
  int bit_cnt = 0;
  for (int idx = 0; idx < max_ref_bv_num - 1; ++idx) {
#if CONFIG_ENTROPY_STATS
    counts->intrabc_drl_idx[bit_cnt][mbmi->intrabc_drl_idx != idx]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->intrabc_drl_idx_cdf[bit_cnt], mbmi->intrabc_drl_idx != idx,
               2);
    if (mbmi->intrabc_drl_idx == idx) break;
    ++bit_cnt;
  }
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT

// Update the stats for compound weighted prediction
static void update_cwp_idx_stats(FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                 const AV1_COMMON *const cm, MACROBLOCKD *xd) {
#if !CONFIG_ENTROPY_STATS
  (void)counts;
#endif  // !CONFIG_ENTROPY_STATS
  const MB_MODE_INFO *mbmi = xd->mi[0];

  assert(mbmi->cwp_idx >= CWP_MIN && mbmi->cwp_idx <= CWP_MAX);
  int bit_cnt = 0;
  const int ctx = 0;

  int8_t final_idx = get_cwp_coding_idx(mbmi->cwp_idx, 1, cm, mbmi);
  for (int idx = 0; idx < MAX_CWP_NUM - 1; ++idx) {
#if CONFIG_ENTROPY_STATS
    counts->cwp_idx[bit_cnt][final_idx != idx]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->cwp_idx_cdf[ctx][bit_cnt], final_idx != idx, 2);
    if (final_idx == idx) break;
    ++bit_cnt;
  }
}

#if CONFIG_EXTENDED_WARP_PREDICTION
static void update_warp_delta_param_stats(int index, int value,
#if CONFIG_ENTROPY_STATS
                                          FRAME_COUNTS *counts,
#endif  // CONFIG_ENTROPY_STATS
                                          FRAME_CONTEXT *fc) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;
  int coded_value = (value / WARP_DELTA_STEP) + WARP_DELTA_CODED_MAX;
  assert(0 <= coded_value && coded_value < WARP_DELTA_NUM_SYMBOLS);

  update_cdf(fc->warp_delta_param_cdf[index_type], coded_value,
             WARP_DELTA_NUM_SYMBOLS);
#if CONFIG_ENTROPY_STATS
  counts->warp_delta_param[index_type][coded_value]++;
#endif  // CONFIG_ENTROPY_STATS
}

static void update_warp_delta_stats(const AV1_COMMON *cm,
                                    const MB_MODE_INFO *mbmi,
                                    const MB_MODE_INFO_EXT *mbmi_ext,
#if CONFIG_ENTROPY_STATS
                                    FRAME_COUNTS *counts,
#endif  // CONFIG_ENTROPY_STATS
                                    FRAME_CONTEXT *fc) {

  if (mbmi->max_num_warp_candidates > 1) {
    assert(mbmi->warp_ref_idx < mbmi->max_num_warp_candidates);
    int max_idx_bits = mbmi->max_num_warp_candidates - 1;
    for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
      aom_cdf_prob *warp_ref_idx_cdf = av1_get_warp_ref_idx_cdf(fc, bit_idx);
      update_cdf(warp_ref_idx_cdf, mbmi->warp_ref_idx != bit_idx, 2);
      if (mbmi->warp_ref_idx == bit_idx) break;
    }
  }
  if (allow_warp_parameter_signaling(cm, mbmi)) {
    const WarpedMotionParams *params = &mbmi->wm_params[0];
    WarpedMotionParams base_params;
    av1_get_warp_base_params(
        cm, mbmi, &base_params, NULL,
        mbmi_ext->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]);

    // The RDO stage should not give us a model which is not warpable.
    // Such models can still be signalled, but are effectively useless
    // as we'll just fall back to translational motion
    assert(!params->invalid);

    // TODO(rachelbarker): Allow signaling warp type?
    update_warp_delta_param_stats(2, params->wmmat[2] - base_params.wmmat[2],
#if CONFIG_ENTROPY_STATS
                                  counts,
#endif  // CONFIG_ENTROPY_STATS
                                  fc);
    update_warp_delta_param_stats(3, params->wmmat[3] - base_params.wmmat[3],
#if CONFIG_ENTROPY_STATS
                                  counts,
#endif  // CONFIG_ENTROPY_STATS
                                  fc);
  }
}
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_SKIP_MODE_ENHANCEMENT
static void update_skip_drl_index_stats(int max_drl_bits, FRAME_CONTEXT *fc,
                                        FRAME_COUNTS *counts,
                                        const MB_MODE_INFO *mbmi) {
#if !CONFIG_ENTROPY_STATS
  (void)counts;
#endif  // !CONFIG_ENTROPY_STATS
  assert(have_drl_index(mbmi->mode));
#if CONFIG_SEP_COMP_DRL
  assert(get_ref_mv_idx(mbmi, 0) < max_drl_bits + 1);
  assert(get_ref_mv_idx(mbmi, 1) < max_drl_bits + 1);
#else
  assert(mbmi->ref_mv_idx < max_drl_bits + 1);
#endif  // CONFIG_SEP_COMP_DRL
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    aom_cdf_prob *drl_cdf = fc->skip_drl_cdf[AOMMIN(idx, 2)];
#if CONFIG_SEP_COMP_DRL
    update_cdf(drl_cdf, mbmi->ref_mv_idx[0] != idx, 2);
#if CONFIG_ENTROPY_STATS
    switch (idx) {
      case 0: counts->skip_drl_mode[idx][mbmi->ref_mv_idx[0] != idx]++; break;
      case 1: counts->skip_drl_mode[idx][mbmi->ref_mv_idx[0] != idx]++; break;
      default: counts->skip_drl_mode[2][mbmi->ref_mv_idx[0] != idx]++; break;
    }
#endif  // CONFIG_ENTROPY_STATS
    if (mbmi->ref_mv_idx[0] == idx) break;
#else
    update_cdf(drl_cdf, mbmi->ref_mv_idx != idx, 2);
#if CONFIG_ENTROPY_STATS
    switch (idx) {
      case 0: counts->skip_drl_mode[idx][mbmi->ref_mv_idx != idx]++; break;
      case 1: counts->skip_drl_mode[idx][mbmi->ref_mv_idx != idx]++; break;
      default: counts->skip_drl_mode[2][mbmi->ref_mv_idx != idx]++; break;
    }
#endif  // CONFIG_ENTROPY_STATS
    if (mbmi->ref_mv_idx == idx) break;
#endif  // CONFIG_SEP_COMP_DRL
  }
}
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

static void update_stats(const AV1_COMMON *const cm, ThreadData *td) {
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const CurrentFrame *const current_frame = &cm->current_frame;
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  FRAME_CONTEXT *fc = xd->tile_ctx;
  const int inter_block = mbmi->ref_frame[0] != INTRA_FRAME;
  const int seg_ref_active = 0;

  if (current_frame->skip_mode_info.skip_mode_flag && !seg_ref_active &&
#if CONFIG_EXTENDED_SDP
      mbmi->region_type != INTRA_REGION &&
#endif  // CONFIG_EXTENDED_SDP
      is_comp_ref_allowed(bsize)) {
    const int skip_mode_ctx = av1_get_skip_mode_context(xd);
#if CONFIG_ENTROPY_STATS
    td->counts->skip_mode[skip_mode_ctx][mbmi->skip_mode]++;
#endif
    update_cdf(fc->skip_mode_cdfs[skip_mode_ctx], mbmi->skip_mode, 2);
  }

#if CONFIG_SKIP_TXFM_OPT
  const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  if (!seg_ref_active) {
    if (!mbmi->skip_mode && !frame_is_intra_only(cm)
#if CONFIG_EXTENDED_SDP
        && mbmi->region_type != INTRA_REGION
#endif  // CONFIG_EXTENDED_SDP
    ) {
      const int intra_inter_ctx = av1_get_intra_inter_context(xd);
#if CONFIG_ENTROPY_STATS
      td->counts->intra_inter[intra_inter_ctx][inter_block]++;
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(fc->intra_inter_cdf[intra_inter_ctx], inter_block, 2);
    }
    if (!inter_block && av1_allow_intrabc(cm, xd) &&
        xd->tree_type != CHROMA_PART) {
#if CONFIG_NEW_CONTEXT_MODELING
      const int intrabc_ctx = get_intrabc_ctx(xd);
      update_cdf(fc->intrabc_cdf[intrabc_ctx], use_intrabc, 2);
#if CONFIG_ENTROPY_STATS
      ++td->counts->intrabc[intrabc_ctx][use_intrabc];
#endif  // CONFIG_ENTROPY_STATS
#else
      update_cdf(fc->intrabc_cdf, use_intrabc, 2);
#if CONFIG_ENTROPY_STATS
      ++td->counts->intrabc[use_intrabc];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_NEW_CONTEXT_MODELING
    }

    if (inter_block || (!inter_block && use_intrabc)) {
#if !CONFIG_SKIP_MODE_ENHANCEMENT
      if (!mbmi->skip_mode) {
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
        const int skip_ctx = av1_get_skip_txfm_context(xd);
#if CONFIG_ENTROPY_STATS
        td->counts->skip_txfm[skip_ctx]
                             [mbmi->skip_txfm[xd->tree_type == CHROMA_PART]]++;
#endif
        update_cdf(fc->skip_txfm_cdfs[skip_ctx],
                   mbmi->skip_txfm[xd->tree_type == CHROMA_PART], 2);
#if !CONFIG_SKIP_MODE_ENHANCEMENT
      }
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
    }
  }
#else
#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (!seg_ref_active) {
#else
  if (!mbmi->skip_mode && !seg_ref_active) {
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    const int skip_ctx = av1_get_skip_txfm_context(xd);
#if CONFIG_ENTROPY_STATS
    td->counts
        ->skip_txfm[skip_ctx][mbmi->skip_txfm[xd->tree_type == CHROMA_PART]]++;
#endif
    update_cdf(fc->skip_txfm_cdfs[skip_ctx],
               mbmi->skip_txfm[xd->tree_type == CHROMA_PART], 2);
  }
#endif  // CONFIG_SKIP_TXFM_OPT

#if CONFIG_ENTROPY_STATS
  // delta quant applies to both intra and inter
  const int super_block_upper_left = ((xd->mi_row & (cm->mib_size - 1)) == 0) &&
                                     ((xd->mi_col & (cm->mib_size - 1)) == 0);
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  if (delta_q_info->delta_q_present_flag &&
      (bsize != cm->sb_size ||
       !mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) &&
      super_block_upper_left) {
    const int dq = (mbmi->current_qindex - xd->current_base_qindex) /
                   delta_q_info->delta_q_res;
    const int absdq = abs(dq);
    for (int i = 0; i < AOMMIN(absdq, DELTA_Q_SMALL); ++i) {
      td->counts->delta_q[i][1]++;
    }
    if (absdq < DELTA_Q_SMALL) td->counts->delta_q[absdq][0]++;
    if (delta_q_info->delta_lf_present_flag) {
      if (delta_q_info->delta_lf_multi) {
        const int frame_lf_count =
            av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          const int delta_lf = (mbmi->delta_lf[lf_id] - xd->delta_lf[lf_id]) /
                               delta_q_info->delta_lf_res;
          const int abs_delta_lf = abs(delta_lf);
          for (int i = 0; i < AOMMIN(abs_delta_lf, DELTA_LF_SMALL); ++i) {
            td->counts->delta_lf_multi[lf_id][i][1]++;
          }
          if (abs_delta_lf < DELTA_LF_SMALL)
            td->counts->delta_lf_multi[lf_id][abs_delta_lf][0]++;
        }
      } else {
        const int delta_lf =
            (mbmi->delta_lf_from_base - xd->delta_lf_from_base) /
            delta_q_info->delta_lf_res;
        const int abs_delta_lf = abs(delta_lf);
        for (int i = 0; i < AOMMIN(abs_delta_lf, DELTA_LF_SMALL); ++i) {
          td->counts->delta_lf[i][1]++;
        }
        if (abs_delta_lf < DELTA_LF_SMALL)
          td->counts->delta_lf[abs_delta_lf][0]++;
      }
    }
  }
#endif
  if (!is_inter_block(mbmi, xd->tree_type)) {
    av1_sum_intra_stats(cm, td->counts, xd, mbmi);
  }
  if (av1_allow_intrabc(cm, xd) && xd->tree_type != CHROMA_PART) {
#if !CONFIG_SKIP_TXFM_OPT
    const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
#if CONFIG_NEW_CONTEXT_MODELING
    const int intrabc_ctx = get_intrabc_ctx(xd);
    update_cdf(fc->intrabc_cdf[intrabc_ctx], use_intrabc, 2);
#if CONFIG_ENTROPY_STATS
    ++td->counts->intrabc[intrabc_ctx][use_intrabc];
#endif  // CONFIG_ENTROPY_STATS
#else
    update_cdf(fc->intrabc_cdf, use_intrabc, 2);
#if CONFIG_ENTROPY_STATS
    ++td->counts->intrabc[use_intrabc];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_NEW_CONTEXT_MODELING
#endif  // !CONFIG_SKIP_TXFM_OPT
#if CONFIG_IBC_BV_IMPROVEMENT
    if (use_intrabc) {
      const int_mv ref_mv = mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv;
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
      MV mv_diff;
      mv_diff.row = mbmi->mv[0].as_mv.row - ref_mv.as_mv.row;
      mv_diff.col = mbmi->mv[0].as_mv.col - ref_mv.as_mv.col;
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
      av1_update_mv_stats(&fc->ndvc, mv_diff, MV_PRECISION_ONE_PEL, 0);
#if CONFIG_DERIVED_MVD_SIGN
      if (mv_diff.row) {
        update_cdf(fc->ndvc.comps[0].sign_cdf, mv_diff.row < 0, 2);
      }
      if (mv_diff.col) {
        update_cdf(fc->ndvc.comps[1].sign_cdf, mv_diff.col < 0, 2);
      }
#endif

#else
      av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
          mv_diff, 0,
#else
          mbmi->mv[0].as_mv, ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
          &fc->ndvc, 0, MV_PRECISION_ONE_PEL);
#endif  // CONFIG_VQ_MVD_CODING
    }
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_BV_IMPROVEMENT
    if (use_intrabc) {
      update_cdf(fc->intrabc_mode_cdf, mbmi->intrabc_mode, 2);
#if CONFIG_ENTROPY_STATS
      ++td->counts->intrabc_mode[mbmi->intrabc_mode];
#endif  // CONFIG_ENTROPY_STATS
#if CONFIG_IBC_MAX_DRL
      update_intrabc_drl_idx_stats(cm->features.max_bvp_drl_bits + 1, fc,
                                   td->counts, mbmi);
#else
      update_intrabc_drl_idx_stats(MAX_REF_BV_STACK_SIZE, fc, td->counts, mbmi);
#endif  // CONFIG_IBC_MAX_DRL

#if CONFIG_MORPH_PRED
      const int morph_pred_ctx = get_morph_pred_ctx(xd);
      update_cdf(fc->morph_pred_cdf[morph_pred_ctx], mbmi->morph_pred, 2);
#if CONFIG_ENTROPY_STATS
      ++td->counts->morph_pred_count[morph_pred_ctx][mbmi->morph_pred];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_MORPH_PRED
    }
#endif  // CONFIG_IBC_BV_IMPROVEMENT
  }

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode && have_drl_index(mbmi->mode)) {
    FRAME_COUNTS *const counts = td->counts;
#if CONFIG_SKIP_MODE_ENHANCEMENT
    update_skip_drl_index_stats(cm->features.max_drl_bits, fc, counts, mbmi);
#else
    const int16_t mode_ctx_pristine =
        av1_mode_context_pristine(mbmi_ext->mode_context, mbmi->ref_frame);
    update_drl_index_stats(cm->features.max_drl_bits, mode_ctx_pristine, fc,
                           counts, mbmi, mbmi_ext);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

#if CONFIG_REFINEMV
  if (mbmi->skip_mode && switchable_refinemv_flag(cm, mbmi)) {
    const int refinemv_ctx = av1_get_refinemv_context(cm, xd, bsize);
    update_cdf(fc->refinemv_flag_cdf[refinemv_ctx], mbmi->refinemv_flag,
               REFINEMV_NUM_MODES);
  }
#endif  // CONFIG_REFINEMV

  if (frame_is_intra_only(cm) || mbmi->skip_mode) return;

  FRAME_COUNTS *const counts = td->counts;

  if (!seg_ref_active) {
#if !CONFIG_SKIP_TXFM_OPT
#if CONFIG_ENTROPY_STATS && !CONFIG_CONTEXT_DERIVATION
    counts->intra_inter[av1_get_intra_inter_context(xd)][inter_block]++;
#endif  // CONFIG_ENTROPY_STATS && !CONFIG_CONTEXT_DERIVATION
#if CONFIG_CONTEXT_DERIVATION
    const int skip_txfm = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
#if CONFIG_ENTROPY_STATS
    counts->intra_inter[skip_txfm][av1_get_intra_inter_context(xd)]
                       [inter_block]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->intra_inter_cdf[skip_txfm][av1_get_intra_inter_context(xd)],
               inter_block, 2);
#else
    update_cdf(fc->intra_inter_cdf[av1_get_intra_inter_context(xd)],
               inter_block, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
#endif  // !CONFIG_SKIP_TXFM_OPT
    // If the segment reference feature is enabled we have only a single
    // reference frame allowed for the segment so exclude it from
    // the reference frame counts used to work out probabilities.
    if (inter_block) {
      const MV_REFERENCE_FRAME ref0 = mbmi->ref_frame[0];
      const MV_REFERENCE_FRAME ref1 = mbmi->ref_frame[1];

      if (cm->features.tip_frame_mode &&
#if CONFIG_EXT_RECUR_PARTITIONS
          is_tip_allowed_bsize(mbmi)) {
#else   // CONFIG_EXT_RECUR_PARTITIONS
          is_tip_allowed_bsize(bsize)) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        const int tip_ctx = get_tip_ctx(xd);
        update_cdf(fc->tip_cdf[tip_ctx], is_tip_ref_frame(ref0), 2);
#if CONFIG_ENTROPY_STATS
        ++counts->tip_ref[tip_ctx][is_tip_ref_frame(ref0)];
#endif
      }

      if (current_frame->reference_mode == REFERENCE_MODE_SELECT &&
          !is_tip_ref_frame(ref0)) {
        if (is_comp_ref_allowed(bsize)) {
#if CONFIG_ENTROPY_STATS
          counts->comp_inter[av1_get_reference_mode_context(cm, xd)]
                            [has_second_ref(mbmi)]++;
#endif  // CONFIG_ENTROPY_STATS
          update_cdf(av1_get_reference_mode_cdf(cm, xd), has_second_ref(mbmi),
                     2);
        }
      }

      if (has_second_ref(mbmi)) {
        const int n_refs = cm->ref_frames_info.num_total_refs;
        int n_bits = 0;
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
        int may_have_same_ref_comp =
            cm->ref_frames_info.num_same_ref_compound > 0;
        assert(ref0 < ref1 + may_have_same_ref_comp);
        for (int i = 0;
             (i < n_refs + n_bits - 2 || may_have_same_ref_comp) && n_bits < 2;
             i++) {
          const int bit =
              ((n_bits == 0) && (ref0 == i)) || ((n_bits == 1) && (ref1 == i));
#elif CONFIG_ALLOW_SAME_REF_COMPOUND
        assert(ref0 <= ref1);
        for (int i = 0; i < n_refs - 1 && n_bits < 2; i++) {
          const int bit =
              ((n_bits == 0) && (ref0 == i)) || ((n_bits == 1) && (ref1 == i));
#else
      assert(ref0 < ref1);
      for (int i = 0; i < n_refs + n_bits - 2 && n_bits < 2; i++) {
            const int bit = ref0 == i || ref1 == i;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
          const int bit_type = n_bits == 0 ? -1
                                           : av1_get_compound_ref_bit_type(
                                                 &cm->ref_frames_info, ref0, i);
          int implicit_ref_bit = n_bits == 0 && i >= RANKED_REF0_TO_PRUNE - 1;
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
          implicit_ref_bit |=
              n_bits == 0 && i >= n_refs - 2 &&
              i + 1 >= cm->ref_frames_info.num_same_ref_compound;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
          if (!implicit_ref_bit) {
            update_cdf(
                av1_get_pred_cdf_compound_ref(xd, i, n_bits, bit_type, n_refs),
                bit, 2);
#if CONFIG_ENTROPY_STATS
            if (n_bits == 0) {
              counts->comp_ref0[av1_get_ref_pred_context(xd, i, n_refs)][i]
                               [bit]++;
            } else {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
              counts->comp_ref1[av1_get_ref_pred_context(xd, i, n_refs)]
                               [bit_type][i][bit]++;
#else
              counts->comp_ref1[av1_get_ref_pred_context(xd, i, n_refs)]
                               [bit_type][i - 1][bit]++;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
            }
#endif  // CONFIG_ENTROPY_STATS
          }
          n_bits += bit;
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
          if (i < cm->ref_frames_info.num_same_ref_compound &&
              may_have_same_ref_comp) {
            may_have_same_ref_comp =
                !bit && i + 1 < cm->ref_frames_info.num_same_ref_compound;
            i -= bit;
          } else {
            may_have_same_ref_comp = 0;
          }
#elif CONFIG_ALLOW_SAME_REF_COMPOUND
          if (i < cm->ref_frames_info.num_same_ref_compound) i -= bit;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
        }
      } else if (!is_tip_ref_frame(ref0)) {
        const int n_refs = cm->ref_frames_info.num_total_refs;
        const MV_REFERENCE_FRAME ref0_nrs = mbmi->ref_frame[0];
        for (int i = 0; i < n_refs - 1; i++) {
          const int bit = ref0_nrs == i;
          update_cdf(av1_get_pred_cdf_single_ref(xd, i, n_refs), bit, 2);
#if CONFIG_ENTROPY_STATS
          counts->single_ref[av1_get_ref_pred_context(xd, i, n_refs)][i][bit]++;
#endif  // CONFIG_ENTROPY_STATS
          if (bit) break;
        }
      }

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        update_cdf(fc->bawp_cdf[0], mbmi->bawp_flag[0] > 0, 2);
        if (mbmi->bawp_flag[0] > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
          update_cdf(fc->explicit_bawp_cdf[ctx_index], mbmi->bawp_flag[0] > 1,
                     2);
          if (mbmi->bawp_flag[0] > 1) {
            update_cdf(fc->explicit_bawp_scale_cdf, mbmi->bawp_flag[0] - 2,
                       EXPLICIT_BAWP_SCALE_CNT);
          }
        }
#else
        update_cdf(fc->bawp_cdf[0], mbmi->bawp_flag[0] == 1, 2);
#endif  // CONFIG_EXPLICIT_BAWP
        if (mbmi->bawp_flag[0]) {
          update_cdf(fc->bawp_cdf[1], mbmi->bawp_flag[1] == 1, 2);
        }
#if CONFIG_ENTROPY_STATS
        counts->bawp[mbmi->bawp_flag[0] == 1]++;
#endif  // CONFIG_ENTROPY_STATS
      }
#else
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        update_cdf(fc->bawp_cdf, mbmi->bawp_flag > 0, 2);
        if (mbmi->bawp_flag > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
          update_cdf(fc->explicit_bawp_cdf[ctx_index], mbmi->bawp_flag > 1, 2);
          if (mbmi->bawp_flag > 1) {
            update_cdf(fc->explicit_bawp_scale_cdf, mbmi->bawp_flag - 2,
                       EXPLICIT_BAWP_SCALE_CNT);
          }
        }
#else
        update_cdf(fc->bawp_cdf, mbmi->bawp_flag == 1, 2);
#endif  // CONFIG_EXPLICIT_BAWP
#if CONFIG_ENTROPY_STATS
        counts->bawp[mbmi->bawp_flag == 1]++;
#endif  // CONFIG_ENTROPY_STATS
      }
#endif  // CONFIG_BAWP_CHROMA
#endif  // CONFIG_BAWP
#if CONFIG_EXTENDED_WARP_PREDICTION
      const int allowed_motion_modes = motion_mode_allowed(
          cm, xd, mbmi_ext->ref_mv_stack[mbmi->ref_frame[0]], mbmi);
      MOTION_MODE motion_mode = mbmi->motion_mode;

      if (mbmi->mode == WARPMV) {
        if (allowed_motion_modes & (1 << WARPED_CAUSAL)) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
          counts->warped_causal_warpmv[motion_mode == WARPED_CAUSAL]++;
#endif
          update_cdf(fc->warped_causal_warpmv_cdf, motion_mode == WARPED_CAUSAL,
                     2);
#else
#if CONFIG_ENTROPY_STATS
          counts->warped_causal_warpmv[bsize][motion_mode == WARPED_CAUSAL]++;
#endif
          update_cdf(fc->warped_causal_warpmv_cdf[bsize],
                     motion_mode == WARPED_CAUSAL, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
        }
      }

      bool continue_motion_mode_signaling =
          (mbmi->mode == WARPMV) ? false : true;

      assert(IMPLIES(mbmi->mode == WARPMV,
                     mbmi->motion_mode == WARP_DELTA ||
                         mbmi->motion_mode == WARPED_CAUSAL));

      if (continue_motion_mode_signaling &&
          (allowed_motion_modes & (1 << INTERINTRA))) {
        const int bsize_group = size_group_lookup[bsize];
#if CONFIG_ENTROPY_STATS
        counts->interintra[bsize_group][motion_mode == INTERINTRA]++;
#endif
        update_cdf(fc->interintra_cdf[bsize_group], motion_mode == INTERINTRA,
                   2);

        if (motion_mode == INTERINTRA) {
#if CONFIG_ENTROPY_STATS
          counts->interintra_mode[bsize_group][mbmi->interintra_mode]++;
#endif
          update_cdf(fc->interintra_mode_cdf[bsize_group],
                     mbmi->interintra_mode, INTERINTRA_MODES);
          if (av1_is_wedge_used(bsize)) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
            counts->wedge_interintra[mbmi->use_wedge_interintra]++;
#endif
            update_cdf(fc->wedge_interintra_cdf, mbmi->use_wedge_interintra, 2);
#else
#if CONFIG_ENTROPY_STATS
            counts->wedge_interintra[bsize][mbmi->use_wedge_interintra]++;
#endif
            update_cdf(fc->wedge_interintra_cdf[bsize],
                       mbmi->use_wedge_interintra, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
            if (mbmi->use_wedge_interintra) {
#if CONFIG_WEDGE_MOD_EXT
              update_wedge_mode_cdf(fc, bsize, mbmi->interintra_wedge_index
#if CONFIG_ENTROPY_STATS
                                    ,
                                    counts
#endif  // CONFIG_ENTROPY_STATS
              );
#else
#if CONFIG_ENTROPY_STATS
              counts->wedge_idx[bsize][mbmi->interintra_wedge_index]++;
#endif
              update_cdf(fc->wedge_idx_cdf[bsize], mbmi->interintra_wedge_index,
                         16);
#endif  // CONFIG_WEDGE_MOD_EXT
            }
          }
          continue_motion_mode_signaling = false;
        }
      }

      if (continue_motion_mode_signaling &&
          (allowed_motion_modes & (1 << OBMC_CAUSAL))) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
        counts->obmc[motion_mode == OBMC_CAUSAL]++;
#endif
        update_cdf(fc->obmc_cdf, motion_mode == OBMC_CAUSAL, 2);
#else
#if CONFIG_ENTROPY_STATS
        counts->obmc[bsize][motion_mode == OBMC_CAUSAL]++;
#endif
        update_cdf(fc->obmc_cdf[bsize], motion_mode == OBMC_CAUSAL, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
        if (motion_mode == OBMC_CAUSAL) {
          continue_motion_mode_signaling = false;
        }
      }

      if (continue_motion_mode_signaling &&
          allowed_motion_modes & (1 << WARP_EXTEND)) {
        const int ctx1 = av1_get_warp_extend_ctx1(xd, mbmi);
        const int ctx2 = av1_get_warp_extend_ctx2(xd, mbmi);
#if CONFIG_ENTROPY_STATS
        counts->warp_extend[ctx1][ctx2][mbmi->motion_mode == WARP_EXTEND]++;
#endif
        update_cdf(fc->warp_extend_cdf[ctx1][ctx2],
                   mbmi->motion_mode == WARP_EXTEND, 2);
        if (motion_mode == WARP_EXTEND) {
          continue_motion_mode_signaling = false;
        }
      }

      if (continue_motion_mode_signaling &&
          (allowed_motion_modes & (1 << WARPED_CAUSAL))) {
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARPED_CAUSAL
#if CONFIG_ENTROPY_STATS
        counts->warped_causal[motion_mode == WARPED_CAUSAL]++;
#endif
        update_cdf(fc->warped_causal_cdf, motion_mode == WARPED_CAUSAL, 2);
#else
#if CONFIG_ENTROPY_STATS
        counts->warped_causal[bsize][motion_mode == WARPED_CAUSAL]++;
#endif
        update_cdf(fc->warped_causal_cdf[bsize], motion_mode == WARPED_CAUSAL,
                   2);
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARPED_CAUSAL
        if (motion_mode == WARPED_CAUSAL) {
          continue_motion_mode_signaling = false;
        }
      }

      if (continue_motion_mode_signaling &&
          (allowed_motion_modes & (1 << WARP_DELTA))) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
        counts->warp_delta[motion_mode == WARP_DELTA]++;
#endif
        update_cdf(fc->warp_delta_cdf, motion_mode == WARP_DELTA, 2);
#else
#if CONFIG_ENTROPY_STATS
        counts->warp_delta[bsize][motion_mode == WARP_DELTA]++;
#endif
        update_cdf(fc->warp_delta_cdf[bsize], motion_mode == WARP_DELTA, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
      }

      if (motion_mode == WARP_DELTA ||
          (motion_mode == WARPED_CAUSAL && mbmi->mode == WARPMV)) {
        update_warp_delta_stats(cm, mbmi, mbmi_ext,
#if CONFIG_ENTROPY_STATS
                                counts,
#endif  // CONFIG_ENTROPY_STATS
                                fc);
        // The following line is commented out to remove a spurious
        // static analysis warning. Uncomment when adding a new motion mode
        // continue_motion_mode_signaling = false;
      }

      if (allow_warpmv_with_mvd_coding(cm, mbmi)) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
        counts->warpmv_with_mvd_flag[mbmi->warpmv_with_mvd_flag]++;
#endif
        update_cdf(fc->warpmv_with_mvd_flag_cdf, mbmi->warpmv_with_mvd_flag, 2);
#else
#if CONFIG_ENTROPY_STATS
        counts->warpmv_with_mvd_flag[bsize][mbmi->warpmv_with_mvd_flag]++;
#endif
        update_cdf(fc->warpmv_with_mvd_flag_cdf[bsize],
                   mbmi->warpmv_with_mvd_flag, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
      } else {
        assert(mbmi->warpmv_with_mvd_flag == 0);
      }
#else
      if (cm->seq_params.enable_interintra_compound &&
          is_interintra_allowed(mbmi)) {
        const int bsize_group = size_group_lookup[bsize];
        if (mbmi->ref_frame[1] == INTRA_FRAME) {
#if CONFIG_ENTROPY_STATS
          counts->interintra[bsize_group][1]++;
#endif
          update_cdf(fc->interintra_cdf[bsize_group], 1, 2);
#if CONFIG_ENTROPY_STATS
          counts->interintra_mode[bsize_group][mbmi->interintra_mode]++;
#endif
          update_cdf(fc->interintra_mode_cdf[bsize_group],
                     mbmi->interintra_mode, INTERINTRA_MODES);
          if (av1_is_wedge_used(bsize)) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
            counts->wedge_interintra[mbmi->use_wedge_interintra]++;
#endif
            update_cdf(fc->wedge_interintra_cdf, mbmi->use_wedge_interintra, 2);
#else
#if CONFIG_ENTROPY_STATS
            counts->wedge_interintra[bsize][mbmi->use_wedge_interintra]++;
#endif
            update_cdf(fc->wedge_interintra_cdf[bsize],
                       mbmi->use_wedge_interintra, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
            if (mbmi->use_wedge_interintra) {
#if CONFIG_WEDGE_MOD_EXT
              update_wedge_mode_cdf(fc, bsize, mbmi->interintra_wedge_index
#if CONFIG_ENTROPY_STATS
                                    ,
                                    counts
#endif  // CONFIG_ENTROPY_STATS
              );
#else
#if CONFIG_ENTROPY_STATS
              counts->wedge_idx[bsize][mbmi->interintra_wedge_index]++;
#endif
              update_cdf(fc->wedge_idx_cdf[bsize], mbmi->interintra_wedge_index,
                         16);
#endif  // CONFIG_WEDGE_MOD_EXT
            }
          }
        } else {
#if CONFIG_ENTROPY_STATS
          counts->interintra[bsize_group][0]++;
#endif
          update_cdf(fc->interintra_cdf[bsize_group], 0, 2);
        }
      }

      const MOTION_MODE motion_allowed = motion_mode_allowed(cm, xd, mbmi);
      if (mbmi->ref_frame[1] != INTRA_FRAME) {
        if (motion_allowed == WARPED_CAUSAL) {
#if CONFIG_ENTROPY_STATS
          counts->motion_mode[bsize][mbmi->motion_mode]++;
#endif
          update_cdf(fc->motion_mode_cdf[bsize], mbmi->motion_mode,
                     MOTION_MODES);
        } else if (motion_allowed == OBMC_CAUSAL) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
          counts->obmc[mbmi->motion_mode == OBMC_CAUSAL]++;
#endif
          update_cdf(fc->obmc_cdf, mbmi->motion_mode == OBMC_CAUSAL, 2);
#else
#if CONFIG_ENTROPY_STATS
          counts->obmc[bsize][mbmi->motion_mode == OBMC_CAUSAL]++;
#endif
          update_cdf(fc->obmc_cdf[bsize], mbmi->motion_mode == OBMC_CAUSAL, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
        }
      }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_REFINEMV
      int is_refinemv_signaled = switchable_refinemv_flag(cm, mbmi);
      if (!mbmi->skip_mode && is_refinemv_signaled) {
        const int refinemv_ctx = av1_get_refinemv_context(cm, xd, bsize);
        update_cdf(fc->refinemv_flag_cdf[refinemv_ctx], mbmi->refinemv_flag,
                   REFINEMV_NUM_MODES);
      }
      assert(IMPLIES(mbmi->refinemv_flag && is_refinemv_signaled,
                     mbmi->comp_group_idx == 0 &&
                         mbmi->interinter_comp.type == COMPOUND_AVERAGE));
#endif  // CONFIG_REFINEMV
      if (has_second_ref(mbmi)
#if CONFIG_OPTFLOW_REFINEMENT
          && mbmi->mode < NEAR_NEARMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
          && (!mbmi->refinemv_flag || !is_refinemv_signaled)
#endif  // CONFIG_REFINEMV
          && !is_joint_amvd_coding_mode(mbmi->mode)) {
#if CONFIG_COMPOUND_WARP_CAUSAL
        assert(current_frame->reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               (mbmi->motion_mode == SIMPLE_TRANSLATION ||
                is_compound_warp_causal_allowed(mbmi)));
#else
        assert(current_frame->reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               mbmi->motion_mode == SIMPLE_TRANSLATION);
#endif  // CONFIG_COMPOUND_WARP_CAUSAL

        const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                         cm->seq_params.enable_masked_compound;
        if (masked_compound_used) {
          const int comp_group_idx_ctx = get_comp_group_idx_context(cm, xd);
#if CONFIG_ENTROPY_STATS
          ++counts->comp_group_idx[comp_group_idx_ctx][mbmi->comp_group_idx];
#endif
          update_cdf(fc->comp_group_idx_cdf[comp_group_idx_ctx],
                     mbmi->comp_group_idx, 2);
        }

        if (mbmi->comp_group_idx == 1) {
          assert(masked_compound_used);
          if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
            ++counts
                  ->compound_type[mbmi->interinter_comp.type - COMPOUND_WEDGE];
#endif
            update_cdf(fc->compound_type_cdf,
                       mbmi->interinter_comp.type - COMPOUND_WEDGE,
                       MASKED_COMPOUND_TYPES);
#else
#if CONFIG_ENTROPY_STATS
            ++counts->compound_type[bsize][mbmi->interinter_comp.type -
                                           COMPOUND_WEDGE];
#endif
            update_cdf(fc->compound_type_cdf[bsize],
                       mbmi->interinter_comp.type - COMPOUND_WEDGE,
                       MASKED_COMPOUND_TYPES);
#endif  // CONFIG_D149_CTX_MODELING_OPT
          }
        }
      }
      if (mbmi->interinter_comp.type == COMPOUND_WEDGE) {
        if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
#if CONFIG_WEDGE_MOD_EXT
          update_wedge_mode_cdf(fc, bsize, mbmi->interinter_comp.wedge_index
#if CONFIG_ENTROPY_STATS
                                ,
                                counts
#endif  // CONFIG_ENTROPY_STATS
          );
#else
#if CONFIG_ENTROPY_STATS
          counts->wedge_idx[bsize][mbmi->interinter_comp.wedge_index]++;
#endif
          update_cdf(fc->wedge_idx_cdf[bsize],
                     mbmi->interinter_comp.wedge_index, 16);
#endif  // CONFIG_WEDGE_MOD_EXT
        }
      }

      if (cm->features.enable_cwp && is_cwp_allowed(mbmi) && !mbmi->skip_mode) {
        update_cwp_idx_stats(fc, td->counts, cm, xd);
      }
    }
  }

  if (inter_block && cm->features.interp_filter == SWITCHABLE &&
      !is_warp_mode(mbmi->motion_mode) && !is_nontrans_global_motion(xd, mbmi)
#if CONFIG_REFINEMV
      && !(mbmi->refinemv_flag || mbmi->mode >= NEAR_NEARMV_OPTFLOW)
#endif  // CONFIG_REFINEMV
  ) {
    update_filter_type_cdf(xd, mbmi);
  }
  if (inter_block &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    const PREDICTION_MODE mode = mbmi->mode;
    const int16_t mode_ctx =
        av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
    if (has_second_ref(mbmi)) {
#if CONFIG_OPTFLOW_REFINEMENT
      if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
          opfl_allowed_for_cur_refs(cm, mbmi)) {
#if CONFIG_AFFINE_REFINEMENT
        const int allow_translational = is_translational_refinement_allowed(
            cm, comp_idx_to_opfl_mode[opfl_get_comp_idx(mode)]);
        const int allow_affine = is_affine_refinement_allowed(
            cm, xd, comp_idx_to_opfl_mode[opfl_get_comp_idx(mode)]);
        if (allow_affine || allow_translational) {
#endif  // CONFIG_AFFINE_REFINEMENT
          const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
#if CONFIG_ENTROPY_STATS
          ++counts->use_optflow[mode_ctx][use_optical_flow];
#endif
          update_cdf(fc->use_optflow_cdf[mode_ctx], use_optical_flow, 2);
#if CONFIG_AFFINE_REFINEMENT
        }
#endif  // CONFIG_AFFINE_REFINEMENT
      }
      const int comp_mode_idx = opfl_get_comp_idx(mode);
#if CONFIG_ENTROPY_STATS
      ++counts->inter_compound_mode[mode_ctx][comp_mode_idx];
#endif
      update_cdf(fc->inter_compound_mode_cdf[mode_ctx], comp_mode_idx,
                 INTER_COMPOUND_REF_TYPES);
#else
#if CONFIG_ENTROPY_STATS
      ++counts->inter_compound_mode[mode_ctx][INTER_COMPOUND_OFFSET(mode)];
#endif
      update_cdf(fc->inter_compound_mode_cdf[mode_ctx],
                 INTER_COMPOUND_OFFSET(mode), INTER_COMPOUND_MODES);
#endif  // CONFIG_OPTFLOW_REFINEMENT
      if (is_joint_mvd_coding_mode(mbmi->mode)) {
        const int is_joint_amvd_mode = is_joint_amvd_coding_mode(mbmi->mode);
        aom_cdf_prob *jmvd_scale_mode_cdf = is_joint_amvd_mode
                                                ? fc->jmvd_amvd_scale_mode_cdf
                                                : fc->jmvd_scale_mode_cdf;
        const int jmvd_scale_cnt = is_joint_amvd_mode
                                       ? JOINT_AMVD_SCALE_FACTOR_CNT
                                       : JOINT_NEWMV_SCALE_FACTOR_CNT;
        update_cdf(jmvd_scale_mode_cdf, mbmi->jmvd_scale_mode, jmvd_scale_cnt);
      }

    } else {
      av1_update_inter_mode_stats(fc, counts, mode, mode_ctx
#if CONFIG_EXTENDED_WARP_PREDICTION
                                  ,
                                  cm, xd, mbmi, bsize
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

      );
    }

    const int new_mv = have_newmv_in_each_reference(mbmi->mode);
    const int jmvd_base_ref_list = is_joint_mvd_coding_mode(mbmi->mode)
                                       ? get_joint_mvd_base_ref_list(cm, mbmi)
                                       : 0;
    const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
    if (have_drl_index(mbmi->mode)) {
      const int16_t mode_ctx_pristine =
          av1_mode_context_pristine(mbmi_ext->mode_context, mbmi->ref_frame);
      update_drl_index_stats(cm->features.max_drl_bits, mode_ctx_pristine, fc,
                             counts, mbmi, mbmi_ext);
    }

#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
    MV mv_diff[2] = { kZeroMv, kZeroMv };
#if CONFIG_DERIVED_MVD_SIGN
    int num_signaled_mvd = 0;
    int start_signaled_mvd_idx = 0;
#endif
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

#if CONFIG_EXTENDED_WARP_PREDICTION
    if (xd->tree_type != CHROMA_PART && mbmi->mode == WARPMV) {
      if (mbmi->warpmv_with_mvd_flag) {
        WarpedMotionParams ref_warp_model =
            mbmi_ext
                ->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
                                  [mbmi->warp_ref_idx]
                .wm_params;
        int_mv ref_mv =
            get_mv_from_wrl(xd, &ref_warp_model, mbmi->pb_mv_precision, bsize,
                            xd->mi_col, xd->mi_row);
        assert(is_adaptive_mvd == 0);
#if CONFIG_DERIVED_MVD_SIGN
        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 0;
#endif
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
        get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                            mbmi->pb_mv_precision, &mv_diff[0]);
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
        av1_update_mv_stats(&fc->nmvc, mv_diff[0], mbmi->pb_mv_precision,
                            is_adaptive_mvd);

#else
        av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
            mv_diff[0], 1,
#else
            mbmi->mv[0].as_mv, ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
            &fc->nmvc, is_adaptive_mvd, mbmi->pb_mv_precision);
#endif  //  CONFIG_VQ_MVD_CODING
      }
    } else {
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

      if (have_newmv_in_inter_mode(mbmi->mode) &&
          xd->tree_type != CHROMA_PART) {
        const int pb_mv_precision = mbmi->pb_mv_precision;
        assert(IMPLIES(cm->features.cur_frame_force_integer_mv,
                       pb_mv_precision == MV_PRECISION_ONE_PEL));

        if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
          assert(!is_adaptive_mvd);
          assert(mbmi->most_probable_pb_mv_precision <= mbmi->max_mv_precision);
          const int mpp_flag_context = av1_get_mpp_flag_context(cm, xd);
          const int mpp_flag =
              (mbmi->pb_mv_precision == mbmi->most_probable_pb_mv_precision);
          update_cdf(fc->pb_mv_mpp_flag_cdf[mpp_flag_context], mpp_flag, 2);

          if (!mpp_flag) {
            const PRECISION_SET *precision_def =
                &av1_mv_precision_sets[mbmi->mb_precision_set];
            int down = av1_get_pb_mv_precision_index(mbmi);
            int nsymbs = precision_def->num_precisions - 1;

            const int down_ctx = av1_get_pb_mv_precision_down_context(cm, xd);

            update_cdf(
                fc->pb_mv_precision_cdf[down_ctx][mbmi->max_mv_precision -
                                                  MV_PRECISION_HALF_PEL],
                down, nsymbs);
          }
        }

        if (new_mv) {
#if CONFIG_DERIVED_MVD_SIGN
          num_signaled_mvd = 1 + has_second_ref(mbmi);
          start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN

          for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
            const int_mv ref_mv = av1_get_ref_mv(x, ref);
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
            get_mvd_from_ref_mv(mbmi->mv[ref].as_mv, ref_mv.as_mv,
                                is_adaptive_mvd, pb_mv_precision,
                                &mv_diff[ref]);
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

#if CONFIG_VQ_MVD_CODING
            av1_update_mv_stats(&fc->nmvc, mv_diff[ref], pb_mv_precision,
                                is_adaptive_mvd);
#else
          av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
              mv_diff[ref], 1,
#else
              mbmi->mv[ref].as_mv, ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
              &fc->nmvc, is_adaptive_mvd, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING
          }
        } else if (have_nearmv_newmv_in_inter_mode(mbmi->mode)) {
          const int ref =
#if CONFIG_OPTFLOW_REFINEMENT
              mbmi->mode == NEAR_NEWMV_OPTFLOW ||
#endif  // CONFIG_OPTFLOW_REFINEMENT
              jmvd_base_ref_list || mbmi->mode == NEAR_NEWMV;
          const int_mv ref_mv = av1_get_ref_mv(x, ref);
#if CONFIG_DERIVED_MVD_SIGN
          num_signaled_mvd = 1;
          start_signaled_mvd_idx = ref;
#endif
#if CONFIG_VQ_MVD_CODING || CONFIG_DERIVED_MVD_SIGN
          get_mvd_from_ref_mv(mbmi->mv[ref].as_mv, ref_mv.as_mv,
                              is_adaptive_mvd, pb_mv_precision, &mv_diff[ref]);
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
          av1_update_mv_stats(&fc->nmvc, mv_diff[ref], pb_mv_precision,
                              is_adaptive_mvd);
#else
        av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
            mv_diff[ref], 1,
#else
            mbmi->mv[ref].as_mv, ref_mv.as_mv,
#endif  //  CONFIG_DERIVED_MVD_SIGN
            &fc->nmvc, is_adaptive_mvd, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING
        }
      }

#if CONFIG_EXTENDED_WARP_PREDICTION
    }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_DERIVED_MVD_SIGN
    // Update stats of the sign in the second pass
    if (num_signaled_mvd > 0) {
      int last_ref = -1;
      int last_comp = -1;
      uint16_t sum_mvd = 0;
      int precision_shift = MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision;
      int th_for_num_nonzero = get_derive_sign_nzero_th(mbmi);
      uint8_t num_nonzero_mvd_comp = 0;
      uint8_t enable_sign_derive = 0;
      if (is_mvd_sign_derive_allowed(cm, xd, mbmi)) {
        for (int ref = start_signaled_mvd_idx;
             ref < start_signaled_mvd_idx + num_signaled_mvd; ++ref) {
          assert(ref == 0 || ref == 1);
          for (int comp = 0; comp < 2; comp++) {
            int this_mvd_comp = comp == 0 ? mv_diff[ref].row : mv_diff[ref].col;
            if (this_mvd_comp) {
              last_ref = ref;
              last_comp = comp;
              sum_mvd = sum_mvd + (abs(this_mvd_comp) >> precision_shift);
              num_nonzero_mvd_comp++;
            }
          }
        }
        if (num_nonzero_mvd_comp >= th_for_num_nonzero) enable_sign_derive = 1;
      }

      for (int ref = start_signaled_mvd_idx;
           ref < start_signaled_mvd_idx + num_signaled_mvd; ++ref) {
        assert(ref == 0 || ref == 1);
        for (int comp = 0; comp < 2; comp++) {
          if (enable_sign_derive && (ref == last_ref && comp == last_comp))
            continue;
          int this_mvd_comp = comp == 0 ? mv_diff[ref].row : mv_diff[ref].col;
          if (this_mvd_comp) {
            const int sign = this_mvd_comp < 0;
            update_cdf(fc->nmvc.comps[comp].sign_cdf, sign, 2);
          }
        }
      }
    }
#endif  // CONFIG_DERIVED_MVD_SIGN
  }
}

/*!\brief Reconstructs an individual coding block
 *
 * \ingroup partition_search
 * Reconstructs an individual coding block by applying the chosen modes stored
 * in ctx, also updates mode counts and entropy models.
 *
 * This function works on planes determined by get_partition_plane_start() and
 * get_partition_plane_end() based on xd->tree_type.
 *
 * \param[in]    cpi       Top-level encoder structure
 * \param[in]    tile_data Pointer to struct holding adaptive
 *                         data/contexts/models for the tile during encoding
 * \param[in]    td        Pointer to thread data
 * \param[in]    tp        Pointer to the starting token
 * \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
 * \param[in]    mi_col    Column coordinate of the block in a step size of
 *                         MI_SIZE
 * \param[in]    dry_run   A code indicating whether it is part of the final
 *                         pass for reconstructing the superblock
 * \param[in]    bsize     Current block size
 * \param[in]    partition Partition mode of the parent block
 * \param[in]    ctx       Pointer to structure holding coding contexts and the
 *                         chosen modes for the current block
 * \param[in]    rate      Pointer to the total rate for the current block
 *
 * Nothing is returned. Instead, reconstructions (w/o in-loop filters)
 * will be updated in the pixel buffers in td->mb.e_mbd. Also, the chosen modes
 * will be stored in the MB_MODE_INFO buffer td->mb.e_mbd.mi[0].
 */
static void encode_b(const AV1_COMP *const cpi, TileDataEnc *tile_data,
                     ThreadData *td, TokenExtra **tp, int mi_row, int mi_col,
                     RUN_TYPE dry_run, BLOCK_SIZE bsize,
                     PARTITION_TYPE partition,
                     const PICK_MODE_CONTEXT *const ctx, int *rate) {
  const AV1_COMMON *const cm = &cpi->common;
  TileInfo *const tile = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;

  av1_set_offsets_without_segment_id(cpi, tile, x, mi_row, mi_col, bsize,
                                     &ctx->chroma_ref_info);
  const int origin_mult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);
  MB_MODE_INFO *mbmi = xd->mi[0];
  mbmi->partition = partition;
  av1_update_state(cpi, td, ctx, mi_row, mi_col, bsize, dry_run);

  const int num_planes = av1_num_planes(cm);
  const int plane_start = (xd->tree_type == CHROMA_PART);
  const int plane_end = (xd->tree_type == LUMA_PART) ? 1 : num_planes;

  if (!dry_run) {
    for (int plane = plane_start; plane < plane_end; plane++) {
      x->mbmi_ext_frame->cb_offset[plane] = x->cb_offset[plane];
      assert(x->cb_offset[plane] <
             (1 << num_pels_log2_lookup[cpi->common.sb_size]));
    }
#if CONFIG_LR_IMPROVEMENTS
    av1_init_txk_skip_array(&cpi->common, mi_row, mi_col, bsize, 0,
                            xd->tree_type, &mbmi->chroma_ref_info, plane_start,
                            plane_end);
#endif  // CONFIG_LR_IMPROVEMENTS
  }

  encode_superblock(cpi, tile_data, td, tp, dry_run, bsize, plane_start,
                    plane_end, rate);
#if CONFIG_REFINED_MVS_IN_TMVP
  if (!dry_run && cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
    const MB_MODE_INFO *const mi = &ctx->mic;
    if (opfl_allowed_for_cur_block(cm, mi)
#if CONFIG_REFINEMV
        || (mi->refinemv_flag && mi->interinter_comp.type == COMPOUND_AVERAGE)
#endif  // CONFIG_REFINEMV
    ) {
      const int bw = mi_size_wide[mi->sb_type[xd->tree_type == CHROMA_PART]];
      const int bh = mi_size_high[mi->sb_type[xd->tree_type == CHROMA_PART]];
      const int x_inside_boundary = AOMMIN(bw, cm->mi_params.mi_cols - mi_col);
      const int y_inside_boundary = AOMMIN(bh, cm->mi_params.mi_rows - mi_row);
      av1_copy_frame_refined_mvs(cm, xd, mi, mi_row, mi_col, x_inside_boundary,
                                 y_inside_boundary);
    }
  }
#endif  // CONFIG_REFINED_MVS_IN_TMVP

  if (!dry_run) {
    for (int plane = plane_start; plane < plane_end; ++plane) {
      if (plane == 0) {
        x->cb_offset[plane] += block_size_wide[bsize] * block_size_high[bsize];
      } else if (xd->is_chroma_ref) {
        const BLOCK_SIZE bsize_base = mbmi->chroma_ref_info.bsize_base;
        x->cb_offset[plane] +=
            block_size_wide[bsize_base] * block_size_high[bsize_base];
      }
    }
    if (bsize == cpi->common.sb_size &&
        mbmi->skip_txfm[xd->tree_type == CHROMA_PART] == 1 &&
        cm->delta_q_info.delta_lf_present_flag) {
      const int frame_lf_count =
          av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
      for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id)
        mbmi->delta_lf[lf_id] = xd->delta_lf[lf_id];
      mbmi->delta_lf_from_base = xd->delta_lf_from_base;
    }
    if (has_second_ref(mbmi)) {
      if (mbmi->interinter_comp.type == COMPOUND_AVERAGE)
        mbmi->comp_group_idx = 0;
      else
        mbmi->comp_group_idx = 1;
    }

    // delta quant applies to both intra and inter
    const int super_block_upper_left = ((mi_row & (cm->mib_size - 1)) == 0) &&
                                       ((mi_col & (cm->mib_size - 1)) == 0);
    const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
    if (delta_q_info->delta_q_present_flag &&
        (bsize != cm->sb_size ||
         !mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) &&
        super_block_upper_left) {
      xd->current_base_qindex = mbmi->current_qindex;
      if (delta_q_info->delta_lf_present_flag) {
        if (delta_q_info->delta_lf_multi) {
          const int frame_lf_count =
              av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
          for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
            xd->delta_lf[lf_id] = mbmi->delta_lf[lf_id];
          }
        } else {
          xd->delta_lf_from_base = mbmi->delta_lf_from_base;
        }
      }
    }

    RD_COUNTS *rdc = &td->rd_counts;
    if (mbmi->skip_mode) {
      assert(!frame_is_intra_only(cm));
      rdc->skip_mode_used_flag = 1;
      if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
#if !CONFIG_SKIP_MODE_ENHANCEMENT
        assert(has_second_ref(mbmi));
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
#if CONFIG_D072_SKIP_MODE_IMPROVE
        if (has_second_ref(mbmi)) {
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
          rdc->compound_ref_used_flag = 1;
#if CONFIG_D072_SKIP_MODE_IMPROVE
        }
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
      }
      set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    } else {
      const int seg_ref_active = 0;
      if (!seg_ref_active) {
        // If the segment reference feature is enabled we have only a single
        // reference frame allowed for the segment so exclude it from
        // the reference frame counts used to work out probabilities.
        if (is_inter_block(mbmi, xd->tree_type)) {
          av1_collect_neighbors_ref_counts(xd);
          if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
            if (has_second_ref(mbmi)) {
              // This flag is also updated for 4x4 blocks
              rdc->compound_ref_used_flag = 1;
            }
          }
          set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
        }
      }
    }

    if (tile_data->allow_update_cdf) update_stats(&cpi->common, td);

    // Gather obmc and warped motion count to update the probability.
    if ((!cpi->sf.inter_sf.disable_obmc &&
         cpi->sf.inter_sf.prune_obmc_prob_thresh > 0) ||
#if CONFIG_EXTENDED_WARP_PREDICTION
        cpi->sf.inter_sf.prune_warped_prob_thresh > 0 ||
        cpi->sf.inter_sf.prune_warpmv_prob_thresh > 0) {
#else
        (cm->features.allow_warped_motion &&
         cpi->sf.inter_sf.prune_warped_prob_thresh > 0)) {
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
      const int inter_block = is_inter_block(mbmi, xd->tree_type);
      const int seg_ref_active = 0;
      if (!seg_ref_active && inter_block) {
#if CONFIG_EXTENDED_WARP_PREDICTION
        const int allowed_motion_modes = motion_mode_allowed(
            cm, xd, x->mbmi_ext->ref_mv_stack[mbmi->ref_frame[0]], mbmi);
        if (mbmi->motion_mode != INTERINTRA) {
          if (allowed_motion_modes & (1 << OBMC_CAUSAL)) {
            td->rd_counts.obmc_used[bsize][mbmi->motion_mode == OBMC_CAUSAL]++;
          }
          int is_warp_allowed = (allowed_motion_modes & (1 << WARPED_CAUSAL)) ||
                                (allowed_motion_modes & (1 << WARP_DELTA)) ||
                                (allowed_motion_modes & (1 << WARP_EXTEND));
          if (is_warp_allowed) {
            td->rd_counts.warped_used[mbmi->motion_mode >= WARPED_CAUSAL]++;
          }
          // TODO(rachelbarker): Add counts and pruning for WARP_DELTA and
          // WARP_EXTEND
        }
#else
        const MOTION_MODE motion_allowed = motion_mode_allowed(cm, xd, mbmi);

        if (mbmi->ref_frame[1] != INTRA_FRAME) {
          if (motion_allowed >= OBMC_CAUSAL) {
            td->rd_counts.obmc_used[bsize][mbmi->motion_mode == OBMC_CAUSAL]++;
          }
          if (motion_allowed == WARPED_CAUSAL) {
            td->rd_counts.warped_used[mbmi->motion_mode == WARPED_CAUSAL]++;
          }
        }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
      }
    }
  }
  // TODO(Ravi/Remya): Move this copy function to a better logical place
  // This function will copy the best mode information from block
  // level (x->mbmi_ext) to frame level (cpi->mbmi_ext_info.frame_base). This
  // frame level buffer (cpi->mbmi_ext_info.frame_base) will be used during
  // bitstream preparation.
  if (xd->tree_type != CHROMA_PART)
#if CONFIG_SKIP_MODE_ENHANCEMENT
  {
    if (mbmi->skip_mode) {
      MV_REFERENCE_FRAME rf[2];
      const SkipModeInfo *const skip_mode_info =
          &cpi->common.current_frame.skip_mode_info;
      rf[0] = skip_mode_info->ref_frame_idx_0;
      rf[1] = skip_mode_info->ref_frame_idx_1;
      MV_REFERENCE_FRAME ref_frame_type = av1_ref_frame_type(rf);

      av1_find_mv_refs(&cpi->common, xd, mbmi, ref_frame_type,
                       x->mbmi_ext->ref_mv_count, xd->ref_mv_stack, xd->weight,
                       NULL, NULL
#if !CONFIG_C076_INTER_MOD_CTX
                       ,
                       NULL
#endif  //! CONFIG_C076_INTER_MOD_CTX
#if CONFIG_EXTENDED_WARP_PREDICTION
                       ,
                       NULL, 0, NULL
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
      );
      // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
      // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
      av1_copy_usable_ref_mv_stack_and_weight(xd, x->mbmi_ext, ref_frame_type);
    }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

    av1_copy_mbmi_ext_to_mbmi_ext_frame(
        x->mbmi_ext_frame, x->mbmi_ext,
#if CONFIG_SEP_COMP_DRL
        mbmi,
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_SKIP_MODE_ENHANCEMENT
        mbmi->skip_mode,
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
        av1_ref_frame_type(xd->mi[0]->ref_frame));
#if CONFIG_SKIP_MODE_ENHANCEMENT
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  x->rdmult = origin_mult;
}

static void update_partition_stats(MACROBLOCKD *const xd,
#if CONFIG_ENTROPY_STATS
                                   FRAME_COUNTS *counts,
#endif  // CONFIG_ENTROPY_STATS
                                   int allow_update_cdf,
                                   const CommonModeInfoParams *const mi_params,
#if CONFIG_EXT_RECUR_PARTITIONS
                                   int disable_ext_part,
                                   PARTITION_TREE const *ptree_luma,
                                   const CHROMA_REF_INFO *chroma_ref_info,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                                   PARTITION_TYPE partition, const int mi_row,
                                   const int mi_col, BLOCK_SIZE bsize,
                                   const int ctx, BLOCK_SIZE sb_size) {
#if !CONFIG_BLOCK_256
  (void)sb_size;
#endif  // !CONFIG_BLOCK_256
  const TREE_TYPE tree_type = xd->tree_type;
  const int plane_index = tree_type == CHROMA_PART;
  FRAME_CONTEXT *fc = xd->tile_ctx;
  assert(ctx >= 0);  // is_partition_point() is true.

#if CONFIG_EXT_RECUR_PARTITIONS
  const bool ss_x = xd->plane[1].subsampling_x;
  const bool ss_y = xd->plane[1].subsampling_y;

  const PARTITION_TYPE derived_partition =
      av1_get_normative_forced_partition_type(
          mi_params, tree_type, ss_x, ss_y, mi_row, mi_col, bsize,
#if CONFIG_CB1TO4_SPLIT
          BLOCK_INVALID,  // as it is a partition point
#endif                    // CONFIG_CB1TO4_SPLIT
          ptree_luma, chroma_ref_info);
  if (derived_partition != PARTITION_INVALID) {
    assert(partition == derived_partition &&
           "Partition does not match normatively derived partition.");
    return;
  }

  const bool do_split = partition != PARTITION_NONE;
  if (allow_update_cdf) {
#if CONFIG_ENTROPY_STATS
    counts->do_split[plane_index][ctx][do_split]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->do_split_cdf[plane_index][ctx], do_split, 2);
  }
  if (!do_split) {
    return;
  }

#if CONFIG_BLOCK_256
  const bool do_square_split = partition == PARTITION_SPLIT;
  if (is_square_split_eligible(bsize, sb_size)) {
    const int square_split_ctx =
        square_split_context(xd, mi_row, mi_col, bsize);
#if CONFIG_ENTROPY_STATS
    counts->do_square_split[plane_index][square_split_ctx][do_square_split]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->do_square_split_cdf[plane_index][square_split_ctx],
               do_square_split, 2);
  }
  if (do_square_split) {
    return;
  }
#endif  // CONFIG_BLOCK_256

  RECT_PART_TYPE rect_type = get_rect_part_type(partition);
  if (rect_type_implied_by_bsize(bsize, tree_type) == RECT_INVALID) {
#if CONFIG_ENTROPY_STATS
    counts->rect_type[plane_index][ctx][rect_type]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->rect_type_cdf[plane_index][ctx], rect_type, 2);
  }

  const bool ext_partition_allowed =
      !disable_ext_part &&
      is_ext_partition_allowed(bsize, rect_type, tree_type);
  if (ext_partition_allowed) {
    const bool do_ext_partition = (partition >= PARTITION_HORZ_3);
#if CONFIG_ENTROPY_STATS
    counts->do_ext_partition[plane_index][rect_type][ctx][do_ext_partition]++;
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->do_ext_partition_cdf[plane_index][rect_type][ctx],
               do_ext_partition, 2);
    if (do_ext_partition) {
      const bool uneven_4way_partition_allowed =
          is_uneven_4way_partition_allowed(bsize, rect_type, tree_type);
      if (uneven_4way_partition_allowed) {
        const bool do_uneven_4way_partition = (partition >= PARTITION_HORZ_4A);
#if CONFIG_ENTROPY_STATS
        counts->do_uneven_4way_partition[plane_index][rect_type][ctx]
                                        [do_uneven_4way_partition]++;
#endif  // CONFIG_ENTROPY_STATS
        update_cdf(
            fc->do_uneven_4way_partition_cdf[plane_index][rect_type][ctx],
            do_uneven_4way_partition, 2);
        if (do_uneven_4way_partition) {
          const UNEVEN_4WAY_PART_TYPE uneven_4way_type =
              (partition == PARTITION_HORZ_4A || partition == PARTITION_VERT_4A)
                  ? UNEVEN_4A
                  : UNEVEN_4B;
#if CONFIG_ENTROPY_STATS
          counts->uneven_4way_partition_type[plane_index][rect_type][ctx]
                                            [uneven_4way_type]++;
#endif  // CONFIG_ENTROPY_STATS
          update_cdf(
              fc->uneven_4way_partition_type_cdf[plane_index][rect_type][ctx],
              uneven_4way_type, NUM_UNEVEN_4WAY_PARTS);
        }
      }
    }
  }
#else  // CONFIG_EXT_RECUR_PARTITIONS
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  const int has_rows = (mi_row + hbs_h) < mi_params->mi_rows;
  const int has_cols = (mi_col + hbs_w) < mi_params->mi_cols;
  if (has_rows && has_cols) {
    int luma_split_flag = 0;
    int parent_block_width = block_size_wide[bsize];
    if (tree_type == CHROMA_PART && parent_block_width >= SHARED_PART_SIZE) {
      luma_split_flag = get_luma_split_flag(bsize, mi_params, mi_row, mi_col);
    }
    if (luma_split_flag <= 3) {
#if CONFIG_ENTROPY_STATS
      counts->partition[plane_index][ctx][partition]++;
#endif  // CONFIG_ENTROPY_STATS
      if (allow_update_cdf) {
        update_cdf(fc->partition_cdf[plane_index][ctx], partition,
                   partition_cdf_length(bsize));
      }
    } else {
      // if luma blocks uses smaller blocks, then chroma will also split
      assert(partition == PARTITION_SPLIT);
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

#if CONFIG_EXT_RECUR_PARTITIONS
/*!\brief Reconstructs a partition (may contain multiple coding blocks)
 *
 * \ingroup partition_search
 * Reconstructs a sub-partition of the superblock by applying the chosen modes
 * and partition trees stored in pc_tree.
 *
 * \param[in]    cpi        Top-level encoder structure
 * \param[in]    td         Pointer to thread data
 * \param[in]    tile_data  Pointer to struct holding adaptive
 *                          data/contexts/models for the tile during encoding
 * \param[in]    tp         Pointer to the starting token
 * \param[in]    mi_row     Row coordinate of the block in a step size of
 *                          MI_SIZE
 * \param[in]    mi_col     Column coordinate of the block in a step size of
 *                          MI_SIZE
 * \param[in]    dry_run    A code indicating whether it is part of the final
 *                          pass for reconstructing the superblock
 * \param[in]    bsize      Current block size
 * \param[in]    pc_tree    Pointer to the PC_TREE node storing the picked
 *                          partitions and mode info for the current block
 * \param[in]    ptree      Pointer to the PARTITION_TREE node holding the
 *                          partition info for the current node and all of its
 *                          descendants.
 * \param[in]    ptree_luma Pointer to the luma partition tree so that the
 *                          encoder to estimate the
 *                          partition type for chroma.
 * \param[in]     rate      Pointer to the total rate for the current block
 *
 * \remark Nothing is returned. Instead, reconstructions (w/o in-loop filters)
 * will be updated in the pixel buffers in td->mb.e_mbd.
 */
static void encode_sb(const AV1_COMP *const cpi, ThreadData *td,
                      TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                      int mi_col, RUN_TYPE dry_run, BLOCK_SIZE bsize,
                      const PC_TREE *pc_tree, PARTITION_TREE *ptree,
                      const PARTITION_TREE *ptree_luma, int *rate) {
#else
/*!\brief Reconstructs a partition (may contain multiple coding blocks)
 *
 * \ingroup partition_search
 * Reconstructs a sub-partition of the superblock by applying the chosen modes
 * and partition trees stored in pc_tree.
 *
 * This function works on planes determined by get_partition_plane_start() and
 * get_partition_plane_end() based on xd->tree_type.
 *
 * \param[in]    cpi       Top-level encoder structure
 * \param[in]    td        Pointer to thread data
 * \param[in]    tile_data Pointer to struct holding adaptive
 *                         data/contexts/models for the tile during encoding
 * \param[in]    tp        Pointer to the starting token
 * \param[in]    mi_row    Row coordinate of the block in a step size of MI_SIZE
 * \param[in]    mi_col    Column coordinate of the block in a step size of
 *                         MI_SIZE
 * \param[in]    dry_run   A code indicating whether it is part of the final
 *                         pass for reconstructing the superblock
 * \param[in]    bsize     Current block size
 * \param[in]    pc_tree   Pointer to the PC_TREE node storing the picked
 *                         partitions and mode info for the current block
 * \param[in]    ptree     Pointer to the PARTITION_TREE node holding the
 *                         partition info for the current node and all of its
 *                         descendants.
 * \param[in]    rate      Pointer to the total rate for the current block
 *
 * \remark Nothing is returned. Instead, reconstructions (w/o in-loop filters)
 * will be updated in the pixel buffers in td->mb.e_mbd.
 */
static void encode_sb(const AV1_COMP *const cpi, ThreadData *td,
                      TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                      int mi_col, RUN_TYPE dry_run, BLOCK_SIZE bsize,
                      const PC_TREE *pc_tree, PARTITION_TREE *ptree,
                      int *rate) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
#else
  const int qbs_w = mi_size_wide[bsize] / 4;
  const int qbs_h = mi_size_high[bsize] / 4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  PARTITION_TREE *parent = ptree ? ptree->parent : NULL;
  const BLOCK_SIZE parent_bsize = parent ? parent->bsize : BLOCK_INVALID;
  const int is_partition_root = is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                                                   ,
                                                   parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
  );
  const int ctx = is_partition_root
                      ? partition_plane_context(xd, mi_row, mi_col, bsize)
                      : -1;
  const PARTITION_TYPE partition = pc_tree->partitioning;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
#if CONFIG_EXT_RECUR_PARTITIONS
  const bool disable_ext_part = !cm->seq_params.enable_ext_partitions;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if !CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize2 = get_partition_subsize(bsize, PARTITION_SPLIT);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  if (subsize == BLOCK_INVALID) return;

  if (!dry_run && ctx >= 0)
    update_partition_stats(xd,
#if CONFIG_ENTROPY_STATS
                           td->counts,
#endif  // CONFIG_ENTROPY_STATS
                           tile_data->allow_update_cdf, mi_params,
#if CONFIG_EXT_RECUR_PARTITIONS
                           disable_ext_part, ptree_luma,
                           &pc_tree->chroma_ref_info,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                           partition, mi_row, mi_col, bsize, ctx, cm->sb_size);

  PARTITION_TREE *sub_tree[4] = { NULL, NULL, NULL, NULL };
#if CONFIG_EXT_RECUR_PARTITIONS
  // If two pass partition tree is enable, then store the partition types in
  // ptree even if it's dry run.
  if (!dry_run || (cpi->sf.part_sf.two_pass_partition_search && ptree)) {
#else
  if (!dry_run) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    assert(ptree);

    ptree->partition = partition;
    ptree->bsize = bsize;
    ptree->mi_row = mi_row;
    ptree->mi_col = mi_col;
#if CONFIG_EXTENDED_SDP
    ptree->region_type = pc_tree->region_type;
    const int is_sb_root = bsize == cm->sb_size;
    ptree->extended_sdp_allowed_flag = pc_tree->extended_sdp_allowed_flag;
    if (!frame_is_intra_only(cm) && !is_sb_root &&
        partition != PARTITION_NONE && parent &&
        parent->region_type != INTRA_REGION && xd->tree_type != CHROMA_PART &&
        cm->seq_params.enable_sdp && ptree->extended_sdp_allowed_flag &&
        is_bsize_allowed_for_extended_sdp(bsize, partition)) {
      assert(xd->tree_type != CHROMA_PART);
      const int intra_region_ctx = get_intra_region_context(bsize);
      update_cdf(xd->tile_ctx->region_type_cdf[intra_region_ctx],
                 ptree->region_type, REGION_TYPES);
    }
#endif  // CONFIG_EXTENDED_SDP
    const int ss_x = xd->plane[1].subsampling_x;
    const int ss_y = xd->plane[1].subsampling_y;
    set_chroma_ref_info(
        xd->tree_type, mi_row, mi_col, ptree->index, bsize,
        &ptree->chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
        parent_bsize, parent ? parent->partition : PARTITION_NONE, ss_x, ss_y);

    switch (partition) {
#if CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_HORZ_4A:
      case PARTITION_HORZ_4B:
      case PARTITION_VERT_4A:
      case PARTITION_VERT_4B:
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_SPLIT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
        break;
#if CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_HORZ:
      case PARTITION_VERT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        break;
      case PARTITION_HORZ_3:
      case PARTITION_VERT_3:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
        break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      default: break;
    }
    for (int i = 0; i < 4; ++i) sub_tree[i] = ptree->sub_tree[i];
  }

#if CONFIG_EXT_RECUR_PARTITIONS
  const int track_ptree_luma =
      is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma, bsize);

  if (track_ptree_luma) {
    assert(partition ==
           sdp_chroma_part_from_luma(bsize, ptree_luma->partition,
                                     cm->seq_params.subsampling_x,
                                     cm->seq_params.subsampling_x));
    if (partition != PARTITION_NONE) {
      assert(ptree_luma);
      assert(ptree_luma->sub_tree);
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
  // encode both the luma and chroma blocks in one intra region
  int encode_sdp_intra_region_yuv = 0;
  if (!frame_is_intra_only(cm) && xd->tree_type == SHARED_PART &&
      pc_tree->region_type == INTRA_REGION) {
    encode_sdp_intra_region_yuv = 1;
    xd->tree_type = LUMA_PART;
  }
#endif  // CONFIG_EXTENDED_SDP
  switch (partition) {
    case PARTITION_NONE:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition,
#if CONFIG_EXTENDED_SDP
               pc_tree->none[pc_tree->region_type],
#else
               pc_tree->none,
#endif  // CONFIG_EXTENDED_SDP
               rate);
      break;
    case PARTITION_VERT:
#if CONFIG_EXT_RECUR_PARTITIONS
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->vertical[pc_tree->region_type][0],
#else
                pc_tree->vertical[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_col + hbs_w < cm->mi_params.mi_cols) {
        encode_sb(cpi, td, tile_data, tp, mi_row, mi_col + hbs_w, dry_run,
                  subsize,
#if CONFIG_EXTENDED_SDP
                  pc_tree->vertical[pc_tree->region_type][1],
#else
                  pc_tree->vertical[1],
#endif  // CONFIG_EXTENDED_SDP
                  sub_tree[1],
                  track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      }
#else   // CONFIG_EXT_RECUR_PARTITIONS
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->vertical[0], rate);
      if (mi_col + hbs_w < mi_params->mi_cols) {
        encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs_w, dry_run,
                 subsize, partition, pc_tree->vertical[1], rate);
      }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->horizontal[pc_tree->region_type][0],
#else
                pc_tree->horizontal[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_row + hbs_h < cm->mi_params.mi_rows) {
        encode_sb(cpi, td, tile_data, tp, mi_row + hbs_h, mi_col, dry_run,
                  subsize,
#if CONFIG_EXTENDED_SDP
                  pc_tree->horizontal[pc_tree->region_type][1],
#else
                  pc_tree->horizontal[1],
#endif  // CONFIG_EXTENDED_SDP
                  sub_tree[1],
                  track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      }
#else   // CONFIG_EXT_RECUR_PARTITIONS
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->horizontal[0], rate);
      if (mi_row + hbs_h < mi_params->mi_rows) {
        encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col, dry_run,
                 subsize, partition, pc_tree->horizontal[1], rate);
      }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->horizontal4a[pc_tree->region_type][0],
#else
                pc_tree->horizontal4a[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_row + ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row + ebs_h, mi_col, dry_run, bsize_med,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal4a[pc_tree->region_type][1],
#else
          pc_tree->horizontal4a[1],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[1], track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      if (mi_row + 3 * ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(cpi, td, tile_data, tp, mi_row + 3 * ebs_h, mi_col, dry_run,
                bsize_big,
#if CONFIG_EXTENDED_SDP
                pc_tree->horizontal4a[pc_tree->region_type][2],
#else
                pc_tree->horizontal4a[2],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[2], track_ptree_luma ? ptree_luma->sub_tree[2] : NULL,
                rate);
      if (mi_row + 7 * ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row + 7 * ebs_h, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal4a[pc_tree->region_type][3],
#else
          pc_tree->horizontal4a[3],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[3], track_ptree_luma ? ptree_luma->sub_tree[3] : NULL, rate);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->horizontal4b[pc_tree->region_type][0],
#else
                pc_tree->horizontal4b[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_row + ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row + ebs_h, mi_col, dry_run, bsize_big,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal4b[pc_tree->region_type][1],
#else
          pc_tree->horizontal4b[1],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[1], track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      if (mi_row + 5 * ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(cpi, td, tile_data, tp, mi_row + 5 * ebs_h, mi_col, dry_run,
                bsize_med,
#if CONFIG_EXTENDED_SDP
                pc_tree->horizontal4b[pc_tree->region_type][2],
#else
                pc_tree->horizontal4b[2],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[2], track_ptree_luma ? ptree_luma->sub_tree[2] : NULL,
                rate);
      if (mi_row + 7 * ebs_h >= cm->mi_params.mi_rows) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row + 7 * ebs_h, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal4b[pc_tree->region_type][3],
#else
          pc_tree->horizontal4b[3],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[3], track_ptree_luma ? ptree_luma->sub_tree[3] : NULL, rate);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->vertical4a[pc_tree->region_type][0],
#else
                pc_tree->vertical4a[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_col + ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row, mi_col + ebs_w, dry_run, bsize_med,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical4a[pc_tree->region_type][1],
#else
          pc_tree->vertical4a[1],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[1], track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      if (mi_col + 3 * ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col + 3 * ebs_w, dry_run,
                bsize_big,
#if CONFIG_EXTENDED_SDP
                pc_tree->vertical4a[pc_tree->region_type][2],
#else
                pc_tree->vertical4a[2],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[2], track_ptree_luma ? ptree_luma->sub_tree[2] : NULL,
                rate);
      if (mi_col + 7 * ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row, mi_col + 7 * ebs_w, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical4a[pc_tree->region_type][3],
#else
          pc_tree->vertical4a[3],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[3], track_ptree_luma ? ptree_luma->sub_tree[3] : NULL, rate);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->vertical4b[pc_tree->region_type][0],
#else
                pc_tree->vertical4b[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      if (mi_col + ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row, mi_col + ebs_w, dry_run, bsize_big,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical4b[pc_tree->region_type][1],
#else
          pc_tree->vertical4b[1],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[1], track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      if (mi_col + 5 * ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col + 5 * ebs_w, dry_run,
                bsize_med,
#if CONFIG_EXTENDED_SDP
                pc_tree->vertical4b[pc_tree->region_type][2],
#else
                pc_tree->vertical4b[2],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[2], track_ptree_luma ? ptree_luma->sub_tree[2] : NULL,
                rate);
      if (mi_col + 7 * ebs_w >= cm->mi_params.mi_cols) break;
      encode_sb(
          cpi, td, tile_data, tp, mi_row, mi_col + 7 * ebs_w, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical4b[pc_tree->region_type][3],
#else
          pc_tree->vertical4b[3],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[3], track_ptree_luma ? ptree_luma->sub_tree[3] : NULL, rate);
      break;
    }
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3: {
      for (int i = 0; i < 4; ++i) {
        const BLOCK_SIZE this_bsize =
            get_h_partition_subsize(bsize, i, partition);
        const int offset_r = get_h_partition_offset_mi_row(bsize, i, partition);
        const int offset_c = get_h_partition_offset_mi_col(bsize, i, partition);
        const int this_mi_row = mi_row + offset_r;
        const int this_mi_col = mi_col + offset_c;
        PC_TREE *this_pc_tree =
            partition == PARTITION_HORZ_3
#if CONFIG_EXTENDED_SDP
                ? pc_tree->horizontal3[pc_tree->region_type][i]
                : pc_tree->vertical3[pc_tree->region_type][i];
#else
                ? pc_tree->horizontal3[i]
                : pc_tree->vertical3[i];
#endif  // CONFIG_EXTENDED_SDP

        if (partition == PARTITION_HORZ_3) {
          if (this_mi_row >= cm->mi_params.mi_rows) break;
        } else {
          if (this_mi_col >= cm->mi_params.mi_cols) break;
        }
        encode_sb(cpi, td, tile_data, tp, this_mi_row, this_mi_col, dry_run,
                  this_bsize, this_pc_tree, sub_tree[i],
                  track_ptree_luma ? ptree_luma->sub_tree[i] : NULL, rate);
      }
      break;
    }
    case PARTITION_SPLIT:
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->split[pc_tree->region_type][0],
#else
                pc_tree->split[0],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[0], track_ptree_luma ? ptree_luma->sub_tree[0] : NULL,
                rate);
      encode_sb(
          cpi, td, tile_data, tp, mi_row, mi_col + hbs_w, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->split[pc_tree->region_type][1],
#else
          pc_tree->split[1],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[1], track_ptree_luma ? ptree_luma->sub_tree[1] : NULL, rate);
      encode_sb(
          cpi, td, tile_data, tp, mi_row + hbs_h, mi_col, dry_run, subsize,
#if CONFIG_EXTENDED_SDP
          pc_tree->split[pc_tree->region_type][2],
#else
          pc_tree->split[2],
#endif  // CONFIG_EXTENDED_SDP
          sub_tree[2], track_ptree_luma ? ptree_luma->sub_tree[2] : NULL, rate);
      encode_sb(cpi, td, tile_data, tp, mi_row + hbs_h, mi_col + hbs_w, dry_run,
                subsize,
#if CONFIG_EXTENDED_SDP
                pc_tree->split[pc_tree->region_type][3],
#else
                pc_tree->split[3],
#endif  // CONFIG_EXTENDED_SDP
                sub_tree[3], track_ptree_luma ? ptree_luma->sub_tree[3] : NULL,
                rate);
      break;
#else   // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT:
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, dry_run, subsize,
                pc_tree->split[0], sub_tree[0], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col + hbs_w, dry_run,
                subsize, pc_tree->split[1], sub_tree[1], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row + hbs_h, mi_col, dry_run,
                subsize, pc_tree->split[2], sub_tree[2], rate);
      encode_sb(cpi, td, tile_data, tp, mi_row + hbs_h, mi_col + hbs_w, dry_run,
                subsize, pc_tree->split[3], sub_tree[3], rate);
      break;
    case PARTITION_HORZ_A:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, bsize2,
               partition, pc_tree->horizontala[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs_w, dry_run, bsize2,
               partition, pc_tree->horizontala[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col, dry_run, subsize,
               partition, pc_tree->horizontala[2], rate);
      break;
    case PARTITION_HORZ_B:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->horizontalb[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col, dry_run, bsize2,
               partition, pc_tree->horizontalb[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col + hbs_w, dry_run,
               bsize2, partition, pc_tree->horizontalb[2], rate);
      break;
    case PARTITION_VERT_A:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, bsize2,
               partition, pc_tree->verticala[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col, dry_run, bsize2,
               partition, pc_tree->verticala[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs_w, dry_run, subsize,
               partition, pc_tree->verticala[2], rate);

      break;
    case PARTITION_VERT_B:
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, subsize,
               partition, pc_tree->verticalb[0], rate);
      encode_b(cpi, tile_data, td, tp, mi_row, mi_col + hbs_w, dry_run, bsize2,
               partition, pc_tree->verticalb[1], rate);
      encode_b(cpi, tile_data, td, tp, mi_row + hbs_h, mi_col + hbs_w, dry_run,
               bsize2, partition, pc_tree->verticalb[2], rate);
      break;
    case PARTITION_HORZ_4:
      for (int i = 0; i < SUB_PARTITIONS_PART4; ++i) {
        int this_mi_row = mi_row + i * qbs_h;
        if (i > 0 && this_mi_row >= mi_params->mi_rows) break;

        encode_b(cpi, tile_data, td, tp, this_mi_row, mi_col, dry_run, subsize,
                 partition, pc_tree->horizontal4[i], rate);
      }
      break;
    case PARTITION_VERT_4:
      for (int i = 0; i < SUB_PARTITIONS_PART4; ++i) {
        int this_mi_col = mi_col + i * qbs_w;
        if (i > 0 && this_mi_col >= mi_params->mi_cols) break;
        encode_b(cpi, tile_data, td, tp, mi_row, this_mi_col, dry_run, subsize,
                 partition, pc_tree->vertical4[i], rate);
      }
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0 && "Invalid partition type."); break;
  }

#if CONFIG_EXTENDED_SDP
  // encode the chroma blocks under one intra region in inter frame
  if (encode_sdp_intra_region_yuv && !cm->seq_params.monochrome) {
    xd->tree_type = CHROMA_PART;
    encode_b(cpi, tile_data, td, tp, mi_row, mi_col, dry_run, bsize,
             PARTITION_NONE, pc_tree->none_chroma, rate);
    xd->tree_type = SHARED_PART;
  }
#endif  // CONFIG_EXTENDED_SDP

  if (ptree) ptree->is_settled = 1;
  update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
}

#if CONFIG_EXT_RECUR_PARTITIONS
static void build_one_split_tree(AV1_COMMON *const cm, TREE_TYPE tree_type,
                                 int mi_row, int mi_col, BLOCK_SIZE bsize,
                                 BLOCK_SIZE final_bsize,
                                 PARTITION_TREE *ptree) {
  assert(block_size_high[bsize] == block_size_wide[bsize]);
  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;

  const int ss_x = cm->seq_params.subsampling_x;
  const int ss_y = cm->seq_params.subsampling_y;

  PARTITION_TREE *parent = ptree->parent;
  set_chroma_ref_info(tree_type, mi_row, mi_col, ptree->index, bsize,
                      &ptree->chroma_ref_info,
                      parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->bsize : BLOCK_INVALID,
                      parent ? parent->partition : PARTITION_NONE, ss_x, ss_y);

  if (bsize == BLOCK_4X4) {
    ptree->partition = PARTITION_NONE;
    return;
  }

  const CHROMA_REF_INFO *chroma_ref_info = &ptree->chroma_ref_info;

  // Handle boundary for first partition.
  PARTITION_TYPE implied_first_partition;
  const bool is_first_part_implied = is_partition_implied_at_boundary(
      &cm->mi_params, tree_type, ss_x, ss_y, mi_row, mi_col, bsize,
      chroma_ref_info, &implied_first_partition);

  if (!is_first_part_implied &&
      (block_size_wide[bsize] <= block_size_wide[final_bsize]) &&
      (block_size_high[bsize] <= block_size_high[final_bsize])) {
    ptree->partition = PARTITION_NONE;
    return;
  }

  // In general, we simulate SPLIT partition as HORZ followed by VERT partition.
  // But in case first partition is implied to be VERT, we are forced to use
  // VERT followed by HORZ.
  PARTITION_TYPE first_partition = PARTITION_INVALID;
  if (is_first_part_implied) {
    first_partition = implied_first_partition;
  } else if (check_is_chroma_size_valid(tree_type, PARTITION_HORZ, bsize,
                                        mi_row, mi_col, ss_x, ss_y,
                                        chroma_ref_info)) {
    first_partition = PARTITION_HORZ;
  } else if (check_is_chroma_size_valid(tree_type, PARTITION_VERT, bsize,
                                        mi_row, mi_col, ss_x, ss_y,
                                        chroma_ref_info)) {
    first_partition = PARTITION_VERT;
  }
  assert(first_partition != PARTITION_INVALID);

  const BLOCK_SIZE subsize = subsize_lookup[PARTITION_SPLIT][bsize];
  const int hbs_w = mi_size_wide[bsize] >> 1;
  const int hbs_h = mi_size_high[bsize] >> 1;

  ptree->partition = first_partition;

  if (first_partition == PARTITION_SPLIT) {
    ptree->partition = first_partition;
    ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
    ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
    ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
    ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
    build_one_split_tree(cm, tree_type, mi_row, mi_col, subsize, final_bsize,
                         ptree->sub_tree[0]);
    build_one_split_tree(cm, tree_type, mi_row, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[1]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col, subsize,
                         final_bsize, ptree->sub_tree[2]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[3]);
    return;
  }

  ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
  ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);

  const PARTITION_TYPE second_partition =
      (first_partition == PARTITION_HORZ) ? PARTITION_VERT : PARTITION_HORZ;

#ifndef NDEBUG
  // Boundary sanity checks for 2nd partitions.
  {
    PARTITION_TYPE implied_second_first_partition;
    const bool is_second_first_part_implied = is_partition_implied_at_boundary(
        &cm->mi_params, tree_type, ss_x, ss_y, mi_row, mi_col,
        subsize_lookup[first_partition][bsize],
        &ptree->sub_tree[0]->chroma_ref_info, &implied_second_first_partition);
    assert(IMPLIES(is_second_first_part_implied,
                   implied_second_first_partition == second_partition));
  }

  {
    const int mi_row_second_second =
        (second_partition == PARTITION_HORZ) ? mi_row + hbs_h : mi_row;
    const int mi_col_second_second =
        (second_partition == PARTITION_VERT) ? mi_col + hbs_w : mi_col;
    PARTITION_TYPE implied_second_second_partition;
    const bool is_second_second_part_implied = is_partition_implied_at_boundary(
        &cm->mi_params, tree_type, ss_x, ss_y, mi_row_second_second,
        mi_col_second_second, subsize_lookup[first_partition][bsize],
        &ptree->sub_tree[0]->chroma_ref_info, &implied_second_second_partition);
    assert(IMPLIES(is_second_second_part_implied,
                   implied_second_second_partition == second_partition));
  }
#endif  // NDEBUG

  ptree->sub_tree[0]->partition = second_partition;
  ptree->sub_tree[0]->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
  ptree->sub_tree[0]->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);

  ptree->sub_tree[1]->partition = second_partition;
  ptree->sub_tree[1]->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
  ptree->sub_tree[1]->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);

  if (first_partition == PARTITION_HORZ) {
    assert(second_partition == PARTITION_VERT);
    build_one_split_tree(cm, tree_type, mi_row, mi_col, subsize, final_bsize,
                         ptree->sub_tree[0]->sub_tree[0]);
    build_one_split_tree(cm, tree_type, mi_row, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[0]->sub_tree[1]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col, subsize,
                         final_bsize, ptree->sub_tree[1]->sub_tree[0]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[1]->sub_tree[1]);
  } else {
    assert(first_partition == PARTITION_VERT);
    assert(second_partition == PARTITION_HORZ);
    build_one_split_tree(cm, tree_type, mi_row, mi_col, subsize, final_bsize,
                         ptree->sub_tree[0]->sub_tree[0]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col, subsize,
                         final_bsize, ptree->sub_tree[0]->sub_tree[1]);
    build_one_split_tree(cm, tree_type, mi_row, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[1]->sub_tree[0]);
    build_one_split_tree(cm, tree_type, mi_row + hbs_h, mi_col + hbs_w, subsize,
                         final_bsize, ptree->sub_tree[1]->sub_tree[1]);
  }
}

void av1_build_partition_tree_fixed_partitioning(AV1_COMMON *const cm,
                                                 TREE_TYPE tree_type,
                                                 int mi_row, int mi_col,
                                                 BLOCK_SIZE bsize,
                                                 PARTITION_TREE *ptree) {
  const BLOCK_SIZE sb_size = cm->sb_size;

  build_one_split_tree(cm, tree_type, mi_row, mi_col, sb_size, bsize, ptree);
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

static PARTITION_TYPE get_preset_partition(const AV1_COMMON *cm,
                                           TREE_TYPE tree_type, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize,
                                           PARTITION_TREE *ptree) {
  if (ptree) {
#ifndef NDEBUG
#if CONFIG_EXT_RECUR_PARTITIONS
    const bool ss_x = cm->cur_frame->buf.subsampling_x;
    const bool ss_y = cm->cur_frame->buf.subsampling_y;
    const PARTITION_TYPE derived_partition =
        av1_get_normative_forced_partition_type(
            &cm->mi_params, tree_type, ss_x, ss_y, mi_row, mi_col, bsize,
#if CONFIG_CB1TO4_SPLIT
            ptree->parent ? ptree->parent->bsize : BLOCK_INVALID,
#endif  // CONFIG_CB1TO4_SPLIT
            /* ptree_luma= */ NULL, &ptree->chroma_ref_info);
    assert(IMPLIES(derived_partition != PARTITION_INVALID,
                   ptree->partition == derived_partition));
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#endif  // NDEBUG
    return ptree->partition;
  }
  if (bsize >= BLOCK_8X8) {
    const int plane_type = (tree_type == CHROMA_PART);
    return get_partition(cm, plane_type, mi_row, mi_col, bsize);
  }
  return PARTITION_NONE;
}

/*!\brief AV1 block partition search (partition estimation and partial search).
*
* \ingroup partition_search
* Encode the block by applying pre-calculated partition patterns that are
* represented by coding block sizes stored in the mbmi array. Minor partition
* adjustments are tested and applied if they lead to lower rd costs. The
* partition types are limited to a basic set: none, horz, vert, and split.
*
* \param[in]    cpi       Top-level encoder structure
* \param[in]    td        Pointer to thread data
* \param[in]    tile_data Pointer to struct holding adaptive
data/contexts/models for the tile during encoding
* \param[in]    mib       Array representing MB_MODE_INFO pointers for mi
blocks starting from the first pixel of the current
block
* \param[in]    tp        Pointer to the starting token
* \param[in]    mi_row    Row coordinate of the block in a step size of
MI_SIZE
* \param[in]    mi_col    Column coordinate of the block in a step size of
MI_SIZE
* \param[in]    bsize     Current block size
* \param[in]    rate      Pointer to the final rate for encoding the current
block
* \param[in]    dist      Pointer to the final distortion of the current block
* \param[in]    do_recon  Whether the reconstruction function needs to be run,
either for finalizing a superblock or providing
reference for future sub-partitions
* \param[in]    ptree     Pointer to the PARTITION_TREE node holding the
pre-calculated partition tree (if any) for the current block
* \param[in]    pc_tree   Pointer to the PC_TREE node holding the picked
partitions and mode info for the current block
*
* Nothing is returned. The pc_tree struct is modified to store the
* picked partition and modes. The rate and dist are also updated with those
* corresponding to the best partition found.
*/
void av1_rd_use_partition(AV1_COMP *cpi, ThreadData *td, TileDataEnc *tile_data,
                          MB_MODE_INFO **mib, TokenExtra **tp, int mi_row,
                          int mi_col, BLOCK_SIZE bsize, int *rate,
                          int64_t *dist, int do_recon, PARTITION_TREE *ptree,
                          PC_TREE *pc_tree) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;
  const ModeCosts *mode_costs = &x->mode_costs;
  assert(bsize < BLOCK_SIZES_ALL);
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int hbh = mi_size_high[bsize] / 2;
  const int hbw = mi_size_wide[bsize] / 2;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  const int pl = (bsize >= BLOCK_8X8)
                     ? partition_plane_context(xd, mi_row, mi_col, bsize)
                     : 0;
  const int plane_type = (xd->tree_type == CHROMA_PART);
  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end = get_partition_plane_end(xd->tree_type, num_planes);
  const PARTITION_TYPE partition =
      get_preset_partition(cm, plane_type, mi_row, mi_col, bsize, ptree);
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  RD_STATS last_part_rdc, invalid_rdc;

#if CONFIG_EXTENDED_SDP
  if (!frame_is_intra_only(cm))
    pc_tree->region_type = MIXED_INTER_INTRA_REGION;
  else
    pc_tree->region_type = INTRA_REGION;
  REGION_TYPE cur_region_type = pc_tree->region_type;
  if (is_inter_sdp_chroma(cm, cur_region_type, x->e_mbd.tree_type)) {
    if (pc_tree->none_chroma == NULL) {
      pc_tree->none_chroma =
          av1_alloc_pmc(cm, xd->tree_type, mi_row, mi_col, bsize, pc_tree,
                        PARTITION_NONE, 0, ss_x, ss_y, &td->shared_coeff_buf);
    }
  } else {
    if (pc_tree->none[cur_region_type] == NULL) {
      pc_tree->none[cur_region_type] =
          av1_alloc_pmc(cm, xd->tree_type, mi_row, mi_col, bsize, pc_tree,
                        PARTITION_NONE, 0, ss_x, ss_y, &td->shared_coeff_buf);
    }
  }
#else
  if (pc_tree->none == NULL) {
    pc_tree->none =
        av1_alloc_pmc(cm, xd->tree_type, mi_row, mi_col, bsize, pc_tree,
                      PARTITION_NONE, 0, ss_x, ss_y, &td->shared_coeff_buf);
  }
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXTENDED_SDP
  PICK_MODE_CONTEXT *ctx_none =
      is_inter_sdp_chroma(cm, cur_region_type, x->e_mbd.tree_type)
          ? pc_tree->none_chroma
          : pc_tree->none[pc_tree->region_type];
#else
  PICK_MODE_CONTEXT *ctx_none = pc_tree->none;
#endif  // CONFIG_EXTENDED_SDP

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  av1_invalid_rd_stats(&last_part_rdc);
  av1_invalid_rd_stats(&invalid_rdc);

  pc_tree->partitioning = partition;

#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  if (bsize == BLOCK_16X16 && cpi->vaq_refresh) {
    av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize,
                    &pc_tree->chroma_ref_info);
    x->mb_energy = av1_log_block_var(cpi, x, bsize);
  }

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);

#if !CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE split_subsize =
      get_partition_subsize(bsize, PARTITION_SPLIT);
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    int x_idx = (i & 1) * hbs;
    int y_idx = (i >> 1) * hbs;
    pc_tree->split[i] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row + y_idx, mi_col + x_idx, split_subsize, pc_tree,
        PARTITION_SPLIT, i, i == 3, ss_x, ss_y);
  }
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  switch (partition) {
    case PARTITION_NONE:
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_NONE,
#if CONFIG_EXTENDED_SDP
                    pc_tree->region_type,
#endif  // CONFIG_EXTENDED_SDP
                    bsize, ctx_none, invalid_rdc);
      break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
      pc_tree->horizontal[cur_region_type][0] =
#else
      pc_tree->horizontal[0] =
#endif  // CONFIG_EXTENDED_SDP
          av1_alloc_pc_tree_node(xd->tree_type, mi_row, mi_col, subsize,
                                 pc_tree, PARTITION_HORZ, 0, 0, ss_x, ss_y);
#if CONFIG_EXTENDED_SDP
      pc_tree->horizontal[cur_region_type][1] =
#else
      pc_tree->horizontal[1] =
#endif  // CONFIG_EXTENDED_SDP
          av1_alloc_pc_tree_node(xd->tree_type, mi_row + hbh, mi_col, subsize,
                                 pc_tree, PARTITION_HORZ, 1, 1, ss_x, ss_y);
      av1_rd_use_partition(cpi, td, tile_data, mib, tp, mi_row, mi_col, subsize,
                           &last_part_rdc.rate, &last_part_rdc.dist, 1,
                           ptree ? ptree->sub_tree[0] : NULL,
#if CONFIG_EXTENDED_SDP
                           pc_tree->horizontal[cur_region_type][0]);
#else
                           pc_tree->horizontal[0]);
#endif  // CONFIG_EXTENDED_SDP
#else   // CONFIG_EXT_RECUR_PARTITIONS
      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        if (pc_tree->horizontal[i] == NULL) {
          pc_tree->horizontal[i] = av1_alloc_pmc(
              cm, xd->tree_type, mi_row + hbs * i, mi_col, subsize, pc_tree,
              PARTITION_HORZ, i, ss_x, ss_y, &td->shared_coeff_buf);
        }
      }
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_HORZ, subsize, pc_tree->horizontal[0],
                    invalid_rdc);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      if (last_part_rdc.rate != INT_MAX && bsize >= BLOCK_8X8 &&
          mi_row + hbs < mi_params->mi_rows) {
        RD_STATS tmp_rdc;
        av1_init_rd_stats(&tmp_rdc);
#if CONFIG_EXT_RECUR_PARTITIONS
        av1_rd_use_partition(cpi, td, tile_data,
                             mib + hbh * mi_params->mi_stride, tp, mi_row + hbh,
                             mi_col, subsize, &tmp_rdc.rate, &tmp_rdc.dist, 0,
                             ptree ? ptree->sub_tree[1] : NULL,
#if CONFIG_EXTENDED_SDP
                             pc_tree->horizontal[cur_region_type][1]);
#else
                             pc_tree->horizontal[1]);
#endif  // CONFIG_EXTENDED_SDP
#else   // CONFIG_EXT_RECUR_PARTITIONS
        const PICK_MODE_CONTEXT *const ctx_h = pc_tree->horizontal[0];
        av1_update_state(cpi, td, ctx_h, mi_row, mi_col, subsize, 1);
        encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize,
                          plane_start, plane_end, NULL);
        pick_sb_modes(cpi, tile_data, x, mi_row + hbs, mi_col, &tmp_rdc,
                      PARTITION_HORZ, subsize, pc_tree->horizontal[1],
                      invalid_rdc);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
    case PARTITION_VERT:
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
      pc_tree->vertical[cur_region_type][0] =
#else
      pc_tree->vertical[0] =
#endif  // CONFIG_EXTENDED_SDP
          av1_alloc_pc_tree_node(xd->tree_type, mi_row, mi_col, subsize,
                                 pc_tree, PARTITION_VERT, 0, 0, ss_x, ss_y);
#if CONFIG_EXTENDED_SDP
      pc_tree->vertical[cur_region_type][1] =
#else
      pc_tree->vertical[1] =
#endif  // CONFIG_EXTENDED_SDP
          av1_alloc_pc_tree_node(xd->tree_type, mi_row, mi_col + hbw, subsize,
                                 pc_tree, PARTITION_VERT, 1, 1, ss_x, ss_y);
      av1_rd_use_partition(cpi, td, tile_data, mib, tp, mi_row, mi_col, subsize,
                           &last_part_rdc.rate, &last_part_rdc.dist, 1,
                           ptree ? ptree->sub_tree[0] : NULL,
#if CONFIG_EXTENDED_SDP
                           pc_tree->vertical[cur_region_type][0]);
#else
                           pc_tree->vertical[0]);
#endif  // CONFIG_EXTENDED_SDP
#else   // CONFIG_EXT_RECUR_PARTITIONS
      for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
        if (pc_tree->vertical[i] == NULL) {
          pc_tree->vertical[i] = av1_alloc_pmc(
              cm, xd->tree_type, mi_row, mi_col + hbs * i, subsize, pc_tree,
              PARTITION_VERT, i, ss_x, ss_y, &td->shared_coeff_buf);
        }
      }
      pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                    PARTITION_VERT, subsize, pc_tree->vertical[0], invalid_rdc);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      if (last_part_rdc.rate != INT_MAX && bsize >= BLOCK_8X8 &&
          mi_col + hbs < mi_params->mi_cols) {
        RD_STATS tmp_rdc;
        av1_init_rd_stats(&tmp_rdc);
#if CONFIG_EXT_RECUR_PARTITIONS
        av1_rd_use_partition(
            cpi, td, tile_data, mib + hbw, tp, mi_row, mi_col + hbw, subsize,
            &tmp_rdc.rate, &tmp_rdc.dist, 0, ptree ? ptree->sub_tree[1] : NULL,
#if CONFIG_EXTENDED_SDP
            pc_tree->vertical[cur_region_type][1]);
#else
            pc_tree->vertical[1]);
#endif  // CONFIG_EXTENDED_SDP
#else   // CONFIG_EXT_RECUR_PARTITIONS
        const PICK_MODE_CONTEXT *const ctx_v = pc_tree->vertical[0];
        av1_update_state(cpi, td, ctx_v, mi_row, mi_col, subsize, 1);
        encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize,
                          plane_start, plane_end, NULL);
        pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + hbs, &tmp_rdc,
                      PARTITION_VERT, subsize,
                      pc_tree->vertical[bsize > BLOCK_8X8], invalid_rdc);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT:
      last_part_rdc.rate = 0;
      last_part_rdc.dist = 0;
      last_part_rdc.rdcost = 0;
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
        int x_idx = (i & 1) * hbs;
        int y_idx = (i >> 1) * hbs;
        int jj = i >> 1, ii = i & 0x01;
        RD_STATS tmp_rdc;
        if ((mi_row + y_idx >= mi_params->mi_rows) ||
            (mi_col + x_idx >= mi_params->mi_cols))
          continue;
#if CONFIG_EXTENDED_SDP
        pc_tree->split[cur_region_type][i]
#else
        pc_tree->split[i]
#endif  // CONFIG_EXTENDED_SDP
            = av1_alloc_pc_tree_node(xd->tree_type, mi_row + y_idx,
                                     mi_col + x_idx, subsize, pc_tree,
                                     PARTITION_SPLIT, i, i == 3, ss_x, ss_y);

        av1_init_rd_stats(&tmp_rdc);
        av1_rd_use_partition(cpi, td, tile_data,
                             mib + jj * hbs * mi_params->mi_stride + ii * hbs,
                             tp, mi_row + y_idx, mi_col + x_idx, subsize,
                             &tmp_rdc.rate, &tmp_rdc.dist,
                             i != (SUB_PARTITIONS_SPLIT - 1),
                             ptree ? ptree->sub_tree[i] : NULL,
#if CONFIG_EXTENDED_SDP
                             pc_tree->split[cur_region_type][i]);
#else
                             pc_tree->split[i]);
#endif  // CONFIG_EXTENDED_SDP
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
      }
      break;
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B:
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3:
#else   // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT:
      last_part_rdc.rate = 0;
      last_part_rdc.dist = 0;
      last_part_rdc.rdcost = 0;
      for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
        int x_idx = (i & 1) * hbs;
        int y_idx = (i >> 1) * hbs;
        int jj = i >> 1, ii = i & 0x01;
        RD_STATS tmp_rdc;
        if ((mi_row + y_idx >= mi_params->mi_rows) ||
            (mi_col + x_idx >= mi_params->mi_cols))
          continue;

        av1_init_rd_stats(&tmp_rdc);
        av1_rd_use_partition(cpi, td, tile_data,
                             mib + jj * hbs * mi_params->mi_stride + ii * hbs,
                             tp, mi_row + y_idx, mi_col + x_idx, subsize,
                             &tmp_rdc.rate, &tmp_rdc.dist,
                             i != (SUB_PARTITIONS_SPLIT - 1), NULL,
                             pc_tree->split[i]);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          av1_invalid_rd_stats(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
      }
      break;
    case PARTITION_VERT_A:
    case PARTITION_VERT_B:
    case PARTITION_HORZ_A:
    case PARTITION_HORZ_B:
    case PARTITION_HORZ_4:
    case PARTITION_VERT_4:
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      assert(0 && "Cannot handle extended partition types");
    default: assert(0); break;
  }

  if (last_part_rdc.rate < INT_MAX) {
    last_part_rdc.rate +=
        mode_costs->partition_cost[xd->tree_type == CHROMA_PART][pl][partition];
    last_part_rdc.rdcost =
        RDCOST(x->rdmult, last_part_rdc.rate, last_part_rdc.dist);
  }

  // If last_part is better set the partitioning to that.
  mib[0]->sb_type[plane_type] = bsize;
  if (bsize >= BLOCK_8X8) pc_tree->partitioning = partition;

  av1_restore_context(cm, x, &x_ctx, mi_row, mi_col, bsize, num_planes);

  // We must have chosen a partitioning and encoding or we'll fail later on.
  // No other opportunities for success.
  if (bsize == cm->sb_size)
    assert(last_part_rdc.rate < INT_MAX && last_part_rdc.dist < INT64_MAX);

  if (do_recon) {
    if (bsize == cm->sb_size) {
      // NOTE: To get estimate for rate due to the tokens, use:
      // int rate_coeffs = 0;
      // encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_COSTCOEFFS,
      //           bsize, pc_tree, &rate_coeffs);
      for (int plane = plane_start; plane < plane_end; plane++) {
        x->cb_offset[plane] = 0;
      }
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, OUTPUT_ENABLED, bsize,
                pc_tree, xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                NULL);
    } else {
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                pc_tree, NULL,
#if CONFIG_EXT_RECUR_PARTITIONS
                NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                NULL);
    }
  }

  *rate = last_part_rdc.rate;
  *dist = last_part_rdc.dist;
  x->rdmult = orig_rdmult;
}
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
/*! \brief Contains level banks used for rdopt.*/
typedef struct LevelBanksRDO {
#if CONFIG_MVP_IMPROVEMENT
  //! The current level bank, used to restore the level bank in MACROBLOCKD.
  REF_MV_BANK curr_level_bank;
  //! The best level bank from the rdopt process.
  REF_MV_BANK best_level_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  //! The current warp, level bank, used to restore the warp level bank in
  //! MACROBLOCKD.
  WARP_PARAM_BANK curr_level_warp_bank;
  //! The best warp level bank from the rdopt process.
  WARP_PARAM_BANK best_level_warp_bank;
#endif  // WARP_CU_BANK
} LevelBanksRDO;

static AOM_INLINE void update_best_level_banks(LevelBanksRDO *level_banks,
                                               const MACROBLOCKD *xd) {
#if CONFIG_MVP_IMPROVEMENT
  level_banks->best_level_bank = xd->ref_mv_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  level_banks->best_level_warp_bank = xd->warp_param_bank;
#endif  // WARP_CU_BANK
}

static AOM_INLINE void restore_level_banks(MACROBLOCKD *xd,
                                           const LevelBanksRDO *level_banks) {
#if CONFIG_MVP_IMPROVEMENT
  xd->ref_mv_bank = level_banks->curr_level_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  xd->warp_param_bank = level_banks->curr_level_warp_bank;
#endif  // WARP_CU_BANK
}
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK

#if !CONFIG_EXT_RECUR_PARTITIONS
// Try searching for an encoding for the given subblock. Returns zero if the
// rdcost is already too high (to tell the caller not to bother searching for
// encodings of further subblocks).
static int rd_try_subblock(AV1_COMP *const cpi, ThreadData *td,
                           TileDataEnc *tile_data, TokenExtra **tp, int is_last,
                           int mi_row, int mi_col, BLOCK_SIZE subsize,
                           RD_STATS best_rdcost, RD_STATS *sum_rdc,
                           PARTITION_TYPE partition,
                           PICK_MODE_CONTEXT *this_ctx) {
  MACROBLOCK *const x = &td->mb;
  const int orig_mult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, subsize, NO_AQ, NULL);

  av1_rd_cost_update(x->rdmult, &best_rdcost);

  RD_STATS rdcost_remaining;
  av1_rd_stats_subtraction(x->rdmult, &best_rdcost, sum_rdc, &rdcost_remaining);
  RD_STATS this_rdc;
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &this_rdc, partition,
#if CONFIG_EXTENDED_SDP
                pc_tree->region_type,
#endif  // CONFIG_EXTENDED_SDP
                subsize, this_ctx, rdcost_remaining);

  if (this_rdc.rate == INT_MAX) {
    sum_rdc->rdcost = INT64_MAX;
  } else {
    sum_rdc->rate += this_rdc.rate;
    sum_rdc->dist += this_rdc.dist;
    av1_rd_cost_update(x->rdmult, sum_rdc);
  }

  if (sum_rdc->rdcost >= best_rdcost.rdcost) {
    x->rdmult = orig_mult;
    return 0;
  }

  MACROBLOCKD *xd = &x->e_mbd;
  const AV1_COMMON *const cm = &cpi->common;
  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));

  if (!is_last) {
    av1_update_state(cpi, td, this_ctx, mi_row, mi_col, subsize, 1);
    encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL, subsize,
                      plane_start, plane_end, NULL);
  }

  x->rdmult = orig_mult;
  return 1;
}

// Tests an AB partition, and updates the encoder status, the pick mode
// contexts, the best rdcost, and the best partition.
static bool rd_test_partition3(AV1_COMP *const cpi, ThreadData *td,
                               TileDataEnc *tile_data, TokenExtra **tp,
                               PC_TREE *pc_tree, RD_STATS *best_rdc,
                               PICK_MODE_CONTEXT *ctxs[SUB_PARTITIONS_AB],
                               int mi_row, int mi_col, BLOCK_SIZE bsize,
                               PARTITION_TYPE partition,
                               const BLOCK_SIZE ab_subsize[SUB_PARTITIONS_AB],
                               const int ab_mi_pos[SUB_PARTITIONS_AB][2]
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                               ,
                               LevelBanksRDO *level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
) {
  const MACROBLOCK *const x = &td->mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  sum_rdc.rate =
      x->mode_costs.partition_cost[xd->tree_type == CHROMA_PART][pl][partition];
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);
  // Loop over sub-partitions in AB partition type.
  for (int i = 0; i < SUB_PARTITIONS_AB; i++) {
    assert(ab_subsize[i] != BLOCK_INVALID);
    if (!rd_try_subblock(cpi, td, tile_data, tp, i == SUB_PARTITIONS_AB - 1,
                         ab_mi_pos[i][0], ab_mi_pos[i][1], ab_subsize[i],
                         *best_rdc, &sum_rdc, partition, ctxs[i]))
      return false;
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost >= best_rdc->rdcost) return false;
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, sum_rdc.dist);
  if (sum_rdc.rdcost >= best_rdc->rdcost) return false;

  *best_rdc = sum_rdc;
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  pc_tree->partitioning = partition;
#if CONFIG_EXTENDED_SDP
  if (!frame_is_intra_only(cm))
    pc_tree->region_type = MIXED_INTER_INTRA_REGION;
  else
    pc_tree->region_type = INTRA_REGION;
#endif  // CONFIG_EXTENDED_SDP
  return true;
}
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXT_RECUR_PARTITIONS
static AOM_INLINE PARTITION_TYPE get_forced_partition_type(
    const AV1_COMMON *const cm, MACROBLOCK *x, int mi_row, int mi_col,
    BLOCK_SIZE bsize,
#if CONFIG_CB1TO4_SPLIT
    BLOCK_SIZE parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
    const PARTITION_TREE *ptree_luma, const PARTITION_TREE *template_tree,
#if CONFIG_EXTENDED_SDP
    REGION_TYPE cur_region_type,
#endif  // CONFIG_EXTENDED_SDP
    const CHROMA_REF_INFO *chroma_ref_info) {
  // Partition types forced by bitstream syntax.
  const MACROBLOCKD *xd = &x->e_mbd;
  const bool ss_x = cm->seq_params.subsampling_x;
  const bool ss_y = cm->seq_params.subsampling_y;
  const PARTITION_TYPE derived_partition =
      av1_get_normative_forced_partition_type(&cm->mi_params, xd->tree_type,
                                              ss_x, ss_y, mi_row, mi_col, bsize,
#if CONFIG_CB1TO4_SPLIT
                                              parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
                                              ptree_luma, chroma_ref_info);
  if (derived_partition != PARTITION_INVALID) {
    return derived_partition;
  }

  // Partition types forced by speed_features.
  if (template_tree) {
    return template_tree->partition;
  }

  if (should_reuse_mode(x, REUSE_PARTITION_MODE_FLAG)
#if CONFIG_CB1TO4_SPLIT
      && (parent_bsize == BLOCK_INVALID || parent_bsize <= BLOCK_LARGEST)
#endif  // CONFIG_CB1TO4_SPLIT
#if CONFIG_EXTENDED_SDP
      && !is_inter_sdp_chroma(cm, cur_region_type, xd->tree_type)
#endif  // CONFIG_EXTENDED_SDP
  ) {
    return av1_get_prev_partition(x, mi_row, mi_col, bsize, cm->sb_size);
  }
  return PARTITION_INVALID;
}

static AOM_INLINE void init_allowed_partitions(
    PartitionSearchState *part_search_state, const PartitionCfg *part_cfg,
    const CHROMA_REF_INFO *chroma_ref_info, TREE_TYPE tree_type) {
  const PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const int mi_row = blk_params->mi_row;
  const int mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;
  const bool has_rows = blk_params->has_rows;
  const bool has_cols = blk_params->has_cols;
  const bool ss_x = part_search_state->ss_x;
  const bool ss_y = part_search_state->ss_y;

  part_search_state->do_rectangular_split = part_cfg->enable_rect_partitions;

  const BLOCK_SIZE horz_subsize = get_partition_subsize(bsize, PARTITION_HORZ);
  const BLOCK_SIZE vert_subsize = get_partition_subsize(bsize, PARTITION_VERT);
  const int is_horz_size_valid =
      is_partition_valid(bsize, PARTITION_HORZ) &&
      check_is_chroma_size_valid(tree_type, PARTITION_HORZ, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info);

  const int is_vert_size_valid =
      is_partition_valid(bsize, PARTITION_VERT) &&
      check_is_chroma_size_valid(tree_type, PARTITION_VERT, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info);

  // Initialize allowed partition types for the partition block.
  part_search_state->is_block_splittable =
      is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                         ,
                         blk_params->parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
      );
  part_search_state->partition_none_allowed =
      (tree_type == CHROMA_PART && bsize == BLOCK_8X8) ||
      (has_rows && has_cols &&
       is_bsize_geq(blk_params->bsize, blk_params->min_partition_size));
  part_search_state->partition_rect_allowed[HORZ] =
      part_search_state->is_block_splittable &&
      part_cfg->enable_rect_partitions &&
      is_bsize_geq(horz_subsize, blk_params->min_partition_size) &&
      is_horz_size_valid;
  part_search_state->partition_rect_allowed[VERT] =
      part_search_state->is_block_splittable &&
      part_cfg->enable_rect_partitions &&
      is_bsize_geq(vert_subsize, blk_params->min_partition_size) &&
      is_vert_size_valid;

  const int ext_partition_allowed =
      part_search_state->is_block_splittable &&
      part_cfg->enable_ext_partitions &&
      is_ext_partition_allowed_at_bsize(bsize, tree_type);

  part_search_state->partition_3_allowed[HORZ] =
      ext_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_HORZ_3) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_HORZ_3, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_HORZ_3),
                   blk_params->min_partition_size)
#if CONFIG_CB1TO4_SPLIT
      && is_bsize_geq(get_h_partition_subsize(bsize, 1, PARTITION_HORZ_3),
                      blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_HORZ_3, ss_x, ss_y),
              blk_params->has_3_4th_rows)
#endif  // CONFIG_CB1TO4_SPLIT
      ;

  part_search_state->partition_3_allowed[VERT] =
      ext_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_VERT_3) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_VERT_3, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_VERT_3),
                   blk_params->min_partition_size)
#if CONFIG_CB1TO4_SPLIT
      && is_bsize_geq(get_h_partition_subsize(bsize, 1, PARTITION_VERT_3),
                      blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_VERT_3, ss_x, ss_y),
              blk_params->has_3_4th_cols)
#endif  // CONFIG_CB1TO4_SPLIT
      ;

  const int uneven_4way_partition_allowed =
      part_search_state->is_block_splittable &&
      part_cfg->enable_ext_partitions &&
      is_uneven_4way_partition_allowed_at_bsize(bsize, tree_type);
  part_search_state->partition_4a_allowed[HORZ] =
      uneven_4way_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_HORZ_4A) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_HORZ_4A, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_HORZ_4A),
                   blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_HORZ_4A, ss_x, ss_y),
              blk_params->has_7_8th_rows);

  part_search_state->partition_4b_allowed[HORZ] =
      uneven_4way_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_HORZ_4B) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_HORZ_4B, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_HORZ_4B),
                   blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_HORZ_4B, ss_x, ss_y),
              blk_params->has_7_8th_rows);

  part_search_state->partition_4a_allowed[VERT] =
      uneven_4way_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_VERT_4A) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_VERT_4A, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_VERT_4A),
                   blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_VERT_4A, ss_x, ss_y),
              blk_params->has_7_8th_cols);

  part_search_state->partition_4b_allowed[VERT] =
      uneven_4way_partition_allowed &&
      get_partition_subsize(bsize, PARTITION_VERT_4B) != BLOCK_INVALID &&
      check_is_chroma_size_valid(tree_type, PARTITION_VERT_4B, bsize, mi_row,
                                 mi_col, ss_x, ss_y, chroma_ref_info) &&
      is_bsize_geq(get_partition_subsize(bsize, PARTITION_VERT_4B),
                   blk_params->min_partition_size) &&
      IMPLIES(have_nz_chroma_ref_offset(bsize, PARTITION_VERT_4B, ss_x, ss_y),
              blk_params->has_7_8th_cols);

  // Reset the flag indicating whether a partition leading to a rdcost lower
  // than the bound best_rdc has been found.
  part_search_state->found_best_partition = false;
}

static const int kZeroPartitionCosts[ALL_PARTITION_TYPES];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
// Initialize state variables of partition search used in
// av1_rd_pick_partition().
static void init_partition_search_state_params(
    MACROBLOCK *x, AV1_COMP *const cpi, PartitionSearchState *part_search_state,
#if CONFIG_EXT_RECUR_PARTITIONS
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, int max_recursion_depth,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    int mi_row, int mi_col, BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  const TREE_TYPE tree_type = xd->tree_type;

  assert(bsize < BLOCK_SIZES_ALL);

  // Initialization of block size related parameters.
  blk_params->mi_step = mi_size_wide[bsize] / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  blk_params->mi_step_h = mi_size_high[bsize] / 2;
  blk_params->mi_step_w = mi_size_wide[bsize] / 2;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  blk_params->mi_row = mi_row;
  blk_params->mi_col = mi_col;
#if CONFIG_EXT_RECUR_PARTITIONS
  blk_params->mi_row_edge = mi_row + blk_params->mi_step_h;
  blk_params->mi_col_edge = mi_col + blk_params->mi_step_w;
#else   // CONFIG_EXT_RECUR_PARTITIONS
  blk_params->mi_row_edge = mi_row + blk_params->mi_step;
  blk_params->mi_col_edge = mi_col + blk_params->mi_step;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  blk_params->width = block_size_wide[bsize];
#if CONFIG_EXT_RECUR_PARTITIONS
  blk_params->min_partition_size = x->sb_enc.min_partition_size;
#else
  blk_params->min_partition_size_1d =
      block_size_wide[x->sb_enc.min_partition_size];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  blk_params->subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  blk_params->split_bsize2 = blk_params->subsize;
#if !CONFIG_EXT_RECUR_PARTITIONS
  blk_params->bsize_at_least_8x8 = (bsize >= BLOCK_8X8);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  blk_params->bsize = bsize;

#if CONFIG_CB1TO4_SPLIT
  assert(pc_tree != NULL);
  blk_params->parent_bsize =
      pc_tree->parent ? pc_tree->parent->block_size : BLOCK_INVALID;
#endif  // CONFIG_CB1TO4_SPLIT

  // Chroma subsampling.
  part_search_state->ss_x = x->e_mbd.plane[1].subsampling_x;
  part_search_state->ss_y = x->e_mbd.plane[1].subsampling_y;

  // Check if the partition corresponds to edge block.
  blk_params->has_rows = (blk_params->mi_row_edge < mi_params->mi_rows);
  blk_params->has_cols = (blk_params->mi_col_edge < mi_params->mi_cols);

  const int ebw = mi_size_wide[bsize] / 8;
  const int ebh = mi_size_high[bsize] / 8;
  blk_params->has_7_8th_rows = (mi_row + 7 * ebh < mi_params->mi_rows);
  blk_params->has_7_8th_cols = (mi_col + 7 * ebw < mi_params->mi_cols);

#if CONFIG_CB1TO4_SPLIT
  blk_params->has_3_4th_rows = (mi_row + 6 * ebh < mi_params->mi_rows);
  blk_params->has_3_4th_cols = (mi_col + 6 * ebw < mi_params->mi_cols);
#endif  // CONFIG_CB1TO4_SPLIT

  // Update intra partitioning related info.
  part_search_state->intra_part_info = &x->part_search_info;
  // Prepare for segmentation CNN-based partitioning for intra-frame.
  if (frame_is_intra_only(cm) && bsize == BLOCK_64X64) {
    part_search_state->intra_part_info->quad_tree_idx = 0;
    part_search_state->intra_part_info->cnn_output_valid = 0;
  }

  // Set partition plane context index.
  part_search_state->pl_ctx_idx =
#if CONFIG_EXT_RECUR_PARTITIONS
      is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                         ,
                         blk_params->parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
                         )
#else
      blk_params->bsize_at_least_8x8
#endif  // CONFIG_EXT_RECUR_PARTITIONS
          ? partition_plane_context(xd, mi_row, mi_col, bsize)
          : 0;

  // Partition cost buffer update
  ModeCosts *mode_costs = &x->mode_costs;
  part_search_state->partition_cost =
      mode_costs->partition_cost[tree_type == CHROMA_PART]
                                [part_search_state->pl_ctx_idx];
#if CONFIG_EXT_RECUR_PARTITIONS
  if (av1_get_normative_forced_partition_type(
          mi_params, tree_type, part_search_state->ss_x,
          part_search_state->ss_y, mi_row, mi_col, bsize,
#if CONFIG_CB1TO4_SPLIT
          blk_params->parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
          ptree_luma, &pc_tree->chroma_ref_info) != PARTITION_INVALID) {
    part_search_state->partition_cost = kZeroPartitionCosts;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXTENDED_SDP
  if (xd->tree_type != CHROMA_PART) {
    const int ctx = get_intra_region_context(bsize);
    part_search_state->region_type_cost = mode_costs->region_type_cost[ctx];
  }
#endif  // CONFIG_EXTENDED_SDP

  // Initialize HORZ and VERT win flags as true for all split partitions.
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
    part_search_state->split_part_rect_win[i].rect_part_win[HORZ] = true;
    part_search_state->split_part_rect_win[i].rect_part_win[VERT] = true;
  }

  // Initialize the rd cost.
  av1_init_rd_stats(&part_search_state->this_rdc);

  // Initialize RD costs for partition types to 0.
  part_search_state->none_rd = 0;
  av1_zero(part_search_state->split_rd);
  av1_zero(part_search_state->rect_part_rd);

  // Initialize SPLIT partition to be not ready.
  av1_zero(part_search_state->is_split_ctx_is_ready);
  // Initialize HORZ and VERT partitions to be not ready.
  av1_zero(part_search_state->is_rect_ctx_is_ready);

  // Initialize partition search flags to defaults.
  part_search_state->terminate_partition_search = 0;

  av1_zero(part_search_state->prune_rect_part);

#if CONFIG_EXT_RECUR_PARTITIONS
  part_search_state->partition_boundaries = NULL;
  part_search_state->prune_partition_none = false;
#if CONFIG_ML_PART_SPLIT
  part_search_state->prune_partition_split = false;
#endif  // CONFIG_ML_PART_SPLIT
  av1_zero(part_search_state->prune_partition_3);
  av1_zero(part_search_state->prune_partition_4a);
  av1_zero(part_search_state->prune_partition_4b);

  part_search_state->forced_partition = get_forced_partition_type(
      cm, x, mi_row, mi_col, bsize,
#if CONFIG_CB1TO4_SPLIT
      blk_params->parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
      ptree_luma, template_tree,
#if CONFIG_EXTENDED_SDP
      (pc_tree ? pc_tree->region_type : MIXED_INTER_INTRA_REGION),
#endif  // CONFIG_EXTENDED_SDP
      &pc_tree->chroma_ref_info);

  init_allowed_partitions(part_search_state, &cpi->oxcf.part_cfg,
                          &pc_tree->chroma_ref_info, tree_type);

  if (max_recursion_depth == 0) {
    part_search_state->prune_rect_part[HORZ] =
        part_search_state->prune_rect_part[VERT] = true;
    part_search_state->prune_partition_3[HORZ] =
        part_search_state->prune_partition_3[VERT] = true;
    part_search_state->prune_partition_4a[HORZ] =
        part_search_state->prune_partition_4a[VERT] = true;
    part_search_state->prune_partition_4b[HORZ] =
        part_search_state->prune_partition_4b[VERT] = true;
  }
#else
  part_search_state->do_square_split =
      blk_params->bsize_at_least_8x8 &&
      (tree_type != CHROMA_PART || bsize > BLOCK_8X8);
  part_search_state->do_rectangular_split =
      cpi->oxcf.part_cfg.enable_rect_partitions &&
      (tree_type != CHROMA_PART || bsize > BLOCK_8X8);

  const BLOCK_SIZE horz_subsize = get_partition_subsize(bsize, PARTITION_HORZ);
  const BLOCK_SIZE vert_subsize = get_partition_subsize(bsize, PARTITION_VERT);
  const int is_horz_size_valid =
      horz_subsize != BLOCK_INVALID &&
      get_plane_block_size(horz_subsize, part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;
  const int is_vert_size_valid =
      vert_subsize != BLOCK_INVALID &&
      get_plane_block_size(vert_subsize, part_search_state->ss_x,
                           part_search_state->ss_y) != BLOCK_INVALID;
  const bool no_sub_16_chroma_part =
      tree_type != CHROMA_PART ||
      (block_size_wide[bsize] > 8 && block_size_high[bsize] > 8);

  // Initialize allowed partition types for the partition block.
  part_search_state->is_block_splittable = is_partition_point(bsize);
  part_search_state->partition_none_allowed =
      blk_params->has_rows && blk_params->has_cols;
  part_search_state->partition_rect_allowed[HORZ] =
      blk_params->has_cols && blk_params->bsize_at_least_8x8 &&
      no_sub_16_chroma_part && cpi->oxcf.part_cfg.enable_rect_partitions &&
      is_horz_size_valid;
  part_search_state->partition_rect_allowed[VERT] =
      blk_params->has_rows && blk_params->bsize_at_least_8x8 &&
      no_sub_16_chroma_part && cpi->oxcf.part_cfg.enable_rect_partitions &&
      is_vert_size_valid;

  // Reset the flag indicating whether a partition leading to a rdcost lower
  // than the bound best_rdc has been found.
  part_search_state->found_best_partition = false;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

#if !CONFIG_EXT_RECUR_PARTITIONS
// Override partition cost buffer for the edge blocks.
static void set_partition_cost_for_edge_blk(
    AV1_COMMON const *cm, MACROBLOCKD *const xd,
    PartitionSearchState *part_search_state) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  assert(blk_params.bsize_at_least_8x8 && part_search_state->pl_ctx_idx >= 0);
  const int plane = xd->tree_type == CHROMA_PART;
  const aom_cdf_prob *partition_cdf =
      cm->fc->partition_cdf[plane][part_search_state->pl_ctx_idx];
  const int max_cost = av1_cost_symbol(0);
  for (PARTITION_TYPE i = 0; i < PARTITION_TYPES; ++i)
    part_search_state->tmp_partition_cost[i] = max_cost;
  if (blk_params.has_cols) {
    // At the bottom, the two possibilities are HORZ and SPLIT.
    aom_cdf_prob bot_cdf[2];
    partition_gather_vert_alike(bot_cdf, partition_cdf, blk_params.bsize);
    static const int bot_inv_map[2] = { PARTITION_HORZ, PARTITION_SPLIT };
    av1_cost_tokens_from_cdf(part_search_state->tmp_partition_cost, bot_cdf,
                             bot_inv_map);
  } else if (blk_params.has_rows) {
    // At the right, the two possibilities are VERT and SPLIT.
    aom_cdf_prob rhs_cdf[2];
    partition_gather_horz_alike(rhs_cdf, partition_cdf, blk_params.bsize);
    static const int rhs_inv_map[2] = { PARTITION_VERT, PARTITION_SPLIT };
    av1_cost_tokens_from_cdf(part_search_state->tmp_partition_cost, rhs_cdf,
                             rhs_inv_map);
  } else {
    // At the bottom right, we always split.
    part_search_state->tmp_partition_cost[PARTITION_SPLIT] = 0;
  }
  // Override the partition cost buffer.
  part_search_state->partition_cost = part_search_state->tmp_partition_cost;
}

// Reset the partition search state flags when
// must_find_valid_partition is equal to 1.
static AOM_INLINE void reset_part_limitations(
    AV1_COMP *const cpi, PartitionSearchState *part_search_state) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  part_search_state->do_square_split =
      blk_params.bsize_at_least_8x8 &&
      (blk_params.width > blk_params.min_partition_size_1d);
  part_search_state->partition_none_allowed =
      blk_params.has_rows && blk_params.has_cols &&
      (blk_params.width >= blk_params.min_partition_size_1d);

  // Initialize allowed partition types for the partition block.
  part_search_state->partition_rect_allowed[HORZ] =
      blk_params.has_cols &&
      is_partition_valid(blk_params.bsize, PARTITION_HORZ) &&
      get_plane_block_size(
          get_partition_subsize(blk_params.bsize, PARTITION_HORZ),
          part_search_state->ss_x, part_search_state->ss_y) != BLOCK_INVALID &&
      (blk_params.width > blk_params.min_partition_size_1d) &&
      cpi->oxcf.part_cfg.enable_rect_partitions;
  part_search_state->partition_rect_allowed[VERT] =
      blk_params.has_rows &&
      is_partition_valid(blk_params.bsize, PARTITION_VERT) &&
      get_plane_block_size(
          get_partition_subsize(blk_params.bsize, PARTITION_VERT),
          part_search_state->ss_x, part_search_state->ss_y) != BLOCK_INVALID &&
      (blk_params.width > blk_params.min_partition_size_1d) &&
      cpi->oxcf.part_cfg.enable_rect_partitions;
  part_search_state->terminate_partition_search = 0;
}
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

static const int rect_partition_type[NUM_RECT_PARTS] = { PARTITION_HORZ,
                                                         PARTITION_VERT };
#if !CONFIG_EXT_RECUR_PARTITIONS
// Rectangular partitions evaluation at sub-block level.
static void rd_pick_rect_partition(AV1_COMP *const cpi, TileDataEnc *tile_data,
                                   MACROBLOCK *x,
                                   PICK_MODE_CONTEXT *cur_partition_ctx,
                                   PartitionSearchState *part_search_state,
                                   RD_STATS *best_rdc, const int idx,
                                   int mi_row, int mi_col, BLOCK_SIZE bsize,
                                   PARTITION_TYPE partition_type) {
  // Obtain the remainder from the best rd cost
  // for further processing of partition.
  RD_STATS best_remain_rdcost;
  av1_rd_stats_subtraction(x->rdmult, best_rdc, &part_search_state->sum_rdc,
                           &best_remain_rdcost);

  // Obtain the best mode for the partition sub-block.
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &part_search_state->this_rdc,
                partition_type,
#if CONFIG_EXTENDED_SDP
                pc_tree->region_type,
#endif  // CONFIG_EXTENDED_SDP
                bsize, cur_partition_ctx, best_remain_rdcost);
  av1_rd_cost_update(x->rdmult, &part_search_state->this_rdc);

  // Update the partition rd cost with the current sub-block rd.
  if (part_search_state->this_rdc.rate == INT_MAX) {
    part_search_state->sum_rdc.rdcost = INT64_MAX;
  } else {
    part_search_state->sum_rdc.rate += part_search_state->this_rdc.rate;
    part_search_state->sum_rdc.dist += part_search_state->this_rdc.dist;
    av1_rd_cost_update(x->rdmult, &part_search_state->sum_rdc);
  }
  const RECT_PART_TYPE rect_part =
      partition_type == PARTITION_HORZ ? HORZ : VERT;
  part_search_state->rect_part_rd[rect_part][idx] =
      part_search_state->this_rdc.rdcost;
}
#else
static void rd_pick_rect_partition(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, PC_TREE *pc_tree,
    PartitionSearchState *part_search_state, const RD_STATS *best_rdc,
    RECT_PART_TYPE rect_type,
    const int mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][2],
    BLOCK_SIZE bsize, const int is_not_edge_block[NUM_RECT_PARTS],
    SB_MULTI_PASS_MODE multi_pass_mode, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, bool *both_blocks_skippable,
#if CONFIG_EXTENDED_SDP
    PARTITION_TYPE parent_partition,
#endif  // CONFIG_EXTENDED_SDP
    int max_recursion_depth
#if CONFIG_ML_PART_SPLIT
    ,
    int next_force_prune_flags[3]
#endif  // CONFIG_ML_PART_SPLIT
) {
  const PARTITION_TYPE partition_type = rect_partition_type[rect_type];
  RD_STATS *sum_rdc = &part_search_state->sum_rdc;

  sum_rdc->rate = part_search_state->partition_cost[partition_type];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size, parent_partition) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_HORZ))
    sum_rdc->rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, 0);

  RD_STATS this_rdc;
  RD_STATS best_remain_rdcost;
#if CONFIG_EXTENDED_SDP
  PC_TREE **sub_tree = (rect_type == HORZ)
                           ? pc_tree->horizontal[pc_tree->region_type]
                           : pc_tree->vertical[pc_tree->region_type];
#else
  PC_TREE **sub_tree =
      (rect_type == HORZ) ? pc_tree->horizontal : pc_tree->vertical;
#endif  // CONFIG_EXTENDED_SDP
  *both_blocks_skippable = true;
  av1_rd_stats_subtraction(x->rdmult, best_rdc, sum_rdc, &best_remain_rdcost);
  bool partition_found = av1_rd_pick_partition(
      cpi, td, tile_data, tp, mi_pos_rect[rect_type][0][0],
      mi_pos_rect[rect_type][0][1], bsize,
#if CONFIG_EXTENDED_SDP
      (rect_type == HORZ ? PARTITION_HORZ : PARTITION_VERT),
#endif  // CONFIG_EXTENDED_SDP
      &this_rdc, best_remain_rdcost, sub_tree[0],
      get_partition_subtree_const(ptree_luma, 0),
      get_partition_subtree_const(template_tree, 0), max_recursion_depth, NULL,
      NULL, multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
      ,
      next_force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
  );
  av1_rd_cost_update(x->rdmult, &this_rdc);
  if (!partition_found) {
    av1_invalid_rd_stats(sum_rdc);
    return;
  } else {
    *both_blocks_skippable &= sub_tree[0]->skippable;
    sum_rdc->rate += this_rdc.rate;
    sum_rdc->dist += this_rdc.dist;
    av1_rd_cost_update(x->rdmult, sum_rdc);
  }
  part_search_state->rect_part_rd[rect_type][0] = this_rdc.rdcost;

  if (sum_rdc->rdcost < best_rdc->rdcost && is_not_edge_block[rect_type]) {
    av1_rd_stats_subtraction(x->rdmult, best_rdc, sum_rdc, &best_remain_rdcost);
    partition_found = av1_rd_pick_partition(
        cpi, td, tile_data, tp, mi_pos_rect[rect_type][1][0],
        mi_pos_rect[rect_type][1][1], bsize,
#if CONFIG_EXTENDED_SDP
        (rect_type == HORZ ? PARTITION_HORZ : PARTITION_VERT),
#endif  // CONFIG_EXTENDED_SDP
        &this_rdc, best_remain_rdcost, sub_tree[1],
        get_partition_subtree_const(ptree_luma, 1),
        get_partition_subtree_const(template_tree, 1), max_recursion_depth,
        NULL, NULL, multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
        ,
        next_force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
    );
    av1_rd_cost_update(x->rdmult, &this_rdc);
    part_search_state->rect_part_rd[rect_type][1] = this_rdc.rdcost;

    if (!partition_found) {
      av1_invalid_rd_stats(sum_rdc);
      return;
    } else {
      *both_blocks_skippable &= sub_tree[1]->skippable;
      sum_rdc->rate += this_rdc.rate;
      sum_rdc->dist += this_rdc.dist;
      av1_rd_cost_update(x->rdmult, sum_rdc);
    }
  }
}

static AOM_INLINE bool is_part_pruned_by_forced_partition(
    const PartitionSearchState *part_state, PARTITION_TYPE partition) {
  const PARTITION_TYPE forced_partition = part_state->forced_partition;
  return forced_partition != PARTITION_INVALID && forced_partition != partition;
}
#endif

typedef int (*active_edge_info)(const AV1_COMP *cpi, int mi_col, int mi_step);

// Checks if HORZ / VERT partition search is allowed.
static AOM_INLINE int is_rect_part_allowed(
    const AV1_COMP *cpi, PartitionSearchState *part_search_state,
    active_edge_info *active_edge, RECT_PART_TYPE rect_part, const int mi_pos) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int mi_step =
      (rect_part == HORZ) ? blk_params.mi_step_h : blk_params.mi_step_w;
#else
  const int mi_step = blk_params.mi_step;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  const int is_part_allowed =
      (!part_search_state->terminate_partition_search &&
       part_search_state->partition_rect_allowed[rect_part] &&
       !part_search_state->prune_rect_part[rect_part] &&
#if CONFIG_EXT_RECUR_PARTITIONS
       is_partition_valid(blk_params.bsize, rect_partition_type[rect_part]) &&
#endif  // CONFIG_EXT_RECUR_PARTITIONS
       (part_search_state->do_rectangular_split ||
        active_edge[rect_part](cpi, mi_pos, mi_step)));
  return is_part_allowed;
}

#if CONFIG_EXT_RECUR_PARTITIONS
static AOM_INLINE void prune_rect_with_none_rd(
    PartitionSearchState *part_search_state, BLOCK_SIZE bsize, int q_index,
    int rdmult, int64_t part_none_rd, const int *is_not_edge_block) {
  for (RECT_PART_TYPE rect = 0; rect < NUM_RECT_PARTS; rect++) {
    // Disable pruning on the boundary
    if (!is_not_edge_block[rect]) {
      continue;
    }
    const PARTITION_TYPE partition_type = rect_partition_type[rect];
    float discount_factor = 1.1f;
    const int q_thresh = 180;
    if (q_index < q_thresh) {
      discount_factor -= 0.025f;
    }
    if (AOMMAX(block_size_wide[bsize], block_size_high[bsize]) < 16) {
      discount_factor -= 0.02f;
    }
    const int part_rate = part_search_state->partition_cost[partition_type];
    const int64_t est_rd = (int64_t)(part_none_rd / discount_factor) +
                           RDCOST(rdmult, part_rate, 0);
    if (est_rd > part_none_rd) {
      part_search_state->prune_rect_part[rect] = true;
    }
  }
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

// Rectangular partition types search function.
static void rectangular_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, PC_TREE *pc_tree,
    RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
#if CONFIG_EXT_RECUR_PARTITIONS
    SB_MULTI_PASS_MODE multi_pass_mode, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, int max_recursion_depth,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    RD_RECT_PART_WIN_INFO *rect_part_win_info,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
#if CONFIG_EXTENDED_SDP
    PARTITION_TYPE parent_partition,
#endif  // CONFIG_EXTENDED_SDP
    int64_t part_none_rd
#if CONFIG_ML_PART_SPLIT
    ,
    int next_force_prune_flags[2][3]
#endif  // CONFIG_ML_PART_SPLIT
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS *sum_rdc = &part_search_state->sum_rdc;

  MACROBLOCKD *xd = &x->e_mbd;
  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
  (void)plane_start;
  (void)plane_end;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;
#else   // !CONFIG_EXT_RECUR_PARTITIONS
  (void)part_none_rd;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  // mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][0]: mi_row postion of
  //                                           HORZ and VERT partition types.
  // mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][1]: mi_col postion of
  //                                           HORZ and VERT partition types.
  const int mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][2] = {
    { { blk_params.mi_row, blk_params.mi_col },
      { blk_params.mi_row_edge, blk_params.mi_col } },
    { { blk_params.mi_row, blk_params.mi_col },
      { blk_params.mi_row, blk_params.mi_col_edge } }
  };

  // Initialize active edge_type function pointer
  // for HOZR and VERT partition types.
  active_edge_info active_edge_type[NUM_RECT_PARTS] = { av1_active_h_edge,
                                                        av1_active_v_edge };

  // Indicates edge blocks for HORZ and VERT partition types.
  const int is_not_edge_block[NUM_RECT_PARTS] = { blk_params.has_rows,
                                                  blk_params.has_cols };
#if !CONFIG_EXT_RECUR_PARTITIONS

  // Initialize pc tree context for HORZ and VERT partition types.
  PICK_MODE_CONTEXT **cur_ctx[NUM_RECT_PARTS][SUB_PARTITIONS_RECT] = {
    { &pc_tree->horizontal[0], &pc_tree->horizontal[1] },
    { &pc_tree->vertical[0], &pc_tree->vertical[1] }
  };
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXT_RECUR_PARTITIONS
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  const BLOCK_SIZE bsize = blk_params.bsize;
  const bool is_whole_block_inside =
      (blk_params.mi_row + mi_size_high[bsize] < mi_params->mi_rows) &&
      (blk_params.mi_col + mi_size_wide[bsize] < mi_params->mi_cols);
  const bool try_prune_with_ml =
      cpi->sf.part_sf.prune_rect_with_ml && !frame_is_intra_only(cm) &&
      part_search_state->forced_partition == PARTITION_INVALID &&
      is_whole_block_inside && part_none_rd < INT64_MAX &&
      (is_rect_part_allowed(cpi, part_search_state, active_edge_type, HORZ,
                            mi_pos_rect[HORZ][0][HORZ]) ||
       is_rect_part_allowed(cpi, part_search_state, active_edge_type, VERT,
                            mi_pos_rect[VERT][0][VERT]));

  if (try_prune_with_ml && bsize != BLOCK_4X8 && bsize != BLOCK_8X4 &&
      is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                         ,
                         blk_params.parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
                         )) {
    float ml_features[19];
    av1_gather_erp_rect_features(ml_features, cpi, x, &tile_data->tile_info,
                                 pc_tree, part_search_state, part_none_rd,
                                 mi_pos_rect);
    const bool is_hd = AOMMIN(cm->width, cm->height) >= 1080;

    av1_erp_prune_rect(bsize, is_hd, ml_features,
                       &part_search_state->prune_rect_part[HORZ],
                       &part_search_state->prune_rect_part[VERT]);
  }
  if (cpi->sf.part_sf.prune_rect_with_none_rd &&
      part_search_state->forced_partition == PARTITION_INVALID &&
      !frame_is_intra_only(cm) && part_none_rd < INT64_MAX) {
    prune_rect_with_none_rd(part_search_state, bsize, x->qindex, x->rdmult,
                            part_none_rd, is_not_edge_block);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Loop over rectangular partition types.
  for (RECT_PART_TYPE i = HORZ; i < NUM_RECT_PARTS; i++) {
    assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                   !part_search_state->partition_rect_allowed[i]));

    // Check if the HORZ / VERT partition search is to be performed.
    if (!is_rect_part_allowed(cpi, part_search_state, active_edge_type, i,
                              mi_pos_rect[i][0][i]))
      continue;

    // Sub-partition idx.
    const PARTITION_TYPE partition_type = rect_partition_type[i];
    blk_params.subsize =
        get_partition_subsize(blk_params.bsize, partition_type);
    const int part_hv_rate = part_search_state->partition_cost[partition_type];
    if (part_hv_rate == INT_MAX ||
        RDCOST(x->rdmult, part_hv_rate, 0) >= best_rdc->rdcost) {
      continue;
    }
#if !CONFIG_EXT_RECUR_PARTITIONS
    assert(blk_params.subsize <= BLOCK_LARGEST);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    av1_init_rd_stats(sum_rdc);
#if CONFIG_EXT_RECUR_PARTITIONS
    if (is_part_pruned_by_forced_partition(part_search_state, partition_type)) {
      continue;
    }

#if CONFIG_EXTENDED_SDP
    const REGION_TYPE cur_region_type = pc_tree->region_type;
    PC_TREE **sub_tree = (i == HORZ) ? pc_tree->horizontal[cur_region_type]
                                     : pc_tree->vertical[cur_region_type];
#else
    PC_TREE **sub_tree = (i == HORZ) ? pc_tree->horizontal : pc_tree->vertical;
#endif  // CONFIG_EXTENDED_SDP
    assert(sub_tree);

    const int num_planes = av1_num_planes(cm);
    for (int idx = 0; idx < SUB_PARTITIONS_RECT; idx++) {
      if (sub_tree[idx]) {
        av1_free_pc_tree_recursive(sub_tree[idx], num_planes, 0, 0);
        sub_tree[idx] = NULL;
      }
    }
    sub_tree[0] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_pos_rect[i][0][0], mi_pos_rect[i][0][1],
        blk_params.subsize, pc_tree, partition_type, 0, 0, ss_x, ss_y);
    sub_tree[1] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_pos_rect[i][1][0], mi_pos_rect[i][1][1],
        blk_params.subsize, pc_tree, partition_type, 1, 1, ss_x, ss_y);

    bool both_blocks_skippable = true;

    const int track_ptree_luma =
        is_luma_chroma_share_same_partition(x->e_mbd.tree_type, ptree_luma,
                                            bsize) &&
        partition_type ==
            sdp_chroma_part_from_luma(bsize, ptree_luma->partition, ss_x, ss_y);
    rd_pick_rect_partition(
        cpi, td, tile_data, tp, x, pc_tree, part_search_state, best_rdc, i,
        mi_pos_rect, blk_params.subsize, is_not_edge_block, multi_pass_mode,
        track_ptree_luma ? ptree_luma : NULL, template_tree,
        &both_blocks_skippable,
#if CONFIG_EXTENDED_SDP
        parent_partition,
#endif  // CONFIG_EXTENDED_SDP
        max_recursion_depth
#if CONFIG_ML_PART_SPLIT
        ,
        next_force_prune_flags[i]
#endif  // CONFIG_ML_PART_SPLIT
    );
#else
    int sub_part_idx = 0;
    for (int j = 0; j < SUB_PARTITIONS_RECT; j++) {
      assert(cur_ctx[i][j] != NULL);
      if (cur_ctx[i][j][0] == NULL) {
        cur_ctx[i][j][0] =
            av1_alloc_pmc(cm, xd->tree_type, mi_pos_rect[i][j][0],
                          mi_pos_rect[i][j][1], blk_params.subsize, pc_tree,
                          partition_type, j, part_search_state->ss_x,
                          part_search_state->ss_y, &td->shared_coeff_buf);
      }
    }
    sum_rdc->rate = part_search_state->partition_cost[partition_type];
    sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, 0);
#if CONFIG_COLLECT_PARTITION_STATS
    if (best_rdc.rdcost - sum_rdc->rdcost >= 0) {
      partition_attempts[partition_type] += 1;
      aom_usec_timer_start(&partition_timer);
      partition_timer_on = 1;
    }
#endif

    // First sub-partition evaluation in HORZ / VERT partition type.
    rd_pick_rect_partition(
        cpi, tile_data, x, cur_ctx[i][sub_part_idx][0], part_search_state,
        best_rdc, 0, mi_pos_rect[i][sub_part_idx][0],
        mi_pos_rect[i][sub_part_idx][1], blk_params.subsize, partition_type);

    // Start of second sub-partition evaluation.
    // Evaluate second sub-partition if the first sub-partition cost
    // is less than the best cost and if it is not an edge block.
    if (sum_rdc->rdcost < best_rdc->rdcost && is_not_edge_block[i]) {
      const MB_MODE_INFO *const mbmi = &cur_ctx[i][sub_part_idx][0]->mic;
      const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
      // Neither palette mode nor cfl predicted.
      if (pmi->palette_size[PLANE_TYPE_Y] == 0 &&
          pmi->palette_size[PLANE_TYPE_UV] == 0) {
        if (mbmi->uv_mode != UV_CFL_PRED)
          part_search_state->is_rect_ctx_is_ready[i] = 1;
      }
      av1_update_state(cpi, td, cur_ctx[i][sub_part_idx][0], blk_params.mi_row,
                       blk_params.mi_col, blk_params.subsize, DRY_RUN_NORMAL);
      encode_superblock(cpi, tile_data, td, tp, DRY_RUN_NORMAL,
                        blk_params.subsize, plane_start, plane_end, NULL);

      // Second sub-partition evaluation in HORZ / VERT partition type.
      sub_part_idx = 1;
      rd_pick_rect_partition(
          cpi, tile_data, x, cur_ctx[i][sub_part_idx][0], part_search_state,
          best_rdc, 1, mi_pos_rect[i][sub_part_idx][0],
          mi_pos_rect[i][sub_part_idx][1], blk_params.subsize, partition_type);
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_COLLECT_PARTITION_STATS
    if (partition_timer_on) {
      aom_usec_timer_mark(&partition_timer);
      int64_t time = aom_usec_timer_elapsed(&partition_timer);
      partition_times[partition_type] += time;
      partition_timer_on = 0;
    }
#endif
    // Update HORZ / VERT best partition.
    if (sum_rdc->rdcost < best_rdc->rdcost) {
      sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, sum_rdc->dist);
      if (sum_rdc->rdcost < best_rdc->rdcost) {
#if CONFIG_EXT_RECUR_PARTITIONS
        pc_tree->skippable = both_blocks_skippable;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        *best_rdc = *sum_rdc;

#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
        update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
        part_search_state->found_best_partition = true;
        pc_tree->partitioning = partition_type;
      }
    } else {
      // Update HORZ / VERT win flag.
      if (rect_part_win_info != NULL)
        rect_part_win_info->rect_part_win[i] = false;
    }
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    av1_restore_context(cm, x, x_ctx, blk_params.mi_row, blk_params.mi_col,
                        blk_params.bsize, av1_num_planes(cm));
#if CONFIG_EXT_RECUR_PARTITIONS
    if (sum_rdc->rdcost < INT64_MAX && both_blocks_skippable &&
        !frame_is_intra_only(cm)) {
      const int right_shift =
          ((2 * (BLOCK_128_MI_SIZE_LOG2)) -
           (mi_size_wide_log2[bsize] + mi_size_high_log2[bsize]));
      const int64_t dist_breakout_thr =
          (right_shift >= 0)
              ? ((cpi->sf.part_sf.partition_search_breakout_dist_thr / 4) >>
                 right_shift)
              : ((cpi->sf.part_sf.partition_search_breakout_dist_thr / 4)
                 << (-right_shift));
      const int rate_breakout_thr =
          (int64_t)25 * cpi->sf.part_sf.partition_search_breakout_rate_thr *
          num_pels_log2_lookup[bsize];
      if (sum_rdc->dist < dist_breakout_thr &&
          sum_rdc->rate < rate_breakout_thr) {
        part_search_state->terminate_partition_search = true;
        break;
      }
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  }
}

#if !CONFIG_EXT_RECUR_PARTITIONS
// AB partition type evaluation.
static void rd_pick_ab_part(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PICK_MODE_CONTEXT *dst_ctxs[SUB_PARTITIONS_AB],
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    const BLOCK_SIZE ab_subsize[SUB_PARTITIONS_AB],
    const int ab_mi_pos[SUB_PARTITIONS_AB][2], const PARTITION_TYPE part_type
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    ,
    LevelBanksRDO *level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;

#if CONFIG_COLLECT_PARTITION_STATS
  {
    RD_STATS tmp_sum_rdc;
    av1_init_rd_stats(&tmp_sum_rdc);
    tmp_sum_rdc.rate =
        x->partition_cost[part_search_state->pl_ctx_idx][part_type];
    tmp_sum_rdc.rdcost = RDCOST(x->rdmult, tmp_sum_rdc.rate, 0);
    if (best_rdc->rdcost - tmp_sum_rdc.rdcost >= 0) {
      partition_attempts[part_type] += 1;
      aom_usec_timer_start(&partition_timer);
      partition_timer_on = 1;
    }
  }
#endif

  // Test this partition and update the best partition.
  part_search_state->found_best_partition |=
      rd_test_partition3(cpi, td, tile_data, tp, pc_tree, best_rdc, dst_ctxs,
                         mi_row, mi_col, bsize, part_type, ab_subsize, ab_mi_pos
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                         ,
                         level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      );

#if CONFIG_COLLECT_PARTITION_STATS
  if (partition_timer_on) {
    aom_usec_timer_mark(&partition_timer);
    int64_t time = aom_usec_timer_elapsed(&partition_timer);
    partition_times[part_type] += time;
    partition_timer_on = 0;
  }
#endif
  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
}

// Check if AB partitions search is allowed.
static AOM_INLINE int is_ab_part_allowed(
    PartitionSearchState *part_search_state,
    const int ab_partitions_allowed[NUM_AB_PARTS], const int ab_part_type) {
  const int is_horz_ab = (ab_part_type >> 1);
  const int is_part_allowed =
      (!part_search_state->terminate_partition_search &&
       part_search_state->partition_rect_allowed[is_horz_ab] &&
       ab_partitions_allowed[ab_part_type]);
  return is_part_allowed;
}

// Set mode search context.
static AOM_INLINE void set_mode_search_ctx(
    PC_TREE *pc_tree, const int is_ctx_ready[NUM_AB_PARTS][2],
    PICK_MODE_CONTEXT **mode_srch_ctx[NUM_AB_PARTS][2]) {
  mode_srch_ctx[HORZ_B][0] = &pc_tree->horizontal[0];
  mode_srch_ctx[VERT_B][0] = &pc_tree->vertical[0];

  if (is_ctx_ready[HORZ_A][0])
    mode_srch_ctx[HORZ_A][0] = &pc_tree->split[0]->none;

  if (is_ctx_ready[VERT_A][0])
    mode_srch_ctx[VERT_A][0] = &pc_tree->split[0]->none;

  if (is_ctx_ready[HORZ_A][1])
    mode_srch_ctx[HORZ_A][1] = &pc_tree->split[1]->none;
}

// AB Partitions type search.
static void ab_partitions_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PartitionSearchState *part_search_state,
    RD_STATS *best_rdc, RD_RECT_PART_WIN_INFO *rect_part_win_info,
    int pb_source_variance, int ext_partition_allowed
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    ,
    LevelBanksRDO *level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;

  int ab_partitions_allowed[NUM_AB_PARTS] = { 1, 1, 1, 1 };
  // Prune AB partitions
  av1_prune_ab_partitions(
      cpi, x, pc_tree, bsize, pb_source_variance, best_rdc->rdcost,
      part_search_state->rect_part_rd, part_search_state->split_rd,
      rect_part_win_info, ext_partition_allowed,
      part_search_state->partition_rect_allowed[HORZ],
      part_search_state->partition_rect_allowed[VERT],
      &ab_partitions_allowed[HORZ_A], &ab_partitions_allowed[HORZ_B],
      &ab_partitions_allowed[VERT_A], &ab_partitions_allowed[VERT_B]);

  // Flags to indicate whether the mode search is done.
  const int is_ctx_ready[NUM_AB_PARTS][2] = {
    { part_search_state->is_split_ctx_is_ready[0],
      part_search_state->is_split_ctx_is_ready[1] },
    { part_search_state->is_rect_ctx_is_ready[HORZ], 0 },
    { part_search_state->is_split_ctx_is_ready[0], 0 },
    { part_search_state->is_rect_ctx_is_ready[VERT], 0 }
  };

  // Current partition context.
  PICK_MODE_CONTEXT **cur_part_ctxs[NUM_AB_PARTS] = { pc_tree->horizontala,
                                                      pc_tree->horizontalb,
                                                      pc_tree->verticala,
                                                      pc_tree->verticalb };

  // Context of already evaluted partition types.
  PICK_MODE_CONTEXT **mode_srch_ctx[NUM_AB_PARTS][2];
  // Set context of already evaluted partition types.
  set_mode_search_ctx(pc_tree, is_ctx_ready, mode_srch_ctx);

  // Array of sub-partition size of AB partition types.
  const BLOCK_SIZE ab_subsize[NUM_AB_PARTS][SUB_PARTITIONS_AB] = {
    { blk_params.split_bsize2, blk_params.split_bsize2,
      get_partition_subsize(bsize, PARTITION_HORZ_A) },
    { get_partition_subsize(bsize, PARTITION_HORZ_B), blk_params.split_bsize2,
      blk_params.split_bsize2 },
    { blk_params.split_bsize2, blk_params.split_bsize2,
      get_partition_subsize(bsize, PARTITION_VERT_A) },
    { get_partition_subsize(bsize, PARTITION_VERT_B), blk_params.split_bsize2,
      blk_params.split_bsize2 }
  };

  // Array of mi_row, mi_col positions corresponds to each sub-partition in AB
  // partition types.
  const int ab_mi_pos[NUM_AB_PARTS][SUB_PARTITIONS_AB][2] = {
    { { mi_row, mi_col },
      { mi_row, blk_params.mi_col_edge },
      { blk_params.mi_row_edge, mi_col } },
    { { mi_row, mi_col },
      { blk_params.mi_row_edge, mi_col },
      { blk_params.mi_row_edge, blk_params.mi_col_edge } },
    { { mi_row, mi_col },
      { blk_params.mi_row_edge, mi_col },
      { mi_row, blk_params.mi_col_edge } },
    { { mi_row, mi_col },
      { mi_row, blk_params.mi_col_edge },
      { blk_params.mi_row_edge, blk_params.mi_col_edge } }
  };

  // Loop over AB partition types.
  for (AB_PART_TYPE ab_part_type = 0; ab_part_type < NUM_AB_PARTS;
       ab_part_type++) {
    const PARTITION_TYPE part_type = ab_part_type + PARTITION_HORZ_A;

    // Check if the AB partition search is to be performed.
    if (!is_ab_part_allowed(part_search_state, ab_partitions_allowed,
                            ab_part_type))
      continue;

    blk_params.subsize = get_partition_subsize(bsize, part_type);
    for (int i = 0; i < SUB_PARTITIONS_AB; i++) {
      assert(cur_part_ctxs[ab_part_type] != NULL);
      // Set AB partition context.
      if (cur_part_ctxs[ab_part_type][i] == NULL)
        cur_part_ctxs[ab_part_type][i] = av1_alloc_pmc(
            cm, x->e_mbd.tree_type, ab_mi_pos[ab_part_type][i][0],
            ab_mi_pos[ab_part_type][i][1], ab_subsize[ab_part_type][i], pc_tree,
            part_type, i, part_search_state->ss_x, part_search_state->ss_y,
            &td->shared_coeff_buf);
      // Set mode as not ready.
      cur_part_ctxs[ab_part_type][i]->rd_mode_is_ready = 0;
    }

    // Copy of mode search results if the ctx is ready.
    if (is_ctx_ready[ab_part_type][0]) {
      av1_copy_tree_context(cur_part_ctxs[ab_part_type][0],
                            mode_srch_ctx[ab_part_type][0][0],
                            av1_num_planes(cm));
      cur_part_ctxs[ab_part_type][0]->mic.partition = part_type;
      cur_part_ctxs[ab_part_type][0]->rd_mode_is_ready = 1;
      if (is_ctx_ready[ab_part_type][1]) {
        av1_copy_tree_context(cur_part_ctxs[ab_part_type][1],
                              mode_srch_ctx[ab_part_type][1][0],
                              av1_num_planes(cm));
        cur_part_ctxs[ab_part_type][1]->mic.partition = part_type;
        cur_part_ctxs[ab_part_type][1]->rd_mode_is_ready = 1;
      }
    }

    // Evaluation of AB partition type.
    rd_pick_ab_part(cpi, td, tile_data, tp, x, x_ctx, pc_tree,
                    cur_part_ctxs[ab_part_type], part_search_state, best_rdc,
                    ab_subsize[ab_part_type], ab_mi_pos[ab_part_type], part_type
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                    ,
                    level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  }
}

// Set mi positions for HORZ4 / VERT4 sub-block partitions.
static void set_mi_pos_partition4(const int inc_step[NUM_PART4_TYPES],
                                  int mi_pos[SUB_PARTITIONS_PART4][2],
                                  const int mi_row, const int mi_col) {
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; i++) {
    mi_pos[i][0] = mi_row + i * inc_step[HORZ4];
    mi_pos[i][1] = mi_col + i * inc_step[VERT4];
  }
}

// Set context and RD cost for HORZ4 / VERT4 partition types.
static void set_4_part_ctx_and_rdcost(
    MACROBLOCK *x, const AV1_COMMON *const cm, ThreadData *td,
    PICK_MODE_CONTEXT *cur_part_ctx[SUB_PARTITIONS_PART4],
    PartitionSearchState *part_search_state, PARTITION_TYPE partition_type,
    BLOCK_SIZE bsize, int mi_pos[SUB_PARTITIONS_PART4][2], PC_TREE *pc_tree) {
  // Initialize sum_rdc RD cost structure.
  av1_init_rd_stats(&part_search_state->sum_rdc);
  const int subsize = get_partition_subsize(bsize, partition_type);
  part_search_state->sum_rdc.rate =
      part_search_state->partition_cost[partition_type];
  part_search_state->sum_rdc.rdcost =
      RDCOST(x->rdmult, part_search_state->sum_rdc.rate, 0);
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    if (cur_part_ctx[i] == NULL)
      cur_part_ctx[i] = av1_alloc_pmc(
          cm, x->e_mbd.tree_type, mi_pos[i][0], mi_pos[i][1], subsize, pc_tree,
          partition_type, i, part_search_state->ss_x, part_search_state->ss_y,
          &td->shared_coeff_buf);
  }
}

// Partition search of HORZ4 / VERT4 partition types.
static void rd_pick_4partition(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PC_TREE *pc_tree, PICK_MODE_CONTEXT *cur_part_ctx[SUB_PARTITIONS_PART4],
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    const int inc_step[NUM_PART4_TYPES], PARTITION_TYPE partition_type
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    ,
    LevelBanksRDO *level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  // mi positions needed for HORZ4 and VERT4 partition types.
  int mi_pos_check[NUM_PART4_TYPES] = { cm->mi_params.mi_rows,
                                        cm->mi_params.mi_cols };
  const PART4_TYPES part4_idx = (partition_type != PARTITION_HORZ_4);
  int mi_pos[SUB_PARTITIONS_PART4][2];

  blk_params.subsize = get_partition_subsize(blk_params.bsize, partition_type);
  // Set mi positions for sub-block sizes.
  set_mi_pos_partition4(inc_step, mi_pos, blk_params.mi_row, blk_params.mi_col);
  // Set partition context and RD cost.
  set_4_part_ctx_and_rdcost(x, cm, td, cur_part_ctx, part_search_state,
                            partition_type, blk_params.bsize, mi_pos, pc_tree);
#if CONFIG_COLLECT_PARTITION_STATS
  if (best_rdc.rdcost - part_search_state->sum_rdc.rdcost >= 0) {
    partition_attempts[partition_type] += 1;
    aom_usec_timer_start(&partition_timer);
    partition_timer_on = 1;
  }
#endif
  // Loop over sub-block partitions.
  for (PART4_TYPES i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    if (i > 0 && mi_pos[i][part4_idx] >= mi_pos_check[part4_idx]) break;

    // Sub-block evaluation of Horz4 / Vert4 partition type.
    cur_part_ctx[i]->rd_mode_is_ready = 0;
    if (!rd_try_subblock(
            cpi, td, tile_data, tp, (i == SUB_PARTITIONS_PART4 - 1),
            mi_pos[i][0], mi_pos[i][1], blk_params.subsize, *best_rdc,
            &part_search_state->sum_rdc, partition_type, cur_part_ctx[i])) {
      av1_invalid_rd_stats(&part_search_state->sum_rdc);
      break;
    }
  }

  // Calculate the total cost and update the best partition.
  av1_rd_cost_update(x->rdmult, &part_search_state->sum_rdc);
  if (part_search_state->sum_rdc.rdcost < best_rdc->rdcost) {
    *best_rdc = part_search_state->sum_rdc;
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    part_search_state->found_best_partition = true;
    pc_tree->partitioning = partition_type;
#if CONFIG_EXTENDED_SDP
    if (!frame_is_intra_only(cm))
      pc_tree->region_type = MIXED_INTER_INTRA_REGION;
    else
      pc_tree->region_type = INTRA_REGION;
#endif  // CONFIG_EXTENDED_SDP
  }
#if CONFIG_COLLECT_PARTITION_STATS
  if (partition_timer_on) {
    aom_usec_timer_mark(&partition_timer);
    int64_t time = aom_usec_timer_elapsed(&partition_timer);
    partition_times[partition_type] += time;
    partition_timer_on = 0;
  }
#endif
  av1_restore_context(cm, x, x_ctx, blk_params.mi_row, blk_params.mi_col,
                      blk_params.bsize, av1_num_planes(cm));
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

// Prune 4-way partitions based on the number of horz/vert wins
// in the current block and sub-blocks in PARTITION_SPLIT.
static void prune_4_partition_using_split_info(
    AV1_COMP *const cpi, MACROBLOCK *x, PartitionSearchState *part_search_state,
    int part4_search_allowed[NUM_PART4_TYPES]) {
  PART4_TYPES cur_part[NUM_PART4_TYPES] = { HORZ4, VERT4 };
  // Count of child blocks in which HORZ or VERT partition has won
  int num_child_rect_win[NUM_RECT_PARTS] = { 0, 0 };
  // Prune HORZ4/VERT4 partitions based on number of HORZ/VERT winners of
  // split partiitons.
  // Conservative pruning for high quantizers.
  const int num_win_thresh = AOMMIN(3 * (MAXQ - x->qindex) / MAXQ + 1, 3);

  for (RECT_PART_TYPE i = HORZ; i < NUM_RECT_PARTS; i++) {
    if (!(cpi->sf.part_sf.prune_4_partition_using_split_info &&
          part4_search_allowed[cur_part[i]]))
      continue;
    // Loop over split partitions.
    // Get reactnagular partitions winner info of split partitions.
    for (int idx = 0; idx < SUB_PARTITIONS_SPLIT; idx++)
      num_child_rect_win[i] +=
          (part_search_state->split_part_rect_win[idx].rect_part_win[i]) ? 1
                                                                         : 0;
    if (num_child_rect_win[i] < num_win_thresh) {
      part4_search_allowed[cur_part[i]] = 0;
    }
  }
}

// Prune 4-way partition search.
static void prune_4_way_partition_search(
    AV1_COMP *const cpi, MACROBLOCK *x, PC_TREE *pc_tree,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    int pb_source_variance, int ext_partition_allowed,
    int part4_search_allowed[NUM_PART4_TYPES]) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  PARTITION_TYPE cur_part[NUM_PART4_TYPES] = { PARTITION_HORZ_4,
                                               PARTITION_VERT_4 };
  const PartitionCfg *const part_cfg = &cpi->oxcf.part_cfg;
  // partition4_allowed is 1 if we can use a PARTITION_HORZ_4 or
  // PARTITION_VERT_4 for this block. This is almost the same as
  // ext_partition_allowed, except that we don't allow 128x32 or 32x128
  // blocks, so we require that bsize is not BLOCK_128X128.
  const int partition4_allowed = part_cfg->enable_1to4_partitions &&
                                 ext_partition_allowed &&
                                 bsize != BLOCK_128X128;

  for (PART4_TYPES i = HORZ4; i < NUM_PART4_TYPES; i++) {
    part4_search_allowed[i] =
        partition4_allowed && part_search_state->partition_rect_allowed[i] &&
        get_plane_block_size(get_partition_subsize(bsize, cur_part[i]),
                             part_search_state->ss_x,
                             part_search_state->ss_y) != BLOCK_INVALID;
  }
  // Pruning: pruning out 4-way partitions based on the current best
  // partition.
  if (cpi->sf.part_sf.prune_ext_partition_types_search_level == 2) {
    part4_search_allowed[HORZ4] &= (pc_tree->partitioning == PARTITION_HORZ ||
                                    pc_tree->partitioning == PARTITION_HORZ_A ||
                                    pc_tree->partitioning == PARTITION_HORZ_B ||
                                    pc_tree->partitioning == PARTITION_SPLIT ||
                                    pc_tree->partitioning == PARTITION_NONE);
    part4_search_allowed[VERT4] &= (pc_tree->partitioning == PARTITION_VERT ||
                                    pc_tree->partitioning == PARTITION_VERT_A ||
                                    pc_tree->partitioning == PARTITION_VERT_B ||
                                    pc_tree->partitioning == PARTITION_SPLIT ||
                                    pc_tree->partitioning == PARTITION_NONE);
  }

  // Pruning: pruning out some 4-way partitions using a DNN taking rd costs of
  // sub-blocks from basic partition types.
  if (cpi->sf.part_sf.ml_prune_4_partition && partition4_allowed &&
      part_search_state->partition_rect_allowed[HORZ] &&
      part_search_state->partition_rect_allowed[VERT]) {
    av1_ml_prune_4_partition(
        cpi, x, bsize, pc_tree->partitioning, best_rdc->rdcost,
        part_search_state->rect_part_rd, part_search_state->split_rd,
        &part4_search_allowed[HORZ4], &part4_search_allowed[VERT4],
        pb_source_variance, mi_row, mi_col);
  }

  // Pruning: pruning out 4-way partitions based on the number of horz/vert
  // wins in the current block and sub-blocks in PARTITION_SPLIT.
  prune_4_partition_using_split_info(cpi, x, part_search_state,
                                     part4_search_allowed);
}
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

// Set PARTITION_NONE allowed flag.
static AOM_INLINE void set_part_none_allowed_flag(
    const AV1_COMP *const cpi,
#if CONFIG_EXT_RECUR_PARTITIONS
    TREE_TYPE tree_type,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
    int sdp_inter_chroma_flag,
#endif  // CONFIG_EXTENDED_SDP
    PartitionSearchState *part_search_state) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
#if CONFIG_EXT_RECUR_PARTITIONS
  if (tree_type == CHROMA_PART && blk_params.bsize == BLOCK_8X8) {
    part_search_state->partition_none_allowed = 1;
    return;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
  if (sdp_inter_chroma_flag) {
    part_search_state->partition_none_allowed = 1;
    return;
  }
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXT_RECUR_PARTITIONS
  if (is_bsize_geq(blk_params.min_partition_size, blk_params.bsize) &&
      blk_params.has_rows && blk_params.has_cols)
#else
  if ((blk_params.width <= blk_params.min_partition_size_1d) &&
      blk_params.has_rows && blk_params.has_cols)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    part_search_state->partition_none_allowed = 1;

#if !CONFIG_EXT_RECUR_PARTITIONS
  if (part_search_state->partition_none_allowed == BLOCK_INVALID) {
    part_search_state->partition_none_allowed = 0;
    return;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Set PARTITION_NONE for screen content.
  if (cpi->is_screen_content_type)
    part_search_state->partition_none_allowed =
        blk_params.has_rows && blk_params.has_cols;
}

// Set params needed for PARTITION_NONE search.
static void set_none_partition_params(const AV1_COMMON *const cm,
                                      ThreadData *td, MACROBLOCK *x,
                                      PC_TREE *pc_tree,
                                      PartitionSearchState *part_search_state,
                                      RD_STATS *best_remain_rdcost,
                                      RD_STATS *best_rdc, int *pt_cost) {
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS partition_rdcost;
  // Set PARTITION_NONE context.
#if CONFIG_EXTENDED_SDP
  if (is_inter_sdp_chroma(cm, pc_tree->region_type, x->e_mbd.tree_type)) {
    if (pc_tree->none_chroma == NULL) {
      pc_tree->none_chroma = av1_alloc_pmc(
          cm, x->e_mbd.tree_type, blk_params.mi_row, blk_params.mi_col,
          blk_params.bsize, pc_tree, PARTITION_NONE, 0, part_search_state->ss_x,
          part_search_state->ss_y, &td->shared_coeff_buf);
    }
  } else {
    if (pc_tree->none[pc_tree->region_type] == NULL) {
      pc_tree->none[pc_tree->region_type] = av1_alloc_pmc(
          cm, x->e_mbd.tree_type, blk_params.mi_row, blk_params.mi_col,
          blk_params.bsize, pc_tree, PARTITION_NONE, 0, part_search_state->ss_x,
          part_search_state->ss_y, &td->shared_coeff_buf);
    }
  }
#else
  if (pc_tree->none == NULL)
    pc_tree->none = av1_alloc_pmc(
        cm, x->e_mbd.tree_type, blk_params.mi_row, blk_params.mi_col,
        blk_params.bsize, pc_tree, PARTITION_NONE, 0, part_search_state->ss_x,
        part_search_state->ss_y, &td->shared_coeff_buf);
#endif  // CONFIG_EXTENDED_SDP

  // Set PARTITION_NONE type cost.
  if (part_search_state->partition_none_allowed) {
    if (part_search_state->is_block_splittable
#if CONFIG_EXTENDED_SDP
        && !is_inter_sdp_chroma(cm, pc_tree->region_type, x->e_mbd.tree_type)
#endif  // CONFIG_EXTENDED_SDP
    ) {
      *pt_cost = part_search_state->partition_cost[PARTITION_NONE] < INT_MAX
                     ? part_search_state->partition_cost[PARTITION_NONE]
                     : 0;
    }

    // Initialize the RD stats structure.
    av1_init_rd_stats(&partition_rdcost);
    partition_rdcost.rate = *pt_cost;
    av1_rd_cost_update(x->rdmult, &partition_rdcost);
    av1_rd_stats_subtraction(x->rdmult, best_rdc, &partition_rdcost,
                             best_remain_rdcost);
  }
}

// Skip other partitions based on PARTITION_NONE rd cost.
static void prune_partitions_after_none(AV1_COMP *const cpi, MACROBLOCK *x,
                                        SIMPLE_MOTION_DATA_TREE *sms_tree,
                                        PICK_MODE_CONTEXT *ctx_none,
                                        PartitionSearchState *part_search_state,
                                        RD_STATS *best_rdc,
                                        unsigned int *pb_source_variance) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  RD_STATS *this_rdc = &part_search_state->this_rdc;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

#if CONFIG_EXT_RECUR_PARTITIONS
  (void)sms_tree;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  if (!frame_is_intra_only(cm) &&
#if CONFIG_EXT_RECUR_PARTITIONS
      part_search_state->do_rectangular_split &&
#else
      (part_search_state->do_square_split ||
       part_search_state->do_rectangular_split) &&
#endif
      !x->e_mbd.lossless[xd->mi[0]->segment_id] && ctx_none->skippable) {
    const int use_ml_based_breakout =
        bsize <= cpi->sf.part_sf.use_square_partition_only_threshold &&
#if CONFIG_EXT_RECUR_PARTITIONS
        is_square_block(bsize) &&
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        bsize > BLOCK_4X4 && xd->bd == 8;
    if (use_ml_based_breakout) {
      if (av1_ml_predict_breakout(cpi, bsize, x, this_rdc,
                                  *pb_source_variance)) {
#if !CONFIG_EXT_RECUR_PARTITIONS
        part_search_state->do_square_split = 0;
#endif
        part_search_state->do_rectangular_split = 0;
      }
    }

    // Adjust dist breakout threshold according to the partition size.
    const int right_shift =
        ((2 * (BLOCK_128_MI_SIZE_LOG2)) -
         (mi_size_wide_log2[bsize] + mi_size_high_log2[bsize]));
    const int64_t dist_breakout_thr =
        (right_shift >= 0)
            ? (cpi->sf.part_sf.partition_search_breakout_dist_thr >>
               right_shift)
            : (cpi->sf.part_sf.partition_search_breakout_dist_thr
               << (-right_shift));
    const int rate_breakout_thr =
        cpi->sf.part_sf.partition_search_breakout_rate_thr *
        num_pels_log2_lookup[bsize];
    // If all y, u, v transform blocks in this partition are skippable,
    // and the dist & rate are within the thresholds, the partition
    // search is terminated for current branch of the partition search
    // tree. The dist & rate thresholds are set to 0 at speed 0 to
    // disable the early termination at that speed.
    if (best_rdc->dist < dist_breakout_thr &&
        best_rdc->rate < rate_breakout_thr) {
#if !CONFIG_EXT_RECUR_PARTITIONS
      part_search_state->do_square_split = 0;
#endif
      part_search_state->do_rectangular_split = 0;
    }
  }

  // Early termination: using simple_motion_search features and the
  // rate, distortion, and rdcost of PARTITION_NONE, a DNN will make a
  // decision on early terminating at PARTITION_NONE.
  bool is_early_term_allowed =
      cpi->sf.part_sf.simple_motion_search_early_term_none &&
      !frame_is_intra_only(cm) && bsize >= BLOCK_16X16 &&
      blk_params.mi_row_edge < mi_params->mi_rows &&
      blk_params.mi_col_edge < mi_params->mi_cols &&
      this_rdc->rdcost < INT64_MAX && this_rdc->rdcost >= 0 &&
      this_rdc->rate < INT_MAX && this_rdc->rate >= 0;
#if CONFIG_EXT_RECUR_PARTITIONS
  is_early_term_allowed &= part_search_state->do_rectangular_split && sms_tree;
#else
  is_early_term_allowed &=
      cm->show_frame && (part_search_state->do_square_split ||
                         part_search_state->do_rectangular_split);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  if (is_early_term_allowed) {
    av1_simple_motion_search_early_term_none(
        cpi, x, sms_tree, blk_params.mi_row, blk_params.mi_col, bsize, this_rdc,
        &part_search_state->terminate_partition_search);
  }
}

#if !CONFIG_EXT_RECUR_PARTITIONS
// Decide early termination and rectangular partition pruning
// based on PARTITION_NONE and PARTITION_SPLIT costs.
static void prune_partitions_after_split(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    int64_t part_none_rd, int64_t part_split_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

#if CONFIG_EXT_RECUR_PARTITIONS
  (void)sms_tree;
  (void)part_none_rd;
  (void)part_split_rd;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#if !CONFIG_EXT_RECUR_PARTITIONS
  // Early termination: using the rd costs of PARTITION_NONE and subblocks
  // from PARTITION_SPLIT to determine an early breakout.
  if (cpi->sf.part_sf.ml_early_term_after_part_split_level &&
      !frame_is_intra_only(cm) &&
      !part_search_state->terminate_partition_search &&
      part_search_state->do_rectangular_split &&
      (part_search_state->partition_rect_allowed[HORZ] ||
       part_search_state->partition_rect_allowed[VERT])) {
    av1_ml_early_term_after_split(
        cpi, x, sms_tree, bsize, best_rdc->rdcost, part_none_rd, part_split_rd,
        part_search_state->split_rd, mi_row, mi_col,
        &part_search_state->terminate_partition_search);
  }
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  // Use the rd costs of PARTITION_NONE and subblocks from PARTITION_SPLIT
  // to prune out rectangular partitions in some directions.
  if (!cpi->sf.part_sf.ml_early_term_after_part_split_level &&
      cpi->sf.part_sf.ml_prune_rect_partition && !frame_is_intra_only(cm) &&
      (part_search_state->partition_rect_allowed[HORZ] ||
       part_search_state->partition_rect_allowed[VERT]) &&
      !(part_search_state->prune_rect_part[HORZ] ||
        part_search_state->prune_rect_part[VERT]) &&
      !part_search_state->terminate_partition_search) {
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, av1_num_planes(cm),
                         NULL);
    av1_ml_prune_rect_partition(
        cpi, x, bsize, best_rdc->rdcost, part_search_state->none_rd,
        part_search_state->split_rd, &part_search_state->prune_rect_part[HORZ],
        &part_search_state->prune_rect_part[VERT]);
  }
}
#endif

#if CONFIG_EXTENDED_SDP
static void inter_sdp_copy_luma_mode_info(PC_TREE *pc_tree,
                                          PICK_MODE_CONTEXT *ctx_chroma_none,
                                          MB_MODE_INFO *mbmi) {
  PC_TREE *cur_pc_tree = pc_tree;
  PARTITION_TYPE partition = cur_pc_tree->partitioning;
  while (partition) {
    switch (partition) {
      case PARTITION_HORZ:
        cur_pc_tree = cur_pc_tree->horizontal[INTRA_REGION][0];
        break;
      case PARTITION_VERT:
        cur_pc_tree = cur_pc_tree->vertical[INTRA_REGION][0];
        break;
      case PARTITION_SPLIT:
        cur_pc_tree = cur_pc_tree->split[INTRA_REGION][0];
        break;
      case PARTITION_HORZ_4A:
        cur_pc_tree = cur_pc_tree->horizontal4a[INTRA_REGION][0];
        break;
      case PARTITION_HORZ_4B:
        cur_pc_tree = cur_pc_tree->horizontal4b[INTRA_REGION][0];
        break;
      case PARTITION_VERT_4A:
        cur_pc_tree = cur_pc_tree->vertical4a[INTRA_REGION][0];
        break;
      case PARTITION_VERT_4B:
        cur_pc_tree = cur_pc_tree->vertical4b[INTRA_REGION][0];
        break;
      case PARTITION_VERT_3:
        cur_pc_tree = cur_pc_tree->vertical3[INTRA_REGION][0];
        break;
      case PARTITION_HORZ_3:
        cur_pc_tree = cur_pc_tree->horizontal3[INTRA_REGION][0];
        break;
      default: assert(0 && "Invalid partition type!"); break;
    }
    partition = cur_pc_tree->partitioning;
  }
  ctx_chroma_none->mic = cur_pc_tree->none[INTRA_REGION]->mic;
  *mbmi = cur_pc_tree->none[INTRA_REGION]->mic;
}
#endif  // CONFIG_EXTENDED_SDP

// PARTITION_NONE search.
static void none_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data, MACROBLOCK *x,
    PC_TREE *pc_tree, SIMPLE_MOTION_DATA_TREE *sms_tree,
    RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    unsigned int *pb_source_variance, int64_t *none_rd, int64_t *part_none_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    ,
    LevelBanksRDO *level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  RD_STATS *this_rdc = &part_search_state->this_rdc;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);

#if CONFIG_EXT_RECUR_PARTITIONS
  if (is_part_pruned_by_forced_partition(part_search_state, PARTITION_NONE)) {
    return;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXTENDED_SDP
  int sdp_inter_chroma_flag =
      is_inter_sdp_chroma(cm, pc_tree->region_type, x->e_mbd.tree_type);
#endif  // CONFIG_EXTENDED_SDP
  // Set PARTITION_NONE allowed flag.
  set_part_none_allowed_flag(cpi,
#if CONFIG_EXT_RECUR_PARTITIONS
                             x->e_mbd.tree_type,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
                             sdp_inter_chroma_flag,
#endif  // CONFIG_EXTENDED_SDP
                             part_search_state);
  if (!part_search_state->partition_none_allowed) {
    return;
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  if (part_search_state->prune_partition_none
#if CONFIG_EXTENDED_SDP
      && !sdp_inter_chroma_flag
#endif  // CONFIG_EXTENDED_SDP
  ) {
    return;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  int pt_cost = 0;
  RD_STATS best_remain_rdcost;

  // Set PARTITION_NONE context and cost.
  set_none_partition_params(cm, td, x, pc_tree, part_search_state,
                            &best_remain_rdcost, best_rdc, &pt_cost);

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
  PICK_MODE_CONTEXT *ctx_none = sdp_inter_chroma_flag
                                    ? pc_tree->none_chroma
                                    : pc_tree->none[pc_tree->region_type];
  if (sdp_inter_chroma_flag) {
    inter_sdp_copy_luma_mode_info(pc_tree, ctx_none, x->e_mbd.mi[0]);
  }
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_COLLECT_PARTITION_STATS
  // Timer start for partition None.
  if (best_remain_rdcost >= 0) {
    partition_attempts[PARTITION_NONE] += 1;
    aom_usec_timer_start(&partition_timer);
    partition_timer_on = 1;
  }
#endif
#if CONFIG_EXT_RECUR_PARTITIONS
  SimpleMotionData *sms_data =
      av1_get_sms_data_entry(x->sms_bufs, mi_row, mi_col, bsize, cm->sb_size);
  av1_set_best_mode_cache(x, sms_data->mode_cache);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // PARTITION_NONE evaluation and cost update.
  pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, this_rdc, PARTITION_NONE,
#if CONFIG_EXTENDED_SDP
                pc_tree->region_type,
#endif  // CONFIG_EXTENDED_SDP
                bsize,
#if CONFIG_EXTENDED_SDP
                ctx_none,
#else
                pc_tree->none,
#endif  // CONFIG_EXTENDED_SDP
                best_remain_rdcost);
#if CONFIG_EXT_RECUR_PARTITIONS
  x->inter_mode_cache = NULL;
  if (this_rdc->rate != INT_MAX
#if CONFIG_EXTENDED_SDP
      && !is_inter_sdp_chroma(cm, cur_region_type, x->e_mbd.tree_type)
#endif  // CONFIG_EXTENDED_SDP
  ) {
    av1_add_mode_search_context_to_cache(sms_data,
#if CONFIG_EXTENDED_SDP
                                         pc_tree->none[pc_tree->region_type]
#else
                                         pc_tree->none
#endif  // CONFIG_EXTENDED_SDP
    );
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  av1_rd_cost_update(x->rdmult, this_rdc);

#if CONFIG_COLLECT_PARTITION_STATS
  // Timer end for partition None.
  if (partition_timer_on) {
    aom_usec_timer_mark(&partition_timer);
    int64_t time = aom_usec_timer_elapsed(&partition_timer);
    partition_times[PARTITION_NONE] += time;
    partition_timer_on = 0;
  }
#endif
  *pb_source_variance = x->source_variance;
  if (none_rd) *none_rd = this_rdc->rdcost;
  part_search_state->none_rd = this_rdc->rdcost;
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION ||
      frame_is_intra_only(cm))
#endif  // CONFIG_EXTENDED_SDP
    pc_tree->none_rd = *this_rdc;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  if (this_rdc->rate != INT_MAX) {
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
    pc_tree->skippable = sdp_inter_chroma_flag
                             ? pc_tree->none_chroma->skippable
                             : pc_tree->none[pc_tree->region_type]->skippable;
#else
    pc_tree->skippable = pc_tree->none->skippable;
#endif  // CONFIG_EXTENDED_SDP
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    // Record picked ref frame to prune ref frames for other partition types.
    if (cpi->sf.inter_sf.prune_ref_frames
#if CONFIG_EXTENDED_SDP
        && x->e_mbd.tree_type != CHROMA_PART
#endif  // CONFIG_EXTENDED_SDP
    ) {
#if CONFIG_EXTENDED_SDP
      const int ref_type = av1_ref_frame_type(
          pc_tree->none[pc_tree->region_type]->mic.ref_frame);
#else
      const int ref_type = av1_ref_frame_type(pc_tree->none->mic.ref_frame);
#endif  // CONFIG_EXTENDED_SDP
      av1_update_picked_ref_frames_mask(x, ref_type, bsize, cm->mib_size,
                                        mi_row, mi_col);
    }

    // Calculate the total cost and update the best partition.
    if (part_search_state->is_block_splittable
#if CONFIG_EXTENDED_SDP
        && !sdp_inter_chroma_flag
#endif  // CONFIG_EXTENDED_SDP
    ) {
      this_rdc->rate += pt_cost;
      this_rdc->rdcost = RDCOST(x->rdmult, this_rdc->rate, this_rdc->dist);
    }
    *part_none_rd = this_rdc->rdcost;
    if (this_rdc->rdcost < best_rdc->rdcost) {
      *best_rdc = *this_rdc;
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      part_search_state->found_best_partition = true;
#if !CONFIG_EXT_RECUR_PARTITIONS
      if (blk_params.bsize_at_least_8x8) {
        pc_tree->partitioning = PARTITION_NONE;
      }
#else
#if CONFIG_EXTENDED_SDP
      if (!sdp_inter_chroma_flag)
#endif  // CONFIG_EXTENDED_SDP
        pc_tree->partitioning = PARTITION_NONE;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

      // Disable split and rectangular partition search
      // based on PARTITION_NONE cost.
#if CONFIG_EXTENDED_SDP
      if (frame_is_intra_only(cm) || cur_region_type != INTRA_REGION ||
          x->e_mbd.tree_type != CHROMA_PART)
#endif  // CONFIG_EXTENDED_SDP
        prune_partitions_after_none(cpi, x, sms_tree,
#if CONFIG_EXTENDED_SDP
                                    pc_tree->none[pc_tree->region_type],
#else
                                  pc_tree->none,
#endif  // CONFIG_EXTENDED_SDP
                                    part_search_state, best_rdc,
                                    pb_source_variance);
    }
  }
  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

// PARTITION_SPLIT search.
static void split_partition_search(
    AV1_COMP *const cpi, ThreadData *td, TileDataEnc *tile_data,
    TokenExtra **tp, MACROBLOCK *x, PC_TREE *pc_tree,
    SIMPLE_MOTION_DATA_TREE *sms_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state, RD_STATS *best_rdc,
    SB_MULTI_PASS_MODE multi_pass_mode, int64_t *part_split_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    ,
    LevelBanksRDO *level_banks
#endif  // CONFIG_C043_MVP_IMPROVEMENTS
#if CONFIG_EXT_RECUR_PARTITIONS
    ,
    const PARTITION_TREE *ptree_luma, const PARTITION_TREE *template_tree,
    int max_recursion_depth
#endif  // CONFIG_EXT_RECUR_PARTITIONS
) {
  const AV1_COMMON *const cm = &cpi->common;
  PartitionBlkParams blk_params = part_search_state->part_blk_params;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = blk_params.mi_row;
  const int mi_col = blk_params.mi_col;
  const BLOCK_SIZE bsize = blk_params.bsize;
  assert(bsize < BLOCK_SIZES_ALL);
  RD_STATS sum_rdc = part_search_state->sum_rdc;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);

  // Check if partition split is allowed.
#if CONFIG_EXT_RECUR_PARTITIONS
  (void)sms_tree;
  if (part_search_state->terminate_partition_search ||
      !is_square_split_eligible(bsize, cm->sb_size)) {
    return;
  }
  if (part_search_state->forced_partition != PARTITION_INVALID &&
      part_search_state->forced_partition != PARTITION_SPLIT) {
    return;
  }
#if CONFIG_ML_PART_SPLIT
  if (part_search_state->prune_partition_split) {
    return;
  }
#endif  // CONFIG_ML_PART_SPLIT
  if (max_recursion_depth < 0) {
    return;
  }

  const int num_planes = av1_num_planes(cm);
#if CONFIG_EXTENDED_SDP
  PC_TREE **sub_tree = pc_tree->split[pc_tree->region_type];
#else
  PC_TREE **sub_tree = pc_tree->split;
#endif  // CONFIG_EXTENDED_SDP
  assert(sub_tree);
  for (int idx = 0; idx < SUB_PARTITIONS_SPLIT; idx++) {
    if (sub_tree[idx]) {
      av1_free_pc_tree_recursive(sub_tree[idx], num_planes, 0, 0);
      sub_tree[idx] = NULL;
    }
  }

  const MACROBLOCKD *const xd = &x->e_mbd;
  const int track_ptree_luma =
      is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma, bsize);
#else
  if (part_search_state->terminate_partition_search ||
      !part_search_state->do_square_split)
    return;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Initialization of this partition RD stats.
  av1_init_rd_stats(&sum_rdc);
  sum_rdc.rate = part_search_state->partition_cost[PARTITION_SPLIT];
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  int idx;
#if CONFIG_COLLECT_PARTITION_STATS
  if (best_rdc->rdcost - sum_rdc.rdcost >= 0) {
    partition_attempts[PARTITION_SPLIT] += 1;
    aom_usec_timer_start(&partition_timer);
    partition_timer_on = 1;
  }
#endif
  // Recursive partition search on 4 sub-blocks.
  for (idx = 0; idx < SUB_PARTITIONS_SPLIT && sum_rdc.rdcost < best_rdc->rdcost;
       ++idx) {
    const int x_idx = (idx & 1) * blk_params.mi_step;
    const int y_idx = (idx >> 1) * blk_params.mi_step;

    if (mi_row + y_idx >= mi_params->mi_rows ||
        mi_col + x_idx >= mi_params->mi_cols)
      continue;
#if CONFIG_EXTENDED_SDP
    if (pc_tree->split[pc_tree->region_type][idx] == NULL) {
#else
    if (pc_tree->split[idx] == NULL) {
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXTENDED_SDP
      pc_tree->split[pc_tree->region_type][idx] = av1_alloc_pc_tree_node(
#else
      pc_tree->split[idx] = av1_alloc_pc_tree_node(
#endif
          x->e_mbd.tree_type, mi_row + y_idx, mi_col + x_idx, subsize, pc_tree,
          PARTITION_SPLIT, idx, idx == 3, part_search_state->ss_x,
          part_search_state->ss_y);
    }
#if !CONFIG_EXT_RECUR_PARTITIONS
    int64_t *p_split_rd = &part_search_state->split_rd[idx];
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    RD_STATS best_remain_rdcost;
    av1_rd_stats_subtraction(x->rdmult, best_rdc, &sum_rdc,
                             &best_remain_rdcost);

    int curr_quad_tree_idx = 0;
    if (frame_is_intra_only(cm) && bsize <= BLOCK_64X64) {
      curr_quad_tree_idx = part_search_state->intra_part_info->quad_tree_idx;
      part_search_state->intra_part_info->quad_tree_idx =
          4 * curr_quad_tree_idx + idx + 1;
    }
    // Split partition evaluation of corresponding idx.
    // If the RD cost exceeds the best cost then do not
    // evaluate other split sub-partitions.
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_ML_PART_SPLIT
    int force_prune_flags[3] = { 0, 0, 0 };
#endif  // CONFIG_ML_PART_SPLIT
    if (!av1_rd_pick_partition(
            cpi, td, tile_data, tp, mi_row + y_idx, mi_col + x_idx, subsize,
#if CONFIG_EXTENDED_SDP
            PARTITION_SPLIT,
#endif  // CONFIG_EXTENDED_SDP
            &part_search_state->this_rdc, best_remain_rdcost, sub_tree[idx],
            track_ptree_luma ? ptree_luma->sub_tree[idx] : NULL,
            get_partition_subtree_const(template_tree, idx),
            max_recursion_depth, NULL, NULL, multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
            ,
            force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
            )) {
      break;
    }
#else
    if (!av1_rd_pick_partition(
            cpi, td, tile_data, tp, mi_row + y_idx, mi_col + x_idx, subsize,
            &part_search_state->this_rdc, best_remain_rdcost,
            pc_tree->split[idx], sms_tree->split[idx], p_split_rd,
            multi_pass_mode, &part_search_state->split_part_rect_win[idx])) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    if (frame_is_intra_only(cm) && bsize <= BLOCK_64X64) {
      part_search_state->intra_part_info->quad_tree_idx = curr_quad_tree_idx;
    }

    sum_rdc.rate += part_search_state->this_rdc.rate;
    sum_rdc.dist += part_search_state->this_rdc.dist;
    av1_rd_cost_update(x->rdmult, &sum_rdc);

    // Set split ctx as ready for use.
    if (idx <= 1 && (bsize <= BLOCK_8X8 ||
#if CONFIG_EXTENDED_SDP
                     pc_tree->split[pc_tree->region_type][idx]->partitioning ==
                         PARTITION_NONE
#else
                     pc_tree->split[idx]->partitioning == PARTITION_NONE
#endif  // CONFIG_EXTENDED_SDP
                     )) {
#if CONFIG_EXTENDED_SDP
      const MB_MODE_INFO *const mbmi =
          &pc_tree->split[pc_tree->region_type][idx]
               ->none[pc_tree->region_type]
               ->mic;
#else
      const MB_MODE_INFO *const mbmi = &pc_tree->split[idx]->none->mic;
#endif  // CONFIG_EXTENDED_SDP
      const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
      // Neither palette mode nor cfl predicted.
      if (pmi->palette_size[0] == 0 && pmi->palette_size[1] == 0) {
        if (mbmi->uv_mode != UV_CFL_PRED)
          part_search_state->is_split_ctx_is_ready[idx] = 1;
      }
    }
  }
#if CONFIG_COLLECT_PARTITION_STATS
  if (partition_timer_on) {
    aom_usec_timer_mark(&partition_timer);
    int64_t time = aom_usec_timer_elapsed(&partition_timer);
    partition_times[PARTITION_SPLIT] += time;
    partition_timer_on = 0;
  }
#endif
  const int reached_last_index = (idx == SUB_PARTITIONS_SPLIT);

  // Calculate the total cost and update the best partition.
  *part_split_rd = sum_rdc.rdcost;
  if (reached_last_index && sum_rdc.rdcost < best_rdc->rdcost) {
    sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, sum_rdc.dist);
    if (sum_rdc.rdcost < best_rdc->rdcost) {
      *best_rdc = sum_rdc;
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      part_search_state->found_best_partition = true;
      pc_tree->partitioning = PARTITION_SPLIT;
    }
  } else if (cpi->sf.part_sf.less_rectangular_check_level > 0) {
#if !CONFIG_EXT_RECUR_PARTITIONS
    // Skip rectangular partition test when partition type none gives better
    // rd than partition type split.
    if (cpi->sf.part_sf.less_rectangular_check_level == 2 || idx <= 2) {
      const int partition_none_valid = part_search_state->none_rd > 0;
      const int partition_none_better =
          part_search_state->none_rd < sum_rdc.rdcost;
      part_search_state->do_rectangular_split &=
          !(partition_none_valid && partition_none_better);
    }
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  }
  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, av1_num_planes(cm));
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

#if CONFIG_EXT_RECUR_PARTITIONS
/*!\brief Stores some data used by rd_try_subblock_new to do rdopt. */
typedef struct SUBBLOCK_RDO_DATA {
  /*!\brief The encoder side partition tree. */
  PC_TREE *pc_tree;
  /*!\brief The luma partition tree. Used by SDP on chroma planes. */
  const PARTITION_TREE *ptree_luma;
  /*!\brief A "template" that the function will follow to skip the partition
   * selection process. */
  const PARTITION_TREE *template_tree;
  /*!\brief The row coordinate of current block in units of mi. */
  int mi_row;
  /*!\brief The col coordinate of current block in units of mi. */
  int mi_col;
  /*!\brief The block_size of the current block. */
  BLOCK_SIZE bsize;
  /*!\brief The partition type used to get the current block. */
  PARTITION_TYPE partition;
} SUBBLOCK_RDO_DATA;

/*!\brief Whether the current partition node uses horizontal type partitions. */
static AOM_INLINE bool node_uses_horz(const PC_TREE *pc_tree) {
  assert(pc_tree);
  return pc_tree->partitioning == PARTITION_HORZ ||
         pc_tree->partitioning == PARTITION_HORZ_4A ||
         pc_tree->partitioning == PARTITION_HORZ_4B ||
         pc_tree->partitioning == PARTITION_HORZ_3;
}

/*!\brief Whether the current partition node uses vertical type partitions. */
static AOM_INLINE bool node_uses_vert(const PC_TREE *pc_tree) {
  assert(pc_tree);
  return pc_tree->partitioning == PARTITION_VERT ||
         pc_tree->partitioning == PARTITION_VERT_4A ||
         pc_tree->partitioning == PARTITION_VERT_4B ||
         pc_tree->partitioning == PARTITION_VERT_3;
}

/*!\brief Try searching for an encoding for the given subblock.
 *
 * Returns zero if the rdcost is already too high (to tell the caller not to
 * bother searching for encodings of further subblocks).
 * */
static int rd_try_subblock_new(AV1_COMP *const cpi, ThreadData *td,
                               TileDataEnc *tile_data, TokenExtra **tp,
                               SUBBLOCK_RDO_DATA *rdo_data,
                               RD_STATS best_rdcost, RD_STATS *sum_rdc,
                               SB_MULTI_PASS_MODE multi_pass_mode,
                               bool *skippable, int max_recursion_depth) {
  MACROBLOCK *const x = &td->mb;
  const int orig_mult = x->rdmult;
  const int mi_row = rdo_data->mi_row;
  const int mi_col = rdo_data->mi_col;
  const BLOCK_SIZE bsize = rdo_data->bsize;

  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);

  av1_rd_cost_update(x->rdmult, &best_rdcost);

  RD_STATS rdcost_remaining;
  av1_rd_stats_subtraction(x->rdmult, &best_rdcost, sum_rdc, &rdcost_remaining);
  RD_STATS this_rdc;

#if CONFIG_ML_PART_SPLIT
  int force_prune_flags[3] = { 0, 0, 0 };
#endif  // CONFIG_ML_PART_SPLIT
  if (!av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
#if CONFIG_EXTENDED_SDP
                             rdo_data->partition,
#endif  // CONFIG_EXTENDED_SDP
                             &this_rdc, rdcost_remaining, rdo_data->pc_tree,
                             rdo_data->ptree_luma, rdo_data->template_tree,
                             max_recursion_depth, NULL, NULL, multi_pass_mode,
                             NULL
#if CONFIG_ML_PART_SPLIT
                             ,
                             force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
                             )) {
    av1_invalid_rd_stats(sum_rdc);
    return 0;
  }

  if (this_rdc.rate == INT_MAX) {
    *skippable = false;
    sum_rdc->rdcost = INT64_MAX;
  } else {
    *skippable &= rdo_data->pc_tree->skippable;
    sum_rdc->rate += this_rdc.rate;
    sum_rdc->dist += this_rdc.dist;
    av1_rd_cost_update(x->rdmult, sum_rdc);
  }

  if (sum_rdc->rdcost >= best_rdcost.rdcost) {
    x->rdmult = orig_mult;
    return 0;
  }

  x->rdmult = orig_mult;
  return 1;
}

/*!\brief Trace out the partition boundaries using the structure in pc_tree.
 *
 * The results are stored in partition_boundaries. The array
 * partition_boundaries has a stride of MAX_MIB_SIZE, and the units are in mi.
 * The actual values stored is a bitmask, with 1 << HORZ means that there is a
 * horizontal boundary, and 1 << VERT means that there is a vertical boundary.
 * */
static AOM_INLINE void trace_partition_boundary(bool *partition_boundaries,
                                                const PC_TREE *pc_tree,
                                                int mi_row, int mi_col,
                                                BLOCK_SIZE bsize) {
  mi_row &= MAX_MIB_MASK;
  mi_col &= MAX_MIB_MASK;
  const PARTITION_TYPE partition = pc_tree->partitioning;
  assert(bsize < BLOCK_SIZES_ALL);
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP
  switch (partition) {
    case PARTITION_NONE:
      for (int col = 0; col < mi_width; col++) {
        partition_boundaries[(mi_row + mi_height - 1) * MAX_MIB_SIZE +
                             (mi_col + col)] |= (1 << HORZ);
      }
      for (int row = 0; row < mi_height; row++) {
        partition_boundaries[(mi_row + row) * MAX_MIB_SIZE + mi_col + mi_width -
                             1] |= (1 << VERT);
      }
      break;
    case PARTITION_HORZ:
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal[cur_region_type][0],
#else
                               pc_tree->horizontal[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col,
                               get_partition_subsize(bsize, PARTITION_HORZ));
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal[cur_region_type][1],
#else
                               pc_tree->horizontal[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + mi_height / 2, mi_col,
                               get_partition_subsize(bsize, PARTITION_HORZ));
      break;
    case PARTITION_VERT:
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical[cur_region_type][0],
#else
                               pc_tree->vertical[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col,
                               get_partition_subsize(bsize, PARTITION_VERT));
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical[cur_region_type][1],
#else
                               pc_tree->vertical[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + mi_width / 2,
                               get_partition_subsize(bsize, PARTITION_VERT));
      break;
    case PARTITION_HORZ_3:
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal3[cur_region_type][0],
#else
          pc_tree->horizontal3[0],
#endif  // CONFIG_EXTENDED_SDP
          mi_row, mi_col, get_h_partition_subsize(bsize, 0, PARTITION_HORZ_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal3[cur_region_type][1],
#else
          pc_tree->horizontal3[1],
#endif  // CONFIG_EXTENDED_SDP
          mi_row + mi_height / 4, mi_col,
          get_h_partition_subsize(bsize, 1, PARTITION_HORZ_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal3[cur_region_type][2],
#else
          pc_tree->horizontal3[2],
#endif  // CONFIG_EXTENDED_SDP
          mi_row + mi_height / 4, mi_col + mi_width / 2,
          get_h_partition_subsize(bsize, 1, PARTITION_HORZ_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal3[cur_region_type][3],
#else
          pc_tree->horizontal3[3],
#endif  // CONFIG_EXTENDED_SDP
          mi_row + 3 * mi_height / 4, mi_col,
          get_h_partition_subsize(bsize, 0, PARTITION_HORZ_3));
      break;
    case PARTITION_VERT_3:
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical3[cur_region_type][0],
#else
          pc_tree->vertical3[0],
#endif  // CONFIG_EXTENDED_SDP
          mi_row, mi_col, get_h_partition_subsize(bsize, 0, PARTITION_VERT_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical3[cur_region_type][1],
#else
          pc_tree->vertical3[1],
#endif  // CONFIG_EXTENDED_SDP
          mi_row, mi_col + mi_width / 4,
          get_h_partition_subsize(bsize, 1, PARTITION_VERT_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical3[cur_region_type][2],
#else
          pc_tree->vertical3[2],
#endif  // CONFIG_EXTENDED_SDP
          mi_row + mi_height / 2, mi_col + mi_width / 4,
          get_h_partition_subsize(bsize, 1, PARTITION_VERT_3));
      trace_partition_boundary(
          partition_boundaries,
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical3[cur_region_type][3],
#else
          pc_tree->vertical3[3],
#endif  // CONFIG_EXTENDED_SDP
          mi_row, mi_col + 3 * mi_width / 4,
          get_h_partition_subsize(bsize, 0, PARTITION_VERT_3));
      break;
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4a[cur_region_type][0],
#else
                               pc_tree->horizontal4a[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col, subsize);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4a[cur_region_type][1],
#else
                               pc_tree->horizontal4a[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + ebs_h, mi_col, bsize_med);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4a[cur_region_type][2],
#else
                               pc_tree->horizontal4a[2],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + 3 * ebs_h, mi_col, bsize_big);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4a[cur_region_type][3],
#else
                               pc_tree->horizontal4a[3],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4b[cur_region_type][0],
#else
                               pc_tree->horizontal4b[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col, subsize);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4b[cur_region_type][1],
#else
                               pc_tree->horizontal4b[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + ebs_h, mi_col, bsize_big);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4b[cur_region_type][2],
#else
                               pc_tree->horizontal4b[2],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + 5 * ebs_h, mi_col, bsize_med);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->horizontal4b[cur_region_type][3],
#else
                               pc_tree->horizontal4b[3],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4a[cur_region_type][0],
#else
                               pc_tree->vertical4a[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col, subsize);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4a[cur_region_type][1],
#else
                               pc_tree->vertical4a[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + ebs_w, bsize_med);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4a[cur_region_type][2],
#else
                               pc_tree->vertical4a[2],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + 3 * ebs_w, bsize_big);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4a[cur_region_type][3],
#else
                               pc_tree->vertical4a[3],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + 7 * ebs_w, subsize);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4b[cur_region_type][0],
#else
                               pc_tree->vertical4b[0],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col, subsize);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4b[cur_region_type][1],
#else
                               pc_tree->vertical4b[1],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + ebs_w, bsize_big);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4b[cur_region_type][2],
#else
                               pc_tree->vertical4b[2],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + 5 * ebs_w, bsize_med);
      trace_partition_boundary(partition_boundaries,
#if CONFIG_EXTENDED_SDP
                               pc_tree->vertical4b[cur_region_type][3],
#else
                               pc_tree->vertical4b[3],
#endif  // CONFIG_EXTENDED_SDP
                               mi_row, mi_col + 7 * ebs_w, subsize);
      break;
    }
    default: assert(0 && "Invalid partition type in trace_partition_boundary!");
  }
}

/*!\brief Prunes h partitions using the current best partition boundaries.
 *
 * If the H-shaped partitions don't have any overlap with the current best
 * partition boundaries, then they are pruned from the search.
 * */
static AOM_INLINE void prune_part_3_with_partition_boundary(
    PartitionSearchState *part_search_state, BLOCK_SIZE bsize, int mi_row,
    int mi_col, bool can_search_horz, bool can_search_vert) {
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  const int masked_mi_row = mi_row & MAX_MIB_MASK;
  const int masked_mi_col = mi_col & MAX_MIB_MASK;
  const bool *partition_boundaries = part_search_state->partition_boundaries;
  if (can_search_horz) {
    bool keep_horz_3 = false;
    for (int col = 0; col < mi_width; col++) {
      if (partition_boundaries[(masked_mi_row + mi_height / 4 - 1) *
                                   MAX_MIB_SIZE +
                               masked_mi_col + col] &
          (1 << HORZ)) {
        keep_horz_3 = true;
        break;
      }
    }
    if (!keep_horz_3) {
      for (int col = 0; col < mi_width; col++) {
        if (partition_boundaries[(masked_mi_row + 3 * mi_height / 4 - 1) *
                                     MAX_MIB_SIZE +
                                 masked_mi_col + col] &
            (1 << HORZ)) {
          keep_horz_3 = true;
          break;
        }
      }
    }
    if (!keep_horz_3) {
      for (int row = 0; row < mi_height / 2; row++) {
        if (partition_boundaries[(masked_mi_row + mi_height / 4 + row) *
                                     MAX_MIB_SIZE +
                                 masked_mi_col + mi_width / 2 - 1] &
            (1 << VERT)) {
          keep_horz_3 = true;
          break;
        }
      }
    }
    part_search_state->prune_partition_3[HORZ] |= !keep_horz_3;
  }
  if (can_search_vert) {
    bool keep_vert_3 = false;
    for (int row = 0; row < mi_height; row++) {
      if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                               masked_mi_col + mi_width / 4 - 1] &
          (1 << VERT)) {
        keep_vert_3 = true;
        break;
      }
    }
    if (!keep_vert_3) {
      for (int row = 0; row < mi_height; row++) {
        if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                                 masked_mi_col + 3 * mi_width / 4 - 1] &
            (1 << VERT)) {
          keep_vert_3 = true;
          break;
        }
      }
    }
    if (!keep_vert_3) {
      for (int col = 0; col < mi_width / 2; col++) {
        if (partition_boundaries[(masked_mi_row + mi_height / 2 - 1) *
                                     MAX_MIB_SIZE +
                                 masked_mi_col + mi_width / 4 + col] &
            (1 << HORZ)) {
          keep_vert_3 = true;
          break;
        }
      }
    }
    part_search_state->prune_partition_3[VERT] |= !keep_vert_3;
  }
}

/*!\brief Prunes 4-way partitions using the current best partition boundaries.
 *
 * If the 4-way partitions don't have any overlap with the current best
 * partition boundaries, then they are pruned from the search.
 */
static AOM_INLINE void prune_part_4_with_partition_boundary(
    PartitionSearchState *part_search_state, const bool *partition_boundaries,
    BLOCK_SIZE bsize, int mi_row, int mi_col, bool can_search_horz_4a,
    bool can_search_horz_4b, bool can_search_vert_4a, bool can_search_vert_4b) {
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  const int masked_mi_row = mi_row & MAX_MIB_MASK;
  const int masked_mi_col = mi_col & MAX_MIB_MASK;
  bool keep_horz_4a = false, keep_horz_4b = false;
  bool keep_vert_4a = false, keep_vert_4b = false;
  if (can_search_horz_4a || can_search_horz_4b) {
    for (int col = 0; col < mi_width; col++) {
      if (partition_boundaries[(masked_mi_row + mi_height / 8 - 1) *
                                   MAX_MIB_SIZE +
                               masked_mi_col + col] &
          (1 << HORZ)) {
        keep_horz_4a = true;
        keep_horz_4b = true;
        break;
      }
      if (partition_boundaries[(masked_mi_row + 7 * mi_height / 8 - 1) *
                                   MAX_MIB_SIZE +
                               masked_mi_col + col] &
          (1 << HORZ)) {
        keep_horz_4a = true;
        keep_horz_4b = true;
        break;
      }
    }
    if (can_search_horz_4a && !keep_horz_4a) {
      for (int col = 0; col < mi_width; col++) {
        if (partition_boundaries[(masked_mi_row + 3 * mi_height / 8 - 1) *
                                     MAX_MIB_SIZE +
                                 masked_mi_col + col] &
            (1 << HORZ)) {
          keep_horz_4a = true;
          break;
        }
      }
    }
    if (can_search_horz_4b && !keep_horz_4b) {
      for (int col = 0; col < mi_width; col++) {
        if (partition_boundaries[(masked_mi_row + 5 * mi_height / 8 - 1) *
                                     MAX_MIB_SIZE +
                                 masked_mi_col + col] &
            (1 << HORZ)) {
          keep_horz_4b = true;
          break;
        }
      }
    }
    part_search_state->prune_partition_4a[HORZ] |= !keep_horz_4a;
    part_search_state->prune_partition_4b[HORZ] |= !keep_horz_4b;
  }
  if (can_search_vert_4a || can_search_vert_4b) {
    for (int row = 0; row < mi_height; row++) {
      if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                               masked_mi_col + mi_width / 8 - 1] &
          (1 << VERT)) {
        keep_vert_4a = true;
        keep_vert_4b = true;
        break;
      }
      if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                               masked_mi_col + 7 * mi_width / 8 - 1] &
          (1 << VERT)) {
        keep_vert_4a = true;
        keep_vert_4b = true;
        break;
      }
    }
    if (can_search_vert_4a && !keep_vert_4a) {
      for (int row = 0; row < mi_height; row++) {
        if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                                 masked_mi_col + 3 * mi_width / 8 - 1] &
            (1 << VERT)) {
          keep_vert_4a = true;
          break;
        }
      }
    }
    if (can_search_vert_4b && !keep_vert_4b) {
      for (int row = 0; row < mi_height; row++) {
        if (partition_boundaries[(masked_mi_row + row) * MAX_MIB_SIZE +
                                 masked_mi_col + 5 * mi_width / 8 - 1] &
            (1 << VERT)) {
          keep_vert_4b = true;
          break;
        }
      }
    }
    part_search_state->prune_partition_4a[VERT] |= !keep_vert_4a;
    part_search_state->prune_partition_4b[VERT] |= !keep_vert_4b;
  }
}

// Pruning logic for PARTITION_HORZ_3 and PARTITION_VERT_3.
static AOM_INLINE void prune_ext_partitions_3way(
    AV1_COMP *const cpi, PC_TREE *pc_tree,
    PartitionSearchState *part_search_state, bool *partition_boundaries) {
  const AV1_COMMON *const cm = &cpi->common;
  const PARTITION_SPEED_FEATURES *part_sf = &cpi->sf.part_sf;
  const PARTITION_TYPE forced_partition = part_search_state->forced_partition;
  if (part_search_state->forced_partition != PARTITION_INVALID) {
    return;
  }

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  // Prune horz 3 with speed features
  if (part_search_state->partition_3_allowed[HORZ] &&
      !frame_is_intra_only(cm) && forced_partition != PARTITION_HORZ_3) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_3[HORZ] = 1;
    }
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but the subtrees did not further
      // split in horz
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal[cur_region_type][0] &&
          pc_tree->horizontal[cur_region_type][1] &&
          !node_uses_horz(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->horizontal[cur_region_type][1])
#else
          !node_uses_horz(pc_tree->horizontal[0]) &&
          !node_uses_horz(pc_tree->horizontal[1])
#endif  // CONFIG_EXTENDED_SDP
      ) {
        part_search_state->prune_partition_3[HORZ] = 1;
      }
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical[cur_region_type][0] &&
          pc_tree->vertical[cur_region_type][1] &&
          !node_uses_horz(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->vertical[cur_region_type][1])
#else
          !node_uses_horz(pc_tree->vertical[0]) &&
          !node_uses_horz(pc_tree->vertical[1])
#endif  // CONFIG_EXTENDED_SDP
      ) {
        part_search_state->prune_partition_3[HORZ] = 1;
      }
    }
  }

  if (part_search_state->partition_3_allowed[VERT] &&
      !frame_is_intra_only(cm) && forced_partition != PARTITION_VERT_3) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_3[VERT] = 1;
    }
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but the subtrees did not further
      // split in vert
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          pc_tree->vertical[cur_region_type][0] &&
          pc_tree->vertical[cur_region_type][1] &&
          !node_uses_vert(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->vertical[cur_region_type][1])
#else
          !node_uses_vert(pc_tree->vertical[0]) &&
          !node_uses_vert(pc_tree->vertical[1])
#endif  // CONFIG_EXTENDED_SDP
      ) {
        part_search_state->prune_partition_3[VERT] = 1;
      }
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          pc_tree->horizontal[cur_region_type][0] &&
          pc_tree->horizontal[cur_region_type][1] &&
          !node_uses_vert(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->horizontal[cur_region_type][1])
#else
          !node_uses_vert(pc_tree->horizontal[0]) &&
          !node_uses_vert(pc_tree->horizontal[1])
#endif  // CONFIG_EXTENDED_SDP
      ) {
        part_search_state->prune_partition_3[VERT] = 1;
      }
    }
  }

  const bool can_search_horz = part_search_state->partition_3_allowed[HORZ] &&
                               !part_search_state->prune_partition_3[HORZ];
  const bool can_search_vert = part_search_state->partition_3_allowed[VERT] &&
                               !part_search_state->prune_partition_3[VERT];
  const PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col,
            bsize = blk_params->bsize;
  if (part_sf->prune_part_h_with_partition_boundary &&
      (can_search_horz || can_search_vert) &&
      part_search_state->found_best_partition) {
    if (!part_search_state->partition_boundaries) {
      part_search_state->partition_boundaries = partition_boundaries;
      trace_partition_boundary(partition_boundaries, pc_tree, mi_row, mi_col,
                               bsize);
    }
    prune_part_3_with_partition_boundary(part_search_state, bsize, mi_row,
                                         mi_col, can_search_horz,
                                         can_search_vert);
  }
}

#if CONFIG_EXTENDED_SDP
// Early termination of SDP for intra blocks in inter frames
static INLINE void early_termination_inter_sdp(PC_TREE *pc_tree,
                                               float *total_count,
                                               float *inter_mode_count) {
  REGION_TYPE cur_region_type = pc_tree->region_type;
  if (pc_tree->partitioning == PARTITION_NONE) {
    if (pc_tree->none[cur_region_type] == NULL) return;
    const MB_MODE_INFO *const mi = &(pc_tree->none[cur_region_type])->mic;
    if (mi == NULL) return;
    *total_count += 1;
    if (mi->mode >= NEARMV && mi->mode < MB_MODE_COUNT) *inter_mode_count += 1;
    return;
  }
  switch (pc_tree->partitioning) {
    case PARTITION_HORZ:
      early_termination_inter_sdp(pc_tree->horizontal[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal[cur_region_type][1],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_VERT:
      early_termination_inter_sdp(pc_tree->vertical[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical[cur_region_type][1],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_HORZ_4A:
      early_termination_inter_sdp(pc_tree->horizontal4a[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4a[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4a[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4a[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_HORZ_4B:
      early_termination_inter_sdp(pc_tree->horizontal4b[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4b[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4b[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal4b[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_VERT_4A:
      early_termination_inter_sdp(pc_tree->vertical4a[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4a[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4a[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4a[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_VERT_4B:
      early_termination_inter_sdp(pc_tree->vertical4b[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4b[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4b[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical4b[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_HORZ_3:
      early_termination_inter_sdp(pc_tree->horizontal3[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal3[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal3[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->horizontal3[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    case PARTITION_VERT_3:
      early_termination_inter_sdp(pc_tree->vertical3[cur_region_type][0],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical3[cur_region_type][1],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical3[cur_region_type][2],
                                  total_count, inter_mode_count);
      early_termination_inter_sdp(pc_tree->vertical3[cur_region_type][3],
                                  total_count, inter_mode_count);
      break;
    default: break;
  }
}

// Search SDP for intra blocks in inter frames
static INLINE void search_intra_region_partitioning(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth,
    PARTITION_TYPE parent_partition) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;

  // Add one encoder fast method for early terminating inter-sdp
  float total_count = 0;
  float inter_mode_count = 0;
  early_termination_inter_sdp(pc_tree, &total_count, &inter_mode_count);
  // if over 60% of the coded blocks under this region are inter coded blocks,
  // skip the rdo for inter-sdp
  if (total_count * 0.7 < inter_mode_count) return;

  pc_tree->region_type = INTRA_REGION;
  // store the current best partitioning
  PARTITION_TYPE cur_best_partitioning = pc_tree->partitioning;
  pc_tree->partitioning = PARTITION_NONE;

  RD_STATS *sum_rdc = &part_search_state->sum_rdc;
  av1_init_rd_stats(sum_rdc);

  sum_rdc->rate = part_search_state->region_type_cost[pc_tree->region_type];
  sum_rdc->rdcost = RDCOST(x->rdmult, sum_rdc->rate, 0);

  RD_STATS best_remain_rdcost;

  av1_rd_stats_subtraction(x->rdmult, best_rdc, sum_rdc, &best_remain_rdcost);

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

  RD_STATS this_rdc;
  av1_init_rd_stats(&this_rdc);

  // Encoder RDO for luma component in intra region
  xd->tree_type = LUMA_PART;
#if CONFIG_ML_PART_SPLIT
  int force_prune_flags[3] = { 0, 0, 0 };
#endif  // CONFIG_ML_PART_SPLIT
  if (!av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
#if CONFIG_EXTENDED_SDP
                             parent_partition,
#endif  // CONFIG_EXTENDED_SDP
                             &this_rdc, best_remain_rdcost, pc_tree, ptree_luma,
                             template_tree, max_recursion_depth, NULL, NULL,
                             multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
                             ,
                             force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
                             )) {
    av1_invalid_rd_stats(&this_rdc);
    av1_invalid_rd_stats(sum_rdc);
  }
  // Encoder RDO for chroma component in intra region
  if (this_rdc.rdcost != INT64_MAX && !cm->seq_params.monochrome) {
    sum_rdc->rate += this_rdc.rate;
    sum_rdc->dist += this_rdc.dist;
    av1_rd_cost_update(x->rdmult, sum_rdc);
    xd->tree_type = CHROMA_PART;
    av1_rd_stats_subtraction(x->rdmult, best_rdc, sum_rdc, &best_remain_rdcost);
    av1_init_rd_stats(&this_rdc);
    if (!av1_rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
#if CONFIG_EXTENDED_SDP
                               parent_partition,
#endif  // CONFIG_EXTENDED_SDP
                               &this_rdc, best_remain_rdcost, pc_tree,
                               ptree_luma, template_tree, max_recursion_depth,
                               NULL, NULL, multi_pass_mode, NULL
#if CONFIG_ML_PART_SPLIT
                               ,
                               force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
                               )) {
      av1_invalid_rd_stats(&this_rdc);
      av1_invalid_rd_stats(sum_rdc);
    }
    if (this_rdc.rdcost != INT64_MAX) {
      sum_rdc->rate += this_rdc.rate;
      sum_rdc->dist += this_rdc.dist;
      av1_rd_cost_update(x->rdmult, sum_rdc);
    }
  }
  // reset tree_type to shared_part
  xd->tree_type = SHARED_PART;
  // compare current rd cost with best rd cost
  if (sum_rdc->rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = *sum_rdc;
    search_state->found_best_partition = true;
#if CONFIG_EXTENDED_SDP
    pc_tree->region_type = INTRA_REGION;
#endif  // CONFIG_EXTENDED_SDP
  } else {
    // set back to the previous stored region_type and partitioning type
    pc_tree->region_type = MIXED_INTER_INTRA_REGION;
    pc_tree->partitioning = cur_best_partitioning;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}
#endif  // CONFIG_EXTENDED_SDP

// Pruning logic for PARTITION_HORZ_4A/B and PARTITION_VERT_4A/B.
static AOM_INLINE void prune_ext_partitions_4way(
    AV1_COMP *const cpi, PC_TREE *pc_tree,
    PartitionSearchState *part_search_state, bool *partition_boundaries) {
  const AV1_COMMON *const cm = &cpi->common;
  const PARTITION_SPEED_FEATURES *part_sf = &cpi->sf.part_sf;
  const PARTITION_TYPE forced_partition = part_search_state->forced_partition;

#if CONFIG_EXTENDED_SDP
  const int cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  // Prune HORZ 4A with speed features
  if (part_search_state->partition_4a_allowed[HORZ] &&
      forced_partition != PARTITION_HORZ_4A) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_4a[HORZ] = 1;
    }
#if CONFIG_FLEX_PARTITION
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but subtrees did not further split
      // in horz
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->horizontal[cur_region_type][1])) {
#else
          !node_uses_horz(pc_tree->horizontal[0]) &&
          !node_uses_horz(pc_tree->horizontal[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4a[HORZ] = 1;
      }
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->vertical[cur_region_type][1])) {
#else
          !node_uses_horz(pc_tree->vertical[0]) &&
          !node_uses_horz(pc_tree->vertical[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4a[HORZ] = 1;
      }
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm)) {
      if (pc_tree->partitioning == PARTITION_HORZ_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->horizontal3[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->horizontal3[cur_region_type][3])) {
#else
          !node_uses_horz(pc_tree->horizontal3[0]) &&
          !node_uses_horz(pc_tree->horizontal3[3])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is horizontal H, but first and last
        // subpartitions did not further split in horizontal direction.
        part_search_state->prune_partition_4a[HORZ] = 1;
      }
      if (pc_tree->partitioning == PARTITION_VERT_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->vertical3[cur_region_type][1]) &&
          !node_uses_horz(pc_tree->vertical3[cur_region_type][2])) {
#else
          !node_uses_horz(pc_tree->vertical3[1]) &&
          !node_uses_horz(pc_tree->vertical3[2])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is vertical H, but middle two
        // subpartitions did not further split in horizontal direction.
        part_search_state->prune_partition_4a[HORZ] = 1;
      }
    }
#else
    if (part_sf->prune_ext_part_with_part_rect &&
        pc_tree->partitioning == PARTITION_HORZ &&
        !node_uses_horz(pc_tree->horizontal[0]) &&
        !node_uses_horz(pc_tree->horizontal[1])) {
      // Prune if the best partition is horz but horz did not further split in
      // horz
      part_search_state->prune_partition_4a[HORZ] = 1;
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_HORZ_3 &&
        !node_uses_horz(pc_tree->horizontal3[0]) &&
        !node_uses_horz(pc_tree->horizontal3[3])) {
      // Prune is best partition is horizontal H, but first and last
      // subpartitions did not further split in horizontal direction.
      part_search_state->prune_partition_4a[HORZ] = 1;
    }
#endif  // CONFIG_FLEX_PARTITION
    if (part_sf->prune_part_4_horz_or_vert && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_VERT &&
        part_search_state->partition_rect_allowed[HORZ]) {
      part_search_state->prune_partition_4a[HORZ] = 1;
    }
  }

  // Prune HORZ 4B with speed features
  if (part_search_state->partition_4b_allowed[HORZ] &&
      forced_partition != PARTITION_HORZ_4B) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_4b[HORZ] = 1;
    }
#if CONFIG_FLEX_PARTITION
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but subtrees did not further split
      // in horz
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->horizontal[cur_region_type][1])) {
#else
          !node_uses_horz(pc_tree->horizontal[0]) &&
          !node_uses_horz(pc_tree->horizontal[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4b[HORZ] = 1;
      }
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->vertical[cur_region_type][1])) {
#else
          !node_uses_horz(pc_tree->vertical[0]) &&
          !node_uses_horz(pc_tree->vertical[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4b[HORZ] = 1;
      }
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm)) {
      if (pc_tree->partitioning == PARTITION_HORZ_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->horizontal3[cur_region_type][0]) &&
          !node_uses_horz(pc_tree->horizontal3[cur_region_type][3])) {
#else
          !node_uses_horz(pc_tree->horizontal3[0]) &&
          !node_uses_horz(pc_tree->horizontal3[3])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is horizontal H, but first and last
        // subpartitions did not further split in horizontal direction.
        part_search_state->prune_partition_4b[HORZ] = 1;
      }
      if (pc_tree->partitioning == PARTITION_VERT_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_horz(pc_tree->vertical3[cur_region_type][1]) &&
          !node_uses_horz(pc_tree->vertical3[cur_region_type][2])) {
#else
          !node_uses_horz(pc_tree->vertical3[1]) &&
          !node_uses_horz(pc_tree->vertical3[2])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is vertical H, but middle two
        // subpartitions did not further split in horizontal direction.
        part_search_state->prune_partition_4b[HORZ] = 1;
      }
    }
#else
    if (part_sf->prune_ext_part_with_part_rect &&
        pc_tree->partitioning == PARTITION_HORZ &&
        !node_uses_horz(pc_tree->horizontal[0]) &&
        !node_uses_horz(pc_tree->horizontal[1])) {
      // Prune if the best partition is horz but horz did not further split in
      // horz
      part_search_state->prune_partition_4b[HORZ] = 1;
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_HORZ_3 &&
        !node_uses_horz(pc_tree->horizontal3[0]) &&
        !node_uses_horz(pc_tree->horizontal3[3])) {
      // Prune is best partition is horizontal H, but first and last
      // subpartitions did not further split in horizontal direction.
      part_search_state->prune_partition_4b[HORZ] = 1;
    }
#endif  // CONFIG_FLEX_PARTITION
    if (part_sf->prune_part_4_horz_or_vert && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_VERT &&
        part_search_state->partition_rect_allowed[HORZ]) {
      part_search_state->prune_partition_4b[HORZ] = 1;
    }
  }

  // Prune VERT_4A with speed features
  if (part_search_state->partition_4a_allowed[VERT] &&
      forced_partition != PARTITION_VERT_4A) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_4a[VERT] = 1;
    }
#if CONFIG_FLEX_PARTITION
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but subtrees did not further split
      // in vert
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->vertical[cur_region_type][1])) {
#else
          !node_uses_vert(pc_tree->vertical[0]) &&
          !node_uses_vert(pc_tree->vertical[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4a[VERT] = 1;
      }
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->horizontal[cur_region_type][1])) {
#else
          !node_uses_vert(pc_tree->horizontal[0]) &&
          !node_uses_vert(pc_tree->horizontal[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4a[VERT] = 1;
      }
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm)) {
      if (pc_tree->partitioning == PARTITION_VERT_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->vertical3[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->vertical3[cur_region_type][3])) {
#else
          !node_uses_vert(pc_tree->vertical3[0]) &&
          !node_uses_vert(pc_tree->vertical3[3])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is vertical H, but first and last
        // subpartitions did not further split in vertical direction.
        part_search_state->prune_partition_4a[VERT] = 1;
      }
      if (pc_tree->partitioning == PARTITION_HORZ_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->horizontal3[cur_region_type][1]) &&
          !node_uses_vert(pc_tree->horizontal3[cur_region_type][2])) {
#else
          !node_uses_vert(pc_tree->horizontal3[1]) &&
          !node_uses_vert(pc_tree->horizontal3[2])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is horizontal H, but middle two
        // subpartitions did not further split in vertical direction.
        part_search_state->prune_partition_4a[VERT] = 1;
      }
    }
#else
    if (part_sf->prune_ext_part_with_part_rect &&
        pc_tree->partitioning == PARTITION_VERT &&
        !node_uses_vert(pc_tree->vertical[0]) &&
        !node_uses_vert(pc_tree->vertical[1])) {
      // Prune if the best partition is vert but vert did not further split in
      // vert
      part_search_state->prune_partition_4a[VERT] = 1;
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_VERT_3 &&
        !node_uses_vert(pc_tree->vertical3[0]) &&
        !node_uses_vert(pc_tree->vertical3[3])) {
      // Prune is best partition is vertical H, but first and last
      // subpartitions did not further split in vertical direction.
      part_search_state->prune_partition_4a[VERT] = 1;
    }
#endif  // CONFIG_FLEX_PARTITION
    if (part_sf->prune_part_4_horz_or_vert && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_HORZ &&
        part_search_state->partition_rect_allowed[VERT]) {
      part_search_state->prune_partition_4a[VERT] = 1;
    }
  }

  // Prune VERT_4B with speed features
  if (part_search_state->partition_4b_allowed[VERT] &&
      forced_partition != PARTITION_VERT_4B) {
    if (part_sf->prune_ext_part_with_part_none &&
        pc_tree->partitioning == PARTITION_NONE) {
      // Prune if the best partition does not split
      part_search_state->prune_partition_4b[VERT] = 1;
    }
#if CONFIG_FLEX_PARTITION
    if (part_sf->prune_ext_part_with_part_rect) {
      // Prune if the best partition is rect but subtrees did not further split
      // in vert
      if (pc_tree->partitioning == PARTITION_VERT &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->vertical[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->vertical[cur_region_type][1])) {
#else
          !node_uses_vert(pc_tree->vertical[0]) &&
          !node_uses_vert(pc_tree->vertical[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4b[VERT] = 1;
      }
      if (pc_tree->partitioning == PARTITION_HORZ &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->horizontal[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->horizontal[cur_region_type][1])) {
#else
          !node_uses_vert(pc_tree->horizontal[0]) &&
          !node_uses_vert(pc_tree->horizontal[1])) {
#endif  // CONFIG_EXTENDED_SDP
        part_search_state->prune_partition_4b[VERT] = 1;
      }
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm)) {
      if (pc_tree->partitioning == PARTITION_VERT_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->vertical3[cur_region_type][0]) &&
          !node_uses_vert(pc_tree->vertical3[cur_region_type][3])) {
#else
          !node_uses_vert(pc_tree->vertical3[0]) &&
          !node_uses_vert(pc_tree->vertical3[3])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is vertical H, but first and last
        // subpartitions did not further split in vertical direction.
        part_search_state->prune_partition_4b[VERT] = 1;
      }
      if (pc_tree->partitioning == PARTITION_HORZ_3 &&
#if CONFIG_EXTENDED_SDP
          !node_uses_vert(pc_tree->horizontal3[cur_region_type][1]) &&
          !node_uses_vert(pc_tree->horizontal3[cur_region_type][2])) {
#else
          !node_uses_vert(pc_tree->horizontal3[1]) &&
          !node_uses_vert(pc_tree->horizontal3[2])) {
#endif  // CONFIG_EXTENDED_SDP
        // Prune if best partition is horizontal H, but middle two
        // subpartitions did not further split in vertical direction.
        part_search_state->prune_partition_4b[VERT] = 1;
      }
    }
#else
    if (part_sf->prune_ext_part_with_part_rect &&
        pc_tree->partitioning == PARTITION_VERT &&
        !node_uses_vert(pc_tree->vertical[0]) &&
        !node_uses_vert(pc_tree->vertical[1])) {
      // Prune if the best partition is vert but vert did not further split in
      // vert
      part_search_state->prune_partition_4b[VERT] = 1;
    }
    if (part_sf->prune_part_4_with_part_3 && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_VERT_3 &&
        !node_uses_vert(pc_tree->vertical3[0]) &&
        !node_uses_vert(pc_tree->vertical3[3])) {
      // Prune is best partition is vertical H, but first and last
      // subpartitions did not further split in vertical direction.
      part_search_state->prune_partition_4b[VERT] = 1;
    }
#endif  // CONFIG_FLEX_PARTITION
    if (part_sf->prune_part_4_horz_or_vert && !frame_is_intra_only(cm) &&
        pc_tree->partitioning == PARTITION_HORZ &&
        part_search_state->partition_rect_allowed[VERT]) {
      part_search_state->prune_partition_4b[VERT] = 1;
    }
  }

  const bool can_search_horz_4a =
      part_search_state->partition_4a_allowed[HORZ] &&
      !part_search_state->prune_partition_4a[HORZ];
  const bool can_search_horz_4b =
      part_search_state->partition_4b_allowed[HORZ] &&
      !part_search_state->prune_partition_4b[HORZ];
  const bool can_search_vert_4a =
      part_search_state->partition_4a_allowed[VERT] &&
      !part_search_state->prune_partition_4a[VERT];
  const bool can_search_vert_4b =
      part_search_state->partition_4b_allowed[VERT] &&
      !part_search_state->prune_partition_4b[VERT];
  const PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col,
            bsize = blk_params->bsize;
  if (part_sf->prune_part_4_with_partition_boundary &&
      (can_search_horz_4a || can_search_vert_4a || can_search_horz_4b ||
       can_search_vert_4b) &&
      part_search_state->found_best_partition) {
    if (!part_search_state->partition_boundaries ||
        pc_tree->partitioning == PARTITION_HORZ_3 ||
        pc_tree->partitioning == PARTITION_VERT_3) {
      part_search_state->partition_boundaries = partition_boundaries;
      trace_partition_boundary(partition_boundaries, pc_tree, mi_row, mi_col,
                               bsize);
    }
    prune_part_4_with_partition_boundary(
        part_search_state, partition_boundaries, bsize, mi_row, mi_col,
        can_search_horz_4a, can_search_horz_4b, can_search_vert_4a,
        can_search_vert_4b);
  }
}

static INLINE void search_partition_horz_4a(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

  if (is_part_pruned_by_forced_partition(part_search_state,
                                         PARTITION_HORZ_4A) ||
      !part_search_state->partition_4a_allowed[HORZ] ||
      part_search_state->prune_partition_4a[HORZ]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_rows ||
      !is_partition_valid(bsize, PARTITION_HORZ_4A) ||
      !(search_state->do_rectangular_split ||
        av1_active_h_edge(cpi, mi_row, blk_params->mi_step_h))) {
    return;
  }

  const int part_h4a_rate = search_state->partition_cost[PARTITION_HORZ_4A];
  if (part_h4a_rate == INT_MAX ||
      RDCOST(x->rdmult, part_h4a_rate, 0) >= best_rdc->rdcost) {
    return;
  }
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int eighth_step = mi_size_high[bsize] / 8;

  sum_rdc.rate = search_state->partition_cost[PARTITION_HORZ_4A];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_HORZ_4A))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_partition_subsize(bsize, PARTITION_HORZ_4A);
  const BLOCK_SIZE big_subsize = get_partition_subsize(bsize, PARTITION_HORZ);
  const BLOCK_SIZE med_subsize = subsize_lookup[PARTITION_HORZ][big_subsize];
  assert(sml_subsize == subsize_lookup[PARTITION_HORZ][med_subsize]);

  const int cum_step_multipliers[4] = { 0, 1, 3, 7 };
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, med_subsize, big_subsize,
                                         sml_subsize };

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->horizontal4a[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal4a[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->horizontal4a[cur_region_type][idx] = NULL;
    }
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[idx];
    pc_tree->horizontal4a[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, this_mi_row, mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_HORZ_4A, idx, idx == 3, ss_x, ss_y);
#else
    if (pc_tree->horizontal4a[idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal4a[idx], num_planes, 0, 0);
      pc_tree->horizontal4a[idx] = NULL;
    }
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[idx];
    pc_tree->horizontal4a[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, this_mi_row, mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_HORZ_4A, idx, idx == 3, ss_x, ss_y);
#endif  // CONFIG_EXTENDED_SDP
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[i];

    if (i > 0 && this_mi_row >= cm->mi_params.mi_rows) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->horizontal4a[cur_region_type][i],
#else
      pc_tree->horizontal4a[i],
#endif
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      this_mi_row,
      mi_col,
      subblock_sizes[i],
      PARTITION_HORZ_4A
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_HORZ_4A;
    pc_tree->skippable = skippable;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

static INLINE void search_partition_horz_4b(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif

  if (is_part_pruned_by_forced_partition(part_search_state,
                                         PARTITION_HORZ_4B) ||
      !part_search_state->partition_4b_allowed[HORZ] ||
      part_search_state->prune_partition_4b[HORZ]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_rows ||
      !is_partition_valid(bsize, PARTITION_HORZ_4B) ||
      !(search_state->do_rectangular_split ||
        av1_active_h_edge(cpi, mi_row, blk_params->mi_step_h))) {
    return;
  }

  const int part_h4b_rate = search_state->partition_cost[PARTITION_HORZ_4B];
  if (part_h4b_rate == INT_MAX ||
      RDCOST(x->rdmult, part_h4b_rate, 0) >= best_rdc->rdcost) {
    return;
  }
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int eighth_step = mi_size_high[bsize] / 8;

  sum_rdc.rate = search_state->partition_cost[PARTITION_HORZ_4B];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_HORZ_4A))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_partition_subsize(bsize, PARTITION_HORZ_4B);
  const BLOCK_SIZE big_subsize = get_partition_subsize(bsize, PARTITION_HORZ);
  const BLOCK_SIZE med_subsize = subsize_lookup[PARTITION_HORZ][big_subsize];
  assert(sml_subsize == subsize_lookup[PARTITION_HORZ][med_subsize]);

  const int cum_step_multipliers[4] = { 0, 1, 5, 7 };
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, big_subsize, med_subsize,
                                         sml_subsize };

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->horizontal4b[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal4b[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->horizontal4b[cur_region_type][idx] = NULL;
    }
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[idx];
    pc_tree->horizontal4b[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, this_mi_row, mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_HORZ_4B, idx, idx == 3, ss_x, ss_y);
#else
    if (pc_tree->horizontal4b[idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal4b[idx], num_planes, 0, 0);
      pc_tree->horizontal4b[idx] = NULL;
    }
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[idx];
    pc_tree->horizontal4b[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, this_mi_row, mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_HORZ_4B, idx, idx == 3, ss_x, ss_y);
#endif  // CONFIG_EXTENDED_SDP
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_row = mi_row + eighth_step * cum_step_multipliers[i];

    if (i > 0 && this_mi_row >= cm->mi_params.mi_rows) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->horizontal4b[cur_region_type][i],
#else
      pc_tree->horizontal4b[i],
#endif  // CONFIG_EXTENDED_SDP
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      this_mi_row,
      mi_col,
      subblock_sizes[i],
      PARTITION_HORZ_4B
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_HORZ_4B;
    pc_tree->skippable = skippable;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

static INLINE void search_partition_vert_4a(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  if (is_part_pruned_by_forced_partition(part_search_state,
                                         PARTITION_VERT_4A) ||
      !part_search_state->partition_4a_allowed[VERT] ||
      part_search_state->prune_partition_4a[VERT]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_cols ||
      !is_partition_valid(bsize, PARTITION_VERT_4A) ||
      !(search_state->do_rectangular_split ||
        av1_active_v_edge(cpi, mi_col, blk_params->mi_step_w))) {
    return;
  }

  const int part_v4a_rate = search_state->partition_cost[PARTITION_VERT_4A];
  if (part_v4a_rate == INT_MAX ||
      RDCOST(x->rdmult, part_v4a_rate, 0) >= best_rdc->rdcost) {
    return;
  }
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int eighth_step = mi_size_wide[bsize] / 8;

  sum_rdc.rate = search_state->partition_cost[PARTITION_VERT_4A];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_VERT_4A))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_partition_subsize(bsize, PARTITION_VERT_4A);
  const BLOCK_SIZE big_subsize = get_partition_subsize(bsize, PARTITION_VERT);
  const BLOCK_SIZE med_subsize = subsize_lookup[PARTITION_VERT][big_subsize];
  assert(sml_subsize == subsize_lookup[PARTITION_VERT][med_subsize]);

  const int cum_step_multipliers[4] = { 0, 1, 3, 7 };
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, med_subsize, big_subsize,
                                         sml_subsize };

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->vertical4a[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical4a[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->vertical4a[cur_region_type][idx] = NULL;
    }
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[idx];
    pc_tree->vertical4a[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row, this_mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_VERT_4A, idx, idx == 3, ss_x, ss_y);
#else
    if (pc_tree->vertical4a[idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical4a[idx], num_planes, 0, 0);
      pc_tree->vertical4a[idx] = NULL;
    }
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[idx];
    pc_tree->vertical4a[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row, this_mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_VERT_4A, idx, idx == 3, ss_x, ss_y);
#endif  // CONFIG_EXTENDED_SDP
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[i];

    if (i > 0 && this_mi_col >= cm->mi_params.mi_cols) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->vertical4a[cur_region_type][i],
#else
      pc_tree->vertical4a[i],
#endif  // CONFIG_EXTENDED_SDP
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      mi_row,
      this_mi_col,
      subblock_sizes[i],
      PARTITION_VERT_4A
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_VERT_4A;
    pc_tree->skippable = skippable;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

static INLINE void search_partition_vert_4b(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  if (is_part_pruned_by_forced_partition(part_search_state,
                                         PARTITION_VERT_4B) ||
      !part_search_state->partition_4b_allowed[VERT] ||
      part_search_state->prune_partition_4b[VERT]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_cols ||
      !is_partition_valid(bsize, PARTITION_VERT_4B) ||
      !(search_state->do_rectangular_split ||
        av1_active_v_edge(cpi, mi_col, blk_params->mi_step_w))) {
    return;
  }

  const int part_v4b_rate = search_state->partition_cost[PARTITION_VERT_4B];
  if (part_v4b_rate == INT_MAX ||
      RDCOST(x->rdmult, part_v4b_rate, 0) >= best_rdc->rdcost) {
    return;
  }
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int eighth_step = mi_size_wide[bsize] / 8;

  sum_rdc.rate = search_state->partition_cost[PARTITION_VERT_4B];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_VERT_4A))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_partition_subsize(bsize, PARTITION_VERT_4B);
  const BLOCK_SIZE big_subsize = get_partition_subsize(bsize, PARTITION_VERT);
  const BLOCK_SIZE med_subsize = subsize_lookup[PARTITION_VERT][big_subsize];
  assert(sml_subsize == subsize_lookup[PARTITION_VERT][med_subsize]);

  const int cum_step_multipliers[4] = { 0, 1, 5, 7 };
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, big_subsize, med_subsize,
                                         sml_subsize };

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->vertical4b[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical4b[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->vertical4b[cur_region_type][idx] = NULL;
    }
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[idx];
    pc_tree->vertical4b[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row, this_mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_VERT_4B, idx, idx == 3, ss_x, ss_y);
#else
    if (pc_tree->vertical4b[idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical4b[idx], num_planes, 0, 0);
      pc_tree->vertical4b[idx] = NULL;
    }
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[idx];
    pc_tree->vertical4b[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row, this_mi_col, subblock_sizes[idx], pc_tree,
        PARTITION_VERT_4B, idx, idx == 3, ss_x, ss_y);
#endif  // CONFIG_EXTENDED_SDP
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_col = mi_col + eighth_step * cum_step_multipliers[i];

    if (i > 0 && this_mi_col >= cm->mi_params.mi_cols) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->vertical4b[cur_region_type][i],
#else
      pc_tree->vertical4b[i],
#endif  // CONFIG_EXTENDED_SDP
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      mi_row,
      this_mi_col,
      subblock_sizes[i],
      PARTITION_VERT_4B
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_VERT_4B;
    pc_tree->skippable = skippable;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

/*!\brief Performs rdopt on PARTITION_HORZ_3. */
static INLINE void search_partition_horz_3(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  if (is_part_pruned_by_forced_partition(part_search_state, PARTITION_HORZ_3) ||
      !part_search_state->partition_3_allowed[HORZ] ||
      part_search_state->prune_partition_3[HORZ]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_rows ||
      !is_partition_valid(bsize, PARTITION_HORZ_3) ||
      !(search_state->do_rectangular_split ||
        av1_active_h_edge(cpi, mi_row, blk_params->mi_step_h))) {
    return;
  }
  // TODO(yuec): set default partition modes for the edge directly by ruling out
  // h partitions from the syntax if the 2nd middle block is not in the frame.
  if (mi_col + (mi_size_wide[bsize] >> 1) >= cm->mi_params.mi_cols) return;

  const int part_h3_rate = search_state->partition_cost[PARTITION_HORZ_3];
  if (part_h3_rate == INT_MAX ||
      RDCOST(x->rdmult, part_h3_rate, 0) >= best_rdc->rdcost) {
    return;
  }
  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int quarter_step = mi_size_high[bsize] / 4;

  sum_rdc.rate = search_state->partition_cost[PARTITION_HORZ_3];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_HORZ_3))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_h_partition_subsize(bsize, 0, PARTITION_HORZ_3);
  const BLOCK_SIZE big_subsize =
      get_h_partition_subsize(bsize, 1, PARTITION_HORZ_3);
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, big_subsize, big_subsize,
                                         sml_subsize };
  const int offset_mr[4] = { 0, quarter_step, quarter_step, 3 * quarter_step };
  const int offset_mc[4] = { 0, 0, mi_size_wide[bsize] / 2, 0 };

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->horizontal3[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal3[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->horizontal3[cur_region_type][idx] = NULL;
    }

    pc_tree->horizontal3[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row + offset_mr[idx], mi_col + offset_mc[idx],
        subblock_sizes[idx], pc_tree, PARTITION_HORZ_3, idx, idx == 3, ss_x,
        ss_y);
#else
    if (pc_tree->horizontal3[idx]) {
      av1_free_pc_tree_recursive(pc_tree->horizontal3[idx], num_planes, 0, 0);
      pc_tree->horizontal3[idx] = NULL;
    }

    pc_tree->horizontal3[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row + offset_mr[idx], mi_col + offset_mc[idx],
        subblock_sizes[idx], pc_tree, PARTITION_HORZ_3, idx, idx == 3, ss_x,
        ss_y);
#endif
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_row = mi_row + offset_mr[i];
    const int this_mi_col = mi_col + offset_mc[i];

    if (i > 0 && this_mi_row >= cm->mi_params.mi_rows) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->horizontal3[cur_region_type][i],
#else
      pc_tree->horizontal3[i],
#endif  // CONFIG_EXTENDED_SDP
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      this_mi_row,
      this_mi_col,
      subblock_sizes[i],
      PARTITION_HORZ_3
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_HORZ_3;
    pc_tree->skippable = skippable;
  }

  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

/*!\brief Performs rdopt on PARTITION_VERT_3. */
static INLINE void search_partition_vert_3(
    PartitionSearchState *search_state, AV1_COMP *const cpi, ThreadData *td,
    TileDataEnc *tile_data, TokenExtra **tp, RD_STATS *best_rdc,
    PC_TREE *pc_tree, const PARTITION_TREE *ptree_luma,
    const PARTITION_TREE *template_tree, RD_SEARCH_MACROBLOCK_CONTEXT *x_ctx,
    const PartitionSearchState *part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    LevelBanksRDO *level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    SB_MULTI_PASS_MODE multi_pass_mode, int max_recursion_depth) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;

  const PartitionBlkParams *blk_params = &search_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP

  if (is_part_pruned_by_forced_partition(part_search_state, PARTITION_VERT_3) ||
      !part_search_state->partition_3_allowed[VERT] ||
      part_search_state->prune_partition_3[VERT]) {
    return;
  }

  if (search_state->terminate_partition_search || !blk_params->has_cols ||
      !is_partition_valid(bsize, PARTITION_VERT_3) ||
      !(search_state->do_rectangular_split ||
        av1_active_v_edge(cpi, mi_col, blk_params->mi_step_w))) {
    return;
  }
  // TODO(yuec): set default partition modes for the edge directly by ruling out
  // h partitions from the syntax if the 2nd middle block is not in the frame.
  if (mi_row + (mi_size_high[bsize] >> 1) >= cm->mi_params.mi_rows) return;

  const int part_v3_rate = search_state->partition_cost[PARTITION_VERT_3];
  if (part_v3_rate == INT_MAX ||
      RDCOST(x->rdmult, part_v3_rate, 0) >= best_rdc->rdcost) {
    return;
  }

  RD_STATS sum_rdc;
  av1_init_rd_stats(&sum_rdc);
  const int quarter_step = mi_size_wide[bsize] / 4;

  sum_rdc.rate = search_state->partition_cost[PARTITION_VERT_3];
#if CONFIG_EXTENDED_SDP
  if (pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cpi->common.seq_params.enable_sdp &&
      is_extended_sdp_allowed(pc_tree->parent->block_size,
                              pc_tree->parent->partitioning) &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_VERT_3))
    sum_rdc.rate +=
        part_search_state->region_type_cost[MIXED_INTER_INTRA_REGION];
#endif  // CONFIG_EXTENDED_SDP
  sum_rdc.rdcost = RDCOST(x->rdmult, sum_rdc.rate, 0);

  const BLOCK_SIZE sml_subsize =
      get_h_partition_subsize(bsize, 0, PARTITION_VERT_3);
  const BLOCK_SIZE big_subsize =
      get_h_partition_subsize(bsize, 1, PARTITION_VERT_3);
  const BLOCK_SIZE subblock_sizes[4] = { sml_subsize, big_subsize, big_subsize,
                                         sml_subsize };
  const int offset_mr[4] = { 0, 0, mi_size_high[bsize] / 2, 0 };
  const int offset_mc[4] = { 0, quarter_step, quarter_step, 3 * quarter_step };

  for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
    if (pc_tree->vertical3[cur_region_type][idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical3[cur_region_type][idx],
                                 num_planes, 0, 0);
      pc_tree->vertical3[cur_region_type][idx] = NULL;
    }

    pc_tree->vertical3[cur_region_type][idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row + offset_mr[idx], mi_col + offset_mc[idx],
        subblock_sizes[idx], pc_tree, PARTITION_VERT_3, idx, idx == 3, ss_x,
        ss_y);
#else
    if (pc_tree->vertical3[idx]) {
      av1_free_pc_tree_recursive(pc_tree->vertical3[idx], num_planes, 0, 0);
      pc_tree->vertical3[idx] = NULL;
    }

    pc_tree->vertical3[idx] = av1_alloc_pc_tree_node(
        xd->tree_type, mi_row + offset_mr[idx], mi_col + offset_mc[idx],
        subblock_sizes[idx], pc_tree, PARTITION_VERT_3, idx, idx == 3, ss_x,
        ss_y);
#endif
  }

  bool skippable = true;
  for (int i = 0; i < 4; ++i) {
    const int this_mi_row = mi_row + offset_mr[i];
    const int this_mi_col = mi_col + offset_mc[i];

    if (i > 0 && this_mi_col >= cm->mi_params.mi_cols) break;

    SUBBLOCK_RDO_DATA rdo_data = {
#if CONFIG_EXTENDED_SDP
      pc_tree->vertical3[cur_region_type][i],
#else
      pc_tree->vertical3[i],
#endif  // CONFIG_EXTENDED_SDP
      get_partition_subtree_const(ptree_luma, i),
      get_partition_subtree_const(template_tree, i),
      this_mi_row,
      this_mi_col,
      subblock_sizes[i],
      PARTITION_VERT_3
    };
    if (!rd_try_subblock_new(cpi, td, tile_data, tp, &rdo_data, *best_rdc,
                             &sum_rdc, multi_pass_mode, &skippable,
                             max_recursion_depth)) {
      av1_invalid_rd_stats(&sum_rdc);
      break;
    }
  }

  av1_rd_cost_update(x->rdmult, &sum_rdc);
  if (sum_rdc.rdcost < best_rdc->rdcost) {
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    update_best_level_banks(level_banks, &x->e_mbd);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    *best_rdc = sum_rdc;
    search_state->found_best_partition = true;
    pc_tree->partitioning = PARTITION_VERT_3;
    pc_tree->skippable = skippable;
  }
  av1_restore_context(cm, x, x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  restore_level_banks(&x->e_mbd, level_banks);
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
}

#endif  // CONFIG_EXT_RECUR_PARTITIONS

static AOM_INLINE int get_partition_depth(const PC_TREE *pc_tree,
                                          int curr_depth) {
  const PARTITION_TYPE partition = pc_tree->partitioning;
  int max_depth = curr_depth;
#if CONFIG_EXTENDED_SDP
  int cur_region_type = pc_tree->region_type;
#endif  // CONFIG_EXTENDED_SDP
  switch (partition) {
    case PARTITION_NONE: break;
    case PARTITION_SPLIT:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth, get_partition_depth(pc_tree->split[cur_region_type][idx],
                                           curr_depth + 2));
#else
        max_depth = AOMMAX(max_depth, get_partition_depth(pc_tree->split[idx],
                                                          curr_depth + 2));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_HORZ:
      for (int idx = 0; idx < 2; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_VERT:
      for (int idx = 0; idx < 2; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth =
            AOMMAX(max_depth,
                   get_partition_depth(pc_tree->vertical[cur_region_type][idx],
                                       curr_depth + 1));
#else
        max_depth =
            AOMMAX(max_depth,
                   get_partition_depth(pc_tree->vertical[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_HORZ_3:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal3[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal3[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_VERT_3:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth =
            AOMMAX(max_depth,
                   get_partition_depth(pc_tree->vertical3[cur_region_type][idx],
                                       curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->vertical3[idx], curr_depth + 1));
#endif
      }
      break;
    case PARTITION_HORZ_4A:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal4a[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal4a[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_HORZ_4B:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal4b[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->horizontal4b[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_VERT_4A:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->vertical4a[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->vertical4a[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    case PARTITION_VERT_4B:
      for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->vertical4b[cur_region_type][idx],
                                curr_depth + 1));
#else
        max_depth = AOMMAX(
            max_depth,
            get_partition_depth(pc_tree->vertical4b[idx], curr_depth + 1));
#endif  // CONFIG_EXTENDED_SDP
      }
      break;
    default: assert(0); break;
#else
    default: break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  }
  return max_depth;
}

#if CONFIG_EXT_RECUR_PARTITIONS
static AOM_INLINE bool try_none_after_rect(
    const MACROBLOCKD *xd, const CommonModeInfoParams *mi_params,
    BLOCK_SIZE bsize,
#if CONFIG_CB1TO4_SPLIT
    BLOCK_SIZE parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
    int mi_row, int mi_col) {
  if (!is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                          ,
                          parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
                          )) {
    return false;
  }
  const int tree_idx = av1_get_sdp_idx(xd->tree_type);
  // This speed feature is not applicable if either the above or left block is
  // unavailable.
  if (tree_idx == 0 && !(xd->up_available && xd->left_available)) {
    return false;
  }
  if (tree_idx == 1 &&
      !(xd->chroma_up_available && xd->chroma_left_available)) {
    return false;
  }
  // Scan for the maximum and minimum dimension of the above and left blocks.
  const int mi_stride = xd->mi_stride;
  int min_left_dim_log2 = INT_MAX, min_above_dim_log2 = INT_MAX;
  int max_left_dim_log2 = 0, max_above_dim_log2 = 0;
  const int mi_height =
      AOMMIN(mi_size_high[bsize], mi_params->mi_rows - mi_row);
  const int mi_width = AOMMIN(mi_size_wide[bsize], mi_params->mi_cols - mi_col);
  for (int row = 0; row < mi_height;) {
    const MB_MODE_INFO *mi = xd->mi[row * mi_stride - 1];
    const BLOCK_SIZE left_bsize = mi->sb_type[tree_idx];

    min_left_dim_log2 =
        AOMMIN(min_left_dim_log2, mi_size_high_log2[left_bsize]);
    max_left_dim_log2 =
        AOMMAX(max_left_dim_log2, mi_size_high_log2[left_bsize]);
    const int row_step =
        tree_idx == 0
            ? mi_size_high[left_bsize] - AOMMAX(mi_row - mi->mi_row_start, 0)
            : mi_size_high[left_bsize] -
                  AOMMAX(mi_row - mi->chroma_mi_row_start, 0);
    row += row_step;
    assert(row_step > 0);
  }
  for (int col = 0; col < mi_width;) {
    const MB_MODE_INFO *mi = xd->mi[-1 * mi_stride + col];
    const BLOCK_SIZE above_bsize = mi->sb_type[tree_idx];

    min_above_dim_log2 =
        AOMMIN(min_above_dim_log2, mi_size_wide_log2[above_bsize]);
    max_above_dim_log2 =
        AOMMAX(max_above_dim_log2, mi_size_wide_log2[above_bsize]);
    const int col_step =
        tree_idx == 0
            ? mi_size_wide[above_bsize] - AOMMAX(mi_col - mi->mi_col_start, 0)
            : mi_size_wide[above_bsize] -
                  AOMMAX(mi_col - mi->chroma_mi_col_start, 0);
    col += col_step;
    assert(col_step > 0);
  }
  // Delay the search for partition none if the above width and left height
  // are not bigger than the current block dimension AND at least one of the
  // dimensions if smaller than the current block by a factor of 4.
  if ((mi_size_high_log2[bsize] > max_left_dim_log2 + 1 &&
       mi_size_wide_log2[bsize] >= min_above_dim_log2) ||
      (mi_size_wide_log2[bsize] > max_above_dim_log2 + 1 &&
       mi_size_high_log2[bsize] >= min_left_dim_log2)) {
    return true;
  }
  return false;
}

/*!\brief Prune PARTITION_NONE search if rect partitions split deeper.
 */
static AOM_INLINE void prune_none_with_rect_results(
    PartitionSearchState *part_search_state, const PC_TREE *pc_tree) {
  if (!part_search_state->found_best_partition) {
    return;
  }

  const PARTITION_TYPE cur_best_partition = pc_tree->partitioning;
  PC_TREE *const *tree = NULL;
  int num_sub_parts = 0;
  if (cur_best_partition == PARTITION_SPLIT) {
#if CONFIG_EXTENDED_SDP
    tree = pc_tree->split[pc_tree->region_type];
#else
    tree = pc_tree->split;
#endif  // CONFIG_EXTENDED_SDP
    num_sub_parts = SUB_PARTITIONS_SPLIT;
  } else if (cur_best_partition == PARTITION_HORZ) {
#if CONFIG_EXTENDED_SDP
    tree = pc_tree->horizontal[pc_tree->region_type];
#else
    tree = pc_tree->horizontal;
#endif  // CONFIG_EXTENDED_SDP
    num_sub_parts = NUM_RECT_PARTS;
  } else if (cur_best_partition == PARTITION_VERT) {
#if CONFIG_EXTENDED_SDP
    tree = pc_tree->vertical[pc_tree->region_type];
#else
    tree = pc_tree->vertical;
#endif  // CONFIG_EXTENDED_SDP
    num_sub_parts = NUM_RECT_PARTS;
  } else {
    assert(0 &&
           "Unexpected best partition type in prune_none_with_rect_results.");
  }
  // Give up on PARTITION_NONE if either of the subtrees decided to split
  // further.
  for (int idx = 0; idx < num_sub_parts; idx++) {
    if (!tree[idx]) {
      break;
    }
    part_search_state->prune_partition_none |=
        tree[idx]->partitioning != PARTITION_NONE;
  }
}

/*!\brief AV1 block partition search (full search).
*
* \ingroup partition_search
* \callgraph
* Searches for the best partition pattern for a block based on the
* rate-distortion cost, and returns a bool value to indicate whether a valid
* partition pattern is found. The partition can recursively go down to the
* smallest block size.
*
* \param[in]    cpi                Top-level encoder structure
* \param[in]    td                 Pointer to thread data
* \param[in]    tile_data          Pointer to struct holding adaptive
data/contexts/models for the tile during
encoding
* \param[in]    tp                 Pointer to the starting token
* \param[in]    mi_row             Row coordinate of the block in a step size
of MI_SIZE
* \param[in]    mi_col             Column coordinate of the block in a step
size of MI_SIZE
* \param[in]    bsize              Current block size
* \param[in]    parent_partition   Partition of the parent block
* \param[in]    rd_cost            Pointer to the final rd cost of the block
* \param[in]    best_rdc           Upper bound of rd cost of a valid partition
* \param[in]    pc_tree            Pointer to the PC_TREE node storing the
picked partitions and mode info for the
current block
* \param[in]    ptree_luma Pointer to the luma partition tree so that the
*                          encoder to estimate the partition type for chroma.
* \param[in]    template_tree      A partial tree that contains the partition
*                                  structure to be used as a template.
* \param[in]    max_recursion_depth The maximum level of recursion allowed
* \param[in]    sms_tree           Pointer to struct holding simple motion
search data for the current block
* \param[in]    none_rd            Pointer to the rd cost in the case of not
splitting the current block
* \param[in]    multi_pass_mode    SB_SINGLE_PASS/SB_DRY_PASS/SB_WET_PASS
* \param[in]    rect_part_win_info Pointer to struct storing whether horz/vert
* partition outperforms previously tested partitions
*
* \return A bool value is returned indicating if a valid partition is found.
* The pc_tree struct is modified to store the picked partition and modes.
* The rd_cost struct is also updated with the RD stats corresponding to the
* best partition found.
*/
#else
/*!\brief AV1 block partition search (full search).
*
* \ingroup partition_search
* \callgraph
* Searches for the best partition pattern for a block based on the
* rate-distortion cost, and returns a bool value to indicate whether a valid
* partition pattern is found. The partition can recursively go down to the
* smallest block size.
*
* This function works on planes determined by get_partition_plane_start() and
* get_partition_plane_end() based on xd->tree_type.
*
* \param[in]    cpi                Top-level encoder structure
* \param[in]    td                 Pointer to thread data
* \param[in]    tile_data          Pointer to struct holding adaptive
data/contexts/models for the tile during
encoding
* \param[in]    tp                 Pointer to the starting token
* \param[in]    mi_row             Row coordinate of the block in a step size
of MI_SIZE
* \param[in]    mi_col             Column coordinate of the block in a step
size of MI_SIZE
* \param[in]    bsize              Current block size
* \param[in]    rd_cost            Pointer to the final rd cost of the block
* \param[in]    best_rdc           Upper bound of rd cost of a valid partition
* \param[in]    pc_tree            Pointer to the PC_TREE node storing the
picked partitions and mode info for the
current block
* \param[in]    sms_tree           Pointer to struct holding simple motion
search data for the current block
* \param[in]    none_rd            Pointer to the rd cost in the case of not
splitting the current block
* \param[in]    multi_pass_mode    SB_SINGLE_PASS/SB_DRY_PASS/SB_WET_PASS
* \param[in]    rect_part_win_info Pointer to struct storing whether horz/vert
partition outperforms previously tested partitions
*
* \return A bool value is returned indicating if a valid partition is found.
* The pc_tree struct is modified to store the picked partition and modes.
* The rd_cost struct is also updated with the RD stats corresponding to the
* best partition found.
*/
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_ML_PART_SPLIT
enum { PRUNE_OTHER = 0, PRUNE_VERT = 1, PRUNE_HORZ = 2 };
#endif  // CONFIG_ML_PART_SPLIT

bool av1_rd_pick_partition(AV1_COMP *const cpi, ThreadData *td,
                           TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                           int mi_col, BLOCK_SIZE bsize,
#if CONFIG_EXTENDED_SDP
                           PARTITION_TYPE parent_partition,
#endif  // CONFIG_EXTENDED_SDP
                           RD_STATS *rd_cost, RD_STATS best_rdc,
                           PC_TREE *pc_tree,
#if CONFIG_EXT_RECUR_PARTITIONS
                           const PARTITION_TREE *ptree_luma,
                           const PARTITION_TREE *template_tree,
                           int max_recursion_depth,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                           SIMPLE_MOTION_DATA_TREE *sms_tree, int64_t *none_rd,
                           SB_MULTI_PASS_MODE multi_pass_mode,
                           RD_RECT_PART_WIN_INFO *rect_part_win_info
#if CONFIG_ML_PART_SPLIT
                           ,
                           int force_prune_flags[3]
#endif  // CONFIG_ML_PART_SPLIT
) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_SEARCH_MACROBLOCK_CONTEXT x_ctx;
  const TokenExtra *const tp_orig = *tp;
  PartitionSearchState part_search_state;

#if CONFIG_EXTENDED_SDP
  if (frame_is_intra_only(cm) && pc_tree) {
    pc_tree->region_type = INTRA_REGION;
  }
#endif  // CONFIG_EXTENDED_SDP

  // Initialization of state variables used in partition search.
  init_partition_search_state_params(x, cpi, &part_search_state,
#if CONFIG_EXT_RECUR_PARTITIONS
                                     pc_tree, ptree_luma, template_tree,
                                     max_recursion_depth,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                                     mi_row, mi_col, bsize);
  PartitionBlkParams blk_params = part_search_state.part_blk_params;
#if CONFIG_EXT_RECUR_PARTITIONS
  if (sms_tree != NULL)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    sms_tree->partitioning = PARTITION_NONE;
  if (best_rdc.rdcost < 0) {
    av1_invalid_rd_stats(rd_cost);
    return part_search_state.found_best_partition;
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  // Check whether there is a counterpart pc_tree node with the same size
  // and the same neighboring context at the same location but from a
  // different partition path. If yes directly copy the RDO decision made for
  // the counterpart.
  PC_TREE *counterpart_block = av1_look_for_counterpart_block(pc_tree);
  assert(pc_tree != NULL);
  if (counterpart_block
#if CONFIG_CB1TO4_SPLIT
      &&
      (is_partition_point(bsize, pc_tree->parent ? pc_tree->parent->block_size
                                                 : BLOCK_INVALID) ==
       is_partition_point(counterpart_block->block_size,
                          counterpart_block->parent
                              ? counterpart_block->parent->block_size
                              : BLOCK_INVALID))
#endif  // CONFIG_CB1TO4_SPLIT
#if CONFIG_EXTENDED_SDP
      && (pc_tree->region_type == counterpart_block->region_type &&
          (pc_tree->region_type != INTRA_REGION || frame_is_intra_only(cm)))
#endif  // CONFIG_EXTENDED_SDP
  ) {
    if (counterpart_block->rd_cost.rate != INT_MAX) {
      av1_copy_pc_tree_recursive(xd, cm, pc_tree, counterpart_block,
                                 part_search_state.ss_x, part_search_state.ss_y,
                                 &td->shared_coeff_buf, xd->tree_type,
                                 num_planes);
      *rd_cost = pc_tree->rd_cost;
      assert(bsize != cm->sb_size);
      if (bsize == cm->sb_size) exit(0);

      if (!pc_tree->is_last_subblock) {
        encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                  pc_tree, NULL,
#if CONFIG_EXT_RECUR_PARTITIONS
                  NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                  NULL);
      }
      return true;
    } else {
      av1_invalid_rd_stats(rd_cost);
      return false;
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  if (bsize == cm->sb_size) x->must_find_valid_partition = 0;

  // Override skipping rectangular partition operations for edge blocks.
  if (none_rd) *none_rd = 0;
  (void)*tp_orig;

#if CONFIG_COLLECT_PARTITION_STATS
  int partition_decisions[EXT_PARTITION_TYPES] = { 0 };
  int partition_attempts[EXT_PARTITION_TYPES] = { 0 };
  int64_t partition_times[EXT_PARTITION_TYPES] = { 0 };
  struct aom_usec_timer partition_timer = { 0 };
  int partition_timer_on = 0;
#if CONFIG_COLLECT_PARTITION_STATS == 2
  PartitionStats *part_stats = &cpi->partition_stats;
#endif
#endif

#if !CONFIG_EXT_RECUR_PARTITIONS
  // Override partition costs at the edges of the frame in the same
  // way as in read_partition (see decodeframe.c).
  if (!(blk_params.has_rows && blk_params.has_cols))
    set_partition_cost_for_edge_blk(cm, xd, &part_search_state);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  // Disable rectangular partitions for inner blocks when the current block is
  // forced to only use square partitions.
  if (is_bsize_gt(bsize, cpi->sf.part_sf.use_square_partition_only_threshold)) {
    part_search_state.partition_rect_allowed[HORZ] &= !blk_params.has_rows;
    part_search_state.partition_rect_allowed[VERT] &= !blk_params.has_cols;
  }

#ifndef NDEBUG
  // Nothing should rely on the default value of this array (which is just
  // leftover from encoding the previous block). Setting it to fixed pattern
  // when debugging.
  // bit 0 is blk_skip of each plane.
  // bit 4 is initialization checking bit (1 = uninitialized).
  // See related logic in `set_blk_skip` and `is_blk_skip`.
  for (int i = (xd->tree_type == CHROMA_PART); i < num_planes; ++i) {
    memset(x->txfm_search_info.blk_skip[i], 0x11,
           sizeof(x->txfm_search_info.blk_skip[i]));
  }
#endif  // NDEBUG

  assert(bsize < BLOCK_SIZES_ALL);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  // Set buffers and offsets.
  av1_set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize,
                  &pc_tree->chroma_ref_info);

  bool search_none_after_split = false;
  bool search_none_after_rect = false;
#if CONFIG_EXT_RECUR_PARTITIONS
  if (part_search_state.forced_partition == PARTITION_INVALID) {
    if (cpi->sf.part_sf.adaptive_partition_search_order) {
      search_none_after_rect = try_none_after_rect(xd, &cm->mi_params, bsize,
#if CONFIG_CB1TO4_SPLIT
                                                   blk_params.parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
                                                   mi_row, mi_col);
    }
#if CONFIG_BLOCK_256
    // For 256X256, always search the subblocks first.
    search_none_after_split |= bsize == BLOCK_256X256;
#endif  // CONFIG_BLOCK_256
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Save rdmult before it might be changed, so it can be restored later.
  const int orig_rdmult = x->rdmult;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, NO_AQ, NULL);

  // Update rd cost of the bound using the current multiplier.
  av1_rd_cost_update(x->rdmult, &best_rdc);

  if (bsize == BLOCK_16X16 && cpi->vaq_refresh)
    x->mb_energy = av1_log_block_var(cpi, x, bsize);

#if !CONFIG_TX_PARTITION_CTX
  // Set the context.
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX
  av1_save_context(x, &x_ctx, mi_row, mi_col, bsize, num_planes);
#if CONFIG_MVP_IMPROVEMENT
  LevelBanksRDO level_banks = {
    x->e_mbd.ref_mv_bank, /* curr_level_bank*/
    x->e_mbd.ref_mv_bank, /* best_level_bank*/
#if WARP_CU_BANK
    x->e_mbd.warp_param_bank, /* curr_level_warp_bank*/
    x->e_mbd.warp_param_bank, /* best_level_warp_bank*/
#endif                        // WARP_CU_BANK
  };
#endif  // CONFIG_MVP_IMPROVEMENT
#if CONFIG_EXT_RECUR_PARTITIONS
  {
    SimpleMotionData *sms_data =
        av1_get_sms_data_entry(x->sms_bufs, mi_row, mi_col, bsize, cm->sb_size);
    sms_tree = sms_data->old_sms;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  int *partition_horz_allowed = &part_search_state.partition_rect_allowed[HORZ];
  int *partition_vert_allowed = &part_search_state.partition_rect_allowed[VERT];
#if CONFIG_EXT_RECUR_PARTITIONS
  if (part_search_state.forced_partition == PARTITION_INVALID &&
      is_bsize_gt(bsize, x->sb_enc.min_partition_size)) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    bool *prune_horz = &part_search_state.prune_rect_part[HORZ];
    bool *prune_vert = &part_search_state.prune_rect_part[VERT];
#if CONFIG_EXT_RECUR_PARTITIONS
    int do_square_split = true;
    int *sqr_split_ptr = &do_square_split;
#else
  int *sqr_split_ptr = &part_search_state.do_square_split;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    // Pruning: before searching any partition type, using source and simple
    // motion search results to prune out unlikely partitions.
    av1_prune_partitions_before_search(
        cpi, x, mi_row, mi_col, bsize, sms_tree,
        &part_search_state.partition_none_allowed, partition_horz_allowed,
        partition_vert_allowed, &part_search_state.do_rectangular_split,
        sqr_split_ptr, prune_horz, prune_vert, pc_tree);
#if CONFIG_EXT_RECUR_PARTITIONS
    part_search_state.forced_partition = get_forced_partition_type(
        cm, x, blk_params.mi_row, blk_params.mi_col, blk_params.bsize,
#if CONFIG_CB1TO4_SPLIT
        blk_params.parent_bsize,
#endif  // CONFIG_CB1TO4_SPLIT
        ptree_luma, template_tree,
#if CONFIG_EXTENDED_SDP
        pc_tree->region_type,
#endif  // CONFIG_EXTENDED_SDP
        &pc_tree->chroma_ref_info);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Pruning: eliminating partition types leading to coding block sizes
  // outside the min and max bsize limitations set from the encoder.
  av1_prune_partitions_by_max_min_bsize(
      &x->sb_enc, bsize, blk_params.has_rows && blk_params.has_cols,
      &part_search_state.partition_none_allowed, partition_horz_allowed,
#if CONFIG_EXT_RECUR_PARTITIONS
      partition_vert_allowed, NULL);
#else
      partition_vert_allowed, &part_search_state.do_square_split);
#endif

  int luma_split_flag = 0;
#if !CONFIG_EXT_RECUR_PARTITIONS
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int parent_block_width = block_size_wide[bsize];
  if (xd->tree_type == CHROMA_PART && parent_block_width >= SHARED_PART_SIZE) {
    luma_split_flag = get_luma_split_flag(bsize, mi_params, mi_row, mi_col);
  }
  // if luma blocks uses smaller blocks, then chroma will also split
  if (luma_split_flag > 3) {
    part_search_state.partition_none_allowed = BLOCK_INVALID;
    part_search_state.partition_rect_allowed[HORZ] = 0;
    part_search_state.partition_rect_allowed[VERT] = 0;
  }
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  // Partition search
BEGIN_PARTITION_SEARCH:
  // If a valid partition is required, usually when the first round cannot
  // find a valid one under the cost limit after pruning, reset the
  // limitations on partition types.
  if (x->must_find_valid_partition) {
#if CONFIG_EXT_RECUR_PARTITIONS
    init_allowed_partitions(&part_search_state, &cpi->oxcf.part_cfg,
                            &pc_tree->chroma_ref_info, xd->tree_type);
#else
    reset_part_limitations(cpi, &part_search_state);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_ML_PART_SPLIT
    part_search_state.prune_rect_part[HORZ] = 0;
    part_search_state.prune_rect_part[VERT] = 0;
    part_search_state.prune_partition_none = 0;
    part_search_state.prune_partition_split = 0;
#endif  // CONFIG_ML_PART_SPLIT
  }

  // Partition block source pixel variance.
  unsigned int pb_source_variance = UINT_MAX;
#if CONFIG_ML_PART_SPLIT
  int next_force_prune_flags[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
  // Don't use ML pruning if this is the second attempt to find a valid
  // partition.
  if (cpi->sf.part_sf.prune_split_with_ml &&
      part_search_state.forced_partition == PARTITION_INVALID &&
      !x->must_find_valid_partition &&
      is_partition_point(bsize
#if CONFIG_CB1TO4_SPLIT
                         ,
                         blk_params.parent_bsize
#endif  // CONFIG_CB1TO4_SPLIT
                         )) {
    part_search_state.prune_partition_none |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_3[0] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_3[1] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_4a[0] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_4a[1] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_4b[0] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_partition_4b[1] |= force_prune_flags[PRUNE_OTHER];
    part_search_state.prune_rect_part[HORZ] |= force_prune_flags[PRUNE_HORZ];
    part_search_state.prune_rect_part[VERT] |= force_prune_flags[PRUNE_VERT];

    // Don't want to run ML in the second stage of the forced split. Want the
    // force split to carry out without interference.
    // Note1: might still be some interference during prune split.
    // Note2: prune split doesn't mean prune both splits on l2, it means
    //        prune either one or both.
    if (!force_prune_flags[PRUNE_OTHER]) {
      int ml_result =
          av1_ml_part_split_infer(cpi, x, mi_row, mi_col, bsize, pc_tree);
      if (ml_result == ML_PART_FORCE_SPLIT) {
        part_search_state.prune_partition_none = 1;
        part_search_state.prune_partition_3[0] = 1;
        part_search_state.prune_partition_3[1] = 1;
        part_search_state.prune_partition_4a[0] = 1;
        part_search_state.prune_partition_4a[1] = 1;
        part_search_state.prune_partition_4b[0] = 1;
        part_search_state.prune_partition_4b[1] = 1;
        if (is_square_split_eligible(bsize, cm->sb_size)) {
          part_search_state.prune_rect_part[VERT] = 1;
          part_search_state.prune_rect_part[HORZ] = 1;
        } else {
          // 64x64 and smaller
          next_force_prune_flags[HORZ][PRUNE_OTHER] = 1;
          next_force_prune_flags[VERT][PRUNE_OTHER] = 1;
          next_force_prune_flags[HORZ][PRUNE_HORZ] = 1;
          next_force_prune_flags[VERT][PRUNE_VERT] = 1;
          // left with HORZ,VERT and VERT,HORZ
        }
      } else if (ml_result == ML_PART_PRUNE_SPLIT) {
        if (is_square_split_eligible(bsize, cm->sb_size)) {
          part_search_state.prune_partition_split = 1;
        } else {
          next_force_prune_flags[HORZ][PRUNE_VERT] = 1;
          next_force_prune_flags[VERT][PRUNE_HORZ] = 1;
        }
      }
    }
  }
#endif  // CONFIG_ML_PART_SPLIT
  // PARTITION_NONE search stage.
  int64_t part_none_rd = INT64_MAX;
#if CONFIG_EXTENDED_SDP
  int partition_none_allowed = 1;
  if (pc_tree->parent && pc_tree->region_type == INTRA_REGION &&
      pc_tree->parent->region_type == MIXED_INTER_INTRA_REGION &&
      xd->tree_type != CHROMA_PART)
    partition_none_allowed = 0;
  if (!frame_is_intra_only(cm) && pc_tree->region_type == INTRA_REGION)
    search_none_after_rect &= (xd->tree_type != CHROMA_PART);
  if (!search_none_after_rect && !search_none_after_split &&
      partition_none_allowed) {
#else
  if (!search_none_after_rect && !search_none_after_split) {
#endif  // CONFIG_EXTENDED_SDP
    none_partition_search(cpi, td, tile_data, x, pc_tree, sms_tree, &x_ctx,
                          &part_search_state, &best_rdc, &pb_source_variance,
                          none_rd, &part_none_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          ,
                          &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
  }
#if CONFIG_EXTENDED_SDP
  if (pc_tree->parent && pc_tree->region_type == INTRA_REGION &&
      pc_tree->parent->region_type == MIXED_INTER_INTRA_REGION &&
      xd->tree_type == CHROMA_PART) {
    *rd_cost = best_rdc;
    return part_search_state.found_best_partition;
  }
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
  if (cpi->sf.part_sf.end_part_search_after_consec_failures && x->is_whole_sb &&
      !frame_is_intra_only(cm) &&
      part_search_state.forced_partition == PARTITION_INVALID &&
      pc_tree->parent && pc_tree->parent->parent) {
    if (pc_tree->none_rd.rate == INT_MAX &&
        pc_tree->parent->none_rd.rate == INT_MAX &&
        pc_tree->parent->parent->none_rd.rate == INT_MAX &&
        part_search_state.partition_none_allowed &&
        best_rdc.rdcost < INT64_MAX) {
      part_search_state.terminate_partition_search = 1;
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // PARTITION_SPLIT search stage.
  int64_t part_split_rd = INT64_MAX;
  split_partition_search(cpi, td, tile_data, tp, x, pc_tree, sms_tree, &x_ctx,
                         &part_search_state, &best_rdc, multi_pass_mode,
                         &part_split_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                         ,
                         &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
#if CONFIG_EXT_RECUR_PARTITIONS
                         ,
                         ptree_luma, template_tree, max_recursion_depth - 1
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  );

#if !CONFIG_EXT_RECUR_PARTITIONS
  // Terminate partition search for child partition,
  // when NONE and SPLIT partition rd_costs are INT64_MAX.
  if (cpi->sf.part_sf.early_term_after_none_split &&
      part_none_rd == INT64_MAX && part_split_rd == INT64_MAX &&
      !x->must_find_valid_partition && (bsize != cm->sb_size)) {
    part_search_state.terminate_partition_search = 1;
  }

  // Prune partitions based on PARTITION_NONE and PARTITION_SPLIT.
  prune_partitions_after_split(cpi, x, sms_tree, &part_search_state, &best_rdc,
                               part_none_rd, part_split_rd);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_BLOCK_256
  if (search_none_after_split) {
    // Based on split result, decide if we want to further delay the search to
    // after rect
    assert(pc_tree->partitioning == PARTITION_SPLIT);
    for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
      const int depth =
          get_partition_depth(pc_tree->split[pc_tree->region_type][idx], 0);
#else
      const int depth = get_partition_depth(pc_tree->split[idx], 0);
#endif  // CONFIG_EXTENDED_SDP
      search_none_after_split &= depth == 0;
    }
  }
  if (cpi->sf.part_sf.prune_rect_with_split_depth && !frame_is_intra_only(cm) &&
      part_search_state.forced_partition == PARTITION_INVALID &&
#if CONFIG_EXTENDED_SDP
      pc_tree->split[pc_tree->region_type][0] &&
      pc_tree->split[pc_tree->region_type][1] &&
      pc_tree->split[pc_tree->region_type][2] &&
      pc_tree->split[pc_tree->region_type][3]
#else
      pc_tree->split[0] && pc_tree->split[1] && pc_tree->split[2] &&
      pc_tree->split[3]
#endif  // CONFIG_EXTENDED_SDP
  ) {
    int min_depth = INT_MAX, max_depth = 0;
    for (int idx = 0; idx < 4; idx++) {
#if CONFIG_EXTENDED_SDP
      const int depth =
          get_partition_depth(pc_tree->split[pc_tree->region_type][idx], 0);
#else
      const int depth = get_partition_depth(pc_tree->split[idx], 0);
#endif  // CONFIG_EXTENDED_SDP
      min_depth = AOMMIN(min_depth, depth);
      max_depth = AOMMAX(max_depth, depth);
    }
    if (min_depth > 4) {
      part_search_state.prune_rect_part[HORZ] =
          part_search_state.prune_rect_part[VERT] = true;
    }
    (void)max_depth;
  }

  if (part_search_state.forced_partition == PARTITION_INVALID &&
#if CONFIG_EXTENDED_SDP
      partition_none_allowed &&
#endif  // CONFIG_EXTENDED_SDP
      search_none_after_split) {
    none_partition_search(cpi, td, tile_data, x, pc_tree, sms_tree, &x_ctx,
                          &part_search_state, &best_rdc, &pb_source_variance,
                          none_rd, &part_none_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          ,
                          &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
  }
#endif  // CONFIG_BLOCK_256

  // Rectangular partitions search stage.
  rectangular_partition_search(
      cpi, td, tile_data, tp, x, pc_tree, &x_ctx, &part_search_state, &best_rdc,
#if CONFIG_EXT_RECUR_PARTITIONS
      multi_pass_mode, ptree_luma, template_tree, max_recursion_depth - 1,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      rect_part_win_info,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
      &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
#if CONFIG_EXTENDED_SDP
      parent_partition,
#endif  // CONFIG_EXTENDED_SDP
      part_none_rd
#if CONFIG_ML_PART_SPLIT
      ,
      next_force_prune_flags
#endif  // CONFIG_ML_PART_SPLIT
  );

  if (pb_source_variance == UINT_MAX) {
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col, num_planes, NULL);
    pb_source_variance = av1_high_get_sby_perpixel_variance(
        cpi, &x->plane[0].src, bsize, xd->bd);
  }

  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part_search_state.do_rectangular_split));
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
  if (search_none_after_rect && !search_none_after_split &&
      partition_none_allowed) {
#else
  if (search_none_after_rect && !search_none_after_split) {
#endif  // CONFIG_EXTENDED_SDP
    prune_none_with_rect_results(&part_search_state, pc_tree);
    none_partition_search(cpi, td, tile_data, x, pc_tree, sms_tree, &x_ctx,
                          &part_search_state, &best_rdc, &pb_source_variance,
                          none_rd, &part_none_rd
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          ,
                          &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if !CONFIG_EXT_RECUR_PARTITIONS
  const int ext_partition_allowed =
      part_search_state.do_rectangular_split &&
      bsize > cpi->sf.part_sf.ext_partition_eval_thresh &&
      blk_params.has_rows && blk_params.has_cols && ((luma_split_flag <= 3));

  // AB partitions search stage.
  ab_partitions_search(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       &part_search_state, &best_rdc, rect_part_win_info,
                       pb_source_variance, ext_partition_allowed
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                       ,
                       &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
  );

  // 4-way partitions search stage.
  int part4_search_allowed[NUM_PART4_TYPES] = { 1, 1 };

  // Disable 4-way partition search flags for width less than twice the
  // minimum width.
  if (blk_params.width < (blk_params.min_partition_size_1d << 2) ||
      (xd->tree_type == CHROMA_PART && bsize <= BLOCK_16X16) ||
      (luma_split_flag > 3)) {
    part4_search_allowed[HORZ4] = 0;
    part4_search_allowed[VERT4] = 0;
  } else {
    // Prune 4-way partition search.
    prune_4_way_partition_search(cpi, x, pc_tree, &part_search_state, &best_rdc,
                                 pb_source_variance, ext_partition_allowed,
                                 part4_search_allowed);
  }

  // PARTITION_HORZ_4
  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part4_search_allowed[HORZ4]));
  if (!part_search_state.terminate_partition_search &&
      part4_search_allowed[HORZ4] && blk_params.has_rows &&
      (part_search_state.do_rectangular_split ||
       av1_active_h_edge(cpi, mi_row, blk_params.mi_step))) {
    const int inc_step[NUM_PART4_TYPES] = { mi_size_high[blk_params.bsize] / 4,
                                            0 };
    // Evaluation of Horz4 partition type.
    rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       pc_tree->horizontal4, &part_search_state, &best_rdc,
                       inc_step, PARTITION_HORZ_4
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                       ,
                       &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
  }

  // PARTITION_VERT_4
  assert(IMPLIES(!cpi->oxcf.part_cfg.enable_rect_partitions,
                 !part4_search_allowed[VERT4]));
  if (!part_search_state.terminate_partition_search &&
      part4_search_allowed[VERT4] && blk_params.has_cols &&
      (part_search_state.do_rectangular_split ||
       av1_active_v_edge(cpi, mi_col, blk_params.mi_step))) {
    const int inc_step[NUM_PART4_TYPES] = { 0, mi_size_wide[blk_params.bsize] /
                                                   4 };
    // Evaluation of Vert4 partition type.
    rd_pick_4partition(cpi, td, tile_data, tp, x, &x_ctx, pc_tree,
                       pc_tree->vertical4, &part_search_state, &best_rdc,
                       inc_step, PARTITION_VERT_4
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                       ,
                       &level_banks
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
    );
  }
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXT_RECUR_PARTITIONS
  bool partition_boundaries[MAX_MIB_SQUARE] = { 0 };
  prune_ext_partitions_3way(cpi, pc_tree, &part_search_state,
                            partition_boundaries);

  const int ext_recur_depth =
      AOMMIN(max_recursion_depth - 1, cpi->sf.part_sf.ext_recur_depth);
  const bool track_ptree_luma =
      is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma, bsize);

  // PARTITION_HORZ_3
  search_partition_horz_3(&part_search_state, cpi, td, tile_data, tp, &best_rdc,
                          pc_tree, track_ptree_luma ? ptree_luma : NULL,
                          template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          multi_pass_mode, ext_recur_depth);

  // PARTITION_VERT_3
  search_partition_vert_3(&part_search_state, cpi, td, tile_data, tp, &best_rdc,
                          pc_tree, track_ptree_luma ? ptree_luma : NULL,
                          template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                          multi_pass_mode, ext_recur_depth);

#if CONFIG_EXTENDED_SDP
  if ((pc_tree->region_type != INTRA_REGION || frame_is_intra_only(cm))) {
#endif  // CONFIG_EXTENDED_SDP
    prune_ext_partitions_4way(cpi, pc_tree, &part_search_state,
                              partition_boundaries);

    // PARTITION_HORZ_4A
    search_partition_horz_4a(&part_search_state, cpi, td, tile_data, tp,
                             &best_rdc, pc_tree,
                             track_ptree_luma ? ptree_luma : NULL,
                             template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             multi_pass_mode, ext_recur_depth);

    // PARTITION_HORZ_4B
    search_partition_horz_4b(&part_search_state, cpi, td, tile_data, tp,
                             &best_rdc, pc_tree,
                             track_ptree_luma ? ptree_luma : NULL,
                             template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             multi_pass_mode, ext_recur_depth);

    // PARTITION_VERT_4A
    search_partition_vert_4a(&part_search_state, cpi, td, tile_data, tp,
                             &best_rdc, pc_tree,
                             track_ptree_luma ? ptree_luma : NULL,
                             template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             multi_pass_mode, ext_recur_depth);

    // PARTITION_VERT_4B
    search_partition_vert_4b(&part_search_state, cpi, td, tile_data, tp,
                             &best_rdc, pc_tree,
                             track_ptree_luma ? ptree_luma : NULL,
                             template_tree, &x_ctx, &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
                             multi_pass_mode, ext_recur_depth);
#if CONFIG_EXTENDED_SDP
  }
#endif  // CONFIG_EXTENDED_SDP
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (bsize == cm->sb_size && !part_search_state.found_best_partition
#if CONFIG_EXTENDED_SDP
      && ((!frame_is_intra_only(cm) &&
           pc_tree->region_type == MIXED_INTER_INTRA_REGION) ||
          ((frame_is_intra_only(cm) && pc_tree->region_type == INTRA_REGION)))
#endif  // CONFIG_EXTENDED_SDP
  ) {
    if (x->must_find_valid_partition) {
      aom_internal_error(
          &cpi->common.error, AOM_CODEC_ERROR,
          "The same superblock is recoded twice. Infinite loop detected?");
    }
    // Did not find a valid partition, go back and search again, with less
    // constraint on which partition types to search.
    x->must_find_valid_partition = 1;

#if CONFIG_COLLECT_PARTITION_STATS == 2
    part_stats->partition_redo += 1;
#endif
    goto BEGIN_PARTITION_SEARCH;
  }
#if CONFIG_EXT_RECUR_PARTITIONS && !defined(NDEBUG)
  if (template_tree && template_tree->partition != PARTITION_INVALID &&
      pc_tree->partitioning != template_tree->partition) {
    assert(0);
    printf("Mismatch with template at fr: %d, mi: (%d, %d), BLOCK_%dX%d\n",
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
           cm->current_frame.display_order_hint,
#else
           cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
           mi_row, mi_col, block_size_wide[bsize], block_size_high[bsize]);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS && !defined(NDEBUG)

#if CONFIG_EXTENDED_SDP
  if (frame_is_intra_only(cm)) pc_tree->extended_sdp_allowed_flag = 0;
  if (!frame_is_intra_only(cm) &&
      pc_tree->region_type == MIXED_INTER_INTRA_REGION && pc_tree->parent &&
      cm->seq_params.enable_sdp && pc_tree->extended_sdp_allowed_flag &&
      is_bsize_allowed_for_extended_sdp(bsize, PARTITION_HORZ)) {
    search_intra_region_partitioning(
        &part_search_state, cpi, td, tile_data, tp, &best_rdc, pc_tree,
        track_ptree_luma ? ptree_luma : NULL, template_tree, &x_ctx,
        &part_search_state,
#if CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
        &level_banks,
#endif  // CONFIG_MVP_IMPROVEMENT || WARP_CU_BANK
        multi_pass_mode, ext_recur_depth, parent_partition);
  }
#endif  // CONFIG_EXTENDED_SDP

  // Store the final rd cost
  *rd_cost = best_rdc;
#if CONFIG_MVP_IMPROVEMENT
  x->e_mbd.ref_mv_bank = level_banks.best_level_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  x->e_mbd.warp_param_bank = level_banks.best_level_warp_bank;
#endif  // WARP_CU_BANK
  pc_tree->rd_cost = best_rdc;
  if (!part_search_state.found_best_partition) {
    av1_invalid_rd_stats(&pc_tree->rd_cost);
  } else
#if CONFIG_CB1TO4_SPLIT
      if (pc_tree->parent == NULL ||
          pc_tree->parent->block_size <= BLOCK_LARGEST)
#endif  // CONFIG_CB1TO4_SPLIT
  {
#if CONFIG_EXT_RECUR_PARTITIONS
    av1_cache_best_partition(x->sms_bufs, mi_row, mi_col, bsize, cm->sb_size,
                             pc_tree->partitioning);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  }

  // Also record the best partition in simple motion data tree because it is
  // necessary for the related speed features.
#if CONFIG_EXT_RECUR_PARTITIONS
  if (sms_tree)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    sms_tree->partitioning = pc_tree->partitioning;

  if (luma_split_flag > 3) {
    assert(pc_tree->partitioning == PARTITION_SPLIT);
  }

#if CONFIG_COLLECT_PARTITION_STATS
  if (best_rdc.rate < INT_MAX && best_rdc.dist < INT64_MAX) {
    partition_decisions[pc_tree->partitioning] += 1;
  }
#endif

#if CONFIG_COLLECT_PARTITION_STATS == 1
  // If CONFIG_COLLECT_PARTITION_STATS is 1, then print out the stats for each
  // prediction block.
  FILE *f = fopen("data.csv", "a");
  fprintf(f, "%d,%d,%d,", bsize, cm->show_frame, frame_is_intra_only(cm));
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", partition_decisions[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%d,", partition_attempts[idx]);
  }
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    fprintf(f, "%ld,", partition_times[idx]);
  }
  fprintf(f, "\n");
  fclose(f);
#endif

#if CONFIG_COLLECT_PARTITION_STATS == 2
  // If CONFIG_COLLECTION_PARTITION_STATS is 2, then we print out the stats
  // for the whole clip. So we need to pass the information upstream to the
  // encoder.
  const int bsize_idx = av1_get_bsize_idx_for_part_stats(bsize);
  int *agg_attempts = part_stats->partition_attempts[bsize_idx];
  int *agg_decisions = part_stats->partition_decisions[bsize_idx];
  int64_t *agg_times = part_stats->partition_times[bsize_idx];
  for (int idx = 0; idx < EXT_PARTITION_TYPES; idx++) {
    agg_attempts[idx] += partition_attempts[idx];
    agg_decisions[idx] += partition_decisions[idx];
    agg_times[idx] += partition_times[idx];
  }
#endif

  // Reset the PC_TREE deallocation flag.
  int pc_tree_dealloc = 0;

  // If a valid partition is found and reconstruction is required for future
  // sub-blocks in the same group.
  if (part_search_state.found_best_partition && pc_tree->index != 3) {
    if (bsize == cm->sb_size) {
      // Encode the superblock.
      const int emit_output = multi_pass_mode != SB_DRY_PASS;
      const RUN_TYPE run_type = emit_output ? OUTPUT_ENABLED : DRY_RUN_NORMAL;
      const int plane_start = (xd->tree_type == CHROMA_PART);
      const int plane_end = (xd->tree_type == LUMA_PART) ? 1 : num_planes;
      for (int plane = plane_start; plane < plane_end; plane++) {
        x->cb_offset[plane] = 0;
      }
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
      x->cb_offset[xd->tree_type == CHROMA_PART] = 0;
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, run_type, bsize,
                pc_tree, xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                xd->tree_type == CHROMA_PART ? xd->sbi->ptree_root[0] : NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                NULL);
      // Dealloc the whole PC_TREE after a superblock is done.
      av1_free_pc_tree_recursive(pc_tree, num_planes, 0, 0);
      pc_tree_dealloc = 1;
    } else {
      // Encode the smaller blocks in DRY_RUN mode.
      encode_sb(cpi, td, tile_data, tp, mi_row, mi_col, DRY_RUN_NORMAL, bsize,
                pc_tree, NULL,
#if CONFIG_EXT_RECUR_PARTITIONS
                NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                NULL);
    }
  }

  int keep_tree = 0;
#if CONFIG_EXT_RECUR_PARTITIONS
  keep_tree = should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG |
                                       REUSE_INTRA_MODE_IN_INTERFRAME_FLAG);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // If the tree still exists (non-superblock), dealloc most nodes, only keep
  // nodes for the best partition and PARTITION_NONE.
  if (!pc_tree_dealloc && !keep_tree) {
    av1_free_pc_tree_recursive(pc_tree, num_planes, 1, 1);
  }

  if (bsize == cm->sb_size) {
    assert(best_rdc.rate < INT_MAX);
    assert(best_rdc.dist < INT64_MAX);
  } else {
    assert(tp_orig == *tp);
  }

  // Restore the rd multiplier.
  x->rdmult = orig_rdmult;
  return part_search_state.found_best_partition;
}
