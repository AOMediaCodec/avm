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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/common_data.h"
#include "av2/common/idct.h"
#include "av2/encoder/av2_quantize.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/bitops.h"
#include "avm_ports/mem.h"

namespace {

// fwd_txfm_c is linear up to rounding, so for |r| <= max_resi coefficient k
// peaks at r[p] = max_resi * sign(T(e_p)[k]) and is bounded by
// sum_p |T(max_resi * e_p)[k]| plus slack for the per-stage rounding.
struct CoeffBound {
  int64_t linear;
  int64_t upper;
  int64_t achieved;
};

bool KernelSupported(int type_1d, int len) {
  return len < 32 || type_1d == DCT2 || type_1d == IDT;
}

CoeffBound PrimaryCoeffBound(TX_SIZE tx_size, PRIMARY_TX_TYPE tx_type,
                             int use_ddt, int max_resi) {
  const int w = tx_size_wide[tx_size];
  const int h = tx_size_high[tx_size];
  const int n_in = w * h;
  const int n_out = std::min(w, 32) * std::min(h, 32);
  TxfmParam param = {};
  param.primary_tx_type = tx_type;
  param.tx_size = tx_size;
  param.use_ddt = use_ddt;
  param.bd = 12;

  std::vector<int16_t> resi(n_in, 0);
  std::vector<tran_low_t> coeff(MAX_TX_SQUARE);
  std::vector<int32_t> resp(static_cast<size_t>(n_in) * n_out);
  std::vector<int64_t> l1(n_out, 0);
  for (int p = 0; p < n_in; ++p) {
    resi[p] = static_cast<int16_t>(max_resi);
    fwd_txfm_c(resi.data(), coeff.data(), w, &param);
    resi[p] = 0;
    for (int k = 0; k < n_out; ++k) {
      resp[static_cast<size_t>(p) * n_out + k] = coeff[k];
      l1[k] += std::abs(coeff[k]);
    }
  }
  const int k_max =
      static_cast<int>(std::max_element(l1.begin(), l1.end()) - l1.begin());
  for (int p = 0; p < n_in; ++p) {
    resi[p] = static_cast<int16_t>(
        resp[static_cast<size_t>(p) * n_out + k_max] < 0 ? -max_resi
                                                         : max_resi);
  }
  fwd_txfm_c(resi.data(), coeff.data(), w, &param);
  return { l1[k_max], l1[k_max] + 3 * n_in,
           std::abs(static_cast<int64_t>(coeff[k_max])) };
}

struct EntryStats {
  int fused = 0, fallback = 0;
  int64_t mult = 0, tmp_rnd = 0, zbin_thr = 0, abs_q = 0, dq_in = 0;
};

// Rounding right shift as vrshrq_n_s32 computes it (no intermediate overflow).
int64_t Rshr(int64_t x, int n) { return (x + (int64_t{ 1 } << (n - 1))) >> n; }

// An int64 model of the fused path, not the kernel: int32 bounds and C's
// formula at 0, the zbin edges and a 7/8 geometric sweep down from coeff_max.
void CheckEntry(int64_t coeff_max, int32_t zbin, int32_t round, int32_t quant,
                int32_t quant_shift, int32_t dequant, int log_scale,
                EntryStats *st) {
  const int64_t kMin = INT32_MIN, kMax = INT32_MAX;
  ASSERT_GT(quant_shift, 0);
  ASSERT_EQ(quant_shift & (quant_shift - 1), 0);
  const int s =
      16 - log_scale - get_msb(static_cast<unsigned int>(quant_shift));
  if (s == 16 && log_scale == 0) {
    ++st->fallback;  // fused_quant_mult() returns 0 and the kernel calls C.
    return;
  }
  // A branch, not ASSERT_GE/LE: scan-build does not carry a passing ASSERT's
  // comparison into s's range, so it flags the shift below as negative.
  if (s < 0 || s > 15) {
    ADD_FAILURE() << "s " << s << " quant_shift " << quant_shift
                  << " log_scale " << log_scale;
    return;
  }
  const int64_t q = 65536 + static_cast<int64_t>(quant);
  ASSERT_GT(q, 0);
  const int64_t mult = q << (15 - s);
  ASSERT_LE(mult, kMax);
  ++st->fused;
  st->mult = std::max(st->mult, mult);

  const int64_t rnd = ROUND_POWER_OF_TWO(round, log_scale);
  const int64_t zbin_ls = ROUND_POWER_OF_TWO(zbin, log_scale);
  const int64_t zbin_thr = zbin_ls + rnd - 1;
  ASSERT_GE(zbin_thr, kMin);
  ASSERT_LE(zbin_thr, kMax);
  st->zbin_thr = std::max(st->zbin_thr, zbin_thr);
  const int n = QUANT_TABLE_BITS + log_scale;
  const int64_t bias =
      log_scale == 0 ? 0
                     : 4 - (int64_t{ 1 } << (QUANT_TABLE_BITS - 1 + log_scale));

  std::vector<int64_t> inputs = { 0, zbin_ls - 1, zbin_ls, zbin_ls + 1 };
  for (int64_t a = coeff_max; a >= 1; a = a * 7 / 8) inputs.push_back(a);
  for (const int64_t a : inputs) {
    if (a < 0 || a > coeff_max) continue;
    const int64_t c_tmp1 = a + rnd;
    const int64_t c_tmpw = c_tmp1 * (1 << AVM_QM_BITS);
    const int64_t c_tmp2 = ((c_tmpw * quant) >> 16) + c_tmpw;
    const int64_t c_abs_q =
        a >= zbin_ls ? (c_tmp2 * quant_shift) >> (16 - log_scale + AVM_QM_BITS)
                     : 0;
    const int64_t c_abs_dq =
        ROUND_POWER_OF_TWO_64(c_abs_q * dequant, QUANT_TABLE_BITS) >> log_scale;

    const int64_t tmp_rnd = a + rnd;
    ASSERT_LE(tmp_rnd, kMax) << "a " << a;
    st->tmp_rnd = std::max(st->tmp_rnd, tmp_rnd);
    const int64_t tmp = tmp_rnd > zbin_thr ? tmp_rnd : 0;
    const int64_t abs_q = (2 * tmp * mult) >> 32;
    ASSERT_LE(abs_q, kMax) << "a " << a;
    ASSERT_EQ(abs_q, c_abs_q) << "a " << a;
    st->abs_q = std::max(st->abs_q, abs_q);

    for (const int neg : { 0, 1 }) {
      const int64_t qv = neg ? -abs_q : abs_q;
      const int64_t prod = qv * dequant;
      ASSERT_GE(prod, kMin) << "a " << a;
      ASSERT_LE(prod, kMax) << "a " << a;
      const int64_t sign = neg ? -1 : 0;
      const int64_t dq_in = prod + (log_scale == 0 ? sign : (bias ^ sign));
      ASSERT_GE(dq_in, kMin) << "a " << a;
      ASSERT_LE(dq_in, kMax) << "a " << a;
      st->dq_in = std::max(st->dq_in, dq_in < 0 ? -dq_in : dq_in);
      ASSERT_EQ(Rshr(dq_in, n), neg ? -c_abs_dq : c_abs_dq) << "a " << a;
    }
  }
}

// Runs the CheckEntry model on every table entry up to the fwd_txfm_c bound;
// only s == 16 at log_scale 0 may skip the model as a C fallback.
TEST(QuantizeBBoundTest, NeonIntermediatesFitInt32) {
  static QUANTS quants;
  static Dequants deq;
  const int kMaxResi12 = (1 << 12) - 1;

  int64_t primary12[TX_SIZES_ALL] = { 0 };
  int64_t achieved12[TX_SIZES_ALL] = { 0 };
  for (int t = 0; t < TX_SIZES_ALL; ++t) {
    const TX_SIZE ts = static_cast<TX_SIZE>(t);
    for (int type = 0; type < PRIMARY_TX_TYPES; ++type) {
      if (!KernelSupported(g_hor_tx_type[type], tx_size_wide[ts]) ||
          !KernelSupported(g_ver_tx_type[type], tx_size_high[ts]))
        continue;
      for (int ddt = 0; ddt <= 1; ++ddt) {
        const CoeffBound b = PrimaryCoeffBound(
            ts, static_cast<PRIMARY_TX_TYPE>(type), ddt, kMaxResi12);
        EXPECT_LE(b.achieved, b.upper);
        // The bound must be reached by a real residual, not just be loose.
        EXPECT_GE(b.achieved * 100, b.linear * 98)
            << "tx " << t << " type " << type;
        if (b.upper > primary12[t]) {
          primary12[t] = b.upper;
          achieved12[t] = b.achieved;
        }
      }
    }
  }

  for (int t = 0; t < TX_SIZES_ALL; ++t)
    printf("tx %2d %2dx%-2d: 12-bit coeff bound %lld, reached %lld\n", t,
           tx_size_wide[t], tx_size_high[t],
           static_cast<long long>(primary12[t]),
           static_cast<long long>(achieved12[t]));

  const avm_bit_depth_t kDepths[] = { AVM_BITS_8, AVM_BITS_10, AVM_BITS_12 };
  for (const avm_bit_depth_t bd : kDepths) {
    av2_build_quantizer(bd, 0, 0, 0, 0, 0, 0, 0, 0, &quants, &deq);
    const int qindex_range = bd == AVM_BITS_8    ? QINDEX_RANGE_8_BITS
                             : bd == AVM_BITS_10 ? QINDEX_RANGE_10_BITS
                                                 : QINDEX_RANGE;
    const int64_t max_resi = (1 << bd) - 1;
    // IST and cross-chroma outputs are clamped to 8 + bd bits.
    const int64_t clamp_max = int64_t{ 1 } << (bd + 7);
    EntryStats st;
    int64_t peak_coeff = 0;
    for (int t = 0; t < TX_SIZES_ALL; ++t) {
      const int log_scale = av2_get_tx_scale(static_cast<TX_SIZE>(t));
      const int64_t coeff_max = std::max(
          clamp_max, (primary12[t] * max_resi + kMaxResi12 - 1) / kMaxResi12);
      peak_coeff = std::max(peak_coeff, coeff_max);
      for (int q = 0; q < qindex_range; ++q) {
        for (int i = 0; i < 2; ++i) {
          const int32_t *tables[3][5] = {
            { quants.y_zbin[q], quants.y_round[q], quants.y_quant[q],
              quants.y_quant_shift[q], deq.y_dequant_QTX[q] },
            { quants.u_zbin[q], quants.u_round[q], quants.u_quant[q],
              quants.u_quant_shift[q], deq.u_dequant_QTX[q] },
            { quants.v_zbin[q], quants.v_round[q], quants.v_quant[q],
              quants.v_quant_shift[q], deq.v_dequant_QTX[q] },
          };
          for (const auto &p : tables) {
            ASSERT_NO_FATAL_FAILURE(CheckEntry(coeff_max, p[0][i], p[1][i],
                                               p[2][i], p[3][i], p[4][i],
                                               log_scale, &st))
                << "bd " << bd << " q " << q << " tx " << t << " i " << i;
          }
        }
      }
    }
    printf(
        "bd %d: coeff_max %lld tmp_rnd %lld zbin_thr %lld abs_q %lld "
        "|dq_in| %lld mult %lld; entries fused %d, C fallback (s == 16) %d\n",
        bd, static_cast<long long>(peak_coeff),
        static_cast<long long>(st.tmp_rnd), static_cast<long long>(st.zbin_thr),
        static_cast<long long>(st.abs_q), static_cast<long long>(st.dq_in),
        static_cast<long long>(st.mult), st.fused, st.fallback);
    EXPECT_GT(st.fused, 0);
    if (bd == AVM_BITS_12) {
      EXPECT_GT(st.fallback, 0);
    } else {
      EXPECT_EQ(st.fallback, 0);
    }
  }
}

}  // namespace
