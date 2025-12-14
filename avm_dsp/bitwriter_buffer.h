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

#ifndef AVM_AVM_DSP_BITWRITER_BUFFER_H_
#define AVM_AVM_DSP_BITWRITER_BUFFER_H_

#include "avm/avm_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct avm_write_bit_buffer {
  uint8_t *bit_buffer;
  uint32_t bit_offset;
};

int avm_wb_is_byte_aligned(const struct avm_write_bit_buffer *wb);

uint32_t avm_wb_bytes_written(const struct avm_write_bit_buffer *wb);

void avm_wb_write_bit(struct avm_write_bit_buffer *wb, int bit);

void avm_wb_overwrite_bit(struct avm_write_bit_buffer *wb, int bit);

void avm_wb_write_literal(struct avm_write_bit_buffer *wb, int data, int bits);

void avm_wb_write_unsigned_literal(struct avm_write_bit_buffer *wb,
                                   uint32_t data, int bits);

void avm_wb_overwrite_literal(struct avm_write_bit_buffer *wb, int data,
                              int bits);

void avm_wb_write_inv_signed_literal(struct avm_write_bit_buffer *wb, int data,
                                     int bits);

// Writes a variable length unsigned integer. UINT32_MAX is an invalid input.
void avm_wb_write_uvlc(struct avm_write_bit_buffer *wb, uint32_t v);

// Writes a variable length signed integer. INT32_MIN is an invalid input.
void avm_wb_write_svlc(struct avm_write_bit_buffer *wb, int32_t v);

void avm_wb_write_primitive_refsubexpfin(struct avm_write_bit_buffer *wb,
                                         uint16_t n, uint16_t k, uint16_t ref,
                                         uint16_t v);
void avm_wb_write_signed_primitive_refsubexpfin(struct avm_write_bit_buffer *wb,
                                                uint16_t n, uint16_t k,
                                                int16_t ref, int16_t v);
void avm_wb_write_primitive_quniform(struct avm_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v);
void avm_wb_write_primitive_ref_quniform(struct avm_write_bit_buffer *wb,
                                         uint16_t n, uint16_t r, uint16_t v);

int avm_wb_count_primitive_refsubexpfin(uint16_t n, uint16_t k, int16_t ref,
                                        int16_t v);

void avm_wb_write_uleb(struct avm_write_bit_buffer *wb, uint32_t value);

void avm_wb_write_rice_golomb(struct avm_write_bit_buffer *wb, uint32_t data,
                              int k);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_BITWRITER_BUFFER_H_
