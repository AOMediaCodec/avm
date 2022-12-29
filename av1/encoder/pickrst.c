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
#include <float.h>
#include <limits.h>
#include <math.h>

#include "config/aom_scale_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/psnr.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/quant_common.h"
#include "av1/common/restoration.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/mathutils.h"
#include "av1/encoder/picklpf.h"
#include "av1/encoder/pickrst.h"

// Number of Wiener iterations
#define NUM_WIENER_ITERS 5

// Penalty factor for use of dual sgr
#define DUAL_SGR_PENALTY_MULT 0.01

// Working precision for Wiener filter coefficients
#define WIENER_TAP_SCALE_FACTOR ((int64_t)1 << 16)

#define SGRPROJ_EP_GRP1_START_IDX 0
#define SGRPROJ_EP_GRP1_END_IDX 9
#define SGRPROJ_EP_GRP1_SEARCH_COUNT 4
#define SGRPROJ_EP_GRP2_3_SEARCH_COUNT 2
static const int sgproj_ep_grp1_seed[SGRPROJ_EP_GRP1_SEARCH_COUNT] = { 0, 3, 6,
                                                                       9 };
static const int sgproj_ep_grp2_3[SGRPROJ_EP_GRP2_3_SEARCH_COUNT][14] = {
  { 10, 10, 11, 11, 12, 12, 13, 13, 13, 13, -1, -1, -1, -1 },
  { 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15 }
};

typedef int64_t (*sse_extractor_type)(const YV12_BUFFER_CONFIG *a,
                                      const YV12_BUFFER_CONFIG *b);
typedef int64_t (*sse_part_extractor_type)(const YV12_BUFFER_CONFIG *a,
                                           const YV12_BUFFER_CONFIG *b,
                                           int hstart, int width, int vstart,
                                           int height);
typedef uint64_t (*var_part_extractor_type)(const YV12_BUFFER_CONFIG *a,
                                            int hstart, int width, int vstart,
                                            int height);

#define NUM_EXTRACTORS 3

static const sse_part_extractor_type sse_part_extractors[NUM_EXTRACTORS] = {
  aom_highbd_get_y_sse_part,
  aom_highbd_get_u_sse_part,
  aom_highbd_get_v_sse_part,
};
static const var_part_extractor_type var_part_extractors[NUM_EXTRACTORS] = {
  aom_highbd_get_y_var,
  aom_highbd_get_u_var,
  aom_highbd_get_v_var,
};

static int64_t sse_restoration_unit(const RestorationTileLimits *limits,
                                    const YV12_BUFFER_CONFIG *src,
                                    const YV12_BUFFER_CONFIG *dst, int plane) {
  return sse_part_extractors[plane](
      src, dst, limits->h_start, limits->h_end - limits->h_start,
      limits->v_start, limits->v_end - limits->v_start);
}

static uint64_t var_restoration_unit(const RestorationTileLimits *limits,
                                     const YV12_BUFFER_CONFIG *src, int plane) {
  return var_part_extractors[plane](
      src, limits->h_start, limits->h_end - limits->h_start, limits->v_start,
      limits->v_end - limits->v_start);
}

typedef struct {
  // The best coefficients for Wiener or Sgrproj restoration
  WienerInfo wiener_info;
  SgrprojInfo sgrproj_info;

  // The sum of squared errors for this rtype.
  int64_t sse[RESTORE_SWITCHABLE_TYPES];

  // The rtype to use for this unit given a frame rtype as
  // index. Indices: WIENER, SGRPROJ, SWITCHABLE.
  RestorationType best_rtype[RESTORE_TYPES - 1];

  // This flag will be set based on the speed feature
  // 'prune_sgr_based_on_wiener'. 0 implies no pruning and 1 implies pruning.
  uint8_t skip_sgr_eval;
} RestUnitSearchInfo;

typedef struct {
  const YV12_BUFFER_CONFIG *src;
  YV12_BUFFER_CONFIG *dst;

  const AV1_COMMON *cm;
  const MACROBLOCK *x;
  int plane;
  int plane_width;
  int plane_height;
  RestUnitSearchInfo *rusi;

  // Speed features
  const LOOP_FILTER_SPEED_FEATURES *lpf_sf;

  uint16_t *dgd_buffer;
  int dgd_stride;
  const uint16_t *src_buffer;
  int src_stride;

  // sse and bits are initialised by reset_rsc in search_rest_type
  int64_t sse;
  int64_t bits;
  int tile_y0, tile_stripe0;
  // Helps convert tile-localized RU indices to frame RU indices.
  int ru_idx_base;

  // sgrproj and wiener are initialised by rsc_on_tile when starting the first
  // tile in the frame.
  WienerInfoBank wiener_bank;
  SgrprojInfoBank sgrproj_bank;

  AV1PixelRect tile_rect;
} RestSearchCtxt;

static AOM_INLINE void reset_all_banks(RestSearchCtxt *rsc) {
  av1_reset_wiener_bank(&rsc->wiener_bank);
  av1_reset_sgrproj_bank(&rsc->sgrproj_bank);
}

static AOM_INLINE void rsc_on_tile(void *priv, int idx_base) {
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  reset_all_banks(rsc);
  rsc->tile_stripe0 = 0;
  rsc->ru_idx_base = idx_base;
}

static AOM_INLINE void reset_rsc(RestSearchCtxt *rsc) {
  rsc->sse = 0;
  rsc->bits = 0;
}

static AOM_INLINE void init_rsc(const YV12_BUFFER_CONFIG *src,
                                const AV1_COMMON *cm, const MACROBLOCK *x,
                                const LOOP_FILTER_SPEED_FEATURES *lpf_sf,
                                int plane, RestUnitSearchInfo *rusi,
                                YV12_BUFFER_CONFIG *dst, RestSearchCtxt *rsc) {
  rsc->src = src;
  rsc->dst = dst;
  rsc->cm = cm;
  rsc->x = x;
  rsc->plane = plane;
  rsc->rusi = rusi;
  rsc->lpf_sf = lpf_sf;

  const YV12_BUFFER_CONFIG *dgd = &cm->cur_frame->buf;
  const int is_uv = plane != AOM_PLANE_Y;
  rsc->plane_width = src->crop_widths[is_uv];
  rsc->plane_height = src->crop_heights[is_uv];
  rsc->src_buffer = src->buffers[plane];
  rsc->src_stride = src->strides[is_uv];
  rsc->dgd_buffer = dgd->buffers[plane];
  rsc->dgd_stride = dgd->strides[is_uv];
  rsc->tile_rect = av1_whole_frame_rect(cm, is_uv);
  assert(src->crop_widths[is_uv] == dgd->crop_widths[is_uv]);
  assert(src->crop_heights[is_uv] == dgd->crop_heights[is_uv]);
}

static int64_t try_restoration_unit(const RestSearchCtxt *rsc,
                                    const RestorationTileLimits *limits,
                                    const AV1PixelRect *tile_rect,
                                    const RestorationUnitInfo *rui) {
  const AV1_COMMON *const cm = rsc->cm;
  const int plane = rsc->plane;
  const int is_uv = plane > 0;
  const RestorationInfo *rsi = &cm->rst_info[plane];
  RestorationLineBuffers rlbs;
  const int bit_depth = cm->seq_params.bit_depth;

  const YV12_BUFFER_CONFIG *fts = &cm->cur_frame->buf;
  // TODO(yunqing): For now, only use optimized LR filter in decoder. Can be
  // also used in encoder.
  const int optimized_lr = 0;

  av1_loop_restoration_filter_unit(
      limits, rui, &rsi->boundaries, &rlbs, tile_rect, rsc->tile_stripe0,
      is_uv && cm->seq_params.subsampling_x,
      is_uv && cm->seq_params.subsampling_y, bit_depth, fts->buffers[plane],
      fts->strides[is_uv], rsc->dst->buffers[plane], rsc->dst->strides[is_uv],
      cm->rst_tmpbuf, optimized_lr);

  return sse_restoration_unit(limits, rsc->src, rsc->dst, plane);
}

int64_t av1_highbd_pixel_proj_error_c(const uint16_t *src, int width,
                                      int height, int src_stride,
                                      const uint16_t *dat, int dat_stride,
                                      int32_t *flt0, int flt0_stride,
                                      int32_t *flt1, int flt1_stride, int xq[2],
                                      const sgr_params_type *params) {
  int i, j;
  int64_t err = 0;
  const int32_t half = 1 << (SGRPROJ_RST_BITS + SGRPROJ_PRJ_BITS - 1);
  if (params->r[0] > 0 && params->r[1] > 0) {
    int xq0 = xq[0];
    int xq1 = xq[1];
    for (i = 0; i < height; ++i) {
      for (j = 0; j < width; ++j) {
        const int32_t d = dat[j];
        const int32_t s = src[j];
        const int32_t u = (int32_t)(d << SGRPROJ_RST_BITS);
        int32_t v0 = flt0[j] - u;
        int32_t v1 = flt1[j] - u;
        int32_t v = half;
        v += xq0 * v0;
        v += xq1 * v1;
        const int32_t e = (v >> (SGRPROJ_RST_BITS + SGRPROJ_PRJ_BITS)) + d - s;
        err += ((int64_t)e * e);
      }
      dat += dat_stride;
      flt0 += flt0_stride;
      flt1 += flt1_stride;
      src += src_stride;
    }
  } else if (params->r[0] > 0 || params->r[1] > 0) {
    int exq;
    int32_t *flt;
    int flt_stride;
    if (params->r[0] > 0) {
      exq = xq[0];
      flt = flt0;
      flt_stride = flt0_stride;
    } else {
      exq = xq[1];
      flt = flt1;
      flt_stride = flt1_stride;
    }
    for (i = 0; i < height; ++i) {
      for (j = 0; j < width; ++j) {
        const int32_t d = dat[j];
        const int32_t s = src[j];
        const int32_t u = (int32_t)(d << SGRPROJ_RST_BITS);
        int32_t v = half;
        v += exq * (flt[j] - u);
        const int32_t e = (v >> (SGRPROJ_RST_BITS + SGRPROJ_PRJ_BITS)) + d - s;
        err += ((int64_t)e * e);
      }
      dat += dat_stride;
      flt += flt_stride;
      src += src_stride;
    }
  } else {
    for (i = 0; i < height; ++i) {
      for (j = 0; j < width; ++j) {
        const int32_t d = dat[j];
        const int32_t s = src[j];
        const int32_t e = d - s;
        err += ((int64_t)e * e);
      }
      dat += dat_stride;
      src += src_stride;
    }
  }
  return err;
}

static int64_t get_pixel_proj_error(const uint16_t *src, int width, int height,
                                    int src_stride, const uint16_t *dat,
                                    int dat_stride, int32_t *flt0,
                                    int flt0_stride, int32_t *flt1,
                                    int flt1_stride, int *xqd,
                                    const sgr_params_type *params) {
  int xq[2];
  av1_decode_xq(xqd, xq, params);

  return av1_highbd_pixel_proj_error(src, width, height, src_stride, dat,
                                     dat_stride, flt0, flt0_stride, flt1,
                                     flt1_stride, xq, params);
}

#define USE_SGRPROJ_REFINEMENT_SEARCH 1
static int64_t finer_search_pixel_proj_error(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int start_step, int *xqd,
    const sgr_params_type *params) {
  int64_t err =
      get_pixel_proj_error(src, width, height, src_stride, dat, dat_stride,
                           flt0, flt0_stride, flt1, flt1_stride, xqd, params);
  (void)start_step;
#if USE_SGRPROJ_REFINEMENT_SEARCH
  int64_t err2;
  int tap_min[] = { SGRPROJ_PRJ_MIN0, SGRPROJ_PRJ_MIN1 };
  int tap_max[] = { SGRPROJ_PRJ_MAX0, SGRPROJ_PRJ_MAX1 };
  for (int s = start_step; s >= 1; s >>= 1) {
    for (int p = 0; p < 2; ++p) {
      if ((params->r[0] == 0 && p == 0) || (params->r[1] == 0 && p == 1)) {
        continue;
      }
      int skip = 0;
      do {
        if (xqd[p] - s >= tap_min[p]) {
          xqd[p] -= s;
          err2 = get_pixel_proj_error(src, width, height, src_stride, dat,
                                      dat_stride, flt0, flt0_stride, flt1,
                                      flt1_stride, xqd, params);
          if (err2 > err) {
            xqd[p] += s;
          } else {
            err = err2;
            skip = 1;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
      if (skip) break;
      do {
        if (xqd[p] + s <= tap_max[p]) {
          xqd[p] += s;
          err2 = get_pixel_proj_error(src, width, height, src_stride, dat,
                                      dat_stride, flt0, flt0_stride, flt1,
                                      flt1_stride, xqd, params);
          if (err2 > err) {
            xqd[p] -= s;
          } else {
            err = err2;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
    }
  }
#endif  // USE_SGRPROJ_REFINEMENT_SEARCH
  return err;
}

static int64_t signed_rounded_divide(int64_t dividend, int64_t divisor) {
  if (dividend < 0)
    return (dividend - divisor / 2) / divisor;
  else
    return (dividend + divisor / 2) / divisor;
}

static AOM_INLINE void calc_proj_params_r0_r1_high_bd_c(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      const int32_t u = (int32_t)(dat[i * dat_stride + j] << SGRPROJ_RST_BITS);
      const int32_t s =
          (int32_t)(src[i * src_stride + j] << SGRPROJ_RST_BITS) - u;
      const int32_t f1 = (int32_t)flt0[i * flt0_stride + j] - u;
      const int32_t f2 = (int32_t)flt1[i * flt1_stride + j] - u;
      H[0][0] += (int64_t)f1 * f1;
      H[1][1] += (int64_t)f2 * f2;
      H[0][1] += (int64_t)f1 * f2;
      C[0] += (int64_t)f1 * s;
      C[1] += (int64_t)f2 * s;
    }
  }
  H[0][0] /= size;
  H[0][1] /= size;
  H[1][1] /= size;
  H[1][0] = H[0][1];
  C[0] /= size;
  C[1] /= size;
}

static AOM_INLINE void calc_proj_params_r0_high_bd_c(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt0, int flt0_stride,
    int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      const int32_t u = (int32_t)(dat[i * dat_stride + j] << SGRPROJ_RST_BITS);
      const int32_t s =
          (int32_t)(src[i * src_stride + j] << SGRPROJ_RST_BITS) - u;
      const int32_t f1 = (int32_t)flt0[i * flt0_stride + j] - u;
      H[0][0] += (int64_t)f1 * f1;
      C[0] += (int64_t)f1 * s;
    }
  }
  H[0][0] /= size;
  C[0] /= size;
}

static AOM_INLINE void calc_proj_params_r1_high_bd_c(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt1, int flt1_stride,
    int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      const int32_t u = (int32_t)(dat[i * dat_stride + j] << SGRPROJ_RST_BITS);
      const int32_t s =
          (int32_t)(src[i * src_stride + j] << SGRPROJ_RST_BITS) - u;
      const int32_t f2 = (int32_t)flt1[i * flt1_stride + j] - u;
      H[1][1] += (int64_t)f2 * f2;
      C[1] += (int64_t)f2 * s;
    }
  }
  H[1][1] /= size;
  C[1] /= size;
}

// The function calls 3 subfunctions for the following cases :
// 1) When params->r[0] > 0 and params->r[1] > 0. In this case all elements
// of C and H need to be computed.
// 2) When only params->r[0] > 0. In this case only H[0][0] and C[0] are
// non-zero and need to be computed.
// 3) When only params->r[1] > 0. In this case only H[1][1] and C[1] are
// non-zero and need to be computed.
static AOM_INLINE void av1_calc_proj_params_high_bd_c(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int64_t H[2][2], int64_t C[2],
    const sgr_params_type *params) {
  if ((params->r[0] > 0) && (params->r[1] > 0)) {
    calc_proj_params_r0_r1_high_bd_c(src, width, height, src_stride, dat,
                                     dat_stride, flt0, flt0_stride, flt1,
                                     flt1_stride, H, C);
  } else if (params->r[0] > 0) {
    calc_proj_params_r0_high_bd_c(src, width, height, src_stride, dat,
                                  dat_stride, flt0, flt0_stride, H, C);
  } else if (params->r[1] > 0) {
    calc_proj_params_r1_high_bd_c(src, width, height, src_stride, dat,
                                  dat_stride, flt1, flt1_stride, H, C);
  }
}

static AOM_INLINE void get_proj_subspace(
    const uint16_t *src, int width, int height, int src_stride,
    const uint16_t *dat, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int *xq, const sgr_params_type *params) {
  int64_t H[2][2] = { { 0, 0 }, { 0, 0 } };
  int64_t C[2] = { 0, 0 };

  // Default values to be returned if the problem becomes ill-posed
  xq[0] = 0;
  xq[1] = 0;

  av1_calc_proj_params_high_bd_c(src, width, height, src_stride, dat,
                                 dat_stride, flt0, flt0_stride, flt1,
                                 flt1_stride, H, C, params);

  if (params->r[0] == 0) {
    // H matrix is now only the scalar H[1][1]
    // C vector is now only the scalar C[1]
    const int64_t Det = H[1][1];
    if (Det == 0) return;  // ill-posed, return default values
    xq[0] = 0;
    xq[1] = (int)signed_rounded_divide(C[1] * (1 << SGRPROJ_PRJ_BITS), Det);
  } else if (params->r[1] == 0) {
    // H matrix is now only the scalar H[0][0]
    // C vector is now only the scalar C[0]
    const int64_t Det = H[0][0];
    if (Det == 0) return;  // ill-posed, return default values
    xq[0] = (int)signed_rounded_divide(C[0] * (1 << SGRPROJ_PRJ_BITS), Det);
    xq[1] = 0;
  } else {
    const int64_t Det = H[0][0] * H[1][1] - H[0][1] * H[1][0];
    if (Det == 0) return;  // ill-posed, return default values

    // If scaling up dividend would overflow, instead scale down the divisor
    const int64_t div1 = H[1][1] * C[0] - H[0][1] * C[1];
    if ((div1 > 0 && INT64_MAX / (1 << SGRPROJ_PRJ_BITS) < div1) ||
        (div1 < 0 && INT64_MIN / (1 << SGRPROJ_PRJ_BITS) > div1))
      xq[0] = (int)signed_rounded_divide(div1, Det / (1 << SGRPROJ_PRJ_BITS));
    else
      xq[0] = (int)signed_rounded_divide(div1 * (1 << SGRPROJ_PRJ_BITS), Det);

    const int64_t div2 = H[0][0] * C[1] - H[1][0] * C[0];
    if ((div2 > 0 && INT64_MAX / (1 << SGRPROJ_PRJ_BITS) < div2) ||
        (div2 < 0 && INT64_MIN / (1 << SGRPROJ_PRJ_BITS) > div2))
      xq[1] = (int)signed_rounded_divide(div2, Det / (1 << SGRPROJ_PRJ_BITS));
    else
      xq[1] = (int)signed_rounded_divide(div2 * (1 << SGRPROJ_PRJ_BITS), Det);
  }
}

static AOM_INLINE void encode_xq(int *xq, int *xqd,
                                 const sgr_params_type *params) {
  if (params->r[0] == 0) {
    xqd[0] = 0;
    xqd[1] = clamp((1 << SGRPROJ_PRJ_BITS) - xq[1], SGRPROJ_PRJ_MIN1,
                   SGRPROJ_PRJ_MAX1);
  } else if (params->r[1] == 0) {
    xqd[0] = clamp(xq[0], SGRPROJ_PRJ_MIN0, SGRPROJ_PRJ_MAX0);
    xqd[1] = clamp((1 << SGRPROJ_PRJ_BITS) - xqd[0], SGRPROJ_PRJ_MIN1,
                   SGRPROJ_PRJ_MAX1);
  } else {
    xqd[0] = clamp(xq[0], SGRPROJ_PRJ_MIN0, SGRPROJ_PRJ_MAX0);
    xqd[1] = clamp((1 << SGRPROJ_PRJ_BITS) - xqd[0] - xq[1], SGRPROJ_PRJ_MIN1,
                   SGRPROJ_PRJ_MAX1);
  }
}

// Apply the self-guided filter across an entire restoration unit.
static AOM_INLINE void apply_sgr(int sgr_params_idx, const uint16_t *dat,
                                 int width, int height, int dat_stride,
                                 int bit_depth, int pu_width, int pu_height,
                                 int32_t *flt0, int32_t *flt1, int flt_stride) {
  for (int i = 0; i < height; i += pu_height) {
    const int h = AOMMIN(pu_height, height - i);
    int32_t *flt0_row = flt0 + i * flt_stride;
    int32_t *flt1_row = flt1 + i * flt_stride;
    const uint16_t *dat_row = dat + i * dat_stride;

    // Iterate over the stripe in blocks of width pu_width
    for (int j = 0; j < width; j += pu_width) {
      const int w = AOMMIN(pu_width, width - j);
      const int ret = av1_selfguided_restoration(
          dat_row + j, w, h, dat_stride, flt0_row + j, flt1_row + j, flt_stride,
          sgr_params_idx, bit_depth);
      (void)ret;
      assert(!ret);
    }
  }
}

static AOM_INLINE int64_t compute_sgrproj_err(
    const uint16_t *dat, const int width, const int height,
    const int dat_stride, const uint16_t *src, const int src_stride,
    const int bit_depth, const int pu_width, const int pu_height, const int ep,
    int32_t *flt0, int32_t *flt1, const int flt_stride, int *exqd) {
  int exq[2];
  apply_sgr(ep, dat, width, height, dat_stride, bit_depth, pu_width, pu_height,
            flt0, flt1, flt_stride);
  aom_clear_system_state();
  const sgr_params_type *const params = &av1_sgr_params[ep];
  get_proj_subspace(src, width, height, src_stride, dat, dat_stride, flt0,
                    flt_stride, flt1, flt_stride, exq, params);
  aom_clear_system_state();
  encode_xq(exq, exqd, params);
  int64_t err = finer_search_pixel_proj_error(
      src, width, height, src_stride, dat, dat_stride, flt0, flt_stride, flt1,
      flt_stride, 2, exqd, params);
  return err;
}

static AOM_INLINE void get_best_error(int64_t *besterr, const int64_t err,
                                      const int *exqd, int *bestxqd,
                                      int *bestep, const int ep) {
  if (*besterr == -1 || err < *besterr) {
    *bestep = ep;
    *besterr = err;
    bestxqd[0] = exqd[0];
    bestxqd[1] = exqd[1];
  }
}

// If limits != NULL, calculates error for current restoration unit.
// Otherwise, calculates error for all units in the stack using stored limits.
static int64_t calc_sgrproj_err(const RestSearchCtxt *rsc,
                                const RestorationTileLimits *limits,
                                const int bit_depth, const int pu_width,
                                const int pu_height, const int ep,
                                int32_t *flt0, int32_t *flt1, int *exqd) {
  int64_t err = 0;

  uint16_t *dat;
  const uint16_t *src;
  int width, height, dat_stride, src_stride, flt_stride;
  dat_stride = rsc->dgd_stride;
  src_stride = rsc->src_stride;
  if (limits != NULL) {
    dat = rsc->dgd_buffer + limits->v_start * rsc->dgd_stride + limits->h_start;
    src = rsc->src_buffer + limits->v_start * rsc->src_stride + limits->h_start;
    width = limits->h_end - limits->h_start;
    height = limits->v_end - limits->v_start;
    flt_stride = ((width + 7) & ~7) + 8;
    err = compute_sgrproj_err(dat, width, height, dat_stride, src, src_stride,
                              bit_depth, pu_width, pu_height, ep, flt0, flt1,
                              flt_stride, exqd);
  } else {
    assert(0 && "Tile limits should not be NULL.");
  }
  return err;
}

static SgrprojInfo search_selfguided_restoration(
    const RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    int bit_depth, int pu_width, int pu_height, int32_t *rstbuf,
    int enable_sgr_ep_pruning) {
  int32_t *flt0 = rstbuf;
  int32_t *flt1 = flt0 + RESTORATION_UNITPELS_MAX;
  int ep, idx, bestep = 0;
  int64_t besterr = -1;
  int exqd[2], bestxqd[2] = { 0, 0 };
  assert(pu_width == (RESTORATION_PROC_UNIT_SIZE >> 1) ||
         pu_width == RESTORATION_PROC_UNIT_SIZE);
  assert(pu_height == (RESTORATION_PROC_UNIT_SIZE >> 1) ||
         pu_height == RESTORATION_PROC_UNIT_SIZE);
  if (!enable_sgr_ep_pruning) {
    for (ep = 0; ep < SGRPROJ_PARAMS; ep++) {
      int64_t err = calc_sgrproj_err(rsc, limits, bit_depth, pu_width,
                                     pu_height, ep, flt0, flt1, exqd);
      get_best_error(&besterr, err, exqd, bestxqd, &bestep, ep);
    }
  } else {
    // evaluate first four seed ep in first group
    for (idx = 0; idx < SGRPROJ_EP_GRP1_SEARCH_COUNT; idx++) {
      ep = sgproj_ep_grp1_seed[idx];
      int64_t err = calc_sgrproj_err(rsc, limits, bit_depth, pu_width,
                                     pu_height, ep, flt0, flt1, exqd);
      get_best_error(&besterr, err, exqd, bestxqd, &bestep, ep);
    }
    // evaluate left and right ep of winner in seed ep
    int bestep_ref = bestep;
    for (ep = bestep_ref - 1; ep < bestep_ref + 2; ep += 2) {
      if (ep < SGRPROJ_EP_GRP1_START_IDX || ep > SGRPROJ_EP_GRP1_END_IDX)
        continue;
      int64_t err = calc_sgrproj_err(rsc, limits, bit_depth, pu_width,
                                     pu_height, ep, flt0, flt1, exqd);
      get_best_error(&besterr, err, exqd, bestxqd, &bestep, ep);
    }
    // evaluate last two group
    for (idx = 0; idx < SGRPROJ_EP_GRP2_3_SEARCH_COUNT; idx++) {
      ep = sgproj_ep_grp2_3[idx][bestep];
      int64_t err = calc_sgrproj_err(rsc, limits, bit_depth, pu_width,
                                     pu_height, ep, flt0, flt1, exqd);
      get_best_error(&besterr, err, exqd, bestxqd, &bestep, ep);
    }
  }

  SgrprojInfo ret;
  ret.ep = bestep;
  ret.xqd[0] = bestxqd[0];
  ret.xqd[1] = bestxqd[1];
  return ret;
}

static int64_t count_sgrproj_bits(const ModeCosts *mode_costs,
                                  SgrprojInfo *sgrproj_info,
                                  const SgrprojInfoBank *bank) {
  (void)mode_costs;
  int64_t bits = 0;
  const SgrprojInfo *ref_sgrproj_info = av1_constref_from_sgrproj_bank(bank, 0);
  bits += (SGRPROJ_PARAMS_BITS << AV1_PROB_COST_SHIFT);
  const sgr_params_type *params = &av1_sgr_params[sgrproj_info->ep];
  if (params->r[0] > 0) {
    bits += aom_count_primitive_refsubexpfin(
                SGRPROJ_PRJ_MAX0 - SGRPROJ_PRJ_MIN0 + 1, SGRPROJ_PRJ_SUBEXP_K,
                ref_sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0,
                sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0)
            << AV1_PROB_COST_SHIFT;
  }
  if (params->r[1] > 0) {
    bits += aom_count_primitive_refsubexpfin(
                SGRPROJ_PRJ_MAX1 - SGRPROJ_PRJ_MIN1 + 1, SGRPROJ_PRJ_SUBEXP_K,
                ref_sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1,
                sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1)
            << AV1_PROB_COST_SHIFT;
  }
  return bits;
}

static AOM_INLINE void search_sgrproj_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_seq, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)rlbs;
  (void)rest_unit_idx_seq;
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const MACROBLOCK *const x = rsc->x;
  const AV1_COMMON *const cm = rsc->cm;
  const int bit_depth = cm->seq_params.bit_depth;

  const int64_t bits_none = x->mode_costs.sgrproj_restore_cost[0];
  // Prune evaluation of RESTORE_SGRPROJ if 'skip_sgr_eval' is set
  if (rusi->skip_sgr_eval) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_SGRPROJ - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_SGRPROJ] = INT64_MAX;
    return;
  }

  const int is_uv = rsc->plane > 0;
  const int ss_x = is_uv && cm->seq_params.subsampling_x;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;
  const int procunit_width = RESTORATION_PROC_UNIT_SIZE >> ss_x;
  const int procunit_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;

  rusi->sgrproj_info = search_selfguided_restoration(
      rsc, limits, bit_depth, procunit_width, procunit_height, tmpbuf,
      rsc->lpf_sf->enable_sgr_ep_pruning);

  RestorationUnitInfo rui;
  rui.restoration_type = RESTORE_SGRPROJ;
  rui.sgrproj_info = rusi->sgrproj_info;

  rusi->sse[RESTORE_SGRPROJ] =
      try_restoration_unit(rsc, limits, &rsc->tile_rect, &rui);

  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);

  const int64_t bits_sgr =
      x->mode_costs.sgrproj_restore_cost[1] +
      count_sgrproj_bits(&x->mode_costs, &rusi->sgrproj_info,
                         &rsc->sgrproj_bank);
  double cost_sgr = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_sgr >> 4, rusi->sse[RESTORE_SGRPROJ], bit_depth);
  if (rusi->sgrproj_info.ep < 10)
    cost_sgr *=
        (1 + DUAL_SGR_PENALTY_MULT * rsc->lpf_sf->dual_sgr_penalty_level);

  RestorationType rtype =
      (cost_sgr < cost_none) ? RESTORE_SGRPROJ : RESTORE_NONE;
  rusi->best_rtype[RESTORE_SGRPROJ - 1] = rtype;

  rsc->sse += rusi->sse[rtype];
  rsc->bits += (cost_sgr < cost_none) ? bits_sgr : bits_none;
  if (cost_sgr < cost_none)
    av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
}

void av1_compute_stats_highbd_c(int wiener_win, const uint16_t *dgd,
                                const uint16_t *src, int h_start, int h_end,
                                int v_start, int v_end, int dgd_stride,
                                int src_stride, int64_t *M, int64_t *H,
                                aom_bit_depth_t bit_depth) {
  int i, j, k, l;
  int32_t Y[WIENER_WIN2];
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  uint16_t avg =
      find_average_highbd(dgd, h_start, h_end, v_start, v_end, dgd_stride);

  uint8_t bit_depth_divider = 1;
  if (bit_depth == AOM_BITS_12)
    bit_depth_divider = 16;
  else if (bit_depth == AOM_BITS_10)
    bit_depth_divider = 4;

  memset(M, 0, sizeof(*M) * wiener_win2);
  memset(H, 0, sizeof(*H) * wiener_win2 * wiener_win2);
  for (i = v_start; i < v_end; i++) {
    for (j = h_start; j < h_end; j++) {
      const int32_t X = (int32_t)src[i * src_stride + j] - (int32_t)avg;
      int idx = 0;
      for (k = -wiener_halfwin; k <= wiener_halfwin; k++) {
        for (l = -wiener_halfwin; l <= wiener_halfwin; l++) {
          Y[idx] = (int32_t)dgd[(i + l) * dgd_stride + (j + k)] - (int32_t)avg;
          idx++;
        }
      }
      assert(idx == wiener_win2);
      for (k = 0; k < wiener_win2; ++k) {
        M[k] += (int64_t)Y[k] * X;
        for (l = k; l < wiener_win2; ++l) {
          // H is a symmetric matrix, so we only need to fill out the upper
          // triangle here. We can copy it down to the lower triangle outside
          // the (i, j) loops.
          H[k * wiener_win2 + l] += (int64_t)Y[k] * Y[l];
        }
      }
    }
  }
  for (k = 0; k < wiener_win2; ++k) {
    M[k] /= bit_depth_divider;
    H[k * wiener_win2 + k] /= bit_depth_divider;
    for (l = k + 1; l < wiener_win2; ++l) {
      H[k * wiener_win2 + l] /= bit_depth_divider;
      H[l * wiener_win2 + k] = H[k * wiener_win2 + l];
    }
  }
}

static INLINE int wrap_index(int i, int wiener_win) {
  const int wiener_halfwin1 = (wiener_win >> 1) + 1;
  return (i >= wiener_halfwin1 ? wiener_win - 1 - i : i);
}

// Solve linear equations to find Wiener filter tap values
// Taps are output scaled by WIENER_FILT_STEP
static int linsolve_wiener(int n, int64_t *A, int stride, int64_t *b,
                           int32_t *x) {
  for (int k = 0; k < n - 1; k++) {
    // Partial pivoting: bring the row with the largest pivot to the top
    for (int i = n - 1; i > k; i--) {
      // If row i has a better (bigger) pivot than row (i-1), swap them
      if (llabs(A[(i - 1) * stride + k]) < llabs(A[i * stride + k])) {
        for (int j = 0; j < n; j++) {
          const int64_t c = A[i * stride + j];
          A[i * stride + j] = A[(i - 1) * stride + j];
          A[(i - 1) * stride + j] = c;
        }
        const int64_t c = b[i];
        b[i] = b[i - 1];
        b[i - 1] = c;
      }
    }
    // Forward elimination (convert A to row-echelon form)
    for (int i = k; i < n - 1; i++) {
      if (A[k * stride + k] == 0) return 0;
      const int64_t c = A[(i + 1) * stride + k];
      const int64_t cd = A[k * stride + k];
      for (int j = 0; j < n; j++) {
        A[(i + 1) * stride + j] -= c / 256 * A[k * stride + j] / cd * 256;
      }
      if (llabs(c) > INT_MAX || llabs(b[k]) > INT_MAX) {
        // Reduce the probability of overflow by computing at lower precision
        b[i + 1] -= AOMMAX(c, b[k]) / 256 * AOMMIN(c, b[k]) / cd * 256;
      } else {
        b[i + 1] -= c * b[k] / cd;
      }
    }
  }
  // Back-substitution
  for (int i = n - 1; i >= 0; i--) {
    if (A[i * stride + i] == 0) return 0;
    int64_t c = 0;
    for (int j = i + 1; j <= n - 1; j++) {
      c += A[i * stride + j] * x[j] / WIENER_TAP_SCALE_FACTOR;
    }
    // Store filter taps x in scaled form.
    x[i] = (int32_t)(WIENER_TAP_SCALE_FACTOR * (b[i] - c) / A[i * stride + i]);
  }

  return 1;
}

// Fix vector b, update vector a
static AOM_INLINE void update_a_sep_sym(int wiener_win, int64_t **Mc,
                                        int64_t **Hc, int32_t *a, int32_t *b) {
  int i, j;
  int32_t S[WIENER_WIN];
  int64_t A[WIENER_HALFWIN1], B[WIENER_HALFWIN1 * WIENER_HALFWIN1];
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin1 = (wiener_win >> 1) + 1;
  memset(A, 0, sizeof(A));
  memset(B, 0, sizeof(B));
  for (i = 0; i < wiener_win; i++) {
    for (j = 0; j < wiener_win; ++j) {
      const int jj = wrap_index(j, wiener_win);
      A[jj] += Mc[i][j] * b[i] / WIENER_TAP_SCALE_FACTOR;
    }
  }
  for (i = 0; i < wiener_win; i++) {
    for (j = 0; j < wiener_win; j++) {
      int k, l;
      for (k = 0; k < wiener_win; ++k) {
        for (l = 0; l < wiener_win; ++l) {
          const int kk = wrap_index(k, wiener_win);
          const int ll = wrap_index(l, wiener_win);
          B[ll * wiener_halfwin1 + kk] +=
              Hc[j * wiener_win + i][k * wiener_win2 + l] * b[i] /
              WIENER_TAP_SCALE_FACTOR * b[j] / WIENER_TAP_SCALE_FACTOR;
        }
      }
    }
  }
  // Normalization enforcement in the system of equations itself
  for (i = 0; i < wiener_halfwin1 - 1; ++i) {
    A[i] -=
        A[wiener_halfwin1 - 1] * 2 +
        B[i * wiener_halfwin1 + wiener_halfwin1 - 1] -
        2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)];
  }
  for (i = 0; i < wiener_halfwin1 - 1; ++i) {
    for (j = 0; j < wiener_halfwin1 - 1; ++j) {
      B[i * wiener_halfwin1 + j] -=
          2 * (B[i * wiener_halfwin1 + (wiener_halfwin1 - 1)] +
               B[(wiener_halfwin1 - 1) * wiener_halfwin1 + j] -
               2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 +
                     (wiener_halfwin1 - 1)]);
    }
  }
  if (linsolve_wiener(wiener_halfwin1 - 1, B, wiener_halfwin1, A, S)) {
    S[wiener_halfwin1 - 1] = WIENER_TAP_SCALE_FACTOR;
    for (i = wiener_halfwin1; i < wiener_win; ++i) {
      S[i] = S[wiener_win - 1 - i];
      S[wiener_halfwin1 - 1] -= 2 * S[i];
    }
    memcpy(a, S, wiener_win * sizeof(*a));
  }
}

// Fix vector a, update vector b
static AOM_INLINE void update_b_sep_sym(int wiener_win, int64_t **Mc,
                                        int64_t **Hc, int32_t *a, int32_t *b) {
  int i, j;
  int32_t S[WIENER_WIN];
  int64_t A[WIENER_HALFWIN1], B[WIENER_HALFWIN1 * WIENER_HALFWIN1];
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin1 = (wiener_win >> 1) + 1;
  memset(A, 0, sizeof(A));
  memset(B, 0, sizeof(B));
  for (i = 0; i < wiener_win; i++) {
    const int ii = wrap_index(i, wiener_win);
    for (j = 0; j < wiener_win; j++) {
      A[ii] += Mc[i][j] * a[j] / WIENER_TAP_SCALE_FACTOR;
    }
  }

  for (i = 0; i < wiener_win; i++) {
    for (j = 0; j < wiener_win; j++) {
      const int ii = wrap_index(i, wiener_win);
      const int jj = wrap_index(j, wiener_win);
      int k, l;
      for (k = 0; k < wiener_win; ++k) {
        for (l = 0; l < wiener_win; ++l) {
          B[jj * wiener_halfwin1 + ii] +=
              Hc[i * wiener_win + j][k * wiener_win2 + l] * a[k] /
              WIENER_TAP_SCALE_FACTOR * a[l] / WIENER_TAP_SCALE_FACTOR;
        }
      }
    }
  }
  // Normalization enforcement in the system of equations itself
  for (i = 0; i < wiener_halfwin1 - 1; ++i) {
    A[i] -=
        A[wiener_halfwin1 - 1] * 2 +
        B[i * wiener_halfwin1 + wiener_halfwin1 - 1] -
        2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)];
  }
  for (i = 0; i < wiener_halfwin1 - 1; ++i) {
    for (j = 0; j < wiener_halfwin1 - 1; ++j) {
      B[i * wiener_halfwin1 + j] -=
          2 * (B[i * wiener_halfwin1 + (wiener_halfwin1 - 1)] +
               B[(wiener_halfwin1 - 1) * wiener_halfwin1 + j] -
               2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 +
                     (wiener_halfwin1 - 1)]);
    }
  }
  if (linsolve_wiener(wiener_halfwin1 - 1, B, wiener_halfwin1, A, S)) {
    S[wiener_halfwin1 - 1] = WIENER_TAP_SCALE_FACTOR;
    for (i = wiener_halfwin1; i < wiener_win; ++i) {
      S[i] = S[wiener_win - 1 - i];
      S[wiener_halfwin1 - 1] -= 2 * S[i];
    }
    memcpy(b, S, wiener_win * sizeof(*b));
  }
}

static int wiener_decompose_sep_sym(int wiener_win, int64_t *M, int64_t *H,
                                    int32_t *a, int32_t *b) {
  static const int32_t init_filt[WIENER_WIN] = {
    WIENER_FILT_TAP0_MIDV, WIENER_FILT_TAP1_MIDV, WIENER_FILT_TAP2_MIDV,
    WIENER_FILT_TAP3_MIDV, WIENER_FILT_TAP2_MIDV, WIENER_FILT_TAP1_MIDV,
    WIENER_FILT_TAP0_MIDV,
  };
  int64_t *Hc[WIENER_WIN2];
  int64_t *Mc[WIENER_WIN];
  int i, j, iter;
  const int plane_off = (WIENER_WIN - wiener_win) >> 1;
  const int wiener_win2 = wiener_win * wiener_win;
  for (i = 0; i < wiener_win; i++) {
    a[i] = b[i] =
        WIENER_TAP_SCALE_FACTOR / WIENER_FILT_STEP * init_filt[i + plane_off];
  }
  for (i = 0; i < wiener_win; i++) {
    Mc[i] = M + i * wiener_win;
    for (j = 0; j < wiener_win; j++) {
      Hc[i * wiener_win + j] =
          H + i * wiener_win * wiener_win2 + j * wiener_win;
    }
  }

  iter = 1;
  while (iter < NUM_WIENER_ITERS) {
    update_a_sep_sym(wiener_win, Mc, Hc, a, b);
    update_b_sep_sym(wiener_win, Mc, Hc, a, b);
    iter++;
  }
  return 1;
}

// Computes the function x'*H*x - x'*M for the learned 2D filter x, and compares
// against identity filters; Final score is defined as the difference between
// the function values
static int64_t compute_score(int wiener_win, int64_t *M, int64_t *H,
                             InterpKernel vfilt, InterpKernel hfilt) {
  int32_t ab[WIENER_WIN * WIENER_WIN];
  int16_t a[WIENER_WIN], b[WIENER_WIN];
  int64_t P = 0, Q = 0;
  int64_t iP = 0, iQ = 0;
  int64_t Score, iScore;
  int i, k, l;
  const int plane_off = (WIENER_WIN - wiener_win) >> 1;
  const int wiener_win2 = wiener_win * wiener_win;

  aom_clear_system_state();

  a[WIENER_HALFWIN] = b[WIENER_HALFWIN] = WIENER_FILT_STEP;
  for (i = 0; i < WIENER_HALFWIN; ++i) {
    a[i] = a[WIENER_WIN - i - 1] = vfilt[i];
    b[i] = b[WIENER_WIN - i - 1] = hfilt[i];
    a[WIENER_HALFWIN] -= 2 * a[i];
    b[WIENER_HALFWIN] -= 2 * b[i];
  }
  memset(ab, 0, sizeof(ab));
  for (k = 0; k < wiener_win; ++k) {
    for (l = 0; l < wiener_win; ++l)
      ab[k * wiener_win + l] = a[l + plane_off] * b[k + plane_off];
  }
  for (k = 0; k < wiener_win2; ++k) {
    P += ab[k] * M[k] / WIENER_FILT_STEP / WIENER_FILT_STEP;
    for (l = 0; l < wiener_win2; ++l) {
      Q += ab[k] * H[k * wiener_win2 + l] * ab[l] / WIENER_FILT_STEP /
           WIENER_FILT_STEP / WIENER_FILT_STEP / WIENER_FILT_STEP;
    }
  }
  Score = Q - 2 * P;

  iP = M[wiener_win2 >> 1];
  iQ = H[(wiener_win2 >> 1) * wiener_win2 + (wiener_win2 >> 1)];
  iScore = iQ - 2 * iP;

  return Score - iScore;
}

static AOM_INLINE void finalize_sym_filter(int wiener_win, int32_t *f,
                                           InterpKernel fi) {
  int i;
  const int wiener_halfwin = (wiener_win >> 1);

  for (i = 0; i < wiener_halfwin; ++i) {
    const int64_t dividend = (int64_t)f[i] * WIENER_FILT_STEP;
    const int64_t divisor = WIENER_TAP_SCALE_FACTOR;
    // Perform this division with proper rounding rather than truncation
    if (dividend < 0) {
      fi[i] = (int16_t)((dividend - (divisor / 2)) / divisor);
    } else {
      fi[i] = (int16_t)((dividend + (divisor / 2)) / divisor);
    }
  }
  // Specialize for 7-tap filter
  if (wiener_win == WIENER_WIN) {
    fi[0] = CLIP(fi[0], WIENER_FILT_TAP0_MINV, WIENER_FILT_TAP0_MAXV);
    fi[1] = CLIP(fi[1], WIENER_FILT_TAP1_MINV, WIENER_FILT_TAP1_MAXV);
    fi[2] = CLIP(fi[2], WIENER_FILT_TAP2_MINV, WIENER_FILT_TAP2_MAXV);
  } else {
    fi[2] = CLIP(fi[1], WIENER_FILT_TAP2_MINV, WIENER_FILT_TAP2_MAXV);
    fi[1] = CLIP(fi[0], WIENER_FILT_TAP1_MINV, WIENER_FILT_TAP1_MAXV);
    fi[0] = 0;
  }
  // Satisfy filter constraints
  fi[WIENER_WIN - 1] = fi[0];
  fi[WIENER_WIN - 2] = fi[1];
  fi[WIENER_WIN - 3] = fi[2];
  // The central element has an implicit +WIENER_FILT_STEP
  fi[3] = -2 * (fi[0] + fi[1] + fi[2]);
}

static int64_t count_wiener_bits(int wiener_win, const ModeCosts *mode_costs,
                                 WienerInfo *wiener_info,
                                 const WienerInfoBank *bank) {
  (void)mode_costs;
  int64_t bits = 0;
  const WienerInfo *ref_wiener_info = av1_constref_from_wiener_bank(bank, 0);
  if (wiener_win == WIENER_WIN)
    bits += aom_count_primitive_refsubexpfin(
                WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
                WIENER_FILT_TAP0_SUBEXP_K,
                ref_wiener_info->vfilter[0] - WIENER_FILT_TAP0_MINV,
                wiener_info->vfilter[0] - WIENER_FILT_TAP0_MINV)
            << AV1_PROB_COST_SHIFT;
  bits += aom_count_primitive_refsubexpfin(
              WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
              WIENER_FILT_TAP1_SUBEXP_K,
              ref_wiener_info->vfilter[1] - WIENER_FILT_TAP1_MINV,
              wiener_info->vfilter[1] - WIENER_FILT_TAP1_MINV)
          << AV1_PROB_COST_SHIFT;
  bits += aom_count_primitive_refsubexpfin(
              WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
              WIENER_FILT_TAP2_SUBEXP_K,
              ref_wiener_info->vfilter[2] - WIENER_FILT_TAP2_MINV,
              wiener_info->vfilter[2] - WIENER_FILT_TAP2_MINV)
          << AV1_PROB_COST_SHIFT;
  if (wiener_win == WIENER_WIN)
    bits += aom_count_primitive_refsubexpfin(
                WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
                WIENER_FILT_TAP0_SUBEXP_K,
                ref_wiener_info->hfilter[0] - WIENER_FILT_TAP0_MINV,
                wiener_info->hfilter[0] - WIENER_FILT_TAP0_MINV)
            << AV1_PROB_COST_SHIFT;
  bits += aom_count_primitive_refsubexpfin(
              WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
              WIENER_FILT_TAP1_SUBEXP_K,
              ref_wiener_info->hfilter[1] - WIENER_FILT_TAP1_MINV,
              wiener_info->hfilter[1] - WIENER_FILT_TAP1_MINV)
          << AV1_PROB_COST_SHIFT;
  bits += aom_count_primitive_refsubexpfin(
              WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
              WIENER_FILT_TAP2_SUBEXP_K,
              ref_wiener_info->hfilter[2] - WIENER_FILT_TAP2_MINV,
              wiener_info->hfilter[2] - WIENER_FILT_TAP2_MINV)
          << AV1_PROB_COST_SHIFT;
  return bits;
}

// If limits != NULL, calculates error for current restoration unit.
// Otherwise, calculates error for all units in the stack using stored limits.
static int64_t calc_finer_tile_search_error(const RestSearchCtxt *rsc,
                                            const RestorationTileLimits *limits,
                                            const AV1PixelRect *tile,
                                            RestorationUnitInfo *rui) {
  int64_t err = try_restoration_unit(rsc, limits, tile, rui);
  return err;
}

#define USE_WIENER_REFINEMENT_SEARCH 1
#define RD_WIENER_REFINEMENT_SEARCH 0
static int64_t finer_tile_search_wiener(RestSearchCtxt *rsc,
                                        const RestorationTileLimits *limits,
                                        const AV1PixelRect *tile,
                                        RestorationUnitInfo *rui,
                                        int wiener_win, int reduced_wiener_win,
                                        const WienerInfoBank *ref_wiener_bank) {
  (void)wiener_win;
  (void)ref_wiener_bank;
  const int plane_off = (WIENER_WIN - reduced_wiener_win) >> 1;
  int64_t err = calc_finer_tile_search_error(rsc, limits, tile, rui);
#if USE_WIENER_REFINEMENT_SEARCH
  WienerInfo *plane_wiener = &rui->wiener_info;

  const MACROBLOCK *const x = rsc->x;
#if RD_WIENER_REFINEMENT_SEARCH
  int64_t bits = count_wiener_bits(wiener_win, &x->mode_costs, plane_wiener,
                                   ref_wiener_bank);
#else
  int64_t bits = 0;
#endif  // RD_WIENER_REFINEMENT_SEARCH
  double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult, bits >> 4, err,
                                               rsc->cm->seq_params.bit_depth);
  int tap_min[] = { WIENER_FILT_TAP0_MINV, WIENER_FILT_TAP1_MINV,
                    WIENER_FILT_TAP2_MINV };
  int tap_max[] = { WIENER_FILT_TAP0_MAXV, WIENER_FILT_TAP1_MAXV,
                    WIENER_FILT_TAP2_MAXV };

  // printf("err  pre = %"PRId64"\n", err);
  const int start_step = 4;
  for (int s = start_step; s >= 1; s >>= 1) {
    for (int p = plane_off; p < WIENER_HALFWIN; ++p) {
      int skip = 0;
      do {
        if (plane_wiener->hfilter[p] - s >= tap_min[p]) {
          plane_wiener->hfilter[p] -= s;
          plane_wiener->hfilter[WIENER_WIN - p - 1] -= s;
          plane_wiener->hfilter[WIENER_HALFWIN] += 2 * s;
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
#if RD_WIENER_REFINEMENT_SEARCH
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#else
          int64_t bits2 = 0;
#endif  // RD_WIENER_REFINEMENT_SEARCH
          double cost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits2 >> 4, err2, rsc->cm->seq_params.bit_depth);
          if (cost2 > cost) {
            plane_wiener->hfilter[p] += s;
            plane_wiener->hfilter[WIENER_WIN - p - 1] += s;
            plane_wiener->hfilter[WIENER_HALFWIN] -= 2 * s;
          } else {
            cost = cost2;
            err = err2;
            skip = 1;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
      if (skip) break;
      do {
        if (plane_wiener->hfilter[p] + s <= tap_max[p]) {
          plane_wiener->hfilter[p] += s;
          plane_wiener->hfilter[WIENER_WIN - p - 1] += s;
          plane_wiener->hfilter[WIENER_HALFWIN] -= 2 * s;
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
#if RD_WIENER_REFINEMENT_SEARCH
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#else
          int64_t bits2 = 0;
#endif  // RD_WIENER_REFINEMENT_SEARCH
          double cost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits2 >> 4, err2, rsc->cm->seq_params.bit_depth);
          if (cost2 > cost) {
            plane_wiener->hfilter[p] -= s;
            plane_wiener->hfilter[WIENER_WIN - p - 1] -= s;
            plane_wiener->hfilter[WIENER_HALFWIN] += 2 * s;
          } else {
            cost = cost2;
            err = err2;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
    }
    for (int p = plane_off; p < WIENER_HALFWIN; ++p) {
      int skip = 0;
      do {
        if (plane_wiener->vfilter[p] - s >= tap_min[p]) {
          plane_wiener->vfilter[p] -= s;
          plane_wiener->vfilter[WIENER_WIN - p - 1] -= s;
          plane_wiener->vfilter[WIENER_HALFWIN] += 2 * s;
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
#if RD_WIENER_REFINEMENT_SEARCH
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#else
          int64_t bits2 = 0;
#endif  // RD_WIENER_REFINEMENT_SEARCH
          double cost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits2 >> 4, err2, rsc->cm->seq_params.bit_depth);
          if (cost2 > cost) {
            plane_wiener->vfilter[p] += s;
            plane_wiener->vfilter[WIENER_WIN - p - 1] += s;
            plane_wiener->vfilter[WIENER_HALFWIN] -= 2 * s;
          } else {
            cost = cost2;
            err = err2;
            skip = 1;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
      if (skip) break;
      do {
        if (plane_wiener->vfilter[p] + s <= tap_max[p]) {
          plane_wiener->vfilter[p] += s;
          plane_wiener->vfilter[WIENER_WIN - p - 1] += s;
          plane_wiener->vfilter[WIENER_HALFWIN] -= 2 * s;
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
#if RD_WIENER_REFINEMENT_SEARCH
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#else
          int64_t bits2 = 0;
#endif  // RD_WIENER_REFINEMENT_SEARCH
          double cost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits2 >> 4, err2, rsc->cm->seq_params.bit_depth);
          if (cost2 > cost) {
            plane_wiener->vfilter[p] -= s;
            plane_wiener->vfilter[WIENER_WIN - p - 1] -= s;
            plane_wiener->vfilter[WIENER_HALFWIN] += 2 * s;
          } else {
            cost = cost2;
            err = err2;
            // At the highest step size continue moving in the same direction
            if (s == start_step) continue;
          }
        }
        break;
      } while (1);
    }
  }
  // printf("err post = %"PRId64"\n", err);
#endif  // USE_WIENER_REFINEMENT_SEARCH
  return err;
}

static AOM_INLINE void search_wiener_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_seq, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const MACROBLOCK *const x = rsc->x;
  const int64_t bits_none = x->mode_costs.wiener_restore_cost[0];

  // Skip Wiener search for low variance contents
  if (rsc->lpf_sf->prune_wiener_based_on_src_var) {
    const int scale[3] = { 0, 1, 2 };
    // Obtain the normalized Qscale
    const int qs = av1_dc_quant_QTX(rsc->cm->quant_params.base_qindex, 0,
                                    rsc->cm->seq_params.base_y_dc_delta_q,
                                    rsc->cm->seq_params.bit_depth) >>
                   3;
    // Derive threshold as sqr(normalized Qscale) * scale / 16,
    const uint64_t thresh =
        (qs * qs * scale[rsc->lpf_sf->prune_wiener_based_on_src_var]) >> 4;
    const uint64_t src_var = var_restoration_unit(limits, rsc->src, rsc->plane);
    // Do not perform Wiener search if source variance is lower than threshold
    // or if the reconstruction error is zero
    int prune_wiener = (src_var < thresh) || (rusi->sse[RESTORE_NONE] == 0);
    if (prune_wiener) {
      rsc->bits += bits_none;
      rsc->sse += rusi->sse[RESTORE_NONE];
      rusi->best_rtype[RESTORE_WIENER - 1] = RESTORE_NONE;
      rusi->sse[RESTORE_WIENER] = INT64_MAX;
      if (rsc->lpf_sf->prune_sgr_based_on_wiener == 2) rusi->skip_sgr_eval = 1;
      return;
    }
  }

  const int wiener_win =
      (rsc->plane == AOM_PLANE_Y) ? WIENER_WIN : WIENER_WIN_CHROMA;

  int reduced_wiener_win = wiener_win;
  if (rsc->lpf_sf->reduce_wiener_window_size) {
    reduced_wiener_win =
        (rsc->plane == AOM_PLANE_Y) ? WIENER_WIN_REDUCED : WIENER_WIN_CHROMA;
  }

  int64_t M[WIENER_WIN2];
  int64_t H[WIENER_WIN2 * WIENER_WIN2];
  int32_t vfilter[WIENER_WIN], hfilter[WIENER_WIN];

  const AV1_COMMON *const cm = rsc->cm;
  av1_compute_stats_highbd(reduced_wiener_win, rsc->dgd_buffer, rsc->src_buffer,
                           limits->h_start, limits->h_end, limits->v_start,
                           limits->v_end, rsc->dgd_stride, rsc->src_stride, M,
                           H, cm->seq_params.bit_depth);

  if (!wiener_decompose_sep_sym(reduced_wiener_win, M, H, vfilter, hfilter)) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_WIENER - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_WIENER] = INT64_MAX;
    if (rsc->lpf_sf->prune_sgr_based_on_wiener == 2) rusi->skip_sgr_eval = 1;
    return;
  }

  RestorationUnitInfo rui;
  memset(&rui, 0, sizeof(rui));
  rui.restoration_type = RESTORE_WIENER;
  finalize_sym_filter(reduced_wiener_win, vfilter, rui.wiener_info.vfilter);
  finalize_sym_filter(reduced_wiener_win, hfilter, rui.wiener_info.hfilter);

  // Filter score computes the value of the function x'*A*x - x'*b for the
  // learned filter and compares it against identity filer. If there is no
  // reduction in the function, the filter is reverted back to identity
  if (compute_score(reduced_wiener_win, M, H, rui.wiener_info.vfilter,
                    rui.wiener_info.hfilter) > 0) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_WIENER - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_WIENER] = INT64_MAX;
    if (rsc->lpf_sf->prune_sgr_based_on_wiener == 2) rusi->skip_sgr_eval = 1;
    return;
  }

  aom_clear_system_state();

  rusi->sse[RESTORE_WIENER] =
      finer_tile_search_wiener(rsc, limits, &rsc->tile_rect, &rui, wiener_win,
                               reduced_wiener_win, &rsc->wiener_bank);
  rusi->wiener_info = rui.wiener_info;

  if (reduced_wiener_win != WIENER_WIN) {
    assert(rui.wiener_info.vfilter[0] == 0 &&
           rui.wiener_info.vfilter[WIENER_WIN - 1] == 0);
    assert(rui.wiener_info.hfilter[0] == 0 &&
           rui.wiener_info.hfilter[WIENER_WIN - 1] == 0);
  }

  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE],
      rsc->cm->seq_params.bit_depth);

  const int64_t bits_wiener =
      x->mode_costs.wiener_restore_cost[1] +
      count_wiener_bits(wiener_win, &x->mode_costs, &rusi->wiener_info,
                        &rsc->wiener_bank);

  double cost_wiener = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_wiener >> 4, rusi->sse[RESTORE_WIENER],
      rsc->cm->seq_params.bit_depth);

  RestorationType rtype =
      (cost_wiener < cost_none) ? RESTORE_WIENER : RESTORE_NONE;
  rusi->best_rtype[RESTORE_WIENER - 1] = rtype;

  // Set 'skip_sgr_eval' based on rdcost ratio of RESTORE_WIENER and
  // RESTORE_NONE or based on best_rtype
  if (rsc->lpf_sf->prune_sgr_based_on_wiener == 1) {
    rusi->skip_sgr_eval = cost_wiener > (1.01 * cost_none);
  } else if (rsc->lpf_sf->prune_sgr_based_on_wiener == 2) {
    rusi->skip_sgr_eval = rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_NONE;
  }

  rsc->sse += rusi->sse[rtype];
  rsc->bits += (cost_wiener < cost_none) ? bits_wiener : bits_none;
  if (cost_wiener < cost_none)
    av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
}

static AOM_INLINE void search_norestore_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_seq, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  rusi->sse[RESTORE_NONE] = sse_restoration_unit(
      limits, rsc->src, &rsc->cm->cur_frame->buf, rsc->plane);

  rsc->sse += rusi->sse[RESTORE_NONE];
}

static int get_switchable_restore_cost(const AV1_COMMON *const cm,
                                       const MACROBLOCK *const x, int plane,
                                       int rest_type) {
  (void)cm;
  (void)plane;
  return x->mode_costs.switchable_restore_cost[rest_type];
}

static int64_t count_switchable_bits(int rest_type, RestSearchCtxt *rsc,
                                     RestUnitSearchInfo *rusi) {
  const MACROBLOCK *const x = rsc->x;
  const int wiener_win =
      (rsc->plane == AOM_PLANE_Y) ? WIENER_WIN : WIENER_WIN_CHROMA;
  if (rest_type > RESTORE_NONE) {
    if (rusi->best_rtype[rest_type - 1] == RESTORE_NONE)
      rest_type = RESTORE_NONE;
  }
  int64_t coeff_bits = 0;
  switch (rest_type) {
    case RESTORE_NONE: coeff_bits = 0; break;
    case RESTORE_WIENER:
      coeff_bits = count_wiener_bits(wiener_win, &x->mode_costs,
                                     &rusi->wiener_info, &rsc->wiener_bank);
      break;
    case RESTORE_SGRPROJ:
      coeff_bits = count_sgrproj_bits(&x->mode_costs, &rusi->sgrproj_info,
                                      &rsc->sgrproj_bank);
      break;
    default: assert(0); break;
  }
  const int64_t bits =
      get_switchable_restore_cost(rsc->cm, x, rsc->plane, rest_type) +
      coeff_bits;
  return bits;
}

static void search_switchable_visitor(const RestorationTileLimits *limits,
                                      const AV1PixelRect *tile_rect,
                                      int rest_unit_idx, int rest_unit_idx_seq,
                                      void *priv, int32_t *tmpbuf,
                                      RestorationLineBuffers *rlbs) {
  (void)limits;
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const MACROBLOCK *const x = rsc->x;

  double best_cost = 0;
  int64_t best_bits = 0;
  RestorationType best_rtype = RESTORE_NONE;

  for (RestorationType r = 0; r < RESTORE_SWITCHABLE_TYPES; ++r) {
    // Check for the condition that wiener or sgrproj search could not
    // find a solution or the solution was worse than RESTORE_NONE.
    // In either case the best_rtype will be set as RESTORE_NONE. These
    // should be skipped from the test below.
    if (r > RESTORE_NONE) {
      if (rusi->best_rtype[r - 1] == RESTORE_NONE) continue;
    }

    const int64_t sse = rusi->sse[r];
    int64_t bits = count_switchable_bits(r, rsc, rusi);
    double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult, bits >> 4, sse,
                                                 rsc->cm->seq_params.bit_depth);
    if (r == RESTORE_SGRPROJ && rusi->sgrproj_info.ep < 10)
      cost *= (1 + DUAL_SGR_PENALTY_MULT * rsc->lpf_sf->dual_sgr_penalty_level);
    if (r == 0 || cost < best_cost) {
      best_cost = cost;
      best_bits = bits;
      best_rtype = r;
    }
  }

  rusi->best_rtype[RESTORE_SWITCHABLE - 1] = best_rtype;
  rsc->sse += rusi->sse[best_rtype];
  rsc->bits += best_bits;

  if (best_rtype == RESTORE_WIENER) {
    av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
  } else if (best_rtype == RESTORE_SGRPROJ) {
    av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
  }
}

static void adjust_frame_rtype(RestorationInfo *rsi, int plane_ntiles,
                               RestSearchCtxt *rsc, const ToolCfg *tool_cfg) {
  (void)rsc;
  (void)tool_cfg;
  if (rsi->frame_restoration_type == RESTORE_NONE) return;
  int tool_count[RESTORE_SWITCHABLE_TYPES] = { 0 };
  for (int u = 0; u < plane_ntiles; ++u) {
    RestorationType rt = rsi->unit_info[u].restoration_type;
    tool_count[rt]++;
  }
  int ntools = 0;
  RestorationType rused = RESTORE_NONE;
  for (int j = 1; j < RESTORE_SWITCHABLE_TYPES; ++j) {
    if (tool_count[j] > 0) {
      ntools++;
      rused = j;
      assert(IMPLIES(j == RESTORE_WIENER, tool_cfg->enable_wiener));
      assert(IMPLIES(j == RESTORE_SGRPROJ, tool_cfg->enable_sgrproj));
    }
  }
  rsi->frame_restoration_type = ntools < 2 ? rused : RESTORE_SWITCHABLE;
  return;
}

static AOM_INLINE void copy_unit_info(RestorationType frame_rtype,
                                      const RestUnitSearchInfo *rusi,
                                      RestorationUnitInfo *rui,
                                      RestSearchCtxt *rsc) {
  (void)rsc;
  assert(frame_rtype > 0);
  rui->restoration_type = frame_rtype == RESTORE_NONE
                              ? RESTORE_NONE
                              : rusi->best_rtype[frame_rtype - 1];
  if (rui->restoration_type == RESTORE_WIENER) {
    rui->wiener_info = rusi->wiener_info;
  } else if (rui->restoration_type == RESTORE_SGRPROJ) {
    rui->sgrproj_info = rusi->sgrproj_info;
  }
}

// Calls visitor function fun() for one specific RU in frame
// Note that RU-tiles are different from coded tiles since the RU sizes can be
// different from Sb sizes and also because there could be super-resolution.
// A RU-tile is a rectangle in the upsampled domain that includes all RUs
// that are signaled for the SBs in a given tile in the coded domain.
// This function processes the vistor function fun() for all RUs within
// the Ru-tile in the order in which they are signaled in the bit-stream.
static void process_one_rutile(RestSearchCtxt *rsc, int tile_row, int tile_col,
                               int *processed, rest_unit_visitor_t fun) {
  const int is_uv = rsc->plane > 0;
  const int ss_y = is_uv && rsc->cm->seq_params.subsampling_y;
  const RestorationInfo *rsi = &rsc->cm->rst_info[rsc->plane];
  const int ru_size = rsi->restoration_unit_size;
  TileInfo tile_info;
  av1_tile_set_row(&tile_info, rsc->cm, tile_row);
  av1_tile_set_col(&tile_info, rsc->cm, tile_col);
  assert(tile_info.mi_row_start < tile_info.mi_row_end);
  assert(tile_info.mi_col_start < tile_info.mi_col_end);

  reset_rsc(rsc);
  rsc_on_tile(rsc, *processed);
  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += rsc->cm->seq_params.mib_size) {
    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += rsc->cm->seq_params.mib_size) {
      int rrow0, rrow1, rcol0, rcol1;
      if (av1_loop_restoration_corners_in_sb(
              rsc->cm, rsc->plane, mi_row, mi_col, rsc->cm->seq_params.sb_size,
              &rcol0, &rcol1, &rrow0, &rrow1)) {
        // RU domain rectangle for the coded SB
        AV1PixelRect ru_sb_rect = av1_get_rutile_rect(
            rsc->cm, is_uv, rrow0, rrow1, rcol0, rcol1, ru_size, ru_size);
        const int unit_idx0 = rrow0 * rsi->horz_units_per_tile + rcol0;
        av1_foreach_rest_unit_in_sb(&ru_sb_rect, unit_idx0, rcol1 - rcol0,
                                    rrow1 - rrow0, rsi->horz_units_per_tile,
                                    ru_size, ss_y, rsc->plane, fun, rsc,
                                    rsc->cm->rst_tmpbuf, NULL, processed);
      }
    }
  }
}

// Calls visitor function fun() for all RUs in frame in RUtile-by-RUtile order
static void process_by_rutile(RestSearchCtxt *rsc, rest_unit_visitor_t fun) {
  int processed = 0;
  for (int tile_row = 0; tile_row < rsc->cm->tiles.rows; tile_row++) {
    for (int tile_col = 0; tile_col < rsc->cm->tiles.cols; tile_col++) {
      process_one_rutile(rsc, tile_row, tile_col, &processed, fun);
    }
  }
}

// Calls visitor function fun() for all RUs in frame in RUtile-by-RUtile order,
// aggregates the bits and sse returned in rsc for each RUtile, and returns
// the overall RD cost for the frame over all RUs in all RUtiles.
static double process_rd_by_rutile(RestSearchCtxt *rsc,
                                   rest_unit_visitor_t fun) {
  int processed = 0;
  int64_t total_bits = 0;
  int64_t total_sse = 0;
  for (int tile_row = 0; tile_row < rsc->cm->tiles.rows; tile_row++) {
    for (int tile_col = 0; tile_col < rsc->cm->tiles.cols; tile_col++) {
      process_one_rutile(rsc, tile_row, tile_col, &processed, fun);
      total_bits += rsc->bits;
      total_sse += rsc->sse;
    }
  }
  return RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, total_bits >> 4,
                                        total_sse,
                                        rsc->cm->seq_params.bit_depth);
}

static void gather_stats_rest_type(RestSearchCtxt *rsc, RestorationType rtype) {
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = { NULL, NULL, NULL,
                                                           NULL };
  if (funs[rtype]) process_by_rutile(rsc, funs[rtype]);
}

static double search_rest_type(RestSearchCtxt *rsc, RestorationType rtype) {
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = {
    search_norestore_visitor, search_wiener_visitor, search_sgrproj_visitor,
    search_switchable_visitor
  };

  if (funs[rtype])
    return process_rd_by_rutile(rsc, funs[rtype]);
  else
    return DBL_MAX;
}

static void copy_unit_info_visitor(const RestorationTileLimits *limits,
                                   const AV1PixelRect *tile_rect,
                                   int rest_unit_idx, int rest_unit_idx_seq,
                                   void *priv, int32_t *tmpbuf,
                                   RestorationLineBuffers *rlbs) {
  (void)limits;
  (void)tile_rect;
  (void)rest_unit_idx_seq;
  (void)tmpbuf;
  (void)rlbs;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  const RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];
  const RestorationInfo *rsi = &rsc->cm->rst_info[rsc->plane];
  copy_unit_info(rsi->frame_restoration_type, rusi,
                 &rsi->unit_info[rest_unit_idx], rsc);
}

static void finalize_frame_and_unit_info(RestorationType frame_rtype,
                                         RestorationInfo *rsi,
                                         RestSearchCtxt *rsc) {
  rsi->frame_restoration_type = frame_rtype;
  if (frame_rtype != RESTORE_NONE) {
    process_by_rutile(rsc, copy_unit_info_visitor);
  }
}

static int rest_tiles_in_plane(const AV1_COMMON *cm, int plane) {
  const RestorationInfo *rsi = &cm->rst_info[plane];
  return rsi->units_per_tile;
}

void av1_pick_filter_restoration(const YV12_BUFFER_CONFIG *src, AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  const int num_planes = av1_num_planes(cm);
  assert(!cm->features.all_lossless);

  av1_fill_lr_rates(&x->mode_costs, x->e_mbd.tile_ctx);

  int ntiles[2];
  for (int is_uv = 0; is_uv < 2; ++is_uv)
    ntiles[is_uv] = rest_tiles_in_plane(cm, is_uv);

  assert(ntiles[1] <= ntiles[0]);
  RestUnitSearchInfo *rusi =
      (RestUnitSearchInfo *)aom_memalign(16, sizeof(*rusi) * ntiles[0]);

  // If the restoration unit dimensions are not multiples of
  // rsi->restoration_unit_size then some elements of the rusi array may be
  // left uninitialised when we reach copy_unit_info(...). This is not a
  // problem, as these elements are ignored later, but in order to quiet
  // Valgrind's warnings we initialise the array below.
  memset(rusi, 0, sizeof(*rusi) * ntiles[0]);
  x->rdmult = cpi->rd.RDMULT;

  RestSearchCtxt rsc;
  const int plane_start = AOM_PLANE_Y;
  const int plane_end = num_planes > 1 ? AOM_PLANE_V : AOM_PLANE_Y;
  for (int plane = plane_start; plane <= plane_end; ++plane) {
    init_rsc(src, &cpi->common, x, &cpi->sf.lpf_sf, plane, rusi,
             &cpi->trial_frame_rst, &rsc);

    const int plane_ntiles = ntiles[plane > 0];
    const RestorationType num_rtypes =
        (plane_ntiles > 1) ? RESTORE_TYPES : RESTORE_SWITCHABLE_TYPES;

    double best_cost = 0;
    RestorationType best_rtype = RESTORE_NONE;

    if (!cpi->sf.lpf_sf.disable_loop_restoration_chroma || !plane) {
      av1_extend_frame(rsc.dgd_buffer, rsc.plane_width, rsc.plane_height,
                       rsc.dgd_stride, RESTORATION_BORDER, RESTORATION_BORDER);

      for (RestorationType r = 0; r < num_rtypes; ++r) {
        const ToolCfg *const tool_cfg = &cpi->oxcf.tool_cfg;
        switch (r) {
          case RESTORE_WIENER:
            if (!tool_cfg->enable_wiener) continue;
            break;
          case RESTORE_SGRPROJ:
            if (!tool_cfg->enable_sgrproj) continue;
            break;
          default: break;
        };

        gather_stats_rest_type(&rsc, r);

        double cost = search_rest_type(&rsc, r);

        if (r == 0 || cost < best_cost) {
          best_cost = cost;
          best_rtype = r;
        }
      }
    }

    finalize_frame_and_unit_info(best_rtype, &cm->rst_info[plane], &rsc);
    adjust_frame_rtype(&cm->rst_info[plane], plane_ntiles, &rsc,
                       &cpi->oxcf.tool_cfg);
  }

  aom_free(rusi);
}
