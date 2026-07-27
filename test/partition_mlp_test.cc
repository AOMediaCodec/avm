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

#include "av2/encoder/partition_mlp.h"
#include "av2/common/enums.h"

namespace {

class PartitionMlpPredictTest : public ::testing::Test {
 protected:
  static constexpr int kBw = 32;
  static constexpr int kBh = 32;
  static constexpr int kStride = kBw;

  std::vector<uint16_t> src_buf_ = std::vector<uint16_t>(kStride * kBh);
};

TEST_F(PartitionMlpPredictTest, ReturnsValidClassIndexForFlatBlock) {
  for (int i = 0; i < kStride * kBh; i++) src_buf_[i] = 512;
  float logits[4];
  const int pred = av2_partition_mlp_predict(
      src_buf_.data(), kStride, kBw, kBh, (int)BLOCK_32X32, 128, /*var=*/0,
      /*none_rd=*/-1, /*above_part=*/-1, /*left_part=*/-1, /*is_intra=*/1,
      logits);
  EXPECT_GE(pred, PART_MLP_NONE);
  EXPECT_LE(pred, PART_MLP_SPLIT);
  for (int i = 0; i < 4; i++) EXPECT_TRUE(std::isfinite(logits[i])) << i;
}

TEST_F(PartitionMlpPredictTest, PredictedClassIsArgmaxOfLogits) {
  for (int r = 0; r < kBh; r++)
    for (int c = 0; c < kBw; c++)
      src_buf_[r * kStride + c] = (uint16_t)(r * 4 + c);
  float logits[4];
  const int pred = av2_partition_mlp_predict(
      src_buf_.data(), kStride, kBw, kBh, (int)BLOCK_32X32, 180, 900, 12345,
      PART_MLP_HORZ, PART_MLP_VERT, /*is_intra=*/0, logits);
  int expected_argmax = 0;
  for (int i = 1; i < 4; i++)
    if (logits[i] > logits[expected_argmax]) expected_argmax = i;
  EXPECT_EQ(pred, expected_argmax);
}

TEST_F(PartitionMlpPredictTest, IsDeterministic) {
  for (int r = 0; r < kBh; r++)
    for (int c = 0; c < kBw; c++)
      src_buf_[r * kStride + c] = (uint16_t)((r * 37 + c * 91) % 1024);
  float logits_a[4];
  float logits_b[4];
  const int pred_a = av2_partition_mlp_predict(src_buf_.data(), kStride, kBw,
                                               kBh, (int)BLOCK_32X32, 210, 500,
                                               -1, -1, -1, 1, logits_a);
  const int pred_b = av2_partition_mlp_predict(src_buf_.data(), kStride, kBw,
                                               kBh, (int)BLOCK_32X32, 210, 500,
                                               -1, -1, -1, 1, logits_b);
  EXPECT_EQ(pred_a, pred_b);
  for (int i = 0; i < 4; i++) EXPECT_FLOAT_EQ(logits_a[i], logits_b[i]) << i;
}

TEST_F(PartitionMlpPredictTest, IntraAndInterUseDifferentModels) {
  for (int r = 0; r < kBh; r++)
    for (int c = 0; c < kBw; c++)
      src_buf_[r * kStride + c] = (uint16_t)((r * 53 + c * 17) % 1024);
  float logits_kf[4];
  float logits_inter[4];
  av2_partition_mlp_predict(src_buf_.data(), kStride, kBw, kBh,
                            (int)BLOCK_32X32, 210, 500, -1, -1, -1,
                            /*is_intra=*/1, logits_kf);
  av2_partition_mlp_predict(src_buf_.data(), kStride, kBw, kBh,
                            (int)BLOCK_32X32, 210, 500, -1, -1, -1,
                            /*is_intra=*/0, logits_inter);
  // KF and inter models have independently trained weights; expect at least
  // one logit to differ (a spurious exact match across all 4 would indicate
  // the is_intra branch isn't actually selecting different weight tables).
  bool any_diff = false;
  for (int i = 0; i < 4; i++)
    if (logits_kf[i] != logits_inter[i]) any_diff = true;
  EXPECT_TRUE(any_diff);
}

TEST_F(PartitionMlpPredictTest, HandlesNonSquareBlocks) {
  const int bw = 64, bh = 32;
  std::vector<uint16_t> src(bw * bh, 300);
  float logits[4];
  const int pred =
      av2_partition_mlp_predict(src.data(), bw, bw, bh, (int)BLOCK_64X32, 150,
                                100, -1, -1, -1, 1, logits);
  EXPECT_GE(pred, PART_MLP_NONE);
  EXPECT_LE(pred, PART_MLP_SPLIT);
  for (int i = 0; i < 4; i++) EXPECT_TRUE(std::isfinite(logits[i])) << i;
}

}  // namespace
