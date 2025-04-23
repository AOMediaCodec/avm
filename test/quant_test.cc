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
#include "config/aom_config.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "av1/encoder/av1_quantize.h"
#include "test/y4m_video_source.h"

namespace {

#if CONFIG_QM_EXTENSION
// Level 6 of the default quantization matrices.
// clang-format off
constexpr uint8_t user_defined_qm_8x8[2][8 * 8] = {
    {
        /* Luma */
        32,  31,  30,  32,  37,  44,  53,  65,
        31,  28,  29,  31,  36,  44,  52,  62,
        30,  29,  29,  35,  39,  45,  53,  63,
        32,  31,  35,  37,  44,  51,  57,  66,
        37,  36,  39,  44,  51,  57,  65,  74,
        44,  44,  45,  51,  57,  65,  74,  85,
        53,  52,  53,  57,  65,  74,  84,  97,
        65,  62,  63,  66,  74,  85,  97,  112,
    },
    {
        /* Chroma */
        32,  30,  36,  40,  44,  47,  51,  53,
        30,  32,  37,  40,  43,  45,  48,  49,
        36,  37,  41,  43,  46,  47,  51,  52,
        40,  40,  43,  45,  47,  49,  51,  53,
        44,  43,  46,  47,  51,  52,  56,  58,
        47,  45,  47,  49,  52,  53,  59,  62,
        51,  48,  51,  51,  56,  59,  65,  71,
        53,  49,  52,  53,  58,  62,  71,  79,
    },
};
constexpr uint8_t user_defined_qm_8x4[2][8 * 4] = {
    {
        /* Luma */
        32,  31,  31,  33,  38,  44,  54,  65,
        30,  30,  32,  35,  40,  46,  54,  62,
        38,  38,  41,  45,  53,  58,  67,  77,
        57,  54,  56,  61,  68,  77,  89,  103,
    },
    {
        /* Chroma */
        32,  31,  37,  41,  45,  48,  52,  54,
        37,  38,  42,  43,  46,  47,  51,  52,
        45,  44,  47,  48,  52,  53,  57,  61,
        51,  48,  51,  53,  57,  62,  70,  76,
    },
};
constexpr uint8_t user_defined_qm_4x8[2][4 * 8] = {
    {
        /* Luma */
        32,  30,  38,  57,
        31,  30,  38,  54,
        31,  32,  41,  56,
        33,  35,  45,  61,
        38,  40,  53,  68,
        44,  46,  58,  77,
        54,  54,  67,  89,
        65,  62,  77,  103,
    },
    {
        /* Chroma */
        32,  37,  45,  51,
        31,  38,  44,  48,
        37,  42,  47,  51,
        41,  43,  48,  53,
        45,  46,  52,  57,
        48,  47,  53,  62,
        52,  51,  57,  70,
        54,  52,  61,  76,
    },
};
// clang-format off
#endif  // CONFIG_QM_EXTENSION

class QMTest
    : public ::libaom_test::CodecTestWith2Params<libaom_test::TestMode, int>,
      public ::libaom_test::EncoderTest {
 protected:
  QMTest() : EncoderTest(GET_PARAM(0)) {}
  virtual ~QMTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = GET_PARAM(2);
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV1E_SET_ENABLE_QM, 1);
      encoder->Control(AV1E_SET_QM_MIN, qm_min_);
      encoder->Control(AV1E_SET_QM_MAX, qm_max_);
#if CONFIG_QM_EXTENSION
      if (user_defined_qmatrix_) {
        ASSERT_EQ(qm_min_, qm_max_);
        aom_user_defined_qm_t qm;
        qm.level = qm_min_;
        qm.num_planes = monochrome_ ? 1 : 3;
        if (monochrome_) {
          qm.qm_8x8[0] = user_defined_qm_8x8[0];
          qm.qm_8x4[0] = user_defined_qm_8x4[0];
          qm.qm_4x8[0] = user_defined_qm_4x8[0];
        } else {
          qm.qm_8x8[0] = user_defined_qm_8x8[0];
          qm.qm_8x8[1] = user_defined_qm_8x8[1];
          qm.qm_8x8[2] = user_defined_qm_8x8[1];
          qm.qm_8x4[0] = user_defined_qm_8x4[0];
          qm.qm_8x4[1] = user_defined_qm_8x4[1];
          qm.qm_8x4[2] = user_defined_qm_8x4[1];
          qm.qm_4x8[0] = user_defined_qm_4x8[0];
          qm.qm_4x8[1] = user_defined_qm_4x8[1];
          qm.qm_4x8[2] = user_defined_qm_4x8[1];
        }
        encoder->Control(AV1E_SET_USER_DEFINED_QMATRIX, &qm);
      }
#endif  // CONFIG_QM_EXTENSION

      encoder->Control(AOME_SET_MAX_INTRA_BITRATE_PCT, 100);
    }
  }

  void DoTest(int qm_min, int qm_max) {
    qm_min_ = qm_min;
    qm_max_ = qm_max;
    if (monochrome_) {
      cfg_.monochrome = 1;
    }
    cfg_.kf_max_dist = 12;
    cfg_.rc_min_quantizer = 32;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 6;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_target_bitrate = 300;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 15);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  bool monochrome_ = false;
  int set_cpu_used_;
  int qm_min_;
  int qm_max_;
#if CONFIG_QM_EXTENSION
  bool user_defined_qmatrix_ = false;
#endif  // CONFIG_QM_EXTENSION
};

// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM1) { DoTest(5, 9); }

// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM2) { DoTest(0, 8); }

// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM3) { DoTest(9, 15); }

// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM4) {
  monochrome_ = true;
  DoTest(5, 9);
}

#if CONFIG_QM_EXTENSION
// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM5) {
  user_defined_qmatrix_ = true;
  DoTest(0, 0);
}

// encodes and decodes without a mismatch.
TEST_P(QMTest, TestNoMisMatchQM6) {
  monochrome_ = true;
  user_defined_qmatrix_ = true;
  DoTest(0, 0);
}
#endif  // CONFIG_QM_EXTENSION

AV1_INSTANTIATE_TEST_SUITE(QMTest,
                           ::testing::Values(::libaom_test::kOnePassGood),
                           ::testing::Range(5, 9));

typedef struct {
  const unsigned int min_q;
  const unsigned int max_q;
} QuantParam;

const QuantParam QuantTestParams[] = {
  { 0, 50 }, { 0, 240 }, { 100, 150 }, { 150, 200 }, { 200, 255 }
};

std::ostream &operator<<(std::ostream &os, const QuantParam &test_arg) {
  return os << "QuantParam { min_q:" << test_arg.min_q
            << " max_q:" << test_arg.max_q << " }";
}

/*
 * This class is used to test whether base_qindex is within min
 * and max quantizer range configured by user.
 */
class QuantizerBoundsCheckTestLarge
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode,
                                                 QuantParam, aom_rc_mode>,
      public ::libaom_test::EncoderTest {
 protected:
  QuantizerBoundsCheckTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        quant_param_(GET_PARAM(2)), rc_end_usage_(GET_PARAM(3)) {
    quant_bound_violated_ = false;
  }
  virtual ~QuantizerBoundsCheckTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = rc_end_usage_;
    cfg_.g_threads = 1;
    cfg_.rc_min_quantizer = quant_param_.min_q;
    cfg_.rc_max_quantizer = quant_param_.max_q;
    cfg_.g_lag_in_frames = 35;
    if (rc_end_usage_ != AOM_Q) {
      cfg_.rc_target_bitrate = 400;
    }
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, 5);
    }
  }

  virtual bool HandleDecodeResult(const aom_codec_err_t res_dec,
                                  libaom_test::Decoder *decoder) {
    EXPECT_EQ(AOM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AOM_CODEC_OK == res_dec) {
      aom_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      AOM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AOMD_GET_LAST_QUANTIZER,
                                    &base_qindex_);
      min_bound_qindex_ = cfg_.rc_min_quantizer;
      max_bound_qindex_ = cfg_.rc_max_quantizer;
      if ((base_qindex_ < min_bound_qindex_ ||
           base_qindex_ > max_bound_qindex_) &&
          quant_bound_violated_ == false) {
        quant_bound_violated_ = true;
      }
    }
    return AOM_CODEC_OK == res_dec;
  }

  ::libaom_test::TestMode encoding_mode_;
  const QuantParam quant_param_;
  int base_qindex_;
  int min_bound_qindex_;
  int max_bound_qindex_;
  bool quant_bound_violated_;
  aom_rc_mode rc_end_usage_;
};

TEST_P(QuantizerBoundsCheckTestLarge, QuantizerBoundsCheckEncodeTest) {
  libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(quant_bound_violated_, false);
}

AV1_INSTANTIATE_TEST_SUITE(QuantizerBoundsCheckTestLarge,
                           GOODQUALITY_TEST_MODES,
                           ::testing::ValuesIn(QuantTestParams),
                           ::testing::Values(AOM_Q, AOM_VBR, AOM_CBR, AOM_CQ));
}  // namespace
