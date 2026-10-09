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

#include <cstdlib>
#include <cstring>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_ports/mem.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

namespace {

using libavm_test::ACMRandom;
using std::make_tuple;

using IntProRowFunc = void (*)(int16_t *hbuf, const uint16_t *ref,
                               const int ref_stride, const int width,
                               const int height, int norm_factor);

// Params: width, height, bit_depth, asm function, c function.
using IntProRowParam = std::tuple<int, int, int, IntProRowFunc, IntProRowFunc>;

class IntProRowTest : public ::testing::TestWithParam<IntProRowParam> {
 public:
  IntProRowTest()
      : width_(GET_PARAM(0)), height_(GET_PARAM(1)), bit_depth_(GET_PARAM(2)),
        asm_func_(GET_PARAM(3)), c_func_(GET_PARAM(4)), source_data_(nullptr),
        hbuf_asm_(nullptr), hbuf_c_(nullptr) {}

 protected:
  static const int kDataAlignment = 32;

  void SetUp() override {
    source_stride_ = (width_ + 31) & ~31;
    const int buf_size = source_stride_ * height_;
    source_data_ = static_cast<uint16_t *>(
        avm_memalign(kDataAlignment, buf_size * sizeof(*source_data_)));
    ASSERT_NE(source_data_, nullptr);
    memset(source_data_, 0, buf_size * sizeof(*source_data_));

    hbuf_asm_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, sizeof(*hbuf_asm_) * width_));
    ASSERT_NE(hbuf_asm_, nullptr);
    hbuf_c_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, sizeof(*hbuf_c_) * width_));
    ASSERT_NE(hbuf_c_, nullptr);
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  void TearDown() override {
    libavm_test::ClearSystemState();
    avm_free(source_data_);
    source_data_ = nullptr;
    avm_free(hbuf_c_);
    hbuf_c_ = nullptr;
    avm_free(hbuf_asm_);
    hbuf_asm_ = nullptr;
  }

  int ComputeNormFactor() const {
    int h_log2 = 0;
    while ((1 << (h_log2 + 1)) <= height_) ++h_log2;
    return (h_log2 - 1) + (bit_depth_ - 8);
  }

  void FillConstant(uint16_t val) {
    for (int i = 0; i < source_stride_ * height_; ++i) {
      source_data_[i] = val;
    }
  }

  void FillRandom() {
    const uint16_t mask = (1 << bit_depth_) - 1;
    for (int i = 0; i < source_stride_ * height_; ++i) {
      source_data_[i] = rnd_.Rand16() & mask;
    }
  }

  void RunComparison() {
    const int norm_factor = ComputeNormFactor();
    memset(hbuf_c_, 0, sizeof(*hbuf_c_) * width_);
    memset(hbuf_asm_, 0, sizeof(*hbuf_asm_) * width_);
    API_REGISTER_STATE_CHECK(c_func_(hbuf_c_, source_data_, source_stride_,
                                     width_, height_, norm_factor));
    API_REGISTER_STATE_CHECK(asm_func_(hbuf_asm_, source_data_, source_stride_,
                                       width_, height_, norm_factor));
    for (int i = 0; i < width_; ++i) {
      ASSERT_EQ(hbuf_c_[i], hbuf_asm_[i])
          << "Mismatch at col " << i << " for " << width_ << "x" << height_
          << " bd=" << bit_depth_;
    }
  }

  int width_;
  int height_;
  int bit_depth_;
  int source_stride_;
  IntProRowFunc asm_func_;
  IntProRowFunc c_func_;
  uint16_t *source_data_;
  int16_t *hbuf_asm_;
  int16_t *hbuf_c_;
  ACMRandom rnd_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(IntProRowTest);

TEST_P(IntProRowTest, MinValue) {
  FillConstant(0);
  RunComparison();
}

TEST_P(IntProRowTest, MaxValue) {
  FillConstant((1 << bit_depth_) - 1);
  RunComparison();
}

TEST_P(IntProRowTest, Random) {
  for (int iter = 0; iter < 20; ++iter) {
    FillRandom();
    RunComparison();
  }
}

using IntProColFunc = void (*)(int16_t *vbuf, const uint16_t *ref,
                               const int ref_stride, const int width,
                               const int height, int norm_factor);

// Params: width, height, bit_depth, asm function, c function.
using IntProColParam = std::tuple<int, int, int, IntProColFunc, IntProColFunc>;

class IntProColTest : public ::testing::TestWithParam<IntProColParam> {
 public:
  IntProColTest()
      : width_(GET_PARAM(0)), height_(GET_PARAM(1)), bit_depth_(GET_PARAM(2)),
        asm_func_(GET_PARAM(3)), c_func_(GET_PARAM(4)), source_data_(nullptr),
        vbuf_asm_(nullptr), vbuf_c_(nullptr) {}

 protected:
  static const int kDataAlignment = 32;

  void SetUp() override {
    source_stride_ = (width_ + 31) & ~31;
    const int buf_size = source_stride_ * height_;
    source_data_ = static_cast<uint16_t *>(
        avm_memalign(kDataAlignment, buf_size * sizeof(*source_data_)));
    ASSERT_NE(source_data_, nullptr);
    memset(source_data_, 0, buf_size * sizeof(*source_data_));

    vbuf_asm_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, sizeof(*vbuf_asm_) * height_));
    ASSERT_NE(vbuf_asm_, nullptr);
    vbuf_c_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, sizeof(*vbuf_c_) * height_));
    ASSERT_NE(vbuf_c_, nullptr);
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  void TearDown() override {
    libavm_test::ClearSystemState();
    avm_free(source_data_);
    source_data_ = nullptr;
    avm_free(vbuf_c_);
    vbuf_c_ = nullptr;
    avm_free(vbuf_asm_);
    vbuf_asm_ = nullptr;
  }

  int ComputeNormFactor() const {
    int w_log2 = 0;
    while ((1 << (w_log2 + 1)) <= width_) ++w_log2;
    return (w_log2 - 1) + (bit_depth_ - 8);
  }

  void FillConstant(uint16_t val) {
    for (int i = 0; i < source_stride_ * height_; ++i) {
      source_data_[i] = val;
    }
  }

  void FillRandom() {
    const uint16_t mask = (1 << bit_depth_) - 1;
    for (int i = 0; i < source_stride_ * height_; ++i) {
      source_data_[i] = rnd_.Rand16() & mask;
    }
  }

  void RunComparison() {
    const int norm_factor = ComputeNormFactor();
    memset(vbuf_c_, 0, sizeof(*vbuf_c_) * height_);
    memset(vbuf_asm_, 0, sizeof(*vbuf_asm_) * height_);
    API_REGISTER_STATE_CHECK(c_func_(vbuf_c_, source_data_, source_stride_,
                                     width_, height_, norm_factor));
    API_REGISTER_STATE_CHECK(asm_func_(vbuf_asm_, source_data_, source_stride_,
                                       width_, height_, norm_factor));
    for (int i = 0; i < height_; ++i) {
      ASSERT_EQ(vbuf_c_[i], vbuf_asm_[i])
          << "Mismatch at row " << i << " for " << width_ << "x" << height_
          << " bd=" << bit_depth_;
    }
  }

  int width_;
  int height_;
  int bit_depth_;
  int source_stride_;
  IntProColFunc asm_func_;
  IntProColFunc c_func_;
  uint16_t *source_data_;
  int16_t *vbuf_asm_;
  int16_t *vbuf_c_;
  ACMRandom rnd_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(IntProColTest);

TEST_P(IntProColTest, MinValue) {
  FillConstant(0);
  RunComparison();
}

TEST_P(IntProColTest, MaxValue) {
  FillConstant((1 << bit_depth_) - 1);
  RunComparison();
}

TEST_P(IntProColTest, Random) {
  for (int iter = 0; iter < 20; ++iter) {
    FillRandom();
    RunComparison();
  }
}

using VectorVarFunc = int (*)(const int16_t *ref, const int16_t *src,
                              const int bwl);
using VecVarParam = std::tuple<int, VectorVarFunc, VectorVarFunc>;

class VectorVarTest : public ::testing::TestWithParam<VecVarParam> {
 public:
  VectorVarTest()
      : bwl_(GET_PARAM(0)), c_func_(GET_PARAM(1)), simd_func_(GET_PARAM(2)),
        ref_vector_(nullptr), src_vector_(nullptr) {}

 protected:
  static const int kDataAlignment = 32;
  static const int kMaxRange = 512;

  void SetUp() override {
    width_ = 4 << bwl_;
    ref_vector_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, (width_ + 16) * sizeof(*ref_vector_)));
    ASSERT_NE(ref_vector_, nullptr);
    src_vector_ = static_cast<int16_t *>(
        avm_memalign(kDataAlignment, width_ * sizeof(*src_vector_)));
    ASSERT_NE(src_vector_, nullptr);
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  void TearDown() override {
    libavm_test::ClearSystemState();
    avm_free(ref_vector_);
    ref_vector_ = nullptr;
    avm_free(src_vector_);
    src_vector_ = nullptr;
  }

  void FillConstant(int16_t ref_val, int16_t src_val) {
    for (int i = 0; i < width_ + 16; ++i) {
      ref_vector_[i] = ref_val;
    }
    for (int i = 0; i < width_; ++i) {
      src_vector_[i] = src_val;
    }
  }

  void FillRandom() {
    for (int i = 0; i < width_ + 16; ++i) {
      ref_vector_[i] = rnd_.Rand16() % kMaxRange;
    }
    for (int i = 0; i < width_; ++i) {
      src_vector_[i] = rnd_.Rand16() % kMaxRange;
    }
  }

  void CheckEquivalence(int ref_offset = 0) {
    int c_var = 0, simd_var = 0;
    API_REGISTER_STATE_CHECK(
        c_var = c_func_(ref_vector_ + ref_offset, src_vector_, bwl_));
    API_REGISTER_STATE_CHECK(
        simd_var = simd_func_(ref_vector_ + ref_offset, src_vector_, bwl_));
    ASSERT_EQ(c_var, simd_var)
        << "Mismatch for bwl=" << bwl_ << " ref_offset=" << ref_offset;
  }

  int width_;
  int bwl_;
  VectorVarFunc c_func_;
  VectorVarFunc simd_func_;
  int16_t *ref_vector_;
  int16_t *src_vector_;
  ACMRandom rnd_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(VectorVarTest);

TEST_P(VectorVarTest, MaxVar) {
  FillConstant(0, kMaxRange - 1);
  CheckEquivalence();
}

TEST_P(VectorVarTest, MaxVarRev) {
  FillConstant(kMaxRange - 1, 0);
  CheckEquivalence();
}

TEST_P(VectorVarTest, AlternatingMaxDiff) {
  for (int i = 0; i < width_; ++i) {
    ref_vector_[i] = (i & 1) ? (kMaxRange - 1) : 0;
    src_vector_[i] = (i & 1) ? 0 : (kMaxRange - 1);
  }
  CheckEquivalence();
}

TEST_P(VectorVarTest, ZeroDiff) {
  FillConstant(0, 0);
  CheckEquivalence();
}

TEST_P(VectorVarTest, ZeroDiffMax) {
  FillConstant(kMaxRange - 1, kMaxRange - 1);
  CheckEquivalence();
}

TEST_P(VectorVarTest, Constant) {
  FillConstant(30, 90);
  CheckEquivalence();
}

TEST_P(VectorVarTest, Random) {
  for (int i = 0; i < 100; ++i) {
    FillRandom();
    CheckEquivalence(i & 15);
  }
}

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, IntProRowTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_row_sse2),
                       ::testing::Values(&avm_int_pro_row_c)));

INSTANTIATE_TEST_SUITE_P(
    SSE2, IntProColTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_col_sse2),
                       ::testing::Values(&avm_int_pro_col_c)));

INSTANTIATE_TEST_SUITE_P(
    SSE2, VectorVarTest,
    ::testing::Values(make_tuple(2, &avm_vector_var_c, &avm_vector_var_sse2),
                      make_tuple(3, &avm_vector_var_c, &avm_vector_var_sse2),
                      make_tuple(4, &avm_vector_var_c, &avm_vector_var_sse2),
                      make_tuple(5, &avm_vector_var_c, &avm_vector_var_sse2),
                      make_tuple(6, &avm_vector_var_c, &avm_vector_var_sse2)));
#endif  // HAVE_SSE2

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, IntProRowTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_row_avx2),
                       ::testing::Values(&avm_int_pro_row_c)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, IntProColTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_col_avx2),
                       ::testing::Values(&avm_int_pro_col_c)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, VectorVarTest,
    ::testing::Values(make_tuple(2, &avm_vector_var_c, &avm_vector_var_avx2),
                      make_tuple(3, &avm_vector_var_c, &avm_vector_var_avx2),
                      make_tuple(4, &avm_vector_var_c, &avm_vector_var_avx2),
                      make_tuple(5, &avm_vector_var_c, &avm_vector_var_avx2),
                      make_tuple(6, &avm_vector_var_c, &avm_vector_var_avx2)));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, IntProRowTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_row_neon),
                       ::testing::Values(&avm_int_pro_row_c)));

INSTANTIATE_TEST_SUITE_P(
    NEON, IntProColTest,
    ::testing::Combine(::testing::Values(16, 32, 64, 128, 256),
                       ::testing::Values(16, 32, 64, 112, 128, 160, 224, 256,
                                         352),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(&avm_int_pro_col_neon),
                       ::testing::Values(&avm_int_pro_col_c)));

INSTANTIATE_TEST_SUITE_P(
    NEON, VectorVarTest,
    ::testing::Values(make_tuple(2, &avm_vector_var_c, &avm_vector_var_neon),
                      make_tuple(3, &avm_vector_var_c, &avm_vector_var_neon),
                      make_tuple(4, &avm_vector_var_c, &avm_vector_var_neon),
                      make_tuple(5, &avm_vector_var_c, &avm_vector_var_neon),
                      make_tuple(6, &avm_vector_var_c, &avm_vector_var_neon)));
#endif  // HAVE_NEON

}  // namespace
