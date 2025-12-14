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
#include "common/stream_iter.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

// Dummy class that increments the width field in the image, which is used
// as a placeholder for the frame number. Stops iterating after N frames.
int dummy_reader(StreamIter *iter, avm_image_t *raw) {
  if (iter->current >= iter->n) {
    return 0;
  }
  ++iter->current;
  raw->w = iter->current;
  return 1;
}

void dummy_stream_iter_init(StreamIter *iter, int total) {
  iter->current = 0;
  iter->n = total;
  iter->reader = dummy_reader;
}

class StreamIterTest : public ::testing::Test {};

TEST_F(StreamIterTest, Skip0) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter skip;
  skip_stream_iter_init(&skip, &input, 0);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&skip, &img));
}

TEST_F(StreamIterTest, Skip1) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter skip;
  skip_stream_iter_init(&skip, &input, 1);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&skip, &img));
}

TEST_F(StreamIterTest, Skip2) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter skip;
  skip_stream_iter_init(&skip, &input, 2);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&skip, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&skip, &img));
}

TEST_F(StreamIterTest, Skip3) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter skip;
  skip_stream_iter_init(&skip, &input, 3);

  avm_image_t img;
  EXPECT_FALSE(read_stream_iter(&skip, &img));
}

TEST_F(StreamIterTest, Skip4) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter skip;
  skip_stream_iter_init(&skip, &input, 4);

  avm_image_t img;
  EXPECT_FALSE(read_stream_iter(&skip, &img));
}

// limit=0 is a special case meaning "no limit."
TEST_F(StreamIterTest, Limit0) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter limit;
  limit_stream_iter_init(&limit, &input, 0);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&limit, &img));
}

TEST_F(StreamIterTest, Limit1) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter limit;
  limit_stream_iter_init(&limit, &input, 1);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_FALSE(read_stream_iter(&limit, &img));
}

TEST_F(StreamIterTest, Limit2) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter limit;
  limit_stream_iter_init(&limit, &input, 2);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_FALSE(read_stream_iter(&limit, &img));
}

TEST_F(StreamIterTest, Limit3) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter limit;
  limit_stream_iter_init(&limit, &input, 3);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&limit, &img));
}

TEST_F(StreamIterTest, Limit4) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter limit;
  limit_stream_iter_init(&limit, &input, 4);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&limit, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&limit, &img));
}

TEST_F(StreamIterTest, Step1) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter step;
  step_stream_iter_init(&step, &input, 1);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(2U, img.w);

  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&step, &img));
}

TEST_F(StreamIterTest, Step2) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter step;
  step_stream_iter_init(&step, &input, 2);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(3U, img.w);

  EXPECT_FALSE(read_stream_iter(&step, &img));
}

TEST_F(StreamIterTest, Step3) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter step;
  step_stream_iter_init(&step, &input, 3);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_FALSE(read_stream_iter(&step, &img));
}

TEST_F(StreamIterTest, Step4) {
  StreamIter input;
  dummy_stream_iter_init(&input, 3);

  StreamIter step;
  step_stream_iter_init(&step, &input, 4);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_FALSE(read_stream_iter(&step, &img));
}

// Test when the step size is an even divisor of the number of frames.
TEST_F(StreamIterTest, Step5) {
  StreamIter input;
  dummy_stream_iter_init(&input, 10);

  StreamIter step;
  step_stream_iter_init(&step, &input, 5);

  avm_image_t img;
  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(1U, img.w);

  EXPECT_TRUE(read_stream_iter(&step, &img));
  EXPECT_EQ(6U, img.w);

  EXPECT_FALSE(read_stream_iter(&step, &img));
}

}  // namespace
