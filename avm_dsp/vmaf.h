/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at avmedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * avmedia.org/license/patent-license/.
 */

#ifndef AVM_AVM_DSP_VMAF_H_
#define AVM_AVM_DSP_VMAF_H_

#include <stdbool.h>
#include "avm_scale/yv12config.h"

typedef struct VmafContext VmafContext;
typedef struct VmafModel VmafModel;

void avm_init_vmaf_context(VmafContext **vmaf_context, VmafModel *vmaf_model,
                           bool cal_vmaf_neg);
void avm_close_vmaf_context(VmafContext *vmaf_context);

void avm_init_vmaf_model(VmafModel **vmaf_model, const char *model_path);
void avm_close_vmaf_model(VmafModel *vmaf_model);

void avm_calc_vmaf(VmafModel *vmaf_model, const YV12_BUFFER_CONFIG *source,
                   const YV12_BUFFER_CONFIG *distorted, int bit_depth,
                   bool cal_vmaf_neg, double *vmaf);

void avm_read_vmaf_image(VmafContext *vmaf_context,
                         const YV12_BUFFER_CONFIG *source,
                         const YV12_BUFFER_CONFIG *distorted, int bit_depth,
                         int frame_index);

double avm_calc_vmaf_at_index(VmafContext *vmaf_context, VmafModel *vmaf_model,
                              int frame_index);

void avm_flush_vmaf_context(VmafContext *vmaf_context);

#endif  // AVM_AVM_DSP_VMAF_H_
