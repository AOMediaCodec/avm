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

#include <memory>
#include <sstream>
#include <string>
#include <tuple>

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
#include "av2/common/common_data.h"
#include "av2/common/entropy.h"
#include "av2/encoder/encodetxb.h"
#include "av2/encoder/rd.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/avm_timer.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"

// Defined in trellis_quant.c, which has no header declaration for it.
extern "C" void trellis_first_pos(const tcq_param_t *p, int scan_pos,
                                  tcq_ctx_t *tcq_ctx, tcq_node_t *trellis);

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

// Nodes at or above this cost descend from the unreachable-state sentinel.
static const int64_t kTcqUnreachableCost = INT64_MAX >> 12;

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
                                        LV_MAP_COEFF_COST *txb_costs,
                                        int cost_mask = 2048 - 1) {
  const int max = cost_mask;
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

// Frozen-reference tests for the q1 rate C optimization.
// The C functions rate_def_luma_q1_c and rate_lf_luma_q1_c were rewritten from
// calling get_coeff_cost_def/get_coeff_cost (generic paths) to reading
// pre-packed tables directly. These tests verify the pre-packed table path
// against the original generic-path semantics frozen here as reference logic.
// If the generic cost functions are deprecated, these frozen references
// preserve the correctness oracle for the q1 specialization.

TEST(TcqRateDefLumaQ1FrozenRef, MatchesGenericPath) {
  const int kIterations = 100000;
  libavm_test::ACMRandom rng(libavm_test::ACMRandom::DeterministicSeed());
  LV_MAP_COEFF_COST txb_costs;
  tcq_param_t param;
  prequant_t pq;
  tcq_coeff_ctx_t coeff_ctx;

  for (int iter = 0; iter < kIterations; ++iter) {
    int log_scale = 1;
    int shift = 16 - log_scale + QUANT_FP_BITS;
    const int32_t quant[2] = { 1 << shift, 1 << shift };
    int dqv = 1 << QUANT_TABLE_BITS;

    int bwl = 3 + (rng.Rand8() & 2);
    int max = (1 << bwl) - 1;
    int r0 = (int)(rng.Rand8() & max);
    int c0 = (int)(rng.Rand8() & max);
    int row = AVMMAX(r0, 4);
    int col = AVMMAX(c0, 4);
    int blk_pos = (row << bwl) + col;
    int scan_pos = blk_pos;
    int diag_ctx = get_nz_map_ctx_from_stats(0, blk_pos, bwl, TX_CLASS_2D, 0);

    param.bwl = bwl;
    param.txb_height = 1 << bwl;
    param.tx_class = (TX_CLASS)0;
    param.txb_costs = &txb_costs;

    generate_random_cost_tables(&rng, &txb_costs);
    av2_pre_quant_q1(0, &pq, quant, dqv, log_scale, scan_pos);
    int eob_rate = rng(512 * 4);

    for (int i = 0; i < 8; i++) {
      coeff_ctx.coef[i] = (rng(4) << 4) + rng(4);
    }
    coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, 1 << bwl, scan_pos);
    coeff_ctx.pad[0] = coeff_ctx.pad[1] = coeff_ctx.pad[2] = 0;

    tcq_rate_t rd;
    memset(&rd, 0, sizeof(rd));
    av2_get_rate_dist_def_luma_q1_c(&param, &pq, &coeff_ctx, blk_pos, diag_ctx,
                                    eob_rate, &rd);

    int base_diag_ctx = diag_ctx & 255;
    for (int i = 0; i < TCQ_N_STATES; i++) {
      int q_i = (i & 2) ? 1 : 0;
      int ctx = base_diag_ctx + (coeff_ctx.coef[i] & 0xF);
      int exp_zero = txb_costs.base_cost[ctx][q_i][0];
      ASSERT_EQ(rd.rate_zero[i], exp_zero)
          << "rate_zero[" << i << "] iter=" << iter;
      if (q_i == 1) {
        int exp = txb_costs.base_cost[ctx][1][1] + av2_cost_literal(1);
        ASSERT_EQ(rd.rate[2 * i], exp) << "rate[" << 2 * i << "] iter=" << iter;
      } else {
        int exp = txb_costs.base_cost[ctx][0][1] + av2_cost_literal(1);
        ASSERT_EQ(rd.rate[2 * i + 1], exp)
            << "rate[" << (2 * i + 1) << "] iter=" << iter;
      }
    }
    int eob_ctx = coeff_ctx.coef_eob;
    int exp_eob =
        eob_rate + txb_costs.base_eob_cost[eob_ctx][0] + av2_cost_literal(1);
    ASSERT_EQ(rd.rate_eob[1], exp_eob) << "rate_eob[1] iter=" << iter;
  }
}

TEST(TcqRateLfLumaQ1FrozenRef, MatchesGenericPath) {
  const int kIterations = 100000;
  libavm_test::ACMRandom rng(libavm_test::ACMRandom::DeterministicSeed());
  LV_MAP_COEFF_COST txb_costs;
  tcq_param_t param;
  prequant_t pq;
  tcq_coeff_ctx_t coeff_ctx;
  int32_t tmp_sign[1024];

  for (int iter = 0; iter < kIterations; ++iter) {
    int log_scale = 1;
    int shift = 16 - log_scale + QUANT_FP_BITS;
    const int32_t quant[2] = { 1 << shift, 1 << shift };
    int dqv = 1 << QUANT_TABLE_BITS;

    int bwl = 2 + (rng.Rand8() & 3);
    int height = 1 << bwl;
    int diag = rng.Rand8() & 3;
    int row = rng.Rand8() % (diag + 1);
    int col = diag - row;
    int blk_pos = (row << bwl) + col;
    int scan_pos = blk_pos;
    int diag_ctx = get_nz_map_ctx_from_stats_lf(0, blk_pos, bwl, TX_CLASS_2D);
    if (scan_pos > 0) diag_ctx += 7 << 8;

    int coeff_sign = rng.Rand8() & 1;
    param.bwl = bwl;
    param.txb_height = height;
    param.tx_class = (TX_CLASS)0;
    param.txb_costs = &txb_costs;
    param.tmp_sign = tmp_sign;
    param.dc_sign_ctx = rng.Rand8() % DC_SIGN_CONTEXTS;
    tmp_sign[blk_pos] = rng.Rand8() % CROSS_COMPONENT_CONTEXTS;

    generate_random_cost_tables(&rng, &txb_costs);
    av2_pre_quant_q1(0, &pq, quant, dqv, log_scale, scan_pos);
    int eob_rate = rng(512 * 4);

    for (int i = 0; i < 8; i++) {
      coeff_ctx.coef[i] = (rng(4) << 4) + rng(4);
    }
    coeff_ctx.coef_eob = get_lower_levels_ctx_eob(bwl, height, scan_pos);
    coeff_ctx.pad[0] = coeff_ctx.pad[1] = coeff_ctx.pad[2] = 0;

    tcq_rate_t rd;
    memset(&rd, 0, sizeof(rd));
    av2_get_rate_dist_lf_luma_q1_c(&param, &pq, &coeff_ctx, blk_pos, diag_ctx,
                                   eob_rate, coeff_sign, &rd);

    int base_diag_ctx = diag_ctx & 255;
    int is_dc = (blk_pos == 0);
    int dc_cost_add = 0;
    if (is_dc) {
      dc_cost_add = txb_costs.dc_sign_cost[0][param.dc_sign_ctx][coeff_sign] -
                    av2_cost_literal(1);
    }

    for (int i = 0; i < TCQ_N_STATES; i++) {
      int q_i = (i & 2) ? 1 : 0;
      int ctx = base_diag_ctx + (coeff_ctx.coef[i] & 0xF);
      int exp_zero = txb_costs.base_lf_cost[ctx][q_i][0];
      ASSERT_EQ(rd.rate_zero[i], exp_zero)
          << "rate_zero[" << i << "] iter=" << iter;
      if (q_i == 1) {
        int exp = txb_costs.base_lf_cost[ctx][1][1] + av2_cost_literal(1) +
                  dc_cost_add;
        ASSERT_EQ(rd.rate[2 * i], exp) << "rate[" << 2 * i << "] iter=" << iter;
      } else {
        int exp = txb_costs.base_lf_cost[ctx][0][1] + av2_cost_literal(1) +
                  dc_cost_add;
        ASSERT_EQ(rd.rate[2 * i + 1], exp)
            << "rate[" << (2 * i + 1) << "] iter=" << iter;
      }
    }
    int eob_ctx = coeff_ctx.coef_eob;
    int exp_eob =
        eob_rate + txb_costs.base_lf_eob_cost[eob_ctx][0] + av2_cost_literal(1);
    if (is_dc) exp_eob += dc_cost_add;
    ASSERT_EQ(rd.rate_eob[1], exp_eob) << "rate_eob[1] iter=" << iter;
  }
}

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

  // With tied_costs, tiny rates, costs and distances make zero-vs-level-1
  // rdcost ties common, so the tie-break order (zero wins) is checked. Random
  // 30-bit values almost never tie.
  void RunTest(bool tied_costs) {
    const int rate_range = tied_costs ? 3 : (1 << 30);
    const int cost_range = tied_costs ? 3 : (1 << 20);
    const int dist_range = tied_costs ? 3 : (1 << 20);
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      for (int i = 0; i < TCQ_N_STATES; i++) {
        prev_[i].rdCost = rng_(cost_range);
        prev_[i].rate = rng_(rate_range);
        prev_[i].absLevel = rng_.Rand8() % 16;
        prev_[i].prevId = rng_.Rand8() % 8;
      }
      for (int i = 0; i < 2 * TCQ_MAX_STATES; i++) {
        rd_.rate[i] = rng_(rate_range);
      }
      for (int i = 0; i < TCQ_MAX_STATES; i++) {
        rd_.rate_zero[i] = rng_(rate_range);
      }
      rd_.rate_eob[0] = rng_(rate_range);
      rd_.rate_eob[1] = rng_(rate_range);

      pq_.absLevel[0] = 0;
      pq_.absLevel[1] = 1;
      pq_.absLevel[2] = 1;
      pq_.absLevel[3] = 0;
      for (int i = 0; i < 4; i++) {
        pq_.deltaDist[i] = (int64_t)rng_(dist_range) - dist_range / 2;
      }
      pq_.qIdx = 1;
      limits_ = 0;
      try_eob_ = rng_.Rand8() & 1;
      rdmult_ = tied_costs ? 1 + rng_(2) : rng_(1 << 30);

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

TEST_P(TcqDecideStatesQ1Test, RandomValues) { RunTest(false); }

TEST_P(TcqDecideStatesQ1Test, TiedCosts) { RunTest(true); }

// Rows of 8 as in quant_fp_QTX / dequant_QTX: DC, then AC in [1..7].
static void FillQuantRow8(int32_t *quant, int32_t *dequant) {
  for (int i = 2; i < 8; i++) {
    quant[i] = quant[1];
    dequant[i] = dequant[1];
  }
}

static void InitTcqCtxDiagonal(tcq_ctx_t *tcq_ctx, int blk_pos, int bwl) {
  const int row = blk_pos >> bwl;
  const int col = blk_pos - (row << bwl);
  const int diag = AVMMIN(row + col, MAX_DIAG) + 2;
  const int ctx_array_size = diag << TCQ_N_STATES_LOG;
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
}

static void CompareBestPath(const tcq_node_t *trellis_ref,
                            const tcq_node_t *trellis_tst, const int16_t *scan,
                            const int32_t *dequant, const qm_val_t *iqmatrix,
                            const tran_low_t *tcoeff, int first_scan_pos,
                            int log_scale, int bwl, int num_coeffs,
                            const std::string &ctx) {
  tran_low_t qc_ref[MAX_TRELLIS] = { 0 }, qc_tst[MAX_TRELLIS] = { 0 };
  tran_low_t dq_ref[MAX_TRELLIS] = { 0 }, dq_tst[MAX_TRELLIS] = { 0 };
  int rate_ref = 0, rate_tst = 0;
  int64_t cost_ref = INT64_MAX, cost_tst = INT64_MAX;
  const int eob_ref = av2_find_best_path(trellis_ref, scan, dequant, iqmatrix,
                                         tcoeff, first_scan_pos, log_scale,
                                         qc_ref, dq_ref, &rate_ref, &cost_ref);
  const int eob_tst = av2_find_best_path(trellis_tst, scan, dequant, iqmatrix,
                                         tcoeff, first_scan_pos, log_scale,
                                         qc_tst, dq_tst, &rate_tst, &cost_tst);
  ASSERT_EQ(eob_ref, eob_tst) << "best-path eob " << ctx;
  ASSERT_EQ(rate_ref, rate_tst) << "best-path rate " << ctx;
  ASSERT_EQ(cost_ref, cost_tst) << "best-path cost " << ctx;
  for (int i = 0; i < num_coeffs; ++i) {
    const int r = i >> bwl, c = i - (r << bwl);
    ASSERT_EQ(qc_ref[i], qc_tst[i])
        << "qcoeff mismatch at r=" << r << " c=" << c << " " << ctx;
    ASSERT_EQ(dq_ref[i], dq_tst[i])
        << "dqcoeff mismatch at r=" << r << " c=" << c << " " << ctx;
  }
}

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
                 uint16_t *block_eob_rate, qm_val_t *iqmatrix = NULL) {
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
    param->block_eob_rate = block_eob_rate;
    param->txb_costs = txb_costs;

    quant[0] = 1 << shift;
    quant[1] = 1 << shift;
    dequant[0] = (1 << QUANT_TABLE_BITS) << (log_scale - 1);
    dequant[1] = (1 << QUANT_TABLE_BITS) << (log_scale - 1);
    FillQuantRow8(quant, dequant);

    if (iqmatrix && (rng_.Rand8() & 1)) {
      for (int i = 0; i < num_coeffs; i++)
        iqmatrix[i] = (qm_val_t)(1 + rng_(1 << AVM_QM_BITS));
      param->iqmatrix = iqmatrix;
    } else {
      param->iqmatrix = NULL;
    }

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

    InitTcqCtxDiagonal(tcq_ctx, blk_pos, bwl);

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
      int32_t quant[8];
      int32_t dequant[8];
      uint16_t block_eob_rate[MAX_TRELLIS];
      qm_val_t iqmatrix[MAX_TRELLIS];

      InitParam(tx_size, &param, &txb_costs, tcoeff, tmp_sign, quant, dequant,
                block_eob_rate, iqmatrix);

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

      std::ostringstream ctx;
      ctx << "iter=" << iter << " tx_size=" << tx_size;
      ASSERT_NO_FATAL_FAILURE(
          CompareBestPath(trellis_ref, trellis_tst, param.scan, param.dequant,
                          param.iqmatrix, param.tcoeff, first_scan_pos,
                          param.log_scale, param.bwl, num_coeffs, ctx.str()));
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
      int32_t quant[8];
      int32_t dequant[8];
      uint16_t block_eob_rate[MAX_TRELLIS];
      qm_val_t iqmatrix[MAX_TRELLIS];

      InitParam(tx_size, &param, &txb_costs, tcoeff, tmp_sign, quant, dequant,
                block_eob_rate, iqmatrix);

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
TEST_P(TcqLoopDiagonalSt8Test, DISABLED_Speed) { RunSpeedTest(); }

// ref func, test func, tx size, bit depth, strict (also compare dead nodes).
typedef std::tuple<TcqLoopDiagonalSt8Func, TcqLoopDiagonalSt8Func, TX_SIZE, int,
                   bool>
    TcqLoopDiagonalSt8BlockParam;

// init_tcq_decision() value for unreachable states.
static const int64_t kTcqSentinel = INT64_MAX >> 10;

class TcqLoopDiagonalSt8BlockTest
    : public ::testing::TestWithParam<TcqLoopDiagonalSt8BlockParam> {
 public:
  // Coefficient classes by orig_qIdx (< 2 takes the q1 decide path).
  enum CoeffMode {
    kCoeffLaplacian,  // 70% q1 (orig_qIdx 0/1), 20% medium, 10% large
    kCoeffQ1Only,
    kCoeffGenericOnly,
    kCoeffZero,
    kCoeffMax,
    kCoeffTieSmall,  // orig_qIdx in [0, 3]
    kCoeffConstant,
    kCoeffCheckerboard,
  };
  enum CostMode {
    kCostDefaultCdf,  // av2_fill_coeff_costs() from the default CDFs
    kCostRandom,      // random 11-bit costs
    kCostTiny,        // random costs in [0, 3]: many rate ties
    kCostZero,
  };
  enum InitMode {
    kInitProduction,  // trellis_first_pos(): states 0 and 4 live
    kInitAllEqual,
    kInitNearEqual,  // all live, rdCost differing by -1 / 0 / +1
    kInitSentinel,   // random live subset, others at or near the sentinel
  };

  struct Config {
    CoeffMode coeff;
    CostMode cost;
    InitMode init;
    int qindex;          // -1: random
    int first_scan_pos;  // -1: random
    int64_t rdmult;      // -1: production formula
    int sharpness;       // -1: random
    int use_qm;          // -1: random
  };

  static void SetUpTestSuite() {
    static const int kCtxQindex[TOKEN_CDF_Q_CTXS] = { 60, 120, 170, 220 };
    std::unique_ptr<AV2_COMMON> cm(new AV2_COMMON());
    const std::unique_ptr<FRAME_CONTEXT> fc(new FRAME_CONTEXT());
    cm->fc = fc.get();
    for (int i = 0; i < TOKEN_CDF_Q_CTXS; i++) {
      cm->quant_params.base_qindex = kCtxQindex[i];
      av2_default_coef_probs(cm.get());
      cdf_costs_[i] =
          static_cast<CoeffCosts *>(avm_memalign(32, sizeof(CoeffCosts)));
      memset(cdf_costs_[i], 0, sizeof(CoeffCosts));
      av2_fill_coeff_costs(cdf_costs_[i], fc.get(), 1);
    }
  }

  static void TearDownTestSuite() {
    for (int i = 0; i < TOKEN_CDF_Q_CTXS; i++) {
      avm_free(cdf_costs_[i]);
      cdf_costs_[i] = nullptr;
    }
  }

  void SetUp() override {
    ref_func_ = std::get<0>(GetParam());
    tst_func_ = std::get<1>(GetParam());
    tx_size_ = std::get<2>(GetParam());
    bd_ = std::get<3>(GetParam());
    strict_ = std::get<4>(GetParam());
    bwl_ = get_txb_bwl(tx_size_);
    width_ = 1 << bwl_;
    height_ = get_txb_high(tx_size_);
    num_coeffs_ = width_ * height_;
    log_scale_ = av2_get_tx_scale(tx_size_) + 1;
    scan_tbl_ = get_scan(tx_size_, DCT_DCT)->scan;
    for (int i = 0; i < num_coeffs_; i++) iscan_[scan_tbl_[i]] = i;

    tcoeff_base_ = Alloc<tran_low_t>(MAX_TRELLIS + kMaxOffset);
    qcoeff_base_ = Alloc<tran_low_t>(MAX_TRELLIS + kMaxOffset);
    tmp_sign_base_ = Alloc<int32_t>(MAX_TRELLIS + kMaxOffset);
    block_eob_rate_base_ = Alloc<uint16_t>(MAX_TRELLIS + kMaxOffset);
    iqmatrix_base_ = Alloc<qm_val_t>(MAX_TRELLIS + kMaxOffset);
    scan_base_ = Alloc<int16_t>(MAX_TRELLIS + kMaxOffset);
    trellis_ref_base_ =
        Alloc<tcq_node_t>(MAX_TRELLIS * TCQ_MAX_STATES + kMaxOffset);
    trellis_tst_base_ =
        Alloc<tcq_node_t>(MAX_TRELLIS * TCQ_MAX_STATES + kMaxOffset);
    ctx_ref_base_ = Alloc<uint8_t>(sizeof(tcq_ctx_t) + kMaxOffset);
    ctx_tst_base_ = Alloc<uint8_t>(sizeof(tcq_ctx_t) + kMaxOffset);
    rand_costs_ = Alloc<LV_MAP_COEFF_COST>(1);
    ASSERT_NE(tcoeff_base_, nullptr);
    ASSERT_NE(qcoeff_base_, nullptr);
    ASSERT_NE(tmp_sign_base_, nullptr);
    ASSERT_NE(block_eob_rate_base_, nullptr);
    ASSERT_NE(iqmatrix_base_, nullptr);
    ASSERT_NE(scan_base_, nullptr);
    ASSERT_NE(trellis_ref_base_, nullptr);
    ASSERT_NE(trellis_tst_base_, nullptr);
    ASSERT_NE(ctx_ref_base_, nullptr);
    ASSERT_NE(ctx_tst_base_, nullptr);
    ASSERT_NE(rand_costs_, nullptr);
    memset(rand_costs_, 0, sizeof(*rand_costs_));
    memset(tmp_sign_base_, 0, sizeof(int32_t) * (MAX_TRELLIS + kMaxOffset));
    SetOffsets(0, 0);
    n_q1_ = n_gen_ = n_def_calls_ = n_lf_only_calls_ = n_ties_ = 0;
  }

  void TearDown() override {
    avm_free(tcoeff_base_);
    avm_free(qcoeff_base_);
    avm_free(tmp_sign_base_);
    avm_free(block_eob_rate_base_);
    avm_free(iqmatrix_base_);
    avm_free(scan_base_);
    avm_free(trellis_ref_base_);
    avm_free(trellis_tst_base_);
    avm_free(ctx_ref_base_);
    avm_free(ctx_tst_base_);
    avm_free(rand_costs_);
    libavm_test::ClearSystemState();
  }

 protected:
  static const int kRandomIters = 1000;
  static const int kBoundaryIters = 100;
  static const int kMaxOffset = 8;

  template <typename T>
  static T *Alloc(int n) {
    return static_cast<T *>(avm_memalign(64, sizeof(T) * n));
  }

  // Offsets are in elements, except tcq_ctx_t's in bytes.
  void SetOffsets(int in_off, int out_off) {
    in_off_ = in_off;
    out_off_ = out_off;
    tcoeff_ = tcoeff_base_ + in_off;
    qcoeff_ = qcoeff_base_ + in_off;
    tmp_sign_ = tmp_sign_base_ + in_off;
    block_eob_rate_ = block_eob_rate_base_ + in_off;
    iqmatrix_ = iqmatrix_base_ + in_off;
    if (in_off) {
      int16_t *s = scan_base_ + in_off;
      memcpy(s, scan_tbl_, sizeof(int16_t) * num_coeffs_);
      scan_ = s;
    } else {
      scan_ = scan_tbl_;
    }
    trellis_ref_ = trellis_ref_base_ + out_off;
    trellis_tst_ = trellis_tst_base_ + out_off;
    ctx_ref_ = reinterpret_cast<tcq_ctx_t *>(ctx_ref_base_ + out_off);
    ctx_tst_ = reinterpret_cast<tcq_ctx_t *>(ctx_tst_base_ + out_off);
  }

  int MaxQindex() const {
    return bd_ == 8    ? QINDEX_RANGE_8_BITS - 1
           : bd_ == 10 ? QINDEX_RANGE_10_BITS - 1
                       : QINDEX_RANGE_12_BITS - 1;
  }

  // Largest |tcoeff| the forward transform gives at this bit depth.
  int CoeffCap() const { return (1 << (bd_ + 7)) - 1; }

  static int QCtx(int q) {
    if (q <= 90) return 0;
    if (q <= 140) return 1;
    if (q <= 190) return 2;
    return 3;
  }

  void SetupQuant(int qindex) {
    const avm_bit_depth_t bd = static_cast<avm_bit_depth_t>(bd_);
    // A DC delta q, as y_dc_delta_q gives, so the DC dequant differs from AC.
    const int dc_delta = rng_(2) ? 0 : rng_(97) - 48;
    dequant_[0] = av2_dc_quant_QTX(qindex, dc_delta, 0, bd);
    dequant_[1] = av2_ac_quant_QTX(qindex, 0, 0, bd);
    for (int i = 0; i < 2; i++) {
      quant_[i] = (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / dequant_[i];
    }
    FillQuantRow8(quant_, dequant_);
  }

  int64_t ProductionRdmult(int qindex, int sharpness, int is_inter) {
    const int q =
        av2_dc_quant_QTX(qindex, 0, 0, static_cast<avm_bit_depth_t>(bd_));
    int64_t rd =
        ROUND_POWER_OF_TWO_64((int64_t)q * q * 84 / 32, 2 * QUANT_TABLE_BITS);
    if (bd_ == 10) rd = ROUND_POWER_OF_TWO(rd, 4);
    if (bd_ == 12) rd = ROUND_POWER_OF_TWO(rd, 8);
    rd = (rd * (64 + rng_(192))) >> 7;
    if (rd < 1) rd = 1;
    return av2_compute_rdmult_for_plane(rd, plane_rd_mult[is_inter][0], bd_,
                                        sharpness + 2);
  }

  void ComputeBlockEobRate(const CoeffCosts *cc, int is_inter, int eob) {
    const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size_);
    const LV_MAP_COEFF_COST *txb_costs = &cc->coeff_costs[txs_ctx][0];
    const int eob_multi_size = txsize_log2_minus4[tx_size_];
    const int *tbl_eob_cost =
        cc->eob_costs[eob_multi_size][0].eob_cost[is_inter];
    block_eob_rate_[0] = tbl_eob_cost[0];
    block_eob_rate_[1] = tbl_eob_cost[1];
    int scan_pos = 2;
    int n_offset_bits = 0;
    while (scan_pos < eob) {
      const int eob_pt_low = AVMMIN(2 + n_offset_bits, EOB_PT_INDEX_COUNT - 1);
      int eob_pt_rate = tbl_eob_cost[eob_pt_low];
      if (eob_multi_size == 4 && (eob_pt_low == EOB_PT_INDEX_COUNT - 1))
        eob_pt_rate += av2_cost_literal(1);
      else if (eob_multi_size > 4 && (eob_pt_low == EOB_PT_INDEX_COUNT - 1))
        eob_pt_rate += av2_cost_literal(2);
      for (int bit = 0; bit < 2; bit++) {
        const int extra_bit_rate =
            txb_costs->eob_extra_cost[n_offset_bits][bit];
        const int eob_rate =
            eob_pt_rate + extra_bit_rate + av2_cost_literal(n_offset_bits);
        for (int i = 0; i < (1 << n_offset_bits) && scan_pos < MAX_TRELLIS;
             i++) {
          block_eob_rate_[scan_pos++] = eob_rate;
        }
      }
      n_offset_bits++;
    }
  }

  int OrigQIdx(int scan_pos) const {
    const int shift = 16 - log_scale_ + QUANT_FP_BITS;
    const tran_low_t v = (tran_low_t)(((int64_t)abs(tcoeff_[scan_[scan_pos]]) *
                                       quant_[scan_pos != 0]) >>
                                      shift);
    prequant_t pq;
    pq.orig_qIdx = v;  // Same int16_t truncation as the kernel.
    return pq.orig_qIdx;
  }

  int TargetQIdx(CoeffMode mode, int scan_pos, int const_t) {
    switch (mode) {
      case kCoeffLaplacian: {
        const int r = rng_(100);
        if (r < 70) return rng_(2);
        if (r < 90) return 2 + rng_(15);
        return 17 + rng_(600);
      }
      case kCoeffQ1Only: return rng_(2);
      case kCoeffGenericOnly: return rng_(5) ? 2 + rng_(30) : 32 + rng_(1000);
      case kCoeffZero: return 0;
      case kCoeffMax: return 30000;
      case kCoeffTieSmall: return rng_(4);
      case kCoeffConstant: return const_t;
      case kCoeffCheckerboard: return (scan_pos & 1) ? 0 : 200 + rng_(300);
      default: return 0;
    }
  }

  tran_low_t CoeffForQIdx(int t, int scan_pos, bool exact_zero) {
    if (exact_zero) return 0;
    const int64_t q = quant_[scan_pos != 0];
    const int shift = 16 - log_scale_ + QUANT_FP_BITS;
    const int64_t lo = (((int64_t)t << shift) + q - 1) / q;
    const int64_t hi = ((((int64_t)t + 1) << shift) + q - 1) / q;
    int64_t c = lo;
    if (hi - lo > 1) c = lo + rng_((int)AVMMIN(hi - lo, 1 << 30));
    const int64_t cap = CoeffCap();
    if (c > cap) c = cap;
    return (tran_low_t)(rng_(2) ? -c : c);
  }

  void GenerateBlock(const Config &cfg) {
    qindex_ = cfg.qindex >= 0 ? cfg.qindex : -1;
    if (qindex_ < 0) {
      const int r = rng_(8);
      qindex_ = r == 0 ? 0 : r == 1 ? MaxQindex() : rng_(MaxQindex() + 1);
    }
    SetupQuant(qindex_);
    sharpness_ = cfg.sharpness >= 0 ? cfg.sharpness
                 : rng_(4) == 0     ? 1 + rng_(7)
                                    : 0;
    const int is_inter = rng_(2);
    if (cfg.rdmult >= 0) {
      rdmult_ = cfg.rdmult;
    } else if (rng_(16) == 0) {
      rdmult_ = (1LL << 32) - 1;  // av2_compute_rdmult_for_plane() clamp.
    } else {
      rdmult_ = ProductionRdmult(qindex_, sharpness_, is_inter);
    }

    if (cfg.first_scan_pos >= 0) {
      first_scan_pos_ = AVMMIN(cfg.first_scan_pos, num_coeffs_ - 1);
    } else {
      const int r = rng_(4);
      if (r == 0) {
        first_scan_pos_ = num_coeffs_ - 1;
      } else if (r == 1) {
        first_scan_pos_ = 1 + rng_(AVMMIN(16, num_coeffs_ - 1));
      } else {
        first_scan_pos_ = 1 + rng_(num_coeffs_ - 1);
      }
    }
    const int eob = first_scan_pos_ + 1;

    const LV_MAP_COEFF_COST *txb_costs = nullptr;
    switch (cfg.cost) {
      case kCostDefaultCdf: {
        const CoeffCosts *cc = cdf_costs_[QCtx(qindex_)];
        txb_costs = &cc->coeff_costs[get_txsize_entropy_ctx(tx_size_)][0];
        ComputeBlockEobRate(cc, is_inter, eob);
        break;
      }
      case kCostRandom:
      case kCostTiny:
      case kCostZero: {
        const int mask = cfg.cost == kCostRandom ? 2047
                         : cfg.cost == kCostTiny ? 3
                                                 : 0;
        generate_random_cost_tables(&rng_, rand_costs_, mask);
        for (int i = 0; i < MAX_TRELLIS; i++) {
          block_eob_rate_[i] = mask ? rng_(mask + 1) : 0;
        }
        txb_costs = rand_costs_;
        break;
      }
    }

    memset(tcoeff_, 0, sizeof(tran_low_t) * MAX_TRELLIS);
    memset(qcoeff_, 0, sizeof(tran_low_t) * MAX_TRELLIS);
    const int const_t = rng_(3) == 0 ? rng_(2) : 2 + rng_(40);
    for (int s = 0; s <= first_scan_pos_; s++) {
      const int blk = scan_[s];
      int t = TargetQIdx(cfg.coeff, s, const_t);
      // The quantizer leaves a nonzero level at the eob position.
      if (cfg.coeff == kCoeffLaplacian && s == first_scan_pos_ && t == 0) t = 1;
      tcoeff_[blk] = CoeffForQIdx(t, s, cfg.coeff == kCoeffZero);
      const int lvl = (t + 1) >> 1;
      qcoeff_[blk] = tcoeff_[blk] < 0 ? -lvl : lvl;
      tmp_sign_[blk] = rng_(2);
    }

    const int use_qm = cfg.use_qm >= 0 ? cfg.use_qm : (rng_(4) == 0);
    if (use_qm) {
      for (int i = 0; i < num_coeffs_; i++) {
        iqmatrix_[i] = (qm_val_t)(8 + rng_(121));
      }
    }

    param_.plane = 0;
    param_.bwl = bwl_;
    param_.txb_height = height_;
    param_.tx_size = tx_size_;
    param_.tx_class = TX_CLASS_2D;
    param_.sharpness = sharpness_;
    param_.rdmult = rdmult_;
    param_.log_scale = log_scale_;
    param_.dc_sign_ctx = rng_(DC_SIGN_CONTEXTS);
    param_.scan = scan_;
    param_.tmp_sign = tmp_sign_;
    param_.qcoeff = qcoeff_;
    param_.tcoeff = tcoeff_;
    param_.quant = quant_;
    param_.dequant = dequant_;
    param_.iqmatrix = use_qm ? iqmatrix_ : NULL;
    param_.block_eob_rate = block_eob_rate_;
    param_.txb_costs = txb_costs;

    memset(ctx_ref_, 0, sizeof(*ctx_ref_));
    InitTcqCtxDiagonal(ctx_ref_, scan_[first_scan_pos_], bwl_);
    const size_t nodes = (size_t)(first_scan_pos_ + 1) * TCQ_MAX_STATES;
    // Pattern-fill the nodes the kernel must overwrite.
    memset(trellis_ref_, 0xA5, nodes * sizeof(tcq_node_t));
    trellis_first_pos(&param_, first_scan_pos_, ctx_ref_, trellis_ref_);

    ApplyInitMode(cfg.init);

    memcpy(ctx_tst_, ctx_ref_, sizeof(tcq_ctx_t));
    memcpy(trellis_tst_, trellis_ref_, nodes * sizeof(tcq_node_t));
  }

  void ApplyInitMode(InitMode mode) {
    if (mode == kInitProduction) return;
    tcq_node_t *d = &trellis_ref_[first_scan_pos_ << TCQ_N_STATES_LOG];
    const int blk_pos = scan_[first_scan_pos_];
    const int col = blk_pos - ((blk_pos >> bwl_) << bwl_);
    const int64_t base = rng_(4) == 0 ? 0 : rng_(1 << 20);
    const int base_rate = rng_(1 << 12);
    const int forced_live = rng_(2) ? 0 : 4;
    for (int st = 0; st < TCQ_N_STATES; st++) {
      const int abs_level = rng_(8);
      bool live = true;
      switch (mode) {
        case kInitAllEqual:
          d[st].rdCost = base;
          d[st].rate = base_rate;
          break;
        case kInitNearEqual:
          d[st].rdCost = AVMMAX(0, base + rng_(3) - 1);
          d[st].rate = base_rate + rng_(3);
          break;
        case kInitSentinel:
          live = st == forced_live || rng_(3) == 0;
          if (live) {
            d[st].rdCost = base + rng_(3);
            d[st].rate = base_rate;
          } else {
            d[st].rdCost = kTcqSentinel - (rng_(2) ? 0 : rng_(4));
            d[st].rate = 0;
          }
          break;
        default: break;
      }
      d[st].absLevel = live ? abs_level : -1;
      d[st].prevId = live ? -1 : -2;
      ctx_ref_->lev_new[col][st] =
          (uint8_t)AVMMIN(AVMMAX(0, (int)d[st].absLevel), MAX_VAL_BR_CTX);
      ctx_ref_->prev_st[col][st] = -1;
      ctx_ref_->orig_st[st] = st;
    }
  }

  std::string Context(const char *test, int iter) const {
    std::ostringstream os;
    os << test << " tx=" << width_ << "x" << height_ << " (TX_SIZE "
       << (int)tx_size_ << ") bd=" << bd_ << " iter=" << iter
       << " first_scan_pos=" << first_scan_pos_ << " qindex=" << qindex_
       << " rdmult=" << rdmult_ << " sharpness=" << sharpness_
       << " qm=" << (param_.iqmatrix != NULL) << " in_off=" << in_off_
       << " out_off=" << out_off_;
    return os.str();
  }

  void RunOne(const Config &cfg, const char *test, int iter) {
    GenerateBlock(cfg);
    const int scan_hi = first_scan_pos_ - 1;
    ref_func_(&param_, scan_hi, 0, ctx_ref_, trellis_ref_);
    ASM_REGISTER_STATE_CHECK(
        tst_func_(&param_, scan_hi, 0, ctx_tst_, trellis_tst_));

    for (int s = 0; s <= scan_hi; s++) {
      if (OrigQIdx(s) < 2)
        n_q1_++;
      else
        n_gen_++;
      const tcq_node_t *d = &trellis_ref_[s << TCQ_N_STATES_LOG];
      for (int a = 0; a < TCQ_N_STATES; a++) {
        for (int b = a + 1; b < TCQ_N_STATES; b++) {
          if (d[a].rdCost == d[b].rdCost && d[a].rdCost < (INT64_MAX >> 12))
            n_ties_++;
        }
      }
    }
    if (scan_hi >= 10) n_def_calls_++;
    if (scan_hi >= 0 && scan_hi < 10) n_lf_only_calls_++;

    CompareOutputs(Context(test, iter));
  }

  void CompareOutputs(const std::string &ctx) {
    // Dead nodes may differ between C and SIMD; only check they stay dead.
    const int64_t kUnreachable = kTcqUnreachableCost;
    const int scan_hi = first_scan_pos_ - 1;
    for (int s = 0; s <= scan_hi; s++) {
      for (int st = 0; st < TCQ_N_STATES; st++) {
        const tcq_node_t &r = trellis_ref_[(s << TCQ_N_STATES_LOG) + st];
        const tcq_node_t &t = trellis_tst_[(s << TCQ_N_STATES_LOG) + st];
        const bool r_live = r.rdCost < kUnreachable;
        const bool t_live = t.rdCost < kUnreachable;
        if (strict_) {
          ASSERT_EQ(r_live, t_live)
              << "reachability mismatch at scan_pos=" << s << " st=" << st
              << " ref rdCost=" << (int64_t)r.rdCost
              << " tst rdCost=" << (int64_t)t.rdCost << " " << ctx;
        }
        if (!r_live && !t_live) continue;
        ASSERT_EQ((int64_t)r.rdCost, (int64_t)t.rdCost)
            << "rdCost mismatch at scan_pos=" << s << " st=" << st << " "
            << ctx;
        ASSERT_EQ((int)r.rate, (int)t.rate)
            << "rate mismatch at scan_pos=" << s << " st=" << st << " " << ctx;
        ASSERT_EQ((int)r.absLevel, (int)t.absLevel)
            << "absLevel mismatch at scan_pos=" << s << " st=" << st << " "
            << ctx;
        ASSERT_EQ((int)r.prevId, (int)t.prevId)
            << "prevId mismatch at scan_pos=" << s << " st=" << st << " "
            << ctx;
      }
    }

    // update_states() writes column c last at blk_pos == c. Live lanes only.
    for (int c = 0; c < width_; c++) {
      const int s = iscan_[c];
      if (s > scan_hi) continue;
      for (int st = 0; st < TCQ_MAX_STATES; ++st) {
        if (trellis_ref_[(s << TCQ_N_STATES_LOG) + st].rdCost >= kUnreachable)
          continue;
        ASSERT_EQ((int)ctx_ref_->lev_new[c][st], (int)ctx_tst_->lev_new[c][st])
            << "lev_new[" << c << "][" << st << "] " << ctx;
        ASSERT_EQ((int)ctx_ref_->prev_st[c][st], (int)ctx_tst_->prev_st[c][st])
            << "prev_st[" << c << "][" << st << "] " << ctx;
        if (c == 0) {
          ASSERT_EQ((int)ctx_ref_->orig_st[st], (int)ctx_tst_->orig_st[st])
              << "orig_st[" << st << "] " << ctx;
        }
      }
    }

    ASSERT_NO_FATAL_FAILURE(CompareBestPath(
        trellis_ref_, trellis_tst_, scan_, dequant_, param_.iqmatrix, tcoeff_,
        first_scan_pos_, log_scale_, bwl_, num_coeffs_, ctx));
  }

  void RunSpeedTest() {
    const int kNumTests = (num_coeffs_ <= 64)    ? 20000
                          : (num_coeffs_ <= 256) ? 5000
                                                 : 1000;
    const int kNumBlocks = 4;
    int64_t t_ref = 0, t_tst = 0;
    for (int b = 0; b < kNumBlocks; b++) {
      const Config cfg = { kCoeffLaplacian,
                           kCostDefaultCdf,
                           kInitProduction,
                           MaxQindex() * 3 / 5,
                           -1,
                           -1,
                           0,
                           0 };
      GenerateBlock(cfg);
      const tcq_ctx_t ctx0 = *ctx_ref_;
      tcq_node_t first[TCQ_MAX_STATES];
      memcpy(first, &trellis_ref_[first_scan_pos_ << TCQ_N_STATES_LOG],
             sizeof(first));
      const int scan_hi = first_scan_pos_ - 1;
      tcq_node_t *dst_first =
          &trellis_tst_[first_scan_pos_ << TCQ_N_STATES_LOG];

      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int i = 0; i < kNumTests; ++i) {
        *ctx_tst_ = ctx0;
        memcpy(dst_first, first, sizeof(first));
        ref_func_(&param_, scan_hi, 0, ctx_tst_, trellis_tst_);
      }
      avm_usec_timer_mark(&timer);
      t_ref += avm_usec_timer_elapsed(&timer);

      avm_usec_timer_start(&timer);
      for (int i = 0; i < kNumTests; ++i) {
        *ctx_tst_ = ctx0;
        memcpy(dst_first, first, sizeof(first));
        tst_func_(&param_, scan_hi, 0, ctx_tst_, trellis_tst_);
      }
      avm_usec_timer_mark(&timer);
      t_tst += avm_usec_timer_elapsed(&timer);
    }
    printf(
        "TcqLoopDiagonalSt8 %dx%d bd=%d: C=%7.1fms SIMD=%7.1fms gain=%.2fx\n",
        tx_size_wide[tx_size_], tx_size_high[tx_size_], bd_, t_ref / 1000.0,
        t_tst / 1000.0, (double)t_ref / AVMMAX(1, (double)t_tst));
  }

  libavm_test::ACMRandom rng_{ libavm_test::ACMRandom::DeterministicSeed() };
  TcqLoopDiagonalSt8Func ref_func_;
  TcqLoopDiagonalSt8Func tst_func_;
  TX_SIZE tx_size_;
  int bd_;
  bool strict_;
  int bwl_, width_, height_, num_coeffs_, log_scale_;
  const int16_t *scan_tbl_;
  const int16_t *scan_;
  int iscan_[MAX_TRELLIS];
  int in_off_ = 0, out_off_ = 0;

  tran_low_t *tcoeff_base_, *tcoeff_;
  tran_low_t *qcoeff_base_, *qcoeff_;
  int32_t *tmp_sign_base_, *tmp_sign_;
  uint16_t *block_eob_rate_base_, *block_eob_rate_;
  qm_val_t *iqmatrix_base_, *iqmatrix_;
  int16_t *scan_base_;
  tcq_node_t *trellis_ref_base_, *trellis_ref_;
  tcq_node_t *trellis_tst_base_, *trellis_tst_;
  uint8_t *ctx_ref_base_, *ctx_tst_base_;
  tcq_ctx_t *ctx_ref_;
  tcq_ctx_t *ctx_tst_;
  LV_MAP_COEFF_COST *rand_costs_;
  int32_t quant_[8];
  int32_t dequant_[8];
  tcq_param_t param_;

  int qindex_ = 0;
  int first_scan_pos_ = 0;
  int64_t rdmult_ = 0;
  int sharpness_ = 0;

  int64_t n_q1_, n_gen_, n_def_calls_, n_lf_only_calls_, n_ties_;

  static CoeffCosts *cdf_costs_[TOKEN_CDF_Q_CTXS];
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqLoopDiagonalSt8BlockTest);

CoeffCosts *TcqLoopDiagonalSt8BlockTest::cdf_costs_[TOKEN_CDF_Q_CTXS] = {};

TEST_P(TcqLoopDiagonalSt8BlockTest, RandomValues) {
  for (int iter = 0; iter < kRandomIters; ++iter) {
    const Config cfg = { kCoeffLaplacian,
                         (iter & 1) ? kCostRandom : kCostDefaultCdf,
                         kInitProduction,
                         -1,
                         -1,
                         -1,
                         -1,
                         -1 };
    RunOne(cfg, "RandomValues", iter);
    if (HasFatalFailure()) return;
  }
  EXPECT_GT(n_q1_, 0) << "q1 decide path never exercised";
  EXPECT_GT(n_gen_, 0) << "generic decide path never exercised";
  EXPECT_GT(n_def_calls_, 0) << "default (non-LF) region never exercised";
  EXPECT_GT(n_lf_only_calls_, 0) << "LF-only calls never exercised";
}

TEST_P(TcqLoopDiagonalSt8BlockTest, TieBreak) {
  static const CoeffMode kCoeff[] = { kCoeffZero, kCoeffConstant,
                                      kCoeffTieSmall, kCoeffQ1Only,
                                      kCoeffGenericOnly };
  static const CostMode kCost[] = { kCostZero, kCostTiny, kCostDefaultCdf };
  for (int iter = 0; iter < kRandomIters; ++iter) {
    const int64_t rdm = rng_(4) == 0 ? 0 : rng_(16);
    const Config cfg = { kCoeff[rng_(5)],
                         kCost[rng_(3)],
                         (InitMode)(1 + (iter % 3)),
                         -1,
                         -1,
                         rdm,
                         -1,
                         rng_(4) == 0 };
    RunOne(cfg, "TieBreak", iter);
    if (HasFatalFailure()) return;
  }
  EXPECT_GT(n_ties_, 0) << "no exact rdCost ties were produced";
  EXPECT_GT(n_q1_, 0);
  EXPECT_GT(n_gen_, 0);
}

TEST_P(TcqLoopDiagonalSt8BlockTest, Extremes) {
  static const CoeffMode kCoeff[] = { kCoeffZero, kCoeffMax, kCoeffCheckerboard,
                                      kCoeffConstant };
  int iter = 0;
  for (const CoeffMode cm : kCoeff) {
    for (const int q : { 0, MaxQindex() }) {
      for (int i = 0; i < kBoundaryIters / 4; ++i, ++iter) {
        const int64_t rdm = (i & 1) ? (1LL << 32) - 1 : -1;
        const Config cfg = { cm,
                             (i & 2) ? kCostRandom : kCostDefaultCdf,
                             kInitProduction,
                             q,
                             (i & 4) ? num_coeffs_ - 1 : -1,
                             rdm,
                             -1,
                             -1 };
        RunOne(cfg, "Extremes", iter);
        if (HasFatalFailure()) return;
      }
    }
  }
}

TEST_P(TcqLoopDiagonalSt8BlockTest, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2 || HAVE_NEON
// The other 64-point TX sizes give the kernel the same inputs as a listed one.
static const TX_SIZE kSt8TxSizes[] = {
  TX_4X4,  TX_8X8,  TX_16X16, TX_32X32, TX_64X64, TX_4X8,  TX_8X4,
  TX_8X16, TX_16X8, TX_16X32, TX_32X16, TX_4X16,  TX_16X4, TX_8X32,
  TX_32X8, TX_4X32, TX_32X4,  TX_8X64,  TX_64X8,
};
#endif  // HAVE_AVX2 || HAVE_NEON

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqLoopDiagonalSt8BlockTest,
    ::testing::Combine(::testing::Values(av2_trellis_loop_diagonal_st8_c),
                       ::testing::Values(av2_trellis_loop_diagonal_st8_avx2),
                       ::testing::ValuesIn(kSt8TxSizes),
                       ::testing::Values(8, 10, 12), ::testing::Values(false)));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, TcqLoopDiagonalSt8BlockTest,
    ::testing::Combine(::testing::Values(av2_trellis_loop_diagonal_st8_c),
                       ::testing::Values(av2_trellis_loop_diagonal_st8_neon),
                       ::testing::ValuesIn(kSt8TxSizes),
                       ::testing::Values(8, 12), ::testing::Values(true)));
#endif  // HAVE_NEON

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

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, TcqLoopDiagonalSt8Test,
                         ::testing::Values(TcqLoopDiagonalSt8TestFuncs(
                             av2_trellis_loop_diagonal_st8_c,
                             av2_trellis_loop_diagonal_st8_neon)));
#endif  // HAVE_NEON

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, TcqDecideStatesQ1Test,
    ::testing::Values(TcqDecideStatesTestFuncs(av2_decide_states_q1_c,
                                               av2_decide_states_q1_neon)));
#endif  // HAVE_NEON

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
