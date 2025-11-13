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

#ifndef AOM_AV1_COMMON_RESTORATION_H_
#define AOM_AV1_COMMON_RESTORATION_H_

#include "aom_ports/mem.h"
#include "config/aom_config.h"

#include "av1/common/blockd.h"
#include "av1/common/enums.h"

#include "third_party/vector/vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! @file */

/*!\cond */

// Border for Loop restoration buffer
#define AOM_RESTORATION_FRAME_BORDER 32
#define CLIP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#define USE_LOOP_RESTORATION_MT \
  0  // Enable when multithrading works again w/
     // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#else
#define USE_LOOP_RESTORATION_MT 1
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES

#define RESTORATION_PROC_UNIT_SIZE 64

// Filter tile grid offset upwards compared to the superblock grid
#define RESTORATION_UNIT_OFFSET 8

// RESTORATION_BORDER_VERT determines line buffer requirement for LR.
// Note the line buffer needed is twice the value of this macro.
#define RESTORATION_BORDER_VERT 4
#define RESTORATION_BORDER_HORZ 4

// How many rows of deblocked pixels do we save above/below each processing
// stripe.
#define RESTORATION_CTX_VERT 2
// Note (RESTORATION_BORDER_VERT - RESTORATION_CTX_VERT) lines are padded by
// repetition.

#define RESTORATION_UNITSIZE_MAX 512
#define RESTORATION_WIDTH_MAX (RESTORATION_UNITSIZE_MAX * 3 / 2)
#define MAX_LRU_STRIPE_BUF_SIZE                            \
  ((RESTORATION_WIDTH_MAX + 2 * RESTORATION_BORDER_HORZ) * \
   (RESTORATION_PROC_UNIT_SIZE + 2 * RESTORATION_BORDER_VERT))
#define NUM_PC_WIENER_TAPS_LUMA 13
#include "av1/common/pc_wiener_filters.h"

// Maximum number of filter-taps in LR non-separable filtering.
#define MAX_NUM_DICTIONARY_TAPS 28

// Central values for the taps
#define WIENER_FILT_TAP0_MIDV (3)
#define WIENER_FILT_TAP1_MIDV (-7)
#define WIENER_FILT_TAP2_MIDV (15)

#define WIENER_FILT_TAP0_BITS 4
#define WIENER_FILT_TAP1_BITS 5
#define WIENER_FILT_TAP2_BITS 6

#define WIENER_FILT_TAP0_MINV \
  (WIENER_FILT_TAP0_MIDV - (1 << WIENER_FILT_TAP0_BITS) / 2)
#define WIENER_FILT_TAP1_MINV \
  (WIENER_FILT_TAP1_MIDV - (1 << WIENER_FILT_TAP1_BITS) / 2)
#define WIENER_FILT_TAP2_MINV \
  (WIENER_FILT_TAP2_MIDV - (1 << WIENER_FILT_TAP2_BITS) / 2)

#define WIENER_FILT_TAP0_MAXV \
  (WIENER_FILT_TAP0_MIDV - 1 + (1 << WIENER_FILT_TAP0_BITS) / 2)
#define WIENER_FILT_TAP1_MAXV \
  (WIENER_FILT_TAP1_MIDV - 1 + (1 << WIENER_FILT_TAP1_BITS) / 2)
#define WIENER_FILT_TAP2_MAXV \
  (WIENER_FILT_TAP2_MIDV - 1 + (1 << WIENER_FILT_TAP2_BITS) / 2)

#define WIENERNS_UV_BRD 2  // Max offset for luma used for chorma

#define WIENERNS_ROW_ID 0
#define WIENERNS_COL_ID 1
#define WIENERNS_BUF_POS 2

#define WIENERNS_COEFCFG_LEN 3
#define WIENERNS_BIT_ID 0
#define WIENERNS_MIN_ID 1
#define WIENERNS_PAR_ID 2

typedef struct {
  NonsepFilterConfig nsfilter_config;
  int ncoeffs;
  const int (*coeffs)[WIENERNS_COEFCFG_LEN];
  int nsubsets;
  const int (*subset_config)[WIENERNS_TAPS_MAX];
} WienernsFilterParameters;

extern const WienernsFilterParameters wienerns_filter_y;
extern const WienernsFilterParameters wienerns_filter_uv;

extern const int wienerns_simd_config_y[25][3];
extern const int wienerns_simd_large_config_y[33][3];
extern const int wienerns_simd_config_uv_from_uv[13][3];
extern const int wienerns_simd_config_uv_from_y[13][3];
extern const int wienerns_simd_config_uv_from_uvonly[13][3];

static INLINE const WienernsFilterParameters *get_wienerns_parameters(
    int qindex, int is_uv) {
  (void)qindex;
  return is_uv ? &wienerns_filter_uv : &wienerns_filter_y;
}

static inline int is_frame_filters_enabled(int plane) {
  (void)plane;
  return 1;
}

// Returns the alternate plane whose reference-frame-filters can be used to
// augment those for plane.
static inline int alternate_ref_plane(int plane) {
  switch (plane) {
    case AOM_PLANE_Y: return -1;
    case AOM_PLANE_U: return AOM_PLANE_V;
    case AOM_PLANE_V: return AOM_PLANE_U;
    default: assert(0); return -1;
  }
}

static inline int max_num_classes(int plane) {
  return (plane == AOM_PLANE_Y) ? NUM_WIENERNS_CLASS_INIT_LUMA
                                : NUM_WIENERNS_CLASS_INIT_CHROMA;
}

static inline int default_num_classes(int plane) {
  return max_num_classes(plane);
}

const uint8_t *get_pc_wiener_sub_classifier(int num_classes, int set_index);

void fill_filter_with_match(WienerNonsepInfo *filter,
                            const int16_t *frame_filter_dictionary,
                            int dict_stride, const int *match_indices,
                            const WienernsFilterParameters *nsfilter_params,
                            int class_id, int nopcw);
void fill_first_slot_of_bank_with_filter_match(
    int plane, WienerNonsepInfoBank *bank, const WienerNonsepInfo *reference,
    const int *match_indices, int base_qindex, int class_id,
    int16_t *frame_filter_dictionary, int dict_stride, int nopcw);

#define ILLEGAL_MATCH -1

static INLINE int get_first_match_index(int compound_match_index,
                                        int num_classes, int nopcw) {
  assert(num_classes >= 1 && num_classes <= WIENERNS_MAX_CLASSES);
  return compound_match_index &
         ((1 << num_frame_first_predictor_bits[nopcw][num_classes]) - 1);
}

#define LR_TILE_ROW 0
#define LR_TILE_COL 0
#define LR_TILE_COLS 1

/*!\endcond */

/*!\brief Parameters related to Restoration Unit Info */
typedef struct {
  /*!
   * restoration type
   */
  RestorationType restoration_type;
  /*!
   * Nonseparable Wiener filter information.
   */
  WienerNonsepInfo wienerns_info;
  /*!
   * Pointer to luma frame.
   */
  const uint16_t *luma;
  /*!
   * Stride for luma frame.
   */
  int luma_stride;

  /*!
   * Plane for filtering.
   */
  int plane;
  /*!
   * Quantizer index.
   */
  int base_qindex;
  /*!
   * The class-id that classifcation related processing should be restricted to.
   */
  int wiener_class_id_restrict;
  /*!
   * Pointer to tskip frame.
   */
  const uint8_t *tskip;
  /*!
   * Stride for tskip frame.
   */
  int tskip_stride;
  /*!
   * Offset to quantizer index.
   */
  int qindex_offset;
  /*!
   * Pointer to wiener_class_id frame.
   */
  uint8_t *wiener_class_id;
  /*!
   * Stride for wiener_class_id frame.
   */
  int wiener_class_id_stride;
  /*!
   * Pointer to buffers for pcwiener computations.
   */
  PcwienerBuffers *pcwiener_buffers;
  /*!
   * flag to skip accumulating txskip values
   */
  bool tskip_zero_flag;
  /*!
   * Whether classification needs to be computed.
   */
  int compute_classification;
  /*!
   * Whether filtering with pre-trained filters should be skipped.
   */
  int skip_pcwiener_filtering;
  /*!\cond */
  MB_MODE_INFO **mbmi_ptr;
  int mi_stride;
  int ss_x;
  int ss_y;
  struct aom_internal_error_info *error;
  /*!\endcond */
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  /*!
   * Pointer to point lossless_segment array in cm.
   */
  const bool *lossless_segment;
  /*!
   * Pointer to cm.
   */
  const struct AV1Common *cm;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
} RestorationUnitInfo;

/*!\cond */

// A restoration line buffer needs space for two lines plus a horizontal filter
// margin of RESTORATION_BORDER_HORZ on each side.
// The maximum picture width is 8192 * 8 for LR to work properly in this
// software implementation. This is one quick implementation, the buffer size
// should be allocated based on picture width
#define MAX_SUPPORTED_PIC_WIDTH_IN_CCALF_IMP (8192 * 8)
#define RESTORATION_LINEBUFFER_WIDTH \
  (MAX_SUPPORTED_PIC_WIDTH_IN_CCALF_IMP * 3 / 2 + 2 * RESTORATION_BORDER_HORZ)

// Similarly, the column buffers (used when we're at a vertical tile edge
// that we can't filter across) need space for one processing unit's worth
// of pixels, plus the top/bottom border width
#define RESTORATION_COLBUFFER_HEIGHT \
  (RESTORATION_PROC_UNIT_SIZE + 2 * RESTORATION_BORDER_VERT)

typedef struct {
  // Temporary buffers to save/restore 3 lines above/below the restoration
  // stripe.
  uint16_t tmp_save_above[2][RESTORATION_BORDER_VERT]
                         [RESTORATION_LINEBUFFER_WIDTH];
  uint16_t tmp_save_below[2][RESTORATION_BORDER_VERT]
                         [RESTORATION_LINEBUFFER_WIDTH];
  uint16_t tmp_save_left[2][RESTORATION_COLBUFFER_HEIGHT]
                        [RESTORATION_BORDER_HORZ];
  uint16_t tmp_save_right[2][RESTORATION_COLBUFFER_HEIGHT]
                         [RESTORATION_BORDER_HORZ];
} RestorationLineBuffers;
/*!\endcond */

/*!\brief Parameters related to Restoration Stripe boundaries */
typedef struct {
  /*!
   * stripe boundary above
   */
  uint16_t *stripe_boundary_above;

  /*!
   * stripe boundary below
   */
  uint16_t *stripe_boundary_below;

  /*!
   * strides for stripe boundaries above and below
   */
  int stripe_boundary_stride;

  /*!
   * size of stripe boundaries above and below
   */
  int stripe_boundary_size;

  /*!
   * number of stripes above and below
   */
  int num_stripes;
} RestorationStripeBoundaries;

/*!\brief Parameters related to Restoration Info */
typedef struct {
  /*!
   * Restoration type for frame
   */
  RestorationType frame_restoration_type;

  /*!
   * Restoration unit size
   */
  int restoration_unit_size;

  /*!
   * Maximum restoration unit size
   */
  int max_restoration_unit_size;
  /*!
   * Minimum restoration unit size
   */
  int min_restoration_unit_size;
  /**
   * \name Fields allocated and initialised by av1_alloc_restoration_struct.
   * (horz_)units_per_tile give the number of restoration units in
   * (one row of) the largest tile in the frame.
   */
  /**@{*/

  /*!
   * Number of vertical units per tile
   */
  int vert_units_per_tile[MAX_TILE_ROWS];

  /*!
   * Number of horizontal units per tile for the largest tile in the frame
   */
  int horz_units_per_tile[MAX_TILE_COLS];

  /*!
   * Number of vertical units per frame
   */
  int vert_units_per_frame;

  /*!
   * Number of horizontal units per frame
   */
  int horz_units_per_frame;

  /*!
   * Number of vertical stripes per frame
   */
  int vert_stripes_per_frame;

  /**@}*/

  /*!
   * List of info for units in tile.
   * The data in unit_info is laid out with units_per_tile entries for each
   * tile, which have stride horz_units_per_tile.
   * Even if there are tiles of different sizes, the data in unit_info is
   * laid out as if all tiles are of full size.
   */
  RestorationUnitInfo *unit_info;

  /*!
   * Number of units allocated for unit_info
   */
  int nunits_alloc;

  /*!
   * Restoration Stripe boundary info
   */
  RestorationStripeBoundaries boundaries;

  /*!
   * Whether optimized lr can be used for speed.
   */
  int optimized_lr;
  /*!
   * Number of classes in the Wienerns filtering calculation.
   */
  int num_filter_classes;
  /*!
   * Whether frame-level filters are on or off.
   */
  int frame_filters_on;
  /*!
   * Frame-level filter taps.
   */
  WienerNonsepInfo frame_filters;
  /*!
   * Whether frame-level filters are initialized.
   */
  int frame_filters_initialized;
  /*!
   * whether frame filter is predicted from a reference picture
   */
  uint8_t temporal_pred_flag;
  /*!
   * reference picture index for frame level filter prediction
   */
  uint8_t rst_ref_pic_idx;
} RestorationInfo;

/*!\cond */

// Clips scale_x to allowed range of Wienerns filter taps.
static INLINE int16_t clip_to_wienerns_range(int16_t scale_x, int16_t minv,
                                             int16_t n) {
  scale_x = AOMMAX(scale_x, minv);
  scale_x = AOMMIN(scale_x, minv + n - 1);
  return (int16_t)scale_x;
}

static INLINE void set_default_wienerns(WienerNonsepInfo *wienerns_info,
                                        int qindex, int num_classes,
                                        int chroma) {
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(qindex, chroma);
  wienerns_info->num_classes = num_classes;
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    wienerns_info->bank_ref_for_class[c_id] = 0;
    int16_t *wienerns_info_nsfilter = nsfilter_taps(wienerns_info, c_id);
    for (int i = 0; i < nsfilter_params->ncoeffs; ++i) {
      wienerns_info_nsfilter[i] =
          nsfilter_params->coeffs[i][WIENERNS_MIN_ID] +
          (1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID]) / 2;
    }
  }
}

static INLINE void set_default_wienerns_fromparams(
    WienerNonsepInfo *wienerns_info, int num_classes,
    const WienernsFilterParameters *nsfilter_params) {
  wienerns_info->num_classes = num_classes;
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    wienerns_info->bank_ref_for_class[c_id] = 0;
    int16_t *wienerns_info_nsfilter = nsfilter_taps(wienerns_info, c_id);
    for (int i = 0; i < nsfilter_params->ncoeffs; ++i) {
      wienerns_info_nsfilter[i] =
          nsfilter_params->coeffs[i][WIENERNS_MIN_ID] +
          (1 << nsfilter_params->coeffs[i][WIENERNS_BIT_ID]) / 2;
    }
  }
}

// 0: Skip luma pixels to scale down to chroma (simplest)
// 1: Average 4 or 2 luma pixels to scale down to chroma
// 2: Average 2 (top and down) luma pixels to scale down to chroma for 420,
// could be based on the luma downsampling type from CFL tool 3: Use 8-tap
// downsampling filter
#define WIENERNS_CROSS_FILT_LUMA_TYPE 2

uint16_t *wienerns_copy_luma_highbd(const uint16_t *dgd, int height_y,
                                    int width_y, int in_stride, uint16_t **luma,
                                    int height_uv, int width_uv, int border,
                                    int out_stride, int bd
#if WIENERNS_CROSS_FILT_LUMA_TYPE == 2
                                    ,
                                    int ds_type
#endif
);

typedef struct {
  int h_start, h_end, v_start, v_end;
} RestorationTileLimits;

typedef void (*rest_unit_visitor_t)(const RestorationTileLimits *limits,
                                    const AV1PixelRect *tile_rect,
                                    int rest_unit_idx, int rest_unit_idx_seq,
                                    void *priv, RestorationLineBuffers *rlbs);

typedef struct FilterFrameCtxt {
  const RestorationInfo *rsi;
  int tile_stripe0;
  int ss_x, ss_y;
  int bit_depth;
  uint16_t *data8, *dst8;
  int data_stride, dst_stride;
  AV1PixelRect tile_rect;
  int plane;
  int plane_width;
  int plane_height;
  int base_qindex;
  const uint16_t *luma;
  int luma_stride;
  const uint8_t *tskip;
  int tskip_stride;
  int qindex_offset;
  uint8_t *wiener_class_id;
  int wiener_class_id_stride;
  bool tskip_zero_flag;
  const struct CommonModeInfoParams *mi_params;
  int order_hint;
  struct aom_internal_error_info *error;
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  const bool *lossless_segment;
  const struct AV1Common *cm;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  int disable_loopfilters_across_tiles;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
} FilterFrameCtxt;

typedef struct AV1LrStruct {
  rest_unit_visitor_t on_rest_unit;
  FilterFrameCtxt ctxt[MAX_MB_PLANE];
  YV12_BUFFER_CONFIG *frame;
  YV12_BUFFER_CONFIG *dst;
  struct CommonTileParams *tiles;
} AV1LrStruct;

uint16_t *wienerns_copy_luma_with_virtual_lines(struct AV1Common *cm,
                                                uint16_t **luma_hbd);

void av1_alloc_restoration_struct(struct AV1Common *cm, RestorationInfo *rsi,
                                  int is_uv);
void av1_free_restoration_struct(RestorationInfo *rst_info);

void av1_extend_frame(uint16_t *data, int width, int height, int stride,
                      int border_horz, int border_vert);

/*!\endcond */

/*!\brief Function for applying loop restoration filter to a single unit.
 *
 * \ingroup in_loop_restoration
 * This function applies the loop restoration filter to a single
 * loop restoration unit.
 *
 * \param[in]  limits        Limits of the unit
 * \param[in]  rui           The parameters to use for this unit and its
 *                           coefficients
 * \param[in]  rsb           Deblocked pixels to use for stripe boundaries
 * \param[in]  rlbs          Space to use as a scratch buffer
 * \param[in]  tile_rect     Limits of the tile containing this unit
 * \param[in]  tile_stripe0  Index of the first stripe in this tile
 * \param[in]  ss_x          Horizontal subsampling for plane
 * \param[in]  ss_y          Vertical subsampling for plane
 * \param[in]  bit_depth     Bit-depth of the video
 * \param[in]  data          Frame data (pointing at the top-left corner of
 *                           the frame, not the restoration unit).
 * \param[in]  stride        Stride of \c data
 * \param[out] dst           Buffer where the results will be written. Like
 *                           \c data, \c dst should point at the top-left
 *                           corner of the frame
 * \param[in]  dst_stride    Stride of \c dst
 * \param[in]  plane_width   Picture width of the current plane
 * \param[in] disable_loopfilters_across_tiles Whether loop filter can across
 *                           tile boundary
 * \param[in]  optimized_lr  Whether to use fast optimized Loop Restoration
 *
 * Nothing is returned. Instead, the filtered unit is output in \c dst
 * at the proper restoration unit offset.
 */
void av1_loop_restoration_filter_unit(
    const RestorationTileLimits *limits, const RestorationUnitInfo *rui,
    const RestorationStripeBoundaries *rsb, RestorationLineBuffers *rlbs,
    const AV1PixelRect *tile_rect, int tile_stripe0, int ss_x, int ss_y,
    int bit_depth, uint16_t *data, int stride, uint16_t *dst, int dst_stride,
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    int plane_width, int disable_loopfilters_across_tiles,
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    int optimized_lr);

/*!\brief Function for applying loop restoration filter to a frame
 *
 * \ingroup in_loop_restoration
 * This function applies the loop restoration filter to a frame.
 *
 * \param[in, out]  frame         Compressed frame buffer
 * \param[in, out]  cm            Pointer to top level common structure
 * \param[in]       optimized_lr  Whether to use fast optimized Loop Restoration
 * \param[in]       lr_ctxt       Loop restoration context
 *
 * Nothing is returned. Instead, the filtered frame is output in \c frame.
 */
void av1_loop_restoration_filter_frame(YV12_BUFFER_CONFIG *frame,
                                       struct AV1Common *cm, int optimized_lr,
                                       void *lr_ctxt);
/*!\cond */

#define DEF_UV_LR_TOOLS_DISABLE_MASK (1 << RESTORE_PC_WIENER)

struct AV1LrSyncData;

typedef void (*sync_read_fn_t)(void *const lr_sync, int r, int c, int plane);

typedef void (*sync_write_fn_t)(void *const lr_sync, int r, int c,
                                const int sb_cols, int plane);

// Call on_rest_unit for each loop restoration unit in a tile.
void av1_foreach_rest_unit_in_tile(const AV1PixelRect *tile_rect, int unit_idx0,
                                   int hunits_per_tile, int vunits_per_tile,
                                   int unit_stride, int unit_size, int ss_y,
                                   int tile_stripe0, void *priv,
                                   uint16_t *stripe_buf);
// Call on_rest_unit for each loop restoration unit in a coded SB.
void av1_foreach_rest_unit_in_sb(const AV1PixelRect *tile_rect,
                                 const AV1PixelRect *sb_rect, int unit_idx0,
                                 int hunits_per_tile, int vunits_per_tile,
                                 int unit_stride, int unit_size, int ss_y,
                                 int plane, rest_unit_visitor_t on_rest_unit,
                                 void *priv, RestorationLineBuffers *rlbs,
                                 int *processed);
// Call on_rest_unit for each loop restoration unit in the plane.
void av1_foreach_rest_unit_in_plane(const struct AV1Common *cm, int plane,
                                    void *priv, AV1PixelRect *tile_rect);

// Return 1 iff the block at mi_row, mi_col with size bsize is a
// top-level superblock containing the top-left corner of at least one
// loop restoration unit.
//
// If the block is a top-level superblock, the function writes to
// *rcol0, *rcol1, *rrow0, *rrow1. The rectangle of restoration unit
// indices given by [*rcol0, *rcol1) x [*rrow0, *rrow1) are relative
// to the current tile, whose starting index is returned as
// *tile_tl_idx.
int av1_loop_restoration_corners_in_sb(const struct AV1Common *cm, int plane,
                                       int mi_row, int mi_col, BLOCK_SIZE bsize,
                                       int *rcol0, int *rcol1, int *rrow0,
                                       int *rrow1);

void av1_loop_restoration_save_boundary_lines(const YV12_BUFFER_CONFIG *frame,
                                              struct AV1Common *cm,
                                              int after_cdef);
void save_tile_row_boundary_lines(const YV12_BUFFER_CONFIG *frame, int plane,
                                  struct AV1Common *cm, int after_cdef);
void av1_loop_restoration_filter_frame_init(AV1LrStruct *lr_ctxt,
                                            YV12_BUFFER_CONFIG *frame,
                                            struct AV1Common *cm,
                                            int optimized_lr, int num_planes);
void av1_loop_restoration_copy_planes(AV1LrStruct *loop_rest_ctxt,
                                      struct AV1Common *cm, int num_planes);
void av1_foreach_rest_unit_in_row(
    RestorationTileLimits *limits, const AV1PixelRect *proc_rect,
    const AV1PixelRect *tile_rect, rest_unit_visitor_t on_rest_unit,
    int row_number, int unit_size, int unit_idx0, int hunits_per_tile,
    int vunits_per_tile, int unit_stride, int plane, void *priv,
    RestorationLineBuffers *rlbs, sync_read_fn_t on_sync_read,
    sync_write_fn_t on_sync_write, struct AV1LrSyncData *const lr_sync,
    int *processed);
AV1PixelRect av1_whole_frame_rect(const struct AV1Common *cm, int is_uv);
AV1PixelRect av1_get_rutile_rect(const struct AV1Common *cm, int is_uv,
                                 int ru_start_row, int ru_end_row,
                                 int ru_start_col, int ru_end_col,
                                 int ru_height, int ru_width);

int av1_lr_count_units_in_tile(int unit_size, int tile_size);
int av1_lr_count_stripes_in_tile(int tile_size, int ss_y);
void av1_lr_sync_read_dummy(void *const lr_sync, int r, int c, int plane);
void av1_lr_sync_write_dummy(void *const lr_sync, int r, int c,
                             const int sb_cols, int plane);

void copy_tile(int width, int height, const uint16_t *src, int src_stride,
               uint16_t *dst, int dst_stride);

void set_restoration_unit_size(
#if CONFIG_RU_SIZE_RESTRICTION || (CONFIG_MINIMUM_LR_UNIT_SIZE_64x64 && \
                                   CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
    struct AV1Common *cm,
#endif  // CONFIG_RU_SIZE_RESTRICTION || (CONFIG_MINIMUM_LR_UNIT_SIZE_64x64 &&
        // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
    int width, int height, int sx, int sy, RestorationInfo *rst);

static INLINE int to_readwrite_framefilters(const RestorationInfo *rsi,
                                            int mi_row, int mi_col) {
  return ((rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
           rsi->frame_restoration_type == RESTORE_SWITCHABLE) &&
          rsi->frame_filters_on && !rsi->temporal_pred_flag && mi_row == 0 &&
          mi_col == 0);
}

void av1_copy_rst_frame_filters(RestorationInfo *to,
                                const RestorationInfo *from);

// returns 1 if sym does not need signaling because there are no asymmetric taps
// in the config
// returns 2 if sym does not need signaling because with asymmetric taps the
// number of taps exceeds the limit of 18 (WIENERNS_SIGNALED_TAPS_MAX).
// returns 0 if sym bit needs signaling
static INLINE int skip_sym_bit(const WienernsFilterParameters *nsfilter_params,
                               int subset) {
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  int ncoeffs1;
  config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, NULL);
  int num_taps = 0;
  int asym_taps = 0;
  for (int i = beg_feat; i < end_feat; ++i) {
    if (!nsfilter_params->subset_config[subset][i]) continue;
    const int is_asym_coeff =
        (i < nsfilter_params->nsfilter_config.asymmetric ||
         (i >= ncoeffs1 &&
          i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
    num_taps++;
    asym_taps += is_asym_coeff;
  }
  assert(num_taps - (asym_taps >> 1) <= WIENERNS_SIGNALED_TAPS_MAX);

  return (num_taps > WIENERNS_SIGNALED_TAPS_MAX ? 2 : (asym_taps == 0));
}

// This function derive index of the first stripe of a tile
static INLINE int get_top_stripe_idx_in_tile(int tile_row, int tile_col,
                                             const struct AV1Common *cm,
                                             int procunit_size,
                                             int procunit_voffset) {
  TileInfo tile_info;
  int top_stripe = 0;
  for (int tr = 0; tr < tile_row; ++tr) {
    av1_tile_init(&tile_info, cm, tr, tile_col);
    const int tile_height =
        (tile_info.mi_row_end - tile_info.mi_row_start) * MI_SIZE;
    const int vprocunits_in_tile =
        (tile_height + procunit_voffset + procunit_size - 1) / procunit_size;
    top_stripe += vprocunits_in_tile;
  }
  return top_stripe;
}

// This function derive index of the first RU of a tile
static INLINE int get_ru_index_for_tile_start(const RestorationInfo *rsi,
                                              int tile_row, int tile_col) {
  int x = 0, y = 0;
  for (int tr = 0; tr < tile_row; ++tr) y += rsi->vert_units_per_tile[tr];
  for (int tc = 0; tc < tile_col; ++tc) x += rsi->horz_units_per_tile[tc];
  return y * rsi->horz_units_per_frame + x;
}
/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_RESTORATION_H_
