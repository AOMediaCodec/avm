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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tuple>

#include "avm_dsp/avm_dsp_common.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/transform_test_base.h"
#include "test/util.h"
#include "av2/common/entropy.h"
#include "avm/avm_codec.h"
#include "avm/avm_integer.h"
#include "avm_ports/mem.h"

using libavm_test::ACMRandom;

namespace {
typedef void (*FdctFunc)(const int16_t *in, tran_low_t *out, int stride);
typedef void (*IdctFunc)(const tran_low_t *in, uint16_t *out, int stride);

using libavm_test::FhtFunc;

typedef std::tuple<FdctFunc, IdctFunc, PRIMARY_TX_TYPE, avm_bit_depth_t, int,
                   FdctFunc>
    Dct4x4Param;

void fwht4x4_ref(const int16_t *in, tran_low_t *out, int stride,
                 TxfmParam * /*txfm_param*/) {
  av2_fwht4x4_c(in, out, stride);
}

void iwht4x4_10(const tran_low_t *in, uint16_t *out, int stride) {
  av2_highbd_iwht4x4_16_add_c(in, out, stride, 10);
}

void iwht4x4_12(const tran_low_t *in, uint16_t *out, int stride) {
  av2_highbd_iwht4x4_16_add_c(in, out, stride, 12);
}

class Trans4x4WHT : public libavm_test::TransformTestBase<tran_low_t>,
                    public ::testing::TestWithParam<Dct4x4Param> {
 public:
  virtual ~Trans4x4WHT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    inv_txfm_ = GET_PARAM(1);
    pitch_ = 4;
    height_ = 4;
    fwd_txfm_ref = fwht4x4_ref;
    bit_depth_ = GET_PARAM(3);
    mask_ = (1 << bit_depth_) - 1;
    num_coeffs_ = GET_PARAM(4);
    fwd_txfm_c_ = GET_PARAM(5);
  }
  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride);
  }
  void RunInvTxfm(const tran_low_t *out, uint16_t *dst, int stride) {
    inv_txfm_(out, dst, stride);
  }
  void RunSpeedTest() {
    if (!fwd_txfm_c_) {
      GTEST_SKIP();
    } else {
      ACMRandom rnd(ACMRandom::DeterministicSeed());
      const int count_test_block = 10;
      const int numIter = 5000;

      int c_sum_time = 0;
      int simd_sum_time = 0;

      int stride = 96;

      int16_t *input_block = reinterpret_cast<int16_t *>(
          avm_memalign(16, sizeof(int16_t) * stride * height_));
      tran_low_t *output_ref_block = reinterpret_cast<tran_low_t *>(
          avm_memalign(16, sizeof(output_ref_block[0]) * num_coeffs_));
      tran_low_t *output_block = reinterpret_cast<tran_low_t *>(
          avm_memalign(16, sizeof(output_block[0]) * num_coeffs_));

      for (int i = 0; i < count_test_block; ++i) {
        int j, k;
        for (j = 0; j < height_; ++j) {
          for (k = 0; k < pitch_; ++k) {
            int in_idx = j * stride + k;
            int out_idx = j * pitch_ + k;
            input_block[in_idx] =
                (rnd.Rand16() & mask_) - (rnd.Rand16() & mask_);
            output_block[out_idx] = output_ref_block[out_idx] =
                rnd.Rand16() & mask_;
          }
        }

        avm_usec_timer c_timer_;
        avm_usec_timer_start(&c_timer_);
        for (int i = 0; i < numIter; i++) {
          ASM_REGISTER_STATE_CHECK(
              fwd_txfm_c_(input_block, output_ref_block, stride));
        }
        avm_usec_timer_mark(&c_timer_);

        avm_usec_timer simd_timer_;
        avm_usec_timer_start(&simd_timer_);

        for (int i = 0; i < numIter; i++) {
          ASM_REGISTER_STATE_CHECK(
              fwd_txfm_(input_block, output_block, stride));
        }
        avm_usec_timer_mark(&simd_timer_);

        c_sum_time += static_cast<int>(avm_usec_timer_elapsed(&c_timer_));
        simd_sum_time += static_cast<int>(avm_usec_timer_elapsed(&simd_timer_));

        // The minimum quant value is 4.
        for (j = 0; j < height_; ++j) {
          for (k = 0; k < pitch_; ++k) {
            int out_idx = j * pitch_ + k;
            ASSERT_EQ(output_block[out_idx], output_ref_block[out_idx])
                << "Error: not bit-exact result at index: " << out_idx
                << " at test block: " << i;
          }
        }
      }

      printf(
          "c_time = %d \t simd_time = %d \t Gain = %4.2f \n", c_sum_time,
          simd_sum_time,
          (static_cast<float>(c_sum_time) / static_cast<float>(simd_sum_time)));

      avm_free(input_block);
      avm_free(output_ref_block);
      avm_free(output_block);
    }
  }

  FdctFunc fwd_txfm_;
  IdctFunc inv_txfm_;

  FdctFunc fwd_txfm_c_;  // C version of forward transform for speed test.
};

TEST_P(Trans4x4WHT, AccuracyCheck) { RunAccuracyCheck(0, 0.00001); }

TEST_P(Trans4x4WHT, CoeffCheck) { RunCoeffCheck(); }

TEST_P(Trans4x4WHT, MemCheck) { RunMemCheck(); }

TEST_P(Trans4x4WHT, InvAccuracyCheck) { RunInvAccuracyCheck(0); }

TEST_P(Trans4x4WHT, DISABLED_Speed) { RunSpeedTest(); }

using std::make_tuple;

INSTANTIATE_TEST_SUITE_P(
    C, Trans4x4WHT,
    ::testing::Values(make_tuple(&av2_highbd_fwht4x4_c, &iwht4x4_10, DCT_DCT,
                                 AVM_BITS_10, 16, static_cast<FdctFunc>(NULL)),
                      make_tuple(&av2_highbd_fwht4x4_c, &iwht4x4_12, DCT_DCT,
                                 AVM_BITS_12, 16,
                                 static_cast<FdctFunc>(NULL))));
#if HAVE_NEON

INSTANTIATE_TEST_SUITE_P(
    NEON, Trans4x4WHT,
    ::testing::Values(make_tuple(&av2_highbd_fwht4x4_neon, &iwht4x4_10, DCT_DCT,
                                 AVM_BITS_10, 16, &av2_highbd_fwht4x4_c),
                      make_tuple(&av2_highbd_fwht4x4_neon, &iwht4x4_12, DCT_DCT,
                                 AVM_BITS_12, 16, &av2_highbd_fwht4x4_c)));

#endif  // HAVE_NEON

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, Trans4x4WHT,
    ::testing::Values(make_tuple(&av2_highbd_fwht4x4_avx2, &iwht4x4_10, DCT_DCT,
                                 AVM_BITS_10, 16, &av2_highbd_fwht4x4_c),
                      make_tuple(&av2_highbd_fwht4x4_avx2, &iwht4x4_12, DCT_DCT,
                                 AVM_BITS_12, 16, &av2_highbd_fwht4x4_c)));
#endif  // HAVE_AVX2

typedef void (*IwhtAddFunc)(const tran_low_t *in, uint16_t *dest, int stride,
                            int bd);
typedef std::tuple<IwhtAddFunc, avm_bit_depth_t> IwhtAddParam;

enum { kFromResidual, kRandomCoeffs, kExtremeCoeffs };

class InvWHT4x4Test : public ::testing::TestWithParam<IwhtAddParam> {
 public:
  virtual ~InvWHT4x4Test() {}

  virtual void SetUp() {
    inv_txfm_ = GET_PARAM(0);
    bd_ = GET_PARAM(1);
    max_pixel_ = (1 << bd_) - 1;
  }
  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  static const int kStride = 8;

  void FillBlock(ACMRandom *rnd, int mode, tran_low_t *coeff, uint16_t *ref,
                 uint16_t *dst) {
    // The decoder clamps dequantized coefficients to bd + 8 bits.
    const int coeff_max = (1 << (bd_ + 7)) - 1;
    if (mode == kFromResidual) {
      int16_t residual[16];
      for (int i = 0; i < 16; ++i) {
        residual[i] =
            (rnd->Rand16() & max_pixel_) - (rnd->Rand16() & max_pixel_);
      }
      av2_fwht4x4_c(residual, coeff, 4);
    } else {
      for (int i = 0; i < 16; ++i) {
        if (mode == kRandomCoeffs) {
          coeff[i] = static_cast<int>(rnd->Rand31() % (2 * (coeff_max + 1))) -
                     coeff_max - 1;
        } else {
          coeff[i] = (rnd->Rand8() & 1) ? coeff_max : -coeff_max - 1;
        }
      }
    }
    for (int i = 0; i < 4 * kStride; ++i) {
      if (mode == kExtremeCoeffs) {
        ref[i] = dst[i] = (rnd->Rand8() & 1) ? max_pixel_ : 0;
      } else {
        ref[i] = dst[i] = rnd->Rand16() & max_pixel_;
      }
    }
  }

  void RunCheck(int mode) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, tran_low_t, coeff[16]);
    DECLARE_ALIGNED(16, uint16_t, ref[4 * kStride]);
    DECLARE_ALIGNED(16, uint16_t, dst[4 * kStride]);
    for (int n = 0; n < 10000; ++n) {
      FillBlock(&rnd, mode, coeff, ref, dst);
      av2_highbd_iwht4x4_16_add_c(coeff, ref, kStride, bd_);
      ASM_REGISTER_STATE_CHECK(inv_txfm_(coeff, dst, kStride, bd_));
      for (int i = 0; i < 4 * kStride; ++i) {
        ASSERT_EQ(ref[i], dst[i])
            << "Error: not bit-exact at index " << i << " of block " << n;
      }
    }
  }

  void RunSpeedTest() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    DECLARE_ALIGNED(16, tran_low_t, coeff[16]);
    DECLARE_ALIGNED(16, uint16_t, ref[4 * kStride]);
    DECLARE_ALIGNED(16, uint16_t, dst[4 * kStride]);
    const int num_iter = 50000;
    FillBlock(&rnd, kFromResidual, coeff, ref, dst);

    avm_usec_timer c_timer;
    avm_usec_timer_start(&c_timer);
    for (int i = 0; i < num_iter; ++i) {
      ASM_REGISTER_STATE_CHECK(
          av2_highbd_iwht4x4_16_add_c(coeff, ref, kStride, bd_));
    }
    avm_usec_timer_mark(&c_timer);

    avm_usec_timer simd_timer;
    avm_usec_timer_start(&simd_timer);
    for (int i = 0; i < num_iter; ++i) {
      ASM_REGISTER_STATE_CHECK(inv_txfm_(coeff, dst, kStride, bd_));
    }
    avm_usec_timer_mark(&simd_timer);

    for (int i = 0; i < 4 * kStride; ++i) {
      ASSERT_EQ(ref[i], dst[i]) << "Error: not bit-exact at index " << i;
    }
    const int c_time = static_cast<int>(avm_usec_timer_elapsed(&c_timer));
    const int simd_time = static_cast<int>(avm_usec_timer_elapsed(&simd_timer));
    printf("c_time = %d \t simd_time = %d \t Gain = %4.2f \n", c_time,
           simd_time, static_cast<float>(c_time) / simd_time);
  }

  IwhtAddFunc inv_txfm_;
  int bd_;
  int max_pixel_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(InvWHT4x4Test);

TEST_P(InvWHT4x4Test, FromResidual) { RunCheck(kFromResidual); }

TEST_P(InvWHT4x4Test, RandomCoeffs) { RunCheck(kRandomCoeffs); }

TEST_P(InvWHT4x4Test, ExtremeCoeffs) { RunCheck(kExtremeCoeffs); }

TEST_P(InvWHT4x4Test, DISABLED_Speed) { RunSpeedTest(); }

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, InvWHT4x4Test,
    ::testing::Combine(::testing::Values(&av2_highbd_iwht4x4_16_add_avx2),
                       ::testing::Values(AVM_BITS_8, AVM_BITS_10,
                                         AVM_BITS_12)));
#endif  // HAVE_AVX2

}  // namespace
