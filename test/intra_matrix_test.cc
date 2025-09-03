/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/register_state_check.h"
#include "test/function_equivalence_test.h"

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_ports/aom_timer.h"
#include "av1/common/enums.h"
#include "av1/common/intra_dip.h"
#include "av1/common/intra_matrix.h"

using libaom_test::FunctionEquivalenceTest;

namespace {

template <typename F, typename T>
class IntraMatrixTest : public FunctionEquivalenceTest<F> {
 protected:
  static const int kIterations = 1000000;
  static const int kBufSize = 8 * 8;

  virtual ~IntraMatrixTest() {}

  virtual void Execute(T *dip_tst) = 0;

  void Common() {
    dip_ref_ = &dip_ref_data_[0];
    dip_tst_ = &dip_tst_data_[0];

    Execute(dip_tst_);

    for (int r = 0; r < kBufSize; ++r) {
      ASSERT_EQ(dip_ref_[r], dip_tst_[r]);
    }
  }

  T dip_arr_[DIP_ROWS * DIP_COLS];
  T dip_feat_[DIP_COLS];

  T dip_ref_data_[kBufSize];
  T dip_tst_data_[kBufSize];

  T *dip_ref_;
  T *dip_tst_;
};

//////////////////////////////////////////////////////////////////////////////
// High bit-depth version
//////////////////////////////////////////////////////////////////////////////

typedef void (*IMHB)(const uint16_t *A, const uint16_t *B, uint16_t *C, int bd);
typedef libaom_test::FuncParam<IMHB> IntraMatrixTestFuncsHBD;

class IntraMatrixTestHB : public IntraMatrixTest<IMHB, uint16_t> {
 protected:
  void Execute(uint16_t *dip_tst) {
    params_.ref_func(dip_arr_, dip_feat_, dip_ref_, bit_depth_);
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(dip_arr_, dip_feat_, dip_tst, bit_depth_));
  }
  int bit_depth_;
};

TEST_P(IntraMatrixTestHB, RandomValues) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    switch (rng_(3)) {
      case 0: bit_depth_ = 8; break;
      case 1: bit_depth_ = 10; break;
      default: bit_depth_ = 12; break;
    }
    const int hi = 1 << bit_depth_;

    for (int i = 0; i < 16; ++i) {
      dip_feat_[i] = rng_(hi);
    }
    int mode = iter % INTRA_DIP_MODE_CNT;
    for (int r = 0; r < DIP_ROWS; ++r) {
      for (int c = 0; c < DIP_FEATURES; ++c) {
        dip_arr_[r * DIP_COLS + c] = av1_intra_matrix_weights[mode][r][c];
      }
    }

    Common();
  }
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, IntraMatrixTestHB,
                         ::testing::Values(IntraMatrixTestFuncsHBD(
                             av1_dip_matrix_multiplication_c,
                             av1_dip_matrix_multiplication_avx2)));
#endif  // HAVE_AVX2

// Speed tests

TEST_P(IntraMatrixTestHB, DISABLED_Speed) {
  const int test_count = 10000000;
  bit_depth_ = 12;
  const int hi = 1 << bit_depth_;
  for (int i = 0; i < 16; ++i) {
    dip_feat_[i] = rng_(hi);
  }
  for (int r = 0; r < 64; ++r) {
    for (int c = 0; c < 11; ++c) {
      dip_arr_[r * 16 + c] = av1_intra_matrix_weights[0][r][c];
    }
  }
  dip_tst_ = &dip_tst_data_[0];
  for (int iter = 0; iter < test_count; ++iter) {
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(dip_arr_, dip_feat_, dip_tst_, bit_depth_));
  }
}

}  // namespace

//////////////////////////////////////////////////////////////////////////////
// ResampleOutputTest
//////////////////////////////////////////////////////////////////////////////

typedef void (*ResampleOutputFunc)(uint16_t *dst, int dst_stride,
                                   const uint16_t *above_row,
                                   const uint16_t *left_col,
                                   uint16_t *ml_output, int bw_log2,
                                   int bh_log2, int transpose);

typedef libaom_test::FuncParam<ResampleOutputFunc> ResampleOutputTestFuncs;

class ResampleOutputTest : public FunctionEquivalenceTest<ResampleOutputFunc> {
 protected:
  static const int kMaxWidth = 64;
  static const int kMaxHeight = 64;
  static const int kBufSize = kMaxWidth * kMaxHeight;
  static const int kMlOutputSize = 8 * 8;
  static const int kContextSize = kMaxWidth + kMaxHeight + 1;

  ResampleOutputTest() {
    dst_ref_ = &dst_ref_data_[0];
    dst_tst_ = &dst_tst_data_[0];
    above_row_ = &context_data_[1];
    left_col_ = &context_data_[kMaxWidth + 2];
    ml_output_ = &ml_output_data_[0];
  }

  virtual ~ResampleOutputTest() {}

  int get_log2(int val) {
    switch (val) {
      case 4: return 2;
      case 8: return 3;
      case 16: return 4;
      case 32: return 5;
      case 64: return 6;
      default: EXPECT_TRUE(false) << "Invalid block size"; return 0;
    }
  }

  void RunCorrectnessTest() {
    const int block_sizes[] = { 8, 16, 32, 64 };
    for (int bw : block_sizes) {
      for (int bh : block_sizes) {
        // Data-driven intra prediction only applies to blocks with w*h >= 128.
        if (bw * bh < 128) continue;
        for (int transpose = 0; transpose < 2; ++transpose) {
          const int bw_log2 = get_log2(bw);
          const int bh_log2 = get_log2(bh);
          const int dst_stride = kMaxWidth;
          const int bit_depth = 12;
          const int hi = (1 << bit_depth) - 1;

          for (int i = 0; i < kContextSize; ++i) {
            context_data_[i] = rng_(hi);
          }
          for (int i = 0; i < kMlOutputSize; ++i) {
            // The range of ml_output is clipped to the corresponding bitdepth.
            // i.e. v = clip_pixel_highbd(v, bit_depth);
            // See av1_dip_matrix_mulplication.
            ml_output_data_[i] = rng_(hi);
          }
          // The top-left corner is shared between above_row[-1] and
          // left_col[-1]
          above_row_[-1] = context_data_[0];
          left_col_[-1] = context_data_[0];

          params_.ref_func(dst_ref_, dst_stride, above_row_, left_col_,
                           ml_output_, bw_log2, bh_log2, transpose);
          ASM_REGISTER_STATE_CHECK(
              params_.tst_func(dst_tst_, dst_stride, above_row_, left_col_,
                               ml_output_, bw_log2, bh_log2, transpose));

          for (int r = 0; r < bh; ++r) {
            for (int c = 0; c < bw; ++c) {
              ASSERT_EQ(dst_ref_[r * dst_stride + c],
                        dst_tst_[r * dst_stride + c])
                  << "Mismatch at (" << c << ", " << r << ") for block size "
                  << bw << "x" << bh << " (transpose=" << transpose << ")";
            }
          }
        }
      }
    }
  }

  void RunSpeedTest() {
    const int block_sizes[] = { 8, 16, 32, 64 };
    for (int bw : block_sizes) {
      for (int bh : block_sizes) {
        // Data-driven intra prediction only applies to blocks with w*h >= 128.
        if (bw * bh < 128) continue;
        for (int transpose = 0; transpose < 2; ++transpose) {
          const int bw_log2 = get_log2(bw);
          const int bh_log2 = get_log2(bh);
          const int dst_stride = kMaxWidth;
          const int bit_depth = 12;
          const int hi = (1 << bit_depth) - 1;
          const int kIterations = 100000;

          for (int i = 0; i < kContextSize; ++i) {
            context_data_[i] = rng_(hi);
          }
          for (int i = 0; i < kMlOutputSize; ++i) {
            // The range of ml_output is clipped to the corresponding bitdepth.
            // i.e. v = clip_pixel_highbd(v, bit_depth);
            // See av1_dip_matrix_mulplication.
            ml_output_data_[i] = rng_(hi);
          }
          above_row_[-1] = context_data_[0];
          left_col_[-1] = context_data_[0];

          aom_usec_timer ref_timer, tst_timer;

          aom_usec_timer_start(&ref_timer);
          for (int i = 0; i < kIterations; ++i) {
            params_.ref_func(dst_ref_, dst_stride, above_row_, left_col_,
                             ml_output_, bw_log2, bh_log2, transpose);
          }
          aom_usec_timer_mark(&ref_timer);
          const double ref_time =
              static_cast<double>(aom_usec_timer_elapsed(&ref_timer));

          aom_usec_timer_start(&tst_timer);
          for (int i = 0; i < kIterations; ++i) {
            params_.tst_func(dst_tst_, dst_stride, above_row_, left_col_,
                             ml_output_, bw_log2, bh_log2, transpose);
          }
          aom_usec_timer_mark(&tst_timer);
          const double tst_time =
              static_cast<double>(aom_usec_timer_elapsed(&tst_timer));

          printf(
              "Block %2dx%2d (T=%d): C time = %7.2f us, SIMD time = %7.2f us, "
              "Speedup = %4.2fx\n",
              bw, bh, transpose, ref_time, tst_time, ref_time / tst_time);
        }
      }
    }
  }

  uint16_t dst_ref_data_[kBufSize];
  uint16_t dst_tst_data_[kBufSize];
  uint16_t context_data_[kContextSize];
  uint16_t ml_output_data_[kMlOutputSize];

  uint16_t *dst_ref_;
  uint16_t *dst_tst_;
  uint16_t *above_row_;
  uint16_t *left_col_;
  uint16_t *ml_output_;
};

TEST_P(ResampleOutputTest, Correctness) { RunCorrectnessTest(); }

TEST_P(ResampleOutputTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, ResampleOutputTest,
                         ::testing::Values(ResampleOutputTestFuncs(
                             resample_output_c, resample_output_avx2)));
#endif  // HAVE_AVX2
