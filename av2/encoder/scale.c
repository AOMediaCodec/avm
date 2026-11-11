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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config/avm_config.h"

#include "av2/common/resize.h"
#include "av2/encoder/encoder_alloc.h"
#include "av2/encoder/random.h"
#include "av2/encoder/ratectrl.h"
#include "av2/encoder/scale.h"
#include "avm_dsp/psnr.h"
#include "avm_ports/system_state.h"
#if CONFIG_LANCZOS_RESAMPLE
#include "common/lanczos_resample.h"
#endif  // CONFIG_LANCZOS_RESAMPLE

// Thresholds and scaling parameters for non-normative encoder-side
// RESIZE_DYNAMIC (--resize-mode=3) and RESIZE_DYNAMIC_SUBGOP (--resize-mode=7)
// denominator selection:
// - 75th-percentile sharpness-weighted Luma resampling MSE + weighted Chroma
//   resampling MSE normalized by Qstep^2(d) (scaled above 1080p by pel_ratio),
// - resampling distortion relative to lookahead motion-compensated (MC)
//   temporal coding energy with flat/shadow sensor-noise damping (plus an
//   above-1080p background-safe full-frame MC gate) on inter frames/subgops,
// - 75th-percentile sharpness-weighted (MSE / 16x16 block AC energy) on
//   key/intra-only frames,
// - intra-GOP Schmitt-trigger subgop scale hysteresis bands, and
// - minimum baseline full-resolution qindex gates (in 8-bit scale).
#define RESIZE_DYNAMIC_THRESH_KF 0.00010
#define RESIZE_DYNAMIC_THRESH 0.00052
#define RESIZE_DYNAMIC_SUBGOP_THRESH 0.00033
#define RESIZE_DYNAMIC_AC_THRESH_KF 0.00065
#define RESIZE_DYNAMIC_MC_THRESH 0.0180
#define RESIZE_DYNAMIC_SUBGOP_MC_THRESH 0.0100
#define RESIZE_DYNAMIC_MC_RATIO_MAX 1.35
#define RESIZE_DYNAMIC_MC_DEADZONE 7.0e-6
#define RESIZE_DYNAMIC_MC_GRAIN_WEIGHT 2.0
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0 0.60
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_SPAN 0.15
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX 0.30
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_SPAN 0.15
#define RESIZE_DYNAMIC_REF_PELS (1920.0 * 1080.0)
#define RESIZE_DYNAMIC_MAX_PEL_RATIO 4.0
#define RESIZE_DYNAMIC_HIGHRES_PEL_RATIO 2.0
#define RESIZE_DYNAMIC_SUBGOP_HYST_STEP 0.78
#define RESIZE_DYNAMIC_SUBGOP_HYST_KEEP 1.12
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX 0.088
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF 0.125
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI 0.021
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN 0.40
#define RESIZE_DYNAMIC_SUBGOP_EXTREME_ACR_HYST_STEP 0.50
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_STEP 0.62
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_KEEP 1.43
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_SLOPE 0.008
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_MOD 1.24
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_HIGH 1.46
#define RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO 0.080
#define RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO 0.028
#define RESIZE_DYNAMIC_CHROMA_WEIGHT 4.0
#define RESIZE_DYNAMIC_MIN_BLOCK_VAR 64.0
#define RESIZE_DYNAMIC_BLOCK_PERCENTILE 0.75
#define RESIZE_DYNAMIC_MINQP_KF 205
#define RESIZE_DYNAMIC_MINQP 180
#define RESIZE_DYNAMIC_SUBGOP_MINQP 180

// This function is used to compute the resolution for low delay mode
static uint8_t get_resolution_ratio_pattern1(const int display_order_hint) {
  uint8_t new_denom = 8;
  const uint8_t denom_indices8[8] = { 8, 10, 12, 14, 16, 15, 13, 9 };
  const uint8_t denom_indices16[16] = { 8,  9,  10, 11, 12, 13, 14, 15,
                                        16, 15, 14, 13, 12, 11, 10, 9 };
  int chunk_size = display_order_hint % 48;
  if (chunk_size < 8) {
    new_denom = denom_indices8[chunk_size];
  } else if (chunk_size < 16) {
    new_denom = 8;
  } else if (chunk_size < 32) {
    new_denom = denom_indices16[chunk_size - 16];
  } else {
    new_denom = 8;
  }
  return new_denom;
}

// This function is used to compute the resolution for RA mode
static uint8_t get_resolution_ratio_pattern2(const int display_order_hint) {
  uint8_t new_denom = 8;
  int chunk_size = display_order_hint % 65;
  if (chunk_size < 17) {
    new_denom = 8;
  } else if (chunk_size < 33) {
    new_denom = 16;
  } else if (chunk_size < 49) {
    new_denom = 8;
  } else {
    new_denom = 12;
  }
  return new_denom;
}

static uint8_t get_resolution_bridge_frame_pattern(const int frame_count) {
  uint8_t new_denom = 8;
  if (frame_count == 0) {
    new_denom = 8;
  } else if (frame_count == 1) {
    new_denom = 16;
  } else {
    new_denom = 32;
  }
  return new_denom;
}

static int dimension_is_ok(int orig_dim, int resized_dim, int denom) {
  return (resized_dim * SCALE_NUMERATOR >= orig_dim * denom / 2);
}

static int cmp_double(const void *a, const void *b) {
  const double da = *(const double *)a;
  const double db = *(const double *)b;
  return (da > db) - (da < db);
}

// Computes the 75th-percentile sharpness-weighted round-trip Luma resampling
// MSE and 75th-percentile sharpness-weighted (MSE / 16x16 block AC energy)
// over non-flat 16x16 blocks whose block variance exceeds
// RESIZE_DYNAMIC_MIN_BLOCK_VAR * 2^(2 * (bit_depth - 8)). Each non-flat
// block's resampling distortion is weighted by a dimensionless,
// contrast-invariant spatial sharpness factor combining edge kurtosis
// (peak-to-mean 1st-order gradient energy) and high-frequency Laplacian-to-
// gradient energy ratio. Falls back to the average over all 16x16 blocks if no
// block exceeds the minimum variance.
static void compute_nonflat_resample_mse_and_ac(const YV12_BUFFER_CONFIG *src,
                                                const uint16_t *upscaled_y,
                                                int orig_w, int orig_h,
                                                avm_bit_depth_t bit_depth,
                                                double *out_mse,
                                                double *out_ac_ratio) {
  const int bsize = 16;
  const int num_pels = bsize * bsize;
  const double inner_pels = (double)((bsize - 2) * (bsize - 2));
  const double bit_depth_scale_sq =
      (double)(1 << (2 * ((int)bit_depth - AVM_BITS_8)));
  const double min_block_var =
      RESIZE_DYNAMIC_MIN_BLOCK_VAR * bit_depth_scale_sq;
  const uint16_t *const src_y = src->y_buffer;
  const int src_stride = src->y_stride;

  const int max_blocks = (orig_h / bsize) * (orig_w / bsize);
  double *sharp_mse_arr = NULL;
  double *sharp_ac_ratio_arr = NULL;
  if (max_blocks > 0) {
    sharp_mse_arr =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*sharp_mse_arr));
    sharp_ac_ratio_arr =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*sharp_ac_ratio_arr));
  }

  double all_mse_sum = 0.0;
  double all_ac_sum = 0.0;
  int all_count = 0;
  double nz_mse_sum = 0.0;
  double nz_ac_sum = 0.0;
  int nz_count = 0;

  for (int r = 0; r + bsize <= orig_h; r += bsize) {
    for (int c = 0; c + bsize <= orig_w; c += bsize) {
      uint64_t sum = 0;
      uint64_t sum_sq = 0;
      uint64_t sse = 0;
      for (int br = 0; br < bsize; ++br) {
        const uint16_t *const s_row = src_y + (r + br) * src_stride + c;
        const uint16_t *const u_row = upscaled_y + (r + br) * orig_w + c;
        for (int bc = 0; bc < bsize; ++bc) {
          const uint64_t v = s_row[bc];
          const int64_t diff = (int64_t)v - (int64_t)u_row[bc];
          sum += v;
          sum_sq += v * v;
          sse += (uint64_t)(diff * diff);
        }
      }
      const double block_ac =
          ((double)sum_sq - ((double)sum * (double)sum) / (double)num_pels) /
          (double)num_pels;
      const double block_mse = (double)sse / (double)num_pels;
      all_mse_sum += block_mse;
      all_ac_sum += block_ac;
      ++all_count;
      if (block_ac > min_block_var) {
        double ge = 0.0;
        double le = 0.0;
        double max_g2 = 0.0;
        for (int br = 1; br < bsize - 1; ++br) {
          const uint16_t *const s_prev = src_y + (r + br - 1) * src_stride + c;
          const uint16_t *const s_curr = src_y + (r + br) * src_stride + c;
          const uint16_t *const s_next = src_y + (r + br + 1) * src_stride + c;
          for (int bc = 1; bc < bsize - 1; ++bc) {
            const double v = (double)s_curr[bc];
            const double vl = (double)s_curr[bc - 1];
            const double vr = (double)s_curr[bc + 1];
            const double vu = (double)s_prev[bc];
            const double vd = (double)s_next[bc];
            const double gx = vr - vl;
            const double gy = vd - vu;
            const double lx = 2.0 * v - vl - vr;
            const double ly = 2.0 * v - vu - vd;
            const double g2 = gx * gx + gy * gy;
            if (g2 > max_g2) max_g2 = g2;
            ge += g2;
            le += lx * lx + ly * ly;
          }
        }
        const double mean_g2 = AVMMAX(ge / inner_pels, bit_depth_scale_sq);
        const double mean_l2 = le / inner_pels;
        const double kurt = max_g2 / mean_g2;
        const double lap_grad = mean_l2 / mean_g2;
        const double block_sharpness = (kurt / 8.0) * (1.0 + lap_grad);
        const double sharp_mse = block_mse * block_sharpness;
        const double sharp_ac_ratio = (block_mse / block_ac) * block_sharpness;

        nz_mse_sum += sharp_mse;
        nz_ac_sum += sharp_ac_ratio;
        if (sharp_mse_arr != NULL && sharp_ac_ratio_arr != NULL) {
          sharp_mse_arr[nz_count] = sharp_mse;
          sharp_ac_ratio_arr[nz_count] = sharp_ac_ratio;
        }
        ++nz_count;
      }
    }
  }

  if (nz_count > 0) {
    if (sharp_mse_arr != NULL && sharp_ac_ratio_arr != NULL) {
      qsort(sharp_mse_arr, (size_t)nz_count, sizeof(*sharp_mse_arr),
            cmp_double);
      qsort(sharp_ac_ratio_arr, (size_t)nz_count, sizeof(*sharp_ac_ratio_arr),
            cmp_double);
      const int pct_idx =
          (int)(RESIZE_DYNAMIC_BLOCK_PERCENTILE * (double)(nz_count - 1));
      *out_mse = sharp_mse_arr[pct_idx];
      *out_ac_ratio = sharp_ac_ratio_arr[pct_idx];
    } else {
      *out_mse = nz_mse_sum / (double)nz_count;
      *out_ac_ratio = nz_ac_sum / (double)nz_count;
    }
  } else if (all_count > 0) {
    *out_mse = all_mse_sum / (double)all_count;
    *out_ac_ratio = all_mse_sum / AVMMAX(all_ac_sum, 1.0);
  } else {
    *out_mse = 0.0;
    *out_ac_ratio = 0.0;
  }

  avm_free(sharp_mse_arr);
  avm_free(sharp_ac_ratio_arr);
}

// Performs an 8/d downscale followed by a d/8 upscale on a single plane using
// Lanczos-5 resampling when enabled (falling back to av2_highbd_resize_plane
// otherwise), matching encoder source downscaling and decoder output upscaling.
static void resample_plane_roundtrip(const uint16_t *src, int orig_h,
                                     int orig_w, int src_stride,
                                     uint16_t *scratch_dn, int scaled_h,
                                     int scaled_w, uint16_t *dst_up, int subx,
                                     int suby, int d, avm_bit_depth_t bit_depth,
                                     int is_uv) {
#if CONFIG_LANCZOS_RESAMPLE
  const int a_hor = is_uv ? LANCZOS_A_ENC_NONNORMATIVE_HOR_C
                          : LANCZOS_A_ENC_NONNORMATIVE_HOR_Y;
  const int a_ver = is_uv ? LANCZOS_A_ENC_NONNORMATIVE_VER_C
                          : LANCZOS_A_ENC_NONNORMATIVE_VER_Y;
  av2_resample_plane_2d_lanczos(
      src, orig_h, orig_w, src_stride, scratch_dn, scaled_h, scaled_w, scaled_w,
      subx, suby, (int)bit_depth, d, SCALE_NUMERATOR, a_hor, a_ver);
  av2_resample_plane_2d_lanczos(
      scratch_dn, scaled_h, scaled_w, scaled_w, dst_up, orig_h, orig_w, orig_w,
      subx, suby, (int)bit_depth, SCALE_NUMERATOR, d, a_hor, a_ver);
#else
  (void)subx;
  (void)suby;
  (void)d;
  (void)is_uv;
  av2_highbd_resize_plane(src, orig_h, orig_w, src_stride, scratch_dn, scaled_h,
                          scaled_w, scaled_w, (int)bit_depth);
  av2_highbd_resize_plane(scratch_dn, scaled_h, scaled_w, scaled_w, dst_up,
                          orig_h, orig_w, orig_w, (int)bit_depth);
#endif  // CONFIG_LANCZOS_RESAMPLE
}

// Computes the average round-trip resampling MSE across U and V chroma planes
// for candidate scaled Luma dimensions (scaled_w, scaled_h) at denominator d,
// reusing scratch_dn and scratch_up (each sized at least orig_w * orig_h
// samples). Returns 0.0 for monochrome inputs or if chroma planes are
// unavailable.
static double compute_resample_chroma_mse(const YV12_BUFFER_CONFIG *src,
                                          int orig_w, int orig_h, int scaled_w,
                                          int scaled_h, int d,
                                          avm_bit_depth_t bit_depth,
                                          uint16_t *scratch_dn,
                                          uint16_t *scratch_up) {
  if (src->monochrome || src->u_buffer == NULL || src->v_buffer == NULL ||
      src->uv_crop_width <= 0 || src->uv_crop_height <= 0 ||
      src->y_crop_width != orig_w || src->y_crop_height != orig_h) {
    return 0.0;
  }
  const int ss_x = src->subsampling_x;
  const int ss_y = src->subsampling_y;
  const int uv_w = src->uv_crop_width;
  const int uv_h = src->uv_crop_height;
  const int scaled_uv_w = (scaled_w + ss_x) >> ss_x;
  const int scaled_uv_h = (scaled_h + ss_y) >> ss_y;
  if (uv_w > orig_w || uv_h > orig_h || scaled_uv_w <= 0 || scaled_uv_h <= 0 ||
      scaled_uv_w > orig_w || scaled_uv_h > orig_h) {
    return 0.0;
  }

  uint64_t uv_sse = 0;
  const uint16_t *const planes[2] = { src->u_buffer, src->v_buffer };
  for (int p = 0; p < 2; ++p) {
    const uint16_t *const plane_src = planes[p];
    resample_plane_roundtrip(plane_src, uv_h, uv_w, src->uv_stride, scratch_dn,
                             scaled_uv_h, scaled_uv_w, scratch_up, ss_x, ss_y,
                             d, bit_depth, /*is_uv=*/1);
    for (int r = 0; r < uv_h; ++r) {
      const uint16_t *const s_row = plane_src + r * src->uv_stride;
      const uint16_t *const u_row = scratch_up + r * uv_w;
      for (int c = 0; c < uv_w; ++c) {
        const int64_t diff = (int64_t)s_row[c] - (int64_t)u_row[c];
        uv_sse += (uint64_t)(diff * diff);
      }
    }
  }
  return (double)uv_sse / (2.0 * (double)uv_w * (double)uv_h);
}

// Estimates motion-compensated 16x16 Luma residual statistics between two
// consecutive unscaled source frames (cur vs. prv) using a fast two-stage
// hierarchical integer search (+-24 step-4 coarse search on 2x2-subsampled
// blocks followed by +-3 step-1 refinement and zero-MV comparison).
// Returns 1 on success and populates:
// - out_mc_p50: median 16x16 MC MSE across sampled blocks,
// - out_mc_nz:  mean 16x16 MC MSE over non-flat blocks,
// - out_mc_acr: ratio of non-flat MC MSE sum to non-flat AC variance sum.
static int compute_pair_mc_stats(const YV12_BUFFER_CONFIG *cur,
                                 const YV12_BUFFER_CONFIG *prv, int orig_w,
                                 int orig_h, avm_bit_depth_t bit_depth,
                                 double *out_mc_p50, double *out_mc_nz,
                                 double *out_mc_acr) {
  if (cur == NULL || prv == NULL || cur->y_buffer == NULL ||
      prv->y_buffer == NULL || cur->y_crop_width != orig_w ||
      cur->y_crop_height != orig_h || prv->y_crop_width != orig_w ||
      prv->y_crop_height != orig_h || orig_w < 16 || orig_h < 16) {
    return 0;
  }

  const int bsize = 16;
  const int sr = 24;
  const int step = (orig_w >= 512) ? 32 : 16;
  const double bds2 = (double)(1 << (2 * ((int)bit_depth - AVM_BITS_8)));
  const double min_block_var = RESIZE_DYNAMIC_MIN_BLOCK_VAR * bds2;
  const uint16_t *const cur_y = cur->y_buffer;
  const uint16_t *const prv_y = prv->y_buffer;
  const int cur_stride = cur->y_stride;
  const int prv_stride = prv->y_stride;

  const int max_blocks =
      ((orig_h - bsize) / step + 1) * ((orig_w - bsize) / step + 1);
  if (max_blocks <= 0) return 0;
  double *const mc_arr =
      (double *)avm_malloc((size_t)max_blocks * sizeof(*mc_arr));
  if (mc_arr == NULL) return 0;

  int count = 0;
  int nz_count = 0;
  double sum_all = 0.0;
  double sum_nz_mc = 0.0;
  double sum_nz_ac = 0.0;

  for (int r = 0; r + bsize <= orig_h; r += step) {
    for (int c = 0; c + bsize <= orig_w; c += step) {
      uint64_t sum = 0;
      uint64_t sum_sq = 0;
      uint64_t zero_sse = 0;
      for (int y = 0; y < bsize; ++y) {
        const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
        const uint16_t *const pr = prv_y + (r + y) * prv_stride + c;
        for (int x = 0; x < bsize; ++x) {
          const uint64_t v = cr[x];
          const int64_t df = (int64_t)v - (int64_t)pr[x];
          sum += v;
          sum_sq += v * v;
          zero_sse += (uint64_t)(df * df);
        }
      }
      const double block_ac =
          ((double)sum_sq - ((double)sum * (double)sum) / 256.0) / 256.0;

      int best_dy = 0;
      int best_dx = 0;
      uint64_t best_coarse = UINT64_MAX;
      for (int dy = -sr; dy <= sr; dy += 4) {
        if (r + dy < 0 || r + dy + bsize > orig_h) continue;
        for (int dx = -sr; dx <= sr; dx += 4) {
          if (c + dx < 0 || c + dx + bsize > orig_w) continue;
          uint64_t cs = 0;
          for (int y = 0; y < bsize; y += 2) {
            const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
            const uint16_t *const pr =
                prv_y + (r + dy + y) * prv_stride + (c + dx);
            for (int x = 0; x < bsize; x += 2) {
              const int64_t df = (int64_t)cr[x] - (int64_t)pr[x];
              cs += (uint64_t)(df * df);
            }
          }
          if (cs < best_coarse || (cs == best_coarse && dy == 0 && dx == 0)) {
            best_coarse = cs;
            best_dy = dy;
            best_dx = dx;
          }
        }
      }

      const int center_dy = best_dy;
      const int center_dx = best_dx;
      for (int dy = center_dy - 3; dy <= center_dy + 3; ++dy) {
        if (r + dy < 0 || r + dy + bsize > orig_h) continue;
        for (int dx = center_dx - 3; dx <= center_dx + 3; ++dx) {
          if (c + dx < 0 || c + dx + bsize > orig_w) continue;
          uint64_t cs = 0;
          for (int y = 0; y < bsize; y += 2) {
            const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
            const uint16_t *const pr =
                prv_y + (r + dy + y) * prv_stride + (c + dx);
            for (int x = 0; x < bsize; x += 2) {
              const int64_t df = (int64_t)cr[x] - (int64_t)pr[x];
              cs += (uint64_t)(df * df);
            }
          }
          if (cs < best_coarse) {
            best_coarse = cs;
            best_dy = dy;
            best_dx = dx;
          }
        }
      }

      uint64_t full_sse = 0;
      for (int y = 0; y < bsize; ++y) {
        const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
        const uint16_t *const pr =
            prv_y + (r + best_dy + y) * prv_stride + (c + best_dx);
        for (int x = 0; x < bsize; ++x) {
          const int64_t df = (int64_t)cr[x] - (int64_t)pr[x];
          full_sse += (uint64_t)(df * df);
        }
      }
      if (zero_sse < full_sse) full_sse = zero_sse;
      const double mc_mse = (double)full_sse / 256.0;
      mc_arr[count++] = mc_mse;
      sum_all += mc_mse;
      if (block_ac > min_block_var) {
        sum_nz_mc += mc_mse;
        sum_nz_ac += block_ac;
        ++nz_count;
      }
    }
  }

  if (count <= 0) {
    avm_free(mc_arr);
    return 0;
  }
  qsort(mc_arr, (size_t)count, sizeof(*mc_arr), cmp_double);
  const int p50_idx = (int)(0.50 * (double)(count - 1));
  const int p60_idx = (int)(0.60 * (double)(count - 1));
  *out_mc_p50 = mc_arr[p50_idx];
  const double mc_p60 = mc_arr[p60_idx];
  *out_mc_nz =
      (nz_count > 0) ? (sum_nz_mc / (double)nz_count) : (sum_all / count);
  *out_mc_acr = (nz_count > 0 && sum_nz_ac > bds2) ? (sum_nz_mc / sum_nz_ac)
                                                   : (mc_p60 / min_block_var);
  avm_free(mc_arr);
  return 1;
}

// Returns the source pixel-count ratio relative to 1080p, clamped to
// [1.0, RESIZE_DYNAMIC_MAX_PEL_RATIO]. Resolutions at or below 1920x1080
// return 1.0 identically, preserving 1080p-calibrated thresholds.
static double get_resize_pel_ratio(int orig_w, int orig_h) {
  const double ratio =
      ((double)orig_w * (double)orig_h) / RESIZE_DYNAMIC_REF_PELS;
  if (ratio <= 1.0) return 1.0;
  if (ratio >= RESIZE_DYNAMIC_MAX_PEL_RATIO) {
    return RESIZE_DYNAMIC_MAX_PEL_RATIO;
  }
  return ratio;
}

// Combines motion-compensated block residual statistics into an effective
// temporal coding energy denominator for gating dynamic downscaling on inter
// frames and subgops. Scales MC energy by the temporal unpredictability ratio
// mc_acr (suppressing well-predicted panning/static backgrounds with localized
// moving objects), adds a spatial-uniformity grain term (high median-to-mean
// MC residual ratio), and includes a quantization deadzone floor attenuated by
// pel_ratio^2 above 1080p so motionless crisp high-resolution scenes cannot
// downscale off an inflated quantization floor. When the median-to-nonflat-mean
// ratio uni exceeds RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0 while mc_acr is below
// RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX (nearly static high-AC scenes sitting
// on uniform flat or dark sensor noise rather than genuine grainy motion or
// high-entropy temporal change), smoothly ramps the grain term down toward
// mc_acr so sensor noise in flat/shadow blocks does not masquerade as
// incompressible temporal coding energy.
static double compute_mc_effective_energy(double mc_p50, double mc_nz,
                                          double mc_acr, double qstep_ref_sq,
                                          double pel_ratio,
                                          avm_bit_depth_t bit_depth) {
  const double bds2 = (double)(1 << (2 * ((int)bit_depth - AVM_BITS_8)));
  const double uni = mc_p50 / AVMMAX(mc_nz, bds2);
  const double uni_clamped = AVMMIN(uni, 1.0);
  double grain_scale = 1.0;
  if (uni_clamped > RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0 &&
      mc_acr < RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX) {
    const double w_uni =
        AVMMIN((uni_clamped - RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0) /
                   RESIZE_DYNAMIC_MC_FLAT_NOISE_SPAN,
               1.0);
    const double w_acr =
        AVMMIN((RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX - mc_acr) /
                   RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_SPAN,
               1.0);
    grain_scale = 1.0 - (w_uni * w_acr) * (1.0 - mc_acr);
  }
  const double grain_term = RESIZE_DYNAMIC_MC_GRAIN_WEIGHT * mc_p50 *
                            uni_clamped * uni_clamped * uni_clamped *
                            grain_scale;
  const double dz_weight =
      (pel_ratio > 1.0) ? (RESIZE_DYNAMIC_MC_DEADZONE / (pel_ratio * pel_ratio))
                        : RESIZE_DYNAMIC_MC_DEADZONE;
  const double mc_eff =
      (mc_p50 + mc_nz) * mc_acr + grain_term + dz_weight * qstep_ref_sq;
  return AVMMAX(mc_eff, 1e-6);
}

// Background-safe full-frame temporal coding energy for gating high-resolution
// (pel_ratio > 1.0) dynamic downscaling: combines the frame-wide median block
// MC MSE mc_p50 (which stays small on stationary crisp backgrounds coded via
// zero-bit SKIP blocks and grows on global camera pans/zooms) with active-block
// MC MSE weighted by temporal unpredictability squared
// (RESIZE_DYNAMIC_MC_GRAIN_WEIGHT * mc_nz * mc_acr^2). Prevents localized
// predictable motion (mc_nz >> mc_p50 with small mc_acr, e.g. ocean ripples or
// an isolated foreground actor against a static 4K scene) from spoofing
// full-frame motion when mc_thresh opens up at coarse QPs, while still passing
// both full-frame pans and high-unpredictability fast action.
static double compute_mc_bg_energy(double mc_p50, double mc_nz, double mc_acr) {
  const double mc_bg =
      mc_p50 + RESIZE_DYNAMIC_MC_GRAIN_WEIGHT * mc_nz * mc_acr * mc_acr;
  return AVMMAX(mc_bg, 1e-6);
}

// Dynamic resize scale decision for RESIZE_DYNAMIC (and keyframes in
// RESIZE_DYNAMIC_SUBGOP):
// Evaluates candidate denominators d = 9..16 (downscale factor 8/d) in order.
// For each d, measures sharpness-weighted 16x16 Luma + weighted Chroma
// round-trip resampling distortion, compares dist / Qstep^2(d) against a
// frame-type-dependent threshold, and gates intra frames by sharpness-weighted
// AC ratio and inter frames by motion-compensated temporal residual energy.
static uint8_t calculate_dynamic_resize_scale(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;

  // Never downscale if the unscaled source is unavailable or lossless coding
  // is requested.
  if (unscaled == NULL || is_lossless_requested(rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  // av2_setup_frame_size() runs before av2_set_size_dependent_vars(). Query
  // the frame's baseline qindex assuming full resolution, saving and restoring
  // frame dimensions and rate-control fields so subsequent frame-size and Q
  // setup see unmodified state.
  const int saved_cm_width = cm->width;
  const int saved_cm_height = cm->height;
  const int saved_this_frame_target = cpi->rc.this_frame_target;
  const int saved_sb64_target_rate = cpi->rc.sb64_target_rate;
  const int saved_level1_qp = cpi->rc.level1_qp;
  const int saved_unresized_base_qindex = cpi->rc.unresized_base_qindex;
  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;
  if (is_stat_consumption_stage(cpi) &&
      (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
    av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
  }
  int bottom_index = 0, top_index = 0;
  const int qindex =
      cm->bridge_frame_info.is_bridge_frame
          ? cm->quant_params.base_qindex
          : av2_rc_pick_q_and_bounds(cpi, &cpi->rc, frm_dim_cfg->width,
                                     frm_dim_cfg->height, cpi->gf_group.index,
                                     &bottom_index, &top_index);
  cm->width = saved_cm_width;
  cm->height = saved_cm_height;
  cpi->rc.this_frame_target = saved_this_frame_target;
  cpi->rc.sb64_target_rate = saved_sb64_target_rate;
  cpi->rc.level1_qp = saved_level1_qp;
  cpi->rc.unresized_base_qindex = saved_unresized_base_qindex;

  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int max_qp = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth);
  const int is_intra = frame_is_intra_only(cm);

  // Keep full resolution if the selected qindex corresponds to lossless (0)
  // or is below the bit-depth-adjusted minimum QP threshold for dynamic resize.
  const int min_qp =
      (is_intra ? RESIZE_DYNAMIC_MINQP_KF : RESIZE_DYNAMIC_MINQP) +
      ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  if (qindex == 0 || qindex < min_qp) {
    return SCALE_NUMERATOR;
  }

  const int orig_w = unscaled->y_crop_width;
  const int orig_h = unscaled->y_crop_height;
  const size_t pel_count = (size_t)orig_w * (size_t)orig_h;
  uint16_t *const downscaled_buf =
      (uint16_t *)avm_malloc(pel_count * sizeof(*downscaled_buf));
  uint16_t *const upscaled_y =
      (uint16_t *)avm_malloc(pel_count * sizeof(*upscaled_y));
  uint16_t *const upscaled_uv =
      (uint16_t *)avm_malloc(pel_count * sizeof(*upscaled_uv));
  if (downscaled_buf == NULL || upscaled_y == NULL || upscaled_uv == NULL) {
    avm_free(downscaled_buf);
    avm_free(upscaled_y);
    avm_free(upscaled_uv);
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate buffers for dynamic resize");
  }

  avm_clear_system_state();
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double qstep_base = av2_convert_qindex_to_q(qindex, bit_depth) * bds;
  const double qstep_min =
      AVMMAX(av2_convert_qindex_to_q(min_qp, bit_depth) * bds, 1e-6);
  const double pel_ratio =
      is_intra ? 1.0 : get_resize_pel_ratio(orig_w, orig_h);
  const double lin_scale = (pel_ratio > 1.0) ? sqrt(pel_ratio) : 1.0;
  const double qstep_ratio = qstep_base / qstep_min;
  const double qstep_ratio_sq = qstep_ratio * qstep_ratio;
  const double mc_q_scale =
      (pel_ratio > 1.0) ? (lin_scale * pow(qstep_ratio, 2.0 * lin_scale))
                        : qstep_ratio_sq;
  const double thresh =
      is_intra ? RESIZE_DYNAMIC_THRESH_KF : (RESIZE_DYNAMIC_THRESH * pel_ratio);
  const double ac_thresh_kf = RESIZE_DYNAMIC_AC_THRESH_KF * qstep_ratio_sq;
  const double mc_thresh = AVMMIN(RESIZE_DYNAMIC_MC_THRESH * mc_q_scale,
                                  RESIZE_DYNAMIC_MC_RATIO_MAX);

  int have_mc = 0;
  double mc_eff = 1.0;
  double mc_bg = 1.0;
  if (!is_intra) {
    double mc_p50 = 0.0, mc_nz = 0.0, mc_acr = 0.0;
    if (compute_pair_mc_stats(unscaled, cpi->unscaled_last_source, orig_w,
                              orig_h, bit_depth, &mc_p50, &mc_nz, &mc_acr)) {
      mc_eff = compute_mc_effective_energy(
          mc_p50, mc_nz, mc_acr, qstep_base * qstep_base, pel_ratio, bit_depth);
      mc_bg = compute_mc_bg_energy(mc_p50, mc_nz, mc_acr);
      have_mc = 1;
    }
  }

  uint8_t best_denom = SCALE_NUMERATOR;
  for (int d = SCALE_NUMERATOR + 1; d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    const int qindex_d = clamp(qindex + av2_get_resize_qp_offset(d), 1, max_qp);
    avm_clear_system_state();
    const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
    const double qstep_sq = qstep * qstep;

    resample_plane_roundtrip(unscaled->y_buffer, orig_h, orig_w,
                             unscaled->y_stride, downscaled_buf, scaled_h,
                             scaled_w, upscaled_y, /*subx=*/0, /*suby=*/0, d,
                             bit_depth, /*is_uv=*/0);
    const double uv_mse = compute_resample_chroma_mse(
        unscaled, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth,
        downscaled_buf, upscaled_uv);

    avm_clear_system_state();
    double luma_mse = 0.0;
    double ac_ratio = 0.0;
    compute_nonflat_resample_mse_and_ac(unscaled, upscaled_y, orig_w, orig_h,
                                        bit_depth, &luma_mse, &ac_ratio);
    const double dist = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
    const double dec_frac =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);
    const int content_ok =
        have_mc ? (dist / mc_eff < mc_thresh) : (ac_ratio < ac_thresh_kf);
    const int bg_ok = !have_mc || pel_ratio <= 1.0 || (dist < dec_frac * mc_bg);
    if (dist / qstep_sq < thresh && content_ok && bg_ok) {
      best_denom = (uint8_t)d;
    } else {
      break;
    }
  }

  if (!is_intra && pel_ratio >= RESIZE_DYNAMIC_HIGHRES_PEL_RATIO &&
      best_denom == SCALE_NUMERATOR + 1) {
    best_denom = SCALE_NUMERATOR;
  }

  avm_free(downscaled_buf);
  avm_free(upscaled_y);
  avm_free(upscaled_uv);
  return best_denom;
}

// Dynamic resize scale decision for a subgop in RESIZE_DYNAMIC_SUBGOP:
// Evaluates candidate denominators d = 9..16 (downscale factor 8/d) in order.
// Estimates the subgop's motion-compensated temporal coding energy from up to
// 4 evenly-spaced consecutive raw lookahead frame pairs, computes the largest
// contiguous d whose subgop-average (dist / Qstep^2(d)) is below
// RESIZE_DYNAMIC_SUBGOP_THRESH and whose subgop-average dist / mc_eff is below
// the Qstep^2-scaled RESIZE_DYNAMIC_SUBGOP_MC_THRESH, and applies discrete
// Schmitt-trigger hysteresis against cpi->resize_prev_subgop_denom within the
// closed GOP.
static uint8_t calculate_dynamic_subgop_resize_scale(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;
  GF_GROUP *const gf_group = &cpi->gf_group;

  // Never downscale if the unscaled source is unavailable or lossless coding
  // is requested.
  if (unscaled == NULL || is_lossless_requested(rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  const YV12_BUFFER_CONFIG *subgop_srcs[MAX_STATIC_GF_GROUP_LENGTH];
  int subgop_qindex[MAX_STATIC_GF_GROUP_LENGTH];
  int num_subgop_frames = 0;

  // Save encoder and rate-control state before querying baseline full-res
  // qindex values across the frames in the current subgop.
  const int cur_gf_index = gf_group->index;
  const int base_disp_idx = gf_group->cur_frame_idx[cur_gf_index] +
                            (cm->immediate_output_picture != 0);
  const int saved_cm_width = cm->width;
  const int saved_cm_height = cm->height;
  const FRAME_TYPE saved_frame_type = cm->current_frame.frame_type;
  const int saved_pyramid_level = cm->current_frame.pyramid_level;
  const int saved_this_frame_target = cpi->rc.this_frame_target;
  const int saved_sb64_target_rate = cpi->rc.sb64_target_rate;
  const int saved_level1_qp = cpi->rc.level1_qp;
  const int saved_unresized_base_qindex = cpi->rc.unresized_base_qindex;
  const int saved_is_src_frame_alt_ref = cpi->rc.is_src_frame_alt_ref;
  int saved_active_best_quality[MAX_ARF_LAYERS + 1];
  memcpy(saved_active_best_quality, cpi->rc.active_best_quality,
         sizeof(saved_active_best_quality));

  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;
  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int max_qp = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth);
  int has_lossless_frame = 0;
  int max_subgop_qindex = 0;
  int max_la_idx = -1;

  // Collect each coded frame in the current subgop along with its intended
  // full-resolution pyramid-level base_qindex.
  for (int gf_idx = cur_gf_index; gf_idx < gf_group->size; ++gf_idx) {
    // Skip pure overlay updates since they do not code a new source picture.
    const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_idx];
    if (update_type == OVERLAY_UPDATE || update_type == INTNL_OVERLAY_UPDATE ||
        update_type == KFFLT_OVERLAY_UPDATE) {
      continue;
    }

    const int frame_display_index =
        gf_group->cur_frame_idx[gf_idx] + gf_group->arf_src_offset[gf_idx];
    const int lookahead_idx = frame_display_index - base_disp_idx;
    if (lookahead_idx > max_la_idx) {
      max_la_idx = lookahead_idx;
    }

    // Locate the frame's unscaled source buffer: use cpi->unscaled_source for
    // the frame currently being encoded, and peek into the lookahead buffer
    // for the remaining frames in the subgop.
    const YV12_BUFFER_CONFIG *frame_src = NULL;
    if (gf_idx == cur_gf_index) {
      frame_src = unscaled;
    } else if (lookahead_idx >= 0) {
      const struct lookahead_entry *const entry = av2_lookahead_peek(
          cpi->lookahead, lookahead_idx, cpi->compressor_stage);
      if (entry != NULL) {
        frame_src = &entry->img;
      }
    }
    if (frame_src == NULL) {
      continue;
    }

    // Configure pyramid level and rate-control target for gf_idx at full
    // resolution, then query its baseline unresized qindex.
    gf_group->index = gf_idx;
    cm->current_frame.frame_type = INTER_FRAME;
    cm->current_frame.pyramid_level = get_true_pyr_level(
        gf_group->layer_depth[gf_idx], 0, gf_group->max_layer_depth, 0);
    cpi->rc.is_src_frame_alt_ref = 0;
    av2_rc_set_frame_target(cpi, gf_group->bit_allocation[gf_idx],
                            frm_dim_cfg->width, frm_dim_cfg->height);
    if (is_stat_consumption_stage(cpi) &&
        (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
      av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
    }

    int bottom_index = 0, top_index = 0;
    const int qindex = av2_rc_pick_q_and_bounds(
        cpi, &cpi->rc, frm_dim_cfg->width, frm_dim_cfg->height, gf_idx,
        &bottom_index, &top_index);
    if (qindex == 0) {
      has_lossless_frame = 1;
      break;
    }
    if (qindex > max_subgop_qindex) {
      max_subgop_qindex = qindex;
    }

    // Propagate active_best_quality across deeper pyramid layers so subsequent
    // frames in the subgop see the same hierarchical Q bounds as during actual
    // encoding.
    for (int i = gf_group->layer_depth[gf_idx]; i <= MAX_ARF_LAYERS; ++i) {
      cpi->rc.active_best_quality[i] = qindex;
    }

    subgop_srcs[num_subgop_frames] = frame_src;
    subgop_qindex[num_subgop_frames] = qindex;
    ++num_subgop_frames;
  }

  // Restore encoder and rate-control state to their pre-query values.
  gf_group->index = cur_gf_index;
  cm->width = saved_cm_width;
  cm->height = saved_cm_height;
  cm->current_frame.frame_type = saved_frame_type;
  cm->current_frame.pyramid_level = saved_pyramid_level;
  cpi->rc.this_frame_target = saved_this_frame_target;
  cpi->rc.sb64_target_rate = saved_sb64_target_rate;
  cpi->rc.level1_qp = saved_level1_qp;
  cpi->rc.unresized_base_qindex = saved_unresized_base_qindex;
  cpi->rc.is_src_frame_alt_ref = saved_is_src_frame_alt_ref;
  memcpy(cpi->rc.active_best_quality, saved_active_best_quality,
         sizeof(saved_active_best_quality));

  // Keep full resolution if any frame in the subgop is lossless (qindex == 0).
  if (has_lossless_frame) {
    return SCALE_NUMERATOR;
  }
  if (num_subgop_frames == 0) {
    return calculate_dynamic_resize_scale(cpi);
  }
  // Keep full resolution if the subgop's baseline (unboosted leaf) qindex is
  // below the bit-depth-adjusted RESIZE_DYNAMIC_SUBGOP_MINQP threshold.
  const int min_qp =
      RESIZE_DYNAMIC_SUBGOP_MINQP + ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  if (max_subgop_qindex < min_qp) {
    return SCALE_NUMERATOR;
  }

  const int orig_w = unscaled->y_crop_width;
  const int orig_h = unscaled->y_crop_height;
  const size_t pel_count = (size_t)orig_w * (size_t)orig_h;
  uint16_t *const downscaled_buf =
      (uint16_t *)avm_malloc(pel_count * sizeof(*downscaled_buf));
  uint16_t *const upscaled_y =
      (uint16_t *)avm_malloc(pel_count * sizeof(*upscaled_y));
  uint16_t *const upscaled_uv =
      (uint16_t *)avm_malloc(pel_count * sizeof(*upscaled_uv));
  if (downscaled_buf == NULL || upscaled_y == NULL || upscaled_uv == NULL) {
    avm_free(downscaled_buf);
    avm_free(upscaled_y);
    avm_free(upscaled_uv);
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate buffers for dynamic subgop resize");
  }

  avm_clear_system_state();
  const double pel_ratio = get_resize_pel_ratio(orig_w, orig_h);
  const double lin_scale = (pel_ratio > 1.0) ? sqrt(pel_ratio) : 1.0;
  const double base_subgop_thresh = RESIZE_DYNAMIC_SUBGOP_THRESH * pel_ratio;
  double subgop_thresh = base_subgop_thresh;
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double qstep_subgop =
      av2_convert_qindex_to_q(max_subgop_qindex, bit_depth) * bds;
  const double qstep_min =
      AVMMAX(av2_convert_qindex_to_q(min_qp, bit_depth) * bds, 1e-6);
  const double qstep_ratio = qstep_subgop / qstep_min;
  const double qstep_ratio_sq = qstep_ratio * qstep_ratio;
  const double mc_q_scale =
      (pel_ratio > 1.0) ? (lin_scale * pow(qstep_ratio, 2.0 * lin_scale))
                        : qstep_ratio_sq;
  double subgop_mc_thresh = AVMMIN(RESIZE_DYNAMIC_SUBGOP_MC_THRESH * mc_q_scale,
                                   RESIZE_DYNAMIC_MC_RATIO_MAX);
  const double subgop_ac_thresh_fallback =
      RESIZE_DYNAMIC_AC_THRESH_KF * qstep_ratio_sq;

  // Estimate subgop temporal MC coding energy from up to 4 evenly-spaced
  // consecutive raw frame pairs across the subgop's display span.
  double sum_mc_p50 = 0.0, sum_mc_nz = 0.0, sum_mc_acr = 0.0;
  int num_mc_pairs = 0;
  if (max_la_idx >= 1) {
    const int target_pairs = AVMMIN(4, max_la_idx);
    int prev_idx = -1;
    for (int p = 0; p < target_pairs; ++p) {
      const int idx =
          (target_pairs == 1) ? 0 : (p * (max_la_idx - 1)) / (target_pairs - 1);
      if (idx == prev_idx) continue;
      prev_idx = idx;
      const struct lookahead_entry *const e0 =
          av2_lookahead_peek(cpi->lookahead, idx, cpi->compressor_stage);
      const struct lookahead_entry *const e1 =
          av2_lookahead_peek(cpi->lookahead, idx + 1, cpi->compressor_stage);
      double p50 = 0.0, nz = 0.0, acr = 0.0;
      if (e0 != NULL && e1 != NULL &&
          compute_pair_mc_stats(&e1->img, &e0->img, orig_w, orig_h, bit_depth,
                                &p50, &nz, &acr)) {
        sum_mc_p50 += p50;
        sum_mc_nz += nz;
        sum_mc_acr += acr;
        ++num_mc_pairs;
      }
    }
  }
  if (num_mc_pairs == 0 && cm->immediate_output_picture != 0 &&
      max_la_idx >= 0) {
    const struct lookahead_entry *const e0 =
        av2_lookahead_peek(cpi->lookahead, 0, cpi->compressor_stage);
    double p50 = 0.0, nz = 0.0, acr = 0.0;
    if (e0 != NULL && compute_pair_mc_stats(&e0->img, unscaled, orig_w, orig_h,
                                            bit_depth, &p50, &nz, &acr)) {
      sum_mc_p50 += p50;
      sum_mc_nz += nz;
      sum_mc_acr += acr;
      ++num_mc_pairs;
    }
  }
  if (num_mc_pairs == 0) {
    double p50 = 0.0, nz = 0.0, acr = 0.0;
    if (compute_pair_mc_stats(unscaled, cpi->unscaled_last_source, orig_w,
                              orig_h, bit_depth, &p50, &nz, &acr)) {
      sum_mc_p50 += p50;
      sum_mc_nz += nz;
      sum_mc_acr += acr;
      ++num_mc_pairs;
    }
  }
  const int have_mc = (num_mc_pairs > 0);
  const double mc_p50 = have_mc ? (sum_mc_p50 / num_mc_pairs) : 0.0;
  const double mc_nz = have_mc ? (sum_mc_nz / num_mc_pairs) : 0.0;
  const double mc_acr = have_mc ? (sum_mc_acr / num_mc_pairs) : 0.0;
  const double mc_uni =
      have_mc ? AVMMIN(mc_p50 / AVMMAX(mc_nz, bds * bds), 1.0) : 0.0;
  const double mc_eff =
      have_mc ? compute_mc_effective_energy(mc_p50, mc_nz, mc_acr,
                                            qstep_subgop * qstep_subgop,
                                            pel_ratio, bit_depth)
              : 1.0;
  const double mc_bg =
      have_mc ? compute_mc_bg_energy(mc_p50, mc_nz, mc_acr) : 1.0;

  const uint8_t prev_denom = cpi->resize_prev_subgop_denom;
  double hyst_step = (pel_ratio > 1.0)
                         ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_STEP, pel_ratio)
                         : RESIZE_DYNAMIC_SUBGOP_HYST_STEP;
  double hyst_keep = (pel_ratio > 1.0)
                         ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_KEEP, pel_ratio)
                         : RESIZE_DYNAMIC_SUBGOP_HYST_KEEP;
  if (have_mc && pel_ratio <= 1.0) {
    if (mc_acr < RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI) {
      // When temporal motion compensation removes > 91% of block AC variance
      // across distributed frame motion, attenuate subgop_mc_thresh cubically
      // so coarse-QP qstep_ratio_sq scaling does not trigger lossy downscaling
      // on temporally predictable panning scenes.
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF;
      subgop_mc_thresh *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN) {
      if (mc_acr > 1.0 && mc_uni > 0.35) {
        // Under extreme global temporal unpredictability (MC residual > intra
        // AC variance across the frame), tighten step-deeper hysteresis to
        // suppress single-subgop scale spikes within a GOP.
        hyst_step = RESIZE_DYNAMIC_SUBGOP_EXTREME_ACR_HYST_STEP;
      } else if ((mc_uni > 0.20 && mc_uni < 0.48) ||
                 (mc_acr > 0.70 && mc_uni > 0.04)) {
        // Under high temporal unpredictability with spatially distributed
        // motion residuals, widen hold hysteresis across the GOP and relax
        // subgop_thresh with qstep_ratio_sq to allow deeper subgop scaling.
        hyst_step = RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_STEP;
        hyst_keep = RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_KEEP;
        const double max_q_boost =
            (mc_uni > 0.34 && mc_acr > 0.50)
                ? RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_HIGH
                : RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_MOD;
        const double q_boost =
            (qstep_ratio_sq > 15.0)
                ? max_q_boost
                : AVMMIN(1.0 + RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_SLOPE *
                                   qstep_ratio_sq,
                         max_q_boost);
        subgop_thresh *= q_boost;
      }
    }
  }
  const double content_thresh =
      have_mc ? subgop_mc_thresh : subgop_ac_thresh_fallback;
  double avg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double content_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double bg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  int max_eval_d = SCALE_NUMERATOR;
  int still_contiguous = 1;
  uint8_t raw_best = SCALE_NUMERATOR;

  for (int d = SCALE_NUMERATOR + 1; d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    double sum_ratio = 0.0;
    double sum_dist = 0.0;
    double sum_ac_ratio = 0.0;
    int valid_frames = 0;
    for (int f = 0; f < num_subgop_frames; ++f) {
      const YV12_BUFFER_CONFIG *const src = subgop_srcs[f];
      if (src->y_crop_width != orig_w || src->y_crop_height != orig_h) {
        continue;
      }
      resample_plane_roundtrip(src->y_buffer, orig_h, orig_w, src->y_stride,
                               downscaled_buf, scaled_h, scaled_w, upscaled_y,
                               /*subx=*/0, /*suby=*/0, d, bit_depth,
                               /*is_uv=*/0);
      const double uv_mse = compute_resample_chroma_mse(
          src, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth, downscaled_buf,
          upscaled_uv);

      avm_clear_system_state();
      double luma_mse = 0.0;
      double ac_ratio = 0.0;
      compute_nonflat_resample_mse_and_ac(src, upscaled_y, orig_w, orig_h,
                                          bit_depth, &luma_mse, &ac_ratio);
      const double dist = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
      const int qindex_d =
          clamp(subgop_qindex[f] + av2_get_resize_qp_offset(d), 1, max_qp);
      const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
      const double qstep_sq = qstep * qstep;
      sum_ratio += dist / qstep_sq;
      sum_dist += dist;
      sum_ac_ratio += ac_ratio;
      ++valid_frames;
    }
    if (valid_frames == 0) break;

    const double avg_ratio = sum_ratio / (double)valid_frames;
    const double avg_dist = sum_dist / (double)valid_frames;
    const double avg_ac_ratio = sum_ac_ratio / (double)valid_frames;
    const double content_ratio = have_mc ? (avg_dist / mc_eff) : avg_ac_ratio;
    const double dec_frac =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);
    const double bg_ratio =
        (have_mc && pel_ratio > 1.0) ? (avg_dist / (dec_frac * mc_bg)) : 0.0;
    avg_ratio_arr[d] = avg_ratio;
    content_ratio_arr[d] = content_ratio;
    bg_ratio_arr[d] = bg_ratio;
    max_eval_d = d;

    if (still_contiguous && avg_ratio < subgop_thresh &&
        content_ratio < content_thresh && bg_ratio < 1.0) {
      raw_best = (uint8_t)d;
    } else {
      still_contiguous = 0;
      // Continue evaluating up to prev_denom only while holding prev_denom via
      // hyst_keep remains possible.
      if (prev_denom <= SCALE_NUMERATOR || d >= prev_denom ||
          avg_ratio >= hyst_keep * subgop_thresh ||
          content_ratio >= hyst_keep * content_thresh ||
          bg_ratio >= hyst_keep) {
        break;
      }
    }
  }

  // Apply discrete Schmitt-trigger hysteresis relative to the preceding subgop
  // in the same closed GOP so marginal boundary fluctuations do not break
  // cross-subgop same-resolution temporal tools (TIP_FRAME_DIRECT, MFMV),
  // widening the hysteresis band above 1080p via pow(HYST_*, pel_ratio):
  // - Stepping deeper past prev_denom requires d = prev_denom + 1 to clear
  //   hyst_step * thresh; once the scale change from prev_denom is warranted,
  //   jump directly to raw_best.
  // - Stepping shallower below prev_denom holds prev_denom while all skipped
  //   denominators up to prev_denom remain within hyst_keep * thresh; otherwise
  //   return to raw_best.
  uint8_t best_denom = raw_best;
  if (prev_denom >= SCALE_NUMERATOR) {
    if (raw_best > prev_denom) {
      const int d_next = (int)prev_denom + 1;
      if (d_next <= max_eval_d &&
          avg_ratio_arr[d_next] < hyst_step * subgop_thresh &&
          content_ratio_arr[d_next] < hyst_step * content_thresh &&
          bg_ratio_arr[d_next] < hyst_step) {
        best_denom = raw_best;
      } else {
        best_denom = prev_denom;
      }
    } else if (raw_best < prev_denom && prev_denom <= max_eval_d) {
      int can_hold = 1;
      const int start_d = AVMMAX(SCALE_NUMERATOR + 1, (int)raw_best + 1);
      for (int d = start_d; d <= (int)prev_denom; ++d) {
        if (avg_ratio_arr[d] >= hyst_keep * subgop_thresh ||
            content_ratio_arr[d] >= hyst_keep * content_thresh ||
            bg_ratio_arr[d] >= hyst_keep) {
          can_hold = 0;
          break;
        }
      }
      if (can_hold) {
        best_denom = prev_denom;
      }
    }
  }

  // Skip isolated d = 9 (8/9 scale, 79% pixels) back to full resolution when:
  // - on high-resolution sources (>= 2x 1080p pixel count), or
  // - d = 9 already consumes a non-trivial share of both temporal MC coding
  //   energy (>= RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO) and quantization
  //   distortion budget (>= RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO *
  //   base_subgop_thresh), where cross-scale Lanczos-5 reference resampling
  //   overhead and disabling same-resolution TIP_FRAME_DIRECT / MFMV outweigh
  //   an 11% linear scale reduction unless at least d >= 10 (64% pixels) is
  //   selected.
  if (best_denom == SCALE_NUMERATOR + 1 &&
      (pel_ratio >= RESIZE_DYNAMIC_HIGHRES_PEL_RATIO ||
       (mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
        content_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO &&
        avg_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO * base_subgop_thresh))) {
    best_denom = SCALE_NUMERATOR;
  }

  avm_free(downscaled_buf);
  avm_free(upscaled_y);
  avm_free(upscaled_uv);
  return best_denom;
}

static uint8_t calculate_next_resize_scale(AV2_COMP *cpi) {
  // Choose an arbitrary random number
  static unsigned int seed = 56789;
  const ResizeCfg *resize_cfg = &cpi->oxcf.resize_cfg;
  if (is_stat_generation_stage(cpi)) return SCALE_NUMERATOR;
  uint8_t new_denom = SCALE_NUMERATOR;
  const int display_order_hint = cpi->common.current_frame.display_order_hint;
  const uint8_t is_low_delay_enc = (cpi->oxcf.gf_cfg.lag_in_frames == 0);

  if (cpi->common.seq_params.single_picture_header_flag) return SCALE_NUMERATOR;
  switch (resize_cfg->resize_mode) {
    case RESIZE_NONE: new_denom = SCALE_NUMERATOR; break;
    case RESIZE_FIXED:
      if (cpi->common.current_frame.frame_type == KEY_FRAME)
        new_denom = resize_cfg->resize_kf_scale_denominator;
      else
        new_denom = resize_cfg->resize_scale_denominator;
      break;
    case RESIZE_RANDOM: new_denom = lcg_rand16(&seed) % 9 + 8; break;
    case RESIZE_RANDOM_SUBGOP:
      // Keyframes pick an independent random scale like RESIZE_RANDOM; for
      // each subgop, pick a random scale once on its first non-keyframe and
      // reuse it across all frames in that subgop.
      if (cpi->common.current_frame.frame_type == KEY_FRAME) {
        new_denom = lcg_rand16(&seed) % 9 + 8;
        cpi->resize_subgop_denom = 0;
      } else {
        if (cpi->resize_subgop_denom == 0) {
          cpi->resize_subgop_denom = lcg_rand16(&seed) % 9 + 8;
        }
        new_denom = cpi->resize_subgop_denom;
      }
      break;
    case RESIZE_DYNAMIC: new_denom = calculate_dynamic_resize_scale(cpi); break;
    case RESIZE_DYNAMIC_SUBGOP:
      // Keyframes pick a dynamic scale using calculate_dynamic_resize_scale()
      // and reset subgop scale hysteresis; for each subgop, compute the subgop
      // dynamic scale once on its first non-keyframe and reuse it across all
      // frames in that subgop.
      if (cpi->common.current_frame.frame_type == KEY_FRAME) {
        new_denom = calculate_dynamic_resize_scale(cpi);
        cpi->resize_subgop_denom = 0;
        cpi->resize_prev_subgop_denom = 0;
      } else {
        if (cpi->resize_subgop_denom == 0) {
          cpi->resize_subgop_denom = calculate_dynamic_subgop_resize_scale(cpi);
          cpi->resize_prev_subgop_denom = cpi->resize_subgop_denom;
        }
        new_denom = cpi->resize_subgop_denom;
      }
      break;
    case RESIZE_PATTERN:
      new_denom = is_low_delay_enc
                      ? get_resolution_ratio_pattern1(display_order_hint)
                      : get_resolution_ratio_pattern2(display_order_hint);
      break;
    case RESIZE_BRIDGE_FRAME_PATTERN:
      new_denom = get_resolution_bridge_frame_pattern(
          cpi->common.bridge_frame_info.frame_count);
      break;
    default: assert(0);
  }
  return new_denom;
}

typedef struct {
  int resize_width;
  int resize_height;
  uint8_t resize_denom;
} size_params_type;

static int dimensions_are_ok(int owidth, int oheight, size_params_type *rsz) {
  // Only need to check the width, as scaling is horizontal only.
  (void)oheight;
  return dimension_is_ok(owidth, rsz->resize_width, SCALE_NUMERATOR);
}

static int validate_size_scales(RESIZE_MODE resize_mode, int owidth,
                                int oheight, size_params_type *rsz) {
  if (dimensions_are_ok(owidth, oheight, rsz)) {  // Nothing to do.
    return 1;
  }
  if (resize_mode == RESIZE_RANDOM || resize_mode == RESIZE_RANDOM_SUBGOP ||
      resize_mode == RESIZE_DYNAMIC || resize_mode == RESIZE_DYNAMIC_SUBGOP ||
      resize_mode == RESIZE_PATTERN) {
    // Alter resize scale as needed to enforce conformity.
    int resize_denom =
        (2 * SCALE_NUMERATOR * SCALE_NUMERATOR) / SCALE_NUMERATOR;
    rsz->resize_width = owidth;
    rsz->resize_height = oheight;
    av2_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                              resize_denom);
    if (!dimensions_are_ok(owidth, oheight, rsz)) {
      if (resize_denom > SCALE_NUMERATOR) {
        --resize_denom;
        rsz->resize_width = owidth;
        rsz->resize_height = oheight;
        av2_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                                  resize_denom);
      }
    }
    rsz->resize_denom = (uint8_t)resize_denom;
  } else {  // We are not allowed to alter resize scale.
    return 0;
  }
  return dimensions_are_ok(owidth, oheight, rsz);
}

// Calculates resize params for next frame.
static size_params_type calculate_next_size_params(AV2_COMP *cpi) {
  const AV2EncoderConfig *oxcf = &cpi->oxcf;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  // Reset to SCALE_NUMERATOR (no resize) before computing the next scale so
  // any full-resolution qindex query inside calculate_next_resize_scale()
  // does not inherit the previous frame's resize QP offset.
  cpi->resize_scale_denom = SCALE_NUMERATOR;
  size_params_type rsz = { frm_dim_cfg->width, frm_dim_cfg->height,
                           SCALE_NUMERATOR };
  int resize_denom = SCALE_NUMERATOR;
  if (is_stat_generation_stage(cpi)) return rsz;
  if (resize_pending_params->width && resize_pending_params->height) {
    rsz.resize_width = resize_pending_params->width;
    rsz.resize_height = resize_pending_params->height;
    // Approximate the effective denominator for externally triggered resizes.
    if (rsz.resize_width < frm_dim_cfg->width) {
      rsz.resize_denom = (uint8_t)clamp(
          (frm_dim_cfg->width * SCALE_NUMERATOR + (rsz.resize_width >> 1)) /
              rsz.resize_width,
          SCALE_NUMERATOR, 2 * SCALE_NUMERATOR);
    }
    resize_pending_params->width = resize_pending_params->height = 0;
    return rsz;
  } else {
    resize_denom = calculate_next_resize_scale(cpi);
    rsz.resize_denom = (uint8_t)resize_denom;
    rsz.resize_width = frm_dim_cfg->width;
    rsz.resize_height = frm_dim_cfg->height;
    av2_calculate_scaled_size(&rsz.resize_width, &rsz.resize_height,
                              resize_denom);
  }
  if (oxcf->resize_cfg.resize_mode != RESIZE_BRIDGE_FRAME_PATTERN)
    if (!validate_size_scales(oxcf->resize_cfg.resize_mode, frm_dim_cfg->width,
                              frm_dim_cfg->height, &rsz))
      assert(0 && "Invalid scale parameters");
  return rsz;
}

static void setup_frame_size_from_params(AV2_COMP *cpi,
                                         const size_params_type *rsz) {
  int encode_width = rsz->resize_width;
  int encode_height = rsz->resize_height;
  // Record the chosen resize denominator on cpi so rate control and RD setup
  // can apply the corresponding logarithmic resize QP offset and RDMULT scale.
  cpi->resize_scale_denom = rsz->resize_denom;
  av2_set_frame_size(cpi, encode_width, encode_height);
}

void av2_setup_frame_size(AV2_COMP *cpi) {
  const size_params_type rsz = calculate_next_size_params(cpi);
  setup_frame_size_from_params(cpi, &rsz);
  AV2_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;
  av2_get_ref_frames(cm, current_frame->display_order_hint, 1, 0,
                     cm->ref_frame_map_pairs);
  cm->ref_frame_flags = (1 << cpi->common.ref_frames_info.num_total_refs) - 1;
  cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;

  if (current_frame->frame_type == KEY_FRAME ||
      current_frame->frame_type == INTRA_ONLY_FRAME)
    cm->ref_frame_flags = 0;
}
