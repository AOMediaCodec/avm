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
#include "aom_dsp/mathutils.h"
#include "aom_dsp/psnr.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/quant_common.h"
#include "av1/common/restoration.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/picklpf.h"
#include "av1/encoder/pickrst.h"

#include "third_party/vector/vector.h"

// Search level 0 - search all drl candidates
// Search level 1 - search drl candidates 0 and the best one for the current RU
// Search level 2 - search only the best drl candidate for the current RU
#define MERGE_DRL_SEARCH_LEVEL 1

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

// Number of elements needed in the temporary buffer for
// compute_wienerns_filter
#define WIENERNS_A_SIZE (WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX)
#define WIENERNS_b_SIZE (WIENERNS_TAPS_MAX)
#define WIENERNS_R_SIZE \
  (WIENERNS_MAX_CLASSES * WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX)
#define WIENERNS_B_SIZE (WIENERNS_MAX_CLASSES * WIENERNS_TAPS_MAX)
#define WIENERNS_TMPBUF_SIZE (WIENERNS_R_SIZE + WIENERNS_B_SIZE + 1)

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
  WienerNonsepInfo wienerns_info;

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
#if CONFIG_COMBINE_PC_NS_WIENER
  // Indicates whether classification has been computed and buffered.
  int classification_is_buffered;
  // Helps prevent search_switchable from punishing frame level filters
  int adjust_switchable_for_frame_filters;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  // sse and bits are initialised by reset_rsc in search_rest_type
  int64_t sse;
  int64_t bits;
  int tile_y0, tile_stripe0;
  // Helps convert tile-localized RU indices to frame RU indices.
  int ru_idx_base;
  // Number of RUs in tile
  int num_rus_in_tile;

  // sgrproj and wiener are initialised by rsc_on_tile when starting the first
  // tile in the frame.
  WienerInfoBank wiener_bank;
  SgrprojInfoBank sgrproj_bank;
  WienerNonsepInfoBank wienerns_bank;

  // Vector storing statistics for all RUs.
  Vector *wienerns_stats;

  // Number of classes in the initial wienerns stat calculation.
  int num_stats_classes;
  // Number of classes in the wienerns filtering calculation.
  int num_filter_classes;
  // Best number of classes for frame filters.
  int best_num_filter_classes;
  // Whether frame-level filters are on or off.
  int frame_filters_on;

  WienerNonsepInfoBank frame_filter_bank;
  double frame_filter_cost;
  double frame_filters_total_cost;
  int num_wiener_nonsep;  // debug: number of RESTORE_WIENER_NONSEP RUs.

  const uint16_t *luma;
#if ISSUE_253
  const uint16_t *luma_stat;
#endif  // ISSUE_253

  int luma_stride;

  // Temporary storage used by compute_wienerns_filter
  double *wienerns_tmpbuf;

  bool tskip_zero_flag;

  // This vector holds the most recent list of units with merged coefficients.
  Vector *unit_stack;
  // This vector holds a list of rest_unit indices to be considered for merging
  // for a given drl candidate to be examined. Note that the unit_stack above
  // includes all previous RUs covering all entries in the drl list, but only
  // a subset needs to be considered for merging for a given drl candidate.
  Vector *unit_indices;
#if CONFIG_TEMP_LR
  // whether frame filter is predicted from a reference picture
  uint8_t temporal_pred_flag;
  // reference picture index for frame level filter prediction
  uint8_t rst_ref_pic_idx;
  WienerNonsepInfo frame_filters;
#endif  // CONFIG_TEMP_LR
  AV1PixelRect tile_rect;
} RestSearchCtxt;

// RU statistics for solving Wiener filters.
typedef struct RstUnitStats {
  double A[WIENERNS_R_SIZE];
  double b[WIENERNS_B_SIZE];
  double weight;  // Importance of this stat in the frame.
  int64_t real_sse;
  int num_stats_classes;
  RestorationTileLimits limits;
  int num_pixels_in_class[WIENERNS_MAX_CLASSES];  // debug.
  int ru_idx;                                     // debug.
  int ru_idx_in_tile;                             // debug.
  int plane;                                      // debug.
} RstUnitStats;

typedef struct RstUnitSnapshot {
  RestorationTileLimits limits;
  int rest_unit_idx;  // update filter value and sse as needed
  int64_t current_sse;
  int64_t current_bits;
  int64_t merge_sse;
  int64_t merge_bits;
  int64_t merge_sse_cand;
  int64_t merge_bits_cand;
  // Wiener filter info
  int64_t M[WIENER_WIN2];
  int64_t H[WIENER_WIN2 * WIENER_WIN2];
  // Wiener filter info
  WienerInfoBank ref_wiener_bank;
  // Sgrproj filter info
  SgrprojInfoBank ref_sgrproj_bank;
  // Nonseparable Wiener filter info.
  // Pointers to respective stats in RstUnitStats.
  const double *A;
  const double *b;
  WienerNonsepInfoBank ref_wienerns_bank;
} RstUnitSnapshot;

static AOM_INLINE void reset_all_banks(RestSearchCtxt *rsc) {
  av1_reset_wiener_bank(&rsc->wiener_bank, rsc->plane != AOM_PLANE_Y);
  av1_reset_sgrproj_bank(&rsc->sgrproj_bank);
  av1_reset_wienerns_bank(&rsc->wienerns_bank,
                          rsc->cm->quant_params.base_qindex,
                          rsc->num_filter_classes, rsc->plane != AOM_PLANE_Y);
}

static void get_ru_limits_in_tile(const AV1_COMMON *cm, int plane, int tile_row,
                                  int tile_col, int *ru_row_start,
                                  int *ru_row_end, int *ru_col_start,
                                  int *ru_col_end) {
  TileInfo tile_info;
  av1_tile_set_row(&tile_info, cm, tile_row);
  av1_tile_set_col(&tile_info, cm, tile_col);
  assert(tile_info.mi_row_start < tile_info.mi_row_end);
  assert(tile_info.mi_col_start < tile_info.mi_col_end);

  *ru_row_start = 0;
  *ru_col_start = 0;
  *ru_row_end = 0;
  *ru_col_end = 0;
  int rrow0, rrow1, rcol0, rcol1;
  // Scan SBs row by row, left to right to find first SB that has RU info in it.
  int found = 0;
  for (int mi_row = tile_info.mi_row_start;
       mi_row < tile_info.mi_row_end && !found; mi_row += cm->mib_size) {
    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += cm->mib_size) {
      if (av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col,
                                             cm->sb_size, &rcol0, &rcol1,
                                             &rrow0, &rrow1)) {
        *ru_row_start = rrow0;  // this is the RU row start limit in RU terms.
        *ru_col_start = rcol0;  // this is the RU col start limit in RU terms.
        found = 1;
        break;
      }
    }
  }
  // Scan SBs in reverse row by row, right to left to find first SB that has RU
  // info in it.
  found = 0;
  const int sb_mi_row_end =
      tile_info.mi_row_end - 1 - (tile_info.mi_row_end - 1) % cm->mib_size;
  const int sb_mi_col_end =
      tile_info.mi_col_end - 1 - (tile_info.mi_col_end - 1) % cm->mib_size;
  for (int mi_row = sb_mi_row_end; mi_row >= tile_info.mi_row_start && !found;
       mi_row -= cm->mib_size) {
    for (int mi_col = sb_mi_col_end; mi_col >= tile_info.mi_col_start;
         mi_col -= cm->mib_size) {
      if (av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col,
                                             cm->sb_size, &rcol0, &rcol1,
                                             &rrow0, &rrow1)) {
        *ru_row_end = rrow1;  // this is the RU row end limit in RU terms.
        *ru_col_end = rcol1;  // this is the RU col end limit in RU terms.
        found = 1;
        break;
      }
    }
  }
}

static AOM_INLINE void rsc_on_tile(void *priv, int idx_base, int tile_row,
                                   int tile_col) {
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  reset_all_banks(rsc);
  rsc->tile_stripe0 = 0;
  rsc->ru_idx_base = idx_base;
  int ru_row_start, ru_row_end;
  int ru_col_start, ru_col_end;
  get_ru_limits_in_tile(rsc->cm, rsc->plane, tile_row, tile_col, &ru_row_start,
                        &ru_row_end, &ru_col_start, &ru_col_end);
  rsc->num_rus_in_tile =
      (ru_row_end - ru_row_start) * (ru_col_end - ru_col_start);
}

static AOM_INLINE void reset_rsc(RestSearchCtxt *rsc) {
  rsc->sse = 0;
  rsc->bits = 0;
  aom_vector_clear(rsc->unit_stack);
  aom_vector_clear(rsc->unit_indices);
}

static AOM_INLINE void init_rsc(const YV12_BUFFER_CONFIG *src,
                                const AV1_COMMON *cm, const MACROBLOCK *x,
                                const LOOP_FILTER_SPEED_FEATURES *lpf_sf,
                                int plane, RestUnitSearchInfo *rusi,
                                Vector *unit_stack, Vector *unit_indices,
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
  rsc->unit_stack = unit_stack;
  rsc->unit_indices = unit_indices;
  rsc->num_stats_classes = default_num_classes(plane);
  rsc->num_filter_classes = rsc->num_stats_classes;
  rsc->best_num_filter_classes = rsc->num_filter_classes;
  rsc->frame_filters_on = 0;
  rsc->num_wiener_nonsep = 0;
#if CONFIG_ENABLE_SR
  rsc->tskip_zero_flag = av1_superres_scaled(cm);
#else
  rsc->tskip_zero_flag = 0;
#endif
#if CONFIG_COMBINE_PC_NS_WIENER
  rsc->classification_is_buffered = 0;
  rsc->adjust_switchable_for_frame_filters = 0;
#endif  // CONFIG_COMBINE_PC_NS_WIENER
  reset_all_banks(rsc);
}

static int64_t try_restoration_unit(const RestSearchCtxt *rsc,
                                    const RestorationTileLimits *limits,
                                    const AV1PixelRect *tile_rect,
                                    const RestorationUnitInfo *rui) {
  const AV1_COMMON *const cm = rsc->cm;
  const int plane = rsc->plane;
  const int is_uv = plane > 0;
  const RestorationInfo *rsi = &cm->rst_info[plane];
#if ISSUE_253
  RestorationLineBuffers *rlbs = aom_malloc(sizeof(RestorationLineBuffers));
  if (rlbs == NULL)
    fprintf(stderr, "rlbs buffer does not allocate successfully\n");
#else
  RestorationLineBuffers rlbs;
#endif
  const int bit_depth = cm->seq_params.bit_depth;

  const YV12_BUFFER_CONFIG *fts = &cm->cur_frame->buf;
  // TODO(yunqing): For now, only use optimized LR filter in decoder. Can be
  // also used in encoder.
  const int optimized_lr = 0;
  av1_loop_restoration_filter_unit(
      limits, rui, &rsi->boundaries,
#if ISSUE_253
      rlbs,
#else
      &rlbs,
#endif
      tile_rect, rsc->tile_stripe0, is_uv && cm->seq_params.subsampling_x,
      is_uv && cm->seq_params.subsampling_y, bit_depth, fts->buffers[plane],
      fts->strides[is_uv], rsc->dst->buffers[plane], rsc->dst->strides[is_uv],
      cm->rst_tmpbuf, optimized_lr);

#if ISSUE_253
  if (rlbs != NULL) aom_free(rlbs);
#endif
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
    Vector *current_unit_stack = rsc->unit_stack;
    Vector *current_unit_indices = rsc->unit_indices;
    assert(current_unit_stack->size > 0);
    assert(current_unit_indices->size > 0);
    int n = 0;
    int idx = *(int *)aom_vector_const_get(current_unit_indices, n);
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      if (old_unit->rest_unit_idx == idx) {
        RestorationTileLimits old_limits = old_unit->limits;
        dat = rsc->dgd_buffer + old_limits.v_start * rsc->dgd_stride +
              old_limits.h_start;
        src = rsc->src_buffer + old_limits.v_start * rsc->src_stride +
              old_limits.h_start;
        width = old_limits.h_end - old_limits.h_start;
        height = old_limits.v_end - old_limits.v_start;
        flt_stride = ((width + 7) & ~7) + 8;
        err += compute_sgrproj_err(dat, width, height, dat_stride, src,
                                   src_stride, bit_depth, pu_width, pu_height,
                                   ep, flt0, flt1, flt_stride, exqd);
        n++;
        if (n >= (int)current_unit_indices->size) break;
        idx = *(int *)aom_vector_const_get(current_unit_indices, n);
      }
    }
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
  int exqd[2] = { 0 };
  int bestxqd[2] = { 0, 0 };
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
  const int ref = sgrproj_info->bank_ref;
  const SgrprojInfo *ref_sgrproj_info =
      av1_constref_from_sgrproj_bank(bank, ref);
  const int equal_ref = check_sgrproj_eq(sgrproj_info, ref_sgrproj_info);
  for (int k = 0; k < AOMMAX(0, bank->bank_size - 1); ++k) {
    const int match = (k == ref);
    bits += (1 << AV1_PROB_COST_SHIFT);
    if (match) break;
  }
  bits += mode_costs->merged_param_cost[equal_ref];
  if (equal_ref) return bits;
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

static int64_t count_sgrproj_bits_set(const ModeCosts *mode_costs,
                                      SgrprojInfo *info,
                                      const SgrprojInfoBank *bank) {
  int64_t best_bits = INT64_MAX;
  int best_ref = -1;
  for (int ref = 0; ref < AOMMAX(1, bank->bank_size); ++ref) {
    info->bank_ref = ref;
    const int64_t bits = count_sgrproj_bits(mode_costs, info, bank);
    if (bits < best_bits) {
      best_bits = bits;
      best_ref = ref;
    }
  }
  info->bank_ref = AOMMAX(0, best_ref);
  return best_bits;
}

static AOM_INLINE void search_sgrproj_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_in_rutile, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)rlbs;
  (void)rest_unit_idx_in_rutile;
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

  Vector *current_unit_stack = rsc->unit_stack;
  int64_t bits_nomerge_base =
      x->mode_costs.sgrproj_restore_cost[1] +
      count_sgrproj_bits_set(&x->mode_costs, &rusi->sgrproj_info,
                             &rsc->sgrproj_bank);

  const int bank_ref_base = rusi->sgrproj_info.bank_ref;
  // Only test the reference in rusi->sgrproj_info.bank_ref, generated from
  // the count call above.

  double cost_nomerge_base = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_nomerge_base >> 4, rusi->sse[RESTORE_SGRPROJ], bit_depth);
  const int bits_min = x->mode_costs.sgrproj_restore_cost[1] +
                       x->mode_costs.merged_param_cost[1] +
                       (1 << AV1_PROB_COST_SHIFT);
  const double cost_min = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_min >> 4, rusi->sse[RESTORE_SGRPROJ], bit_depth);
  const double cost_nomerge_thr = (cost_nomerge_base + 3 * cost_min) / 4;
  RestorationType rtype =
      (cost_none <= cost_nomerge_thr) ? RESTORE_NONE : RESTORE_SGRPROJ;
  if (cost_none <= cost_nomerge_thr) {
    bits_nomerge_base = bits_none;
    cost_nomerge_base = cost_none;
  }

  RstUnitSnapshot unit_snapshot;
  memset(&unit_snapshot, 0, sizeof(unit_snapshot));
  unit_snapshot.limits = *limits;
  unit_snapshot.rest_unit_idx = rest_unit_idx;
  rusi->best_rtype[RESTORE_SGRPROJ - 1] = rtype;
  rsc->sse += rusi->sse[rtype];
  rsc->bits += bits_nomerge_base;
  unit_snapshot.current_sse = rusi->sse[rtype];
  unit_snapshot.current_bits = bits_nomerge_base;
  // Only matters for first unit in stack.
  unit_snapshot.ref_sgrproj_bank = rsc->sgrproj_bank;
  // If current_unit_stack is empty, we can leave early.
  if (aom_vector_is_empty(current_unit_stack)) {
    if (rtype == RESTORE_SGRPROJ)
      av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    return;
  }

  // Handles special case where no-merge filter is equal to merged
  // filter for the stack - we don't want to perform another merge and
  // get a less optimal filter, but we want to continue building the stack.
  int equal_ref;
  if (rtype == RESTORE_SGRPROJ &&
      (equal_ref = check_sgrproj_bank_eq(&rsc->sgrproj_bank,
                                         &rusi->sgrproj_info)) >= 0) {
    rsc->bits -= bits_nomerge_base;
    rusi->sgrproj_info.bank_ref = equal_ref;
    unit_snapshot.current_bits =
        x->mode_costs.sgrproj_restore_cost[1] +
        count_sgrproj_bits(&x->mode_costs, &rusi->sgrproj_info,
                           &rsc->sgrproj_bank);
    rsc->bits += unit_snapshot.current_bits;
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    return;
  }

  // Push current unit onto stack.
  aom_vector_push_back(current_unit_stack, &unit_snapshot);
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;

  double cost_merge = DBL_MAX;
  double cost_nomerge = 0;
  int begin_idx = -1;
  int bank_ref = -1;
  RestorationUnitInfo rui_temp;

  // Trial start
  for (int bank_ref_cand = 0;
       bank_ref_cand < AOMMAX(1, rsc->sgrproj_bank.bank_size);
       bank_ref_cand++) {
#if MERGE_DRL_SEARCH_LEVEL == 1
    if (bank_ref_cand != 0 && bank_ref_cand != bank_ref_base) continue;
#elif MERGE_DRL_SEARCH_LEVEL == 2
    if (bank_ref_cand != bank_ref_base) continue;
#else
    (void)bank_ref_base;
#endif
    const SgrprojInfo *ref_sgrproj_info_cand =
        av1_constref_from_sgrproj_bank(&rsc->sgrproj_bank, bank_ref_cand);
    SgrprojInfo ref_sgrproj_info_tmp = *ref_sgrproj_info_cand;

    // Iterate once to get the begin unit of the run
    int begin_idx_cand = -1;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == last_idx) continue;
      if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
          check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info_cand)) {
        if (check_sgrproj_bank_eq(&old_unit->ref_sgrproj_bank,
                                  ref_sgrproj_info_cand) == -1) {
          begin_idx_cand = old_unit->rest_unit_idx;
        }
      }
    }
    if (begin_idx_cand == -1) continue;

    Vector *current_unit_indices = rsc->unit_indices;
    aom_vector_clear(current_unit_indices);
    bool has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
          old_unit->rest_unit_idx != last_idx &&
          !check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info_cand))
        continue;
      int index = old_unit->rest_unit_idx;
      aom_vector_push_back(current_unit_indices, &index);
    }

    // Generate new filter.
    RestorationUnitInfo rui_temp_cand;
    memset(&rui_temp_cand, 0, sizeof(rui_temp_cand));
    rui_temp_cand.restoration_type = RESTORE_SGRPROJ;
    rui_temp_cand.sgrproj_info = search_selfguided_restoration(
        rsc, NULL, bit_depth, procunit_width, procunit_height, tmpbuf,
        rsc->lpf_sf->enable_sgr_ep_pruning);

    aom_vector_clear(current_unit_indices);

    // Iterate once more for the no-merge cost
    double cost_nomerge_cand = cost_nomerge_base;
    has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      // last unit already in cost_nomerge
      if (old_unit->rest_unit_idx == last_idx) continue;
      if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
          !check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info_cand))
        continue;
      cost_nomerge_cand +=
          RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult, old_unit->current_bits >> 4,
                                         old_unit->current_sse, bit_depth);
    }

    // Iterate through vector to get sse and bits for each on the new filter.
    double cost_merge_cand = 0;
    has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
          old_unit->rest_unit_idx != last_idx &&
          !check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info_cand))
        continue;

      old_unit->merge_sse_cand = try_restoration_unit(
          rsc, &old_unit->limits, &rsc->tile_rect, &rui_temp_cand);

      // First unit in stack has larger unit_bits because the
      // merged coeffs are linked to it.
      if (old_unit->rest_unit_idx == begin_idx_cand) {
        const int new_bits = (int)count_sgrproj_bits_set(
            &x->mode_costs, &rui_temp_cand.sgrproj_info,
            &old_unit->ref_sgrproj_bank);
        old_unit->merge_bits_cand =
            x->mode_costs.sgrproj_restore_cost[1] + new_bits;
      } else {
        equal_ref = check_sgrproj_bank_eq(&old_unit->ref_sgrproj_bank,
                                          ref_sgrproj_info_cand);
        assert(equal_ref >= 0);  // Must exist in bank
        ref_sgrproj_info_tmp.bank_ref = equal_ref;
        const int merge_bits = (int)count_sgrproj_bits(
            &x->mode_costs, &ref_sgrproj_info_tmp, &old_unit->ref_sgrproj_bank);
        old_unit->merge_bits_cand =
            x->mode_costs.sgrproj_restore_cost[1] + merge_bits;
      }
      cost_merge_cand += RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, old_unit->merge_bits_cand >> 4, old_unit->merge_sse_cand,
          bit_depth);
    }
    if (cost_merge_cand - cost_nomerge_cand < cost_merge - cost_nomerge) {
      begin_idx = begin_idx_cand;
      bank_ref = bank_ref_cand;
      cost_merge = cost_merge_cand;
      cost_nomerge = cost_nomerge_cand;
      has_begun = false;
      VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
        RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
        RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
        if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
        if (!has_begun) continue;
        if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
            old_unit->rest_unit_idx != last_idx &&
            !check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info_cand))
          continue;
        old_unit->merge_sse = old_unit->merge_sse_cand;
        old_unit->merge_bits = old_unit->merge_bits_cand;
      }
      rui_temp = rui_temp_cand;
    }
  }
  // Trial end

  if (cost_merge < cost_nomerge) {
    const SgrprojInfo *ref_sgrproj_info =
        av1_constref_from_sgrproj_bank(&rsc->sgrproj_bank, bank_ref);
    // Update data within the stack.
    bool has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_SGRPROJ - 1] == RESTORE_SGRPROJ &&
          old_unit->rest_unit_idx != last_idx &&
          !check_sgrproj_eq(&old_rusi->sgrproj_info, ref_sgrproj_info))
        continue;

      if (old_unit->rest_unit_idx != begin_idx) {
        equal_ref = check_sgrproj_bank_eq(&old_unit->ref_sgrproj_bank,
                                          ref_sgrproj_info);
        assert(equal_ref >= 0);  // Must exist in bank
        av1_upd_to_sgrproj_bank(&old_unit->ref_sgrproj_bank, equal_ref,
                                &rui_temp.sgrproj_info);
      }
      old_rusi->best_rtype[RESTORE_SGRPROJ - 1] = RESTORE_SGRPROJ;
      old_rusi->sgrproj_info = rui_temp.sgrproj_info;
      old_rusi->sse[RESTORE_SGRPROJ] = old_unit->merge_sse;
      rsc->sse -= old_unit->current_sse;
      rsc->sse += old_unit->merge_sse;
      rsc->bits -= old_unit->current_bits;
      rsc->bits += old_unit->merge_bits;
      old_unit->current_sse = old_unit->merge_sse;
      old_unit->current_bits = old_unit->merge_bits;
    }
    RstUnitSnapshot *last_unit = aom_vector_back(current_unit_stack);
    equal_ref = check_sgrproj_bank_eq(&last_unit->ref_sgrproj_bank,
                                      &rui_temp.sgrproj_info);
    assert(equal_ref >= 0);  // Must exist in bank
    av1_upd_to_sgrproj_bank(&rsc->sgrproj_bank, equal_ref,
                            &rui_temp.sgrproj_info);
  } else {
    // Copy current unit from the top of the stack.
    // memset(&unit_snapshot, 0, sizeof(unit_snapshot));
    // unit_snapshot = *(RstUnitSnapshot *)aom_vector_back(current_unit_stack);
    // RESTORE_NONE units are discarded if they make the sse worse compared to
    // the no restore case, without consideration for bitrate.
    if (rtype == RESTORE_SGRPROJ) {
      av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
      // aom_vector_clear(current_unit_stack);
      // aom_vector_push_back(current_unit_stack, &unit_snapshot);
    } else /*if (rusi->sse[RESTORE_SGRPROJ] > rusi->sse[RESTORE_NONE])*/ {
      // Remove unit of RESTORE_NONE type only if its sse is worse (higher)
      // than no_restore ss.
      aom_vector_pop_back(current_unit_stack);
    }
  }
  /*
     intf("sgrproj(%d) [merge %f < nomerge %f] : %d, bank_size %d\n",
     rsc->plane, cost_merge, cost_nomerge, (cost_merge < cost_nomerge),
     rsc->sgrproj_bank.bank_size);
     */
}

static void initialize_rui_for_nonsep_search(const RestSearchCtxt *rsc,
                                             RestorationUnitInfo *rui) {
  memset(rui, 0, sizeof(*rui));
  rui->wiener_class_id_restrict = -1;
  rui->wiener_class_id = rsc->cm->mi_params.wiener_class_id[rsc->plane];
  rui->wiener_class_id_stride =
      rsc->cm->mi_params.wiener_class_id_stride[rsc->plane];
  rui->tskip = rsc->cm->mi_params.tx_skip[rsc->plane];
  rui->tskip_stride = rsc->cm->mi_params.tx_skip_stride[rsc->plane];
  rui->base_qindex = rsc->cm->quant_params.base_qindex;
  if (rsc->plane != AOM_PLANE_Y)
    rui->qindex_offset = rsc->plane == AOM_PLANE_U
                             ? rsc->cm->quant_params.u_ac_delta_q
                             : rsc->cm->quant_params.v_ac_delta_q;
  else
    rui->qindex_offset = 0;
  rui->luma = rsc->luma;
  rui->luma_stride = rsc->luma_stride;
  rui->plane = rsc->plane;
  rui->wienerns_info.num_classes = rsc->num_filter_classes;
  rui->tskip_zero_flag = rsc->tskip_zero_flag;
#if CONFIG_COMBINE_PC_NS_WIENER
  rui->skip_pcwiener_filtering = 0;
#endif  // CONFIG_COMBINE_PC_NS_WIENER
}

static int count_pc_wiener_bits() {
  // No side-information for now.
  return 0;
}

static AOM_INLINE void search_pc_wiener_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_seq, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const int bit_depth = rsc->cm->seq_params.bit_depth;
  const MACROBLOCK *const x = rsc->x;
  const int64_t bits_none = x->mode_costs.pc_wiener_restore_cost[0];

#if CONFIG_COMBINE_PC_NS_WIENER
  const int pcwiener_disabled =
      rsc->plane > 0 ||
      (rsc->cm->seq_params.lr_tools_disable_mask[AOM_PLANE_Y] &
       (1 << RESTORE_PC_WIENER));
#endif  // CONFIG_COMBINE_PC_NS_WIENER

#if CONFIG_COMBINE_PC_NS_WIENER_ADD
  // This routine is called (i) when the rtype = RESTORE_PC_WIENER or (ii) when
  // handling frame filters with rtype = RESTORE_WIENER_NONSEP.
  // (i) Run the full pc-wiener, i.e., don't skip, if pc-wiener is not disabled.
  // (Disabling can be accomplished by config or the plane being chroma.)
  // (ii) If disabled, skip only if frame-filters are off or the number of
  // filter classes for frame filters is one.
  bool skip_search =
      pcwiener_disabled &&
      (!is_frame_filters_enabled(rsc->plane) || rsc->num_filter_classes == 1);
#else
  bool skip_search = rsc->plane != AOM_PLANE_Y;
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
  if (skip_search) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_PC_WIENER - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_PC_WIENER] = INT64_MAX;
    return;
  }

  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
#if CONFIG_COMBINE_PC_NS_WIENER
  // Only need the classification if running for frame filters.
  rui.skip_pcwiener_filtering = pcwiener_disabled ? 1 : 0;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  rui.restoration_type = RESTORE_PC_WIENER;
  rusi->sse[RESTORE_PC_WIENER] =
      try_restoration_unit(rsc, limits, &rsc->tile_rect, &rui);

  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);

#if CONFIG_COMBINE_PC_NS_WIENER
  if (pcwiener_disabled) {
    rusi->best_rtype[RESTORE_PC_WIENER - 1] = RESTORE_NONE;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rsc->bits += bits_none;
    return;
  }
#endif

  const int64_t bits_pc_wiener =
      x->mode_costs.pc_wiener_restore_cost[1] +
      (count_pc_wiener_bits() << AV1_PROB_COST_SHIFT);
  double cost_pc_wiener = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_pc_wiener >> 4, rusi->sse[RESTORE_PC_WIENER], bit_depth);

  RestorationType rtype =
      (cost_pc_wiener < cost_none) ? RESTORE_PC_WIENER : RESTORE_NONE;
  rusi->best_rtype[RESTORE_PC_WIENER - 1] = rtype;

  rsc->sse += rusi->sse[rtype];
  rsc->bits += (cost_pc_wiener < cost_none) ? bits_pc_wiener : bits_none;
  // No side-information for now to copy to info.
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
  const int ref = wiener_info->bank_ref;
  const WienerInfo *ref_wiener_info = av1_constref_from_wiener_bank(bank, ref);
  const int equal_ref = check_wiener_eq(wiener_info, ref_wiener_info);
  for (int k = 0; k < AOMMAX(0, bank->bank_size - 1); ++k) {
    const int match = (k == ref);
    bits += (1 << AV1_PROB_COST_SHIFT);
    if (match) break;
  }
  bits += mode_costs->merged_param_cost[equal_ref];
  if (equal_ref) return bits;
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

static int64_t count_wiener_bits_set(int wiener_win,
                                     const ModeCosts *mode_costs,
                                     WienerInfo *info,
                                     const WienerInfoBank *bank) {
  int64_t best_bits = INT64_MAX;
  int best_ref = -1;
  for (int ref = 0; ref < AOMMAX(1, bank->bank_size); ++ref) {
    info->bank_ref = ref;
    const int64_t bits = count_wiener_bits(wiener_win, mode_costs, info, bank);
    if (bits < best_bits) {
      best_bits = bits;
      best_ref = ref;
    }
  }
  info->bank_ref = AOMMAX(0, best_ref);
  return best_bits;
}

// If limits != NULL, calculates error for current restoration unit.
// Otherwise, calculates error for all units in the stack using stored limits.
static int64_t calc_finer_tile_search_error(const RestSearchCtxt *rsc,
                                            const RestorationTileLimits *limits,
                                            const AV1PixelRect *tile,
                                            RestorationUnitInfo *rui) {
  int64_t err = 0;
  if (limits != NULL) {
    err = try_restoration_unit(rsc, limits, tile, rui);
  } else {
    Vector *current_unit_stack = rsc->unit_stack;
    Vector *current_unit_indices = rsc->unit_indices;
    int n = 0;
    int idx = *(int *)aom_vector_const_get(current_unit_indices, n);
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      if (old_unit->rest_unit_idx == idx) {
        err += try_restoration_unit(rsc, &old_unit->limits, tile, rui);
        n++;
        if (n >= (int)current_unit_indices->size) break;
        idx = *(int *)aom_vector_const_get(current_unit_indices, n);
      }
    }
  }
  return err;
}

// This function resets the dst buffers using the correct filters.
static int64_t reset_unit_stack_dst_buffers(const RestSearchCtxt *rsc,
                                            const RestorationTileLimits *limits,
                                            const AV1PixelRect *tile,
                                            RestorationUnitInfo *rui) {
  int64_t err = 0;
  if (limits != NULL) {
    err = try_restoration_unit(rsc, limits, tile, rui);
  } else {
    Vector *current_unit_stack = rsc->unit_stack;
    Vector *current_unit_indices = rsc->unit_indices;
    const int last_idx =
        ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;

    // Will update filters in rui as we go along. Buffer the rui filters here.
    WienerNonsepInfo last_unit_filters = rui->wienerns_info;
    int n = 0;
    int idx = *(int *)aom_vector_const_get(current_unit_indices, n);
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];

      if (old_unit->rest_unit_idx == idx) {
        if (idx == last_idx) {
          // Use the input filters on the last unit.
          copy_nsfilter_taps(&rui->wienerns_info, &last_unit_filters);
        } else {
          // Revert to old unit's filters.
          copy_nsfilter_taps(&rui->wienerns_info, &old_rusi->wienerns_info);
        }
        err += try_restoration_unit(rsc, &old_unit->limits, tile, rui);
        n++;
        if (n >= (int)current_unit_indices->size) break;
        idx = *(int *)aom_vector_const_get(current_unit_indices, n);
      }
    }
#ifndef NDEBUG
    {
      const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
          rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
      assert(check_wienerns_eq(&rui->wienerns_info, &last_unit_filters,
                               nsfilter_params->ncoeffs, ALL_WIENERNS_CLASSES));
    }
#endif  // NDEBUG
  }
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
  int64_t bits = count_wiener_bits_set(wiener_win, &x->mode_costs, plane_wiener,
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
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
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
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
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
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
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
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
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
  // Set bank_ref correctly
  (void)count_wiener_bits_set(wiener_win, &x->mode_costs, plane_wiener,
                              ref_wiener_bank);
  return err;
}

static AOM_INLINE void search_wiener_visitor(
    const RestorationTileLimits *limits, const AV1PixelRect *tile_rect,
    int rest_unit_idx, int rest_unit_idx_in_rutile, void *priv, int32_t *tmpbuf,
    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_in_rutile;
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

  Vector *current_unit_stack = rsc->unit_stack;
  int64_t bits_nomerge_base =
      x->mode_costs.wiener_restore_cost[1] +
      count_wiener_bits_set(wiener_win, &x->mode_costs, &rusi->wiener_info,
                            &rsc->wiener_bank);

  const int bank_ref_base = rusi->wiener_info.bank_ref;
  // Only test the reference in rusi->wiener_info.bank_ref, generated from
  // the count call above.

  double cost_nomerge_base = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_nomerge_base >> 4, rusi->sse[RESTORE_WIENER],
      rsc->cm->seq_params.bit_depth);
  const int bits_min = x->mode_costs.wiener_restore_cost[1] +
                       x->mode_costs.merged_param_cost[1] +
                       (1 << AV1_PROB_COST_SHIFT);
  const double cost_min = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_min >> 4, rusi->sse[RESTORE_WIENER],
      rsc->cm->seq_params.bit_depth);
  const double cost_nomerge_thr = (cost_nomerge_base + 3 * cost_min) / 4;
  RestorationType rtype =
      (cost_none <= cost_nomerge_thr) ? RESTORE_NONE : RESTORE_WIENER;
  if (cost_none <= cost_nomerge_thr) {
    bits_nomerge_base = bits_none;
    cost_nomerge_base = cost_none;
  }

  RstUnitSnapshot unit_snapshot;
  memset(&unit_snapshot, 0, sizeof(unit_snapshot));
  unit_snapshot.limits = *limits;
  unit_snapshot.rest_unit_idx = rest_unit_idx;
  memcpy(unit_snapshot.M, M, WIENER_WIN2 * sizeof(*M));
  memcpy(unit_snapshot.H, H, WIENER_WIN2 * WIENER_WIN2 * sizeof(*H));
  rusi->best_rtype[RESTORE_WIENER - 1] = rtype;
  rsc->sse += rusi->sse[rtype];
  rsc->bits += bits_nomerge_base;
  unit_snapshot.current_sse = rusi->sse[rtype];
  unit_snapshot.current_bits = bits_nomerge_base;
  // Only matters for first unit in stack.
  unit_snapshot.ref_wiener_bank = rsc->wiener_bank;
  // If current_unit_stack is empty, we can leave early.
  if (aom_vector_is_empty(current_unit_stack)) {
    if (rtype == RESTORE_WIENER)
      av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    return;
  }
  // Handles special case where no-merge filter is equal to merged
  // filter for the stack - we don't want to perform another merge and
  // get a less optimal filter, but we want to continue building the stack.
  int equal_ref;
  if (rtype == RESTORE_WIENER &&
      (equal_ref =
           check_wiener_bank_eq(&rsc->wiener_bank, &rusi->wiener_info)) >= 0) {
    rsc->bits -= bits_nomerge_base;
    rusi->wiener_info.bank_ref = equal_ref;
    unit_snapshot.current_bits =
        x->mode_costs.wiener_restore_cost[1] +
        count_wiener_bits_set(wiener_win, &x->mode_costs, &rusi->wiener_info,
                              &rsc->wiener_bank);
    rsc->bits += unit_snapshot.current_bits;
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    return;
  }

  // Push current unit onto stack.
  aom_vector_push_back(current_unit_stack, &unit_snapshot);
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;

  double cost_merge = DBL_MAX;
  double cost_nomerge = 0;
  int begin_idx = -1;
  int bank_ref = -1;
  RestorationUnitInfo rui_temp;

  // Trial start
  for (int bank_ref_cand = 0;
       bank_ref_cand < AOMMAX(1, rsc->wiener_bank.bank_size); bank_ref_cand++) {
#if MERGE_DRL_SEARCH_LEVEL == 1
    if (bank_ref_cand != 0 && bank_ref_cand != bank_ref_base) continue;
#elif MERGE_DRL_SEARCH_LEVEL == 2
    if (bank_ref_cand != bank_ref_base) continue;
#else
    (void)bank_ref_base;
#endif
    const WienerInfo *ref_wiener_info_cand =
        av1_constref_from_wiener_bank(&rsc->wiener_bank, bank_ref_cand);
    WienerInfo ref_wiener_info_tmp = *ref_wiener_info_cand;
    const WienerInfoBank *begin_wiener_bank = NULL;
    // Iterate once to get the begin unit of the run
    int begin_idx_cand = -1;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == last_idx) continue;
      if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_NONE ||
          (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
           check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info_cand))) {
        if (check_wiener_bank_eq(&old_unit->ref_wiener_bank,
                                 ref_wiener_info_cand) == -1) {
          begin_idx_cand = old_unit->rest_unit_idx;
          begin_wiener_bank = &old_unit->ref_wiener_bank;
        }
      }
    }
    if (begin_idx_cand == -1) continue;
    assert(begin_wiener_bank != NULL);
    begin_wiener_bank =
        begin_wiener_bank == NULL ? &rsc->wiener_bank : begin_wiener_bank;

    Vector *current_unit_indices = rsc->unit_indices;
    aom_vector_clear(current_unit_indices);
    bool has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
          old_unit->rest_unit_idx != last_idx &&
          !check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info_cand))
        continue;
      int index = old_unit->rest_unit_idx;
      aom_vector_push_back(current_unit_indices, &index);
    }

    int64_t M_AVG[WIENER_WIN2];
    int64_t H_AVG[WIENER_WIN2 * WIENER_WIN2];
    for (int index = 0; index < WIENER_WIN2; ++index) {
      M_AVG[index] = M[index] / current_unit_indices->size;
    }
    for (int index = 0; index < WIENER_WIN2 * WIENER_WIN2; ++index) {
      H_AVG[index] = H[index] / current_unit_indices->size;
    }
    // Iterate through vector to get current cost and the sum of M and H so far.
#ifndef NDEBUG
    int num_units = 0;
#endif  // NDEBUG
    has_begun = false;
    double cost_nomerge_cand = cost_nomerge_base;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      if (old_unit->rest_unit_idx == last_idx) continue;
      if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
          !check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info_cand))
        continue;

      cost_nomerge_cand += RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, old_unit->current_bits >> 4, old_unit->current_sse,
          rsc->cm->seq_params.bit_depth);
      for (int index = 0; index < WIENER_WIN2; ++index) {
        M_AVG[index] += old_unit->M[index] / current_unit_indices->size;
      }
      for (int index = 0; index < WIENER_WIN2 * WIENER_WIN2; ++index) {
        H_AVG[index] += old_unit->H[index] / current_unit_indices->size;
      }
#ifndef NDEBUG
      num_units++;
#endif  // NDEBUG
    }
    assert(num_units + 1 == (int)current_unit_indices->size);

    // Generate new filter.
    RestorationUnitInfo rui_temp_cand;
    memset(&rui_temp_cand, 0, sizeof(rui_temp_cand));
    rui_temp_cand.restoration_type = RESTORE_WIENER;
    int32_t vfilter_merge[WIENER_WIN], hfilter_merge[WIENER_WIN];
    wiener_decompose_sep_sym(reduced_wiener_win, M_AVG, H_AVG, vfilter_merge,
                             hfilter_merge);
    finalize_sym_filter(reduced_wiener_win, vfilter_merge,
                        rui_temp_cand.wiener_info.vfilter);
    finalize_sym_filter(reduced_wiener_win, hfilter_merge,
                        rui_temp_cand.wiener_info.hfilter);
    finer_tile_search_wiener(rsc, NULL, &rsc->tile_rect, &rui_temp_cand,
                             wiener_win, reduced_wiener_win, begin_wiener_bank);
    aom_vector_clear(current_unit_indices);
    if (compute_score(reduced_wiener_win, M_AVG, H_AVG,
                      rui_temp_cand.wiener_info.vfilter,
                      rui_temp_cand.wiener_info.hfilter) > 0) {
      continue;
    }

    // Iterate through vector to get sse and bits for each on the new filter.
    double cost_merge_cand = 0;
    has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
          old_unit->rest_unit_idx != last_idx &&
          !check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info_cand))
        continue;

      old_unit->merge_sse_cand = try_restoration_unit(
          rsc, &old_unit->limits, &rsc->tile_rect, &rui_temp_cand);
      // First unit in stack has larger unit_bits because the
      // merged coeffs are linked to it.
      if (old_unit->rest_unit_idx == begin_idx_cand) {
        const int new_bits = (int)count_wiener_bits_set(
            wiener_win, &x->mode_costs, &rui_temp_cand.wiener_info,
            &old_unit->ref_wiener_bank);
        old_unit->merge_bits_cand =
            x->mode_costs.wiener_restore_cost[1] + new_bits;
      } else {
        equal_ref = check_wiener_bank_eq(&old_unit->ref_wiener_bank,
                                         ref_wiener_info_cand);
        assert(equal_ref >= 0);  // Must exist in bank
        ref_wiener_info_tmp.bank_ref = equal_ref;
        const int merge_bits = (int)count_wiener_bits(
            wiener_win, &x->mode_costs, &ref_wiener_info_tmp,
            &old_unit->ref_wiener_bank);
        old_unit->merge_bits_cand =
            x->mode_costs.wiener_restore_cost[1] + merge_bits;
      }
      cost_merge_cand += RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, old_unit->merge_bits_cand >> 4, old_unit->merge_sse_cand,
          rsc->cm->seq_params.bit_depth);
    }
    if (cost_merge_cand - cost_nomerge_cand < cost_merge - cost_nomerge) {
      begin_idx = begin_idx_cand;
      bank_ref = bank_ref_cand;
      cost_merge = cost_merge_cand;
      cost_nomerge = cost_nomerge_cand;
      has_begun = false;
      VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
        RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
        RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
        if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
        if (!has_begun) continue;
        if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
            old_unit->rest_unit_idx != last_idx &&
            !check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info_cand))
          continue;
        old_unit->merge_sse = old_unit->merge_sse_cand;
        old_unit->merge_bits = old_unit->merge_bits_cand;
      }
      rui_temp = rui_temp_cand;
    }
  }
  // Trial end

  if (cost_merge < cost_nomerge) {
    const WienerInfo *ref_wiener_info =
        av1_constref_from_wiener_bank(&rsc->wiener_bank, bank_ref);
    // Update data within the stack.
    bool has_begun = false;
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
      if (old_unit->rest_unit_idx == begin_idx) has_begun = true;
      if (!has_begun) continue;
      if (old_rusi->best_rtype[RESTORE_WIENER - 1] == RESTORE_WIENER &&
          old_unit->rest_unit_idx != last_idx &&
          !check_wiener_eq(&old_rusi->wiener_info, ref_wiener_info))
        continue;

      if (old_unit->rest_unit_idx != begin_idx) {  // Not the first
        equal_ref =
            check_wiener_bank_eq(&old_unit->ref_wiener_bank, ref_wiener_info);
        assert(equal_ref >= 0);  // Must exist in bank
        av1_upd_to_wiener_bank(&old_unit->ref_wiener_bank, equal_ref,
                               &rui_temp.wiener_info);
      }
      old_rusi->best_rtype[RESTORE_WIENER - 1] = RESTORE_WIENER;
      old_rusi->wiener_info = rui_temp.wiener_info;
      old_rusi->sse[RESTORE_WIENER] = old_unit->merge_sse;
      rsc->sse -= old_unit->current_sse;
      rsc->sse += old_unit->merge_sse;
      rsc->bits -= old_unit->current_bits;
      rsc->bits += old_unit->merge_bits;
      old_unit->current_sse = old_unit->merge_sse;
      old_unit->current_bits = old_unit->merge_bits;
    }
    assert(has_begun);
    RstUnitSnapshot *last_unit = aom_vector_back(current_unit_stack);
    equal_ref = check_wiener_bank_eq(&last_unit->ref_wiener_bank,
                                     &rui_temp.wiener_info);
    assert(equal_ref >= 0);  // Must exist in bank
    av1_upd_to_wiener_bank(&rsc->wiener_bank, equal_ref, &rui_temp.wiener_info);
  } else {
    // Copy current unit from the top of the stack.
    // memset(&unit_snapshot, 0, sizeof(unit_snapshot));
    // unit_snapshot = *(RstUnitSnapshot *)aom_vector_back(current_unit_stack);
    // RESTORE_WIENER units become start of new stack, and
    // RESTORE_NONE units are discarded.
    if (rtype == RESTORE_WIENER) {
      av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
      // aom_vector_clear(current_unit_stack);
      // aom_vector_push_back(current_unit_stack, &unit_snapshot);
    } else /*if (rusi->sse[RESTORE_WIENER] > rusi->sse[RESTORE_NONE])*/ {
      // Remove unit of RESTORE_NONE type only if its sse is worse (higher)
      // than no_restore ss.
      aom_vector_pop_back(current_unit_stack);
    }
  }
  /*
     printf("wiener(%d) [merge %f < nomerge %f] : %d, bank_size %d\n",
     rsc->plane, cost_merge, cost_nomerge, (cost_merge < cost_nomerge),
     rsc->wiener_bank.bank_size);
     */
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

static int64_t count_wienerns_bits(
    int plane, const ModeCosts *mode_costs,
    const WienerNonsepInfo *wienerns_info, const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id) {
  (void)mode_costs;
  int is_uv = (plane != AOM_PLANE_Y);
  int64_t bits = 0;
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };

  int c_id_begin = 0;
  int c_id_end = wienerns_info->num_classes;
  if (wiener_class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = wiener_class_id;
    c_id_end = wiener_class_id + 1;
  }
  for (int c_id = c_id_begin; bank && c_id < c_id_end; ++c_id) {
    const int ref = wienerns_info->bank_ref_for_class[c_id];
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(bank, ref, c_id);
    const int equal_ref = check_wienerns_eq(wienerns_info, ref_wienerns_info,
                                            nsfilter_params->ncoeffs, c_id);
    for (int k = 0; k < bank->bank_size_for_class[c_id] - 1; ++k) {
      const int match = (k == ref);
      bits += (1 << AV1_PROB_COST_SHIFT);
      if (match) break;
    }
    bits += mode_costs->merged_param_cost[equal_ref];
    skip_filter_write_for_class[c_id] = equal_ref;
    ref_for_class[c_id] = ref;
  }
  const int(*length_cost)[2] = mode_costs->wienerns_length_cost;
  const int *uv_sym_cost = mode_costs->wienerns_uv_sym_cost;
  const int(*cost_4part)[4] = mode_costs->wienerns_4part_cost;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  assert(c_id_begin >= 0);
  assert(c_id_end <= WIENERNS_MAX_CLASSES);
  WienerNonsepInfo def_wienerns_info;
  if (!bank)
    set_default_wienerns_fromparams(&def_wienerns_info, c_id_end,
                                    nsfilter_params);
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    if (skip_filter_write_for_class[c_id]) continue;
    const WienerNonsepInfo *ref_wienerns_info;
    if (bank) {
      ref_wienerns_info =
          av1_constref_from_wienerns_bank(bank, ref_for_class[c_id], c_id);
    } else {
      ref_wienerns_info = &def_wienerns_info;
    }

    const int16_t *wienerns_info_nsfilter =
        const_nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s;
    for (s = 0; s < nsfilter_params->nsubsets; ++s) {
      int i;
      for (i = 0; i < end_feat; ++i) {
        if (nsfilter_params->subset_config[s][i] == 0 &&
            wienerns_info_nsfilter[i] != 0)
          break;
      }
      if (i == end_feat) break;
    }
    assert(s < nsfilter_params->nsubsets);
    int s_ = s;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit = (s_ > 0);
      bits += length_cost[is_uv][filter_length_bit];
      if (!filter_length_bit) break;
      s_--;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      for (int i = beg_feat; i < end_feat; i++) {
        if (!nsfilter_params->subset_config[s][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (!is_asym_coeff) continue;
        if (wienerns_info_nsfilter[i + 1] != wienerns_info_nsfilter[i]) {
          sym = 0;
          break;
        }
        i++;
      }
      assert(is_uv);
      bits += uv_sym_cost[sym];
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      bits += aom_count_4part_wref(
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          cost_4part[wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID]],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID], AV1_PROB_COST_SHIFT);
      if (sym && is_asym_coeff) {
        // Don't code symmetrical taps
        assert(wienerns_info_nsfilter[i + 1] == wienerns_info_nsfilter[i]);
        i += 1;
      }
    }
  }
  return bits;
}

static int64_t count_wienerns_bits_set(
    int plane, const ModeCosts *mode_costs, WienerNonsepInfo *info,
    const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id) {
  // TODO: Add match_indices bits when needed.
  int64_t total_bits = 0;
  int c_id_begin = 0;
  int c_id_end = info->num_classes;
  if (wiener_class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = wiener_class_id;
    c_id_end = wiener_class_id + 1;
  }
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int64_t best_bits = INT64_MAX;
    int best_ref = -1;
    for (int ref = 0;
         ref < AOMMAX(1, bank ? bank->bank_size_for_class[c_id] : 0); ++ref) {
      info->bank_ref_for_class[c_id] = ref;
      const int64_t bits = count_wienerns_bits(plane, mode_costs, info, bank,
                                               nsfilter_params, c_id);
      if (bits < best_bits) {
        best_bits = bits;
        best_ref = ref;
      }
    }
    total_bits += best_bits;
    info->bank_ref_for_class[c_id] = AOMMAX(0, best_ref);
  }
  return total_bits;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#if CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
#define FINER_UPDATE_BANK 1
#define FINER_REPLACE 1
#else
#define FINER_UPDATE_BANK 0
#define FINER_REPLACE 0
#endif
static void initialize_bank_with_best_frame_filter_match(
    const RestSearchCtxt *rsc, WienerNonsepInfo *filter,
    WienerNonsepInfoBank *bank, int reset_dict);
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD

static int get_subset_from_nsfilter(
    const WienernsFilterParameters *nsfilter_params, int16_t *nsfilter) {
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  for (int s = 0; s < nsfilter_params->nsubsets; ++s) {
    int i;
    for (i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i] && nsfilter[i]) break;
    }
    if (i == end_feat) return s;
  }
  return -1;
}

static int64_t finer_tile_search_wienerns(
    const RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    const AV1PixelRect *tile_rect, RestorationUnitInfo *rui,
    const WienernsFilterParameters *nsfilter_params, int ext_search,
    const WienerNonsepInfoBank *reference_wienerns_bank, int wiener_class_id) {
  assert(rsc->plane == rui->plane);
  const MACROBLOCK *const x = rsc->x;
  // WienerNonsepInfo curr = rui->wienerns_info;
  WienerNonsepInfo best = rui->wienerns_info;
  WienerNonsepInfoBank tmp_bank = *reference_wienerns_bank;
  WienerNonsepInfoBank *ref_bank_ptr = &tmp_bank;
  WienerNonsepInfoBank *cur_bank_ptr = ref_bank_ptr;
#if CONFIG_COMBINE_PC_NS_WIENER
  // If frame filters are on ref_bank_ptr points to the bank matched to the
  // best filters. work_bank is used in trials and gets swapped with
  // ref_bank_ptr as needed.
  WienerNonsepInfoBank work_bank = *reference_wienerns_bank;
#if FINER_UPDATE_BANK || FINER_REPLACE
  if (rsc->frame_filters_on) {
    cur_bank_ptr = &work_bank;
    *cur_bank_ptr = *ref_bank_ptr;
  }
#endif
#endif
  const int num_feat = nsfilter_params->ncoeffs;
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  int c_id_begin = wiener_class_id;
  int c_id_end = wiener_class_id + 1;
  rui->wiener_class_id_restrict = wiener_class_id;
  if (wiener_class_id == ALL_WIENERNS_CLASSES) {
    c_id_begin = 0;
    c_id_end = rui->wienerns_info.num_classes;
    rui->wiener_class_id_restrict = -1;
  }
  int64_t best_err = calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
  // When wiener_class_id != ALL_WIENERNS_CLASSES we are calculating bits for
  // wiener_class_id only since that is the filter we are changing. Should be OK
  // since bits for classes outside wiener_class_id are not needed for decisions
  // in this fn.
  int64_t best_bits =
      count_wienerns_bits_set(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                              cur_bank_ptr, nsfilter_params, wiener_class_id);
  double best_cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, best_bits >> 4, best_err, rsc->cm->seq_params.bit_depth);

  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  int ncoeffs1, ncoeffs2;
  int ncoeffs =
      config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
  assert(nsfilter_params->ncoeffs == ncoeffs);
  (void)ncoeffs;

#if CONFIG_COMBINE_PC_NS_WIENER
  int reset_dict = 1;
#endif

#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_REPLACE
  if (rsc->frame_filters_on) {
    assert(wiener_class_id == ALL_WIENERNS_CLASSES);
    copy_nsfilter_taps(&rui->wienerns_info, &best);
    cur_bank_ptr = &work_bank;
    for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
      const int bank_ref = 0;
      copy_nsfilter_taps_for_class(
          &rui->wienerns_info,
          av1_constref_from_wienerns_bank(ref_bank_ptr, bank_ref, c_id), c_id);
      rui->wiener_class_id_restrict = c_id;

      const int64_t err =
          calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
      initialize_bank_with_best_frame_filter_match(rsc, &rui->wienerns_info,
                                                   cur_bank_ptr, reset_dict);
      reset_dict = 0;
      const int64_t bits = count_wienerns_bits_set(
          rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
          nsfilter_params, ALL_WIENERNS_CLASSES);
      const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
      if (cost < best_cost) {
        best_err = err;
        best_cost = cost;
        copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
        ref_bank_ptr = cur_bank_ptr;
        cur_bank_ptr = tmp_ptr;
      } else {
        copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        // Re-establish dst.
        if (c_id_end - c_id_begin > 1 && rui->wiener_class_id_restrict != -1) {
          calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
        }
      }
    }
    copy_nsfilter_taps(&rui->wienerns_info, &best);
    *cur_bank_ptr = *ref_bank_ptr;
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_REPLACE

  const int refine_iters = rsc->lpf_sf->wienerns_refine_iters;
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *best_nsfilter = nsfilter_taps(&best, c_id);
    // int16_t *curr_nsfilter = nsfilter_taps(&curr, c_id);
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);

    const int subset =
        get_subset_from_nsfilter(nsfilter_params, rui_wienerns_info_nsfilter);
    assert(subset != -1);

    // calc_finer_tile_search_error() above sets dst. Update only parts of dst
    // relevant to c_id.
    rui->wiener_class_id_restrict = c_id;
    int src_range = 2;

    int sym = 1;
    int skip_sym = skip_sym_bit(nsfilter_params, subset);
    if (!skip_sym) {
      for (int i = beg_feat; i < end_feat; i++) {
        if (!nsfilter_params->subset_config[subset][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (!is_asym_coeff) continue;
        if (is_asym_coeff && rui_wienerns_info_nsfilter[i + 1] !=
                                 rui_wienerns_info_nsfilter[i]) {
          sym = 0;
          break;
        }
        i++;
      }
    }
    if (skip_sym != 1 && sym) {  // Forced symmetry taps search
      int no_improv = 1;
      for (int s = 0; s < refine_iters; ++s) {
        for (int i = beg_feat; i < end_feat; i++) {
          if (!nsfilter_params->subset_config[subset][i]) continue;
          const int is_asym_coeff =
              (i < nsfilter_params->nsfilter_config.asymmetric ||
               (i >= ncoeffs1 &&
                i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
          if (!is_asym_coeff) continue;
          int cval = (rui_wienerns_info_nsfilter[i] +
                      rui_wienerns_info_nsfilter[i + 1]) /
                     2;
          int cmin = MAX(cval - src_range,
                         wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]);
          int cmax =
              MIN(cval + src_range,
                  wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID] +
                      (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]));
          for (int ci = cmin; ci < cmax; ++ci) {
            rui_wienerns_info_nsfilter[i] = ci;
            rui_wienerns_info_nsfilter[i + 1] = ci;
            const int64_t err =
                calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
            if (rsc->frame_filters_on) {
              initialize_bank_with_best_frame_filter_match(
                  rsc, &rui->wienerns_info, cur_bank_ptr, reset_dict);
              reset_dict = 0;
            }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
            const int c_id_for_bits = wiener_class_id == ALL_WIENERNS_CLASSES
                                          ? ALL_WIENERNS_CLASSES
                                          : c_id;
            const int64_t bits = count_wienerns_bits_set(
                rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
                nsfilter_params, c_id_for_bits);
            const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
                x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
            if (cost < best_cost) {
              no_improv = 0;
              best_err = err;
              best_cost = cost;
              copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
              if (rsc->frame_filters_on) {
                WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
                ref_bank_ptr = cur_bank_ptr;
                cur_bank_ptr = tmp_ptr;
              }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
            }
          }
          // copy_nsfilter_taps_for_class(&curr, &best, c_id);
          rui_wienerns_info_nsfilter[i] = best_nsfilter[i];
          rui_wienerns_info_nsfilter[i + 1] = best_nsfilter[i + 1];
          i++;
        }
        if (no_improv) {
          break;
        }
        copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      }
    }

    for (int s = 0; s < refine_iters; ++s) {
      int no_improv = 1;
      for (int i = beg_feat; i < end_feat; ++i) {
        if (!nsfilter_params->subset_config[subset][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (is_asym_coeff && skip_sym == 2) continue;
        int cval = rui_wienerns_info_nsfilter[i];
        int cmin = MAX(rui_wienerns_info_nsfilter[i] - src_range,
                       wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]);
        int cmax =
            MIN(rui_wienerns_info_nsfilter[i] + src_range,
                wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID] +
                    (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]));

        for (int ci = cmin; ci < cmax; ++ci) {
          if (ci == cval) {
            continue;
          }
          rui_wienerns_info_nsfilter[i] = ci;
          const int64_t err =
              calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          if (rsc->frame_filters_on) {
            initialize_bank_with_best_frame_filter_match(
                rsc, &rui->wienerns_info, cur_bank_ptr, reset_dict);
            reset_dict = 0;
          }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          const int c_id_for_bits = wiener_class_id == ALL_WIENERNS_CLASSES
                                        ? ALL_WIENERNS_CLASSES
                                        : c_id;
          const int64_t bits = count_wienerns_bits_set(
              rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
              nsfilter_params, c_id_for_bits);
          const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
          if (cost < best_cost) {
            no_improv = 0;
            best_err = err;
            best_cost = cost;
            copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
            if (rsc->frame_filters_on) {
              WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
              ref_bank_ptr = cur_bank_ptr;
              cur_bank_ptr = tmp_ptr;
            }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          }
        }
        // copy_nsfilter_taps_for_class(&curr, &best, c_id);
        rui_wienerns_info_nsfilter[i] = best_nsfilter[i];
      }
      if (no_improv) {
        break;
      }
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      // copy_nsfilter_taps_for_class(&curr, &rui->wienerns_info, c_id);
    }
    // Re-establish dst.
    if (refine_iters && c_id_end - c_id_begin > 1 &&
        rui->wiener_class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }
  copy_nsfilter_taps(&rui->wienerns_info, &best);
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsc->frame_filters_on) {
#if FINER_UPDATE_BANK
    *cur_bank_ptr = *ref_bank_ptr;
#endif
  }
#endif

  if (!ext_search) return best_err;

  // Try reducing filter complexity by forcing asymmetrical parts
  // to be symmetrical
  if (nsfilter_params->nsfilter_config.asymmetric +
          nsfilter_params->nsfilter_config.asymmetric2 >
      0) {
    for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
      int16_t *rui_wienerns_info_nsfilter =
          nsfilter_taps(&rui->wienerns_info, c_id);
      const int subset =
          get_subset_from_nsfilter(nsfilter_params, rui_wienerns_info_nsfilter);
      assert(subset != -1);
      rui->wiener_class_id_restrict = c_id;
      for (int i = beg_feat; i < end_feat; i++) {
        if (!nsfilter_params->subset_config[subset][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (!is_asym_coeff) continue;
        int avg = ROUND_POWER_OF_TWO(
            rui_wienerns_info_nsfilter[i] + rui_wienerns_info_nsfilter[i + 1],
            1);
        rui_wienerns_info_nsfilter[i] = avg;
        rui_wienerns_info_nsfilter[i + 1] = avg;
        i++;
      }
      const int64_t err =
          calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      if (rsc->frame_filters_on) {
        initialize_bank_with_best_frame_filter_match(rsc, &rui->wienerns_info,
                                                     cur_bank_ptr, reset_dict);
        reset_dict = 0;
      }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      const int c_id_for_bits =
          wiener_class_id == ALL_WIENERNS_CLASSES ? ALL_WIENERNS_CLASSES : c_id;
      const int64_t bits = count_wienerns_bits_set(
          rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
          nsfilter_params, c_id_for_bits);
      const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
      if (cost < best_cost) {
        best_err = err;
        best_cost = cost;
        copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
        if (rsc->frame_filters_on) {
          WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
          ref_bank_ptr = cur_bank_ptr;
          cur_bank_ptr = tmp_ptr;
        }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      } else {
        copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      }
      // Re-establish dst.
      if (c_id_end - c_id_begin > 1 && rui->wiener_class_id_restrict != -1) {
        copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
      }
    }
    copy_nsfilter_taps(&rui->wienerns_info, &best);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
    if (rsc->frame_filters_on) {
#if FINER_UPDATE_BANK
      *cur_bank_ptr = *ref_bank_ptr;
#endif  // FINER_UPDATE_BANK
    }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
  }

  // Try reduced filters by forcing trailing coeffs to 0
  assert((end_feat & 1) == 0);
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);
    const int subset =
        get_subset_from_nsfilter(nsfilter_params, rui_wienerns_info_nsfilter);
    assert(subset != -1);
    rui->wiener_class_id_restrict = c_id;
    const int c_id_for_bits =
        wiener_class_id == ALL_WIENERNS_CLASSES ? ALL_WIENERNS_CLASSES : c_id;

    for (int s = 0; s < subset; ++s) {
      for (int i = beg_feat; i < end_feat; ++i) {
        if (!nsfilter_params->subset_config[s][i])
          rui_wienerns_info_nsfilter[i] = 0;
      }
      const int64_t err =
          calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      if (rsc->frame_filters_on) {
        initialize_bank_with_best_frame_filter_match(rsc, &rui->wienerns_info,
                                                     cur_bank_ptr, reset_dict);
        reset_dict = 0;
      }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      const int64_t bits = count_wienerns_bits_set(
          rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
          nsfilter_params, c_id_for_bits);
      const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
          x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
      if (cost < best_cost) {
        best_err = err;
        best_cost = cost;
        copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
        if (rsc->frame_filters_on) {
          WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
          ref_bank_ptr = cur_bank_ptr;
          cur_bank_ptr = tmp_ptr;
        }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
      } else {
        copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      }
    }
    // Re-establish dst.
    if (c_id_end - c_id_begin > 1 && rui->wiener_class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }
  copy_nsfilter_taps(&rui->wienerns_info, &best);
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsc->frame_filters_on) {
#if FINER_UPDATE_BANK
    *cur_bank_ptr = *ref_bank_ptr;
#endif
  }
#endif

  if (ext_search == 1) return best_err;

  const int src_steps[][2] = {
    { 1, -1 }, { -1, 1 }, { 1, 1 },  { -1, -1 }, { 2, 1 },   { 1, 2 },
    { -2, 1 }, { 1, -2 }, { 2, -1 }, { -1, 2 },  { -2, -1 }, { -1, -2 },
  };
  const int nsrc_steps = sizeof(src_steps) / (2 * sizeof(src_steps[0][0]));
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);
    // int16_t *curr_nsfilter = nsfilter_taps(&curr, c_id);
    int16_t *best_nsfilter = nsfilter_taps(&best, c_id);
    rui->wiener_class_id_restrict = c_id;
    for (int s = 0; s < refine_iters; ++s) {
      int no_improv = 1;
      for (int i = beg_feat + (num_feat & 1); i < end_feat; i += 2) {
        int cmin[2] = { wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
                        wienerns_coeffs[i + 1 - beg_feat][WIENERNS_MIN_ID] };
        int cmax[2] = {
          wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID] +
              (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]),
          wienerns_coeffs[i + 1 - beg_feat][WIENERNS_MIN_ID] +
              (1 << wienerns_coeffs[i + 1 - beg_feat][WIENERNS_BIT_ID])
        };

        for (int ci = 0; ci < nsrc_steps; ++ci) {
          rui_wienerns_info_nsfilter[i] = best_nsfilter[i] + src_steps[ci][0];
          rui_wienerns_info_nsfilter[i + 1] =
              best_nsfilter[i + 1] + src_steps[ci][1];
          if (rui_wienerns_info_nsfilter[i] < cmin[0] ||
              rui_wienerns_info_nsfilter[i] >= cmax[0] ||
              rui_wienerns_info_nsfilter[i + 1] < cmin[1] ||
              rui_wienerns_info_nsfilter[i + 1] >= cmax[1]) {
            copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
            continue;
          }
          const int64_t err =
              calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          if (rsc->frame_filters_on) {
            initialize_bank_with_best_frame_filter_match(
                rsc, &rui->wienerns_info, cur_bank_ptr, reset_dict);
            reset_dict = 0;
          }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          const int c_id_for_bits = wiener_class_id == ALL_WIENERNS_CLASSES
                                        ? ALL_WIENERNS_CLASSES
                                        : c_id;
          const int64_t bits = count_wienerns_bits_set(
              rsc->plane, &x->mode_costs, &rui->wienerns_info, cur_bank_ptr,
              nsfilter_params, c_id_for_bits);
          const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
          if (cost < best_cost) {
            no_improv = 0;
            best_err = err;
            best_cost = cost;
            copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
            if (rsc->frame_filters_on) {
              WienerNonsepInfoBank *tmp_ptr = ref_bank_ptr;
              ref_bank_ptr = cur_bank_ptr;
              cur_bank_ptr = tmp_ptr;
            }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD && FINER_UPDATE_BANK
          }
        }
        // copy_nsfilter_taps_for_class(&curr, &best, c_id);
        rui_wienerns_info_nsfilter[i] = best_nsfilter[i];
        rui_wienerns_info_nsfilter[i + 1] = best_nsfilter[i + 1];
      }
      if (no_improv) {
        break;
      }
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      // copy_nsfilter_taps_for_class(&curr, &rui->wienerns_info, c_id);
    }
    // Re-establish dst.
    if (c_id_end - c_id_begin > 1 && rui->wiener_class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }

  copy_nsfilter_taps(&rui->wienerns_info, &best);
  (void)count_wienerns_bits_set(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_bank_ptr, nsfilter_params, wiener_class_id);
  return best_err;
}

#if CONFIG_COMBINE_PC_NS_WIENER
typedef struct BestMatchResults {
  int use_one_match_index;      // Index of the best matching dictionary-filter.
  int64_t use_one_taps_score;   // debug: Bits to encode the filter with match.
  int64_t all_zero_taps_score;  // debug: Bits to encode with all zeros.
} BestMatchResults;

// Returns the index of the dictionary-filter in frame_filter_bank that
// best matches WienerNonsepInfo *filter. This is useful when encoding filter
// where the match index is first encoded and then the filter is encoded
// conditioned on the relevant dictionary-filter.
static BestMatchResults find_best_match_for_class(
    const RestSearchCtxt *rsc, WienerNonsepInfo *filter, int class_id,
    const int16_t *frame_filter_dictionary, int dict_stride,
    const WienernsFilterParameters *nsfilter_params,
    WienerNonsepInfoBank *bank) {
  BestMatchResults best_match_results = { 0 };
  const int bank_ref = 0;
  assert(bank->bank_size_for_class[class_id] == 0);
  WienerNonsepInfo tmp_filter;
  tmp_filter.num_classes = filter->num_classes;

  int64_t best_scr = INT_MAX;
  int64_t all_zeros_score = 0;  // debug
  int best_filter_index = ILLEGAL_MATCH;
  const int zero_filter_index = 0;
  const int nopcw =
      disable_pcwiener_filters_in_framefilters(&rsc->cm->seq_params);
  const int max_filter_index = num_dictionary_slots(filter->num_classes, nopcw);
  for (int filter_index = 0; filter_index < max_filter_index; ++filter_index) {
    if (!is_match_allowed(filter_index, class_id, filter->num_classes))
      continue;
    filter->match_indices[class_id] = filter_index;
    fill_filter_with_match(&tmp_filter, frame_filter_dictionary, dict_stride,
                           filter->match_indices, nsfilter_params, class_id,
                           nopcw);
    av1_upd_to_wienerns_bank(bank, bank_ref, &tmp_filter, class_id);
    bank->bank_size_for_class[class_id] = 1;
    const int64_t score =
        count_wienerns_bits_set(rsc->plane, &rsc->x->mode_costs, filter, bank,
                                nsfilter_params, class_id);
    if (score < best_scr) {
      best_scr = score;
      best_filter_index = filter_index;
    }
    if (filter_index == zero_filter_index) all_zeros_score = score;
  }
  assert(best_filter_index != ILLEGAL_MATCH);
  best_match_results.use_one_match_index = best_filter_index;
  best_match_results.use_one_taps_score = best_scr;
  best_match_results.all_zero_taps_score = all_zeros_score;
  return best_match_results;
}

// Filter will be encoded differentially wrto a matching filter. Finds the
// matching-filter that minimizes the side-information bits for filter. Fills
// filter->match_indices with the found match.
static void find_best_match_for_filter(const RestSearchCtxt *rsc,
                                       WienerNonsepInfo *filter,
                                       int base_qindex,
                                       int16_t *frame_filter_dictionary,
                                       int dict_stride) {
  is_frame_filters_enabled(rsc->plane);
  const int is_uv = rsc->plane > 0;
  const int nopcw =
      disable_pcwiener_filters_in_framefilters(&rsc->cm->seq_params);
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);
  WienerNonsepInfoBank tmp_bank;
  av1_reset_wienerns_bank(&tmp_bank, base_qindex, filter->num_classes, 0);
  BestMatchResults best_match_results[WIENERNS_MAX_CLASSES];
  for (int c_id = 0; c_id < filter->num_classes; ++c_id) {
    best_match_results[c_id] =
        find_best_match_for_class(rsc, filter, c_id, frame_filter_dictionary,
                                  dict_stride, nsfilter_params, &tmp_bank);
    add_filter_to_dictionary(filter, c_id, nsfilter_params,
                             frame_filter_dictionary, dict_stride, nopcw);
  }

  int use_one_match_indices[WIENERNS_MAX_CLASSES] = { 0 };
  for (int c_id = 0; c_id < filter->num_classes; ++c_id) {
    use_one_match_indices[c_id] = best_match_results[c_id].use_one_match_index;
    assert(best_match_results[c_id].use_one_match_index ==
           get_first_match_index(use_one_match_indices[c_id],
                                 filter->num_classes, nopcw));
  }
  int *best_match_indices = use_one_match_indices;

  WienerNonsepInfo tmp_filter = *filter;
  for (int c_id = 0; c_id < filter->num_classes; ++c_id) {
    filter->match_indices[c_id] = best_match_indices[c_id];
    // Debug.
    fill_filter_with_match(&tmp_filter, frame_filter_dictionary, dict_stride,
                           filter->match_indices, nsfilter_params, c_id, nopcw);
    av1_upd_to_wienerns_bank(&tmp_bank, /*bank_ref=*/0, &tmp_filter, c_id);
    tmp_bank.bank_size_for_class[c_id] = 1;
    const int64_t taps_score =
        count_wienerns_bits_set(rsc->plane, &rsc->x->mode_costs, filter,
                                &tmp_bank, nsfilter_params, c_id);
    (void)taps_score;
    assert(taps_score == best_match_results[c_id].use_one_taps_score);
  }
}

static void initialize_bank_with_best_frame_filter_match(
    const RestSearchCtxt *rsc, WienerNonsepInfo *filter,
    WienerNonsepInfoBank *bank, int reset_dict) {
  const int nopcw =
      disable_pcwiener_filters_in_framefilters(&rsc->cm->seq_params);
  const int base_qindex = rsc->cm->quant_params.base_qindex;
  int dict_stride = rsc->cm->frame_filter_dictionary_stride;
  assert(rsc->cm->frame_filter_dictionary != NULL);
  assert(rsc->cm->translated_pcwiener_filters != NULL);
  assert(rsc->cm->translation_done);
  int16_t *frame_filter_dictionary = rsc->cm->frame_filter_dictionary;
  if (reset_dict) {
    *rsc->cm->num_ref_filters =
        set_frame_filter_dictionary(rsc->plane, rsc->cm, filter->num_classes,
                                    frame_filter_dictionary, dict_stride);
  }
  filter->num_ref_filters = *rsc->cm->num_ref_filters;
  find_best_match_for_filter(rsc, filter, base_qindex, frame_filter_dictionary,
                             dict_stride);
  av1_reset_wienerns_bank(bank, base_qindex, filter->num_classes,
                          rsc->plane != AOM_PLANE_Y);
  fill_first_slot_of_bank_with_filter_match(
      rsc->plane, bank, filter, filter->match_indices, base_qindex,
      ALL_WIENERNS_CLASSES, frame_filter_dictionary, dict_stride, nopcw);
}
#endif  // CONFIG_COMBINE_PC_NS_WIENER

static int compute_wienerns_filter_select_sym(
    int n, const int *select, const double *A, int stride, const double *b,
    double *tmpbuf, int16_t *nsfilter,
    const WienernsFilterParameters *nsfilter_params) {
  int ncoeffs1, ncoeffs2;
  int ncoeffs =
      config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
  (void)ncoeffs;
  if (nsfilter_params->nsfilter_config.asymmetric +
          nsfilter_params->nsfilter_config.asymmetric2 ==
      0)
    return 0;
  double sym_A[WIENERNS_A_SIZE];
  double sym_b[WIENERNS_b_SIZE];
  int stridep = n;
  int ip = 0;
  for (int i = 0; i < n; ++i) {
    if (!select[i]) continue;
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1) continue;
    const int is_merge_i = (is_asym_coeff_i && (i & 1) == 0);
    for (int j = i, jp = ip; j < n; ++j) {
      if (!select[j]) continue;
      const int is_asym_coeff_j =
          (j < nsfilter_params->nsfilter_config.asymmetric ||
           (j >= ncoeffs1 &&
            j - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      if (is_asym_coeff_j && (j & 1) == 1) continue;
      const int is_merge_j = is_asym_coeff_j && (j & 1) == 0;
      if (is_merge_i && is_merge_j)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[i * stride + j + 1] +
                                   A[(i + 1) * stride + j] +
                                   A[(i + 1) * stride + j + 1];
      else if (is_merge_i)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[(i + 1) * stride + j];
      else if (is_merge_j)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[i * stride + j + 1];
      else
        sym_A[ip * stridep + jp] = A[i * stride + j];
      if (ip != jp) sym_A[jp * stridep + ip] = sym_A[ip * stridep + jp];
      jp++;
    }
    if (is_merge_i)
      sym_b[ip] = b[i] + b[i + 1];
    else
      sym_b[ip] = b[i];
    ip++;
  }
  int np = ip;
  assert(np > 0);
  assert(stridep > 0);

  // Use temporary storage to avoid modifying A and b
  double *R = tmpbuf;
  double *tmp = tmpbuf + WIENERNS_R_SIZE;

  // Set up quantization data
  double prec[WIENERNS_TAPS_MAX] = { 0 };
  int32_t min[WIENERNS_TAPS_MAX] = { 0 };
  int32_t max[WIENERNS_TAPS_MAX] = { 0 };
  int32_t scale[WIENERNS_TAPS_MAX] = { 0 };

  assert(np <= nsfilter_params->ncoeffs);

  ip = 0;
  for (int i = 0; i < n; i++) {
    if (!select[i]) continue;
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1) continue;
    int min_val = nsfilter_params->coeffs[i][WIENERNS_MIN_ID];
    int range = 1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID];

    prec[ip] = (double)(1 << nsfilter_params->nsfilter_config.prec_bits);
    min[ip] = min_val;
    max[ip] = min_val + range - 1;
    scale[ip] = 1;
    ip++;
  }
  // Solve problem
  int32_t sym_x[WIENERNS_TAPS_MAX];
  int ret = linsolve_spd_quantize(np, sym_A, R, stridep, sym_b, tmp, sym_x,
                                  prec, min, max, scale);
  if (!ret) goto finished;

  // Convert results from 32-bit to 16-bit storage
  ip = 0;
  for (int i = 0; i < n; i++) {
    if (!select[i]) {
      nsfilter[i] = 0;
      continue;
    }
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1)
      nsfilter[i] = nsfilter[i - 1];
    else
      nsfilter[i] = sym_x[ip++];
  }

finished:
  return ret;
}

static int compute_wienerns_filter_select(
    int n, const int *select, const double *A, int stride, const double *b,
    double *tmpbuf, int16_t *nsfilter,
    const WienernsFilterParameters *nsfilter_params) {
  double sel_A[WIENERNS_A_SIZE];
  double sel_b[WIENERNS_b_SIZE];
  int stridep = n;
  int ip = 0;
  for (int i = 0; i < n; ++i) {
    if (!select[i]) continue;
    int jp = 0;
    for (int j = 0; j < n; ++j) {
      if (!select[j]) continue;
      sel_A[ip * stridep + jp] = A[i * stride + j];
      jp++;
    }
    sel_b[ip] = b[i];
    ip++;
  }
  int np = ip;

  // Use temporary storage to avoid modifying A and b
  double *R = tmpbuf;
  double *tmp = tmpbuf + WIENERNS_R_SIZE;

  // Set up quantization data
  double prec[WIENERNS_TAPS_MAX] = { 0 };
  int32_t min[WIENERNS_TAPS_MAX] = { 0 };
  int32_t max[WIENERNS_TAPS_MAX] = { 0 };
  int32_t scale[WIENERNS_TAPS_MAX] = { 0 };

  assert(np <= nsfilter_params->ncoeffs);

  ip = 0;
  for (int i = 0; i < n; ++i) {
    if (!select[i]) continue;
    int min_val = nsfilter_params->coeffs[i][WIENERNS_MIN_ID];
    int range = 1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID];
    prec[ip] = (double)(1 << nsfilter_params->nsfilter_config.prec_bits);
    min[ip] = min_val;
    max[ip] = min_val + range - 1;
    scale[ip] = 1;
    ip++;
  }
  // Solve problem
  int32_t sel_x[WIENERNS_TAPS_MAX];
  int ret = linsolve_spd_quantize(np, sel_A, R, stridep, sel_b, tmp, sel_x,
                                  prec, min, max, scale);
  if (!ret) return ret;
  ip = 0;
  for (int i = 0; i < n; i++) {
    nsfilter[i] = select[i] ? sel_x[ip++] : 0;
  }
  return ret;
}

static int compute_wienerns_filter_sym(
    int n, const double *A, int stride, const double *b, double *tmpbuf,
    int16_t *nsfilter, const WienernsFilterParameters *nsfilter_params) {
  int ncoeffs1, ncoeffs2;
  int ncoeffs =
      config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
  (void)ncoeffs;
  if (nsfilter_params->nsfilter_config.asymmetric +
          nsfilter_params->nsfilter_config.asymmetric2 ==
      0)
    return 0;
  double sym_A[WIENERNS_A_SIZE];
  double sym_b[WIENERNS_b_SIZE];
  int stridep = n;
  int ip = 0;
  for (int i = 0; i < n; ++i) {
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1) continue;
    const int is_merge_i = (is_asym_coeff_i && (i & 1) == 0);
    for (int j = i, jp = ip; j < n; ++j) {
      const int is_asym_coeff_j =
          (j < nsfilter_params->nsfilter_config.asymmetric ||
           (j >= ncoeffs1 &&
            j - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      if (is_asym_coeff_j && (j & 1) == 1) continue;
      const int is_merge_j = is_asym_coeff_j && (j & 1) == 0;
      if (is_merge_i && is_merge_j)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[i * stride + j + 1] +
                                   A[(i + 1) * stride + j] +
                                   A[(i + 1) * stride + j + 1];
      else if (is_merge_i)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[(i + 1) * stride + j];
      else if (is_merge_j)
        sym_A[ip * stridep + jp] = A[i * stride + j] + A[i * stride + j + 1];
      else
        sym_A[ip * stridep + jp] = A[i * stride + j];
      if (ip != jp) sym_A[jp * stridep + ip] = sym_A[ip * stridep + jp];
      jp++;
    }
    if (is_merge_i)
      sym_b[ip] = b[i] + b[i + 1];
    else
      sym_b[ip] = b[i];
    ip++;
  }
  int np = ip;
  assert(np > 0);
  assert(stridep > 0);

  // Use temporary storage to avoid modifying A and b
  double *R = tmpbuf;
  double *tmp = tmpbuf + WIENERNS_R_SIZE;

  // Set up quantization data
  double prec[WIENERNS_TAPS_MAX] = { 0 };
  int32_t min[WIENERNS_TAPS_MAX] = { 0 };
  int32_t max[WIENERNS_TAPS_MAX] = { 0 };
  int32_t scale[WIENERNS_TAPS_MAX] = { 0 };

  assert(np <= nsfilter_params->ncoeffs);

  ip = 0;
  for (int i = 0; i < n; i++) {
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1) continue;
    int min_val = nsfilter_params->coeffs[i][WIENERNS_MIN_ID];
    int range = 1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID];

    prec[ip] = (double)(1 << nsfilter_params->nsfilter_config.prec_bits);
    min[ip] = min_val;
    max[ip] = min_val + range - 1;
    scale[ip] = 1;
    ip++;
  }
  // Solve problem
  int32_t sym_x[WIENERNS_TAPS_MAX];
  int ret = linsolve_spd_quantize(np, sym_A, R, stridep, sym_b, tmp, sym_x,
                                  prec, min, max, scale);
  if (!ret) goto finished;

  // Convert results from 32-bit to 16-bit storage
  ip = 0;
  for (int i = 0; i < n; i++) {
    const int is_asym_coeff_i =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    if (is_asym_coeff_i && (i & 1) == 1)
      nsfilter[i] = nsfilter[i - 1];
    else
      nsfilter[i] = sym_x[ip++];
  }

finished:
  return ret;
}

static int compute_wienerns_filter(
    int n, const double *A, int stride, const double *b, double *tmpbuf,
    int16_t *nsfilter, const WienernsFilterParameters *nsfilter_params) {
  assert(n > 0);
  assert(stride > 0);

  // Use temporary storage to avoid modifying A and b
  double *R = tmpbuf;
  double *tmp = tmpbuf + WIENERNS_R_SIZE;

  // Set up quantization data
  double prec[WIENERNS_TAPS_MAX] = { 0 };
  int32_t min[WIENERNS_TAPS_MAX] = { 0 };
  int32_t max[WIENERNS_TAPS_MAX] = { 0 };
  int32_t scale[WIENERNS_TAPS_MAX] = { 0 };

  assert(n <= nsfilter_params->ncoeffs);
  for (int i = 0; i < n; i++) {
    int min_val = nsfilter_params->coeffs[i][WIENERNS_MIN_ID];
    int range = 1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID];

    prec[i] = (double)(1 << nsfilter_params->nsfilter_config.prec_bits);
    min[i] = min_val;
    max[i] = min_val + range - 1;
    scale[i] = 1;
  }

  // Solve problem
  int32_t x[WIENERNS_TAPS_MAX];
  int ret =
      linsolve_spd_quantize(n, A, R, stride, b, tmp, x, prec, min, max, scale);
  if (!ret) goto finished;

  // Convert results from 32-bit to 16-bit storage
  for (int i = 0; i < n; i++) {
    nsfilter[i] = x[i];
  }

finished:
  return ret;
}

static int compute_wienerns_filter_select_master_basic(
    const RestSearchCtxt *rsc, WienerNonsepInfo *filter, int c_id, int n, int s,
    const double *A, int stride, const double *b, double *tmpbuf,
    const WienernsFilterParameters *nsfilter_params, int do_sym_search) {
  double cost = DBL_MAX, cost_sym = DBL_MAX;
  int16_t *nsfilter = nsfilter_taps(filter, c_id);
  int16_t nsfilter_bak[WIENERNS_TAPS_MAX];
  const int num_feat = nsfilter_params->ncoeffs;

  memset(nsfilter, 0, num_feat * sizeof(*nsfilter));
  int linsolve_successful = 0;
  if (skip_sym_bit(nsfilter_params, s) != 2) {  // If #taps > max allowed 18,
                                                // skip and go to sym search
    linsolve_successful = compute_wienerns_filter_select(
        n, nsfilter_params->subset_config[s], A, stride, b, tmpbuf, nsfilter,
        nsfilter_params);
    if (!do_sym_search) return linsolve_successful;
    if (nsfilter_params->nsfilter_config.asymmetric +
            nsfilter_params->nsfilter_config.asymmetric2 ==
        0)
      return linsolve_successful;
    if (linsolve_successful) {
      double err = 0;
      for (int i = 0; i < num_feat; ++i) err -= nsfilter[i] * b[i];
      const int64_t bits = count_wienerns_bits_set(
          rsc->plane, &rsc->x->mode_costs, filter, NULL, nsfilter_params, c_id);
      cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4,
                                            (int64_t)err,
                                            rsc->cm->seq_params.bit_depth);
      memcpy(nsfilter_bak, nsfilter, num_feat * sizeof(*nsfilter));
    }
    memset(nsfilter, 0, num_feat * sizeof(*nsfilter));
  }
  int linsolve_successful_sym = compute_wienerns_filter_select_sym(
      n, nsfilter_params->subset_config[s], A, stride, b, tmpbuf, nsfilter,
      nsfilter_params);
  if (linsolve_successful_sym) {
    double err = 0;
    for (int i = 0; i < num_feat; ++i) err -= nsfilter[i] * b[i];
    const int64_t bits = count_wienerns_bits_set(
        rsc->plane, &rsc->x->mode_costs, filter, NULL, nsfilter_params, c_id);
    cost_sym = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, bits >> 4, (int64_t)err, rsc->cm->seq_params.bit_depth);
  }
  if (!linsolve_successful && !linsolve_successful_sym) return 0;
  if (cost < cost_sym || (linsolve_successful && !linsolve_successful_sym)) {
    memcpy(nsfilter, nsfilter_bak, num_feat * sizeof(*nsfilter));
  }
  return 1;
}

static int compute_wienerns_filter_select_master(
    const RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    const AV1PixelRect *tile_rect, RestorationUnitInfo *rui, int c_id, int n,
    int s, const double *A, int stride, const double *b, double *tmpbuf,
    const WienernsFilterParameters *nsfilter_params, int do_sym_search) {
  double cost = DBL_MAX, cost_sym = DBL_MAX;
  int16_t *nsfilter = nsfilter_taps(&rui->wienerns_info, c_id);
  int16_t nsfilter_bak[WIENERNS_TAPS_MAX];
  const int num_feat = nsfilter_params->ncoeffs;
  // const int ru_size = rsc->cm->rst_info[rsc->plane].restoration_unit_size;

  memset(nsfilter, 0, num_feat * sizeof(*nsfilter));
  int linsolve_successful = 0;
  if (skip_sym_bit(nsfilter_params, s) != 2) {
    linsolve_successful = compute_wienerns_filter_select(
        n, nsfilter_params->subset_config[s], A, stride, b, tmpbuf, nsfilter,
        nsfilter_params);
    if (!do_sym_search) return linsolve_successful;
    if (nsfilter_params->nsfilter_config.asymmetric +
            nsfilter_params->nsfilter_config.asymmetric2 ==
        0)
      return linsolve_successful;
    if (linsolve_successful) {
      const int64_t err =
          calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
      const int64_t bits = count_wienerns_bits_set(
          rsc->plane, &rsc->x->mode_costs, &rui->wienerns_info,
          &rsc->wienerns_bank, nsfilter_params, c_id);
      cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, err,
                                            rsc->cm->seq_params.bit_depth);
      // printf("[%d] Asym: e %" PRId64 " b %" PRId64 " cost %f\n", ru_size,
      // err,
      //        bits, cost);
      memcpy(nsfilter_bak, nsfilter, num_feat * sizeof(*nsfilter));
    }
    memset(nsfilter, 0, num_feat * sizeof(*nsfilter));
  }
  int linsolve_successful_sym = compute_wienerns_filter_select_sym(
      n, nsfilter_params->subset_config[s], A, stride, b, tmpbuf, nsfilter,
      nsfilter_params);
  if (linsolve_successful_sym) {
    const int64_t err =
        calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    const int64_t bits = count_wienerns_bits_set(
        rsc->plane, &rsc->x->mode_costs, &rui->wienerns_info,
        &rsc->wienerns_bank, nsfilter_params, c_id);
    cost_sym = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, err,
                                              rsc->cm->seq_params.bit_depth);
    // printf("[%d] Sym:  e %" PRId64 " b %" PRId64 " cost %f\n", ru_size, err,
    //        bits, cost_sym);
  }
  if (!linsolve_successful && !linsolve_successful_sym) return 0;
  if (cost < cost_sym || (linsolve_successful && !linsolve_successful_sym)) {
    memcpy(nsfilter, nsfilter_bak, num_feat * sizeof(*nsfilter));
  }
  return 1;
}

int compute_wienerns_filter_master_basic(
    const RestSearchCtxt *rsc, WienerNonsepInfo *filter, int c_id, int n,
    const double *A, int stride, const double *b, double *tmpbuf,
    const WienernsFilterParameters *nsfilter_params, int do_sym_search) {
  double cost = DBL_MAX, cost_sym = DBL_MAX;
  int16_t *nsfilter = nsfilter_taps(filter, c_id);
  int16_t nsfilter_bak[WIENERNS_TAPS_MAX];
  const int num_feat = nsfilter_params->ncoeffs;

  int linsolve_successful = compute_wienerns_filter(n, A, stride, b, tmpbuf,
                                                    nsfilter, nsfilter_params);
  if (!do_sym_search) return linsolve_successful;
  if (linsolve_successful) {
    double err = 0;
    for (int i = 0; i < num_feat; ++i) err -= nsfilter[i] * b[i];
    const int64_t bits =
        count_wienerns_bits_set(rsc->plane, &rsc->x->mode_costs, filter,
                                &rsc->wienerns_bank, nsfilter_params, c_id);
    cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, bits >> 4, (int64_t)err, rsc->cm->seq_params.bit_depth);
    memcpy(nsfilter_bak, nsfilter, num_feat * sizeof(*nsfilter));
  }
  int linsolve_successful_sym = compute_wienerns_filter_sym(
      n, A, stride, b, tmpbuf, nsfilter, nsfilter_params);
  if (linsolve_successful_sym) {
    double err = 0;
    for (int i = 0; i < num_feat; ++i) err -= nsfilter[i] * b[i];
    const int64_t bits =
        count_wienerns_bits_set(rsc->plane, &rsc->x->mode_costs, filter,
                                &rsc->wienerns_bank, nsfilter_params, c_id);
    cost_sym = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, bits >> 4, (int64_t)err, rsc->cm->seq_params.bit_depth);
  }
  if (!linsolve_successful && !linsolve_successful_sym) return 0;
  if (cost < cost_sym || (linsolve_successful && !linsolve_successful_sym)) {
    memcpy(nsfilter, nsfilter_bak, num_feat * sizeof(*nsfilter));
  }
  return 1;
}

int compute_wienerns_filter_master(
    RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    const AV1PixelRect *tile_rect, RestorationUnitInfo *rui, int c_id, int n,
    const double *A, int stride, const double *b, double *tmpbuf,
    const WienernsFilterParameters *nsfilter_params, int do_sym_search) {
  double cost = DBL_MAX, cost_sym = DBL_MAX;
  int16_t *nsfilter = nsfilter_taps(&rui->wienerns_info, c_id);
  int16_t nsfilter_bak[WIENERNS_TAPS_MAX];
  const int num_feat = nsfilter_params->ncoeffs;

  int linsolve_successful = compute_wienerns_filter(n, A, stride, b, tmpbuf,
                                                    nsfilter, nsfilter_params);
  if (!do_sym_search) return linsolve_successful;
  if (linsolve_successful) {
    const int64_t err =
        calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    const int64_t bits = count_wienerns_bits_set(
        rsc->plane, &rsc->x->mode_costs, &rui->wienerns_info,
        &rsc->wienerns_bank, nsfilter_params, c_id);
    cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, err,
                                          rsc->cm->seq_params.bit_depth);
    memcpy(nsfilter_bak, nsfilter, num_feat * sizeof(*nsfilter));
  }
  int linsolve_successful_sym = compute_wienerns_filter_sym(
      n, A, stride, b, tmpbuf, nsfilter, nsfilter_params);
  if (linsolve_successful_sym) {
    const int64_t err =
        calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    const int64_t bits = count_wienerns_bits_set(
        rsc->plane, &rsc->x->mode_costs, &rui->wienerns_info,
        &rsc->wienerns_bank, nsfilter_params, c_id);
    cost_sym = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, err,
                                              rsc->cm->seq_params.bit_depth);
  }
  if (!linsolve_successful && !linsolve_successful_sym) return 0;
  if (cost < cost_sym || (linsolve_successful && !linsolve_successful_sym)) {
    memcpy(nsfilter, nsfilter_bak, num_feat * sizeof(*nsfilter));
  }
  return 1;
}

static int64_t compute_stats_for_wienerns_filter(
    const uint16_t *dgd_hbd, const uint16_t *src_hbd,
    const RestorationTileLimits *limits, int dgd_stride, int src_stride,
    const RestorationUnitInfo *rui, int bit_depth, double *A, double *b,
    int *num_pixels_in_class, const WienernsFilterParameters *nsfilter_params,
    int num_classes) {
  (void)rui;
  const uint16_t *luma_hbd = rui->luma;

  const int total_dim_A = num_classes * WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int total_dim_b = num_classes * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;
#if CONFIG_COMBINE_PC_NS_WIENER
  const int set_index =
      get_filter_set_index(rui->base_qindex + rui->qindex_offset);
  const uint8_t *pc_wiener_sub_classify =
      get_pc_wiener_sub_classifier(num_classes, set_index);
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  int16_t buf[WIENERNS_TAPS_MAX];
  memset(A, 0, sizeof(*A) * total_dim_A);
  memset(b, 0, sizeof(*b) * total_dim_b);
  memset(num_pixels_in_class, 0, sizeof(*num_pixels_in_class) * num_classes);

  const int(*wienerns_config)[3] = nsfilter_params->nsfilter_config.config;
  int is_uv = (rui->plane != AOM_PLANE_Y);
  const int(*wienerns_config2)[3] =
      is_uv ? nsfilter_params->nsfilter_config.config2 : NULL;
  const int end_pixel = is_uv ? nsfilter_params->nsfilter_config.num_pixels +
                                    nsfilter_params->nsfilter_config.num_pixels2
                              : nsfilter_params->nsfilter_config.num_pixels;
  const int num_feat = nsfilter_params->ncoeffs;

  int64_t real_sse = 0;  // for debuggung purposes
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    for (int i = limits->v_start; i < limits->v_end; ++i) {
      for (int j = limits->h_start; j < limits->h_end; ++j) {
        int dgd_id = i * dgd_stride + j;
        int src_id = i * src_stride + j;
#if CONFIG_COMBINE_PC_NS_WIENER
        // Skip pixel if not of sub_class_id.
        if (num_classes > 1) {
          const int full_class_id =
              rui->wiener_class_id[(i >> MI_SIZE_LOG2) *
                                       rui->wiener_class_id_stride +
                                   (j >> MI_SIZE_LOG2)];
          const int sub_class_id = pc_wiener_sub_classify[full_class_id];
          if (c_id != sub_class_id) continue;
        }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
        int luma_id = i * rui->luma_stride + j;
        memset(buf, 0, sizeof(buf));
        for (int k = 0; k < end_pixel; ++k) {
          const int cross =
              (is_uv && k >= nsfilter_params->nsfilter_config.num_pixels);

          if (!cross) {
            const int pos = wienerns_config[k][WIENERNS_BUF_POS];
            const int r = wienerns_config[k][WIENERNS_ROW_ID];
            const int c = wienerns_config[k][WIENERNS_COL_ID];
            if (r == 0 && c == 0) {
              buf[pos] += 1;
              continue;
            }
            buf[pos] +=
                clip_base((int16_t)dgd_hbd[(i + r) * dgd_stride + (j + c)] -
                              (int16_t)dgd_hbd[dgd_id],
                          bit_depth);
          } else {
            const int k2 = k - nsfilter_params->nsfilter_config.num_pixels;
            const int pos = wienerns_config2[k2][WIENERNS_BUF_POS];
            const int r = wienerns_config2[k2][WIENERNS_ROW_ID];
            const int c = wienerns_config2[k2][WIENERNS_COL_ID];

            buf[pos] += clip_base(
                (int16_t)luma_hbd[(i + r) * rui->luma_stride + (j + c)] -
                    (int16_t)luma_hbd[luma_id],
                bit_depth);
          }
        }
        int16_t y;
        y = ((int64_t)src_hbd[src_id] - dgd_hbd[dgd_id]);
        for (int k = 0; k < num_feat; ++k) {
          for (int l = 0; l <= k; ++l) {
            A[k * num_feat + l + c_id * stride_A] +=
                (double)buf[k] * (double)buf[l];
          }
          b[k + c_id * stride_b] += (double)buf[k] * (double)y;
        }
        real_sse += (int64_t)y * (int64_t)y;
        ++num_pixels_in_class[c_id];
      }
    }
    for (int k = 0; k < num_feat; ++k) {
      for (int l = k + 1; l < num_feat; ++l) {
        A[k * num_feat + l + c_id * stride_A] =
            A[l * num_feat + k + c_id * stride_A];
      }
    }
  }
  return real_sse;
}

static int compute_quantized_wienerns_filter(
    RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    const AV1PixelRect *tile_rect, RestorationUnitInfo *rui, const double *A,
    const double *b, int64_t real_sse,
    const WienernsFilterParameters *nsfilter_params) {
  const int num_classes = rsc->num_filter_classes;
  assert(num_classes == rsc->wienerns_bank.filter[0].num_classes);
  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int total_dim_b = num_classes * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;

  double solver_x[WIENERNS_MAX_CLASSES * WIENERNS_TAPS_MAX];
  const int num_feat = nsfilter_params->ncoeffs;

  int ret = 0;
  WienerNonsepInfo best = { 0 };
  best.num_classes = num_classes;
  double best_cost = DBL_MAX;

  assert((num_feat & 1) == 0);
  for (int s = nsfilter_params->nsubsets - 1; s >= 0; --s) {
    memset(solver_x, 0, sizeof(*solver_x) * total_dim_b);
    config2ncoeffs_select(&nsfilter_params->nsfilter_config,
                          nsfilter_params->subset_config[s], NULL, NULL);
    int linsolve_successful = 0;
    for (int c_id = 0; c_id < num_classes; ++c_id) {
      int16_t *nsfilter = nsfilter_taps(&rui->wienerns_info, c_id);
      memset(nsfilter, 0, num_feat * sizeof(*nsfilter));
      linsolve_successful = compute_wienerns_filter_select_master(
          rsc, limits, tile_rect, rui, c_id, num_feat, s, A + c_id * stride_A,
          num_feat, b + c_id * stride_b, rsc->wienerns_tmpbuf, nsfilter_params,
          0);
    }
    if (linsolve_successful) {
      do {
        assert(rui->wiener_class_id_restrict == -1);
        int64_t real_errq =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
        int64_t bits = count_wienerns_bits_set(
            rui->plane, &rsc->x->mode_costs, &rui->wienerns_info,
            &rsc->wienerns_bank, nsfilter_params, ALL_WIENERNS_CLASSES);
        double cost =
            RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, real_errq,
                                           rsc->cm->seq_params.bit_depth);
        // Check if found filter is worse than no filtering.
        if (real_errq <= real_sse && cost < best_cost) {
          best_cost = cost;
          copy_nsfilter_taps(&best, &rui->wienerns_info);
          ret = 1;
        } else {
          copy_nsfilter_taps(&rui->wienerns_info, &best);
        }
      } while (0);
    }
  }
  if (ret) {
    copy_nsfilter_taps(&rui->wienerns_info, &best);
  }
  return ret;
}

int get_merge_begin_index(const RestSearchCtxt *rsc,
                          const WienernsFilterParameters *nsfilter_params,
                          const WienerNonsepInfo *token_wienerns_info,
                          Vector *current_unit_stack,
                          WienerNonsepInfoBank **begin_bank,
                          int wiener_class_id) {
  int begin_idx = -1;
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;
  int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
    RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
    RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
    if (old_unit->rest_unit_idx == last_idx) continue;
    if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
            RESTORE_WIENER_NONSEP &&
        check_wienerns_eq(&old_rusi->wienerns_info, token_wienerns_info,
                          nsfilter_params->ncoeffs, wiener_class_id)) {
      // Same filter as before.
      if (check_wienerns_bank_eq(&old_unit->ref_wienerns_bank,
                                 token_wienerns_info, nsfilter_params->ncoeffs,
                                 wiener_class_id, equal_ref_for_class) == -1) {
        // Head merge point for this filter.
        begin_idx = old_unit->rest_unit_idx;
        // Set merge-leader's bank.
        *begin_bank = &old_unit->ref_wienerns_bank;
      }
    }
  }
  return begin_idx;
}

void populate_current_unit_indices(
    const RestSearchCtxt *rsc, const WienernsFilterParameters *nsfilter_params,
    const WienerNonsepInfo *token_wienerns_info, int begin_idx_cand,
    Vector *current_unit_stack, Vector *current_unit_indices,
    int wiener_class_id) {
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;
  bool has_begun = false;
  VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
    RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
    RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
    if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
    if (!has_begun) continue;
    if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
            RESTORE_WIENER_NONSEP &&
        old_unit->rest_unit_idx != last_idx &&
        !check_wienerns_eq(&old_rusi->wienerns_info, token_wienerns_info,
                           nsfilter_params->ncoeffs, wiener_class_id))
      continue;
    int index = old_unit->rest_unit_idx;
    aom_vector_push_back(current_unit_indices, &index);
  }
}

double set_cand_merge_sse_and_bits(
    RestSearchCtxt *rsc, const WienernsFilterParameters *nsfilter_params,
    const AV1PixelRect *tile_rect, int begin_idx_cand,
    Vector *current_unit_stack, WienerNonsepInfo *token_wienerns_info_cand,
    RestorationUnitInfo *rui_merge_cand, int wiener_class_id) {
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;
  const int is_uv = (rsc->plane != AOM_PLANE_Y);
  const MACROBLOCK *const x = rsc->x;
  const int bit_depth = rsc->cm->seq_params.bit_depth;

  double cost_merge_cand = 0;
  int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  rui_merge_cand->wiener_class_id_restrict = wiener_class_id;
  bool has_begun = false;
  VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
    RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
    RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
    if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
    if (!has_begun) continue;
    if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
            RESTORE_WIENER_NONSEP &&
        old_unit->rest_unit_idx != last_idx &&
        !check_wienerns_eq(&old_rusi->wienerns_info, token_wienerns_info_cand,
                           nsfilter_params->ncoeffs, wiener_class_id))
      continue;

    old_unit->merge_sse_cand =
        try_restoration_unit(rsc, &old_unit->limits, tile_rect, rui_merge_cand);
    // First unit in stack has larger unit_bits because the
    // merged coeffs are linked to it.
    if (old_unit->rest_unit_idx == begin_idx_cand) {
      // The first unit will have a different filter
      // (rui_merge_cand->wienerns_info) to signal at wiener_class_id, same
      // filters elsewhere.
      WienerNonsepInfo tmp_filters = old_rusi->wienerns_info;
      copy_nsfilter_taps_for_class(&tmp_filters, &rui_merge_cand->wienerns_info,
                                   wiener_class_id);
      const int new_bits = (int)count_wienerns_bits_set(
          is_uv, &x->mode_costs, &tmp_filters, &old_unit->ref_wienerns_bank,
          nsfilter_params, ALL_WIENERNS_CLASSES);
      old_unit->merge_bits_cand =
          x->mode_costs.wienerns_restore_cost[1] + new_bits;
    } else if (old_unit->rest_unit_idx != last_idx) {
      const int is_equal = check_wienerns_bank_eq(
          &old_unit->ref_wienerns_bank, token_wienerns_info_cand,
          nsfilter_params->ncoeffs, wiener_class_id, equal_ref_for_class);
      (void)is_equal;
      assert(is_equal >= 0);  // Must exist in bank
      const int merge_bits = (int)count_wienerns_bits(
          is_uv, &x->mode_costs, &old_rusi->wienerns_info,
          &old_unit->ref_wienerns_bank, nsfilter_params, ALL_WIENERNS_CLASSES);
      assert(merge_bits == count_wienerns_bits_set(
                               is_uv, &x->mode_costs, &old_rusi->wienerns_info,
                               &old_unit->ref_wienerns_bank, nsfilter_params,
                               ALL_WIENERNS_CLASSES));
      old_unit->merge_bits_cand =
          x->mode_costs.wienerns_restore_cost[1] + merge_bits;
    } else {
      // This should be the last RU in the chain we are optimizing.
      // Old bank is not updated. Use the old value in token_wienerns_info_cand
      // to calculate the merge-ref.
      const int is_equal = check_wienerns_bank_eq(
          &old_unit->ref_wienerns_bank, token_wienerns_info_cand,
          nsfilter_params->ncoeffs, wiener_class_id, equal_ref_for_class);
      (void)is_equal;
      assert(is_equal >= 0);  // Must exist in bank

      // token_wienerns_info_cand has the best filters for classes <
      // wiener_class_id and the token filter at wiener_class_id. Remaining
      // filters are the computed RU filters that have not entered the merge
      // trial.
      token_wienerns_info_cand->bank_ref_for_class[wiener_class_id] =
          equal_ref_for_class[wiener_class_id];

      // Using count_wienerns_bits_set just in case.
      const int merge_bits = (int)count_wienerns_bits_set(
          is_uv, &x->mode_costs, token_wienerns_info_cand,
          &old_unit->ref_wienerns_bank, nsfilter_params, ALL_WIENERNS_CLASSES);
      old_unit->merge_bits_cand =
          x->mode_costs.wienerns_restore_cost[1] + merge_bits;
    }
    cost_merge_cand += RDCOST_DBL_WITH_NATIVE_BD_DIST(
        x->rdmult, old_unit->merge_bits_cand >> 4, old_unit->merge_sse_cand,
        bit_depth);
  }
  return cost_merge_cand;
}

double accumulate_merge_stats(const RestSearchCtxt *rsc,
                              const WienernsFilterParameters *nsfilter_params,
                              const WienerNonsepInfo *ref_wienerns_info_cand,
                              int begin_idx_cand, Vector *current_unit_stack,
                              Vector *current_unit_indices,
                              double *solver_A_AVG, double *solver_b_AVG,
                              int dim_A, int dim_b, int offset_A, int offset_b,
                              int wiener_class_id) {
  (void)current_unit_indices;
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;
  const MACROBLOCK *const x = rsc->x;
  const int bit_depth = rsc->cm->seq_params.bit_depth;
  double cost_nomerge_cand = 0;
  bool has_begun = false;
  int num_units = 0;
  VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
    RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
    RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
    if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
    if (!has_begun) continue;
    if (old_unit->rest_unit_idx == last_idx) continue;
    if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
            RESTORE_WIENER_NONSEP &&
        !check_wienerns_eq(&old_rusi->wienerns_info, ref_wienerns_info_cand,
                           nsfilter_params->ncoeffs, wiener_class_id))
      continue;

    cost_nomerge_cand +=
        RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult, old_unit->current_bits >> 4,
                                       old_unit->current_sse, bit_depth);

    for (int index = 0; index < dim_A; ++index) {
      solver_A_AVG[index] += old_unit->A[index + offset_A];
    }
    for (int index = 0; index < dim_b; ++index) {
      solver_b_AVG[index] += old_unit->b[index + offset_b];
    }
    num_units++;
  }
  assert(num_units + 1 == (int)current_unit_indices->size);
  // Divide A and b by vector size + 1 to get average.
  for (int index = 0; index < dim_A; ++index) {
    solver_A_AVG[index] = DIVIDE_AND_ROUND(solver_A_AVG[index], num_units + 1);
  }
  for (int index = 0; index < dim_b; ++index) {
    solver_b_AVG[index] = DIVIDE_AND_ROUND(solver_b_AVG[index], num_units + 1);
  }

  return cost_nomerge_cand;
}

static void gather_stats_wienerns(const RestorationTileLimits *limits,
                                  const AV1PixelRect *tile_rect,
                                  int rest_unit_idx,
                                  int rest_unit_idx_in_rutile, void *priv,
                                  int32_t *tmpbuf,
                                  RestorationLineBuffers *rlbs) {
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_in_rutile;
  (void)tile_rect;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;

  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
#if ISSUE_253
  rui.luma = rsc->luma_stat;
#endif  // ISSUE_253
  rui.restoration_type = RESTORE_WIENER_NONSEP;
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  assert(rsc->num_filter_classes == rsc->wienerns_bank.filter[0].num_classes);

  // Calculate and save this RU's stats.
  RstUnitStats unit_stats;
  unit_stats.real_sse = compute_stats_for_wienerns_filter(
      rsc->dgd_buffer, rsc->src_buffer, limits, rsc->dgd_stride,
      rsc->src_stride, &rui, rsc->cm->seq_params.bit_depth, unit_stats.A,
      unit_stats.b, unit_stats.num_pixels_in_class, nsfilter_params,
      rsc->num_stats_classes);
  unit_stats.ru_idx = rest_unit_idx;
  unit_stats.ru_idx_in_tile = rest_unit_idx_in_rutile - rsc->ru_idx_base;
  unit_stats.limits = *limits;
  unit_stats.plane = rsc->plane;
  unit_stats.num_stats_classes = rsc->num_stats_classes;
  aom_vector_push_back(rsc->wienerns_stats, &unit_stats);
  return;
}

#if CONFIG_COMBINE_PC_NS_WIENER

// Returns the sse of using the frame filters on the RU specified with limits.
static int64_t evaluate_frame_filter(RestSearchCtxt *rsc,
                                     const RestorationTileLimits *limits,
                                     RestorationUnitInfo *rui) {
  int num_classes = rsc->num_filter_classes;
  assert(rui->wienerns_info.num_classes == num_classes);

  // Assume one set of filters for now.
  const int num_frame_filters = rsc->frame_filter_bank.bank_size_for_class[0];
  (void)num_frame_filters;
  assert(num_frame_filters == 1);
  for (int c_id = 1; c_id < num_classes; ++c_id) {
    assert(rsc->frame_filter_bank.bank_size_for_class[c_id] ==
           num_frame_filters);
  }

  // Copy from bank to rui, all classes are at the same ref in the dictionary.
  const int frame_filter_ref = 0;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    const WienerNonsepInfo *bank_info = av1_constref_from_wienerns_bank(
        &rsc->frame_filter_bank, frame_filter_ref, c_id);
    copy_nsfilter_taps_for_class(&rui->wienerns_info, bank_info, c_id);
    rui->wienerns_info.match_indices[c_id] = bank_info->match_indices[c_id];
    rui->wienerns_info.bank_ref_for_class[c_id] = 0;
  }

  // Evaluate on RU using all classes.
  rui->wiener_class_id_restrict = -1;
  const int64_t sse =
      calc_finer_tile_search_error(rsc, limits, &rsc->tile_rect, rui);
  return sse;
}

// Determines the cost of using the frame filters on an RU
// (RESTORE_WIENER_NONSEP mode), compares the cost to none (RESTORE_NONE), and
// updates the RU mode, sse, bits accordingly.
static int64_t decide_wienerns_on_off(RestSearchCtxt *rsc, int rest_unit_idx,
                                      double cost_none, int64_t bits_none) {
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  const MACROBLOCK *const x = rsc->x;
  const int bit_depth = rsc->cm->seq_params.bit_depth;

  const WienerNonsepInfoBank *bank_to_use = &rsc->frame_filter_bank;

  int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  const int is_equal = check_wienerns_bank_eq(
      bank_to_use, &rusi->wienerns_info, nsfilter_params->ncoeffs,
      ALL_WIENERNS_CLASSES, equal_ref_for_class);
  (void)is_equal;
  assert(is_equal == 0);

  const int num_frame_filters = rsc->frame_filter_bank.bank_size_for_class[0];
  (void)num_frame_filters;
  for (int c_id = 1; c_id < rsc->num_filter_classes; ++c_id) {
    assert(rsc->frame_filter_bank.bank_size_for_class[c_id] ==
           num_frame_filters);
  }
  // Assume one set of filters for now.
  assert(num_frame_filters == 1);
  const int64_t filter_tap_bits = 0;
  const int64_t bits_wienerns =
      x->mode_costs.wienerns_restore_cost[1] + filter_tap_bits;
  double cost_wienerns = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_wienerns >> 4, rusi->sse[RESTORE_WIENER_NONSEP],
      bit_depth);

  const RestorationType rtype =
      (cost_wienerns < cost_none) ? RESTORE_WIENER_NONSEP : RESTORE_NONE;
  rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] = rtype;
  rsc->sse += rusi->sse[rtype];
  const int64_t bits = (cost_wienerns < cost_none) ? bits_wienerns : bits_none;
  rsc->bits += bits;

  if (cost_wienerns < cost_none)
    av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                             ALL_WIENERNS_CLASSES);
  return bits;
}

#endif  // CONFIG_COMBINE_PC_NS_WIENER

static void search_wienerns_visitor(const RestorationTileLimits *limits,
                                    const AV1PixelRect *tile_rect,
                                    int rest_unit_idx,
                                    int rest_unit_idx_in_rutile, void *priv,
                                    int32_t *tmpbuf,
                                    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_in_rutile;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const MACROBLOCK *const x = rsc->x;
  const int64_t bits_none = x->mode_costs.wienerns_restore_cost[0];
  const int bit_depth = rsc->cm->seq_params.bit_depth;
  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);

  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
#if CONFIG_COMBINE_PC_NS_WIENER
  // Classification has already been calculated by search_pc_wiener_visitor().
  rui.compute_classification = 0;
  if (is_frame_filters_enabled(rsc->plane)) {
    // Ensure search_pc_wiener_visitor was done and classification was computed.
    assert(rsc->classification_is_buffered);
  } else {
    assert(!rsc->classification_is_buffered);
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
  rui.restoration_type = RESTORE_WIENER_NONSEP;
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);

  const RstUnitStats *unit_stats = (const RstUnitStats *)aom_vector_const_get(
      rsc->wienerns_stats, rest_unit_idx_in_rutile);
  assert(unit_stats->ru_idx == rest_unit_idx);
  assert(unit_stats->ru_idx_in_tile + rsc->ru_idx_base ==
         rest_unit_idx_in_rutile);
  assert(unit_stats->plane == rsc->plane);
  assert(rusi->sse[RESTORE_NONE] == unit_stats->real_sse);
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsc->frame_filters_on && is_frame_filters_enabled(rsc->plane)) {
    // Pick the best filter for this RU.
    rusi->sse[RESTORE_WIENER_NONSEP] = evaluate_frame_filter(rsc, limits, &rui);

    assert(rusi->sse[RESTORE_WIENER_NONSEP] != INT64_MAX);
    rusi->wienerns_info = rui.wienerns_info;
    // Pick among filtering or RESTORE_NONE.
    decide_wienerns_on_off(rsc, rest_unit_idx, cost_none, bits_none);
    if (rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] == RESTORE_WIENER_NONSEP)
      rsc->num_wiener_nonsep++;
    return;
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
  if (!compute_quantized_wienerns_filter(
          rsc, limits, &rsc->tile_rect, &rui, unit_stats->A, unit_stats->b,
          unit_stats->real_sse, nsfilter_params)) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_WIENER_NONSEP] = INT64_MAX;
    return;
  }
  aom_clear_system_state();
  rusi->sse[RESTORE_WIENER_NONSEP] = finer_tile_search_wienerns(
      rsc, limits, &rsc->tile_rect, &rui, nsfilter_params, 1,
      &rsc->wienerns_bank, ALL_WIENERNS_CLASSES);

  rusi->wienerns_info = rui.wienerns_info;
  assert(rusi->sse[RESTORE_WIENER_NONSEP] != INT64_MAX);

  const int num_classes = rsc->num_filter_classes;
  assert(num_classes == rsc->wienerns_bank.filter[0].num_classes);
  if (num_classes > 1) {
    rui.wiener_class_id_restrict = -1;
    calc_finer_tile_search_error(rsc, limits, &rsc->tile_rect, &rui);
  }
  double solver_A_AVG[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX];
  const int class_dim_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  double solver_b_AVG[WIENERNS_TAPS_MAX];
  const int class_dim_b = WIENERNS_TAPS_MAX;
  const int coeffs_dim_A = nsfilter_params->ncoeffs * nsfilter_params->ncoeffs;
  const int coeffs_dim_b = nsfilter_params->ncoeffs;

  int is_uv = (rsc->plane != AOM_PLANE_Y);
  Vector *current_unit_stack = rsc->unit_stack;
  int64_t bits_nomerge_base =
      x->mode_costs.wienerns_restore_cost[1] +
      count_wienerns_bits_set(rsc->plane, &x->mode_costs, &rusi->wienerns_info,
                              &rsc->wienerns_bank, nsfilter_params,
                              ALL_WIENERNS_CLASSES);

  // Only test the reference in rusi->wienerns_info.bank_ref, generated from
  // the count call above.
  int ns_bank_ref_base[WIENERNS_MAX_CLASSES];
  memcpy(ns_bank_ref_base, rusi->wienerns_info.bank_ref_for_class,
         num_classes * sizeof(*ns_bank_ref_base));

  // Copy the bank_refs to rui.
  memcpy(rui.wienerns_info.bank_ref_for_class,
         rusi->wienerns_info.bank_ref_for_class,
         num_classes * sizeof(*ns_bank_ref_base));
  double cost_nomerge_base = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_nomerge_base >> 4, rusi->sse[RESTORE_WIENER_NONSEP],
      bit_depth);
  const int bits_min = x->mode_costs.wienerns_restore_cost[1] +
                       x->mode_costs.merged_param_cost[1] +
                       (1 << AV1_PROB_COST_SHIFT);
  const double cost_min = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_min >> 4, rusi->sse[RESTORE_WIENER_NONSEP], bit_depth);
  const double cost_nomerge_thr = (cost_nomerge_base + 3 * cost_min) / 4;
  const RestorationType rtype =
      (cost_none <= cost_nomerge_thr) ? RESTORE_NONE : RESTORE_WIENER_NONSEP;
  if (cost_none <= cost_nomerge_thr) {
    bits_nomerge_base = bits_none;
    cost_nomerge_base = cost_none;
  }

  RstUnitSnapshot unit_snapshot;
  memset(&unit_snapshot, 0, sizeof(unit_snapshot));
  unit_snapshot.limits = *limits;
  unit_snapshot.rest_unit_idx = rest_unit_idx;
  unit_snapshot.A = unit_stats->A;
  unit_snapshot.b = unit_stats->b;
  rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] = rtype;
  rsc->sse += rusi->sse[rtype];
  rsc->bits += bits_nomerge_base;
  unit_snapshot.current_sse = rusi->sse[rtype];
  unit_snapshot.current_bits = bits_nomerge_base;
  // Only matters for first unit in stack.
  unit_snapshot.ref_wienerns_bank = rsc->wienerns_bank;
  // If current_unit_stack is empty, we can leave early.
  if (aom_vector_is_empty(current_unit_stack)) {
    if (rtype == RESTORE_WIENER_NONSEP)
      av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                               ALL_WIENERNS_CLASSES);
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    if (rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] == RESTORE_WIENER_NONSEP)
      rsc->num_wiener_nonsep++;
    return;
  }
  // Handles special case where no-merge filter is equal to merged
  // filter for the stack - we don't want to perform another merge and
  // get a less optimal filter, but we want to continue building the stack.
  int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  if (rtype == RESTORE_WIENER_NONSEP &&
      check_wienerns_bank_eq(&rsc->wienerns_bank, &rusi->wienerns_info,
                             nsfilter_params->ncoeffs, ALL_WIENERNS_CLASSES,
                             equal_ref_for_class) >= 0) {
    rsc->bits -= bits_nomerge_base;
    memcpy(rusi->wienerns_info.bank_ref_for_class, equal_ref_for_class,
           rusi->wienerns_info.num_classes * (*equal_ref_for_class));
    unit_snapshot.current_bits =
        x->mode_costs.wienerns_restore_cost[1] +
        count_wienerns_bits_set(is_uv, &x->mode_costs, &rusi->wienerns_info,
                                &rsc->wienerns_bank, nsfilter_params,
                                ALL_WIENERNS_CLASSES);
    rsc->bits += unit_snapshot.current_bits;
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    if (rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] == RESTORE_WIENER_NONSEP)
      rsc->num_wiener_nonsep++;
    return;
  }
  // Push current unit onto stack.
  aom_vector_push_back(current_unit_stack, &unit_snapshot);
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;

  double cost_merge = DBL_MAX;
  double cost_nomerge = 0;
  int begin_idx[WIENERNS_MAX_CLASSES];
  int bank_ref[WIENERNS_MAX_CLASSES];

  // Set rui_merge_best as the current best filters with the best refs.
  RestorationUnitInfo rui_merge_best = rui;

  // Trial start
  int merged_class_count = 0;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    bank_ref[c_id] = -1;
    begin_idx[c_id] = -1;
    for (int bank_ref_cand = 0;
         bank_ref_cand <
         AOMMAX(1, rsc->wienerns_bank.bank_size_for_class[c_id]);
         bank_ref_cand++) {
#if MERGE_DRL_SEARCH_LEVEL == 1
      // Only check the best and zero references for the solved filter.
      if (bank_ref_cand != 0 && bank_ref_cand != ns_bank_ref_base[c_id])
        continue;
#elif MERGE_DRL_SEARCH_LEVEL == 2
      // Only check the best reference for the solved filter.
      if (bank_ref_cand != ns_bank_ref_base[c_id]) continue;
#else
      (void)ns_bank_ref_base;
#endif

      // Needed to track the set of merge candidate RUs.
      // set_merge_sse_and_bits() uses ALL_WIENERNS_CLASSES to calculate bits.
      // Hence initialize with the best filters we have from rui_merge_best but
      // use the c_id filter from the bank. The latter is needed to calculate
      // merge bits for c_id, the former all other bits.
      WienerNonsepInfo token_wienerns_info_cand = rui_merge_best.wienerns_info;
      copy_nsfilter_taps_for_class(
          &token_wienerns_info_cand,
          av1_constref_from_wienerns_bank(&rsc->wienerns_bank, bank_ref_cand,
                                          c_id),
          c_id);
      token_wienerns_info_cand.bank_ref_for_class[c_id] = bank_ref_cand;

      // Keep track of would be merge leader's bank.
      WienerNonsepInfoBank *begin_wienerns_bank = NULL;
      // Get the begin unit of the run using the candidate taps.
      int begin_idx_cand =
          get_merge_begin_index(rsc, nsfilter_params, &token_wienerns_info_cand,
                                current_unit_stack, &begin_wienerns_bank, c_id);
      if (begin_idx_cand == -1) continue;
      assert(begin_wienerns_bank != NULL);
      begin_wienerns_bank = begin_wienerns_bank != NULL ? &rsc->wienerns_bank
                                                        : begin_wienerns_bank;

      // Populate current_unit_indices with the indices of RUs using this
      // filter.
      Vector *current_unit_indices = rsc->unit_indices;
      aom_vector_clear(current_unit_indices);
      populate_current_unit_indices(
          rsc, nsfilter_params, &token_wienerns_info_cand, begin_idx_cand,
          current_unit_stack, current_unit_indices, c_id);

      // Initialize stats.
      double cost_nomerge_cand = cost_nomerge_base;
      const int offset_A = c_id * class_dim_A;
      memcpy(solver_A_AVG, unit_stats->A + offset_A,
             coeffs_dim_A * sizeof(*unit_stats->A));

      const int offset_b = c_id * class_dim_b;
      memcpy(solver_b_AVG, unit_stats->b + offset_b,
             coeffs_dim_b * sizeof(*unit_stats->b));

      // Get current cost and the average of A and b.
      cost_nomerge_cand += accumulate_merge_stats(
          rsc, nsfilter_params, &token_wienerns_info_cand, begin_idx_cand,
          current_unit_stack, current_unit_indices, solver_A_AVG, solver_b_AVG,
          coeffs_dim_A, coeffs_dim_b, offset_A, offset_b, c_id);

      // Generate new filter.
      RestorationUnitInfo rui_merge_cand = rui_merge_best;
      rui_merge_cand.restoration_type = RESTORE_WIENER_NONSEP;

      const int num_feat = nsfilter_params->ncoeffs;
      int linsolve_successful = compute_wienerns_filter_select_master(
          rsc, limits, &rsc->tile_rect, &rui_merge_cand, c_id, num_feat,
          nsfilter_params->nsubsets - 1, solver_A_AVG, num_feat, solver_b_AVG,
          rsc->wienerns_tmpbuf, nsfilter_params, 0);
      if (!linsolve_successful) continue;

      aom_clear_system_state();

      // After this call rsc will have updated buffers. We will reset below if
      // not merging.
      finer_tile_search_wienerns(rsc, NULL, &rsc->tile_rect, &rui_merge_cand,
                                 nsfilter_params, 1, begin_wienerns_bank, c_id);

      // Iterate through vector to set candidate merge sse and bits on
      // current_unit_stack.
      const double cost_merge_cand = set_cand_merge_sse_and_bits(
          rsc, nsfilter_params, &rsc->tile_rect, begin_idx_cand,
          current_unit_stack, &token_wienerns_info_cand, &rui_merge_cand, c_id);

      // Find the candidate that brings the largest improvement over touched
      // RUs. The best such candidate can still be worse than nomerge.
      if (cost_merge_cand - cost_nomerge_cand < cost_merge - cost_nomerge) {
        begin_idx[c_id] = begin_idx_cand;
        bank_ref[c_id] = bank_ref_cand;
        cost_merge = cost_merge_cand;
        cost_nomerge = cost_nomerge_cand;
        bool has_begun = false;
        VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
          RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
          RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
          if (old_unit->rest_unit_idx == begin_idx_cand) has_begun = true;
          if (!has_begun) continue;
          if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
                  RESTORE_WIENER_NONSEP &&
              old_unit->rest_unit_idx != last_idx &&
              !check_wienerns_eq(&old_rusi->wienerns_info,
                                 &token_wienerns_info_cand,
                                 nsfilter_params->ncoeffs, c_id))
            continue;
          old_unit->merge_sse = old_unit->merge_sse_cand;
          old_unit->merge_bits = old_unit->merge_bits_cand;
        }

        if (cost_merge < cost_nomerge) {
          // We found a better merge candidate that will be merged. Update best
          // filters.
          // Keep track of bank_ref_for_class as we will assign rui_merge_best
          // to token_wienerns_info_cand which in turn will be used to calculate
          // bits in set_cand_merge_sse_and_bits().
          rui_merge_cand.wienerns_info.bank_ref_for_class[c_id] = bank_ref_cand;
          copy_nsfilter_taps_for_class(&rui_merge_best.wienerns_info,
                                       &rui_merge_cand.wienerns_info, c_id);
        }
      }
      if (num_classes > 1 &&
          (begin_idx[c_id] != begin_idx_cand || cost_merge >= cost_nomerge)) {
        // We will not be merging this trial even if it is the best cand. Reset
        // rsc buffers to the best solution so far. Re-establish dst.
        rui_merge_best.wiener_class_id_restrict = c_id;
        reset_unit_stack_dst_buffers(rsc, NULL, &rsc->tile_rect,
                                     &rui_merge_best);
      }
      aom_vector_clear(current_unit_indices);
    }
    // Trial end

    RstUnitSnapshot *last_unit = aom_vector_back(current_unit_stack);
    RestUnitSearchInfo *last_rusi = &rsc->rusi[last_unit->rest_unit_idx];
    (void)last_rusi;
    if (cost_merge < cost_nomerge && begin_idx[c_id] != -1) {
      ++merged_class_count;
      const WienerNonsepInfo *token_wienerns_info =
          av1_constref_from_wienerns_bank(&rsc->wienerns_bank, bank_ref[c_id],
                                          c_id);
      // Update data within the stack.
      bool has_begun = false;
      VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
        RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
        RestUnitSearchInfo *old_rusi = &rsc->rusi[old_unit->rest_unit_idx];
        if (old_unit->rest_unit_idx == begin_idx[c_id]) has_begun = true;
        if (!has_begun) continue;
        if (old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] ==
                RESTORE_WIENER_NONSEP &&
            old_unit->rest_unit_idx != last_idx &&
            !check_wienerns_eq(&old_rusi->wienerns_info, token_wienerns_info,
                               nsfilter_params->ncoeffs, c_id))
          continue;

        if (old_unit->rest_unit_idx != begin_idx[c_id]) {
          const int is_equal = check_wienerns_bank_eq(
              &old_unit->ref_wienerns_bank, token_wienerns_info,
              nsfilter_params->ncoeffs, c_id, equal_ref_for_class);
          (void)is_equal;
          assert(is_equal >= 0);  // Must exist in bank
          // Update bank.
          av1_upd_to_wienerns_bank(&old_unit->ref_wienerns_bank,
                                   equal_ref_for_class[c_id],
                                   &rui_merge_best.wienerns_info, c_id);
          // Copy filter taps.
          copy_nsfilter_taps_for_class(&old_rusi->wienerns_info,
                                       &rui_merge_best.wienerns_info, c_id);
          // Keep track of bank_ref as copy_nsfilter_taps_for_class updates it.
          old_rusi->wienerns_info.bank_ref_for_class[c_id] =
              equal_ref_for_class[c_id];
        } else {
          // Merge leader. Copy filter taps.
          copy_nsfilter_taps_for_class(&old_rusi->wienerns_info,
                                       &rui_merge_best.wienerns_info, c_id);
          // Keep track of bank_ref as copy_nsfilter_taps_for_class updates it.
          count_wienerns_bits_set(
              is_uv, &x->mode_costs, &old_rusi->wienerns_info,
              &old_unit->ref_wienerns_bank, nsfilter_params, c_id);
        }
        old_rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] = RESTORE_WIENER_NONSEP;
        old_rusi->sse[RESTORE_WIENER_NONSEP] = old_unit->merge_sse;
        rsc->sse -= old_unit->current_sse;
        rsc->sse += old_unit->merge_sse;
        rsc->bits -= old_unit->current_bits;
        rsc->bits += old_unit->merge_bits;
        old_unit->current_sse = old_unit->merge_sse;
        old_unit->current_bits = old_unit->merge_bits;
      }
      // Above we updated the entire stack. Here we update rsc->wienerns_bank.
      const int is_equal = check_wienerns_bank_eq(
          &last_unit->ref_wienerns_bank, &rui_merge_best.wienerns_info,
          nsfilter_params->ncoeffs, c_id, equal_ref_for_class);
      (void)is_equal;
      assert(is_equal >= 0);  // Must exist in bank
      assert(rui_merge_best.wienerns_info.bank_ref_for_class[c_id] ==
             equal_ref_for_class[c_id]);
      av1_upd_to_wienerns_bank(&rsc->wienerns_bank, equal_ref_for_class[c_id],
                               &rui_merge_best.wienerns_info, c_id);
    } else {
      assert(check_wienerns_eq(&last_rusi->wienerns_info,
                               &rui_merge_best.wienerns_info,
                               nsfilter_params->ncoeffs, c_id));
      // Copy current unit from the top of the stack.
      // memset(&unit_snapshot, 0, sizeof(unit_snapshot));
      // unit_snapshot = *(RstUnitSnapshot
      // *)aom_vector_back(current_unit_stack); RESTORE_WIENER_NONSEP units
      // become start of new stack, and RESTORE_NONE units are discarded.
      if (rtype == RESTORE_WIENER_NONSEP) {
        // We may be merging some c_ids but not this one.
        av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                                 c_id);
        // aom_vector_clear(current_unit_stack);
        // aom_vector_push_back(current_unit_stack, &unit_snapshot);
      }
    }
  }
  if (merged_class_count == 0 && rtype != RESTORE_WIENER_NONSEP) {
    aom_vector_pop_back(current_unit_stack);
  }
  if (rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] == RESTORE_WIENER_NONSEP)
    rsc->num_wiener_nonsep++;
}

static int get_switchable_restore_cost(const AV1_COMMON *const cm,
                                       const MACROBLOCK *const x, int plane,
                                       int rest_type) {
  (void)cm;
  (void)plane;
  int cost = 0;
  for (int re = 0; re <= cm->features.lr_last_switchable_ndx[plane]; re++) {
    if (cm->features.lr_tools_disable_mask[plane] & (1 << re)) continue;
    const int found = (re == rest_type);
    cost += x->mode_costs.switchable_flex_restore_cost[re][plane][found];
    if (found) break;
  }
  return cost;
}

static int64_t count_switchable_bits(int rest_type, RestSearchCtxt *rsc,
                                     RestUnitSearchInfo *rusi) {
  const MACROBLOCK *const x = rsc->x;
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
#if CONFIG_COMBINE_PC_NS_WIENER
  // Ensure search_switchable does not punish frame level filters.
  const WienerNonsepInfoBank *bank_to_use =
      rsc->adjust_switchable_for_frame_filters &&
              is_frame_filters_enabled(rsc->plane)
          ? &rsc->frame_filter_bank
          : &rsc->wienerns_bank;
#else
  const WienerNonsepInfoBank *bank_to_use = &rsc->wienerns_bank;
#endif  // CONFIG_COMBINE_PC_NS_WIENER
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
      coeff_bits = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                         &rusi->wiener_info, &rsc->wiener_bank);
      break;
    case RESTORE_SGRPROJ:
      coeff_bits = count_sgrproj_bits_set(&x->mode_costs, &rusi->sgrproj_info,
                                          &rsc->sgrproj_bank);
      break;
    case RESTORE_PC_WIENER:
      // No side-information for now.
      coeff_bits = 0;
      break;
    case RESTORE_WIENER_NONSEP:
      coeff_bits = count_wienerns_bits_set(
          rsc->plane, &x->mode_costs, &rusi->wienerns_info, bank_to_use,
          nsfilter_params, ALL_WIENERNS_CLASSES);
      break;
    default: assert(0); break;
  }
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsc->adjust_switchable_for_frame_filters &&
      is_frame_filters_enabled(rsc->plane) &&
      rest_type == RESTORE_WIENER_NONSEP &&
      rsc->frame_filter_bank.bank_size_for_class[0] == 1) {
    coeff_bits = 0;
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
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
#if CONFIG_COMBINE_PC_NS_WIENER
  if (!rsc->adjust_switchable_for_frame_filters && rsc->frame_filters_on &&
      is_frame_filters_enabled(rsc->plane) &&
      rsc->wienerns_bank.bank_size_for_class[0] == 1) {
    assert(rsc->cm->frame_filter_dictionary != NULL);
    assert(rsc->cm->translated_pcwiener_filters != NULL);
    assert(rsc->cm->translation_done);
    int dict_stride = rsc->cm->frame_filter_dictionary_stride;
    int16_t *frame_filter_dictionary = rsc->cm->frame_filter_dictionary;
    *rsc->cm->num_ref_filters = set_frame_filter_dictionary(
        rsc->plane, rsc->cm, rsc->frame_filter_bank.filter->num_classes,
        frame_filter_dictionary, dict_stride);
    rsc->frame_filter_bank.filter->num_ref_filters = *rsc->cm->num_ref_filters;

    // Initialize bank for first call.
    const int nopcw =
        disable_pcwiener_filters_in_framefilters(&rsc->cm->seq_params);
    const int base_qindex = rsc->cm->quant_params.base_qindex;
    fill_first_slot_of_bank_with_filter_match(
        rsc->plane, &rsc->wienerns_bank, rsc->frame_filter_bank.filter,
        rsc->frame_filter_bank.filter->match_indices, base_qindex,
        ALL_WIENERNS_CLASSES, frame_filter_dictionary, dict_stride, nopcw);
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER

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
    if (rsc->cm->features.lr_tools_disable_mask[rsc->plane] & (1 << r))
      continue;
    if (rsc->plane != AOM_PLANE_Y && r == RESTORE_PC_WIENER) continue;

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
    const int equal_ref =
        check_wiener_bank_eq(&rsc->wiener_bank, &rusi->wiener_info);
    if (equal_ref == -1 || rsc->wiener_bank.bank_size == 0)
      av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
  } else if (best_rtype == RESTORE_SGRPROJ) {
    const int equal_ref =
        check_sgrproj_bank_eq(&rsc->sgrproj_bank, &rusi->sgrproj_info);
    if (equal_ref == -1 || rsc->sgrproj_bank.bank_size == 0)
      av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
  } else if (best_rtype == RESTORE_PC_WIENER) {
    // No side-information for now.
  } else if (best_rtype == RESTORE_WIENER_NONSEP) {
    rsc->num_wiener_nonsep++;
    const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
        rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
    int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
    for (int c_id = 0; c_id < rusi->wienerns_info.num_classes; ++c_id) {
      const int is_equal = check_wienerns_bank_eq(
          &rsc->wienerns_bank, &rusi->wienerns_info, nsfilter_params->ncoeffs,
          c_id, equal_ref_for_class);
      if (is_equal == -1) {
        av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                                 c_id);
      }
    }
  }
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsc->frame_filters_on) {
    for (int c_id = 0; c_id < rsc->num_filter_classes; ++c_id)
      assert(rsc->wienerns_bank.bank_size_for_class[c_id] <= 2);
  }
#endif
}

static void adjust_frame_rtype(RestorationInfo *rsi, int plane_ntiles,
                               RestSearchCtxt *rsc, const ToolCfg *tool_cfg) {
  (void)rsc;
  (void)tool_cfg;
  rsi->sw_lr_tools_disable_mask = 0;
  uint8_t sw_lr_tools_disable_mask = 0;
  (void)sw_lr_tools_disable_mask;
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
      assert((rsc->cm->features.lr_tools_disable_mask[rsc->plane] & (1 << j)) ==
             0);
    } else {
      sw_lr_tools_disable_mask |= (1 << j);
    }
  }
  rsi->frame_restoration_type = ntools < 2 ? rused : RESTORE_SWITCHABLE;
#if !CONFIG_COMBINE_PC_NS_WIENER
  rsi->num_filter_classes = rsc->num_filter_classes;
  if (rsi->frame_restoration_type == RESTORE_SWITCHABLE &&
      rsc->cm->features.lr_tools_count[rsc->plane] > 2) {
    rsi->sw_lr_tools_disable_mask = sw_lr_tools_disable_mask;
  }
#endif  // !CONFIG_COMBINE_PC_NS_WIENER
  return;
}

static AOM_INLINE void copy_unit_info(RestorationType frame_rtype,
                                      const RestUnitSearchInfo *rusi,
                                      RestorationUnitInfo *rui,
                                      RestSearchCtxt *rsc) {
  const ModeCosts *mode_costs = &rsc->x->mode_costs;
  assert(frame_rtype > 0);
  rui->restoration_type = frame_rtype == RESTORE_NONE
                              ? RESTORE_NONE
                              : rusi->best_rtype[frame_rtype - 1];
  if (rui->restoration_type == RESTORE_WIENER) {
    rui->wiener_info = rusi->wiener_info;
    const int wiener_win =
        (rsc->plane == AOM_PLANE_Y) ? WIENER_WIN : WIENER_WIN_CHROMA;
    const int equal_ref =
        check_wiener_bank_eq(&rsc->wiener_bank, &rui->wiener_info);
    if (equal_ref >= 0) {
      rui->wiener_info.bank_ref = equal_ref;
      if (rsc->wiener_bank.bank_size == 0)
        av1_add_to_wiener_bank(&rsc->wiener_bank, &rui->wiener_info);
    } else {
      count_wiener_bits_set(wiener_win, mode_costs, &rui->wiener_info,
                            &rsc->wiener_bank);
      av1_add_to_wiener_bank(&rsc->wiener_bank, &rui->wiener_info);
    }
  } else if (rui->restoration_type == RESTORE_SGRPROJ) {
    rui->sgrproj_info = rusi->sgrproj_info;
    const int equal_ref =
        check_sgrproj_bank_eq(&rsc->sgrproj_bank, &rui->sgrproj_info);
    if (equal_ref >= 0) {
      rui->sgrproj_info.bank_ref = equal_ref;
      if (rsc->sgrproj_bank.bank_size == 0)
        av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rui->sgrproj_info);
    } else {
      count_sgrproj_bits_set(mode_costs, &rui->sgrproj_info,
                             &rsc->sgrproj_bank);
      av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rui->sgrproj_info);
    }
  } else if (rui->restoration_type == RESTORE_PC_WIENER) {
    // No side-information for now.
  } else if (rui->restoration_type == RESTORE_WIENER_NONSEP) {
    rui->wienerns_info = rusi->wienerns_info;
    const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
        rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
    int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
    count_wienerns_bits_set(rsc->plane, mode_costs, &rui->wienerns_info,
                            &rsc->wienerns_bank, nsfilter_params,
                            ALL_WIENERNS_CLASSES);
    for (int c_id = 0; c_id < rui->wienerns_info.num_classes; ++c_id) {
      const int is_equal = check_wienerns_bank_eq(
          &rsc->wienerns_bank, &rui->wienerns_info, nsfilter_params->ncoeffs,
          c_id, equal_ref_for_class);
      if (is_equal == -1) {
        av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rui->wienerns_info,
                                 c_id);
      }
    }
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
  rsc_on_tile(rsc, *processed, tile_row, tile_col);
  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += rsc->cm->mib_size) {
    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += rsc->cm->mib_size) {
      int rrow0, rrow1, rcol0, rcol1;
      if (av1_loop_restoration_corners_in_sb(rsc->cm, rsc->plane, mi_row,
                                             mi_col, rsc->cm->sb_size, &rcol0,
                                             &rcol1, &rrow0, &rrow1)) {
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
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = {
    NULL, NULL, NULL, NULL, gather_stats_wienerns, NULL
  };
  if (rtype == RESTORE_WIENER_NONSEP) aom_vector_clear(rsc->wienerns_stats);

  if (funs[rtype]) process_by_rutile(rsc, funs[rtype]);
}

static double search_rest_type(RestSearchCtxt *rsc, RestorationType rtype) {
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = {
    search_norestore_visitor, search_wiener_visitor,   search_sgrproj_visitor,
    search_pc_wiener_visitor, search_wienerns_visitor, search_switchable_visitor
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
#if CONFIG_TEMP_LR
  assert(rsi->temporal_pred_flag == rsc->temporal_pred_flag);
#endif  // CONFIG_TEMP_LR
}

static void finalize_frame_and_unit_info(RestorationType frame_rtype,
                                         RestorationInfo *rsi,
                                         RestSearchCtxt *rsc) {
  rsi->frame_restoration_type = frame_rtype;
#if CONFIG_COMBINE_PC_NS_WIENER
  rsi->frame_filters_on = rsc->frame_filters_on;
  rsi->frame_filters_initialized = 0;
  rsi->num_filter_classes = rsc->num_filter_classes;
  rsi->frame_filters = rsc->frame_filter_bank.filter[0];
#if CONFIG_TEMP_LR
  rsi->frame_filters = rsc->frame_filters;
  if (is_frame_filters_enabled(rsc->plane)) {
    rsi->temporal_pred_flag = rsc->temporal_pred_flag;
    rsi->rst_ref_pic_idx = rsc->rst_ref_pic_idx;
  }
#endif  // CONFIG_TEMP_LR
#endif
  if (frame_rtype != RESTORE_NONE) {
    process_by_rutile(rsc, copy_unit_info_visitor);
  }
}

#if CONFIG_COMBINE_PC_NS_WIENER

const uint8_t *get_class_converter(const RestSearchCtxt *rsc,
                                   int num_stats_classes,
                                   int num_target_classes) {
  int qindex_offset = 0;
  if (rsc->plane != AOM_PLANE_Y)
    qindex_offset = rsc->plane == AOM_PLANE_U
                        ? rsc->cm->quant_params.u_ac_delta_q
                        : rsc->cm->quant_params.v_ac_delta_q;
  else
    qindex_offset = 0;
  const int set_index =
      get_filter_set_index(rsc->cm->quant_params.base_qindex + qindex_offset);
  return get_converter(set_index, num_stats_classes, num_target_classes);
}

// Reduces the class granularity of the stats to target_classes. Stats are
// initially calculated for WIENERNS_MAX_CLASSES. Frame-level filters are then
// derived for num_classes <= WIENERNS_MAX_CLASSES. Once stats are used at a
// particular num_classes they are used to calculate the stats for the next
// target_classes < num_classes (without recalculating stats from data). This is
// possible since the classification design resuls in class labels over a tree
// having WIENERNS_MAX_CLASSES leaves reduced to the single-class root.
static void collapse_stats_to_target_classes(int target_classes,
                                             const uint8_t *class_converter,
                                             RstUnitStats *unit_stats) {
  assert(unit_stats->num_stats_classes >= target_classes);
  if (unit_stats->num_stats_classes == target_classes) return;

  // TODO: Reduce buffer/copy size.
  RstUnitStats collapsed_unit_stats;
  memset(&collapsed_unit_stats, 0, sizeof(collapsed_unit_stats));
  collapsed_unit_stats.ru_idx = unit_stats->ru_idx;
  collapsed_unit_stats.ru_idx_in_tile = unit_stats->ru_idx_in_tile;
  collapsed_unit_stats.plane = unit_stats->plane;
  collapsed_unit_stats.real_sse = unit_stats->real_sse;
  collapsed_unit_stats.limits = unit_stats->limits;
  collapsed_unit_stats.weight = unit_stats->weight;
  collapsed_unit_stats.num_stats_classes = target_classes;

  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;
  for (int c_id = 0; c_id < unit_stats->num_stats_classes; ++c_id) {
    const int tc_id = class_converter[c_id];
    for (int i = 0; i < stride_A; ++i) {
      collapsed_unit_stats.A[tc_id * stride_A + i] +=
          unit_stats->A[c_id * stride_A + i];
    }
    for (int i = 0; i < stride_b; ++i) {
      collapsed_unit_stats.b[tc_id * stride_b + i] +=
          unit_stats->b[c_id * stride_b + i];
    }
    collapsed_unit_stats.num_pixels_in_class[tc_id] +=
        unit_stats->num_pixels_in_class[c_id];
  }
  *unit_stats = collapsed_unit_stats;
}

// Initial set of stats are determined using num_stats_classes classes. This
// routine collapses the initial set of stats to num_target_classes for
// downstream use.
static void collapse_all_stats(RestSearchCtxt *rsc, int num_stats_classes,
                               int num_target_classes) {
  const uint8_t *class_converter =
      get_class_converter(rsc, num_stats_classes, num_target_classes);
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    RstUnitStats *unit_stats_ptr = (RstUnitStats *)unit_stats.pointer;
    assert(unit_stats_ptr->num_stats_classes == num_stats_classes);
    collapse_stats_to_target_classes(num_target_classes, class_converter,
                                     unit_stats_ptr);
  }
}

// Returns the number of classes that are not active on a given picture. While
// all class labels tend to be active at high rates at lower rates some class
// labels are no longer active as the picture loses detail. Useful in debugging.
static int count_classes_with_no_pixels(const RestSearchCtxt *rsc) {
  int pixel_count[WIENERNS_MAX_CLASSES] = { 0 };
  int num_classes = -1;
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    RstUnitStats *unit_stats_ptr = (RstUnitStats *)unit_stats.pointer;
    num_classes =
        num_classes == -1 ? unit_stats_ptr->num_stats_classes : num_classes;
    assert(unit_stats_ptr->num_stats_classes == num_classes);
    for (int c_id = 0; c_id < unit_stats_ptr->num_stats_classes; ++c_id) {
      pixel_count[c_id] += unit_stats_ptr->num_pixels_in_class[c_id];
    }
  }
  int unoccupied = num_classes;
  for (int c_id = 0; c_id < num_classes; ++c_id)
    if (pixel_count[c_id]) --unoccupied;
  return unoccupied;
}

// Calculates the weighted sum of all frame-level statistics. Useful in deriving
// frame-level Wiener filters.
static void weighted_sum_all_stats(const RestSearchCtxt *rsc,
                                   RstUnitStats *sum_stats, int num_feat) {
  memset(sum_stats, 0, sizeof(*sum_stats));

  // Get a sample to fill the basic fields of sum_stats;
  const RstUnitStats *sample_stat =
      aom_vector_begin(rsc->wienerns_stats).pointer;
  sum_stats->ru_idx = sample_stat->ru_idx;
  sum_stats->plane = sample_stat->plane;
  sum_stats->num_stats_classes = sample_stat->num_stats_classes;

  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    const RstUnitStats *unit_stat = (const RstUnitStats *)unit_stats.pointer;
    const double weight = unit_stat->weight;

    // Begin: not really needed.
    sum_stats->real_sse += (int64_t)(weight * unit_stat->real_sse);
    sum_stats->weight += weight;
    // End: not really needed.

    for (int c_id = 0; c_id < sum_stats->num_stats_classes; ++c_id) {
      for (int i = 0; i < num_feat * num_feat; ++i) {
        sum_stats->A[c_id * stride_A + i] +=
            weight * unit_stat->A[c_id * stride_A + i];
      }
      for (int i = 0; i < num_feat; ++i) {
        sum_stats->b[c_id * stride_b + i] +=
            weight * unit_stat->b[c_id * stride_b + i];
      }
      sum_stats->num_pixels_in_class[c_id] +=
          (int)(weight * unit_stat->num_pixels_in_class[c_id]);
    }
  }
}

#if CONFIG_COMBINE_PC_NS_WIENER_ADD
int count_match_indices_bits(int plane, int num_classes, int num_ref_frames,
                             const int *match_indices, int nopcw) {
  assert(NUM_MATCH_GROUPS == 3);
  int group_counts[NUM_MATCH_GROUPS];
  set_group_counts(plane, num_classes, num_ref_frames, group_counts, nopcw);
  int total_bits = 0;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    int only;
    const int pred_group =
        predict_group(c_id, match_indices, group_counts, &only);
    const int group = index_to_group(match_indices[c_id], group_counts);
    if (!only) ++total_bits;
    if (group != pred_group) {
      const int other_group = 3 - (group + pred_group);
      if (group_counts[other_group]) ++total_bits;
    }
    const int ref =
        predict_within_group(group, c_id, match_indices, group_counts);
    const int base = get_group_base(group, group_counts);
    const int n = group == 0 ? c_id + 1 : group_counts[group];
    if (n > 1) {
      total_bits += aom_count_primitive_refsubexpfin(
          n, 4, ref - base, match_indices[c_id] - base);
    }
  }
  return total_bits;
}
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD

static double calculate_frame_filters_cost(const RestSearchCtxt *rsc,
                                           const WienerNonsepInfoBank *bank,
                                           WienerNonsepInfo *filter,
                                           int64_t *filter_bits) {
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  int64_t bits =
      count_wienerns_bits_set(rsc->plane, &rsc->x->mode_costs, filter, bank,
                              nsfilter_params, ALL_WIENERNS_CLASSES);
  assert(filter->num_ref_filters == *rsc->cm->num_ref_filters);
  const int nopcw =
      disable_pcwiener_filters_in_framefilters(&rsc->cm->seq_params);

#if CONFIG_COMBINE_PC_NS_WIENER_ADD
  bits += count_match_indices_bits(rsc->plane, filter->num_classes,
                                   filter->num_ref_filters,
                                   filter->match_indices, nopcw) *
          (1 << AV1_PROB_COST_SHIFT);
#else
  bits += count_match_indices_bits(filter->num_classes, nopcw) *
          (1 << AV1_PROB_COST_SHIFT);
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD

  *filter_bits = bits;
  double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, 0,
                                               rsc->cm->seq_params.bit_depth);
  return cost;
}

static int cost_compar(const void *left, const void *right) {
  return *((double *)left) <= *((double *)right) ? -1 : 1;
}

typedef struct RdResults {
  int utilization;
  int64_t total_distortion;
  int64_t total_bits;
  int64_t total_stat_distortion;
  int64_t total_sse;
  double total_cost;
} RdResults;

// Distortion is given by, d = sse - 2 * b^T f + f^T A f. For an optimized
// filter the distortion improvement del_d = - 2 * b^T f + f^T A f, should be
// negative. This routine calculates del_d giving the caller the opportunity to
// (i) calculate d, (ii) detect a suboptimal filter and set it to zero.
// (Suboptimal filters can result during discretization of filter-taps.)
// Returned result is scaled up by tap_qstep * tap_qstep.
static double calculate_distortion_improvement(const double *A, int stride,
                                               const double *b, int num_feat,
                                               const int16_t *filter_taps,
                                               int tap_qstep) {
  double del_d = 0;
  for (int i = 0; i < num_feat; ++i) {
    // f^T A f.
    double tmp_sum = 0;
    for (int j = 0; j < num_feat; ++j) {
      tmp_sum += A[i * stride + j] * filter_taps[j];
    }
    // f^T A f - 2 * b^T f.
    del_d += (tmp_sum - 2 * b[i] * tap_qstep) * filter_taps[i];
  }
  return del_d;
}

// Distortion is given by, d = sse - 2 * b^T f + f^T A f. Returned result is
// scaled up by tap_qstep * tap_qstep.
static double get_scaled_filter_distortion(const RstUnitStats *stats,
                                           WienerNonsepInfo *filter,
                                           int num_feat, int tap_qstep) {
  assert(filter->num_classes == stats->num_stats_classes);
  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;

  double distortion = stats->real_sse * tap_qstep * tap_qstep;
  for (int c_id = 0; c_id < stats->num_stats_classes; ++c_id) {
    const int16_t *filter_taps = const_nsfilter_taps(filter, c_id);

    // - 2 * b^T f + f^T A f
    double del_d_for_class = calculate_distortion_improvement(
        stats->A + c_id * stride_A, num_feat, stats->b + c_id * stride_b,
        num_feat, filter_taps, tap_qstep);

    distortion += del_d_for_class;
  }
  return distortion;
}

// Solves frame-filters with the help of sum_stats using least-squares and basic
// integerization. Solution is returned in WienerNonsepInfo *filter. If no
// solution is found for a particular class, correspondinf filter is set to
// zeros.
static void solve_filters_from_stats_wienerns(const RestSearchCtxt *rsc,
                                              const RstUnitStats *sum_stats,
                                              WienerNonsepInfo *filter) {
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  const int num_feat = nsfilter_params->ncoeffs;
  const int stride_A = WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX;
  const int stride_b = WIENERNS_TAPS_MAX;
  const int num_target_classes = filter->num_classes;
  int linsolve_successful = 0;
  for (int c_id = 0; c_id < num_target_classes; ++c_id) {
    linsolve_successful = compute_wienerns_filter_select_master_basic(
        rsc, filter, c_id, num_feat, nsfilter_params->nsubsets - 1,
        sum_stats->A + c_id * stride_A, num_feat,
        sum_stats->b + c_id * stride_b, rsc->wienerns_tmpbuf, nsfilter_params,
        0);
    /*
    linsolve_successful = compute_wienerns_filter_select(
        num_feat, nsfilter_params->subset_config[nsfilter_params->nsubsets - 1],
        sum_stats->A + c_id * stride_A, num_feat,
        sum_stats->b + c_id * stride_b, rsc->wienerns_tmpbuf,
        nsfilter_taps(filter, c_id), nsfilter_params);
        */
    /*
    linsolve_successful = compute_wienerns_filter(
        num_feat, sum_stats->A + c_id * stride_A, num_feat,
        sum_stats->b + c_id * stride_b, rsc->wienerns_tmpbuf,
        nsfilter_taps(filter, c_id), nsfilter_params);
        */
    if (!linsolve_successful) {
      memset(nsfilter_taps(filter, c_id), 0,
             num_feat * sizeof(*filter->allfiltertaps));
    }
  }
}

#define RU_ON_WEIGHT 1
#define RU_OFF_WEIGHT 1e-10

// Calculates the cost of using frame-level filters on a picture by
//   (i) determining per RU on-off mode based on R-D,
//  (ii) calculating resulting per RU cost.
// Results are returned in the RdResults struct. RUs with on-mode are assigned
// the weight RU_ON_WEIGHT, off-mode RU_OFF_WEIGHT. Useful in optimizing
// frame-level filters in conjunction with on-off mode-decisions.
static RdResults update_cost_and_weights_wienerns(RestSearchCtxt *rsc,
                                                  RestorationUnitInfo *rui,
                                                  double *work_cost_array,
                                                  int fill_stat_distortion) {
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  const int num_feat = nsfilter_params->ncoeffs;
  const int tap_shift = nsfilter_params->nsfilter_config.prec_bits;
  const int tap_qstep = 1 << tap_shift;
  const int use_rate = 1;

  const int64_t bits_none =
      use_rate ? rsc->x->mode_costs.wienerns_restore_cost[0] : 0;
  const int64_t bits_wienerns =
      use_rate ? rsc->x->mode_costs.wienerns_restore_cost[1] : 0;

  RdResults results = { 0 };
  int stat_slot = -1;
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    stat_slot++;
    RstUnitStats *unit_stats_ptr = (RstUnitStats *)(unit_stats.pointer);
    int64_t distortion = calc_finer_tile_search_error(
        rsc, &unit_stats_ptr->limits, &rsc->tile_rect, rui);
    const int64_t distortion_none = unit_stats_ptr->real_sse;

    const double cost_wienerns = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, bits_wienerns >> 4, distortion,
        rsc->cm->seq_params.bit_depth);
    const double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, bits_none >> 4, distortion_none,
        rsc->cm->seq_params.bit_depth);

    const double cost_diff = cost_wienerns - cost_none;
    work_cost_array[stat_slot] = cost_diff;
    const int condn = cost_wienerns < cost_none;

    if (condn) {
      results.total_distortion += distortion;
      results.total_bits += bits_wienerns;
      results.utilization++;
      results.total_cost += cost_wienerns;
      unit_stats_ptr->weight = RU_ON_WEIGHT;

      if (fill_stat_distortion) {
        const double scaled_distortion = get_scaled_filter_distortion(
            unit_stats_ptr, &rui->wienerns_info, num_feat, tap_qstep);
        results.total_stat_distortion +=
            ROUND_POWER_OF_TWO((int64_t)scaled_distortion, 2 * tap_shift);
      }
    } else {
      results.total_distortion += distortion_none;
      results.total_bits += bits_none;
      results.total_cost += cost_none;
      unit_stats_ptr->weight = RU_OFF_WEIGHT;

      if (fill_stat_distortion) {
        results.total_stat_distortion += distortion_none;
      }
    }
    results.total_sse += distortion_none;
  }
  return results;
}
#if CONFIG_TEMP_LR
// Returns the cost of using frame-level filters directly obtained from
// reference frames.
static double obtain_temp_pred_frame_filters_cost(RestSearchCtxt *rsc,
                                                  WienerNonsepInfo *filter) {
  assert(rsc->temporal_pred_flag);

  const int work_array_dim = (int)rsc->wienerns_stats->size;
  double *work_cost_array =
      (double *)(aom_malloc((work_array_dim + 1) * sizeof(*work_cost_array)));
  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
  rui.restoration_type = RESTORE_WIENER_NONSEP;

  rui.wienerns_info = *filter;
  RdResults rd_results =
      update_cost_and_weights_wienerns(rsc, &rui, work_cost_array, 0);

  double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      rsc->x->rdmult, rd_results.total_bits >> 4, rd_results.total_distortion,
      rsc->cm->seq_params.bit_depth);
  // cost += frame level infor cos, to be added;
  aom_free(work_cost_array);
  return cost;
}
#endif  // CONFIG_TEMP_LR

// Optimizes frame-level filters for a given num_target_classes. Optimization
// progresses by weighting the stats of all RUs with RU_ON_WEIGHT and including
// all RUs when solving for the filters. Then fewer and fewer RUs are included
// to solve for filters more specialized on the included RUs. Once the reduction
// process is completed the overall best is determined and the relevant cost
// returned.
static double optimize_frame_filters_for_target_classes(
    RestSearchCtxt *rsc, WienerNonsepInfo *filter, int *best_utilization,
    double *best_cost_array) {
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
  const int num_feat = nsfilter_params->ncoeffs;
  const int num_target_classes = filter->num_classes;
  WienerNonsepInfoBank bank = { 0 };
  WienerNonsepInfo tmp_filter = { 0 };
  tmp_filter.num_classes = num_target_classes;

  double fraction_rus_to_include[][2] = { { .9, .0 }, { .8, .0 }, { .7, .0 },
                                          { .6, .0 }, { .5, .0 }, { .4, .0 },
                                          { .3, .0 }, { .2, .0 }, { .1, 0 } };
  const int num_ru_perc_to_try =
      sizeof(fraction_rus_to_include) / sizeof(*fraction_rus_to_include);
  const int solve_iterations = 1;
  double best_cost = DBL_MAX;

  RstUnitStats sum_stats;
  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
  rui.restoration_type = RESTORE_WIENER_NONSEP;

  const int work_array_dim = (int)rsc->wienerns_stats->size;
  double *work_cost_array =
      (double *)(aom_malloc((work_array_dim + 1) * sizeof(*work_cost_array)));

  // First run is with the initial stats. Then we try to refine
  // num_ru_perc_to_try times. Then a final round to better optimize the best
  // filter.
  int cnt = 0;
  while (cnt < num_ru_perc_to_try + 2) {
    ++cnt;
    RdResults rd_results = { 0 };
    for (int n = 0; n < solve_iterations; ++n) {
      weighted_sum_all_stats(rsc, &sum_stats, num_feat);
      assert(sum_stats.num_stats_classes == num_target_classes);
      solve_filters_from_stats_wienerns(rsc, &sum_stats, &tmp_filter);
      rui.wienerns_info = tmp_filter;
      RdResults iter_rd_results =
          update_cost_and_weights_wienerns(rsc, &rui, work_cost_array, 1);
      if (iter_rd_results.total_distortion == rd_results.total_distortion &&
          iter_rd_results.total_bits == rd_results.total_bits)
        break;
      rd_results = iter_rd_results;
    }

    double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rsc->x->rdmult, rd_results.total_bits >> 4, rd_results.total_distortion,
        rsc->cm->seq_params.bit_depth);
    initialize_bank_with_best_frame_filter_match(rsc, &tmp_filter, &bank, 1);
    int64_t filter_bits = 0;
    cost += calculate_frame_filters_cost(rsc, &bank, &tmp_filter, &filter_bits);

    if (cost < best_cost) {
      best_cost = cost;
      *best_utilization = rd_results.utilization;
      *filter = tmp_filter;
      for (int i = 0; i < work_array_dim; ++i)
        best_cost_array[i] = work_cost_array[i];
    } else {
      tmp_filter = *filter;
    }
    if (cnt <= num_ru_perc_to_try) {
      // Sorting will change the order in the arrays. Preserve data and ordering
      // in best_cost_array.
      for (int i = 0; i < work_array_dim; ++i)
        work_cost_array[i] = best_cost_array[i];
      qsort(work_cost_array, work_array_dim, sizeof(*work_cost_array),
            cost_compar);

      const int pnt =
          (int)(fraction_rus_to_include[cnt - 1][0] * work_array_dim + .5);
      const double max_cost_allowed = work_cost_array[pnt];

      int stat_slot = -1;
      VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
        stat_slot++;
        RstUnitStats *unit_stats_ptr = (RstUnitStats *)(unit_stats.pointer);
        if (best_cost_array[stat_slot] < max_cost_allowed) {
          unit_stats_ptr->weight = RU_ON_WEIGHT;
        } else {
          unit_stats_ptr->weight = RU_OFF_WEIGHT;
        }
      }
    } else {
      // Recover the weights for the best filter.
      int stat_slot = -1;
      VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
        stat_slot++;
        RstUnitStats *unit_stats_ptr = (RstUnitStats *)(unit_stats.pointer);
        unit_stats_ptr->weight =
            best_cost_array[stat_slot] < 0 ? RU_ON_WEIGHT : RU_OFF_WEIGHT;
      }
    }
  }
  aom_free(work_cost_array);

  if (*best_utilization == 0) {
    for (int c_id = 0; c_id < num_target_classes; ++c_id) {
      memset(nsfilter_taps(filter, c_id), 0,
             WIENERNS_TAPS_MAX * sizeof(*filter->allfiltertaps));
    }
  }
  return best_cost;
}

static AOM_INLINE void initialize_stat_weights(RestSearchCtxt *rsc) {
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    RstUnitStats *unit_stats_ptr = (RstUnitStats *)(unit_stats.pointer);
    unit_stats_ptr->weight = RU_ON_WEIGHT;
  }
}

#define USE_FINER_TILE 1

#if USE_FINER_TILE

// Uses finer_tile_search_wienerns() to fine-tune the frame-level filters over
// the RUs using them. Without this routine the frame-level filters are
// optimized with aggregate stats which do not incorporate rounding. This
// routine establishes fine-grain optimization that also accounts for rounding
// that follows the filtering process, i.e., the actual final distortion after
// rounding is used to guide the optimization.
static double optimize_frame_filters_with_rounding(
    RestSearchCtxt *rsc, WienerNonsepInfo *best_filter, double *cost_array) {
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);

  Vector *current_unit_stack = rsc->unit_stack;
  Vector *current_unit_indices = rsc->unit_indices;
  aom_vector_clear(current_unit_indices);
  aom_vector_clear(current_unit_stack);

  WienerNonsepInfoBank tmp_bank = { 0 };
  initialize_bank_with_best_frame_filter_match(rsc, best_filter, &tmp_bank, 1);

  int cnt = 0;
  int stat_slot = -1;
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    stat_slot++;
    if (cost_array[stat_slot] >= 0) continue;

    cnt++;
    const RstUnitStats *unit_stat = (const RstUnitStats *)unit_stats.pointer;

    RstUnitSnapshot unit_snapshot;
    memset(&unit_snapshot, 0, sizeof(unit_snapshot));
    unit_snapshot.limits = unit_stat->limits;

    unit_snapshot.rest_unit_idx = stat_slot;
    unit_snapshot.ref_wienerns_bank = tmp_bank;
    aom_vector_push_back(current_unit_stack, &unit_snapshot);
    aom_vector_push_back(current_unit_indices, &stat_slot);
  }
  if (cnt == 0) return -1;

  RestorationUnitInfo rui;
  initialize_rui_for_nonsep_search(rsc, &rui);
  rui.restoration_type = RESTORE_WIENER_NONSEP;
  rui.wienerns_info = *best_filter;
  rui.wiener_class_id_restrict = -1;
  finer_tile_search_wienerns(rsc, NULL, &rsc->tile_rect, &rui, nsfilter_params,
                             1, &tmp_bank, ALL_WIENERNS_CLASSES);
  *best_filter = rui.wienerns_info;
  RdResults rd_results =
      update_cost_and_weights_wienerns(rsc, &rui, cost_array, 0);
  initialize_bank_with_best_frame_filter_match(rsc, best_filter, &tmp_bank, 1);
  double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      rsc->x->rdmult, rd_results.total_bits >> 4, rd_results.total_distortion,
      rsc->cm->seq_params.bit_depth);
  int64_t filter_bits = 0;
  const double filter_cost =
      calculate_frame_filters_cost(rsc, &tmp_bank, best_filter, &filter_bits);
  cost += filter_cost;

  return cost;
}
#endif  // USE_FINER_TILE

// At the end of this routine all stats will be collapsed to one.
static void find_optimal_num_classes_and_frame_filters(RestSearchCtxt *rsc) {
  const int max_num_classes_allowed = max_num_classes(rsc->plane);
  const int num_classes_to_try[] = { 16, 12, 8, 6, 4, 3, 2, 1 };
  const int num_try = sizeof(num_classes_to_try) / sizeof(*num_classes_to_try);

  WienerNonsepInfoBank tmp_bank = { 0 };  // Needed for filter rate calculation.
  WienerNonsepInfo tmp_filter = { 0 };    // Set all including bank ref to 0.
  WienerNonsepInfo best_filter;

  const int work_array_dim = (int)rsc->wienerns_stats->size;
  double *best_cost_array =
      (double *)(aom_malloc((work_array_dim + 1) * sizeof(*best_cost_array)));
  double *work_cost_array =
      (double *)(aom_malloc((work_array_dim + 1) * sizeof(*work_cost_array)));
  double best_cost = DBL_MAX;
  int best_utilization = 0;
  int best_num_classes = -1;
  int num_stats_classes = rsc->num_stats_classes;
  initialize_stat_weights(rsc);
#if CONFIG_TEMP_LR
  rsc->temporal_pred_flag = 0;
#endif  // CONFIG_TEMP_LR
  for (int i = 0; i < num_try; ++i) {
    const int num_target_classes = num_classes_to_try[i];
    assert(decode_num_filter_classes(encode_num_filter_classes(
               num_target_classes)) == num_target_classes);
    if (num_target_classes > max_num_classes_allowed) continue;

    if (num_stats_classes > num_target_classes)
      collapse_all_stats(rsc, num_stats_classes, num_target_classes);
    const int unoccupied = count_classes_with_no_pixels(rsc);
    (void)unoccupied;
    num_stats_classes = num_target_classes;
    tmp_filter.num_classes = num_target_classes;
    tmp_filter.num_ref_filters = *rsc->cm->num_ref_filters;
    int utilization = 0;
    const double cost = optimize_frame_filters_for_target_classes(
        rsc, &tmp_filter, &utilization, work_cost_array);

    if (cost < best_cost) {
      best_cost = cost;
      best_utilization = utilization;
      best_num_classes = num_target_classes;
      best_filter = tmp_filter;
      for (int n = 0; n < work_array_dim; ++n) {
        best_cost_array[n] = work_cost_array[n];
      }
      //      double *tmp_array = work_cost_array;
      //      work_cost_array = best_cost_array;
      //      best_cost_array = tmp_array;
    }
  }

  assert(best_num_classes != -1);
  (void)best_utilization;

  rsc->num_filter_classes = best_num_classes;
  rsc->best_num_filter_classes = best_num_classes;

#if USE_FINER_TILE
  tmp_filter = best_filter;
  const double tmp_cost =
      optimize_frame_filters_with_rounding(rsc, &best_filter, best_cost_array);
  if (tmp_cost >= 0 && tmp_cost < best_cost) {
    best_cost = tmp_cost;
  } else {
    best_filter = tmp_filter;
  }
#endif  // USE_FINER_TILE
  assert(best_filter.num_classes == best_num_classes);
  initialize_bank_with_best_frame_filter_match(rsc, &best_filter, &tmp_bank, 1);

  int64_t filter_bits = 0;
  rsc->frame_filter_cost =
      calculate_frame_filters_cost(rsc, &tmp_bank, &best_filter, &filter_bits);

#if CONFIG_TEMP_LR
  int8_t best_ref_idx = -1;
  const int num_ref_frames =
      (frame_is_intra_only(rsc->cm) || rsc->cm->features.error_resilient_mode)
          ? 0
          : rsc->cm->ref_frames_info.num_total_refs;
  for (int ref_idx = 0; ref_idx < num_ref_frames; ref_idx++) {
    RestorationInfo rsi =
        get_ref_frame_buf(rsc->cm, ref_idx)->rst_info[rsc->plane];
    if (!rsi.frame_filters_on) {
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
      const int alternate_plane = alternate_ref_plane(rsc->plane);
      if (alternate_plane != -1) {
        rsi = get_ref_frame_buf(rsc->cm, ref_idx)->rst_info[alternate_plane];
      }
      if (!rsi.frame_filters_on) continue;
#else
      continue;
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
    }
    rsc->temporal_pred_flag = 1;
    tmp_filter = rsi.frame_filters;
    rsc->num_filter_classes = tmp_filter.num_classes;

    double cost = obtain_temp_pred_frame_filters_cost(rsc, &tmp_filter);

    // Reset this bank to account for bits that signal the frame level filters.
    initialize_bank_with_best_frame_filter_match(rsc, &tmp_filter, &tmp_bank,
                                                 1);

    if (cost < best_cost) {
      best_cost = cost;
      best_ref_idx = ref_idx;
      best_filter = tmp_filter;
    }
  }

  best_num_classes = best_filter.num_classes;
  rsc->num_filter_classes = best_filter.num_classes;
  rsc->best_num_filter_classes = best_filter.num_classes;

  if (best_ref_idx >= 0) {
    rsc->temporal_pred_flag = 1;
    rsc->rst_ref_pic_idx = best_ref_idx;
    rsc->frame_filter_cost = 0;

  } else {
    rsc->temporal_pred_flag = 0;
  }
#endif  // CONFIG_TEMP_LR

  rsc->frame_filters_total_cost = best_cost;
  av1_reset_wienerns_bank(&rsc->frame_filter_bank,
                          rsc->cm->quant_params.base_qindex, best_num_classes,
                          rsc->plane != AOM_PLANE_Y);

  for (int c_id = 0; c_id < best_num_classes; ++c_id) {
    // Park best filter in the bank, first slot.
    av1_add_to_wienerns_bank(&rsc->frame_filter_bank, &best_filter, c_id);
    rsc->frame_filter_bank.filter->match_indices[c_id] =
        best_filter.match_indices[c_id];
    assert(rsc->frame_filter_bank.bank_size_for_class[c_id] == 1);
  }

#if CONFIG_TEMP_LR
  rsc->frame_filters = best_filter;
#endif  // CONFIG_TEMP_LR

  aom_free(best_cost_array);
  aom_free(work_cost_array);
}
#endif  // CONFIG_COMBINE_PC_NS_WIENER

static int rest_tiles_in_plane(const AV1_COMMON *cm, int plane) {
  const RestorationInfo *rsi = &cm->rst_info[plane];
  return rsi->units_per_tile;
}

// Set the value of number of units, for a given unit size.
void av1_reset_restoration_struct(AV1_COMMON *cm, RestorationInfo *rsi,
                                  int is_uv) {
  const AV1PixelRect tile_rect = av1_whole_frame_rect(cm, is_uv);
  const int max_tile_w = tile_rect.right - tile_rect.left;
  const int max_tile_h = tile_rect.bottom - tile_rect.top;

  // To calculate hpertile and vpertile (horizontal and vertical units per
  // tile), we basically want to divide the largest tile width or height by the
  // size of a restoration unit. Rather than rounding up unconditionally as you
  // might expect, we round to nearest, which models the way a right or bottom
  // restoration unit can extend to up to 150% its normal width or height. The
  // max with 1 is to deal with tiles that are smaller than half of a
  // restoration unit.
  const int unit_size = rsi->restoration_unit_size;
  const int hpertile = av1_lr_count_units_in_tile(unit_size, max_tile_w);
  const int vpertile = av1_lr_count_units_in_tile(unit_size, max_tile_h);

  rsi->units_per_tile = hpertile * vpertile;
  rsi->horz_units_per_tile = hpertile;
  rsi->vert_units_per_tile = vpertile;
}

#if CONFIG_COMBINE_PC_NS_WIENER
// Incorporates frame-level filters into the decision flow that compares
// RESTORE_WIENER_NONSEP and RESTORE_SWITCHABLE by replacing
// per-RU-specified filters (regular-RESTORE_WIENER_NONSEP) with
// frame-level-specified filters (frame-level-RESTORE_WIENER_NONSEP.)
//
// av1_pick_filter_restoration() proceeds by evaluating the cost of frame-level
// filters followed by regular-RESTORE_WIENER_NONSEP and RESTORE_SWITCHABLE with
// regular-RESTORE_WIENER_NONSEP. This routine decides whether the final mode
// should be frame-level-RESTORE_WIENER_NONSEP or RESTORE_SWITCHABLE with
// frame-level-RESTORE_WIENER_NONSEP.
static int replace_with_frame_filters(RestSearchCtxt *rsc, double *best_cost) {
  rsc->frame_filters_on = 1;
  rsc->num_filter_classes = rsc->best_num_filter_classes;

  // Update RESTORE_WIENER_NONSEP to use frame-level filters.
  rsc->num_wiener_nonsep = 0;
  double cost_again = search_rest_type(rsc, RESTORE_WIENER_NONSEP);
  if (rsc->num_wiener_nonsep) cost_again += rsc->frame_filter_cost;
  (void)cost_again;
  // should match the earlier calculated cost.
#if 0  // debug_point
  assert(fabs(cost_again - rsc->frame_filters_total_cost) < 1e-3);
#endif
  cost_again = rsc->frame_filters_total_cost;
  const int num_wiener_nonsep = rsc->num_wiener_nonsep;

  // Update RESTORE_SWITCHABLE to use frame-level filters.
  rsc->adjust_switchable_for_frame_filters = 1;
  rsc->num_wiener_nonsep = 0;
  double cost = search_rest_type(rsc, RESTORE_SWITCHABLE);
  if (rsc->num_wiener_nonsep) cost += rsc->frame_filter_cost;
  rsc->adjust_switchable_for_frame_filters = 0;

  int final_r = RESTORE_SWITCHABLE;
  if (cost > cost_again) {
    final_r = RESTORE_WIENER_NONSEP;
    rsc->num_wiener_nonsep = num_wiener_nonsep;
    cost = cost_again;
  }
  *best_cost = cost;
  return final_r;
}

#ifndef NDEBUG
void print_costs(double cost, char best_highlight, int r, char highlight,
                 const RestSearchCtxt *rsc, int unit_size) {
  if (!is_frame_filters_enabled(rsc->plane)) return;
  printf("%c U(%4d) Plane[%1d], %cr[%1d], %15.1f", best_highlight, unit_size,
         rsc->plane, highlight, r, cost);
  if (r == RESTORE_SWITCHABLE || r == RESTORE_WIENER_NONSEP) {
    printf(" (c:%3d, ns%3d).\n", rsc->num_filter_classes,
           rsc->num_wiener_nonsep);
  } else {
    printf(".\n");
  }
}
#endif  // NDEBUG

#endif  // CONFIG_COMBINE_PC_NS_WIENER

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

  Vector unit_stack;
  aom_vector_setup(&unit_stack,
                   1,                                // resizable capacity
                   sizeof(struct RstUnitSnapshot));  // element size
  Vector unit_indices;
  aom_vector_setup(&unit_indices,
                   1,             // resizable capacity
                   sizeof(int));  // element size

  RestSearchCtxt rsc;
  const int plane_start = AOM_PLANE_Y;
  const int plane_end = num_planes > 1 ? AOM_PLANE_V : AOM_PLANE_Y;

  Vector wienerns_stats;
  aom_vector_setup(&wienerns_stats,
                   1,                             // resizable capacity
                   sizeof(struct RstUnitStats));  // element size
  rsc.wienerns_stats = &wienerns_stats;
#if CONFIG_COMBINE_PC_NS_WIENER
  WienerNonsepInfoBank frame_filter_dict;
  double frame_filter_cost = DBL_MAX;
  int best_frame_filters_state = 0;
#if CONFIG_TEMP_LR
  int8_t best_temp_pred_flag = 0;
  int8_t best_temp_ref_idx = -1;
#endif  // CONFIG_TEMP_LR
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  uint16_t *luma = NULL;
  uint16_t *luma_buf;
  const YV12_BUFFER_CONFIG *dgd = &cpi->common.cur_frame->buf;
  rsc.luma_stride = dgd->crop_widths[1] + 2 * WIENERNS_UV_BRD;
  luma_buf = wienerns_copy_luma_highbd(
      dgd->buffers[AOM_PLANE_Y], dgd->crop_heights[AOM_PLANE_Y],
      dgd->crop_widths[AOM_PLANE_Y], dgd->strides[AOM_PLANE_Y], &luma,
      dgd->crop_heights[1], dgd->crop_widths[1], WIENERNS_UV_BRD,
      rsc.luma_stride, cm->seq_params.bit_depth
#if WIENERNS_CROSS_FILT_LUMA_TYPE == 2
      ,
#if CONFIG_IMPROVED_DS_CC_WIENER
      cm->seq_params.cfl_ds_filter_index
#else
      cm->seq_params.cfl_ds_filter_index == 1
#endif  // CONFIG_IMPROVED_DS_CC_WIENER
#endif
  );
  assert(luma_buf != NULL);

#if ISSUE_253
  rsc.luma_stat = luma;

  uint16_t *luma_virtual = NULL;
  uint16_t *luma_virtual_buf;

  luma_virtual_buf = wienerns_copy_luma_with_virtual_lines(cm, &luma_virtual);
  rsc.luma = luma_virtual;
#else
  rsc.luma = luma;
#endif  // ISSUE_253

  rsc.wienerns_tmpbuf =
      (double *)aom_malloc(WIENERNS_TMPBUF_SIZE * sizeof(*rsc.wienerns_tmpbuf));

  for (int plane = plane_start; plane <= plane_end; ++plane) {
    init_rsc(src, &cpi->common, x, &cpi->sf.lpf_sf, plane, rusi, &unit_stack,
             &unit_indices, &cpi->trial_frame_rst, &rsc);

    const int plane_ntiles = ntiles[plane > 0];
    const RestorationType num_rtypes =
        (plane_ntiles > 1) ? RESTORE_TYPES : RESTORE_SWITCHABLE_TYPES;

    double best_cost = DBL_MAX;
    RestorationType best_rtype = RESTORE_NONE;
#if CONFIG_COMBINE_PC_NS_WIENER
    best_frame_filters_state = 0;
#if CONFIG_TEMP_LR
    best_temp_pred_flag = 0;
    best_temp_ref_idx = -1;
    rsc.temporal_pred_flag = 0;
#endif  // CONFIG_TEMP_LR
    const int frame_filters_configured =
        cpi->common.features.lr_tools_disable_mask[plane > 0] &
                (1 << RESTORE_WIENER_NONSEP)
            ? 0
            : 1;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

    RestorationInfo *rsi = &cm->rst_info[plane];
    const int max_unit_size = rsi->max_restoration_unit_size;
    const int min_unit_size = rsi->min_restoration_unit_size;

    int best_unit_size = min_unit_size;

    for (int unit_size = min_unit_size; unit_size <= max_unit_size;
         unit_size <<= 1) {
      if (plane == 2 && unit_size != cm->rst_info[1].restoration_unit_size) {
        continue;
      }
      aom_vector_clear(&wienerns_stats);

      rsi->restoration_unit_size = unit_size;

      av1_reset_restoration_struct(cm, rsi, plane > 0);
      if (!cpi->sf.lpf_sf.disable_loop_restoration_chroma || !plane) {
        av1_extend_frame(rsc.dgd_buffer, rsc.plane_width, rsc.plane_height,
                         rsc.dgd_stride, RESTORATION_BORDER,
                         RESTORATION_BORDER);
#if CONFIG_COMBINE_PC_NS_WIENER
        assert(rsc.adjust_switchable_for_frame_filters == 0);
#endif  // CONFIG_COMBINE_PC_NS_WIENER
        for (RestorationType r = 0; r < num_rtypes; ++r) {
          if (
#if CONFIG_COMBINE_PC_NS_WIENER
              // Need classification and related stats. Run
              // search_pc_wiener_visitor without any filtering. (Follow
              // pcwiener_disabled.)
              (r != RESTORE_PC_WIENER) &&
#endif  // CONFIG_COMBINE_PC_NS_WIENER
              cpi->common.features.lr_tools_disable_mask[plane > 0] & (1 << r))
            continue;
#if !CONFIG_COMBINE_PC_NS_WIENER_ADD
          if (plane != AOM_PLANE_Y && r == RESTORE_PC_WIENER) continue;
#endif  // !CONFIG_COMBINE_PC_NS_WIENER_ADD

          gather_stats_rest_type(&rsc, r);

          if (r == RESTORE_WIENER_NONSEP) {
            rsc.num_filter_classes = default_num_classes(rsc.plane);
          }

#if CONFIG_COMBINE_PC_NS_WIENER
          if (r == RESTORE_WIENER_NONSEP &&
              is_frame_filters_enabled(rsc.plane) && frame_filters_configured) {
            // Find RDO-num_classes and frame-level filters. After this call
            // multiclass stats collapse to a single class. If that is not
            // desired make a copy of stats.
            rsc.frame_filters_on = 1;
            find_optimal_num_classes_and_frame_filters(&rsc);

            // Store for later copy into SWITCHABLE.
            frame_filter_dict = rsc.frame_filter_bank;
            frame_filter_cost = rsc.frame_filter_cost;
          }
          if (r == RESTORE_SWITCHABLE && is_frame_filters_enabled(rsc.plane) &&
              frame_filters_configured) {
            assert(RESTORE_WIENER_NONSEP < RESTORE_SWITCHABLE);
            rsc.frame_filter_bank = frame_filter_dict;
            rsc.frame_filter_cost = frame_filter_cost;
          }
          rsc.frame_filters_on = 0;
          rsc.num_filter_classes = 1;
#endif  // CONFIG_COMBINE_PC_NS_WIENER
          if (r == RESTORE_WIENER_NONSEP || r == RESTORE_SWITCHABLE)
            rsc.num_wiener_nonsep = 0;

          double cost = search_rest_type(&rsc, r);
          int real_r = r;
#if CONFIG_COMBINE_PC_NS_WIENER
          if (r == RESTORE_SWITCHABLE && is_frame_filters_enabled(plane) &&
              frame_filters_configured && cost > rsc.frame_filters_total_cost &&
              best_cost > rsc.frame_filters_total_cost) {
#if PRINT_LR_COSTS && !defined(NDEBUG)
            print_costs(cost, cost < best_cost ? '*' : ' ', r, ' ', &rsc,
                        rsi->restoration_unit_size);
#endif  // NDEBUG
            real_r = replace_with_frame_filters(&rsc, &cost);
          }
#if PRINT_LR_COSTS && !defined(NDEBUG)
          print_costs(cost, cost < best_cost ? '*' : ' ', real_r,
                      rsc.frame_filters_on ? 'f' : ' ', &rsc,
                      rsi->restoration_unit_size);
#endif
          assert(RESTORE_PC_WIENER < RESTORE_WIENER_NONSEP);
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
          if (r == RESTORE_PC_WIENER && is_frame_filters_enabled(plane) &&
              frame_filters_configured) {
#else
          if (r == RESTORE_PC_WIENER && plane == AOM_PLANE_Y) {
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
            rsc.classification_is_buffered = 1;  // Buffer is set.
          }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
          int invalid_result = 0;
          if (r == RESTORE_PC_WIENER && plane != AOM_PLANE_Y) {
            invalid_result = 1;
            assert(cost >= best_cost);
          }
          const int found_best = cost < best_cost && !invalid_result;
#else
          const int found_best = cost < best_cost;
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
          if (found_best) {
            best_cost = cost;
            best_rtype = real_r;
            best_unit_size = unit_size;
#if CONFIG_COMBINE_PC_NS_WIENER
            if (is_frame_filters_enabled(rsc.plane) &&
                frame_filters_configured) {
              best_frame_filters_state = rsc.frame_filters_on;
#if CONFIG_TEMP_LR
              if (rsc.frame_filters_on) {
                best_temp_pred_flag = rsc.temporal_pred_flag;
                best_temp_ref_idx = rsc.rst_ref_pic_idx;
              } else {
                best_temp_pred_flag = 0;
                best_temp_ref_idx = -1;
              }
#endif  // CONFIG_TEMP_LR
            }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
          }
        }
#if CONFIG_COMBINE_PC_NS_WIENER
        rsc.classification_is_buffered = 0;  // Buffer is consumed.
#endif                                       // CONFIG_COMBINE_PC_NS_WIENER
      }
#if CONFIG_COMBINE_PC_NS_WIENER
      if (is_frame_filters_enabled(rsc.plane)) {
        rsc.frame_filters_on = best_frame_filters_state;
#if CONFIG_TEMP_LR
        rsc.temporal_pred_flag = best_temp_pred_flag;
        rsc.rst_ref_pic_idx = best_temp_ref_idx;
#endif  // CONFIG_TEMP_LR
        if (!frame_filters_configured) {
          assert(best_temp_ref_idx == -1);
          assert(best_temp_pred_flag == 0);
          assert(best_frame_filters_state == 0);
        }
      }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
      if (rsi->restoration_unit_size == min_unit_size ||
          best_unit_size == rsi->restoration_unit_size) {
        finalize_frame_and_unit_info(best_rtype, &cm->rst_info[plane], &rsc);
      }
    }
    assert(IMPLIES(
        cm->features.lr_tools_count[plane] < 2,
        cm->rst_info[plane].frame_restoration_type != RESTORE_SWITCHABLE));
    rsi->restoration_unit_size = best_unit_size;
    av1_reset_restoration_struct(cm, rsi, plane > 0);
    int ru_num = rest_tiles_in_plane(cm, plane > 0);
    adjust_frame_rtype(&cm->rst_info[plane], ru_num, &rsc, &cpi->oxcf.tool_cfg);
    /*
    printf("[Frame %d][%d]: unit_size %d best_frame_filters_state %d(%d)\n",
           cm->current_frame.order_hint, plane, best_unit_size,
           best_frame_filters_state, rsc.best_num_filter_classes);
           */
  }

  aom_free(rusi);
  free(luma_buf);
#if ISSUE_253
  free(luma_virtual_buf);
#endif  // ISSUE_253
  aom_free(rsc.wienerns_tmpbuf);
  aom_vector_destroy(&wienerns_stats);
  aom_vector_destroy(&unit_stack);
  aom_vector_destroy(&unit_indices);
}
