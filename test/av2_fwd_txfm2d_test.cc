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
#include "av2/common/common_data.h"
#include "avm_dsp/avm_dsp_common.h"

using libavm_test::ACMRandom;
using libavm_test::BdParamName;
using libavm_test::kTestBd;

namespace {

typedef void (*FwdTxfm2dFunc)(const int16_t *resi, tran_low_t *coeff,
                              int diff_stride, TxfmParam *txfm_param);

// (kernel under test, bit depth)
typedef std::tuple<FwdTxfm2dFunc, int> FwdTxfm2dParam;

// The encoder passes the plane block width as diff_stride, so we also test a
// stride wider than the widest transform.
const int kMaxStride = 2 * MAX_TX_SIZE;
const int kResiBufSize = MAX_TX_SIZE * kMaxStride;
const int kNumPatterns = 5;

class AV2FwdTxfm2dTest : public ::testing::TestWithParam<FwdTxfm2dParam> {
 public:
  AV2FwdTxfm2dTest()
      : test_func_(std::get<0>(GetParam())), bd_(std::get<1>(GetParam())) {}

 protected:
  // Mirrors the assert()s in the 1D dispatcher: a length-64 axis only accepts
  // DCT2, and a length-32 axis only accepts DCT2 or IDT.
  static bool IsValidCombo(TX_SIZE tx_size, PRIMARY_TX_TYPE primary_tx_type) {
    const int w = tx_size_wide[tx_size];
    const int h = tx_size_high[tx_size];
    const int row_1d = g_hor_tx_type[primary_tx_type];
    const int col_1d = g_ver_tx_type[primary_tx_type];
    if (w == 64 && row_1d != DCT2) return false;
    if (h == 64 && col_1d != DCT2) return false;
    if (w == 32 && row_1d != DCT2 && row_1d != IDT) return false;
    if (h == 32 && col_1d != DCT2 && col_1d != IDT) return false;
    return true;
  }

  // 0-1: full-range random, 2: constant +max or -max, 3: +/-max checkerboard,
  // 4: small values with about half zeros.
  void FillResidual(ACMRandom *rnd, int pattern, int stride,
                    int16_t *resi) const {
    const int max_val = (1 << bd_) - 1;
    const int sign = rnd->Rand8() & 1 ? 1 : -1;
    for (int i = 0; i < kResiBufSize; ++i) {
      const int r = i / stride;
      const int c = i % stride;
      int v;
      switch (pattern) {
        case 2: v = sign * max_val; break;
        case 3: v = ((r + c) & 1) ? max_val : -max_val; break;
        case 4: v = (rnd->Rand8() & 1) ? 0 : (*rnd)(17) - 8; break;
        default: v = (*rnd)(2 * max_val + 1) - max_val; break;
      }
      resi[i] = static_cast<int16_t>(v);
    }
  }

  static void SetTxfmParam(TX_SIZE tx_size, PRIMARY_TX_TYPE primary_tx_type,
                           int use_ddt, int bd, TxfmParam *param) {
    memset(param, 0, sizeof(*param));
    param->primary_tx_type = primary_tx_type;
    param->tx_size = tx_size;
    param->bd = bd;
    param->lossless = 0;
    param->use_ddt = use_ddt;
    param->is_inter = use_ddt;
  }

  void AV2FwdTxfm2dMatchTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(64, int16_t, resi[kResiBufSize]);
    DECLARE_ALIGNED(64, tran_low_t, coeff_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(64, tran_low_t, coeff_tst[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      const int cols = tx_size_wide[tx_size];
      // Only the low 32x32 frequencies are kept. Values past max_eob are never
      // read and the SIMD kernels skip the C code's zeroing of them.
      const int num_coeffs = av2_get_max_eob((TX_SIZE)tx_size);
      const int strides[2] = { cols, kMaxStride };
      for (int tx_type = 0; tx_type < PRIMARY_TX_TYPES; ++tx_type) {
        if (!IsValidCombo((TX_SIZE)tx_size, (PRIMARY_TX_TYPE)tx_type)) continue;
        for (int use_ddt = 0; use_ddt < 2; ++use_ddt) {
          for (int s = 0; s < 2; ++s) {
            for (int pattern = 0; pattern < kNumPatterns; ++pattern) {
              FillResidual(&rnd, pattern, strides[s], resi);
              for (int i = 0; i < MAX_TX_SQUARE; ++i) {
                coeff_ref[i] = coeff_tst[i] = rnd.Rand31();
              }
              TxfmParam param;
              SetTxfmParam((TX_SIZE)tx_size, (PRIMARY_TX_TYPE)tx_type, use_ddt,
                           bd_, &param);

              fwd_txfm_c(resi, coeff_ref, strides[s], &param);
              test_func_(resi, coeff_tst, strides[s], &param);

              for (int i = 0; i < num_coeffs; ++i) {
                ASSERT_EQ(coeff_ref[i], coeff_tst[i])
                    << "tx_size=" << tx_size << " tx_type=" << tx_type
                    << " bd=" << bd_ << " use_ddt=" << use_ddt
                    << " stride=" << strides[s] << " pattern=" << pattern
                    << " pos=" << i;
              }
            }
          }
        }
      }
    }
  }

  // Lossless DCT_DCT is a 4x4 Walsh-Hadamard transform inside fwd_txfm.
  void AV2FwdTxfm2dLosslessTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(64, int16_t, resi[kResiBufSize]);
    DECLARE_ALIGNED(64, tran_low_t, coeff_ref[MAX_TX_SQUARE]);
    DECLARE_ALIGNED(64, tran_low_t, coeff_tst[MAX_TX_SQUARE]);
    const int num_coeffs = av2_get_max_eob(TX_4X4);
    const int strides[2] = { tx_size_wide[TX_4X4], kMaxStride };
    for (int s = 0; s < 2; ++s) {
      for (int pattern = 0; pattern < kNumPatterns; ++pattern) {
        FillResidual(&rnd, pattern, strides[s], resi);
        for (int i = 0; i < MAX_TX_SQUARE; ++i) {
          coeff_ref[i] = coeff_tst[i] = rnd.Rand31();
        }
        TxfmParam param;
        SetTxfmParam(TX_4X4, DCT_DCT, 0, bd_, &param);
        param.lossless = 1;

        fwd_txfm_c(resi, coeff_ref, strides[s], &param);
        test_func_(resi, coeff_tst, strides[s], &param);

        for (int i = 0; i < num_coeffs; ++i) {
          ASSERT_EQ(coeff_ref[i], coeff_tst[i])
              << "bd=" << bd_ << " stride=" << strides[s]
              << " pattern=" << pattern << " pos=" << i;
        }
      }
    }
  }

  void AV2FwdTxfm2dSpeedTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(64, int16_t, resi[kResiBufSize]);
    DECLARE_ALIGNED(64, tran_low_t, coeff[MAX_TX_SQUARE]);
    for (int tx_size = 0; tx_size < TX_SIZES_ALL; ++tx_size) {
      const int cols = tx_size_wide[tx_size];
      const int rows = tx_size_high[tx_size];
      FillResidual(&rnd, 0, cols, resi);
      TxfmParam param;
      SetTxfmParam((TX_SIZE)tx_size, DCT_DCT, 0, bd_, &param);
      // Same number of samples per size.
      const int num_loops = (1 << 22) / (cols * rows);

      avm_usec_timer ref_timer, test_timer;
      avm_usec_timer_start(&ref_timer);
      for (int i = 0; i < num_loops; ++i) fwd_txfm_c(resi, coeff, cols, &param);
      avm_usec_timer_mark(&ref_timer);
      const int elapsed_time_c =
          static_cast<int>(avm_usec_timer_elapsed(&ref_timer));

      avm_usec_timer_start(&test_timer);
      for (int i = 0; i < num_loops; ++i) test_func_(resi, coeff, cols, &param);
      avm_usec_timer_mark(&test_timer);
      const int elapsed_time_simd =
          static_cast<int>(avm_usec_timer_elapsed(&test_timer));

      printf(
          "bd[%2d] \t tx_size[%2d] (%2dx%2d) \t c_time=%d \t simd_time=%d \t "
          "gain=%f\n",
          bd_, tx_size, cols, rows, elapsed_time_c, elapsed_time_simd,
          ((1.0 * elapsed_time_c) / elapsed_time_simd));
    }
  }

  FwdTxfm2dFunc test_func_;
  int bd_;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2FwdTxfm2dTest);

TEST_P(AV2FwdTxfm2dTest, match) { AV2FwdTxfm2dMatchTest(); }
TEST_P(AV2FwdTxfm2dTest, lossless) { AV2FwdTxfm2dLosslessTest(); }
TEST_P(AV2FwdTxfm2dTest, DISABLED_Speed) { AV2FwdTxfm2dSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, AV2FwdTxfm2dTest,
                         ::testing::Combine(::testing::Values(fwd_txfm_avx2),
                                            ::testing::ValuesIn(kTestBd)),
                         BdParamName<AV2FwdTxfm2dTest::ParamType>);
#endif  // HAVE_AVX2
}  // namespace
