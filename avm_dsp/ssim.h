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

#ifndef AVM_AVM_DSP_SSIM_H_
#define AVM_AVM_DSP_SSIM_H_

#define MAX_SSIM_DB 100.0;

#ifdef __cplusplus
extern "C" {
#endif

#include "config/avm_config.h"

#include "avm_scale/yv12config.h"

// metrics used for calculating ssim, ssim2, dssim, and ssimc
typedef struct {
  // source sum ( over 8x8 region )
  uint32_t sum_s;

  // reference sum (over 8x8 region )
  uint32_t sum_r;

  // source sum squared ( over 8x8 region )
  uint32_t sum_sq_s;

  // reference sum squared (over 8x8 region )
  uint32_t sum_sq_r;

  // sum of source times reference (over 8x8 region)
  uint32_t sum_sxr;

  // calculated ssim score between source and reference
  double ssim;
} Ssimv;

// metrics collected on a frame basis
typedef struct {
  // ssim consistency error metric ( see code for explanation )
  double ssimc;

  // standard ssim
  double ssim;

  // revised ssim ( see code for explanation)
  double ssim2;

  // ssim restated as an error metric like sse
  double dssim;

  // dssim converted to decibels
  double dssimd;

  // ssimc converted to decibels
  double ssimcd;
} Metrics;

double avm_calc_fastssim(const YV12_BUFFER_CONFIG *source,
                         const YV12_BUFFER_CONFIG *dest, double *ssim_y,
                         double *ssim_u, double *ssim_v, uint32_t bd,
                         uint32_t in_bd);

double avm_highbd_calc_ssim(const YV12_BUFFER_CONFIG *source,
                            const YV12_BUFFER_CONFIG *dest, double *weight,
                            uint32_t bd, uint32_t in_bd);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_SSIM_H_
