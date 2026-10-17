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
#ifndef AVM_COMMON_GDF_H_
#define AVM_COMMON_GDF_H_

#include "av2/common/gdf.h"
#include "av2/common/gdf_block.h"

static int gdf_num_stripes_in_tile(int stripe_size, int tile_size) {
  const int first_stripe_offset = GDF_TEST_STRIPE_OFF;
  return (tile_size + first_stripe_offset + stripe_size - 1) / stripe_size;
}

void init_gdf_test(GdfInfo *gi, int mib_size, int rec_height, int rec_width) {
  gi->gdf_mode = 0;
  gi->gdf_pic_qp_idx = 0;
  gi->gdf_pic_scale_idx = 0;
  gi->gdf_block_size = AVMMAX(mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
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

void init_gdf(AV2_COMMON *cm) {
  GdfInfo *gi = &cm->gdf_info;
  gi->gdf_mode = 0;
  gi->gdf_pic_qp_idx = 0;
  gi->gdf_pic_scale_idx = 0;
  gi->gdf_block_size =
      cm->seq_params.gdf_unit_matches_sb_size
          ? cm->mib_size << MI_SIZE_LOG2
          : AVMMAX(cm->mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
  const int num_tile_rows = cm->tiles.rows;
  const int num_tile_cols = cm->tiles.cols;

  // if super_block size is 64x64
  if (cm->mib_size == 16 && !cm->seq_params.gdf_unit_matches_sb_size) {
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
    AV2PixelRect tile_rect = av2_whole_frame_rect(cm, 0);
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
      av2_tile_init(&tile_info, cm, tile_row, 0);
      AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
      const int tile_height = tile_rect.bottom - tile_rect.top;
      gi->gdf_vert_blks_per_tile[tile_row] =
          1 + ((tile_height - 1) / gi->gdf_block_size);
      gi->gdf_block_num_h += gi->gdf_vert_blks_per_tile[tile_row];
      gi->gdf_vert_stripes_per_tile[tile_row] =
          gdf_num_stripes_in_tile(gi->gdf_stripe_size, tile_height);
    }
    for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
      TileInfo tile_info;
      av2_tile_init(&tile_info, cm, 0, tile_col);
      AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
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
  gi->gdf_block_flags = (int32_t *)avm_malloc(gi->gdf_block_num * sizeof(int));
  memset(gi->gdf_block_flags, 0, gi->gdf_block_num * sizeof(int));
}

void free_gdf_buffers(GdfInfo *gi) {
  if (gi->gdf_block_flags != NULL) {
    avm_free(gi->gdf_block_flags);
    gi->gdf_block_flags = NULL;
  }
}

#define GDF_PRINT_INT(x) printf(#x " : %d\n", x)

void gdf_print_info(AV2_COMMON *cm, char *info, int poc) {
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

void gdf_copy_guided_frame(AV2_COMMON *cm) {
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
      (uint16_t *)avm_memalign(32, (top_buf + rec_height + bot_buf + 4) *
                                       input_stride * sizeof(uint16_t));
  for (int i = top_buf; i < top_buf + rec_height; i++) {
    memcpy(
        cm->gdf_info.inp_pad_ptr + i * input_stride + GDF_TEST_EXTRA_HOR_BORDER,
        cm->cur_frame->buf.buffers[AVM_PLANE_Y] + (i - top_buf) * rec_stride,
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

void gdf_setup_processing_stripe_leftright_boundary(
    const GdfInfo *gdf, GdfStripeBuf *sb, int i_min, int i_max, int j_min,
    int j_max, int tile_boundary_left, int tile_boundary_right) {
  const int data_stride = gdf->inp_stride;
  const int h = i_max - i_min;
  const int w = j_max - j_min;
  const int h_border = GDF_TEST_EXTRA_HOR_BORDER;
  const int v_border = GDF_TEST_EXTRA_VER_BORDER;
  const int stride = GDF_TEST_EXTRA_HOR_BORDER;
  assert(h <= RESTORATION_PROC_UNIT_SIZE);
  uint16_t *data_tl = gdf_stripe_ptr(gdf, sb, i_min, j_min);
  if (tile_boundary_left) {
    uint16_t *d = data_tl - v_border * data_stride - h_border;
    for (int i = 0; i < v_border; ++i) {
      memcpy(sb->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
    for (int i = v_border; i < h + v_border; ++i) {
      memcpy(sb->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
    for (int i = h + v_border; i < h + 2 * v_border; ++i) {
      memcpy(sb->tmp_save_left + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride + h_border),
                   h_border);
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = data_tl + w - v_border * data_stride;
    for (int i = 0; i < v_border; ++i) {
      memcpy(sb->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
    for (int i = v_border; i < h + v_border; ++i) {
      memcpy(sb->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
    for (int i = h + v_border; i < h + 2 * v_border; ++i) {
      memcpy(sb->tmp_save_right + i * stride, d + i * data_stride,
             h_border * sizeof(*d));
      // Replicate
      avm_memset16(d + i * data_stride, *(d + i * data_stride - 1), h_border);
    }
  }
}

void gdf_restore_processing_stripe_leftright_boundary(
    const GdfInfo *gdf, GdfStripeBuf *sb, int i_min, int i_max, int j_min,
    int j_max, int tile_boundary_left, int tile_boundary_right) {
  const int data_stride = gdf->inp_stride;
  const int h = i_max - i_min;
  const int w = j_max - j_min;
  const int h_border = GDF_TEST_EXTRA_HOR_BORDER;
  const int v_border = GDF_TEST_EXTRA_VER_BORDER;
  const int stride = GDF_TEST_EXTRA_HOR_BORDER;
  assert(h <= RESTORATION_PROC_UNIT_SIZE);
  uint16_t *data_tl = gdf_stripe_ptr(gdf, sb, i_min, j_min);
  if (tile_boundary_left) {
    uint16_t *d = data_tl - v_border * data_stride - h_border;
    for (int i = 0; i < h + 2 * v_border; ++i) {
      memcpy(d + i * data_stride, sb->tmp_save_left + i * stride,
             h_border * sizeof(*d));
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = data_tl + w - v_border * data_stride;
    for (int i = 0; i < h + 2 * v_border; ++i) {
      memcpy(d + i * data_stride, sb->tmp_save_right + i * stride,
             h_border * sizeof(*d));
    }
  }
}

// Extra columns copied on each side of a job by gdf_load_stripe(): the filter
// border plus slack for SIMD loads.
#define GDF_STRIPE_COPY_MARGIN 32

void gdf_alloc_stripe_buf(AV2_COMMON *cm, GdfStripeBuf *sb) {
  const GdfInfo *gi = &cm->gdf_info;
  // Room for the stripe, its boundary lines and the same 4 spare rows the
  // guided frame has.
  const int band_rows = gi->gdf_unit_size + 2 * GDF_TEST_EXTRA_VER_BORDER + 4;
  const int lap_buf_height = (gi->err_height >> 1) + 2;
  const int cls_buf_height = (gi->err_height >> 1) + 2;
  const int save_size = (gi->gdf_unit_size + 2 * GDF_TEST_EXTRA_VER_BORDER) *
                        GDF_TEST_EXTRA_HOR_BORDER;
  memset(sb, 0, sizeof(*sb));
  CHECK_MEM_ERROR(
      cm, sb->band,
      (uint16_t *)avm_memalign(
          32, (size_t)band_rows * gi->inp_stride * sizeof(*sb->band)));
  CHECK_MEM_ERROR(
      cm, sb->lap_ptr,
      (uint16_t **)avm_calloc(GDF_NET_INP_GRD_NUM, sizeof(*sb->lap_ptr)));
  for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) {
    CHECK_MEM_ERROR(
        cm, sb->lap_ptr[i],
        (uint16_t *)avm_memalign(
            32, lap_buf_height * gi->lap_stride * sizeof(uint16_t)));
  }
  CHECK_MEM_ERROR(cm, sb->cls_ptr,
                  (uint32_t *)avm_memalign(
                      32, cls_buf_height * gi->cls_stride * sizeof(uint32_t)));
  CHECK_MEM_ERROR(cm, sb->err_ptr,
                  (int16_t *)avm_memalign(
                      32, gi->err_height * gi->err_stride * sizeof(int16_t)));
  CHECK_MEM_ERROR(
      cm, sb->tmp_save_left,
      (uint16_t *)avm_malloc(save_size * sizeof(*sb->tmp_save_left)));
  CHECK_MEM_ERROR(
      cm, sb->tmp_save_right,
      (uint16_t *)avm_malloc(save_size * sizeof(*sb->tmp_save_right)));
  gdf_clear_stripe_buf(cm, sb);
}

void gdf_clear_stripe_buf(const AV2_COMMON *cm, GdfStripeBuf *sb) {
  const GdfInfo *gi = &cm->gdf_info;
  const int lap_buf_height = (gi->err_height >> 1) + 2;
  const int cls_buf_height = (gi->err_height >> 1) + 2;
  for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) {
    memset(sb->lap_ptr[i], 0,
           lap_buf_height * gi->lap_stride * sizeof(uint16_t));
  }
  memset(sb->cls_ptr, 0, cls_buf_height * gi->cls_stride * sizeof(uint32_t));
  memset(sb->err_ptr, 0, gi->err_height * gi->err_stride * sizeof(int16_t));
}

void gdf_free_stripe_buf(GdfStripeBuf *sb) {
  avm_free(sb->band);
  if (sb->lap_ptr != NULL) {
    for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) avm_free(sb->lap_ptr[i]);
    avm_free(sb->lap_ptr);
  }
  avm_free(sb->cls_ptr);
  avm_free(sb->err_ptr);
  avm_free(sb->tmp_save_left);
  avm_free(sb->tmp_save_right);
  memset(sb, 0, sizeof(*sb));
}

// Overwrites the guided frame line at dst with the saved stripe boundary line
// src, then pads it as the guided frame is padded.
static void gdf_put_boundary_line(const AV2_COMMON *cm, uint16_t *dst,
                                  const uint16_t *src) {
  const int rec_width = cm->cur_frame->buf.y_width;
  memcpy(dst, src, rec_width * sizeof(*dst));
  if (cm->cur_frame->buf.bit_depth > GDF_TEST_INP_PREC) {
    const unsigned int diff_bit_depth =
        cm->cur_frame->buf.bit_depth - GDF_TEST_INP_PREC;
    for (int j = 0; j < rec_width; j++) {
      dst[j] >>= diff_bit_depth;
    }
  }
  gdf_extend_frame_highbd(dst, rec_width, 1, cm->gdf_info.inp_stride,
                          GDF_TEST_EXTRA_HOR_BORDER, 0);
}

void gdf_load_stripe(const AV2_COMMON *cm, GdfStripeBuf *sb, int i_min,
                     int i_max, int j_min, int j_max, int frame_stripe,
                     int copy_above, int copy_below) {
  const GdfInfo *gi = &cm->gdf_info;
  const int data_stride = gi->inp_stride;
  const int x0 =
      AVMMAX(j_min - GDF_STRIPE_COPY_MARGIN, -GDF_TEST_EXTRA_HOR_BORDER);
  const int x1 = AVMMIN(j_max + GDF_STRIPE_COPY_MARGIN,
                        data_stride - GDF_TEST_EXTRA_HOR_BORDER);
  sb->band_i0 = i_min - GDF_TEST_EXTRA_VER_BORDER;
  for (int i = sb->band_i0; i < i_max + GDF_TEST_EXTRA_VER_BORDER; ++i) {
    memcpy(gdf_stripe_ptr(gi, sb, i, x0), gi->inp_ptr + i * data_stride + x0,
           (x1 - x0) * sizeof(*sb->band));
  }

  const RestorationStripeBoundaries *rsb = &cm->rst_info[0].boundaries;
  const int rsb_row = frame_stripe * RESTORATION_CTX_VERT;
  const int buf_x0_off = RESTORATION_BORDER_HORZ;
  const int buf_stride = rsb->stripe_boundary_stride;
  if (copy_above) {
    for (int i = -GDF_TEST_EXTRA_VER_BORDER; i < 0; ++i) {
      const int buf_row = rsb_row + AVMMAX(i + RESTORATION_CTX_VERT, 0);
      gdf_put_boundary_line(
          cm, gdf_stripe_ptr(gi, sb, i_min + i, 0),
          rsb->stripe_boundary_above + buf_x0_off + buf_row * buf_stride);
    }
  }
  if (copy_below) {
    for (int i = 0; i < GDF_TEST_EXTRA_VER_BORDER; ++i) {
      const int buf_row = rsb_row + AVMMIN(i, RESTORATION_CTX_VERT - 1);
      gdf_put_boundary_line(
          cm, gdf_stripe_ptr(gi, sb, i_max + i, 0),
          rsb->stripe_boundary_below + buf_x0_off + buf_row * buf_stride);
    }
  }
}

void gdf_free_guided_frame(AV2_COMMON *cm) {
  avm_free(cm->gdf_info.inp_pad_ptr);
}

int gdf_get_block_idx(const AV2_COMMON *cm, int y_h, int y_w) {
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

static INLINE int get_ref_dst_max(const AV2_COMMON *const cm) {
  int ref_dst_max = 0;
  for (int i = 0; i < cm->ref_frames_info.num_future_refs; i++) {
    const int ref = cm->ref_frames_info.future_refs[i];
    if ((ref == 0 || ref == 1) && get_ref_frame_buf(cm, ref) != NULL) {
      ref_dst_max =
          AVMMAX(ref_dst_max, abs(cm->ref_frames_info.ref_frame_distance[ref]));
    }
  }
  for (int i = 0; i < cm->ref_frames_info.num_past_refs; i++) {
    const int ref = cm->ref_frames_info.past_refs[i];
    if ((ref == 0 || ref == 1) && get_ref_frame_buf(cm, ref) != NULL) {
      ref_dst_max =
          AVMMAX(ref_dst_max, abs(cm->ref_frames_info.ref_frame_distance[ref]));
    }
  }

  return ref_dst_max > 0 ? ref_dst_max : INT_MAX;
}

int gdf_get_ref_dst_idx(const AV2_COMMON *cm) {
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

int gdf_get_qp_idx_base(const AV2_COMMON *cm) {
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

int gdf_max_block_rows(const AV2_COMMON *cm) {
  int num_rows = 0;
  for (int tile_row = 0; tile_row < cm->tiles.rows; ++tile_row) {
    TileInfo tile_info;
    av2_tile_init(&tile_info, cm, tile_row, 0);
    const AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
    const int tile_height = tile_rect.bottom - tile_rect.top;
    num_rows +=
        (tile_height + GDF_TEST_STRIPE_OFF + cm->gdf_info.gdf_block_size - 1) /
        cm->gdf_info.gdf_block_size;
  }
  return num_rows;
}

int gdf_get_block_rows(const AV2_COMMON *cm, int skip_inactive_fus,
                       GdfBlockRow *rows) {
  const GdfInfo *gi = &cm->gdf_info;
  const int num_tile_rows = cm->tiles.rows;
  const int num_tile_cols = cm->tiles.cols;
  int num_rows = 0;
  int blk_idx = 0;
  int tile_blk_stripe0 = 0;
  for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
    TileInfo tile_info;
    av2_tile_init(&tile_info, cm, tile_row, 0);
    AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
    const int tile_height = tile_rect.bottom - tile_rect.top;
    for (int y_pos = -GDF_TEST_STRIPE_OFF, blk_idx_h = 0; y_pos < tile_height;
         y_pos += gi->gdf_block_size, blk_idx_h++) {
      if (blk_idx_h == gi->gdf_vert_blks_per_tile[tile_row]) {
        blk_idx -= gi->gdf_block_num_w;
      }
      GdfBlockRow *row = &rows[num_rows++];
      row->tile_row = tile_row;
      row->y_pos = y_pos;
      row->blk_idx0 = blk_idx;
      row->frame_stripe0 = tile_blk_stripe0;

      int num_stripes = 0;
      for (int v_pos = y_pos;
           v_pos < y_pos + gi->gdf_block_size && v_pos < tile_height;
           v_pos += gi->gdf_unit_size) {
        num_stripes++;
      }
      // The stripe count only moves on when some block of the row is
      // processed.
      int any_block = 0;
      for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
        av2_tile_init(&tile_info, cm, tile_row, tile_col);
        tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
        const int tile_width = tile_rect.right - tile_rect.left;
        for (int x_pos = 0; x_pos < tile_width; x_pos += gi->gdf_block_size) {
          if (!skip_inactive_fus ||
              !gdf_search_fu_skipped(cm, &tile_rect, x_pos, y_pos)) {
            any_block = 1;
          }
          blk_idx++;
        }
      }
      if (any_block) tile_blk_stripe0 += num_stripes;
    }
  }
  return num_rows;
}

int gdf_get_jobs(const AV2_COMMON *cm, const GdfBlockRow *rows, int num_rows,
                 int blks_per_job, GdfJob *jobs) {
  const int num_w = cm->gdf_info.gdf_block_num_w;
  int num_jobs = 0;
  for (int r = 0; r < num_rows;) {
    int group = 1;
    while (r + group < num_rows &&
           rows[r + group].blk_idx0 == rows[r].blk_idx0) {
      group++;
    }
    for (int blk = 0; blk < num_w; blk += blks_per_job) {
      GdfJob *job = &jobs[num_jobs++];
      job->row0 = r;
      job->num_rows = group;
      job->blk0 = blk;
      job->blk1 = AVMMIN(blk + blks_per_job, num_w);
    }
    r += group;
  }
  return num_jobs;
}

void gdf_get_job_cols(const AV2_COMMON *cm, const GdfJob *job, int *j_min,
                      int *j_max) {
  int tile_col, x_pos;
  TileInfo tile_info;
  gdf_get_block_col(cm, job->blk0, &tile_col, &x_pos);
  av2_tile_init(&tile_info, cm, 0, tile_col);
  AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
  *j_min = tile_rect.left + x_pos;
  gdf_get_block_col(cm, job->blk1 - 1, &tile_col, &x_pos);
  av2_tile_init(&tile_info, cm, 0, tile_col);
  tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
  *j_max = AVMMIN(tile_rect.left + x_pos + cm->gdf_info.gdf_block_size,
                  tile_rect.right);
}

void gdf_filter_job(AV2_COMMON *cm, GdfStripeBuf *sb, const GdfBlockRow *rows,
                    const GdfJob *job) {
  const GdfInfo *gi = &cm->gdf_info;
  uint16_t *const rec_pnt = cm->cur_frame->buf.buffers[AVM_PLANE_Y];
  const int rec_stride = cm->cur_frame->buf.y_stride;

  const int bit_depth = cm->cur_frame->buf.bit_depth;
  const int pxl_max = (1 << cm->cur_frame->buf.bit_depth) - 1;
  const int pxl_shift =
      GDF_TEST_INP_PREC - AVMMIN(bit_depth, GDF_TEST_INP_PREC);
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + GDF_TEST_INP_PREC - bit_depth;
  int ref_dst_idx = gdf_get_ref_dst_idx(cm);
  int qp_idx_min = gdf_get_qp_idx_base(cm) + gi->gdf_pic_qp_idx;
  int qp_idx_max_plus_1 = qp_idx_min + 1;
  int scale_val = gi->gdf_pic_scale_idx + 1;

  const int num_tile_rows = cm->tiles.rows;
  int job_j_min, job_j_max;
  gdf_get_job_cols(cm, job, &job_j_min, &job_j_max);
  for (int r = job->row0; r < job->row0 + job->num_rows; ++r) {
    const GdfBlockRow *row = &rows[r];
    const int tile_row = row->tile_row;
    TileInfo tile_info;
    av2_tile_init(&tile_info, cm, tile_row, 0);
    const AV2PixelRect row_rect = av2_get_tile_rect(&tile_info, cm, 0);
    const int tile_height = row_rect.bottom - row_rect.top;
    int blk_stripe = 0;
    for (int v_pos = row->y_pos;
         v_pos < row->y_pos + gi->gdf_block_size && v_pos < tile_height;
         v_pos += gi->gdf_unit_size, blk_stripe++) {
      const int stripe_i_min =
          AVMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE) + row_rect.top;
      const int stripe_i_max =
          AVMMIN(v_pos + gi->gdf_unit_size,
                 tile_height - GDF_TEST_FRAME_BOUNDARY_SIZE) +
          row_rect.top;

      int copy_above = 1, copy_below = 1;
      if (cm->seq_params.disable_loopfilters_across_tiles == 0) {
        // tile top but not picture top
        if (v_pos == -GDF_TEST_STRIPE_OFF && tile_row != 0) copy_above = 0;
        // tile bottom but not picture bottom
        if (v_pos + gi->gdf_unit_size >= tile_height &&
            tile_row != num_tile_rows - 1)
          copy_below = 0;
      }

      gdf_load_stripe(cm, sb, stripe_i_min, stripe_i_max, job_j_min, job_j_max,
                      row->frame_stripe0 + blk_stripe, copy_above, copy_below);
      for (int blk = job->blk0; blk < job->blk1; ++blk) {
        const int blk_idx = row->blk_idx0 + blk;
        int tile_col, x_pos;
        gdf_get_block_col(cm, blk, &tile_col, &x_pos);
        av2_tile_init(&tile_info, cm, tile_row, tile_col);
        const AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
        const int tile_width = tile_rect.right - tile_rect.left;
        for (int u_pos = x_pos;
             u_pos < x_pos + gi->gdf_block_size && u_pos < tile_width;
             u_pos += gi->gdf_unit_size) {
          int i_min = stripe_i_min;
          int i_max = stripe_i_max;
          int j_min =
              AVMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE) + tile_rect.left;
          int j_max = AVMMIN(u_pos + gi->gdf_unit_size,
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
              gi, sb, i_min, i_max, j_min, j_max, tile_boundary_left,
              tile_boundary_right);

          int use_gdf_local = 1;
          // FU level skip
          if (cm->bru.enabled) {
            const int mbmi_idx = get_mi_grid_idx(
                &cm->mi_params,
                AVMMIN(i_max - 1, (i_min + GDF_TEST_STRIPE_OFF)) >>
                    MI_SIZE_LOG2,
                j_min >> MI_SIZE_LOG2);
            use_gdf_local =
                cm->mi_params.mi_grid_base[mbmi_idx]->local_gdf_mode;
          }
          use_gdf_local &=
              gdf_block_adjust_and_validate(&i_min, &i_max, &j_min, &j_max);
          if ((gi->gdf_mode == 1 || gi->gdf_block_flags[blk_idx]) &&
              use_gdf_local) {
            const int bru_blk_skip = !bru_is_sb_active(
                cm, j_min >> MI_SIZE_LOG2,
                AVMMIN(i_max - 1, (i_min + GDF_TEST_STRIPE_OFF)) >>
                    MI_SIZE_LOG2);
            if (cm->bru.enabled && bru_blk_skip) {
              avm_internal_error(&cm->error, AVM_CODEC_ERROR,
                                 "GDF on not active SB");
            }
            for (int qp_idx = qp_idx_min; qp_idx < qp_idx_max_plus_1;
                 qp_idx++) {
              gdf_set_lap_and_cls_unit(
                  i_min, i_max, j_min, j_max,
                  gdf_stripe_ptr(gi, sb, i_min - 1, j_min - 1), gi->inp_stride,
                  bit_depth, sb->lap_ptr, gi->lap_stride, sb->cls_ptr,
                  gi->cls_stride);
              gdf_inference_unit(i_min, i_max, j_min, j_max, qp_idx,
                                 gdf_stripe_ptr(gi, sb, i_min, j_min),
                                 gi->inp_stride, sb->lap_ptr, gi->lap_stride,
                                 sb->cls_ptr, gi->cls_stride, sb->err_ptr,
                                 gi->err_stride, pxl_shift, ref_dst_idx);
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
                    const int mi_idx = get_mi_grid_idx(
                        &cm->mi_params, i_pos_4x4 >> MI_SIZE_LOG2,
                        j_pos_4x4 >> MI_SIZE_LOG2);
                    const int is_lossless =
                        cm->features.lossless_segment
                            [cm->mi_params.mi_grid_base[mi_idx]->segment_id];
                    if (!is_lossless) {
                      int height_4x4 = AVMMIN(min_b_size, i_max - i_pos_4x4);
                      int width_4x4 = AVMMIN(min_b_size, j_max - j_pos_4x4);
                      uint16_t *rec_pnt_4x4 =
                          rec_pnt + i_pos_4x4 * rec_stride + j_pos_4x4;
                      int16_t *errPnt = sb->err_ptr +
                                        (i_pos_4x4 - i_min) * gi->err_stride +
                                        (j_pos_4x4 - j_min);
                      gdf_compensation_unit_c(
                          rec_pnt_4x4, rec_stride, errPnt, gi->err_stride,
                          err_shift, scale_val, pxl_max, height_4x4, width_4x4);
                    }
                  }
                }
              } else {
                gdf_compensation_unit(rec_pnt + i_min * rec_stride + j_min,
                                      rec_stride, sb->err_ptr, gi->err_stride,
                                      err_shift, scale_val, pxl_max,
                                      i_max - i_min, j_max - j_min);
              }
            }
          }
          gdf_restore_processing_stripe_leftright_boundary(
              gi, sb, i_min, i_max, j_min, j_max, tile_boundary_left,
              tile_boundary_right);
        }
      }
    }
  }
}

void gdf_filter_frame(AV2_COMMON *cm, GdfStripeBuf *sb, GdfBlockRow *rows,
                      GdfJob *jobs) {
  if (gdf_frame_not_filtered(cm)) return;

  const int num_rows = gdf_get_block_rows(cm, 0, rows);
  // One job per group of block rows, so each stripe is copied only once.
  const int num_jobs =
      gdf_get_jobs(cm, rows, num_rows, cm->gdf_info.gdf_block_num_w, jobs);
  for (int i = 0; i < num_jobs; ++i) {
    gdf_filter_job(cm, sb, rows, &jobs[i]);
  }
}

#endif  // AVM_COMMON_GDF_H_
