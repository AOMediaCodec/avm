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
    src1_ = reinterpret_cast<uint16_t *>(avm_memalign(16, 256 * 256));
    src2_ = reinterpret_cast<uint16_t *>(avm_memalign(16, 256 * 256));

    ASSERT_TRUE(src1_ != NULL);
    ASSERT_TRUE(src2_ != NULL);
  }

  virtual void TearDown() {
    libavm_test::ClearSystemState();
    avm_free(src1_);
    avm_free(src2_);
  }
  void RunTest(int isRandom, int width, int height, int run_times, int bd);

  void GenRandomData(int width, int height, int stride, int stride2, int bd) {
    if (bd == 10) {
      for (int ii = 0; ii < height; ii++) {
        for (int jj = 0; jj < width; jj++) {
          src1_[ii * stride + jj] = rnd_.Rand16() & 0x3FF;
          src2_[ii * stride2 + jj] = rnd_.Rand16() & 0x3FF;
        }
      }
    } else {
      for (int ii = 0; ii < height; ii++) {
        for (int jj = 0; jj < width; jj++) {
          src1_[ii * stride + jj] = rnd_.Rand16() & 0xFFF;
          src2_[ii * stride2 + jj] = rnd_.Rand16() & 0xFFF;
        }
      }
    }
  }

  void GenExtremeData(int width, int height, int stride, uint16_t *data,
                      int stride2, uint16_t *data2, uint16_t val, int bd) {
    if (bd == 10) {
      for (int ii = 0; ii < height; ii++) {
        for (int jj = 0; jj < width; jj++) {
          data[ii * stride + jj] = val;
          data2[ii * stride2 + jj] = (1023 - val);
        }
      }
    } else {
      for (int ii = 0; ii < height; ii++) {
        for (int jj = 0; jj < width; jj++) {
          data[ii * stride + jj] = val;
          data2[ii * stride2 + jj] = (4095 - val);
        }
      }
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
    const int stride = width;
    const int stride2 = width;
    if (isRandom) {
      GenRandomData(width, height, stride, stride2, BD);
    } else {
      const int msb = BD;
      const uint16_t limit = (1 << msb) - 1;
      if (k == 0) {
        GenExtremeData(width, height, stride, src1_, stride2, src2_, limit, BD);
      } else {
        GenExtremeData(width, height, stride, src1_, stride2, src2_, 0, BD);
      }
    }
    double sigma[1] = { 2.1002103677063437 };
    DECLARE_ALIGNED(16, unsigned int, accumulator_ref[1024 * 3]);
    DECLARE_ALIGNED(16, uint16_t, count_ref[1024 * 3]);
    memset(accumulator_ref, 0, 1024 * 3 * sizeof(accumulator_ref[0]));
    memset(count_ref, 0, 1024 * 3 * sizeof(count_ref[0]));
    DECLARE_ALIGNED(16, unsigned int, accumulator_mod[1024 * 3]);
    DECLARE_ALIGNED(16, uint16_t, count_mod[1024 * 3]);
    memset(accumulator_mod, 0, 1024 * 3 * sizeof(accumulator_mod[0]));
    memset(count_mod, 0, 1024 * 3 * sizeof(count_mod[0]));

    assert(width == 32 && height == 32);
    const BLOCK_SIZE block_size = BLOCK_32X32;
    const MV subblock_mvs[4] = { { 0, 0 }, { 5, 5 }, { 7, 8 }, { 2, 10 } };
    const int subblock_mses[4] = { 15, 16, 17, 18 };
    const int q_factor = 12;
    const int filter_strength = 5;
    const int mb_row = 0;
    const int mb_col = 0;
    const int num_planes = 1;
    YV12_BUFFER_CONFIG *ref_frame =
        (YV12_BUFFER_CONFIG *)malloc(sizeof(YV12_BUFFER_CONFIG));
    ref_frame->y_crop_height = 360;
    ref_frame->y_crop_width = 540;
    ref_frame->heights[0] = height;
    ref_frame->strides[0] = stride;
    DECLARE_ALIGNED(16, uint16_t, src[1024 * 3]);
    ref_frame->buffer_alloc = (uint8_t *)src;
    ref_frame->buffers[0] = src;
    memcpy(src, src1_, 1024 * 3 * sizeof(uint16_t));

    MACROBLOCKD *mbd = (MACROBLOCKD *)malloc(sizeof(MACROBLOCKD));
    mbd->plane[0].subsampling_y = 0;
    mbd->plane[0].subsampling_x = 0;
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
      const int elapsed_time_c =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int j = 0; j < run_times; j++) {
        params_.tst_func(ref_frame, mbd, block_size, mb_row, mb_col, num_planes,
                         sigma, subblock_mvs, subblock_mses, q_factor,
                         filter_strength, src2_, accumulator_mod, count_mod);
      }
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf(
          "c_time=%d \t simd_time=%d \t "
          "gain=%f\t width=%d\t height=%d \n",
          elapsed_time_c, elapsed_time_simd,
          (float)((float)elapsed_time_c / (float)elapsed_time_simd), width,
          height);

    } else {
      for (int i = 0, l = 0; i < height; i++) {
        for (int j = 0; j < width; j++, l++) {
          EXPECT_EQ(accumulator_ref[l], accumulator_mod[l])
              << "Error:" << k << " SSE Sum Test [" << width << "x" << height
              << "] C accumulator does not match optimized accumulator.";
          EXPECT_EQ(count_ref[l], count_mod[l])
              << "Error:" << k << " SSE Sum Test [" << width << "x" << height
              << "] C count does not match optimized count.";
        }
      }
    }

    free(ref_frame);
    free(mbd);
  }
}

TEST_P(HBDTemporalFilterTest, OperationCheck) {
  for (int height = 32; height <= 32; height = height * 2) {
    RunTest(1, height, height, 1, 10);  // GenRandomData
  }
}

TEST_P(HBDTemporalFilterTest, ExtremeValues) {
  for (int height = 32; height <= 32; height = height * 2) {
    RunTest(0, height, height, 1, 10);
  }
}

TEST_P(HBDTemporalFilterTest, DISABLED_Speed) {
  for (int height = 32; height <= 32; height = height * 2) {
    RunTest(1, height, height, 100000, 10);
  }
}
}  // namespace
