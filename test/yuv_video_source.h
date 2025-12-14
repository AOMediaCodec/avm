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
#ifndef AVM_TEST_YUV_VIDEO_SOURCE_H_
#define AVM_TEST_YUV_VIDEO_SOURCE_H_

#include <cstdio>
#include <cstdlib>
#include <string>

#include "test/video_source.h"
#include "avm/avm_image.h"

namespace libavm_test {

// This class extends VideoSource to allow parsing of raw YUV
// formats of various color sampling and bit-depths so that we can
// do actual file encodes.
class YUVVideoSource : public VideoSource {
 public:
  YUVVideoSource(const std::string &file_name, avm_img_fmt format,
                 unsigned int width, unsigned int height, int rate_numerator,
                 int rate_denominator, unsigned int start, int limit)
      : file_name_(file_name), input_file_(NULL), img_(NULL), start_(start),
        limit_(limit), frame_(0), width_(0), height_(0),
        format_(AVM_IMG_FMT_NONE), framerate_numerator_(rate_numerator),
        framerate_denominator_(rate_denominator) {
    // This initializes format_, raw_size_, width_, height_ and allocates img.
    SetSize(width, height, format);
  }

  virtual ~YUVVideoSource() {
    avm_img_free(img_);
    if (input_file_) fclose(input_file_);
  }

  virtual void Begin() {
    if (input_file_) fclose(input_file_);
    input_file_ = OpenTestDataFile(file_name_);
    ASSERT_TRUE(input_file_ != NULL)
        << "Input file open failed. Filename: " << file_name_;
    if (start_)
      fseek(input_file_, static_cast<unsigned>(raw_size_) * start_, SEEK_SET);

    frame_ = start_;
    FillFrame();
  }

  virtual void Next() {
    ++frame_;
    FillFrame();
  }

  virtual avm_image_t *img() const { return (frame_ < limit_) ? img_ : NULL; }

  // Models a stream where Timebase = 1/FPS, so pts == frame.
  virtual avm_codec_pts_t pts() const { return frame_; }

  virtual unsigned long duration() const { return 1; }

  virtual avm_rational_t timebase() const {
    const avm_rational_t t = { framerate_denominator_, framerate_numerator_ };
    return t;
  }

  virtual unsigned int frame() const { return frame_; }

  virtual unsigned int limit() const { return limit_; }

  virtual void SetSize(unsigned int width, unsigned int height,
                       avm_img_fmt format) {
    if (width != width_ || height != height_ || format != format_) {
      avm_img_free(img_);
      img_ = avm_img_alloc(NULL, format, width, height, 1);
      ASSERT_TRUE(img_ != NULL);
      width_ = width;
      height_ = height;
      format_ = format;
      switch (format) {
        case AVM_IMG_FMT_I420: raw_size_ = width * height * 3 / 2; break;
        case AVM_IMG_FMT_I422: raw_size_ = width * height * 2; break;
        case AVM_IMG_FMT_I444: raw_size_ = width * height * 3; break;
        case AVM_IMG_FMT_I42016: raw_size_ = width * height * 3; break;
        case AVM_IMG_FMT_I42216: raw_size_ = width * height * 4; break;
        case AVM_IMG_FMT_I44416: raw_size_ = width * height * 6; break;
        default: ASSERT_TRUE(0);
      }
    }
  }

  virtual void FillFrame() {
    ASSERT_TRUE(input_file_ != NULL);
    // Read a frame from input_file.
    if (fread(img_->img_data, raw_size_, 1, input_file_) == 0) {
      limit_ = frame_;
    }
  }

 protected:
  std::string file_name_;
  FILE *input_file_;
  avm_image_t *img_;
  size_t raw_size_;
  unsigned int start_;
  unsigned int limit_;
  unsigned int frame_;
  unsigned int width_;
  unsigned int height_;
  avm_img_fmt format_;
  int framerate_numerator_;
  int framerate_denominator_;
};

}  // namespace libavm_test

#endif  // AVM_TEST_YUV_VIDEO_SOURCE_H_
