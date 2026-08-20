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
#include <stddef.h>
#include <string.h>
#include <float.h>

#include "av2/common/enums.h"
#include "config/avm_dsp_rtcd.h"
#include "config/avm_scale_rtcd.h"

#include "avm/avm_integer.h"
#include "avm_ports/system_state.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/reconinter.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/pickccso.h"

const int ccso_offset[8] = { -10, -7, -3, -1, 0, 1, 3, 7 };
const int ccso_scale[4] = { 1, 2, 3, 4 };

static INLINE bool reuse_ccso_class_info(const AV2_COMMON *cm) {
  return !cm->bru.enabled;
}

// Resets per-frame state in a persistent CcsoCtx while preserving all
// allocated buffer pointers and size-tracking fields.
static void ccso_ctx_reset(CcsoCtx *ctx) {
  memset(ctx, 0, offsetof(CcsoCtx, class_err_slab));
}

void ccso_derive_src_block_c(const uint16_t *src_y, uint8_t *const src_cls0,
                             uint8_t *const src_cls1, const int src_y_stride,
                             const int src_cls_stride, const int x, const int y,
                             const int pic_width, const int pic_height,
                             const int y_uv_hscale, const int y_uv_vscale,
                             const int qstep, const int neg_qstep,
                             const int *src_loc, const int blk_size_x,
                             const int blk_size_y, const int edge_clf) {
  int src_cls[2];
  const int y_end = AVMMIN(pic_height - y, blk_size_y);
  const int x_end = AVMMIN(pic_width - x, blk_size_x);
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
static void ccso_derive_src_info(AV2_COMMON *cm, CcsoCtx *ctx,
                                 MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  uint8_t *src_cls0 = ctx->src_cls0;
  uint8_t *src_cls1 = ctx->src_cls1;
  const uint16_t qstep = quant_sz[ctx->scale_idx][ctx->quant_idx];
  const uint8_t filter_sup = ctx->ext_filter_support;
  const int edge_clf = ctx->edge_clf;
  const int ccso_stride = ctx->ccso_stride;
  const int ccso_stride_ext = ctx->ccso_stride_ext;
  const int pic_height = xd->plane[s->plane].dst.height;
  const int pic_width = xd->plane[s->plane].dst.width;
  const int y_uv_hscale = xd->plane[s->plane].subsampling_x;
  const int y_uv_vscale = xd->plane[s->plane].subsampling_y;
  const uint16_t *src_y = s->ext_rec_y;
  const int blk_log2_y = s->log2_filter_unit_size_y;
  const int blk_log2_x = s->log2_filter_unit_size_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int unit_log2_x = AVMMIN(s->log2_proc_unit_size, blk_log2_x);
  const int unit_size_x = 1 << unit_log2_x;
  const int unit_log2_y = AVMMIN(s->log2_proc_unit_size, blk_log2_y);
  const int unit_size_y = 1 << unit_log2_y;
  const int neg_qstep = qstep * -1;
  int src_loc[2];

  derive_ccso_sample_pos(src_loc, ccso_stride_ext, filter_sup);
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
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
      const int y_end = AVMMIN(pic_height - y, blk_size_y);
      const int x_end = AVMMIN(pic_width - x, blk_size_x);
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
static void ccso_pre_compute_class_err(const AV2_COMMON *cm, CcsoCtx *ctx,
                                       MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  const uint8_t shift_bits = ctx->shift_bits;
  const uint8_t init_shift_bits = ctx->init_shift_bits;
  const int sb_count = s->sb_count;
  const int max_band = 1 << (cm->seq_params.bit_depth - shift_bits);

  for (int d0 = 0; d0 < ctx->max_edge_interval; d0++) {
    for (int d1 = 0; d1 < ctx->max_edge_interval; d1++) {
      av2_zero_array(ctx->total_class_err[d0][d1][0], max_band * sb_count);
      av2_zero_array(ctx->total_class_cnt[d0][d1][0], max_band * sb_count);
    }
  }
  // Error and count of previously computed bands are reused to compute
  // error and count of the new lower bands.
  if ((init_shift_bits != shift_bits) && reuse_ccso_class_info(cm)) {
    const int num_bins_to_be_summed = 1 << (shift_bits - init_shift_bits);
    for (int d0 = 0; d0 < ctx->max_edge_interval; d0++) {
      for (int d1 = 0; d1 < ctx->max_edge_interval; d1++) {
        for (int fb_cnt = 0; fb_cnt < sb_count; fb_cnt++) {
          for (int band_num = 0; band_num < max_band; band_num++) {
            const int bin_index = (band_num * num_bins_to_be_summed);
            for (int cur_bin_index = bin_index;
                 cur_bin_index < bin_index + num_bins_to_be_summed;
                 cur_bin_index++) {
              ctx->total_class_err[d0][d1][band_num][fb_cnt] +=
                  ctx->reuse_total_class_err[d0][d1][cur_bin_index][fb_cnt];

              ctx->total_class_cnt[d0][d1][band_num][fb_cnt] +=
                  ctx->reuse_total_class_cnt[d0][d1][cur_bin_index][fb_cnt];
            }
          }
        }
      }
    }
    return;
  }

  uint8_t *src_cls0 = ctx->src_cls0;
  uint8_t *src_cls1 = ctx->src_cls1;
  const int pic_height = xd->plane[s->plane].dst.height;
  const int pic_width = xd->plane[s->plane].dst.width;
  const int y_uv_hscale = xd->plane[s->plane].subsampling_x;
  const int y_uv_vscale = xd->plane[s->plane].subsampling_y;
  const uint16_t *src_y = s->ext_rec_y;
  const uint16_t *ref = s->org_uv;
  const uint16_t *dst = s->rec_uv;
  const int blk_log2_y = s->log2_filter_unit_size_y;
  const int blk_log2_x = s->log2_filter_unit_size_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  const int scaled_stride = (ctx->ccso_stride << y_uv_vscale);
  const int unit_log2_x = AVMMIN(s->log2_proc_unit_size, blk_log2_x);
  const int unit_size_x = 1 << unit_log2_x;
  const int unit_log2_y = AVMMIN(s->log2_proc_unit_size, blk_log2_y);
  const int unit_size_y = 1 << unit_log2_y;
  int fb_idx = 0;
  uint8_t cur_src_cls0;
  uint8_t cur_src_cls1;

  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      fb_idx++;
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;  // FSU skip
      }
      const int y_end = AVMMIN(pic_height - y, blk_size_y);
      const int x_end = AVMMIN(pic_width - x, blk_size_x);
      // reset temp pointers for sbs
      const uint16_t *ref_unit = ref;
      const uint16_t *dst_unit = dst;
      const uint16_t *src_y_unit = src_y;
      const uint8_t *src_unit0 = src_cls0;
      const uint8_t *src_unit1 = src_cls1;
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          const int y_end_unit = AVMMIN(pic_height - y - unit_y, unit_size_y);
          const int x_end_unit = AVMMIN(pic_width - x - unit_x, unit_size_x);
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
            }
            ref_unit += ctx->ccso_stride;
            dst_unit += ctx->ccso_stride;
            src_y_unit +=
                scaled_ext_stride;  // scaled means already done + y_uv_vscale
            src_unit0 += scaled_stride;
            src_unit1 += scaled_stride;
          }
          ref_unit -= ctx->ccso_stride * y_end_unit;
          dst_unit -= ctx->ccso_stride * y_end_unit;
          src_y_unit -= scaled_ext_stride * y_end_unit;
          src_unit0 -= scaled_stride * y_end_unit;
          src_unit1 -= scaled_stride * y_end_unit;
        }
        // move to next unit (sb) row
        ref_unit += unit_size_y * ctx->ccso_stride;
        dst_unit += unit_size_y * ctx->ccso_stride;
        src_y_unit += scaled_ext_stride * unit_size_y;
        src_unit0 += scaled_stride * unit_size_y;
        src_unit1 += scaled_stride * unit_size_y;
      }
    }
    ref += (ctx->ccso_stride << blk_log2_y);
    dst += (ctx->ccso_stride << blk_log2_y);
    src_y += (ctx->ccso_stride_ext << (blk_log2_y + y_uv_vscale));
    src_cls0 += (ctx->ccso_stride << (blk_log2_y + y_uv_vscale));
    src_cls1 += (ctx->ccso_stride << (blk_log2_y + y_uv_vscale));
  }

  // Store the computed error and count to reuse the same for a given band
  if (reuse_ccso_class_info(cm)) {
    for (int d0 = 0; d0 < ctx->max_edge_interval; ++d0) {
      for (int d1 = 0; d1 < ctx->max_edge_interval; ++d1) {
        av2_copy_array(ctx->reuse_total_class_err[d0][d1][0],
                       ctx->total_class_err[d0][d1][0], max_band * sb_count);
        av2_copy_array(ctx->reuse_total_class_cnt[d0][d1][0],
                       ctx->total_class_cnt[d0][d1][0], max_band * sb_count);
      }
    }
  }
}

// pre compute classes for band offset only option
static void ccso_pre_compute_class_err_bo(const AV2_COMMON *cm, CcsoCtx *ctx,
                                          MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  const uint8_t shift_bits = ctx->shift_bits;
  const int max_band = 1 << ctx->max_band_log2;
  const int pic_height = xd->plane[s->plane].dst.height;
  const int pic_width = xd->plane[s->plane].dst.width;
  const int y_uv_hscale = xd->plane[s->plane].subsampling_x;
  const int y_uv_vscale = xd->plane[s->plane].subsampling_y;
  const uint16_t *src_y = s->ext_rec_y;
  const uint16_t *ref = s->org_uv;
  const uint16_t *dst = s->rec_uv;
  const int blk_log2_y = s->log2_filter_unit_size_y;
  const int blk_log2_x = s->log2_filter_unit_size_x;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  const int unit_log2_x = AVMMIN(s->log2_proc_unit_size, blk_log2_x);
  const int unit_size_x = 1 << unit_log2_x;
  const int unit_log2_y = AVMMIN(s->log2_proc_unit_size, blk_log2_y);
  const int unit_size_y = 1 << unit_log2_y;
  int fb_idx = 0;

  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  av2_zero_array(ctx->total_class_err_bo[0], max_band * s->sb_count);
  av2_zero_array(ctx->total_class_cnt_bo[0], max_band * s->sb_count);
  for (int y = 0; y < pic_height; y += blk_size_y) {
    for (int x = 0; x < pic_width; x += blk_size_x) {
      fb_idx++;
      assert(ctx->filter_control);
      if (cm->bru.enabled && !ctx->filter_control[fb_idx - 1]) {
        continue;
      }
      const int y_end = AVMMIN(pic_height - y, blk_size_y);
      const int x_end = AVMMIN(pic_width - x, blk_size_x);
      assert(ctx->filter_control);
      // reset temp pointers for sbs
      const uint16_t *ref_unit = ref;
      const uint16_t *dst_unit = dst;
      const uint16_t *src_y_unit = src_y;
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          const int y_end_unit = AVMMIN(pic_height - y - unit_y, unit_size_y);
          const int x_end_unit = AVMMIN(pic_width - x - unit_x, unit_size_x);
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
            }
            ref_unit += ctx->ccso_stride;
            dst_unit += ctx->ccso_stride;
            src_y_unit += scaled_ext_stride;
          }
          ref_unit -= ctx->ccso_stride * y_end_unit;
          dst_unit -= ctx->ccso_stride * y_end_unit;
          src_y_unit -= scaled_ext_stride * y_end_unit;
        }

        // move to next unit (sb) row
        ref_unit += unit_size_y * ctx->ccso_stride;
        dst_unit += unit_size_y * ctx->ccso_stride;
        src_y_unit += unit_size_y * scaled_ext_stride;
      }
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
  const int y_end = AVMMIN(pic_height - y, blk_size_y);
  const int x_end = AVMMIN(pic_width - x, blk_size_x);
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
  const int y_end = AVMMIN(pic_height - y, blk_size_y);
  const int x_end = AVMMIN(pic_width - x, blk_size_x);
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
static void ccso_try_luma_filter(AV2_COMMON *cm, CcsoCtx *ctx, MACROBLOCKD *xd,
                                 const int8_t *filter_offset) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  const uint16_t *src_y = s->ext_rec_y;
  const int blk_log2 = s->log2_filter_unit_size_y;
  const int plane = s->plane;
  uint16_t *dst_yuv = ctx->temp_rec_uv_buf;
  const int dst_stride = ctx->ccso_stride;
  uint8_t *src_cls0 = ctx->src_cls0;
  uint8_t *src_cls1 = ctx->src_cls1;
  const uint8_t shift_bits = ctx->shift_bits;
  const uint8_t ccso_bo_only = ctx->ccso_bo_only;
  const int ccso_stride = ctx->ccso_stride;
  const int ccso_stride_ext = ctx->ccso_stride_ext;
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
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
      const int y_end = AVMMIN(pic_height - y, blk_size);
      const int x_end = AVMMIN(pic_width - x, blk_size);
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
        }
        dst_unit += (dst_stride << unit_log2);
        src_y_unit += (ccso_stride_ext << unit_log2);
        src_unit0 += (ccso_stride << unit_log2);
        src_unit1 += (ccso_stride << unit_log2);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_stride_ext << blk_log2);
    src_cls0 += (ccso_stride << blk_log2);
    src_cls1 += (ccso_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component at encoder (high bit-depth) */
static void ccso_try_chroma_filter(AV2_COMMON *cm, CcsoCtx *ctx,
                                   MACROBLOCKD *xd,
                                   const int8_t *filter_offset) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  const uint16_t *src_y = s->ext_rec_y;
  const int blk_log2_y = s->log2_filter_unit_size_y;
  const int blk_log2_x = s->log2_filter_unit_size_x;
  const int plane = s->plane;
  uint16_t *dst_yuv = ctx->temp_rec_uv_buf;
  const int dst_stride = ctx->ccso_stride;
  uint8_t *src_cls0 = ctx->src_cls0;
  uint8_t *src_cls1 = ctx->src_cls1;
  const uint8_t shift_bits = ctx->shift_bits;
  const uint8_t ccso_bo_only = ctx->ccso_bo_only;
  const int ccso_stride = ctx->ccso_stride;
  const int ccso_stride_ext = ctx->ccso_stride_ext;
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int blk_size_y = 1 << blk_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  int fb_idx = 0;

  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  int unit_log2_x = cm->mib_size_log2 + MI_SIZE_LOG2 - y_uv_hscale;
  if (unit_log2_x > blk_log2_x) {
    unit_log2_x = blk_log2_x;
  }
  const int unit_size_x = 1 << unit_log2_x;
  int unit_log2_y = cm->mib_size_log2 + MI_SIZE_LOG2 - y_uv_vscale;
  if (unit_log2_y > blk_log2_y) {
    unit_log2_y = blk_log2_y;
  }
  const int unit_size_y = 1 << unit_log2_y;
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
      const int y_end = AVMMIN(pic_height - y, blk_size_y);
      const int x_end = AVMMIN(pic_width - x, blk_size_x);
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
        }
        dst_unit += (dst_stride << unit_log2_y);
        src_y_unit += (ccso_stride_ext << (unit_log2_y + y_uv_vscale));
        src_unit0 += (ccso_stride << (unit_log2_y + y_uv_vscale));
        src_unit1 += (ccso_stride << (unit_log2_y + y_uv_vscale));
      }
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
static void compute_distortion(const AV2_COMMON *cm, const CcsoCtx *ctx,
                               MACROBLOCKD *xd, const uint16_t *rec16,
                               uint64_t *distortion_buf,
                               uint64_t *total_distortion) {
  const CcsoCtxCommon *s = &ctx->ccso_cm;
  const uint16_t *org = s->org_uv;
  const int blk_log2_y = s->log2_filter_unit_size_y;
  const int blk_log2_x = s->log2_filter_unit_size_x;
  const int plane = s->plane;
  const int distortion_buf_stride = s->ccso_nhfb;
  const int org_stride = ctx->ccso_stride;
  const int rec_stride = ctx->ccso_stride;
  const int subsampling_y = xd->plane[plane].subsampling_y;
  const int subsampling_x = xd->plane[plane].subsampling_x;
  const int height = xd->plane[plane].dst.crop_height;
  const int width = xd->plane[plane].dst.crop_width;
  const int unit_log2_x = AVMMIN(s->log2_proc_unit_size, blk_log2_x);
  const int unit_log2_y = AVMMIN(s->log2_proc_unit_size, blk_log2_y);
  const int unit_size_x = 1 << unit_log2_x;
  const int unit_size_y = 1 << unit_log2_y;
  const int blk_size_x = 1 << blk_log2_x;
  const int blk_size_y = 1 << blk_log2_y;

  *total_distortion = 0;
  for (int y = 0; y < height; y += blk_size_y) {
    for (int x = 0; x < width; x += blk_size_x) {
      const int h_scale = MI_SIZE_LOG2 - subsampling_x;
      const int v_scale = MI_SIZE_LOG2 - subsampling_y;
      // check BRU skip in entire CCSO FSU
      if (bru_is_fu_skipped_mbmi(cm, x >> h_scale, y >> v_scale,
                                 blk_size_x >> h_scale,
                                 blk_size_y >> v_scale)) {
        distortion_buf[(y >> blk_log2_y) * distortion_buf_stride +
                       (x >> blk_log2_x)] = 0;
        continue;
      }
      // All unified into pixel size
      uint64_t sb_ssd = 0;
      const uint16_t *org_unit = org;
      const uint16_t *rec_unit = rec16;
      const int y_end = AVMMIN(height - y, blk_size_y);
      const int x_end = AVMMIN(width - x, blk_size_x);
      for (int unit_y = 0; unit_y < y_end; unit_y += unit_size_y) {
        for (int unit_x = 0; unit_x < x_end; unit_x += unit_size_x) {
          // skip if unit skip
          const int mbmi_idx = get_mi_grid_idx(
              &cm->mi_params, (y + unit_y) >> v_scale, (x + unit_x) >> h_scale);
          if (cm->bru.enabled &&
              cm->mi_params.mi_grid_base[mbmi_idx]->sb_active_mode !=
                  BRU_ACTIVE_SB) {
            continue;
          }
          // skip if unit skip
          sb_ssd += compute_distortion_block(
              org_unit, org_stride, rec_unit, rec_stride, x + unit_x,
              y + unit_y, unit_log2_y, unit_log2_x, height, width);
        }
        // offset org, rec16 here
        org_unit += (org_stride << unit_log2_x);
        rec_unit += (rec_stride << unit_log2_x);
      }
      distortion_buf[(y >> blk_log2_y) * distortion_buf_stride +
                     (x >> blk_log2_x)] = sb_ssd;
      *total_distortion += sb_ssd;
    }
    org += (org_stride << blk_log2_y);
    rec16 += (rec_stride << blk_log2_y);
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
static void derive_blk_md(AV2_COMMON *cm, CcsoCtx *ctx, MACROBLOCKD *xd,
                          uint64_t *cur_total_dist, int *cur_total_rate,
                          bool *filter_enable) {
  const CcsoCtxCommon *s = &ctx->ccso_cm;
  bool *m_filter_control = ctx->filter_control;
  const int plane = s->plane;
  const int ccso_blk_size = s->ccso_blk_size;
  const int ccso_nhfb = s->ccso_nhfb;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int sb_unit_size_x =
      (1 << s->log2_filter_unit_size_x >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << s->log2_filter_unit_size_y >> (MI_SIZE_LOG2 - ss_y));
  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int blk_size_y = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  avm_cdf_prob ccso_cdf[CCSO_CONTEXT][CDF_SIZE(2)];
  bool cur_filter_enabled = false;
  int sb_idx = 0;

  *cur_total_dist = 0;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av2_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      av2_tile_set_col(&tile_info, cm, tile_col);

      av2_copy(ccso_cdf, cm->fc->ccso_cdf[plane]);

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
          av2_cost_tokens_from_cdf(cost_from_cdf[ccso_ctx], ccso_cdf[ccso_ctx],
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
              ssd = s->unfiltered_dist_block[sb_idx];
            } else {
              ssd = ctx->training_dist_block[sb_idx];
            }
            ssd = ROUND_POWER_OF_TWO(ssd, (xd->bd - 8) * 2);

            const uint64_t rd_cost =
                RDCOST(s->rdmult, cost_from_cdf[ccso_ctx][cur_filter_control],
                       ssd * 16);
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

static void get_sb_reuse_dist(AV2_COMMON *cm, CcsoCtx *ctx, MACROBLOCKD *xd,
                              uint64_t *cur_total_dist, int *cur_total_rate,
                              bool *filter_enable) {
  const CcsoCtxCommon *s = &ctx->ccso_cm;
  const bool *m_filter_control = ctx->filter_control;
  const int plane = s->plane;
  const int ccso_blk_size = s->ccso_blk_size;
  const int ccso_nhfb = s->ccso_nhfb;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int sb_unit_size_x =
      (1 << s->log2_filter_unit_size_x >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << s->log2_filter_unit_size_y >> (MI_SIZE_LOG2 - ss_y));
  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int blk_size_y = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  bool cur_filter_enabled = false;
  int sb_idx = 0;

  *cur_total_dist = 0;
  *cur_total_rate = 0;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av2_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      av2_tile_set_col(&tile_info, cm, tile_col);

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
            ssd = ctx->training_dist_block[sb_idx];
          else
            ssd = s->unfiltered_dist_block[sb_idx];

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
static void ccso_compute_class_err(CcsoCtx *ctx) {
  const int max_edge_interval = ctx->max_edge_interval;
  const int fb_count = ctx->ccso_cm.sb_count;
  const int max_band = 1 << ctx->max_band_log2;

  av2_zero_array(ctx->chroma_error, max_band * 16);
  av2_zero_array(ctx->chroma_count, max_band * 16);
  for (int fb_idx = 0; fb_idx < fb_count; fb_idx++) {
    if (!ctx->filter_control[fb_idx]) continue;
    if (ctx->ccso_bo_only) {
      int d0 = 0;
      int d1 = 0;
      for (int band_num = 0; band_num < max_band; band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        ctx->chroma_error[lut_idx_ext] +=
            ctx->total_class_err_bo[band_num][fb_idx];
        ctx->chroma_count[lut_idx_ext] +=
            ctx->total_class_cnt_bo[band_num][fb_idx];
      }
    } else {
      for (int d0 = 0; d0 < max_edge_interval; d0++) {
        for (int d1 = 0; d1 < max_edge_interval; d1++) {
          for (int band_num = 0; band_num < max_band; band_num++) {
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
static INLINE int count_lut_bits(const CcsoCtx *ctx,
                                 const int8_t *filter_offset) {
  int ccso_offset_reordered[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  for (int idx = 0; idx < 8; ++idx)
    ccso_offset_reordered[idx] =
        ccso_offset_reordered[idx] * ccso_scale[ctx->scale_idx];
  int temp_bits = 0;
  int num_edge_offset_intervals =
      ctx->ccso_bo_only ? 1 : ctx->max_edge_interval;
  for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
    for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
      for (int band_num = 0; band_num < (1 << ctx->max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        for (int idx = 0; idx < 7; ++idx) {
          temp_bits++;
          if (ccso_offset_reordered[idx] == filter_offset[lut_idx_ext]) break;
        }
      }
    }
  }
  return temp_bits;
}

/* Derive the offset value in the look-up table */
static void derive_lut_offset(CcsoCtx *ctx, int8_t *filter_offset) {
  const int *chroma_count = ctx->chroma_count;
  float temp_offset = 0;
  int num_edge_offset_intervals =
      ctx->ccso_bo_only ? 1 : ctx->max_edge_interval;
  int this_ccso_offset[8] = { 0 };

  av2_zero_array(filter_offset, (1 << ctx->max_band_log2) * 16);
  for (int idx = 0; idx < 8; ++idx)
    this_ccso_offset[idx] = ccso_offset[idx] * ccso_scale[ctx->scale_idx];
  for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
    for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
      for (int band_num = 0; band_num < (1 << ctx->max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        if (chroma_count[lut_idx_ext]) {
          temp_offset =
              (float)ctx->chroma_error[lut_idx_ext] / chroma_count[lut_idx_ext];
          if ((temp_offset < this_ccso_offset[0]) ||
              (temp_offset >= this_ccso_offset[7])) {
            filter_offset[lut_idx_ext] = clamp(
                (int)temp_offset, this_ccso_offset[0], this_ccso_offset[7]);
          } else {
            for (int offset_idx = 0; offset_idx < 7; offset_idx++) {
              if ((temp_offset >= this_ccso_offset[offset_idx]) &&
                  (temp_offset <= this_ccso_offset[offset_idx + 1])) {
                if (fabs(temp_offset - this_ccso_offset[offset_idx]) >
                    fabs(temp_offset - this_ccso_offset[offset_idx + 1])) {
                  filter_offset[lut_idx_ext] = this_ccso_offset[offset_idx + 1];
                } else {
                  filter_offset[lut_idx_ext] = this_ccso_offset[offset_idx];
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

// Allocates buffers required for ccso parameter rdo search
void av2_ccso_alloc_search_buffers(AV2_COMMON *cm, CcsoCtx *ctx,
                                   int ccso_stride, int ccso_height,
                                   int sb_count) {
  const size_t luma_size = (size_t)ccso_height * ccso_stride;

  if (sb_count > ctx->alloc_sb_count) {
    ctx->alloc_sb_count = 0;
    avm_free(ctx->class_err_slab);
    CHECK_MEM_ERROR(cm, ctx->class_err_slab,
                    avm_malloc(sizeof(*ctx->class_err_slab) *
                               CCSO_CLASS_STATS_ENTRIES * sb_count));

    avm_free(ctx->class_cnt_slab);
    CHECK_MEM_ERROR(cm, ctx->class_cnt_slab,
                    avm_malloc(sizeof(*ctx->class_cnt_slab) *
                               CCSO_CLASS_STATS_ENTRIES * sb_count));

    avm_free(ctx->class_err_bo_slab);
    CHECK_MEM_ERROR(
        cm, ctx->class_err_bo_slab,
        avm_malloc(sizeof(*ctx->class_err_bo_slab) * CCSO_BAND_NUM * sb_count));

    avm_free(ctx->class_cnt_bo_slab);
    CHECK_MEM_ERROR(
        cm, ctx->class_cnt_bo_slab,
        avm_malloc(sizeof(*ctx->class_cnt_bo_slab) * CCSO_BAND_NUM * sb_count));

    avm_free(ctx->training_dist_block);
    CHECK_MEM_ERROR(cm, ctx->training_dist_block,
                    avm_malloc(sb_count * sizeof(*ctx->training_dist_block)));

    avm_free(ctx->filter_control);
    CHECK_MEM_ERROR(cm, ctx->filter_control,
                    avm_malloc(sb_count * sizeof(*ctx->filter_control)));

    avm_free(ctx->best_filter_control);
    CHECK_MEM_ERROR(cm, ctx->best_filter_control,
                    avm_malloc(sb_count * sizeof(*ctx->best_filter_control)));

    avm_free(ctx->final_filter_control);
    CHECK_MEM_ERROR(cm, ctx->final_filter_control,
                    avm_malloc(sb_count * sizeof(*ctx->final_filter_control)));

    ctx->alloc_sb_count = sb_count;
  }

  if (reuse_ccso_class_info(cm) && sb_count > ctx->alloc_reuse_sb_count) {
    ctx->alloc_reuse_sb_count = 0;
    avm_free(ctx->reuse_class_err_slab);
    CHECK_MEM_ERROR(cm, ctx->reuse_class_err_slab,
                    avm_malloc(sizeof(*ctx->reuse_class_err_slab) *
                               CCSO_CLASS_STATS_ENTRIES * sb_count));

    avm_free(ctx->reuse_class_cnt_slab);
    CHECK_MEM_ERROR(cm, ctx->reuse_class_cnt_slab,
                    avm_malloc(sizeof(*ctx->reuse_class_cnt_slab) *
                               CCSO_CLASS_STATS_ENTRIES * sb_count));

    ctx->alloc_reuse_sb_count = sb_count;
  }

  if (luma_size > ctx->alloc_luma_size) {
    ctx->alloc_luma_size = 0;
    avm_free(ctx->temp_rec_uv_buf);
    CHECK_MEM_ERROR(cm, ctx->temp_rec_uv_buf,
                    avm_malloc(luma_size * sizeof(*ctx->temp_rec_uv_buf)));

    avm_free(ctx->src_cls0);
    CHECK_MEM_ERROR(cm, ctx->src_cls0,
                    avm_malloc(luma_size * sizeof(*ctx->src_cls0)));

    avm_free(ctx->src_cls1);
    CHECK_MEM_ERROR(cm, ctx->src_cls1,
                    avm_malloc(luma_size * sizeof(*ctx->src_cls1)));

    ctx->alloc_luma_size = luma_size;
  }

  // Re-establish internal pointer layout (depends on current sb_count).
  int *p = ctx->class_err_slab;
  for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; ++d0)
    for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; ++d1)
      for (int band_num = 0; band_num < CCSO_BAND_NUM;
           ++band_num, p += sb_count)
        ctx->total_class_err[d0][d1][band_num] = p;

  p = ctx->class_cnt_slab;
  for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; ++d0)
    for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; ++d1)
      for (int band_num = 0; band_num < CCSO_BAND_NUM;
           ++band_num, p += sb_count)
        ctx->total_class_cnt[d0][d1][band_num] = p;

  p = ctx->class_err_bo_slab;
  for (int band_num = 0; band_num < CCSO_BAND_NUM; ++band_num, p += sb_count)
    ctx->total_class_err_bo[band_num] = p;

  p = ctx->class_cnt_bo_slab;
  for (int band_num = 0; band_num < CCSO_BAND_NUM; ++band_num, p += sb_count)
    ctx->total_class_cnt_bo[band_num] = p;

  if (reuse_ccso_class_info(cm)) {
    p = ctx->reuse_class_err_slab;
    for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; ++d0)
      for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; ++d1)
        for (int band_num = 0; band_num < CCSO_BAND_NUM;
             ++band_num, p += sb_count)
          ctx->reuse_total_class_err[d0][d1][band_num] = p;

    p = ctx->reuse_class_cnt_slab;
    for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; ++d0)
      for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; ++d1)
        for (int band_num = 0; band_num < CCSO_BAND_NUM;
             ++band_num, p += sb_count)
          ctx->reuse_total_class_cnt[d0][d1][band_num] = p;
  }
}

// Frees all persistent CCSO encoder buffers (pixel buffers + RDO scratch).
// Call once at encoder close via av2_remove_compressor.
void av2_ccso_ctx_free(AV2_COMP *cpi) {
  CcsoCtx *ctx = &cpi->ccso_ctx;
  avm_free(ctx->class_err_slab);
  avm_free(ctx->class_cnt_slab);
  avm_free(ctx->class_err_bo_slab);
  avm_free(ctx->class_cnt_bo_slab);
  avm_free(ctx->reuse_class_err_slab);
  avm_free(ctx->reuse_class_cnt_slab);
  avm_free(ctx->training_dist_block);
  avm_free(ctx->filter_control);
  avm_free(ctx->best_filter_control);
  avm_free(ctx->final_filter_control);
  avm_free(ctx->temp_rec_uv_buf);
  avm_free(ctx->src_cls0);
  avm_free(ctx->src_cls1);
  avm_free(cpi->unfiltered_dist_block);
  avm_free(cpi->ccso_ext_rec_y);
  for (int plane = 0; plane < CCSO_NUM_COMPONENTS; ++plane) {
    avm_free(cpi->ccso_rec_uv[plane]);
    avm_free(cpi->ccso_org_uv[plane]);
  }
}

// Runs the RD-cost training loop for one (candidate, reuse_ccso_idx,
// ref_idx, sb_reuse_idx) combination and updates s->best if it improves
// on the current best for this max_band_log2 iteration.
static AVM_INLINE void run_training(AV2_COMMON *cm, CcsoCtx *ctx,
                                    MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  int8_t *const filter_offset = ctx->filter_offset;
  const int plane = s->plane;
  int training_iter_count = 0;
  bool ccso_enable = true;
  bool keep_training = true;
  bool improvement = false;
  uint64_t filtered_dist_frame;
  uint64_t prev_total_cost = UINT64_MAX;

  while (keep_training) {
    improvement = false;

    if (!ctx->skip_filter_calculation) {
      if (ccso_enable) {
        if (!ctx->reuse_ccso_idx) {
          ccso_compute_class_err(ctx);
          derive_lut_offset(ctx, filter_offset);
        } else {
          const int max_band = 1 << ctx->max_band_log2;
          av2_copy_array(filter_offset,
                         ctx->ref_frame_ccso_info->filter_offset[plane],
                         max_band * 16);
        }
      }
      av2_copy_array(ctx->temp_rec_uv_buf, s->rec_uv,
                     xd->plane[plane].dst.height * ctx->ccso_stride);
      if (plane > 0)
        ccso_try_chroma_filter(cm, ctx, xd, filter_offset);
      else
        ccso_try_luma_filter(cm, ctx, xd, filter_offset);

      compute_distortion(cm, ctx, xd, ctx->temp_rec_uv_buf,
                         ctx->training_dist_block, &filtered_dist_frame);
    }

    uint64_t cur_total_dist = 0;
    int cur_total_rate = 0;

    if (ctx->sb_reuse_idx) {
      get_sb_reuse_dist(cm, ctx, xd, &cur_total_dist, &cur_total_rate,
                        &ccso_enable);
      cur_total_rate = av2_cost_literal(
          ctx->reuse_ccso_idx ? 0 : avm_ceil_log2(s->num_ref_frames));
    } else {
      derive_blk_md(cm, ctx, xd, &cur_total_dist, &cur_total_rate,
                    &ccso_enable);
    }

    if (ccso_enable) {
      const int lut_bits = count_lut_bits(ctx, filter_offset);
      int cur_total_bits = lut_bits + (ctx->ccso_bo_only ? s->frame_bits_bo_only
                                                         : s->frame_bits);

      if (!ctx->ccso_bo_only && !quant_sz[ctx->scale_idx][ctx->quant_idx]) {
        // remove one frame bit for quant sz is 0 case
        cur_total_bits -= 1;
      }

      cur_total_rate +=
          (ctx->reuse_ccso_idx
               ? av2_cost_literal(2 + avm_ceil_log2(s->num_ref_frames))
               : av2_cost_literal(cur_total_bits));
      const uint64_t cur_total_cost =
          RDCOST(s->rdmult, cur_total_rate, cur_total_dist * 16);
      if (cur_total_cost < prev_total_cost) {
        prev_total_cost = cur_total_cost;
        improvement = true;
      }
      if (cur_total_cost < ctx->best.filtered_cost) {
        ctx->best.filtered_cost = cur_total_cost;
        ctx->best.reuse_ccso = ctx->reuse_ccso_idx;
        ctx->best.sb_reuse_ccso = ctx->sb_reuse_idx;
        ctx->best.quant_idx = ctx->quant_idx;
        ctx->best.scale_idx = ctx->scale_idx;
        ctx->best.ext_filter_support = ctx->ext_filter_support;
        ctx->best.ccso_bo_only = ctx->ccso_bo_only;
        ctx->best.ref_idx = ctx->ref_idx - 1;
        av2_copy_array(ctx->best.filter_offset, filter_offset,
                       (1 << ctx->max_band_log2) * 16);
        ctx->best.edge_classifier = ctx->edge_clf;
        ctx->best.band_log2 = ctx->max_band_log2;
        av2_copy_array(ctx->best_filter_control, ctx->filter_control,
                       s->sb_count);
      }
    }

    training_iter_count++;
    if (!improvement || training_iter_count > CCSO_MAX_ITERATIONS ||
        ctx->sb_reuse_idx || ctx->reuse_ccso_idx) {
      keep_training = false;
    }
  }
}

// Tries sb_reuse_idx = 0 (fresh per-superblock enable decision) and, when
// s->check_sb_reuse allows it, sb_reuse_idx = 1 (reuse s->ref_frame_ccso_info's
// per-superblock pattern instead).
static AVM_INLINE void search_sb_reuse_idx(AV2_COMMON *cm, CcsoCtx *ctx,
                                           MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;

  for (uint8_t sb_reuse_idx = 0; sb_reuse_idx <= ctx->check_sb_reuse;
       ++sb_reuse_idx) {
    if (sb_reuse_idx == 0 && ctx->reuse_ccso_idx == 0 && ctx->ref_idx > 0)
      continue;

    ctx->sb_reuse_idx = sb_reuse_idx;

    if (sb_reuse_idx) {
      // Overwrite filter control
      av2_copy_array(ctx->filter_control,
                     ctx->ref_frame_ccso_info->sb_filter_control[s->plane],
                     s->sb_count);
    } else {
      int control_idx = 0;
      for (int y = 0; y < s->ccso_nvfb; y++) {
        for (int x = 0; x < s->ccso_nhfb; x++) {
          ctx->filter_control[control_idx++] = 1;
        }
      }
    }

    run_training(cm, ctx, xd);
  }
}

// Tries ref_idx = 0 (no reference reuse) and each valid reference frame
// index for s->reuse_ccso_idx
static AVM_INLINE void search_ref_idx(AV2_COMMON *cm, CcsoCtx *ctx,
                                      MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;
  const int plane = s->plane;
  const int ccso_blk_size = s->ccso_blk_size;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const uint8_t reuse_ccso_idx = ctx->reuse_ccso_idx;
  RefCntBuffer *ref_frame = NULL;
  CcsoInfo *ccso_info = NULL;

  for (int ref_idx = 0; ref_idx <= s->num_ref_frames; ref_idx++) {
    ctx->ref_frame_ccso_info = NULL;

    if (reuse_ccso_idx > 0 && ref_idx == 0) continue;
    // do not use BRU frame as ref for now
    if (ref_idx == cm->bru.update_ref_idx) continue;

    if (ref_idx > 0) {
      ref_frame = get_ref_frame_buf(cm, ref_idx - 1);
      if (ref_frame->is_restricted) continue;

      ccso_info = &ref_frame->ccso_info;
      if (!ccso_info->ccso_enable[plane]) continue;

      ctx->ref_frame_ccso_info = ccso_info;

      int repeat_ref = 0;
      for (int idx = 0; idx < ctx->checked_reuse_ref_idx[reuse_ccso_idx];
           idx++) {
        if (ctx->checked_reuse_ref[reuse_ccso_idx][idx] ==
            ccso_info->reuse_root_ref[plane]) {
          repeat_ref = 1;
          break;
        }
      }
      if (repeat_ref) continue;

      const int slot = ctx->checked_reuse_ref_idx[reuse_ccso_idx];
      ctx->checked_reuse_ref[reuse_ccso_idx][slot] =
          ctx->ref_frame_ccso_info->reuse_root_ref[plane];
      ctx->checked_reuse_ref_idx[reuse_ccso_idx]++;
    }

    ctx->ref_idx = ref_idx;

    if (reuse_ccso_idx) {
      if (ccso_info == NULL ||
          !((ctx->scale_idx == ccso_info->scale_idx[plane]) &&
            (ctx->ccso_bo_only == ccso_info->ccso_bo_only[plane]) &&
            (ctx->ext_filter_support == ccso_info->ext_filter_support[plane]) &&
            (ctx->quant_idx == ccso_info->quant_idx[plane]) &&
            (ctx->edge_clf == ccso_info->edge_clf[plane]) &&
            (ctx->max_band_log2 == ccso_info->max_band_log2[plane]))) {
        continue;
      }
    }

    ctx->check_sb_reuse = s->check_ccso && (ccso_info != NULL) &&
                          (cm->mi_params.mi_rows == ref_frame->mi_rows) &&
                          (cm->mi_params.mi_cols == ref_frame->mi_cols) &&
                          (ss_y == ccso_info->subsampling_y[plane]) &&
                          (ss_x == ccso_info->subsampling_x[plane]) &&
                          (ccso_blk_size == ccso_info->ccso_blk_size) &&
                          (ccso_blk_size == CCSO_BLK_SIZE);

    search_sb_reuse_idx(cm, ctx, xd);

    if (reuse_ccso_idx == 0) ctx->skip_filter_calculation = true;
  }
}

// Evaluates new filter / reference frame's already-derived filter for the
// current candidate
static AVM_INLINE void search_reuse_ccso_idx(AV2_COMMON *cm, CcsoCtx *ctx,
                                             MACROBLOCKD *xd) {
  for (int reuse_ccso_idx = 0; reuse_ccso_idx <= 1; reuse_ccso_idx++) {
    ctx->reuse_ccso_idx = reuse_ccso_idx;
    ctx->skip_filter_calculation = false;
    search_ref_idx(cm, ctx, xd);
  }
}

// Evaluates every max_band_log2 value for the current
// (scale_idx, so mode, filter_support, quant_idx, edge_clf) candidate,
// updates ctx->final whenever a new frame-wide best is found.
static AVM_INLINE bool search_max_band_log2(AV2_COMMON *cm, CcsoCtx *ctx,
                                            MACROBLOCKD *xd) {
  CcsoCtxCommon *s = &ctx->ccso_cm;

  for (int max_band_log2 = 0; max_band_log2 < ctx->num_band_iter;
       max_band_log2++) {
    ctx->max_band_log2 = max_band_log2;
    ctx->shift_bits = cm->seq_params.bit_depth - max_band_log2;
    if (ctx->ccso_bo_only) {
      ccso_pre_compute_class_err_bo(cm, ctx, xd);
    } else {
      if (ctx->init_shift_bits != ctx->shift_bits ||
          !reuse_ccso_class_info(cm)) {
        ccso_pre_compute_class_err(cm, ctx, xd);
      } else {
        const int max_band = 1 << max_band_log2;

        for (int d0 = 0; d0 < ctx->max_edge_interval; d0++) {
          for (int d1 = 0; d1 < ctx->max_edge_interval; d1++) {
            av2_copy_array(ctx->total_class_err[d0][d1][0],
                           ctx->reuse_total_class_err[d0][d1][0],
                           max_band * s->sb_count);
            av2_copy_array(ctx->total_class_cnt[d0][d1][0],
                           ctx->reuse_total_class_cnt[d0][d1][0],
                           max_band * s->sb_count);
          }
        }
      }
    }

    memset(ctx->checked_reuse_ref, -1, sizeof(ctx->checked_reuse_ref));
    av2_zero_array(ctx->checked_reuse_ref_idx, 2);
    ctx->best.filtered_cost = UINT64_MAX;

    search_reuse_ccso_idx(cm, ctx, xd);

    if (ctx->best.filtered_cost < ctx->final.filtered_cost) {
      ctx->final = ctx->best;
      av2_copy_array(ctx->final_filter_control, ctx->best_filter_control,
                     s->sb_count);
    }
    if (s->early_terminate_ccso_search &&
        ctx->final.filtered_cost != UINT64_MAX &&
        1.001 * ctx->final.filtered_cost > ctx->last_best_cost) {
      return true;
    }
  }
  return false;
}

// Evaluates edge_clf0 and edge_clf1 for the current (scale_idx, so mode,
// filter, qstep) candidate.
static AVM_INLINE bool search_edge_clf(AV2_COMMON *cm, CcsoCtx *ctx,
                                       MACROBLOCKD *xd) {
  const int num_edge_clf_iter = ctx->ccso_bo_only ? 1 : 2;
  const int total_band_log2_plus1 = ctx->ccso_bo_only ? 7 : 4;
  const int total_band_log2 = total_band_log2_plus1 - 1;

  ctx->num_band_iter = total_band_log2_plus1;
  for (int edge_clf = 0; edge_clf < num_edge_clf_iter; edge_clf++) {
    ctx->edge_clf = edge_clf;
    ctx->max_edge_interval = edge_clf_to_edge_interval[edge_clf];
    ctx->last_best_cost = ctx->final.filtered_cost;

    if (quant_sz[ctx->scale_idx][ctx->quant_idx] == 0 && edge_clf == 1) {
      continue;
    }
    if (!ctx->ccso_bo_only) {
      ccso_derive_src_info(cm, ctx, xd);

      // compute the total_class_err for minimum shift_bits possible before the
      // below loop starts, later use the same in the ccso_pre_compute_class_err
      // calls.
      ctx->init_shift_bits = cm->seq_params.bit_depth - total_band_log2;
      ctx->shift_bits = ctx->init_shift_bits;
      ccso_pre_compute_class_err(cm, ctx, xd);
    }

    if (search_max_band_log2(cm, ctx, xd)) return true;
  }
  return false;
}

// For a given [plane, scale_idx, so mode, eo switchable filter type, qstep],
// this function performs rdo search across all eo classifier types, max bands,
// reference's ccso filter offsets / new filter offsets and selects params with
// best rdo.
bool av2_ccso_param_search(AV2_COMMON *cm, CcsoCtx *ctx, MACROBLOCKD *xd) {
  return search_edge_clf(cm, ctx, xd);
}

// Writes val into the MB_MODE_INFO field that stores CCSO's per-block
// enable state for `plane`.
static void set_mbmi_ccso_blk(MB_MODE_INFO *mbmi, int plane, uint8_t val) {
  if (plane == AVM_PLANE_Y) {
    mbmi->ccso_blk_y = val;
  } else if (plane == AVM_PLANE_U) {
    mbmi->ccso_blk_u = val;
  } else {
    mbmi->ccso_blk_v = val;
  }
}

static void finalize_ccso_plane(AV2_COMMON *cm, CcsoCtx *ctx, ThreadData *td,
                                int disable_ccso) {
  CcsoInfo *cur_frame_ccso_info = &cm->cur_frame->ccso_info;
  CcsoCtxCommon *s = &ctx->ccso_cm;
  CcsoCandidate *final = &ctx->final;
  MACROBLOCKD *const xd = &td->mb.e_mbd;
  const int plane = s->plane;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;

  cur_frame_ccso_info->subsampling_x[plane] = ss_x;
  cur_frame_ccso_info->subsampling_y[plane] = ss_y;
  if (disable_ccso) {
    av2_zero_array(ctx->final_filter_control, s->sb_count);
    cm->ccso_info.ccso_enable[plane] = false;
    av2_zero_array(cur_frame_ccso_info->sb_filter_control[plane], s->sb_count);
    cm->cur_frame->ccso_info.ccso_enable[plane] = false;
    return;
  }

  cm->ccso_info.ccso_enable[plane] = true;
  cm->ccso_info.sb_reuse_ccso[plane] = final->sb_reuse_ccso;
  cm->ccso_info.reuse_ccso[plane] = final->reuse_ccso;
  CcsoInfo *ref_frame_ccso_info = NULL;
  if (final->reuse_ccso || final->sb_reuse_ccso) {
    RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, final->ref_idx);
    assert(ref_frame != NULL);
    ref_frame_ccso_info = &ref_frame->ccso_info;
    cm->ccso_info.ccso_ref_idx[plane] = final->ref_idx;
  }
  cur_frame_ccso_info->ccso_enable[plane] = true;
  cur_frame_ccso_info->ccso_blk_size = s->ccso_blk_size;
  cur_frame_ccso_info->reuse_root_ref[plane] =
      cm->current_frame.display_order_hint;

  CommonModeInfoParams *const mi_params = &cm->mi_params;
  bool *cur_frame_filter_control =
      cur_frame_ccso_info->sb_filter_control[plane];
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  const int f_w = 1 << s->ccso_blk_size >> MI_SIZE_LOG2;
  const int f_h = 1 << s->ccso_blk_size >> MI_SIZE_LOG2;
  const int step_h = (mi_size_high[bsize] + f_h - 1) / f_h;
  const int step_w = (mi_size_wide[bsize] + f_w - 1) / f_w;
  const int sb_unit_size_x =
      (1 << s->log2_filter_unit_size_x >> (MI_SIZE_LOG2 - ss_x));
  const int sb_unit_size_y =
      (1 << s->log2_filter_unit_size_y >> (MI_SIZE_LOG2 - ss_y));
  const int ccso_mib_size_y = (1 << (s->ccso_blk_size - MI_SIZE_LOG2));
  const int ccso_mib_size_x = (1 << (s->ccso_blk_size - MI_SIZE_LOG2));

  if (!final->sb_reuse_ccso) {
    for (int y_sb = 0; y_sb < s->ccso_nvfb; y_sb += step_h) {
      for (int x_sb = 0; x_sb < s->ccso_nhfb; x_sb += step_w) {
        const bool sb_filter_control =
            ctx->final_filter_control[y_sb * s->ccso_nhfb + x_sb];
        for (int row = y_sb; row < y_sb + step_h; row++) {
          for (int col = x_sb; col < x_sb + step_w; col++) {
            cur_frame_filter_control[row * s->ccso_nhfb + col] =
                sb_filter_control;

            const int mi_row = f_h * row;
            const int mi_col = f_w * col;
            int grid_idx = mi_row * mi_params->mi_stride + mi_col;
            MB_MODE_INFO *mbmi = mi_params->mi_grid_base[grid_idx];

            // for tile skip, no valid mi exist
            if (cm->bru.enabled &&
                bru_is_fu_skipped_mbmi(cm, sb_unit_size_x * col,
                                       sb_unit_size_y * row, f_w, f_h)) {
              assert(sb_filter_control == 0);
            }
            set_mbmi_ccso_blk(mbmi, plane, sb_filter_control);

            for (int j = 0;
                 j < AVMMIN(ccso_mib_size_y, mi_params->mi_rows - mi_row);
                 j++) {
              for (int k = 0;
                   k < AVMMIN(ccso_mib_size_x, mi_params->mi_cols - mi_col);
                   k++) {
                grid_idx = get_mi_grid_idx(mi_params, mi_row + j, mi_col + k);
                mbmi = mi_params->mi_grid_base[grid_idx];
                set_mbmi_ccso_blk(mbmi, plane, sb_filter_control);
              }
            }
          }
        }

#if CONFIG_ENTROPY_STATS
        const int ccso_ctx = get_ccso_context(y_sb, x_sb, s->ccso_nhfb,
                                              ctx->final_filter_control);

        ++td->counts->default_ccso_cnts
              [plane][ccso_ctx]
              [ctx->final_filter_control[y_sb * s->ccso_nhfb + x_sb]];
#endif
      }
    }
  } else {
    assert(ref_frame_ccso_info != NULL);
    bool *ref_frame_filter_control =
        ref_frame_ccso_info->sb_filter_control[plane];
    av2_copy_array(cur_frame_filter_control, ref_frame_filter_control,
                   s->sb_count);
    for (int y_sb = 0; y_sb < s->ccso_nvfb; y_sb++) {
      for (int x_sb = 0; x_sb < s->ccso_nhfb; x_sb++) {
        int grid_idx = f_h * y_sb * mi_params->mi_stride + f_w * x_sb;
        MB_MODE_INFO *mbmi = mi_params->mi_grid_base[grid_idx];
        bool filter_control =
            ref_frame_filter_control[y_sb * s->ccso_nhfb + x_sb];
        set_mbmi_ccso_blk(mbmi, plane, filter_control);
      }
    }
  }
  if (!cm->ccso_info.reuse_ccso[plane]) {
    av2_copy_array(cm->ccso_info.filter_offset[plane], final->filter_offset,
                   (1 << final->band_log2) * 16);
    cm->ccso_info.quant_idx[plane] = final->quant_idx;
    cm->ccso_info.scale_idx[plane] = final->scale_idx;
    cm->ccso_info.ext_filter_support[plane] = final->ext_filter_support;
    cm->ccso_info.ccso_bo_only[plane] = final->ccso_bo_only;
    cm->ccso_info.max_band_log2[plane] = final->band_log2;
    cm->ccso_info.edge_clf[plane] = final->edge_classifier;

    av2_copy_array(cur_frame_ccso_info->filter_offset[plane],
                   final->filter_offset, (1 << final->band_log2) * 16);
    cur_frame_ccso_info->quant_idx[plane] = final->quant_idx;
    cur_frame_ccso_info->scale_idx[plane] = final->scale_idx;
    cur_frame_ccso_info->ext_filter_support[plane] = final->ext_filter_support;
    cur_frame_ccso_info->ccso_bo_only[plane] = final->ccso_bo_only;
    cur_frame_ccso_info->max_band_log2[plane] = final->band_log2;
    cur_frame_ccso_info->edge_clf[plane] = final->edge_classifier;
  } else {
    av2_copy_ccso_filters(&cm->ccso_info, ref_frame_ccso_info, plane, 1, 0,
                          s->sb_count);
    av2_copy_ccso_filters(cur_frame_ccso_info, ref_frame_ccso_info, plane, 1, 0,
                          s->sb_count);
  }

  if (final->reuse_ccso && final->sb_reuse_ccso) {
    assert(ref_frame_ccso_info != NULL);
    cur_frame_ccso_info->reuse_root_ref[plane] =
        ref_frame_ccso_info->reuse_root_ref[plane];
  }
}

/* Derive the look-up table for a color component */
static void derive_ccso_filter(AV2_COMP *cpi, const int plane,
                               const uint16_t *org_uv,
                               const uint16_t *ext_rec_y,
                               const uint16_t *rec_uv, int rdmult,
                               bool error_resilient_frame_seen,
                               int early_terminate_ccso_search) {
  AV2_COMMON *const cm = &cpi->common;
  ThreadData *td = &cpi->td;
  MACROBLOCKD *const xd = &td->mb.e_mbd;
  const int ccso_stride = xd->plane[AVM_PLANE_Y].dst.width;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ss_x = xd->plane[plane].subsampling_x;
  const int ss_y = xd->plane[plane].subsampling_y;
  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  cm->ccso_info.ccso_blk_size = ccso_blk_size;
  const int log2_filter_unit_size_y = ccso_blk_size - ss_y;
  const int log2_filter_unit_size_x = ccso_blk_size - ss_x;
  const int log2_proc_unit_size =
      cm->mib_size_log2 - AVMMAX(ss_x, ss_y) + MI_SIZE_LOG2;
  const int ccso_nvfb =
      ((mi_params->mi_rows >> ss_y) + (1 << log2_filter_unit_size_y >> 2) - 1) /
      (1 << log2_filter_unit_size_y >> 2);
  const int ccso_nhfb =
      ((mi_params->mi_cols >> ss_x) + (1 << log2_filter_unit_size_x >> 2) - 1) /
      (1 << log2_filter_unit_size_x >> 2);
  const int sb_count = ccso_nvfb * ccso_nhfb;

  const int total_scale_idx = 4;
  const int total_filter_support = 7;
  const int total_quant_idx = 4;

  uint8_t frame_bits = 1;  // ccso_planes[ plane ]
  frame_bits += 1;         // ccso_bo_only[ plane ]
  frame_bits += 2;         // ccso_scale_idx[ plane ]
  frame_bits += 2;         // ccso_quant_idx[ plane ]
  frame_bits += 3;         // ccso_ext_filter[ plane ]
  frame_bits += 1;         // ccso_edge_clf[ plane ]
  frame_bits += 2;         // ccso_max_band_log2[ plane ]

  uint8_t frame_bits_bo_only = 1;  // ccso_planes[ plane ]
  frame_bits_bo_only += 1;         // ccso_bo_only[ plane ]
  frame_bits_bo_only += 2;         // ccso_scale_idx[ plane ]
  frame_bits_bo_only += 3;         // ccso_max_band_log2[ plane ]

  int check_ccso = 0;
  const bool is_frame_intra_or_switch =
      frame_is_intra_only(cm) || frame_is_sframe(cm);
  const int num_ref_frames =
      (is_frame_intra_or_switch || error_resilient_frame_seen)
          ? 0
          : cm->ref_frames_info.num_total_refs;

  if (!is_frame_intra_or_switch) {
    frame_bits += 1;  // reuse_ccso[ plane ]
    frame_bits += 1;  // sb_reuse_ccso[ plane ]

    frame_bits_bo_only += 1;  // reuse_ccso[ plane ]
    frame_bits_bo_only += 1;  // sb_reuse_ccso[ plane ]

    check_ccso = 1;
  }

  if (cpi->unfiltered_dist_block == NULL ||
      sb_count > cpi->unfiltered_dist_block_alloc_sb_count) {
    avm_free(cpi->unfiltered_dist_block);
    CHECK_MEM_ERROR(cm, cpi->unfiltered_dist_block,
                    avm_malloc(sb_count * sizeof(*cpi->unfiltered_dist_block)));
    cpi->unfiltered_dist_block_alloc_sb_count = sb_count;
  }
  // alloc ccso search context
  CcsoCtx *ctx = &cpi->ccso_ctx;
  ccso_ctx_reset(ctx);
  av2_ccso_alloc_search_buffers(cm, ctx, ccso_stride,
                                xd->plane[AVM_PLANE_Y].dst.height, sb_count);

  // init ccso search context
  CcsoCtxCommon *ccso_cm = &ctx->ccso_cm;
  ccso_cm->org_uv = org_uv;
  ccso_cm->ext_rec_y = ext_rec_y;
  ccso_cm->rec_uv = rec_uv;
  ccso_cm->unfiltered_dist_block = cpi->unfiltered_dist_block;
  ccso_cm->plane = plane;
  ccso_cm->rdmult = rdmult;
  ccso_cm->ccso_blk_size = ccso_blk_size;
  ccso_cm->log2_filter_unit_size_x = log2_filter_unit_size_x;
  ccso_cm->log2_filter_unit_size_y = log2_filter_unit_size_y;
  ccso_cm->log2_proc_unit_size = log2_proc_unit_size;
  ccso_cm->ccso_nvfb = ccso_nvfb;
  ccso_cm->ccso_nhfb = ccso_nhfb;
  ccso_cm->sb_count = sb_count;
  ccso_cm->frame_bits = frame_bits;
  ccso_cm->frame_bits_bo_only = frame_bits_bo_only;
  ccso_cm->check_ccso = check_ccso;
  ccso_cm->num_ref_frames = num_ref_frames;
  ccso_cm->early_terminate_ccso_search = early_terminate_ccso_search;

  ctx->final.filtered_cost = UINT64_MAX;
  ctx->final.ref_idx = -1;
  ctx->ccso_stride = ccso_stride;
  ctx->ccso_stride_ext = ccso_stride + (CCSO_PADDING_SIZE << 1);

  uint64_t unfiltered_dist_frame;
  compute_distortion(cm, ctx, xd, rec_uv, cpi->unfiltered_dist_block,
                     &unfiltered_dist_frame);
  unfiltered_dist_frame =
      ROUND_POWER_OF_TWO(unfiltered_dist_frame, (xd->bd - 8) * 2);
  const uint64_t best_unfiltered_cost =
      RDCOST(rdmult, av2_cost_literal(1), unfiltered_dist_frame * 16);

  for (uint8_t scale_idx = 0; scale_idx < total_scale_idx; ++scale_idx) {
    for (uint8_t search_idx = 0; search_idx < 2; search_idx++) {
      // A BO-only candidate is cheaper and covers a different part of the
      // search space. Under early termination, evaluate it first so the full
      // filter search starts with a finite RD bound. Keep the original order
      // for exhaustive search, where ordering should not affect the result.
      const uint8_t ccso_bo_only =
          early_terminate_ccso_search ? 1 - search_idx : search_idx;
      const int num_filter_iter = ccso_bo_only ? 1 : total_filter_support;
      for (uint8_t ext_filter_support = 0; ext_filter_support < num_filter_iter;
           ++ext_filter_support) {
        const uint8_t num_quant_iter = ccso_bo_only ? 1 : total_quant_idx;
        for (uint8_t quant_idx = 0; quant_idx < num_quant_iter; ++quant_idx) {
          ctx->scale_idx = scale_idx;
          ctx->ccso_bo_only = ccso_bo_only;
          ctx->ext_filter_support = ext_filter_support;
          ctx->quant_idx = quant_idx;
          if (av2_ccso_param_search(cm, ctx, xd)) goto exit_loops;
        }
      }
    }
  }
exit_loops:

  finalize_ccso_plane(cm, ctx, td,
                      best_unfiltered_cost < ctx->final.filtered_cost);
}

/* Derive the look-up table for a frame */
void av2_ccso_search(struct AV2_COMP *cpi, const uint16_t *ext_rec_y,
                     uint16_t *rec_uv[MAX_MB_PLANE],
                     uint16_t *org_uv[MAX_MB_PLANE],
                     bool error_resilient_frame_seen,
                     int early_terminate_ccso_search, int ccso_chroma_dep) {
  AV2_COMMON *const cm = &cpi->common;
  ThreadData *td = &cpi->td;
  MACROBLOCKD *const xd = &td->mb.e_mbd;
  const int num_planes = av2_num_planes(cm);
  const int rdmult_weight = clamp(cm->quant_params.base_qindex >> 3, 1, 37);
  const int rdmult = td->mb.rdmult;

  cm->ccso_info.ccso_frame_flag = false;
  for (int plane = AVM_PLANE_Y; plane < num_planes; ++plane) {
    cm->cur_frame->ccso_info.ccso_enable[plane] = false;
    cm->ccso_info.ccso_enable[plane] = false;
    cm->ccso_info.sb_reuse_ccso[plane] = false;
    cm->ccso_info.reuse_ccso[plane] = false;
  }

  if ((int64_t)rdmult * rdmult_weight < INT_MAX) {
    av2_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                         NULL);
    for (int plane = num_planes - 1; plane >= AVM_PLANE_Y; --plane) {
      if (plane == AVM_PLANE_Y && ccso_chroma_dep && num_planes > 1 &&
          !cm->ccso_info.ccso_frame_flag) {
        break;
      }
      int rdmult_plane = (plane == AVM_PLANE_Y) ? rdmult : ((rdmult * 7) >> 3);
      derive_ccso_filter(cpi, plane, org_uv[plane], ext_rec_y, rec_uv[plane],
                         rdmult_plane, error_resilient_frame_seen,
                         early_terminate_ccso_search);
      cm->ccso_info.ccso_frame_flag |= cm->ccso_info.ccso_enable[plane];
    }
  }
}
