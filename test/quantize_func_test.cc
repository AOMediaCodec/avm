/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "avm/avm_codec.h"
#include "avm_ports/avm_timer.h"
#include "av2/encoder/encoder.h"
#include "av2/common/idct.h"
#include "av2/common/scan.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

namespace {
using libavm_test::ACMRandom;

typedef void (*QuantizeFuncHbd)(
    int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr, intptr_t n_coeffs,
    const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr,
    const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, int log_scale);

enum QuantType { TYPE_B, TYPE_DC, TYPE_FP };

enum LogScale {
  LOGSCALE_0 = 0,
  LOGSCALE_1 = 1,
  LOGSCALE_2 = 2,
};

using std::tuple;
typedef tuple<QuantizeFuncHbd, QuantizeFuncHbd, TX_SIZE, QuantType,
              avm_bit_depth_t, LogScale>
    QuantizeParam;

typedef struct {
  QUANTS quant;
  Dequants dequant;
} QuanTable;

const int kTestNum = 1000;

class QuantizeTest : public ::testing::TestWithParam<QuantizeParam> {
 protected:
  QuantizeTest()
      : quant_ref_(GET_PARAM(0)), quant_(GET_PARAM(1)), tx_size_(GET_PARAM(2)),
        type_(GET_PARAM(3)), bd_(GET_PARAM(4)), LogScale_(GET_PARAM(5)) {}

  virtual ~QuantizeTest() {}

  virtual void SetUp() {
    qtab_ = reinterpret_cast<QuanTable *>(avm_memalign(32, sizeof(*qtab_)));
    const int n_coeffs = coeff_num();
    coeff_ = reinterpret_cast<tran_low_t *>(
        avm_memalign(32, 6 * n_coeffs * sizeof(tran_low_t)));
    InitQuantizer();
  }

  virtual void TearDown() {
    avm_free(qtab_);
    qtab_ = NULL;
    avm_free(coeff_);
    coeff_ = NULL;
    libavm_test::ClearSystemState();
  }

  void InitQuantizer() {
    av2_build_quantizer(bd_, 0, 0, 0, 0, 0, 0, 0, 0, &qtab_->quant,
                        &qtab_->dequant);
  }

  void QuantizeRun(bool is_loop, int q = 0, int test_num = 1) {
    tran_low_t *coeff_ptr = coeff_;
    const intptr_t n_coeffs = coeff_num();

    tran_low_t *qcoeff_ref = coeff_ptr + n_coeffs;
    tran_low_t *dqcoeff_ref = qcoeff_ref + n_coeffs;

    tran_low_t *qcoeff = dqcoeff_ref + n_coeffs;
    tran_low_t *dqcoeff = qcoeff + n_coeffs;
    uint16_t *eob = (uint16_t *)(dqcoeff + n_coeffs);
    const int log_scale = LogScale_;

    // Testing uses 2-D DCT scan order table
    const SCAN_ORDER *const sc = get_default_scan(tx_size_, DCT_DCT);

    // Testing uses luminance quantization table
    const int32_t *zbin = qtab_->quant.y_zbin[q];
    const int32_t *round = 0;
    const int32_t *quant = 0;

    if (type_ == TYPE_B) {
      round = qtab_->quant.y_round[q];
      quant = qtab_->quant.y_quant[q];
    } else if (type_ == TYPE_FP) {
      round = qtab_->quant.y_round_fp[q];
      quant = qtab_->quant.y_quant_fp[q];
    }

    const int32_t *quant_shift = qtab_->quant.y_quant_shift[q];
    const int32_t *dequant = qtab_->dequant.y_dequant_QTX[q];

    for (int i = 0; i < test_num; ++i) {
      if (is_loop) FillCoeffRandom();

      memset(qcoeff_ref, 0, 5 * n_coeffs * sizeof(*qcoeff_ref));

      quant_ref_(0, coeff_ptr, n_coeffs, zbin, round, quant, quant_shift,
                 qcoeff_ref, dqcoeff_ref, dequant, &eob[0], sc->scan, sc->iscan,
                 log_scale);

      ASM_REGISTER_STATE_CHECK(quant_(
          0, coeff_ptr, n_coeffs, zbin, round, quant, quant_shift, qcoeff,
          dqcoeff, dequant, &eob[1], sc->scan, sc->iscan, log_scale));

      for (int j = 0; j < n_coeffs; ++j) {
        ASSERT_EQ(qcoeff_ref[j], qcoeff[j])
            << "Q mismatch on test: " << i << " at position: " << j
            << " Q: " << q << " coeff: " << coeff_ptr[j];
      }

      for (int j = 0; j < n_coeffs; ++j) {
        ASSERT_EQ(dqcoeff_ref[j], dqcoeff[j])
            << "Dq mismatch on test: " << i << " at position: " << j
            << " Q: " << q << " coeff: " << coeff_ptr[j];
      }

      ASSERT_EQ(eob[0], eob[1])
          << "eobs mismatch on test: " << i << " Q: " << q;
    }
  }

  void CompareResults(const tran_low_t *buf_ref, const tran_low_t *buf,
                      int size, const char *text, int q, int number) {
    int i;
    for (i = 0; i < size; ++i) {
      ASSERT_EQ(buf_ref[i], buf[i]) << text << " mismatch on test: " << number
                                    << " at position: " << i << " Q: " << q;
    }
  }

  int coeff_num() const { return av2_get_max_eob(tx_size_); }

  void FillCoeff(tran_low_t c) {
    const int n_coeffs = coeff_num();
    for (int i = 0; i < n_coeffs; ++i) {
      coeff_[i] = c;
    }
  }

  void FillCoeffRandom() {
    const int n_coeffs = coeff_num();
    FillCoeffZero();
    int num = rnd_.Rand16() % n_coeffs;
    for (int i = 0; i < num; ++i) {
      coeff_[i] = GetRandomCoeff();
    }
  }

  void FillCoeffRandomRows(int num) {
    FillCoeffZero();
    for (int i = 0; i < num; ++i) {
      coeff_[i] = GetRandomCoeff();
    }
  }

  void FillCoeffZero() { FillCoeff(0); }

  void FillCoeffConstant() {
    tran_low_t c = GetRandomCoeff();
    FillCoeff(c);
  }

  void FillDcOnly() {
    FillCoeffZero();
    coeff_[0] = GetRandomCoeff();
  }

  void FillDcLargeNegative() {
    FillCoeffZero();
    // Generate a qcoeff which contains 512/-512 (0x0100/0xFE00) to catch issues
    // like BUG=883 where the constant being compared was incorrectly
    // initialized.
    coeff_[0] = -8191;
  }

  tran_low_t GetRandomCoeff() {
    tran_low_t coeff;
    if (bd_ == AVM_BITS_8) {
      coeff =
          clamp(static_cast<int16_t>(rnd_.Rand16()), INT16_MIN + 1, INT16_MAX);
    } else {
      tran_low_t min = -(1 << (7 + bd_));
      tran_low_t max = -min - 1;
      coeff = clamp(static_cast<tran_low_t>(rnd_.Rand31()), min, max);
    }
    return coeff;
  }

  ACMRandom rnd_;
  QuanTable *qtab_;
  tran_low_t *coeff_;
  QuantizeFuncHbd quant_ref_;
  QuantizeFuncHbd quant_;
  TX_SIZE tx_size_;
  QuantType type_;
  avm_bit_depth_t bd_;
  LogScale LogScale_;
};

TEST_P(QuantizeTest, ZeroInput) {
  FillCoeffZero();
  QuantizeRun(false);
}

TEST_P(QuantizeTest, LargeNegativeInput) {
  FillDcLargeNegative();
  QuantizeRun(false, 0, 1);
}

TEST_P(QuantizeTest, DcOnlyInput) {
  FillDcOnly();
  QuantizeRun(false, 0, 1);
}

TEST_P(QuantizeTest, RandomInput) { QuantizeRun(true, 0, kTestNum); }

TEST_P(QuantizeTest, MultipleQ) {
  for (int q = 0; q < (QINDEX_RANGE_8_BITS + (bd_ - AVM_BITS_8) * MAXQ_OFFSET);
       ++q) {
    QuantizeRun(true, q, kTestNum);
  }
}

// Force the coeff to be half the value of the dequant.
TEST_P(QuantizeTest, CoeffHalfDequant) {
  FillCoeff(16);
  QuantizeRun(false, 25, 1);
}

TEST_P(QuantizeTest, DISABLED_Speed) {
  tran_low_t *coeff_ptr = coeff_;
  const intptr_t n_coeffs = coeff_num();

  tran_low_t *qcoeff_ref = coeff_ptr + n_coeffs;
  tran_low_t *dqcoeff_ref = qcoeff_ref + n_coeffs;

  tran_low_t *qcoeff = dqcoeff_ref + n_coeffs;
  tran_low_t *dqcoeff = qcoeff + n_coeffs;
  uint16_t *eob = (uint16_t *)(dqcoeff + n_coeffs);

  // Testing uses 2-D DCT scan order table
  const SCAN_ORDER *const sc = get_default_scan(tx_size_, DCT_DCT);

  // Testing uses luminance quantization table
  const int q = 22;
  const int32_t *zbin = qtab_->quant.y_zbin[q];
  const int32_t *round_fp = qtab_->quant.y_round_fp[q];
  const int32_t *quant_fp = qtab_->quant.y_quant_fp[q];
  const int32_t *quant_shift = qtab_->quant.y_quant_shift[q];
  const int32_t *dequant = qtab_->dequant.y_dequant_QTX[q];
  const int log_scale = LogScale_;

  const int kNumTests = 5000000;
  avm_usec_timer timer, simd_timer;
  int rows = tx_size_high[tx_size_];
  int cols = tx_size_wide[tx_size_];
  rows = AVMMIN(32, rows);
  cols = AVMMIN(32, cols);
  for (int cnt = 0; cnt <= rows; cnt++) {
    FillCoeffRandomRows(cnt * cols);

    avm_usec_timer_start(&timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_ref_(0, coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift,
                 qcoeff, dqcoeff, dequant, eob, sc->scan, sc->iscan, log_scale);
    }
    avm_usec_timer_mark(&timer);

    avm_usec_timer_start(&simd_timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_(0, coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift,
             qcoeff, dqcoeff, dequant, eob, sc->scan, sc->iscan, log_scale);
    }
    avm_usec_timer_mark(&simd_timer);

    const int elapsed_time = static_cast<int>(avm_usec_timer_elapsed(&timer));
    const int simd_elapsed_time =
        static_cast<int>(avm_usec_timer_elapsed(&simd_timer));
    printf("c_time = %d \t simd_time = %d \t Gain = %f \n", elapsed_time,
           simd_elapsed_time, ((float)elapsed_time / simd_elapsed_time));
  }
}

using std::make_tuple;

#if HAVE_NEON
const QuantizeParam kQParamArrayNeon[] = {
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_8, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_12, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_8, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_12, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_8, LOGSCALE_2),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_12, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_8, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_12, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_8, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_12, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_8, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_12, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_4X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_8X4), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_8X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_16X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_32X16), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_32X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_neon,
             static_cast<TX_SIZE>(TX_64X32), TYPE_B, AVM_BITS_10, LOGSCALE_2),
};

INSTANTIATE_TEST_SUITE_P(NEON, QuantizeTest,
                         ::testing::ValuesIn(kQParamArrayNeon));
#endif  // HAVE_NEON

#if HAVE_AVX2
const QuantizeParam kQParamArrayAvx2[] = {
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_8, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_12, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_8, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_12, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_8, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_12, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_4X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_8X4), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_8X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X16), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X32), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_8, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AVM_BITS_12, LOGSCALE_0),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_8, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AVM_BITS_12, LOGSCALE_1),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_8, LOGSCALE_2),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AVM_BITS_12, LOGSCALE_2),
};

INSTANTIATE_TEST_SUITE_P(AVX2, QuantizeTest,
                         ::testing::ValuesIn(kQParamArrayAvx2));
#endif  // HAVE_AVX2

#if HAVE_SSE2
const QuantizeParam kQParamArraySSE2[] = {
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_8, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AVM_BITS_12, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_8, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AVM_BITS_12, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_8, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AVM_BITS_12, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_4X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_8X4), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_8X16), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X8), TYPE_B, AVM_BITS_10, LOGSCALE_0),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X32), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X16), TYPE_B, AVM_BITS_10, LOGSCALE_1),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X64), TYPE_B, AVM_BITS_10, LOGSCALE_2),
  make_tuple(&avm_highbd_quantize_b_c, &avm_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X32), TYPE_B, AVM_BITS_10, LOGSCALE_2),
};

INSTANTIATE_TEST_SUITE_P(SSE2, QuantizeTest,
                         ::testing::ValuesIn(kQParamArraySSE2));
#endif  // HAVE_SSE2

// avm_highbd_quantize_b against the C reference as
// av2_highbd_quantize_b_facade calls it: every TX size with the log_scale
// av2_get_tx_scale() gives it, and the av2_build_quantizer() tables.

typedef tuple<QuantizeFuncHbd, QuantizeFuncHbd, TX_SIZE, avm_bit_depth_t>
    QuantizeBParam;

const int kQbMaxCoeffs = 1024;
// Room for the +1 / +3 element offsets and a poisoned tail that catches
// writes past n_coeffs.
const int kQbBufElems = kQbMaxCoeffs + 64;
const int32_t kQbPoison = 0x5a5a5a5a;
// Just above the largest 12-bit fwd_txfm_c coefficient that
// quantize_b_bound_test.cc reaches (524160).
const tran_low_t kQbCoeffBound12 = 524288;
const int kQbRandomIters = 1000;
const int kQbDcDeltaQ = 24;

class QuantizeBTest : public ::testing::TestWithParam<QuantizeBParam> {
 protected:
  struct Params {
    const int32_t *zbin, *round, *quant, *quant_shift, *dequant;
  };

  void SetUp() override {
    ref_fn_ = GET_PARAM(0);
    tst_fn_ = GET_PARAM(1);
    tx_size_ = GET_PARAM(2);
    bd_ = GET_PARAM(3);
    n_coeffs_ = av2_get_max_eob(tx_size_);
    log_scale_ = av2_get_tx_scale(tx_size_);
    qindex_range_ = bd_ == AVM_BITS_8    ? QINDEX_RANGE_8_BITS
                    : bd_ == AVM_BITS_10 ? QINDEX_RANGE_10_BITS
                                         : QINDEX_RANGE;
    max_mag_ = kQbCoeffBound12 >> (12 - static_cast<int>(bd_));
    rnd_.Reset(ACMRandom::DeterministicSeed());

    qtab_ = static_cast<QuanTable *>(avm_memalign(32, sizeof(*qtab_)));
    ASSERT_NE(qtab_, nullptr);
    av2_build_quantizer(bd_, 0, 0, 0, 0, 0, 0, 0, 0, &qtab_->quant,
                        &qtab_->dequant);
    qtab_dc_ = static_cast<QuanTable *>(avm_memalign(32, sizeof(*qtab_dc_)));
    ASSERT_NE(qtab_dc_, nullptr);
    av2_build_quantizer(bd_, kQbDcDeltaQ, 0, 0, 0, 0, 0, 0, 0, &qtab_dc_->quant,
                        &qtab_dc_->dequant);
    qtab_dc_neg_ =
        static_cast<QuanTable *>(avm_memalign(32, sizeof(*qtab_dc_neg_)));
    ASSERT_NE(qtab_dc_neg_, nullptr);
    av2_build_quantizer(bd_, -kQbDcDeltaQ, 0, 0, 0, 0, 0, 0, 0,
                        &qtab_dc_neg_->quant, &qtab_dc_neg_->dequant);
    tran_low_t **bufs[5] = { &coeff_buf_, &ref_q_, &ref_dq_, &tst_q_,
                             &tst_dq_ };
    for (tran_low_t **b : bufs) {
      *b = static_cast<tran_low_t *>(
          avm_memalign(32, kQbBufElems * sizeof(tran_low_t)));
      ASSERT_NE(*b, nullptr);
    }
    coeff_ = coeff_buf_;

    // The primary tx types share the default, mrow and mcol scans.
    for (int t = 0; t < PRIMARY_TX_TYPES; ++t) {
      const SCAN_ORDER *sc = &av2_scan_orders[tx_size_][t];
      bool seen = false;
      for (const SCAN_ORDER *s : scans_) {
        seen = seen || (s->scan == sc->scan && s->iscan == sc->iscan);
      }
      if (!seen) scans_.push_back(sc);
    }
    ASSERT_FALSE(scans_.empty());
  }

  void TearDown() override {
    avm_free(qtab_);
    avm_free(qtab_dc_);
    avm_free(qtab_dc_neg_);
    avm_free(coeff_buf_);
    avm_free(ref_q_);
    avm_free(ref_dq_);
    avm_free(tst_q_);
    avm_free(tst_dq_);
    libavm_test::ClearSystemState();
  }

  static Params YOf(const QuanTable *t, int q) {
    return { t->quant.y_zbin[q], t->quant.y_round[q], t->quant.y_quant[q],
             t->quant.y_quant_shift[q], t->dequant.y_dequant_QTX[q] };
  }

  Params Y(int q) const { return YOf(qtab_, q); }

  int Zbin(const Params &p, int i) const {
    return ROUND_POWER_OF_TWO(p.zbin[i], log_scale_);
  }

  std::vector<int> QIndices() const {
    const int r = qindex_range_;
    return { 0, 1, r / 8, r / 4, r / 2, 3 * r / 4, r - 2, r - 1 };
  }

  std::string Where(int q, int scan_idx, int iter) const {
    std::ostringstream os;
    os << " tx=" << tx_size_wide[tx_size_] << "x" << tx_size_high[tx_size_]
       << " n_coeffs=" << n_coeffs_ << " bd=" << bd_
       << " log_scale=" << log_scale_ << " q=" << q << " scan=" << scan_idx
       << " iter=" << iter;
    return os.str();
  }

  void SetCoeffOffset(int coeff_off) {
    for (int i = 0; i < kQbBufElems; ++i) coeff_buf_[i] = 0;
    coeff_ = coeff_buf_ + coeff_off;
  }

  void Run(const Params &p, int q, int scan_idx, int iter, int q_off = 0,
           int dq_off = 0, int tst_boost = 0, uint16_t *eob_out = nullptr) {
    const SCAN_ORDER *sc = scans_[scan_idx];
    for (int i = 0; i < kQbBufElems; ++i) {
      ref_q_[i] = tst_q_[i] = kQbPoison;
      ref_dq_[i] = tst_dq_[i] = kQbPoison;
    }
    uint16_t eob_ref = 0xa5a5, eob_tst = 0x5a5a;
    ref_fn_(0, coeff_, n_coeffs_, p.zbin, p.round, p.quant, p.quant_shift,
            ref_q_ + q_off, ref_dq_ + dq_off, p.dequant, &eob_ref, sc->scan,
            sc->iscan, log_scale_);
    ASM_REGISTER_STATE_CHECK(
        tst_fn_(tst_boost, coeff_, n_coeffs_, p.zbin, p.round, p.quant,
                p.quant_shift, tst_q_ + q_off, tst_dq_ + dq_off, p.dequant,
                &eob_tst, sc->scan, sc->iscan, log_scale_));
    for (int j = 0; j < kQbBufElems; ++j) {
      const int pos = j - q_off;
      ASSERT_EQ(ref_q_[j], tst_q_[j])
          << "qcoeff mismatch at pos=" << pos
          << " coeff=" << (pos >= 0 && pos < n_coeffs_ ? coeff_[pos] : 0)
          << " q_off=" << q_off << " boost=" << tst_boost
          << Where(q, scan_idx, iter);
    }
    for (int j = 0; j < kQbBufElems; ++j) {
      const int pos = j - dq_off;
      ASSERT_EQ(ref_dq_[j], tst_dq_[j])
          << "dqcoeff mismatch at pos=" << pos
          << " coeff=" << (pos >= 0 && pos < n_coeffs_ ? coeff_[pos] : 0)
          << " dq_off=" << dq_off << " boost=" << tst_boost
          << Where(q, scan_idx, iter);
    }
    ASSERT_EQ(eob_ref, eob_tst)
        << "eob mismatch boost=" << tst_boost << Where(q, scan_idx, iter);
    if (eob_out) *eob_out = eob_ref;
  }

  void FillConstant(tran_low_t v) {
    for (int i = 0; i < n_coeffs_; ++i) coeff_[i] = v;
  }

  tran_low_t RandSign(tran_low_t mag) {
    return (rnd_.Rand8() & 1) ? -mag : mag;
  }

  void FillRandom(const Params &p, int scan_idx) {
    const SCAN_ORDER *sc = scans_[scan_idx];
    const int last =
        (rnd_.Rand8() & 1) ? rnd_.PseudoUniform(n_coeffs_ + 1) : n_coeffs_;
    for (int i = 0; i < n_coeffs_; ++i) {
      const int rc = sc->scan[i];
      const int ac = rc != 0;
      const int zb = Zbin(p, ac);
      const int dq = p.dequant[ac];
      tran_low_t mag = 0;
      const int r = rnd_.PseudoUniform(100);
      if (i >= last || r < 40) {
        mag = 0;
      } else if (r < 55) {
        mag = AVMMAX(0, zb - 2 + rnd_.PseudoUniform(5));
      } else if (r < 80) {
        mag = rnd_.PseudoUniform(AVMMAX(1, (8 * dq) >> QUANT_TABLE_BITS));
      } else if (r < 95) {
        mag = static_cast<tran_low_t>(rnd_.Rand31() % (max_mag_ + 1));
      } else {
        mag = max_mag_;
      }
      coeff_[rc] = RandSign(AVMMIN(mag, max_mag_));
    }
  }

  // One +/-max_mag_ coefficient at scan position scan_pos of each scan: eob
  // must be scan_pos + 1.
  void SingleNonzeroAt(int scan_pos) {
    SetCoeffOffset(0);
    for (const int q : QIndices()) {
      for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
        for (const int sign : { 1, -1 }) {
          FillConstant(0);
          coeff_[scans_[s]->scan[scan_pos]] = sign * max_mag_;
          uint16_t eob = 0;
          ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, sign, 0, 0, 0, &eob));
          ASSERT_EQ(eob, scan_pos + 1) << Where(q, s, sign);
        }
      }
    }
  }

  ACMRandom rnd_;
  QuantizeFuncHbd ref_fn_ = nullptr;
  QuantizeFuncHbd tst_fn_ = nullptr;
  TX_SIZE tx_size_ = TX_4X4;
  avm_bit_depth_t bd_ = AVM_BITS_8;
  int n_coeffs_ = 0;
  int log_scale_ = 0;
  int qindex_range_ = 0;
  tran_low_t max_mag_ = 0;
  QuanTable *qtab_ = nullptr;
  QuanTable *qtab_dc_ = nullptr;
  QuanTable *qtab_dc_neg_ = nullptr;
  tran_low_t *coeff_buf_ = nullptr;
  tran_low_t *coeff_ = nullptr;
  tran_low_t *ref_q_ = nullptr;
  tran_low_t *ref_dq_ = nullptr;
  tran_low_t *tst_q_ = nullptr;
  tran_low_t *tst_dq_ = nullptr;
  std::vector<const SCAN_ORDER *> scans_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(QuantizeBTest);

TEST_P(QuantizeBTest, ExtremesAllZero) {
  SetCoeffOffset(0);
  for (const int q : QIndices()) {
    for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
      uint16_t eob = 0;
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, 0, 0, 0, 0, &eob));
      ASSERT_EQ(eob, 0) << Where(q, s, 0);
    }
  }
}

// At q = 0 (the smallest dequant) this is also the accumulator stress: it
// maximizes tmp_rnd, abs_q and abs_q * dequant.
TEST_P(QuantizeBTest, ExtremesAllMaxMixedSigns) {
  SetCoeffOffset(0);
  for (const int q : QIndices()) {
    for (int pattern = 0; pattern < 4; ++pattern) {
      for (int i = 0; i < n_coeffs_; ++i) {
        const bool neg = pattern == 0   ? false
                         : pattern == 1 ? true
                         : pattern == 2 ? (i & 1) != 0
                                        : (rnd_.Rand8() & 1) != 0;
        coeff_[i] = neg ? -kQbCoeffBound12 : kQbCoeffBound12;
      }
      for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
        uint16_t eob = 0;
        ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, pattern, 0, 0, 0, &eob));
        ASSERT_EQ(eob, n_coeffs_) << Where(q, s, pattern);
      }
    }
  }
}

// Accumulator stress: covered by ExtremesAllMaxMixedSigns at q = 0.

// The IST and cross-chroma outputs are clamped to +/-(1 << (bd + 7)).
TEST_P(QuantizeBTest, BitDepthCoeffLimits) {
  SetCoeffOffset(0);
  const tran_low_t lim = (1 << (static_cast<int>(bd_) + 7)) - 1;
  for (const int q : QIndices()) {
    for (int pattern = 0; pattern < 4; ++pattern) {
      for (int i = 0; i < n_coeffs_; ++i) {
        switch (pattern) {
          case 0: coeff_[i] = lim; break;
          case 1: coeff_[i] = -lim - 1; break;
          case 2: coeff_[i] = (i & 1) ? -(lim + 1) / 2 : (lim + 1) / 2; break;
          default: coeff_[i] = (i & 1) ? 0 : ((i & 2) ? -lim : lim); break;
        }
      }
      for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
        ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, pattern));
      }
    }
  }
}

TEST_P(QuantizeBTest, ZbinEdgeDc) {
  SetCoeffOffset(0);
  for (int q = 0; q < qindex_range_; ++q) {
    const Params p = Y(q);
    const int zb = Zbin(p, 0);
    for (int d = -1; d <= 1; ++d) {
      for (const int sign : { 1, -1 }) {
        FillConstant(0);
        coeff_[0] = sign * (zb + d);
        uint16_t eob = 0;
        ASSERT_NO_FATAL_FAILURE(Run(p, q, 0, d, 0, 0, 0, &eob));
        if (d < 0) {
          ASSERT_EQ(eob, 0) << "below the DC zbin" << Where(q, 0, d);
        }
      }
    }
  }
}

TEST_P(QuantizeBTest, ZbinEdgeAc) {
  SetCoeffOffset(0);
  for (int q = 0; q < qindex_range_; ++q) {
    const Params p = Y(q);
    const int zb = Zbin(p, 1);
    for (int rot = 0; rot < 3; ++rot) {
      coeff_[0] = 0;
      for (int i = 1; i < n_coeffs_; ++i) {
        const int d = (i + rot) % 3 - 1;
        coeff_[i] = ((i + rot) & 1 ? -1 : 1) * (zb + d);
      }
      for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
        ASSERT_NO_FATAL_FAILURE(Run(p, q, s, rot));
      }
    }
    for (int i = 1; i < n_coeffs_; ++i) coeff_[i] = (i & 1 ? -1 : 1) * (zb - 1);
    uint16_t eob = 0;
    ASSERT_NO_FATAL_FAILURE(Run(p, q, 0, 3, 0, 0, 0, &eob));
    ASSERT_EQ(eob, 0) << "below the AC zbin" << Where(q, 0, 3);
  }
}

TEST_P(QuantizeBTest, SingleNonzeroLastScanPos) {
  SingleNonzeroAt(n_coeffs_ - 1);
}

TEST_P(QuantizeBTest, SingleNonzeroScanPos0) { SingleNonzeroAt(0); }

int RefAbsQ(int abs_coeff, int32_t round, int32_t quant, int32_t quant_shift,
            int log_scale) {
  const int64_t tmp1 = abs_coeff + ROUND_POWER_OF_TWO(round, log_scale);
  const int64_t tmp2 = ((tmp1 * quant) >> 16) + tmp1;
  return static_cast<int>((tmp2 * quant_shift) >> (16 - log_scale));
}

// Production rows are [8] (0 DC, 1 AC, 2..7 the AC repeated), and AVX2 loads
// all 8.
void FillParamRow(int32_t row[8], int32_t dc, int32_t ac) {
  row[0] = dc;
  for (int i = 1; i < 8; ++i) row[i] = ac;
}

// A coefficient at or above the zero bin can still quantize to 0 and must not
// move eob. The real tables may have no such value; with round = 0 a zero bin
// under one quantizer step always gives one.
TEST_P(QuantizeBTest, AboveZbinQuantizesToZero) {
  SetCoeffOffset(0);
  int32_t zbin[8], round[8], quant[8], quant_shift[8], dequant[8];
  int cases = 0;
  for (int q = 0; q < qindex_range_; ++q) {
    for (int crafted = 0; crafted <= 1; ++crafted) {
      const Params y = Y(q);
      FillParamRow(zbin, y.zbin[0], y.zbin[1]);
      FillParamRow(round, crafted ? 0 : y.round[0], crafted ? 0 : y.round[1]);
      FillParamRow(quant, y.quant[0], y.quant[1]);
      FillParamRow(quant_shift, y.quant_shift[0], y.quant_shift[1]);
      FillParamRow(dequant, y.dequant[0], y.dequant[1]);
      const Params p = { zbin, round, quant, quant_shift, dequant };
      for (int s = 0; s < static_cast<int>(scans_.size()); ++s) {
        const SCAN_ORDER *sc = scans_[s];
        const int pos = AVMMAX(1, (3 * n_coeffs_) / 4);
        const int rc = sc->scan[pos];
        const int ac = rc != 0;
        const int zb = Zbin(p, ac);
        int mag = -1;
        for (int c = zb; c < zb + p.dequant[ac]; ++c) {
          if (RefAbsQ(c, round[ac], quant[ac], quant_shift[ac], log_scale_) ==
              0)
            mag = c;
        }
        if (mag < 0) continue;
        for (const int lead : { -1, 0, pos / 2 }) {
          FillConstant(0);
          coeff_[rc] = (lead & 1) ? -mag : mag;
          if (lead >= 0) coeff_[sc->scan[lead]] = max_mag_;
          uint16_t eob = 0;
          ASSERT_NO_FATAL_FAILURE(Run(p, q, s, crafted, 0, 0, 0, &eob));
          ASSERT_EQ(eob, lead + 1)
              << "crafted=" << crafted << Where(q, s, lead);
          ++cases;
        }
      }
    }
  }
  ASSERT_GT(cases, 0) << "no zero-quantized above-zbin case"
                      << Where(-1, -1, 0);
}

// Stride edge cases: not applicable, the kernel has no stride argument.

TEST_P(QuantizeBTest, MisalignedCoeff) {
  for (const int off : { 1, 3 }) {
    SCOPED_TRACE(off);
    SetCoeffOffset(off);
    for (const int q : QIndices()) {
      FillRandom(Y(q), 0);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, 0, off));
    }
  }
}

TEST_P(QuantizeBTest, MisalignedOutputs) {
  SetCoeffOffset(0);
  for (const int off : { 1, 3 }) {
    SCOPED_TRACE(off);
    for (const int q : QIndices()) {
      FillRandom(Y(q), 0);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, 0, off, off, 0));
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, 0, off, 0, off));
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, 0, off, off, off));
    }
  }
}

TEST_P(QuantizeBTest, MisalignedCoeffAndOutputs) {
  const int offs[][3] = { { 1, 1, 1 }, { 3, 3, 3 }, { 1, 3, 1 }, { 3, 1, 3 } };
  for (const auto &o : offs) {
    SCOPED_TRACE(testing::Message() << o[0] << "," << o[1] << "," << o[2]);
    SetCoeffOffset(o[0]);
    for (const int q : QIndices()) {
      FillRandom(Y(q), 0);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, 0, o[0], o[1], o[2]));
    }
  }
}

// The C reference ignores use_tcq_deadzone_boost and the facade passes 0.
TEST_P(QuantizeBTest, TcqDeadzoneBoostIgnored) {
  SetCoeffOffset(0);
  for (const int q : QIndices()) {
    for (int i = 0; i < 64; ++i) {
      const int s = i % static_cast<int>(scans_.size());
      FillRandom(Y(q), s);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, i, 0, 0, 1));
      const SCAN_ORDER *sc = scans_[s];
      const Params p = Y(q);
      uint16_t eob0 = 0, eob1 = 0;
      ref_fn_(0, coeff_, n_coeffs_, p.zbin, p.round, p.quant, p.quant_shift,
              ref_q_, ref_dq_, p.dequant, &eob0, sc->scan, sc->iscan,
              log_scale_);
      ref_fn_(1, coeff_, n_coeffs_, p.zbin, p.round, p.quant, p.quant_shift,
              tst_q_, tst_dq_, p.dequant, &eob1, sc->scan, sc->iscan,
              log_scale_);
      ASSERT_EQ(eob0, eob1) << Where(q, s, i);
      for (int j = 0; j < n_coeffs_; ++j) {
        ASSERT_EQ(ref_q_[j], tst_q_[j]) << "pos=" << j << Where(q, s, i);
        ASSERT_EQ(ref_dq_[j], tst_dq_[j]) << "pos=" << j << Where(q, s, i);
      }
    }
  }
}

TEST_P(QuantizeBTest, RandomBitExact) {
  SetCoeffOffset(0);
  for (const int q : QIndices()) {
    for (int i = 0; i < kQbRandomIters; ++i) {
      const int s = i % static_cast<int>(scans_.size());
      FillRandom(Y(q), s);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, i));
    }
  }
}

// Tables built with all delta-q 0 have DC == AC, so only a DC delta-q shows a
// lane that reads the wrong half of the table.
TEST_P(QuantizeBTest, DcDiffersFromAc) {
  SetCoeffOffset(0);
  int differ = 0;
  std::vector<std::pair<const QuanTable *, int>> qs;
  for (const int q : QIndices()) qs.emplace_back(qtab_dc_, q);
  // At 12-bit only q 305..328 at delta +24 has DC s == 16 and AC s < 16, and
  // only q 329..351 at -24 the reverse, so only there does the DC or the AC
  // fallback check decide the path.
  if (bd_ == AVM_BITS_12) {
    qs.emplace_back(qtab_dc_, 316);
    qs.emplace_back(qtab_dc_neg_, 340);
  }
  for (const auto &t : qs) {
    const int q = t.second;
    const Params p = YOf(t.first, q);
    differ += p.dequant[0] != p.dequant[1];
    for (int i = 0; i < 64; ++i) {
      const int s = i % static_cast<int>(scans_.size());
      FillRandom(p, s);
      ASSERT_NO_FATAL_FAILURE(Run(p, q, s, i));
    }
  }
  ASSERT_GT(differ, 0) << Where(-1, -1, 0);
}

TEST_P(QuantizeBTest, RandomEveryQIndex) {
  SetCoeffOffset(0);
  for (int q = 0; q < qindex_range_; ++q) {
    for (int i = 0; i < 8; ++i) {
      const int s = i % static_cast<int>(scans_.size());
      FillRandom(Y(q), s);
      ASSERT_NO_FATAL_FAILURE(Run(Y(q), q, s, i));
    }
  }
}

TEST_P(QuantizeBTest, DISABLED_Speed) {
  SetCoeffOffset(0);
  const int q = qindex_range_ / 2;
  const Params p = Y(q);
  const SCAN_ORDER *sc = scans_[0];
  FillRandom(p, 0);
  const int kNumIter = 2000000 / AVMMAX(1, n_coeffs_ / 16);
  uint16_t eob = 0;
  avm_usec_timer timer_ref;
  avm_usec_timer_start(&timer_ref);
  for (int i = 0; i < kNumIter; ++i) {
    ref_fn_(0, coeff_, n_coeffs_, p.zbin, p.round, p.quant, p.quant_shift,
            ref_q_, ref_dq_, p.dequant, &eob, sc->scan, sc->iscan, log_scale_);
  }
  avm_usec_timer_mark(&timer_ref);
  avm_usec_timer timer_tst;
  avm_usec_timer_start(&timer_tst);
  for (int i = 0; i < kNumIter; ++i) {
    tst_fn_(0, coeff_, n_coeffs_, p.zbin, p.round, p.quant, p.quant_shift,
            tst_q_, tst_dq_, p.dequant, &eob, sc->scan, sc->iscan, log_scale_);
  }
  avm_usec_timer_mark(&timer_tst);
  const double t_ref = static_cast<double>(avm_usec_timer_elapsed(&timer_ref));
  const double t_tst = static_cast<double>(avm_usec_timer_elapsed(&timer_tst));
  printf(
      "quantize_b %dx%d n=%d ls=%d bd=%d q=%d: C=%7.1fms SIMD=%7.1fms "
      "gain=%.2fx\n",
      tx_size_wide[tx_size_], tx_size_high[tx_size_], n_coeffs_, log_scale_,
      bd_, q, t_ref / 1000.0, t_tst / 1000.0, t_ref / t_tst);
}

const TX_SIZE kQbTxSizes[] = {
  TX_4X4,  TX_8X8,  TX_16X16, TX_32X32, TX_64X64, TX_4X8,   TX_8X4,
  TX_8X16, TX_16X8, TX_16X32, TX_32X16, TX_32X64, TX_64X32, TX_4X16,
  TX_16X4, TX_8X32, TX_32X8,  TX_16X64, TX_64X16, TX_4X32,  TX_32X4,
  TX_8X64, TX_64X8, TX_4X64,  TX_64X4,
};
const avm_bit_depth_t kQbBitDepths[] = { AVM_BITS_8, AVM_BITS_10, AVM_BITS_12 };

// C vs C: a harness sanity baseline, not SIMD coverage.
INSTANTIATE_TEST_SUITE_P(
    C, QuantizeBTest,
    ::testing::Combine(::testing::Values(&avm_highbd_quantize_b_c),
                       ::testing::Values(&avm_highbd_quantize_b_c),
                       ::testing::ValuesIn(kQbTxSizes),
                       ::testing::ValuesIn(kQbBitDepths)));

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, QuantizeBTest,
    ::testing::Combine(::testing::Values(&avm_highbd_quantize_b_c),
                       ::testing::Values(&avm_highbd_quantize_b_neon),
                       ::testing::ValuesIn(kQbTxSizes),
                       ::testing::ValuesIn(kQbBitDepths)));
#endif  // HAVE_NEON

#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, QuantizeBTest,
    ::testing::Combine(::testing::Values(&avm_highbd_quantize_b_c),
                       ::testing::Values(&avm_highbd_quantize_b_sse2),
                       ::testing::ValuesIn(kQbTxSizes),
                       ::testing::ValuesIn(kQbBitDepths)));
#endif  // HAVE_SSE2

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, QuantizeBTest,
    ::testing::Combine(::testing::Values(&avm_highbd_quantize_b_c),
                       ::testing::Values(&avm_highbd_quantize_b_avx2),
                       ::testing::ValuesIn(kQbTxSizes),
                       ::testing::ValuesIn(kQbBitDepths)));
#endif  // HAVE_AVX2

}  // namespace
