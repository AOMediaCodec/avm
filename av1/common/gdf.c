/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */
#ifndef AOM_COMMON_GDF_H_
#define AOM_COMMON_GDF_H_

#include "av1/common/gdf.h"
#include "av1/common/gdf_block.h"

static int gdf_num_stripes_in_tile(int stripe_size, int tile_size) {
  const int first_stripe_offset = GDF_TEST_STRIPE_OFF;
  return (tile_size + first_stripe_offset + stripe_size - 1) / stripe_size;
}

void init_gdf_test(GdfInfo *gi, int mib_size, int rec_height, int rec_width) {
  gi->gdf_mode = 0;
  gi->gdf_pic_qp_idx = 0;
  gi->gdf_pic_scale_idx = 0;
  gi->gdf_block_size = AOMMAX(mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
  gi->gdf_stripe_size = GDF_TEST_STRIPE_SIZE;
  gi->gdf_unit_size = GDF_TEST_STRIPE_SIZE;
  gi->gdf_vert_blks_per_tile[0] = 1 + ((rec_height - 1) / gi->gdf_block_size);
  gi->gdf_block_num_h = 1 + ((rec_height - 1) / gi->gdf_block_size);
  gi->gdf_horz_blks_per_tile[0] = 1 + ((rec_width - 1) / gi->gdf_block_size);
  gi->gdf_block_num_w = 1 + ((rec_width - 1) / gi->gdf_block_size);
  gi->gdf_block_num = gi->gdf_block_num_h * gi->gdf_block_num_w;
  gi->gdf_vert_stripes_per_tile[0] =
      gdf_num_stripes_in_tile(gi->gdf_stripe_size, rec_height);
  gi->err_height = gi->gdf_unit_size;
  gi->lap_stride = gi->gdf_unit_size + GDF_ERR_STRIDE_MARGIN;
  gi->cls_stride = (gi->gdf_unit_size >> 1) + GDF_ERR_STRIDE_MARGIN;
  gi->err_stride = gi->gdf_unit_size + GDF_ERR_STRIDE_MARGIN;
}

void init_gdf(AV1_COMMON *cm) {
  GdfInfo *gi = &cm->gdf_info;
  gi->gdf_mode = 0;
  gi->gdf_pic_qp_idx = 0;
  gi->gdf_pic_scale_idx = 0;
  gi->gdf_block_size = AOMMAX(cm->mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
  const int num_tile_rows = cm->tiles.rows;
  const int num_tile_cols = cm->tiles.cols;

  // if super_block size is 64x64
  if (cm->mib_size == 16) {
    int e2 = 0;
    for (int i = 0; i < cm->tiles.cols - 1; ++i) {
      const int size =
          cm->tiles.col_start_sb[i + 1] - cm->tiles.col_start_sb[i];
      e2 += size & 1;
    }
    for (int i = 0; i < cm->tiles.rows - 1; ++i) {
      const int size =
          cm->tiles.row_start_sb[i + 1] - cm->tiles.row_start_sb[i];
      e2 += size & 1;
    }
    if (e2) gi->gdf_block_size = 64;
  }

  gi->gdf_stripe_size = GDF_TEST_STRIPE_SIZE;
  gi->gdf_unit_size = GDF_TEST_STRIPE_SIZE;
  // Calculate number of blocks
  gi->gdf_block_num_h = 0;
  gi->gdf_block_num_w = 0;
  if (num_tile_rows == 1 && num_tile_cols == 1) {
    AV1PixelRect tile_rect = av1_whole_frame_rect(cm, 0);
    const int tile_height = tile_rect.bottom - tile_rect.top;
    const int tile_width = tile_rect.right - tile_rect.left;
    gi->gdf_vert_blks_per_tile[0] =
        1 + ((tile_height - 1) / gi->gdf_block_size);
    gi->gdf_block_num_h += gi->gdf_vert_blks_per_tile[0];
    gi->gdf_horz_blks_per_tile[0] = 1 + ((tile_width - 1) / gi->gdf_block_size);
    gi->gdf_block_num_w += gi->gdf_horz_blks_per_tile[0];
    gi->gdf_vert_stripes_per_tile[0] =
        gdf_num_stripes_in_tile(gi->gdf_stripe_size, tile_height);
  } else {
    for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
      TileInfo tile_info;
      av1_tile_init(&tile_info, cm, tile_row, 0);
      AV1PixelRect tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
      const int tile_height = tile_rect.bottom - tile_rect.top;
      gi->gdf_vert_blks_per_tile[tile_row] =
          1 + ((tile_height - 1) / gi->gdf_block_size);
      gi->gdf_block_num_h += gi->gdf_vert_blks_per_tile[tile_row];
      gi->gdf_vert_stripes_per_tile[tile_row] =
          gdf_num_stripes_in_tile(gi->gdf_stripe_size, tile_height);
    }
    for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
      TileInfo tile_info;
      av1_tile_init(&tile_info, cm, 0, tile_col);
      AV1PixelRect tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
      const int tile_width = tile_rect.right - tile_rect.left;
      gi->gdf_horz_blks_per_tile[tile_col] =
          1 + ((tile_width - 1) / gi->gdf_block_size);
      gi->gdf_block_num_w += gi->gdf_horz_blks_per_tile[tile_col];
    }
  }
  gi->gdf_block_num = gi->gdf_block_num_h * gi->gdf_block_num_w;
  gi->err_height = gi->gdf_unit_size;
  gi->lap_stride = gi->gdf_unit_size + GDF_ERR_STRIDE_MARGIN;
  gi->cls_stride = (gi->gdf_unit_size >> 1) + GDF_ERR_STRIDE_MARGIN;
  gi->err_stride = gi->gdf_unit_size + GDF_ERR_STRIDE_MARGIN;
}

void alloc_gdf_buffers(GdfInfo *gi) {
  free_gdf_buffers(gi);
  gi->lap_ptr =
      (uint16_t **)aom_malloc(GDF_NET_INP_GRD_NUM * sizeof(uint16_t *));
  const int lap_buf_height = (gi->err_height >> 1) + 2;
  const int cls_buf_height = (gi->err_height >> 1) + 2;
  for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) {
    gi->lap_ptr[i] = (uint16_t *)aom_memalign(
        32, lap_buf_height * gi->lap_stride * sizeof(uint16_t));
    memset(gi->lap_ptr[i], 0,
           lap_buf_height * gi->lap_stride * sizeof(uint16_t));
  }
  gi->cls_ptr = (uint32_t *)aom_memalign(
      32, cls_buf_height * gi->cls_stride * sizeof(uint32_t));
  memset(gi->cls_ptr, 0, cls_buf_height * gi->cls_stride * sizeof(uint32_t));
  gi->err_ptr = (int16_t *)aom_memalign(
      32, gi->err_height * gi->err_stride * sizeof(int16_t));
  memset(gi->err_ptr, 0, gi->err_height * gi->err_stride * sizeof(int16_t));
  gi->gdf_block_flags = (int32_t *)aom_malloc(gi->gdf_block_num * sizeof(int));
  memset(gi->gdf_block_flags, 0, gi->gdf_block_num * sizeof(int));
  gi->glbs = (GDFLineBuffers *)aom_malloc(sizeof(GDFLineBuffers));
  gi->tmp_save_left = (uint16_t *)aom_malloc(
      (gi->gdf_unit_size + 2 * GDF_TEST_EXTRA_VER_BORDER) *
      GDF_TEST_EXTRA_HOR_BORDER * sizeof(*gi->tmp_save_left));
  gi->tmp_save_right = (uint16_t *)aom_malloc(
      (gi->gdf_unit_size + 2 * GDF_TEST_EXTRA_VER_BORDER) *
      GDF_TEST_EXTRA_HOR_BORDER * sizeof(*gi->tmp_save_right));
}

void free_gdf_buffers(GdfInfo *gi) {
  if (gi->lap_ptr != NULL) {
    for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) {
      aom_free(gi->lap_ptr[i]);
      gi->lap_ptr[i] = NULL;
    }
    aom_free(gi->lap_ptr);
    gi->lap_ptr = NULL;
  }
  if (gi->cls_ptr != NULL) {
    aom_free(gi->cls_ptr);
    gi->cls_ptr = NULL;
  }
  if (gi->err_ptr != NULL) {
    aom_free(gi->err_ptr);
    gi->err_ptr = NULL;
  }
  if (gi->gdf_block_flags != NULL) {
    aom_free(gi->gdf_block_flags);
    gi->gdf_block_flags = NULL;
  }
  if (gi->glbs != NULL) {
    aom_free(gi->glbs);
    gi->glbs = NULL;
  }
  if (gi->tmp_save_left != NULL) {
    aom_free(gi->tmp_save_left);
    gi->tmp_save_left = NULL;
  }
  if (gi->tmp_save_right != NULL) {
    aom_free(gi->tmp_save_right);
    gi->tmp_save_right = NULL;
  }
}

#define GDF_PRINT_INT(x) printf(#x " : %d\n", x)

void gdf_print_info(AV1_COMMON *cm, char *info, int poc) {
  printf("=================GDF %s info=================\n", info);

  GDF_PRINT_INT(cm->cur_frame->buf.y_width);
  GDF_PRINT_INT(cm->cur_frame->buf.y_height);
  GDF_PRINT_INT(cm->cur_frame->buf.y_stride);
  GDF_PRINT_INT(cm->cur_frame->buf.bit_depth);
  GDF_PRINT_INT(cm->quant_params.base_qindex);
  GDF_PRINT_INT(cm->ref_frames_info.ref_frame_distance[0]);
  GDF_PRINT_INT(cm->ref_frames_info.ref_frame_distance[1]);
  GDF_PRINT_INT(cm->current_frame.frame_type);
  GDF_PRINT_INT(cm->tiles.height);
  GDF_PRINT_INT(cm->tiles.width);
  GDF_PRINT_INT(cm->mib_size);

  printf("%s[%3d]: gdf_info = [ flag = %d ", info, poc, cm->gdf_info.gdf_mode);
  if (cm->gdf_info.gdf_mode > 0) {
    printf("=> (qp_idx, scale_idx) = (%3d %3d) ", cm->gdf_info.gdf_pic_qp_idx,
           cm->gdf_info.gdf_pic_scale_idx);
  }
  if (cm->gdf_info.gdf_mode > 1) {
    printf("(");
    for (int blk_idx = 0; blk_idx < cm->gdf_info.gdf_block_num; blk_idx++) {
      printf(" %d", cm->gdf_info.gdf_block_flags[blk_idx]);
    }
    printf(")");
  }
  printf(" ]\n");
}
#undef GDF_PRINT_INT

void gdf_extend_frame_highbd(uint16_t *data, int width, int height, int stride,
                             int border_horz, int border_vert) {
  uint16_t *data_p;
  int i, j;
  for (i = 0; i < height; ++i) {
    data_p = data + i * stride;
    for (j = -border_horz; j < 0; ++j) data_p[j] = data_p[0];
    for (j = width; j < width + border_horz; ++j) data_p[j] = data_p[width - 1];
  }
  data_p = data - border_horz;
  for (i = -border_vert; i < 0; ++i) {
    memcpy(data_p + i * stride, data_p,
           (width + 2 * border_horz) * sizeof(uint16_t));
  }
  for (i = height; i < height + border_vert; ++i) {
    memcpy(data_p + i * stride, data_p + (height - 1) * stride,
           (width + 2 * border_horz) * sizeof(uint16_t));
  }
}

void gdf_copy_guided_frame(AV1_COMMON *cm) {
  int top_buf = GDF_TEST_EXTRA_VER_BORDER;
  int bot_buf = GDF_TEST_EXTRA_VER_BORDER;
  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_width = cm->cur_frame->buf.y_width;
  const int rec_stride = cm->cur_frame->buf.y_stride;

  const int input_stride = (((rec_width + GDF_TEST_STRIPE_SIZE) >> 4) << 4) +
                           16;  // GDF_TEST_STRIPE_SIZE: max unit size
                                // 16: AVX2 vector length
  cm->gdf_info.inp_stride = input_stride;

  cm->gdf_info.inp_pad_ptr =
      (uint16_t *)aom_memalign(32, (top_buf + rec_height + bot_buf + 4) *
                                       input_stride * sizeof(uint16_t));
  for (int i = top_buf; i < top_buf + rec_height; i++) {
    memcpy(
        cm->gdf_info.inp_pad_ptr + i * input_stride + GDF_TEST_EXTRA_HOR_BORDER,
        cm->cur_frame->buf.buffers[AOM_PLANE_Y] + (i - top_buf) * rec_stride,
        sizeof(uint16_t) * rec_width);
    if (cm->cur_frame->buf.bit_depth > GDF_TEST_INP_PREC) {
      const unsigned int diff_bit_depth =
          cm->cur_frame->buf.bit_depth - GDF_TEST_INP_PREC;
      uint16_t *cur_line = cm->gdf_info.inp_pad_ptr + i * input_stride +
                           GDF_TEST_EXTRA_HOR_BORDER;
      for (int j = 0; j < rec_width; j++) {
        cur_line[j] >>= diff_bit_depth;
      }
    }
  }
  cm->gdf_info.inp_ptr = cm->gdf_info.inp_pad_ptr + top_buf * input_stride +
                         GDF_TEST_EXTRA_HOR_BORDER;
  gdf_extend_frame_highbd(cm->gdf_info.inp_ptr, rec_width, rec_height,
                          input_stride, GDF_TEST_EXTRA_HOR_BORDER,
                          GDF_TEST_EXTRA_VER_BORDER);
}

void gdf_setup_processing_stripe_leftright_boundary(GdfInfo *gdf, int i_min,
                                                    int i_max, int j_min,
                                                    int j_max,
                                                    int tile_boundary_left,
                                                    int tile_boundary_right) {
  const int data_stride = gdf->inp_stride;
  const int h = i_max - i_min;
  const int w = j_max - j_min;
  const int h_border = GDF_TEST_EXTRA_HOR_BORDER;
  const int v_border = GDF_TEST_EXTRA_VER_BORDER;
  const int stride = GDF_TEST_EXTRA_HOR_BORDER;
  assert(h <= RESTORATION_PROC_UNIT_SIZE);
  uint16_t *data_tl = gdf->inp_ptr + i_min * data_stride + j_min;
  if (tile_boundary_left) {
    uint16_t *d = data_tl - v_border * data_stride - h_border;
    for (int i = 0; i < v_border; ++i) {
      memcpy(gdf->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
    for (int i = v_border; i < h + v_border; ++i) {
      memcpy(gdf->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
    for (int i = h + v_border; i < h + 2 * v_border; ++i) {
      memcpy(gdf->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = data_tl + w - v_border * data_stride;
    for (int i = 0; i < v_border; ++i) {
      memcpy(gdf->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
    for (int i = v_border; i < h + v_border; ++i) {
      memcpy(gdf->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
    for (int i = h + v_border; i < h + 2 * v_border; ++i) {
      memcpy(gdf->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      aom_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
  }
}

void gdf_restore_processing_stripe_leftright_boundary(GdfInfo *gdf, int i_min,
                                                      int i_max, int j_min,
                                                      int j_max,
                                                      int tile_boundary_left,
                                                      int tile_boundary_right) {
  const int data_stride = gdf->inp_stride;
  const int h = i_max - i_min;
  const int w = j_max - j_min;
  const int h_border = GDF_TEST_EXTRA_HOR_BORDER;
  const int v_border = GDF_TEST_EXTRA_VER_BORDER;
  const int stride = GDF_TEST_EXTRA_HOR_BORDER;
  assert(h <= RESTORATION_PROC_UNIT_SIZE);
  uint16_t *data_tl = gdf->inp_ptr + i_min * data_stride + j_min;
  if (tile_boundary_left) {
    uint16_t *d = data_tl - v_border * data_stride - h_border;
    for (int i = 0; i < h + 2 * v_border; ++i) {
      memcpy(d + i * data_stride, gdf->tmp_save_left + i * stride,
             h_border * sizeof(*d));
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = data_tl + w - v_border * data_stride;
    for (int i = 0; i < h + 2 * v_border; ++i) {
      memcpy(d + i * data_stride, gdf->tmp_save_right + i * stride,
             h_border * sizeof(*d));
    }
  }
}

void gdf_setup_reference_lines(AV1_COMMON *cm, int i_min, int i_max,
                               int frame_stripe, int copy_above,
                               int copy_below) {
  const RestorationStripeBoundaries *rsb = &cm->rst_info[0].boundaries;
  const int rsb_row = frame_stripe * RESTORATION_CTX_VERT;

  const int rec_width = cm->cur_frame->buf.y_width;
  const int buf_x0_off = RESTORATION_BORDER_HORZ;
  const int buf_stride = rsb->stripe_boundary_stride;
  const int data_stride = cm->gdf_info.inp_stride;
  const int line_size = rec_width << 1;

  if (copy_above) {
    uint16_t *data_tl = cm->gdf_info.inp_ptr + i_min * data_stride;
    for (int i = -GDF_TEST_EXTRA_VER_BORDER; i < 0; ++i) {
      const int buf_row = rsb_row + AOMMAX(i + RESTORATION_CTX_VERT, 0);
      const int buf_off = buf_x0_off + buf_row * buf_stride;
      const uint16_t *buf = rsb->stripe_boundary_above + buf_off;
      uint16_t *dst = data_tl + i * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_above
      memcpy(cm->gdf_info.glbs->gdf_save_above[i + GDF_TEST_EXTRA_VER_BORDER],
             dst - GDF_TEST_EXTRA_HOR_BORDER,
             line_size +
                 4 * GDF_TEST_EXTRA_HOR_BORDER);  // (sizeof(int16_t) * width +
                                                  // sizeof(int16_t) * 2 *
                                                  // GDF_TEST_EXTRA_HOR_BORDER
      memcpy(dst, buf, line_size);
      if (cm->cur_frame->buf.bit_depth > GDF_TEST_INP_PREC) {
        const unsigned int diff_bit_depth =
            cm->cur_frame->buf.bit_depth - GDF_TEST_INP_PREC;
        uint16_t *cur_line = dst;
        for (int j = 0; j < rec_width; j++) {
          cur_line[j] >>= diff_bit_depth;
        }
      }
      gdf_extend_frame_highbd(dst, rec_width, 1, data_stride,
                              GDF_TEST_EXTRA_HOR_BORDER, 0);
    }
  }
  if (copy_below) {
    uint16_t *data_bl = cm->gdf_info.inp_ptr + i_max * data_stride;
    for (int i = 0; i < GDF_TEST_EXTRA_VER_BORDER; ++i) {
      const int buf_row = rsb_row + AOMMIN(i, RESTORATION_CTX_VERT - 1);
      const int buf_off = buf_x0_off + buf_row * buf_stride;
      const uint16_t *src = rsb->stripe_boundary_below + buf_off;
      uint16_t *dst = data_bl + i * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_below
      memcpy(cm->gdf_info.glbs->gdf_save_below[i],
             dst - GDF_TEST_EXTRA_HOR_BORDER,
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
      memcpy(dst, src, line_size);
      if (cm->cur_frame->buf.bit_depth > GDF_TEST_INP_PREC) {
        const unsigned int diff_bit_depth =
            cm->cur_frame->buf.bit_depth - GDF_TEST_INP_PREC;
        uint16_t *cur_line = dst;
        for (int j = 0; j < rec_width; j++) {
          cur_line[j] >>= diff_bit_depth;
        }
      }
      gdf_extend_frame_highbd(dst, rec_width, 1, data_stride,
                              GDF_TEST_EXTRA_HOR_BORDER, 0);
    }
  }
}

void gdf_unset_reference_lines(AV1_COMMON *cm, int i_min, int i_max,
                               int copy_above, int copy_below) {
  const int rec_width = cm->cur_frame->buf.y_width;
  const int data_stride = cm->gdf_info.inp_stride;
  const int line_size = rec_width << 1;

  if (copy_above) {
    uint16_t *data_tl = cm->gdf_info.inp_ptr + i_min * data_stride;
    for (int i = -GDF_TEST_EXTRA_VER_BORDER; i < 0; ++i) {
      uint16_t *dst = data_tl + i * data_stride;
      memcpy(dst - GDF_TEST_EXTRA_HOR_BORDER,
             cm->gdf_info.glbs->gdf_save_above[i + GDF_TEST_EXTRA_VER_BORDER],
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
    }
  }

  if (copy_below) {
    uint16_t *data_bl = cm->gdf_info.inp_ptr + i_max * data_stride;
    for (int i = 0; i < GDF_TEST_EXTRA_VER_BORDER; ++i) {
      uint16_t *dst = data_bl + i * data_stride;
      memcpy(dst - GDF_TEST_EXTRA_HOR_BORDER,
             cm->gdf_info.glbs->gdf_save_below[i],
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
    }
  }
}

void gdf_free_guided_frame(AV1_COMMON *cm) {
  aom_free(cm->gdf_info.inp_pad_ptr);
}

int gdf_get_block_idx(const AV1_COMMON *cm, int y_h, int y_w) {
  int blk_idx = -1;
  if ((y_h % cm->gdf_info.gdf_block_size == 0) &&
      (y_w % cm->gdf_info.gdf_block_size == 0)) {
    int blk_idx_h = y_h / cm->gdf_info.gdf_block_size;
    int blk_idx_w = y_w / cm->gdf_info.gdf_block_size;
    blk_idx = blk_idx_h * cm->gdf_info.gdf_block_num_w + blk_idx_w;
  }
  blk_idx = blk_idx < cm->gdf_info.gdf_block_num ? blk_idx : -1;
  return blk_idx;
}

static INLINE int get_ref_dst_max(const AV1_COMMON *const cm) {
  int ref_dst_max = 0;
  for (int i = 0; i < cm->ref_frames_info.num_future_refs; i++) {
    const int ref = cm->ref_frames_info.future_refs[i];
    if ((ref == 0 || ref == 1) && get_ref_frame_buf(cm, ref) != NULL) {
      ref_dst_max =
          AOMMAX(ref_dst_max, abs(cm->ref_frames_info.ref_frame_distance[ref]));
    }
  }
  for (int i = 0; i < cm->ref_frames_info.num_past_refs; i++) {
    const int ref = cm->ref_frames_info.past_refs[i];
    if ((ref == 0 || ref == 1) && get_ref_frame_buf(cm, ref) != NULL) {
      ref_dst_max =
          AOMMAX(ref_dst_max, abs(cm->ref_frames_info.ref_frame_distance[ref]));
    }
  }

  return ref_dst_max > 0 ? ref_dst_max : INT_MAX;
}

int gdf_get_ref_dst_idx(const AV1_COMMON *cm) {
  int ref_dst_idx = 0;
  if (frame_is_intra_only(cm)) return ref_dst_idx;

  int ref_dst_max = get_ref_dst_max(cm);
  if (ref_dst_max < 2)
    ref_dst_idx = 1;
  else if (ref_dst_max < 3)
    ref_dst_idx = 2;
  else if (ref_dst_max < 6)
    ref_dst_idx = 3;
  else if (ref_dst_max < 11)
    ref_dst_idx = 4;
  else
    ref_dst_idx = 5;
  return ref_dst_idx;
}

int gdf_get_qp_idx_base(const AV1_COMMON *cm) {
  const int is_intra = frame_is_intra_only(cm);
  const int bit_depth = cm->cur_frame->buf.bit_depth;
  int qp_base = is_intra ? 85 : 110;
  int qp_offset = 24 * (bit_depth - 8);
  int qp = cm->quant_params.base_qindex;
  int qp_idx_avg, qp_idx_base;
  if (qp < (qp_base + 12 + qp_offset))
    qp_idx_avg = 0;
  else if (qp < (qp_base + 37 + qp_offset))
    qp_idx_avg = 1;
  else if (qp < (qp_base + 62 + qp_offset))
    qp_idx_avg = 2;
  else if (qp < (qp_base + 87 + qp_offset))
    qp_idx_avg = 3;
  else if (qp < (qp_base + 112 + qp_offset))
    qp_idx_avg = 4;
  else
    qp_idx_avg = 5;
  qp_idx_base = CLIP(qp_idx_avg - (GDF_RDO_QP_NUM >> 1), 0,
                     GDF_TRAIN_QP_NUM - GDF_RDO_QP_NUM);
  return qp_idx_base;
}

void gdf_filter_frame(AV1_COMMON *cm) {
  uint16_t *const rec_pnt = cm->cur_frame->buf.buffers[AOM_PLANE_Y];
  const int rec_stride = cm->cur_frame->buf.y_stride;

  if (cm->bru.frame_inactive_flag) return;
  if (cm->bridge_frame_info.is_bridge_frame) return;
  const int bit_depth = cm->cur_frame->buf.bit_depth;
  const int pxl_max = (1 << cm->cur_frame->buf.bit_depth) - 1;
  const int pxl_shift =
      GDF_TEST_INP_PREC - AOMMIN(bit_depth, GDF_TEST_INP_PREC);
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + GDF_TEST_INP_PREC - bit_depth;
  int ref_dst_idx = gdf_get_ref_dst_idx(cm);
  int qp_idx_min = gdf_get_qp_idx_base(cm) + cm->gdf_info.gdf_pic_qp_idx;
  int qp_idx_max_plus_1 = qp_idx_min + 1;
  int scale_val = cm->gdf_info.gdf_pic_scale_idx + 1;

  const int num_tile_rows = cm->tiles.rows;
  const int num_tile_cols = cm->tiles.cols;
  int blk_idx = 0;
  int tile_blk_stripe0 = 0;
  for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
    TileInfo tile_info;
    av1_tile_init(&tile_info, cm, tile_row, 0);
    AV1PixelRect tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
    const int tile_height = tile_rect.bottom - tile_rect.top;
    for (int y_pos = -GDF_TEST_STRIPE_OFF, blk_idx_h = 0; y_pos < tile_height;
         y_pos += cm->gdf_info.gdf_block_size, blk_idx_h++) {
      if (blk_idx_h == cm->gdf_info.gdf_vert_blks_per_tile[tile_row]) {
        blk_idx -= cm->gdf_info.gdf_block_num_w;
      }
      int blk_stripe = 0;
      for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
        av1_tile_init(&tile_info, cm, tile_row, tile_col);
        tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
        const int tile_width = tile_rect.right - tile_rect.left;
        for (int x_pos = 0; x_pos < tile_width;
             x_pos += cm->gdf_info.gdf_block_size) {
          blk_stripe = 0;
          for (int v_pos = y_pos; v_pos < y_pos + cm->gdf_info.gdf_block_size &&
                                  v_pos < tile_height;
               v_pos += cm->gdf_info.gdf_unit_size) {
            int i_min =
                AOMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE) + tile_rect.top;
            int i_max = AOMMIN(v_pos + cm->gdf_info.gdf_unit_size,
                               tile_height - GDF_TEST_FRAME_BOUNDARY_SIZE) +
                        tile_rect.top;

            int copy_above = 1, copy_below = 1;
            if (cm->seq_params.disable_loopfilters_across_tiles == 0) {
              // tile top but not picture top
              if (v_pos == -GDF_TEST_STRIPE_OFF && tile_row != 0)
                copy_above = 0;
              // tile bottom but not picture bottom
              if (v_pos + cm->gdf_info.gdf_unit_size >= tile_height &&
                  tile_row != num_tile_rows - 1)
                copy_below = 0;
            }

            gdf_setup_reference_lines(cm, i_min, i_max,
                                      tile_blk_stripe0 + blk_stripe, copy_above,
                                      copy_below);
            for (int u_pos = x_pos;
                 u_pos < x_pos + cm->gdf_info.gdf_block_size &&
                 u_pos < tile_width;
                 u_pos += cm->gdf_info.gdf_unit_size) {
              int j_min =
                  AOMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE) + tile_rect.left;
              int j_max = AOMMIN(u_pos + cm->gdf_info.gdf_unit_size,
                                 tile_width - GDF_TEST_FRAME_BOUNDARY_SIZE) +
                          tile_rect.left;
              int tile_boundary_left =
                  cm->seq_params.disable_loopfilters_across_tiles
                      ? (j_min == tile_rect.left)
                      : (j_min == 0);
              int tile_boundary_right =
                  cm->seq_params.disable_loopfilters_across_tiles
                      ? (j_max == tile_rect.right)
                      : (j_max == cm->cur_frame->buf.y_width);

              gdf_setup_processing_stripe_leftright_boundary(
                  &cm->gdf_info, i_min, i_max, j_min, j_max, tile_boundary_left,
                  tile_boundary_right);

              int use_gdf_local = 1;
              // FU level skip
              if (cm->bru.enabled) {
                const int mbmi_idx = get_mi_grid_idx(
                    &cm->mi_params,
                    AOMMIN(i_max - 1, (i_min + GDF_TEST_STRIPE_OFF)) >>
                        MI_SIZE_LOG2,
                    j_min >> MI_SIZE_LOG2);
                use_gdf_local =
                    cm->mi_params.mi_grid_base[mbmi_idx]->local_gdf_mode;
              }
              use_gdf_local &=
                  gdf_block_adjust_and_validate(&i_min, &i_max, &j_min, &j_max);
              if ((cm->gdf_info.gdf_mode == 1 ||
                   cm->gdf_info.gdf_block_flags[blk_idx]) &&
                  use_gdf_local) {
                const int bru_blk_skip = !bru_is_sb_active(
                    cm, j_min >> MI_SIZE_LOG2,
                    AOMMIN(i_max - 1, (i_min + GDF_TEST_STRIPE_OFF)) >>
                        MI_SIZE_LOG2);
                if (cm->bru.enabled && bru_blk_skip) {
                  aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                     "GDF on not active SB");
                }
                for (int qp_idx = qp_idx_min; qp_idx < qp_idx_max_plus_1;
                     qp_idx++) {
                  gdf_set_lap_and_cls_unit(
                      i_min, i_max, j_min, j_max,
                      cm->gdf_info.inp_ptr +
                          cm->gdf_info.inp_stride * (i_min - 1) + (j_min - 1),
                      cm->gdf_info.inp_stride, bit_depth, cm->gdf_info.lap_ptr,
                      cm->gdf_info.lap_stride, cm->gdf_info.cls_ptr,
                      cm->gdf_info.cls_stride);
                  gdf_inference_unit(
                      i_min, i_max, j_min, j_max, qp_idx,
                      cm->gdf_info.inp_ptr + cm->gdf_info.inp_stride * i_min +
                          j_min,
                      cm->gdf_info.inp_stride, cm->gdf_info.lap_ptr,
                      cm->gdf_info.lap_stride, cm->gdf_info.cls_ptr,
                      cm->gdf_info.cls_stride, cm->gdf_info.err_ptr,
                      cm->gdf_info.err_stride, pxl_shift, ref_dst_idx);
                  // If there is at-least 1 segment is lossless in a frame, we
                  // have to do 4x4 processing, because minimum lossless block
                  // can be 4x4 size. Although, regardless the value of
                  // cm->features.has_lossless_segment, we can always do 4x4
                  // processing, however, for software optimization purpose we
                  // have used  full block processing for whole lossy frame.
                  if (cm->features.has_lossless_segment) {
                    // 4x4 block level processing
                    int min_b_size = 1 << MI_SIZE_LOG2;
                    for (int i_pos_4x4 = i_min; i_pos_4x4 < i_max;
                         i_pos_4x4 += min_b_size) {
                      for (int j_pos_4x4 = j_min; j_pos_4x4 < j_max;
                           j_pos_4x4 += min_b_size) {
                        // CHECK_LOSSLESS(j_pos_4x4 % 4, " j_pos_4x4 is not
                        // multiple of 4"); CHECK_LOSSLESS(i_pos_4x4 % 4, "
                        // i_pos_4x4 is not multiple of 4");

                        const int mi_idx = get_mi_grid_idx(
                            &cm->mi_params, i_pos_4x4 >> MI_SIZE_LOG2,
                            j_pos_4x4 >> MI_SIZE_LOG2);
                        const int is_lossless =
                            cm->features
                                .lossless_segment[cm->mi_params
                                                      .mi_grid_base[mi_idx]
                                                      ->segment_id];
                        if (!is_lossless) {
                          int height_4x4 =
                              AOMMIN(min_b_size, i_max - i_pos_4x4);
                          int width_4x4 = AOMMIN(min_b_size, j_max - j_pos_4x4);
                          uint16_t *rec_pnt_4x4 =
                              rec_pnt + i_pos_4x4 * rec_stride + j_pos_4x4;
                          int16_t *errPnt =
                              cm->gdf_info.err_ptr +
                              (i_pos_4x4 - i_min) * cm->gdf_info.err_stride +
                              (j_pos_4x4 - j_min);
                          gdf_compensation_unit_c(
                              rec_pnt_4x4, rec_stride, errPnt,
                              cm->gdf_info.err_stride, err_shift, scale_val,
                              pxl_max, height_4x4, width_4x4);
                        }
                      }
                    }
                  } else {
                    gdf_compensation_unit(rec_pnt + i_min * rec_stride + j_min,
                                          rec_stride, cm->gdf_info.err_ptr,
                                          cm->gdf_info.err_stride, err_shift,
                                          scale_val, pxl_max, i_max - i_min,
                                          j_max - j_min);
                  }
                }
              }
              gdf_restore_processing_stripe_leftright_boundary(
                  &cm->gdf_info, i_min, i_max, j_min, j_max, tile_boundary_left,
                  tile_boundary_right);
            }  // u_pos
            gdf_unset_reference_lines(cm, i_min, i_max, copy_above, copy_below);
            blk_stripe++;
          }  // v_pos
          blk_idx++;
        }  // x_pos
      }  // tile_col
      tile_blk_stripe0 += blk_stripe;
    }  // y_pos
  }  // tile_row
}

#endif  // AOM_COMMON_GDF_H_
