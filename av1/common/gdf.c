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

#include "av1/common/gdf.h"
#include "av1/common/gdf_block.h"

#if CONFIG_GDF

#define GDF_TEST_STRIPE_SIZE \
  64  // GDF_TEST_BLK_SIZE has to be multiple of GDF_TEST_STRIPE_SIZE

void init_gdf(AV1_COMMON *cm) {
  const int rec_height = cm->cur_frame->buf.y_height;
  const int rec_width = cm->cur_frame->buf.y_width;

  cm->gdf_info.gdf_mode = 0;
  cm->gdf_info.gdf_pic_qc_idx = 0;
  cm->gdf_info.gdf_pic_scale_idx = 0;
  cm->gdf_info.gdf_block_size =
      AOMMAX(cm->mib_size << MI_SIZE_LOG2, GDF_TEST_BLK_SIZE);
  const int gdf_block_num_h = 1 + ((rec_height + GDF_TEST_STRIPE_OFF - 1) /
                                   cm->gdf_info.gdf_block_size);
  const int gdf_block_num_w =
      1 + ((rec_width - 1) / cm->gdf_info.gdf_block_size);
  cm->gdf_info.gdf_block_num = gdf_block_num_h * gdf_block_num_w;
  cm->gdf_info.gdf_stripe_size = GDF_TEST_STRIPE_SIZE;
  cm->gdf_info.gdf_unit_size = GDF_TEST_STRIPE_SIZE;
  cm->gdf_info.err_height = cm->gdf_info.gdf_unit_size;
  cm->gdf_info.err_stride = cm->gdf_info.gdf_unit_size + GDF_ERR_STRIDE_MARGIN;
}

void alloc_gdf_buffers(AV1_COMMON *cm) {
  if (cm->gdf_info.err_ptr == NULL || cm->gdf_info.gdf_block_flags == NULL) {
    cm->gdf_info.err_ptr = (int16_t *)aom_memalign(
        32,
        cm->gdf_info.err_height * cm->gdf_info.err_stride * sizeof(int16_t));
    cm->gdf_info.gdf_block_flags =
        (int32_t *)aom_malloc(cm->gdf_info.gdf_block_num * sizeof(int));
  }
  memset(cm->gdf_info.err_ptr, 0, cm->gdf_info.err_height * sizeof(int16_t));
  memset(cm->gdf_info.gdf_block_flags, 0,
         cm->gdf_info.gdf_block_num * sizeof(int));
}

void free_gdf_buffers(AV1_COMMON *cm) {
  if (cm->gdf_info.err_ptr != NULL || cm->gdf_info.gdf_block_flags != NULL) {
    aom_free(cm->gdf_info.err_ptr);
    cm->gdf_info.err_ptr = NULL;
    aom_free(cm->gdf_info.gdf_block_flags);
    cm->gdf_info.gdf_block_flags = NULL;
  }
}

void gdf_print_info(AV1_COMMON *cm, char *info, int poc) {
  printf("%s[%3d]: gdf_info = [ flag = %d ", info, poc, cm->gdf_info.gdf_mode);
  if (cm->gdf_info.gdf_mode > 0) {
    printf("=> (qp_idx, scale_idx) = (%3d %3d) ", cm->gdf_info.gdf_pic_qc_idx,
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

void gdf_free_guided_frame(AV1_COMMON *cm) {
  aom_free(cm->gdf_info.inp_pad_ptr);
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
  const unsigned int bit_depth = cm->cur_frame->buf.bit_depth;
  const int pxl_max = (1 << cm->cur_frame->buf.bit_depth) - 1;
  const int pxl_shift = GDF_TEST_INP_PREC - bit_depth;
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + pxl_shift;

  int ref_dst_idx = gdf_get_ref_dst_idx(cm);
  int qp_idx_min = gdf_get_qp_idx_base(cm) + cm->gdf_info.gdf_pic_qc_idx;
  int qp_idx_max_plus_1 = qp_idx_min + 1;
  int scale_val = cm->gdf_info.gdf_pic_scale_idx + 1;

  int blk_idx = 0;
  for (int y_pos = -GDF_TEST_STRIPE_OFF; y_pos < rec_height;
       y_pos += cm->gdf_info.gdf_block_size) {
    for (int x_pos = 0; x_pos < rec_width;
         x_pos += cm->gdf_info.gdf_block_size) {
#if CONFIG_BRU
      const int bru_blk_skip =
          !bru_is_sb_active(cm, x_pos >> MI_SIZE_LOG2, y_pos >> MI_SIZE_LOG2);
#endif
      for (int v_pos = y_pos; v_pos < y_pos + cm->gdf_info.gdf_block_size;
           v_pos += cm->gdf_info.gdf_unit_size) {
        for (int u_pos = x_pos; u_pos < x_pos + cm->gdf_info.gdf_block_size;
             u_pos += cm->gdf_info.gdf_unit_size) {
          int i_min = AOMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
          int i_max = AOMMIN(v_pos + cm->gdf_info.gdf_unit_size,
                             rec_height - GDF_TEST_FRAME_BOUNDARY_SIZE);
          int j_min = AOMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
          int j_max = AOMMIN(u_pos + cm->gdf_info.gdf_unit_size,
                             rec_width - GDF_TEST_FRAME_BOUNDARY_SIZE);
          if ((cm->gdf_info.gdf_mode == 1 ||
               cm->gdf_info.gdf_block_flags[blk_idx]) &&
              (i_max > i_min) && (j_max > j_min)) {
#if CONFIG_BRU
            if (cm->bru.enabled && bru_blk_skip) {
              aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                 "GDF on not active SB");
            }
#endif
            for (int qp_idx = qp_idx_min; qp_idx < qp_idx_max_plus_1;
                 qp_idx++) {
              gdf_inference_block(i_min, i_max, j_min, j_max,
                                  cm->gdf_info.gdf_stripe_size, qp_idx,
                                  cm->gdf_info.inp_ptr, rec_stride, bit_depth,
                                  cm->gdf_info.err_ptr, cm->gdf_info.err_stride,
                                  pxl_shift, ref_dst_idx);
              gdf_compensation_block(
                  rec_pnt + i_min * rec_stride + j_min, rec_stride,
                  cm->gdf_info.err_ptr, cm->gdf_info.err_stride, err_shift,
                  scale_val, pxl_max, i_max - i_min, j_max - j_min);
            }
          }
        }
      }
      blk_idx++;
    }
  }
}

#endif  // CONFIG_GDF
