##
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
sub av1_common_forward_decls() {
print <<EOF
/*
 * AV1
 */

#include "aom/aom_integer.h"
#include "aom_dsp/txfm_common.h"
#include "av1/common/common.h"
#include "av1/common/enums.h"
#include "av1/common/quant_common.h"
#include "av1/common/filter.h"
#include "av1/common/convolve.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/odintrin.h"
#include "av1/common/restoration.h"

struct macroblockd;

/* Encoder forward decls */
struct macroblock;
struct txfm_param;
struct aom_variance_vtable;
struct search_site_config;
struct yv12_buffer_config;
struct NN_CONFIG;
typedef struct NN_CONFIG NN_CONFIG;
struct tcq_node_t;
struct tcq_ctx_t;
struct tcq_lf_ctx_t;
struct prequant_t;
struct tcq_rate_t;
struct tcq_coeff_ctx_t;
struct tcq_param_t;
struct LV_MAP_COEFF_COST;

enum { NONE, RELU, SOFTSIGN, SIGMOID } UENUM1BYTE(ACTIVATION);
#if CONFIG_NN_V2
enum { SOFTMAX_CROSS_ENTROPY } UENUM1BYTE(LOSS);
struct NN_CONFIG_V2;
typedef struct NN_CONFIG_V2 NN_CONFIG_V2;
struct FC_LAYER;
typedef struct FC_LAYER FC_LAYER;
#endif  // CONFIG_NN_V2

struct CNN_CONFIG;
typedef struct CNN_CONFIG CNN_CONFIG;
struct CNN_LAYER_CONFIG;
typedef struct CNN_LAYER_CONFIG CNN_LAYER_CONFIG;
struct CNN_THREAD_DATA;
typedef struct CNN_THREAD_DATA CNN_THREAD_DATA;
struct CNN_BRANCH_CONFIG;
typedef struct CNN_BRANCH_CONFIG CNN_BRANCH_CONFIG;
struct CNN_MULTI_OUT;
typedef struct CNN_MULTI_OUT CNN_MULTI_OUT;

/* Function pointers return by CfL functions */
typedef void (*cfl_subsample_hbd_fn)(const uint16_t *input, int input_stride,
                                     uint16_t *output_q3);

typedef void (*cfl_predict_hbd_fn)(const int16_t *src, uint16_t *dst,
                                   int dst_stride, int alpha_q3, int bd);

typedef void (*cfl_subtract_average_fn)(const uint16_t *src, int16_t *dst);

EOF
}
forward_decls qw/av1_common_forward_decls/;

# functions that are 64 bit only.
$mmx_x86_64 = $sse2_x86_64 = $ssse3_x86_64 = $avx_x86_64 = $avx2_x86_64 = '';
if ($opts{arch} eq "x86_64") {
  $mmx_x86_64 = 'mmx';
  $sse2_x86_64 = 'sse2';
  $ssse3_x86_64 = 'ssse3';
  $avx_x86_64 = 'avx';
  $avx2_x86_64 = 'avx2';
}

add_proto qw/void av1_highbd_convolve_horiz_rs/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const int16_t *x_filters, int x0_qn, int x_step_qn, int bd";
specialize qw/av1_highbd_convolve_horiz_rs sse4_1/;

add_proto qw/void av1_highbd_wiener_convolve_add_src/, "const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h, const WienerConvolveParams *conv_params, int bd";
specialize qw/av1_highbd_wiener_convolve_add_src ssse3 avx2/;

# pc wiener filter
add_proto qw/void av1_fill_tskip_sum_buffer/, "int row, const uint8_t *tskip, int tskip_stride, int8_t *tskip_sum_buffer, int width, int height, int tskip_lead, int tskip_lag, bool use_strict_bounds";
specialize qw/av1_fill_tskip_sum_buffer avx2/;
add_proto qw/void fill_directional_feature_buffers_highbd/, "int *feature_sum_buffers[], int16_t *feature_line_buffers[], int row, int buffer_row, const uint16_t *dgd, int dgd_stride, int width, int feature_lead, int feature_lag";
specialize qw/fill_directional_feature_buffers_highbd avx2/;
add_proto qw/void av1_fill_directional_feature_accumulators/, "int dir_feature_accum[NUM_PC_WIENER_FEATURES][PC_WIENER_FEATURE_ACC_SIZE], int *feature_sum_buff[NUM_PC_WIENER_FEATURES], int width, int col_offset, int feature_lead, int feature_lag";
specialize qw/av1_fill_directional_feature_accumulators avx2/;
add_proto qw/void av1_fill_tskip_feature_accumulator/, "int16_t tskip_feature_accum[PC_WIENER_FEATURE_ACC_SIZE], int8_t* tskip_sum_buff, int width, int col_offset,int tskip_lead, int tskip_lag";
specialize qw/av1_fill_tskip_feature_accumulator avx2/;

# Non-separable Wiener filter
add_proto qw/void av1_convolve_symmetric_highbd/, "const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config, const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin, int block_row_end, int block_col_begin, int block_col_end";
specialize qw/av1_convolve_symmetric_highbd avx2/;
add_proto qw/void av1_convolve_symmetric_subtract_center_highbd/, "const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config, const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin, int block_row_end, int block_col_begin, int block_col_end";
specialize qw/av1_convolve_symmetric_subtract_center_highbd avx2/;
add_proto qw/void av1_convolve_symmetric_dual_highbd/, "const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual, int dgd_dual_stride, const NonsepFilterConfig *filter_config, const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin, int block_row_end, int block_col_begin, int block_col_end";
specialize qw/av1_convolve_symmetric_dual_highbd avx2/;
add_proto qw/void av1_convolve_symmetric_dual_subtract_center_highbd/, "const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual, int dgd_dual_stride, const NonsepFilterConfig *filter_config, const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin, int block_row_end, int block_col_begin, int block_col_end";
specialize qw/av1_convolve_symmetric_dual_subtract_center_highbd avx2/;
add_proto qw/void av1_convolve_mixedsymmetric_highbd/, "const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config, const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin, int block_row_end, int block_col_begin, int block_col_end";
specialize qw/av1_convolve_mixedsymmetric_highbd avx2/;

# optical flow interpolation function
add_proto qw/void av1_bicubic_grad_interpolation_highbd/, "const int16_t *pred_src, int16_t *x_grad, int16_t *y_grad, const int stride, const int blk_width, const int blk_height";
specialize qw/av1_bicubic_grad_interpolation_highbd sse4_1 avx2/;

add_proto qw/int av1_opfl_mv_refinement_nxn/, " const int16_t *pdiff, int pstride, const int16_t *gx, const int16_t *gy, int gstride, int bw, int bh, int n, int d0, int d1, int grad_prec_bits, int mv_prec_bits, int mi_x, int mi_y, int mi_cols, int mi_rows, int is_decode, int *vx0, int *vy0, int *vx1, int *vy1";
specialize qw/av1_opfl_mv_refinement_nxn sse4_1 avx2/;

add_proto qw/void av1_copy_pred_array_highbd/, "const uint16_t *src1, const uint16_t *src2, int16_t *dst1,int16_t *dst2, int bw, int bh, int d0, int d1, int centered";
specialize qw/av1_copy_pred_array_highbd sse4_1/;

# High bitdepth functions

#inv txfm
add_proto qw/void inv_stxfm/ , "tran_low_t *src, tran_low_t *dst, const PREDICTION_MODE mode, const uint8_t stx_idx, const int size, const int bd";
specialize qw/inv_stxfm sse4_1 avx2/;
add_proto qw/void av1_highbd_inv_txfm_add/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add sse4_1 avx2/;

if (aom_config("CONFIG_CORE_TX") eq "yes") {
    add_proto qw/void inv_txfm/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
    specialize qw/inv_txfm avx2/;
}

if (aom_config("CONFIG_LOSSLESS_DPCM") eq "yes"){
    add_proto qw/void av1_highbd_inv_txfm_add_vert/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
    add_proto qw/void av1_highbd_inv_txfm_add_horz/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
}

add_proto qw/void av1_highbd_inv_txfm_add_4x4/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_4x4 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_8x8/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_8x8 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_4x8/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_4x8 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_8x4/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_8x4 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_4x16/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_4x16 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_16x4/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_16x4 sse4_1 neon/;
add_proto qw/void av1_highbd_inv_txfm_add_8x16/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_8x16  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_16x8/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_16x8  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_16x32/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_16x32  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_32x16/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x16  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_32x32/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x32  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_32x64/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x64  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_64x32/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_64x32  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_64x64/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_64x64  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_8x32/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x32  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_32x8/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x64  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_16x64/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_64x32  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_64x16/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_64x64  neon/;
add_proto qw/void av1_highbd_inv_txfm_add_4x32/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_4x32 sse4_1/;
add_proto qw/void av1_highbd_inv_txfm_add_32x4/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_32x4 sse4_1/;
add_proto qw/void av1_highbd_inv_txfm_add_4x64/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_4x64 sse4_1/;
add_proto qw/void av1_highbd_inv_txfm_add_64x4/,  "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
specialize qw/av1_highbd_inv_txfm_add_64x4 sse4_1/;

add_proto qw/void av1_highbd_iwht4x4_1_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";
add_proto qw/void av1_highbd_iwht4x4_16_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";

if (aom_config("CONFIG_LOSSLESS_DPCM") eq "yes"){
    add_proto qw/void av1_highbd_iwht4x4_1_vert_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";
    add_proto qw/void av1_highbd_iwht4x4_16_vert_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";
    add_proto qw/void av1_highbd_iwht4x4_1_horz_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";
    add_proto qw/void av1_highbd_iwht4x4_16_horz_add/, "const tran_low_t *input, uint16_t *dest, int dest_stride, int bd";
    add_proto qw/void av1_inv_idfm2d_add_4x4_vert/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd";
    add_proto qw/void av1_inv_idfm2d_add_4x4_horz/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int bd";
}

add_proto qw/void av1_inv_txfm2d_add_4x8/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_8x4/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_8x16/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_16x8/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_16x32/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_32x16/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_4x4/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
specialize qw/av1_inv_txfm2d_add_4x4 sse4_1/;
add_proto qw/void av1_inv_txfm2d_add_8x8/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
specialize qw/av1_inv_txfm2d_add_8x8 sse4_1/;
add_proto qw/void av1_inv_txfm2d_add_16x16/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_32x32/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";

add_proto qw/void av1_inv_txfm2d_add_64x64/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_32x64/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_64x32/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_16x64/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_64x16/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";

add_proto qw/void av1_inv_txfm2d_add_4x16/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_16x4/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_8x32/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_32x8/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";

add_proto qw/void av1_inv_txfm2d_add_4x32/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_32x4/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_8x64/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_64x8/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_4x64/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
add_proto qw/void av1_inv_txfm2d_add_64x4/, "const int32_t *input, uint16_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";

if (aom_config("CONFIG_IMPROVE_LOSSLESS_TXM") eq "yes") {
  if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
    add_proto qw/void av1_lossless_fwd_idtx/, "const int16_t *src_diff, tran_low_t *coeff, int diff_stride, TxfmParam *txfm_param";
    specialize qw/av1_lossless_fwd_idtx avx2/;
  }
  add_proto qw/void av1_lossless_inv_idtx_add/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
  specialize qw/av1_lossless_inv_idtx_add avx2/;
  if (aom_config("CONFIG_LOSSLESS_DPCM") eq "yes") {
    add_proto qw/void av1_lossless_inv_idtx_add_vert/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
    add_proto qw/void av1_lossless_inv_idtx_add_horz/, "const tran_low_t *input, uint16_t *dest, int stride, const TxfmParam *txfm_param";
  }
}

  # directional intra predictor functions
add_proto qw/void av1_highbd_dr_prediction_z1/, "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int upsample_above, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z1 avx2/;
add_proto qw/void av1_highbd_dr_prediction_z2/, "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int upsample_above, int upsample_left, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z2 avx2/;
add_proto qw/void av1_highbd_dr_prediction_z3/, "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int upsample_left, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z3 avx2/;

add_proto qw/void av1_highbd_dr_prediction_z1_idif/ , "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z1_idif avx2/;
add_proto qw/void av1_highbd_dr_prediction_z2_idif/ , "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z2_idif avx2/;
add_proto qw/void av1_highbd_dr_prediction_z3_idif/ , "uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *above, const uint16_t *left, int dx, int dy, int bd, int mrl_index";
specialize qw/av1_highbd_dr_prediction_z3_idif avx2/;

add_proto qw / void av1_highbd_ibp_dr_prediction_z1 /,
    "const IbpWeightsType weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90], int mode_idx, uint16_t *dst, ptrdiff_t stride, uint16_t* second_pred, ptrdiff_t second_stride, int bw, int bh";
add_proto qw / void av1_highbd_ibp_dr_prediction_z3 /,
    "const IbpWeightsType weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90], int mode_idx, uint16_t *dst, ptrdiff_t stride, uint16_t* second_pred, ptrdiff_t second_stride, int bw, int bh";

# Data-driven intra prediction (DIP)
add_proto qw/void av1_dip_matrix_multiplication/, "const uint16_t *A, const uint16_t *B, uint16_t *C, int bd";
specialize qw/av1_dip_matrix_multiplication avx2/;

# build compound seg mask functions
add_proto qw/void av1_build_compound_diffwtd_mask_highbd/, "uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const uint16_t *src0, int src0_stride, const uint16_t *src1, int src1_stride, int h, int w, int bd";
specialize qw/av1_build_compound_diffwtd_mask_highbd ssse3 avx2/;

add_proto qw/void av1_build_compound_diffwtd_mask_d16/, "uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0, int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w, ConvolveParams *conv_params, int bd";
specialize qw/av1_build_compound_diffwtd_mask_d16 sse4_1 avx2 neon/;

add_proto qw/void av1_avg_pooling_pdiff_gradients/,"int16_t *pdiff, const int pstride, int16_t *gx, int16_t *gy, const int gstride, const int bw, const int bh, const int n";
specialize qw/av1_avg_pooling_pdiff_gradients avx2/;

#
# Block Adaptive Weighted Prediction
#
add_proto qw/void av1_make_bawp_block/, "uint16_t *dst, int dst_stride, int16_t alpha, int32_t beta, int shift, int bw, int bh, int bd";
specialize qw/av1_make_bawp_block avx2/;


# Helper functions.
add_proto qw/void av1_round_shift_array/, "int32_t *arr, int size, int bit";
specialize "av1_round_shift_array", qw/sse4_1 neon/;

# Resize functions.
add_proto qw/void av1_resize_and_extend_frame/, "const YV12_BUFFER_CONFIG *src, YV12_BUFFER_CONFIG *dst, const InterpFilter filter, const int phase, const int num_planes";

#
# Encoder functions below this point.
#
if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {

  # ENCODEMB INVOKE
  add_proto qw/void av1_quantize_fp/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";
  add_proto qw/void av1_quantize_fp_32x32/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";
  add_proto qw/void av1_quantize_fp_64x64/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";
  add_proto qw/void av1_quantize_lp/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *round_ptr, const int32_t *quant_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan";
  add_proto qw/void aom_quantize_b_helper/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr, const int log_scale";

  # trellis quant
  add_proto qw/void av1_decide_states/, "const struct tcq_node_t *prev, const struct tcq_rate_t *rd, const struct prequant_t *pq, int limits, int tru_eob, int64_t rdmult, struct tcq_node_t *decision";
  specialize qw/av1_decide_states avx2/;
  add_proto qw/void av1_pre_quant/, "tran_low_t tqc, struct prequant_t* pqData, const int32_t* quant_ptr, int dqv, int log_scale, int scan_pos";
  specialize qw/av1_pre_quant avx2/;

  add_proto qw/void av1_get_rate_dist_def_luma/, "const struct tcq_param_t *p, const struct prequant_t *pq, const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int diag_ctx, int eob_rate, struct tcq_rate_t *rd";
  specialize qw/av1_get_rate_dist_def_luma avx2/;
  add_proto qw/void av1_get_rate_dist_def_chroma/, "const struct LV_MAP_COEFF_COST* txb_costs, const struct prequant_t *pq, const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int bwl, TX_CLASS tx_class, int diag_ctx, int eob_rate, int plane, int t_sign, int sign, struct tcq_rate_t *rd";
  specialize qw/av1_get_rate_dist_def_chroma avx2/;
  add_proto qw/void av1_get_rate_dist_lf_luma/, "const struct tcq_param_t *p, const struct prequant_t *pq, const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int diag_ctx, int eob_rate, int coeff_sign, struct tcq_rate_t *rd";
  specialize qw/av1_get_rate_dist_lf_luma avx2/;
  add_proto qw/void av1_get_rate_dist_lf_chroma/, "const struct LV_MAP_COEFF_COST *txb_costs, const struct prequant_t *pq, const struct tcq_coeff_ctx_t *coeff_ctx, int blk_pos, int diag_ctx, int eob_rate, int dc_sign_ctx, const int32_t *tmp_sign, int bwl, TX_CLASS tx_class, int plane, int coeff_sign, struct tcq_rate_t *rd";
  specialize qw/av1_get_rate_dist_lf_chroma avx2/;
  add_proto qw/void av1_update_states/, "const struct tcq_node_t *decision, int col, struct tcq_ctx_t *tcq_ctx";
  specialize qw/av1_update_states avx2/;
  add_proto qw/void av1_calc_block_eob_rate/, "struct macroblock *x, int plane, TX_SIZE tx_size, int eob, uint16_t *block_eob_rate";
  specialize qw/av1_calc_block_eob_rate avx2/;
  add_proto qw/int av1_find_best_path/, "const struct tcq_node_t *trellis, const int16_t *scan, const int32_t *dequant, const qm_val_t *iqmatrix, const tran_low_t *tcoeff, int first_scan_pos, int log_scale, tran_low_t *qcoeff, tran_low_t *dqcoeff, int *min_rate, int64_t *min_cost";
  specialize qw/av1_find_best_path avx2/;
  add_proto qw/void av1_get_coeff_ctx/, "const struct tcq_ctx_t *tcq_ctx, int col, struct tcq_coeff_ctx_t *coeff_ctx";
  specialize qw/av1_get_coeff_ctx avx2/;
  add_proto qw/void av1_update_nbr_diagonal/, "struct tcq_ctx_t *tcq_ctx, int row, int col, int bwl";
  specialize qw/av1_update_nbr_diagonal avx2/;

  # fdct functions

  add_proto qw/void av1_fwht4x4/, "const int16_t *input, tran_low_t *output, int stride";
  specialize qw/av1_fwht4x4 neon/;

  # fwd cctx
  add_proto qw/void av1_fwd_cross_chroma_tx_block/, "tran_low_t *coeff_c1, tran_low_t *coeff_c2,
                         TX_SIZE tx_size, CctxType cctx_type, const int bd";
  specialize qw/av1_fwd_cross_chroma_tx_block avx2/;

  #fwd txfm
  add_proto qw/void fwd_stxfm/ , "tran_low_t *src, tran_low_t *dst, const PREDICTION_MODE mode, const uint8_t stx_idx, const int size, const int bd";
  specialize qw/fwd_stxfm sse4_1 avx2/;
  add_proto qw/void av1_lowbd_fwd_txfm/, "const int16_t *src_diff, tran_low_t *coeff, int diff_stride, TxfmParam *txfm_param";
  add_proto qw/void av1_fwd_txfm2d_4x8/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_8x4/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_8x16/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_16x8/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_16x32/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_32x16/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_4x16/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_16x4/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_8x32/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_32x8/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_4x4/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_8x8/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_16x16/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  add_proto qw/void av1_fwd_txfm2d_32x32/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";

  if (aom_config("CONFIG_CORE_TX") eq "yes") {
    add_proto qw/void fwd_txfm/,  "const int16_t *resi, tran_low_t *coeff, int diff_stride, TxfmParam *txfm_param";
    specialize qw/fwd_txfm avx2/;
  }

  specialize qw/av1_lowbd_fwd_txfm sse2 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_4x8 sse4_1/;
  specialize qw/av1_fwd_txfm2d_8x4 sse4_1/;
  specialize qw/av1_fwd_txfm2d_8x16 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_16x8 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_16x32 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_32x16 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_4x16 sse4_1/;
  specialize qw/av1_fwd_txfm2d_16x4 sse4_1/;
  specialize qw/av1_fwd_txfm2d_8x32 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_32x8 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_4x4 sse4_1/;
  specialize qw/av1_fwd_txfm2d_8x8 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_16x16 sse4_1 avx2/;
  specialize qw/av1_fwd_txfm2d_32x32 sse4_1 avx2/;


  add_proto qw/void av1_fwd_txfm2d_64x64/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_64x64 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_32x64/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_32x64 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_64x32/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_64x32 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_16x64/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_16x64 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_64x16/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_64x16 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_4x32/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_4x32 sse4_1/;
  add_proto qw/void av1_fwd_txfm2d_32x4/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_32x4 sse4_1/;
  add_proto qw/void av1_fwd_txfm2d_8x64/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_8x64 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_64x8/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_64x8 sse4_1 avx2/;
  add_proto qw/void av1_fwd_txfm2d_4x64/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_4x64 sse4_1/;
  add_proto qw/void av1_fwd_txfm2d_64x4/, "const int16_t *input, int32_t *output, int stride, TX_TYPE tx_type, int use_ddt, int bd";
  specialize qw/av1_fwd_txfm2d_64x4 sse4_1/;

  #
  # Motion search
  #
  add_proto qw/void av1_highbd_apply_temporal_filter/, "const struct yv12_buffer_config *ref_frame, const struct macroblockd *mbd, const BLOCK_SIZE block_size, const int mb_row, const int mb_col, const int num_planes, const double *noise_levels, const MV *subblock_mvs, const int *subblock_mses, const int q_factor, const int filter_strength, const uint16_t *pred, uint32_t *accum, uint16_t *count";
  specialize qw/av1_highbd_apply_temporal_filter sse2/;

  add_proto qw/void av1_quantize_b/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, const qm_val_t * qm_ptr, const qm_val_t * iqm_ptr, int log_scale";

  # ENCODEMB INVOKE
  add_proto qw/int64_t av1_highbd_block_error/, "const tran_low_t *coeff, const tran_low_t *dqcoeff, intptr_t block_size, int64_t *ssz, int bd";
  specialize qw/av1_highbd_block_error sse2 avx2/;

  add_proto qw/void av1_highbd_quantize_fp/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, int log_scale";
  specialize qw/av1_highbd_quantize_fp sse4_1 avx2/;

  add_proto qw/void av1_highbd_fwht4x4/, "const int16_t *input, tran_low_t *output, int stride";
  specialize qw/av1_highbd_fwht4x4 neon/;

  # End av1_high encoder functions

  # txb
  add_proto qw/void av1_txb_init_levels_skip/, "const tran_low_t *const coeff, const int width, const int height, uint8_t *const levels";
  specialize qw/av1_txb_init_levels_skip sse4_1 avx2/;

  add_proto qw/void av1_get_nz_map_contexts_skip/, "const uint8_t *const levels, const int16_t *const scan, const uint16_t bob, const uint16_t eob, const TX_SIZE tx_size, int8_t *const coeff_contexts";

  add_proto qw/void av1_get_nz_map_contexts/, "const uint8_t *const levels, const int16_t *const scan, const uint16_t eob, const TX_SIZE tx_size, const TX_CLASS tx_class, int8_t *const coeff_contexts, const int plane";
  specialize qw/av1_get_nz_map_contexts sse2/;

  add_proto qw/void av1_txb_init_levels_signs/, "const tran_low_t *const coeff, const int width, const int height, uint8_t *const levels, int8_t *const signs";
  specialize qw/av1_txb_init_levels_signs sse4_1 avx2/;

  add_proto qw/void av1_txb_init_levels/, "const tran_low_t *const coeff, const int width, const int height, uint8_t *const levels";
  specialize qw/av1_txb_init_levels sse4_1 avx2 neon/;

  add_proto qw/uint64_t av1_wedge_sse_from_residuals/, "const int16_t *r1, const int16_t *d, const uint8_t *m, int N";
  specialize qw/av1_wedge_sse_from_residuals sse2 avx2/;
  add_proto qw/int8_t av1_wedge_sign_from_residuals/, "const int16_t *ds, const uint8_t *m, int N, int64_t limit";
  specialize qw/av1_wedge_sign_from_residuals sse2 avx2/;
  add_proto qw/void av1_wedge_compute_delta_squares/, "int16_t *d, const int16_t *a, const int16_t *b, int N";
  specialize qw/av1_wedge_compute_delta_squares sse2 avx2/;

  # hash
  add_proto qw/uint32_t av1_get_crc32c_value/, "void *crc_calculator, uint8_t *p, size_t length";
  specialize qw/av1_get_crc32c_value sse4_2/;

  add_proto qw/void av1_compute_stats_highbd/,  "int wiener_win, const uint16_t *dgd8, const uint16_t *src, int h_start, int h_end, int v_start, int v_end, int dgd_stride, int src_stride, int64_t *M, int64_t *H, aom_bit_depth_t bit_depth";
  specialize qw/av1_compute_stats_highbd sse4_1 avx2/;

  add_proto qw/void av1_get_horver_correlation_full/, " const int16_t *diff, int stride, int w, int h, float *hcorr, float *vcorr";
  specialize qw/av1_get_horver_correlation_full sse4_1 avx2 neon/;

  add_proto qw/void av1_nn_predict/, " const float *input_nodes, const NN_CONFIG *const nn_config, int reduce_prec, float *const output";
  if (aom_config("CONFIG_EXCLUDE_SIMD_MISMATCH") ne "yes") {
    specialize qw/av1_nn_predict sse3 neon/;
  }
}
# end encoder functions

# CNN functions

add_proto qw/void av1_cnn_activate/, " float **input, int channels, int width, int height, int stride, ACTIVATION layer_activation";
add_proto qw/void av1_cnn_add/, " float **input, int channels, int width, int height, int stride, const float **add";
add_proto qw/void av1_cnn_predict/, " const float **input, int in_width, int in_height, int in_stride, const CNN_CONFIG *cnn_config, const CNN_THREAD_DATA *thread_data, CNN_MULTI_OUT *output_struct";
add_proto qw/void av1_cnn_convolve/, " const float **input, int in_width, int in_height, int in_stride, const CNN_LAYER_CONFIG *layer_config, float **output, int out_stride, int start_idx, int step";
add_proto qw/void av1_cnn_deconvolve/, " const float **input, int in_width, int in_height, int in_stride, const CNN_LAYER_CONFIG *layer_config, float **output, int out_stride";
add_proto qw/void av1_cnn_batchnorm/, "float **image, int channels, int width, int height, int stride, const float *gamma, const float *beta, const float *mean, const float *std";

# Deringing Functions

add_proto qw/int cdef_find_dir/, "const uint16_t *img, int stride, int32_t *var, int coeff_shift";
add_proto qw/void cdef_filter_block/, "uint8_t *dst8, uint16_t *dst16, int dstride, const uint16_t *in, int pri_strength, int sec_strength, int dir, int pri_damping, int sec_damping, BLOCK_SIZE bsize, int coeff_shift";

add_proto qw/void cdef_copy_rect8_16bit_to_16bit/, "uint16_t *dst, int dstride, const uint16_t *src, int sstride, int v, int h";

# VS compiling for 32 bit targets does not support vector types in
# structs as arguments, which makes the v256 type of the intrinsics
# hard to support, so optimizations for this target are disabled.
if ($opts{config} !~ /libs-x86-win32-vs.*/) {
  specialize qw/cdef_find_dir sse2 ssse3 sse4_1 avx2 neon/;
  specialize qw/cdef_filter_block sse2 ssse3 sse4_1 avx2 neon/;
  specialize qw/cdef_copy_rect8_16bit_to_16bit sse2 ssse3 sse4_1 avx2 neon/;
}

if (aom_config("CONFIG_GDF") eq "yes") {
  add_proto qw/void gdf_set_lap_and_cls_unit/, "const int i_min, const int i_max, const int j_min, const int j_max, const int stripe_size, const uint16_t *rec_pnt, const int rec_stride, const int bit_depth, uint16_t *const *gdf_lap_y, const int gdf_lap_y_stride, uint32_t *gdf_cls_y, const int gdf_cls_y_stride";
  specialize qw/gdf_set_lap_and_cls_unit avx2/;
  add_proto qw/void gdf_inference_unit/, "const int i_min, const int i_max, const int j_min, const int j_max, const int stripe_size, const int qp_idx, const uint16_t* rec_pnt, const int rec_stride, uint16_t *const *gdf_lap_pnt, const int gdf_lap_stride, const uint32_t *gdf_cls_pnt, const int gdf_cls_stride, int16_t* err_pnt, const int err_stride, const int pxl_shift, const int ref_dst_idx";
  specialize qw/gdf_inference_unit avx2/;
  add_proto qw/void gdf_compensation_unit/, "uint16_t* rec_pnt, const int rec_stride, int16_t* err_pnt, const int err_stride, const int err_shift, const int scale, const int pxl_max, const int blk_height, const int blk_width";
  specialize qw/gdf_compensation_unit avx2/;
}

# Cross-component Sample Offset
add_proto qw/void ccso_filter_block_hbd_wo_buf/, "const uint16_t *src_y, uint16_t *dst_yuv, const int x, const int y, const int pic_width, const int pic_height, int *src_cls, const int8_t *offset_buf, const int scaled_ext_stride, const int dst_stride, const int y_uv_hscale, const int y_uv_vscale, const int thr, const int neg_thr, const int *src_loc, const int max_val, const int blk_size_x, const int blk_size_y, const bool isSingleBand, const uint8_t shift_bits, const int edge_clf, const uint8_t ccso_bo_only";
specialize qw/ccso_filter_block_hbd_wo_buf avx2/;

if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
  add_proto qw/void ccso_filter_block_hbd_with_buf/, "const uint16_t *src_y, uint16_t *dst_yuv, const uint8_t *src_cls0, const uint8_t *src_cls1,
                    const int src_y_stride, const int dst_stride,
                    const int ccso_stride,
                    const int x, const int y,
                    const int pic_width, const int pic_height,
                    const int8_t *filter_offset, const int blk_size_x,
                    const int blk_size_y,
                    const int y_uv_hscale,  const int y_uv_vscale,
                    const int max_val, const uint8_t shift_bits,
                    const uint8_t ccso_bo_only";
  specialize qw/ccso_filter_block_hbd_with_buf avx2/;

  add_proto qw/void ccso_filter_block_hbd_with_buf_bo_only/, "const uint16_t *src_y, uint16_t *dst_yuv, const uint8_t *src_cls0, const uint8_t *src_cls1,
                    const int src_y_stride, const int dst_stride,
                    const int ccso_stride,
                    const int x, const int y,
                    const int pic_width, const int pic_height,
                    const int8_t *filter_offset, const int blk_size_x,
                    const int blk_size_y,
                    const int y_uv_hscale,  const int y_uv_vscale,
                    const int max_val, const uint8_t shift_bits,
                    const uint8_t ccso_bo_only";
  specialize qw/ccso_filter_block_hbd_with_buf_bo_only avx2/;

  add_proto qw/uint64_t compute_distortion_block/, "const uint16_t *org, const int org_stride,
                      const uint16_t *rec16, const int rec_stride, const int x, const int y,
                      const int log2_filter_unit_size_y, const int log2_filter_unit_size_x, const int height,
                      const int width";
  specialize qw/compute_distortion_block avx2/;

  add_proto qw/void ccso_derive_src_block/, "const uint16_t *src_y, uint8_t *const src_cls0,
                        uint8_t *const src_cls1, const int src_y_stride, const int ccso_stride,
                        const int x, const int y, const int pic_width, const int pic_height,
                        const int y_uv_hscale, const int y_uv_vscale, const int qstep,
                        const int neg_qstep, const int *src_loc, const int blk_size_x,
                        const int blk_size_y, const int edge_clf";
  specialize qw/ccso_derive_src_block avx2/
}

# WARPED_MOTION / GLOBAL_MOTION functions

add_proto qw/void av1_highbd_warp_affine/, "const int32_t *mat, const uint16_t *ref, int width, int height, int stride, uint16_t *pred, int p_col, int p_row, int p_width, int p_height, int p_stride, int subsampling_x, int subsampling_y, int bd, ConvolveParams *conv_params, int16_t alpha, int16_t beta, int16_t gamma, int16_t delta";
specialize qw/av1_highbd_warp_affine sse4_1 avx2/;

if (aom_config("CONFIG_EXT_WARP_FILTER") eq "yes") {
  if (aom_config("CONFIG_WARP_BD_BOX") eq "yes") {
    add_proto qw/void av1_ext_highbd_warp_affine/, "const int32_t *mat, const uint16_t *ref, int width, int height, int stride, uint16_t *pred, int p_col, int p_row, int p_width, int p_height, int p_stride, int subsampling_x, int subsampling_y, int bd, ConvolveParams *conv_params, int use_warp_bd_box, WarpBoundaryBox *warp_bd_box";
    specialize qw/av1_ext_highbd_warp_affine sse4_1/;
  } else {
    add_proto qw/void av1_ext_highbd_warp_affine/, "const int32_t *mat, const uint16_t *ref, int width, int height, int stride, uint16_t *pred, int p_col, int p_row, int p_width, int p_height, int p_stride, int subsampling_x, int subsampling_y, int bd, ConvolveParams *conv_params";
    specialize qw/av1_ext_highbd_warp_affine sse4_1 avx2/;
  }
}

# CONVOLVE_ROUND/COMPOUND_ROUND functions

add_proto qw/void av1_highbd_convolve_2d_sr/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_convolve_x_sr/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_convolve_y_sr/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn, int bd";
add_proto qw/void av1_highbd_dist_wtd_convolve_2d/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int subpel_y_qn, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_dist_wtd_convolve_x/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_dist_wtd_convolve_y/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_dist_wtd_convolve_2d_copy/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, ConvolveParams *conv_params, int bd";
add_proto qw/void av1_highbd_convolve_2d_scale/, "const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w, int h, const InterpFilterParams *filter_params_x, const InterpFilterParams *filter_params_y, const int subpel_x_qn, const int x_step_qn, const int subpel_y_qn, const int y_step_qn, ConvolveParams *conv_params, int bd";

specialize qw/av1_highbd_dist_wtd_convolve_2d sse4_1 avx2/;
specialize qw/av1_highbd_dist_wtd_convolve_x sse4_1 avx2/;
specialize qw/av1_highbd_dist_wtd_convolve_y sse4_1 avx2/;
specialize qw/av1_highbd_dist_wtd_convolve_2d_copy sse4_1 avx2/;
specialize qw/av1_highbd_convolve_2d_sr ssse3 avx2/;
specialize qw/av1_highbd_convolve_x_sr ssse3 avx2/;
specialize qw/av1_highbd_convolve_y_sr ssse3 avx2/;
specialize qw/av1_highbd_convolve_2d_scale sse4_1/;

# INTRA_EDGE functions
add_proto qw/void av1_filter_intra_edge_high/, "uint16_t *p, int sz, int strength";
specialize qw/av1_filter_intra_edge_high sse4_1/;
add_proto qw/void av1_upsample_intra_edge_high/, "uint16_t *p, int sz, int bd";
specialize qw/av1_upsample_intra_edge_high sse4_1/;

# CFL
add_proto qw/void mhccp_predict_hv_hbd/, "const uint16_t *input, uint16_t *dst, bool have_top, bool have_left, int dst_stride, int64_t *alpha_q3, int bit_depth, int width, int height, int dir";
# Temporarily disable the sse4 function since it might overflow.
if ((aom_config("MHCCP_CONVOLVE_SIMPLIFY") eq "yes") && 0) {
  specialize qw/mhccp_predict_hv_hbd sse4_1/;
}

add_proto qw/cfl_subtract_average_fn cfl_get_subtract_average_fn/, "TX_SIZE tx_size";
specialize qw/cfl_get_subtract_average_fn sse2 avx2 neon vsx/;

add_proto qw/cfl_subsample_hbd_fn cfl_get_luma_subsampling_420_hbd/, "TX_SIZE tx_size";
specialize qw/cfl_get_luma_subsampling_420_hbd ssse3 avx2 neon/;

add_proto qw/cfl_subsample_hbd_fn cfl_get_luma_subsampling_422_hbd/, "TX_SIZE tx_size";
specialize qw/cfl_get_luma_subsampling_422_hbd ssse3 avx2 neon/;

add_proto qw/cfl_subsample_hbd_fn cfl_get_luma_subsampling_444_hbd/, "TX_SIZE tx_size";
specialize qw/cfl_get_luma_subsampling_444_hbd ssse3 avx2 neon/;

add_proto qw/cfl_predict_hbd_fn cfl_get_predict_hbd_fn/, "TX_SIZE tx_size";
specialize qw/cfl_get_predict_hbd_fn avx2 neon/;

1;
