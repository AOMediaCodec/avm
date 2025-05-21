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

#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/psnr.h"
#include "aom_scale/yv12config.h"

#define MIN_SSE 0.5

double aom_sse_to_psnr(double samples, double peak, double sse) {
  const bool zero_sse = (sse < MIN_SSE);
  if (zero_sse) sse = MIN_SSE;
  assert(sse > 0.0);
  double psnr = 10.0 * log10(samples * peak * peak / sse);
  if (zero_sse) psnr = ceil(psnr);
  return psnr;
}

static void encoder_highbd_variance64(const uint16_t *a, int a_stride,
                                      const uint16_t *b, int b_stride, int w,
                                      int h, uint64_t *sse, int64_t *sum) {
  int64_t tsum = 0;
  uint64_t tsse = 0;
  for (int i = 0; i < h; ++i) {
    int32_t lsum = 0;
    for (int j = 0; j < w; ++j) {
      const int diff = a[j] - b[j];
      lsum += diff;
      tsse += (uint32_t)(diff * diff);
    }
    tsum += lsum;
    a += a_stride;
    b += b_stride;
  }
  *sum = tsum;
  *sse = tsse;
}

static void encoder_highbd_8_variance(const uint16_t *a8, int a_stride,
                                      const uint16_t *b8, int b_stride, int w,
                                      int h, unsigned int *sse, int *sum) {
  uint64_t sse_long = 0;
  int64_t sum_long = 0;
  encoder_highbd_variance64(a8, a_stride, b8, b_stride, w, h, &sse_long,
                            &sum_long);
  *sse = (unsigned int)sse_long;
  *sum = (int)sum_long;
}

static int64_t highbd_get_sse_shift(const uint16_t *a, int a_stride,
                                    const uint16_t *b, int b_stride, int width,
                                    int height, unsigned int down_shift,
                                    int bit_depth) {
  (void)bit_depth;
  int64_t total_sse = 0;
  int x, y;
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      int64_t diff;
#if CONFIG_ZERO_OFFSET_BITUPSHIFT
      uint16_t av = (uint16_t)ROUND_POWER_OF_TWO(a[x], down_shift);
      av = (uint16_t)clip_pixel_highbd(av, bit_depth);
      uint16_t bv = (uint16_t)ROUND_POWER_OF_TWO(b[x], down_shift);
      bv = (uint16_t)clip_pixel_highbd(bv, bit_depth);
      diff = (av - bv);
#else
      diff = (a[x] >> down_shift) - (b[x] >> down_shift);
#endif  // CONFIG_ZERO_OFFSET_BITUPSHIFT
      total_sse += diff * diff;
    }
    a += a_stride;
    b += b_stride;
  }
  return total_sse;
}

static int64_t highbd_get_sse(const uint16_t *a, int a_stride,
                              const uint16_t *b, int b_stride, int width,
                              int height) {
  int64_t total_sse = 0;
  int x, y;
  const int dw = width % 16;
  const int dh = height % 16;
  unsigned int sse = 0;
  int sum = 0;
  if (dw > 0) {
    encoder_highbd_8_variance(&a[width - dw], a_stride, &b[width - dw],
                              b_stride, dw, height, &sse, &sum);
    total_sse += sse;
  }
  if (dh > 0) {
    encoder_highbd_8_variance(&a[(height - dh) * a_stride], a_stride,
                              &b[(height - dh) * b_stride], b_stride,
                              width - dw, dh, &sse, &sum);
    total_sse += sse;
  }
  for (y = 0; y < height / 16; ++y) {
    const uint16_t *pa = a;
    const uint16_t *pb = b;
    for (x = 0; x < width / 16; ++x) {
      aom_highbd_8_mse16x16(pa, a_stride, pb, b_stride, &sse);
      total_sse += sse;
      pa += 16;
      pb += 16;
    }
    a += 16 * a_stride;
    b += 16 * b_stride;
  }
  return total_sse;
}

uint64_t aom_highbd_get_y_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height) {
  return aom_var_2d_u16(a->y_buffer + vstart * a->y_stride + hstart,
                        a->y_stride, width, height) /
         (width * height);
}

uint64_t aom_highbd_get_u_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height) {
  return aom_var_2d_u16(a->u_buffer + vstart * a->uv_stride + hstart,
                        a->uv_stride, width, height) /
         (width * height);
}

uint64_t aom_highbd_get_v_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height) {
  return aom_var_2d_u16(a->v_buffer + vstart * a->uv_stride + hstart,
                        a->uv_stride, width, height) /
         (width * height);
}

int64_t aom_highbd_get_y_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height) {
  return highbd_get_sse(
      a->y_buffer + vstart * a->y_stride + hstart, a->y_stride,
      b->y_buffer + vstart * b->y_stride + hstart, b->y_stride, width, height);
}

int64_t aom_highbd_get_y_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b) {
  assert(a->y_crop_width == b->y_crop_width);
  assert(a->y_crop_height == b->y_crop_height);

  return highbd_get_sse(a->y_buffer, a->y_stride, b->y_buffer, b->y_stride,
                        a->y_crop_width, a->y_crop_height);
}

int64_t aom_highbd_get_u_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height) {
  return highbd_get_sse(a->u_buffer + vstart * a->uv_stride + hstart,
                        a->uv_stride,
                        b->u_buffer + vstart * b->uv_stride + hstart,
                        b->uv_stride, width, height);
}

int64_t aom_highbd_get_u_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b) {
  assert(a->uv_crop_width == b->uv_crop_width);
  assert(a->uv_crop_height == b->uv_crop_height);

  return highbd_get_sse(a->u_buffer, a->uv_stride, b->u_buffer, b->uv_stride,
                        a->uv_crop_width, a->uv_crop_height);
}

int64_t aom_highbd_get_v_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height) {
  return highbd_get_sse(a->v_buffer + vstart * a->uv_stride + hstart,
                        a->uv_stride,
                        b->v_buffer + vstart * b->uv_stride + hstart,
                        b->uv_stride, width, height);
}

int64_t aom_highbd_get_v_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b) {
  assert(a->uv_crop_width == b->uv_crop_width);
  assert(a->uv_crop_height == b->uv_crop_height);

  return highbd_get_sse(a->v_buffer, a->uv_stride, b->v_buffer, b->uv_stride,
                        a->uv_crop_width, a->uv_crop_height);
}

int64_t aom_get_sse_plane(const YV12_BUFFER_CONFIG *a,
                          const YV12_BUFFER_CONFIG *b, int plane) {
  switch (plane) {
    case 0: return aom_highbd_get_y_sse(a, b);
    case 1: return aom_highbd_get_u_sse(a, b);
    case 2: return aom_highbd_get_v_sse(a, b);
    default: assert(plane >= 0 && plane <= 2); return 0;
  }
}

#if CONFIG_BRU
int64_t aom_get_sse_plane_available(const YV12_BUFFER_CONFIG *a,
                                    const YV12_BUFFER_CONFIG *b, int plane,
                                    const uint8_t *active_map,
                                    const int active_map_stride,
                                    const int unit_cols, const int unit_rows,
                                    const int unit_w, const int unit_h) {
  // act is in bru unit size
  // unit_w , unit_h are subsampled
  const uint8_t *act = active_map;
  int64_t total_err = 0;
  for (int r = 0; r < unit_rows; r++) {
    for (int c = 0; c < unit_cols; c++) {
      // need to check both active and support
      if (act[c]) {
        const int h_start = c * unit_w;
        const int v_start = r * unit_h;
        const int width = AOMMIN(
            unit_w, (plane > 0 ? a->uv_crop_width : a->y_crop_width) - h_start);
        const int height =
            AOMMIN(unit_h, (plane > 0 ? a->uv_crop_height : a->y_crop_height) -
                               v_start);
        switch (plane) {
          case 0:
            total_err += aom_highbd_get_y_sse_part(a, b, h_start, width,
                                                   v_start, height);
            break;
          case 1:
            total_err += aom_highbd_get_u_sse_part(a, b, h_start, width,
                                                   v_start, height);
            break;
          case 2:
            total_err += aom_highbd_get_v_sse_part(a, b, h_start, width,
                                                   v_start, height);
            break;
          default: assert(plane >= 0 && plane <= 2); break;
        }
      }
    }
    act += active_map_stride;
  }
  return total_err;
}
#endif  // CONFIG_BRU
void aom_calc_highbd_psnr(const YV12_BUFFER_CONFIG *a,
                          const YV12_BUFFER_CONFIG *b, PSNR_STATS *psnr,
                          uint32_t bit_depth, uint32_t in_bit_depth,
                          bool lossless) {
  assert(a->y_crop_width == b->y_crop_width);
  assert(a->y_crop_height == b->y_crop_height);
  assert(a->uv_crop_width == b->uv_crop_width);
  assert(a->uv_crop_height == b->uv_crop_height);
  const int widths[3] = { a->y_crop_width, a->uv_crop_width, a->uv_crop_width };
  const int heights[3] = { a->y_crop_height, a->uv_crop_height,
                           a->uv_crop_height };
  const int a_strides[3] = { a->y_stride, a->uv_stride, a->uv_stride };
  const int b_strides[3] = { b->y_stride, b->uv_stride, b->uv_stride };
  int i;
  uint64_t total_sse = 0;
  uint32_t total_samples = 0;
#if CONFIG_AV2CTC_PSNR_PEAK
  double peak = (double)(255 << (in_bit_depth - 8));
#else
  double peak = (double)((1 << in_bit_depth) - 1);
#endif  // CONFIG_AV2CTC_PSNR_PEAK
  const unsigned int input_shift = bit_depth - in_bit_depth;

  for (i = 0; i < 3; ++i) {
    const int w = widths[i];
    const int h = heights[i];
    const uint32_t samples = w * h;
    uint64_t sse;
    if (input_shift) {
      sse = highbd_get_sse_shift(a->buffers[i], a_strides[i], b->buffers[i],
                                 b_strides[i], w, h, input_shift, in_bit_depth);
    } else {
      sse = highbd_get_sse(a->buffers[i], a_strides[i], b->buffers[i],
                           b_strides[i], w, h);
    }

    psnr->sse[1 + i] = sse;
    psnr->samples[1 + i] = samples;
    psnr->psnr[1 + i] = aom_sse_to_psnr(samples, peak, (double)sse);

    total_sse += sse;
    total_samples += samples;
  }

  if (in_bit_depth >= bit_depth) {
    if (lossless && total_sse) {
      fprintf(stderr, "Non-zero SSE observed in lossless frame!\n");
      assert(0);
    }
  }

  psnr->sse[0] = total_sse;
  psnr->samples[0] = total_samples;
  psnr->psnr[0] =
      aom_sse_to_psnr((double)total_samples, peak, (double)total_sse);

  // Compute PSNR based on stream bit depth
  if (in_bit_depth < bit_depth) {
#if CONFIG_AV2CTC_PSNR_PEAK
    peak = (double)(255 << (bit_depth - 8));
#else
    peak = (double)((1 << bit_depth) - 1);
#endif  // CONFIG_AV2CTC_PSNR_PEAK
    total_sse = 0;
    total_samples = 0;
    for (i = 0; i < 3; ++i) {
      const int w = widths[i];
      const int h = heights[i];
      const uint32_t samples = w * h;
      uint64_t sse;
      sse = highbd_get_sse(a->buffers[i], a_strides[i], b->buffers[i],
                           b_strides[i], w, h);
      psnr->sse_hbd[1 + i] = sse;
      psnr->samples_hbd[1 + i] = samples;
      psnr->psnr_hbd[1 + i] = aom_sse_to_psnr(samples, peak, (double)sse);
      total_sse += sse;
      total_samples += samples;
    }

    if (lossless && total_sse) {
      fprintf(stderr, "Non-zero SSE observed in lossless frame!\n");
      assert(0);
    }

    psnr->sse_hbd[0] = total_sse;
    psnr->samples_hbd[0] = total_samples;
    psnr->psnr_hbd[0] =
        aom_sse_to_psnr((double)total_samples, peak, (double)total_sse);
  }
}
