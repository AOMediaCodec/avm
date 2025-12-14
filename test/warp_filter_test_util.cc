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
#include "avm_ports/avm_timer.h"
#include "test/warp_filter_test_util.h"

using std::make_tuple;
using std::tuple;

namespace libavm_test {

int32_t random_warped_param(libavm_test::ACMRandom *rnd, int bits) {
  // 1 in 32 chance of generating zero (arbitrarily chosen)
  if (((rnd->Rand8()) & 0x1f) == 0) return 0;
  // Otherwise, enerate uniform values in the range
  // [-(1 << bits), 1] U [1, 1<<bits]
  int32_t v = 1 + (rnd->Rand16() & ((1 << bits) - 1));
  if ((rnd->Rand8()) & 1) return -v;
  return v;
}

void generate_warped_model(libavm_test::ACMRandom *rnd, int32_t *mat,
                           int16_t *alpha, int16_t *beta, int16_t *gamma,
                           int16_t *delta, const int is_alpha_zero,
                           const int is_beta_zero, const int is_gamma_zero,
                           const int is_delta_zero) {
  while (1) {
    int rnd8 = rnd->Rand8() & 3;
    mat[0] = random_warped_param(rnd, WARPEDMODEL_PREC_BITS + 6);
    mat[1] = random_warped_param(rnd, WARPEDMODEL_PREC_BITS + 6);
    mat[2] = (random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3)) +
             (1 << WARPEDMODEL_PREC_BITS);
    mat[3] = random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3);

    if (is_alpha_zero == 1) mat[2] = 1 << WARPEDMODEL_PREC_BITS;
    if (is_beta_zero == 1) mat[3] = 0;
    if (rnd8 <= 1) {
      // AFFINE
      mat[4] = random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3);
      mat[5] = (random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3)) +
               (1 << WARPEDMODEL_PREC_BITS);
    } else if (rnd8 == 2) {
      mat[4] = -mat[3];
      mat[5] = mat[2];
    } else {
      mat[4] = random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3);
      mat[5] = (random_warped_param(rnd, WARPEDMODEL_PREC_BITS - 3)) +
               (1 << WARPEDMODEL_PREC_BITS);
    }
    if (is_gamma_zero == 1) mat[4] = 0;
    if (is_delta_zero == 1)
      mat[5] = static_cast<int32_t>(
          ((static_cast<int64_t>(mat[3]) * mat[4] + (mat[2] / 2)) / mat[2]) +
          (1 << WARPEDMODEL_PREC_BITS));

    // Calculate the derived parameters and check that they are suitable
    // for the warp filter.
    assert(mat[2] != 0);

    *alpha = clamp(mat[2] - (1 << WARPEDMODEL_PREC_BITS), INT16_MIN, INT16_MAX);
    *beta = clamp(mat[3], INT16_MIN, INT16_MAX);
    *gamma = static_cast<int16_t>(clamp64(
        (static_cast<int64_t>(mat[4]) * (1 << WARPEDMODEL_PREC_BITS)) / mat[2],
        INT16_MIN, INT16_MAX));
    *delta = static_cast<int16_t>(clamp64(
        mat[5] -
            ((static_cast<int64_t>(mat[3]) * mat[4] + (mat[2] / 2)) / mat[2]) -
            (1 << WARPEDMODEL_PREC_BITS),
        INT16_MIN, INT16_MAX));

    if ((4 * abs(*alpha) + 7 * abs(*beta) >= (3 << WARPEDMODEL_PREC_BITS)) ||
        (4 * abs(*gamma) + 4 * abs(*delta) >= (3 << WARPEDMODEL_PREC_BITS)))
      continue;

    *alpha = ROUND_POWER_OF_TWO_SIGNED(*alpha, WARP_PARAM_REDUCE_BITS) *
             (1 << WARP_PARAM_REDUCE_BITS);
    *beta = ROUND_POWER_OF_TWO_SIGNED(*beta, WARP_PARAM_REDUCE_BITS) *
            (1 << WARP_PARAM_REDUCE_BITS);
    *gamma = ROUND_POWER_OF_TWO_SIGNED(*gamma, WARP_PARAM_REDUCE_BITS) *
             (1 << WARP_PARAM_REDUCE_BITS);
    *delta = ROUND_POWER_OF_TWO_SIGNED(*delta, WARP_PARAM_REDUCE_BITS) *
             (1 << WARP_PARAM_REDUCE_BITS);

    // We have a valid model, so finish
    return;
  }
}

namespace AV2HighbdWarpFilter {
::testing::internal::ParamGenerator<HighbdWarpTestParams> BuildParams(
    highbd_warp_affine_func filter) {
  const HighbdWarpTestParam params[] = {
    make_tuple(4, 4, 100, 8, filter),    make_tuple(8, 8, 100, 8, filter),
    make_tuple(64, 64, 100, 8, filter),  make_tuple(4, 16, 100, 8, filter),
    make_tuple(32, 8, 100, 8, filter),   make_tuple(4, 4, 100, 10, filter),
    make_tuple(8, 8, 100, 10, filter),   make_tuple(64, 64, 100, 10, filter),
    make_tuple(4, 16, 100, 10, filter),  make_tuple(32, 8, 100, 10, filter),
    make_tuple(4, 4, 100, 12, filter),   make_tuple(8, 8, 100, 12, filter),
    make_tuple(64, 64, 100, 12, filter), make_tuple(4, 16, 100, 12, filter),
    make_tuple(32, 8, 100, 12, filter),
  };
  return ::testing::Combine(::testing::ValuesIn(params),
                            ::testing::Values(0, 1), ::testing::Values(0, 1),
                            ::testing::Values(0, 1), ::testing::Values(0, 1));
}

AV2HighbdWarpFilterTest::~AV2HighbdWarpFilterTest() {}
void AV2HighbdWarpFilterTest::SetUp() {
  rnd_.Reset(ACMRandom::DeterministicSeed());
}

void AV2HighbdWarpFilterTest::TearDown() { libavm_test::ClearSystemState(); }

void AV2HighbdWarpFilterTest::RunSpeedTest(highbd_warp_affine_func test_impl) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  HighbdWarpTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = std::get<0>(param), out_h = std::get<1>(param);
  const int bd = std::get<3>(param);
  const int mask = (1 << bd) - 1;
  int sub_x, sub_y, p_row, p_col;

  // The warp functions always write rows with widths that are multiples of 8.
  // So to avoid a buffer overflow, we may need to pad rows to a multiple of 8.
  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride];
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n];
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;
  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  CONV_BUF_TYPE *dsta = new CONV_BUF_TYPE[output_n];

  generate_warped_model(&rnd_, mat, &alpha, &beta, &gamma, &delta,
                        is_alpha_zero, is_beta_zero, is_gamma_zero,
                        is_delta_zero);
  // Generate an input block and extend its borders horizontally
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < border; ++c) {
      input[r * stride - border + c] = input[r * stride];
      input[r * stride + w + c] = input[r * stride + (w - 1)];
    }
  }

  sub_x = 0;
  sub_y = 0;
  p_row = 32;
  p_col = 32;
  int do_average = 0;
  conv_params = get_conv_params_no_round(do_average, 0, dsta, out_w, 1, bd);

  const int num_loops = 1000000000 / (out_w + out_h);
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);

  for (int i = 0; i < num_loops; ++i)
    test_impl(mat, input, w, h, stride, output, p_col, p_row, out_w, out_h,
              out_w, sub_x, sub_y, bd, &conv_params, alpha, beta, gamma, delta);

  avm_usec_timer_mark(&timer);
  const int elapsed_time1 = static_cast<int>(avm_usec_timer_elapsed(&timer));

  printf("highbd warp %3dx%-3d: %7.2f ns\n", out_w, out_h,
         1000.0 * elapsed_time1 / num_loops);

  delete[] input_;
  delete[] output;
  delete[] dsta;
}

void AV2HighbdWarpFilterTest::RunCheckOutput(
    highbd_warp_affine_func test_impl) {
  const int w = 128, h = 128;
  const int p_row = 32, p_col = 32;
  const int border = 16;
  const int stride = w + 2 * border;
  HighbdWarpTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = std::get<0>(param), out_h = std::get<1>(param);
  const int bd = std::get<3>(param);
  const int num_iters = std::get<2>(param);
  const int mask = (1 << bd) - 1;
  int i, j, sub_x, sub_y;

  // The warp functions always write rows with widths that are multiples of 8.
  // So to avoid a buffer overflow, we may need to pad rows to a multiple of 8.
  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride];
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n];
  uint16_t *output2 = new uint16_t[output_n];
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;
  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  CONV_BUF_TYPE *dsta = new CONV_BUF_TYPE[output_n];
  CONV_BUF_TYPE *dstb = new CONV_BUF_TYPE[output_n];
  for (int i = 0; i < output_n; ++i) output[i] = output2[i] = rnd_.Rand16();

  for (i = 0; i < num_iters; ++i) {
    // Generate an input block and extend its borders horizontally
    for (int r = 0; r < h; ++r)
      for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
    for (int r = 0; r < h; ++r) {
      for (int c = 0; c < border; ++c) {
        input[r * stride - border + c] = input[r * stride];
        input[r * stride + w + c] = input[r * stride + (w - 1)];
      }
    }
    const int use_no_round = rnd_.Rand8() & 1;
    for (sub_x = 0; sub_x < 2; ++sub_x)
      for (sub_y = 0; sub_y < 2; ++sub_y) {
        generate_warped_model(&rnd_, mat, &alpha, &beta, &gamma, &delta,
                              is_alpha_zero, is_beta_zero, is_gamma_zero,
                              is_delta_zero);
        for (int ii = 0; ii < 2; ++ii) {
          for (int jj = 0; jj < 5; ++jj) {
            for (int do_average = 0; do_average <= 1; ++do_average) {
              if (use_no_round) {
                conv_params =
                    get_conv_params_no_round(do_average, 0, dsta, out_w, 1, bd);
              } else {
                conv_params = get_conv_params(0, 0, bd);
              }
              if (jj >= 4) {
              } else {
                conv_params.fwd_offset = quant_dist_lookup_table[jj][ii];
                conv_params.bck_offset = quant_dist_lookup_table[jj][1 - ii];
              }

              av2_highbd_warp_affine_c(mat, input, w, h, stride, output, p_col,
                                       p_row, out_w, out_h, out_w, sub_x, sub_y,
                                       bd, &conv_params, alpha, beta, gamma,
                                       delta);
              if (use_no_round) {
                // TODO(angiebird): Change this to test_impl once we have SIMD
                // implementation
                conv_params =
                    get_conv_params_no_round(do_average, 0, dstb, out_w, 1, bd);
              }
              if (jj >= 4) {
              } else {
                conv_params.fwd_offset = quant_dist_lookup_table[jj][ii];
                conv_params.bck_offset = quant_dist_lookup_table[jj][1 - ii];
              }
              test_impl(mat, input, w, h, stride, output2, p_col, p_row, out_w,
                        out_h, out_w, sub_x, sub_y, bd, &conv_params, alpha,
                        beta, gamma, delta);

              if (use_no_round) {
                for (j = 0; j < out_w * out_h; ++j)
                  ASSERT_EQ(dsta[j], dstb[j])
                      << "Pixel mismatch at index " << j << " = ("
                      << (j % out_w) << ", " << (j / out_w) << ") on iteration "
                      << i;
                for (j = 0; j < out_w * out_h; ++j)
                  ASSERT_EQ(output[j], output2[j])
                      << "Pixel mismatch at index " << j << " = ("
                      << (j % out_w) << ", " << (j / out_w) << ") on iteration "
                      << i;
              } else {
                for (j = 0; j < out_w * out_h; ++j)
                  ASSERT_EQ(output[j], output2[j])
                      << "Pixel mismatch at index " << j << " = ("
                      << (j % out_w) << ", " << (j / out_w) << ") on iteration "
                      << i;
              }
            }
          }
        }
      }
  }

  delete[] input_;
  delete[] output;
  delete[] output2;
  delete[] dsta;
  delete[] dstb;
}
}  // namespace AV2HighbdWarpFilter

namespace AV2ExtHighbdWarpFilter {
::testing::internal::ParamGenerator<ExtHighbdWarpTestParams> BuildParams(
    ext_highbd_warp_affine_func filter) {
  const ExtHighbdWarpTestParam params[] = {
    make_tuple(4, 4, 100, 8, filter),    make_tuple(8, 8, 100, 8, filter),
    make_tuple(64, 64, 100, 8, filter),  make_tuple(4, 16, 100, 8, filter),
    make_tuple(32, 8, 100, 8, filter),   make_tuple(4, 4, 100, 10, filter),
    make_tuple(8, 8, 100, 10, filter),   make_tuple(64, 64, 100, 10, filter),
    make_tuple(4, 16, 100, 10, filter),  make_tuple(32, 8, 100, 10, filter),
    make_tuple(4, 4, 100, 12, filter),   make_tuple(8, 8, 100, 12, filter),
    make_tuple(64, 64, 100, 12, filter), make_tuple(4, 16, 100, 12, filter),
    make_tuple(32, 8, 100, 12, filter),
  };
  return ::testing::Combine(::testing::ValuesIn(params),
                            ::testing::Values(0, 1), ::testing::Values(0, 1),
                            ::testing::Values(0, 1), ::testing::Values(0, 1));
}

AV2ExtHighbdWarpFilterTest::~AV2ExtHighbdWarpFilterTest() {}
void AV2ExtHighbdWarpFilterTest::SetUp() {
  rnd_.Reset(ACMRandom::DeterministicSeed());
}

void AV2ExtHighbdWarpFilterTest::TearDown() { libavm_test::ClearSystemState(); }

void AV2ExtHighbdWarpFilterTest::RunSpeedTest(
    ext_highbd_warp_affine_func test_impl) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  ExtHighbdWarpTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  const int mask = (1 << bd) - 1;
  int sub_x, sub_y;

  // The warp functions always write rows with widths that are multiples of 8.
  // So to avoid a buffer overflow, we may need to pad rows to a multiple of 8.
  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride];
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n];
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;
  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  CONV_BUF_TYPE *dsta = new CONV_BUF_TYPE[output_n];

  generate_warped_model(&rnd_, mat, &alpha, &beta, &gamma, &delta,
                        is_alpha_zero, is_beta_zero, is_gamma_zero,
                        is_delta_zero);
  // Generate an input block and extend its borders horizontally
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < border; ++c) {
      input[r * stride - border + c] = input[r * stride];
      input[r * stride + w + c] = input[r * stride + (w - 1)];
    }
  }

  sub_x = 0;
  sub_y = 0;
  int do_average = 0;
  conv_params = get_conv_params_no_round(do_average, 0, dsta, out_w, 1, bd);

  const int num_loops = 1000000000 / (out_w + out_h);
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);

  for (int i = 0; i < num_loops; ++i)
    test_impl(mat, input, w, h, stride, output, 32, 32, out_w, out_h, out_w,
              sub_x, sub_y, bd, &conv_params, 0, NULL);

  avm_usec_timer_mark(&timer);
  const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));
  printf("highbd warp %3dx%-3d: %7.2f ns\n", out_w, out_h,
         1000.0 * elapsed_time / num_loops);

  delete[] input_;
  delete[] output;
  delete[] dsta;
}

void AV2ExtHighbdWarpFilterTest::RunCheckOutput(
    ext_highbd_warp_affine_func test_impl) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  ExtHighbdWarpTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  const int num_iters = ::testing::get<2>(param);
  const int mask = (1 << bd) - 1;
  int i, j, sub_x, sub_y;

  // The warp functions always write rows with widths that are multiples of 8.
  // So to avoid a buffer overflow, we may need to pad rows to a multiple of 8.
  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride];
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n];
  uint16_t *output2 = new uint16_t[output_n];
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;
  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  CONV_BUF_TYPE *dsta = new CONV_BUF_TYPE[output_n];
  CONV_BUF_TYPE *dstb = new CONV_BUF_TYPE[output_n];
  for (int i = 0; i < output_n; ++i) output[i] = output2[i] = rnd_.Rand16();

  for (i = 0; i < num_iters; ++i) {
    // Generate an input block and extend its borders horizontally
    for (int r = 0; r < h; ++r)
      for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
    for (int r = 0; r < h; ++r) {
      for (int c = 0; c < border; ++c) {
        input[r * stride - border + c] = input[r * stride];
        input[r * stride + w + c] = input[r * stride + (w - 1)];
      }
    }
    const int use_no_round = rnd_.Rand8() & 1;
    for (sub_x = 0; sub_x < 2; ++sub_x)
      for (sub_y = 0; sub_y < 2; ++sub_y) {
        generate_warped_model(&rnd_, mat, &alpha, &beta, &gamma, &delta,
                              is_alpha_zero, is_beta_zero, is_gamma_zero,
                              is_delta_zero);
        for (int ii = 0; ii < 4; ++ii) {
          for (int do_average = 0; do_average <= 1; ++do_average) {
            if (use_no_round) {
              conv_params =
                  get_conv_params_no_round(do_average, 0, dsta, out_w, 1, bd);
            } else {
              conv_params = get_conv_params(0, 0, bd);
            }
            conv_params.fwd_offset = quant_dist_lookup_table[ii][0];
            conv_params.bck_offset = quant_dist_lookup_table[ii][1];

            av2_ext_highbd_warp_affine_c(mat, input, w, h, stride, output, 32,
                                         32, out_w, out_h, out_w, sub_x, sub_y,
                                         bd, &conv_params, 0, NULL);
            if (use_no_round) {
              // TODO(angiebird): Change this to test_impl once we have SIMD
              // implementation
              conv_params =
                  get_conv_params_no_round(do_average, 0, dstb, out_w, 1, bd);
            }
            conv_params.fwd_offset = quant_dist_lookup_table[ii][0];
            conv_params.bck_offset = quant_dist_lookup_table[ii][1];
            test_impl(mat, input, w, h, stride, output2, 32, 32, out_w, out_h,
                      out_w, sub_x, sub_y, bd, &conv_params, 0, NULL);

            if (use_no_round) {
              for (j = 0; j < out_w * out_h; ++j)
                ASSERT_EQ(dsta[j], dstb[j])
                    << "Pixel mismatch at index " << j << " = (" << (j % out_w)
                    << ", " << (j / out_w) << ") on iteration " << i;
              for (j = 0; j < out_w * out_h; ++j)
                ASSERT_EQ(output[j], output2[j])
                    << "Pixel mismatch at index " << j << " = (" << (j % out_w)
                    << ", " << (j / out_w) << ") on iteration " << i;
            } else {
              for (j = 0; j < out_w * out_h; ++j)
                ASSERT_EQ(output[j], output2[j])
                    << "Pixel mismatch at index " << j << " = (" << (j % out_w)
                    << ", " << (j / out_w) << ") on iteration " << i;
            }
          }
        }
      }
  }

  delete[] input_;
  delete[] output;
  delete[] output2;
  delete[] dsta;
  delete[] dstb;
}
}  // namespace AV2ExtHighbdWarpFilter
}  // namespace libavm_test
