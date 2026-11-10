/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
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
#include <immintrin.h>

#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/bitops.h"
#include "avm_ports/mem.h"
#include "config/avm_dsp_rtcd.h"

// diff = src - pred for one row of w pixels.
static AVM_FORCE_INLINE void subtract_row(int16_t *diff, const uint16_t *src,
                                          const uint16_t *pred, int w) {
  if (w == 4) {
    const __m128i s = _mm_loadl_epi64((const __m128i *)src);
    const __m128i p = _mm_loadl_epi64((const __m128i *)pred);
    _mm_storel_epi64((__m128i *)diff, _mm_sub_epi16(s, p));
  } else if (w == 8) {
    const __m128i s = _mm_loadu_si128((const __m128i *)src);
    const __m128i p = _mm_loadu_si128((const __m128i *)pred);
    _mm_storeu_si128((__m128i *)diff, _mm_sub_epi16(s, p));
  } else {
    for (int c = 0; c < w; c += 16) {
      const __m256i s = _mm256_loadu_si256((const __m256i *)(src + c));
      const __m256i p = _mm256_loadu_si256((const __m256i *)(pred + c));
      _mm256_storeu_si256((__m256i *)(diff + c), _mm256_sub_epi16(s, p));
    }
  }
}

// Four rows per iteration: rows is a multiple of 4.
static AVM_FORCE_INLINE void subtract_block(
    int rows, int w, int16_t *diff, ptrdiff_t diff_stride, const uint16_t *src,
    ptrdiff_t src_stride, const uint16_t *pred, ptrdiff_t pred_stride) {
  do {
    for (int r = 0; r < 4; ++r) {
      subtract_row(diff + r * diff_stride, src + r * src_stride,
                   pred + r * pred_stride, w);
    }
    src += 4 * src_stride;
    pred += 4 * pred_stride;
    diff += 4 * diff_stride;
    rows -= 4;
  } while (rows > 0);
}

// One row per iteration, for the constant row counts that unroll fully. It
// needs fewer registers than subtract_block() unrolled.
static AVM_FORCE_INLINE void subtract_rows(
    int rows, int w, int16_t *diff, ptrdiff_t diff_stride, const uint16_t *src,
    ptrdiff_t src_stride, const uint16_t *pred, ptrdiff_t pred_stride) {
  for (int r = 0; r < rows; ++r) {
    subtract_row(diff, src, pred, w);
    src += src_stride;
    pred += pred_stride;
    diff += diff_stride;
  }
}

// Widths 4 and 8: the common row counts are constants so that the loops unroll.
static AVM_FORCE_INLINE void subtract_narrow_block(
    int rows, int w, int16_t *diff, ptrdiff_t diff_stride, const uint16_t *src,
    ptrdiff_t src_stride, const uint16_t *pred, ptrdiff_t pred_stride) {
  if (rows == 4) {
    subtract_block(4, w, diff, diff_stride, src, src_stride, pred, pred_stride);
  } else if (rows == 8) {
    subtract_rows(8, w, diff, diff_stride, src, src_stride, pred, pred_stride);
  } else if (rows == 16) {
    subtract_rows(16, w, diff, diff_stride, src, src_stride, pred, pred_stride);
  } else {
    subtract_block(rows, w, diff, diff_stride, src, src_stride, pred,
                   pred_stride);
  }
}

typedef void (*SubtractFunc)(int rows, int cols, int16_t *diff,
                             ptrdiff_t diff_stride, const uint16_t *src,
                             ptrdiff_t src_stride, const uint16_t *pred,
                             ptrdiff_t pred_stride, int bd);

// One function per width from 16, with the signature of
// avm_highbd_subtract_block() so that the dispatch is a tail call.
#define SUBTRACT_W(w)                                                   \
  static void subtract_w##w(int rows, int cols, int16_t *diff,          \
                            ptrdiff_t diff_stride, const uint16_t *src, \
                            ptrdiff_t src_stride, const uint16_t *pred, \
                            ptrdiff_t pred_stride, int bd) {            \
    (void)cols;                                                         \
    (void)bd;                                                           \
    subtract_block(rows, w, diff, diff_stride, src, src_stride, pred,   \
                   pred_stride);                                        \
  }

SUBTRACT_W(16)
SUBTRACT_W(32)
SUBTRACT_W(64)
SUBTRACT_W(128)
SUBTRACT_W(256)

// cols is a power of two from 4 to 256 and rows is a multiple of 4.
void avm_highbd_subtract_block_avx2(int rows, int cols, int16_t *diff,
                                    ptrdiff_t diff_stride, const uint16_t *src,
                                    ptrdiff_t src_stride, const uint16_t *pred,
                                    ptrdiff_t pred_stride, int bd) {
  // Indexed by log2(cols) - 4.
  static const SubtractFunc kSubtract[5] = { subtract_w16, subtract_w32,
                                             subtract_w64, subtract_w128,
                                             subtract_w256 };
  assert(rows >= 4 && rows % 4 == 0);
  assert(cols >= 4 && cols <= 256 && (cols & (cols - 1)) == 0);
  // Widths 4 and 8 (most of the calls, 4 the most) without the table lookup.
  if (cols <= 8) {
    if (LIKELY(cols == 4)) {
      subtract_narrow_block(rows, 4, diff, diff_stride, src, src_stride, pred,
                            pred_stride);
    } else {
      subtract_narrow_block(rows, 8, diff, diff_stride, src, src_stride, pred,
                            pred_stride);
    }
    return;
  }
  kSubtract[get_msb(cols) - 4](rows, cols, diff, diff_stride, src, src_stride,
                               pred, pred_stride, bd);
}
