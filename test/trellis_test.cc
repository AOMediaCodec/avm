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

typedef void (*TcqUpdateNbrDiagonalFunc)(struct tcq_ctx_t *tcq_ctx, int row,
                                         int col, int bwl);
typedef libavm_test::FuncParam<TcqUpdateNbrDiagonalFunc>
    TcqUpdateNbrDiagonalTestFuncs;

class TcqUpdateNbrDiagonalTest
    : public FunctionEquivalenceTest<TcqUpdateNbrDiagonalFunc> {
 protected:
  static const int kIterations = 10000;

  void RunOneIteration(int bwl, int diag, bool identity_orig) {
    int w = 1 << bwl;
    int row = rng_.Rand8() % (diag + 1);
    int col = diag - row;
    if (col >= w) col = w - 1;
    row = diag - col;
    if (row >= w) row = w - 1;

    tcq_ctx_t ctx_ref;
    tcq_ctx_t ctx_tst;
    memset(&ctx_ref, 0, sizeof(ctx_ref));

    for (int i = 0; i < TCQ_MAX_STATES; i++) {
      ctx_ref.orig_st[i] =
          identity_orig
              ? i
              : ((rng_.Rand8() % 3 == 0) ? -1
                                         : (rng_.Rand8() % TCQ_MAX_STATES));
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
    int idx_end = AVMMIN(diag + 1, w);
    int idx0 = AVMMAX(col - 2, 0);

    for (int i = idx0; i < idx_end; i++) {
      for (int st = 0; st < TCQ_MAX_STATES; st++) {
        ASSERT_EQ((int)ctx_ref.ctx[i][st], (int)ctx_tst.ctx[i][st])
            << "Mismatch in ctx at i=" << i << ", st=" << st << ", row=" << row
            << ", col=" << col << ", diag=" << diag << ", bwl=" << bwl;
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
};

TEST_P(TcqUpdateNbrDiagonalTest, RandomValues) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    int bwl = 2 + (rng_.Rand8() & 3);
    int max_diag = (1 << bwl) * 2 - 2;
    int diag = rng_.Rand8() % (max_diag + 1);
    RunOneIteration(bwl, diag, false);
  }
}

TEST_P(TcqUpdateNbrDiagonalTest, IdentityOrigSt) {
  for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
    int bwl = 2 + (rng_.Rand8() & 3);
    int max_diag = (1 << bwl) * 2 - 2;
    int diag;
    if ((iter & 3) == 0 && max_diag >= 5)
      diag = 5;
    else
      diag = (max_diag <= 6) ? max_diag : 6 + rng_.Rand8() % (max_diag - 5);
    RunOneIteration(bwl, diag, true);
  }
}

// ============================================================
// FindBestPath: invariant test (permanent)
//
// Verifies av2_find_best_path output against independently computed
// invariants derived from the TCQ spec. Covers both iqmatrix paths.
// No ISA dependency -- runs on all platforms.
// ============================================================

class FindBestPathInvariantTest : public ::testing::Test {
 protected:
  static const int kIterations = 5000;
  static const int kMaxEob = 64;
  ACMRandom rng_;

  FindBestPathInvariantTest() : rng_(ACMRandom::DeterministicSeed()) {}

  struct TrellisFixture {
    tcq_node_t trellis[MAX_TRELLIS * TCQ_MAX_STATES];
    tran_low_t tcoeff[MAX_TRELLIS];
    int32_t dequant[2];
    qm_val_t iqmatrix[MAX_TRELLIS];
    int16_t scan[MAX_TRELLIS];
    int eob_length;
    int first_scan_pos;
    int log_scale;
  };

  void GenerateTrellis(TrellisFixture *f, int eob_length, bool use_iqmatrix) {
    f->eob_length = eob_length;
    f->first_scan_pos = eob_length - 1 + rng_(8);
    f->log_scale = 1 + (rng_.Rand8() & 1);
    f->dequant[0] = (1 << QUANT_TABLE_BITS) << (f->log_scale - 1);
    f->dequant[1] = 4 + rng_(200);

    for (int i = 0; i < MAX_TRELLIS; i++) {
      f->scan[i] = i;
      f->tcoeff[i] = (tran_low_t)((rng_.Rand16() % 2048) - 1024);
    }

    if (use_iqmatrix) {
      for (int i = 0; i < MAX_TRELLIS; i++)
        f->iqmatrix[i] = 16 + (rng_.Rand8() % 16);
    }

    memset(f->trellis, 0, sizeof(tcq_node_t) * MAX_TRELLIS * TCQ_MAX_STATES);

    if (eob_length == 0) return;

    int path_states[kMaxEob] = {};
    for (int i = 0; i < eob_length; i++)
      path_states[i] = rng_.Rand8() % TCQ_N_STATES;

    for (int sp = 0; sp < eob_length; sp++) {
      int base = sp * TCQ_N_STATES;
      for (int st = 0; st < TCQ_N_STATES; st++) {
        tcq_node_t *n = &f->trellis[base + st];
        n->rdCost = (int64_t)(rng_(1 << 20)) + 1;
        n->rate = rng_(1 << 16);
        n->absLevel = rng_.Rand8() % 32;
        if (sp < eob_length - 1)
          n->prevId = rng_.Rand8() % TCQ_N_STATES;
        else
          n->prevId = -1;
      }
    }

    tcq_node_t *best = &f->trellis[path_states[0]];
    best->rdCost = 1;
  }

  static int get_dqv_ref(const int32_t *dequant, int coeff_idx,
                         const qm_val_t *iqmatrix) {
    int dqv = dequant[!!coeff_idx];
    if (iqmatrix != NULL)
      dqv = ((iqmatrix[coeff_idx] * dqv) + (1 << (AVM_QM_BITS - 1))) >>
            AVM_QM_BITS;
    return dqv;
  }

  void VerifyInvariants(const TrellisFixture *f, const qm_val_t *iqm,
                        const tran_low_t *qcoeff, const tran_low_t *dqcoeff,
                        int eob, int min_rate, int64_t min_cost) {
    // Invariant A: min_cost/min_rate match best initial state
    int64_t best_cost = INT64_MAX;
    int best_state = -2;
    int best_rate = 0;
    for (int s = 0; s < TCQ_N_STATES; s++) {
      if (f->trellis[s].rdCost < best_cost) {
        best_cost = f->trellis[s].rdCost;
        best_state = s;
        best_rate = f->trellis[s].rate;
      }
    }
    ASSERT_EQ(min_cost, best_cost) << "min_cost mismatch";
    ASSERT_EQ(min_rate, best_rate) << "min_rate mismatch";

    if (best_state < 0) {
      ASSERT_EQ(eob, 0) << "eob should be 0 when no valid state";
      return;
    }

    // Invariant F: path length == eob
    int path_len = 0;
    int state = best_state;
    for (int sp = 0; state >= 0 && sp < MAX_TRELLIS; sp++) {
      const tcq_node_t *node = &f->trellis[sp * TCQ_N_STATES + state];
      state = node->prevId;
      path_len++;
    }
    ASSERT_EQ(eob, path_len) << "eob does not match path length";

    // Walk path again to verify B, C, D
    state = best_state;
    for (int sp = 0; sp < eob; sp++) {
      const tcq_node_t *node = &f->trellis[sp * TCQ_N_STATES + state];
      int abs_level = node->absLevel;
      int next_state = node->prevId;
      int blk_pos = f->scan[sp];

      // Invariant B: |qcoeff| == absLevel
      ASSERT_EQ(abs(qcoeff[blk_pos]), abs_level)
          << "qcoeff magnitude mismatch at scan_pos=" << sp;

      // Invariant C: sign consistency
      if (abs_level > 0) {
        bool coeff_neg = f->tcoeff[blk_pos] < 0;
        bool q_neg = qcoeff[blk_pos] < 0;
        ASSERT_EQ(q_neg, coeff_neg)
            << "qcoeff sign mismatch at scan_pos=" << sp;
      } else {
        ASSERT_EQ(qcoeff[blk_pos], 0)
            << "qcoeff should be 0 when absLevel=0 at scan_pos=" << sp;
      }

      // Invariant D: dqcoeff matches independent dequant formula
      int dqv = get_dqv_ref(f->dequant, blk_pos, iqm);
      int q_i = (next_state >= 0) ? (bool)(next_state & 2) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - q_i);
      int expected_dqc = (tran_low_t)ROUND_POWER_OF_TWO_64(
                             (tran_high_t)qc * dqv, QUANT_TABLE_BITS) >>
                         f->log_scale;
      if (f->tcoeff[blk_pos] < 0) expected_dqc = -expected_dqc;

      ASSERT_EQ(dqcoeff[blk_pos], expected_dqc)
          << "dqcoeff mismatch at scan_pos=" << sp << " absLevel=" << abs_level
          << " q_i=" << q_i << " dqv=" << dqv << " qc=" << qc;

      state = next_state;
    }

    // Invariant E: zero padding beyond eob
    for (int sp = eob; sp <= f->first_scan_pos; sp++) {
      int blk_pos = f->scan[sp];
      ASSERT_EQ(qcoeff[blk_pos], 0) << "qcoeff not zeroed at scan_pos=" << sp;
      ASSERT_EQ(dqcoeff[blk_pos], 0) << "dqcoeff not zeroed at scan_pos=" << sp;
    }
  }

  void RunInvariantTest(bool use_iqmatrix) {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      int eob_length = 1 + rng_(kMaxEob - 1);
      TrellisFixture f;
      GenerateTrellis(&f, eob_length, use_iqmatrix);

      tran_low_t qcoeff[MAX_TRELLIS] = { 0 };
      tran_low_t dqcoeff[MAX_TRELLIS] = { 0 };
      int min_rate = 0;
      int64_t min_cost = INT64_MAX;
      const qm_val_t *iqm = use_iqmatrix ? f.iqmatrix : NULL;

      int eob = av2_find_best_path(f.trellis, f.scan, f.dequant, iqm, f.tcoeff,
                                   f.first_scan_pos, f.log_scale, qcoeff,
                                   dqcoeff, &min_rate, &min_cost);
      VerifyInvariants(&f, iqm, qcoeff, dqcoeff, eob, min_rate, min_cost);
    }
  }
};

TEST_F(FindBestPathInvariantTest, NoIqmatrix) { RunInvariantTest(false); }

TEST_F(FindBestPathInvariantTest, WithIqmatrix) { RunInvariantTest(true); }

TEST_F(FindBestPathInvariantTest, SingleCoeff) {
  for (int iter = 0; iter < 500 && !HasFatalFailure(); ++iter) {
    TrellisFixture f;
    GenerateTrellis(&f, 1, iter % 2 == 0);

    tran_low_t qcoeff[MAX_TRELLIS] = { 0 };
    tran_low_t dqcoeff[MAX_TRELLIS] = { 0 };
    int min_rate = 0;
    int64_t min_cost = INT64_MAX;
    const qm_val_t *iqm = (iter % 2 == 0) ? f.iqmatrix : NULL;

    int eob = av2_find_best_path(f.trellis, f.scan, f.dequant, iqm, f.tcoeff,
                                 f.first_scan_pos, f.log_scale, qcoeff, dqcoeff,
                                 &min_rate, &min_cost);
    ASSERT_EQ(eob, 1) << "single-coeff path should have eob=1";
    VerifyInvariants(&f, iqm, qcoeff, dqcoeff, eob, min_rate, min_cost);
  }
}

TEST_F(FindBestPathInvariantTest, AllZeroAbsLevel) {
  for (int iter = 0; iter < 500 && !HasFatalFailure(); ++iter) {
    int eob_length = 2 + rng_(16);
    TrellisFixture f;
    GenerateTrellis(&f, eob_length, false);

    for (int sp = 0; sp < eob_length; sp++) {
      for (int st = 0; st < TCQ_N_STATES; st++)
        f.trellis[sp * TCQ_N_STATES + st].absLevel = 0;
    }

    tran_low_t qcoeff[MAX_TRELLIS] = { 0 };
    tran_low_t dqcoeff[MAX_TRELLIS] = { 0 };
    int min_rate = 0;
    int64_t min_cost = INT64_MAX;

    int eob = av2_find_best_path(f.trellis, f.scan, f.dequant, NULL, f.tcoeff,
                                 f.first_scan_pos, f.log_scale, qcoeff, dqcoeff,
                                 &min_rate, &min_cost);
    VerifyInvariants(&f, NULL, qcoeff, dqcoeff, eob, min_rate, min_cost);

    for (int sp = 0; sp < eob; sp++) {
      int blk_pos = f.scan[sp];
      ASSERT_EQ(qcoeff[blk_pos], 0);
      ASSERT_EQ(dqcoeff[blk_pos], 0);
    }
  }
}

// ============================================================
// FindBestPath: regression test (temporary)
//
// Compares the optimized av2_find_best_path against a frozen copy of
// the pre-optimization C implementation. This is a safety net that
// can be removed once the optimized code has been validated across
// multiple CTC cycles.
// ============================================================

static int find_best_path_reference(const tcq_node_t *trellis,
                                    const int16_t *scan, const int32_t *dequant,
                                    const qm_val_t *iqmatrix,
                                    const tran_low_t *tcoeff,
                                    int first_scan_pos, int log_scale,
                                    tran_low_t *qcoeff, tran_low_t *dqcoeff,
                                    int *min_rate, int64_t *min_cost) {
  int64_t min_path_cost = INT64_MAX;
  int trel_min_rate = 0;
  int prev_id = -2;
  for (int state = 0; state < TCQ_N_STATES; state++) {
    const tcq_node_t *decision = &trellis[state];
    if (decision->rdCost < min_path_cost) {
      prev_id = state;
      min_path_cost = decision->rdCost;
      trel_min_rate = decision->rate;
    }
  }

  int scan_pos = 0;
  if (!iqmatrix) {
    int dqv = dequant[0];
    int dqv_ac = dequant[1];
    for (; prev_id >= 0; scan_pos++) {
      const tcq_node_t *decision =
          &trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
      prev_id = decision->prevId;
      int abs_level = decision->absLevel;
      int blk_pos = scan[scan_pos];
      int sign = -(tcoeff[blk_pos] < 0);
      int q_i = prev_id >= 0 ? (bool)(prev_id & 2) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - q_i);
      int dqc = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qc * dqv,
                                                  QUANT_TABLE_BITS) >>
                log_scale;
      qcoeff[blk_pos] = (abs_level ^ sign) - sign;
      dqcoeff[blk_pos] = (dqc ^ sign) - sign;
      dqv = dqv_ac;
    }
  } else {
    for (; prev_id >= 0; scan_pos++) {
      const tcq_node_t *decision =
          &trellis[(scan_pos << TCQ_N_STATES_LOG) + prev_id];
      prev_id = decision->prevId;
      int abs_level = decision->absLevel;
      int blk_pos = scan[scan_pos];
      int sign = -(tcoeff[blk_pos] < 0);
      qcoeff[blk_pos] = (abs_level ^ sign) - sign;
      int dqv = dequant[!!blk_pos];
      dqv =
          ((iqmatrix[blk_pos] * dqv) + (1 << (AVM_QM_BITS - 1))) >> AVM_QM_BITS;
      int q_i = prev_id >= 0 ? (bool)(prev_id & 2) : 0;
      int qc = (abs_level == 0) ? 0 : (2 * abs_level - q_i);
      int dqc = (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)qc * dqv,
                                                  QUANT_TABLE_BITS) >>
                log_scale;
      dqcoeff[blk_pos] = (dqc ^ sign) - sign;
    }
  }
  int eob = scan_pos;

  for (; scan_pos <= first_scan_pos; scan_pos++) {
    int blk_pos = scan[scan_pos];
    qcoeff[blk_pos] = 0;
    dqcoeff[blk_pos] = 0;
  }

  *min_rate = trel_min_rate;
  *min_cost = min_path_cost;
  return eob;
}

class FindBestPathRegressionTest : public ::testing::Test {
 protected:
  static const int kIterations = 5000;
  static const int kMaxEob = 64;
  ACMRandom rng_;

  FindBestPathRegressionTest() : rng_(ACMRandom::DeterministicSeed()) {}

  void RunRegressionTest(bool use_iqmatrix) {
    for (int iter = 0; iter < kIterations && !HasFatalFailure(); ++iter) {
      int eob_length = 1 + rng_(kMaxEob - 1);
      int first_scan_pos = eob_length - 1 + rng_(8);
      int log_scale = 1 + (rng_.Rand8() & 1);
      int32_t dequant[2];
      dequant[0] = (1 << QUANT_TABLE_BITS) << (log_scale - 1);
      dequant[1] = 4 + rng_(200);

      int16_t scan[MAX_TRELLIS];
      tran_low_t tcoeff[MAX_TRELLIS];
      for (int i = 0; i < MAX_TRELLIS; i++) {
        scan[i] = i;
        tcoeff[i] = (tran_low_t)((rng_.Rand16() % 2048) - 1024);
      }

      qm_val_t iqmatrix[MAX_TRELLIS];
      if (use_iqmatrix) {
        for (int i = 0; i < MAX_TRELLIS; i++)
          iqmatrix[i] = 16 + (rng_.Rand8() % 16);
      }
      const qm_val_t *iqm = use_iqmatrix ? iqmatrix : NULL;

      tcq_node_t trellis[MAX_TRELLIS * TCQ_MAX_STATES];
      memset(trellis, 0, sizeof(trellis));

      for (int sp = 0; sp < eob_length; sp++) {
        for (int st = 0; st < TCQ_N_STATES; st++) {
          tcq_node_t *n = &trellis[sp * TCQ_N_STATES + st];
          n->rdCost = (int64_t)(rng_(1 << 20)) + 1;
          n->rate = rng_(1 << 16);
          n->absLevel = rng_.Rand8() % 32;
          n->prevId =
              (sp < eob_length - 1) ? (rng_.Rand8() % TCQ_N_STATES) : -1;
        }
      }
      trellis[rng_.Rand8() % TCQ_N_STATES].rdCost = 1;

      tran_low_t qcoeff_ref[MAX_TRELLIS] = { 0 };
      tran_low_t dqcoeff_ref[MAX_TRELLIS] = { 0 };
      tran_low_t qcoeff_tst[MAX_TRELLIS] = { 0 };
      tran_low_t dqcoeff_tst[MAX_TRELLIS] = { 0 };
      int rate_ref = 0, rate_tst = 0;
      int64_t cost_ref = INT64_MAX, cost_tst = INT64_MAX;

      int eob_ref = find_best_path_reference(
          trellis, scan, dequant, iqm, tcoeff, first_scan_pos, log_scale,
          qcoeff_ref, dqcoeff_ref, &rate_ref, &cost_ref);
      int eob_tst = av2_find_best_path(trellis, scan, dequant, iqm, tcoeff,
                                       first_scan_pos, log_scale, qcoeff_tst,
                                       dqcoeff_tst, &rate_tst, &cost_tst);

      ASSERT_EQ(eob_ref, eob_tst) << "eob mismatch at iter=" << iter;
      ASSERT_EQ(rate_ref, rate_tst) << "rate mismatch at iter=" << iter;
      ASSERT_EQ(cost_ref, cost_tst) << "cost mismatch at iter=" << iter;
      for (int i = 0; i < MAX_TRELLIS; i++) {
        ASSERT_EQ(qcoeff_ref[i], qcoeff_tst[i])
            << "qcoeff mismatch at pos=" << i << " iter=" << iter;
        ASSERT_EQ(dqcoeff_ref[i], dqcoeff_tst[i])
            << "dqcoeff mismatch at pos=" << i << " iter=" << iter;
      }
    }
  }
};

TEST_F(FindBestPathRegressionTest, NoIqmatrix) { RunRegressionTest(false); }

TEST_F(FindBestPathRegressionTest, WithIqmatrix) { RunRegressionTest(true); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLumaTest,
    ::testing::Values(TcqRateLumaTestFuncs(av2_get_rate_dist_def_luma_c,
                                           av2_get_rate_dist_def_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, TcqRateLfLumaTest,
    ::testing::Values(TcqRateLfLumaTestFuncs(av2_get_rate_dist_lf_luma_c,
                                             av2_get_rate_dist_lf_luma_avx2)));

INSTANTIATE_TEST_SUITE_P(AVX2, TcqUpdateNbrDiagonalTest,
                         ::testing::Values(TcqUpdateNbrDiagonalTestFuncs(
                             av2_update_nbr_diagonal_c,
                             av2_update_nbr_diagonal_avx2)));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(NEON, TcqUpdateNbrDiagonalTest,
                         ::testing::Values(TcqUpdateNbrDiagonalTestFuncs(
                             av2_update_nbr_diagonal_c,
                             av2_update_nbr_diagonal_neon)));
#endif  // HAVE_NEON

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLumaTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqRateLfLumaTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TcqUpdateNbrDiagonalTest);

}  // namespace
