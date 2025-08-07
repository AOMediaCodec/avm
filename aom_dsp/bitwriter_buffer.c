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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "config/aom_config.h"

#include "aom_dsp/bitwriter_buffer.h"
#include "aom_dsp/recenter.h"
#include "aom_ports/bitops.h"

int aom_wb_is_byte_aligned(const struct aom_write_bit_buffer *wb) {
  return (wb->bit_offset % CHAR_BIT == 0);
}

uint32_t aom_wb_bytes_written(const struct aom_write_bit_buffer *wb) {
  return wb->bit_offset / CHAR_BIT + (wb->bit_offset % CHAR_BIT > 0);
}
#if ENABLE_ENCTRACE
void aom_wb_write_bit(struct aom_write_bit_buffer *wb, int bit, const char* str)
#else
void aom_wb_write_bit(struct aom_write_bit_buffer *wb, int bit)
#endif
{
  const int off = (int)wb->bit_offset;
  const int p = off / CHAR_BIT;
  const int q = CHAR_BIT - 1 - off % CHAR_BIT;
  if (q == CHAR_BIT - 1) {
    // Zero next char and write bit
    wb->bit_buffer[p] = bit << q;
  } else {
    wb->bit_buffer[p] &= ~(1 << q);
    wb->bit_buffer[p] |= bit << q;
  }
  wb->bit_offset = off + 1;
#if ENABLE_ENCTRACE
    if(strcmp(str, ""))
      printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "bit", off, 1, bit);
#endif
}
#if ENABLE_ENCTRACE
void aom_wb_overwrite_bit(struct aom_write_bit_buffer *wb, int bit, const char* str)
#else
void aom_wb_overwrite_bit(struct aom_write_bit_buffer *wb, int bit)
#endif
{
  // Do not zero bytes but overwrite exisiting values
  const int off = (int)wb->bit_offset;
  const int p = off / CHAR_BIT;
  const int q = CHAR_BIT - 1 - off % CHAR_BIT;
  wb->bit_buffer[p] &= ~(1 << q);
  wb->bit_buffer[p] |= bit << q;
  wb->bit_offset = off + 1;
#if ENABLE_ENCTRACE
    if(strcmp(str, ""))
      printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "overwritebit", off, 1, bit);
#endif

}
#if ENABLE_ENCTRACE
void aom_wb_write_literal(struct aom_write_bit_buffer *wb, int data, int bits, const char* str)
#else
void aom_wb_write_literal(struct aom_write_bit_buffer *wb, int data, int bits)
#endif
{
  assert(bits <= 31);
#if ENABLE_ENCTRACE
  int startPos = wb->bit_offset;
#endif
  int bit;
  for (bit = bits - 1; bit >= 0; bit--) aom_wb_write_bit(wb, (data >> bit) & 1 ADD_ENCTRACE(""));
#if ENABLE_ENCTRACE
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "literal", startPos, bits, data);
#endif

}
#if ENABLE_ENCTRACE
void aom_wb_write_unsigned_literal(struct aom_write_bit_buffer *wb,
                                   uint32_t data, int bits, const char* str)
#else
void aom_wb_write_unsigned_literal(struct aom_write_bit_buffer *wb,
                                   uint32_t data, int bits)
#endif
{
  assert(bits <= 32);
#if ENABLE_ENCTRACE
  const int off = (int)wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "uliteral", off, bits, data);
#endif
  int bit;
  for (bit = bits - 1; bit >= 0; bit--) aom_wb_write_bit(wb, (data >> bit) & 1 ADD_ENCTRACE(""));
}
#if ENABLE_ENCTRACE
void aom_wb_overwrite_literal(struct aom_write_bit_buffer *wb, int data,
                              int bits, const char* str)
#else
void aom_wb_overwrite_literal(struct aom_write_bit_buffer *wb, int data,
                              int bits)
#endif
{
#if ENABLE_ENCTRACE
  const int off = (int)wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "oliteral", off, bits, data);
#endif
  int bit;
  for (bit = bits - 1; bit >= 0; bit--)
    aom_wb_overwrite_bit(wb, (data >> bit) & 1 ADD_ENCTRACE(""));
}
#if ENABLE_ENCTRACE
void aom_wb_write_inv_signed_literal(struct aom_write_bit_buffer *wb, int data,
                                     int bits, const char* str)
#else
void aom_wb_write_inv_signed_literal(struct aom_write_bit_buffer *wb, int data,
                                     int bits)
#endif
{
#if ENABLE_ENCTRACE
  const int off = (int)wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "isigedlit", off, bits, data);
#endif
  aom_wb_write_literal(wb, data, bits + 1 ADD_ENCTRACE(""));
}
#if ENABLE_ENCTRACE
void aom_wb_write_uvlc(struct aom_write_bit_buffer *wb, uint32_t v, const char* str)
#else
void aom_wb_write_uvlc(struct aom_write_bit_buffer *wb, uint32_t v)
#endif
{
#if ENABLE_ENCTRACE
  uint32_t input_val = v;
#endif
  int64_t shift_val = ++v;
  int leading_zeroes = 1;

  assert(shift_val > 0);

  while (shift_val >>= 1) leading_zeroes += 2;
#if ENABLE_ENCTRACE
  int startPos = wb->bit_offset;
#endif
  aom_wb_write_literal(wb, 0, leading_zeroes >> 1 ADD_ENCTRACE(""));
  aom_wb_write_unsigned_literal(wb, v, (leading_zeroes + 1) >> 1 ADD_ENCTRACE(""));
#if ENABLE_ENCTRACE
  int endPos = wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "uvlc", startPos,
           endPos-startPos, input_val);
#endif
}
#if ENABLE_ENCTRACE
void aom_wb_write_svlc(struct aom_write_bit_buffer *wb, int32_t v, const char* str){
  unsigned int uiCode = ( v <= 0) ? -v<<1 : (v<<1)-1;
#if ENABLE_ENCTRACE
  int startPos = wb->bit_offset;
#endif
  aom_wb_write_uvlc( wb, uiCode ADD_ENCTRACE("") );
#if ENABLE_ENCTRACE
  int endPos = wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\t%2d\t%d\n", str, "svlc", startPos,
           endPos-startPos, v);
#endif

}
#endif

#if ENABLE_ENCTRACE
void aom_wb_write_primitive_quniform(struct aom_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v, const char* str)
#else
void aom_wb_write_primitive_quniform(struct aom_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v)
#endif
{
#if ENABLE_ENCTRACE
  int startPos = wb->bit_offset;
  if(strcmp(str,""))
    printf("%-32s\t%-10s:[%8d]\n", str, "primitive_quniform", startPos);
#endif
  if (n <= 1) return;
  assert(v < n);
  // Split the valid range into two.
  // The encoded value is in the range [0, n), but in order to map a range
  // which may not be a power of 2 onto a binary code, we split into the
  // sub-ranges [0, m) and [m, n), where m is an intermediate point.
  // Values in the range [0, m) then use one fewer bit than values in
  // the range [m, n).
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  if (v < m) {
    aom_wb_write_literal(wb, v, l - 1 ADD_ENCTRACE(""));
  } else {
    aom_wb_write_literal(wb, m + ((v - m) >> 1), l - 1 ADD_ENCTRACE(""));
    aom_wb_write_bit(wb, (v - m) & 1 ADD_ENCTRACE(""));
  }
}

static void wb_write_primitive_subexpfin(struct aom_write_bit_buffer *wb,
                                         uint16_t n, uint16_t k, uint16_t v) {
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (n <= mk + 3 * a) {
      aom_wb_write_primitive_quniform(wb, n - mk, v - mk ADD_ENCTRACE(""));
      break;
    } else {
      int t = (v >= mk + a);
      aom_wb_write_bit(wb, t ADD_ENCTRACE(""));
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        aom_wb_write_literal(wb, v - mk, b ADD_ENCTRACE(""));
        break;
      }
    }
  }
}

static void wb_write_primitive_refsubexpfin(struct aom_write_bit_buffer *wb,
                                            uint16_t n, uint16_t k,
                                            uint16_t ref, uint16_t v) {
  assert(ref < n);
  assert(v < n);
  wb_write_primitive_subexpfin(wb, n, k, recenter_finite_nonneg(n, ref, v));
}

void aom_wb_write_signed_primitive_refsubexpfin(struct aom_write_bit_buffer *wb,
                                                uint16_t n, uint16_t k,
                                                int16_t ref, int16_t v) {
  assert(n > 0);
  const uint16_t offset = n - 1;
  const uint16_t scaled_n = (n << 1) - 1;
  wb_write_primitive_refsubexpfin(wb, scaled_n, k, ref + offset, v + offset);
}
