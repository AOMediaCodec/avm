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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/y4m_video_source.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "aom/aom_codec.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/subgop.h"

// Silence compiler warning for unused static functions
static void yuvconfig2image(aom_image_t *img, const YV12_BUFFER_CONFIG *yv12,
                            void *user_priv) AOM_UNUSED;
static aom_codec_err_t image2yuvconfig(const aom_image_t *img,
                                       YV12_BUFFER_CONFIG *yv12) AOM_UNUSED;
static void image2yuvconfig_upshift(aom_image_t *hbd_img,
                                    const aom_image_t *img,
                                    YV12_BUFFER_CONFIG *yv12) AOM_UNUSED;
#include "av1/av1_iface_common.h"

#define MAX_SUBGOP_CODES 3

static const char *subgop_config_str_nondef[] = {
  // enh, subgop size = 4
  "4:0:4U1/2U2/1V3/2S/3V3/4S,"
  "4:1:3U1/2U2/1V3/2S/3S/4V3,"
  // enh, subgop size = 5
  "5:0:5U1/3U2/1V3/2V3/3S/4V3/5S,"
  "5:1:4U1/2U2/1V3/2S/3V3/4S/5V3,"
  // enh, subgop size = 7
  "7:0:7U1/3U2/1V4/2V4/3S/5U3/4V4/5S/6V4/7S,"
  "7:1:6U1/3U2/2U3/1V4/2S/3S/5U3/4V4/5S/6S/7V4,"
  // enh, subgop size = 9
  "9:0:9F1/4U2/2U3/1V4/2S/3V4/4S/7U3/5V4/6V5/7S/8V5/9R1,"
  "9:1:7F1/3U2/1V4/2V4/3S/5U3/4V4/5S/6V4/7R1/9U3/8V4/9S,",
};

namespace {
// Default config
extern "C" const char subgop_config_str_def[];
// An enhanced config where the last subgop uses a shorter dist to arf
extern "C" const char subgop_config_str_enh[];
// A config that honors temporally scalable prediction structure, i.e.
// no frame is coded with references at higher pyramid depths.
extern "C" const char subgop_config_str_ts[];
// An asymmetrical config where the hierarchical frames are not exactly
// dyadic, but slightly skewed.
extern "C" const char subgop_config_str_asym[];
// low delay config without references
extern "C" const char subgop_config_str_ld[];

const int kCpuUsed = 5;
const unsigned int kFrames = 70;

typedef enum {
  DEFAULT,
  ENHANCE,
  ASYMMETRIC,
  TEMPORAL_SCALABLE,
  LOW_DELAY,
} subgop_config_tag;

typedef struct {
  const char *preset_tag;
  const char *preset_str;
} subgop_config_str_preset_map_type;

const subgop_config_str_preset_map_type subgop_config_str_preset_map[] = {
  { "def", subgop_config_str_def },   { "enh", subgop_config_str_enh },
  { "asym", subgop_config_str_asym }, { "ts", subgop_config_str_ts },
  { "ld", subgop_config_str_ld },
};

typedef struct {
  const char *subgop_str;
  const char *input_file;
  int min_gf_interval;
  int max_gf_interval;
  int frame_w;
  int frame_h;
  int lag_in_frames;
  int use_fixed_qp_offsets;
} SubgopTestParams;

int is_extension_y4m(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return 0;
  else
    return !strcmp(dot, ".y4m");
}

static const SubgopTestParams SubGopTestVectors[] = {
  // Default subgop config
  { subgop_config_str_preset_map[DEFAULT].preset_tag,
    "hantro_collage_w352h288.yuv", 0, 16, 352, 288, 35, 0 },
  { subgop_config_str_preset_map[DEFAULT].preset_tag, "desktop1.320_180.yuv", 0,
    16, 320, 180, 35, 0 },
  { subgop_config_str_preset_map[DEFAULT].preset_tag,
    "pixel_capture_w320h240.yuv", 16, 16, 320, 240, 35, 1 },
  { subgop_config_str_preset_map[DEFAULT].preset_tag,
    "hantro_collage_w352h288.yuv", 0, 32, 352, 288, 35, 0 },
  { subgop_config_str_preset_map[DEFAULT].preset_tag,
    "pixel_capture_w320h240.yuv", 32, 32, 320, 240, 35, 1 },

  // Enhanced subgop config
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "niklas_640_480_30.yuv",
    0, 15, 640, 480, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "paris_352_288_30.y4m", 0,
    6, 352, 288, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "hantro_collage_w352h288.yuv", 0, 16, 352, 288, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "pixel_capture_w320h240.yuv", 0, 12, 320, 240, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "niklas_1280_720_30.y4m",
    0, 11, 1280, 720, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "screendata.y4m", 0, 16,
    640, 480, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "pixel_capture_w320h240.yuv", 0, 14, 320, 240, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "desktop1.320_180.yuv", 0,
    10, 320, 180, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "paris_352_288_30.y4m", 0,
    13, 352, 288, 35, 0 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "pixel_capture_w320h240.yuv", 0, 8, 320, 240, 35, 0 },

  // Asymmetric subgop config
  { subgop_config_str_preset_map[ASYMMETRIC].preset_tag,
    "pixel_capture_w320h240.yuv", 0, 16, 320, 240, 35, 0 },
  { subgop_config_str_preset_map[ASYMMETRIC].preset_tag, "desktop1.320_180.yuv",
    0, 16, 320, 180, 35, 0 },

  // Temporal scalable subgop config
  { subgop_config_str_preset_map[TEMPORAL_SCALABLE].preset_tag,
    "pixel_capture_w320h240.yuv", 0, 16, 320, 240, 35, 0 },
  { subgop_config_str_preset_map[TEMPORAL_SCALABLE].preset_tag,
    "hantro_collage_w352h288.yuv", 0, 16, 352, 288, 35, 0 },

  // Low delay subgop config
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "paris_352_288_30.y4m",
    0, 16, 352, 288, 0, 0 },
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "desktop1.320_180.yuv",
    16, 16, 320, 180, 0, 1 },
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "paris_352_288_30.y4m",
    0, 32, 352, 288, 0, 0 },
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "desktop1.320_180.yuv",
    32, 32, 320, 180, 0, 1 },

  // Non-default subgop config
  { subgop_config_str_nondef[0], "pixel_capture_w320h240.yuv", 0, 4, 320, 240,
    35, 0 },
  { subgop_config_str_nondef[0], "desktop1.320_180.yuv", 0, 5, 320, 180, 35,
    0 },
  { subgop_config_str_nondef[0], "pixel_capture_w320h240.yuv", 0, 7, 320, 240,
    35, 0 },
  { subgop_config_str_nondef[0], "hantro_collage_w352h288.yuv", 0, 9, 352, 288,
    35, 0 },
};

std::ostream &operator<<(std::ostream &os, const SubgopTestParams &test_arg) {
  return os << "SubgopTestParams { sub_gop_config:" << test_arg.subgop_str
            << " source_file:" << test_arg.input_file
            << " min_gf_interval:" << test_arg.min_gf_interval
            << " max_gf_interval:" << test_arg.max_gf_interval
            << " frame_width:" << test_arg.frame_w
            << " frame_height:" << test_arg.frame_h << " cpu_used:" << kCpuUsed
            << " lag_in_frames:" << test_arg.lag_in_frames
            << " use_fixed_qp_offsets:" << test_arg.use_fixed_qp_offsets
            << " }";
}
// This class is used to validate the subgop config in a gop.
class SubGopTestLarge
    : public ::libaom_test::CodecTestWith2Params<SubgopTestParams, aom_rc_mode>,
      public ::libaom_test::EncoderTest {
 protected:
  SubGopTestLarge()
      : EncoderTest(GET_PARAM(0)), subgop_test_params_(GET_PARAM(1)),
        rc_end_usage_(GET_PARAM(2)) {
    InitSubgop();
  }
  virtual ~SubGopTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libaom_test::kOnePassGood);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.g_threads = 1;
    cfg_.rc_end_usage = rc_end_usage_;
    if (rc_end_usage_ == AOM_Q) {
      cfg_.use_fixed_qp_offsets = subgop_test_params_.use_fixed_qp_offsets;
    }
    // Note: kf_min_dist, kf_max_dist, g_lag_in_frames are configurable
    // parameters
    cfg_.kf_min_dist = 65;
    cfg_.kf_max_dist = 65;
    cfg_.g_lag_in_frames = subgop_test_params_.lag_in_frames;
    // Note: Uncomment the following line for verbose output, to aid debugging.
    // init_flags_ = AOM_CODEC_USE_PER_FRAME_STATS;
  }

  // check if subgop_config_str is a preset tag
  void GetSubGOPConfigStr() {
    int num_preset_configs = sizeof(subgop_config_str_preset_map) /
                             sizeof(*subgop_config_str_preset_map);
    for (int p = 0; p < num_preset_configs; ++p) {
      if (!strcmp(subgop_test_params_.subgop_str,
                  subgop_config_str_preset_map[p].preset_tag)) {
        subgop_test_params_.subgop_str =
            subgop_config_str_preset_map[p].preset_str;
        break;
      }
    }
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, kCpuUsed);
      if (rc_end_usage_ == AOM_Q) {
        encoder->Control(AOME_SET_QP, 210);
      }
      encoder->Control(AV1E_ENABLE_SUBGOP_STATS, enable_subgop_stats_);
      GetSubGOPConfigStr();
      encoder->Control(AV1E_SET_SUBGOP_CONFIG_STR,
                       subgop_test_params_.subgop_str);
      av1_process_subgop_config_set(subgop_test_params_.subgop_str,
                                    &user_cfg_set_);
      encoder->Control(AV1E_SET_MIN_GF_INTERVAL,
                       subgop_test_params_.min_gf_interval);
      encoder->Control(AV1E_SET_MAX_GF_INTERVAL,
                       subgop_test_params_.max_gf_interval);
    }
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreDecodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Decoder *decoder) {
    aom_codec_ctx_t *ctx_dec = decoder->GetDecoder();
    if (video->frame() == 0)
      AOM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AV1D_ENABLE_SUBGOP_STATS,
                                    enable_subgop_stats_);
  }

  void InitSubgop() {
    memset(&user_cfg_set_, 0, sizeof(user_cfg_set_));
    subgop_data_.num_steps = MAX_SUBGOP_STATS_SIZE;
    ResetSubgop();
    is_first_frame_in_subgop_key_ = 0;
    frames_from_key_ = 0;
    enable_subgop_stats_ = 1;
    memset(&subgop_last_step_, 0, sizeof(subgop_last_step_));
  }

  void ResetSubgop() {
    subgop_info_.is_user_specified = 0;
    subgop_info_.frames_to_key = 0;
    subgop_info_.gf_interval = 0;
    subgop_info_.size = 0;
    subgop_info_.pos_code = SUBGOP_IN_GOP_GENERIC;

    for (int idx = 0; idx < MAX_SUBGOP_STATS_SIZE; idx++) {
      subgop_data_.step[idx].disp_frame_idx = -1;
      subgop_data_.step[idx].show_existing_frame = -1;
      subgop_data_.step[idx].show_frame = -1;
      subgop_data_.step[idx].is_filtered = -1;
      subgop_data_.step[idx].pyramid_level = 0;
      subgop_data_.step[idx].qindex = 0;
      subgop_data_.step[idx].refresh_frame_flags = 0;
      subgop_data_.step[idx].num_references = -1;
      memset(subgop_data_.step[idx].ref_frame_pyr_level, 0,
             sizeof(subgop_data_.step[idx].ref_frame_pyr_level));
      memset(subgop_data_.step[idx].is_valid_ref_frame, 0,
             sizeof(subgop_data_.step[idx].is_valid_ref_frame));
      memset(subgop_data_.step[idx].ref_frame_map, 0,
             sizeof(subgop_data_.step[idx].ref_frame_map));
      for (int ref = 0; ref < INTER_REFS_PER_FRAME; ref++) {
        subgop_data_.step[idx].ref_frame_disp_order[ref] = -1;
        display_order_test_[idx][ref] = -1;
      }
    }
    subgop_data_.num_steps = 0;
    subgop_data_.step_idx_enc = 0;
    subgop_data_.step_idx_dec = 0;

    subgop_code_test_ = SUBGOP_IN_GOP_GENERIC;
    subgop_size_ = 0;
    frame_num_in_subgop_ = 0;
  }

  void DetermineSubgopCode(libaom_test::Encoder *encoder) {
    encoder->Control(AV1E_GET_FRAME_TYPE, &frame_type_test_);
    if (frame_type_test_ == KEY_FRAME) {
      is_first_frame_in_subgop_key_ = 1;
      return;
    }
    const int is_last_subgop =
        subgop_info_.frames_to_key <= (subgop_info_.gf_interval + 1);
    const int is_first_subgop = is_first_frame_in_subgop_key_;
    if (is_last_subgop)
      subgop_code_test_ = SUBGOP_IN_GOP_LAST;
    else if (is_first_subgop)
      subgop_code_test_ = SUBGOP_IN_GOP_FIRST;
    else
      subgop_code_test_ = SUBGOP_IN_GOP_GENERIC;
    subgop_size_ = subgop_info_.gf_interval;
  }

  virtual bool HandleEncodeResult(::libaom_test::VideoSource *video,
                                  libaom_test::Encoder *encoder) {
    (void)video;
    // Capturing the subgop info at the start of subgop.
    if (!frame_num_in_subgop_) {
      encoder->Control(AV1E_GET_SUB_GOP_CONFIG, &subgop_info_);
      DetermineSubgopCode(encoder);
      // Validation of user specified subgop structure adoption in encoder path.
      ValidateSubgopConfig();
      subgop_data_.num_steps = subgop_info_.num_steps;
    }
    if (subgop_info_.is_user_specified)
      encoder->Control(AV1E_GET_FRAME_INFO, &subgop_data_);
    return 1;
  }

  void FillTestSubgopConfig() {
    int filtered_frames[REF_FRAMES] = { 0 }, buf_idx = 0;
    if (frame_type_test_ == KEY_FRAME) return;

    subgop_cfg_test_.num_frames = (int8_t)subgop_info_.size;
    subgop_cfg_test_.num_steps = (int8_t)subgop_data_.num_steps;
    subgop_cfg_test_.subgop_in_gop_code = subgop_info_.pos_code;
    // Populating the filter-type of out-of-order frames appropriately for all
    // steps in sub-gop.
    for (int idx = 0; idx < subgop_data_.num_steps; idx++) {
      subgop_cfg_test_.step[idx].disp_frame_idx =
          (int8_t)(subgop_data_.step[idx].disp_frame_idx - frames_from_key_);
      subgop_cfg_test_.step[idx].pyr_level =
          (int8_t)subgop_data_.step[idx].pyramid_level;
      subgop_cfg_test_.step[idx].num_references =
          (int8_t)subgop_data_.step[idx].num_references;
      for (int ref = 0; ref < INTER_REFS_PER_FRAME; ref++) {
        subgop_cfg_test_.step[idx].references[ref] =
            (int8_t)subgop_data_.step[idx].ref_frame_pyr_level[ref];
        display_order_test_[idx][ref] =
            subgop_data_.step[idx].ref_frame_disp_order[ref];
      }
      if (subgop_data_.step[idx].is_filtered) {
        filtered_frames[buf_idx++] =
            subgop_data_.step[idx].disp_frame_idx - frames_from_key_;
      } else {
        for (int ref_frame = 0; ref_frame < buf_idx; ref_frame++) {
          if (subgop_cfg_test_.step[idx].disp_frame_idx ==
              (int8_t)filtered_frames[ref_frame])
            subgop_data_.step[idx].is_filtered = 1;
        }
      }
    }
    // Calculating frame type code for all the steps in subgop.
    for (int idx = 0; idx < subgop_data_.num_steps; idx++) {
      FRAME_TYPE_CODE frame_type_code = FRAME_TYPE_INO_VISIBLE;
      int show_existing_frame = subgop_data_.step[idx].show_existing_frame;
      int show_frame = subgop_data_.step[idx].show_frame;
      int is_filtered = subgop_data_.step[idx].is_filtered;

      assert(show_existing_frame >= 0);
      assert(show_frame >= 0);
      assert(frame_type_code != 0);
      if (show_existing_frame == 0) {
        if (show_frame == 0)
          frame_type_code = (is_filtered == 1) ? FRAME_TYPE_OOO_FILTERED
                                               : FRAME_TYPE_OOO_UNFILTERED;
        else if (show_frame == 1)
          frame_type_code = (is_filtered == 1) ? FRAME_TYPE_INO_REPEAT
                                               : FRAME_TYPE_INO_VISIBLE;
      } else if (show_existing_frame == 1) {
        frame_type_code = (is_filtered == 1) ? FRAME_TYPE_INO_REPEAT
                                             : FRAME_TYPE_INO_SHOWEXISTING;
      }
      subgop_cfg_test_.step[idx].type_code = frame_type_code;
    }
  }

  SubGOPCfg *DetermineSubgopConfig() {
    SubGOPCfg *subgop_cfg = user_cfg_set_.config;
    SubGOPCfg *viable_subgop_cfg[MAX_SUBGOP_CODES] = { NULL };
    unsigned int cfg_count = 0;

    for (int idx = 0; idx < user_cfg_set_.num_configs; idx++) {
      if (subgop_cfg[idx].num_frames == (int8_t)subgop_size_) {
        if (subgop_cfg[idx].subgop_in_gop_code == subgop_code_test_)
          return &subgop_cfg[idx];
        else
          viable_subgop_cfg[cfg_count++] = &subgop_cfg[idx];
        assert(cfg_count < MAX_SUBGOP_CODES);
      }
    }
    subgop_code_test_ = SUBGOP_IN_GOP_GENERIC;
    for (unsigned int cfg = 0; cfg < cfg_count; cfg++) {
      if (viable_subgop_cfg[cfg]->subgop_in_gop_code == subgop_code_test_)
        return viable_subgop_cfg[cfg];
    }
    return NULL;
  }

  // Validates frametype(along with temporal filtering), frame coding order
  bool ValidateSubgopFrametype() {
    for (int idx = 0; idx < subgop_cfg_ref_->num_steps; idx++) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        EXPECT_EQ(subgop_cfg_ref_->step[idx].disp_frame_idx,
                  subgop_cfg_test_.step[idx].disp_frame_idx)
            << "Error:display_index doesn't match";
        EXPECT_EQ(subgop_cfg_ref_->step[idx].type_code,
                  subgop_cfg_test_.step[idx].type_code)
            << "Error:frame type doesn't match";
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      }
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    }
    return 1;
  }

  // Validates Pyramid level with user config
  void ValidatePyramidLevel() {
    int8_t max_pyramid_level = 0;
    for (int idx = 0; idx < subgop_cfg_ref_->num_steps; idx++) {
      if (max_pyramid_level < subgop_cfg_ref_->step[idx].pyr_level)
        max_pyramid_level = subgop_cfg_ref_->step[idx].pyr_level;
    }
    for (int idx = 0; idx < subgop_cfg_ref_->num_steps; idx++) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        int8_t ref_pyramid_level =
            (subgop_cfg_ref_->step[idx].pyr_level == max_pyramid_level)
                ? MAX_ARF_LAYERS
                : subgop_cfg_ref_->step[idx].pyr_level;
        EXPECT_EQ(subgop_cfg_test_.step[idx].pyr_level, ref_pyramid_level)
            << "Error:pyramid level doesn't match";
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      }
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    }
  }

  // Validates Pyramid level along with qindex assignment
  void ValidatePyramidLevelQIndex() {
    int level_qindex[MAX_ARF_LAYERS + 1];
    for (int i = 0; i <= MAX_ARF_LAYERS; i++) level_qindex[i] = -1;
    int pyramid_level;
    for (int idx = 0; idx < subgop_cfg_ref_->num_steps; idx++) {
      pyramid_level = subgop_cfg_test_.step[idx].pyr_level;
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        if (level_qindex[pyramid_level] < 0) {
          level_qindex[pyramid_level] = subgop_data_.step[idx].qindex;
        } else if (!subgop_data_.step[idx].show_existing_frame &&
                   !subgop_data_.step[idx].is_filtered) {
          EXPECT_EQ(level_qindex[pyramid_level], subgop_data_.step[idx].qindex)
              << "Error:qindex in a pyramid level doesn't match";
        }
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      }
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    }
    for (pyramid_level = 1; pyramid_level <= MAX_ARF_LAYERS; pyramid_level++) {
      if (level_qindex[pyramid_level] >= 0) {
        EXPECT_LT(level_qindex[pyramid_level - 1], level_qindex[pyramid_level])
            << "Error: level " << pyramid_level - 1 << " qindex "
            << "should be less than level " << pyramid_level << " qindex";
      }
    }
  }

  // Validates reference buffer refresh
  void ValidateRefBufRefresh() {
    int start_idx = 0;
    SubGOPStepData *prev_step_data = &subgop_last_step_;
    if (is_first_frame_in_subgop_key_) {
      start_idx = 1;
      prev_step_data = &subgop_data_.step[0];
    }

    for (int idx = start_idx; idx < subgop_cfg_ref_->num_steps; idx++) {
      SubGOPStepData *curr_step_data = &subgop_data_.step[idx];
      int ref_count = 0;
      int refresh_frame_flags = curr_step_data->refresh_frame_flags;
      // Validates user-defined refresh_flag with decoder
      if (subgop_cfg_ref_->step[idx].refresh != -1 &&
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
          subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
          !curr_step_data->show_existing_frame) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        EXPECT_EQ(subgop_cfg_ref_->step[idx].refresh,
                  (int8_t)refresh_frame_flags)
            << "Error: refresh flag mismatch";
      }
      // Validates reference picture management w.r.t refresh_flags
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (refresh_frame_flags &&
          subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (refresh_frame_flags && !curr_step_data->show_existing_frame) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        for (int mask = refresh_frame_flags; mask; mask >>= 1) {
          if (mask & 1)
            EXPECT_EQ(curr_step_data->disp_frame_idx,
                      (int)curr_step_data->ref_frame_map[ref_count])
                << "Error: reference buffer refresh failed";
          else
            EXPECT_EQ(prev_step_data->ref_frame_map[ref_count],
                      curr_step_data->ref_frame_map[ref_count])
                << "Error: reference buffer refresh failed";
          assert(ref_count < REF_FRAMES);
          ref_count++;
        }
      }

      for (int ref_idx = ref_count; ref_idx < REF_FRAMES; ref_idx++)
        EXPECT_EQ(prev_step_data->ref_frame_map[ref_idx],
                  curr_step_data->ref_frame_map[ref_idx])
            << "Error: reference buffer refresh failed";
      prev_step_data = curr_step_data;
    }
  }

  void ValidateRefFrames() {
    int start_idx = is_first_frame_in_subgop_key_;
    for (int idx = start_idx; idx < subgop_cfg_ref_->num_steps; idx++) {
      unsigned int *ref_frame_map =
          (idx > 0) ? subgop_data_.step[idx - 1].ref_frame_map
                    : subgop_last_step_.ref_frame_map;
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (subgop_cfg_ref_->step[idx].type_code != FRAME_TYPE_INO_SHOWEXISTING) {
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      if (!subgop_data_.step[idx].show_existing_frame) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        EXPECT_EQ(subgop_cfg_ref_->step[idx].num_references,
                  subgop_cfg_test_.step[idx].num_references)
            << "Error:Reference frames count doesn't match";
      }
      // To validate the count of ref_frames and their management with user
      // config.
      for (int ref = 0; ref < subgop_cfg_test_.step[idx].num_references;
           ref++) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        if (subgop_cfg_ref_->step[idx].type_code !=
                FRAME_TYPE_INO_SHOWEXISTING &&
            subgop_data_.step[idx].is_valid_ref_frame[ref]) {
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
        if (subgop_data_.step[idx].is_valid_ref_frame[ref]) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
          EXPECT_EQ(subgop_cfg_ref_->step[idx].references[ref],
                    subgop_cfg_test_.step[idx].references[ref])
              << "Error:Reference frame level doesn't match";
          for (int buf = 0; buf < REF_FRAMES; buf++) {
            if (display_order_test_[idx][ref] == (int)ref_frame_map[buf]) break;
            EXPECT_NE(buf, REF_FRAMES - 1)
                << "Error:Ref frame isn't part of ref_picture_buf";
          }
        }
      }
    }
  }

  bool IsInputSubgopCfgUsed() {
    int num_ooo_frames_ref = 0;
    int num_ooo_frames_test = 0;
    // Encoder may choose to opt out input sub-gop config when use_altref is 0
    // for the given sub-gop
    for (int idx = 0; idx < subgop_cfg_ref_->num_steps; idx++) {
      // Count number out-of-order frames in reference config
      num_ooo_frames_ref +=
          subgop_cfg_ref_->step[idx].type_code == FRAME_TYPE_OOO_FILTERED;
      num_ooo_frames_ref +=
          subgop_cfg_ref_->step[idx].type_code == FRAME_TYPE_OOO_UNFILTERED;
      // Count number out-of-order frames in test config
      num_ooo_frames_test +=
          subgop_cfg_test_.step[idx].type_code == FRAME_TYPE_OOO_FILTERED;
      num_ooo_frames_test +=
          subgop_cfg_test_.step[idx].type_code == FRAME_TYPE_OOO_UNFILTERED;
    }
    return num_ooo_frames_ref == num_ooo_frames_test;
  }

  void ValidateSubgopConfig() {
    if (frame_type_test_ == KEY_FRAME) return;
    subgop_cfg_ref_ = DetermineSubgopConfig();
    if (subgop_cfg_ref_) {
      EXPECT_EQ((int8_t)subgop_size_, subgop_cfg_ref_->num_frames)
          << "Error:subgop config selection wrong";
      subgop_info_.is_user_specified = 1;
    }
  }

  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    frame_num_in_subgop_ += pkt->data.frame.frame_count;
#else
    (void)pkt;
    ++frame_num_in_subgop_;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  }

  virtual bool HandleDecodeResult(const aom_codec_err_t res_dec,
                                  libaom_test::Decoder *decoder) {
    EXPECT_EQ(AOM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AOM_CODEC_OK != res_dec) return 0;
    aom_codec_ctx_t *ctx_dec = decoder->GetDecoder();

    int is_last_frame_in_subgop = (frame_num_in_subgop_ == subgop_info_.size);

    if (subgop_info_.is_user_specified ||
        is_last_frame_in_subgop)  // To collect last step info of subgop in
                                  // encoder defined config
      AOM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AOMD_GET_FRAME_INFO,
                                    &subgop_data_);
    if (is_last_frame_in_subgop) {
      // Validation of sub-gop structure propagation to decoder.
      if (subgop_info_.is_user_specified) {
        FillTestSubgopConfig();
        if ((subgop_info_.is_user_specified = IsInputSubgopCfgUsed())) {
          EXPECT_EQ(subgop_code_test_, subgop_info_.pos_code)
              << "Error:subgop code doesn't match";
          ValidateSubgopFrametype();
          ValidatePyramidLevel();
          if (rc_end_usage_ == AOM_Q) ValidatePyramidLevelQIndex();
          ValidateRefBufRefresh();
          ValidateRefFrames();
        }
      }
      frames_from_key_ += subgop_info_.size;
      if (frame_type_test_ == KEY_FRAME) {
        frames_from_key_ = 0;
      } else {
        is_first_frame_in_subgop_key_ = 0;
        // To collect last step info of subgop.
        assert(subgop_data_.step_idx_dec >= 0);
        memcpy(&subgop_last_step_,
               &subgop_data_.step[subgop_data_.step_idx_dec - 1],
               sizeof(subgop_last_step_));
      }
      ResetSubgop();
    }
    return AOM_CODEC_OK == res_dec;
  }

  SubgopTestParams subgop_test_params_;
  SubGOPSetCfg user_cfg_set_;
  SubGOPCfg subgop_cfg_test_;
  SubGOPCfg *subgop_cfg_ref_;
  SubGOPInfo subgop_info_;
  SubGOPData subgop_data_;
  SubGOPStepData subgop_last_step_;
  SUBGOP_IN_GOP_CODE subgop_code_test_;
  FRAME_TYPE frame_type_test_;
  aom_rc_mode rc_end_usage_;
  int display_order_test_[MAX_SUBGOP_SIZE][REF_FRAMES];
  int subgop_size_;
  bool is_first_frame_in_subgop_key_;
  int frames_from_key_;
  int frame_num_in_subgop_;
  unsigned int frame_num_;
  unsigned int enable_subgop_stats_;
};

TEST_P(SubGopTestLarge, SubGopTest) {
  if (!is_extension_y4m(subgop_test_params_.input_file)) {
    libaom_test::I420VideoSource video(
        subgop_test_params_.input_file, subgop_test_params_.frame_w,
        subgop_test_params_.frame_h, cfg_.g_timebase.den, cfg_.g_timebase.num,
        0, kFrames);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  } else {
    ::libaom_test::Y4mVideoSource video(subgop_test_params_.input_file, 0,
                                        kFrames);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }
}

AV1_INSTANTIATE_TEST_SUITE(SubGopTestLarge,
                           ::testing::ValuesIn(SubGopTestVectors),
                           ::testing::Values(AOM_Q, AOM_VBR
                                             // Disabled to reduce combinations.
                                             //, AOM_CQ, AOM_CBR
                                             ));

typedef struct {
  const char *subgop_str;
  const char *input_file;
  int frame_w;
  int frame_h;
  int lag_in_frames;
} SubgopPsnrTestParams;

static const SubgopPsnrTestParams SubGopPsnrTestVectors[] = {
  { subgop_config_str_preset_map[DEFAULT].preset_tag,
    "hantro_collage_w352h288.yuv", 352, 288, 35 },
  { subgop_config_str_preset_map[DEFAULT].preset_tag, "desktop1.320_180.yuv",
    320, 180, 35 },

  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "hantro_collage_w352h288.yuv", 352, 288, 35 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "pixel_capture_w320h240.yuv", 320, 240, 35 },
  // TODO(any): Enable after fix
  /* { subgop_config_str_preset_map[ENHANCE].preset_tag, "paris_352_288_30.y4m",
     352, 288, 35 },
     { subgop_config_str_preset_map[ENHANCE].preset_tag, "screendata.y4m", 640,
     480, 35 },
     { subgop_config_str_preset_map[ENHANCE].preset_tag, "paris_352_288_30.y4m",
     352, 288, 35 }, */

  { subgop_config_str_preset_map[ASYMMETRIC].preset_tag,
    "pixel_capture_w320h240.yuv", 320, 240, 35 },
  // TODO(any): Enable after fix
  /* { subgop_config_str_preset_map[ASYMMETRIC].preset_tag,
    "desktop1.320_180.yuv", 320, 180, 35 }, */

  { subgop_config_str_preset_map[TEMPORAL_SCALABLE].preset_tag,
    "hantro_collage_w352h288.yuv", 352, 288, 35 },

  // TODO(any): Enable after fix
  /* { subgop_config_str_preset_map[LOW_DELAY].preset_tag,
     "paris_352_288_30.y4m", 352, 288, 0 },
     { subgop_config_str_preset_map[LOW_DELAY].preset_tag,
     "desktop1.320_180.yuv", 320, 180, 0 }, */
};

std::ostream &operator<<(std::ostream &os,
                         const SubgopPsnrTestParams &test_arg) {
  return os << "SubgopPsnrTestParams { sub_gop_config:" << test_arg.subgop_str
            << " source_file:" << test_arg.input_file
            << " frame_width:" << test_arg.frame_w
            << " frame_height:" << test_arg.frame_h << " cpu_used:" << kCpuUsed
            << " lag_in_frames:" << test_arg.lag_in_frames << " }";
}

class SubGopPSNRCheckTestLarge
    : public ::libaom_test::CodecTestWith2Params<SubgopPsnrTestParams,
                                                 aom_rc_mode>,
      public ::libaom_test::EncoderTest {
 protected:
  SubGopPSNRCheckTestLarge()
      : EncoderTest(GET_PARAM(0)), test_params_(GET_PARAM(1)),
        rc_end_usage_(GET_PARAM(2)) {
    Reset();
  }
  virtual ~SubGopPSNRCheckTestLarge() {}

  void Reset() {
    frame_num_ = 0;
    total_psnr_ = 0.0;
    enable_subgop_ = 0;
  }

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libaom_test::kOnePassGood);
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = test_params_.lag_in_frames;
    cfg_.rc_end_usage = rc_end_usage_;
    init_flags_ = AOM_CODEC_USE_PSNR;
  }

  virtual void PSNRPktHook(const aom_codec_cx_pkt_t *pkt) {
    // Accumulate total psnr
    total_psnr_ += pkt->data.psnr.psnr[0];
    frame_num_++;
  }

  double GetAveragePsnr() const {
    if (frame_num_) return total_psnr_ / frame_num_;
    return 0.0;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, kCpuUsed);
      if (rc_end_usage_ == AOM_Q) {
        encoder->Control(AOME_SET_QP, 210);
      }
      if (enable_subgop_)
        encoder->Control(AV1E_SET_SUBGOP_CONFIG_STR, test_params_.subgop_str);
    }
  }
  unsigned int enable_subgop_;
  SubgopPsnrTestParams test_params_;

 private:
  aom_rc_mode rc_end_usage_;
  double total_psnr_;
  unsigned int frame_num_;
};

TEST_P(SubGopPSNRCheckTestLarge, SubGopPSNRCheck) {
  std::unique_ptr<libaom_test::VideoSource> video;
  const double psnr_diff_thresh = 0.5;
  if (is_extension_y4m(test_params_.input_file)) {
    video.reset(
        new libaom_test::Y4mVideoSource(test_params_.input_file, 0, kFrames));
  } else {
    video.reset(new libaom_test::YUVVideoSource(
        test_params_.input_file, AOM_IMG_FMT_I420, test_params_.frame_w,
        test_params_.frame_h, 30, 1, 0, kFrames));
  }

  // Encode with no sub-gop configuration
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
  const double psnr_no_subgop_ = GetAveragePsnr();
  Reset();

  // Encode with default sub-gop configuration
  enable_subgop_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
  const double psnr_subgop_ = GetAveragePsnr();

  const double psnr_diff = psnr_subgop_ - psnr_no_subgop_;
  EXPECT_LE(fabs(psnr_diff), psnr_diff_thresh);
}

// TODO(any) : Enable AOM_CBR after fix
AV1_INSTANTIATE_TEST_SUITE(SubGopPSNRCheckTestLarge,
                           ::testing::ValuesIn(SubGopPsnrTestVectors),
                           ::testing::Values(AOM_Q, AOM_VBR,
                                             AOM_CQ /*, AOM_CBR*/));

typedef struct {
  const char *subgop_str;
  const char *input_file;
  int frame_w;
  int frame_h;
  int lag_in_frames;
  int max_gf_interval;
} SubGopSwitchTestParams;

std::ostream &operator<<(std::ostream &os,
                         const SubGopSwitchTestParams &test_arg) {
  return os << "SubGopSwitchTestParams { sub_gop_config:" << test_arg.subgop_str
            << " source_file:" << test_arg.input_file
            << " frame_width:" << test_arg.frame_w
            << " frame_height:" << test_arg.frame_h << " cpu_used:" << kCpuUsed
            << " lag_in_frames:" << test_arg.lag_in_frames
            << " max_gf_interval:" << test_arg.max_gf_interval << " }";
}

static const SubGopSwitchTestParams SubgopSwitchTestVectors[] = {
  { subgop_config_str_preset_map[DEFAULT].preset_tag, "niklas_640_480_30.yuv",
    640, 480, 35, 16 },
  /* TODO(sarahparker/debargha): Enable after adding default 32 subgop config.
   { subgop_config_str_preset_map[DEFAULT].preset_tag, "niklas_640_480_30.yuv",
    640, 480, 35, 32 },*/
  { subgop_config_str_preset_map[ENHANCE].preset_tag, "desktop1.320_180.yuv",
    320, 180, 35, 16 },
  { subgop_config_str_preset_map[ENHANCE].preset_tag,
    "hantro_collage_w352h288.yuv", 352, 288, 35, 16 },
  { subgop_config_str_preset_map[ASYMMETRIC].preset_tag,
    "pixel_capture_w320h240.yuv", 320, 240, 35, 16 },
  { subgop_config_str_preset_map[TEMPORAL_SCALABLE].preset_tag,
    "paris_352_288_30.y4m", 352, 288, 35, 16 },
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "screendata.y4m", 640,
    480, 0, 16 },
  { subgop_config_str_preset_map[LOW_DELAY].preset_tag, "screendata.y4m", 640,
    480, 0, 32 },
};

using libaom_test::ACMRandom;
class SubGopSwitchingTestLarge
    : public ::libaom_test::CodecTestWith2Params<SubGopSwitchTestParams,
                                                 aom_rc_mode>,
      public ::libaom_test::EncoderTest {
 protected:
  SubGopSwitchingTestLarge()
      : EncoderTest(GET_PARAM(0)), test_params_(GET_PARAM(1)),
        rc_end_usage_(GET_PARAM(2)) {
    last_subgop_str_ = NULL;
    num_subgop_cfg_used_ = 0;
    rnd_.Reset(ACMRandom::DeterministicSeed());
    ResetSubgop();
  }
  virtual ~SubGopSwitchingTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libaom_test::kOnePassGood);
    cfg_.g_threads = 1;
    cfg_.rc_end_usage = rc_end_usage_;
    // Keep sufficient distance between keyframes to let subgop configs be used.
    cfg_.kf_min_dist = 65;
    cfg_.kf_max_dist = 9999;
    cfg_.g_lag_in_frames = test_params_.lag_in_frames;
  }

  void ResetSubgop() {
    frame_num_in_subgop_ = 0;
    subgop_size_ = 0;
    memset(&subgop_info_, 0, sizeof(subgop_info_));
  }

  bool GetRandSwitch() { return !(rnd_.Rand8() & 1); }

  int GetRandGFIntervalEnh() {
    const int subgop_size_enh[] = { 6, 8, 10, 11, 12, 13, 14, 15, 16 };
    const int length = sizeof(subgop_size_enh) / sizeof(subgop_size_enh[0]);
    const int idx = rnd_.Rand8() % length;

    return subgop_size_enh[idx];
  }

  void set_subgop_config(::libaom_test::Encoder *encoder) {
    const bool switch_subgop_cfg = GetRandSwitch();
    if (!switch_subgop_cfg) return;

    // Switch between input sub-gop and no sub-gop config and configure the
    // encoder
    const char *subgop_str = last_subgop_str_ ? NULL : test_params_.subgop_str;
    int max_gf_interval = test_params_.max_gf_interval;
    // Get max gf interval for enh config
    if (subgop_str && !strcmp(subgop_str, "enh"))
      max_gf_interval = GetRandGFIntervalEnh();

    // Set subgop config string
    encoder->Control(AV1E_SET_SUBGOP_CONFIG_STR, subgop_str);

    // Set max gf interval
    if (subgop_str) encoder->Control(AV1E_SET_MAX_GF_INTERVAL, max_gf_interval);

    // Keep min gf interval same as max gf interval in most cases, to ensure
    // that user-provided subgop config is used.
    int min_gf_interval = max_gf_interval;
    // In case of no subgop config / enhanced subgop config, test arbitrary gf
    // intervals by setting a lower min gf interval.
    if (!subgop_str || !strcmp(subgop_str, "enh")) min_gf_interval = 6;

    // Set min gf interval
    encoder->Control(AV1E_SET_MIN_GF_INTERVAL, min_gf_interval);

    last_subgop_str_ = subgop_str;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, kCpuUsed);
      if (rc_end_usage_ == AOM_Q) {
        encoder->Control(AOME_SET_QP, 210);
      }
      set_subgop_config(encoder);
    }

    // Configure sub-gop string before sub-gop decision
    if (frame_num_in_subgop_ == subgop_size_) {
      ResetSubgop();
      set_subgop_config(encoder);
    }
  }

  virtual bool HandleEncodeResult(::libaom_test::VideoSource *video,
                                  libaom_test::Encoder *encoder) {
    (void)video;
    // Get sub-gop info at beginning of the sub-gop
    if (!frame_num_in_subgop_) {
      FRAME_TYPE frame_type = FRAME_TYPES;

      // Get current frame type
      encoder->Control(AV1E_GET_FRAME_TYPE, &frame_type);
      assert(frame_type != FRAME_TYPES);
      // Get subgop config
      encoder->Control(AV1E_GET_SUB_GOP_CONFIG, &subgop_info_);

      // Compute sub-gop size
      subgop_size_ = subgop_info_.gf_interval;
      // Include KF in sub-gop size
      if (frame_type == KEY_FRAME) subgop_size_++;

      // Update count of subgop cfg usage by the encoder
      num_subgop_cfg_used_ += subgop_info_.is_user_specified;
    }
    return 1;
  }

  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    frame_num_in_subgop_ += pkt->data.frame.frame_count;
#else
    (void)pkt;
    ++frame_num_in_subgop_;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  }

  virtual bool HandleDecodeResult(const aom_codec_err_t res_dec,
                                  libaom_test::Decoder *decoder) {
    EXPECT_EQ(AOM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AOM_CODEC_OK != res_dec) return 0;

    return AOM_CODEC_OK == res_dec;
  }
  SubGopSwitchTestParams test_params_;
  unsigned int num_subgop_cfg_used_;

 private:
  ACMRandom rnd_;
  aom_rc_mode rc_end_usage_;
  SubGOPInfo subgop_info_;
  unsigned int frame_num_in_subgop_;
  unsigned int subgop_size_;
  const char *last_subgop_str_;
};

TEST_P(SubGopSwitchingTestLarge, SubGopSwitching) {
  std::unique_ptr<libaom_test::VideoSource> video;
  if (is_extension_y4m(test_params_.input_file)) {
    video.reset(
        new libaom_test::Y4mVideoSource(test_params_.input_file, 0, kFrames));
  } else {
    video.reset(new libaom_test::YUVVideoSource(
        test_params_.input_file, AOM_IMG_FMT_I420, test_params_.frame_w,
        test_params_.frame_h, 30, 1, 0, kFrames));
  }

  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));

  // Check input config is used by the encoder
  EXPECT_TRUE(num_subgop_cfg_used_);
}

AV1_INSTANTIATE_TEST_SUITE(SubGopSwitchingTestLarge,
                           ::testing::ValuesIn(SubgopSwitchTestVectors),
                           ::testing::Values(AOM_Q, AOM_VBR, AOM_CQ, AOM_CBR));
}  // namespace
