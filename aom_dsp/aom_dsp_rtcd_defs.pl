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
sub aom_dsp_forward_decls() {
print <<EOF
/*
 * DSP
 */

#include "aom/aom_integer.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/entdec.h"
#include "av1/common/enums.h"
#include "av1/common/blockd.h"

EOF
}
forward_decls qw/aom_dsp_forward_decls/;

# optimizations which depend on multiple features
$avx2_ssse3 = '';
if ((aom_config("HAVE_AVX2") eq "yes") && (aom_config("HAVE_SSSE3") eq "yes")) {
  $avx2_ssse3 = 'avx2';
}

# functions that are 64 bit only.
$mmx_x86_64 = $sse2_x86_64 = $ssse3_x86_64 = $avx_x86_64 = $avx2_x86_64 = '';
if ($opts{arch} eq "x86_64") {
  $mmx_x86_64 = 'mmx';
  $sse2_x86_64 = 'sse2';
  $ssse3_x86_64 = 'ssse3';
  $avx_x86_64 = 'avx';
  $avx2_x86_64 = 'avx2';
}

@block_widths = (4, 8, 16, 32, 64, 128, 256);

@block_sizes = ();
foreach $w (@block_widths) {
  foreach $h (@block_widths) {
    push @block_sizes, [$w, $h] if ($w <= 2*$h && $h <= 2*$w) ;
  }
}
push @block_sizes, [4, 16];
push @block_sizes, [16, 4];
push @block_sizes, [8, 32];
push @block_sizes, [32, 8];
push @block_sizes, [16, 64];
push @block_sizes, [64, 16];
push @block_sizes, [4, 32];
push @block_sizes, [32, 4];
push @block_sizes, [8, 64];
push @block_sizes, [64, 8];
push @block_sizes, [4, 64];
push @block_sizes, [64, 4];

@tx_dims = (2, 4, 8, 16, 32, 64);
@tx_sizes = ();
foreach $w (@tx_dims) {
  push @tx_sizes, [$w, $w];
  foreach $h (@tx_dims) {
    push @tx_sizes, [$w, $h] if ($w >=4 && $h >=4 && ($w == 2*$h || $h == 2*$w));
    push @tx_sizes, [$w, $h] if ($w >=4 && $h >=4 && ($w == 4*$h || $h == 4*$w));
    push @tx_sizes, [$w, $h] if ($w >=4 && $h >=4 && ($w == 8*$h || $h == 8*$w));
    push @tx_sizes, [$w, $h] if ($w >=4 && $h >=4 && ($w == 16*$h || $h == 16*$w));
  }
}

@pred_names = qw /
              dc dc_top dc_left dc_128 v h paeth smooth smooth_v smooth_h ibp_dc
              ibp_dc_top ibp_dc_left /
    ;

#
# Intra prediction
#

foreach (@tx_sizes) {
  ($w, $h) = @$_;
  foreach $pred_name (@pred_names) {
    add_proto "void", "aom_highbd_${pred_name}_predictor_${w}x${h}",
              "uint16_t *dst, ptrdiff_t y_stride, const uint16_t *above, const uint16_t *left, int bd";
  }
}

specialize qw/aom_highbd_v_predictor_4x4 sse2/;
specialize qw/aom_highbd_v_predictor_4x8 sse2/;
specialize qw/aom_highbd_v_predictor_8x4 sse2/;
specialize qw/aom_highbd_v_predictor_8x8 sse2/;
specialize qw/aom_highbd_v_predictor_8x16 sse2/;
specialize qw/aom_highbd_v_predictor_16x8 sse2/;
specialize qw/aom_highbd_v_predictor_16x16 sse2/;
specialize qw/aom_highbd_v_predictor_16x32 sse2/;
specialize qw/aom_highbd_v_predictor_32x16 sse2/;
specialize qw/aom_highbd_v_predictor_32x32 sse2/;

# TODO(yunqingwang): optimize rectangular DC_PRED to replace division
# by multiply and shift.
specialize qw/aom_highbd_dc_predictor_4x4 sse2 neon/;
specialize qw/aom_highbd_dc_predictor_4x8 sse2/;
specialize qw/aom_highbd_dc_predictor_8x4 sse2/;;
specialize qw/aom_highbd_dc_predictor_8x8 sse2 neon/;;
specialize qw/aom_highbd_dc_predictor_8x16 sse2/;;
specialize qw/aom_highbd_dc_predictor_16x8 sse2/;
specialize qw/aom_highbd_dc_predictor_16x16 sse2 neon/;
specialize qw/aom_highbd_dc_predictor_16x32 sse2/;
specialize qw/aom_highbd_dc_predictor_32x16 sse2/;
specialize qw/aom_highbd_dc_predictor_32x32 sse2 neon/;
specialize qw/aom_highbd_dc_predictor_64x64 neon/;

specialize qw/aom_highbd_h_predictor_4x4 sse2/;
specialize qw/aom_highbd_h_predictor_4x8 sse2/;
specialize qw/aom_highbd_h_predictor_8x4 sse2/;
specialize qw/aom_highbd_h_predictor_8x8 sse2/;
specialize qw/aom_highbd_h_predictor_8x16 sse2/;
specialize qw/aom_highbd_h_predictor_16x8 sse2/;
specialize qw/aom_highbd_h_predictor_16x16 sse2/;
specialize qw/aom_highbd_h_predictor_16x32 sse2/;
specialize qw/aom_highbd_h_predictor_32x16 sse2/;
specialize qw/aom_highbd_h_predictor_32x32 sse2/;
specialize qw/aom_highbd_dc_left_predictor_4x4 sse2/;
specialize qw/aom_highbd_dc_top_predictor_4x4 sse2/;
specialize qw/aom_highbd_dc_128_predictor_4x4 sse2/;
specialize qw/aom_highbd_dc_left_predictor_4x8 sse2/;
specialize qw/aom_highbd_dc_top_predictor_4x8 sse2/;
specialize qw/aom_highbd_dc_128_predictor_4x8 sse2/;
specialize qw/aom_highbd_dc_top_predictor_8x4 sse2/;
specialize qw/aom_highbd_dc_128_predictor_8x4 sse2/;
specialize qw/aom_highbd_dc_left_predictor_8x4 sse2/;
specialize qw/aom_highbd_dc_left_predictor_8x8 sse2/;
specialize qw/aom_highbd_dc_left_predictor_8x16 sse2/;
specialize qw/aom_highbd_dc_top_predictor_8x8 sse2/;
specialize qw/aom_highbd_dc_128_predictor_8x8 sse2/;
specialize qw/aom_highbd_dc_top_predictor_8x16 sse2/;
specialize qw/aom_highbd_dc_128_predictor_8x16 sse2/;
specialize qw/aom_highbd_dc_left_predictor_16x8 sse2/;
specialize qw/aom_highbd_dc_top_predictor_16x8 sse2/;
specialize qw/aom_highbd_dc_128_predictor_16x8 sse2/;
specialize qw/aom_highbd_dc_left_predictor_16x16 sse2/;
specialize qw/aom_highbd_dc_top_predictor_16x16 sse2/;
specialize qw/aom_highbd_dc_128_predictor_16x16 sse2/;
specialize qw/aom_highbd_dc_left_predictor_16x32 sse2/;
specialize qw/aom_highbd_dc_top_predictor_16x32 sse2/;
specialize qw/aom_highbd_dc_128_predictor_16x32 sse2/;
specialize qw/aom_highbd_dc_left_predictor_32x16 sse2/;
specialize qw/aom_highbd_dc_top_predictor_32x16 sse2/;
specialize qw/aom_highbd_dc_128_predictor_32x16 sse2/;
specialize qw/aom_highbd_dc_left_predictor_32x32 sse2/;
specialize qw/aom_highbd_dc_top_predictor_32x32 sse2/;
specialize qw/aom_highbd_dc_128_predictor_32x32 sse2/;
#
# Sub Pixel Filters
#
add_proto qw/void aom_highbd_convolve8/, "const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst, ptrdiff_t dst_stride, const InterpKernel *filter, int x0_q4, int x_step_q4, int y0_q4, int y_step_q4, int w, int h, int bd";
add_proto qw/void aom_convolve_copy/,             "const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, int w, int h";

specialize qw/aom_convolve_copy       neon dspr2 msa sse2 avx2/;

add_proto qw/void aom_highbd_convolve_copy/, "const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst, ptrdiff_t dst_stride, int w, int h";
specialize qw/aom_highbd_convolve_copy sse2 avx2/;

add_proto qw/void aom_highbd_convolve8_horiz/, "const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h, int bd";
specialize qw/aom_highbd_convolve8_horiz sse2 avx2/;

add_proto qw/void aom_highbd_convolve8_vert/, "const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h, int bd";
specialize qw/aom_highbd_convolve8_vert sse2 avx2/;

#
# Loopfilter
#
add_proto qw/void aom_highbd_lpf_horizontal_generic/, "uint16_t *s, int pitch, int filt_width_neg, int filt_width_pos, const uint16_t *q_thresh, const uint16_t *side_thresh, int bd, int is_lossless_neg, int is_lossless_pos";
specialize qw/aom_highbd_lpf_horizontal_generic sse4_1/;

add_proto qw/void aom_highbd_lpf_vertical_generic/, "uint16_t *s, int pitch, int filt_width_neg, int filt_width_pos, const uint16_t *q_thresh, const uint16_t *side_thresh, int bd, int is_lossless_neg, int is_lossless_pos";
specialize qw/aom_highbd_lpf_vertical_generic sse4_1/;

#
# Entropy
#
add_proto qw/int od_ec_decode_cdf_q15/, "od_ec_dec *dec, const uint16_t *icdf, int nsyms";
if (aom_config("CONFIG_AV1_DECODER") eq "yes") {
  specialize qw/od_ec_decode_cdf_q15 avx2/;
}

#
# Encoder functions.
#

#
# Forward transform
#
if (aom_config("CONFIG_AV1_ENCODER") eq "yes"){
    add_proto qw/void aom_fdct4x4/, "const int16_t *input, tran_low_t *output, int stride";
    specialize qw/aom_fdct4x4 neon sse2/;

    add_proto qw/void aom_fdct4x4_lp/, "const int16_t *input, int16_t *output, int stride";
    specialize qw/aom_fdct4x4_lp neon sse2/;

    add_proto qw/void aom_highbd_fdct8x8/, "const int16_t *input, tran_low_t *output, int stride";
    specialize qw/aom_highbd_fdct8x8 sse2/;

    # FFT/IFFT (float) only used for denoising (and noise power spectral density estimation)
    add_proto qw/void aom_fft2x2_float/, "const float *input, float *temp, float *output";

    add_proto qw/void aom_fft4x4_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_fft4x4_float                  sse2/;

    add_proto qw/void aom_fft8x8_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_fft8x8_float avx2             sse2/;

    add_proto qw/void aom_fft16x16_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_fft16x16_float avx2           sse2/;

    add_proto qw/void aom_fft32x32_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_fft32x32_float avx2           sse2/;

    add_proto qw/void aom_ifft2x2_float/, "const float *input, float *temp, float *output";

    add_proto qw/void aom_ifft4x4_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_ifft4x4_float                 sse2/;

    add_proto qw/void aom_ifft8x8_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_ifft8x8_float avx2            sse2/;

    add_proto qw/void aom_ifft16x16_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_ifft16x16_float avx2          sse2/;

    add_proto qw/void aom_ifft32x32_float/, "const float *input, float *temp, float *output";
    specialize qw/aom_ifft32x32_float avx2          sse2/;
}  # CONFIG_AV1_ENCODER

#
# Quantization
#
if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
    add_proto qw/void aom_highbd_quantize_b/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan, const int log_scale";
    specialize qw/aom_highbd_quantize_b avx2 sse2/;

    add_proto qw/void aom_highbd_quantize_b_adaptive/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";

    add_proto qw/void aom_highbd_quantize_b_32x32_adaptive/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";

    add_proto qw/void aom_highbd_quantize_b_64x64_adaptive/, "const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr, const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan";
}  # CONFIG_AV1_ENCODER

#
# Alpha blending with mask
#
add_proto qw/void aom_highbd_blend_a64_mask/, "uint16_t *dst, uint32_t dst_stride, const uint16_t *src0, uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride, const uint8_t *mask, uint32_t mask_stride, int w, int h, int subw, int subh, int bd";
add_proto qw/void aom_highbd_blend_a64_hmask/, "uint16_t *dst, uint32_t dst_stride, const uint16_t *src0, uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride, const uint8_t *mask, int w, int h, int bd";
add_proto qw/void aom_highbd_blend_a64_vmask/, "uint16_t *dst, uint32_t dst_stride, const uint16_t *src0, uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride, const uint8_t *mask, int w, int h, int bd";
add_proto qw/void aom_highbd_blend_a64_d16_mask/, "uint16_t *dst, uint32_t dst_stride, const CONV_BUF_TYPE *src0, uint32_t src0_stride, const CONV_BUF_TYPE *src1, uint32_t src1_stride, const uint8_t *mask, uint32_t mask_stride, int w, int h, int subw, int subh, ConvolveParams *conv_params, const int bd";
specialize "aom_highbd_blend_a64_mask", qw/sse4_1/;
specialize "aom_highbd_blend_a64_hmask", qw/sse4_1/;
specialize "aom_highbd_blend_a64_vmask", qw/sse4_1/;
specialize "aom_highbd_blend_a64_d16_mask", qw/sse4_1 avx2/;

#
# Block subtraction
#
add_proto qw/void aom_highbd_subtract_block/, "int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint16_t *src_ptr, ptrdiff_t src_stride, const uint16_t *pred_ptr, ptrdiff_t pred_stride, int bd";
specialize qw/aom_highbd_subtract_block sse2/;

add_proto qw/void aom_highbd_subtract_block_vert/, "int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint16_t *src_ptr, ptrdiff_t src_stride, const uint16_t *pred_ptr, ptrdiff_t pred_stride, int bd";
add_proto qw/void aom_highbd_subtract_block_horz/, "int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint16_t *src_ptr, ptrdiff_t src_stride, const uint16_t *pred_ptr, ptrdiff_t pred_stride, int bd";

add_proto qw/unsigned int/, "aom_highbd_sad8x8", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
specialize "aom_highbd_sad8x8", qw/sse2/;
specialize qw/aom_highbd_sad8x8 sse2/;

add_proto qw/unsigned int/, "aom_highbd_sad8x16", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
specialize "aom_highbd_sad8x16", qw/sse2/;
specialize qw/aom_highbd_sad8x16 sse2/;

add_proto qw/unsigned int/, "aom_highbd_sad16x8", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";

add_proto qw/unsigned int/, "aom_highbd_sad16x16", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";

add_proto qw/unsigned int/, "aom_highbd_sad8x8_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad16x16_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad16x8_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad8x16_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad12x20_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad20x12_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad12x12_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad20x20_ds", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";

add_proto qw/unsigned int/, "aom_highbd_sad20x20", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad20x12", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad12x20", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
add_proto qw/unsigned int/, "aom_highbd_sad12x12", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";

if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
  add_proto qw/void/, "aom_get_blk_sse_sum", "const int16_t *data, int stride, int bw, int bh, int *x_sum, int64_t *x2_sum";
  specialize qw/aom_get_blk_sse_sum sse2 avx2/;

  add_proto qw/int64_t/, "aom_highbd_sse", "const uint16_t *a, int a_stride, const uint16_t *b,int b_stride, int width, int height";
  specialize qw/aom_highbd_sse  sse4_1 avx2 neon/;

  if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
    #
    # Sum of Squares
    #
    add_proto qw/uint64_t aom_sum_squares_2d_i16/, "const int16_t *src, int stride, int width, int height";
    specialize qw/aom_sum_squares_2d_i16 sse2 avx2 neon/;

    add_proto qw/uint64_t aom_sum_squares_i16/, "const int16_t *src, uint32_t N";
    specialize qw/aom_sum_squares_i16 sse2/;

    add_proto qw/uint64_t aom_sum_squares_i32/, "const int32_t *src, int32_t n";
    specialize qw/aom_sum_squares_i32 avx2/;

    add_proto qw/uint64_t aom_var_2d_u8/, "uint8_t *src, int src_stride, int width, int height";
    specialize qw/aom_var_2d_u8 sse2 avx2/;

    add_proto qw/uint64_t aom_var_2d_u16/, "uint16_t *src, int src_stride, int width, int height";
    specialize qw/aom_var_2d_u16 sse2 avx2/;
  }

  add_proto qw/uint64_t aom_sum_sse_2d_i16/, "const int16_t *src, int src_stride, int width, int height, int *sum";
  specialize qw/aom_sum_sse_2d_i16 sse2 avx2/;

  foreach (@block_sizes) {
    ($w, $h) = @$_;
    add_proto qw/unsigned int/, "aom_highbd_sad${w}x${h}", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
    add_proto qw/unsigned int/, "aom_highbd_sad_skip_${w}x${h}", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride";
    add_proto qw/unsigned int/, "aom_highbd_sad${w}x${h}_avg", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride, const uint16_t *second_pred";
    if ($w != 128 && $h != 128 && $w != 4 && $w != 256 && $h != 256) {
      if (!($w == 16 && $h == 64) && !($w == 16 && $h == 32) && !($w == 16 && $h == 16) && !($w == 16 && $h == 8) && !($w == 16 && $h == 4)) {
        specialize "aom_highbd_sad${w}x${h}", qw/sse2/;
        specialize "aom_highbd_sad${w}x${h}_avg", qw/sse2/;
      }
    }
    add_proto qw/unsigned int/, "aom_highbd_dist_wtd_sad${w}x${h}_avg", "const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr, int ref_stride, const uint16_t *second_pred, const DIST_WTD_COMP_PARAMS* jcp_param";
  }
  specialize qw/aom_highbd_sad256x256 avx2/;
  specialize qw/aom_highbd_sad256x128 avx2/;
  specialize qw/aom_highbd_sad128x256 avx2/;
  specialize qw/aom_highbd_sad128x128 avx2/;
  specialize qw/aom_highbd_sad128x64  avx2/;
  specialize qw/aom_highbd_sad64x128  avx2/;
  specialize qw/aom_highbd_sad64x64   avx2 sse2/;
  specialize qw/aom_highbd_sad64x32   avx2 sse2/;
  specialize qw/aom_highbd_sad32x64   avx2 sse2/;
  specialize qw/aom_highbd_sad32x32   avx2 sse2/;
  specialize qw/aom_highbd_sad32x16   avx2 sse2/;
  specialize qw/aom_highbd_sad16x16_ds   avx2/;
  specialize qw/aom_highbd_sad16x8_ds    avx2/;
  specialize qw/aom_highbd_sad8x16_ds   avx2/;
  specialize qw/aom_highbd_sad8x8_ds    avx2/;
  specialize qw/aom_highbd_sad12x12_ds    avx2/;
  specialize qw/aom_highbd_sad20x20_ds    avx2/;
  specialize qw/aom_highbd_sad12x20_ds    avx2/;
  specialize qw/aom_highbd_sad20x12_ds    avx2/;
  specialize qw/aom_highbd_sad20x20   avx2/;
  specialize qw/aom_highbd_sad20x12   avx2/;
  specialize qw/aom_highbd_sad12x20   avx2/;
  specialize qw/aom_highbd_sad12x12   avx2/;
  specialize qw/aom_highbd_sad16x64   avx2/;
  specialize qw/aom_highbd_sad16x32   avx2/;
  specialize qw/aom_highbd_sad16x16   avx2/;
  specialize qw/aom_highbd_sad16x8    avx2/;
  specialize qw/aom_highbd_sad16x4    avx2/;

  specialize qw/aom_highbd_sad8x16         sse2/;
  specialize qw/aom_highbd_sad8x8          sse2/;
  specialize qw/aom_highbd_sad8x4          sse2/;
  specialize qw/aom_highbd_sad4x8          sse2/;
  specialize qw/aom_highbd_sad4x4          sse2/;

  specialize qw/aom_highbd_sad4x16         sse2/;
  specialize qw/aom_highbd_sad8x32         sse2/;
  specialize qw/aom_highbd_sad32x8    avx2 sse2/;
  specialize qw/aom_highbd_sad64x16   avx2 sse2/;

  specialize qw/aom_highbd_sad_skip_256x256 avx2/;
  specialize qw/aom_highbd_sad_skip_256x128 avx2/;
  specialize qw/aom_highbd_sad_skip_128x256 avx2/;
  specialize qw/aom_highbd_sad_skip_128x128 avx2/;
  specialize qw/aom_highbd_sad_skip_128x64  avx2/;
  specialize qw/aom_highbd_sad_skip_64x128  avx2/;
  specialize qw/aom_highbd_sad_skip_64x64   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_64x32   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x64   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x32   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x16   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_16x64   avx2/;
  specialize qw/aom_highbd_sad_skip_16x32   avx2/;
  specialize qw/aom_highbd_sad_skip_16x16   avx2/;
  specialize qw/aom_highbd_sad_skip_16x8    avx2/;

  specialize qw/aom_highbd_sad_skip_8x16         sse2/;
  specialize qw/aom_highbd_sad_skip_8x8          sse2/;
  specialize qw/aom_highbd_sad_skip_4x8          sse2/;

  specialize qw/aom_highbd_sad_skip_4x16         sse2/;
  specialize qw/aom_highbd_sad_skip_16x4         avx2/;
  specialize qw/aom_highbd_sad_skip_8x32         sse2/;
  specialize qw/aom_highbd_sad_skip_32x8    avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_64x16   avx2 sse2/;

  specialize qw/aom_highbd_sad256x256_avg avx2/;
  specialize qw/aom_highbd_sad256x128_avg avx2/;
  specialize qw/aom_highbd_sad128x256_avg avx2/;
  specialize qw/aom_highbd_sad128x128_avg avx2/;
  specialize qw/aom_highbd_sad128x64_avg  avx2/;
  specialize qw/aom_highbd_sad64x128_avg  avx2/;
  specialize qw/aom_highbd_sad64x64_avg   avx2 sse2/;
  specialize qw/aom_highbd_sad64x32_avg   avx2 sse2/;
  specialize qw/aom_highbd_sad32x64_avg   avx2 sse2/;
  specialize qw/aom_highbd_sad32x32_avg   avx2 sse2/;
  specialize qw/aom_highbd_sad32x16_avg   avx2 sse2/;
  specialize qw/aom_highbd_sad16x64_avg   avx2/;
  specialize qw/aom_highbd_sad16x32_avg   avx2/;
  specialize qw/aom_highbd_sad16x16_avg   avx2/;
  specialize qw/aom_highbd_sad16x8_avg    avx2/;
  specialize qw/aom_highbd_sad8x4_avg     sse2/;
  specialize qw/aom_highbd_sad4x8_avg     sse2/;
  specialize qw/aom_highbd_sad4x4_avg     sse2/;
  specialize qw/aom_highbd_sad4x16_avg    sse2/;
  specialize qw/aom_highbd_sad16x4_avg    avx2/;
  specialize qw/aom_highbd_sad8x32_avg    sse2/;
  specialize qw/aom_highbd_sad32x8_avg    avx2 sse2/;
  specialize qw/aom_highbd_sad64x16_avg   avx2 sse2/;

  specialize qw/aom_highbd_sad4x32       sse2/;
  specialize qw/aom_highbd_sad32x4       avx2 sse2/;
  specialize qw/aom_highbd_sad8x64       sse2/;
  specialize qw/aom_highbd_sad64x8       avx2 sse2/;
  specialize qw/aom_highbd_sad4x64       sse2/;
  specialize qw/aom_highbd_sad64x4       avx2 sse2/;

  specialize qw/aom_highbd_sad4x32_avg   sse2/;
  specialize qw/aom_highbd_sad32x4_avg   sse2/;
  specialize qw/aom_highbd_sad8x64_avg   sse2/;
  specialize qw/aom_highbd_sad64x8_avg   sse2/;
  specialize qw/aom_highbd_sad4x64_avg   sse2/;
  specialize qw/aom_highbd_sad64x4_avg   sse2/;

  specialize qw/aom_highbd_sad_skip_4x32   sse2/;
  specialize qw/aom_highbd_sad_skip_32x4   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_8x64   sse2/;
  specialize qw/aom_highbd_sad_skip_64x8   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_4x64   sse2/;
  specialize qw/aom_highbd_sad_skip_64x4   avx2 sse2/;

  #
  # Masked SAD
  #
  foreach (@block_sizes) {
    ($w, $h) = @$_;
    add_proto qw/unsigned int/, "aom_highbd_masked_sad${w}x${h}", "const uint16_t *src8, int src_stride, const uint16_t *ref8, int ref_stride, const uint16_t *second_pred8, const uint8_t *msk, int msk_stride, int invert_mask";
    # TODO(any): Add ssse3 optimization
    if ($w == 256 || $h == 256) {
      specialize "aom_highbd_masked_sad${w}x${h}", qw/avx2/;
    } else {
      specialize "aom_highbd_masked_sad${w}x${h}", qw/ssse3 avx2/;
    }
  }

  #
  # Multi-block SAD, comparing a reference to N independent blocks
  #
  foreach (@block_sizes) {
    ($w, $h) = @$_;
    add_proto qw/void/, "aom_highbd_sad${w}x${h}x4d", "const uint16_t *src_ptr, int src_stride, const uint16_t * const ref_ptr[], int ref_stride, uint32_t *sad_array";
    add_proto qw/void/, "aom_highbd_sad_skip_${w}x${h}x4d", "const uint16_t *src_ptr, int src_stride, const uint16_t * const ref_ptr[], int ref_stride, uint32_t *sad_array";
    if ($w != 128 && $h != 128 && $w != 256 && $h != 256) {
      specialize "aom_highbd_sad${w}x${h}x4d", qw/sse2/;
    }
  }
  specialize qw/aom_highbd_sad256x256x4d avx2/;
  specialize qw/aom_highbd_sad256x128x4d avx2/;
  specialize qw/aom_highbd_sad128x256x4d avx2/;
  specialize qw/aom_highbd_sad128x128x4d avx2/;
  specialize qw/aom_highbd_sad128x64x4d  avx2/;
  specialize qw/aom_highbd_sad64x128x4d  avx2/;
  specialize qw/aom_highbd_sad64x64x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad64x32x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad32x64x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad32x32x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad32x16x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad16x32x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad16x16x4d   sse2 avx2/;
  specialize qw/aom_highbd_sad16x8x4d    sse2 avx2/;
  specialize qw/aom_highbd_sad8x16x4d    sse2 avx2/;
  specialize qw/aom_highbd_sad8x8x4d     sse2 avx2/;
  specialize qw/aom_highbd_sad8x4x4d     sse2 avx2/;
  specialize qw/aom_highbd_sad4x8x4d     sse2/;
  specialize qw/aom_highbd_sad4x4x4d     sse2/;

  specialize qw/aom_highbd_sad4x16x4d         sse2/;
  specialize qw/aom_highbd_sad16x4x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad8x32x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad32x8x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad16x64x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad64x16x4d   avx2 sse2/;

  specialize qw/aom_highbd_sad_skip_256x256x4d avx2/;
  specialize qw/aom_highbd_sad_skip_256x128x4d avx2/;
  specialize qw/aom_highbd_sad_skip_128x256x4d avx2/;
  specialize qw/aom_highbd_sad_skip_128x128x4d avx2/;
  specialize qw/aom_highbd_sad_skip_128x64x4d  avx2/;
  specialize qw/aom_highbd_sad_skip_64x128x4d  avx2/;
  specialize qw/aom_highbd_sad_skip_64x64x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_64x32x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x64x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x32x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x16x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_16x32x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_16x16x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_16x8x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_8x16x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_8x8x4d     avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_4x8x4d          sse2/;

  specialize qw/aom_highbd_sad_skip_4x16x4d         sse2/;
  specialize qw/aom_highbd_sad_skip_16x4x4d         avx2/;
  specialize qw/aom_highbd_sad_skip_8x32x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_32x8x4d    avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_16x64x4d   avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_64x16x4d   avx2 sse2/;

  specialize qw/aom_highbd_sad4x32x4d  sse2/;
  specialize qw/aom_highbd_sad32x4x4d  avx2 sse2/;
  specialize qw/aom_highbd_sad8x64x4d  avx2 sse2/;
  specialize qw/aom_highbd_sad64x8x4d  avx2 sse2/;
  specialize qw/aom_highbd_sad4x64x4d  sse2/;
  specialize qw/aom_highbd_sad64x4x4d  avx2 sse2/;

  specialize qw/aom_highbd_sad_skip_4x32x4d  sse2/;
  specialize qw/aom_highbd_sad_skip_32x4x4d  avx2/;
  specialize qw/aom_highbd_sad_skip_8x64x4d  avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_64x8x4d  avx2 sse2/;
  specialize qw/aom_highbd_sad_skip_4x64x4d  sse2/;
  specialize qw/aom_highbd_sad_skip_64x4x4d  avx2/;

  #
  # Avg
  #
  add_proto qw/unsigned int aom_highbd_avg_8x8/, "const uint16_t *, int p";
  add_proto qw/unsigned int aom_highbd_avg_4x4/, "const uint16_t *, int p";
  add_proto qw/void aom_highbd_minmax_8x8/, "const uint16_t *s, int p, const uint16_t *d, int dp, int *min, int *max";

  add_proto qw/int aom_vector_var/, "const int16_t *ref, const int16_t *src, const int bwl";
  specialize qw/aom_vector_var neon/;
  # TODO(kyslov@) bring back SSE2 by extending it to 128 block size
  #specialize qw/aom_vector_var neon sse2/;

  #
  # hamadard transform and satd for implmenting temporal dependency model
  #
  add_proto qw/void aom_highbd_hadamard_8x8/, "const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff";
  specialize qw/aom_highbd_hadamard_8x8 avx2/;

  add_proto qw/void aom_highbd_hadamard_16x16/, "const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff";
  specialize qw/aom_highbd_hadamard_16x16 avx2/;

  add_proto qw/void aom_highbd_hadamard_32x32/, "const int16_t *src_diff, ptrdiff_t src_stride, tran_low_t *coeff";
  specialize qw/aom_highbd_hadamard_32x32 avx2/;

  add_proto qw/int aom_satd/, "const tran_low_t *coeff, int length";
  specialize qw/aom_satd neon avx2/;

  add_proto qw/int aom_satd_lp/, "const int16_t *coeff, int length";
  specialize qw/aom_satd_lp avx2 neon/;

  #
  # Structured Similarity (SSIM)
  #
  if (aom_config("CONFIG_INTERNAL_STATS") eq "yes") {
    add_proto qw/void aom_highbd_ssim_parms_8x8/, "const uint16_t *s, int sp, const uint16_t *r, int rp, uint32_t *sum_s, uint32_t *sum_r, uint32_t *sum_sq_s, uint32_t *sum_sq_r, uint32_t *sum_sxr";
  }
}  # CONFIG_AV1_ENCODER

if (aom_config("CONFIG_AV1_ENCODER") eq "yes") {
  add_proto qw/void aom_highbd_upsampled_pred/, "MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                                 const MV *const mv, uint16_t *comp_pred8, int width, int height, int subpel_x_q3,
                                                 int subpel_y_q3, const uint16_t *ref8, int ref_stride, int bd, int subpel_search,
						 int is_scaled_ref";
  specialize qw/aom_highbd_upsampled_pred sse2/;

  add_proto qw/void aom_highbd_comp_avg_upsampled_pred/, "MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                                          const MV *const mv, uint16_t *comp_pred8, const uint16_t *pred8, int width,
                                                          int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref8, int ref_stride,
							  int bd, int subpel_search, int is_scaled_ref";
  specialize qw/aom_highbd_comp_avg_upsampled_pred sse2/;

  add_proto qw/void aom_highbd_dist_wtd_comp_avg_upsampled_pred/, "MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                                              const MV *const mv, uint16_t *comp_pred8, const uint16_t *pred8, int width,
                                                              int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref8,
                                                              int ref_stride, int bd, const DIST_WTD_COMP_PARAMS *jcp_param, int subpel_search, int is_scaled_ref";
  specialize qw/aom_highbd_dist_wtd_comp_avg_upsampled_pred sse2/;

  add_proto qw/void aom_highbd_comp_mask_upsampled_pred/, "MACROBLOCKD *xd, const struct AV1Common *const cm, int mi_row, int mi_col,
                                                              const MV *const mv, uint16_t *comp_pred8, const uint16_t *pred8, int width,
                                                              int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref8,
                                                              int ref_stride, const uint8_t *mask, int mask_stride, int invert_mask,
                                                              int bd, int subpel_search, int is_scaled_ref";


  #
  #
  #
  add_proto qw/unsigned int aom_get_mb_ss/, "const int16_t *";

  specialize qw/aom_get_mb_ss sse2 msa/;

  #
  # Variance / Subpixel Variance / Subpixel Avg Variance
  #
  foreach $bd (8, 10, 12) {
    add_proto qw/unsigned int/, "aom_highbd_${bd}_variance2x2", "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

    add_proto qw/unsigned int/, "aom_highbd_${bd}_variance2x4", "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

    add_proto qw/unsigned int/, "aom_highbd_${bd}_variance4x2", "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

    foreach (@block_sizes) {
      ($w, $h) = @$_;
      add_proto qw/unsigned int/, "aom_highbd_${bd}_variance${w}x${h}", "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
      add_proto qw/uint32_t/, "aom_highbd_${bd}_sub_pixel_variance${w}x${h}", "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
      add_proto qw/uint32_t/, "aom_highbd_${bd}_sub_pixel_avg_variance${w}x${h}", "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
      if ($w != 128 && $h != 128 && $w != 4 && $h != 4 && $w != 256 && $h != 256) {
        specialize "aom_highbd_${bd}_variance${w}x${h}", "sse2";
      }
      # TODO(rachelbarker): When ext-partition-types is enabled, we currently
      # don't have vectorized highbd variance functions for block sizes corresponding
      # to width/height is 4 (excluding 4x4) with bd=12
      if ($w == 4 && $h == 4) {
        specialize "aom_highbd_${bd}_variance${w}x${h}", "sse4_1";
        specialize "aom_highbd_${bd}_sub_pixel_variance${w}x${h}", "sse4_1";
        specialize "aom_highbd_${bd}_sub_pixel_avg_variance${w}x${h}", "sse4_1";
      } elsif ($bd != 12 && ($h == 4 || $w == 4)) {
        specialize "aom_highbd_${bd}_variance${w}x${h}", "avx2";
      }


      if ($w != 128 && $h != 128 && $w != 4 && $w != 256 && $h != 256) {
        if (!($w == 16 && $h == 4) && !($w == 16 && $h == 8) && !($w == 16 && $h == 16) && !($w == 16 && $h == 32) && !($w == 16 && $h == 64)) {
          specialize "aom_highbd_${bd}_sub_pixel_variance${w}x${h}", qw/sse2/;
          specialize "aom_highbd_${bd}_sub_pixel_avg_variance${w}x${h}", qw/sse2/;
        }
      }

      add_proto qw/uint32_t/, "aom_highbd_${bd}_dist_wtd_sub_pixel_avg_variance${w}x${h}", "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred, const DIST_WTD_COMP_PARAMS* jcp_param";
    }
  }
  #
  # Masked Variance / Masked Subpixel Variance
  #
  foreach $bd ("_8_", "_10_", "_12_") {
    foreach (@block_sizes) {
      ($w, $h) = @$_;
      add_proto qw/unsigned int/, "aom_highbd${bd}masked_sub_pixel_variance${w}x${h}", "const uint16_t *src, int src_stride, int xoffset, int yoffset, const uint16_t *ref, int ref_stride, const uint16_t *second_pred, const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse";
      specialize "aom_highbd${bd}masked_sub_pixel_variance${w}x${h}", qw/ssse3/;
    }
  }

  #
  # Variance
  #
  add_proto qw/unsigned int aom_highbd_12_variance256x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance256x256 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance256x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance256x128 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance128x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance128x256 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance128x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance128x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance128x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance128x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance64x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance64x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance64x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance64x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance64x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance64x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance32x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance32x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance32x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance32x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance32x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance32x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance16x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance16x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance16x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance16x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance16x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance16x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance8x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance8x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance8x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance8x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance8x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_12_variance4x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_12_variance4x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

  add_proto qw/unsigned int aom_highbd_12_variance8x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance8x32 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_12_variance32x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance32x8 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_12_variance16x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance16x64 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_12_variance64x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance64x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance64x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance64x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_12_variance8x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_variance8x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance256x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance256x256 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance256x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance256x128 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance128x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance128x256 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance128x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance128x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance128x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance128x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance64x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance64x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance64x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance32x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance32x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance32x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance32x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance32x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance32x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance16x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance16x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance16x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance16x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance16x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance16x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance8x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance8x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance8x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance8x8 sse2 avx2/;


  add_proto qw/unsigned int aom_highbd_10_variance64x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance16x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance16x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance32x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance32x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance8x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance8x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance8x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_10_variance4x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_10_variance4x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

  add_proto qw/unsigned int aom_highbd_10_variance8x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance8x32 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_10_variance32x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance32x8 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_10_variance16x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance16x64 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_10_variance64x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance64x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance64x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_10_variance8x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_variance8x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance256x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance256x256 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance256x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance256x128 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance128x256/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance128x256 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance128x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance128x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance128x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance128x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance64x128/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance64x128 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance64x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance64x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance64x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance64x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance32x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance32x64 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance32x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance32x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance32x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance32x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance16x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance16x32 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance16x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance16x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance16x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance16x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance8x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance8x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance8x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance8x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance8x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_8_variance4x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_8_variance4x4/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";

  add_proto qw/unsigned int aom_highbd_8_variance8x32/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance8x32 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_8_variance32x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance32x8 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_8_variance16x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance16x64 sse2 avx2/;
  add_proto qw/unsigned int aom_highbd_8_variance64x16/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance64x16 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance64x8/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance64x8 sse2 avx2/;

  add_proto qw/unsigned int aom_highbd_8_variance8x64/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_variance8x64 sse2 avx2/;

  add_proto qw/void aom_highbd_8_get16x16var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";
  add_proto qw/void aom_highbd_8_get8x8var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";

  add_proto qw/void aom_highbd_10_get16x16var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";
  add_proto qw/void aom_highbd_10_get8x8var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";

  add_proto qw/void aom_highbd_12_get16x16var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";
  add_proto qw/void aom_highbd_12_get8x8var/, "const uint16_t *src_ptr, int source_stride, const uint16_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum";

  add_proto qw/unsigned int aom_highbd_8_mse16x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_mse16x16 sse2/;

  add_proto qw/unsigned int aom_highbd_8_mse16x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_8_mse8x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_8_mse8x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_8_mse8x8 sse2/;

  add_proto qw/unsigned int aom_highbd_10_mse16x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_mse16x16 sse2/;

  add_proto qw/unsigned int aom_highbd_10_mse16x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_10_mse8x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_10_mse8x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_10_mse8x8 sse2/;

  add_proto qw/unsigned int aom_highbd_12_mse16x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_mse16x16 sse2/;

  add_proto qw/unsigned int aom_highbd_12_mse16x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_12_mse8x16/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  add_proto qw/unsigned int aom_highbd_12_mse8x8/, "const uint16_t *src_ptr, int  source_stride, const uint16_t *ref_ptr, int  recon_stride, unsigned int *sse";
  specialize qw/aom_highbd_12_mse8x8 sse2/;

  add_proto qw/void aom_highbd_comp_avg_pred/, "uint16_t *comp_pred8, const uint16_t *pred8, int width, int height, const uint16_t *ref8, int ref_stride";

  add_proto qw/void aom_highbd_dist_wtd_comp_avg_pred/, "uint16_t *comp_pred8, const uint16_t *pred8, int width, int height, const uint16_t *ref8, int ref_stride, const DIST_WTD_COMP_PARAMS *jcp_param";
  specialize qw/aom_highbd_dist_wtd_comp_avg_pred sse2/;

  add_proto qw/uint64_t/, "aom_mse_wxh_16bit_highbd", "uint16_t *dst, int dstride,uint16_t *src, int sstride, int w, int h";
  specialize qw/aom_mse_wxh_16bit_highbd   sse2 avx2/;

  #
  # Subpixel Variance
  #
  specialize qw/aom_highbd_12_sub_pixel_variance256x256 sse2/;
  specialize qw/aom_highbd_12_sub_pixel_variance256x128 sse2/;
  specialize qw/aom_highbd_12_sub_pixel_variance128x256 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance128x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance128x128 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance128x128 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance128x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance128x64 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance128x64 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance64x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance64x128 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance64x128 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance64x64 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance64x64 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance64x32 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance64x32 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance32x64 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance32x64 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance32x32 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance32x32 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance32x16 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance32x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance16x32 avx2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance16x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_12_sub_pixel_variance16x8 avx2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_12_sub_pixel_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_12_sub_pixel_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_12_sub_pixel_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";


  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance64x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance64x16 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance64x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance16x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance16x64 avx2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance32x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  # specialize qw/aom_highbd_12_sub_pixel_variance32x8 sse2 avx2/;
  specialize qw/aom_highbd_12_sub_pixel_variance32x8 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_variance16x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_12_sub_pixel_variance16x4 avx2/;

  specialize qw/aom_highbd_10_sub_pixel_variance256x256 sse2/;
  specialize qw/aom_highbd_10_sub_pixel_variance256x128 sse2 avx2/;
  specialize qw/aom_highbd_10_sub_pixel_variance128x256 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance128x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance128x128 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance128x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance128x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance64x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance64x128 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance64x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance64x32 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance32x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance32x32 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance32x16 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance16x32 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance16x16 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance16x8 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";


  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance64x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance64x16 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance16x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance16x64 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance32x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance32x8 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_variance16x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_10_sub_pixel_variance16x4 avx2/;

  specialize qw/aom_highbd_8_sub_pixel_variance256x256 sse2/;
  specialize qw/aom_highbd_8_sub_pixel_variance256x128 sse2 avx2/;
  specialize qw/aom_highbd_8_sub_pixel_variance128x256 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance128x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance128x128 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance128x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance128x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance64x128/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance64x128 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance64x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance64x32 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance32x64 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance32x32 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance32x16 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance16x32 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance16x16 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance16x8 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";


  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance64x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance64x16 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance16x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance16x64 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance32x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance32x8 sse2 avx2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_variance16x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse";
  specialize qw/aom_highbd_8_sub_pixel_variance16x4 avx2/;

  #
  # Subpixel Avg Variance
  #

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance64x64 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance64x32 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance32x64 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance32x32 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance32x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_12_sub_pixel_avg_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  add_proto qw/uint32_t aom_highbd_12_sub_pixel_avg_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance64x64 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance64x32 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance32x64 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance32x32 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance32x16 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_10_sub_pixel_avg_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  add_proto qw/uint32_t aom_highbd_10_sub_pixel_avg_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance64x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance64x64 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance64x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance64x32 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance32x64/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance32x64 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance32x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance32x32 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance32x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance32x16 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance16x32/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance16x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance16x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance8x16/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance8x16 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance8x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance8x8 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance8x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  specialize qw/aom_highbd_8_sub_pixel_avg_variance8x4 sse2/;

  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance4x8/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";
  add_proto qw/uint32_t aom_highbd_8_sub_pixel_avg_variance4x4/, "const uint16_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint16_t *ref_ptr, int ref_stride, uint32_t *sse, const uint16_t *second_pred";

  add_proto qw/void aom_highbd_comp_mask_pred/, "uint16_t *comp_pred, const uint16_t *pred8, int width, int height, const uint16_t *ref8, int ref_stride, const uint8_t *mask, int mask_stride, int invert_mask";
  specialize qw/aom_highbd_comp_mask_pred sse2 avx2/;

  # Flow estimation library
  add_proto qw/bool aom_compute_mean_stddev/, "const unsigned char *frame, int stride, int x, int y, double *mean, double *one_over_stddev";
  specialize qw/aom_compute_mean_stddev sse4_1 avx2/;

  add_proto qw/double aom_compute_correlation/, "const unsigned char *frame1, int stride1, int x1, int y1, double mean1, double one_over_stddev1, const unsigned char *frame2, int stride2, int x2, int y2, double mean2, double one_over_stddev2";
  specialize qw/aom_compute_correlation sse4_1 avx2/;

  add_proto qw/void aom_compute_flow_at_point/, "const uint8_t *src, const uint8_t *ref, int x, int y, int width, int height, int stride, double *u, double *v";
  specialize qw/aom_compute_flow_at_point sse4_1 avx2 neon/;
}  # CONFIG_AV1_ENCODER

1;
