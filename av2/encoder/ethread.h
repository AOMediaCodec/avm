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

#ifndef AVM_AV2_ENCODER_ETHREAD_H_
#define AVM_AV2_ENCODER_ETHREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

struct AV2_COMP;
struct ThreadData;

typedef struct EncWorkerData {
  struct AV2_COMP *cpi;
  struct ThreadData *td;
  struct avm_internal_error_info error_info;
  int start;
  int thread_id;
} EncWorkerData;

void av2_row_mt_sync_read(AV2EncRowMultiThreadSync *row_mt_sync, int r, int c);
void av2_row_mt_sync_write(AV2EncRowMultiThreadSync *row_mt_sync, int r, int c,
                           int cols);

void av2_row_mt_sync_read_dummy(AV2EncRowMultiThreadSync *row_mt_sync, int r,
                                int c);
void av2_row_mt_sync_write_dummy(AV2EncRowMultiThreadSync *row_mt_sync, int r,
                                 int c, int cols);

void av2_encode_tiles_mt(struct AV2_COMP *cpi);
void av2_encode_tiles_row_mt(struct AV2_COMP *cpi);

void av2_fp_encode_tiles_row_mt(AV2_COMP *cpi);

int av2_fp_compute_num_enc_workers(AV2_COMP *cpi);

void av2_accumulate_frame_counts(struct FRAME_COUNTS *acc_counts,
                                 const struct FRAME_COUNTS *counts);

void av2_row_mt_mem_dealloc(AV2_COMP *cpi);

void av2_global_motion_estimation_mt(AV2_COMP *cpi);

void av2_gm_dealloc(AV2GlobalMotionSync *gm_sync_data);

void av2_tpl_row_mt_sync_read_dummy(AV2TplRowMultiThreadSync *tpl_mt_sync,
                                    int r, int c);
void av2_tpl_row_mt_sync_write_dummy(AV2TplRowMultiThreadSync *tpl_mt_sync,
                                     int r, int c, int cols);

void av2_tpl_row_mt_sync_read(AV2TplRowMultiThreadSync *tpl_mt_sync, int r,
                              int c);
void av2_tpl_row_mt_sync_write(AV2TplRowMultiThreadSync *tpl_mt_sync, int r,
                               int c, int cols);

void av2_mc_flow_dispenser_mt(AV2_COMP *cpi);

void av2_tpl_dealloc(AV2TplRowMultiThreadSync *tpl_sync);

int av2_compute_num_enc_workers(AV2_COMP *cpi, int max_workers);

void av2_create_workers(AV2_COMP *cpi, int num_workers);

// Runs hook(data1, data2 + i * data2_size) on workers[0..num_workers) and waits
// for all of them; returns 0 if any call returned 0. workers[0] runs on the
// calling thread, the others on the threads av2_create_workers() started, so
// num_workers must not exceed mt_info.num_workers. hook must not call
// avm_internal_error(), as it runs on other threads.
int av2_run_workers(AVxWorker *workers, int num_workers, AVxWorkerHook hook,
                    void *data1, void *data2, size_t data2_size);

// Build of the TIP frame, as av2_setup_tip_frame() with the encoder's subpel
// parameters, on the worker threads of the tile encoding when there are
// several. The result is the same for any number of threads.
void av2_enc_build_tip_frame(AV2_COMP *cpi, int copy_refined_mvs);

// Loop filter of the TIP frame, as loop_filter_tip_frame(), on the worker
// threads when there are several. The result is the same for any number of
// threads.
void av2_enc_loop_filter_tip_frame(AV2_COMP *cpi, int plane_start,
                                   int plane_end);

// SSE of rec against src over the luma plane, and over the chroma planes if
// include_chroma, as the sum of avm_highbd_get_y_sse() and avm_highbd_sse() of
// each chroma plane, on the worker threads when there are several. The result
// is the same for any number of threads.
int64_t av2_enc_tip_sse(AV2_COMP *cpi, const YV12_BUFFER_CONFIG *src,
                        const YV12_BUFFER_CONFIG *rec, int include_chroma);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ETHREAD_H_
