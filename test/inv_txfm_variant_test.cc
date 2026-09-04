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

#include <string.h>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/av2_rtcd.h"

#include "av2/common/common_data.h"
#include "av2/common/idct.h"
#include "avm_dsp/txfm_common.h"
#include "avm_ports/mem.h"
#include "test/acm_random.h"

using libavm_test::ACMRandom;

namespace {

struct InvTxfmParam {
  TX_SIZE tx_size;
  PRIM_TX_TYPE prim_tx_type;
  int bd;
  int seed;
  int use_ddt;
};

class InvTxfmVariantTest : public ::testing::TestWithParam<InvTxfmParam> {};

TEST_P(InvTxfmVariantTest, BitExact) {
  const InvTxfmParam &p = GetParam();
  const int txw = tx_size_wide[p.tx_size];
  const int txh = tx_size_high[p.tx_size];
  const int num_coeffs = txw * txh;
  const int max_pixel = (1 << p.bd) - 1;
  const int stride = txw + 8;
  const int buf_size = stride * txh;

  ACMRandom rng(p.seed);

  DECLARE_ALIGNED(32, tran_low_t, input[64 * 64]);
  DECLARE_ALIGNED(16, uint16_t, ref_dest[72 * 64]);
  DECLARE_ALIGNED(16, uint16_t, opt_dest[72 * 64]);

  memset(input, 0, sizeof(input));
  for (int k = 0; k < num_coeffs; k++) {
    int r = rng.Rand16() & 0x3FF;
    if (r < 100) {
      int val = rng.Rand15Signed() % (1 << (p.bd + 2));
      input[k] = (rng.Rand16() & 1) ? val : -val;
    }
  }

  for (int k = 0; k < buf_size; k++) {
    ref_dest[k] = rng.Rand16() & max_pixel;
    opt_dest[k] = ref_dest[k];
  }

  TxfmParam txfm_param;
  memset(&txfm_param, 0, sizeof(txfm_param));
  txfm_param.tx_size = p.tx_size;
  txfm_param.prim_tx_type = p.prim_tx_type;
  txfm_param.bd = p.bd;
  txfm_param.lossless = 0;
  txfm_param.use_ddt = p.use_ddt;

  inv_txfm_c(input, ref_dest, stride, &txfm_param);
  inv_txfm(input, opt_dest, stride, &txfm_param);

  for (int y = 0; y < txh; y++) {
    for (int x = 0; x < txw; x++) {
      ASSERT_EQ(ref_dest[y * stride + x], opt_dest[y * stride + x])
          << "mismatch at (" << x << "," << y << ")" << " tx_size=" << p.tx_size
          << " tx_type=" << p.prim_tx_type << " bd=" << p.bd
          << " seed=" << p.seed << " use_ddt=" << p.use_ddt;
    }
  }
}

static bool is_valid_inv_txfm_combo(TX_SIZE sz, PRIM_TX_TYPE ty) {
  if (ty == DCT_DCT || ty == IDTX) return true;
  const int w = tx_size_wide[sz];
  const int h = tx_size_high[sz];
  const int row_needs_adst = (ty == DCT_ADST || ty == ADST_ADST);
  const int col_needs_adst = (ty == ADST_DCT || ty == ADST_ADST);
  if (row_needs_adst && w > 16) return false;
  if (col_needs_adst && h > 16) return false;
  return true;
}

static std::vector<InvTxfmParam> GenerateParams() {
  std::vector<InvTxfmParam> params;
  const int seeds[] = { 1, 42, 100, 255, 1000, 2023, 3141, 5678, 7777, 9999 };
  const int bds[] = { 8, 10, 12 };
  const TX_SIZE sizes[] = { TX_4X4, TX_8X8,  TX_16X16, TX_32X32, TX_4X8,
                            TX_8X4, TX_8X16, TX_16X8,  TX_16X32, TX_32X16 };
  const PRIM_TX_TYPE types[] = { DCT_DCT, ADST_DCT, DCT_ADST, ADST_ADST, IDTX };

  for (int use_ddt = 0; use_ddt <= 1; use_ddt++)
    for (auto bd : bds)
      for (auto sz : sizes)
        for (auto ty : types)
          if (is_valid_inv_txfm_combo(sz, ty))
            for (auto seed : seeds)
              params.push_back({ sz, ty, bd, seed, use_ddt });

  return params;
}

INSTANTIATE_TEST_SUITE_P(Variants, InvTxfmVariantTest,
                         ::testing::ValuesIn(GenerateParams()));

TEST(InvTxfmVariantExtreme, AllZero) {
  const TX_SIZE sizes[] = { TX_4X4, TX_8X8, TX_16X16, TX_32X32 };
  const int bds[] = { 8, 10, 12 };

  for (auto bd : bds) {
    for (auto sz : sizes) {
      const int txw = tx_size_wide[sz];
      const int txh = tx_size_high[sz];
      const int stride = txw + 8;
      const int buf_size = stride * txh;

      DECLARE_ALIGNED(32, tran_low_t, input[64 * 64]);
      DECLARE_ALIGNED(16, uint16_t, ref_dest[72 * 64]);
      DECLARE_ALIGNED(16, uint16_t, opt_dest[72 * 64]);

      memset(input, 0, sizeof(input));
      for (int k = 0; k < buf_size; k++) {
        ref_dest[k] = (1 << bd) / 2;
        opt_dest[k] = ref_dest[k];
      }

      TxfmParam txfm_param;
      memset(&txfm_param, 0, sizeof(txfm_param));
      txfm_param.tx_size = sz;
      txfm_param.prim_tx_type = DCT_DCT;
      txfm_param.bd = bd;

      inv_txfm_c(input, ref_dest, stride, &txfm_param);
      inv_txfm(input, opt_dest, stride, &txfm_param);

      for (int i = 0; i < buf_size; i++) {
        ASSERT_EQ(ref_dest[i], opt_dest[i])
            << "all-zero mismatch at i=" << i << " sz=" << sz << " bd=" << bd;
      }
    }
  }
}

TEST(InvTxfmVariantExtreme, DcOnly) {
  const TX_SIZE sizes[] = { TX_4X4, TX_8X8, TX_16X16, TX_32X32 };
  const int bds[] = { 8, 10, 12 };

  for (auto bd : bds) {
    for (auto sz : sizes) {
      const int txw = tx_size_wide[sz];
      const int txh = tx_size_high[sz];
      const int stride = txw + 8;
      const int buf_size = stride * txh;

      DECLARE_ALIGNED(32, tran_low_t, input[64 * 64]);
      DECLARE_ALIGNED(16, uint16_t, ref_dest[72 * 64]);
      DECLARE_ALIGNED(16, uint16_t, opt_dest[72 * 64]);

      memset(input, 0, sizeof(input));
      input[0] = (1 << (bd + 2)) - 1;

      for (int k = 0; k < buf_size; k++) {
        ref_dest[k] = 0;
        opt_dest[k] = 0;
      }

      TxfmParam txfm_param;
      memset(&txfm_param, 0, sizeof(txfm_param));
      txfm_param.tx_size = sz;
      txfm_param.prim_tx_type = DCT_DCT;
      txfm_param.bd = bd;

      inv_txfm_c(input, ref_dest, stride, &txfm_param);
      inv_txfm(input, opt_dest, stride, &txfm_param);

      for (int y = 0; y < txh; y++) {
        for (int x = 0; x < txw; x++) {
          ASSERT_EQ(ref_dest[y * stride + x], opt_dest[y * stride + x])
              << "dc-only mismatch at (" << x << "," << y << ")" << " sz=" << sz
              << " bd=" << bd;
        }
      }
    }
  }
}

}  // namespace
