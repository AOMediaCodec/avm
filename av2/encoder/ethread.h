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

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ETHREAD_H_
