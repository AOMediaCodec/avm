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
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/encoder/intra_mode_mlp.h"
#include "av2/encoder/intra_mode_mlp_weights.h"
#include "test/acm_random.h"

namespace {

constexpr float kEps = 1e-4f;

// ---------------------------------------------------------------------------
// intra_mode_mlp_get_topk
// ---------------------------------------------------------------------------

TEST(IntraModeMlpGetTopk, ReturnsDescendingOrderIndices) {
  // Distinct values, so the top-k order is unambiguous.
  float logits[MLP_OUTPUT_DIM] = { 0.1f, 0.9f, 0.3f,  -0.2f, 0.05f, 0.7f, 0.15f,
                                   0.0f, 0.4f, -1.0f, 0.6f,  0.25f, 0.55f };
  int modes[MLP_OUTPUT_DIM];
  intra_mode_mlp_get_topk(logits, 5, modes);
  // Sorted descending: 0.9(1), 0.7(5), 0.6(10), 0.55(12), 0.4(8)
  const int expected[5] = { 1, 5, 10, 12, 8 };
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(modes[i], expected[i]) << "mismatch at top-" << i;
  }
}

TEST(IntraModeMlpGetTopk, NoDuplicatesEvenWithTies) {
  float logits[MLP_OUTPUT_DIM];
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) logits[i] = 0.5f;  // all tied
  int modes[MLP_OUTPUT_DIM];
  intra_mode_mlp_get_topk(logits, MLP_OUTPUT_DIM, modes);
  bool seen[MLP_OUTPUT_DIM] = { false };
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
    ASSERT_GE(modes[i], 0);
    ASSERT_LT(modes[i], MLP_OUTPUT_DIM);
    ASSERT_FALSE(seen[modes[i]]) << "duplicate mode " << modes[i];
    seen[modes[i]] = true;
  }
}

// Regression test: a NaN logit must not corrupt the fallback and produce a
// duplicate index (see intra_mode_mlp_get_topk's NaN-safe best_idx fallback).
TEST(IntraModeMlpGetTopk, NanLogitsDoNotProduceDuplicates) {
  float logits[MLP_OUTPUT_DIM] = { 0.9f, 0.1f, 0.2f, 0.3f,  0.4f,  0.5f, 0.6f,
                                   0.7f, 0.8f, 0.0f, -0.1f, -0.2f, -0.3f };
  const float nan_val = std::nanf("");
  for (int i = 5; i < MLP_OUTPUT_DIM; i++) logits[i] = nan_val;

  int modes[MLP_OUTPUT_DIM];
  intra_mode_mlp_get_topk(logits, MLP_OUTPUT_DIM, modes);
  bool seen[MLP_OUTPUT_DIM] = { false };
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
    ASSERT_GE(modes[i], 0);
    ASSERT_LT(modes[i], MLP_OUTPUT_DIM);
    ASSERT_FALSE(seen[modes[i]])
        << "duplicate mode " << modes[i] << " with NaN logits present";
    seen[modes[i]] = true;
  }
}

TEST(IntraModeMlpGetTopk, KEqualsOneReturnsMax) {
  float logits[MLP_OUTPUT_DIM] = { 0.1f, 0.2f, 0.9f, 0.3f,  0.4f,  0.5f, 0.6f,
                                   0.7f, 0.8f, 0.0f, -0.1f, -0.2f, -0.3f };
  int modes[1];
  intra_mode_mlp_get_topk(logits, 1, modes);
  EXPECT_EQ(modes[0], 2);
}

// ---------------------------------------------------------------------------
// intra_mode_mlp_prepare_features
// ---------------------------------------------------------------------------

class IntraModeMlpPrepareFeaturesTest : public ::testing::Test {
 protected:
  static constexpr int kBw = 16;
  static constexpr int kBh = 16;
  static constexpr int kStride = kBw;
  static constexpr int kBd = 10;

  std::vector<uint16_t> src_buf_ = std::vector<uint16_t>(kStride * kBh);
};

TEST_F(IntraModeMlpPrepareFeaturesTest, ConstantBlockDownsamplesToConstant) {
  const uint16_t kValue = 100;
  for (int i = 0; i < kStride * kBh; i++) src_buf_[i] = kValue;

  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  -1, 0, -1, 0, 0, features);

  const float pixel_norm = (float)((1 << kBd) - 1);
  for (int i = 0; i < 64; i++) {
    EXPECT_NEAR(features[i], kValue / pixel_norm, kEps)
        << "pixel feature " << i;
  }
}

TEST_F(IntraModeMlpPrepareFeaturesTest, AbsentNeighborUsesSentinel) {
  for (int i = 0; i < kStride * kBh; i++) src_buf_[i] = 0;
  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  /*neighbor_above_mode=*/-1, /*delta=*/5,
                                  /*neighbor_left_mode=*/-1, /*delta=*/-5, 0,
                                  features);
  // Absent neighbor -> sentinel mode 13, delta forced to 0 regardless of the
  // (don't-care) delta argument passed in.
  EXPECT_NEAR(features[64], 13.0f / MLP_NORM_NEIGHBOR_MODE, kEps);
  EXPECT_NEAR(features[65],
              (0.0f + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE, kEps);
  EXPECT_NEAR(features[66], 13.0f / MLP_NORM_NEIGHBOR_MODE, kEps);
  EXPECT_NEAR(features[67],
              (0.0f + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE, kEps);
}

TEST_F(IntraModeMlpPrepareFeaturesTest, PresentNeighborUsesActualValues) {
  for (int i = 0; i < kStride * kBh; i++) src_buf_[i] = 0;
  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  /*neighbor_above_mode=*/5, /*delta=*/2,
                                  /*neighbor_left_mode=*/1, /*delta=*/-1, 0,
                                  features);
  EXPECT_NEAR(features[64], 5.0f / MLP_NORM_NEIGHBOR_MODE, kEps);
  EXPECT_NEAR(features[65],
              (2.0f + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE, kEps);
  EXPECT_NEAR(features[66], 1.0f / MLP_NORM_NEIGHBOR_MODE, kEps);
  EXPECT_NEAR(features[67],
              (-1.0f + MLP_NORM_DELTA_OFFSET) / MLP_NORM_DELTA_RANGE, kEps);
}

TEST_F(IntraModeMlpPrepareFeaturesTest,
       SizeQpAndInterFlagAreNormalizedCorrectly) {
  for (int i = 0; i < kStride * kBh; i++) src_buf_[i] = 0;
  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  -1, 0, -1, 0, /*is_inter_frame=*/1, features);
  EXPECT_NEAR(features[68], (float)kBw / MLP_NORM_BW, kEps);
  EXPECT_NEAR(features[69], (float)kBh / MLP_NORM_BH, kEps);
  EXPECT_NEAR(features[70], 128.0f / MLP_NORM_QP, kEps);
  EXPECT_FLOAT_EQ(features[71], 1.0f);

  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  -1, 0, -1, 0, /*is_inter_frame=*/0, features);
  EXPECT_FLOAT_EQ(features[71], 0.0f);
}

// Block where pixel value depends only on column (dx-dominant gradient)
// produces a Sobel dx of exactly 8 and dy of exactly 0 at every interior
// pixel (verified by hand from the Sobel kernel weights), landing all HOG
// energy in bin 0 with no truncation ambiguity (mag=8 is even).
TEST_F(IntraModeMlpPrepareFeaturesTest, DxOnlyGradientConcentratesInBinZero) {
  for (int r = 0; r < kBh; r++)
    for (int c = 0; c < kBw; c++) src_buf_[r * kStride + c] = (uint16_t)c;

  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  -1, 0, -1, 0, 0, features);
  const float *hog = &features[72];
  EXPECT_NEAR(hog[0], 1.0f, 0.01f);
  for (int i = 1; i < 32; i++) EXPECT_NEAR(hog[i], 0.0f, 0.01f) << "bin " << i;
}

// Block where pixel value depends only on row (dy-dominant gradient, dx==0)
// hits the dx==0 special case, which splits energy evenly between bin 0 and
// bin 31.
TEST_F(IntraModeMlpPrepareFeaturesTest, DyOnlyGradientSplitsBinZeroAndBin31) {
  for (int r = 0; r < kBh; r++)
    for (int c = 0; c < kBw; c++) src_buf_[r * kStride + c] = (uint16_t)r;

  float features[MLP_INPUT_DIM];
  intra_mode_mlp_prepare_features(src_buf_.data(), kStride, kBd, kBw, kBh, 128,
                                  -1, 0, -1, 0, 0, features);
  const float *hog = &features[72];
  EXPECT_NEAR(hog[0], 0.5f, 0.01f);
  EXPECT_NEAR(hog[31], 0.5f, 0.01f);
  for (int i = 1; i < 31; i++) EXPECT_NEAR(hog[i], 0.0f, 0.01f) << "bin " << i;
}

// ---------------------------------------------------------------------------
// intra_mode_mlp_predict
// ---------------------------------------------------------------------------

TEST(IntraModeMlpPredict, ProducesFiniteOutputForZeroInput) {
  float features[MLP_INPUT_DIM] = { 0.0f };
  float logits[MLP_OUTPUT_DIM];
  intra_mode_mlp_predict(features, logits);
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
    EXPECT_TRUE(std::isfinite(logits[i])) << "logit " << i << " not finite";
  }
}

TEST(IntraModeMlpPredict, IsDeterministic) {
  libavm_test::ACMRandom rng;
  float features[MLP_INPUT_DIM];
  for (int i = 0; i < MLP_INPUT_DIM; i++)
    features[i] = ((float)rng.Rand31() - (1 << 30)) / (1u << 31);

  float logits_a[MLP_OUTPUT_DIM];
  float logits_b[MLP_OUTPUT_DIM];
  intra_mode_mlp_predict(features, logits_a);
  intra_mode_mlp_predict(features, logits_b);
  for (int i = 0; i < MLP_OUTPUT_DIM; i++) {
    EXPECT_TRUE(std::isfinite(logits_a[i]));
    EXPECT_FLOAT_EQ(logits_a[i], logits_b[i]) << "logit " << i;
  }
}

TEST(IntraModeMlpPredict, TopkOnPredictOutputIsWellFormed) {
  float features[MLP_INPUT_DIM] = { 0.0f };
  float logits[MLP_OUTPUT_DIM];
  intra_mode_mlp_predict(features, logits);

  int modes[MLP_TOP_K];
  intra_mode_mlp_get_topk(logits, MLP_TOP_K, modes);
  bool seen[MLP_OUTPUT_DIM] = { false };
  for (int i = 0; i < MLP_TOP_K; i++) {
    ASSERT_GE(modes[i], 0);
    ASSERT_LT(modes[i], MLP_OUTPUT_DIM);
    ASSERT_FALSE(seen[modes[i]]);
    seen[modes[i]] = true;
  }
}

}  // namespace
