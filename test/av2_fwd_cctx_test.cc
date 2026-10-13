/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <stdio.h>
#include <string.h>
#include <tuple>

#include "config/av2_rtcd.h"

#include "test/acm_random.h"
#include "test/util.h"
#include "test/av2_txfm_test.h"
#include "av2/common/av2_txfm.h"
#include "av2/common/blockd.h"
#include "avm_ports/mem.h"

using libavm_test::ACMRandom;
using libavm_test::BdParamName;
using libavm_test::kTestBd;

namespace {

typedef void (*FwdCctxFunc)(tran_low_t *coeff_c1, tran_low_t *coeff_c2,
                            TX_SIZE tx_size, CctxType cctx_type, const int bd);

// (kernel under test, bit depth)
typedef std::tuple<FwdCctxFunc, int> FwdCctxParam;

const int kNumPatterns = 5;

class AV2FwdCctxTest : public ::testing::TestWithParam<FwdCctxParam> {
 public:
  AV2FwdCctxTest()
      : test_func_(std::get<0>(GetParam())), bd_(std::get<1>(GetParam())) {}

 protected:
  // 0-1: full-range random, 2: all +max, 3: all -max, 4: c1 +max, c2 -max.
  // Forward coefficients fit in bd + 8 bits, which keeps the 32-bit SIMD
  // products exact.
  void FillCoeffs(ACMRandom *rnd, int pattern, tran_low_t *c1,
                  tran_low_t *c2) const {
    const int max_val = (1 << (bd_ + 8)) - 1;
    for (int i = 0; i < MAX_TX_SQUARE; ++i) {
      switch (pattern) {
        case 2: c1[i] = c2[i] = max_val; break;
        case 3: c1[i] = c2[i] = -max_val; break;
        case 4:
          c1[i] = max_val;
          c2[i] = -max_val;
          break;
        default:
          c1[i] = (*rnd)(2 * max_val + 1) - max_val;
          c2[i] = (*rnd)(2 * max_val + 1) - max_val;
          break;
      }
    }
  }

  void AV2FwdCctxMatchTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(32, tran_low_t, c1_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c1_tst[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2_tst[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      for (int cctx_type = 0; cctx_type < CCTX_TYPES; ++cctx_type) {
        for (int pattern = 0; pattern < kNumPatterns; ++pattern) {
          FillCoeffs(&rnd, pattern, c1_ref, c2_ref);
          memcpy(c1_tst, c1_ref, sizeof(c1_ref));
          memcpy(c2_tst, c2_ref, sizeof(c2_ref));

          av2_fwd_cross_chroma_tx_block_c(c1_ref, c2_ref, (TX_SIZE)tx_size,
                                          (CctxType)cctx_type, bd_);
          test_func_(c1_tst, c2_tst, (TX_SIZE)tx_size, (CctxType)cctx_type,
                     bd_);

          // The whole buffer, so writes past max_eob are caught too.
          for (int i = 0; i < MAX_TX_SQUARE; ++i) {
            ASSERT_EQ(c1_ref[i], c1_tst[i])
                << "c1 tx_size=" << tx_size << " cctx_type=" << cctx_type
                << " bd=" << bd_ << " pattern=" << pattern << " pos=" << i;
            ASSERT_EQ(c2_ref[i], c2_tst[i])
                << "c2 tx_size=" << tx_size << " cctx_type=" << cctx_type
                << " bd=" << bd_ << " pattern=" << pattern << " pos=" << i;
          }
        }
      }
    }
  }

  void AV2FwdCctxSpeedTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(32, tran_low_t, c1[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      FillCoeffs(&rnd, 0, c1, c2);
      // Same number of coefficients per size.
      const int num_loops = (1 << 22) / av2_get_max_eob((TX_SIZE)tx_size);

      avm_usec_timer ref_timer, test_timer;
      avm_usec_timer_start(&ref_timer);
      for (int i = 0; i < num_loops; ++i) {
        av2_fwd_cross_chroma_tx_block_c(c1, c2, (TX_SIZE)tx_size, CCTX_45, bd_);
      }
      avm_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int i = 0; i < num_loops; ++i) {
        test_func_(c1, c2, (TX_SIZE)tx_size, CCTX_45, bd_);
      }
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf(
          "bd[%2d] \t tx_size[%2d] (%2dx%2d) \t c_time=%d \t simd_time=%d \t "
          "gain=%f\n",
          bd_, tx_size, tx_size_wide[tx_size], tx_size_high[tx_size],
          elapsed_time_c, elapsed_time_simd,
          ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }

  FwdCctxFunc test_func_;
  int bd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2FwdCctxTest);

TEST_P(AV2FwdCctxTest, match) { AV2FwdCctxMatchTest(); }
TEST_P(AV2FwdCctxTest, DISABLED_Speed) { AV2FwdCctxSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2FwdCctxTest,
    ::testing::Combine(::testing::Values(av2_fwd_cross_chroma_tx_block_avx2),
                       ::testing::ValuesIn(kTestBd)),
    BdParamName<AV2FwdCctxTest::ParamType>);
#endif  // HAVE_AVX2
}  // namespace
