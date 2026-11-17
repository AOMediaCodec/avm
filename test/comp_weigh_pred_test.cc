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

#include "test/comp_weigh_pred_test.h"

using libavm_test::ACMRandom;
using libavm_test::AV2CWP::AV2HighbdCwpTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2HighbdCwpTest);
using libavm_test::AV2CWP::AV2HighbdCwpUpsampledTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2HighbdCwpUpsampledTest);
using std::make_tuple;
using std::tuple;

namespace {

TEST_P(AV2HighbdCwpTest, DISABLED_Speed) { RunSpeedTest(GET_PARAM(1)); }

TEST_P(AV2HighbdCwpTest, CheckOutput) { RunCheckOutput(GET_PARAM(1)); }

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, AV2HighbdCwpTest,
                         libavm_test::AV2CWP::BuildParams(avm_highbd_cwp_sse2,
                                                          1));
#endif

TEST_P(AV2HighbdCwpUpsampledTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(1));
}

TEST_P(AV2HighbdCwpUpsampledTest, CheckOutput) { RunCheckOutput(GET_PARAM(1)); }

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, AV2HighbdCwpUpsampledTest,
    libavm_test::AV2CWP::BuildParams(avm_highbd_cwp_upsampled_sse2));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, AV2HighbdCwpUpsampledTest,
    libavm_test::AV2CWP::BuildParams(avm_highbd_cwp_upsampled_neon));
#endif

}  // namespace
