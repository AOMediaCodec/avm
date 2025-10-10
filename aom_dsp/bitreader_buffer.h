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

#ifndef AOM_AOM_DSP_BITREADER_BUFFER_H_
#define AOM_AOM_DSP_BITREADER_BUFFER_H_

#include <limits.h>

#include "aom/aom_codec.h"
#include "aom/aom_integer.h"
#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aom_rb_error_handler)(void *data, aom_codec_err_t error,
                                     const char *detail);

struct aom_read_bit_buffer {
  const uint8_t *bit_buffer;
  const uint8_t *bit_buffer_end;
  uint32_t bit_offset;

  void *error_handler_data;
  aom_rb_error_handler error_handler;
};

size_t aom_rb_bytes_read(const struct aom_read_bit_buffer *rb);

int aom_rb_read_bit(struct aom_read_bit_buffer *rb);

int aom_rb_read_literal(struct aom_read_bit_buffer *rb, int bits);

uint32_t aom_rb_read_unsigned_literal(struct aom_read_bit_buffer *rb, int bits);

int aom_rb_read_inv_signed_literal(struct aom_read_bit_buffer *rb, int bits);

// Reads a variable length unsigned integer. Valid range is 0..UINT32_MAX - 1.
// Returns UINT32_MAX if the input is too long (has 32 or more leading zero
// bits); if rb->error_handler is not null, rb->error_handler() is invoked.
// If rb->error_handler() calls aom_internal_error(), the function does a long
// jump and does not return.
uint32_t aom_rb_read_uvlc(struct aom_read_bit_buffer *rb);

// Reads a variable length signed integer. Valid range is
// INT32_MIN + 1..INT32_MAX. Returns INT32_MIN if the input is too long (has 32
// or more leading zero bits); if rb->error_handler is not null,
// rb->error_handler() is invoked. If rb->error_handler() calls
// aom_internal_error(), the function does a long jump and does not return.
int32_t aom_rb_read_svlc(struct aom_read_bit_buffer *rb);

uint16_t aom_rb_read_primitive_refsubexpfin(struct aom_read_bit_buffer *rb,
                                            uint16_t n, uint16_t k,
                                            uint16_t ref);
int16_t aom_rb_read_signed_primitive_refsubexpfin(
    struct aom_read_bit_buffer *rb, uint16_t n, uint16_t k, int16_t ref);

uint16_t aom_rb_read_primitive_quniform(struct aom_read_bit_buffer *rb,
                                        uint16_t n);

uint16_t aom_rb_read_primitive_ref_quniform(struct aom_read_bit_buffer *rb,
                                            uint16_t n, uint16_t r);

#if CONFIG_MULTILAYER_HLS
uint32_t aom_rb_read_uleb(struct aom_read_bit_buffer *rb);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_BITREADER_BUFFER_H_
