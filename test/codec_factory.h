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
#ifndef AVM_TEST_CODEC_FACTORY_H_
#define AVM_TEST_CODEC_FACTORY_H_

#include <tuple>

#include "config/avm_config.h"

#include "avm/avm_decoder.h"
#include "avm/avm_encoder.h"
#if CONFIG_AV2_ENCODER
#include "avm/avmcx.h"
#endif
#if CONFIG_AV2_DECODER
#include "avm/avmdx.h"
#endif

#include "test/decode_test_driver.h"
#include "test/encode_test_driver.h"
namespace libavm_test {

const int kCodecFactoryParam = 0;

class CodecFactory {
 public:
  CodecFactory() {}

  virtual ~CodecFactory() {}

  virtual Decoder *CreateDecoder(avm_codec_dec_cfg_t cfg) const = 0;

  virtual Decoder *CreateDecoder(avm_codec_dec_cfg_t cfg,
                                 const avm_codec_flags_t flags) const = 0;

  virtual Encoder *CreateEncoder(avm_codec_enc_cfg_t cfg,
                                 const avm_codec_flags_t init_flags) const = 0;

  virtual avm_codec_err_t DefaultEncoderConfig(avm_codec_enc_cfg_t *cfg,
                                               unsigned int usage) const = 0;
};

/* Provide CodecTestWith<n>Params classes for a variable number of parameters
 * to avoid having to include a pointer to the CodecFactory in every test
 * definition.
 */
template <class T1>
class CodecTestWithParam
    : public ::testing::TestWithParam<
          std::tuple<const libavm_test::CodecFactory *, T1> > {};

template <class T1, class T2>
class CodecTestWith2Params
    : public ::testing::TestWithParam<
          std::tuple<const libavm_test::CodecFactory *, T1, T2> > {};

template <class T1, class T2, class T3>
class CodecTestWith3Params
    : public ::testing::TestWithParam<
          std::tuple<const libavm_test::CodecFactory *, T1, T2, T3> > {};

template <class T1, class T2, class T3, class T4>
class CodecTestWith4Params
    : public ::testing::TestWithParam<
          std::tuple<const libavm_test::CodecFactory *, T1, T2, T3, T4> > {};

template <class T1, class T2, class T3, class T4, class T5>
class CodecTestWith5Params
    : public ::testing::TestWithParam<
          std::tuple<const libavm_test::CodecFactory *, T1, T2, T3, T4, T5> > {
};

template <class T1, class T2, class T3, class T4, class T5, class T6, class T7>
class CodecTestWith7Params
    : public ::testing::TestWithParam<std::tuple<
          const libavm_test::CodecFactory *, T1, T2, T3, T4, T5, T6, T7> > {};

/*
 * AV2 Codec Definitions
 */
class AV2Decoder : public Decoder {
 public:
  explicit AV2Decoder(avm_codec_dec_cfg_t cfg) : Decoder(cfg) {}

  AV2Decoder(avm_codec_dec_cfg_t cfg, const avm_codec_flags_t flag)
      : Decoder(cfg, flag) {}

 protected:
  virtual avm_codec_iface_t *CodecInterface() const {
#if CONFIG_AV2_DECODER
    return avm_codec_av2_dx();
#else
    return NULL;
#endif
  }
};

class AV2Encoder : public Encoder {
 public:
  AV2Encoder(avm_codec_enc_cfg_t cfg, const avm_codec_flags_t init_flags)
      : Encoder(cfg, init_flags) {}

 protected:
  virtual avm_codec_iface_t *CodecInterface() const {
#if CONFIG_AV2_ENCODER
    return avm_codec_av2_cx();
#else
    return NULL;
#endif
  }
};

class AV2CodecFactory : public CodecFactory {
 public:
  AV2CodecFactory() : CodecFactory() {}

  virtual Decoder *CreateDecoder(avm_codec_dec_cfg_t cfg) const {
    return CreateDecoder(cfg, 0);
  }

  virtual Decoder *CreateDecoder(avm_codec_dec_cfg_t cfg,
                                 const avm_codec_flags_t flags) const {
#if CONFIG_AV2_DECODER
    return new AV2Decoder(cfg, flags);
#else
    (void)cfg;
    (void)flags;
    return NULL;
#endif
  }

  virtual Encoder *CreateEncoder(avm_codec_enc_cfg_t cfg,
                                 const avm_codec_flags_t init_flags) const {
#if CONFIG_AV2_ENCODER
    return new AV2Encoder(cfg, init_flags);
#else
    (void)cfg;
    (void)init_flags;
    return NULL;
#endif
  }

  virtual avm_codec_err_t DefaultEncoderConfig(avm_codec_enc_cfg_t *cfg,
                                               unsigned int usage) const {
#if CONFIG_AV2_ENCODER
    return avm_codec_enc_config_default(avm_codec_av2_cx(), cfg, usage);
#else
    (void)cfg;
    (void)usage;
    return AVM_CODEC_INCAPABLE;
#endif
  }
};

const libavm_test::AV2CodecFactory kAV2;

#define AV2_INSTANTIATE_TEST_SUITE(test, ...)                               \
  INSTANTIATE_TEST_SUITE_P(                                                 \
      AV2, test,                                                            \
      ::testing::Combine(                                                   \
          ::testing::Values(static_cast<const libavm_test::CodecFactory *>( \
              &libavm_test::kAV2)),                                         \
          __VA_ARGS__))

}  // namespace libavm_test
#endif  // AVM_TEST_CODEC_FACTORY_H_
