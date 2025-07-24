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

#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_ports/aom_timer.h"
#include "av1/encoder/encoder.h"
#include "av1/common/scan.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

namespace {
using libaom_test::ACMRandom;

typedef void (*QuantizeFuncHbd)(
    const tran_low_t *coeff_ptr, intptr_t n_coeffs, const int32_t *zbin_ptr,
    const int32_t *round_ptr, const int32_t *quant_ptr,
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
              aom_bit_depth_t, LogScale>
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
    qtab_ = reinterpret_cast<QuanTable *>(aom_memalign(32, sizeof(*qtab_)));
    const int n_coeffs = coeff_num();
    coeff_ = reinterpret_cast<tran_low_t *>(
        aom_memalign(32, 6 * n_coeffs * sizeof(tran_low_t)));
    InitQuantizer();
  }

  virtual void TearDown() {
    aom_free(qtab_);
    qtab_ = NULL;
    aom_free(coeff_);
    coeff_ = NULL;
    libaom_test::ClearSystemState();
  }

  void InitQuantizer() {
    av1_build_quantizer(bd_, 0, 0, 0, 0, 0, 0, 0, 0, &qtab_->quant,
                        &qtab_->dequant
#if !CONFIG_TCQ_FOR_ALL_FRAMES
                        ,
                        0
#endif  // !CONFIG_TCQ_FOR_ALL_FRAMES
    );
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

      quant_ref_(coeff_ptr, n_coeffs, zbin, round, quant, quant_shift,
                 qcoeff_ref, dqcoeff_ref, dequant, &eob[0], sc->scan, sc->iscan,
                 log_scale);

      ASM_REGISTER_STATE_CHECK(quant_(coeff_ptr, n_coeffs, zbin, round, quant,
                                      quant_shift, qcoeff, dqcoeff, dequant,
                                      &eob[1], sc->scan, sc->iscan, log_scale));

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

  int coeff_num() const { return av1_get_max_eob(tx_size_); }

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
    if (bd_ == AOM_BITS_8) {
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
  aom_bit_depth_t bd_;
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
  for (int q = 0; q < (QINDEX_RANGE_8_BITS + (bd_ - AOM_BITS_8) * MAXQ_OFFSET);
       ++q) {
    QuantizeRun(true, q, kTestNum);
  }
}

// Force the coeff to be half the value of the dequant.  This exposes a
// mismatch found in av1_quantize_fp_sse2().
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
  aom_usec_timer timer, simd_timer;
  int rows = tx_size_high[tx_size_];
  int cols = tx_size_wide[tx_size_];
  rows = AOMMIN(32, rows);
  cols = AOMMIN(32, cols);
  for (int cnt = 0; cnt <= rows; cnt++) {
    FillCoeffRandomRows(cnt * cols);

    aom_usec_timer_start(&timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_ref_(coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift,
                 qcoeff, dqcoeff, dequant, eob, sc->scan, sc->iscan, log_scale);
    }
    aom_usec_timer_mark(&timer);

    aom_usec_timer_start(&simd_timer);
    for (int n = 0; n < kNumTests; ++n) {
      quant_(coeff_ptr, n_coeffs, zbin, round_fp, quant_fp, quant_shift, qcoeff,
             dqcoeff, dequant, eob, sc->scan, sc->iscan, log_scale);
    }
    aom_usec_timer_mark(&simd_timer);

    const int elapsed_time = static_cast<int>(aom_usec_timer_elapsed(&timer));
    const int simd_elapsed_time =
        static_cast<int>(aom_usec_timer_elapsed(&simd_timer));
    printf("c_time = %d \t simd_time = %d \t Gain = %f \n", elapsed_time,
           simd_elapsed_time, ((float)elapsed_time / simd_elapsed_time));
  }
}

using std::make_tuple;

#if HAVE_AVX2
const QuantizeParam kQParamArrayAvx2[] = {
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_10, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8, LOGSCALE_2),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_10, LOGSCALE_2),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12, LOGSCALE_2),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_8, LOGSCALE_0),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_10, LOGSCALE_0),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_FP, AOM_BITS_12, LOGSCALE_0),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_8, LOGSCALE_1),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_10, LOGSCALE_1),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_FP, AOM_BITS_12, LOGSCALE_1),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_8, LOGSCALE_2),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_10, LOGSCALE_2),
  make_tuple(&av1_highbd_quantize_fp_c, &av1_highbd_quantize_fp_avx2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_FP, AOM_BITS_12, LOGSCALE_2),
};

INSTANTIATE_TEST_SUITE_P(AVX2, QuantizeTest,
                         ::testing::ValuesIn(kQParamArrayAvx2));
#endif  // HAVE_AVX2

#if HAVE_SSE2
const QuantizeParam kQParamArraySSE2[] = {
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_8, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_10, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_16X16), TYPE_B, AOM_BITS_12, LOGSCALE_0),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_8, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_10, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_32X32), TYPE_B, AOM_BITS_12, LOGSCALE_1),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_8, LOGSCALE_2),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_10, LOGSCALE_2),
  make_tuple(&aom_highbd_quantize_b_c, &aom_highbd_quantize_b_sse2,
             static_cast<TX_SIZE>(TX_64X64), TYPE_B, AOM_BITS_12, LOGSCALE_2),
};

INSTANTIATE_TEST_SUITE_P(SSE2, QuantizeTest,
                         ::testing::ValuesIn(kQParamArraySSE2));
#endif  // HAVE_SSE2

}  // namespace
