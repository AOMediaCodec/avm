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
#ifndef AOM_AV1_AV1_IFACE_COMMON_H_
#define AOM_AV1_AV1_IFACE_COMMON_H_

#include <assert.h>

#include "aom_ports/mem.h"
#include "aom_scale/yv12config.h"
#include "av1/common/enums.h"

/* Constant value specifying size of subgop*/
#define MAX_SUBGOP_SIZE 32
typedef struct {
  int disp_frame_idx;
  int is_filtered;
  int show_frame;
  int show_existing_frame;
  int pyramid_level;
  int qindex;

  int refresh_frame_flags;
  int num_references;
  int ref_frame_pyr_level[INTER_REFS_PER_FRAME];
  int ref_frame_disp_order[INTER_REFS_PER_FRAME];
  int is_valid_ref_frame[INTER_REFS_PER_FRAME];
  unsigned int ref_frame_map[REF_FRAMES];
} SubGOPStepData;

typedef struct {
  int num_steps;
  int step_idx_enc;
  int step_idx_dec;
  SubGOPStepData step[MAX_SUBGOP_SIZE];
} SubGOPData;

static void yuvconfig2image(aom_image_t *img, const YV12_BUFFER_CONFIG *yv12,
                            void *user_priv) {
  /* aom_img_wrap() doesn't allow specifying independent strides for
   * the Y, U, and V planes, nor other alignment adjustments that
   * might be representable by a YV12_BUFFER_CONFIG, so we just
   * initialize all the fields.
   */
  int bps;
  if (!yv12->subsampling_y) {
    if (!yv12->subsampling_x) {
      img->fmt = AOM_IMG_FMT_I444;
      bps = 24;
    } else {
      img->fmt = AOM_IMG_FMT_I422;
      bps = 16;
    }
  } else {
    img->fmt = AOM_IMG_FMT_I420;
    bps = 12;
  }
  img->cp = yv12->color_primaries;
  img->tc = yv12->transfer_characteristics;
  img->mc = yv12->matrix_coefficients;
  img->monochrome = yv12->monochrome;
  img->csp = yv12->chroma_sample_position;
  img->range = yv12->color_range;
  img->bit_depth = 8;
  img->w = yv12->y_width;
  img->h = yv12->y_height;
  img->d_w = yv12->y_crop_width;
  img->d_h = yv12->y_crop_height;
  img->r_w = yv12->render_width;
  img->r_h = yv12->render_height;
  img->x_chroma_shift = yv12->subsampling_x;
  img->y_chroma_shift = yv12->subsampling_y;
  bps *= 2;
  // aom_image_t uses byte strides and a pointer to the first byte
  // of the image.
  img->fmt = (aom_img_fmt_t)(img->fmt | AOM_IMG_FMT_HIGHBITDEPTH);
  img->bit_depth = yv12->bit_depth;
  img->planes[AOM_PLANE_Y] = (uint8_t *)yv12->y_buffer;
  img->planes[AOM_PLANE_U] = (uint8_t *)yv12->u_buffer;
  img->planes[AOM_PLANE_V] = (uint8_t *)yv12->v_buffer;
  img->stride[AOM_PLANE_Y] = 2 * yv12->y_stride;
  img->stride[AOM_PLANE_U] = 2 * yv12->uv_stride;
  img->stride[AOM_PLANE_V] = 2 * yv12->uv_stride;
  img->bps = bps;
  img->user_priv = user_priv;
  img->img_data = yv12->buffer_alloc;
  img->img_data_owner = 0;
  img->self_allocd = 0;
  img->sz = yv12->frame_size;
  assert(!yv12->metadata);
  img->metadata = NULL;
#if CONFIG_CROP_WIN_CWG_F220
  img->w_conf_win_enabled_flag = yv12->w_conf_win_enabled_flag;
  if (img->w_conf_win_enabled_flag) {
    img->w_conf_win_left_offset = yv12->w_win_left_offset;
    img->w_conf_win_right_offset = yv12->w_win_right_offset;
    img->w_conf_win_top_offset = yv12->w_win_top_offset;
    img->w_conf_win_bottom_offset = yv12->w_win_bottom_offset;

    // Determine subsampling factors
    const int ss_x = img->monochrome ? img->x_chroma_shift : 0;
    const int ss_y = img->monochrome ? img->y_chroma_shift : 0;

    // Convert
    const int plane_left_offset = img->w_conf_win_left_offset >> ss_x;
    const int plane_right_offset = img->w_conf_win_right_offset >> ss_x;
    const int plane_top_offset = img->w_conf_win_top_offset >> ss_y;
    const int plane_bottom_offset = img->w_conf_win_bottom_offset >> ss_y;

    // Calculate cropping positions
    int left_pos_x = plane_left_offset;
    int right_pos_x = img->d_w - 1 - plane_right_offset;
    int top_pos_y = plane_top_offset;
    int bottom_pos_y = img->d_h - 1 - plane_bottom_offset;

    // Calculate the cropped size
    img->crop_width = right_pos_x - left_pos_x + 1;
    img->crop_height = bottom_pos_y - top_pos_y + 1;
    img->w = img->d_w;
    img->h = img->d_h;
    img->d_w = img->crop_width;
    img->d_h = img->crop_height;
  } else {
    img->w_conf_win_bottom_offset = 0;
    img->w_conf_win_left_offset = 0;
    img->w_conf_win_right_offset = 0;
    img->w_conf_win_top_offset = 0;
  }
  img->max_width = yv12->max_width;
  img->max_height = yv12->max_height;
#endif  // CONFIG_CROP_WIN_CWG_F220
}

static aom_codec_err_t image2yuvconfig(const aom_image_t *img,
                                       YV12_BUFFER_CONFIG *yv12) {
  yv12->y_buffer = (uint16_t *)img->planes[AOM_PLANE_Y];
  yv12->u_buffer = (uint16_t *)img->planes[AOM_PLANE_U];
  yv12->v_buffer = (uint16_t *)img->planes[AOM_PLANE_V];

  yv12->y_crop_width = img->d_w;
  yv12->y_crop_height = img->d_h;
  yv12->render_width = img->r_w;
  yv12->render_height = img->r_h;
  yv12->y_width = img->w;
  yv12->y_height = img->h;

  yv12->uv_width =
      img->x_chroma_shift == 1 ? (1 + yv12->y_width) / 2 : yv12->y_width;
  yv12->uv_height =
      img->y_chroma_shift == 1 ? (1 + yv12->y_height) / 2 : yv12->y_height;
  yv12->uv_crop_width = yv12->uv_width;
  yv12->uv_crop_height = yv12->uv_height;

  yv12->y_stride = img->stride[AOM_PLANE_Y];
  yv12->uv_stride = img->stride[AOM_PLANE_U];
  yv12->color_primaries = img->cp;
  yv12->transfer_characteristics = img->tc;
  yv12->matrix_coefficients = img->mc;
  yv12->monochrome = img->monochrome;
  yv12->chroma_sample_position = img->csp;
  yv12->color_range = img->range;

  // In aom_image_t
  //     planes point to uint8 address of start of data
  //     stride counts uint8s to reach next row
  // In YV12_BUFFER_CONFIG
  //     y_buffer, u_buffer, v_buffer point to uint16 address of data
  //     stride and border counts in uint16s
  // This means that all the address calculations in the main body of code
  // should work correctly.
  // However, before we do any pixel operations we need to cast the address
  // to a uint16 ponter and double its value.
  yv12->y_stride >>= 1;
  yv12->uv_stride >>= 1;

  // Note(yunqing): if img is allocated the same as the frame buffer, y_stride
  // is 32-byte aligned. Also, handle the cases while allocating img without a
  // border or stride_align is less than 32.
  int border = (yv12->y_stride - (int)((img->w + 31) & ~31)) / 2;
  yv12->border = (border < 0) ? 0 : border;
  yv12->subsampling_x = img->x_chroma_shift;
  yv12->subsampling_y = img->y_chroma_shift;
  yv12->metadata = img->metadata;
  return AOM_CODEC_OK;
}

static void image2yuvconfig_upshift(aom_image_t *hbd_img,
                                    const aom_image_t *img,
                                    YV12_BUFFER_CONFIG *yv12) {
  aom_img_upshift(hbd_img, img, 0);
  // Copy some properties aom_img_upshift() ignores
  hbd_img->cp = img->cp;
  hbd_img->tc = img->tc;
  hbd_img->mc = img->mc;
  hbd_img->monochrome = img->monochrome;
  hbd_img->csp = img->csp;
  hbd_img->range = img->range;
  image2yuvconfig(hbd_img, yv12);
  yv12->metadata = img->metadata;
}
#endif  // AOM_AV1_AV1_IFACE_COMMON_H_
