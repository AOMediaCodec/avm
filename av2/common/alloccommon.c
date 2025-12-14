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

#include "config/avm_config.h"

#include "avm_mem/avm_mem.h"

#include "av2/common/alloccommon.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/cdef_block.h"
#include "av2/common/entropymode.h"
#include "av2/common/entropymv.h"
#include "av2/common/thread_common.h"

int av2_get_MBs(int width, int height) {
  const int aligned_width = ALIGN_POWER_OF_TWO(width, 3);
  const int aligned_height = ALIGN_POWER_OF_TWO(height, 3);
  const int mi_cols = aligned_width >> MI_SIZE_LOG2;
  const int mi_rows = aligned_height >> MI_SIZE_LOG2;

  const int mb_cols = (mi_cols + 2) >> 2;
  const int mb_rows = (mi_rows + 2) >> 2;
  return mb_rows * mb_cols;
}

void av2_free_ref_frame_buffers(BufferPool *pool) {
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
    avm_free(pool->frame_bufs[i].mvs);
    pool->frame_bufs[i].mvs = NULL;
    avm_free(pool->frame_bufs[i].seg_map);
    pool->frame_bufs[i].seg_map = NULL;
    avm_free_frame_buffer(&pool->frame_bufs[i].buf);

    for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
      if (pool->frame_bufs[i].ccso_info.sb_filter_control[plane] != NULL) {
        avm_free(pool->frame_bufs[i].ccso_info.sb_filter_control[plane]);
        pool->frame_bufs[i].ccso_info.sb_filter_control[plane] = NULL;
      }
    }

    for (int p = 0; p < MAX_MB_PLANE; ++p) {
      av2_free_restoration_struct(&pool->frame_bufs[i].rst_info[p]);
    }
  }
}

// Frees CDEF line buffers when their allocated and new size is not matching.
static INLINE void free_cdef_linebuf_conditional(
    AV2_COMMON *const cm, const size_t *new_linebuf_size) {
  CdefInfo *cdef_info = &cm->cdef_info;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    if (new_linebuf_size[plane] != cdef_info->allocated_linebuf_size[plane]) {
      avm_free(cdef_info->linebuf[plane]);
      cdef_info->linebuf[plane] = NULL;
    }
  }
}

// Frees CDEF source/column buffers if their allocated and new size is not
// matching.
static INLINE void free_cdef_bufs_conditional(AV2_COMMON *const cm,
                                              uint16_t **const colbuf,
                                              uint16_t **const srcbuf,
                                              const size_t *new_colbuf_size,
                                              const size_t new_srcbuf_size) {
  CdefInfo *cdef_info = &cm->cdef_info;
  if (new_srcbuf_size != cdef_info->allocated_srcbuf_size) {
    avm_free(*srcbuf);
    *srcbuf = NULL;
  }
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    if (new_colbuf_size[plane] != cdef_info->allocated_colbuf_size[plane]) {
      avm_free(colbuf[plane]);
      colbuf[plane] = NULL;
    }
  }
}

// Free and reset CDEF source and column buffers.
static INLINE void free_cdef_bufs(uint16_t **const colbuf,
                                  uint16_t **const srcbuf) {
  avm_free(*srcbuf);
  *srcbuf = NULL;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    avm_free(colbuf[plane]);
    colbuf[plane] = NULL;
  }
}

// Frees the memory and synchronization primitives allocated for CDEF row sync.
static INLINE void free_cdef_row_sync(AV2CdefRowSync **cdef_row_mt,
                                      const int num_mi_rows) {
  if (*cdef_row_mt == NULL) return;
#if CONFIG_MULTITHREAD
  for (int row_idx = 0; row_idx < num_mi_rows; row_idx++) {
    if ((*cdef_row_mt)[row_idx].row_mutex_ != NULL) {
      pthread_mutex_destroy((*cdef_row_mt)[row_idx].row_mutex_);
      avm_free((*cdef_row_mt)[row_idx].row_mutex_);
    }
    if ((*cdef_row_mt)[row_idx].row_cond_ != NULL) {
      pthread_cond_destroy((*cdef_row_mt)[row_idx].row_cond_);
      avm_free((*cdef_row_mt)[row_idx].row_cond_);
    }
  }
#else
  (void)num_mi_rows;
#endif  // CONFIG_MULTITHREAD
  avm_free(*cdef_row_mt);
  *cdef_row_mt = NULL;
}

void av2_free_cdef_buffers(AV2_COMMON *const cm,
                           AV2CdefWorkerData **cdef_worker,
                           AV2CdefSync *cdef_sync, int num_workers) {
  CdefInfo *cdef_info = &cm->cdef_info;
  const int num_mi_rows = cdef_info->allocated_mi_rows;

  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    avm_free(cdef_info->linebuf[plane]);
    cdef_info->linebuf[plane] = NULL;
  }
  // De-allocation of column buffer & source buffer (worker 0).
  free_cdef_bufs(cdef_info->colbuf, &cdef_info->srcbuf);

  if (num_workers < 2) return;
  if (*cdef_worker != NULL) {
    for (int idx = 1; idx < num_workers; idx++) {
      // De-allocation of column buffer & source buffer for remaining workers.
      free_cdef_bufs((*cdef_worker)[idx].colbuf, &(*cdef_worker)[idx].srcbuf);
    }
    avm_free(*cdef_worker);
    *cdef_worker = NULL;
  }
  free_cdef_row_sync(&cdef_sync->cdef_row_mt, num_mi_rows);
}

// Allocate CDEF line buffers for each plane if not already allocated.
static INLINE void alloc_cdef_linebuf(AV2_COMMON *const cm,
                                      uint16_t **const linebuf,
                                      int num_planes) {
  CdefInfo *cdef_info = &cm->cdef_info;
  for (int plane = 0; plane < num_planes; plane++) {
    if (linebuf[plane] == NULL)
      CHECK_MEM_ERROR(cm, linebuf[plane],
                      avm_malloc(cdef_info->allocated_linebuf_size[plane]));
  }
}

// Allocate CDEF source and column buffers.
static INLINE void alloc_cdef_bufs(AV2_COMMON *const cm, uint16_t **colbuf,
                                   uint16_t **srcbuf, const int num_planes) {
  CdefInfo *cdef_info = &cm->cdef_info;
  if (*srcbuf == NULL)
    CHECK_MEM_ERROR(cm, *srcbuf,
                    avm_memalign(16, cdef_info->allocated_srcbuf_size));

  for (int plane = 0; plane < num_planes; plane++) {
    if (colbuf[plane] == NULL)
      CHECK_MEM_ERROR(cm, colbuf[plane],
                      avm_malloc(cdef_info->allocated_colbuf_size[plane]));
  }
}

// Allocate and initialize row-level synchronization structure for CDEF.
static INLINE void alloc_cdef_row_sync(AV2_COMMON *const cm,
                                       AV2CdefRowSync **cdef_row_mt,
                                       const int num_mi_rows) {
  if (*cdef_row_mt != NULL) return;

  CHECK_MEM_ERROR(cm, *cdef_row_mt,
                  avm_calloc(num_mi_rows, sizeof(**cdef_row_mt)));
#if CONFIG_MULTITHREAD
  for (int row_idx = 0; row_idx < num_mi_rows; row_idx++) {
    CHECK_MEM_ERROR(cm, (*cdef_row_mt)[row_idx].row_mutex_,
                    avm_malloc(sizeof(*(*cdef_row_mt)[row_idx].row_mutex_)));
    pthread_mutex_init((*cdef_row_mt)[row_idx].row_mutex_, NULL);

    CHECK_MEM_ERROR(cm, (*cdef_row_mt)[row_idx].row_cond_,
                    avm_malloc(sizeof(*(*cdef_row_mt)[row_idx].row_cond_)));
    pthread_cond_init((*cdef_row_mt)[row_idx].row_cond_, NULL);
  }
#endif  // CONFIG_MULTITHREAD
}

void av2_alloc_cdef_buffers(AV2_COMMON *const cm,
                            AV2CdefWorkerData **cdef_worker,
                            AV2CdefSync *cdef_sync, int num_workers) {
  CdefInfo *cdef_info = &cm->cdef_info;
  size_t new_linebuf_size[MAX_MB_PLANE] = { 0 };
  size_t new_colbuf_size[MAX_MB_PLANE] = { 0 };
  size_t new_srcbuf_size = 0;
  const int num_planes = av2_num_planes(cm);
  const int luma_stride =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols << MI_SIZE_LOG2, 4);
  // Check for configuration change
  const int num_mi_rows =
      (cm->mi_params.mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  const int is_num_workers_changed =
      cdef_info->allocated_num_workers != num_workers;
  const int is_cdef_enabled = cm->seq_params.enable_cdef;
  // num-bufs=3 represents ping-pong buffers for top linebuf,
  // followed by bottom linebuf.
  // ping-pong is to avoid top linebuf over-write by consecutive row.
  int num_bufs = 3;
  if (num_workers > 1)
    num_bufs = (cm->mi_params.mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;

  if (is_cdef_enabled) {
    // Calculate src buffer size
    new_srcbuf_size = sizeof(*cdef_info->srcbuf) * CDEF_INBUF_SIZE;
    for (int plane = 0; plane < num_planes; plane++) {
      const int shift = plane == AVM_PLANE_Y ? 0 : cm->seq_params.subsampling_x;
      // Calculate top and bottom line buffer size
      new_linebuf_size[plane] = sizeof(*cdef_info->linebuf) * num_bufs *
                                (CDEF_VBORDER << 1) * (luma_stride >> shift);
      // Calculate column buffer size
      const int block_height =
          (CDEF_BLOCKSIZE << (MI_SIZE_LOG2 - shift)) * 2 * CDEF_VBORDER;
      new_colbuf_size[plane] =
          sizeof(*cdef_info->colbuf[plane]) * block_height * CDEF_HBORDER;
    }
  }

  // Free src, line and column buffers for worker 0 in case of reallocation
  free_cdef_linebuf_conditional(cm, new_linebuf_size);
  free_cdef_bufs_conditional(cm, cdef_info->colbuf, &cdef_info->srcbuf,
                             new_colbuf_size, new_srcbuf_size);

  if (*cdef_worker != NULL) {
    if (is_num_workers_changed) {
      // Free src and column buffers for remaining workers in case of change in
      // num_workers
      for (int idx = 1; idx < cdef_info->allocated_num_workers; idx++)
        free_cdef_bufs((*cdef_worker)[idx].colbuf, &(*cdef_worker)[idx].srcbuf);
    } else if (num_workers > 1) {
      // Free src and column buffers for remaining workers in case of
      // reallocation
      for (int idx = 1; idx < num_workers; idx++)
        free_cdef_bufs_conditional(cm, (*cdef_worker)[idx].colbuf,
                                   &(*cdef_worker)[idx].srcbuf, new_colbuf_size,
                                   new_srcbuf_size);
    }
  }

  if (cdef_info->allocated_mi_rows != num_mi_rows)
    free_cdef_row_sync(&cdef_sync->cdef_row_mt, cdef_info->allocated_mi_rows);

  // Store allocated sizes for reallocation
  cdef_info->allocated_srcbuf_size = new_srcbuf_size;
  av2_copy(cdef_info->allocated_colbuf_size, new_colbuf_size);
  av2_copy(cdef_info->allocated_linebuf_size, new_linebuf_size);
  // Store configuration to check change in configuration
  cdef_info->allocated_mi_rows = num_mi_rows;
  cdef_info->allocated_num_workers = num_workers;

  if (!is_cdef_enabled) return;

  // Memory allocation of column buffer & source buffer (worker 0).
  alloc_cdef_bufs(cm, cdef_info->colbuf, &cdef_info->srcbuf, num_planes);
  alloc_cdef_linebuf(cm, cdef_info->linebuf, num_planes);

  if (num_workers < 2) return;

  if (*cdef_worker == NULL)
    CHECK_MEM_ERROR(cm, *cdef_worker,
                    avm_calloc(num_workers, sizeof(**cdef_worker)));

  for (int idx = 1; idx < num_workers; idx++) {
    // Memory allocation of column buffer & source buffer for remaining workers.
    alloc_cdef_bufs(cm, (*cdef_worker)[idx].colbuf, &(*cdef_worker)[idx].srcbuf,
                    num_planes);
  }

  alloc_cdef_row_sync(cm, &cdef_sync->cdef_row_mt,
                      cdef_info->allocated_mi_rows);
}

// Assumes cm->rst_info[p].restoration_unit_size is already initialized
void av2_alloc_restoration_buffers(AV2_COMMON *cm) {
  const int num_planes = av2_num_planes(cm);
  for (int p = 0; p < num_planes; ++p)
    av2_alloc_restoration_struct(cm, &cm->rst_info[p], p > 0);

  if (cm->frame_filter_dictionary == NULL) {
    allocate_frame_filter_dictionary(cm);
    translate_pcwiener_filters_to_wienerns(cm);
  }

  if (cm->rlbs == NULL) {
    CHECK_MEM_ERROR(cm, cm->rlbs, avm_malloc(sizeof(RestorationLineBuffers)));
  }
  if (cm->lru_stripe_buf == NULL) {
    CHECK_MEM_ERROR(
        cm, cm->lru_stripe_buf,
        avm_malloc(MAX_LRU_STRIPE_BUF_SIZE * sizeof(*cm->lru_stripe_buf)));
  }

  av2_alloc_restoration_boundary_buffers(cm, num_planes);
}

void av2_alloc_restoration_boundary_buffers(struct AV2Common *cm,
                                            int num_planes) {
  // For striped loop restoration, we divide each row of tiles into "stripes",
  // of height 64 luma pixels but with an offset by RESTORATION_UNIT_OFFSET
  // luma pixels to match the output from CDEF. We will need to store 2 *
  // RESTORATION_CTX_VERT lines of data for each stripe, and also need to be
  // able to quickly answer the question "Where is the <n>'th stripe for tile
  // row <m>?" To make that efficient, we generate the rst_last_stripe array.

  AV2PixelRect tile_rect;
  int num_stripes = 0;
  TileInfo tile_info;
  for (int tr = 0; tr < cm->tiles.rows; ++tr) {
    av2_tile_init(&tile_info, cm, tr, 0);
    tile_rect = av2_get_tile_rect(&tile_info, cm, 0);
    const int tile_h = tile_rect.bottom - tile_rect.top;
    num_stripes += av2_lr_count_stripes_in_tile(tile_h, 0);
  }
  // int num_stripes = cm->rst_info[0].vert_stripes_per_frame;

  // Now we need to allocate enough space to store the line buffers for the
  // stripes
  const int frame_w = cm->mi_params.mi_cols << MI_SIZE_LOG2;

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
      avm_free(boundaries->stripe_boundary_above);
      avm_free(boundaries->stripe_boundary_below);
      CHECK_MEM_ERROR(cm, boundaries->stripe_boundary_above,
                      avm_memalign(32, buf_size));
      CHECK_MEM_ERROR(cm, boundaries->stripe_boundary_below,
                      avm_memalign(32, buf_size));

      boundaries->stripe_boundary_size = buf_size;
    }
    boundaries->stripe_boundary_stride = stride;
  }
}

void av2_free_restoration_buffers(AV2_COMMON *cm) {
  int p;
  for (p = 0; p < MAX_MB_PLANE; ++p)
    av2_free_restoration_struct(&cm->rst_info[p]);
  avm_free(cm->rlbs);
  cm->rlbs = NULL;
  avm_free(cm->lru_stripe_buf);
  cm->lru_stripe_buf = NULL;
  for (p = 0; p < MAX_MB_PLANE; ++p) {
    RestorationStripeBoundaries *boundaries = &cm->rst_info[p].boundaries;
    avm_free(boundaries->stripe_boundary_above);
    avm_free(boundaries->stripe_boundary_below);
    boundaries->stripe_boundary_above = NULL;
    boundaries->stripe_boundary_below = NULL;
  }
  free_frame_filter_dictionary(cm);
  avm_free_frame_buffer(&cm->rst_frame);
}

void av2_free_above_context_buffers(CommonContexts *above_contexts) {
  int i;
  const int num_planes = above_contexts->num_planes;

  for (int tile_row = 0; tile_row < above_contexts->num_tile_rows; tile_row++) {
    for (i = 0; i < num_planes; i++) {
      avm_free(above_contexts->entropy[i][tile_row]);
      above_contexts->entropy[i][tile_row] = NULL;
      avm_free(above_contexts->partition[i][tile_row]);
      above_contexts->partition[i][tile_row] = NULL;
    }
  }
  for (i = 0; i < num_planes; i++) {
    avm_free(above_contexts->entropy[i]);
    above_contexts->entropy[i] = NULL;
    avm_free(above_contexts->partition[i]);
    above_contexts->partition[i] = NULL;
  }

  above_contexts->num_tile_rows = 0;
  above_contexts->num_mi_cols = 0;
  above_contexts->num_planes = 0;
}

static void free_sbi(CommonSBInfoParams *sbi_params) {
  for (int i = 0; i < sbi_params->sbi_alloc_size; ++i) {
    av2_free_ptree_recursive(sbi_params->sbi_grid_base[i].ptree_root[0]);
    av2_free_ptree_recursive(sbi_params->sbi_grid_base[i].ptree_root[1]);
  }

  avm_free(sbi_params->sbi_grid_base);
  sbi_params->sbi_grid_base = NULL;
  sbi_params->sbi_alloc_size = 0;
}

void av2_free_context_buffers(AV2_COMMON *cm) {
  cm->mi_params.free_mi(&cm->mi_params);
  free_sbi(&cm->sbi_params);

  av2_free_above_context_buffers(&cm->above_contexts);
}

int av2_alloc_above_context_buffers(CommonContexts *above_contexts,
                                    int num_tile_rows, int num_mi_cols,
                                    int num_planes) {
  const int aligned_mi_cols =
      ALIGN_POWER_OF_TWO(num_mi_cols, MAX_MIB_SIZE_LOG2);

  // Allocate above context buffers
  above_contexts->num_tile_rows = num_tile_rows;
  above_contexts->num_mi_cols = aligned_mi_cols;
  above_contexts->num_planes = num_planes;
  for (int plane_idx = 0; plane_idx < num_planes; plane_idx++) {
    above_contexts->entropy[plane_idx] = (ENTROPY_CONTEXT **)avm_calloc(
        num_tile_rows, sizeof(above_contexts->entropy[0]));
    if (!above_contexts->entropy[plane_idx]) return 1;
    above_contexts->partition[plane_idx] = (PARTITION_CONTEXT **)avm_calloc(
        num_tile_rows, sizeof(above_contexts->partition[plane_idx]));
    if (!above_contexts->partition[plane_idx]) return 1;
  }

  for (int tile_row = 0; tile_row < num_tile_rows; tile_row++) {
    for (int plane_idx = 0; plane_idx < num_planes; plane_idx++) {
      above_contexts->entropy[plane_idx][tile_row] =
          (ENTROPY_CONTEXT *)avm_calloc(
              aligned_mi_cols, sizeof(*above_contexts->entropy[0][tile_row]));
      if (!above_contexts->entropy[plane_idx][tile_row]) return 1;
      above_contexts->partition[plane_idx][tile_row] =
          (PARTITION_CONTEXT *)avm_calloc(
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
static int alloc_mi(CommonModeInfoParams *mi_params, AV2_COMMON *cm,
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
        avm_calloc(alloc_mi_size, sizeof(*mi_params->mi_alloc));
    if (!mi_params->mi_alloc) return 1;
    mi_params->mi_alloc_size = alloc_mi_size;

    mi_params->mi_grid_base = (MB_MODE_INFO **)avm_calloc(
        mi_grid_size, sizeof(*mi_params->mi_grid_base));
    if (!mi_params->mi_grid_base) return 1;
    mi_params->mi_grid_size = mi_grid_size;
    av2_alloc_txk_skip_array(mi_params, cm);
    av2_alloc_class_id_array(mi_params, cm, height);
    mi_params->mi_alloc_sub =
        avm_calloc(alloc_mi_size, sizeof(*mi_params->mi_alloc_sub));
    if (!mi_params->mi_alloc_sub) return 1;
    mi_params->submi_grid_base = (SUBMB_INFO **)avm_calloc(
        mi_grid_size, sizeof(*mi_params->submi_grid_base));
    if (!mi_params->submi_grid_base) return 1;

    mi_params->tx_type_map =
        avm_calloc(mi_grid_size, sizeof(*mi_params->tx_type_map));
    if (!mi_params->tx_type_map) return 1;
    mi_params->cctx_type_map =
        avm_calloc(mi_grid_size, sizeof(*mi_params->cctx_type_map));
    if (!mi_params->cctx_type_map) return 1;
  } else {
    // Set only the strides corresponding to the current frame dims
    av2_set_txk_skip_array_stride(mi_params, cm);
    av2_set_class_id_array_stride(mi_params, cm, height);
  }

  return 0;
}

static void set_sb_si(AV2_COMMON *cm) {
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
        avm_calloc(sbi_size, sizeof(*sbi_params->sbi_grid_base));

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

int av2_alloc_superblock_info_buffers(AV2_COMMON *cm) {
  CommonSBInfoParams *const sbi_params = &cm->sbi_params;
  set_sb_si(cm);
  return alloc_sbi(sbi_params);
}

int av2_alloc_context_buffers(AV2_COMMON *cm, int width, int height) {
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  mi_params->set_mb_mi(mi_params, width, height);
  if (alloc_mi(mi_params, cm, height)) goto fail;

  if (av2_alloc_superblock_info_buffers(cm)) goto fail;

  return 0;

fail:
  // clear the mi_* values to force a realloc on resync
  mi_params->set_mb_mi(mi_params, 0, 0);
  av2_free_context_buffers(cm);
  return 1;
}

void av2_remove_common(AV2_COMMON *cm) {
  av2_free_context_buffers(cm);

  avm_free(cm->fc);
  cm->fc = NULL;
  avm_free(cm->default_frame_context);
  cm->default_frame_context = NULL;
}

void av2_init_mi_buffers(CommonModeInfoParams *mi_params) {
  mi_params->setup_mi(mi_params);
}
