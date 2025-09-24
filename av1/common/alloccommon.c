/*
 *
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

#include "config/aom_config.h"

#include "aom_mem/aom_mem.h"

#include "av1/common/alloccommon.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/cdef_block.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"

int av1_get_MBs(int width, int height) {
  const int aligned_width = ALIGN_POWER_OF_TWO(width, 3);
  const int aligned_height = ALIGN_POWER_OF_TWO(height, 3);
  const int mi_cols = aligned_width >> MI_SIZE_LOG2;
  const int mi_rows = aligned_height >> MI_SIZE_LOG2;

  const int mb_cols = (mi_cols + 2) >> 2;
  const int mb_rows = (mi_rows + 2) >> 2;
  return mb_rows * mb_cols;
}

void av1_free_ref_frame_buffers(BufferPool *pool) {
  int i;

  for (i = 0; i < FRAME_BUFFERS; ++i) {
    if (pool->frame_bufs[i].ref_count > 0 &&
        pool->frame_bufs[i].raw_frame_buffer.data != NULL) {
      pool->release_fb_cb(pool->cb_priv, &pool->frame_bufs[i].raw_frame_buffer);
      pool->frame_bufs[i].raw_frame_buffer.data = NULL;
      pool->frame_bufs[i].raw_frame_buffer.size = 0;
      pool->frame_bufs[i].raw_frame_buffer.priv = NULL;
      pool->frame_bufs[i].ref_count = 0;
    }
    aom_free(pool->frame_bufs[i].mvs);
    pool->frame_bufs[i].mvs = NULL;
    aom_free(pool->frame_bufs[i].seg_map);
    pool->frame_bufs[i].seg_map = NULL;
    aom_free_frame_buffer(&pool->frame_bufs[i].buf);

    for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
      if (pool->frame_bufs[i].ccso_info.sb_filter_control[plane] != NULL) {
        aom_free(pool->frame_bufs[i].ccso_info.sb_filter_control[plane]);
        pool->frame_bufs[i].ccso_info.sb_filter_control[plane] = NULL;
      }
    }

    for (int p = 0; p < MAX_MB_PLANE; ++p) {
      av1_free_restoration_struct(&pool->frame_bufs[i].rst_info[p]);
    }
  }
}

void av1_free_cdef_linebuf(AV1_COMMON *const cm) {
  if (cm->cdef_info.srcbuf != NULL) aom_free(cm->cdef_info.srcbuf);
  cm->cdef_info.srcbuf = NULL;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    if (cm->cdef_info.linebuf[plane] != NULL)
      aom_free(cm->cdef_info.linebuf[plane]);
    cm->cdef_info.linebuf[plane] = NULL;

    if (cm->cdef_info.colbuf[plane] != NULL)
      aom_free(cm->cdef_info.colbuf[plane]);
    cm->cdef_info.colbuf[plane] = NULL;
  }
}

void av1_alloc_cdef_linebuf(AV1_COMMON *const cm) {
  CdefInfo *cdef_info = &cm->cdef_info;
  const int num_planes = av1_num_planes(cm);
  const int luma_stride =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols << MI_SIZE_LOG2, 4);
  // Check for configuration change
  const int is_sub_sampling_changed =
      (cdef_info->allocated_subsampling_x != cm->seq_params.subsampling_x ||
       cdef_info->allocated_subsampling_y != cm->seq_params.subsampling_y);
  const int is_frame_scaled =
      cdef_info->allocated_mi_cols != cm->mi_params.mi_cols;
  const int is_cdef_flag_changed =
      cdef_info->prev_cdef_enable_flag != cm->seq_params.enable_cdef;
  const int is_large_scale_tile_changed =
      cdef_info->prev_large_scale_tile_flag != cm->tiles.large_scale;
  const int is_num_planes_changed = cdef_info->prev_num_planes != num_planes;
  // num-bufs=3 represents ping-pong buffers for top linebuf,
  // followed by bottom linebuf.
  // ping-pong is to avoid top linebuf over-write by consecutive row.
  int num_bufs = 3;

  if (is_frame_scaled || is_sub_sampling_changed || is_cdef_flag_changed ||
      is_large_scale_tile_changed || is_num_planes_changed)
    av1_free_cdef_linebuf(cm);

  // Store configuration to check change in configuration
  cdef_info->allocated_mi_cols = cm->mi_params.mi_cols;
  cdef_info->allocated_subsampling_x = cm->seq_params.subsampling_x;
  cdef_info->allocated_subsampling_y = cm->seq_params.subsampling_y;
  cdef_info->prev_cdef_enable_flag = cm->seq_params.enable_cdef;
  cdef_info->prev_large_scale_tile_flag = cm->tiles.large_scale;
  cdef_info->prev_num_planes = num_planes;

  if (!cm->seq_params.enable_cdef && cm->tiles.large_scale) return;

  for (int plane = 0; plane < num_planes; plane++) {
    if (cdef_info->linebuf[plane] == NULL) {
      const int stride =
          luma_stride >>
          (plane == AOM_PLANE_Y ? 0 : cm->seq_params.subsampling_x);
      CHECK_MEM_ERROR(cm, cdef_info->linebuf[plane],
                      aom_malloc(sizeof(*cdef_info->linebuf) * num_bufs *
                                 (CDEF_VBORDER << 1) * stride));
    }
  }

  if (cm->cdef_info.srcbuf == NULL)
    CHECK_MEM_ERROR(
        cm, cm->cdef_info.srcbuf,
        aom_malloc(sizeof(*cm->cdef_info.srcbuf) * CDEF_INBUF_SIZE));

  for (int plane = 0; plane < num_planes; plane++) {
    const int shift = plane == AOM_PLANE_Y ? 0 : cm->seq_params.subsampling_x;
    const int block_height =
        (CDEF_BLOCKSIZE << (MI_SIZE_LOG2 - shift)) * 2 * CDEF_VBORDER;

    if (cm->cdef_info.colbuf[plane] == NULL)
      CHECK_MEM_ERROR(cm, cm->cdef_info.colbuf[plane],
                      aom_malloc(sizeof(*cm->cdef_info.colbuf[plane]) *
                                 block_height * CDEF_HBORDER));
  }
}

// Assumes cm->rst_info[p].restoration_unit_size is already initialized
void av1_alloc_restoration_buffers(AV1_COMMON *cm) {
  const int num_planes = av1_num_planes(cm);
  for (int p = 0; p < num_planes; ++p)
    av1_alloc_restoration_struct(cm, &cm->rst_info[p], p > 0);

  if (cm->frame_filter_dictionary == NULL) {
    allocate_frame_filter_dictionary(cm);
    translate_pcwiener_filters_to_wienerns(cm);
  }

  if (cm->rlbs == NULL) {
    CHECK_MEM_ERROR(cm, cm->rlbs, aom_malloc(sizeof(RestorationLineBuffers)));
  }

  av1_alloc_restoration_boundary_buffers(cm, num_planes);
}

void av1_alloc_restoration_boundary_buffers(struct AV1Common *cm,
                                            int num_planes) {
  // For striped loop restoration, we divide each row of tiles into "stripes",
  // of height 64 luma pixels but with an offset by RESTORATION_UNIT_OFFSET
  // luma pixels to match the output from CDEF. We will need to store 2 *
  // RESTORATION_CTX_VERT lines of data for each stripe, and also need to be
  // able to quickly answer the question "Where is the <n>'th stripe for tile
  // row <m>?" To make that efficient, we generate the rst_last_stripe array.

  AV1PixelRect tile_rect;
  int num_stripes = 0;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  if (cm->seq_params.disable_loopfilters_across_tiles) {
    TileInfo tile_info;
    for (int tr = 0; tr < cm->tiles.rows; ++tr) {
      av1_tile_init(&tile_info, cm, tr, 0);
      tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
      const int tile_h = tile_rect.bottom - tile_rect.top;
      num_stripes += av1_lr_count_stripes_in_tile(tile_h, 0);
    }
  } else {
    tile_rect = av1_whole_frame_rect(cm, 0);
    const int tile_h = tile_rect.bottom - tile_rect.top;
    num_stripes = av1_lr_count_stripes_in_tile(tile_h, 0);
  }
#else
  tile_rect = av1_whole_frame_rect(cm, 0);
  const int tile_h = tile_rect.bottom - tile_rect.top;
  num_stripes = av1_lr_count_stripes_in_tile(tile_h, 0);
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  // int num_stripes = cm->rst_info[0].vert_stripes_per_frame;

  // Now we need to allocate enough space to store the line buffers for the
  // stripes
#if CONFIG_F054_PIC_BOUNDARY
  const int frame_w = cm->mi_params.mi_cols << MI_SIZE_LOG2;
#else
  const int frame_w = cm->width;
#endif  // CONFIG_F054_PIC_BOUNDARY

  for (int p = 0; p < num_planes; ++p) {
    const int is_uv = p > 0;
    const int ss_x = is_uv && cm->seq_params.subsampling_x;
    const int plane_w =
        ((frame_w + ss_x) >> ss_x) + 2 * RESTORATION_BORDER_HORZ;
    const int stride = ALIGN_POWER_OF_TWO(plane_w, 5);
    const int buf_size = num_stripes * stride * RESTORATION_CTX_VERT << 1;
    RestorationStripeBoundaries *boundaries = &cm->rst_info[p].boundaries;
    boundaries->num_stripes = num_stripes;

    if (buf_size != boundaries->stripe_boundary_size ||
        boundaries->stripe_boundary_above == NULL ||
        boundaries->stripe_boundary_below == NULL) {
      aom_free(boundaries->stripe_boundary_above);
      aom_free(boundaries->stripe_boundary_below);
      CHECK_MEM_ERROR(cm, boundaries->stripe_boundary_above,
                      aom_memalign(32, buf_size));
      CHECK_MEM_ERROR(cm, boundaries->stripe_boundary_below,
                      aom_memalign(32, buf_size));

      boundaries->stripe_boundary_size = buf_size;
    }
    boundaries->stripe_boundary_stride = stride;
  }
}

void av1_free_restoration_buffers(AV1_COMMON *cm) {
  int p;
  for (p = 0; p < MAX_MB_PLANE; ++p)
    av1_free_restoration_struct(&cm->rst_info[p]);
  aom_free(cm->rlbs);
  cm->rlbs = NULL;
  for (p = 0; p < MAX_MB_PLANE; ++p) {
    RestorationStripeBoundaries *boundaries = &cm->rst_info[p].boundaries;
    aom_free(boundaries->stripe_boundary_above);
    aom_free(boundaries->stripe_boundary_below);
    boundaries->stripe_boundary_above = NULL;
    boundaries->stripe_boundary_below = NULL;
  }
  free_frame_filter_dictionary(cm);
  aom_free_frame_buffer(&cm->rst_frame);
}

void av1_free_above_context_buffers(CommonContexts *above_contexts) {
  int i;
  const int num_planes = above_contexts->num_planes;

  for (int tile_row = 0; tile_row < above_contexts->num_tile_rows; tile_row++) {
    for (i = 0; i < num_planes; i++) {
      aom_free(above_contexts->entropy[i][tile_row]);
      above_contexts->entropy[i][tile_row] = NULL;
      aom_free(above_contexts->partition[i][tile_row]);
      above_contexts->partition[i][tile_row] = NULL;
    }
  }
  for (i = 0; i < num_planes; i++) {
    aom_free(above_contexts->entropy[i]);
    above_contexts->entropy[i] = NULL;
    aom_free(above_contexts->partition[i]);
    above_contexts->partition[i] = NULL;
  }

  above_contexts->num_tile_rows = 0;
  above_contexts->num_mi_cols = 0;
  above_contexts->num_planes = 0;
}

static void free_sbi(CommonSBInfoParams *sbi_params) {
  for (int i = 0; i < sbi_params->sbi_alloc_size; ++i) {
    av1_free_ptree_recursive(sbi_params->sbi_grid_base[i].ptree_root[0]);
    av1_free_ptree_recursive(sbi_params->sbi_grid_base[i].ptree_root[1]);
  }

  aom_free(sbi_params->sbi_grid_base);
  sbi_params->sbi_grid_base = NULL;
  sbi_params->sbi_alloc_size = 0;
}

void av1_free_context_buffers(AV1_COMMON *cm) {
  cm->mi_params.free_mi(&cm->mi_params);
  free_sbi(&cm->sbi_params);

  av1_free_above_context_buffers(&cm->above_contexts);
}

int av1_alloc_above_context_buffers(CommonContexts *above_contexts,
                                    int num_tile_rows, int num_mi_cols,
                                    int num_planes) {
  const int aligned_mi_cols =
      ALIGN_POWER_OF_TWO(num_mi_cols, MAX_MIB_SIZE_LOG2);

  // Allocate above context buffers
  above_contexts->num_tile_rows = num_tile_rows;
  above_contexts->num_mi_cols = aligned_mi_cols;
  above_contexts->num_planes = num_planes;
  for (int plane_idx = 0; plane_idx < num_planes; plane_idx++) {
    above_contexts->entropy[plane_idx] = (ENTROPY_CONTEXT **)aom_calloc(
        num_tile_rows, sizeof(above_contexts->entropy[0]));
    if (!above_contexts->entropy[plane_idx]) return 1;
    above_contexts->partition[plane_idx] = (PARTITION_CONTEXT **)aom_calloc(
        num_tile_rows, sizeof(above_contexts->partition[plane_idx]));
    if (!above_contexts->partition[plane_idx]) return 1;
  }

  for (int tile_row = 0; tile_row < num_tile_rows; tile_row++) {
    for (int plane_idx = 0; plane_idx < num_planes; plane_idx++) {
      above_contexts->entropy[plane_idx][tile_row] =
          (ENTROPY_CONTEXT *)aom_calloc(
              aligned_mi_cols, sizeof(*above_contexts->entropy[0][tile_row]));
      if (!above_contexts->entropy[plane_idx][tile_row]) return 1;
      above_contexts->partition[plane_idx][tile_row] =
          (PARTITION_CONTEXT *)aom_calloc(
              aligned_mi_cols,
              sizeof(*above_contexts->partition[plane_idx][tile_row]));
      if (!above_contexts->partition[plane_idx][tile_row]) return 1;
    }
  }

  return 0;
}

// Allocate the dynamically allocated arrays in 'mi_params' assuming
// 'mi_params->set_mb_mi()' was already called earlier to initialize the rest of
// the struct members.
static int alloc_mi(CommonModeInfoParams *mi_params, AV1_COMMON *cm,
                    int height) {
  const int aligned_mi_rows = calc_mi_size(mi_params->mi_rows);
  const int mi_grid_size = mi_params->mi_stride * aligned_mi_rows;
  const int alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  const int alloc_mi_size =
      mi_params->mi_alloc_stride * (aligned_mi_rows / alloc_size_1d);

  if (mi_params->mi_alloc_size < alloc_mi_size ||
      mi_params->mi_grid_size < mi_grid_size) {
    mi_params->free_mi(mi_params);

    mi_params->mi_alloc =
        aom_calloc(alloc_mi_size, sizeof(*mi_params->mi_alloc));
    if (!mi_params->mi_alloc) return 1;
    mi_params->mi_alloc_size = alloc_mi_size;

    mi_params->mi_grid_base = (MB_MODE_INFO **)aom_calloc(
        mi_grid_size, sizeof(*mi_params->mi_grid_base));
    if (!mi_params->mi_grid_base) return 1;
    mi_params->mi_grid_size = mi_grid_size;
    av1_alloc_txk_skip_array(mi_params, cm);
    av1_alloc_class_id_array(mi_params, cm, height);
    mi_params->mi_alloc_sub =
        aom_calloc(alloc_mi_size, sizeof(*mi_params->mi_alloc_sub));
    if (!mi_params->mi_alloc_sub) return 1;
    mi_params->submi_grid_base = (SUBMB_INFO **)aom_calloc(
        mi_grid_size, sizeof(*mi_params->submi_grid_base));
    if (!mi_params->submi_grid_base) return 1;

    mi_params->tx_type_map =
        aom_calloc(mi_grid_size, sizeof(*mi_params->tx_type_map));
    if (!mi_params->tx_type_map) return 1;
    mi_params->cctx_type_map =
        aom_calloc(mi_grid_size, sizeof(*mi_params->cctx_type_map));
    if (!mi_params->cctx_type_map) return 1;
  } else {
    // Set only the strides corresponding to the current frame dims
    av1_set_txk_skip_array_stride(mi_params, cm);
    av1_set_class_id_array_stride(mi_params, cm, height);
  }

  return 0;
}

static void set_sb_si(AV1_COMMON *cm) {
  CommonSBInfoParams *const sbi_params = &cm->sbi_params;
  const int mib_size_log2 = cm->mib_size_log2;
  sbi_params->sb_cols =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols, mib_size_log2) >> mib_size_log2;
  sbi_params->sb_rows =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_rows, mib_size_log2) >> mib_size_log2;
  sbi_params->sbi_stride = cm->mi_params.mi_stride >> mib_size_log2;
}

static int alloc_sbi(CommonSBInfoParams *sbi_params) {
  const int sbi_size = sbi_params->sbi_stride * sbi_params->sb_rows;

  if (sbi_params->sbi_alloc_size < sbi_size) {
    free_sbi(sbi_params);
    sbi_params->sbi_grid_base =
        aom_calloc(sbi_size, sizeof(*sbi_params->sbi_grid_base));

    if (!sbi_params->sbi_grid_base) return 1;

    sbi_params->sbi_alloc_size = sbi_size;
    for (int i = 0; i < sbi_size; ++i) {
      sbi_params->sbi_grid_base[i].ptree_root[0] = NULL;
      sbi_params->sbi_grid_base[i].ptree_root[1] = NULL;
      sbi_params->sbi_grid_base[i].sb_active_mode = BRU_ACTIVE_SB;
    }
  }

  return 0;
}

int av1_alloc_superblock_info_buffers(AV1_COMMON *cm) {
  CommonSBInfoParams *const sbi_params = &cm->sbi_params;
  set_sb_si(cm);
  return alloc_sbi(sbi_params);
}

int av1_alloc_context_buffers(AV1_COMMON *cm, int width, int height) {
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  mi_params->set_mb_mi(mi_params, width, height);
  if (alloc_mi(mi_params, cm, height)) goto fail;

  if (av1_alloc_superblock_info_buffers(cm)) goto fail;

  return 0;

fail:
  // clear the mi_* values to force a realloc on resync
  mi_params->set_mb_mi(mi_params, 0, 0);
  av1_free_context_buffers(cm);
  return 1;
}

void av1_remove_common(AV1_COMMON *cm) {
  av1_free_context_buffers(cm);

  aom_free(cm->fc);
  cm->fc = NULL;
  aom_free(cm->default_frame_context);
  cm->default_frame_context = NULL;
}

void av1_init_mi_buffers(CommonModeInfoParams *mi_params) {
  mi_params->setup_mi(mi_params);
}
