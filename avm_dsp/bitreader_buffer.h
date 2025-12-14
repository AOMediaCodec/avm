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

#ifndef AVM_AVM_DSP_BITREADER_BUFFER_H_
#define AVM_AVM_DSP_BITREADER_BUFFER_H_

#include <limits.h>

#include "avm/avm_codec.h"
#include "avm/avm_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*avm_rb_error_handler)(void *data, avm_codec_err_t error,
                                     const char *detail);

struct avm_read_bit_buffer {
  const uint8_t *bit_buffer;
  const uint8_t *bit_buffer_end;
  uint32_t bit_offset;

  void *error_handler_data;
  avm_rb_error_handler error_handler;
};

size_t avm_rb_bytes_read(const struct avm_read_bit_buffer *rb);

int avm_rb_read_bit(struct avm_read_bit_buffer *rb);

int avm_rb_read_literal(struct avm_read_bit_buffer *rb, int bits);

uint32_t avm_rb_read_unsigned_literal(struct avm_read_bit_buffer *rb, int bits);

int avm_rb_read_inv_signed_literal(struct avm_read_bit_buffer *rb, int bits);

// Reads a variable length unsigned integer. Valid range is 0..UINT32_MAX - 1.
// Returns UINT32_MAX if the input is too long (has 32 or more leading zero
// bits); if rb->error_handler is not null, rb->error_handler() is invoked.
// If rb->error_handler() calls avm_internal_error(), the function does a long
// jump and does not return.
uint32_t avm_rb_read_uvlc(struct avm_read_bit_buffer *rb);

// Reads a variable length signed integer. Valid range is
// INT32_MIN + 1..INT32_MAX. Returns INT32_MIN if the input is too long (has 32
// or more leading zero bits); if rb->error_handler is not null,
// rb->error_handler() is invoked. If rb->error_handler() calls
// avm_internal_error(), the function does a long jump and does not return.
int32_t avm_rb_read_svlc(struct avm_read_bit_buffer *rb);

// Reads a Rice Golomb descriptor with a parameter k. k must be <= 26. Valid
// range is 0..2^(k + 5) - 1. Returns UINT32_MAX if the quotient part of the
// coding is too long (has 32 or more leading one bits); if rb->error_handler
// is not null, rb->error_handler() is invoked. If rb->error_handler() calls
// avm_internal_error(), the function does a long jump and does not return.
uint32_t avm_rb_read_rice_golomb(struct avm_read_bit_buffer *rb, int k);

uint16_t avm_rb_read_primitive_refsubexpfin(struct avm_read_bit_buffer *rb,
                                            uint16_t n, uint16_t k,
                                            uint16_t ref);
int16_t avm_rb_read_signed_primitive_refsubexpfin(
    struct avm_read_bit_buffer *rb, uint16_t n, uint16_t k, int16_t ref);

uint16_t avm_rb_read_primitive_quniform(struct avm_read_bit_buffer *rb,
                                        uint16_t n);

uint16_t avm_rb_read_primitive_ref_quniform(struct avm_read_bit_buffer *rb,
                                            uint16_t n, uint16_t r);

uint32_t avm_rb_read_uleb(struct avm_read_bit_buffer *rb);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_BITREADER_BUFFER_H_
