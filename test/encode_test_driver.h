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
#ifndef AVM_TEST_ENCODE_TEST_DRIVER_H_
#define AVM_TEST_ENCODE_TEST_DRIVER_H_

#include <string>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"

#if CONFIG_AV2_ENCODER
#include "avm/avmcx.h"
#endif
#include "avm/avm_encoder.h"

namespace libavm_test {

class CodecFactory;
class VideoSource;
class DxDataIterator;

enum TestMode { kOnePassGood };
#define ALL_TEST_MODES ::testing::Values(::libavm_test::kOnePassGood)

#define ONE_PASS_TEST_MODES ::testing::Values(::libavm_test::kOnePassGood)

#define TWO_PASS_TEST_MODES ::testing::Values()

#define GOODQUALITY_TEST_MODES ::testing::Values(::libavm_test::kOnePassGood)

// Provides an object to handle the libavm get_cx_data() iteration pattern
class CxDataIterator {
 public:
  explicit CxDataIterator(avm_codec_ctx_t *encoder)
      : encoder_(encoder), iter_(NULL) {}

  const avm_codec_cx_pkt_t *Next() {
    return avm_codec_get_cx_data(encoder_, &iter_);
  }

 private:
  avm_codec_ctx_t *encoder_;
  avm_codec_iter_t iter_;
};

// Implements an in-memory store for libavm twopass statistics
class TwopassStatsStore {
 public:
  void Append(const avm_codec_cx_pkt_t &pkt) {
    buffer_.append(reinterpret_cast<char *>(pkt.data.twopass_stats.buf),
                   pkt.data.twopass_stats.sz);
  }

  avm_fixed_buf_t buf() {
    const avm_fixed_buf_t buf = { &buffer_[0], buffer_.size() };
    return buf;
  }

  void Reset() { buffer_.clear(); }

 protected:
  std::string buffer_;
};

// Provides a simplified interface to manage one video encoding pass, given
// a configuration and video source.
//
// TODO(jkoleszar): The exact services it provides and the appropriate
// level of abstraction will be fleshed out as more tests are written.
class Encoder {
 public:
  Encoder(avm_codec_enc_cfg_t cfg, const avm_codec_flags_t init_flags)
      : cfg_(cfg), init_flags_(init_flags) {
    memset(&encoder_, 0, sizeof(encoder_));
  }

  virtual ~Encoder() { avm_codec_destroy(&encoder_); }

  CxDataIterator GetCxData() { return CxDataIterator(&encoder_); }

  void InitEncoder(VideoSource *video);

  const avm_image_t *GetPreviewFrame() {
    return avm_codec_get_preview_frame(&encoder_);
  }
  // This is a thin wrapper around avm_codec_encode(), so refer to
  // avm_encoder.h for its semantics.
  void EncodeFrame(VideoSource *video, const unsigned long frame_flags);

  // Convenience wrapper for EncodeFrame()
  void EncodeFrame(VideoSource *video) { EncodeFrame(video, 0); }

  void Control(int ctrl_id, int arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void Control(int ctrl_id, int *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void Control(int ctrl_id, struct avm_scaling_mode *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void Control(int ctrl_id, const char *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void Control(int ctrl_id, const void *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

#if CONFIG_AV2_ENCODER
  void Control(int ctrl_id, avm_active_map_t *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void Control(int ctrl_id, const avm_user_defined_qm_t *arg) {
    const avm_codec_err_t res = avm_codec_control(&encoder_, ctrl_id, arg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }

  void SetOption(const char *key, const char *value) {
    const avm_codec_err_t res = avm_codec_set_option(&encoder_, key, value);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
  }
#endif

  void Config(const avm_codec_enc_cfg_t *cfg) {
    const avm_codec_err_t res = avm_codec_enc_config_set(&encoder_, cfg);
    ASSERT_EQ(AVM_CODEC_OK, res) << EncoderError();
    cfg_ = *cfg;
  }

 protected:
  virtual avm_codec_iface_t *CodecInterface() const = 0;

  const char *EncoderError() {
    const char *detail = avm_codec_error_detail(&encoder_);
    return detail ? detail : avm_codec_error(&encoder_);
  }

  // Encode an image
  void EncodeFrameInternal(const VideoSource &video,
                           const unsigned long frame_flags);

  // Flush the encoder on EOS
  void Flush();

  avm_codec_ctx_t encoder_;
  avm_codec_enc_cfg_t cfg_;
  avm_codec_flags_t init_flags_;
};

// Common test functionality for all Encoder tests.
//
// This class is a mixin which provides the main loop common to all
// encoder tests. It provides hooks which can be overridden by subclasses
// to implement each test's specific behavior, while centralizing the bulk
// of the boilerplate. Note that it doesn't inherit the gtest testing
// classes directly, so that tests can be parameterized differently.
class EncoderTest {
 protected:
  explicit EncoderTest(const CodecFactory *codec)
      : codec_(codec), abort_(false), init_flags_(0), frame_flags_(0),
        last_pts_(0), mode_(kOnePassGood), number_spatial_layers_(1) {
    // Default to 1 thread.
    cfg_.g_threads = 1;
  }

  virtual ~EncoderTest() {}

  // Initialize the cfg_ member with the default configuration.
  void InitializeConfig();

  // Map the TestMode enum to the passes_ variables.
  void SetMode(TestMode mode);

  // Set encoder flag.
  void set_init_flags(avm_codec_flags_t flag) { init_flags_ = flag; }

  // Main loop
  virtual void RunLoop(VideoSource *video);

  // Hook to be called at the beginning of a pass.
  virtual void BeginPassHook(unsigned int /*pass*/) {}

  // Hook to be called at the end of a pass.
  virtual void EndPassHook() {}

  // Hook to be called before encoding a frame.
  virtual void PreEncodeFrameHook(VideoSource * /*video*/) {}
  virtual void PreEncodeFrameHook(VideoSource * /*video*/,
                                  Encoder * /*encoder*/) {}

  // Hook to be called on every compressed data packet.
  virtual void FramePktHook(const avm_codec_cx_pkt_t * /*pkt*/
                            ,
                            DxDataIterator * /*dec_iter*/
  ) {}

  // Hook to be called on every PSNR packet.
  virtual void PSNRPktHook(const avm_codec_cx_pkt_t * /*pkt*/) {}

  // Hook to be called on every first pass stats packet.
  virtual void StatsPktHook(const avm_codec_cx_pkt_t * /*pkt*/) {}

  // Hook to determine whether the encode loop should continue.
  virtual bool Continue() const {
    return !(::testing::Test::HasFatalFailure() || abort_);
  }

  // Hook to call before decoding a frame.
  virtual void PreDecodeFrameHook(VideoSource * /*video*/,
                                  Decoder * /*decoder*/) {}

  // Hook to determine whether to decode frame after encoding
  virtual bool DoDecode() const { return true; }

  // Hook to determine whether to decode invisible frames after encoding
  virtual bool DoDecodeInvisible() const { return true; }

  // Hook to handle encode/decode mismatch
  virtual void MismatchHook(const avm_image_t *img1, const avm_image_t *img2);

  // Hook to be called on every decompressed frame.
  virtual void DecompressedFrameHook(const avm_image_t & /*img*/,
                                     avm_codec_pts_t /*pts*/) {}

  // Hook to be called to handle decode result. Return true to continue.
  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  // Hook to be called to handle encode result. Return true to continue.
  virtual bool HandleEncodeResult(VideoSource * /*video*/,
                                  Encoder * /*encoder*/) {
    return 1;
  }

  virtual int GetNumEmbeddedLayers() { return 1; }

  // Hook that can modify the encoder's output data
  virtual const avm_codec_cx_pkt_t *MutateEncoderOutputHook(
      const avm_codec_cx_pkt_t *pkt) {
    return pkt;
  }

  const CodecFactory *codec_;
  bool abort_;
  avm_codec_enc_cfg_t cfg_;
  unsigned int passes_;
  avm_codec_flags_t init_flags_;
  unsigned long frame_flags_;
  avm_codec_pts_t last_pts_;
  TestMode mode_;
  int number_spatial_layers_;
};

}  // namespace libavm_test

#endif  // AVM_TEST_ENCODE_TEST_DRIVER_H_
