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

#if CONFIG_RST_MERGECOEFFS
#include "third_party/vector/vector.h"
#endif  // CONFIG_RST_MERGECOEFFS

// Number of Wiener iterations
#define NUM_WIENER_ITERS 5

// Penalty factor for use of dual sgr
#define DUAL_SGR_PENALTY_MULT 0.01

#if CONFIG_RST_MERGECOEFFS
// Search level 0 - search all drl candidates
// Search level 1 - search drl candidates 0 and the best one for the current RU
// Search level 2 - search only the best drl candidate for the current RU
#define MERGE_DRL_SEARCH_LEVEL 1
#endif  // CONFIG_RST_MERGECOEFFS

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
#if CONFIG_WIENER_NONSEP
  WienerNonsepInfo wienerns_info;
#endif  // CONFIG_WIENER_NONSEP

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
  const YV12_BUFFER_CONFIG *dgd;
  YV12_BUFFER_CONFIG *dst;

  const AV1_COMMON *cm;
  const MACROBLOCK *x;
  int plane;
  int plane_width;
  int plane_height;
  RestUnitSearchInfo *rusi;

  // Speed features
  const LOOP_FILTER_SPEED_FEATURES *lpf_sf;

  uint8_t *dgd_buffer;
  int dgd_stride;
  const uint8_t *src_buffer;
  int src_stride;
#if CONFIG_COMBINE_PC_NS_WIENER
  bool is_buffered;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

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
#if CONFIG_WIENER_NONSEP
  WienerNonsepInfoBank wienerns_bank;

  // Vector storing statistics for all RUs.
  Vector *wienerns_stats;

  // Number of classes in the initial wienerns stat calculation.
  int num_stat_classes;
  // Number of classes in the wienerns filtering calculation.
  int num_filter_classes;

#if CONFIG_WIENER_NONSEP_CROSS_FILT
  const uint8_t *luma;
  int luma_stride;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
#endif  // CONFIG_WIENER_NONSEP

#if CONFIG_RST_MERGECOEFFS
  // This vector holds the most recent list of units with merged coefficients.
  Vector *unit_stack;
  // This vector holds a list of rest_unit indices to be considered for merging
  // for a given drl candidate to be examined. Note that the unit_stack above
  // includes all previous RUs covering all entries in the drl list, but only
  // a subset needs to be considered for merging for a given drl candidate.
  Vector *unit_indices;
#endif  // CONFIG_RST_MERGECOEFFS

  AV1PixelRect tile_rect;
} RestSearchCtxt;

#if CONFIG_WIENER_NONSEP
// RU statistics for solving Wiener filters.
typedef struct RstUnitStats {
  double A[WIENERNS_MAX_CLASSES * WIENERNS_MAX * WIENERNS_MAX];
  double b[WIENERNS_MAX_CLASSES * WIENERNS_MAX];
  // TODO(oguleryuz): Add weights.
  int64_t real_sse;
  int num_stats_classes;
  int ru_idx;          // debug.
  int ru_idx_in_tile;  // debug.
  int plane;           // debug.
} RstUnitStats;
#endif  // CONFIG_WIENER_NONSEP

#if CONFIG_RST_MERGECOEFFS
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
  WienerInfoBank ref_wiener_bank;
#if CONFIG_WIENER_NONSEP
  // Nonseparable Wiener filter info.
  // Pointers to respective stats in RstUnitStats.
  const double *A;
  const double *b;
  WienerNonsepInfoBank ref_wienerns_bank;
#endif  // CONFIG_WIENER_NONSEP
  // Sgrproj filter info
  SgrprojInfoBank ref_sgrproj_bank;
} RstUnitSnapshot;
#endif  // CONFIG_RST_MERGECOEFFS

static AOM_INLINE void reset_all_banks(RestSearchCtxt *rsc) {
  av1_reset_wiener_bank(&rsc->wiener_bank);
  av1_reset_sgrproj_bank(&rsc->sgrproj_bank);
#if CONFIG_WIENER_NONSEP
  av1_reset_wienerns_bank(&rsc->wienerns_bank,
                          rsc->cm->quant_params.base_qindex,
                          rsc->num_filter_classes, rsc->plane != AOM_PLANE_Y);
#endif  // CONFIG_WIENER_NONSEP
}

static AOM_INLINE void rsc_on_tile(void *priv, int idx) {
  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  reset_all_banks(rsc);
  rsc->tile_stripe0 = 0;
  rsc->ru_idx_base = idx;
}

#if CONFIG_SAVE_IN_LOOP_DATA

// Basic data structure to maintain the in-loop data export. Manipulate using
// below-defined methods.
#define LEN_FILENAME 32
#define POC_REGISTER_SIZE 1024
typedef struct {
  const int len_filename;

  // File name to save the data under.
  char filename[LEN_FILENAME];

  // Rudimentary data structure to prevent double saving of frames that are
  // visited twice.
  const int poc_register_size;
  int poc_register[POC_REGISTER_SIZE];

  // All frames within the export are assumed to have this size. For now only
  // luma-related exports are supported.
  int num_rows_luma;
  int num_cols_luma;

  // Only frames satisfying (frame_number % skip_frame == 0) are exported.
  const int skip_frame;

  bool initialized;
} ExportContext;

// Struct instance for the in-loop data export.
static ExportContext export_context = {
  .len_filename = LEN_FILENAME,
  .filename = "test_set.dat",
  .poc_register_size = POC_REGISTER_SIZE,
  .poc_register = { -1 },  // This should be sufficient if poc always starts at
                           // 0.
  .num_rows_luma = 0,
  .num_cols_luma = 0,
  .initialized = false,
  .skip_frame = 1
};

// Basic methods to maintain the in-loop data export.

// Changes export filename from the default.
static void export_context_set_filename(const char *filename) {
  // Add a null path so that we can use this function once and avoid
  // -Wunused-function.
  if (filename == NULL) return;
  snprintf(export_context.filename, export_context.len_filename, "%s",
           filename);
}

// Returns true if the frame corresponding to this frame_number has been
// exported before. Useful in handling frames visited twice.
static bool export_context_is_exported(int frame_number) {
  const int register_slot = frame_number % export_context.poc_register_size;
  const int prev_saved_frame_no = export_context.poc_register[register_slot];
  return frame_number == prev_saved_frame_no;
}

// Updates the register with the saved frame.
static void export_context_register_as_exported(int frame_number) {
  const int register_slot = frame_number % export_context.poc_register_size;
  export_context.poc_register[register_slot] = frame_number;
}

static bool export_context_is_skipped(int frame_number) {
  return frame_number % export_context.skip_frame != 0;
}

static bool export_context_is_initialized() {
  return export_context.initialized;
}

static bool export_context_initialize(int num_rows_luma, int num_cols_luma,
                                      float rdmult) {
  assert(export_context.initialized == false);

  FILE *export_file = fopen(export_context.filename, "wb");
  if (export_file == NULL) return false;
  fwrite(&num_rows_luma, sizeof(num_rows_luma), 1, export_file);
  fwrite(&num_cols_luma, sizeof(num_cols_luma), 1, export_file);
  fwrite(&rdmult, sizeof(rdmult), 1, export_file);
  fclose(export_file);

  // Just in case.
  for (int slot = 0; slot < export_context.poc_register_size; ++slot)
    export_context.poc_register[slot] = -1;

  export_context.num_rows_luma = num_rows_luma;
  export_context.num_cols_luma = num_cols_luma;
  export_context.initialized = true;
  return true;
}

// Saves the frame data as floating point values. frame should have
// export_context.num_rows_luma and export_context.num_cols_luma dimensions.
// If upsample_factor > 1 then frame data is pixel-repeated. This useful
// in saving tskip-like data.
static bool export_context_export_frame(const uint8_t *frame, int stride,
                                        bool high_bd, int upsample_factor) {
  assert(export_context.initialized == true);
  assert(upsample_factor >= 1);

  // Inefficient but convenient. OK since we only export data infrequently.
  const uint8_t *frame_8bit = (uint8_t *)frame;
  const uint16_t *frame_16bit = CONVERT_TO_SHORTPTR(frame);

  // Append to export.
  FILE *export_file = fopen(export_context.filename, "ab");
  if (export_file == NULL) return false;
  for (int r = 0; r < export_context.num_rows_luma; ++r) {
    int dr = r / upsample_factor;
    for (int c = 0; c < export_context.num_cols_luma; ++c) {
      int dc = c / upsample_factor;
      const float pixel_value = (float)(high_bd ? frame_16bit[dr * stride + dc]
                                                : frame_8bit[dr * stride + dc]);
      fwrite(&pixel_value, sizeof(pixel_value), 1, export_file);
    }
  }
  fclose(export_file);
  return true;
}

// Exports qstep.
static bool export_context_export_qstep(AV1_COMP *cpi) {
  assert(export_context.initialized == true);

  AV1_COMMON *const cm = &cpi->common;

  // Append a constant qstep value to export. This should be replaced with
  // frame varying qstep if cases outside of AOM CC need to be considered.
  FILE *export_file = fopen(export_context.filename, "ab");
  if (export_file == NULL) return false;
  for (int plane = 0; plane < 3; ++plane) {
    int offset = 0;
    if (plane != AOM_PLANE_Y)
      offset = plane == AOM_PLANE_U ? cm->quant_params.u_dc_delta_q
                                    : cm->quant_params.v_dc_delta_q;
    else
      offset = cm->quant_params.y_dc_delta_q;
    const float qstep = (float)av1_convert_qindex_to_q(
        cm->quant_params.base_qindex + offset, cm->seq_params.bit_depth);

    for (int r = 0; r < export_context.num_rows_luma; ++r) {
      for (int c = 0; c < export_context.num_cols_luma; ++c) {
        const float pixel_value = qstep;
        fwrite(&pixel_value, sizeof(pixel_value), 1, export_file);
      }
    }
  }
  fclose(export_file);
  return true;
}

#endif  // CONFIG_SAVE_IN_LOOP_DATA

static AOM_INLINE void reset_rsc(RestSearchCtxt *rsc) {
  rsc->sse = 0;
  rsc->bits = 0;
#if CONFIG_RST_MERGECOEFFS
  aom_vector_clear(rsc->unit_stack);
  aom_vector_clear(rsc->unit_indices);
#endif  // CONFIG_RST_MERGECOEFFS
}

static AOM_INLINE void init_rsc(const YV12_BUFFER_CONFIG *src,
                                const AV1_COMMON *cm, const MACROBLOCK *x,
                                const LOOP_FILTER_SPEED_FEATURES *lpf_sf,
                                int plane, RestUnitSearchInfo *rusi,
                                YV12_BUFFER_CONFIG *dst,
#if CONFIG_RST_MERGECOEFFS
                                Vector *unit_stack, Vector *unit_indices,
#endif  // CONFIG_RST_MERGECOEFFS
                                RestSearchCtxt *rsc) {
  const YV12_BUFFER_CONFIG *dgd = &cm->cur_frame->buf;

  const int is_uv = plane != AOM_PLANE_Y;
  rsc->src = src;
  rsc->dst = dst;
  rsc->cm = cm;
  rsc->x = x;
  rsc->plane = plane;
  rsc->rusi = rusi;
  rsc->lpf_sf = lpf_sf;
  rsc->dgd = dgd;

  rsc->plane_width = src->crop_widths[is_uv];
  rsc->plane_height = src->crop_heights[is_uv];
  rsc->src_stride = src->strides[is_uv];
  rsc->src_buffer = src->buffers[plane];
  rsc->dgd_stride = dgd->strides[is_uv];
  rsc->dgd_buffer = dgd->buffers[plane];
  rsc->tile_rect = av1_whole_frame_rect(cm, is_uv);
  assert(src->crop_widths[is_uv] == dgd->crop_widths[is_uv]);
  assert(src->crop_heights[is_uv] == dgd->crop_heights[is_uv]);
#if CONFIG_RST_MERGECOEFFS
  rsc->unit_stack = unit_stack;
  rsc->unit_indices = unit_indices;
#endif  // CONFIG_RST_MERGECOEFFS
#if CONFIG_WIENER_NONSEP
  rsc->num_stat_classes =
      is_uv ? NUM_WIENERNS_CLASS_INIT_CHROMA : NUM_WIENERNS_CLASS_INIT_LUMA;
  rsc->num_filter_classes = rsc->num_stat_classes;
#endif  // CONFIG_WIENER_NONSEP
}

static int rest_tiles_in_plane(const AV1_COMMON *cm, int plane) {
  const RestorationInfo *rsi = &cm->rst_info[plane];
  return rsi->units_per_tile;
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

int64_t av1_highbd_pixel_proj_error_c(const uint8_t *src8, int width,
                                      int height, int src_stride,
                                      const uint8_t *dat8, int dat_stride,
                                      int32_t *flt0, int flt0_stride,
                                      int32_t *flt1, int flt1_stride, int xq[2],
                                      const sgr_params_type *params) {
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dat = CONVERT_TO_SHORTPTR(dat8);
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

static int64_t get_pixel_proj_error(const uint8_t *src8, int width, int height,
                                    int src_stride, const uint8_t *dat8,
                                    int dat_stride, int32_t *flt0,
                                    int flt0_stride, int32_t *flt1,
                                    int flt1_stride, int *xqd,
                                    const sgr_params_type *params) {
  int xq[2];
  av1_decode_xq(xqd, xq, params);

  return av1_highbd_pixel_proj_error(src8, width, height, src_stride, dat8,
                                     dat_stride, flt0, flt0_stride, flt1,
                                     flt1_stride, xq, params);
}

#define USE_SGRPROJ_REFINEMENT_SEARCH 1
static int64_t finer_search_pixel_proj_error(
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int start_step, int *xqd,
    const sgr_params_type *params) {
  int64_t err =
      get_pixel_proj_error(src8, width, height, src_stride, dat8, dat_stride,
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
          err2 = get_pixel_proj_error(src8, width, height, src_stride, dat8,
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
          err2 = get_pixel_proj_error(src8, width, height, src_stride, dat8,
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
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dat = CONVERT_TO_SHORTPTR(dat8);
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
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride,
    int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dat = CONVERT_TO_SHORTPTR(dat8);
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
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt1, int flt1_stride,
    int64_t H[2][2], int64_t C[2]) {
  const int size = width * height;
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dat = CONVERT_TO_SHORTPTR(dat8);
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
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int64_t H[2][2], int64_t C[2],
    const sgr_params_type *params) {
  if ((params->r[0] > 0) && (params->r[1] > 0)) {
    calc_proj_params_r0_r1_high_bd_c(src8, width, height, src_stride, dat8,
                                     dat_stride, flt0, flt0_stride, flt1,
                                     flt1_stride, H, C);
  } else if (params->r[0] > 0) {
    calc_proj_params_r0_high_bd_c(src8, width, height, src_stride, dat8,
                                  dat_stride, flt0, flt0_stride, H, C);
  } else if (params->r[1] > 0) {
    calc_proj_params_r1_high_bd_c(src8, width, height, src_stride, dat8,
                                  dat_stride, flt1, flt1_stride, H, C);
  }
}

static AOM_INLINE void get_proj_subspace(
    const uint8_t *src8, int width, int height, int src_stride,
    const uint8_t *dat8, int dat_stride, int32_t *flt0, int flt0_stride,
    int32_t *flt1, int flt1_stride, int *xq, const sgr_params_type *params) {
  int64_t H[2][2] = { { 0, 0 }, { 0, 0 } };
  int64_t C[2] = { 0, 0 };

  // Default values to be returned if the problem becomes ill-posed
  xq[0] = 0;
  xq[1] = 0;

  av1_calc_proj_params_high_bd_c(src8, width, height, src_stride, dat8,
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
static AOM_INLINE void apply_sgr(int sgr_params_idx, const uint8_t *dat8,
                                 int width, int height, int dat_stride,
                                 int bit_depth, int pu_width, int pu_height,
                                 int32_t *flt0, int32_t *flt1, int flt_stride) {
  for (int i = 0; i < height; i += pu_height) {
    const int h = AOMMIN(pu_height, height - i);
    int32_t *flt0_row = flt0 + i * flt_stride;
    int32_t *flt1_row = flt1 + i * flt_stride;
    const uint8_t *dat8_row = dat8 + i * dat_stride;

    // Iterate over the stripe in blocks of width pu_width
    for (int j = 0; j < width; j += pu_width) {
      const int w = AOMMIN(pu_width, width - j);
      const int ret = av1_selfguided_restoration(
          dat8_row + j, w, h, dat_stride, flt0_row + j, flt1_row + j,
          flt_stride, sgr_params_idx, bit_depth);
      (void)ret;
      assert(!ret);
    }
  }
}

static int64_t compute_sgrproj_err(const uint8_t *dat8, const int width,
                                   const int height, const int dat_stride,
                                   const uint8_t *src8, const int src_stride,
                                   const int bit_depth, const int pu_width,
                                   const int pu_height, const int ep,
                                   int32_t *flt0, int32_t *flt1,
                                   const int flt_stride, int *exqd) {
  int exq[2];
  apply_sgr(ep, dat8, width, height, dat_stride, bit_depth, pu_width, pu_height,
            flt0, flt1, flt_stride);
  aom_clear_system_state();
  const sgr_params_type *const params = &av1_sgr_params[ep];
  get_proj_subspace(src8, width, height, src_stride, dat8, dat_stride, flt0,
                    flt_stride, flt1, flt_stride, exq, params);
  aom_clear_system_state();
  encode_xq(exq, exqd, params);
  int64_t err = finer_search_pixel_proj_error(
      src8, width, height, src_stride, dat8, dat_stride, flt0, flt_stride, flt1,
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

  uint8_t *dat8;
  const uint8_t *src8;
  int width, height, dat_stride, src_stride, flt_stride;
  dat_stride = rsc->dgd_stride;
  src_stride = rsc->src_stride;
  if (limits != NULL) {
    dat8 =
        rsc->dgd_buffer + limits->v_start * rsc->dgd_stride + limits->h_start;
    src8 =
        rsc->src_buffer + limits->v_start * rsc->src_stride + limits->h_start;
    width = limits->h_end - limits->h_start;
    height = limits->v_end - limits->v_start;
    flt_stride = ((width + 7) & ~7) + 8;
    err = compute_sgrproj_err(dat8, width, height, dat_stride, src8, src_stride,
                              bit_depth, pu_width, pu_height, ep, flt0, flt1,
                              flt_stride, exqd);
  } else {
#if CONFIG_RST_MERGECOEFFS
    Vector *current_unit_stack = rsc->unit_stack;
    Vector *current_unit_indices = rsc->unit_indices;
    int n = 0;
    int idx = *(int *)aom_vector_const_get(current_unit_indices, n);
    VECTOR_FOR_EACH(current_unit_stack, listed_unit) {
      RstUnitSnapshot *old_unit = (RstUnitSnapshot *)(listed_unit.pointer);
      if (old_unit->rest_unit_idx == idx) {
        RestorationTileLimits old_limits = old_unit->limits;
        dat8 = rsc->dgd_buffer + old_limits.v_start * rsc->dgd_stride +
               old_limits.h_start;
        src8 = rsc->src_buffer + old_limits.v_start * rsc->src_stride +
               old_limits.h_start;
        width = old_limits.h_end - old_limits.h_start;
        height = old_limits.v_end - old_limits.v_start;
        flt_stride = ((width + 7) & ~7) + 8;
        err += compute_sgrproj_err(dat8, width, height, dat_stride, src8,
                                   src_stride, bit_depth, pu_width, pu_height,
                                   ep, flt0, flt1, flt_stride, exqd);
        n++;
        if (n >= (int)current_unit_indices->size) break;
        idx = *(int *)aom_vector_const_get(current_unit_indices, n);
      }
    }
#else   // CONFIG_RST_MERGECOEFFS
    assert(0 && "Tile limits should not be NULL.");
#endif  // CONFIG_RST_MERGECOEFFS
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
  int exqd[2] = { 0, 0 }, bestxqd[2] = { 0, 0 };
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
                                  const SgrprojInfo *sgrproj_info,
                                  const SgrprojInfoBank *bank) {
  (void)mode_costs;
  int64_t bits = 0;
#if CONFIG_RST_MERGECOEFFS
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
#else
  const SgrprojInfo *ref_sgrproj_info = av1_constref_from_sgrproj_bank(bank, 0);
#endif  // CONFIG_RST_MERGECOEFFS
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

#if CONFIG_RST_MERGECOEFFS
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

int get_sgrproj_best_ref(const ModeCosts *mode_costs, const SgrprojInfo *info,
                         const SgrprojInfoBank *bank) {
  SgrprojInfo info_ = *info;
  int64_t best_bits = INT64_MAX;
  int best_ref = -1;
  for (int ref = 0; ref < AOMMAX(1, bank->bank_size); ++ref) {
    info_.bank_ref = ref;
    const int64_t bits = count_sgrproj_bits(mode_costs, &info_, bank);
    if (bits < best_bits) {
      best_bits = bits;
      best_ref = ref;
    }
  }
  return AOMMAX(0, best_ref);
}
#endif  // CONFIG_RST_MERGECOEFFS

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

#if CONFIG_RST_MERGECOEFFS
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
#else   // CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_RST_MERGECOEFFS
}

void av1_compute_stats_highbd_c(int wiener_win, const uint8_t *dgd8,
                                const uint8_t *src8, int h_start, int h_end,
                                int v_start, int v_end, int dgd_stride,
                                int src_stride, int64_t *M, int64_t *H,
                                aom_bit_depth_t bit_depth) {
  int i, j, k, l;
  int32_t Y[WIENER_WIN2];
  const int wiener_win2 = wiener_win * wiener_win;
  const int wiener_halfwin = (wiener_win >> 1);
  const uint16_t *src = CONVERT_TO_SHORTPTR(src8);
  const uint16_t *dgd = CONVERT_TO_SHORTPTR(dgd8);
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
    P += (int64_t)ab[k] * M[k] / (WIENER_FILT_STEP * WIENER_FILT_STEP);
    for (l = 0; l < wiener_win2; ++l) {
      Q += ((int64_t)ab[k] * (H[k * wiener_win2 + l] / WIENER_FILT_STEP) /
            WIENER_FILT_STEP) *
           (int64_t)ab[l] / (WIENER_FILT_STEP * WIENER_FILT_STEP);
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

#if CONFIG_PC_WIENER

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
  const int plane = rsc->plane;

  bool skip_search = !PC_WIENER_FILTER_CHROMA && plane != AOM_PLANE_Y &&
                     !PC_WIENER_ONLY_CLASSIFY_CHROMA;
  if (skip_search) {
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_PC_WIENER - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_PC_WIENER] = INT64_MAX;
    return;
  }

  RestorationUnitInfo rui;
  rui.plane = plane;
  rui.restoration_type = RESTORE_PC_WIENER;
  rui.tskip = rsc->cm->mi_params.tx_skip[plane];
  rui.tskip_stride = rsc->cm->mi_params.tx_skip_stride[plane];
  rui.base_qindex = rsc->cm->quant_params.base_qindex;
  if (plane != AOM_PLANE_Y)
    rui.qindex_offset = plane == AOM_PLANE_U
                            ? rsc->cm->quant_params.u_dc_delta_q
                            : rsc->cm->quant_params.v_dc_delta_q;
  else
    rui.qindex_offset = rsc->cm->quant_params.y_dc_delta_q;
  rui.class_id = rsc->cm->mi_params.class_id[plane];
  rui.class_id_stride = rsc->cm->mi_params.class_id_stride[plane];
  rusi->sse[RESTORE_PC_WIENER] =
      try_restoration_unit(rsc, limits, &rsc->tile_rect, &rui);

  if (PC_WIENER_ONLY_CLASSIFY_CHROMA && plane != AOM_PLANE_Y) {
    // Classified the data for downsteram processing.
    rsc->bits += bits_none;
    rsc->sse += rusi->sse[RESTORE_NONE];
    rusi->best_rtype[RESTORE_PC_WIENER - 1] = RESTORE_NONE;
    rusi->sse[RESTORE_PC_WIENER] = INT64_MAX;
    return;
  }

  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);

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
#endif  // CONFIG_PC_WIENER

static int64_t count_wiener_bits(int wiener_win, const ModeCosts *mode_costs,
                                 const WienerInfo *wiener_info,
                                 const WienerInfoBank *bank) {
  (void)mode_costs;
  int64_t bits = 0;
#if CONFIG_RST_MERGECOEFFS
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
#else
  const WienerInfo *ref_wiener_info = av1_constref_from_wiener_bank(bank, 0);
#endif  // CONFIG_RST_MERGECOEFFS
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

#if CONFIG_RST_MERGECOEFFS
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

int get_wiener_best_ref(int wiener_win, const ModeCosts *mode_costs,
                        const WienerInfo *info, const WienerInfoBank *bank) {
  WienerInfo info_ = *info;
  int64_t best_bits = INT64_MAX;
  int best_ref = -1;
  for (int ref = 0; ref < AOMMAX(1, bank->bank_size); ++ref) {
    info_.bank_ref = ref;
    const int64_t bits =
        count_wiener_bits(wiener_win, mode_costs, &info_, bank);
    if (bits < best_bits) {
      best_bits = bits;
      best_ref = ref;
    }
  }
  return AOMMAX(0, best_ref);
}
#endif  // CONFIG_RST_MERGECOEFFS

#if CONFIG_WIENER_NONSEP || CONFIG_RST_MERGECOEFFS

// If limits != NULL, calculates error for current restoration unit.
// Otherwise, calculates error for all units in the stack using stored limits.
static int64_t calc_finer_tile_search_error(const RestSearchCtxt *rsc,
                                            const RestorationTileLimits *limits,
                                            const AV1PixelRect *tile,
                                            RestorationUnitInfo *rui) {
  int64_t err = 0;
#if CONFIG_RST_MERGECOEFFS
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
#else   // CONFIG_RST_MERGECOEFFS || CONFIG_RST_MERGECOEFFS
  err = try_restoration_unit(rsc, limits, tile, rui);
#endif  // CONFIG_RST_MERGECOEFFS || CONFIG_RST_MERGECOEFFS
  return err;
}

#if CONFIG_WIENER_NONSEP && CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_WIENER_NONSEP && CONFIG_RST_MERGECOEFFS

#endif  // CONFIG_WIENER_NONSEP

#define USE_WIENER_REFINEMENT_SEARCH 1
static int64_t finer_tile_search_wiener(RestSearchCtxt *rsc,
                                        const RestorationTileLimits *limits,
                                        const AV1PixelRect *tile,
                                        RestorationUnitInfo *rui,
                                        int wiener_win, int reduced_wiener_win,
                                        const WienerInfoBank *ref_wiener_bank) {
  const int plane_off = (WIENER_WIN - reduced_wiener_win) >> 1;
#if CONFIG_RST_MERGECOEFFS
  int64_t err = calc_finer_tile_search_error(rsc, limits, tile, rui);
#else   // CONFIG_RST_MERGECOEFFS
  int64_t err = try_restoration_unit(rsc, limits, tile, rui);
#endif  // CONFIG_RST_MERGECOEFFS
#if USE_WIENER_REFINEMENT_SEARCH
  WienerInfo *plane_wiener = &rui->wiener_info;

  const MACROBLOCK *const x = rsc->x;
#if CONFIG_RST_MERGECOEFFS
  int64_t bits = count_wiener_bits_set(wiener_win, &x->mode_costs, plane_wiener,
                                       ref_wiener_bank);
#else
  int64_t bits = count_wiener_bits(wiener_win, &x->mode_costs, plane_wiener,
                                   ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
#if CONFIG_RST_MERGECOEFFS
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                                plane_wiener, ref_wiener_bank);
#else   // CONFIG_RST_MERGECOEFFS
          int64_t err2 = try_restoration_unit(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
#if CONFIG_RST_MERGECOEFFS
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                                plane_wiener, ref_wiener_bank);
#else   // CONFIG_RST_MERGECOEFFS
          int64_t err2 = try_restoration_unit(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
#if CONFIG_RST_MERGECOEFFS
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                                plane_wiener, ref_wiener_bank);
#else   // CONFIG_RST_MERGECOEFFS
          int64_t err2 = try_restoration_unit(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
#if CONFIG_RST_MERGECOEFFS
          int64_t err2 = calc_finer_tile_search_error(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                                plane_wiener, ref_wiener_bank);
#else   // CONFIG_RST_MERGECOEFFS
          int64_t err2 = try_restoration_unit(rsc, limits, tile, rui);
          int64_t bits2 = count_wiener_bits(wiener_win, &x->mode_costs,
                                            plane_wiener, ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
#if CONFIG_RST_MERGECOEFFS
  // Set bank_ref correctly
  (void)count_wiener_bits_set(wiener_win, &x->mode_costs, plane_wiener,
                              ref_wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
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
  const int bit_depth = rsc->cm->seq_params.bit_depth;

  // Skip Wiener search for low variance contents
  if (rsc->lpf_sf->prune_wiener_based_on_src_var) {
    const int scale[3] = { 0, 1, 2 };
    // Obtain the normalized Qscale
    const int qs =
        av1_dc_quant_QTX(rsc->cm->quant_params.base_qindex, 0,
                         rsc->cm->seq_params.base_y_dc_delta_q, bit_depth) >>
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

  av1_compute_stats_highbd(reduced_wiener_win, rsc->dgd_buffer, rsc->src_buffer,
                           limits->h_start, limits->h_end, limits->v_start,
                           limits->v_end, rsc->dgd_stride, rsc->src_stride, M,
                           H, bit_depth);

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
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);
#if CONFIG_RST_MERGECOEFFS
  Vector *current_unit_stack = rsc->unit_stack;
  int64_t bits_nomerge_base =
      x->mode_costs.wiener_restore_cost[1] +
      count_wiener_bits_set(wiener_win, &x->mode_costs, &rusi->wiener_info,
                            &rsc->wiener_bank);
  const int bank_ref_base = rusi->wiener_info.bank_ref;
  // Only test the reference in rusi->wiener_info.bank_ref, generated from
  // the count call above.

  double cost_nomerge_base = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_nomerge_base >> 4, rusi->sse[RESTORE_WIENER], bit_depth);
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
    int num_units = 0;
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
      num_units++;
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
#else   // CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_RST_MERGECOEFFS
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

#if CONFIG_WIENER_NONSEP
static int64_t count_wienerns_bits(
    int plane, const ModeCosts *mode_costs,
    const WienerNonsepInfo *wienerns_info, const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int class_id) {
  (void)mode_costs;
  int is_uv = (plane != AOM_PLANE_Y);
  int64_t bits = 0;
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };

  int c_id_begin = 0;
  int c_id_end = wienerns_info->num_classes;
  if (class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = class_id;
    c_id_end = class_id + 1;
  }
#if CONFIG_RST_MERGECOEFFS
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
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
#endif  // CONFIG_RST_MERGECOEFFS
  const int(*reduce_cost)[2] = mode_costs->wienerns_reduce_cost;
#if CONFIG_LR_4PART_CODE
  const int(*cost_4part)[4] = mode_costs->wienerns_4part_cost;
#endif  // CONFIG_LR_4PART_CODE
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  int reduce_step[WIENERNS_REDUCE_STEPS];
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    if (skip_filter_write_for_class[c_id]) continue;
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(bank, ref_for_class[c_id], c_id);

    const int16_t *wienerns_info_nsfilter =
        const_nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);
    memset(reduce_step, 0, sizeof(reduce_step));
    if (end_feat - beg_feat > 1 && wienerns_info_nsfilter[end_feat - 1] == 0) {
      reduce_step[WIENERNS_REDUCE_STEPS - 1] = 1;
      if (end_feat - beg_feat > 2 &&
          wienerns_info_nsfilter[end_feat - 2] == 0) {
        reduce_step[WIENERNS_REDUCE_STEPS - 2] = 1;
        if (end_feat - beg_feat > 3 &&
            wienerns_info_nsfilter[end_feat - 3] == 0) {
          reduce_step[WIENERNS_REDUCE_STEPS - 3] = 1;
          if (end_feat - beg_feat > 4 &&
              wienerns_info_nsfilter[end_feat - 4] == 0) {
            reduce_step[WIENERNS_REDUCE_STEPS - 4] = 1;
            if (end_feat - beg_feat > 5 &&
                wienerns_info_nsfilter[end_feat - 5] == 0) {
              reduce_step[WIENERNS_REDUCE_STEPS - 5] = 1;
            }
          }
        }
      }
    }
    const int rodd = is_uv ? 0 : (end_feat & 1);
    for (int i = beg_feat; i < end_feat; ++i) {
      if (rodd && i == end_feat - 5 && i != beg_feat) {
        bits += reduce_cost[0][reduce_step[0]];
        if (reduce_step[0]) break;
      }
      if (!rodd && i == end_feat - 4 && i != beg_feat) {
        bits += reduce_cost[1][reduce_step[1]];
        if (reduce_step[1]) break;
      }
      if (rodd && i == end_feat - 3 && i != beg_feat) {
        bits += reduce_cost[2][reduce_step[2]];
        if (reduce_step[2]) break;
      }
      if (!rodd && i == end_feat - 2 && i != beg_feat) {
        bits += reduce_cost[3][reduce_step[3]];
        if (reduce_step[3]) break;
      }
      if (rodd && i == end_feat - 1 && i != beg_feat) {
        bits += reduce_cost[4][reduce_step[4]];
        if (reduce_step[4]) break;
      }
#if CONFIG_LR_4PART_CODE
      bits += aom_count_4part_wref(
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          cost_4part[wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID]],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID], AV1_PROB_COST_SHIFT);
#else
      bits += aom_count_primitive_refsubexpfin(
                  (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]),
                  wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID],
                  ref_wienerns_info_nsfilter[i] -
                      wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
                  wienerns_info_nsfilter[i] -
                      wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID])
              << AV1_PROB_COST_SHIFT;
#endif  // CONFIG_LR_4PART_CODE
    }
  }
  return bits;
}

#if CONFIG_RST_MERGECOEFFS
static int64_t count_wienerns_bits_set(
    int plane, const ModeCosts *mode_costs, WienerNonsepInfo *info,
    const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int class_id) {
  int64_t total_bits = 0;
  int c_id_begin = 0;
  int c_id_end = info->num_classes;
  if (class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = class_id;
    c_id_end = class_id + 1;
  }
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int64_t best_bits = INT64_MAX;
    int best_ref = -1;
    for (int ref = 0; ref < AOMMAX(1, bank->bank_size_for_class[c_id]); ++ref) {
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
#endif  // CONFIG_RST_MERGECOEFFS

static int16_t quantize(double x, int16_t minv, int16_t n, int prec_bits) {
  int scale_x = (int)round(x * (1 << prec_bits));
  scale_x = AOMMAX(scale_x, minv);
  scale_x = AOMMIN(scale_x, minv + n - 1);
  return (int16_t)scale_x;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#define USE_Q_WRAPPER 1

// quantize_wrapper() allows a better (in D) solution to the linear system
// compared to rounding. It is intended as a better initializer than rounding.
// As is, quantize_wrapper() only uses distortion but it can be augmented to use
// total D-R cost. Intended use is to initialize with quantize_wrapper() then
// run a reduced set of iterations within finer_tile_search_wienerns() for
// complexity and quality improvements.
//
// ~20 q_wrapper iterations are ~ 1 finer_tile iteration for a 256 x 256 RU.
// When effective RU size increases with RST_MERGE the 10x simplifcation will
// increase.
#define Q_WRAPPER_MAX_ITER 20
#define MAX_INCREMENT 2  // Controls the extent of each greedy update.

// After Q_WRAPPER_MAX_ITER iterations one can reduce the finer_tile iterations.
#define FINER_TILE_SEARCH_WIENERNS_ITER_STEP (USE_Q_WRAPPER ? 5 : 12)

static int64_t finer_tile_search_wienerns(
    RestSearchCtxt *rsc, const RestorationTileLimits *limits,
    const AV1PixelRect *tile_rect, RestorationUnitInfo *rui,
    const WienernsFilterParameters *nsfilter_params, int ext_search,
    const WienerNonsepInfoBank *ref_wienerns_bank, int class_id) {
  assert(rsc->plane == rui->plane);
  const MACROBLOCK *const x = rsc->x;
  WienerNonsepInfo curr = rui->wienerns_info;
  WienerNonsepInfo best = curr;

  int c_id_begin = class_id;
  int c_id_end = class_id + 1;
  rui->class_id_restrict = class_id;
  if (class_id == ALL_WIENERNS_CLASSES) {
    c_id_begin = 0;
    c_id_end = rui->wienerns_info.num_classes;
    rui->class_id_restrict = -1;
  }
  int64_t best_err = calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
  // When class_id != ALL_WIENERNS_CLASSES we are calculating bits for class_id
  // only since that is the filter we are changing. Should be OK since bits for
  // classes outside class_id are not needed for decisions in this fn.
  int64_t best_bits =
      count_wienerns_bits_set(rsc->plane, &x->mode_costs, &curr,
                              ref_wienerns_bank, nsfilter_params, class_id);
#else
  int64_t best_bits =
      count_wienerns_bits(rsc->plane, &x->mode_costs, &curr, ref_wienerns_bank,
                          nsfilter_params, class_id);
#endif  // CONFIG_RST_MERGECOEFFS
  double best_cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, best_bits >> 4, best_err, rsc->cm->seq_params.bit_depth);
  // printf("Err  pre = %"PRId64", cost = %f\n", best_err, best_cost);

  int is_uv = (rui->plane != AOM_PLANE_Y);
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  const int num_feat = nsfilter_params->ncoeffs;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  const int iter_step = FINER_TILE_SEARCH_WIENERNS_ITER_STEP;
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *curr_nsfilter = nsfilter_taps(&curr, c_id);
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);

    // calc_finer_tile_search_error() above sets dst. Update only parts of dst
    // relevant to c_id.
    rui->class_id_restrict = c_id;
    int src_range = 2;
    for (int s = 0; s < iter_step; ++s) {
      int no_improv = 1;
      for (int i = beg_feat; i < end_feat; ++i) {
        int cmin = MAX(curr_nsfilter[i] - src_range,
                       wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]);
        int cmax =
            MIN(curr_nsfilter[i] + src_range,
                wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID] +
                    (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]));

        for (int ci = cmin; ci < cmax; ++ci) {
          if (ci == curr_nsfilter[i]) {
            continue;
          }
          rui_wienerns_info_nsfilter[i] = ci;
          const int64_t err =
              calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
          const int64_t bits = count_wienerns_bits_set(
              rsc->plane, &x->mode_costs, &rui->wienerns_info,
              ref_wienerns_bank, nsfilter_params, c_id);
#else
          const int64_t bits = count_wienerns_bits(
              rsc->plane, &x->mode_costs, &rui->wienerns_info,
              ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
          const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
          if (cost < best_cost) {
            no_improv = 0;
            best_err = err;
            best_cost = cost;
            best_bits = bits;
            copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
          }
        }
        copy_nsfilter_taps_for_class(&curr, &best, c_id);
        rui_wienerns_info_nsfilter[i] = curr_nsfilter[i];
      }
      if (no_improv) {
        break;
      }
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      copy_nsfilter_taps_for_class(&curr, &rui->wienerns_info, c_id);
    }
    // Re-establish dst.
    if (c_id_end - c_id_begin > 1 && rui->class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }
  copy_nsfilter_taps(&rui->wienerns_info, &best);

  if (!ext_search) return best_err;

  // Try reduced filters by forcing trailing 2, 4, 6 coeffs to 0
  const int rodd = is_uv ? 0 : (end_feat & 1);
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);
    rui->class_id_restrict = c_id;
    if (rodd) {
      if (end_feat - beg_feat > 1 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
      if (end_feat - beg_feat > 3 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 3] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        rui_wienerns_info_nsfilter[end_feat - 3] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
      if (end_feat - beg_feat > 5 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 3] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 4] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 5] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        rui_wienerns_info_nsfilter[end_feat - 3] = 0;
        rui_wienerns_info_nsfilter[end_feat - 4] = 0;
        rui_wienerns_info_nsfilter[end_feat - 5] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
      if (end_feat - beg_feat > 5 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 3] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 4] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 5] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 6] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 7] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        rui_wienerns_info_nsfilter[end_feat - 3] = 0;
        rui_wienerns_info_nsfilter[end_feat - 4] = 0;
        rui_wienerns_info_nsfilter[end_feat - 5] = 0;
        rui_wienerns_info_nsfilter[end_feat - 6] = 0;
        rui_wienerns_info_nsfilter[end_feat - 7] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
    } else {
      if (end_feat - beg_feat > 2 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
      if (end_feat - beg_feat > 4 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 3] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 4] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        rui_wienerns_info_nsfilter[end_feat - 3] = 0;
        rui_wienerns_info_nsfilter[end_feat - 4] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
      if (end_feat - beg_feat > 6 &&
          (rui_wienerns_info_nsfilter[end_feat - 1] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 2] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 3] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 4] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 5] != 0 ||
           rui_wienerns_info_nsfilter[end_feat - 6] != 0)) {
        rui_wienerns_info_nsfilter[end_feat - 1] = 0;
        rui_wienerns_info_nsfilter[end_feat - 2] = 0;
        rui_wienerns_info_nsfilter[end_feat - 3] = 0;
        rui_wienerns_info_nsfilter[end_feat - 4] = 0;
        rui_wienerns_info_nsfilter[end_feat - 5] = 0;
        rui_wienerns_info_nsfilter[end_feat - 6] = 0;
        const int64_t err =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
        const int64_t bits = count_wienerns_bits_set(
            rsc->plane, &x->mode_costs, &rui->wienerns_info, ref_wienerns_bank,
            nsfilter_params, c_id);
#else
        const int64_t bits =
            count_wienerns_bits(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
        const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_err = err;
          best_cost = cost;
          best_bits = bits;
          copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
        } else {
          copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
        }
      }
    }
    // Re-establish dst.
    if (c_id_end - c_id_begin > 1 && rui->class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }
  copy_nsfilter_taps(&rui->wienerns_info, &best);
  if (ext_search == 1) return best_err;
  // printf("Err  int = %"PRId64", cost = %f\n", best_err, best_cost);

  const int src_steps[][2] = {
    { 1, -1 }, { -1, 1 }, { 1, 1 },  { -1, -1 }, { 2, 1 },   { 1, 2 },
    { -2, 1 }, { 1, -2 }, { 2, -1 }, { -1, 2 },  { -2, -1 }, { -1, -2 },
  };
  const int nsrc_steps = sizeof(src_steps) / (2 * sizeof(src_steps[0][0]));
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *rui_wienerns_info_nsfilter =
        nsfilter_taps(&rui->wienerns_info, c_id);
    int16_t *curr_nsfilter = nsfilter_taps(&curr, c_id);
    rui->class_id_restrict = c_id;
    for (int s = 0; s < iter_step; ++s) {
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
          rui_wienerns_info_nsfilter[i] = curr_nsfilter[i] + src_steps[ci][0];
          rui_wienerns_info_nsfilter[i + 1] =
              curr_nsfilter[i + 1] + src_steps[ci][1];
          if (rui_wienerns_info_nsfilter[i] < cmin[0] ||
              rui_wienerns_info_nsfilter[i] >= cmax[0] ||
              rui_wienerns_info_nsfilter[i + 1] < cmin[1] ||
              rui_wienerns_info_nsfilter[i + 1] >= cmax[1]) {
            copy_nsfilter_taps_for_class(&rui->wienerns_info, &curr, c_id);
            continue;
          }
          const int64_t err =
              calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
#if CONFIG_RST_MERGECOEFFS
          const int64_t bits = count_wienerns_bits_set(
              rsc->plane, &x->mode_costs, &rui->wienerns_info,
              ref_wienerns_bank, nsfilter_params, c_id);
#else
          const int64_t bits = count_wienerns_bits(
              rsc->plane, &x->mode_costs, &rui->wienerns_info,
              ref_wienerns_bank, nsfilter_params, c_id);
#endif  // CONFIG_RST_MERGECOEFFS
          const double cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
              x->rdmult, bits >> 4, err, rsc->cm->seq_params.bit_depth);
          if (cost < best_cost) {
            no_improv = 0;
            best_err = err;
            best_cost = cost;
            best_bits = bits;
            copy_nsfilter_taps_for_class(&best, &rui->wienerns_info, c_id);
          }
        }
        copy_nsfilter_taps_for_class(&curr, &best, c_id);
        rui_wienerns_info_nsfilter[i] = curr_nsfilter[i];
        rui_wienerns_info_nsfilter[i + 1] = curr_nsfilter[i + 1];
      }
      if (no_improv) {
        break;
      }
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      copy_nsfilter_taps_for_class(&curr, &rui->wienerns_info, c_id);
    }
    // Re-establish dst.
    if (c_id_end - c_id_begin > 1 && rui->class_id_restrict != -1) {
      copy_nsfilter_taps_for_class(&rui->wienerns_info, &best, c_id);
      calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
    }
  }

  copy_nsfilter_taps(&rui->wienerns_info, &best);

  // printf("Err post = %"PRId64", cost = %f\n", best_err, best_cost);
#if CONFIG_RST_MERGECOEFFS
  (void)count_wienerns_bits_set(rsc->plane, &x->mode_costs, &rui->wienerns_info,
                                ref_wienerns_bank, nsfilter_params, class_id);
#endif  // CONFIG_RST_MERGECOEFFS
  return best_err;
}

static int linsolve_wrapper(int n, const double *A, int stride, const double *b,
                            double *x) {
  int linsolve_successful = linsolve_const(n, A, stride, b, x);
  if (linsolve_successful) return linsolve_successful;
  // TODO: Set to a deault filter instead.
  memset(x, 0, WIENERNS_MAX * sizeof(*x));
  return 1;
}

#if USE_Q_WRAPPER
static void quantize_wrapper(int n, const double *square_mat_A, int stride,
                             const double *b, double *float_soln,
                             const WienernsFilterParameters *nsfilter_params,
                             WienerNonsepInfo *wienerns_info,
                             int max_num_iterations, int class_id) {
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  int c_id_begin = 0;
  int c_id_end = wienerns_info->num_classes;
  if (class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = class_id;
    c_id_end = class_id + 1;
  }

  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *nsfilter = nsfilter_taps(wienerns_info, c_id);
    for (int k = beg_feat; k < end_feat; ++k) {
      nsfilter[k] =
          quantize(float_soln[k - beg_feat],
                   wienerns_coeffs[k - beg_feat][WIENERNS_MIN_ID],
                   (1 << wienerns_coeffs[k - beg_feat][WIENERNS_BIT_ID]),
                   nsfilter_params->nsfilter_config.prec_bits);
    }
  }

  if (max_num_iterations <= 0) return;

  const int dim = n;
  assert(dim <= end_feat - beg_feat);

  const double tap_qstep = 1 << nsfilter_params->nsfilter_config.prec_bits;
  const double eps = 1e-10;
  double *error = (double *)aom_malloc(dim * sizeof(*error));
  double *half_normalizers = (double *)aom_malloc(dim * sizeof(*error));

  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *nsfilter = nsfilter_taps(wienerns_info, c_id);

    // Set baseline error.
    for (int row = 0; row < dim; ++row) {
      double sum = 0;
      for (int col = 0; col < dim; ++col) {
        const int tap_index = col + beg_feat;
        sum += square_mat_A[row * stride + col] * nsfilter[tap_index];
      }
      error[row] = b[row] * tap_qstep - sum;
    }

    // Set normalizers.
    for (int col = 0; col < dim; ++col) {
      double sum = 0;
      for (int row = 0; row < dim; ++row) {
        sum +=
            square_mat_A[row * stride + col] * square_mat_A[row * stride + col];
      }
      half_normalizers[col] = AOMMAX(sum, eps) / 2;
    }
#ifndef NDEBUG
    double prev_err = 1e90;
#endif
    int change = 1;
    int num_iterations = 0;
    while (change && num_iterations < max_num_iterations) {
#ifndef NDEBUG
      double err_sum = 0;
      for (int row = 0; row < dim; ++row) {
        err_sum += error[row] * error[row];
      }
      assert(err_sum <= prev_err);
      prev_err = err_sum;
#endif
      // TODO: Switch to pseudo-random traversal.
      const int offset = 1723 * num_iterations;
      ++num_iterations;
      change = 0;
      for (int k = 0; k < dim; ++k) {
        const int col = (k + offset) % dim;
        double sum = 0;
        for (int row = 0; row < dim; ++row) {
          sum += square_mat_A[row * stride + col] * error[row];
        }

        const double abs_sum = fabs(sum);
        const int tap_index = col + beg_feat;
        int updated_tap = nsfilter[tap_index];
        if (abs_sum >= half_normalizers[col]) {
          // This should be an integer division. Can also do a search for
          // abs(increment) = 0, 1, 2, ...
          const double increment = CLIP(sum / (2 * half_normalizers[col]),
                                        -MAX_INCREMENT, MAX_INCREMENT);

          // TODO: This is D only. Potentially work in bits and cost.
          updated_tap = quantize((nsfilter[tap_index] + increment) / tap_qstep,
                                 wienerns_coeffs[col][WIENERNS_MIN_ID],
                                 (1 << wienerns_coeffs[col][WIENERNS_BIT_ID]),
                                 nsfilter_params->nsfilter_config.prec_bits);
        }
        const int tap_diff = updated_tap - nsfilter[tap_index];
        if (tap_diff) {
          change += abs(tap_diff);
          // Update error.
          for (int row = 0; row < dim; ++row) {
            error[row] -= square_mat_A[row * stride + col] * tap_diff;
          }
          nsfilter[tap_index] = updated_tap;
        }
      }
    }
  }
  aom_free(half_normalizers);
  aom_free(error);
}
#endif  // USE_Q_WRAPPER

static int64_t compute_stats_for_wienerns_filter(
    const uint8_t *dgd, const uint8_t *src, const RestorationTileLimits *limits,
    int dgd_stride, int src_stride, const RestorationUnitInfo *rui,
    int bit_depth, double *A, double *b,
    const WienernsFilterParameters *nsfilter_params, int num_classes) {
  (void)rui;
  const uint16_t *src_hbd = CONVERT_TO_SHORTPTR(src);
  const uint16_t *dgd_hbd = CONVERT_TO_SHORTPTR(dgd);
#if CONFIG_WIENER_NONSEP_CROSS_FILT
  const uint16_t *luma_hbd = CONVERT_TO_SHORTPTR(rui->luma);
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT

  const int total_dim_A = num_classes * WIENERNS_MAX * WIENERNS_MAX;
  const int stride_A = WIENERNS_MAX * WIENERNS_MAX;
  const int total_dim_b = num_classes * WIENERNS_MAX;
  const int stride_b = WIENERNS_MAX;
#if CONFIG_COMBINE_PC_NS_WIENER
  const int bank_index =
      get_filter_bank_index(rui->base_qindex + rui->qindex_offset);
  const uint8_t *pc_wiener_sub_classify =
      get_pc_wiener_sub_classifier(num_classes, bank_index);
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  int16_t buf[WIENERNS_MAX];
  memset(A, 0, sizeof(*A) * total_dim_A);
  memset(b, 0, sizeof(*b) * total_dim_b);

  const int(*wienerns_config)[3] = nsfilter_params->nsfilter_config.config;
#if CONFIG_WIENER_NONSEP_CROSS_FILT
  int is_uv = (rui->plane != AOM_PLANE_Y);
  const int(*wienerns_config2)[3] =
      is_uv ? nsfilter_params->nsfilter_config.config2 : NULL;
  const int end_pixel = is_uv ? nsfilter_params->nsfilter_config.num_pixels +
                                    nsfilter_params->nsfilter_config.num_pixels2
                              : nsfilter_params->nsfilter_config.num_pixels;
#else
  const int end_pixel = nsfilter_params->nsfilter_config.num_pixels;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
  const int num_feat = nsfilter_params->ncoeffs;

  int64_t real_sse = 0;  // for debuggung purposes
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    for (int i = limits->v_start; i < limits->v_end; ++i) {
      for (int j = limits->h_start; j < limits->h_end; ++j) {
        int dgd_id = i * dgd_stride + j;
        int src_id = i * src_stride + j;
#if CONFIG_COMBINE_PC_NS_WIENER
        // TODO: This is redundant since rui->class_id is uint8 and for
        // num_classes = 1 pc_wiener_sub_classify is always 0.
        if (num_classes > 1) {
          const int full_class_id =
              rui->class_id[(i >> MI_SIZE_LOG2) * rui->class_id_stride +
                            (j >> MI_SIZE_LOG2)];
          const int sub_class_id = pc_wiener_sub_classify[full_class_id];
          if (c_id != sub_class_id) continue;
        }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_WIENER_NONSEP_CROSS_FILT
        int luma_id = i * rui->luma_stride + j;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
        memset(buf, 0, sizeof(buf));
        for (int k = 0; k < end_pixel; ++k) {
#if CONFIG_WIENER_NONSEP_CROSS_FILT
          const int cross =
              (is_uv && k >= nsfilter_params->nsfilter_config.num_pixels);
#else
          const int cross = 0;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
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
#if CONFIG_WIENER_NONSEP_CROSS_FILT
            const int k2 = k - nsfilter_params->nsfilter_config.num_pixels;
            const int pos = wienerns_config2[k2][WIENERNS_BUF_POS];
            const int r = wienerns_config2[k2][WIENERNS_ROW_ID];
            const int c = wienerns_config2[k2][WIENERNS_COL_ID];
            buf[pos] += clip_base(
                (int16_t)luma_hbd[(i + r) * rui->luma_stride + (j + c)] -
                    (int16_t)luma_hbd[luma_id],
                bit_depth);
#else
            assert(0 && "Incorrect CONFIG_WIENER_NONSEP configuration");
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
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
  const int stride_A = WIENERNS_MAX * WIENERNS_MAX;
  const int total_dim_b = num_classes * WIENERNS_MAX;
  const int stride_b = WIENERNS_MAX;

  double solver_x[WIENERNS_MAX_CLASSES * WIENERNS_MAX];
  int is_uv = (rui->plane != AOM_PLANE_Y);
  const int num_feat = nsfilter_params->ncoeffs;

  int ret = 0;
  WienerNonsepInfo best = { 0 };
  best.num_classes = num_classes;
  double best_cost = DBL_MAX;

  // double e[WIENERNS_MAX];
  const int rodd = is_uv ? 0 : (num_feat & 1);
  const int max_reduce_steps_search = 4 + rodd;
  // TODO: In order for below reduction to work well the less important taps
  //  should be at the end. Better way is to define an order of reduction in
  //  conjunction with the filter config, e.g., wienerns_config_y, and process
  //  through that.
  for (int reduce = 0; reduce <= max_reduce_steps_search;
       reduce += (reduce ? 2 : 2 - rodd)) {
    memset(solver_x, 0, sizeof(*solver_x) * total_dim_b);
    // Try a filter shape with #parameters num_feat - reduce
    int success = 0;
    int linsolve_successful = 0;
    for (int c_id = 0; c_id < num_classes; ++c_id) {
      linsolve_successful =
          linsolve_wrapper(num_feat - reduce, A + c_id * stride_A, num_feat,
                           b + c_id * stride_b, solver_x + c_id * stride_b);
      if (!linsolve_successful) break;
    }
    if (num_feat > reduce && linsolve_successful) {
      // double err = (double)real_sse;
      // for (int k = 0; k < num_feat; ++k) err -= x[k] * b[k];
      do {
        // if ((int64_t)err > real_sse) break;
#if USE_Q_WRAPPER
        for (int c_id = 0; c_id < num_classes; ++c_id) {
          quantize_wrapper(num_feat - reduce, A + c_id * stride_A, num_feat,
                           b + c_id * stride_b, solver_x + c_id * stride_b,
                           nsfilter_params, &rui->wienerns_info,
                           Q_WRAPPER_MAX_ITER, c_id);
        }
#else
        const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] =
            nsfilter_params->coeffs;
        const int prec_bits = nsfilter_params->nsfilter_config.prec_bits;
        const int beg_feat = 0;
        const int end_feat = nsfilter_params->ncoeffs;
        for (int c_id = 0; c_id < num_classes; ++c_id) {
          int16_t *rui_wienerns_info_nsfilter =
              nsfilter_taps(&rui->wienerns_info, c_id);
          for (int k = beg_feat; k < end_feat; ++k) {
            rui_wienerns_info_nsfilter[k] =
                quantize(solver_x[k - beg_feat + c_id * stride_b],
                         wienerns_coeffs[k - beg_feat][WIENERNS_MIN_ID],
                         (1 << wienerns_coeffs[k - beg_feat][WIENERNS_BIT_ID]),
                         prec_bits);
            // e[k - beg_feat] =
            //     x[k - beg_feat] -
            //     (double)rui_wienerns_info_nsfilter[k] / (1 <<
            //     prec_bits);
          }
        }
#endif  // USE_Q_WRAPPER
        // double errq = err + eval_quadratic(num_feat, A, num_feat, e);
        assert(rui->class_id_restrict == -1);
        int64_t real_errq =
            calc_finer_tile_search_error(rsc, limits, tile_rect, rui);
        // NOTE: replace with:
        // int64_t real_errq =
        //     finer_tile_search_wienerns(rsc, limits, tile_rect, rui, wnsf, 0);
        // for better results at the expense of higger encoder complexity.

        // Found filter is worse than no filtering.
        if (real_errq > real_sse) break;
#if CONFIG_RST_MERGECOEFFS
        int64_t bits = count_wienerns_bits_set(
            rui->plane, &rsc->x->mode_costs, &rui->wienerns_info,
            &rsc->wienerns_bank, nsfilter_params, ALL_WIENERNS_CLASSES);
#else
        int64_t bits = count_wienerns_bits(
            rui->plane, &rsc->x->mode_costs, &rui->wienerns_info,
            &rsc->wienerns_bank, nsfilter_params, ALL_WIENERNS_CLASSES);
#endif  // CONFIG_RST_MERGECOEFFS
        double cost =
            RDCOST_DBL_WITH_NATIVE_BD_DIST(rsc->x->rdmult, bits >> 4, real_errq,
                                           rsc->cm->seq_params.bit_depth);
        if (cost < best_cost) {
          best_cost = cost;
          copy_nsfilter_taps(&best, &rui->wienerns_info);
          success = 1;
          ret = 1;
        } else {
          copy_nsfilter_taps(&rui->wienerns_info, &best);
        }
      } while (0);
      if (ret && !success) break;
    }
  }
  if (ret) {
    copy_nsfilter_taps(&rui->wienerns_info, &best);
  }
  return ret;
}

#if CONFIG_RST_MERGECOEFFS

// TODO: This routine could also populate_current_unit_indices.
int get_merge_begin_index(const RestSearchCtxt *rsc,
                          const WienernsFilterParameters *nsfilter_params,
                          const WienerNonsepInfo *token_wienerns_info,
                          Vector *current_unit_stack,
                          WienerNonsepInfoBank **begin_bank, int class_id) {
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
                          nsfilter_params->ncoeffs, class_id)) {
      // Same filter as before.
      if (check_wienerns_bank_eq(&old_unit->ref_wienerns_bank,
                                 token_wienerns_info, nsfilter_params->ncoeffs,
                                 class_id, equal_ref_for_class) == -1) {
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
    Vector *current_unit_stack, Vector *current_unit_indices, int class_id) {
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
                           nsfilter_params->ncoeffs, class_id))
      continue;
    int index = old_unit->rest_unit_idx;
    aom_vector_push_back(current_unit_indices, &index);
  }
}

// TODO: Using token_wienerns_info_cand to navigate through the vector is
//  awkward. Once current_unit_indices are populated we should always use that
//  for navigation.
double set_cand_merge_sse_and_bits(
    RestSearchCtxt *rsc, const WienernsFilterParameters *nsfilter_params,
    const AV1PixelRect *tile_rect, int begin_idx_cand,
    Vector *current_unit_stack, WienerNonsepInfo *token_wienerns_info_cand,
    RestorationUnitInfo *rui_merge_cand, int class_id) {
  const int last_idx =
      ((RstUnitSnapshot *)aom_vector_back(current_unit_stack))->rest_unit_idx;
  const int is_uv = (rsc->plane != AOM_PLANE_Y);
  const MACROBLOCK *const x = rsc->x;
  const int bit_depth = rsc->cm->seq_params.bit_depth;

  double cost_merge_cand = 0;
  int equal_ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  rui_merge_cand->class_id_restrict = class_id;
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
                           nsfilter_params->ncoeffs, class_id))
      continue;

    old_unit->merge_sse_cand =
        try_restoration_unit(rsc, &old_unit->limits, tile_rect, rui_merge_cand);
    // First unit in stack has larger unit_bits because the
    // merged coeffs are linked to it.
    if (old_unit->rest_unit_idx == begin_idx_cand) {
      // The first unit will have a different filter
      // (rui_merge_cand->wienerns_info) to signal at class_id, same filters
      // elsewhere.
      WienerNonsepInfo tmp_filters = old_rusi->wienerns_info;
      copy_nsfilter_taps_for_class(&tmp_filters, &rui_merge_cand->wienerns_info,
                                   class_id);
      const int new_bits = (int)count_wienerns_bits_set(
          is_uv, &x->mode_costs, &tmp_filters, &old_unit->ref_wienerns_bank,
          nsfilter_params, ALL_WIENERNS_CLASSES);
      old_unit->merge_bits_cand =
          x->mode_costs.wienerns_restore_cost[1] + new_bits;
    } else if (old_unit->rest_unit_idx != last_idx) {
      const int is_equal = check_wienerns_bank_eq(
          &old_unit->ref_wienerns_bank, token_wienerns_info_cand,
          nsfilter_params->ncoeffs, class_id, equal_ref_for_class);
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
          nsfilter_params->ncoeffs, class_id, equal_ref_for_class);
      assert(is_equal >= 0);  // Must exist in bank

      // token_wienerns_info_cand has the best filters for classes < class_id
      // and the token filter at class_id. Remaining filters are the computed RU
      // filters that have not entered the merge trial.
      token_wienerns_info_cand->bank_ref_for_class[class_id] =
          equal_ref_for_class[class_id];

      // TODO: Merge bits calculated this way is not entirely correct since we
      //  don't know the optimal merge status for classes > class_id.
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
                              int class_id) {
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
                           nsfilter_params->ncoeffs, class_id))
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

#endif  // CONFIG_RST_MERGECOEFFS

static void gather_stats_wienerns_visitor(const RestorationTileLimits *limits,
                                          const AV1PixelRect *tile_rect,
                                          int rest_unit_idx,
                                          int rest_unit_idx_seq, void *priv,
                                          int32_t *tmpbuf,
                                          RestorationLineBuffers *rlbs) {
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;
  (void)tile_rect;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;

  RestorationUnitInfo rui;
  memset(&rui, 0, sizeof(rui));
  rui.restoration_type = RESTORE_WIENER_NONSEP;
  rui.class_id_restrict = -1;
#if CONFIG_COMBINE_PC_NS_WIENER
  rui.compute_classification = 0;
  if (rsc->plane == AOM_PLANE_Y || PC_WIENER_FILTER_CHROMA ||
      PC_WIENER_ONLY_CLASSIFY_CHROMA) {
    // Ensure search_pc_wiener was done and classification was computed.
    assert(rsc->is_buffered == true);
  } else {
    assert(rsc->is_buffered == false);
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_PC_WIENER
  rui.class_id = rsc->cm->mi_params.class_id[rsc->plane];
  rui.class_id_stride = rsc->cm->mi_params.class_id_stride[rsc->plane];
  // These are not needed since class_id is already computed. Add them to avoid
  // NULLs etc. during debug and other uses.
  rui.tskip = rsc->cm->mi_params.tx_skip[rsc->plane];
  rui.tskip_stride = rsc->cm->mi_params.tx_skip_stride[rsc->plane];
  rui.base_qindex = rsc->cm->quant_params.base_qindex;
  if (rsc->plane != AOM_PLANE_Y)
    rui.qindex_offset = rsc->plane == AOM_PLANE_U
                            ? rsc->cm->quant_params.u_dc_delta_q
                            : rsc->cm->quant_params.v_dc_delta_q;
  else
    rui.qindex_offset = rsc->cm->quant_params.y_dc_delta_q;
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP_CROSS_FILT
  rui.luma = rsc->luma;
  rui.luma_stride = rsc->luma_stride;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
  rui.plane = rsc->plane;
  rui.base_qindex = rsc->cm->quant_params.base_qindex;
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);

  const int num_classes = rsc->num_filter_classes;
  assert(num_classes == rsc->wienerns_bank.filter[0].num_classes);
  rui.wienerns_info.num_classes = num_classes;
  // Calculate and save this RU's stats.
  RstUnitStats unit_stats;
  unit_stats.real_sse = compute_stats_for_wienerns_filter(
      rsc->dgd_buffer, rsc->src_buffer, limits, rsc->dgd_stride,
      rsc->src_stride, &rui, rsc->cm->seq_params.bit_depth, unit_stats.A,
      unit_stats.b, nsfilter_params, rsc->num_stat_classes);
  unit_stats.ru_idx = rest_unit_idx;
  unit_stats.ru_idx_in_tile = rest_unit_idx_seq - rsc->ru_idx_base;
  unit_stats.plane = rsc->plane;
  unit_stats.num_stats_classes = rsc->num_stat_classes;
  aom_vector_push_back(rsc->wienerns_stats, &unit_stats);
  return;
}

static void search_wienerns_visitor(const RestorationTileLimits *limits,
                                    const AV1PixelRect *tile_rect,
                                    int rest_unit_idx, int rest_unit_idx_seq,
                                    void *priv, int32_t *tmpbuf,
                                    RestorationLineBuffers *rlbs) {
  (void)tile_rect;
  (void)tmpbuf;
  (void)rlbs;
  (void)rest_unit_idx_seq;

  RestSearchCtxt *rsc = (RestSearchCtxt *)priv;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  const MACROBLOCK *const x = rsc->x;
  const int64_t bits_none = x->mode_costs.wienerns_restore_cost[0];
  const int bit_depth = rsc->cm->seq_params.bit_depth;
  double cost_none = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_none >> 4, rusi->sse[RESTORE_NONE], bit_depth);
  RestorationUnitInfo rui;
  memset(&rui, 0, sizeof(rui));
  rui.restoration_type = RESTORE_WIENER_NONSEP;
  rui.class_id_restrict = -1;
#if CONFIG_COMBINE_PC_NS_WIENER
  rui.compute_classification = 0;
  if (rsc->plane == AOM_PLANE_Y || PC_WIENER_FILTER_CHROMA ||
      PC_WIENER_ONLY_CLASSIFY_CHROMA) {
    // Ensure search_pc_wiener was done and classification was computed.
    assert(rsc->is_buffered == true);
  } else {
    assert(rsc->is_buffered == false);
  }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_PC_WIENER
  rui.class_id = rsc->cm->mi_params.class_id[rsc->plane];
  rui.class_id_stride = rsc->cm->mi_params.class_id_stride[rsc->plane];
  // These are not needed since class_id is already computed. Add them to avoid
  // NULLs etc. during debug and other uses.
  rui.tskip = rsc->cm->mi_params.tx_skip[rsc->plane];
  rui.tskip_stride = rsc->cm->mi_params.tx_skip_stride[rsc->plane];
  rui.base_qindex = rsc->cm->quant_params.base_qindex;
  if (rsc->plane != AOM_PLANE_Y)
    rui.qindex_offset = rsc->plane == AOM_PLANE_U
                            ? rsc->cm->quant_params.u_dc_delta_q
                            : rsc->cm->quant_params.v_dc_delta_q;
  else
    rui.qindex_offset = rsc->cm->quant_params.y_dc_delta_q;
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP_CROSS_FILT
  rui.luma = rsc->luma;
  rui.luma_stride = rsc->luma_stride;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
  rui.plane = rsc->plane;
  rui.base_qindex = rsc->cm->quant_params.base_qindex;
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);

  const int num_classes = rsc->num_filter_classes;
  assert(num_classes == rsc->wienerns_bank.filter[0].num_classes);
  rui.wienerns_info.num_classes = num_classes;

  const RstUnitStats *unit_stats = (const RstUnitStats *)aom_vector_const_get(
      rsc->wienerns_stats, rest_unit_idx_seq);
  assert(unit_stats->ru_idx == rest_unit_idx);
  assert(unit_stats->ru_idx_in_tile + rsc->ru_idx_base == rest_unit_idx_seq);
  assert(unit_stats->plane == rsc->plane);

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
  // NOTE: replace with:
  //  calc_finer_tile_search_error(rsc, limits, &rsc->tile_rect, &rui);
  //  if finer search was already done in compute_quantized_wienerns_filter()
  rusi->wienerns_info = rui.wienerns_info;
  assert(rusi->sse[RESTORE_WIENER_NONSEP] != INT64_MAX);

#if CONFIG_RST_MERGECOEFFS
  // TODO(oguleryuz): RUs that don't have a certain class_id should match
  //  others that do.
  if (num_classes > 1) {
    rui.class_id_restrict = -1;
    calc_finer_tile_search_error(rsc, limits, &rsc->tile_rect, &rui);
  }
  double solver_A_AVG[WIENERNS_MAX * WIENERNS_MAX];
  const int class_dim_A = WIENERNS_MAX * WIENERNS_MAX;
  double solver_b_AVG[WIENERNS_MAX];
  const int class_dim_b = WIENERNS_MAX;
  double solver_merge_filter_stats[WIENERNS_MAX];

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
    // TODO: Why is this needed? We did set above.
    memcpy(rusi->wienerns_info.bank_ref_for_class, equal_ref_for_class,
           rusi->wienerns_info.num_classes * (*equal_ref_for_class));
    unit_snapshot.current_bits =
        x->mode_costs.wienerns_restore_cost[1] +
        count_wienerns_bits_set(is_uv, &x->mode_costs, &rusi->wienerns_info,
                                &rsc->wienerns_bank, nsfilter_params,
                                ALL_WIENERNS_CLASSES);
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
      copy_nsfilter_taps(&token_wienerns_info_cand,
                         av1_constref_from_wienerns_bank(&rsc->wienerns_bank,
                                                         bank_ref_cand, c_id));
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
             class_dim_A * sizeof(*unit_stats->A));

      const int offset_b = c_id * class_dim_b;
      memcpy(solver_b_AVG, unit_stats->b + offset_b,
             class_dim_b * sizeof(*unit_stats->b));

      // Get current cost and the average of A and b.
      cost_nomerge_cand += accumulate_merge_stats(
          rsc, nsfilter_params, &token_wienerns_info_cand, begin_idx_cand,
          current_unit_stack, current_unit_indices, solver_A_AVG, solver_b_AVG,
          class_dim_A, class_dim_b, offset_A, offset_b, c_id);

      // Generate new filter.
      RestorationUnitInfo rui_merge_cand = rui_merge_best;
      rui_merge_cand.restoration_type = RESTORE_WIENER_NONSEP;

      const int num_feat = nsfilter_params->ncoeffs;
      int linsolve_successful =
          linsolve_wrapper(num_feat, solver_A_AVG, num_feat, solver_b_AVG,
                           solver_merge_filter_stats);
      if (linsolve_successful) {
#if USE_Q_WRAPPER
        quantize_wrapper(num_feat, solver_A_AVG, num_feat, solver_b_AVG,
                         solver_merge_filter_stats, nsfilter_params,
                         &rui_merge_cand.wienerns_info, Q_WRAPPER_MAX_ITER,
                         c_id);
#else
        const int beg_feat = 0;
        const int end_feat = nsfilter_params->ncoeffs;
        const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] =
            nsfilter_params->coeffs;

        int16_t *rui_merge_cand_wienerns_info_nsfilter =
            nsfilter_taps(&rui_merge_cand.wienerns_info, c_id);
        for (int k = beg_feat; k < end_feat; ++k) {
          rui_merge_cand_wienerns_info_nsfilter[k] =
              quantize(solver_merge_filter_stats[k - beg_feat],
                       wienerns_coeffs[k - beg_feat][WIENERNS_MIN_ID],
                       (1 << wienerns_coeffs[k - beg_feat][WIENERNS_BIT_ID]),
                       nsfilter_params->nsfilter_config.prec_bits);
        }
#endif  // USE_Q_WRAPPER
      } else {
        continue;
      }

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
      // TODO: Why not add && cost_merge_cand < cost_nomerge_cand?
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
      // TODO: Only reset if this is the last trial or the next trial is for a
      //  different c_id.
      if (num_classes > 1 &&
          (begin_idx[c_id] != begin_idx_cand || cost_merge >= cost_nomerge)) {
        // We will not be merging this trial even if it is the best cand. Reset
        // rsc buffers to the best solution so far. Re-establish dst.
        rui_merge_best.class_id_restrict = c_id;

        // TODO(oguleryuz): Potentially change restoration to apply zero filter
        //  to non-matching classes.
        reset_unit_stack_dst_buffers(rsc, NULL, &rsc->tile_rect,
                                     &rui_merge_best);
      }
      aom_vector_clear(current_unit_indices);
    }
    // Trial end

    RstUnitSnapshot *last_unit = aom_vector_back(current_unit_stack);
    RestUnitSearchInfo *last_rusi = &rsc->rusi[last_unit->rest_unit_idx];
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
          // TODO: Instead of this call, push merge leader's bank_ref into rui
          //  or elsewhere as a special var within set_cand_merge_sse_and_bits()
          //  and set old_rusi->wienerns_info.bank_ref_for_class[c_id] here with
          //  it.
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
      // TODO: Is this needed? Why not just copy last_unit->ref_wienerns_bank?
      const int is_equal = check_wienerns_bank_eq(
          &last_unit->ref_wienerns_bank, &rui_merge_best.wienerns_info,
          nsfilter_params->ncoeffs, c_id, equal_ref_for_class);
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
  /*
     printf("wienerns(%d) [merge %f < nomerge %f] : %d, bank_size %d\n",
     rsc->plane, cost_merge, cost_nomerge, (cost_merge < cost_nomerge),
     rsc->wienerns_bank.bank_size);
     */
#else   // CONFIG_RST_MERGECOEFFS
  const int64_t bits_wienerns =
      x->mode_costs.wienerns_restore_cost[1] +
      count_wienerns_bits(rui.plane, &x->mode_costs, &rusi->wienerns_info,
                          &rsc->wienerns_bank, nsfilter_params,
                          ALL_WIENERNS_CLASSES);
  double cost_wienerns = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      x->rdmult, bits_wienerns >> 4, rusi->sse[RESTORE_WIENER_NONSEP],
      bit_depth);
  const RestorationType rtype =
      (cost_wienerns < cost_none) ? RESTORE_WIENER_NONSEP : RESTORE_NONE;
  rusi->best_rtype[RESTORE_WIENER_NONSEP - 1] = rtype;
  rsc->sse += rusi->sse[rtype];
  rsc->bits += (cost_wienerns < cost_none) ? bits_wienerns : bits_none;
  if (cost_wienerns < cost_none)
    av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                             ALL_WIENERNS_CLASSES);
    /*
       printf("[%d] none: %"PRId64"/%"PRId64"/%f; wns:
       %"PRId64"/%"PRId64"/%f\n", x->rdmult, rusi->sse[RESTORE_NONE], bits_none,
       cost_none, rusi->sse[RESTORE_WIENER_NONSEP], bits_wienerns,
       cost_wienerns);
       */
#endif  // CONFIG_RST_MERGECOEFFS
}
#endif  // CONFIG_WIENER_NONSEP

static int get_switchable_restore_cost(const AV1_COMMON *const cm,
                                       const MACROBLOCK *const x, int plane,
                                       int rest_type) {
  (void)cm;
  (void)plane;
#if CONFIG_LR_FLEX_SYNTAX
  int cost = 0;
  for (int re = 0; re <= cm->features.lr_last_switchable_ndx[plane]; re++) {
    if (cm->features.lr_tools_disable_mask[plane] & (1 << re)) continue;
    const int found = (re == rest_type);
    cost += x->mode_costs.switchable_flex_restore_cost[re][plane][found];
    if (found) break;
  }
  return cost;
#else
  return x->mode_costs.switchable_restore_cost[rest_type];
#endif  // CONFIG_LR_FLEX_SYNTAX
}

static int64_t count_switchable_bits(int rest_type, RestSearchCtxt *rsc,
                                     RestUnitSearchInfo *rusi) {
  const MACROBLOCK *const x = rsc->x;
#if CONFIG_WIENER_NONSEP
  const WienernsFilterParameters *nsfilter_params = get_wienerns_parameters(
      rsc->cm->quant_params.base_qindex, rsc->plane != AOM_PLANE_Y);
#endif  // CONFIG_WIENER_NONSEP
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
#if CONFIG_RST_MERGECOEFFS
      coeff_bits = count_wiener_bits_set(wiener_win, &x->mode_costs,
                                         &rusi->wiener_info, &rsc->wiener_bank);
#else
      coeff_bits = count_wiener_bits(wiener_win, &x->mode_costs,
                                     &rusi->wiener_info, &rsc->wiener_bank);
#endif  // CONFIG_RST_MERGECOEFFS
      break;
    case RESTORE_SGRPROJ:
#if CONFIG_RST_MERGECOEFFS
      coeff_bits = count_sgrproj_bits_set(&x->mode_costs, &rusi->sgrproj_info,
                                          &rsc->sgrproj_bank);
#else
      coeff_bits = count_sgrproj_bits(&x->mode_costs, &rusi->sgrproj_info,
                                      &rsc->sgrproj_bank);
#endif  // CONFIG_RST_MERGECOEFFS
      break;
#if CONFIG_WIENER_NONSEP
    case RESTORE_WIENER_NONSEP:
#if CONFIG_RST_MERGECOEFFS
      coeff_bits = count_wienerns_bits_set(
          rsc->plane, &x->mode_costs, &rusi->wienerns_info, &rsc->wienerns_bank,
          nsfilter_params, ALL_WIENERNS_CLASSES);
#else
      coeff_bits = count_wienerns_bits(
          rsc->plane, &x->mode_costs, &rusi->wienerns_info, &rsc->wienerns_bank,
          nsfilter_params, ALL_WIENERNS_CLASSES);
#endif  // CONFIG_RST_MERGECOEFFS
      break;
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
    case RESTORE_PC_WIENER:
      // No side-information for now.
      coeff_bits = 0;
      break;
#endif  // CONFIG_PC_WIENER
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

  const MACROBLOCK *const x = rsc->x;
  RestUnitSearchInfo *rusi = &rsc->rusi[rest_unit_idx];

  double best_cost = DBL_MAX;
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
#if CONFIG_LR_FLEX_SYNTAX
    if (rsc->cm->features.lr_tools_disable_mask[rsc->plane] & (1 << r))
      continue;
#endif  // CONFIG_LR_FLEX_SYNTAX
#if CONFIG_PC_WIENER
    if (rsc->plane != AOM_PLANE_Y && r == RESTORE_PC_WIENER) continue;
#endif  // CONFIG_PC_WIENER

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
#if CONFIG_RST_MERGECOEFFS
    const int equal_ref =
        check_wiener_bank_eq(&rsc->wiener_bank, &rusi->wiener_info);
    if (equal_ref == -1 || rsc->wiener_bank.bank_size == 0)
      av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
#else
    av1_add_to_wiener_bank(&rsc->wiener_bank, &rusi->wiener_info);
#endif  // CONFIG_RST_MERGECOEFFS
  } else if (best_rtype == RESTORE_SGRPROJ) {
#if CONFIG_RST_MERGECOEFFS
    const int equal_ref =
        check_sgrproj_bank_eq(&rsc->sgrproj_bank, &rusi->sgrproj_info);
    if (equal_ref == -1 || rsc->sgrproj_bank.bank_size == 0)
      av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
#else
    av1_add_to_sgrproj_bank(&rsc->sgrproj_bank, &rusi->sgrproj_info);
#endif  // CONFIG_RST_MERGECOEFFS
#if CONFIG_WIENER_NONSEP
  } else if (best_rtype == RESTORE_WIENER_NONSEP) {
#if CONFIG_RST_MERGECOEFFS
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
#else
    av1_add_to_wienerns_bank(&rsc->wienerns_bank, &rusi->wienerns_info,
                             ALL_WIENERNS_CLASSES);
#endif  // CONFIG_RST_MERGECOEFFS
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  } else if (best_rtype == RESTORE_PC_WIENER) {
    // No side-information for now.
#endif  // CONFIG_PC_WIENER
  }
}

static AOM_INLINE void copy_unit_info(RestorationType frame_rtype,
                                      const RestUnitSearchInfo *rusi,
                                      RestorationUnitInfo *rui,
                                      RestSearchCtxt *rsc) {
#if CONFIG_RST_MERGECOEFFS
  const ModeCosts *mode_costs = &rsc->x->mode_costs;
#else
  (void)rsc;
#endif  // CONFIG_RST_MERGECOEFFS
  rui->restoration_type = frame_rtype == RESTORE_NONE
                              ? RESTORE_NONE
                              : rusi->best_rtype[frame_rtype - 1];
  if (rui->restoration_type == RESTORE_WIENER) {
    rui->wiener_info = rusi->wiener_info;
#if CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_RST_MERGECOEFFS
#if CONFIG_WIENER_NONSEP
  } else if (rui->restoration_type == RESTORE_WIENER_NONSEP) {
    rui->wienerns_info = rusi->wienerns_info;
#if CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_RST_MERGECOEFFS
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  } else if (rui->restoration_type == RESTORE_PC_WIENER) {
    // No side-information for now.
#endif  // CONFIG_PC_WIENER
  } else if (rui->restoration_type == RESTORE_SGRPROJ) {
    rui->sgrproj_info = rusi->sgrproj_info;
#if CONFIG_RST_MERGECOEFFS
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
#endif  // CONFIG_RST_MERGECOEFFS
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
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = {
    NULL,
    NULL,
    NULL,
#if CONFIG_PC_WIENER
    NULL,
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
    gather_stats_wienerns_visitor,
#endif  // CONFIG_WIENER_NONSEP
    NULL
  };
  // TODO(oguleryuz): Move to reset_rsc_stats().
#if CONFIG_WIENER_NONSEP
  if (rtype == RESTORE_WIENER_NONSEP) aom_vector_clear(rsc->wienerns_stats);
#endif  // CONFIG_WIENER_NONSEP
  if (funs[rtype]) process_by_rutile(rsc, funs[rtype]);
}

static double search_rest_type(RestSearchCtxt *rsc, RestorationType rtype) {
  static const rest_unit_visitor_t funs[RESTORE_TYPES] = {
    search_norestore_visitor,
    search_wiener_visitor,
    search_sgrproj_visitor,
#if CONFIG_PC_WIENER
    search_pc_wiener_visitor,
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
    search_wienerns_visitor,
#endif  // CONFIG_WIENER_NONSEP
    search_switchable_visitor
  };
  if (funs[rtype])
    return process_rd_by_rutile(rsc, funs[rtype]);
  else
    return DBL_MAX;
}

static void adjust_frame_rtype(RestorationInfo *rsi, int plane_ntiles,
                               RestSearchCtxt *rsc, const ToolCfg *tool_cfg) {
  (void)rsc;
  (void)tool_cfg;
#if CONFIG_LR_FLEX_SYNTAX
  rsi->sw_lr_tools_disable_mask = 0;
  uint8_t sw_lr_tools_disable_mask = 0;
#endif  // CONFIG_LR_FLEX_SYNTAX
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
#if CONFIG_LR_FLEX_SYNTAX
      assert((rsc->cm->features.lr_tools_disable_mask[rsc->plane] & (1 << j)) ==
             0);
    } else {
      sw_lr_tools_disable_mask |= (1 << j);
#else
      assert(IMPLIES(j == RESTORE_WIENER, tool_cfg->enable_wiener));
      assert(IMPLIES(j == RESTORE_SGRPROJ, tool_cfg->enable_sgrproj));
#if CONFIG_PC_WIENER
      assert(IMPLIES(j == RESTORE_PC_WIENER, tool_cfg->enable_pc_wiener));
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
      assert(IMPLIES(j == RESTORE_WIENER_NONSEP, tool_cfg->enable_wienerns));
#endif  // CONFIG_WIENER_NONSEP
#endif  // CONFIG_LR_FLEX_SYNTAX
    }
  }
  rsi->frame_restoration_type = ntools < 2 ? rused : RESTORE_SWITCHABLE;
#if CONFIG_WIENER_NONSEP
  rsi->num_filter_classes = rsc->num_filter_classes;
#endif
#if CONFIG_LR_FLEX_SYNTAX
  if (rsi->frame_restoration_type == RESTORE_SWITCHABLE &&
      rsc->cm->features.lr_tools_count[rsc->plane] > 2) {
    rsi->sw_lr_tools_disable_mask = sw_lr_tools_disable_mask;
  }
#endif  // CONFIG_LR_FLEX_SYNTAX
  return;
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

#if CONFIG_WIENER_NONSEP
// Reduces the class granularity of the stats to target_classes.
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
  collapsed_unit_stats.num_stats_classes = target_classes;

  const int dim_per_class_A = WIENERNS_MAX * WIENERNS_MAX;
  const int dim_per_class_b = WIENERNS_MAX;
  for (int c_id = 0; c_id < unit_stats->num_stats_classes; ++c_id) {
    const int tc_id = class_converter[c_id];
    for (int i = 0; i < dim_per_class_A; ++i) {
      collapsed_unit_stats.A[tc_id * dim_per_class_A + i] +=
          unit_stats->A[c_id * dim_per_class_A + i];
    }
    for (int i = 0; i < dim_per_class_b; ++i) {
      collapsed_unit_stats.b[tc_id * dim_per_class_b + i] +=
          unit_stats->b[c_id * dim_per_class_b + i];
    }
  }
  *unit_stats = collapsed_unit_stats;
}

static void find_optimal_num_classes_and_filters(RestSearchCtxt *rsc) {
  // TODO(oguleryuz): Fill this routine.
  rsc->num_filter_classes = rsc->plane == AOM_PLANE_Y
                                ? NUM_WIENERNS_CLASS_INIT_LUMA
                                : NUM_WIENERNS_CLASS_INIT_CHROMA;
}

// Initial set of stats are determined using rsc->num_stat_classes classes.
// After optimization the final class number is rsc->num_filter_classes. This
// routine collapses the initial set of stats to rsc->num_filter_classes for
// downstream use.
static void collapse_all_stats(RestSearchCtxt *rsc) {
  const int num_stats_classes = rsc->num_stat_classes;
  const int target_classes = rsc->num_filter_classes;

  int qindex_offset = 0;
  if (rsc->plane != AOM_PLANE_Y)
    qindex_offset = rsc->plane == AOM_PLANE_U
                        ? rsc->cm->quant_params.u_dc_delta_q
                        : rsc->cm->quant_params.v_dc_delta_q;
  else
    qindex_offset = rsc->cm->quant_params.y_dc_delta_q;
  const int bank_index =
      get_filter_bank_index(rsc->cm->quant_params.base_qindex + qindex_offset);
  const uint8_t *class_converter =
      get_converter(bank_index, num_stats_classes, target_classes);
  VECTOR_FOR_EACH(rsc->wienerns_stats, unit_stats) {
    collapse_stats_to_target_classes(target_classes, class_converter,
                                     (RstUnitStats *)(unit_stats.pointer));
  }
}

#endif  // CONFIG_WIENER_NONSEP

void av1_pick_filter_restoration(const YV12_BUFFER_CONFIG *src, AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  const int num_planes = av1_num_planes(cm);
  assert(!cm->features.all_lossless);

  av1_fill_lr_rates(&x->mode_costs, x->e_mbd.tile_ctx,
                    cm->quant_params.base_qindex);

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

#if CONFIG_RST_MERGECOEFFS
  Vector unit_stack;
  aom_vector_setup(&unit_stack,
                   1,                                // resizable capacity
                   sizeof(struct RstUnitSnapshot));  // element size
  Vector unit_indices;
  aom_vector_setup(&unit_indices,
                   1,             // resizable capacity
                   sizeof(int));  // element size
#endif                            // CONFIG_RST_MERGECOEFFS

  RestSearchCtxt rsc;
  const int plane_start = AOM_PLANE_Y;
  const int plane_end = num_planes > 1 ? AOM_PLANE_V : AOM_PLANE_Y;

#if CONFIG_WIENER_NONSEP
  Vector wienerns_stats;
  aom_vector_setup(&wienerns_stats,
                   1,                             // resizable capacity
                   sizeof(struct RstUnitStats));  // element size
  rsc.wienerns_stats = &wienerns_stats;

#if CONFIG_WIENER_NONSEP_CROSS_FILT
  uint8_t *luma = NULL;
  uint16_t *luma_buf;
  const YV12_BUFFER_CONFIG *dgd = &cpi->common.cur_frame->buf;
  rsc.luma_stride = dgd->crop_widths[1] + 2 * WIENERNS_UV_BRD;
  luma_buf = wienerns_copy_luma_highbd(
      dgd->buffers[AOM_PLANE_Y], dgd->crop_heights[AOM_PLANE_Y],
      dgd->crop_widths[AOM_PLANE_Y], dgd->strides[AOM_PLANE_Y], &luma,
      dgd->crop_heights[1], dgd->crop_widths[1], WIENERNS_UV_BRD,
      rsc.luma_stride, cm->seq_params.bit_depth);
  assert(luma_buf != NULL);
  rsc.luma = luma;
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT
#endif  // CONFIG_WIENER_NONSEP

  for (int plane = plane_start; plane <= plane_end; ++plane) {
    init_rsc(src, &cpi->common, x, &cpi->sf.lpf_sf, plane, rusi,
             &cpi->trial_frame_rst,
#if CONFIG_RST_MERGECOEFFS
             &unit_stack, &unit_indices,
#endif  // CONFIG_RST_MERGECOEFFS
             &rsc);

    const int plane_ntiles = ntiles[plane > 0];
    const RestorationType num_rtypes =
        (plane_ntiles > 1) ? RESTORE_TYPES : RESTORE_SWITCHABLE_TYPES;

    double best_cost = 0;
    RestorationType best_rtype = RESTORE_NONE;

    if (!(cpi->sf.lpf_sf.disable_loop_restoration_chroma && plane)) {
      av1_extend_frame(rsc.dgd_buffer, rsc.plane_width, rsc.plane_height,
                       rsc.dgd_stride, RESTORATION_BORDER, RESTORATION_BORDER);

      for (RestorationType r = 0; r < num_rtypes; ++r) {
#if CONFIG_LR_FLEX_SYNTAX
        if (cpi->common.features.lr_tools_disable_mask[plane > 0] & (1 << r))
          continue;
#else
        const ToolCfg *const tool_cfg = &cpi->oxcf.tool_cfg;
        switch (r) {
          case RESTORE_WIENER:
            if (!tool_cfg->enable_wiener) continue;
            break;
          case RESTORE_SGRPROJ:
            if (!tool_cfg->enable_sgrproj) continue;
            break;
#if CONFIG_PC_WIENER
          case RESTORE_PC_WIENER:
            if (!tool_cfg->enable_pc_wiener) continue;
            break;
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
          case RESTORE_WIENER_NONSEP:
            if (!tool_cfg->enable_wienerns) continue;
            break;
#endif  // CONFIG_WIENER_NONSEP
          default: break;
        };
#endif  // CONFIG_LR_FLEX_SYNTAX

#if CONFIG_PC_WIENER
        // TODO: Redundant search_pc_wiener will skip search for this case.
        if (plane != AOM_PLANE_Y && r == RESTORE_PC_WIENER &&
            !PC_WIENER_FILTER_CHROMA && !PC_WIENER_ONLY_CLASSIFY_CHROMA)
          continue;
#endif  // CONFIG_PC_WIENER

        gather_stats_rest_type(&rsc, r);
#if CONFIG_WIENER_NONSEP
        if (r == RESTORE_WIENER_NONSEP) {
          // Find RDO-num_classes and frame-level filters.
          find_optimal_num_classes_and_filters(&rsc);

          // Collapse stats to resulting num_classes;
          collapse_all_stats(&rsc);
        }
#endif  // CONFIG_WIENER_NONSEP
        double cost = search_rest_type(&rsc, r);
        // printf("Plane[%d] r[%d]: cost %f\n", plane, r, cost);
#if CONFIG_COMBINE_PC_NS_WIENER
        assert(RESTORE_PC_WIENER < RESTORE_WIENER_NONSEP);
        if (r == RESTORE_PC_WIENER &&
            (plane == AOM_PLANE_Y || PC_WIENER_FILTER_CHROMA ||
             PC_WIENER_ONLY_CLASSIFY_CHROMA)) {
          rsc.is_buffered = true;  // Buffer is set.
        }
#endif  // CONFIG_COMBINE_PC_NS_WIENER

        if (r == 0 || cost < best_cost) {
          best_cost = cost;
          best_rtype = r;
        }
      }
#if CONFIG_COMBINE_PC_NS_WIENER
      rsc.is_buffered = false;  // Buffer is consumed.
#endif                          // CONFIG_COMBINE_PC_NS_WIENER
    }

#if CONFIG_CNN_GUIDED_QUADTREE
    if (plane == plane_start) cm->lr_y_rdcost = best_cost;
#endif  // CONFIG_CNN_GUIDED_QUADTREE

    finalize_frame_and_unit_info(best_rtype, &cm->rst_info[plane], &rsc);

#if CONFIG_LR_FLEX_SYNTAX
    assert(IMPLIES(
        cm->features.lr_tools_count[plane] < 2,
        cm->rst_info[plane].frame_restoration_type != RESTORE_SWITCHABLE));
#endif  // CONFIG_LR_FLEX_SYNTAX
    adjust_frame_rtype(&cm->rst_info[plane], plane_ntiles, &rsc,
                       &cpi->oxcf.tool_cfg);
  }

#if CONFIG_SAVE_IN_LOOP_DATA
  // File format for the exported data:
  // Two integers then a float: num_rows_luma, num_cols_luma, rdmult.
  // In float:
  // original_frame0, pre_lr_frame0, tskip_frame0, qstep_frame0, post_lr_frame0,
  // ...
  const int absolute_poc = cm->cur_frame->absolute_poc;
  const bool exporting_this_frame = !(export_context_is_skipped(absolute_poc) ||
                                      export_context_is_exported(absolute_poc));
  assert(cm->seq_params.subsampling_y == 1);
  assert(cm->seq_params.subsampling_x == 1);
  if (exporting_this_frame) {
    const YV12_BUFFER_CONFIG *pre_lr_decoded = &cpi->common.cur_frame->buf;
    bool success = true;
    if (!export_context_is_initialized()) {
      const int num_rows_luma = pre_lr_decoded->crop_heights[0];
      const int num_cols_luma = pre_lr_decoded->crop_widths[0];
      const int rdmult_scale = (1 << 4) * (1 << RDDIV_BITS);
      const float rdmult_for_rate_in_bits = x->rdmult * 1.0f / rdmult_scale;

      // Keep default filename.
      export_context_set_filename(NULL);
      success = export_context_initialize(num_rows_luma, num_cols_luma,
                                          rdmult_for_rate_in_bits);
      assert(success);
    }
    export_context_register_as_exported(absolute_poc);

    // Export original.
    success = true;
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      const int stride_index = plane != AOM_PLANE_Y ? 1 : 0;
      success = success &&
                export_context_export_frame(
                    src->buffers[plane], src->strides[stride_index],
                    true /*cm->seq_params.use_highbitdepth*/, upsample_factor);
    }

    // Export decoded frame before loop reconstruction.
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      const int stride_index = plane != AOM_PLANE_Y ? 1 : 0;
      success = success &&
                export_context_export_frame(
                    pre_lr_decoded->buffers[plane],
                    pre_lr_decoded->strides[stride_index],
                    true /*cm->seq_params.use_highbitdepth*/, upsample_factor);
    }

    // Export tskip.
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      success = success && export_context_export_frame(
                               cm->mi_params.tx_skip[plane],
                               cm->mi_params.tx_skip_stride[plane], false,
                               upsample_factor << MI_SIZE_LOG2);
    }
    assert(success);

    // Export qstep.
    success = success && export_context_export_qstep(cpi);
    assert(success);

    // Construct and save the output of loop restoration after lr optimization
    // carried out above.
    // (i) Fill tmp_buffer with pre-lr decoded frame.
    YV12_BUFFER_CONFIG *tmp_buffer = &cpi->trial_frame_rst;
    assert(tmp_buffer->crop_heights[0] >= export_context.num_rows_luma &&
           tmp_buffer->crop_widths[0] >= export_context.num_cols_luma);
    for (int plane = 0; plane < num_planes; ++plane) {
      const int stride_index = plane != AOM_PLANE_Y ? 1 : 0;
      const int tmp_buffer_stride = tmp_buffer->strides[stride_index];
      const int pre_lr_stride = pre_lr_decoded->strides[stride_index];
      uint8_t *tmp_buffer_buffers_8bit = (uint8_t *)tmp_buffer->buffers[plane];
      uint16_t *tmp_buffer_buffers_16bit =
          CONVERT_TO_SHORTPTR(tmp_buffer->buffers[plane]);
      const uint16_t *pre_lr_decoded_buffers_16bit =
          CONVERT_TO_SHORTPTR(pre_lr_decoded->buffers[plane]);

      const int num_rows = plane != AOM_PLANE_Y
                               ? export_context.num_rows_luma >> 1
                               : export_context.num_rows_luma;
      const int num_cols = plane != AOM_PLANE_Y
                               ? export_context.num_cols_luma >> 1
                               : export_context.num_cols_luma;
      for (int r = 0; r < num_rows; ++r) {
        for (int c = 0; c < num_cols; ++c) {
          tmp_buffer_buffers_16bit[r * tmp_buffer_stride + c] =
              pre_lr_decoded_buffers_16bit[r * pre_lr_stride + c];
        }
      }
    }
    const int lr_mode_info_w = cm->mi_params.mi_cols << MI_SIZE_LOG2;
    const int lr_mode_info_h = cm->mi_params.mi_rows << MI_SIZE_LOG2;
    assert(lr_mode_info_w >= export_context.num_cols_luma);
    assert(lr_mode_info_h >= export_context.num_rows_luma);
    const int impossible_lr_mode = 255;
    for (int plane = 0; plane < num_planes; ++plane) {
      const int buffer_size = plane ? lr_mode_info_w * lr_mode_info_h / 4
                                    : lr_mode_info_w * lr_mode_info_h;
      cm->mi_params.lr_mode_info[plane] =
          aom_calloc(buffer_size, sizeof(uint8_t));
      cm->mi_params.lr_mode_info_stride[plane] =
          plane == AOM_PLANE_Y ? lr_mode_info_w : lr_mode_info_w / 2;
      memset(cm->mi_params.lr_mode_info[plane], impossible_lr_mode,
             buffer_size);
    }

    // (ii) Apply lr.
    av1_loop_restoration_filter_frame(tmp_buffer, cm, 0, &cpi->lr_ctxt);

    // (iii) Export reconstruction.
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      const int stride_index = plane != AOM_PLANE_Y ? 1 : 0;
      success =
          success &&
          export_context_export_frame(
              tmp_buffer->buffers[plane], tmp_buffer->strides[stride_index],
              true /*cm->seq_params.use_highbitdepth*/, upsample_factor);
    }
    assert(success);

    // (iv) Export modes.
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      success = success && export_context_export_frame(
                               cm->mi_params.lr_mode_info[plane],
                               cm->mi_params.lr_mode_info_stride[plane], false,
                               upsample_factor);
    }
    assert(success);

    // Check consistency.
    for (int plane = 0; plane < num_planes; ++plane) {
      const int upsample_factor = plane != AOM_PLANE_Y ? 2 : 1;
      const int num_rows = export_context.num_rows_luma / upsample_factor;
      const int num_cols = export_context.num_cols_luma / upsample_factor;
      const int stride = cm->mi_params.lr_mode_info_stride[plane];
      for (int row = 0; row < num_rows; ++row) {
        for (int col = 0; col < num_cols; ++col) {
          assert(cm->mi_params.lr_mode_info[plane][row * stride + col] !=
                     impossible_lr_mode &&
                 "Found pixel with unassigned mode.");
        }
      }
    }
  }

  for (int plane = 0; plane < num_planes; ++plane) {
    aom_free(cm->mi_params.lr_mode_info[plane]);
    cm->mi_params.lr_mode_info[plane] = NULL;
  }
#endif  // CONFIG_SAVE_IN_LOOP_DATA

#if CONFIG_WIENER_NONSEP && CONFIG_WIENER_NONSEP_CROSS_FILT
  free(luma_buf);
#endif  // CONFIG_WIENER_NONSEP_CROSS_FILT

  aom_free(rusi);
#if CONFIG_WIENER_NONSEP
  aom_vector_destroy(&wienerns_stats);
#endif  // CONFIG_WIENER_NONSEP

#if CONFIG_RST_MERGECOEFFS
  aom_vector_destroy(&unit_stack);
  aom_vector_destroy(&unit_indices);
#endif  // CONFIG_RST_MERGECOEFFS
}
