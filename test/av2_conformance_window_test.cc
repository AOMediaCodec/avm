/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <string.h>

#include "av2/common/av2_common_int.h"
#include "av2/decoder/decodeframe.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

// Sets up a sequence header whose conformance window offsets are individually
// legal (each one is below the sequence maximum) but which have to be scaled by
// the frame size. Both quantities are bounded by 65536, so the product of an
// offset and the matching frame dimension does not fit in a 32-bit int and the
// scaling has to be done in 64 bits.
void init_large_conformance_window(SequenceHeader *seq) {
  memset(seq, 0, sizeof(*seq));

  seq->max_frame_width = 65536;
  seq->max_frame_height = 65536;
  // Keep the chroma alignment requirements inactive so that the window is
  // rejected by the position ordering checks below.
  seq->subsampling_x = 0;
  seq->subsampling_y = 0;

  seq->conf.conf_win_enabled_flag = true;
  seq->conf.conf_win_left_offset = 65535;
  seq->conf.conf_win_right_offset = 65535;
  seq->conf.conf_win_top_offset = 65535;
  seq->conf.conf_win_bottom_offset = 65535;
}

// The offsets scale to 65535, so the left position (65535) lies to the right of
// the right position (65536 - 1 - 65535 == 0). The window is invalid regardless
// of overflow; the point of the test is that the overflow must not hide that
// from the checks.
TEST(ConformanceWindowTest, ScaledOffsetsDoNotOverflow) {
  SequenceHeader seq;
  init_large_conformance_window(&seq);

  struct avm_internal_error_info error_info;
  memset(&error_info, 0, sizeof(error_info));
  error_info.setjmp = 0;  // Record the error instead of longjmp-ing.

  av2_validate_frame_level_conformance(&seq, seq.max_frame_width,
                                       seq.max_frame_height, &error_info);
  EXPECT_EQ(error_info.error_code, AVM_CODEC_UNSUP_BITSTREAM);
}

// A conforming window must still be accepted, so that the change above is not
// over-rejecting.
TEST(ConformanceWindowTest, ValidWindowIsAccepted) {
  SequenceHeader seq;
  memset(&seq, 0, sizeof(seq));

  seq.max_frame_width = 1920;
  seq.max_frame_height = 1080;
  seq.subsampling_x = 1;
  seq.subsampling_y = 1;

  seq.conf.conf_win_enabled_flag = true;
  seq.conf.conf_win_left_offset = 0;
  seq.conf.conf_win_right_offset = 1;
  seq.conf.conf_win_top_offset = 0;
  seq.conf.conf_win_bottom_offset = 1;

  struct avm_internal_error_info error_info;
  memset(&error_info, 0, sizeof(error_info));
  error_info.setjmp = 0;

  av2_validate_frame_level_conformance(&seq, 1920, 1080, &error_info);
  EXPECT_EQ(error_info.error_code, AVM_CODEC_OK);
}

}  // namespace
