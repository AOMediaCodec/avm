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

#ifndef AOM_COMMON_VIDEO_COMMON_H_
#define AOM_COMMON_VIDEO_COMMON_H_

#include "common/tools_common.h"

typedef struct {
  uint32_t codec_fourcc;
  int frame_width;
  int frame_height;
  struct AvxRational time_base;
} AvxVideoInfo;

#endif  // AOM_COMMON_VIDEO_COMMON_H_
