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
#include "av2/common/common_data.h"
#include "av2/common/idct.h"
#include "avm_dsp/avm_dsp_common.h"

using libavm_test::ACMRandom;
using libavm_test::BdParamName;
using libavm_test::kTestBd;

namespace {

typedef void (*InvTxfm2dFunc)(const tran_low_t *input, uint16_t *dest,
                              int stride, const TxfmParam *txfm_param);

// (kernel under test, bit depth)
typedef std::tuple<InvTxfm2dFunc, int> InvTxfm2dParam;

class AV2InvTxfm2dTest : public ::testing::TestWithParam<InvTxfm2dParam> {
 public:
  AV2InvTxfm2dTest()
      : test_func_(std::get<0>(GetParam())), bd_(std::get<1>(GetParam())) {}

 protected:
  // Coefficient patterns exercised by the match test.
  static constexpr int kNumMatchReps = 7;

  // Mirrors the assert()s in the 1D dispatcher: dimensions are clamped to 32,
  // and a length-32 axis only accepts DCT2 or IDT.
  static bool IsValidCombo(TX_SIZE tx_size, PRIMARY_TX_TYPE primary_tx_type) {
    const int w = AVMMIN(32, tx_size_wide[tx_size]);
    const int h = AVMMIN(32, tx_size_high[tx_size]);
    const int row_1d = g_hor_tx_type[primary_tx_type];
    const int col_1d = g_ver_tx_type[primary_tx_type];
    if (w == 32 && row_1d != DCT2 && row_1d != IDT) return false;
    if (h == 32 && col_1d != DCT2 && col_1d != IDT) return false;
    return true;
  }

  void AV2InvTxfm2dMatchTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int cur_bd = bd_;
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      const int cols = tx_size_wide[tx_size];
      const int rows = tx_size_high[tx_size];
      for (int tx_type = 0; tx_type < PRIMARY_TX_TYPES; ++tx_type) {
        if (!IsValidCombo((TX_SIZE)tx_size, (PRIMARY_TX_TYPE)tx_type)) continue;
        for (int use_ddt = 0; use_ddt < 2; ++use_ddt) {
          for (int rep = 0; rep < kNumMatchReps; ++rep) {
            DECLARE_ALIGNED(64, tran_low_t, input[MAX_TX_SQUARE]);
            DECLARE_ALIGNED(64, uint16_t, dst_ref[MAX_TX_SQUARE]);
            DECLARE_ALIGNED(64, uint16_t, dst_tst[MAX_TX_SQUARE]);
            const int stride = cols;
            const int coeff_range = (1 << (cur_bd + 7)) - 1;
            const int clamp_hi = 1 << (cur_bd + 7);
            int nz = rows * cols;
            for (int i = 0; i < MAX_TX_SQUARE; ++i) input[i] = 0;
            switch (rep) {
              case 0:
              case 1:  // Full-energy dense block.
                for (int i = 0; i < nz; ++i) {
                  input[i] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
                }
                break;
              case 2:
              case 3: {  // Realistic sparse, low amplitude.
                const int range = 1 + (coeff_range >> 4);
                nz = 1 + rnd(rows * cols - 1);
                for (int i = 0; i < nz; ++i) {
                  input[i] = rnd(2) ? rnd(range) : -rnd(range);
                }
                break;
              }
              case 4: {
                // The clamp endpoints themselves. rnd(n) only returns
                // [0, n-1], so the reps above never generate -clamp_hi or
                // clamp_hi - 1, which is where an off-by-one clamp shows up
                // once values are packed into int16.
                const int edge[4] = { -clamp_hi, -(clamp_hi - 1), clamp_hi - 2,
                                      clamp_hi - 1 };
                for (int i = 0; i < nz; ++i) input[i] = edge[rnd(4)];
                break;
              }
              case 5:
                // Beyond the clamp range, so the driver's clamp actually
                // fires. The C reference clamps identically, so the outputs
                // must still match exactly.
                for (int i = 0; i < nz; ++i) {
                  input[i] = rnd(2) ? rnd(4 * clamp_hi) : -rnd(4 * clamp_hi);
                }
                break;
              case 6:
                // A single full-amplitude DC coefficient: the extreme input
                // for the shift-based DC stage, with every other tap zero.
                nz = 1;
                input[0] = rnd(2) ? coeff_range : -coeff_range;
                break;
              default: FAIL() << "unhandled rep " << rep; break;
            }
            const int pix_max = (1 << cur_bd) - 1;
            for (int i = 0; i < rows * cols; ++i) {
              const uint16_t p = static_cast<uint16_t>(rnd(pix_max + 1));
              dst_ref[i] = p;
              dst_tst[i] = p;
            }
            TxfmParam param;
            memset(&param, 0, sizeof(param));
            param.primary_tx_type = (PRIMARY_TX_TYPE)tx_type;
            param.tx_size = (TX_SIZE)tx_size;
            param.bd = cur_bd;
            param.lossless = 0;
            param.use_ddt = use_ddt;
            param.eob = nz;

            inv_txfm_c(input, dst_ref, stride, &param);
            test_func_(input, dst_tst, stride, &param);

            for (int i = 0; i < rows * cols; ++i) {
              ASSERT_EQ(dst_ref[i], dst_tst[i])
                  << "tx_size=" << tx_size << " tx_type=" << tx_type
                  << " bd=" << cur_bd << " use_ddt=" << use_ddt
                  << " rep=" << rep << " pos=" << i << " ref=" << dst_ref[i]
                  << " simd=" << dst_tst[i];
            }
          }
        }
      }
    }
  }

  // One config per 1D kernel family, so that optimizing any single kernel
  // shows up somewhere in the speed output. DDTX/FDDT replace DST7/DCT8 only
  // when use_ddt is set, hence the repeated tx_types.
  struct SpeedConfig {
    PRIMARY_TX_TYPE tx_type;
    int use_ddt;
    const char *name;
  };

  void AV2InvTxfm2dSpeedTest() {
    static const SpeedConfig kConfigs[] = {
      { DCT_DCT, 0, "DCT2" },   { IDTX, 0, "IDTX" },
      { ADST_ADST, 0, "ADST" }, { FLIPADST_FLIPADST, 0, "FDST" },
      { ADST_ADST, 1, "DDTX" }, { FLIPADST_FLIPADST, 1, "FDDT" },
    };
    for (const SpeedConfig &cfg : kConfigs) {
      AV2InvTxfm2dSpeedTestOne(cfg);
    }
  }

  void AV2InvTxfm2dSpeedTestOne(const SpeedConfig &cfg) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const PRIMARY_TX_TYPE primary_tx_type = cfg.tx_type;
    const int cur_bd = bd_;
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      if (!IsValidCombo((TX_SIZE)tx_size, primary_tx_type)) continue;
      const int cols = tx_size_wide[tx_size];
      const int rows = tx_size_high[tx_size];
      DECLARE_ALIGNED(64, tran_low_t, input[MAX_TX_SQUARE]);
      DECLARE_ALIGNED(64, uint16_t, dst_ref[MAX_TX_SQUARE]);
      DECLARE_ALIGNED(64, uint16_t, dst_tst[MAX_TX_SQUARE]);
      const int stride = cols;
      const int coeff_range = (1 << (cur_bd + 7)) - 1;
      for (int i = 0; i < MAX_TX_SQUARE; ++i) {
        input[i] = rnd(2) ? rnd(coeff_range) : -rnd(coeff_range);
      }
      const int pix_max = (1 << cur_bd) - 1;
      for (int i = 0; i < rows * cols; ++i) {
        dst_ref[i] = dst_tst[i] = static_cast<uint16_t>(rnd(pix_max + 1));
      }
      TxfmParam param;
      memset(&param, 0, sizeof(param));
      param.primary_tx_type = primary_tx_type;
      param.tx_size = (TX_SIZE)tx_size;
      param.bd = cur_bd;
      param.use_ddt = cfg.use_ddt;
      param.eob = rows * cols;

      avm_usec_timer ref_timer, test_timer;
      const int knum_loops = 100000;
      avm_usec_timer_start(&ref_timer);
      for (int i = 0; i < knum_loops; ++i)
        inv_txfm_c(input, dst_ref, stride, &param);
      avm_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int i = 0; i < knum_loops; ++i)
        test_func_(input, dst_tst, stride, &param);
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf(
          "%s \t bd[%2d] \t tx_size[%2d] (%2dx%2d) \t c_time=%d \t "
          "simd_time=%d \t gain=%f\n",
          cfg.name, cur_bd, tx_size, cols, rows, elapsed_time_c,
          elapsed_time_simd, ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }

  InvTxfm2dFunc test_func_;
  int bd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2InvTxfm2dTest);

TEST_P(AV2InvTxfm2dTest, match) { AV2InvTxfm2dMatchTest(); }
TEST_P(AV2InvTxfm2dTest, DISABLED_Speed) { AV2InvTxfm2dSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2InvTxfm2dTest,
                         ::testing::Combine(::testing::Values(inv_txfm_avx2),
                                            ::testing::ValuesIn(kTestBd)),
                         BdParamName<AV2InvTxfm2dTest::ParamType>);
#endif  // HAVE_AVX2

#if HAVE_AVX512
INSTANTIATE_TEST_SUITE_P(AVX512, AV2InvTxfm2dTest,
                         ::testing::Combine(::testing::Values(inv_txfm_avx512),
                                            ::testing::ValuesIn(kTestBd)),
                         BdParamName<AV2InvTxfm2dTest::ParamType>);
#endif  // HAVE_AVX512
}  // namespace
