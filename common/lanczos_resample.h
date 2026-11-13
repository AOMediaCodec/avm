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

#ifndef AVM_COMMON_LANCZOS_RESAMPLE_H_
#define AVM_COMMON_LANCZOS_RESAMPLE_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "config/avm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_RATIONAL_FACTOR 16
#define MAX_FILTER_LEN 320

void av2_derive_scale_factor(int width_scaled, int width, int *p, int *q);

void av2_resample_plane_2d_lanczos(const uint16_t *const input, int height,
                                   int width, int in_stride, uint16_t *output,
                                   int height2, int width2, int out_stride,
                                   int subx, int suby, int bd, int denom,
                                   int num, int lanczos_a_hor,
                                   int lanczos_a_ver);
// Non-normative 2D rational Lanczos resampler accepting independent horizontal
// (num_hor / denom_hor) and vertical (num_ver / denom_ver) rational scale
// factors when integer-rounded dimensions have slightly anisotropic ratios.
void av2_resample_plane_2d_lanczos_hv(const uint16_t *const input, int height,
                                      int width, int in_stride,
                                      uint16_t *output, int height2, int width2,
                                      int out_stride, int subx, int suby,
                                      int bd, int denom_hor, int num_hor,
                                      int denom_ver, int num_ver,
                                      int lanczos_a_hor, int lanczos_a_ver);
void av2_resample_plane_2d_lanczos_hv_c(const uint16_t *const input, int height,
                                        int width, int in_stride,
                                        uint16_t *output, int height2,
                                        int width2, int out_stride, int subx,
                                        int suby, int bd, int denom_hor,
                                        int num_hor, int denom_ver, int num_ver,
                                        int lanczos_a_hor, int lanczos_a_ver);
void av2_resample_plane_2d_8b_lanczos(const uint8_t *const input, int height,
                                      int width, int in_stride, uint8_t *output,
                                      int height2, int width2, int out_stride,
                                      int subx, int suby, int bd, int denom,
                                      int num, int lanczos_a_hor,
                                      int lanczos_a_ver);
void av2_resample_plane_2d_8b_lanczos_c(const uint8_t *const input, int height,
                                        int width, int in_stride,
                                        uint8_t *output, int height2,
                                        int width2, int out_stride, int subx,
                                        int suby, int bd, int denom, int num,
                                        int lanczos_a_hor, int lanczos_a_ver);

// Note: check window() function implementation for values of any
// other params used by these windowing functions.
typedef enum {
  WIN_LANCZOS,      // Sinc window (i.e. Lanczos)
  WIN_LANCZOS_DIL,  // Dilated Lanczos window
  WIN_GAUSSIAN,     // Gaussian window
  WIN_GENGAUSSIAN,  // Gaussian window
  WIN_COSINE,       // Cosine window
  WIN_HAMMING,      // Hamming Window
  WIN_BLACKMAN,     // Blackman window
  WIN_KAISER,       // Kaiser window
} WIN_TYPE;

typedef enum { EXT_REPEAT, EXT_SYMMETRIC, EXT_REFLECT, EXT_GRADIENT } EXT_TYPE;

typedef struct {
  int p;
  int q;
  int length;
  EXT_TYPE ext_type;
  WIN_TYPE win_type;
  int filter_bits;
  int start;
  int steps[MAX_RATIONAL_FACTOR];
  int16_t filter[MAX_RATIONAL_FACTOR][MAX_FILTER_LEN];
  double phases[MAX_RATIONAL_FACTOR];
} RationalResampleFilter;

typedef struct {
  int bits;
  int issigned;
} ClipProfile;

double get_centered_x0(int p, int q);

// x0 is assumed to be in (-1, 1)
double get_inverse_x0_numeric(int p, int q, double x0);

// In the functions below using x0 as an argument,
// x0 is assumed to be in (-1, 1);
//                        or 99 (ascii value of 'c') meaning centered;
//                        or 100 (ascii value of 'd') meaning co-sited chroma
//                        if the chroma plane is subsampled.
double get_inverse_x0(int p, int q, double x0, int subsampled);

// Note: 16-bit resampling functions accumulate products in 32-bit signed
// integers and require filter_bits <= 14 and 0 <= downshift <= 30.
int get_resample_filter(int p, int q, int a, double x0, EXT_TYPE ext_type,
                        WIN_TYPE win_type, int subsampled, int bits,
                        RationalResampleFilter *rf);
int get_resample_filter_inv(int p, int q, int a, double x0, EXT_TYPE ext_type,
                            WIN_TYPE win_type, int subsampled, int bits,
                            RationalResampleFilter *rf);

// whether the resampler filter is a no-op
int is_resampler_noop(RationalResampleFilter *rf);

// 16-bit versions of high-level resampling functions

// Assume no extension of the input x buffer. Requires rf->filter_bits <= 14
// and 0 <= downshift <= 30.
void resample_1d(const int16_t *x, int inlen, RationalResampleFilter *rf,
                 int downshift, ClipProfile *clip, int16_t *y, int outlen);
void resample_1d_c(const int16_t *x, int inlen, RationalResampleFilter *rf,
                   int downshift, ClipProfile *clip, int16_t *y, int outlen);
#if HAVE_SSE2
void av2_resample_1d_core_sse2(const int16_t *x, int inlen,
                               RationalResampleFilter *rf, int downshift,
                               ClipProfile *clip, int16_t *y, int outlen);
void av2_resample_vert_rows_16b_sse2(const int16_t *src, int width,
                                     int inheight, int instride,
                                     RationalResampleFilter *rfv, int downshift,
                                     ClipProfile *clip, int16_t *dst,
                                     int outheight, int outstride);
void av2_resample_vert_rows_out8b_sse2(const int16_t *src, int width,
                                       int inheight, int instride,
                                       RationalResampleFilter *rfv,
                                       int downshift, ClipProfile *clip,
                                       uint8_t *dst, int outheight,
                                       int outstride);
#endif  // HAVE_SSE2

void av2_resample_2d(const int16_t *x, int inwidth, int inheight, int instride,
                     RationalResampleFilter *rfh, RationalResampleFilter *rfv,
                     int int_extra_bits, ClipProfile *clip, int16_t *y,
                     int outwidth, int outheight, int outstride);
void av2_resample_2d_c(const int16_t *x, int inwidth, int inheight,
                       int instride, RationalResampleFilter *rfh,
                       RationalResampleFilter *rfv, int int_extra_bits,
                       ClipProfile *clip, int16_t *y, int outwidth,
                       int outheight, int outstride);

void resample_horz(const int16_t *x, int inwidth, int inheight, int instride,
                   RationalResampleFilter *rfh, ClipProfile *clip, int16_t *y,
                   int outwidth, int outstride);

void resample_vert(const int16_t *x, int inwidth, int inheight, int instride,
                   RationalResampleFilter *rfv, ClipProfile *clip, int16_t *y,
                   int outheight, int outstride);

// 8-bit versions of high-level resampling functions

// Assume no extension of the input x buffer
void resample_1d_8b(const uint8_t *x, int inlen, RationalResampleFilter *rf,
                    int downshift, ClipProfile *clip, uint8_t *y, int outlen);

void av2_resample_2d_8b(const uint8_t *x, int inwidth, int inheight,
                        int instride, RationalResampleFilter *rfh,
                        RationalResampleFilter *rfv, int int_extra_bits,
                        ClipProfile *clip, uint8_t *y, int outwidth,
                        int outheight, int outstride);
void av2_resample_2d_8b_c(const uint8_t *x, int inwidth, int inheight,
                          int instride, RationalResampleFilter *rfh,
                          RationalResampleFilter *rfv, int int_extra_bits,
                          ClipProfile *clip, uint8_t *y, int outwidth,
                          int outheight, int outstride);

void resample_horz_8b(const uint8_t *x, int inwidth, int inheight, int instride,
                      RationalResampleFilter *rfh, ClipProfile *clip,
                      uint8_t *y, int outwidth, int outstride);

void resample_vert_8b(const uint8_t *x, int inwidth, int inheight, int instride,
                      RationalResampleFilter *rfv, ClipProfile *clip,
                      uint8_t *y, int outheight, int outstride);

void show_resample_filter(RationalResampleFilter *rf);

int get_resampled_output_length(int inlen, int p, int q, int force_even);
const char *ext2str(EXT_TYPE ext_type);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_LANCZOS_RESAMPLE_H_
