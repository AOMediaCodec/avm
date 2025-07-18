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
#include "av1/common/ccso.h"

/* clean up tx_skip array for support and inactive SBs */
static void bru_update_txk_skip_array(const AV1_COMMON *cm, int mi_row,
                                      int mi_col, TREE_TYPE tree_type,
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

  const uint32_t stride = cm->mi_params.tx_skip_stride[plane];
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
      cm->mi_params.tx_skip[plane][idx] = 1;
    }
  }
}

/* copy segment id for support and inactive SBs*/
static void bru_copy_segment_id(const CommonModeInfoParams *const mi_params,
                                const uint8_t *last_segment_ids,
                                uint8_t *current_segment_ids, int mi_offset,
                                int x_inside_boundary, int y_inside_boundary) {
  for (int y = 0; y < y_inside_boundary; y++)
    for (int x = 0; x < x_inside_boundary; x++)
      current_segment_ids[mi_offset + y * mi_params->mi_cols + x] =
          last_segment_ids
              ? last_segment_ids[mi_offset + y * mi_params->mi_cols + x]
              : 0;
}

/* set segment id for support and inactive SBs*/
static void bru_set_segment_id(const AV1_COMMON *cm, int mi_offset,
                               int x_inside_boundary, int y_inside_boundary,
                               int segment_id) {
  assert(segment_id >= 0 && segment_id < MAX_SEGMENTS);

  for (int y = 0; y < y_inside_boundary; y++)
    for (int x = 0; x < x_inside_boundary; x++)
      cm->cur_frame->seg_map[mi_offset + y * cm->mi_params.mi_cols + x] =
          segment_id;
}

/* Set correct mbmi address for inactive and support SB since there is no chance
 * to set them in later stage  */
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

/* Copy recon data from BRU ref to current frame buffer. This is only used for
 * BRU_SUPPORT_SB of BRU optimized decoder/encoder */
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
  return;
}

/* Update recon data from current frame buffer to BRU ref. This is only used for
 * BRU_ACTIVE_SB of BRU optimized decoder/encoder */
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

/* Set default inter mode for Support and Inactive SBs */
void bru_set_default_inter_mb_mode_info(const AV1_COMMON *const cm,
                                        MACROBLOCKD *const xd,
                                        MB_MODE_INFO *const mbmi,
                                        BLOCK_SIZE bsize) {
  // think reuse init_mbmi() here
  mbmi->segment_id = 0;
  mbmi->skip_mode = 0;
  xd->tree_type = SHARED_PART;
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
  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
  mbmi->cwp_idx = CWP_EQUAL;
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_C076_INTER_MOD_CTX
#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV
#endif  // CONFIG_C076_INTER_MOD_CTX
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
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
#if CONFIG_WARP_INTER_INTRA
  mbmi->warp_inter_intra = 0;
#endif
#if CONFIG_MORPH_PRED
  mbmi->morph_pred = 0;
#endif
#if CONFIG_DIP
  mbmi->use_intra_dip = 0;
#endif  // CONFIG_DIP
  mbmi->seg_id_predicted = 0;
  mbmi->use_amvd = 0;
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
  mbmi->local_gdf_mode = 0;
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  /// bru use only pixel precision
  set_mv_precision(mbmi, MV_PRECISION_ONE_PEL);
  // set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, cm->seq_params.sb_size);
  set_most_probable_mv_precision(cm, mbmi, cm->seq_params.sb_size);
  mbmi->interp_fltr = MULTITAP_SHARP;

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
  if (cm->seg.enabled) {
    const int mi_offset = xd->mi_row * cm->mi_params.mi_cols + xd->mi_col;
    const int bw = mi_size_wide[bsize];
    const int bh = mi_size_high[bsize];
    const int x_inside_boundary =
        AOMMIN(cm->mi_params.mi_cols - xd->mi_col, bw);
    const int y_inside_boundary =
        AOMMIN(cm->mi_params.mi_rows - xd->mi_row, bh);
    if (!cm->seg.update_map) {
      bru_copy_segment_id(&cm->mi_params, cm->last_frame_seg_map,
                          cm->cur_frame->seg_map, mi_offset, x_inside_boundary,
                          y_inside_boundary);
    } else {
      bru_set_segment_id(cm, mi_offset, x_inside_boundary, y_inside_boundary,
                         mbmi->segment_id);
    }
  }
}

/* Core function of swap BRU reference frame and current frame for BRU optimized
 * decoder/encoder*/
RefCntBuffer *bru_swap_common(AV1_COMMON *cm) {
  // should not use this function at all in none bru frames
  if (cm->bru.enabled) {
    assert(cm->bru.update_ref_idx >= 0);
    RefCntBuffer *ref_buf = get_ref_frame_buf(cm, cm->bru.update_ref_idx);
    assert(ref_buf != NULL);
    cm->bru.update_ref_fc = ref_buf->frame_context;  // pass all the values
    MV_REFERENCE_FRAME ref_frame;
    for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
      const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
      if (buf != NULL && ref_frame < cm->ref_frames_info.num_total_refs) {
        ref_buf->ref_order_hints[ref_frame] = buf->order_hint;
        ref_buf->ref_display_order_hint[ref_frame] = buf->display_order_hint;
      } else {
        ref_buf->ref_order_hints[ref_frame] = -1;
        ref_buf->ref_display_order_hint[ref_frame] = -1;
      }
    }
#if CONFIG_TEMP_LR
    RefCntBuffer *tmp_buf = cm->cur_frame;
#endif
    ref_buf->order_hint = cm->cur_frame->order_hint;
    ref_buf->display_order_hint = cm->cur_frame->display_order_hint;
    ref_buf->absolute_poc = cm->cur_frame->absolute_poc;
    ref_buf->pyramid_level = cm->cur_frame->pyramid_level;
    ref_buf->base_qindex = cm->cur_frame->base_qindex;
    ref_buf->num_ref_frames = cm->cur_frame->num_ref_frames;
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
    ref_buf->frame_output_done = 0;
#endif
#if CONFIG_TEMP_LR
    ref_buf->rst_info[0] = tmp_buf->rst_info[0];
    ref_buf->rst_info[1] = tmp_buf->rst_info[1];
    ref_buf->rst_info[2] = tmp_buf->rst_info[2];
    av1_copy_rst_frame_filters(&ref_buf->rst_info[0], &tmp_buf->rst_info[0]);
    av1_copy_rst_frame_filters(&ref_buf->rst_info[1], &tmp_buf->rst_info[1]);
    av1_copy_rst_frame_filters(&ref_buf->rst_info[2], &tmp_buf->rst_info[2]);
#endif
    if (cm->bru.frame_inactive_flag) {
      ref_buf->ccso_info.ccso_frame_flag = 0;
    } else {
      ref_buf->ccso_info.ccso_frame_flag = tmp_buf->ccso_info.ccso_frame_flag;
    }
    for (int plane = 0; plane < CCSO_NUM_COMPONENTS; plane++) {
      if (cm->bru.frame_inactive_flag) {
        av1_copy_ccso_filters(&ref_buf->ccso_info, &cm->ccso_info, plane, 1, 0,
                              0);
        continue;
      }
      // copy from current to bru ref
      ref_buf->ccso_info.reuse_ccso[plane] =
          tmp_buf->ccso_info.reuse_ccso[plane];
      ref_buf->ccso_info.sb_reuse_ccso[plane] =
          tmp_buf->ccso_info.sb_reuse_ccso[plane];
      ref_buf->ccso_info.ccso_enable[plane] =
          tmp_buf->ccso_info.ccso_enable[plane];
      ref_buf->ccso_info.ccso_ref_idx[plane] =
          tmp_buf->ccso_info.ccso_ref_idx[plane];
      ref_buf->ccso_info.subsampling_x[plane] =
          plane > 0 ? cm->seq_params.subsampling_x : 0;
      ref_buf->ccso_info.subsampling_y[plane] =
          plane > 0 ? cm->seq_params.subsampling_y : 0;
      ref_buf->ccso_info.reuse_root_ref[plane] =
          tmp_buf->ccso_info.reuse_root_ref[plane];
      const int log2_filter_unit_size_y =
          plane == 0 ? CCSO_BLK_SIZE
                     : CCSO_BLK_SIZE - cm->seq_params.subsampling_y;
      const int log2_filter_unit_size_x =
          plane == 0 ? CCSO_BLK_SIZE
                     : CCSO_BLK_SIZE - cm->seq_params.subsampling_x;
      const int ccso_nvfb = ((cm->mi_params.mi_rows >>
                              (plane ? cm->seq_params.subsampling_y : 0)) +
                             (1 << log2_filter_unit_size_y >> 2) - 1) /
                            (1 << log2_filter_unit_size_y >> 2);
      const int ccso_nhfb = ((cm->mi_params.mi_cols >>
                              (plane ? cm->seq_params.subsampling_x : 0)) +
                             (1 << log2_filter_unit_size_x >> 2) - 1) /
                            (1 << log2_filter_unit_size_x >> 2);
      const int sb_count = ccso_nvfb * ccso_nhfb;
      av1_copy_ccso_filters(&ref_buf->ccso_info, &tmp_buf->ccso_info, plane, 1,
                            1, sb_count);
    }
    // replace cur by bru_ref
    assign_frame_buffer_p(&cm->cur_frame, ref_buf);
    return ref_buf;
  }
  return NULL;
}
