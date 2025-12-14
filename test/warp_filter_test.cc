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
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/warp_filter_test_util.h"
using libavm_test::ACMRandom;
using libavm_test::AV2ExtHighbdWarpFilter::AV2ExtHighbdWarpFilterTest;
using libavm_test::AV2HighbdWarpFilter::AV2HighbdWarpFilterTest;
using std::make_tuple;
using std::tuple;

namespace {

#if HAVE_SSE4_1
TEST_P(AV2HighbdWarpFilterTest, CheckOutput) {
  RunCheckOutput(std::get<4>(GET_PARAM(0)));
}
TEST_P(AV2HighbdWarpFilterTest, DISABLED_Speed) {
  RunSpeedTest(std::get<4>(GET_PARAM(0)));
}

INSTANTIATE_TEST_SUITE_P(SSE4_1, AV2HighbdWarpFilterTest,
                         libavm_test::AV2HighbdWarpFilter::BuildParams(
                             av2_highbd_warp_affine_sse4_1));
#endif  // HAVE_SSE4_1
TEST_P(AV2ExtHighbdWarpFilterTest, CheckOutput) {
  RunCheckOutput(::testing::get<4>(GET_PARAM(0)));
}
TEST_P(AV2ExtHighbdWarpFilterTest, DISABLED_Speed) {
  RunSpeedTest(::testing::get<4>(GET_PARAM(0)));
}
#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, AV2ExtHighbdWarpFilterTest,
                         libavm_test::AV2ExtHighbdWarpFilter::BuildParams(
                             av2_ext_highbd_warp_affine_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2HighbdWarpFilterTest,
    libavm_test::AV2HighbdWarpFilter::BuildParams(av2_highbd_warp_affine_avx2));
#endif  // HAVE_AVX2

}  // namespace
