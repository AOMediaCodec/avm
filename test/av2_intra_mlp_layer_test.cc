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

#include <cmath>
#include <tuple>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm/avm_integer.h"
#include "config/avm_config.h"
#include "config/av2_rtcd.h"
#include "av2/encoder/intra_mode_mlp_weights.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/util.h"

namespace {
typedef void (*IntraMlpLayer_Func)(const float *input, int in_dim,
                                   const float *weights, const float *bias,
                                   float *output, int out_dim, int apply_relu);

typedef std::tuple<const IntraMlpLayer_Func> IntraMlpLayerTestParam;

const float epsilon = 1e-3f;  // Error threshold for functional equivalence

class IntraMlpLayerTest
    : public ::testing::TestWithParam<IntraMlpLayerTestParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

 protected:
  void RunLayerTest(int in_dim, int out_dim, int apply_relu);

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

    av2_intra_mlp_layer_c(input.data(), in_dim, weights.data(), bias.data(),
                          output_ref.data(), out_dim, apply_relu);
    target_func_(input.data(), in_dim, weights.data(), bias.data(),
                 output_test.data(), out_dim, apply_relu);
    libavm_test::ClearSystemState();

    for (int i = 0; i < out_dim; i++) {
      if (fabsf(output_ref[i]) < epsilon) {
        ASSERT_LE(fabsf(output_test[i]), epsilon)
            << "Reference output was near-zero, test output was not "
            << "(in_dim=" << in_dim << ", out_dim=" << out_dim << ")";
      } else {
        const float relative_error =
            fabsf((output_ref[i] - output_test[i]) / output_ref[i]);
        ASSERT_LE(relative_error, epsilon)
            << "Excessive relative error between reference and test "
            << "(in_dim=" << in_dim << ", out_dim=" << out_dim << ")";
      }
    }
  }
}

// Covers the layer shapes actually used by intra_mode_mlp_predict()
// (MLP_INPUT_DIM->MLP_H1_DIM->MLP_H2_DIM->MLP_H3_DIM), all with ReLU applied.
TEST_P(IntraMlpLayerTest, RandomValues) {
  RunLayerTest(MLP_INPUT_DIM, MLP_H1_DIM, 1);
  RunLayerTest(MLP_H1_DIM, MLP_H2_DIM, 1);
  RunLayerTest(MLP_H2_DIM, MLP_H3_DIM, 1);
}

#if HAVE_SSE2 && !CONFIG_EXCLUDE_SIMD_MISMATCH
INSTANTIATE_TEST_SUITE_P(SSE2, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_sse2));
#endif

#if HAVE_AVX2 && !CONFIG_EXCLUDE_SIMD_MISMATCH
INSTANTIATE_TEST_SUITE_P(AVX2, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_avx2));
#endif

#if HAVE_NEON && !CONFIG_EXCLUDE_SIMD_MISMATCH
INSTANTIATE_TEST_SUITE_P(NEON, IntraMlpLayerTest,
                         ::testing::Values(av2_intra_mlp_layer_neon));
#endif

}  // namespace
