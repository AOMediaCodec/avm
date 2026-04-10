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

// Test that av2_add_film_grain is thread-safe when called concurrently
// with images of different chroma subsampling (4:2:0 vs 4:4:4).
// Reproduces the data race on static globals in grain_synthesis.c.

#include <cstring>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "avm/avm_image.h"
#include "avm_dsp/grain_synthesis.h"

namespace {

// Minimal film grain params that trigger the overlap path.
avm_film_grain_t MakeGrainParams() {
  avm_film_grain_t params;
  memset(&params, 0, sizeof(params));
  params.apply_grain = 1;
  params.update_parameters = 1;
  params.fgm_points[0] = 2;
  params.fgm_scaling_points_0[0][0] = 0;
  params.fgm_scaling_points_0[0][1] = 96;
  params.fgm_scaling_points_0[1][0] = 255;
  params.fgm_scaling_points_0[1][1] = 96;
  params.fgm_points[1] = 2;
  params.fgm_scaling_points_1[0][0] = 0;
  params.fgm_scaling_points_1[0][1] = 64;
  params.fgm_scaling_points_1[1][0] = 255;
  params.fgm_scaling_points_1[1][1] = 64;
  params.fgm_points[2] = 2;
  params.fgm_scaling_points_2[0][0] = 0;
  params.fgm_scaling_points_2[0][1] = 64;
  params.fgm_scaling_points_2[1][0] = 255;
  params.fgm_scaling_points_2[1][1] = 64;
  params.scaling_shift = 11;
  params.ar_coeff_lag = 1;
  params.ar_coeff_shift = 7;
  params.overlap_flag = 1;
  params.bit_depth = 8;
  params.random_seed = 7391;
  params.cb_mult = 128;
  params.cb_luma_mult = 192;
  params.cb_offset = 256;
  params.cr_mult = 128;
  params.cr_luma_mult = 192;
  params.cr_offset = 256;
  return params;
}

void RunGrain(avm_img_fmt_t fmt, int iterations) {
  const int width = 128;
  const int height = 128;

  avm_image_t src;
  ASSERT_NE(avm_img_alloc(&src, fmt, width, height, 32), nullptr);
  memset(src.planes[AVM_PLANE_Y], 128,
         (size_t)src.stride[AVM_PLANE_Y] * height);
  int chroma_h = (fmt == AVM_IMG_FMT_I420) ? height / 2 : height;
  memset(src.planes[AVM_PLANE_U], 128,
         (size_t)src.stride[AVM_PLANE_U] * chroma_h);
  memset(src.planes[AVM_PLANE_V], 128,
         (size_t)src.stride[AVM_PLANE_V] * chroma_h);
  src.bit_depth = 8;
  src.mc = AVM_CICP_MC_BT_709;

  avm_image_t dst;
  ASSERT_NE(avm_img_alloc(&dst, fmt, width, height, 32), nullptr);
  dst.bit_depth = 8;
  dst.mc = AVM_CICP_MC_BT_709;

  avm_film_grain_t params = MakeGrainParams();

  for (int i = 0; i < iterations; ++i) {
    params.random_seed = (uint16_t)(7391 + i);
    int ret = av2_add_film_grain(&params, &src, &dst);
    ASSERT_EQ(ret, 0) << "av2_add_film_grain failed on iteration " << i;
  }

  avm_img_free(&src);
  avm_img_free(&dst);
}

TEST(GrainSynthesisRaceTest, ConcurrentDifferentSubsampling) {
  const int kThreads = 4;
  const int kIterations = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreads * 2);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] { RunGrain(AVM_IMG_FMT_I420, kIterations); });
    threads.emplace_back([&] { RunGrain(AVM_IMG_FMT_I444, kIterations); });
  }

  for (auto &t : threads) {
    t.join();
  }
}

}  // namespace
