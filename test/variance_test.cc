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

#include <cstdlib>
#include <new>
#include <ostream>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "avm/avm_codec.h"
#include "avm/avm_integer.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/avm_timer.h"
#include "avm_ports/mem.h"

namespace {

typedef uint64_t (*MseWxH16bitFunc)(uint16_t *dst, int dstride, uint16_t *src,
                                    int sstride, int w, int h);
typedef unsigned int (*VarianceMxNFunc)(const uint16_t *a, int a_stride,
                                        const uint16_t *b, int b_stride,
                                        unsigned int *sse);
typedef unsigned int (*SubpixVarMxNFunc)(const uint16_t *a, int a_stride,
                                         int xoffset, int yoffset,
                                         const uint16_t *b, int b_stride,
                                         unsigned int *sse);
typedef unsigned int (*SubpixAvgVarMxNFunc)(const uint16_t *a, int a_stride,
                                            int xoffset, int yoffset,
                                            const uint16_t *b, int b_stride,
                                            uint32_t *sse,
                                            const uint16_t *second_pred);
typedef unsigned int (*Get4x4SseFunc)(const uint16_t *a, int a_stride,
                                      const uint16_t *b, int b_stride);
typedef unsigned int (*SumOfSquaresFunction)(const int16_t *src);
typedef unsigned int (*DistWtdSubpixAvgVarMxNFunc)(
    const uint16_t *a, int a_stride, int xoffset, int yoffset,
    const uint16_t *b, int b_stride, uint32_t *sse, const uint16_t *second_pred,
    const DIST_WTD_COMP_PARAMS *jcp_param);

using libavm_test::ACMRandom;

// Truncate high bit depth results by downshifting (with rounding) by:
// 2 * (bit_depth - 8) for sse
// (bit_depth - 8) for se
static void RoundHighBitDepth(int bit_depth, int64_t *se, uint64_t *sse) {
  switch (bit_depth) {
    case AVM_BITS_12:
      *sse = (*sse + 128) >> 8;
      *se = (*se + 8) >> 4;
      break;
    case AVM_BITS_10:
      *sse = (*sse + 8) >> 4;
      *se = (*se + 2) >> 2;
      break;
    case AVM_BITS_8:
    default: break;
  }
}

static unsigned int mb_ss_ref(const int16_t *src) {
  unsigned int res = 0;
  for (int i = 0; i < 256; ++i) {
    res += src[i] * src[i];
  }
  return res;
}

/* Note:
 *  Our codebase calculates the "diff" value in the variance algorithm by
 *  (src - ref).
 */
static uint32_t variance_ref(const uint16_t *src, const uint16_t *ref, int l2w,
                             int l2h, int src_stride, int ref_stride,
                             uint32_t *sse_ptr, avm_bit_depth_t bit_depth) {
  int64_t se = 0;
  uint64_t sse = 0;
  const int w = 1 << l2w;
  const int h = 1 << l2h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int diff;
      diff = src[y * src_stride + x] - ref[y * ref_stride + x];
      se += diff;
      sse += diff * diff;
    }
  }
  RoundHighBitDepth(bit_depth, &se, &sse);
  *sse_ptr = static_cast<uint32_t>(sse);
  return static_cast<uint32_t>(sse - ((se * se) >> (l2w + l2h)));
}

/* The subpel reference functions differ from the codec version in one aspect:
 * they calculate the bilinear factors directly instead of using a lookup table
 * and therefore upshift xoff and yoff by 1. Only every other calculated value
 * is used so the codec version shrinks the table to save space.
 */
static uint32_t subpel_variance_ref(const uint16_t *ref, const uint16_t *src,
                                    int l2w, int l2h, int xoff, int yoff,
                                    uint32_t *sse_ptr,
                                    avm_bit_depth_t bit_depth) {
  int64_t se = 0;
  uint64_t sse = 0;
  const int w = 1 << l2w;
  const int h = 1 << l2h;

  xoff <<= 1;
  yoff <<= 1;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // Bilinear interpolation at a 16th pel step.
      uint16_t *ref16 = (uint16_t *)ref;
      uint16_t *src16 = (uint16_t *)src;
      const int a1 = ref16[(w + 1) * (y + 0) + x + 0];
      const int a2 = ref16[(w + 1) * (y + 0) + x + 1];
      const int b1 = ref16[(w + 1) * (y + 1) + x + 0];
      const int b2 = ref16[(w + 1) * (y + 1) + x + 1];
      const int a = a1 + (((a2 - a1) * xoff + 8) >> 4);
      const int b = b1 + (((b2 - b1) * xoff + 8) >> 4);
      const int r = a + (((b - a) * yoff + 8) >> 4);
      const int diff = r - src16[w * y + x];
      se += diff;
      sse += diff * diff;
    }
  }
  RoundHighBitDepth(bit_depth, &se, &sse);
  *sse_ptr = static_cast<uint32_t>(sse);
  return static_cast<uint32_t>(sse - ((se * se) >> (l2w + l2h)));
}

static uint32_t subpel_avg_variance_ref(const uint16_t *ref,
                                        const uint16_t *src,
                                        const uint16_t *second_pred, int l2w,
                                        int l2h, int xoff, int yoff,
                                        uint32_t *sse_ptr,
                                        avm_bit_depth_t bit_depth) {
  int64_t se = 0;
  uint64_t sse = 0;
  const int w = 1 << l2w;
  const int h = 1 << l2h;

  xoff <<= 1;
  yoff <<= 1;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // bilinear interpolation at a 16th pel step
      const uint16_t *ref16 = ref;
      const uint16_t *src16 = src;
      const uint16_t *sec16 = second_pred;
      const int a1 = ref16[(w + 1) * (y + 0) + x + 0];
      const int a2 = ref16[(w + 1) * (y + 0) + x + 1];
      const int b1 = ref16[(w + 1) * (y + 1) + x + 0];
      const int b2 = ref16[(w + 1) * (y + 1) + x + 1];
      const int a = a1 + (((a2 - a1) * xoff + 8) >> 4);
      const int b = b1 + (((b2 - b1) * xoff + 8) >> 4);
      const int r = a + (((b - a) * yoff + 8) >> 4);
      const int diff = ((r + sec16[w * y + x] + 1) >> 1) - src16[w * y + x];
      se += diff;
      sse += diff * diff;
    }
  }
  RoundHighBitDepth(bit_depth, &se, &sse);
  *sse_ptr = static_cast<uint32_t>(sse);
  return static_cast<uint32_t>(sse - ((se * se) >> (l2w + l2h)));
}

////////////////////////////////////////////////////////////////////////////////

class SumOfSquaresTest : public ::testing::TestWithParam<SumOfSquaresFunction> {
 public:
  SumOfSquaresTest() : func_(GetParam()) {}

  virtual ~SumOfSquaresTest() { libavm_test::ClearSystemState(); }

 protected:
  void ConstTest();
  void RefTest();

  SumOfSquaresFunction func_;
  ACMRandom rnd_;
};

void SumOfSquaresTest::ConstTest() {
  int16_t mem[256];
  unsigned int res;
  for (int v = 0; v < 256; ++v) {
    for (int i = 0; i < 256; ++i) {
      mem[i] = v;
    }
    ASM_REGISTER_STATE_CHECK(res = func_(mem));
    EXPECT_EQ(256u * (v * v), res);
  }
}

void SumOfSquaresTest::RefTest() {
  int16_t mem[256];
  for (int i = 0; i < 100; ++i) {
    for (int j = 0; j < 256; ++j) {
      mem[j] = rnd_.Rand8() - rnd_.Rand8();
    }

    const unsigned int expected = mb_ss_ref(mem);
    unsigned int res;
    ASM_REGISTER_STATE_CHECK(res = func_(mem));
    EXPECT_EQ(expected, res);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Encapsulating struct to store the function to test along with
// some testing context.
// Can be used for MSE, SSE, Variance, etc.

template <typename Func>
struct TestParams {
  TestParams(int log2w = 0, int log2h = 0, Func function = NULL,
             int bit_depth_value = 0)
      : log2width(log2w), log2height(log2h), func(function) {
    bit_depth = static_cast<avm_bit_depth_t>(bit_depth_value);
    width = 1 << log2width;
    height = 1 << log2height;
    block_size = width * height;
    mask = (1u << bit_depth) - 1;
  }

  int log2width, log2height;
  int width, height;
  int block_size;
  Func func;
  avm_bit_depth_t bit_depth;
  uint32_t mask;
};

template <typename Func>
std::ostream &operator<<(std::ostream &os, const TestParams<Func> &p) {
  return os << "width/height:" << p.width << "/" << p.height
            << " function:" << reinterpret_cast<const void *>(p.func)
            << " bit-depth:" << p.bit_depth;
}

// Main class for testing a function type
template <typename FunctionType>
class MainTestClass
    : public ::testing::TestWithParam<TestParams<FunctionType> > {
 public:
  virtual void SetUp() {
    params_ = this->GetParam();

    rnd_.Reset(ACMRandom::DeterministicSeed());
    const size_t unit = sizeof(uint16_t);
    src_ = reinterpret_cast<uint16_t *>(avm_memalign(16, block_size() * unit));
    ref_ = new uint16_t[block_size() * unit];
    ASSERT_TRUE(src_ != NULL);
    ASSERT_TRUE(ref_ != NULL);
  }

  virtual void TearDown() {
    avm_free(src_);
    delete[] ref_;
    src_ = NULL;
    ref_ = NULL;
    libavm_test::ClearSystemState();
  }

 protected:
  // We could sub-class MainTestClass into dedicated class for Variance
  // and MSE/SSE, but it involves a lot of 'this->xxx' dereferencing
  // to access top class fields xxx. That's cumbersome, so for now we'll just
  // implement the testing methods here:

  // Variance tests
  void ZeroTest();
  void RefTest();
  void RefStrideTest();
  void OneQuarterTest();
  void AllExtremeTest();
  void SpeedTest();

  // MSE/SSE tests
  void RefTestMse();
  void RefTestSse();
  void MaxTestMse();
  void MaxTestSse();

 protected:
  ACMRandom rnd_;
  uint16_t *src_;
  uint16_t *ref_;
  TestParams<FunctionType> params_;

  // some relay helpers
  int byte_shift() const { return params_.bit_depth - 8; }
  int block_size() const { return params_.block_size; }
  int width() const { return params_.width; }
  int height() const { return params_.height; }
  uint32_t mask() const { return params_.mask; }
};

////////////////////////////////////////////////////////////////////////////////
// Tests related to variance.

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::ZeroTest() {
  for (int i = 0; i <= 255; ++i) {
    uint16_t *const src16 = src_;
    for (int k = 0; k < block_size(); ++k) src16[k] = i << byte_shift();
    for (int j = 0; j <= 255; ++j) {
      uint16_t *const ref16 = ref_;
      for (int k = 0; k < block_size(); ++k) ref16[k] = j << byte_shift();
      unsigned int sse, var;
      ASM_REGISTER_STATE_CHECK(
          var = params_.func(src_, width(), ref_, width(), &sse));
      EXPECT_EQ(0u, var) << "src values: " << i << " ref values: " << j;
    }
  }
}

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::RefTest() {
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < block_size(); j++) {
      src_[j] = rnd_.Rand16() & mask();
      ref_[j] = rnd_.Rand16() & mask();
    }
    unsigned int sse1, sse2, var1, var2;
    const int stride = width();
    ASM_REGISTER_STATE_CHECK(
        var1 = params_.func(src_, stride, ref_, stride, &sse1));
    var2 = variance_ref(src_, ref_, params_.log2width, params_.log2height,
                        stride, stride, &sse2, params_.bit_depth);
    EXPECT_EQ(sse1, sse2) << "Error at test index: " << i;
    EXPECT_EQ(var1, var2) << "Error at test index: " << i;
  }
}

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::RefStrideTest() {
  for (int i = 0; i < 10; ++i) {
    const int ref_stride = (i & 1) * width();
    const int src_stride = ((i >> 1) & 1) * width();
    for (int j = 0; j < block_size(); j++) {
      const int ref_ind = (j / width()) * ref_stride + j % width();
      const int src_ind = (j / width()) * src_stride + j % width();
      src_[src_ind] = rnd_.Rand16() & mask();
      ref_[ref_ind] = rnd_.Rand16() & mask();
    }
    unsigned int sse1, sse2;
    unsigned int var1, var2;

    ASM_REGISTER_STATE_CHECK(
        var1 = params_.func(src_, src_stride, ref_, ref_stride, &sse1));
    var2 = variance_ref(src_, ref_, params_.log2width, params_.log2height,
                        src_stride, ref_stride, &sse2, params_.bit_depth);
    EXPECT_EQ(sse1, sse2) << "Error at test index: " << i;
    EXPECT_EQ(var1, var2) << "Error at test index: " << i;
  }
}

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::OneQuarterTest() {
  const int half = block_size() / 2;
  avm_memset16(src_, 255 << byte_shift(), block_size());
  avm_memset16(ref_, 255 << byte_shift(), half);
  avm_memset16(ref_ + half, 0, half);
  unsigned int sse, var, expected;
  ASM_REGISTER_STATE_CHECK(
      var = params_.func(src_, width(), ref_, width(), &sse));
  expected = block_size() * 255l * 255 / 4;
  EXPECT_EQ(expected, var);
}

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::AllExtremeTest() {
  avm_memset16(src_, (1 << params_.bit_depth) - 1, block_size());
  avm_memset16(ref_, 0, block_size());
  unsigned int sse_ref, sse_mod, var_ref, var_mod;
  ASM_REGISTER_STATE_CHECK(
      var_mod = params_.func(src_, width(), ref_, width(), &sse_mod));
  var_ref = variance_ref(src_, ref_, params_.log2width, params_.log2height,
                         width(), width(), &sse_ref, params_.bit_depth);
  EXPECT_EQ(var_ref, var_mod);
  EXPECT_EQ(sse_ref, sse_mod);
}

template <typename VarianceFunctionType>
void MainTestClass<VarianceFunctionType>::SpeedTest() {
  for (int j = 0; j < block_size(); j++) {
    src_[j] = rnd_.Rand16() & mask();
    ref_[j] = rnd_.Rand16() & mask();
  }
  unsigned int sse;
  const int stride = width();
  int run_time = 1000000000 / block_size();
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int i = 0; i < run_time; ++i) {
    params_.func(src_, stride, ref_, stride, &sse);
  }

  avm_usec_timer_mark(&timer);
  const double elapsed_time =
      static_cast<double>(avm_usec_timer_elapsed(&timer));
  printf("Bitdepth: %d, Variance %dx%d : %7.2fns\n", params_.bit_depth, width(),
         height(), elapsed_time);
}

////////////////////////////////////////////////////////////////////////////////
// Tests related to MSE / SSE.

template <typename FunctionType>
void MainTestClass<FunctionType>::RefTestMse() {
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < block_size(); ++j) {
      src_[j] = rnd_.Rand8();
      ref_[j] = rnd_.Rand8();
    }
    unsigned int sse1, sse2;
    const int stride = width();
    ASM_REGISTER_STATE_CHECK(params_.func(src_, stride, ref_, stride, &sse1));
    variance_ref(src_, ref_, params_.log2width, params_.log2height, stride,
                 stride, &sse2, AVM_BITS_8);
    EXPECT_EQ(sse1, sse2);
  }
}

template <typename FunctionType>
void MainTestClass<FunctionType>::RefTestSse() {
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < block_size(); ++j) {
      src_[j] = rnd_.Rand8();
      ref_[j] = rnd_.Rand8();
    }
    unsigned int sse2;
    unsigned int var1;
    const int stride = width();
    ASM_REGISTER_STATE_CHECK(var1 = params_.func(src_, stride, ref_, stride));
    variance_ref(src_, ref_, params_.log2width, params_.log2height, stride,
                 stride, &sse2, AVM_BITS_8);
    EXPECT_EQ(var1, sse2);
  }
}

template <typename FunctionType>
void MainTestClass<FunctionType>::MaxTestMse() {
  memset(src_, 255, block_size());
  memset(ref_, 0, block_size());
  unsigned int sse;
  ASM_REGISTER_STATE_CHECK(params_.func(src_, width(), ref_, width(), &sse));
  const unsigned int expected = block_size() * 255 * 255;
  EXPECT_EQ(expected, sse);
}

template <typename FunctionType>
void MainTestClass<FunctionType>::MaxTestSse() {
  memset(src_, 255, block_size());
  memset(ref_, 0, block_size());
  unsigned int var;
  ASM_REGISTER_STATE_CHECK(var = params_.func(src_, width(), ref_, width()));
  const unsigned int expected = block_size() * 255 * 255;
  EXPECT_EQ(expected, var);
}

////////////////////////////////////////////////////////////////////////////////

using std::get;
using std::make_tuple;
using std::tuple;

template <typename FunctionType>
class SubpelVarianceTest
    : public ::testing::TestWithParam<TestParams<FunctionType> > {
 public:
  virtual void SetUp() {
    params_ = this->GetParam();

    rnd_.Reset(ACMRandom::DeterministicSeed());
    src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(32, block_size() * sizeof(uint16_t)));
    sec_ = reinterpret_cast<uint16_t *>(
        avm_memalign(32, block_size() * sizeof(uint16_t)));
    ref_ = reinterpret_cast<uint16_t *>(avm_memalign(
        32, (block_size() + width() + height() + 1) * sizeof(uint16_t)));
    ASSERT_TRUE(src_ != NULL);
    ASSERT_TRUE(sec_ != NULL);
    ASSERT_TRUE(ref_ != NULL);
  }

  virtual void TearDown() {
    avm_free(src_);
    avm_free(ref_);
    avm_free(sec_);
    libavm_test::ClearSystemState();
  }

 protected:
  void RefTest();
  void ExtremeRefTest();
  void SpeedTest();

  ACMRandom rnd_;
  uint16_t *src_;
  uint16_t *ref_;
  uint16_t *sec_;
  TestParams<FunctionType> params_;
  DIST_WTD_COMP_PARAMS jcp_param_;

  // some relay helpers
  int byte_shift() const { return params_.bit_depth - 8; }
  int block_size() const { return params_.block_size; }
  int width() const { return params_.width; }
  int height() const { return params_.height; }
  uint32_t mask() const { return params_.mask; }
};

template <typename SubpelVarianceFunctionType>
void SubpelVarianceTest<SubpelVarianceFunctionType>::RefTest() {
  for (int x = 0; x < 8; ++x) {
    for (int y = 0; y < 8; ++y) {
      for (int j = 0; j < block_size(); j++) {
        src_[j] = rnd_.Rand16() & mask();
      }
      for (int j = 0; j < block_size() + width() + height() + 1; j++) {
        ref_[j] = rnd_.Rand16() & mask();
      }
      unsigned int sse1, sse2;
      unsigned int var1;
      ASM_REGISTER_STATE_CHECK(
          var1 = params_.func(ref_, width() + 1, x, y, src_, width(), &sse1));
      const unsigned int var2 =
          subpel_variance_ref(ref_, src_, params_.log2width, params_.log2height,
                              x, y, &sse2, params_.bit_depth);
      EXPECT_EQ(sse1, sse2) << "at position " << x << ", " << y;
      EXPECT_EQ(var1, var2) << "at position " << x << ", " << y;
    }
  }
}

template <typename SubpelVarianceFunctionType>
void SubpelVarianceTest<SubpelVarianceFunctionType>::ExtremeRefTest() {
  // Compare against reference.
  // Src: Set the first half of values to 0, the second half to the maximum.
  // Ref: Set the first half of values to the maximum, the second half to 0.
  for (int x = 0; x < 8; ++x) {
    for (int y = 0; y < 8; ++y) {
      const int half = block_size() / 2;
      avm_memset16(src_, mask(), half);
      avm_memset16(src_ + half, 0, half);
      avm_memset16(ref_, 0, half);
      avm_memset16(ref_ + half, mask(), half + width() + height() + 1);
      unsigned int sse1, sse2;
      unsigned int var1;
      ASM_REGISTER_STATE_CHECK(
          var1 = params_.func(ref_, width() + 1, x, y, src_, width(), &sse1));
      const unsigned int var2 =
          subpel_variance_ref(ref_, src_, params_.log2width, params_.log2height,
                              x, y, &sse2, params_.bit_depth);
      EXPECT_EQ(sse1, sse2) << "for xoffset " << x << " and yoffset " << y;
      EXPECT_EQ(var1, var2) << "for xoffset " << x << " and yoffset " << y;
    }
  }
}

template <typename SubpelVarianceFunctionType>
void SubpelVarianceTest<SubpelVarianceFunctionType>::SpeedTest() {
  for (int j = 0; j < block_size(); j++) {
    src_[j] = rnd_.Rand16() & mask();
  }
  for (int j = 0; j < block_size() + width() + height() + 1; j++) {
    ref_[j] = rnd_.Rand16() & mask();
  }

  unsigned int sse1, sse2;
  int run_time = 1000000000 / block_size();
  avm_usec_timer timer;

  avm_usec_timer_start(&timer);
  for (int i = 0; i < run_time; ++i) {
    int x = rnd_(8);
    int y = rnd_(8);
    params_.func(ref_, width() + 1, x, y, src_, width(), &sse1);
  }
  avm_usec_timer_mark(&timer);

  const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));

  avm_usec_timer timer_c;

  avm_usec_timer_start(&timer_c);
  for (int i = 0; i < run_time; ++i) {
    int x = rnd_(8);
    int y = rnd_(8);
    subpel_variance_ref(ref_, src_, params_.log2width, params_.log2height, x, y,
                        &sse2, params_.bit_depth);
  }
  avm_usec_timer_mark(&timer_c);

  const int elapsed_time_c = static_cast<int>(avm_usec_timer_elapsed(&timer_c));

  printf(
      "sub_pixel_variance_%dx%d_%d: ref_time=%d us opt_time=%d us gain=%d \n",
      width(), height(), params_.bit_depth, elapsed_time_c, elapsed_time,
      elapsed_time_c / elapsed_time);
}

template <>
void SubpelVarianceTest<SubpixAvgVarMxNFunc>::RefTest() {
  for (int x = 0; x < 8; ++x) {
    for (int y = 0; y < 8; ++y) {
      for (int j = 0; j < block_size(); j++) {
        src_[j] = rnd_.Rand16() & mask();
        sec_[j] = rnd_.Rand16() & mask();
      }
      for (int j = 0; j < block_size() + width() + height() + 1; j++) {
        ref_[j] = rnd_.Rand16() & mask();
      }
      uint32_t sse1, sse2;
      uint32_t var1, var2;
      ASM_REGISTER_STATE_CHECK(var1 = params_.func(ref_, width() + 1, x, y,
                                                   src_, width(), &sse1, sec_));
      var2 = subpel_avg_variance_ref(ref_, src_, sec_, params_.log2width,
                                     params_.log2height, x, y, &sse2,
                                     params_.bit_depth);
      EXPECT_EQ(sse1, sse2) << "at position " << x << ", " << y;
      EXPECT_EQ(var1, var2) << "at position " << x << ", " << y;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

typedef SubpelVarianceTest<SubpixVarMxNFunc> AvxSubpelVarianceTest;
typedef SubpelVarianceTest<SubpixAvgVarMxNFunc> AvxSubpelAvgVarianceTest;

TEST_P(SumOfSquaresTest, Const) { ConstTest(); }
TEST_P(SumOfSquaresTest, Ref) { RefTest(); }
TEST_P(AvxSubpelVarianceTest, Ref) { RefTest(); }
TEST_P(AvxSubpelVarianceTest, ExtremeRef) { ExtremeRefTest(); }
TEST_P(AvxSubpelVarianceTest, DISABLED_Speed) { SpeedTest(); }
TEST_P(AvxSubpelAvgVarianceTest, Ref) { RefTest(); }

INSTANTIATE_TEST_SUITE_P(C, SumOfSquaresTest,
                         ::testing::Values(avm_get_mb_ss_c));

typedef TestParams<VarianceMxNFunc> VarianceParams;
typedef TestParams<SubpixVarMxNFunc> SubpelVarianceParams;
typedef TestParams<SubpixAvgVarMxNFunc> SubpelAvgVarianceParams;
typedef TestParams<DistWtdSubpixAvgVarMxNFunc> DistWtdSubpelAvgVarianceParams;

typedef uint64_t (*MseHBDWxH16bitFunc)(uint16_t *dst, int dstride,
                                       uint16_t *src, int sstride, int w,
                                       int h);

template <typename FunctionType>
class MseHBDWxHTestClass
    : public ::testing::TestWithParam<TestParams<FunctionType> > {
 public:
  virtual void SetUp() {
    params_ = this->GetParam();

    rnd_.Reset(ACMRandom::DeterministicSeed());
    src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, block_size() * sizeof(src_)));
    dst_ = reinterpret_cast<uint16_t *>(
        avm_memalign(16, block_size() * sizeof(dst_)));
    ASSERT_TRUE(src_ != NULL);
    ASSERT_TRUE(dst_ != NULL);
  }

  virtual void TearDown() {
    avm_free(src_);
    avm_free(dst_);
    src_ = NULL;
    dst_ = NULL;
    libavm_test::ClearSystemState();
  }

 protected:
  void RefMatchTestMse();
  void SpeedTest();

 protected:
  ACMRandom rnd_;
  uint16_t *dst_;
  uint16_t *src_;
  TestParams<FunctionType> params_;

  // some relay helpers
  int block_size() const { return params_.block_size; }
  int width() const { return params_.width; }
  int d_stride() const { return params_.width; }  // stride is same as width
  int s_stride() const { return params_.width; }  // stride is same as width
  int height() const { return params_.height; }
  int mask() const { return params_.mask; }
};

template <typename MseHBDWxHFunctionType>
void MseHBDWxHTestClass<MseHBDWxHFunctionType>::SpeedTest() {
  avm_usec_timer ref_timer, test_timer;
  double elapsed_time_c = 0;
  double elapsed_time_simd = 0;
  int run_time = 10000000;
  int w = width();
  int h = height();
  int dstride = d_stride();
  int sstride = s_stride();
  for (int k = 0; k < block_size(); ++k) {
    dst_[k] = rnd_.Rand16() & mask();
    src_[k] = rnd_.Rand16() & mask();
  }
  avm_usec_timer_start(&ref_timer);
  for (int i = 0; i < run_time; i++) {
    avm_mse_wxh_16bit_highbd_c(dst_, dstride, src_, sstride, w, h);
  }
  avm_usec_timer_mark(&ref_timer);
  elapsed_time_c = static_cast<double>(avm_usec_timer_elapsed(&ref_timer));

  avm_usec_timer_start(&test_timer);
  for (int i = 0; i < run_time; i++) {
    params_.func(dst_, dstride, src_, sstride, w, h);
  }
  avm_usec_timer_mark(&test_timer);
  elapsed_time_simd = static_cast<double>(avm_usec_timer_elapsed(&test_timer));

  printf("%dx%d\tc_time=%lf \t simd_time=%lf \t gain=%lf\n", width(), height(),
         elapsed_time_c, elapsed_time_simd,
         (elapsed_time_c / elapsed_time_simd));
}

template <typename MseHBDWxHFunctionType>
void MseHBDWxHTestClass<MseHBDWxHFunctionType>::RefMatchTestMse() {
  uint64_t mse_ref = 0;
  uint64_t mse_mod = 0;
  int w = width();
  int h = height();
  int dstride = d_stride();
  int sstride = s_stride();
  for (int i = 0; i < 10; i++) {
    for (int k = 0; k < block_size(); ++k) {
      dst_[k] = rnd_.Rand16() & mask();
      src_[k] = rnd_.Rand16() & mask();
    }
    ASM_REGISTER_STATE_CHECK(mse_ref = avm_mse_wxh_16bit_highbd_c(
                                 dst_, dstride, src_, sstride, w, h));
    ASM_REGISTER_STATE_CHECK(
        mse_mod = params_.func(dst_, dstride, src_, sstride, w, h));
    EXPECT_EQ(mse_ref, mse_mod)
        << "ref mse: " << mse_ref << " mod mse: " << mse_mod;
  }
}

typedef TestParams<MseHBDWxH16bitFunc> MseHBDWxHParams;
typedef MseHBDWxHTestClass<MseHBDWxH16bitFunc> MseHBDWxHTest;
typedef MainTestClass<VarianceMxNFunc> AvxHBDMseTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AvxHBDMseTest);
typedef MainTestClass<VarianceMxNFunc> AvxHBDVarianceTest;
typedef SubpelVarianceTest<SubpixVarMxNFunc> AvxHBDSubpelVarianceTest;
typedef SubpelVarianceTest<SubpixAvgVarMxNFunc> AvxHBDSubpelAvgVarianceTest;

TEST_P(MseHBDWxHTest, RefMse) { RefMatchTestMse(); }
TEST_P(MseHBDWxHTest, DISABLED_SpeedMse) { SpeedTest(); }
TEST_P(AvxHBDMseTest, RefMse) { RefTestMse(); }
TEST_P(AvxHBDMseTest, MaxMse) { MaxTestMse(); }
TEST_P(AvxHBDVarianceTest, Zero) { ZeroTest(); }
TEST_P(AvxHBDVarianceTest, Ref) { RefTest(); }
TEST_P(AvxHBDVarianceTest, RefStride) { RefStrideTest(); }
TEST_P(AvxHBDVarianceTest, OneQuarter) { OneQuarterTest(); }
TEST_P(AvxHBDVarianceTest, AllExtreme) { AllExtremeTest(); }
TEST_P(AvxHBDVarianceTest, DISABLED_Speed) { SpeedTest(); }
TEST_P(AvxHBDSubpelVarianceTest, Ref) { RefTest(); }
TEST_P(AvxHBDSubpelVarianceTest, ExtremeRef) { ExtremeRefTest(); }
TEST_P(AvxHBDSubpelVarianceTest, DISABLED_Speed) { SpeedTest(); }
TEST_P(AvxHBDSubpelAvgVarianceTest, Ref) { RefTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, MseHBDWxHTest,
    ::testing::Values(MseHBDWxHParams(3, 3, &avm_mse_wxh_16bit_highbd_c, 10),
                      MseHBDWxHParams(3, 2, &avm_mse_wxh_16bit_highbd_c, 10),
                      MseHBDWxHParams(2, 3, &avm_mse_wxh_16bit_highbd_c, 10),
                      MseHBDWxHParams(2, 2, &avm_mse_wxh_16bit_highbd_c, 10)));

/* TODO(debargha): This test does not support the highbd version
INSTANTIATE_TEST_SUITE_P(
    C, AvxHBDMseTest,
    ::testing::Values(make_tuple(4, 4, &avm_highbd_12_mse16x16_c),
                      make_tuple(4, 4, &avm_highbd_12_mse16x8_c),
                      make_tuple(4, 4, &avm_highbd_12_mse8x16_c),
                      make_tuple(4, 4, &avm_highbd_12_mse8x8_c),
                      make_tuple(4, 4, &avm_highbd_10_mse16x16_c),
                      make_tuple(4, 4, &avm_highbd_10_mse16x8_c),
                      make_tuple(4, 4, &avm_highbd_10_mse8x16_c),
                      make_tuple(4, 4, &avm_highbd_10_mse8x8_c),
                      make_tuple(4, 4, &avm_highbd_8_mse16x16_c),
                      make_tuple(4, 4, &avm_highbd_8_mse16x8_c),
                      make_tuple(4, 4, &avm_highbd_8_mse8x16_c),
                      make_tuple(4, 4, &avm_highbd_8_mse8x8_c)));
*/

const VarianceParams kArrayHBDVariance_c[] = {
  VarianceParams(7, 7, &avm_highbd_12_variance128x128_c, 12),
  VarianceParams(7, 6, &avm_highbd_12_variance128x64_c, 12),
  VarianceParams(6, 7, &avm_highbd_12_variance64x128_c, 12),
  VarianceParams(6, 6, &avm_highbd_12_variance64x64_c, 12),
  VarianceParams(6, 5, &avm_highbd_12_variance64x32_c, 12),
  VarianceParams(5, 6, &avm_highbd_12_variance32x64_c, 12),
  VarianceParams(5, 5, &avm_highbd_12_variance32x32_c, 12),
  VarianceParams(5, 4, &avm_highbd_12_variance32x16_c, 12),
  VarianceParams(4, 5, &avm_highbd_12_variance16x32_c, 12),
  VarianceParams(4, 4, &avm_highbd_12_variance16x16_c, 12),
  VarianceParams(4, 3, &avm_highbd_12_variance16x8_c, 12),
  VarianceParams(3, 4, &avm_highbd_12_variance8x16_c, 12),
  VarianceParams(3, 3, &avm_highbd_12_variance8x8_c, 12),
  VarianceParams(3, 2, &avm_highbd_12_variance8x4_c, 12),
  VarianceParams(2, 3, &avm_highbd_12_variance4x8_c, 12),
  VarianceParams(2, 2, &avm_highbd_12_variance4x4_c, 12),
  VarianceParams(7, 7, &avm_highbd_10_variance128x128_c, 10),
  VarianceParams(7, 6, &avm_highbd_10_variance128x64_c, 10),
  VarianceParams(6, 7, &avm_highbd_10_variance64x128_c, 10),
  VarianceParams(6, 6, &avm_highbd_10_variance64x64_c, 10),
  VarianceParams(6, 5, &avm_highbd_10_variance64x32_c, 10),
  VarianceParams(5, 6, &avm_highbd_10_variance32x64_c, 10),
  VarianceParams(5, 5, &avm_highbd_10_variance32x32_c, 10),
  VarianceParams(5, 4, &avm_highbd_10_variance32x16_c, 10),
  VarianceParams(4, 5, &avm_highbd_10_variance16x32_c, 10),
  VarianceParams(4, 4, &avm_highbd_10_variance16x16_c, 10),
  VarianceParams(4, 3, &avm_highbd_10_variance16x8_c, 10),
  VarianceParams(3, 4, &avm_highbd_10_variance8x16_c, 10),
  VarianceParams(3, 3, &avm_highbd_10_variance8x8_c, 10),
  VarianceParams(3, 2, &avm_highbd_10_variance8x4_c, 10),
  VarianceParams(2, 3, &avm_highbd_10_variance4x8_c, 10),
  VarianceParams(2, 2, &avm_highbd_10_variance4x4_c, 10),
  VarianceParams(7, 7, &avm_highbd_8_variance128x128_c, 8),
  VarianceParams(7, 6, &avm_highbd_8_variance128x64_c, 8),
  VarianceParams(6, 7, &avm_highbd_8_variance64x128_c, 8),
  VarianceParams(6, 6, &avm_highbd_8_variance64x64_c, 8),
  VarianceParams(6, 5, &avm_highbd_8_variance64x32_c, 8),
  VarianceParams(5, 6, &avm_highbd_8_variance32x64_c, 8),
  VarianceParams(5, 5, &avm_highbd_8_variance32x32_c, 8),
  VarianceParams(5, 4, &avm_highbd_8_variance32x16_c, 8),
  VarianceParams(4, 5, &avm_highbd_8_variance16x32_c, 8),
  VarianceParams(4, 4, &avm_highbd_8_variance16x16_c, 8),
  VarianceParams(4, 3, &avm_highbd_8_variance16x8_c, 8),
  VarianceParams(3, 4, &avm_highbd_8_variance8x16_c, 8),
  VarianceParams(3, 3, &avm_highbd_8_variance8x8_c, 8),
  VarianceParams(3, 2, &avm_highbd_8_variance8x4_c, 8),
  VarianceParams(2, 3, &avm_highbd_8_variance4x8_c, 8),
  VarianceParams(2, 2, &avm_highbd_8_variance4x4_c, 8),

  VarianceParams(6, 4, &avm_highbd_12_variance64x16_c, 12),
  VarianceParams(4, 6, &avm_highbd_12_variance16x64_c, 12),
  VarianceParams(5, 3, &avm_highbd_12_variance32x8_c, 12),
  VarianceParams(3, 5, &avm_highbd_12_variance8x32_c, 12),
  VarianceParams(4, 2, &avm_highbd_12_variance16x4_c, 12),
  VarianceParams(2, 4, &avm_highbd_12_variance4x16_c, 12),
  VarianceParams(6, 4, &avm_highbd_10_variance64x16_c, 10),
  VarianceParams(4, 6, &avm_highbd_10_variance16x64_c, 10),
  VarianceParams(5, 3, &avm_highbd_10_variance32x8_c, 10),
  VarianceParams(3, 5, &avm_highbd_10_variance8x32_c, 10),
  VarianceParams(4, 2, &avm_highbd_10_variance16x4_c, 10),
  VarianceParams(2, 4, &avm_highbd_10_variance4x16_c, 10),
  VarianceParams(6, 4, &avm_highbd_8_variance64x16_c, 8),
  VarianceParams(4, 6, &avm_highbd_8_variance16x64_c, 8),
  VarianceParams(5, 3, &avm_highbd_8_variance32x8_c, 8),
  VarianceParams(3, 5, &avm_highbd_8_variance8x32_c, 8),
  VarianceParams(4, 2, &avm_highbd_8_variance16x4_c, 8),
  VarianceParams(2, 4, &avm_highbd_8_variance4x16_c, 8),
  VarianceParams(6, 3, &avm_highbd_12_variance64x8_c, 12),
  VarianceParams(3, 6, &avm_highbd_12_variance8x64_c, 12),
  VarianceParams(5, 2, &avm_highbd_12_variance32x4_c, 12),
  VarianceParams(2, 5, &avm_highbd_12_variance4x32_c, 12),
  VarianceParams(6, 2, &avm_highbd_12_variance64x4_c, 12),
  VarianceParams(2, 6, &avm_highbd_12_variance4x64_c, 12),
  VarianceParams(6, 3, &avm_highbd_10_variance64x8_c, 10),
  VarianceParams(3, 6, &avm_highbd_10_variance8x64_c, 10),
  VarianceParams(5, 2, &avm_highbd_10_variance32x4_c, 10),
  VarianceParams(2, 5, &avm_highbd_10_variance4x32_c, 10),
  VarianceParams(6, 2, &avm_highbd_10_variance64x4_c, 10),
  VarianceParams(2, 6, &avm_highbd_10_variance4x64_c, 10),
  VarianceParams(6, 3, &avm_highbd_8_variance64x8_c, 8),
  VarianceParams(3, 6, &avm_highbd_8_variance8x64_c, 8),
  VarianceParams(5, 2, &avm_highbd_8_variance32x4_c, 8),
  VarianceParams(2, 5, &avm_highbd_8_variance4x32_c, 8),
  VarianceParams(6, 2, &avm_highbd_8_variance64x4_c, 8),
  VarianceParams(2, 6, &avm_highbd_8_variance4x64_c, 8),
};
INSTANTIATE_TEST_SUITE_P(C, AvxHBDVarianceTest,
                         ::testing::ValuesIn(kArrayHBDVariance_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AvxHBDVarianceTest,
    ::testing::Values(
        VarianceParams(2, 2, &avm_highbd_8_variance4x4_sse4_1, 8),
        VarianceParams(2, 2, &avm_highbd_10_variance4x4_sse4_1, 10),
        VarianceParams(2, 2, &avm_highbd_12_variance4x4_sse4_1, 12)));
#endif  // HAVE_SSE4_1

const SubpelVarianceParams kArrayHBDSubpelVariance_c[] = {
  SubpelVarianceParams(7, 7, &avm_highbd_8_sub_pixel_variance128x128_c, 8),
  SubpelVarianceParams(7, 6, &avm_highbd_8_sub_pixel_variance128x64_c, 8),
  SubpelVarianceParams(6, 7, &avm_highbd_8_sub_pixel_variance64x128_c, 8),
  SubpelVarianceParams(6, 6, &avm_highbd_8_sub_pixel_variance64x64_c, 8),
  SubpelVarianceParams(6, 5, &avm_highbd_8_sub_pixel_variance64x32_c, 8),
  SubpelVarianceParams(5, 6, &avm_highbd_8_sub_pixel_variance32x64_c, 8),
  SubpelVarianceParams(5, 5, &avm_highbd_8_sub_pixel_variance32x32_c, 8),
  SubpelVarianceParams(5, 4, &avm_highbd_8_sub_pixel_variance32x16_c, 8),
  SubpelVarianceParams(4, 5, &avm_highbd_8_sub_pixel_variance16x32_c, 8),
  SubpelVarianceParams(4, 4, &avm_highbd_8_sub_pixel_variance16x16_c, 8),
  SubpelVarianceParams(4, 3, &avm_highbd_8_sub_pixel_variance16x8_c, 8),
  SubpelVarianceParams(3, 4, &avm_highbd_8_sub_pixel_variance8x16_c, 8),
  SubpelVarianceParams(3, 3, &avm_highbd_8_sub_pixel_variance8x8_c, 8),
  SubpelVarianceParams(3, 2, &avm_highbd_8_sub_pixel_variance8x4_c, 8),
  SubpelVarianceParams(2, 3, &avm_highbd_8_sub_pixel_variance4x8_c, 8),
  SubpelVarianceParams(2, 2, &avm_highbd_8_sub_pixel_variance4x4_c, 8),
  SubpelVarianceParams(7, 7, &avm_highbd_10_sub_pixel_variance128x128_c, 10),
  SubpelVarianceParams(7, 6, &avm_highbd_10_sub_pixel_variance128x64_c, 10),
  SubpelVarianceParams(6, 7, &avm_highbd_10_sub_pixel_variance64x128_c, 10),
  SubpelVarianceParams(6, 6, &avm_highbd_10_sub_pixel_variance64x64_c, 10),
  SubpelVarianceParams(6, 5, &avm_highbd_10_sub_pixel_variance64x32_c, 10),
  SubpelVarianceParams(5, 6, &avm_highbd_10_sub_pixel_variance32x64_c, 10),
  SubpelVarianceParams(5, 5, &avm_highbd_10_sub_pixel_variance32x32_c, 10),
  SubpelVarianceParams(5, 4, &avm_highbd_10_sub_pixel_variance32x16_c, 10),
  SubpelVarianceParams(4, 5, &avm_highbd_10_sub_pixel_variance16x32_c, 10),
  SubpelVarianceParams(4, 4, &avm_highbd_10_sub_pixel_variance16x16_c, 10),
  SubpelVarianceParams(4, 3, &avm_highbd_10_sub_pixel_variance16x8_c, 10),
  SubpelVarianceParams(3, 4, &avm_highbd_10_sub_pixel_variance8x16_c, 10),
  SubpelVarianceParams(3, 3, &avm_highbd_10_sub_pixel_variance8x8_c, 10),
  SubpelVarianceParams(3, 2, &avm_highbd_10_sub_pixel_variance8x4_c, 10),
  SubpelVarianceParams(2, 3, &avm_highbd_10_sub_pixel_variance4x8_c, 10),
  SubpelVarianceParams(2, 2, &avm_highbd_10_sub_pixel_variance4x4_c, 10),
  SubpelVarianceParams(7, 7, &avm_highbd_12_sub_pixel_variance128x128_c, 12),
  SubpelVarianceParams(7, 6, &avm_highbd_12_sub_pixel_variance128x64_c, 12),
  SubpelVarianceParams(6, 7, &avm_highbd_12_sub_pixel_variance64x128_c, 12),
  SubpelVarianceParams(6, 6, &avm_highbd_12_sub_pixel_variance64x64_c, 12),
  SubpelVarianceParams(6, 5, &avm_highbd_12_sub_pixel_variance64x32_c, 12),
  SubpelVarianceParams(5, 6, &avm_highbd_12_sub_pixel_variance32x64_c, 12),
  SubpelVarianceParams(5, 5, &avm_highbd_12_sub_pixel_variance32x32_c, 12),
  SubpelVarianceParams(5, 4, &avm_highbd_12_sub_pixel_variance32x16_c, 12),
  SubpelVarianceParams(4, 5, &avm_highbd_12_sub_pixel_variance16x32_c, 12),
  SubpelVarianceParams(4, 4, &avm_highbd_12_sub_pixel_variance16x16_c, 12),
  SubpelVarianceParams(4, 3, &avm_highbd_12_sub_pixel_variance16x8_c, 12),
  SubpelVarianceParams(3, 4, &avm_highbd_12_sub_pixel_variance8x16_c, 12),
  SubpelVarianceParams(3, 3, &avm_highbd_12_sub_pixel_variance8x8_c, 12),
  SubpelVarianceParams(3, 2, &avm_highbd_12_sub_pixel_variance8x4_c, 12),
  SubpelVarianceParams(2, 3, &avm_highbd_12_sub_pixel_variance4x8_c, 12),
  SubpelVarianceParams(2, 2, &avm_highbd_12_sub_pixel_variance4x4_c, 12),

  SubpelVarianceParams(6, 4, &avm_highbd_8_sub_pixel_variance64x16_c, 8),
  SubpelVarianceParams(4, 6, &avm_highbd_8_sub_pixel_variance16x64_c, 8),
  SubpelVarianceParams(5, 3, &avm_highbd_8_sub_pixel_variance32x8_c, 8),
  SubpelVarianceParams(3, 5, &avm_highbd_8_sub_pixel_variance8x32_c, 8),
  SubpelVarianceParams(4, 2, &avm_highbd_8_sub_pixel_variance16x4_c, 8),
  SubpelVarianceParams(2, 4, &avm_highbd_8_sub_pixel_variance4x16_c, 8),
  SubpelVarianceParams(6, 4, &avm_highbd_10_sub_pixel_variance64x16_c, 10),
  SubpelVarianceParams(4, 6, &avm_highbd_10_sub_pixel_variance16x64_c, 10),
  SubpelVarianceParams(5, 3, &avm_highbd_10_sub_pixel_variance32x8_c, 10),
  SubpelVarianceParams(3, 5, &avm_highbd_10_sub_pixel_variance8x32_c, 10),
  SubpelVarianceParams(4, 2, &avm_highbd_10_sub_pixel_variance16x4_c, 10),
  SubpelVarianceParams(2, 4, &avm_highbd_10_sub_pixel_variance4x16_c, 10),
  SubpelVarianceParams(6, 4, &avm_highbd_12_sub_pixel_variance64x16_c, 12),
  SubpelVarianceParams(4, 6, &avm_highbd_12_sub_pixel_variance16x64_c, 12),
  SubpelVarianceParams(5, 3, &avm_highbd_12_sub_pixel_variance32x8_c, 12),
  SubpelVarianceParams(3, 5, &avm_highbd_12_sub_pixel_variance8x32_c, 12),
  SubpelVarianceParams(4, 2, &avm_highbd_12_sub_pixel_variance16x4_c, 12),
  SubpelVarianceParams(2, 4, &avm_highbd_12_sub_pixel_variance4x16_c, 12),
  SubpelVarianceParams(6, 3, &avm_highbd_8_sub_pixel_variance64x8_c, 8),
  SubpelVarianceParams(3, 6, &avm_highbd_8_sub_pixel_variance8x64_c, 8),
  SubpelVarianceParams(5, 2, &avm_highbd_8_sub_pixel_variance32x4_c, 8),
  SubpelVarianceParams(2, 5, &avm_highbd_8_sub_pixel_variance4x32_c, 8),
  SubpelVarianceParams(6, 2, &avm_highbd_8_sub_pixel_variance64x4_c, 8),
  SubpelVarianceParams(2, 6, &avm_highbd_8_sub_pixel_variance4x64_c, 8),
  SubpelVarianceParams(6, 3, &avm_highbd_10_sub_pixel_variance64x8_c, 10),
  SubpelVarianceParams(3, 6, &avm_highbd_10_sub_pixel_variance8x64_c, 10),
  SubpelVarianceParams(5, 2, &avm_highbd_10_sub_pixel_variance32x4_c, 10),
  SubpelVarianceParams(2, 5, &avm_highbd_10_sub_pixel_variance4x32_c, 10),
  SubpelVarianceParams(6, 2, &avm_highbd_10_sub_pixel_variance64x4_c, 10),
  SubpelVarianceParams(2, 6, &avm_highbd_10_sub_pixel_variance4x64_c, 10),
  SubpelVarianceParams(6, 3, &avm_highbd_12_sub_pixel_variance64x8_c, 12),
  SubpelVarianceParams(3, 6, &avm_highbd_12_sub_pixel_variance8x64_c, 12),
  SubpelVarianceParams(5, 2, &avm_highbd_12_sub_pixel_variance32x4_c, 12),
  SubpelVarianceParams(2, 5, &avm_highbd_12_sub_pixel_variance4x32_c, 12),
  SubpelVarianceParams(6, 2, &avm_highbd_12_sub_pixel_variance64x4_c, 12),
  SubpelVarianceParams(2, 6, &avm_highbd_12_sub_pixel_variance4x64_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, AvxHBDSubpelVarianceTest,
                         ::testing::ValuesIn(kArrayHBDSubpelVariance_c));

const SubpelAvgVarianceParams kArrayHBDSubpelAvgVariance_c[] = {
  SubpelAvgVarianceParams(7, 7, &avm_highbd_8_sub_pixel_avg_variance128x128_c,
                          8),
  SubpelAvgVarianceParams(7, 6, &avm_highbd_8_sub_pixel_avg_variance128x64_c,
                          8),
  SubpelAvgVarianceParams(6, 7, &avm_highbd_8_sub_pixel_avg_variance64x128_c,
                          8),
  SubpelAvgVarianceParams(6, 6, &avm_highbd_8_sub_pixel_avg_variance64x64_c, 8),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_8_sub_pixel_avg_variance64x32_c, 8),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_8_sub_pixel_avg_variance32x64_c, 8),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_8_sub_pixel_avg_variance32x32_c, 8),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_8_sub_pixel_avg_variance32x16_c, 8),
  SubpelAvgVarianceParams(4, 5, &avm_highbd_8_sub_pixel_avg_variance16x32_c, 8),
  SubpelAvgVarianceParams(4, 4, &avm_highbd_8_sub_pixel_avg_variance16x16_c, 8),
  SubpelAvgVarianceParams(4, 3, &avm_highbd_8_sub_pixel_avg_variance16x8_c, 8),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_8_sub_pixel_avg_variance8x16_c, 8),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_8_sub_pixel_avg_variance8x8_c, 8),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_8_sub_pixel_avg_variance8x4_c, 8),
  SubpelAvgVarianceParams(2, 3, &avm_highbd_8_sub_pixel_avg_variance4x8_c, 8),
  SubpelAvgVarianceParams(2, 2, &avm_highbd_8_sub_pixel_avg_variance4x4_c, 8),
  SubpelAvgVarianceParams(7, 7, &avm_highbd_10_sub_pixel_avg_variance128x128_c,
                          10),
  SubpelAvgVarianceParams(7, 6, &avm_highbd_10_sub_pixel_avg_variance128x64_c,
                          10),
  SubpelAvgVarianceParams(6, 7, &avm_highbd_10_sub_pixel_avg_variance64x128_c,
                          10),
  SubpelAvgVarianceParams(6, 6, &avm_highbd_10_sub_pixel_avg_variance64x64_c,
                          10),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_10_sub_pixel_avg_variance64x32_c,
                          10),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_10_sub_pixel_avg_variance32x64_c,
                          10),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_10_sub_pixel_avg_variance32x32_c,
                          10),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_10_sub_pixel_avg_variance32x16_c,
                          10),
  SubpelAvgVarianceParams(4, 5, &avm_highbd_10_sub_pixel_avg_variance16x32_c,
                          10),
  SubpelAvgVarianceParams(4, 4, &avm_highbd_10_sub_pixel_avg_variance16x16_c,
                          10),
  SubpelAvgVarianceParams(4, 3, &avm_highbd_10_sub_pixel_avg_variance16x8_c,
                          10),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_10_sub_pixel_avg_variance8x16_c,
                          10),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_10_sub_pixel_avg_variance8x8_c, 10),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_10_sub_pixel_avg_variance8x4_c, 10),
  SubpelAvgVarianceParams(2, 3, &avm_highbd_10_sub_pixel_avg_variance4x8_c, 10),
  SubpelAvgVarianceParams(2, 2, &avm_highbd_10_sub_pixel_avg_variance4x4_c, 10),
  SubpelAvgVarianceParams(7, 7, &avm_highbd_12_sub_pixel_avg_variance128x128_c,
                          12),
  SubpelAvgVarianceParams(7, 6, &avm_highbd_12_sub_pixel_avg_variance128x64_c,
                          12),
  SubpelAvgVarianceParams(6, 7, &avm_highbd_12_sub_pixel_avg_variance64x128_c,
                          12),
  SubpelAvgVarianceParams(6, 6, &avm_highbd_12_sub_pixel_avg_variance64x64_c,
                          12),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_12_sub_pixel_avg_variance64x32_c,
                          12),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_12_sub_pixel_avg_variance32x64_c,
                          12),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_12_sub_pixel_avg_variance32x32_c,
                          12),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_12_sub_pixel_avg_variance32x16_c,
                          12),
  SubpelAvgVarianceParams(4, 5, &avm_highbd_12_sub_pixel_avg_variance16x32_c,
                          12),
  SubpelAvgVarianceParams(4, 4, &avm_highbd_12_sub_pixel_avg_variance16x16_c,
                          12),
  SubpelAvgVarianceParams(4, 3, &avm_highbd_12_sub_pixel_avg_variance16x8_c,
                          12),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_12_sub_pixel_avg_variance8x16_c,
                          12),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_12_sub_pixel_avg_variance8x8_c, 12),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_12_sub_pixel_avg_variance8x4_c, 12),
  SubpelAvgVarianceParams(2, 3, &avm_highbd_12_sub_pixel_avg_variance4x8_c, 12),
  SubpelAvgVarianceParams(2, 2, &avm_highbd_12_sub_pixel_avg_variance4x4_c, 12),

  SubpelAvgVarianceParams(6, 4, &avm_highbd_8_sub_pixel_avg_variance64x16_c, 8),
  SubpelAvgVarianceParams(4, 6, &avm_highbd_8_sub_pixel_avg_variance16x64_c, 8),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_8_sub_pixel_avg_variance32x8_c, 8),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_8_sub_pixel_avg_variance8x32_c, 8),
  SubpelAvgVarianceParams(4, 2, &avm_highbd_8_sub_pixel_avg_variance16x4_c, 8),
  SubpelAvgVarianceParams(2, 4, &avm_highbd_8_sub_pixel_avg_variance4x16_c, 8),
  SubpelAvgVarianceParams(6, 4, &avm_highbd_10_sub_pixel_avg_variance64x16_c,
                          10),
  SubpelAvgVarianceParams(4, 6, &avm_highbd_10_sub_pixel_avg_variance16x64_c,
                          10),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_10_sub_pixel_avg_variance32x8_c,
                          10),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_10_sub_pixel_avg_variance8x32_c,
                          10),
  SubpelAvgVarianceParams(4, 2, &avm_highbd_10_sub_pixel_avg_variance16x4_c,
                          10),
  SubpelAvgVarianceParams(2, 4, &avm_highbd_10_sub_pixel_avg_variance4x16_c,
                          10),
  SubpelAvgVarianceParams(6, 4, &avm_highbd_12_sub_pixel_avg_variance64x16_c,
                          12),
  SubpelAvgVarianceParams(4, 6, &avm_highbd_12_sub_pixel_avg_variance16x64_c,
                          12),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_12_sub_pixel_avg_variance32x8_c,
                          12),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_12_sub_pixel_avg_variance8x32_c,
                          12),
  SubpelAvgVarianceParams(4, 2, &avm_highbd_12_sub_pixel_avg_variance16x4_c,
                          12),
  SubpelAvgVarianceParams(2, 4, &avm_highbd_12_sub_pixel_avg_variance4x16_c,
                          12),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_8_sub_pixel_avg_variance64x8_c, 8),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_8_sub_pixel_avg_variance8x64_c, 8),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_8_sub_pixel_avg_variance32x4_c, 8),
  SubpelAvgVarianceParams(2, 5, &avm_highbd_8_sub_pixel_avg_variance4x32_c, 8),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_8_sub_pixel_avg_variance64x4_c, 8),
  SubpelAvgVarianceParams(2, 6, &avm_highbd_8_sub_pixel_avg_variance4x64_c, 8),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_10_sub_pixel_avg_variance64x8_c,
                          10),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_10_sub_pixel_avg_variance8x64_c,
                          10),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_10_sub_pixel_avg_variance32x4_c,
                          10),
  SubpelAvgVarianceParams(2, 5, &avm_highbd_10_sub_pixel_avg_variance4x32_c,
                          10),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_10_sub_pixel_avg_variance64x4_c,
                          10),
  SubpelAvgVarianceParams(2, 6, &avm_highbd_10_sub_pixel_avg_variance4x64_c,
                          10),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_12_sub_pixel_avg_variance64x8_c,
                          12),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_12_sub_pixel_avg_variance8x64_c,
                          12),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_12_sub_pixel_avg_variance32x4_c,
                          12),
  SubpelAvgVarianceParams(2, 5, &avm_highbd_12_sub_pixel_avg_variance4x32_c,
                          12),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_12_sub_pixel_avg_variance64x4_c,
                          12),
  SubpelAvgVarianceParams(2, 6, &avm_highbd_12_sub_pixel_avg_variance4x64_c,
                          12),
};
INSTANTIATE_TEST_SUITE_P(C, AvxHBDSubpelAvgVarianceTest,
                         ::testing::ValuesIn(kArrayHBDSubpelAvgVariance_c));

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, MseHBDWxHTest,
    ::testing::Values(MseHBDWxHParams(3, 3, &avm_mse_wxh_16bit_highbd_sse2, 10),
                      MseHBDWxHParams(3, 2, &avm_mse_wxh_16bit_highbd_sse2, 10),
                      MseHBDWxHParams(2, 3, &avm_mse_wxh_16bit_highbd_sse2, 10),
                      MseHBDWxHParams(2, 2, &avm_mse_wxh_16bit_highbd_sse2,
                                      10)));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AvxSubpelVarianceTest,
    ::testing::Values(
        SubpelVarianceParams(2, 2, &avm_highbd_8_sub_pixel_variance4x4_sse4_1,
                             8),
        SubpelVarianceParams(2, 2, &avm_highbd_10_sub_pixel_variance4x4_sse4_1,
                             10),
        SubpelVarianceParams(2, 2, &avm_highbd_12_sub_pixel_variance4x4_sse4_1,
                             12)));

INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AvxSubpelAvgVarianceTest,
    ::testing::Values(
        SubpelAvgVarianceParams(2, 2,
                                &avm_highbd_8_sub_pixel_avg_variance4x4_sse4_1,
                                8),
        SubpelAvgVarianceParams(2, 2,
                                &avm_highbd_10_sub_pixel_avg_variance4x4_sse4_1,
                                10),
        SubpelAvgVarianceParams(2, 2,
                                &avm_highbd_12_sub_pixel_avg_variance4x4_sse4_1,
                                12)));
#endif  // HAVE_SSE4_1

/* TODO(debargha): This test does not support the highbd version
INSTANTIATE_TEST_SUITE_P(
    SSE2, AvxHBDMseTest,
    ::testing::Values(MseParams(4, 4, &avm_highbd_12_mse16x16_sse2),
                      MseParams(4, 3, &avm_highbd_12_mse16x8_sse2),
                      MseParams(3, 4, &avm_highbd_12_mse8x16_sse2),
                      MseParams(3, 3, &avm_highbd_12_mse8x8_sse2),
                      MseParams(4, 4, &avm_highbd_10_mse16x16_sse2),
                      MseParams(4, 3, &avm_highbd_10_mse16x8_sse2),
                      MseParams(3, 4, &avm_highbd_10_mse8x16_sse2),
                      MseParams(3, 3, &avm_highbd_10_mse8x8_sse2),
                      MseParams(4, 4, &avm_highbd_8_mse16x16_sse2),
                      MseParams(4, 3, &avm_highbd_8_mse16x8_sse2),
                      MseParams(3, 4, &avm_highbd_8_mse8x16_sse2),
                      MseParams(3, 3, &avm_highbd_8_mse8x8_sse2)));
*/

const VarianceParams kArrayHBDVariance_sse2[] = {
  VarianceParams(7, 7, &avm_highbd_12_variance128x128_sse2, 12),
  VarianceParams(7, 6, &avm_highbd_12_variance128x64_sse2, 12),
  VarianceParams(6, 7, &avm_highbd_12_variance64x128_sse2, 12),
  VarianceParams(6, 6, &avm_highbd_12_variance64x64_sse2, 12),
  VarianceParams(6, 5, &avm_highbd_12_variance64x32_sse2, 12),
  VarianceParams(5, 6, &avm_highbd_12_variance32x64_sse2, 12),
  VarianceParams(5, 5, &avm_highbd_12_variance32x32_sse2, 12),
  VarianceParams(5, 4, &avm_highbd_12_variance32x16_sse2, 12),
  VarianceParams(4, 5, &avm_highbd_12_variance16x32_sse2, 12),
  VarianceParams(4, 4, &avm_highbd_12_variance16x16_sse2, 12),
  VarianceParams(4, 3, &avm_highbd_12_variance16x8_sse2, 12),
  VarianceParams(3, 4, &avm_highbd_12_variance8x16_sse2, 12),
  VarianceParams(3, 3, &avm_highbd_12_variance8x8_sse2, 12),
  VarianceParams(7, 7, &avm_highbd_10_variance128x128_sse2, 10),
  VarianceParams(7, 6, &avm_highbd_10_variance128x64_sse2, 10),
  VarianceParams(6, 7, &avm_highbd_10_variance64x128_sse2, 10),
  VarianceParams(6, 6, &avm_highbd_10_variance64x64_sse2, 10),
  VarianceParams(6, 5, &avm_highbd_10_variance64x32_sse2, 10),
  VarianceParams(5, 6, &avm_highbd_10_variance32x64_sse2, 10),
  VarianceParams(5, 5, &avm_highbd_10_variance32x32_sse2, 10),
  VarianceParams(5, 4, &avm_highbd_10_variance32x16_sse2, 10),
  VarianceParams(4, 5, &avm_highbd_10_variance16x32_sse2, 10),
  VarianceParams(4, 4, &avm_highbd_10_variance16x16_sse2, 10),
  VarianceParams(4, 3, &avm_highbd_10_variance16x8_sse2, 10),
  VarianceParams(3, 4, &avm_highbd_10_variance8x16_sse2, 10),
  VarianceParams(3, 3, &avm_highbd_10_variance8x8_sse2, 10),
  VarianceParams(7, 7, &avm_highbd_8_variance128x128_sse2, 8),
  VarianceParams(7, 6, &avm_highbd_8_variance128x64_sse2, 8),
  VarianceParams(6, 7, &avm_highbd_8_variance64x128_sse2, 8),
  VarianceParams(6, 6, &avm_highbd_8_variance64x64_sse2, 8),
  VarianceParams(6, 5, &avm_highbd_8_variance64x32_sse2, 8),
  VarianceParams(5, 6, &avm_highbd_8_variance32x64_sse2, 8),
  VarianceParams(5, 5, &avm_highbd_8_variance32x32_sse2, 8),
  VarianceParams(5, 4, &avm_highbd_8_variance32x16_sse2, 8),
  VarianceParams(4, 5, &avm_highbd_8_variance16x32_sse2, 8),
  VarianceParams(4, 4, &avm_highbd_8_variance16x16_sse2, 8),
  VarianceParams(4, 3, &avm_highbd_8_variance16x8_sse2, 8),
  VarianceParams(3, 4, &avm_highbd_8_variance8x16_sse2, 8),
  VarianceParams(3, 3, &avm_highbd_8_variance8x8_sse2, 8),

  VarianceParams(6, 4, &avm_highbd_12_variance64x16_sse2, 12),
  VarianceParams(4, 6, &avm_highbd_12_variance16x64_sse2, 12),
  VarianceParams(5, 3, &avm_highbd_12_variance32x8_sse2, 12),
  VarianceParams(3, 5, &avm_highbd_12_variance8x32_sse2, 12),
  // VarianceParams(4, 2, &avm_highbd_12_variance16x4_sse2, 12),
  // VarianceParams(2, 4, &avm_highbd_12_variance4x16_sse2, 12),
  VarianceParams(6, 4, &avm_highbd_10_variance64x16_sse2, 10),
  VarianceParams(4, 6, &avm_highbd_10_variance16x64_sse2, 10),
  VarianceParams(5, 3, &avm_highbd_10_variance32x8_sse2, 10),
  VarianceParams(3, 5, &avm_highbd_10_variance8x32_sse2, 10),
  // VarianceParams(4, 2, &avm_highbd_10_variance16x4_sse2, 10),
  // VarianceParams(2, 4, &avm_highbd_10_variance4x16_sse2, 10),
  VarianceParams(6, 4, &avm_highbd_8_variance64x16_sse2, 8),
  VarianceParams(4, 6, &avm_highbd_8_variance16x64_sse2, 8),
  VarianceParams(5, 3, &avm_highbd_8_variance32x8_sse2, 8),
  VarianceParams(3, 5, &avm_highbd_8_variance8x32_sse2, 8),
  // VarianceParams(4, 2, &avm_highbd_8_variance16x4_sse2, 8),
  // VarianceParams(2, 4, &avm_highbd_8_variance4x16_sse2, 8),
  VarianceParams(6, 3, &avm_highbd_12_variance64x8_sse2, 12),
  VarianceParams(3, 6, &avm_highbd_12_variance8x64_sse2, 12),
  // VarianceParams(5, 2, &avm_highbd_12_variance32x4_sse2, 12),
  // VarianceParams(2, 5, &avm_highbd_12_variance4x32_sse2, 12),
  // VarianceParams(6, 2, &avm_highbd_12_variance64x4_sse2, 12),
  // VarianceParams(2, 6, &avm_highbd_12_variance4x64_sse2, 12),
  VarianceParams(6, 3, &avm_highbd_10_variance64x8_sse2, 10),
  VarianceParams(3, 6, &avm_highbd_10_variance8x64_sse2, 10),
  // VarianceParams(5, 2, &avm_highbd_10_variance32x4_sse2, 10),
  // VarianceParams(2, 5, &avm_highbd_10_variance4x32_sse2, 10),
  // VarianceParams(6, 2, &avm_highbd_10_variance64x4_sse2, 10),
  // VarianceParams(2, 6, &avm_highbd_10_variance4x64_sse2, 10),
  VarianceParams(6, 3, &avm_highbd_8_variance64x8_sse2, 8),
  VarianceParams(3, 6, &avm_highbd_8_variance8x64_sse2, 8),
  // VarianceParams(5, 2, &avm_highbd_8_variance32x4_sse2, 8),
  // VarianceParams(2, 5, &avm_highbd_8_variance4x32_sse2, 8),
  // VarianceParams(6, 2, &avm_highbd_8_variance64x4_sse2, 8),
  // VarianceParams(2, 6, &avm_highbd_8_variance4x64_sse2, 8),
};
INSTANTIATE_TEST_SUITE_P(SSE2, AvxHBDVarianceTest,
                         ::testing::ValuesIn(kArrayHBDVariance_sse2));

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, MseHBDWxHTest,
    ::testing::Values(MseHBDWxHParams(3, 3, &avm_mse_wxh_16bit_highbd_avx2, 10),
                      MseHBDWxHParams(3, 2, &avm_mse_wxh_16bit_highbd_avx2, 10),
                      MseHBDWxHParams(2, 3, &avm_mse_wxh_16bit_highbd_avx2, 10),
                      MseHBDWxHParams(2, 2, &avm_mse_wxh_16bit_highbd_avx2,
                                      10)));

const VarianceParams kArrayHBDVariance_avx2[] = {
  VarianceParams(8, 8, &avm_highbd_8_variance256x256_avx2, 8),
  VarianceParams(8, 7, &avm_highbd_8_variance256x128_avx2, 8),
  VarianceParams(7, 8, &avm_highbd_8_variance128x256_avx2, 8),
  VarianceParams(7, 7, &avm_highbd_8_variance128x128_avx2, 8),
  VarianceParams(7, 6, &avm_highbd_8_variance128x64_avx2, 8),
  VarianceParams(6, 7, &avm_highbd_8_variance64x128_avx2, 8),
  VarianceParams(6, 6, &avm_highbd_8_variance64x64_avx2, 8),
  VarianceParams(6, 5, &avm_highbd_8_variance64x32_avx2, 8),
  VarianceParams(5, 6, &avm_highbd_8_variance32x64_avx2, 8),
  VarianceParams(5, 5, &avm_highbd_8_variance32x32_avx2, 8),
  VarianceParams(5, 4, &avm_highbd_8_variance32x16_avx2, 8),
  VarianceParams(4, 5, &avm_highbd_8_variance16x32_avx2, 8),
  VarianceParams(4, 4, &avm_highbd_8_variance16x16_avx2, 8),
  VarianceParams(4, 3, &avm_highbd_8_variance16x8_avx2, 8),
  VarianceParams(3, 4, &avm_highbd_8_variance8x16_avx2, 8),
  VarianceParams(3, 3, &avm_highbd_8_variance8x8_avx2, 8),

  VarianceParams(3, 5, &avm_highbd_8_variance8x32_avx2, 8),
  VarianceParams(5, 3, &avm_highbd_8_variance32x8_avx2, 8),
  VarianceParams(4, 6, &avm_highbd_8_variance16x64_avx2, 8),
  VarianceParams(6, 4, &avm_highbd_8_variance64x16_avx2, 8),
  VarianceParams(2, 4, &avm_highbd_8_variance4x16_avx2, 8),
  VarianceParams(4, 2, &avm_highbd_8_variance16x4_avx2, 8),
  VarianceParams(2, 3, &avm_highbd_8_variance4x8_avx2, 8),
  VarianceParams(3, 2, &avm_highbd_8_variance8x4_avx2, 8),
  VarianceParams(6, 3, &avm_highbd_8_variance64x8_avx2, 8),
  VarianceParams(3, 6, &avm_highbd_8_variance8x64_avx2, 8),
  VarianceParams(2, 6, &avm_highbd_8_variance4x64_avx2, 8),
  VarianceParams(6, 2, &avm_highbd_8_variance64x4_avx2, 8),
  VarianceParams(2, 5, &avm_highbd_8_variance4x32_avx2, 8),
  VarianceParams(5, 2, &avm_highbd_8_variance32x4_avx2, 8),

  VarianceParams(8, 8, &avm_highbd_10_variance256x256_avx2, 10),
  VarianceParams(8, 7, &avm_highbd_10_variance256x128_avx2, 10),
  VarianceParams(7, 8, &avm_highbd_10_variance128x256_avx2, 10),
  VarianceParams(7, 7, &avm_highbd_10_variance128x128_avx2, 10),
  VarianceParams(7, 6, &avm_highbd_10_variance128x64_avx2, 10),
  VarianceParams(6, 7, &avm_highbd_10_variance64x128_avx2, 10),
  VarianceParams(6, 6, &avm_highbd_10_variance64x64_avx2, 10),
  VarianceParams(6, 5, &avm_highbd_10_variance64x32_avx2, 10),
  VarianceParams(5, 6, &avm_highbd_10_variance32x64_avx2, 10),
  VarianceParams(5, 5, &avm_highbd_10_variance32x32_avx2, 10),
  VarianceParams(5, 4, &avm_highbd_10_variance32x16_avx2, 10),
  VarianceParams(4, 5, &avm_highbd_10_variance16x32_avx2, 10),
  VarianceParams(4, 4, &avm_highbd_10_variance16x16_avx2, 10),
  VarianceParams(4, 3, &avm_highbd_10_variance16x8_avx2, 10),
  VarianceParams(3, 4, &avm_highbd_10_variance8x16_avx2, 10),
  VarianceParams(3, 3, &avm_highbd_10_variance8x8_avx2, 10),

  VarianceParams(3, 5, &avm_highbd_10_variance8x32_avx2, 10),
  VarianceParams(5, 3, &avm_highbd_10_variance32x8_avx2, 10),
  VarianceParams(4, 6, &avm_highbd_10_variance16x64_avx2, 10),
  VarianceParams(6, 4, &avm_highbd_10_variance64x16_avx2, 10),
  VarianceParams(2, 4, &avm_highbd_10_variance4x16_avx2, 10),
  VarianceParams(4, 2, &avm_highbd_10_variance16x4_avx2, 10),
  VarianceParams(2, 3, &avm_highbd_10_variance4x8_avx2, 10),
  VarianceParams(3, 2, &avm_highbd_10_variance8x4_avx2, 10),
  VarianceParams(6, 3, &avm_highbd_10_variance64x8_avx2, 10),
  VarianceParams(3, 6, &avm_highbd_10_variance8x64_avx2, 10),
  VarianceParams(2, 6, &avm_highbd_10_variance4x64_avx2, 10),
  VarianceParams(6, 2, &avm_highbd_10_variance64x4_avx2, 10),
  VarianceParams(2, 5, &avm_highbd_10_variance4x32_avx2, 10),
  VarianceParams(5, 2, &avm_highbd_10_variance32x4_avx2, 10),

  VarianceParams(8, 8, &avm_highbd_12_variance256x256_avx2, 12),
  VarianceParams(8, 7, &avm_highbd_12_variance256x128_avx2, 12),
  VarianceParams(7, 8, &avm_highbd_12_variance128x256_avx2, 12),
  VarianceParams(7, 7, &avm_highbd_12_variance128x128_avx2, 12),
  VarianceParams(7, 6, &avm_highbd_12_variance128x64_avx2, 12),
  VarianceParams(6, 7, &avm_highbd_12_variance64x128_avx2, 12),
  VarianceParams(6, 6, &avm_highbd_12_variance64x64_avx2, 12),
  VarianceParams(6, 5, &avm_highbd_12_variance64x32_avx2, 12),
  VarianceParams(5, 6, &avm_highbd_12_variance32x64_avx2, 12),
  VarianceParams(5, 5, &avm_highbd_12_variance32x32_avx2, 12),
  VarianceParams(5, 4, &avm_highbd_12_variance32x16_avx2, 12),
  VarianceParams(4, 5, &avm_highbd_12_variance16x32_avx2, 12),
  VarianceParams(4, 4, &avm_highbd_12_variance16x16_avx2, 12),
  VarianceParams(4, 3, &avm_highbd_12_variance16x8_avx2, 12),
  VarianceParams(3, 4, &avm_highbd_12_variance8x16_avx2, 12),
  VarianceParams(3, 3, &avm_highbd_12_variance8x8_avx2, 12),

  VarianceParams(3, 5, &avm_highbd_12_variance8x32_avx2, 12),
  VarianceParams(5, 3, &avm_highbd_12_variance32x8_avx2, 12),
  VarianceParams(4, 6, &avm_highbd_12_variance16x64_avx2, 12),
  VarianceParams(6, 4, &avm_highbd_12_variance64x16_avx2, 12),
  VarianceParams(6, 3, &avm_highbd_12_variance64x8_avx2, 12),
  VarianceParams(3, 6, &avm_highbd_12_variance8x64_avx2, 12),
};

INSTANTIATE_TEST_SUITE_P(AVX2, AvxHBDVarianceTest,
                         ::testing::ValuesIn(kArrayHBDVariance_avx2));

const SubpelVarianceParams kArrayHBDSubpelVariance_avx2[] = {
  // SubpelVarianceParams(8, 8, &avm_highbd_12_sub_pixel_variance256x256_avx2,
  // 12),
  // SubpelVarianceParams(8, 7, &avm_highbd_12_sub_pixel_variance256x128_avx2,
  // 12),
  // SubpelVarianceParams(7, 8, &avm_highbd_12_sub_pixel_variance128x256_avx2,
  // 12),
  // SubpelVarianceParams(7, 7, &avm_highbd_12_sub_pixel_variance128x128_avx2,
  // 12),
  // SubpelVarianceParams(7, 6, &avm_highbd_12_sub_pixel_variance128x64_avx2,
  // 12),
  // SubpelVarianceParams(6, 7, &avm_highbd_12_sub_pixel_variance64x128_avx2,
  // 12),
  // SubpelVarianceParams(6, 6, &avm_highbd_12_sub_pixel_variance64x64_avx2,
  // 12),
  // SubpelVarianceParams(6, 5, &avm_highbd_12_sub_pixel_variance64x32_avx2,
  // 12),
  // SubpelVarianceParams(5, 6, &avm_highbd_12_sub_pixel_variance32x64_avx2,
  // 12),
  // SubpelVarianceParams(5, 5, &avm_highbd_12_sub_pixel_variance32x32_avx2,
  // 12),
  // SubpelVarianceParams(5, 4, &avm_highbd_12_sub_pixel_variance32x16_avx2,
  // 12),
  // SubpelVarianceParams(4, 5, &avm_highbd_12_sub_pixel_variance16x32_avx2,
  // 12),
  // SubpelVarianceParams(4, 4, &avm_highbd_12_sub_pixel_variance16x16_avx2,
  // 12),
  SubpelVarianceParams(4, 3, &avm_highbd_12_sub_pixel_variance16x8_avx2, 12),

  // SubpelVarianceParams(8, 8, &avm_highbd_10_sub_pixel_variance256x256_avx2,
  // 10),
  SubpelVarianceParams(8, 7, &avm_highbd_10_sub_pixel_variance256x128_avx2, 10),
  SubpelVarianceParams(7, 8, &avm_highbd_10_sub_pixel_variance128x256_avx2, 10),
  SubpelVarianceParams(7, 7, &avm_highbd_10_sub_pixel_variance128x128_avx2, 10),
  SubpelVarianceParams(7, 6, &avm_highbd_10_sub_pixel_variance128x64_avx2, 10),
  SubpelVarianceParams(6, 7, &avm_highbd_10_sub_pixel_variance64x128_avx2, 10),
  SubpelVarianceParams(6, 6, &avm_highbd_10_sub_pixel_variance64x64_avx2, 10),
  SubpelVarianceParams(6, 5, &avm_highbd_10_sub_pixel_variance64x32_avx2, 10),
  SubpelVarianceParams(5, 6, &avm_highbd_10_sub_pixel_variance32x64_avx2, 10),
  SubpelVarianceParams(5, 5, &avm_highbd_10_sub_pixel_variance32x32_avx2, 10),
  SubpelVarianceParams(5, 4, &avm_highbd_10_sub_pixel_variance32x16_avx2, 10),
  SubpelVarianceParams(4, 5, &avm_highbd_10_sub_pixel_variance16x32_avx2, 10),
  SubpelVarianceParams(4, 4, &avm_highbd_10_sub_pixel_variance16x16_avx2, 10),
  SubpelVarianceParams(4, 3, &avm_highbd_10_sub_pixel_variance16x8_avx2, 10),

  // SubpelVarianceParams(8, 8, &avm_highbd_8_sub_pixel_variance256x256_avx2,
  // 8),
  SubpelVarianceParams(8, 7, &avm_highbd_8_sub_pixel_variance256x128_avx2, 8),
  SubpelVarianceParams(7, 8, &avm_highbd_8_sub_pixel_variance128x256_avx2, 8),
  SubpelVarianceParams(7, 7, &avm_highbd_8_sub_pixel_variance128x128_avx2, 8),
  SubpelVarianceParams(7, 6, &avm_highbd_8_sub_pixel_variance128x64_avx2, 8),
  SubpelVarianceParams(6, 7, &avm_highbd_8_sub_pixel_variance64x128_avx2, 8),
  SubpelVarianceParams(6, 6, &avm_highbd_8_sub_pixel_variance64x64_avx2, 8),
  SubpelVarianceParams(6, 5, &avm_highbd_8_sub_pixel_variance64x32_avx2, 8),
  SubpelVarianceParams(5, 6, &avm_highbd_8_sub_pixel_variance32x64_avx2, 8),
  SubpelVarianceParams(5, 5, &avm_highbd_8_sub_pixel_variance32x32_avx2, 8),
  SubpelVarianceParams(5, 4, &avm_highbd_8_sub_pixel_variance32x16_avx2, 8),
  SubpelVarianceParams(4, 5, &avm_highbd_8_sub_pixel_variance16x32_avx2, 8),
  SubpelVarianceParams(4, 4, &avm_highbd_8_sub_pixel_variance16x16_avx2, 8),
  SubpelVarianceParams(4, 3, &avm_highbd_8_sub_pixel_variance16x8_avx2, 8),

  // SubpelVarianceParams(6, 4, &avm_highbd_12_sub_pixel_variance64x16_avx2,
  // 12),
  // SubpelVarianceParams(4, 6, &avm_highbd_12_sub_pixel_variance16x64_avx2,
  // 12),
  // SubpelVarianceParams(5, 3, &avm_highbd_12_sub_pixel_variance32x8_avx2, 12),
  SubpelVarianceParams(4, 2, &avm_highbd_12_sub_pixel_variance16x4_avx2, 12),

  SubpelVarianceParams(6, 4, &avm_highbd_10_sub_pixel_variance64x16_avx2, 10),
  SubpelVarianceParams(4, 6, &avm_highbd_10_sub_pixel_variance16x64_avx2, 10),
  SubpelVarianceParams(5, 3, &avm_highbd_10_sub_pixel_variance32x8_avx2, 10),
  SubpelVarianceParams(4, 2, &avm_highbd_10_sub_pixel_variance16x4_avx2, 10),

  SubpelVarianceParams(6, 4, &avm_highbd_8_sub_pixel_variance64x16_avx2, 8),
  SubpelVarianceParams(4, 6, &avm_highbd_8_sub_pixel_variance16x64_avx2, 8),
  SubpelVarianceParams(5, 3, &avm_highbd_8_sub_pixel_variance32x8_avx2, 8),
  SubpelVarianceParams(4, 2, &avm_highbd_8_sub_pixel_variance16x4_avx2, 8),
};

INSTANTIATE_TEST_SUITE_P(AVX2, AvxHBDSubpelVarianceTest,
                         ::testing::ValuesIn(kArrayHBDSubpelVariance_avx2));
#endif  // HAVE_AVX2

const SubpelVarianceParams kArrayHBDSubpelVariance_sse2[] = {
  SubpelVarianceParams(8, 8, &avm_highbd_12_sub_pixel_variance256x256_sse2, 12),
  SubpelVarianceParams(8, 7, &avm_highbd_12_sub_pixel_variance256x128_sse2, 12),
  SubpelVarianceParams(7, 8, &avm_highbd_12_sub_pixel_variance128x256_sse2, 12),
  SubpelVarianceParams(7, 7, &avm_highbd_12_sub_pixel_variance128x128_sse2, 12),
  SubpelVarianceParams(7, 6, &avm_highbd_12_sub_pixel_variance128x64_sse2, 12),
  SubpelVarianceParams(6, 7, &avm_highbd_12_sub_pixel_variance64x128_sse2, 12),
  SubpelVarianceParams(6, 6, &avm_highbd_12_sub_pixel_variance64x64_sse2, 12),
  SubpelVarianceParams(6, 5, &avm_highbd_12_sub_pixel_variance64x32_sse2, 12),
  SubpelVarianceParams(5, 6, &avm_highbd_12_sub_pixel_variance32x64_sse2, 12),
  SubpelVarianceParams(5, 5, &avm_highbd_12_sub_pixel_variance32x32_sse2, 12),
  SubpelVarianceParams(5, 4, &avm_highbd_12_sub_pixel_variance32x16_sse2, 12),
  SubpelVarianceParams(3, 4, &avm_highbd_12_sub_pixel_variance8x16_sse2, 12),
  SubpelVarianceParams(3, 3, &avm_highbd_12_sub_pixel_variance8x8_sse2, 12),
  SubpelVarianceParams(3, 2, &avm_highbd_12_sub_pixel_variance8x4_sse2, 12),

  SubpelVarianceParams(8, 8, &avm_highbd_10_sub_pixel_variance256x256_sse2, 10),
  SubpelVarianceParams(8, 7, &avm_highbd_10_sub_pixel_variance256x128_sse2, 10),
  SubpelVarianceParams(7, 8, &avm_highbd_10_sub_pixel_variance128x256_sse2, 10),
  SubpelVarianceParams(7, 7, &avm_highbd_10_sub_pixel_variance128x128_sse2, 10),
  SubpelVarianceParams(7, 6, &avm_highbd_10_sub_pixel_variance128x64_sse2, 10),
  SubpelVarianceParams(6, 7, &avm_highbd_10_sub_pixel_variance64x128_sse2, 10),
  SubpelVarianceParams(6, 6, &avm_highbd_10_sub_pixel_variance64x64_sse2, 10),
  SubpelVarianceParams(6, 5, &avm_highbd_10_sub_pixel_variance64x32_sse2, 10),
  SubpelVarianceParams(5, 6, &avm_highbd_10_sub_pixel_variance32x64_sse2, 10),
  SubpelVarianceParams(5, 5, &avm_highbd_10_sub_pixel_variance32x32_sse2, 10),
  SubpelVarianceParams(5, 4, &avm_highbd_10_sub_pixel_variance32x16_sse2, 10),
  SubpelVarianceParams(3, 4, &avm_highbd_10_sub_pixel_variance8x16_sse2, 10),
  SubpelVarianceParams(3, 3, &avm_highbd_10_sub_pixel_variance8x8_sse2, 10),
  SubpelVarianceParams(3, 2, &avm_highbd_10_sub_pixel_variance8x4_sse2, 10),

  SubpelVarianceParams(8, 8, &avm_highbd_8_sub_pixel_variance256x256_sse2, 8),
  SubpelVarianceParams(8, 7, &avm_highbd_8_sub_pixel_variance256x128_sse2, 8),
  SubpelVarianceParams(7, 8, &avm_highbd_8_sub_pixel_variance128x256_sse2, 8),
  SubpelVarianceParams(7, 7, &avm_highbd_8_sub_pixel_variance128x128_sse2, 8),
  SubpelVarianceParams(7, 6, &avm_highbd_8_sub_pixel_variance128x64_sse2, 8),
  SubpelVarianceParams(6, 7, &avm_highbd_8_sub_pixel_variance64x128_sse2, 8),
  SubpelVarianceParams(6, 6, &avm_highbd_8_sub_pixel_variance64x64_sse2, 8),
  SubpelVarianceParams(6, 5, &avm_highbd_8_sub_pixel_variance64x32_sse2, 8),
  SubpelVarianceParams(5, 6, &avm_highbd_8_sub_pixel_variance32x64_sse2, 8),
  SubpelVarianceParams(5, 5, &avm_highbd_8_sub_pixel_variance32x32_sse2, 8),
  SubpelVarianceParams(5, 4, &avm_highbd_8_sub_pixel_variance32x16_sse2, 8),
  SubpelVarianceParams(3, 4, &avm_highbd_8_sub_pixel_variance8x16_sse2, 8),
  SubpelVarianceParams(3, 3, &avm_highbd_8_sub_pixel_variance8x8_sse2, 8),
  SubpelVarianceParams(3, 2, &avm_highbd_8_sub_pixel_variance8x4_sse2, 8),

  SubpelVarianceParams(6, 4, &avm_highbd_12_sub_pixel_variance64x16_sse2, 12),
  SubpelVarianceParams(5, 3, &avm_highbd_12_sub_pixel_variance32x8_sse2, 12),
  SubpelVarianceParams(3, 5, &avm_highbd_12_sub_pixel_variance8x32_sse2, 12),
  // SubpelVarianceParams(2, 4, &avm_highbd_12_sub_pixel_variance4x16_sse2, 12),
  SubpelVarianceParams(6, 4, &avm_highbd_10_sub_pixel_variance64x16_sse2, 10),
  SubpelVarianceParams(5, 3, &avm_highbd_10_sub_pixel_variance32x8_sse2, 10),
  SubpelVarianceParams(3, 5, &avm_highbd_10_sub_pixel_variance8x32_sse2, 10),
  // SubpelVarianceParams(2, 4, &avm_highbd_10_sub_pixel_variance4x16_sse2, 10),
  SubpelVarianceParams(6, 4, &avm_highbd_8_sub_pixel_variance64x16_sse2, 8),
  SubpelVarianceParams(5, 3, &avm_highbd_8_sub_pixel_variance32x8_sse2, 8),
  SubpelVarianceParams(3, 5, &avm_highbd_8_sub_pixel_variance8x32_sse2, 8),
  // SubpelVarianceParams(2, 4, &avm_highbd_8_sub_pixel_variance4x16_sse2, 8),
  SubpelVarianceParams(6, 3, &avm_highbd_12_sub_pixel_variance64x8_sse2, 12),
  SubpelVarianceParams(3, 6, &avm_highbd_12_sub_pixel_variance8x64_sse2, 12),
  SubpelVarianceParams(5, 2, &avm_highbd_12_sub_pixel_variance32x4_sse2, 12),
  // SubpelVarianceParams(2, 5, &avm_highbd_12_sub_pixel_variance4x32_sse2, 12),
  SubpelVarianceParams(6, 2, &avm_highbd_12_sub_pixel_variance64x4_sse2, 12),
  // SubpelVarianceParams(2, 6, &avm_highbd_12_sub_pixel_variance4x64_sse2, 12),
  SubpelVarianceParams(6, 3, &avm_highbd_10_sub_pixel_variance64x8_sse2, 10),
  SubpelVarianceParams(3, 6, &avm_highbd_10_sub_pixel_variance8x64_sse2, 10),
  SubpelVarianceParams(5, 2, &avm_highbd_10_sub_pixel_variance32x4_sse2, 10),
  // SubpelVarianceParams(2, 5, &avm_highbd_10_sub_pixel_variance4x32_sse2, 10),
  SubpelVarianceParams(6, 2, &avm_highbd_10_sub_pixel_variance64x4_sse2, 10),
  // SubpelVarianceParams(2, 6, &avm_highbd_10_sub_pixel_variance4x64_sse2, 10),
  SubpelVarianceParams(6, 3, &avm_highbd_8_sub_pixel_variance64x8_sse2, 8),
  SubpelVarianceParams(3, 6, &avm_highbd_8_sub_pixel_variance8x64_sse2, 8),
  SubpelVarianceParams(5, 2, &avm_highbd_8_sub_pixel_variance32x4_sse2, 8),
  // SubpelVarianceParams(2, 5, &avm_highbd_8_sub_pixel_variance4x32_sse2, 8),
  SubpelVarianceParams(6, 2, &avm_highbd_8_sub_pixel_variance64x4_sse2, 8),
  // SubpelVarianceParams(2, 6, &avm_highbd_8_sub_pixel_variance4x64_sse2, 8),
};
INSTANTIATE_TEST_SUITE_P(SSE2, AvxHBDSubpelVarianceTest,
                         ::testing::ValuesIn(kArrayHBDSubpelVariance_sse2));

const SubpelAvgVarianceParams kArrayHBDSubpelAvgVariance_sse2[] = {
  SubpelAvgVarianceParams(6, 6, &avm_highbd_12_sub_pixel_avg_variance64x64_sse2,
                          12),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_12_sub_pixel_avg_variance64x32_sse2,
                          12),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_12_sub_pixel_avg_variance32x64_sse2,
                          12),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_12_sub_pixel_avg_variance32x32_sse2,
                          12),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_12_sub_pixel_avg_variance32x16_sse2,
                          12),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_12_sub_pixel_avg_variance8x16_sse2,
                          12),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_12_sub_pixel_avg_variance8x8_sse2,
                          12),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_12_sub_pixel_avg_variance8x4_sse2,
                          12),
  SubpelAvgVarianceParams(6, 6, &avm_highbd_10_sub_pixel_avg_variance64x64_sse2,
                          10),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_10_sub_pixel_avg_variance64x32_sse2,
                          10),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_10_sub_pixel_avg_variance32x64_sse2,
                          10),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_10_sub_pixel_avg_variance32x32_sse2,
                          10),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_10_sub_pixel_avg_variance32x16_sse2,
                          10),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_10_sub_pixel_avg_variance8x16_sse2,
                          10),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_10_sub_pixel_avg_variance8x8_sse2,
                          10),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_10_sub_pixel_avg_variance8x4_sse2,
                          10),
  SubpelAvgVarianceParams(6, 6, &avm_highbd_8_sub_pixel_avg_variance64x64_sse2,
                          8),
  SubpelAvgVarianceParams(6, 5, &avm_highbd_8_sub_pixel_avg_variance64x32_sse2,
                          8),
  SubpelAvgVarianceParams(5, 6, &avm_highbd_8_sub_pixel_avg_variance32x64_sse2,
                          8),
  SubpelAvgVarianceParams(5, 5, &avm_highbd_8_sub_pixel_avg_variance32x32_sse2,
                          8),
  SubpelAvgVarianceParams(5, 4, &avm_highbd_8_sub_pixel_avg_variance32x16_sse2,
                          8),
  SubpelAvgVarianceParams(3, 4, &avm_highbd_8_sub_pixel_avg_variance8x16_sse2,
                          8),
  SubpelAvgVarianceParams(3, 3, &avm_highbd_8_sub_pixel_avg_variance8x8_sse2,
                          8),
  SubpelAvgVarianceParams(3, 2, &avm_highbd_8_sub_pixel_avg_variance8x4_sse2,
                          8),

  SubpelAvgVarianceParams(6, 4, &avm_highbd_12_sub_pixel_avg_variance64x16_sse2,
                          12),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_12_sub_pixel_avg_variance32x8_sse2,
                          12),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_12_sub_pixel_avg_variance8x32_sse2,
                          12),
  // SubpelAvgVarianceParams(2, 4,
  // &avm_highbd_12_sub_pixel_avg_variance4x16_sse2, 12),
  SubpelAvgVarianceParams(6, 4, &avm_highbd_10_sub_pixel_avg_variance64x16_sse2,
                          10),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_10_sub_pixel_avg_variance32x8_sse2,
                          10),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_10_sub_pixel_avg_variance8x32_sse2,
                          10),
  // SubpelAvgVarianceParams(2, 4,
  // &avm_highbd_10_sub_pixel_avg_variance4x16_sse2, 10),
  SubpelAvgVarianceParams(6, 4, &avm_highbd_8_sub_pixel_avg_variance64x16_sse2,
                          8),
  SubpelAvgVarianceParams(5, 3, &avm_highbd_8_sub_pixel_avg_variance32x8_sse2,
                          8),
  SubpelAvgVarianceParams(3, 5, &avm_highbd_8_sub_pixel_avg_variance8x32_sse2,
                          8),
  // SubpelAvgVarianceParams(2, 4,
  // &avm_highbd_8_sub_pixel_avg_variance4x16_sse2, 8),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_12_sub_pixel_avg_variance64x8_sse2,
                          12),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_12_sub_pixel_avg_variance8x64_sse2,
                          12),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_12_sub_pixel_avg_variance32x4_sse2,
                          12),
  // SubpelAvgVarianceParams(2, 5,
  //                         &avm_highbd_12_sub_pixel_avg_variance4x32_sse2,
  //                         12),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_12_sub_pixel_avg_variance64x4_sse2,
                          12),
  // SubpelAvgVarianceParams(2, 6,
  //                         &avm_highbd_12_sub_pixel_avg_variance4x64_sse2,
  //                         12),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_10_sub_pixel_avg_variance64x8_sse2,
                          10),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_10_sub_pixel_avg_variance8x64_sse2,
                          10),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_10_sub_pixel_avg_variance32x4_sse2,
                          10),
  // SubpelAvgVarianceParams(2, 5,
  //                         &avm_highbd_10_sub_pixel_avg_variance4x32_sse2,
  //                         10),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_10_sub_pixel_avg_variance64x4_sse2,
                          10),
  // SubpelAvgVarianceParams(2, 6,
  //                         &avm_highbd_10_sub_pixel_avg_variance4x64_sse2,
  //                         10),
  SubpelAvgVarianceParams(6, 3, &avm_highbd_8_sub_pixel_avg_variance64x8_sse2,
                          8),
  SubpelAvgVarianceParams(3, 6, &avm_highbd_8_sub_pixel_avg_variance8x64_sse2,
                          8),
  SubpelAvgVarianceParams(5, 2, &avm_highbd_8_sub_pixel_avg_variance32x4_sse2,
                          8),
  // SubpelAvgVarianceParams(2, 5,
  //                         &avm_highbd_8_sub_pixel_avg_variance4x32_sse2, 8),
  SubpelAvgVarianceParams(6, 2, &avm_highbd_8_sub_pixel_avg_variance64x4_sse2,
                          8),
  // SubpelAvgVarianceParams(2, 6,
  //`                        &avm_highbd_8_sub_pixel_avg_variance4x64_sse2, 8),
};

INSTANTIATE_TEST_SUITE_P(SSE2, AvxHBDSubpelAvgVarianceTest,
                         ::testing::ValuesIn(kArrayHBDSubpelAvgVariance_sse2));
#endif  // HAVE_SSE2
}  // namespace
