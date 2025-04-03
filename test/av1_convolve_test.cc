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

#include <ostream>
#include <set>
#include <vector>
#include "aom_ports/aom_timer.h"
#include "config/av1_rtcd.h"
#include "config/aom_dsp_rtcd.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#include "test/acm_random.h"
#pragma GCC diagnostic pop
#include "test/clear_system_state.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av1/common/restoration.h"

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
  std::vector<TestParam<T>> result;
  for (const BlockSize &block : sizes) {
    for (int bd : bit_depths) {
      result.push_back(TestParam<T>(block, bd, test_func));
    }
  }
  return result;
}

// Test the test-parameters generators work as expected.
class AV1ConvolveParametersTest : public ::testing::Test {};

template <typename T>
std::vector<TestParam<T>> GetHighbdTestParams(T test_func) {
  return GetTestParams({ 10, 12 }, test_func);
}

template <typename T>
::testing::internal::ParamGenerator<TestParam<T>> BuildHighbdParams(
    T test_func) {
  return ::testing::ValuesIn(GetHighbdTestParams(test_func));
}

TEST_F(AV1ConvolveParametersTest, GetHighbdTestParams) {
  auto v = GetHighbdTestParams(av1_highbd_convolve_x_sr_c);
#if CONFIG_EXT_RECUR_PARTITIONS
  ASSERT_EQ(80U, v.size());
#else
  ASSERT_EQ(60U, v.size());
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  int num_10 = 0;
  int num_12 = 0;
  for (const auto &p : v) {
    ASSERT_TRUE(p.BitDepth() == 10 || p.BitDepth() == 12);
    bool same_fn = av1_highbd_convolve_x_sr_c == p.TestFunction();
    ASSERT_TRUE(same_fn);
    if (p.BitDepth() == 10) {
      ++num_10;
    } else {
      ++num_12;
    }
  }
  ASSERT_EQ(num_10, num_12);
}

// AV1ConvolveTest is the base class that all convolve tests should derive from.
// It provides storage/methods for generating randomized buffers for both
// low bit-depth and high bit-depth, and setup/teardown methods for clearing
// system state. Implementors can get the bit-depth / block-size /
// test function by calling GetParam().
template <typename T>
class AV1ConvolveTest : public ::testing::TestWithParam<TestParam<T>> {
 public:
  virtual ~AV1ConvolveTest() { TearDown(); }

  virtual void SetUp() override {
    rnd_.Reset(libaom_test::ACMRandom::DeterministicSeed());
  }

  virtual void TearDown() override { libaom_test::ClearSystemState(); }

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

  libaom_test::ACMRandom rnd_;
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

class AV1ConvolveXHighbdTest : public AV1ConvolveTest<highbd_convolve_x_func> {
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

 private:
  void TestConvolve(const int sub_x, const InterpFilter filter) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_x =
        av1_get_interp_filter_params_with_block_size(filter, width);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    av1_highbd_convolve_x_sr_c(input, width, reference, kOutputStride, width,
                               height, filter_params_x, sub_x, &conv_params1,
                               bit_depth);

    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height,
                              filter_params_x, sub_x, &conv_params2, bit_depth);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV1ConvolveXHighbdTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV1ConvolveXHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_x_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV1ConvolveXHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_x_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1ConvolveXHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_x_sr_avx2));
#endif

/////////////////////////////////////////////////////////
// Single reference convolve-y functions (high bit-depth)
/////////////////////////////////////////////////////////
typedef void (*highbd_convolve_y_func)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_y, const int subpel_y_qn,
    int bd);

class AV1ConvolveYHighbdTest : public AV1ConvolveTest<highbd_convolve_y_func> {
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

 private:
  void TestConvolve(const int sub_y, const InterpFilter filter) {
    const int width = GetParam().Block().Width();
    const int height = GetParam().Block().Height();
    const int bit_depth = GetParam().BitDepth();
    const InterpFilterParams *filter_params_y =
        av1_get_interp_filter_params_with_block_size(filter, height);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    av1_highbd_convolve_y_sr_c(input, width, reference, kOutputStride, width,
                               height, filter_params_y, sub_y, bit_depth);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height,
                              filter_params_y, sub_y, bit_depth);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV1ConvolveYHighbdTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV1ConvolveYHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_y_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV1ConvolveYHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_y_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1ConvolveYHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_y_sr_avx2));
#endif

///////////////////////////////////////////////////////////////
// Single reference convolve-copy functions (high bit-depth)
///////////////////////////////////////////////////////////////
typedef void (*highbd_convolve_copy_func)(const uint16_t *src,
                                          ptrdiff_t src_stride, uint16_t *dst,
                                          ptrdiff_t dst_stride, int w, int h);

class AV1ConvolveCopyHighbdTest
    : public AV1ConvolveTest<highbd_convolve_copy_func> {
 public:
  void RunTest() {
    const BlockSize &block = GetParam().Block();
    const int width = block.Width();
    const int height = block.Height();
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    aom_highbd_convolve_copy_c(input, width, reference, kOutputStride, width,
                               height);
    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    GetParam().TestFunction()(input, width, test, kOutputStride, width, height);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV1ConvolveCopyHighbdTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV1ConvolveCopyHighbdTest,
                         BuildHighbdParams(aom_highbd_convolve_copy_c));

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, AV1ConvolveCopyHighbdTest,
                         BuildHighbdParams(aom_highbd_convolve_copy_sse2));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1ConvolveCopyHighbdTest,
                         BuildHighbdParams(aom_highbd_convolve_copy_avx2));
#endif

//////////////////////////////////////////////////////////
// Single reference convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_2d_func)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);

class AV1Convolve2DHighbdTest
    : public AV1ConvolveTest<highbd_convolve_2d_func> {
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
        av1_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av1_get_interp_filter_params_with_block_size(v_f, height);
    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, NULL, 0, 0, bit_depth);
    av1_highbd_convolve_2d_sr_c(input, width, reference, kOutputStride, width,
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
        av1_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av1_get_interp_filter_params_with_block_size(v_f, height);

    const uint16_t *input = FirstRandomInput12(GetParam());
    DECLARE_ALIGNED(32, uint16_t, reference[MAX_SB_SQUARE]);
    ConvolveParams conv_params1 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      av1_highbd_convolve_2d_sr_c(input, width, reference, kOutputStride, width,
                                  height, filter_params_x, filter_params_y,
                                  sub_x, sub_y, &conv_params1, bit_depth);
    }
    aom_usec_timer_mark(&timer);
    const int time1 = static_cast<int>(aom_usec_timer_elapsed(&timer));

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    ConvolveParams conv_params2 =
        get_conv_params_no_round(0, 0, nullptr, 0, 0, bit_depth);
    aom_usec_timer_start(&timer);
    for (int i = 0; i < num_iters; ++i) {
      GetParam().TestFunction()(input, width, test, kOutputStride, width,
                                height, filter_params_x, filter_params_y, sub_x,
                                sub_y, &conv_params2, bit_depth);
    }
    aom_usec_timer_mark(&timer);
    const int time2 = static_cast<int>(aom_usec_timer_elapsed(&timer));

    printf("%d - %d %3dx%-3d bd: %d ref: %d mod: %d (%3.2f)\n", h_f, v_f, width,
           height, bit_depth, time1, time2, (double)time1 / time2);
    AssertOutputBufferEq(reference, test, width, height);
  }
};

TEST_P(AV1Convolve2DHighbdTest, RunTest) { RunTest(); }
TEST_P(AV1Convolve2DHighbdTest, DISABLED_Speed) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(C, AV1Convolve2DHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_2d_sr_c));

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV1Convolve2DHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_2d_sr_ssse3));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1Convolve2DHighbdTest,
                         BuildHighbdParams(av1_highbd_convolve_2d_sr_avx2));
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

TEST_F(AV1ConvolveParametersTest, GetHighbdLumaTestParams) {
  auto v = GetHighbdLumaTestParams(av1_highbd_dist_wtd_convolve_x_c);
  ASSERT_EQ(static_cast<size_t>(BLOCK_SIZES_ALL * 2), v.size());
  int num_10 = 0;
  int num_12 = 0;
  for (const auto &e : v) {
    ASSERT_TRUE(10 == e.BitDepth() || 12 == e.BitDepth());
    bool same_fn = av1_highbd_dist_wtd_convolve_x_c == e.TestFunction();
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
  return result;
}

TEST_F(AV1ConvolveParametersTest, GetCompoundParams) {
  auto v = GetCompoundParams();
  ASSERT_EQ(9U, v.size());
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

class AV1ConvolveXHighbdCompoundTest
    : public AV1ConvolveTest<highbd_convolve_x_func> {
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
    return av1_get_interp_filter_params_with_block_size(f, block.Width());
  }

  virtual highbd_convolve_x_func ReferenceFunc() const {
    return av1_highbd_dist_wtd_convolve_x_c;
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

TEST_P(AV1ConvolveXHighbdCompoundTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, AV1ConvolveXHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_x_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV1ConvolveXHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_x_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1ConvolveXHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_x_avx2));
#endif

/////////////////////////////////////////////////
// Compound convolve-y functions (high bit-depth)
/////////////////////////////////////////////////

// Again, the X and Y convolve functions have the same type signature and logic.
class AV1ConvolveYHighbdCompoundTest : public AV1ConvolveXHighbdCompoundTest {
  virtual highbd_convolve_x_func ReferenceFunc() const override {
    return av1_highbd_dist_wtd_convolve_y_c;
  }
  virtual const InterpFilterParams *FilterParams(
      InterpFilter f, const BlockSize &block) const override {
    return av1_get_interp_filter_params_with_block_size(f, block.Height());
  }
};

TEST_P(AV1ConvolveYHighbdCompoundTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, AV1ConvolveYHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_y_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV1ConvolveYHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_y_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1ConvolveYHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_y_avx2));
#endif

///////////////////////////////////////////////////////
// Compound convolve-2d-copy functions (high bit-depth)
///////////////////////////////////////////////////////
typedef void (*highbd_compound_conv_2d_copy_func)(const uint16_t *src,
                                                  int src_stride, uint16_t *dst,
                                                  int dst_stride, int w, int h,
                                                  ConvolveParams *conv_params,
                                                  int bd);

class AV1Convolve2DCopyHighbdCompoundTest
    : public AV1ConvolveTest<highbd_compound_conv_2d_copy_func> {
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

    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int i = 0; i < nob; i++) {
      av1_highbd_dist_wtd_convolve_2d_copy_c(input, width, conv_buf,
                                             kOutputStride, width, height,
                                             &conv_params, bit_depth);
      av1_highbd_dist_wtd_convolve_2d_copy_c(input, width, conv_buf,
                                             kOutputStride, width, height,
                                             &conv_params_do_avg, bit_depth);
    }
    aom_usec_timer_mark(&timer);
    const int elapsed_time = static_cast<int>(aom_usec_timer_elapsed(&timer));

    aom_usec_timer timer1;
    aom_usec_timer_start(&timer1);
    for (int i = 0; i < nob; i++) {
      test_func(input, width, conv_buf, kOutputStride, width, height,
                &conv_params, bit_depth);
      test_func(input, width, conv_buf, kOutputStride, width, height,
                &conv_params_do_avg, bit_depth);
    }
    aom_usec_timer_mark(&timer1);
    const int elapsed_time1 = static_cast<int>(aom_usec_timer_elapsed(&timer1));
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
    Convolve(av1_highbd_dist_wtd_convolve_2d_copy_c, input1, input2, reference,
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

TEST_P(AV1Convolve2DCopyHighbdCompoundTest, RunTest) { RunTest(); }
TEST_P(AV1Convolve2DCopyHighbdCompoundTest, DISABLED_SpeedTest) { SpeedTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, AV1Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_copy_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV1Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_copy_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1Convolve2DCopyHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_copy_avx2));
#endif

//////////////////////////////////////////////////
// Compound convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////

class AV1Convolve2DHighbdCompoundTest
    : public AV1ConvolveTest<highbd_convolve_2d_func> {
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
    Convolve(av1_highbd_dist_wtd_convolve_2d_c, input1, input2, reference,
             reference_conv_buf, compound, h_f, v_f, sub_x, sub_y);

    DECLARE_ALIGNED(32, uint16_t, test[MAX_SB_SQUARE]);
    DECLARE_ALIGNED(32, CONV_BUF_TYPE, test_conv_buf[MAX_SB_SQUARE]);
    Convolve(GetParam().TestFunction(), input1, input2, test, test_conv_buf,
             compound, h_f, v_f, sub_x, sub_y);

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
        av1_get_interp_filter_params_with_block_size(h_f, width);
    const InterpFilterParams *filter_params_y =
        av1_get_interp_filter_params_with_block_size(v_f, height);
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

TEST_P(AV1Convolve2DHighbdCompoundTest, RunTest) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(
    C, AV1Convolve2DHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV1Convolve2DHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1Convolve2DHighbdCompoundTest,
    BuildHighbdLumaParams(av1_highbd_dist_wtd_convolve_2d_avx2));
#endif

//////////////////////////////////////////////////////////
// Nonseparable convolve-2d functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_nonsep_2d_func)(
    const uint16_t *src, int src_stride,
    const NonsepFilterConfig *filter_config, const int16_t *filter,
    uint16_t *dst, int dst_stride, int bit_depth, int block_row_begin,
    int block_row_end, int block_col_begin, int block_col_end);

class AV1ConvolveNonSep2DHighbdTest
    : public AV1ConvolveTest<highbd_convolve_nonsep_2d_func> {
 public:
  void RunTest(RestorationType rtype) {
    for (int i = 0; i < kTestIterations; i++) {
      SetFilterTaps();
      TestConvolve(FilterTaps_, rtype);
    }
  }
  void RunSpeedTest(RestorationType rtype) {
    SpeedTestConvolve(FilterTaps_, rtype);
  };

 private:
  void BitMatchTest(const uint16_t *input, int input_stride, int width,
                    int height, const int16_t *filter, uint16_t *reference,
                    uint16_t *test, int dst_stride, int bit_depth,
                    int block_row_begin, int block_row_end, int block_col_begin,
                    int block_col_end, RestorationType rtype) {
    const NonsepFilterConfig *filter_config[2] = { NULL, NULL };
    highbd_convolve_nonsep_2d_func ref_func = av1_convolve_symmetric_highbd_c;
    const int num_planes = 2;

    if (rtype == RESTORE_PC_WIENER) {
      ref_func = av1_convolve_symmetric_highbd_c;
      filter_config[0] = &UnconstrainedSumFilterConfig_;
      filter_config[1] = &PcWienerNonsepFilterConfigChroma_;
    }

    // When CONFIG_WIENER_NONSEP=1, luma and chroma plane uses different number
    // of filter taps and both needs to be tested. Here, luma is tested for
    // 12/13-tap filtering whereas chroma is tested for 6-tap filtering.
    if (rtype == RESTORE_WIENER_NONSEP) {
      ref_func = av1_convolve_symmetric_subtract_center_highbd_c;
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
                              kMaxPrecisionBeforeOverflow);
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
    highbd_convolve_nonsep_2d_func ref_func = av1_convolve_symmetric_highbd_c;

    if (rtype == RESTORE_PC_WIENER) {
      ref_func = av1_convolve_symmetric_highbd_c;
      filter_config[0] = &UnconstrainedSumFilterConfig_;
      filter_config[1] = &PcWienerNonsepFilterConfigChroma_;
    }

    // When CONFIG_WIENER_NONSEP=1, luma and chroma uses different number of
    // filter taps and both needs to be tested. Here, luma is tested for
    // 12/13-tap filtering whereas chroma is tested for 6-tap filtering.
    if (rtype == RESTORE_WIENER_NONSEP) {
      ref_func = av1_convolve_symmetric_subtract_center_highbd_c;
      filter_config[0] = &UnitSumFilterConfig_;
      filter_config[1] = &UnitSumFilterConfigChroma_;
    }

    for (int plane = 0; plane < num_planes; plane++) {
      // Calculate time taken by reference/c function
      aom_usec_timer timer;
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        ref_func(input, width, filter_config[plane], filter, reference,
                 kOutputStride, bit_depth, 0, height, 0, width);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        GetParam().TestFunction()(input, width, filter_config[plane], filter,
                                  test, kOutputStride, bit_depth, 0, height, 0,
                                  width);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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
    Randomize(FilterTaps_, kMaxNumSymmetricTaps + 1,
              kMaxPrecisionBeforeOverflow);
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

  libaom_test::ACMRandom rnd_;
  static constexpr int kMaxPrecisionBeforeOverflow = 12;
  static constexpr int kMaxNumSymmetricTaps = 18;
  static constexpr int kMaxTapOffset = 3;  // Filters are 7x7.
  static constexpr int kSpeedIterations = 10000;
  static constexpr int kTestIterations = 100;

  // Filters use all unique taps.
  const NonsepFilterConfig UnconstrainedSumFilterConfig_ = {
    kMaxPrecisionBeforeOverflow,
    sizeof(wienerns_simd_config_y) / sizeof(wienerns_simd_config_y[0]),
    0,
    wienerns_simd_config_y,
    NULL,
    0,
    0,
    1,
    1
  };

  const NonsepFilterConfig PcWienerNonsepFilterConfigChroma_ = {
    kMaxPrecisionBeforeOverflow,
    sizeof(wienerns_simd_config_uv_from_uvonly) /
        sizeof(wienerns_simd_config_uv_from_uvonly[0]),
    0,
    wienerns_simd_config_uv_from_uvonly,
    NULL,
    0,
    0,
    1,
    1
  };

  const NonsepFilterConfig UnitSumFilterConfig_ = {
    kMaxPrecisionBeforeOverflow,
    sizeof(wienerns_simd_config_y) / sizeof(wienerns_simd_config_y[0]) - 1,
    0,
    wienerns_simd_config_y,
    NULL,
    0,
    1,
    1,
    1
  };

  // Config used for filtering of chroma when CONFIG_WIENER_NONSEP=1.
  const NonsepFilterConfig UnitSumFilterConfigChroma_ = {
    kMaxPrecisionBeforeOverflow,
    sizeof(wienerns_simd_config_uv_from_uv) /
            sizeof(wienerns_simd_config_uv_from_uv[0]) -
        1,
    0,
    wienerns_simd_config_uv_from_uv,
    NULL,
    0,
    1,
    1,
    1
  };

  int16_t FilterTaps_[kMaxNumSymmetricTaps + 1];
};

TEST_P(AV1ConvolveNonSep2DHighbdTest, DISABLED_RunTest) {
  RunTest(RESTORE_PC_WIENER);
}

TEST_P(AV1ConvolveNonSep2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(RESTORE_PC_WIENER);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1ConvolveNonSep2DHighbdTest,
                         BuildHighbdParams(av1_convolve_symmetric_highbd_avx2));
#endif

class AV1ConvolveWienerNonSep2DHighbdTest
    : public AV1ConvolveNonSep2DHighbdTest {};

TEST_P(AV1ConvolveWienerNonSep2DHighbdTest, RunTest) {
  RunTest(RESTORE_WIENER_NONSEP);
}
TEST_P(AV1ConvolveWienerNonSep2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(RESTORE_WIENER_NONSEP);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1ConvolveWienerNonSep2DHighbdTest,
    BuildHighbdParams(av1_convolve_symmetric_subtract_center_highbd_avx2));
#endif

//////////////////////////////////////////////////////////
// Nonseparable convolve-2d Dual functions (high bit-depth)
//////////////////////////////////////////////////////////

typedef void (*highbd_convolve_nonsep_dual_2d_func)(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end);

class AV1ConvolveNonSep_dual2DHighbdTest
    : public AV1ConvolveTest<highbd_convolve_nonsep_dual_2d_func> {
 public:
  void RunTest(int is_subtract_center) {
    for (int i = 0; i < kTestIterations; i++) {
      SetFilterTaps();
      TestConvolve(FilterTaps_, is_subtract_center);
    }
  }
  void RunSpeedTest(int is_subtract_center) {
    SpeedTestConvolve(FilterTaps_, is_subtract_center);
  };

 private:
  libaom_test::ACMRandom rnd_;
  static constexpr int kMaxPrecisionBeforeOverflow = 12;
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
    Randomize(FilterTaps_, kNumSubtractCenterOffTaps,
              kMaxPrecisionBeforeOverflow);
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
    ref_func = av1_convolve_symmetric_dual_subtract_center_highbd_c;

    if (!is_subtract_center) {
      ref_func = av1_convolve_symmetric_dual_highbd_c;
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
                              kMaxPrecisionBeforeOverflow);
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
    ref_func = av1_convolve_symmetric_dual_subtract_center_highbd_c;

    if (!is_subtract_center) {
      ref_func = av1_convolve_symmetric_dual_highbd_c;
      filter_cfg = &DualFilterWithoutCenterConfig_;
    }

    // Calculate time taken by reference/c function
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      ref_func(dgd, width, dgd_dual, width, filter_cfg, filter, reference,
               kOutputStride, bit_depth, 0, height, 0, width);
    }
    aom_usec_timer_mark(&timer);
    auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

    // Calculate time taken by optimized/intrinsic function
    aom_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      GetParam().TestFunction()(dgd, width, dgd_dual, width, filter_cfg, filter,
                                test, kOutputStride, bit_depth, 0, height, 0,
                                width);
    }
    aom_usec_timer_mark(&timer);
    auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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

  const NonsepFilterConfig DualFilterWithCenterConfig_ = {
    kMaxPrecisionBeforeOverflow,  // prec_bits;
    sizeof(wienerns_simd_config_uv_from_uv) /
            sizeof(wienerns_simd_config_uv_from_uv[0]) -
        1,  // num_pixels;
    sizeof(wienerns_simd_config_uv_from_y) /
            sizeof(wienerns_simd_config_uv_from_y[0]) -
        1,                            // num_pixels2
    wienerns_simd_config_uv_from_uv,  // config
    wienerns_simd_config_uv_from_y,   // config2
    0,                                // strict_bounds
    1,                                // subtract_center
    1,                                // symmetry config
    0,                                // symmetry config2
  };

  const NonsepFilterConfig DualFilterWithoutCenterConfig_ = {
    kMaxPrecisionBeforeOverflow,  // prec_bits;
    sizeof(wienerns_simd_config_uv_from_uv) /
        sizeof(wienerns_simd_config_uv_from_uv[0]),  // num_pixels;
    sizeof(wienerns_simd_config_uv_from_y) /
        sizeof(wienerns_simd_config_uv_from_y[0]),  // num_pixels2
    wienerns_simd_config_uv_from_uv,                // config
    wienerns_simd_config_uv_from_y,                 // config2
    0,                                              // strict_bounds
    0,                                              // subtract_center
    1,                                              // symmetry config
    0,                                              // symmetry config2
  };
};

TEST_P(AV1ConvolveNonSep_dual2DHighbdTest, RunTest) { RunTest(1); }
TEST_P(AV1ConvolveNonSep_dual2DHighbdTest, DISABLED_Speed) { RunSpeedTest(1); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1ConvolveNonSep_dual2DHighbdTest,
    BuildHighbdParams(av1_convolve_symmetric_dual_subtract_center_highbd_avx2));
#endif  // HAVE_AVX2

/* Dual with subtract center off unit-test*/
class AV1ConvolveDualWithoutsubtract2DHighbdTest
    : public AV1ConvolveNonSep_dual2DHighbdTest {};

TEST_P(AV1ConvolveDualWithoutsubtract2DHighbdTest, RunTest) { RunTest(0); }
TEST_P(AV1ConvolveDualWithoutsubtract2DHighbdTest, DISABLED_Speed) {
  RunSpeedTest(0);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1ConvolveDualWithoutsubtract2DHighbdTest,
    BuildHighbdParams(av1_convolve_symmetric_dual_highbd_avx2));
#endif

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

class AV1FillDirFeatureBufHighbdTest
    : public AV1ConvolveTest<fill_directional_feature_buffers_highbd_func> {
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
          (aom_malloc(buffer_width_ * sizeof(*feature_line_buffers_c_[j]))));
      ASSERT_NE(feature_line_buffers_c_[j], nullptr);

      feature_line_buffers_simd_[j] = static_cast<int16_t *>(
          (aom_malloc(buffer_width_ * sizeof(*feature_line_buffers_simd_[j]))));
      ASSERT_NE(feature_line_buffers_simd_[j], nullptr);
    }

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      feature_sum_buffers_c_[j] = static_cast<int *>(
          (aom_malloc(buffer_width_ * sizeof(*feature_sum_buffers_c_[j]))));
      ASSERT_NE(feature_sum_buffers_c_[j], nullptr);

      feature_sum_buffers_simd_[j] = static_cast<int *>(
          (aom_malloc(buffer_width_ * sizeof(*feature_sum_buffers_simd_[j]))));
      ASSERT_NE(feature_sum_buffers_simd_[j], nullptr);
    }
  }

  virtual void TearDown() {
    for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
      aom_free(feature_line_buffers_c_[j]);
      feature_line_buffers_c_[j] = NULL;
      aom_free(feature_line_buffers_simd_[j]);
      feature_line_buffers_simd_[j] = NULL;
    }

    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      aom_free(feature_sum_buffers_c_[j]);
      feature_sum_buffers_c_[j] = NULL;
      aom_free(feature_sum_buffers_simd_[j]);
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
  libaom_test::ACMRandom rnd_;
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
      const int row_to_process = AOMMIN(i + feature_lag, height + 3 - 2);
      fill_directional_feature_buffers_highbd_c(
          feature_sum_buffers_c_, feature_line_buffers_c_, row_to_process,
          feature_length - 1, input, input_stride, width, feature_lead,
          feature_lag);
    }

    // SIMD function call
    for (int i = 0; i < height; ++i) {
      const int row_to_process = AOMMIN(i + feature_lag, height + 3 - 2);
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
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      for (int i = 0; i < height; ++i) {
        const int row_to_process = AOMMIN(i + feature_lag, height + 3 - 2);
        fill_directional_feature_buffers_highbd_c(
            feature_sum_buffers_c_, feature_line_buffers_c_, row_to_process,
            feature_length - 1, input, input_stride, width, feature_lead,
            feature_lag);
      }
    }
    aom_usec_timer_mark(&timer);
    auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

    // Calculate time taken by optimized/intrinsic function
    aom_usec_timer_start(&timer);
    for (int i = 0; i < kSpeedIterations; ++i) {
      for (int i = 0; i < height; ++i) {
        const int row_to_process = AOMMIN(i + feature_lag, height + 3 - 2);
        GetParam().TestFunction()(feature_sum_buffers_simd_,
                                  feature_line_buffers_simd_, row_to_process,
                                  feature_length - 1, input, input_stride,
                                  width, feature_lead, feature_lag);
      }
    }
    aom_usec_timer_mark(&timer);
    auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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

TEST_P(AV1FillDirFeatureBufHighbdTest, RunTest) { RunTest(); }

TEST_P(AV1FillDirFeatureBufHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1FillDirFeatureBufHighbdTest,
    BuildHighbdPCWienerParams(fill_directional_feature_buffers_highbd_avx2));
#endif  // HAVE_AVX2

typedef void (*FillTSkipSumBufferFunc)(int row, const uint8_t *tskip,
                                       int tskip_stride,
                                       int8_t *tskip_sum_buffer, int width,
                                       int height, int tskip_lead,
                                       int tskip_lag, bool use_strict_bounds);

typedef std::tuple<const FillTSkipSumBufferFunc> AV1FillTSkipSumBufferFuncParam;

class AV1Fill_TSkip_Sum_BufferTest
    : public ::testing::TestWithParam<AV1FillTSkipSumBufferFuncParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) {
      TestTSkipSum();
    }
  }
  void RunSpeedTest() { SpeedTestTSkipSum(); };

 private:
  libaom_test::ACMRandom rnd_;
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
        av1_fill_tskip_sum_buffer_c(row, input_buffer_, kInputStride,
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
      aom_usec_timer timer;
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        // Reference function
        for (int row = -tskip_lead; row < (tskip_lag + plane_height - 1);
             ++row) {
          av1_fill_tskip_sum_buffer_c(row, input_buffer_, kInputStride,
                                      ref_buffer_, plane_width, plane_height,
                                      tskip_lead, tskip_lag, tskip_strict_);
        }
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        for (int row = -tskip_lead; row < (tskip_lag + plane_height - 1);
             ++row) {
          target_func_(row, input_buffer_, kInputStride, test_buffer_,
                       plane_width, plane_height, tskip_lead, tskip_lag,
                       tskip_strict_);
        }
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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

TEST_P(AV1Fill_TSkip_Sum_BufferTest, RunTest) { RunTest(); }
TEST_P(AV1Fill_TSkip_Sum_BufferTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1Fill_TSkip_Sum_BufferTest,
                         ::testing::Values(av1_fill_tskip_sum_buffer_avx2));
#endif  // HAVE_AVX2

//////////////////////////////////////////////////////////
//       unit-test for 'directional_feature_accum'      //
//////////////////////////////////////////////////////////
typedef void (*FillDirFeatureAccumFunc)(
    int dir_feature_accum[NUM_PC_WIENER_FEATURES][PC_WIENER_FEATURE_ACC_SIZE],
    int *feature_sum_buf[NUM_PC_WIENER_FEATURES], int width, int col_offset,
    int feature_lead, int feature_lag);

typedef std::tuple<const FillDirFeatureAccumFunc>
    AV1FillDirFeatureAccumFuncParam;

class AV1FeatureDirAccumHighbdTest
    : public ::testing::TestWithParam<AV1FillDirFeatureAccumFuncParam> {
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
          (int *)(aom_malloc(kInputWidth * sizeof(*feature_sum_buf[j])));
    }
  }

  virtual void TearDown() {
    for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
      aom_free(feature_sum_buf[j]);
      feature_sum_buf[j] = NULL;
    }
  }

 private:
  libaom_test::ACMRandom rnd_;
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
    av1_zero(dir_feature_accum_buf_c);
    av1_zero(dir_feature_accum_buf_simd);
  }

  void TestFillDirFeatureAccum() {
    for (int plane = 0; plane < kNumPlanes; ++plane) {
      const int is_uv = (plane > 0);
      const int ss_x = is_uv ? 1 : 0;
      const int plane_width = kWidth >> ss_x;
      const int feature_lead = PC_WIENER_FEATURE_LEAD_LUMA;
      const int feature_lag = PC_WIENER_FEATURE_LAG_LUMA;

      // Reset output buffers
      av1_zero(dir_feature_accum_buf_c);
      av1_zero(dir_feature_accum_buf_simd);

      // C function call
      av1_fill_directional_feature_accumulators_c(
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
      aom_usec_timer timer;
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        av1_fill_directional_feature_accumulators_c(
            dir_feature_accum_buf_c, feature_sum_buf, plane_width, feature_lag,
            feature_lead, feature_lag);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        target_func_(dir_feature_accum_buf_simd, feature_sum_buf, plane_width,
                     feature_lag, feature_lead, feature_lag);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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

TEST_P(AV1FeatureDirAccumHighbdTest, RunTest) { RunTest(); }
TEST_P(AV1FeatureDirAccumHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1FeatureDirAccumHighbdTest,
    ::testing::Values(av1_fill_directional_feature_accumulators_avx2));
#endif  // HAVE_AVX2

//////////////////////////////////////////////////////////
//     unit-test for 'fill_tskip_feature_accumulator'   //
//////////////////////////////////////////////////////////
typedef void (*FillTskip_Accumulator_func)(
    int16_t tskip_feature_accum[PC_WIENER_FEATURE_ACC_SIZE],
    int8_t *tskip_sum_buff, int width, int col_offset, int tskip_lead,
    int tskip_lag);
typedef std::tuple<const FillTskip_Accumulator_func>
    AV1FillTSkipAccumBufferFuncParam;

class AV1TskipAccumHighbdTest
    : public ::testing::TestWithParam<AV1FillTSkipAccumBufferFuncParam> {
 public:
  virtual void SetUp() { target_func_ = GET_PARAM(0); }

  void RunTest() {
    for (int i = 0; i < kTestIterations; i++) TestTskipAccum();
  }

  void RunSpeedTest() { SpeedTestTskipAccum(); };

 private:
  libaom_test::ACMRandom rnd_;
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
        (int8_t *)(aom_malloc(kInputWidth * sizeof(*tskip_sum_buf)));
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
      av1_zero(tskip_feature_accum_c);
      av1_zero(tskip_feature_accum_simd);

      // C function call
      av1_fill_tskip_feature_accumulator_c(tskip_feature_accum_c, tskip_sum_buf,
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
    aom_free(tskip_sum_buf);
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
      aom_usec_timer timer;
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        av1_fill_tskip_feature_accumulator_c(tskip_feature_accum_c,
                                             tskip_sum_buf, plane_width,
                                             tskip_lag, tskip_lead, tskip_lag);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_c = aom_usec_timer_elapsed(&timer);

      // Calculate time taken by optimized/intrinsic function
      aom_usec_timer_start(&timer);
      for (int i = 0; i < kSpeedIterations; ++i) {
        target_func_(tskip_feature_accum_simd, tskip_sum_buf, plane_width,
                     tskip_lag, tskip_lead, tskip_lag);
      }
      aom_usec_timer_mark(&timer);
      auto elapsed_time_opt = aom_usec_timer_elapsed(&timer);

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
    aom_free(tskip_sum_buf);
    tskip_sum_buf = NULL;
  }
};

TEST_P(AV1TskipAccumHighbdTest, RunTest) { RunTest(); }
TEST_P(AV1TskipAccumHighbdTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1TskipAccumHighbdTest,
    ::testing::Values(av1_fill_tskip_feature_accumulator_avx2));
#endif  // HAVE_AVX2
}  // namespace
