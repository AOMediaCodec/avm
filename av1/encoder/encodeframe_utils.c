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

#include "aom_ports/system_state.h"

#include "av1/common/reconintra.h"
#include "av1/common/intra_dip.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/partition_strategy.h"
#include "av1/encoder/rdopt.h"

static AOM_INLINE int set_deltaq_rdmult(const AV1_COMP *const cpi,
                                        const MACROBLOCK *const x) {
  const AV1_COMMON *const cm = &cpi->common;
  const CommonQuantParams *quant_params = &cm->quant_params;
  return av1_compute_rd_mult(cpi, quant_params->base_qindex + x->delta_qindex +
                                      quant_params->y_dc_delta_q);
}

void av1_set_ssim_rdmult(const AV1_COMP *const cpi, MvCosts *const mv_costs,
                         const BLOCK_SIZE bsize, const int mi_row,
                         const int mi_col, int *const rdmult) {
  const AV1_COMMON *const cm = &cpi->common;

  const BLOCK_SIZE bsize_base = BLOCK_16X16;
  const int num_mi_w = mi_size_wide[bsize_base];
  const int num_mi_h = mi_size_high[bsize_base];
  const int num_cols = (cm->mi_params.mi_cols + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const int num_bcols = (mi_size_wide[bsize] + num_mi_w - 1) / num_mi_w;
  const int num_brows = (mi_size_high[bsize] + num_mi_h - 1) / num_mi_h;
  int row, col;
  double num_of_mi = 0.0;
  double geom_mean_of_scale = 0.0;

  assert(cpi->oxcf.tune_cfg.tuning == AOM_TUNE_SSIM);

  aom_clear_system_state();
  for (row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (col = mi_col / num_mi_h;
         col < num_cols && col < mi_col / num_mi_h + num_bcols; ++col) {
      const int index = row * num_cols + col;
      geom_mean_of_scale += log(cpi->ssim_rdmult_scaling_factors[index]);
      num_of_mi += 1.0;
    }
  }
  geom_mean_of_scale = exp(geom_mean_of_scale / num_of_mi);

  *rdmult = (int)((double)(*rdmult) * geom_mean_of_scale + 0.5);
  *rdmult = AOMMAX(*rdmult, 0);
  av1_set_error_per_bit(mv_costs, *rdmult);
  aom_clear_system_state();
}

// Return the end column for the current superblock, in unit of TPL blocks.
static int get_superblock_tpl_column_end(const AV1_COMMON *const cm, int mi_col,
                                         int num_mi_w) {
  // Find the start column of this superblock.
  const int sb_mi_col_start = (mi_col >> cm->mib_size_log2)
                              << cm->mib_size_log2;
  // Same but in superres upscaled dimension.
  const int sb_mi_col_start_sr =
      coded_to_superres_mi(sb_mi_col_start, cm->superres_scale_denominator);
  // Width of this superblock in mi units.
  const int sb_mi_width = mi_size_wide[cm->sb_size];
  // Same but in superres upscaled dimension.
  const int sb_mi_width_sr =
      coded_to_superres_mi(sb_mi_width, cm->superres_scale_denominator);
  // Superblock end in mi units.
  const int sb_mi_end = sb_mi_col_start_sr + sb_mi_width_sr;
  // Superblock end in TPL units.
  return (sb_mi_end + num_mi_w - 1) / num_mi_w;
}

int av1_get_hier_tpl_rdmult(const AV1_COMP *const cpi, MACROBLOCK *const x,
                            const BLOCK_SIZE bsize, const int mi_row,
                            const int mi_col, int orig_rdmult) {
  const AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));
  const int tpl_idx = cpi->gf_group.index;
  const TplDepFrame *tpl_frame = &cpi->tpl_data.tpl_frame[tpl_idx];
  const int deltaq_rdmult = set_deltaq_rdmult(cpi, x);
  if (tpl_frame->is_valid == 0) return deltaq_rdmult;
  if (!is_frame_tpl_eligible(gf_group, gf_group->index)) return deltaq_rdmult;
  if (tpl_idx >= MAX_TPL_FRAME_IDX) return deltaq_rdmult;
  if (cpi->oxcf.q_cfg.aq_mode != NO_AQ) return deltaq_rdmult;

  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int block_mi_width_sr =
      coded_to_superres_mi(mi_size_wide[bsize], cm->superres_scale_denominator);

  const BLOCK_SIZE bsize_base = BLOCK_16X16;
  const int num_mi_w = mi_size_wide[bsize_base];
  const int num_mi_h = mi_size_high[bsize_base];
  const int num_cols = (mi_cols_sr + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const int num_bcols = (block_mi_width_sr + num_mi_w - 1) / num_mi_w;
  const int num_brows = (mi_size_high[bsize] + num_mi_h - 1) / num_mi_h;
  // This is required because the end col of superblock may be off by 1 in case
  // of superres.
  const int sb_bcol_end = get_superblock_tpl_column_end(cm, mi_col, num_mi_w);
  int row, col;
  double base_block_count = 0.0;
  double geom_mean_of_scale = 0.0;
  aom_clear_system_state();
  for (row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (col = mi_col_sr / num_mi_h;
         col < num_cols && col < mi_col_sr / num_mi_h + num_bcols &&
         col < sb_bcol_end;
         ++col) {
      const int index = row * num_cols + col;
      geom_mean_of_scale += log(cpi->tpl_sb_rdmult_scaling_factors[index]);
      base_block_count += 1.0;
    }
  }
  geom_mean_of_scale = exp(geom_mean_of_scale / base_block_count);
  int rdmult = (int)((double)orig_rdmult * geom_mean_of_scale + 0.5);
  rdmult = AOMMAX(rdmult, 0);
  av1_set_error_per_bit(&x->mv_costs, rdmult);
  aom_clear_system_state();
  if (bsize == cm->sb_size) {
    const int rdmult_sb = set_deltaq_rdmult(cpi, x);
    assert(rdmult_sb == rdmult);
    (void)rdmult_sb;
  }
  return rdmult;
}

static AOM_INLINE void update_filter_type_count(FRAME_COUNTS *counts,
                                                const MACROBLOCKD *xd,
                                                const MB_MODE_INFO *mbmi) {
  const int ctx = av1_get_pred_context_switchable_interp(xd, 0);
  ++counts->switchable_interp[ctx][mbmi->interp_fltr];
}

static void reset_tx_size(MACROBLOCK *x, MB_MODE_INFO *mbmi,
                          const TX_MODE tx_mode) {
  MACROBLOCKD *const xd = &x->e_mbd;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int plane_index = xd->tree_type == CHROMA_PART;
  if (xd->lossless[mbmi->segment_id]) {
    mbmi->tx_size = TX_4X4;
  } else if (tx_mode != TX_MODE_SELECT) {
    mbmi->tx_size = tx_size_from_tx_mode(mbmi->sb_type[plane_index], tx_mode);
  } else {
    BLOCK_SIZE bsize = mbmi->sb_type[plane_index];
    TX_SIZE min_tx_size = depth_to_tx_size(MAX_TX_DEPTH, bsize);
    mbmi->tx_size = (TX_SIZE)TXSIZEMAX(mbmi->tx_size, min_tx_size);
  }
#if CONFIG_NEW_TX_PARTITION
  memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
  memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
         sizeof(mbmi->tx_partition_type));
#else
  if (is_inter_block(mbmi, xd->tree_type)) {
    memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
  }
#endif  // CONFIG_NEW_TX_PARTITION
  const int stride = xd->tx_type_map_stride;
  const int bw = mi_size_wide[mbmi->sb_type[plane_index]];
  for (int row = 0; row < mi_size_high[mbmi->sb_type[plane_index]]; ++row) {
    memset(xd->tx_type_map + row * stride, DCT_DCT,
           bw * sizeof(xd->tx_type_map[0]));
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE chroma_bsize = get_bsize_base(xd, mbmi, AOM_PLANE_U);
  for (int row = 0; row < mi_size_high[chroma_bsize]; ++row)
    memset(xd->cctx_type_map + row * xd->cctx_type_map_stride, CCTX_NONE,
           mi_size_wide[chroma_bsize] * sizeof(xd->cctx_type_map[0]));
#else
  for (int row = 0; row < mi_size_high[mbmi->sb_type[plane_index]]; ++row)
    memset(xd->cctx_type_map + row * xd->cctx_type_map_stride, CCTX_NONE,
           bw * sizeof(xd->cctx_type_map[0]));
#endif  // CONFIG_EXT_RECUR_PARTITION
  av1_zero(txfm_info->blk_skip);
  txfm_info->skip_txfm = 0;
}

// This function will copy the best reference mode information from
// MB_MODE_INFO_EXT_FRAME to MB_MODE_INFO_EXT.
static INLINE void copy_mbmi_ext_frame_to_mbmi_ext(
    MB_MODE_INFO_EXT *mbmi_ext,
    const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_best, uint8_t ref_frame_type
#if CONFIG_SKIP_MODE_ENHANCEMENT
    ,
    int skip_mode
#endif
#if CONFIG_SEP_COMP_DRL
    ,
    PREDICTION_MODE this_mode
#endif  // CONFIG_SEP_COMP_DRL
) {
#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (skip_mode) {
    memcpy(&(mbmi_ext->skip_mvp_candidate_list),
           &(mbmi_ext_best->skip_mvp_candidate_list),
           sizeof(mbmi_ext->skip_mvp_candidate_list));
  }
#endif

#if CONFIG_SEP_COMP_DRL
  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame_type);
  if (has_second_drl_by_mode(this_mode, rf)) {
    memcpy(mbmi_ext->ref_mv_stack[rf[0]], mbmi_ext_best->ref_mv_stack[0],
           sizeof(mbmi_ext->ref_mv_stack[0]));
    memcpy(mbmi_ext->weight[rf[0]], mbmi_ext_best->weight[0],
           sizeof(mbmi_ext->weight[0]));
    mbmi_ext->ref_mv_count[rf[0]] = mbmi_ext_best->ref_mv_count[0];
    memcpy(mbmi_ext->ref_mv_stack[rf[1]], mbmi_ext_best->ref_mv_stack[1],
           sizeof(mbmi_ext->ref_mv_stack[0]));
    memcpy(mbmi_ext->weight[rf[1]], mbmi_ext_best->weight[1],
           sizeof(mbmi_ext->weight[0]));
    mbmi_ext->ref_mv_count[rf[1]] = mbmi_ext_best->ref_mv_count[1];
  } else {
    memcpy(mbmi_ext->ref_mv_stack[ref_frame_type],
           mbmi_ext_best->ref_mv_stack[0], sizeof(mbmi_ext->ref_mv_stack[0]));
    memcpy(mbmi_ext->weight[ref_frame_type], mbmi_ext_best->weight[0],
           sizeof(mbmi_ext->weight[0]));
    mbmi_ext->ref_mv_count[ref_frame_type] = mbmi_ext_best->ref_mv_count[0];
  }
#else
  memcpy(mbmi_ext->ref_mv_stack[ref_frame_type], mbmi_ext_best->ref_mv_stack,
         sizeof(mbmi_ext->ref_mv_stack[0]));
  memcpy(mbmi_ext->weight[ref_frame_type], mbmi_ext_best->weight,
         sizeof(mbmi_ext->weight[0]));
  mbmi_ext->ref_mv_count[ref_frame_type] = mbmi_ext_best->ref_mv_count;
#endif  // CONFIG_SEP_COMP_DRL
  mbmi_ext->mode_context[ref_frame_type] = mbmi_ext_best->mode_context;
  memcpy(mbmi_ext->global_mvs, mbmi_ext_best->global_mvs,
         sizeof(mbmi_ext->global_mvs));

  if (ref_frame_type < INTER_REFS_PER_FRAME) {
    memcpy(mbmi_ext->warp_param_stack[ref_frame_type],
           mbmi_ext_best->warp_param_stack,
           sizeof(mbmi_ext->warp_param_stack[0]));
  }
}

void av1_update_state(const AV1_COMP *const cpi, ThreadData *td,
                      const PICK_MODE_CONTEXT *const ctx, int mi_row,
                      int mi_col, BLOCK_SIZE bsize, RUN_TYPE dry_run) {
  int i, x_idx, y;
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int num_planes = av1_num_planes(cm);
  RD_COUNTS *const rdc = &td->rd_counts;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const MB_MODE_INFO *const mi = &ctx->mic;
  MB_MODE_INFO *const mi_addr = xd->mi[0];
  const struct segmentation *const seg = &cm->seg;
  assert(bsize < BLOCK_SIZES_ALL);
  const int bw = mi_size_wide[mi->sb_type[xd->tree_type == CHROMA_PART]];
  const int bh = mi_size_high[mi->sb_type[xd->tree_type == CHROMA_PART]];
  const int mis = mi_params->mi_stride;
  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  assert(mi->sb_type[xd->tree_type == CHROMA_PART] == bsize);

  *mi_addr = *mi;
  mi_addr->chroma_ref_info = ctx->chroma_ref_info;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mi->motion_mode)) update_submi(xd, cm, ctx->submic, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  if (xd->tree_type != CHROMA_PART)
    copy_mbmi_ext_frame_to_mbmi_ext(x->mbmi_ext, &ctx->mbmi_ext_best,
                                    av1_ref_frame_type(ctx->mic.ref_frame)
#if CONFIG_SKIP_MODE_ENHANCEMENT
                                        ,
                                    mi->skip_mode
#endif
#if CONFIG_SEP_COMP_DRL
                                    ,
                                    ctx->mic.mode
#endif  // CONFIG_SEP_COMP_DRL
    );

  for (i = 0; i < num_planes; ++i) {
    const int num_blk_plane =
        (i == AOM_PLANE_Y) ? ctx->num_4x4_blk : ctx->num_4x4_blk_chroma;
    memcpy(txfm_info->blk_skip[i], ctx->blk_skip[i],
           sizeof(*txfm_info->blk_skip[i]) * num_blk_plane);
  }

  txfm_info->skip_txfm = ctx->rd_stats.skip_txfm;
  if (xd->tree_type != CHROMA_PART) {
    xd->tx_type_map = ctx->tx_type_map;
    xd->tx_type_map_stride = mi_size_wide[bsize];
    // If not dry_run, copy the transform type data into the frame level buffer.
    // Encoder will fetch tx types when writing bitstream.
    if (!dry_run) {
      const int grid_idx = get_mi_grid_idx(mi_params, mi_row, mi_col);
      TX_TYPE *const tx_type_map = mi_params->tx_type_map + grid_idx;
      const int mi_stride = mi_params->mi_stride;
      for (int blk_row = 0; blk_row < bh; ++blk_row) {
        av1_copy_array(tx_type_map + blk_row * mi_stride,
                       xd->tx_type_map + blk_row * xd->tx_type_map_stride, bw);
      }
      xd->tx_type_map = tx_type_map;
      xd->tx_type_map_stride = mi_stride;
    }
  }

  if (xd->tree_type != LUMA_PART && xd->is_chroma_ref &&
      is_cctx_allowed(cm, xd)) {
    xd->cctx_type_map = ctx->cctx_type_map;
#if CONFIG_EXT_RECUR_PARTITIONS
    const BLOCK_SIZE chroma_bsize = get_bsize_base(xd, mi, AOM_PLANE_U);
    xd->cctx_type_map_stride = mi_size_wide[chroma_bsize];
#else
    xd->cctx_type_map_stride = mi_size_wide[bsize];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    // If not dry_run, copy the cctx type data into the frame level buffer.
    // Encoder will fetch cctx types when writing bitstream.
    if (!dry_run) {
      const int mi_stride = mi_params->mi_stride;
      CctxType cur_cctx_type =
          txfm_info->skip_txfm ? CCTX_NONE : xd->cctx_type_map[0];
#if CONFIG_EXT_RECUR_PARTITIONS
      const int chroma_bw = mi_size_wide[chroma_bsize];
      const int chroma_bh = mi_size_high[chroma_bsize];
      const int grid_idx =
          get_mi_grid_idx(mi_params, mi->chroma_ref_info.mi_row_chroma_base,
                          mi->chroma_ref_info.mi_col_chroma_base);
      CctxType *const cctx_type_map = mi_params->cctx_type_map + grid_idx;
      for (int blk_row = 0; blk_row < chroma_bh; ++blk_row) {
        memset(&cctx_type_map[blk_row * mi_stride], cur_cctx_type,
               chroma_bw * sizeof(cctx_type_map[0]));
      }
#else
      // If this block is sub 8x8 in luma, derive the parent >= 8x8 block area,
      // then update its corresponding chroma area in cctx_type_map to the
      // current cctx type
      const int ss_x = pd[AOM_PLANE_U].subsampling_x;
      const int ss_y = pd[AOM_PLANE_U].subsampling_y;
      const int mi_row_offset = (mi_row & 0x01) && (bh & 0x01) && ss_y;
      const int mi_col_offset = (mi_col & 0x01) && (bw & 0x01) && ss_x;
      const int grid_idx = get_mi_grid_idx(mi_params, mi_row - mi_row_offset,
                                           mi_col - mi_col_offset);
      CctxType *const cctx_type_map = mi_params->cctx_type_map + grid_idx;
      for (int blk_row = 0; blk_row < (mi_row_offset ? 2 : bh); ++blk_row) {
        memset(&cctx_type_map[blk_row * mi_stride], cur_cctx_type,
               (mi_col_offset ? 2 : bw) * sizeof(cctx_type_map[0]));
      }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      xd->cctx_type_map = cctx_type_map;
      xd->cctx_type_map_stride = mi_stride;
    }
  }

  // If segmentation in use
  if (seg->enabled) {
    // For in frame complexity AQ copy the segment id from the segment map.
    if (cpi->oxcf.q_cfg.aq_mode == COMPLEXITY_AQ) {
      const uint8_t *const map =
          seg->update_map ? cpi->enc_seg.map : cm->last_frame_seg_map;
      mi_addr->segment_id =
          map ? get_segment_id(mi_params, map, bsize, mi_row, mi_col) : 0;
      reset_tx_size(x, mi_addr, x->txfm_search_params.tx_mode_search_type);
    }
    // Else for cyclic refresh mode update the segment map, set the segment id
    // and then update the quantizer.
    if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
        xd->tree_type == SHARED_PART) {
      av1_cyclic_refresh_update_segment(cpi, mi_addr, mi_row, mi_col, bsize,
                                        ctx->rd_stats.rate, ctx->rd_stats.dist,
                                        txfm_info->skip_txfm);
    }
    if (mi_addr->uv_mode == UV_CFL_PRED && !is_cfl_allowed(xd))
      mi_addr->uv_mode = UV_DC_PRED;
  }
  for (i = (xd->tree_type == CHROMA_PART); i < num_planes; ++i) {
    p[i].coeff = ctx->coeff[i];
    p[i].qcoeff = ctx->qcoeff[i];
    p[i].dqcoeff = ctx->dqcoeff[i];
    p[i].eobs = ctx->eobs[i];
    p[i].bobs = ctx->bobs[i];
    p[i].txb_entropy_ctx = ctx->txb_entropy_ctx[i];
  }
  for (i = 0; i < 2; ++i) pd[i].color_index_map = ctx->color_index_map[i];
  // Restore the coding context of the MB to that that was in place
  // when the mode was picked for it
  // Note: the copying here must match corresponding decoder-side copying in
  // parse_decode_block().
  // TODO(any): Refactor.
  for (y = 0; y < mi_height; y++) {
    for (x_idx = 0; x_idx < mi_width; x_idx++) {
      if ((xd->mb_to_right_edge >> (3 + MI_SIZE_LOG2)) + mi_width > x_idx &&
          (xd->mb_to_bottom_edge >> (3 + MI_SIZE_LOG2)) + mi_height > y) {
        const int mi_idx =
            get_alloc_mi_idx(mi_params, mi_row + y, mi_col + x_idx);
        xd->mi[x_idx + y * mis] = &mi_params->mi_alloc[mi_idx];
        if (xd->tree_type == LUMA_PART) {
          *(xd->mi[x_idx + y * mis]) = *mi_addr;
        } else if (xd->tree_type == CHROMA_PART) {
          xd->mi[x_idx + y * mis]->sb_type[PLANE_TYPE_UV] =
              mi_addr->sb_type[PLANE_TYPE_UV];
          xd->mi[x_idx + y * mis]->uv_mode = mi_addr->uv_mode;
          xd->mi[x_idx + y * mis]->angle_delta[PLANE_TYPE_UV] =
              mi_addr->angle_delta[PLANE_TYPE_UV];
          xd->mi[x_idx + y * mis]->cfl_alpha_signs = mi_addr->cfl_alpha_signs;
          xd->mi[x_idx + y * mis]->cfl_alpha_idx = mi_addr->cfl_alpha_idx;
          xd->mi[x_idx + y * mis]->partition = mi_addr->partition;
#if CONFIG_EXT_RECUR_PARTITIONS
          xd->mi[x_idx + y * mis]->chroma_mi_row_start =
              mi_addr->chroma_mi_row_start;
          xd->mi[x_idx + y * mis]->chroma_mi_col_start =
              mi_addr->chroma_mi_col_start;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
          xd->mi[x_idx + y * mis]
              ->palette_mode_info.palette_size[PLANE_TYPE_UV] =
              mi_addr->palette_mode_info.palette_size[PLANE_TYPE_UV];
          for (i = PALETTE_MAX_SIZE; i < 3 * PALETTE_MAX_SIZE; i++)
            xd->mi[x_idx + y * mis]->palette_mode_info.palette_colors[i] =
                mi_addr->palette_mode_info.palette_colors[i];
        } else {
          xd->mi[x_idx + y * mis] = mi_addr;
        }
      }
    }
  }

  if (cpi->oxcf.q_cfg.aq_mode)
    av1_init_plane_quantizers(cpi, x, mi_addr->segment_id);

  if (dry_run) return;

  if (mi_addr->ref_frame[0] != INTRA_FRAME) {
    if (cm->features.interp_filter == SWITCHABLE &&
        !is_warp_mode(mi_addr->motion_mode) &&
        !is_nontrans_global_motion(xd, xd->mi[0])) {
      update_filter_type_count(td->counts, xd, mi_addr);
    }

    rdc->comp_pred_diff[SINGLE_REFERENCE] += ctx->single_pred_diff;
    rdc->comp_pred_diff[COMPOUND_REFERENCE] += ctx->comp_pred_diff;
    rdc->comp_pred_diff[REFERENCE_MODE_SELECT] += ctx->hybrid_pred_diff;
  }

  const int x_inside_boundary = AOMMIN(bw, mi_params->mi_cols - mi_col);
  const int y_inside_boundary = AOMMIN(bh, mi_params->mi_rows - mi_row);
  if (cm->seq_params.order_hint_info.enable_ref_frame_mvs)
    av1_copy_frame_mvs(cm, xd, mi, mi_row, mi_col, x_inside_boundary,
                       y_inside_boundary);
}

void av1_update_inter_mode_stats(FRAME_CONTEXT *fc, FRAME_COUNTS *counts,
                                 PREDICTION_MODE mode, int16_t mode_context,
                                 const AV1_COMMON *const cm,
                                 const MACROBLOCKD *xd,
                                 const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize

) {
  (void)counts;

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    const int tip_pred_index =
        tip_pred_mode_to_index[mode - SINGLE_INTER_MODE_START];
#if CONFIG_ENTROPY_STATS
    ++counts->tip_pred_mode_cnt[tip_pred_index];
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->tip_pred_mode_cdf, tip_pred_index, TIP_PRED_MODES);
    return;
  }
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP

  if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
    const int is_warpmv_or_warp_newmv = (mode == WARPMV || mode == WARP_NEWMV);
#if CONFIG_ENTROPY_STATS
    ++counts->inter_warp_cnts[iswarpmvmode_ctx][is_warpmv_or_warp_newmv];
#endif  // CONFIG_ENTROPY_STATS
    update_cdf(fc->inter_warp_mode_cdf[iswarpmvmode_ctx],
               is_warpmv_or_warp_newmv, 2);
    if (is_warpmv_or_warp_newmv) {
      if (is_warp_newmv_allowed(cm, xd, mbmi, bsize)) {
#if CONFIG_ENTROPY_STATS
        ++counts->is_warpmv_or_warp_newmv_cnt[mode == WARPMV];
#endif  // CONFIG_ENTROPY_STATS
        update_cdf(fc->is_warpmv_or_warp_newmv_cdf, mode == WARPMV, 2);
      }
      return;
    }
#else
    update_cdf(fc->inter_warp_mode_cdf[iswarpmvmode_ctx], mode == WARPMV, 2);
    if (mode == WARPMV) return;
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  }

  const int16_t ismode_ctx = inter_single_mode_ctx(mode_context);
#if CONFIG_ENTROPY_STATS
  ++counts->inter_single_mode[ismode_ctx][mode - SINGLE_INTER_MODE_START];
#endif  // CONFIG_ENTROPY_STATS
  update_cdf(fc->inter_single_mode_cdf[ismode_ctx],
             mode - SINGLE_INTER_MODE_START, INTER_SINGLE_MODES);
}

static void update_palette_cdf(MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi,
                               FRAME_COUNTS *counts) {
  FRAME_CONTEXT *fc = xd->tile_ctx;
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int palette_bsize_ctx = av1_get_palette_bsize_ctx(bsize);

  (void)counts;
  if (mbmi->mode == DC_PRED && xd->tree_type != CHROMA_PART) {
    const int n = pmi->palette_size[0];
    const int palette_mode_ctx = av1_get_palette_mode_ctx(xd);

#if CONFIG_ENTROPY_STATS
    ++counts->palette_y_mode[palette_bsize_ctx][palette_mode_ctx][n > 0];
#endif
    update_cdf(fc->palette_y_mode_cdf[palette_bsize_ctx][palette_mode_ctx],
               n > 0, 2);
    if (n > 0) {
#if CONFIG_ENTROPY_STATS
      ++counts->palette_y_size[palette_bsize_ctx][n - PALETTE_MIN_SIZE];
#endif
      update_cdf(fc->palette_y_size_cdf[palette_bsize_ctx],
                 n - PALETTE_MIN_SIZE, PALETTE_SIZES);
    }
  }
  if (mbmi->uv_mode == UV_DC_PRED && xd->tree_type != LUMA_PART) {
    const int n = pmi->palette_size[1];
    const int palette_uv_mode_ctx = (pmi->palette_size[0] > 0);

#if CONFIG_ENTROPY_STATS
    ++counts->palette_uv_mode[palette_uv_mode_ctx][n > 0];
#endif
    update_cdf(fc->palette_uv_mode_cdf[palette_uv_mode_ctx], n > 0, 2);

    if (n > 0) {
#if CONFIG_ENTROPY_STATS
      ++counts->palette_uv_size[palette_bsize_ctx][n - PALETTE_MIN_SIZE];
#endif
      update_cdf(fc->palette_uv_size_cdf[palette_bsize_ctx],
                 n - PALETTE_MIN_SIZE, PALETTE_SIZES);
    }
  }
}

static INLINE void update_fsc_cdf(const AV1_COMMON *const cm, MACROBLOCKD *xd,
#if CONFIG_ENTROPY_STATS
                                  FRAME_COUNTS *counts,
#endif  // CONFIG_ENTROPY_STATS
                                  const int intraonly) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  if (allow_fsc_intra(cm,
#if !CONFIG_LOSSLESS_DPCM
                      xd,
#endif  // CONFIG_LOSSLESS_DPCM
                      bsize, mbmi)) {
#if CONFIG_ENTROPY_STATS
    const int ctx = get_fsc_mode_ctx(xd, intraonly);
    ++counts->fsc_mode[ctx][fsc_bsize_groups[bsize]]
                      [mbmi->fsc_mode[xd->tree_type == CHROMA_PART]];
#endif  // CONFIG_ENTROPY_STATS
    aom_cdf_prob *fsc_cdf = get_fsc_mode_cdf(xd, bsize, intraonly);
    update_cdf(fsc_cdf, mbmi->fsc_mode[xd->tree_type == CHROMA_PART],
               FSC_MODES);
  }
}

void av1_sum_intra_stats(const AV1_COMMON *const cm, FRAME_COUNTS *counts,
                         MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi) {
  FRAME_CONTEXT *fc = xd->tile_ctx;
#if CONFIG_ENTROPY_STATS
  int cdf_idx = cm->coef_cdf_category;
#endif
#if !CONFIG_AIMC
  const PREDICTION_MODE y_mode = mbmi->mode;
#endif  // !CONFIG_AIMC
  (void)counts;
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  if (xd->tree_type != CHROMA_PART) {
    const int intraonly = frame_is_intra_only(cm);
#if CONFIG_AIMC
#if CONFIG_LOSSLESS_DPCM
    if (xd->lossless[mbmi->segment_id]) {
      update_cdf(fc->dpcm_cdf, mbmi->use_dpcm_y, 2);
      if (mbmi->use_dpcm_y == 0) {
        const int context = get_y_mode_idx_ctx(xd);
        const int mode_idx = mbmi->y_mode_idx;
        int mode_set_index = mode_idx < FIRST_MODE_COUNT ? 0 : 1;
        mode_set_index += ((mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
#if CONFIG_ENTROPY_STATS
        ++counts->y_mode_set_idx[mode_set_index];
#endif
        update_cdf(fc->y_mode_set_cdf, mode_set_index, INTRA_MODE_SETS);
        if (mode_set_index == 0) {
#if CONFIG_ENTROPY_STATS
          ++counts->y_mode_idx_0[context][mode_idx];
#endif
          update_cdf(fc->y_mode_idx_cdf_0[context], mode_idx, FIRST_MODE_COUNT);
        } else {
          const int mode_idx_in_set = mode_idx - FIRST_MODE_COUNT -
                                      SECOND_MODE_COUNT * (mode_set_index - 1);
#if CONFIG_ENTROPY_STATS
          ++counts->y_mode_idx_1[context][mode_idx_in_set];
#endif
          update_cdf(fc->y_mode_idx_cdf_1[context], mode_idx_in_set,
                     SECOND_MODE_COUNT);
        }
      } else {
        update_cdf(fc->dpcm_vert_horz_cdf, mbmi->dpcm_mode_y, 2);
      }
    } else {
      const int context = get_y_mode_idx_ctx(xd);
      const int mode_idx = mbmi->y_mode_idx;
      int mode_set_index = mode_idx < FIRST_MODE_COUNT ? 0 : 1;
      mode_set_index += ((mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
#if CONFIG_ENTROPY_STATS
      ++counts->y_mode_set_idx[mode_set_index];
#endif
      update_cdf(fc->y_mode_set_cdf, mode_set_index, INTRA_MODE_SETS);
      if (mode_set_index == 0) {
#if CONFIG_ENTROPY_STATS
        ++counts->y_mode_idx_0[context][mode_idx];
#endif
        update_cdf(fc->y_mode_idx_cdf_0[context], mode_idx, FIRST_MODE_COUNT);
      } else {
        const int mode_idx_in_set = mode_idx - FIRST_MODE_COUNT -
                                    SECOND_MODE_COUNT * (mode_set_index - 1);
#if CONFIG_ENTROPY_STATS
        ++counts->y_mode_idx_1[context][mode_idx_in_set];
#endif
        update_cdf(fc->y_mode_idx_cdf_1[context], mode_idx_in_set,
                   SECOND_MODE_COUNT);
      }
    }
#else  // CONFIG_LOSSLESS_DPCM
    const int context = get_y_mode_idx_ctx(xd);
    const int mode_idx = mbmi->y_mode_idx;
    int mode_set_index = mode_idx < FIRST_MODE_COUNT ? 0 : 1;
    mode_set_index += ((mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
#if CONFIG_ENTROPY_STATS
    ++counts->y_mode_set_idx[mode_set_index];
#endif
    update_cdf(fc->y_mode_set_cdf, mode_set_index, INTRA_MODE_SETS);
    if (mode_set_index == 0) {
#if CONFIG_ENTROPY_STATS
      ++counts->y_mode_idx_0[context][mode_idx];
#endif
      update_cdf(fc->y_mode_idx_cdf_0[context], mode_idx, FIRST_MODE_COUNT);
    } else {
      const int mode_idx_in_set = mode_idx - FIRST_MODE_COUNT -
                                  SECOND_MODE_COUNT * (mode_set_index - 1);
#if CONFIG_ENTROPY_STATS
      ++counts->y_mode_idx_1[context][mode_idx_in_set];
#endif
      update_cdf(fc->y_mode_idx_cdf_1[context], mode_idx_in_set,
                 SECOND_MODE_COUNT);
    }
#endif  // CONFIG_LOSSLESS_DPCM
    update_fsc_cdf(cm, xd,
#if CONFIG_ENTROPY_STATS
                   counts,
#endif  // CONFIG_ENTROPY_STATS
                   intraonly);
#else
    if (intraonly) {
#if CONFIG_ENTROPY_STATS
      const int neighbor0_ctx = get_y_mode_ctx(xd->neighbors[0]);
      const int neighbor1_ctx = get_y_mode_ctx(xd->neighbors[1]);
      ++counts->kf_y_mode[neighbor0_ctx][neighbor1_ctx][y_mode];
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(get_y_mode_cdf(fc, xd->neighbors[0], xd->neighbors[1]), y_mode,
                 INTRA_MODES);
    } else {
#if CONFIG_ENTROPY_STATS
      ++counts->y_mode[size_group_lookup[bsize]][y_mode];
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(fc->y_mode_cdf[size_group_lookup[bsize]], y_mode, INTRA_MODES);
    }
    update_fsc_cdf(cm, xd,
#if CONFIG_ENTROPY_STATS
                   counts,
#endif  // CONFIG_ENTROPY_STATS
                   intraonly);
#endif  // CONFIG_AIMC
    if (cm->seq_params.enable_mrls && av1_is_directional_mode(mbmi->mode)) {
#if CONFIG_LOSSLESS_DPCM
      if (xd->lossless[mbmi->segment_id]) {
        if (mbmi->use_dpcm_y == 0) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
          int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
          update_cdf(fc->mrl_index_cdf[mrl_ctx], mbmi->mrl_index,
                     MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
          ++counts->mrl_index[mrl_ctx][mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#else
          update_cdf(fc->mrl_index_cdf, mbmi->mrl_index, MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
          ++counts->mrl_index[mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
        }
      } else {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
        int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
        update_cdf(fc->mrl_index_cdf[mrl_ctx], mbmi->mrl_index,
                   MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
        ++counts->mrl_index[mrl_ctx][mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#else
        update_cdf(fc->mrl_index_cdf, mbmi->mrl_index, MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
        ++counts->mrl_index[mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
      }
#else  // CONFIG_LOSSLESS_DPCM
#if CONFIG_IMPROVED_INTRA_DIR_PRED
      int mrl_ctx = get_mrl_index_ctx(xd->neighbors[0], xd->neighbors[1]);
      update_cdf(fc->mrl_index_cdf[mrl_ctx], mbmi->mrl_index, MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
      ++counts->mrl_index[mrl_ctx][mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#else
      update_cdf(fc->mrl_index_cdf, mbmi->mrl_index, MRL_LINE_NUMBER);
#if CONFIG_ENTROPY_STATS
      ++counts->mrl_index[mbmi->mrl_index];
#endif  // CONFIG_ENTROPY_STATS
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#endif  // CONFIG_LOSSLESS_DPCM
    }
    if (av1_filter_intra_allowed(cm, mbmi
#if CONFIG_LOSSLESS_DPCM
                                 ,
                                 xd
#endif
                                 )) {
      const int use_filter_intra_mode =
          mbmi->filter_intra_mode_info.use_filter_intra;
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_ENTROPY_STATS
      ++counts->filter_intra[use_filter_intra_mode];
      if (use_filter_intra_mode) {
        ++counts->filter_intra_mode[mbmi->filter_intra_mode_info
                                        .filter_intra_mode];
      }
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(fc->filter_intra_cdfs, use_filter_intra_mode, 2);
#else
#if CONFIG_ENTROPY_STATS
      ++counts->filter_intra[bsize][use_filter_intra_mode];
      if (use_filter_intra_mode) {
        ++counts->filter_intra_mode[mbmi->filter_intra_mode_info
                                        .filter_intra_mode];
      }
#endif  // CONFIG_ENTROPY_STATS
      update_cdf(fc->filter_intra_cdfs[bsize], use_filter_intra_mode, 2);
#endif  // CONFIG_D149_CTX_MODELING_OPT
      if (use_filter_intra_mode) {
        update_cdf(fc->filter_intra_mode_cdf,
                   mbmi->filter_intra_mode_info.filter_intra_mode,
                   FILTER_INTRA_MODES);
      }
    }
#if CONFIG_DIP
    if (av1_intra_dip_allowed(cm, mbmi)) {
      const int use_intra_dip = mbmi->use_intra_dip;
      int ctx = get_intra_dip_ctx(xd->neighbors[0], xd->neighbors[1], bsize);
#if CONFIG_ENTROPY_STATS
      ++counts->intra_dip[cdf_idx][ctx][use_intra_dip];
      if (use_intra_dip) {
        ++counts->intra_dip_mode_n6[mbmi->intra_dip_mode & 15];
      }
#endif
      aom_cdf_prob *cdf = fc->intra_dip_cdf[ctx];
      update_cdf(cdf, use_intra_dip, 2);
      if (use_intra_dip) {
        int n_modes = av1_intra_dip_modes(bsize);
        aom_cdf_prob *mode_cdf = xd->tile_ctx->intra_dip_mode_n6_cdf;
        update_cdf(mode_cdf, mbmi->intra_dip_mode & 15, n_modes);
      }
    }
#endif  // CONFIG_DIP
#if !CONFIG_AIMC
    if (av1_is_directional_mode(mbmi->mode) && av1_use_angle_delta(bsize)) {
#if CONFIG_ENTROPY_STATS
      ++counts->angle_delta[mbmi->mode - V_PRED]
                           [mbmi->angle_delta[PLANE_TYPE_Y] + MAX_ANGLE_DELTA];
#endif

      update_cdf(fc->angle_delta_cdf[PLANE_TYPE_Y][mbmi->mode - V_PRED],
                 mbmi->angle_delta[PLANE_TYPE_Y] + MAX_ANGLE_DELTA,
                 2 * MAX_ANGLE_DELTA + 1);
    }
#endif  // !CONFIG_AIMC
  }

  if (!xd->is_chroma_ref) return;
  if (xd->tree_type != LUMA_PART) {
    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
    const CFL_ALLOWED_TYPE cfl_allowed = is_cfl_allowed(xd);
#if CONFIG_AIMC
    const int uv_context = av1_is_directional_mode(mbmi->mode) ? 1 : 0;
#endif  // CONFIG_AIMC
#if CONFIG_ENTROPY_STATS
#if CONFIG_AIMC
    if (cfl_allowed) {
      const int cfl_ctx = get_cfl_ctx(xd);
      ++counts->cfl_mode[cfl_ctx][uv_mode == UV_CFL_PRED];
      if (uv_mode != UV_CFL_PRED) {
        ++counts->uv_mode[uv_context][uv_mode];
      }
    } else {
      ++counts->uv_mode[uv_context][uv_mode];
    }
#else
    ++counts->uv_mode[cfl_allowed][y_mode][uv_mode];
#endif  // CONFIG_AIMC
#endif  // CONFIG_ENTROPY_STATS
#if CONFIG_AIMC
    if (cfl_allowed) {
      const int cfl_ctx = get_cfl_ctx(xd);
      update_cdf(fc->cfl_cdf[cfl_ctx], mbmi->uv_mode == UV_CFL_PRED, 2);
      if (mbmi->uv_mode != UV_CFL_PRED) {
#if CONFIG_LOSSLESS_DPCM
        if (xd->lossless[mbmi->segment_id]) {
          update_cdf(fc->dpcm_uv_cdf, mbmi->use_dpcm_uv, 2);
          if (mbmi->use_dpcm_uv == 0) {
            update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                       UV_INTRA_MODES - 1);
          } else {
            update_cdf(fc->dpcm_uv_vert_horz_cdf, mbmi->dpcm_mode_uv, 2);
          }
        } else {
          update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                     UV_INTRA_MODES - 1);
        }
#else   // CONFIG_LOSSLESS_DPCM
        update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                   UV_INTRA_MODES - 1);
#endif  // CONFIG_LOSSLESS_DPCM
      }
    } else {
#if CONFIG_LOSSLESS_DPCM
      if (xd->lossless[mbmi->segment_id]) {
        update_cdf(fc->dpcm_uv_cdf, mbmi->use_dpcm_uv, 2);
        if (mbmi->use_dpcm_uv == 0) {
          update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                     UV_INTRA_MODES - 1);
        } else {
          update_cdf(fc->dpcm_uv_vert_horz_cdf, mbmi->dpcm_mode_uv, 2);
        }
      } else {
        update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                   UV_INTRA_MODES - 1);
      }
#else   // CONFIG_LOSSLESS_DPCM
      update_cdf(fc->uv_mode_cdf[uv_context], mbmi->uv_mode_idx,
                 UV_INTRA_MODES - 1);
#endif  // CONFIG_LOSSLESS_DPCM
    }
    if (mbmi->uv_mode == UV_CFL_PRED) {
#if CONFIG_ENTROPY_STATS
      ++counts->cfl_index[mbmi->cfl_idx];
#endif
#if CONFIG_ENABLE_MHCCP
      update_cdf(fc->cfl_index_cdf, mbmi->cfl_idx, CFL_TYPE_COUNT - 1);
      if (mbmi->cfl_idx == CFL_MULTI_PARAM_V) {
        aom_cdf_prob *filter_dir_cdf = get_mhccp_dir_cdf(xd, bsize);
        update_cdf(filter_dir_cdf, mbmi->mh_dir, MHCCP_MODE_NUM);
      }
#else
      update_cdf(fc->cfl_index_cdf, mbmi->cfl_idx, CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
    }
#else
    update_cdf(fc->uv_mode_cdf[cfl_allowed][y_mode], uv_mode,
               UV_INTRA_MODES - !cfl_allowed);
#endif  // CONFIG_AIMC
    if (uv_mode == UV_CFL_PRED) {
      const int8_t joint_sign = mbmi->cfl_alpha_signs;
      const uint8_t idx = mbmi->cfl_alpha_idx;

#if CONFIG_ENTROPY_STATS
      ++counts->cfl_sign[joint_sign];
#endif
      update_cdf(fc->cfl_sign_cdf, joint_sign, CFL_JOINT_SIGNS);
      if (CFL_SIGN_U(joint_sign) != CFL_SIGN_ZERO) {
        aom_cdf_prob *cdf_u = fc->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];

#if CONFIG_ENTROPY_STATS
        ++counts->cfl_alpha[CFL_CONTEXT_U(joint_sign)][CFL_IDX_U(idx)];
#endif
        update_cdf(cdf_u, CFL_IDX_U(idx), CFL_ALPHABET_SIZE);
      }
      if (CFL_SIGN_V(joint_sign) != CFL_SIGN_ZERO) {
        aom_cdf_prob *cdf_v = fc->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];

#if CONFIG_ENTROPY_STATS
        ++counts->cfl_alpha[CFL_CONTEXT_V(joint_sign)][CFL_IDX_V(idx)];
#endif
        update_cdf(cdf_v, CFL_IDX_V(idx), CFL_ALPHABET_SIZE);
      }
    }
#if !CONFIG_AIMC
    if (av1_is_directional_mode(get_uv_mode(uv_mode)) &&
        av1_use_angle_delta(bsize)) {
#if CONFIG_ENTROPY_STATS
      ++counts->angle_delta[uv_mode - UV_V_PRED]
                           [mbmi->angle_delta[PLANE_TYPE_UV] + MAX_ANGLE_DELTA];
#endif
      if (cm->seq_params.enable_sdp)
        update_cdf(fc->angle_delta_cdf[PLANE_TYPE_UV][uv_mode - UV_V_PRED],
                   mbmi->angle_delta[PLANE_TYPE_UV] + MAX_ANGLE_DELTA,
                   2 * MAX_ANGLE_DELTA + 1);
      else
        update_cdf(fc->angle_delta_cdf[PLANE_TYPE_Y][uv_mode - UV_V_PRED],
                   mbmi->angle_delta[PLANE_TYPE_UV] + MAX_ANGLE_DELTA,
                   2 * MAX_ANGLE_DELTA + 1);
    }
#endif  // !CONFIG_AIMC
  }
  if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
    update_palette_cdf(xd, mbmi, counts);
  }
}

void av1_restore_context(const AV1_COMMON *cm, MACROBLOCK *x,
                         const RD_SEARCH_MACROBLOCK_CONTEXT *ctx, int mi_row,
                         int mi_col, BLOCK_SIZE bsize, const int num_planes) {
  (void)cm;
  MACROBLOCKD *xd = &x->e_mbd;
  int p;
  const int num_4x4_blocks_wide = mi_size_wide[bsize];
  const int num_4x4_blocks_high = mi_size_high[bsize];
  int mi_width = mi_size_wide[bsize];
  int mi_height = mi_size_high[bsize];
  for (p = (xd->tree_type == CHROMA_PART); p < num_planes; p++) {
    int tx_col = mi_col;
    int tx_row = mi_row & MAX_MIB_MASK;
    memcpy(
        xd->above_entropy_context[p] + (tx_col >> xd->plane[p].subsampling_x),
        ctx->a + num_4x4_blocks_wide * p,
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_wide) >>
            xd->plane[p].subsampling_x);
    memcpy(xd->left_entropy_context[p] + (tx_row >> xd->plane[p].subsampling_y),
           ctx->l + num_4x4_blocks_high * p,
           (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_high) >>
               xd->plane[p].subsampling_y);
    memcpy(xd->above_partition_context[p] + mi_col, ctx->sa + mi_width * p,
           sizeof(*xd->above_partition_context[p]) * mi_width);
    memcpy(xd->left_partition_context[p] + (mi_row & MAX_MIB_MASK),
           ctx->sl + mi_height * p,
           sizeof(xd->left_partition_context[p][0]) * mi_height);
  }
#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context = ctx->p_ta;
  xd->left_txfm_context = ctx->p_tl;
  memcpy(xd->above_txfm_context, ctx->ta,
         sizeof(*xd->above_txfm_context) * mi_width);
  memcpy(xd->left_txfm_context, ctx->tl,
         sizeof(*xd->left_txfm_context) * mi_height);
#endif  // !CONFIG_TX_PARTITION_CTX
  av1_mark_block_as_not_coded(xd, mi_row, mi_col, bsize, cm->sb_size);
}

void av1_save_context(const MACROBLOCK *x, RD_SEARCH_MACROBLOCK_CONTEXT *ctx,
                      int mi_row, int mi_col, BLOCK_SIZE bsize,
                      const int num_planes) {
  const MACROBLOCKD *xd = &x->e_mbd;
  int p;
  int mi_width = mi_size_wide[bsize];
  int mi_height = mi_size_high[bsize];

  // buffer the above/left context information of the block in search.
  for (p = (xd->tree_type == CHROMA_PART); p < num_planes; ++p) {
    int tx_col = mi_col;
    int tx_row = mi_row & MAX_MIB_MASK;
    memcpy(
        ctx->a + mi_width * p,
        xd->above_entropy_context[p] + (tx_col >> xd->plane[p].subsampling_x),
        (sizeof(ENTROPY_CONTEXT) * mi_width) >> xd->plane[p].subsampling_x);
    memcpy(ctx->l + mi_height * p,
           xd->left_entropy_context[p] + (tx_row >> xd->plane[p].subsampling_y),
           (sizeof(ENTROPY_CONTEXT) * mi_height) >> xd->plane[p].subsampling_y);
    memcpy(ctx->sa + mi_width * p, xd->above_partition_context[p] + mi_col,
           sizeof(*xd->above_partition_context[p]) * mi_width);
    memcpy(ctx->sl + mi_height * p,
           xd->left_partition_context[p] + (mi_row & MAX_MIB_MASK),
           sizeof(xd->left_partition_context[p][0]) * mi_height);
  }
#if !CONFIG_TX_PARTITION_CTX
  memcpy(ctx->ta, xd->above_txfm_context,
         sizeof(*xd->above_txfm_context) * mi_width);
  memcpy(ctx->tl, xd->left_txfm_context,
         sizeof(*xd->left_txfm_context) * mi_height);
  ctx->p_ta = xd->above_txfm_context;
  ctx->p_tl = xd->left_txfm_context;
#endif  // !CONFIG_TX_PARTITION_CTX
}

static void set_partial_sb_partition(const AV1_COMMON *const cm,
                                     MB_MODE_INFO *mi, int bh_in, int bw_in,
                                     int mi_rows_remaining,
                                     int mi_cols_remaining, BLOCK_SIZE bsize,
                                     MB_MODE_INFO **mib) {
  int bh = bh_in;
  int r, c;
  for (r = 0; r < cm->mib_size; r += bh) {
    int bw = bw_in;
    for (c = 0; c < cm->mib_size; c += bw) {
      const int grid_index = get_mi_grid_idx(&cm->mi_params, r, c);
      const int mi_index = get_alloc_mi_idx(&cm->mi_params, r, c);
      mib[grid_index] = mi + mi_index;
      mib[grid_index]->sb_type[PLANE_TYPE_Y] =
          mib[grid_index]->sb_type[PLANE_TYPE_UV] = find_partition_size(
              bsize, mi_rows_remaining - r, mi_cols_remaining - c, &bh, &bw);
    }
  }
}

// This function attempts to set all mode info entries in a given superblock
// to the same block partition size.
// However, at the bottom and right borders of the image the requested size
// may not be allowed in which case this code attempts to choose the largest
// allowable partition.
void av1_set_fixed_partitioning(AV1_COMP *cpi, const TileInfo *const tile,
                                MB_MODE_INFO **mib, int mi_row, int mi_col,
                                BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_rows_remaining = tile->mi_row_end - mi_row;
  const int mi_cols_remaining = tile->mi_col_end - mi_col;
  MB_MODE_INFO *const mi_upper_left =
      mi_params->mi_alloc + get_alloc_mi_idx(mi_params, mi_row, mi_col);
  int bh = mi_size_high[bsize];
  int bw = mi_size_wide[bsize];

  assert(bsize >= mi_params->mi_alloc_bsize &&
         "Attempted to use bsize < mi_params->mi_alloc_bsize");
  assert((mi_rows_remaining > 0) && (mi_cols_remaining > 0));

  // Apply the requested partition size to the SB if it is all "in image"
  if ((mi_cols_remaining >= cm->mib_size) &&
      (mi_rows_remaining >= cm->mib_size)) {
    for (int block_row = 0; block_row < cm->mib_size; block_row += bh) {
      for (int block_col = 0; block_col < cm->mib_size; block_col += bw) {
        const int grid_index = get_mi_grid_idx(mi_params, block_row, block_col);
        const int mi_index = get_alloc_mi_idx(mi_params, block_row, block_col);
        mib[grid_index] = mi_upper_left + mi_index;
        mib[grid_index]->sb_type[PLANE_TYPE_Y] = bsize;
        mib[grid_index]->sb_type[PLANE_TYPE_UV] = bsize;
      }
    }
  } else {
    // Else this is a partial SB.
    set_partial_sb_partition(cm, mi_upper_left, bh, bw, mi_rows_remaining,
                             mi_cols_remaining, bsize, mib);
  }
}
int av1_is_leaf_split_partition(AV1_COMMON *cm, MACROBLOCKD *const xd,
                                int mi_row, int mi_col, BLOCK_SIZE bsize) {
  const int bs = mi_size_wide[bsize];
  const int hbs = bs / 2;
  assert(bsize >= BLOCK_8X8);
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);

  for (int i = 0; i < 4; i++) {
    int x_idx = (i & 1) * hbs;
    int y_idx = (i >> 1) * hbs;
    if ((mi_row + y_idx >= cm->mi_params.mi_rows) ||
        (mi_col + x_idx >= cm->mi_params.mi_cols))
      return 0;
    if (get_partition(cm, xd->tree_type == CHROMA_PART, mi_row + y_idx,
                      mi_col + x_idx, subsize) != PARTITION_NONE &&
        subsize != BLOCK_8X8)
      return 0;
  }
  return 1;
}

int av1_get_rdmult_delta(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int orig_rdmult) {
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));
  const int tpl_idx = cpi->gf_group.index;
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  int tpl_stride = tpl_frame->stride;
  int64_t intra_cost = 0;
  int64_t mc_dep_cost = 0;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  if (tpl_frame->is_valid == 0) return orig_rdmult;

  if (!is_frame_tpl_eligible(gf_group, gf_group->index)) return orig_rdmult;

  if (cpi->gf_group.index >= MAX_TPL_FRAME_IDX) return orig_rdmult;

#ifndef NDEBUG
  int mi_count = 0;
#endif  // NDEBUG
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int step = 1 << block_mis_log2;
  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) continue;
      TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      intra_cost += this_stats->recrf_dist << RDDIV_BITS;
      mc_dep_cost += (this_stats->recrf_dist << RDDIV_BITS) + mc_dep_delta;
#ifndef NDEBUG
      mi_count++;
#endif  // NDEBUG
    }
  }
  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);

  aom_clear_system_state();

  double beta = 1.0;
  if (mc_dep_cost > 0 && intra_cost > 0) {
    const double r0 = cpi->rd.r0;
    const double rk = (double)intra_cost / mc_dep_cost;
    beta = (r0 / rk);
  }

  int rdmult = av1_get_adaptive_rdmult(cpi, beta);

  aom_clear_system_state();

  rdmult = AOMMIN(rdmult, orig_rdmult * 3 / 2);
  rdmult = AOMMAX(rdmult, orig_rdmult * 1 / 2);

  rdmult = AOMMAX(1, rdmult);

  return rdmult;
}

// Checks to see if a super block is on a horizontal image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int av1_active_h_edge(const AV1_COMP *cpi, int mi_row, int mi_step) {
  int top_edge = 0;
  int bottom_edge = cpi->common.mi_params.mi_rows;
  int is_active_h_edge = 0;

  if (((top_edge >= mi_row) && (top_edge < (mi_row + mi_step))) ||
      ((bottom_edge >= mi_row) && (bottom_edge < (mi_row + mi_step)))) {
    is_active_h_edge = 1;
  }
  return is_active_h_edge;
}

// Checks to see if a super block is on a vertical image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int av1_active_v_edge(const AV1_COMP *cpi, int mi_col, int mi_step) {
  int left_edge = 0;
  int right_edge = cpi->common.mi_params.mi_cols;
  int is_active_v_edge = 0;

  if (((left_edge >= mi_col) && (left_edge < (mi_col + mi_step))) ||
      ((right_edge >= mi_col) && (right_edge < (mi_col + mi_step)))) {
    is_active_v_edge = 1;
  }
  return is_active_v_edge;
}

void av1_get_tpl_stats_sb(AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row,
                          int mi_col, SuperBlockEnc *sb_enc) {
  sb_enc->tpl_data_count = 0;

  if (!cpi->oxcf.algo_cfg.enable_tpl_model) return;
  if (cpi->common.current_frame.frame_type == KEY_FRAME) return;
  const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
  if (update_type == INTNL_OVERLAY_UPDATE || update_type == OVERLAY_UPDATE ||
      update_type == KFFLT_OVERLAY_UPDATE)
    return;
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));

  AV1_COMMON *const cm = &cpi->common;
  const int gf_group_index = cpi->gf_group.index;
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[gf_group_index];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  int tpl_stride = tpl_frame->stride;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  if (tpl_frame->is_valid == 0) return;
  if (gf_group_index >= MAX_TPL_FRAME_IDX) return;

  int mi_count = 0;
  int count = 0;
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  // mi_cols_sr is mi_cols at superres case.
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

  // TPL store unit size is not the same as the motion estimation unit size.
  // Here always use motion estimation size to avoid getting repetitive inter/
  // intra cost.
  const BLOCK_SIZE tpl_bsize = convert_length_to_bsize(tpl_data->tpl_bsize_1d);
  assert(mi_size_wide[tpl_bsize] == mi_size_high[tpl_bsize]);
  const int row_step = mi_size_high[tpl_bsize];
  const int col_step_sr = coded_to_superres_mi(mi_size_wide[tpl_bsize],
                                               cm->superres_scale_denominator);

  // Stride is only based on SB size, and we fill in values for every 16x16
  // block in a SB.
  sb_enc->tpl_stride = (mi_col_end_sr - mi_col_sr) / col_step_sr;

  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      assert(count < MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);
      // Handle partial SB, so that no invalid values are used later.
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) {
        sb_enc->tpl_inter_cost[count] = INT64_MAX;
        sb_enc->tpl_intra_cost[count] = INT64_MAX;
        for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
          sb_enc->tpl_mv[count][i].as_int = INVALID_MV;
        }
        count++;
        continue;
      }

      TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
          row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];
      sb_enc->tpl_inter_cost[count] = this_stats->inter_cost;
      sb_enc->tpl_intra_cost[count] = this_stats->intra_cost;
      memcpy(sb_enc->tpl_mv[count], this_stats->mv, sizeof(this_stats->mv));
      mi_count++;
      count++;
    }
  }

  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);
  sb_enc->tpl_data_count = mi_count;
}

// analysis_type 0: Use mc_dep_cost and intra_cost
// analysis_type 1: Use count of best inter predictor chosen
// analysis_type 2: Use cost reduction from intra to inter for best inter
//                  predictor chosen
int av1_get_q_for_deltaq_objective(AV1_COMP *const cpi, BLOCK_SIZE bsize,
                                   int mi_row, int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_group.index < cpi->gf_group.size));
  const int tpl_idx = cpi->gf_group.index;
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  int tpl_stride = tpl_frame->stride;
  int64_t intra_cost = 0;
  int64_t mc_dep_cost = 0;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];
  const int base_qindex = cm->quant_params.base_qindex;

  if (tpl_frame->is_valid == 0) return base_qindex;

  if (!is_frame_tpl_eligible(gf_group, gf_group->index)) return base_qindex;

  if (cpi->gf_group.index >= MAX_TPL_FRAME_IDX) return base_qindex;

#ifndef NDEBUG
  int mi_count = 0;
#endif  // NDEBUG
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int step = 1 << block_mis_log2;
  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < mi_row + mi_high; row += row_step) {
    for (int col = mi_col_sr; col < mi_col_end_sr; col += col_step_sr) {
      if (row >= cm->mi_params.mi_rows || col >= mi_cols_sr) continue;
      TplDepStats *this_stats =
          &tpl_stats[av1_tpl_ptr_pos(row, col, tpl_stride, block_mis_log2)];
      int64_t mc_dep_delta =
          RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                 this_stats->mc_dep_dist);
      intra_cost += this_stats->recrf_dist << RDDIV_BITS;
      mc_dep_cost += (this_stats->recrf_dist << RDDIV_BITS) + mc_dep_delta;
#ifndef NDEBUG
      mi_count++;
#endif  // NDEBUG
    }
  }
  assert(mi_count <= MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB);

  aom_clear_system_state();

  int offset = 0;
  double beta = 1.0;
  if (mc_dep_cost > 0 && intra_cost > 0) {
    const double r0 = cpi->rd.r0;
    const double rk = (double)intra_cost / mc_dep_cost;
    beta = (r0 / rk);
    assert(beta > 0.0);
  }
  offset = av1_get_deltaq_offset(cpi, base_qindex, beta);
  aom_clear_system_state();

  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  offset = AOMMIN(offset, delta_q_info->delta_q_res * 9 - 1);
  offset = AOMMAX(offset, -delta_q_info->delta_q_res * 9 + 1);
  int qindex = cm->quant_params.base_qindex + offset;

  qindex =
      AOMMIN(qindex, cm->seq_params.bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
                     : cm->seq_params.bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                               : MAXQ);
  qindex = AOMMAX(qindex, MINQ);

  return qindex;
}

void av1_reset_simple_motion_tree_partition(SIMPLE_MOTION_DATA_TREE *sms_tree,
                                            BLOCK_SIZE bsize) {
  sms_tree->partitioning = PARTITION_NONE;

  if (bsize >= BLOCK_8X8) {
    BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
    assert(subsize < BLOCK_SIZES_ALL);
    assert(is_square_block(subsize));
    for (int idx = 0; idx < 4; ++idx)
      av1_reset_simple_motion_tree_partition(sms_tree->split[idx], subsize);
  }
}

// Record the ref frames that have been selected by square partition blocks.
void av1_update_picked_ref_frames_mask(MACROBLOCK *const x, int ref_type,
                                       BLOCK_SIZE bsize, int mib_size,
                                       int mi_row, int mi_col) {
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  const int sb_size_mask = mib_size - 1;
  const int mi_row_in_sb = mi_row & sb_size_mask;
  const int mi_col_in_sb = mi_col & sb_size_mask;
  const int mi_size_h = mi_size_high[bsize];
  const int mi_size_w = mi_size_wide[bsize];
  for (int i = mi_row_in_sb; i < mi_row_in_sb + mi_size_h; ++i) {
    for (int j = mi_col_in_sb; j < mi_col_in_sb + mi_size_w; ++j) {
#if CONFIG_SAME_REF_COMPOUND
      x->picked_ref_frames_mask[i * mib_size + j] |= 1ULL << ref_type;
#else
      x->picked_ref_frames_mask[i * mib_size + j] |= 1 << ref_type;
#endif  // CONFIG_SAME_REF_COMPOUND
    }
  }
}

#if !CONFIG_ENHANCED_FRAME_CONTEXT_INIT
static void avg_cdf_symbol(aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr,
                           int num_cdfs, int cdf_stride, int nsymbs,
                           int wt_left, int wt_tr) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      cdf_ptr_left[i * cdf_stride + j] =
          (aom_cdf_prob)(((int)cdf_ptr_left[i * cdf_stride + j] * wt_left +
                          (int)cdf_ptr_tr[i * cdf_stride + j] * wt_tr +
                          ((wt_left + wt_tr) / 2)) /
                         (wt_left + wt_tr));
      assert(cdf_ptr_left[i * cdf_stride + j] >= 0 &&
             cdf_ptr_left[i * cdf_stride + j] < CDF_PROB_TOP);
    }
  }
}
#define AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))
#define AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)           \
  do {                                                                     \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;               \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                   \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);       \
    int num_cdfs = array_size / cdf_stride;                                \
    avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, nsymbs, \
                   wt_left, wt_tr);                                        \
  } while (0)
static void avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr, int wt_left,
                    int wt_tr) {
#if !CONFIG_VQ_MVD_CODING
  AVERAGE_CDF(nmv_left->joints_cdf, nmv_tr->joints_cdf, 4);
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
    AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                nmv_tr->joint_shell_class_cdf[prec], num_mv_class);
  }
  AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
              nmv_tr->shell_offset_low_class_cdf, 2);
  AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
              nmv_tr->shell_offset_class2_cdf, 2);
  for (int i = 0; i < NUM_CTX_CLASS_OFFSETS; i++) {
    AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf[i],
                nmv_tr->shell_offset_other_class_cdf[i], 2);
  }
  AVERAGE_CDF(nmv_left->col_mv_greter_flags_cdf,
              nmv_tr->col_mv_greter_flags_cdf, 2);
  AVERAGE_CDF(nmv_left->col_mv_index_cdf, nmv_tr->col_mv_index_cdf, 2);

#endif  // !CONFIG_VQ_MVD_CODING
  AVERAGE_CDF(nmv_left->amvd_joints_cdf, nmv_tr->amvd_joints_cdf, MV_JOINTS);
  for (int i = 0; i < 2; i++) {
#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].classes_cdf, nmv_tr->comps[i].classes_cdf,
                MV_CLASSES);
    AVERAGE_CDF(nmv_left->comps[i].amvd_classes_cdf,
                nmv_tr->comps[i].amvd_classes_cdf, MV_CLASSES);
#else
    AVERAGE_CDF(nmv_left->comps[i].amvd_indices_cdf,
                nmv_tr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].class0_fp_cdf,
                nmv_tr->comps[i].class0_fp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].fp_cdf, nmv_tr->comps[i].fp_cdf, 2);
#endif  //! CONFIG_VQ_MVD_CODING

    AVERAGE_CDF(nmv_left->comps[i].sign_cdf, nmv_tr->comps[i].sign_cdf, 2);

#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].class0_hp_cdf,
                nmv_tr->comps[i].class0_hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].hp_cdf, nmv_tr->comps[i].hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].class0_cdf, nmv_tr->comps[i].class0_cdf,
                CLASS0_SIZE);
    AVERAGE_CDF(nmv_left->comps[i].bits_cdf, nmv_tr->comps[i].bits_cdf, 2);
#endif  // !CONFIG_VQ_MVD_CODING
  }
}

// In case of row-based multi-threading of encoder, since we always
// keep a top - right sync, we can average the top - right SB's CDFs and
// the left SB's CDFs and use the same for current SB's encoding to
// improve the performance. This function facilitates the averaging
// of CDF and used only when row-mt is enabled in encoder.
void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr) {
  AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->v_dc_sign_cdf, ctx_tr->v_dc_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->v_ac_sign_cdf, ctx_tr->v_ac_sign_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
              EOB_MAX_SYMS - 6);
  AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
              EOB_MAX_SYMS - 5);
  AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
              EOB_MAX_SYMS - 4);
  AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
              EOB_MAX_SYMS - 2);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
              EOB_MAX_SYMS - 1);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
              EOB_MAX_SYMS);
  AVERAGE_CDF(ctx_left->coeff_base_eob_cdf, ctx_tr->coeff_base_eob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_bob_cdf, ctx_tr->coeff_base_bob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_lf_cdf, ctx_tr->coeff_base_lf_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_cdf, ctx_tr->coeff_base_lf_eob_cdf,
              LF_BASE_SYMBOLS - 1);
  AVERAGE_CDF(ctx_left->coeff_br_lf_cdf, ctx_tr->coeff_br_lf_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  AVERAGE_CDF(ctx_left->idtx_sign_cdf, ctx_tr->idtx_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->coeff_base_cdf_idtx, ctx_tr->coeff_base_cdf_idtx, 4);
  AVERAGE_CDF(ctx_left->coeff_br_cdf_idtx, ctx_tr->coeff_br_cdf_idtx,
              BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->inter_single_mode_cdf, ctx_tr->inter_single_mode_cdf,
              INTER_SINGLE_MODES);
#if CONFIG_CHROMA_CODING
  AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf, ctx_tr->coeff_base_eob_uv_cdf,
              3);
  AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf, ctx_tr->coeff_base_lf_uv_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
              ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
  AVERAGE_CDF(ctx_left->coeff_br_lf_uv_cdf, ctx_tr->coeff_br_lf_uv_cdf,
              BR_CDF_SIZE);
#endif  // CONFIG_CHROMA_CODING

  AVERAGE_CDF(ctx_left->inter_warp_mode_cdf, ctx_tr->inter_warp_mode_cdf, 2);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
              ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_REFINEMV
  AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
              REFINEMV_NUM_MODES);
#endif  // CONFIG_REFINEMV

  AVERAGE_CDF(ctx_left->drl_cdf[0], ctx_tr->drl_cdf[0], 2);
  AVERAGE_CDF(ctx_left->drl_cdf[1], ctx_tr->drl_cdf[1], 2);
  AVERAGE_CDF(ctx_left->drl_cdf[2], ctx_tr->drl_cdf[2], 2);
#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP

  AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

#if CONFIG_INTER_COMPOUND_BY_JOINT
  AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
              ctx_tr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
              ctx_tr->inter_compound_mode_non_joint_type_cdf,
              NUM_OPTIONS_NON_JOINT_TYPE);
  AVERAGE_CDF(ctx_left->inter_compound_mode_joint_type_cdf,
              ctx_tr->inter_compound_mode_joint_type_cdf,
              NUM_OPTIONS_JOINT_TYPE);
#else
  AVERAGE_CDF(ctx_left->inter_compound_mode_cdf,
              ctx_tr->inter_compound_mode_cdf, INTER_COMPOUND_REF_TYPES);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
              ctx_tr->inter_compound_mode_same_refs_cdf,
              INTER_COMPOUND_SAME_REFS_TYPES);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf, ctx_tr->jmvd_scale_mode_cdf,
              JOINT_NEWMV_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
              ctx_tr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
              MASKED_COMPOUND_TYPES);
#if CONFIG_WEDGE_MOD_EXT
  AVERAGE_CDF(ctx_left->wedge_angle_dir_cdf, ctx_tr->wedge_angle_dir_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_angle_0_cdf, ctx_tr->wedge_angle_0_cdf,
              H_WEDGE_ANGLES);
  AVERAGE_CDF(ctx_left->wedge_angle_1_cdf, ctx_tr->wedge_angle_1_cdf,
              H_WEDGE_ANGLES);
  AVERAGE_CDF(ctx_left->wedge_dist_cdf, ctx_tr->wedge_dist_cdf, NUM_WEDGE_DIST);
  AVERAGE_CDF(ctx_left->wedge_dist_cdf2, ctx_tr->wedge_dist_cdf2,
              NUM_WEDGE_DIST - 1);
#else
  AVERAGE_CDF(ctx_left->wedge_idx_cdf, ctx_tr->wedge_idx_cdf, 16);
#endif
  AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_interintra_cdf, ctx_tr->wedge_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_mode_cdf, ctx_tr->interintra_mode_cdf,
              INTERINTRA_MODES);
  AVERAGE_CDF(ctx_left->obmc_cdf, ctx_tr->obmc_cdf, 2);
  AVERAGE_CDF(ctx_left->warped_causal_cdf, ctx_tr->warped_causal_cdf, 2);
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->warp_delta_cdf, ctx_tr->warp_delta_cdf, 2);
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->warp_delta_param_cdf, ctx_tr->warp_delta_param_cdf,
              WARP_DELTA_NUMSYMBOLS_LOW);
#if CONFIG_WARP_PRECISION
  AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
              ctx_tr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  AVERAGE_CDF(ctx_left->warp_param_sign_cdf, ctx_tr->warp_param_sign_cdf, 2);
#endif  // CONFIG_WARP_PRECISION

  AVERAGE_CDF(ctx_left->warped_causal_warpmv_cdf,
              ctx_tr->warped_causal_warpmv_cdf, 2);
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf[0], ctx_tr->warp_ref_idx_cdf[0], 2);
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf[1], ctx_tr->warp_ref_idx_cdf[1], 2);
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf[2], ctx_tr->warp_ref_idx_cdf[2], 2);
  AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
              ctx_tr->warpmv_with_mvd_flag_cdf, 2);

#if CONFIG_WARP_PRECISION
  AVERAGE_CDF(ctx_left->warp_precision_idx_cdf, ctx_tr->warp_precision_idx_cdf,
              NUM_WARP_PRECISION_MODES);
#endif  // CONFIG_WARP_PRECISION

  AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  AVERAGE_CDF(ctx_left->bawp_cdf[0], ctx_tr->bawp_cdf[0], 2);
  AVERAGE_CDF(ctx_left->bawp_cdf[1], ctx_tr->bawp_cdf[1], 2);
#else
  AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
#endif  // CONFIG_BAWP_CHROMA
#if CONFIG_EXPLICIT_BAWP
  AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
              ctx_tr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
#endif  // CONFIG_EXPLICIT_BAWP
#endif
  AVERAGE_CDF(ctx_left->palette_y_size_cdf, ctx_tr->palette_y_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->palette_uv_size_cdf, ctx_tr->palette_uv_size_cdf,
              PALETTE_SIZES);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                   ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
    AVG_CDF_STRIDE(ctx_left->palette_uv_color_index_cdf[j],
                   ctx_tr->palette_uv_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
  }
  AVERAGE_CDF(ctx_left->palette_y_mode_cdf, ctx_tr->palette_y_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_uv_mode_cdf, ctx_tr->palette_uv_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  AVERAGE_CDF(ctx_left->txfm_do_partition_cdf, ctx_tr->txfm_do_partition_cdf,
              2);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
              ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
              ctx_tr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
#else
  // Square blocks
  AVERAGE_CDF(ctx_left->inter_4way_txfm_partition_cdf[0],
              ctx_tr->inter_4way_txfm_partition_cdf[0], 4);
  // Rectangular blocks
  AVERAGE_CDF(ctx_left->inter_4way_txfm_partition_cdf[1],
              ctx_tr->inter_4way_txfm_partition_cdf[1], 4);
  AVERAGE_CDF(ctx_left->inter_2way_txfm_partition_cdf,
              ctx_tr->inter_2way_txfm_partition_cdf, 2);
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->txfm_partition_cdf, ctx_tr->txfm_partition_cdf, 2);
#endif  // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->comp_group_idx_cdf, ctx_tr->comp_group_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  AVERAGE_CDF(ctx_left->intra_inter_cdf[0], ctx_tr->intra_inter_cdf[0], 2);
  AVERAGE_CDF(ctx_left->intra_inter_cdf[1], ctx_tr->intra_inter_cdf[1], 2);
#else
  AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, wt_left, wt_tr);
  avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, wt_left, wt_tr);
  AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
#if CONFIG_IBC_BV_IMPROVEMENT
  AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->intrabc_drl_idx_cdf, ctx_tr->intrabc_drl_idx_cdf, 2);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
              ctx_tr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#if CONFIG_MORPH_PRED
  AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
#endif  // CONFIG_MORPH_PRED
  AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->filter_intra_cdfs, ctx_tr->filter_intra_cdfs, 2);
  AVERAGE_CDF(ctx_left->filter_intra_mode_cdf, ctx_tr->filter_intra_mode_cdf,
              FILTER_INTRA_MODES);
  AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
              ctx_tr->switchable_flex_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wiener_restore_cdf, ctx_tr->wiener_restore_cdf, 2);
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
#if CONFIG_CCSO_IMPROVE
    for (int ctx = 0; ctx < CCSO_CONTEXT; ctx++) {
      AVERAGE_CDF(ctx_left->ccso_cdf[plane][ctx], ctx_tr->ccso_cdf[plane][ctx],
                  2);
    }
#else
    AVERAGE_CDF(ctx_left->ccso_cdf[plane], ctx_tr->ccso_cdf[plane], 2);
#endif  // CONFIG_CCSO_IMPROVE
  }
#if CONFIG_CDEF_ENHANCEMENTS
  AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
              ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                   CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
#endif  // CONFIG_CDEF_ENHANCEMENTS
  AVERAGE_CDF(ctx_left->sgrproj_restore_cdf, ctx_tr->sgrproj_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_restore_cdf, ctx_tr->wienerns_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_length_cdf, ctx_tr->wienerns_length_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf, ctx_tr->wienerns_uv_sym_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_4part_cdf, ctx_tr->wienerns_4part_cdf, 4);
  AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf, ctx_tr->pc_wiener_restore_cdf,
              2);
  AVERAGE_CDF(ctx_left->merged_param_cdf, ctx_tr->merged_param_cdf, 2);
  AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf, FSC_MODES);
  AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf, MRL_LINE_NUMBER);

#if CONFIG_LOSSLESS_DPCM
  AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf, ctx_tr->dpcm_vert_horz_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf, ctx_tr->dpcm_uv_vert_horz_cdf,
              2);
#endif  // CONFIG_LOSSLESS_DPCM

#if CONFIG_ENABLE_MHCCP
  AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf, MHCCP_MODE_NUM);
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
              CFL_TYPE_COUNT - 1);
#else
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf, CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_AIMC
  AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
              INTRA_MODE_SETS);
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_0, ctx_tr->y_mode_idx_cdf_0,
              FIRST_MODE_COUNT);
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_1, ctx_tr->y_mode_idx_cdf_1,
              SECOND_MODE_COUNT);
#else
  AVERAGE_CDF(ctx_left->y_mode_cdf, ctx_tr->y_mode_cdf, INTRA_MODES);
#endif  // CONFIG_AIMC
#if CONFIG_AIMC
  AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf, UV_INTRA_MODES);
#else
  AVG_CDF_STRIDE(ctx_left->uv_mode_cdf[0], ctx_tr->uv_mode_cdf[0],
                 UV_INTRA_MODES - 1, CDF_SIZE(UV_INTRA_MODES));
  AVERAGE_CDF(ctx_left->uv_mode_cdf[1], ctx_tr->uv_mode_cdf[1], UV_INTRA_MODES);
#endif  // CONFIG_AIMC

#if CONFIG_EXTENDED_SDP
  for (int i = 0; i < INTER_SDP_BSIZE_GROUP; i++) {
    AVERAGE_CDF(ctx_left->region_type_cdf[i], ctx_tr->region_type_cdf[i],
                REGION_TYPES);
  }
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      AVERAGE_CDF(ctx_left->do_split_cdf[plane_index][i],
                  ctx_tr->do_split_cdf[plane_index][i], 2);
    }
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < SQUARE_SPLIT_CONTEXTS; i++) {
      AVERAGE_CDF(ctx_left->do_square_split_cdf[plane_index][i],
                  ctx_tr->do_square_split_cdf[plane_index][i], 2);
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      AVERAGE_CDF(ctx_left->rect_type_cdf[plane_index][i],
                  ctx_tr->rect_type_cdf[plane_index][i], 2);
    }
  }
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      for (RECT_PART_TYPE rect = 0; rect < NUM_RECT_PARTS; rect++) {
        AVERAGE_CDF(ctx_left->do_ext_partition_cdf[plane_index][rect][i],
                    ctx_tr->do_ext_partition_cdf[plane_index][rect][i], 2);
        AVERAGE_CDF(
            ctx_left->do_uneven_4way_partition_cdf[plane_index][rect][i],
            ctx_tr->do_uneven_4way_partition_cdf[plane_index][rect][i], 2);
        AVERAGE_CDF(
            ctx_left->uneven_4way_partition_type_cdf[plane_index][rect][i],
            ctx_tr->uneven_4way_partition_type_cdf[plane_index][rect][i],
            NUM_UNEVEN_4WAY_PARTS);
      }
    }
  }
#else
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      if (i < 4) {
        AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                       ctx_tr->partition_cdf[plane_index][i], 4, CDF_SIZE(10));
      } else if (i < 16) {
        AVERAGE_CDF(ctx_left->partition_cdf[plane_index][i],
                    ctx_tr->partition_cdf[plane_index][i], 10);
      } else {
        AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                       ctx_tr->partition_cdf[plane_index][i], 8, CDF_SIZE(10));
      }
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  AVERAGE_CDF(ctx_left->switchable_interp_cdf, ctx_tr->switchable_interp_cdf,
              SWITCHABLE_FILTERS);
#if !CONFIG_AIMC
  AVERAGE_CDF(ctx_left->kf_y_cdf, ctx_tr->kf_y_cdf, INTRA_MODES);
  AVERAGE_CDF(ctx_left->angle_delta_cdf, ctx_tr->angle_delta_cdf,
              2 * MAX_ANGLE_DELTA + 1);
#endif  // !CONFIG_AIMC

#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  // Square blocks
  AVERAGE_CDF(ctx_left->intra_4way_txfm_partition_cdf[0],
              ctx_tr->intra_4way_txfm_partition_cdf[0], 4);
  // Rectangular blocks
  AVERAGE_CDF(ctx_left->intra_4way_txfm_partition_cdf[1],
              ctx_tr->intra_4way_txfm_partition_cdf[1], 4);
  AVERAGE_CDF(ctx_left->intra_2way_txfm_partition_cdf,
              ctx_tr->intra_2way_txfm_partition_cdf, 2);
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  AVG_CDF_STRIDE(ctx_left->tx_size_cdf[0], ctx_tr->tx_size_cdf[0], MAX_TX_DEPTH,
                 CDF_SIZE(MAX_TX_DEPTH + 1));
  AVERAGE_CDF(ctx_left->tx_size_cdf[1], ctx_tr->tx_size_cdf[1],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[2], ctx_tr->tx_size_cdf[2],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[3], ctx_tr->tx_size_cdf[3],
              MAX_TX_DEPTH + 1);
#endif  // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf, DELTA_Q_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    AVERAGE_CDF(ctx_left->delta_lf_multi_cdf[i], ctx_tr->delta_lf_multi_cdf[i],
                DELTA_LF_PROBS + 1);
  }
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
              ctx_tr->inter_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
              ctx_tr->intra_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1], ctx_tr->intra_ext_tx_cdf[1],
                 INTRA_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2], ctx_tr->intra_ext_tx_cdf[2],
                 INTRA_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1], ctx_tr->inter_ext_tx_cdf[1],
                 INTER_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2], ctx_tr->inter_ext_tx_cdf[2],
                 INTER_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3], ctx_tr->inter_ext_tx_cdf[3],
                 INTER_TX_SET3, CDF_SIZE(TX_TYPES));
  AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
              CFL_ALPHABET_SIZE);
  AVG_CDF_STRIDE(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES,
                 CDF_SIZE(STX_TYPES));
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
              ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
#else
  AVERAGE_CDF(ctx_left->stx_set_cdf, ctx_tr->stx_set_cdf, IST_DIR_SIZE);
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG

  for (int p = 0; p < NUM_MV_PREC_MPP_CONTEXT; ++p) {
    AVG_CDF_STRIDE(ctx_left->pb_mv_mpp_flag_cdf[p],
                   ctx_tr->pb_mv_mpp_flag_cdf[p], 2, CDF_SIZE(2));
  }
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      AVG_CDF_STRIDE(
          ctx_left->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          ctx_tr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  AVERAGE_CDF(ctx_left->coeff_base_ph_cdf, ctx_tr->coeff_base_ph_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_ph_cdf, ctx_tr->coeff_br_ph_cdf, 4);
  AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf, CCTX_TYPES);
}
#endif  // !CONFIG_ENHANCED_FRAME_CONTEXT_INIT

// Memset the mbmis at the current superblock to 0
void av1_reset_mbmi(const CommonModeInfoParams *const mi_params,
                    BLOCK_SIZE sb_size, int mi_row, int mi_col) {
  // size of sb in unit of mi (BLOCK_4X4)
  const int sb_size_mi = mi_size_wide[sb_size];
  const int mi_alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  // size of sb in unit of allocated mi size
  const int sb_size_alloc_mi = mi_size_wide[sb_size] / mi_alloc_size_1d;
  assert(mi_params->mi_alloc_stride % sb_size_alloc_mi == 0 &&
         "mi is not allocated as a multiple of sb!");
  assert(mi_params->mi_stride % sb_size_mi == 0 &&
         "mi_grid_base is not allocated as a multiple of sb!");

  const int mi_rows = mi_size_high[sb_size];
  for (int cur_mi_row = 0; cur_mi_row < mi_rows; cur_mi_row++) {
    assert(get_mi_grid_idx(mi_params, 0, mi_col + mi_alloc_size_1d) <
           mi_params->mi_stride);
    const int mi_grid_idx =
        get_mi_grid_idx(mi_params, mi_row + cur_mi_row, mi_col);
    const int alloc_mi_idx =
        get_alloc_mi_idx(mi_params, mi_row + cur_mi_row, mi_col);
    memset(&mi_params->mi_grid_base[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->mi_grid_base));
    memset(&mi_params->tx_type_map[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->tx_type_map));
#if CONFIG_C071_SUBBLK_WARPMV
    memset(&mi_params->submi_grid_base[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->submi_grid_base));
#endif  // CONFIG_C071_SUBBLK_WARPMV
    memset(&mi_params->cctx_type_map[mi_grid_idx], 0,
           sb_size_mi * sizeof(*mi_params->cctx_type_map));
    if (cur_mi_row % mi_alloc_size_1d == 0) {
      memset(&mi_params->mi_alloc[alloc_mi_idx], 0,
             sb_size_alloc_mi * sizeof(*mi_params->mi_alloc));
#if CONFIG_C071_SUBBLK_WARPMV
      memset(&mi_params->mi_alloc_sub[alloc_mi_idx], 0,
             sb_size_alloc_mi * sizeof(*mi_params->mi_alloc_sub));
#endif  // CONFIG_C071_SUBBLK_WARPMV
    }
  }
}

void av1_backup_sb_state(SB_FIRST_PASS_STATS *sb_fp_stats, const AV1_COMP *cpi,
                         ThreadData *td, const TileDataEnc *tile_data,
                         int mi_row, int mi_col) {
  MACROBLOCK *x = &td->mb;
#if !CONFIG_TX_PARTITION_CTX
  MACROBLOCKD *xd = &x->e_mbd;
  const TileInfo *tile_info = &tile_data->tile_info;
#endif  // !CONFIG_TX_PARTITION_CTX

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const BLOCK_SIZE sb_size = cm->sb_size;

#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context =
      cm->above_contexts.txfm[tile_info->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX
  av1_save_context(x, &sb_fp_stats->x_ctx, mi_row, mi_col, sb_size, num_planes);

  sb_fp_stats->rd_count = td->rd_counts;
  sb_fp_stats->split_count = x->txfm_search_info.txb_split_count;

  sb_fp_stats->fc = *td->counts;

  memcpy(sb_fp_stats->inter_mode_rd_models, tile_data->inter_mode_rd_models,
         sizeof(sb_fp_stats->inter_mode_rd_models));

  memcpy(sb_fp_stats->thresh_freq_fact, x->thresh_freq_fact,
         sizeof(sb_fp_stats->thresh_freq_fact));

  const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
  sb_fp_stats->current_qindex =
      cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex;
#if CONFIG_MVP_IMPROVEMENT
  sb_fp_stats->ref_mv_bank = td->mb.e_mbd.ref_mv_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  sb_fp_stats->warp_param_bank = td->mb.e_mbd.warp_param_bank;
#endif  // WARP_CU_BANK
#if CONFIG_EXT_RECUR_PARTITIONS
  sb_fp_stats->min_partition_size = x->sb_enc.min_partition_size;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

void av1_restore_sb_state(const SB_FIRST_PASS_STATS *sb_fp_stats, AV1_COMP *cpi,
                          ThreadData *td, TileDataEnc *tile_data, int mi_row,
                          int mi_col) {
  MACROBLOCK *x = &td->mb;

  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const BLOCK_SIZE sb_size = cm->sb_size;

  av1_restore_context(cm, x, &sb_fp_stats->x_ctx, mi_row, mi_col, sb_size,
                      num_planes);

  td->rd_counts = sb_fp_stats->rd_count;
  x->txfm_search_info.txb_split_count = sb_fp_stats->split_count;

  *td->counts = sb_fp_stats->fc;

  memcpy(tile_data->inter_mode_rd_models, sb_fp_stats->inter_mode_rd_models,
         sizeof(sb_fp_stats->inter_mode_rd_models));
  memcpy(x->thresh_freq_fact, sb_fp_stats->thresh_freq_fact,
         sizeof(sb_fp_stats->thresh_freq_fact));

  const int alloc_mi_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
  cm->mi_params.mi_alloc[alloc_mi_idx].current_qindex =
      sb_fp_stats->current_qindex;
#if CONFIG_MVP_IMPROVEMENT
  x->e_mbd.ref_mv_bank = sb_fp_stats->ref_mv_bank;
#endif  // CONFIG_MVP_IMPROVEMENT
#if WARP_CU_BANK
  x->e_mbd.warp_param_bank = sb_fp_stats->warp_param_bank;
#endif  // WARP_CU_BANK
#if CONFIG_EXT_RECUR_PARTITIONS
  x->sb_enc.min_partition_size = sb_fp_stats->min_partition_size;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

// Update the rate costs of some symbols according to the frequency directed
// by speed features
void av1_set_cost_upd_freq(AV1_COMP *cpi, ThreadData *td,
                           const TileInfo *const tile_info, const int mi_row,
                           const int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  switch (cpi->oxcf.cost_upd_freq.coeff) {
    case COST_UPD_TILE:  // Tile level
      if (mi_row != tile_info->mi_row_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SBROW:  // SB row level in tile
      if (mi_col != tile_info->mi_col_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SB:  // SB level
      if (cpi->sf.inter_sf.disable_sb_level_coeff_cost_upd &&
          mi_col != tile_info->mi_col_start)
        break;
      av1_fill_coeff_costs(&x->coeff_costs, xd->tile_ctx, num_planes);
      break;
    default: assert(0);
  }

  switch (cpi->oxcf.cost_upd_freq.mode) {
    case COST_UPD_TILE:  // Tile level
      if (mi_row != tile_info->mi_row_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SBROW:  // SB row level in tile
      if (mi_col != tile_info->mi_col_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SB:  // SB level
      av1_fill_mode_rates(cm, &x->mode_costs, xd->tile_ctx);
      break;
    default: assert(0);
  }
  switch (cpi->oxcf.cost_upd_freq.mv) {
    case COST_UPD_OFF: break;
    case COST_UPD_TILE:  // Tile level
      if (mi_row != tile_info->mi_row_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SBROW:  // SB row level in tile
      if (mi_col != tile_info->mi_col_start) break;
      AOM_FALLTHROUGH_INTENDED;
    case COST_UPD_SB:  // SB level
      if (cpi->sf.inter_sf.disable_sb_level_mv_cost_upd &&
          mi_col != tile_info->mi_col_start)
        break;
      av1_fill_mv_costs(xd->tile_ctx, cm->features.cur_frame_force_integer_mv,
                        cm->features.fr_mv_precision, &x->mv_costs);
#if CONFIG_IBC_BV_IMPROVEMENT
      if (cm->features.allow_intrabc) {
        fill_dv_costs(&x->dv_costs, xd->tile_ctx, &x->mv_costs);
      }
#endif  // CONFIG_IBC_BV_IMPROVEMENT
      break;
    default: assert(0);
  }
}
