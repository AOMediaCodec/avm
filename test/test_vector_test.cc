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

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "common/tools_common.h"
#include "config/avm_config.h"
#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/ivf_video_source.h"
#include "test/md5_helper.h"
#include "test/test_vectors.h"
#include "test/util.h"
#if CONFIG_WEBM_IO
#include "test/webm_video_source.h"
#endif

namespace {

const int kThreads = 0;
const int kFileName = 1;
const int kRowMT = 2;

typedef std::tuple<int, const char *, int> DecodeParam;

class TestVectorTest : public ::libavm_test::DecoderTest,
                       public ::libavm_test::CodecTestWithParam<DecodeParam> {
 protected:
  TestVectorTest() : DecoderTest(GET_PARAM(0)), md5_file_(NULL) {}

  virtual ~TestVectorTest() {
    if (md5_file_) fclose(md5_file_);
  }

  void OpenMD5File(const std::string &md5_file_name_) {
    md5_file_ = libavm_test::OpenTestDataFile(md5_file_name_);
    ASSERT_TRUE(md5_file_ != NULL)
        << "Md5 file open failed. Filename: " << md5_file_name_;
  }

  virtual void PreDecodeFrameHook(
      const libavm_test::CompressedVideoSource &video,
      libavm_test::Decoder *decoder) {
    if (video.frame_number() == 0) decoder->Control(AV2D_SET_ROW_MT, row_mt_);
  }

  virtual void DecompressedFrameHook(const avm_image_t &img,
                                     const unsigned int frame_number) {
    ASSERT_TRUE(md5_file_ != NULL);
    char expected_md5[33];
    char junk[128];

    // Read correct md5 checksums.
    const int res = fscanf(md5_file_, "%s  %s", expected_md5, junk);
    ASSERT_NE(res, EOF) << "Read md5 data failed";
    expected_md5[32] = '\0';

    ::libavm_test::MD5 md5_res;
    const avm_img_fmt_t shifted_fmt =
        (avm_img_fmt)(img.fmt & ~AVM_IMG_FMT_HIGHBITDEPTH);
    if (img.bit_depth == 8 && shifted_fmt != img.fmt) {
      avm_image_t *img_shifted =
          avm_img_alloc(NULL, shifted_fmt, img.d_w, img.d_h, 16);
      img_shifted->bit_depth = img.bit_depth;
      img_shifted->monochrome = img.monochrome;
      avm_img_downshift(img_shifted, &img, 0, 8);
      md5_res.Add(img_shifted);
      avm_img_free(img_shifted);
    } else {
      md5_res.Add(&img);
    }

    const char *actual_md5 = md5_res.Get();
    // Check md5 match.
    ASSERT_STREQ(expected_md5, actual_md5)
        << "Md5 checksums don't match: frame number = " << frame_number;
  }

  unsigned int row_mt_;

 private:
  FILE *md5_file_;
};

// This test runs through the whole set of test vectors, and decodes them.
// The md5 checksums are computed for each frame in the video file. If md5
// checksums match the correct md5 data, then the test is passed. Otherwise,
// the test failed.
TEST_P(TestVectorTest, DISABLED_MD5Match) {
  const DecodeParam input = GET_PARAM(1);
  const std::string filename = std::get<kFileName>(input);
  avm_codec_flags_t flags = 0;
  avm_codec_dec_cfg_t cfg = avm_codec_dec_cfg_t();
  char str[256];

  cfg.threads = std::get<kThreads>(input);
  row_mt_ = std::get<kRowMT>(input);

  snprintf(str, sizeof(str) / sizeof(str[0]) - 1, "file: %s threads: %d",
           filename.c_str(), cfg.threads);
  SCOPED_TRACE(str);

  // Open compressed video file.
  std::unique_ptr<libavm_test::CompressedVideoSource> video;
  if (filename.substr(filename.length() - 3, 3) == "ivf") {
    video.reset(new libavm_test::IVFVideoSource(filename));
  } else if (filename.substr(filename.length() - 4, 4) == "webm" ||
             filename.substr(filename.length() - 3, 3) == "mkv") {
#if CONFIG_WEBM_IO
    video.reset(new libavm_test::WebMVideoSource(filename));
#else
    fprintf(stderr, "WebM IO is disabled, skipping test vector %s\n",
            filename.c_str());
    return;
#endif
  }
  ASSERT_TRUE(video.get() != NULL);
  video->Init();

  // Construct md5 file name.
  const std::string md5_filename = filename + ".md5";
  OpenMD5File(md5_filename);

  // Set decode config and flags.
  set_cfg(cfg);
  set_flags(flags);

  // Decode frame, and check the md5 matching.
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get(), cfg));
}

#if CONFIG_AV2_DECODER
AV2_INSTANTIATE_TEST_SUITE(
    TestVectorTest,
    ::testing::Combine(::testing::Values(1),  // Single thread.
                       ::testing::ValuesIn(libavm_test::kAV2TestVectors,
                                           libavm_test::kAV2TestVectors +
                                               libavm_test::kNumAV2TestVectors),
                       ::testing::Values(0)));

// Test AV2 decode in with different numbers of threads.
INSTANTIATE_TEST_SUITE_P(
    AV2MultiThreaded, TestVectorTest,
    ::testing::Combine(
        ::testing::Values(
            static_cast<const libavm_test::CodecFactory *>(&libavm_test::kAV2)),
        ::testing::Combine(
            ::testing::Range(2, 9),  // With 2 ~ 8 threads.
            ::testing::ValuesIn(libavm_test::kAV2TestVectors,
                                libavm_test::kAV2TestVectors +
                                    libavm_test::kNumAV2TestVectors),
            ::testing::Range(0, 2))));

#endif  // CONFIG_AV2_DECODER

}  // namespace
