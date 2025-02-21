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
