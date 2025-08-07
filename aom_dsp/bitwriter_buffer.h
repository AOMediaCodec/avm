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

#ifndef AOM_AOM_DSP_BITWRITER_BUFFER_H_
#define AOM_AOM_DSP_BITWRITER_BUFFER_H_

#include "aom/aom_integer.h"
#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if ENABLE_ENCTRACE
#define ADD_ENCTRACE(s) , s
#define ADD_ENCTRACE2(d0, s0, s1) , (d0? s0"_"s1 : s1)
#define ADD_ENCTRACE_I2(s0, d1) , (d1==0?s0"[0]":d1==1?s0"[1]":s0"[2]")
#else
#define ADD_ENCTRACE(s)
#define ADD_ENCTRACE2(d0, s0, s1)
#define ADD_ENCTRACE_I2(s0, d1)
#endif
struct aom_write_bit_buffer {
  uint8_t *bit_buffer;
  uint32_t bit_offset;
};

int aom_wb_is_byte_aligned(const struct aom_write_bit_buffer *wb);

uint32_t aom_wb_bytes_written(const struct aom_write_bit_buffer *wb);
#if ENABLE_ENCTRACE
void aom_wb_write_bit(struct aom_write_bit_buffer *wb, int bit, const char* str);
void aom_wb_overwrite_bit(struct aom_write_bit_buffer *wb, int bit, const char* str);
void aom_wb_write_literal(struct aom_write_bit_buffer *wb, int data, int bits, const char* str);
void aom_wb_write_unsigned_literal(struct aom_write_bit_buffer *wb,
                                   uint32_t data, int bits, const char* str);
void aom_wb_overwrite_literal(struct aom_write_bit_buffer *wb, int data,
                              int bits, const char* str);
void aom_wb_write_inv_signed_literal(struct aom_write_bit_buffer *wb, int data,
                                     int bits, const char* str);
void aom_wb_write_uvlc(struct aom_write_bit_buffer *wb, uint32_t v, const char* str);
void aom_wb_write_svlc(struct aom_write_bit_buffer *wb, int32_t v, const char* str);
#else
void aom_wb_write_bit(struct aom_write_bit_buffer *wb, int bit);

void aom_wb_overwrite_bit(struct aom_write_bit_buffer *wb, int bit);

void aom_wb_write_literal(struct aom_write_bit_buffer *wb, int data, int bits);

void aom_wb_write_unsigned_literal(struct aom_write_bit_buffer *wb,
                                   uint32_t data, int bits);

void aom_wb_overwrite_literal(struct aom_write_bit_buffer *wb, int data,
                              int bits);

void aom_wb_write_inv_signed_literal(struct aom_write_bit_buffer *wb, int data,
                                     int bits);

void aom_wb_write_uvlc(struct aom_write_bit_buffer *wb, uint32_t v);
#endif
void aom_wb_write_signed_primitive_refsubexpfin(struct aom_write_bit_buffer *wb,
                                                uint16_t n, uint16_t k,
                                                int16_t ref, int16_t v);
#if ENABLE_ENCTRACE
void aom_wb_write_primitive_quniform(struct aom_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v, const char* str);
#else
void aom_wb_write_primitive_quniform(struct aom_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_BITWRITER_BUFFER_H_
