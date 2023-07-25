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

#include <stdbool.h>
#include <memory>
#include <tuple>
#include "aom_mem/aom_mem.h"
#include "av1/encoder/rdopt.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

using std::get;
using std::tuple;

/** Get the (i, j) value from the input; if i or j is outside of the width
 * or height, the nearest pixel value is returned.
 */
static int get_nearest_pix(const int *buf, int w, int h, int i, int j) {
  int offset = AOMMAX(AOMMIN(i, w - 1), 0) + w * AOMMAX(AOMMIN(j, h - 1), 0);
  return buf[offset];
}

/** Given the image data, creates a new image with padded values, so an
 * 8-tap filter can be convolved. The padded value is the same as the closest
 * value in the image. Returns a pointer to the start of the image in the
 * padded data. Must be freed with free_pad_8tap. The output will be either
 * 8-bit or 16-bit, depending on the high bit-depth (high_bd) field.
 */
static uint16_t *pad_8tap_convolve(const int *data, int w, int h) {
  // SIMD optimizations require the width to be a multiple of 8 and the height
  // to be multiples of 4.
  assert(w % 8 == 0);
  assert(h % 4 == 0);
  // For an 8-tap filter, we need to pad with 3 lines on top and on the left,
  // and 4 lines on the right and bottom, for 7 extra lines.
  const int pad_w = w + 7;
  const int pad_h = h + 7;

  uint16_t *dst =
      (uint16_t *)aom_memalign(32, sizeof(uint16_t) * pad_w * pad_h);
  if (dst == nullptr) {
    EXPECT_NE(dst, nullptr);
    return nullptr;
  }

  for (int j = 0; j < pad_h; ++j) {
    for (int i = 0; i < pad_w; ++i) {
      const int v = get_nearest_pix(data, w, h, i - 3, j - 3);
      dst[i + j * pad_w] = v;
    }
  }
  return dst + (w + 7) * 3 + 3;
}

static int stride_8tap(int width) { return width + 7; }

static void free_pad_8tap(uint16_t *padded, int width) {
  aom_free(padded - (width + 7) * 3 - 3);
}

struct Pad8TapConvolveDeleter {
  Pad8TapConvolveDeleter(const int width) : width(width) {}
  void operator()(uint16_t *p) {
    if (p != nullptr) {
      free_pad_8tap(p, width);
    }
  }
  const int width;
};

struct MallocBdDeleter {
  explicit MallocBdDeleter(void) {}
  void operator()(uint16_t *p) { aom_free(p); }
};

class EdgeDetectBrightnessTest :
    // Parameters are (brightness, width, height, high bit depth representation,
    // bit depth).
    public ::testing::TestWithParam<tuple<int, int, int, int> > {
 protected:
  void SetUp() override {
    // Allocate a (width by height) array of luma values in orig_.
    // padded_ will be filled by the pad() call, which adds a border around
    // the orig_. The output_ array has enough space for the computation.
    const int brightness = GET_PARAM(0);
    const int width = GET_PARAM(1);
    const int height = GET_PARAM(2);

    // Create the padded image of uniform brightness.
    std::unique_ptr<int[]> orig(new int[width * height]);
    ASSERT_NE(orig, nullptr);
    for (int i = 0; i < width * height; ++i) {
      orig[i] = brightness;
    }
    input_ = pad_8tap_convolve(orig.get(), width, height);
    ASSERT_NE(input_, nullptr);
    output_ = (uint16_t *)aom_memalign(32, sizeof(uint16_t) * width * height);
    ASSERT_NE(output_, nullptr);
  }

  void TearDown() override {
    const int width = GET_PARAM(1);
    free_pad_8tap(input_, width);
    aom_free(output_);
  }

  // Skip the tests where brightness exceeds the bit-depth; we run into this
  // issue because of gtest's limitation on valid combinations of test
  // parameters. Also skip the tests where bit depth is greater than 8, but
  // high bit depth representation is not set.
  bool should_skip() const {
    const int brightness = GET_PARAM(0);
    const int bd = GET_PARAM(3);
    if (brightness >= (1 << bd)) {
      return true;
    }
    return false;
  }

  uint16_t *input_;
  uint16_t *output_;
};

TEST_P(EdgeDetectBrightnessTest, BlurUniformBrightness) {
  // Some combination of parameters are non-sensical, due to limitations
  // of the testing framework. Ignore these.
  if (should_skip()) {
    return;
  }

  // For varying levels of brightness, the algorithm should
  // produce the same output.
  const int brightness = GET_PARAM(0);
  const int width = GET_PARAM(1);
  const int height = GET_PARAM(2);
  const int bd = GET_PARAM(3);

  av1_gaussian_blur(input_, stride_8tap(width), width, height, output_, bd);
  for (int i = 0; i < width * height; ++i) {
    ASSERT_EQ(brightness, output_[i]);
  }
}

// No edges on a uniformly bright image.
TEST_P(EdgeDetectBrightnessTest, DetectUniformBrightness) {
  if (should_skip()) {
    return;
  }
  const int width = GET_PARAM(1);
  const int height = GET_PARAM(2);
  const int bd = GET_PARAM(3);

  ASSERT_EQ(
      0,
      av1_edge_exists(input_, stride_8tap(width), width, height, bd).magnitude);
}

INSTANTIATE_TEST_SUITE_P(ImageBrightnessTests, EdgeDetectBrightnessTest,
                         ::testing::Combine(
                             // Brightness
                             ::testing::Values(0, 1, 2, 127, 128, 129, 254, 255,
                                               256, 511, 512, 1023, 1024, 2048,
                                               4095),
                             // Width
                             ::testing::Values(8, 16, 32),
                             // Height
                             ::testing::Values(4, 8, 12, 32),
                             // Bit depth
                             ::testing::Values(8, 10, 12)));

class EdgeDetectImageTest :
    // Parameters are (width, height, high bit depth representation, bit depth).
    public ::testing::TestWithParam<tuple<int, int, int> > {};

// Generate images with black on one side and white on the other.
TEST_P(EdgeDetectImageTest, BlackWhite) {
  const int width = GET_PARAM(0);
  const int height = GET_PARAM(1);
  const int bd = GET_PARAM(2);

  const int white = (1 << bd) - 1;
  std::unique_ptr<int[]> orig(new int[width * height]);
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      if (i < width / 2) {
        orig[i + j * width] = 0;
      } else {
        orig[i + j * width] = white;
      }
    }
  }

  std::unique_ptr<uint16_t[], Pad8TapConvolveDeleter> padded(
      pad_8tap_convolve(orig.get(), width, height),
      Pad8TapConvolveDeleter(width));
  ASSERT_NE(padded, nullptr);
  // Value should be between 556 and 560.
  ASSERT_LE(556,
            av1_edge_exists(padded.get(), stride_8tap(width), width, height, bd)
                .magnitude);
  ASSERT_GE(560,
            av1_edge_exists(padded.get(), stride_8tap(width), width, height, bd)
                .magnitude);
}

// Hardcoded blur tests.
static const int luma[32] = { 241, 147, 7,   90,  184, 103, 28,  186,
                              2,   248, 49,  242, 114, 146, 127, 22,
                              121, 228, 167, 108, 158, 174, 41,  168,
                              214, 99,  184, 109, 114, 247, 117, 119 };
static const uint16_t expected[] = { 161, 138, 119, 118, 123, 118, 113, 122,
                                     143, 140, 134, 133, 134, 126, 116, 114,
                                     147, 149, 145, 142, 143, 138, 126, 118,
                                     164, 156, 148, 144, 148, 148, 138, 126 };

static void hardcoded_blur_test_aux(void) {
  const int w = 8;
  const int h = 4;
  for (int bd = 8; bd <= 12; bd += 2) {
    // Skip the tests where bit depth is greater than 8, but high bit depth
    // representation is not set.
    std::unique_ptr<uint16_t[], MallocBdDeleter> output(
        (uint16_t *)aom_memalign(32, sizeof(uint16_t) * w * h),
        MallocBdDeleter());
    ASSERT_NE(output, nullptr);
    std::unique_ptr<uint16_t[], Pad8TapConvolveDeleter> padded(
        pad_8tap_convolve(luma, w, h), Pad8TapConvolveDeleter(w));
    ASSERT_NE(padded, nullptr);
    av1_gaussian_blur(padded.get(), stride_8tap(w), w, h, output.get(), bd);
    for (int i = 0; i < w * h; ++i) {
      uint16_t *buf = output.get();
      ASSERT_EQ(expected[i], buf[i]);
    }

    // If we multiply the inputs by a constant factor, the output should not
    // vary more than 0.5 * factor.
    for (int c = 2; c < (1 << (bd - 8)); ++c) {
      int scaled_luma[32];
      for (int i = 0; i < 32; ++i) {
        scaled_luma[i] = luma[i] * c;
      }
      padded.reset(pad_8tap_convolve(scaled_luma, w, h));
      ASSERT_NE(padded, nullptr);
      av1_gaussian_blur(padded.get(), stride_8tap(w), w, h, output.get(), bd);
      for (int i = 0; i < w * h; ++i) {
        uint16_t *buf = output.get();
        ASSERT_GE(c / 2, abs(expected[i] * c - buf[i]));
      }
    }
  }
}

TEST(EdgeDetectImageTest, HardcodedBlurTest) { hardcoded_blur_test_aux(); }

TEST(EdgeDetectImageTest, SobelTest) {
  // Randomly generated 3x3. Compute Sobel for middle value.
  const uint16_t buf8_16[9] = { 241, 147, 7, 90, 184, 103, 28, 186, 2 };
  const int stride = 3;
  sobel_xy result = av1_sobel(buf8_16, stride, 1, 1);
  ASSERT_EQ(234, result.x);
  ASSERT_EQ(140, result.y);

  // Verify it works for high bit-depth values as well.
  const uint16_t buf16[9] = { 241, 147, 7, 90, 184, 2003, 1028, 186, 2 };
  result = av1_sobel(buf16, stride, 1, 1);
  ASSERT_EQ(-2566, result.x);
  ASSERT_EQ(-860, result.y);
}

INSTANTIATE_TEST_SUITE_P(EdgeDetectImages, EdgeDetectImageTest,
                         ::testing::Combine(
                             // Width
                             ::testing::Values(8, 16, 32),
                             // Height
                             ::testing::Values(4, 8, 12, 32),
                             // Bit depth
                             ::testing::Values(8, 10, 12)));
}  // namespace
