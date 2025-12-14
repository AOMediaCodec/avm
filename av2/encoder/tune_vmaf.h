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

#ifndef AVM_AV2_ENCODER_TUNE_VMAF_H_
#define AVM_AV2_ENCODER_TUNE_VMAF_H_

#include "avm_dsp/vmaf.h"
#include "avm_scale/yv12config.h"
#include "av2/common/enums.h"
#include "av2/encoder/ratectrl.h"
#include "av2/encoder/block.h"

typedef struct {
  // Stores the scaling factors for rdmult when tuning for VMAF.
  // rdmult_scaling_factors[row * num_cols + col] stores the scaling factors for
  // 64x64 block at (row, col).
  double *rdmult_scaling_factors;

  // Stores the luma sse of the last frame.
  double last_frame_ysse[MAX_ARF_LAYERS];

  // Stores the VMAF of the last frame.
  double last_frame_vmaf[MAX_ARF_LAYERS];

  // Stores the filter strength of the last frame.
  double last_frame_unsharp_amount[MAX_ARF_LAYERS];

  // Stores the base unsharp amount in video pre-processing.
  double best_unsharp_amount[MAX_ARF_LAYERS];

  // Stores the origial qindex before scaling.
  int original_qindex;

#if CONFIG_USE_VMAF_RC
  // VMAF model used in VMAF caculations.
  VmafModel *vmaf_model;
#endif
} TuneVMAFInfo;

typedef struct AV2_COMP AV2_COMP;

void av2_vmaf_blk_preprocessing(AV2_COMP *cpi, YV12_BUFFER_CONFIG *source);

void av2_vmaf_frame_preprocessing(AV2_COMP *cpi, YV12_BUFFER_CONFIG *source);

#ifdef CONFIG_USE_VMAF_RC
void av2_vmaf_neg_preprocessing(AV2_COMP *cpi, YV12_BUFFER_CONFIG *source);
#endif

void av2_set_mb_vmaf_rdmult_scaling(AV2_COMP *cpi);

void av2_set_vmaf_rdmult(const AV2_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                         int mi_row, int mi_col, int *rdmult);

int av2_get_vmaf_base_qindex(const AV2_COMP *cpi, int current_qindex);

void av2_update_vmaf_curve(AV2_COMP *cpi);

#endif  // AVM_AV2_ENCODER_TUNE_VMAF_H_
