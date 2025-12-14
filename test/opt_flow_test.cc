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

#include <set>
#include <vector>
#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm_ports/avm_timer.h"
#include "av2/common/reconinter.h"
#include "av2/common/mvref_common.h"

namespace {

class BlockSize {
 public:
  BlockSize(int w, int h) : width_(w), height_(h) {
    n_ = (w <= 8 && h <= 8) ? OF_MIN_BSIZE : OF_BSIZE;
  }

  int Width() const { return width_; }
  int Height() const { return height_; }
  int OptFlowBlkSize() const { return n_; }

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
  int n_;
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

// AV2OptFlowTest is the base class that all optical flow tests should derive
// from.
template <typename T>
class AV2OptFlowTest : public ::testing::TestWithParam<TestParam<T>> {
 public:
  virtual ~AV2OptFlowTest() { TearDown(); }

  virtual void SetUp() override {
    rnd_.Reset(libavm_test::ACMRandom::DeterministicSeed());
  }

  virtual void TearDown() override { libavm_test::ClearSystemState(); }

  // Check that two 8-bit output buffers are identical.
  void AssertOutputEq(const int *ref, const int *test, int n) {
    ASSERT_TRUE(ref != test) << "Buffers must be at different memory locations";
    for (int idx = 0; idx < n; ++idx) {
      ASSERT_EQ(ref[idx], test[idx]) << "Mismatch at index " << idx;
    }
  }

  // Check that two 16-bit output buffers are identical.
  void AssertOutputBufferEq(const int16_t *ref, const int16_t *test, int width,
                            int height, int stride) {
    ASSERT_TRUE(ref != test) << "Buffers must be in different memory locations";
    for (int row = 0; row < height; ++row) {
      for (int col = 0; col < width; ++col) {
        ASSERT_EQ(ref[row * stride + col], test[row * stride + col])
            << width << "x" << height << " Pixel mismatch at (" << col << ", "
            << row << ")";
      }
    }
  }

  uint8_t RandomFrameIdx(int max_bit_range) {
    const int max_val = (1 << max_bit_range) - 1;
    uint8_t rand_val = rnd_.Rand8() & max_val;
    return rand_val;
  }

  int8_t RelativeDistExtreme(int max_bit_range) {
    return Rand8SingedExtremes(max_bit_range);
  }

  void RandomInput8(uint8_t *p, const TestParam<T> &param) {
    EXPECT_EQ(8, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    Randomize(p, bw * bh);
  }

  void Randomize9Signed(int16_t *p, int size) {
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand9Signed();
    }
  }

  void RandomInput9(int16_t *p, const TestParam<T> &param) {
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    Randomize9Signed(p, bw * bh);
  }

  void RandomInput16(uint16_t *p, const TestParam<T> &param,
                     int max_bit_range) {
    EXPECT_GE(12, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    Randomize(p, bw * bh, max_bit_range);
  }

  void RandomInput16(int16_t *p, const TestParam<T> &param, int max_bit_range) {
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    Randomize(p, bw * bh, max_bit_range);
  }

  void RandomInput8Extreme(uint8_t *p, const TestParam<T> &param) {
    EXPECT_EQ(8, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    RandomizeExtreme(p, bw * bh);
  }

  void RandomInput9Extreme(int16_t *p, const TestParam<T> &param,
                           int max_bit_range) {
    EXPECT_GE(12, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    Randomize9Extreme(p, bw * bh, max_bit_range);
  }

  void RandomInput16Extreme(uint16_t *p, const TestParam<T> &param,
                            int max_bit_range) {
    EXPECT_GE(12, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    RandomizeExtreme(p, bw * bh, max_bit_range);
  }

  void RandomInput16Extreme(int16_t *p, const TestParam<T> &param,
                            int max_bit_range) {
    EXPECT_GE(12, param.BitDepth());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Width());
    EXPECT_GE(MAX_SB_SIZE, param.Block().Height());
    const int bw = param.Block().Width();
    const int bh = param.Block().Height();
    RandomizeExtreme(p, bw * bh, max_bit_range);
  }

 private:
  void Randomize(uint8_t *p, int size) {
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand8();
    }
  }

  void Randomize(uint16_t *p, int size, int max_bit_range) {
    assert(max_bit_range <= 16);
    for (int i = 0; i < size; ++i) {
      p[i] = rnd_.Rand16() & ((1 << max_bit_range) - 1);
    }
  }

  void Randomize(int16_t *p, int size, int max_bit_range) {
    assert(max_bit_range <= 16);
    for (int i = 0; i < size; ++i) {
      p[i] = (rnd_.Rand16() & ((1 << max_bit_range) - 1)) -
             (1 << (max_bit_range - 1));
    }
  }

  int RandBool() {
    const uint32_t value = rnd_.Rand8();
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 7) & 0x1;
  }

  uint8_t Rand8Extremes() { return static_cast<uint8_t>(RandBool() ? 255 : 0); }

  int8_t Rand8SingedExtremes(int max_bit_range) {
    const int max_val = (1 << max_bit_range) - 1;
    const int half_max_val = 1 << (max_bit_range - 1);
    uint8_t r_u8 = Rand8Extremes() & max_val;
    return static_cast<int8_t>(r_u8 - half_max_val);
  }

  int16_t Rand9SingedExtremes(int max_bit_range) {
    const int half_max_val = 1 << (max_bit_range - 1);
    uint16_t r_u16 = Rand16Extremes(max_bit_range);
    return static_cast<int16_t>(r_u16 - half_max_val);
  }

  uint16_t Rand16Extremes(int max_bit_range) {
    const int max_val = (1 << max_bit_range) - 1;
    return static_cast<uint16_t>(RandBool() ? max_val : 0);
  }

  int16_t Rand16SingedExtremes(int max_bit_range) {
    const int half_max_val = 1 << (max_bit_range - 1);
    uint16_t r_u16 = Rand16Extremes(max_bit_range);
    return static_cast<int16_t>(r_u16 - half_max_val);
  }

  void RandomizeExtreme(uint8_t *p, int size) {
    for (int i = 0; i < size; ++i) {
      p[i] = Rand8Extremes();
    }
  }

  void RandomizeExtreme(uint16_t *p, int size, int max_bit_range) {
    for (int i = 0; i < size; ++i) {
      p[i] = Rand16Extremes(max_bit_range);
    }
  }

  void RandomizeExtreme(int16_t *p, int size, int max_bit_range) {
    for (int i = 0; i < size; ++i) {
      p[i] = Rand16SingedExtremes(max_bit_range);
    }
  }

  void Randomize9Extreme(int16_t *p, int size, int max_bit_range) {
    for (int i = 0; i < size; ++i) {
      p[i] = Rand9SingedExtremes(max_bit_range);
    }
  }

  libavm_test::ACMRandom rnd_;
};

// a function to generate test parameters for just luma block sizes.
template <typename T>
std::vector<TestParam<T>> GetOptFlowTestParams(
    std::initializer_list<int> bit_depths, T test_func) {
  std::set<BlockSize> sizes;
  for (int bsize = BLOCK_8X8; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const int w = block_size_wide[bsize];
    const int h = block_size_high[bsize];
    if (w < 8 || h < 8) continue;
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
std::vector<TestParam<T>> GetOptFlowHighbdTestParams(T test_func) {
  return GetOptFlowTestParams({ 8, 10, 12 }, test_func);
}

template <typename T>
::testing::internal::ParamGenerator<TestParam<T>> BuildOptFlowHighbdParams(
    T test_func) {
  return ::testing::ValuesIn(GetOptFlowHighbdTestParams(test_func));
}

#if OPFL_BICUBIC_GRAD
typedef void (*bicubic_grad_interp_highbd)(const int16_t *pred_src,
                                           int16_t *x_grad, int16_t *y_grad,
                                           const int stride,
                                           const int blk_width,
                                           const int blk_height);

class AV2OptFlowBiCubicGradHighbdTest
    : public AV2OptFlowTest<bicubic_grad_interp_highbd> {
 public:
  AV2OptFlowBiCubicGradHighbdTest() {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();

    pred_src_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(int16_t));
    x_grad_ref_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(int16_t));
    y_grad_ref_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(int16_t));
    x_grad_test_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(int16_t));
    y_grad_test_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(int16_t));

    memset(x_grad_ref_, 0, bw * bh * sizeof(int16_t));
    memset(y_grad_ref_, 0, bw * bh * sizeof(int16_t));
    memset(x_grad_test_, 0, bw * bh * sizeof(int16_t));
    memset(y_grad_test_, 0, bw * bh * sizeof(int16_t));
  }

  ~AV2OptFlowBiCubicGradHighbdTest() {
    avm_free(pred_src_);
    avm_free(x_grad_ref_);
    avm_free(y_grad_ref_);
    avm_free(x_grad_test_);
    avm_free(y_grad_test_);
  }

  void Run(const int is_speed) {
    const BlockSize &block = GetParam().Block();
    const int bd = GetParam().BitDepth();
    const int bw_log2 = block.Width() >> MI_SIZE_LOG2;
    const int bh_log2 = block.Height() >> MI_SIZE_LOG2;
    const int numIter = is_speed ? 1 : 16384 / (bw_log2 * bh_log2);

    for (int count = 0; count < numIter; count++) {
      RandomInput16(pred_src_, GetParam(), bd);
      TestBicubicGradHighbd(pred_src_, x_grad_ref_, y_grad_ref_, x_grad_test_,
                            y_grad_test_, is_speed);
    }
    if (is_speed) return;

    for (int count = 0; count < numIter; count++) {
      RandomInput16Extreme((uint16_t *)pred_src_, GetParam(), bd);
      TestBicubicGradHighbd(pred_src_, x_grad_ref_, y_grad_ref_, x_grad_test_,
                            y_grad_test_, 0);
    }
  }

 private:
  void TestBicubicGradHighbd(int16_t *pred_src, int16_t *x_grad_ref,
                             int16_t *y_grad_ref, int16_t *x_grad_test,
                             int16_t *y_grad_test, int is_speed) {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();

    bicubic_grad_interp_highbd ref_func =
        av2_bicubic_grad_interpolation_highbd_c;
    bicubic_grad_interp_highbd test_func = GetParam().TestFunction();
    if (is_speed)
      BicubicGradHighbdSpeed(ref_func, test_func, pred_src, x_grad_ref,
                             y_grad_ref, x_grad_test, y_grad_test, bw, bh);
    else
      BicubicGradHighbd(ref_func, test_func, pred_src, x_grad_ref, y_grad_ref,
                        x_grad_test, y_grad_test, bw, bh);
  }

  void BicubicGradHighbd(bicubic_grad_interp_highbd ref_func,
                         bicubic_grad_interp_highbd test_func,
                         const int16_t *pred_src, int16_t *x_grad_ref,
                         int16_t *y_grad_ref, int16_t *x_grad_test,
                         int16_t *y_grad_test, const int bw, const int bh) {
    ref_func(pred_src, x_grad_ref, y_grad_ref, bw, bw, bh);
    test_func(pred_src, x_grad_test, y_grad_test, bw, bw, bh);

    AssertOutputBufferEq(x_grad_ref, x_grad_test, bw, bh, bw);
    AssertOutputBufferEq(y_grad_ref, y_grad_test, bw, bh, bw);
  }

  void BicubicGradHighbdSpeed(bicubic_grad_interp_highbd ref_func,
                              bicubic_grad_interp_highbd test_func,
                              int16_t *pred_src, int16_t *x_grad_ref,
                              int16_t *y_grad_ref, int16_t *x_grad_test,
                              int16_t *y_grad_test, const int bw,
                              const int bh) {
    const int bw_log2 = bw >> MI_SIZE_LOG2;
    const int bh_log2 = bh >> MI_SIZE_LOG2;

    const int numIter = 2097152 / (bw_log2 * bh_log2);
    avm_usec_timer timer_ref;
    avm_usec_timer timer_test;

    avm_usec_timer_start(&timer_ref);
    for (int count = 0; count < numIter; count++)
      ref_func(pred_src, x_grad_ref, y_grad_ref, bw, bw, bh);
    avm_usec_timer_mark(&timer_ref);

    avm_usec_timer_start(&timer_test);
    for (int count = 0; count < numIter; count++)
      test_func(pred_src, x_grad_test, y_grad_test, bw, bw, bh);
    avm_usec_timer_mark(&timer_test);

    const int total_time_ref =
        static_cast<int>(avm_usec_timer_elapsed(&timer_ref));
    const int total_time_test =
        static_cast<int>(avm_usec_timer_elapsed(&timer_test));

    printf("ref_time = %d \t simd_time = %d \t Gain = %4.2f \n", total_time_ref,
           total_time_test,
           (static_cast<float>(total_time_ref) /
            static_cast<float>(total_time_test)));
  }

  int16_t *pred_src_;
  int16_t *x_grad_ref_;
  int16_t *y_grad_ref_;
  int16_t *x_grad_test_;
  int16_t *y_grad_test_;
};
TEST_P(AV2OptFlowBiCubicGradHighbdTest, CheckOutput) { Run(0); }
TEST_P(AV2OptFlowBiCubicGradHighbdTest, DISABLED_Speed) { Run(1); }

INSTANTIATE_TEST_SUITE_P(
    C, AV2OptFlowBiCubicGradHighbdTest,
    BuildOptFlowHighbdParams(av2_bicubic_grad_interpolation_highbd_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2OptFlowBiCubicGradHighbdTest,
    BuildOptFlowHighbdParams(av2_bicubic_grad_interpolation_highbd_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2OptFlowBiCubicGradHighbdTest,
    BuildOptFlowHighbdParams(av2_bicubic_grad_interpolation_highbd_avx2));
#endif
#endif  // OPFL_BICUBIC_GRAD

typedef int (*opfl_mv_refinement)(const int16_t *pdiff, int pstride,
                                  const int16_t *gx, const int16_t *gy,
                                  int gstride, int bw, int bh, int n, int d0,
                                  int d1, int grad_prec_bits, int mv_prec_bits,
                                  int mi_x, int mi_y, int mi_cols, int mi_rows,
                                  int build_for_decode, int *vx0, int *vy0,
                                  int *vx1, int *vy1);

class AV2OptFlowRefineTest : public AV2OptFlowTest<opfl_mv_refinement> {
 public:
  AV2OptFlowRefineTest() {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();

    input_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(*input_));
    gx_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(*gx_));
    gy_ = (int16_t *)avm_memalign(16, bw * bh * sizeof(*gy_));
  }

  ~AV2OptFlowRefineTest() {
    avm_free(input_);
    avm_free(gx_);
    avm_free(gy_);
  }

  void RunTest(const int is_speed) {
    OrderHintInfo oh_info;
    const BlockSize &block = GetParam().Block();
    const int bd = GetParam().BitDepth();
    const int bw_log2 = block.Width() >> MI_SIZE_LOG2;
    const int bh_log2 = block.Height() >> MI_SIZE_LOG2;
    const int numIter = is_speed ? 1 : 16384 / (bw_log2 * bh_log2);
    const int oh_start_bits = is_speed ? kMaxOrderHintBits : 1;

    for (int oh_bits = oh_start_bits; oh_bits <= kMaxOrderHintBits; oh_bits++) {
      for (int count = 0; count < numIter;) {
        const int cur_frm_idx = RandomFrameIdx(oh_bits);
        const int ref0_frm_idx = RandomFrameIdx(oh_bits);
        const int ref1_frm_idx = RandomFrameIdx(oh_bits);

        oh_info.order_hint_bits_minus_1 = oh_bits - 1;
        int d0 = get_relative_dist(&oh_info, cur_frm_idx, ref0_frm_idx);
        int d1 = get_relative_dist(&oh_info, cur_frm_idx, ref1_frm_idx);
        if (!d0 || !d1) continue;
        reduce_temporal_dist(&d0, &d1);

        // Here, the input corresponds to 'd0*p0 - d1*p1' (where P0 and P1 can
        // be 12 bits, abs(d0)+abs(d1) can be >=2 bits) and gx, gy are gradients
        // of input. Due to the clamping of these value to
        // [-OPFL_GRAD_CLAMP_VAL, OPFL_GRAD_CLAMP_VAL], with
        // OPFL_GRAD_CLAMP_VAL ((1 << ((MAX_OPFL_AUTOCORR_BITS - 6) >> 1)) - 1),
        // testing of the same is required. Hence, populating the input_, gx_
        // and gy_ buffers as per the requirement.
        RandomInput16(input_, GetParam(), AVMMIN(16, bd + 1));
        RandomInput16(gx_, GetParam(),
                      AVMMIN(16, (MAX_OPFL_AUTOCORR_BITS - 6) >> 1));
        RandomInput16(gy_, GetParam(),
                      AVMMIN(16, (MAX_OPFL_AUTOCORR_BITS - 6) >> 1));

        TestOptFlowRefine(input_, gx_, gy_, is_speed, d0, d1);
        count++;
      }
    }
    if (is_speed) return;

    // Extreme value test
    for (int oh_bits = oh_start_bits; oh_bits <= kMaxOrderHintBits;
         oh_bits += kMaxOrderHintBits - 1) {
      for (int count = 0; count < numIter;) {
        int d0 = RelativeDistExtreme(oh_bits);
        int d1 = RelativeDistExtreme(oh_bits);
        if (!d0 || !d1) continue;
        reduce_temporal_dist(&d0, &d1);

        RandomInput16Extreme(input_, GetParam(), AVMMIN(16, bd + 1));
        RandomInput16Extreme(gx_, GetParam(),
                             AVMMIN(16, (MAX_OPFL_AUTOCORR_BITS - 6) >> 1));
        RandomInput16Extreme(gy_, GetParam(),
                             AVMMIN(16, (MAX_OPFL_AUTOCORR_BITS - 6) >> 1));

        TestOptFlowRefine(input_, gx_, gy_, 0, d0, d1);
        count++;
      }
    }
  }

 private:
  void TestOptFlowRefine(int16_t *input, int16_t *gx, int16_t *gy,
                         const int is_speed, int d0, int d1) {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();
    const int n = block.OptFlowBlkSize();

    opfl_mv_refinement ref_func = av2_opfl_mv_refinement_nxn_c;
    opfl_mv_refinement test_func = GetParam().TestFunction();

    if (is_speed)
      OptFlowRefineSpeed(ref_func, test_func, input, gx, gy, bw, bh, n, d0, d1);
    else
      OptFlowRefine(ref_func, test_func, input, gx, gy, bw, bh, n, d0, d1);
  }

  void OptFlowRefine(opfl_mv_refinement ref_func, opfl_mv_refinement test_func,
                     const int16_t *input, const int16_t *gx, const int16_t *gy,
                     int bw, int bh, int n, int d0, int d1) {
    int ref_out[4 * N_OF_OFFSETS] = { 0 };
    int test_out[4 * N_OF_OFFSETS] = { 0 };
    const int grad_prec_bits = 3 - kSubpelGradDeltaBits - 2;
    const int mv_prec_bits = MV_REFINE_PREC_BITS;
    int stride = bw;
    int gstride = bw;

    int n_blocks_ref = ref_func(
        input, stride, gx, gy, gstride, bw, bh, n, d0, d1, grad_prec_bits,
        mv_prec_bits, 0, 0, 0, 0, 0, &ref_out[kVX_0 * N_OF_OFFSETS],
        &ref_out[kVY_0 * N_OF_OFFSETS], &ref_out[kVX_1 * N_OF_OFFSETS],
        &ref_out[kVY_1 * N_OF_OFFSETS]);
    int n_blocks = test_func(
        input, stride, gx, gy, gstride, bw, bh, n, d0, d1, grad_prec_bits,
        mv_prec_bits, 0, 0, 0, 0, 0, &test_out[kVX_0 * N_OF_OFFSETS],
        &test_out[kVY_0 * N_OF_OFFSETS], &test_out[kVX_1 * N_OF_OFFSETS],
        &test_out[kVY_1 * N_OF_OFFSETS]);

    ASSERT_EQ(n_blocks_ref, n_blocks) << "Mismatch of subblock numbers";
    AssertOutputEq(&ref_out[kVX_0 * N_OF_OFFSETS],
                   &test_out[kVX_0 * N_OF_OFFSETS], n_blocks);
    AssertOutputEq(&ref_out[kVY_0 * N_OF_OFFSETS],
                   &test_out[kVY_0 * N_OF_OFFSETS], n_blocks);
    AssertOutputEq(&ref_out[kVX_1 * N_OF_OFFSETS],
                   &test_out[kVX_1 * N_OF_OFFSETS], n_blocks);
    AssertOutputEq(&ref_out[kVY_1 * N_OF_OFFSETS],
                   &test_out[kVY_1 * N_OF_OFFSETS], n_blocks);
  }

  void OptFlowRefineSpeed(opfl_mv_refinement ref_func,
                          opfl_mv_refinement test_func, const int16_t *input,
                          const int16_t *gx, const int16_t *gy, int bw, int bh,
                          int n, int d0, int d1) {
    int ref_out[4 * N_OF_OFFSETS] = { 0 };
    int test_out[4 * N_OF_OFFSETS] = { 0 };
    const int grad_prec_bits = 3 - kSubpelGradDeltaBits - 2;
    const int mv_prec_bits = MV_REFINE_PREC_BITS;
    const int bw_log2 = bw >> MI_SIZE_LOG2;
    const int bh_log2 = bh >> MI_SIZE_LOG2;
    int stride = bw;
    int gstride = bw;

    const int numIter = 2097152 / (bw_log2 * bh_log2);
    avm_usec_timer timer_ref;
    avm_usec_timer timer_test;

    avm_usec_timer_start(&timer_ref);
    for (int count = 0; count < numIter; count++) {
      ref_func(input, stride, gx, gy, gstride, bw, bh, n, d0, d1,
               grad_prec_bits, mv_prec_bits, 0, 0, 0, 0, 0,
               &ref_out[kVX_0 * N_OF_OFFSETS], &ref_out[kVY_0 * N_OF_OFFSETS],
               &ref_out[kVX_1 * N_OF_OFFSETS], &ref_out[kVY_1 * N_OF_OFFSETS]);
    }
    avm_usec_timer_mark(&timer_ref);

    avm_usec_timer_start(&timer_test);
    for (int count = 0; count < numIter; count++) {
      test_func(
          input, stride, gx, gy, gstride, bw, bh, n, d0, d1, grad_prec_bits,
          mv_prec_bits, 0, 0, 0, 0, 0, &test_out[kVX_0 * N_OF_OFFSETS],
          &test_out[kVY_0 * N_OF_OFFSETS], &test_out[kVX_1 * N_OF_OFFSETS],
          &test_out[kVY_1 * N_OF_OFFSETS]);
    }
    avm_usec_timer_mark(&timer_test);

    const int total_time_ref =
        static_cast<int>(avm_usec_timer_elapsed(&timer_ref));
    const int total_time_test =
        static_cast<int>(avm_usec_timer_elapsed(&timer_test));

    printf(
        "Block size: %dx%d \t ref_time = %d \t simd_time = %d \t Gain = %4.2f "
        "\n",
        bw, bh, total_time_ref, total_time_test,
        (static_cast<float>(total_time_ref) /
         static_cast<float>(total_time_test)));
  }

  static constexpr int kVX_0 = 0;
  static constexpr int kVX_1 = 1;
  static constexpr int kVY_0 = 2;
  static constexpr int kVY_1 = 3;
  static constexpr int kMaxOrderHintBits = 8;
  static constexpr int kSubpelGradDeltaBits = 2;
  int16_t *input_;
  int16_t *gx_;
  int16_t *gy_;
};
TEST_P(AV2OptFlowRefineTest, CheckOutput) { RunTest(0); }
TEST_P(AV2OptFlowRefineTest, DISABLED_Speed) { RunTest(1); }

INSTANTIATE_TEST_SUITE_P(
    C, AV2OptFlowRefineTest,
    BuildOptFlowHighbdParams(av2_opfl_mv_refinement_nxn_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2OptFlowRefineTest,
    BuildOptFlowHighbdParams(av2_opfl_mv_refinement_nxn_sse4_1));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2OptFlowRefineTest,
    BuildOptFlowHighbdParams(av2_opfl_mv_refinement_nxn_avx2));
#endif

#if OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
typedef void (*pred_buffer_copy_highbd)(const uint16_t *src1,
                                        const uint16_t *src2, int src_stride,
                                        int16_t *dst1, int16_t *dst2, int bw,
                                        int bh, int d0, int d1, int bd,
                                        int centered);

class AV2OptFlowCopyPredHighbdTest
    : public AV2OptFlowTest<pred_buffer_copy_highbd> {
 public:
  AV2OptFlowCopyPredHighbdTest() {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();

    src_buf1_ = (uint16_t *)avm_memalign(16, bw * bh * sizeof(*src_buf1_));
    src_buf2_ = (uint16_t *)avm_memalign(16, bw * bh * sizeof(*src_buf2_));
    dst_buf1_ref_ =
        (int16_t *)avm_memalign(16, bw * bh * sizeof(*dst_buf1_ref_));
    dst_buf2_ref_ =
        (int16_t *)avm_memalign(16, bw * bh * sizeof(*dst_buf2_ref_));
    dst_buf1_test_ =
        (int16_t *)avm_memalign(16, bw * bh * sizeof(*dst_buf1_test_));
    dst_buf2_test_ =
        (int16_t *)avm_memalign(16, bw * bh * sizeof(*dst_buf2_test_));

    memset(dst_buf2_ref_, 0, bw * bh * sizeof(*dst_buf2_ref_));
    memset(dst_buf2_test_, 0, bw * bh * sizeof(*dst_buf2_test_));
  }

  ~AV2OptFlowCopyPredHighbdTest() {
    avm_free(src_buf1_);
    avm_free(src_buf2_);
    avm_free(dst_buf1_ref_);
    avm_free(dst_buf2_ref_);
    avm_free(dst_buf1_test_);
    avm_free(dst_buf2_test_);
  }

  void Run(const int is_speed) {
    OrderHintInfo oh_info;
    const BlockSize &block = GetParam().Block();
    const int bw_log2 = block.Width() >> MI_SIZE_LOG2;
    const int bh_log2 = block.Height() >> MI_SIZE_LOG2;
    const int bd = GetParam().BitDepth();
    const int numIter = is_speed ? 1 : 16384 / (bw_log2 * bh_log2);
    const int oh_start_bits = is_speed ? kMaxOrderHintBits : 1;

    for (int oh_bits = oh_start_bits; oh_bits <= kMaxOrderHintBits; oh_bits++) {
      for (int count = 0; count < numIter;) {
        const int cur_frm_idx = RandomFrameIdx(oh_bits);
        const int ref0_frm_idx = RandomFrameIdx(oh_bits);
        const int ref1_frm_idx = RandomFrameIdx(oh_bits);

        oh_info.order_hint_bits_minus_1 = oh_bits - 1;
        int d0 = get_relative_dist(&oh_info, cur_frm_idx, ref0_frm_idx);
        int d1 = get_relative_dist(&oh_info, cur_frm_idx, ref1_frm_idx);
        if (!d0 || !d1) continue;
        reduce_temporal_dist(&d0, &d1);

        RandomInput16(src_buf1_, GetParam(), bd);
        RandomInput16(src_buf2_, GetParam(), bd);
        TestCopyPredArray(src_buf1_, src_buf2_, dst_buf1_ref_, dst_buf2_ref_,
                          dst_buf1_test_, dst_buf2_test_, d0, d1, is_speed);
        count++;
      }
    }
    if (is_speed) return;

    // Extreme value test
    for (int oh_bits = oh_start_bits; oh_bits <= kMaxOrderHintBits;
         oh_bits += kMaxOrderHintBits - 1) {
      for (int count = 0; count < numIter;) {
        int d0 = RelativeDistExtreme(oh_bits);
        int d1 = RelativeDistExtreme(oh_bits);
        if (!d0 || !d1) continue;
        reduce_temporal_dist(&d0, &d1);

        RandomInput16Extreme(src_buf1_, GetParam(), bd);
        RandomInput16Extreme(src_buf2_, GetParam(), bd);
        TestCopyPredArray(src_buf1_, src_buf2_, dst_buf1_ref_, dst_buf2_ref_,
                          dst_buf1_test_, dst_buf2_test_, d0, d1, 0);
        count++;
      }
    }
  }

 private:
  void TestCopyPredArray(uint16_t *src_buf1, uint16_t *src_buf2,
                         int16_t *dst_buf1_ref, int16_t *dst_buf2_ref,
                         int16_t *dst_buf1_test, int16_t *dst_buf2_test, int d0,
                         int d1, int is_speed) {
    const BlockSize &block = GetParam().Block();
    const int bw = block.Width();
    const int bh = block.Height();

    pred_buffer_copy_highbd ref_func = av2_copy_pred_array_highbd_c;
    pred_buffer_copy_highbd test_func = GetParam().TestFunction();
    if (is_speed)
      CopyPredArraySpeed(ref_func, test_func, src_buf1, src_buf2, dst_buf1_ref,
                         dst_buf2_ref, dst_buf1_test, dst_buf2_test, d0, d1, bw,
                         bh);
    else
      CopyPredArray(ref_func, test_func, src_buf1, src_buf2, dst_buf1_ref,
                    dst_buf2_ref, dst_buf1_test, dst_buf2_test, d0, d1, bw, bh);
  }

  void CopyPredArray(pred_buffer_copy_highbd ref_func,
                     pred_buffer_copy_highbd test_func,
                     const uint16_t *src_buf1, uint16_t *src_buf2,
                     int16_t *dst_buf1_ref, int16_t *dst_buf2_ref,
                     int16_t *dst_buf1_test, int16_t *dst_buf2_test,
                     const int d0, const int d1, const int bw, const int bh) {
    const int bd = GetParam().BitDepth();
    ref_func(src_buf1, src_buf2, bw, dst_buf1_ref, dst_buf2_ref, bw, bh, d0, d1,
             bd, 0);
    test_func(src_buf1, src_buf2, bw, dst_buf1_test, dst_buf2_test, bw, bh, d0,
              d1, bd, 0);
    AssertOutputBufferEq(dst_buf1_ref, dst_buf1_test, bw, bh, bw);
    AssertOutputBufferEq(dst_buf2_ref, dst_buf2_test, bw, bh, bw);
  }

  void CopyPredArraySpeed(pred_buffer_copy_highbd ref_func,
                          pred_buffer_copy_highbd test_func,
                          const uint16_t *src_buf1, uint16_t *src_buf2,
                          int16_t *dst_buf1_ref, int16_t *dst_buf2_ref,
                          int16_t *dst_buf1_test, int16_t *dst_buf2_test,
                          const int d0, const int d1, const int bw,
                          const int bh) {
    const int bw_log2 = bw >> MI_SIZE_LOG2;
    const int bh_log2 = bh >> MI_SIZE_LOG2;
    printf("bw=%d, bh=%d\n", bw, bh);
    const int numIter = 2097152 / (bw_log2 * bh_log2);
    const int bd = GetParam().BitDepth();
    avm_usec_timer timer_ref;
    avm_usec_timer timer_test;

    avm_usec_timer_start(&timer_ref);
    for (int count = 0; count < numIter; count++)
      ref_func(src_buf1, src_buf2, bw, dst_buf1_ref, dst_buf2_ref, bw, bh, d0,
               d1, bd, 0);
    avm_usec_timer_mark(&timer_ref);

    avm_usec_timer_start(&timer_test);
    for (int count = 0; count < numIter; count++)
      test_func(src_buf1, src_buf2, bw, dst_buf1_test, dst_buf2_test, bw, bh,
                d0, d1, bd, 0);
    avm_usec_timer_mark(&timer_test);

    const int total_time_ref =
        static_cast<int>(avm_usec_timer_elapsed(&timer_ref));
    const int total_time_test =
        static_cast<int>(avm_usec_timer_elapsed(&timer_test));

    printf("ref_time = %d \t simd_time = %d \t Gain = %4.2f \n", total_time_ref,
           total_time_test,
           (static_cast<float>(total_time_ref) /
            static_cast<float>(total_time_test)));
  }

  uint16_t *src_buf1_;
  uint16_t *src_buf2_;
  int16_t *dst_buf1_ref_;
  int16_t *dst_buf2_ref_;
  int16_t *dst_buf1_test_;
  int16_t *dst_buf2_test_;
  static constexpr int kMaxOrderHintBits = 8;
};

TEST_P(AV2OptFlowCopyPredHighbdTest, CheckOutput) { Run(0); }
TEST_P(AV2OptFlowCopyPredHighbdTest, DISABLED_Speed) { Run(1); }

INSTANTIATE_TEST_SUITE_P(
    C, AV2OptFlowCopyPredHighbdTest,
    BuildOptFlowHighbdParams(av2_copy_pred_array_highbd_c));

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, AV2OptFlowCopyPredHighbdTest,
    BuildOptFlowHighbdParams(av2_copy_pred_array_highbd_sse4_1));
#endif
#endif  // OPFL_BILINEAR_GRAD || OPFL_BICUBIC_GRAD
}  // namespace
