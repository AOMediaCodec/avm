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
void derive_ccso_sample_pos(AV1_COMMON *cm, int *rec_idx, const int ccso_stride,
                            const uint8_t ext_filter_support) {
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
}

void ccso_filter_block_hbd_wo_buf_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y,
    const int pic_width, const int pic_height, int *src_cls,
    const int8_t *offset_buf, const int src_y_stride, const int dst_stride,
    const int y_uv_hscale, const int y_uv_vscale, const int thr,
    const int neg_thr, const int *src_loc, const int max_val,
#if CONFIG_CCSO_FU_BUGFIX
    const int blk_size_x, const int blk_size_y,
#else
    const int blk_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
    const bool isSingleBand, const uint8_t shift_bits, const int edge_clf,
    const uint8_t ccso_bo_only) {
#if CONFIG_CCSO_FU_BUGFIX
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
#else
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
#endif  // CONFIG_CCSO_FU_BUGFIX
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
#if CCSO_REFACTORING
                               const int proc_unit_log2,
#endif  // CCSO_REFACTORING
                               const uint16_t thr, const uint8_t filter_sup,
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
#if CONFIG_CCSO_FU_BUGFIX
  assert(plane == 0);  // function must only be called for plane == 0
  const int blk_log2 = CCSO_BLK_SIZE;
#else
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
#endif  // CONFIG_CCSO_FU_BUGFIX
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
#if CCSO_REFACTORING
  int unit_log2 = proc_unit_log2 > blk_log2 ? blk_log2 : proc_unit_log2;
  const int unit_size = 1 << (unit_log2);
#endif  // CCSO_REFACTORING
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
#if CONFIG_CCSO_FU_BUGFIX
      const int ccso_blk_idx =
          (blk_size >> MI_SIZE_LOG2) * (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> MI_SIZE_LOG2) * (x >> blk_log2);
#else
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
#endif  // CONFIG_CCSO_FU_BUGFIX
      const bool use_ccso = mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y;
      if (!use_ccso) continue;
#if CCSO_REFACTORING
      const uint16_t *src_unit_y = src_y;
      uint16_t *dst_unit_yuv = dst_yuv;
      const int y_end = AOMMIN(pic_height - y, blk_size);
      const int x_end = AOMMIN(pic_width - x, blk_size);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size) {
#if CONFIG_BRU
          // FPU level skip
          const int mbmi_idx =
              get_mi_grid_idx(mi_params, (y + unit_y) >> MI_SIZE_LOG2,
                              (x + unit_x) >> MI_SIZE_LOG2);
          const int use_ccso_local =
              mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
          if (!use_ccso_local) {
            continue;
          }
          if (cm->bru.enabled &&
              mi_params->mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            aom_internal_error(
                &cm->error, AOM_CODEC_ERROR,
                "Invalid BRU activity in CCSO: only active SB can be filtered");
            return;
          }
#endif  // CONFIG_BRU
          if (cm->ccso_info.ccso_bo_only[plane]) {
            ccso_filter_block_hbd_wo_buf_c(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, 0, 0, thr, neg_thr, src_loc,
                max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
                unit_size, false, shift_bits, edge_clf,
                cm->ccso_info.ccso_bo_only[plane]);
          } else {
            ccso_filter_block_hbd_wo_buf(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, 0, 0, thr, neg_thr, src_loc,
                max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
                unit_size, false, shift_bits, edge_clf, 0);
          }
        }
        dst_unit_yuv += (dst_stride << unit_log2);
        src_unit_y += (ccso_ext_stride << unit_log2);
      }
#else
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size, false, shift_bits, edge_clf,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size, false, shift_bits, edge_clf, 0);
      }
#endif  // CCSO_REFACTORING
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on luma component when single band is applied */
void ccso_apply_luma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                               const uint16_t *src_y, uint16_t *dst_yuv,
                               const int dst_stride,
#if CCSO_REFACTORING
                               const int proc_unit_log2,
#endif  // CCSO_REFACTORING
                               const uint16_t thr, const uint8_t filter_sup,
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
#if CONFIG_CCSO_FU_BUGFIX
  assert(plane == 0);  // function must only be called for plane == 0
  const int blk_log2 = CCSO_BLK_SIZE;
#else
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
#endif  // CONFIG_CCSO_FU_BUGFIX
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
#if CCSO_REFACTORING
  int unit_log2 = proc_unit_log2 > blk_log2 ? blk_log2 : proc_unit_log2;
  const int unit_size = 1 << (unit_log2);
#endif  // CCSO_REFACTORING
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
#if CONFIG_CCSO_FU_BUGFIX
      const int ccso_blk_idx =
          (blk_size >> MI_SIZE_LOG2) * (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> MI_SIZE_LOG2) * (x >> blk_log2);
#else
      const int ccso_blk_idx =
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2) * mi_params->mi_stride +
          (blk_size >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2);
#endif  // CONFIG_CCSO_FU_BUGFIX
      const bool use_ccso = mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y;
      if (!use_ccso) continue;
#if CCSO_REFACTORING
      const uint16_t *src_unit_y = src_y;
      uint16_t *dst_unit_yuv = dst_yuv;
      const int y_end = AOMMIN(pic_height - y, blk_size);
      const int x_end = AOMMIN(pic_width - x, blk_size);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size) {
#if CONFIG_BRU
          // FPU level skip
          const int mbmi_idx =
              get_mi_grid_idx(mi_params, (y + unit_y) >> MI_SIZE_LOG2,
                              (x + unit_x) >> MI_SIZE_LOG2);
          const int use_ccso_local =
              mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
          if (!use_ccso_local) {
            continue;
          }
          if (cm->bru.enabled &&
              mi_params->mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            aom_internal_error(
                &cm->error, AOM_CODEC_ERROR,
                "Invalid BRU activity in CCSO: only active SB can be filtered");
            return;
          }
#endif  // CONFIG_BRU
          if (cm->ccso_info.ccso_bo_only[plane]) {
            ccso_filter_block_hbd_wo_buf_c(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, 0, 0, thr, neg_thr, src_loc,
                max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size,
#endif
                unit_size, true, shift_bits, edge_clf,
                cm->ccso_info.ccso_bo_only[plane]);
          } else {
            ccso_filter_block_hbd_wo_buf(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, 0, 0, thr, neg_thr, src_loc,
                max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size,
#endif
                unit_size, true, shift_bits, edge_clf, 0);
          }
        }
        dst_unit_yuv += (dst_stride << unit_log2);
        src_unit_y += (ccso_ext_stride << unit_log2);
      }
#else
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size, true, shift_bits, edge_clf,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride, 0,
            0, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size, true, shift_bits, edge_clf, 0);
      }
#endif  // CCSO_REFACTORING
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_ext_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component when multiple bands are applied */
void ccso_apply_chroma_mb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
#if CCSO_REFACTORING
                                 const int proc_unit_log2,
#endif  // CCSO_REFACTORING
                                 const uint16_t thr, const uint8_t filter_sup,
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
#if CONFIG_CCSO_FU_BUGFIX
  assert(plane > 0);  // function must only be called for plane > 0
  const int blk_size = 1 << CCSO_BLK_SIZE;
  const int blk_log2_y = CCSO_BLK_SIZE - cm->seq_params.subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - cm->seq_params.subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
#else
  const int blk_log2_y = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_y;
#endif  // CONFIG_CCSO_FU_BUGFIX
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
#if CCSO_REFACTORING
  const int unit_log2_x =
      proc_unit_log2 > blk_log2_x ? blk_log2_x : proc_unit_log2;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_log2_y =
      proc_unit_log2 > blk_log2_y ? blk_log2_y : proc_unit_log2;
  const int unit_size_y = 1 << (unit_log2_y);
#endif  // CCSO_REFACTORING
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
#if CONFIG_CCSO_FU_BUGFIX
      const int ccso_blk_idx = (blk_size >> MI_SIZE_LOG2) * (y >> blk_log2_y) *
                                   mi_params->mi_stride +
                               (blk_size >> MI_SIZE_LOG2) * (x >> blk_log2_x);
#else
      const int ccso_blk_idx =
          (blk_size_y >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2_y) * mi_params->mi_stride +
          (blk_size_x >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2_y);
#endif  // CONFIG_CCSO_FU_BUGFIX
      const bool use_ccso =
          (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                       : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
      if (!use_ccso) continue;
#if CCSO_REFACTORING
      const uint16_t *src_unit_y = src_y;
      uint16_t *dst_unit_yuv = dst_yuv;
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
#if CONFIG_BRU
          // FPU level skip
          const int mbmi_idx = get_mi_grid_idx(
              mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          const int use_ccso_local =
              mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
          if (!use_ccso_local) {
            continue;
          }
          if (cm->bru.enabled &&
              mi_params->mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            aom_internal_error(
                &cm->error, AOM_CODEC_ERROR,
                "Invalid BRU activity in CCSO: only active SB can be filtered");
            return;
          }
#endif  // CONFIG_BRU
          if (cm->ccso_info.ccso_bo_only[plane]) {
            ccso_filter_block_hbd_wo_buf_c(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, y_uv_hscale, y_uv_vscale, thr,
                neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size_x,
#endif
                unit_size_y, false, shift_bits, edge_clf,
                cm->ccso_info.ccso_bo_only[plane]);
          } else {
            ccso_filter_block_hbd_wo_buf(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, y_uv_hscale, y_uv_vscale, thr,
                neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size_x,
#endif
                unit_size_y, false, shift_bits, edge_clf, 0);
          }
        }
        dst_unit_yuv += (dst_stride << unit_log2_y);
        src_unit_y += (ccso_ext_stride << (unit_log2_y + y_uv_vscale));
      }
#else
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size_x,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size_y, false, shift_bits, edge_clf,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size_x,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size_y, false, shift_bits, edge_clf, 0);
      }
#endif  // CCSO_REFACTORING
    }
    dst_yuv += (dst_stride << blk_log2_y);
    src_y += (ccso_ext_stride << (blk_log2_y + y_uv_vscale));
  }
}

/* Apply CCSO on chroma component when single bands is applied */
void ccso_apply_chroma_sb_filter(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 uint16_t *dst_yuv, const int dst_stride,
#if CCSO_REFACTORING
                                 const int proc_unit_log2,
#endif  // CCSO_REFACTORING
                                 const uint16_t thr, const uint8_t filter_sup,
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
#if CONFIG_CCSO_FU_BUGFIX
  assert(plane > 0);  // function must only be called for plane > 0
  const int blk_size = 1 << CCSO_BLK_SIZE;
  const int blk_log2_y = CCSO_BLK_SIZE - cm->seq_params.subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - cm->seq_params.subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
#else
  const int blk_log2_y = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_y;
#endif  // CONFIG_CCSO_FU_BUGFIX
  src_y += CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE;
#if CCSO_REFACTORING
  const int unit_log2_x =
      proc_unit_log2 > blk_log2_x ? blk_log2_x : proc_unit_log2;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_log2_y =
      proc_unit_log2 > blk_log2_y ? blk_log2_y : proc_unit_log2;
  const int unit_size_y = 1 << (unit_log2_y);
#endif  // CCSO_REFACTORING
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
#if CONFIG_CCSO_FU_BUGFIX
      const int ccso_blk_idx = (blk_size >> MI_SIZE_LOG2) * (y >> blk_log2_y) *
                                   mi_params->mi_stride +
                               (blk_size >> MI_SIZE_LOG2) * (x >> blk_log2_x);
#else
      const int ccso_blk_idx =
          (blk_size_y >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_y)) *
              (y >> blk_log2_y) * mi_params->mi_stride +
          (blk_size_x >> (MI_SIZE_LOG2 - xd->plane[plane].subsampling_x)) *
              (x >> blk_log2_y);
#endif  // CONFIG_CCSO_FU_BUGFIX
      const bool use_ccso =
          (plane == 1) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                       : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
      if (!use_ccso) continue;
#if CCSO_REFACTORING
      const uint16_t *src_unit_y = src_y;
      uint16_t *dst_unit_yuv = dst_yuv;
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
#if CONFIG_BRU
          // FPU level skip
          const int mbmi_idx = get_mi_grid_idx(
              mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          const int use_ccso_local =
              mi_params->mi_grid_base[mbmi_idx]->local_ccso_blk_flag;
          if (!use_ccso_local) {
            continue;
          }
          if (cm->bru.enabled &&
              mi_params->mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            aom_internal_error(
                &cm->error, AOM_CODEC_ERROR,
                "Invalid BRU activity in CCSO: only active SB can be filtered");
            return;
          }
#endif  // CONFIG_BRU
          if (cm->ccso_info.ccso_bo_only[plane]) {
            ccso_filter_block_hbd_wo_buf_c(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, y_uv_hscale, y_uv_vscale, thr,
                neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size_x,
#endif
                unit_size_y, true, shift_bits, edge_clf,
                cm->ccso_info.ccso_bo_only[plane]);
          } else {
            ccso_filter_block_hbd_wo_buf(
                src_unit_y, dst_unit_yuv, x + unit_x, y + unit_y, pic_width,
                pic_height, src_cls, cm->ccso_info.filter_offset[plane],
                ccso_ext_stride, dst_stride, y_uv_hscale, y_uv_vscale, thr,
                neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
                unit_size_x,
#endif
                unit_size_y, true, shift_bits, edge_clf, 0);
          }
        }
        dst_unit_yuv += (dst_stride << unit_log2_y);
        src_unit_y += (ccso_ext_stride << (unit_log2_y + y_uv_vscale));
      }
#else
      if (cm->ccso_info.ccso_bo_only[plane]) {
        ccso_filter_block_hbd_wo_buf_c(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size_x,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size_y, true, shift_bits, edge_clf,
            cm->ccso_info.ccso_bo_only[plane]);
      } else {
        ccso_filter_block_hbd_wo_buf(
            src_y, dst_yuv, x, y, pic_width, pic_height, src_cls,
            cm->ccso_info.filter_offset[plane], ccso_ext_stride, dst_stride,
            y_uv_hscale, y_uv_vscale, thr, neg_thr, src_loc, max_val,
#if CONFIG_CCSO_FU_BUGFIX
            blk_size_x,
#endif  // CONFIG_CCSO_FU_BUGFIX
            blk_size_y, true, shift_bits, edge_clf, 0);
      }
#endif  // CCSO_REFACTORING
    }
    dst_yuv += (dst_stride << blk_log2_y);
    src_y += (ccso_ext_stride << (blk_log2_y + y_uv_vscale));
  }
}

/* Apply CCSO for one frame */
void ccso_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm, MACROBLOCKD *xd,
                uint16_t *ext_rec_y) {
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                    { 32, 16, 64, 128 },
                                    { 48, 24, 96, 192 },
                                    { 64, 32, 128, 256 } };

  for (int plane = 0; plane < num_planes; plane++) {
    const int dst_stride = xd->plane[plane].dst.stride;
    const uint16_t quant_step_size = quant_sz[cm->ccso_info.scale_idx[plane]]
                                             [cm->ccso_info.quant_idx[plane]];
    if (cm->ccso_info.ccso_enable[plane]) {
      CCSO_FILTER_FUNC apply_ccso_filter_func =
          cm->ccso_info.max_band_log2[plane]
              ? (plane > 0 ? ccso_apply_chroma_mb_filter
                           : ccso_apply_luma_mb_filter)
              : (plane > 0 ? ccso_apply_chroma_sb_filter
                           : ccso_apply_luma_sb_filter);
      apply_ccso_filter_func(
          cm, xd, plane, ext_rec_y, &(xd->plane[plane].dst.buf)[0], dst_stride,
#if CCSO_REFACTORING
          cm->mib_size_log2 -
              AOMMAX(xd->plane[plane].subsampling_x,
                     xd->plane[plane].subsampling_y) +
              MI_SIZE_LOG2,
#endif  // CCSO_REFACTORING
          quant_step_size, cm->ccso_info.ext_filter_support[plane],
          cm->ccso_info.max_band_log2[plane], cm->ccso_info.edge_clf[plane]);
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
