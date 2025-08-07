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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "config/aom_config.h"

#include "aom_dsp/bitreader_buffer.h"
#include "aom_dsp/recenter.h"
#include "aom_ports/bitops.h"

size_t aom_rb_bytes_read(const struct aom_read_bit_buffer *rb) {
  return (rb->bit_offset + 7) >> 3;
}
#if ENABLE_DECTRACE
int aom_rb_read_bit(struct aom_read_bit_buffer *rb, const char* str)
#else
int aom_rb_read_bit(struct aom_read_bit_buffer *rb)
#endif
{
#if ENABLE_DECTRACE
  uint32_t startPos=rb->bit_offset;
#endif
  const uint32_t off = rb->bit_offset;
  const uint32_t p = off >> 3;
  const int q = 7 - (int)(off & 0x7);
  if (rb->bit_buffer + p < rb->bit_buffer_end) {
    const int bit = (rb->bit_buffer[p] >> q) & 1;
    rb->bit_offset = off + 1;
#if ENABLE_DECTRACE
    if(strcmp(str, ""))
      printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "bit", startPos, 1, bit);
#endif
    return bit;
  } else {
    if (rb->error_handler) rb->error_handler(rb->error_handler_data);
    return 0;
  }
}
#if ENABLE_DECTRACE
int aom_rb_read_literal(struct aom_read_bit_buffer *rb, int bits, const char* str)
#else
int aom_rb_read_literal(struct aom_read_bit_buffer *rb, int bits)
#endif
{
  assert(bits <= 31);
  int value = 0, bit;
#if ENABLE_DECTRACE
  uint32_t startPos=rb->bit_offset;
#endif
  for (bit = bits - 1; bit >= 0; bit--) value |= aom_rb_read_bit(rb ADD_DECTRACE("")) << bit;
#if ENABLE_DECTRACE
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "literal", startPos, bits, value);
#endif

  return value;
}
#if ENABLE_DECTRACE
uint32_t aom_rb_read_unsigned_literal(struct aom_read_bit_buffer *rb,
                                      int bits, const char* str)
#else
uint32_t aom_rb_read_unsigned_literal(struct aom_read_bit_buffer *rb,
                                      int bits)
#endif
{
  assert(bits <= 32);
#if ENABLE_DECTRACE
  uint32_t startPos=rb->bit_offset;
#endif

  uint32_t value = 0;
  int bit;
  for (bit = bits - 1; bit >= 0; bit--)
    value |= (uint32_t)aom_rb_read_bit(rb ADD_DECTRACE("")) << bit;
#if ENABLE_DECTRACE
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "uliteral", startPos, bits, value);
#endif

  return value;
}
#if ENABLE_DECTRACE
int aom_rb_read_inv_signed_literal(struct aom_read_bit_buffer *rb, int bits, const char* str)
#else
int aom_rb_read_inv_signed_literal(struct aom_read_bit_buffer *rb, int bits)
#endif
{
#if ENABLE_DECTRACE
  int startPos = rb->bit_offset;
#endif
  const int nbits = sizeof(unsigned) * 8 - bits - 1;
  const unsigned value = (unsigned)aom_rb_read_literal(rb, bits + 1 ADD_DECTRACE("")) << nbits;
#if ENABLE_DECTRACE
  int retVal=((int)value) >> nbits;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "isigedlit", startPos, bits, retVal);
#endif

  return ((int)value) >> nbits;
}
#if ENABLE_DECTRACE
uint32_t aom_rb_read_uvlc(struct aom_read_bit_buffer *rb, const char* str)
#else
uint32_t aom_rb_read_uvlc(struct aom_read_bit_buffer *rb)
#endif
{
#if ENABLE_DECTRACE
  int startPos = rb->bit_offset;
#endif
  int leading_zeros = 0;
  while (leading_zeros < 32 && !aom_rb_read_bit(rb ADD_DECTRACE(""))) ++leading_zeros;
  // Maximum 32 bits.
  if (leading_zeros == 32) return UINT32_MAX;
  const uint32_t base = (1u << leading_zeros) - 1;
  const uint32_t value = aom_rb_read_literal(rb, leading_zeros ADD_DECTRACE(""));
#if ENABLE_DECTRACE
  int endPos = rb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "uvlc", startPos, endPos-startPos, base + value);
#endif

  return base + value;
}
#if ENABLE_DECTRACE
int32_t aom_rb_read_svlc(struct aom_read_bit_buffer *rb, const char* str){
#if ENABLE_DECTRACE
  int startPos = rb->bit_offset;
#endif
  int rValue = 0;
  unsigned int uiBits = aom_rb_read_bit(rb, "");
  if( 0 == uiBits )
  {
    unsigned int uiLength = 0;
    while( ! ( uiBits & 1 )){
      uiBits = aom_rb_read_bit(rb, "");
      uiLength++;
    }
    uiBits = aom_rb_read_literal(rb, uiLength, "");
    uiBits += (1 << uiLength);
    rValue = ( uiBits & 1) ? -(int)(uiBits>>1) : (int)(uiBits>>1);
  }
  
#if ENABLE_DECTRACE
  int endPos = rb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "svlc", startPos, endPos-startPos, rValue);
#endif
  
  return rValue;
}

int aom_rb_read_truncted_unary(struct aom_read_bit_buffer *rb, int max_bits, const char* str){
#if ENABLE_DECTRACE
  uint32_t startPos=rb->bit_offset;
#endif
  int val=0;
  while (val < max_bits) {
    if (!aom_rb_read_bit(rb, "")) {
      break;
    }
    val++;
  }
#if ENABLE_DECTRACE
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "unary", startPos, val+2, val); //TODO: val+2 or val+1?
#endif
  return val;
}
#endif
#if ENABLE_DECTRACE
uint16_t aom_rb_read_primitive_quniform(struct aom_read_bit_buffer *rb,
                                        uint16_t n, const char* str)
#else
uint16_t aom_rb_read_primitive_quniform(struct aom_read_bit_buffer *rb,
                                        uint16_t n)
#endif
{
#if ENABLE_DECTRACE
  int startPos = rb->bit_offset;
#endif
  if (n <= 1) return 0;
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  const int v = aom_rb_read_literal(rb, l - 1 ADD_DECTRACE(""));
#if ENABLE_DECTRACE
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\n", str, "primitive_quniform", startPos);
#endif

  return v < m ? v : (v << 1) - m + aom_rb_read_bit(rb ADD_DECTRACE(""));
}

static uint16_t aom_rb_read_primitive_subexpfin(struct aom_read_bit_buffer *rb,
                                                uint16_t n, uint16_t k) {
  int i = 0;
  int mk = 0;

  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);

    if (n <= mk + 3 * a) {
      return aom_rb_read_primitive_quniform(rb, n - mk ADD_DECTRACE("")) + mk;
    }

    if (!aom_rb_read_bit(rb ADD_DECTRACE(""))) {
      return aom_rb_read_literal(rb, b ADD_DECTRACE("")) + mk;
    }

    i = i + 1;
    mk += a;
  }

  assert(0);
  return 0;
}

static uint16_t aom_rb_read_primitive_refsubexpfin(
    struct aom_read_bit_buffer *rb, uint16_t n, uint16_t k, uint16_t ref) {
  assert(ref < n);
  return inv_recenter_finite_nonneg(n, ref,
                                    aom_rb_read_primitive_subexpfin(rb, n, k));
}

int16_t aom_rb_read_signed_primitive_refsubexpfin(
    struct aom_read_bit_buffer *rb, uint16_t n, uint16_t k, int16_t ref) {
  assert(n > 0);
  const uint16_t offset = n - 1;
  const uint16_t scaled_n = (n << 1) - 1;
  return aom_rb_read_primitive_refsubexpfin(rb, scaled_n, k, ref + offset) -
         offset;
}
