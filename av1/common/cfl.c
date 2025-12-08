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

#include "av1/common/av1_common_int.h"
#include "av1/common/cfl.h"
#include "av1/common/common_data.h"
#include "av1/common/enums.h"
#include "av1/common/reconintra.h"
#include "config/av1_rtcd.h"
#include "av1/common/reconinter.h"
#include "av1/common/warped_motion.h"

#define LOCAL_FIXED_MULT(x, y, round, bits) (((x) * (y) + round) >> bits)

/*
 * Approximate ((a * b) + round) >> shift using only 32-bit intermediates.
 * Strategy:
 *   1) Compute the bit-width of |a| and |b|.
 *   2) Right-shift a and/or b by s1/s2 so that (bits(a)-s1)+(bits(b)-s2) <= 31,
 *      ensuring int32 multiplication cannot overflow.
 *   3) Compensate in the final right shift: adj = shift - (s1 + s2).
 *   4) Perform symmetric round-to-nearest (round half away from zero).
 *
 * This keeps all intermediates in 32-bit while keeping the mean error low.
 */
static INLINE int32_t mul_fixed32_adapt(int32_t a, int32_t b, int shift) {
  /* 1) Effective bit-widths of |a| and |b| (in [1..32]) */
  const uint32_t ua = (uint32_t)(a < 0 ? -a : a);
  const uint32_t ub = (uint32_t)(b < 0 ? -b : b);
  const int bits_a = ilog2_32(ua) + 1;
  const int bits_b = ilog2_32(ub) + 1;
  // We need to reserve extra bits to prevent overflow.
  const int bits_limit = 29;

  /* 2) Decide how many bits to drop in total to avoid mul overflow */
  int need = bits_a + bits_b - bits_limit;
  if (need < 0) need = 0;

  /* Split the drop across a and b to minimize error */
  int s1 = need >> 1;
  int s2 = need - s1;

  const int adj = shift - (s1 + s2);
  assert(s1 >= 0);
  assert(s2 >= 0);
  const int32_t a_sh = s1 ? (a >> s1) : a;
  const int32_t b_sh = s2 ? (b >> s2) : b;

  /* 3) Safe 32-bit product */
  const int32_t prod = a_sh * b_sh;

  /* 4) Final right shift with symmetric rounding to nearest */
  if (adj <= 0) return prod; /* no further shift */

  const uint32_t bias = (adj <= bits_limit) ? (1u << (adj - 1)) : 0u;
  if (prod >= 0) {
    return (adj <= bits_limit) ? (int32_t)(((uint32_t)prod + bias) >> adj) : 0;
  } else {
    return (adj <= bits_limit) ? -(int32_t)(((uint32_t)(-prod) + bias) >> adj)
                               : 0;
  }
}

void cfl_init(CFL_CTX *cfl, const SequenceHeader *seq_params) {
  assert(block_size_wide[CFL_MAX_BLOCK_SIZE] == CFL_BUF_LINE);
  assert(block_size_high[CFL_MAX_BLOCK_SIZE] == CFL_BUF_LINE);

  memset(&cfl->recon_buf_q3, 0, sizeof(cfl->recon_buf_q3));
  memset(&cfl->ac_buf_q3, 0, sizeof(cfl->ac_buf_q3));
  cfl->subsampling_x = seq_params->subsampling_x;
  cfl->subsampling_y = seq_params->subsampling_y;
  cfl->are_parameters_computed = 0;
  cfl->store_y = 0;
  // The DC_PRED cache is disabled by default and is only enabled in
  // cfl_rd_pick_alpha
  cfl->use_dc_pred_cache = 0;
  cfl->dc_pred_is_cached[CFL_PRED_U] = 0;
  cfl->dc_pred_is_cached[CFL_PRED_V] = 0;
}

void cfl_store_dc_pred(MACROBLOCKD *const xd, const uint16_t *input,
                       CFL_PRED_TYPE pred_plane, int width) {
  assert(pred_plane < CFL_PRED_PLANES);
  assert(width <= CFL_BUF_LINE);

  memcpy(xd->cfl.dc_pred_cache[pred_plane], input, width * sizeof(*input));
  return;
}

static void cfl_load_dc_pred_hbd(const uint16_t *dc_pred_cache, uint16_t *dst,
                                 int dst_stride, int width, int height) {
  const size_t num_bytes = width * sizeof(*dst);
  for (int j = 0; j < height; j++) {
    memcpy(dst, dc_pred_cache, num_bytes);
    dst += dst_stride;
  }
}
void cfl_load_dc_pred(MACROBLOCKD *const xd, uint16_t *dst, int dst_stride,
                      TX_SIZE tx_size, CFL_PRED_TYPE pred_plane) {
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  assert(pred_plane < CFL_PRED_PLANES);
  assert(width <= CFL_BUF_LINE);
  assert(height <= CFL_BUF_LINE);
  cfl_load_dc_pred_hbd(xd->cfl.dc_pred_cache[pred_plane], dst, dst_stride,
                       width, height);
}

// Due to frame boundary issues, it is possible that the total area covered by
// chroma exceeds that of luma. When this happens, we fill the missing pixels by
// repeating the last columns and/or rows.
static INLINE void cfl_pad(CFL_CTX *cfl, int width, int height) {
  const int diff_width = width - cfl->buf_width;
  const int diff_height = height - cfl->buf_height;
  uint16_t last_pixel;
  if (diff_width > 0) {
    const int min_height = height - diff_height;
    uint16_t *recon_buf_q3 = cfl->recon_buf_q3 + (width - diff_width);
    for (int j = 0; j < min_height; j++) {
      last_pixel = recon_buf_q3[-1];
      assert(recon_buf_q3 + diff_width <= cfl->recon_buf_q3 + CFL_BUF_SQUARE);
      for (int i = 0; i < diff_width; i++) {
        recon_buf_q3[i] = last_pixel;
      }
      recon_buf_q3 += CFL_BUF_LINE;
    }
    cfl->buf_width = width;
  }
  if (diff_height > 0) {
    uint16_t *recon_buf_q3 =
        cfl->recon_buf_q3 + ((height - diff_height) * CFL_BUF_LINE);
    for (int j = 0; j < diff_height; j++) {
      const uint16_t *last_row_q3 = recon_buf_q3 - CFL_BUF_LINE;
      assert(recon_buf_q3 + width <= cfl->recon_buf_q3 + CFL_BUF_SQUARE);
      for (int i = 0; i < width; i++) {
        recon_buf_q3[i] = last_row_q3[i];
      }
      recon_buf_q3 += CFL_BUF_LINE;
    }
    cfl->buf_height = height;
  }
}

static void subtract_average_c(const uint16_t *src, int16_t *dst, int width,
                               int height, int round_offset, int num_pel_log2) {
  int sum = round_offset;
  const uint16_t *recon = src;
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      sum += recon[i];
    }
    recon += CFL_BUF_LINE;
  }
  const int avg = sum >> num_pel_log2;
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      dst[i] = src[i] - avg;
    }
    src += CFL_BUF_LINE;
    dst += CFL_BUF_LINE;
  }
}

CFL_SUB_AVG_FN(c)

static INLINE int cfl_idx_to_alpha(uint8_t alpha_idx, int8_t joint_sign,
                                   CFL_PRED_TYPE pred_type) {
  const int alpha_sign = (pred_type == CFL_PRED_U) ? CFL_SIGN_U(joint_sign)
                                                   : CFL_SIGN_V(joint_sign);
  if (alpha_sign == CFL_SIGN_ZERO) return 0;
  const int abs_alpha_q3 =
      (pred_type == CFL_PRED_U) ? CFL_IDX_U(alpha_idx) : CFL_IDX_V(alpha_idx);
  return (alpha_sign == CFL_SIGN_POS) ? abs_alpha_q3 + 1 : -abs_alpha_q3 - 1;
}

void cfl_predict_hbd_c(const int16_t *ac_buf_q3, uint16_t *dst, int dst_stride,
                       int alpha_q3, int bit_depth, int width, int height) {
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      dst[i] = clip_pixel_highbd(
          get_scaled_luma_q0(alpha_q3, ac_buf_q3[i]) + dst[i], bit_depth);
    }
    dst += dst_stride;
    ac_buf_q3 += CFL_BUF_LINE;
  }
}

CFL_PREDICT_FN(c, hbd)

// Subtract the average from neighoring pixels
static void subtract_average_neighbor(const uint16_t *src, int16_t *dst,
                                      int width, int height, int avg) {
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      dst[i] = src[i] - avg;
    }
    src += CFL_BUF_LINE;
    dst += CFL_BUF_LINE;
  }
}

// Calculate luma AC values with neighbor DC
static void cfl_compute_parameters_alt(CFL_CTX *const cfl, TX_SIZE tx_size) {
  cfl_pad(cfl, tx_size_wide[tx_size], tx_size_high[tx_size]);

  subtract_average_neighbor(cfl->recon_buf_q3, cfl->ac_buf_q3,
                            tx_size_wide[tx_size], tx_size_high[tx_size],
                            cfl->avg_l);
  cfl->are_parameters_computed = 1;
}

static void get_top_bottom_offsets(int is_top_sb_boundary, int *top_offset,
                                   int *bottom_offset) {
  // If this is the above super block boundary, use only the above line and
  // repeated it. This can be done by changing the offset.
  *top_offset = 2 - is_top_sb_boundary;
  *bottom_offset = 1 - is_top_sb_boundary;
}

void cfl_implicit_fetch_neighbor_luma(const AV1_COMMON *cm,
                                      MACROBLOCKD *const xd, int row, int col,
                                      int is_top_sb_boundary, int width,
                                      int height) {
  CFL_CTX *const cfl = &xd->cfl;
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  int input_stride = pd->dst.stride;

  const int row_dst =
      row + xd->mi[0]->chroma_ref_info.mi_row_chroma_base - xd->mi_row;
  const int col_dst =
      col + xd->mi[0]->chroma_ref_info.mi_col_chroma_base - xd->mi_col;
  uint16_t *dst =
      &pd->dst.buf[-((-row_dst * pd->dst.stride - col_dst) << MI_SIZE_LOG2)];

  const int sub_x = cfl->subsampling_x;
  const int sub_y = cfl->subsampling_y;
  const int row_start =
      ((xd->mi[0]->chroma_ref_info.mi_row_chroma_base + row) << MI_SIZE_LOG2);
  const int col_start =
      ((xd->mi[0]->chroma_ref_info.mi_col_chroma_base + col) << MI_SIZE_LOG2);
  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row, col, AOM_PLANE_U);

  memset(cfl->recon_yuv_buf_above[0], 0, sizeof(cfl->recon_yuv_buf_above[0]));
  memset(cfl->recon_yuv_buf_left[0], 0, sizeof(cfl->recon_yuv_buf_left[0]));
  // top boundary
  uint16_t *output_q3 = cfl->recon_yuv_buf_above[0];
  if (have_top) {
    // If this is the above super block boundary, use only the above line and
    // repeated it.
    int top_offset = 0;  // In the case filter_type is 2, top_offset points to
                         // the middle reference line
    int bottom_offset = 0;
    get_top_bottom_offsets(is_top_sb_boundary, &top_offset, &bottom_offset);

    if (sub_x && sub_y) {
      uint16_t *input = dst - top_offset * input_stride;
      for (int i = 0; i < width; i += 2) {
        const int bot = i + bottom_offset * input_stride;
        const int filter_type = cm->seq_params.cfl_ds_filter_index;
        if (filter_type == 1) {
          output_q3[i >> 1] = input[AOMMAX(0, i - 1)] + 2 * input[i] +
                              input[i + 1] + input[bot + AOMMAX(-1, -i)] +
                              2 * input[bot] + input[bot + 1];
        } else if (filter_type == 2) {
          const int top =
              i - (is_top_sb_boundary ? 0 : 1) *
                      input_stride;  // If this is the top sb boundary, the top
                                     // index points to the current sample
          output_q3[i >> 1] = input[AOMMAX(0, i - 1)] + 4 * input[i] +
                              input[i + 1] + input[top] + input[bot];
        } else {
          output_q3[i >> 1] =
              (input[i] + input[i + 1] + input[bot] + input[bot + 1]) << 1;
        }
      }
    } else if (sub_x) {
      uint16_t *input = dst - input_stride;
      for (int i = 0; i < width; i += 2) {
        const int filter_type = cm->seq_params.cfl_ds_filter_index;
        if (filter_type == 1) {
          output_q3[i >> 1] =
              (input[AOMMAX(0, i - 1)] + 2 * input[i] + input[i + 1]) << 1;
        } else if (filter_type == 2) {
          output_q3[i >> 1] = input[i] << 3;
        } else {
          output_q3[i >> 1] = (input[i] + input[i + 1]) << 2;
        }
      }
    } else if (sub_y) {
      uint16_t *input = dst - top_offset * input_stride;
      for (int i = 0; i < width; ++i) {
        const int bot = i + bottom_offset * input_stride;
        output_q3[i] = (input[i] + input[bot]) << 2;
      }
    } else {
      uint16_t *input = dst - input_stride;
      for (int i = 0; i < width; ++i) output_q3[i] = input[i] << 3;
    }
    if (col_start >= pd->dst.width) {
      const uint16_t mid = (1 << xd->bd) >> 1;
      for (int j = 0; j < width >> sub_x; ++j) {
        output_q3[j] = mid;
      }
    } else if ((col_start + width) > pd->dst.width) {
      int temp = width - ((col_start + width) - pd->dst.width);
      assert(temp > 0 && temp < width);
      for (int i = temp >> sub_x; i < width >> sub_x; ++i) {
        output_q3[i] = output_q3[i - 1];
      }
    }
  }

  // left boundary
  output_q3 = cfl->recon_yuv_buf_left[0];
  if (have_left) {
    if (sub_x && sub_y) {
      uint16_t *input = dst - 2;
      for (int j = 0; j < height; j += 2) {
        const int bot = input_stride;
        const int filter_type = cm->seq_params.cfl_ds_filter_index;
        if (filter_type == 1) {
          output_q3[j >> 1] = input[-1] + 2 * input[0] + input[1] +
                              input[bot - 1] + 2 * input[bot] + input[bot + 1];
        } else if (filter_type == 2) {
          const int top = (j == 0) ? 0 : (0 - input_stride);
          output_q3[j >> 1] =
              input[-1] + 4 * input[0] + input[1] + input[top] + input[bot];
        } else {
          output_q3[j >> 1] =
              (input[0] + input[1] + input[bot] + input[bot + 1]) << 1;
        }
        input += input_stride * 2;
      }
    } else if (sub_x) {
      uint16_t *input = dst - 2;
      for (int j = 0; j < height; ++j) {
        const int filter_type = cm->seq_params.cfl_ds_filter_index;
        if (filter_type == 1) {
          output_q3[j] = (input[-1] + 2 * input[0] + input[1]) << 1;
        } else if (filter_type == 2) {
          output_q3[j] = input[0] << 3;
        } else {
          output_q3[j] = (input[0] + input[1]) << 2;
        }
        input += input_stride;
      }
    } else if (sub_y) {
      uint16_t *input = dst - 1;
      for (int j = 0; j < height; ++j) {
        output_q3[j] = (input[0] + input[input_stride]) << 2;
        input += input_stride * 2;
      }
    } else {
      uint16_t *input = dst - 1;
      for (int j = 0; j < height; ++j)
        output_q3[j] = input[j * input_stride] << 3;
    }
    if (row_start >= pd->dst.height) {
      const uint16_t mid = (1 << xd->bd) >> 1;
      for (int j = 0; j < height >> sub_y; ++j) {
        output_q3[j] = mid;
      }
    } else if ((row_start + height) > pd->dst.height) {
      int temp = height - ((row_start + height) - pd->dst.height);
      assert(temp > 0 && temp < height);
      for (int j = temp >> sub_y; j < height >> sub_y; ++j) {
        output_q3[j] = output_q3[j - 1];
      }
    }
  }
}

void cfl_calc_luma_dc(MACROBLOCKD *const xd, int row, int col,
                      TX_SIZE tx_size) {
  CFL_CTX *const cfl = &xd->cfl;
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const int ss_hor = width > 32 ? 2 : 1;
  const int ss_ver = height > 32 ? 2 : 1;

  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row, col, AOM_PLANE_U);

  int count = 0;
  int sum_x = 0;

  uint16_t *l;
  if (have_top) {
    l = cfl->recon_yuv_buf_above[0];
    for (int i = 0; i < width; i += ss_hor) {
      sum_x += l[i];
      count++;
    }
  }

  if (have_left) {
    l = cfl->recon_yuv_buf_left[0];
    for (int i = 0; i < height; i += ss_ver) {
      sum_x += l[i];
      count++;
    }
  }

  if (count > 0) {
    int16_t shift = 0;
    const uint16_t scale = resolve_divisor_32(count, &shift);
    const uint16_t rounding = 1 << shift >> 1;
    // The clipping range is set to bd + 3, as the downsampling filter applied
    // to luma samples uses coefficients with a total sum of 8
    const uint16_t val = (sum_x * scale + rounding) >> shift;
    const uint16_t max_v = (1 << (xd->bd + 3)) - 1;
    cfl->avg_l = val > max_v ? max_v : val;
  } else {
    cfl->avg_l = 8 << (xd->bd - 1);
  }
}

void cfl_implicit_fetch_neighbor_chroma(const AV1_COMMON *cm,
                                        MACROBLOCKD *const xd, int plane,
                                        int row, int col, TX_SIZE tx_size) {
  (void)cm;
  CFL_CTX *const cfl = &xd->cfl;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  int input_stride = pd->dst.stride;
  uint16_t *dst = &pd->dst.buf[(row * pd->dst.stride + col) << MI_SIZE_LOG2];

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const int sub_x = cfl->subsampling_x;
  const int sub_y = cfl->subsampling_y;

  int pic_width_c = pd->dst.width;
  int pic_height_c = pd->dst.height;

  const int row_start =
      (((xd->mi[0]->chroma_ref_info.mi_row_chroma_base >> sub_y) + row)
       << MI_SIZE_LOG2);
  const int col_start =
      (((xd->mi[0]->chroma_ref_info.mi_col_chroma_base >> sub_x) + col)
       << MI_SIZE_LOG2);
  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row, col, plane);

  memset(cfl->recon_yuv_buf_above[plane], 0,
         sizeof(cfl->recon_yuv_buf_above[plane]));
  memset(cfl->recon_yuv_buf_left[plane], 0,
         sizeof(cfl->recon_yuv_buf_left[plane]));

  // top boundary
  uint16_t *output_q3 = cfl->recon_yuv_buf_above[plane];
  if (have_top) {
    uint16_t *input = dst - input_stride;
    for (int i = 0; i < width; ++i) {
      output_q3[i] = input[i];
    }
    if (col_start >= pic_width_c) {
      const uint16_t mid = (1 << xd->bd) >> 1;
      for (int i = 0; i < width; ++i) {
        output_q3[i] = mid;
      }
    } else if ((col_start + width) > pic_width_c) {
      int temp = width - ((col_start + width) - pic_width_c);
      assert(temp > 0 && temp < width);
      for (int i = temp; i < width; ++i) {
        output_q3[i] = output_q3[i - 1];
      }
    }
  }

  // left boundary
  output_q3 = cfl->recon_yuv_buf_left[plane];
  if (have_left) {
    uint16_t *input = dst - 1;
    for (int j = 0; j < height; ++j) {
      output_q3[j] = input[0];
      input += input_stride;
    }

    if (row_start >= pic_height_c) {
      const uint16_t mid = (1 << xd->bd) >> 1;
      for (int i = 0; i < height; ++i) {
        output_q3[i] = mid;
      }
    } else if ((row_start + height) > pic_height_c) {
      int temp = height - ((row_start + height) - pic_height_c);
      assert(temp > 0 && temp < height);
      for (int j = temp; j < height; ++j) {
        output_q3[j] = output_q3[j - 1];
      }
    }
  }
}

void cfl_derive_implicit_scaling_factor(MACROBLOCKD *const xd, int plane,
                                        int row, int col, TX_SIZE tx_size) {
  CFL_CTX *const cfl = &xd->cfl;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row, col, plane);

  // Distribute number of reference samples above and left based on the width,
  // height and the availability of the above and left. If only one side is
  // available, the number is distributed to the avalable reference side. Else,
  // if one side is larger than the other side by more than 2 times, the number
  // is distributed to the larger side. Else, the number is distributed equally
  // to two side. NUM_REF_SAM_CFL is 8, so the division can be replaced by bit
  // right shift by 3.
  int numb_up = 0;
  int numb_left = 0;

  if (have_top && have_left) {
    if (width > (height * 2)) {
      numb_left = 0;
      numb_up = NUM_REF_SAM_CFL;
    } else if (height > (width * 2)) {
      numb_up = 0;
      numb_left = NUM_REF_SAM_CFL;
    } else {
      numb_up = NUM_REF_SAM_CFL >> 1;
      numb_left = NUM_REF_SAM_CFL >> 1;
    }
  } else {
    numb_up = have_top ? NUM_REF_SAM_CFL : 0;
    numb_left = have_left ? NUM_REF_SAM_CFL : 0;
  }
  numb_up = (numb_up > width) ? width : numb_up;
  numb_left = (numb_left > height) ? height : numb_left;

  int count = 0;
  int sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;

  uint16_t *l, *c;

  if (numb_up > 0) {
    l = cfl->recon_yuv_buf_above[0];
    c = cfl->recon_yuv_buf_above[plane];

    const int step_up = AOMMAX((int)width / numb_up, 1);
    const int start_up = (step_up == 1) ? 0 : (step_up >> 1);

    for (int i = start_up; i < width; i += step_up) {
      sum_x += l[i] >> 3;
      sum_y += c[i];
      sum_xy += (l[i] >> 3) * c[i];
      sum_xx += (l[i] >> 3) * (l[i] >> 3);
      ++count;
    }
  }

  if (numb_left > 0) {
    l = cfl->recon_yuv_buf_left[0];
    c = cfl->recon_yuv_buf_left[plane];

    const int step_left = AOMMAX((int)height / numb_left, 1);
    const int start_left = (step_left == 1) ? 0 : (step_left >> 1);

    for (int i = start_left; i < height; i += step_left) {
      sum_x += l[i] >> 3;
      sum_y += c[i];
      sum_xy += (l[i] >> 3) * c[i];
      sum_xx += (l[i] >> 3) * (l[i] >> 3);
      ++count;
    }
  }

  const int shift = 3 + CFL_ADD_BITS_ALPHA;
  mbmi->cfl_implicit_alpha[plane - 1] = derive_linear_parameters_alpha(
      sum_x, sum_y, sum_xx, sum_xy, count, shift);
}

void cfl_derive_block_implicit_scaling_factor(uint16_t *l, const uint16_t *c,
                                              const int width, const int height,
                                              const int stride,
                                              const int chroma_stride,
                                              int *alpha) {
  int count = 0;
  int sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      sum_x += l[i + j * stride] >> 3;
      sum_y += c[i + j * chroma_stride];
      sum_xy += (l[i + j * stride] >> 3) * c[i + j * chroma_stride];
      sum_xx += (l[i + j * stride] >> 3) * (l[i + j * stride] >> 3);
    }
    count += width;
  }

  const int shift = 3 + CFL_ADD_BITS_ALPHA;
  *alpha = derive_linear_parameters_alpha(sum_x, sum_y, sum_xx, sum_xy, count,
                                          shift);
}

void cfl_predict_block(bool seq_enable_cfl_intra, bool seq_enable_mhccp,
                       MACROBLOCKD *const xd, uint16_t *dst, int dst_stride,
                       TX_SIZE tx_size, int plane, bool have_top,
                       bool have_left, int above_lines, int left_lines) {
  CFL_CTX *const cfl = &xd->cfl;
  MB_MODE_INFO *mbmi = xd->mi[0];

  if (!seq_enable_cfl_intra && !seq_enable_mhccp) return;

  cfl_compute_parameters_alt(cfl, tx_size);
  int alpha_q3;
  if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
    mhccp_predict_hv_hbd(cfl->mhccp_ref_buf_q3[0] + (uint16_t)left_lines +
                             (uint16_t)above_lines * CFL_BUF_LINE * 2,
                         dst, have_top, have_left, dst_stride,
                         mbmi->mhccp_implicit_param[plane - 1], xd->bd,
                         tx_size_wide[tx_size], tx_size_high[tx_size],
                         mbmi->mh_dir);
    return;
  } else if (mbmi->cfl_idx == CFL_DERIVED_ALPHA) {
    alpha_q3 = mbmi->cfl_implicit_alpha[plane - 1];
  } else {
    alpha_q3 =
        cfl_idx_to_alpha(mbmi->cfl_alpha_idx, mbmi->cfl_alpha_signs, plane - 1);
    alpha_q3 *= (1 << CFL_ADD_BITS_ALPHA);
  }

  assert((tx_size_high[tx_size] - 1) * CFL_BUF_LINE + tx_size_wide[tx_size] <=
         CFL_BUF_SQUARE);

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  if (AOMMAX(width, height) > 32) {
    cfl_predict_hbd_c(cfl->ac_buf_q3, dst, dst_stride, alpha_q3, xd->bd, width,
                      height);
  } else
    cfl_get_predict_hbd_fn(tx_size)(cfl->ac_buf_q3, dst, dst_stride, alpha_q3,
                                    xd->bd);
}

static void cfl_luma_subsampling_420_hbd_c(const uint16_t *input,
                                           int input_stride,
                                           uint16_t *output_q3, int width,
                                           int height) {
  for (int j = 0; j < height; j += 2) {
    for (int i = 0; i < width; i += 2) {
      const int bot = i + input_stride;
      output_q3[i >> 1] =
          (input[i] + input[i + 1] + input[bot] + input[bot + 1]) << 1;
    }
    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

void cfl_luma_subsampling_420_hbd_colocated_c(const uint16_t *input,
                                              int input_stride,
                                              uint16_t *output_q3, int width,
                                              int height) {
  for (int j = 0; j < height; j += 2) {
    for (int i = 0; i < width; i += 2) {
      const int top = ((j & 63) == 0) ? i : (i - input_stride);
      const int bot = i + input_stride;
      output_q3[i >> 1] = input[AOMMAX(i & (-64), i - 1)] + 4 * input[i] +
                          input[i + 1] + input[top] + input[bot];
    }
    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

void cfl_luma_subsampling_420_hbd_121_c(const uint16_t *input, int input_stride,
                                        uint16_t *output_q3, int width,
                                        int height) {
  for (int j = 0; j < height; j += 2) {
    for (int i = 0; i < width; i += 2) {
      const int left = AOMMAX(i & (-64), i - 1);
      output_q3[i >> 1] = input[left] + 2 * input[i] + input[i + 1] +
                          input[left + input_stride] +
                          2 * input[i + input_stride] +
                          input[i + input_stride + 1];
    }
    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_422_hbd_c(const uint16_t *input,
                                           int input_stride,
                                           uint16_t *output_q3, int width,
                                           int height) {
  assert((height - 1) * CFL_BUF_LINE + width <= CFL_BUF_SQUARE);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i += 2) {
      output_q3[i >> 1] = (input[i] + input[i + 1]) << 2;
    }
    input += input_stride;
    output_q3 += CFL_BUF_LINE;
  }
}

void cfl_adaptive_luma_subsampling_422_hbd_c(const uint16_t *input,
                                             int input_stride,
                                             uint16_t *output_q3, int width,
                                             int height, int filter_type) {
  assert((height - 1) * CFL_BUF_LINE + width <= CFL_BUF_SQUARE);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i += 2) {
      if (filter_type == 1) {
        output_q3[i >> 1] =
            (input[AOMMAX(i & (-64), i - 1)] + 2 * input[i] + input[i + 1])
            << 1;
      } else if (filter_type == 2) {
        output_q3[i >> 1] = (input[i]) << 3;
      } else {
        output_q3[i >> 1] = (input[i] + input[i + 1]) << 2;
      }
    }
    input += input_stride;
    output_q3 += CFL_BUF_LINE;
  }
}

void cfl_luma_subsampling_444_hbd_c(const uint16_t *input, int input_stride,
                                    uint16_t *output_q3, int width,
                                    int height) {
  assert((height - 1) * CFL_BUF_LINE + width <= CFL_BUF_SQUARE);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      output_q3[i] = input[i] << 3;
    }
    input += input_stride;
    output_q3 += CFL_BUF_LINE;
  }
}

CFL_GET_SUBSAMPLE_FUNCTION(c)

CFL_GET_SUBSAMPLE_121_FUNCTION(c)

CFL_GET_SUBSAMPLE_COLOCATED_FUNCTION(c)

static INLINE cfl_subsample_hbd_fn cfl_subsampling_hbd(TX_SIZE tx_size,
                                                       int sub_x, int sub_y) {
  if (sub_x == 1) {
    if (sub_y == 1) {
      return cfl_get_luma_subsampling_420_hbd(tx_size);
    }
    return cfl_get_luma_subsampling_422_hbd(tx_size);
  }
  return cfl_get_luma_subsampling_444_hbd(tx_size);
}

void cfl_store(MACROBLOCKD *const xd, CFL_CTX *cfl, const uint16_t *input,
               int input_stride, int row, int col, int width, int height,
               int filter_type) {
  const TX_SIZE tx_size = get_tx_size(width, height);
  const int tx_off_log2 = MI_SIZE_LOG2;
  const int sub_x = cfl->subsampling_x;
  const int sub_y = cfl->subsampling_y;
  const int store_row = row << (tx_off_log2 - sub_y);
  const int store_col = col << (tx_off_log2 - sub_x);
  const int store_height = height >> sub_y;
  const int store_width = width >> sub_x;

  // Invalidate current parameters
  cfl->are_parameters_computed = 0;

  // Store the surface of the pixel buffer that was written to, this way we
  // can manage chroma overrun (e.g. when the chroma surfaces goes beyond the
  // frame boundary)
  if (col == 0 && row == 0) {
    cfl->buf_width = store_width;
    cfl->buf_height = store_height;
  } else {
    cfl->buf_width = OD_MAXI(store_col + store_width, cfl->buf_width);
    cfl->buf_height = OD_MAXI(store_row + store_height, cfl->buf_height);
  }

  if (xd->tree_type == CHROMA_PART) {
    const struct macroblockd_plane *const pd = &xd->plane[PLANE_TYPE_UV];
    if (xd->mb_to_right_edge < 0)
      cfl->buf_width += xd->mb_to_right_edge >> (3 + pd->subsampling_x);
    if (xd->mb_to_bottom_edge < 0)
      cfl->buf_height += xd->mb_to_bottom_edge >> (3 + pd->subsampling_y);
  }

  // Check that we will remain inside the pixel buffer.
  assert(store_row + store_height <= CFL_BUF_LINE);
  assert(store_col + store_width <= CFL_BUF_LINE);

  // Store the input into the CfL pixel buffer
  uint16_t *recon_buf_q3 =
      cfl->recon_buf_q3 + (store_row * CFL_BUF_LINE + store_col);
  if (sub_x == 1 && sub_y == 0) {
    cfl_adaptive_luma_subsampling_422_hbd_c(input, input_stride, recon_buf_q3,
                                            width, height, filter_type);
  } else if (sub_x == 0 && sub_y == 0) {
    if (AOMMAX(width, height) > 64) {
      cfl_luma_subsampling_444_hbd_c(input, input_stride, recon_buf_q3, width,
                                     height);
    } else {
      cfl_subsampling_hbd(tx_size, sub_x, sub_y)(input, input_stride,
                                                 recon_buf_q3);
    }
  } else if (filter_type == 1) {
    if (sub_x && sub_y) {
      if (AOMMAX(width, height) > 64) {
        cfl_luma_subsampling_420_hbd_121_c(input, input_stride, recon_buf_q3,
                                           width, height);
      } else {
        cfl_get_luma_subsampling_420_hbd_121(tx_size)(input, input_stride,
                                                      recon_buf_q3);
      }
    } else {
      if (AOMMAX(width, height) > 64) {
        cfl_luma_subsampling_420_hbd_c(input, input_stride, recon_buf_q3, width,
                                       height);
      } else {
        cfl_subsampling_hbd(tx_size, sub_x, sub_y)(input, input_stride,
                                                   recon_buf_q3);
      }
    }
  } else if (filter_type == 2) {
    if (sub_x && sub_y) {
      if (AOMMAX(width, height) > 64) {
        cfl_luma_subsampling_420_hbd_colocated_c(input, input_stride,
                                                 recon_buf_q3, width, height);
      } else {
        cfl_get_luma_subsampling_420_hbd_colocated(tx_size)(input, input_stride,
                                                            recon_buf_q3);
      }
    } else {
      if (AOMMAX(width, height) > 64) {
        cfl_luma_subsampling_420_hbd_c(input, input_stride, recon_buf_q3, width,
                                       height);
      } else {
        cfl_subsampling_hbd(tx_size, sub_x, sub_y)(input, input_stride,
                                                   recon_buf_q3);
      }
    }
  } else {
    if (AOMMAX(width, height) > 64) {
      cfl_luma_subsampling_420_hbd_c(input, input_stride, recon_buf_q3, width,
                                     height);
    } else {
      cfl_subsampling_hbd(tx_size, sub_x, sub_y)(input, input_stride,
                                                 recon_buf_q3);
    }
  }
}

void cfl_store_block(MACROBLOCKD *const xd, BLOCK_SIZE bsize, TX_SIZE tx_size,
                     int filter_type) {
  CFL_CTX *const cfl = &xd->cfl;
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  // Always store full block, even if partially outside frame boundary.
  const int width = block_size_wide[bsize];
  const int height = block_size_high[bsize];
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  const int row_offset = mi_row - xd->mi[0]->chroma_ref_info.mi_row_chroma_base;
  const int col_offset = mi_col - xd->mi[0]->chroma_ref_info.mi_col_chroma_base;

  (void)tx_size;

  cfl_store(xd, cfl, pd->dst.buf, pd->dst.stride, row_offset, col_offset, width,
            height, filter_type);
}

#define NON_LINEAR(V, M, BD) ((V * V + M) >> BD)
// Derives multi-parameter chroma prediction coefficients from neighboring luma
// and chroma reference samples.
void av1_mhccp_derive_multi_param_hv_c(MACROBLOCKD *const xd, int plane,
                                       int above_lines, int left_lines,
                                       int ref_width, int ref_height, int dir,
                                       int is_top_sb_boundary) {
  CFL_CTX *const cfl = &xd->cfl;
  MB_MODE_INFO *mbmi = xd->mi[0];

  int count = 0;

  // Collect reference data to input matrix A and target vector Y
  int16_t A[MHCCP_NUM_PARAMS][MHCCP_MAX_REF_SAMPLES];
  uint16_t YCb[MHCCP_MAX_REF_SAMPLES];
  const int16_t mid = (1 << (xd->bd - 1));

  if (above_lines || left_lines) {
    uint16_t *l = cfl->mhccp_ref_buf_q3[0];
    uint16_t *c = cfl->mhccp_ref_buf_q3[plane];

    int ref_stride = CFL_BUF_LINE * 2;
    for (int j = 1; j < ref_height - 1; ++j) {
      for (int i = 1; i < ref_width - 1; ++i) {
        if ((i >= left_lines && j >= above_lines)) continue;
        int ref_h_offset = 0;
        if (is_top_sb_boundary && above_lines == (LINE_NUM + 1)) {
          if (j < above_lines) {
            ref_h_offset = above_lines - 1 - j;
          }
        }
        // 3-tap cross
        assert(dir >= 0 && dir <= 2);
        if (dir == 0) {
          A[0][count] = (l[i + (j + ref_h_offset) * ref_stride] >> 3);  // C
          A[1][count] = NON_LINEAR(
              (l[i + (j + ref_h_offset) * ref_stride] >> 3), mid, xd->bd);

        } else if (dir == 1) {
          A[0][count] = (l[i + (j + ref_h_offset - 1) * ref_stride] >> 3);  // T
          A[1][count] = NON_LINEAR(
              (l[i + (j + ref_h_offset) * ref_stride] >> 3), mid, xd->bd);
        } else if (dir == 2) {
          A[0][count] =
              (l[(i - 1) + (j + ref_h_offset) * ref_stride] >> 3);  // L
          A[1][count] = NON_LINEAR(
              (l[i + (j + ref_h_offset) * ref_stride] >> 3), mid, xd->bd);
        }
        A[2][count] = mid;
        YCb[count] = c[i + (j + ref_h_offset) * ref_stride];
        ++count;
      }
    }
  }

  if (count > 0) {
#if CONFIG_MHCCP_SOLVER_BITS
    int32_t ATA[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS];
    // One more column is added to store the derived parameters
    int32_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1];
    int32_t Ty[MHCCP_NUM_PARAMS];
    memset(ATA, 0x00,
           sizeof(int32_t) * (MHCCP_NUM_PARAMS) * (MHCCP_NUM_PARAMS));
    memset(Ty, 0x00, sizeof(int32_t) * (MHCCP_NUM_PARAMS));
    memset(C, 0x00, sizeof(C));
#else
    int64_t ATA[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS];
    // One more column is added to store the derived parameters
    int64_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1];
    int64_t Ty[MHCCP_NUM_PARAMS];
    memset(ATA, 0x00,
           sizeof(int64_t) * (MHCCP_NUM_PARAMS) * (MHCCP_NUM_PARAMS));
    memset(Ty, 0x00, sizeof(int64_t) * (MHCCP_NUM_PARAMS));
    memset(C, 0x00, sizeof(C));
#endif  // CONFIG_MHCCP_SOLVER_BITS
    for (int coli0 = 0; coli0 < (MHCCP_NUM_PARAMS); ++coli0) {
      for (int coli1 = coli0; coli1 < (MHCCP_NUM_PARAMS); ++coli1) {
        int16_t *col0 = A[coli0];
        int16_t *col1 = A[coli1];

        for (int rowi = 0; rowi < count; ++rowi) {
          ATA[coli0][coli1] += col0[rowi] * col1[rowi];
        }
      }
    }

    for (int coli = 0; coli < (MHCCP_NUM_PARAMS); ++coli) {
      int16_t *col = A[coli];

      for (int rowi = 0; rowi < count; ++rowi) {
        Ty[coli] += col[rowi] * YCb[rowi];
      }
    }

    // Scale the matrix and vector to selected dynamic range
    int matrixShift =
        (MHCCP_DECIM_BITS + 6) - 2 * xd->bd - (int)ceil(log2(count));

    if (matrixShift > 0) {
      for (int coli0 = 0; coli0 < MHCCP_NUM_PARAMS; coli0++)
        for (int coli1 = coli0; coli1 < MHCCP_NUM_PARAMS; coli1++)
          ATA[coli0][coli1] <<= matrixShift;

      for (int coli = 0; coli < MHCCP_NUM_PARAMS; coli++)
        Ty[coli] <<= matrixShift;
    } else if (matrixShift < 0) {
      matrixShift = -matrixShift;

      for (int coli0 = 0; coli0 < MHCCP_NUM_PARAMS; coli0++)
        for (int coli1 = coli0; coli1 < MHCCP_NUM_PARAMS; coli1++)
          ATA[coli0][coli1] >>= matrixShift;

      for (int coli = 0; coli < MHCCP_NUM_PARAMS; coli++)
        Ty[coli] >>= matrixShift;
    }
    gauss_elimination_mhccp(ATA, C, Ty, mbmi->mhccp_implicit_param[plane - 1],
                            MHCCP_NUM_PARAMS, xd->bd);
  } else {
    for (int i = 0; i < MHCCP_NUM_PARAMS - 1; ++i) {
      mbmi->mhccp_implicit_param[plane - 1][i] = 0;
    }
    mbmi->mhccp_implicit_param[plane - 1][MHCCP_NUM_PARAMS - 1] =
        1 << MHCCP_DECIM_BITS;
  }
}

#define DIV_PREC_BITS 14
#define DIV_PREC_BITS_POW2 8
#define DIV_SLOT_BITS 3
#define DIV_INTR_BITS (DIV_PREC_BITS - DIV_SLOT_BITS)

// Return the number of shifted bits for the denominator
static INLINE int floorLog2Uint64(uint64_t x) {
  if (x == 0) {
    return 0;
  }
  int result = 0;
  if (x & 0xffffffff00000000) {
    x >>= 32;
    result += 32;
  }
  if (x & 0xffff0000) {
    x >>= 16;
    result += 16;
  }
  if (x & 0xff00) {
    x >>= 8;
    result += 8;
  }
  if (x & 0xf0) {
    x >>= 4;
    result += 4;
  }
  if (x & 0xc) {
    x >>= 2;
    result += 2;
  }
  if (x & 0x2) {
    result += 1;
  }
  return result;
}

#if CONFIG_MHCCP_SOLVER_BITS
void get_division_scale_shift(uint32_t denom, int32_t *scale, int32_t *round,
                              int32_t *shift) {
#else
void get_division_scale_shift(uint64_t denom, int *scale, int64_t *round,
                              int *shift) {
#endif  // CONFIG_MHCCP_SOLVER_BITS
  // This array stores the coefficients for the quadratic
  // (squared) term in the polynomial for each of the 8 regions.
  static const int pow2W[DIV_PREC_BITS_POW2] = { 214, 153, 113, 86,
                                                 67,  53,  43,  35 };
  static const int pow2Q[DIV_PREC_BITS_POW2] = { 227, 181, 148, 124,
                                                 104, 89,  77,  68 };
  // This array contains the offset values used to adjust
  //  the normalized denominator for each region.
  static const int pow2O[DIV_PREC_BITS_POW2] = { 1024, 3072,  5120,  7168,
                                                 9216, 11264, 13312, 15360 };

  // This array holds the constant bias term for each region's polynomial.
  static const int pow2B[DIV_PREC_BITS_POW2] = { 15420, 13797, 12483, 11397,
                                                 10485, 9709,  9039,  8456 };

  *shift = floorLog2Uint64(denom);
  assert(*shift < 32);
  if (*shift == 0) {
    *round = 0;
  } else {
#if CONFIG_MHCCP_SOLVER_BITS
    *round = (int32_t)(1U << (*shift) >> 1);
#else
    *round = (int64_t)(1ULL << (*shift) >> 1);
#endif  // CONFIG_MHCCP_SOLVER_BITS
  }
  // Consider the division approximation: y = (x + D/2) / D,
  // where x is the numerator and D is the denominator.
  // We want to approximate it as: y ≈ (x / d) >> s,
  // where d is in the range [1, 2) and s = floor(log2(D)).
  //
  // Step 1: Normalize D into fixed-point format with DIV_PREC_BITS fractional
  // bits.
  //         The expression below computes a scaled version of D:
  //         normDiff_tmp = ((D << DIV_PREC_BITS) + round) >> shift
  //         This ensures fixed-point precision and rounding.
#if CONFIG_MHCCP_SOLVER_BITS
  int delta = *shift - DIV_PREC_BITS;
  int32_t normDiff_tmp;
  if (delta >= 0) {
    // pure right shift delta
    // note: delta==0, bias = 0
    int32_t bias = (delta > 0) ? (1U << (delta - 1)) : 0U;
    normDiff_tmp = (denom + bias) >> delta;
  } else {
    // left shift -delta; at this time (round >> *shift) is 0 and will not
    // affect the result
    int32_t s = -delta;  // = DIV_PREC_BITS - *shift
    normDiff_tmp = denom << s;
  }
#else
  const int normDiff_tmp =
      (int)(((denom << DIV_PREC_BITS) + *round) >> (*shift));
#endif  // CONFIG_MHCCP_SOLVER_BITS

  // Step 2: Clip the scaled value to make sure it's within the valid range.
  //         The valid range is [1, 2), represented as：
  //         [1 << (DIV_PREC_BITS), (1 << (DIV_PREC_BITS + 1)) - 1].
  //         The rounding in Step 1 may push the value out of range, so clipping
  //         is needed.
  const int32_t normDiff_clip =
      CLIP(normDiff_tmp, 1, (1 << (DIV_PREC_BITS + 1)) - 1);

  // Step 3: Extract the fractional part of the normalized denominator `d`.
  //         This is done by masking out the lower DIV_PREC_BITS bits.
  int32_t normDiff = normDiff_clip & ((1 << DIV_PREC_BITS) - 1);

  // The vale of index is ranging from 0 to 7
  int32_t index = normDiff >> DIV_INTR_BITS;
  int32_t normDiff2 = normDiff - pow2O[index];

#if CONFIG_MHCCP_SOLVER_BITS
  *scale = (((pow2W[index] * ((normDiff2 * normDiff2) >> DIV_PREC_BITS)) >>
             DIV_PREC_BITS_POW2) -
            ((pow2Q[index] * normDiff2) >> DIV_PREC_BITS_POW2) + pow2B[index]);
#else
  *scale = (int)((((int64_t)pow2W[index] *
                   ((normDiff2 * normDiff2) >> DIV_PREC_BITS)) >>
                  DIV_PREC_BITS_POW2) -
                 (((int64_t)pow2Q[index] * normDiff2) >> DIV_PREC_BITS_POW2) +
                 (int64_t)pow2B[index]);
#endif  // CONFIG_MHCCP_SOLVER_BITS
  *scale <<= MHCCP_DECIM_BITS - DIV_PREC_BITS;
}

#if CONFIG_MHCCP_SOLVER_BITS
void gauss_back_substitute(int32_t *x,
                           int32_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                           int numEq, int col, int round, int bits) {
  (void)round;
  x[numEq - 1] = C[numEq - 1][col];

  for (int i = numEq - 2; i >= 0; i--) {
    x[i] = C[i][col];

    for (int j = i + 1; j < numEq; j++) {
      x[i] -= mul_fixed32_adapt(C[i][j], x[j], bits);
    }
  }
}
#else
void gauss_back_substitute(int64_t *x,
                           int64_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                           int numEq, int col, int round, int bits) {
  x[numEq - 1] = C[numEq - 1][col];

  for (int i = numEq - 2; i >= 0; i--) {
    x[i] = C[i][col];

    for (int j = i + 1; j < numEq; j++) {
      x[i] -= (int32_t)LOCAL_FIXED_MULT((int64_t)C[i][j], (int64_t)x[j], round,
                                        bits);
    }
  }
}
#endif  // CONFIG_MHCCP_SOLVER_BITS

#if CONFIG_MHCCP_SOLVER_BITS
void gauss_elimination_mhccp(int32_t A[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS],
                             int32_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                             int32_t *y0, int32_t *x0, int numEq, int bd) {
#else
void gauss_elimination_mhccp(int64_t A[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS],
                             int64_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                             int64_t *y0, int64_t *x0, int numEq, int bd) {
#endif  // CONFIG_MHCCP_SOLVER_BITS
  int colChr0 = numEq;

  int reg = 2 << (bd - 8);
  const int decimBits = MHCCP_DECIM_BITS;
  const int decimRound = (1 << (decimBits - 1));
  // Create an [M][M+2] matrix system (could have been done already when
  // calculating auto/cross-correlations)
  for (int i = 0; i < numEq; i++) {
    for (int j = 0; j < numEq; j++) {
      C[i][j] = j >= i ? A[i][j] : A[j][i];
    }

    C[i][i] += reg;  // Regularization
    C[i][colChr0] = y0[i];
  }

  for (int i = 0; i < numEq; i++) {
#if CONFIG_MHCCP_SOLVER_BITS
    int32_t *src = C[i];
    uint32_t diag = abs(src[i]) < 1 ? 1 : abs(src[i]);
    int32_t round;
#else
    int64_t *src = C[i];
    uint64_t diag = llabs(src[i]) < 1 ? 1 : llabs(src[i]);
    int64_t round;
#endif  // CONFIG_MHCCP_SOLVER_BITS
    int32_t scale, shift;
    get_division_scale_shift(diag, &scale, &round, &shift);

    for (int j = i + 1; j < numEq + 1; j++) {
#if CONFIG_MHCCP_SOLVER_BITS
      int32_t tmp = mul_fixed32_adapt(src[j], scale, shift);
      src[j] = tmp;
#else
      src[j] = (src[j] * scale + round) >> shift;
#endif  // CONFIG_MHCCP_SOLVER_BITS
    }

    for (int j = i + 1; j < numEq; j++) {
#if CONFIG_MHCCP_SOLVER_BITS
      int32_t *dst = C[j];
      int32_t scale_factor = dst[i];
#else
      int64_t *dst = C[j];
      int64_t scale_factor = dst[i];
#endif  // CONFIG_MHCCP_SOLVER_BITS

      // On row j all elements with k < i+1 are now zero (not zeroing those here
      // as backsubstitution does not need them)
      for (int k = i + 1; k < numEq + 1; k++) {
#if CONFIG_MHCCP_SOLVER_BITS
        const int32_t delta =
            mul_fixed32_adapt(scale_factor, src[k], decimBits);
        dst[k] -= delta;
#else
        dst[k] -= (int32_t)LOCAL_FIXED_MULT(scale_factor, (int64_t)src[k],
                                            decimRound, decimBits);
#endif  // CONFIG_MHCCP_SOLVER_BITS
      }
    }
  }

  // Solve with backsubstitution
  gauss_back_substitute(x0, C, numEq, colChr0, decimRound, decimBits);
}

#if CONFIG_MHCCP_SOLVER_BITS
static int16_t convolve(int32_t *params, uint16_t *vector, int16_t numParams) {
  int32_t sum = 0;
  const int decimBits = MHCCP_DECIM_BITS;
  for (int i = 0; i < numParams; i++) {
    sum += mul_fixed32_adapt(params[i], vector[i], decimBits);
  }
  return (int16_t)clamp(sum, INT16_MIN, INT16_MAX);
}
#else
static int16_t convolve(int64_t *params, uint16_t *vector, int16_t numParams) {
  int64_t sum = 0;
  const int decimBits = MHCCP_DECIM_BITS;
  const int decimRound = (1 << (decimBits - 1));
  for (int i = 0; i < numParams; i++) {
    sum += LOCAL_FIXED_MULT(params[i], vector[i], decimRound, decimBits);
  }
  return (int16_t)clamp64(sum, INT16_MIN, INT16_MAX);
}
#endif  // CONFIG_MHCCP_SOLVER_BITS

#if CONFIG_MHCCP_SOLVER_BITS
void mhccp_predict_hv_hbd_c(const uint16_t *input, uint16_t *dst, bool have_top,
                            bool have_left, int dst_stride, int32_t *alpha_q3,
                            int bit_depth, int width, int height, int dir) {
#else
void mhccp_predict_hv_hbd_c(const uint16_t *input, uint16_t *dst, bool have_top,
                            bool have_left, int dst_stride, int64_t *alpha_q3,
                            int bit_depth, int width, int height, int dir) {
#endif  // CONFIG_MHCCP_SOLVER_BITS
  const uint16_t mid = (1 << (bit_depth - 1));

  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      uint16_t vector[MHCCP_NUM_PARAMS];
      vector[0] = input[i] >> 3;  // C
      uint16_t a =
          (j - 1 < 0 && !have_top ? input[i] : input[i - CFL_BUF_LINE * 2]) >>
          3;  // above
      uint16_t c =
          (i - 1 < 0 && !have_left ? input[i] : input[i - 1]) >> 3;  // left
      if (dir == 1) {
        vector[0] = a;  // T
      } else if (dir == 2) {
        vector[0] = c;  // L
      }
      vector[1] = NON_LINEAR((input[i] >> 3), mid, bit_depth);
      vector[2] = mid;
      dst[i] = clip_pixel_highbd(convolve(alpha_q3, vector, MHCCP_NUM_PARAMS),
                                 bit_depth);
    }
    dst += dst_stride;
    input += CFL_BUF_LINE * 2;
  }
}
#undef NON_LINEAR
