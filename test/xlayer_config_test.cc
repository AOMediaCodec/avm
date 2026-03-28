/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <stdio.h>
#include <string.h>

#include <string>

#include "common/xlayer_config.h"
#include "common/xlayer_config_parse.h"
#include "avm/avmcx.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

// Helper: write a string to a temporary file and return the path.
// Uses a static buffer — only one temp file at a time.
static char temp_path[256];

const char *WriteTempJson(const char *json_str) {
  snprintf(temp_path, sizeof(temp_path), "%s",
           testing::TempDir().append("xlayer_test.json").c_str());
  FILE *f = fopen(temp_path, "w");
  EXPECT_NE(f, nullptr);
  if (!f) return nullptr;
  fputs(json_str, f);
  fclose(f);
  return temp_path;
}

// --- Config Init Tests ---

TEST(XLayerConfig, InitDefaults) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);

  EXPECT_EQ(cfg.num_xlayers, 0);
  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.lcr_doh_constraint_flag, 1);
  EXPECT_EQ(cfg.combined_tu, 1);
  EXPECT_EQ(cfg.monotonic_output_order, 1);
  EXPECT_EQ(cfg.limit, 0);
  EXPECT_EQ(cfg.enable_msdo, 0);
  EXPECT_EQ(cfg.enable_atlas, 0);
  EXPECT_EQ(cfg.num_ops_sets, 0);

  // Check xlayer defaults
  for (int i = 0; i < MAX_NUM_XLAYERS - 1; i++) {
    EXPECT_EQ(cfg.xlayers[i].xlayer_id, -1);
    EXPECT_EQ(cfg.xlayers[i].qp, -1);
    EXPECT_EQ(cfg.xlayers[i].bitrate, -1);
    EXPECT_EQ(cfg.xlayers[i].cpu_used, -1);
    EXPECT_EQ(cfg.xlayers[i].lag_in_frames, -1);
    EXPECT_EQ(cfg.xlayers[i].profile, (unsigned int)MAIN_420_10_IP1);
    EXPECT_EQ(cfg.xlayers[i].level, (unsigned int)SEQ_LEVEL_4_0);
    EXPECT_EQ(cfg.xlayers[i].num_temporal_layers, 1);
    EXPECT_EQ(cfg.xlayers[i].num_embedded_layers, 1);
    EXPECT_EQ(cfg.xlayers[i].view_type, VIEW_UNSPECIFIED);
  }
}

// --- JSON Parsing Tests ---

TEST(XLayerConfigParse, MinimalTwoLayer) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "qp": 128, "cpu_used": 5 },
      { "xlayer_id": 1, "input": "b.raw", "width": 1920, "height": 1080,
        "qp": 160, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.num_xlayers, 2);
  EXPECT_EQ(cfg.xlayers[0].xlayer_id, 0);
  EXPECT_EQ(cfg.xlayers[1].xlayer_id, 1);
  EXPECT_STREQ(cfg.xlayers[0].input_filename, "a.raw");
  EXPECT_STREQ(cfg.xlayers[1].input_filename, "b.raw");
  EXPECT_EQ(cfg.xlayers[0].width, 1920u);
  EXPECT_EQ(cfg.xlayers[0].height, 1080u);
  EXPECT_EQ(cfg.xlayers[0].qp, 128);
  EXPECT_EQ(cfg.xlayers[1].qp, 160);
  EXPECT_STREQ(cfg.output_filename, "out.obu");

  // Defaults should apply
  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.combined_tu, 1);
}

TEST(XLayerConfigParse, LayerTypes) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "layer_type": "texture",
        "view_type": "left" },
      { "xlayer_id": 1, "input": "b.raw", "layer_type": "auxiliary",
        "auxiliary_type": "depth" },
      { "xlayer_id": 2, "input": "c.raw", "layer_type": "stereo",
        "view_type": "right" }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].layer_type, TEXTURE_LAYER);
  EXPECT_EQ(cfg.xlayers[0].view_type, VIEW_LEFT);
  EXPECT_EQ(cfg.xlayers[1].layer_type, AUX_LAYER);
  EXPECT_EQ(cfg.xlayers[1].auxiliary_type, LCR_DEPTH_AUX);
  EXPECT_EQ(cfg.xlayers[2].layer_type, STEREO_LAYER);
  EXPECT_EQ(cfg.xlayers[2].view_type, VIEW_RIGHT);
}

TEST(XLayerConfigParse, AllAuxiliaryTypes) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "layer_type": "auxiliary",
        "auxiliary_type": "alpha" },
      { "xlayer_id": 1, "input": "b.raw", "layer_type": "auxiliary",
        "auxiliary_type": "depth" },
      { "xlayer_id": 2, "input": "c.raw", "layer_type": "auxiliary",
        "auxiliary_type": "segmentation" },
      { "xlayer_id": 3, "input": "d.raw", "layer_type": "auxiliary",
        "auxiliary_type": "gain_map" }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].auxiliary_type, LCR_ALPHA_AUX);
  EXPECT_EQ(cfg.xlayers[1].auxiliary_type, LCR_DEPTH_AUX);
  EXPECT_EQ(cfg.xlayers[2].auxiliary_type, LCR_SEGMENTATION_AUX);
  EXPECT_EQ(cfg.xlayers[3].auxiliary_type, LCR_GAIN_MAP_AUX);
}

TEST(XLayerConfigParse, GlobalLcrSection) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw" }
    ],
    "global_lcr": {
      "enable": true,
      "purpose_id": 3,
      "dependent_xlayers": true,
      "doh_constraint": false
    }
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.lcr_purpose_id, 3);
  EXPECT_EQ(cfg.lcr_dependent_xlayers_flag, 1);
  EXPECT_EQ(cfg.lcr_doh_constraint_flag, 0);
}

TEST(XLayerConfigParse, MsdoSection) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw" }
    ],
    "msdo": { "enable": true }
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.enable_msdo, 1);
}

TEST(XLayerConfigParse, OpsSection) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw" },
      { "xlayer_id": 1, "input": "b.raw" }
    ],
    "ops": [
      {
        "ops_id": 0,
        "priority": 2,
        "intent_present": true,
        "ptl_present": true,
        "operating_points": [
          { "intent": 0, "xlayer_map": [0] },
          { "intent": 1, "xlayer_map": [0, 1] }
        ]
      }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.num_ops_sets, 1);
  EXPECT_EQ(cfg.ops_sets[0].ops_id, 0);
  EXPECT_EQ(cfg.ops_sets[0].priority, 2);
  EXPECT_EQ(cfg.ops_sets[0].intent_present_flag, 1);
  EXPECT_EQ(cfg.ops_sets[0].ptl_present_flag, 1);
  EXPECT_EQ(cfg.ops_sets[0].num_operating_points, 2);

  // OP0: xlayer 0 only => bitmask = 0x1
  EXPECT_EQ(cfg.ops_sets[0].ops[0].intent, 0);
  EXPECT_EQ(cfg.ops_sets[0].ops[0].xlayer_map, 1u);

  // OP1: xlayers 0 and 1 => bitmask = 0x3
  EXPECT_EQ(cfg.ops_sets[0].ops[1].intent, 1);
  EXPECT_EQ(cfg.ops_sets[0].ops[1].xlayer_map, 3u);
}

TEST(XLayerConfigParse, EncoderOverrideDefaults) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 5, "input": "a.raw" }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // Unspecified overrides should be -1
  EXPECT_EQ(cfg.xlayers[0].qp, -1);
  EXPECT_EQ(cfg.xlayers[0].bitrate, -1);
  EXPECT_EQ(cfg.xlayers[0].cpu_used, -1);
  EXPECT_EQ(cfg.xlayers[0].lag_in_frames, -1);

  // Defaults from init
  EXPECT_EQ(cfg.xlayers[0].profile, (unsigned int)MAIN_420_10_IP1);
  EXPECT_EQ(cfg.xlayers[0].num_temporal_layers, 1);
  EXPECT_EQ(cfg.xlayers[0].num_embedded_layers, 1);
}

// --- Error / Invalid Input Tests ---

TEST(XLayerConfigParse, NonexistentFile) {
  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config("/nonexistent/path.json", &cfg), 0);
}

TEST(XLayerConfigParse, InvalidJson) {
  const char *json = "{ this is not valid json }}}";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, MissingXlayersArray) {
  const char *json = R"({ "output": "test.obu" })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, XlayerIdOutOfRange) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 31, "input": "a.raw" }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, MissingInputField) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0 }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  // Parse succeeds (input is optional when shared source is used)
  EXPECT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  // But validation fails (no input and no shared source)
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

// --- Validation Tests ---

TEST(XLayerConfigValidate, ValidTwoLayers) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");

  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, DuplicateXlayerId) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.xlayers[0].xlayer_id = 3;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[1].xlayer_id = 3;  // duplicate
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, MissingInputFilename) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  // input_filename left empty

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, OpsReferencesInvalidXlayer) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");

  // OPS references xlayer 5, which doesn't exist
  cfg.num_ops_sets = 1;
  cfg.ops_sets[0].enable = 1;
  cfg.ops_sets[0].num_operating_points = 1;
  cfg.ops_sets[0].ops[0].xlayer_map = (1u << 5);  // xlayer 5

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, ZeroXlayers) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 0;

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, NonMonotonicRequiresSameCodingStructure) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 0;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_temporal_layers = 1;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].num_temporal_layers = 1;

  // Same coding structure — should pass
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);

  // Different num_temporal_layers — should fail
  cfg.xlayers[1].num_temporal_layers = 3;
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
  cfg.xlayers[1].num_temporal_layers = 1;  // restore

  // Different lag_in_frames — should fail
  cfg.xlayers[0].lag_in_frames = 19;
  cfg.xlayers[1].lag_in_frames = 35;
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
  cfg.xlayers[1].lag_in_frames = 19;  // restore

  // Different kf_max_dist — should fail
  cfg.xlayers[0].kf_max_dist = 150;
  cfg.xlayers[1].kf_max_dist = 300;
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
  cfg.xlayers[1].kf_max_dist = 150;  // restore

  // Different subgop_config — should fail
  snprintf(cfg.xlayers[0].subgop_config_path, PATH_MAX, "low_delay.json");
  snprintf(cfg.xlayers[1].subgop_config_path, PATH_MAX, "random_access.json");
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, MonotonicAllowsDifferentCodingStructure) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_temporal_layers = 1;
  cfg.xlayers[0].lag_in_frames = 19;
  cfg.xlayers[0].kf_max_dist = 150;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].num_temporal_layers = 3;
  cfg.xlayers[1].lag_in_frames = 35;
  cfg.xlayers[1].kf_max_dist = 300;

  // Different coding structures should be allowed with monotonic=1
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigParse, CodingStructureFields) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "kf_max_dist": 150,
        "subgop_config": "low_delay.json" },
      { "xlayer_id": 1, "input": "b.raw",
        "kf_max_dist": 300 }
    ],
    "monotonic_output_order": true
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].kf_max_dist, 150);
  EXPECT_STREQ(cfg.xlayers[0].subgop_config_path, "low_delay.json");
  EXPECT_EQ(cfg.xlayers[1].kf_max_dist, 300);
  EXPECT_STREQ(cfg.xlayers[1].subgop_config_path, "");
  EXPECT_EQ(cfg.monotonic_output_order, 1);
}

TEST(XLayerConfigParse, NonMonotonicRejectsMismatch) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "num_temporal_layers": 1 },
      { "xlayer_id": 1, "input": "b.raw", "num_temporal_layers": 3 }
    ],
    "monotonic_output_order": false
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  // Parsing succeeds but validation should fail
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

// --- Annex G Config File Parsing Tests ---

// Helper to get the path to a config file in the source tree.
// Relies on AVM_ROOT being the repo root (test runs from build dir).
static std::string CfgPath(const char *relative) {
  // Try the source tree relative to the build directory
  const char *candidates[] = {
    "../avm/cfg/xlayer/",     // build dir is sibling of avm/
    "../../avm/cfg/xlayer/",  // one level deeper
    "../cfg/xlayer/",         // build dir inside avm/
    "cfg/xlayer/",            // running from repo root
  };
  for (const char *prefix : candidates) {
    std::string path = std::string(prefix) + relative;
    FILE *f = fopen(path.c_str(), "r");
    if (f) {
      fclose(f);
      return path;
    }
  }
  // Fall back — will fail with a clear error
  return std::string("cfg/xlayer/") + relative;
}

TEST(XLayerConfigAnnexG, G2_360Degree9Xlayer) {
  std::string path = CfgPath("annexG2_360degree_9xlayer.json");
  MultiXLayerConfig cfg;
  int rc = parse_multi_xlayer_config(path.c_str(), &cfg);
  if (rc != 0) {
    GTEST_SKIP() << "Config file not found: " << path;
  }

  EXPECT_EQ(cfg.num_xlayers, 9);

  // Verify xlayer IDs are 0-8
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(cfg.xlayers[i].xlayer_id, i);
  }

  // All subpictures are 1280x640
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(cfg.xlayers[i].width, 1280u);
    EXPECT_EQ(cfg.xlayers[i].height, 640u);
  }

  // Center viewport (xlayer 4) should have lowest QP (highest quality)
  EXPECT_LT(cfg.xlayers[4].qp, cfg.xlayers[0].qp);

  // 3 embedded layers per xlayer
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(cfg.xlayers[i].num_embedded_layers, 3);
  }

  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.enable_msdo, 0);  // MSDO disabled (>4 streams)

  // OPS: 3 operating points
  EXPECT_EQ(cfg.num_ops_sets, 1);
  EXPECT_EQ(cfg.ops_sets[0].num_operating_points, 3);

  // OP0: center only (xlayer 4)
  EXPECT_EQ(cfg.ops_sets[0].ops[0].xlayer_map, (1u << 4));

  // OP2: all 9 subpictures
  uint32_t all9 = (1u << 9) - 1;  // bits 0-8
  EXPECT_EQ(cfg.ops_sets[0].ops[2].xlayer_map, all9);

  EXPECT_EQ(cfg.enable_atlas, 1);
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigAnnexG, G3_VideoConf3Xlayer) {
  std::string path = CfgPath("annexG3_videoconf_3xlayer.json");
  MultiXLayerConfig cfg;
  int rc = parse_multi_xlayer_config(path.c_str(), &cfg);
  if (rc != 0) {
    GTEST_SKIP() << "Config file not found: " << path;
  }

  EXPECT_EQ(cfg.num_xlayers, 3);
  EXPECT_EQ(cfg.xlayers[0].xlayer_id, 0);
  EXPECT_EQ(cfg.xlayers[1].xlayer_id, 1);
  EXPECT_EQ(cfg.xlayers[2].xlayer_id, 2);

  // Main speaker: 1280x1080
  EXPECT_EQ(cfg.xlayers[0].width, 1280u);
  EXPECT_EQ(cfg.xlayers[0].height, 1080u);

  // Participant 2: 480x360 (encoded small, upsampled by atlas)
  EXPECT_EQ(cfg.xlayers[1].width, 480u);
  EXPECT_EQ(cfg.xlayers[1].height, 360u);

  // Participant 3: 640x540
  EXPECT_EQ(cfg.xlayers[2].width, 640u);
  EXPECT_EQ(cfg.xlayers[2].height, 540u);

  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.lcr_purpose_id, 6);  // Multiview Playback

  // OPS: 3 operating points
  EXPECT_EQ(cfg.num_ops_sets, 1);
  EXPECT_EQ(cfg.ops_sets[0].num_operating_points, 3);

  // OP0: main speaker only
  EXPECT_EQ(cfg.ops_sets[0].ops[0].xlayer_map, (1u << 0));

  // OP2: all 3 participants
  EXPECT_EQ(cfg.ops_sets[0].ops[2].xlayer_map,
            (1u << 0) | (1u << 1) | (1u << 2));

  EXPECT_EQ(cfg.enable_atlas, 1);
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigAnnexG, G4_RoiScalable2Xlayer) {
  std::string path = CfgPath("annexG4_roi_scalable_2xlayer.json");
  MultiXLayerConfig cfg;
  int rc = parse_multi_xlayer_config(path.c_str(), &cfg);
  if (rc != 0) {
    GTEST_SKIP() << "Config file not found: " << path;
  }

  EXPECT_EQ(cfg.num_xlayers, 2);
  EXPECT_EQ(cfg.xlayers[0].xlayer_id, 0);
  EXPECT_EQ(cfg.xlayers[1].xlayer_id, 1);

  // Base layer: full stadium 1920x1080
  EXPECT_EQ(cfg.xlayers[0].width, 1920u);
  EXPECT_EQ(cfg.xlayers[0].height, 1080u);

  // Enhancement: field-of-play 1280x720
  EXPECT_EQ(cfg.xlayers[1].width, 1280u);
  EXPECT_EQ(cfg.xlayers[1].height, 720u);

  // Enhancement should have better quality (lower QP)
  EXPECT_LT(cfg.xlayers[1].qp, cfg.xlayers[0].qp);

  EXPECT_EQ(cfg.enable_global_lcr, 1);
  EXPECT_EQ(cfg.enable_msdo, 0);

  // OPS: 3 operating points
  EXPECT_EQ(cfg.num_ops_sets, 1);
  EXPECT_EQ(cfg.ops_sets[0].num_operating_points, 3);

  // OP0: base only
  EXPECT_EQ(cfg.ops_sets[0].ops[0].xlayer_map, (1u << 0));

  // OP1: enhancement only
  EXPECT_EQ(cfg.ops_sets[0].ops[1].xlayer_map, (1u << 1));

  // OP2: both layers
  EXPECT_EQ(cfg.ops_sets[0].ops[2].xlayer_map, (1u << 0) | (1u << 1));

  EXPECT_EQ(cfg.enable_atlas, 1);
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

// --- GOP Config Tests ---

TEST(XLayerConfigParse, GopModeFields) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "gop_mode": "closed", "fwd_kf_enabled": 1 },
      { "xlayer_id": 1, "input": "b.raw",
        "gop_mode": "open_leading", "enable_keyframe_filtering": 2 },
      { "xlayer_id": 2, "input": "c.raw",
        "gop_mode": "open_sef", "add_sef_for_hidden_frames": 1 }
    ],
    "monotonic_output_order": false
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].gop_mode, 0);
  EXPECT_EQ(cfg.xlayers[0].fwd_kf_enabled, 1);
  EXPECT_EQ(cfg.xlayers[1].gop_mode, 1);
  EXPECT_EQ(cfg.xlayers[1].enable_keyframe_filtering, 2);
  EXPECT_EQ(cfg.xlayers[2].gop_mode, 2);
  EXPECT_EQ(cfg.xlayers[2].add_sef_for_hidden_frames, 1);
}

TEST(XLayerConfigValidate, OpenLeadingRejectedWithMonotonic) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].gop_mode = 1;  // open_leading
  cfg.monotonic_output_order = 1;

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, OpenLeadingAllowedWithNonMonotonic) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 0;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].gop_mode = 1;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].gop_mode = 1;

  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, NonMonotonicRequiresSameGopMode) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 0;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].gop_mode = 0;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].gop_mode = 2;  // mismatch

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigParse, GopModeDefaults) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw" }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // GOP mode defaults: 0 (closed), overrides = -1 (derive)
  EXPECT_EQ(cfg.xlayers[0].gop_mode, 0);
  EXPECT_EQ(cfg.xlayers[0].fwd_kf_enabled, -1);
  EXPECT_EQ(cfg.xlayers[0].enable_keyframe_filtering, -1);
  EXPECT_EQ(cfg.xlayers[0].add_sef_for_hidden_frames, -1);
}

// --- Atlas Config Tests ---

TEST(XLayerConfigParse, AtlasLayoutFields) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 960, "height": 540,
        "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input": "b.raw", "width": 960, "height": 540,
        "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "atlas": {
      "enable": true,
      "mode": 0,
      "width": 1920,
      "height": 540,
      "uniform_spacing": false
    }
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.enable_atlas, 1);
  EXPECT_EQ(cfg.atlas_mode, 0);
  EXPECT_EQ(cfg.atlas_width, 1920);
  EXPECT_EQ(cfg.atlas_height, 540);
  EXPECT_EQ(cfg.atlas_uniform_spacing, 0);
  EXPECT_EQ(cfg.xlayers[0].atlas_pos_x, 0);
  EXPECT_EQ(cfg.xlayers[0].atlas_pos_y, 0);
  EXPECT_EQ(cfg.xlayers[1].atlas_pos_x, 960);
  EXPECT_EQ(cfg.xlayers[1].atlas_pos_y, 0);
}

// --- Scaling Mode / Embedded Layer Tests ---

TEST(XLayerConfigParse, ScalingModeInteger) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "scaling_mode": [4, 6, 0] }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].num_embedded_layers, 3);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], 4);  // AVME_ONEFOUR
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], 6);  // AVME_ONETWO
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], 0);  // AVME_NORMAL
}

TEST(XLayerConfigParse, ScalingModeString) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "scaling_mode": ["1/4", "1/2", "1:1"] }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], AVME_NORMAL);
}

TEST(XLayerConfigParse, ScalingModeAllStringVariants) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "num_embedded_layers": 7,
        "scaling_mode": ["1/8", "1/4", "1/2", "3/5", "3/4", "4/5", "1:1"] }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONEEIGHT);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_ONEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[3], AVME_THREEFIVE);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[4], AVME_THREEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[5], AVME_FOURFIVE);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[6], AVME_NORMAL);
}

TEST(XLayerConfigParse, ScalingModeInvalidString) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "num_embedded_layers": 2,
        "scaling_mode": ["bogus", "1:1"] }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, ScalingModeDefaultDerivation2Layers) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "num_embedded_layers": 2 }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // Default for 2 layers: [1/2, 1:1]
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_NORMAL);
}

TEST(XLayerConfigParse, ScalingModeDefaultDerivation3Layers) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "num_embedded_layers": 3 }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // Default for 3 layers: [1/4, 1/2, 1:1]
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], AVME_NORMAL);
}

TEST(XLayerConfigParse, ScalingModeExplicitOverridesDefault) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw",
        "num_embedded_layers": 2,
        "scaling_mode": ["3/4", "1:1"] }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // Explicit values override defaults
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_THREEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_NORMAL);
}

TEST(XLayerConfigValidate, EmbeddedLayerLastMustBeFullRes) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_embedded_layers = 2;
  cfg.xlayers[0].scaling_mode[0] = AVME_ONETWO;
  cfg.xlayers[0].scaling_mode[1] = AVME_ONETWO;  // Not full-res — invalid

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);

  // Fix it
  cfg.xlayers[0].scaling_mode[1] = AVME_NORMAL;
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, EmbeddedLayerOutOfRange) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_embedded_layers = 0;  // Invalid

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, EmbeddedLayerInvalidScalingMode) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_embedded_layers = 2;
  cfg.xlayers[0].scaling_mode[0] = 99;  // Invalid value
  cfg.xlayers[0].scaling_mode[1] = AVME_NORMAL;

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, NonMonotonicAllowsDifferentEmbeddedLayers) {
  // Different num_embedded_layers is valid — the constraint is that output
  // frames within a TU must have matching order hints and synchronized RAPs,
  // NOT that embedded layer counts match across xlayers.
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 0;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_embedded_layers = 3;
  cfg.xlayers[0].scaling_mode[0] = AVME_ONEFOUR;
  cfg.xlayers[0].scaling_mode[1] = AVME_ONETWO;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].num_embedded_layers = 1;

  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, MonotonicAllowsDifferentEmbeddedLayers) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 2;
  cfg.monotonic_output_order = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].num_embedded_layers = 3;
  cfg.xlayers[0].scaling_mode[0] = AVME_ONEFOUR;
  cfg.xlayers[0].scaling_mode[1] = AVME_ONETWO;
  cfg.xlayers[1].xlayer_id = 1;
  snprintf(cfg.xlayers[1].input_filename, PATH_MAX, "b.raw");
  cfg.xlayers[1].num_embedded_layers = 1;

  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

// --- Multi-Source Input Tests ---

TEST(XLayerConfigParse, MultiSourceParsing) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 },
      { "name": "alpha", "filename": "alpha.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "texture", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "alpha", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.num_input_sources, 2);
  EXPECT_STREQ(cfg.input_sources[0].name, "texture");
  EXPECT_STREQ(cfg.input_sources[0].filename, "video.raw");
  EXPECT_EQ(cfg.input_sources[0].width, 1920u);
  EXPECT_EQ(cfg.input_sources[0].height, 1080u);
  EXPECT_STREQ(cfg.input_sources[1].name, "alpha");
  EXPECT_STREQ(cfg.input_sources[1].filename, "alpha.raw");
  EXPECT_STREQ(cfg.xlayers[0].input_source_name, "texture");
  EXPECT_STREQ(cfg.xlayers[1].input_source_name, "alpha");
}

TEST(XLayerConfigParse, MultiSourceResolution) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 },
      { "name": "alpha", "filename": "alpha.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "texture", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "texture", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 },
      { "xlayer_id": 2, "input_source": "alpha", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[1].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[2].input_source_idx, 1);
}

TEST(XLayerConfigParse, MultiSourceBackwardCompat) {
  const char *json = R"({
    "source": {
      "filename": "video.raw",
      "width": 1920,
      "height": 1080
    },
    "xlayers": [
      { "xlayer_id": 0, "width": 960, "height": 540,
        "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "width": 960, "height": 540,
        "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // Legacy source is converted to input_sources[0] named "default"
  EXPECT_EQ(cfg.num_input_sources, 1);
  EXPECT_STREQ(cfg.input_sources[0].name, "default");
  EXPECT_STREQ(cfg.input_sources[0].filename, "video.raw");
  EXPECT_EQ(cfg.input_sources[0].width, 1920u);
  EXPECT_EQ(cfg.input_sources[0].height, 1080u);
  // Legacy fields still populated
  EXPECT_STREQ(cfg.source_filename, "video.raw");

  // Resolve should assign all xlayers to source 0
  ASSERT_EQ(resolve_input_sources(&cfg), 0);
  EXPECT_EQ(cfg.xlayers[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[1].input_source_idx, 0);
}

TEST(XLayerConfigParse, MultiSourceUnknownName) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "nonexistent", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  // Resolve should fail — unknown source name
  EXPECT_NE(resolve_input_sources(&cfg), 0);
}

TEST(XLayerConfigValidate, MultiSourceDuplicateName) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_input_sources = 2;
  snprintf(cfg.input_sources[0].name, MAX_SOURCE_NAME_LEN, "texture");
  snprintf(cfg.input_sources[0].filename, PATH_MAX, "a.raw");
  cfg.input_sources[0].width = 1920;
  cfg.input_sources[0].height = 1080;
  snprintf(cfg.input_sources[1].name, MAX_SOURCE_NAME_LEN, "texture");
  snprintf(cfg.input_sources[1].filename, PATH_MAX, "b.raw");
  cfg.input_sources[1].width = 1920;
  cfg.input_sources[1].height = 1080;

  cfg.num_xlayers = 2;
  cfg.xlayers[0].xlayer_id = 0;
  cfg.xlayers[0].input_source_idx = 0;
  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 540;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;
  cfg.xlayers[1].xlayer_id = 1;
  cfg.xlayers[1].input_source_idx = 1;
  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 540;
  cfg.xlayers[1].atlas_pos_x = 960;
  cfg.xlayers[1].atlas_pos_y = 0;

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, MultiSourceChromaValidation) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_input_sources = 1;
  snprintf(cfg.input_sources[0].name, MAX_SOURCE_NAME_LEN, "default");
  snprintf(cfg.input_sources[0].filename, PATH_MAX, "v.raw");
  cfg.input_sources[0].width = 1920;
  cfg.input_sources[0].height = 1080;

  cfg.num_xlayers = 2;
  cfg.xlayers[0].xlayer_id = 0;
  cfg.xlayers[0].input_source_idx = 0;
  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 540;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;
  cfg.xlayers[0].profile = MAIN_420_10_IP1;
  cfg.xlayers[1].xlayer_id = 1;
  cfg.xlayers[1].input_source_idx = 0;
  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 540;
  cfg.xlayers[1].atlas_pos_x = 960;
  cfg.xlayers[1].atlas_pos_y = 0;
  cfg.xlayers[1].profile = MAIN_444_10_IP1;  // Mismatch

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);

  // Fix chroma mismatch
  cfg.xlayers[1].profile = MAIN_420_10_IP1;
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigParse, MultiSourceMixedMode) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "texture", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input": "depth.raw", "width": 1920,
        "height": 1080 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  // xlayer 0 uses shared source, xlayer 1 uses own file
  EXPECT_EQ(cfg.xlayers[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[1].input_source_idx, -1);
  EXPECT_STREQ(cfg.xlayers[1].input_filename, "depth.raw");
}

TEST(XLayerConfigParse, MultiSourceSingleDefault) {
  const char *json = R"({
    "inputs": [
      { "name": "main", "filename": "video.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "width": 960, "height": 540,
        "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "width": 960, "height": 540,
        "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  // Single input source — all unassigned xlayers auto-assign to it
  EXPECT_EQ(cfg.xlayers[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[1].input_source_idx, 0);
}

TEST(XLayerConfigParse, InputsAndSourceMutuallyExclusive) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 }
    ],
    "source": {
      "filename": "video.raw",
      "width": 1920,
      "height": 1080
    },
    "xlayers": [
      { "xlayer_id": 0, "width": 960, "height": 540,
        "atlas_pos_x": 0, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, MultiSourceAmbiguousNoInputSource) {
  const char *json = R"({
    "inputs": [
      { "name": "texture", "filename": "video.raw", "width": 1920,
        "height": 1080 },
      { "name": "alpha", "filename": "alpha.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "width": 960, "height": 540,
        "atlas_pos_x": 0, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  // Multiple inputs, no explicit input_source — ambiguous, should fail
  EXPECT_NE(resolve_input_sources(&cfg), 0);
}

// --- Frame Rate Tests ---

TEST(XLayerConfigParse, FrameRateIntegerParsing) {
  const char *json = R"({
    "inputs": [
      { "name": "fast", "filename": "a.raw", "width": 1920, "height": 1080,
        "frame_rate": 60 },
      { "name": "slow", "filename": "b.raw", "width": 1920, "height": 1080,
        "frame_rate": 15 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "fast", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "slow", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.input_sources[0].frame_rate_num, 60);
  EXPECT_EQ(cfg.input_sources[0].frame_rate_den, 1);
  EXPECT_EQ(cfg.input_sources[1].frame_rate_num, 15);
  EXPECT_EQ(cfg.input_sources[1].frame_rate_den, 1);

  ASSERT_EQ(resolve_input_sources(&cfg), 0);
  EXPECT_EQ(cfg.input_sources[0].frame_skip, 1);  // 60/60 = 1
  EXPECT_EQ(cfg.input_sources[1].frame_skip, 4);  // 60/15 = 4
}

TEST(XLayerConfigParse, FrameRateRationalString) {
  const char *json = R"({
    "inputs": [
      { "name": "ntsc", "filename": "a.raw", "width": 1920, "height": 1080,
        "frame_rate": "30000/1001" },
      { "name": "half", "filename": "b.raw", "width": 1920, "height": 1080,
        "frame_rate": "15000/1001" }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "ntsc", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "half", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.input_sources[0].frame_rate_num, 30000);
  EXPECT_EQ(cfg.input_sources[0].frame_rate_den, 1001);
  EXPECT_EQ(cfg.input_sources[1].frame_rate_num, 15000);
  EXPECT_EQ(cfg.input_sources[1].frame_rate_den, 1001);

  ASSERT_EQ(resolve_input_sources(&cfg), 0);
  // 30000/1001 / (15000/1001) = 30000*1001 / (1001*15000) = 2
  EXPECT_EQ(cfg.input_sources[0].frame_skip, 1);
  EXPECT_EQ(cfg.input_sources[1].frame_skip, 2);
}

TEST(XLayerConfigParse, FrameRateNonDivisorFails) {
  const char *json = R"({
    "inputs": [
      { "name": "a", "filename": "a.raw", "width": 1920, "height": 1080,
        "frame_rate": 30 },
      { "name": "b", "filename": "b.raw", "width": 1920, "height": 1080,
        "frame_rate": 24 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "a", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "b", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  // 30/24 = 1.25, not an integer — should fail
  EXPECT_NE(resolve_input_sources(&cfg), 0);
}

TEST(XLayerConfigParse, FrameRateUnspecifiedAssumesMax) {
  const char *json = R"({
    "inputs": [
      { "name": "fast", "filename": "a.raw", "width": 1920, "height": 1080,
        "frame_rate": 60 },
      { "name": "auto", "filename": "b.raw", "width": 1920,
        "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "fast", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "auto", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  // Unspecified source assumes master rate (skip=1)
  EXPECT_EQ(cfg.input_sources[0].frame_skip, 1);
  EXPECT_EQ(cfg.input_sources[1].frame_skip, 1);
}

TEST(XLayerConfigParse, FrameRateAllSameNoSkip) {
  const char *json = R"({
    "inputs": [
      { "name": "a", "filename": "a.raw", "width": 1920, "height": 1080,
        "frame_rate": 30 },
      { "name": "b", "filename": "b.raw", "width": 1920, "height": 1080,
        "frame_rate": 30 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "a", "width": 960,
        "height": 540, "atlas_pos_x": 0, "atlas_pos_y": 0 },
      { "xlayer_id": 1, "input_source": "b", "width": 960,
        "height": 540, "atlas_pos_x": 960, "atlas_pos_y": 0 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  EXPECT_EQ(cfg.input_sources[0].frame_skip, 1);
  EXPECT_EQ(cfg.input_sources[1].frame_skip, 1);
}

// --- Embedded Layers (Per-MLlayer Source) Tests ---

TEST(XLayerConfigParse, EmbeddedLayersParsing) {
  const char *json = R"({
    "inputs": [
      { "name": "left", "filename": "left.raw", "width": 1920, "height": 1080 },
      { "name": "right", "filename": "right.raw", "width": 1920, "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "left", "width": 1920, "height": 1080,
        "atlas_pos_x": 0, "atlas_pos_y": 0,
        "num_embedded_layers": 2,
        "embedded_layers": [
          { "scaling_mode": "1/2", "input_source": "left",
            "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
            "depends_on": [] },
          { "scaling_mode": "1:1", "input_source": "right",
            "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
            "depends_on": [0] }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "stereo_ml.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);
  ASSERT_EQ(validate_multi_xlayer_config(&cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].num_embedded_layers, 2);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_NORMAL);
  EXPECT_EQ(cfg.xlayers[0].has_per_mlayer_sources, 1);
  EXPECT_EQ(cfg.xlayers[0].has_mlayer_dependencies, 1);

  // mlayer 0: source "left"
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].dependency_mask, 0);

  // mlayer 1: source "right", depends on mlayer 0
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].input_source_idx, 1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].dependency_mask, 1);
}

TEST(XLayerConfigParse, EmbeddedLayersScalingModeOnly) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "test.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "embedded_layers": [
          { "scaling_mode": "1/4" },
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1" }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], AVME_NORMAL);
  EXPECT_EQ(cfg.xlayers[0].has_per_mlayer_sources, 0);
  EXPECT_EQ(cfg.xlayers[0].has_mlayer_dependencies, 0);
}

TEST(XLayerConfigParse, EmbeddedLayersAndScalingModeMutualExclusion) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "test.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 2,
        "scaling_mode": ["1/2", "1:1"],
        "embedded_layers": [
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1" }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, EmbeddedLayersCountMismatch) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "test.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "embedded_layers": [
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1" }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(path, &cfg), 0);
}

TEST(XLayerConfigParse, EmbeddedLayersDependsOnParsing) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "test.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "embedded_layers": [
          { "scaling_mode": "1/4", "depends_on": [] },
          { "scaling_mode": "1/2", "depends_on": [0] },
          { "scaling_mode": "1:1" }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // mlayer 0: depends_on: [] -> mask=0
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].dependency_mask, 0);
  // mlayer 1: depends_on: [0] -> mask=1
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].dependency_mask, 1);
  // mlayer 2: no depends_on -> mask=-1 (default)
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[2].dependency_mask, -1);
  EXPECT_EQ(cfg.xlayers[0].has_mlayer_dependencies, 1);
}

TEST(XLayerConfigResolve, EmbeddedLayersSourceResolution) {
  const char *json = R"({
    "inputs": [
      { "name": "main", "filename": "main.raw", "width": 1920, "height": 1080 },
      { "name": "aux", "filename": "aux.raw", "width": 1920, "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "main", "width": 1920, "height": 1080,
        "atlas_pos_x": 0, "atlas_pos_y": 0,
        "num_embedded_layers": 2,
        "embedded_layers": [
          { "scaling_mode": "1/2", "input_source": "main",
            "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080 },
          { "scaling_mode": "1:1", "input_source": "aux",
            "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080 }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].input_source_idx, 0);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].input_source_idx, 1);
}

TEST(XLayerConfigResolve, EmbeddedLayersInheritance) {
  const char *json = R"({
    "inputs": [
      { "name": "main", "filename": "main.raw", "width": 3840, "height": 2160 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "main", "width": 1920, "height": 1080,
        "atlas_pos_x": 100, "atlas_pos_y": 200,
        "num_embedded_layers": 2,
        "embedded_layers": [
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1" }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);

  // Both mlayers inherit from xlayer — since no mlayer has explicit source,
  // has_per_mlayer_sources is 0, so mlayer_sources stay at defaults.
  // The encoder uses the xlayer's source for all mlayers automatically.
  EXPECT_EQ(cfg.xlayers[0].has_per_mlayer_sources, 0);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].input_source_idx, -1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].input_source_idx, -1);
}

TEST(XLayerConfigResolve, EmbeddedLayersUnknownSource) {
  const char *json = R"({
    "inputs": [
      { "name": "main", "filename": "main.raw", "width": 1920, "height": 1080 }
    ],
    "xlayers": [
      { "xlayer_id": 0, "input_source": "main", "width": 1920, "height": 1080,
        "atlas_pos_x": 0, "atlas_pos_y": 0,
        "num_embedded_layers": 2,
        "embedded_layers": [
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1", "input_source": "nonexistent",
            "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080 }
        ],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  EXPECT_NE(resolve_input_sources(&cfg), 0);
}

TEST(XLayerConfigValidate, EmbeddedLayersRequireDimensions) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_input_sources = 2;
  snprintf(cfg.input_sources[0].name, MAX_SOURCE_NAME_LEN, "left");
  snprintf(cfg.input_sources[0].filename, PATH_MAX, "left.raw");
  cfg.input_sources[0].width = 1920;
  cfg.input_sources[0].height = 1080;
  snprintf(cfg.input_sources[1].name, MAX_SOURCE_NAME_LEN, "right");
  snprintf(cfg.input_sources[1].filename, PATH_MAX, "right.raw");
  cfg.input_sources[1].width = 1920;
  cfg.input_sources[1].height = 1080;

  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  cfg.xlayers[0].input_source_idx = 0;
  cfg.xlayers[0].width = 1920;
  cfg.xlayers[0].height = 1080;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;
  cfg.xlayers[0].num_embedded_layers = 2;
  cfg.xlayers[0].scaling_mode[0] = AVME_ONETWO;
  cfg.xlayers[0].scaling_mode[1] = AVME_NORMAL;
  cfg.xlayers[0].has_per_mlayer_sources = 1;

  // mlayer 1 has source but no width/height
  cfg.xlayers[0].mlayer_sources[0].input_source_idx = 0;
  cfg.xlayers[0].mlayer_sources[0].atlas_pos_x = 0;
  cfg.xlayers[0].mlayer_sources[0].atlas_pos_y = 0;
  cfg.xlayers[0].mlayer_sources[0].width = 1920;
  cfg.xlayers[0].mlayer_sources[0].height = 1080;

  cfg.xlayers[0].mlayer_sources[1].input_source_idx = 1;
  cfg.xlayers[0].mlayer_sources[1].atlas_pos_x = 0;
  cfg.xlayers[0].mlayer_sources[1].atlas_pos_y = 0;
  cfg.xlayers[0].mlayer_sources[1].width = 0;   // Missing!
  cfg.xlayers[0].mlayer_sources[1].height = 0;  // Missing!

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);

  // Fix: add dimensions
  cfg.xlayers[0].mlayer_sources[1].width = 1920;
  cfg.xlayers[0].mlayer_sources[1].height = 1080;
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigValidate, EmbeddedLayersDependsOnRange) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "test.raw");
  cfg.xlayers[0].width = 1920;
  cfg.xlayers[0].height = 1080;
  cfg.xlayers[0].num_embedded_layers = 2;
  cfg.xlayers[0].scaling_mode[0] = AVME_ONETWO;
  cfg.xlayers[0].scaling_mode[1] = AVME_NORMAL;
  cfg.xlayers[0].has_mlayer_dependencies = 1;

  // mlayer 0 trying to depend on mlayer 1 (invalid: >= self)
  cfg.xlayers[0].mlayer_sources[0].dependency_mask = 0x02;  // bit 1 set

  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);

  // Fix: mlayer 0 depends on nothing
  cfg.xlayers[0].mlayer_sources[0].dependency_mask = 0;
  cfg.xlayers[0].mlayer_sources[1].dependency_mask = 1;  // depends on 0
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigParse, EmbeddedLayersBackwardCompat) {
  // Existing flat scaling_mode array should still work
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "test.raw", "width": 1920, "height": 1080,
        "num_embedded_layers": 3,
        "scaling_mode": ["1/4", "1/2", "1:1"],
        "qp": 128, "cpu_used": 5 }
    ],
    "output": "out.obu"
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  EXPECT_EQ(cfg.xlayers[0].scaling_mode[0], AVME_ONEFOUR);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[1], AVME_ONETWO);
  EXPECT_EQ(cfg.xlayers[0].scaling_mode[2], AVME_NORMAL);
  EXPECT_EQ(cfg.xlayers[0].has_per_mlayer_sources, 0);
  EXPECT_EQ(cfg.xlayers[0].has_mlayer_dependencies, 0);
  // No embedded_layers array means mlayer_sources stay at defaults
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].input_source_idx, -1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].dependency_mask, -1);
}

// --- Codec Controls Tests ---

TEST(XLayerConfig, CodecControlsParsing) {
  const char *json = R"({
    "inputs": [{ "name": "src", "filename": "test.raw", "width": 64, "height": 64 }],
    "xlayers": [{
      "xlayer_id": 0, "input_source": "src",
      "width": 64, "height": 64,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "codec_controls": [
        ["enable_deblocking", 0],
        ["enable_cdef", 0],
        ["enable_intrabc", 0]
      ]
    }],
    "ops": [{ "ops_id": 0, "priority": 0, "intent_present": true,
              "ptl_present": true,
              "operating_points": [{ "intent": 0, "xlayer_map": [0] }] }],
    "output": "/tmp/test_cc.obu"
  })";
  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(WriteTempJson(json), &cfg), 0);
  EXPECT_EQ(cfg.xlayers[0].num_codec_controls, 3);
  EXPECT_STREQ(cfg.xlayers[0].codec_controls[0].name, "enable_deblocking");
  EXPECT_EQ(cfg.xlayers[0].codec_controls[0].value, 0);
  EXPECT_STREQ(cfg.xlayers[0].codec_controls[1].name, "enable_cdef");
  EXPECT_EQ(cfg.xlayers[0].codec_controls[1].value, 0);
  EXPECT_STREQ(cfg.xlayers[0].codec_controls[2].name, "enable_intrabc");
  EXPECT_EQ(cfg.xlayers[0].codec_controls[2].value, 0);
}

TEST(XLayerConfig, CodecControlsInvalidFormat) {
  // codec_controls entry is not a [name, value] pair
  const char *json = R"({
    "inputs": [{ "name": "src", "filename": "test.raw", "width": 64, "height": 64 }],
    "xlayers": [{
      "xlayer_id": 0, "input_source": "src",
      "width": 64, "height": 64,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "codec_controls": [
        ["enable_deblocking"]
      ]
    }],
    "ops": [{ "ops_id": 0, "priority": 0, "intent_present": true,
              "ptl_present": true,
              "operating_points": [{ "intent": 0, "xlayer_map": [0] }] }],
    "output": "/tmp/test_cc.obu"
  })";
  MultiXLayerConfig cfg;
  EXPECT_NE(parse_multi_xlayer_config(WriteTempJson(json), &cfg), 0);
}

TEST(XLayerConfig, CodecControlsEmpty) {
  // Empty codec_controls array is valid
  const char *json = R"({
    "inputs": [{ "name": "src", "filename": "test.raw", "width": 64, "height": 64 }],
    "xlayers": [{
      "xlayer_id": 0, "input_source": "src",
      "width": 64, "height": 64,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "codec_controls": []
    }],
    "ops": [{ "ops_id": 0, "priority": 0, "intent_present": true,
              "ptl_present": true,
              "operating_points": [{ "intent": 0, "xlayer_map": [0] }] }],
    "output": "/tmp/test_cc.obu"
  })";
  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(WriteTempJson(json), &cfg), 0);
  EXPECT_EQ(cfg.xlayers[0].num_codec_controls, 0);
}

// --- Per-MLayer Content Interpretation Tests ---

TEST(XLayerConfigParse, EmbeddedLayersCIParsing) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "color_primaries": 1,
        "transfer_characteristics": 1,
        "matrix_coefficients": 1,
        "full_range_flag": 0,
        "num_embedded_layers": 2,
        "embedded_layers": [
          { "scaling_mode": "1/2",
            "color_primaries": 9,
            "transfer_characteristics": 16,
            "matrix_coefficients": 9,
            "full_range_flag": 1 },
          { "scaling_mode": "1:1" }
        ]
      }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // mlayer 0: explicit CI values
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].color_primaries, 9);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].transfer_characteristics, 16);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].matrix_coefficients, 9);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].full_range_flag, 1);

  // mlayer 1: no CI fields → -1 (inherit from xlayer)
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].color_primaries, -1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].transfer_characteristics, -1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].matrix_coefficients, -1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].full_range_flag, -1);
}

TEST(XLayerConfigResolve, EmbeddedLayersCIInheritance) {
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "color_primaries": 1,
        "transfer_characteristics": 13,
        "matrix_coefficients": 6,
        "full_range_flag": 0,
        "num_embedded_layers": 3,
        "embedded_layers": [
          { "scaling_mode": "1/4",
            "color_primaries": 9 },
          { "scaling_mode": "1/2" },
          { "scaling_mode": "1:1",
            "full_range_flag": 1 }
        ]
      }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  ASSERT_EQ(resolve_input_sources(&cfg), 0);
  resolve_mlayer_ci(&cfg);

  // mlayer 0: explicit color_primaries=9, rest inherit from xlayer
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].color_primaries, 9);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].transfer_characteristics, 13);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].matrix_coefficients, 6);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[0].full_range_flag, 0);

  // mlayer 1: all inherit from xlayer
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].color_primaries, 1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].transfer_characteristics, 13);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].matrix_coefficients, 6);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[1].full_range_flag, 0);

  // mlayer 2: explicit full_range_flag=1, rest inherit from xlayer
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[2].color_primaries, 1);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[2].transfer_characteristics, 13);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[2].matrix_coefficients, 6);
  EXPECT_EQ(cfg.xlayers[0].mlayer_sources[2].full_range_flag, 1);
}

TEST(XLayerConfigParse, XLayerColorPropagation) {
  // Verify xlayer-level color fields are parsed correctly
  const char *json = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "width": 1920, "height": 1080,
        "color_primaries": 9,
        "transfer_characteristics": 16,
        "matrix_coefficients": 9,
        "full_range_flag": 1 },
      { "xlayer_id": 1, "input": "b.raw", "width": 1920, "height": 1080 }
    ]
  })";
  const char *path = WriteTempJson(json);
  ASSERT_NE(path, nullptr);

  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);

  // xlayer 0: explicit color info
  EXPECT_EQ(cfg.xlayers[0].color_primaries, 9);
  EXPECT_EQ(cfg.xlayers[0].transfer_characteristics, 16);
  EXPECT_EQ(cfg.xlayers[0].matrix_coefficients, 9);
  EXPECT_EQ(cfg.xlayers[0].full_range_flag, 1);

  // xlayer 1: no color info → -1 (use codec defaults)
  EXPECT_EQ(cfg.xlayers[1].color_primaries, -1);
  EXPECT_EQ(cfg.xlayers[1].transfer_characteristics, -1);
  EXPECT_EQ(cfg.xlayers[1].matrix_coefficients, -1);
  EXPECT_EQ(cfg.xlayers[1].full_range_flag, -1);
}

TEST(XLayerConfigValidate, EmbeddedLayersCIRangeValidation) {
  MultiXLayerConfig cfg;
  xlayer_config_init(&cfg);
  cfg.num_xlayers = 1;
  cfg.xlayers[0].xlayer_id = 0;
  snprintf(cfg.xlayers[0].input_filename, PATH_MAX, "a.raw");
  cfg.xlayers[0].width = 416;
  cfg.xlayers[0].height = 240;
  cfg.xlayers[0].num_embedded_layers = 1;

  // Valid: no CI specified (all -1)
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);

  // Invalid: color_primaries = 300
  cfg.xlayers[0].mlayer_sources[0].color_primaries = 300;
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
  cfg.xlayers[0].mlayer_sources[0].color_primaries = -1;

  // Invalid: full_range_flag = 2
  cfg.xlayers[0].mlayer_sources[0].full_range_flag = 2;
  EXPECT_NE(validate_multi_xlayer_config(&cfg), 0);
  cfg.xlayers[0].mlayer_sources[0].full_range_flag = -1;

  // Valid again
  EXPECT_EQ(validate_multi_xlayer_config(&cfg), 0);
}

TEST(XLayerConfigParse, LimitField) {
  // Default: limit=0 (unlimited)
  const char *json_no_limit = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "qp": 100 },
      { "xlayer_id": 1, "input": "b.raw", "qp": 160 }
    ]
  })";
  const char *path = WriteTempJson(json_no_limit);
  ASSERT_NE(path, nullptr);
  MultiXLayerConfig cfg;
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  EXPECT_EQ(cfg.limit, 0);

  // Explicit limit
  const char *json_limit = R"({
    "xlayers": [
      { "xlayer_id": 0, "input": "a.raw", "qp": 100 },
      { "xlayer_id": 1, "input": "b.raw", "qp": 160 }
    ],
    "limit": 5
  })";
  path = WriteTempJson(json_limit);
  ASSERT_NE(path, nullptr);
  ASSERT_EQ(parse_multi_xlayer_config(path, &cfg), 0);
  EXPECT_EQ(cfg.limit, 5);
}

}  // namespace
