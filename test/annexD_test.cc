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

#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/enums.h"
#include "av2/decoder/annexD.h"
#include "avm_scale/yv12config.h"

namespace {

// Allocates a 4:2:0 8-bit buffer and fills Y/U/V with constants.
class Frame {
 public:
  Frame(int w, int h, int y, int u, int v, int bd = 8, int ssx = 1, int ssy = 1,
        int mono = 0) {
    memset(&buf_, 0, sizeof(buf_));
    EXPECT_EQ(avm_alloc_frame_buffer(&buf_, w, h, /*ss_x=*/ssx, /*ss_y=*/ssy,
                                     /*border=*/0, /*byte_alignment=*/0,
                                     /*alloc_pyramid=*/false),
              0);
    buf_.bit_depth = bd;
    buf_.monochrome = mono;
    Fill(buf_.y_buffer, buf_.y_stride, buf_.y_crop_width, buf_.y_crop_height,
         y);
    if (!mono) {
      Fill(buf_.u_buffer, buf_.uv_stride, buf_.uv_crop_width,
           buf_.uv_crop_height, u);
      Fill(buf_.v_buffer, buf_.uv_stride, buf_.uv_crop_width,
           buf_.uv_crop_height, v);
    }
  }
  ~Frame() { avm_free_frame_buffer(&buf_); }
  YV12_BUFFER_CONFIG *cfg() { return &buf_; }

  static void Fill(uint16_t *p, int stride, int w, int h, int val) {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) p[j * stride + i] = (uint16_t)val;
  }

 private:
  YV12_BUFFER_CONFIG buf_;
};

uint16_t Ypix(const YV12_BUFFER_CONFIG *b, int x, int y) {
  return b->y_buffer[y * b->y_stride + x];
}
uint16_t Upix(const YV12_BUFFER_CONFIG *b, int x, int y) {
  return b->u_buffer[y * b->uv_stride + x];
}

// Builds an atlas with `n` segments; geometry arrays supplied by caller.
struct AtlasBuilder {
  AtlasSegmentInfo atlas;
  AtlasBasicInfo bi;
  AtlasBuilder(int w, int h, int n) {
    memset(&atlas, 0, sizeof(atlas));
    memset(&bi, 0, sizeof(bi));
    atlas.atlas_segment_mode_idc = MULTISTREAM_ATLAS;
    atlas.ats_basic_info = &bi;
    bi.ats_atlas_width = w;
    bi.ats_atlas_height = h;
    bi.ats_num_atlas_segments_minus_1 = n - 1;
  }
  void Seg(int i, int x, int y, int w, int h, int stream_id) {
    bi.ats_segment_top_left_pos_x[i] = x;
    bi.ats_segment_top_left_pos_y[i] = y;
    bi.ats_segment_width[i] = w;
    bi.ats_segment_height[i] = h;
    bi.ats_input_stream_id[i] = stream_id;
  }
  void Background(int r, int g, int b) {
    bi.ats_background_info_present_flag = 1;
    bi.ats_background_red_value = r;
    bi.ats_background_green_value = g;
    bi.ats_background_blue_value = b;
  }
  // Switch to MULTISTREAM_ALPHA_ATLAS and mark segment `i` as an alpha matte
  // (paired with the following texture segment).
  void AlphaMode() {
    atlas.atlas_segment_mode_idc = MULTISTREAM_ALPHA_ATLAS;
    bi.ats_alpha_segments_present_flag = 1;
  }
  void AlphaSeg(int i) { bi.ats_alpha_segment_flag[i] = 1; }
};

// Annex D alpha blend reference: Round2((alphaMax-a)*bg + a*fg, bd), with
// alphaMax = 1<<bd. (bd = 8 here.)
static int AlphaBlend(int bg, int a, int fg) {
  const long long am = 1 << 8;
  const long long t = (am - a) * bg + (long long)a * fg;
  return (int)((t + (1 << 7)) >> 8);
}

TEST(AnnexDComposeTest, TwoUpNoResample) {
  AtlasBuilder ab(64, 32, 2);
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 32, 0, 32, 32, 1);
  Frame s0(32, 32, 100, 110, 120), s1(32, 32, 200, 210, 220);
  Frame canvas(64, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { s0.cfg(), s1.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 0, 0), 100);
  EXPECT_EQ(Ypix(canvas.cfg(), 31, 31), 100);
  EXPECT_EQ(Ypix(canvas.cfg(), 32, 0), 200);
  EXPECT_EQ(Ypix(canvas.cfg(), 63, 31), 200);
  EXPECT_EQ(Upix(canvas.cfg(), 0, 0), 110);
  EXPECT_EQ(Upix(canvas.cfg(), 31, 15),
            210);  // chroma x=31 -> luma x=62 (seg1)
}

TEST(AnnexDComposeTest, BackgroundFillAroundSegment) {
  AtlasBuilder ab(64, 64, 1);
  ab.Seg(0, 16, 16, 32, 32, 0);
  ab.Background(0, 0, 0);  // BT.709 limited black -> Y=16, U=V=128
  Frame s0(32, 32, 100, 100, 100);
  Frame canvas(64, 64, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[1] = { s0.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 1, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 0, 0), 16);     // background
  EXPECT_EQ(Ypix(canvas.cfg(), 63, 63), 16);   // background
  EXPECT_EQ(Ypix(canvas.cfg(), 16, 16), 100);  // segment
  EXPECT_EQ(Ypix(canvas.cfg(), 47, 47), 100);  // segment edge
  EXPECT_EQ(Upix(canvas.cfg(), 0, 0), 128);    // background chroma
}

TEST(AnnexDComposeTest, NullSourceLeavesBackground) {
  AtlasBuilder ab(64, 32, 2);
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 32, 0, 32, 32, 1);
  ab.Background(0, 0, 0);
  Frame s0(32, 32, 100, 100, 100);
  Frame canvas(64, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { s0.cfg(), nullptr };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 0, 0), 100);   // segment 0 present
  EXPECT_EQ(Ypix(canvas.cfg(), 40, 10), 16);  // segment 1 missing -> background
}

TEST(AnnexDComposeTest, OverlapLaterSegmentWins) {
  AtlasBuilder ab(32, 32, 2);
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 0, 0, 32, 32, 1);
  Frame s0(32, 32, 100, 100, 100), s1(32, 32, 200, 200, 200);
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { s0.cfg(), s1.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 10, 10), 200);  // second segment overwrites
}

TEST(AnnexDComposeTest, ResampleConstantIsExact) {
  AtlasBuilder ab(32, 32, 1);
  ab.Seg(0, 0, 0, 32, 32, 0);
  Frame s0(16, 16, 150, 140, 130);  // smaller than 32x32 segment -> upscaled
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[1] = { s0.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 1, canvas.cfg()), 0);
  // Resampling a constant plane yields the same constant in the interior.
  EXPECT_EQ(Ypix(canvas.cfg(), 16, 16), 150);
  EXPECT_EQ(Upix(canvas.cfg(), 8, 8), 140);
}

TEST(AnnexDComposeTest, HigherDepthTextureDownconverts) {
  // A 10-bit texture placed into an 8-bit canvas is down-converted (Round2),
  // not rejected.
  AtlasBuilder ab(32, 32, 1);
  ab.Seg(0, 0, 0, 32, 32, 0);
  Frame s0(32, 32, 400, 200, 200, /*bd=*/10);
  Frame canvas(32, 32, 0, 0, 0, /*bd=*/8);
  YV12_BUFFER_CONFIG *srcs[1] = { s0.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 1, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 16, 16),
            (400 + 2) >> 2);                            // Round2(400, 2) = 100
  EXPECT_EQ(Upix(canvas.cfg(), 8, 8), (200 + 2) >> 2);  // Round2(200, 2) = 50
}

TEST(AnnexDComposeTest, AlphaOpaqueEqualsTexture) {
  // 2 segments at (0,0): seg0 = alpha matte (opaque), seg1 = texture.
  AtlasBuilder ab(32, 32, 2);
  ab.AlphaMode();
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 0, 0, 32, 32, 1);
  ab.AlphaSeg(0);
  ab.Background(0, 0, 0);  // Y=16, U=V=128
  Frame alpha(32, 32, 255, 128, 128), tex(32, 32, 200, 210, 220);
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { alpha.cfg(), tex.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 10, 10), AlphaBlend(16, 255, 200));
  EXPECT_EQ(Upix(canvas.cfg(), 5, 5), AlphaBlend(128, 255, 210));
}

TEST(AnnexDComposeTest, AlphaZeroKeepsBackground) {
  AtlasBuilder ab(32, 32, 2);
  ab.AlphaMode();
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 0, 0, 32, 32, 1);
  ab.AlphaSeg(0);
  ab.Background(0, 0, 0);  // Y=16, U=V=128
  Frame alpha(32, 32, 0, 128, 128), tex(32, 32, 200, 210, 220);
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { alpha.cfg(), tex.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 10, 10), 16);  // fully transparent -> background
  EXPECT_EQ(Upix(canvas.cfg(), 5, 5), 128);
}

TEST(AnnexDComposeTest, AlphaHalfBlends) {
  AtlasBuilder ab(32, 32, 2);
  ab.AlphaMode();
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 0, 0, 32, 32, 1);
  ab.AlphaSeg(0);
  ab.Background(0, 0, 0);  // Y=16, U=V=128
  Frame alpha(32, 32, 128, 128, 128), tex(32, 32, 200, 210, 220);
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { alpha.cfg(), tex.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 10, 10), AlphaBlend(16, 128, 200));
  EXPECT_EQ(Upix(canvas.cfg(), 5, 5), AlphaBlend(128, 128, 210));
}

TEST(AnnexDComposeTest, AlphaSegmentOffsetOpaqueOutside) {
  // Alpha segment covers only the left half; the texture covers the whole
  // canvas. Inside the alpha region the matte is transparent (-> background);
  // outside it the texture is opaque.
  AtlasBuilder ab(32, 32, 2);
  ab.AlphaMode();
  ab.Seg(0, 0, 0, 16, 32, 0);  // alpha matte: left half only
  ab.Seg(1, 0, 0, 32, 32, 1);  // texture: full canvas
  ab.AlphaSeg(0);
  ab.Background(0, 0, 0);  // Y=16
  Frame alpha(16, 32, 0, 128, 128), tex(32, 32, 200, 210, 220);
  Frame canvas(32, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { alpha.cfg(), tex.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 5, 5), 16);    // inside alpha, transparent -> bg
  EXPECT_EQ(Ypix(canvas.cfg(), 20, 5), 200);  // outside alpha -> opaque texture
}

TEST(AnnexDComposeTest, MixedDepthTexturesUpconvert) {
  // seg0 = 8-bit texture, seg1 = 10-bit texture; canvas at 10-bit.
  AtlasBuilder ab(64, 32, 2);
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 32, 0, 32, 32, 1);
  Frame t8(32, 32, 200, 128, 128, /*bd=*/8);
  Frame t10(32, 32, 900, 500, 500, /*bd=*/10);
  Frame canvas(64, 32, 0, 0, 0, /*bd=*/10);
  YV12_BUFFER_CONFIG *srcs[2] = { t8.cfg(), t10.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  EXPECT_EQ(Ypix(canvas.cfg(), 0, 0), 200 << 2);  // 8-bit upconverted
  EXPECT_EQ(Ypix(canvas.cfg(), 32, 0), 900);      // 10-bit native
  EXPECT_EQ(Upix(canvas.cfg(), 0, 0), 128 << 2);  // 8-bit chroma upconverted
  EXPECT_EQ(Upix(canvas.cfg(), 16, 0), 500);      // 10-bit chroma native
}

TEST(AnnexDComposeTest, MixedDepthAlphaBlend) {
  // 8-bit texture blended by an 8-bit matte into a 10-bit canvas.
  AtlasBuilder ab(32, 32, 2);
  ab.AlphaMode();
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 0, 0, 32, 32, 1);
  ab.AlphaSeg(0);
  ab.Background(0, 0, 0);  // 10-bit limited black: Y = 16<<2 = 64
  Frame alpha(32, 32, 128, 128, 128, /*bd=*/8);
  Frame tex(32, 32, 200, 128, 128, /*bd=*/8);
  Frame canvas(32, 32, 0, 0, 0, /*bd=*/10);
  YV12_BUFFER_CONFIG *srcs[2] = { alpha.cfg(), tex.cfg() };

  ASSERT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), 0);
  // bg=64 (10-bit), tex upconverted 200<<2=800, alpha 128 (alphaMax 1<<8).
  EXPECT_EQ(Ypix(canvas.cfg(), 10, 10), AlphaBlend(64, 128, 200 << 2));
}

TEST(AnnexDComposeTest, MixedChromaErrors) {
  // A monochrome texture against a color canvas is rejected (no chroma conv).
  AtlasBuilder ab(64, 32, 2);
  ab.Seg(0, 0, 0, 32, 32, 0);
  ab.Seg(1, 32, 0, 32, 32, 1);
  Frame color(32, 32, 100, 110, 120);
  Frame mono(32, 32, 200, 0, 0, /*bd=*/8, /*ssx=*/1, /*ssy=*/1, /*mono=*/1);
  Frame canvas(64, 32, 0, 0, 0);
  YV12_BUFFER_CONFIG *srcs[2] = { color.cfg(), mono.cfg() };

  EXPECT_EQ(av2_annex_d_compose_tu(&ab.atlas, srcs, 2, canvas.cfg()), -1);
}

}  // namespace
