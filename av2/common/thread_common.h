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

#ifndef AVM_AV2_COMMON_THREAD_COMMON_H_
#define AVM_AV2_COMMON_THREAD_COMMON_H_

#include "config/avm_config.h"

#include "av2/common/av2_loopfilter.h"
#include "av2/common/cdef.h"
#include "avm_util/avm_thread.h"
#include "av2/common/tip.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV2Common;

typedef struct AV2LfMTInfo {
  int mi_row;
  int plane;
  int dir;
} AV2LfMTInfo;

// Loopfilter row synchronization
typedef struct AV2LfSyncData {
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
  AV2LfMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV2LfSync;

// ccso data for job
typedef struct AV2CCSOMTInfo {
  int row;       // row number within a frame
  int plane;     // plane info
  int blk_proc;  // number of process blocks within a row
} AV2CCSOMTInfo;

// Row-based parallel ccso data
typedef struct CCSOWorkerData {
  AV2_COMMON *cm;
  MACROBLOCKD *xd;
  uint16_t *src_y;
} CCSOWorkerData;

// ccso row synchronization
typedef struct AV2CcsoSync {
  int rows;
  int num_workers;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  // Row-based parallel ccso data
  CCSOWorkerData *ccsoworkerdata;
  // Job info
  AV2CCSOMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV2CcsoSync;

// TIP data for job
typedef struct AV2TIPMTInfo {
  int row;  // row number within a frame
} AV2TIPMTInfo;

// Row-based parallel TIP data
typedef struct TIPWorkerData {
  DECLARE_ALIGNED(32, MACROBLOCKD, xd);
  AV2_COMMON *cm;
  CONV_BUF_TYPE *tmp_conv_dst;
  uint16_t *mc_buf[2];
  CalcSubpelParamsFunc calc_subpel_params_func;
  int copy_refined_mvs;
} TIPWorkerData;

// TIP row synchronization
typedef struct AV2TipSync {
  int unit_blk_size;
  int num_workers;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  // Job info
  AV2TIPMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV2TipSync;

typedef struct AV2LrMTInfo {
  RestorationTileLimits limits;
  RestorationTileLimits remaining_stripes;
  AV2PixelRect tile_rect;
  int tile_row;
  int tile_col;
  int lr_unit_row;
  int plane;
  int proc_height;
  int start_height;
  int none_type_count;
} AV2LrMTInfo;

typedef struct LoopRestorationWorkerData {
  void *lr_ctxt;
  AV2_COMMON *cm;
  uint16_t *scratch_buf;
} LRWorkerData;

// Looprestoration row synchronization
typedef struct AV2LrSyncData {
  int rows;
  int num_workers;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *job_mutex;
#endif
  // Row-based parallel loopfilter data
  LRWorkerData *lrworkerdata;

  AV2LrMTInfo *job_queue;
  int jobs_enqueued;
  int jobs_dequeued;
} AV2LrSync;

// Structure to hold CDEF worker data, including buffers and row initialization
// function.
typedef struct AV2CdefWorker {
  AV2_COMMON *cm;   // Pointer to common structure.
  MACROBLOCKD *xd;  // Pointer to common current coding block structure.
  uint16_t *colbuf[MAX_MB_PLANE];   // Column buffers for each plane
  uint16_t *srcbuf;                 // Source buffer for the worker
  uint16_t *linebuf[MAX_MB_PLANE];  // Top/bottom line buffers per plane
  cdef_init_fb_row_t cdef_init_fb_row_fn;
} AV2CdefWorkerData;

// Structure to hold CDEF row synchronization data for multithreading.
typedef struct AV2CdefRowSync {
#if CONFIG_MULTITHREAD
  pthread_mutex_t *row_mutex_;  // Mutex for row synchronization
  pthread_cond_t *row_cond_;    // indicates row completion between threads
#endif                          // CONFIG_MULTITHREAD
  int is_row_done;  // Flag to indicate if row processing is complete.
} AV2CdefRowSync;

// Structure to hold CDEF row multi-thread synchronization data.
typedef struct AV2CdefSyncData {
#if CONFIG_MULTITHREAD
  // Mutex lock used while dispatching jobs.
  pthread_mutex_t *mutex_;
#endif  // CONFIG_MULTITHREAD
  // Data related to CDEF row mt sync information
  AV2CdefRowSync *cdef_row_mt;
  // Flag to indicate all blocks are processed and end of frame is reached
  int end_of_frame;
  // Row index in units of 64x64 block
  int fbr;
  // Column index in units of 64x64 block
  int fbc;
} AV2CdefSync;

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
void av2_cdef_frame_mt(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                       AV2CdefWorkerData *const cdef_worker,
                       AVxWorker *const workers, AV2CdefSync *const cdef_sync,
                       int num_workers, cdef_init_fb_row_t cdef_init_fb_row_fn);
// Initializes CDEF filter block info for a row in multi-threaded mode.
// Sets frame boundaries, prepares line buffers, and syncs with workers.
void av2_cdef_init_fb_row_mt(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                             CdefBlockInfo *const fb_info,
                             uint16_t **const linebuf, uint16_t *const src,
                             struct AV2CdefSyncData *const cdef_sync, int fbr);
// Copies a block from source to destination buffer for CDEF.
void av2_cdef_copy_sb8_16(AV2_COMMON *const cm, uint16_t *const dst,
                          int dstride, const uint16_t *src, int src_voffset,
                          int src_hoffset, int sstride, int vsize, int hsize);
// Allocates and initializes synchronization objects required for CDEF filtering
// in multi-threaded mode. No allocation is performed if single-thread execution
// is used.
void av2_alloc_cdef_sync(AV2_COMMON *const cm, AV2CdefSync *cdef_sync,
                         int num_workers);
// Frees CDEF sync resources, destroys and releases mutex if allocated.
void av2_free_cdef_sync(AV2CdefSync *cdef_sync);

// Deallocate loopfilter synchronization related mutex and data.
void av2_loop_filter_dealloc(AV2LfSync *lf_sync);

void av2_loop_filter_frame_mt(YV12_BUFFER_CONFIG *frame, struct AV2Common *cm,
                              struct macroblockd *xd, int plane_start,
                              int plane_end, int partial_frame,
                              AVxWorker *workers, int num_workers,
                              AV2LfSync *lf_sync);
void av2_loop_restoration_filter_frame_mt(YV12_BUFFER_CONFIG *frame,
                                          struct AV2Common *cm,
                                          int optimized_lr, AVxWorker *workers,
                                          int num_workers, AV2LrSync *lr_sync,
                                          void *lr_ctxt);
void av2_loop_restoration_dealloc(AV2LrSync *lr_sync, int num_workers);

// Apply LR for each restoration unit in a tile row.
void av2_foreach_rest_unit_in_tile_row(
    RestorationTileLimits *limits, RestorationTileLimits *remaining_stripes,
    const AV2PixelRect *tile_rect, int row_number, int start_height,
    int proc_height, int unit_size, int unit_idx0, int hunits_per_tile,
    int unit_stride, int tile_stripe0, void *priv, uint16_t *stripe_buf);

// Deallocate ccsofilter synchronization related mutex and data.
void av2_ccso_filter_dealloc(AV2CcsoSync *ccso_sync);

// Deallocate TIP synchronization related mutex and data.
void av2_tip_dealloc(AV2TipSync *tip_sync);

// MT implementation of ccso_frame
void av2_ccso_frame_mt(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm,
                       MACROBLOCKD *xd, AVxWorker *workers, int num_workers,
                       uint16_t *ext_rec_y, AV2CcsoSync *ccso_sync);

// MT implementation of av1_setup_tip_frame
void av2_setup_tip_frame_mt(AV2_COMMON *cm,
                            CalcSubpelParamsFunc calc_subpel_params_func,
                            int copy_refined_mvs, AVxWorker *workers,
                            int num_workers, AV2TipSync *tip_sync,
                            TIPWorkerData *tip_worker_data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_THREAD_COMMON_H_
