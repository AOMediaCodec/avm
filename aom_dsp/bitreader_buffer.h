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

#include "aom/aom_integer.h"
#include "config/aom_config.h"
#ifdef __cplusplus
extern "C" {
#endif
#if ENABLE_DECTRACE
#define ADD_DECTRACE(s) , s
#define ADD_DECTRACEC(s0, s1) , (printf("%s", s0))
#define ADD_DECTRACE2(d0, s0, s1) , (d0? s0"_"s1 : s1)
#define ADD_DECTRACE_I2(s0, d1) , (d1==0?s0"[0]":d1==1?s0"[1]":s0"[2]")

#else
#define ADD_DECTRACE(s)
#define ADD_DECTRACE2(d0, s0, s1)
#define ADD_DECTRACE_I2(s0, d1)
#endif
typedef void (*aom_rb_error_handler)(void *data);

struct aom_read_bit_buffer {
  const uint8_t *bit_buffer;
  const uint8_t *bit_buffer_end;
  uint32_t bit_offset;

  void *error_handler_data;
  aom_rb_error_handler error_handler;
};

size_t aom_rb_bytes_read(const struct aom_read_bit_buffer *rb);
#if ENABLE_DECTRACE
int aom_rb_read_bit(struct aom_read_bit_buffer *rb, const char* str);

int aom_rb_read_literal(struct aom_read_bit_buffer *rb, int bits, const char* str);

uint32_t aom_rb_read_unsigned_literal(struct aom_read_bit_buffer *rb, int bits, const char* str);

int aom_rb_read_inv_signed_literal(struct aom_read_bit_buffer *rb, int bits, const char* str);

uint32_t aom_rb_read_uvlc(struct aom_read_bit_buffer *rb, const char* str);

int32_t aom_rb_read_svlc(struct aom_read_bit_buffer *rb, const char* str);

int aom_rb_read_truncted_unary(struct aom_read_bit_buffer *rb, int max_bits, const char* str);

#else
int aom_rb_read_bit(struct aom_read_bit_buffer *rb);

int aom_rb_read_literal(struct aom_read_bit_buffer *rb, int bits);

uint32_t aom_rb_read_unsigned_literal(struct aom_read_bit_buffer *rb, int bits);

int aom_rb_read_inv_signed_literal(struct aom_read_bit_buffer *rb, int bits);

uint32_t aom_rb_read_uvlc(struct aom_read_bit_buffer *rb);
#endif
int16_t aom_rb_read_signed_primitive_refsubexpfin(
    struct aom_read_bit_buffer *rb, uint16_t n, uint16_t k, int16_t ref);
#if ENABLE_DECTRACE
uint16_t aom_rb_read_primitive_quniform(struct aom_read_bit_buffer *rb,
                                        uint16_t n, const char* str);
#else
uint16_t aom_rb_read_primitive_quniform(struct aom_read_bit_buffer *rb,
                                        uint16_t n);
#endif
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_BITREADER_BUFFER_H_
