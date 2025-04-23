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
using libaom_test::ACMRandom;
#if CONFIG_EXT_WARP_FILTER
using libaom_test::AV1ExtHighbdWarpFilter::AV1ExtHighbdWarpFilterTest;
#endif  // CONFIG_EXT_WARP_FILTER
using libaom_test::AV1HighbdWarpFilter::AV1HighbdWarpFilterTest;
#if CONFIG_AFFINE_REFINEMENT
using libaom_test::AV1HighbdWarpBilinearFilter::AV1HighbdWarpBilinearFilterTest;
#endif  // CONFIG_AFFINE_REFINEMENT
using std::make_tuple;
using std::tuple;

namespace {

#if HAVE_SSE4_1
TEST_P(AV1HighbdWarpFilterTest, CheckOutput) {
  RunCheckOutput(std::get<4>(GET_PARAM(0)));
}
TEST_P(AV1HighbdWarpFilterTest, DISABLED_Speed) {
  RunSpeedTest(std::get<4>(GET_PARAM(0)));
}

INSTANTIATE_TEST_SUITE_P(SSE4_1, AV1HighbdWarpFilterTest,
                         libaom_test::AV1HighbdWarpFilter::BuildParams(
                             av1_highbd_warp_affine_sse4_1));
#endif  // HAVE_SSE4_1
#if CONFIG_EXT_WARP_FILTER
TEST_P(AV1ExtHighbdWarpFilterTest, CheckOutput) {
  RunCheckOutput(::testing::get<4>(GET_PARAM(0)));
}
TEST_P(AV1ExtHighbdWarpFilterTest, DISABLED_Speed) {
  RunSpeedTest(::testing::get<4>(GET_PARAM(0)));
}
#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, AV1ExtHighbdWarpFilterTest,
                         libaom_test::AV1ExtHighbdWarpFilter::BuildParams(
                             av1_ext_highbd_warp_affine_sse4_1));
#endif  // HAVE_SSE4_1
#if HAVE_AVX2
#if !CONFIG_WARP_BD_BOX
INSTANTIATE_TEST_SUITE_P(AVX2, AV1ExtHighbdWarpFilterTest,
                         libaom_test::AV1ExtHighbdWarpFilter::BuildParams(
                             av1_ext_highbd_warp_affine_avx2));
#endif  // HAVE_AVX2
#endif  // !COFNIG_WARP_BD
#endif  // CONFIG_EXT_WARP_FILTER

#if HAVE_AVX2
#if CONFIG_OPFL_MEMBW_REDUCTION
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV1HighbdWarpFilterTest,
    libaom_test::AV1HighbdWarpFilter::BuildParams(av1_highbd_warp_affine_avx2));
#endif  // CONFIG_OPFL_MEMBW_REDUCTION
#endif  // HAVE_AVX2

}  // namespace

#if CONFIG_AFFINE_REFINEMENT
#if AFFINE_FAST_WARP_METHOD == 3
TEST_P(AV1HighbdWarpBilinearFilterTest, DISABLED_Speed) {
  RunSpeedTest(std::get<4>(GET_PARAM(0)));
}

TEST_P(AV1HighbdWarpBilinearFilterTest, CheckOutput) {
  RunCheckOutput(std::get<4>(GET_PARAM(0)));
}
TEST_P(AV1HighbdWarpBilinearFilterTest, ExtremeValues) {
  RunTest_ExtremeValues(std::get<4>(GET_PARAM(0)));
}
#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1HighbdWarpBilinearFilterTest,
                         libaom_test::AV1HighbdWarpBilinearFilter::BuildParams(
                             av1_warp_plane_bilinear_avx2));
#endif  // HAVE_AVX2
#endif  // AFFINE_FAST_WARP_METHOD == 3
#endif  // CONFIG_AFFINE_REFINEMENT
