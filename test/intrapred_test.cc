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

#include <string>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "av2/common/blockd.h"
#include "av2/common/common.h"
#include "av2/common/pred_common.h"
#include "avm_mem/avm_mem.h"

namespace {

using libavm_test::ACMRandom;

const int count_test_block = 100000;

typedef void (*HighbdIntraPred)(uint16_t *dst, ptrdiff_t stride,
                                const uint16_t *above, const uint16_t *left,
                                int bps);
}  // namespace

// NOTE: Under gcc version 7.3.0 (Debian 7.3.0-5), if this template is in the
// anonymous namespace, then we get a strange compiler warning in
// the begin() and end() methods of the ParamGenerator template class in
// gtest/internal/gtest-param-util.h:
//   warning: ‘<anonymous>’ is used uninitialized in this function
// As a workaround, put this template outside the anonymous namespace.
// See bug aomedia:2003.
template <typename FuncType>
struct IntraPredFunc {
  IntraPredFunc(FuncType pred = NULL, FuncType ref = NULL,
                int block_width_value = 0, int block_height_value = 0,
                int bit_depth_value = 0)
      : pred_fn(pred), ref_fn(ref), block_width(block_width_value),
        block_height(block_height_value), bit_depth(bit_depth_value) {}

  FuncType pred_fn;
  FuncType ref_fn;
  int block_width;
  int block_height;
  int bit_depth;
};

namespace {

template <typename FuncType, typename Pixel>
class AV2IntraPredTest
    : public ::testing::TestWithParam<IntraPredFunc<FuncType> > {
 public:
  void RunTest(Pixel *left_col, Pixel *above_data, Pixel *dst, Pixel *ref_dst) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int block_width = params_.block_width;
    const int block_height = params_.block_height;
    above_row_ = above_data + 16;
    left_col_ = left_col;
    dst_ = dst;
    ref_dst_ = ref_dst;
    int error_count = 0;
    for (int i = 0; i < count_test_block; ++i) {
      // Fill edges with random data, try first with saturated values.
      for (int x = -1; x <= block_width * 2; x++) {
        if (i == 0) {
          above_row_[x] = mask_;
        } else {
          above_row_[x] = rnd.Rand16() & mask_;
        }
      }
      for (int y = 0; y < block_height; y++) {
        if (i == 0) {
          left_col_[y] = mask_;
        } else {
          left_col_[y] = rnd.Rand16() & mask_;
        }
      }
      Predict();
      CheckPrediction(i, &error_count);
    }
    ASSERT_EQ(0, error_count);
  }
  void RunSpeedTest(Pixel *left_col, Pixel *above_data, Pixel *dst,
                    Pixel *ref_dst) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int block_width = params_.block_width;
    const int block_height = params_.block_height;
    above_row_ = above_data + 16;
    left_col_ = left_col;
    dst_ = dst;
    ref_dst_ = ref_dst;
    int error_count = 0;
    const int numIter = 100;

    int c_sum_time = 0;
    int simd_sum_time = 0;
    for (int i = 0; i < count_test_block; ++i) {
      // Fill edges with random data, try first with saturated values.
      for (int x = -1; x <= block_width * 2; x++) {
        if (i == 0) {
          above_row_[x] = mask_;
        } else {
          above_row_[x] = rnd.Rand16() & mask_;
        }
      }
      for (int y = 0; y < block_height; y++) {
        if (i == 0) {
          left_col_[y] = mask_;
        } else {
          left_col_[y] = rnd.Rand16() & mask_;
        }
      }

      avm_usec_timer c_timer_;
      avm_usec_timer_start(&c_timer_);

      PredictRefSpeedTest(numIter);

      avm_usec_timer_mark(&c_timer_);

      avm_usec_timer simd_timer_;
      avm_usec_timer_start(&simd_timer_);

      PredictFncSpeedTest(numIter);

      avm_usec_timer_mark(&simd_timer_);

      c_sum_time += static_cast<int>(avm_usec_timer_elapsed(&c_timer_));
      simd_sum_time += static_cast<int>(avm_usec_timer_elapsed(&simd_timer_));

      CheckPrediction(i, &error_count);
    }

    printf(
        "blockWxH = %d x %d c_time = %d \t simd_time = %d \t Gain = %4.2f \n",
        block_width, block_height, c_sum_time, simd_sum_time,
        (static_cast<float>(c_sum_time) / static_cast<float>(simd_sum_time)));
    ASSERT_EQ(0, error_count);
  }

 protected:
  virtual void SetUp() {
    params_ = this->GetParam();
    stride_ = params_.block_width * 3;
    mask_ = (1 << params_.bit_depth) - 1;
  }

  virtual void Predict() = 0;

  virtual void PredictRefSpeedTest(int num) = 0;
  virtual void PredictFncSpeedTest(int num) = 0;

  void CheckPrediction(int test_case_number, int *error_count) const {
    // For each pixel ensure that the calculated value is the same as reference.
    const int block_width = params_.block_width;
    const int block_height = params_.block_height;
    for (int y = 0; y < block_height; y++) {
      for (int x = 0; x < block_width; x++) {
        *error_count += ref_dst_[x + y * stride_] != dst_[x + y * stride_];
        if (*error_count == 1) {
          ASSERT_EQ(ref_dst_[x + y * stride_], dst_[x + y * stride_])
              << " Failed on Test Case Number " << test_case_number
              << " location: x = " << x << " y = " << y;
        }
      }
    }
  }

  Pixel *above_row_;
  Pixel *left_col_;
  Pixel *dst_;
  Pixel *ref_dst_;
  ptrdiff_t stride_;
  int mask_;

  IntraPredFunc<FuncType> params_;
};

class HighbdIntraPredTest : public AV2IntraPredTest<HighbdIntraPred, uint16_t> {
 protected:
  void Predict() {
    const int bit_depth = params_.bit_depth;
    params_.ref_fn(ref_dst_, stride_, above_row_, left_col_, bit_depth);
    ASM_REGISTER_STATE_CHECK(
        params_.pred_fn(dst_, stride_, above_row_, left_col_, bit_depth));
  }
  void PredictRefSpeedTest(int num) {
    const int bit_depth = params_.bit_depth;
    for (int i = 0; i < num; i++) {
      params_.ref_fn(ref_dst_, stride_, above_row_, left_col_, bit_depth);
    }
  }
  void PredictFncSpeedTest(int num) {
    const int bit_depth = params_.bit_depth;
    for (int i = 0; i < num; i++) {
      params_.pred_fn(dst_, stride_, above_row_, left_col_, bit_depth);
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HighbdIntraPredTest);

// Suppress an unitialized warning. Once there are implementations to test then
// this can be restored.
TEST_P(HighbdIntraPredTest, Bitexact) {
  // max block size is 64
  DECLARE_ALIGNED(16, uint16_t, left_col[2 * 64]);
  DECLARE_ALIGNED(16, uint16_t, above_data[2 * 64 + 64]);
  DECLARE_ALIGNED(16, uint16_t, dst[3 * 64 * 64]);
  DECLARE_ALIGNED(16, uint16_t, ref_dst[3 * 64 * 64]);
  av2_zero(left_col);
  av2_zero(above_data);
  RunTest(left_col, above_data, dst, ref_dst);
}

TEST_P(HighbdIntraPredTest, DISABLED_Speed) {
  // max block size is 64
  DECLARE_ALIGNED(16, uint16_t, left_col[2 * 64]);
  DECLARE_ALIGNED(16, uint16_t, above_data[2 * 64 + 64]);
  DECLARE_ALIGNED(16, uint16_t, dst[3 * 64 * 64]);
  DECLARE_ALIGNED(16, uint16_t, ref_dst[3 * 64 * 64]);
  av2_zero(left_col);
  av2_zero(above_data);
  RunSpeedTest(left_col, above_data, dst, ref_dst);
}

// -----------------------------------------------------------------------------
// High Bit Depth Tests
#define highbd_entry(type, width, height, opt, bd)                          \
  IntraPredFunc<HighbdIntraPred>(                                           \
      &avm_highbd_##type##_predictor_##width##x##height##_##opt,            \
      &avm_highbd_##type##_predictor_##width##x##height##_c, width, height, \
      bd)

#define highbd_intrapred(type, opt, bd)                                       \
  highbd_entry(type, 4, 4, opt, bd), highbd_entry(type, 4, 8, opt, bd),       \
      highbd_entry(type, 8, 4, opt, bd), highbd_entry(type, 8, 8, opt, bd),   \
      highbd_entry(type, 8, 16, opt, bd), highbd_entry(type, 16, 8, opt, bd), \
      highbd_entry(type, 16, 16, opt, bd),                                    \
      highbd_entry(type, 16, 32, opt, bd),                                    \
      highbd_entry(type, 32, 16, opt, bd), highbd_entry(type, 32, 32, opt, bd)

#if HAVE_NEON
const IntraPredFunc<HighbdIntraPred> HighbdIntraPredTestVectorNeon[] = {
  highbd_entry(dc, 4, 4, neon, 8),   highbd_entry(dc, 8, 8, neon, 8),
  highbd_entry(dc, 16, 16, neon, 8), highbd_entry(dc, 32, 32, neon, 8),
  highbd_entry(dc, 64, 64, neon, 8),
};

INSTANTIATE_TEST_SUITE_P(NEON, HighbdIntraPredTest,
                         ::testing::ValuesIn(HighbdIntraPredTestVectorNeon));

#endif  // HAVE_NEON

#if HAVE_SSE2
const IntraPredFunc<HighbdIntraPred> HighbdIntraPredTestVectorSse2[] = {
  highbd_intrapred(dc, sse2, 8),       highbd_intrapred(dc, sse2, 10),
  highbd_intrapred(dc, sse2, 12),      highbd_intrapred(dc_top, sse2, 8),
  highbd_intrapred(dc_top, sse2, 10),  highbd_intrapred(dc_top, sse2, 12),
  highbd_intrapred(dc_left, sse2, 8),  highbd_intrapred(dc_left, sse2, 10),
  highbd_intrapred(dc_left, sse2, 12), highbd_intrapred(dc_128, sse2, 8),
  highbd_intrapred(dc_128, sse2, 10),  highbd_intrapred(dc_128, sse2, 12),
  highbd_intrapred(v, sse2, 8),        highbd_intrapred(v, sse2, 10),
  highbd_intrapred(v, sse2, 12),       highbd_intrapred(h, sse2, 8),
  highbd_intrapred(h, sse2, 10),       highbd_intrapred(h, sse2, 12),
};

INSTANTIATE_TEST_SUITE_P(SSE2, HighbdIntraPredTest,
                         ::testing::ValuesIn(HighbdIntraPredTestVectorSse2));
#endif  // HAVE_SSE2
}  // namespace
