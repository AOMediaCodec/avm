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
#include "test/hiprec_convolve_test_util.h"

using libavm_test::ACMRandom;
using libavm_test::AV2HighbdHiprecConvolve::AV2HighbdHiprecConvolveTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2HighbdHiprecConvolveTest);
using std::make_tuple;
using std::tuple;

namespace {

#if HAVE_SSSE3 || HAVE_AVX2
TEST_P(AV2HighbdHiprecConvolveTest, CheckOutput) {
  RunCheckOutput(GET_PARAM(4));
}
TEST_P(AV2HighbdHiprecConvolveTest, DISABLED_SpeedTest) {
  RunSpeedTest(GET_PARAM(4));
}
#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(SSSE3, AV2HighbdHiprecConvolveTest,
                         libavm_test::AV2HighbdHiprecConvolve::BuildParams(
                             av2_highbd_wiener_convolve_add_src_ssse3));
#endif
#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2HighbdHiprecConvolveTest,
                         libavm_test::AV2HighbdHiprecConvolve::BuildParams(
                             av2_highbd_wiener_convolve_add_src_avx2));
#endif
#endif

}  // namespace
