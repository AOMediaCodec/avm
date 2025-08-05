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

#if CONFIG_GDF

void init_gdf(GdfInfo *gi, int mib_size, int rec_height, int rec_width) {
  gi->gdf_mode = 0;
  gi->gdf_pic_qp_idx = 0;
  gi->gdf_pic_scale_idx = 0;
  gi->gdf_block_size = AOMMAX(mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
  gi->gdf_block_num_h =
      1 + ((rec_height + GDF_TEST_STRIPE_OFF - 1) / gi->gdf_block_size);
  gi->gdf_block_num_w = 1 + ((rec_width - 1) / gi->gdf_block_size);
  gi->gdf_block_num = gi->gdf_block_num_h * gi->gdf_block_num_w;
  gi->gdf_stripe_size = GDF_TEST_STRIPE_SIZE;
  gi->gdf_unit_size = GDF_TEST_STRIPE_SIZE;
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
#if CONFIG_GDF_IMPROVEMENT
  gi->glbs = (GDFLineBuffers *)aom_malloc(sizeof(GDFLineBuffers));
#endif
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
#if CONFIG_GDF_IMPROVEMENT
  if (gi->glbs != NULL) {
    aom_free(gi->glbs);
    gi->glbs = NULL;
  }
#endif
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

#if CONFIG_GDF_IMPROVEMENT

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
      for (int j = 0; j < rec_width; j++) {
        uint16_t *cur_line = cm->gdf_info.inp_pad_ptr + i * input_stride +
                             GDF_TEST_EXTRA_HOR_BORDER;
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

void gdf_setup_reference_lines(AV1_COMMON *cm, int i_min, int i_max,
                               int v_pos) {
  const RestorationStripeBoundaries *rsb = &cm->rst_info[0].boundaries;
  const int rsb_row =
      ((v_pos + GDF_TEST_STRIPE_OFF) / cm->gdf_info.gdf_unit_size) *
      RESTORATION_CTX_VERT;

  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_width = cm->cur_frame->buf.y_width;
  const int buf_x0_off = RESTORATION_EXTRA_HORZ;
  const int buf_stride = rsb->stripe_boundary_stride;
  const int data_x0_off = 0;
  const int data_stride = cm->gdf_info.inp_stride;
  const int line_size = rec_width << 1;

  int copy_above = v_pos == -GDF_TEST_STRIPE_OFF ? 0 : 1;
  int copy_below = (v_pos + cm->gdf_info.gdf_unit_size) >= rec_height ? 0 : 1;
  if (copy_above) {
    uint16_t *data_tl =
        cm->gdf_info.inp_ptr + i_min * data_stride + data_x0_off;
    for (int i = -RESTORATION_BORDER; i < 0; ++i) {
      const int buf_row = rsb_row + AOMMAX(i + RESTORATION_CTX_VERT, 0);
      const int buf_off = buf_x0_off + buf_row * buf_stride;
      const uint16_t *buf = rsb->stripe_boundary_above + buf_off;
      uint16_t *dst = data_tl + i * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_above
      memcpy(cm->gdf_info.glbs->gdf_save_above[i + RESTORATION_BORDER],
             dst - GDF_TEST_EXTRA_HOR_BORDER,
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
      memcpy(dst, buf, line_size);
      gdf_extend_frame_highbd(dst, rec_width, 1, data_stride,
                              GDF_TEST_EXTRA_HOR_BORDER, 0);
    }
  }
  if (copy_below) {
    uint16_t *data_bl =
        cm->gdf_info.inp_ptr + i_max * data_stride + data_x0_off;
    for (int i = 0; i < RESTORATION_BORDER; ++i) {
      const int buf_row = rsb_row + AOMMIN(i, RESTORATION_CTX_VERT - 1);
      const int buf_off = buf_x0_off + buf_row * buf_stride;
      const uint16_t *src = rsb->stripe_boundary_below + buf_off;
      uint16_t *dst = data_bl + i * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_below
      memcpy(cm->gdf_info.glbs->gdf_save_below[i],
             dst - GDF_TEST_EXTRA_HOR_BORDER,
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
      memcpy(dst, src, line_size);
      gdf_extend_frame_highbd(dst, rec_width, 1, data_stride,
                              GDF_TEST_EXTRA_HOR_BORDER, 0);
    }
  }
}

void gdf_unset_reference_lines(AV1_COMMON *cm, int i_min, int i_max,
                               int v_pos) {
  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_width = cm->cur_frame->buf.y_width;
  const int data_x0_off = 0;
  const int data_stride = cm->gdf_info.inp_stride;
  const int line_size = rec_width << 1;

  int copy_above = v_pos == -GDF_TEST_STRIPE_OFF ? 0 : 1;
  int copy_below = (v_pos + cm->gdf_info.gdf_unit_size) >= rec_height ? 0 : 1;
  if (copy_above) {
    uint16_t *data_tl =
        cm->gdf_info.inp_ptr + i_min * data_stride + data_x0_off;
    for (int i = -RESTORATION_BORDER; i < 0; ++i) {
      uint16_t *dst = data_tl + i * data_stride;
      memcpy(dst - GDF_TEST_EXTRA_HOR_BORDER,
             cm->gdf_info.glbs->gdf_save_above[i + RESTORATION_BORDER],
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
    }
  }

  if (copy_below) {
    uint16_t *data_bl =
        cm->gdf_info.inp_ptr + i_max * data_stride + data_x0_off;
    for (int i = 0; i < RESTORATION_BORDER; ++i) {
      uint16_t *dst = data_bl + i * data_stride;
      memcpy(dst - GDF_TEST_EXTRA_HOR_BORDER,
             cm->gdf_info.glbs->gdf_save_below[i],
             line_size + 4 * GDF_TEST_EXTRA_HOR_BORDER);
    }
  }
}

#else
void gdf_copy_guided_frame(AV1_COMMON *cm) {
  int top_buf = 3, bot_buf = 3;
  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_stride = cm->cur_frame->buf.y_stride;

  cm->gdf_info.inp_pad_ptr = (uint16_t *)aom_memalign(
      32, (top_buf + rec_height + bot_buf) * rec_stride * sizeof(uint16_t));
  for (int i = top_buf; i < top_buf + rec_height; i++) {
    for (int j = 0; j < rec_stride; j++) {
      cm->gdf_info.inp_pad_ptr[i * rec_stride + j] =
          cm->cur_frame->buf
              .buffers[AOM_PLANE_Y][(i - top_buf) * rec_stride + j];
    }
  }
  cm->gdf_info.inp_ptr = cm->gdf_info.inp_pad_ptr + top_buf * rec_stride;
}
#endif

void gdf_free_guided_frame(AV1_COMMON *cm) {
  aom_free(cm->gdf_info.inp_pad_ptr);
}

int gdf_get_block_idx(const AV1_COMMON *cm, int y_h, int y_w) {
  int blk_idx = -1;
  if (((y_h == 0) ||
       ((y_h + GDF_TEST_STRIPE_OFF) % cm->gdf_info.gdf_block_size == 0)) &&
      ((y_w == 0) || (y_w % cm->gdf_info.gdf_block_size == 0))) {
    int blk_idx_h =
        (y_h == 0)
            ? 0
            : ((y_h + GDF_TEST_STRIPE_OFF) / cm->gdf_info.gdf_block_size);
    int blk_idx_w = (y_w == 0) ? 0 : (y_w / cm->gdf_info.gdf_block_size);
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
  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_width = cm->cur_frame->buf.y_width;
  const int rec_stride = cm->cur_frame->buf.y_stride;

#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) return;
#endif
  const int bit_depth = cm->cur_frame->buf.bit_depth;
  const int pxl_max = (1 << cm->cur_frame->buf.bit_depth) - 1;
  const int pxl_shift =
      GDF_TEST_INP_PREC - AOMMIN(bit_depth, GDF_TEST_INP_PREC);
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + GDF_TEST_INP_PREC - bit_depth;

  int ref_dst_idx = gdf_get_ref_dst_idx(cm);
  int qp_idx_min = gdf_get_qp_idx_base(cm) + cm->gdf_info.gdf_pic_qp_idx;
  int qp_idx_max_plus_1 = qp_idx_min + 1;
  int scale_val = cm->gdf_info.gdf_pic_scale_idx + 1;

  int blk_idx = 0;
  for (int y_pos = -GDF_TEST_STRIPE_OFF; y_pos < rec_height;
       y_pos += cm->gdf_info.gdf_block_size) {
    for (int x_pos = 0; x_pos < rec_width;
         x_pos += cm->gdf_info.gdf_block_size) {
      for (int v_pos = y_pos;
           v_pos < y_pos + cm->gdf_info.gdf_block_size && v_pos < rec_height;
           v_pos += cm->gdf_info.gdf_unit_size) {
        for (int u_pos = x_pos;
             u_pos < x_pos + cm->gdf_info.gdf_block_size && u_pos < rec_width;
             u_pos += cm->gdf_info.gdf_unit_size) {
          int i_min = AOMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
          int i_max = AOMMIN(v_pos + cm->gdf_info.gdf_unit_size,
                             rec_height - GDF_TEST_FRAME_BOUNDARY_SIZE);
          int j_min = AOMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
          int j_max = AOMMIN(u_pos + cm->gdf_info.gdf_unit_size,
                             rec_width - GDF_TEST_FRAME_BOUNDARY_SIZE);
#if CONFIG_GDF_IMPROVEMENT && (GDF_TEST_VIRTUAL_BOUNDARY == 2)
          if (u_pos == 0) {
            gdf_setup_reference_lines(cm, i_min, i_max, v_pos);
          }
#endif
          int use_gdf_local = 1;
#if CONFIG_BRU
          // FU level skip
          if (cm->bru.enabled) {
            const int mbmi_idx = get_mi_grid_idx(
                &cm->mi_params, i_min >> MI_SIZE_LOG2, j_min >> MI_SIZE_LOG2);
            use_gdf_local =
                cm->mi_params.mi_grid_base[mbmi_idx]->local_gdf_mode;
          }
#endif
          use_gdf_local &=
              gdf_block_adjust_and_validate(&i_min, &i_max, &j_min, &j_max);
          if ((cm->gdf_info.gdf_mode == 1 ||
               cm->gdf_info.gdf_block_flags[blk_idx]) &&
              use_gdf_local) {
#if CONFIG_BRU
            const int bru_blk_skip = !bru_is_sb_active(
                cm, j_min >> MI_SIZE_LOG2, i_min >> MI_SIZE_LOG2);
            if (cm->bru.enabled && bru_blk_skip) {
              aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                 "GDF on not active SB");
            }
#endif
            for (int qp_idx = qp_idx_min; qp_idx < qp_idx_max_plus_1;
                 qp_idx++) {
#if CONFIG_GDF_IMPROVEMENT
              gdf_set_lap_and_cls_unit(
                  i_min, i_max, j_min, j_max, cm->gdf_info.gdf_stripe_size,
                  cm->gdf_info.inp_ptr + cm->gdf_info.inp_stride * i_min +
                      j_min,
                  cm->gdf_info.inp_stride, bit_depth, cm->gdf_info.lap_ptr,
                  cm->gdf_info.lap_stride, cm->gdf_info.cls_ptr,
                  cm->gdf_info.cls_stride);
              gdf_inference_unit(i_min, i_max, j_min, j_max,
                                 cm->gdf_info.gdf_stripe_size, qp_idx,
                                 cm->gdf_info.inp_ptr +
                                     cm->gdf_info.inp_stride * i_min + j_min,
                                 cm->gdf_info.inp_stride, cm->gdf_info.lap_ptr,
                                 cm->gdf_info.lap_stride, cm->gdf_info.cls_ptr,
                                 cm->gdf_info.cls_stride, cm->gdf_info.err_ptr,
                                 cm->gdf_info.err_stride, pxl_shift,
                                 ref_dst_idx);
#else
              gdf_set_lap_and_cls_unit(
                  i_min, i_max, j_min, j_max, cm->gdf_info.gdf_stripe_size,
                  cm->gdf_info.inp_ptr + rec_stride * i_min + j_min, rec_stride,
                  bit_depth, cm->gdf_info.lap_ptr, cm->gdf_info.lap_stride,
                  cm->gdf_info.cls_ptr, cm->gdf_info.cls_stride);
              gdf_inference_unit(
                  i_min, i_max, j_min, j_max, cm->gdf_info.gdf_stripe_size,
                  qp_idx, cm->gdf_info.inp_ptr + rec_stride * i_min + j_min,
                  rec_stride, cm->gdf_info.lap_ptr, cm->gdf_info.lap_stride,
                  cm->gdf_info.cls_ptr, cm->gdf_info.cls_stride,
                  cm->gdf_info.err_ptr, cm->gdf_info.err_stride, pxl_shift,
                  ref_dst_idx);
#endif
              gdf_compensation_unit(
                  rec_pnt + i_min * rec_stride + j_min, rec_stride,
                  cm->gdf_info.err_ptr, cm->gdf_info.err_stride, err_shift,
                  scale_val, pxl_max, i_max - i_min, j_max - j_min);
            }
          }
#if CONFIG_GDF_IMPROVEMENT && (GDF_TEST_VIRTUAL_BOUNDARY == 2)
          if (u_pos == 0) {
            gdf_unset_reference_lines(cm, i_min, i_max, v_pos);
          }
#endif
        }
      }
      blk_idx++;
    }
  }
}

#endif  // CONFIG_GDF

#endif  // AOM_COMMON_GDF_H_
