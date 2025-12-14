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
#ifndef AVM_COMMON_IVFDEC_H_
#define AVM_COMMON_IVFDEC_H_

#include "avm/avm_codec.h"
#include "common/tools_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int file_is_ivf(struct AvxInputContext *input);
int ivf_read_frame(FILE *infile, uint8_t **buffer, size_t *bytes_read,
                   size_t *buffer_size, avm_codec_pts_t *pts);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // AVM_COMMON_IVFDEC_H_
