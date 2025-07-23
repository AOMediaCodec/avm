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

#ifndef AOM_AV1_COMMON_CFL_H_
#define AOM_AV1_COMMON_CFL_H_

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"

#define CFL_ADD_BITS_ALPHA 5

// Linear modeal Y = alpha * X + beta has been used in a few coding tools.
// This function derives parameter alpha. The equation is:
// alpha = (sum_xy - sum_x * sum_y / n) / (sum_xx - sum_x * sum_x / n)
static INLINE int16_t derive_linear_parameters_alpha(int sum_x, int sum_y,
                                                     int sum_xx, int sum_xy,
                                                     int count, int shift) {
  if (count == 0) return 0;
  int32_t der = sum_xx - (int32_t)((int64_t)sum_x * sum_x / count);
  int32_t nor = sum_xy - (int32_t)((int64_t)sum_x * sum_y / count);
  if (der == 0 || nor == 0) return 0;
  const int16_t alpha = resolve_divisor_32_CfL(nor, der, shift);
  return alpha;
}

// This function derives parameter beta, assuming alpha is known/derived.
// beta = Y - alpha * X.
static INLINE int derive_linear_parameters_beta(int sum_x, int sum_y, int count,
                                                int shift, int16_t alpha) {
  if (count == 0) return 0;
  const int beta = ((sum_y << shift) - sum_x * alpha) / count;
  return beta;
}

// Can we use CfL for the current block?
static INLINE CFL_ALLOWED_TYPE is_cfl_allowed(const MACROBLOCKD *xd) {
  const MB_MODE_INFO *mbmi = xd->mi[0];
  if (xd->tree_type == LUMA_PART) return CFL_DISALLOWED;

#if CONFIG_SDP_CFL_LATENCY_FIX
  assert(xd->is_cfl_allowed_in_sdp < CFL_ALLOWED_TYPES_FOR_SDP);
  if (xd->is_cfl_allowed_in_sdp != CFL_ALLOWED_FOR_CHROMA) {
    assert(xd->tree_type == CHROMA_PART);
    if (xd->is_cfl_allowed_in_sdp == CFL_DISALLOWED_FOR_CHROMA)
      return CFL_DISALLOWED;
  }
#endif  // CONFIG_SDP_CFL_LATENCY_FIX

  const BLOCK_SIZE bsize = get_bsize_base(xd, mbmi, AOM_PLANE_U);
  assert(bsize < BLOCK_SIZES_ALL);
  const int ssx = xd->plane[AOM_PLANE_U].subsampling_x;
  const int ssy = xd->plane[AOM_PLANE_U].subsampling_y;
  const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, ssx, ssy);

  // In lossless, CfL is available when the partition size is equal to the
  // transform size.
  if (xd->lossless[mbmi->segment_id]) {
    return (CFL_ALLOWED_TYPE)(plane_bsize == BLOCK_4X4);
  }

#if CONFIG_CFL_64x64
  // Ensure that plane_bsize doesn't go beyond allowed max chroma tx size (which
  // is 32x32 currently as per `av1_get_max_uv_txsize`). Specifically,
  // - For YUV 4:2:0 input, this will imply max luma tx size of 64x64.
  // - For YUV 4:4:4 input, this will imply max luma tx size of 32x32.
  //   This is because, for a YUV444 input, luma tx size of 64x64 must NOT be
  //   allowed for the following reason: when luma (and chroma) coding block
  //   size is 64x64, it will imply uv_tx_size of 32x32 . So, there will be a
  //   mismatch between chroma prediction block size and chroma transform block
  //   size, which is NOT allowed for intra blocks.
  const TX_SIZE max_uv_tx_size = av1_get_max_uv_txsize(bsize, ssx, ssy);
  if (block_size_wide[plane_bsize] > tx_size_wide[max_uv_tx_size] ||
      block_size_high[plane_bsize] > tx_size_high[max_uv_tx_size]) {
    return CFL_DISALLOWED;
  }
#endif  // CONFIG_CFL_64x64

  // CfL is available to luma partitions CFL_BUF_LINE x CFL_BUF_LINE or smaller.
  return (CFL_ALLOWED_TYPE)(block_size_wide[bsize] <= CFL_BUF_LINE &&
                            block_size_high[bsize] <= CFL_BUF_LINE);
}

// Do we need to save the luma pixels from the current block,
// for a possible future CfL prediction?
static INLINE CFL_ALLOWED_TYPE store_cfl_required(const AV1_COMMON *cm,
                                                  const MACROBLOCKD *xd) {
  const MB_MODE_INFO *mbmi = xd->mi[0];

  if (cm->seq_params.monochrome) return CFL_DISALLOWED;

  if (!xd->is_chroma_ref) {
    // CfL is available to luma partitions lesser than or equal to 32x32.
    const BLOCK_SIZE bsize = mbmi->sb_type[0];
    assert(bsize < BLOCK_SIZES_ALL);
    return (CFL_ALLOWED_TYPE)(block_size_wide[bsize] <= CFL_BUF_LINE &&
                              block_size_high[bsize] <= CFL_BUF_LINE);
  }

  // If this block has chroma information, we know whether we're
  // actually going to perform a CfL prediction
  return (CFL_ALLOWED_TYPE)(!is_inter_block(mbmi, xd->tree_type) &&
                            mbmi->uv_mode == UV_CFL_PRED);
}

// Derive multi parameters for MHCCP
void mhccp_derive_multi_param_hv(MACROBLOCKD *const xd, int plane,
                                 int above_lines, int left_lines, int ref_width,
                                 int ref_height, int dir,
                                 int is_top_sb_boundary);
// Apply the back substitution process to generate the MHCCP parameters
void gauss_back_substitute(int64_t *x,
                           int64_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                           int numEq, int col, int round, int bits);
// Use gaussian elimination approach to derive the parameters for MHCCP mode
void gauss_elimination_mhccp(int64_t A[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS],
                             int64_t C[MHCCP_NUM_PARAMS][MHCCP_NUM_PARAMS + 1],
                             int64_t *y0, int64_t *x0, int numEq, int bd);
// Get the number of shifted bits for denominator and the scaling factors
void get_division_scale_shift(uint64_t denom, int *scale, int64_t *round,
                              int *shift);

static INLINE int get_scaled_luma_q0(int alpha_q3, int16_t pred_buf_q3) {
  int scaled_luma_q6 = alpha_q3 * pred_buf_q3;
  return ROUND_POWER_OF_TWO_SIGNED(scaled_luma_q6, (6 + CFL_ADD_BITS_ALPHA));
}

static INLINE CFL_PRED_TYPE get_cfl_pred_type(PLANE_TYPE plane) {
  assert(plane > 0);
  return (CFL_PRED_TYPE)(plane - 1);
}

void cfl_predict_block(MACROBLOCKD *const xd, uint16_t *dst, int dst_stride,
                       TX_SIZE tx_size, int plane, bool have_top,
                       bool have_left, int above_lines, int left_lines);

void cfl_store_block(MACROBLOCKD *const xd, BLOCK_SIZE bsize, TX_SIZE tx_size,
                     int filter_type);

void cfl_store(MACROBLOCKD *const xd, CFL_CTX *cfl, const uint16_t *input,
               int input_stride, int row, int col, TX_SIZE tx_size,
               int filter_type);

void cfl_luma_subsampling_420_hbd_colocated(const uint16_t *input,
                                            int input_stride,
                                            uint16_t *output_q3, int width,
                                            int height);

void cfl_adaptive_luma_subsampling_422_hbd_c(const uint16_t *input,
                                             int input_stride,
                                             uint16_t *output_q3, int width,
                                             int height, int filter_type);

// Get neighbor luma reconstruction pixels
void cfl_implicit_fetch_neighbor_luma(const AV1_COMMON *cm,
                                      MACROBLOCKD *const xd, int row, int col,
#if CONFIG_CFL_SIMPLIFICATION
                                      int is_top_sb_boundary,
#endif  // CONFIG_CFL_SIMPLIFICATION
                                      TX_SIZE tx_size);

// Calculate luma DC
void cfl_calc_luma_dc(MACROBLOCKD *const xd, int row, int col, TX_SIZE tx_size);

// Get neighbor chroma reconstruction pixels
void cfl_implicit_fetch_neighbor_chroma(const AV1_COMMON *cm,
                                        MACROBLOCKD *const xd, int plane,
                                        int row, int col, TX_SIZE tx_size);

// Derive the implicit scaling factor
void cfl_derive_implicit_scaling_factor(MACROBLOCKD *const xd, int plane,
                                        int row, int col, TX_SIZE tx_size);

// Derive the implicit scaling factor for the block
void cfl_derive_block_implicit_scaling_factor(uint16_t *l, const uint16_t *c,
                                              const int width, const int height,
                                              const int stride,
                                              const int chroma_stride,
                                              int *alpha);
// 121 subsample filter
void cfl_luma_subsampling_420_hbd_121_c(const uint16_t *input, int input_stride,
                                        uint16_t *output_q3, int width,
                                        int height);

void cfl_store_dc_pred(MACROBLOCKD *const xd, const uint16_t *input,
                       CFL_PRED_TYPE pred_plane, int width);

void cfl_load_dc_pred(MACROBLOCKD *const xd, uint16_t *dst, int dst_stride,
                      TX_SIZE tx_size, CFL_PRED_TYPE pred_plane);

// Allows the CFL_SUBSAMPLE function to switch types depending on the bitdepth.
#define CFL_lbd_TYPE uint8_t *cfl_type
#define CFL_hbd_TYPE uint16_t *cfl_type

// Declare a size-specific wrapper for the size-generic function. The compiler
// will inline the size generic function in here, the advantage is that the size
// will be constant allowing for loop unrolling and other constant propagated
// goodness.
#define CFL_SUBSAMPLE(arch, sub, bd, width, height)                       \
  void cfl_subsample_##bd##_##sub##_##width##x##height##_##arch(          \
      const CFL_##bd##_TYPE, int input_stride, uint16_t *output_q3) {     \
    cfl_luma_subsampling_##sub##_##bd##_##arch(cfl_type, input_stride,    \
                                               output_q3, width, height); \
  }

// Declare size-specific wrappers for all valid CfL sizes.
#if CONFIG_CFL_64x64
#define CFL_SUBSAMPLE_FUNCTIONS(arch, sub, bd)                            \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 4)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 8)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 16)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 32)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 8)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 4)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 16)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 8)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 32)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 16)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 16)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 4)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 32)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 8)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 32)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 4)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 64, 64)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 64)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 64)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 64)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 64)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 64, 32)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 64, 16)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 64, 8)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 64, 4)                                     \
  cfl_subsample_##bd##_fn cfl_get_luma_subsampling_##sub##_##bd##_##arch( \
      TX_SIZE tx_size) {                                                  \
    CFL_SUBSAMPLE_FUNCTION_ARRAY(arch, sub, bd)                           \
    return subfn_##sub[tx_size];                                          \
  }
#else
#define CFL_SUBSAMPLE_FUNCTIONS(arch, sub, bd)                            \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 4)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 8)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 16)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 32)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 8)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 4)                                      \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 16)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 8)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 32)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 16)                                    \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 16)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 16, 4)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 8, 32)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 8)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 4, 32)                                     \
  CFL_SUBSAMPLE(arch, sub, bd, 32, 4)                                     \
  cfl_subsample_##bd##_fn cfl_get_luma_subsampling_##sub##_##bd##_##arch( \
      TX_SIZE tx_size) {                                                  \
    CFL_SUBSAMPLE_FUNCTION_ARRAY(arch, sub, bd)                           \
    return subfn_##sub[tx_size];                                          \
  }
#endif  // CONFIG_CFL_64x64

// Declare an architecture-specific array of function pointers for size-specific
// wrappers.
#if CONFIG_CFL_64x64
#define CFL_SUBSAMPLE_FUNCTION_ARRAY(arch, sub, bd)                  \
  static const cfl_subsample_##bd##_fn subfn_##sub[TX_SIZES_ALL] = { \
    cfl_subsample_##bd##_##sub##_4x4_##arch,   /* 4x4 */             \
    cfl_subsample_##bd##_##sub##_8x8_##arch,   /* 8x8 */             \
    cfl_subsample_##bd##_##sub##_16x16_##arch, /* 16x16 */           \
    cfl_subsample_##bd##_##sub##_32x32_##arch, /* 32x32 */           \
    cfl_subsample_##bd##_##sub##_64x64_##arch, /* 64x64 */           \
    cfl_subsample_##bd##_##sub##_4x8_##arch,   /* 4x8 */             \
    cfl_subsample_##bd##_##sub##_8x4_##arch,   /* 8x4 */             \
    cfl_subsample_##bd##_##sub##_8x16_##arch,  /* 8x16 */            \
    cfl_subsample_##bd##_##sub##_16x8_##arch,  /* 16x8 */            \
    cfl_subsample_##bd##_##sub##_16x32_##arch, /* 16x32 */           \
    cfl_subsample_##bd##_##sub##_32x16_##arch, /* 32x16 */           \
    cfl_subsample_##bd##_##sub##_32x64_##arch, /* 32x64 */           \
    cfl_subsample_##bd##_##sub##_64x32_##arch, /* 64x32 */           \
    cfl_subsample_##bd##_##sub##_4x16_##arch,  /* 4x16  */           \
    cfl_subsample_##bd##_##sub##_16x4_##arch,  /* 16x4  */           \
    cfl_subsample_##bd##_##sub##_8x32_##arch,  /* 8x32  */           \
    cfl_subsample_##bd##_##sub##_32x8_##arch,  /* 32x8  */           \
    cfl_subsample_##bd##_##sub##_16x64_##arch, /* 16x64 */           \
    cfl_subsample_##bd##_##sub##_64x16_##arch, /* 64x16 */           \
    cfl_subsample_##bd##_##sub##_4x32_##arch,  /* 4x32 */            \
    cfl_subsample_##bd##_##sub##_32x4_##arch,  /* 32x4 */            \
    cfl_subsample_##bd##_##sub##_8x64_##arch,  /* 8x64 */            \
    cfl_subsample_##bd##_##sub##_64x8_##arch,  /* 64x8 */            \
    cfl_subsample_##bd##_##sub##_4x64_##arch,  /* 4x64 */            \
    cfl_subsample_##bd##_##sub##_64x4_##arch,  /* 64x4 */            \
  };
#else
#define CFL_SUBSAMPLE_FUNCTION_ARRAY(arch, sub, bd)                           \
  static const cfl_subsample_##bd##_fn subfn_##sub[TX_SIZES_ALL] = {          \
    cfl_subsample_##bd##_##sub##_4x4_##arch,   /* 4x4 */                      \
    cfl_subsample_##bd##_##sub##_8x8_##arch,   /* 8x8 */                      \
    cfl_subsample_##bd##_##sub##_16x16_##arch, /* 16x16 */                    \
    cfl_subsample_##bd##_##sub##_32x32_##arch, /* 32x32 */                    \
    NULL,                                      /* 64x64 (invalid CFL size) */ \
    cfl_subsample_##bd##_##sub##_4x8_##arch,   /* 4x8 */                      \
    cfl_subsample_##bd##_##sub##_8x4_##arch,   /* 8x4 */                      \
    cfl_subsample_##bd##_##sub##_8x16_##arch,  /* 8x16 */                     \
    cfl_subsample_##bd##_##sub##_16x8_##arch,  /* 16x8 */                     \
    cfl_subsample_##bd##_##sub##_16x32_##arch, /* 16x32 */                    \
    cfl_subsample_##bd##_##sub##_32x16_##arch, /* 32x16 */                    \
    NULL,                                      /* 32x64 (invalid CFL size) */ \
    NULL,                                      /* 64x32 (invalid CFL size) */ \
    cfl_subsample_##bd##_##sub##_4x16_##arch,  /* 4x16  */                    \
    cfl_subsample_##bd##_##sub##_16x4_##arch,  /* 16x4  */                    \
    cfl_subsample_##bd##_##sub##_8x32_##arch,  /* 8x32  */                    \
    cfl_subsample_##bd##_##sub##_32x8_##arch,  /* 32x8  */                    \
    NULL,                                      /* 16x64 (invalid CFL size) */ \
    NULL,                                      /* 64x16 (invalid CFL size) */ \
    cfl_subsample_##bd##_##sub##_4x32_##arch,  /* 4x32 */                     \
    cfl_subsample_##bd##_##sub##_32x4_##arch,  /* 32x4 */                     \
    NULL,                                      /* 8x64 (invalid CFL size) */  \
    NULL,                                      /* 64x8 (invalid CFL size) */  \
    NULL,                                      /* 4x64 (invalid CFL size) */  \
    NULL,                                      /* 64x4 (invalid CFL size) */  \
  };
#endif  // CONFIG_CFL_64x64

// The RTCD script does not support passing in an array, so we wrap it in this
// function.
#define CFL_GET_SUBSAMPLE_FUNCTION(arch)  \
  CFL_SUBSAMPLE_FUNCTIONS(arch, 420, hbd) \
  CFL_SUBSAMPLE_FUNCTIONS(arch, 422, hbd) \
  CFL_SUBSAMPLE_FUNCTIONS(arch, 444, hbd)

// Declare a size-specific wrapper for the size-generic function. The compiler
// will inline the size generic function in here, the advantage is that the size
// will be constant allowing for loop unrolling and other constant propagated
// goodness.
#define CFL_SUB_AVG_X(arch, width, height, round_offset, num_pel_log2)       \
  void cfl_subtract_average_##width##x##height##_##arch(const uint16_t *src, \
                                                        int16_t *dst) {      \
    subtract_average_##arch(src, dst, width, height, round_offset,           \
                            num_pel_log2);                                   \
  }

// Declare size-specific wrappers for all valid CfL sizes.
#if CONFIG_CFL_64x64
#define CFL_SUB_AVG_FN(arch)                                              \
  CFL_SUB_AVG_X(arch, 4, 4, 8, 4)                                         \
  CFL_SUB_AVG_X(arch, 4, 8, 16, 5)                                        \
  CFL_SUB_AVG_X(arch, 4, 16, 32, 6)                                       \
  CFL_SUB_AVG_X(arch, 4, 32, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 8, 4, 16, 5)                                        \
  CFL_SUB_AVG_X(arch, 8, 8, 32, 6)                                        \
  CFL_SUB_AVG_X(arch, 8, 16, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 8, 32, 128, 8)                                      \
  CFL_SUB_AVG_X(arch, 16, 4, 32, 6)                                       \
  CFL_SUB_AVG_X(arch, 16, 8, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 16, 16, 128, 8)                                     \
  CFL_SUB_AVG_X(arch, 16, 32, 256, 9)                                     \
  CFL_SUB_AVG_X(arch, 32, 4, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 32, 8, 128, 8)                                      \
  CFL_SUB_AVG_X(arch, 32, 16, 256, 9)                                     \
  CFL_SUB_AVG_X(arch, 32, 32, 512, 10)                                    \
  CFL_SUB_AVG_X(arch, 64, 64, 2048, 12)                                   \
  CFL_SUB_AVG_X(arch, 64, 32, 1024, 11)                                   \
  CFL_SUB_AVG_X(arch, 64, 16, 512, 10)                                    \
  CFL_SUB_AVG_X(arch, 64, 8, 256, 9)                                      \
  CFL_SUB_AVG_X(arch, 64, 4, 128, 8)                                      \
  CFL_SUB_AVG_X(arch, 32, 64, 1024, 11)                                   \
  CFL_SUB_AVG_X(arch, 16, 64, 512, 10)                                    \
  CFL_SUB_AVG_X(arch, 8, 64, 256, 9)                                      \
  CFL_SUB_AVG_X(arch, 4, 64, 128, 8)                                      \
  cfl_subtract_average_fn cfl_get_subtract_average_fn_##arch(             \
      TX_SIZE tx_size) {                                                  \
    static const cfl_subtract_average_fn sub_avg[TX_SIZES_ALL] = {        \
      cfl_subtract_average_4x4_##arch,   /* 4x4 */                        \
      cfl_subtract_average_8x8_##arch,   /* 8x8 */                        \
      cfl_subtract_average_16x16_##arch, /* 16x16 */                      \
      cfl_subtract_average_32x32_##arch, /* 32x32 */                      \
      cfl_subtract_average_64x64_##arch, /* 64x64 */                      \
      cfl_subtract_average_4x8_##arch,   /* 4x8 */                        \
      cfl_subtract_average_8x4_##arch,   /* 8x4 */                        \
      cfl_subtract_average_8x16_##arch,  /* 8x16 */                       \
      cfl_subtract_average_16x8_##arch,  /* 16x8 */                       \
      cfl_subtract_average_16x32_##arch, /* 16x32 */                      \
      cfl_subtract_average_32x16_##arch, /* 32x16 */                      \
      cfl_subtract_average_32x64_##arch, /* 32x64 */                      \
      cfl_subtract_average_64x32_##arch, /* 64x32 */                      \
      cfl_subtract_average_4x16_##arch,  /* 4x16 */                       \
      cfl_subtract_average_16x4_##arch,  /* 16x4 */                       \
      cfl_subtract_average_8x32_##arch,  /* 8x32 */                       \
      cfl_subtract_average_32x8_##arch,  /* 32x8 */                       \
      cfl_subtract_average_16x64_##arch, /* 16x64 */                      \
      cfl_subtract_average_64x16_##arch, /* 64x16 */                      \
      cfl_subtract_average_4x32_##arch,  /* 4x32 */                       \
      cfl_subtract_average_32x4_##arch,  /* 32x4 */                       \
      cfl_subtract_average_8x64_##arch,  /* 8x64 */                       \
      cfl_subtract_average_64x8_##arch,  /* 64x8 */                       \
      cfl_subtract_average_4x64_##arch,  /* 4x64 */                       \
      cfl_subtract_average_64x4_##arch,  /* 64x4 */                       \
    };                                                                    \
    /* Modulo TX_SIZES_ALL to ensure that an attacker won't be able to */ \
    /* index the function pointer array out of bounds. */                 \
    return sub_avg[tx_size % TX_SIZES_ALL];                               \
  }
#else
#define CFL_SUB_AVG_FN(arch)                                              \
  CFL_SUB_AVG_X(arch, 4, 4, 8, 4)                                         \
  CFL_SUB_AVG_X(arch, 4, 8, 16, 5)                                        \
  CFL_SUB_AVG_X(arch, 4, 16, 32, 6)                                       \
  CFL_SUB_AVG_X(arch, 4, 32, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 8, 4, 16, 5)                                        \
  CFL_SUB_AVG_X(arch, 8, 8, 32, 6)                                        \
  CFL_SUB_AVG_X(arch, 8, 16, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 8, 32, 128, 8)                                      \
  CFL_SUB_AVG_X(arch, 16, 4, 32, 6)                                       \
  CFL_SUB_AVG_X(arch, 16, 8, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 16, 16, 128, 8)                                     \
  CFL_SUB_AVG_X(arch, 16, 32, 256, 9)                                     \
  CFL_SUB_AVG_X(arch, 32, 4, 64, 7)                                       \
  CFL_SUB_AVG_X(arch, 32, 8, 128, 8)                                      \
  CFL_SUB_AVG_X(arch, 32, 16, 256, 9)                                     \
  CFL_SUB_AVG_X(arch, 32, 32, 512, 10)                                    \
  cfl_subtract_average_fn cfl_get_subtract_average_fn_##arch(             \
      TX_SIZE tx_size) {                                                  \
    static const cfl_subtract_average_fn sub_avg[TX_SIZES_ALL] = {        \
      cfl_subtract_average_4x4_##arch,   /* 4x4 */                        \
      cfl_subtract_average_8x8_##arch,   /* 8x8 */                        \
      cfl_subtract_average_16x16_##arch, /* 16x16 */                      \
      cfl_subtract_average_32x32_##arch, /* 32x32 */                      \
      NULL,                              /* 64x64 (invalid CFL size) */   \
      cfl_subtract_average_4x8_##arch,   /* 4x8 */                        \
      cfl_subtract_average_8x4_##arch,   /* 8x4 */                        \
      cfl_subtract_average_8x16_##arch,  /* 8x16 */                       \
      cfl_subtract_average_16x8_##arch,  /* 16x8 */                       \
      cfl_subtract_average_16x32_##arch, /* 16x32 */                      \
      cfl_subtract_average_32x16_##arch, /* 32x16 */                      \
      NULL,                              /* 32x64 (invalid CFL size) */   \
      NULL,                              /* 64x32 (invalid CFL size) */   \
      cfl_subtract_average_4x16_##arch,  /* 4x16 */                       \
      cfl_subtract_average_16x4_##arch,  /* 16x4 */                       \
      cfl_subtract_average_8x32_##arch,  /* 8x32 */                       \
      cfl_subtract_average_32x8_##arch,  /* 32x8 */                       \
      NULL,                              /* 16x64 (invalid CFL size) */   \
      NULL,                              /* 64x16 (invalid CFL size) */   \
      cfl_subtract_average_4x32_##arch,  /* 4x32 */                       \
      cfl_subtract_average_32x4_##arch,  /* 32x4 */                       \
      NULL,                              /* 8x64 (invalid CFL size) */    \
      NULL,                              /* 64x8 (invalid CFL size) */    \
      NULL,                              /* 4x64 (invalid CFL size) */    \
      NULL,                              /* 64x4 (invalid CFL size) */    \
    };                                                                    \
    /* Modulo TX_SIZES_ALL to ensure that an attacker won't be able to */ \
    /* index the function pointer array out of bounds. */                 \
    return sub_avg[tx_size % TX_SIZES_ALL];                               \
  }
#endif  // CONFIG_CFL_64x64

// For VSX SIMD optimization, the C versions of width == 4 subtract are
// faster than the VSX. As such, the VSX code calls the C versions.
void cfl_subtract_average_4x4_c(const uint16_t *src, int16_t *dst);
void cfl_subtract_average_4x8_c(const uint16_t *src, int16_t *dst);
void cfl_subtract_average_4x16_c(const uint16_t *src, int16_t *dst);

#define CFL_PREDICT_hbd(arch, width, height)                                   \
  void cfl_predict_hbd_##width##x##height##_##arch(                            \
      const int16_t *pred_buf_q3, uint16_t *dst, int dst_stride, int alpha_q3, \
      int bd) {                                                                \
    cfl_predict_hbd_##arch(pred_buf_q3, dst, dst_stride, alpha_q3, bd, width,  \
                           height);                                            \
  }

// This wrapper exists because clang format does not like calling macros with
// lowercase letters.
#define CFL_PREDICT_X(arch, width, height, bd) \
  CFL_PREDICT_##bd(arch, width, height)

#if CONFIG_CFL_64x64
#define CFL_PREDICT_FN(arch, bd)                                            \
  CFL_PREDICT_X(arch, 4, 4, bd)                                             \
  CFL_PREDICT_X(arch, 4, 8, bd)                                             \
  CFL_PREDICT_X(arch, 4, 16, bd)                                            \
  CFL_PREDICT_X(arch, 4, 32, bd)                                            \
  CFL_PREDICT_X(arch, 8, 4, bd)                                             \
  CFL_PREDICT_X(arch, 8, 8, bd)                                             \
  CFL_PREDICT_X(arch, 8, 16, bd)                                            \
  CFL_PREDICT_X(arch, 8, 32, bd)                                            \
  CFL_PREDICT_X(arch, 16, 4, bd)                                            \
  CFL_PREDICT_X(arch, 16, 8, bd)                                            \
  CFL_PREDICT_X(arch, 16, 16, bd)                                           \
  CFL_PREDICT_X(arch, 16, 32, bd)                                           \
  CFL_PREDICT_X(arch, 32, 4, bd)                                            \
  CFL_PREDICT_X(arch, 32, 8, bd)                                            \
  CFL_PREDICT_X(arch, 32, 16, bd)                                           \
  CFL_PREDICT_X(arch, 32, 32, bd)                                           \
  CFL_PREDICT_X(arch, 64, 64, bd)                                           \
  CFL_PREDICT_X(arch, 64, 32, bd)                                           \
  CFL_PREDICT_X(arch, 64, 16, bd)                                           \
  CFL_PREDICT_X(arch, 64, 8, bd)                                            \
  CFL_PREDICT_X(arch, 64, 4, bd)                                            \
  CFL_PREDICT_X(arch, 32, 64, bd)                                           \
  CFL_PREDICT_X(arch, 16, 64, bd)                                           \
  CFL_PREDICT_X(arch, 8, 64, bd)                                            \
  CFL_PREDICT_X(arch, 4, 64, bd)                                            \
  cfl_predict_##bd##_fn cfl_get_predict_##bd##_fn_##arch(TX_SIZE tx_size) { \
    static const cfl_predict_##bd##_fn pred[TX_SIZES_ALL] = {               \
      cfl_predict_##bd##_4x4_##arch,   /* 4x4 */                            \
      cfl_predict_##bd##_8x8_##arch,   /* 8x8 */                            \
      cfl_predict_##bd##_16x16_##arch, /* 16x16 */                          \
      cfl_predict_##bd##_32x32_##arch, /* 32x32 */                          \
      cfl_predict_##bd##_64x64_##arch, /* 64x64 */                          \
      cfl_predict_##bd##_4x8_##arch,   /* 4x8 */                            \
      cfl_predict_##bd##_8x4_##arch,   /* 8x4 */                            \
      cfl_predict_##bd##_8x16_##arch,  /* 8x16 */                           \
      cfl_predict_##bd##_16x8_##arch,  /* 16x8 */                           \
      cfl_predict_##bd##_16x32_##arch, /* 16x32 */                          \
      cfl_predict_##bd##_32x16_##arch, /* 32x16 */                          \
      cfl_predict_##bd##_32x64_##arch, /* 32x64 */                          \
      cfl_predict_##bd##_64x32_##arch, /* 64x32 */                          \
      cfl_predict_##bd##_4x16_##arch,  /* 4x16  */                          \
      cfl_predict_##bd##_16x4_##arch,  /* 16x4  */                          \
      cfl_predict_##bd##_8x32_##arch,  /* 8x32  */                          \
      cfl_predict_##bd##_32x8_##arch,  /* 32x8  */                          \
      cfl_predict_##bd##_16x64_##arch, /* 16x64 */                          \
      cfl_predict_##bd##_64x16_##arch, /* 64x16 */                          \
      cfl_predict_##bd##_4x32_##arch,  /* 4x32 */                           \
      cfl_predict_##bd##_32x4_##arch,  /* 32x4 */                           \
      cfl_predict_##bd##_8x64_##arch,  /* 8x64 */                           \
      cfl_predict_##bd##_64x8_##arch,  /* 64x8 */                           \
      cfl_predict_##bd##_4x64_##arch,  /* 4x64 */                           \
      cfl_predict_##bd##_64x4_##arch,  /* 64x4 */                           \
    };                                                                      \
    /* Modulo TX_SIZES_ALL to ensure that an attacker won't be able to */   \
    /* index the function pointer array out of bounds. */                   \
    return pred[tx_size % TX_SIZES_ALL];                                    \
  }
#else
#define CFL_PREDICT_FN(arch, bd)                                            \
  CFL_PREDICT_X(arch, 4, 4, bd)                                             \
  CFL_PREDICT_X(arch, 4, 8, bd)                                             \
  CFL_PREDICT_X(arch, 4, 16, bd)                                            \
  CFL_PREDICT_X(arch, 4, 32, bd)                                            \
  CFL_PREDICT_X(arch, 8, 4, bd)                                             \
  CFL_PREDICT_X(arch, 8, 8, bd)                                             \
  CFL_PREDICT_X(arch, 8, 16, bd)                                            \
  CFL_PREDICT_X(arch, 8, 32, bd)                                            \
  CFL_PREDICT_X(arch, 16, 4, bd)                                            \
  CFL_PREDICT_X(arch, 16, 8, bd)                                            \
  CFL_PREDICT_X(arch, 16, 16, bd)                                           \
  CFL_PREDICT_X(arch, 16, 32, bd)                                           \
  CFL_PREDICT_X(arch, 32, 4, bd)                                            \
  CFL_PREDICT_X(arch, 32, 8, bd)                                            \
  CFL_PREDICT_X(arch, 32, 16, bd)                                           \
  CFL_PREDICT_X(arch, 32, 32, bd)                                           \
  cfl_predict_##bd##_fn cfl_get_predict_##bd##_fn_##arch(TX_SIZE tx_size) { \
    static const cfl_predict_##bd##_fn pred[TX_SIZES_ALL] = {               \
      cfl_predict_##bd##_4x4_##arch,   /* 4x4 */                            \
      cfl_predict_##bd##_8x8_##arch,   /* 8x8 */                            \
      cfl_predict_##bd##_16x16_##arch, /* 16x16 */                          \
      cfl_predict_##bd##_32x32_##arch, /* 32x32 */                          \
      NULL,                            /* 64x64 (invalid CFL size) */       \
      cfl_predict_##bd##_4x8_##arch,   /* 4x8 */                            \
      cfl_predict_##bd##_8x4_##arch,   /* 8x4 */                            \
      cfl_predict_##bd##_8x16_##arch,  /* 8x16 */                           \
      cfl_predict_##bd##_16x8_##arch,  /* 16x8 */                           \
      cfl_predict_##bd##_16x32_##arch, /* 16x32 */                          \
      cfl_predict_##bd##_32x16_##arch, /* 32x16 */                          \
      NULL,                            /* 32x64 (invalid CFL size) */       \
      NULL,                            /* 64x32 (invalid CFL size) */       \
      cfl_predict_##bd##_4x16_##arch,  /* 4x16  */                          \
      cfl_predict_##bd##_16x4_##arch,  /* 16x4  */                          \
      cfl_predict_##bd##_8x32_##arch,  /* 8x32  */                          \
      cfl_predict_##bd##_32x8_##arch,  /* 32x8  */                          \
      NULL,                            /* 16x64 (invalid CFL size) */       \
      NULL,                            /* 64x16 (invalid CFL size) */       \
      cfl_predict_##bd##_4x32_##arch,  /* 4x32  */                          \
      cfl_predict_##bd##_32x4_##arch,  /* 32x4  */                          \
      NULL,                            /* 8x64 (invalid CFL size) */        \
      NULL,                            /* 64x8 (invalid CFL size) */        \
      NULL,                            /* 4x64 (invalid CFL size) */        \
      NULL,                            /* 64x4 (invalid CFL size) */        \
    };                                                                      \
    /* Modulo TX_SIZES_ALL to ensure that an attacker won't be able to */   \
    /* index the function pointer array out of bounds. */                   \
    return pred[tx_size % TX_SIZES_ALL];                                    \
  }
#endif  // CONFIG_CFL_64x64

#endif  // AOM_AV1_COMMON_CFL_H_
