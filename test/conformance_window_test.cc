/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain a copy at aomedia.org/license/software-license/bsd-3-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain a copy at
 * aomedia.org/license/patent-license/.
 */

#include <string.h>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"

#include "av2/av2_iface_common.h"
#include "av2/common/av2_common_int.h"
#include "av2/decoder/decodeframe.h"
#include "avm/internal/avm_codec_internal.h"

namespace {

// 46341 * 46342 exceeds INT32_MAX by 51,575, so scaling a conformance
// window offset that is valid (offset < max_frame_width) onto a frame that
// is as large as the sequence maximum overflows 32-bit signed arithmetic.
// The tests below use exactly this geometry: with the products computed in
// 64 bits the window is conformance-checked and mapped inside the frame;
// with the pre-widening wraparound the left position went negative, which
// silently bypassed the conformance requirements and moved the output
// plane pointers below the start of the frame buffer.

TEST(ConformanceWindowTest, OverflowingProductIsStillConformanceChecked) {
  SequenceHeader seq;
  memset(&seq, 0, sizeof(seq));
  seq.max_frame_width = 46342;
  seq.max_frame_height = 16;
  seq.conf.conf_win_enabled_flag = 1;
  seq.conf.conf_win_left_offset = 46341;
  seq.conf.conf_win_right_offset = 1;
  seq.monochrome = 1;
  seq.subsampling_x = 1;
  seq.subsampling_y = 1;

  avm_internal_error_info error;
  memset(&error, 0, sizeof(error));

  av2_validate_frame_level_conformance(&seq, 46342, 16, &error);

  // LeftPosX = 46341 and RightPosX = 46340, so conformance requirement 3
  // (LeftPosX <= RightPosX) must reject the window. Before the products
  // were widened, the wrapped LeftPosX was negative and the requirement
  // was silently bypassed.
  EXPECT_EQ(error.error_code, AVM_CODEC_UNSUP_BITSTREAM);
}

TEST(ConformanceWindowTest, OutputPlaneOffsetsStayInsideTheFrame) {
  YV12_BUFFER_CONFIG yv12;
  memset(&yv12, 0, sizeof(yv12));
  yv12.y_crop_width = 46342;
  yv12.y_crop_height = 16;
  yv12.y_width = 46344;
  yv12.y_height = 16;
  yv12.max_width = 46342;
  yv12.max_height = 16;
  yv12.subsampling_x = 1;
  yv12.subsampling_y = 1;
  yv12.monochrome = 1;
  yv12.w_conf_win_enabled_flag = 1;
  yv12.w_win_left_offset = 46341;
  yv12.w_win_right_offset = 0;
  yv12.w_win_top_offset = 0;
  yv12.w_win_bottom_offset = 0;
  static uint8_t y_base[1];
  yv12.y_buffer = (uint16_t *)y_base;

  avm_image_t img;
  memset(&img, 0, sizeof(img));

  yuvconfig2image(&img, &yv12, NULL);

  // The left position is 46341 samples inside a 46342 sample wide frame,
  // so exactly one column stays visible and the Y plane pointer moves
  // forward by 46341 samples (2 bytes each in the decoder's high bitdepth
  // internal representation). Before the widening, the wrapped offset was
  // negative: the pointer moved below the frame buffer and the cropped
  // width grew past the frame width.
  EXPECT_EQ(img.d_w, 1u);
  EXPECT_EQ(img.d_h, 16u);
  EXPECT_EQ(img.planes[AVM_PLANE_Y], y_base + 2 * 46341);
}

}  // namespace
