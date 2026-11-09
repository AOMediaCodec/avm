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

#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "avm/avm_codec.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

typedef unsigned int (*SadMxNFunc)(const uint16_t *src_ptr, int src_stride,
                                   const uint16_t *ref_ptr, int ref_stride);
typedef std::tuple<int, int, SadMxNFunc, int> SadMxNParam;

typedef unsigned int (*SadDSMxNFunc)(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride);
typedef std::tuple<int, int, SadDSMxNFunc, int> SadDSMxNParam;

typedef unsigned int (*SadSkipMxNFunc)(const uint16_t *src_ptr, int src_stride,
                                       const uint16_t *ref_ptr, int ref_stride);
typedef std::tuple<int, int, SadSkipMxNFunc, int> SadSkipMxNParam;

typedef uint32_t (*SadMxNAvgFunc)(const uint16_t *src_ptr, int src_stride,
                                  const uint16_t *ref_ptr, int ref_stride,
                                  const uint16_t *second_pred);
typedef std::tuple<int, int, SadMxNAvgFunc, int> SadMxNAvgParam;

typedef void (*CwpFunc)(uint16_t *comp_pred, const uint16_t *pred, int width,
                        int height, const uint16_t *ref, int ref_stride,
                        const CWP_PARAMS *cwp_param);
typedef std::tuple<int, int, CwpFunc, int> CwpParam;

typedef unsigned int (*CwpSadMxhFunc)(const uint16_t *src_ptr, int src_stride,
                                      const uint16_t *ref_ptr, int ref_stride,
                                      int width, int height);
typedef std::tuple<int, int, CwpSadMxhFunc, int> CwpSadMxhParam;

typedef uint32_t (*CwpSadMxNAvgFunc)(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *second_pred,
                                     const CWP_PARAMS *cwp_param);
typedef std::tuple<int, int, CwpSadMxNAvgFunc, int> CwpSadMxNAvgParam;

typedef void (*SadMxNx4Func)(const uint16_t *src_ptr, int src_stride,
                             const uint16_t *const ref_ptr[], int ref_stride,
                             uint32_t *sad_array);
typedef std::tuple<int, int, SadMxNx4Func, int> SadMxNx4Param;

typedef void (*SadSkipMxNx4Func)(const uint16_t *src_ptr, int src_stride,
                                 const uint16_t *const ref_ptr[],
                                 int ref_stride, uint32_t *sad_array);
typedef std::tuple<int, int, SadSkipMxNx4Func, int> SadSkipMxNx4Param;

typedef void (*SadMxNx4AvgFunc)(const uint16_t *src_ptr, int src_stride,
                                const uint16_t *const ref_ptr[], int ref_stride,
                                const uint16_t *second_pred,
                                uint32_t *sad_array);
typedef std::tuple<int, int, SadMxNx4AvgFunc, int> SadMxNx4AvgParam;

using libavm_test::ACMRandom;

namespace {
class SADTestBase : public ::testing::Test {
 public:
  SADTestBase(int width, int height, int bit_depth)
      : width_(width), height_(height), bd_(bit_depth) {}

  static void SetUpTestSuite() {
    source_data16_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kDataBlockSize * sizeof(uint16_t)));
    reference_data16_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kDataBufferSize * sizeof(uint16_t)));
    second_pred16_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kDataBlockSize * sizeof(uint16_t)));
    comp_pred16_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kDataBlockSize * sizeof(uint16_t)));
    comp_pred16_test_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kDataBlockSize * sizeof(uint16_t)));
  }

  static void TearDownTestSuite() {
    avm_free(source_data16_);
    source_data16_ = NULL;
    avm_free(reference_data16_);
    reference_data16_ = NULL;
    avm_free(second_pred16_);
    second_pred16_ = NULL;
    avm_free(comp_pred16_);
    comp_pred16_ = NULL;
    avm_free(comp_pred16_test_);
    comp_pred16_test_ = NULL;
  }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  // Handle up to 4 256x256 blocks, with stride up to 256
  static const int kDataAlignment = 16;
  static const int kMaxDim = 256;
  static const int kDataBlockSize = kMaxDim * (kMaxDim + 1);
  static const int kDataBufferSize = 4 * kDataBlockSize;

  virtual void SetUp() {
    bit_depth_ = static_cast<avm_bit_depth_t>(bd_);
    source_data_ = source_data16_;
    reference_data_ = reference_data16_;
    second_pred_ = second_pred16_;
    comp_pred_ = comp_pred16_;
    comp_pred_test_ = comp_pred16_test_;
    mask_ = (1 << bit_depth_) - 1;
    source_stride_ = (width_ + 31) & ~31;
    reference_stride_ = width_;
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  virtual uint16_t *GetReference(int block_idx) {
    return reference_data_ + block_idx * kDataBlockSize;
  }

  // Sum of Absolute Differences. Given two blocks, calculate the absolute
  // difference between two pixels in the same relative location; accumulate.
  unsigned int ReferenceSAD(int block_idx) {
    unsigned int sad = 0;
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const source16 = source_data_;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        sad += abs(source16[h * source_stride_ + w] -
                   reference16[h * reference_stride_ + w]);
      }
    }
    return sad;
  }

  // Sum of Absolute Differences of Downsampled rows.
  unsigned int ReferenceSADDS(int block_idx) {
    unsigned int sad = 0;
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const source16 = source_data_;
    for (int h = 0; h < height_; h += 2) {
      for (int w = 0; w < width_; ++w) {
        sad += abs(source16[h * source_stride_ + w] -
                   reference16[h * reference_stride_ + w]);
      }
    }
    return sad;
  }

  // Sum of Absolute Differences Skip rows. Given two blocks,
  // calculate the absolute  difference between two pixels in the same
  // relative location every other row; accumulate and double the result at the
  // end.
  unsigned int ReferenceSADSkip(int block_idx) {
    unsigned int sad = 0;
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const source16 = source_data_;
    for (int h = 0; h < height_; h += 2) {
      for (int w = 0; w < width_; ++w) {
        sad += abs(source16[h * source_stride_ + w] -
                   reference16[h * reference_stride_ + w]);
      }
    }
    return sad * 2;
  }

  // Sum of Absolute Differences Average. Given two blocks, and a prediction
  // calculate the absolute difference between one pixel and average of the
  // corresponding and predicted pixels; accumulate.
  unsigned int ReferenceSADavg(int block_idx) {
    unsigned int sad = 0;
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const source16 = source_data_;
    const uint16_t *const second_pred16 = second_pred_;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        const int tmp = second_pred16[h * width_ + w] +
                        reference16[h * reference_stride_ + w];
        const uint16_t comp_pred = ROUND_POWER_OF_TWO(tmp, 1);
        sad += abs(source16[h * source_stride_ + w] - comp_pred);
      }
    }
    return sad;
  }

  void ReferenceCwp(int block_idx) {
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const second_pred16 = second_pred_;
    uint16_t *const comp_pred16 = comp_pred_;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        const int tmp =
            second_pred16[h * width_ + w] * cwp_param_.bck_offset +
            reference16[h * reference_stride_ + w] * cwp_param_.fwd_offset;
        comp_pred16[h * width_ + w] = ROUND_POWER_OF_TWO(tmp, 4);
      }
    }
  }

  unsigned int ReferenceCwpSadAvg(int block_idx) {
    unsigned int sad = 0;
    const uint16_t *const reference16 = GetReference(block_idx);
    const uint16_t *const source16 = source_data_;
    const uint16_t *const second_pred16 = second_pred_;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        const int tmp =
            second_pred16[h * width_ + w] * cwp_param_.bck_offset +
            reference16[h * reference_stride_ + w] * cwp_param_.fwd_offset;
        const uint16_t comp_pred = ROUND_POWER_OF_TWO(tmp, 4);
        sad += abs(source16[h * source_stride_ + w] - comp_pred);
      }
    }
    return sad;
  }

  void FillConstant(uint16_t *data, int stride, uint16_t fill_constant) {
    uint16_t *data16 = data;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        data16[h * stride + w] = fill_constant;
      }
    }
  }

  void FillRandom(uint16_t *data, int stride) {
    uint16_t *data16 = data;
    for (int h = 0; h < height_; ++h) {
      for (int w = 0; w < width_; ++w) {
        data16[h * stride + w] = rnd_.Rand16() & mask_;
      }
    }
  }

  int width_, height_, mask_, bd_;
  avm_bit_depth_t bit_depth_;
  static uint16_t *source_data_;
  static uint16_t *reference_data_;
  static uint16_t *second_pred_;
  int source_stride_;
  static uint16_t *source_data16_;
  static uint16_t *reference_data16_;
  static uint16_t *second_pred16_;
  int reference_stride_;
  static uint16_t *comp_pred_;
  static uint16_t *comp_pred16_;
  static uint16_t *comp_pred_test_;
  static uint16_t *comp_pred16_test_;
  CWP_PARAMS cwp_param_;

  ACMRandom rnd_;
};

class SADx4Test : public ::testing::WithParamInterface<SadMxNx4Param>,
                  public SADTestBase {
 public:
  SADx4Test() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  // Buffers of the layout tests (the x4d TEST_Ps after DISABLED_Speed): one
  // source plane and one reference plane, sized for the largest stride
  // (width + 32, which covers width + 16 plus a +3 element offset), the
  // largest block (256x256) and kX4Margin rows of window above and below.
  static const int kX4MaxStride = kMaxDim + 32;
  static const int kX4Margin = 8;
  static const int kX4MaxDx = 16;
  static const int kX4Slack = 64;
  static const int kX4SrcElems = kX4MaxStride * kMaxDim + kX4Slack;
  // One region per ref when the refs are disjoint; all four regions form one
  // reference window when the refs overlap.
  static const int kX4RegionElems =
      kX4MaxStride * (kMaxDim + 2 * kX4Margin) + kX4Slack;
  static const int kX4RefElems = 4 * kX4RegionElems;
  static const int kX4Iterations = 1000;

  virtual void SetUp() {
    SADTestBase::SetUp();
    x4_src_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kX4SrcElems * sizeof(uint16_t)));
    x4_ref_ = reinterpret_cast<uint16_t *>(
        avm_memalign(kDataAlignment, kX4RefElems * sizeof(uint16_t)));
    ASSERT_NE(x4_src_, nullptr);
    ASSERT_NE(x4_ref_, nullptr);
    memset(x4_src_, 0, kX4SrcElems * sizeof(uint16_t));
    memset(x4_ref_, 0, kX4RefElems * sizeof(uint16_t));
  }

  virtual void TearDown() {
    avm_free(x4_src_);
    x4_src_ = NULL;
    avm_free(x4_ref_);
    x4_ref_ = NULL;
    SADTestBase::TearDown();
  }

  // SAD of the width_ x height_ block at src against the one at ref (the C
  // reference's definition, sad4d.c).
  unsigned int BlockSad(const uint16_t *src, int src_stride,
                        const uint16_t *ref, int ref_stride) const {
    unsigned int sad = 0;
    for (int r = 0; r < height_; ++r) {
      for (int c = 0; c < width_; ++c) {
        sad += abs(src[r * src_stride + c] - ref[r * ref_stride + c]);
      }
    }
    return sad;
  }

  // Calls the function under test once and compares each of the four SADs
  // with BlockSad of its own reference: a kernel that reuses ref 0, swaps
  // two outputs or reads with the wrong stride fails.
  void CheckX4(const uint16_t *src, int src_stride,
               const uint16_t *const refs[4], int ref_stride, const char *what,
               int iter) {
    uint32_t got[4] = { 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };
    ASM_REGISTER_STATE_CHECK(
        GET_PARAM(2)(src, src_stride, refs, ref_stride, got));
    for (int i = 0; i < 4; ++i) {
      ASSERT_EQ(BlockSad(src, src_stride, refs[i], ref_stride), got[i])
          << what << ": ref=" << i << " w=" << width_ << " h=" << height_
          << " bd=" << bd_ << " iter=" << iter << " src_stride=" << src_stride
          << " ref_stride=" << ref_stride << " ref_off=" << (refs[i] - refs[0]);
    }
  }

  // Ref i in its own region of the reference plane, at element offset
  // off[i] from the region's aligned row kX4Margin; the regions are taken in
  // the order {2, 0, 3, 1}, so ref memory order is not ref index order.
  void DisjointRefs(const int off[4], const uint16_t *refs[4]) const {
    static const int kOrder[4] = { 2, 0, 3, 1 };
    for (int i = 0; i < 4; ++i) {
      refs[i] = x4_ref_ + kOrder[i] * kX4RegionElems +
                kX4Margin * kX4MaxStride + off[i];
    }
  }

  // The encoder's layout: the four candidates in one reference window,
  // ref_array[i] = center + dy[i] * stride + dx[i] (|dy| <= kX4Margin,
  // |dx| <= kX4MaxDx), so neighbouring refs overlap.
  const uint16_t *WindowCenter(int stride, int align) const {
    return x4_ref_ + kX4Margin * stride + 2 * kX4MaxDx + align;
  }
  void WindowRefs(int stride, int align, const int dy[4], const int dx[4],
                  const uint16_t *refs[4]) const {
    const uint16_t *center = WindowCenter(stride, align);
    for (int i = 0; i < 4; ++i) refs[i] = center + dy[i] * stride + dx[i];
  }
  // Every pixel any WindowRefs candidate can read, from x4_ref_ on.
  int WindowElems(int stride) const {
    return (height_ + 2 * kX4Margin) * stride + 4 * kX4MaxDx + 4;
  }

  // Fills the width_ x height_ block at p, pixel (r, c) = f(r, c).
  template <typename F>
  void FillBlock(uint16_t *p, int stride, F f) {
    for (int r = 0; r < height_; ++r) {
      for (int c = 0; c < width_; ++c) p[r * stride + c] = f(r, c);
    }
  }
  void FillRandomN(uint16_t *p, int n) {
    for (int i = 0; i < n; ++i) p[i] = rnd_.Rand16() & mask_;
  }

  uint16_t *x4_src_ = nullptr;
  uint16_t *x4_ref_ = nullptr;

  void SADs(unsigned int *results) {
    const uint16_t *references[] = { GetReference(0), GetReference(1),
                                     GetReference(2), GetReference(3) };

    ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(
        source_data_, source_stride_, references, reference_stride_, results));
  }

  void CheckSADs() {
    unsigned int reference_sad, exp_sad[4];

    SADs(exp_sad);
    for (int block = 0; block < 4; ++block) {
      reference_sad = ReferenceSAD(block);

      EXPECT_EQ(reference_sad, exp_sad[block]) << "block " << block;
    }
  }

  void SpeedSAD() {
    const uint16_t *references[] = { GetReference(0), GetReference(1),
                                     GetReference(2), GetReference(3) };
    unsigned int exp_sad[4];

    const int test_count = 1000000000 / (GET_PARAM(0) * GET_PARAM(1));
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < test_count; ++i) {
      ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(source_data_, source_stride_,
                                            references, reference_stride_,
                                            exp_sad));
    }
    avm_usec_timer_mark(&timer);
    const double elapsed_time =
        static_cast<double>(avm_usec_timer_elapsed(&timer));
    printf("Bitdepth: %d, SADx4 %dx%d: %7.2fns\n", GET_PARAM(3), GET_PARAM(0),
           GET_PARAM(1), elapsed_time);
  }
};

class SADSkipx4Test : public ::testing::WithParamInterface<SadMxNx4Param>,
                      public SADTestBase {
 public:
  SADSkipx4Test() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  void SADs(unsigned int *results) {
    const uint16_t *references[] = { GetReference(0), GetReference(1),
                                     GetReference(2), GetReference(3) };

    ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(
        source_data_, source_stride_, references, reference_stride_, results));
  }

  void CheckSADs() {
    unsigned int reference_sad, exp_sad[4];

    SADs(exp_sad);
    for (int block = 0; block < 4; ++block) {
      reference_sad = ReferenceSADSkip(block);

      EXPECT_EQ(reference_sad, exp_sad[block]) << "block " << block;
    }
  }

  void SpeedSAD() {
    const uint16_t *references[] = { GetReference(0), GetReference(1),
                                     GetReference(2), GetReference(3) };
    unsigned int exp_sad[4];

    const int test_count = 1000000000 / (GET_PARAM(0) * GET_PARAM(1));
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < test_count; ++i) {
      ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(source_data_, source_stride_,
                                            references, reference_stride_,
                                            exp_sad));
    }
    avm_usec_timer_mark(&timer);
    const double elapsed_time =
        static_cast<double>(avm_usec_timer_elapsed(&timer));
    printf("Bitdepth: %d, SADSkipx4 %dx%d: %7.2fns\n", GET_PARAM(3),
           GET_PARAM(0), GET_PARAM(1), elapsed_time);
  }
};

class SADx4AvgTest : public ::testing::WithParamInterface<SadMxNx4AvgParam>,
                     public SADTestBase {
 public:
  SADx4AvgTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  void SADs(unsigned int *results) {
    const uint16_t *references[] = { GetReference(0), GetReference(1),
                                     GetReference(2), GetReference(3) };

    ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(source_data_, source_stride_,
                                          references, reference_stride_,
                                          second_pred_, results));
  }

  void CheckSADs() {
    unsigned int reference_sad, exp_sad[4];

    SADs(exp_sad);
    for (int block = 0; block < 4; ++block) {
      reference_sad = ReferenceSADavg(block);

      EXPECT_EQ(reference_sad, exp_sad[block]) << "block " << block;
    }
  }

  void SpeedSAD() {
    int test_count = 200000;
    unsigned int exp_sad[4];
    while (test_count > 0) {
      SADs(exp_sad);
      test_count -= 1;
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SADx4AvgTest);

class SADTest : public ::testing::WithParamInterface<SadMxNParam>,
                public SADTestBase {
 public:
  SADTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSAD(0);
    const unsigned int exp_sad = SAD(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }

  void SpeedSAD() {
    int test_count = 20000000;
    while (test_count > 0) {
      SAD(0);
      test_count -= 1;
    }
  }
};

class SADDSTest : public ::testing::WithParamInterface<SadDSMxNParam>,
                  public SADTestBase {
 public:
  SADDSTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD_ds(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSADDS(0);
    const unsigned int exp_sad = SAD_ds(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }

  void SpeedSAD() {
    int test_count = 20000000;
    while (test_count > 0) {
      SAD_ds(0);
      test_count -= 1;
    }
  }
};

class SADSkipTest : public ::testing::WithParamInterface<SadMxNParam>,
                    public SADTestBase {
 public:
  SADSkipTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSADSkip(0);
    const unsigned int exp_sad = SAD(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }

  void SpeedSAD() {
    int test_count = 20000000;
    while (test_count > 0) {
      SAD(0);
      test_count -= 1;
    }
  }
};

class SADavgTest : public ::testing::WithParamInterface<SadMxNAvgParam>,
                   public SADTestBase {
 public:
  SADavgTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD_avg(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_,
                                                second_pred_));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSADavg(0);
    const unsigned int exp_sad = SAD_avg(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }
};

class CwpTest : public ::testing::WithParamInterface<CwpParam>,
                public SADTestBase {
 public:
  CwpTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  void CompWtdPred(int block_idx) {
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(GET_PARAM(2)(comp_pred_test_, second_pred_, width_,
                                          height_, reference, reference_stride_,
                                          &cwp_param_));
  }

  void CheckCompAvg() {
    for (int j = 0; j < 2; ++j) {
      for (int i = 0; i < 4; ++i) {
        cwp_param_.fwd_offset = quant_dist_lookup_table[i][j];
        cwp_param_.bck_offset = quant_dist_lookup_table[i][1 - j];

        ReferenceCwp(0);
        CompWtdPred(0);

        for (int y = 0; y < height_; ++y)
          for (int x = 0; x < width_; ++x)
            ASSERT_EQ(comp_pred_[y * width_ + x],
                      comp_pred_test_[y * width_ + x]);
      }
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CwpTest);

class CwpSadTest : public ::testing::WithParamInterface<CwpSadMxhParam>,
                   public SADTestBase {
 public:
  CwpSadTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int SAD(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_,
                                                GET_PARAM(0), GET_PARAM(1)));
    return ret;
  }

  void CheckSAD() {
    const unsigned int reference_sad = ReferenceSAD(0);
    const unsigned int exp_sad = SAD(0);

    ASSERT_EQ(reference_sad, exp_sad);
  }

  void SpeedSAD() {
    int test_count = 20000000;
    while (test_count > 0) {
      SAD(0);
      test_count -= 1;
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CwpSadTest);

class CwpSadAvgTest : public ::testing::WithParamInterface<CwpSadMxNAvgParam>,
                      public SADTestBase {
 public:
  CwpSadAvgTest() : SADTestBase(GET_PARAM(0), GET_PARAM(1), GET_PARAM(3)) {}

 protected:
  unsigned int CwpSadAvg(int block_idx) {
    unsigned int ret;
    const uint16_t *const reference = GetReference(block_idx);

    ASM_REGISTER_STATE_CHECK(ret = GET_PARAM(2)(source_data_, source_stride_,
                                                reference, reference_stride_,
                                                second_pred_, &cwp_param_));
    return ret;
  }

  void CheckSAD() {
    for (int j = 0; j < 2; ++j) {
      for (int i = 0; i < 4; ++i) {
        cwp_param_.fwd_offset = quant_dist_lookup_table[i][j];
        cwp_param_.bck_offset = quant_dist_lookup_table[i][1 - j];

        const unsigned int reference_sad = ReferenceCwpSadAvg(0);
        const unsigned int exp_sad = CwpSadAvg(0);

        ASSERT_EQ(reference_sad, exp_sad);
      }
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CwpSadAvgTest);

uint16_t *SADTestBase::source_data_ = NULL;
uint16_t *SADTestBase::reference_data_ = NULL;
uint16_t *SADTestBase::second_pred_ = NULL;
uint16_t *SADTestBase::comp_pred_ = NULL;
uint16_t *SADTestBase::comp_pred_test_ = NULL;
uint16_t *SADTestBase::source_data16_ = NULL;
uint16_t *SADTestBase::reference_data16_ = NULL;
uint16_t *SADTestBase::second_pred16_ = NULL;
uint16_t *SADTestBase::comp_pred16_ = NULL;
uint16_t *SADTestBase::comp_pred16_test_ = NULL;

TEST_P(SADTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD();
}

TEST_P(SADTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD();
}

TEST_P(SADTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADTest, DISABLED_Speed) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  SpeedSAD();
  source_stride_ = tmp_stride;
}

TEST_P(SADDSTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD();
}

TEST_P(SADDSTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD();
}

TEST_P(SADDSTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADDSTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADDSTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADDSTest, DISABLED_Speed) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  SpeedSAD();
  source_stride_ = tmp_stride;
}

TEST_P(SADSkipTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD();
}

TEST_P(SADSkipTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD();
}

TEST_P(SADSkipTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADSkipTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADSkipTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADSkipTest, DISABLED_Speed) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  SpeedSAD();
  source_stride_ = tmp_stride;
}

TEST_P(SADavgTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}
TEST_P(SADavgTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}

TEST_P(SADavgTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADavgTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADavgTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    FillRandom(second_pred_, width_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(CwpTest, MaxRef) {
  FillConstant(reference_data_, reference_stride_, mask_);
  FillConstant(second_pred_, width_, 0);
  CheckCompAvg();
}

TEST_P(CwpTest, MaxSecondPred) {
  FillConstant(reference_data_, reference_stride_, 0);
  FillConstant(second_pred_, width_, mask_);
  CheckCompAvg();
}

TEST_P(CwpTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckCompAvg();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckCompAvg();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpSadTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  CheckSAD();
}

TEST_P(CwpSadTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  CheckSAD();
}

TEST_P(CwpSadTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpSadTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpSadTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(CwpSadAvgTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(reference_data_, reference_stride_, mask_);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}
TEST_P(CwpSadAvgTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(reference_data_, reference_stride_, 0);
  FillConstant(second_pred_, width_, 0);
  CheckSAD();
}

TEST_P(CwpSadAvgTest, ShortRef) {
  const int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpSadAvgTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  const int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(reference_data_, reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(CwpSadAvgTest, ShortSrc) {
  const int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 2000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(reference_data_, reference_stride_);
    FillRandom(second_pred_, width_);
    CheckSAD();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADx4Test, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(GetReference(0), reference_stride_, mask_);
  FillConstant(GetReference(1), reference_stride_, mask_);
  FillConstant(GetReference(2), reference_stride_, mask_);
  FillConstant(GetReference(3), reference_stride_, mask_);
  CheckSADs();
}

TEST_P(SADx4Test, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(GetReference(0), reference_stride_, 0);
  FillConstant(GetReference(1), reference_stride_, 0);
  FillConstant(GetReference(2), reference_stride_, 0);
  FillConstant(GetReference(3), reference_stride_, 0);
  CheckSADs();
}

TEST_P(SADx4Test, ShortRef) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4Test, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4Test, ShortSrc) {
  int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 1000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(GetReference(0), reference_stride_);
    FillRandom(GetReference(1), reference_stride_);
    FillRandom(GetReference(2), reference_stride_);
    FillRandom(GetReference(3), reference_stride_);
    CheckSADs();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADx4Test, SrcAlignedByWidth) {
  uint16_t *tmp_source_data = source_data_;
  source_data_ += width_;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  source_data_ = tmp_source_data;
}

TEST_P(SADx4Test, DISABLED_Speed) {
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  SpeedSAD();
}

// x4d layout tests. Each uses the fixture's own planes (x4_src_, x4_ref_),
// sized for the largest stride and offset at 256x256, and checks all four
// SADs against BlockSad. Accumulator stress: the widest sum is src all-max
// against ref all-zero (or the reverse). MaxSrc and MaxRef above (up to
// 256x256 at 12 bits: 65536 * 4095 per ref) and ExtremesPerRefDistinct below
// reach it with refs in separate blocks, which no shared-row layout matches;
// OverlappingRefsSearchStepsExtremes reaches it with the refs sharing rows
// (diamond and square at 2- and 4-row distances).

// Four refs with different data at different offsets in separate regions:
// a kernel reusing ref 0's data or pointer, or writing the SADs out of
// order, gets at least one wrong.
TEST_P(SADx4Test, DistinctRefsDistinctOffsets) {
  const int off[4] = { 0, 5, 2, 7 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const int ss = width_ + 16, rs = width_ + 8;
  FillBlock(x4_src_, ss, [&](int, int) { return rnd_.Rand16() & mask_; });
  for (int i = 0; i < 4; ++i) {
    // Ref i is src plus a per-ref bias, clamped: distinct SADs per ref.
    uint16_t *ref = const_cast<uint16_t *>(refs[i]);
    FillBlock(ref, rs, [&](int r, int c) {
      const int v = x4_src_[r * ss + c] + (i + 1) * 3 * ((r + c) % 3 == 0);
      return static_cast<uint16_t>(v > mask_ ? mask_ : v);
    });
  }
  CheckX4(x4_src_, ss, refs, rs, "distinct refs", 0);
}

// Encoder-like overlapping refs: ref_array[i] = center + small offsets in
// one reference window (a diamond step: up, left, right, down), the window
// random.
TEST_P(SADx4Test, OverlappingRefsDiamond) {
  const int rs = kX4MaxStride;
  const int ss = width_;
  FillRandomN(x4_ref_, WindowElems(rs));
  FillBlock(x4_src_, ss, [&](int, int) { return rnd_.Rand16() & mask_; });
  const int dy[4] = { -1, 0, 0, 1 };
  const int dx[4] = { 0, -1, 1, 0 };
  const uint16_t *refs[4];
  WindowRefs(rs, 0, dy, dx, refs);
  CheckX4(x4_src_, ss, refs, rs, "diamond", 0);
  // A wider pattern with negative offsets past the block (mesh/int_pro).
  const int dy2[4] = { 0, -kX4Margin, 3, kX4Margin };
  const int dx2[4] = { 0, kX4MaxDx, -kX4MaxDx, -3 };
  WindowRefs(rs, 1, dy2, dx2, refs);
  CheckX4(x4_src_, ss, refs, rs, "wide window", 1);
}

// The motion search's step layouts in the encoder's ref order, where refs
// share rows: diamond (ref 1 = ref 0 + 2s rows) and square (refs 3 and 1 =
// refs 0 and 2 + s rows) at 2- and 4-row distances, a 3-row diamond and
// square, and the diamond with refs 0 and 1 swapped (ref 1 above ref 0), each
// at two strides and an aligned and an odd window base.
TEST_P(SADx4Test, OverlappingRefsSearchSteps) {
  struct Step {
    const char *name;
    int dy[4], dx[4];
  };
  static const Step kSteps[] = {
    { "diamond d=2", { -1, 1, 0, 0 }, { 0, 0, -1, 1 } },
    { "diamond d=4", { -2, 2, 0, 0 }, { 0, 0, -2, 2 } },
    { "square d=2", { -1, 1, -1, 1 }, { -1, 1, 1, -1 } },
    { "square d=4", { -2, 2, -2, 2 }, { -2, 2, 2, -2 } },
    { "diamond d=3", { -1, 2, 0, 0 }, { 0, 0, -1, 1 } },
    { "square d=3", { -1, 2, -1, 2 }, { -1, 2, 2, -1 } },
    { "diamond swapped", { 1, -1, 0, 0 }, { 0, 0, -1, 1 } },
    { "square d=4, ref 1 off", { -2, 2, -2, 2 }, { -2, 3, 2, -2 } },
  };
  const int strides[2] = { kX4MaxStride, width_ + 16 };
  const int ss = width_ + 8;
  int iter = 0;
  for (const Step &st : kSteps) {
    for (int k = 0; k < 2; ++k) {
      const int rs = strides[k];
      FillRandomN(x4_ref_, WindowElems(rs));
      FillBlock(x4_src_, ss, [&](int, int) { return rnd_.Rand16() & mask_; });
      const uint16_t *refs[4];
      WindowRefs(rs, k, st.dy, st.dx, refs);
      ASSERT_NO_FATAL_FAILURE(CheckX4(x4_src_, ss, refs, rs, st.name, iter));
      ++iter;
    }
  }
}

// OverlappingRefsSearchSteps' row-sharing layouts (diamond and square at 2-
// and 4-row distances) with extreme values: src all-max against an all-zero
// window, then src all-zero against an all-max window, so every SAD is
// w * h * max and each uint16 lane of a row-sharing kernel takes its largest
// sum before widening (at 8x32 and up for 8-wide blocks: a 32-row batch).
TEST_P(SADx4Test, OverlappingRefsSearchStepsExtremes) {
  struct Step {
    const char *name;
    int dy[4], dx[4];
  };
  static const Step kSteps[] = {
    { "diamond d=2", { -1, 1, 0, 0 }, { 0, 0, -1, 1 } },
    { "diamond d=4", { -2, 2, 0, 0 }, { 0, 0, -2, 2 } },
    { "square d=2", { -1, 1, -1, 1 }, { -1, 1, 1, -1 } },
    { "square d=4", { -2, 2, -2, 2 }, { -2, 2, 2, -2 } },
  };
  const int strides[2] = { kX4MaxStride, width_ + 16 };
  const int ss = width_ + 8;
  const uint16_t m = static_cast<uint16_t>(mask_);
  const uint16_t src_v[2] = { m, 0 };
  const uint16_t ref_v[2] = { 0, m };
  int iter = 0;
  for (const Step &st : kSteps) {
    for (int k = 0; k < 2; ++k) {
      const int rs = strides[k];
      for (int v = 0; v < 2; ++v) {
        const int n = WindowElems(rs);
        for (int i = 0; i < n; ++i) x4_ref_[i] = ref_v[v];
        FillBlock(x4_src_, ss, [&](int, int) { return src_v[v]; });
        const uint16_t *refs[4];
        WindowRefs(rs, k, st.dy, st.dx, refs);
        ASSERT_NO_FATAL_FAILURE(CheckX4(x4_src_, ss, refs, rs, st.name, iter));
        ++iter;
      }
    }
  }
}

// src at +1 and +3 elements from an aligned base; refs aligned.
TEST_P(SADx4Test, MisalignedSrc) {
  const int off[4] = { 0, 0, 0, 0 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const int rs = width_;
  for (int i = 0; i < 4; ++i) {
    FillRandom(const_cast<uint16_t *>(refs[i]), rs);
  }
  const int src_off[2] = { 1, 3 };
  for (int k = 0; k < 2; ++k) {
    uint16_t *src = x4_src_ + src_off[k];
    const int ss = width_ + 16;
    FillRandom(src, ss);
    CheckX4(src, ss, refs, rs, "src misaligned", src_off[k]);
  }
}

// Each ref at +1 or +3 elements from an aligned base (different per ref);
// src aligned.
TEST_P(SADx4Test, MisalignedRefs) {
  const int offs[2][4] = { { 1, 3, 3, 1 }, { 3, 1, 1, 3 } };
  const int ss = width_;
  FillRandom(x4_src_, ss);
  for (int k = 0; k < 2; ++k) {
    const uint16_t *refs[4];
    DisjointRefs(offs[k], refs);
    const int rs = width_ + 16;
    for (int i = 0; i < 4; ++i) {
      FillRandom(const_cast<uint16_t *>(refs[i]), rs);
    }
    CheckX4(x4_src_, ss, refs, rs, "refs misaligned", k);
  }
}

// src and refs misaligned together: src +1 with refs +3, src +3 with refs +1,
// and src +3 with overlapping window refs at an odd base.
TEST_P(SADx4Test, MisalignedSrcAndRefs) {
  const int src_off[2] = { 1, 3 };
  const int ref_off[2] = { 3, 1 };
  for (int k = 0; k < 2; ++k) {
    const int off[4] = { ref_off[k], ref_off[k], ref_off[k], ref_off[k] };
    const uint16_t *refs[4];
    DisjointRefs(off, refs);
    const int rs = width_ + 16;
    for (int i = 0; i < 4; ++i) {
      FillRandom(const_cast<uint16_t *>(refs[i]), rs);
    }
    uint16_t *src = x4_src_ + src_off[k];
    FillRandom(src, width_);
    CheckX4(src, width_, refs, rs, "src and refs misaligned", k);
  }
  const int rs = kX4MaxStride - 1;
  FillRandomN(x4_ref_, WindowElems(rs));
  const int dy[4] = { 0, 1, -2, 2 };
  const int dx[4] = { 0, 1, 2, -1 };
  const uint16_t *refs[4];
  WindowRefs(rs, 3, dy, dx, refs);
  uint16_t *src = x4_src_ + 3;
  FillRandom(src, width_ + 16);
  CheckX4(src, width_ + 16, refs, rs, "src and window refs misaligned", 2);
}

// Extremes: everything zero, then everything at the bit-depth maximum (both
// SADs 0); src = max with ref = 0 and the reverse are MaxSrc and MaxRef.
TEST_P(SADx4Test, ExtremesAllZeroAllMax) {
  const int off[4] = { 0, 1, 2, 3 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const uint16_t vals[2] = { 0, static_cast<uint16_t>(mask_) };
  for (int k = 0; k < 2; ++k) {
    FillConstant(x4_src_, width_, vals[k]);
    for (int i = 0; i < 4; ++i) {
      FillConstant(const_cast<uint16_t *>(refs[i]), width_, vals[k]);
    }
    CheckX4(x4_src_, width_, refs, width_, k ? "all max" : "all zero", k);
  }
}

// Per-ref extremes in one call: src all-max against refs {0, max, 0, max-1}
// (the accumulator-stress sum on refs 0 and 2), then src all-zero against
// {max, 0, 1, max}: the four SADs differ, so a swap is caught.
TEST_P(SADx4Test, ExtremesPerRefDistinct) {
  const int off[4] = { 2, 0, 3, 1 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const int ss = width_ + 16, rs = width_;
  const uint16_t m = static_cast<uint16_t>(mask_);
  const uint16_t src_v[2] = { m, 0 };
  const uint16_t ref_v[2][4] = { { 0, m, 0, static_cast<uint16_t>(m - 1) },
                                 { m, 0, 1, m } };
  for (int k = 0; k < 2; ++k) {
    FillConstant(x4_src_, ss, src_v[k]);
    for (int i = 0; i < 4; ++i) {
      FillConstant(const_cast<uint16_t *>(refs[i]), rs, ref_v[k][i]);
    }
    // One pixel of ref 3 at 0 in the first case: its SAD is w * h + max - 1.
    if (k == 0) const_cast<uint16_t *>(refs[3])[(height_ - 1) * rs] = 0;
    CheckX4(x4_src_, ss, refs, rs, "per-ref extremes", k);
  }
}

// Bit-depth midpoint: src every pixel 1 << (bd - 1), refs {0, max, mid,
// mid - 1}.
TEST_P(SADx4Test, BitDepthMidpoint) {
  const int off[4] = { 1, 0, 3, 2 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const uint16_t mid = static_cast<uint16_t>(1 << (bd_ - 1));
  const uint16_t ref_v[4] = { 0, static_cast<uint16_t>(mask_), mid,
                              static_cast<uint16_t>(mid - 1) };
  FillConstant(x4_src_, width_, mid);
  for (int i = 0; i < 4; ++i) {
    FillConstant(const_cast<uint16_t *>(refs[i]), width_ + 16, ref_v[i]);
  }
  CheckX4(x4_src_, width_, refs, width_ + 16, "midpoint", 0);
}

// Checkerboard of 0 and max: src (r + c) % 2, refs the inverse (the maximum
// SAD), the same pattern (0), a row-alternating pattern, and the midpoint.
TEST_P(SADx4Test, BitDepthCheckerboard) {
  const int rs = kX4MaxStride;
  const int off[4] = { 0, 0, 0, 0 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const uint16_t m = static_cast<uint16_t>(mask_);
  FillBlock(x4_src_, width_, [&](int r, int c) {
    return static_cast<uint16_t>((r + c) % 2 ? m : 0);
  });
  FillBlock(const_cast<uint16_t *>(refs[0]), rs, [&](int r, int c) {
    return static_cast<uint16_t>((r + c) % 2 ? 0 : m);
  });
  FillBlock(const_cast<uint16_t *>(refs[1]), rs, [&](int r, int c) {
    return static_cast<uint16_t>((r + c) % 2 ? m : 0);
  });
  FillBlock(const_cast<uint16_t *>(refs[2]), rs,
            [&](int r, int) { return static_cast<uint16_t>(r % 2 ? m : 0); });
  FillBlock(const_cast<uint16_t *>(refs[3]), rs,
            [&](int, int) { return static_cast<uint16_t>(1 << (bd_ - 1)); });
  CheckX4(x4_src_, width_, refs, rs, "checkerboard", 0);
}

// Strides: both == width (no padding), then width + 16 on one side and
// width on the other, both ways round (swapped strides are caught).
TEST_P(SADx4Test, StrideEdgeCases) {
  const int off[4] = { 0, 3, 1, 2 };
  const uint16_t *refs[4];
  DisjointRefs(off, refs);
  const int strides[3][2] = { { width_, width_ },
                              { width_ + 16, width_ },
                              { width_, width_ + 16 } };
  for (int k = 0; k < 3; ++k) {
    const int ss = strides[k][0], rs = strides[k][1];
    FillRandom(x4_src_, ss);
    for (int i = 0; i < 4; ++i) {
      FillRandom(const_cast<uint16_t *>(refs[i]), rs);
    }
    CheckX4(x4_src_, ss, refs, rs, "strides", k);
  }
}

// Random: kX4Iterations per parameter, each a fresh layout (disjoint refs at
// random +0..3 offsets, or overlapping window refs at random dy, dx), random
// src offset and strides (width, width + 16, or width + 0..31; src and ref
// drawn apart), and pixels from one of four value modes (full range, near
// max, near zero, either extreme).
TEST_P(SADx4Test, RandomLayouts) {
  for (int iter = 0; iter < kX4Iterations; ++iter) {
    const int mode = iter % 4;
    auto pixel = [&]() -> uint16_t {
      switch (mode) {
        case 0: return rnd_.Rand16() & mask_;
        case 1: return static_cast<uint16_t>(mask_ - rnd_(16));
        case 2: return static_cast<uint16_t>(rnd_(16));
        default: return rnd_(2) ? static_cast<uint16_t>(mask_) : 0;
      }
    };
    auto stride = [&]() {
      switch (rnd_(3)) {
        case 0: return width_;
        case 1: return width_ + 16;
        default: return width_ + static_cast<int>(rnd_(32));
      }
    };
    const int ss = stride();
    const int rs = stride();
    uint16_t *src = x4_src_ + rnd_(4);
    FillBlock(src, ss, [&](int, int) { return pixel(); });
    const uint16_t *refs[4];
    if (iter % 2) {
      int off[4];
      for (int i = 0; i < 4; ++i) off[i] = rnd_(4);
      DisjointRefs(off, refs);
      for (int i = 0; i < 4; ++i) {
        FillBlock(const_cast<uint16_t *>(refs[i]), rs,
                  [&](int, int) { return pixel(); });
      }
    } else {
      const int n = WindowElems(rs);
      for (int i = 0; i < n; ++i) x4_ref_[i] = pixel();
      int dy[4], dx[4];
      for (int i = 0; i < 4; ++i) {
        dy[i] = static_cast<int>(rnd_(2 * kX4Margin + 1)) - kX4Margin;
        dx[i] = static_cast<int>(rnd_(2 * kX4MaxDx + 1)) - kX4MaxDx;
      }
      WindowRefs(rs, rnd_(4), dy, dx, refs);
    }
    ASSERT_NO_FATAL_FAILURE(CheckX4(src, ss, refs, rs, "random", iter));
  }
}

// SADSkipx4
TEST_P(SADSkipx4Test, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(GetReference(0), reference_stride_, mask_);
  FillConstant(GetReference(1), reference_stride_, mask_);
  FillConstant(GetReference(2), reference_stride_, mask_);
  FillConstant(GetReference(3), reference_stride_, mask_);
  CheckSADs();
}

TEST_P(SADSkipx4Test, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(GetReference(0), reference_stride_, 0);
  FillConstant(GetReference(1), reference_stride_, 0);
  FillConstant(GetReference(2), reference_stride_, 0);
  FillConstant(GetReference(3), reference_stride_, 0);
  CheckSADs();
}

TEST_P(SADSkipx4Test, ShortRef) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADSkipx4Test, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADSkipx4Test, ShortSrc) {
  int tmp_stride = source_stride_;
  source_stride_ >>= 1;
  int test_count = 1000;
  while (test_count > 0) {
    FillRandom(source_data_, source_stride_);
    FillRandom(GetReference(0), reference_stride_);
    FillRandom(GetReference(1), reference_stride_);
    FillRandom(GetReference(2), reference_stride_);
    FillRandom(GetReference(3), reference_stride_);
    CheckSADs();
    test_count -= 1;
  }
  source_stride_ = tmp_stride;
}

TEST_P(SADSkipx4Test, SrcAlignedByWidth) {
  uint16_t *tmp_source_data = source_data_;
  source_data_ += width_;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  CheckSADs();
  source_data_ = tmp_source_data;
}

TEST_P(SADSkipx4Test, DISABLED_Speed) {
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  SpeedSAD();
}

using std::make_tuple;

TEST_P(SADx4AvgTest, DISABLED_Speed) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  FillRandom(second_pred_, width_);
  SpeedSAD();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4AvgTest, MaxRef) {
  FillConstant(source_data_, source_stride_, 0);
  FillConstant(GetReference(0), reference_stride_, mask_);
  FillConstant(GetReference(1), reference_stride_, mask_);
  FillConstant(GetReference(2), reference_stride_, mask_);
  FillConstant(GetReference(3), reference_stride_, mask_);
  FillConstant(second_pred_, width_, 0);
  CheckSADs();
}

TEST_P(SADx4AvgTest, MaxSrc) {
  FillConstant(source_data_, source_stride_, mask_);
  FillConstant(GetReference(0), reference_stride_, 0);
  FillConstant(GetReference(1), reference_stride_, 0);
  FillConstant(GetReference(2), reference_stride_, 0);
  FillConstant(GetReference(3), reference_stride_, 0);
  FillConstant(second_pred_, width_, 0);
  CheckSADs();
}

TEST_P(SADx4AvgTest, ShortRef) {
  int tmp_stride = reference_stride_;
  reference_stride_ >>= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

TEST_P(SADx4AvgTest, UnalignedRef) {
  // The reference frame, but not the source frame, may be unaligned for
  // certain types of searches.
  int tmp_stride = reference_stride_;
  reference_stride_ -= 1;
  FillRandom(source_data_, source_stride_);
  FillRandom(GetReference(0), reference_stride_);
  FillRandom(GetReference(1), reference_stride_);
  FillRandom(GetReference(2), reference_stride_);
  FillRandom(GetReference(3), reference_stride_);
  FillRandom(second_pred_, width_);
  CheckSADs();
  reference_stride_ = tmp_stride;
}

//------------------------------------------------------------------------------
// C functions
const SadMxNParam c_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256_c, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128_c, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256_c, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128_c, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64_c, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128_c, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_c, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_c, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_c, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_c, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_c, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32_c, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_c, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_c, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_c, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8_c, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4_c, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8_c, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4_c, 8),
  make_tuple(256, 256, &avm_highbd_sad256x256_c, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128_c, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256_c, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128_c, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64_c, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128_c, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_c, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_c, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_c, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_c, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_c, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32_c, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16_c, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8_c, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16_c, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8_c, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4_c, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8_c, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4_c, 10),
  make_tuple(256, 256, &avm_highbd_sad256x256_c, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128_c, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256_c, 12),
  make_tuple(128, 128, &avm_highbd_sad128x128_c, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64_c, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128_c, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64_c, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_c, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_c, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_c, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_c, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32_c, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16_c, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8_c, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16_c, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8_c, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4_c, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8_c, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4_c, 12),
  make_tuple(12, 12, &avm_highbd_sad12x12_c, 8),
  make_tuple(12, 12, &avm_highbd_sad12x12_c, 10),
  make_tuple(12, 12, &avm_highbd_sad12x12_c, 12),
  make_tuple(12, 20, &avm_highbd_sad12x20_c, 8),
  make_tuple(12, 20, &avm_highbd_sad12x20_c, 10),
  make_tuple(12, 20, &avm_highbd_sad12x20_c, 12),
  make_tuple(20, 12, &avm_highbd_sad20x12_c, 8),
  make_tuple(20, 12, &avm_highbd_sad20x12_c, 10),
  make_tuple(20, 12, &avm_highbd_sad20x12_c, 12),
  make_tuple(20, 20, &avm_highbd_sad20x20_c, 8),
  make_tuple(20, 20, &avm_highbd_sad20x20_c, 10),
  make_tuple(20, 20, &avm_highbd_sad20x20_c, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_c, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64_c, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_c, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64_c, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_c, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64_c, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8_c, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32_c, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_c, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32_c, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_c, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32_c, 12),

  make_tuple(16, 4, &avm_highbd_sad16x4_c, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16_c, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4_c, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16_c, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4_c, 12),
  make_tuple(4, 16, &avm_highbd_sad4x16_c, 12),

  make_tuple(64, 8, &avm_highbd_sad64x8_c, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64_c, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8_c, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64_c, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8_c, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64_c, 12),

  make_tuple(32, 4, &avm_highbd_sad32x4_c, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32_c, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4_c, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32_c, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4_c, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32_c, 12),

  make_tuple(64, 4, &avm_highbd_sad64x4_c, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64_c, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4_c, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64_c, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4_c, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, SADTest, ::testing::ValuesIn(c_tests));

const SadDSMxNParam ds_c_tests[] = {
  make_tuple(8, 8, &avm_highbd_sad8x8_ds_c, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_ds_c, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_ds_c, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_ds_c, 8),
  make_tuple(12, 12, &avm_highbd_sad12x12_ds_c, 8),
  make_tuple(12, 20, &avm_highbd_sad12x20_ds_c, 8),
  make_tuple(20, 12, &avm_highbd_sad20x12_ds_c, 8),
  make_tuple(20, 20, &avm_highbd_sad20x20_ds_c, 8),
};
INSTANTIATE_TEST_SUITE_P(C, SADDSTest, ::testing::ValuesIn(ds_c_tests));

const SadSkipMxNParam skip_c_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_c, 8),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_c, 8),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_c, 8),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_c, 8),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_c, 8),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_c, 8),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_c, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_c, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_c, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_c, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_c, 8),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_c, 8),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_c, 8),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_c, 8),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_c, 8),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_c, 8),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4_c, 8),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_c, 8),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4_c, 8),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_c, 8),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_c, 8),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_c, 8),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_c, 8),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_c, 8),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_c, 8),

  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_c, 10),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_c, 10),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_c, 10),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_c, 10),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_c, 10),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_c, 10),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_c, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_c, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_c, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_c, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_c, 10),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_c, 10),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_c, 10),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_c, 10),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_c, 10),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_c, 10),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4_c, 10),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_c, 10),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4_c, 10),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_c, 10),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_c, 10),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_c, 10),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_c, 10),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_c, 10),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_c, 10),

  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_c, 12),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_c, 12),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_c, 12),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_c, 12),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_c, 12),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_c, 12),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_c, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_c, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_c, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_c, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_c, 12),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_c, 12),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_c, 12),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_c, 12),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_c, 12),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_c, 12),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4_c, 12),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_c, 12),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4_c, 12),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_c, 12),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_c, 12),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_c, 12),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_c, 12),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_c, 12),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, SADSkipTest, ::testing::ValuesIn(skip_c_tests));

const SadMxNAvgParam avg_c_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256_avg_c, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_c, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_c, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128_avg_c, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_c, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_c, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_c, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_c, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_c, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_c, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_c, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_c, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_c, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_c, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_c, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_c, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_c, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_c, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_c, 8),

  make_tuple(256, 256, &avm_highbd_sad256x256_avg_c, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_c, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_c, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128_avg_c, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_c, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_c, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_c, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_c, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_c, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_c, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_c, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_c, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_c, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_c, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_c, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_c, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_c, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_c, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_c, 10),

  make_tuple(256, 256, &avm_highbd_sad256x256_avg_c, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_c, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_c, 12),
  make_tuple(128, 128, &avm_highbd_sad128x128_avg_c, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_c, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_c, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_c, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_c, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_c, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_c, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_c, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_c, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_c, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_c, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_c, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_c, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_c, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_c, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_c, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_avg_c, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_c, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_c, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_c, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_c, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_c, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8_avg_c, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_c, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_c, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_c, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_c, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_c, 12),

  make_tuple(16, 4, &avm_highbd_sad16x4_avg_c, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16_avg_c, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4_avg_c, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16_avg_c, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4_avg_c, 12),
  make_tuple(4, 16, &avm_highbd_sad4x16_avg_c, 12),

  make_tuple(64, 8, &avm_highbd_sad64x8_avg_c, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_c, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8_avg_c, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_c, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8_avg_c, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_c, 12),

  make_tuple(32, 4, &avm_highbd_sad32x4_avg_c, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32_avg_c, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4_avg_c, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32_avg_c, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4_avg_c, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32_avg_c, 12),

  make_tuple(64, 4, &avm_highbd_sad64x4_avg_c, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64_avg_c, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4_avg_c, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64_avg_c, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4_avg_c, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64_avg_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, SADavgTest, ::testing::ValuesIn(avg_c_tests));

const SadMxNx4Param x4d_c_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256x4d_c, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_c, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_c, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_c, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_c, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_c, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_c, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_c, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_c, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_c, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_c, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_c, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_c, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_c, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_c, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_c, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_c, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_c, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_c, 8),

  make_tuple(256, 256, &avm_highbd_sad256x256x4d_c, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_c, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_c, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_c, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_c, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_c, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_c, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_c, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_c, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_c, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_c, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_c, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_c, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_c, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_c, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_c, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_c, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_c, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_c, 10),

  make_tuple(256, 256, &avm_highbd_sad256x256x4d_c, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_c, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_c, 12),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_c, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_c, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_c, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_c, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_c, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_c, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_c, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_c, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_c, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_c, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_c, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_c, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_c, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_c, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_c, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_c, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16x4d_c, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_c, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_c, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_c, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_c, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_c, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8x4d_c, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_c, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_c, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_c, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_c, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_c, 12),

  make_tuple(16, 4, &avm_highbd_sad16x4x4d_c, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_c, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_c, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_c, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_c, 12),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_c, 12),

  make_tuple(64, 8, &avm_highbd_sad64x8x4d_c, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_c, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_c, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_c, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_c, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_c, 12),

  make_tuple(32, 4, &avm_highbd_sad32x4x4d_c, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_c, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_c, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_c, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_c, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_c, 12),

  make_tuple(64, 4, &avm_highbd_sad64x4x4d_c, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_c, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_c, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_c, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_c, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, SADx4Test, ::testing::ValuesIn(x4d_c_tests));

const SadMxNx4Param skip_x4d_c_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_c, 8),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_c, 8),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_c, 8),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_c, 8),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_c, 8),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_c, 8),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_c, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_c, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_c, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_c, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_c, 8),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_c, 8),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_c, 8),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_c, 8),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_c, 8),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_c, 8),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4x4d_c, 8),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_c, 8),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4x4d_c, 8),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_c, 8),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_c, 8),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_c, 8),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_c, 8),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_c, 8),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_c, 8),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_c, 8),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_c, 8),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_c, 8),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_c, 8),

  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_c, 10),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_c, 10),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_c, 10),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_c, 10),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_c, 10),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_c, 10),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_c, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_c, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_c, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_c, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_c, 10),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_c, 10),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_c, 10),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_c, 10),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_c, 10),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_c, 10),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4x4d_c, 10),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_c, 10),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4x4d_c, 10),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_c, 10),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_c, 10),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_c, 10),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_c, 10),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_c, 10),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_c, 10),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_c, 10),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_c, 10),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_c, 10),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_c, 10),

  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_c, 12),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_c, 12),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_c, 12),
  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_c, 12),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_c, 12),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_c, 12),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_c, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_c, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_c, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_c, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_c, 12),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_c, 12),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_c, 12),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_c, 12),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_c, 12),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_c, 12),
  make_tuple(8, 4, &avm_highbd_sad_skip_8x4x4d_c, 12),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_c, 12),
  make_tuple(4, 4, &avm_highbd_sad_skip_4x4x4d_c, 12),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_c, 12),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_c, 12),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_c, 12),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_c, 12),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_c, 12),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_c, 12),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_c, 12),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_c, 12),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_c, 12),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_c, 12),
};
INSTANTIATE_TEST_SUITE_P(C, SADSkipx4Test,
                         ::testing::ValuesIn(skip_x4d_c_tests));
//------------------------------------------------------------------------------
// x86 functions
#if HAVE_SSE2
const SadMxNParam sse2_tests[] = {
  make_tuple(64, 64, &avm_highbd_sad64x64_sse2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_sse2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_sse2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_sse2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_sse2, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_sse2, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8_sse2, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4_sse2, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8_sse2, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4_sse2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_sse2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_sse2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_sse2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_sse2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_sse2, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16_sse2, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8_sse2, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4_sse2, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8_sse2, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4_sse2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_sse2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_sse2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_sse2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_sse2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_sse2, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16_sse2, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8_sse2, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4_sse2, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8_sse2, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4_sse2, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_sse2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_sse2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_sse2, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8_sse2, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32_sse2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_sse2, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32_sse2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_sse2, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32_sse2, 12),

  make_tuple(4, 16, &avm_highbd_sad4x16_sse2, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16_sse2, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16_sse2, 12),

  // make_tuple(64, 8, &avm_highbd_sad64x8_sse2, -1),
  // make_tuple(8, 64, &avm_highbd_sad8x64_sse2, -1),
  make_tuple(64, 8, &avm_highbd_sad64x8_sse2, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64_sse2, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8_sse2, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64_sse2, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8_sse2, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64_sse2, 12),

  // make_tuple(32, 4, &avm_highbd_sad32x4_sse2, -1),
  // make_tuple(4, 32, &avm_highbd_sad4x32_sse2, -1),
  make_tuple(32, 4, &avm_highbd_sad32x4_sse2, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32_sse2, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4_sse2, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32_sse2, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4_sse2, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32_sse2, 12),

  // make_tuple(64, 4, &avm_highbd_sad64x4_sse2, -1),
  // make_tuple(4, 64, &avm_highbd_sad4x64_sse2, -1),
  make_tuple(64, 4, &avm_highbd_sad64x4_sse2, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64_sse2, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4_sse2, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64_sse2, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4_sse2, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64_sse2, 12),
};
INSTANTIATE_TEST_SUITE_P(SSE2, SADTest, ::testing::ValuesIn(sse2_tests));

const SadSkipMxNParam skip_sse2_tests[] = {
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_sse2, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_sse2, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_sse2, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_sse2, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_sse2, 8),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_sse2, 8),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_sse2, 8),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_sse2, 8),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_sse2, 8),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_sse2, 8),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_sse2, 8),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_sse2, 8),

  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_sse2, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_sse2, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_sse2, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_sse2, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_sse2, 10),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_sse2, 10),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_sse2, 10),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_sse2, 10),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_sse2, 10),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_sse2, 10),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_sse2, 10),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_sse2, 10),

  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_sse2, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_sse2, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_sse2, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_sse2, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_sse2, 12),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16_sse2, 12),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8_sse2, 12),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8_sse2, 12),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16_sse2, 12),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8_sse2, 12),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32_sse2, 12),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16_sse2, 12),

  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_sse2, 8),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32_sse2, 8),
  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_sse2, 8),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64_sse2, 8),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_sse2, 8),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64_sse2, 8),

  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_sse2, 10),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32_sse2, 10),
  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_sse2, 10),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64_sse2, 10),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_sse2, 10),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64_sse2, 10),

  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_sse2, 12),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32_sse2, 12),
  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_sse2, 12),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64_sse2, 12),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_sse2, 12),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64_sse2, 12),
};
INSTANTIATE_TEST_SUITE_P(SSE2, SADSkipTest,
                         ::testing::ValuesIn(skip_sse2_tests));

const SadMxNAvgParam avg_sse2_tests[] = {
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_sse2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_sse2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_sse2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_sse2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_sse2, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_sse2, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_sse2, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_sse2, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_sse2, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_sse2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_sse2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_sse2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_sse2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_sse2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_sse2, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_sse2, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_sse2, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_sse2, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_sse2, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_sse2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_sse2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_sse2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_sse2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_sse2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_sse2, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16_avg_sse2, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8_avg_sse2, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4_avg_sse2, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8_avg_sse2, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4_avg_sse2, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_avg_sse2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_sse2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_sse2, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8_avg_sse2, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_sse2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_sse2, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_sse2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_sse2, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32_avg_sse2, 12),

  make_tuple(4, 16, &avm_highbd_sad4x16_avg_sse2, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16_avg_sse2, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16_avg_sse2, 12),

  // make_tuple(64, 8, &avm_highbd_sad64x8_avg_sse2, -1),
  // make_tuple(8, 64, &avm_highbd_sad8x64_avg_sse2, -1),
  make_tuple(64, 8, &avm_highbd_sad64x8_avg_sse2, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_sse2, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8_avg_sse2, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_sse2, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8_avg_sse2, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64_avg_sse2, 12),

  // make_tuple(32, 4, &avm_highbd_sad32x4_avg_sse2, -1),
  // make_tuple(4, 32, &avm_highbd_sad4x32_avg_sse2, -1),
  make_tuple(32, 4, &avm_highbd_sad32x4_avg_sse2, 8),
  // make_tuple(4, 32, &avm_highbd_sad4x32_avg_sse2, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4_avg_sse2, 10),
  // make_tuple(4, 32, &avm_highbd_sad4x32_avg_sse2, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4_avg_sse2, 12),
  // make_tuple(4, 32, &avm_highbd_sad4x32_avg_sse2, 12),

  // make_tuple(64, 4, &avm_highbd_sad64x4_avg_sse2, -1),
  // make_tuple(4, 64, &avm_highbd_sad4x64_avg_sse2, -1),
  make_tuple(64, 4, &avm_highbd_sad64x4_avg_sse2, 8),
  // make_tuple(4, 64, &avm_highbd_sad4x64_avg_sse2, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4_avg_sse2, 10),
  // make_tuple(4, 64, &avm_highbd_sad4x64_avg_sse2, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4_avg_sse2, 12),
  // make_tuple(4, 64, &avm_highbd_sad4x64_avg_sse2, 12),
};
INSTANTIATE_TEST_SUITE_P(SSE2, SADavgTest, ::testing::ValuesIn(avg_sse2_tests));

const SadMxNx4Param x4d_sse2_tests[] = {
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_sse2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_sse2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_sse2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_sse2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_sse2, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_sse2, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_sse2, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_sse2, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_sse2, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_sse2, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_sse2, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_sse2, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_sse2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_sse2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_sse2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_sse2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_sse2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_sse2, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_sse2, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_sse2, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_sse2, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_sse2, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_sse2, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_sse2, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_sse2, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_sse2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_sse2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_sse2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_sse2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_sse2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_sse2, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_sse2, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_sse2, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_sse2, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_sse2, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_sse2, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_sse2, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_sse2, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_sse2, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16x4d_sse2, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_sse2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_sse2, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_sse2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_sse2, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_sse2, 12),

  make_tuple(32, 8, &avm_highbd_sad32x8x4d_sse2, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_sse2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_sse2, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_sse2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_sse2, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_sse2, 12),

  make_tuple(16, 4, &avm_highbd_sad16x4x4d_sse2, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_sse2, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_sse2, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_sse2, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_sse2, 12),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_sse2, 12),

  // make_tuple(64, 8, &avm_highbd_sad64x8x4d_sse2, -1),
  // make_tuple(8, 64, &avm_highbd_sad8x64x4d_sse2, -1),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_sse2, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_sse2, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_sse2, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_sse2, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_sse2, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_sse2, 12),

  // make_tuple(32, 4, &avm_highbd_sad32x4x4d_sse2, -1),
  // make_tuple(4, 32, &avm_highbd_sad4x32x4d_sse2, -1),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_sse2, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_sse2, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_sse2, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_sse2, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_sse2, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_sse2, 12),

  // make_tuple(64, 4, &avm_highbd_sad64x4x4d_sse2, -1),
  // make_tuple(4, 64, &avm_highbd_sad4x64x4d_sse2, -1),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_sse2, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_sse2, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_sse2, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_sse2, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_sse2, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_sse2, 12),
};
INSTANTIATE_TEST_SUITE_P(SSE2, SADx4Test, ::testing::ValuesIn(x4d_sse2_tests));

const SadSkipMxNx4Param skip_x4d_sse2_tests[] = {
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_sse2, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_sse2, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_sse2, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_sse2, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_sse2, 8),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_sse2, 8),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_sse2, 8),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_sse2, 8),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_sse2, 8),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_sse2, 8),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_sse2, 8),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_sse2, 8),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_sse2, 8),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_sse2, 8),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_sse2, 8),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_sse2, 8),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_sse2, 8),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_sse2, 8),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_sse2, 8),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_sse2, 8),

  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_sse2, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_sse2, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_sse2, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_sse2, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_sse2, 10),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_sse2, 10),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_sse2, 10),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_sse2, 10),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_sse2, 10),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_sse2, 10),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_sse2, 10),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_sse2, 10),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_sse2, 10),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_sse2, 10),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_sse2, 10),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_sse2, 10),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_sse2, 10),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_sse2, 10),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_sse2, 10),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_sse2, 10),

  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_sse2, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_sse2, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_sse2, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_sse2, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_sse2, 12),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_sse2, 12),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_sse2, 12),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_sse2, 12),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_sse2, 12),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_sse2, 12),
  make_tuple(4, 8, &avm_highbd_sad_skip_4x8x4d_sse2, 12),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_sse2, 12),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_sse2, 12),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_sse2, 12),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_sse2, 12),
  make_tuple(4, 16, &avm_highbd_sad_skip_4x16x4d_sse2, 12),
  make_tuple(4, 32, &avm_highbd_sad_skip_4x32x4d_sse2, 12),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_sse2, 12),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_sse2, 12),
  make_tuple(4, 64, &avm_highbd_sad_skip_4x64x4d_sse2, 12),
};
INSTANTIATE_TEST_SUITE_P(SSE2, SADSkipx4Test,
                         ::testing::ValuesIn(skip_x4d_sse2_tests));
#endif  // HAVE_SSE2

#if HAVE_AVX2
const SadMxNParam avx2_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256_avx2, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128_avx2, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256_avx2, 8),
  make_tuple(256, 256, &avm_highbd_sad256x256_avx2, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128_avx2, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256_avx2, 10),
  make_tuple(256, 256, &avm_highbd_sad256x256_avx2, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128_avx2, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256_avx2, 12),

  make_tuple(128, 128, &avm_highbd_sad128x128_avx2, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128_avx2, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128_avx2, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64_avx2, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64_avx2, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64_avx2, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128_avx2, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128_avx2, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128_avx2, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64_avx2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_avx2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_avx2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_avx2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_avx2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_avx2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_avx2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_avx2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_avx2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_avx2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_avx2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_avx2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_avx2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_avx2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_avx2, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32_avx2, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32_avx2, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32_avx2, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_avx2, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16_avx2, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_avx2, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8_avx2, 12),
  make_tuple(12, 12, &avm_highbd_sad12x12_avx2, 8),
  make_tuple(12, 12, &avm_highbd_sad12x12_avx2, 10),
  make_tuple(12, 12, &avm_highbd_sad12x12_avx2, 12),
  make_tuple(12, 20, &avm_highbd_sad12x20_avx2, 8),
  make_tuple(12, 20, &avm_highbd_sad12x20_avx2, 10),
  make_tuple(12, 20, &avm_highbd_sad12x20_avx2, 12),
  make_tuple(20, 12, &avm_highbd_sad20x12_avx2, 8),
  make_tuple(20, 12, &avm_highbd_sad20x12_avx2, 10),
  make_tuple(20, 12, &avm_highbd_sad20x12_avx2, 12),
  make_tuple(20, 20, &avm_highbd_sad20x20_avx2, 8),
  make_tuple(20, 20, &avm_highbd_sad20x20_avx2, 10),
  make_tuple(20, 20, &avm_highbd_sad20x20_avx2, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_avx2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_avx2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_avx2, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64_avx2, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64_avx2, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64_avx2, 12),
  make_tuple(32, 8, &avm_highbd_sad32x8_avx2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_avx2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_avx2, 12),
  make_tuple(16, 4, &avm_highbd_sad16x4_avx2, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4_avx2, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4_avx2, 12),

  make_tuple(64, 4, &avm_highbd_sad64x4_avx2, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8_avx2, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4_avx2, 8),

  make_tuple(64, 4, &avm_highbd_sad64x4_avx2, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8_avx2, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4_avx2, 10),

  make_tuple(64, 4, &avm_highbd_sad64x4_avx2, 12),
  make_tuple(64, 8, &avm_highbd_sad64x8_avx2, 12),
  make_tuple(32, 4, &avm_highbd_sad32x4_avx2, 12),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADTest, ::testing::ValuesIn(avx2_tests));

const SadDSMxNParam ds_avx2_tests[] = {
  make_tuple(8, 8, &avm_highbd_sad8x8_ds_avx2, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16_ds_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_ds_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_ds_avx2, 8),
  make_tuple(12, 12, &avm_highbd_sad12x12_ds_avx2, 8),
  make_tuple(12, 20, &avm_highbd_sad12x20_ds_avx2, 8),
  make_tuple(20, 12, &avm_highbd_sad20x12_ds_avx2, 8),
  make_tuple(20, 20, &avm_highbd_sad20x20_ds_avx2, 8),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADDSTest, ::testing::ValuesIn(ds_avx2_tests));

const SadSkipMxNParam skip_avx2_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_avx2, 8),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_avx2, 8),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_avx2, 8),
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_avx2, 10),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_avx2, 10),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_avx2, 10),
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256_avx2, 12),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128_avx2, 12),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256_avx2, 12),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_avx2, 8),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_avx2, 8),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_avx2, 8),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_avx2, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_avx2, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_avx2, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_avx2, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_avx2, 8),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_avx2, 8),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_avx2, 8),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_avx2, 8),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_avx2, 10),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_avx2, 10),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_avx2, 10),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_avx2, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_avx2, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_avx2, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_avx2, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_avx2, 10),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_avx2, 10),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_avx2, 10),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_avx2, 10),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_avx2, 10),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_avx2, 10),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128_avx2, 12),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64_avx2, 12),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128_avx2, 12),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64_avx2, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32_avx2, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64_avx2, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32_avx2, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16_avx2, 12),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64_avx2, 12),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32_avx2, 12),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16_avx2, 12),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8_avx2, 12),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4_avx2, 12),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_avx2, 8),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_avx2, 8),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_avx2, 8),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_avx2, 10),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_avx2, 10),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_avx2, 10),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4_avx2, 12),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8_avx2, 12),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4_avx2, 12),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADSkipTest,
                         ::testing::ValuesIn(skip_avx2_tests));

const SadMxNAvgParam avg_avx2_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256_avg_avx2, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_avx2, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_avx2, 8),
  make_tuple(256, 256, &avm_highbd_sad256x256_avg_avx2, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_avx2, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_avx2, 10),
  make_tuple(256, 256, &avm_highbd_sad256x256_avg_avx2, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128_avg_avx2, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256_avg_avx2, 12),

  make_tuple(128, 128, &avm_highbd_sad128x128_avg_avx2, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128_avg_avx2, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128_avg_avx2, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_avx2, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_avx2, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64_avg_avx2, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_avx2, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_avx2, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128_avg_avx2, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_avx2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_avx2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64_avg_avx2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_avx2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_avx2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32_avg_avx2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_avx2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_avx2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64_avg_avx2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_avx2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_avx2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32_avg_avx2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_avx2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_avx2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16_avg_avx2, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_avx2, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_avx2, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32_avg_avx2, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_avx2, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16_avg_avx2, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_avx2, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8_avg_avx2, 12),

  make_tuple(64, 16, &avm_highbd_sad64x16_avg_avx2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_avx2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16_avg_avx2, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_avx2, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_avx2, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64_avg_avx2, 12),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_avx2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_avx2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8_avg_avx2, 12),
  make_tuple(16, 4, &avm_highbd_sad16x4_avg_avx2, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4_avg_avx2, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4_avg_avx2, 12),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADavgTest, ::testing::ValuesIn(avg_avx2_tests));

const SadSkipMxNx4Param skip_x4d_avx2_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_avx2, 8),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_avx2, 8),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_avx2, 8),
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_avx2, 10),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_avx2, 10),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_avx2, 10),
  make_tuple(256, 256, &avm_highbd_sad_skip_256x256x4d_avx2, 12),
  make_tuple(256, 128, &avm_highbd_sad_skip_256x128x4d_avx2, 12),
  make_tuple(128, 256, &avm_highbd_sad_skip_128x256x4d_avx2, 12),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_avx2, 8),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_avx2, 8),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_avx2, 8),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_avx2, 8),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_avx2, 8),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_avx2, 8),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_avx2, 8),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_avx2, 8),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_avx2, 8),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_avx2, 8),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_avx2, 8),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_avx2, 8),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_avx2, 8),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_avx2, 8),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_avx2, 8),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_avx2, 8),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_avx2, 10),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_avx2, 10),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_avx2, 10),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_avx2, 10),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_avx2, 10),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_avx2, 10),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_avx2, 10),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_avx2, 10),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_avx2, 10),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_avx2, 10),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_avx2, 10),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_avx2, 10),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_avx2, 10),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_avx2, 10),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_avx2, 10),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_avx2, 10),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_avx2, 10),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_avx2, 10),

  make_tuple(128, 128, &avm_highbd_sad_skip_128x128x4d_avx2, 12),
  make_tuple(128, 64, &avm_highbd_sad_skip_128x64x4d_avx2, 12),
  make_tuple(64, 128, &avm_highbd_sad_skip_64x128x4d_avx2, 12),
  make_tuple(64, 64, &avm_highbd_sad_skip_64x64x4d_avx2, 12),
  make_tuple(64, 32, &avm_highbd_sad_skip_64x32x4d_avx2, 12),
  make_tuple(64, 16, &avm_highbd_sad_skip_64x16x4d_avx2, 12),
  make_tuple(32, 64, &avm_highbd_sad_skip_32x64x4d_avx2, 12),
  make_tuple(32, 32, &avm_highbd_sad_skip_32x32x4d_avx2, 12),
  make_tuple(32, 16, &avm_highbd_sad_skip_32x16x4d_avx2, 12),
  make_tuple(32, 8, &avm_highbd_sad_skip_32x8x4d_avx2, 12),
  make_tuple(8, 32, &avm_highbd_sad_skip_8x32x4d_avx2, 12),
  make_tuple(16, 64, &avm_highbd_sad_skip_16x64x4d_avx2, 12),
  make_tuple(16, 32, &avm_highbd_sad_skip_16x32x4d_avx2, 12),
  make_tuple(16, 16, &avm_highbd_sad_skip_16x16x4d_avx2, 12),
  make_tuple(16, 8, &avm_highbd_sad_skip_16x8x4d_avx2, 12),
  make_tuple(8, 16, &avm_highbd_sad_skip_8x16x4d_avx2, 12),
  make_tuple(8, 8, &avm_highbd_sad_skip_8x8x4d_avx2, 12),
  make_tuple(16, 4, &avm_highbd_sad_skip_16x4x4d_avx2, 12),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4x4d_avx2, 8),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_avx2, 8),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_avx2, 8),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4x4d_avx2, 8),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4x4d_avx2, 10),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_avx2, 10),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_avx2, 10),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4x4d_avx2, 10),

  make_tuple(64, 4, &avm_highbd_sad_skip_64x4x4d_avx2, 12),
  make_tuple(64, 8, &avm_highbd_sad_skip_64x8x4d_avx2, 12),
  make_tuple(8, 64, &avm_highbd_sad_skip_8x64x4d_avx2, 12),
  make_tuple(32, 4, &avm_highbd_sad_skip_32x4x4d_avx2, 12),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADSkipx4Test,
                         ::testing::ValuesIn(skip_x4d_avx2_tests));

const SadMxNx4Param x4d_avx2_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256x4d_avx2, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_avx2, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_avx2, 8),
  make_tuple(256, 256, &avm_highbd_sad256x256x4d_avx2, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_avx2, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_avx2, 10),
  make_tuple(256, 256, &avm_highbd_sad256x256x4d_avx2, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_avx2, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_avx2, 12),

  make_tuple(128, 128, &avm_highbd_sad128x128x4d_avx2, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_avx2, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_avx2, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_avx2, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_avx2, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_avx2, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_avx2, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_avx2, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_avx2, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_avx2, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_avx2, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_avx2, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_avx2, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_avx2, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_avx2, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_avx2, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_avx2, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_avx2, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_avx2, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_avx2, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_avx2, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_avx2, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_avx2, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_avx2, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_avx2, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_avx2, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_avx2, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_avx2, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_avx2, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_avx2, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_avx2, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_avx2, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_avx2, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_avx2, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_avx2, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_avx2, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_avx2, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_avx2, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_avx2, 12),

  make_tuple(16, 64, &avm_highbd_sad16x64x4d_avx2, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_avx2, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_avx2, 12),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_avx2, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_avx2, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_avx2, 12),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_avx2, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_avx2, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_avx2, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_avx2, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_avx2, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_avx2, 12),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_avx2, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_avx2, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_avx2, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_avx2, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_avx2, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_avx2, 12),

  make_tuple(64, 4, &avm_highbd_sad64x4x4d_avx2, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_avx2, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_avx2, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_avx2, 8),

  make_tuple(64, 4, &avm_highbd_sad64x4x4d_avx2, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_avx2, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_avx2, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_avx2, 10),

  make_tuple(64, 4, &avm_highbd_sad64x4x4d_avx2, 12),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_avx2, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_avx2, 12),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_avx2, 12),
};
INSTANTIATE_TEST_SUITE_P(AVX2, SADx4Test, ::testing::ValuesIn(x4d_avx2_tests));
#endif  // HAVE_AVX2

#if HAVE_NEON
// Every plain highbd x4d size (avm_dsp_rtcd_defs.pl, all 31), at 8, 10 and
// 12 bits.
const SadMxNx4Param x4d_neon_tests[] = {
  make_tuple(256, 256, &avm_highbd_sad256x256x4d_neon, 8),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_neon, 8),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_neon, 8),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_neon, 8),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_neon, 8),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_neon, 8),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_neon, 8),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_neon, 8),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_neon, 8),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_neon, 8),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_neon, 8),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_neon, 8),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_neon, 8),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_neon, 8),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_neon, 8),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_neon, 8),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_neon, 8),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_neon, 8),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_neon, 8),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_neon, 8),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_neon, 8),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_neon, 8),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_neon, 8),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_neon, 8),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_neon, 8),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_neon, 8),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_neon, 8),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_neon, 8),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_neon, 8),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_neon, 8),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_neon, 8),

  make_tuple(256, 256, &avm_highbd_sad256x256x4d_neon, 10),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_neon, 10),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_neon, 10),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_neon, 10),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_neon, 10),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_neon, 10),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_neon, 10),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_neon, 10),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_neon, 10),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_neon, 10),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_neon, 10),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_neon, 10),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_neon, 10),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_neon, 10),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_neon, 10),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_neon, 10),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_neon, 10),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_neon, 10),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_neon, 10),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_neon, 10),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_neon, 10),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_neon, 10),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_neon, 10),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_neon, 10),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_neon, 10),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_neon, 10),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_neon, 10),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_neon, 10),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_neon, 10),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_neon, 10),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_neon, 10),

  make_tuple(256, 256, &avm_highbd_sad256x256x4d_neon, 12),
  make_tuple(256, 128, &avm_highbd_sad256x128x4d_neon, 12),
  make_tuple(128, 256, &avm_highbd_sad128x256x4d_neon, 12),
  make_tuple(128, 128, &avm_highbd_sad128x128x4d_neon, 12),
  make_tuple(128, 64, &avm_highbd_sad128x64x4d_neon, 12),
  make_tuple(64, 128, &avm_highbd_sad64x128x4d_neon, 12),
  make_tuple(64, 64, &avm_highbd_sad64x64x4d_neon, 12),
  make_tuple(64, 32, &avm_highbd_sad64x32x4d_neon, 12),
  make_tuple(32, 64, &avm_highbd_sad32x64x4d_neon, 12),
  make_tuple(32, 32, &avm_highbd_sad32x32x4d_neon, 12),
  make_tuple(32, 16, &avm_highbd_sad32x16x4d_neon, 12),
  make_tuple(16, 32, &avm_highbd_sad16x32x4d_neon, 12),
  make_tuple(16, 16, &avm_highbd_sad16x16x4d_neon, 12),
  make_tuple(16, 8, &avm_highbd_sad16x8x4d_neon, 12),
  make_tuple(8, 16, &avm_highbd_sad8x16x4d_neon, 12),
  make_tuple(8, 8, &avm_highbd_sad8x8x4d_neon, 12),
  make_tuple(8, 4, &avm_highbd_sad8x4x4d_neon, 12),
  make_tuple(4, 8, &avm_highbd_sad4x8x4d_neon, 12),
  make_tuple(4, 4, &avm_highbd_sad4x4x4d_neon, 12),
  make_tuple(4, 16, &avm_highbd_sad4x16x4d_neon, 12),
  make_tuple(16, 4, &avm_highbd_sad16x4x4d_neon, 12),
  make_tuple(8, 32, &avm_highbd_sad8x32x4d_neon, 12),
  make_tuple(32, 8, &avm_highbd_sad32x8x4d_neon, 12),
  make_tuple(16, 64, &avm_highbd_sad16x64x4d_neon, 12),
  make_tuple(64, 16, &avm_highbd_sad64x16x4d_neon, 12),
  make_tuple(4, 32, &avm_highbd_sad4x32x4d_neon, 12),
  make_tuple(32, 4, &avm_highbd_sad32x4x4d_neon, 12),
  make_tuple(8, 64, &avm_highbd_sad8x64x4d_neon, 12),
  make_tuple(64, 8, &avm_highbd_sad64x8x4d_neon, 12),
  make_tuple(4, 64, &avm_highbd_sad4x64x4d_neon, 12),
  make_tuple(64, 4, &avm_highbd_sad64x4x4d_neon, 12),
};
INSTANTIATE_TEST_SUITE_P(NEON, SADx4Test, ::testing::ValuesIn(x4d_neon_tests));
#endif  // HAVE_NEON

}  // namespace
