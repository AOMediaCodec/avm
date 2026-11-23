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

#include <cstdio>
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"

#include "av2/common/enums.h"

#include "test/acm_random.h"
#include "test/function_equivalence_test.h"
#include "test/register_state_check.h"

#define WEDGE_WEIGHT_BITS 6
#define MAX_MASK_VALUE (1 << (WEDGE_WEIGHT_BITS))

using libavm_test::ACMRandom;
using libavm_test::FunctionEquivalenceTest;

namespace {

static const int16_t kInt13Max = (1 << 12) - 1;

//////////////////////////////////////////////////////////////////////////////
// av2_wedge_sse_from_residuals - functionality
//////////////////////////////////////////////////////////////////////////////

class WedgeUtilsSSEFuncTest : public testing::Test {
 protected:
  WedgeUtilsSSEFuncTest() : rng_(ACMRandom::DeterministicSeed()) {}

  static const int kIterations = 1000;

  ACMRandom rng_;
};

static uint64_t sse_from_residuals(const int16_t *r0, const int16_t *r1,
                                   const uint8_t *m, int N) {
  uint64_t acc = 0;
  for (int i = 0; i < N; i++) {
    const int32_t m0 = m[i];
    const int32_t m1 = MAX_MASK_VALUE - m0;
    const int32_t r = m0 * r0[i] + m1 * r1[i];
    acc += r * r;
  }
  return ROUND_POWER_OF_TWO(acc, 2 * WEDGE_WEIGHT_BITS);
}

TEST_F(WedgeUtilsSSEFuncTest, ResidualBlendingMethod) {
  DECLARE_ALIGNED(32, int16_t, r0[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      r1[i] = rng_(2 * INT8_MAX - 2 * INT8_MIN + 1) + 2 * INT8_MIN;
      d[i] = rng_(2 * INT8_MAX - 2 * INT8_MIN + 1) + 2 * INT8_MIN;
      m[i] = rng_(MAX_MASK_VALUE + 1);
    }

    const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);

    for (int i = 0; i < N; i++) r0[i] = r1[i] + d[i];

    const uint64_t ref_res = sse_from_residuals(r0, r1, m, N);
    const uint64_t tst_res = av2_wedge_sse_from_residuals(r1, d, m, N);

    ASSERT_EQ(ref_res, tst_res);
  }
}

//////////////////////////////////////////////////////////////////////////////
// av2_wedge_sse_from_residuals - optimizations
//////////////////////////////////////////////////////////////////////////////

typedef uint64_t (*FSSE)(const int16_t *r1, const int16_t *d, const uint8_t *m,
                         int N);
typedef libavm_test::FuncParam<FSSE> TestFuncsFSSE;

class WedgeUtilsSSEOptTest : public FunctionEquivalenceTest<FSSE> {
 protected:
  static const int kIterations = 10000;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WedgeUtilsSSEOptTest);

TEST_P(WedgeUtilsSSEOptTest, RandomValues) {
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      r1[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
      d[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
      m[i] = rng_(MAX_MASK_VALUE + 1);
    }

    const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);

    const uint64_t ref_res = params_.ref_func(r1, d, m, N);
    uint64_t tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res = params_.tst_func(r1, d, m, N));

    ASSERT_EQ(ref_res, tst_res);
  }
}

TEST_P(WedgeUtilsSSEOptTest, ExtremeValues) {
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    if (rng_(2)) {
      for (int i = 0; i < MAX_SB_SQUARE; ++i) r1[i] = kInt13Max;
    } else {
      for (int i = 0; i < MAX_SB_SQUARE; ++i) r1[i] = -kInt13Max;
    }

    if (rng_(2)) {
      for (int i = 0; i < MAX_SB_SQUARE; ++i) d[i] = kInt13Max;
    } else {
      for (int i = 0; i < MAX_SB_SQUARE; ++i) d[i] = -kInt13Max;
    }

    for (int i = 0; i < MAX_SB_SQUARE; ++i) m[i] = MAX_MASK_VALUE;

    const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);

    const uint64_t ref_res = params_.ref_func(r1, d, m, N);
    uint64_t tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res = params_.tst_func(r1, d, m, N));

    ASSERT_EQ(ref_res, tst_res);
  }
}

TEST_P(WedgeUtilsSSEOptTest, DISABLED_Speed) {
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);
  constexpr int kNumIters = 100000000;

  for (int i = 0; i < MAX_SB_SQUARE; ++i) {
    r1[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
    d[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
    m[i] = rng_(MAX_MASK_VALUE + 1);
  }

  const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);

  avm_usec_timer ref_timer, test_timer;
  avm_usec_timer_start(&ref_timer);
  for (int iter = 0; iter < kNumIters; ++iter) {
    params_.ref_func(r1, d, m, N);
  }
  avm_usec_timer_mark(&ref_timer);
  const int elapsed_time_c =
      static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

  avm_usec_timer_start(&test_timer);
  for (int iter = 0; iter < kNumIters; ++iter) {
    params_.tst_func(r1, d, m, N);
  }
  avm_usec_timer_mark(&test_timer);
  const int elapsed_time_simd =
      static_cast<int>(avm_usec_timer_elapsed(&test_timer));
  printf(
      "c_time=%d \t simd_time=%d \t "
      "Scaling=%lf \n",
      elapsed_time_c, elapsed_time_simd,
      (static_cast<double>(elapsed_time_c) / elapsed_time_simd));
}

//////////////////////////////////////////////////////////////////////////////
// av2_wedge_sign_from_residuals
//////////////////////////////////////////////////////////////////////////////

typedef int8_t (*FSign)(const int16_t *ds, const uint8_t *m, int N,
                        int64_t limit);
typedef libavm_test::FuncParam<FSign> TestFuncsFSign;

class WedgeUtilsSignOptTest : public FunctionEquivalenceTest<FSign> {
 protected:
  static const int kIterations = 10000;
  static const int kMaxSize = 8196;  // Size limited by SIMD implementation.
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WedgeUtilsSignOptTest);

TEST_P(WedgeUtilsSignOptTest, RandomValues) {
  DECLARE_ALIGNED(32, int16_t, r0[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, ds[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      r0[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
      r1[i] = rng_(2 * kInt13Max + 1) - kInt13Max;
      m[i] = rng_(MAX_MASK_VALUE + 1);
    }

    const int maxN = AVMMIN(kMaxSize, MAX_SB_SQUARE);
    const int N = 64 * (rng_(maxN / 64 - 1) + 1);

    int64_t limit;
    limit = (int64_t)avm_sum_squares_i16(r0, N);
    limit -= (int64_t)avm_sum_squares_i16(r1, N);
    limit *= (1 << WEDGE_WEIGHT_BITS) / 2;

    for (int i = 0; i < N; i++)
      ds[i] = clamp(r0[i] * r0[i] - r1[i] * r1[i], INT16_MIN, INT16_MAX);

    const int ref_res = params_.ref_func(ds, m, N, limit);
    int tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res = params_.tst_func(ds, m, N, limit));

    ASSERT_EQ(ref_res, tst_res);
  }
}

TEST_P(WedgeUtilsSignOptTest, ExtremeValues) {
  DECLARE_ALIGNED(32, int16_t, r0[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, ds[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, uint8_t, m[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    switch (rng_(4)) {
      case 0:
        for (int i = 0; i < MAX_SB_SQUARE; ++i) {
          r0[i] = 0;
          r1[i] = kInt13Max;
        }
        break;
      case 1:
        for (int i = 0; i < MAX_SB_SQUARE; ++i) {
          r0[i] = kInt13Max;
          r1[i] = 0;
        }
        break;
      case 2:
        for (int i = 0; i < MAX_SB_SQUARE; ++i) {
          r0[i] = 0;
          r1[i] = -kInt13Max;
        }
        break;
      default:
        for (int i = 0; i < MAX_SB_SQUARE; ++i) {
          r0[i] = -kInt13Max;
          r1[i] = 0;
        }
        break;
    }

    for (int i = 0; i < MAX_SB_SQUARE; ++i) m[i] = MAX_MASK_VALUE;

    const int maxN = AVMMIN(kMaxSize, MAX_SB_SQUARE);
    const int N = 64 * (rng_(maxN / 64 - 1) + 1);

    int64_t limit;
    limit = (int64_t)avm_sum_squares_i16(r0, N);
    limit -= (int64_t)avm_sum_squares_i16(r1, N);
    limit *= (1 << WEDGE_WEIGHT_BITS) / 2;

    for (int i = 0; i < N; i++)
      ds[i] = clamp(r0[i] * r0[i] - r1[i] * r1[i], INT16_MIN, INT16_MAX);

    const int ref_res = params_.ref_func(ds, m, N, limit);
    int tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res = params_.tst_func(ds, m, N, limit));

    ASSERT_EQ(ref_res, tst_res);
  }
}

//////////////////////////////////////////////////////////////////////////////
// av2_wedge_compute_delta_squares
//////////////////////////////////////////////////////////////////////////////

typedef void (*FDS)(int16_t *d, const int16_t *a, const int16_t *b, int N);
typedef libavm_test::FuncParam<FDS> TestFuncsFDS;

class WedgeUtilsDeltaSquaresOptTest : public FunctionEquivalenceTest<FDS> {
 protected:
  static const int kIterations = 10000;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WedgeUtilsDeltaSquaresOptTest);

TEST_P(WedgeUtilsDeltaSquaresOptTest, RandomValues) {
  DECLARE_ALIGNED(32, int16_t, a[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, b[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d_ref[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d_tst[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      a[i] = rng_.Rand16();
      b[i] = rng_(2 * INT16_MAX + 1) - INT16_MAX;
    }

    const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);

    memset(&d_ref, INT16_MAX, sizeof(d_ref));
    memset(&d_tst, INT16_MAX, sizeof(d_tst));

    params_.ref_func(d_ref, a, b, N);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(d_tst, a, b, N));

    for (int i = 0; i < MAX_SB_SQUARE; ++i) ASSERT_EQ(d_ref[i], d_tst[i]);
  }
}

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, WedgeUtilsSSEOptTest,
    ::testing::Values(TestFuncsFSSE(av2_wedge_sse_from_residuals_c,
                                    av2_wedge_sse_from_residuals_sse2)));

INSTANTIATE_TEST_SUITE_P(
    SSE2, WedgeUtilsSignOptTest,
    ::testing::Values(TestFuncsFSign(av2_wedge_sign_from_residuals_c,
                                     av2_wedge_sign_from_residuals_sse2)));

INSTANTIATE_TEST_SUITE_P(
    SSE2, WedgeUtilsDeltaSquaresOptTest,
    ::testing::Values(TestFuncsFDS(av2_wedge_compute_delta_squares_c,
                                   av2_wedge_compute_delta_squares_sse2)));
#endif  // HAVE_SSE2

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, WedgeUtilsSSEOptTest,
    ::testing::Values(TestFuncsFSSE(av2_wedge_sse_from_residuals_c,
                                    av2_wedge_sse_from_residuals_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, WedgeUtilsSignOptTest,
    ::testing::Values(TestFuncsFSign(av2_wedge_sign_from_residuals_sse2,
                                     av2_wedge_sign_from_residuals_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, WedgeUtilsDeltaSquaresOptTest,
    ::testing::Values(TestFuncsFDS(av2_wedge_compute_delta_squares_sse2,
                                   av2_wedge_compute_delta_squares_avx2)));

typedef int64_t (*FInitDS)(int16_t *ds, const int16_t *r1, const int16_t *d,
                           int N, int bd);

class WedgeUtilsInitDeltaSquaresTest
    : public testing::TestWithParam<libavm_test::FuncParam<FInitDS>> {
 protected:
  ACMRandom rng_{ ACMRandom::DeterministicSeed() };
};

TEST_P(WedgeUtilsInitDeltaSquaresTest, RandomAndExtremeValues) {
  DECLARE_ALIGNED(32, int16_t, r1[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, d[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, ds_ref[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int16_t, ds_tst[MAX_SB_SQUARE]);
  const int bds[3] = { 8, 10, 12 };
  const auto params = GetParam();
  for (int bd : bds) {
    const int max_res = (1 << bd) - 1;
    for (int iter = 0; iter < 500; ++iter) {
      for (int i = 0; i < MAX_SB_SQUARE; ++i) {
        int r0v, r1v;
        if (iter == 0) {
          r0v = max_res;
          r1v = -max_res;
        } else if (iter == 1) {
          r0v = -max_res;
          r1v = max_res;
        } else {
          r0v = rng_(2 * max_res + 1) - max_res;
          r1v = rng_(2 * max_res + 1) - max_res;
        }
        r1[i] = static_cast<int16_t>(r1v);
        d[i] = static_cast<int16_t>(r0v - r1v);
      }
      const int N = 64 * (rng_(MAX_SB_SQUARE / 64) + 1);
      const int64_t sum_ref = params.ref_func(ds_ref, r1, d, N, bd);
      int64_t sum_tst;
      ASM_REGISTER_STATE_CHECK(sum_tst = params.tst_func(ds_tst, r1, d, N, bd));
      ASSERT_EQ(sum_ref, sum_tst) << "bd=" << bd << " N=" << N;
      for (int i = 0; i < N; ++i) ASSERT_EQ(ds_ref[i], ds_tst[i]);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(AVX2, WedgeUtilsInitDeltaSquaresTest,
                         ::testing::Values(libavm_test::FuncParam<FInitDS>(
                             av2_wedge_init_delta_squares_c,
                             av2_wedge_init_delta_squares_avx2)));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, WedgeUtilsSSEOptTest,
    ::testing::Values(TestFuncsFSSE(av2_wedge_sse_from_residuals_c,
                                    av2_wedge_sse_from_residuals_neon)));
#endif  // HAVE_NEON

}  // namespace
