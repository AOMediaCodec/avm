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
#include <cstring>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "avm_mem/avm_mem.h"
#include "avm_ports/avm_timer.h"
#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#include "test/acm_random.h"
#pragma GCC diagnostic pop
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/restoration.h"

namespace {

// TODO(any): Remove following INTERP_FILTERS_ALL define, so that 12-tap filter
// is tested once 12-tap filter SIMD is done.
#undef INTERP_FILTERS_ALL
#define INTERP_FILTERS_ALL 4

// All single reference convolve tests are parameterized on block size,
// bit-depth, and function to test.
//
// Note that parameterizing on these variables (and not other parameters) is
// a conscious decision - Jenkins needs some degree of parallelization to run
// the tests within the time limit, but if the number of parameters increases
// too much, the gtest framework does not handle it well (increased overhead per
// test, huge amount of output to stdout, etc.).
//
// Also note that the test suites must be named with the architecture, e.g.,
// C, C_X, AVX2_X, ... The test suite that runs on Jenkins sometimes runs tests
// that cannot deal with intrinsics (e.g., the Valgrind tests on 32-bit x86
// binaries) and will disable tests using a filter like
// --gtest_filter=-:SSE4_1.*. If the test suites are not named this way, the
// testing infrastructure will not selectively filter them properly.
class BlockSize {
 public:
  BlockSize(int w, int h) : width_(w), height_(h) {}

  int Width() const { return width_; }
  int Height() const { return height_; }

  bool operator<(const BlockSize &other) const {
    if (Width() == other.Width()) {
      return Height() < other.Height();
    }
    return Width() < other.Width();
  }

  bool operator==(const BlockSize &other) const {
    return Width() == other.Width() && Height() == other.Height();
  }

 private:
  int width_;
  int height_;
};

// Block size / bit depth / test function used to parameterize the tests.
template <typename T>
class TestParam {
 public:
  TestParam(const BlockSize &block, int bd, T test_func)
      : block_(block), bd_(bd), test_func_(test_func) {}

  const BlockSize &Block() const { return block_; }
  int BitDepth() const { return bd_; }
  T TestFunction() const { return test_func_; }

  bool operator==(const TestParam &other) const {
    return Block() == other.Block() && BitDepth() == other.BitDepth() &&
           TestFunction() == other.TestFunction();
  }

 private:
  BlockSize block_;
  int bd_;
  T test_func_;
};

template <typename T>
std::ostream &operator<<(std::ostream &os, const TestParam<T> &test_arg) {
  return os << "TestParam { width:" << test_arg.Block().Width()
            << " height:" << test_arg.Block().Height()
            << " bd:" << test_arg.BitDepth() << " }";
}

// Generate the list of all block widths / heights that need to be tested,
// includes chroma and luma sizes, for the given bit-depths. The test
// function is the same for all generated parameters.
template <typename T>
std::vector<TestParam<T>> GetTestParams(std::initializer_list<int> bit_depths,
                                        T test_func) {
  std::set<BlockSize> sizes;
  for (int b = BLOCK_4X4; b < BLOCK_SIZES_ALL; ++b) {
    const int w = block_size_wide[b];
    const int h = block_size_high[b];
    sizes.insert(BlockSize(w, h));
    // Add in smaller chroma sizes as well.
    if (w == 4 || h == 4) {
      sizes.insert(BlockSize(w / 2, h / 2));
    }
  }
  sizes.insert(BlockSize(24, 24));
  // AV2 extended partition sizes seen in convolve histograms (not in BLOCK_SIZE
  // enum).
  sizes.insert(BlockSize(12, 12));
  sizes.insert(BlockSize(12, 20));
  sizes.insert(BlockSize(16, 24));
  sizes.insert(BlockSize(20, 12));
  sizes.insert(BlockSize(20, 20));
  sizes.insert(BlockSize(24, 16));
  std::vector<TestParam<T>> result;
  for (const BlockSize &block : sizes) {
    for (int bd : bit_depths) {
      result.push_back(TestParam<T>(block, bd, test_func));
    }
  }
  return result;
}

// Test the test-parameters generators work as expected.
class AV2ConvolveParametersTest : public ::testing::Test {};

template <typename T>
std::vector<TestParam<T>> GetHighbdTestParams(T test_func) {
  return GetTestParams({ 10, 12 }, test_func);
}

template <typename T>
::testing::internal::ParamGenerator<TestParam<T>> BuildHighbdParams(
    T test_func) {
  return ::testing::ValuesIn(GetHighbdTestParams(test_func));
}

TEST_F(AV2ConvolveParametersTest, GetHighbdTestParams) {
  auto v = GetHighbdTestParams(av2_highbd_convolve_x_sr_c);
  ASSERT_EQ(86U, v.size());
  int num_10 = 0;
  int num_12 = 0;
  for (const auto &p : v) {
    ASSERT_TRUE(p.BitDepth() == 10 || p.BitDepth() == 12);
    bool same_fn = av2_highbd_convolve_x_sr_c == p.TestFunction();
    ASSERT_TRUE(same_fn);
    if (p.BitDepth() == 10) {
      ++num_10;
    } else {
      ++num_12;
    }
  }
  ASSERT_EQ(num_10, num_12);
}

// AV2ConvolveTest is the base class that all convolve tests should derive from.
// It provides storage/methods for generating randomized buffers for both
// low bit-depth and high bit-depth, and setup/teardown methods for clearing
// system state. Implementors can get the bit-depth / block-size /
// test function by calling GetParam().
template <typename T>
class AV2ConvolveTest : public ::testing::TestWithParam<TestParam<T>> {
 public:
  virtual ~AV2ConvolveTest() { TearDown(); }

  virtual void SetUp() override {
    rnd_.Reset(libavm_test::ACMRandom::DeterministicSeed());
  }

  virtual void TearDown() override { libavm_test::ClearSystemState(); }

  // Randomizes the 8-bit input buffer and returns a pointer to it. Note that
  // the pointer is safe to use with an 8-tap filter. The stride can range
  // from width to (width + kPadding). Also note that the pointer is to the
  // same memory location.
  static constexpr int kInputPadding = 8;

  // Get a pointer to a buffer with stride == width. Note that we must have
  // the test param passed in explicitly -- the gtest framework does not
  // support calling GetParam() within a templatized class.
  // Note that FirstRandomInput8 always returns the same pointer -- if two
  // inputs are needed, also use SecondRandomInput8.
  const uint8_t *FirstRandomInput8(const TestParam<T> &param) {
    // Note we can't call GetParam() directly -- gtest does not support
    // this for parameterized types.
    return RandomInput8(input8_1_, param);
  }

  const uint8_t *SecondRandomInput8(const TestParam<T> &param) {
    return RandomInput8(input8_2_, param);
  }

  // Some of the intrinsics perform writes in 32 byte chunks. Moreover, some
  // of the instrinsics assume that the stride is also a multiple of 32.
  // To satisfy these constraints and also remain simple, output buffer strides
  // are assumed MAX_SB_SIZE.
  static constexpr int kOutputStride = MAX_SB_SIZE;

  // Check that two 8-bit output buffers are identical.
  void AssertOutputBufferEq(const uint8_t *p1, const uint8_t *p2, int width,
                            int height) {
    ASSERT_TRUE(p1 != p2) << "Buffers must be at different memory locations";
    for (int j = 0; j < height; ++j) {
      if (memcmp(p1, p2, sizeof(*p1) * width) == 0) {
        p1 += kOutputStride;
        p2 += kOutputStride;
        continue;
      }
      for (int i = 0; i < width; ++i) {
        ASSERT_EQ(p1[i], p2[i])
            << width << "x" << height << " Pixel mismatch at (" << i << ", "
            << j << ")";
      }
    }
  }

  // Check that two 16-bit output buffers are identical.
  void AssertOutputBufferEq(const uint16_t *p1, const uint16_t *p2, int width,
                            int height) {
    ASSERT_TRUE(p1 != p2) << "Buffers must be in different memory locations";
    for (int j = 0; j < height; ++j) {
      if (memcmp(p1, p2, sizeof(*p1) * width) == 0) {
        p1 += kOutputStride;
        p2 += kOutputStride;
        continue;
      }
      for (int i = 0; i < width; ++i) {
        ASSERT_EQ(p1[i], p2[i])
            << width << "x" << height << " Pixel mismatch at (" << i << ", "
            << j << ")";
      }
    }
  }

  // Note that the randomized values are capped by bit-depth.
  const uint16_t *FirstRandomInput12(const TestParam<T> &param) {
    return RandomInput12(input16_1_, param);
  }

  const uint16_t *SecondRandomInput12(const TestParam<T> &param) {
    return RandomInput12(input16_2_, param);
  }

  const uint16_t *FirstRandomInput16Extreme(const TestParam<T> &param) {
    return RandomInput16Extreme(input16_1_, param);
  }

 private:
  const uint8_t *RandomInput8(uint8_t *p, const TestParam<T> &param) {
    EXPECT_EQ(8, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int padded_width = param.Block().Width() + kInputPadding;
    const int padded_height = param.Block().Height() + kInputPadding;
    Randomize(p, padded_width * padded_height);
    return p + (kInputPadding / 2) * padded_width + kInputPadding / 2;
  }

  void Randomize(uint8_t *p, int size) {
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand8();
    }
  }

  const uint16_t *RandomInput12(uint16_t *p, const TestParam<T> &param) {
    // Check that this is only called with high bit-depths up to 12.
    EXPECT_TRUE(param.BitDepth() == 10 || param.BitDepth() == 12);
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int padded_width = param.Block().Width() + kInputPadding;
    const int padded_height = param.Block().Height() + kInputPadding;
    Randomize12(p, padded_width * padded_height, param.BitDepth());
    return p + (kInputPadding / 2) * padded_width + kInputPadding / 2;
  }

  void Randomize12(uint16_t *p, int size, int bit_depth) {
    EXPECT_TRUE(bit_depth == 10 || bit_depth == 12);
    // Make sure bitdepth is capped in case error not triggered
    const int bd_capped = bit_depth >= 12 ? 12 : bit_depth;
    for (int i = 0; i < size; ++i) {
      p[i] = (uint16_t)Clamp(rnd_.Rand12(), 0, (1 << bd_capped) - 1);
    }
  }

  int Clamp(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
  }

  const uint16_t *RandomInput16Extreme(uint16_t *p, const TestParam<T> &param) {
    // Check that this is only called with high bit-depths.
    EXPECT_TRUE(param.BitDepth() == 10 || param.BitDepth() == 12);
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int padded_width = param.Block().Width() + kInputPadding;
    const int padded_height = param.Block().Height() + kInputPadding;
    RandomizeExtreme(p, padded_width * padded_height, param.BitDepth());
    return p + (kInputPadding / 2) * padded_width + kInputPadding / 2;
  }

  void RandomizeExtreme(uint16_t *p, int size, int max_bit_range) {
    EXPECT_GE(12, max_bit_range);
    const int max_val = (1 << max_bit_range) - 1;
    for (int i = 0; i < size; ++i) {
      p[i] = static_cast<uint16_t>(RandBool() ? max_val : 0);
    }
  }

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  static constexpr int kInputStride = MAX_SB_SIZE + kInputPadding;

  libavm_test::ACMRandom rnd_;
  // Statically allocate all the memory that is needed for the tests. Note
  // that we cannot allocate output memory here. It must use DECLARE_ALIGNED,
  // which is a C99 feature and interacts badly with C++ member variables.
  uint8_t input8_1_[kInputStride * kInputStride];
  uint8_t input8_2_[kInputStride * kInputStride];
  uint16_t input16_1_[kInputStride * kInputStride];
  uint16_t input16_2_[kInputStride * kInputStride];
};

/////////////////////////////////////////////////////////
// Single reference convolve-x functions (high bit-depth)
/////////////////////////////////////////////////////////
typedef void (*highbd_convolve_x_func)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x, const int subpel_x_qn,
    ConvolveParams *conv_params, int bd);

class AV2ConvolveXHighbdTest : public AV2ConvolveTest<highbd_convolve_x_func> {
 public:
  void RunTest() {
    for (int sub_x = 0; sub_x < 16; ++sub_x) {
      for (int filter = EIGHTTAP_REGULAR; filter < INTERP_FILTERS_ALL;
           ++filter) {
        InterpFilter f = static_cast<InterpFilter>(filter);
        TestConvolve(sub_x, f);
      }
    }
  }

  void SpeedTest() { TestConvolveSpeed(EIGHTTAP_REGULAR, 8, 20000); }

 private:
  void TestConvolve(const int sub_x, const InterpFilter filter) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(filter, width);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    av2_highbd_convolve_x_sr_c(input, width, reference, kOutputStride, width,
                               height, filter_params_x, sub_x, &conv_params1,
                               bit_depth);

    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height,
                              filter_params_x, sub_x, &conv_params2, bit_depth);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void TestConvolveSpeed(const InterpFilter filter, const int sub_x,
                         const int num_iters) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(filter, width);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);

    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      av2_highbd_convolve_x_sr_c(input, width, reference, kOutputStride, width,
                                 height, filter_params_x, sub_x, &conv_params1,
                                 bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time1 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      GetParam().TestFunction()(input, width, test, kOutputStride, width,
                                height, filter_params_x, sub_x, &conv_params2,
                                bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time2 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    printf("f%d %3dx%-3d bd: %d ref: %d mod: %d (%3.2f)\n", filter, width,
           height, bit_depth, time1, time2, (double)time1 / time2);
  }
};

TEST_P(AV2ConvolveXHighbdTest, RunTest) { RunTest(); }
TEST_P(AV2ConvolveXHighbdTest, DISABLED_Speed) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveXHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_x_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV2ConvolveXHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_x_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveXHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_x_sr_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveXHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_x_sr_neon));
#endif
#if HAVE_AVX512
INSTANTIATE_TEST_SUITE_P(AVX512, AV2ConvolveXHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_x_sr_avx512));
#endif

/////////////////////////////////////////////////////////
// Single reference convolve-y functions (high bit-depth)
/////////////////////////////////////////////////////////
typedef void (*highbd_convolve_y_func)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn,
    int bd);

class AV2ConvolveYHighbdTest : public AV2ConvolveTest<highbd_convolve_y_func> {
 public:
  void RunTest() {
    for (int sub_y = 0; sub_y < 16; ++sub_y) {
      for (int filter = EIGHTTAP_REGULAR; filter < INTERP_FILTERS_ALL;
           ++filter) {
        InterpFilter f = static_cast<InterpFilter>(filter);
        TestConvolve(sub_y, f);
      }
    }
  }

  void SpeedTest() { TestConvolveSpeed(EIGHTTAP_REGULAR, 8, 20000); }

 private:
  void TestConvolve(const int sub_y, const InterpFilter filter) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(filter, height);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    av2_highbd_convolve_y_sr_c(input, width, reference, kOutputStride, width,
                               height, filter_params_y, sub_y, bit_depth);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height,
                              filter_params_y, sub_y, bit_depth);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void TestConvolveSpeed(const InterpFilter filter, const int sub_y,
                         const int num_iters) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(filter, height);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);

    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      av2_highbd_convolve_y_sr_c(input, width, reference, kOutputStride, width,
                                 height, filter_params_y, sub_y, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time1 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      GetParam().TestFunction()(input, width, test, kOutputStride, width,
                                height, filter_params_y, sub_y, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time2 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    printf("f%d %3dx%-3d bd: %d ref: %d mod: %d (%3.2f)\n", filter, width,
           height, bit_depth, time1, time2, (double)time1 / time2);
  }
};

TEST_P(AV2ConvolveYHighbdTest, RunTest) { RunTest(); }
TEST_P(AV2ConvolveYHighbdTest, DISABLED_Speed) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveYHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_y_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV2ConvolveYHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_y_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveYHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_y_sr_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveYHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_y_sr_neon));
#endif
#if HAVE_AVX512
INSTANTIATE_TEST_SUITE_P(AVX512, AV2ConvolveYHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_y_sr_avx512));
#endif

///////////////////////////////////////////////////////////////
// Single reference convolve-copy functions (high bit-depth)
///////////////////////////////////////////////////////////////
typedef void (*highbd_convolve_copy_func)(const uint16_t *src,
                                          ptrdiff_t src_stride, uint16_t *dst,
                                          ptrdiff_t dst_stride, int w, int h);

class AV2ConvolveCopyHighbdTest
    : public AV2ConvolveTest<highbd_convolve_copy_func> {
 public:
  void RunTest() {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    avm_highbd_convolve_copy_c(input, width, reference, kOutputStride, width,
                               height);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV2ConvolveCopyHighbdTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveCopyHighbdTest,
                         BuildHighbdParams(avm_highbd_convolve_copy_c));

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, AV2ConvolveCopyHighbdTest,
                         BuildHighbdParams(avm_highbd_convolve_copy_sse2));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveCopyHighbdTest,
                         BuildHighbdParams(avm_highbd_convolve_copy_avx2));
#endif

//////////////////////////////////////////////////////////
// Single reference convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_2d_func)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);

class AV2Convolve2DHighbdTest
    : public AV2ConvolveTest<highbd_convolve_2d_func> {
 public:
  void RunTest() {
    for (int sub_x = 0; sub_x < 16; ++sub_x) {
      for (int sub_y = 0; sub_y < 16; ++sub_y) {
        for (int h_f = EIGHTTAP_REGULAR; h_f <= BILINEAR; ++h_f) {
          for (int v_f = EIGHTTAP_REGULAR; v_f <= BILINEAR; ++v_f) {
            TestConvolve(static_cast<InterpFilter>(h_f),
                         static_cast<InterpFilter>(v_f), sub_x, sub_y);
          }
        }
      }
    }
  }

  void SpeedTest() {
    for (int h_f = EIGHTTAP_REGULAR; h_f <= BILINEAR; ++h_f) {
      for (int v_f = EIGHTTAP_REGULAR; v_f <= BILINEAR; ++v_f) {
        TestConvolveSpeed(static_cast<InterpFilter>(h_f),
                          static_cast<InterpFilter>(v_f), 50000, 8, 8);
      }
    }
  }

 private:
  void TestConvolve(const InterpFilter h_f, const InterpFilter v_f,
                    const int sub_x, const int sub_y) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(v_f, height);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    av2_highbd_convolve_2d_sr_c(input, width, reference, kOutputStride, width,
                                height, filter_params_x, filter_params_y, sub_x,
                                sub_y, &conv_params1, bit_depth);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height,
                              filter_params_x, filter_params_y, sub_x, sub_y,
                              &conv_params2, bit_depth);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void TestConvolveSpeed(const InterpFilter h_f, const InterpFilter v_f,
                         int num_iters, int sub_x, int sub_y) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(v_f, height);

    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      av2_highbd_convolve_2d_sr_c(input, width, reference, kOutputStride, width,
                                  height, filter_params_x, filter_params_y,
                                  sub_x, sub_y, &conv_params1, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time1 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      GetParam().TestFunction()(input, width, test, kOutputStride, width,
                                height, filter_params_x, filter_params_y, sub_x,
                                sub_y, &conv_params2, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time2 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    printf("%d - %d %3dx%-3d bd: %d ref: %d mod: %d (%3.2f)\n", h_f, v_f, width,
           height, bit_depth, time1, time2, (double)time1 / time2);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV2Convolve2DHighbdTest, RunTest) { RunTest(); }
TEST_P(AV2Convolve2DHighbdTest, DISABLED_Speed) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2Convolve2DHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_2d_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV2Convolve2DHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_2d_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2Convolve2DHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_2d_sr_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2Convolve2DHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_2d_sr_neon));
#endif

#if HAVE_AVX512
INSTANTIATE_TEST_SUITE_P(AVX512, AV2Convolve2DHighbdTest,
                         BuildHighbdParams(av2_highbd_convolve_2d_sr_avx512));
#endif

//////////////////////////
// Compound Convolve Tests
//////////////////////////

// The compound functions do not work for chroma block sizes. Provide
// a function to generate test parameters for just luma block sizes.
template <typename T>
std::vector<TestParam<T>> GetLumaTestParams(
    std::initializer_list<int> bit_depths, T test_func) {
  std::set<BlockSize> sizes;
  for (int b = BLOCK_4X4; b < BLOCK_SIZES_ALL; ++b) {
    const int w = block_size_wide[b];
    const int h = block_size_high[b];
    sizes.insert(BlockSize(w, h));
  }
  std::vector<TestParam<T>> result;
  for (int bit_depth : bit_depths) {
    for (const auto &block : sizes) {
      result.push_back(TestParam<T>(block, bit_depth, test_func));
    }
  }
  return result;
}

template <typename T>
std::vector<TestParam<T>> GetHighbdLumaTestParams(T test_func) {
  return GetLumaTestParams({ 10, 12 }, test_func);
}

TEST_F(AV2ConvolveParametersTest, GetHighbdLumaTestParams) {
  auto v = GetHighbdLumaTestParams(av2_highbd_cwp_convolve_x_c);
  ASSERT_EQ(static_cast<size_t>(BLOCK_SIZES_ALL * 2), v.size());
  int num_10 = 0;
  int num_12 = 0;
  for (const auto &e : v) {
    ASSERT_TRUE(10 == e.BitDepth() || 12 == e.BitDepth());
    bool same_fn = av2_highbd_cwp_convolve_x_c == e.TestFunction();
    ASSERT_TRUE(same_fn);
    if (e.BitDepth() == 10) {
      ++num_10;
    } else {
      ++num_12;
    }
  }
  ASSERT_EQ(num_10, num_12);
}

template <typename T>
::testing::internal::ParamGenerator<TestParam<T>> BuildHighbdLumaParams(
    T test_func) {
  return ::testing::ValuesIn(GetHighbdLumaTestParams(test_func));
}

// Compound cases also need to test different frame offsets and weightings.
class CompoundParam {
 public:
  CompoundParam(int fwd_offset, int bck_offset)
      : fwd_offset_(fwd_offset), bck_offset_(bck_offset) {}

  bool UseWtdCompAvg() const {
    return bck_offset_ != (1 << (DIST_PRECISION_BITS - 1)) ||
           fwd_offset_ != (1 << (DIST_PRECISION_BITS - 1));
  }
  int FwdOffset() const { return fwd_offset_; }
  int BckOffset() const { return bck_offset_; }

 private:
  int fwd_offset_;
  int bck_offset_;
};

std::vector<CompoundParam> GetCompoundParams() {
  std::vector<CompoundParam> result;
  result.push_back(CompoundParam(1 << (DIST_PRECISION_BITS - 1),
                                 1 << (DIST_PRECISION_BITS - 1)));
  for (int k = 0; k < 2; ++k) {
    for (int l = 0; l < 4; ++l) {
      result.push_back(CompoundParam(quant_dist_lookup_table[l][k],
                                     quant_dist_lookup_table[l][1 - k]));
    }
  }
  // CWP and TIP weights can go negative (e.g., {20,-4}, {18,-2}).
  result.push_back(CompoundParam(20, -4));
  result.push_back(CompoundParam(-4, 20));
  result.push_back(CompoundParam(18, -2));
  result.push_back(CompoundParam(-2, 18));
  return result;
}

TEST_F(AV2ConvolveParametersTest, GetCompoundParams) {
  auto v = GetCompoundParams();
  ASSERT_EQ(13U, v.size());
  ASSERT_FALSE(v[0].UseWtdCompAvg());
  for (size_t i = 1; i < v.size(); ++i) {
    ASSERT_TRUE(v[i].UseWtdCompAvg());
  }
}

/////////////////////////////////////////////////
// Compound convolve-x functions (high bit-depth)
/////////////////////////////////////////////////
ConvolveParams GetConvolveParams(int do_average, CONV_BUF_TYPE *conv_buf,
                                 int width, int bit_depth,
                                 const CompoundParam &compound) {
  ConvolveParams conv_params =
      get_conv_params_no_round(do_average, 0, conv_buf, width, 1, bit_depth);
  (void)compound;
  conv_params.fwd_offset = compound.FwdOffset();
  conv_params.bck_offset = compound.BckOffset();
  return conv_params;
}

class AV2ConvolveXHighbdCompoundTest
    : public AV2ConvolveTest<highbd_convolve_x_func> {
 public:
  void RunTest() {
    auto compound_params = GetCompoundParams();
    for (int sub_pix = 0; sub_pix < 16; ++sub_pix) {
      for (int f = EIGHTTAP_REGULAR; f < INTERP_FILTERS_ALL; ++f) {
        for (const auto &c : compound_params) {
          TestConvolve(sub_pix, static_cast<InterpFilter>(f), c);
        }
      }
    }
  }

 protected:
  virtual const InterpFilterParams *FilterParams(InterpFilter f,
                                                 const BlockSize &block) const {
    return av2_get_interp_filter_params_with_block_size(f, block.Width());
  }

  virtual highbd_convolve_x_func ReferenceFunc() const {
    return av2_highbd_cwp_convolve_x_c;
  }

 private:
  void TestConvolve(const int sub_pix, const InterpFilter filter,
                    const CompoundParam &compound) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();

    const uint16_t *input1 = FirstRandomInput12(GetParam());
    const uint16_t *input2 = SecondRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, reference_conv_buf[MAX_SB_SQUARE]);
    Convolve(ReferenceFunc(), input1, input2, reference, reference_conv_buf,
             compound, sub_pix, filter);

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, test_conv_buf[MAX_SB_SQUARE]);
    Convolve(GetParam().TestFunction(), input1, input2, test, test_conv_buf,
             compound, sub_pix, filter);

    AssertOutputBufferEq(reference_conv_buf, test_conv_buf, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void Convolve(highbd_convolve_x_func test_func, const uint16_t *src1,
                const uint16_t *src2, uint16_t *dst, CONV_BUF_TYPE *conv_buf,
                const CompoundParam &compound, const int sub_pix,
                const InterpFilter filter) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params =
        FilterParams(filter, GetParam().Block());
    ConvolveParams conv_params =
        GetConvolveParams(0, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src1, width, dst, kOutputStride, width, height, filter_params,
              sub_pix, &conv_params, bit_depth);
    conv_params =
        GetConvolveParams(1, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src2, width, dst, kOutputStride, width, height, filter_params,
              sub_pix, &conv_params, bit_depth);
  }
};

TEST_P(AV2ConvolveXHighbdCompoundTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveXHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_x_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2ConvolveXHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_x_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveXHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_x_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveXHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_x_neon));
#endif

/////////////////////////////////////////////////
// Compound convolve-y functions (high bit-depth)
/////////////////////////////////////////////////

// Again, the X and Y convolve functions have the same type signature and logic.
class AV2ConvolveYHighbdCompoundTest : public AV2ConvolveXHighbdCompoundTest {
  virtual highbd_convolve_x_func ReferenceFunc() const override {
    return av2_highbd_cwp_convolve_y_c;
  }
  virtual const InterpFilterParams *FilterParams(
      InterpFilter f, const BlockSize &block) const override {
    return av2_get_interp_filter_params_with_block_size(f, block.Height());
  }
};

TEST_P(AV2ConvolveYHighbdCompoundTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveYHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_y_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2ConvolveYHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_y_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveYHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_y_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveYHighbdCompoundTest,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_y_neon));
#endif

///////////////////////////////////////////////////////
// Compound convolve-2d-copy functions (high bit-depth)
///////////////////////////////////////////////////////
typedef void (*highbd_compound_conv_2d_copy_func)(const uint16_t *src,
                                                  int src_stride, uint16_t *dst,
                                                  int dst_stride, int w, int h,
                                                  ConvolveParams *conv_params,
                                                  int bd);

class AV2Convolve2DCopyHighbdCompoundTest
    : public AV2ConvolveTest<highbd_compound_conv_2d_copy_func> {
 public:
  void RunTest() {
    auto compound_params = GetCompoundParams();
    for (const auto &compound : compound_params) {
      TestConvolve(compound);
    }
  }

 public:
  void SpeedTest() {
    auto compound_params = GetCompoundParams();
    for (const auto &compound : compound_params) {
      SpeedTestConvolve(compound);
    }
  }

 private:
  void SpeedTestConvolve(const CompoundParam &compound) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const int bit_depth = GetParam().BitDepth();
    int nob = 100000;

    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, conv_buf[MAX_SB_SQUARE]);
    highbd_compound_conv_2d_copy_func test_func = GetParam().TestFunction();

    ConvolveParams conv_params =
        GetConvolveParams(0, conv_buf, kOutputStride, bit_depth, compound);
    ConvolveParams conv_params_do_avg =
        GetConvolveParams(1, conv_buf, kOutputStride, bit_depth, compound);

    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < nob; i++) {
      av2_highbd_cwp_convolve_2d_copy_c(input, width, conv_buf, kOutputStride,
                                        width, height, &conv_params, bit_depth);
      av2_highbd_cwp_convolve_2d_copy_c(input, width, conv_buf, kOutputStride,
                                        width, height, &conv_params_do_avg,
                                        bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));

    avm_usec_timer timer1;
    avm_usec_timer_start(&timer1);
    for (int i = 0; i < nob; i++) {
      test_func(input, width, conv_buf, kOutputStride, width, height,
                &conv_params, bit_depth);
      test_func(input, width, conv_buf, kOutputStride, width, height,
                &conv_params_do_avg, bit_depth);
    }
    avm_usec_timer_mark(&timer1);
    const int elapsed_time1 = static_cast<int>(avm_usec_timer_elapsed(&timer1));
    printf("%d x %d block: bd: %d, Scaling = %.2f\n", width, height, bit_depth,
           (double)elapsed_time / elapsed_time1);
  }

 private:
  void TestConvolve(const CompoundParam &compound) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();

    const uint16_t *input1 = FirstRandomInput12(GetParam());
    const uint16_t *input2 = SecondRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, reference_conv_buf[MAX_SB_SQUARE]);
    Convolve(av2_highbd_cwp_convolve_2d_copy_c, input1, input2, reference,
             reference_conv_buf, compound);

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, test_conv_buf[MAX_SB_SQUARE]);
    Convolve(GetParam().TestFunction(), input1, input2, test, test_conv_buf,
             compound);

    AssertOutputBufferEq(reference_conv_buf, test_conv_buf, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void Convolve(highbd_compound_conv_2d_copy_func test_func,
                const uint16_t *src1, const uint16_t *src2, uint16_t *dst,
                uint16_t *conv_buf, const CompoundParam &compound) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const int bit_depth = GetParam().BitDepth();

    ConvolveParams conv_params =
        GetConvolveParams(0, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src1, width, dst, kOutputStride, width, height, &conv_params,
              bit_depth);

    conv_params =
        GetConvolveParams(1, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src2, width, dst, kOutputStride, width, height, &conv_params,
              bit_depth);
  }
};

TEST_P(AV2Convolve2DCopyHighbdCompoundTest, RunTest) { RunTest(); }
TEST_P(AV2Convolve2DCopyHighbdCompoundTest, DISABLED_SpeedTest) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, AV2Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_copy_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_copy_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_copy_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_copy_neon));
#endif

//////////////////////////////////////////////////
// Compound convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////

class AV2Convolve2DHighbdCompoundTestLarge
    : public AV2ConvolveTest<highbd_convolve_2d_func> {
 public:
  void RunTest() {
    auto compound_params = GetCompoundParams();
    for (int h_f = EIGHTTAP_REGULAR; h_f < INTERP_FILTERS_ALL; ++h_f) {
      for (int v_f = EIGHTTAP_REGULAR; v_f < INTERP_FILTERS_ALL; ++v_f) {
        for (int sub_x = 0; sub_x < 16; ++sub_x) {
          for (int sub_y = 0; sub_y < 16; ++sub_y) {
            for (const auto &compound : compound_params) {
              TestConvolve(static_cast<InterpFilter>(h_f),
                           static_cast<InterpFilter>(v_f), sub_x, sub_y,
                           compound);
            }
          }
        }
      }
    }
  }
  void SpeedTest() {
    auto compound_params = GetCompoundParams();
    for (int h_f = EIGHTTAP_REGULAR; h_f < INTERP_FILTERS_ALL; ++h_f) {
      for (int v_f = EIGHTTAP_REGULAR; v_f < INTERP_FILTERS_ALL; ++v_f) {
        if (h_f != v_f) continue;
        ConvolveSpeed(static_cast<InterpFilter>(h_f),
                      static_cast<InterpFilter>(v_f), 10, 10,
                      compound_params.front());
      }
    }
  }

 private:
  void TestConvolve(const InterpFilter h_f, const InterpFilter v_f,
                    const int sub_x, const int sub_y,
                    const CompoundParam &compound) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const uint16_t *input1 = FirstRandomInput12(GetParam());
    const uint16_t *input2 = SecondRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, reference_conv_buf[MAX_SB_SQUARE]);
    Convolve(av2_highbd_cwp_convolve_2d_c, input1, input2, reference,
             reference_conv_buf, compound, h_f, v_f, sub_x, sub_y);

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, test_conv_buf[MAX_SB_SQUARE]);
    Convolve(GetParam().TestFunction(), input1, input2, test, test_conv_buf,
             compound, h_f, v_f, sub_x, sub_y);

    AssertOutputBufferEq(reference_conv_buf, test_conv_buf, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }

  void ConvolveSpeed(const InterpFilter h_f, const InterpFilter v_f,
                     const int sub_x, const int sub_y,
                     const CompoundParam &compound) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const uint16_t *input1 = FirstRandomInput12(GetParam());

    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, reference_conv_buf[MAX_SB_SQUARE]);

    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(v_f, height);
    const int bit_depth = GetParam().BitDepth();

    ConvolveParams conv_params_ref1 = GetConvolveParams(
        0, reference_conv_buf, kOutputStride, bit_depth, compound);
    ConvolveParams conv_params_ref2 = GetConvolveParams(
        1, reference_conv_buf, kOutputStride, bit_depth, compound);

    const int num_iters = 100000000 / (width * height);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      av2_highbd_cwp_convolve_2d_c(input1, width, reference, kOutputStride,
                                   width, height, filter_params_x,
                                   filter_params_y, sub_x, sub_y,
                                   &conv_params_ref1, bit_depth);
      av2_highbd_cwp_convolve_2d_c(input1, width, reference, kOutputStride,
                                   width, height, filter_params_x,
                                   filter_params_y, sub_x, sub_y,
                                   &conv_params_ref2, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time1 = static_cast<int>(avm_usec_timer_elapsed(&timer));

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, test_conv_buf[MAX_SB_SQUARE]);
    ConvolveParams conv_params_mod1 =
        GetConvolveParams(0, test_conv_buf, kOutputStride, bit_depth, compound);
    ConvolveParams conv_params_mod2 =
        GetConvolveParams(1, test_conv_buf, kOutputStride, bit_depth, compound);

    avm_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      GetParam().TestFunction()(input1, width, test, kOutputStride, width,
                                height, filter_params_x, filter_params_y, sub_x,
                                sub_y, &conv_params_mod1, bit_depth);
      GetParam().TestFunction()(input1, width, test, kOutputStride, width,
                                height, filter_params_x, filter_params_y, sub_x,
                                sub_y, &conv_params_mod2, bit_depth);
    }
    avm_usec_timer_mark(&timer);
    const int time2 = static_cast<int>(avm_usec_timer_elapsed(&timer));
    printf("%d - %d %3dx%-3d: ref: %d mod: %d (%3.2f)\n", h_f, v_f, width,
           height, time1, time2, (double)time1 / time2);

    AssertOutputBufferEq(reference_conv_buf, test_conv_buf, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }

 private:
  void Convolve(highbd_convolve_2d_func test_func, const uint16_t *src1,
                const uint16_t *src2, uint16_t *dst, uint16_t *conv_buf,
                const CompoundParam &compound, const InterpFilter h_f,
                const InterpFilter v_f, const int sub_x, const int sub_y) {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();

    const InterpFilterParams *filter_params_x =
        av2_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av2_get_interp_filter_params_with_block_size(v_f, height);
    const int bit_depth = GetParam().BitDepth();
    ConvolveParams conv_params =
        GetConvolveParams(0, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src1, width, dst, kOutputStride, width, height, filter_params_x,
              filter_params_y, sub_x, sub_y, &conv_params, bit_depth);

    conv_params =
        GetConvolveParams(1, conv_buf, kOutputStride, bit_depth, compound);
    test_func(src2, width, dst, kOutputStride, width, height, filter_params_x,
              filter_params_y, sub_x, sub_y, &conv_params, bit_depth);
  }
};

TEST_P(AV2Convolve2DHighbdCompoundTestLarge, RunTest) { RunTest(); }
TEST_P(AV2Convolve2DHighbdCompoundTestLarge, DISABLED_Speed) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV2Convolve2DHighbdCompoundTestLarge,
                         BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2Convolve2DHighbdCompoundTestLarge,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2Convolve2DHighbdCompoundTestLarge,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2Convolve2DHighbdCompoundTestLarge,
    BuildHighbdLumaParams(av2_highbd_cwp_convolve_2d_neon));
#endif

//////////////////////////////////////////////////////////
// Nonseparable convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_nonsep_2d_func)(
    const uint16_t *src, int src_stride,
    const NonsepFilterConfig *filter_config, const int16_t *filter,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_row_end, int block_col_begin, int block_col_end);

class AV2ConvolveNonSep2DHighbdTest
    : public AV2ConvolveTest<highbd_convolve_nonsep_2d_func> {
 public:
  void RunTest(RestorationType rtype) {
    InitFilterConfigs(prec_bits_);
    for (int i = 0; i < kTestIterations; i++) {
      SetFilterTaps();
      TestConvolve(FilterTaps_, rtype);
    }
  }
  void RunSpeedTest(RestorationType rtype) {
    InitFilterConfigs(prec_bits_);
    SpeedTestConvolve(FilterTaps_, rtype);
  };

 protected:
  int prec_bits_ = 12;
  void BitMatchTest(const uint16_t *input, int input_stride, int width,
                    int height, const int16_t *filter, uint16_t *reference,
                    uint16_t *test, int dst_stride, int bit_depth,
                    int block_row_begin, int block_row_end, int block_col_begin,
                    int block_col_end, RestorationType rtype) {
    const NonsepFilterConfig *filter_config[2] = { NULL, NULL };
    highbd_convolve_nonsep_2d_func ref_func = av2_convolve_symmetric_highbd_c;
    const int num_planes = 2;

    if (rtype == RESTORE_PC_WIENER) {
      ref_func = av2_convolve_symmetric_highbd_c;
      filter_config[0] = &UnconstrainedSumFilterConfig_;
      filter_config[1] = &PcWienerNonsepFilterConfigChroma_;
    }

    // When CONFIG_WIENER_NONSEP=1, luma and chroma plane uses different number
    // of filter taps and both needs to be tested. Here, luma is tested for
    // 12/13-tap filtering whereas chroma is tested for 6-tap filtering.
    if (rtype == RESTORE_WIENER_NONSEP) {
      ref_func = av2_convolve_symmetric_subtract_center_highbd_c;
      filter_config[0] = &UnitSumFilterConfig_;
      filter_config[1] = &UnitSumFilterConfigChroma_;
    }

    assert(filter_config[0] != NULL && filter_config[1] != NULL);

    for (int plane = 0; plane < num_planes; plane++) {
      ref_func(input, input_stride, filter_config[plane], filter, reference,
               dst_stride, bit_depth, block_row_begin, block_row_end,
               block_col_begin, block_col_end);
      GetParam().TestFunction()(input, input_stride, filter_config[plane],
                                filter, test, dst_stride, bit_depth,
                                block_row_begin, block_row_end, block_col_begin,
                                block_col_end);
      AssertOutputBufferEq(reference, test, width, height);
    }
  }
  void TestConvolve(const int16_t *filter, RestorationType rtype) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();

    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);

    ASSERT_TRUE(kInputPadding >= kMaxTapOffset)
        << "Not enough padding for 7x7 filters";
    const int input_stride = width;
    BitMatchTest(input, input_stride, width, height, filter, reference, test,
                 kOutputStride, bit_depth, 0, height, 0, width, rtype);
    // Extreme value test
    const uint16_t *extreme_input = FirstRandomInput16Extreme(GetParam());
    int16_t Extream_Tap_[kMaxNumSymmetricTaps + 1];
    RandomizeExtreamFilterTap(Extream_Tap_, kMaxNumSymmetricTaps + 1,
                              prec_bits_);
    BitMatchTest(extreme_input, input_stride, width, height, Extream_Tap_,
                 reference, test, kOutputStride, bit_depth, 0, height, 0, width,
                 rtype);
  }

  void SpeedTestConvolve(const int16_t *filter, RestorationType rtype) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const int num_planes = 2;

    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);

    ASSERT_TRUE(kInputPadding >= kMaxTapOffset)
        << "Not enough padding for 7x7 filters";

    // Calculate time taken for C function
    const NonsepFilterConfig *filter_config[2] = { NULL, NULL };
    highbd_convolve_nonsep_2d_func ref_func = av2_convolve_symmetric_highbd_c;

    if (rtype == RESTORE_PC_WIENER) {
      ref_func = av2_convolve_symmetric_highbd_c;
      filter_config[0] = &UnconstrainedSumFilterConfig_;
      filter_config[1] = &PcWienerNonsepFilterConfigChroma_;
    }

    // When CONFIG_WIENER_NONSEP=1, luma and chroma uses different number of
    // filter taps and both needs to be tested. Here, luma is tested for
    // 12/13-tap filtering whereas chroma is tested for 6-tap filtering.
    if (rtype == RESTORE_WIENER_NONSEP) {
      ref_func = av2_convolve_symmetric_subtract_center_highbd_c;
      filter_config[0] = &UnitSumFilterConfig_;
      filter_config[1] = &UnitSumFilterConfigChroma_;
    }

    for (int plane = 0; plane < num_planes; plane++) {
      // Calculate time taken by reference/c function
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        ref_func(input, width, filter_config[plane], filter, reference,
                 kOutputStride, bit_depth, 0, height, 0, width);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        GetParam().TestFunction()(input, width, filter_config[plane], filter,
                                  test, kOutputStride, bit_depth, 0, height, 0,
                                  width);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

      float c_time_per_pixel =
          (float)1000.0 * elapsed_time_c / (kSpeedIterations * width * height);
      float opt_time_per_pixel = (float)1000.0 * elapsed_time_opt /
                                 (kSpeedIterations * width * height);
      float scaling = c_time_per_pixel / opt_time_per_pixel;
      printf(
          "plane=%3d, %3dx%-3d: c_time_per_pixel=%10.5f, "
          "opt_time_per_pixel=%10.5f,  scaling=%f \n",
          plane, width, height, c_time_per_pixel, opt_time_per_pixel, scaling);
    }
  }

  // Generates NonsepFilterConfig compliant origin symmetric filter tap values.
  // The first (2 * kMaxNumSymmetricTaps) are for the CONFIG_WIENER_NONSEP use
  // case where the center tap is constrained so that filter sums to one. The
  // last added tap at (2 * kMaxNumSymmetricTaps) is unconstrained and intended
  // for CONFIG_PC_WIENER use case.
  void SetFilterTaps() {
    Randomize(FilterTaps_, kMaxNumSymmetricTaps + 1, prec_bits_);
  }

  // Fills the array p with signed integers.
  void Randomize(int16_t *p, int size, int max_bit_range) {
    ASSERT_TRUE(max_bit_range < 16) << "max_bit_range has to be less than 16";
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand15Signed() & ((1 << max_bit_range) - 1);
    }
  }

  // Fills the array p with maximum and minimum possible integers.
  void RandomizeExtreamFilterTap(int16_t *p, int size, int max_bit_range) {
    ASSERT_TRUE(max_bit_range < 16) << "max_bit_range has to be less than 16";
    const int sign_max_val = (1 << (max_bit_range - 1)) - 1;
    for (int i = 0; i < size; ++i) {
      p[i] =
          static_cast<int16_t>(RandBool() ? sign_max_val : -(sign_max_val + 1));
    }
  }

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  void InitFilterConfigs(int prec_bits) {
    UnconstrainedSumFilterConfig_ = { prec_bits,
                                      (int)(sizeof(wienerns_simd_config_y) /
                                            sizeof(wienerns_simd_config_y[0])),
                                      0,
                                      wienerns_simd_config_y,
                                      NULL,
                                      0,
                                      0,
                                      1,
                                      1 };
    PcWienerNonsepFilterConfigChroma_ = {
      prec_bits,
      (int)(sizeof(wienerns_simd_config_uv_from_uvonly) /
            sizeof(wienerns_simd_config_uv_from_uvonly[0])),
      0,
      wienerns_simd_config_uv_from_uvonly,
      NULL,
      0,
      0,
      1,
      1
    };
    UnitSumFilterConfig_ = { prec_bits,
                             (int)(sizeof(wienerns_simd_config_y) /
                                       sizeof(wienerns_simd_config_y[0]) -
                                   1),
                             0,
                             wienerns_simd_config_y,
                             NULL,
                             0,
                             1,
                             1,
                             1 };
    UnitSumFilterConfigChroma_ = {
      prec_bits,
      (int)(sizeof(wienerns_simd_config_uv_from_uv) /
                sizeof(wienerns_simd_config_uv_from_uv[0]) -
            1),
      0,
      wienerns_simd_config_uv_from_uv,
      NULL,
      0,
      1,
      1,
      1
    };
  }

  libavm_test::ACMRandom rnd_;
  static constexpr int kMaxNumSymmetricTaps = 18;
  static constexpr int kMaxTapOffset = 3;  // Filters are 7x7.
  static constexpr int kSpeedIterations = 10000;
  static constexpr int kTestIterations = 100;

  NonsepFilterConfig UnconstrainedSumFilterConfig_ = {};
  NonsepFilterConfig PcWienerNonsepFilterConfigChroma_ = {};
  NonsepFilterConfig UnitSumFilterConfig_ = {};
  NonsepFilterConfig UnitSumFilterConfigChroma_ = {};

  int16_t FilterTaps_[kMaxNumSymmetricTaps + 1];
};

TEST_P(AV2ConvolveNonSep2DHighbdTest, RunTest) { RunTest(RESTORE_PC_WIENER); }

TEST_P(AV2ConvolveNonSep2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(RESTORE_PC_WIENER);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveNonSep2DHighbdTest,
                         BuildHighbdParams(av2_convolve_symmetric_highbd_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveNonSep2DHighbdTest,
                         BuildHighbdParams(av2_convolve_symmetric_highbd_neon));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2ConvolveNonSep2DHighbdTest);

class AV2ConvolveWienerNonSep2DHighbdTest
    : public AV2ConvolveNonSep2DHighbdTest {};

TEST_P(AV2ConvolveWienerNonSep2DHighbdTest, RunTest) {
  RunTest(RESTORE_WIENER_NONSEP);
}
TEST_P(AV2ConvolveWienerNonSep2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(RESTORE_WIENER_NONSEP);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveWienerNonSep2DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_subtract_center_highbd_avx2));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveWienerNonSep2DHighbdTest);

class AV2ConvolveNonSepBlk8x82DHighbdTest
    : public AV2ConvolveNonSep2DHighbdTest {};

TEST_P(AV2ConvolveNonSepBlk8x82DHighbdTest, RunTest) {
  RunTest(RESTORE_PC_WIENER);
}
TEST_P(AV2ConvolveNonSepBlk8x82DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(RESTORE_PC_WIENER);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveNonSepBlk8x82DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_blk8x8_highbd_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveNonSepBlk8x82DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_blk8x8_highbd_neon));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveNonSepBlk8x82DHighbdTest);

class AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest
    : public AV2ConvolveNonSep2DHighbdTest {
 public:
  AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest() { prec_bits_ = 7; }
};

TEST_P(AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest, RunTest) {
  RunTest(RESTORE_PC_WIENER);
}

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest,
    BuildHighbdParams(av2_convolve_symmetric_blk8x8_highbd_neon));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest,
    BuildHighbdParams(av2_convolve_symmetric_blk8x8_highbd_avx2));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveNonSepBlk8x82DHighbdLowPrecTest);

//////////////////////////////////////////////////////////
// Nonseparable convolve-2d Dual functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_nonsep_dual_2d_func)(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end);

class AV2ConvolveNonSep_dual2DHighbdTest
    : public AV2ConvolveTest<highbd_convolve_nonsep_dual_2d_func> {
 public:
  void RunTest(int is_subtract_center) {
    InitDualFilterConfigs(prec_bits_);
    for (int i = 0; i < kTestIterations; i++) {
      SetFilterTaps();
      TestConvolve(FilterTaps_, is_subtract_center);
    }
  }
  void RunSpeedTest(int is_subtract_center) {
    InitDualFilterConfigs(prec_bits_);
    SpeedTestConvolve(FilterTaps_, is_subtract_center);
  };

 protected:
  int prec_bits_ = 12;
  libavm_test::ACMRandom rnd_;
  static constexpr int kNumSubtractCenterOffTaps = 20;
  static constexpr int kMaxTapOffset = 2;  // Filters are 5x5.
  static constexpr int kSpeedIterations = 10000;
  static constexpr int kTestIterations = 100;

  // Declare the filter taps for worst case (i.e., for subtract center off
  // case).
  int16_t FilterTaps_[kNumSubtractCenterOffTaps];

  // Fills the array p with signed integers.
  void Randomize(int16_t *p, int size, int max_bit_range) {
    ASSERT_TRUE(max_bit_range < 16) << "max_bit_range has to be less than 16";
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand15Signed() & ((1 << max_bit_range) - 1);
    }
  }

  void SetFilterTaps() {
    Randomize(FilterTaps_, kNumSubtractCenterOffTaps, prec_bits_);
  }

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  // Fills the array p with maximum and minimum possible integers.
  void RandomizeExtreamFilterTap(int16_t *p, int size, int max_bit_range) {
    ASSERT_TRUE(max_bit_range < 16) << "max_bit_range has to be less than 16";
    const int sign_max_val = (1 << (max_bit_range - 1)) - 1;
    for (int i = 0; i < size; ++i) {
      p[i] =
          static_cast<int16_t>(RandBool() ? sign_max_val : -(sign_max_val + 1));
    }
  }

  void BitMatchTest(const uint16_t *dgd, const uint16_t *dgd_dual,
                    int dgd_stride, int width, int height,
                    const int16_t *filter, uint16_t *reference, uint16_t *test,
                    int dst_stride, int bit_depth, int block_row_begin,
                    int block_row_end, int block_col_begin, int block_col_end,
                    int is_subtract_center) {
    // Set filter_config and reference function appropriately.
    highbd_convolve_nonsep_dual_2d_func ref_func;
    const NonsepFilterConfig *filter_cfg;

    filter_cfg = &DualFilterWithCenterConfig_;
    ref_func = av2_convolve_symmetric_dual_subtract_center_highbd_c;

    if (!is_subtract_center) {
      ref_func = av2_convolve_symmetric_dual_highbd_c;
      filter_cfg = &DualFilterWithoutCenterConfig_;
    }
    // Reference function
    ref_func(dgd, dgd_stride, dgd_dual, dgd_stride, filter_cfg, filter,
             reference, dst_stride, bit_depth, block_row_begin, block_row_end,
             block_col_begin, block_col_end);

    // Test function
    GetParam().TestFunction()(dgd, dgd_stride, dgd_dual, dgd_stride, filter_cfg,
                              filter, test, dst_stride, bit_depth,
                              block_row_begin, block_row_end, block_col_begin,
                              block_col_end);

    // Compare the output of reference and test for bit match
    AssertOutputBufferEq(reference, test, width, height);
  }

  void TestConvolve(const int16_t *filter, int is_subtract_center) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();

    const uint16_t *dgd = FirstRandomInput12(GetParam());
    const uint16_t *dgd_dual = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);

    ASSERT_TRUE(kInputPadding >= kMaxTapOffset)
        << "Not enough padding for 5x5 filters";
    const int input_stride = width;
    BitMatchTest(dgd, dgd_dual, input_stride, width, height, filter, reference,
                 test, kOutputStride, bit_depth, 0, height, 0, width,
                 is_subtract_center);
    // Extreme value test
    const uint16_t *extreme_input1 = FirstRandomInput16Extreme(GetParam());
    const uint16_t *extreme_input2 = FirstRandomInput16Extreme(GetParam());
    int16_t Extream_Tap_[kNumSubtractCenterOffTaps];
    RandomizeExtreamFilterTap(Extream_Tap_, kNumSubtractCenterOffTaps,
                              prec_bits_);
    BitMatchTest(extreme_input1, extreme_input2, input_stride, width, height,
                 Extream_Tap_, reference, test, kOutputStride, bit_depth, 0,
                 height, 0, width, is_subtract_center);
  }

  void SpeedTestConvolve(const int16_t *filter, int is_subtract_center) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();

    const uint16_t *dgd = FirstRandomInput12(GetParam());
    const uint16_t *dgd_dual = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);

    ASSERT_TRUE(kInputPadding >= kMaxTapOffset)
        << "Not enough padding for 5x5 filters";

    // Set filter_config and reference function appropriately.
    highbd_convolve_nonsep_dual_2d_func ref_func;
    const NonsepFilterConfig *filter_cfg;

    filter_cfg = &DualFilterWithCenterConfig_;
    ref_func = av2_convolve_symmetric_dual_subtract_center_highbd_c;

    if (!is_subtract_center) {
      ref_func = av2_convolve_symmetric_dual_highbd_c;
      filter_cfg = &DualFilterWithoutCenterConfig_;
    }

    // Calculate time taken by reference/c function
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      ref_func(dgd, width, dgd_dual, width, filter_cfg, filter, reference,
               kOutputStride, bit_depth, 0, height, 0, width);
    }
    avm_usec_timer_mark(&timer);
    auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

    // Calculate time taken by optimized/intrinsic function
    avm_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      GetParam().TestFunction()(dgd, width, dgd_dual, width, filter_cfg, filter,
                                test, kOutputStride, bit_depth, 0, height, 0,
                                width);
    }
    avm_usec_timer_mark(&timer);
    auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

    float c_time_per_pixel =
        (float)1000.0 * elapsed_time_c / (kSpeedIterations * width * height);
    float opt_time_per_pixel =
        (float)1000.0 * elapsed_time_opt / (kSpeedIterations * width * height);
    float scaling = c_time_per_pixel / opt_time_per_pixel;
    printf(
        " %3dx%-3d: c_time_per_pixel=%10.5f, "
        "opt_time_per_pixel=%10.5f,  scaling=%f \n",
        width, height, c_time_per_pixel, opt_time_per_pixel, scaling);
  }

  void InitDualFilterConfigs(int prec_bits) {
    DualFilterWithCenterConfig_ = {
      prec_bits,  // prec_bits;
      (int)(sizeof(wienerns_simd_config_uv_from_uv) /
                sizeof(wienerns_simd_config_uv_from_uv[0]) -
            1),  // num_pixels;
      (int)(sizeof(wienerns_simd_config_uv_from_y) /
                sizeof(wienerns_simd_config_uv_from_y[0]) -
            1),                         // num_pixels2
      wienerns_simd_config_uv_from_uv,  // config
      wienerns_simd_config_uv_from_y,   // config2
      0,                                // strict_bounds
      1,                                // subtract_center
      1,                                // symmetry config
      0,                                // symmetry config2
    };
    DualFilterWithoutCenterConfig_ = {
      prec_bits,  // prec_bits;
      (int)(sizeof(wienerns_simd_config_uv_from_uv) /
            sizeof(wienerns_simd_config_uv_from_uv[0])),  // num_pixels;
      (int)(sizeof(wienerns_simd_config_uv_from_y) /
            sizeof(wienerns_simd_config_uv_from_y[0])),  // num_pixels2
      wienerns_simd_config_uv_from_uv,                   // config
      wienerns_simd_config_uv_from_y,                    // config2
      0,                                                 // strict_bounds
      0,                                                 // subtract_center
      1,                                                 // symmetry config
      0,                                                 // symmetry config2
    };
  }

  NonsepFilterConfig DualFilterWithCenterConfig_ = {};
  NonsepFilterConfig DualFilterWithoutCenterConfig_ = {};
};

TEST_P(AV2ConvolveNonSep_dual2DHighbdTest, RunTest) { RunTest(1); }
TEST_P(AV2ConvolveNonSep_dual2DHighbdTest, DISABLED_Speed) { RunSpeedTest(1); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveNonSep_dual2DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_dual_subtract_center_highbd_avx2));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveNonSep_dual2DHighbdTest);

/* Dual with subtract center off unit-test*/
class AV2ConvolveDualWithoutsubtract2DHighbdTest
    : public AV2ConvolveNonSep_dual2DHighbdTest {};

TEST_P(AV2ConvolveDualWithoutsubtract2DHighbdTest, RunTest) { RunTest(0); }
TEST_P(AV2ConvolveDualWithoutsubtract2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(0);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveDualWithoutsubtract2DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_dual_highbd_avx2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveDualWithoutsubtract2DHighbdTest,
    BuildHighbdParams(av2_convolve_symmetric_dual_highbd_neon));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveDualWithoutsubtract2DHighbdTest);

class AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest
    : public AV2ConvolveNonSep_dual2DHighbdTest {
 public:
  AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest() { prec_bits_ = 7; }
};

TEST_P(AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest, RunTest) {
  RunTest(0);
}

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest,
    BuildHighbdParams(av2_convolve_symmetric_dual_highbd_neon));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest,
    BuildHighbdParams(av2_convolve_symmetric_dual_highbd_avx2));
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveDualWithoutsubtract2DHighbdLowPrecTest);

enum WnsConfigId {
  kWnsLargeY16,
  kWnsLargeY13,
  kWnsConfigY,
  kWnsUvOnly,
  kWnsDualUv,
};

const char *WnsConfigName(int cfg) {
  switch (cfg) {
    case kWnsLargeY16: return "large_config_y/16tap";
    case kWnsLargeY13: return "large_config_y/taps12-15zero";
    case kWnsConfigY: return "config_y";
    case kWnsUvOnly: return "config_uv_from_uvonly";
    case kWnsDualUv: return "uv_from_uv+uv_from_y";
    default: return "?";
  }
}

struct WnsKernel {
  const char *name;
  highbd_convolve_nonsep_2d_func ref;
  highbd_convolve_nonsep_2d_func tst;
  highbd_convolve_nonsep_dual_2d_func ref_dual;
  highbd_convolve_nonsep_dual_2d_func tst_dual;
};

struct WnsParam {
  WnsKernel kernel;
  int width;
  int height;
  int bd;
  int cfg;
  int prec_bits;
};

std::ostream &operator<<(std::ostream &os, const WnsParam &p) {
  return os << "WnsParam { " << p.kernel.name << " " << p.width << "x"
            << p.height << " bd:" << p.bd << " cfg:" << WnsConfigName(p.cfg)
            << " prec_bits:" << p.prec_bits << " }";
}

std::vector<WnsParam> WnsParams(const WnsKernel &kernel,
                                const std::vector<std::pair<int, int>> &sizes,
                                std::initializer_list<int> cfgs) {
  std::vector<WnsParam> result;
  for (const auto &size : sizes) {
    for (int bd : { 8, 10, 12 }) {
      for (int cfg : cfgs) {
        for (int prec_bits : { 7, 12 }) {
          result.push_back(
              { kernel, size.first, size.second, bd, cfg, prec_bits });
        }
      }
    }
  }
  return result;
}

// The blk4x4 and dual wrappers pass min(4, remaining) in each dimension.
std::vector<std::pair<int, int>> WnsSizesUpTo4x4() {
  std::vector<std::pair<int, int>> sizes;
  for (int w = 1; w <= 4; ++w) {
    for (int h = 1; h <= 4; ++h) sizes.push_back({ w, h });
  }
  return sizes;
}

// Plus widths reaching the generic 32/16/8-column NEON paths and scalar tail.
std::vector<std::pair<int, int>> WnsSizesSym() {
  std::vector<std::pair<int, int>> sizes = WnsSizesUpTo4x4();
  for (const auto &s : std::vector<std::pair<int, int>>{
           { 8, 8 }, { 12, 4 }, { 16, 4 }, { 32, 4 }, { 44, 2 }, { 7, 5 } }) {
    sizes.push_back(s);
  }
  return sizes;
}

// The single-class wrapper passes min(8, remaining) in each dimension.
std::vector<std::pair<int, int>> WnsSizesBlk8x8() {
  return { { 8, 8 }, { 8, 7 }, { 7, 8 }, { 8, 6 }, { 6, 8 }, { 8, 4 },
           { 4, 8 }, { 8, 2 }, { 2, 8 }, { 8, 1 }, { 1, 8 }, { 7, 7 },
           { 6, 6 }, { 5, 3 }, { 3, 5 }, { 1, 1 } };
}

std::vector<std::pair<int, int>> WnsSizesDual() {
  std::vector<std::pair<int, int>> sizes = WnsSizesUpTo4x4();
  for (const auto &s : std::vector<std::pair<int, int>>{
           { 8, 4 }, { 12, 3 }, { 9, 2 }, { 16, 16 } }) {
    sizes.push_back(s);
  }
  return sizes;
}

struct WnsLayout {
  int src_stride;
  int dst_stride;
  int dual_stride;
  int src_off;
  int dst_off;
  int dual_off;
  int row_begin;
  int col_begin;
  const char *label;
};

class AV2ConvolveNonSepSymTestBase : public ::testing::TestWithParam<WnsParam> {
 public:
  void SetUp() override {
    p_ = GetParam();
    rnd_.Reset(libavm_test::ACMRandom::DeterministicSeed());
    dual_ = p_.kernel.ref_dual != nullptr;
    mask_ = (1 << p_.bd) - 1;
    src_ = static_cast<uint16_t *>(avm_memalign(32, kSrcAlloc * sizeof(*src_)));
    dual_src_ = static_cast<uint16_t *>(
        avm_memalign(32, kSrcAlloc * sizeof(*dual_src_)));
    ref_dst_ = static_cast<uint16_t *>(
        avm_memalign(32, kDstAlloc * sizeof(*ref_dst_)));
    tst_dst_ = static_cast<uint16_t *>(
        avm_memalign(32, kDstAlloc * sizeof(*tst_dst_)));
    ASSERT_NE(src_, nullptr);
    ASSERT_NE(dual_src_, nullptr);
    ASSERT_NE(ref_dst_, nullptr);
    ASSERT_NE(tst_dst_, nullptr);
    // Only the tap window is read; fill the rest so a stray read is
    // deterministic.
    for (int i = 0; i < kSrcAlloc; ++i) {
      src_[i] = rnd_.Rand16() & mask_;
      dual_src_[i] = rnd_.Rand16() & mask_;
    }
    memset(filter_, 0, sizeof(filter_));
    InitConfig();
  }

  void TearDown() override {
    avm_free(src_);
    avm_free(dual_src_);
    avm_free(ref_dst_);
    avm_free(tst_dst_);
    libavm_test::ClearSystemState();
  }

 protected:
  static constexpr int kMaxW = 44;
  static constexpr int kMaxH = 16;
  static constexpr int kMaxBegin = 5;
  static constexpr int kMaxRadius = 4;
  static constexpr int kMaxStride = 128;
  static constexpr int kSrcAlloc =
      (kMaxBegin + kMaxH + 2 * kMaxRadius + 4) * kMaxStride + 64;
  static constexpr int kDstAlloc = (kMaxBegin + kMaxH + 1) * kMaxStride + 64;
  static constexpr int kNumRandomIter = 1000;
  static constexpr int kMaxTaps = 24;

  void SetMainConfig(const int (*config)[3], int num_pixels) {
    cfg_.config = config;
    cfg_.num_pixels = num_pixels;
  }

  void InitConfig() {
    cfg_ = {};
    cfg_.prec_bits = p_.prec_bits;
    switch (p_.cfg) {
      case kWnsLargeY16:
      case kWnsLargeY13:
        SetMainConfig(
            wienerns_simd_large_config_y,
            static_cast<int>(sizeof(wienerns_simd_large_config_y) /
                             sizeof(wienerns_simd_large_config_y[0])));
        break;
      case kWnsConfigY:
        SetMainConfig(wienerns_simd_config_y,
                      static_cast<int>(sizeof(wienerns_simd_config_y) /
                                       sizeof(wienerns_simd_config_y[0])));
        break;
      case kWnsUvOnly:
        SetMainConfig(
            wienerns_simd_config_uv_from_uvonly,
            static_cast<int>(sizeof(wienerns_simd_config_uv_from_uvonly) /
                             sizeof(wienerns_simd_config_uv_from_uvonly[0])));
        break;
      case kWnsDualUv:
        SetMainConfig(
            wienerns_simd_config_uv_from_uv,
            static_cast<int>(sizeof(wienerns_simd_config_uv_from_uv) /
                             sizeof(wienerns_simd_config_uv_from_uv[0])));
        cfg_.config2 = wienerns_simd_config_uv_from_y;
        cfg_.num_pixels2 =
            static_cast<int>(sizeof(wienerns_simd_config_uv_from_y) /
                             sizeof(wienerns_simd_config_uv_from_y[0]));
        // As the encoder holds it (wienerns_filter_uv): config2 asymmetric.
        cfg_.asymmetric2 = cfg_.num_pixels2 - 1;
        break;
      default: FAIL() << "unknown config " << p_.cfg;
    }
    ASSERT_EQ(dual_, p_.cfg == kWnsDualUv) << "config / kernel mismatch";
    radius_ = 0;
    n_filter_ = 0;
    for (int k = 0; k < cfg_.num_pixels; ++k) {
      radius_ = AVMMAX(radius_, abs(cfg_.config[k][NONSEP_ROW_ID]));
      radius_ = AVMMAX(radius_, abs(cfg_.config[k][NONSEP_COL_ID]));
      n_filter_ = AVMMAX(n_filter_, cfg_.config[k][NONSEP_BUF_POS] + 1);
    }
    for (int k = 0; k < cfg_.num_pixels2; ++k) {
      radius_ = AVMMAX(radius_, abs(cfg_.config2[k][NONSEP_ROW_ID]));
      radius_ = AVMMAX(radius_, abs(cfg_.config2[k][NONSEP_COL_ID]));
      n_filter_ = AVMMAX(n_filter_, cfg_.config2[k][NONSEP_BUF_POS] + 1);
    }
    ASSERT_LE(radius_, kMaxRadius);
    ASSERT_LE(n_filter_, kMaxTaps);
    center_ = cfg_.config[cfg_.num_pixels - 1][NONSEP_BUF_POS];
    center2_ = dual_ ? cfg_.config2[cfg_.num_pixels2 - 1][NONSEP_BUF_POS] : -1;
  }

  // Private copies make the NEON dispatch match by memcmp, not by pointer.
  void UseConfigCopies() {
    memcpy(config_copy_, cfg_.config,
           cfg_.num_pixels * sizeof(config_copy_[0]));
    cfg_.config = config_copy_;
    if (dual_) {
      memcpy(config2_copy_, cfg_.config2,
             cfg_.num_pixels2 * sizeof(config2_copy_[0]));
      cfg_.config2 = config2_copy_;
    }
  }

  bool IsCenter(int pos) const { return pos == center_ || pos == center2_; }

  // The prec_bits 7 envelope covers the codec's tap tables: wienerns [-24, 39],
  // pc-wiener [-48, 127] with center [-208, 6].
  int EnvMin(int pos) const {
    if (p_.prec_bits == 7) return IsCenter(pos) ? -512 : -128;
    return -2048;
  }
  int EnvMax(int pos) const {
    if (p_.prec_bits == 7) return IsCenter(pos) ? 511 : 127;
    return 2047;
  }

  const WienernsFilterParameters *CodecTable() const {
    if (p_.prec_bits != 7) return nullptr;
    if (p_.cfg == kWnsLargeY16 || p_.cfg == kWnsLargeY13) {
      return &wienerns_filter_y;
    }
    if (p_.cfg == kWnsDualUv) return &wienerns_filter_uv;
    return nullptr;
  }

  void CodecRange(int pos, int *lo, int *hi) const {
    const WienernsFilterParameters *t = CodecTable();
    if (t != nullptr && pos < t->ncoeffs) {
      *lo = t->coeffs[pos][WIENERNS_MIN_ID];
      *hi = *lo + (1 << t->coeffs[pos][WIENERNS_BIT_ID]) - 1;
    } else {
      *lo = EnvMin(pos);
      *hi = EnvMax(pos);
    }
  }

  int RandRange(int lo, int hi) { return lo + rnd_.PseudoUniform(hi - lo + 1); }

  // As adjust_filter_to_non_subtract_center() derives them.
  void DeriveCenters() {
    int sum = 0;
    for (int k = 0; k < cfg_.num_pixels - 1; ++k) {
      sum += filter_[cfg_.config[k][NONSEP_BUF_POS]];
    }
    filter_[center_] = static_cast<int16_t>(-sum);
    if (dual_) {
      int sum2 = 0;
      for (int k = 0; k < cfg_.num_pixels2 - 1; ++k) {
        sum2 += filter_[cfg_.config2[k][NONSEP_BUF_POS]];
      }
      filter_[center2_] = static_cast<int16_t>(-sum2);
    }
  }

  void FixupTaps() {
    if (p_.cfg == kWnsLargeY13) {
      for (int pos = 12; pos < 16; ++pos) filter_[pos] = 0;
    } else if (p_.cfg == kWnsLargeY16) {
      if (!filter_[12] && !filter_[13] && !filter_[14] && !filter_[15]) {
        filter_[12] = 1;
      }
    }
  }

  void RandomTaps(bool codec_range) {
    const bool use_table = codec_range && CodecTable() != nullptr;
    for (int pos = 0; pos < n_filter_; ++pos) {
      int lo, hi;
      if (use_table) {
        CodecRange(pos, &lo, &hi);
      } else {
        lo = EnvMin(pos);
        hi = EnvMax(pos);
      }
      filter_[pos] = static_cast<int16_t>(RandRange(lo, hi));
    }
    FixupTaps();
    if (use_table) DeriveCenters();
  }

  enum TapSet {
    kCodecMax,
    kCodecMin,
    kEnvMax,
    kEnvMin,
    kAltPosFirst,
    kAltNegFirst,
    kCodecAlt,
  };

  static const char *TapSetName(int s) {
    switch (s) {
      case kCodecMax: return "codec-max";
      case kCodecMin: return "codec-min";
      case kEnvMax: return "env-max";
      case kEnvMin: return "env-min";
      case kAltPosFirst: return "alt+-";
      case kAltNegFirst: return "alt-+";
      case kCodecAlt: return "codec-alt";
      default: return "?";
    }
  }

  void SetTaps(int set) {
    for (int pos = 0; pos < n_filter_; ++pos) {
      int lo, hi;
      CodecRange(pos, &lo, &hi);
      int v = 0;
      switch (set) {
        case kCodecMax: v = hi; break;
        case kCodecMin: v = lo; break;
        case kEnvMax: v = EnvMax(pos); break;
        case kEnvMin: v = EnvMin(pos); break;
        case kAltPosFirst:
          v = IsCenter(pos) ? EnvMax(pos)
                            : ((pos & 1) ? EnvMin(pos) : EnvMax(pos));
          break;
        case kAltNegFirst:
          v = IsCenter(pos) ? EnvMin(pos)
                            : ((pos & 1) ? EnvMax(pos) : EnvMin(pos));
          break;
        case kCodecAlt: v = (pos & 1) ? lo : hi; break;
        default: FAIL() << "unknown tap set " << set;
      }
      filter_[pos] = static_cast<int16_t>(v);
    }
    FixupTaps();
    if ((set == kCodecMax || set == kCodecMin || set == kCodecAlt) &&
        CodecTable() != nullptr) {
      DeriveCenters();
    }
  }

  int MinSrcStride(int col_begin) const {
    return col_begin + p_.width + 2 * radius_;
  }

  static int AlignUp(int v, int a) { return (v + a - 1) / a * a; }

  WnsLayout DefaultLayout(int row_begin = 0, int col_begin = 0) const {
    const int min_src = MinSrcStride(col_begin);
    return { AlignUp(min_src, 8) + 8,
             AlignUp(col_begin + p_.width, 8) + 16,
             AlignUp(min_src, 8) + 16,
             0,
             0,
             0,
             row_begin,
             col_begin,
             "default" };
  }

  uint16_t *SrcOrigin(uint16_t *buf, int stride, int off) const {
    return buf + AlignUp(kMaxRadius * stride + kMaxRadius, 16) + off;
  }
  uint16_t *Src(const WnsLayout &l) const {
    return SrcOrigin(src_, l.src_stride, l.src_off);
  }
  uint16_t *Dual(const WnsLayout &l) const {
    return SrcOrigin(dual_src_, l.dual_stride, l.dual_off);
  }
  int DstExtent(const WnsLayout &l) const {
    return l.dst_off + (l.row_begin + p_.height + 1) * l.dst_stride;
  }

  void CheckLayout(const WnsLayout &l) const {
    ASSERT_LE(l.row_begin, kMaxBegin);
    ASSERT_LE(l.col_begin, kMaxBegin);
    ASSERT_LE(p_.width, kMaxW);
    ASSERT_LE(p_.height, kMaxH);
    ASSERT_GE(l.src_stride, MinSrcStride(l.col_begin));
    ASSERT_LE(l.src_stride, kMaxStride);
    ASSERT_GE(l.dst_stride, l.col_begin + p_.width);
    ASSERT_LE(l.dst_stride, kMaxStride);
    ASSERT_LE(l.dual_stride, kMaxStride);
    ASSERT_GE(l.dual_stride, MinSrcStride(l.col_begin));
    const int last_src = AlignUp(kMaxRadius * kMaxStride + kMaxRadius, 16) + 3 +
                         (l.row_begin + p_.height + radius_) * kMaxStride +
                         kMaxStride;
    ASSERT_LE(last_src, kSrcAlloc);
    ASSERT_LE(DstExtent(l) + 3, kDstAlloc);
  }

  template <typename Gen>
  void FillWindow(uint16_t *origin, int stride, const WnsLayout &l, Gen gen) {
    for (int r = -radius_; r < l.row_begin + p_.height + radius_; ++r) {
      for (int c = -radius_; c < l.col_begin + p_.width + radius_; ++c) {
        origin[r * stride + c] = static_cast<uint16_t>(gen(r, c));
      }
    }
  }

  template <typename Gen>
  void Fill(const WnsLayout &l, Gen gen) {
    FillWindow(Src(l), l.src_stride, l, gen);
    if (dual_) FillWindow(Dual(l), l.dual_stride, l, gen);
  }

  void FillRandom(const WnsLayout &l) {
    Fill(l, [this](int, int) { return rnd_.Rand16() & mask_; });
  }

  void FillConstant(const WnsLayout &l, int v) {
    Fill(l, [v](int, int) { return v; });
  }

  void FillCheckerboard(const WnsLayout &l) {
    const int m = mask_;
    Fill(l, [m](int r, int c) { return ((r + c) & 1) ? m : 0; });
  }

  void FillAccumulatorPattern(const WnsLayout &l, bool want_positive) {
    FillConstant(l, 0);
    uint16_t *o = Src(l) + l.row_begin * l.src_stride + l.col_begin;
    for (int k = 0; k < cfg_.num_pixels; ++k) {
      const int pos = cfg_.config[k][NONSEP_BUF_POS];
      int w = filter_[pos];
      if (k == cfg_.num_pixels - 1) w += 1 << p_.prec_bits;
      const bool on = want_positive ? w > 0 : w < 0;
      o[cfg_.config[k][NONSEP_ROW_ID] * l.src_stride +
        cfg_.config[k][NONSEP_COL_ID]] = on ? mask_ : 0;
    }
    if (dual_) {
      uint16_t *o2 = Dual(l) + l.row_begin * l.dual_stride + l.col_begin;
      for (int k = 0; k < cfg_.num_pixels2; ++k) {
        const int w = filter_[cfg_.config2[k][NONSEP_BUF_POS]];
        const bool on = want_positive ? w > 0 : w < 0;
        o2[cfg_.config2[k][NONSEP_ROW_ID] * l.dual_stride +
           cfg_.config2[k][NONSEP_COL_ID]] = on ? mask_ : 0;
      }
    }
  }

  void CallRef(const WnsLayout &l, uint16_t *dst) {
    const int rb = l.row_begin, cb = l.col_begin;
    if (dual_) {
      p_.kernel.ref_dual(Src(l), l.src_stride, Dual(l), l.dual_stride, &cfg_,
                         filter_, dst + l.dst_off, l.dst_stride, p_.bd, rb,
                         rb + p_.height, cb, cb + p_.width);
    } else {
      p_.kernel.ref(Src(l), l.src_stride, &cfg_, filter_, dst + l.dst_off,
                    l.dst_stride, p_.bd, rb, rb + p_.height, cb, cb + p_.width);
    }
  }

  void CallTst(const WnsLayout &l, uint16_t *dst) {
    const int rb = l.row_begin, cb = l.col_begin;
    if (dual_) {
      p_.kernel.tst_dual(Src(l), l.src_stride, Dual(l), l.dual_stride, &cfg_,
                         filter_, dst + l.dst_off, l.dst_stride, p_.bd, rb,
                         rb + p_.height, cb, cb + p_.width);
    } else {
      p_.kernel.tst(Src(l), l.src_stride, &cfg_, filter_, dst + l.dst_off,
                    l.dst_stride, p_.bd, rb, rb + p_.height, cb, cb + p_.width);
    }
  }

  void RunComparison(const WnsLayout &l, int iter, const char *what) {
    ASSERT_NO_FATAL_FAILURE(CheckLayout(l));
    const int extent = DstExtent(l);
    for (int i = 0; i < extent; ++i) {
      ref_dst_[i] = tst_dst_[i] = static_cast<uint16_t>(0xA5A5 + i);
    }
    CallRef(l, ref_dst_);
    ASM_REGISTER_STATE_CHECK(CallTst(l, tst_dst_));
    for (int i = 0; i < extent; ++i) {
      if (ref_dst_[i] == tst_dst_[i]) continue;
      const int rel = i - l.dst_off;
      std::string taps;
      for (int pos = 0; pos < n_filter_; ++pos) {
        taps += std::to_string(filter_[pos]) + (pos + 1 < n_filter_ ? "," : "");
      }
      ASSERT_EQ(ref_dst_[i], tst_dst_[i])
          << "mismatch at r=" << (rel >= 0 ? rel / l.dst_stride : -1)
          << " c=" << (rel >= 0 ? rel % l.dst_stride : rel) << " (block rows "
          << l.row_begin << ".." << l.row_begin + p_.height << ", cols "
          << l.col_begin << ".." << l.col_begin + p_.width << ") w=" << p_.width
          << " h=" << p_.height << " bd=" << p_.bd << " iter=" << iter
          << " kernel=" << p_.kernel.name << " cfg=" << WnsConfigName(p_.cfg)
          << " prec_bits=" << p_.prec_bits << " case=" << what
          << " layout=" << l.label << " src_stride=" << l.src_stride
          << " dst_stride=" << l.dst_stride << " dual_stride=" << l.dual_stride
          << " src_off=" << l.src_off << " dst_off=" << l.dst_off
          << " dual_off=" << l.dual_off << " taps=" << taps;
    }
  }

  void RunRandomBitExact() {
    const WnsLayout l = DefaultLayout();
    for (int i = 0; i < kNumRandomIter; ++i) {
      FillRandom(l);
      RandomTaps((i & 1) == 0);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, i, "random"));
    }
  }

  void RunConstantInput(int value, const char *what) {
    const WnsLayout l = DefaultLayout();
    FillConstant(l, value);
    for (int i = 0; i < 4; ++i) {
      RandomTaps((i & 1) == 0);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, i, what));
    }
    for (int set : { kCodecMax, kCodecMin }) {
      SetTaps(set);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, set, what));
    }
  }

  void RunExtremesAllZero() { RunConstantInput(0, "all-zero"); }
  void RunExtremesAllMax() { RunConstantInput(mask_, "all-max"); }
  void RunBitDepthMidpoint() { RunConstantInput(1 << (p_.bd - 1), "midpoint"); }

  void RunBitDepthCheckerboard() {
    const WnsLayout l = DefaultLayout();
    FillCheckerboard(l);
    for (int i = 0; i < 4; ++i) {
      RandomTaps((i & 1) == 0);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, i, "checkerboard"));
    }
    for (int set : { kCodecMax, kCodecMin, kCodecAlt }) {
      SetTaps(set);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, set, "checkerboard"));
    }
  }

  void RunTapSets(std::initializer_list<int> sets) {
    const WnsLayout l = DefaultLayout();
    for (int set : sets) {
      for (int input = 0; input < 3; ++input) {
        if (input == 0) FillRandom(l);
        if (input == 1) FillConstant(l, mask_);
        if (input == 2) FillCheckerboard(l);
        SetTaps(set);
        const std::string what =
            std::string(TapSetName(set)) + (input == 0   ? "/random"
                                            : input == 1 ? "/all-max"
                                                         : "/checkerboard");
        ASSERT_NO_FATAL_FAILURE(RunComparison(l, input, what.c_str()));
      }
    }
  }

  void RunExtremeTaps() {
    RunTapSets({ kCodecMax, kCodecMin, kEnvMax, kEnvMin });
  }

  void RunAlternatingSignTaps() {
    RunTapSets({ kAltPosFirst, kAltNegFirst, kCodecAlt });
  }

  // At bd 12 / prec_bits 12 this is the int32 stress case.
  void RunAccumulatorStress() {
    const WnsLayout l = DefaultLayout();
    FillConstant(l, mask_);
    SetTaps(kEnvMax);
    ASSERT_NO_FATAL_FAILURE(RunComparison(l, 0, "acc/env-max/all-max"));
    SetTaps(kEnvMin);
    ASSERT_NO_FATAL_FAILURE(RunComparison(l, 1, "acc/env-min/all-max"));
    for (int set : { kAltPosFirst, kAltNegFirst, kCodecAlt }) {
      SetTaps(set);
      for (bool positive : { true, false }) {
        FillAccumulatorPattern(l, positive);
        const std::string what = std::string("acc/") + TapSetName(set) +
                                 (positive ? "/positive" : "/negative");
        ASSERT_NO_FATAL_FAILURE(RunComparison(l, set, what.c_str()));
      }
    }
  }

  // The NEON 16-bit path accepts max(pos, neg) x max_pixel <= INT16_MAX. These
  // filters sit at the largest accepted sum s and just above it, all taps of
  // one sign. All-max pixels hide a wrapped positive R (it still clips to max),
  // so the positive cases also lower one center pixel to max_pixel / 4. s + 1
  // (center tap 1) and s + 2 (pairs only) are the smallest rejected sums.
  void RunS16AccumulatorBoundary() {
    const WnsLayout l = DefaultLayout();
    std::set<int> pair_positions;
    for (int k = 0; k < cfg_.num_pixels - 1; ++k) {
      const int pos = cfg_.config[k][NONSEP_BUF_POS];
      if (p_.cfg == kWnsLargeY13 && pos >= 12 && pos < 16) continue;
      pair_positions.insert(pos);
    }
    // Highest positions first, so the 16-tap config gets taps 12-15 nonzero.
    const std::vector<int> order(pair_positions.rbegin(),
                                 pair_positions.rend());
    const int largest_accepted = INT16_MAX / mask_;
    auto set_filter = [&](int s, int sign) {
      memset(filter_, 0, sizeof(filter_));
      filter_[center_] = static_cast<int16_t>(sign * (s & 1));
      for (int i = 0; i < s / 2; ++i) {
        int16_t &tap = filter_[order[i % order.size()]];
        tap = static_cast<int16_t>(tap + sign);
      }
      int sum = 0;
      for (int k = 0; k < cfg_.num_pixels; ++k) {
        sum += filter_[cfg_.config[k][NONSEP_BUF_POS]];
      }
      ASSERT_EQ(sum, sign * s);
      if (p_.cfg == kWnsLargeY16) {
        ASSERT_TRUE(filter_[12] || filter_[13] || filter_[14] || filter_[15]);
      }
    };
    for (int s : { largest_accepted, largest_accepted + 1 }) {
      for (int sign : { 1, -1 }) {
        ASSERT_NO_FATAL_FAILURE(set_filter(s, sign));
        FillConstant(l, mask_);
        const std::string what =
            std::string("s16-bound/") +
            (s == largest_accepted ? "accepted" : "rejected") +
            (sign > 0 ? "/positive" : "/negative");
        ASSERT_NO_FATAL_FAILURE(RunComparison(l, s, what.c_str()));
      }
    }
    uint16_t *const o = Src(l) + l.row_begin * l.src_stride + l.col_begin;
    const int low_at[2] = { 0, (p_.height - 1) * l.src_stride + p_.width - 1 };
    for (int s :
         { largest_accepted, largest_accepted + 1, largest_accepted + 2 }) {
      ASSERT_NO_FATAL_FAILURE(set_filter(s, 1));
      for (int at : low_at) {
        FillConstant(l, mask_);
        o[at] = static_cast<uint16_t>(mask_ >> 2);
        const std::string what =
            std::string("s16-bound/") +
            (s == largest_accepted ? "accepted" : "rejected") +
            "/positive/low-center" + (s & 1 ? "/center-tap" : "/pairs-only") +
            (at == 0 ? "/first" : "/last");
        ASSERT_NO_FATAL_FAILURE(RunComparison(l, s, what.c_str()));
      }
    }
    // One pair position at a time puts a sign's whole weight in one chain, so a
    // rejected sum overflows a chain whatever the kernel's chain split.
    for (int pos : order) {
      for (int extra : { 0, 1, 2 }) {
        for (int sign : { 1, -1 }) {
          const int s = largest_accepted + extra;
          memset(filter_, 0, sizeof(filter_));
          filter_[center_] = static_cast<int16_t>(sign * (s & 1));
          filter_[pos] = static_cast<int16_t>(sign * (s / 2));
          FillConstant(l, mask_);
          const std::string what = std::string("s16-bound/") +
                                   (extra == 0 ? "accepted" : "rejected") +
                                   (sign > 0 ? "/positive" : "/negative") +
                                   "/one-pair/" + std::to_string(pos) +
                                   (s & 1 ? "/center-tap" : "/pairs-only");
          ASSERT_NO_FATAL_FAILURE(RunComparison(l, s, what.c_str()));
        }
      }
    }
  }

  void RunLayout(const WnsLayout &l, int iters) {
    for (int i = 0; i < iters; ++i) {
      FillRandom(l);
      RandomTaps((i & 1) == 0);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, i, "random"));
    }
  }

  void RunStrideMinimum() {
    const int min_src = MinSrcStride(0);
    RunLayout({ min_src, p_.width, min_src, 0, 0, 0, 0, 0, "stride-min" }, 20);
  }

  void RunStridePadded() {
    const int min_src = MinSrcStride(0);
    ASSERT_NO_FATAL_FAILURE(
        RunLayout({ min_src + 16, p_.width + 16, min_src + 24, 0, 0, 0, 0, 0,
                    "stride-padded" },
                  20));
    ASSERT_NO_FATAL_FAILURE(
        RunLayout({ min_src + 17, p_.width + 19, min_src + 21, 0, 0, 0, 0, 0,
                    "stride-padded-odd" },
                  20));
  }

  void RunOffsets(int src_off, int dst_off, int dual_off, const char *label) {
    WnsLayout l = DefaultLayout();
    l.src_off = src_off;
    l.dst_off = dst_off;
    l.dual_off = dual_off;
    l.label = label;
    RunLayout(l, 20);
  }

  void RunMisalignedSrc() {
    for (int off : { 1, 3 }) {
      SCOPED_TRACE(off);
      ASSERT_NO_FATAL_FAILURE(RunOffsets(off, 0, 0, "src-misaligned"));
    }
  }
  void RunMisalignedDst() {
    for (int off : { 1, 3 }) {
      SCOPED_TRACE(off);
      ASSERT_NO_FATAL_FAILURE(RunOffsets(0, off, 0, "dst-misaligned"));
    }
  }
  void RunMisalignedSrcAndDst() {
    ASSERT_NO_FATAL_FAILURE(RunOffsets(1, 3, 0, "src+dst-misaligned"));
    ASSERT_NO_FATAL_FAILURE(RunOffsets(3, 1, 0, "src+dst-misaligned"));
  }
  void RunMisalignedDual() {
    for (int off : { 1, 3 }) {
      SCOPED_TRACE(off);
      ASSERT_NO_FATAL_FAILURE(RunOffsets(0, 0, off, "dual-misaligned"));
    }
  }
  void RunMisalignedAll() {
    ASSERT_NO_FATAL_FAILURE(RunOffsets(1, 3, 3, "src+dst+dual-misaligned"));
    ASSERT_NO_FATAL_FAILURE(RunOffsets(3, 1, 1, "src+dst+dual-misaligned"));
    ASSERT_NO_FATAL_FAILURE(RunOffsets(1, 0, 3, "src+dual-misaligned"));
  }

  // As the pc-wiener loop passes them (kernels index by absolute row/column).
  void RunPartialBlockOffsets() {
    static const int kBegins[][2] = { { 0, 1 }, { 1, 0 }, { 2, 3 },
                                      { 3, 5 }, { 5, 2 }, { 4, 4 } };
    for (const auto &b : kBegins) {
      WnsLayout l = DefaultLayout(b[0], b[1]);
      l.label = "begin-offset";
      ASSERT_NO_FATAL_FAILURE(RunLayout(l, 50));
    }
  }

  void RunConfigByContents() {
    UseConfigCopies();
    WnsLayout l = DefaultLayout();
    l.label = "config-by-contents";
    RunLayout(l, 200);
  }

  // Drops the center row: the shortened config is a prefix of the SIMD table,
  // but C then applies no center tap, so the old center tap must be ignored.
  void RunNoCenterTap(bool by_contents) {
    if (by_contents) UseConfigCopies();
    --cfg_.num_pixels;
    if (dual_) --cfg_.num_pixels2;
    center_ = center2_ = -1;
    const WnsLayout l = DefaultLayout();
    for (int i = 0; i < 200; ++i) {
      FillRandom(l);
      RandomTaps(false);
      ASSERT_NO_FATAL_FAILURE(RunComparison(l, i, "no-center-tap"));
    }
  }

  void RunSpeed() {
    const WnsLayout l = DefaultLayout();
    FillRandom(l);
    RandomTaps(true);
    const int iters = AVMMAX(10000, 4000000 / (p_.width * p_.height));
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < iters; ++i) CallRef(l, ref_dst_);
    avm_usec_timer_mark(&timer);
    const double t_ref = static_cast<double>(avm_usec_timer_elapsed(&timer));
    avm_usec_timer_start(&timer);
    for (int i = 0; i < iters; ++i) CallTst(l, tst_dst_);
    avm_usec_timer_mark(&timer);
    const double t_tst = static_cast<double>(avm_usec_timer_elapsed(&timer));
    printf(
        "%s %s prec_bits=%d %dx%d bd=%d: C=%7.1fms SIMD=%7.1fms "
        "gain=%.2fx\n",
        p_.kernel.name, WnsConfigName(p_.cfg), p_.prec_bits, p_.width,
        p_.height, p_.bd, t_ref / 1000.0, t_tst / 1000.0,
        t_tst > 0 ? t_ref / t_tst : 0.0);
  }

  WnsParam p_;
  libavm_test::ACMRandom rnd_;
  bool dual_ = false;
  int mask_ = 0;
  int radius_ = 0;
  int n_filter_ = 0;
  int center_ = -1;
  int center2_ = -1;
  NonsepFilterConfig cfg_ = {};
  int config_copy_[33][3];
  int config2_copy_[13][3];
  int16_t filter_[kMaxTaps];
  uint16_t *src_ = nullptr;
  uint16_t *dual_src_ = nullptr;
  uint16_t *ref_dst_ = nullptr;
  uint16_t *tst_dst_ = nullptr;
};

#define WNS_SYM_TESTS(Suite)                                         \
  TEST_P(Suite, RandomBitExact) { RunRandomBitExact(); }             \
  TEST_P(Suite, ExtremesAllZero) { RunExtremesAllZero(); }           \
  TEST_P(Suite, ExtremesAllMax) { RunExtremesAllMax(); }             \
  TEST_P(Suite, BitDepthMidpoint) { RunBitDepthMidpoint(); }         \
  TEST_P(Suite, BitDepthCheckerboard) { RunBitDepthCheckerboard(); } \
  TEST_P(Suite, ExtremeTaps) { RunExtremeTaps(); }                   \
  TEST_P(Suite, AlternatingSignTaps) { RunAlternatingSignTaps(); }   \
  TEST_P(Suite, AccumulatorStress) { RunAccumulatorStress(); }       \
  TEST_P(Suite, StrideMinimum) { RunStrideMinimum(); }               \
  TEST_P(Suite, StridePadded) { RunStridePadded(); }                 \
  TEST_P(Suite, MisalignedSrc) { RunMisalignedSrc(); }               \
  TEST_P(Suite, MisalignedDst) { RunMisalignedDst(); }               \
  TEST_P(Suite, MisalignedSrcAndDst) { RunMisalignedSrcAndDst(); }   \
  TEST_P(Suite, PartialBlockOffsets) { RunPartialBlockOffsets(); }   \
  TEST_P(Suite, ConfigByContents) { RunConfigByContents(); }         \
  TEST_P(Suite, NoCenterTap) { RunNoCenterTap(false); }              \
  TEST_P(Suite, NoCenterTapByContents) { RunNoCenterTap(true); }     \
  TEST_P(Suite, DISABLED_Speed) { RunSpeed(); }

class AV2ConvolveNonSepSymHighbdTest : public AV2ConvolveNonSepSymTestBase {};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2ConvolveNonSepSymHighbdTest);
WNS_SYM_TESTS(AV2ConvolveNonSepSymHighbdTest)
TEST_P(AV2ConvolveNonSepSymHighbdTest, S16AccumulatorBoundary) {
  RunS16AccumulatorBoundary();
}
// Random taps rarely pass the 16-bit check at bd 10 / 12; this drives the
// memcmp-matched routes into the 16-bit kernels.
TEST_P(AV2ConvolveNonSepSymHighbdTest, S16AccumulatorBoundaryByContents) {
  UseConfigCopies();
  RunS16AccumulatorBoundary();
}

class AV2ConvolveNonSepSymBlk8x8HighbdTest
    : public AV2ConvolveNonSepSymTestBase {};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveNonSepSymBlk8x8HighbdTest);
WNS_SYM_TESTS(AV2ConvolveNonSepSymBlk8x8HighbdTest)
TEST_P(AV2ConvolveNonSepSymBlk8x8HighbdTest, S16AccumulatorBoundary) {
  RunS16AccumulatorBoundary();
}
TEST_P(AV2ConvolveNonSepSymBlk8x8HighbdTest, S16AccumulatorBoundaryByContents) {
  UseConfigCopies();
  RunS16AccumulatorBoundary();
}

class AV2ConvolveNonSepSymDualHighbdTest : public AV2ConvolveNonSepSymTestBase {
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2ConvolveNonSepSymDualHighbdTest);
WNS_SYM_TESTS(AV2ConvolveNonSepSymDualHighbdTest)
TEST_P(AV2ConvolveNonSepSymDualHighbdTest, MisalignedDual) {
  RunMisalignedDual();
}
TEST_P(AV2ConvolveNonSepSymDualHighbdTest, MisalignedSrcDstAndDual) {
  RunMisalignedAll();
}

#undef WNS_SYM_TESTS

#define WNS_SYM_PARAMS(name, ref, tst)                               \
  ::testing::ValuesIn(                                               \
      WnsParams({ name, ref, tst, nullptr, nullptr }, WnsSizesSym(), \
                { kWnsLargeY16, kWnsLargeY13, kWnsConfigY, kWnsUvOnly }))
#define WNS_BLK8X8_PARAMS(name, ref, tst)                             \
  ::testing::ValuesIn(WnsParams({ name, ref, tst, nullptr, nullptr }, \
                                WnsSizesBlk8x8(),                     \
                                { kWnsLargeY16, kWnsLargeY13, kWnsConfigY }))
#define WNS_DUAL_PARAMS(name, ref, tst)                               \
  ::testing::ValuesIn(WnsParams({ name, nullptr, nullptr, ref, tst }, \
                                WnsSizesDual(), { kWnsDualUv }))

// Harness sanity baseline (C vs C); not SIMD coverage.
INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveNonSepSymHighbdTest,
                         WNS_SYM_PARAMS("sym_c",
                                        av2_convolve_symmetric_highbd_c,
                                        av2_convolve_symmetric_highbd_c));
INSTANTIATE_TEST_SUITE_P(
    C, AV2ConvolveNonSepSymBlk8x8HighbdTest,
    WNS_BLK8X8_PARAMS("blk8x8_c", av2_convolve_symmetric_blk8x8_highbd_c,
                      av2_convolve_symmetric_blk8x8_highbd_c));
INSTANTIATE_TEST_SUITE_P(C, AV2ConvolveNonSepSymDualHighbdTest,
                         WNS_DUAL_PARAMS("dual_c",
                                         av2_convolve_symmetric_dual_highbd_c,
                                         av2_convolve_symmetric_dual_highbd_c));

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2ConvolveNonSepSymHighbdTest,
                         WNS_SYM_PARAMS("sym_neon",
                                        av2_convolve_symmetric_highbd_c,
                                        av2_convolve_symmetric_highbd_neon));
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveNonSepSymBlk8x8HighbdTest,
    WNS_BLK8X8_PARAMS("blk8x8_neon", av2_convolve_symmetric_blk8x8_highbd_c,
                      av2_convolve_symmetric_blk8x8_highbd_neon));
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2ConvolveNonSepSymDualHighbdTest,
    WNS_DUAL_PARAMS("dual_neon", av2_convolve_symmetric_dual_highbd_c,
                    av2_convolve_symmetric_dual_highbd_neon));
#endif  // HAVE_NEON

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2ConvolveNonSepSymHighbdTest,
                         WNS_SYM_PARAMS("sym_avx2",
                                        av2_convolve_symmetric_highbd_c,
                                        av2_convolve_symmetric_highbd_avx2));
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveNonSepSymBlk8x8HighbdTest,
    WNS_BLK8X8_PARAMS("blk8x8_avx2", av2_convolve_symmetric_blk8x8_highbd_c,
                      av2_convolve_symmetric_blk8x8_highbd_avx2));
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2ConvolveNonSepSymDualHighbdTest,
    WNS_DUAL_PARAMS("dual_avx2", av2_convolve_symmetric_dual_highbd_c,
                    av2_convolve_symmetric_dual_highbd_avx2));
#endif  // HAVE_AVX2

#undef WNS_SYM_PARAMS
#undef WNS_BLK8X8_PARAMS
#undef WNS_DUAL_PARAMS

//////////////////////////////////////////////////////////
// Unit-test corresponds to buffer accumulations to derive filter
// index for each block size (pc_wiener_block_size: 4x4)
//////////////////////////////////////////////////////////

// Generate the list of all block widths / heights that need to be tested for
// pc_wiener.
template <typename T>
std::vector<TestParam<T>> GetPCWienerTestParams(
    std::initializer_list<int> bit_depths, T test_func) {
  std::set<BlockSize> sizes;
  for (int b = BLOCK_4X4; b < BLOCK_SIZES_ALL; ++b) {
    const int w = block_size_wide[b];
    const int h = block_size_high[b];
    if (w > RESTORATION_PROC_UNIT_SIZE || h > RESTORATION_PROC_UNIT_SIZE) {
      continue;
    }
    sizes.insert(BlockSize(w, h));
    // Add in smaller chroma sizes as well.
    if (w == 4 || h == 4) {
      sizes.insert(BlockSize(w / 2, h / 2));
    }
  }
  std::vector<TestParam<T>> result;
  for (const BlockSize &block : sizes) {
    for (int bd : bit_depths) {
      result.push_back(TestParam<T>(block, bd, test_func));
    }
  }
  return result;
}
template <typename T>
::testing::internal::ParamGenerator<TestParam<T>> BuildHighbdPCWienerParams(
    T test_func) {
  return ::testing::ValuesIn(GetPCWienerTestParams({ 10, 12 }, test_func));
}

typedef void (*fill_directional_feature_buffers_highbd_func)(
    int *feature_sum_buffers[], int16_t *feature_line_buffers[], int row,
    int buffer_row, const uint16_t *dgd, int dgd_stride, int width,
    int feature_lead, int feature_lag);

class AV2FillDirFeatureBufHighbdTest
    : public AV2ConvolveTest<fill_directional_feature_buffers_highbd_func> {
 public:
  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) {
      // Set buffer values here.
      SetBufferValues();
      TestConvolve();
    }
  }

  void RunSpeedTest() { SpeedTestConvolve(); };

 protected:
  virtual void SetUp() {
    for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
      feature_line_buffers_c_[j] = static_cast<int16_t *>(
          (avm_malloc(buffer_width_ * sizeof(*feature_line_buffers_c_[j]))));
      ASSERT_NE(feature_line_buffers_c_[j], nullptr);

      feature_line_buffers_simd_[j] = static_cast<int16_t *>(
          (avm_malloc(buffer_width_ * sizeof(*feature_line_buffers_simd_[j]))));
      ASSERT_NE(feature_line_buffers_simd_[j], nullptr);
    }

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      feature_sum_buffers_c_[j] = static_cast<int *>(
          (avm_malloc(buffer_width_ * sizeof(*feature_sum_buffers_c_[j]))));
      ASSERT_NE(feature_sum_buffers_c_[j], nullptr);

      feature_sum_buffers_simd_[j] = static_cast<int *>(
          (avm_malloc(buffer_width_ * sizeof(*feature_sum_buffers_simd_[j]))));
      ASSERT_NE(feature_sum_buffers_simd_[j], nullptr);
    }
  }

  virtual void TearDown() {
    for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
      avm_free(feature_line_buffers_c_[j]);
      feature_line_buffers_c_[j] = NULL;
      avm_free(feature_line_buffers_simd_[j]);
      feature_line_buffers_simd_[j] = NULL;
    }

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      avm_free(feature_sum_buffers_c_[j]);
      feature_sum_buffers_c_[j] = NULL;
      avm_free(feature_sum_buffers_simd_[j]);
      feature_sum_buffers_simd_[j] = NULL;
    }
  }

  void SetBufferValues() {
    const int bitdepth = GetParam().BitDepth();
    for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
      Randomize(feature_line_buffers_c_[j], buffer_width_, bitdepth);
      memcpy(feature_line_buffers_simd_[j], feature_line_buffers_c_[j],
             buffer_width_ * sizeof(*feature_line_buffers_simd_[j]));
    }

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      RandomizeSigned31(feature_sum_buffers_c_[j], buffer_width_, 31);
      memcpy(feature_sum_buffers_simd_[j], feature_sum_buffers_c_[j],
             buffer_width_ * sizeof(*feature_sum_buffers_simd_[j]));
    }
  }

 private:
  libavm_test::ACMRandom rnd_;
  static constexpr int kSpeedIterations = 10000;
  static constexpr int kTestIterations = 100;

  void TestConvolve() {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    // Input buffer allocation.
    const uint16_t *input = FirstRandomInput12(GetParam());
    const int input_stride = width;

    // C function call
    for (int i = 0; i < height; ++i) {
      const int row_to_process = AVMMIN(i + feature_lag, height + 3 - 2);
      fill_directional_feature_buffers_highbd_c(
          feature_sum_buffers_c_, feature_line_buffers_c_, row_to_process,
          feature_length - 1, input, input_stride, width, feature_lead,
          feature_lag);
    }

    // SIMD function call
    for (int i = 0; i < height; ++i) {
      const int row_to_process = AVMMIN(i + feature_lag, height + 3 - 2);
      GetParam().TestFunction()(feature_sum_buffers_simd_,
                                feature_line_buffers_simd_, row_to_process,
                                feature_length - 1, input, input_stride, width,
                                feature_lead, feature_lag);
    }

    // Compare the outputs of C and SIMD
    for (int i = 0; i < NUM_PC_WIENER_FEATURES; i++) {
      int *c_buf = feature_sum_buffers_c_[i];
      int *simd_buf = feature_sum_buffers_simd_[i];
      for (int j = 0; j < buffer_width_; ++j) {
        ASSERT_EQ(c_buf[j], simd_buf[j])
            << "feature_buf=" << i << " Pixel mismatch at width (" << i << ")";
      }
    }
  }

  void SpeedTestConvolve() {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();

    // Input buffer allocation.
    const uint16_t *input = FirstRandomInput12(GetParam());
    const int input_stride = width;

    // Calculate time taken for C function
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      for (int i = 0; i < height; ++i) {
        const int row_to_process = AVMMIN(i + feature_lag, height + 3 - 2);
        fill_directional_feature_buffers_highbd_c(
            feature_sum_buffers_c_, feature_line_buffers_c_, row_to_process,
            feature_length - 1, input, input_stride, width, feature_lead,
            feature_lag);
      }
    }
    avm_usec_timer_mark(&timer);
    auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

    // Calculate time taken by optimized/intrinsic function
    avm_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      for (int i = 0; i < height; ++i) {
        const int row_to_process = AVMMIN(i + feature_lag, height + 3 - 2);
        GetParam().TestFunction()(feature_sum_buffers_simd_,
                                  feature_line_buffers_simd_, row_to_process,
                                  feature_length - 1, input, input_stride,
                                  width, feature_lead, feature_lag);
      }
    }
    avm_usec_timer_mark(&timer);
    auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

    float c_time_per_pixel =
        (float)1000.0 * elapsed_time_c / (kSpeedIterations * width * height);
    float opt_time_per_pixel =
        (float)1000.0 * elapsed_time_opt / (kSpeedIterations * width * height);
    float scaling = c_time_per_pixel / opt_time_per_pixel;
    printf(
        "%3dx%-3d: c_time_per_pixel=%10.5f, "
        "opt_time_per_pixel=%10.5f,  scaling=%f \n",
        width, height, c_time_per_pixel, opt_time_per_pixel, scaling);
  }

  // Fills the array p with signed integers.
  void Randomize(int16_t *p, int size, int max_bit_range) {
    ASSERT_TRUE(max_bit_range < 16) << "max_bit_range has to be less than 16";
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand15Signed() & ((1 << max_bit_range) - 1);
    }
  }

  // Fills the array p with signed integers of 31 bit range.
  void RandomizeSigned31(int *p, int size, uint32_t max_bit_range) {
    assert(max_bit_range <= 31);
    uint32_t mask = (uint32_t)(1 << max_bit_range) - 1;
    for (int i = 0; i < size; ++i) {
      p[i] = (int)(rnd_.Rand31() & mask);
    }
  }

  int *feature_sum_buffers_c_[NUM_PC_WIENER_FEATURES];
  int *feature_sum_buffers_simd_[NUM_PC_WIENER_FEATURES];
  int16_t *feature_line_buffers_c_[NUM_FEATURE_LINE_BUFFERS];
  int16_t *feature_line_buffers_simd_[NUM_FEATURE_LINE_BUFFERS];
  const int feature_lead = PC_WIENER_FEATURE_LEAD_LUMA;
  const int feature_lag = PC_WIENER_FEATURE_LAG_LUMA;
  const int feature_length = PC_WIENER_FEATURE_LENGTH_LUMA;
  const int buffer_width_ = MAX_SB_SIZE + kInputPadding;
};

TEST_P(AV2FillDirFeatureBufHighbdTest, RunTest) { RunTest(); }

TEST_P(AV2FillDirFeatureBufHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2FillDirFeatureBufHighbdTest,
    BuildHighbdPCWienerParams(fill_directional_feature_buffers_highbd_avx2));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2FillDirFeatureBufHighbdTest);

typedef void (*FillTSkipSumBufferFunc)(int row, const uint8_t *tskip,
                                       int tskip_stride,
                                       int8_t *tskip_sum_buffer, int width,
                                       int height, int tskip_lead,
                                       int tskip_lag, bool use_strict_bounds);

typedef std::tuple<const FillTSkipSumBufferFunc> AV2FillTSkipSumBufferFuncParam;

class AV2Fill_TSkip_Sum_BufferTest
    : public ::testing::TestWithParam<AV2FillTSkipSumBufferFuncParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) {
      TestTSkipSum();
    }
  }
  void RunSpeedTest() { SpeedTestTSkipSum(); };

 private:
  libavm_test::ACMRandom rnd_;
  FillTSkipSumBufferFunc target_func_;

  static constexpr int kSpeedIterations = 10000;
  static constexpr int kTestIterations = 100;
  static constexpr int kNumPlanes = 1;
  static constexpr int kWidth = RESTORATION_PROC_UNIT_SIZE;
  static constexpr int kHeight = RESTORATION_PROC_UNIT_SIZE;
  static constexpr int kInputWidth = MI_SIZE_64X64;
  static constexpr int kInputStride = MI_SIZE_64X64;
  static constexpr int kOutputWidth =
      (RESTORATION_PROC_UNIT_SIZE + PC_WIENER_FEATURE_LENGTH_LUMA - 1);

  uint8_t input_buffer_[MI_SIZE_64X64 * MI_SIZE_64X64];
  int8_t ref_buffer_[kOutputWidth];
  int8_t test_buffer_[kOutputWidth];
  const bool tskip_strict_ = true;

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  void TestTSkipSum() {
    for (int i = 0; i < kInputWidth * kInputStride; ++i) {
      input_buffer_[i] = static_cast<uint8_t>(RandBool() ? 1 : 0);
    }

    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int ss_y = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int plane_height = kHeight >> ss_y;
      const int tskip_lead = PC_WIENER_TSKIP_LEAD_LUMA;
      const int tskip_lag = PC_WIENER_TSKIP_LAG_LUMA;

      memset(ref_buffer_, 0, sizeof(*ref_buffer_) * kOutputWidth);
      memset(test_buffer_, 0, sizeof(*test_buffer_) * kOutputWidth);

      // Reference function
      for (int row = -tskip_lead; row < (tskip_lag + plane_height); ++row) {
        av2_fill_tskip_sum_buffer_c(row, input_buffer_, kInputStride,
                                    ref_buffer_, plane_width, plane_height,
                                    tskip_lead, tskip_lag, tskip_strict_);
      }

      // Test function
      for (int row = -tskip_lead; row < (tskip_lag + plane_height); ++row) {
        target_func_(row, input_buffer_, kInputStride, test_buffer_,
                     plane_width, plane_height, tskip_lead, tskip_lag,
                     tskip_strict_);
      }

      // Compare the output of reference and test for bit match
      for (int i = 0; i < kOutputWidth; ++i) {
        ASSERT_EQ(ref_buffer_[i], test_buffer_[i])
            << " Mismatch at (" << i << ")";
      }
    }
  }

  void SpeedTestTSkipSum() {
    for (int i = 0; i < kInputWidth * kInputStride; ++i) {
      input_buffer_[i] = static_cast<uint8_t>(RandBool() ? 1 : 0);
    }

    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int ss_y = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int plane_height = kHeight >> ss_y;
      const int tskip_lead = PC_WIENER_TSKIP_LEAD_LUMA;
      const int tskip_lag = PC_WIENER_TSKIP_LAG_LUMA;

      memset(ref_buffer_, 0, sizeof(*ref_buffer_) * kOutputWidth);
      memset(test_buffer_, 0, sizeof(*test_buffer_) * kOutputWidth);

      // Calculate time taken by reference/c function
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        // Reference function
        for (int row = -tskip_lead; row < (tskip_lag + plane_height - 1);
             ++row) {
          av2_fill_tskip_sum_buffer_c(row, input_buffer_, kInputStride,
                                      ref_buffer_, plane_width, plane_height,
                                      tskip_lead, tskip_lag, tskip_strict_);
        }
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        for (int row = -tskip_lead; row < (tskip_lag + plane_height - 1);
             ++row) {
          target_func_(row, input_buffer_, kInputStride, test_buffer_,
                       plane_width, plane_height, tskip_lead, tskip_lag,
                       tskip_strict_);
        }
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

      float c_time_per_pixel =
          (float)1000.0 * elapsed_time_c / kSpeedIterations;
      float opt_time_per_pixel =
          (float)1000.0 * elapsed_time_opt / kSpeedIterations;
      float scaling = c_time_per_pixel / opt_time_per_pixel;
      printf(
          " %3dx%-3d: c_time_per_pixel=%10.5f, "
          "opt_time_per_pixel=%10.5f,  scaling=%f \n",
          plane_width, plane_height, c_time_per_pixel, opt_time_per_pixel,
          scaling);
    }
  }
};

TEST_P(AV2Fill_TSkip_Sum_BufferTest, RunTest) { RunTest(); }
TEST_P(AV2Fill_TSkip_Sum_BufferTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2Fill_TSkip_Sum_BufferTest,
                         ::testing::Values(av2_fill_tskip_sum_buffer_avx2));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2Fill_TSkip_Sum_BufferTest);

//////////////////////////////////////////////////////////
//       unit-test for 'directional_feature_accum'      //
//////////////////////////////////////////////////////////
typedef void (*FillDirFeatureAccumFunc)(
    int dir_feature_accum[NUM_PC_WIENER_FEATURES][PC_WIENER_FEATURE_ACC_SIZE],
    int *feature_sum_buf[NUM_PC_WIENER_FEATURES], int width, int col_offset,
    int feature_lead, int feature_lag);

typedef std::tuple<const FillDirFeatureAccumFunc>
    AV2FillDirFeatureAccumFuncParam;

class AV2FeatureDirAccumHighbdTest
    : public ::testing::TestWithParam<AV2FillDirFeatureAccumFuncParam> {
 public:
  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) {
      FillInputBufs();
      TestFillDirFeatureAccum();
    }
  }

  void RunSpeedTest() { SpeedTestConvolve(); };

  virtual void SetUp() {
    target_func_ = GET_PARAM(0);

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      feature_sum_buf[j] =
          (int *)(avm_malloc(kInputWidth * sizeof(*feature_sum_buf[j])));
    }
  }

  virtual void TearDown() {
    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      avm_free(feature_sum_buf[j]);
      feature_sum_buf[j] = NULL;
    }
  }

 private:
  libavm_test::ACMRandom rnd_;
  FillDirFeatureAccumFunc target_func_;

  static constexpr int kSpeedIterations = 1000000;
  static constexpr int kTestIterations = 100;
  static constexpr int kNumPlanes = 2;
  static constexpr int kWidth = RESTORATION_PROC_UNIT_SIZE;
  static constexpr int kInputWidth =
      (RESTORATION_PROC_UNIT_SIZE + PC_WIENER_FEATURE_LENGTH_LUMA - 1);

  int *feature_sum_buf[NUM_PC_WIENER_FEATURES];
  int dir_feature_accum_buf_c[NUM_PC_WIENER_FEATURES]
                             [PC_WIENER_FEATURE_ACC_SIZE] = { { 0 } };
  int dir_feature_accum_buf_simd[NUM_PC_WIENER_FEATURES]
                                [PC_WIENER_FEATURE_ACC_SIZE] = { { 0 } };
  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  void FillInputBufs() {
    for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i) {
      for (int j = 0; j < kInputWidth; ++j) {
        // For the extreme values case, the maimum input that feature_sum_buf
        // can take is (kInputWidth * 2 * input_max_value). Hence, clipping the
        // value generated to 23 bit.
        const int max_range = (1 << 23);
        const int value = rnd_.Rand31() % max_range;
        feature_sum_buf[i][j] =
            static_cast<uint8_t>(RandBool() ? value : -value);
      }
    }
    // Reset output buffers
    av2_zero(dir_feature_accum_buf_c);
    av2_zero(dir_feature_accum_buf_simd);
  }

  void TestFillDirFeatureAccum() {
    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int feature_lead = PC_WIENER_FEATURE_LEAD_LUMA;
      const int feature_lag = PC_WIENER_FEATURE_LAG_LUMA;

      // Reset output buffers
      av2_zero(dir_feature_accum_buf_c);
      av2_zero(dir_feature_accum_buf_simd);

      // C function call
      av2_fill_directional_feature_accumulators_c(
          dir_feature_accum_buf_c, feature_sum_buf, plane_width, feature_lag,
          feature_lead, feature_lag);

      // SIMD function call
      target_func_(dir_feature_accum_buf_simd, feature_sum_buf, plane_width,
                   feature_lag, feature_lead, feature_lag);

      // Compare the output of reference and test for bit match
      for (int i = 0; i < NUM_PC_WIENER_FEATURES; i++) {
        for (int j = 0; j < PC_WIENER_FEATURE_ACC_SIZE; j++) {
          ASSERT_EQ(dir_feature_accum_buf_c[i][j],
                    dir_feature_accum_buf_simd[i][j])
              << " Feature_Buf: Pixel mismatch at (" << i << ", " << j << ", "
              << plane_width << ")";
        }
      }
    }
  }

  void SpeedTestConvolve() {
    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int feature_lead = PC_WIENER_FEATURE_LEAD_LUMA;
      const int feature_lag = PC_WIENER_FEATURE_LAG_LUMA;
      FillInputBufs();

      // Calculate time taken by reference/c function
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        av2_fill_directional_feature_accumulators_c(
            dir_feature_accum_buf_c, feature_sum_buf, plane_width, feature_lag,
            feature_lead, feature_lag);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        target_func_(dir_feature_accum_buf_simd, feature_sum_buf, plane_width,
                     feature_lag, feature_lead, feature_lag);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

      float c_time_per_pixel =
          (float)1000.0 * elapsed_time_c / (kSpeedIterations * plane_width);
      float opt_time_per_pixel =
          (float)1000.0 * elapsed_time_opt / (kSpeedIterations * plane_width);
      float scaling = c_time_per_pixel / opt_time_per_pixel;
      printf(
          " %3d: c_time_per_pixel=%10.5f, "
          "opt_time_per_pixel=%10.5f,  scaling=%f \n",
          plane_width, c_time_per_pixel, opt_time_per_pixel, scaling);
    }
  }
};

TEST_P(AV2FeatureDirAccumHighbdTest, RunTest) { RunTest(); }
TEST_P(AV2FeatureDirAccumHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2FeatureDirAccumHighbdTest,
    ::testing::Values(av2_fill_directional_feature_accumulators_avx2));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2FeatureDirAccumHighbdTest);

//////////////////////////////////////////////////////////
//     unit-test for 'fill_tskip_feature_accumulator'   //
//////////////////////////////////////////////////////////
typedef void (*FillTskip_Accumulator_func)(
    int16_t tskip_feature_accum[PC_WIENER_FEATURE_ACC_SIZE],
    int8_t *tskip_sum_buff, int width, int col_offset, int tskip_lead,
    int tskip_lag);
typedef std::tuple<const FillTskip_Accumulator_func>
    AV2FillTSkipAccumBufferFuncParam;

class AV2TskipAccumHighbdTest
    : public ::testing::TestWithParam<AV2FillTSkipAccumBufferFuncParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) TestTskipAccum();
  }

  void RunSpeedTest() { SpeedTestTskipAccum(); };

 private:
  libavm_test::ACMRandom rnd_;
  FillTskip_Accumulator_func target_func_;

  static constexpr int kSpeedIterations = 1000000;
  static constexpr int kTestIterations = 100;
  static constexpr int kNumPlanes = 2;
  static constexpr int kWidth = RESTORATION_PROC_UNIT_SIZE;
  static constexpr int kInputWidth =
      (RESTORATION_PROC_UNIT_SIZE + PC_WIENER_FEATURE_LENGTH_LUMA - 1);

  int8_t *tskip_sum_buf;
  int16_t tskip_feature_accum_c[PC_WIENER_FEATURE_ACC_SIZE] = { 0 };
  int16_t tskip_feature_accum_simd[PC_WIENER_FEATURE_ACC_SIZE] = { 0 };

  void buffer_alloc_and_set_data() {
    tskip_sum_buf =
        (int8_t *)(avm_malloc(kInputWidth * sizeof(*tskip_sum_buf)));
    // Input buffer filling. Tskip buffer max value will not cross width of
    // restoration unit size. Hence, the generated values are clipped to the
    // same.
    for (int i = 0; i < kInputWidth; ++i) {
      const int8_t value =
          static_cast<int8_t>(rnd_.Rand8() % RESTORATION_PROC_UNIT_SIZE);
      tskip_sum_buf[i] = static_cast<uint8_t>(RandBool() ? value : -value);
    }
  }

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  void TestTskipAccum() {
    // Allocate memory and fill input buffer
    buffer_alloc_and_set_data();

    // Loop over luma and chroma plane
    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int tskip_lead = PC_WIENER_TSKIP_LEAD_LUMA;
      const int tskip_lag = PC_WIENER_TSKIP_LAG_LUMA;
      av2_zero(tskip_feature_accum_c);
      av2_zero(tskip_feature_accum_simd);

      // C function call
      av2_fill_tskip_feature_accumulator_c(tskip_feature_accum_c, tskip_sum_buf,
                                           plane_width, tskip_lag, tskip_lead,
                                           tskip_lag);

      // SIMD function call
      target_func_(tskip_feature_accum_simd, tskip_sum_buf, plane_width,
                   tskip_lag, tskip_lead, tskip_lag);

      // Compare the output of reference and test for bit match
      for (int i = 0; i < PC_WIENER_FEATURE_ACC_SIZE; i++) {
        ASSERT_EQ(tskip_feature_accum_c[i], tskip_feature_accum_simd[i])
            << " Feature_Buf: Pixel mismatch at (" << i << "," << plane_width
            << ")";
      }
    }
    avm_free(tskip_sum_buf);
    tskip_sum_buf = NULL;
  }

  void SpeedTestTskipAccum() {
    // Allocate memory and fill input buffer
    buffer_alloc_and_set_data();

    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int tskip_lead = PC_WIENER_TSKIP_LEAD_LUMA;
      const int tskip_lag = PC_WIENER_TSKIP_LAG_LUMA;

      // Calculate time taken by reference/c function
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        av2_fill_tskip_feature_accumulator_c(tskip_feature_accum_c,
                                             tskip_sum_buf, plane_width,
                                             tskip_lag, tskip_lead, tskip_lag);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_c = avm_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        target_func_(tskip_feature_accum_simd, tskip_sum_buf, plane_width,
                     tskip_lag, tskip_lead, tskip_lag);
      }
      avm_usec_timer_mark(&timer);
      auto elapsed_time_opt = avm_usec_timer_elapsed(&timer);

      float c_time_per_pixel =
          (float)1000.0 * elapsed_time_c / (kSpeedIterations * plane_width);
      float opt_time_per_pixel =
          (float)1000.0 * elapsed_time_opt / (kSpeedIterations * plane_width);
      float scaling = c_time_per_pixel / opt_time_per_pixel;
      printf(
          " %3d: c_time_per_pixel=%10.5f, "
          "opt_time_per_pixel=%10.5f,  scaling=%f \n",
          plane_width, c_time_per_pixel, opt_time_per_pixel, scaling);
    }
    avm_free(tskip_sum_buf);
    tskip_sum_buf = NULL;
  }
};

TEST_P(AV2TskipAccumHighbdTest, RunTest) { RunTest(); }
TEST_P(AV2TskipAccumHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2TskipAccumHighbdTest,
    ::testing::Values(av2_fill_tskip_feature_accumulator_avx2));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2TskipAccumHighbdTest);

//////////////////////////////////////////////////////////
//     unit-test for 'calc_wieners_ds_luma'             //
//////////////////////////////////////////////////////////
using libavm_test::ACMRandom;
using std::get;
using std::make_tuple;
using std::tuple;
using ::testing::Combine;
using ::testing::Values;
using ::testing::ValuesIn;

typedef void (*calc_wieners_ds_luma_func)(const uint16_t *src, int src_stride,
                                          uint16_t *const dst, int dst_stride,
                                          int ds_type, int height_uv,
                                          int width_uv, int ss_x, int ss_y,
                                          int col_start);

// width, height, ss_x, ss_y
typedef tuple<int, int, int, int> Param;
typedef tuple<calc_wieners_ds_luma_func, Param> WienersDSParam;

class WienersDSLumaTest : public ::testing::TestWithParam<WienersDSParam> {
 public:
  ~WienersDSLumaTest();
  void SetUp();

  void TearDown();

 protected:
  void RunCheckOutput();
  void RunSpeedTest();
  bool CheckResult(int width, int height, int ds_type) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int idx = y * width + x;
        if (out1_[idx] != out2_[idx]) {
          printf("%dx%d mismatch @%d for %d(%d,%d) ", width, height, idx,
                 ds_type, x, y);
          printf("%d != %d ", out1_[idx], out2_[idx]);
          return false;
        }
      }
    }
    return true;
  }
  ACMRandom rnd_;
  calc_wieners_ds_luma_func test_fun_;
  Param param_;
  uint16_t *src_;
  uint16_t *out1_;
  uint16_t *out2_;
  int width_;
  int width_uv_;
  int height_;
  int height_uv_;
  int src_stride_;
  int out_stride_;
  int ss_x_;
  int ss_y_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WienersDSLumaTest);

WienersDSLumaTest::~WienersDSLumaTest() {}

void WienersDSLumaTest::SetUp() {
  rnd_.Reset(ACMRandom::DeterministicSeed());
  test_fun_ = GET_PARAM(0);
  param_ = GET_PARAM(1);
  width_ = std::get<0>(param_);
  height_ = std::get<1>(param_);
  ss_x_ = std::get<2>(param_);
  ss_y_ = std::get<3>(param_);
  src_stride_ = width_;
  out_stride_ = width_ >> ss_x_;
  width_uv_ = out_stride_;
  height_uv_ = height_ >> ss_y_;
  src_ = (uint16_t *)avm_memalign(16, width_ * height_ * sizeof(*src_));
  ASSERT_NE(src_, nullptr);
  out1_ = (uint16_t *)avm_memalign(16, width_uv_ * height_uv_ * sizeof(*out1_));
  ASSERT_NE(out1_, nullptr);
  out2_ = (uint16_t *)avm_memalign(16, width_uv_ * height_uv_ * sizeof(*out2_));
  ASSERT_NE(out2_, nullptr);

  for (int i = 0; i < width_ * height_; ++i) {
    src_[i] = rnd_.Rand16();
  }
}

void WienersDSLumaTest::TearDown() {
  avm_free(src_);
  avm_free(out1_);
  avm_free(out2_);
}

void WienersDSLumaTest::RunCheckOutput() {
  for (int ds_type = 0; ds_type < 3; ds_type++) {
    calc_wienerns_ds_luma_420_c(src_, src_stride_, out1_, out_stride_, ds_type,
                                height_uv_, width_uv_, ss_x_, ss_y_, 0);
    test_fun_(src_, src_stride_, out2_, out_stride_, ds_type, height_uv_,
              width_uv_, ss_x_, ss_y_, 0);

    ASSERT_EQ(CheckResult(width_uv_, height_uv_, ds_type), true);
  }
}

void WienersDSLumaTest::RunSpeedTest() {
  const int num_loops = 1000;

  calc_wieners_ds_luma_func functions[2] = { calc_wienerns_ds_luma_420_c,
                                             test_fun_ };
  double elapsed_time[2] = { 0.0 };
  for (int i = 0; i < 2; ++i) {
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    calc_wieners_ds_luma_func func = functions[i];
    for (int j = 0; j < num_loops; ++j) {
      for (int ds_type = 0; ds_type < 3; ds_type++)
        func(src_, src_stride_, out1_, out_stride_, ds_type, height_uv_,
             width_uv_, ss_x_, ss_y_, 0);
    }
    avm_usec_timer_mark(&timer);
    const double time = static_cast<double>(avm_usec_timer_elapsed(&timer));
    elapsed_time[i] = 1000.0 * time;
  }
  printf(
      "WienersDSLumaTest %3dx%-3d: c_time=%7.2fs, simd_time=%7.2fs, "
      "scaling=%3.2f\n",
      width_, height_, elapsed_time[0], elapsed_time[1],
      elapsed_time[0] / elapsed_time[1]);
}

TEST_P(WienersDSLumaTest, CheckOutput) { RunCheckOutput(); }

TEST_P(WienersDSLumaTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
const Param kwienerns_ds_luma_420[] = {
  make_tuple(3840, 2160, 1, 1), make_tuple(1920, 1080, 1, 1),
  make_tuple(1280, 720, 1, 1),  make_tuple(640, 480, 1, 1),
  make_tuple(480, 270, 1, 1),   make_tuple(270, 480, 1, 1),
};

INSTANTIATE_TEST_SUITE_P(
    AVX2, WienersDSLumaTest,
    ::testing::Combine(::testing::Values(&calc_wienerns_ds_luma_420_avx2),
                       ::testing::ValuesIn(kwienerns_ds_luma_420)));
#endif  // HAVE_AVX2
}  // namespace
