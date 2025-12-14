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

#ifndef AVM_TEST_WARP_FILTER_TEST_UTIL_H_
#define AVM_TEST_WARP_FILTER_TEST_UTIL_H_

#include <tuple>

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/acm_random.h"
#include "test/util.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"

#include "av2/common/mv.h"
#include "av2/common/common_data.h"
#include "av2/common/reconinter.h"

namespace libavm_test {

void generate_warped_model(libavm_test::ACMRandom *rnd, int32_t *mat,
                           int16_t *alpha, int16_t *beta, int16_t *gamma,
                           int16_t *delta, int is_alpha_zero, int is_beta_zero,
                           int is_gamma_zero, int is_delta_zero);
void generate_ref_area_limits(libavm_test::ACMRandom *rnd,
                              ReferenceArea *ref_area, int w, int h, int out_w,
                              int out_h, int p_row, int p_col, int *mat,
                              int use_ref_area_pad);

namespace AV2WarpFilter {

typedef void (*warp_affine_func)(const int32_t *mat, const uint8_t *ref,
                                 int width, int height, int stride,
                                 uint8_t *pred, int p_col, int p_row,
                                 int p_width, int p_height, int p_stride,
                                 int subsampling_x, int subsampling_y,
                                 ConvolveParams *conv_params, int16_t alpha,
                                 int16_t beta, int16_t gamma, int16_t delta);

typedef std::tuple<int, int, int, warp_affine_func> WarpTestParam;
typedef std::tuple<WarpTestParam, int, int, int, int> WarpTestParams;

::testing::internal::ParamGenerator<WarpTestParams> BuildParams(
    warp_affine_func filter);

class AV2WarpFilterTest : public ::testing::TestWithParam<WarpTestParams> {
 public:
  virtual ~AV2WarpFilterTest();
  virtual void SetUp();

  virtual void TearDown();

 protected:
  void RunCheckOutput(warp_affine_func test_impl);
  void RunSpeedTest(warp_affine_func test_impl);

  libavm_test::ACMRandom rnd_;
};

}  // namespace AV2WarpFilter

namespace AV2HighbdWarpFilter {
typedef void (*highbd_warp_affine_func)(const int32_t *mat, const uint16_t *ref,
                                        int width, int height, int stride,
                                        uint16_t *pred, int p_col, int p_row,
                                        int p_width, int p_height, int p_stride,
                                        int subsampling_x, int subsampling_y,
                                        int bd, ConvolveParams *conv_params,
                                        int16_t alpha, int16_t beta,
                                        int16_t gamma, int16_t delta);

typedef std::tuple<int, int, int, int, highbd_warp_affine_func>
    HighbdWarpTestParam;
typedef std::tuple<HighbdWarpTestParam, int, int, int, int>
    HighbdWarpTestParams;

::testing::internal::ParamGenerator<HighbdWarpTestParams> BuildParams(
    highbd_warp_affine_func filter);

class AV2HighbdWarpFilterTest
    : public ::testing::TestWithParam<HighbdWarpTestParams> {
 public:
  virtual ~AV2HighbdWarpFilterTest();
  virtual void SetUp();

  virtual void TearDown();

 protected:
  void RunCheckOutput(highbd_warp_affine_func test_impl);
  void RunSpeedTest(highbd_warp_affine_func test_impl);

  libavm_test::ACMRandom rnd_;
};

}  // namespace AV2HighbdWarpFilter

namespace AV2ExtHighbdWarpFilter {
typedef void (*ext_highbd_warp_affine_func)(
    const int32_t *mat, const uint16_t *ref, int width, int height, int stride,
    uint16_t *pred, int p_col, int p_row, int p_width, int p_height,
    int p_stride, int subsampling_x, int subsampling_y, int bd,
    ConvolveParams *conv_params, int use_warp_bd_box, PadBlock *warp_bd_box);

typedef ::testing::tuple<int, int, int, int, ext_highbd_warp_affine_func>
    ExtHighbdWarpTestParam;
typedef ::testing::tuple<ExtHighbdWarpTestParam, int, int, int, int>
    ExtHighbdWarpTestParams;

::testing::internal::ParamGenerator<ExtHighbdWarpTestParams> BuildParams(
    ext_highbd_warp_affine_func filter);

class AV2ExtHighbdWarpFilterTest
    : public ::testing::TestWithParam<ExtHighbdWarpTestParams> {
 public:
  virtual ~AV2ExtHighbdWarpFilterTest();
  virtual void SetUp();

  virtual void TearDown();

 protected:
  void RunCheckOutput(ext_highbd_warp_affine_func test_impl);
  void RunSpeedTest(ext_highbd_warp_affine_func test_impl);

  libavm_test::ACMRandom rnd_;
};

}  // namespace AV2ExtHighbdWarpFilter
}  // namespace libavm_test

#endif  // AVM_TEST_WARP_FILTER_TEST_UTIL_H_
