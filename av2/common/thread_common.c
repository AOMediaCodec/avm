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

#include "config/avm_config.h"
#include "config/avm_scale_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_mem/avm_mem.h"
#include "av2/common/av2_loopfilter.h"
#include "av2/common/entropymode.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/thread_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/resize.h"
#include "av2/common/restoration.h"
#include "av2/common/ccso.h"
#include "av2/common/tip.h"

// Set up nsync by width.
static INLINE int get_sync_range(int width) {
  // nsync numbers are picked by testing. For example, for 4k
  // video, using 4 gives best performance.
  if (width < 640)
    return 1;
  else if (width <= 1280)
    return 2;
  else if (width <= 4096)
    return 4;
  else
    return 8;
}

// Allocate memory for lf row synchronization
static void loop_filter_alloc(AV2LfSync *lf_sync, AV2_COMMON *cm, int rows,
                              int width, int num_workers) {
  lf_sync->rows = rows;
#if CONFIG_MULTITHREAD
  {
    int i, j;

    for (j = 0; j < MAX_MB_PLANE; j++) {
      CHECK_MEM_ERROR(cm, lf_sync->mutex_[j],
                      avm_malloc(sizeof(*(lf_sync->mutex_[j])) * rows));
      if (lf_sync->mutex_[j]) {
        for (i = 0; i < rows; ++i) {
          pthread_mutex_init(&lf_sync->mutex_[j][i], NULL);
        }
      }

      CHECK_MEM_ERROR(cm, lf_sync->cond_[j],
                      avm_malloc(sizeof(*(lf_sync->cond_[j])) * rows));
      if (lf_sync->cond_[j]) {
        for (i = 0; i < rows; ++i) {
          pthread_cond_init(&lf_sync->cond_[j][i], NULL);
        }
      }
    }

    CHECK_MEM_ERROR(cm, lf_sync->job_mutex,
                    avm_malloc(sizeof(*(lf_sync->job_mutex))));
    if (lf_sync->job_mutex) {
      pthread_mutex_init(lf_sync->job_mutex, NULL);
    }
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, lf_sync->lfdata,
                  avm_malloc(num_workers * sizeof(*(lf_sync->lfdata))));
  lf_sync->num_workers = num_workers;

  for (int j = 0; j < MAX_MB_PLANE; j++) {
    CHECK_MEM_ERROR(cm, lf_sync->cur_sb_col[j],
                    avm_malloc(sizeof(*(lf_sync->cur_sb_col[j])) * rows));
  }
  CHECK_MEM_ERROR(
      cm, lf_sync->job_queue,
      avm_malloc(sizeof(*(lf_sync->job_queue)) * rows * MAX_MB_PLANE * 2));
  // Set up nsync.
  lf_sync->sync_range = get_sync_range(width);
}

// Deallocate lf synchronization related mutex and data
void av2_loop_filter_dealloc(AV2LfSync *lf_sync) {
  if (lf_sync != NULL) {
    int j;
#if CONFIG_MULTITHREAD
    int i;
    for (j = 0; j < MAX_MB_PLANE; j++) {
      if (lf_sync->mutex_[j] != NULL) {
        for (i = 0; i < lf_sync->rows; ++i) {
          pthread_mutex_destroy(&lf_sync->mutex_[j][i]);
        }
        avm_free(lf_sync->mutex_[j]);
      }
      if (lf_sync->cond_[j] != NULL) {
        for (i = 0; i < lf_sync->rows; ++i) {
          pthread_cond_destroy(&lf_sync->cond_[j][i]);
        }
        avm_free(lf_sync->cond_[j]);
      }
    }
    if (lf_sync->job_mutex != NULL) {
      pthread_mutex_destroy(lf_sync->job_mutex);
      avm_free(lf_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD
    avm_free(lf_sync->lfdata);
    for (j = 0; j < MAX_MB_PLANE; j++) {
      avm_free(lf_sync->cur_sb_col[j]);
    }

    avm_free(lf_sync->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av2_zero(*lf_sync);
  }
}

static void loop_filter_data_reset(LFWorkerData *lf_data,
                                   YV12_BUFFER_CONFIG *frame_buffer,
                                   struct AV2Common *cm, MACROBLOCKD *xd) {
  struct macroblockd_plane *pd = xd->plane;
  lf_data->frame_buffer = frame_buffer;
  lf_data->cm = cm;
  lf_data->xd = xd;
  for (int i = 0; i < MAX_MB_PLANE; i++) {
    memcpy(&lf_data->planes[i].dst, &pd[i].dst, sizeof(lf_data->planes[i].dst));
    lf_data->planes[i].subsampling_x = pd[i].subsampling_x;
    lf_data->planes[i].subsampling_y = pd[i].subsampling_y;
  }
}

void av2_alloc_cdef_sync(AV2_COMMON *const cm, AV2CdefSync *cdef_sync,
                         int num_workers) {
  if (num_workers < 1) return;
#if CONFIG_MULTITHREAD
  if (cdef_sync->mutex_ == NULL) {
    CHECK_MEM_ERROR(cm, cdef_sync->mutex_,
                    avm_malloc(sizeof(*(cdef_sync->mutex_))));
    if (cdef_sync->mutex_) pthread_mutex_init(cdef_sync->mutex_, NULL);
  }
#else
  (void)cm;
  (void)cdef_sync;
#endif  // CONFIG_MULTITHREAD
}

void av2_free_cdef_sync(AV2CdefSync *cdef_sync) {
  if (cdef_sync == NULL) return;
#if CONFIG_MULTITHREAD
  if (cdef_sync->mutex_ != NULL) {
    pthread_mutex_destroy(cdef_sync->mutex_);
    avm_free(cdef_sync->mutex_);
  }
#endif  // CONFIG_MULTITHREAD
}

// Wait for the previous row to complete in cdef row multi-thread.
static INLINE void cdef_row_mt_sync_read(AV2CdefSync *cdef_sync, int row) {
  if (!row) return;
#if CONFIG_MULTITHREAD
  AV2CdefRowSync *cdef_row_mt = cdef_sync->cdef_row_mt;
  pthread_mutex_lock(cdef_row_mt[row - 1].row_mutex_);
  while (cdef_row_mt[row - 1].is_row_done != 1)
    pthread_cond_wait(cdef_row_mt[row - 1].row_cond_,
                      cdef_row_mt[row - 1].row_mutex_);
  cdef_row_mt[row - 1].is_row_done = 0;
  pthread_mutex_unlock(cdef_row_mt[row - 1].row_mutex_);
#else
  (void)cdef_sync;
#endif  // CONFIG_MULTITHREAD
}

// Signals current CDEF row has completed its processing to other threads.
static INLINE void cdef_row_mt_sync_write(AV2CdefSync *cdef_sync, int row) {
#if CONFIG_MULTITHREAD
  AV2CdefRowSync *cdef_row_mt = cdef_sync->cdef_row_mt;
  pthread_mutex_lock(cdef_row_mt[row].row_mutex_);
  pthread_cond_signal(cdef_row_mt[row].row_cond_);
  cdef_row_mt[row].is_row_done = 1;
  pthread_mutex_unlock(cdef_row_mt[row].row_mutex_);
#else
  (void)cdef_sync;
  (void)row;
#endif  // CONFIG_MULTITHREAD
}

static INLINE void sync_read(AV2LfSync *const lf_sync, int r, int c,
                             int plane) {
#if CONFIG_MULTITHREAD
  const int nsync = lf_sync->sync_range;

  if (r && !(c & (nsync - 1))) {
    pthread_mutex_t *const mutex = &lf_sync->mutex_[plane][r - 1];
    pthread_mutex_lock(mutex);

    while (c > lf_sync->cur_sb_col[plane][r - 1] - nsync) {
      pthread_cond_wait(&lf_sync->cond_[plane][r - 1], mutex);
    }
    pthread_mutex_unlock(mutex);
  }
#else
  (void)lf_sync;
  (void)r;
  (void)c;
  (void)plane;
#endif  // CONFIG_MULTITHREAD
}

static INLINE void sync_write(AV2LfSync *const lf_sync, int r, int c,
                              const int sb_cols, int plane) {
#if CONFIG_MULTITHREAD
  const int nsync = lf_sync->sync_range;
  int cur;
  // Only signal when there are enough filtered SB for next row to run.
  int sig = 1;

  if (c < sb_cols - 1) {
    cur = c;
    if (c % nsync) sig = 0;
  } else {
    cur = sb_cols + nsync;
  }

  if (sig) {
    pthread_mutex_lock(&lf_sync->mutex_[plane][r]);

    lf_sync->cur_sb_col[plane][r] = cur;

    pthread_cond_broadcast(&lf_sync->cond_[plane][r]);
    pthread_mutex_unlock(&lf_sync->mutex_[plane][r]);
  }
#else
  (void)lf_sync;
  (void)r;
  (void)c;
  (void)sb_cols;
  (void)plane;
#endif  // CONFIG_MULTITHREAD
}

static void enqueue_lf_jobs(AV2LfSync *lf_sync, AV2_COMMON *cm, int start,
                            int stop, int plane_start, int plane_end,
                            int mib_size) {
  int mi_row, plane, dir;
  AV2LfMTInfo *lf_job_queue = lf_sync->job_queue;
  lf_sync->jobs_enqueued = 0;
  lf_sync->jobs_dequeued = 0;

  for (dir = 0; dir < 2; dir++) {
    for (plane = plane_start; plane < plane_end; plane++) {
      if (plane == 0 && !(cm->lf.apply_deblocking_filter[0]) &&
          !(cm->lf.apply_deblocking_filter[1]))
        break;
      else if (plane == 1 && !(cm->lf.apply_deblocking_filter_u))
        continue;
      else if (plane == 2 && !(cm->lf.apply_deblocking_filter_v))
        continue;
      for (mi_row = start; mi_row < stop; mi_row += mib_size) {
        lf_job_queue->mi_row = mi_row;
        lf_job_queue->plane = plane;
        lf_job_queue->dir = dir;
        lf_job_queue++;
        lf_sync->jobs_enqueued++;
      }
    }
  }
}

static AV2LfMTInfo *get_lf_job_info(AV2LfSync *lf_sync) {
  AV2LfMTInfo *cur_job_info = NULL;

#if CONFIG_MULTITHREAD
  pthread_mutex_lock(lf_sync->job_mutex);

  if (lf_sync->jobs_dequeued < lf_sync->jobs_enqueued) {
    cur_job_info = lf_sync->job_queue + lf_sync->jobs_dequeued;
    lf_sync->jobs_dequeued++;
  }

  pthread_mutex_unlock(lf_sync->job_mutex);
#else
  (void)lf_sync;
#endif

  return cur_job_info;
}

// Implement row loopfiltering for each thread.
static INLINE void thread_loop_filter_rows(
    const YV12_BUFFER_CONFIG *const frame_buffer, AV2_COMMON *const cm,
    struct macroblockd_plane *planes, MACROBLOCKD *xd,
    AV2LfSync *const lf_sync) {
  const int mib_size = cm->mib_size;
  const int mib_size_log2 = cm->mib_size_log2;
  const int sb_cols =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols, mib_size_log2) >> mib_size_log2;
  int mi_row, mi_col, plane, dir;
  int r, c;

  while (1) {
    AV2LfMTInfo *cur_job_info = get_lf_job_info(lf_sync);

    if (cur_job_info != NULL) {
      mi_row = cur_job_info->mi_row;
      plane = cur_job_info->plane;
      dir = cur_job_info->dir;
      r = mi_row >> mib_size_log2;

      if (dir == 0) {
        for (mi_col = 0; mi_col < cm->mi_params.mi_cols; mi_col += mib_size) {
          c = mi_col >> mib_size_log2;

          av2_setup_dst_planes(planes, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);

          av2_filter_block_plane_vert(cm, xd, plane, &planes[plane], mi_row,
                                      mi_col);
          sync_write(lf_sync, r, c, sb_cols, plane);
        }
      } else if (dir == 1) {
        for (mi_col = 0; mi_col < cm->mi_params.mi_cols; mi_col += mib_size) {
          c = mi_col >> mib_size_log2;

          // Wait for vertical edge filtering of the top-right block to be
          // completed
          sync_read(lf_sync, r, c, plane);

          // Wait for vertical edge filtering of the right block to be
          // completed
          sync_read(lf_sync, r + 1, c, plane);

          av2_setup_dst_planes(planes, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av2_filter_block_plane_horz(cm, xd, plane, &planes[plane], mi_row,
                                      mi_col);
        }
      }
    } else {
      break;
    }
  }
}

// Row-based multi-threaded loopfilter hook
static int loop_filter_row_worker(void *arg1, void *arg2) {
  AV2LfSync *const lf_sync = (AV2LfSync *)arg1;
  LFWorkerData *const lf_data = (LFWorkerData *)arg2;
  thread_loop_filter_rows(lf_data->frame_buffer, lf_data->cm, lf_data->planes,
                          lf_data->xd, lf_sync);
  return 1;
}

static void loop_filter_rows_mt(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm,
                                MACROBLOCKD *xd, int start, int stop,
                                int plane_start, int plane_end,
                                AVxWorker *workers, int nworkers,
                                AV2LfSync *lf_sync) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  const int mib_size = cm->mib_size;
  const int mib_size_log2 = cm->mib_size_log2;
  // Number of superblock rows and cols
  const int sb_rows =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_rows, mib_size_log2) >> mib_size_log2;
  const int num_workers = nworkers;
  int i;

  if (!lf_sync->sync_range || sb_rows != lf_sync->rows ||
      num_workers > lf_sync->num_workers) {
    av2_loop_filter_dealloc(lf_sync);
    loop_filter_alloc(lf_sync, cm, sb_rows, cm->width, num_workers);
  }

  // Initialize cur_sb_col to -1 for all SB rows.
  for (i = 0; i < MAX_MB_PLANE; i++) {
    memset(lf_sync->cur_sb_col[i], -1,
           sizeof(*(lf_sync->cur_sb_col[i])) * sb_rows);
  }

  enqueue_lf_jobs(lf_sync, cm, start, stop, plane_start, plane_end, mib_size);

  // Set up loopfilter thread data.
  for (i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    LFWorkerData *const lf_data = &lf_sync->lfdata[i];

    worker->hook = loop_filter_row_worker;
    worker->data1 = lf_sync;
    worker->data2 = lf_data;

    // Loopfilter data
    loop_filter_data_reset(lf_data, frame, cm, xd);

    // Start loopfiltering
    if (i == num_workers - 1) {
      winterface->execute(worker);
    } else {
      winterface->launch(worker);
    }
  }

  // Wait till all rows are finished
  for (i = 0; i < num_workers; ++i) {
    winterface->sync(&workers[i]);
  }
}

void av2_loop_filter_frame_mt(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm,
                              MACROBLOCKD *xd, int plane_start, int plane_end,
                              int partial_frame, AVxWorker *workers,
                              int num_workers, AV2LfSync *lf_sync) {
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }

  int start_mi_row, end_mi_row, mi_rows_to_filter;

  start_mi_row = 0;
  mi_rows_to_filter = cm->mi_params.mi_rows;
  if (partial_frame && cm->mi_params.mi_rows > 8) {
    start_mi_row = cm->mi_params.mi_rows >> 1;
    start_mi_row &= 0xfffffff8;
    mi_rows_to_filter = AVMMAX(cm->mi_params.mi_rows / 8, 8);
  }
  end_mi_row = start_mi_row + mi_rows_to_filter;
  av2_loop_filter_frame_init(cm, plane_start, plane_end);

  loop_filter_rows_mt(frame, cm, xd, start_mi_row, end_mi_row, plane_start,
                      plane_end, workers, num_workers, lf_sync);
}

// Initialize ccso worker data
static INLINE void ccso_data_reset(CCSOWorkerData *ccsoworkerdata,
                                   AV2_COMMON *cm, MACROBLOCKD *xd,
                                   uint16_t *ext_rec_y) {
  ccsoworkerdata->cm = cm;
  ccsoworkerdata->xd = xd;
  ccsoworkerdata->src_y = ext_rec_y;
}

// Allocate memory for ccso row synchronization
static void ccso_filter_alloc(AV2CcsoSync *ccso_sync, AV2_COMMON *cm, int rows,
                              int num_workers) {
  ccso_sync->rows = rows;
#if CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, ccso_sync->job_mutex,
                  avm_malloc(sizeof(*(ccso_sync->job_mutex))));
  if (ccso_sync->job_mutex) {
    pthread_mutex_init(ccso_sync->job_mutex, NULL);
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(
      cm, ccso_sync->ccsoworkerdata,
      avm_malloc(num_workers * sizeof(*(ccso_sync->ccsoworkerdata))));
  ccso_sync->num_workers = num_workers;

  CHECK_MEM_ERROR(cm, ccso_sync->job_queue,
                  avm_malloc(sizeof(*(ccso_sync->job_queue)) * rows));
}

// Deallocate ccso synchronization related mutex and data
void av2_ccso_filter_dealloc(AV2CcsoSync *ccso_sync) {
  if (ccso_sync != NULL) {
#if CONFIG_MULTITHREAD
    if (ccso_sync->job_mutex != NULL) {
      pthread_mutex_destroy(ccso_sync->job_mutex);
      avm_free(ccso_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD
    avm_free(ccso_sync->ccsoworkerdata);

    avm_free(ccso_sync->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av2_zero(*ccso_sync);
  }
}

// Compares the number of CCSO blocks processed between two rows used for job
// sorting
static int compare_ccso_blk_proc(const void *a, const void *b) {
  const AV2CCSOMTInfo *structA = (const AV2CCSOMTInfo *)a;
  const AV2CCSOMTInfo *structB = (const AV2CCSOMTInfo *)b;

  if (structA->blk_proc < structB->blk_proc) {
    return 1;  // structA comes after structB
  } else if (structA->blk_proc > structB->blk_proc) {
    return -1;  // structA comes before structB
  } else {
    return 0;  // Elements are equal
  }
}

// Generates job list for ccso filter
static void enqueue_ccso_jobs(AV2CcsoSync *ccso_sync, AV2_COMMON *cm,
                              MACROBLOCKD *xd) {
  AV2CCSOMTInfo *ccso_job_queue = ccso_sync->job_queue;
  ccso_sync->jobs_enqueued = 0;
  ccso_sync->jobs_dequeued = 0;
  const int num_planes = av2_num_planes(cm);
  const int inc_row = 1 << CCSO_PROC_BLK_LOG2;
  const int blk_size = 1 << get_ccso_unit_size_log2_adaptive_tile(
                           cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const CommonModeInfoParams *const mi_params = &cm->mi_params;

  for (int plane = 0; plane < num_planes; plane++) {
    const int pic_height = xd->plane[plane].dst.height;
    const int pic_width = xd->plane[plane].dst.width;
    const int blk_log2 = get_ccso_unit_size_log2_adaptive_tile(
        cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
    int blk_log2_x = blk_log2;
    int blk_log2_y = blk_log2;
    if (plane != 0) {
      blk_log2_x -= cm->seq_params.subsampling_x;
      blk_log2_y -= cm->seq_params.subsampling_y;
    }
    if (cm->ccso_info.ccso_enable[plane]) {
      for (int row = 0; row < pic_height; row += inc_row) {
        int blk_processed = 0;
        const int ccso_blk_idx_y = (blk_size >> MI_SIZE_LOG2) *
                                   (row >> blk_log2_y) * mi_params->mi_stride;
        for (int col = 0; col < pic_width; col += inc_row) {
          const int ccso_blk_idx =
              ccso_blk_idx_y + (blk_size >> MI_SIZE_LOG2) * (col >> blk_log2_x);
          const bool use_ccso =
              (plane == 0) ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_y
              : (plane == 1)
                  ? mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_u
                  : mi_params->mi_grid_base[ccso_blk_idx]->ccso_blk_v;
          if (use_ccso) blk_processed++;
        }
        // No block is available for CCSO processing, skip this job
        if (blk_processed == 0) continue;

        ccso_job_queue->row = row;
        ccso_job_queue->plane = plane;
        ccso_job_queue->blk_proc = blk_processed;
        ccso_job_queue++;
        ccso_sync->jobs_enqueued++;
      }
    }
  }
  qsort(ccso_sync->job_queue, ccso_sync->jobs_enqueued,
        sizeof(ccso_sync->job_queue[0]), compare_ccso_blk_proc);
}

// Returns job info for each thread
static AV2CCSOMTInfo *get_ccso_job_info(AV2CcsoSync *ccso_sync) {
  AV2CCSOMTInfo *cur_job_info = NULL;

#if CONFIG_MULTITHREAD
  pthread_mutex_lock(ccso_sync->job_mutex);
  if (ccso_sync->jobs_dequeued < ccso_sync->jobs_enqueued) {
    cur_job_info = ccso_sync->job_queue + ccso_sync->jobs_dequeued;
    ccso_sync->jobs_dequeued++;
  }
  pthread_mutex_unlock(ccso_sync->job_mutex);
#else
  (void)ccso_sync;
#endif
  return cur_job_info;
}

// Implement row ccso for each thread.
static INLINE void process_ccso_rows(AV2_COMMON *const cm, MACROBLOCKD *xd,
                                     uint16_t *src_y,
                                     AV2CcsoSync *const ccso_sync) {
  int src_cls[2];
  int src_loc[2];
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int blk_log2 = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);

  while (1) {
    AV2CCSOMTInfo *cur_job_info = get_ccso_job_info(ccso_sync);
    // Break the while loop if no job is available
    if (cur_job_info == NULL) break;

    int blk_log2_x = blk_log2;
    int blk_log2_y = blk_log2;
    const int row = cur_job_info->row;
    const int plane = cur_job_info->plane;
    uint16_t *dst_yuv = xd->plane[plane].dst.buf;
    const uint16_t thr = quant_sz[cm->ccso_info.scale_idx[plane]]
                                 [cm->ccso_info.quant_idx[plane]];
    const uint8_t filter_sup = cm->ccso_info.ext_filter_support[plane];
    const int dst_stride = xd->plane[plane].dst.stride;
    const int proc_unit_log2 =
        cm->mib_size_log2 -
        AVMMAX(xd->plane[plane].subsampling_x, xd->plane[plane].subsampling_y) +
        MI_SIZE_LOG2;
    const int y_uv_vscale = xd->plane[plane].subsampling_y;
    derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
    if (plane != 0) {
      blk_log2_x -= cm->seq_params.subsampling_x;
      blk_log2_y -= cm->seq_params.subsampling_y;
    }
    const int unit_log2_x = AVMMIN(proc_unit_log2, blk_log2_x);
    const int unit_log2_y = AVMMIN(proc_unit_log2, blk_log2_y);
    const int blk_size = 1 << blk_log2;
    const int blk_log2_proc = CCSO_PROC_BLK_LOG2;
    const int blk_size_proc = 1 << blk_log2_proc;
    const uint16_t *src_y_temp =
        src_y + (CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE) +
        (row >> CCSO_PROC_BLK_LOG2) *
            (ccso_ext_stride << (blk_log2_proc + y_uv_vscale));
    uint16_t *dst_yuv_temp =
        dst_yuv + (row >> CCSO_PROC_BLK_LOG2) * (dst_stride << blk_log2_proc);

    av2_apply_ccso_filter_for_row(
        cm, xd, src_y_temp, dst_yuv_temp, src_loc, src_cls, row, thr, blk_size,
        blk_size_proc, blk_log2_x, blk_log2_y, unit_log2_x, unit_log2_y, plane);
  }
}

// Row based processing of ccso worker
static int ccso_row_worker(void *arg1, void *arg2) {
  AV2CcsoSync *ccso_sync = (AV2CcsoSync *)arg1;
  CCSOWorkerData *ccso_data = (CCSOWorkerData *)arg2;
  process_ccso_rows(ccso_data->cm, ccso_data->xd, ccso_data->src_y, ccso_sync);
  return 1;
}

// Apply CCSO filter for frame with multithread support
static void apply_ccso_filter_mt(AVxWorker *workers, int nworkers,
                                 AV2_COMMON *const cm, MACROBLOCKD *const xd,
                                 uint16_t *ext_rec_y, AV2CcsoSync *ccso_sync) {
  const int num_planes = av2_num_planes(cm);
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  int num_proc_blk_rows = 0;
  const int num_workers = nworkers;
  const int round_offset = (1 << CCSO_PROC_BLK_LOG2) - 1;

  for (int plane = 0; plane < num_planes; plane++) {
    const int pic_height = xd->plane[plane].dst.height;
    if (cm->ccso_info.ccso_enable[plane])
      num_proc_blk_rows +=
          (pic_height + round_offset) & ~round_offset >> CCSO_PROC_BLK_LOG2;
  }

  if (num_proc_blk_rows != ccso_sync->rows ||
      num_workers > ccso_sync->num_workers) {
    av2_ccso_filter_dealloc(ccso_sync);
    ccso_filter_alloc(ccso_sync, cm, num_proc_blk_rows, num_workers);
  }

  enqueue_ccso_jobs(ccso_sync, cm, xd);
  // return if no blocks in frame are to be filtered
  if (ccso_sync->jobs_enqueued == 0) return;

  // Setup ccso worker data and execute
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    CCSOWorkerData *const ccso_data = &ccso_sync->ccsoworkerdata[i];
    worker->hook = ccso_row_worker;
    worker->data1 = ccso_sync;
    worker->data2 = ccso_data;

    // ccso data
    ccso_data_reset(ccso_data, cm, xd, ext_rec_y);

    // Start the workers
    if (i == num_workers - 1) {
      winterface->execute(worker);
    } else {
      winterface->launch(worker);
    }
  }

  // Wait till all workers are finished
  for (int i = 0; i < num_workers; ++i) {
    winterface->sync(&workers[i]);
  }
}

void av2_ccso_frame_mt(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm,
                       MACROBLOCKD *xd, AVxWorker *workers, int num_workers,
                       uint16_t *ext_rec_y, AV2CcsoSync *ccso_sync) {
  const int num_planes = av2_num_planes(cm);
  av2_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  apply_ccso_filter_mt(workers, num_workers, cm, xd, ext_rec_y, ccso_sync);
}

// Get next TIP row job for the worker
static AV2TIPMTInfo *get_tip_job_info(AV2TipSync *const tip_sync) {
  AV2TIPMTInfo *cur_job_info = NULL;

#if CONFIG_MULTITHREAD
  pthread_mutex_lock(tip_sync->job_mutex);
  if (tip_sync->jobs_dequeued < tip_sync->jobs_enqueued) {
    cur_job_info = tip_sync->job_queue + tip_sync->jobs_dequeued;
    tip_sync->jobs_dequeued++;
  }
  pthread_mutex_unlock(tip_sync->job_mutex);
#else
  (void)tip_sync;
#endif
  return cur_job_info;
}

// Process TIP row jobs of the given worker
static INLINE void process_tip_rows(
    AV2_COMMON *const cm, MACROBLOCKD *xd, uint16_t **mc_buf,
    CONV_BUF_TYPE *tmp_conv_dst, CalcSubpelParamsFunc calc_subpel_params_func,
    int copy_refined_mvs, AV2TipSync *const tip_sync) {
  const int unit_blk_size =
      (get_unit_bsize_for_tip_frame(
           cm->features.tip_frame_mode, cm->tip_interp_filter,
           cm->seq_params.enable_tip_refinemv) == BLOCK_16X16)
          ? 16
          : 8;

  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  while (1) {
    AV2TIPMTInfo *cur_job_info = get_tip_job_info(tip_sync);
    // Break the while loop if no job is available
    if (cur_job_info == NULL) break;
    const int blk_row = cur_job_info->row;
    av2_tip_setup_tip_frame_row(
        cm, xd, blk_row, 0, mvs_rows, mvs_cols, mvs_cols, unit_blk_size,
        MAX_BLOCK_SIZE_WITH_SAME_MV, mc_buf, tmp_conv_dst,
        calc_subpel_params_func, copy_refined_mvs);
  }
}

// Row based processing of TIP worker
static int tip_row_worker(void *arg1, void *arg2) {
  AV2TipSync *const tip_sync = (AV2TipSync *)arg1;
  TIPWorkerData *const tip_worker_data = (TIPWorkerData *)arg2;
  process_tip_rows(tip_worker_data->cm, &tip_worker_data->xd,
                   tip_worker_data->mc_buf, tip_worker_data->tmp_conv_dst,
                   tip_worker_data->calc_subpel_params_func,
                   tip_worker_data->copy_refined_mvs, tip_sync);
  return 1;
}

// Generates TIP row job list
static void enqueue_tip_jobs(AV2TipSync *const tip_sync, int mvs_rows,
                             int step) {
  AV2TIPMTInfo *tip_job_queue = tip_sync->job_queue;
  tip_sync->jobs_enqueued = 0;
  tip_sync->jobs_dequeued = 0;
  for (int row = 0; row < mvs_rows; row += step) {
    tip_job_queue->row = row;
    tip_job_queue++;
    tip_sync->jobs_enqueued++;
  }
}

// Deallocate TIP synchronization related mutex and data
void av2_tip_dealloc(AV2TipSync *const tip_sync) {
  if (tip_sync != NULL) {
#if CONFIG_MULTITHREAD
    if (tip_sync->job_mutex != NULL) {
      pthread_mutex_destroy(tip_sync->job_mutex);
      avm_free(tip_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD

    avm_free(tip_sync->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av2_zero(*tip_sync);
  }
}

// Allocate memory for TIP row synchronization
static void tip_alloc(AV2TipSync *const tip_sync, AV2_COMMON *cm,
                      int num_workers, int mvs_rows, int unit_blk_size,
                      int step) {
  tip_sync->unit_blk_size = unit_blk_size;
#if CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, tip_sync->job_mutex,
                  avm_malloc(sizeof(*(tip_sync->job_mutex))));
  if (tip_sync->job_mutex) {
    pthread_mutex_init(tip_sync->job_mutex, NULL);
  }
#endif  // CONFIG_MULTITHREAD
  tip_sync->num_workers = num_workers;

  CHECK_MEM_ERROR(
      cm, tip_sync->job_queue,
      avm_malloc(sizeof(*(tip_sync->job_queue)) * (mvs_rows / step)));
}

// Setup TIP frame with multithread support
void av2_setup_tip_frame_mt(AV2_COMMON *cm,
                            CalcSubpelParamsFunc calc_subpel_params_func,
                            int copy_refined_mvs, AVxWorker *const workers,
                            int num_workers, AV2TipSync *const tip_sync,
                            TIPWorkerData *const tip_worker_data) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  const int unit_blk_size =
      (get_unit_bsize_for_tip_frame(
           cm->features.tip_frame_mode, cm->tip_interp_filter,
           cm->seq_params.enable_tip_refinemv) == BLOCK_16X16)
          ? 16
          : 8;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int step = (unit_blk_size >> TMVP_MI_SZ_LOG2);
  if (tip_sync->unit_blk_size != unit_blk_size ||
      num_workers > tip_sync->num_workers) {
    av2_tip_dealloc(tip_sync);
    tip_alloc(tip_sync, cm, num_workers, mvs_rows, unit_blk_size, step);
  }
  enqueue_tip_jobs(tip_sync, mvs_rows, step);

  // Setup TIP worker data and execute
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    tip_worker_data[i].calc_subpel_params_func = calc_subpel_params_func;
    tip_worker_data[i].copy_refined_mvs = copy_refined_mvs;
    tip_worker_data[i].cm = cm;

    TIPWorkerData *const tip_data = &tip_worker_data[i];
    worker->hook = tip_row_worker;
    worker->data1 = tip_sync;
    worker->data2 = tip_data;
    // Start the workers
    if (i == num_workers - 1) {
      winterface->execute(worker);
    } else {
      winterface->launch(worker);
    }
  }

  // Wait till all workers are finished
  for (int i = 0; i < num_workers; ++i) {
    winterface->sync(&workers[i]);
  }
}

// Allocate memory for loop restoration row worker data
static void loop_restoration_alloc(AV2LrSync *lr_sync, AV2_COMMON *cm,
                                   int num_workers, int num_rows_lr) {
  lr_sync->rows = num_rows_lr;
#if CONFIG_MULTITHREAD
  {
    CHECK_MEM_ERROR(cm, lr_sync->job_mutex,
                    avm_malloc(sizeof(*(lr_sync->job_mutex))));
    if (lr_sync->job_mutex) {
      pthread_mutex_init(lr_sync->job_mutex, NULL);
    }
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, lr_sync->lrworkerdata,
                  avm_malloc(num_workers * sizeof(*(lr_sync->lrworkerdata))));

  for (int worker_idx = 0; worker_idx < num_workers; ++worker_idx) {
    if (worker_idx < num_workers - 1) {
      CHECK_MEM_ERROR(
          cm, lr_sync->lrworkerdata[worker_idx].scratch_buf,
          avm_malloc(MAX_LRU_STRIPE_BUF_SIZE *
                     sizeof(*lr_sync->lrworkerdata[0].scratch_buf)));

    } else {
      lr_sync->lrworkerdata[worker_idx].scratch_buf = cm->lru_stripe_buf;
    }
  }

  lr_sync->num_workers = num_workers;
  CHECK_MEM_ERROR(cm, lr_sync->job_queue,
                  avm_malloc(sizeof(*lr_sync->job_queue) * num_rows_lr));
}

// Deallocate loop restoration worker data and scratch buffer
void av2_loop_restoration_dealloc(AV2LrSync *lr_sync, int num_workers) {
  if (lr_sync != NULL) {
#if CONFIG_MULTITHREAD
    if (lr_sync->job_mutex != NULL) {
      pthread_mutex_destroy(lr_sync->job_mutex);
      avm_free(lr_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD

    avm_free(lr_sync->job_queue);

    if (lr_sync->lrworkerdata) {
      for (int worker_idx = 0; worker_idx < num_workers - 1; worker_idx++) {
        LRWorkerData *const workerdata_data =
            lr_sync->lrworkerdata + worker_idx;

        avm_free(workerdata_data->scratch_buf);
      }
      avm_free(lr_sync->lrworkerdata);
    }

    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av2_zero(*lr_sync);
  }
}

// Compares the number of none type RUs processed between two row jobs and sorts
// the jobs
static int compare_lr_none_blk_proc(const void *a, const void *b) {
  const AV2LrMTInfo *structA = (const AV2LrMTInfo *)a;
  const AV2LrMTInfo *structB = (const AV2LrMTInfo *)b;

  if (structA->none_type_count < structB->none_type_count)
    return -1;
  else if (structA->none_type_count > structB->none_type_count)
    return 1;
  else
    return 0;
}

// Prepares lr stripe row job list and sorts the jobs in descending order based
// on number of none type LRU.
static void enqueue_lr_jobs(AV2LrSync *lr_sync, AV2LrStruct *lr_ctxt,
                            AV2_COMMON *cm) {
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;

  const int num_planes = av2_num_planes(cm);
  AV2LrMTInfo *lr_job_queue = lr_sync->job_queue;
  int32_t job_counter = 0;
  lr_sync->jobs_enqueued = 0;
  lr_sync->jobs_dequeued = 0;

  const int num_tile_rows = cm->tiles.rows;
  const int num_tile_cols = cm->tiles.cols;

  for (int plane = 0; plane < num_planes; plane++) {
    if (cm->rst_info[plane].frame_restoration_type == RESTORE_NONE) continue;
    const int is_uv = plane > 0;
    const int ss_y = is_uv && cm->seq_params.subsampling_y;
    const int unit_size = ctxt[plane].rsi->restoration_unit_size;

    for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
      for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
        AV2PixelRect tile_rect;
        TileInfo tile_info;
        av2_tile_init(&tile_info, cm, tile_row, tile_col);
        tile_rect = av2_get_tile_rect(&tile_info, cm, is_uv);
        const int unit_idx0 = get_ru_index_for_tile_start(&cm->rst_info[plane],
                                                          tile_row, tile_col);
        const int tile_h = tile_rect.bottom - tile_rect.top;
        int y0 = 0, lr_unit_row = 0;

        // Loop over tile height in steps of LR unit size.
        while (y0 < tile_h) {
          int remaining_h = tile_h - y0;
          int lru_height = (lr_unit_row ==
                            ctxt[plane].rsi->vert_units_per_tile[tile_row] - 1)
                               ? remaining_h
                               : unit_size;
          RestorationTileLimits limits;
          limits.v_start = tile_rect.top + y0;
          limits.v_end = tile_rect.top + y0 + lru_height;
          assert(limits.v_end <= tile_rect.bottom);
          // Offset the tile upwards to align with the restoration processing
          // stripe
          if (limits.v_start == tile_rect.top) {
            const int voffset = RESTORATION_UNIT_OFFSET >> ss_y;
            if (limits.v_end < tile_rect.bottom) limits.v_end -= voffset;
            lru_height = limits.v_end - limits.v_start;
          }

          RestorationTileLimits remaining_stripes = limits;
          int unit_row = 0;
          int unit_h = limits.v_end - limits.v_start;

          // Loop over LRU height in steps of stripe_height. Each stripe height
          // and tile row width is a job for
          // av2_foreach_rest_unit_in_tile_row(). Further, for each stripe
          // height there is a loop over tile width in steps of LRU size to
          // check NONE type based on which the jobs are sorted.
          while (unit_row < unit_h) {
            remaining_stripes.v_start = limits.v_start + unit_row;
            const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
            const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;
            const int rel_tile_stripe =
                (remaining_stripes.v_start - tile_rect.top + runit_offset) /
                full_stripe_height;
            const int nominal_stripe_height =
                full_stripe_height -
                ((rel_tile_stripe == 0) ? runit_offset : 0);
            const int proc_height =
                AVMMIN(nominal_stripe_height,
                       remaining_stripes.v_end - remaining_stripes.v_start);
            const int tile_w = tile_rect.right - tile_rect.left;
            int x0 = 0, stripe_col = 0, lru_none_count = 0;

            // Loop over tile width in steps of LR unit size.
            while (x0 < tile_w) {
              const int remaining_w = tile_w - x0;
              const int lru_width =
                  (stripe_col ==
                   ctxt[plane].rsi->horz_units_per_tile[tile_col] - 1)
                      ? remaining_w
                      : unit_size;

              limits.h_start = tile_rect.left + x0;
              limits.h_end = tile_rect.left + x0 + lru_width;
              assert(limits.h_end <= tile_rect.right);
              const int unit_idx =
                  unit_idx0 +
                  lr_unit_row * ctxt[plane].rsi->horz_units_per_frame +
                  stripe_col;
              const RestorationUnitInfo *lr_unit_info =
                  &ctxt[plane].rsi->unit_info[unit_idx];
              const RestorationType unit_rtype = lr_unit_info->restoration_type;
              if (unit_rtype == RESTORE_NONE) lru_none_count++;
              x0 += lru_width;
              ++stripe_col;
            }

            lr_job_queue[job_counter].lr_unit_row = lr_unit_row;
            lr_job_queue[job_counter].plane = plane;
            lr_job_queue[job_counter].limits = limits;
            lr_job_queue[job_counter].remaining_stripes = remaining_stripes;
            lr_job_queue[job_counter].tile_rect = tile_rect;
            lr_job_queue[job_counter].tile_row = tile_row;
            lr_job_queue[job_counter].tile_col = tile_col;
            lr_job_queue[job_counter].proc_height = proc_height;
            lr_job_queue[job_counter].start_height = unit_row;
            lr_job_queue[job_counter].none_type_count = lru_none_count;
            job_counter++;
            lr_sync->jobs_enqueued++;
            unit_row += proc_height;
          }
          y0 += lru_height;
          ++lr_unit_row;
        }
      }
    }
  }

  // The job which has maximum number of NONE type units should be evaluated at
  // last.
  qsort(lr_sync->job_queue, lr_sync->jobs_enqueued,
        sizeof(lr_sync->job_queue[0]), compare_lr_none_blk_proc);
}

static AV2LrMTInfo *get_lr_job_info(AV2LrSync *lr_sync) {
  AV2LrMTInfo *cur_job_info = NULL;

#if CONFIG_MULTITHREAD
  pthread_mutex_lock(lr_sync->job_mutex);

  if (lr_sync->jobs_dequeued < lr_sync->jobs_enqueued) {
    cur_job_info = lr_sync->job_queue + lr_sync->jobs_dequeued;
    lr_sync->jobs_dequeued++;
  }

  pthread_mutex_unlock(lr_sync->job_mutex);
#else
  (void)lr_sync;
#endif

  return cur_job_info;
}

// Implement row loop restoration for each thread.
static int loop_restoration_row_worker(void *arg1, void *arg2) {
  AV2LrSync *const lr_sync = (AV2LrSync *)arg1;
  LRWorkerData *lrworkerdata = (LRWorkerData *)arg2;
  AV2LrStruct *lr_ctxt = (AV2LrStruct *)lrworkerdata->lr_ctxt;
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;
  AV2_COMMON *cm = lrworkerdata->cm;

  while (1) {
    AV2LrMTInfo *cur_job_info = get_lr_job_info(lr_sync);
    if (cur_job_info == NULL) break;

    const int lr_unit_row = cur_job_info->lr_unit_row;
    const int start_height = cur_job_info->start_height;
    const int proc_height = cur_job_info->proc_height;
    const int plane = cur_job_info->plane;
    const int tile_row = cur_job_info->tile_row;
    const int tile_col = cur_job_info->tile_col;
    const int unit_idx0 =
        get_ru_index_for_tile_start(ctxt[plane].rsi, tile_row, tile_col);
    const int tile_stripe0 = get_top_stripe_idx_in_tile(
        tile_row, 0, cm, RESTORATION_PROC_UNIT_SIZE, RESTORATION_UNIT_OFFSET);

    av2_foreach_rest_unit_in_tile_row(
        &cur_job_info->limits, &cur_job_info->remaining_stripes,
        &cur_job_info->tile_rect, lr_unit_row, start_height, proc_height,
        ctxt[plane].rsi->restoration_unit_size, unit_idx0,
        ctxt[plane].rsi->horz_units_per_tile[tile_col],
        ctxt[plane].rsi->horz_units_per_frame, tile_stripe0, &ctxt[plane],
        lrworkerdata->scratch_buf);
  }
  return 1;
}

static void foreach_rest_unit_in_planes_mt(AV2LrStruct *lr_ctxt,
                                           AVxWorker *workers, int nworkers,
                                           AV2LrSync *lr_sync, AV2_COMMON *cm) {
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;

  uint16_t *luma = NULL;
  uint16_t *luma_buf = NULL;
  const YV12_BUFFER_CONFIG *dgd = &cm->cur_frame->buf;
  int luma_stride = dgd->widths[1] + 2 * WIENERNS_UV_BRD;
  const int num_planes = av2_num_planes(cm);

  const int num_tile_cols = cm->tiles.cols;
  const int num_tile_rows = cm->tiles.rows;

  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  int num_rows_lr = 0;

  for (int plane = 0; plane < num_planes; plane++) {
    if (cm->rst_info[plane].frame_restoration_type == RESTORE_NONE) continue;

    ctxt[plane].plane = plane;
    ctxt[plane].base_qindex = cm->quant_params.base_qindex;
    const int is_uv = (plane != AVM_PLANE_Y);
    if (is_uv && luma_buf == NULL)
      luma_buf = wienerns_copy_luma_with_virtual_lines(cm, &luma);
    ctxt[plane].luma = is_uv ? luma : NULL;
    ctxt[plane].luma_stride = is_uv ? luma_stride : -1;
    ctxt[plane].tskip = cm->mi_params.tx_skip[plane];
    ctxt[plane].tskip_stride = cm->mi_params.tx_skip_stride[plane];
    if (plane != AVM_PLANE_Y)
      ctxt[plane].qindex_offset = plane == AVM_PLANE_U
                                      ? cm->quant_params.u_ac_delta_q
                                      : cm->quant_params.v_ac_delta_q;
    else
      ctxt[plane].qindex_offset = 0;
    ctxt[plane].wiener_class_id = cm->mi_params.wiener_class_id[plane];
    ctxt[plane].wiener_class_id_stride =
        cm->mi_params.wiener_class_id_stride[plane];
    ctxt[plane].tskip_zero_flag = 0;

    TileInfo tile_info;
    const int ss_y = is_uv && cm->seq_params.subsampling_y;
    for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
      for (int tile_col = 0; tile_col < num_tile_cols; ++tile_col) {
        av2_tile_init(&tile_info, cm, tile_row, tile_col);
        AV2PixelRect tile_rect = av2_get_tile_rect(&tile_info, cm, is_uv);
        const int tile_h = tile_rect.bottom - tile_rect.top;

        int y0 = 0, lr_unit_row = 0;
        // Loop over tile height in steps of LR unit size.
        while (y0 < tile_h) {
          int remaining_h = tile_h - y0;
          int lru_height =
              (lr_unit_row ==
               cm->rst_info[plane].vert_units_per_tile[tile_row] - 1)
                  ? remaining_h
                  : cm->rst_info[plane].restoration_unit_size;
          RestorationTileLimits limits;
          limits.v_start = tile_rect.top + y0;
          limits.v_end = tile_rect.top + y0 + lru_height;
          if (limits.v_start == tile_rect.top) {
            const int voffset = RESTORATION_UNIT_OFFSET >> ss_y;
            if (limits.v_end < tile_rect.bottom) limits.v_end -= voffset;
            lru_height = limits.v_end - limits.v_start;
          }

          RestorationTileLimits remaining_stripes = limits;
          int unit_row = 0;
          int unit_h = limits.v_end - limits.v_start;

          // Loop over LRU height in steps of stripe_height. Count the
          // total number of rows in steps of stripe height.
          while (unit_row < unit_h) {
            remaining_stripes.v_start = limits.v_start + unit_row;
            const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
            const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;
            const int rel_tile_stripe =
                (remaining_stripes.v_start - tile_rect.top + runit_offset) /
                full_stripe_height;
            const int nominal_stripe_height =
                full_stripe_height -
                ((rel_tile_stripe == 0) ? runit_offset : 0);
            const int height =
                AVMMIN(nominal_stripe_height,
                       remaining_stripes.v_end - remaining_stripes.v_start);
            num_rows_lr++;
            unit_row += height;
          }
          y0 += lru_height;
          ++lr_unit_row;
        }
      }
    }
  }

  const int num_workers = nworkers;
  int i;
  assert(MAX_MB_PLANE == 3);

  if (num_rows_lr != lr_sync->rows || num_workers > lr_sync->num_workers) {
    av2_loop_restoration_dealloc(lr_sync, num_workers);
    loop_restoration_alloc(lr_sync, cm, num_workers, num_rows_lr);
  }

  enqueue_lr_jobs(lr_sync, lr_ctxt, cm);

  // Set up looprestoration thread data.
  for (i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    lr_sync->lrworkerdata[i].lr_ctxt = (void *)lr_ctxt;
    lr_sync->lrworkerdata[i].cm = cm;
    worker->hook = loop_restoration_row_worker;
    worker->data1 = lr_sync;
    worker->data2 = &lr_sync->lrworkerdata[i];

    // Start loopfiltering
    if (i == num_workers - 1) {
      winterface->execute(worker);
    } else {
      winterface->launch(worker);
    }
  }

  // Wait till all rows are finished
  for (i = 0; i < num_workers; ++i) {
    winterface->sync(&workers[i]);
  }
  if (luma_buf != NULL) free(luma_buf);
}

void av2_loop_restoration_filter_frame_mt(YV12_BUFFER_CONFIG *frame,
                                          AV2_COMMON *cm, int optimized_lr,
                                          AVxWorker *workers, int num_workers,
                                          AV2LrSync *lr_sync, void *lr_ctxt) {
  assert(!cm->features.all_lossless);

  const int num_planes = av2_num_planes(cm);

  AV2LrStruct *loop_rest_ctxt = (AV2LrStruct *)lr_ctxt;

  av2_loop_restoration_filter_frame_init(loop_rest_ctxt, frame, cm,
                                         optimized_lr, num_planes);

  foreach_rest_unit_in_planes_mt(loop_rest_ctxt, workers, num_workers, lr_sync,
                                 cm);
  av2_loop_restoration_copy_planes(loop_rest_ctxt, cm, num_planes);
}

// Initializes cdef_sync parameters.
static AVM_INLINE void reset_cdef_job_info(AV2CdefSync *cdef_sync) {
  cdef_sync->end_of_frame = 0;
  cdef_sync->fbr = 0;
  cdef_sync->fbc = 0;
}

// Launch all CDEF workers for row multithreading
static AVM_INLINE void launch_cdef_workers(AVxWorker *const workers,
                                           int num_workers) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    if (i == num_workers - 1)
      winterface->execute(worker);
    else
      winterface->launch(worker);
  }
}

// Synchronize all CDEF workers for the completion of Cdef frame.
static AVM_INLINE void sync_cdef_workers(AVxWorker *const workers,
                                         AV2_COMMON *const cm,
                                         int num_workers) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();
  int had_error = 0;

  // Wait for completion of Cdef frame.
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    had_error |= !winterface->sync(worker);
  }
  if (had_error)
    avm_internal_error(&cm->error, AVM_CODEC_ERROR,
                       "Failed to process cdef frame");
}

// Updates the row index of the next job to be processed.
// Also updates end_of_frame flag when the processing of all rows is complete.
static void update_cdef_row_next_job_info(AV2CdefSync *cdef_sync, int nvfb) {
  cdef_sync->fbr++;
  if (cdef_sync->fbr == nvfb) {
    cdef_sync->end_of_frame = 1;
  }
}

// Checks if a job is available. If job is available,
// populates next job information and returns 1, else returns 0.
static AVM_INLINE int get_cdef_row_next_job(AV2CdefSync *cdef_sync,
                                            int *cur_fbr, const int nvfb) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(cdef_sync->mutex_);
#endif  // CONFIG_MULTITHREAD
  int do_next_row = 0;
  // Populates information needed for current job and update the row
  // index of the next row to be processed.
  if (cdef_sync->end_of_frame == 0) {
    do_next_row = 1;
    *cur_fbr = cdef_sync->fbr;
    update_cdef_row_next_job_info(cdef_sync, nvfb);
  }
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(cdef_sync->mutex_);
#endif  // CONFIG_MULTITHREAD
  return do_next_row;
}

// CDEF worker hook for row multi-threading
static int cdef_sb_row_worker_hook(void *arg1, void *arg2) {
  AV2CdefSync *const cdef_sync = (AV2CdefSync *)arg1;
  AV2CdefWorkerData *cdef_worker = (AV2CdefWorkerData *)arg2;
  const int nvfb =
      (cdef_worker->cm->mi_params.mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  int cur_fbr;
  while (get_cdef_row_next_job(cdef_sync, &cur_fbr, nvfb)) {
    av2_cdef_fb_row(cdef_worker->cm, cdef_worker->xd, cdef_worker->linebuf,
                    cdef_worker->colbuf, cdef_worker->srcbuf, cur_fbr,
                    cdef_worker->cdef_init_fb_row_fn, cdef_sync);
  }
  return 1;
}

// Assigns CDEF hook function and thread data to each worker.
static void prepare_cdef_frame_workers(AV2_COMMON *cm, MACROBLOCKD *xd,
                                       AV2CdefWorkerData *cdef_worker,
                                       AVxWorkerHook hook, AVxWorker *workers,
                                       AV2CdefSync *cdef_sync, int num_workers,
                                       cdef_init_fb_row_t cdef_init_fb_row_fn) {
  const int num_planes = av2_num_planes(cm);

  cdef_worker[0].srcbuf = cm->cdef_info.srcbuf;
  for (int plane = 0; plane < num_planes; plane++)
    cdef_worker[0].colbuf[plane] = cm->cdef_info.colbuf[plane];
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *worker = &workers[i];
    cdef_worker[i].cm = cm;
    cdef_worker[i].xd = xd;
    cdef_worker[i].cdef_init_fb_row_fn = cdef_init_fb_row_fn;
    for (int plane = 0; plane < num_planes; plane++)
      cdef_worker[i].linebuf[plane] = cm->cdef_info.linebuf[plane];

    worker->hook = hook;
    worker->data1 = cdef_sync;
    worker->data2 = &cdef_worker[i];
  }
}

void av2_cdef_init_fb_row_mt(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                             CdefBlockInfo *const fb_info,
                             uint16_t **const linebuf, uint16_t *const src,
                             struct AV2CdefSyncData *const cdef_sync, int fbr) {
  const int num_planes = av2_num_planes(cm);
  const int nvfb = (cm->mi_params.mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  const int luma_stride =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols << MI_SIZE_LOG2, 4);

  // for the current filter block, it's top left corner mi structure (mi_tl)
  // is first accessed to check whether the top and left boundaries are
  // frame boundaries. Then bottom-left and top-right mi structures are
  // accessed to check whether the bottom and right boundaries
  // (respectively) are frame boundaries.
  //
  // Note that we can't just check the bottom-right mi structure - eg. if
  // we're at the right-hand edge of the frame but not the bottom, then
  // the bottom-right mi is NULL but the bottom-left is not.
  fb_info->frame_boundary[TOP] = (MI_SIZE_64X64 * fbr == 0) ? 1 : 0;
  if (fbr != nvfb - 1)
    fb_info->frame_boundary[BOTTOM] =
        (MI_SIZE_64X64 * (fbr + 1) == cm->mi_params.mi_rows) ? 1 : 0;
  else
    fb_info->frame_boundary[BOTTOM] = 1;

  fb_info->src = src;
  fb_info->damping = cm->cdef_info.cdef_damping;
  fb_info->coeff_shift = AVMMAX(cm->seq_params.bit_depth - 8, 0);
  av2_zero(fb_info->dir);
  av2_zero(fb_info->var);

  for (int plane = 0; plane < num_planes; plane++) {
    const int stride = luma_stride >> xd->plane[plane].subsampling_x;
    const int mi_high_l2 = MI_SIZE_LOG2 - xd->plane[plane].subsampling_y;
    const int top_offset = MI_SIZE_64X64 * (fbr + 1) << mi_high_l2;
    const int bot_offset = MI_SIZE_64X64 * (fbr + 1) << mi_high_l2;
    uint16_t *top_linebuf = &linebuf[plane][0];
    uint16_t *bot_linebuf = &linebuf[plane][nvfb * CDEF_VBORDER * stride];

    if (fbr != nvfb - 1)  // if (fbr != 0)  // top line buffer copy
      av2_cdef_copy_sb8_16(cm, &top_linebuf[(fbr + 1) * CDEF_VBORDER * stride],
                           stride, xd->plane[plane].dst.buf,
                           top_offset - CDEF_VBORDER, 0,
                           xd->plane[plane].dst.stride, CDEF_VBORDER, stride);
    if (fbr != nvfb - 1)  // bottom line buffer copy
      av2_cdef_copy_sb8_16(cm, &bot_linebuf[fbr * CDEF_VBORDER * stride],
                           stride, xd->plane[plane].dst.buf, bot_offset, 0,
                           xd->plane[plane].dst.stride, CDEF_VBORDER, stride);

    fb_info->top_linebuf[plane] = &linebuf[plane][fbr * CDEF_VBORDER * stride];
    fb_info->bot_linebuf[plane] =
        &linebuf[plane]
                [nvfb * CDEF_VBORDER * stride + (fbr * CDEF_VBORDER * stride)];
  }

  cdef_row_mt_sync_write(cdef_sync, fbr);
  cdef_row_mt_sync_read(cdef_sync, fbr);
}

void av2_cdef_frame_mt(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                       AV2CdefWorkerData *const cdef_worker,
                       AVxWorker *const workers, AV2CdefSync *const cdef_sync,
                       int num_workers,
                       cdef_init_fb_row_t cdef_init_fb_row_fn) {
  YV12_BUFFER_CONFIG *frame = &cm->cur_frame->buf;
  const int num_planes = av2_num_planes(cm);

  av2_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  reset_cdef_job_info(cdef_sync);
  prepare_cdef_frame_workers(cm, xd, cdef_worker, cdef_sb_row_worker_hook,
                             workers, cdef_sync, num_workers,
                             cdef_init_fb_row_fn);
  launch_cdef_workers(workers, num_workers);
  sync_cdef_workers(workers, cm, num_workers);
}
