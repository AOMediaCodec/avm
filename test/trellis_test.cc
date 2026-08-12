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

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "avm/avm_integer.h"
#include "av2/common/enums.h"
#include "av2/encoder/trellis_quant.h"

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

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqDecideStatesTest,
    ::testing::Values(TcqDecideStatesTestFuncs(av2_decide_states_c,
                                               av2_decide_states_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqDecideStatesQ1Test,
    ::testing::Values(TcqDecideStatesTestFuncs(av2_decide_states_q1_c,
                                               av2_decide_states_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, PreQuantTest,
    ::testing::Values(PreQuantTestFuncs(av2_pre_quant_c, av2_pre_quant_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, PreQuantQ1Test,
    ::testing::Values(PreQuantTestFuncs(av2_pre_quant_q1_c, av2_pre_quant_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLumaTest,
    ::testing::Values(TcqRateLumaTestFuncs(av2_get_rate_dist_def_luma_c,
                                           av2_get_rate_dist_def_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLumaQ1Test,
    ::testing::Values(TcqRateLumaTestFuncs(av2_get_rate_dist_def_luma_q1_c,
                                           av2_get_rate_dist_def_luma_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLfLumaTest,
    ::testing::Values(TcqRateLfLumaTestFuncs(av2_get_rate_dist_lf_luma_c,
                                             av2_get_rate_dist_lf_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLfLumaQ1Test,
    ::testing::Values(TcqRateLfLumaTestFuncs(av2_get_rate_dist_lf_luma_q1_c,
                                             av2_get_rate_dist_lf_luma_q1_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqUpdateNbrDiagonalTest,
                         ::testing::Values(TcqUpdateNbrDiagonalTestFuncs(
                             av2_update_nbr_diagonal_c,
                             av2_update_nbr_diagonal_avx2)));
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

}  // namespace
