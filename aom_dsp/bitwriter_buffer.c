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

#include "aom_dsp/bitwriter_buffer.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include "config/aom_config.h"

#include "aom_dsp/recenter.h"
#include "aom_ports/bitops.h"

int aom_wb_is_byte_aligned(const struct aom_write_bit_buffer *wb) {
  return (wb->bit_offset % CHAR_BIT == 0);
}

uint32_t aom_wb_bytes_written(const struct aom_write_bit_buffer *wb) {
  return wb->bit_offset / CHAR_BIT + (wb->bit_offset % CHAR_BIT > 0);
}

void aom_wb_write_bit(struct aom_write_bit_buffer *wb, int bit) {
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
}

void aom_wb_overwrite_bit(struct aom_write_bit_buffer *wb, int bit) {
  // Do not zero bytes but overwrite exisiting values
  const int off = (int)wb->bit_offset;
  const int p = off / CHAR_BIT;
  const int q = CHAR_BIT - 1 - off % CHAR_BIT;
  wb->bit_buffer[p] &= ~(1 << q);
  wb->bit_buffer[p] |= bit << q;
  wb->bit_offset = off + 1;
}

void aom_wb_write_literal(struct aom_write_bit_buffer *wb, int data, int bits) {
  assert(bits <= 31);
  int bit;
  for (bit = bits - 1; bit >= 0; bit--) aom_wb_write_bit(wb, (data >> bit) & 1);
}

void aom_wb_write_unsigned_literal(struct aom_write_bit_buffer *wb,
                                   uint32_t data, int bits) {
  assert(bits <= 32);
  int bit;
  for (bit = bits - 1; bit >= 0; bit--) aom_wb_write_bit(wb, (data >> bit) & 1);
}

void aom_wb_overwrite_literal(struct aom_write_bit_buffer *wb, int data,
                              int bits) {
  int bit;
  for (bit = bits - 1; bit >= 0; bit--)
    aom_wb_overwrite_bit(wb, (data >> bit) & 1);
}

void aom_wb_write_inv_signed_literal(struct aom_write_bit_buffer *wb, int data,
                                     int bits) {
  aom_wb_write_literal(wb, data, bits + 1);
}

void aom_wb_write_uvlc(struct aom_write_bit_buffer *wb, uint32_t v) {
  assert(v != UINT32_MAX);
  ++v;
  const int leading_zeroes = get_msb(v);
  aom_wb_write_literal(wb, 0, leading_zeroes);
  aom_wb_write_unsigned_literal(wb, v, leading_zeroes + 1);
}

void aom_wb_write_svlc(struct aom_write_bit_buffer *wb, int32_t v) {
  assert(v != INT32_MIN);
  if (v <= 0) {
    const uint32_t abs_val = (uint32_t)(-v);
    aom_wb_write_uvlc(wb, 2 * abs_val);
  } else {
    const uint32_t abs_val = (uint32_t)v;
    aom_wb_write_uvlc(wb, 2 * abs_val - 1);
  }
}

void aom_wb_write_primitive_quniform(struct aom_write_bit_buffer *wb,
                                     uint16_t n, uint16_t v) {
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
    aom_wb_write_literal(wb, v, l - 1);
  } else {
    aom_wb_write_literal(wb, m + ((v - m) >> 1), l - 1);
    aom_wb_write_bit(wb, (v - m) & 1);
  }
}

void aom_wb_write_primitive_ref_quniform(struct aom_write_bit_buffer *wb,
                                         uint16_t n, uint16_t r, uint16_t v) {
  // Signal a single bit to indicate if the value is unequal to the reference r
  // If unequal signal a (n - 1)-ary quasi-uniform code where v values
  // higher than r are reduced by 1.
  const int unequal = (v != r);
  aom_wb_write_bit(wb, unequal);
  if (unequal) {
    v -= (v > r);
    aom_wb_write_primitive_quniform(wb, n - 1, v);
  }
}

#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
static int wb_count_primitive_quniform(uint16_t n, uint16_t v) {
  int bits = 0;
  if (n <= 1) return 0;
  assert(v < n);
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  if (v < m) {
    bits += l - 1;
  } else {
    bits += l;
  }
  return bits;
}
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER

static void wb_write_primitive_subexpfin(struct aom_write_bit_buffer *wb,
                                         uint16_t n, uint16_t k, uint16_t v) {
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (n <= mk + 3 * a) {
      aom_wb_write_primitive_quniform(wb, n - mk, v - mk);
      break;
    } else {
      int t = (v >= mk + a);
      aom_wb_write_bit(wb, t);
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        aom_wb_write_literal(wb, v - mk, b);
        break;
      }
    }
  }
}

#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
static int wb_count_primitive_subexpfin(uint16_t n, uint16_t k, uint16_t v) {
  int bits = 0;
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (n <= mk + 3 * a) {
      bits += wb_count_primitive_quniform(n - mk, v - mk);
      break;
    } else {
      int t = (v >= mk + a);
      bits++;
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        bits += b;
        break;
      }
    }
  }
  return bits;
}
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER

void aom_wb_write_primitive_refsubexpfin(struct aom_write_bit_buffer *wb,
                                         uint16_t n, uint16_t k, uint16_t ref,
                                         uint16_t v) {
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
  aom_wb_write_primitive_refsubexpfin(wb, scaled_n, k, ref + offset,
                                      v + offset);
}

#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
int aom_wb_count_primitive_refsubexpfin(uint16_t n, uint16_t k, int16_t ref,
                                        int16_t v) {
  assert(ref < n);
  assert(v < n);
  return wb_count_primitive_subexpfin(n, k, recenter_finite_nonneg(n, ref, v));
}
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER

#if CONFIG_MULTILAYER_HLS
// implementation of leb128() signaling in the specification using
// aom_write_bit_buffer
void aom_wb_write_uleb(struct aom_write_bit_buffer *wb, uint64_t value) {
  uint64_t enc_val = value;
  const size_t leb_size = aom_uleb_size_in_bytes(enc_val);
  for (size_t i = 0; i < leb_size; ++i) {
    uint8_t encoded_byte = enc_val & 0x7f;
    enc_val >>= 7;
    if (enc_val != 0) encoded_byte |= 0x80;  // Signal that more bytes follow.
    aom_wb_write_literal(wb, encoded_byte, 8);
  }
}
#endif  // CONFIG_MULTILAYER_HLS
