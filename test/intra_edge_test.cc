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

using libaom_test::FunctionEquivalenceTest;

namespace {

template <typename F, typename T>
class FilterEdgeTest : public FunctionEquivalenceTest<F> {
 protected:
  static const int kIterations = 1000000;
  static const int kMaxEdge = 2 * 64;
  static const int kBufSize = kMaxEdge + 32;
  static const int kOffset = 15;

  virtual ~FilterEdgeTest() {}

  virtual void Execute(T *edge_tst) = 0;

  void Common() {
    edge_ref_ = &edge_ref_data_[kOffset];
    edge_tst_ = &edge_tst_data_[kOffset];

    Execute(edge_tst_);

    for (int r = 0; r < size_; ++r) {
      ASSERT_EQ(edge_ref_[r], edge_tst_[r]);
    }
  }

  T edge_ref_data_[kBufSize];
  T edge_tst_data_[kBufSize];

  T *edge_ref_;
  T *edge_tst_;

  int size_;
  int strength_;
};

//////////////////////////////////////////////////////////////////////////////
// High bit-depth version
//////////////////////////////////////////////////////////////////////////////

typedef void (*FEHB)(uint16_t *p, int size, int strength);
typedef libaom_test::FuncParam<FEHB> FilterEdgeTestFuncsHBD;

class FilterEdgeTestHB : public FilterEdgeTest<FEHB, uint16_t> {
 protected:
  void Execute(uint16_t *edge_tst) {
    params_.ref_func(edge_ref_, size_, strength_);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(edge_tst, size_, strength_));
  }
  int bit_depth_;
};

TEST_P(FilterEdgeTestHB, RandomValues) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    switch (rng_(3)) {
      case 0: bit_depth_ = 8; break;
      case 1: bit_depth_ = 10; break;
      default: bit_depth_ = 12; break;
    }
    const int hi = 1 << bit_depth_;
    strength_ = this->rng_(4);
    size_ = 4 * (this->rng_(128 / 4) + 1) + 1;

    int i, pix = 0;
    for (i = 0; i < kOffset + size_; ++i) {
      pix = rng_(hi);
      edge_ref_data_[i] = pix;
      edge_tst_data_[i] = pix;
    }

    Common();
  }
}

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, FilterEdgeTestHB,
                         ::testing::Values(FilterEdgeTestFuncsHBD(
                             av1_filter_intra_edge_high_c,
                             av1_filter_intra_edge_high_sse4_1)));
#endif  // HAVE_SSE4_1

// Speed tests

TEST_P(FilterEdgeTestHB, DISABLED_Speed) {
  const int test_count = 10000000;
  size_ = kMaxEdge;
  strength_ = 1;
  bit_depth_ = 12;
  const int hi = 1 << bit_depth_;
  for (int i = 0; i < kOffset + size_; ++i) {
    edge_tst_data_[i] = rng_(hi);
  }
  edge_tst_ = &edge_tst_data_[kOffset];
  for (int iter = 0; iter < test_count; ++iter) {
    ASM_REGISTER_STATE_CHECK(params_.tst_func(edge_tst_, size_, strength_));
    // iterate over filter strengths (1,2,3)
    strength_ = (strength_ == 3) ? 1 : strength_ + 1;
  }
}

}  // namespace
