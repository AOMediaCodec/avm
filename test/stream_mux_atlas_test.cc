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

#include <cstdio>
#include <sstream>
#include <string>

#include "test/video_source.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "tools/multistream_atlas_json.h"

namespace {

class ScopedTempJsonFile {
 public:
  explicit ScopedTempJsonFile(const std::string &text) {
    FILE *f = libavm_test::GetTempOutFile(&path_);
    EXPECT_NE(f, nullptr);
    if (f != nullptr) {
      EXPECT_EQ(text.size(), fwrite(text.data(), 1, text.size(), f));
      fclose(f);
    }
  }
  ~ScopedTempJsonFile() {
    if (!path_.empty()) {
      remove(path_.c_str());
    }
  }
  const char *c_str() const { return path_.c_str(); }

 private:
  std::string path_;
};

}  // namespace

// ---------- JSON loader: positive cases -----------------------------------

TEST(StreamMuxAtlas, JsonDefaultEmitPolicyIsEveryTu) {
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 1920,
      "msi_height": 1080,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1920, "height": 1080 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(file.c_str(), &cfg, &err)) << err;
  EXPECT_EQ(AtlasEmitPolicy::kEveryTu, cfg.emit_policy);
  EXPECT_EQ(0, cfg.atlas_multistream_info.atlas_segment_id);
  EXPECT_EQ(1920, cfg.atlas_multistream_info.msi_width);
  EXPECT_EQ(1080, cfg.atlas_multistream_info.msi_height);
  EXPECT_FALSE(cfg.atlas_multistream_info.background_present);
  ASSERT_EQ(1u, cfg.atlas_multistream_info.segments.size());
  EXPECT_EQ(0, cfg.atlas_multistream_info.segments[0].input_stream_id);
}

TEST(StreamMuxAtlas, JsonAtlasSegmentIdDefaultsToZero) {
  // atlas_segment_id is optional and defaults to 0 when omitted.
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(file.c_str(), &cfg, &err)) << err;
  EXPECT_EQ(0, cfg.atlas_multistream_info.atlas_segment_id);
}

TEST(StreamMuxAtlas, JsonEmitPolicyValues) {
  const std::string base = R"({
    "emit_policy": "%s",
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  char buf1[1024];
  char buf2[1024];
  snprintf(buf1, sizeof(buf1), base.c_str(), "every_tu");
  snprintf(buf2, sizeof(buf2), base.c_str(), "with_msdo");

  ScopedTempJsonFile p1(buf1);
  ScopedTempJsonFile p2(buf2);
  AtlasMultistreamConfig c1, c2;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(p1.c_str(), &c1, &err)) << err;
  EXPECT_EQ(AtlasEmitPolicy::kEveryTu, c1.emit_policy);
  ASSERT_TRUE(LoadAtlasJson(p2.c_str(), &c2, &err)) << err;
  EXPECT_EQ(AtlasEmitPolicy::kWithMsdo, c2.emit_policy);
}

TEST(StreamMuxAtlas, JsonWithBackgroundParsedCorrectly) {
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 8, "msi_height": 8,
      "background": { "red_value": 11, "green_value": 22, "blue_value": 33 },
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 8, "height": 8 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(file.c_str(), &cfg, &err)) << err;
  EXPECT_TRUE(cfg.atlas_multistream_info.background_present);
  EXPECT_EQ(11, cfg.atlas_multistream_info.background_r);
  EXPECT_EQ(22, cfg.atlas_multistream_info.background_g);
  EXPECT_EQ(33, cfg.atlas_multistream_info.background_b);
}

TEST(StreamMuxAtlas, JsonMaxSegmentsAndMaxGeometryValues) {
  std::ostringstream oss;
  oss << R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 7,
      "atlas_segment_mode_idc": "MULTISTREAM_ALPHA_ATLAS",
      "alpha_segments_present": true,
      "msi_width": 2147483647,
      "msi_height": 2147483647,
      "background": { "red_value": 255, "green_value": 255, "blue_value": 255 },
      "segments": [)";
  for (int i = 0; i < 256; ++i) {
    if (i > 0) oss << ",";
    oss << "\n        { \"input_stream_id\": " << (i % 32)
        << ", \"top_left_pos_x\": 2147483647"
        << ", \"top_left_pos_y\": 2147483647"
        << ", \"width\": 2147483647"
        << ", \"height\": 2147483647"
        << ", \"alpha_segment\": true }";
  }
  oss << "\n      ]\n    }\n  }";
  ScopedTempJsonFile file(oss.str());
  AtlasMultistreamConfig cfg;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(file.c_str(), &cfg, &err)) << err;
  EXPECT_EQ(256u, cfg.atlas_multistream_info.segments.size());
  EXPECT_EQ(INT_MAX, cfg.atlas_multistream_info.msi_width);
  EXPECT_EQ(INT_MAX, cfg.atlas_multistream_info.segments[255].width);
}

// ---------- JSON loader: negative cases -----------------------------------

TEST(StreamMuxAtlas, JsonAcceptsAlphaMode) {
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ALPHA_ATLAS",
      "alpha_segments_present": true,
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1, "alpha_segment": true },
        { "input_stream_id": 1, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  ASSERT_TRUE(LoadAtlasJson(file.c_str(), &cfg, &err)) << err;
  EXPECT_TRUE(cfg.atlas_multistream_info.alpha);
  EXPECT_TRUE(cfg.atlas_multistream_info.alpha_segments_present);
  EXPECT_TRUE(cfg.atlas_multistream_info.segments[0].alpha_segment);
}

TEST(StreamMuxAtlas, JsonRejectAlphaPresentWithoutAlphaMode) {
  // alpha_segments_present is only valid for MULTISTREAM_ALPHA_ATLAS.
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "alpha_segments_present": true,
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  EXPECT_FALSE(LoadAtlasJson(file.c_str(), &cfg, &err));
}

TEST(StreamMuxAtlas, JsonRejectOutOfRangeStreamId) {
  const std::string json = R"({
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 32, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  EXPECT_FALSE(LoadAtlasJson(file.c_str(), &cfg, &err));
}

TEST(StreamMuxAtlas, JsonRejectInvalidEmitPolicy) {
  const std::string json = R"({
    "emit_policy": "sometimes",
    "atlas_multistream_info": {
      "atlas_segment_id": 0,
      "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
      "msi_width": 1, "msi_height": 1,
      "segments": [
        { "input_stream_id": 0, "top_left_pos_x": 0, "top_left_pos_y": 0,
          "width": 1, "height": 1 }
      ]
    }
  })";
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  EXPECT_FALSE(LoadAtlasJson(file.c_str(), &cfg, &err));
}

// The legacy multi-atlas form (top-level `atlas_segments` array) is no longer
// supported. A JSON using only that key fails naturally because the required
// `atlas_multistream_info` object is missing.

TEST(StreamMuxAtlas, JsonRejectMalformed) {
  const std::string json = "{\"atlas_multistream_info\": {";  // syntax error
  ScopedTempJsonFile file(json);
  AtlasMultistreamConfig cfg;
  std::string err;
  EXPECT_FALSE(LoadAtlasJson(file.c_str(), &cfg, &err));
}
