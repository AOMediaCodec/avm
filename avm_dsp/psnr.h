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

#ifndef AVM_AVM_DSP_PSNR_H_
#define AVM_AVM_DSP_PSNR_H_

#include "avm_scale/yv12config.h"

#define MAX_PSNR 100.0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  double psnr[4];           // total/y/u/v
  uint64_t sse[4];          // total/y/u/v
  uint32_t samples[4];      // total/y/u/v
  double psnr_hbd[4];       // total/y/u/v when input-bit-depth < bit-depth
  uint64_t sse_hbd[4];      // total/y/u/v when input-bit-depth < bit-depth
  uint32_t samples_hbd[4];  // total/y/u/v when input-bit-depth < bit-depth
} PSNR_STATS;

/*!\brief Converts SSE to PSNR
 *
 * Converts sum of squared errros (SSE) to peak signal-to-noise ratio (PNSR).
 *
 * \param[in]    samples       Number of samples
 * \param[in]    peak          Max sample value
 * \param[in]    sse           Sum of squared errors
 */
double avm_sse_to_psnr(double samples, double peak, double sse);
int64_t avm_get_sse_plane(const YV12_BUFFER_CONFIG *a,
                          const YV12_BUFFER_CONFIG *b, int plane);
int64_t avm_get_sse_plane_available(const YV12_BUFFER_CONFIG *a,
                                    const YV12_BUFFER_CONFIG *b, int plane,
                                    const uint8_t *active_map,
                                    const int active_map_stride,
                                    const int unit_cols, const int unit_rows,
                                    const int unit_w, const int unit_h);
uint64_t avm_highbd_get_y_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height);
uint64_t avm_highbd_get_u_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height);
uint64_t avm_highbd_get_v_var(const YV12_BUFFER_CONFIG *a, int hstart,
                              int width, int vstart, int height);
int64_t avm_highbd_get_y_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height);
int64_t avm_highbd_get_y_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b);
// avm_highbd_get_y_sse() is the sum of these two over the whole plane: the
// SSE of the right and bottom strips that are not covered by 16x16 blocks, and
// the SSE of the rows [row16_start, row16_end) of 16x16 blocks. Both are
// independent of each other, so the second can be split by rows. The edges
// are the sum of the right strip, which is split by rows, and the bottom strip
// to the left of it, which is split by columns, [0, width - width % 16).
int64_t avm_highbd_get_sse_right_edge(const uint16_t *a, int a_stride,
                                      const uint16_t *b, int b_stride,
                                      int width, int row_start, int row_end);
int64_t avm_highbd_get_sse_bottom_edge(const uint16_t *a, int a_stride,
                                       const uint16_t *b, int b_stride,
                                       int height, int col_start, int col_end);
int64_t avm_highbd_get_sse_edges(const uint16_t *a, int a_stride,
                                 const uint16_t *b, int b_stride, int width,
                                 int height);
int64_t avm_highbd_get_sse_block_rows(const uint16_t *a, int a_stride,
                                      const uint16_t *b, int b_stride,
                                      int width, int row16_start,
                                      int row16_end);
int64_t avm_highbd_get_u_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height);
int64_t avm_highbd_get_u_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b);
int64_t avm_highbd_get_v_sse_part(const YV12_BUFFER_CONFIG *a,
                                  const YV12_BUFFER_CONFIG *b, int hstart,
                                  int width, int vstart, int height);
int64_t avm_highbd_get_v_sse(const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b);
void avm_calc_highbd_psnr(const YV12_BUFFER_CONFIG *a,
                          const YV12_BUFFER_CONFIG *b, PSNR_STATS *psnr,
                          unsigned int bit_depth, unsigned int in_bit_depth,
                          bool lossless);

double avm_psnrhvs(const YV12_BUFFER_CONFIG *source,
                   const YV12_BUFFER_CONFIG *dest, double *phvs_y,
                   double *phvs_u, double *phvs_v, uint32_t bd, uint32_t in_bd);
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AVM_DSP_PSNR_H_
