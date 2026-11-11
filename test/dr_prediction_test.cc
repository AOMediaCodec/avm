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

#include <cstring>
#include <tuple>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_mem/avm_mem.h"
#include "avm_ports/avm_timer.h"
#include "av2/common/blockd.h"
#include "av2/common/pred_common.h"
#include "av2/common/reconintra.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

namespace {

const int kZ1Start = 0;
const int kZ2Start = 90;
const int kZ3Start = 180;

const TX_SIZE kTxSize[] = {
  TX_4X4,  TX_8X8,  TX_16X16, TX_32X32, TX_64X64, TX_4X8,   TX_8X4,
  TX_8X16, TX_16X8, TX_16X32, TX_32X16, TX_32X64, TX_64X32, TX_4X16,
  TX_16X4, TX_8X32, TX_32X8,  TX_16X64, TX_64X16, TX_4X32,  TX_32X4,
  TX_8X64, TX_64X8, TX_4X64,  TX_64X4,
};

const char *const kTxSizeStrings[] = {
  "TX_4X4",   "TX_8X8",   "TX_16X16", "TX_32X32", "TX_64X64",
  "TX_4X8",   "TX_8X4",   "TX_8X16",  "TX_16X8",  "TX_16X32",
  "TX_32X16", "TX_32X64", "TX_64X32", "TX_4X16",  "TX_16X4",
  "TX_8X32",  "TX_32X8",  "TX_16X64", "TX_64X16", "TX_4X32",
  "TX_32X4",  "TX_8X64",  "TX_64X8",  "TX_4X64",  "TX_64X4",
};

using libavm_test::ACMRandom;

typedef void (*DrPred_Hbd)(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                           const uint16_t *above, const uint16_t *left, int dx,
                           int dy, int bd, int mrl_index);

typedef void (*Z1_Lbd)(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint8_t *above, const uint8_t *left, int dx,
                       int dy, int mrl_index);
template <Z1_Lbd fn>
void z1_wrapper(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                const uint8_t *above, const uint8_t *left, int dx, int dy,
                int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, mrl_index);
}

typedef void (*Z2_Lbd)(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint8_t *above, const uint8_t *left, int dx,
                       int dy, int mrl_index);
template <Z2_Lbd fn>
void z2_wrapper(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                const uint8_t *above, const uint8_t *left, int dx, int dy,
                int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, mrl_index);
}

typedef void (*Z3_Lbd)(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint8_t *above, const uint8_t *left, int dx,
                       int dy, int mrl_index);
template <Z3_Lbd fn>
void z3_wrapper(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                const uint8_t *above, const uint8_t *left, int dx, int dy,
                int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, mrl_index);
}

typedef void (*Z1_Hbd)(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint16_t *above, const uint16_t *left, int dx,
                       int dy, int bd, int mrl_index);
template <Z1_Hbd fn>
void z1_wrapper_hbd(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                    const uint16_t *above, const uint16_t *left, int dx, int dy,
                    int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, bd, mrl_index);
}

typedef void (*Z2_Hbd)(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint16_t *above, const uint16_t *left, int dx,
                       int dy, int bd, int mrl_index);
template <Z2_Hbd fn>
void z2_wrapper_hbd(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                    const uint16_t *above, const uint16_t *left, int dx, int dy,
                    int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, bd, mrl_index);
}

typedef void (*Z3_Hbd)(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                       const uint16_t *above, const uint16_t *left, int dx,
                       int dy, int bd, int mrl_index);
template <Z3_Hbd fn>
void z3_wrapper_hbd(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                    const uint16_t *above, const uint16_t *left, int dx, int dy,
                    int bd, int mrl_index) {
  (void)bd;
  fn(dst, stride, bw, bh, above, left, dx, dy, bd, mrl_index);
}

typedef void (*IDIF_Hbd)(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                         const uint16_t *above, const uint16_t *left, int dx,
                         int dy, int bd, int mrl_index);
template <IDIF_Hbd fn>
void idif_wrapper_hbd(uint16_t *dst, ptrdiff_t stride, int bw, int bh,
                      const uint16_t *above, const uint16_t *left, int dx,
                      int dy, int bd, int mrl_index) {
  fn(dst, stride, bw, bh, above, left, dx, dy, bd, mrl_index);
}

template <typename FuncType>
struct DrPredFunc {
  DrPredFunc(FuncType pred = NULL, FuncType tst = NULL, int bit_depth_value = 0,
             int start_angle_value = 0)
      : ref_fn(pred), tst_fn(tst), bit_depth(bit_depth_value),
        start_angle(start_angle_value) {}

  FuncType ref_fn;
  FuncType tst_fn;
  int bit_depth;
  int start_angle;
};

template <typename Pixel, typename FuncType>
class DrPredTest : public ::testing::TestWithParam<DrPredFunc<FuncType> > {
 protected:
  static const int kMaxNumTests = 10000;
  static const int kIterations = 10;
  static const int kDstStride = 64;
  static const int kDstSize = kDstStride * kDstStride;
  static const int kOffset = 16;
  static const int kBufSize = ((2 * MAX_TX_SIZE) << 1) + 16;

  DrPredTest() : bw_(0), bh_(0), dx_(1), dy_(1), bd_(8), txsize_(TX_4X4) {
    params_ = this->GetParam();
    start_angle_ = params_.start_angle;
    stop_angle_ = start_angle_ + 90;

    dst_ref_ = &dst_ref_data_[0];
    dst_tst_ = &dst_tst_data_[0];
    dst_stride_ = kDstStride;
    above_ = &above_data_[kOffset];
    left_ = &left_data_[kOffset];

    for (int i = 0; i < kBufSize; ++i) {
      above_data_[i] = rng_.Rand8();
      left_data_[i] = rng_.Rand8();
    }

    for (int i = 0; i < kDstSize; ++i) {
      dst_ref_[i] = 0;
      dst_tst_[i] = 0;
    }
  }

  virtual ~DrPredTest() {}

  void Predict(bool speedtest, int tx) {
    const int kNumTests = speedtest ? kMaxNumTests : 1;
    avm_usec_timer timer;
    int tst_time = 0;

    bd_ = params_.bit_depth;

    avm_usec_timer_start(&timer);
    for (int mrl_index_ = 0; mrl_index_ < MRL_LINE_NUMBER; ++mrl_index_) {
      for (int k = 0; k < kNumTests; ++k) {
        params_.ref_fn(dst_ref_, dst_stride_, bw_, bh_, above_, left_, dx_, dy_,
                       bd_, mrl_index_);
      }
    }

    avm_usec_timer_mark(&timer);
    const int ref_time = static_cast<int>(avm_usec_timer_elapsed(&timer));

    if (params_.tst_fn) {
      avm_usec_timer_start(&timer);
      for (int mrl_index_ = 0; mrl_index_ < MRL_LINE_NUMBER; ++mrl_index_) {
        for (int k = 0; k < kNumTests; ++k) {
          ASM_REGISTER_STATE_CHECK(params_.tst_fn(dst_tst_, dst_stride_, bw_,
                                                  bh_, above_, left_, dx_, dy_,
                                                  bd_, mrl_index_));
        }
      }
      avm_usec_timer_mark(&timer);
      tst_time = static_cast<int>(avm_usec_timer_elapsed(&timer));
    } else {
      for (int i = 0; i < kDstSize; ++i) {
        dst_ref_[i] = dst_tst_[i];
      }
    }

    OutputTimes(kNumTests, ref_time, tst_time, tx);
  }

  void RunTest(bool speedtest, bool needsaturation) {
    bd_ = params_.bit_depth;

    if (needsaturation) {
      for (int i = 0; i < kBufSize; ++i) {
        above_data_[i] = left_data_[i] = (1 << bd_) - 1;
      }
    }
    for (int tx = 0; tx < TX_SIZES_ALL; ++tx) {
      if (params_.tst_fn == NULL) {
        for (int i = 0; i < kDstSize; ++i) {
          dst_tst_[i] = (1 << bd_) - 1;
          dst_ref_[i] = (1 << bd_) - 1;
        }
      } else {
        for (int i = 0; i < kDstSize; ++i) {
          dst_ref_[i] = 0;
          dst_tst_[i] = 0;
        }
      }

      bw_ = tx_size_wide[kTxSize[tx]];
      bh_ = tx_size_high[kTxSize[tx]];

      Predict(speedtest, tx);

      for (int r = 0; r < bh_; ++r) {
        for (int c = 0; c < bw_; ++c) {
          ASSERT_EQ(dst_ref_[r * dst_stride_ + c],
                    dst_tst_[r * dst_stride_ + c])
              << bw_ << "x" << bh_ << " r: " << r << " c: " << c
              << " dx: " << dx_ << " dy: " << dy_;
        }
      }
    }
  }

  void OutputTimes(int num_tests, int ref_time, int tst_time, int tx) {
    if (num_tests > 1) {
      if (params_.tst_fn) {
        const float x = static_cast<float>(ref_time) / tst_time;
        printf("\t[%8s] :: ref time %6d, tst time %6d     %3.2f\n",
               kTxSizeStrings[tx], ref_time, tst_time, x);
      } else {
        printf("\t[%8s] :: ref time %6d\n", kTxSizeStrings[tx], ref_time);
      }
    }
  }

  Pixel dst_ref_data_[kDstSize];
  Pixel dst_tst_data_[kDstSize];

  Pixel left_data_[kBufSize];
  Pixel dummy_data_[kBufSize];
  Pixel above_data_[kBufSize];

  Pixel *dst_ref_;
  Pixel *dst_tst_;
  Pixel *above_;
  Pixel *left_;
  int dst_stride_;

  int bw_;
  int bh_;
  int dx_;
  int dy_;
  int bd_;
  int mrl_index_;
  TX_SIZE txsize_;

  int start_angle_;
  int stop_angle_;

  ACMRandom rng_;

  DrPredFunc<FuncType> params_;
};

using std::make_tuple;

class HighbdDrPredTest : public DrPredTest<uint16_t, DrPred_Hbd> {};

TEST_P(HighbdDrPredTest, SaturatedValues) {
  for (int angle = start_angle_; angle < stop_angle_; ++angle) {
    dx_ = av2_get_dx(angle);
    dy_ = av2_get_dy(angle);
    if (dx_ && dy_) RunTest(false, true);
  }
}

TEST_P(HighbdDrPredTest, DISABLED_Speed) {
  const int angles[] = { 3, 45, 87 };
  for (int i = 0; i < 3; ++i) {
    const int angle = angles[i] + start_angle_;
    dx_ = av2_get_dx(angle);
    dy_ = av2_get_dy(angle);
    printf("angle: %d ~~~~~~~~~~~~~~~\n", angle);
    if (dx_ && dy_) RunTest(true, false);
  }
}

TEST_P(HighbdDrPredTest, OperationCheck) {
  if (params_.tst_fn == NULL) return;
  // const int angles[] = { 3, 45, 81, 87, 93, 100, 145, 187, 199, 260 };
  for (int angle = start_angle_; angle < stop_angle_; angle++) {
    dx_ = av2_get_dx(angle);
    dy_ = av2_get_dy(angle);
    if (dx_ && dy_) RunTest(false, false);
  }
}

INSTANTIATE_TEST_SUITE_P(
    C, HighbdDrPredTest,
    ::testing::Values(
        DrPredFunc<DrPred_Hbd>(&z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
                               NULL, AVM_BITS_8, kZ1Start),
        DrPredFunc<DrPred_Hbd>(&z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
                               NULL, AVM_BITS_10, kZ1Start),
        DrPredFunc<DrPred_Hbd>(&z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
                               NULL, AVM_BITS_12, kZ1Start),
        DrPredFunc<DrPred_Hbd>(&z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
                               NULL, AVM_BITS_8, kZ2Start),
        DrPredFunc<DrPred_Hbd>(&z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
                               NULL, AVM_BITS_10, kZ2Start),
        DrPredFunc<DrPred_Hbd>(&z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
                               NULL, AVM_BITS_12, kZ2Start),
        DrPredFunc<DrPred_Hbd>(&z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
                               NULL, AVM_BITS_8, kZ3Start),
        DrPredFunc<DrPred_Hbd>(&z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
                               NULL, AVM_BITS_10, kZ3Start),
        DrPredFunc<DrPred_Hbd>(&z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
                               NULL, AVM_BITS_12, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>, NULL,
            AVM_BITS_8, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>, NULL,
            AVM_BITS_10, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>, NULL,
            AVM_BITS_12, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>, NULL,
            AVM_BITS_8, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>, NULL,
            AVM_BITS_10, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>, NULL,
            AVM_BITS_12, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>, NULL,
            AVM_BITS_8, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>, NULL,
            AVM_BITS_10, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>, NULL,
            AVM_BITS_12, kZ2Start)));

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, HighbdDrPredTest,
    ::testing::Values(
        DrPredFunc<DrPred_Hbd>(
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_avx2>, AVM_BITS_8,
            kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_avx2>, AVM_BITS_10,
            kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_c>,
            &z1_wrapper_hbd<av2_highbd_dr_prediction_z1_avx2>, AVM_BITS_12,
            kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_avx2>, AVM_BITS_8,
            kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_avx2>, AVM_BITS_10,
            kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_c>,
            &z2_wrapper_hbd<av2_highbd_dr_prediction_z2_avx2>, AVM_BITS_12,
            kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_avx2>, AVM_BITS_8,
            kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_avx2>, AVM_BITS_10,
            kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_c>,
            &z3_wrapper_hbd<av2_highbd_dr_prediction_z3_avx2>, AVM_BITS_12,
            kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_avx2>,
            AVM_BITS_8, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_avx2>,
            AVM_BITS_10, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_avx2>,
            AVM_BITS_12, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_avx2>,
            AVM_BITS_8, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_avx2>,
            AVM_BITS_10, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_avx2>,
            AVM_BITS_12, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_avx2>,
            AVM_BITS_8, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_avx2>,
            AVM_BITS_10, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_avx2>,
            AVM_BITS_12, kZ2Start)));
#endif  // HAVE_AVX2

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, HighbdDrPredTest,
    ::testing::Values(
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_neon>,
            AVM_BITS_8, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_neon>,
            AVM_BITS_10, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_neon>,
            AVM_BITS_12, kZ1Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_neon>,
            AVM_BITS_8, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_neon>,
            AVM_BITS_10, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_neon>,
            AVM_BITS_12, kZ2Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_neon>,
            AVM_BITS_8, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_neon>,
            AVM_BITS_10, kZ3Start),
        DrPredFunc<DrPred_Hbd>(
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
            &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_neon>,
            AVM_BITS_12, kZ3Start)));
#endif  // HAVE_NEON

// ---------------------------------------------------------------------------
// IDIF family (z1_idif, z2_idif, z3_idif): edges laid out and padded the way
// production hands them to the kernel, and the whole dst buffer compared.
// ---------------------------------------------------------------------------

// av2_build_intra_predictors_high() (av2/common/reconintra.c): each edge is a
// NUM_INTRA_NEIGHBOUR_PIXELS (MAX_TX_SIZE * 2 + 64) buffer, 16-byte aligned,
// with the row/column pointer 32 elements in. The kernels may read any of it.
const int kIdifEdgeLead = 32;
const int kIdifEdgeLen = MAX_TX_SIZE * 2 + 64;
// Element offsets applied to the aligned bases (misalignment tests use +1, +3).
const int kIdifAlignSlack = 16;
const int kIdifEdgeBufLen = kIdifEdgeLen + kIdifAlignSlack;
// dst: the widest stride is width + kIdifStridePad at the largest block.
const int kIdifStridePad = 16;
const int kIdifMaxStride = MAX_TX_SIZE + kIdifStridePad;
const int kIdifDstBufLen = kIdifMaxStride * MAX_TX_SIZE + kIdifAlignSlack;
// Stride of the tests that do not vary it: neither == width nor width + 16.
const int kIdifDefaultStride = MAX_TX_SIZE + 8;
// Every dr_intra_derivative index (1..89; entry 0 is unused). Production
// reaches a subset of these angles; the C kernels accept them all.
const int kIdifNumDerivatives = 89;

struct IdifFuncs {
  DrPred_Hbd ref_fn;
  DrPred_Hbd tst_fn;
  int zone;  // 1, 2 or 3
};

// (C reference and implementation under test, tx size, bit depth)
typedef std::tuple<IdifFuncs, TX_SIZE, int> IdifParam;

class HighbdDrPredIdifTest : public ::testing::TestWithParam<IdifParam> {
 public:
  void SetUp() override {
    funcs_ = std::get<0>(GetParam());
    tx_size_ = std::get<1>(GetParam());
    bw_ = tx_size_wide[tx_size_];
    bh_ = tx_size_high[tx_size_];
    bd_ = std::get<2>(GetParam());
    mask_ = (1 << bd_) - 1;

    above_src_ = AllocU16(kIdifEdgeBufLen);
    left_src_ = AllocU16(kIdifEdgeBufLen);
    above_buf_ = AllocU16(kIdifEdgeBufLen);
    left_buf_ = AllocU16(kIdifEdgeBufLen);
    background_ = AllocU16(kIdifDstBufLen);
    ref_dst_ = AllocU16(kIdifDstBufLen);
    tst_dst_ = AllocU16(kIdifDstBufLen);
    ASSERT_NE(above_src_, nullptr);
    ASSERT_NE(left_src_, nullptr);
    ASSERT_NE(above_buf_, nullptr);
    ASSERT_NE(left_buf_, nullptr);
    ASSERT_NE(background_, nullptr);
    ASSERT_NE(ref_dst_, nullptr);
    ASSERT_NE(tst_dst_, nullptr);
    // Random 16-bit background for dst: a write outside the block almost
    // surely shows as a mismatch.
    for (int i = 0; i < kIdifDstBufLen; ++i) background_[i] = rng_.Rand16();
    FillRandomEdges();
  }

  void TearDown() override {
    avm_free(above_src_);
    avm_free(left_src_);
    avm_free(above_buf_);
    avm_free(left_buf_);
    avm_free(background_);
    avm_free(ref_dst_);
    avm_free(tst_dst_);
    libavm_test::ClearSystemState();
  }

 protected:
  static uint16_t *AllocU16(int n) {
    return static_cast<uint16_t *>(avm_memalign(16, n * sizeof(uint16_t)));
  }

  // Whole edge buffers (every element the kernel could read) from a pattern
  // of the element index i (i = kIdifEdgeLead is above[0] / left[0] at
  // offset 0). The production padding is applied per call, on a copy.
  template <typename AboveFn, typename LeftFn>
  void FillEdges(AboveFn above_val, LeftFn left_val) {
    for (int i = 0; i < kIdifEdgeBufLen; ++i) {
      above_src_[i] = static_cast<uint16_t>(above_val(i));
      left_src_[i] = static_cast<uint16_t>(left_val(i));
    }
  }

  void FillRandomEdges() {
    FillEdges([&](int) { return rng_.Rand16() & mask_; },
              [&](int) { return rng_.Rand16() & mask_; });
  }

  // (dx, dy) of derivative index a in 1..89 for this zone, as production
  // computes them: z1 angle a (av2_get_dx), z3 angle 270 - a (av2_get_dy; the
  // IBP second predictor passes the same dr_intra_derivative[90 - angle]), z2
  // angle 90 + a.
  void Derivatives(int a, int *dx, int *dy) const {
    if (funcs_.zone == 1) {
      *dx = dr_intra_derivative[a];
      *dy = 1;
    } else if (funcs_.zone == 3) {
      *dx = 1;
      *dy = dr_intra_derivative[a];
    } else {
      *dx = dr_intra_derivative[90 - a];
      *dy = dr_intra_derivative[a];
    }
  }

  // The +2 / -1 edge copies the C reference reads. highbd_dr_predictor_idif()
  // pads further for SIMD loads (BuilderWrittenEdges below); here those
  // samples stay random, which is stricter.
  void PadLikeProduction(uint16_t *above, uint16_t *left, int mrl) const {
    const int max_base_z1_z3 = (bw_ + bh_) - 1 + (mrl << 1);
    if (funcs_.zone == 1) {
      above[max_base_z1_z3 + 1] = above[max_base_z1_z3];
      above[max_base_z1_z3 + 2] = above[max_base_z1_z3];
    } else if (funcs_.zone == 2) {
      const int min_base_z2 = -(1 + mrl);
      above[min_base_z2 - 1] = above[min_base_z2];
      left[min_base_z2 - 1] = left[min_base_z2];
      if (mrl == 0) {
        above[bw_] = above[bw_ - 1];
        left[bh_] = left[bh_ - 1];
      }
    } else {
      left[max_base_z1_z3 + 1] = left[max_base_z1_z3];
      left[max_base_z1_z3 + 2] = left[max_base_z1_z3];
    }
  }

  // One call of C and of the implementation under test on the same padded
  // edges, the WHOLE dst buffer compared (a write outside the bw x bh block,
  // in the stride padding or before the base, fails). Callers in loops wrap
  // this in ASSERT_NO_FATAL_FAILURE.
  void RunOne(int a, int mrl, int stride, int dst_off, int above_off,
              int left_off, int iter) {
    int dx, dy;
    Derivatives(a, &dx, &dy);
    memcpy(above_buf_, above_src_, kIdifEdgeBufLen * sizeof(uint16_t));
    memcpy(left_buf_, left_src_, kIdifEdgeBufLen * sizeof(uint16_t));
    uint16_t *above = above_buf_ + kIdifEdgeLead + above_off;
    uint16_t *left = left_buf_ + kIdifEdgeLead + left_off;
    PadLikeProduction(above, left, mrl);
    memcpy(ref_dst_, background_, kIdifDstBufLen * sizeof(uint16_t));
    memcpy(tst_dst_, background_, kIdifDstBufLen * sizeof(uint16_t));

    funcs_.ref_fn(ref_dst_ + dst_off, stride, bw_, bh_, above, left, dx, dy,
                  bd_, mrl);
    ASM_REGISTER_STATE_CHECK(funcs_.tst_fn(tst_dst_ + dst_off, stride, bw_, bh_,
                                           above, left, dx, dy, bd_, mrl));

    if (memcmp(ref_dst_, tst_dst_, kIdifDstBufLen * sizeof(uint16_t)) == 0) {
      return;
    }
    for (int i = 0; i < kIdifDstBufLen; ++i) {
      const int pos = i - dst_off;
      const int r = pos >= 0 ? pos / stride : -1;
      const int c = pos >= 0 ? pos % stride : pos;
      const bool inside = r >= 0 && r < bh_ && c < bw_;
      ASSERT_EQ(ref_dst_[i], tst_dst_[i])
          << "z" << funcs_.zone << "_idif mismatch at r=" << r << " c=" << c
          << (inside ? " (in block)" : " (OUTSIDE block)") << " w=" << bw_
          << " h=" << bh_ << " bd=" << bd_ << " iter=" << iter << " dx=" << dx
          << " dy=" << dy << " mrl=" << mrl << " stride=" << stride
          << " dst_off=" << dst_off << " above_off=" << above_off
          << " left_off=" << left_off;
    }
  }

  // Every derivative this zone can receive, at every mrl_index, on the
  // current edges.
  void Sweep(int stride, int dst_off, int above_off, int left_off) {
    int iter = 0;
    for (int mrl = 0; mrl < MRL_LINE_NUMBER; ++mrl) {
      for (int a = 1; a <= kIdifNumDerivatives; ++a, ++iter) {
        ASSERT_NO_FATAL_FAILURE(
            RunOne(a, mrl, stride, dst_off, above_off, left_off, iter));
      }
    }
  }

  void SweepDefault() { Sweep(kIdifDefaultStride, 0, 0, 0); }

  IdifFuncs funcs_;
  TX_SIZE tx_size_;
  int bw_, bh_, bd_, mask_;
  ACMRandom rng_{ ACMRandom::DeterministicSeed() };
  uint16_t *above_src_ = nullptr;
  uint16_t *left_src_ = nullptr;
  uint16_t *above_buf_ = nullptr;
  uint16_t *left_buf_ = nullptr;
  uint16_t *background_ = nullptr;
  uint16_t *ref_dst_ = nullptr;
  uint16_t *tst_dst_ = nullptr;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HighbdDrPredIdifTest);

// -- Boundary: extremes (above and left both) --

TEST_P(HighbdDrPredIdifTest, ExtremesAllZero) {
  FillEdges([](int) { return 0; }, [](int) { return 0; });
  SweepDefault();
}

TEST_P(HighbdDrPredIdifTest, ExtremesAllMax) {
  FillEdges([&](int) { return mask_; }, [&](int) { return mask_; });
  SweepDefault();
}

// Two-input mix: above max, left zero (z2 blends both edges in one block).
TEST_P(HighbdDrPredIdifTest, ExtremesAboveMaxLeftZero) {
  FillEdges([&](int) { return mask_; }, [](int) { return 0; });
  SweepDefault();
}

TEST_P(HighbdDrPredIdifTest, ExtremesAboveZeroLeftMax) {
  FillEdges([](int) { return 0; }, [&](int) { return mask_; });
  SweepDefault();
}

// -- Boundary: accumulator stress --
// The 4-tap av2_dr_interp_filter rows are { -, +, +, - }. Period-4 edges
// 0, max, max, 0 put max under both positive taps and 0 under both negative
// ones for every pixel whose base falls on the period's phase (the sweep's
// per-row/column bases hit every phase): the largest positive sum, which
// must clip to max. The inverse, max, 0, 0, max, is the most negative sum,
// which must clip to 0.

TEST_P(HighbdDrPredIdifTest, AccumulatorStressPositive) {
  auto f = [&](int i) { return (i & 3) == 1 || (i & 3) == 2 ? mask_ : 0; };
  FillEdges(f, f);
  SweepDefault();
}

TEST_P(HighbdDrPredIdifTest, AccumulatorStressNegative) {
  auto f = [&](int i) { return (i & 3) == 1 || (i & 3) == 2 ? 0 : mask_; };
  FillEdges(f, f);
  SweepDefault();
}

// -- Boundary: bit-depth limits (all-max is ExtremesAllMax) --

TEST_P(HighbdDrPredIdifTest, BitDepthMidpoint) {
  const int mid = 1 << (bd_ - 1);
  FillEdges([&](int) { return mid; }, [&](int) { return mid; });
  SweepDefault();
}

// Alternating 0 / max per element, above and left in opposite phase.
TEST_P(HighbdDrPredIdifTest, BitDepthCheckerboard) {
  FillEdges([&](int i) { return (i & 1) ? mask_ : 0; },
            [&](int i) { return (i & 1) ? 0 : mask_; });
  SweepDefault();
}

// Ramps: above rising, left falling, both wrapping at the bit depth, so
// every lane of a vector load holds a distinct value.
TEST_P(HighbdDrPredIdifTest, EdgeRamp) {
  FillEdges([&](int i) { return (i * 37) & mask_; },
            [&](int i) { return mask_ - ((i * 23) & mask_); });
  SweepDefault();
}

// -- Boundary: stride edge cases --
// The kernels read 1-D edges and write one strided buffer (dst), so there is
// no second stride to differ from dst's.

TEST_P(HighbdDrPredIdifTest, StrideMinimum) { Sweep(bw_, 0, 0, 0); }

TEST_P(HighbdDrPredIdifTest, StridePadded) {
  Sweep(bw_ + kIdifStridePad, 0, 0, 0);
}

// -- Boundary: alignment (offsets in uint16_t elements) --

TEST_P(HighbdDrPredIdifTest, MisalignedAbove) {
  for (const int off : { 1, 3 }) {
    SCOPED_TRACE(off);
    ASSERT_NO_FATAL_FAILURE(Sweep(kIdifDefaultStride, 0, off, 0));
  }
}

TEST_P(HighbdDrPredIdifTest, MisalignedLeft) {
  for (const int off : { 1, 3 }) {
    SCOPED_TRACE(off);
    ASSERT_NO_FATAL_FAILURE(Sweep(kIdifDefaultStride, 0, 0, off));
  }
}

TEST_P(HighbdDrPredIdifTest, MisalignedDst) {
  for (const int off : { 1, 3 }) {
    SCOPED_TRACE(off);
    ASSERT_NO_FATAL_FAILURE(Sweep(kIdifDefaultStride, off, 0, 0));
  }
}

TEST_P(HighbdDrPredIdifTest, MisalignedEdgesAndDst) {
  ASSERT_NO_FATAL_FAILURE(Sweep(kIdifDefaultStride, 3, 1, 1));
  ASSERT_NO_FATAL_FAILURE(Sweep(kIdifDefaultStride, 1, 3, 3));
}

// -- Random: 1000 iterations per (zone, tx size, bit depth) --
// Each iteration draws fresh random edges and takes the next (derivative,
// mrl_index) pair in order, so the 89 x 4 = 356 pairs are each run 2-3 times
// on different edges: denser than one full derivative sweep (356 calls).

TEST_P(HighbdDrPredIdifTest, RandomBitExact) {
  const int kNumIter = 1000;
  for (int i = 0; i < kNumIter; ++i) {
    FillRandomEdges();
    const int a = 1 + i % kIdifNumDerivatives;
    const int mrl = (i / kIdifNumDerivatives) % MRL_LINE_NUMBER;
    ASSERT_NO_FATAL_FAILURE(RunOne(a, mrl, kIdifDefaultStride, 0, 0, 0, i));
  }
}

// -- Edges with only the samples production writes --
// av2_build_intra_predictors_high_default() (mrl 0) does not clear its edge
// buffers; with highbd_dr_predictor_idif()'s padding it writes z1 above[-1,
// max_base + 10], z3 left[-1, max_base + 10] and z2 above[-9, bw], left[-9,
// bh]. Each written range ends at a PROT_NONE page (low end, then high end),
// so any kernel load outside it faults (and would be uninitialized memory).
#if !defined(_WIN32)
// Not instantiated for AVX2: those kernels read past these ranges.
class HighbdDrPredIdifGuardTest : public HighbdDrPredIdifTest {};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(HighbdDrPredIdifGuardTest);

TEST_P(HighbdDrPredIdifGuardTest, BuilderWrittenEdges) {
  const int max_base = bw_ + bh_ - 1;
  int lo[2] = { 0, 0 }, hi[2] = { -1, -1 };  // above, left
  if (funcs_.zone == 1) {
    lo[0] = -1;
    hi[0] = max_base + 10;
  } else if (funcs_.zone == 3) {
    lo[1] = -1;
    hi[1] = max_base + 10;
  } else {
    lo[0] = lo[1] = -9;
    hi[0] = bw_;
    hi[1] = bh_;
  }
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const int n = static_cast<int>(page / sizeof(uint16_t));
  struct Maps {  // unmapped on every exit, including a failed ASSERT
    uint8_t *p[2] = { nullptr, nullptr };
    size_t len = 0;
    ~Maps() {
      for (uint8_t *m : p) {
        if (m) munmap(m, len);
      }
    }
  } maps;
  maps.len = 3 * page;
  uint8_t **map = maps.p;
  for (int e = 0; e < 2; ++e) {
    void *m = mmap(nullptr, 3 * page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    ASSERT_NE(m, MAP_FAILED);
    map[e] = static_cast<uint8_t *>(m);
    ASSERT_EQ(0, mprotect(map[e], page, PROT_NONE));
    ASSERT_EQ(0, mprotect(map[e] + 2 * page, page, PROT_NONE));
  }
  memcpy(above_buf_, above_src_, kIdifEdgeBufLen * sizeof(uint16_t));
  memcpy(left_buf_, left_src_, kIdifEdgeBufLen * sizeof(uint16_t));
  uint16_t *ref_edge[2] = { above_buf_ + kIdifEdgeLead,
                            left_buf_ + kIdifEdgeLead };
  const int stride = kIdifDefaultStride;
  for (int high = 0; high < 2; ++high) {
    uint16_t *edge[2];
    for (int e = 0; e < 2; ++e) {
      uint16_t *data = reinterpret_cast<uint16_t *>(map[e] + page);
      edge[e] = high ? data + n - 1 - hi[e] : data - lo[e];
    }
    for (int a = 1; a <= kIdifNumDerivatives; ++a) {
      for (int e = 0; e < 2; ++e) {
        for (int i = lo[e]; i <= hi[e]; ++i) {
          edge[e][i] = ref_edge[e][i] = rng_.Rand16() & mask_;
        }
      }
      int dx, dy;
      Derivatives(a, &dx, &dy);
      funcs_.ref_fn(ref_dst_, stride, bw_, bh_, ref_edge[0], ref_edge[1], dx,
                    dy, bd_, 0);
      funcs_.tst_fn(tst_dst_, stride, bw_, bh_, edge[0], edge[1], dx, dy, bd_,
                    0);
      for (int r = 0; r < bh_; ++r) {
        ASSERT_EQ(0, memcmp(ref_dst_ + r * stride, tst_dst_ + r * stride,
                            bw_ * sizeof(uint16_t)))
            << "z" << funcs_.zone << " w=" << bw_ << " h=" << bh_
            << " bd=" << bd_ << " dx=" << dx << " dy=" << dy << " high=" << high
            << " row " << r;
      }
    }
  }
}
#endif  // !defined(_WIN32)

// -- Speed --

TEST_P(HighbdDrPredIdifTest, DISABLED_Speed) {
  const int kNumSweeps = 200;
  FillRandomEdges();
  memcpy(above_buf_, above_src_, kIdifEdgeBufLen * sizeof(uint16_t));
  memcpy(left_buf_, left_src_, kIdifEdgeBufLen * sizeof(uint16_t));
  uint16_t *above = above_buf_ + kIdifEdgeLead;
  uint16_t *left = left_buf_ + kIdifEdgeLead;
  // The largest padding (mrl 3) once; the timed loops do not re-pad.
  PadLikeProduction(above, left, MRL_LINE_NUMBER - 1);
  double t[2];
  for (int side = 0; side < 2; ++side) {
    const DrPred_Hbd fn = side ? funcs_.tst_fn : funcs_.ref_fn;
    uint16_t *dst = side ? tst_dst_ : ref_dst_;
    avm_usec_timer timer;
    avm_usec_timer_start(&timer);
    for (int n = 0; n < kNumSweeps; ++n) {
      for (int a = 1; a <= kIdifNumDerivatives; ++a) {
        int dx, dy;
        Derivatives(a, &dx, &dy);
        fn(dst, kIdifDefaultStride, bw_, bh_, above, left, dx, dy, bd_, 0);
      }
    }
    avm_usec_timer_mark(&timer);
    t[side] = static_cast<double>(avm_usec_timer_elapsed(&timer));
  }
  printf(
      "av2_highbd_dr_prediction_z%d_idif %dx%d bd=%d: C=%7.1fms "
      "SIMD=%7.1fms gain=%.2fx\n",
      funcs_.zone, bw_, bh_, bd_, t[0] / 1000.0, t[1] / 1000.0, t[0] / t[1]);
}

// -- Instantiation: every tx size (rectangles and 4:1 included) x 8/10/12 --

// Harness sanity baseline (C vs C): not SIMD coverage.
const IdifFuncs kIdifC[] = {
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>, 1 },
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>, 2 },
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>, 3 }
};
INSTANTIATE_TEST_SUITE_P(C, HighbdDrPredIdifTest,
                         ::testing::Combine(::testing::ValuesIn(kIdifC),
                                            ::testing::ValuesIn(kTxSize),
                                            ::testing::Values(8, 10, 12)));
#if !defined(_WIN32)
INSTANTIATE_TEST_SUITE_P(C, HighbdDrPredIdifGuardTest,
                         ::testing::Combine(::testing::ValuesIn(kIdifC),
                                            ::testing::ValuesIn(kTxSize),
                                            ::testing::Values(8, 10, 12)));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, HighbdDrPredIdifTest,
    ::testing::Combine(
        ::testing::Values(
            IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
                       &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_avx2>,
                       1 },
            IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
                       &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_avx2>,
                       2 },
            IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
                       &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_avx2>,
                       3 }),
        ::testing::ValuesIn(kTxSize), ::testing::Values(8, 10, 12)));
#endif  // HAVE_AVX2

#if HAVE_NEON
const IdifFuncs kIdifNeon[] = {
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z1_idif_neon>, 1 },
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z2_idif_neon>, 2 },
  IdifFuncs{ &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_c>,
             &idif_wrapper_hbd<av2_highbd_dr_prediction_z3_idif_neon>, 3 }
};
INSTANTIATE_TEST_SUITE_P(NEON, HighbdDrPredIdifTest,
                         ::testing::Combine(::testing::ValuesIn(kIdifNeon),
                                            ::testing::ValuesIn(kTxSize),
                                            ::testing::Values(8, 10, 12)));
#if !defined(_WIN32)
INSTANTIATE_TEST_SUITE_P(NEON, HighbdDrPredIdifGuardTest,
                         ::testing::Combine(::testing::ValuesIn(kIdifNeon),
                                            ::testing::ValuesIn(kTxSize),
                                            ::testing::Values(8, 10, 12)));
#endif
#endif  // HAVE_NEON

}  // namespace
