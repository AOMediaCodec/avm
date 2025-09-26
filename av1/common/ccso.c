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

#include <assert.h>
#include <math.h>
#include <string.h>

#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "av1/common/ccso.h"
#include "av1/common/reconinter.h"

/* Pad the border of a frame */
#if CONFIG_F054_PIC_BOUNDARY
void extend_ccso_border(const YV12_BUFFER_CONFIG *frame, uint16_t *buf,
                        const int d) {
#else
void extend_ccso_border(uint16_t *buf, const int d, MACROBLOCKD *xd) {
#endif  // CONFIG_F054_PIC_BOUNDARY
#if CONFIG_F054_PIC_BOUNDARY
  int s = frame->y_width + (CCSO_PADDING_SIZE << 1);
  int h = frame->y_height;
  int w = frame->y_width;
#else
  int s = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  int h = xd->plane[0].dst.height;
  int w = xd->plane[0].dst.width;
#endif  // CONFIG_F054_PIC_BOUNDARY
  uint16_t *p = &buf[d * s + d];
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < d; x++) {
      *(p - d + x) = p[0];
      p[w + x] = p[w - 1];
    }
    p += s;
  }
  p -= (s + d);
  for (int y = 0; y < d; y++) {
    memcpy(p + (y + 1) * s, p, sizeof(uint16_t) * (w + (d << 1)));
  }
  p -= ((h - 1) * s);
  for (int y = 0; y < d; y++) {
    memcpy(p - (y + 1) * s, p, sizeof(uint16_t) * (w + (d << 1)));
  }
}

#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
void extend_ccso_tile_border(const int tile_height, const int tile_width,
                             const int tile_stride, uint16_t *buf,
                             const int d) {
  uint16_t *p = &buf[d * tile_stride + d];
  for (int y = 0; y < tile_height; y++) {
    for (int x = 0; x < d; x++) {
      *(p - d + x) = p[0];
      p[tile_width + x] = p[tile_width - 1];
    }
    p += tile_stride;
  }
  p -= (tile_stride + d);
  for (int y = 0; y < d; y++) {
    memcpy(p + (y + 1) * tile_stride, p,
           sizeof(uint16_t) * (tile_width + (d << 1)));
  }
  p -= ((tile_height - 1) * tile_stride);
  for (int y = 0; y < d; y++) {
    memcpy(p - (y + 1) * tile_stride, p,
           sizeof(uint16_t) * (tile_width + (d << 1)));
  }
}
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES

/* Derive the quantized index, later it can be used for retriving offset values
 * from the look-up table */
void cal_filter_support(int *rec_luma_idx, const uint16_t *rec_y,
                        const int quant_step_size, const int inv_quant_step,
                        const int *rec_idx, const int edge_clf) {
  if (edge_clf == 0) {
    for (int i = 0; i < 2; i++) {
      int d = rec_y[rec_idx[i]] - rec_y[0];
      if (d > quant_step_size)
        rec_luma_idx[i] = 2;
      else if (d < inv_quant_step)
        rec_luma_idx[i] = 0;
      else
        rec_luma_idx[i] = 1;
    }
  } else {  // if (edge_clf == 1)
    for (int i = 0; i < 2; i++) {
      int d = rec_y[rec_idx[i]] - rec_y[0];
      if (d < inv_quant_step)
        rec_luma_idx[i] = 0;
      else
        rec_luma_idx[i] = 1;
    }
  }
}

/* Derive sample locations for CCSO */
void derive_ccso_sample_pos(int *rec_idx, const int ccso_stride,
                            const uint8_t ext_filter_support) {
  // Input sample locations for CCSO
  // 4 2 0 3 5
  // 6 1 x 1 6
  // 5 3 0 2 4
  assert(ext_filter_support < 7);
  if (ext_filter_support == 0) {
    rec_idx[0] = -1 * ccso_stride;
    rec_idx[1] = 1 * ccso_stride;
  } else if (ext_filter_support == 1) {
    rec_idx[0] = -1;
    rec_idx[1] = 1;
  } else if (ext_filter_support == 4) {
    rec_idx[0] = -ccso_stride - 2;
    rec_idx[1] = ccso_stride + 2;
  } else if (ext_filter_support == 5) {
    rec_idx[0] = ccso_stride - 2;
    rec_idx[1] = -ccso_stride + 2;
  } else if (ext_filter_support == 2) {
    rec_idx[0] = -1 * ccso_stride - 1;
    rec_idx[1] = 1 * ccso_stride + 1;
  } else if (ext_filter_support == 3) {
    rec_idx[0] = -1 * ccso_stride + 1;
    rec_idx[1] = 1 * ccso_stride - 1;
  } else {  // ext_filter_support == 6
    rec_idx[0] = 2;
    rec_idx[1] = -2;
  }
}

#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
/* Derive rect area of a tile for CCSO process */
INLINE static AV1PixelRect av1_get_tile_rect_ccso(const TileInfo *tile_info,
                                                  const AV1_COMMON *cm,
                                                  MACROBLOCKD *xd, int is_uv) {
  AV1PixelRect r;

  // Calculate position in the Y plane
  r.left = tile_info->mi_col_start * MI_SIZE;
  r.right = tile_info->mi_col_end * MI_SIZE;
  r.top = tile_info->mi_row_start * MI_SIZE;
  r.bottom = tile_info->mi_row_end * MI_SIZE;

  const int frame_w = xd->plane[0].dst.width;
  const int frame_h = xd->plane[0].dst.height;

  // Make sure we don't fall off the bottom-right of the frame.
  r.right = AOMMIN(r.right, frame_w);
  r.bottom = AOMMIN(r.bottom, frame_h);

  // Convert to coordinates in the appropriate plane
  const int ss_x = is_uv && cm->seq_params.subsampling_x;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;

  r.left = ROUND_POWER_OF_TWO(r.left, ss_x);
  r.right = ROUND_POWER_OF_TWO(r.right, ss_x);
  r.top = ROUND_POWER_OF_TWO(r.top, ss_y);
  r.bottom = ROUND_POWER_OF_TWO(r.bottom, ss_y);

  return r;
}
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES

// Cross-component Sample Offset band offset only case.
void ccso_filter_block_hbd_wo_buf_bo_only_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y,
    const int pic_width, const int pic_height, const int8_t *offset_buf,
    const int src_y_stride, const int dst_stride, const int y_uv_hscale,
    const int y_uv_vscale, const int max_val, const int blk_size_x,
    const int blk_size_y, const bool isSingleBand, const uint8_t shift_bits) {
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
      const int band_num = isSingleBand
                               ? 0
                               : src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                       (x_pos << y_uv_hscale)] >>
                                     shift_bits;
      const int lut_idx_ext = (band_num << 4);
      const int offset_val = offset_buf[lut_idx_ext];
      dst_yuv[y_pos * dst_stride + x_pos] =
          clamp(offset_val + dst_yuv[y_pos * dst_stride + x_pos], 0, max_val);
    }
  }
}

void ccso_filter_block_hbd_wo_buf_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y,
    const int pic_width, const int pic_height, int *src_cls,
    const int8_t *offset_buf, const int src_y_stride, const int dst_stride,
    const int y_uv_hscale, const int y_uv_vscale, const int thr,
    const int neg_thr, const int *src_loc, const int max_val,
    const int blk_size_x, const int blk_size_y, const bool isSingleBand,
    const uint8_t shift_bits, const int edge_clf, const uint8_t ccso_bo_only) {
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
      if (!ccso_bo_only) {
        cal_filter_support(src_cls,
                           &src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                  (x_pos << y_uv_hscale)],
                           thr, neg_thr, src_loc, edge_clf);
      } else {
        src_cls[0] = 0;
        src_cls[1] = 0;
      }
      const int band_num = isSingleBand
                               ? 0
                               : src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                       (x_pos << y_uv_hscale)] >>
                                     shift_bits;
      const int lut_idx_ext = (band_num << 4) + (src_cls[0] << 2) + src_cls[1];
      const int offset_val = offset_buf[lut_idx_ext];
      dst_yuv[y_pos * dst_stride + x_pos] =
          clamp(offset_val + dst_yuv[y_pos * dst_stride + x_pos], 0, max_val);
    }
  }
}

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
// If there is at-least 1 segment is lossless in a frame, we have
// to do 4x4 processing, because minimum lossless block can be 4x4
// size. Although, regardless the value of
// cm->features.has_lossless_segment, we can always do 4x4
// processing, however, for software optimization purpose we have
// used  full block processing for whole lossy frame.
void ccso_filter_block_hbd_wo_buf_4x4_c(
    AV1_COMMON *cm, const uint16_t *src_y, uint16_t *dst_yuv,
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    int tile_col_start, int tile_row_start,
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int x, const int y, const int pic_width, const int pic_height,
    int *src_cls, const int8_t *offset_buf, const int src_y_stride,
    const int dst_stride, const int y_uv_hscale, const int y_uv_vscale,
    const int thr, const int neg_thr, const int *src_loc, const int max_val,
    const int blk_size_x, const int blk_size_y, const bool isSingleBand,
    const uint8_t shift_bits, const int edge_clf, const uint8_t ccso_bo_only,
    int plane) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  int min_b_size_x = (1 << MI_SIZE_LOG2) >> y_uv_hscale;
  int min_b_size_y = (1 << MI_SIZE_LOG2) >> y_uv_vscale;

  for (int y_start = 0; y_start < y_end; y_start += min_b_size_y) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start += min_b_size_x) {
      const int x_pos = x + x_start;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      const int this_mi_row =
          ((tile_row_start + y_pos + y) << y_uv_vscale) >> MI_SIZE_LOG2;
      const int this_mi_col =
          ((tile_col_start + x_pos) << y_uv_hscale) >> MI_SIZE_LOG2;

#else
      const int this_mi_row = ((y_pos + y) << y_uv_vscale) >> MI_SIZE_LOG2;
      const int this_mi_col = (x_pos << y_uv_hscale) >> MI_SIZE_LOG2;

#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      MB_MODE_INFO **this_mbmi_ptr = mi_params->mi_grid_base +
                                     this_mi_row * mi_params->mi_stride +
                                     this_mi_col;
      MB_MODE_INFO **this_mbmi =
          get_mi_location_from_collocated_mi(cm, this_mbmi_ptr, plane);

      const int is_lossless =
          cm->features.lossless_segment[this_mbmi[0]->segment_id];
      if (!is_lossless) {
        int j_max = AOMMIN(x_pos + min_b_size_x, x + x_start + x_end);
        int i_max = AOMMIN(y_pos + min_b_size_y, y_end);
        for (int i_pos_4x4 = y_pos; i_pos_4x4 < i_max; i_pos_4x4++) {
          for (int j_pos_4x4 = x_pos; j_pos_4x4 < j_max; j_pos_4x4++) {
            if (!ccso_bo_only) {
              cal_filter_support(
                  src_cls,
                  &src_y[(i_pos_4x4 << y_uv_vscale) * src_y_stride +
                         (j_pos_4x4 << y_uv_hscale)],
                  thr, neg_thr, src_loc, edge_clf);
            } else {
              src_cls[0] = 0;
              src_cls[1] = 0;
            }
            const int band_num =
                isSingleBand ? 0
                             : src_y[(i_pos_4x4 << y_uv_vscale) * src_y_stride +
                                     (j_pos_4x4 << y_uv_hscale)] >>
                                   shift_bits;
            const int lut_idx_ext =
                (band_num << 4) + (src_cls[0] << 2) + src_cls[1];
            const int offset_val = offset_buf[lut_idx_ext];
            dst_yuv[i_pos_4x4 * dst_stride + j_pos_4x4] =
                clamp(offset_val + dst_yuv[i_pos_4x4 * dst_stride + j_pos_4x4],
                      0, max_val);
          }
        }
      }
    }
  }
}
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

// Apply CCSO for each process block row
void av1_apply_ccso_filter_for_row(AV1_COMMON *cm, MACROBLOCKD *xd,
                                   const uint16_t *src_y, uint16_t *dst_yuv,
                                   int *src_loc, int *src_cls, int blk_row,
                                   int thr, int blk_size, int blk_size_proc,
                                   int blk_log2_x, int blk_log2_y,
                                   int unit_log2_x, int unit_log2_y,
                                   int plane) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int src_y_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const uint8_t max_band_log2 = cm->ccso_info.max_band_log2[plane];
  const int dst_stride = xd->plane[plane].dst.stride;
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int neg_thr = thr * -1;
  const bool is_single_band = !max_band_log2;
  const uint8_t shift_bits = cm->seq_params.bit_depth - max_band_log2;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int edge_clf = cm->ccso_info.edge_clf[plane];
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;

  for (int blk_col = 0; blk_col < pic_width; blk_col += blk_size_proc) {
    const int ccso_blk_idx =
        (blk_size >> MI_SIZE_LOG2) * (blk_row >> blk_log2_y) *
            mi_params->mi_stride +
        (blk_size >> MI_SIZE_LOG2) * (blk_col >> blk_log2_x);
    const bool use_ccso =
        (plane == 0)   ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y
        : (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                       : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
    if (!use_ccso) continue;

    // FPU level skip
    const int x_mbmi = (blk_col >> unit_log2_x) << unit_log2_x;
    const int y_mbmi = (blk_row >> unit_log2_y) << unit_log2_y;
    const int mbmi_idx =
        get_mi_grid_idx(mi_params, y_mbmi >> (MI_SIZE_LOG2 - y_uv_vscale),
                        x_mbmi >> (MI_SIZE_LOG2 - y_uv_hscale));
    const int use_ccso_local =
        mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
    if (!use_ccso_local) continue;

    if (cm->bru.enabled &&
        mi_params->mi_grid_base[mbmi_idx]->sb_active_mode != BRU_ACTIVE_SB) {
      aom_internal_error(
          &cm->error, AOM_CODEC_ERROR,
          "Invalid BRU activity in CCSO: only active SB can be filtered");
      return;
    }

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
    if (cm->features.has_lossless_segment) {
      ccso_filter_block_hbd_wo_buf_4x4_c(
          cm, src_y, dst_yuv,
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
          0, 0,
#endif  // CCONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
          blk_col, blk_row, pic_width, pic_height, src_cls,
          cm->ccso_info.filter_offset[plane], src_y_stride, dst_stride,
          y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
          blk_size_proc, blk_size_proc, is_single_band, shift_bits, edge_clf,
          cm->ccso_info.ccso_bo_only[plane], plane);
    } else {
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_bo_only(
            src_y, dst_yuv, blk_col, blk_row, pic_width, pic_height,
            cm->ccso_info.filter_offset[plane], src_y_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, max_val, blk_size_proc, blk_size_proc,
            is_single_band, shift_bits);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, blk_col, blk_row, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], src_y_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
            blk_size_proc, blk_size_proc, is_single_band, shift_bits, edge_clf,
            0);
      }
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
    }
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  }
}

/* Apply CCSO on luma or chroma component when single or multiple bands are
 * applied */
void apply_ccso_filter(AV1_COMMON *cm, MACROBLOCKD *xd, int plane,
                       const uint16_t *src_y, uint16_t *dst_yuv, int dst_stride,
                       int proc_unit_log2, uint16_t thr, uint8_t filter_sup
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
                       ,
                       uint8_t max_band_log2, int edge_clf
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
) {
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  int src_cls[2];
  int src_loc[2];
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
  const int blk_log2 = CCSO_BLK_SIZE;
  const int blk_size = 1 << blk_log2;
  const int blk_log2_x = CCSO_BLK_SIZE - y_uv_hscale;
  const int blk_log2_y = CCSO_BLK_SIZE - y_uv_vscale;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
  const int unit_log2_x = AOMMIN(proc_unit_log2, blk_log2_x);
  const int unit_log2_y = AOMMIN(proc_unit_log2, blk_log2_y);
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const uint8_t shift_bits = cm->seq_params.bit_depth - max_band_log2;
  const bool is_single_band = !max_band_log2;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int neg_thr = thr * -1;
  const int unit_size_x = 1 << unit_log2_x;
  const int unit_size_y = 1 << unit_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int blk_size_y = 1 << blk_log2_y;
  if (cm->seq_params.disable_loopfilters_across_tiles) {
    int tile_rows = cm->tiles.rows;
    int tile_cols = cm->tiles.cols;
    TileInfo tile_info_y;
    AV1PixelRect tile_rect_y;
    for (int tile_row = 0; tile_row < tile_rows; ++tile_row) {
      int tile_height = 0;
      for (int tile_col = 0; tile_col < tile_cols; ++tile_col) {
        av1_tile_init(&tile_info_y, cm, tile_row, tile_col);
        tile_rect_y = av1_get_tile_rect_ccso(&tile_info_y, cm, xd, 0);

        int tile_row_start = tile_rect_y.top;
        int tile_col_start = tile_rect_y.left;
        int tile_row_end = tile_rect_y.bottom;
        int tile_col_end = tile_rect_y.right;
        int tile_width = tile_col_end - tile_col_start;
        tile_height = tile_row_end - tile_row_start;
        const int ccso_ext_tile_stride = tile_width + (CCSO_PADDING_SIZE << 1);
        derive_ccso_sample_pos(src_loc, ccso_ext_tile_stride, filter_sup);

        uint16_t *ext_rec_tile_y = NULL;
        // const uint16_t *rec_y = cm->cur_frame->buf.y_buffer;
        ext_rec_tile_y = aom_malloc(sizeof(*ext_rec_tile_y) *
                                    (tile_height + (CCSO_PADDING_SIZE << 1)) *
                                    (tile_width + (CCSO_PADDING_SIZE << 1)));
        for (int r = 0; r < tile_height; ++r) {
          for (int c = 0; c < tile_width; ++c) {
            ext_rec_tile_y[(r + CCSO_PADDING_SIZE) * ccso_ext_tile_stride + c +
                           CCSO_PADDING_SIZE] =
                src_y[(r + tile_row_start) * ccso_ext_stride +
                      (c + tile_col_start)];
          }
        }
        extend_ccso_tile_border(tile_height, tile_width, ccso_ext_tile_stride,
                                ext_rec_tile_y, CCSO_PADDING_SIZE);

        uint16_t *src_tile_y = ext_rec_tile_y;
        src_tile_y +=
            CCSO_PADDING_SIZE * ccso_ext_tile_stride + CCSO_PADDING_SIZE;
        if (plane != 0) {
          TileInfo tile_info_uv;
          AV1PixelRect tile_rect_uv;
          av1_tile_init(&tile_info_uv, cm, tile_row, tile_col);
          tile_rect_uv = av1_get_tile_rect_ccso(&tile_info_uv, cm, xd, 1);
          tile_row_start = tile_rect_uv.top;
          tile_col_start = tile_rect_uv.left;
          tile_row_end = tile_rect_uv.bottom;
          tile_col_end = tile_rect_uv.right;
          tile_width = tile_col_end - tile_col_start;
          tile_height = tile_row_end - tile_row_start;
        }
        uint16_t *dst_tile_yuv = dst_yuv + tile_col_start;
        for (int frame_pxl_y = tile_row_start; frame_pxl_y < tile_row_end;
             frame_pxl_y += blk_size_y) {
          for (int frame_pxl_x = tile_col_start; frame_pxl_x < tile_col_end;
               frame_pxl_x += blk_size_x) {
            // int x = frame_pxl_x;
            // int y = frame_pxl_y;
            int tile_pxl_x = frame_pxl_x - tile_col_start;
            int tile_pxl_y = frame_pxl_y - tile_row_start;

            const int ccso_blk_idx =
                (blk_size >> MI_SIZE_LOG2) * (frame_pxl_y >> blk_log2_y) *
                    mi_params->mi_stride +
                (blk_size >> MI_SIZE_LOG2) * (frame_pxl_x >> blk_log2_x);
            const bool use_ccso =
                (plane == 0) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y
                : (plane == 1)
                    ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                    : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
            if (!use_ccso) continue;
            const uint16_t *src_unit_y = src_tile_y;
            uint16_t *dst_unit_yuv = dst_tile_yuv;
            const int y_end = AOMMIN(tile_row_end - frame_pxl_y, blk_size_y);
            const int x_end = AOMMIN(tile_col_end - frame_pxl_x, blk_size_x);
            for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
              for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
                // FPU level skip
                const int mbmi_idx = get_mi_grid_idx(
                    mi_params,
                    (frame_pxl_y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
                    (frame_pxl_x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
                const int use_ccso_local =
                    mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
                if (!use_ccso_local) {
                  continue;
                }
                if (cm->bru.enabled &&
                    mi_params->mi_grid_base[mbmi_idx]->sb_active_mode !=
                        BRU_ACTIVE_SB) {
                  aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                     "Invalid BRU activity in CCSO: only "
                                     "active SB can be filtered");
                  return;
                }
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                if (cm->features.has_lossless_segment) {
                  ccso_filter_block_hbd_wo_buf_4x4_c(
                      cm, src_unit_y, dst_unit_yuv, tile_col_start,
                      tile_row_start, tile_pxl_x + unit_x, tile_pxl_y + unit_y,
                      tile_width, tile_height, src_cls,
                      cm->ccso_info.filter_offset[plane], ccso_ext_tile_stride,
                      dst_stride, y_uv_hscale, y_uv_vscale, thr, neg_thr,
                      src_loc, max_val, unit_size_x, unit_size_y,
                      is_single_band, shift_bits, edge_clf,
                      cm->ccso_info.ccso_bo_only[plane], plane);
                } else {
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                  if (cm->ccso_info.ccso_bo_only[plane]) {
                    ccso_filter_block_hbd_wo_buf_c(
                        src_unit_y, dst_unit_yuv, tile_pxl_x + unit_x,
                        tile_pxl_y + unit_y, tile_width, tile_height, src_cls,
                        cm->ccso_info.filter_offset[plane],
                        ccso_ext_tile_stride, dst_stride, y_uv_hscale,
                        y_uv_vscale, thr, neg_thr, src_loc, max_val,
                        unit_size_x, unit_size_y, is_single_band, shift_bits,
                        edge_clf, cm->ccso_info.ccso_bo_only[plane]);
                  } else {
                    ccso_filter_block_hbd_wo_buf(
                        src_unit_y, dst_unit_yuv, tile_pxl_x + unit_x,
                        tile_pxl_y + unit_y, tile_width, tile_height, src_cls,
                        cm->ccso_info.filter_offset[plane],
                        ccso_ext_tile_stride, dst_stride, y_uv_hscale,
                        y_uv_vscale, thr, neg_thr, src_loc, max_val,
                        unit_size_x, unit_size_y, is_single_band, shift_bits,
                        edge_clf, 0);
                  }
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                }
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
              }
              dst_unit_yuv += (dst_stride << unit_log2_y);
              src_unit_y +=
                  (ccso_ext_tile_stride << (unit_log2_y + y_uv_vscale));
            }
          }
          dst_tile_yuv += (dst_stride << blk_log2_y);
          src_tile_y += (ccso_ext_tile_stride << (blk_log2_y + y_uv_vscale));
        }
        aom_free(ext_rec_tile_y);
      }
      dst_yuv += (dst_stride * tile_height);
      // src_y += (ccso_ext_stride * (cm->tiles.height << (MI_SIZE_LOG2)));
    }
    return;
  }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  const int blk_log2_proc = CCSO_PROC_BLK_LOG2;
  const int blk_size_proc = 1 << blk_log2_proc;
  for (int blk_row = 0; blk_row < pic_height; blk_row += blk_size_proc) {
    av1_apply_ccso_filter_for_row(
        cm, xd, src_y, dst_yuv, src_loc, src_cls, blk_row, thr, blk_size,
        blk_size_proc, blk_log2_x, blk_log2_y, unit_log2_x, unit_log2_y, plane);
    dst_yuv += dst_stride << blk_log2_proc;
    src_y += ccso_ext_stride << (blk_log2_proc + y_uv_vscale);
  }
}

/* Apply CCSO for one frame */
void ccso_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm, MACROBLOCKD *xd,
                uint16_t *ext_rec_y) {
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

#if !CONFIG_CCSO_CLEANUP
  const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                    { 32, 16, 64, 128 },
                                    { 48, 24, 96, 192 },
                                    { 64, 32, 128, 256 } };
#endif  // !CONFIG_CCSO_CLEANUP

  for (int plane = 0; plane < num_planes; plane++) {
    const int dst_stride = xd->plane[plane].dst.stride;
    const uint16_t quant_step_size = quant_sz[cm->ccso_info.scale_idx[plane]]
                                             [cm->ccso_info.quant_idx[plane]];
    if (cm->ccso_info.ccso_enable[plane]) {
      apply_ccso_filter(
          cm, xd, plane, ext_rec_y, &(xd->plane[plane].dst.buf)[0], dst_stride,
          cm->mib_size_log2 -
              AOMMAX(xd->plane[plane].subsampling_x,
                     xd->plane[plane].subsampling_y) +
              MI_SIZE_LOG2,
          quant_step_size, cm->ccso_info.ext_filter_support[plane]
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
          ,
          cm->ccso_info.max_band_log2[plane], cm->ccso_info.edge_clf[plane]
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      );
    }
  }
}

// This function is to copy ccso filter parameters between frames when
// ccso_reuse is true.
void av1_copy_ccso_filters(CcsoInfo *to, CcsoInfo *from, int plane,
                           bool frame_level, bool block_level, int sb_count) {
  if (frame_level) {
    memcpy(to->filter_offset[plane], from->filter_offset[plane],
           sizeof(to->filter_offset[plane]));
    to->quant_idx[plane] = from->quant_idx[plane];
    to->ext_filter_support[plane] = from->ext_filter_support[plane];
    to->edge_clf[plane] = from->edge_clf[plane];
    to->ccso_bo_only[plane] = from->ccso_bo_only[plane];
    to->max_band_log2[plane] = from->max_band_log2[plane];
    to->scale_idx[plane] = from->scale_idx[plane];
  }

  if (block_level) {
    if (to->sb_filter_control[plane]) {
      memcpy(to->sb_filter_control[plane], from->sb_filter_control[plane],
             sizeof(*from->sb_filter_control[plane]) * sb_count);
    }
  }

  to->ccso_enable[plane] = from->ccso_enable[plane];
}
