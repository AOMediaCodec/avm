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

#ifndef AVM_TEST_COMP_WEIGH_PRED_TEST_H_
#define AVM_TEST_COMP_WEIGH_PRED_TEST_H_

#include <tuple>

#include "config/avm_dsp_rtcd.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/acm_random.h"
#include "test/util.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "av2/common/common_data.h"
#include "avm_ports/avm_timer.h"

namespace libavm_test {
const int kMaxSize = MAX_SB_SIZE + 32;  // padding

namespace AV2CWP {

typedef void (*cwp_func)(uint16_t *comp_pred, const uint16_t *pred, int width,
                         int height, const uint16_t *ref, int ref_stride,
                         const CWP_PARAMS *cwp_param);

typedef void (*cwp_upsampled_func)(MACROBLOCKD *xd,
                                   const struct AV2Common *const cm, int mi_row,
                                   int mi_col, const MV *const mv,
                                   uint8_t *comp_pred, const uint8_t *pred,
                                   int width, int height, int subpel_x_q3,
                                   int subpel_y_q3, const uint8_t *ref,
                                   int ref_stride, const CWP_PARAMS *cwp_param,
                                   int subpel_search, int is_scaled_ref);

typedef std::tuple<cwp_func, BLOCK_SIZE> CwpParam;

typedef std::tuple<cwp_upsampled_func, BLOCK_SIZE> CwpUpsampledParam;

typedef void (*highbd_cwp_upsampled_func)(
    MACROBLOCKD *xd, const struct AV2Common *const cm, int mi_row, int mi_col,
    const MV *const mv, uint16_t *comp_pred8, const uint16_t *pred8, int width,
    int height, int subpel_x_q3, int subpel_y_q3, const uint16_t *ref8,
    int ref_stride, int bd, const CWP_PARAMS *cwp_param, int subpel_search,
    int is_scaled_ref);

typedef std::tuple<int, highbd_cwp_upsampled_func, BLOCK_SIZE>
    HighbdCwpUpsampledParam;

typedef std::tuple<int, cwp_func, BLOCK_SIZE> HighbdCwpParam;

::testing::internal::ParamGenerator<HighbdCwpParam> BuildParams(cwp_func filter,
                                                                int is_hbd) {
  (void)is_hbd;
  return ::testing::Combine(::testing::Range(8, 13, 2),
                            ::testing::Values(filter),
                            ::testing::Range(BLOCK_4X4, BLOCK_SIZES_ALL));
}

::testing::internal::ParamGenerator<HighbdCwpUpsampledParam> BuildParams(
    highbd_cwp_upsampled_func filter) {
  return ::testing::Combine(::testing::Range(8, 13, 2),
                            ::testing::Values(filter),
                            ::testing::Range(BLOCK_4X4, BLOCK_SIZES_ALL));
}

::testing::internal::ParamGenerator<CwpParam> BuildParams(cwp_func filter) {
  return ::testing::Combine(::testing::Values(filter),
                            ::testing::Range(BLOCK_4X4, BLOCK_SIZES_ALL));
}

::testing::internal::ParamGenerator<CwpUpsampledParam> BuildParams(
    cwp_upsampled_func filter) {
  return ::testing::Combine(::testing::Values(filter),
                            ::testing::Range(BLOCK_4X4, BLOCK_SIZES_ALL));
}

class AV2HighbdCwpTest : public ::testing::TestWithParam<HighbdCwpParam> {
 public:
  ~AV2HighbdCwpTest() {}
  void SetUp() { rnd_.Reset(ACMRandom::DeterministicSeed()); }

  void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  void RunCheckOutput(cwp_func test_impl) {
    const int w = kMaxSize, h = kMaxSize;
    const int block_idx = GET_PARAM(2);
    const int bd = GET_PARAM(0);
    uint16_t pred8[kMaxSize * kMaxSize];
    uint16_t ref8[kMaxSize * kMaxSize];
    uint16_t output[kMaxSize * kMaxSize];
    uint16_t output2[kMaxSize * kMaxSize];

    for (int i = 0; i < h; ++i)
      for (int j = 0; j < w; ++j) {
        pred8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
        ref8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
      }
    const int in_w = block_size_wide[block_idx];
    const int in_h = block_size_high[block_idx];

    CWP_PARAMS cwp_params;

    for (int ii = 0; ii < 2; ii++) {
      for (int jj = 0; jj < 4; jj++) {
        cwp_params.fwd_offset = quant_dist_lookup_table[jj][ii];
        cwp_params.bck_offset = quant_dist_lookup_table[jj][1 - ii];

        const int offset_r = 3 + rnd_.PseudoUniform(h - in_h - 7);
        const int offset_c = 3 + rnd_.PseudoUniform(w - in_w - 7);
        avm_highbd_cwp_c(output, pred8 + offset_r * w + offset_c, in_w, in_h,
                         ref8 + offset_r * w + offset_c, in_w, &cwp_params);
        test_impl(output2, pred8 + offset_r * w + offset_c, in_w, in_h,
                  ref8 + offset_r * w + offset_c, in_w, &cwp_params);

        for (int i = 0; i < in_h; ++i) {
          for (int j = 0; j < in_w; ++j) {
            int idx = i * in_w + j;
            ASSERT_EQ(output[idx], output2[idx])
                << "Mismatch at unit tests for AV2HighbdCwpTest\n"
                << in_w << "x" << in_h << " Pixel mismatch at index " << idx
                << " = (" << i << ", " << j << ")";
          }
        }
      }
    }
  }
  void RunSpeedTest(cwp_func test_impl) {
    const int w = kMaxSize, h = kMaxSize;
    const int block_idx = GET_PARAM(2);
    const int bd = GET_PARAM(0);
    uint16_t pred8[kMaxSize * kMaxSize];
    uint16_t ref8[kMaxSize * kMaxSize];
    uint16_t output[kMaxSize * kMaxSize];
    uint16_t output2[kMaxSize * kMaxSize];

    for (int i = 0; i < h; ++i)
      for (int j = 0; j < w; ++j) {
        pred8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
        ref8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
      }
    const int in_w = block_size_wide[block_idx];
    const int in_h = block_size_high[block_idx];

    CWP_PARAMS cwp_params;

    cwp_params.fwd_offset = quant_dist_lookup_table[0][0];
    cwp_params.bck_offset = quant_dist_lookup_table[0][1];

    const int num_loops = 1000000000 / (in_w + in_h);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);

    for (int i = 0; i < num_loops; ++i)
      avm_highbd_cwp_c(output, pred8, in_w, in_h, ref8, in_w, &cwp_params);

    avm_usec_timer_mark(&timer);
    const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));
    printf("highbdcwp c_code %3dx%-3d: %7.2f us\n", in_w, in_h,
           1000.0 * elapsed_time / num_loops);

    avm_usec_timer timer1;
    avm_usec_timer_start(&timer1);

    for (int i = 0; i < num_loops; ++i)
      test_impl(output2, pred8, in_w, in_h, ref8, in_w, &cwp_params);

    avm_usec_timer_mark(&timer1);
    const int elapsed_time1 = static_cast<int>(avm_usec_timer_elapsed(&timer1));
    printf("highbdcwp test_code %3dx%-3d: %7.2f us\n", in_w, in_h,
           1000.0 * elapsed_time1 / num_loops);
  }

  libavm_test::ACMRandom rnd_;
};  // class AV2HighbdCwpTest

class AV2HighbdCwpUpsampledTest
    : public ::testing::TestWithParam<HighbdCwpUpsampledParam> {
 public:
  ~AV2HighbdCwpUpsampledTest() {}
  void SetUp() { rnd_.Reset(ACMRandom::DeterministicSeed()); }
  void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  void RunCheckOutput(highbd_cwp_upsampled_func test_impl) {
    const int w = kMaxSize, h = kMaxSize;
    const int block_idx = GET_PARAM(2);
    const int bd = GET_PARAM(0);
    uint16_t pred8[kMaxSize * kMaxSize];
    uint16_t ref8[kMaxSize * kMaxSize];
    DECLARE_ALIGNED(16, uint16_t, output[kMaxSize * kMaxSize]);
    DECLARE_ALIGNED(16, uint16_t, output2[kMaxSize * kMaxSize]);

    for (int i = 0; i < h; ++i)
      for (int j = 0; j < w; ++j) {
        pred8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
        ref8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
      }
    const int in_w = block_size_wide[block_idx];
    const int in_h = block_size_high[block_idx];

    CWP_PARAMS cwp_params;
    int sub_x_q3, sub_y_q3;
    int subpel_search;
    for (subpel_search = USE_4_TAPS; subpel_search <= USE_8_TAPS;
         ++subpel_search) {
      for (sub_x_q3 = 0; sub_x_q3 < 8; ++sub_x_q3) {
        for (sub_y_q3 = 0; sub_y_q3 < 8; ++sub_y_q3) {
          for (int ii = 0; ii < 2; ii++) {
            for (int jj = 0; jj < 4; jj++) {
              cwp_params.fwd_offset = quant_dist_lookup_table[jj][ii];
              cwp_params.bck_offset = quant_dist_lookup_table[jj][1 - ii];

              const int offset_r = 3 + rnd_.PseudoUniform(h - in_h - 7);
              const int offset_c = 3 + rnd_.PseudoUniform(w - in_w - 7);

              avm_highbd_cwp_upsampled_c(NULL, NULL, 0, 0, NULL, output,
                                         pred8 + offset_r * w + offset_c, in_w,
                                         in_h, sub_x_q3, sub_y_q3,
                                         ref8 + offset_r * w + offset_c, in_w,
                                         bd, &cwp_params, subpel_search, 0);
              test_impl(NULL, NULL, 0, 0, NULL, output2,
                        pred8 + offset_r * w + offset_c, in_w, in_h, sub_x_q3,
                        sub_y_q3, ref8 + offset_r * w + offset_c, in_w, bd,
                        &cwp_params, subpel_search, 0);

              for (int i = 0; i < in_h; ++i) {
                for (int j = 0; j < in_w; ++j) {
                  int idx = i * in_w + j;
                  ASSERT_EQ(output[idx], output2[idx])
                      << "Mismatch at unit tests for "
                         "AV2HighbdCwpUpsampledTest\n"
                      << in_w << "x" << in_h << " Pixel mismatch at index "
                      << idx << " = (" << i << ", " << j
                      << "), sub pixel offset = (" << sub_y_q3 << ", "
                      << sub_x_q3 << ")";
                }
              }
            }
          }
        }
      }
    }
  }
  void RunSpeedTest(highbd_cwp_upsampled_func test_impl) {
    const int w = kMaxSize, h = kMaxSize;
    const int block_idx = GET_PARAM(2);
    const int bd = GET_PARAM(0);
    uint16_t pred8[kMaxSize * kMaxSize];
    uint16_t ref8[kMaxSize * kMaxSize];
    DECLARE_ALIGNED(16, uint16_t, output[kMaxSize * kMaxSize]);
    DECLARE_ALIGNED(16, uint16_t, output2[kMaxSize * kMaxSize]);

    for (int i = 0; i < h; ++i)
      for (int j = 0; j < w; ++j) {
        pred8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
        ref8[i * w + j] = rnd_.Rand16() & ((1 << bd) - 1);
      }
    const int in_w = block_size_wide[block_idx];
    const int in_h = block_size_high[block_idx];

    CWP_PARAMS cwp_params;

    cwp_params.fwd_offset = quant_dist_lookup_table[0][0];
    cwp_params.bck_offset = quant_dist_lookup_table[0][1];
    int sub_x_q3 = 0;
    int sub_y_q3 = 0;
    const int num_loops = 1000000000 / (in_w + in_h);
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    int subpel_search = USE_8_TAPS;  // set to USE_4_TAPS to test 4-tap filter.
    for (int i = 0; i < num_loops; ++i)
      avm_highbd_cwp_upsampled_c(NULL, NULL, 0, 0, NULL, output, pred8, in_w,
                                 in_h, sub_x_q3, sub_y_q3, ref8, in_w, bd,
                                 &cwp_params, subpel_search, 0);

    avm_usec_timer_mark(&timer);
    const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));
    printf("highbdcwpupsampled c_code %3dx%-3d: %7.2f us\n", in_w, in_h,
           1000.0 * elapsed_time / num_loops);

    avm_usec_timer timer1;
    avm_usec_timer_start(&timer1);

    for (int i = 0; i < num_loops; ++i)
      test_impl(NULL, NULL, 0, 0, NULL, output2, pred8, in_w, in_h, sub_x_q3,
                sub_y_q3, ref8, in_w, bd, &cwp_params, subpel_search, 0);

    avm_usec_timer_mark(&timer1);
    const int elapsed_time1 = static_cast<int>(avm_usec_timer_elapsed(&timer1));
    printf("highbdcwpupsampled test_code %3dx%-3d: %7.2f us\n", in_w, in_h,
           1000.0 * elapsed_time1 / num_loops);
  }

  libavm_test::ACMRandom rnd_;
};  // class AV2HighbdCwpUpsampledTest

}  // namespace AV2CWP
}  // namespace libavm_test

#endif  // AVM_TEST_COMP_WEIGH_PRED_TEST_H_
