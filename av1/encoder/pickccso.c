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

#include <math.h>
#include <string.h>
#include <float.h>

#include "av1/common/enums.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/pickccso.h"

typedef struct {
  uint8_t final_band_log2;
  int8_t best_filter_offset[CCSO_NUM_COMPONENTS][CCSO_BAND_NUM * 16];
  int8_t final_filter_offset[CCSO_NUM_COMPONENTS][CCSO_BAND_NUM * 16];
  bool best_filter_enabled[CCSO_NUM_COMPONENTS];
  bool final_filter_enabled[CCSO_NUM_COMPONENTS];
  uint8_t final_ext_filter_support[CCSO_NUM_COMPONENTS];
  int final_reuse_ccso[CCSO_NUM_COMPONENTS];
  int final_sb_reuse_ccso[CCSO_NUM_COMPONENTS];
  uint8_t final_scale_idx[CCSO_NUM_COMPONENTS];
  uint8_t final_quant_idx[CCSO_NUM_COMPONENTS];
  uint8_t final_ccso_bo_only[CCSO_NUM_COMPONENTS];

  int chroma_error[CCSO_BAND_NUM * 16];
  int chroma_count[CCSO_BAND_NUM * 16];
  int *total_class_err[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_cnt[CCSO_INPUT_INTERVAL][CCSO_INPUT_INTERVAL][CCSO_BAND_NUM];
  int *total_class_err_bo[CCSO_BAND_NUM];
  int *total_class_cnt_bo[CCSO_BAND_NUM];
  int ccso_stride;
  int ccso_stride_ext;
  bool *filter_control;
  bool *best_filter_control;
  bool *final_filter_control;
  uint64_t unfiltered_dist_frame;
  uint64_t filtered_dist_frame;
  uint64_t *unfiltered_dist_block;
  uint64_t *training_dist_block;
} CcsoCtx;

const int ccso_offset[8] = { -10, -7, -3, -1, 0, 1, 3, 7 };
#if !CONFIG_CCSO_CLEANUP
const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                  { 32, 16, 64, 128 },
                                  { 48, 24, 96, 192 },
                                  { 64, 32, 128, 256 } };
#endif  // CONFIG_CCSO_CLEANUP

const int ccso_scale[4] = { 1, 2, 3, 4 };

void ccso_derive_src_block_c(const uint16_t *src_y, uint8_t *const src_cls0,
                             uint8_t *const src_cls1, const int src_y_stride,
                             const int src_cls_stride, const int x, const int y,
                             const int pic_width, const int pic_height,
                             const int y_uv_hscale, const int y_uv_vscale,
                             const int qstep, const int neg_qstep,
                             const int *src_loc, const int blk_size_x,
                             const int blk_size_y, const int edge_clf) {
  int src_cls[2];
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
      cal_filter_support(src_cls,
                         &src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                (x_pos << y_uv_hscale)],
                         qstep, neg_qstep, src_loc, edge_clf);
      src_cls0[(y_pos << y_uv_vscale) * src_cls_stride +
               (x_pos << y_uv_hscale)] = src_cls[0];
      src_cls1[(y_pos << y_uv_vscale) * src_cls_stride +
               (x_pos << y_uv_hscale)] = src_cls[1];
    }
  }
}

/* Derive CCSO filter support information */
static void ccso_derive_src_info(AV1_COMMON *cm, MACROBLOCKD *xd,
                                 const int plane, const uint16_t *src_y,
                                 const int proc_unit_log2, const uint16_t qstep,
                                 const uint8_t filter_sup, uint8_t *src_cls0,
                                 uint8_t *src_cls1, int edge_clf,
                                 int ccso_stride, int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int neg_qstep = qstep * -1;
  int src_loc[2];
  derive_ccso_sample_pos(src_loc, ccso_stride_ext, filter_sup);
  const int blk_log2_y = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  const int unit_log2_x =
      proc_unit_log2 > blk_log2_x ? blk_log2_x : proc_unit_log2;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_log2_y =
      proc_unit_log2 > blk_log2_y ? blk_log2_y : proc_unit_log2;
  const int unit_size_y = 1 << (unit_log2_y);
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      // check BRU skip in entire CCSO FU, this means no signal needed
      if (bru_is_fu_skipped_mbmi(cm, x >> (MI_SIZE_LOG2 - y_uv_hscale),
                                 y >> (MI_SIZE_LOG2 - y_uv_vscale),
                                 blk_size_x >> (MI_SIZE_LOG2 - y_uv_hscale),
                                 blk_size_y >> (MI_SIZE_LOG2 - y_uv_vscale))) {
        continue;
      }
      // reset begining of FSU
      const uint16_t *src_y_unit = src_y;
      uint8_t *src_unit_0 = src_cls0;
      uint8_t *src_unit_1 = src_cls1;
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          ccso_derive_src_block(src_y_unit, src_unit_0, src_unit_1,
                                ccso_stride_ext, ccso_stride, x + unit_x,
                                y + unit_y, pic_width, pic_height, y_uv_hscale,
                                y_uv_vscale, qstep, neg_qstep, src_loc,
                                unit_size_x, unit_size_y, edge_clf);
        }
        // progress FPU
        src_y_unit += (ccso_stride_ext << (unit_log2_y + y_uv_vscale));
        src_unit_0 += (ccso_stride << (unit_log2_y + y_uv_vscale));
        src_unit_1 += (ccso_stride << (unit_log2_y + y_uv_vscale));
      }
    }
    src_y += (ccso_stride_ext << (blk_log2_y + y_uv_vscale));
    src_cls0 += (ccso_stride << (blk_log2_y + y_uv_vscale));
    src_cls1 += (ccso_stride << (blk_log2_y + y_uv_vscale));
  }
}

/* Compute the aggregated residual between original and reconstructed sample for
 * each entry of the LUT */
static void ccso_pre_compute_class_err(CcsoCtx *ctx, MACROBLOCKD *xd,
                                       const int plane, const AV1_COMMON *cm,
                                       const int proc_unit_log2,
                                       const uint16_t *src_y,
                                       const uint16_t *ref, const uint16_t *dst,
                                       uint8_t *src_cls0, uint8_t *src_cls1,
                                       const uint8_t shift_bits) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  int fb_idx = 0;
  uint8_t cur_src_cls0;
  uint8_t cur_src_cls1;
  const int blk_log2_y = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  const int scaled_stride = (ctx->ccso_stride << y_uv_vscale);
  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  const int unit_log2_x =
      proc_unit_log2 > blk_log2_x ? blk_log2_x : proc_unit_log2;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_log2_y =
      proc_unit_log2 > blk_log2_y ? blk_log2_y : proc_unit_log2;
  const int unit_size_y = 1 << (unit_log2_y);
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      fb_idx++;
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;  // FSU skip
      }
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      // reset temp pointers for sbs
      const uint16_t *ref_unit = ref;
      const uint16_t *dst_unit = dst;
      const uint16_t *src_y_unit = src_y;
      const uint8_t *src_unit0 = src_cls0;
      const uint8_t *src_unit1 = src_cls1;
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          const int y_end_unit = AOMMIN(pic_height - y - unit_y, unit_size_y);
          const int x_end_unit = AOMMIN(pic_width - x - unit_x, unit_size_x);
          // FPU skip
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          for (int y_start = 0; y_start < y_end_unit; y_start++) {
            for (int x_start = 0; x_start < x_end_unit; x_start++) {
              const int x_pos = x + unit_x + x_start;
              cur_src_cls0 = src_unit0[x_pos << y_uv_hscale];
              cur_src_cls1 = src_unit1[x_pos << y_uv_hscale];
              const int band_num =
                  src_y_unit[x_pos << y_uv_hscale] >> shift_bits;
              ctx->total_class_err[cur_src_cls0][cur_src_cls1][band_num]
                                  [fb_idx - 1] +=
                  ref_unit[x_pos] - dst_unit[x_pos];
              ctx->total_class_cnt[cur_src_cls0][cur_src_cls1][band_num]
                                  [fb_idx - 1]++;
            }  // x_start
            ref_unit += ctx->ccso_stride;
            dst_unit += ctx->ccso_stride;
            src_y_unit +=
                scaled_ext_stride;  // scaled means already done + y_uv_vscale
            src_unit0 += scaled_stride;
            src_unit1 += scaled_stride;
          }  // y_start
          ref_unit -= ctx->ccso_stride * y_end_unit;
          dst_unit -= ctx->ccso_stride * y_end_unit;
          src_y_unit -= scaled_ext_stride * y_end_unit;
          src_unit0 -= scaled_stride * y_end_unit;
          src_unit1 -= scaled_stride * y_end_unit;
        }  // unit_x
        // move to next unit (sb) row
        ref_unit += unit_size_y * ctx->ccso_stride;
        dst_unit += unit_size_y * ctx->ccso_stride;
        src_y_unit += scaled_ext_stride * unit_size_y;
        src_unit0 += scaled_stride * unit_size_y;
        src_unit1 += scaled_stride * unit_size_y;
      }  // unit_y
    }
    ref += (ctx->ccso_stride << blk_log2_y);
    dst += (ctx->ccso_stride << blk_log2_y);
    src_y += (ctx->ccso_stride_ext << (blk_log2_y + y_uv_vscale));
    src_cls0 += (ctx->ccso_stride << (blk_log2_y + y_uv_vscale));
    src_cls1 += (ctx->ccso_stride << (blk_log2_y + y_uv_vscale));
  }
}

// pre compute classes for band offset only option
static void ccso_pre_compute_class_err_bo(
    CcsoCtx *ctx, MACROBLOCKD *xd, const int plane, const AV1_COMMON *cm,
    const int proc_unit_log2, const uint16_t *src_y, const uint16_t *ref,
    const uint16_t *dst, const uint8_t shift_bits) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  int fb_idx = 0;
  const int blk_log2_y = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  const int unit_log2_x =
      proc_unit_log2 > blk_log2_x ? blk_log2_x : proc_unit_log2;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_log2_y =
      proc_unit_log2 > blk_log2_y ? blk_log2_y : proc_unit_log2;
  const int unit_size_y = 1 << (unit_log2_y);
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      fb_idx++;
      assert(ctx->filter_control);
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;
      }
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      assert(ctx->filter_control);
      // reset temp pointers for sbs
      const uint16_t *ref_unit = ref;
      const uint16_t *dst_unit = dst;
      const uint16_t *src_y_unit = src_y;
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          const int y_end_unit = AOMMIN(pic_height - y - unit_y, unit_size_y);
          const int x_end_unit = AOMMIN(pic_width - x - unit_x, unit_size_x);
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          for (int y_start = 0; y_start < y_end_unit; y_start++) {
            for (int x_start = 0; x_start < x_end_unit; x_start++) {
              const int x_pos = x + unit_x + x_start;
              const int band_num =
                  src_y_unit[x_pos << y_uv_hscale] >> shift_bits;
              ctx->total_class_err_bo[band_num][fb_idx - 1] +=
                  ref_unit[x_pos] - dst_unit[x_pos];
              ctx->total_class_cnt_bo[band_num][fb_idx - 1]++;
            }  // x_start
            ref_unit += ctx->ccso_stride;
            dst_unit += ctx->ccso_stride;
            src_y_unit += scaled_ext_stride;
          }  // y_start
          ref_unit -= ctx->ccso_stride * y_end_unit;
          dst_unit -= ctx->ccso_stride * y_end_unit;
          src_y_unit -= scaled_ext_stride * y_end_unit;
        }  // unit_x
        // move to next unit (sb) row
        ref_unit += unit_size_y * ctx->ccso_stride;
        dst_unit += unit_size_y * ctx->ccso_stride;
        src_y_unit += unit_size_y * scaled_ext_stride;
      }  // unit_y
    }
    ref += (ctx->ccso_stride << blk_log2_y);
    dst += (ctx->ccso_stride << blk_log2_y);
    src_y += (ctx->ccso_stride_ext << (blk_log2_y + y_uv_vscale));
  }
}

// Apply ccso filter when Band Offset Only option is true.
void ccso_filter_block_hbd_with_buf_bo_only_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int src_cls_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size_x,
    const int blk_size_y, const int y_uv_hscale, const int y_uv_vscale,
    const int max_val, const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  assert(ccso_bo_only == 1);

  (void)src_cls0;
  (void)src_cls1;
  (void)src_cls_stride;
  (void)ccso_bo_only;

  int cur_src_cls0;
  int cur_src_cls1;
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
      cur_src_cls0 = 0;
      cur_src_cls1 = 0;
      const int band_num = src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                 (x_pos << y_uv_hscale)] >>
                           shift_bits;
      const int lut_idx_ext =
          (band_num << 4) + (cur_src_cls0 << 2) + cur_src_cls1;
      const int offset_val = filter_offset[lut_idx_ext];
      dst_yuv[y_pos * dst_stride + x_pos] =
          clamp(offset_val + dst_yuv[y_pos * dst_stride + x_pos], 0, max_val);
    }
  }
}

void ccso_filter_block_hbd_with_buf_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int src_cls_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size_x,
    const int blk_size_y, const int y_uv_hscale, const int y_uv_vscale,
    const int max_val, const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  if (ccso_bo_only) {
    (void)src_cls0;
    (void)src_cls1;
  }
  int cur_src_cls0;
  int cur_src_cls1;
  const int y_end = AOMMIN(pic_height - y, blk_size_y);
  const int x_end = AOMMIN(pic_width - x, blk_size_x);
  for (int y_start = 0; y_start < y_end; y_start++) {
    const int y_pos = y_start;
    for (int x_start = 0; x_start < x_end; x_start++) {
      const int x_pos = x + x_start;
      if (!ccso_bo_only) {
        cur_src_cls0 = src_cls0[(y_pos << y_uv_vscale) * src_cls_stride +
                                (x_pos << y_uv_hscale)];
        cur_src_cls1 = src_cls1[(y_pos << y_uv_vscale) * src_cls_stride +
                                (x_pos << y_uv_hscale)];
      } else {
        cur_src_cls0 = 0;
        cur_src_cls1 = 0;
      }
      const int band_num = src_y[(y_pos << y_uv_vscale) * src_y_stride +
                                 (x_pos << y_uv_hscale)] >>
                           shift_bits;
      const int lut_idx_ext =
          (band_num << 4) + (cur_src_cls0 << 2) + cur_src_cls1;
      const int offset_val = filter_offset[lut_idx_ext];
      dst_yuv[y_pos * dst_stride + x_pos] =
          clamp(offset_val + dst_yuv[y_pos * dst_stride + x_pos], 0, max_val);
    }
  }
}
/* Apply CCSO on luma component at encoder (high bit-depth) */
void ccso_try_luma_filter(CcsoCtx *ctx, AV1_COMMON *cm, MACROBLOCKD *xd,
                          const int plane, const uint16_t *src_y,
                          uint16_t *dst_yuv, const int dst_stride,
                          const int8_t *filter_offset, uint8_t *src_cls0,
                          uint8_t *src_cls1, const uint8_t shift_bits,
                          const uint8_t ccso_bo_only, int ccso_stride,
                          int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int blk_log2 = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int blk_size = 1 << blk_log2;
  int fb_idx = 0;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  // luma only
  int unit_log2 = cm->mib_size_log2 + MI_SIZE_LOG2;
  if (unit_log2 > blk_log2) {
    unit_log2 = blk_log2;
  }
  const int unit_size = 1 << (unit_log2);
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      fb_idx++;
      assert(ctx->filter_control);
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;  // FSU level skip
      }
      const uint16_t *src_y_unit = src_y;
      uint16_t *dst_unit = dst_yuv;
      const uint8_t *src_unit0 = src_cls0;
      const uint8_t *src_unit1 = src_cls1;
      const int y_end = AOMMIN(pic_height - y, blk_size);
      const int x_end = AOMMIN(pic_width - x, blk_size);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size) {
          // FPU level skip
          const int mbmi_idx =
              get_mi_grid_idx(&cm->mi_params, (y + unit_y) >> MI_SIZE_LOG2,
                              (x + unit_x) >> MI_SIZE_LOG2);
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          if (ccso_bo_only) {
            ccso_filter_block_hbd_with_buf_bo_only(
                src_y_unit, dst_unit, src_unit0, src_unit1, ccso_stride_ext,
                dst_stride, ccso_stride, x + unit_x, y + unit_y, pic_width,
                pic_height, filter_offset, unit_size, unit_size,
                // y_uv_scale in h and v shall be zero
                0, 0, max_val, shift_bits, ccso_bo_only);
          } else {
            ccso_filter_block_hbd_with_buf(
                src_y_unit, dst_unit, src_unit0, src_unit1, ccso_stride_ext,
                dst_stride, ccso_stride, x + unit_x, y + unit_y, pic_width,
                pic_height, filter_offset, unit_size, unit_size,
                // y_uv_scale in h and v shall be zero
                0, 0, max_val, shift_bits, 0);
          }
        }  // unit_x
        dst_unit += (dst_stride << unit_log2);
        src_y_unit += (ccso_stride_ext << unit_log2);
        src_unit0 += (ccso_stride << unit_log2);
        src_unit1 += (ccso_stride << unit_log2);
      }  // unit_y
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_stride_ext << blk_log2);
    src_cls0 += (ccso_stride << blk_log2);
    src_cls1 += (ccso_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component at encoder (high bit-depth) */
static void ccso_try_chroma_filter(
    CcsoCtx *ctx, AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
    const uint16_t *src_y, uint16_t *dst_yuv, const int dst_stride,
    const int8_t *filter_offset, uint8_t *src_cls0, uint8_t *src_cls1,
    const uint8_t shift_bits, const uint8_t ccso_bo_only, int ccso_stride,
    int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int blk_log2_y = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int blk_log2_x = CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  int fb_idx = 0;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  int unit_log2_x = cm->mib_size_log2 + MI_SIZE_LOG2 - y_uv_hscale;
  if (unit_log2_x > blk_log2_x) {
    unit_log2_x = blk_log2_x;
  }
  const int unit_size_x = 1 << (unit_log2_x);
  int unit_log2_y = cm->mib_size_log2 + MI_SIZE_LOG2 - y_uv_vscale;
  if (unit_log2_y > blk_log2_y) {
    unit_log2_y = blk_log2_y;
  }
  const int unit_size_y = 1 << (unit_log2_y);
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      fb_idx++;
      assert(ctx->filter_control);
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;
      }
      const uint16_t *src_y_unit = src_y;
      uint16_t *dst_unit = dst_yuv;
      const uint8_t *src_unit0 = src_cls0;
      const uint8_t *src_unit1 = src_cls1;
      const int y_end = AOMMIN(pic_height - y, blk_size_y);
      const int x_end = AOMMIN(pic_width - x, blk_size_x);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          // FPU level skip
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - y_uv_vscale),
              (x + unit_x) >> (MI_SIZE_LOG2 - y_uv_hscale));
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          if (ccso_bo_only) {
            ccso_filter_block_hbd_with_buf_bo_only(
                src_y_unit, dst_unit, src_unit0, src_unit1, ccso_stride_ext,
                dst_stride, ccso_stride, x + unit_x, y + unit_y, pic_width,
                pic_height, filter_offset, unit_size_x, unit_size_y,
                y_uv_hscale, y_uv_vscale, max_val, shift_bits, ccso_bo_only);
          } else {
            ccso_filter_block_hbd_with_buf(
                src_y_unit, dst_unit, src_unit0, src_unit1, ccso_stride_ext,
                dst_stride, ccso_stride, x + unit_x, y + unit_y, pic_width,
                pic_height, filter_offset, unit_size_x, unit_size_y,
                y_uv_hscale, y_uv_vscale, max_val, shift_bits, 0);
          }
        }  // unit_x
        dst_unit += (dst_stride << unit_log2_y);
        src_y_unit += (ccso_stride_ext << (unit_log2_y + y_uv_vscale));
        src_unit0 += (ccso_stride << (unit_log2_y + y_uv_vscale));
        src_unit1 += (ccso_stride << (unit_log2_y + y_uv_vscale));
      }  // unit_y
    }
    dst_yuv += (dst_stride << blk_log2_y);
    src_y += (ccso_stride_ext << (blk_log2_y + y_uv_vscale));
    src_cls0 += (ccso_stride << (blk_log2_y + y_uv_vscale));
    src_cls1 += (ccso_stride << (blk_log2_y + y_uv_vscale));
  }
}

uint64_t compute_distortion_block_c(const uint16_t *org, const int org_stride,
                                    const uint16_t *rec16, const int rec_stride,
                                    const int x, const int y,
                                    const int log2_filter_unit_size_y,
                                    const int log2_filter_unit_size_x,
                                    const int height, const int width) {
  int err;
  uint64_t ssd = 0;
  int y_offset;
  int x_offset;
  if (y + (1 << log2_filter_unit_size_y) >= height)
    y_offset = height - y;
  else
    y_offset = (1 << log2_filter_unit_size_y);

  if (x + (1 << log2_filter_unit_size_x) >= width)
    x_offset = width - x;
  else
    x_offset = (1 << log2_filter_unit_size_x);

  for (int y_off = 0; y_off < y_offset; y_off++) {
    for (int x_off = 0; x_off < x_offset; x_off++) {
      err = org[org_stride * y_off + x + x_off] -
            rec16[rec_stride * y_off + x + x_off];
      ssd += err * err;
    }
  }
  return ssd;
}
/* Compute SSE */
static void compute_distortion(
    const uint16_t *org, const int org_stride, const uint16_t *rec16,
    const int rec_stride, const int log2_filter_unit_size_y,
    const int log2_filter_unit_size_x, const int log2_proc_unit_size,
    const AV1_COMMON *cm, const int subsampling_y, const int subsampling_x,
    const int height, const int width, uint64_t *distortion_buf,
    const int distortion_buf_stride, uint64_t *total_distortion) {
  int unit_log2_x = log2_proc_unit_size > log2_filter_unit_size_x
                        ? log2_filter_unit_size_x
                        : log2_proc_unit_size;
  int unit_log2_y = log2_proc_unit_size > log2_filter_unit_size_y
                        ? log2_filter_unit_size_y
                        : log2_proc_unit_size;
  const int unit_size_x = 1 << (unit_log2_x);
  const int unit_size_y = 1 << (unit_log2_y);
  const int blk_size_x = (1 << log2_filter_unit_size_x);
  const int blk_size_y = (1 << log2_filter_unit_size_y);
  for (int y = 0; y < height; y += (1 << log2_filter_unit_size_y)) {
    for (int x = 0; x < width; x += (1 << log2_filter_unit_size_x)) {
      // check BRU skip in entire CCSO FSU
      if (bru_is_fu_skipped_mbmi(
              cm, x >> (MI_SIZE_LOG2 - subsampling_x),
              y >> (MI_SIZE_LOG2 - subsampling_y),
              blk_size_x >> (MI_SIZE_LOG2 - subsampling_x),
              blk_size_y >> (MI_SIZE_LOG2 - subsampling_y))) {
        distortion_buf[(y >> log2_filter_unit_size_y) * distortion_buf_stride +
                       (x >> log2_filter_unit_size_x)] = 0;
        continue;
      }
      // All unified into pixel size
      uint64_t sb_ssd = 0;
      const uint16_t *org_unit = org;
      const uint16_t *rec_unit = rec16;
      const int y_end = AOMMIN(height - y, (1 << log2_filter_unit_size_y));
      const int x_end = AOMMIN(width - x, (1 << log2_filter_unit_size_x));
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          // skip if unit skip
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> (MI_SIZE_LOG2 - subsampling_y),
              (x + unit_x) >> (MI_SIZE_LOG2 - subsampling_x));
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          // skip if unit skip
          sb_ssd += compute_distortion_block(
              org_unit, org_stride, rec_unit, rec_stride, x + unit_x,
              y + unit_y, unit_log2_y, unit_log2_x, height, width);
        }  //
        // offset org, rec16 here
        org_unit += (org_stride << unit_log2_x);
        rec_unit += (rec_stride << unit_log2_x);
      }  //
      const uint64_t ssd = sb_ssd;
      distortion_buf[(y >> log2_filter_unit_size_y) * distortion_buf_stride +
                     (x >> log2_filter_unit_size_x)] = ssd;
      *total_distortion += ssd;
    }
    org += (org_stride << log2_filter_unit_size_y);
    rec16 += (rec_stride << log2_filter_unit_size_y);
  }
}

int get_ccso_context(const int sb_y, const int sb_x, const int ccso_nhfb,
                     bool *m_filter_control) {
  int neighbor0_sb_y = -1;
  int neighbor0_sb_x = -1;
  int neighbor1_sb_y = -1;
  int neighbor1_sb_x = -1;
  int neighbor0_sb_idx = -1;
  int neighbor1_sb_idx = -1;

  int is_neighbor0_ccso = 0;
  int is_neighbor1_ccso = 0;

  if (sb_y > 0 && sb_x > 0) {
    neighbor0_sb_y = sb_y;
    neighbor0_sb_x = sb_x - 1;
    neighbor1_sb_y = sb_y - 1;
    neighbor1_sb_x = sb_x;

    neighbor0_sb_idx = neighbor0_sb_y * ccso_nhfb + neighbor0_sb_x;
    neighbor1_sb_idx = neighbor1_sb_y * ccso_nhfb + neighbor1_sb_x;

    is_neighbor0_ccso = m_filter_control[neighbor0_sb_idx];
    is_neighbor1_ccso = m_filter_control[neighbor1_sb_idx];

    return is_neighbor0_ccso && is_neighbor1_ccso
               ? 3
               : is_neighbor0_ccso || is_neighbor1_ccso;
  } else if (sb_y > 0 || sb_x > 0) {
    if (sb_x > 0) {
      neighbor0_sb_y = sb_y;
      neighbor0_sb_x = sb_x - 1;
    } else {
      neighbor0_sb_y = sb_y - 1;
      neighbor0_sb_x = sb_x;
    }
    neighbor0_sb_idx = neighbor0_sb_y * ccso_nhfb + neighbor0_sb_x;
    is_neighbor0_ccso = m_filter_control[neighbor0_sb_idx];

    return is_neighbor0_ccso ? 2 : 0;
  } else {
    return 0;
  }
}

/* Derive block level on/off for CCSO */
static void derive_blk_md(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                          const uint64_t *unfiltered_dist,
                          const uint64_t *training_dist, bool *m_filter_control,
                          uint64_t *cur_total_dist, int *cur_total_rate,
                          bool *filter_enable, const int rdmult) {
  aom_cdf_prob ccso_cdf[CCSO_CONTEXT][CDF_SIZE(2)];
  const int log2_filter_unit_size =
      CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  bool cur_filter_enabled = false;
  int sb_idx = 0;

  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int sb_unit_size_x =
      (1 << log2_filter_unit_size >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << log2_filter_unit_size >> (MI_SIZE_LOG2 - ss_y));
  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int blk_size_y = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;

  *cur_total_dist = 0;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av1_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      av1_tile_set_col(&tile_info, cm, tile_col);

      av1_copy(ccso_cdf, cm->fc->ccso_cdf[plane]);

      const int mi_row_start = tile_info.mi_row_start;
      const int mi_row_end = tile_info.mi_row_end;
      const int mi_col_start = tile_info.mi_col_start;
      const int mi_col_end = tile_info.mi_col_end;

      for (int mi_row = mi_row_start; mi_row < mi_row_end; ++mi_row) {
        for (int mi_col = mi_col_start; mi_col < mi_col_end; ++mi_col) {
          if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x)) {
            sb_idx = (mi_row / (blk_size_y + 1)) * ccso_nhfb +
                     (mi_col / (blk_size_x + 1));
          } else {
            continue;
          }
          const int ccso_ctx = get_ccso_context((mi_row / (blk_size_y + 1)),
                                                (mi_col / (blk_size_x + 1)),
                                                ccso_nhfb, m_filter_control);
          uint64_t ssd;
          uint64_t best_ssd = UINT64_MAX;
          int best_rate = INT_MAX;

          uint64_t best_cost = UINT64_MAX;

          uint8_t cur_best_filter_control = 0;

          int cost_from_cdf[CCSO_CONTEXT][2];
          av1_cost_tokens_from_cdf(cost_from_cdf[ccso_ctx], ccso_cdf[ccso_ctx],
                                   2, NULL);
          // check BRU skip in entire CCSO FU
          if (bru_is_fu_skipped_mbmi(cm, mi_col, mi_row, sb_unit_size_x,
                                     sb_unit_size_y)) {
            // assert(m_filter_control[sb_idx] == 0);
            m_filter_control[sb_idx] = 0;
            continue;
          }
          for (int cur_filter_control = 0; cur_filter_control < 2;
               cur_filter_control++) {
            if (!(*filter_enable)) {
              continue;
            }
            if (cur_filter_control == 0) {
              ssd = unfiltered_dist[sb_idx];
            } else {
              ssd = training_dist[sb_idx];
            }
            ssd = ROUND_POWER_OF_TWO(ssd, (xd->bd - 8) * 2);

            const uint64_t rd_cost = RDCOST(
                rdmult, cost_from_cdf[ccso_ctx][cur_filter_control], ssd * 16);
            if (rd_cost < best_cost) {
              best_cost = rd_cost;

              best_rate = cost_from_cdf[ccso_ctx][cur_filter_control];

              best_ssd = ssd;
              cur_best_filter_control = cur_filter_control;
              m_filter_control[sb_idx] = cur_filter_control;
            }
          }

          update_cdf(ccso_cdf[ccso_ctx], cur_best_filter_control, 2);

          if (cur_best_filter_control != 0) {
            cur_filter_enabled = true;
          }
          *cur_total_rate += best_rate;
          *cur_total_dist += best_ssd;
        }
      }
    }
  }

  *filter_enable = cur_filter_enabled;
}

static void get_sb_reuse_dist(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                              const uint64_t *unfiltered_dist,
                              const uint64_t *training_dist,
                              const bool *m_filter_control,
                              uint64_t *cur_total_dist, int *cur_total_rate,
                              bool *filter_enable, const int rdmult) {
  (void)rdmult;
  const int log2_filter_unit_size =
      CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  bool cur_filter_enabled = false;
  int sb_idx = 0;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int sb_unit_size_x =
      (1 << log2_filter_unit_size >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << log2_filter_unit_size >> (MI_SIZE_LOG2 - ss_y));
  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int blk_size_y = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;

  *cur_total_dist = 0;
  *cur_total_rate = 0;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av1_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      av1_tile_set_col(&tile_info, cm, tile_col);

      const int mi_row_start = tile_info.mi_row_start;
      const int mi_row_end = tile_info.mi_row_end;
      const int mi_col_start = tile_info.mi_col_start;
      const int mi_col_end = tile_info.mi_col_end;

      for (int mi_row = mi_row_start; mi_row < mi_row_end; ++mi_row) {
        for (int mi_col = mi_col_start; mi_col < mi_col_end; ++mi_col) {
          if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x)) {
            sb_idx = (mi_row / (blk_size_y + 1)) * ccso_nhfb +
                     (mi_col / (blk_size_x + 1));
          } else {
            continue;
          }

          // check BRU skip in entire CCSO FU
          if (bru_is_fu_skipped_mbmi(cm, mi_col, mi_row, sb_unit_size_x,
                                     sb_unit_size_y)) {
            // assert(m_filter_control[sb_idx] == 0);
            continue;
          }
          uint64_t ssd;

          if (!(*filter_enable)) continue;

          if (m_filter_control[sb_idx])
            ssd = training_dist[sb_idx];
          else
            ssd = unfiltered_dist[sb_idx];

          ssd = ROUND_POWER_OF_TWO(ssd, (xd->bd - 8) * 2);

          if (m_filter_control[sb_idx] != 0) cur_filter_enabled = true;

          *cur_total_dist += ssd;
        }
      }
    }
  }

  *filter_enable = cur_filter_enabled;
}

/* Compute the residual for each entry of the LUT using CCSO enabled filter
 * blocks
 */
static void ccso_compute_class_err(CcsoCtx *ctx, AV1_COMMON *cm,
                                   const int plane, MACROBLOCKD *xd,
                                   const int max_band_log2,
                                   const int max_edge_interval,
                                   const uint8_t ccso_bo_only) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int blk_log2 = CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int nvfb = ((mi_params->mi_rows >> xd->plane[plane].subsampling_y) +
                    (1 << blk_log2 >> MI_SIZE_LOG2) - 1) /
                   (1 << blk_log2 >> MI_SIZE_LOG2);
  const int nhfb = ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
                    (1 << blk_log2 >> MI_SIZE_LOG2) - 1) /
                   (1 << blk_log2 >> MI_SIZE_LOG2);
  const int fb_count = nvfb * nhfb;

  for (int fb_idx = 0; fb_idx < fb_count; fb_idx++) {
    if (!ctx->filter_control[fb_idx]) continue;
    if (ccso_bo_only) {
      int d0 = 0;
      int d1 = 0;
      for (int band_num = 0; band_num < (1 << max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        ctx->chroma_error[lut_idx_ext] +=
            ctx->total_class_err_bo[band_num][fb_idx];
        ctx->chroma_count[lut_idx_ext] +=
            ctx->total_class_cnt_bo[band_num][fb_idx];
      }
    } else {
      for (int d0 = 0; d0 < max_edge_interval; d0++) {
        for (int d1 = 0; d1 < max_edge_interval; d1++) {
          for (int band_num = 0; band_num < (1 << max_band_log2); band_num++) {
            const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
            ctx->chroma_error[lut_idx_ext] +=
                ctx->total_class_err[d0][d1][band_num][fb_idx];
            ctx->chroma_count[lut_idx_ext] +=
                ctx->total_class_cnt[d0][d1][band_num][fb_idx];
          }
        }
      }
    }
  }
}

/* Count the bits for signaling the offset index */
static INLINE int count_lut_bits(int8_t *temp_filter_offset, int scale_idx,
                                 const int max_band_log2,
                                 const int max_edge_interval,
                                 const uint8_t ccso_bo_only) {
  int ccso_offset_reordered[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  for (int idx = 0; idx < 8; ++idx)
    ccso_offset_reordered[idx] =
        ccso_offset_reordered[idx] * ccso_scale[scale_idx];
  int temp_bits = 0;
  int num_edge_offset_intervals = ccso_bo_only ? 1 : max_edge_interval;
  for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
    for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
      for (int band_num = 0; band_num < (1 << max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        for (int idx = 0; idx < 7; ++idx) {
          temp_bits++;
          if (ccso_offset_reordered[idx] == temp_filter_offset[lut_idx_ext])
            break;
        }
      }
    }
  }
  return temp_bits;
}

/* Derive the offset value in the look-up table */
static void derive_lut_offset(int8_t *temp_filter_offset, int scale_idx,
                              const int max_band_log2,
                              const int max_edge_interval,
                              const uint8_t ccso_bo_only,
                              const int chroma_count[CCSO_BAND_NUM * 16],
                              const int chroma_error[CCSO_BAND_NUM * 16]) {
  float temp_offset = 0;
  int num_edge_offset_intervals = ccso_bo_only ? 1 : max_edge_interval;
  int this_ccso_offset[8] = { 0 };

  for (int idx = 0; idx < 8; ++idx)
    this_ccso_offset[idx] = ccso_offset[idx] * ccso_scale[scale_idx];
  for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
    for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
      for (int band_num = 0; band_num < (1 << max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        if (chroma_count[lut_idx_ext]) {
          temp_offset =
              (float)chroma_error[lut_idx_ext] / chroma_count[lut_idx_ext];
          if ((temp_offset < this_ccso_offset[0]) ||
              (temp_offset >= this_ccso_offset[7])) {
            temp_filter_offset[lut_idx_ext] = clamp(
                (int)temp_offset, this_ccso_offset[0], this_ccso_offset[7]);
          } else {
            for (int offset_idx = 0; offset_idx < 7; offset_idx++) {
              if ((temp_offset >= this_ccso_offset[offset_idx]) &&
                  (temp_offset <= this_ccso_offset[offset_idx + 1])) {
                if (fabs(temp_offset - this_ccso_offset[offset_idx]) >
                    fabs(temp_offset - this_ccso_offset[offset_idx + 1])) {
                  temp_filter_offset[lut_idx_ext] =
                      this_ccso_offset[offset_idx + 1];
                } else {
                  temp_filter_offset[lut_idx_ext] =
                      this_ccso_offset[offset_idx];
                }
                break;
              }
            }
          }
        }
      }
    }
  }
}

/* Derive the look-up table for a color component */
static void derive_ccso_filter(CcsoCtx *ctx, AV1_COMMON *cm, const int plane,
                               MACROBLOCKD *xd, const uint16_t *org_uv,
                               const uint16_t *ext_rec_y,
                               const uint16_t *rec_uv, int rdmult,
                               bool error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                               ,
                               ThreadData *td
#endif
) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int log2_filter_unit_size_y =
      CCSO_BLK_SIZE - xd->plane[plane].subsampling_y;
  const int log2_filter_unit_size_x =
      CCSO_BLK_SIZE - xd->plane[plane].subsampling_x;

  const int ccso_nvfb =
      ((mi_params->mi_rows >> xd->plane[plane].subsampling_y) +
       (1 << log2_filter_unit_size_y >> 2) - 1) /
      (1 << log2_filter_unit_size_y >> 2);
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size_x >> 2) - 1) /
      (1 << log2_filter_unit_size_x >> 2);
  const int sb_count = ccso_nvfb * ccso_nhfb;
  // Use cropped height for derivation of ccso filter coefficients at encoder
  const int pic_height_c = xd->plane[plane].dst.crop_height;
  // Use cropped width for derivation of ccso filter coefficients at encoder
  const int pic_width_c = xd->plane[plane].dst.crop_width;
  uint16_t *temp_rec_uv_buf;
  ctx->unfiltered_dist_frame = 0;
  ctx->unfiltered_dist_block =
      aom_malloc(sizeof(*ctx->unfiltered_dist_block) * sb_count);
  memset(ctx->unfiltered_dist_block, 0,
         sizeof(*ctx->unfiltered_dist_block) * sb_count);
  ctx->training_dist_block =
      aom_malloc(sizeof(*ctx->training_dist_block) * sb_count);
  memset(ctx->training_dist_block, 0,
         sizeof(*ctx->training_dist_block) * sb_count);
  ctx->filter_control = aom_malloc(sizeof(*ctx->filter_control) * sb_count);
  memset(ctx->filter_control, 0, sizeof(*ctx->filter_control) * sb_count);
  ctx->best_filter_control =
      aom_malloc(sizeof(*ctx->best_filter_control) * sb_count);
  memset(ctx->best_filter_control, 0,
         sizeof(*ctx->best_filter_control) * sb_count);
  ctx->final_filter_control =
      aom_malloc(sizeof(*ctx->final_filter_control) * sb_count);
  memset(ctx->final_filter_control, 0,
         sizeof(*ctx->final_filter_control) * sb_count);
  temp_rec_uv_buf = aom_malloc(sizeof(*temp_rec_uv_buf) *
                               xd->plane[0].dst.height * ctx->ccso_stride);
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int sb_unit_size_x =
      (1 << log2_filter_unit_size_x >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << log2_filter_unit_size_y >> (MI_SIZE_LOG2 - ss_y));
  for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; d0++) {
    for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; d1++) {
      for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
        ctx->total_class_err[d0][d1][band_num] = aom_malloc(
            sizeof(*ctx->total_class_err[d0][d1][band_num]) * sb_count);
        ctx->total_class_cnt[d0][d1][band_num] = aom_malloc(
            sizeof(*ctx->total_class_cnt[d0][d1][band_num]) * sb_count);
      }
    }
  }
  for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
    ctx->total_class_err_bo[band_num] =
        aom_malloc(sizeof(*ctx->total_class_err_bo[band_num]) * sb_count);
    ctx->total_class_cnt_bo[band_num] =
        aom_malloc(sizeof(*ctx->total_class_cnt_bo[band_num]) * sb_count);
  }

  compute_distortion(org_uv, ctx->ccso_stride, rec_uv, ctx->ccso_stride,
                     log2_filter_unit_size_y, log2_filter_unit_size_x,
                     cm->mib_size_log2 - AOMMAX(ss_x, ss_y) + MI_SIZE_LOG2, cm,
                     ss_y, ss_x, pic_height_c, pic_width_c,
                     ctx->unfiltered_dist_block, ccso_nhfb,
                     &ctx->unfiltered_dist_frame);

  ctx->unfiltered_dist_frame =
      ROUND_POWER_OF_TWO(ctx->unfiltered_dist_frame, (xd->bd - 8) * 2);
  const uint64_t best_unfiltered_cost =
      RDCOST(rdmult, av1_cost_literal(1), ctx->unfiltered_dist_frame * 16);
  uint64_t best_filtered_cost;
  uint64_t final_filtered_cost = UINT64_MAX;
  int best_reuse_ccso = 0;
  int best_sb_reuse_ccso = 0;
  int best_ref_idx = -1;
  int final_ref_idx = -1;
  const int total_scale_idx = 4;

  uint8_t best_edge_classifier = 0;
  uint8_t final_edge_classifier = 0;
  const int total_edge_classifier = 2;
  int8_t filter_offset[CCSO_BAND_NUM * 16];
  const int total_filter_support = 7;

  const int total_quant_idx = 4;
  const int total_band_log2_plus1 = 4;
  uint8_t frame_bits = 1;
  uint8_t frame_bits_bo_only = 1;  // enabling flag
  frame_bits_bo_only += 1;         // bo only flag
  frame_bits += 1;                 // bo only flag
  frame_bits += 2;                 // quant step size
  frame_bits += 2;                 // scale index
  frame_bits += 3;                 // filter support index
  frame_bits += 1;                 // edge_clf
  frame_bits += 2;                 // band number log2
  frame_bits_bo_only += 3;         // band number log2
  frame_bits_bo_only += 2;         // scale index
  uint8_t *src_cls0;
  uint8_t *src_cls1;
  src_cls0 = aom_malloc(sizeof(*src_cls0) * xd->plane[0].dst.height *
                        xd->plane[0].dst.width);
  src_cls1 = aom_malloc(sizeof(*src_cls1) * xd->plane[0].dst.height *
                        xd->plane[0].dst.width);
  memset(src_cls0, 0,
         sizeof(*src_cls0) * xd->plane[0].dst.height * xd->plane[0].dst.width);
  memset(src_cls1, 0,
         sizeof(*src_cls1) * xd->plane[0].dst.height * xd->plane[0].dst.width);
  const int is_intra_frame = frame_is_intra_only(cm);
  int check_ccso = 0;

  RefCntBuffer *ref_frame = NULL;
  CcsoInfo *ref_frame_ccso_info = NULL;

  const int num_ref_frames =
      (frame_is_intra_only(cm) || cm->features.error_resilient_mode ||
       error_resilient_frame_seen)
          ? 0
          : cm->ref_frames_info.num_total_refs;

  cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
  memset(cm->cur_frame->ccso_info.sb_filter_control[plane], 0,
         sizeof(*cm->cur_frame->ccso_info.sb_filter_control[plane]) * sb_count);

  if (!is_intra_frame) {
    frame_bits += 2;
    frame_bits_bo_only += 2;
    check_ccso = 1;
  }

  for (int scale_idx = 0; scale_idx < total_scale_idx; ++scale_idx) {
    for (uint8_t ccso_bo_only = 0; ccso_bo_only < 2; ccso_bo_only++) {
      int num_filter_iter = ccso_bo_only ? 1 : total_filter_support;
      int num_quant_iter = ccso_bo_only ? 1 : total_quant_idx;
      int num_edge_clf_iter = ccso_bo_only ? 1 : total_edge_classifier;
      for (int ext_filter_support = 0; ext_filter_support < num_filter_iter;
           ext_filter_support++) {
        for (int quant_idx = 0; quant_idx < num_quant_iter; quant_idx++) {
          for (int edge_clf = 0; edge_clf < num_edge_clf_iter; edge_clf++) {
            const int max_edge_interval = edge_clf_to_edge_interval[edge_clf];

#if CONFIG_CCSO_CLEANUP
            if (quant_sz[scale_idx][quant_idx] == 0 && edge_clf == 1) {
              continue;
            }
#endif  // CONFIG_CCSO_CLEANUP
            if (!ccso_bo_only) {
              ccso_derive_src_info(
                  cm, xd, plane, ext_rec_y,
                  cm->mib_size_log2 - (ss_x > ss_y ? ss_x : ss_y) +
                      MI_SIZE_LOG2,
                  quant_sz[scale_idx][quant_idx], ext_filter_support, src_cls0,
                  src_cls1, edge_clf, ctx->ccso_stride, ctx->ccso_stride_ext);
            }
            int num_band_iter = total_band_log2_plus1;
            if (ccso_bo_only) {
              num_band_iter = total_band_log2_plus1 + 3;
            }
            for (int max_band_log2 = 0; max_band_log2 < num_band_iter;
                 max_band_log2++) {
              const int shift_bits = cm->seq_params.bit_depth - max_band_log2;
              const int max_band = 1 << max_band_log2;
              if (ccso_bo_only) {
                for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
                  memset(ctx->total_class_err_bo[band_num], 0,
                         sizeof(*ctx->total_class_err_bo[band_num]) * sb_count);
                  memset(ctx->total_class_cnt_bo[band_num], 0,
                         sizeof(*ctx->total_class_cnt_bo[band_num]) * sb_count);
                }
                ccso_pre_compute_class_err_bo(
                    ctx, xd, plane, cm,
                    cm->mib_size_log2 - AOMMAX(ss_x, ss_y) + MI_SIZE_LOG2,
                    ext_rec_y, org_uv, rec_uv, shift_bits);
              } else {
                for (int d0 = 0; d0 < max_edge_interval; d0++) {
                  for (int d1 = 0; d1 < max_edge_interval; d1++) {
                    for (int band_num = 0; band_num < max_band; band_num++) {
                      memset(ctx->total_class_err[d0][d1][band_num], 0,
                             sizeof(*ctx->total_class_err[d0][d1][band_num]) *
                                 sb_count);
                      memset(ctx->total_class_cnt[d0][d1][band_num], 0,
                             sizeof(*ctx->total_class_cnt[d0][d1][band_num]) *
                                 sb_count);
                    }
                  }
                }
                ccso_pre_compute_class_err(
                    ctx, xd, plane, cm,
                    cm->mib_size_log2 - AOMMAX(ss_x, ss_y) + MI_SIZE_LOG2,
                    ext_rec_y, org_uv, rec_uv, src_cls0, src_cls1, shift_bits);
              }

              unsigned int
                  checked_reuse_ref[2][7];  // used to store the already checked
                                            // ccso parameters to avoid checking
                                            // for a second time.
              memset(checked_reuse_ref, -1,
                     sizeof(checked_reuse_ref[0][0]) * 14);
              int checked_reuse_ref_idx[2] = { 0 };

              best_filtered_cost = UINT64_MAX;

              for (int reuse_ccso_idx = 0; reuse_ccso_idx <= 1;
                   reuse_ccso_idx++) {
                bool skip_filter_calculation = false;
                for (int ref_idx = 0; ref_idx <= num_ref_frames; ref_idx++) {
                  ref_frame_ccso_info = NULL;
                  if (reuse_ccso_idx > 0 && ref_idx == 0) continue;
                  // do not use BRU frame as ref for now
                  if (ref_idx == cm->bru.update_ref_idx) {
                    continue;
                  }

                  if (ref_idx > 0) {
                    ref_frame = get_ref_frame_buf(cm, ref_idx - 1);
                    CcsoInfo *ccso_tmp = &ref_frame->ccso_info;
                    if (!ccso_tmp->ccso_enable[plane]) {
                      continue;
                    }
                    ref_frame_ccso_info = ccso_tmp;

                    int repeat_ref = 0;
                    for (int idx = 0;
                         idx < checked_reuse_ref_idx[reuse_ccso_idx]; idx++) {
                      if (checked_reuse_ref[reuse_ccso_idx][idx] ==
                          ref_frame_ccso_info->reuse_root_ref[plane]) {
                        repeat_ref = 1;
                      }
                    }
                    if (repeat_ref) continue;
                    checked_reuse_ref[reuse_ccso_idx]
                                     [checked_reuse_ref_idx[reuse_ccso_idx]++] =
                                         ref_frame_ccso_info
                                             ->reuse_root_ref[plane];
                  }

                  if (reuse_ccso_idx) {
                    if (ref_frame_ccso_info == NULL ||
                        !((scale_idx ==
                           ref_frame_ccso_info->scale_idx[plane]) &&
                          (ccso_bo_only ==
                           ref_frame_ccso_info->ccso_bo_only[plane]) &&
                          (ext_filter_support ==
                           ref_frame_ccso_info->ext_filter_support[plane]) &&
                          (quant_idx ==
                           ref_frame_ccso_info->quant_idx[plane]) &&
                          (edge_clf == ref_frame_ccso_info->edge_clf[plane]) &&
                          (max_band_log2 ==
                           ref_frame_ccso_info->max_band_log2[plane]))) {
                      continue;
                    }
                  }

                  bool check_sb_reuse =
                      check_ccso && (ref_frame_ccso_info != NULL) &&
                      (mi_params->mi_rows == ref_frame->mi_rows) &&
                      (mi_params->mi_cols == ref_frame->mi_cols) &&
                      (xd->plane[plane].subsampling_y ==
                       ref_frame_ccso_info->subsampling_y[plane]) &&
                      (xd->plane[plane].subsampling_x ==
                       ref_frame_ccso_info->subsampling_x[plane]);

                  for (int sb_reuse_idx = 0; sb_reuse_idx <= check_sb_reuse;
                       ++sb_reuse_idx) {
                    if (sb_reuse_idx == 0 && reuse_ccso_idx == 0 && ref_idx > 0)
                      continue;

                    if (sb_reuse_idx) {
                      // Overwrite filter control
                      memcpy(ctx->filter_control,
                             ref_frame_ccso_info->sb_filter_control[plane],
                             sizeof(*ctx->filter_control) * sb_count);
                    } else {
                      int control_idx = 0;
                      for (int y = 0; y < ccso_nvfb; y++) {
                        for (int x = 0; x < ccso_nhfb; x++) {
                          ctx->filter_control[control_idx] = 1;
                          control_idx++;
                        }
                      }
                    }

                    int training_iter_count = 0;
                    bool ccso_enable = true;
                    bool keep_training = true;
                    bool improvement = false;
                    uint64_t prev_total_cost = UINT64_MAX;

                    while (keep_training) {
                      improvement = false;

                      if (!skip_filter_calculation) {
                        if (ccso_enable) {
                          if (!reuse_ccso_idx) {
                            memset(ctx->chroma_error, 0,
                                   sizeof(ctx->chroma_error));
                            memset(ctx->chroma_count, 0,
                                   sizeof(ctx->chroma_count));
                            memset(filter_offset, 0, sizeof(filter_offset));
                            ccso_compute_class_err(
                                ctx, cm, plane, xd, max_band_log2,
                                max_edge_interval, ccso_bo_only);
                            derive_lut_offset(filter_offset, scale_idx,
                                              max_band_log2, max_edge_interval,
                                              ccso_bo_only, ctx->chroma_count,
                                              ctx->chroma_error);
                          } else {
                            memcpy(filter_offset,
                                   ref_frame_ccso_info->filter_offset[plane],
                                   sizeof(filter_offset));
                          }
                        }
                        memcpy(temp_rec_uv_buf, rec_uv,
                               sizeof(*temp_rec_uv_buf) *
                                   xd->plane[0].dst.height * ctx->ccso_stride);
                        if (plane > 0)
                          ccso_try_chroma_filter(
                              ctx, cm, xd, plane, ext_rec_y, temp_rec_uv_buf,
                              ctx->ccso_stride, filter_offset, src_cls0,
                              src_cls1, shift_bits, ccso_bo_only,
                              ctx->ccso_stride, ctx->ccso_stride_ext);
                        else
                          ccso_try_luma_filter(
                              ctx, cm, xd, plane, ext_rec_y, temp_rec_uv_buf,
                              ctx->ccso_stride, filter_offset, src_cls0,
                              src_cls1, shift_bits, ccso_bo_only,
                              ctx->ccso_stride, ctx->ccso_stride_ext);
                        ctx->filtered_dist_frame = 0;
                        compute_distortion(
                            org_uv, ctx->ccso_stride, temp_rec_uv_buf,
                            ctx->ccso_stride, log2_filter_unit_size_y,
                            log2_filter_unit_size_x,
                            cm->mib_size_log2 - AOMMAX(ss_x, ss_y) +
                                MI_SIZE_LOG2,
                            cm, ss_y, ss_x, pic_height_c, pic_width_c,
                            ctx->training_dist_block, ccso_nhfb,
                            &ctx->filtered_dist_frame);
                      }

                      uint64_t cur_total_dist = 0;
                      int cur_total_rate = 0;

                      if (sb_reuse_idx) {
                        get_sb_reuse_dist(
                            cm, xd, plane, ctx->unfiltered_dist_block,
                            ctx->training_dist_block, ctx->filter_control,
                            &cur_total_dist, &cur_total_rate, &ccso_enable,
                            rdmult);
                        cur_total_rate = av1_cost_literal(
                            reuse_ccso_idx ? 0 : aom_ceil_log2(num_ref_frames));
                      } else {
                        derive_blk_md(cm, xd, plane, ctx->unfiltered_dist_block,
                                      ctx->training_dist_block,
                                      ctx->filter_control, &cur_total_dist,
                                      &cur_total_rate, &ccso_enable, rdmult);
                      }

                      if (ccso_enable) {
                        const int lut_bits = count_lut_bits(
                            filter_offset, scale_idx, max_band_log2,
                            max_edge_interval, ccso_bo_only);
                        int cur_total_bits =
                            lut_bits +
                            (ccso_bo_only ? frame_bits_bo_only : frame_bits);

#if CONFIG_CCSO_CLEANUP
                        if (!ccso_bo_only && !quant_sz[scale_idx][quant_idx]) {
                          // remove one frame bit for quant sz is 0 case
                          cur_total_bits -= 1;
                        }
#endif  // CONFIG_CCSO_CLEANUP

                        cur_total_rate +=
                            (reuse_ccso_idx
                                 ? av1_cost_literal(
                                       2 + aom_ceil_log2(num_ref_frames))
                                 : av1_cost_literal(cur_total_bits));
                        const uint64_t cur_total_cost =
                            RDCOST(rdmult, cur_total_rate, cur_total_dist * 16);
                        if (cur_total_cost < prev_total_cost) {
                          prev_total_cost = cur_total_cost;
                          improvement = true;
                        }
                        if (cur_total_cost < best_filtered_cost) {
                          best_filtered_cost = cur_total_cost;
                          best_reuse_ccso = reuse_ccso_idx;
                          best_sb_reuse_ccso = sb_reuse_idx;
                          ctx->best_filter_enabled[plane] = ccso_enable;
                          best_ref_idx = ref_idx - 1;
                          memcpy(ctx->best_filter_offset[plane], filter_offset,
                                 sizeof(filter_offset));
                          best_edge_classifier = edge_clf;
                          memcpy(ctx->best_filter_control, ctx->filter_control,
                                 sizeof(*ctx->filter_control) * sb_count);
                        }
                      }

                      training_iter_count++;
                      if (!improvement ||
                          training_iter_count > CCSO_MAX_ITERATIONS ||
                          sb_reuse_idx || reuse_ccso_idx) {
                        keep_training = false;
                      }
                    }
                  }
                  if (reuse_ccso_idx == 0) skip_filter_calculation = true;
                }
              }

              if (best_filtered_cost < final_filtered_cost) {
                final_filtered_cost = best_filtered_cost;
                ctx->final_reuse_ccso[plane] = best_reuse_ccso;
                ctx->final_sb_reuse_ccso[plane] = best_sb_reuse_ccso;
                ctx->final_filter_enabled[plane] =
                    ctx->best_filter_enabled[plane];
                ctx->final_quant_idx[plane] = quant_idx;
                ctx->final_scale_idx[plane] = scale_idx;
                ctx->final_ext_filter_support[plane] = ext_filter_support;
                ctx->final_ccso_bo_only[plane] = ccso_bo_only;
                final_ref_idx = best_ref_idx;
                memcpy(ctx->final_filter_offset[plane],
                       ctx->best_filter_offset[plane],
                       sizeof(ctx->best_filter_offset[plane]));
                ctx->final_band_log2 = max_band_log2;
                final_edge_classifier = best_edge_classifier;
                memcpy(ctx->final_filter_control, ctx->best_filter_control,
                       sizeof(*ctx->best_filter_control) * sb_count);
              }
            }
          }
        }
      }
    }  // end bo only
  }

  if (best_unfiltered_cost < final_filtered_cost) {
    memset(ctx->final_filter_control, 0,
           sizeof(*ctx->final_filter_control) * sb_count);
    cm->ccso_info.ccso_enable[plane] = false;
  } else {
    cm->ccso_info.ccso_enable[plane] = true;
  }

  if (cm->ccso_info.ccso_enable[plane] &&
      (ctx->final_reuse_ccso[plane] || ctx->final_sb_reuse_ccso[plane])) {
    assert(get_ref_frame_buf(cm, final_ref_idx) != NULL);
    ref_frame_ccso_info = &get_ref_frame_buf(cm, final_ref_idx)->ccso_info;
    cm->ccso_info.ccso_ref_idx[plane] = final_ref_idx;
  }

  cm->ccso_info.sb_reuse_ccso[plane] = false;
  cm->ccso_info.reuse_ccso[plane] = false;
  cm->cur_frame->ccso_info.subsampling_y[plane] =
      xd->plane[plane].subsampling_y;
  cm->cur_frame->ccso_info.subsampling_x[plane] =
      xd->plane[plane].subsampling_x;
  if (cm->ccso_info.ccso_enable[plane]) {
    cm->cur_frame->ccso_info.ccso_enable[plane] = 1;
    cm->cur_frame->ccso_info.reuse_root_ref[plane] =
        cm->current_frame.display_order_hint;
    cm->ccso_info.sb_reuse_ccso[plane] = ctx->final_sb_reuse_ccso[plane];
    const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
    const int bw = mi_size_wide[bsize];
    const int bh = mi_size_high[bsize];
    const int log2_w = CCSO_BLK_SIZE;
    const int log2_h = CCSO_BLK_SIZE;
    const int f_w = 1 << log2_w >> MI_SIZE_LOG2;
    const int f_h = 1 << log2_h >> MI_SIZE_LOG2;
    const int step_h = (bh + f_h - 1) / f_h;
    const int step_w = (bw + f_w - 1) / f_w;

    if (!cm->ccso_info.sb_reuse_ccso[plane]) {
      for (int y_sb = 0; y_sb < ccso_nvfb; y_sb += step_h) {
        for (int x_sb = 0; x_sb < ccso_nhfb; x_sb += step_w) {
          for (int row = y_sb; row < y_sb + step_h; row++) {
            for (int col = x_sb; col < x_sb + step_w; col++) {
              int sb_idx = row * ccso_nhfb + col;
              cm->cur_frame->ccso_info.sb_filter_control[plane][sb_idx] =
                  ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              const int grid_idx_mbmi =
                  (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * row *
                      mi_params->mi_stride +
                  (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * col;
              MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx_mbmi];
              if (plane == AOM_PLANE_Y) {
                // for tile skip, no valid mi exist
                if (cm->bru.enabled &&
                    bru_is_fu_skipped_mbmi(cm, sb_unit_size_x * col,
                                           sb_unit_size_y * row, f_w, f_h)) {
                  assert(ctx->final_filter_control[y_sb * ccso_nhfb + x_sb] ==
                         0);
                  mbmi->ccso_blk_y = 0;
                } else
                  mbmi->ccso_blk_y =
                      ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              } else if (plane == AOM_PLANE_U) {
                if (cm->bru.enabled &&
                    bru_is_fu_skipped_mbmi(cm, sb_unit_size_x * col,
                                           sb_unit_size_y * row, f_w, f_h)) {
                  assert(ctx->final_filter_control[y_sb * ccso_nhfb + x_sb] ==
                         0);
                  mbmi->ccso_blk_u = 0;
                } else
                  mbmi->ccso_blk_u =
                      ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              } else {
                // for tile skip, no valid mi exist
                if (cm->bru.enabled &&
                    bru_is_fu_skipped_mbmi(cm, sb_unit_size_x * col,
                                           sb_unit_size_y * row, f_w, f_h)) {
                  assert(ctx->final_filter_control[y_sb * ccso_nhfb + x_sb] ==
                         0);
                  mbmi->ccso_blk_v = 0;
                } else
                  mbmi->ccso_blk_v =
                      ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              }
              const int ccso_mib_size_y = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2));
              const int ccso_mib_size_x = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2));

              int mi_row = (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * row;
              int mi_col = (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * col;
              for (int j = 0;
                   j < AOMMIN(ccso_mib_size_y, cm->mi_params.mi_rows - mi_row);
                   j++) {
                for (int k = 0; k < AOMMIN(ccso_mib_size_x,
                                           cm->mi_params.mi_cols - mi_col);
                     k++) {
                  const int grid_idx =
                      get_mi_grid_idx(mi_params, mi_row + j, mi_col + k);
                  if (plane == AOM_PLANE_Y) {
                    mi_params->mi_grid_base[grid_idx]->ccso_blk_y =
                        ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
                  } else if (plane == AOM_PLANE_U) {
                    mi_params->mi_grid_base[grid_idx]->ccso_blk_u =
                        ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
                  } else {
                    mi_params->mi_grid_base[grid_idx]->ccso_blk_v =
                        ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
                  }
                }
              }
            }
          }

#if CONFIG_ENTROPY_STATS
          const int ccso_ctx = get_ccso_context(y_sb, x_sb, ccso_nhfb,
                                                ctx->final_filter_control);

          ++td->counts->default_ccso_cnts
                [plane][ccso_ctx]
                [ctx->final_filter_control[y_sb * ccso_nhfb + x_sb]];
#endif
        }
      }
    } else {
      assert(ref_frame_ccso_info != NULL);

      memcpy(cm->cur_frame->ccso_info.sb_filter_control[plane],
             ref_frame_ccso_info->sb_filter_control[plane],
             sizeof(*cm->cur_frame->ccso_info.sb_filter_control[plane]) *
                 sb_count);

      for (int y_sb = 0; y_sb < ccso_nvfb; y_sb++) {
        for (int x_sb = 0; x_sb < ccso_nhfb; x_sb++) {
          const int grid_idx = (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * y_sb *
                                   mi_params->mi_stride +
                               (1 << CCSO_BLK_SIZE >> MI_SIZE_LOG2) * x_sb;
          MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
          if (plane == AOM_PLANE_Y) {
            mbmi->ccso_blk_y =
                ref_frame_ccso_info
                    ->sb_filter_control[plane][y_sb * ccso_nhfb + x_sb];
          } else if (plane == AOM_PLANE_U) {
            mbmi->ccso_blk_u =
                ref_frame_ccso_info
                    ->sb_filter_control[plane][y_sb * ccso_nhfb + x_sb];
          } else {
            mbmi->ccso_blk_v =
                ref_frame_ccso_info
                    ->sb_filter_control[plane][y_sb * ccso_nhfb + x_sb];
          }
        }
      }
    }
    cm->ccso_info.reuse_ccso[plane] = ctx->final_reuse_ccso[plane];
    if (!cm->ccso_info.reuse_ccso[plane]) {
      memcpy(cm->ccso_info.filter_offset[plane],
             ctx->final_filter_offset[plane],
             sizeof(ctx->final_filter_offset[plane]));
      cm->ccso_info.quant_idx[plane] = ctx->final_quant_idx[plane];
      cm->ccso_info.scale_idx[plane] = ctx->final_scale_idx[plane];
      cm->ccso_info.ext_filter_support[plane] =
          ctx->final_ext_filter_support[plane];
      cm->ccso_info.ccso_bo_only[plane] = ctx->final_ccso_bo_only[plane];
      cm->ccso_info.max_band_log2[plane] = ctx->final_band_log2;
      cm->ccso_info.edge_clf[plane] = final_edge_classifier;
      memcpy(cm->cur_frame->ccso_info.filter_offset[plane],
             ctx->final_filter_offset[plane],
             sizeof(ctx->final_filter_offset[plane]));
      cm->cur_frame->ccso_info.quant_idx[plane] = ctx->final_quant_idx[plane];
      cm->cur_frame->ccso_info.scale_idx[plane] = ctx->final_scale_idx[plane];
      cm->cur_frame->ccso_info.ext_filter_support[plane] =
          ctx->final_ext_filter_support[plane];
      cm->cur_frame->ccso_info.ccso_bo_only[plane] =
          ctx->final_ccso_bo_only[plane];
      cm->cur_frame->ccso_info.max_band_log2[plane] = ctx->final_band_log2;
      cm->cur_frame->ccso_info.edge_clf[plane] = final_edge_classifier;
    } else {
      av1_copy_ccso_filters(&cm->ccso_info, ref_frame_ccso_info, plane, 1, 0,
                            sb_count);
      av1_copy_ccso_filters(&cm->cur_frame->ccso_info, ref_frame_ccso_info,
                            plane, 1, 0, sb_count);
    }

    if (cm->ccso_info.reuse_ccso[plane] && cm->ccso_info.sb_reuse_ccso[plane]) {
      cm->cur_frame->ccso_info.reuse_root_ref[plane] =
          ref_frame_ccso_info->reuse_root_ref[plane];
    }
  } else {
    cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
  }
  aom_free(ctx->unfiltered_dist_block);
  aom_free(ctx->training_dist_block);
  aom_free(ctx->filter_control);
  aom_free(ctx->final_filter_control);
  aom_free(temp_rec_uv_buf);
  aom_free(ctx->best_filter_control);
  aom_free(src_cls0);
  aom_free(src_cls1);
  for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; d0++) {
    for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; d1++) {
      for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
        aom_free(ctx->total_class_err[d0][d1][band_num]);
        aom_free(ctx->total_class_cnt[d0][d1][band_num]);
      }
    }
  }
  for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
    aom_free(ctx->total_class_err_bo[band_num]);
    aom_free(ctx->total_class_cnt_bo[band_num]);
  }
}

/* Derive the look-up table for a frame */
void ccso_search(AV1_COMMON *cm, MACROBLOCKD *xd, int rdmult,
                 const uint16_t *ext_rec_y, uint16_t *rec_uv[3],
                 uint16_t *org_uv[3], bool error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                 ,
                 ThreadData *td
#endif
) {
  int rdmult_weight = clamp(cm->quant_params.base_qindex >> 3, 1, 37);
  int64_t rdmult_temp = (int64_t)rdmult * (int64_t)rdmult_weight;
  if (rdmult_temp >= INT_MAX) {
    cm->ccso_info.ccso_frame_flag = false;
    cm->ccso_info.ccso_enable[0] = cm->ccso_info.ccso_enable[1] =
        cm->ccso_info.ccso_enable[2] = 0;
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
      cm->ccso_info.sb_reuse_ccso[plane] = false;
      cm->ccso_info.reuse_ccso[plane] = false;
    }
    return;
  }
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                       NULL);

  CcsoCtx *const ctx = aom_calloc(1, sizeof(CcsoCtx));
  ctx->ccso_stride = xd->plane[0].dst.width;
  ctx->ccso_stride_ext = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  derive_ccso_filter(ctx, cm, AOM_PLANE_Y, xd, org_uv[AOM_PLANE_Y], ext_rec_y,
                     rec_uv[AOM_PLANE_Y], rdmult, error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                     ,
                     td
#endif
  );

  cm->ccso_info.ccso_frame_flag = cm->ccso_info.ccso_enable[0];
  if (num_planes > 1) {
    rdmult = (rdmult * 7) >> 3;
    derive_ccso_filter(ctx, cm, AOM_PLANE_U, xd, org_uv[AOM_PLANE_U], ext_rec_y,
                       rec_uv[AOM_PLANE_U], rdmult, error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                       ,
                       td
#endif
    );
    derive_ccso_filter(ctx, cm, AOM_PLANE_V, xd, org_uv[AOM_PLANE_V], ext_rec_y,
                       rec_uv[AOM_PLANE_V], rdmult, error_resilient_frame_seen
#if CONFIG_ENTROPY_STATS
                       ,
                       td
#endif
    );
    cm->ccso_info.ccso_frame_flag |= cm->ccso_info.ccso_enable[1];
    cm->ccso_info.ccso_frame_flag |= cm->ccso_info.ccso_enable[2];
  }
  aom_free(ctx);
}
