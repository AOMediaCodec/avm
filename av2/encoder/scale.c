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

#include <limits.h>
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
#define RESIZE_DYNAMIC_MC_PAN_GRAIN_WEIGHT 8.0
#define RESIZE_DYNAMIC_MC_UNIFORM_NOISE_GRAIN_WEIGHT 14.0
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0 0.60
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_SPAN 0.15
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX 0.30
#define RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_SPAN 0.15
#define RESIZE_DYNAMIC_MC_UNIFORM_NOISE_UNI_MIN 0.65
#define RESIZE_DYNAMIC_MC_UNIFORM_NOISE_P50_MIN 15.0
#define RESIZE_DYNAMIC_MC_UNIFORM_NOISE_MIN_QRAT_SQ 4.0
#define RESIZE_DYNAMIC_SUBGOP_UNIFORM_NOISE_THRESH_BOOST 1.68
#define RESIZE_DYNAMIC_REF_PELS (1920.0 * 1080.0)
#define RESIZE_DYNAMIC_MAX_PEL_RATIO 4.0
#define RESIZE_DYNAMIC_HIGHRES_PEL_RATIO 2.0
#define RESIZE_DYNAMIC_HIGHRES_PAN_P50_MIN 7.0
#define RESIZE_DYNAMIC_HIGHRES_PAN_P50_STRONG 10.0
#define RESIZE_DYNAMIC_HIGHRES_PAN_MOTION_MIN 0.45
#define RESIZE_DYNAMIC_SUBGOP_HYST_STEP 0.78
#define RESIZE_DYNAMIC_SUBGOP_HYST_KEEP 1.12
#define RESIZE_DYNAMIC_SUBGOP_HYST_KEEP_COARSE_ADD 0.14
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX 0.088
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF 0.125
#define RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI 0.021
#define RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_MAX 0.140
#define RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_REF 0.250
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN 0.40
#define RESIZE_DYNAMIC_SUBGOP_EXTREME_ACR_HYST_STEP 0.50
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_STEP 0.62
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_KEEP 1.43
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_SLOPE 0.045
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_MOD 1.24
#define RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_Q_MAX_HIGH 1.46
#define RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO 0.080
#define RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO 0.028
#define RESIZE_DYNAMIC_KF_COSCALE_MIN_DENOM (SCALE_NUMERATOR + 2)
#define RESIZE_DYNAMIC_KF_COSCALE_MAX_Q_NORM 0.56
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_DENOM (SCALE_NUMERATOR + 4)
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_MIN_QRAT_SQ 4.0
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_HIGH_QRAT_SQ 15.0
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID 0.132
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ 76.0
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_ACR_MID 0.145
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MIN 0.018
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_MID_UNI 0.30
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_MID_LOW_UNI 0.16
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_HIGH_UNI 0.16
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_HIGH_LOW_UNI 0.05
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QREF_MID 1.05
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QREF_HIGH 0.25
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_MID 12.5
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_HIGH 3.2
#define RESIZE_DYNAMIC_SUBGOP_SYNERGY_QNORM_FLOOR 0.02
#define RESIZE_DYNAMIC_CHROMA_WEIGHT 4.0
#define RESIZE_DYNAMIC_MIN_BLOCK_VAR 64.0
#define RESIZE_DYNAMIC_BLOCK_PERCENTILE 0.75
#define RESIZE_DYNAMIC_MINQP_KF 205
#define RESIZE_DYNAMIC_MINQP 155
#define RESIZE_DYNAMIC_SUBGOP_MINQP 155
#define RESIZE_DYNAMIC_QREF 180
#define RESIZE_DYNAMIC_HIGHRES_PAN_QREF 148
#define RESIZE_DYNAMIC_HALF_SPLIT_PRECHECK_RATIO 2.05
#define RESIZE_DYNAMIC_HALF_SPLIT_DIST_RATIO 2.35
#define RESIZE_DYNAMIC_HALF_SPLIT_MIN_STEP 3
#define RESIZE_DYNAMIC_CUT_NZ_RATIO 6.0
#define RESIZE_DYNAMIC_CUT_NZ_MIN 150.0
#define RESIZE_DYNAMIC_CUT_ACR_MIN 0.90
#define RESIZE_DYN_UNIFORM_NOISE_MIN_QRAT_SQ 1.0
#define RESIZE_DYN_UNIFORM_NOISE_GRAIN_WEIGHT 22.0
#define RESIZE_DYN_UNIFORM_NOISE_THRESH_BOOST 6.60
#define RESIZE_DYN_UNIFORM_NOISE_MC_BOOST 3.20
#define RESIZE_DYN_SYNERGY_MIN_QRAT_SQ 1.0
#define RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM (SCALE_NUMERATOR + 2)
#define RESIZE_DYN_SYNERGY_HOLD_LOW_ACR 0.072
#define RESIZE_DYN_SYNERGY_HOLD_MID_ACR 0.125
#define RESIZE_DYN_SYNERGY_HOLD_HYST_KEEP 1.45
#define RESIZE_DYN_SYNERGY_HOLD_ACR_MIN 0.25
#define RESIZE_DYN_SYNERGY_QCAP_MID_UNI 0.72
#define RESIZE_DYN_SYNERGY_QCAP_MID_LOW_UNI 0.60
#define RESIZE_DYN_SYNERGY_QCAP_HIGH_UNI 0.36
#define RESIZE_DYN_SYNERGY_QCAP_HIGH_LOW_UNI 0.05
#define RESIZE_DYN_SYNERGY_HOLD_QCAP_MULT 1.25
#define RESIZE_DYN_SYNERGY_QREF_MID 2.80
#define RESIZE_DYN_SYNERGY_QREF_HIGH 0.70
#define RESIZE_DYN_SYNERGY_MAX_MID 45.0
#define RESIZE_DYN_SYNERGY_MAX_HIGH 9.0
#define RESIZE_DYN_SYNERGY_KF_MAX_Q_NORM 0.92
#define RESIZE_DYN_EXTREME_MC_ACR_MIN 0.46
#define RESIZE_DYN_EXTREME_MC_P50_MIN 60.0
#define RESIZE_DYN_EXTREME_MC_MAX_DENOM (SCALE_NUMERATOR + 2)
#define RESIZE_DYN_EXTREME_MC_THRESH_FLOOR 0.0185
#define RESIZE_DYN_EXTREME_MC_QRAT_FLOOR 0.90
#define RESIZE_DYN_EXTREME_MC_STRONG_ACR_MIN 0.58
#define RESIZE_DYN_EXTREME_MC_STRONG_P50_MIN 100.0
#define RESIZE_DYN_EXTREME_MC_D9_STRONG_MULT 3.40
#define RESIZE_DYN_EXTREME_MC_D9_MULT 2.35
#define RESIZE_DYN_EXTREME_MC_D10_MULT 1.72
#define RESIZE_DYN_HIGHRES_COSCALE_P50_MIN 5.5
#define RESIZE_DYN_HIGHRES_COSCALE_ACR_MIN 0.50
#define RESIZE_DYN_HIGHRES_HIGH_ACR_Q_BOOST 1.90
#define RESIZE_DYN_HIGHRES_LOWQ_KF_MAX_Q_NORM 0.15
#define RESIZE_DYN_HIGHRES_HIGH_ACR_KF_MAX_Q_NORM 0.95
#define RESIZE_DYN_SMOOTH_ROLLOFF_MAX 2.42
#define RESIZE_DYN_VERY_SMOOTH_ROLLOFF_MAX 2.20
#define RESIZE_DYN_HIGH_ACR_SMOOTH_P50_MIN 20.0
#define RESIZE_DYN_HIGH_ACR_SMOOTH_MC_BOOST 2.50
#define RESIZE_DYN_HIGH_ACR_SMOOTH_Q_BOOST_MID 3.25
#define RESIZE_DYN_HIGH_ACR_SMOOTH_Q_BOOST_COARSE 2.65
#define RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_MID (SCALE_NUMERATOR + 6)
#define RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_COARSE (SCALE_NUMERATOR + 7)
#define RESIZE_DYN_SMOOTH_SYN_Q_BOOST_MID 1.50
#define RESIZE_DYN_SMOOTH_SYN_QCAP_MID 1.55
#define RESIZE_DYN_SMOOTH_SYN_QREF_MID 5.80
#define RESIZE_DYN_SMOOTH_SYN_MAX_MID 60.0
#define RESIZE_DYN_SMOOTH_SYN_QCAP_COARSE 0.60
#define RESIZE_DYN_SMOOTH_SYN_QREF_COARSE 1.15
#define RESIZE_DYN_SMOOTH_SYN_MAX_COARSE 15.0
#define RESIZE_DYN_FLAT_BG_UNI_MAX 0.040
#define RESIZE_DYN_FLAT_BG_NZ_P50_MIN 10.0
#define RESIZE_DYN_FLAT_BG_NZ_MIN 55.0
#define RESIZE_DYN_FLAT_BG_HOLD_NZ_MIN 45.0
#define RESIZE_DYN_FLAT_BG_ACR_MIN 0.080
#define RESIZE_DYN_FLAT_BG_HOLD_ACR_MIN 0.048
#define RESIZE_DYN_FLAT_BG_ROLLOFF_MAX 2.95
#define RESIZE_DYN_FLAT_BG_Q_BOOST_MID 1.50
#define RESIZE_DYN_FLAT_BG_Q_BOOST_COARSE 1.35
#define RESIZE_DYN_FLAT_BG_QCAP_MID 1.50
#define RESIZE_DYN_FLAT_BG_QREF_MID 6.80
#define RESIZE_DYN_FLAT_BG_QCAP_COARSE 0.90
#define RESIZE_DYN_FLAT_BG_QREF_COARSE 1.80
#define RESIZE_DYN_FLAT_BG_MAX_COARSE 18.0
#define RESIZE_DYN_CAUSAL_SPATIAL_JUMP_RATIO 2.35
#define RESIZE_DYN_CAUSAL_HOLD_BREAK_STEP 3

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

// Selects the k-th smallest element in arr[0..n-1] in O(n) average time using
// median-of-three Hoare quickselect (100% identical result to qsort + arr[k]).
static double select_kth_double(double *arr, int n, int k) {
  int left = 0;
  int right = n - 1;
  while (right - left > 16) {
    const int mid = left + ((right - left) >> 1);
    if (arr[mid] < arr[left]) {
      const double t = arr[left];
      arr[left] = arr[mid];
      arr[mid] = t;
    }
    if (arr[right] < arr[left]) {
      const double t = arr[left];
      arr[left] = arr[right];
      arr[right] = t;
    }
    if (arr[right] < arr[mid]) {
      const double t = arr[mid];
      arr[mid] = arr[right];
      arr[right] = t;
    }
    const double pivot = arr[mid];
    int i = left;
    int j = right;
    for (;;) {
      while (arr[i] < pivot) ++i;
      while (arr[j] > pivot) --j;
      if (i >= j) break;
      const double t = arr[i];
      arr[i] = arr[j];
      arr[j] = t;
      ++i;
      --j;
    }
    if (k <= j) {
      right = j;
    } else {
      left = j + 1;
    }
  }
  for (int i = left + 1; i <= right; ++i) {
    const double key = arr[i];
    int j = i - 1;
    while (j >= left && arr[j] > key) {
      arr[j + 1] = arr[j];
      --j;
    }
    arr[j + 1] = key;
  }
  return arr[k];
}

void av2_free_resize_dyn_scratch_bufs(AV2_COMP *cpi) {
  avm_free(cpi->resize_dyn_downscaled_buf);
  avm_free(cpi->resize_dyn_upscaled_y);
  avm_free(cpi->resize_dyn_upscaled_uv);
  avm_free(cpi->resize_dyn_sharp_mse);
  avm_free(cpi->resize_dyn_sharp_ac);
  cpi->resize_dyn_downscaled_buf = NULL;
  cpi->resize_dyn_upscaled_y = NULL;
  cpi->resize_dyn_upscaled_uv = NULL;
  cpi->resize_dyn_scratch_pels = 0;
  cpi->resize_dyn_sharp_mse = NULL;
  cpi->resize_dyn_sharp_ac = NULL;
  cpi->resize_dyn_sharp_blocks = 0;
}

static void ensure_resize_dyn_scratch_bufs(AV2_COMP *cpi, size_t pel_count,
                                           int max_blocks) {
  AV2_COMMON *const cm = &cpi->common;
  if (pel_count > cpi->resize_dyn_scratch_pels) {
    avm_free(cpi->resize_dyn_downscaled_buf);
    avm_free(cpi->resize_dyn_upscaled_y);
    avm_free(cpi->resize_dyn_upscaled_uv);
    cpi->resize_dyn_downscaled_buf = (uint16_t *)avm_malloc(
        pel_count * sizeof(*cpi->resize_dyn_downscaled_buf));
    cpi->resize_dyn_upscaled_y =
        (uint16_t *)avm_malloc(pel_count * sizeof(*cpi->resize_dyn_upscaled_y));
    cpi->resize_dyn_upscaled_uv = (uint16_t *)avm_malloc(
        pel_count * sizeof(*cpi->resize_dyn_upscaled_uv));
    if (cpi->resize_dyn_downscaled_buf == NULL ||
        cpi->resize_dyn_upscaled_y == NULL ||
        cpi->resize_dyn_upscaled_uv == NULL) {
      av2_free_resize_dyn_scratch_bufs(cpi);
      avm_internal_error(
          &cm->error, AVM_CODEC_MEM_ERROR,
          "Failed to allocate scratch buffers for dynamic resize");
    }
    cpi->resize_dyn_scratch_pels = pel_count;
  }
  if (max_blocks > cpi->resize_dyn_sharp_blocks) {
    avm_free(cpi->resize_dyn_sharp_mse);
    avm_free(cpi->resize_dyn_sharp_ac);
    cpi->resize_dyn_sharp_mse = (double *)avm_malloc(
        (size_t)max_blocks * sizeof(*cpi->resize_dyn_sharp_mse));
    cpi->resize_dyn_sharp_ac = (double *)avm_malloc(
        (size_t)max_blocks * sizeof(*cpi->resize_dyn_sharp_ac));
    if (cpi->resize_dyn_sharp_mse == NULL || cpi->resize_dyn_sharp_ac == NULL) {
      av2_free_resize_dyn_scratch_bufs(cpi);
      avm_internal_error(
          &cm->error, AVM_CODEC_MEM_ERROR,
          "Failed to allocate sharpness buffers for dynamic resize");
    }
    cpi->resize_dyn_sharp_blocks = max_blocks;
  }
}

// Per-frame source-only 16x16 AC variance and spatial sharpness weights,
// computed once per frame and reused across candidate denominators d = 9..16.
typedef struct {
  int valid;
  int num_blocks;
  int nz_count;
  double all_ac_sum;
  double *block_ac;
  double *block_sharp;
} ResizeFrameSharpCache;

static void free_frame_sharp_cache(ResizeFrameSharpCache *cache) {
  avm_free(cache->block_ac);
  avm_free(cache->block_sharp);
  memset(cache, 0, sizeof(*cache));
}

static void populate_frame_sharp_cache(const YV12_BUFFER_CONFIG *src,
                                       int orig_w, int orig_h,
                                       avm_bit_depth_t bit_depth,
                                       ResizeFrameSharpCache *cache) {
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

  cache->num_blocks = max_blocks;
  cache->nz_count = 0;
  cache->all_ac_sum = 0.0;
  cache->block_ac = NULL;
  cache->block_sharp = NULL;
  if (max_blocks > 0) {
    cache->block_ac =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*cache->block_ac));
    cache->block_sharp =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*cache->block_sharp));
    if (cache->block_ac == NULL || cache->block_sharp == NULL) {
      free_frame_sharp_cache(cache);
      return;
    }
  }

  int blk_idx = 0;
  int nz_count = 0;
  double all_ac_sum = 0.0;
  for (int r = 0; r + bsize <= orig_h; r += bsize) {
    for (int c = 0; c + bsize <= orig_w; c += bsize) {
      uint64_t sum = 0;
      uint64_t sum_sq = 0;
      for (int br = 0; br < bsize; ++br) {
        const uint16_t *const s_row = src_y + (r + br) * src_stride + c;
        uint32_t row_sum = 0;
        uint32_t row_sum_sq = 0;
        for (int bc = 0; bc < bsize; ++bc) {
          const uint32_t v = s_row[bc];
          row_sum += v;
          row_sum_sq += v * v;
        }
        sum += row_sum;
        sum_sq += row_sum_sq;
      }
      const double block_ac =
          ((double)sum_sq - ((double)sum * (double)sum) / (double)num_pels) /
          (double)num_pels;
      all_ac_sum += block_ac;
      cache->block_ac[blk_idx] = block_ac;
      if (block_ac > min_block_var) {
        uint64_t ge = 0;
        uint64_t le = 0;
        uint32_t max_g2 = 0;
        for (int br = 1; br < bsize - 1; ++br) {
          const uint16_t *const s_prev = src_y + (r + br - 1) * src_stride + c;
          const uint16_t *const s_curr = src_y + (r + br) * src_stride + c;
          const uint16_t *const s_next = src_y + (r + br + 1) * src_stride + c;
          for (int bc = 1; bc < bsize - 1; ++bc) {
            const int32_t v = s_curr[bc];
            const int32_t vl = s_curr[bc - 1];
            const int32_t vr = s_curr[bc + 1];
            const int32_t vu = s_prev[bc];
            const int32_t vd = s_next[bc];
            const int32_t gx = vr - vl;
            const int32_t gy = vd - vu;
            const int32_t lx = 2 * v - vl - vr;
            const int32_t ly = 2 * v - vu - vd;
            const uint32_t g2 = (uint32_t)(gx * gx + gy * gy);
            if (g2 > max_g2) max_g2 = g2;
            ge += g2;
            le += (uint64_t)(lx * lx + ly * ly);
          }
        }
        const double mean_g2 =
            AVMMAX((double)ge / inner_pels, bit_depth_scale_sq);
        const double mean_l2 = (double)le / inner_pels;
        const double kurt = (double)max_g2 / mean_g2;
        const double lap_grad = mean_l2 / mean_g2;
        cache->block_sharp[blk_idx] = (kurt / 8.0) * (1.0 + lap_grad);
        ++nz_count;
      } else {
        cache->block_sharp[blk_idx] = -1.0;
      }
      ++blk_idx;
    }
  }
  cache->nz_count = nz_count;
  cache->all_ac_sum = all_ac_sum;
  cache->valid = 1;
}

// Computes the 75th-percentile sharpness-weighted round-trip Luma resampling
// MSE and 75th-percentile sharpness-weighted (MSE / 16x16 block AC energy)
// over non-flat 16x16 blocks, reusing precomputed per-frame source sharpness
// weights and reusable scratch arrays when provided.
static void compute_nonflat_resample_mse_and_ac_cached(
    const YV12_BUFFER_CONFIG *src, const uint16_t *upscaled_y, int orig_w,
    int orig_h, avm_bit_depth_t bit_depth, ResizeFrameSharpCache *cache,
    double *scratch_mse, double *scratch_ac, double *out_mse,
    double *out_ac_ratio) {
  const int bsize = 16;
  const int num_pels = bsize * bsize;
  const uint16_t *const src_y = src->y_buffer;
  const int src_stride = src->y_stride;
  const int max_blocks = (orig_h / bsize) * (orig_w / bsize);

  if (!cache->valid) {
    populate_frame_sharp_cache(src, orig_w, orig_h, bit_depth, cache);
  }

  double *sharp_mse_arr = scratch_mse;
  double *sharp_ac_ratio_arr = scratch_ac;
  int own_scratch = 0;
  if (max_blocks > 0 && (sharp_mse_arr == NULL || sharp_ac_ratio_arr == NULL)) {
    sharp_mse_arr =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*sharp_mse_arr));
    sharp_ac_ratio_arr =
        (double *)avm_malloc((size_t)max_blocks * sizeof(*sharp_ac_ratio_arr));
    own_scratch = 1;
  }

  double all_mse_sum = 0.0;
  int all_count = 0;
  double nz_mse_sum = 0.0;
  double nz_ac_sum = 0.0;
  int nz_count = 0;
  int blk_idx = 0;

  if (cache->valid && cache->block_ac != NULL && cache->block_sharp != NULL) {
    for (int r = 0; r + bsize <= orig_h; r += bsize) {
      for (int c = 0; c + bsize <= orig_w; c += bsize) {
        uint64_t sse = 0;
        for (int br = 0; br < bsize; ++br) {
          const uint16_t *const s_row = src_y + (r + br) * src_stride + c;
          const uint16_t *const u_row = upscaled_y + (r + br) * orig_w + c;
          uint32_t row_sse = 0;
          for (int bc = 0; bc < bsize; ++bc) {
            const int32_t diff = (int32_t)s_row[bc] - (int32_t)u_row[bc];
            row_sse += (uint32_t)(diff * diff);
          }
          sse += row_sse;
        }
        const double block_mse = (double)sse / (double)num_pels;
        all_mse_sum += block_mse;
        ++all_count;
        const double block_sharpness = cache->block_sharp[blk_idx];
        if (block_sharpness >= 0.0) {
          const double block_ac = cache->block_ac[blk_idx];
          const double sharp_mse = block_mse * block_sharpness;
          const double sharp_ac_ratio =
              (block_mse / block_ac) * block_sharpness;
          nz_mse_sum += sharp_mse;
          nz_ac_sum += sharp_ac_ratio;
          if (sharp_mse_arr != NULL && sharp_ac_ratio_arr != NULL) {
            sharp_mse_arr[nz_count] = sharp_mse;
            sharp_ac_ratio_arr[nz_count] = sharp_ac_ratio;
          }
          ++nz_count;
        }
        ++blk_idx;
      }
    }
  }

  if (nz_count > 0) {
    if (sharp_mse_arr != NULL && sharp_ac_ratio_arr != NULL) {
      const int pct_idx =
          (int)(RESIZE_DYNAMIC_BLOCK_PERCENTILE * (double)(nz_count - 1));
      *out_mse = select_kth_double(sharp_mse_arr, nz_count, pct_idx);
      *out_ac_ratio = select_kth_double(sharp_ac_ratio_arr, nz_count, pct_idx);
    } else {
      *out_mse = nz_mse_sum / (double)nz_count;
      *out_ac_ratio = nz_ac_sum / (double)nz_count;
    }
  } else if (all_count > 0) {
    *out_mse = all_mse_sum / (double)all_count;
    *out_ac_ratio = all_mse_sum / AVMMAX(cache->all_ac_sum, 1.0);
  } else {
    *out_mse = 0.0;
    *out_ac_ratio = 0.0;
  }

  if (own_scratch) {
    avm_free(sharp_mse_arr);
    avm_free(sharp_ac_ratio_arr);
  }
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
        const int32_t diff = (int32_t)s_row[c] - (int32_t)u_row[c];
        uv_sse += (uint32_t)(diff * diff);
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
// - out_mc_p50:    median 16x16 MC MSE across sampled blocks,
// - out_mc_nz:     mean 16x16 MC MSE over non-flat blocks,
// - out_mc_acr:    ratio of non-flat MC MSE sum to non-flat AC variance sum,
// - out_mc_p50_nz: (optional) median 16x16 MC MSE over non-flat blocks.
static int compute_pair_mc_stats(const YV12_BUFFER_CONFIG *cur,
                                 const YV12_BUFFER_CONFIG *prv, int orig_w,
                                 int orig_h, avm_bit_depth_t bit_depth,
                                 double *out_mc_p50, double *out_mc_nz,
                                 double *out_mc_acr, double *out_mc_p50_nz) {
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
  double *const nz_mc_arr =
      (out_mc_p50_nz != NULL)
          ? (double *)avm_malloc((size_t)max_blocks * sizeof(*nz_mc_arr))
          : NULL;
  if (out_mc_p50_nz != NULL && nz_mc_arr == NULL) {
    avm_free(mc_arr);
    return 0;
  }

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
      uint16_t cur_sub[64];
      uint32_t zero_sub_sse = 0;
      for (int y = 0; y < bsize; ++y) {
        const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
        const uint16_t *const pr = prv_y + (r + y) * prv_stride + c;
        uint32_t row_sum = 0;
        uint32_t row_sum_sq = 0;
        uint32_t row_zsse = 0;
        for (int x = 0; x < bsize; ++x) {
          const uint32_t v = cr[x];
          const int32_t df = (int32_t)v - (int32_t)pr[x];
          row_sum += v;
          row_sum_sq += v * v;
          row_zsse += (uint32_t)(df * df);
        }
        sum += row_sum;
        sum_sq += row_sum_sq;
        zero_sse += row_zsse;
        if ((y & 1) == 0) {
          uint16_t *const cs_row = cur_sub + ((y >> 1) << 3);
          for (int xi = 0; xi < 8; ++xi) {
            const uint16_t v = cr[xi << 1];
            cs_row[xi] = v;
            const int32_t df = (int32_t)v - (int32_t)pr[xi << 1];
            zero_sub_sse += (uint32_t)(df * df);
          }
        }
      }
      const double block_ac =
          ((double)sum_sq - ((double)sum * (double)sum) / 256.0) / 256.0;

      // Seed coarse search with (0, 0) so early-exit pruning on cs >=
      // best_coarse preserves the exact (0, 0) tie-breaker bit-for-bit.
      int best_dy = 0;
      int best_dx = 0;
      uint32_t best_coarse = zero_sub_sse;
      for (int dy = -sr; dy <= sr; dy += 4) {
        if (r + dy < 0 || r + dy + bsize > orig_h) continue;
        for (int dx = -sr; dx <= sr; dx += 4) {
          if ((dy == 0 && dx == 0) || c + dx < 0 || c + dx + bsize > orig_w) {
            continue;
          }
          uint32_t cs = 0;
          const uint16_t *pr = prv_y + (r + dy) * prv_stride + (c + dx);
          const int prv_stride2 = prv_stride << 1;
          for (int yi = 0; yi < 8; ++yi) {
            const uint16_t *const cr_sub = cur_sub + (yi << 3);
            for (int xi = 0; xi < 8; ++xi) {
              const int32_t df = (int32_t)cr_sub[xi] - (int32_t)pr[xi << 1];
              cs += (uint32_t)(df * df);
            }
            if (cs >= best_coarse) break;
            pr += prv_stride2;
          }
          if (cs < best_coarse) {
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
          uint32_t cs = 0;
          const uint16_t *pr = prv_y + (r + dy) * prv_stride + (c + dx);
          const int prv_stride2 = prv_stride << 1;
          for (int yi = 0; yi < 8; ++yi) {
            const uint16_t *const cr_sub = cur_sub + (yi << 3);
            for (int xi = 0; xi < 8; ++xi) {
              const int32_t df = (int32_t)cr_sub[xi] - (int32_t)pr[xi << 1];
              cs += (uint32_t)(df * df);
            }
            if (cs >= best_coarse) break;
            pr += prv_stride2;
          }
          if (cs < best_coarse) {
            best_coarse = cs;
            best_dy = dy;
            best_dx = dx;
          }
        }
      }

      uint64_t full_sse = zero_sse;
      if (best_dy != 0 || best_dx != 0) {
        uint64_t mv_sse = 0;
        for (int y = 0; y < bsize; ++y) {
          const uint16_t *const cr = cur_y + (r + y) * cur_stride + c;
          const uint16_t *const pr =
              prv_y + (r + best_dy + y) * prv_stride + (c + best_dx);
          uint32_t row_sse = 0;
          for (int x = 0; x < bsize; ++x) {
            const int32_t df = (int32_t)cr[x] - (int32_t)pr[x];
            row_sse += (uint32_t)(df * df);
          }
          mv_sse += row_sse;
          if (mv_sse >= full_sse) break;
        }
        if (mv_sse < full_sse) full_sse = mv_sse;
      }
      const double mc_mse = (double)full_sse / 256.0;
      mc_arr[count++] = mc_mse;
      sum_all += mc_mse;
      if (block_ac > min_block_var) {
        if (nz_mc_arr != NULL) nz_mc_arr[nz_count] = mc_mse;
        sum_nz_mc += mc_mse;
        sum_nz_ac += block_ac;
        ++nz_count;
      }
    }
  }

  if (count <= 0) {
    avm_free(mc_arr);
    avm_free(nz_mc_arr);
    return 0;
  }
  const int p50_idx = (int)(0.50 * (double)(count - 1));
  const int p60_idx = (int)(0.60 * (double)(count - 1));
  *out_mc_p50 = select_kth_double(mc_arr, count, p50_idx);
  const double mc_p60 =
      (p60_idx > p50_idx) ? select_kth_double(mc_arr + p50_idx, count - p50_idx,
                                              p60_idx - p50_idx)
                          : *out_mc_p50;
  *out_mc_nz =
      (nz_count > 0) ? (sum_nz_mc / (double)nz_count) : (sum_all / count);
  *out_mc_acr = (nz_count > 0 && sum_nz_ac > bds2) ? (sum_nz_mc / sum_nz_ac)
                                                   : (mc_p60 / min_block_var);
  if (out_mc_p50_nz != NULL) {
    if (nz_count > 0) {
      *out_mc_p50_nz = select_kth_double(nz_mc_arr, nz_count,
                                         (int)(0.50 * (double)(nz_count - 1)));
    } else {
      *out_mc_p50_nz = *out_mc_p50;
    }
  }
  avm_free(mc_arr);
  avm_free(nz_mc_arr);
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
                                          double pel_ratio, double grain_weight,
                                          int linear_uni,
                                          int bypass_flat_noise_damper,
                                          avm_bit_depth_t bit_depth) {
  const double bds2 = (double)(1 << (2 * ((int)bit_depth - AVM_BITS_8)));
  const double uni = mc_p50 / AVMMAX(mc_nz, bds2);
  const double uni_clamped = AVMMIN(uni, 1.0);
  double grain_scale = 1.0;
  if (!bypass_flat_noise_damper &&
      uni_clamped > RESIZE_DYNAMIC_MC_FLAT_NOISE_UNI0 &&
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
  const double uni_pow =
      linear_uni ? uni_clamped : (uni_clamped * uni_clamped * uni_clamped);
  const double grain_term = grain_weight * mc_p50 * uni_pow * grain_scale;
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

// Relaxes the subgop MC content threshold at moderate scales (d <= 12) on
// coarse-QP Full-HD scenes when sharpness-weighted resampling distortion is a
// small fraction of Qstep^2(d) (qn_base = avg_ratio / base_subgop_thresh <
// q_cap), so motion-blurred scenes with moderate temporal residual energy are
// not blocked by an independent hard MC ratio cutoff. Since the multiplier is
// monotonically non-increasing in avg_ratio, evaluating it at a running partial
// average provides a valid upper bound for early-exit pruning.
static double compute_subgop_synergy_content_thresh(
    double base_content_thresh, double avg_ratio, double base_subgop_thresh,
    double q_cap, double q_ref_num, double max_syn) {
  const double qn_base = avg_ratio / AVMMAX(base_subgop_thresh, 1e-12);
  if (qn_base >= q_cap) return base_content_thresh;
  const double ratio =
      q_ref_num / AVMMAX(qn_base, RESIZE_DYNAMIC_SUBGOP_SYNERGY_QNORM_FLOOR);
  const double syn_mult = AVMMIN(ratio * ratio, max_syn);
  return base_content_thresh * syn_mult;
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
  const int max_blocks = (orig_h / 16) * (orig_w / 16);
  ensure_resize_dyn_scratch_bufs(cpi, pel_count, max_blocks);
  uint16_t *const downscaled_buf = cpi->resize_dyn_downscaled_buf;
  uint16_t *const upscaled_y = cpi->resize_dyn_upscaled_y;
  uint16_t *const upscaled_uv = cpi->resize_dyn_upscaled_uv;
  ResizeFrameSharpCache sharp_cache;
  memset(&sharp_cache, 0, sizeof(sharp_cache));

  avm_clear_system_state();
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double qstep_base = av2_convert_qindex_to_q(qindex, bit_depth) * bds;
  const int qref_qp =
      (is_intra ? RESIZE_DYNAMIC_MINQP_KF : RESIZE_DYNAMIC_QREF) +
      ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  const double qstep_min =
      AVMMAX(av2_convert_qindex_to_q(qref_qp, bit_depth) * bds, 1e-6);
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
                              orig_h, bit_depth, &mc_p50, &mc_nz, &mc_acr,
                              NULL)) {
      mc_eff = compute_mc_effective_energy(
          mc_p50, mc_nz, mc_acr, qstep_base * qstep_base, pel_ratio,
          RESIZE_DYNAMIC_MC_GRAIN_WEIGHT, /*linear_uni=*/0,
          /*bypass_flat_noise_damper=*/0, bit_depth);
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
    const double dec_frac =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);

    resample_plane_roundtrip(unscaled->y_buffer, orig_h, orig_w,
                             unscaled->y_stride, downscaled_buf, scaled_h,
                             scaled_w, upscaled_y, /*subx=*/0, /*suby=*/0, d,
                             bit_depth, /*is_uv=*/0);
    avm_clear_system_state();
    double luma_mse = 0.0;
    double ac_ratio = 0.0;
    compute_nonflat_resample_mse_and_ac_cached(
        unscaled, upscaled_y, orig_w, orig_h, bit_depth, &sharp_cache,
        cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac, &luma_mse,
        &ac_ratio);
    const int luma_content_ok =
        have_mc ? (luma_mse / mc_eff < mc_thresh) : (ac_ratio < ac_thresh_kf);
    const int luma_bg_ok =
        !have_mc || pel_ratio <= 1.0 || (luma_mse < dec_frac * mc_bg);
    if (luma_mse / qstep_sq >= thresh || !luma_content_ok || !luma_bg_ok) {
      break;
    }

    const double uv_mse = compute_resample_chroma_mse(
        unscaled, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth,
        downscaled_buf, upscaled_uv);
    avm_clear_system_state();
    const double dist = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
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

  free_frame_sharp_cache(&sharp_cache);
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
// When from_keyframe is non-zero (called on a closed-GOP KEY_FRAME), evaluates
// the upcoming first inter subgop in gf_group (gf_idx >= 1), sets
// *out_has_subgop = 1 when valid inter subgop frames exist, and sets
// *out_coscale_kf = 1 when co-scaling the keyframe to the same denominator
// avoids cross-scale reference resampling across the GOP without excessive
// intra resampling loss.
static uint8_t calculate_dynamic_subgop_resize_scale(AV2_COMP *cpi,
                                                     int from_keyframe,
                                                     int *out_has_subgop,
                                                     int *out_coscale_kf) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;
  GF_GROUP *const gf_group = &cpi->gf_group;

  if (out_has_subgop != NULL) *out_has_subgop = 0;
  if (out_coscale_kf != NULL) *out_coscale_kf = 0;

  // Never downscale if the unscaled source is unavailable or lossless coding
  // is requested.
  if (unscaled == NULL || is_lossless_requested(rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  const int cur_gf_index = gf_group->index;
  if (from_keyframe &&
      (cur_gf_index != 0 || cpi->no_show_fwd_kf || gf_group->size <= 1)) {
    return SCALE_NUMERATOR;
  }
  const int start_gf_idx = from_keyframe ? (cur_gf_index + 1) : cur_gf_index;

  const YV12_BUFFER_CONFIG *subgop_srcs[MAX_STATIC_GF_GROUP_LENGTH];
  int subgop_qindex[MAX_STATIC_GF_GROUP_LENGTH];
  int num_subgop_frames = 0;

  // Save encoder and rate-control state before querying baseline full-res
  // qindex values across the frames in the target subgop.
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
  const int saved_frames_since_key = cpi->rc.frames_since_key;
  const int saved_last_q_kf = cpi->rc.last_q[KEY_FRAME];
  const int saved_avg_kf_qindex = cpi->rc.avg_frame_qindex[KEY_FRAME];
  const int saved_last_kf_qindex = cpi->rc.last_kf_qindex;
  const int saved_last_boosted_qindex = cpi->rc.last_boosted_qindex;
  int saved_active_best_quality[MAX_ARF_LAYERS + 1];
  memcpy(saved_active_best_quality, cpi->rc.active_best_quality,
         sizeof(saved_active_best_quality));

  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;
  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int max_qp = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth);
  int has_lossless_frame = 0;
  int max_subgop_qindex = 0;
  int min_la_idx = INT_MAX;
  int max_la_idx = -1;

  if (from_keyframe) {
    if (is_stat_consumption_stage(cpi) &&
        (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
      av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
    }
    int kf_bottom = 0, kf_top = 0;
    const int kf_qindex =
        cm->bridge_frame_info.is_bridge_frame
            ? cm->quant_params.base_qindex
            : av2_rc_pick_q_and_bounds(cpi, &cpi->rc, frm_dim_cfg->width,
                                       frm_dim_cfg->height, cur_gf_index,
                                       &kf_bottom, &kf_top);
    if (kf_qindex == 0) {
      has_lossless_frame = 1;
    } else {
      cpi->rc.frames_since_key = 1;
      cpi->rc.level1_qp = -1;
      cpi->rc.last_q[KEY_FRAME] = kf_qindex;
      cpi->rc.avg_frame_qindex[KEY_FRAME] = ROUND_POWER_OF_TWO(
          3 * cpi->rc.avg_frame_qindex[KEY_FRAME] + kf_qindex, 2);
      cpi->rc.last_kf_qindex = kf_qindex;
      cpi->rc.last_boosted_qindex = kf_qindex;
      for (int i = 0; i <= MAX_ARF_LAYERS; ++i) {
        cpi->rc.active_best_quality[i] = kf_qindex;
      }
    }
  }

  // Collect each coded frame in the target subgop along with its intended
  // full-resolution pyramid-level base_qindex.
  for (int gf_idx = start_gf_idx;
       !has_lossless_frame && gf_idx < gf_group->size; ++gf_idx) {
    // Skip pure overlay updates since they do not code a new source picture.
    const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_idx];
    if (update_type == OVERLAY_UPDATE || update_type == INTNL_OVERLAY_UPDATE ||
        update_type == KFFLT_OVERLAY_UPDATE) {
      continue;
    }

    const int frame_display_index =
        gf_group->cur_frame_idx[gf_idx] + gf_group->arf_src_offset[gf_idx];
    const int lookahead_idx = frame_display_index - base_disp_idx;
    if (lookahead_idx >= 0 && lookahead_idx < min_la_idx) {
      min_la_idx = lookahead_idx;
    }
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
  cpi->rc.frames_since_key = saved_frames_since_key;
  cpi->rc.last_q[KEY_FRAME] = saved_last_q_kf;
  cpi->rc.avg_frame_qindex[KEY_FRAME] = saved_avg_kf_qindex;
  cpi->rc.last_kf_qindex = saved_last_kf_qindex;
  cpi->rc.last_boosted_qindex = saved_last_boosted_qindex;
  memcpy(cpi->rc.active_best_quality, saved_active_best_quality,
         sizeof(saved_active_best_quality));

  // Keep full resolution if any frame in the subgop is lossless (qindex == 0).
  if (has_lossless_frame) {
    return SCALE_NUMERATOR;
  }
  if (num_subgop_frames == 0) {
    return from_keyframe ? SCALE_NUMERATOR
                         : calculate_dynamic_resize_scale(cpi);
  }
  if (out_has_subgop != NULL) *out_has_subgop = 1;
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
  const int max_blocks = (orig_h / 16) * (orig_w / 16);
  ensure_resize_dyn_scratch_bufs(cpi, pel_count, max_blocks);
  uint16_t *const downscaled_buf = cpi->resize_dyn_downscaled_buf;
  uint16_t *const upscaled_y = cpi->resize_dyn_upscaled_y;
  uint16_t *const upscaled_uv = cpi->resize_dyn_upscaled_uv;
  ResizeFrameSharpCache sharp_caches[MAX_STATIC_GF_GROUP_LENGTH];
  memset(sharp_caches, 0, sizeof(sharp_caches));

  avm_clear_system_state();
  const double pel_ratio = get_resize_pel_ratio(orig_w, orig_h);
  const double lin_scale = (pel_ratio > 1.0) ? sqrt(pel_ratio) : 1.0;
  const double base_subgop_thresh = RESIZE_DYNAMIC_SUBGOP_THRESH * pel_ratio;
  double subgop_thresh = base_subgop_thresh;
  const int qp_offset = ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  const int base_qref_qp = RESIZE_DYNAMIC_QREF + qp_offset;
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double bds2 = bds * bds;
  const double qstep_subgop =
      av2_convert_qindex_to_q(max_subgop_qindex, bit_depth) * bds;

  // Estimate subgop temporal MC coding energy from up to 4 evenly-spaced
  // consecutive raw frame pairs across the subgop's display span.
  double sum_mc_p50 = 0.0, sum_mc_nz = 0.0, sum_mc_acr = 0.0;
  int num_mc_pairs = 0;
  const int la_base = (from_keyframe && min_la_idx < INT_MAX && min_la_idx > 0)
                          ? min_la_idx
                          : 0;
  const int la_span = max_la_idx - la_base;
  if (la_span >= 1) {
    const int target_pairs = AVMMIN(4, la_span);
    int prev_idx = -1;
    for (int p = 0; p < target_pairs; ++p) {
      const int idx =
          (target_pairs == 1) ? 0 : (p * (la_span - 1)) / (target_pairs - 1);
      if (idx == prev_idx) continue;
      prev_idx = idx;
      const struct lookahead_entry *const e0 = av2_lookahead_peek(
          cpi->lookahead, la_base + idx, cpi->compressor_stage);
      const struct lookahead_entry *const e1 = av2_lookahead_peek(
          cpi->lookahead, la_base + idx + 1, cpi->compressor_stage);
      double p50 = 0.0, nz = 0.0, acr = 0.0;
      if (e0 != NULL && e1 != NULL &&
          compute_pair_mc_stats(&e1->img, &e0->img, orig_w, orig_h, bit_depth,
                                &p50, &nz, &acr, NULL)) {
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
        av2_lookahead_peek(cpi->lookahead, la_base, cpi->compressor_stage);
    double p50 = 0.0, nz = 0.0, acr = 0.0;
    if (e0 != NULL && compute_pair_mc_stats(&e0->img, unscaled, orig_w, orig_h,
                                            bit_depth, &p50, &nz, &acr, NULL)) {
      sum_mc_p50 += p50;
      sum_mc_nz += nz;
      sum_mc_acr += acr;
      ++num_mc_pairs;
    }
  }
  if (num_mc_pairs == 0 && !from_keyframe) {
    double p50 = 0.0, nz = 0.0, acr = 0.0;
    if (compute_pair_mc_stats(unscaled, cpi->unscaled_last_source, orig_w,
                              orig_h, bit_depth, &p50, &nz, &acr, NULL)) {
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
      have_mc ? AVMMIN(mc_p50 / AVMMAX(mc_nz, bds2), 1.0) : 0.0;

  const uint8_t prev_denom = from_keyframe ? 0 : cpi->resize_prev_subgop_denom;
  const int is_highres_pan =
      have_mc && pel_ratio > 1.0 &&
      mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_MIN * bds2 &&
      (mc_uni + 2.0 * mc_acr > RESIZE_DYNAMIC_HIGHRES_PAN_MOTION_MIN ||
       mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_STRONG * bds2);
  const int qref_qp = is_highres_pan
                          ? (RESIZE_DYNAMIC_HIGHRES_PAN_QREF + qp_offset)
                          : base_qref_qp;
  const double qstep_min =
      AVMMAX(av2_convert_qindex_to_q(qref_qp, bit_depth) * bds, 1e-6);
  const double qstep_ratio = qstep_subgop / qstep_min;
  const double qstep_ratio_sq = qstep_ratio * qstep_ratio;
  const double mc_q_scale =
      (pel_ratio > 1.0) ? (lin_scale * pow(qstep_ratio, 2.0 * lin_scale))
                        : qstep_ratio_sq;
  double subgop_mc_thresh = AVMMIN(RESIZE_DYNAMIC_SUBGOP_MC_THRESH * mc_q_scale,
                                   RESIZE_DYNAMIC_MC_RATIO_MAX);
  const double subgop_ac_thresh_fallback =
      RESIZE_DYNAMIC_AC_THRESH_KF * qstep_ratio_sq;
  // At coarse QPs (qstep_ratio_sq > 4.0) on <= 1080p scenes whose temporal MC
  // residual is spatially pervasive (mc_uni > 0.65) and substantial (mc_p50 >
  // 15 * bds^2) despite low structured motion (mc_acr < 0.30), round-trip
  // resampling MSE is dominated by attenuated uncorrelated sensor/shadow noise
  // rather than lost scene structure. Now that closed-GOP keyframes co-scale
  // with the subgop, treat the pervasive noise floor as incompressible temporal
  // coding energy and relax subgop_thresh accordingly.
  const int is_uniform_noise =
      have_mc && pel_ratio <= 1.0 &&
      mc_uni > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_UNI_MIN &&
      mc_acr < RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX &&
      mc_p50 > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_P50_MIN * bds2 &&
      qstep_ratio_sq > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_MIN_QRAT_SQ;
  const double grain_weight =
      is_highres_pan
          ? RESIZE_DYNAMIC_MC_PAN_GRAIN_WEIGHT
          : (is_uniform_noise ? RESIZE_DYNAMIC_MC_UNIFORM_NOISE_GRAIN_WEIGHT
                              : RESIZE_DYNAMIC_MC_GRAIN_WEIGHT);
  const double mc_eff =
      have_mc ? compute_mc_effective_energy(
                    mc_p50, mc_nz, mc_acr, qstep_subgop * qstep_subgop,
                    pel_ratio, grain_weight, is_highres_pan || is_uniform_noise,
                    is_uniform_noise, bit_depth)
              : 1.0;
  const double mc_bg =
      have_mc ? compute_mc_bg_energy(mc_p50, mc_nz, mc_acr) : 1.0;

  double hyst_step = (pel_ratio > 1.0 && !is_highres_pan)
                         ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_STEP, pel_ratio)
                         : RESIZE_DYNAMIC_SUBGOP_HYST_STEP;
  const double coarse_keep_ramp =
      AVMMIN(AVMMAX(qstep_ratio_sq - 6.0, 0.0) / 10.0, 1.0);
  double hyst_keep =
      (pel_ratio > 1.0)
          ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_KEEP, pel_ratio)
          : (RESIZE_DYNAMIC_SUBGOP_HYST_KEEP +
             RESIZE_DYNAMIC_SUBGOP_HYST_KEEP_COARSE_ADD * coarse_keep_ramp);
  if (is_uniform_noise) {
    subgop_thresh *= RESIZE_DYNAMIC_SUBGOP_UNIFORM_NOISE_THRESH_BOOST;
  } else if (have_mc && pel_ratio <= 1.0) {
    if (mc_acr < RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI) {
      // When temporal motion compensation removes > 91% of block AC variance
      // across distributed frame motion, attenuate subgop_mc_thresh cubically
      // so coarse-QP qstep_ratio_sq scaling does not trigger lossy downscaling
      // on temporally predictable panning scenes.
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF;
      subgop_mc_thresh *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr < RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_MAX &&
               mc_uni <= RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI) {
      // On stationary backgrounds (very low median-to-active MC ratio) with
      // predictable localized motion (low mc_acr), attenuate subgop_mc_thresh
      // cubically so localized foreground motion does not downscale crisp
      // static scene detail at coarse QPs.
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_REF;
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
  const int allow_synergy =
      have_mc && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
      pel_ratio <= 1.0 && !is_uniform_noise &&
      qstep_ratio_sq > RESIZE_DYNAMIC_SUBGOP_SYNERGY_MIN_QRAT_SQ &&
      ((mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID &&
        mc_acr > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX &&
        mc_nz < RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ * bds2) ||
       (mc_acr > RESIZE_DYNAMIC_SUBGOP_SYNERGY_ACR_MID &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MIN));
  const int is_very_coarse_qp =
      (qstep_ratio_sq > RESIZE_DYNAMIC_SUBGOP_SYNERGY_HIGH_QRAT_SQ);
  const double syn_q_cap =
      is_very_coarse_qp
          ? ((mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID)
                 ? RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_HIGH_UNI
                 : RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_HIGH_LOW_UNI)
          : ((mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID)
                 ? RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_MID_UNI
                 : RESIZE_DYNAMIC_SUBGOP_SYNERGY_QCAP_MID_LOW_UNI);
  const double syn_q_ref = is_very_coarse_qp
                               ? RESIZE_DYNAMIC_SUBGOP_SYNERGY_QREF_HIGH
                               : RESIZE_DYNAMIC_SUBGOP_SYNERGY_QREF_MID;
  const double syn_max = is_very_coarse_qp
                             ? RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_HIGH
                             : RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_MID;
  double avg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double content_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double bg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double eff_content_thresh_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  int max_eval_d = SCALE_NUMERATOR;
  int still_contiguous = 1;
  uint8_t raw_best = SCALE_NUMERATOR;

  int total_valid_frames = 0;
  for (int f = 0; f < num_subgop_frames; ++f) {
    const YV12_BUFFER_CONFIG *const src = subgop_srcs[f];
    if (src->y_crop_width == orig_w && src->y_crop_height == orig_h) {
      ++total_valid_frames;
    }
  }

  for (int d = SCALE_NUMERATOR + 1;
       total_valid_frames > 0 && d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    const int use_syn_d =
        allow_synergy && d <= RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_DENOM;
    const double dec_frac =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);
    // All terms accumulated across the subgop are non-negative, so dividing
    // the running sum by total_valid_frames forms a monotonic lower bound on
    // the full-subgop average. Once any metric reaches the maximum active
    // threshold multiplier for denominator d (1.0 when only contiguous pass is
    // relevant, or max(1.0, hyst_keep) when d <= prev_denom can be held via
    // hysteresis), the remaining frames cannot prevent d from failing.
    const double max_mult =
        (prev_denom > SCALE_NUMERATOR && d <= (int)prev_denom)
            ? AVMMAX(1.0, hyst_keep)
            : 1.0;

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
      avm_clear_system_state();
      double luma_mse = 0.0;
      double ac_ratio = 0.0;
      compute_nonflat_resample_mse_and_ac_cached(
          src, upscaled_y, orig_w, orig_h, bit_depth, &sharp_caches[f],
          cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac, &luma_mse,
          &ac_ratio);
      const int qindex_d =
          clamp(subgop_qindex[f] + av2_get_resize_qp_offset(d), 1, max_qp);
      const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
      const double qstep_sq = qstep * qstep;
      sum_ac_ratio += ac_ratio;

      const double luma_avg_ratio =
          (sum_ratio + luma_mse / qstep_sq) / (double)total_valid_frames;
      const double luma_avg_dist =
          (sum_dist + luma_mse) / (double)total_valid_frames;
      const double cur_avg_ac_ratio = sum_ac_ratio / (double)total_valid_frames;
      const double luma_content_ratio =
          have_mc ? (luma_avg_dist / mc_eff) : cur_avg_ac_ratio;
      const double luma_bg_ratio = (have_mc && pel_ratio > 1.0)
                                       ? (luma_avg_dist / (dec_frac * mc_bg))
                                       : 0.0;
      const double luma_content_thresh_ub =
          use_syn_d ? compute_subgop_synergy_content_thresh(
                          content_thresh, luma_avg_ratio, base_subgop_thresh,
                          syn_q_cap, syn_q_ref, syn_max)
                    : content_thresh;
      if (luma_avg_ratio >= max_mult * subgop_thresh ||
          luma_content_ratio >= max_mult * luma_content_thresh_ub ||
          luma_bg_ratio >= max_mult) {
        sum_ratio += luma_mse / qstep_sq;
        sum_dist += luma_mse;
        ++valid_frames;
        break;
      }

      const double uv_mse = compute_resample_chroma_mse(
          src, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth, downscaled_buf,
          upscaled_uv);
      avm_clear_system_state();
      const double dist = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
      sum_ratio += dist / qstep_sq;
      sum_dist += dist;
      ++valid_frames;
      if (valid_frames < total_valid_frames) {
        const double cur_avg_ratio = sum_ratio / (double)total_valid_frames;
        const double cur_avg_dist = sum_dist / (double)total_valid_frames;
        const double cur_content_ratio =
            have_mc ? (cur_avg_dist / mc_eff) : cur_avg_ac_ratio;
        const double cur_bg_ratio = (have_mc && pel_ratio > 1.0)
                                        ? (cur_avg_dist / (dec_frac * mc_bg))
                                        : 0.0;
        const double cur_content_thresh_ub =
            use_syn_d ? compute_subgop_synergy_content_thresh(
                            content_thresh, cur_avg_ratio, base_subgop_thresh,
                            syn_q_cap, syn_q_ref, syn_max)
                      : content_thresh;
        if (cur_avg_ratio >= max_mult * subgop_thresh ||
            cur_content_ratio >= max_mult * cur_content_thresh_ub ||
            cur_bg_ratio >= max_mult) {
          break;
        }
      }
    }

    const double avg_ratio = sum_ratio / (double)total_valid_frames;
    const double avg_dist = sum_dist / (double)total_valid_frames;
    const double avg_ac_ratio = sum_ac_ratio / (double)total_valid_frames;
    const double content_ratio = have_mc ? (avg_dist / mc_eff) : avg_ac_ratio;
    const double bg_ratio =
        (have_mc && pel_ratio > 1.0) ? (avg_dist / (dec_frac * mc_bg)) : 0.0;
    const double eff_content_thresh =
        use_syn_d ? compute_subgop_synergy_content_thresh(
                        content_thresh, avg_ratio, base_subgop_thresh,
                        syn_q_cap, syn_q_ref, syn_max)
                  : content_thresh;
    avg_ratio_arr[d] = avg_ratio;
    content_ratio_arr[d] = content_ratio;
    bg_ratio_arr[d] = bg_ratio;
    eff_content_thresh_arr[d] = eff_content_thresh;
    max_eval_d = d;

    if (still_contiguous && avg_ratio < subgop_thresh &&
        content_ratio < eff_content_thresh && bg_ratio < 1.0) {
      raw_best = (uint8_t)d;
    } else {
      still_contiguous = 0;
      // Continue evaluating up to prev_denom only while holding prev_denom via
      // hyst_keep remains possible.
      if (prev_denom <= SCALE_NUMERATOR || d >= prev_denom ||
          avg_ratio >= hyst_keep * subgop_thresh ||
          content_ratio >= hyst_keep * eff_content_thresh ||
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
          content_ratio_arr[d_next] <
              hyst_step * eff_content_thresh_arr[d_next] &&
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
            content_ratio_arr[d] >= hyst_keep * eff_content_thresh_arr[d] ||
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
  // - on high-resolution sources (>= 2x 1080p pixel count),
  // - below the baseline reference qindex base_qref_qp (requiring at least
  //   d >= 10 when downscaling at finer QPs), or
  // - entering d = 9 from a different denominator (prev_denom != 9) when d = 9
  //   already consumes a non-trivial share of both temporal MC coding energy
  //   (>= RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO) and quantization
  //   distortion budget (>= RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO *
  //   base_subgop_thresh), where cross-scale Lanczos-5 reference resampling
  //   overhead and disabling same-resolution TIP_FRAME_DIRECT / MFMV outweigh
  //   an 11% linear scale reduction unless at least d >= 10 (64% pixels) is
  //   selected.
  if (best_denom == SCALE_NUMERATOR + 1 &&
      (pel_ratio >= RESIZE_DYNAMIC_HIGHRES_PEL_RATIO ||
       max_subgop_qindex < base_qref_qp ||
       (prev_denom != SCALE_NUMERATOR + 1 &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
        content_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO &&
        avg_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO * base_subgop_thresh))) {
    best_denom = SCALE_NUMERATOR;
  }

  if (out_coscale_kf != NULL) {
    *out_coscale_kf =
        (best_denom >= RESIZE_DYNAMIC_KF_COSCALE_MIN_DENOM &&
         (pel_ratio <= 1.0 || is_highres_pan) &&
         (is_uniform_noise ||
          avg_ratio_arr[best_denom] <
              RESIZE_DYNAMIC_KF_COSCALE_MAX_Q_NORM * base_subgop_thresh));
  }

  for (int f = 0; f < num_subgop_frames; ++f) {
    free_frame_sharp_cache(&sharp_caches[f]);
  }
  return best_denom;
}

// Averages consecutive-pair MC statistics while trimming isolated hard
// scene-cut residual spikes when at least 3 pairs are sampled, preventing a
// single cross-shot boundary pair from inflating within-shot MC energy.
static void average_trimmed_mc_pairs(
    const double *p50_arr, const double *p50_nz_arr, const double *nz_arr,
    const double *acr_arr, int num_pairs, double bds2, double *out_p50,
    double *out_p50_nz, double *out_nz, double *out_acr) {
  if (num_pairs <= 0) {
    *out_p50 = 0.0;
    *out_p50_nz = 0.0;
    *out_nz = 0.0;
    *out_acr = 0.0;
    return;
  }
  double med_nz = nz_arr[0];
  if (num_pairs >= 3) {
    double sorted_nz[8];
    const int n_copy = AVMMIN(num_pairs, 8);
    memcpy(sorted_nz, nz_arr, (size_t)n_copy * sizeof(*sorted_nz));
    qsort(sorted_nz, (size_t)n_copy, sizeof(*sorted_nz), cmp_double);
    med_nz = sorted_nz[n_copy / 2];
  }
  const double cut_nz_thresh = AVMMAX(RESIZE_DYNAMIC_CUT_NZ_RATIO * med_nz,
                                      RESIZE_DYNAMIC_CUT_NZ_MIN * bds2);
  double sum_p50 = 0.0, sum_p50_nz = 0.0, sum_nz = 0.0, sum_acr = 0.0;
  int kept = 0;
  for (int i = 0; i < num_pairs; ++i) {
    if (num_pairs >= 3 && nz_arr[i] > cut_nz_thresh &&
        acr_arr[i] > RESIZE_DYNAMIC_CUT_ACR_MIN) {
      continue;
    }
    sum_p50 += p50_arr[i];
    sum_p50_nz += p50_nz_arr[i];
    sum_nz += nz_arr[i];
    sum_acr += acr_arr[i];
    ++kept;
  }
  if (kept == 0) {
    for (int i = 0; i < num_pairs; ++i) {
      sum_p50 += p50_arr[i];
      sum_p50_nz += p50_nz_arr[i];
      sum_nz += nz_arr[i];
      sum_acr += acr_arr[i];
    }
    kept = num_pairs;
  }
  *out_p50 = sum_p50 / (double)kept;
  *out_p50_nz = sum_p50_nz / (double)kept;
  *out_nz = sum_nz / (double)kept;
  *out_acr = sum_acr / (double)kept;
}

// Samples up to 4 evenly spaced consecutive lookahead frame pairs across
// [la_start .. la_end] and returns scene-cut-trimmed mean MC statistics.
static int sample_lookahead_mc_range(const AV2_COMP *cpi, int la_start,
                                     int la_end, int orig_w, int orig_h,
                                     avm_bit_depth_t bit_depth, double bds2,
                                     double *out_p50, double *out_p50_nz,
                                     double *out_nz, double *out_acr) {
  const int span = la_end - la_start;
  if (span < 1 || cpi->lookahead == NULL) return 0;
  const int target_pairs = AVMMIN(4, span);
  double p50_arr[4], p50_nz_arr[4], nz_arr[4], acr_arr[4];
  int num_pairs = 0;
  int prev_idx = -1;
  for (int p = 0; p < target_pairs; ++p) {
    const int idx =
        (target_pairs == 1) ? 0 : (p * (span - 1)) / (target_pairs - 1);
    if (idx == prev_idx) continue;
    prev_idx = idx;
    const struct lookahead_entry *const e0 = av2_lookahead_peek(
        cpi->lookahead, la_start + idx, cpi->compressor_stage);
    const struct lookahead_entry *const e1 = av2_lookahead_peek(
        cpi->lookahead, la_start + idx + 1, cpi->compressor_stage);
    double p50 = 0.0, p50_nz = 0.0, nz = 0.0, acr = 0.0;
    if (e0 != NULL && e1 != NULL &&
        compute_pair_mc_stats(&e1->img, &e0->img, orig_w, orig_h, bit_depth,
                              &p50, &nz, &acr, &p50_nz)) {
      p50_arr[num_pairs] = p50;
      p50_nz_arr[num_pairs] = p50_nz;
      nz_arr[num_pairs] = nz;
      acr_arr[num_pairs] = acr;
      ++num_pairs;
    }
  }
  if (num_pairs == 0) return 0;
  average_trimmed_mc_pairs(p50_arr, p50_nz_arr, nz_arr, acr_arr, num_pairs,
                           bds2, out_p50, out_p50_nz, out_nz, out_acr);
  return 1;
}

// Selects the optimal resize denominator d in [8..max_eval_d] for a segment or
// causal frame in RESIZE_DYNAMIC mode using the calibrated content-regime model
// and Schmitt-trigger hysteresis relative to prev_denom.
static uint8_t evaluate_calibrated_resize_segment(
    const double *avg_dist_arr, const double *avg_ratio_arr,
    const double *avg_ac_arr, int max_eval_d, int max_qindex, int have_mc,
    double mc_p50, double mc_p50_nz, double mc_nz, double mc_acr, int orig_w,
    int orig_h, avm_bit_depth_t bit_depth, uint8_t prev_denom,
    int *out_allow_highres_coscale, int *out_is_uniform_noise,
    double *out_kf_q_thresh) {
  const size_t pel_count = (size_t)orig_w * (size_t)orig_h;
  const double pel_ratio = get_resize_pel_ratio(orig_w, orig_h);
  const double lin_scale = (pel_ratio > 1.0) ? sqrt(pel_ratio) : 1.0;
  const double base_thresh = RESIZE_DYNAMIC_SUBGOP_THRESH * pel_ratio;
  double seg_thresh = base_thresh;
  const int qp_offset = ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  const int base_qref_qp = RESIZE_DYNAMIC_QREF + qp_offset;
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double bds2 = bds * bds;
  const double qstep_ref = av2_convert_qindex_to_q(max_qindex, bit_depth) * bds;
  const double mc_uni =
      have_mc ? AVMMIN(mc_p50 / AVMMAX(mc_nz, bds2), 1.0) : 0.0;
  // Optical spectral rolloff between d=10 and d=12: soft/band-limited optics
  // have gentle distortion growth (< 2.42), whereas sharp lenses have a steep
  // cliff (>= 2.50).
  const double rolloff =
      (max_eval_d >= SCALE_NUMERATOR + 4 &&
       avg_dist_arr[SCALE_NUMERATOR + 2] > 1e-6 &&
       avg_dist_arr[SCALE_NUMERATOR + 4] > avg_dist_arr[SCALE_NUMERATOR + 2])
          ? (avg_dist_arr[SCALE_NUMERATOR + 4] /
             avg_dist_arr[SCALE_NUMERATOR + 2])
          : 3.0;

  const int is_highres_pan =
      have_mc && pel_ratio > 1.0 &&
      mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_MIN * bds2 &&
      (mc_uni + 2.0 * mc_acr > RESIZE_DYNAMIC_HIGHRES_PAN_MOTION_MIN ||
       mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_STRONG * bds2);
  const int qref_qp = is_highres_pan
                          ? (RESIZE_DYNAMIC_HIGHRES_PAN_QREF + qp_offset)
                          : base_qref_qp;
  const double qstep_min =
      AVMMAX(av2_convert_qindex_to_q(qref_qp, bit_depth) * bds, 1e-6);
  const double qstep_ratio = qstep_ref / qstep_min;
  const double qstep_ratio_sq = qstep_ratio * qstep_ratio;
  const double mc_q_scale =
      (pel_ratio > 1.0) ? (lin_scale * pow(qstep_ratio, 2.0 * lin_scale))
                        : qstep_ratio_sq;
  double seg_mc_thresh = AVMMIN(RESIZE_DYNAMIC_SUBGOP_MC_THRESH * mc_q_scale,
                                RESIZE_DYNAMIC_MC_RATIO_MAX);
  const double ac_thresh_fallback =
      RESIZE_DYNAMIC_AC_THRESH_KF * qstep_ratio_sq;

  // Regime 1: Pervasive uniform sensor/shadow noise across <= 1080p frames.
  const int is_uniform_noise =
      have_mc && pel_ratio <= 1.0 &&
      mc_uni > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_UNI_MIN &&
      mc_acr < RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX &&
      mc_p50 > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_P50_MIN * bds2 &&
      qstep_ratio_sq > RESIZE_DYN_UNIFORM_NOISE_MIN_QRAT_SQ;
  const double grain_weight =
      is_highres_pan ? RESIZE_DYNAMIC_MC_PAN_GRAIN_WEIGHT
                     : (is_uniform_noise ? RESIZE_DYN_UNIFORM_NOISE_GRAIN_WEIGHT
                                         : RESIZE_DYNAMIC_MC_GRAIN_WEIGHT);
  const double mc_eff =
      have_mc ? compute_mc_effective_energy(
                    mc_p50, mc_nz, mc_acr, qstep_ref * qstep_ref, pel_ratio,
                    grain_weight, is_highres_pan || is_uniform_noise,
                    is_uniform_noise, bit_depth)
              : 1.0;
  const double mc_bg =
      have_mc ? compute_mc_bg_energy(mc_p50, mc_nz, mc_acr) : 1.0;

  double hyst_step = (pel_ratio > 1.0 && !is_highres_pan)
                         ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_STEP, pel_ratio)
                         : RESIZE_DYNAMIC_SUBGOP_HYST_STEP;
  const double coarse_keep_ramp =
      AVMMIN(AVMMAX(qstep_ratio_sq - 6.0, 0.0) / 10.0, 1.0);
  double hyst_keep =
      (pel_ratio > 1.0)
          ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_KEEP, pel_ratio)
          : (RESIZE_DYNAMIC_SUBGOP_HYST_KEEP +
             RESIZE_DYNAMIC_SUBGOP_HYST_KEEP_COARSE_ADD * coarse_keep_ramp);

  // Detect Full-HD scenes dominated by flat regions (collapsing all-block
  // mc_p50 / mc_uni) whose non-flat textured blocks nevertheless exhibit strong
  // distributed temporal motion (high mc_p50_nz and mc_nz).
  const double flat_bg_acr_min =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_FLAT_BG_HOLD_ACR_MIN
          : RESIZE_DYN_FLAT_BG_ACR_MIN;
  const double flat_bg_nz_min =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_FLAT_BG_HOLD_NZ_MIN
          : RESIZE_DYN_FLAT_BG_NZ_MIN;
  const int is_flat_bg_active =
      have_mc && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
      pel_ratio <= 1.0 && !is_uniform_noise &&
      mc_uni < RESIZE_DYN_FLAT_BG_UNI_MAX &&
      mc_p50_nz > RESIZE_DYN_FLAT_BG_NZ_P50_MIN * bds2 &&
      mc_nz > flat_bg_nz_min * bds2 && mc_acr > flat_bg_acr_min &&
      rolloff < RESIZE_DYN_FLAT_BG_ROLLOFF_MAX;

  int high_acr_smooth = 0;
  int hr_high_acr = 0;
  if (is_uniform_noise) {
    seg_thresh *= RESIZE_DYN_UNIFORM_NOISE_THRESH_BOOST;
    seg_mc_thresh = AVMMIN(seg_mc_thresh * RESIZE_DYN_UNIFORM_NOISE_MC_BOOST,
                           RESIZE_DYNAMIC_MC_RATIO_MAX);
  } else if (have_mc && pel_ratio <= 1.0) {
    if (mc_acr < RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI && !is_flat_bg_active) {
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF;
      seg_mc_thresh *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr < RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_MAX &&
               mc_uni <= RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
               !is_flat_bg_active) {
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_REF;
      seg_mc_thresh *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN) {
      if (mc_acr > 1.0 && mc_uni > 0.35) {
        hyst_step = RESIZE_DYNAMIC_SUBGOP_EXTREME_ACR_HYST_STEP;
      } else if ((mc_uni > 0.20 && mc_uni < 0.48) ||
                 (mc_acr > 0.70 && mc_uni > 0.04)) {
        hyst_step = RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_STEP;
        hyst_keep = RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_HYST_KEEP;
        if ((double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
            rolloff < RESIZE_DYN_SMOOTH_ROLLOFF_MAX &&
            mc_p50 > RESIZE_DYN_HIGH_ACR_SMOOTH_P50_MIN * bds2) {
          // Under high temporal unpredictability on band-limited 1080p optics,
          // marginal rate savings from decimation are large across q185..q235.
          high_acr_smooth = 1;
          seg_mc_thresh =
              AVMMIN(seg_mc_thresh * RESIZE_DYN_HIGH_ACR_SMOOTH_MC_BOOST,
                     RESIZE_DYNAMIC_MC_RATIO_MAX);
          const double q_boost =
              (qstep_ratio_sq > RESIZE_DYNAMIC_SUBGOP_SYNERGY_HIGH_QRAT_SQ)
                  ? RESIZE_DYN_HIGH_ACR_SMOOTH_Q_BOOST_COARSE
                  : RESIZE_DYN_HIGH_ACR_SMOOTH_Q_BOOST_MID;
          seg_thresh *= q_boost;
        } else {
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
          seg_thresh *= q_boost;
        }
      }
    }
  } else if (have_mc && pel_ratio > 1.0 &&
             mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN) {
    // High-ACR >1080p content (still guarded by background-safe bg_ratio < 1):
    // relax quantization distortion budget for temporally unpredictable action.
    hr_high_acr = 1;
    seg_thresh *= RESIZE_DYN_HIGHRES_HIGH_ACR_Q_BOOST;
  }

  // Regime 2: Full-HD spatial-smoothness / MC synergy with Schmitt hysteresis
  // on mc_acr when holding prev_denom >= 10.
  const double syn_low_acr = (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
                                 ? RESIZE_DYN_SYNERGY_HOLD_LOW_ACR
                                 : RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX;
  const double syn_mid_acr = (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
                                 ? RESIZE_DYN_SYNERGY_HOLD_MID_ACR
                                 : RESIZE_DYNAMIC_SUBGOP_SYNERGY_ACR_MID;
  const int allow_synergy =
      have_mc && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
      pel_ratio <= 1.0 && !is_uniform_noise &&
      qstep_ratio_sq > RESIZE_DYN_SYNERGY_MIN_QRAT_SQ &&
      (((mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID &&
         mc_acr > syn_low_acr &&
         mc_nz < RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ * bds2) ||
        (mc_acr > syn_mid_acr &&
         mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MIN)) ||
       is_flat_bg_active);
  if (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM && pel_ratio <= 1.0 &&
      (allow_synergy ||
       (have_mc && mc_acr > RESIZE_DYN_SYNERGY_HOLD_ACR_MIN))) {
    hyst_keep = AVMMAX(hyst_keep, RESIZE_DYN_SYNERGY_HOLD_HYST_KEEP);
  }

  // Regime 3: Moderate-scale (d <= 10) allowance under extreme temporal
  // unpredictability (e.g. dense water ripples / foliage motion / fast action).
  const int is_extreme_mc = have_mc && pel_ratio <= 1.0 &&
                            mc_acr > RESIZE_DYN_EXTREME_MC_ACR_MIN &&
                            mc_p50 > RESIZE_DYN_EXTREME_MC_P50_MIN * bds2;

  const double base_content_thresh =
      have_mc ? seg_mc_thresh : ac_thresh_fallback;
  const int is_very_coarse_qp =
      (qstep_ratio_sq > RESIZE_DYNAMIC_SUBGOP_SYNERGY_HIGH_QRAT_SQ);
  const int has_mid_uni =
      (mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID || is_flat_bg_active);
  double syn_q_cap = is_very_coarse_qp
                         ? (has_mid_uni ? RESIZE_DYN_SYNERGY_QCAP_HIGH_UNI
                                        : RESIZE_DYN_SYNERGY_QCAP_HIGH_LOW_UNI)
                         : (has_mid_uni ? RESIZE_DYN_SYNERGY_QCAP_MID_UNI
                                        : RESIZE_DYN_SYNERGY_QCAP_MID_LOW_UNI);
  double syn_q_ref = is_very_coarse_qp ? RESIZE_DYN_SYNERGY_QREF_HIGH
                                       : RESIZE_DYN_SYNERGY_QREF_MID;
  double syn_max = is_very_coarse_qp ? RESIZE_DYN_SYNERGY_MAX_HIGH
                                     : RESIZE_DYN_SYNERGY_MAX_MID;
  int syn_max_denom = RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_DENOM;

  // On band-limited 1080p optics (gentle d12/d10 rolloff) or flat-dominated
  // active-foreground 1080p scenes, extend synergy depth and Q budget cleanly
  // while keeping steep-rolloff sharp-lens scenes untouched.
  const int is_smooth_1080_syn =
      allow_synergy && !high_acr_smooth &&
      ((mc_uni > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID &&
        mc_nz < RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ * bds2 &&
        rolloff < RESIZE_DYN_SMOOTH_ROLLOFF_MAX) ||
       is_flat_bg_active);
  if (is_smooth_1080_syn) {
    if (is_very_coarse_qp) {
      if (is_flat_bg_active) {
        syn_max_denom = RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_COARSE;
        seg_thresh *= RESIZE_DYN_FLAT_BG_Q_BOOST_COARSE;
        syn_q_cap = RESIZE_DYN_FLAT_BG_QCAP_COARSE;
        syn_q_ref = RESIZE_DYN_FLAT_BG_QREF_COARSE;
        syn_max = RESIZE_DYN_FLAT_BG_MAX_COARSE;
      } else if (rolloff < RESIZE_DYN_VERY_SMOOTH_ROLLOFF_MAX) {
        syn_max_denom = RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_COARSE;
        syn_q_cap = RESIZE_DYN_SMOOTH_SYN_QCAP_COARSE;
        syn_q_ref = RESIZE_DYN_SMOOTH_SYN_QREF_COARSE;
        syn_max = RESIZE_DYN_SMOOTH_SYN_MAX_COARSE;
      }
    } else {
      if (is_flat_bg_active) {
        seg_thresh *= RESIZE_DYN_FLAT_BG_Q_BOOST_MID;
        syn_max_denom = RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_DENOM;
        syn_q_cap = RESIZE_DYN_FLAT_BG_QCAP_MID;
        syn_q_ref = RESIZE_DYN_FLAT_BG_QREF_MID;
      } else {
        seg_thresh *= RESIZE_DYN_SMOOTH_SYN_Q_BOOST_MID;
        syn_max_denom = RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_MID;
        syn_q_cap = RESIZE_DYN_SMOOTH_SYN_QCAP_MID;
        syn_q_ref = RESIZE_DYN_SMOOTH_SYN_QREF_MID;
      }
      syn_max = RESIZE_DYN_SMOOTH_SYN_MAX_MID;
    }
  }

  double content_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double bg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double eff_content_thresh_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double eff_q_thresh_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  int still_contiguous = 1;
  uint8_t raw_best = SCALE_NUMERATOR;

  for (int d = SCALE_NUMERATOR + 1; d <= max_eval_d; ++d) {
    const int use_syn_d = allow_synergy && d <= syn_max_denom;
    const double dec_frac =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);
    const double avg_dist = avg_dist_arr[d];
    const double avg_ratio = avg_ratio_arr[d];
    const double content_ratio = have_mc ? (avg_dist / mc_eff) : avg_ac_arr[d];
    const double bg_ratio =
        (have_mc && pel_ratio > 1.0) ? (avg_dist / (dec_frac * mc_bg)) : 0.0;

    double q_thresh_d = seg_thresh;
    double content_thresh_d = base_content_thresh;
    if (is_extreme_mc && d <= RESIZE_DYN_EXTREME_MC_MAX_DENOM) {
      content_thresh_d =
          AVMMAX(content_thresh_d,
                 RESIZE_DYN_EXTREME_MC_THRESH_FLOOR *
                     AVMMAX(qstep_ratio_sq, RESIZE_DYN_EXTREME_MC_QRAT_FLOOR));
      if (d == SCALE_NUMERATOR + 1) {
        const double mult9 =
            (mc_acr > RESIZE_DYN_EXTREME_MC_STRONG_ACR_MIN &&
             mc_p50 > RESIZE_DYN_EXTREME_MC_STRONG_P50_MIN * bds2)
                ? RESIZE_DYN_EXTREME_MC_D9_STRONG_MULT
                : RESIZE_DYN_EXTREME_MC_D9_MULT;
        q_thresh_d = AVMMAX(q_thresh_d, mult9 * base_thresh);
      } else {
        q_thresh_d =
            AVMMAX(q_thresh_d, RESIZE_DYN_EXTREME_MC_D10_MULT * base_thresh);
      }
    }

    const double syn_q_cap_d =
        (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM &&
         d <= (int)prev_denom && !is_smooth_1080_syn)
            ? AVMMIN(syn_q_cap * RESIZE_DYN_SYNERGY_HOLD_QCAP_MULT, 0.98)
            : syn_q_cap;
    const double eff_content_thresh =
        use_syn_d ? compute_subgop_synergy_content_thresh(
                        content_thresh_d, avg_ratio, base_thresh, syn_q_cap_d,
                        syn_q_ref, syn_max)
                  : content_thresh_d;
    content_ratio_arr[d] = content_ratio;
    bg_ratio_arr[d] = bg_ratio;
    eff_content_thresh_arr[d] = eff_content_thresh;
    eff_q_thresh_arr[d] = q_thresh_d;

    if (still_contiguous && avg_ratio < q_thresh_d &&
        content_ratio < eff_content_thresh && bg_ratio < 1.0) {
      raw_best = (uint8_t)d;
    } else {
      still_contiguous = 0;
    }
  }

  uint8_t best_denom = raw_best;
  if (prev_denom >= SCALE_NUMERATOR) {
    if (raw_best > prev_denom) {
      const int d_next = (int)prev_denom + 1;
      if (d_next <= max_eval_d &&
          avg_ratio_arr[d_next] < hyst_step * eff_q_thresh_arr[d_next] &&
          content_ratio_arr[d_next] <
              hyst_step * eff_content_thresh_arr[d_next] &&
          bg_ratio_arr[d_next] < hyst_step) {
        best_denom = raw_best;
      } else {
        best_denom = prev_denom;
      }
    } else if (raw_best < prev_denom && prev_denom <= max_eval_d) {
      int can_hold = 1;
      const int start_d = AVMMAX(SCALE_NUMERATOR + 1, (int)raw_best + 1);
      for (int d = start_d; d <= (int)prev_denom; ++d) {
        if (avg_ratio_arr[d] >= hyst_keep * eff_q_thresh_arr[d] ||
            content_ratio_arr[d] >= hyst_keep * eff_content_thresh_arr[d] ||
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

  if (best_denom == SCALE_NUMERATOR + 1 &&
      (pel_ratio >= RESIZE_DYNAMIC_HIGHRES_PEL_RATIO ||
       (max_qindex < base_qref_qp && !is_extreme_mc) ||
       (prev_denom != SCALE_NUMERATOR + 1 &&
        mc_uni > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
        content_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_CONTENT_RATIO &&
        avg_ratio_arr[SCALE_NUMERATOR + 1] >=
            RESIZE_DYNAMIC_SUBGOP_D9_MAX_Q_RATIO * base_thresh))) {
    best_denom = SCALE_NUMERATOR;
  }

  const int allow_highres_coscale =
      is_highres_pan ||
      (have_mc && pel_ratio > 1.0 &&
       (mc_p50 > RESIZE_DYN_HIGHRES_COSCALE_P50_MIN * bds2 ||
        mc_acr > RESIZE_DYN_HIGHRES_COSCALE_ACR_MIN)) ||
      (pel_ratio > 1.0 && best_denom >= RESIZE_DYNAMIC_KF_COSCALE_MIN_DENOM &&
       best_denom <= max_eval_d &&
       avg_ratio_arr[best_denom] <
           RESIZE_DYN_HIGHRES_LOWQ_KF_MAX_Q_NORM * base_thresh);
  double kf_q_thresh = ((pel_ratio <= 1.0 && allow_synergy && have_mc &&
                         mc_acr < RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX &&
                         best_denom <= syn_max_denom)
                            ? RESIZE_DYN_SYNERGY_KF_MAX_Q_NORM
                            : RESIZE_DYNAMIC_KF_COSCALE_MAX_Q_NORM) *
                       base_thresh;
  if (hr_high_acr && allow_highres_coscale &&
      mc_acr > RESIZE_DYN_HIGHRES_COSCALE_ACR_MIN &&
      max_qindex >= base_qref_qp) {
    kf_q_thresh = RESIZE_DYN_HIGHRES_HIGH_ACR_KF_MAX_Q_NORM * seg_thresh;
  } else if (is_smooth_1080_syn) {
    kf_q_thresh = RESIZE_DYN_SYNERGY_KF_MAX_Q_NORM * seg_thresh;
  } else if (high_acr_smooth && (is_very_coarse_qp || mc_uni < 0.30)) {
    kf_q_thresh = RESIZE_DYN_SYNERGY_KF_MAX_Q_NORM * seg_thresh;
  }
  if (out_allow_highres_coscale != NULL) {
    *out_allow_highres_coscale = allow_highres_coscale;
  }
  if (out_is_uniform_noise != NULL) *out_is_uniform_noise = is_uniform_noise;
  if (out_kf_q_thresh != NULL) *out_kf_q_thresh = kf_q_thresh;
  return best_denom;
}

// Populates per-frame dynamic resize denominators across a lookahead GF group
// in RESIZE_DYNAMIC mode. Preserves pyramid scale coherence across stationary
// subgops (keeping same-resolution OPFL, BAWP, TIP, WARP, GLOBALMV, and MFMV
// active) while dynamically splitting dyadic half-subgops [0..N/2-1] and
// [N/2..N-1] in chronological display order when a strong mid-subgop spatial
// blur/focus transition occurs.
static void populate_dynamic_gf_resize_scales(AV2_COMP *cpi, int from_keyframe,
                                              int *out_has_group,
                                              int *out_coscale_kf,
                                              uint8_t *out_first_denom) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;
  GF_GROUP *const gf_group = &cpi->gf_group;

  if (out_has_group != NULL) *out_has_group = 0;
  if (out_coscale_kf != NULL) *out_coscale_kf = 0;
  if (out_first_denom != NULL) *out_first_denom = SCALE_NUMERATOR;
  memset(cpi->resize_dyn_gf_denom, SCALE_NUMERATOR,
         sizeof(cpi->resize_dyn_gf_denom));

  if (unscaled == NULL || is_lossless_requested(rc_cfg)) return;

  const int cur_gf_index = gf_group->index;
  if (from_keyframe &&
      (cur_gf_index != 0 || cpi->no_show_fwd_kf || gf_group->size <= 1)) {
    return;
  }
  const int start_gf_idx = from_keyframe ? (cur_gf_index + 1) : cur_gf_index;

  const YV12_BUFFER_CONFIG *grp_srcs[MAX_STATIC_GF_GROUP_LENGTH];
  int grp_qindex[MAX_STATIC_GF_GROUP_LENGTH];
  int grp_gf_idx[MAX_STATIC_GF_GROUP_LENGTH];
  int grp_disp_idx[MAX_STATIC_GF_GROUP_LENGTH];
  int num_grp_frames = 0;

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
  const int saved_frames_since_key = cpi->rc.frames_since_key;
  const int saved_last_q_kf = cpi->rc.last_q[KEY_FRAME];
  const int saved_avg_kf_qindex = cpi->rc.avg_frame_qindex[KEY_FRAME];
  const int saved_last_kf_qindex = cpi->rc.last_kf_qindex;
  const int saved_last_boosted_qindex = cpi->rc.last_boosted_qindex;
  int saved_active_best_quality[MAX_ARF_LAYERS + 1];
  memcpy(saved_active_best_quality, cpi->rc.active_best_quality,
         sizeof(saved_active_best_quality));

  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;
  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int max_qp = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth);
  int has_lossless_frame = 0;
  int max_grp_qindex = 0;
  int min_la_idx = INT_MAX;
  int max_la_idx = -1;

  if (from_keyframe) {
    if (is_stat_consumption_stage(cpi) &&
        (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
      av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
    }
    int kf_bottom = 0, kf_top = 0;
    const int kf_qindex =
        cm->bridge_frame_info.is_bridge_frame
            ? cm->quant_params.base_qindex
            : av2_rc_pick_q_and_bounds(cpi, &cpi->rc, frm_dim_cfg->width,
                                       frm_dim_cfg->height, cur_gf_index,
                                       &kf_bottom, &kf_top);
    if (kf_qindex == 0) {
      has_lossless_frame = 1;
    } else {
      cpi->rc.frames_since_key = 1;
      cpi->rc.level1_qp = -1;
      cpi->rc.last_q[KEY_FRAME] = kf_qindex;
      cpi->rc.avg_frame_qindex[KEY_FRAME] = ROUND_POWER_OF_TWO(
          3 * cpi->rc.avg_frame_qindex[KEY_FRAME] + kf_qindex, 2);
      cpi->rc.last_kf_qindex = kf_qindex;
      cpi->rc.last_boosted_qindex = kf_qindex;
      for (int i = 0; i <= MAX_ARF_LAYERS; ++i) {
        cpi->rc.active_best_quality[i] = kf_qindex;
      }
    }
  }

  for (int gf_idx = start_gf_idx;
       !has_lossless_frame && gf_idx < gf_group->size &&
       gf_idx < MAX_STATIC_GF_GROUP_LENGTH;
       ++gf_idx) {
    const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_idx];
    if (update_type == OVERLAY_UPDATE || update_type == INTNL_OVERLAY_UPDATE ||
        update_type == KFFLT_OVERLAY_UPDATE) {
      continue;
    }

    const int frame_display_index =
        gf_group->cur_frame_idx[gf_idx] + gf_group->arf_src_offset[gf_idx];
    const int lookahead_idx = frame_display_index - base_disp_idx;
    if (lookahead_idx >= 0 && lookahead_idx < min_la_idx) {
      min_la_idx = lookahead_idx;
    }
    if (lookahead_idx > max_la_idx) {
      max_la_idx = lookahead_idx;
    }

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
    if (frame_src == NULL) continue;

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
    if (qindex > max_grp_qindex) max_grp_qindex = qindex;

    for (int i = gf_group->layer_depth[gf_idx]; i <= MAX_ARF_LAYERS; ++i) {
      cpi->rc.active_best_quality[i] = qindex;
    }

    grp_srcs[num_grp_frames] = frame_src;
    grp_qindex[num_grp_frames] = qindex;
    grp_gf_idx[num_grp_frames] = gf_idx;
    grp_disp_idx[num_grp_frames] = frame_display_index;
    ++num_grp_frames;
  }

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
  cpi->rc.frames_since_key = saved_frames_since_key;
  cpi->rc.last_q[KEY_FRAME] = saved_last_q_kf;
  cpi->rc.avg_frame_qindex[KEY_FRAME] = saved_avg_kf_qindex;
  cpi->rc.last_kf_qindex = saved_last_kf_qindex;
  cpi->rc.last_boosted_qindex = saved_last_boosted_qindex;
  memcpy(cpi->rc.active_best_quality, saved_active_best_quality,
         sizeof(saved_active_best_quality));

  if (has_lossless_frame) {
    if (out_has_group != NULL) *out_has_group = (num_grp_frames >= 2);
    return;
  }
  // Require at least 2 valid lookahead/current frames; single-frame / zero-lag
  // groups use calculate_dynamic_causal_frame_scale() per frame instead.
  const int orig_w = unscaled->y_crop_width;
  const int orig_h = unscaled->y_crop_height;
  int ord[MAX_STATIC_GF_GROUP_LENGTH];
  int total_valid_frames = 0;
  for (int f = 0; f < num_grp_frames; ++f) {
    const YV12_BUFFER_CONFIG *const src = grp_srcs[f];
    if (src->y_crop_width == orig_w && src->y_crop_height == orig_h) {
      ord[total_valid_frames++] = f;
    }
  }
  if (total_valid_frames < 2) return;
  if (out_has_group != NULL) *out_has_group = 1;

  // Sort valid group frames in ascending chronological display order so
  // ord[0..half_n-1] and ord[half_n..total_valid_frames-1] correspond strictly
  // to the left and right dyadic temporal halves of the subgop.
  for (int i = 1; i < total_valid_frames; ++i) {
    const int cur_f = ord[i];
    int j = i - 1;
    while (j >= 0 && (grp_disp_idx[ord[j]] > grp_disp_idx[cur_f] ||
                      (grp_disp_idx[ord[j]] == grp_disp_idx[cur_f] &&
                       grp_gf_idx[ord[j]] > grp_gf_idx[cur_f]))) {
      ord[j + 1] = ord[j];
      --j;
    }
    ord[j + 1] = cur_f;
  }

  cpi->resize_dyn_base_qindex = max_grp_qindex;
  const int min_qp =
      RESIZE_DYNAMIC_SUBGOP_MINQP + ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  if (max_grp_qindex < min_qp) {
    cpi->resize_dyn_prev_denom = SCALE_NUMERATOR;
    return;
  }

  const size_t pel_count = (size_t)orig_w * (size_t)orig_h;
  const int max_blocks = (orig_h / 16) * (orig_w / 16);
  ensure_resize_dyn_scratch_bufs(cpi, pel_count, max_blocks);
  uint16_t *const downscaled_buf = cpi->resize_dyn_downscaled_buf;
  uint16_t *const upscaled_y = cpi->resize_dyn_upscaled_y;
  uint16_t *const upscaled_uv = cpi->resize_dyn_upscaled_uv;
  ResizeFrameSharpCache sharp_caches[MAX_STATIC_GF_GROUP_LENGTH];
  memset(sharp_caches, 0, sizeof(sharp_caches));

  avm_clear_system_state();
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double bds2 = bds * bds;
  const int la_base = (from_keyframe && min_la_idx < INT_MAX && min_la_idx > 0)
                          ? min_la_idx
                          : 0;
  double mc_p50 = 0.0, mc_p50_nz = 0.0, mc_nz = 0.0, mc_acr = 0.0;
  int have_mc = sample_lookahead_mc_range(cpi, la_base, max_la_idx, orig_w,
                                          orig_h, bit_depth, bds2, &mc_p50,
                                          &mc_p50_nz, &mc_nz, &mc_acr);
  if (!have_mc && cm->immediate_output_picture != 0 && max_la_idx >= 0) {
    const struct lookahead_entry *const e0 =
        av2_lookahead_peek(cpi->lookahead, la_base, cpi->compressor_stage);
    if (e0 != NULL &&
        compute_pair_mc_stats(&e0->img, unscaled, orig_w, orig_h, bit_depth,
                              &mc_p50, &mc_nz, &mc_acr, &mc_p50_nz)) {
      have_mc = 1;
    }
  }
  if (!have_mc && !from_keyframe &&
      compute_pair_mc_stats(unscaled, cpi->unscaled_last_source, orig_w, orig_h,
                            bit_depth, &mc_p50, &mc_nz, &mc_acr, &mc_p50_nz)) {
    have_mc = 1;
  }

  const uint8_t prev_denom = from_keyframe ? 0 : cpi->resize_dyn_prev_denom;
  const int half_n = total_valid_frames / 2;
  const int can_split_halves = (total_valid_frames >= 8);
  int check_half_split = 0;

  double avg_dist_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ac_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_dist_L[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_dist_R[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ac_L[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ac_R[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double inv_q2_mean[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  int max_eval_d = SCALE_NUMERATOR;

  const double pel_ratio = get_resize_pel_ratio(orig_w, orig_h);
  const double lin_scale_pre = (pel_ratio > 1.0) ? sqrt(pel_ratio) : 1.0;
  const double base_thresh_pre = RESIZE_DYNAMIC_SUBGOP_THRESH * pel_ratio;
  const double mc_uni_pre =
      have_mc ? AVMMIN(mc_p50 / AVMMAX(mc_nz, bds2), 1.0) : 0.0;
  const int qp_offset_pre = ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  const int base_qref_qp_pre = RESIZE_DYNAMIC_QREF + qp_offset_pre;
  const double qstep_ref_pre =
      av2_convert_qindex_to_q(max_grp_qindex, bit_depth) * bds;
  const int is_highres_pan_pre =
      have_mc && pel_ratio > 1.0 &&
      mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_MIN * bds2 &&
      (mc_uni_pre + 2.0 * mc_acr > RESIZE_DYNAMIC_HIGHRES_PAN_MOTION_MIN ||
       mc_p50 > RESIZE_DYNAMIC_HIGHRES_PAN_P50_STRONG * bds2);
  const int qref_qp_pre =
      is_highres_pan_pre ? (RESIZE_DYNAMIC_HIGHRES_PAN_QREF + qp_offset_pre)
                         : base_qref_qp_pre;
  const double qstep_min_pre =
      AVMMAX(av2_convert_qindex_to_q(qref_qp_pre, bit_depth) * bds, 1e-6);
  const double qstep_ratio_pre = qstep_ref_pre / qstep_min_pre;
  const double qstep_ratio_sq_pre = qstep_ratio_pre * qstep_ratio_pre;
  const double mc_q_scale_pre =
      (pel_ratio > 1.0)
          ? (lin_scale_pre * pow(qstep_ratio_pre, 2.0 * lin_scale_pre))
          : qstep_ratio_sq_pre;
  double seg_mc_thresh_ub =
      AVMMIN(RESIZE_DYNAMIC_SUBGOP_MC_THRESH * mc_q_scale_pre,
             RESIZE_DYNAMIC_MC_RATIO_MAX);
  const int is_uniform_noise_pre =
      have_mc && pel_ratio <= 1.0 &&
      mc_uni_pre > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_UNI_MIN &&
      mc_acr < RESIZE_DYNAMIC_MC_FLAT_NOISE_ACR_MAX &&
      mc_p50 > RESIZE_DYNAMIC_MC_UNIFORM_NOISE_P50_MIN * bds2 &&
      qstep_ratio_sq_pre > RESIZE_DYN_UNIFORM_NOISE_MIN_QRAT_SQ;
  const double grain_weight_pre =
      is_highres_pan_pre
          ? RESIZE_DYNAMIC_MC_PAN_GRAIN_WEIGHT
          : (is_uniform_noise_pre ? RESIZE_DYN_UNIFORM_NOISE_GRAIN_WEIGHT
                                  : RESIZE_DYNAMIC_MC_GRAIN_WEIGHT);
  const double mc_eff_pre =
      have_mc
          ? compute_mc_effective_energy(
                mc_p50, mc_nz, mc_acr, qstep_ref_pre * qstep_ref_pre, pel_ratio,
                grain_weight_pre, is_highres_pan_pre || is_uniform_noise_pre,
                is_uniform_noise_pre, bit_depth)
          : 1.0;
  const double mc_bg_pre =
      have_mc ? compute_mc_bg_energy(mc_p50, mc_nz, mc_acr) : 1.0;

  // Conservative upper bounds across all possible optical-rolloff branches in
  // evaluate_calibrated_resize_segment() for bit-exact MC/background pruning.
  const double flat_bg_acr_min_pre =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_FLAT_BG_HOLD_ACR_MIN
          : RESIZE_DYN_FLAT_BG_ACR_MIN;
  const double flat_bg_nz_min_pre =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_FLAT_BG_HOLD_NZ_MIN
          : RESIZE_DYN_FLAT_BG_NZ_MIN;
  const int is_flat_bg_possible =
      have_mc && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
      pel_ratio <= 1.0 && !is_uniform_noise_pre &&
      mc_uni_pre < RESIZE_DYN_FLAT_BG_UNI_MAX &&
      mc_p50_nz > RESIZE_DYN_FLAT_BG_NZ_P50_MIN * bds2 &&
      mc_nz > flat_bg_nz_min_pre * bds2 && mc_acr > flat_bg_acr_min_pre;
  if (is_uniform_noise_pre) {
    seg_mc_thresh_ub =
        AVMMIN(seg_mc_thresh_ub * RESIZE_DYN_UNIFORM_NOISE_MC_BOOST,
               RESIZE_DYNAMIC_MC_RATIO_MAX);
  } else if (have_mc && pel_ratio <= 1.0) {
    if (mc_acr < RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX &&
        mc_uni_pre > RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
        !is_flat_bg_possible) {
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_LOW_ACR_REF;
      seg_mc_thresh_ub *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr < RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_MAX &&
               mc_uni_pre <= RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MIN_UNI &&
               !is_flat_bg_possible) {
      const double acr_scale = mc_acr / RESIZE_DYNAMIC_SUBGOP_STATIC_BG_ACR_REF;
      seg_mc_thresh_ub *= acr_scale * acr_scale * acr_scale;
    } else if (mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN &&
               !(mc_acr > 1.0 && mc_uni_pre > 0.35) &&
               ((mc_uni_pre > 0.20 && mc_uni_pre < 0.48) ||
                (mc_acr > 0.70 && mc_uni_pre > 0.04)) &&
               (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
               mc_p50 > RESIZE_DYN_HIGH_ACR_SMOOTH_P50_MIN * bds2) {
      seg_mc_thresh_ub =
          AVMMIN(seg_mc_thresh_ub * RESIZE_DYN_HIGH_ACR_SMOOTH_MC_BOOST,
                 RESIZE_DYNAMIC_MC_RATIO_MAX);
    }
  }
  const double syn_low_acr_pre =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_SYNERGY_HOLD_LOW_ACR
          : RESIZE_DYNAMIC_SUBGOP_LOW_ACR_MAX;
  const double syn_mid_acr_pre =
      (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM)
          ? RESIZE_DYN_SYNERGY_HOLD_MID_ACR
          : RESIZE_DYNAMIC_SUBGOP_SYNERGY_ACR_MID;
  const int allow_synergy_possible =
      have_mc && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
      pel_ratio <= 1.0 && !is_uniform_noise_pre &&
      qstep_ratio_sq_pre > RESIZE_DYN_SYNERGY_MIN_QRAT_SQ &&
      (((mc_uni_pre > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID &&
         mc_acr > syn_low_acr_pre &&
         mc_nz < RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ * bds2) ||
        (mc_acr > syn_mid_acr_pre &&
         mc_uni_pre > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MIN)) ||
       is_flat_bg_possible);
  const int is_extreme_mc_pre = have_mc && pel_ratio <= 1.0 &&
                                mc_acr > RESIZE_DYN_EXTREME_MC_ACR_MIN &&
                                mc_p50 > RESIZE_DYN_EXTREME_MC_P50_MIN * bds2;
  const int is_very_coarse_qp_pre =
      (qstep_ratio_sq_pre > RESIZE_DYNAMIC_SUBGOP_SYNERGY_HIGH_QRAT_SQ);
  const int is_smooth_1080_possible =
      allow_synergy_possible &&
      ((mc_uni_pre > RESIZE_DYNAMIC_SUBGOP_SYNERGY_UNI_MID &&
        mc_nz < RESIZE_DYNAMIC_SUBGOP_SYNERGY_LOW_ACR_MAX_NZ * bds2) ||
       is_flat_bg_possible);
  const int syn_max_denom_ub =
      is_smooth_1080_possible
          ? (is_very_coarse_qp_pre ? RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_COARSE
                                   : RESIZE_DYN_SMOOTH_SYN_MAX_DENOM_MID)
          : RESIZE_DYNAMIC_SUBGOP_SYNERGY_MAX_DENOM;
  const double syn_q_cap_ub =
      is_very_coarse_qp_pre ? AVMMAX(AVMMAX(RESIZE_DYN_SYNERGY_QCAP_HIGH_UNI,
                                            RESIZE_DYN_SMOOTH_SYN_QCAP_COARSE),
                                     RESIZE_DYN_FLAT_BG_QCAP_COARSE)
                            : AVMMAX(AVMMAX(RESIZE_DYN_SYNERGY_QCAP_MID_UNI,
                                            RESIZE_DYN_SMOOTH_SYN_QCAP_MID),
                                     RESIZE_DYN_FLAT_BG_QCAP_MID);
  const double syn_q_ref_ub =
      is_very_coarse_qp_pre ? AVMMAX(AVMMAX(RESIZE_DYN_SYNERGY_QREF_HIGH,
                                            RESIZE_DYN_SMOOTH_SYN_QREF_COARSE),
                                     RESIZE_DYN_FLAT_BG_QREF_COARSE)
                            : AVMMAX(AVMMAX(RESIZE_DYN_SYNERGY_QREF_MID,
                                            RESIZE_DYN_SMOOTH_SYN_QREF_MID),
                                     RESIZE_DYN_FLAT_BG_QREF_MID);
  const double syn_max_ub =
      is_very_coarse_qp_pre
          ? AVMMAX(AVMMAX(RESIZE_DYN_SYNERGY_MAX_HIGH,
                          RESIZE_DYN_SMOOTH_SYN_MAX_COARSE),
                   RESIZE_DYN_FLAT_BG_MAX_COARSE)
          : AVMMAX(RESIZE_DYN_SYNERGY_MAX_MID, RESIZE_DYN_SMOOTH_SYN_MAX_MID);

  const int could_be_uniform_noise = have_mc && pel_ratio <= 1.0 &&
                                     mc_uni_pre > 0.40 && mc_acr < 0.45 &&
                                     mc_p50 > 10.0 * bds2;
  const int right_n = total_valid_frames - half_n;

  for (int d = SCALE_NUMERATOR + 1; d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    double sum_inv_q2 = 0.0;
    for (int k = 0; k < total_valid_frames; ++k) {
      const int f = ord[k];
      const int qindex_d =
          clamp(grp_qindex[f] + av2_get_resize_qp_offset(d), 1, max_qp);
      const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
      sum_inv_q2 += 1.0 / (qstep * qstep);
    }
    inv_q2_mean[d] = sum_inv_q2 / (double)total_valid_frames;

    const int max_qindex_d =
        clamp(max_grp_qindex + av2_get_resize_qp_offset(d), 1, max_qp);
    const double max_qstep_d =
        av2_convert_qindex_to_q(max_qindex_d, bit_depth) * bds;
    const double inv_q2_lb = 1.0 / (max_qstep_d * max_qstep_d);
    const double hyst_mult_ub =
        (prev_denom > SCALE_NUMERATOR && d <= (int)prev_denom)
            ? ((pel_ratio > 1.0)
                   ? pow(RESIZE_DYNAMIC_SUBGOP_HYST_KEEP, pel_ratio)
                   : RESIZE_DYN_SYNERGY_HOLD_HYST_KEEP)
            : 1.0;
    double q_mult_ub = 1.0;
    if (pel_ratio > 1.0) {
      if (have_mc && mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN) {
        q_mult_ub = RESIZE_DYN_HIGHRES_HIGH_ACR_Q_BOOST;
      }
    } else {
      if (could_be_uniform_noise) {
        q_mult_ub = RESIZE_DYN_UNIFORM_NOISE_THRESH_BOOST;
      } else if (have_mc && mc_acr > RESIZE_DYNAMIC_SUBGOP_HIGH_ACR_MIN) {
        q_mult_ub = AVMMAX(RESIZE_DYN_EXTREME_MC_D9_STRONG_MULT,
                           RESIZE_DYN_HIGH_ACR_SMOOTH_Q_BOOST_MID);
      } else {
        q_mult_ub = AVMMAX(RESIZE_DYN_SMOOTH_SYN_Q_BOOST_MID,
                           RESIZE_DYN_FLAT_BG_Q_BOOST_MID);
      }
    }
    const double ub_q_thresh_d = hyst_mult_ub * q_mult_ub * base_thresh_pre;
    const double dec_frac_d =
        1.0 - (double)(SCALE_NUMERATOR * SCALE_NUMERATOR) / (double)(d * d);
    const double ub_dist_bg_d = hyst_mult_ub * dec_frac_d * mc_bg_pre;
    double base_mc_thresh_d_ub = seg_mc_thresh_ub;
    if (is_extreme_mc_pre && d <= RESIZE_DYN_EXTREME_MC_MAX_DENOM) {
      base_mc_thresh_d_ub = AVMMAX(
          base_mc_thresh_d_ub,
          RESIZE_DYN_EXTREME_MC_THRESH_FLOOR *
              AVMMAX(qstep_ratio_sq_pre, RESIZE_DYN_EXTREME_MC_QRAT_FLOOR));
    }

    const int need_rolloff_d =
        (pel_ratio <= 1.0 && (double)pel_count >= RESIZE_DYNAMIC_REF_PELS &&
         d > SCALE_NUMERATOR + 1 && d <= SCALE_NUMERATOR + 4);

    double cached_luma[MAX_STATIC_GF_GROUP_LENGTH] = { 0.0 };
    double cached_ac[MAX_STATIC_GF_GROUP_LENGTH] = { 0.0 };
    uint8_t luma_done[MAX_STATIC_GF_GROUP_LENGTH] = { 0 };
    int both_pruned = 0;

    if (!check_half_split && !need_rolloff_d) {
      if (can_split_halves) {
        double probe_luma_l = 0.0;
        int left_exceeds = 0;
        for (int k = 0; k < half_n; ++k) {
          const YV12_BUFFER_CONFIG *const src = grp_srcs[ord[k]];
          resample_plane_roundtrip(src->y_buffer, orig_h, orig_w, src->y_stride,
                                   downscaled_buf, scaled_h, scaled_w,
                                   upscaled_y, /*subx=*/0, /*suby=*/0, d,
                                   bit_depth, /*is_uv=*/0);
          avm_clear_system_state();
          compute_nonflat_resample_mse_and_ac_cached(
              src, upscaled_y, orig_w, orig_h, bit_depth, &sharp_caches[k],
              cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac,
              &cached_luma[k], &cached_ac[k]);
          luma_done[k] = 1;
          probe_luma_l += cached_luma[k];
          if ((probe_luma_l * inv_q2_lb) / (double)half_n >= ub_q_thresh_d) {
            left_exceeds = 1;
            break;
          }
        }
        if (left_exceeds) {
          double probe_luma_r = 0.0;
          int right_exceeds = 0;
          for (int k = half_n; k < total_valid_frames; ++k) {
            const YV12_BUFFER_CONFIG *const src = grp_srcs[ord[k]];
            resample_plane_roundtrip(src->y_buffer, orig_h, orig_w,
                                     src->y_stride, downscaled_buf, scaled_h,
                                     scaled_w, upscaled_y, /*subx=*/0,
                                     /*suby=*/0, d, bit_depth, /*is_uv=*/0);
            avm_clear_system_state();
            compute_nonflat_resample_mse_and_ac_cached(
                src, upscaled_y, orig_w, orig_h, bit_depth, &sharp_caches[k],
                cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac,
                &cached_luma[k], &cached_ac[k]);
            luma_done[k] = 1;
            probe_luma_r += cached_luma[k];
            if ((probe_luma_r * inv_q2_lb) / (double)right_n >= ub_q_thresh_d) {
              right_exceeds = 1;
              break;
            }
          }
          if (right_exceeds) {
            avg_dist_arr[d] = 0.0;
            avg_ratio_arr[d] = ub_q_thresh_d + 1.0;
            avg_dist_L[d] = 0.0;
            avg_dist_R[d] = 0.0;
            both_pruned = 1;
          }
        }
      } else {
        double probe_luma = 0.0;
        for (int k = 0; k < total_valid_frames; ++k) {
          const YV12_BUFFER_CONFIG *const src = grp_srcs[ord[k]];
          resample_plane_roundtrip(src->y_buffer, orig_h, orig_w, src->y_stride,
                                   downscaled_buf, scaled_h, scaled_w,
                                   upscaled_y, /*subx=*/0, /*suby=*/0, d,
                                   bit_depth, /*is_uv=*/0);
          avm_clear_system_state();
          compute_nonflat_resample_mse_and_ac_cached(
              src, upscaled_y, orig_w, orig_h, bit_depth, &sharp_caches[k],
              cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac,
              &cached_luma[k], &cached_ac[k]);
          luma_done[k] = 1;
          probe_luma += cached_luma[k];
          if ((probe_luma * inv_q2_lb) / (double)total_valid_frames >=
              ub_q_thresh_d) {
            avg_dist_arr[d] = 0.0;
            avg_ratio_arr[d] = ub_q_thresh_d + 1.0;
            both_pruned = 1;
            break;
          }
        }
      }
    }

    if (both_pruned) {
      max_eval_d = d;
      if (prev_denom <= SCALE_NUMERATOR || d >= (int)prev_denom) {
        break;
      }
      continue;
    }

    // Complete Luma evaluation across all frames before Chroma so we can prune
    // Chroma and larger denominators whenever Luma alone already strictly fails
    // the full-segment background or MC content thresholds.
    double sum_luma_all = 0.0, sum_luma_l = 0.0, sum_luma_r = 0.0;
    double sum_luma_ratio = 0.0;
    double frame_qstep_sq[MAX_STATIC_GF_GROUP_LENGTH];
    for (int k = 0; k < total_valid_frames; ++k) {
      const int f = ord[k];
      const YV12_BUFFER_CONFIG *const src = grp_srcs[f];
      if (!luma_done[k]) {
        resample_plane_roundtrip(src->y_buffer, orig_h, orig_w, src->y_stride,
                                 downscaled_buf, scaled_h, scaled_w, upscaled_y,
                                 /*subx=*/0, /*suby=*/0, d, bit_depth,
                                 /*is_uv=*/0);
        avm_clear_system_state();
        compute_nonflat_resample_mse_and_ac_cached(
            src, upscaled_y, orig_w, orig_h, bit_depth, &sharp_caches[k],
            cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac,
            &cached_luma[k], &cached_ac[k]);
        luma_done[k] = 1;
      }
      const int qindex_d =
          clamp(grp_qindex[f] + av2_get_resize_qp_offset(d), 1, max_qp);
      const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
      frame_qstep_sq[k] = qstep * qstep;
      sum_luma_all += cached_luma[k];
      sum_luma_ratio += cached_luma[k] / frame_qstep_sq[k];
      if (k < half_n) {
        sum_luma_l += cached_luma[k];
      } else {
        sum_luma_r += cached_luma[k];
      }
    }

    const double avg_luma_all = sum_luma_all / (double)total_valid_frames;
    const double avg_luma_ratio = sum_luma_ratio / (double)total_valid_frames;
    const double luma_asym = can_split_halves
                                 ? (AVMMAX(sum_luma_l / (double)half_n,
                                           sum_luma_r / (double)right_n) /
                                    AVMMAX(AVMMIN(sum_luma_l / (double)half_n,
                                                  sum_luma_r / (double)right_n),
                                           1e-6))
                                 : 1.0;
    if (have_mc && !check_half_split && !need_rolloff_d &&
        (!can_split_halves || luma_asym < 1.65) &&
        (prev_denom <= SCALE_NUMERATOR || d >= (int)prev_denom)) {
      const double syn_q_cap_d_ub =
          (prev_denom >= RESIZE_DYN_SYNERGY_HOLD_MIN_DENOM &&
           d <= (int)prev_denom)
              ? AVMMAX(syn_q_cap_ub,
                       AVMMIN(syn_q_cap_ub * RESIZE_DYN_SYNERGY_HOLD_QCAP_MULT,
                              0.98))
              : syn_q_cap_ub;
      const double eff_mc_thresh_ub =
          (allow_synergy_possible && d <= syn_max_denom_ub)
              ? compute_subgop_synergy_content_thresh(
                    base_mc_thresh_d_ub, avg_luma_ratio, base_thresh_pre,
                    syn_q_cap_d_ub, syn_q_ref_ub, syn_max_ub)
              : base_mc_thresh_d_ub;
      const double ub_dist_mc_d = hyst_mult_ub * eff_mc_thresh_ub * mc_eff_pre;
      if ((pel_ratio > 1.0 && avg_luma_all >= ub_dist_bg_d) ||
          avg_luma_all >= ub_dist_mc_d) {
        avg_dist_arr[d] = 0.0;
        avg_ratio_arr[d] = ub_q_thresh_d + 1.0;
        avg_dist_L[d] = 0.0;
        avg_dist_R[d] = 0.0;
        max_eval_d = d;
        break;
      }
    }

    double sum_ratio = 0.0, sum_dist = 0.0, sum_ac = 0.0;
    double sum_dist_l = 0.0, sum_dist_r = 0.0;
    double sum_ac_l = 0.0, sum_ac_r = 0.0;
    for (int k = 0; k < total_valid_frames; ++k) {
      const int f = ord[k];
      const YV12_BUFFER_CONFIG *const src = grp_srcs[f];
      const double luma_mse = cached_luma[k];
      const double ac_ratio = cached_ac[k];
      const double uv_mse = compute_resample_chroma_mse(
          src, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth, downscaled_buf,
          upscaled_uv);
      avm_clear_system_state();
      const double dist = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
      sum_ratio += dist / frame_qstep_sq[k];
      sum_dist += dist;
      sum_ac += ac_ratio;
      if (k < half_n) {
        sum_dist_l += dist;
        sum_ac_l += ac_ratio;
      } else {
        sum_dist_r += dist;
        sum_ac_r += ac_ratio;
      }
    }

    avg_dist_arr[d] = sum_dist / (double)total_valid_frames;
    avg_ratio_arr[d] = sum_ratio / (double)total_valid_frames;
    avg_ac_arr[d] = sum_ac / (double)total_valid_frames;
    if (can_split_halves) {
      avg_dist_L[d] = sum_dist_l / (double)half_n;
      avg_dist_R[d] = sum_dist_r / (double)right_n;
      avg_ac_L[d] = sum_ac_l / (double)half_n;
      avg_ac_R[d] = sum_ac_r / (double)right_n;
      if (d <= SCALE_NUMERATOR + 2) {
        const double asym = AVMMAX(avg_dist_L[d], avg_dist_R[d]) /
                            AVMMAX(AVMMIN(avg_dist_L[d], avg_dist_R[d]), 1e-6);
        if (asym >= RESIZE_DYNAMIC_HALF_SPLIT_PRECHECK_RATIO) {
          check_half_split = 1;
        }
      }
    }
    max_eval_d = d;
    if (!check_half_split && !need_rolloff_d &&
        (prev_denom <= SCALE_NUMERATOR || d >= (int)prev_denom) &&
        avg_dist_L[d] * inv_q2_lb >= ub_q_thresh_d &&
        avg_dist_R[d] * inv_q2_lb >= ub_q_thresh_d &&
        avg_ratio_arr[d] >= ub_q_thresh_d) {
      break;
    }
  }

  int allow_highres_coscale = 0;
  int is_uniform_noise = 0;
  double kf_q_thresh = RESIZE_DYNAMIC_KF_COSCALE_MAX_Q_NORM *
                       RESIZE_DYNAMIC_SUBGOP_THRESH * pel_ratio;
  const uint8_t d_full = evaluate_calibrated_resize_segment(
      avg_dist_arr, avg_ratio_arr, avg_ac_arr, max_eval_d, max_grp_qindex,
      have_mc, mc_p50, mc_p50_nz, mc_nz, mc_acr, orig_w, orig_h, bit_depth,
      prev_denom, &allow_highres_coscale, &is_uniform_noise, &kf_q_thresh);

  uint8_t chosen_L = d_full;
  uint8_t chosen_R = d_full;
  double first_seg_ratio_at_chosen =
      (d_full > SCALE_NUMERATOR && d_full <= max_eval_d) ? avg_ratio_arr[d_full]
                                                         : 0.0;

  if (can_split_halves && check_half_split &&
      max_eval_d >= SCALE_NUMERATOR + 4) {
    const int ref_d = SCALE_NUMERATOR + 4;
    const double d12_asym =
        AVMMAX(avg_dist_L[ref_d], avg_dist_R[ref_d]) /
        AVMMAX(AVMMIN(avg_dist_L[ref_d], avg_dist_R[ref_d]), 1e-6);
    if (d12_asym >= RESIZE_DYNAMIC_HALF_SPLIT_DIST_RATIO) {
      double qrat_L[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
      double qrat_R[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
      for (int d = SCALE_NUMERATOR + 1; d <= max_eval_d; ++d) {
        qrat_L[d] = avg_dist_L[d] * inv_q2_mean[d];
        qrat_R[d] = avg_dist_R[d] * inv_q2_mean[d];
      }
      const int la_mid = la_base + (max_la_idx - la_base) / 2;
      const int la_right_start =
          (max_la_idx >= la_mid + 2) ? (la_mid + 1) : la_mid;
      double p50_l = mc_p50, p50_nz_l = mc_p50_nz, nz_l = mc_nz, acr_l = mc_acr;
      double p50_r = mc_p50, p50_nz_r = mc_p50_nz, nz_r = mc_nz, acr_r = mc_acr;
      int have_mc_l = have_mc, have_mc_r = have_mc;
      if (la_mid > la_base &&
          sample_lookahead_mc_range(cpi, la_base, la_mid, orig_w, orig_h,
                                    bit_depth, bds2, &p50_l, &p50_nz_l, &nz_l,
                                    &acr_l)) {
        have_mc_l = 1;
      }
      if (max_la_idx > la_right_start &&
          sample_lookahead_mc_range(cpi, la_right_start, max_la_idx, orig_w,
                                    orig_h, bit_depth, bds2, &p50_r, &p50_nz_r,
                                    &nz_r, &acr_r)) {
        have_mc_r = 1;
      }
      int allow_hr_l = allow_highres_coscale;
      int is_un_l = is_uniform_noise;
      double kf_q_thresh_l = kf_q_thresh;
      const uint8_t d_L = evaluate_calibrated_resize_segment(
          avg_dist_L, qrat_L, avg_ac_L, max_eval_d, max_grp_qindex, have_mc_l,
          p50_l, p50_nz_l, nz_l, acr_l, orig_w, orig_h, bit_depth, prev_denom,
          &allow_hr_l, &is_un_l, &kf_q_thresh_l);
      const uint8_t d_R = evaluate_calibrated_resize_segment(
          avg_dist_R, qrat_R, avg_ac_R, max_eval_d, max_grp_qindex, have_mc_r,
          p50_r, p50_nz_r, nz_r, acr_r, orig_w, orig_h, bit_depth, d_L, NULL,
          NULL, NULL);
      if (abs((int)d_L - (int)d_R) >= RESIZE_DYNAMIC_HALF_SPLIT_MIN_STEP) {
        chosen_L = d_L;
        chosen_R = d_R;
        allow_highres_coscale = allow_hr_l;
        is_uniform_noise = is_un_l;
        kf_q_thresh = kf_q_thresh_l;
        first_seg_ratio_at_chosen =
            (d_L > SCALE_NUMERATOR && d_L <= max_eval_d) ? qrat_L[d_L] : 0.0;
      }
    }
  }

  // On split subgops, align the midpoint internal ARF (ord[half_n - 1], disp=8)
  // with the right-half scale chosen_R so all right-half frames (disp=9..15)
  // retain same-resolution bi-prediction between ARF2 and ARF, and ARF2 shares
  // scale with its forward ARF anchor.
  const int left_end_k =
      (can_split_halves && chosen_L != chosen_R) ? (half_n - 1) : half_n;
  for (int k = 0; k < total_valid_frames; ++k) {
    const int f = ord[k];
    cpi->resize_dyn_gf_denom[grp_gf_idx[f]] =
        (can_split_halves && k < left_end_k) ? chosen_L : chosen_R;
  }
  // Propagate matching scales onto any overlay entries in gf_group.
  for (int gf_idx = start_gf_idx;
       gf_idx < gf_group->size && gf_idx < MAX_STATIC_GF_GROUP_LENGTH;
       ++gf_idx) {
    const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_idx];
    if (update_type == OVERLAY_UPDATE || update_type == INTNL_OVERLAY_UPDATE ||
        update_type == KFFLT_OVERLAY_UPDATE) {
      const int disp =
          gf_group->cur_frame_idx[gf_idx] + gf_group->arf_src_offset[gf_idx];
      uint8_t match_d = chosen_R;
      for (int k = 0; k < total_valid_frames; ++k) {
        if (grp_disp_idx[ord[k]] == disp) {
          match_d = cpi->resize_dyn_gf_denom[grp_gf_idx[ord[k]]];
          break;
        }
      }
      cpi->resize_dyn_gf_denom[gf_idx] = match_d;
    }
  }

  cpi->resize_dyn_prev_denom = chosen_R;
  if (out_first_denom != NULL) *out_first_denom = chosen_L;
  if (out_coscale_kf != NULL) {
    *out_coscale_kf =
        (chosen_L >= RESIZE_DYNAMIC_KF_COSCALE_MIN_DENOM &&
         (pel_ratio <= 1.0 || allow_highres_coscale) &&
         (is_uniform_noise || first_seg_ratio_at_chosen < kf_q_thresh));
  }

  for (int k = 0; k < total_valid_frames; ++k) {
    free_frame_sharp_cache(&sharp_caches[k]);
  }
}

// Causal per-frame dynamic resize evaluation for zero-lag / Low-Delay /
// single-frame inter coding in RESIZE_DYNAMIC mode. Normalizes against the
// unboosted GF-group baseline qindex (eliminating mini-GOP layer-QP
// oscillation), tracks shot-aware causal MC and spatial resampling statistics
// via EMAs that reset immediately across hard scene cuts or major blur shifts,
// and applies Schmitt-trigger hysteresis plus mini-GOP hold against
// cpi->resize_dyn_prev_denom.
static uint8_t calculate_dynamic_causal_frame_scale(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;
  GF_GROUP *const gf_group = &cpi->gf_group;

  if (unscaled == NULL || is_lossless_requested(rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  const int cur_gf_index = gf_group->index;
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

  int grp_qindex[MAX_STATIC_GF_GROUP_LENGTH];
  int num_q = 0;
  int max_gf_qindex = 0;
  int has_lossless = 0;

  if (cm->bridge_frame_info.is_bridge_frame) {
    max_gf_qindex = cm->quant_params.base_qindex;
    grp_qindex[num_q++] = max_gf_qindex;
    if (max_gf_qindex == 0) has_lossless = 1;
  } else {
    const int q_start =
        (gf_group->update_type[0] == KF_UPDATE && cur_gf_index > 0) ? 1 : 0;
    const int q_end = AVMMIN(gf_group->size, MAX_STATIC_GF_GROUP_LENGTH);
    for (int gi = q_start; gi < q_end; ++gi) {
      const FRAME_UPDATE_TYPE ut = gf_group->update_type[gi];
      if (ut == OVERLAY_UPDATE || ut == INTNL_OVERLAY_UPDATE ||
          ut == KFFLT_OVERLAY_UPDATE) {
        continue;
      }
      gf_group->index = gi;
      cm->current_frame.frame_type = INTER_FRAME;
      cm->current_frame.pyramid_level = get_true_pyr_level(
          gf_group->layer_depth[gi], 0, gf_group->max_layer_depth, 0);
      cpi->rc.is_src_frame_alt_ref = 0;
      av2_rc_set_frame_target(cpi, gf_group->bit_allocation[gi],
                              frm_dim_cfg->width, frm_dim_cfg->height);
      if (is_stat_consumption_stage(cpi) &&
          (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
        av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
      }
      int bottom_index = 0, top_index = 0;
      const int qi = av2_rc_pick_q_and_bounds(cpi, &cpi->rc, frm_dim_cfg->width,
                                              frm_dim_cfg->height, gi,
                                              &bottom_index, &top_index);
      if (qi == 0) {
        has_lossless = 1;
        break;
      }
      if (qi > max_gf_qindex) max_gf_qindex = qi;
      for (int i = gf_group->layer_depth[gi]; i <= MAX_ARF_LAYERS; ++i) {
        cpi->rc.active_best_quality[i] = qi;
      }
      grp_qindex[num_q++] = qi;
    }
  }

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

  if (has_lossless || num_q == 0) return SCALE_NUMERATOR;
  if (num_q == 1 && cpi->resize_dyn_causal_leaf_qindex > max_gf_qindex) {
    cpi->resize_dyn_causal_boost_qindex = max_gf_qindex;
    max_gf_qindex = cpi->resize_dyn_causal_leaf_qindex;
  } else {
    cpi->resize_dyn_causal_leaf_qindex = max_gf_qindex;
  }
  cpi->resize_dyn_base_qindex = max_gf_qindex;
  const int min_qp =
      RESIZE_DYNAMIC_SUBGOP_MINQP + ((int)bit_depth - AVM_BITS_8) * MAXQ_OFFSET;
  if (max_gf_qindex < min_qp) {
    cpi->resize_dyn_prev_denom = SCALE_NUMERATOR;
    cpi->resize_dyn_causal_hold = 0;
    return SCALE_NUMERATOR;
  }

  const int orig_w = unscaled->y_crop_width;
  const int orig_h = unscaled->y_crop_height;
  const size_t pel_count = (size_t)orig_w * (size_t)orig_h;
  const int max_blocks = (orig_h / 16) * (orig_w / 16);
  ensure_resize_dyn_scratch_bufs(cpi, pel_count, max_blocks);
  uint16_t *const downscaled_buf = cpi->resize_dyn_downscaled_buf;
  uint16_t *const upscaled_y = cpi->resize_dyn_upscaled_y;
  uint16_t *const upscaled_uv = cpi->resize_dyn_upscaled_uv;
  ResizeFrameSharpCache sharp_cache;
  memset(&sharp_cache, 0, sizeof(sharp_cache));

  avm_clear_system_state();
  const double bds = (double)(1 << ((int)bit_depth - AVM_BITS_8));
  const double bds2 = bds * bds;
  double cur_p50 = 0.0, cur_p50_nz = 0.0, cur_nz = 0.0, cur_acr = 0.0;
  const YV12_BUFFER_CONFIG *ref_src = cpi->unscaled_last_source;
  if (ref_src == NULL && cpi->lookahead != NULL) {
    const struct lookahead_entry *const e_prev =
        av2_lookahead_peek(cpi->lookahead, -1, cpi->compressor_stage);
    if (e_prev != NULL) ref_src = &e_prev->img;
  }
  int is_cut = 0;
  int have_mc =
      compute_pair_mc_stats(unscaled, ref_src, orig_w, orig_h, bit_depth,
                            &cur_p50, &cur_nz, &cur_acr, &cur_p50_nz);
  if (have_mc) {
    is_cut =
        cpi->resize_dyn_have_ema_mc &&
        cur_nz > AVMMAX(RESIZE_DYNAMIC_CUT_NZ_RATIO * cpi->resize_dyn_ema_nz,
                        RESIZE_DYNAMIC_CUT_NZ_MIN * bds2) &&
        cur_acr > RESIZE_DYNAMIC_CUT_ACR_MIN;
    if (!cpi->resize_dyn_have_ema_mc || is_cut) {
      cpi->resize_dyn_ema_p50 = cur_p50;
      cpi->resize_dyn_ema_p50_nz = cur_p50_nz;
      cpi->resize_dyn_ema_nz = cur_nz;
      cpi->resize_dyn_ema_acr = cur_acr;
      cpi->resize_dyn_have_ema_mc = 1;
      if (is_cut) {
        cpi->resize_dyn_prev_denom = 0;
        cpi->resize_dyn_causal_hold = 0;
      }
    } else {
      cpi->resize_dyn_ema_p50 = 0.5 * cur_p50 + 0.5 * cpi->resize_dyn_ema_p50;
      cpi->resize_dyn_ema_p50_nz =
          0.5 * cur_p50_nz + 0.5 * cpi->resize_dyn_ema_p50_nz;
      cpi->resize_dyn_ema_nz = 0.5 * cur_nz + 0.5 * cpi->resize_dyn_ema_nz;
      cpi->resize_dyn_ema_acr = 0.5 * cur_acr + 0.5 * cpi->resize_dyn_ema_acr;
    }
  } else if (cpi->resize_dyn_have_ema_mc) {
    have_mc = 1;
  }

  const double mc_p50 = have_mc ? cpi->resize_dyn_ema_p50 : 0.0;
  const double mc_p50_nz = have_mc ? cpi->resize_dyn_ema_p50_nz : 0.0;
  const double mc_nz = have_mc ? cpi->resize_dyn_ema_nz : 0.0;
  const double mc_acr = have_mc ? cpi->resize_dyn_ema_acr : 0.0;

  double raw_dist_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double raw_ac_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double inv_q2_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  int max_eval_d = SCALE_NUMERATOR;

  for (int d = SCALE_NUMERATOR + 1; d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    double sum_inv_q2 = 0.0;
    if (cpi->resize_dyn_causal_boost_qindex > 0 &&
        cpi->resize_dyn_causal_leaf_qindex > 0) {
      const int qb_d = clamp(
          cpi->resize_dyn_causal_boost_qindex + av2_get_resize_qp_offset(d), 1,
          max_qp);
      const int ql_d = clamp(
          cpi->resize_dyn_causal_leaf_qindex + av2_get_resize_qp_offset(d), 1,
          max_qp);
      const double qsb = av2_convert_qindex_to_q(qb_d, bit_depth) * bds;
      const double qsl = av2_convert_qindex_to_q(ql_d, bit_depth) * bds;
      inv_q2_arr[d] = 0.25 / (qsb * qsb) + 0.75 / (qsl * qsl);
    } else {
      for (int i = 0; i < num_q; ++i) {
        const int qindex_d =
            clamp(grp_qindex[i] + av2_get_resize_qp_offset(d), 1, max_qp);
        const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) * bds;
        sum_inv_q2 += 1.0 / (qstep * qstep);
      }
      inv_q2_arr[d] = sum_inv_q2 / (double)num_q;
    }

    resample_plane_roundtrip(unscaled->y_buffer, orig_h, orig_w,
                             unscaled->y_stride, downscaled_buf, scaled_h,
                             scaled_w, upscaled_y, /*subx=*/0, /*suby=*/0, d,
                             bit_depth, /*is_uv=*/0);
    avm_clear_system_state();
    double luma_mse = 0.0, ac_ratio = 0.0;
    compute_nonflat_resample_mse_and_ac_cached(
        unscaled, upscaled_y, orig_w, orig_h, bit_depth, &sharp_cache,
        cpi->resize_dyn_sharp_mse, cpi->resize_dyn_sharp_ac, &luma_mse,
        &ac_ratio);
    const double uv_mse = compute_resample_chroma_mse(
        unscaled, orig_w, orig_h, scaled_w, scaled_h, d, bit_depth,
        downscaled_buf, upscaled_uv);
    avm_clear_system_state();
    raw_dist_arr[d] = luma_mse + RESIZE_DYNAMIC_CHROMA_WEIGHT * uv_mse;
    raw_ac_arr[d] = ac_ratio;
    max_eval_d = d;
  }

  const int ref_jump_d = AVMMIN(max_eval_d, SCALE_NUMERATOR + 4);
  int is_spatial_jump = 0;
  if (cpi->resize_dyn_have_ema_dist && ref_jump_d > SCALE_NUMERATOR) {
    const double prev_d_val = cpi->resize_dyn_ema_dist[ref_jump_d];
    const double cur_d_val = raw_dist_arr[ref_jump_d];
    const double ratio = AVMMAX(prev_d_val, cur_d_val) /
                         AVMMAX(AVMMIN(prev_d_val, cur_d_val), 1e-6);
    if (ratio >= RESIZE_DYN_CAUSAL_SPATIAL_JUMP_RATIO) {
      is_spatial_jump = 1;
    }
  }

  double avg_dist_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ratio_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  double avg_ac_arr[SCALE_NUMERATOR * 2 + 1] = { 0.0 };
  if (!cpi->resize_dyn_have_ema_dist || is_cut || is_spatial_jump) {
    for (int d = SCALE_NUMERATOR + 1; d <= max_eval_d; ++d) {
      cpi->resize_dyn_ema_dist[d] = raw_dist_arr[d];
      cpi->resize_dyn_ema_ac[d] = raw_ac_arr[d];
    }
    cpi->resize_dyn_have_ema_dist = 1;
  } else {
    for (int d = SCALE_NUMERATOR + 1; d <= max_eval_d; ++d) {
      cpi->resize_dyn_ema_dist[d] =
          0.5 * raw_dist_arr[d] + 0.5 * cpi->resize_dyn_ema_dist[d];
      cpi->resize_dyn_ema_ac[d] =
          0.5 * raw_ac_arr[d] + 0.5 * cpi->resize_dyn_ema_ac[d];
    }
  }
  for (int d = SCALE_NUMERATOR + 1; d <= max_eval_d; ++d) {
    avg_dist_arr[d] = cpi->resize_dyn_ema_dist[d];
    avg_ratio_arr[d] = avg_dist_arr[d] * inv_q2_arr[d];
    avg_ac_arr[d] = cpi->resize_dyn_ema_ac[d];
  }

  const uint8_t prev_denom = cpi->resize_dyn_prev_denom;
  uint8_t best_denom = evaluate_calibrated_resize_segment(
      avg_dist_arr, avg_ratio_arr, avg_ac_arr, max_eval_d, max_gf_qindex,
      have_mc, mc_p50, mc_p50_nz, mc_nz, mc_acr, orig_w, orig_h, bit_depth,
      prev_denom, NULL, NULL, NULL);
  if (best_denom == SCALE_NUMERATOR + 1) {
    best_denom = SCALE_NUMERATOR;
  }

  const int is_ld_leaf = cpi->resize_dyn_causal_boost_qindex > 0 &&
                         cur_gf_index >= 0 && cur_gf_index < gf_group->size &&
                         gf_group->layer_depth[cur_gf_index] >= 2;
  if (prev_denom >= SCALE_NUMERATOR && !is_cut && !is_spatial_jump &&
      (is_ld_leaf || (cpi->resize_dyn_causal_hold > 0 &&
                      abs((int)best_denom - (int)prev_denom) <
                          RESIZE_DYN_CAUSAL_HOLD_BREAK_STEP))) {
    best_denom = prev_denom;
    if (cpi->resize_dyn_causal_hold > 0) --cpi->resize_dyn_causal_hold;
  } else if (best_denom != prev_denom) {
    cpi->resize_dyn_causal_hold = 3;
  } else if (cpi->resize_dyn_causal_hold > 0) {
    --cpi->resize_dyn_causal_hold;
  }
  cpi->resize_dyn_prev_denom = best_denom;

  free_frame_sharp_cache(&sharp_cache);
  return best_denom;
}

// Top-level scale decision for RESIZE_DYNAMIC (--resize-mode=3), keeping
// RESIZE_DYNAMIC_SUBGOP (--resize-mode=7) 100% bit-exact.
static uint8_t calculate_dynamic_mode_resize_scale(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const GF_GROUP *const gf_group = &cpi->gf_group;

  if (cpi->unscaled_source == NULL || is_lossless_requested(&oxcf->rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  // Keep native resolution on sharp low-color screen/UI/voxel content (or
  // explicit screen-content tuning) where spatial resampling blurs crisp text,
  // HUD, or 1-pixel edges and degrades perceptual quality.
  if (cpi->resize_dyn_is_screen_content) {
    memset(cpi->resize_dyn_gf_denom, SCALE_NUMERATOR,
           sizeof(cpi->resize_dyn_gf_denom));
    cpi->resize_dyn_gf_ready =
        (oxcf->gf_cfg.lag_in_frames > 0 &&
         cm->current_frame.frame_type != KEY_FRAME && !frame_is_intra_only(cm));
    cpi->resize_dyn_prev_denom = SCALE_NUMERATOR;
    cpi->resize_dyn_have_ema_mc = 0;
    cpi->resize_dyn_have_ema_dist = 0;
    cpi->resize_dyn_causal_hold = 0;
    cpi->resize_dyn_gop_coscaled = 0;
    return SCALE_NUMERATOR;
  }

  if (cm->current_frame.frame_type == KEY_FRAME) {
    cpi->resize_dyn_gf_ready = 0;
    cpi->resize_dyn_prev_denom = 0;
    cpi->resize_dyn_have_ema_mc = 0;
    cpi->resize_dyn_have_ema_dist = 0;
    cpi->resize_dyn_causal_hold = 0;
    cpi->resize_dyn_gop_coscaled = 0;
    if (oxcf->gf_cfg.lag_in_frames > 0 && gf_group->index == 0 &&
        !cpi->no_show_fwd_kf && gf_group->size > 1) {
      int has_group = 0;
      int coscale_kf = 0;
      uint8_t first_denom = SCALE_NUMERATOR;
      populate_dynamic_gf_resize_scales(cpi, /*from_keyframe=*/1, &has_group,
                                        &coscale_kf, &first_denom);
      if (has_group && coscale_kf) {
        cpi->resize_dyn_gop_coscaled = 1;
        if (oxcf->rc_cfg.mode == AVM_Q &&
            !gf_group->has_overlay_for_key_frame) {
          cpi->resize_dyn_gf_ready = 1;
        } else {
          cpi->resize_dyn_gf_ready = 0;
          cpi->resize_dyn_prev_denom = 0;
        }
        return first_denom;
      }
      cpi->resize_dyn_gf_ready = 0;
      cpi->resize_dyn_prev_denom = 0;
    }
    return calculate_dynamic_resize_scale(cpi);
  }

  if (frame_is_intra_only(cm)) {
    cpi->resize_dyn_gf_ready = 0;
    cpi->resize_dyn_prev_denom = 0;
    cpi->resize_dyn_have_ema_mc = 0;
    cpi->resize_dyn_have_ema_dist = 0;
    cpi->resize_dyn_causal_hold = 0;
    cpi->resize_dyn_gop_coscaled = 0;
    return calculate_dynamic_resize_scale(cpi);
  }

  const int cur_gf_idx = gf_group->index;
  if (cpi->resize_dyn_gf_ready && cur_gf_idx >= 0 &&
      cur_gf_idx < MAX_STATIC_GF_GROUP_LENGTH) {
    return cpi->resize_dyn_gf_denom[cur_gf_idx];
  }

  if (oxcf->gf_cfg.lag_in_frames > 0 && cur_gf_idx >= 0 &&
      cur_gf_idx < MAX_STATIC_GF_GROUP_LENGTH &&
      gf_group->size - cur_gf_idx >= 2) {
    int has_group = 0;
    populate_dynamic_gf_resize_scales(cpi, /*from_keyframe=*/0, &has_group,
                                      NULL, NULL);
    if (has_group) {
      cpi->resize_dyn_gf_ready = 1;
      return cpi->resize_dyn_gf_denom[cur_gf_idx];
    }
  }

  return calculate_dynamic_causal_frame_scale(cpi);
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
    case RESIZE_DYNAMIC:
      new_denom = calculate_dynamic_mode_resize_scale(cpi);
      break;
    case RESIZE_DYNAMIC_SUBGOP:
      // On a closed-GOP keyframe, evaluate the upcoming first subgop's dynamic
      // scale and co-scale the keyframe to that exact denominator when the
      // subgop downscales to at least d >= 10 with low resampling distortion
      // (preserving same-resolution TIP_FRAME_DIRECT, MFMV, and GLOBALMV across
      // the GOP and avoiding cross-scale reference resampling); otherwise fall
      // back to calculate_dynamic_resize_scale(). In AVM_Q mode without a
      // separate keyframe overlay, lock cpi->resize_subgop_denom to sg0_denom
      // when co-scaling holds so the first inter subgop matches the keyframe
      // without duplicate resampling; otherwise leave resize_subgop_denom = 0
      // so ARF_UPDATE evaluates using its temporally filtered altref buffer.
      if (cpi->common.current_frame.frame_type == KEY_FRAME) {
        int has_subgop = 0;
        int coscale_kf = 0;
        const uint8_t sg0_denom = calculate_dynamic_subgop_resize_scale(
            cpi, /*from_keyframe=*/1, &has_subgop, &coscale_kf);
        if (has_subgop && coscale_kf) {
          new_denom = sg0_denom;
          if (cpi->oxcf.rc_cfg.mode == AVM_Q &&
              !cpi->gf_group.has_overlay_for_key_frame) {
            cpi->resize_subgop_denom = sg0_denom;
            cpi->resize_prev_subgop_denom = sg0_denom;
          } else {
            cpi->resize_subgop_denom = 0;
            cpi->resize_prev_subgop_denom = 0;
          }
        } else {
          new_denom = calculate_dynamic_resize_scale(cpi);
          cpi->resize_subgop_denom = 0;
          cpi->resize_prev_subgop_denom = 0;
        }
      } else {
        if (cpi->resize_subgop_denom == 0) {
          cpi->resize_subgop_denom = calculate_dynamic_subgop_resize_scale(
              cpi, /*from_keyframe=*/0, NULL, NULL);
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
