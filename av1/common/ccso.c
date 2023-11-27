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
                        const uint8_t quant_step_size, const int inv_quant_step,
                        const int *rec_idx
#if CONFIG_CCSO_EDGE_CLF
                        ,
                        const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
) {
#if CONFIG_CCSO_EDGE_CLF
  if (edge_clf == 0) {
#endif  // CONFIG_CCSO_EDGE_CLF
    for (int i = 0; i < 2; i++) {
      int d = rec_y[rec_idx[i]] - rec_y[0];
      if (d > quant_step_size)
        rec_luma_idx[i] = 2;
      else if (d < inv_quant_step)
        rec_luma_idx[i] = 0;
      else
        rec_luma_idx[i] = 1;
    }
#if CONFIG_CCSO_EDGE_CLF
  } else {  // if (edge_clf == 1)
    for (int i = 0; i < 2; i++) {
      int d = rec_y[rec_idx[i]] - rec_y[0];
      if (d < inv_quant_step)
        rec_luma_idx[i] = 0;
      else
        rec_luma_idx[i] = 1;
    }
  }
#endif  // CONFIG_CCSO_EDGE_CLF
}

/* Derive sample locations for CCSO */
void derive_ccso_sample_pos(int *rec_idx, const int ccso_stride,
                            const uint8_t ext_filter_support) {
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
}

#if CONFIG_CCSO_EXT

void ccso_filter_block_hbd_wo_buf_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y,
    const int pic_width, const int pic_height, int *src_cls,
    const int8_t *offset_buf, const int src_y_stride, const int dst_stride,
    const int y_uv_hscale, const int y_uv_vscale, const int thr,
    const int neg_thr, const int *src_loc, const int max_val,
    const int blk_size, const bool isSingleBand, const uint8_t shift_bits
#if CONFIG_CCSO_EDGE_CLF
    ,
    const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
#if CONFIG_CCSO_BO_ONLY_OPTION
    ,
    const uint8_t ccso_bo_only
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
) {
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
#if CONFIG_CCSO_BO_ONLY_OPTION
      if (!ccso_bo_only) {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        cal_filter_support(src_cls,
                           &src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                  (x_pos << y_uv_hscale)],
                           thr, neg_thr, src_loc
#if CONFIG_CCSO_EDGE_CLF
                           ,
                           edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
        );
#if CONFIG_CCSO_BO_ONLY_OPTION
      } else {
        src_cls[0] = 0;
        src_cls[1] = 0;
      }
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
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
                               const int dst_stride, const uint8_t thr,
                               const uint8_t filter_sup,
                               const uint8_t max_band_log2
#if CONFIG_CCSO_EDGE_CLF
                               ,
                               const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const uint8_t shift_bits = cm->seq_params.bit_depth - max_band_log2;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  int src_cls[2];
  const int neg_thr = thr * -1;
  int src_loc[2];
  derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
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
#if CONFIG_CCSO_BO_ONLY_OPTION
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, false, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
            ,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, false, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
#if CONFIG_CCSO_BO_ONLY_OPTION
            ,
            0
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        );
#if CONFIG_CCSO_BO_ONLY_OPTION
      }
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on luma component when single band is applied */
void ccso_apply_luma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                               const uint16_t *src_y, uint16_t *dst_yuv,
                               const int dst_stride, const uint8_t thr,
                               const uint8_t filter_sup,
                               const uint8_t max_band_log2
#if CONFIG_CCSO_EDGE_CLF
                               ,
                               const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
) {
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
  derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
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
#if CONFIG_CCSO_BO_ONLY_OPTION
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, true, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF

            ,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val, blk_size, true, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
#if CONFIG_CCSO_BO_ONLY_OPTION
            ,
            0
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        );
#if CONFIG_CCSO_BO_ONLY_OPTION
      }
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component when multiple bands are applied */
void ccso_apply_chroma_mb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
                                 const uint8_t thr, const uint8_t filter_sup,
                                 const uint8_t max_band_log2
#if CONFIG_CCSO_EDGE_CLF
                                 ,
                                 const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
) {
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
  derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
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
#if CONFIG_CCSO_BO_ONLY_OPTION
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            false, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
            ,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            false, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
#if CONFIG_CCSO_BO_ONLY_OPTION
            ,
            0
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        );
#if CONFIG_CCSO_BO_ONLY_OPTION
      }
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << (blk_log2 + y_uv_vscale));
  }
}

/* Apply CCSO on chroma component when single bands is applied */
void ccso_apply_chroma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
                                 const uint8_t thr, const uint8_t filter_sup,
                                 const uint8_t max_band_log2
#if CONFIG_CCSO_EDGE_CLF
                                 ,
                                 const int edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
) {
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
  derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
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
#if CONFIG_CCSO_BO_ONLY_OPTION
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            true, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
            ,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val, blk_size,
            true, shift_bits
#if CONFIG_CCSO_EDGE_CLF
            ,
            edge_clf
#endif  // CONFIG_CCSO_EDGE_CLF
#if CONFIG_CCSO_BO_ONLY_OPTION
            ,
            0
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        );
#if CONFIG_CCSO_BO_ONLY_OPTION
      }
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << (blk_log2 + y_uv_vscale));
  }
}
#endif  // CONFIG_CCSO_EXT

#if !CONFIG_CCSO_EXT
/* Apply CCSO for one filtering unit using c code (high bit-depth) */
void ccso_filter_block_hbd_c(
    const uint16_t *temp_rec_y_buf, uint16_t *rec_uv_16, const int x,
    const int y, const int pic_width_c, const int pic_height_c,
    int *rec_luma_idx, const int8_t *offset_buf,
#if CONFIG_CCSO_EXT
    const int shift_bits,
#endif
    const int *ccso_stride_idx, const int *dst_stride_idx,
    const int y_uv_hori_scale, const int y_uv_vert_scale, const int pad_stride,
    const int quant_step_size, const int inv_quant_step, const int *rec_idx,
    const int maxval, const int filter_unit_size) {
  int y_offset;
  int x_offset;
  if (y + filter_unit_size >= pic_height_c)
    y_offset = pic_height_c - y;
  else
    y_offset = filter_unit_size;

  if (x + filter_unit_size >= pic_width_c)
    x_offset = pic_width_c - x;
  else
    x_offset = filter_unit_size;

  for (int y_off = 0; y_off < y_offset; y_off++) {
    for (int x_off = 0; x_off < x_offset; x_off++) {
#if CONFIG_CCSO_EXT
      const int band_num =
          temp_rec_y_buf[((ccso_stride_idx[y_off] << y_uv_vert_scale) +
                          ((x + x_off) << y_uv_hori_scale)) +
                         pad_stride] >>
          shift_bits;
#endif
      cal_filter_support(
          rec_luma_idx,
          &temp_rec_y_buf[((ccso_stride_idx[y_off] << y_uv_vert_scale) +
                           ((x + x_off) << y_uv_hori_scale)) +
                          pad_stride],
          quant_step_size, inv_quant_step, rec_idx);
      const int offset_val = offset_buf[
#if CONFIG_CCSO_EXT
          (band_num << 4) +
#endif
          (rec_luma_idx[0] << 2) + rec_luma_idx[1]];
      rec_uv_16[dst_stride_idx[y_off] + x + x_off] = clamp(
          offset_val + rec_uv_16[dst_stride_idx[y_off] + x + x_off], 0, maxval);
    }
  }
}

/* Apply CCSO for one color component (high bit-depth) */
void apply_ccso_filter_hbd(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                           const uint16_t *temp_rec_y_buf, uint16_t *rec_uv_16,
                           const int dst_stride, const int8_t *filter_offset,
                           const uint8_t quant_step_size,
                           const uint8_t ext_filter_support) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_stride_ext = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int pic_height_c = xd->plane[plane].dst.height;
  const int pic_width_c = xd->plane[plane].dst.width;
  int rec_luma_idx[2];
  int inv_quant_step = quant_step_size * -1;
  int rec_idx[2];
  const int maxval = (1 << cm->seq_params.bit_depth) - 1;
#if CONFIG_CCSO_EXT
  const int shift_bits =
      cm->seq_params.bit_depth - cm->ccso_info.max_band_log2[plane];
#endif
  derive_ccso_sample_pos(rec_idx, ccso_stride_ext, ext_filter_support);

  const int8_t *offset_buf;
  if (filter_offset == NULL) {
#if CONFIG_CCSO_EXT
    offset_buf = cm->ccso_info.filter_offset[plane];
#else
    offset_buf = cm->ccso_info.filter_offset[plane - 1];
#endif
  } else {
    offset_buf = filter_offset;
  }
  const int log2_filter_unit_size =
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int filter_unit_size = 1 << log2_filter_unit_size;
  int ccso_stride_ext_idx[1 << (CCSO_BLK_SIZE + 1)];
  int dst_stride_idx[1 << (CCSO_BLK_SIZE + 1)];
  for (int i = 0; i < filter_unit_size; i++) {
    ccso_stride_ext_idx[i] = ccso_stride_ext * i;
    dst_stride_idx[i] = dst_stride * i;
  }
  const int pad_stride =
      CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  const int y_uv_hori_scale = xd->plane[plane].subsampling_x;
  const int y_uv_vert_scale = xd->plane[plane].subsampling_y;
  for (int y = 0; y < pic_height_c; y += filter_unit_size) {
    for (int x = 0; x < pic_width_c; x += filter_unit_size) {
      if (filter_offset == NULL) {
        const int ccso_blk_idx =
            (filter_unit_size >>
             (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
                (y >> log2_filter_unit_size) * mi_params->mi_stride +
            (filter_unit_size >>
             (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
                (x >> log2_filter_unit_size);

        const bool use_ccso =
            (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
#if CONFIG_CCSO_EXT
            : (plane == 0) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y
                           : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
#else
                         : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
#endif
        if (!use_ccso) continue;
      }

      ccso_filter_block_hbd(temp_rec_y_buf, rec_uv_16, x, y, pic_width_c,
                            pic_height_c, rec_luma_idx, offset_buf,
#if CONFIG_CCSO_EXT
                            shift_bits,
#endif
                            ccso_stride_ext_idx, dst_stride_idx,
                            y_uv_hori_scale, y_uv_vert_scale, pad_stride,
                            quant_step_size, inv_quant_step, rec_idx, maxval,
                            filter_unit_size);
    }
    temp_rec_y_buf +=
        (ccso_stride_ext << (log2_filter_unit_size + y_uv_vert_scale));
    rec_uv_16 += (dst_stride << log2_filter_unit_size);
  }
}
#endif  // !CONFIG_CCSO_EXT

/* Apply CCSO for one frame */
void ccso_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm, MACROBLOCKD *xd,
                uint16_t *ext_rec_y) {
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  const uint8_t quant_sz[4] = { 16, 8, 32, 64 };
#if CONFIG_CCSO_EXT
  for (int plane = 0; plane < num_planes; plane++) {
    const int dst_stride = xd->plane[plane].dst.stride;
    const uint8_t quant_step_size = quant_sz[cm->ccso_info.quant_idx[plane]];
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
          cm->ccso_info.max_band_log2[plane]
#if CONFIG_CCSO_EDGE_CLF
          ,
          cm->ccso_info.edge_clf[plane]
#endif  // CONFIG_CCSO_EDGE_CLF
      );
    }
#else
  for (int plane = 1; plane < 3; plane++) {
    const int dst_stride = xd->plane[plane].dst.stride;
    const uint8_t quant_step_size =
        quant_sz[cm->ccso_info.quant_idx[plane - 1]];
    if (cm->ccso_info.ccso_enable[plane - 1]) {
      apply_ccso_filter_hbd(
          cm, xd, plane, ext_rec_y, &(xd->plane[plane].dst.buf)[0], dst_stride,
          NULL, quant_step_size, cm->ccso_info.ext_filter_support[plane - 1]);
    }
#endif
  }
}
