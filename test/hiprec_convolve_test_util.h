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

#ifndef AVM_TEST_HIPREC_CONVOLVE_TEST_UTIL_H_
#define AVM_TEST_HIPREC_CONVOLVE_TEST_UTIL_H_

#include <tuple>

#include "config/av2_rtcd.h"

#include "test/acm_random.h"
#include "test/util.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm_ports/avm_timer.h"
#include "av2/common/convolve.h"
#include "av2/common/mv.h"

namespace libavm_test {

namespace AV2HighbdHiprecConvolve {
typedef void (*highbd_hiprec_convolve_func)(
    const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,
    ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4,
    const int16_t *filter_y, int y_step_q4, int w, int h,
    const WienerConvolveParams *conv_params, int bps);

typedef std::tuple<int, int, int, int, highbd_hiprec_convolve_func>
    HighbdHiprecConvolveParam;

::testing::internal::ParamGenerator<HighbdHiprecConvolveParam> BuildParams(
    highbd_hiprec_convolve_func filter);

class AV2HighbdHiprecConvolveTest
    : public ::testing::TestWithParam<HighbdHiprecConvolveParam> {
 public:
  virtual ~AV2HighbdHiprecConvolveTest();
  virtual void SetUp();

  virtual void TearDown();

 protected:
  void RunCheckOutput(highbd_hiprec_convolve_func test_impl);
  void RunSpeedTest(highbd_hiprec_convolve_func test_impl);

  libavm_test::ACMRandom rnd_;
};

}  // namespace AV2HighbdHiprecConvolve
}  // namespace libavm_test

#endif  // AVM_TEST_HIPREC_CONVOLVE_TEST_UTIL_H_
