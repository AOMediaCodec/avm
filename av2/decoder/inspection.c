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
#include "av2/decoder/decoder.h"
#include "av2/decoder/inspection.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"
#include "av2/common/cdef.h"

static void ifd_init_mi_rc(insp_frame_data *fd, int mi_cols, int mi_rows) {
  fd->mi_cols = mi_cols;
  fd->mi_rows = mi_rows;
  fd->mi_grid = (insp_mi_data *)avm_malloc(sizeof(insp_mi_data) * fd->mi_rows *
                                           fd->mi_cols);
  fd->max_sb_rows =
      (mi_rows + (1 << MIN_MIB_SIZE_LOG2) - 1) / (1 << MIN_MIB_SIZE_LOG2);
  fd->max_sb_cols =
      (mi_cols + (1 << MIN_MIB_SIZE_LOG2) - 1) / (1 << MIN_MIB_SIZE_LOG2);
  fd->sb_grid = (insp_sb_data *)avm_calloc(sizeof(insp_sb_data),
                                           fd->max_sb_rows * fd->max_sb_cols);
}

void ifd_init(insp_frame_data *fd, int frame_width, int frame_height) {
  int mi_cols = ALIGN_POWER_OF_TWO(frame_width, 3) >> MI_SIZE_LOG2;
  int mi_rows = ALIGN_POWER_OF_TWO(frame_height, 3) >> MI_SIZE_LOG2;
  ifd_init_mi_rc(fd, mi_cols, mi_rows);
}

void ifd_clear(insp_frame_data *fd) {
  avm_free(fd->mi_grid);
  fd->mi_grid = NULL;
  for (int i = 0; i < fd->max_sb_rows; i++) {
    for (int j = 0; j < fd->max_sb_cols; j++) {
      insp_sb_data *sb = &fd->sb_grid[i * fd->max_sb_cols + j];
      // Note: NULL checking happens within av2_free_ptree_recursive
      av2_free_ptree_recursive(sb->partition_tree_luma);
      av2_free_ptree_recursive(sb->partition_tree_chroma);
    }
  }
  avm_free(fd->sb_grid);
  fd->sb_grid = NULL;
}

PARTITION_TREE *copy_partition_tree(PARTITION_TREE *orig,
                                    PARTITION_TREE *parent) {
  PARTITION_TREE *copy = av2_alloc_ptree_node(NULL, 0);
  memcpy(copy, orig, sizeof(PARTITION_TREE));
  copy->parent = parent;
  for (size_t i = 0; i < sizeof(copy->sub_tree) / sizeof(copy->sub_tree[0]);
       i++) {
    if (copy->sub_tree[i] != NULL) {
      copy->sub_tree[i] = copy_partition_tree(orig->sub_tree[i], copy);
    }
  }
  return copy;
}

int ifd_inspect_superblock(insp_frame_data *fd, void *decoder) {
  struct AV2Decoder *pbi = (struct AV2Decoder *)decoder;
  AV2_COMMON *const cm = &pbi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  if (fd->mi_rows != mi_params->mi_rows || fd->mi_cols != mi_params->mi_cols) {
    ifd_clear(fd);
    ifd_init_mi_rc(fd, mi_params->mi_cols, mi_params->mi_rows);
  }

  int frame_type = pbi->common.current_frame.frame_type;
  int sb_size = cm->seq_params.sb_size;
  // 256x256 superblocks are disabled for intra frames.
  if (sb_size == BLOCK_256X256 && frame_type == 0) {
    sb_size = BLOCK_128X128;
  }
  int sb_width = mi_size_wide[sb_size];
  int sb_height = mi_size_high[sb_size];

  int sb_row = pbi->td.dcb.xd.sbi->mi_row / sb_height;
  int sb_col = pbi->td.dcb.xd.sbi->mi_col / sb_width;

  PARTITION_TREE *luma_tree = pbi->td.dcb.xd.sbi->ptree_root[0];
  PARTITION_TREE *chroma_tree = pbi->td.dcb.xd.sbi->ptree_root[1];
  insp_sb_data *sb = &fd->sb_grid[sb_row * fd->max_sb_cols + sb_col];
  sb->partition_tree_luma = copy_partition_tree(luma_tree, NULL);
  // Semi-decoupled partitioning is enabled only for intra-frames.
  int use_sdp = is_sdp_enabled_in_keyframe(cm);
  if (chroma_tree != NULL && use_sdp) {
    sb->partition_tree_chroma = copy_partition_tree(chroma_tree, NULL);
  } else {
    // For consistency, use a copy of the luma tree when SDP is not enabled for
    // the frame.
    sb->partition_tree_chroma = copy_partition_tree(luma_tree, NULL);
  }
  sb->has_separate_chroma_partition_tree = use_sdp;

  for (int i = 0; i < MAX_MB_PLANE; i++) {
    memcpy(sb->dqcoeff[i], pbi->td.dcb.dqcoeff_block_copy[i], MAX_SB_SQUARE);
    memcpy(sb->qcoeff[i], pbi->td.dcb.qcoeff_block[i], MAX_SB_SQUARE);
    memcpy(sb->dequant_values[i], pbi->td.dcb.dequant_values[i], MAX_SB_SQUARE);
  }
  return 1;
}

/* TODO(negge) This function may be called by more than one thread when using
               a multi-threaded decoder and this may cause a data race. */
int ifd_inspect(insp_frame_data *fd, void *decoder, int skip_not_transform) {
  struct AV2Decoder *pbi = (struct AV2Decoder *)decoder;
  AV2_COMMON *const cm = &pbi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const CommonQuantParams *quant_params = &cm->quant_params;
  fd->recon_frame_buffer = cm->cur_frame->buf;
  fd->predicted_frame_buffer = cm->predicted_pixels;
  fd->prefiltered_frame_buffer = cm->prefiltered_pixels;
  if (fd->mi_rows != mi_params->mi_rows || fd->mi_cols != mi_params->mi_cols) {
    ifd_clear(fd);
    ifd_init_mi_rc(fd, mi_params->mi_cols, mi_params->mi_rows);
  }
#if !CONFIG_F024_KEYOBU
  fd->show_existing_frame = cm->show_existing_frame;
#endif
  fd->frame_number = cm->current_frame.frame_number;
  fd->immediate_output_picture = cm->immediate_output_picture;
  fd->frame_type = cm->current_frame.frame_type;
  fd->base_qindex = quant_params->base_qindex;
  fd->tip_frame_mode = cm->features.tip_frame_mode;

  int sb_size = cm->seq_params.sb_size;
  // 256x256 superblocks are disabled for intra frames.
  if (sb_size == BLOCK_256X256 && fd->frame_type == 0) {
    sb_size = BLOCK_128X128;
  }

  fd->superblock_size = sb_size;
  // Set width and height of the first tile until generic support can be added
  TileInfo tile_info;
  av2_tile_set_row(&tile_info, cm, 0);
  av2_tile_set_col(&tile_info, cm, 0);
  fd->tile_mi_cols = tile_info.mi_col_end - tile_info.mi_col_start;
  fd->tile_mi_rows = tile_info.mi_row_end - tile_info.mi_row_start;
  fd->delta_q_present_flag = cm->delta_q_info.delta_q_present_flag;
  fd->delta_q_res = cm->delta_q_info.delta_q_res;
  fd->bit_depth = cm->seq_params.bit_depth;
  fd->width = cm->width;
  fd->height = cm->height;
  fd->render_width = cm->render_width;
  fd->render_height = cm->render_height;
#if CONFIG_ACCOUNTING
  fd->accounting = &pbi->accounting;
#endif
  // TODO(negge): copy per frame CDEF data
  int i, j;
  for (i = 0; i < MAX_SEGMENTS; i++) {
    for (j = 0; j < 2; j++) {
      fd->y_dequant[i][j] = quant_params->y_dequant_QTX[i][j];
      fd->u_dequant[i][j] = quant_params->u_dequant_QTX[i][j];
      fd->v_dequant[i][j] = quant_params->v_dequant_QTX[i][j];
    }
  }
  for (j = 0; j < mi_params->mi_rows; j++) {
    for (i = 0; i < mi_params->mi_cols; i++) {
      const MB_MODE_INFO *mbmi =
          mi_params->mi_grid_base[j * mi_params->mi_stride + i];
      insp_mi_data *mi = &fd->mi_grid[j * mi_params->mi_cols + i];
      // Segment
      mi->segment_id = mbmi->segment_id;
      for (int mv = 0; mv < 2; mv++) {
        // Motion Vectors
        mi->mv[mv].row = mbmi->mv[mv].as_mv.row;
        mi->mv[mv].col = mbmi->mv[mv].as_mv.col;
        // Reference Frames
        mi->ref_frame[mv] = mbmi->ref_frame[mv];
        mi->ref_frame_is_tip[mv] = is_tip_ref_frame(mbmi->ref_frame[mv]);
        mi->ref_frame_is_inter[mv] = is_inter_ref_frame(mbmi->ref_frame[mv]);
        RefCntBuffer *buf = get_ref_frame_buf(cm, mbmi->ref_frame[mv]);
        if (buf != NULL) {
          mi->ref_frame_order_hint[mv] = buf->order_hint;
        } else {
          mi->ref_frame_order_hint[mv] = -1;
        }
      }
      mi->mv_precision = mbmi->pb_mv_precision;

      mi->angle_delta = mbmi->angle_delta[0];
      mi->uv_angle_delta = mbmi->angle_delta[1];
      // Prediction Mode
      mi->mode = mbmi->mode;
      mi->intrabc = (int16_t)mbmi->use_intrabc[0];
      mi->palette = (int16_t)mbmi->palette_mode_info.palette_size[0];
      mi->uv_palette = (int16_t)mbmi->palette_mode_info.palette_size[1];
      // Prediction Mode for Chromatic planes
      if (mi->mode < INTRA_MODES) {
        mi->uv_mode = mbmi->uv_mode;
      } else {
        mi->uv_mode = UV_MODE_INVALID;
      }

      mi->motion_mode = mbmi->motion_mode;
      mi->compound_type = mbmi->interinter_comp.type;

      // Block Size
      mi->sb_type = mbmi->sb_type[0];
      mi->sb_type_chroma = mbmi->sb_type[1];
      // Skip Flag
      // TODO(comc): Check handling of skip_txfm vs tx_skip.
      mi->skip = mbmi->skip_txfm[0];
      mi->filter[0] = mbmi->interp_fltr;
      mi->filter[1] = mbmi->interp_fltr;
      mi->dual_filter_type = mi->filter[0] * 3 + mi->filter[1];

      // Transform
      // TODO(comc): Should use partition type here!
      const BLOCK_SIZE bsize = mbmi->sb_type[0];
      const int c = i % mi_size_wide[bsize];
      const int r = j % mi_size_high[bsize];

      mi->tx_size = mbmi->tx_size;

      if (skip_not_transform && mi->skip) mi->tx_size = -1;

      // TODO(comc): chroma tx_type uses lookup table based on mode for intra,
      // and the same as luma for inter.
      const int tx_type_row = j - j % tx_size_high_unit[mi->tx_size];
      const int tx_type_col = i - i % tx_size_wide_unit[mi->tx_size];
      const int tx_type_map_idx =
          tx_type_row * mi_params->mi_stride + tx_type_col;
      mi->tx_type = mi_params->tx_type_map[tx_type_map_idx];

      bool skip = mbmi->tx_skip[av2_get_txk_type_index(bsize, r, c)];
      mi->skip |= skip;

      if (skip_not_transform &&
          (mi->skip || mbmi->tx_skip[av2_get_txk_type_index(bsize, r, c)])) {
        mi->tx_type = -1;
      }

      mi->cdef_level = cm->cdef_info.cdef_strengths[mbmi->cdef_strength] /
                       CDEF_SEC_STRENGTHS;
      mi->cdef_strength = cm->cdef_info.cdef_strengths[mbmi->cdef_strength] %
                          CDEF_SEC_STRENGTHS;

      mi->cdef_strength += mi->cdef_strength == 3;
      if (mbmi->uv_mode == UV_CFL_PRED) {
        mi->cfl_alpha_idx = mbmi->cfl_alpha_idx;
        mi->cfl_alpha_sign = mbmi->cfl_alpha_signs;
      } else {
        mi->cfl_alpha_idx = 0;
        mi->cfl_alpha_sign = 0;
      }
      // delta_q
      mi->current_qindex = mbmi->current_qindex;
    }
  }
  return 1;
}
