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

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "av1/common/av1_loopfilter.h"
#include "av1/common/entropymode.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/thread_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/resize.h"
#include "av1/common/restoration.h"
#include "av1/common/ccso.h"

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
static void loop_filter_alloc(AV1LfSync *lf_sync, AV1_COMMON *cm, int rows,
                              int width, int num_workers) {
  lf_sync->rows = rows;
#if CONFIG_MULTITHREAD
  {
    int i, j;

    for (j = 0; j < MAX_MB_PLANE; j++) {
      CHECK_MEM_ERROR(cm, lf_sync->mutex_[j],
                      aom_malloc(sizeof(*(lf_sync->mutex_[j])) * rows));
      if (lf_sync->mutex_[j]) {
        for (i = 0; i < rows; ++i) {
          pthread_mutex_init(&lf_sync->mutex_[j][i], NULL);
        }
      }

      CHECK_MEM_ERROR(cm, lf_sync->cond_[j],
                      aom_malloc(sizeof(*(lf_sync->cond_[j])) * rows));
      if (lf_sync->cond_[j]) {
        for (i = 0; i < rows; ++i) {
          pthread_cond_init(&lf_sync->cond_[j][i], NULL);
        }
      }
    }

    CHECK_MEM_ERROR(cm, lf_sync->job_mutex,
                    aom_malloc(sizeof(*(lf_sync->job_mutex))));
    if (lf_sync->job_mutex) {
      pthread_mutex_init(lf_sync->job_mutex, NULL);
    }
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, lf_sync->lfdata,
                  aom_malloc(num_workers * sizeof(*(lf_sync->lfdata))));
  lf_sync->num_workers = num_workers;

  for (int j = 0; j < MAX_MB_PLANE; j++) {
    CHECK_MEM_ERROR(cm, lf_sync->cur_sb_col[j],
                    aom_malloc(sizeof(*(lf_sync->cur_sb_col[j])) * rows));
  }
  CHECK_MEM_ERROR(
      cm, lf_sync->job_queue,
      aom_malloc(sizeof(*(lf_sync->job_queue)) * rows * MAX_MB_PLANE * 2));
  // Set up nsync.
  lf_sync->sync_range = get_sync_range(width);
}

// Deallocate lf synchronization related mutex and data
void av1_loop_filter_dealloc(AV1LfSync *lf_sync) {
  if (lf_sync != NULL) {
    int j;
#if CONFIG_MULTITHREAD
    int i;
    for (j = 0; j < MAX_MB_PLANE; j++) {
      if (lf_sync->mutex_[j] != NULL) {
        for (i = 0; i < lf_sync->rows; ++i) {
          pthread_mutex_destroy(&lf_sync->mutex_[j][i]);
        }
        aom_free(lf_sync->mutex_[j]);
      }
      if (lf_sync->cond_[j] != NULL) {
        for (i = 0; i < lf_sync->rows; ++i) {
          pthread_cond_destroy(&lf_sync->cond_[j][i]);
        }
        aom_free(lf_sync->cond_[j]);
      }
    }
    if (lf_sync->job_mutex != NULL) {
      pthread_mutex_destroy(lf_sync->job_mutex);
      aom_free(lf_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD
    aom_free(lf_sync->lfdata);
    for (j = 0; j < MAX_MB_PLANE; j++) {
      aom_free(lf_sync->cur_sb_col[j]);
    }

    aom_free(lf_sync->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av1_zero(*lf_sync);
  }
}

static void loop_filter_data_reset(LFWorkerData *lf_data,
                                   YV12_BUFFER_CONFIG *frame_buffer,
                                   struct AV1Common *cm, MACROBLOCKD *xd) {
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

void av1_alloc_cdef_sync(AV1_COMMON *const cm, AV1CdefSync *cdef_sync,
                         int num_workers) {
  if (num_workers < 1) return;
#if CONFIG_MULTITHREAD
  if (cdef_sync->mutex_ == NULL) {
    CHECK_MEM_ERROR(cm, cdef_sync->mutex_,
                    aom_malloc(sizeof(*(cdef_sync->mutex_))));
    if (cdef_sync->mutex_) pthread_mutex_init(cdef_sync->mutex_, NULL);
  }
#else
  (void)cm;
  (void)cdef_sync;
#endif  // CONFIG_MULTITHREAD
}

void av1_free_cdef_sync(AV1CdefSync *cdef_sync) {
  if (cdef_sync == NULL) return;
#if CONFIG_MULTITHREAD
  if (cdef_sync->mutex_ != NULL) {
    pthread_mutex_destroy(cdef_sync->mutex_);
    aom_free(cdef_sync->mutex_);
  }
#endif  // CONFIG_MULTITHREAD
}

// Wait for the previous row to complete in cdef row multi-thread.
static INLINE void cdef_row_mt_sync_read(AV1CdefSync *cdef_sync, int row) {
  if (!row) return;
#if CONFIG_MULTITHREAD
  AV1CdefRowSync *cdef_row_mt = cdef_sync->cdef_row_mt;
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
static INLINE void cdef_row_mt_sync_write(AV1CdefSync *cdef_sync, int row) {
#if CONFIG_MULTITHREAD
  AV1CdefRowSync *cdef_row_mt = cdef_sync->cdef_row_mt;
  pthread_mutex_lock(cdef_row_mt[row].row_mutex_);
  pthread_cond_signal(cdef_row_mt[row].row_cond_);
  cdef_row_mt[row].is_row_done = 1;
  pthread_mutex_unlock(cdef_row_mt[row].row_mutex_);
#else
  (void)cdef_sync;
  (void)row;
#endif  // CONFIG_MULTITHREAD
}

static INLINE void sync_read(AV1LfSync *const lf_sync, int r, int c,
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

static INLINE void sync_write(AV1LfSync *const lf_sync, int r, int c,
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

static void enqueue_lf_jobs(AV1LfSync *lf_sync, AV1_COMMON *cm, int start,
                            int stop, int plane_start, int plane_end,
                            int mib_size) {
  int mi_row, plane, dir;
  AV1LfMTInfo *lf_job_queue = lf_sync->job_queue;
  lf_sync->jobs_enqueued = 0;
  lf_sync->jobs_dequeued = 0;

  for (dir = 0; dir < 2; dir++) {
    for (plane = plane_start; plane < plane_end; plane++) {
      if (plane == 0 && !(cm->lf.filter_level[0]) && !(cm->lf.filter_level[1]))
        break;
      else if (plane == 1 && !(cm->lf.filter_level_u))
        continue;
      else if (plane == 2 && !(cm->lf.filter_level_v))
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

static AV1LfMTInfo *get_lf_job_info(AV1LfSync *lf_sync) {
  AV1LfMTInfo *cur_job_info = NULL;

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
    const YV12_BUFFER_CONFIG *const frame_buffer, AV1_COMMON *const cm,
    struct macroblockd_plane *planes, MACROBLOCKD *xd,
    AV1LfSync *const lf_sync) {
  const int mib_size = cm->mib_size;
  const int mib_size_log2 = cm->mib_size_log2;
  const int sb_cols =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols, mib_size_log2) >> mib_size_log2;
  int mi_row, mi_col, plane, dir;
  int r, c;

  while (1) {
    AV1LfMTInfo *cur_job_info = get_lf_job_info(lf_sync);

    if (cur_job_info != NULL) {
      mi_row = cur_job_info->mi_row;
      plane = cur_job_info->plane;
      dir = cur_job_info->dir;
      r = mi_row >> mib_size_log2;

      if (dir == 0) {
        for (mi_col = 0; mi_col < cm->mi_params.mi_cols; mi_col += mib_size) {
          c = mi_col >> mib_size_log2;

          av1_setup_dst_planes(planes, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);

          av1_filter_block_plane_vert(cm, xd, plane, &planes[plane], mi_row,
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

          av1_setup_dst_planes(planes, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av1_filter_block_plane_horz(cm, xd, plane, &planes[plane], mi_row,
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
  AV1LfSync *const lf_sync = (AV1LfSync *)arg1;
  LFWorkerData *const lf_data = (LFWorkerData *)arg2;
  thread_loop_filter_rows(lf_data->frame_buffer, lf_data->cm, lf_data->planes,
                          lf_data->xd, lf_sync);
  return 1;
}

static void loop_filter_rows_mt(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm,
                                MACROBLOCKD *xd, int start, int stop,
                                int plane_start, int plane_end,
                                AVxWorker *workers, int nworkers,
                                AV1LfSync *lf_sync) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  const int mib_size = cm->mib_size;
  const int mib_size_log2 = cm->mib_size_log2;
  // Number of superblock rows and cols
  const int sb_rows =
      ALIGN_POWER_OF_TWO(cm->mi_params.mi_rows, mib_size_log2) >> mib_size_log2;
  const int num_workers = nworkers;
  int i;

  if (!lf_sync->sync_range || sb_rows != lf_sync->rows ||
      num_workers > lf_sync->num_workers) {
    av1_loop_filter_dealloc(lf_sync);
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

void av1_loop_filter_frame_mt(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm,
                              MACROBLOCKD *xd, int plane_start, int plane_end,
                              int partial_frame, AVxWorker *workers,
                              int num_workers, AV1LfSync *lf_sync) {
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif

  int start_mi_row, end_mi_row, mi_rows_to_filter;

  start_mi_row = 0;
  mi_rows_to_filter = cm->mi_params.mi_rows;
  if (partial_frame && cm->mi_params.mi_rows > 8) {
    start_mi_row = cm->mi_params.mi_rows >> 1;
    start_mi_row &= 0xfffffff8;
    mi_rows_to_filter = AOMMAX(cm->mi_params.mi_rows / 8, 8);
  }
  end_mi_row = start_mi_row + mi_rows_to_filter;
  av1_loop_filter_frame_init(cm, plane_start, plane_end);

  loop_filter_rows_mt(frame, cm, xd, start_mi_row, end_mi_row, plane_start,
                      plane_end, workers, num_workers, lf_sync);
}

// Initialize ccso worker data
static INLINE void ccso_data_reset(CCSOWorkerData *ccsoworkerdata,
                                   AV1_COMMON *cm, MACROBLOCKD *xd,
                                   uint16_t *ext_rec_y) {
  ccsoworkerdata->cm = cm;
  ccsoworkerdata->xd = xd;
  ccsoworkerdata->src_y = ext_rec_y;
}

// Allocate memory for ccso row synchronization
static void ccso_filter_alloc(AV1CcsoSync *ccso_sync, AV1_COMMON *cm, int rows,
                              int num_workers) {
  ccso_sync->rows = rows;
#if CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, ccso_sync->job_mutex,
                  aom_malloc(sizeof(*(ccso_sync->job_mutex))));
  if (ccso_sync->job_mutex) {
    pthread_mutex_init(ccso_sync->job_mutex, NULL);
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(
      cm, ccso_sync->ccsoworkerdata,
      aom_malloc(num_workers * sizeof(*(ccso_sync->ccsoworkerdata))));
  ccso_sync->num_workers = num_workers;

  CHECK_MEM_ERROR(cm, ccso_sync->job_queue,
                  aom_malloc(sizeof(*(ccso_sync->job_queue)) * rows));
}

// Deallocate ccso synchronization related mutex and data
void av1_ccso_filter_dealloc(AV1CcsoSync *ccso_sync) {
  if (ccso_sync != NULL) {
#if CONFIG_MULTITHREAD
    if (ccso_sync->job_mutex != NULL) {
      pthread_mutex_destroy(ccso_sync->job_mutex);
      aom_free(ccso_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD
    aom_free(ccso_sync->ccsoworkerdata);

    aom_free(ccso_sync->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av1_zero(*ccso_sync);
  }
}

// Compares the number of CCSO blocks processed between two rows used for job
// sorting
static int compare_ccso_blk_proc(const void *a, const void *b) {
  const AV1CCSOMTInfo *structA = (const AV1CCSOMTInfo *)a;
  const AV1CCSOMTInfo *structB = (const AV1CCSOMTInfo *)b;

  if (structA->blk_proc < structB->blk_proc) {
    return 1;  // structA comes after structB
  } else if (structA->blk_proc > structB->blk_proc) {
    return -1;  // structA comes before structB
  } else {
    return 0;  // Elements are equal
  }
}

// Generates job list for ccso filter
static void enqueue_ccso_jobs(AV1CcsoSync *ccso_sync, AV1_COMMON *cm,
                              MACROBLOCKD *xd) {
  AV1CCSOMTInfo *ccso_job_queue = ccso_sync->job_queue;
  ccso_sync->jobs_enqueued = 0;
  ccso_sync->jobs_dequeued = 0;
  const int num_planes = av1_num_planes(cm);
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
static AV1CCSOMTInfo *get_ccso_job_info(AV1CcsoSync *ccso_sync) {
  AV1CCSOMTInfo *cur_job_info = NULL;

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
static INLINE void process_ccso_rows(AV1_COMMON *const cm, MACROBLOCKD *xd,
                                     uint16_t *src_y,
                                     AV1CcsoSync *const ccso_sync) {
  int src_cls[2];
  int src_loc[2];
  const int ccso_ext_stride = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  const int blk_log2 = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);

  while (1) {
    AV1CCSOMTInfo *cur_job_info = get_ccso_job_info(ccso_sync);
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
        AOMMAX(xd->plane[plane].subsampling_x, xd->plane[plane].subsampling_y) +
        MI_SIZE_LOG2;
    const int y_uv_vscale = xd->plane[plane].subsampling_y;
    derive_ccso_sample_pos(src_loc, ccso_ext_stride, filter_sup);
    if (plane != 0) {
      blk_log2_x -= cm->seq_params.subsampling_x;
      blk_log2_y -= cm->seq_params.subsampling_y;
    }
    const int unit_log2_x = AOMMIN(proc_unit_log2, blk_log2_x);
    const int unit_log2_y = AOMMIN(proc_unit_log2, blk_log2_y);
    const int blk_size = 1 << blk_log2;
    const int blk_log2_proc = CCSO_PROC_BLK_LOG2;
    const int blk_size_proc = 1 << blk_log2_proc;
    const uint16_t *src_y_temp =
        src_y + (CCSO_PADDING_SIZE * ccso_ext_stride + CCSO_PADDING_SIZE) +
        (row >> CCSO_PROC_BLK_LOG2) *
            (ccso_ext_stride << (blk_log2_proc + y_uv_vscale));
    uint16_t *dst_yuv_temp =
        dst_yuv + (row >> CCSO_PROC_BLK_LOG2) * (dst_stride << blk_log2_proc);

    av1_apply_ccso_filter_for_row(
        cm, xd, src_y_temp, dst_yuv_temp, src_loc, src_cls, row, thr, blk_size,
        blk_size_proc, blk_log2_x, blk_log2_y, unit_log2_x, unit_log2_y, plane);
  }
}

// Row based processing of ccso worker
static int ccso_row_worker(void *arg1, void *arg2) {
  AV1CcsoSync *ccso_sync = (AV1CcsoSync *)arg1;
  CCSOWorkerData *ccso_data = (CCSOWorkerData *)arg2;
  process_ccso_rows(ccso_data->cm, ccso_data->xd, ccso_data->src_y, ccso_sync);
  return 1;
}

// Apply CCSO filter for frame with multithread support
static void apply_ccso_filter_mt(AVxWorker *workers, int nworkers,
                                 AV1_COMMON *const cm, MACROBLOCKD *const xd,
                                 uint16_t *ext_rec_y, AV1CcsoSync *ccso_sync) {
  const int num_planes = av1_num_planes(cm);
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
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
    av1_ccso_filter_dealloc(ccso_sync);
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

void av1_ccso_frame_mt(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm,
                       MACROBLOCKD *xd, AVxWorker *workers, int num_workers,
                       uint16_t *ext_rec_y, AV1CcsoSync *ccso_sync) {
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  apply_ccso_filter_mt(workers, num_workers, cm, xd, ext_rec_y, ccso_sync);
}

// Allocate memory for loop restoration row worker data
static void loop_restoration_alloc(AV1LrSync *lr_sync, AV1_COMMON *cm,
                                   int num_workers, int num_rows_lr) {
  lr_sync->rows = num_rows_lr;
#if CONFIG_MULTITHREAD
  {
    CHECK_MEM_ERROR(cm, lr_sync->job_mutex,
                    aom_malloc(sizeof(*(lr_sync->job_mutex))));
    if (lr_sync->job_mutex) {
      pthread_mutex_init(lr_sync->job_mutex, NULL);
    }
  }
#endif  // CONFIG_MULTITHREAD
  CHECK_MEM_ERROR(cm, lr_sync->lrworkerdata,
                  aom_malloc(num_workers * sizeof(*(lr_sync->lrworkerdata))));

  for (int worker_idx = 0; worker_idx < num_workers; ++worker_idx) {
    if (worker_idx < num_workers - 1) {
      CHECK_MEM_ERROR(
          cm, lr_sync->lrworkerdata[worker_idx].scratch_buf,
          aom_malloc(MAX_LRU_STRIPE_BUF_SIZE *
                     sizeof(*lr_sync->lrworkerdata[0].scratch_buf)));

    } else {
      lr_sync->lrworkerdata[worker_idx].scratch_buf = cm->lru_stripe_buf;
    }
  }

  lr_sync->num_workers = num_workers;
  CHECK_MEM_ERROR(cm, lr_sync->job_queue,
                  aom_malloc(sizeof(*lr_sync->job_queue) * num_rows_lr));
}

// Deallocate loop restoration worker data and scratch buffer
void av1_loop_restoration_dealloc(AV1LrSync *lr_sync, int num_workers) {
  if (lr_sync != NULL) {
#if CONFIG_MULTITHREAD
    if (lr_sync->job_mutex != NULL) {
      pthread_mutex_destroy(lr_sync->job_mutex);
      aom_free(lr_sync->job_mutex);
    }
#endif  // CONFIG_MULTITHREAD

    aom_free(lr_sync->job_queue);

    if (lr_sync->lrworkerdata) {
      for (int worker_idx = 0; worker_idx < num_workers - 1; worker_idx++) {
        LRWorkerData *const workerdata_data =
            lr_sync->lrworkerdata + worker_idx;

        aom_free(workerdata_data->scratch_buf);
      }
      aom_free(lr_sync->lrworkerdata);
    }

    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av1_zero(*lr_sync);
  }
}

// Compares the number of none type RUs processed between two row jobs and sorts
// the jobs
static int compare_lr_none_blk_proc(const void *a, const void *b) {
  const AV1LrMTInfo *structA = (const AV1LrMTInfo *)a;
  const AV1LrMTInfo *structB = (const AV1LrMTInfo *)b;

  if (structA->none_type_count < structB->none_type_count)
    return -1;
  else if (structA->none_type_count > structB->none_type_count)
    return 1;
  else
    return 0;
}

// Prepares lr stripe row job list and sorts the jobs in descending order based
// on number of none type LRU.
static void enqueue_lr_jobs(AV1LrSync *lr_sync, AV1LrStruct *lr_ctxt,
                            AV1_COMMON *cm) {
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;

  const int num_planes = av1_num_planes(cm);
  AV1LrMTInfo *lr_job_queue = lr_sync->job_queue;
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
        AV1PixelRect tile_rect;
        TileInfo tile_info;
        av1_tile_init(&tile_info, cm, tile_row, tile_col);
        tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
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
          // av1_foreach_rest_unit_in_tile_row(). Further, for each stripe
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
                AOMMIN(nominal_stripe_height,
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

static AV1LrMTInfo *get_lr_job_info(AV1LrSync *lr_sync) {
  AV1LrMTInfo *cur_job_info = NULL;

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
  AV1LrSync *const lr_sync = (AV1LrSync *)arg1;
  LRWorkerData *lrworkerdata = (LRWorkerData *)arg2;
  AV1LrStruct *lr_ctxt = (AV1LrStruct *)lrworkerdata->lr_ctxt;
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;
  AV1_COMMON *cm = lrworkerdata->cm;

  while (1) {
    AV1LrMTInfo *cur_job_info = get_lr_job_info(lr_sync);
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

    av1_foreach_rest_unit_in_tile_row(
        &cur_job_info->limits, &cur_job_info->remaining_stripes,
        &cur_job_info->tile_rect, lr_unit_row, start_height, proc_height,
        ctxt[plane].rsi->restoration_unit_size, unit_idx0,
        ctxt[plane].rsi->horz_units_per_tile[tile_col],
        ctxt[plane].rsi->horz_units_per_frame, tile_stripe0, &ctxt[plane],
        lrworkerdata->scratch_buf);
  }
  return 1;
}

static void foreach_rest_unit_in_planes_mt(AV1LrStruct *lr_ctxt,
                                           AVxWorker *workers, int nworkers,
                                           AV1LrSync *lr_sync, AV1_COMMON *cm) {
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;

  uint16_t *luma = NULL;
  uint16_t *luma_buf = NULL;
  const YV12_BUFFER_CONFIG *dgd = &cm->cur_frame->buf;
  int luma_stride = dgd->widths[1] + 2 * WIENERNS_UV_BRD;
  const int num_planes = av1_num_planes(cm);

  const int num_tile_cols = cm->tiles.cols;
  const int num_tile_rows = cm->tiles.rows;

  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  int num_rows_lr = 0;

  for (int plane = 0; plane < num_planes; plane++) {
    if (cm->rst_info[plane].frame_restoration_type == RESTORE_NONE) continue;

    ctxt[plane].plane = plane;
    ctxt[plane].base_qindex = cm->quant_params.base_qindex;
    const int is_uv = (plane != AOM_PLANE_Y);
    if (is_uv && luma_buf == NULL)
      luma_buf = wienerns_copy_luma_with_virtual_lines(cm, &luma);
    ctxt[plane].luma = is_uv ? luma : NULL;
    ctxt[plane].luma_stride = is_uv ? luma_stride : -1;
    ctxt[plane].tskip = cm->mi_params.tx_skip[plane];
    ctxt[plane].tskip_stride = cm->mi_params.tx_skip_stride[plane];
    if (plane != AOM_PLANE_Y)
      ctxt[plane].qindex_offset = plane == AOM_PLANE_U
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
        av1_tile_init(&tile_info, cm, tile_row, tile_col);
        AV1PixelRect tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
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
                AOMMIN(nominal_stripe_height,
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
    av1_loop_restoration_dealloc(lr_sync, num_workers);
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

void av1_loop_restoration_filter_frame_mt(YV12_BUFFER_CONFIG *frame,
                                          AV1_COMMON *cm, int optimized_lr,
                                          AVxWorker *workers, int num_workers,
                                          AV1LrSync *lr_sync, void *lr_ctxt) {
  assert(!cm->features.all_lossless);

  const int num_planes = av1_num_planes(cm);

  AV1LrStruct *loop_rest_ctxt = (AV1LrStruct *)lr_ctxt;

  av1_loop_restoration_filter_frame_init(loop_rest_ctxt, frame, cm,
                                         optimized_lr, num_planes);

  foreach_rest_unit_in_planes_mt(loop_rest_ctxt, workers, num_workers, lr_sync,
                                 cm);
  av1_loop_restoration_copy_planes(loop_rest_ctxt, cm, num_planes);
}

// Initializes cdef_sync parameters.
static AOM_INLINE void reset_cdef_job_info(AV1CdefSync *cdef_sync) {
  cdef_sync->end_of_frame = 0;
  cdef_sync->fbr = 0;
  cdef_sync->fbc = 0;
}

// Launch all CDEF workers for row multithreading
static AOM_INLINE void launch_cdef_workers(AVxWorker *const workers,
                                           int num_workers) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    if (i == num_workers - 1)
      winterface->execute(worker);
    else
      winterface->launch(worker);
  }
}

// Synchronize all CDEF workers for the completion of Cdef frame.
static AOM_INLINE void sync_cdef_workers(AVxWorker *const workers,
                                         AV1_COMMON *const cm,
                                         int num_workers) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  int had_error = 0;

  // Wait for completion of Cdef frame.
  for (int i = 0; i < num_workers; ++i) {
    AVxWorker *const worker = &workers[i];
    had_error |= !winterface->sync(worker);
  }
  if (had_error)
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Failed to process cdef frame");
}

// Updates the row index of the next job to be processed.
// Also updates end_of_frame flag when the processing of all rows is complete.
static void update_cdef_row_next_job_info(AV1CdefSync *cdef_sync, int nvfb) {
  cdef_sync->fbr++;
  if (cdef_sync->fbr == nvfb) {
    cdef_sync->end_of_frame = 1;
  }
}

// Checks if a job is available. If job is available,
// populates next job information and returns 1, else returns 0.
static AOM_INLINE int get_cdef_row_next_job(AV1CdefSync *cdef_sync,
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
  AV1CdefSync *const cdef_sync = (AV1CdefSync *)arg1;
  AV1CdefWorkerData *cdef_worker = (AV1CdefWorkerData *)arg2;
  const int nvfb =
      (cdef_worker->cm->mi_params.mi_rows + MI_SIZE_64X64 - 1) / MI_SIZE_64X64;
  int cur_fbr;
  while (get_cdef_row_next_job(cdef_sync, &cur_fbr, nvfb)) {
    av1_cdef_fb_row(cdef_worker->cm, cdef_worker->xd, cdef_worker->linebuf,
                    cdef_worker->colbuf, cdef_worker->srcbuf, cur_fbr,
                    cdef_worker->cdef_init_fb_row_fn, cdef_sync);
  }
  return 1;
}

// Assigns CDEF hook function and thread data to each worker.
static void prepare_cdef_frame_workers(AV1_COMMON *cm, MACROBLOCKD *xd,
                                       AV1CdefWorkerData *cdef_worker,
                                       AVxWorkerHook hook, AVxWorker *workers,
                                       AV1CdefSync *cdef_sync, int num_workers,
                                       cdef_init_fb_row_t cdef_init_fb_row_fn) {
  const int num_planes = av1_num_planes(cm);

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

void av1_cdef_init_fb_row_mt(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                             CdefBlockInfo *const fb_info,
                             uint16_t **const linebuf, uint16_t *const src,
                             struct AV1CdefSyncData *const cdef_sync, int fbr) {
  const int num_planes = av1_num_planes(cm);
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
  fb_info->coeff_shift = AOMMAX(cm->seq_params.bit_depth - 8, 0);
  av1_zero(fb_info->dir);
  av1_zero(fb_info->var);

  for (int plane = 0; plane < num_planes; plane++) {
    const int stride = luma_stride >> xd->plane[plane].subsampling_x;
    const int mi_high_l2 = MI_SIZE_LOG2 - xd->plane[plane].subsampling_y;
    const int top_offset = MI_SIZE_64X64 * (fbr + 1) << mi_high_l2;
    const int bot_offset = MI_SIZE_64X64 * (fbr + 1) << mi_high_l2;
    uint16_t *top_linebuf = &linebuf[plane][0];
    uint16_t *bot_linebuf = &linebuf[plane][nvfb * CDEF_VBORDER * stride];

    if (fbr != nvfb - 1)  // if (fbr != 0)  // top line buffer copy
      av1_cdef_copy_sb8_16(cm, &top_linebuf[(fbr + 1) * CDEF_VBORDER * stride],
                           stride, xd->plane[plane].dst.buf,
                           top_offset - CDEF_VBORDER, 0,
                           xd->plane[plane].dst.stride, CDEF_VBORDER, stride);
    if (fbr != nvfb - 1)  // bottom line buffer copy
      av1_cdef_copy_sb8_16(cm, &bot_linebuf[fbr * CDEF_VBORDER * stride],
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

void av1_cdef_frame_mt(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                       AV1CdefWorkerData *const cdef_worker,
                       AVxWorker *const workers, AV1CdefSync *const cdef_sync,
                       int num_workers,
                       cdef_init_fb_row_t cdef_init_fb_row_fn) {
  YV12_BUFFER_CONFIG *frame = &cm->cur_frame->buf;
  const int num_planes = av1_num_planes(cm);

  av1_setup_dst_planes(xd->plane, frame, 0, 0, 0, num_planes, NULL);

  reset_cdef_job_info(cdef_sync);
  prepare_cdef_frame_workers(cm, xd, cdef_worker, cdef_sb_row_worker_hook,
                             workers, cdef_sync, num_workers,
                             cdef_init_fb_row_fn);
  launch_cdef_workers(workers, num_workers);
  sync_cdef_workers(workers, cm, num_workers);
}
