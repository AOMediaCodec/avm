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

#include "avm_dsp/avm_dsp_common.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

#include "av2/common/common.h"
#include "av2/encoder/extend.h"

static void highbd_copy_and_extend_plane(const uint16_t *src, int src_pitch,
                                         uint16_t *dst, int dst_pitch, int w,
                                         int h, int extend_top, int extend_left,
                                         int extend_bottom, int extend_right) {
  int i, linesize;

  // copy the left and right most columns out
  const uint16_t *src_ptr1 = src;
  const uint16_t *src_ptr2 = src + w - 1;
  uint16_t *dst_ptr1 = dst - extend_left;
  uint16_t *dst_ptr2 = dst + w;

  for (i = 0; i < h; i++) {
    avm_memset16(dst_ptr1, src_ptr1[0], extend_left);
    memcpy(dst_ptr1 + extend_left, src_ptr1, w * sizeof(src_ptr1[0]));
    avm_memset16(dst_ptr2, src_ptr2[0], extend_right);
    src_ptr1 += src_pitch;
    src_ptr2 += src_pitch;
    dst_ptr1 += dst_pitch;
    dst_ptr2 += dst_pitch;
  }

  // Now copy the top and bottom lines into each line of the respective
  // borders
  src_ptr1 = dst - extend_left;
  src_ptr2 = dst + dst_pitch * (h - 1) - extend_left;
  dst_ptr1 = dst + dst_pitch * (-extend_top) - extend_left;
  dst_ptr2 = dst + dst_pitch * (h)-extend_left;
  linesize = extend_left + extend_right + w;

  for (i = 0; i < extend_top; i++) {
    memcpy(dst_ptr1, src_ptr1, linesize * sizeof(src_ptr1[0]));
    dst_ptr1 += dst_pitch;
  }

  for (i = 0; i < extend_bottom; i++) {
    memcpy(dst_ptr2, src_ptr2, linesize * sizeof(src_ptr2[0]));
    dst_ptr2 += dst_pitch;
  }
}

void av2_copy_and_extend_frame(const YV12_BUFFER_CONFIG *src,
                               YV12_BUFFER_CONFIG *dst) {
  // Extend src frame in buffer
  const int et_y = dst->border;
  const int el_y = dst->border;
  const int er_y =
      AVMMAX(src->y_width + dst->border, ALIGN_POWER_OF_TWO(src->y_width, 6)) -
      src->y_crop_width;
  const int eb_y = AVMMAX(src->y_height + dst->border,
                          ALIGN_POWER_OF_TWO(src->y_height, 6)) -
                   src->y_crop_height;
  const int uv_width_subsampling = (src->uv_width != src->y_width);
  const int uv_height_subsampling = (src->uv_height != src->y_height);
  const int et_uv = et_y >> uv_height_subsampling;
  const int el_uv = el_y >> uv_width_subsampling;
  const int eb_uv = eb_y >> uv_height_subsampling;
  const int er_uv = er_y >> uv_width_subsampling;

  highbd_copy_and_extend_plane(src->y_buffer, src->y_stride, dst->y_buffer,
                               dst->y_stride, src->y_crop_width,
                               src->y_crop_height, et_y, el_y, eb_y, er_y);
  if (!src->monochrome) {
    highbd_copy_and_extend_plane(
        src->u_buffer, src->uv_stride, dst->u_buffer, dst->uv_stride,
        src->uv_crop_width, src->uv_crop_height, et_uv, el_uv, eb_uv, er_uv);
    highbd_copy_and_extend_plane(
        src->v_buffer, src->uv_stride, dst->v_buffer, dst->uv_stride,
        src->uv_crop_width, src->uv_crop_height, et_uv, el_uv, eb_uv, er_uv);
  }
}
