/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "config/av2_rtcd.h"

#include "test/acm_random.h"
#include "test/util.h"

namespace {
typedef void (*make_bawp_func)(uint16_t *dst, int dst_stride, int16_t alpha,
                               int32_t beta, int shift, int bw, int bh, int bd);
#if HAVE_AVX2
const BLOCK_SIZE kValidBlockSize[] = {
  BLOCK_4X4,     BLOCK_4X8,     BLOCK_8X4,     BLOCK_8X8,     BLOCK_8X16,
  BLOCK_16X8,    BLOCK_16X16,   BLOCK_16X32,   BLOCK_32X16,   BLOCK_32X32,
  BLOCK_32X64,   BLOCK_64X32,   BLOCK_64X64,   BLOCK_64X128,  BLOCK_128X64,
  BLOCK_128X128, BLOCK_128X256, BLOCK_256X128, BLOCK_256X256, BLOCK_4X16,
  BLOCK_16X4,    BLOCK_8X32,    BLOCK_32X8,    BLOCK_16X64,   BLOCK_64X16,
  BLOCK_4X32,    BLOCK_32X4,    BLOCK_8X64,    BLOCK_64X8,    BLOCK_4X64,
  BLOCK_64X4,
};
#endif  // HAVE_AVX2

typedef std::tuple<make_bawp_func, BLOCK_SIZE> BAWPParam;

class BAWPTest : public ::testing::TestWithParam<BAWPParam> {
 public:
  ~BAWPTest();
  void SetUp();

  void TearDown();

 protected:
  void RunCheckOutput(make_bawp_func test_impl, BLOCK_SIZE bsize);
  void RunSpeedTest(make_bawp_func test_impl, BLOCK_SIZE bsize);
  bool CheckResult(int width, int height) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int idx = y * width + x;
        if (pred1_[idx] != pred2_[idx]) {
          printf("%dx%d mismatch @%d(%d,%d) ", width, height, idx, x, y);
          printf("%d != %d ", pred1_[idx], pred2_[idx]);
          return false;
        }
      }
    }
    return true;
  }

  libavm_test::ACMRandom rnd_;
  uint16_t *pred1_;
  uint16_t *pred2_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BAWPTest);

BAWPTest::~BAWPTest() {}

void BAWPTest::SetUp() {
  rnd_.Reset(libavm_test::ACMRandom::DeterministicSeed());

  pred1_ = (uint16_t *)avm_memalign(
      16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));
  ASSERT_NE(pred1_, nullptr);
  pred2_ = (uint16_t *)avm_memalign(
      16, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(uint16_t));
  ASSERT_NE(pred2_, nullptr);
  for (int i = 0; i < (MAX_SB_SIZE * MAX_SB_SIZE); ++i) {
    pred1_[i] = rnd_.Rand16();
    pred2_[i] = pred1_[i];
  }
}

void BAWPTest::TearDown() {
  avm_free(pred1_);
  avm_free(pred2_);
}

void BAWPTest::RunCheckOutput(make_bawp_func test_impl, BLOCK_SIZE bsize) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int16_t alpha = 320;
  const int32_t beta = -42036;
  const int shift = 8;
  int bd[3] = { 8, 10, 12 };
  for (int i = 0; i < 3; ++i) {
    av2_make_bawp_block_c(pred1_, MAX_SB_SIZE, alpha, beta, shift, w, h, bd[i]);
    test_impl(pred2_, MAX_SB_SIZE, alpha, beta, shift, w, h, bd[i]);

    ASSERT_EQ(CheckResult(w, h), true);
  }
}

void BAWPTest::RunSpeedTest(make_bawp_func test_impl, BLOCK_SIZE bsize) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int num_loops = 1000000000 / (w + h);
  const int16_t alpha = 320;
  const int32_t beta = -42036;
  const int shift = 8;
  int bd = 8;

  make_bawp_func functions[2] = { av2_make_bawp_block_c, test_impl };
  double elapsed_time[2] = { 0.0 };
  for (int i = 0; i < 2; ++i) {
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    make_bawp_func func = functions[i];
    for (int j = 0; j < num_loops; ++j) {
      func(pred1_, MAX_SB_SIZE, alpha, beta, shift, w, h, bd);
    }
    avm_usec_timer_mark(&timer);
    const double time = static_cast<double>(avm_usec_timer_elapsed(&timer));
    elapsed_time[i] = 1000.0 * time;
  }
  printf("BAWP %3dx%-3d: c_time=%7.2fs, simd_time=%7.2fs, scaling=%3.2f\n", w,
         h, elapsed_time[0], elapsed_time[1],
         elapsed_time[0] / elapsed_time[1]);
}

TEST_P(BAWPTest, CheckOutput) { RunCheckOutput(GET_PARAM(0), GET_PARAM(1)); }

TEST_P(BAWPTest, DISABLED_Speed) { RunSpeedTest(GET_PARAM(0), GET_PARAM(1)); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, BAWPTest,
    ::testing::Combine(::testing::Values(&av2_make_bawp_block_avx2),
                       ::testing::ValuesIn(kValidBlockSize)));
#endif  // HAVE_AVX2
}  // namespace
