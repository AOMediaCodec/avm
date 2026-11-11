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

#include <cstdio>
#include <tuple>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm/avm_integer.h"
#include "config/avm_config.h"
#include "config/av2_rtcd.h"
#include "av2/encoder/intra_mode_mlp_weights.h"
#include "av2/encoder/partition_sms_weights.h"
#include "avm_ports/avm_timer.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/util.h"

#if ARCH_X86
// On 32-bit x86, the C reference and the scalar tails of the SIMD functions run
// on the x87 unit, which rounds to 64 bits by default. Round to 24 bits like
// the SSE float operations so the exact comparison is valid. x86-64 already
// does scalar float math in SSE.
#include "avm_ports/x86.h"
#define FLOATING_POINT_SET_SINGLE_PRECISION \
  unsigned short x87_orig_mode = x87_set_single_precision();
#define FLOATING_POINT_RESTORE_PRECISION x87_set_control_word(x87_orig_mode);
#else
#define FLOATING_POINT_SET_SINGLE_PRECISION
#define FLOATING_POINT_RESTORE_PRECISION
#endif  // ARCH_X86

// alias of definition in partition_sms.h
#define SMS_UNIFIED_N_CLASSES 10

namespace {
typedef void (*IntraMlpLayer_Func)(const float *input, int in_dim,
                                   const float *weights, const float *bias,
                                   float *output, int out_dim, int apply_relu);

typedef std::tuple<const IntraMlpLayer_Func> IntraMlpLayerTestParam;

class IntraMlpLayerTest
    : public ::testing::TestWithParam<IntraMlpLayerTestParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

 protected:
  void RunLayerTest(int in_dim, int out_dim, int apply_relu);
  void RunSpeedTest(int in_dim, int out_dim, int apply_relu);

  IntraMlpLayer_Func target_func_;
  libavm_test::ACMRandom rng_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(IntraMlpLayerTest);

void IntraMlpLayerTest::RunLayerTest(int in_dim, int out_dim, int apply_relu) {
  libavm_test::ClearSystemState();
  std::vector<float> input(in_dim);
  std::vector<float> weights(in_dim * out_dim);
  std::vector<float> bias(out_dim);
  std::vector<float> output_ref(out_dim);
  std::vector<float> output_test(out_dim);

  for (int iter = 0; iter < 1000 && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < in_dim; i++)
      input[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);
    for (int i = 0; i < in_dim * out_dim; i++)
      weights[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);
    for (int i = 0; i < out_dim; i++)
      bias[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);

    FLOATING_POINT_SET_SINGLE_PRECISION
    av2_intra_mlp_layer_c(input.data(), in_dim, weights.data(), bias.data(),
                          output_ref.data(), out_dim, apply_relu);
    target_func_(input.data(), in_dim, weights.data(), bias.data(),
                 output_test.data(), out_dim, apply_relu);
    FLOATING_POINT_RESTORE_PRECISION
    libavm_test::ClearSystemState();

    for (int i = 0; i < out_dim; i++) {
      ASSERT_EQ(output_ref[i], output_test[i])
          << "Output not bit-exact with C reference at index " << i
          << " (in_dim=" << in_dim << ", out_dim=" << out_dim << ")";
    }
  }
}

void IntraMlpLayerTest::RunSpeedTest(int in_dim, int out_dim, int apply_relu) {
  libavm_test::ClearSystemState();
  std::vector<float> input(in_dim);
  std::vector<float> weights(in_dim * out_dim);
  std::vector<float> bias(out_dim);
  std::vector<float> output_ref(out_dim);
  std::vector<float> output_test(out_dim);

  for (int i = 0; i < in_dim; i++)
    input[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);
  for (int i = 0; i < in_dim * out_dim; i++)
    weights[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);
  for (int i = 0; i < out_dim; i++)
    bias[i] = ((float)rng_.Rand31() - (1 << 30)) / (1u << 31);

  const int num_iter = 200000;
  avm_usec_timer timer_ref;
  avm_usec_timer timer_test;

  avm_usec_timer_start(&timer_ref);
  for (int count = 0; count < num_iter; count++)
    av2_intra_mlp_layer_c(input.data(), in_dim, weights.data(), bias.data(),
                          output_ref.data(), out_dim, apply_relu);
  avm_usec_timer_mark(&timer_ref);

  avm_usec_timer_start(&timer_test);
  for (int count = 0; count < num_iter; count++)
    target_func_(input.data(), in_dim, weights.data(), bias.data(),
                 output_test.data(), out_dim, apply_relu);
  avm_usec_timer_mark(&timer_test);
  libavm_test::ClearSystemState();

  const int total_time_ref =
      static_cast<int>(avm_usec_timer_elapsed(&timer_ref));
  const int total_time_test =
      static_cast<int>(avm_usec_timer_elapsed(&timer_test));

  printf(
      "in_dim = %3d out_dim = %3d \t ref_time = %d \t simd_time = %d \t "
      "Gain = %4.2f\n",
      in_dim, out_dim, total_time_ref, total_time_test,
      static_cast<float>(total_time_ref) /
          static_cast<float>(total_time_test > 0 ? total_time_test : 1));
}

// Covers the layer shapes actually used by intra_mode_mlp_predict()
// (MLP_INPUT_DIM->MLP_H1_DIM->MLP_H2_DIM->MLP_H3_DIM), with ReLU applied.
// (MLP_H3_DIM->MLP_OUTPUT_DIM), without ReLU applied.
// (SMS_UNIFIED_IN_DIM->SMS_UNIFIED_H1_DIM->SMS_UNIFIED_H2_DIM), with ReLU
// applied. (SMS_UNIFIED_H2_DIM->SMS_UNIFIED_N_CLASSES), without ReLU applied.
TEST_P(IntraMlpLayerTest, RandomValues) {
  RunLayerTest(MLP_INPUT_DIM, MLP_H1_DIM, 1);
  RunLayerTest(MLP_H1_DIM, MLP_H2_DIM, 1);
  RunLayerTest(MLP_H2_DIM, MLP_H3_DIM, 1);
  RunLayerTest(MLP_H3_DIM, MLP_OUTPUT_DIM, 0);

  RunLayerTest(SMS_UNIFIED_IN_DIM, SMS_UNIFIED_H1_DIM, 1);
  RunLayerTest(SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM, 1);
  RunLayerTest(SMS_UNIFIED_H2_DIM, SMS_UNIFIED_N_CLASSES, 0);
}

TEST_P(IntraMlpLayerTest, DISABLED_Speed) {
  RunSpeedTest(MLP_INPUT_DIM, MLP_H1_DIM, 1);
  RunSpeedTest(MLP_H1_DIM, MLP_H2_DIM, 1);
  RunSpeedTest(MLP_H2_DIM, MLP_H3_DIM, 1);
  RunSpeedTest(MLP_H3_DIM, MLP_OUTPUT_DIM, 0);

  RunSpeedTest(SMS_UNIFIED_IN_DIM, SMS_UNIFIED_H1_DIM, 1);
  RunSpeedTest(SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM, 1);
  RunSpeedTest(SMS_UNIFIED_H2_DIM, SMS_UNIFIED_N_CLASSES, 0);
}

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_sse2));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_neon));
#endif

}  // namespace
