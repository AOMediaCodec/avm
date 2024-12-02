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
void extend_ccso_border(uint16_t *buf, const int d, MACROBLOCKD *xd) {
  int s = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  uint16_t *p = &buf[d * s + d];
  int h = xd->plane[0].dst.height;
  int w = xd->plane[0].dst.width;
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

/* Derive the quantized index, later it can be used for retriving offset values
 * from the look-up table */
void cal_filter_support(int *rec_luma_idx, const uint16_t *rec_y,
#if CONFIG_CCSO_IMPROVE
                        const int quant_step_size,
#else
                        const uint8_t quant_step_size,
#endif
                        const int inv_quant_step, const int *rec_idx,
                        const int edge_clf) {
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
void derive_ccso_sample_pos(AV1_COMMON *cm, int *rec_idx, const int ccso_stride,
                            const uint8_t ext_filter_support) {
#if CONFIG_CCSO_FT_SHAPE
  // Input sample locations for CCSO
  // 4 2 0 3 5
  // 6 1 x 1 6
  // 5 3 0 2 4
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
  } else if (ext_filter_support == 6) {
    rec_idx[0] = 2;
    rec_idx[1] = -2;
  } else {
    if (cm != NULL) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "wrong ccso filter shape");
    } else {
      printf("wrong ccso filter shape\n");  // unit test case
    }
  }
#else
  // Input sample locations for CCSO
  //         2 1 4
  // 6 o 5 o 3 o 3 o 5 o 6
  //         4 1 2
  if (ext_filter_support == 0) {
    // Sample position 1
    rec_idx[0] = -1 * ccso_stride;
    rec_idx[1] = 1 * ccso_stride;
  } else if (ext_filter_support == 1) {
    // Sample position 2
    rec_idx[0] = -1 * ccso_stride - 1;
    rec_idx[1] = 1 * ccso_stride + 1;
  } else if (ext_filter_support == 2) {
    // Sample position 3
    rec_idx[0] = -1;
    rec_idx[1] = 1;
  } else if (ext_filter_support == 3) {
    // Sample position 4
    rec_idx[0] = 1 * ccso_stride - 1;
    rec_idx[1] = -1 * ccso_stride + 1;
  } else if (ext_filter_support == 4) {
    // Sample position 5
    rec_idx[0] = -3;
    rec_idx[1] = 3;
  } else {  // if(ext_filter_support == 5) {
    // Sample position 6
    rec_idx[0] = -5;
    rec_idx[1] = 5;
  }
#endif
}

void ccso_filter_block_hbd_wo_buf_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y,
    const int pic_width, const int pic_height, int *src_cls,
    const int8_t *offset_buf, const int src_y_stride, const int dst_stride,
    const int y_uv_hscale, const int y_uv_vscale, const int thr,
    const int neg_thr, const int *src_loc, const int max_val,
    const int blk_size, const bool isSingleBand, const uint8_t shift_bits,
    const int edge_clf, const uint8_t ccso_bo_only) {
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
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

/* Apply CCSO on luma component when multiple bands are applied */
void ccso_apply_luma_mb_filter(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                               const uint16_t *src_y, uint16_t *dst_yuv,
                               const int dst_stride,
#if CONFIG_CCSO_IMPROVE
                               const uint16_t thr,
#else
                               const uint8_t thr,
#endif  // CONFIG_CCSO_IMPROVE
                               const uint8_t filter_sup,
                               const uint8_t max_band_log2,
                               const int edge_clf) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const uint8_t shift_bits = cm->seq_params.bit_depth - max_band_log2;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  int src_cls[2];
  const int neg_thr = thr * -1;
  int src_loc[2];
  derive_ccso_sample_pos(cm, src_loc, ccso_ext_stride, filter_sup);
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
      const bool use_ccso = mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y;
      if (!use_ccso) continue;
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, false, shift_bits,
            edge_clf, cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, false, shift_bits,
            edge_clf, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on luma component when single band is applied */
void ccso_apply_luma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                               const uint16_t *src_y, uint16_t *dst_yuv,
                               const int dst_stride,
#if CONFIG_CCSO_IMPROVE
                               const uint16_t thr,
#else
                               const uint8_t thr,
#endif  // CONFIG_CCSO_IMPROVE
                               const uint8_t filter_sup,
                               const uint8_t max_band_log2,
                               const int edge_clf) {
  (void)max_band_log2;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const uint8_t shift_bits = cm->seq_params.bit_depth;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  int src_cls[2];
  const int neg_thr = thr * -1;
  int src_loc[2];
  derive_ccso_sample_pos(cm, src_loc, ccso_ext_stride, filter_sup);
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
      const bool use_ccso = mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y;
      if (!use_ccso) continue;
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, true, shift_bits,
            edge_clf, cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, true, shift_bits,
            edge_clf, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component when multiple bands are applied */
void ccso_apply_chroma_mb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
#if CONFIG_CCSO_IMPROVE
                                 const uint16_t thr,
#else
                                 const uint8_t thr,
#endif  // CONFIG_CCSO_IMPROVE
                                 const uint8_t filter_sup,
                                 const uint8_t max_band_log2,
                                 const int edge_clf) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const uint8_t shift_bits = cm->seq_params.bit_depth - max_band_log2;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  int src_cls[2];
  const int neg_thr = thr * -1;
  int src_loc[2];
  derive_ccso_sample_pos(cm, src_loc, ccso_ext_stride, filter_sup);
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
      const bool use_ccso =
          (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                       : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
      if (!use_ccso) continue;
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            false, shift_bits, edge_clf, cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            false, shift_bits, edge_clf, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << (blk_log2 + y_uv_vscale));
  }
}

/* Apply CCSO on chroma component when single bands is applied */
void ccso_apply_chroma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
#if CONFIG_CCSO_IMPROVE
                                 const uint16_t thr,
#else
                                 const uint8_t thr,
#endif  // CONFIG_CCSO_IMPROVE
                                 const uint8_t filter_sup,
                                 const uint8_t max_band_log2,
                                 const int edge_clf) {
  (void)max_band_log2;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const uint8_t shift_bits = cm->seq_params.bit_depth;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  int src_cls[2];
  const int neg_thr = thr * -1;
  int src_loc[2];
  derive_ccso_sample_pos(cm, src_loc, ccso_ext_stride, filter_sup);
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
      const bool use_ccso =
          (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                       : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
      if (!use_ccso) continue;
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            true, shift_bits, edge_clf, cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            true, shift_bits, edge_clf, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << (blk_log2 + y_uv_vscale));
  }
}

/* Apply CCSO for one frame */
void ccso_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm, MACROBLOCKD *xd,
                uint16_t *ext_rec_y) {
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

#if CONFIG_CCSO_IMPROVE
  const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                    { 32, 16, 64, 128 },
                                    { 48, 24, 96, 192 },
                                    { 64, 32, 128, 256 } };
#else
  const uint8_t quant_sz[4] = { 16, 8, 32, 64 };
#endif  // CONFIG_CCSO_IMPROVE

  for (int plane = 0; plane < num_planes; plane++) {
    const int dst_stride = xd->plane[plane].dst.stride;
#if CONFIG_CCSO_IMPROVE
    const uint16_t quant_step_size = quant_sz[cm->ccso_info.scale_idx[plane]]
                                             [cm->ccso_info.quant_idx[plane]];
#else
    const uint8_t quant_step_size = quant_sz[cm->ccso_info.quant_idx[plane]];
#endif  // CONFIG_CCSO_IMPROVE
    if (cm->ccso_info.ccso_enable[plane]) {
      CCSO_FILTER_FUNC apply_ccso_filter_func =
          cm->ccso_info.max_band_log2[plane]
              ? (plane > 0 ? ccso_apply_chroma_mb_filter
                           : ccso_apply_luma_mb_filter)
              : (plane > 0 ? ccso_apply_chroma_sb_filter
                           : ccso_apply_luma_sb_filter);
      apply_ccso_filter_func(
          cm, xd, plane, ext_rec_y, &(xd->plane[plane].dst.buf)[0], dst_stride,
          quant_step_size, cm->ccso_info.ext_filter_support[plane],
          cm->ccso_info.max_band_log2[plane], cm->ccso_info.edge_clf[plane]);
    }
  }
}

#if CONFIG_CCSO_IMPROVE
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
#endif  // CONFIG_CCSO_IMPROVE