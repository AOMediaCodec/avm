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

#include "config/avm_dsp_rtcd.h"

#include "avm_ports/avm_timer.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/util.h"

namespace AV2CompAvgUpsampledPred {
#if (HAVE_SSE2 || HAVE_AVX2)
const BLOCK_SIZE kValidBlockSize[] = {
  BLOCK_8X8,     BLOCK_8X16,   BLOCK_16X8,    BLOCK_16X16,   BLOCK_16X32,
  BLOCK_32X16,   BLOCK_32X32,  BLOCK_32X64,   BLOCK_64X32,   BLOCK_64X64,
  BLOCK_64X128,  BLOCK_128X64, BLOCK_128X128, BLOCK_128X256, BLOCK_256X128,
  BLOCK_256X256, BLOCK_4X16,   BLOCK_16X4,    BLOCK_8X32,    BLOCK_32X8,
  BLOCK_16X64,   BLOCK_64X16,  BLOCK_4X32,    BLOCK_32X4,    BLOCK_8X64,
  BLOCK_64X8
};
#endif

typedef void (*highbd_comp_avg_upsampled_pred_func)(
    MACROBLOCKD *xd, const struct AV2Common *const cm, int mi_row, int mi_col,
    const MV *const mv, uint16_t *comp_pred16, const uint16_t *pred, int width,
    int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref,
    int ref_stride, int bd, int subpel_search, int is_scaled_ref);

typedef std::tuple<highbd_comp_avg_upsampled_pred_func, BLOCK_SIZE, int>
    HighbdCompAvgUpsampledPredParam;

class AV2HighbdCompAvgUpsampledPredTest
    : public ::testing::TestWithParam<HighbdCompAvgUpsampledPredParam> {
 public:
  ~AV2HighbdCompAvgUpsampledPredTest();
  void SetUp();

  void TearDown();

 protected:
  void RunCheckOutput(highbd_comp_avg_upsampled_pred_func test_impl,
                      BLOCK_SIZE bsize, int bd_);
  void RunSpeedTest(highbd_comp_avg_upsampled_pred_func test_impl,
                    BLOCK_SIZE bsize, int bd_);
  bool CheckResult(int width, int height) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int idx = y * width + x;
        if (comp_pred1_[idx] != comp_pred2_[idx]) {
          printf("%dx%d mismatch @%d(%d,%d) ", width, height, idx, y, x);
          printf("%d != %d ", comp_pred1_[idx], comp_pred2_[idx]);
          return false;
        }
      }
    }
    return true;
  }

  libavm_test::ACMRandom rnd_;
  uint16_t *comp_pred1_;
  uint16_t *comp_pred2_;
  uint16_t *pred_;
  uint16_t *ref_buffer_;
  uint16_t *ref_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2HighbdCompAvgUpsampledPredTest);

AV2HighbdCompAvgUpsampledPredTest::~AV2HighbdCompAvgUpsampledPredTest() { ; }

void AV2HighbdCompAvgUpsampledPredTest::SetUp() {
  rnd_.Reset(libavm_test::ACMRandom::DeterministicSeed());

  comp_pred1_ = (uint16_t *)avm_memalign(
      16, (MAX_SB_SQUARE + (8 * MAX_SB_SIZE)) * sizeof(*comp_pred1_));
  comp_pred2_ = (uint16_t *)avm_memalign(
      16, (MAX_SB_SQUARE + (8 * MAX_SB_SIZE)) * sizeof(*comp_pred2_));
  pred_ = (uint16_t *)avm_memalign(16, MAX_SB_SQUARE * sizeof(*pred_));
  ref_buffer_ = (uint16_t *)avm_memalign(
      16, (8 + MAX_SB_SIZE) * (8 + MAX_SB_SIZE) * sizeof(*ref_buffer_));
  ref_ = ref_buffer_ + 4 * (MAX_SB_SIZE + 8) + 4;
}

void AV2HighbdCompAvgUpsampledPredTest::TearDown() {
  avm_free(comp_pred1_);
  avm_free(comp_pred2_);
  avm_free(pred_);
  avm_free(ref_buffer_);
  libavm_test::ClearSystemState();
}

void AV2HighbdCompAvgUpsampledPredTest::RunCheckOutput(
    highbd_comp_avg_upsampled_pred_func test_impl, BLOCK_SIZE bsize, int bd_) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int is_scaled_ref = 0;

  for (int i = 0; i < MAX_SB_SQUARE; ++i) {
    pred_[i] = rnd_.Rand16() & ((1 << bd_) - 1);
  }
  for (int i = 0; i < (8 + MAX_SB_SIZE) * (8 + MAX_SB_SIZE); ++i) {
    ref_buffer_[i] = rnd_.Rand16() & ((1 << bd_) - 1);
  }

  for (int subpel_search = USE_2_TAPS; subpel_search <= USE_8_TAPS;
       ++subpel_search) {
    for (int sub = 0; sub < 64; ++sub) {
      const int subx = sub & 0x7;
      const int suby = sub >> 3;

      avm_highbd_comp_avg_upsampled_pred_c(
          NULL, NULL, 0, 0, NULL, comp_pred1_, pred_, w, h, subx, suby, ref_,
          MAX_SB_SIZE, bd_, subpel_search, is_scaled_ref);

      test_impl(NULL, NULL, 0, 0, NULL, comp_pred2_, pred_, w, h, subx, suby,
                ref_, MAX_SB_SIZE, bd_, subpel_search, is_scaled_ref);

      ASSERT_TRUE(CheckResult(w, h)) << "sub (" << subx << "," << suby << ")";
    }
  }
}

void AV2HighbdCompAvgUpsampledPredTest::RunSpeedTest(
    highbd_comp_avg_upsampled_pred_func test_impl, BLOCK_SIZE bsize, int bd_) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  constexpr int kNumIters = 100000;
  double elapsed_time[2] = { 0.0 };
  const int subx = 0;
  const int suby = 0;
  const int subpel_search = USE_8_TAPS;
  const int is_scaled_ref = 0;
  for (int i = 0; i < MAX_SB_SQUARE; ++i) {
    pred_[i] = rnd_.Rand16() & ((1 << bd_) - 1);
  }
  for (int i = 0; i < (8 + MAX_SB_SIZE) * (8 + MAX_SB_SIZE); ++i) {
    ref_buffer_[i] = rnd_.Rand16() & ((1 << bd_) - 1);
  }

  avm_usec_timer timer1;
  avm_usec_timer_start(&timer1);
  for (int j = 0; j < kNumIters; ++j) {
    avm_highbd_comp_avg_upsampled_pred_c(
        NULL, NULL, 0, 0, NULL, comp_pred1_, pred_, w, h, subx, suby, ref_,
        MAX_SB_SIZE, bd_, subpel_search, is_scaled_ref);
  }
  avm_usec_timer_mark(&timer1);
  const double time1 = static_cast<double>(avm_usec_timer_elapsed(&timer1));
  elapsed_time[0] = 1000.0 * time1;

  avm_usec_timer timer2;
  avm_usec_timer_start(&timer2);
  for (int j = 0; j < kNumIters; ++j) {
    test_impl(NULL, NULL, 0, 0, NULL, comp_pred1_, pred_, w, h, subx, suby,
              ref_, MAX_SB_SIZE, bd_, subpel_search, is_scaled_ref);
  }
  avm_usec_timer_mark(&timer2);
  const double time2 = static_cast<double>(avm_usec_timer_elapsed(&timer2));
  elapsed_time[1] = 1000.0 * time2;

  printf("CompAvg %3dx%-3d: c_time=%7.2fs, simd_time=%7.2fs, scaling=%3.2f\n",
         w, h, elapsed_time[0], elapsed_time[1],
         elapsed_time[0] / elapsed_time[1]);
}

TEST_P(AV2HighbdCompAvgUpsampledPredTest, CheckOutput) {
  RunCheckOutput(GET_PARAM(0), GET_PARAM(1), GET_PARAM(2));
}

TEST_P(AV2HighbdCompAvgUpsampledPredTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(0), GET_PARAM(1), GET_PARAM(2));
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2HighbdCompAvgUpsampledPredTest,
    ::testing::Combine(
        ::testing::Values(&avm_highbd_comp_avg_upsampled_pred_avx2),
        ::testing::ValuesIn(kValidBlockSize), ::testing::Values(10)));
#endif

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, AV2HighbdCompAvgUpsampledPredTest,
    ::testing::Combine(
        ::testing::Values(&avm_highbd_comp_avg_upsampled_pred_sse2),
        ::testing::ValuesIn(kValidBlockSize), ::testing::Values(10)));
#endif
}  // namespace AV2CompAvgUpsampledPred
