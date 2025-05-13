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

#ifndef AOM_AV1_ENCODER_ENCODER_UTILS_H_
#define AOM_AV1_ENCODER_ENCODER_UTILS_H_

#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AM_SEGMENT_ID_INACTIVE 7
#define AM_SEGMENT_ID_ACTIVE 0

extern const int default_tx_type_probs[FRAME_UPDATE_TYPES][TX_SIZES_ALL]
                                      [TX_TYPES];

extern const int default_warped_probs[FRAME_UPDATE_TYPES];

extern const int default_switchable_interp_probs[FRAME_UPDATE_TYPES]
                                                [SWITCHABLE_FILTER_CONTEXTS]
                                                [SWITCHABLE_FILTERS];

// Mark all inactive blocks as active. Other segmentation features may be set
// so memset cannot be used, instead only inactive blocks should be reset.
static AOM_INLINE void suppress_active_map(AV1_COMP *cpi) {
  unsigned char *const seg_map = cpi->enc_seg.map;
  int i;
  if (cpi->active_map.enabled || cpi->active_map.update)
    for (i = 0;
         i < cpi->common.mi_params.mi_rows * cpi->common.mi_params.mi_cols; ++i)
      if (seg_map[i] == AM_SEGMENT_ID_INACTIVE)
        seg_map[i] = AM_SEGMENT_ID_ACTIVE;
}

static AOM_INLINE void set_mb_mi(CommonModeInfoParams *mi_params, int width,
                                 int height) {
  // Ensure that the decoded width and height are both multiples of
  // 8 luma pixels (note: this may only be a multiple of 4 chroma pixels if
  // subsampling is used).
  // This simplifies the implementation of various experiments,
  // eg. cdef, which operates on units of 8x8 luma pixels.
  const int aligned_width = ALIGN_POWER_OF_TWO(width, 3);
  const int aligned_height = ALIGN_POWER_OF_TWO(height, 3);

  mi_params->mi_cols = aligned_width >> MI_SIZE_LOG2;
  mi_params->mi_rows = aligned_height >> MI_SIZE_LOG2;
  mi_params->mi_stride = calc_mi_size(mi_params->mi_cols);

  mi_params->mb_cols = (mi_params->mi_cols + 2) >> 2;
  mi_params->mb_rows = (mi_params->mi_rows + 2) >> 2;
  mi_params->MBs = mi_params->mb_rows * mi_params->mb_cols;

  const int mi_alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  mi_params->mi_alloc_stride =
      (mi_params->mi_stride + mi_alloc_size_1d - 1) / mi_alloc_size_1d;

  assert(mi_size_wide[mi_params->mi_alloc_bsize] ==
         mi_size_high[mi_params->mi_alloc_bsize]);
}

static AOM_INLINE void enc_free_mi(CommonModeInfoParams *mi_params) {
  aom_free(mi_params->mi_alloc);
  mi_params->mi_alloc = NULL;
  aom_free(mi_params->mi_grid_base);
  mi_params->mi_grid_base = NULL;
#if CONFIG_C071_SUBBLK_WARPMV
  aom_free(mi_params->mi_alloc_sub);
  mi_params->mi_alloc_sub = NULL;
  aom_free(mi_params->submi_grid_base);
  mi_params->submi_grid_base = NULL;
#endif
  mi_params->mi_alloc_size = 0;
  aom_free(mi_params->tx_type_map);
  mi_params->tx_type_map = NULL;
  aom_free(mi_params->cctx_type_map);
  mi_params->cctx_type_map = NULL;
  av1_dealloc_txk_skip_array(mi_params);
  av1_dealloc_class_id_array(mi_params);
}

static AOM_INLINE void enc_set_mb_mi(CommonModeInfoParams *mi_params, int width,
                                     int height) {
  const int is_4k_or_larger = AOMMIN(width, height) >= 2160;
  mi_params->mi_alloc_bsize = is_4k_or_larger ? BLOCK_8X8 : BLOCK_4X4;

  set_mb_mi(mi_params, width, height);
}

static AOM_INLINE void stat_stage_set_mb_mi(CommonModeInfoParams *mi_params,
                                            int width, int height) {
  mi_params->mi_alloc_bsize = BLOCK_16X16;

  set_mb_mi(mi_params, width, height);
}

static AOM_INLINE void enc_setup_mi(CommonModeInfoParams *mi_params) {
  const int mi_grid_size =
      mi_params->mi_stride * calc_mi_size(mi_params->mi_rows);
  memset(mi_params->mi_alloc, 0,
         mi_params->mi_alloc_size * sizeof(*mi_params->mi_alloc));
  memset(mi_params->mi_grid_base, 0,
         mi_grid_size * sizeof(*mi_params->mi_grid_base));
  memset(mi_params->tx_type_map, 0,
         mi_grid_size * sizeof(*mi_params->tx_type_map));
  memset(mi_params->cctx_type_map, 0,
         mi_grid_size * sizeof(*mi_params->cctx_type_map));
  av1_reset_txk_skip_array_using_mi_params(mi_params);
}

static AOM_INLINE void init_buffer_indices(
    ForceIntegerMVInfo *const force_intpel_info, int *const remapped_ref_idx) {
  int fb_idx;
  for (fb_idx = 0; fb_idx < INTER_REFS_PER_FRAME; ++fb_idx)
    remapped_ref_idx[fb_idx] = fb_idx;
  force_intpel_info->rate_index = 0;
  force_intpel_info->rate_size = 0;
}

#define HIGHBD_BFP(BT, SDF, SDAF, VF, SVF, SVAF, SDX4DF, JSDAF, JSVAF) \
  cpi->fn_ptr[BT].sdf = SDF;                                           \
  cpi->fn_ptr[BT].sdaf = SDAF;                                         \
  cpi->fn_ptr[BT].vf = VF;                                             \
  cpi->fn_ptr[BT].svf = SVF;                                           \
  cpi->fn_ptr[BT].svaf = SVAF;                                         \
  cpi->fn_ptr[BT].sdx4df = SDX4DF;                                     \
  cpi->fn_ptr[BT].jsdaf = JSDAF;                                       \
  cpi->fn_ptr[BT].jsvaf = JSVAF;

#define HIGHBD_BFP_WRAPPER(WIDTH, HEIGHT, BD)                                \
  HIGHBD_BFP(                                                                \
      BLOCK_##WIDTH##X##HEIGHT, aom_highbd_sad##WIDTH##x##HEIGHT##_bits##BD, \
      aom_highbd_sad##WIDTH##x##HEIGHT##_avg_bits##BD,                       \
      aom_highbd_##BD##_variance##WIDTH##x##HEIGHT,                          \
      aom_highbd_##BD##_sub_pixel_variance##WIDTH##x##HEIGHT,                \
      aom_highbd_##BD##_sub_pixel_avg_variance##WIDTH##x##HEIGHT,            \
      aom_highbd_sad##WIDTH##x##HEIGHT##x4d_bits##BD,                        \
      aom_highbd_dist_wtd_sad##WIDTH##x##HEIGHT##_avg_bits##BD,              \
      aom_highbd_##BD##_dist_wtd_sub_pixel_avg_variance##WIDTH##x##HEIGHT)

#define MAKE_BFP_SAD_WRAPPER(fnname)                                       \
  static unsigned int fnname##_bits8(                                      \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride) {                                                    \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride);            \
  }                                                                        \
  static unsigned int fnname##_bits10(                                     \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride) {                                                    \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2;       \
  }                                                                        \
  static unsigned int fnname##_bits12(                                     \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride) {                                                    \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4;       \
  }

#define MAKE_BFP_SADAVG_WRAPPER(fnname)                                        \
  static unsigned int fnname##_bits8(                                          \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,     \
      int ref_stride, const uint16_t *second_pred) {                           \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred);   \
  }                                                                            \
  static unsigned int fnname##_bits10(                                         \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,     \
      int ref_stride, const uint16_t *second_pred) {                           \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred) >> \
           2;                                                                  \
  }                                                                            \
  static unsigned int fnname##_bits12(                                         \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,     \
      int ref_stride, const uint16_t *second_pred) {                           \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred) >> \
           4;                                                                  \
  }

#define MAKE_BFP_SAD4D_WRAPPER(fnname)                                         \
  static void fnname##_bits8(const uint16_t *src_ptr, int source_stride,       \
                             const uint16_t *const ref_ptr[], int ref_stride,  \
                             unsigned int *sad_array) {                        \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
  }                                                                            \
  static void fnname##_bits10(const uint16_t *src_ptr, int source_stride,      \
                              const uint16_t *const ref_ptr[], int ref_stride, \
                              unsigned int *sad_array) {                       \
    int i;                                                                     \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
    for (i = 0; i < 4; i++) sad_array[i] >>= 2;                                \
  }                                                                            \
  static void fnname##_bits12(const uint16_t *src_ptr, int source_stride,      \
                              const uint16_t *const ref_ptr[], int ref_stride, \
                              unsigned int *sad_array) {                       \
    int i;                                                                     \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
    for (i = 0; i < 4; i++) sad_array[i] >>= 4;                                \
  }

#define MAKE_BFP_JSADAVG_WRAPPER(fnname)                                    \
  static unsigned int fnname##_bits8(                                       \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,  \
      int ref_stride, const uint16_t *second_pred,                          \
      const DIST_WTD_COMP_PARAMS *jcp_param) {                              \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred, \
                  jcp_param);                                               \
  }                                                                         \
  static unsigned int fnname##_bits10(                                      \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,  \
      int ref_stride, const uint16_t *second_pred,                          \
      const DIST_WTD_COMP_PARAMS *jcp_param) {                              \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred, \
                  jcp_param) >>                                             \
           2;                                                               \
  }                                                                         \
  static unsigned int fnname##_bits12(                                      \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr,  \
      int ref_stride, const uint16_t *second_pred,                          \
      const DIST_WTD_COMP_PARAMS *jcp_param) {                              \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred, \
                  jcp_param) >>                                             \
           4;                                                               \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad256x256)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad256x256_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad256x256x4d)

MAKE_BFP_SAD_WRAPPER(aom_highbd_sad256x128)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad256x128_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad256x128x4d)

MAKE_BFP_SAD_WRAPPER(aom_highbd_sad128x256)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad128x256_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad128x256x4d)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad128x128)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad128x128_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad128x128x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad128x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad128x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad128x64x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x128)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x128_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x128x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad32x16)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad32x16_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad32x16x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad16x32)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad16x32_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad16x32x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x32)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x32_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x32x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad32x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad32x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad32x64x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad32x32)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad32x32_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad32x32x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x64x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad16x16)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad16x16_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad16x16x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad16x8)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad16x8_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad16x8x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad8x16)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad8x16_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad8x16x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad8x8)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad8x8_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad8x8x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad8x4)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad8x4_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad8x4x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad4x8)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad4x8_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad4x8x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad4x4)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad4x4_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad4x4x4d)

MAKE_BFP_SAD_WRAPPER(aom_highbd_sad4x16)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad4x16_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad4x16x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad16x4)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad16x4_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad16x4x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad8x32)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad8x32_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad8x32x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad32x8)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad32x8_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad32x8x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad16x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad16x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad16x64x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x16)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x16_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x16x4d)

#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x8)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x8_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x8x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad8x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad8x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad8x64x4d)

MAKE_BFP_SAD_WRAPPER(aom_highbd_sad64x4)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad64x4_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad64x4x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad4x64)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad4x64_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad4x64x4d)

MAKE_BFP_SAD_WRAPPER(aom_highbd_sad32x4)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad32x4_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad32x4x4d)
MAKE_BFP_SAD_WRAPPER(aom_highbd_sad4x32)
MAKE_BFP_SADAVG_WRAPPER(aom_highbd_sad4x32_avg)
MAKE_BFP_SAD4D_WRAPPER(aom_highbd_sad4x32x4d)

MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad256x256_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad256x128_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad128x256_avg)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad128x128_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad128x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x128_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad32x16_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad16x32_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x32_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad32x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad32x32_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad16x16_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad16x8_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad8x16_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad8x8_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad8x4_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad4x8_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad4x4_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad4x16_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad16x4_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad8x32_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad32x8_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad16x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x16_avg)
#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad8x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x8_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad4x64_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad64x4_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad4x32_avg)
MAKE_BFP_JSADAVG_WRAPPER(aom_highbd_dist_wtd_sad32x4_avg)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#define HIGHBD_MBFP(BT, MCSDF, MCSVF) \
  cpi->fn_ptr[BT].msdf = MCSDF;       \
  cpi->fn_ptr[BT].msvf = MCSVF;

#define HIGHBD_MBFP_WRAPPER(WIDTH, HEIGHT, BD)                    \
  HIGHBD_MBFP(BLOCK_##WIDTH##X##HEIGHT,                           \
              aom_highbd_masked_sad##WIDTH##x##HEIGHT##_bits##BD, \
              aom_highbd_##BD##_masked_sub_pixel_variance##WIDTH##x##HEIGHT)

#define MAKE_MBFP_COMPOUND_SAD_WRAPPER(fnname)                             \
  static unsigned int fnname##_bits8(                                      \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride, const uint16_t *second_pred_ptr, const uint8_t *m,   \
      int m_stride, int invert_mask) {                                     \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride,             \
                  second_pred_ptr, m, m_stride, invert_mask);              \
  }                                                                        \
  static unsigned int fnname##_bits10(                                     \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride, const uint16_t *second_pred_ptr, const uint8_t *m,   \
      int m_stride, int invert_mask) {                                     \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride,             \
                  second_pred_ptr, m, m_stride, invert_mask) >>            \
           2;                                                              \
  }                                                                        \
  static unsigned int fnname##_bits12(                                     \
      const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, \
      int ref_stride, const uint16_t *second_pred_ptr, const uint8_t *m,   \
      int m_stride, int invert_mask) {                                     \
    return fnname(src_ptr, source_stride, ref_ptr, ref_stride,             \
                  second_pred_ptr, m, m_stride, invert_mask) >>            \
           4;                                                              \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad256x256)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad256x128)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad128x256)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad128x128)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad128x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x128)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x32)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad32x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad32x32)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad32x16)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad16x32)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad16x16)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad16x8)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad8x16)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad8x8)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad8x4)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad4x8)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad4x4)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad4x16)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad16x4)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad8x32)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad32x8)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad16x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x16)
#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad8x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x8)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad4x64)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad64x4)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad4x32)
MAKE_MBFP_COMPOUND_SAD_WRAPPER(aom_highbd_masked_sad32x4)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#define HIGHBD_SDSFP(BT, SDSF, SDSX4DF) \
  cpi->fn_ptr[BT].sdsf = SDSF;          \
  cpi->fn_ptr[BT].sdsx4df = SDSX4DF;

#define HIGHBD_SDSFP_WRAPPER(WIDTH, HEIGHT, BD)                   \
  HIGHBD_SDSFP(BLOCK_##WIDTH##X##HEIGHT,                          \
               aom_highbd_sad_skip_##WIDTH##x##HEIGHT##_bits##BD, \
               aom_highbd_sad_skip_##WIDTH##x##HEIGHT##x4d##_bits##BD)

#define MAKE_SDSF_SKIP_SAD_WRAPPER(fnname)                                   \
  static unsigned int fnname##_bits8(const uint16_t *src, int src_stride,    \
                                     const uint16_t *ref, int ref_stride) {  \
    return fnname(src, src_stride, ref, ref_stride);                         \
  }                                                                          \
  static unsigned int fnname##_bits10(const uint16_t *src, int src_stride,   \
                                      const uint16_t *ref, int ref_stride) { \
    return fnname(src, src_stride, ref, ref_stride) >> 2;                    \
  }                                                                          \
  static unsigned int fnname##_bits12(const uint16_t *src, int src_stride,   \
                                      const uint16_t *ref, int ref_stride) { \
    return fnname(src, src_stride, ref, ref_stride) >> 4;                    \
  }

#define MAKE_SDSF_SKIP_SAD_4D_WRAPPER(fnname)                                  \
  static void fnname##_bits8(const uint16_t *src_ptr, int source_stride,       \
                             const uint16_t *const ref_ptr[], int ref_stride,  \
                             unsigned int *sad_array) {                        \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
  }                                                                            \
  static void fnname##_bits10(const uint16_t *src_ptr, int source_stride,      \
                              const uint16_t *const ref_ptr[], int ref_stride, \
                              unsigned int *sad_array) {                       \
    int i;                                                                     \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
    for (i = 0; i < 4; i++) sad_array[i] >>= 2;                                \
  }                                                                            \
  static void fnname##_bits12(const uint16_t *src_ptr, int source_stride,      \
                              const uint16_t *const ref_ptr[], int ref_stride, \
                              unsigned int *sad_array) {                       \
    int i;                                                                     \
    fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array);            \
    for (i = 0; i < 4; i++) sad_array[i] >>= 4;                                \
  }

#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_256x256)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_256x128)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_128x256)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_128x128)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_128x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x128)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x32)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x16)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_32x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_32x32)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_32x16)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_32x8)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_16x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_16x32)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_16x16)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_16x8)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_8x16)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_8x8)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_4x16)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_4x8)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_8x32)
#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_8x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x8)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_4x64)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_64x4)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_4x32)
MAKE_SDSF_SKIP_SAD_WRAPPER(aom_highbd_sad_skip_32x4)

MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_256x256x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_256x128x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_128x256x4d)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_128x128x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_128x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x128x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x32x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x16x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_32x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_32x32x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_32x16x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_32x8x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_16x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_16x32x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_16x16x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_16x8x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_8x16x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_8x8x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_4x16x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_4x8x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_8x32x4d)
#if CONFIG_EXT_RECUR_PARTITIONS
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_8x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x8x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_4x64x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_64x4x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_4x32x4d)
MAKE_SDSF_SKIP_SAD_4D_WRAPPER(aom_highbd_sad_skip_32x4x4d)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

static AOM_INLINE void highbd_set_var_fns(AV1_COMP *const cpi) {
  AV1_COMMON *const cm = &cpi->common;
  switch (cm->seq_params.bit_depth) {
    case AOM_BITS_8:
      HIGHBD_BFP_WRAPPER(64, 16, 8)
      HIGHBD_BFP_WRAPPER(16, 64, 8)
      HIGHBD_BFP_WRAPPER(32, 8, 8)
      HIGHBD_BFP_WRAPPER(8, 32, 8)
      HIGHBD_BFP_WRAPPER(16, 4, 8)
      HIGHBD_BFP_WRAPPER(4, 16, 8)
      HIGHBD_BFP_WRAPPER(32, 16, 8)
      HIGHBD_BFP_WRAPPER(16, 32, 8)
      HIGHBD_BFP_WRAPPER(64, 32, 8)
      HIGHBD_BFP_WRAPPER(32, 64, 8)
      HIGHBD_BFP_WRAPPER(32, 32, 8)
      HIGHBD_BFP_WRAPPER(64, 64, 8)
      HIGHBD_BFP_WRAPPER(16, 16, 8)
      HIGHBD_BFP_WRAPPER(16, 8, 8)
      HIGHBD_BFP_WRAPPER(8, 16, 8)
      HIGHBD_BFP_WRAPPER(8, 8, 8)
      HIGHBD_BFP_WRAPPER(8, 4, 8)
      HIGHBD_BFP_WRAPPER(4, 8, 8)
      HIGHBD_BFP_WRAPPER(4, 4, 8)
      HIGHBD_BFP_WRAPPER(128, 128, 8)
      HIGHBD_BFP_WRAPPER(128, 64, 8)
      HIGHBD_BFP_WRAPPER(64, 128, 8)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_BFP_WRAPPER(64, 8, 8)
      HIGHBD_BFP_WRAPPER(8, 64, 8)
      HIGHBD_BFP_WRAPPER(32, 4, 8)
      HIGHBD_BFP_WRAPPER(4, 32, 8)
      HIGHBD_BFP_WRAPPER(64, 4, 8)
      HIGHBD_BFP_WRAPPER(4, 64, 8)
      HIGHBD_BFP_WRAPPER(128, 256, 8)
      HIGHBD_BFP_WRAPPER(256, 128, 8)
      HIGHBD_BFP_WRAPPER(256, 256, 8)

      HIGHBD_MBFP_WRAPPER(256, 256, 8)
      HIGHBD_MBFP_WRAPPER(256, 128, 8)
      HIGHBD_MBFP_WRAPPER(128, 256, 8)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(128, 128, 8)
      HIGHBD_MBFP_WRAPPER(128, 64, 8)
      HIGHBD_MBFP_WRAPPER(64, 128, 8)
      HIGHBD_MBFP_WRAPPER(64, 64, 8)
      HIGHBD_MBFP_WRAPPER(64, 32, 8)
      HIGHBD_MBFP_WRAPPER(32, 64, 8)
      HIGHBD_MBFP_WRAPPER(32, 32, 8)
      HIGHBD_MBFP_WRAPPER(32, 16, 8)
      HIGHBD_MBFP_WRAPPER(16, 32, 8)
      HIGHBD_MBFP_WRAPPER(16, 16, 8)
      HIGHBD_MBFP_WRAPPER(8, 16, 8)
      HIGHBD_MBFP_WRAPPER(16, 8, 8)
      HIGHBD_MBFP_WRAPPER(8, 8, 8)
      HIGHBD_MBFP_WRAPPER(4, 8, 8)
      HIGHBD_MBFP_WRAPPER(8, 4, 8)
      HIGHBD_MBFP_WRAPPER(4, 4, 8)
      HIGHBD_MBFP_WRAPPER(64, 16, 8)
      HIGHBD_MBFP_WRAPPER(16, 64, 8)
      HIGHBD_MBFP_WRAPPER(32, 8, 8)
      HIGHBD_MBFP_WRAPPER(8, 32, 8)
      HIGHBD_MBFP_WRAPPER(16, 4, 8)
      HIGHBD_MBFP_WRAPPER(4, 16, 8)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(64, 8, 8)
      HIGHBD_MBFP_WRAPPER(8, 64, 8)
      HIGHBD_MBFP_WRAPPER(32, 4, 8)
      HIGHBD_MBFP_WRAPPER(4, 32, 8)
      HIGHBD_MBFP_WRAPPER(64, 4, 8)
      HIGHBD_MBFP_WRAPPER(4, 64, 8)
      HIGHBD_SDSFP_WRAPPER(256, 256, 8)
      HIGHBD_SDSFP_WRAPPER(256, 128, 8)
      HIGHBD_SDSFP_WRAPPER(128, 256, 8)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(128, 128, 8);
      HIGHBD_SDSFP_WRAPPER(128, 64, 8);
      HIGHBD_SDSFP_WRAPPER(64, 128, 8);
      HIGHBD_SDSFP_WRAPPER(64, 64, 8);
      HIGHBD_SDSFP_WRAPPER(64, 32, 8);
      HIGHBD_SDSFP_WRAPPER(64, 16, 8);
      HIGHBD_SDSFP_WRAPPER(32, 64, 8);
      HIGHBD_SDSFP_WRAPPER(32, 32, 8);
      HIGHBD_SDSFP_WRAPPER(32, 16, 8);
      HIGHBD_SDSFP_WRAPPER(32, 8, 8);
      HIGHBD_SDSFP_WRAPPER(16, 64, 8);
      HIGHBD_SDSFP_WRAPPER(16, 32, 8);
      HIGHBD_SDSFP_WRAPPER(16, 16, 8);
      HIGHBD_SDSFP_WRAPPER(16, 8, 8);
      HIGHBD_SDSFP_WRAPPER(8, 16, 8);
      HIGHBD_SDSFP_WRAPPER(8, 8, 8);
      HIGHBD_SDSFP_WRAPPER(4, 16, 8);
      HIGHBD_SDSFP_WRAPPER(4, 8, 8);
      HIGHBD_SDSFP_WRAPPER(8, 32, 8);
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(64, 8, 8)
      HIGHBD_SDSFP_WRAPPER(8, 64, 8)
      HIGHBD_SDSFP_WRAPPER(32, 4, 8)
      HIGHBD_SDSFP_WRAPPER(4, 32, 8)
      HIGHBD_SDSFP_WRAPPER(64, 4, 8)
      HIGHBD_SDSFP_WRAPPER(4, 64, 8)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;

    case AOM_BITS_10:
      HIGHBD_BFP_WRAPPER(64, 16, 10)
      HIGHBD_BFP_WRAPPER(16, 64, 10)
      HIGHBD_BFP_WRAPPER(32, 8, 10)
      HIGHBD_BFP_WRAPPER(8, 32, 10)
      HIGHBD_BFP_WRAPPER(16, 4, 10)
      HIGHBD_BFP_WRAPPER(4, 16, 10)
      HIGHBD_BFP_WRAPPER(32, 16, 10)
      HIGHBD_BFP_WRAPPER(16, 32, 10)
      HIGHBD_BFP_WRAPPER(64, 32, 10)
      HIGHBD_BFP_WRAPPER(32, 64, 10)
      HIGHBD_BFP_WRAPPER(32, 32, 10)
      HIGHBD_BFP_WRAPPER(64, 64, 10)
      HIGHBD_BFP_WRAPPER(16, 16, 10)
      HIGHBD_BFP_WRAPPER(16, 8, 10)
      HIGHBD_BFP_WRAPPER(8, 16, 10)
      HIGHBD_BFP_WRAPPER(8, 8, 10)
      HIGHBD_BFP_WRAPPER(8, 4, 10)
      HIGHBD_BFP_WRAPPER(4, 8, 10)
      HIGHBD_BFP_WRAPPER(4, 4, 10)
      HIGHBD_BFP_WRAPPER(128, 128, 10)
      HIGHBD_BFP_WRAPPER(128, 64, 10)
      HIGHBD_BFP_WRAPPER(64, 128, 10)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_BFP_WRAPPER(64, 8, 10)
      HIGHBD_BFP_WRAPPER(8, 64, 10)
      HIGHBD_BFP_WRAPPER(32, 4, 10)
      HIGHBD_BFP_WRAPPER(4, 32, 10)
      HIGHBD_BFP_WRAPPER(64, 4, 10)
      HIGHBD_BFP_WRAPPER(4, 64, 10)
      HIGHBD_BFP_WRAPPER(128, 256, 10)
      HIGHBD_BFP_WRAPPER(256, 128, 10)
      HIGHBD_BFP_WRAPPER(256, 256, 10)

      HIGHBD_MBFP_WRAPPER(256, 256, 10)
      HIGHBD_MBFP_WRAPPER(256, 128, 10)
      HIGHBD_MBFP_WRAPPER(128, 256, 10)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(128, 128, 10)
      HIGHBD_MBFP_WRAPPER(128, 64, 10)
      HIGHBD_MBFP_WRAPPER(64, 128, 10)
      HIGHBD_MBFP_WRAPPER(64, 64, 10)
      HIGHBD_MBFP_WRAPPER(64, 32, 10)
      HIGHBD_MBFP_WRAPPER(32, 64, 10)
      HIGHBD_MBFP_WRAPPER(32, 32, 10)
      HIGHBD_MBFP_WRAPPER(32, 16, 10)
      HIGHBD_MBFP_WRAPPER(16, 32, 10)
      HIGHBD_MBFP_WRAPPER(16, 16, 10)
      HIGHBD_MBFP_WRAPPER(8, 16, 10)
      HIGHBD_MBFP_WRAPPER(16, 8, 10)
      HIGHBD_MBFP_WRAPPER(8, 8, 10)
      HIGHBD_MBFP_WRAPPER(4, 8, 10)
      HIGHBD_MBFP_WRAPPER(8, 4, 10)
      HIGHBD_MBFP_WRAPPER(4, 4, 10)
      HIGHBD_MBFP_WRAPPER(64, 16, 10)
      HIGHBD_MBFP_WRAPPER(16, 64, 10)
      HIGHBD_MBFP_WRAPPER(32, 8, 10)
      HIGHBD_MBFP_WRAPPER(8, 32, 10)
      HIGHBD_MBFP_WRAPPER(16, 4, 10)
      HIGHBD_MBFP_WRAPPER(4, 16, 10)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(64, 8, 10)
      HIGHBD_MBFP_WRAPPER(8, 64, 10)
      HIGHBD_MBFP_WRAPPER(32, 4, 10)
      HIGHBD_MBFP_WRAPPER(4, 32, 10)
      HIGHBD_MBFP_WRAPPER(64, 4, 10)
      HIGHBD_MBFP_WRAPPER(4, 64, 10)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(256, 256, 10)
      HIGHBD_SDSFP_WRAPPER(256, 128, 10)
      HIGHBD_SDSFP_WRAPPER(128, 256, 10)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(128, 128, 10);
      HIGHBD_SDSFP_WRAPPER(128, 64, 10);
      HIGHBD_SDSFP_WRAPPER(64, 128, 10);
      HIGHBD_SDSFP_WRAPPER(64, 64, 10);
      HIGHBD_SDSFP_WRAPPER(64, 32, 10);
      HIGHBD_SDSFP_WRAPPER(64, 16, 10);
      HIGHBD_SDSFP_WRAPPER(32, 64, 10);
      HIGHBD_SDSFP_WRAPPER(32, 32, 10);
      HIGHBD_SDSFP_WRAPPER(32, 16, 10);
      HIGHBD_SDSFP_WRAPPER(32, 8, 10);
      HIGHBD_SDSFP_WRAPPER(16, 64, 10);
      HIGHBD_SDSFP_WRAPPER(16, 32, 10);
      HIGHBD_SDSFP_WRAPPER(16, 16, 10);
      HIGHBD_SDSFP_WRAPPER(16, 8, 10);
      HIGHBD_SDSFP_WRAPPER(8, 16, 10);
      HIGHBD_SDSFP_WRAPPER(8, 8, 10);
      HIGHBD_SDSFP_WRAPPER(4, 16, 10);
      HIGHBD_SDSFP_WRAPPER(4, 8, 10);
      HIGHBD_SDSFP_WRAPPER(8, 32, 10);
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(64, 8, 10)
      HIGHBD_SDSFP_WRAPPER(8, 64, 10)
      HIGHBD_SDSFP_WRAPPER(32, 4, 10)
      HIGHBD_SDSFP_WRAPPER(4, 32, 10)
      HIGHBD_SDSFP_WRAPPER(64, 4, 10)
      HIGHBD_SDSFP_WRAPPER(4, 64, 10)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;

    case AOM_BITS_12:
      HIGHBD_BFP_WRAPPER(64, 16, 12)
      HIGHBD_BFP_WRAPPER(16, 64, 12)
      HIGHBD_BFP_WRAPPER(32, 8, 12)
      HIGHBD_BFP_WRAPPER(8, 32, 12)
      HIGHBD_BFP_WRAPPER(16, 4, 12)
      HIGHBD_BFP_WRAPPER(4, 16, 12)
      HIGHBD_BFP_WRAPPER(32, 16, 12)
      HIGHBD_BFP_WRAPPER(16, 32, 12)
      HIGHBD_BFP_WRAPPER(64, 32, 12)
      HIGHBD_BFP_WRAPPER(32, 64, 12)
      HIGHBD_BFP_WRAPPER(32, 32, 12)
      HIGHBD_BFP_WRAPPER(64, 64, 12)
      HIGHBD_BFP_WRAPPER(16, 16, 12)
      HIGHBD_BFP_WRAPPER(16, 8, 12)
      HIGHBD_BFP_WRAPPER(8, 16, 12)
      HIGHBD_BFP_WRAPPER(8, 8, 12)
      HIGHBD_BFP_WRAPPER(8, 4, 12)
      HIGHBD_BFP_WRAPPER(4, 8, 12)
      HIGHBD_BFP_WRAPPER(4, 4, 12)
      HIGHBD_BFP_WRAPPER(128, 128, 12)
      HIGHBD_BFP_WRAPPER(128, 64, 12)
      HIGHBD_BFP_WRAPPER(64, 128, 12)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_BFP_WRAPPER(64, 8, 12)
      HIGHBD_BFP_WRAPPER(8, 64, 12)
      HIGHBD_BFP_WRAPPER(32, 4, 12)
      HIGHBD_BFP_WRAPPER(4, 32, 12)
      HIGHBD_BFP_WRAPPER(64, 4, 12)
      HIGHBD_BFP_WRAPPER(4, 64, 12)
      HIGHBD_BFP_WRAPPER(128, 256, 12)
      HIGHBD_BFP_WRAPPER(256, 128, 12)
      HIGHBD_BFP_WRAPPER(256, 256, 12)

      HIGHBD_MBFP_WRAPPER(128, 256, 12)
      HIGHBD_MBFP_WRAPPER(256, 128, 12)
      HIGHBD_MBFP_WRAPPER(256, 256, 12)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(128, 128, 12)
      HIGHBD_MBFP_WRAPPER(128, 64, 12)
      HIGHBD_MBFP_WRAPPER(64, 128, 12)
      HIGHBD_MBFP_WRAPPER(64, 64, 12)
      HIGHBD_MBFP_WRAPPER(64, 32, 12)
      HIGHBD_MBFP_WRAPPER(32, 64, 12)
      HIGHBD_MBFP_WRAPPER(32, 32, 12)
      HIGHBD_MBFP_WRAPPER(32, 16, 12)
      HIGHBD_MBFP_WRAPPER(16, 32, 12)
      HIGHBD_MBFP_WRAPPER(16, 16, 12)
      HIGHBD_MBFP_WRAPPER(8, 16, 12)
      HIGHBD_MBFP_WRAPPER(16, 8, 12)
      HIGHBD_MBFP_WRAPPER(8, 8, 12)
      HIGHBD_MBFP_WRAPPER(4, 8, 12)
      HIGHBD_MBFP_WRAPPER(8, 4, 12)
      HIGHBD_MBFP_WRAPPER(4, 4, 12)
      HIGHBD_MBFP_WRAPPER(64, 16, 12)
      HIGHBD_MBFP_WRAPPER(16, 64, 12)
      HIGHBD_MBFP_WRAPPER(32, 8, 12)
      HIGHBD_MBFP_WRAPPER(8, 32, 12)
      HIGHBD_MBFP_WRAPPER(16, 4, 12)
      HIGHBD_MBFP_WRAPPER(4, 16, 12)
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_MBFP_WRAPPER(64, 8, 12)
      HIGHBD_MBFP_WRAPPER(8, 64, 12)
      HIGHBD_MBFP_WRAPPER(32, 4, 12)
      HIGHBD_MBFP_WRAPPER(4, 32, 12)
      HIGHBD_MBFP_WRAPPER(64, 4, 12)
      HIGHBD_MBFP_WRAPPER(4, 64, 12)
      HIGHBD_SDSFP_WRAPPER(256, 256, 12)
      HIGHBD_SDSFP_WRAPPER(256, 128, 12)
      HIGHBD_SDSFP_WRAPPER(128, 256, 12)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(128, 128, 12);
      HIGHBD_SDSFP_WRAPPER(128, 64, 12);
      HIGHBD_SDSFP_WRAPPER(64, 128, 12);
      HIGHBD_SDSFP_WRAPPER(64, 64, 12);
      HIGHBD_SDSFP_WRAPPER(64, 32, 12);
      HIGHBD_SDSFP_WRAPPER(64, 16, 12);
      HIGHBD_SDSFP_WRAPPER(32, 64, 12);
      HIGHBD_SDSFP_WRAPPER(32, 32, 12);
      HIGHBD_SDSFP_WRAPPER(32, 16, 12);
      HIGHBD_SDSFP_WRAPPER(32, 8, 12);
      HIGHBD_SDSFP_WRAPPER(16, 64, 12);
      HIGHBD_SDSFP_WRAPPER(16, 32, 12);
      HIGHBD_SDSFP_WRAPPER(16, 16, 12);
      HIGHBD_SDSFP_WRAPPER(16, 8, 12);
      HIGHBD_SDSFP_WRAPPER(8, 16, 12);
      HIGHBD_SDSFP_WRAPPER(8, 8, 12);
      HIGHBD_SDSFP_WRAPPER(4, 16, 12);
      HIGHBD_SDSFP_WRAPPER(4, 8, 12);
      HIGHBD_SDSFP_WRAPPER(8, 32, 12);
#if CONFIG_EXT_RECUR_PARTITIONS
      HIGHBD_SDSFP_WRAPPER(64, 8, 12)
      HIGHBD_SDSFP_WRAPPER(8, 64, 12)
      HIGHBD_SDSFP_WRAPPER(32, 4, 12)
      HIGHBD_SDSFP_WRAPPER(4, 32, 12)
      HIGHBD_SDSFP_WRAPPER(64, 4, 12)
      HIGHBD_SDSFP_WRAPPER(4, 64, 12)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;

    default:
      assert(0 &&
             "cm->seq_params.bit_depth should be AOM_BITS_8, "
             "AOM_BITS_10 or AOM_BITS_12");
  }
}

static AOM_INLINE void copy_frame_prob_info(AV1_COMP *cpi) {
  FrameProbInfo *const frame_probs = &cpi->frame_probs;
  if (cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats) {
    av1_copy(frame_probs->tx_type_probs, default_tx_type_probs);
  }
  if (cpi->sf.inter_sf.prune_warped_prob_thresh > 0 ||
      cpi->sf.inter_sf.prune_warpmv_prob_thresh > 0) {
    av1_copy(frame_probs->warped_probs, default_warped_probs);
  }
}

// Coding context that only needs to be restored when recode loop includes
// filtering (deblocking, CDEF, superres post-encode upscale and/or loop
// restoraton).
static AOM_INLINE void restore_extra_coding_context(AV1_COMP *cpi) {
  CODING_CONTEXT *const cc = &cpi->coding_context;
  AV1_COMMON *cm = &cpi->common;
  cm->lf = cc->lf;
  cm->cdef_info = cc->cdef_info;
  cpi->rc = cc->rc;
  cpi->mv_stats = cc->mv_stats;
  cm->features = cc->features;
}

static AOM_INLINE int equal_dimensions_and_border(const YV12_BUFFER_CONFIG *a,
                                                  const YV12_BUFFER_CONFIG *b) {
  return a->y_height == b->y_height && a->y_width == b->y_width &&
         a->uv_height == b->uv_height && a->uv_width == b->uv_width &&
         a->y_stride == b->y_stride && a->uv_stride == b->uv_stride &&
         a->border == b->border;
}

static AOM_INLINE int update_entropy(bool *ext_refresh_frame_context,
                                     bool *ext_refresh_frame_context_pending,
                                     bool update) {
  *ext_refresh_frame_context = update;
  *ext_refresh_frame_context_pending = 1;
  return 0;
}

static AOM_INLINE int combine_prior_with_tpl_boost(double min_factor,
                                                   double max_factor,
                                                   int prior_boost,
                                                   int tpl_boost,
                                                   int frames_to_key) {
  double factor = sqrt((double)frames_to_key);
  double range = max_factor - min_factor;
  factor = AOMMIN(factor, max_factor);
  factor = AOMMAX(factor, min_factor);
  factor -= min_factor;
  int boost =
      (int)((factor * prior_boost + (range - factor) * tpl_boost) / range);
  return boost;
}

static AOM_INLINE void set_size_independent_vars(AV1_COMP *cpi) {
  int i;
  AV1_COMMON *const cm = &cpi->common;
  for (i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    cm->global_motion[i] = default_warp_params;
  }
  cpi->gm_info.search_done = 0;

  av1_set_speed_features_framesize_independent(cpi, cpi->speed);
  av1_set_rd_speed_thresholds(cpi);
  cm->features.interp_filter = SWITCHABLE;
  cm->features.opfl_refine_type = REFINE_SWITCHABLE;
}

static AOM_INLINE void release_scaled_references(AV1_COMP *cpi) {
  // TODO(isbs): only refresh the necessary frames, rather than all of them
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    RefCntBuffer *const buf = cpi->scaled_ref_buf[i];
    if (buf != NULL) {
      --buf->ref_count;
      cpi->scaled_ref_buf[i] = NULL;
    }
  }
}

static AOM_INLINE void restore_all_coding_context(AV1_COMP *cpi) {
  restore_extra_coding_context(cpi);
  if (!frame_is_intra_only(&cpi->common)) release_scaled_references(cpi);
}

// Refresh reference frame buffers according to refresh_frame_flags.
static AOM_INLINE void refresh_reference_frames(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;

  // Reset display order hint for forward keyframe
  // TODO(sarahparker): Find a way to also reset order_hint without causing
  // a crash.
  if (cm->cur_frame->frame_type == KEY_FRAME && cm->show_existing_frame) {
    cm->cur_frame->display_order_hint = 0;
  }
#if CONFIG_BRU
  if (!cm->bru.enabled) {
#endif  // CONFIG_BRU
    // All buffers are refreshed for shown keyframes and S-frames.
    for (int ref_frame = 0; ref_frame < cm->seq_params.ref_frames;
         ref_frame++) {
      if (((cm->current_frame.refresh_frame_flags >> ref_frame) & 1) == 1) {
        assign_frame_buffer_p(&cm->ref_frame_map[ref_frame], cm->cur_frame);
      }
    }
#if CONFIG_BRU
  }
#endif  // CONFIG_BRU
}

static AOM_INLINE void update_subgop_stats(
    const GF_GROUP *const gf_group, SubGOPStatsEnc *const subgop_stats,
    const OrderHintInfo *const order_hint_info, int key_freq_max,
    unsigned int enable_subgop_stats) {
  (void)key_freq_max;
  if (!enable_subgop_stats) return;
  if (order_hint_info->enable_order_hint) {
    int max_order_hint = 1 << (order_hint_info->order_hint_bits_minus_1 + 1);
    (void)max_order_hint;
    assert(key_freq_max <= max_order_hint + 1);
  }
  subgop_stats->pyramid_level[subgop_stats->stat_count] =
      gf_group->layer_depth[gf_group->index];
  subgop_stats->is_filtered[subgop_stats->stat_count] =
      gf_group->is_filtered[gf_group->index];
  assert(subgop_stats->stat_count < MAX_SUBGOP_STATS_SIZE);
  subgop_stats->stat_count++;
}

static AOM_INLINE void update_subgop_ref_stats(
    SubGOPStatsEnc *const subgop_stats, unsigned int enable_subgop_stats,
    int ref_frame, int is_valid_ref_frame, int pyramid_level, int disp_order,
    int num_references) {
  if (!enable_subgop_stats) return;
  assert(subgop_stats->stat_count < MAX_SUBGOP_STATS_SIZE);
  int stat_idx = subgop_stats->stat_count - 1;
  subgop_stats->is_valid_ref_frame[stat_idx][ref_frame] = is_valid_ref_frame;
  subgop_stats->ref_frame_pyr_level[stat_idx][ref_frame] = pyramid_level;
  subgop_stats->ref_frame_disp_order[stat_idx][ref_frame] = disp_order;
  subgop_stats->num_references[stat_idx] = num_references;
}

void av1_update_film_grain_parameters(struct AV1_COMP *cpi,
                                      const AV1EncoderConfig *oxcf);

void av1_scale_references(AV1_COMP *cpi, const InterpFilter filter,
                          const int phase, const int use_optimized_scaler);

void av1_setup_frame(AV1_COMP *cpi);

BLOCK_SIZE av1_select_sb_size(const AV1_COMP *const cpi);
#if CONFIG_BRU
void set_ard_active_map(AV1_COMP *cpi);
#endif  // CONFIG_BRU

void av1_apply_active_map(AV1_COMP *cpi);

void av1_determine_sc_tools_with_encoding(AV1_COMP *cpi, const int q_orig);

void av1_set_size_dependent_vars(AV1_COMP *cpi, int *q, int *bottom_index,
                                 int *top_index);

void av1_finalize_encoded_frame(AV1_COMP *const cpi);

int av1_is_integer_mv(const YV12_BUFFER_CONFIG *cur_picture,
                      const YV12_BUFFER_CONFIG *last_picture,
                      ForceIntegerMVInfo *const force_intpel_info);

void av1_set_mb_ssim_rdmult_scaling(AV1_COMP *cpi);

void av1_save_all_coding_context(AV1_COMP *cpi);
#if CONFIG_BRU
void active_region_detection(AV1_COMP *cpi,
                             const YV12_BUFFER_CONFIG *cur_picture,
                             const YV12_BUFFER_CONFIG *last_picture);
#endif  // CONFIG_BRU

static AOM_INLINE void av1_set_tile_info(AV1_COMMON *const cm,
                                         const TileConfig *const tile_cfg) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  CommonTileParams *const tiles = &cm->tiles;
  int i, start_sb;

  av1_get_tile_limits(cm);

  // configure tile columns
  if (tile_cfg->tile_width_count == 0 || tile_cfg->tile_height_count == 0) {
    tiles->uniform_spacing = 1;
    tiles->log2_cols = AOMMAX(tile_cfg->tile_columns, tiles->min_log2_cols);
    tiles->log2_cols = AOMMIN(tiles->log2_cols, tiles->max_log2_cols);
  } else {
    int mi_cols = ALIGN_POWER_OF_TWO(mi_params->mi_cols, cm->mib_size_log2);
    int sb_cols = mi_cols >> cm->mib_size_log2;
    int size_sb, j = 0;
    tiles->uniform_spacing = 0;
    for (i = 0, start_sb = 0; start_sb < sb_cols && i < MAX_TILE_COLS; i++) {
      tiles->col_start_sb[i] = start_sb;
      size_sb = tile_cfg->tile_widths[j++];
      if (j >= tile_cfg->tile_width_count) j = 0;
      start_sb += AOMMIN(size_sb, tiles->max_width_sb);
    }
    tiles->cols = i;
    tiles->col_start_sb[i] = sb_cols;
  }
  av1_calculate_tile_cols(cm, mi_params->mi_rows, mi_params->mi_cols, tiles);

  // configure tile rows
  if (tiles->uniform_spacing) {
    tiles->log2_rows = AOMMAX(tile_cfg->tile_rows, tiles->min_log2_rows);
    tiles->log2_rows = AOMMIN(tiles->log2_rows, tiles->max_log2_rows);
  } else {
    int mi_rows = ALIGN_POWER_OF_TWO(mi_params->mi_rows, cm->mib_size_log2);
    int sb_rows = mi_rows >> cm->mib_size_log2;
    int size_sb, j = 0;
    for (i = 0, start_sb = 0; start_sb < sb_rows && i < MAX_TILE_ROWS; i++) {
      tiles->row_start_sb[i] = start_sb;
      size_sb = tile_cfg->tile_heights[j++];
      if (j >= tile_cfg->tile_height_count) j = 0;
      start_sb += AOMMIN(size_sb, tiles->max_height_sb);
    }
    tiles->rows = i;
    tiles->row_start_sb[i] = sb_rows;
  }
  av1_calculate_tile_rows(cm, mi_params->mi_rows, tiles);
}

void reallocate_sb_size_dependent_buffers(AV1_COMP *cpi);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENCODER_UTILS_H_
