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

#include <cmath>
#include <cstdlib>
#include <string>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "avm_ports/mem.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "test/function_equivalence_test.h"

using libavm_test::ACMRandom;
using libavm_test::FunctionEquivalenceTest;
using ::testing::Combine;
using ::testing::Range;
using ::testing::Values;
using ::testing::ValuesIn;

namespace {

typedef void (*HBDTemporalFilterFunc)(
    const YV12_BUFFER_CONFIG *ref_frame, const MACROBLOCKD *mbd,
    const BLOCK_SIZE block_size, const int mb_row, const int mb_col,
    const int num_planes, const double *noise_level, const MV *subblock_mvs,
    const int *subblock_mses, const int q_factor, const int filter_strenght,
    const uint16_t *pred, uint32_t *accum, uint16_t *count);
typedef libavm_test::FuncParam<HBDTemporalFilterFunc>
    HBDTemporalFilterFuncParam;

typedef std::tuple<HBDTemporalFilterFuncParam, int> HBDTemporalFilterWithParam;

class HBDTemporalFilterTest
    : public ::testing::TestWithParam<HBDTemporalFilterWithParam> {
 public:
  virtual ~HBDTemporalFilterTest() {}
  virtual void SetUp() {
    params_ = GET_PARAM(0);
    rnd_.Reset(ACMRandom::DeterministicSeed());
    src1_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, 256 * 256 * sizeof(uint16_t)));
    src2_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, 256 * 256 * sizeof(uint16_t)));

    ASSERT_TRUE(src1_ != NULL);
    ASSERT_TRUE(src2_ != NULL);
  }

  virtual void TearDown() {
    libavm_test::ClearSystemState();
    avm_free(src1_);
    avm_free(src2_);
  }
  void RunTest(int isRandom, int width, int height, int run_times, int bd);

  void GenRandomData(int num_samples, int bd) {
    const uint16_t mask = (bd == 10) ? 0x3FF : 0xFFF;
    for (int ii = 0; ii < num_samples; ii++) {
      src1_[ii] = rnd_.Rand16() & mask;
      src2_[ii] = rnd_.Rand16() & mask;
    }
  }

  void GenExtremeData(int num_samples, uint16_t *data, uint16_t *data2,
                      uint16_t val, int bd) {
    const uint16_t max_val = (bd == 10) ? 1023 : 4095;
    for (int ii = 0; ii < num_samples; ii++) {
      data[ii] = val;
      data2[ii] = (max_val - val);
    }
  }

 protected:
  HBDTemporalFilterFuncParam params_;
  uint16_t *src1_;
  uint16_t *src2_;
  ACMRandom rnd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HBDTemporalFilterTest);

void HBDTemporalFilterTest::RunTest(int isRandom, int width, int height,
                                    int run_times, int BD) {
  avm_usec_timer ref_timer, test_timer;
  for (int k = 0; k < 3; k++) {
    const int total_samples = 4096 * 3;
    if (isRandom) {
      GenRandomData(total_samples, BD);
    } else {
      const int msb = BD;
      const uint16_t limit = (1 << msb) - 1;
      if (k == 0) {
        GenExtremeData(total_samples, src1_, src2_, limit, BD);
      } else {
        GenExtremeData(total_samples, src1_, src2_, 0, BD);
      }
    }
    double sigma[3] = { 2.1002103677063437, 2.1002103677063437,
                        2.1002103677063437 };
    DECLARE_ALIGNED(16, unsigned int, accumulator_ref[4096 * 3]);
    DECLARE_ALIGNED(16, uint16_t, count_ref[4096 * 3]);
    memset(accumulator_ref, 0, 4096 * 3 * sizeof(accumulator_ref[0]));
    memset(count_ref, 0, 4096 * 3 * sizeof(count_ref[0]));
    DECLARE_ALIGNED(16, unsigned int, accumulator_mod[4096 * 3]);
    DECLARE_ALIGNED(16, uint16_t, count_mod[4096 * 3]);
    memset(accumulator_mod, 0, 4096 * 3 * sizeof(accumulator_mod[0]));
    memset(count_mod, 0, 4096 * 3 * sizeof(count_mod[0]));

    assert(width == 64 && height == 64);
    const BLOCK_SIZE block_size = BLOCK_64X64;
    const MV subblock_mvs[16] = {
      { 0, 0 },   { 1, 2 },   { 2, 1 },  { 3, 3 }, { 5, 5 }, { 4, 6 },
      { 6, 4 },   { 7, 7 },   { 7, 8 },  { 8, 7 }, { 9, 6 }, { 6, 9 },
      { 32, 30 }, { 50, 42 }, { 11, 4 }, { 4, 11 }
    };
    const int subblock_mses[16] = { 15, 16, 17, 18, 19, 20, 21, 22,
                                    23, 24, 25, 26, 27, 28, 29, 30 };
    const int q_factor = 12;
    const int filter_strength = 5;
    const int mb_row = 0;
    const int mb_col = 0;
    const int num_planes = 3;
    YV12_BUFFER_CONFIG *ref_frame =
        (YV12_BUFFER_CONFIG *)malloc(sizeof(YV12_BUFFER_CONFIG));
    memset(ref_frame, 0, sizeof(YV12_BUFFER_CONFIG));
    ref_frame->y_crop_height = 360;
    ref_frame->y_crop_width = 540;
    ref_frame->heights[0] = 64;
    ref_frame->strides[0] = 64;
    ref_frame->heights[1] = 32;
    ref_frame->strides[1] = 32;
    DECLARE_ALIGNED(16, uint16_t, src[4096 * 3]);
    ref_frame->buffer_alloc = (uint8_t *)src;
    ref_frame->buffers[0] = src;
    ref_frame->buffers[1] = src + 4096;
    ref_frame->buffers[2] = src + 4096 + 1024;
    memcpy(src, src1_, 4096 * 3 * sizeof(uint16_t));

    MACROBLOCKD *mbd = (MACROBLOCKD *)malloc(sizeof(MACROBLOCKD));
    memset(mbd, 0, sizeof(MACROBLOCKD));
    mbd->plane[0].subsampling_y = 0;
    mbd->plane[0].subsampling_x = 0;
    mbd->plane[1].subsampling_y = 1;
    mbd->plane[1].subsampling_x = 1;
    mbd->plane[2].subsampling_y = 1;
    mbd->plane[2].subsampling_x = 1;
    mbd->bd = BD;

    params_.ref_func(ref_frame, mbd, block_size, mb_row, mb_col, num_planes,
                     sigma, subblock_mvs, subblock_mses, q_factor,
                     filter_strength, src2_, accumulator_ref, count_ref);
    params_.tst_func(ref_frame, mbd, block_size, mb_row, mb_col, num_planes,
                     sigma, subblock_mvs, subblock_mses, q_factor,
                     filter_strength, src2_, accumulator_mod, count_mod);

    if (run_times > 1) {
      avm_usec_timer_start(&ref_timer);
      for (int j = 0; j < run_times; j++) {
        params_.ref_func(ref_frame, mbd, block_size, mb_row, mb_col, num_planes,
                         sigma, subblock_mvs, subblock_mses, q_factor,
                         filter_strength, src2_, accumulator_ref, count_ref);
      }
      avm_usec_timer_mark(&ref_timer);
      const int elapsed_time_ref =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int j = 0; j < run_times; j++) {
        params_.tst_func(ref_frame, mbd, block_size, mb_row, mb_col, num_planes,
                         sigma, subblock_mvs, subblock_mses, q_factor,
                         filter_strength, src2_, accumulator_mod, count_mod);
      }
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_tst =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf(
          "ref_time=%d us \t tst_time=%d us \t "
          "gain=%.2fx (%.1f%% reduction)\t width=%d\t height=%d \n",
          elapsed_time_ref, elapsed_time_tst,
          (float)elapsed_time_ref / (float)elapsed_time_tst,
          (1.0f - (float)elapsed_time_tst / (float)elapsed_time_ref) * 100.0f,
          width, height);

    } else {
      // Check Plane 0 (Y: 64x64 = 4096 pixels)
      for (int l = 0; l < 4096; l++) {
        EXPECT_EQ(accumulator_ref[l], accumulator_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref accumulator does not match Test accumulator at plane 0 "
               "index "
            << l;
        EXPECT_EQ(count_ref[l], count_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref count does not match Test count at plane 0 index " << l;
      }
      // Check Plane 1 (U: 32x32 = 1024 pixels)
      for (int l = 4096; l < 4096 + 1024; l++) {
        EXPECT_EQ(accumulator_ref[l], accumulator_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref accumulator does not match Test accumulator at plane 1 "
               "index "
            << l;
        EXPECT_EQ(count_ref[l], count_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref count does not match Test count at plane 1 index " << l;
      }
      // Check Plane 2 (V: 32x32 = 1024 pixels)
      for (int l = 8192; l < 8192 + 1024; l++) {
        EXPECT_EQ(accumulator_ref[l], accumulator_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref accumulator does not match Test accumulator at plane 2 "
               "index "
            << l;
        EXPECT_EQ(count_ref[l], count_mod[l])
            << "Error:" << k << " SSE Sum Test [" << width << "x" << height
            << "] Ref count does not match Test count at plane 2 index " << l;
      }
    }

    free(ref_frame);
    free(mbd);
  }
}

TEST_P(HBDTemporalFilterTest, OperationCheck) {
  RunTest(1, 64, 64, 1, 10);  // GenRandomData
}

TEST_P(HBDTemporalFilterTest, ExtremeValues) { RunTest(0, 64, 64, 1, 10); }

TEST_P(HBDTemporalFilterTest, DISABLED_Speed) { RunTest(1, 64, 64, 10000, 10); }

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, HBDTemporalFilterTest,
    ::testing::Values(HBDTemporalFilterWithParam(
        HBDTemporalFilterFuncParam(&av2_highbd_apply_temporal_filter_c,
                                   &av2_highbd_apply_temporal_filter_sse2),
        10)));
#endif  // HAVE_SSE2

}  // namespace
