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
#include <stdint.h>
#include <string.h>
#include <smmintrin.h>

#include "avm/avm_integer.h"
#include "config/av2_rtcd.h"

// Byte-boundary alignment issues
#define ALIGN_SIZE 8
#define ALIGN_MASK (ALIGN_SIZE - 1)

#define CALC_CRC(op, crc, type, buf, len) \
  while ((len) >= sizeof(type)) {         \
    (crc) = op((crc), *(type *)(buf));    \
    (len) -= sizeof(type);                \
    buf += sizeof(type);                  \
  }

/**
 * Calculates 32-bit CRC for the input buffer
 * polynomial is 0x11EDC6F41
 * @return A 32-bit unsigned integer representing the CRC
 */
uint32_t av2_get_crc32c_value_sse4_2(void *crc_calculator, uint8_t *p,
                                     size_t len) {
  (void)crc_calculator;
  const uint8_t *buf = p;
  uint32_t crc = 0xFFFFFFFF;

  // Align the input to the word boundary
  for (; (len > 0) && ((intptr_t)buf & ALIGN_MASK); len--, buf++) {
    crc = _mm_crc32_u8(crc, *buf);
  }

#ifdef __x86_64__
  uint64_t crc64 = crc;
  CALC_CRC(_mm_crc32_u64, crc64, uint64_t, buf, len);
  crc = (uint32_t)crc64;
#endif
  CALC_CRC(_mm_crc32_u32, crc, uint32_t, buf, len);
  CALC_CRC(_mm_crc32_u16, crc, uint16_t, buf, len);
  CALC_CRC(_mm_crc32_u8, crc, uint8_t, buf, len);
  return (crc ^ 0xFFFFFFFF);
}

static AVM_INLINE uint32_t crc32c_u64_sse42(uint32_t crc, uint64_t v) {
#ifdef __x86_64__
  return (uint32_t)_mm_crc32_u64(crc, v);
#else
  crc = _mm_crc32_u32(crc, (uint32_t)v);
  return _mm_crc32_u32(crc, (uint32_t)(v >> 32));
#endif
}

uint64_t av2_tx_cache_hash_sse4_2(const int16_t *residual, int stride, int tx_w,
                                  int tx_h, int qindex, int txb_skip_ctx,
                                  int dc_sign_ctx) {
  assert(stride >= tx_w);
  assert((tx_w & 3) == 0);
  uint32_t crc_lo = 0xFFFFFFFFu;
  uint32_t crc_hi = 0x9E3779B9u;
  for (int r = 0; r < tx_h; ++r) {
    const int16_t *row = residual + (size_t)r * stride;
    for (int c = 0; c < tx_w; c += 4) {
      uint64_t v;
      memcpy(&v, &row[c], sizeof(v));
      crc_lo = crc32c_u64_sse42(crc_lo, v);
      crc_hi = crc32c_u64_sse42(crc_hi, v ^ 0xA5A5A5A5A5A5A5A5ULL);
    }
  }
  const uint64_t meta0 = ((uint64_t)qindex << 32) |
                         ((uint64_t)txb_skip_ctx << 16) | (uint32_t)dc_sign_ctx;
  const uint64_t meta1 = ((uint64_t)tx_w << 32) | (uint32_t)tx_h;
  crc_lo = crc32c_u64_sse42(crc_lo, meta0);
  crc_hi = crc32c_u64_sse42(crc_hi, meta1);
  return ((uint64_t)crc_hi << 32) | crc_lo;
}
