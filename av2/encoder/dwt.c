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
#include <stdlib.h>
#include <math.h>

#include "config/av2_rtcd.h"
#include "av2/encoder/dwt.h"

// Note: block length must be even for this implementation
static void analysis_53_row(int length, tran_low_t *x, tran_low_t *lowpass,
                            tran_low_t *highpass) {
  int n;
  tran_low_t r, *a, *b;

  n = length >> 1;
  b = highpass;
  a = lowpass;
  while (--n) {
    *a++ = (r = *x++) * 2;
    *b++ = *x - ((r + x[1] + 1) >> 1);
    x++;
  }
  *a = (r = *x++) * 2;
  *b = *x - r;

  n = length >> 1;
  b = highpass;
  a = lowpass;
  r = *highpass;
  while (n--) {
    *a++ += (r + (*b) + 1) >> 1;
    r = *b++;
  }
}

static void analysis_53_col(int length, tran_low_t *x, tran_low_t *lowpass,
                            tran_low_t *highpass) {
  int n;
  tran_low_t r, *a, *b;

  n = length >> 1;
  b = highpass;
  a = lowpass;
  while (--n) {
    *a++ = (r = *x++);
    *b++ = (((*x) * 2) - (r + x[1]) + 2) >> 2;
    x++;
  }
  *a = (r = *x++);
  *b = (*x - r + 1) >> 1;

  n = length >> 1;
  b = highpass;
  a = lowpass;
  r = *highpass;
  while (n--) {
    *a++ += (r + (*b) + 1) >> 1;
    r = *b++;
  }
}

static void dyadic_analyze_53_uint8_input(int levels, int width, int height,
                                          uint16_t *x, int pitch_x,
                                          tran_low_t *c, int pitch_c,
                                          int dwt_scale_bits) {
  int lv, i, j, nh, nw, hh = height, hw = width;
  tran_low_t buffer[2 * DWT_MAX_LENGTH];

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      c[i * pitch_c + j] = x[i * pitch_x + j] << dwt_scale_bits;
    }
  }

  for (lv = 0; lv < levels; lv++) {
    nh = hh;
    hh = (hh + 1) >> 1;
    nw = hw;
    hw = (hw + 1) >> 1;
    if ((nh < 2) || (nw < 2)) return;
    for (i = 0; i < nh; i++) {
      memcpy(buffer, &c[i * pitch_c], nw * sizeof(tran_low_t));
      analysis_53_row(nw, buffer, &c[i * pitch_c], &c[i * pitch_c] + hw);
    }
    for (j = 0; j < nw; j++) {
      for (i = 0; i < nh; i++) buffer[i + nh] = c[i * pitch_c + j];
      analysis_53_col(nh, buffer + nh, buffer, buffer + hh);
      for (i = 0; i < nh; i++) c[i * pitch_c + j] = buffer[i];
    }
  }
}

void av2_fdwt8x8_uint8_input_c(uint16_t *input, tran_low_t *output,
                               int stride) {
  dyadic_analyze_53_uint8_input(4, 8, 8, input, stride, output, 8, 2);
}

int av2_haar_ac_sad(tran_low_t *output, int bw, int bh, int stride) {
  int acsad = 0;

  for (int r = 0; r < bh; ++r)
    for (int c = 0; c < bw; ++c) {
      if (r >= bh / 2 || c >= bw / 2) acsad += abs(output[r * stride + c]);
    }
  return acsad;
}

int av2_haar_ac_sad_8x8_uint8_input(uint16_t *input, int stride) {
  tran_low_t output[64];

  av2_fdwt8x8_uint8_input_c(input, output, stride);
  return av2_haar_ac_sad(output, 8, 8, 8);
}
