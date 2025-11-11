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

#ifndef AOM_AV1_COMMON_ALLOCCOMMON_H_
#define AOM_AV1_COMMON_ALLOCCOMMON_H_

#define INVALID_IDX -1  // Invalid buffer index.

#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV1Common;
struct BufferPool;
struct CommonContexts;
struct CommonModeInfoParams;
struct AV1CdefWorker;
struct AV1CdefSyncData;

void av1_remove_common(struct AV1Common *cm);

int av1_alloc_above_context_buffers(struct CommonContexts *above_contexts,
                                    int num_tile_rows, int num_mi_cols,
                                    int num_planes);
void av1_free_above_context_buffers(struct CommonContexts *above_contexts);
int av1_alloc_superblock_info_buffers(struct AV1Common *cm);
int av1_alloc_context_buffers(struct AV1Common *cm, int width, int height);
void av1_init_mi_buffers(struct CommonModeInfoParams *mi_params);
void av1_free_context_buffers(struct AV1Common *cm);

void av1_free_ref_frame_buffers(struct BufferPool *pool);

// Allocates the line buffers of CDEF for all planes and reallocates the buffer
// if frame size, subsampling, flags, or plane count changed.
void av1_alloc_cdef_buffers(struct AV1Common *const cm,
                            struct AV1CdefWorker **cdef_worker,
                            struct AV1CdefSyncData *cdef_sync, int num_workers);
// Free all CDEF-related buffers, worker data, and sync objects.
void av1_free_cdef_buffers(struct AV1Common *const cm,
                           struct AV1CdefWorker **cdef_worker,
                           struct AV1CdefSyncData *cdef_sync, int num_workers);

void av1_alloc_restoration_buffers(struct AV1Common *cm);
void av1_alloc_restoration_boundary_buffers(struct AV1Common *cm,
                                            int num_planes);
void av1_free_restoration_buffers(struct AV1Common *cm);

int av1_get_MBs(int width, int height);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ALLOCCOMMON_H_
