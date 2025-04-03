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
#ifndef AOM_TEST_WEBM_VIDEO_SOURCE_H_
#define AOM_TEST_WEBM_VIDEO_SOURCE_H_
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include "common/tools_common.h"
#include "common/webmdec.h"
#include "test/video_source.h"

namespace libaom_test {

// This class extends VideoSource to allow parsing of WebM files,
// so that we can do actual file decodes.
class WebMVideoSource : public CompressedVideoSource {
 public:
  explicit WebMVideoSource(const std::string &file_name)
      : file_name_(file_name), aom_ctx_(new AvxInputContext()),
        webm_ctx_(new WebmInputContext()), buf_(NULL), buf_sz_(0), frame_sz_(0),
        frame_number_(0), end_of_file_(false) {}

  virtual ~WebMVideoSource() {
#if CONFIG_MULTIVIEW_CORE
    if (aom_ctx_->file[0] != NULL) fclose(aom_ctx_->file[0]);
#else
    if (aom_ctx_->file != NULL) fclose(aom_ctx_->file);
#endif
    webm_free(webm_ctx_);
    delete aom_ctx_;
    delete webm_ctx_;
  }

  virtual void Init() {}

  virtual void Begin() {
#if CONFIG_MULTIVIEW_CORE
    aom_ctx_->file[0] = OpenTestDataFile(file_name_);
    ASSERT_TRUE(aom_ctx_->file[0] != NULL)
        << "Input file open failed. Filename: " << file_name_;
#else
    aom_ctx_->file = OpenTestDataFile(file_name_);
    ASSERT_TRUE(aom_ctx_->file != NULL)
        << "Input file open failed. Filename: " << file_name_;
#endif

    ASSERT_EQ(file_is_webm(webm_ctx_, aom_ctx_), 1) << "file is not WebM";

    FillFrame();
  }

  virtual void Next() {
    ++frame_number_;
    FillFrame();
  }

  void FillFrame() {
#if CONFIG_MULTIVIEW_CORE
    ASSERT_TRUE(aom_ctx_->file[0] != NULL);
#else
    ASSERT_TRUE(aom_ctx_->file != NULL);
#endif
    const int status = webm_read_frame(webm_ctx_, &buf_, &frame_sz_, &buf_sz_);
    ASSERT_GE(status, 0) << "webm_read_frame failed";
    if (status == 1) {
      end_of_file_ = true;
    }
  }

  void SeekToNextKeyFrame() {
#if CONFIG_MULTIVIEW_CORE
    ASSERT_TRUE(aom_ctx_->file[0] != NULL);
#else
    ASSERT_TRUE(aom_ctx_->file != NULL);
#endif
    do {
      const int status =
          webm_read_frame(webm_ctx_, &buf_, &frame_sz_, &buf_sz_);
      ASSERT_GE(status, 0) << "webm_read_frame failed";
      ++frame_number_;
      if (status == 1) {
        end_of_file_ = true;
      }
    } while (!webm_ctx_->is_key_frame && !end_of_file_);
  }

  virtual const uint8_t *cxdata() const { return end_of_file_ ? NULL : buf_; }
  virtual size_t frame_size() const { return frame_sz_; }
  virtual unsigned int frame_number() const { return frame_number_; }

 protected:
  std::string file_name_;
  AvxInputContext *aom_ctx_;
  WebmInputContext *webm_ctx_;
  uint8_t *buf_;  // Owned by webm_ctx_ and freed when webm_ctx_ is freed.
  size_t buf_sz_;
  size_t frame_sz_;
  unsigned int frame_number_;
  bool end_of_file_;
};

}  // namespace libaom_test

#endif  // AOM_TEST_WEBM_VIDEO_SOURCE_H_
