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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tuple>

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "avm_ports/mem.h"
#include "av2/common/reconinter.h"
#include "av2/common/scan.h"
#include "av2/common/txb_common.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {
using libavm_test::ACMRandom;

typedef void (*buildcompdiffwtdmaskd16_func)(
    uint8_t *mask, DIFFWTD_MASK_TYPE mask_type, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride, int h, int w,
    ConvolveParams *conv_params, int bd);

typedef std::tuple<int, buildcompdiffwtdmaskd16_func, BLOCK_SIZE>
    BuildCompDiffwtdMaskD16Param;

#if HAVE_SSE4_1 || HAVE_NEON
::testing::internal::ParamGenerator<BuildCompDiffwtdMaskD16Param> BuildParams(
    buildcompdiffwtdmaskd16_func filter) {
  return ::testing::Combine(::testing::Range(8, 13, 2),
                            ::testing::Values(filter),
                            ::testing::Range(BLOCK_4X4, BLOCK_SIZES_ALL));
}
#endif
class BuildCompDiffwtdMaskD16Test
    : public ::testing::TestWithParam<BuildCompDiffwtdMaskD16Param> {
 public:
  ~BuildCompDiffwtdMaskD16Test() {}
  virtual void TearDown() { libavm_test::ClearSystemState(); }
  void SetUp() { rnd_.Reset(ACMRandom::DeterministicSeed()); }

 protected:
  void RunCheckOutput(buildcompdiffwtdmaskd16_func test_impl);
  void RunSpeedTest(buildcompdiffwtdmaskd16_func test_impl,
                    DIFFWTD_MASK_TYPE mask_type);
  libavm_test::ACMRandom rnd_;
};  // class BuildCompDiffwtdMaskD16Test
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BuildCompDiffwtdMaskD16Test);

void BuildCompDiffwtdMaskD16Test::RunCheckOutput(
    buildcompdiffwtdmaskd16_func test_impl) {
  const int block_idx = GET_PARAM(2);
  const int bd = GET_PARAM(0);
  const int width = block_size_wide[block_idx];
  const int height = block_size_high[block_idx];
  DECLARE_ALIGNED(16, uint8_t, mask_ref[2 * MAX_SB_SQUARE]);
  DECLARE_ALIGNED(16, uint8_t, mask_test[2 * MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint16_t, src0[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint16_t, src1[MAX_SB_SQUARE]);

  ConvolveParams conv_params = get_conv_params_no_round(0, 0, NULL, 0, 1, bd);

  int in_precision =
      bd + 2 * FILTER_BITS - conv_params.round_0 - conv_params.round_1 + 2;
  assert(in_precision >= 0);  // Ensure left-shift is non-negative.
  assert(in_precision < 32);  // Ensure left-shift doesn't overflow.

  for (int i = 0; i < MAX_SB_SQUARE; i++) {
    src0[i] = rnd_.Rand16() & ((1 << in_precision) - 1);
    src1[i] = rnd_.Rand16() & ((1 << in_precision) - 1);
  }

  for (int mask_type = 0; mask_type < DIFFWTD_MASK_TYPES; mask_type++) {
    av2_build_compound_diffwtd_mask_d16_c(
        mask_ref, (DIFFWTD_MASK_TYPE)mask_type, src0, width, src1, width,
        height, width, &conv_params, bd);

    test_impl(mask_test, (DIFFWTD_MASK_TYPE)mask_type, src0, width, src1, width,
              height, width, &conv_params, bd);

    for (int r = 0; r < height; ++r) {
      for (int c = 0; c < width; ++c) {
        ASSERT_EQ(mask_ref[c + r * width], mask_test[c + r * width])
            << "Mismatch at unit tests for BuildCompDiffwtdMaskD16Test\n"
            << " Pixel mismatch at index " << "[" << r << "," << c << "] "
            << " @ " << width << "x" << height << " inv " << mask_type;
      }
    }
  }
}

void BuildCompDiffwtdMaskD16Test::RunSpeedTest(
    buildcompdiffwtdmaskd16_func test_impl, DIFFWTD_MASK_TYPE mask_type) {
  const int block_idx = GET_PARAM(2);
  const int bd = GET_PARAM(0);
  const int width = block_size_wide[block_idx];
  const int height = block_size_high[block_idx];
  DECLARE_ALIGNED(16, uint8_t, mask[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint16_t, src0[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint16_t, src1[MAX_SB_SQUARE]);

  ConvolveParams conv_params = get_conv_params_no_round(0, 0, NULL, 0, 1, bd);

  int in_precision =
      bd + 2 * FILTER_BITS - conv_params.round_0 - conv_params.round_1 + 2;
  assert(in_precision >= 0);  // Ensure left-shift is non-negative.
  assert(in_precision < 32);  // Ensure left-shift doesn't overflow.

  for (int i = 0; i < MAX_SB_SQUARE; i++) {
    src0[i] = rnd_.Rand16() & ((1 << in_precision) - 1);
    src1[i] = rnd_.Rand16() & ((1 << in_precision) - 1);
  }

  const int num_loops = 10000000 / (width + height);
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);

  for (int i = 0; i < num_loops; ++i)
    av2_build_compound_diffwtd_mask_d16_c(mask, mask_type, src0, width, src1,
                                          width, height, width, &conv_params,
                                          bd);

  avm_usec_timer_mark(&timer);
  const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));

  avm_usec_timer timer1;
  avm_usec_timer_start(&timer1);

  for (int i = 0; i < num_loops; ++i)
    test_impl(mask, mask_type, src0, width, src1, width, height, width,
              &conv_params, bd);

  avm_usec_timer_mark(&timer1);
  const int elapsed_time1 = static_cast<int>(avm_usec_timer_elapsed(&timer1));
  printf("av2_build_compound_diffwtd_mask_d16  %3dx%-3d: %7.2f \n", width,
         height, elapsed_time / double(elapsed_time1));
}
TEST_P(BuildCompDiffwtdMaskD16Test, CheckOutput) {
  RunCheckOutput(GET_PARAM(1));
}

TEST_P(BuildCompDiffwtdMaskD16Test, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(1), DIFFWTD_38);
  RunSpeedTest(GET_PARAM(1), DIFFWTD_38_INV);
}

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, BuildCompDiffwtdMaskD16Test,
    BuildParams(av2_build_compound_diffwtd_mask_d16_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, BuildCompDiffwtdMaskD16Test,
                         BuildParams(av2_build_compound_diffwtd_mask_d16_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, BuildCompDiffwtdMaskD16Test,
                         BuildParams(av2_build_compound_diffwtd_mask_d16_neon));
#endif

typedef void (*RefinemvPadMCBorderFunc)(const uint16_t *src, int src_stride,
                                        uint16_t *dst, int dst_stride, int x0,
                                        int y0, int b_w, int b_h,
                                        const ReferenceArea *ref_area);

using std::get;
using std::make_tuple;
using std::tuple;

static constexpr int kSpeedIterations = 10000;
static constexpr int kMaxDimension = 2 * (REF_BUFFER_WIDTH + 1);

// <width, height, bit_depth, subtract>
typedef tuple<int, int, int, RefinemvPadMCBorderFunc> Params;

class RefinemvPadMCBorderTest : public ::testing::TestWithParam<Params> {
 public:
  virtual void SetUp() {
    block_width_ = GET_PARAM(0) + (AVM_INTERP_EXTEND - 1) + AVM_INTERP_EXTEND;
    block_height_ = GET_PARAM(1) + (AVM_INTERP_EXTEND - 1) + AVM_INTERP_EXTEND;
    int ref_area_width = (GET_PARAM(0) == 4)
                             ? GET_PARAM(0) + FOUR_TAPS_REF_LEFT_BORDER +
                                   FOUR_TAPS_REF_RIGHT_BORDER
                             : GET_PARAM(0) + EIGHT_TAPS_REF_LEFT_BORDER +
                                   EIGHT_TAPS_REF_RIGHT_BORDER;
    int ref_area_height = (GET_PARAM(1) == 4)
                              ? GET_PARAM(1) + FOUR_TAPS_REF_TOP_BORDER +
                                    FOUR_TAPS_REF_BOTTOM_BORDER
                              : GET_PARAM(1) + EIGHT_TAPS_REF_TOP_BORDER +
                                    EIGHT_TAPS_REF_BOTTOM_BORDER;
    func_ = GET_PARAM(3);
    src_stride_ = kMaxDimension;
    dst_stride_ = REF_BUFFER_WIDTH;

    const size_t max_width = REF_BUFFER_WIDTH + 1;
    const size_t max_block_size = max_width * max_width;
    src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, kMaxDimension * kMaxDimension * sizeof(uint16_t)));
    dst_ref_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, max_block_size * sizeof(uint16_t)));
    dst_test_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, max_block_size * sizeof(uint16_t)));

    avm_bit_depth_t bit_depth = static_cast<avm_bit_depth_t>(GET_PARAM(2));
    const int mask = (1 << bit_depth) - 1;
    ref_area_ = { { 0, ref_area_width, 0, ref_area_height }, { 0 }, 0 };

    ACMRandom rnd;
    rnd.Reset(ACMRandom::DeterministicSeed());
    for (int j = 0; j < (int)max_block_size; ++j) {
      src_[j] = rnd.Rand16() & mask;
    }
  }

  int BorderLeft() const { return (kMaxDimension - block_width_) / 2; }
  int BorderTop() const { return (kMaxDimension - block_height_) / 2; }

  uint16_t *input() const {
    const int offset = BorderTop() * kMaxDimension + BorderLeft();
    return src_ + offset;
  }

  virtual void TearDown() {
    avm_free(src_);
    avm_free(dst_ref_);
    avm_free(dst_test_);
  }

  void AssertOutputBufferEq(const uint16_t *p1, const uint16_t *p2, int width,
                            int height, int stride) {
    ASSERT_TRUE(p1 != p2) << "Buffers must be in different memory locations";
    for (int j = 0; j < height; ++j) {
      if (memcmp(p1, p2, sizeof(*p1) * width) == 0) {
        p1 += stride;
        p2 += stride;
        continue;
      }
      for (int i = 0; i < width; ++i) {
        ASSERT_EQ(p1[i], p2[i])
            << width << "x" << height << " Pixel mismatch at (" << i << ", "
            << j << ")";
      }
    }
  }

 protected:
  void CheckResult();
  void RunSpeedTest();

 private:
  int block_height_;
  int block_width_;
  RefinemvPadMCBorderFunc func_;
  uint16_t *src_;
  uint16_t *dst_ref_;
  uint16_t *dst_test_;
  int src_stride_;
  int dst_stride_;
  ReferenceArea ref_area_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(RefinemvPadMCBorderTest);

void RefinemvPadMCBorderTest::CheckResult() {
  uint16_t *const in = input();
  for (int y0 = -block_width_; y0 <= block_width_; y0++) {
    for (int x0 = -block_width_; x0 <= block_width_; x0++) {
      const uint16_t *const buf_ptr = in + y0 * src_stride_ + x0;
      refinemv_highbd_pad_mc_border_c(buf_ptr, src_stride_, dst_ref_,
                                      dst_stride_, x0, y0, block_width_,
                                      block_height_, &ref_area_);
      func_(buf_ptr, src_stride_, dst_test_, dst_stride_, x0, y0, block_width_,
            block_height_, &ref_area_);

      AssertOutputBufferEq(dst_ref_, dst_test_, block_width_, block_height_,
                           dst_stride_);
    }
  }
}

TEST_P(RefinemvPadMCBorderTest, CheckResult) { CheckResult(); }

void RefinemvPadMCBorderTest::RunSpeedTest() {
  int x0[3] = { 0, 1, block_width_ };
  int y0[3] = { 0, 1, block_width_ };
  uint16_t *const in = input();

  for (int k = 0; k <= 2; k++) {
    for (int l = 0; l <= 2; l++) {
      const uint16_t *const buf_ptr = in + y0[k] * src_stride_ + x0[l];
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        refinemv_highbd_pad_mc_border_c(buf_ptr, src_stride_, dst_ref_,
                                        dst_stride_, x0[l], y0[k], block_width_,
                                        block_height_, &ref_area_);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        func_(buf_ptr, src_stride_, dst_test_, dst_stride_, x0[l], y0[k],
              block_width_, block_height_, &ref_area_);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

      float c_time_per_pixel =
          (float)1000.0 * elapsed_time_c /
          (kSpeedIterations * block_width_ * block_height_);
      float opt_time_per_pixel =
          (float)1000.0 * elapsed_time_opt /
          (kSpeedIterations * block_width_ * block_height_);
      float scaling = c_time_per_pixel / opt_time_per_pixel;
      printf(
          "%3dx%-3d: c_time_per_pixel=%10.5f, "
          "opt_time_per_pixel=%10.5f,  scaling=%f, x0:%d, y0:%d \n",
          block_width_, block_height_, c_time_per_pixel, opt_time_per_pixel,
          scaling, x0[l], y0[k]);
    }
  }
}

TEST_P(RefinemvPadMCBorderTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
const Params kRefinemvPadMCBorder_avx2[] = {
  make_tuple(8, 4, 8, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 8, 8, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 16, 8, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 4, 10, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 8, 10, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 16, 10, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 4, 12, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 8, 12, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(8, 16, 12, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 4, 8, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 8, 8, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 4, 10, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 8, 10, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 4, 12, &refinemv_highbd_pad_mc_border_avx2),
  make_tuple(4, 8, 12, &refinemv_highbd_pad_mc_border_avx2)
};

INSTANTIATE_TEST_SUITE_P(AVX2, RefinemvPadMCBorderTest,
                         ::testing::ValuesIn(kRefinemvPadMCBorder_avx2));
#endif  // HAVE_AVX2

}  // namespace
