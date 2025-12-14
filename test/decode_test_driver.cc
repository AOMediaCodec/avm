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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/register_state_check.h"
#include "test/video_source.h"

namespace libavm_test {

const char kAV2Name[] = "AOMedia Project AV2 Decoder";

avm_codec_err_t Decoder::PeekStream(const uint8_t *cxdata, size_t size,
                                    avm_codec_stream_info_t *stream_info) {
  return avm_codec_peek_stream_info(CodecInterface(), cxdata, size,
                                    stream_info);
}

avm_codec_err_t Decoder::DecodeFrame(const uint8_t *cxdata, size_t size) {
  return DecodeFrame(cxdata, size, NULL);
}

avm_codec_err_t Decoder::DecodeFrame(const uint8_t *cxdata, size_t size,
                                     void *user_priv) {
  avm_codec_err_t res_dec;
  InitOnce();
  API_REGISTER_STATE_CHECK(
      res_dec = avm_codec_decode(&decoder_, cxdata, size, user_priv));
  return res_dec;
}

bool Decoder::IsAV2() const {
  const char *codec_name = GetDecoderName();
  return strncmp(kAV2Name, codec_name, sizeof(kAV2Name) - 1) == 0;
}

void DecoderTest::HandlePeekResult(Decoder *const /*decoder*/,
                                   CompressedVideoSource * /*video*/,
                                   const avm_codec_err_t res_peek) {
  /* The Av2 implementation of PeekStream returns an error only if the
   * data passed to it isn't a valid Av2 chunk. */
  ASSERT_EQ(AVM_CODEC_OK, res_peek)
      << "Peek return failed: " << avm_codec_err_to_string(res_peek);
}

void DecoderTest::RunLoop(CompressedVideoSource *video,
                          const avm_codec_dec_cfg_t &dec_cfg) {
  Decoder *const decoder = codec_->CreateDecoder(dec_cfg, flags_);
  ASSERT_TRUE(decoder != NULL);
  bool end_of_file = false;
  bool peeked_stream = false;

  // Decode frames.
  for (video->Begin(); !::testing::Test::HasFailure() && !end_of_file;
       video->Next()) {
    PreDecodeFrameHook(*video, decoder);

    avm_codec_stream_info_t stream_info;

    if (video->cxdata() != NULL) {
      if (!peeked_stream) {
        // TODO(yaowu): PeekStream returns error for non-sequence_header_obu,
        // therefore should only be tried once per sequence, this shall be fixed
        // once PeekStream is updated to properly operate on other obus.
        const avm_codec_err_t res_peek = decoder->PeekStream(
            video->cxdata(), video->frame_size(), &stream_info);
        HandlePeekResult(decoder, video, res_peek);
        ASSERT_FALSE(::testing::Test::HasFailure());
        peeked_stream = true;
      }

      avm_codec_err_t res_dec =
          decoder->DecodeFrame(video->cxdata(), video->frame_size());
      if (!HandleDecodeResult(res_dec, *video, decoder)) break;
    } else {
      // Signal end of the file to the decoder.
      const avm_codec_err_t res_dec = decoder->DecodeFrame(NULL, 0);
      ASSERT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
      end_of_file = true;
    }

    DxDataIterator dec_iter = decoder->GetDxData();
    const avm_image_t *img = NULL;

    // Get decompressed data
    while (!::testing::Test::HasFailure() && (img = dec_iter.Next()))
      DecompressedFrameHook(*img, video->frame_number());
  }
  delete decoder;
}

void DecoderTest::RunLoop(CompressedVideoSource *video) {
  avm_codec_dec_cfg_t dec_cfg = avm_codec_dec_cfg_t();
  RunLoop(video, dec_cfg);
}

void DecoderTest::set_cfg(const avm_codec_dec_cfg_t &dec_cfg) {
  memcpy(&cfg_, &dec_cfg, sizeof(cfg_));
}

void DecoderTest::set_flags(const avm_codec_flags_t flags) { flags_ = flags; }

}  // namespace libavm_test
