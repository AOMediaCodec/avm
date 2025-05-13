/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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
#include <stdlib.h>
#include <string.h>
#include <tuple>

#include "aom_dsp/aom_dsp_common.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/av1_rtcd.h"
#include "config/aom_dsp_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/transform_test_base.h"
#include "test/util.h"
#include "aom/aom_integer.h"
#include "aom_ports/mem.h"

using libaom_test::ACMRandom;

namespace {

typedef void (*FwdFunc)(const int16_t *in, tran_low_t *out, int stride,
                        TxfmParam *);
typedef void (*InvFunc)(const tran_low_t *in, uint16_t *out, int stride,
                        const TxfmParam *);

typedef std::tuple<FwdFunc, InvFunc, int, aom_bit_depth_t> LosslessParam;

class LosslessIdtxTest : public libaom_test::TransformTestBase<tran_low_t>,
                         public ::testing::TestWithParam<LosslessParam> {
 public:
  virtual ~LosslessIdtxTest() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    inv_txfm_ = GET_PARAM(1);
    TX_SIZE tx_size = GET_PARAM(2);
    bit_depth_ = GET_PARAM(3);

    fwd_txfm_ref = av1_lossless_fwd_idtx_c;
    inv_txfm_ref = av1_lossless_inv_idtx_add_c;

    mask_ = (1 << bit_depth_) - 1;
    const int txw = tx_size_wide[tx_size];
    const int txh = tx_size_high[tx_size];
    height_ = txh;
    pitch_ = txw;
    num_coeffs_ = txw * txh;
    txfm_param_.bd = bit_depth_;
    txfm_param_.tx_size = tx_size;
  }
  virtual void TearDown() { libaom_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride, &txfm_param_);
  }
  void RunInvTxfm(const tran_low_t *out, uint16_t *dst, int stride) {
    inv_txfm_(out, dst, stride, &txfm_param_);
  }

  FwdFunc fwd_txfm_;
  InvFunc inv_txfm_;
};

TEST_P(LosslessIdtxTest, AccuracyCheck) { RunAccuracyCheck(0, 0.0); }

TEST_P(LosslessIdtxTest, CoeffCheck) { RunCoeffCheck(); }

TEST_P(LosslessIdtxTest, MemCheck) { RunMemCheck(); }

TEST_P(LosslessIdtxTest, InvAccuracyCheck) { RunInvAccuracyCheck(0); }

INSTANTIATE_TEST_SUITE_P(
    C, LosslessIdtxTest,
    ::testing::Combine(::testing::Values(&av1_lossless_fwd_idtx_c),
                       ::testing::Values(&av1_lossless_inv_idtx_add_c),
                       ::testing::Values(TX_4X4, TX_8X8),
                       ::testing::Values(AOM_BITS_8, AOM_BITS_10,
                                         AOM_BITS_12)));

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, LosslessIdtxTest,
    ::testing::Combine(::testing::Values(&av1_lossless_fwd_idtx_avx2),
                       ::testing::Values(&av1_lossless_inv_idtx_add_avx2),
                       ::testing::Values(TX_4X4, TX_8X8),
                       ::testing::Values(AOM_BITS_8, AOM_BITS_10,
                                         AOM_BITS_12)));
#endif  // HAVE_AVX2
}  // namespace
