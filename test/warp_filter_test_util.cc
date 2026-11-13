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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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

namespace {
template <typename RefFn, typename TestFn>
void RunBoundaryCheckImpl(int out_w, int out_h, int bd, int is_alpha_zero,
                          int is_beta_zero, int is_gamma_zero,
                          int is_delta_zero, int mode,
                          libavm_test::ACMRandom *rnd, RefFn ref_fn,
                          TestFn test_fn) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  const int mask = (1 << bd) - 1;

  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride]{};
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n]{};
  uint16_t *output2 = new uint16_t[output_n]{};
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;

  generate_warped_model(rnd, mat, &alpha, &beta, &gamma, &delta, is_alpha_zero,
                        is_beta_zero, is_gamma_zero, is_delta_zero);

  uint16_t fill_val = 0;
  if (mode == 0)
    fill_val = 0;
  else if (mode == 1)
    fill_val = mask;
  else
    fill_val = (1 << (bd - 1));

  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) input[r * stride + c] = fill_val;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < border; ++c) {
      input[r * stride - border + c] = fill_val;
      input[r * stride + w + c] = fill_val;
    }
  }

  ConvolveParams conv_params = get_conv_params(0, 0, bd);

  ref_fn(mat, input, w, h, stride, output, out_w, out_h, bd, &conv_params,
         alpha, beta, gamma, delta);
  test_fn(mat, input, w, h, stride, output2, out_w, out_h, bd, &conv_params,
          alpha, beta, gamma, delta);

  for (int j = 0; j < out_w * out_h; ++j) {
    ASSERT_EQ(output[j], output2[j])
        << "mismatch at index " << j << " = (" << (j % out_w) << ", "
        << (j / out_w) << ") w=" << out_w << " h=" << out_h << " bd=" << bd
        << " mode=" << mode;
  }

  delete[] input_;
  delete[] output;
  delete[] output2;
}
}  // namespace

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
              ASM_REGISTER_STATE_CHECK(
                  test_impl(mat, input, w, h, stride, output2, p_col, p_row,
                            out_w, out_h, out_w, sub_x, sub_y, bd, &conv_params,
                            alpha, beta, gamma, delta));

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

void AV2HighbdWarpFilterTest::RunBoundaryCheck(
    highbd_warp_affine_func test_impl, int mode) {
  HighbdWarpTestParam param = GET_PARAM(0);
  const int out_w = std::get<0>(param), out_h = std::get<1>(param);
  const int bd = std::get<3>(param);
  RunBoundaryCheckImpl(
      out_w, out_h, bd, GET_PARAM(1), GET_PARAM(2), GET_PARAM(3), GET_PARAM(4),
      mode, &rnd_,
      [](const int32_t *mat, const uint16_t *input, int w, int h, int stride,
         uint16_t *output, int out_w, int out_h, int bd,
         ConvolveParams *conv_params, int16_t alpha, int16_t beta,
         int16_t gamma, int16_t delta) {
        av2_highbd_warp_affine_c(mat, input, w, h, stride, output, 32, 32,
                                 out_w, out_h, out_w, 0, 0, bd, conv_params,
                                 alpha, beta, gamma, delta);
      },
      [test_impl](const int32_t *mat, const uint16_t *input, int w, int h,
                  int stride, uint16_t *output, int out_w, int out_h, int bd,
                  ConvolveParams *conv_params, int16_t alpha, int16_t beta,
                  int16_t gamma, int16_t delta) {
        ASM_REGISTER_STATE_CHECK(
            test_impl(mat, input, w, h, stride, output, 32, 32, out_w, out_h,
                      out_w, 0, 0, bd, conv_params, alpha, beta, gamma, delta));
      });
}

namespace {
enum { kStoreSingle, kStoreComp, kStoreCompAvg, kNumStoreModes };
const char *const kStoreModeNames[kNumStoreModes] = { "single", "comp",
                                                      "compavg" };

// Codec uses the ext path for w or h < 8.
const std::tuple<int, int> kSpeedCompareSizes[] = {
  make_tuple(8, 8),   make_tuple(16, 8),    make_tuple(8, 16),
  make_tuple(16, 16), make_tuple(32, 8),    make_tuple(32, 32),
  make_tuple(64, 64), make_tuple(128, 128),
};

struct SpeedCompareRecord {
  int w, h, store, nz;
  double base_ns, test_ns;
};
std::vector<SpeedCompareRecord> g_speed_compare_records;

char NzChar(int nz, int bit) { return (nz & bit) ? 'N' : 'Z'; }

// Also rejects params that are zero by chance.
bool GenerateExactWarpedModel(ACMRandom *rnd, int32_t *mat, int16_t *alpha,
                              int16_t *beta, int16_t *gamma, int16_t *delta,
                              int nz) {
  const bool az = !(nz & 8), bz = !(nz & 4), gz = !(nz & 2), dz = !(nz & 1);
  for (int t = 0; t < 10000; ++t) {
    generate_warped_model(rnd, mat, alpha, beta, gamma, delta, az, bz, gz, dz);
    if ((*alpha == 0) == az && (*beta == 0) == bz && (*gamma == 0) == gz &&
        (*delta == 0) == dz)
      return true;
  }
  return false;
}

ConvolveParams SpeedCompareConvParams(int store, CONV_BUF_TYPE *dst,
                                      int dst_stride, int bd) {
  if (store == kStoreSingle) return get_conv_params(0, 0, bd);
  return get_conv_params_no_round(store == kStoreCompAvg, 0, dst, dst_stride, 1,
                                  bd);
}

struct GeoStat {
  std::string key;
  double log_sum;
  int n, faster;
};

void PrintGeoGroup(const char *title,
                   std::string (*key_fn)(const SpeedCompareRecord &)) {
  std::vector<GeoStat> stats;
  for (const SpeedCompareRecord &r : g_speed_compare_records) {
    const std::string key = key_fn(r);
    size_t i = 0;
    while (i < stats.size() && stats[i].key != key) ++i;
    if (i == stats.size()) stats.push_back({ key, 0.0, 0, 0 });
    stats[i].log_sum += std::log(r.base_ns / r.test_ns);
    stats[i].n++;
    stats[i].faster += r.test_ns < r.base_ns;
  }
  printf("  by %s:\n", title);
  for (const GeoStat &s : stats) {
    printf("    %-10s n=%-4d speedup %.2fx  faster %d/%d\n", s.key.c_str(), s.n,
           std::exp(s.log_sum / s.n), s.faster, s.n);
  }
}

std::string KeyAll(const SpeedCompareRecord &) { return "all"; }
std::string KeySize(const SpeedCompareRecord &r) {
  return std::to_string(r.w) + "x" + std::to_string(r.h);
}
std::string KeyStore(const SpeedCompareRecord &r) {
  return kStoreModeNames[r.store];
}
std::string KeyAlphaBeta(const SpeedCompareRecord &r) {
  return std::string("a") + NzChar(r.nz, 8) + "b" + NzChar(r.nz, 4);
}
}  // namespace

::testing::internal::ParamGenerator<HighbdWarpSpeedCompareParams>
BuildSpeedCompareParams(highbd_warp_affine_func baseline,
                        highbd_warp_affine_func test) {
  return ::testing::Combine(
      ::testing::Values(make_tuple(baseline, test)),
      ::testing::ValuesIn(kSpeedCompareSizes),
      ::testing::Range(0, static_cast<int>(kNumStoreModes)),
      ::testing::Range(0, 16));
}

std::string SpeedCompareParamName(
    const ::testing::TestParamInfo<HighbdWarpSpeedCompareParams> &info) {
  const std::tuple<int, int> size = std::get<1>(info.param);
  const int nz = std::get<3>(info.param);
  char name[64];
  snprintf(name, sizeof(name), "%dx%d_%s_a%cb%cg%cd%c", std::get<0>(size),
           std::get<1>(size), kStoreModeNames[std::get<2>(info.param)],
           NzChar(nz, 8), NzChar(nz, 4), NzChar(nz, 2), NzChar(nz, 1));
  return name;
}

AV2HighbdWarpSpeedCompareTest::~AV2HighbdWarpSpeedCompareTest() {}
void AV2HighbdWarpSpeedCompareTest::SetUp() {
  rnd_.Reset(ACMRandom::DeterministicSeed());
}
void AV2HighbdWarpSpeedCompareTest::TearDown() {
  libavm_test::ClearSystemState();
}

void AV2HighbdWarpSpeedCompareTest::TearDownTestSuite() {
  if (g_speed_compare_records.empty()) return;
  printf("\n==== warp speed compare (speedup = base/test time) ====\n");
  PrintGeoGroup("all", KeyAll);
  PrintGeoGroup("size", KeySize);
  PrintGeoGroup("store", KeyStore);
  PrintGeoGroup("alpha/beta", KeyAlphaBeta);
  g_speed_compare_records.clear();
}

void AV2HighbdWarpSpeedCompareTest::RunSpeedCompare() {
  const highbd_warp_affine_func base_fn = std::get<0>(GET_PARAM(0));
  const highbd_warp_affine_func test_fn = std::get<1>(GET_PARAM(0));
  const int out_w = std::get<0>(GET_PARAM(1));
  const int out_h = std::get<1>(GET_PARAM(1));
  const int store = GET_PARAM(2);
  const int nz = GET_PARAM(3);
  const int bd = 10;
  // 128x128 at (64, 64) stays mostly interior
  const int w = 256, h = 256, border = 16;
  const int stride = w + 2 * border;
  const int p_row = 64, p_col = 64;
  const int mask = (1 << bd) - 1;
  // pow2: m = i & (kModels - 1)
  const int kModels = 8;
  const int kReps = 3;
  const int out_n = out_w * out_h;

  std::vector<uint16_t> input_buf(h * stride);
  uint16_t *input = input_buf.data() + border;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
    for (int c = 0; c < border; ++c) {
      input[r * stride - border + c] = input[r * stride];
      input[r * stride + w + c] = input[r * stride + (w - 1)];
    }
  }

  int32_t mat[kModels][8];
  int16_t alpha[kModels], beta[kModels], gamma[kModels], delta[kModels];
  for (int m = 0; m < kModels; ++m) {
    ASSERT_TRUE(GenerateExactWarpedModel(&rnd_, mat[m], &alpha[m], &beta[m],
                                         &gamma[m], &delta[m], nz))
        << "no warp model for nonzero mask " << nz;
  }

  std::vector<uint16_t> pred_ref(out_n), pred_base(out_n), pred_test(out_n);
  std::vector<CONV_BUF_TYPE> dst_init(out_n), dst_ref(out_n), dst_base(out_n),
      dst_test(out_n);

  // compavg reads the first prediction from dst
  ConvolveParams conv_first =
      SpeedCompareConvParams(kStoreComp, dst_init.data(), out_w, bd);
  av2_highbd_warp_affine_c(mat[0], input, w, h, stride, pred_ref.data(), p_col,
                           p_row, out_w, out_h, out_w, 0, 0, bd, &conv_first,
                           alpha[0], beta[0], gamma[0], delta[0]);

  auto run_once = [&](highbd_warp_affine_func fn, int m,
                      std::vector<uint16_t> &pred,
                      std::vector<CONV_BUF_TYPE> &dst) {
    dst = dst_init;
    std::fill(pred.begin(), pred.end(), 0);
    ConvolveParams conv = SpeedCompareConvParams(store, dst.data(), out_w, bd);
    fn(mat[m], input, w, h, stride, pred.data(), p_col, p_row, out_w, out_h,
       out_w, 0, 0, bd, &conv, alpha[m], beta[m], gamma[m], delta[m]);
  };

  for (int m = 0; m < kModels; ++m) {
    run_once(av2_highbd_warp_affine_c, m, pred_ref, dst_ref);
    run_once(base_fn, m, pred_base, dst_base);
    run_once(test_fn, m, pred_test, dst_test);
    for (int j = 0; j < out_n; ++j) {
      ASSERT_EQ(pred_ref[j], pred_base[j])
          << "baseline pred, model " << m << " idx " << j;
      ASSERT_EQ(dst_ref[j], dst_base[j])
          << "baseline dst, model " << m << " idx " << j;
      ASSERT_EQ(pred_ref[j], pred_test[j])
          << "test pred, model " << m << " idx " << j;
      ASSERT_EQ(dst_ref[j], dst_test[j])
          << "test dst, model " << m << " idx " << j;
    }
  }

  // AVM_WARP_SPEED_SCALE: pixel budget multiplier (default 1)
  const char *scale_env = getenv("AVM_WARP_SPEED_SCALE");
  const int scale = scale_env ? std::max(1, atoi(scale_env)) : 1;
  const int loops = std::max(kModels, scale * (1 << 23) / out_n);

  auto time_fn = [&](highbd_warp_affine_func fn, std::vector<uint16_t> &pred,
                     std::vector<CONV_BUF_TYPE> &dst) {
    dst = dst_init;
    ConvolveParams conv = SpeedCompareConvParams(store, dst.data(), out_w, bd);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < loops; ++i) {
      const int m = i & (kModels - 1);
      fn(mat[m], input, w, h, stride, pred.data(), p_col, p_row, out_w, out_h,
         out_w, 0, 0, bd, &conv, alpha[m], beta[m], gamma[m], delta[m]);
    }
    avm_usec_timer_mark(&timer);
    return static_cast<double>(avm_usec_timer_elapsed(&timer));
  };

  // min over reps, alternating order to cancel drift
  double base_us = 1e30, test_us = 1e30;
  for (int r = 0; r < kReps; ++r) {
    if (r & 1) {
      test_us = std::min(test_us, time_fn(test_fn, pred_test, dst_test));
      base_us = std::min(base_us, time_fn(base_fn, pred_base, dst_base));
    } else {
      base_us = std::min(base_us, time_fn(base_fn, pred_base, dst_base));
      test_us = std::min(test_us, time_fn(test_fn, pred_test, dst_test));
    }
  }

  const double base_ns = 1000.0 * base_us / loops;
  const double test_ns = 1000.0 * test_us / loops;
  printf(
      "warp-cmp %3dx%-3d %-7s a%cb%cg%cd%c  base %9.2f ns  test %9.2f ns  "
      "speedup %.2fx\n",
      out_w, out_h, kStoreModeNames[store], NzChar(nz, 8), NzChar(nz, 4),
      NzChar(nz, 2), NzChar(nz, 1), base_ns, test_ns, base_ns / test_ns);
  g_speed_compare_records.push_back(
      { out_w, out_h, store, nz, base_ns, test_ns });
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
            ASM_REGISTER_STATE_CHECK(test_impl(
                mat, input, w, h, stride, output2, 32, 32, out_w, out_h, out_w,
                sub_x, sub_y, bd, &conv_params, 0, NULL));

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

void AV2ExtHighbdWarpFilterTest::RunBoundaryCheck(
    ext_highbd_warp_affine_func test_impl, int mode) {
  ExtHighbdWarpTestParam param = GET_PARAM(0);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  RunBoundaryCheckImpl(
      out_w, out_h, bd, GET_PARAM(1), GET_PARAM(2), GET_PARAM(3), GET_PARAM(4),
      mode, &rnd_,
      [](const int32_t *mat, const uint16_t *input, int w, int h, int stride,
         uint16_t *output, int out_w, int out_h, int bd,
         ConvolveParams *conv_params, int16_t, int16_t, int16_t, int16_t) {
        av2_ext_highbd_warp_affine_c(mat, input, w, h, stride, output, 32, 32,
                                     out_w, out_h, out_w, 0, 0, bd, conv_params,
                                     0, NULL);
      },
      [test_impl](const int32_t *mat, const uint16_t *input, int w, int h,
                  int stride, uint16_t *output, int out_w, int out_h, int bd,
                  ConvolveParams *conv_params, int16_t, int16_t, int16_t,
                  int16_t) {
        ASM_REGISTER_STATE_CHECK(test_impl(mat, input, w, h, stride, output, 32,
                                           32, out_w, out_h, out_w, 0, 0, bd,
                                           conv_params, 0, NULL));
      });
}

::testing::internal::ParamGenerator<ExtHighbdWarpScaledTestParams>
BuildScaledParams(ext_highbd_warp_affine_scaled_func filter) {
  const ExtHighbdWarpScaledTestParam params[] = {
    make_tuple(2, 2, 25, 8, filter),    make_tuple(2, 4, 25, 8, filter),
    make_tuple(4, 4, 25, 8, filter),    make_tuple(8, 8, 25, 8, filter),
    make_tuple(64, 64, 10, 8, filter),  make_tuple(4, 16, 25, 8, filter),
    make_tuple(32, 8, 25, 8, filter),   make_tuple(2, 2, 25, 10, filter),
    make_tuple(2, 4, 25, 10, filter),   make_tuple(4, 4, 25, 10, filter),
    make_tuple(8, 8, 25, 10, filter),   make_tuple(64, 64, 10, 10, filter),
    make_tuple(4, 16, 25, 10, filter),  make_tuple(32, 8, 25, 10, filter),
    make_tuple(2, 2, 25, 12, filter),   make_tuple(2, 4, 25, 12, filter),
    make_tuple(4, 4, 25, 12, filter),   make_tuple(8, 8, 25, 12, filter),
    make_tuple(64, 64, 10, 12, filter), make_tuple(4, 16, 25, 12, filter),
    make_tuple(32, 8, 25, 12, filter),
  };
  return ::testing::Combine(::testing::ValuesIn(params),
                            ::testing::Values(0, 1), ::testing::Values(0, 1),
                            ::testing::Values(0, 1), ::testing::Values(0, 1));
}

AV2ExtHighbdWarpFilterScaledTest::~AV2ExtHighbdWarpFilterScaledTest() {}
void AV2ExtHighbdWarpFilterScaledTest::SetUp() {
  rnd_.Reset(ACMRandom::DeterministicSeed());
}

void AV2ExtHighbdWarpFilterScaledTest::TearDown() {
  libavm_test::ClearSystemState();
}

void AV2ExtHighbdWarpFilterScaledTest::RunSpeedTest(
    ext_highbd_warp_affine_scaled_func test_impl) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  ExtHighbdWarpScaledTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  const int mask = (1 << bd) - 1;

  int output_n = ((out_w + 7) & ~7) * out_h;
  uint16_t *input_ = new uint16_t[h * stride];
  uint16_t *input = input_ + border;
  uint16_t *output = new uint16_t[output_n];
  int32_t mat[8];
  int16_t alpha, beta, gamma, delta;
  CONV_BUF_TYPE *dsta = new CONV_BUF_TYPE[output_n];

  generate_warped_model(&rnd_, mat, &alpha, &beta, &gamma, &delta,
                        is_alpha_zero, is_beta_zero, is_gamma_zero,
                        is_delta_zero);
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < border; ++c) {
      input[r * stride - border + c] = input[r * stride];
      input[r * stride + w + c] = input[r * stride + (w - 1)];
    }
  }

  struct scale_factors sf;
  av2_setup_scale_factors_for_frame(&sf, w * 3 / 4, h * 3 / 4, w, h);
  ConvolveParams conv_params =
      get_conv_params_no_round(0, 0, dsta, out_w, 1, bd);

  const int num_loops = 100000000 / (out_w + out_h);
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int i = 0; i < num_loops; ++i)
    av2_ext_highbd_warp_affine_scaled_c(mat, input, w, h, stride, output, 32,
                                        32, out_w, out_h, out_w, 0, 0, bd,
                                        &conv_params, &sf, 0, NULL);
  avm_usec_timer_mark(&timer);
  const int elapsed_c = static_cast<int>(avm_usec_timer_elapsed(&timer));

  avm_usec_timer_start(&timer);
  for (int i = 0; i < num_loops; ++i)
    test_impl(mat, input, w, h, stride, output, 32, 32, out_w, out_h, out_w, 0,
              0, bd, &conv_params, &sf, 0, NULL);
  avm_usec_timer_mark(&timer);
  const int elapsed_simd = static_cast<int>(avm_usec_timer_elapsed(&timer));

  printf("ext highbd warp scaled %3dx%-3d: c %7.2f ns  simd %7.2f ns (%.2fx)\n",
         out_w, out_h, 1000.0 * elapsed_c / num_loops,
         1000.0 * elapsed_simd / num_loops,
         elapsed_simd > 0 ? static_cast<double>(elapsed_c) / elapsed_simd : 0);

  delete[] input_;
  delete[] output;
  delete[] dsta;
}

void AV2ExtHighbdWarpFilterScaledTest::RunCheckOutput(
    ext_highbd_warp_affine_scaled_func test_impl) {
  const int w = 128, h = 128;
  const int border = 16;
  const int stride = w + 2 * border;
  ExtHighbdWarpScaledTestParam param = GET_PARAM(0);
  const int is_alpha_zero = GET_PARAM(1);
  const int is_beta_zero = GET_PARAM(2);
  const int is_gamma_zero = GET_PARAM(3);
  const int is_delta_zero = GET_PARAM(4);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  const int num_iters = ::testing::get<2>(param);
  const int mask = (1 << bd) - 1;

  static const int kScalePairs[][4] = {
    { 96, 96, 128, 128 },   // 3/4 downscaled reference
    { 64, 64, 128, 128 },   // 1/2 downscaled reference
    { 112, 80, 128, 128 },  // anisotropic downscaled reference
    { 128, 128, 64, 64 },   // 2x upscaled reference
  };
  const int num_scales =
      static_cast<int>(sizeof(kScalePairs) / sizeof(kScalePairs[0]));

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
  PadBlock warp_bd_box[MAX_WARP_BD_SIZE * MAX_WARP_BD_SIZE];
  for (int k = 0; k < output_n; ++k) output[k] = output2[k] = rnd_.Rand16();

  for (int i = 0; i < num_iters; ++i) {
    for (int r = 0; r < h; ++r)
      for (int c = 0; c < w; ++c) input[r * stride + c] = rnd_.Rand16() & mask;
    for (int r = 0; r < h; ++r) {
      for (int c = 0; c < border; ++c) {
        input[r * stride - border + c] = input[r * stride];
        input[r * stride + w + c] = input[r * stride + (w - 1)];
      }
    }
    const int use_no_round = rnd_.Rand8() & 1;
    const int use_bd_box = i & 1;
    for (int idx = 0; idx < MAX_WARP_BD_SIZE * MAX_WARP_BD_SIZE; ++idx) {
      if ((i & 3) == 3) {
        warp_bd_box[idx].x0 = 8 + ((idx * 7) & 31);
        warp_bd_box[idx].x1 = w - (8 + ((idx * 11) & 31));
        warp_bd_box[idx].y0 = 8 + ((idx * 5) & 31);
        warp_bd_box[idx].y1 = h - (8 + ((idx * 13) & 31));
      } else {
        warp_bd_box[idx].x0 = (idx * 3) & 7;
        warp_bd_box[idx].x1 = w - ((idx * 5) & 7);
        warp_bd_box[idx].y0 = (idx * 2) & 7;
        warp_bd_box[idx].y1 = h - ((idx * 7) & 7);
      }
    }
    struct scale_factors sf;
    const int *sp = kScalePairs[i % num_scales];
    av2_setup_scale_factors_for_frame(&sf, sp[0], sp[1], sp[2], sp[3]);

    for (int sub_x = 0; sub_x < 2; ++sub_x)
      for (int sub_y = 0; sub_y < 2; ++sub_y) {
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

            av2_ext_highbd_warp_affine_scaled_c(
                mat, input, w, h, stride, output, 32, 32, out_w, out_h, out_w,
                sub_x, sub_y, bd, &conv_params, &sf, use_bd_box,
                use_bd_box ? warp_bd_box : NULL);
            if (use_no_round) {
              conv_params =
                  get_conv_params_no_round(do_average, 0, dstb, out_w, 1, bd);
            }
            conv_params.fwd_offset = quant_dist_lookup_table[ii][0];
            conv_params.bck_offset = quant_dist_lookup_table[ii][1];
            ASM_REGISTER_STATE_CHECK(
                test_impl(mat, input, w, h, stride, output2, 32, 32, out_w,
                          out_h, out_w, sub_x, sub_y, bd, &conv_params, &sf,
                          use_bd_box, use_bd_box ? warp_bd_box : NULL));

            if (use_no_round) {
              for (int j = 0; j < out_w * out_h; ++j)
                ASSERT_EQ(dsta[j], dstb[j])
                    << "Pixel mismatch at index " << j << " = (" << (j % out_w)
                    << ", " << (j / out_w) << ") on iteration " << i;
              for (int j = 0; j < out_w * out_h; ++j)
                ASSERT_EQ(output[j], output2[j])
                    << "Pixel mismatch at index " << j << " = (" << (j % out_w)
                    << ", " << (j / out_w) << ") on iteration " << i;
            } else {
              for (int j = 0; j < out_w * out_h; ++j)
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

void AV2ExtHighbdWarpFilterScaledTest::RunBoundaryCheck(
    ext_highbd_warp_affine_scaled_func test_impl, int mode) {
  ExtHighbdWarpScaledTestParam param = GET_PARAM(0);
  const int out_w = ::testing::get<0>(param), out_h = ::testing::get<1>(param);
  const int bd = ::testing::get<3>(param);
  struct scale_factors sf;
  av2_setup_scale_factors_for_frame(&sf, 96, 96, 128, 128);
  RunBoundaryCheckImpl(
      out_w, out_h, bd, GET_PARAM(1), GET_PARAM(2), GET_PARAM(3), GET_PARAM(4),
      mode, &rnd_,
      [&sf](const int32_t *mat, const uint16_t *input, int w, int h, int stride,
            uint16_t *output, int out_w, int out_h, int bd,
            ConvolveParams *conv_params, int16_t, int16_t, int16_t, int16_t) {
        av2_ext_highbd_warp_affine_scaled_c(mat, input, w, h, stride, output,
                                            32, 32, out_w, out_h, out_w, 0, 0,
                                            bd, conv_params, &sf, 0, NULL);
      },
      [test_impl, &sf](const int32_t *mat, const uint16_t *input, int w, int h,
                       int stride, uint16_t *output, int out_w, int out_h,
                       int bd, ConvolveParams *conv_params, int16_t, int16_t,
                       int16_t, int16_t) {
        ASM_REGISTER_STATE_CHECK(test_impl(mat, input, w, h, stride, output, 32,
                                           32, out_w, out_h, out_w, 0, 0, bd,
                                           conv_params, &sf, 0, NULL));
      });
}
}  // namespace AV2ExtHighbdWarpFilter
}  // namespace libavm_test
