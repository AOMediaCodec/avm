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
#include <stdlib.h>

#include <memory>
#include <string>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "av2/common/scan.h"
#include "av2/encoder/av2_quantize.h"
#include "avm_mem/avm_mem.h"

namespace {

typedef void (*QuantizeFpFunc)(
    int use_tcq_deadzone_boost, const tran_low_t *coeff_ptr, intptr_t count,
    const int32_t *zbin_ptr, const int32_t *round_ptr, const int32_t *quant_ptr,
    const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, int log_scale);

struct QuantizeFuncParams {
  QuantizeFuncParams(QuantizeFpFunc qF = NULL, QuantizeFpFunc qRefF = NULL,
                     int count = 16, int log_scale = -1)
      : qFunc(qF), qFuncRef(qRefF), coeffCount(count), logScale(log_scale) {}
  QuantizeFpFunc qFunc;
  QuantizeFpFunc qFuncRef;
  int coeffCount;
  int logScale;
};

using libavm_test::ACMRandom;

const int numTests = 1000;
const int maxSize = 1024;
const int roundFactorRange = 64;
const int dequantRange = 1048576;
const int coeffRange = (1 << 20) - 1;

tran_low_t RandSignedCoeff(ACMRandom *rnd) {
  return static_cast<tran_low_t>((*rnd)(2 * coeffRange + 1)) - coeffRange;
}

// The NEON kernel's int32 quant << (11 + log_scale) needs dequant >
// (8 << log_scale). Real quantizer tables start at 40.
int32_t RandDequant(ACMRandom *rnd, int log_scale) {
  const int32_t min_dequant = (8 << log_scale) + 1;
  return min_dequant + (*rnd)(dequantRange - min_dequant);
}

struct AvmFreeDeleter {
  void operator()(tran_low_t *p) const { avm_free(p); }
};
typedef std::unique_ptr<tran_low_t, AvmFreeDeleter> AlignedCoeffs;

tran_low_t *AllocCoeffs(int n) {
  return static_cast<tran_low_t *>(avm_memalign(16, sizeof(tran_low_t) * n));
}

class AV2QuantizeTest : public ::testing::TestWithParam<QuantizeFuncParams> {
 public:
  void RunQuantizeTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, tran_low_t, coeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, tran_low_t, qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);
    uint16_t eob;
    uint16_t ref_eob;
    int err_count_total = 0;
    int first_failure = -1;
    int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const int log_scale = getLogScale(txSize);
    QuantizeFpFunc quanFunc = params_.qFunc;
    QuantizeFpFunc quanFuncRef = params_.qFuncRef;

    const SCAN_ORDER scanOrder = getScanOrder(txSize);
    for (int i = 0; i < numTests; i++) {
      int err_count = 0;
      ref_eob = eob = UINT16_MAX;
      for (int j = 0; j < count; j++) {
        coeff_ptr[j] = RandSignedCoeff(&rnd);
      }

      FillRandomParams(&rnd, log_scale, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, dequant_ptr);
      for (int boost = 0; boost < 2; ++boost) {
        ref_eob = eob = UINT16_MAX;
        quanFuncRef(boost, coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                    quant_shift_ptr, ref_qcoeff_ptr, ref_dqcoeff_ptr,
                    dequant_ptr, &ref_eob, scanOrder.scan, scanOrder.iscan,
                    log_scale);

        ASM_REGISTER_STATE_CHECK(
            quanFunc(boost, coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                     quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                     &eob, scanOrder.scan, scanOrder.iscan, log_scale));

        for (int j = 0; j < count; ++j) {
          err_count += (ref_qcoeff_ptr[j] != qcoeff_ptr[j]) |
                       (ref_dqcoeff_ptr[j] != dqcoeff_ptr[j]);
          ASSERT_EQ(ref_qcoeff_ptr[j], qcoeff_ptr[j])
              << "qcoeff error: i = " << i << " j = " << j
              << " boost = " << boost << "\n";
          EXPECT_EQ(ref_dqcoeff_ptr[j], dqcoeff_ptr[j])
              << "dqcoeff error: i = " << i << " j = " << j
              << " boost = " << boost << "\n";
        }
        EXPECT_EQ(ref_eob, eob)
            << "eob error: " << "i = " << i << " boost = " << boost << "\n";
        err_count += (ref_eob != eob);
      }
      if (err_count && !err_count_total) {
        first_failure = i;
      }
      err_count_total += err_count;
    }
    EXPECT_EQ(0, err_count_total)
        << "Error: Quantization Test, C output doesn't match optimized output. "
        << "First failed at test case " << first_failure;
  }

  void RunEobTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, tran_low_t, coeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, tran_low_t, qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);
    uint16_t eob;
    uint16_t ref_eob;
    int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const int log_scale = getLogScale(txSize);
    QuantizeFpFunc quanFunc = params_.qFunc;
    QuantizeFpFunc quanFuncRef = params_.qFuncRef;
    const SCAN_ORDER scanOrder = getScanOrder(txSize);

    for (int i = 0; i < numTests; i++) {
      ref_eob = eob = UINT16_MAX;
      for (int j = 0; j < count; j++) {
        coeff_ptr[j] = 0;
      }

      coeff_ptr[rnd(count)] = RandSignedCoeff(&rnd);
      coeff_ptr[rnd(count)] = RandSignedCoeff(&rnd);
      coeff_ptr[rnd(count)] = RandSignedCoeff(&rnd);

      FillRandomParams(&rnd, log_scale, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, dequant_ptr);

      for (int boost = 0; boost < 2; ++boost) {
        ref_eob = eob = UINT16_MAX;
        quanFuncRef(boost, coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                    quant_shift_ptr, ref_qcoeff_ptr, ref_dqcoeff_ptr,
                    dequant_ptr, &ref_eob, scanOrder.scan, scanOrder.iscan,
                    log_scale);

        ASM_REGISTER_STATE_CHECK(
            quanFunc(boost, coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                     quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr, dequant_ptr,
                     &eob, scanOrder.scan, scanOrder.iscan, log_scale));
        EXPECT_EQ(ref_eob, eob)
            << "eob error: " << "i = " << i << " boost = " << boost << "\n";
      }
    }
  }

  // q at nonzero multiples of 2^16 (12/14-bit content) catches a kernel that
  // narrows q to int16 before its nonzero test for eob.
  void RunHighBitDepthTest() {
    DECLARE_ALIGNED(16, tran_low_t, coeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, tran_low_t, qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);

    const int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const int log_scale = getLogScale(txSize);
    const SCAN_ORDER scanOrder = getScanOrder(txSize);

    const int32_t dq = 9 << log_scale;
    const int32_t quant = (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / dq;
    for (int j = 0; j < 8; j++) {
      dequant_ptr[j] = dq;
      quant_ptr[j] = quant;
      round_ptr[j] = 0;
      zbin_ptr[j] = 0;
      quant_shift_ptr[j] = 1 << 16;
    }

    const int shift = 16 - log_scale + QUANT_FP_BITS;
    const tran_low_t base = (tran_low_t)(((int64_t)65536 << shift) / quant);

    const tran_low_t targets[] = {
      (tran_low_t)(base - 1),     base,
      (tran_low_t)(base + 1),     (tran_low_t)(2 * base - 1),
      (tran_low_t)(2 * base),     (tran_low_t)(2 * base + 1),
      (tran_low_t)(3 * base - 1), (tran_low_t)(3 * base),
      (tran_low_t)(3 * base + 1),
    };
    const int n_targets = sizeof(targets) / sizeof(targets[0]);

    for (int t = 0; t < n_targets; t++) {
      memset(coeff_ptr, 0, sizeof(tran_low_t) * maxSize);
      coeff_ptr[0] = targets[t];
      if (count > 9) coeff_ptr[9] = targets[t];
      if (count > 17) coeff_ptr[17] = -targets[t];
      CompareOnInput(coeff_ptr, qcoeff_ptr, dqcoeff_ptr, ref_qcoeff_ptr,
                     ref_dqcoeff_ptr, zbin_ptr, round_ptr, quant_ptr,
                     quant_shift_ptr, dequant_ptr, log_scale, scanOrder,
                     "target=" + std::to_string(targets[t]));
      if (HasFatalFailure()) return;
    }
  }

  // round = dequant / 4 keeps qcoeff nonzero at the deadzone threshold, so an
  // off-by-one threshold shows up in qcoeff. The encoder's round leaves qcoeff
  // 0 just below the threshold, the case where a kernel may skip the zero-bin
  // mask.
  void RunDeadzoneBoundaryTest() {
    DECLARE_ALIGNED(16, tran_low_t, coeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, tran_low_t, qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);

    const int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const SCAN_ORDER scanOrder = getScanOrder(txSize);
    // Multiples of 9 << (3 + log_scale) put the boosted threshold on a value.
    const int32_t dequants[][2] = { { 41, 47 },      { 64, 100 },
                                    { 127, 128 },    { 288, 576 },
                                    { 1000, 1023 },  { 1152, 2304 },
                                    { 4097, 65537 }, { 300001, 77 } };

    for (int log_scale = 0; log_scale <= 2; ++log_scale) {
      const int zbin_shift = 1 + log_scale + QUANT_TABLE_BITS;
      for (int encoder_round = 0; encoder_round < 2; ++encoder_round) {
        for (const auto &dq : dequants) {
          for (int j = 0; j < 8; j++) {
            dequant_ptr[j] = dq[j != 0];
            quant_ptr[j] =
                (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / dequant_ptr[j];
            round_ptr[j] = encoder_round
                               ? (64 * dequant_ptr[j]) >> (7 + QUANT_TABLE_BITS)
                               : dequant_ptr[j] / 4;
            zbin_ptr[j] = 0;
            quant_shift_ptr[j] = 0;
          }
          for (int iter = 0; iter < 6; ++iter) {
            const int boosted = iter & 1;
            const int phase = iter >> 1;
            const int64_t num_scale = boosted ? 10 : 1;
            const int64_t den = (int64_t)(boosted ? 9 : 1) << zbin_shift;
            for (int j = 0; j < count; j++) {
              const int64_t t = (num_scale * dq[j != 0] + den - 1) / den;
              const int sign = ((j / 3) & 1) ? -1 : 1;
              coeff_ptr[j] =
                  static_cast<tran_low_t>(sign * (t + (j + phase) % 3 - 1));
            }
            CompareOnInput(
                coeff_ptr, qcoeff_ptr, dqcoeff_ptr, ref_qcoeff_ptr,
                ref_dqcoeff_ptr, zbin_ptr, round_ptr, quant_ptr,
                quant_shift_ptr, dequant_ptr, log_scale, scanOrder,
                "dq=" + std::to_string(dq[0]) + "," + std::to_string(dq[1]) +
                    " boosted_threshold=" + std::to_string(boosted) +
                    " phase=" + std::to_string(phase) +
                    " encoder_round=" + std::to_string(encoder_round));
            if (HasFatalFailure()) return;
          }
        }
      }
    }
  }

  static void FillRandomParams(ACMRandom *rnd, int log_scale, int32_t *zbin,
                               int32_t *round, int32_t *quant,
                               int32_t *quant_shift, int32_t *dequant) {
    for (int j = 0; j < 2; j++) {
      zbin[j] = rnd->Rand31();
      quant_shift[j] = rnd->Rand31();
      dequant[j] = RandDequant(rnd, log_scale);
      quant[j] = static_cast<int32_t>(
          (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / dequant[j]);
      round[j] = (abs((*rnd)(roundFactorRange + 1)) * dequant[j]) >>
                 (7 + QUANT_TABLE_BITS);
    }
    for (int j = 2; j < 8; ++j) {
      zbin[j] = zbin[1];
      quant_shift[j] = quant_shift[1];
      dequant[j] = dequant[1];
      quant[j] = quant[1];
      round[j] = round[1];
    }
  }

  void CompareOnInput(const tran_low_t *coeff, tran_low_t *qcoeff,
                      tran_low_t *dqcoeff, tran_low_t *ref_qcoeff,
                      tran_low_t *ref_dqcoeff, const int32_t *zbin,
                      const int32_t *round, const int32_t *quant,
                      const int32_t *quant_shift, const int32_t *dequant,
                      int log_scale, const SCAN_ORDER &scanOrder,
                      const std::string &what) {
    const int count = params_.coeffCount;
    for (int boost = 0; boost < 2; ++boost) {
      uint16_t eob = UINT16_MAX, ref_eob = UINT16_MAX;
      params_.qFuncRef(boost, coeff, count, zbin, round, quant, quant_shift,
                       ref_qcoeff, ref_dqcoeff, dequant, &ref_eob,
                       scanOrder.scan, scanOrder.iscan, log_scale);
      ASM_REGISTER_STATE_CHECK(params_.qFunc(
          boost, coeff, count, zbin, round, quant, quant_shift, qcoeff, dqcoeff,
          dequant, &eob, scanOrder.scan, scanOrder.iscan, log_scale));
      for (int j = 0; j < count; ++j) {
        ASSERT_EQ(ref_qcoeff[j], qcoeff[j])
            << "qcoeff error: " << what << " j=" << j << " count=" << count
            << " log_scale=" << log_scale << " boost=" << boost;
        ASSERT_EQ(ref_dqcoeff[j], dqcoeff[j])
            << "dqcoeff error: " << what << " j=" << j << " count=" << count
            << " log_scale=" << log_scale << " boost=" << boost;
      }
      ASSERT_EQ(ref_eob, eob)
          << "eob error: " << what << " count=" << count
          << " log_scale=" << log_scale << " boost=" << boost;
    }
  }

  void RunExtremesTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, tran_low_t, coeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, tran_low_t, qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_qcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, tran_low_t, ref_dqcoeff_ptr[maxSize]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);
    const int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const int log_scale = getLogScale(txSize);
    const SCAN_ORDER scanOrder = getScanOrder(txSize);
    static const char *const kPatterns[] = { "zero", "+max", "-max",
                                             "alternating", "checkerboard" };
    for (int iter = 0; iter < 100; ++iter) {
      FillRandomParams(&rnd, log_scale, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, dequant_ptr);
      for (int p = 0; p < 5; ++p) {
        for (int j = 0; j < count; ++j) {
          switch (p) {
            case 0: coeff_ptr[j] = 0; break;
            case 1: coeff_ptr[j] = coeffRange; break;
            case 2: coeff_ptr[j] = -coeffRange; break;
            case 3: coeff_ptr[j] = (j & 1) ? -coeffRange : coeffRange; break;
            default: coeff_ptr[j] = (j & 1) ? 0 : coeffRange; break;
          }
        }
        CompareOnInput(
            coeff_ptr, qcoeff_ptr, dqcoeff_ptr, ref_qcoeff_ptr, ref_dqcoeff_ptr,
            zbin_ptr, round_ptr, quant_ptr, quant_shift_ptr, dequant_ptr,
            log_scale, scanOrder,
            std::string(kPatterns[p]) + " iter=" + std::to_string(iter));
        if (HasFatalFailure()) return;
      }
    }
  }

  void RunMisalignedTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, int32_t, zbin_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, round_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, quant_shift_ptr[8]);
    DECLARE_ALIGNED(16, int32_t, dequant_ptr[8]);
    const int kPad = 4;
    AlignedCoeffs coeff_buf(AllocCoeffs(maxSize + kPad));
    AlignedCoeffs qcoeff_buf(AllocCoeffs(maxSize + kPad));
    AlignedCoeffs dqcoeff_buf(AllocCoeffs(maxSize + kPad));
    AlignedCoeffs ref_qcoeff_buf(AllocCoeffs(maxSize));
    AlignedCoeffs ref_dqcoeff_buf(AllocCoeffs(maxSize));
    tran_low_t *const coeff = coeff_buf.get();
    tran_low_t *const qcoeff = qcoeff_buf.get();
    tran_low_t *const dqcoeff = dqcoeff_buf.get();
    tran_low_t *const ref_qcoeff = ref_qcoeff_buf.get();
    tran_low_t *const ref_dqcoeff = ref_dqcoeff_buf.get();
    ASSERT_TRUE(coeff && qcoeff && dqcoeff && ref_qcoeff && ref_dqcoeff);
    const int count = params_.coeffCount;
    const TX_SIZE txSize = getTxSize(count);
    const int log_scale = getLogScale(txSize);
    const SCAN_ORDER scanOrder = getScanOrder(txSize);
    static const int kOffsets[][2] = { { 1, 0 }, { 3, 0 }, { 0, 1 },
                                       { 0, 3 }, { 1, 1 }, { 3, 3 } };
    for (const auto &off : kOffsets) {
      for (int iter = 0; iter < 200 && !HasFatalFailure(); ++iter) {
        FillRandomParams(&rnd, log_scale, zbin_ptr, round_ptr, quant_ptr,
                         quant_shift_ptr, dequant_ptr);
        for (int j = 0; j < count; ++j) {
          coeff[off[0] + j] =
              rnd(2) ? RandSignedCoeff(&rnd)
                     : static_cast<tran_low_t>(rnd(2 * 256 + 1)) - 256;
        }
        CompareOnInput(coeff + off[0], qcoeff + off[1], dqcoeff + off[1],
                       ref_qcoeff, ref_dqcoeff, zbin_ptr, round_ptr, quant_ptr,
                       quant_shift_ptr, dequant_ptr, log_scale, scanOrder,
                       "coeff+" + std::to_string(off[0]) + " out+" +
                           std::to_string(off[1]) +
                           " iter=" + std::to_string(iter));
      }
    }
  }

  virtual void SetUp() { params_ = GetParam(); }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

  virtual ~AV2QuantizeTest() {}

 private:
  // 256 at log_scale 1 is TX_8X64: av2_get_max_eob() keeps 8x32 of it.
  TX_SIZE getTxSize(int count) {
    switch (count) {
      case 16: return TX_4X4;
      case 32: return TX_4X8;
      case 64: return TX_8X8;
      case 128: return TX_8X16;
      case 256: return params_.logScale == 1 ? TX_8X64 : TX_16X16;
      case 512: return TX_16X32;
      case 1024: return TX_32X32;
      default: return TX_4X4;
    }
  }

  static SCAN_ORDER getScanOrder(TX_SIZE txSize) {
    return txSize < TX_SIZES ? av2_default_scan_orders[txSize]
                             : av2_scan_orders[txSize][DCT_DCT];
  }

  int getLogScale(TX_SIZE txSize) const {
    return params_.logScale >= 0 ? params_.logScale : (txSize == TX_32X32);
  }

  QuantizeFuncParams params_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AV2QuantizeTest);

TEST_P(AV2QuantizeTest, BitExactCheck) { RunQuantizeTest(); }
TEST_P(AV2QuantizeTest, EobVerify) { RunEobTest(); }
TEST_P(AV2QuantizeTest, HighBitDepthEob) { RunHighBitDepthTest(); }
TEST_P(AV2QuantizeTest, DeadzoneBoundary) { RunDeadzoneBoundaryTest(); }
TEST_P(AV2QuantizeTest, Extremes) { RunExtremesTest(); }
TEST_P(AV2QuantizeTest, Misaligned) { RunMisalignedTest(); }

// C vs C: a harness sanity check, not SIMD coverage.
const QuantizeFuncParams qfps_c[] = {
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 16),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 32,
                     0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 64),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 128,
                     0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 256),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 256,
                     1),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 512,
                     1),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c,
                     1024),
  QuantizeFuncParams(&av2_highbd_quantize_fp_c, &av2_highbd_quantize_fp_c, 1024,
                     2),
};

INSTANTIATE_TEST_SUITE_P(C, AV2QuantizeTest, ::testing::ValuesIn(qfps_c));

#if HAVE_SSE4_1
const QuantizeFuncParams qfps[] = {
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     16),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     64),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     256),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     1024),
  // Rectangular transforms, at the log_scale the encoder uses: 4x8 / 8x4
  // (32) and 8x16 / 16x8 (128) at 0; 16x32 / 32x16 / 16x64 / 64x16 (512) and
  // 8x64 / 64x8 (256) at 1.
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     32, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     128, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     512, 1),
  QuantizeFuncParams(&av2_highbd_quantize_fp_sse4_1, &av2_highbd_quantize_fp_c,
                     256, 1),
};

INSTANTIATE_TEST_SUITE_P(SSE4_1, AV2QuantizeTest, ::testing::ValuesIn(qfps));
#endif  // HAVE_SSE4_1

#if HAVE_AVX2
const QuantizeFuncParams qfps_avx2[] = {
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     16),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     64),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     256),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     1024),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     32, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     128, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     512, 1),
  QuantizeFuncParams(&av2_highbd_quantize_fp_avx2, &av2_highbd_quantize_fp_c,
                     256, 1),
};

INSTANTIATE_TEST_SUITE_P(AVX2, AV2QuantizeTest, ::testing::ValuesIn(qfps_avx2));
#endif  // HAVE_AVX2

#if HAVE_NEON
const QuantizeFuncParams qfps_neon[] = {
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     16),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     64),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     256),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     1024),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     1024, 2),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     32, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     128, 0),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     512, 1),
  QuantizeFuncParams(&av2_highbd_quantize_fp_neon, &av2_highbd_quantize_fp_c,
                     256, 1),
};

INSTANTIATE_TEST_SUITE_P(NEON, AV2QuantizeTest, ::testing::ValuesIn(qfps_neon));
#endif  // HAVE_NEON

}  // namespace
