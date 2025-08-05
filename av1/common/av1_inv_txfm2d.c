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

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "av1/common/enums.h"
#include "av1/common/av1_txfm.h"

void av1_highbd_iwht4x4_16_add_c(const tran_low_t *input, uint16_t *dest,
                                 int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    range_check_value(a1, bd + 1);
    range_check_value(b1, bd + 1);
    range_check_value(c1, bd + 1);
    range_check_value(d1, bd + 1);

    dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
    dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], b1, bd);
    dest[stride * 2] = highbd_clip_pixel_add(dest[stride * 2], c1, bd);
    dest[stride * 3] = highbd_clip_pixel_add(dest[stride * 3], d1, bd);

    ip++;
    dest++;
  }
}

// perform 16 coefficents 4x4 inverse Hadamard transform for vertical DPCM
void av1_highbd_iwht4x4_16_vert_add_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    range_check_value(a1, bd + 1);
    range_check_value(a1 + b1, bd + 1);
    range_check_value(a1 + b1 + c1, bd + 1);
    range_check_value(a1 + b1 + c1 + d1, bd + 1);

    dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
    dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], a1 + b1, bd);
    dest[stride * 2] =
        highbd_clip_pixel_add(dest[stride * 2], a1 + b1 + c1, bd);
    dest[stride * 3] =
        highbd_clip_pixel_add(dest[stride * 3], a1 + b1 + c1 + d1, bd);

    ip++;
    dest++;
  }
}

// perform 16 coefficents 4x4 inverse Hadamard transform for horizontal DPCM
void av1_highbd_iwht4x4_16_horz_add_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  tran_low_t a1_delay = 0;
  tran_low_t b1_delay = 0;
  tran_low_t c1_delay = 0;
  tran_low_t d1_delay = 0;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    if (i == 0) {
      range_check_value(a1, bd + 1);
      range_check_value(b1, bd + 1);
      range_check_value(c1, bd + 1);
      range_check_value(d1, bd + 1);
      dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
      dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], b1, bd);
      dest[stride * 2] = highbd_clip_pixel_add(dest[stride * 2], c1, bd);
      dest[stride * 3] = highbd_clip_pixel_add(dest[stride * 3], d1, bd);
      a1_delay = a1;
      b1_delay = b1;
      c1_delay = c1;
      d1_delay = d1;
    } else {
      range_check_value(a1 + a1_delay, bd + 1);
      range_check_value(b1 + b1_delay, bd + 1);
      range_check_value(c1 + c1_delay, bd + 1);
      range_check_value(d1 + d1_delay, bd + 1);
      dest[stride * 0] =
          highbd_clip_pixel_add(dest[stride * 0], a1 + a1_delay, bd);
      dest[stride * 1] =
          highbd_clip_pixel_add(dest[stride * 1], b1 + b1_delay, bd);
      dest[stride * 2] =
          highbd_clip_pixel_add(dest[stride * 2], c1 + c1_delay, bd);
      dest[stride * 3] =
          highbd_clip_pixel_add(dest[stride * 3], d1 + d1_delay, bd);
      a1_delay = a1_delay + a1;
      b1_delay = b1_delay + b1;
      c1_delay = c1_delay + c1;
      d1_delay = d1_delay + d1;
    }

    ip++;
    dest++;
  }
}

void av1_highbd_iwht4x4_1_add_c(const tran_low_t *in, uint16_t *dest,
                                int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;
  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    dest[dest_stride * 0] =
        highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
    dest[dest_stride * 1] =
        highbd_clip_pixel_add(dest[dest_stride * 1], e1, bd);
    dest[dest_stride * 2] =
        highbd_clip_pixel_add(dest[dest_stride * 2], e1, bd);
    dest[dest_stride * 3] =
        highbd_clip_pixel_add(dest[dest_stride * 3], e1, bd);
    ip++;
    dest++;
  }
}

// perform 1 coefficents 4x4 inverse idetity transform for vertical DPCM
void av1_highbd_iwht4x4_1_vert_add_c(const tran_low_t *in, uint16_t *dest,
                                     int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;
  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    range_check_value(a1, bd + 1);
    range_check_value(a1 + e1, bd + 1);
    range_check_value(a1 + e1 + e1, bd + 1);
    range_check_value(a1 + e1 + e1 + e1, bd + 1);
    dest[dest_stride * 0] =
        highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
    dest[dest_stride * 1] =
        highbd_clip_pixel_add(dest[dest_stride * 1], a1 + e1, bd);
    dest[dest_stride * 2] =
        highbd_clip_pixel_add(dest[dest_stride * 2], a1 + e1 + e1, bd);
    dest[dest_stride * 3] =
        highbd_clip_pixel_add(dest[dest_stride * 3], a1 + e1 + e1 + e1, bd);
    ip++;
    dest++;
  }
}

// perform 1 coefficents 4x4 inverse idetity transform for horizontal DPCM
void av1_highbd_iwht4x4_1_horz_add_c(const tran_low_t *in, uint16_t *dest,
                                     int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;

  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  // tran_low_t *ip_delay = tmp;
  tran_low_t a1_delay = 0;
  tran_low_t e1_delay = 0;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    if (i == 0) {
      range_check_value(a1, bd + 1);
      range_check_value(e1, bd + 1);
      dest[dest_stride * 0] =
          highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
      dest[dest_stride * 1] =
          highbd_clip_pixel_add(dest[dest_stride * 1], e1, bd);
      dest[dest_stride * 2] =
          highbd_clip_pixel_add(dest[dest_stride * 2], e1, bd);
      dest[dest_stride * 3] =
          highbd_clip_pixel_add(dest[dest_stride * 3], e1, bd);
      a1_delay = a1;
      e1_delay = e1;
    } else {
      range_check_value(a1 + a1_delay, bd + 1);
      range_check_value(e1 + e1_delay, bd + 1);
      dest[dest_stride * 0] =
          highbd_clip_pixel_add(dest[dest_stride * 0], a1 + a1_delay, bd);
      dest[dest_stride * 1] =
          highbd_clip_pixel_add(dest[dest_stride * 1], e1 + e1_delay, bd);
      dest[dest_stride * 2] =
          highbd_clip_pixel_add(dest[dest_stride * 2], e1 + e1_delay, bd);
      dest[dest_stride * 3] =
          highbd_clip_pixel_add(dest[dest_stride * 3], e1 + e1_delay, bd);
      a1_delay = a1 + a1_delay;
      e1_delay = e1 + e1_delay;
    }

    ip++;
    dest++;
  }
}
