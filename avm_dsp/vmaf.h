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

#ifndef AVM_AVM_DSP_VMAF_H_
#define AVM_AVM_DSP_VMAF_H_

#include <stdbool.h>
#include "avm_scale/yv12config.h"

#if CONFIG_USE_VMAF_RC
typedef struct VmafContext VmafContext;
typedef struct VmafModel VmafModel;
#endif

#if CONFIG_USE_VMAF_RC
void avm_init_vmaf_context_rc(VmafContext **vmaf_context, VmafModel *vmaf_model,
                              bool cal_vmaf_neg);
void avm_close_vmaf_context_rc(VmafContext *vmaf_context);

void avm_init_vmaf_model_rc(VmafModel **vmaf_model, const char *model_path);
void avm_close_vmaf_model_rc(VmafModel *vmaf_model);

void avm_calc_vmaf_at_index_rc(VmafContext *vmaf_context, VmafModel *vmaf_model,
                               const YV12_BUFFER_CONFIG *source,
                               const YV12_BUFFER_CONFIG *distorted,
                               int bit_depth, int frame_index, double *vmaf);
#else
void avm_calc_vmaf(const char *model_path, const YV12_BUFFER_CONFIG *source,
                   const YV12_BUFFER_CONFIG *distorted, int bit_depth,
                   double *vmaf);

void avm_calc_vmaf_multi_frame(
    void *user_data, const char *model_path,
    int (*read_frame)(float *ref_data, float *main_data, float *temp_data,
                      int stride_byte, void *user_data),
    int frame_width, int frame_height, int bit_depth, double *vmaf);
#endif  // CONFIG_USE_VMAF_RC

#endif  // AVM_AVM_DSP_VMAF_H_
