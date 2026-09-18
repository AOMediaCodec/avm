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

#include <stdlib.h>
#include <string.h>
#include <tuple>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "av2/common/common_data.h"
#include "av2/common/enums.h"
#include "avm_ports/avm_timer.h"
#include "avm_ports/mem.h"

using libavm_test::ACMRandom;

namespace {

// -- highbd fdct8x8 --

typedef void (*Fdct8x8Func)(const int16_t *, tran_low_t *, int);
typedef std::tuple<Fdct8x8Func, Fdct8x8Func> Fdct8x8Param;

class HighbdFdct8x8Test : public ::testing::TestWithParam<Fdct8x8Param> {
 protected:
  void TearDown() override { libavm_test::ClearSystemState(); }

  void RunCheck(const int16_t *input, int tolerance, const char *label) {
    DECLARE_ALIGNED(16, tran_low_t, output_ref[64]);
    DECLARE_ALIGNED(16, tran_low_t, output_tst[64]);
    memset(output_ref, 0, sizeof(output_ref));
    memset(output_tst, 0, sizeof(output_tst));

    Fdct8x8Func ref = std::get<0>(GetParam());
    Fdct8x8Func tst = std::get<1>(GetParam());

    ref(input, output_ref, 8);
    ASM_REGISTER_STATE_CHECK(tst(input, output_tst, 8));

    for (int i = 0; i < 64; i++) {
      ASSERT_LE(abs(output_ref[i] - output_tst[i]), tolerance)
          << label << " mismatch at " << i;
    }
  }

  void RunMatchTest(int num_iterations) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, int16_t, input[64]);

    for (int iter = 0; iter < num_iterations; iter++) {
      for (int i = 0; i < 64; i++) {
        input[i] = (int16_t)(rnd.Rand16() & 0x3FF) - 512;
      }
      RunCheck(input, 0, "random");
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HighbdFdct8x8Test);

TEST_P(HighbdFdct8x8Test, RandomBitExact) { RunMatchTest(1000); }

TEST_P(HighbdFdct8x8Test, ExtremesAllZero) {
  DECLARE_ALIGNED(16, int16_t, input[64]);
  memset(input, 0, sizeof(input));
  RunCheck(input, 0, "AllZero");
}

TEST_P(HighbdFdct8x8Test, ExtremesAllMax) {
  DECLARE_ALIGNED(16, int16_t, input[64]);
  for (int i = 0; i < 64; i++) input[i] = 511;
  RunCheck(input, 0, "AllMax");
}

TEST_P(HighbdFdct8x8Test, ExtremesAllMin) {
  DECLARE_ALIGNED(16, int16_t, input[64]);
  for (int i = 0; i < 64; i++) input[i] = -512;
  RunCheck(input, 0, "AllMin");
}

TEST_P(HighbdFdct8x8Test, ExtremesInt16Range) {
  DECLARE_ALIGNED(16, int16_t, input[64]);
  for (int i = 0; i < 64; i++) input[i] = (i & 1) ? INT16_MAX : INT16_MIN;
  RunCheck(input, 0, "Int16Extremes");
}

TEST_P(HighbdFdct8x8Test, RandomWideRange) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  DECLARE_ALIGNED(16, int16_t, input[64]);
  for (int iter = 0; iter < 1000; iter++) {
    for (int i = 0; i < 64; i++) input[i] = (int16_t)rnd.Rand16();
    RunCheck(input, 0, "wide-range");
  }
}

TEST_P(HighbdFdct8x8Test, DISABLED_Speed) {
  DECLARE_ALIGNED(16, int16_t, input[64]);
  DECLARE_ALIGNED(16, tran_low_t, output[64]);
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < 64; i++) input[i] = (int16_t)(rnd.Rand16() & 0x3FF) - 512;

  Fdct8x8Func ref = std::get<0>(GetParam());
  Fdct8x8Func tst = std::get<1>(GetParam());
  const int kNumIter = 10000000;

  avm_usec_timer timer_c;
  avm_usec_timer_start(&timer_c);
  for (int i = 0; i < kNumIter; i++) ref(input, output, 8);
  avm_usec_timer_mark(&timer_c);

  avm_usec_timer timer_tst;
  avm_usec_timer_start(&timer_tst);
  for (int i = 0; i < kNumIter; i++) tst(input, output, 8);
  avm_usec_timer_mark(&timer_tst);

  const double t_c = static_cast<double>(avm_usec_timer_elapsed(&timer_c));
  const double t_tst = static_cast<double>(avm_usec_timer_elapsed(&timer_tst));
  printf("highbd_fdct8x8: C=%7.1fms SIMD=%7.1fms gain=%.2fx\n", t_c / 1000.0,
         t_tst / 1000.0, t_c / t_tst);
}

INSTANTIATE_TEST_SUITE_P(C, HighbdFdct8x8Test,
                         ::testing::Values(std::make_tuple(
                             &avm_highbd_fdct8x8_c, &avm_highbd_fdct8x8_c)));

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, HighbdFdct8x8Test,
                         ::testing::Values(std::make_tuple(
                             &avm_highbd_fdct8x8_c, &avm_highbd_fdct8x8_neon)));
#endif  // HAVE_NEON
// -- fwd cross chroma tx --

typedef void (*CctxFunc)(tran_low_t *, tran_low_t *, TX_SIZE, CctxType,
                         const int);
typedef std::tuple<CctxFunc, CctxFunc, CctxType, TX_SIZE, int> CctxParam;

class FwdCctxTest : public ::testing::TestWithParam<CctxParam> {
 protected:
  void TearDown() override { libavm_test::ClearSystemState(); }

  void RunMatchTest(int num_iterations) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    CctxFunc ref = std::get<0>(GetParam());
    CctxFunc tst = std::get<1>(GetParam());
    CctxType cctx_type = std::get<2>(GetParam());
    TX_SIZE tx_size = std::get<3>(GetParam());
    int bd = std::get<4>(GetParam());
    const int ncoeffs = av2_get_max_eob(tx_size);
    DECLARE_ALIGNED(16, tran_low_t, c1_ref[1024]);
    DECLARE_ALIGNED(16, tran_low_t, c2_ref[1024]);
    DECLARE_ALIGNED(16, tran_low_t, c1_tst[1024]);
    DECLARE_ALIGNED(16, tran_low_t, c2_tst[1024]);

    for (int iter = 0; iter < num_iterations; iter++) {
      const int range = 1 << (7 + bd);
      for (int i = 0; i < ncoeffs; i++) {
        int32_t val1 = (int32_t)(rnd.Rand31() % (2 * range)) - range;
        int32_t val2 = (int32_t)(rnd.Rand31() % (2 * range)) - range;
        c1_ref[i] = c1_tst[i] = val1;
        c2_ref[i] = c2_tst[i] = val2;
      }

      ref(c1_ref, c2_ref, tx_size, cctx_type, bd);
      ASM_REGISTER_STATE_CHECK(tst(c1_tst, c2_tst, tx_size, cctx_type, bd));

      for (int i = 0; i < ncoeffs; i++) {
        ASSERT_EQ(c1_ref[i], c1_tst[i])
            << "c1 mismatch at " << i << " cctx=" << cctx_type << " bd=" << bd
            << " iter=" << iter;
        ASSERT_EQ(c2_ref[i], c2_tst[i])
            << "c2 mismatch at " << i << " cctx=" << cctx_type << " bd=" << bd
            << " iter=" << iter;
      }
    }
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FwdCctxTest);

TEST_P(FwdCctxTest, RandomBitExact) { RunMatchTest(1000); }

// jscpd:ignore-start
TEST_P(FwdCctxTest, DISABLED_Speed) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  CctxFunc ref = std::get<0>(GetParam());
  CctxFunc tst = std::get<1>(GetParam());
  CctxType cctx_type = std::get<2>(GetParam());
  TX_SIZE tx_size = std::get<3>(GetParam());
  int bd = std::get<4>(GetParam());
  const int ncoeffs = av2_get_max_eob(tx_size);

  DECLARE_ALIGNED(16, tran_low_t, c1[1024]);
  DECLARE_ALIGNED(16, tran_low_t, c2[1024]);
  const int range = 1 << (7 + bd);
  for (int i = 0; i < ncoeffs; i++) {
    c1[i] = (int32_t)(rnd.Rand31() % (2 * range)) - range;
    c2[i] = (int32_t)(rnd.Rand31() % (2 * range)) - range;
  }

  const int kNumIter = 1000000;

  avm_usec_timer timer_c;
  avm_usec_timer_start(&timer_c);
  for (int i = 0; i < kNumIter; i++) ref(c1, c2, tx_size, cctx_type, bd);
  avm_usec_timer_mark(&timer_c);

  avm_usec_timer timer_tst;
  avm_usec_timer_start(&timer_tst);
  for (int i = 0; i < kNumIter; i++) tst(c1, c2, tx_size, cctx_type, bd);
  avm_usec_timer_mark(&timer_tst);

  const double t_c = static_cast<double>(avm_usec_timer_elapsed(&timer_c));
  const double t_tst = static_cast<double>(avm_usec_timer_elapsed(&timer_tst));
  printf("fwd_cctx cctx=%d bd=%d: C=%7.1fms SIMD=%7.1fms gain=%.2fx\n",
         cctx_type, bd, t_c / 1000.0, t_tst / 1000.0, t_c / t_tst);
}
// jscpd:ignore-end

static const CctxType kCctxTypes[] = {
  CCTX_45, CCTX_30, CCTX_60, CCTX_MINUS45, CCTX_MINUS30, CCTX_MINUS60
};
static const TX_SIZE kCctxTxSizes[] = { TX_4X4, TX_8X8, TX_16X16 };

INSTANTIATE_TEST_SUITE_P(
    C, FwdCctxTest,
    ::testing::Combine(::testing::Values(&av2_fwd_cross_chroma_tx_block_c),
                       ::testing::Values(&av2_fwd_cross_chroma_tx_block_c),
                       ::testing::ValuesIn(kCctxTypes),
                       ::testing::ValuesIn(kCctxTxSizes),
                       ::testing::Values(8, 10, 12)));

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, FwdCctxTest,
    ::testing::Combine(::testing::Values(&av2_fwd_cross_chroma_tx_block_c),
                       ::testing::Values(&av2_fwd_cross_chroma_tx_block_neon),
                       ::testing::ValuesIn(kCctxTypes),
                       ::testing::ValuesIn(kCctxTxSizes),
                       ::testing::Values(8, 10, 12)));
#endif  // HAVE_NEON

// -- fwd_txfm (full pipeline C vs RTCD) --

struct FwdTxfmParam {
  TX_SIZE tx_size;
  TX_TYPE tx_type;
  int bd;
  int seed;
  int use_ddt;
};

class FwdTxfmVariantTest : public ::testing::TestWithParam<FwdTxfmParam> {};

TEST_P(FwdTxfmVariantTest, BitExact) {
  const FwdTxfmParam &p = GetParam();
  const int txw = tx_size_wide[p.tx_size];
  const int txh = tx_size_high[p.tx_size];
  const int num_coeffs = txw * txh;

  ACMRandom rng(p.seed);

  DECLARE_ALIGNED(32, int16_t, input[64 * 64]);
  DECLARE_ALIGNED(32, tran_low_t, ref_coeff[64 * 64]);
  DECLARE_ALIGNED(32, tran_low_t, opt_coeff[64 * 64]);

  const int max_resi = (1 << p.bd) - 1;
  for (int k = 0; k < txw * txh; k++) {
    input[k] =
        (int16_t)((int32_t)(rng.Rand31() % (2 * max_resi + 1)) - max_resi);
  }
  memset(ref_coeff, 0, sizeof(ref_coeff));
  memset(opt_coeff, 0, sizeof(opt_coeff));

  TxfmParam txfm_param;
  memset(&txfm_param, 0, sizeof(txfm_param));
  txfm_param.tx_size = p.tx_size;
  txfm_param.tx_type = p.tx_type;
  txfm_param.bd = p.bd;
  txfm_param.lossless = 0;
  txfm_param.use_ddt = p.use_ddt;

  fwd_txfm_c(input, ref_coeff, txw, &txfm_param);
  fwd_txfm(input, opt_coeff, txw, &txfm_param);

  for (int k = 0; k < num_coeffs; k++) {
    ASSERT_EQ(ref_coeff[k], opt_coeff[k])
        << "mismatch at " << k << " tx_size=" << p.tx_size
        << " tx_type=" << p.tx_type << " bd=" << p.bd << " seed=" << p.seed
        << " use_ddt=" << p.use_ddt;
  }
}

static bool is_valid_fwd_txfm_combo(TX_SIZE sz, TX_TYPE ty) {
  if (ty == DCT_DCT || ty == IDTX) return true;
  const int w = tx_size_wide[sz];
  const int h = tx_size_high[sz];
  const int row_1d = g_hor_tx_type[ty];
  const int col_1d = g_ver_tx_type[ty];
  if ((row_1d == DST7 || row_1d == DCT8) && w > 16) return false;
  if ((col_1d == DST7 || col_1d == DCT8) && h > 16) return false;
  if (row_1d == IDT && w > 32) return false;
  if (col_1d == IDT && h > 32) return false;
  return true;
}

static std::vector<FwdTxfmParam> GenerateFwdParams() {
  std::vector<FwdTxfmParam> params;
  const int seeds[] = { 1, 42, 100, 255, 1000, 2023, 3141, 5678, 7777, 9999 };
  const int bds[] = { 8, 10, 12 };
  const TX_SIZE sizes[] = { TX_4X4,  TX_8X8,  TX_16X16, TX_32X32, TX_4X8,
                            TX_8X4,  TX_8X16, TX_16X8,  TX_16X32, TX_32X16,
                            TX_4X16, TX_16X4, TX_8X32,  TX_32X8,  TX_4X32,
                            TX_32X4 };
  const TX_TYPE types[] = {
    DCT_DCT,      ADST_DCT,          DCT_ADST, ADST_ADST, FLIPADST_DCT,
    DCT_FLIPADST, FLIPADST_FLIPADST, IDTX,     V_DCT,     H_DCT
  };

  for (int use_ddt = 0; use_ddt <= 1; use_ddt++)
    for (auto bd : bds)
      for (auto sz : sizes)
        for (auto ty : types)
          if (is_valid_fwd_txfm_combo(sz, ty))
            for (auto seed : seeds)
              params.push_back({ sz, ty, bd, seed, use_ddt });

  return params;
}

INSTANTIATE_TEST_SUITE_P(Variants, FwdTxfmVariantTest,
                         ::testing::ValuesIn(GenerateFwdParams()));

TEST(FwdTxfmVariantExtreme, AllZero) {
  const TX_SIZE sizes[] = { TX_4X4, TX_8X8, TX_16X16, TX_32X32 };
  const int bds[] = { 8, 10, 12 };

  for (auto bd : bds) {
    for (auto sz : sizes) {
      const int txw = tx_size_wide[sz];
      const int txh = tx_size_high[sz];

      DECLARE_ALIGNED(32, int16_t, input[64 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, ref_coeff[64 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, opt_coeff[64 * 64]);

      memset(input, 0, sizeof(input));
      memset(ref_coeff, 0, sizeof(ref_coeff));
      memset(opt_coeff, 0, sizeof(opt_coeff));

      TxfmParam txfm_param;
      memset(&txfm_param, 0, sizeof(txfm_param));
      txfm_param.tx_size = sz;
      txfm_param.tx_type = DCT_DCT;
      txfm_param.bd = bd;

      fwd_txfm_c(input, ref_coeff, txw, &txfm_param);
      fwd_txfm(input, opt_coeff, txw, &txfm_param);

      for (int k = 0; k < txw * txh; k++) {
        ASSERT_EQ(ref_coeff[k], opt_coeff[k])
            << "all-zero mismatch at " << k << " sz=" << sz << " bd=" << bd;
      }
    }
  }
}

TEST(FwdTxfmVariantExtreme, StrideMismatch) {
  const TX_SIZE sizes[] = { TX_8X8, TX_16X16 };
  const int bds[] = { 8, 10, 12 };

  for (auto bd : bds) {
    for (auto sz : sizes) {
      const int txw = tx_size_wide[sz];
      const int txh = tx_size_high[sz];
      const int stride = txw + 8;

      DECLARE_ALIGNED(32, int16_t, input[72 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, ref_coeff[64 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, opt_coeff[64 * 64]);

      ACMRandom rng(42);
      const int max_resi = (1 << bd) - 1;
      for (int y = 0; y < txh; y++)
        for (int x = 0; x < stride; x++)
          input[y * stride + x] =
              (int16_t)((int32_t)(rng.Rand31() % (2 * max_resi + 1)) -
                        max_resi);
      memset(ref_coeff, 0, sizeof(ref_coeff));
      memset(opt_coeff, 0, sizeof(opt_coeff));

      TxfmParam txfm_param;
      memset(&txfm_param, 0, sizeof(txfm_param));
      txfm_param.tx_size = sz;
      txfm_param.tx_type = DCT_DCT;
      txfm_param.bd = bd;

      fwd_txfm_c(input, ref_coeff, stride, &txfm_param);
      fwd_txfm(input, opt_coeff, stride, &txfm_param);

      for (int k = 0; k < txw * txh; k++) {
        ASSERT_EQ(ref_coeff[k], opt_coeff[k])
            << "stride mismatch at " << k << " sz=" << sz << " bd=" << bd;
      }
    }
  }
}

TEST(FwdTxfmVariantExtreme, MaxResidualNonSquare) {
  const TX_SIZE sizes[] = { TX_4X8,  TX_8X4,  TX_8X16, TX_16X8,
                            TX_4X16, TX_16X4, TX_8X32, TX_32X8 };
  const int bds[] = { 8, 10, 12 };

  for (auto bd : bds) {
    const int max_resi = (1 << bd) - 1;
    for (auto sz : sizes) {
      const int txw = tx_size_wide[sz];
      const int txh = tx_size_high[sz];

      DECLARE_ALIGNED(32, int16_t, input[64 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, ref_coeff[64 * 64]);
      DECLARE_ALIGNED(32, tran_low_t, opt_coeff[64 * 64]);

      for (int k = 0; k < txw * txh; k++)
        input[k] = (k & 1) ? max_resi : -max_resi;
      memset(ref_coeff, 0, sizeof(ref_coeff));
      memset(opt_coeff, 0, sizeof(opt_coeff));

      TxfmParam txfm_param;
      memset(&txfm_param, 0, sizeof(txfm_param));
      txfm_param.tx_size = sz;
      txfm_param.tx_type = DCT_DCT;
      txfm_param.bd = bd;

      fwd_txfm_c(input, ref_coeff, txw, &txfm_param);
      fwd_txfm(input, opt_coeff, txw, &txfm_param);

      for (int k = 0; k < txw * txh; k++) {
        ASSERT_EQ(ref_coeff[k], opt_coeff[k])
            << "max-resi mismatch at " << k << " sz=" << sz << " bd=" << bd;
      }
    }
  }
}

}  // namespace
