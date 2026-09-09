/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.
 * If the Alliance for Open Media Patent License 1.0 was not distributed with
 * this source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/av2_rtcd.h"

#include "test/acm_random.h"
#include "av2/common/av2_txfm.h"
#include "av2/common/enums.h"
#include "avm_ports/avm_timer.h"

using libavm_test::ACMRandom;

namespace {
static const int kBitDepths[] = { 8, 10, 12 };
static const int kNumBitDepths = 3;

typedef void (*InvSTxfmFunc)(tran_low_t *src, tran_low_t *dst,
                             const PREDICTION_MODE mode, const uint8_t stx_idx,
                             const int size, const int bd);
class AV2InvSecTxfmTest : public ::testing::TestWithParam<InvSTxfmFunc> {
 public:
  AV2InvSecTxfmTest() : inv_stxfm_func_(GetParam()) {}
  void AV2InvSecTxfmMatchTest() {
    av2_init_stxfm_kernels();
    for (int bdi = 0; bdi < kNumBitDepths; ++bdi) {
      const int bd = kBitDepths[bdi];
      for (int set_id = 0; set_id < IST_SET_SIZE; ++set_id) {
        for (uint8_t stx = 0; stx < STX_TYPES - 1; ++stx) {
          for (int sb_size = 0; sb_size < 4; ++sb_size) {
            DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_HEIGHT]) = { 0 };
            DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_WIDTH]) = { 0 };
            DECLARE_ALIGNED(32, tran_low_t, ref_output[IST_8x8_WIDTH]) = { 0 };
            ACMRandom rnd(ACMRandom::DeterministicSeed());
            const int coeff_range = ((1 << (bd + 7)) - 1);
            for (int cnt = 0; cnt < 3; ++cnt) {
              memset(output, 0, sizeof(output));
              memset(ref_output, 0, sizeof(ref_output));
              if (cnt == 0) {
                for (int r = 0; r < IST_8x8_HEIGHT; r++) {
                  input[r] = coeff_range;
                }
              } else if (cnt == 1) {
                for (int r = 0; r < IST_8x8_HEIGHT; r++) {
                  input[r] = -coeff_range;
                }
              } else {
                for (int r = 0; r < IST_8x8_HEIGHT; r++) {
                  input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
                }
              }
              inv_stxfm_c(input, ref_output, set_id, stx, sb_size, bd);
              inv_stxfm_func_(input, output, set_id, stx, sb_size, bd);
              int check_rows = (sb_size == 0) ? IST_4x4_WIDTH : IST_8x8_WIDTH;
              for (int r = 0; r < check_rows; ++r) {
                ASSERT_EQ(ref_output[r], output[r])
                    << "[" << r << "] bd:" << bd << " cnt:" << cnt
                    << " ref_output: " << ref_output[r]
                    << " mod_output: " << output[r];
              }
            }
          }
        }
      }
    }
  }

  void AV2InvSecTxfmSpeedTest() {
    const int bd = 10;
    av2_init_stxfm_kernels();
    for (int sb_size = 0; sb_size < 4; ++sb_size) {
      DECLARE_ALIGNED(32, tran_low_t, input[IST_8x8_HEIGHT]) = { 0 };
      DECLARE_ALIGNED(32, tran_low_t, output[IST_8x8_WIDTH]) = { 0 };
      ACMRandom rnd(ACMRandom::DeterministicSeed());
      const int coeff_range = (1 << (bd + 7)) - 1;
      for (int r = 0; r < IST_8x8_HEIGHT; ++r) {
        input[r] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
      }
      avm_usec_timer ref_timer, test_timer;
      const int knum_loops = 100000000;
      avm_usec_timer_start(&ref_timer);
      for (int i = 0; i < knum_loops; ++i) {
        inv_stxfm_c(input, output, 0, 0, sb_size, bd);
      }
      avm_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int i = 0; i < knum_loops; ++i) {
        inv_stxfm_func_(input, output, 0, 0, sb_size, bd);
      }
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf("sb_size[%d] \t c_time=%d \t simd_time=%d \t gain=%f \n", sb_size,
             elapsed_time_c, elapsed_time_simd,
             ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }
  InvSTxfmFunc inv_stxfm_func_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2InvSecTxfmTest);

TEST_P(AV2InvSecTxfmTest, match) { AV2InvSecTxfmMatchTest(); }
TEST_P(AV2InvSecTxfmTest, DISABLED_Speed) { AV2InvSecTxfmSpeedTest(); }

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(SSE4_1, AV2InvSecTxfmTest,
                         ::testing::Values(inv_stxfm_sse4_1));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2InvSecTxfmTest,
                         ::testing::Values(inv_stxfm_avx2));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, AV2InvSecTxfmTest,
                         ::testing::Values(inv_stxfm_neon));
#endif  // HAVE_NEON

}  // namespace
