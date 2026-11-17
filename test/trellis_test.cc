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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/register_state_check.h"
#include "test/function_equivalence_test.h"
#include "test/util.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "avm/avm_integer.h"
#include "av2/common/enums.h"
#include "av2/common/idct.h"
#include "av2/common/scan.h"
#include "av2/encoder/trellis_quant.h"
#include "av2/common/txb_common.h"

using libavm_test::FunctionEquivalenceTest;

namespace {

template <typename F, typename T>
class TcqRateTest : public FunctionEquivalenceTest<F> {
 protected:
  static const int kIterations = 100000;

  virtual ~TcqRateTest() {}

  virtual void Execute(T *rate_tst) = 0;

  void Common() {
    Execute(&rate_tst_);

    ASSERT_EQ(rate_ref_.rate_zero[0], rate_tst_.rate_zero[0]);
    ASSERT_EQ(rate_ref_.rate_zero[1], rate_tst_.rate_zero[1]);
    ASSERT_EQ(rate_ref_.rate_eob[0], rate_tst_.rate_eob[0]);
    ASSERT_EQ(rate_ref_.rate_eob[1], rate_tst_.rate_eob[1]);
    for (int i = 0; i < 8; i++) {
      ASSERT_EQ(rate_ref_.rate[i], rate_tst_.rate[i]);
    }
  }

  T rate_ref_;
  T rate_tst_;
};

//////////////////////////////////////////////////////////////////////////////
// TCQ Rate calculation functions.
//////////////////////////////////////////////////////////////////////////////

typedef void (*TcqRateLuma)(const struct tcq_param_t *p,
                            const struct prequant_t *pq,
                            const struct tcq_coeff_ctx_t *coeff_ctx,
                            int blk_pos, int diag_ctx, int eob_rate,
                            struct tcq_rate_t *rd);
typedef libavm_test::FuncParam<TcqRateLuma> TcqRateLumaTestFuncs;

class TcqRateLumaTest : public TcqRateTest<TcqRateLuma, tcq_rate_t> {
 protected:
  void Execute(tcq_rate_t *rate_tst) {
    params_.ref_func(&param_, &pre_quant_, &coeff_ctx_, blk_pos_, diag_ctx_,
                     eob_rate_, &rate_ref_);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(&param_, &pre_quant_, &coeff_ctx_,
                                              blk_pos_, diag_ctx_, eob_rate_,
                                              rate_tst));
  }
  tcq_param_t param_;
  LV_MAP_COEFF_COST txb_costs_;
  prequant_t pre_quant_;
  tcq_coeff_ctx_t coeff_ctx_;
  int blk_pos_;
  int diag_ctx_;
  int eob_rate_;
};

typedef void (*TcqRateLfLuma)(const struct tcq_param_t *p,
                              const struct prequant_t *pq,
                              const struct tcq_coeff_ctx_t *coeff_ctx,
                              int blk_pos, int diag_ctx, int eob_rate,
                              int coeff_sign, struct tcq_rate_t *rd);
typedef libavm_test::FuncParam<TcqRateLfLuma> TcqRateLfLumaTestFuncs;

class TcqRateLfLumaTest : public TcqRateTest<TcqRateLfLuma, tcq_rate_t> {
 protected:
  void Execute(tcq_rate_t *rate_tst) {
    params_.ref_func(&param_, &pre_quant_, &coeff_ctx_, blk_pos_, diag_ctx_,
                     eob_rate_, coeff_sign_, &rate_ref_);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(&param_, &pre_quant_, &coeff_ctx_,
                                              blk_pos_, diag_ctx_, eob_rate_,
                                              coeff_sign_, rate_tst));
  }
  tcq_param_t param_;
  LV_MAP_COEFF_COST txb_costs_;
  prequant_t pre_quant_;
  tcq_coeff_ctx_t coeff_ctx_;
  int blk_pos_;
  int diag_ctx_;
  int eob_rate_;
  int coeff_sign_;
  int tmp_sign_[1024];
};

static int generate_random_q_idx(libavm_test::ACMRandom *rng) {
  int r1 = rng->Rand8() & 15;
  int r2 = (r1 == 15) ? rng->Rand8() & 15 : 0;
  int r3 = (r2 == 15) ? rng->Rand16() & 8191 : 0;
  int r = r1 + r2 + r3;
  return r;
}

// Init coeff syntax costs randomly
// - base_cost[], lps_cost[], base_eob_cost[]
// - base_cost_zero[], base_cost_low_tbl[], base_eob_cost_tbl[], mid_cost_tbl[]
static void generate_random_cost_tables(libavm_test::ACMRandom *rng,
                                        LV_MAP_COEFF_COST *txb_costs) {
  int max = 2048 - 1;
  int n;
  int *p0;

  // Init sign costs
  n = sizeof(txb_costs->dc_sign_cost) / sizeof(txb_costs->dc_sign_cost[0][0]);
  p0 = txb_costs->dc_sign_cost[0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }

  // Init base costs
  n = sizeof(txb_costs->base_cost) / sizeof(txb_costs->base_cost[0][0][0]);
  p0 = &txb_costs->base_cost[0][0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }
  n = sizeof(txb_costs->base_lf_cost) /
      sizeof(txb_costs->base_lf_cost[0][0][0]);
  p0 = &txb_costs->base_lf_cost[0][0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }

  // Init mid-range (lps) costs
  n = sizeof(txb_costs->lps_cost) / sizeof(txb_costs->lps_cost[0][0]);
  p0 = &txb_costs->lps_cost[0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }
  n = sizeof(txb_costs->lps_lf_cost) / sizeof(txb_costs->lps_lf_cost[0][0]);
  p0 = &txb_costs->lps_lf_cost[0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }

  // Init base_eob costs
  n = sizeof(txb_costs->base_eob_cost) / sizeof(txb_costs->base_eob_cost[0][0]);
  p0 = &txb_costs->base_eob_cost[0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }
  n = sizeof(txb_costs->base_lf_eob_cost) /
      sizeof(txb_costs->base_lf_eob_cost[0][0]);
  p0 = &txb_costs->base_lf_eob_cost[0][0];
  for (int i = 0; i < n; i++) {
    *p0++ = rng->Rand16() & max;
  }

  // Rearrange costs into base_cost_zero[] array for quicker access.
  // (from av2/encoder/rd.c)
  for (int q_i = 0; q_i < TCQ_CTXS; q_i++) {
    for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
      txb_costs->base_cost_zero[q_i][ctx] = txb_costs->base_cost[ctx][q_i][0];
    }
  }
  // Rearrange costs into base_lf_cost_zero[] array for quicker access.
  for (int q_i = 0; q_i < TCQ_CTXS; q_i++) {
    for (int ctx = 0; ctx < LF_SIG_COEF_CONTEXTS; ++ctx) {
      txb_costs->base_lf_cost_zero[q_i][ctx] =
          txb_costs->base_lf_cost[ctx][q_i][0];
    }
  }
  // Precompute some base_costs for trellis, interleaved for quick access.
  // Look-up take to retrive data from precomputed cost array
  static const uint8_t trel_abslev[15][4] = {
    { 2, 1, 1, 2 },  // qIdx=1
    { 2, 3, 1, 2 },  // qidx=2
    { 2, 3, 3, 2 },  // qidx=3
    { 2, 3, 3, 4 },  // qidx=4
    { 4, 3, 3, 4 },  // qidx=5
    { 4, 5, 3, 4 },  // qidx=6
    { 4, 5, 5, 4 },  // qidx=7
    { 4, 5, 5, 6 },  // qidx=8
    { 6, 5, 5, 6 },  // qidx=9
    { 6, 7, 5, 6 },  // qidx=10
    { 6, 7, 7, 6 },  // qidx=11
    { 6, 7, 7, 8 },  // qidx=12
    { 8, 7, 7, 8 },  // qidx=13
    { 8, 9, 7, 8 },  // qidx=14
    { 8, 9, 9, 8 },  // qidx=15
  };
  for (int idx = 0; idx < 5; idx++) {
    int a0 = AVMMIN(trel_abslev[idx][0], 3);
    int a1 = AVMMIN(trel_abslev[idx][1], 3);
    int a2 = AVMMIN(trel_abslev[idx][2], 3);
    int a3 = AVMMIN(trel_abslev[idx][3], 3);
    for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
      // Q0, absLev 0 / 2
      txb_costs->base_cost_low_tbl[idx][ctx][0][0] =
          txb_costs->base_cost[ctx][0][a0] + av2_cost_literal(1);
      txb_costs->base_cost_low_tbl[idx][ctx][0][1] =
          txb_costs->base_cost[ctx][0][a2] + av2_cost_literal(1);
      // Q1, absLev 1 / 3
      txb_costs->base_cost_low_tbl[idx][ctx][1][0] =
          txb_costs->base_cost[ctx][1][a1] + av2_cost_literal(1);
      txb_costs->base_cost_low_tbl[idx][ctx][1][1] =
          txb_costs->base_cost[ctx][1][a3] + av2_cost_literal(1);
    }
    for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx) {
      // EOB coeff, absLev 0 / 2
      txb_costs->base_eob_cost_tbl[idx][ctx][0] =
          txb_costs->base_eob_cost[ctx][a0 - 1] + av2_cost_literal(1);
      txb_costs->base_eob_cost_tbl[idx][ctx][1] =
          txb_costs->base_eob_cost[ctx][a2 - 1] + av2_cost_literal(1);
    }
  }
  for (int idx = 0; idx < 9; idx++) {
    int max = LF_BASE_SYMBOLS - 1;
    int a0 = AVMMIN(trel_abslev[idx][0], max);
    int a1 = AVMMIN(trel_abslev[idx][1], max);
    int a2 = AVMMIN(trel_abslev[idx][2], max);
    int a3 = AVMMIN(trel_abslev[idx][3], max);
    for (int ctx = 0; ctx < LF_SIG_COEF_CONTEXTS; ++ctx) {
      // Q0, absLev 0 / 2
      txb_costs->base_lf_cost_low_tbl[idx][ctx][0][0] =
          txb_costs->base_lf_cost[ctx][0][a0] + av2_cost_literal(1);
      txb_costs->base_lf_cost_low_tbl[idx][ctx][0][1] =
          txb_costs->base_lf_cost[ctx][0][a2] + av2_cost_literal(1);
      // Q1, absLev 1 / 3
      txb_costs->base_lf_cost_low_tbl[idx][ctx][1][0] =
          txb_costs->base_lf_cost[ctx][1][a1] + av2_cost_literal(1);
      txb_costs->base_lf_cost_low_tbl[idx][ctx][1][1] =
          txb_costs->base_lf_cost[ctx][1][a3] + av2_cost_literal(1);
    }
    for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx) {
      // EOB coeff, absLev 0 / 2
      txb_costs->base_lf_eob_cost_tbl[idx][ctx][0] =
          txb_costs->base_lf_eob_cost[ctx][a0 - 1] + av2_cost_literal(1);
      txb_costs->base_lf_eob_cost_tbl[idx][ctx][1] =
          txb_costs->base_lf_eob_cost[ctx][a2 - 1] + av2_cost_literal(1);
    }
  }
  // Precalc mid costs for default region.
  for (int idx = 0; idx < 5 + 2 * COEFF_BASE_RANGE; idx++) {
    int a0 = get_low_range(trel_abslev[idx][0], 0);
    int a1 = get_low_range(trel_abslev[idx][1], 0);
    int a2 = get_low_range(trel_abslev[idx][2], 0);
    int a3 = get_low_range(trel_abslev[idx][3], 0);
    for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
      // Q0, absLev 0 / 2
      txb_costs->mid_cost_tbl[idx][ctx][0][0] =
          a0 < 0 ? 0 : txb_costs->lps_cost[ctx][a0];
      txb_costs->mid_cost_tbl[idx][ctx][0][1] =
          a2 < 0 ? 0 : txb_costs->lps_cost[ctx][a2];
      // Q1, absLev 1 / 3
      txb_costs->mid_cost_tbl[idx][ctx][1][0] =
          a1 < 0 ? 0 : txb_costs->lps_cost[ctx][a1];
      txb_costs->mid_cost_tbl[idx][ctx][1][1] =
          a3 < 0 ? 0 : txb_costs->lps_cost[ctx][a3];
    }
  }
  // Precalc mid costs for default region.
  for (int idx = 0; idx < 9 + 2 * COEFF_BASE_RANGE; idx++) {
    int a0 = get_low_range(trel_abslev[idx][0], 1);
    int a1 = get_low_range(trel_abslev[idx][1], 1);
    int a2 = get_low_range(trel_abslev[idx][2], 1);
    int a3 = get_low_range(trel_abslev[idx][3], 1);
    for (int ctx = 0; ctx < LF_LEVEL_CONTEXTS; ++ctx) {
      // Q0, absLev 0 / 2
      txb_costs->mid_lf_cost_tbl[idx][ctx][0][0] =
          a0 < 0 ? 0 : txb_costs->lps_lf_cost[ctx][a0];
      txb_costs->mid_lf_cost_tbl[idx][ctx][0][1] =
          a2 < 0 ? 0 : txb_costs->lps_lf_cost[ctx][a2];
      // Q1, absLev 1 / 3
      txb_costs->mid_lf_cost_tbl[idx][ctx][1][0] =
          a1 < 0 ? 0 : txb_costs->lps_lf_cost[ctx][a1];
      txb_costs->mid_lf_cost_tbl[idx][ctx][1][1] =
          a3 < 0 ? 0 : txb_costs->lps_lf_cost[ctx][a3];
    }
  }
}

TEST_P(TcqRateLumaTest, RandomValues) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    int log_scale = 1;
    int shift = 16 - log_scale + QUANT_FP_BITS;
    const int32_t quant[2] = { 1 << shift, 1 << shift };
    int dqv = 1 << QUANT_TABLE_BITS;
    int tqc = iter < 16000 ? iter : generate_random_q_idx(&rng_);

    // Initialize param structure.
    int bwl = 2 + (rng_.Rand8() & 3);
    int height = 1 << bwl;
    int max = (1 << bwl) - 1;
    int row = rng_.Rand8() & max;
    int col = rng_.Rand8() & max;
    row = AVMMAX(row, 4);
    col = AVMMAX(col, 4);
    int blk_pos = (row << bwl) + col;
    int scan_pos = blk_pos;
    int diag_ctx = get_nz_map_ctx_from_stats(0, blk_pos, bwl, TX_CLASS_2D, 0);

    blk_pos_ = blk_pos;
    diag_ctx_ = diag_ctx;
    param_.bwl = bwl;
    param_.txb_height = height;
    param_.tx_class = 0;
    param_.txb_costs = &txb_costs_;

    // Generate random syntax costs.
    generate_random_cost_tables(&rng_, &txb_costs_);

    // Generate pre_quant info with random coeff.
    av2_pre_quant_c(tqc, &pre_quant_, quant, dqv, log_scale, scan_pos);
    eob_rate_ = rng_(512 * 4);

    // Generate random coeff_ctx
    for (int i = 0; i < 8; i++) {
      coeff_ctx_.coef[i] = (rng_(4) << 4) + rng_(4);
    }
    coeff_ctx_.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    coeff_ctx_.pad[0] = 0;
    coeff_ctx_.pad[1] = 0;
    coeff_ctx_.pad[2] = 0;

    Common();
  }
}

class TcqRateLumaQ1Test : public FunctionEquivalenceTest<TcqRateLuma> {
 protected:
  static const int kIterations = 100000;

  void Execute(tcq_rate_t *rate_ref, tcq_rate_t *rate_tst) {
    memset(rate_ref, 0, sizeof(*rate_ref));
    memset(rate_tst, 0, sizeof(*rate_tst));
    params_.ref_func(&param_, &pre_quant_, &coeff_ctx_, blk_pos_, diag_ctx_,
                     eob_rate_, rate_ref);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(&param_, &pre_quant_, &coeff_ctx_,
                                              blk_pos_, diag_ctx_, eob_rate_,
                                              rate_tst));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      int log_scale = 1;
      int shift = 16 - log_scale + QUANT_FP_BITS;
      const int32_t quant[2] = { 1 << shift, 1 << shift };
      int dqv = 1 << QUANT_TABLE_BITS;
      int tqc = 0;

      // Initialize param structure.
      int bwl = 3 + (rng_.Rand8() & 2);
      int height = 1 << bwl;
      int max = (1 << bwl) - 1;
      int row = rng_.Rand8() & max;
      int col = rng_.Rand8() & max;
      row = AVMMAX(row, 4);
      col = AVMMAX(col, 4);
      int blk_pos = (row << bwl) + col;
      int scan_pos = blk_pos;
      int diag_ctx = get_nz_map_ctx_from_stats(0, blk_pos, bwl, TX_CLASS_2D, 0);

      blk_pos_ = blk_pos;
      diag_ctx_ = diag_ctx;
      param_.bwl = bwl;
      param_.txb_height = height;
      param_.tx_class = 0;
      param_.txb_costs = &txb_costs_;

      // Generate random syntax costs.
      generate_random_cost_tables(&rng_, &txb_costs_);

      // Generate pre_quant info with random coeff.
      av2_pre_quant_q1(tqc, &pre_quant_, quant, dqv, log_scale, scan_pos);
      eob_rate_ = rng_(512 * 4);

      // Generate random coeff_ctx
      for (int i = 0; i < 8; i++) {
        coeff_ctx_.coef[i] = (rng_(4) << 4) + rng_(4);
      }
      coeff_ctx_.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
      coeff_ctx_.pad[0] = 0;
      coeff_ctx_.pad[1] = 0;
      coeff_ctx_.pad[2] = 0;

      tcq_rate_t rate_ref, rate_tst;
      Execute(&rate_ref, &rate_tst);

      for (int i = 0; i < 8; i++) {
        ASSERT_EQ(rate_ref.rate_zero[i], rate_tst.rate_zero[i])
            << "rate_zero mismatch at i=" << i;
      }
      ASSERT_EQ(rate_ref.rate_eob[1], rate_tst.rate_eob[1]);
      ASSERT_EQ(rate_ref.rate[1], rate_tst.rate[1]);
      ASSERT_EQ(rate_ref.rate[3], rate_tst.rate[3]);
      ASSERT_EQ(rate_ref.rate[4], rate_tst.rate[4]);
      ASSERT_EQ(rate_ref.rate[6], rate_tst.rate[6]);
      ASSERT_EQ(rate_ref.rate[9], rate_tst.rate[9]);
      ASSERT_EQ(rate_ref.rate[11], rate_tst.rate[11]);
      ASSERT_EQ(rate_ref.rate[12], rate_tst.rate[12]);
      ASSERT_EQ(rate_ref.rate[14], rate_tst.rate[14]);
    }
  }

  tcq_param_t param_;
  LV_MAP_COEFF_COST txb_costs_;
  prequant_t pre_quant_;
  tcq_coeff_ctx_t coeff_ctx_;
  int blk_pos_;
  int diag_ctx_;
  int eob_rate_;
};

TEST_P(TcqRateLumaQ1Test, RandomValues) { RunTest(); }

TEST_P(TcqRateLfLumaTest, RandomValues) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    int log_scale = 1;
    int shift = 16 - log_scale + QUANT_FP_BITS;
    const int32_t quant[2] = { 1 << shift, 1 << shift };
    int dqv = 1 << QUANT_TABLE_BITS;
    int tqc = iter < 16000 ? iter : generate_random_q_idx(&rng_);

    // Initialize param structure.
    int bwl = 2 + (rng_.Rand8() & 3);
    int height = 1 << bwl;
    int diag = rng_.Rand8() & 3;
    int row = rng_.Rand8() % (diag + 1);
    int col = diag - row;
    int blk_pos = (row << bwl) + col;
    int scan_pos = blk_pos;
    int diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, TX_CLASS_2D);
    if (scan_pos > 0) {
      diag_ctx += 7 << 8;
    }

    blk_pos_ = blk_pos;
    diag_ctx_ = diag_ctx;
    coeff_sign_ = rng_.Rand8() & 1;
    param_.bwl = bwl;
    param_.txb_height = height;
    param_.tx_class = 0;
    param_.txb_costs = &txb_costs_;
    param_.tmp_sign = tmp_sign_;
    param_.dc_sign_ctx = rng_.Rand8() % DC_SIGN_CONTEXTS;
    tmp_sign_[blk_pos] = rng_.Rand8() % CROSS_COMPONENT_CONTEXTS;

    // Generate random syntax costs.
    generate_random_cost_tables(&rng_, &txb_costs_);

    // Generate pre_quant info with random coeff.
    av2_pre_quant_c(tqc, &pre_quant_, quant, dqv, log_scale, scan_pos);
    eob_rate_ = rng_(512 * 4);

    // Generate random coeff_ctx
    for (int i = 0; i < 8; i++) {
      coeff_ctx_.coef[i] = (rng_(4) << 4) + rng_(4);
    }
    coeff_ctx_.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    coeff_ctx_.pad[0] = 0;
    coeff_ctx_.pad[1] = 0;
    coeff_ctx_.pad[2] = 0;

    Common();
  }
}

class TcqRateLfLumaQ1Test : public FunctionEquivalenceTest<TcqRateLfLuma> {
 protected:
  static const int kIterations = 100000;

  void Execute(tcq_rate_t *rate_ref, tcq_rate_t *rate_tst) {
    memset(rate_ref, 0, sizeof(*rate_ref));
    memset(rate_tst, 0, sizeof(*rate_tst));
    params_.ref_func(&param_, &pre_quant_, &coeff_ctx_, blk_pos_, diag_ctx_,
                     eob_rate_, coeff_sign_, rate_ref);
    ASM_REGISTER_STATE_CHECK(params_.tst_func(&param_, &pre_quant_, &coeff_ctx_,
                                              blk_pos_, diag_ctx_, eob_rate_,
                                              coeff_sign_, rate_tst));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      int log_scale = 1;
      int shift = 16 - log_scale + QUANT_FP_BITS;
      const int32_t quant[2] = { 1 << shift, 1 << shift };
      int dqv = 1 << QUANT_TABLE_BITS;
      int tqc = 0;

      // Initialize param structure.
      int bwl = 2 + (rng_.Rand8() & 3);
      int height = 1 << bwl;
      int diag = rng_.Rand8() & 3;
      int row = rng_.Rand8() % (diag + 1);
      int col = diag - row;
      int blk_pos = (row << bwl) + col;
      int scan_pos = blk_pos;
      int diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, TX_CLASS_2D);
      if (scan_pos > 0) {
        diag_ctx += 7 << 8;
      }

      blk_pos_ = blk_pos;
      diag_ctx_ = diag_ctx;
      coeff_sign_ = rng_.Rand8() & 1;
      param_.bwl = bwl;
      param_.txb_height = height;
      param_.tx_class = 0;
      param_.txb_costs = &txb_costs_;
      param_.tmp_sign = tmp_sign_;
      param_.dc_sign_ctx = rng_.Rand8() % DC_SIGN_CONTEXTS;
      tmp_sign_[blk_pos] = rng_.Rand8() % CROSS_COMPONENT_CONTEXTS;

      // Generate random syntax costs.
      generate_random_cost_tables(&rng_, &txb_costs_);

      // Generate pre_quant info for q1.
      av2_pre_quant_q1(tqc, &pre_quant_, quant, dqv, log_scale, scan_pos);
      eob_rate_ = rng_(512 * 4);

      // Generate random coeff_ctx
      for (int i = 0; i < 8; i++) {
        coeff_ctx_.coef[i] = (rng_(4) << 4) + rng_(4);
      }
      coeff_ctx_.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
      coeff_ctx_.pad[0] = 0;
      coeff_ctx_.pad[1] = 0;
      coeff_ctx_.pad[2] = 0;

      tcq_rate_t rate_ref, rate_tst;
      Execute(&rate_ref, &rate_tst);

      for (int i = 0; i < 8; i++) {
        ASSERT_EQ(rate_ref.rate_zero[i], rate_tst.rate_zero[i])
            << "rate_zero mismatch at i=" << i;
      }
      ASSERT_EQ(rate_ref.rate_eob[1], rate_tst.rate_eob[1]);
      ASSERT_EQ(rate_ref.rate[1], rate_tst.rate[1]);
      ASSERT_EQ(rate_ref.rate[3], rate_tst.rate[3]);
      ASSERT_EQ(rate_ref.rate[4], rate_tst.rate[4]);
      ASSERT_EQ(rate_ref.rate[6], rate_tst.rate[6]);
      ASSERT_EQ(rate_ref.rate[9], rate_tst.rate[9]);
      ASSERT_EQ(rate_ref.rate[11], rate_tst.rate[11]);
      ASSERT_EQ(rate_ref.rate[12], rate_tst.rate[12]);
      ASSERT_EQ(rate_ref.rate[14], rate_tst.rate[14]);
    }
  }

  tcq_param_t param_;
  LV_MAP_COEFF_COST txb_costs_;
  prequant_t pre_quant_;
  tcq_coeff_ctx_t coeff_ctx_;
  int blk_pos_;
  int diag_ctx_;
  int eob_rate_;
  int coeff_sign_;
  int tmp_sign_[1024];
};

TEST_P(TcqRateLfLumaQ1Test, RandomValues) { RunTest(); }

typedef void (*TcqUpdateNbrDiagonalFunc)(struct tcq_ctx_t *tcq_ctx, int row,
                                         int col, int bwl);
typedef libavm_test::FuncParam<TcqUpdateNbrDiagonalFunc>
    TcqUpdateNbrDiagonalTestFuncs;

class TcqUpdateNbrDiagonalTest
    : public FunctionEquivalenceTest<TcqUpdateNbrDiagonalFunc> {
 protected:
  static const int kIterations = 10000;

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      int bwl = 2 + (rng_.Rand8() & 3);
      int max_diag = (1 << bwl) + (1 << bwl) - 2;
      int diag = rng_.Rand8() % (max_diag + 1);
      int row = rng_.Rand8() % (diag + 1);
      int col = diag - row;
      if (col >= (1 << bwl)) col = (1 << bwl) - 1;
      row = diag - col;
      if (row >= (1 << bwl)) row = (1 << bwl) - 1;

      tcq_ctx_t ctx_ref;
      tcq_ctx_t ctx_tst;
      memset(&ctx_ref, 0, sizeof(ctx_ref));

      for (int i = 0; i < TCQ_MAX_STATES; i++) {
        ctx_ref.orig_st[i] =
            (rng_.Rand8() % 3 == 0) ? -1 : (rng_.Rand8() % TCQ_MAX_STATES);
      }
      for (int i = 0; i < MAX_DIAG + 8; i++) {
        for (int st = 0; st < TCQ_MAX_STATES; st++) {
          ctx_ref.lev_new[i][st] = rng_.Rand8() % (MAX_VAL_BR_CTX + 1);
          ctx_ref.mag_base[i][st] = rng_.Rand8();
          ctx_ref.mag_mid[i][st] = rng_.Rand8();
          ctx_ref.ctx[i][st] = rng_.Rand8();
          ctx_ref.prev_st[i][st] =
              (rng_.Rand8() % 3 == 0) ? -1 : (rng_.Rand8() % TCQ_MAX_STATES);
        }
      }

      ctx_tst = ctx_ref;

      params_.ref_func(&ctx_ref, row, col, bwl);
      ASM_REGISTER_STATE_CHECK(params_.tst_func(&ctx_tst, row, col, bwl));
      int idx_start = col;
      int idx_end = AVMMIN(diag + 1, 1 << bwl);
      int idx0 = AVMMAX(idx_start - 2, 0);

      for (int i = idx0; i < idx_end; i++) {
        for (int st = 0; st < TCQ_MAX_STATES; st++) {
          ASSERT_EQ((int)ctx_ref.ctx[i][st], (int)ctx_tst.ctx[i][st])
              << "Mismatch in ctx at i=" << i << ", st=" << st
              << ", row=" << row << ", col=" << col << ", diag=" << diag
              << ", bwl=" << bwl;
          ASSERT_EQ((int)ctx_ref.mag_base[i][st], (int)ctx_tst.mag_base[i][st])
              << "Mismatch in mag_base at i=" << i << ", st=" << st
              << ", row=" << row << ", col=" << col << ", diag=" << diag
              << ", bwl=" << bwl;
          ASSERT_EQ((int)ctx_ref.mag_mid[i][st], (int)ctx_tst.mag_mid[i][st])
              << "Mismatch in mag_mid at i=" << i << ", st=" << st
              << ", row=" << row << ", col=" << col << ", diag=" << diag
              << ", bwl=" << bwl;
        }
      }
      for (int st = 0; st < TCQ_MAX_STATES; st++) {
        ASSERT_EQ((int)ctx_ref.orig_st[st], (int)ctx_tst.orig_st[st])
            << "Mismatch in orig_st at st=" << st;
      }
    }
  }
};

TEST_P(TcqUpdateNbrDiagonalTest, RandomValues) { RunTest(); }

typedef void (*PreQuantFunc)(tran_low_t tqc, struct prequant_t *pqData,
                             const int32_t *quant_ptr, int dqv, int log_scale,
                             int scan_pos);
typedef libavm_test::FuncParam<PreQuantFunc> PreQuantTestFuncs;

class PreQuantTest : public FunctionEquivalenceTest<PreQuantFunc> {
 protected:
  static const int kIterations = 100000;

  void Execute(prequant_t *ref, prequant_t *tst) {
    memset(ref, 0, sizeof(*ref));
    memset(tst, 0, sizeof(*tst));
    params_.ref_func(tqc_, ref, quant_, dqv_, log_scale_, scan_pos_);
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(tqc_, tst, quant_, dqv_, log_scale_, scan_pos_));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      log_scale_ = 1 + (rng_.Rand8() % 3);
      int shift = 16 - log_scale_ + QUANT_FP_BITS;
      quant_[0] = 1 << shift;
      quant_[1] = 1 << shift;
      dqv_ = rng_(1 << (QUANT_TABLE_BITS + 4));
      scan_pos_ = rng_.Rand8() % 64;
      tqc_ = (tran_low_t)(rng_.Rand16() - 32768);

      prequant_t ref, tst;
      Execute(&ref, &tst);

      ASSERT_EQ(ref.qIdx, tst.qIdx);
      for (int i = 0; i < 4; i++) {
        ASSERT_EQ(ref.absLevel[i], tst.absLevel[i]);
        ASSERT_EQ(ref.deltaDist[i], tst.deltaDist[i]);
      }
    }
  }

  tran_low_t tqc_;
  int32_t quant_[2];
  int dqv_;
  int log_scale_;
  int scan_pos_;
};

TEST_P(PreQuantTest, RandomValues) { RunTest(); }

class PreQuantQ1Test : public FunctionEquivalenceTest<PreQuantFunc> {
 protected:
  static const int kIterations = 100000;

  void Execute(prequant_t *ref, prequant_t *tst) {
    memset(ref, 0, sizeof(*ref));
    memset(tst, 0, sizeof(*tst));
    params_.ref_func(tqc_, ref, quant_, dqv_, log_scale_, scan_pos_);
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(tqc_, tst, quant_, dqv_, log_scale_, scan_pos_));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      log_scale_ = 1 + (rng_.Rand8() % 3);
      int shift = 16 - log_scale_ + QUANT_FP_BITS;
      quant_[0] = 1 << shift;
      quant_[1] = 1 << shift;
      dqv_ = rng_(1 << (QUANT_TABLE_BITS + 4));
      scan_pos_ = rng_.Rand8() % 64;
      tqc_ = (tran_low_t)(rng_.Rand16() - 32768);

      prequant_t ref, tst;
      Execute(&ref, &tst);

      ASSERT_EQ(ref.qIdx, tst.qIdx);
      ASSERT_EQ(ref.absLevel[1], tst.absLevel[1]);
      ASSERT_EQ(ref.absLevel[2], tst.absLevel[2]);
      ASSERT_EQ(ref.deltaDist[1], tst.deltaDist[1]);
      ASSERT_EQ(ref.deltaDist[2], tst.deltaDist[2]);
    }
  }

  tran_low_t tqc_;
  int32_t quant_[2];
  int dqv_;
  int log_scale_;
  int scan_pos_;
};

TEST_P(PreQuantQ1Test, RandomValues) { RunTest(); }

typedef void (*TcqDecideStatesFunc)(const struct tcq_node_t *prev,
                                    const struct tcq_rate_t *rd,
                                    const struct prequant_t *pq, int limits,
                                    int try_eob, int64_t rdmult,
                                    struct tcq_node_t *decision);
typedef libavm_test::FuncParam<TcqDecideStatesFunc> TcqDecideStatesTestFuncs;

class TcqDecideStatesTest
    : public FunctionEquivalenceTest<TcqDecideStatesFunc> {
 protected:
  static const int kIterations = 100000;

  void Execute(tcq_node_t *ref, tcq_node_t *tst) {
    memset(ref, 0, sizeof(tcq_node_t) * TCQ_N_STATES);
    memset(tst, 0, sizeof(tcq_node_t) * TCQ_N_STATES);
    params_.ref_func(prev_, &rd_, &pq_, limits_, try_eob_, rdmult_, ref);
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(prev_, &rd_, &pq_, limits_, try_eob_, rdmult_, tst));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      for (int i = 0; i < TCQ_N_STATES; i++) {
        prev_[i].rdCost = rng_(1 << 20);
        prev_[i].rate = rng_(1 << 16);
        prev_[i].absLevel = rng_.Rand8() % 16;
        prev_[i].prevId = rng_.Rand8() % 8;
      }
      for (int i = 0; i < 2 * TCQ_MAX_STATES; i++) {
        rd_.rate[i] = rng_(1 << 16);
      }
      for (int i = 0; i < TCQ_MAX_STATES; i++) {
        rd_.rate_zero[i] = rng_(1 << 16);
      }
      rd_.rate_eob[0] = rng_(1 << 16);
      rd_.rate_eob[1] = rng_(1 << 16);

      pq_.absLevel[0] = (rng_.Rand8() % 8) * 2;
      pq_.absLevel[1] = (rng_.Rand8() % 8) * 2 + 1;
      pq_.absLevel[2] = (rng_.Rand8() % 8) * 2 + 1;
      pq_.absLevel[3] = (rng_.Rand8() % 8) * 2;
      for (int i = 0; i < 4; i++) {
        pq_.deltaDist[i] = (int64_t)rng_(1 << 20);
      }
      pq_.qIdx = 1 + (rng_.Rand8() % 16);
      limits_ = 0;
      try_eob_ = rng_.Rand8() & 1;
      rdmult_ = rng_(1 << 16);

      tcq_node_t ref[TCQ_N_STATES], tst[TCQ_N_STATES];
      Execute(ref, tst);

      for (int i = 0; i < TCQ_N_STATES; i++) {
        ASSERT_EQ(ref[i].rdCost, tst[i].rdCost) << "rdCost mismatch at i=" << i;
        ASSERT_EQ(ref[i].rate, tst[i].rate) << "rate mismatch at i=" << i;
        ASSERT_EQ(ref[i].absLevel, tst[i].absLevel)
            << "absLevel mismatch at i=" << i;
        ASSERT_EQ(ref[i].prevId, tst[i].prevId) << "prevId mismatch at i=" << i;
      }
    }
  }

  tcq_node_t prev_[TCQ_N_STATES];
  tcq_rate_t rd_;
  prequant_t pq_;
  int limits_;
  int try_eob_;
  int64_t rdmult_;
};

TEST_P(TcqDecideStatesTest, RandomValues) { RunTest(); }

class TcqDecideStatesQ1Test
    : public FunctionEquivalenceTest<TcqDecideStatesFunc> {
 protected:
  static const int kIterations = 100000;

  void Execute(tcq_node_t *ref, tcq_node_t *tst) {
    memset(ref, 0, sizeof(tcq_node_t) * TCQ_N_STATES);
    memset(tst, 0, sizeof(tcq_node_t) * TCQ_N_STATES);
    params_.ref_func(prev_, &rd_, &pq_, limits_, try_eob_, rdmult_, ref);
    ASM_REGISTER_STATE_CHECK(
        params_.tst_func(prev_, &rd_, &pq_, limits_, try_eob_, rdmult_, tst));
  }

  void RunTest() {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      for (int i = 0; i < TCQ_N_STATES; i++) {
        prev_[i].rdCost = rng_(1 << 20);
        prev_[i].rate = rng_(1 << 16);
        prev_[i].absLevel = rng_.Rand8() % 16;
        prev_[i].prevId = rng_.Rand8() % 8;
      }
      for (int i = 0; i < 2 * TCQ_MAX_STATES; i++) {
        rd_.rate[i] = rng_(1 << 16);
      }
      for (int i = 0; i < TCQ_MAX_STATES; i++) {
        rd_.rate_zero[i] = rng_(1 << 16);
      }
      rd_.rate_eob[0] = rng_(1 << 16);
      rd_.rate_eob[1] = rng_(1 << 16);

      pq_.absLevel[0] = 0;
      pq_.absLevel[1] = 1;
      pq_.absLevel[2] = 1;
      pq_.absLevel[3] = 0;
      pq_.deltaDist[0] = (int64_t)rng_(1 << 20);
      pq_.deltaDist[1] = (int64_t)rng_(1 << 20);
      pq_.deltaDist[2] = (int64_t)rng_(1 << 20);
      pq_.deltaDist[3] = (int64_t)rng_(1 << 20);
      pq_.qIdx = 1;
      limits_ = 0;
      try_eob_ = rng_.Rand8() & 1;
      rdmult_ = rng_(1 << 16);

      tcq_node_t ref[TCQ_N_STATES], tst[TCQ_N_STATES];
      Execute(ref, tst);

      for (int i = 0; i < TCQ_N_STATES; i++) {
        ASSERT_EQ(ref[i].rdCost, tst[i].rdCost) << "rdCost mismatch at i=" << i;
        ASSERT_EQ(ref[i].rate, tst[i].rate) << "rate mismatch at i=" << i;
        ASSERT_EQ(ref[i].absLevel, tst[i].absLevel)
            << "absLevel mismatch at i=" << i;
        ASSERT_EQ(ref[i].prevId, tst[i].prevId) << "prevId mismatch at i=" << i;
      }
    }
  }

  tcq_node_t prev_[TCQ_N_STATES];
  tcq_rate_t rd_;
  prequant_t pq_;
  int limits_;
  int try_eob_;
  int64_t rdmult_;
};

TEST_P(TcqDecideStatesQ1Test, RandomValues) { RunTest(); }

typedef void (*TcqLoopDiagonalSt8Func)(const struct tcq_param_t *p, int scan_hi,
                                       int scan_lo, struct tcq_ctx_t *tcq_ctx,
                                       struct tcq_node_t *trellis);
typedef libavm_test::FuncParam<TcqLoopDiagonalSt8Func>
    TcqLoopDiagonalSt8TestFuncs;

class TcqLoopDiagonalSt8Test
    : public FunctionEquivalenceTest<TcqLoopDiagonalSt8Func> {
 protected:
  static const int kIterations = 10000;

  void InitParam(TX_SIZE tx_size, tcq_param_t *param,
                 LV_MAP_COEFF_COST *txb_costs, tran_low_t *tcoeff,
                 int32_t *tmp_sign, int32_t *quant, int32_t *dequant,
                 uint16_t *block_eob_rate) {
    const int bwl = get_txb_bwl(tx_size);
    const int height = get_txb_high(tx_size);
    const int width = 1 << bwl;
    const int num_coeffs = width * height;
    const int log_scale = av2_get_tx_scale(tx_size) + 1;
    const int shift = 16 - log_scale + QUANT_FP_BITS;

    const SCAN_ORDER *scan_order = get_scan(tx_size, DCT_DCT);
    param->plane = 0;
    param->bwl = bwl;
    param->txb_height = height;
    param->tx_size = tx_size;
    param->tx_class = TX_CLASS_2D;
    param->sharpness = rng_.Rand8() & 1;
    param->rdmult = rng_(1 << 16) + 100;
    param->log_scale = log_scale;
    param->dc_sign_ctx = rng_.Rand8() % DC_SIGN_CONTEXTS;
    param->scan = scan_order->scan;
    param->tmp_sign = tmp_sign;
    param->qcoeff = NULL;
    param->tcoeff = tcoeff;
    param->quant = quant;
    param->dequant = dequant;
    param->iqmatrix = NULL;
    param->block_eob_rate = block_eob_rate;
    param->txb_costs = txb_costs;

    quant[0] = 1 << shift;
    quant[1] = 1 << shift;
    dequant[0] = (1 << QUANT_TABLE_BITS) << (log_scale - 1);
    dequant[1] = (1 << QUANT_TABLE_BITS) << (log_scale - 1);

    generate_random_cost_tables(&rng_, txb_costs);

    for (int i = 0; i < num_coeffs; i++) {
      tcoeff[i] = (tran_low_t)((rng_.Rand16() % 2048) - 1024);
      tmp_sign[i] = rng_.Rand8() & 1;
    }
    for (int i = 0; i < MAX_TRELLIS; i++) {
      block_eob_rate[i] = rng_(512 * 4);
    }
  }

  void InitContextAndTrellis(const tcq_param_t *param, int first_scan_pos,
                             tcq_ctx_t *tcq_ctx, tcq_node_t *trellis) {
    int blk_pos = param->scan[first_scan_pos];
    TX_SIZE tx_size = param->tx_size;
    const int bwl = get_txb_bwl(tx_size);
    const int height = get_txb_high(tx_size);
    const int row = blk_pos >> bwl;
    const int col = blk_pos - (row << bwl);

    int diag = AVMMIN(row + col, MAX_DIAG) + 2;
    int ctx_array_size = diag << TCQ_N_STATES_LOG;

    static const int8_t init_st[4][TCQ_MAX_STATES] = {
      { 0, 1, 2, 3, 4, 5, 6, 7 },
      { 0, 1, 2, 3, 4, 5, 6, 7 },
      { 0, 1, 2, 3, 4, 5, 6, 7 },
      { 0, 1, 2, 3, 4, 5, 6, 7 },
    };

    memset(&tcq_ctx->mag_base, 0, ctx_array_size);
    memset(&tcq_ctx->mag_mid, 0, ctx_array_size);
    memset(&tcq_ctx->ctx, 0, ctx_array_size);
    memset(&tcq_ctx->lev_new, 0, ctx_array_size);

    for (int i = 0; i < diag; i += 4) {
      memcpy(tcq_ctx->prev_st[i], init_st, sizeof(init_st));
    }

    memset(trellis, 0, sizeof(tcq_node_t) * MAX_TRELLIS * TCQ_MAX_STATES);

    tcq_node_t *decision = &trellis[first_scan_pos << TCQ_N_STATES_LOG];
    static const tcq_node_t def = { INT64_MAX >> 10, 0, -1, -2 };
    for (int i = 0; i < TCQ_N_STATES; i++) {
      decision[i] = def;
    }
    decision[0].rdCost = rng_(1 << 20);
    decision[0].rate = rng_(1 << 16);
    decision[0].absLevel = rng_.Rand8() % 16;
    decision[0].prevId = -1;
    decision[4].rdCost = rng_(1 << 20);
    decision[4].rate = rng_(1 << 16);
    decision[4].absLevel = rng_.Rand8() % 16;
    decision[4].prevId = -1;

    for (int i = 0; i < TCQ_MAX_STATES; i++) {
      tcq_ctx->orig_st[i] = i;
      tcq_ctx->prev_st[col][i] = -1;
      tcq_ctx->lev_new[col][i] = 0;
    }
    tcq_ctx->lev_new[col][0] =
        AVMMIN(AVMMAX(0, decision[0].absLevel), MAX_VAL_BR_CTX);
    tcq_ctx->lev_new[col][4] =
        AVMMIN(AVMMAX(0, decision[4].absLevel), MAX_VAL_BR_CTX);

    if ((col == 0 && row != 0) || row == height - 1) {
      av2_update_nbr_diagonal_c(tcq_ctx, row, col, bwl);
    }
  }

  void RunEquivalenceTest() {
    static const TX_SIZE kTxSizes[] = { TX_4X4,   TX_8X8,   TX_16X16, TX_32X32,
                                        TX_4X8,   TX_8X4,   TX_8X16,  TX_16X8,
                                        TX_16X32, TX_32X16, TX_4X16,  TX_16X4,
                                        TX_8X32,  TX_32X8 };
    const int kNumTxSizes = sizeof(kTxSizes) / sizeof(kTxSizes[0]);

    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      TX_SIZE tx_size = kTxSizes[iter % kNumTxSizes];
      tcq_param_t param;
      LV_MAP_COEFF_COST txb_costs;
      tran_low_t tcoeff[MAX_TRELLIS];
      int32_t tmp_sign[MAX_TRELLIS];
      int32_t quant[2];
      int32_t dequant[2];
      uint16_t block_eob_rate[MAX_TRELLIS];

      InitParam(tx_size, &param, &txb_costs, tcoeff, tmp_sign, quant, dequant,
                block_eob_rate);

      const int width = 1 << param.bwl;
      const int height = param.txb_height;
      const int num_coeffs = width * height;
      const int max_diag = width + height - 2;

      // Select a diagonal (from 1 to max_diag) to start from.
      int start_diag = max_diag;
      if (iter % 2 != 0 && max_diag > 1) {
        start_diag = 1 + (rng_.Rand16() % max_diag);
      }

      // Find bottom-left scan pos of start_diag.
      int col_bot = AVMMAX(0, start_diag - (height - 1));
      int row_bot = start_diag - col_bot;
      int blk_pos_bot = (row_bot << param.bwl) + col_bot;
      int first_scan_pos = -1;
      for (int s = 0; s < num_coeffs; ++s) {
        if (param.scan[s] == blk_pos_bot) {
          first_scan_pos = s;
          break;
        }
      }
      ASSERT_GE(first_scan_pos, 0);

      tcq_ctx_t tcq_ctx_ref, tcq_ctx_tst;
      tcq_node_t trellis_ref[MAX_TRELLIS * TCQ_MAX_STATES];
      tcq_node_t trellis_tst[MAX_TRELLIS * TCQ_MAX_STATES];

      InitContextAndTrellis(&param, first_scan_pos, &tcq_ctx_ref, trellis_ref);
      tcq_ctx_tst = tcq_ctx_ref;
      memcpy(trellis_tst, trellis_ref, sizeof(trellis_ref));

      int scan_hi = first_scan_pos - 1;
      params_.ref_func(&param, scan_hi, 0, &tcq_ctx_ref, trellis_ref);
      ASM_REGISTER_STATE_CHECK(
          params_.tst_func(&param, scan_hi, 0, &tcq_ctx_tst, trellis_tst));

      const int64_t kUnreachable = (INT64_MAX >> 12);
      for (int scan_pos = 0; scan_pos <= scan_hi; ++scan_pos) {
        for (int st = 0; st < TCQ_N_STATES; ++st) {
          int idx = (scan_pos << TCQ_N_STATES_LOG) + st;
          if ((trellis_ref[idx].rdCost >= 0 &&
               trellis_ref[idx].rdCost < kUnreachable) ||
              (trellis_tst[idx].rdCost >= 0 &&
               trellis_tst[idx].rdCost < kUnreachable)) {
            ASSERT_EQ(trellis_ref[idx].rdCost, trellis_tst[idx].rdCost)
                << "rdCost mismatch at scan_pos=" << scan_pos << " st=" << st
                << " tx_size=" << tx_size;
            ASSERT_EQ(trellis_ref[idx].rate, trellis_tst[idx].rate)
                << "rate mismatch at scan_pos=" << scan_pos << " st=" << st
                << " tx_size=" << tx_size;
            ASSERT_EQ(trellis_ref[idx].absLevel, trellis_tst[idx].absLevel)
                << "absLevel mismatch at scan_pos=" << scan_pos << " st=" << st
                << " tx_size=" << tx_size;
            ASSERT_EQ(trellis_ref[idx].prevId, trellis_tst[idx].prevId)
                << "prevId mismatch at scan_pos=" << scan_pos << " st=" << st
                << " tx_size=" << tx_size;
          }
        }
      }

      for (int st = 0; st < TCQ_MAX_STATES; ++st) {
        if ((trellis_ref[st].rdCost >= 0 &&
             trellis_ref[st].rdCost < kUnreachable) ||
            (trellis_tst[st].rdCost >= 0 &&
             trellis_tst[st].rdCost < kUnreachable)) {
          ASSERT_EQ((int)tcq_ctx_ref.orig_st[st], (int)tcq_ctx_tst.orig_st[st])
              << "orig_st mismatch at iter=" << iter << " st=" << st
              << " tx_size=" << tx_size;
          ASSERT_EQ((int)tcq_ctx_ref.lev_new[0][st],
                    (int)tcq_ctx_tst.lev_new[0][st])
              << "lev_new[0] mismatch at iter=" << iter << " st=" << st
              << " tx_size=" << tx_size;
          ASSERT_EQ((int)tcq_ctx_ref.prev_st[0][st],
                    (int)tcq_ctx_tst.prev_st[0][st])
              << "prev_st[0] mismatch at iter=" << iter << " st=" << st
              << " tx_size=" << tx_size;
        }
      }

      tran_low_t qcoeff_ref[MAX_TRELLIS] = { 0 },
                 qcoeff_tst[MAX_TRELLIS] = { 0 };
      tran_low_t dqcoeff_ref[MAX_TRELLIS] = { 0 },
                 dqcoeff_tst[MAX_TRELLIS] = { 0 };
      int rate_ref = 0, rate_tst = 0;
      int64_t cost_ref = INT64_MAX, cost_tst = INT64_MAX;
      int eob_ref = av2_find_best_path(
          trellis_ref, param.scan, param.dequant, param.iqmatrix, param.tcoeff,
          first_scan_pos, param.log_scale, qcoeff_ref, dqcoeff_ref, &rate_ref,
          &cost_ref);
      int eob_tst = av2_find_best_path(
          trellis_tst, param.scan, param.dequant, param.iqmatrix, param.tcoeff,
          first_scan_pos, param.log_scale, qcoeff_tst, dqcoeff_tst, &rate_tst,
          &cost_tst);
      ASSERT_EQ(eob_ref, eob_tst);
      ASSERT_EQ(rate_ref, rate_tst);
      ASSERT_EQ(cost_ref, cost_tst);
      for (int i = 0; i < num_coeffs; ++i) {
        ASSERT_EQ(qcoeff_ref[i], qcoeff_tst[i]);
        ASSERT_EQ(dqcoeff_ref[i], dqcoeff_tst[i]);
      }
    }
  }

#if HAVE_AVX2
  static AVM_FORCE_INLINE int get_diag_ctx_bench(int lf, int blk_pos,
                                                 int scan_pos, int bwl) {
    int diag_ctx;
    if (lf) {
      diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, TX_CLASS_2D);
      if (scan_pos > 0) diag_ctx += 7 << 8;
    } else {
      diag_ctx = get_nz_map_ctx_from_stats(0, blk_pos, bwl, TX_CLASS_2D, 0);
    }
    return diag_ctx;
  }

  static AVM_FORCE_INLINE int get_dqv_bench(const int32_t *dequant,
                                            int coeff_idx,
                                            const qm_val_t *iqmatrix) {
    int dqv = dequant[coeff_idx != 0];
    if (iqmatrix != NULL) {
      dqv =
          (dqv * iqmatrix[coeff_idx] + (1 << (AVM_QM_BITS - 1))) >> AVM_QM_BITS;
    }
    return dqv;
  }

  static void trellis_loop_diagonal_st8_prepatch_baseline_avx2(
      const tcq_param_t *p, int scan_hi, int scan_lo, tcq_ctx_t *tcq_ctx,
      tcq_node_t *trellis) {
    int log_scale = p->log_scale;
    int try_eob = p->sharpness == 0;
    int64_t rdmult = p->rdmult;
    const int16_t *scan = p->scan;
    const tran_low_t *tcoeff = p->tcoeff;
    const int32_t *quant = p->quant;
    const int32_t *dequant = p->dequant;
    const qm_val_t *iqmatrix = p->iqmatrix;
    const uint16_t *block_eob_rate = p->block_eob_rate;
    int bwl = p->bwl;
    int height = p->txb_height;
    int dc_coeff_sign = tcoeff[0] < 0;
    int blk_pos_inc = (1 << bwl) - 1;
    int blk_pos, row, col;
    int shift = 16 - log_scale + QUANT_FP_BITS;

    while (scan_hi >= 10) {
      blk_pos = scan[scan_hi];
      row = blk_pos >> bwl;
      col = blk_pos - (row << bwl);
      int inc = AVMMIN(height - 1 - row, col);
      scan_lo = scan_hi - inc;
      int lf = 0;
      int diag_ctx = get_diag_ctx_bench(lf, blk_pos, scan_lo, bwl);

      for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
        tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
        tcq_node_t *prev_decision = &decision[TCQ_N_STATES];
        prequant_t pqData;
        int tempdqv = get_dqv_bench(dequant, scan[scan_pos], iqmatrix);
        tran_low_t orig_qIdx = (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) *
                                             quant[scan_pos != 0]) >>
                                            shift);
        pqData.orig_qIdx = orig_qIdx;

        tcq_coeff_ctx_t coeff_ctx;
        av2_get_coeff_ctx_avx2(tcq_ctx, col, &coeff_ctx);
        coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
        int eob_rate = block_eob_rate[scan_pos];
        tcq_rate_t rd;

        if (pqData.orig_qIdx < 2) {
          av2_pre_quant_q1_avx2(tcoeff[blk_pos], &pqData, quant, tempdqv,
                                log_scale, scan_pos);
          av2_get_rate_dist_def_luma_q1_avx2(p, &pqData, &coeff_ctx, blk_pos,
                                             diag_ctx, eob_rate, &rd);
          av2_decide_states_q1_avx2(prev_decision, &rd, &pqData, lf, try_eob,
                                    rdmult, decision);
        } else {
          av2_pre_quant_avx2(tcoeff[blk_pos], &pqData, quant, tempdqv,
                             log_scale, scan_pos);
          av2_get_rate_dist_def_luma_avx2(p, &pqData, &coeff_ctx, blk_pos,
                                          diag_ctx, eob_rate, &rd);
          av2_decide_states_avx2(prev_decision, &rd, &pqData, lf, try_eob,
                                 rdmult, decision);
        }
        av2_update_states_avx2(decision, col, tcq_ctx);
        blk_pos += blk_pos_inc;
        col--;
        row++;
      }
      av2_update_nbr_diagonal_avx2(tcq_ctx, row - 1, col + 1, bwl);
      scan_hi = scan_lo - 1;
    }
    while (scan_hi >= 0) {
      blk_pos = scan[scan_hi];
      row = blk_pos >> bwl;
      col = blk_pos - (row << bwl);
      int inc = AVMMIN(height - 1 - row, col);
      scan_lo = scan_hi - inc;
      int lf = 1;
      int diag_ctx = get_diag_ctx_bench(lf, blk_pos, scan_lo, bwl);

      for (int scan_pos = scan_hi; scan_pos >= scan_lo; scan_pos--) {
        tcq_node_t *decision = &trellis[scan_pos << TCQ_N_STATES_LOG];
        tcq_node_t *prev_decision = &decision[TCQ_N_STATES];
        prequant_t pqData;
        int tempdqv = get_dqv_bench(dequant, scan[scan_pos], iqmatrix);
        tran_low_t orig_qIdx = (tran_low_t)(((int64_t)abs(tcoeff[blk_pos]) *
                                             quant[scan_pos != 0]) >>
                                            shift);
        pqData.orig_qIdx = orig_qIdx;

        tcq_coeff_ctx_t coeff_ctx;
        av2_get_coeff_ctx_avx2(tcq_ctx, col, &coeff_ctx);
        coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
        int eob_rate = block_eob_rate[scan_pos];
        tcq_rate_t rd;

        if (pqData.orig_qIdx < 2) {
          av2_pre_quant_q1_avx2(tcoeff[blk_pos], &pqData, quant, tempdqv,
                                log_scale, scan_pos);
          av2_get_rate_dist_lf_luma_q1_avx2(p, &pqData, &coeff_ctx, blk_pos,
                                            diag_ctx, eob_rate, dc_coeff_sign,
                                            &rd);
          av2_decide_states_q1_avx2(prev_decision, &rd, &pqData, lf, try_eob,
                                    rdmult, decision);
        } else {
          av2_pre_quant_avx2(tcoeff[blk_pos], &pqData, quant, tempdqv,
                             log_scale, scan_pos);
          av2_get_rate_dist_lf_luma_avx2(p, &pqData, &coeff_ctx, blk_pos,
                                         diag_ctx, eob_rate, dc_coeff_sign,
                                         &rd);
          av2_decide_states_avx2(prev_decision, &rd, &pqData, lf, try_eob,
                                 rdmult, decision);
        }
        av2_update_states_avx2(decision, col, tcq_ctx);
        blk_pos += blk_pos_inc;
        col--;
        row++;
      }
      if (scan_hi != 0) {
        av2_update_nbr_diagonal_avx2(tcq_ctx, row - 1, col + 1, bwl);
      }
      scan_hi = scan_lo - 1;
    }
  }
#endif  // HAVE_AVX2

  void RunSpeedTest() {
    static const TX_SIZE kTxSizes[] = { TX_4X4,   TX_8X8,   TX_16X16, TX_32X32,
                                        TX_4X8,   TX_8X4,   TX_8X16,  TX_16X8,
                                        TX_16X32, TX_32X16, TX_4X16,  TX_16X4,
                                        TX_8X32,  TX_32X8 };
    const int kNumTxSizes = sizeof(kTxSizes) / sizeof(kTxSizes[0]);

    printf(
        "\n============================ TCQ Diagonal Loop: Direct Speed "
        "Benchmark ============================\n");
    printf("%-8s %-6s %-12s %-15s %-15s %-16s %-16s\n", "TX Size", "Coeffs",
           "Pure C (us)", "Base AVX2 (us)", "Patch AVX2 (us)", "vs Base AVX2",
           "vs Pure C");
    printf(
        "----------------------------------------------------------------------"
        "------------------------------\n");

    for (int txi = 0; txi < kNumTxSizes; ++txi) {
      TX_SIZE tx_size = kTxSizes[txi];
      const int width = 1 << get_txb_bwl(tx_size);
      const int height = get_txb_high(tx_size);
      const int num_coeffs = width * height;
      const int kNumTests = (num_coeffs <= 64)    ? 20000
                            : (num_coeffs <= 256) ? 5000
                                                  : 1000;

      tcq_param_t param;
      LV_MAP_COEFF_COST txb_costs;
      tran_low_t tcoeff[MAX_TRELLIS];
      int32_t tmp_sign[MAX_TRELLIS];
      int32_t quant[2];
      int32_t dequant[2];
      uint16_t block_eob_rate[MAX_TRELLIS];

      InitParam(tx_size, &param, &txb_costs, tcoeff, tmp_sign, quant, dequant,
                block_eob_rate);

      tcq_ctx_t tcq_ctx_c, tcq_ctx_opt;
      tcq_node_t trellis_c[MAX_TRELLIS * TCQ_MAX_STATES];
      tcq_node_t trellis_opt[MAX_TRELLIS * TCQ_MAX_STATES];
#if HAVE_AVX2
      tcq_ctx_t tcq_ctx_base;
      tcq_node_t trellis_base[MAX_TRELLIS * TCQ_MAX_STATES];
#endif

      int first_scan_pos = num_coeffs - 1;
      int scan_hi = first_scan_pos - 1;

      avm_usec_timer timer_c;
      avm_usec_timer_start(&timer_c);
      for (int i = 0; i < kNumTests; ++i) {
        InitContextAndTrellis(&param, first_scan_pos, &tcq_ctx_c, trellis_c);
        params_.ref_func(&param, scan_hi, 0, &tcq_ctx_c, trellis_c);
      }
      avm_usec_timer_mark(&timer_c);
      const int64_t elapsed_time_c = avm_usec_timer_elapsed(&timer_c);

#if HAVE_AVX2
      avm_usec_timer timer_base;
      avm_usec_timer_start(&timer_base);
      for (int i = 0; i < kNumTests; ++i) {
        InitContextAndTrellis(&param, first_scan_pos, &tcq_ctx_base,
                              trellis_base);
        trellis_loop_diagonal_st8_prepatch_baseline_avx2(
            &param, scan_hi, 0, &tcq_ctx_base, trellis_base);
      }
      avm_usec_timer_mark(&timer_base);
      const int64_t elapsed_time_base = avm_usec_timer_elapsed(&timer_base);
#else
      const int64_t elapsed_time_base = elapsed_time_c;
#endif

      avm_usec_timer timer_opt;
      avm_usec_timer_start(&timer_opt);
      for (int i = 0; i < kNumTests; ++i) {
        InitContextAndTrellis(&param, first_scan_pos, &tcq_ctx_opt,
                              trellis_opt);
        params_.tst_func(&param, scan_hi, 0, &tcq_ctx_opt, trellis_opt);
      }
      avm_usec_timer_mark(&timer_opt);
      const int64_t elapsed_time_opt = avm_usec_timer_elapsed(&timer_opt);

      const double gain_vs_base =
          (double)elapsed_time_base / (double)elapsed_time_opt;
      const double speedup_vs_base =
          (1.0 - (double)elapsed_time_opt / (double)elapsed_time_base) * 100.0;

      const double gain_vs_c =
          (double)elapsed_time_c / (double)elapsed_time_opt;
      const double speedup_vs_c =
          (1.0 - (double)elapsed_time_opt / (double)elapsed_time_c) * 100.0;

      char size_str[32];
      snprintf(size_str, sizeof(size_str), "%dx%d", width, height);
      char vs_base_str[32];
      snprintf(vs_base_str, sizeof(vs_base_str), "%.2fx (+%.1f%%)",
               gain_vs_base, speedup_vs_base);
      char vs_c_str[32];
      snprintf(vs_c_str, sizeof(vs_c_str), "%.2fx (+%.1f%%)", gain_vs_c,
               speedup_vs_c);

      printf("%-8s %-6d %-12ld %-15ld %-15ld %-16s %-16s\n", size_str,
             num_coeffs, (long)elapsed_time_c, (long)elapsed_time_base,
             (long)elapsed_time_opt, vs_base_str, vs_c_str);
    }
    printf(
        "======================================================================"
        "==============================\n\n");
  }
};

TEST_P(TcqLoopDiagonalSt8Test, RandomValues) { RunEquivalenceTest(); }
TEST_P(TcqLoopDiagonalSt8Test, Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, TcqDecideStatesTest,
                         ::testing::Values(TcqDecideStatesTestFuncs(
                             av2_decide_states_c, av2_decide_states_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqDecideStatesQ1Test,
    ::testing::Values(TcqDecideStatesTestFuncs(av2_decide_states_q1_c,
                                               av2_decide_states_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, PreQuantTest,
    ::testing::Values(PreQuantTestFuncs(av2_pre_quant_c, av2_pre_quant_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, PreQuantQ1Test,
                         ::testing::Values(PreQuantTestFuncs(
                             av2_pre_quant_q1_c, av2_pre_quant_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLumaTest,
    ::testing::Values(TcqRateLumaTestFuncs(av2_get_rate_dist_def_luma_c,
                                           av2_get_rate_dist_def_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqRateLumaQ1Test,
                         ::testing::Values(TcqRateLumaTestFuncs(
                             av2_get_rate_dist_def_luma_q1_c,
                             av2_get_rate_dist_def_luma_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLfLumaTest,
    ::testing::Values(TcqRateLfLumaTestFuncs(av2_get_rate_dist_lf_luma_c,
                                             av2_get_rate_dist_lf_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqRateLfLumaQ1Test,
                         ::testing::Values(TcqRateLfLumaTestFuncs(
                             av2_get_rate_dist_lf_luma_q1_c,
                             av2_get_rate_dist_lf_luma_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqUpdateNbrDiagonalTest,
                         ::testing::Values(TcqUpdateNbrDiagonalTestFuncs(
                             av2_update_nbr_diagonal_c,
                             av2_update_nbr_diagonal_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqLoopDiagonalSt8Test,
                         ::testing::Values(TcqLoopDiagonalSt8TestFuncs(
                             av2_trellis_loop_diagonal_st8_c,
                             av2_trellis_loop_diagonal_st8_avx2)));
#endif  // HAVE_AVX2

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqDecideStatesTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqDecideStatesQ1Test);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PreQuantTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PreQuantQ1Test);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLumaTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLumaQ1Test);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLfLumaTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLfLumaQ1Test);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqUpdateNbrDiagonalTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqLoopDiagonalSt8Test);

}  // namespace
