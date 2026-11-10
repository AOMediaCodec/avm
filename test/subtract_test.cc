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

#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "av2/common/blockd.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

namespace {

using libavm_test::ACMRandom;

typedef void (*HBDSubtractFunc)(int rows, int cols, int16_t *diff_ptr,
                                ptrdiff_t diff_stride, const uint16_t *src_ptr,
                                ptrdiff_t src_stride, const uint16_t *pred_ptr,
                                ptrdiff_t pred_stride, int bd);

using std::get;
using std::make_tuple;
using std::tuple;

// <<width, height>, bit_depth, subtract>
typedef tuple<int, int> BlockSize;
typedef tuple<BlockSize, int, HBDSubtractFunc> Params;

// Stride of src; it differs from the width of most blocks.
const int kMaxWidth = 256;
// Extra stride of pred, so that pred_stride differs from diff_stride.
const int kPredPad = 16;

class AV2HBDSubtractBlockTest : public ::testing::TestWithParam<Params> {
 public:
  virtual void SetUp() {
    block_width_ = get<0>(GET_PARAM(0));
    block_height_ = get<1>(GET_PARAM(0));
    bit_depth_ = static_cast<avm_bit_depth_t>(GET_PARAM(1));
    func_ = GET_PARAM(2);

    rnd_.Reset(ACMRandom::DeterministicSeed());

    const size_t max_block_size = kMaxWidth * kMaxWidth;
    src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, max_block_size * sizeof(uint16_t)));
    pred_ = reinterpret_cast<uint16_t *>(avm_memalign(
        16, (kMaxWidth + kPredPad) * kMaxWidth * sizeof(uint16_t)));
    diff_ = reinterpret_cast<int16_t *>(
        avm_memalign(16, max_block_size * sizeof(int16_t)));
  }

  virtual void TearDown() {
    avm_free(src_);
    avm_free(pred_);
    avm_free(diff_);
  }

 protected:
  void CheckResult();
  void RunForSpeed();

 private:
  ACMRandom rnd_;
  int block_height_;
  int block_width_;
  avm_bit_depth_t bit_depth_;
  HBDSubtractFunc func_;
  uint16_t *src_;
  uint16_t *pred_;
  int16_t *diff_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2HBDSubtractBlockTest);

void AV2HBDSubtractBlockTest::CheckResult() {
  const int test_num = 100;
  const int mask = (1 << bit_depth_) - 1;
  const int pred_stride = block_width_ + kPredPad;

  for (int i = 0; i < test_num; ++i) {
    for (int j = 0; j < block_height_ * kMaxWidth; ++j) {
      src_[j] = rnd_.Rand16() & mask;
    }
    for (int j = 0; j < block_height_ * pred_stride; ++j) {
      pred_[j] = rnd_.Rand16() & mask;
    }

    ASM_REGISTER_STATE_CHECK(func_(block_height_, block_width_, diff_,
                                   block_width_, src_, kMaxWidth, pred_,
                                   pred_stride, bit_depth_));

    for (int r = 0; r < block_height_; ++r) {
      for (int c = 0; c < block_width_; ++c) {
        EXPECT_EQ(diff_[r * block_width_ + c],
                  (src_[r * kMaxWidth + c] - pred_[r * pred_stride + c]))
            << "r = " << r << ", c = " << c << ", test: " << i;
      }
    }
  }
}

TEST_P(AV2HBDSubtractBlockTest, CheckResult) { CheckResult(); }

void AV2HBDSubtractBlockTest::RunForSpeed() {
  const int test_num = 200000;
  const int max_block_size = kMaxWidth * kMaxWidth;
  const int mask = (1 << bit_depth_) - 1;
  int i, j;

  for (j = 0; j < max_block_size; ++j) {
    src_[j] = rnd_.Rand16() & mask;
    pred_[j] = rnd_.Rand16() & mask;
  }

  for (i = 0; i < test_num; ++i) {
    func_(block_height_, block_width_, diff_, block_width_, src_, block_width_,
          pred_, block_width_, bit_depth_);
  }
}

TEST_P(AV2HBDSubtractBlockTest, DISABLED_Speed) { RunForSpeed(); }

// Block sizes run for every kernel.
const BlockSize kBlockSizes[] = {
  make_tuple(4, 4),     make_tuple(4, 8),     make_tuple(8, 4),
  make_tuple(8, 8),     make_tuple(8, 16),    make_tuple(16, 8),
  make_tuple(16, 16),   make_tuple(16, 32),   make_tuple(32, 16),
  make_tuple(32, 32),   make_tuple(32, 64),   make_tuple(64, 32),
  make_tuple(64, 64),   make_tuple(64, 128),  make_tuple(128, 64),
  make_tuple(128, 128), make_tuple(128, 256), make_tuple(256, 128),
  make_tuple(256, 256), make_tuple(4, 16),    make_tuple(16, 4),
  make_tuple(8, 32),    make_tuple(32, 8),    make_tuple(16, 64),
  make_tuple(64, 16),   make_tuple(4, 32),    make_tuple(32, 4),
  make_tuple(8, 64),    make_tuple(64, 8),    make_tuple(4, 64),
  make_tuple(64, 4),
};

INSTANTIATE_TEST_SUITE_P(
    C, AV2HBDSubtractBlockTest,
    ::testing::Combine(::testing::ValuesIn(kBlockSizes), ::testing::Values(12),
                       ::testing::Values(&avm_highbd_subtract_block_c)));

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, AV2HBDSubtractBlockTest,
    ::testing::Combine(::testing::ValuesIn(kBlockSizes), ::testing::Values(12),
                       ::testing::Values(&avm_highbd_subtract_block_sse2)));
#endif  // HAVE_SSE2

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2HBDSubtractBlockTest,
    ::testing::Combine(::testing::ValuesIn(kBlockSizes), ::testing::Values(12),
                       ::testing::Values(&avm_highbd_subtract_block_avx2)));
#endif  // HAVE_AVX2
}  // namespace
