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
#include <emmintrin.h>
#include <smmintrin.h>  // For SSE4.1
#include <immintrin.h>

#include "config/av1_rtcd.h"

#include "av1/common/cfl.h"

#include "av1/common/reconinter.h"

#if CONFIG_ENABLE_MHCCP
static __m128i read_int64(int64_t x) {
#ifdef __x86_64__
  return _mm_cvtsi64_si128(x);
#else
  return _mm_set_epi32(0, 0, x >> 32, (int32_t)x);
#endif
}

void mhccp_predict_hv_hbd_sse4_1(const uint16_t *input, uint16_t *dst,
                                 bool have_top, bool have_left, int dst_stride,
                                 int64_t *alpha_q3, int bit_depth, int width,
                                 int height, int dir) {
  const uint16_t mid_s = (1 << (bit_depth - 1));
  const __m128i mid_vec = _mm_set1_epi16(mid_s);
  const int cfl_stride = CFL_BUF_LINE * 2;
  const uint16_t *above_row = input - cfl_stride;
  __m128i rounding_vec = _mm_set1_epi32(MHCCP_DECIM_ROUND);
  __m128i max_val_vec;
  switch (bit_depth) {
    case 8:
    default: max_val_vec = _mm_set1_epi16(255); break;
    case 10: max_val_vec = _mm_set1_epi16(1023); break;
    case 12: max_val_vec = _mm_set1_epi16(4095); break;
  }
  __m128i zero_vec = _mm_setzero_si128();

  for (int j = 0; j < height; j++) {
    const uint16_t *current_input_row = input;
    uint16_t *current_dst_row = dst;
    __m128i left_vec_shr3 = _mm_setzero_si128();  // For the very first block
    uint16_t left_element =
        current_input_row[-1] >> 3;  // Value to the left of the current block

    for (int i = 0; i < width; i += 8) {
      __m128i current_in_vec =
          _mm_loadu_si128((const __m128i *)&current_input_row[i]);
      __m128i current_in_shr3_vec = _mm_srli_epi16(current_in_vec, 3);

      __m128i vector_1_vec;
      if (dir == 0) {
        if (j == 0 && !have_top) {
          vector_1_vec = current_in_shr3_vec;
        } else {
          vector_1_vec = _mm_srli_epi16(
              _mm_loadu_si128((const __m128i *)&above_row[i]), 3);
        }
      } else {
        __m128i prev_vec;
        if (i == 0) {
          uint16_t L = !have_left ? (current_input_row[i] >> 3) : left_element;
          prev_vec = _mm_setr_epi16(L, L, L, L, L, L, L, L);
        } else {
          prev_vec = left_vec_shr3;
        }
        vector_1_vec = _mm_alignr_epi8(current_in_shr3_vec, prev_vec, 14);
      }
      left_vec_shr3 = current_in_shr3_vec;
      left_element =
          _mm_extract_epi16(current_in_shr3_vec, 7);  // Update for next block

      // --- Calculate NON_LINEAR (V * V + M) >> BD ---
      __m128i v_val = current_in_shr3_vec;
      __m128i v_val_lo = _mm_mullo_epi16(v_val, v_val);
      __m128i v_val_hi = _mm_mulhi_epi16(v_val, v_val);
      __m128i v_sq_lo = _mm_unpacklo_epi16(v_val_lo, v_val_hi);
      __m128i v_sq_hi = _mm_unpackhi_epi16(v_val_lo, v_val_hi);

      __m128i mid_vec_bcast_lo =
          _mm_unpacklo_epi16(mid_vec, _mm_setzero_si128());
      __m128i mid_vec_bcast_hi =
          _mm_unpackhi_epi16(mid_vec, _mm_setzero_si128());

      __m128i non_linear_res_lo = _mm_add_epi32(v_sq_lo, mid_vec_bcast_lo);
      __m128i non_linear_res_hi = _mm_add_epi32(v_sq_hi, mid_vec_bcast_hi);

      non_linear_res_lo = _mm_srai_epi32(non_linear_res_lo, bit_depth);
      non_linear_res_hi = _mm_srai_epi32(non_linear_res_hi, bit_depth);

      __m128i vector_2_vec =
          _mm_packus_epi32(non_linear_res_lo, non_linear_res_hi);
      __m128i vector_0_vec = current_in_shr3_vec;
      __m128i vector_3_vec = mid_vec;

      // --- Convolve ---
      __m128i sum_lo = _mm_setzero_si128();
      __m128i sum_hi = _mm_setzero_si128();

      // param 0
      __m128i param0_lo_val = read_int64(alpha_q3[0]);
      __m128i param0_lo_bcast =
          _mm_shuffle_epi32(param0_lo_val, _MM_SHUFFLE(0, 0, 0, 0));
      __m128i vec0_lo = _mm_unpacklo_epi16(vector_0_vec, zero_vec);
      __m128i vec0_hi = _mm_unpackhi_epi16(vector_0_vec, zero_vec);
      sum_lo = _mm_add_epi32(sum_lo, _mm_mullo_epi32(vec0_lo, param0_lo_bcast));
      sum_hi = _mm_add_epi32(sum_hi, _mm_mullo_epi32(vec0_hi, param0_lo_bcast));

      // param 1
      __m128i param1_lo_val = read_int64(alpha_q3[1]);
      __m128i param1_lo_bcast =
          _mm_shuffle_epi32(param1_lo_val, _MM_SHUFFLE(0, 0, 0, 0));
      __m128i vec1_lo = _mm_unpacklo_epi16(vector_1_vec, zero_vec);
      __m128i vec1_hi = _mm_unpackhi_epi16(vector_1_vec, zero_vec);
      sum_lo = _mm_add_epi32(sum_lo, _mm_mullo_epi32(vec1_lo, param1_lo_bcast));
      sum_hi = _mm_add_epi32(sum_hi, _mm_mullo_epi32(vec1_hi, param1_lo_bcast));

      // param 2
      __m128i param2_lo_val = read_int64(alpha_q3[2]);
      __m128i param2_lo_bcast =
          _mm_shuffle_epi32(param2_lo_val, _MM_SHUFFLE(0, 0, 0, 0));
      __m128i vec2_lo = _mm_unpacklo_epi16(vector_2_vec, zero_vec);
      __m128i vec2_hi = _mm_unpackhi_epi16(vector_2_vec, zero_vec);
      sum_lo = _mm_add_epi32(sum_lo, _mm_mullo_epi32(vec2_lo, param2_lo_bcast));
      sum_hi = _mm_add_epi32(sum_hi, _mm_mullo_epi32(vec2_hi, param2_lo_bcast));

      // param 3
      __m128i param3_lo_val = read_int64(alpha_q3[3]);
      __m128i param3_lo_bcast =
          _mm_shuffle_epi32(param3_lo_val, _MM_SHUFFLE(0, 0, 0, 0));
      __m128i vec3_lo = _mm_unpacklo_epi16(vector_3_vec, zero_vec);
      __m128i vec3_hi = _mm_unpackhi_epi16(vector_3_vec, zero_vec);
      sum_lo = _mm_add_epi32(sum_lo, _mm_mullo_epi32(vec3_lo, param3_lo_bcast));
      sum_hi = _mm_add_epi32(sum_hi, _mm_mullo_epi32(vec3_hi, param3_lo_bcast));

      sum_lo = _mm_add_epi32(sum_lo, rounding_vec);
      sum_hi = _mm_add_epi32(sum_hi, rounding_vec);

      __m128i shifted_lo = _mm_srai_epi32(sum_lo, MHCCP_DECIM_BITS);
      __m128i shifted_hi = _mm_srai_epi32(sum_hi, MHCCP_DECIM_BITS);

      __m128i convolve_res = _mm_packs_epi32(shifted_lo, shifted_hi);

      // --- Clip ---
      __m128i clipped_res = _mm_max_epi16(convolve_res, zero_vec);
      clipped_res = _mm_min_epi16(clipped_res, max_val_vec);

      if (width < 8) {
        _mm_storeu_si64((__m128i *)&current_dst_row[i], clipped_res);
      } else {
        _mm_storeu_si128((__m128i *)&current_dst_row[i], clipped_res);
      }
    }

    dst += dst_stride;
    input += cfl_stride;
    above_row += cfl_stride;
  }
}
#endif  // CONFIG_ENABLE_MHCCP
