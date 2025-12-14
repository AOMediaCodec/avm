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

// <width, height, bit_dpeth, subtract>
typedef tuple<int, int, int, HBDSubtractFunc> Params;

class AV2HBDSubtractBlockTest : public ::testing::TestWithParam<Params> {
 public:
  virtual void SetUp() {
    block_width_ = GET_PARAM(0);
    block_height_ = GET_PARAM(1);
    bit_depth_ = static_cast<avm_bit_depth_t>(GET_PARAM(2));
    func_ = GET_PARAM(3);

    rnd_.Reset(ACMRandom::DeterministicSeed());

    const size_t max_width = 128;
    const size_t max_block_size = max_width * max_width;
    src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, max_block_size * sizeof(uint16_t)));
    pred_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, max_block_size * sizeof(uint16_t)));
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
  const size_t max_width = 128;
  const int max_block_size = max_width * max_width;
  const int mask = (1 << bit_depth_) - 1;
  int i, j;

  for (i = 0; i < test_num; ++i) {
    for (j = 0; j < max_block_size; ++j) {
      src_[j] = rnd_.Rand16() & mask;
      pred_[j] = rnd_.Rand16() & mask;
    }

    func_(block_height_, block_width_, diff_, block_width_, src_, block_width_,
          pred_, block_width_, bit_depth_);

    for (int r = 0; r < block_height_; ++r) {
      for (int c = 0; c < block_width_; ++c) {
        EXPECT_EQ(diff_[r * block_width_ + c],
                  (src_[r * block_width_ + c] - pred_[r * block_width_ + c]))
            << "r = " << r << ", c = " << c << ", test: " << i;
      }
    }
  }
}

TEST_P(AV2HBDSubtractBlockTest, CheckResult) { CheckResult(); }

void AV2HBDSubtractBlockTest::RunForSpeed() {
  const int test_num = 200000;
  const size_t max_width = 128;
  const int max_block_size = max_width * max_width;
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

#if HAVE_SSE2
const Params kAV2HBDSubtractBlock_sse2[] = {
  make_tuple(4, 4, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(4, 4, 12, &avm_highbd_subtract_block_c),
  make_tuple(4, 8, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(4, 8, 12, &avm_highbd_subtract_block_c),
  make_tuple(8, 4, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(8, 4, 12, &avm_highbd_subtract_block_c),
  make_tuple(8, 8, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(8, 8, 12, &avm_highbd_subtract_block_c),
  make_tuple(8, 16, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(8, 16, 12, &avm_highbd_subtract_block_c),
  make_tuple(16, 8, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(16, 8, 12, &avm_highbd_subtract_block_c),
  make_tuple(16, 16, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(16, 16, 12, &avm_highbd_subtract_block_c),
  make_tuple(16, 32, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(16, 32, 12, &avm_highbd_subtract_block_c),
  make_tuple(32, 16, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(32, 16, 12, &avm_highbd_subtract_block_c),
  make_tuple(32, 32, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(32, 32, 12, &avm_highbd_subtract_block_c),
  make_tuple(32, 64, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(32, 64, 12, &avm_highbd_subtract_block_c),
  make_tuple(64, 32, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(64, 32, 12, &avm_highbd_subtract_block_c),
  make_tuple(64, 64, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(64, 64, 12, &avm_highbd_subtract_block_c),
  make_tuple(64, 128, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(64, 128, 12, &avm_highbd_subtract_block_c),
  make_tuple(128, 64, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(128, 64, 12, &avm_highbd_subtract_block_c),
  make_tuple(128, 128, 12, &avm_highbd_subtract_block_sse2),
  make_tuple(128, 128, 12, &avm_highbd_subtract_block_c)
};

INSTANTIATE_TEST_SUITE_P(SSE2, AV2HBDSubtractBlockTest,
                         ::testing::ValuesIn(kAV2HBDSubtractBlock_sse2));
#endif  // HAVE_SSE2
}  // namespace
