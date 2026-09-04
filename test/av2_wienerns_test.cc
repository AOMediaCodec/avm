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
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/restoration.h"

using libavm_test::ACMRandom;

namespace {

typedef void (*AccumulateWienernsCorrFunc)(double *A_base, double *b_base,
                                           const int16_t *buf, int16_t y,
                                           int num_feat);

typedef std::tuple<AccumulateWienernsCorrFunc> AccumulateWienernsCorrParam;

class WienernsCorrTest
    : public ::testing::TestWithParam<AccumulateWienernsCorrParam> {
 public:
  virtual void SetUp() {
    target_func_ = GET_PARAM(0);
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

  void RunRandomTest();
  void RunExtremeTest();
  void RunSpeedTest(int run_times);

 private:
  AccumulateWienernsCorrFunc target_func_;
  ACMRandom rnd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WienernsCorrTest);

static const int kTestFeatCounts[] = {
  1, 2, 3, 4, 5, 8, 9, 13, 16, 20, 25, 32
};
static const int kNumFeatTests =
    sizeof(kTestFeatCounts) / sizeof(kTestFeatCounts[0]);

void WienernsCorrTest::RunRandomTest() {
  double A_ref[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX];
  double A_tst[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX];
  double b_ref[WIENERNS_TAPS_MAX];
  double b_tst[WIENERNS_TAPS_MAX];
  int16_t buf[WIENERNS_TAPS_MAX];

  for (int f_idx = 0; f_idx < kNumFeatTests; ++f_idx) {
    const int num_feat = kTestFeatCounts[f_idx];
    if (num_feat > WIENERNS_TAPS_MAX) continue;

    for (int iter = 0; iter < 500 && !HasFatalFailure(); ++iter) {
      for (int i = 0; i < num_feat * num_feat; ++i) {
        const double init_val = static_cast<double>(rnd_.Rand31() % 100000);
        A_ref[i] = init_val;
        A_tst[i] = init_val;
      }
      for (int i = 0; i < num_feat; ++i) {
        const double init_val = static_cast<double>(rnd_.Rand31() % 100000);
        b_ref[i] = init_val;
        b_tst[i] = init_val;
      }

      const int num_samples = 1 + (rnd_.Rand8() % 32);
      for (int s = 0; s < num_samples; ++s) {
        for (int k = 0; k < num_feat; ++k) {
          buf[k] = static_cast<int16_t>((rnd_.Rand16() % 4096) - 2048);
        }
        const int16_t y = static_cast<int16_t>((rnd_.Rand16() % 4096) - 2048);

        av2_accumulate_wienerns_correlation_c(A_ref, b_ref, buf, y, num_feat);
        target_func_(A_tst, b_tst, buf, y, num_feat);
      }

      for (int k = 0; k < num_feat; ++k) {
        ASSERT_DOUBLE_EQ(b_ref[k], b_tst[k])
            << "b mismatch at index " << k << " for num_feat=" << num_feat;
      }

      for (int i = 0; i < num_feat; ++i) {
        for (int j = 0; j <= i; ++j) {
          ASSERT_DOUBLE_EQ(A_ref[i * num_feat + j], A_tst[i * num_feat + j])
              << "A mismatch at (" << i << ", " << j
              << ") for num_feat=" << num_feat;
        }
      }
    }
  }
}

void WienernsCorrTest::RunExtremeTest() {
  double A_ref[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX];
  double A_tst[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX];
  double b_ref[WIENERNS_TAPS_MAX];
  double b_tst[WIENERNS_TAPS_MAX];
  int16_t buf[WIENERNS_TAPS_MAX];

  const int16_t extreme_vals[] = { -32768, -4095, -2048, -1,   0,
                                   1,      2047,  4095,  32767 };
  const int num_extreme = sizeof(extreme_vals) / sizeof(extreme_vals[0]);

  for (int f_idx = 0; f_idx < kNumFeatTests; ++f_idx) {
    const int num_feat = kTestFeatCounts[f_idx];
    if (num_feat > WIENERNS_TAPS_MAX) continue;

    for (int e1 = 0; e1 < num_extreme; ++e1) {
      for (int e2 = 0; e2 < num_extreme; ++e2) {
        memset(A_ref, 0, sizeof(A_ref));
        memset(A_tst, 0, sizeof(A_tst));
        memset(b_ref, 0, sizeof(b_ref));
        memset(b_tst, 0, sizeof(b_tst));

        for (int k = 0; k < num_feat; ++k) {
          buf[k] = (k % 2 == 0) ? extreme_vals[e1] : extreme_vals[e2];
        }
        const int16_t y = extreme_vals[e1];

        av2_accumulate_wienerns_correlation_c(A_ref, b_ref, buf, y, num_feat);
        target_func_(A_tst, b_tst, buf, y, num_feat);

        for (int k = 0; k < num_feat; ++k) {
          ASSERT_DOUBLE_EQ(b_ref[k], b_tst[k])
              << "Extreme b mismatch at index " << k
              << " for num_feat=" << num_feat;
        }

        for (int i = 0; i < num_feat; ++i) {
          for (int j = 0; j <= i; ++j) {
            ASSERT_DOUBLE_EQ(A_ref[i * num_feat + j], A_tst[i * num_feat + j])
                << "Extreme A mismatch at (" << i << ", " << j
                << ") for num_feat=" << num_feat;
          }
        }
      }
    }
  }
}

void WienernsCorrTest::RunSpeedTest(int run_times) {
  double A_ref[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX] = { 0.0 };
  double A_tst[WIENERNS_TAPS_MAX * WIENERNS_TAPS_MAX] = { 0.0 };
  double b_ref[WIENERNS_TAPS_MAX] = { 0.0 };
  double b_tst[WIENERNS_TAPS_MAX] = { 0.0 };
  int16_t buf[WIENERNS_TAPS_MAX];

  for (int k = 0; k < WIENERNS_TAPS_MAX; ++k) {
    buf[k] = static_cast<int16_t>((rnd_.Rand16() % 4096) - 2048);
  }
  const int16_t y = static_cast<int16_t>((rnd_.Rand16() % 4096) - 2048);

  for (int f_idx = 0; f_idx < kNumFeatTests; ++f_idx) {
    const int num_feat = kTestFeatCounts[f_idx];
    if (num_feat > WIENERNS_TAPS_MAX) continue;

    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int i = 0; i < run_times; ++i) {
      av2_accumulate_wienerns_correlation_c(A_ref, b_ref, buf, y, num_feat);
    }
    avm_usec_timer_mark(&timer);
    const double time_c = static_cast<double>(avm_usec_timer_elapsed(&timer));

    avm_usec_timer_start(&timer);
    for (int i = 0; i < run_times; ++i) {
      target_func_(A_tst, b_tst, buf, y, num_feat);
    }
    avm_usec_timer_mark(&timer);
    const double time_simd =
        static_cast<double>(avm_usec_timer_elapsed(&timer));

    printf("num_feat %2d: C %7.2fus / SIMD %7.2fus (speedup: %3.2fx)\n",
           num_feat, time_c, time_simd, time_c / time_simd);
  }
}

TEST_P(WienernsCorrTest, RandomValues) { RunRandomTest(); }
TEST_P(WienernsCorrTest, ExtremeValues) { RunExtremeTest(); }
TEST_P(WienernsCorrTest, DISABLED_Speed) { RunSpeedTest(200000); }

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, WienernsCorrTest,
    ::testing::Values(av2_accumulate_wienerns_correlation_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, WienernsCorrTest,
    ::testing::Values(av2_accumulate_wienerns_correlation_avx2));
#endif  // HAVE_AVX2

}  // namespace
