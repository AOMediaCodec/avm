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
#ifndef AVM_COMMON_OBUDEC_H_
#define AVM_COMMON_OBUDEC_H_

#include "common/tools_common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ObuDecInputContext {
  struct AvxInputContext *avx_ctx;
  uint8_t *buffer;
  size_t buffer_capacity;
  size_t bytes_buffered;
};

// Returns 1 when file data starts (if Annex B stream, after reading the
// size of the OBU) with what appears to be a Temporal Delimiter
// OBU as defined by Section 5 of the AV2 bitstream specification.
int file_is_obu(struct ObuDecInputContext *obu_ctx);

// Reads one frame unit from the input file. Returns 0 when a frame unit is
// successfully read, 1 when end of file is reached, and less than 0 when an
// error occurs. Stores frame unit data in 'buffer'. Reallocs buffer to match
// frame unit size, returns buffer capacity via 'buffer_size', and returns size
// of buffered data via 'bytes_read'. this buffer includes non video coded layer
// units such as parameter sets, metadata, QM and FGM obus.
int obudec_read_frame_unit(struct ObuDecInputContext *obu_ctx, uint8_t **buffer,
                           size_t *bytes_read, size_t *buffer_size);
void obudec_free(struct ObuDecInputContext *obu_ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // AVM_COMMON_OBUDEC_H_
