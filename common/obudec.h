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
#ifndef AOM_COMMON_OBUDEC_H_
#define AOM_COMMON_OBUDEC_H_

#include "config/aom_config.h"

#if CONFIG_NEW_OBU_HEADER
#include "av1/common/obu_util.h"
#endif  // CONFIG_NEW_OBU_HEADER
#include "common/tools_common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ObuDecInputContext {
  struct AvxInputContext *avx_ctx;
  uint8_t *buffer;
  size_t buffer_capacity;
  size_t bytes_buffered;
  int is_annexb;
#if CONFIG_NEW_OBU_HEADER
  int has_temporal_delimiter;
#endif  // CONFIG_NEW_OBU_HEADER
};

// Returns 1 when file data starts (if Annex B stream, after reading the
// size of the OBU) with what appears to be a Temporal Delimiter
// OBU as defined by Section 5 of the AV1 bitstream specification.
int file_is_obu(struct ObuDecInputContext *obu_ctx);

// Reads one Temporal Unit from the input file. Returns 0 when a TU is
// successfully read, 1 when end of file is reached, and less than 0 when an
// error occurs. Stores TU data in 'buffer'. Reallocs buffer to match TU size,
// returns buffer capacity via 'buffer_size', and returns size of buffered data
// via 'bytes_read'.
#if CONFIG_NEW_OBU_HEADER
int obudec_read_temporal_unit(struct ObuDecInputContext *obu_ctx,
                              uint8_t **buffer, size_t *bytes_read,
                              size_t *buffer_size, ObuHeader *obu_header_list,
                              int *obu_idx);
#else
int obudec_read_temporal_unit(struct ObuDecInputContext *obu_ctx,
                              uint8_t **buffer, size_t *bytes_read,
                              size_t *buffer_size);
#endif  // CONFIG_NEW_OBU_HEADER

void obudec_free(struct ObuDecInputContext *obu_ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // AOM_COMMON_OBUDEC_H_
