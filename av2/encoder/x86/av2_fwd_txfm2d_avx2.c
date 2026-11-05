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

#include "config/av2_rtcd.h"

#include "av2/common/enums.h"
#include "av2/common/av2_txfm.h"
#include "av2/common/idct.h"
#include "avm_dsp/x86/transpose_sse2.h"
#include <immintrin.h>

static INLINE __m256i load_4x4_s16_avx2(const int16_t *src, int stride) {
  // Cast src pointer to handle byte-level stride arithmetic
  const uint8_t *src_bytes = (const uint8_t *)src;
  const ptrdiff_t stride_bytes = stride * 2;

  // 1. Load each 8-byte row into the lower 64 bits of a __m128i register.
  // _mm_loadl_epi64 handles potential unaligned access.
  __m128i r0 = _mm_loadl_epi64(
      (const __m128i *)(src_bytes + 0 * stride_bytes));  // Row 0
  __m128i r1 = _mm_loadl_epi64(
      (const __m128i *)(src_bytes + 1 * stride_bytes));  // Row 1
  __m128i r2 = _mm_loadl_epi64(
      (const __m128i *)(src_bytes + 2 * stride_bytes));  // Row 2
  __m128i r3 = _mm_loadl_epi64(
      (const __m128i *)(src_bytes + 3 * stride_bytes));  // Row 3
  // Example: r0 contains [Row0_Bytes7..0 | 0x00... ] (128 bits total)

  // 2. Combine pairs of rows (r0, r1) and (r2, r3) into 128-bit registers.
  // _mm_unpacklo_epi64 interleaves the low 64 bits.
  __m128i low128 =
      _mm_unpacklo_epi64(r0, r1);  // low128 = [Row1_Bytes7..0 | Row0_Bytes7..0]
  __m128i high128 = _mm_unpacklo_epi64(
      r2, r3);  // high128 = [Row3_Bytes7..0 | Row2_Bytes7..0]

  // 3. Assemble the full 256-bit register from the two 128-bit lanes.
  // _mm256_set_m128i expects arguments in (hi, lo) order.
  __m256i block256 = _mm256_set_m128i(high128, low128);
  // block256 = [Row3 | Row2 | Row1 | Row0]

  return block256;
}

static INLINE void store_4x4_s16_s32_avx2(__m256i data_s16, int32_t *dst,
                                          int stride) {
  const ptrdiff_t stride_bytes = stride * 4;
  // --- Step 1: Convert 16x s16 -> 16x s32 (results in two __m256i) ---

  // Isolate the lower 128 bits (first 8 x int16_t: elements 0..7)
  __m128i low128_s16 = _mm256_castsi256_si128(data_s16);
  // Sign-extend lower 8 int16_t -> first 8 int32_t
  __m256i s32_low = _mm256_cvtepi16_epi32(low128_s16);
  // s32_low now contains [el7, el6, el5, el4 | el3, el2, el1, el0] (32-bit
  // elements)

  // Isolate the upper 128 bits (next 8 x int16_t: elements 8..15)
  __m128i high128_s16 = _mm256_extracti128_si256(data_s16, 1);
  // Sign-extend upper 8 int16_t -> next 8 int32_t
  __m256i s32_high = _mm256_cvtepi16_epi32(high128_s16);
  // s32_high now contains [el15, el14, el13, el12 | el11, el10, el9, el8]
  // (32-bit elements)

  // --- Step 2: Store the 16x s32 from the two registers into memory ---

  // Cast destination pointer for byte-level stride arithmetic
  uint8_t *dst_bytes = (uint8_t *)dst;

  // Extract Row 0 data (elements 0..3) from the lower 128 bits of s32_low
  __m128i row0_data = _mm256_castsi256_si128(s32_low);
  // Store Row 0 (4x int32_t = 16 bytes) at the base destination address.
  // Using _mm_storeu_si128 for safety with potentially unaligned destinations.
  _mm_storeu_si128((__m128i *)(dst_bytes + 0 * stride_bytes), row0_data);

  // Extract Row 1 data (elements 4..7) from the upper 128 bits of s32_low
  __m128i row1_data = _mm256_extracti128_si256(s32_low, 1);
  // Store Row 1 (16 bytes) at the destination address + 1*stride.
  _mm_storeu_si128((__m128i *)(dst_bytes + 1 * stride_bytes), row1_data);

  // Extract Row 2 data (elements 8..11) from the lower 128 bits of s32_high
  __m128i row2_data = _mm256_castsi256_si128(s32_high);
  // Store Row 2 (16 bytes) at the destination address + 2*stride.
  _mm_storeu_si128((__m128i *)(dst_bytes + 2 * stride_bytes), row2_data);

  // Extract Row 3 data (elements 12..15) from the upper 128 bits of s32_high
  __m128i row3_data = _mm256_extracti128_si256(s32_high, 1);
  // Store Row 3 (16 bytes) at the destination address + 3*stride.
  _mm_storeu_si128((__m128i *)(dst_bytes + 3 * stride_bytes), row3_data);
}

// 4-point WHT of av2_fwht4x4_c on 4 lanes; in[] is a, b, c, d and becomes the
// outputs a, c, d, b.
static INLINE void fwht4(__m128i *in) {
  const __m128i a = _mm_add_epi16(in[0], in[1]);
  const __m128i d = _mm_sub_epi16(in[3], in[2]);
  const __m128i e = _mm_srai_epi16(_mm_sub_epi16(a, d), 1);
  const __m128i b = _mm_sub_epi16(e, in[1]);
  const __m128i c = _mm_sub_epi16(e, in[2]);
  in[0] = _mm_sub_epi16(a, c);
  in[1] = c;
  in[2] = _mm_add_epi16(d, b);
  in[3] = b;
}

void av2_lossless_fwd_idtx_avx2(const int16_t *src_diff, tran_low_t *coeff,
                                int diff_stride, TxfmParam *txfm_param) {
  const int txw = tx_size_wide[txfm_param->tx_size];
  const int txh = tx_size_high[txfm_param->tx_size];
  int scale_bits = 3 - av2_get_tx_scale(txfm_param->tx_size);

  for (int i = 0; i < txh; i += MI_SIZE) {
    for (int j = 0; j < txw; j += MI_SIZE) {
      __m256i block =
          load_4x4_s16_avx2(src_diff + i * diff_stride + j, diff_stride);
      // Perform the left shift on the 16 packed int16_t elements
      // _mm256_slli_epi16 shifts zeros in from the right.
      __m256i shifted_block = _mm256_slli_epi16(block, scale_bits);
      store_4x4_s16_s32_avx2(shifted_block, coeff + i * txw + j, txw);
    }
  }
}

void av2_highbd_fwht4x4_avx2(const int16_t *input, tran_low_t *output,
                             int stride) {
  // Residuals of up to 12 bits keep every sum within 16 bits.
  __m128i s[4], col[4], row[4];
  for (int i = 0; i < 4; ++i) {
    s[i] = _mm_loadl_epi64((const __m128i *)(input + i * stride));
  }
  fwht4(s);
  transpose_16bit_4x4(s, col);
  fwht4(col);
  // row[0] holds rows 0-1 and row[2] rows 2-3.
  transpose_16bit_4x4(col, row);
  for (int i = 0; i < 2; ++i) {
    const __m256i v = _mm256_cvtepi16_epi32(row[2 * i]);
    _mm256_storeu_si256((__m256i *)(output + 8 * i),
                        _mm256_slli_epi32(v, UNIT_QUANT_SHIFT));
  }
}
