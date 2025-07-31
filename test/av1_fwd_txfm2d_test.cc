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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <tuple>
#include <vector>

#include "config/av1_rtcd.h"

#include "test/acm_random.h"
#include "test/util.h"
#include "test/av1_txfm_test.h"
#include "test/function_equivalence_test.h"
#include "av1/common/av1_txfm.h"
#include "av1/encoder/hybrid_fwd_txfm.h"

using libaom_test::ACMRandom;
using libaom_test::bd;
using libaom_test::compute_avg_abs_error;
using libaom_test::input_base;
using libaom_test::TYPE_TXFM;

using std::vector;

namespace {
///////////////////////////////////////////////////////////////
//       unit-test for 'fwd_stxfm'                           //
///////////////////////////////////////////////////////////////

typedef void (*FwdSTxfmFunc)(tran_low_t *src, tran_low_t *dst,
                             const PREDICTION_MODE mode, const uint8_t stx_idx,
                             const int size, const int bd);
class AV1FwdSecTxfmTest : public ::testing::TestWithParam<FwdSTxfmFunc> {
 public:
  AV1FwdSecTxfmTest() : fwd_stxfm_func_(GetParam()) {}
  void AV1FwdSecTxfmMatchTest() {
    av1_init_stxfm_kernels();
    for (int set_id = 0; set_id < IST_DIR_SIZE; ++set_id) {
      for (uint8_t stx = 0; stx < STX_TYPES - 1; ++stx) {
#if CONFIG_E124_IST_REDUCE_METHOD4
        for (int sb_size = 0; sb_size < 3; ++sb_size) {
#else
        for (int sb_size_idx = 0; sb_size_idx < 2; ++sb_size_idx) {
          int sb_size = (sb_size_idx == 0) ? 4 : 8;
#endif
          DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_WIDTH]) = { 0 };
          DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_HEIGHT]) = { 0 };
          DECLARE_ALIGNED(32, tran_low_t, ref_output[IST_8x8_HEIGHT]) = { 0 };
          ACMRandom rnd(ACMRandom::DeterministicSeed());
          const int coeff_range = (1 << (bd + 7)) - 1;
          for (int cnt = 0; cnt < 3; ++cnt) {
            if (cnt == 0) {
              for (int r = 0; r < IST_8x8_WIDTH; ++r) {
                input[r] = coeff_range;
              }
            } else if (cnt == 1) {
              for (int r = 0; r < IST_8x8_WIDTH; ++r) {
                input[r] = -coeff_range;
              }
            } else {
              for (int r = 0; r < IST_8x8_WIDTH; ++r) {
                input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
              }
            }
            fwd_stxfm_c(input, ref_output, set_id, stx, sb_size, bd);
            fwd_stxfm_func_(input, output, set_id, stx, sb_size, bd);
#if CONFIG_E124_IST_REDUCE_METHOD4
            int check_rows = (sb_size == 0)   ? IST_4x4_HEIGHT
                             : (sb_size == 1) ? IST_8x8_HEIGHT_RED
                                              : IST_8x8_HEIGHT;
#else
            int check_rows = (sb_size == 4) ? IST_4x4_HEIGHT : IST_8x8_HEIGHT;
#endif
            for (int r = 0; r < check_rows; ++r) {
              ASSERT_EQ(ref_output[r], output[r])
                  << "[" << r << "] cnt:" << cnt
                  << " ref_output: " << ref_output[r]
                  << " mod_output: " << output[r];
            }
          }
        }
      }
    }
  }

  void AV1FwdSecTxfmSpeedTest() {
    av1_init_stxfm_kernels();
#if CONFIG_E124_IST_REDUCE_METHOD4
    for (int sb_size = 0; sb_size < 3; ++sb_size) {
#else
    for (int sb_size_idx = 0; sb_size_idx < 2; ++sb_size_idx) {
      int sb_size = (sb_size_idx == 0) ? 4 : 8;
#endif
      DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_WIDTH]) = { 0 };
      DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_HEIGHT]) = { 0 };
      ACMRandom rnd(ACMRandom::DeterministicSeed());
      const int coeff_range = (1 << (bd + 7)) - 1;
      for (int r = 0; r < IST_8x8_WIDTH; ++r) {
        input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
      }
      aom_usec_timer ref_timer, test_timer;
      const int knum_loops = 100000000;
      aom_usec_timer_start(&ref_timer);
      for (int i = 0; i < knum_loops; ++i) {
        fwd_stxfm_c(input, output, 0, 0, sb_size, bd);
      }
      aom_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(aom_usec_timer_elapsed(&ref_timer));

      aom_usec_timer_start(&test_timer);
      for (int i = 0; i < knum_loops; ++i) {
        fwd_stxfm_func_(input, output, 0, 0, sb_size, bd);
      }
      aom_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(aom_usec_timer_elapsed(&test_timer));

      printf(
          "sb_size[%d] \t c_time=%d \t simd_time=%d \t "
          "gain=%f \n",
          sb_size, elapsed_time_c, elapsed_time_simd,
          ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }
  FwdSTxfmFunc fwd_stxfm_func_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV1FwdSecTxfmTest);

TEST_P(AV1FwdSecTxfmTest, match) { AV1FwdSecTxfmMatchTest(); }
TEST_P(AV1FwdSecTxfmTest, DISABLED_Speed) { AV1FwdSecTxfmSpeedTest(); }

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, AV1FwdSecTxfmTest,
                         ::testing::Values(fwd_stxfm_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1FwdSecTxfmTest,
                         ::testing::Values(fwd_stxfm_avx2));
#endif  // HAVE_AVX2

///////////////////////////////////////////////////////////////
//       unit-test for 'inv_stxfm'                           //
///////////////////////////////////////////////////////////////

typedef void (*InvSTxfmFunc)(tran_low_t *src, tran_low_t *dst,
                             const PREDICTION_MODE mode, const uint8_t stx_idx,
                             const int size, const int bd);
class AV1InvSecTxfmTest : public ::testing::TestWithParam<InvSTxfmFunc> {
 public:
  AV1InvSecTxfmTest() : inv_stxfm_func_(GetParam()) {}
  void AV1InvSecTxfmMatchTest() {
    av1_init_stxfm_kernels();
    for (int set_id = 0; set_id < IST_DIR_SIZE; ++set_id) {
      for (uint8_t stx = 0; stx < STX_TYPES - 1; ++stx) {
#if CONFIG_E124_IST_REDUCE_METHOD4
        for (int sb_size = 0; sb_size < 3; ++sb_size) {
#else
        for (int sb_size_idx = 0; sb_size_idx < 2; ++sb_size_idx) {
          int sb_size = (sb_size_idx == 0) ? 4 : 8;
#endif
          DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_HEIGHT]) = { 0 };
          DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_WIDTH]) = { 0 };
          DECLARE_ALIGNED(32, tran_low_t, ref_output[IST_8x8_WIDTH]) = { 0 };
          ACMRandom rnd(ACMRandom::DeterministicSeed());
          const int coeff_range = ((1 << (bd + 7)) - 1);
          for (int r = 0; r < IST_8x8_HEIGHT; r++) {
            input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
          }
          inv_stxfm_c(input, ref_output, set_id, stx, sb_size, bd);
          inv_stxfm_func_(input, output, set_id, stx, sb_size, bd);
#if CONFIG_E124_IST_REDUCE_METHOD4
          int check_rows = (sb_size == 0) ? IST_4x4_WIDTH : IST_8x8_WIDTH;
#else
          int check_rows = (sb_size == 4) ? IST_4x4_WIDTH : IST_8x8_WIDTH;
#endif
          for (int r = 0; r < check_rows; ++r) {
            ASSERT_EQ(ref_output[r], output[r])
                << "[" << r << "] " << " ref_output: " << ref_output[r]
                << " mod_output: " << output[r];
          }
        }
      }
    }
  }

  void AV1InvSecTxfmSpeedTest() {
    av1_init_stxfm_kernels();
#if CONFIG_E124_IST_REDUCE_METHOD4
    for (int sb_size = 0; sb_size < 3; ++sb_size) {
#else
    for (int sb_size_idx = 0; sb_size_idx < 2; ++sb_size_idx) {
      int sb_size = (sb_size_idx == 0) ? 4 : 8;
#endif
      DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_HEIGHT]) = { 0 };
      DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_WIDTH]) = { 0 };
      ACMRandom rnd(ACMRandom::DeterministicSeed());
      const int coeff_range = ((1 << (bd + 7)) - 1);
      for (int r = 0; r < IST_8x8_HEIGHT; ++r) {
        input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
      }
      aom_usec_timer ref_timer, test_timer;
      const int knum_loops = 100000000;
      aom_usec_timer_start(&ref_timer);
      for (int i = 0; i < knum_loops; ++i) {
        fwd_stxfm_c(input, output, 0, 0, sb_size, bd);
      }
      aom_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(aom_usec_timer_elapsed(&ref_timer));

      aom_usec_timer_start(&test_timer);
      for (int i = 0; i < knum_loops; ++i) {
        inv_stxfm_func_(input, output, 0, 0, sb_size, bd);
      }
      aom_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(aom_usec_timer_elapsed(&test_timer));

      printf(
          "sb_size[%d] \t c_time=%d \t simd_time=%d \t "
          "gain=%f \n",
          sb_size, elapsed_time_c, elapsed_time_simd,
          ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }
  InvSTxfmFunc inv_stxfm_func_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV1InvSecTxfmTest);

TEST_P(AV1InvSecTxfmTest, match) { AV1InvSecTxfmMatchTest(); }
TEST_P(AV1InvSecTxfmTest, DISABLED_Speed) { AV1InvSecTxfmSpeedTest(); }

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, AV1InvSecTxfmTest,
                         ::testing::Values(inv_stxfm_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV1InvSecTxfmTest,
                         ::testing::Values(inv_stxfm_avx2));
#endif  // HAVE_AVX2
}  // namespace
