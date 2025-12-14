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
#include <emmintrin.h>

#include "config/av2_rtcd.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/temporal_filter.h"

// For the squared error buffer, keep a padding for 4 samples
#define SSE_STRIDE (BW + 4)

DECLARE_ALIGNED(32, static const uint32_t, sse_bytemask_2x4[4][2][4]) = {
  { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } },
  { { 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 } },
  { { 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 } },
  { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } }
};

static void get_squared_error(const uint16_t *frame1, const unsigned int stride,
                              const uint16_t *frame2,
                              const unsigned int stride2, const int block_width,
                              const int block_height, uint32_t *frame_sse,
                              const unsigned int dst_stride) {
  const uint16_t *src1 = frame1;
  const uint16_t *src2 = frame2;
  uint32_t *dst = frame_sse;

  for (int i = 0; i < block_height; i++) {
    for (int j = 0; j < block_width; j += 8) {
      __m128i vsrc1 = _mm_loadu_si128((__m128i *)(src1 + j));
      __m128i vsrc2 = _mm_loadu_si128((__m128i *)(src2 + j));

      __m128i vdiff = _mm_sub_epi16(vsrc1, vsrc2);
      __m128i vmullo = _mm_mullo_epi16(vdiff, vdiff);
      __m128i vmullh = _mm_mulhi_epi16(vdiff, vdiff);

      __m128i vres1 = _mm_unpacklo_epi16(vmullo, vmullh);
      __m128i vres2 = _mm_unpackhi_epi16(vmullo, vmullh);

      _mm_storeu_si128((__m128i *)(dst + j + 2), vres1);
      _mm_storeu_si128((__m128i *)(dst + j + 6), vres2);
    }

    src1 += stride;
    src2 += stride2;
    dst += dst_stride;
  }
}

static void xx_load_and_pad(uint32_t *src, __m128i *dstvec, int col,
                            int block_width) {
  __m128i vtmp1 = _mm_loadu_si128((__m128i *)src);
  __m128i vtmp2 = _mm_loadu_si128((__m128i *)(src + 4));
  // For the first column, replicate the first element twice to the left
  dstvec[0] = (col) ? vtmp1 : _mm_shuffle_epi32(vtmp1, 0xEA);
  // For the last column, replicate the last element twice to the right
  dstvec[1] = (col < block_width - 4) ? vtmp2 : _mm_shuffle_epi32(vtmp2, 0x54);
}

static int32_t xx_mask_and_hadd(__m128i vsum1, __m128i vsum2, int i) {
  __m128i veca, vecb;
  // Mask and obtain the required 5 values inside the vector
  veca = _mm_and_si128(vsum1, *(__m128i *)sse_bytemask_2x4[i][0]);
  vecb = _mm_and_si128(vsum2, *(__m128i *)sse_bytemask_2x4[i][1]);
  // A = [A0+B0, A1+B1, A2+B2, A3+B3]
  veca = _mm_add_epi32(veca, vecb);
  // B = [A2+B2, A3+B3, 0, 0]
  vecb = _mm_srli_si128(veca, 8);
  // A = [A0+B0+A2+B2, A1+B1+A3+B3, X, X]
  veca = _mm_add_epi32(veca, vecb);
  // B = [A1+B1+A3+B3, 0, 0, 0]
  vecb = _mm_srli_si128(veca, 4);
  // A = [A0+B0+A2+B2+A1+B1+A3+B3, X, X, X]
  veca = _mm_add_epi32(veca, vecb);
  return _mm_cvtsi128_si32(veca);
}

static void highbd_apply_temporal_filter(
    const uint16_t *frame1, const unsigned int stride, const uint16_t *frame2,
    const unsigned int stride2, const int block_width, const int block_height,
    const int min_frame_size, const double sigma, const MV *subblock_mvs,
    const int *subblock_mses, const int q_factor, const int filter_strength,
    unsigned int *accumulator, uint16_t *count, uint32_t *luma_sq_error,
    uint32_t *chroma_sq_error, int plane, int ss_x_shift, int ss_y_shift,
    int bd) {
  assert(((block_width == 32) && (block_height == 32)) ||
         ((block_width == 16) && (block_height == 16)));
  if (plane > PLANE_TYPE_Y) assert(chroma_sq_error != NULL);

  uint32_t acc_5x5_sse[BH][BW];
  uint32_t *frame_sse =
      (plane == PLANE_TYPE_Y) ? luma_sq_error : chroma_sq_error;

  get_squared_error(frame1, stride, frame2, stride2, block_width, block_height,
                    frame_sse, SSE_STRIDE);

  __m128i vsrc[5][2];

  const double n_decay = 0.5 + log(2 * sigma + 5.0);
  const double q_decay =
      CLIP(pow((double)q_factor / TF_Q_DECAY_THRESHOLD, 2), 1e-5, 1);
  const double s_decay =
      CLIP(pow((double)filter_strength / TF_STRENGTH_THRESHOLD, 2), 1e-5, 1);

  // Traverse 4 columns at a time
  // First and last columns will require padding
  for (int col = 0; col < block_width; col += 4) {
    uint32_t *src = frame_sse + col;

    // Load and pad(for first and last col) 3 rows from the top
    for (int i = 2; i < 5; i++) {
      xx_load_and_pad(src, vsrc[i], col, block_width);
      src += SSE_STRIDE;
    }

    // Padding for top 2 rows
    vsrc[0][0] = vsrc[2][0];
    vsrc[0][1] = vsrc[2][1];
    vsrc[1][0] = vsrc[2][0];
    vsrc[1][1] = vsrc[2][1];

    for (int row = 0; row < block_height - 3; row++) {
      __m128i vsum11 = _mm_add_epi32(vsrc[0][0], vsrc[1][0]);
      __m128i vsum12 = _mm_add_epi32(vsrc[2][0], vsrc[3][0]);
      __m128i vsum13 = _mm_add_epi32(vsum11, vsum12);
      __m128i vsum1 = _mm_add_epi32(vsum13, vsrc[4][0]);

      __m128i vsum21 = _mm_add_epi32(vsrc[0][1], vsrc[1][1]);
      __m128i vsum22 = _mm_add_epi32(vsrc[2][1], vsrc[3][1]);
      __m128i vsum23 = _mm_add_epi32(vsum21, vsum22);
      __m128i vsum2 = _mm_add_epi32(vsum23, vsrc[4][1]);

      vsrc[0][0] = vsrc[1][0];
      vsrc[0][1] = vsrc[1][1];
      vsrc[1][0] = vsrc[2][0];
      vsrc[1][1] = vsrc[2][1];
      vsrc[2][0] = vsrc[3][0];
      vsrc[2][1] = vsrc[3][1];
      vsrc[3][0] = vsrc[4][0];
      vsrc[3][1] = vsrc[4][1];

      // Load next row
      xx_load_and_pad(src, vsrc[4], col, block_width);
      src += SSE_STRIDE;

      acc_5x5_sse[row][col] = xx_mask_and_hadd(vsum1, vsum2, 0);
      acc_5x5_sse[row][col + 1] = xx_mask_and_hadd(vsum1, vsum2, 1);
      acc_5x5_sse[row][col + 2] = xx_mask_and_hadd(vsum1, vsum2, 2);
      acc_5x5_sse[row][col + 3] = xx_mask_and_hadd(vsum1, vsum2, 3);
    }
    for (int row = block_height - 3; row < block_height; row++) {
      __m128i vsum11 = _mm_add_epi32(vsrc[0][0], vsrc[1][0]);
      __m128i vsum12 = _mm_add_epi32(vsrc[2][0], vsrc[3][0]);
      __m128i vsum13 = _mm_add_epi32(vsum11, vsum12);
      __m128i vsum1 = _mm_add_epi32(vsum13, vsrc[4][0]);

      __m128i vsum21 = _mm_add_epi32(vsrc[0][1], vsrc[1][1]);
      __m128i vsum22 = _mm_add_epi32(vsrc[2][1], vsrc[3][1]);
      __m128i vsum23 = _mm_add_epi32(vsum21, vsum22);
      __m128i vsum2 = _mm_add_epi32(vsum23, vsrc[4][1]);

      vsrc[0][0] = vsrc[1][0];
      vsrc[0][1] = vsrc[1][1];
      vsrc[1][0] = vsrc[2][0];
      vsrc[1][1] = vsrc[2][1];
      vsrc[2][0] = vsrc[3][0];
      vsrc[2][1] = vsrc[3][1];
      vsrc[3][0] = vsrc[4][0];
      vsrc[3][1] = vsrc[4][1];

      acc_5x5_sse[row][col] = xx_mask_and_hadd(vsum1, vsum2, 0);
      acc_5x5_sse[row][col + 1] = xx_mask_and_hadd(vsum1, vsum2, 1);
      acc_5x5_sse[row][col + 2] = xx_mask_and_hadd(vsum1, vsum2, 2);
      acc_5x5_sse[row][col + 3] = xx_mask_and_hadd(vsum1, vsum2, 3);
    }
  }

  for (int i = 0, k = 0; i < block_height; i++) {
    for (int j = 0; j < block_width; j++, k++) {
      const int pixel_value = frame2[i * stride2 + j];

      int diff_sse = acc_5x5_sse[i][j];
      int num_ref_pixels = TF_WINDOW_LENGTH * TF_WINDOW_LENGTH;

      // Filter U-plane and V-plane using Y-plane. This is because motion
      // search is only done on Y-plane, so the information from Y-plane will
      // be more accurate.
      if (plane != PLANE_TYPE_Y) {
        for (int ii = 0; ii < (1 << ss_y_shift); ++ii) {
          for (int jj = 0; jj < (1 << ss_x_shift); ++jj) {
            const int yy = (i << ss_y_shift) + ii;      // Y-coord on Y-plane.
            const int xx = (j << ss_x_shift) + jj + 2;  // X-coord on Y-plane.
            const int ww = SSE_STRIDE;                  // Stride of Y-plane.
            diff_sse += luma_sq_error[yy * ww + xx];
            ++num_ref_pixels;
          }
        }
      }

      // Scale down the difference for high bit depth input.
      diff_sse >>= ((bd - 8) * 2);

      const double window_error = (double)(diff_sse) / num_ref_pixels;
      const int subblock_idx =
          (i >= block_height / 2) * 2 + (j >= block_width / 2);
      const double block_error = (double)subblock_mses[subblock_idx];
      const double combined_error =
          (TF_WINDOW_BLOCK_BALANCE_WEIGHT * window_error + block_error) /
          (TF_WINDOW_BLOCK_BALANCE_WEIGHT + 1) / TF_SEARCH_ERROR_NORM_WEIGHT;

      const MV mv = subblock_mvs[subblock_idx];
      const double distance = sqrt(pow(mv.row, 2) + pow(mv.col, 2));
      const double distance_threshold =
          (double)AVMMAX(min_frame_size * TF_SEARCH_DISTANCE_THRESHOLD, 1);
      const double d_factor = AVMMAX(distance / distance_threshold, 1);

      const double scaled_error =
          AVMMIN(combined_error * d_factor / n_decay / q_decay / s_decay, 7);
      const int weight = (int)(exp(-scaled_error) * TF_WEIGHT_SCALE);

      count[k] += weight;
      accumulator[k] += weight * pixel_value;
    }
  }
}

void av2_highbd_apply_temporal_filter_sse2(
    const YV12_BUFFER_CONFIG *frame_to_filter, const MACROBLOCKD *mbd,
    const BLOCK_SIZE block_size, const int mb_row, const int mb_col,
    const int num_planes, const double *noise_levels, const MV *subblock_mvs,
    const int *subblock_mses, const int q_factor, const int filter_strength,
    const uint16_t *pred, uint32_t *accum, uint16_t *count) {
  assert(block_size == BLOCK_32X32 && "Only support 32x32 block with avx2!");
  assert(TF_WINDOW_LENGTH == 5 && "Only support window length 5 with avx2!");
  assert(num_planes >= 1 && num_planes <= MAX_MB_PLANE);

  const int mb_height = block_size_high[block_size];
  const int mb_width = block_size_wide[block_size];
  const int mb_pels = mb_height * mb_width;
  const int frame_height = frame_to_filter->y_crop_height;
  const int frame_width = frame_to_filter->y_crop_width;
  const int min_frame_size = AVMMIN(frame_height, frame_width);
  uint32_t luma_sq_error[SSE_STRIDE * BH];
  uint32_t *chroma_sq_error =
      (num_planes > 0)
          ? (uint32_t *)avm_malloc(SSE_STRIDE * BH * sizeof(uint32_t))
          : NULL;
  for (int plane = 0; plane < num_planes; ++plane) {
    const uint32_t plane_h = mb_height >> mbd->plane[plane].subsampling_y;
    const uint32_t plane_w = mb_width >> mbd->plane[plane].subsampling_x;
    const uint32_t frame_stride = frame_to_filter->strides[plane == 0 ? 0 : 1];
    const int frame_offset = mb_row * plane_h * frame_stride + mb_col * plane_w;

    const uint16_t *ref = frame_to_filter->buffers[plane] + frame_offset;
    const int ss_x_shift =
        mbd->plane[plane].subsampling_x - mbd->plane[0].subsampling_x;
    const int ss_y_shift =
        mbd->plane[plane].subsampling_y - mbd->plane[0].subsampling_y;

    highbd_apply_temporal_filter(
        ref, frame_stride, pred + mb_pels * plane, plane_w, plane_w, plane_h,
        min_frame_size, noise_levels[plane], subblock_mvs, subblock_mses,
        q_factor, filter_strength, accum + mb_pels * plane,
        count + mb_pels * plane, luma_sq_error, chroma_sq_error, plane,
        ss_x_shift, ss_y_shift, mbd->bd);
  }
  if (chroma_sq_error != NULL) avm_free(chroma_sq_error);
}
