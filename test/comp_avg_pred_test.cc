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

#include "test/comp_avg_pred_test.h"

using libavm_test::ACMRandom;
using libavm_test::AV2DISTWTDCOMPAVG::AV2HighBDDISTWTDCOMPAVGTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2HighBDDISTWTDCOMPAVGTest);
using libavm_test::AV2DISTWTDCOMPAVG::AV2HighBDDISTWTDCOMPAVGUPSAMPLEDTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    AV2HighBDDISTWTDCOMPAVGUPSAMPLEDTest);
using std::make_tuple;
using std::tuple;

namespace {

TEST_P(AV2HighBDDISTWTDCOMPAVGTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(1));
}

TEST_P(AV2HighBDDISTWTDCOMPAVGTest, CheckOutput) {
  RunCheckOutput(GET_PARAM(1));
}

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, AV2HighBDDISTWTDCOMPAVGTest,
                         libavm_test::AV2DISTWTDCOMPAVG::BuildParams(
                             avm_highbd_dist_wtd_comp_avg_pred_sse2, 1));
#endif

TEST_P(AV2HighBDDISTWTDCOMPAVGUPSAMPLEDTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(1));
}

TEST_P(AV2HighBDDISTWTDCOMPAVGUPSAMPLEDTest, CheckOutput) {
  RunCheckOutput(GET_PARAM(1));
}

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(SSE2, AV2HighBDDISTWTDCOMPAVGUPSAMPLEDTest,
                         libavm_test::AV2DISTWTDCOMPAVG::BuildParams(
                             avm_highbd_dist_wtd_comp_avg_upsampled_pred_sse2));
#endif

}  // namespace
