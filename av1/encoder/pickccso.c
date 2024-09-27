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
#if CONFIG_CCSO_IMPROVE
  int final_reuse_ccso[CCSO_NUM_COMPONENTS];
  int final_sb_reuse_ccso[CCSO_NUM_COMPONENTS];
  uint8_t final_scale_idx[CCSO_NUM_COMPONENTS];
#endif  // CONFIG_CCSO_IMPROVE
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
#if CONFIG_CCSO_IMPROVE
const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                  { 32, 16, 64, 128 },
                                  { 48, 24, 96, 192 },
                                  { 64, 32, 128, 256 } };

const int ccso_scale[4] = { 1, 2, 3, 4 };
#else
const uint8_t quant_sz[4] = { 16, 8, 32, 64 };
#endif  // CONFIG_CCSO_IMPROVE

void ccso_derive_src_block_c(const uint16_t *src_y, uint8_t *const src_cls0,
                             uint8_t *const src_cls1, const int src_y_stride,
                             const int src_cls_stride, const int x, const int y,
                             const int pic_width, const int pic_height,
                             const int y_uv_hscale, const int y_uv_vscale,
                             const int qstep, const int neg_qstep,
                             const int *src_loc, const int blk_size,
                             const int edge_clf) {
  int src_cls[2];
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
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
#if CONFIG_CCSO_IMPROVE
                                 const uint16_t qstep,
#else
                                 const uint8_t qstep,
#endif  // CONFIG_CCSO_IMPROVE
                                 const uint8_t filter_sup, uint8_t *src_cls0,
                                 uint8_t *src_cls1, int edge_clf,
                                 int ccso_stride, int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int neg_qstep = qstep * -1;
  int src_loc[2];
  derive_ccso_sample_pos(cm, src_loc, ccso_stride_ext, filter_sup);
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      ccso_derive_src_block(src_y, src_cls0, src_cls1, ccso_stride_ext,
                            ccso_stride, x, y, pic_width, pic_height,
                            y_uv_hscale, y_uv_vscale, qstep, neg_qstep, src_loc,
                            blk_size, edge_clf);
    }
    src_y += (ccso_stride_ext << (blk_log2 + y_uv_vscale));
    src_cls0 += (ccso_stride << (blk_log2 + y_uv_vscale));
    src_cls1 += (ccso_stride << (blk_log2 + y_uv_vscale));
  }
}

/* Compute the aggregated residual between original and reconstructed sample for
 * each entry of the LUT */
static void ccso_pre_compute_class_err(CcsoCtx *ctx, MACROBLOCKD *xd,
                                       const int plane, const uint16_t *src_y,
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
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  const int scaled_stride = (ctx->ccso_stride << y_uv_vscale);
  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      fb_idx++;
      const int y_end = AOMMIN(pic_height - y, blk_size);
      const int x_end = AOMMIN(pic_width - x, blk_size);
      for (int y_start = 0; y_start < y_end; y_start++) {
        for (int x_start = 0; x_start < x_end; x_start++) {
          const int x_pos = x + x_start;
          cur_src_cls0 = src_cls0[x_pos << y_uv_hscale];
          cur_src_cls1 = src_cls1[x_pos << y_uv_hscale];
          const int band_num = src_y[x_pos << y_uv_hscale] >> shift_bits;
          ctx->total_class_err[cur_src_cls0][cur_src_cls1][band_num]
                              [fb_idx - 1] += ref[x_pos] - dst[x_pos];
          ctx->total_class_cnt[cur_src_cls0][cur_src_cls1][band_num]
                              [fb_idx - 1]++;
        }
        ref += ctx->ccso_stride;
        dst += ctx->ccso_stride;
        src_y += scaled_ext_stride;
        src_cls0 += scaled_stride;
        src_cls1 += scaled_stride;
      }
      ref -= ctx->ccso_stride * y_end;
      dst -= ctx->ccso_stride * y_end;
      src_y -= scaled_ext_stride * y_end;
      src_cls0 -= scaled_stride * y_end;
      src_cls1 -= scaled_stride * y_end;
    }
    ref += (ctx->ccso_stride << blk_log2);
    dst += (ctx->ccso_stride << blk_log2);
    src_y += (ctx->ccso_stride_ext << (blk_log2 + y_uv_vscale));
    src_cls0 += (ctx->ccso_stride << (blk_log2 + y_uv_vscale));
    src_cls1 += (ctx->ccso_stride << (blk_log2 + y_uv_vscale));
  }
}

// pre compute classes for band offset only option
static void ccso_pre_compute_class_err_bo(
    CcsoCtx *ctx, MACROBLOCKD *xd, const int plane, const uint16_t *src_y,
    const uint16_t *ref, const uint16_t *dst, const uint8_t shift_bits) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  int fb_idx = 0;
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  const int scaled_ext_stride = (ctx->ccso_stride_ext << y_uv_vscale);
  src_y += CCSO_PADDING_SIZE * ctx->ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      fb_idx++;
      const int y_end = AOMMIN(pic_height - y, blk_size);
      const int x_end = AOMMIN(pic_width - x, blk_size);
      for (int y_start = 0; y_start < y_end; y_start++) {
        for (int x_start = 0; x_start < x_end; x_start++) {
          const int x_pos = x + x_start;
          const int band_num = src_y[x_pos << y_uv_hscale] >> shift_bits;
          ctx->total_class_err_bo[band_num][fb_idx - 1] +=
              ref[x_pos] - dst[x_pos];
          ctx->total_class_cnt_bo[band_num][fb_idx - 1]++;
        }
        ref += ctx->ccso_stride;
        dst += ctx->ccso_stride;
        src_y += scaled_ext_stride;
      }
      ref -= ctx->ccso_stride * y_end;
      dst -= ctx->ccso_stride * y_end;
      src_y -= scaled_ext_stride * y_end;
    }
    ref += (ctx->ccso_stride << blk_log2);
    dst += (ctx->ccso_stride << blk_log2);
    src_y += (ctx->ccso_stride_ext << (blk_log2 + y_uv_vscale));
  }
}

// Apply ccso filter when Band Offset Only option is true.
void ccso_filter_block_hbd_with_buf_bo_only_c(
    const uint16_t *src_y, uint16_t *dst_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int src_cls_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size,
    const int y_uv_hscale, const int y_uv_vscale, const int max_val,
    const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  assert(ccso_bo_only == 1);

  (void)src_cls0;
  (void)src_cls1;
  (void)src_cls_stride;
  (void)ccso_bo_only;

  int cur_src_cls0;
  int cur_src_cls1;
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
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
    const int pic_height, const int8_t *filter_offset, const int blk_size,
    const int y_uv_hscale, const int y_uv_vscale, const int max_val,
    const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  if (ccso_bo_only) {
    (void)src_cls0;
    (void)src_cls1;
  }
  int cur_src_cls0;
  int cur_src_cls1;
  const int y_end = AOMMIN(pic_height - y, blk_size);
  const int x_end = AOMMIN(pic_width - x, blk_size);
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
void ccso_try_luma_filter(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                          const uint16_t *src_y, uint16_t *dst_yuv,
                          const int dst_stride, const int8_t *filter_offset,
                          uint8_t *src_cls0, uint8_t *src_cls1,
                          const uint8_t shift_bits, const uint8_t ccso_bo_only,
                          int ccso_stride, int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      if (ccso_bo_only) {
#if CONFIG_CCSO_IMPROVE
        ccso_filter_block_hbd_with_buf_bo_only(
#else
        ccso_filter_block_hbd_with_buf_c(
#endif  // CONFIG_CCSO_IMPROVE
            src_y, dst_yuv, src_cls0, src_cls1, ccso_stride_ext, dst_stride,
            ccso_stride, x, y, pic_width, pic_height, filter_offset, blk_size,
            // y_uv_scale in h and v shall be zero
            0, 0, max_val, shift_bits, ccso_bo_only);
      } else {
        ccso_filter_block_hbd_with_buf(
            src_y, dst_yuv, src_cls0, src_cls1, ccso_stride_ext, dst_stride,
            ccso_stride, x, y, pic_width, pic_height, filter_offset, blk_size,
            // y_uv_scale in h and v shall be zero
            0, 0, max_val, shift_bits, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_stride_ext << blk_log2);
    src_cls0 += (ccso_stride << blk_log2);
    src_cls1 += (ccso_stride << blk_log2);
  }
}

/* Apply CCSO on chroma component at encoder (high bit-depth) */
static void ccso_try_chroma_filter(
    AV1_COMMON *cm, MACROBLOCKD *xd, const int plane, const uint16_t *src_y,
    uint16_t *dst_yuv, const int dst_stride, const int8_t *filter_offset,
    uint8_t *src_cls0, uint8_t *src_cls1, const uint8_t shift_bits,
    const uint8_t ccso_bo_only, int ccso_stride, int ccso_stride_ext) {
  const int pic_height = xd->plane[plane].dst.height;
  const int pic_width = xd->plane[plane].dst.width;
  const int y_uv_hscale = xd->plane[plane].subsampling_x;
  const int y_uv_vscale = xd->plane[plane].subsampling_y;
  const int max_val = (1 << cm->seq_params.bit_depth) - 1;
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const int blk_size = 1 << blk_log2;
  src_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
  for (int y = 0; y < pic_height; y += blk_size) {
    for (int x = 0; x < pic_width; x += blk_size) {
      if (ccso_bo_only) {
#if CONFIG_CCSO_IMPROVE
        ccso_filter_block_hbd_with_buf_bo_only(
#else
        ccso_filter_block_hbd_with_buf_c(
#endif  // CONFIG_CCSO_IMPROVE
            src_y, dst_yuv, src_cls0, src_cls1, ccso_stride_ext, dst_stride,
            ccso_stride, x, y, pic_width, pic_height, filter_offset, blk_size,
            y_uv_hscale, y_uv_vscale, max_val, shift_bits, ccso_bo_only);
      } else {
        ccso_filter_block_hbd_with_buf(
            src_y, dst_yuv, src_cls0, src_cls1, ccso_stride_ext, dst_stride,
            ccso_stride, x, y, pic_width, pic_height, filter_offset, blk_size,
            y_uv_hscale, y_uv_vscale, max_val, shift_bits, 0);
      }
    }
    dst_yuv += (dst_stride << blk_log2);
    src_y += (ccso_stride_ext << (blk_log2 + y_uv_vscale));
    src_cls0 += (ccso_stride << (blk_log2 + y_uv_vscale));
    src_cls1 += (ccso_stride << (blk_log2 + y_uv_vscale));
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
static void compute_distortion(const uint16_t *org, const int org_stride,
                               const uint16_t *rec16, const int rec_stride,
                               const int log2_filter_unit_size_y,
                               const int log2_filter_unit_size_x,
                               const int height, const int width,
                               uint64_t *distortion_buf,
                               const int distortion_buf_stride,
                               uint64_t *total_distortion) {
  for (int y = 0; y < height; y += (1 << log2_filter_unit_size_y)) {
    for (int x = 0; x < width; x += (1 << log2_filter_unit_size_x)) {
      const uint64_t ssd = compute_distortion_block(
          org, org_stride, rec16, rec_stride, x, y, log2_filter_unit_size_y,
          log2_filter_unit_size_x, height, width);
      distortion_buf[(y >> log2_filter_unit_size_y) * distortion_buf_stride +
                     (x >> log2_filter_unit_size_x)] = ssd;
      *total_distortion += ssd;
    }
    org += (org_stride << log2_filter_unit_size_y);
    rec16 += (rec_stride << log2_filter_unit_size_y);
  }
}

#if CONFIG_CCSO_IMPROVE
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
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + xd->plane[1].subsampling_x;
  ;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  bool cur_filter_enabled = false;
  int sb_idx = 0;

  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;

  const int blk_size_y =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_y - MI_SIZE_LOG2)) - 1;
  const int blk_size_x =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_x - MI_SIZE_LOG2)) - 1;

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
                                   NULL);

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
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + xd->plane[1].subsampling_x;
  ;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  bool cur_filter_enabled = false;
  int sb_idx = 0;

  const CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;

  const int blk_size_y =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_y - MI_SIZE_LOG2)) - 1;
  const int blk_size_x =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_x - MI_SIZE_LOG2)) - 1;

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
#else
/* Derive block level on/off for CCSO */
static void derive_blk_md(AV1_COMMON *cm, MACROBLOCKD *xd, const int plane,
                          const uint64_t *unfiltered_dist,
                          const uint64_t *training_dist, bool *m_filter_control,
                          uint64_t *cur_total_dist, int *cur_total_rate,
                          bool *filter_enable) {
  aom_cdf_prob ccso_cdf[CDF_SIZE(2)];
  static const aom_cdf_prob default_ccso_cdf[CDF_SIZE(2)] = { AOM_CDF2(11570) };
  av1_copy(ccso_cdf, default_ccso_cdf);
  const int log2_filter_unit_size =
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int ccso_nvfb =
      ((mi_params->mi_rows >> xd->plane[plane].subsampling_y) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size >> 2) - 1) /
      (1 << log2_filter_unit_size >> 2);
  bool cur_filter_enabled = false;
  int sb_idx = 0;
  for (int y_sb = 0; y_sb < ccso_nvfb; y_sb++) {
    for (int x_sb = 0; x_sb < ccso_nhfb; x_sb++) {
      uint64_t ssd;
      uint64_t best_ssd = UINT64_MAX;
      int best_rate = INT_MAX;
      uint8_t cur_best_filter_control = 0;
      int cost_from_cdf[2];
      av1_cost_tokens_from_cdf(cost_from_cdf, ccso_cdf, NULL);
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
        if (ssd < best_ssd) {
          best_rate = cost_from_cdf[cur_filter_control];
          best_ssd = ssd;
          cur_best_filter_control = cur_filter_control;
          m_filter_control[sb_idx] = cur_filter_control;
        }
      }
      update_cdf(ccso_cdf, cur_best_filter_control, 2);
      if (cur_best_filter_control != 0) {
        cur_filter_enabled = true;
      }
      *cur_total_rate += best_rate;
      *cur_total_dist += best_ssd;
      sb_idx++;
    }
  }
  *filter_enable = cur_filter_enabled;
}
#endif  // CONFIG_CCSO_IMPROVE

/* Compute the residual for each entry of the LUT using CCSO enabled filter
 * blocks
 */
static void ccso_compute_class_err(CcsoCtx *ctx, AV1_COMMON *cm,
                                   const int plane, MACROBLOCKD *xd,
                                   const int max_band_log2,
                                   const int max_edge_interval,
                                   const uint8_t ccso_bo_only) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int blk_log2 = plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + 1;
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
static INLINE int count_lut_bits(int8_t *temp_filter_offset,
#if CONFIG_CCSO_IMPROVE
                                 int scale_idx,
#endif
                                 const int max_band_log2,
                                 const int max_edge_interval,
                                 const uint8_t ccso_bo_only) {
#if CONFIG_CCSO_IMPROVE
  int ccso_offset_reordered[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  for (int idx = 0; idx < 8; ++idx)
    ccso_offset_reordered[idx] =
        ccso_offset_reordered[idx] * ccso_scale[scale_idx];
#else
  const int ccso_offset_reordered[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
#endif  // CONFIG_CCSO_IMPROVE
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
static void derive_lut_offset(int8_t *temp_filter_offset,
#if CONFIG_CCSO_IMPROVE
                              int scale_idx,
#endif
                              const int max_band_log2,
                              const int max_edge_interval,
                              const uint8_t ccso_bo_only,
                              const int chroma_count[CCSO_BAND_NUM * 16],
                              const int chroma_error[CCSO_BAND_NUM * 16]) {
  float temp_offset = 0;
  int num_edge_offset_intervals = ccso_bo_only ? 1 : max_edge_interval;
#if CONFIG_CCSO_IMPROVE
  int this_ccso_offset[8] = { 0 };

  for (int idx = 0; idx < 8; ++idx)
    this_ccso_offset[idx] = ccso_offset[idx] * ccso_scale[scale_idx];
#endif  // CONFIG_CCSO_IMPROVE
  for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
    for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
      for (int band_num = 0; band_num < (1 << max_band_log2); band_num++) {
        const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
        if (chroma_count[lut_idx_ext]) {
          temp_offset =
              (float)chroma_error[lut_idx_ext] / chroma_count[lut_idx_ext];
#if CONFIG_CCSO_IMPROVE
          if ((temp_offset < this_ccso_offset[0]) ||
              (temp_offset >= this_ccso_offset[7])) {
            temp_filter_offset[lut_idx_ext] = clamp(
                (int)temp_offset, this_ccso_offset[0], this_ccso_offset[7]);
#else
          if ((temp_offset < ccso_offset[0]) ||
              (temp_offset >= ccso_offset[7])) {
            temp_filter_offset[lut_idx_ext] =
                clamp((int)temp_offset, ccso_offset[0], ccso_offset[7]);
#endif  // CONFIG_CCSO_IMPROVE
          } else {
            for (int offset_idx = 0; offset_idx < 7; offset_idx++) {
#if CONFIG_CCSO_IMPROVE
              if ((temp_offset >= this_ccso_offset[offset_idx]) &&
                  (temp_offset <= this_ccso_offset[offset_idx + 1])) {
                if (fabs(temp_offset - this_ccso_offset[offset_idx]) >
                    fabs(temp_offset - this_ccso_offset[offset_idx + 1])) {
                  temp_filter_offset[lut_idx_ext] =
                      this_ccso_offset[offset_idx + 1];
#else
              if ((temp_offset >= ccso_offset[offset_idx]) &&
                  (temp_offset <= ccso_offset[offset_idx + 1])) {
                if (fabs(temp_offset - ccso_offset[offset_idx]) >
                    fabs(temp_offset - ccso_offset[offset_idx + 1])) {
                  temp_filter_offset[lut_idx_ext] = ccso_offset[offset_idx + 1];
#endif  // CONFIG_CCSO_IMPROVE
                } else {
#if CONFIG_CCSO_IMPROVE
                  temp_filter_offset[lut_idx_ext] =
                      this_ccso_offset[offset_idx];
#else
                  temp_filter_offset[lut_idx_ext] = ccso_offset[offset_idx];
#endif  // CONFIG_CCSO_IMPROVE
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
                               const uint16_t *rec_uv, int rdmult
#if CONFIG_CCSO_IMPROVE
                               ,
                               bool error_resilient_frame_seen
#endif
#if CONFIG_ENTROPY_STATS
                               ,
                               ThreadData *td
#endif
) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int log2_filter_unit_size_y =
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + xd->plane[1].subsampling_y;
  const int log2_filter_unit_size_x =
      plane > 0 ? CCSO_BLK_SIZE : CCSO_BLK_SIZE + xd->plane[1].subsampling_x;

  const int ccso_nvfb =
      ((mi_params->mi_rows >> xd->plane[plane].subsampling_y) +
       (1 << log2_filter_unit_size_y >> 2) - 1) /
      (1 << log2_filter_unit_size_y >> 2);
  const int ccso_nhfb =
      ((mi_params->mi_cols >> xd->plane[plane].subsampling_x) +
       (1 << log2_filter_unit_size_x >> 2) - 1) /
      (1 << log2_filter_unit_size_x >> 2);
  const int sb_count = ccso_nvfb * ccso_nhfb;
  const int pic_height_c = xd->plane[plane].dst.height;
  const int pic_width_c = xd->plane[plane].dst.width;
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
                     pic_height_c, pic_width_c, ctx->unfiltered_dist_block,
                     ccso_nhfb, &ctx->unfiltered_dist_frame);

#if CONFIG_CCSO_IMPROVE
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
#else
  const double best_unfiltered_cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      rdmult, 1, ctx->unfiltered_dist_frame, cm->seq_params.bit_depth);
  double best_filtered_cost;
  double final_filtered_cost = DBL_MAX;
#endif  // CONFIG_CCSO_IMPROVE

  uint8_t best_edge_classifier = 0;
  uint8_t final_edge_classifier = 0;
  const int total_edge_classifier = 2;
  int8_t filter_offset[CCSO_BAND_NUM * 16];
#if CONFIG_CCSO_FT_SHAPE
  const int total_filter_support = 7;
#else
  const int total_filter_support = 6;
#endif

  const int total_quant_idx = 4;
  const int total_band_log2_plus1 = 4;
  uint8_t frame_bits = 1;
#if CONFIG_CCSO_SIGFIX
  uint8_t frame_bits_bo_only = 1;  // enabling flag
  frame_bits_bo_only += 1;         // bo only flag
  frame_bits += 1;                 // bo only flag
#endif                             // CONFIG_CCSO_SIGFIX
  frame_bits += 2;                 // quant step size
#if CONFIG_CCSO_IMPROVE
  frame_bits += 2;  // scale index
#endif
  frame_bits += 3;  // filter support index
  frame_bits += 1;  // edge_clf
  frame_bits += 2;  // band number log2
#if CONFIG_CCSO_SIGFIX
  frame_bits_bo_only += 3;  // band number log2
#if CONFIG_CCSO_IMPROVE
  frame_bits_bo_only += 2;  // scale index
#endif
#endif  // CONFIG_CCSO_SIGFIX
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
#if CONFIG_CCSO_IMPROVE
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
#endif  // CONFIG_CCSO_IMPROVE
    for (uint8_t ccso_bo_only = 0; ccso_bo_only < 2; ccso_bo_only++) {
#if CONFIG_CCSO_SIGFIX
      int num_filter_iter = ccso_bo_only ? 1 : total_filter_support;
      int num_quant_iter = ccso_bo_only ? 1 : total_quant_idx;
      int num_edge_clf_iter = ccso_bo_only ? 1 : total_edge_classifier;
#else
    int num_filter_iter = total_filter_support;
    int num_quant_iter = total_quant_idx;
    int num_edge_clf_iter = total_edge_classifier;
#endif  // CONFIG_CCSO_SIGFIX
      for (int ext_filter_support = 0; ext_filter_support < num_filter_iter;
           ext_filter_support++) {
        for (int quant_idx = 0; quant_idx < num_quant_iter; quant_idx++) {
          for (int edge_clf = 0; edge_clf < num_edge_clf_iter; edge_clf++) {
            const int max_edge_interval = edge_clf_to_edge_interval[edge_clf];

            if (!ccso_bo_only) {
              ccso_derive_src_info(cm, xd, plane, ext_rec_y,
#if CONFIG_CCSO_IMPROVE
                                   quant_sz[scale_idx][quant_idx],
#else
                                 quant_sz[quant_idx],
#endif
                                   ext_filter_support, src_cls0, src_cls1,
                                   edge_clf, ctx->ccso_stride,
                                   ctx->ccso_stride_ext);
            }
            int num_band_iter = total_band_log2_plus1;
            if (ccso_bo_only) {
#if CONFIG_CCSO_BO_REDUCE
              num_band_iter = total_band_log2_plus1 + 3;
#else
            num_band_iter = total_band_log2_plus1 + 4;
#endif  // CONFIG_CCSO_BO_REDUCE
            }
            for (int max_band_log2 = 0; max_band_log2 < num_band_iter;
                 max_band_log2++) {
              const int shift_bits = cm->seq_params.bit_depth - max_band_log2;
              const int max_band = 1 << max_band_log2;
#if !CONFIG_CCSO_IMPROVE
              best_filtered_cost = DBL_MAX;
              bool ccso_enable = true;
              bool keep_training = true;
              bool improvement = false;
              double prev_total_cost = DBL_MAX;
              int control_idx = 0;
              for (int y = 0; y < ccso_nvfb; y++) {
                for (int x = 0; x < ccso_nhfb; x++) {
                  ctx->filter_control[control_idx] = 1;
                  control_idx++;
                }
              }
#endif  //! CONFIG_CCSO_IMPROVE
              if (ccso_bo_only) {
                for (int band_num = 0; band_num < CCSO_BAND_NUM; band_num++) {
                  memset(ctx->total_class_err_bo[band_num], 0,
                         sizeof(*ctx->total_class_err_bo[band_num]) * sb_count);
                  memset(ctx->total_class_cnt_bo[band_num], 0,
                         sizeof(*ctx->total_class_cnt_bo[band_num]) * sb_count);
                }
                ccso_pre_compute_class_err_bo(ctx, xd, plane, ext_rec_y, org_uv,
                                              rec_uv, shift_bits);
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
                ccso_pre_compute_class_err(ctx, xd, plane, ext_rec_y, org_uv,
                                           rec_uv, src_cls0, src_cls1,
                                           shift_bits);
              }

#if CONFIG_CCSO_IMPROVE
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
                              cm, xd, plane, ext_rec_y, temp_rec_uv_buf,
                              ctx->ccso_stride, filter_offset, src_cls0,
                              src_cls1, shift_bits, ccso_bo_only,
                              ctx->ccso_stride, ctx->ccso_stride_ext);
                        else
                          ccso_try_luma_filter(
                              cm, xd, plane, ext_rec_y, temp_rec_uv_buf,
                              ctx->ccso_stride, filter_offset, src_cls0,
                              src_cls1, shift_bits, ccso_bo_only,
                              ctx->ccso_stride, ctx->ccso_stride_ext);
                        ctx->filtered_dist_frame = 0;
                        compute_distortion(
                            org_uv, ctx->ccso_stride, temp_rec_uv_buf,
                            ctx->ccso_stride, log2_filter_unit_size_y,
                            log2_filter_unit_size_x, pic_height_c, pic_width_c,
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
                        cur_total_rate =
                            av1_cost_literal(reuse_ccso_idx ? 0 : 3);
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

                        cur_total_rate +=
                            (reuse_ccso_idx ? av1_cost_literal(2 + 3)
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
#else
            int training_iter_count = 0;
            while (keep_training) {
              improvement = false;
              if (ccso_enable) {
                memset(ctx->chroma_error, 0, sizeof(ctx->chroma_error));
                memset(ctx->chroma_count, 0, sizeof(ctx->chroma_count));
                memset(filter_offset, 0, sizeof(filter_offset));
                memcpy(temp_rec_uv_buf, rec_uv,
                       sizeof(*temp_rec_uv_buf) * xd->plane[0].dst.height *
                           ctx->ccso_stride);
                ccso_compute_class_err(ctx, cm, plane, xd, max_band_log2,
                                       max_edge_interval, ccso_bo_only);
                derive_lut_offset(filter_offset, max_band_log2,
                                  max_edge_interval, ccso_bo_only,
                                  ctx->chroma_count, ctx->chroma_error);
              }
              memcpy(temp_rec_uv_buf, rec_uv,
                     sizeof(*temp_rec_uv_buf) * xd->plane[0].dst.height *
                         ctx->ccso_stride);
              if (plane > 0)
                ccso_try_chroma_filter(
                    cm, xd, plane, ext_rec_y, temp_rec_uv_buf, ctx->ccso_stride,
                    filter_offset, src_cls0, src_cls1, shift_bits, ccso_bo_only,
                    ctx->ccso_stride, ctx->ccso_stride_ext);
              else
                ccso_try_luma_filter(cm, xd, plane, ext_rec_y, temp_rec_uv_buf,
                                     ctx->ccso_stride, filter_offset, src_cls0,
                                     src_cls1, shift_bits, ccso_bo_only,
                                     ctx->ccso_stride, ctx->ccso_stride_ext);
              ctx->filtered_dist_frame = 0;
              compute_distortion(org_uv, ctx->ccso_stride, temp_rec_uv_buf,
                                 ctx->ccso_stride, log2_filter_unit_size_y,
                                 log2_filter_unit_size_x, pic_height_c,
                                 pic_width_c, ctx->training_dist_block,
                                 ccso_nhfb, &ctx->filtered_dist_frame);

              uint64_t cur_total_dist = 0;
              int cur_total_rate = 0;
              derive_blk_md(cm, xd, plane, ctx->unfiltered_dist_block,
                            ctx->training_dist_block, ctx->filter_control,
                            &cur_total_dist, &cur_total_rate, &ccso_enable);
              if (ccso_enable) {
                const int lut_bits =
                    count_lut_bits(filter_offset, max_band_log2,
                                   max_edge_interval, ccso_bo_only);
                const int cur_total_bits =
                    lut_bits +
                    (
#if CONFIG_CCSO_SIGFIX
                        ccso_bo_only ? frame_bits_bo_only :
#endif  // CONFIG_CCSO_SIGFIX
                                     frame_bits) +
                    sb_count;
                const double cur_total_cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
                    rdmult, cur_total_bits, cur_total_dist,
                    cm->seq_params.bit_depth);
                if (cur_total_cost < prev_total_cost) {
                  prev_total_cost = cur_total_cost;
                  improvement = true;
                }
                if (cur_total_cost < best_filtered_cost) {
                  best_filtered_cost = cur_total_cost;
                  ctx->best_filter_enabled[plane] = ccso_enable;
                  memcpy(ctx->best_filter_offset[plane], filter_offset,
                         sizeof(filter_offset));
                  best_edge_classifier = edge_clf;
                  memcpy(ctx->best_filter_control, ctx->filter_control,
                         sizeof(*ctx->filter_control) * sb_count);
                }
              }
              training_iter_count++;
              if (!improvement || training_iter_count > CCSO_MAX_ITERATIONS) {
                keep_training = false;
              }
            }

            if (best_filtered_cost < final_filtered_cost) {
              final_filtered_cost = best_filtered_cost;
              ctx->final_filter_enabled[plane] =
                  ctx->best_filter_enabled[plane];
              ctx->final_quant_idx[plane] = quant_idx;
              ctx->final_ext_filter_support[plane] = ext_filter_support;
              ctx->final_ccso_bo_only[plane] = ccso_bo_only;
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
  }
#endif  // CONFIG_CCSO_IMPROVE

  if (best_unfiltered_cost < final_filtered_cost) {
    memset(ctx->final_filter_control, 0,
           sizeof(*ctx->final_filter_control) * sb_count);
    cm->ccso_info.ccso_enable[plane] = false;
  } else {
    cm->ccso_info.ccso_enable[plane] = true;
  }

#if CONFIG_CCSO_IMPROVE
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
#endif  // CONFIG_CCSO_IMPROVE
  if (cm->ccso_info.ccso_enable[plane]) {
#if CONFIG_CCSO_IMPROVE
    cm->cur_frame->ccso_info.ccso_enable[plane] = 1;
    cm->cur_frame->ccso_info.reuse_root_ref[plane] =
        cm->current_frame.display_order_hint;
    cm->ccso_info.sb_reuse_ccso[plane] = ctx->final_sb_reuse_ccso[plane];
    const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
    const int bw = mi_size_wide[bsize];
    const int bh = mi_size_high[bsize];
    const int log2_w = CCSO_BLK_SIZE + xd->plane[1].subsampling_x;
    const int log2_h = CCSO_BLK_SIZE + xd->plane[1].subsampling_y;
    const int f_w = 1 << log2_w >> MI_SIZE_LOG2;
    const int f_h = 1 << log2_h >> MI_SIZE_LOG2;
    const int step_h = (bh + f_h - 1) / f_h;
    const int step_w = (bw + f_w - 1) / f_w;

    if (!cm->ccso_info.sb_reuse_ccso[plane]) {
#endif  // CONFIG_CCSO_IMPROVE
      for (int y_sb = 0; y_sb < ccso_nvfb; y_sb += step_h) {
        for (int x_sb = 0; x_sb < ccso_nhfb; x_sb += step_w) {
#if CONFIG_CCSO_IMPROVE
          for (int row = y_sb; row < y_sb + step_h; row++) {
            for (int col = x_sb; col < x_sb + step_w; col++) {
              int sb_idx = row * ccso_nhfb + col;
              cm->cur_frame->ccso_info.sb_filter_control[plane][sb_idx] =
                  ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
#endif  // CONFIG_CCSO_IMPROVE
              if (plane == AOM_PLANE_Y) {
                mi_params
                    ->mi_grid_base
                        [(1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[1].subsampling_y)) *
                             row * mi_params->mi_stride +
                         (1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[1].subsampling_x)) *
                             col]
                    ->ccso_blk_y =
                    ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              } else if (plane == AOM_PLANE_U) {
                mi_params
                    ->mi_grid_base
                        [(1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[1].subsampling_y)) *
                             row * mi_params->mi_stride +
                         (1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[1].subsampling_x)) *
                             col]
                    ->ccso_blk_u =
                    ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              } else {
                mi_params
                    ->mi_grid_base
                        [(1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[2].subsampling_y)) *
                             row * mi_params->mi_stride +
                         (1 << CCSO_BLK_SIZE >>
                          (MI_SIZE_LOG2 - xd->plane[2].subsampling_x)) *
                             col]
                    ->ccso_blk_v =
                    ctx->final_filter_control[y_sb * ccso_nhfb + x_sb];
              }
#if CONFIG_CCSO_IMPROVE
              const int ccso_mib_size_y =
                  (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_y -
                         MI_SIZE_LOG2));
              const int ccso_mib_size_x =
                  (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_x -
                         MI_SIZE_LOG2));

              int mi_row = (1 << CCSO_BLK_SIZE >>
                            (MI_SIZE_LOG2 - xd->plane[1].subsampling_y)) *
                           row;
              int mi_col = (1 << CCSO_BLK_SIZE >>
                            (MI_SIZE_LOG2 - xd->plane[1].subsampling_x)) *
                           col;
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
#endif  // CONFIG_CCSO_IMPROVE

#if CONFIG_ENTROPY_STATS
#if CONFIG_CCSO_IMPROVE
          const int ccso_ctx = get_ccso_context(y_sb, x_sb, ccso_nhfb,
                                                ctx->final_filter_control);

          ++td->counts->default_ccso_cnts
                [plane][ccso_ctx]
                [ctx->final_filter_control[y_sb * ccso_nhfb + x_sb]];
#endif  // CONFIG_CCSO_IMPROVE
#endif
        }
      }
    }
#if CONFIG_CCSO_IMPROVE
    else {
      assert(ref_frame_ccso_info != NULL);

      memcpy(cm->cur_frame->ccso_info.sb_filter_control[plane],
             ref_frame_ccso_info->sb_filter_control[plane],
             sizeof(*cm->cur_frame->ccso_info.sb_filter_control[plane]) *
                 sb_count);

      for (int y_sb = 0; y_sb < ccso_nvfb; y_sb++) {
        for (int x_sb = 0; x_sb < ccso_nhfb; x_sb++) {
          if (plane == AOM_PLANE_Y) {
            mi_params
                ->mi_grid_base[(1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[1].subsampling_y)) *
                                   y_sb * mi_params->mi_stride +
                               (1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[1].subsampling_x)) *
                                   x_sb]
                ->ccso_blk_y =
                ref_frame_ccso_info
                    ->sb_filter_control[plane][y_sb * ccso_nhfb + x_sb];
          } else if (plane == AOM_PLANE_U) {
            mi_params
                ->mi_grid_base[(1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[1].subsampling_y)) *
                                   y_sb * mi_params->mi_stride +
                               (1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[1].subsampling_x)) *
                                   x_sb]
                ->ccso_blk_u =
                ref_frame_ccso_info
                    ->sb_filter_control[plane][y_sb * ccso_nhfb + x_sb];
          } else {
            mi_params
                ->mi_grid_base[(1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[2].subsampling_y)) *
                                   y_sb * mi_params->mi_stride +
                               (1 << CCSO_BLK_SIZE >>
                                (MI_SIZE_LOG2 - xd->plane[2].subsampling_x)) *
                                   x_sb]
                ->ccso_blk_v =
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
#else
  memcpy(cm->ccso_info.filter_offset[plane], ctx->final_filter_offset[plane],
         sizeof(ctx->final_filter_offset[plane]));
  cm->ccso_info.quant_idx[plane] = ctx->final_quant_idx[plane];
  cm->ccso_info.ext_filter_support[plane] =
      ctx->final_ext_filter_support[plane];
  cm->ccso_info.ccso_bo_only[plane] = ctx->final_ccso_bo_only[plane];
  cm->ccso_info.max_band_log2[plane] = ctx->final_band_log2;
  cm->ccso_info.edge_clf[plane] = final_edge_classifier;
#endif  // CONFIG_CCSO_IMPROVE
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
                 uint16_t *org_uv[3]
#if CONFIG_CCSO_IMPROVE
                 ,
                 bool error_resilient_frame_seen
#endif
#if CONFIG_ENTROPY_STATS
                 ,
                 ThreadData *td
#endif
) {
  int rdmult_weight = clamp(cm->quant_params.base_qindex >> 3, 1, 37);
  int64_t rdmult_temp = (int64_t)rdmult * (int64_t)rdmult_weight;
#if CONFIG_CCSO_IMPROVE
  if (rdmult_temp >= INT_MAX
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      || is_global_intrabc_allowed(cm)
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  ) {
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
#else
  if (rdmult_temp < INT_MAX)
    rdmult = (int)rdmult_temp;
  else
    return;
#endif  // CONFIG_CCSO_IMPROVE
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                       NULL);

  CcsoCtx *const ctx = aom_calloc(1, sizeof(CcsoCtx));
  ctx->ccso_stride = xd->plane[0].dst.width;
  ctx->ccso_stride_ext = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  derive_ccso_filter(ctx, cm, AOM_PLANE_Y, xd, org_uv[AOM_PLANE_Y], ext_rec_y,
                     rec_uv[AOM_PLANE_Y], rdmult
#if CONFIG_CCSO_IMPROVE
                     ,
                     error_resilient_frame_seen
#endif
#if CONFIG_ENTROPY_STATS
                     ,
                     td
#endif
  );

#if CONFIG_D143_CCSO_FM_FLAG
  cm->ccso_info.ccso_frame_flag = cm->ccso_info.ccso_enable[0];
#endif  // CONFIG_D143_CCSO_FM_FLAG
  if (num_planes > 1) {
#if CONFIG_CCSO_IMPROVE
    rdmult = (rdmult * 7) >> 3;
#endif  // CONFIG_CCSO_IMPROVE
    derive_ccso_filter(ctx, cm, AOM_PLANE_U, xd, org_uv[AOM_PLANE_U], ext_rec_y,
                       rec_uv[AOM_PLANE_U], rdmult
#if CONFIG_CCSO_IMPROVE
                       ,
                       error_resilient_frame_seen
#endif
#if CONFIG_ENTROPY_STATS
                       ,
                       td
#endif
    );
    derive_ccso_filter(ctx, cm, AOM_PLANE_V, xd, org_uv[AOM_PLANE_V], ext_rec_y,
                       rec_uv[AOM_PLANE_V], rdmult
#if CONFIG_CCSO_IMPROVE
                       ,
                       error_resilient_frame_seen
#endif
#if CONFIG_ENTROPY_STATS
                       ,
                       td
#endif
    );
#if CONFIG_D143_CCSO_FM_FLAG
    cm->ccso_info.ccso_frame_flag |= cm->ccso_info.ccso_enable[1];
    cm->ccso_info.ccso_frame_flag |= cm->ccso_info.ccso_enable[2];
#endif  // CONFIG_D143_CCSO_FM_FLAG
  }
  aom_free(ctx);
}
