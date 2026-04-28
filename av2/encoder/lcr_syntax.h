/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_COMMON_LCR_SYNTAX_H_
#define AVM_AV2_COMMON_LCR_SYNTAX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/common/av2_common_int.h"
#include "avm_dsp/bitwriter_buffer.h"

int av2_write_lcr_global_info(struct LayerConfigurationRecord *lcr_params,
                              struct avm_write_bit_buffer *wb);

int av2_write_lcr_local_info(struct LayerConfigurationRecord *lcr_params,
                             struct avm_write_bit_buffer *wb);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AV2_COMMON_LCR_SYNTAX_H_
