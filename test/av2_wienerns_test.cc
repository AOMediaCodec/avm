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

#include <cmath>
#include <cstring>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/restoration.h"
#include "av2/encoder/pickrst.h"
#include "avm_ports/avm_timer.h"

using libavm_test::ACMRandom;

namespace {

typedef void (*WienernsStatsBatchFunc)(int64_t *A, const int16_t *feat,
                                       int feat_stride, int n, int dim);

typedef std::tuple<WienernsStatsBatchFunc> WienernsStatsBatchParam;

// Rows of the feature matrix, and their stride in the tests.
constexpr int kMaxDim = WIENERNS_TAPS_MAX + 1;
constexpr int kStrides[] = { WIENERNS_STATS_BATCH, WIENERNS_STATS_BATCH + 8 };
constexpr int kMaxStride = WIENERNS_STATS_BATCH + 8;
// Entries of A past dim * dim, to catch writes outside the matrix.
constexpr int kGuard = 16;
constexpr int kMaxVal = WIENERNS_STATS_MAX_ABS - 1;

class WienernsStatsBatchTest
    : public ::testing::TestWithParam<WienernsStatsBatchParam> {
 public:
  virtual void SetUp() {
    target_func_ = GET_PARAM(0);
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  // Fills the features with random values. Odd iter values use the full
  // allowed range, even ones use small values, like the 8-bit features.
  void FillRandom(int iter) {
    const int range = (iter & 1) ? 2 * kMaxVal + 1 : 511;
    for (int i = 0; i < kMaxDim * kMaxStride; ++i) {
      feat_[i] = static_cast<int16_t>(static_cast<int>(rnd_.Rand31() % range) -
                                      range / 2);
    }
  }

  // Runs both functions on the same random A and compares all of it.
  void RunAndCheck(int stride, int n, int dim) {
    for (int i = 0; i < dim * dim + kGuard; ++i) {
      const int64_t v = (static_cast<int64_t>(rnd_.Rand31()) << 10) -
                        (static_cast<int64_t>(1) << 40);
      A_ref_[i] = v;
      A_tst_[i] = v;
    }
    av2_wienerns_stats_batch_c(A_ref_, feat_, stride, n, dim);
    ASM_REGISTER_STATE_CHECK(target_func_(A_tst_, feat_, stride, n, dim));
    for (int i = 0; i < dim * dim + kGuard; ++i) {
      ASSERT_EQ(A_ref_[i], A_tst_[i])
          << "A mismatch at (" << i / dim << ", " << i % dim
          << ") stride=" << stride << " n=" << n << " dim=" << dim;
    }
  }

  WienernsStatsBatchFunc target_func_;
  ACMRandom rnd_;
  int16_t feat_[kMaxDim * kMaxStride];
  int64_t A_ref_[kMaxDim * kMaxDim + kGuard];
  int64_t A_tst_[kMaxDim * kMaxDim + kGuard];
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WienernsStatsBatchTest);

// Every dim, n and stride once, with both value ranges.
TEST_P(WienernsStatsBatchTest, AllSizes) {
  for (int stride : kStrides) {
    for (int dim = 1; dim <= kMaxDim; ++dim) {
      for (int n = 8; n <= WIENERNS_STATS_BATCH; n += 8) {
        for (int iter = 0; iter < 2; ++iter) {
          FillRandom(iter);
          RunAndCheck(stride, n, dim);
          if (HasFatalFailure()) return;
        }
      }
    }
  }
}

// Random dim, n, stride and values.
TEST_P(WienernsStatsBatchTest, RandomValues) {
  constexpr int kIterations = 1000;
  constexpr int kNumStrides = sizeof(kStrides) / sizeof(kStrides[0]);
  for (int iter = 0; iter < kIterations; ++iter) {
    const int stride = kStrides[rnd_(kNumStrides)];
    const int dim = 1 + rnd_(kMaxDim);
    const int n = 8 * (1 + rnd_(WIENERNS_STATS_BATCH / 8));
    FillRandom(iter);
    RunAndCheck(stride, n, dim);
    if (HasFatalFailure()) return;
  }
}

// The largest allowed values, which give the largest sums.
TEST_P(WienernsStatsBatchTest, ExtremeValues) {
  const int16_t kVals[] = { -kMaxVal, kMaxVal, 0, 1, -1 };
  for (int stride : kStrides) {
    for (int pattern = 0; pattern < 4; ++pattern) {
      for (int r = 0; r < kMaxDim; ++r) {
        for (int x = 0; x < stride; ++x) {
          int16_t v = kVals[0];
          switch (pattern) {
            case 0: v = kVals[0]; break;
            case 1: v = kVals[1]; break;
            case 2: v = kVals[(r + x) & 1]; break;
            default: v = kVals[rnd_.Rand8() % 5]; break;
          }
          feat_[r * stride + x] = v;
        }
      }
      for (int dim = 1; dim <= kMaxDim; ++dim) {
        RunAndCheck(stride, WIENERNS_STATS_BATCH, dim);
        if (HasFatalFailure()) return;
      }
    }
  }
}

// Calls with n == 0 do nothing.
TEST_P(WienernsStatsBatchTest, NoPixels) {
  for (int i = 0; i < kMaxDim * kMaxStride; ++i) feat_[i] = 1;
  RunAndCheck(WIENERNS_STATS_BATCH, 0, kMaxDim);
}

TEST_P(WienernsStatsBatchTest, DISABLED_Speed) {
  for (int i = 0; i < kMaxDim * kMaxStride; ++i) {
    feat_[i] =
        static_cast<int16_t>(static_cast<int>(rnd_.Rand16() % 4096) - 2048);
  }
  memset(A_ref_, 0, sizeof(A_ref_));
  memset(A_tst_, 0, sizeof(A_tst_));
  const int kRuns = 200000;
  for (int dim : { 7, 13, 17, 21, 33 }) {
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < kRuns; ++i) {
      av2_wienerns_stats_batch_c(A_ref_, feat_, WIENERNS_STATS_BATCH,
                                 WIENERNS_STATS_BATCH, dim);
    }
    avm_usec_timer_mark(&timer);
    const double time_c = static_cast<double>(avm_usec_timer_elapsed(&timer));

    avm_usec_timer_start(&timer);
    for (int i = 0; i < kRuns; ++i) {
      target_func_(A_tst_, feat_, WIENERNS_STATS_BATCH, WIENERNS_STATS_BATCH,
                   dim);
    }
    avm_usec_timer_mark(&timer);
    const double time_simd =
        static_cast<double>(avm_usec_timer_elapsed(&timer));

    printf("dim %2d: C %9.0fus / SIMD %9.0fus (speedup: %5.2fx)\n", dim, time_c,
           time_simd, time_c / time_simd);
  }
}

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, WienernsStatsBatchTest,
                         ::testing::Values(av2_wienerns_stats_batch_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, WienernsStatsBatchTest,
                         ::testing::Values(av2_wienerns_stats_batch_avx2));
#endif  // HAVE_AVX2

}  // namespace
