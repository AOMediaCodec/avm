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

#ifndef AVM_AV2_DECODER_DTHREAD_H_
#define AVM_AV2_DECODER_DTHREAD_H_

#include "config/avm_config.h"

#include "avm_util/avm_thread.h"
#include "avm/internal/avm_codec_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV2Common;
struct AV2Decoder;
struct ThreadData;

typedef struct DecWorkerData {
  struct ThreadData *td;
  const uint8_t *data_end;
  struct avm_internal_error_info error_info;
} DecWorkerData;

// WorkerData for the FrameWorker thread. It contains all the information of
// the worker and decode structures for decoding a frame.
typedef struct FrameWorkerData {
  struct AV2Decoder *pbi;
  const uint8_t *data;
  const uint8_t *data_end;
  size_t data_size;
  void *user_priv;
  int received_frame;
} FrameWorkerData;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_DECODER_DTHREAD_H_
