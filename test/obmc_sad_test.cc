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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/function_equivalence_test.h"
#include "test/register_state_check.h"

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"

#define MAX_SB_SQUARE (MAX_SB_SIZE * MAX_SB_SIZE)

using libaom_test::FunctionEquivalenceTest;

namespace {

static const int kIterations = 1000;
static const int kMaskMax = 64;

typedef unsigned int (*ObmcSadF)(const uint16_t *pre, int pre_stride,
                                 const int32_t *wsrc, const int32_t *mask);
typedef libaom_test::FuncParam<ObmcSadF> TestFuncs;

////////////////////////////////////////////////////////////////////////////////
// High bit-depth
////////////////////////////////////////////////////////////////////////////////

class ObmcSadHBDTest : public FunctionEquivalenceTest<ObmcSadF> {};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ObmcSadHBDTest);

TEST_P(ObmcSadHBDTest, RandomValues) {
  DECLARE_ALIGNED(32, uint16_t, pre[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int32_t, wsrc[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int32_t, mask[MAX_SB_SQUARE]);

  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    const int pre_stride = rng_(MAX_SB_SIZE + 1);

    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      pre[i] = rng_(1 << 12);
      wsrc[i] = rng_(1 << 12) * rng_(kMaskMax * kMaskMax + 1);
      mask[i] = rng_(kMaskMax * kMaskMax + 1);
    }

    const unsigned int ref_res = params_.ref_func(pre, pre_stride, wsrc, mask);
    unsigned int tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res =
                                 params_.tst_func(pre, pre_stride, wsrc, mask));

    ASSERT_EQ(ref_res, tst_res);
  }
}

TEST_P(ObmcSadHBDTest, ExtremeValues) {
  DECLARE_ALIGNED(32, uint16_t, pre[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int32_t, wsrc[MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, int32_t, mask[MAX_SB_SQUARE]);

  for (int iter = 0; iter < MAX_SB_SIZE && !HasFatalFailure(); ++iter) {
    const int pre_stride = iter;

    for (int i = 0; i < MAX_SB_SQUARE; ++i) {
      pre[i] = (1 << 12) - 1;
      wsrc[i] = ((1 << 12) - 1) * kMaskMax * kMaskMax;
      mask[i] = kMaskMax * kMaskMax;
    }

    const unsigned int ref_res = params_.ref_func(pre, pre_stride, wsrc, mask);
    unsigned int tst_res;
    ASM_REGISTER_STATE_CHECK(tst_res =
                                 params_.tst_func(pre, pre_stride, wsrc, mask));

    ASSERT_EQ(ref_res, tst_res);
  }
}

#if HAVE_SSE4_1
ObmcSadHBDTest::ParamType sse4_functions_hbd[] = {
  TestFuncs(aom_highbd_obmc_sad128x128_c, aom_highbd_obmc_sad128x128_sse4_1),
  TestFuncs(aom_highbd_obmc_sad128x64_c, aom_highbd_obmc_sad128x64_sse4_1),
  TestFuncs(aom_highbd_obmc_sad64x128_c, aom_highbd_obmc_sad64x128_sse4_1),
  TestFuncs(aom_highbd_obmc_sad64x64_c, aom_highbd_obmc_sad64x64_sse4_1),
  TestFuncs(aom_highbd_obmc_sad64x32_c, aom_highbd_obmc_sad64x32_sse4_1),
  TestFuncs(aom_highbd_obmc_sad32x64_c, aom_highbd_obmc_sad32x64_sse4_1),
  TestFuncs(aom_highbd_obmc_sad32x32_c, aom_highbd_obmc_sad32x32_sse4_1),
  TestFuncs(aom_highbd_obmc_sad32x16_c, aom_highbd_obmc_sad32x16_sse4_1),
  TestFuncs(aom_highbd_obmc_sad16x32_c, aom_highbd_obmc_sad16x32_sse4_1),
  TestFuncs(aom_highbd_obmc_sad16x16_c, aom_highbd_obmc_sad16x16_sse4_1),
  TestFuncs(aom_highbd_obmc_sad16x8_c, aom_highbd_obmc_sad16x8_sse4_1),
  TestFuncs(aom_highbd_obmc_sad8x16_c, aom_highbd_obmc_sad8x16_sse4_1),
  TestFuncs(aom_highbd_obmc_sad8x8_c, aom_highbd_obmc_sad8x8_sse4_1),
  TestFuncs(aom_highbd_obmc_sad8x4_c, aom_highbd_obmc_sad8x4_sse4_1),
  TestFuncs(aom_highbd_obmc_sad4x8_c, aom_highbd_obmc_sad4x8_sse4_1),
  TestFuncs(aom_highbd_obmc_sad4x4_c, aom_highbd_obmc_sad4x4_sse4_1),

  TestFuncs(aom_highbd_obmc_sad64x16_c, aom_highbd_obmc_sad64x16_sse4_1),
  TestFuncs(aom_highbd_obmc_sad16x64_c, aom_highbd_obmc_sad16x64_sse4_1),
  TestFuncs(aom_highbd_obmc_sad32x8_c, aom_highbd_obmc_sad32x8_sse4_1),
  TestFuncs(aom_highbd_obmc_sad8x32_c, aom_highbd_obmc_sad8x32_sse4_1),
  TestFuncs(aom_highbd_obmc_sad16x4_c, aom_highbd_obmc_sad16x4_sse4_1),
  TestFuncs(aom_highbd_obmc_sad4x16_c, aom_highbd_obmc_sad4x16_sse4_1),

#if CONFIG_EXT_RECUR_PARTITIONS
  TestFuncs(aom_highbd_obmc_sad64x8_c, aom_highbd_obmc_sad64x8_sse4_1),
  TestFuncs(aom_highbd_obmc_sad8x64_c, aom_highbd_obmc_sad8x64_sse4_1),
  TestFuncs(aom_highbd_obmc_sad32x4_c, aom_highbd_obmc_sad32x4_sse4_1),
  TestFuncs(aom_highbd_obmc_sad4x32_c, aom_highbd_obmc_sad4x32_sse4_1),
  TestFuncs(aom_highbd_obmc_sad64x4_c, aom_highbd_obmc_sad64x4_sse4_1),
  TestFuncs(aom_highbd_obmc_sad4x64_c, aom_highbd_obmc_sad4x64_sse4_1),
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

INSTANTIATE_TEST_SUITE_P(SSE4_1, ObmcSadHBDTest,
                         ::testing::ValuesIn(sse4_functions_hbd));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
ObmcSadHBDTest::ParamType avx2_functions_hbd[] = {
#if CONFIG_EXT_RECUR_PARTITIONS
  TestFuncs(aom_highbd_obmc_sad256x256_c, aom_highbd_obmc_sad256x256_avx2),
  TestFuncs(aom_highbd_obmc_sad256x128_c, aom_highbd_obmc_sad256x128_avx2),
  TestFuncs(aom_highbd_obmc_sad128x256_c, aom_highbd_obmc_sad128x256_avx2),
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  TestFuncs(aom_highbd_obmc_sad128x128_c, aom_highbd_obmc_sad128x128_avx2),
  TestFuncs(aom_highbd_obmc_sad128x64_c, aom_highbd_obmc_sad128x64_avx2),
  TestFuncs(aom_highbd_obmc_sad64x128_c, aom_highbd_obmc_sad64x128_avx2),
  TestFuncs(aom_highbd_obmc_sad64x64_c, aom_highbd_obmc_sad64x64_avx2),
  TestFuncs(aom_highbd_obmc_sad64x32_c, aom_highbd_obmc_sad64x32_avx2),
  TestFuncs(aom_highbd_obmc_sad32x64_c, aom_highbd_obmc_sad32x64_avx2),
  TestFuncs(aom_highbd_obmc_sad32x32_c, aom_highbd_obmc_sad32x32_avx2),
  TestFuncs(aom_highbd_obmc_sad32x16_c, aom_highbd_obmc_sad32x16_avx2),
  TestFuncs(aom_highbd_obmc_sad16x32_c, aom_highbd_obmc_sad16x32_avx2),
  TestFuncs(aom_highbd_obmc_sad16x16_c, aom_highbd_obmc_sad16x16_avx2),
  TestFuncs(aom_highbd_obmc_sad16x8_c, aom_highbd_obmc_sad16x8_avx2),
  TestFuncs(aom_highbd_obmc_sad8x16_c, aom_highbd_obmc_sad8x16_avx2),
  TestFuncs(aom_highbd_obmc_sad8x8_c, aom_highbd_obmc_sad8x8_avx2),
  TestFuncs(aom_highbd_obmc_sad8x4_c, aom_highbd_obmc_sad8x4_avx2),
  TestFuncs(aom_highbd_obmc_sad4x8_c, aom_highbd_obmc_sad4x8_avx2),
  TestFuncs(aom_highbd_obmc_sad4x4_c, aom_highbd_obmc_sad4x4_avx2),

  TestFuncs(aom_highbd_obmc_sad64x16_c, aom_highbd_obmc_sad64x16_avx2),
  TestFuncs(aom_highbd_obmc_sad16x64_c, aom_highbd_obmc_sad16x64_avx2),
  TestFuncs(aom_highbd_obmc_sad32x8_c, aom_highbd_obmc_sad32x8_avx2),
  TestFuncs(aom_highbd_obmc_sad8x32_c, aom_highbd_obmc_sad8x32_avx2),
  TestFuncs(aom_highbd_obmc_sad16x4_c, aom_highbd_obmc_sad16x4_avx2),
  TestFuncs(aom_highbd_obmc_sad4x16_c, aom_highbd_obmc_sad4x16_avx2),

#if CONFIG_EXT_RECUR_PARTITIONS
  TestFuncs(aom_highbd_obmc_sad64x8_c, aom_highbd_obmc_sad64x8_avx2),
  TestFuncs(aom_highbd_obmc_sad8x64_c, aom_highbd_obmc_sad8x64_avx2),
  TestFuncs(aom_highbd_obmc_sad32x4_c, aom_highbd_obmc_sad32x4_avx2),
  TestFuncs(aom_highbd_obmc_sad4x32_c, aom_highbd_obmc_sad4x32_avx2),
  TestFuncs(aom_highbd_obmc_sad64x4_c, aom_highbd_obmc_sad64x4_avx2),
  TestFuncs(aom_highbd_obmc_sad4x64_c, aom_highbd_obmc_sad4x64_avx2),
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

INSTANTIATE_TEST_SUITE_P(AVX2, ObmcSadHBDTest,
                         ::testing::ValuesIn(avx2_functions_hbd));
#endif  // HAVE_AVX2
}  // namespace
