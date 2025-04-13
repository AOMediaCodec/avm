/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av1/common/bru.h"
#include "av1/common/common_data.h"
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"
#include "av1/common/reconinter.h"

static AOM_INLINE uint16_t *highbd_build_mc_border(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int x,
    int y, int b_w, int b_h, int w, int h) {
  // Get a pointer to the start of the real data for this row.
  const uint16_t *ref_row = src - x - y * src_stride;
  if (y >= h)
    ref_row += (h - 1) * src_stride;
  else if (y > 0)
    ref_row += y * src_stride;

  do {
    int right = 0, copy;
    int left = x < 0 ? -x : 0;
    int dont_cp = 0;
    if (left > b_w) left = b_w;

    if (x + b_w > w) right = x + b_w - w;

    if (right > b_w) right = b_w;

    copy = b_w - left - right;

    if (dst + left == ref_row + x + left) dont_cp = 1;
    if (left) aom_memset16(dst, ref_row[0], left);

    if (copy && !dont_cp)
      memcpy(dst + left, ref_row + x + left, copy * sizeof(uint16_t));

    if (right) aom_memset16(dst + left + copy, ref_row[w - 1], right);

    dst += dst_stride;
    ++y;

    if (y > 0 && y < h) ref_row += src_stride;
  } while (--b_h);
  return dst;
}

void bru_extend_mc_border(const AV1_COMMON *const cm, int mi_row, int mi_col,
                          BLOCK_SIZE bsize, YV12_BUFFER_CONFIG *src) {
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int W = bw << MI_SIZE_LOG2;
  const int H = bh << MI_SIZE_LOG2;
  const int X = mi_col << MI_SIZE_LOG2;
  const int Y = mi_row << MI_SIZE_LOG2;
  const int ss_x = src->uv_width < src->y_width;
  const int ss_y = src->uv_height < src->y_height;
  uint16_t *src_data;
  uint16_t *dst_data;
  for (int plane = 0; plane < av1_num_planes(cm); plane++) {
    const int is_uv = plane > 0;
    const int s_x = is_uv ? ss_x : 0;
    const int s_y = is_uv ? ss_y : 0;
    int x = X >> s_x;
    int y = Y >> s_y;
    int w = W >> s_x;
    int h = H >> s_y;
    const int border = src->border >> is_uv;
    // const int frame_H = src->heights[is_uv];
    const int frame_H = is_uv ? src->uv_crop_height : src->y_crop_height;
    // const int frame_W = src->widths[is_uv];
    const int frame_W = is_uv ? src->uv_crop_width : src->y_crop_width;
    const int extend_left = x == 0;
    const int extend_right = (x + w) >= frame_W;
    const int extend_top = y == 0;
    const int extend_bottom = (y + h) >= frame_H;
    if (extend_left || extend_top || extend_right || extend_bottom) {
      int x_end = (x + w) >= frame_W ? frame_W : x + w;
      int y_end = (y + h) >= frame_H ? frame_H : y + h;
      if (extend_left) {
        x -= (w > border ? border : w);
      }
      if (extend_top) {
        y -= (h > border ? border : h);
      }
      if (extend_right) {
        x_end += border;
      }
      if (extend_bottom) {
        y_end += border;
      }
      int b_w = x_end - x;
      int b_h = y_end - y;
      int stride = src->strides[is_uv];
      // Get reference block pointer.
      src_data = src->buffers[plane] + scaled_buffer_offset(x, y, stride, NULL);
      dst_data = src->buffers[plane] + scaled_buffer_offset(x, y, stride, NULL);
      highbd_build_mc_border(src_data, stride, dst_data, stride, x, y, b_w, b_h,
                             frame_W, frame_H);
    }
  }
}

void bru_update_txk_skip_array(const AV1_COMMON *cm, int mi_row, int mi_col,
                               TREE_TYPE tree_type,
                               const CHROMA_REF_INFO *chroma_ref_info,
                               int plane, int blk_w, int blk_h) {
  (void)tree_type;
  (void)chroma_ref_info;
  if (mi_col + blk_w > cm->mi_params.mi_cols)
    blk_w = cm->mi_params.mi_cols - mi_col;

  if (mi_row + blk_h > cm->mi_params.mi_rows)
    blk_h = cm->mi_params.mi_rows - mi_row;

  blk_w = blk_w >> ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
  blk_h = blk_h >> ((plane == 0) ? 0 : cm->seq_params.subsampling_y);

  int w = ((cm->width + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2)
          << MAX_SB_SIZE_LOG2;
  w >>= ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
  const uint32_t stride = (w + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
  const int cols = (blk_w << MI_SIZE_LOG2) >> MIN_TX_SIZE_LOG2;
  const int rows = (blk_h << MI_SIZE_LOG2) >> MIN_TX_SIZE_LOG2;
  int x = (mi_col << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
  int y = (mi_row << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
  x = x >> MIN_TX_SIZE_LOG2;
  y = y >> MIN_TX_SIZE_LOG2;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const uint32_t idx = (y + r) * stride + x + c;
      assert(idx < cm->mi_params.tx_skip_buf_size[plane]);
      assert(stride == cm->mi_params.tx_skip_stride[plane]);
      cm->mi_params.tx_skip[plane][idx] = 1;
    }
  }
}

BruActiveMode set_sb_mbmi_bru_mode(const AV1_COMMON *cm, MACROBLOCKD *const xd,
                                   const int mi_col, const int mi_row,
                                   const BLOCK_SIZE bsize,
                                   const BruActiveMode bru_sb_mode) {
  // only set very first mi
  // for inactive SB, other location on the mi_grid is invalid
  // for active SB, later set_offset will redo the address assignment
  if (cm->bru.enabled) {
    xd->mi_col = mi_col;
    xd->mi_row = mi_row;
    const int mi_grid_idx = get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
    const int mi_alloc_idx = get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
    cm->mi_params.mi_grid_base[mi_grid_idx] =
        &cm->mi_params.mi_alloc[mi_alloc_idx];
    xd->mi = cm->mi_params.mi_grid_base + mi_grid_idx;
    xd->mi[0]->sb_active_mode = bru_sb_mode;
    // if not active, propagate to all the mi in bsize
    // this function will also be used in decoder
    if (bru_sb_mode != BRU_ACTIVE_SB) {
      const int mi_h = mi_size_high[bsize];
      const int mi_w = mi_size_wide[bsize];
      MB_MODE_INFO *const mi_addr = xd->mi[0];
      const int x_inside_boundary =
          AOMMIN(mi_w, cm->mi_params.mi_cols - mi_col);
      const int y_inside_boundary =
          AOMMIN(mi_h, cm->mi_params.mi_rows - mi_row);
      const int mis = cm->mi_params.mi_stride;
      for (int y = 0; y < y_inside_boundary; y++) {
        for (int x_idx = 0; x_idx < x_inside_boundary; x_idx++) {
          xd->mi[x_idx + y * mis] = mi_addr;
        }
      }
    }
    return bru_sb_mode;
  }
  return BRU_ACTIVE_SB;
}

void bru_copy_sb(const struct AV1Common *cm, const int mi_col,
                 const int mi_row) {
  if (cm->bru.update_ref_idx < 0)
    return;  // now ref_idx is the sole indicator of frame level bru
  const int sb_size = cm->seq_params.sb_size;
  const int w = mi_size_wide[sb_size];
  const int h = mi_size_high[sb_size];
  RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, cm->bru.update_ref_idx);
  YV12_BUFFER_CONFIG *const ref_src = &ref_buf->buf;
  YV12_BUFFER_CONFIG *const rec_dst = &cm->cur_frame->buf;
  const int x_inside_boundary = AOMMIN(w, cm->mi_params.mi_cols - mi_col)
                                << MI_SIZE_LOG2;
  const int y_inside_boundary = AOMMIN(h, cm->mi_params.mi_rows - mi_row)
                                << MI_SIZE_LOG2;
  const int x = mi_col << MI_SIZE_LOG2;
  const int y = mi_row << MI_SIZE_LOG2;
  for (int i_plane = 0; i_plane < av1_num_planes(cm); ++i_plane) {
    uint16_t *rec_data = rec_dst->buffers[i_plane];
    uint16_t *ref_data = ref_src->buffers[i_plane];
    int rec_stride = i_plane > 0 ? rec_dst->uv_stride : rec_dst->y_stride;
    int ref_stride = i_plane > 0 ? ref_src->uv_stride : ref_src->y_stride;
    int subsample_x = i_plane > 0 ? ref_src->subsampling_x : 0;
    int subsample_y = i_plane > 0 ? ref_src->subsampling_y : 0;
    uint64_t rec_offset = scaled_buffer_offset(
        x >> subsample_x, y >> subsample_y, rec_stride, NULL);
    uint64_t ref_offset = scaled_buffer_offset(
        x >> subsample_x, y >> subsample_y, ref_stride, NULL);
    copy_tile(x_inside_boundary >> subsample_x,
              y_inside_boundary >> subsample_y, ref_data + ref_offset,
              ref_stride, rec_data + rec_offset, rec_stride);
  }
  if (cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
    // set cur_frame mvs to 0
    bru_zero_sb_mvs(cm, -1, mi_row, mi_col, x_inside_boundary >> MI_SIZE_LOG2,
                    y_inside_boundary >> MI_SIZE_LOG2);
  }
  // check if still necessary
  bru_extend_mc_border(cm, mi_row, mi_col, sb_size, rec_dst);
  return;
}

void bru_update_sb(const struct AV1Common *cm, const int mi_col,
                   const int mi_row) {
  if (cm->bru.update_ref_idx < 0)
    return;  // now ref_idx is the sole indicator of frame level bru
  RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, cm->bru.update_ref_idx);
  // just swap these two
  YV12_BUFFER_CONFIG *const rec_dst = &ref_buf->buf;
  YV12_BUFFER_CONFIG *const ref_src = &cm->cur_frame->buf;
  const int sb_size = cm->seq_params.sb_size;
  const int w = mi_size_wide[sb_size];
  const int h = mi_size_high[sb_size];
  const int x_inside_boundary = AOMMIN(w, cm->mi_params.mi_cols - mi_col)
                                << MI_SIZE_LOG2;
  const int y_inside_boundary = AOMMIN(h, cm->mi_params.mi_rows - mi_row)
                                << MI_SIZE_LOG2;
  const int x = mi_col << MI_SIZE_LOG2;
  const int y = mi_row << MI_SIZE_LOG2;

  for (int i_plane = 0; i_plane < av1_num_planes(cm); ++i_plane) {
    uint16_t *rec_data = rec_dst->buffers[i_plane];
    uint16_t *ref_data = ref_src->buffers[i_plane];
    const int rec_stride = i_plane > 0 ? rec_dst->uv_stride : rec_dst->y_stride;
    const int ref_stride = i_plane > 0 ? ref_src->uv_stride : ref_src->y_stride;
    const int subsample_x = i_plane > 0 ? ref_src->subsampling_x : 0;
    const int subsample_y = i_plane > 0 ? ref_src->subsampling_y : 0;
    const uint64_t rec_offset = scaled_buffer_offset(
        x >> subsample_x, y >> subsample_y, rec_stride, NULL);
    const uint64_t ref_offset = scaled_buffer_offset(
        x >> subsample_x, y >> subsample_y, ref_stride, NULL);
    copy_tile(x_inside_boundary >> subsample_x,
              y_inside_boundary >> subsample_y, ref_data + ref_offset,
              ref_stride, rec_data + rec_offset, rec_stride);
  }

  if (cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
    // copy mvs from cur frame to ref frame
    // It is ok to copy since all TMVP are collocated now
    bru_copy_sb_mvs(cm, -1, cm->bru.update_ref_idx, mi_row, mi_col,
                    x_inside_boundary >> MI_SIZE_LOG2,
                    y_inside_boundary >> MI_SIZE_LOG2);
  }
}

void bru_set_default_inter_mb_mode_info(const AV1_COMMON *const cm,
                                        MACROBLOCKD *const xd,
                                        MB_MODE_INFO *const mbmi,
                                        BLOCK_SIZE bsize) {
  // think reuse init_mbmi() here
  mbmi->segment_id = 0;
  mbmi->skip_mode = 0;
#if CONFIG_EXTENDED_SDP
  xd->tree_type = SHARED_PART;
#endif
  mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
  mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
#if CONFIG_NEW_CONTEXT_MODELING
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
#endif
#if CONFIG_BAWP
  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
#endif
  mbmi->cwp_idx = CWP_EQUAL;
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_C076_INTER_MOD_CTX
#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV
#endif  // CONFIG_C076_INTER_MOD_CTX
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  mbmi->comp_group_idx = 0;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->mv[0].as_int = 0;
  mbmi->mv[1].as_int = 0;
  assert(cm->bru.update_ref_idx >= 0);
  // todo find del idx
  mbmi->ref_frame[0] = cm->bru.update_ref_idx;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->skip_mode = 0;
  mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;
  mbmi->mode = NEWMV;
  mbmi->region_type = MIXED_INTER_INTRA_REGION;
  mbmi->sb_type[0] = bsize;
  mbmi->sb_type[1] = bsize;
  mbmi->chroma_ref_info.bsize_base = bsize;
  mbmi->chroma_ref_info.bsize = bsize;
  xd->mi[0]->mi_col_start = xd->mi_col;
  xd->mi[0]->mi_row_start = xd->mi_row;
  xd->mi[0]->chroma_ref_info.mi_col_chroma_base = xd->mi_col;
  xd->mi[0]->chroma_ref_info.mi_row_chroma_base = xd->mi_row;
  xd->ccso_blk_y = 0;
  xd->ccso_blk_u = 0;
  xd->ccso_blk_v = 0;
  mbmi->ccso_blk_y = 0;
  mbmi->ccso_blk_u = 0;
  mbmi->ccso_blk_v = 0;
  mbmi->cdef_strength = -1;
  mbmi->local_rest_type = 0;
  mbmi->local_ccso_blk_flag = 0;
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  /// bru use only pixel precision
  set_mv_precision(mbmi, MV_PRECISION_ONE_PEL);
  // set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, cm->seq_params.sb_size);
  set_most_probable_mv_precision(cm, mbmi, cm->seq_params.sb_size);
  mbmi->interp_fltr = MULTITAP_SHARP;
#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context =
      cm->above_contexts.txfm[xd->tile.tile_row] + xd->mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (xd->mi_row & MAX_MIB_MASK);
#endif
  if (is_bru_not_active_and_not_on_partial_border(cm, xd->mi_col, xd->mi_row,
                                                  bsize)) {
    mbmi->tx_size = TX_64X64;
  } else {
    mbmi->tx_size = tx_size_from_tx_mode(bsize, cm->features.tx_mode);
  }
  for (int plane = 0; plane < 1; plane++) {
    bru_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                              &mbmi->chroma_ref_info, plane, MAX_MIB_SIZE,
                              MAX_MIB_SIZE);
  }
}