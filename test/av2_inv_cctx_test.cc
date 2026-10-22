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

#include <cstdio>
#include <cstring>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/blockd.h"
#include "avm_ports/avm_timer.h"
#include "avm_ports/mem.h"
#include "config/av2_rtcd.h"
#include "test/acm_random.h"
#include "test/av2_txfm_test.h"
#include "test/register_state_check.h"

using libavm_test::ACMRandom;
using libavm_test::kTestBd;

namespace {

typedef void (*InvCctxFunc)(tran_low_t *dqcoeff_c1, tran_low_t *dqcoeff_c2,
                            TX_SIZE tx_size, CctxType cctx_type, const int bd);

typedef std::tuple<InvCctxFunc, int> InvCctxParam;

constexpr int kNumIterations = 1000;

class AV2InvCctxTest : public ::testing::TestWithParam<InvCctxParam> {
 public:
  AV2InvCctxTest()
      : test_func_(std::get<0>(GetParam())), bd_(std::get<1>(GetParam())),
        max_val_((1 << (bd_ + 7)) - 1), min_val_(-(1 << (bd_ + 7))),
        rnd_(ACMRandom::DeterministicSeed()) {}

 protected:
  tran_low_t GetRandomCoeff() {
    return min_val_ + static_cast<tran_low_t>(rnd_(max_val_ - min_val_ + 1));
  }

  // Random coefficients in the first num positions, zeros after them.
  void FillCoeffRandom(tran_low_t *c1, tran_low_t *c2, TX_SIZE tx_size) {
    memset(c1, 0, MAX_TX_SQUARE * sizeof(*c1));
    memset(c2, 0, MAX_TX_SQUARE * sizeof(*c2));
    const int n_coeffs = av2_get_max_eob(tx_size);
    const int num = rnd_.Rand16() % n_coeffs;
    for (int i = 0; i < num; ++i) {
      c1[i] = GetRandomCoeff();
      c2[i] = GetRandomCoeff();
    }
  }

  void FillCoeffExtreme(tran_low_t *c1, tran_low_t *c2, TX_SIZE tx_size) {
    memset(c1, 0, MAX_TX_SQUARE * sizeof(*c1));
    memset(c2, 0, MAX_TX_SQUARE * sizeof(*c2));
    const int n_coeffs = av2_get_max_eob(tx_size);
    for (int i = 0; i < n_coeffs; ++i) {
      c1[i] = rnd_(2) ? max_val_ : min_val_;
      c2[i] = rnd_(2) ? max_val_ : min_val_;
    }
  }

  void AV2InvCctxOperationCheck() {
    DECLARE_ALIGNED(32, tran_low_t, c1_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c1_tst[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2_tst[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      for (int cctx_type = 0; cctx_type < CCTX_TYPES; ++cctx_type) {
        for (int iter = 0; iter < kNumIterations; ++iter) {
          if (iter & 1)
            FillCoeffExtreme(c1_ref, c2_ref, (TX_SIZE)tx_size);
          else
            FillCoeffRandom(c1_ref, c2_ref, (TX_SIZE)tx_size);
          memcpy(c1_tst, c1_ref, sizeof(c1_ref));
          memcpy(c2_tst, c2_ref, sizeof(c2_ref));

          av2_inv_cross_chroma_tx_block_c(c1_ref, c2_ref, (TX_SIZE)tx_size,
                                          (CctxType)cctx_type, bd_);
          API_REGISTER_STATE_CHECK(test_func_(c1_tst, c2_tst, (TX_SIZE)tx_size,
                                              (CctxType)cctx_type, bd_));

          // Test the whole buffer, so that writes past max_eob are also caught.
          for (int i = 0; i < MAX_TX_SQUARE; ++i) {
            ASSERT_EQ(c1_ref[i], c1_tst[i])
                << "c1 tx_size=" << tx_size << " cctx_type=" << cctx_type
                << " bd=" << bd_ << " iter=" << iter << " pos=" << i;
            ASSERT_EQ(c2_ref[i], c2_tst[i])
                << "c2 tx_size=" << tx_size << " cctx_type=" << cctx_type
                << " bd=" << bd_ << " iter=" << iter << " pos=" << i;
          }
        }
      }
    }
  }

  void AV2InvCctxSpeedTest() {
    DECLARE_ALIGNED(32, tran_low_t, c1[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(32, tran_low_t, c2[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      const int n_coeffs = av2_get_max_eob((TX_SIZE)tx_size);
      for (int i = 0; i < n_coeffs; ++i) {
        c1[i] = GetRandomCoeff();
        c2[i] = GetRandomCoeff();
      }
      const int num_loops = (1 << 22) / n_coeffs;

      avm_usec_timer ref_timer, test_timer;
      avm_usec_timer_start(&ref_timer);
      for (int i = 0; i < num_loops; ++i) {
        av2_inv_cross_chroma_tx_block_c(c1, c2, (TX_SIZE)tx_size, CCTX_45, bd_);
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

  InvCctxFunc test_func_;
  int bd_;
  tran_low_t max_val_;
  tran_low_t min_val_;
  ACMRandom rnd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2InvCctxTest);

TEST_P(AV2InvCctxTest, OperationCheck) { AV2InvCctxOperationCheck(); }
TEST_P(AV2InvCctxTest, DISABLED_Speed) { AV2InvCctxSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, AV2InvCctxTest,
    ::testing::Combine(::testing::Values(av2_inv_cross_chroma_tx_block_avx2),
                       ::testing::ValuesIn(kTestBd)));
#endif  // HAVE_AVX2
}  // namespace
