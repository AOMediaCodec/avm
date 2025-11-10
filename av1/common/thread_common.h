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

#ifndef AOM_AV1_COMMON_THREAD_COMMON_H_
#define AOM_AV1_COMMON_THREAD_COMMON_H_

#include "config/aom_config.h"

#include "av1/common/av1_loopfilter.h"
#include "av1/common/cdef.h"
#include "aom_util/aom_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV1Common;

typedef struct AV1LfMTInfo {
  int mi_row;
  int plane;
  int dir;
} AV1LfMTInfo;

// Loopfilter row synchronization
typedef struct AV1LfSyncData {
#if CONFIG_MULTITHREAD
  pthread_mutex_t *mutex_[MAX_MB_PLANE];
  pthread_cond_t *cond_[MAX_MB_PLANE];
#endif
  // Allocate memory to store the loop-filtered superblock index in each row.
  int *cur_sb_col[MAX_MB_PLANE];
  // The optimal sync_range for different resolution and platform should be
  // determined by testing. Currently, it is chosen to be a power-of-2 number.
  int sync_range;
  int rows;

  // Row-based parallel loopfilter data
  LFWorkerData *lfdata;
  int num_workers;

#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  AV1LfMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV1LfSync;

// ccso data for job
typedef struct AV1CCSOMTInfo {
  int row;       // row number within a frame
  int plane;     // plane info
  int blk_proc;  // number of process blocks within a row
} AV1CCSOMTInfo;

// Row-based parallel ccso data
typedef struct CCSOWorkerData {
  AV1_COMMON *cm;
  MACROBLOCKD *xd;
  uint16_t *src_y;
} CCSOWorkerData;

// ccso row synchronization
typedef struct AV1CcsoSync {
  int rows;
  int num_workers;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  // Row-based parallel ccso data
  CCSOWorkerData *ccsoworkerdata;
  // Job info
  AV1CCSOMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV1CcsoSync;

typedef struct AV1LrMTInfo {
  RestorationTileLimits limits;
  RestorationTileLimits remaining_stripes;
  AV1PixelRect tile_rect;
  int tile_row;
  int tile_col;
  int lr_unit_row;
  int plane;
  int proc_height;
  int start_height;
  int none_type_count;
} AV1LrMTInfo;

typedef struct LoopRestorationWorkerData {
  void *lr_ctxt;
  AV1_COMMON *cm;
  uint16_t *scratch_buf;
} LRWorkerData;

// Looprestoration row synchronization
typedef struct AV1LrSyncData {
  int rows;
  int num_workers;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  // Row-based parallel loopfilter data
  LRWorkerData *lrworkerdata;

  AV1LrMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV1LrSync;

// Structure to hold CDEF worker data, including buffers and row initialization
// function.
typedef struct AV1CdefWorker {
  AV1_COMMON *cm;   // Pointer to common structure.
  MACROBLOCKD *xd;  // Pointer to common current coding block structure.
  uint16_t *colbuf[MAX_MB_PLANE];   // Column buffers for each plane
  uint16_t *srcbuf;                 // Source buffer for the worker
  uint16_t *linebuf[MAX_MB_PLANE];  // Top/bottom line buffers per plane
  cdef_init_fb_row_t cdef_init_fb_row_fn;
} AV1CdefWorkerData;

// Structure to hold CDEF row synchronization data for multithreading.
typedef struct AV1CdefRowSync {
#if CONFIG_MULTITHREAD
  pthread_mutex_t *row_mutex_;  // Mutex for row synchronization
  pthread_cond_t *row_cond_;    // indicates row completion between threads
#endif                          // CONFIG_MULTITHREAD
  int is_row_done;  // Flag to indicate if row processing is complete.
} AV1CdefRowSync;

// Structure to hold CDEF row multi-thread synchronization data.
typedef struct AV1CdefSyncData {
#if CONFIG_MULTITHREAD
  // Mutex lock used while dispatching jobs.
  pthread_mutex_t *mutex_;
#endif  // CONFIG_MULTITHREAD
  // Data related to CDEF row mt sync information
  AV1CdefRowSync *cdef_row_mt;
  // Flag to indicate all blocks are processed and end of frame is reached
  int end_of_frame;
  // Row index in units of 64x64 block
  int fbr;
  // Column index in units of 64x64 block
  int fbc;
} AV1CdefSync;

// Implements multi-threading for CDEF.
// Perform CDEF on input frame.
// Inputs:
//   cm: Pointer to common structure.
//   xd: Pointer to common current coding block structure.
//   cdef_worker: Pointer to CDEF worker data
//   workers: Pointer to worker data
//   cdef_sync: Pointer to CDEF sync data
//   num_workers: Number of workers available for CDEF processing
//   cdef_init_fb_row_fn: Function pointer to initialize filter block row.
// Returns:
//   Nothing will be returned.
void av1_cdef_frame_mt(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                       AV1CdefWorkerData *const cdef_worker,
                       AVxWorker *const workers, AV1CdefSync *const cdef_sync,
                       int num_workers, cdef_init_fb_row_t cdef_init_fb_row_fn);
// Initializes CDEF filter block info for a row in multi-threaded mode.
// Sets frame boundaries, prepares line buffers, and syncs with workers.
void av1_cdef_init_fb_row_mt(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                             CdefBlockInfo *const fb_info,
                             uint16_t **const linebuf, uint16_t *const src,
                             struct AV1CdefSyncData *const cdef_sync, int fbr);
// Copies a block from source to destination buffer for CDEF.
void av1_cdef_copy_sb8_16(AV1_COMMON *const cm, uint16_t *const dst,
                          int dstride, const uint16_t *src, int src_voffset,
                          int src_hoffset, int sstride, int vsize, int hsize);
// Allocates and initializes synchronization objects required for CDEF filtering
// in multi-threaded mode. No allocation is performed if single-thread execution
// is used.
void av1_alloc_cdef_sync(AV1_COMMON *const cm, AV1CdefSync *cdef_sync,
                         int num_workers);
// Frees CDEF sync resources, destroys and releases mutex if allocated.
void av1_free_cdef_sync(AV1CdefSync *cdef_sync);

// Deallocate loopfilter synchronization related mutex and data.
void av1_loop_filter_dealloc(AV1LfSync *lf_sync);

void av1_loop_filter_frame_mt(YV12_BUFFER_CONFIG *frame, struct AV1Common *cm,
                              struct macroblockd *xd, int plane_start,
                              int plane_end, int partial_frame,
                              AVxWorker *workers, int num_workers,
                              AV1LfSync *lf_sync);
void av1_loop_restoration_filter_frame_mt(YV12_BUFFER_CONFIG *frame,
                                          struct AV1Common *cm,
                                          int optimized_lr, AVxWorker *workers,
                                          int num_workers, AV1LrSync *lr_sync,
                                          void *lr_ctxt);
void av1_loop_restoration_dealloc(AV1LrSync *lr_sync, int num_workers);

// Apply LR for each restoration unit in a tile row.
void av1_foreach_rest_unit_in_tile_row(
    RestorationTileLimits *limits, RestorationTileLimits *remaining_stripes,
    const AV1PixelRect *tile_rect, int row_number, int start_height,
    int proc_height, int unit_size, int unit_idx0, int hunits_per_tile,
    int unit_stride, int tile_stripe0, void *priv, uint16_t *stripe_buf);

// Deallocate ccsofilter synchronization related mutex and data.
void av1_ccso_filter_dealloc(AV1CcsoSync *ccso_sync);

// MT implementation of ccso_frame
void av1_ccso_frame_mt(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm,
                       MACROBLOCKD *xd, AVxWorker *workers, int num_workers,
                       uint16_t *ext_rec_y, AV1CcsoSync *ccso_sync);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_THREAD_COMMON_H_
