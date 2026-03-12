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

#include "avm_ports/system_state.h"
#include "av2/common/level.h"
#include "av2/common/tile_common.h"
#include "av2/encoder/encoder.h"
#include "av2/common/annexA.h"

/* clang-format off */
#define UNDEFINED_LEVEL     \
  { .level = SEQ_LEVEL_MAX, \
    .max_picture_size = 0,  \
    .max_h_size = 0,        \
    .max_v_size = 0,        \
    .max_display_rate = 0,  \
    .max_decode_rate = 0,   \
    .max_header_rate = 0,   \
    .main_mbps = 0,         \
    .high_mbps = 0,         \
    .main_cr = 0,           \
    .high_cr = 0,           \
    .max_tiles = 0,         \
    .max_tile_cols = 0 }
/* clang-format on */

const AV2LevelSpec av2_level_defs[SEQ_LEVELS] = {
  { .level = SEQ_LEVEL_2_0,
    .max_picture_size = 147456,
    .max_h_size = 640,
    .max_v_size = 640,
    .max_display_rate = 4423680L,
    .max_decode_rate = 5529600L,
    .max_header_rate = 150,
    .main_mbps = 1.5,
    .high_mbps = 0,
    .main_cr = 2.0,
    .high_cr = 0,
    .max_tiles = 8,
    .max_tile_cols = 4 },
  { .level = SEQ_LEVEL_2_1,
    .max_picture_size = 278784,
    .max_h_size = 880,
    .max_v_size = 880,
    .max_display_rate = 8363520L,
    .max_decode_rate = 10454400L,
    .max_header_rate = 150,
    .main_mbps = 3.0,
    .high_mbps = 0,
    .main_cr = 2.0,
    .high_cr = 0,
    .max_tiles = 8,
    .max_tile_cols = 4 },
  { .level = SEQ_LEVEL_3_0,
    .max_picture_size = 665856,
    .max_h_size = 1360,
    .max_v_size = 1360,
    .max_display_rate = 19975680L,
    .max_decode_rate = 24969600L,
    .max_header_rate = 150,
    .main_mbps = 6.0,
    .high_mbps = 0,
    .main_cr = 2.0,
    .high_cr = 0,
    .max_tiles = 16,
    .max_tile_cols = 6 },
  { .level = SEQ_LEVEL_3_1,
    .max_picture_size = 1065024,
    .max_h_size = 1720,
    .max_v_size = 1720,
    .max_display_rate = 31950720L,
    .max_decode_rate = 39938400L,
    .max_header_rate = 150,
    .main_mbps = 10.0,
    .high_mbps = 0,
    .main_cr = 2.0,
    .high_cr = 0,
    .max_tiles = 16,
    .max_tile_cols = 6 },
  { .level = SEQ_LEVEL_4_0,
    .max_picture_size = 2359296,
    .max_h_size = 2560,
    .max_v_size = 2560,
    .max_display_rate = 70778880L,
    .max_decode_rate = 77856768L,
    .max_header_rate = 300,
    .main_mbps = 12.0,
    .high_mbps = 30.0,
    .main_cr = 4.0,
    .high_cr = 4.0,
    .max_tiles = 32,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_4_1,
    .max_picture_size = 2359296,
    .max_h_size = 2560,
    .max_v_size = 2560,
    .max_display_rate = 141557760L,
    .max_decode_rate = 155713536L,
    .max_header_rate = 300,
    .main_mbps = 20.0,
    .high_mbps = 50.0,
    .main_cr = 4.0,
    .high_cr = 4.0,
    .max_tiles = 32,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_5_0,
    .max_picture_size = 8912896,
    .max_h_size = 4975,
    .max_v_size = 4975,
    .max_display_rate = 267386880L,
    .max_decode_rate = 273715200L,
    .max_header_rate = 300,
    .main_mbps = 30.0,
    .high_mbps = 100.0,
    .main_cr = 6.0,
    .high_cr = 4.0,
    .max_tiles = 64,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_5_1,
    .max_picture_size = 8912896,
    .max_h_size = 4975,
    .max_v_size = 4975,
    .max_display_rate = 534773760L,
    .max_decode_rate = 547430400L,
    .max_header_rate = 300,
    .main_mbps = 40.0,
    .high_mbps = 160.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 64,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_5_2,
    .max_picture_size = 8912896,
    .max_h_size = 4975,
    .max_v_size = 4975,
    .max_display_rate = 1069547520L,
    .max_decode_rate = 1094860800L,
    .max_header_rate = 300,
    .main_mbps = 60.0,
    .high_mbps = 240.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 64,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_5_3,
    .max_picture_size = 8912896,
    .max_h_size = 4975,
    .max_v_size = 4975,
    .max_display_rate = 1069547520L,
    .max_decode_rate = 1176502272L,
    .max_header_rate = 300,
    .main_mbps = 60.0,
    .high_mbps = 240.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 64,
    .max_tile_cols = 8 },
  { .level = SEQ_LEVEL_6_0,
    .max_picture_size = 35651584,
    .max_h_size = 9951,
    .max_v_size = 9951,
    .max_display_rate = 1069547520L,
    .max_decode_rate = 1176502272L,
    .max_header_rate = 300,
    .main_mbps = 60.0,
    .high_mbps = 240.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 128,
    .max_tile_cols = 16 },
  { .level = SEQ_LEVEL_6_1,
    .max_picture_size = 35651584,
    .max_h_size = 9951,
    .max_v_size = 9951,
    .max_display_rate = 2139095040L,
    .max_decode_rate = 2189721600L,
    .max_header_rate = 300,
    .main_mbps = 100.0,
    .high_mbps = 480.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 128,
    .max_tile_cols = 16 },
  { .level = SEQ_LEVEL_6_2,
    .max_picture_size = 35651584,
    .max_h_size = 9951,
    .max_v_size = 9951,
    .max_display_rate = 4278190080L,
    .max_decode_rate = 4379443200L,
    .max_header_rate = 300,
    .main_mbps = 160.0,
    .high_mbps = 800.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 128,
    .max_tile_cols = 16 },
  { .level = SEQ_LEVEL_6_3,
    .max_picture_size = 35651584,
    .max_h_size = 9951,
    .max_v_size = 9951,
    .max_display_rate = 4278190080L,
    .max_decode_rate = 4706009088L,
    .max_header_rate = 300,
    .main_mbps = 160.0,
    .high_mbps = 800.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 128,
    .max_tile_cols = 16 },
  { .level = SEQ_LEVEL_7_0,
    .max_picture_size = 142606336,
    .max_h_size = 19902,
    .max_v_size = 19902,
    .max_display_rate = 4278190080L,
    .max_decode_rate = 4706009088L,
    .max_header_rate = 960,
    .main_mbps = 160.0,
    .high_mbps = 800.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 256,
    .max_tile_cols = 32 },
  { .level = SEQ_LEVEL_7_1,
    .max_picture_size = 142606336,
    .max_h_size = 19902,
    .max_v_size = 19902,
    .max_display_rate = 8556380160L,
    .max_decode_rate = 8758886400L,
    .max_header_rate = 960,
    .main_mbps = 200.0,
    .high_mbps = 960.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 256,
    .max_tile_cols = 32 },
  { .level = SEQ_LEVEL_7_2,
    .max_picture_size = 142606336,
    .max_h_size = 19902,
    .max_v_size = 19902,
    .max_display_rate = 17112760320L,
    .max_decode_rate = 17517772800L,
    .max_header_rate = 960,
    .main_mbps = 320.0,
    .high_mbps = 1600.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 256,
    .max_tile_cols = 32 },
  { .level = SEQ_LEVEL_7_3,
    .max_picture_size = 142606336,
    .max_h_size = 19902,
    .max_v_size = 19902,
    .max_display_rate = 17112760320L,
    .max_decode_rate = 18824036352L,
    .max_header_rate = 960,
    .main_mbps = 320.0,
    .high_mbps = 1600.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 256,
    .max_tile_cols = 32 },
  { .level = SEQ_LEVEL_8_0,
    .max_picture_size = 530841600,
    .max_h_size = 38400,
    .max_v_size = 38400,
    .max_display_rate = 17112760320L,
    .max_decode_rate = 18824036352L,
    .max_header_rate = 960,
    .main_mbps = 320.0,
    .high_mbps = 1600.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 512,
    .max_tile_cols = 64 },
  { .level = SEQ_LEVEL_8_1,
    .max_picture_size = 530841600,
    .max_h_size = 38400,
    .max_v_size = 38400,
    .max_display_rate = 34225520640L,
    .max_decode_rate = 34910031052L,
    .max_header_rate = 960,
    .main_mbps = 400.0,
    .high_mbps = 1920.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 512,
    .max_tile_cols = 64 },
  { .level = SEQ_LEVEL_8_2,
    .max_picture_size = 530841600,
    .max_h_size = 38400,
    .max_v_size = 38400,
    .max_display_rate = 68451041280L,
    .max_decode_rate = 69820062105L,
    .max_header_rate = 960,
    .main_mbps = 640.0,
    .high_mbps = 3200.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 512,
    .max_tile_cols = 64 },
  { .level = SEQ_LEVEL_8_3,
    .max_picture_size = 530841600,
    .max_h_size = 38400,
    .max_v_size = 38400,
    .max_display_rate = 68451041280L,
    .max_decode_rate = 75296145408L,
    .max_header_rate = 960,
    .main_mbps = 640.0,
    .high_mbps = 3200.0,
    .main_cr = 8.0,
    .high_cr = 4.0,
    .max_tiles = 512,
    .max_tile_cols = 64 }
};

static const AV2SubstreamLevelSpec av2_substream_level_defs[15] = {
  { .max_picture_size = 2359296,
    .max_picture_size_x = 1433600,
    .scale_factor_x = 1.5,
    .max_v_size_x = 1600,
    .max_h_size_x = 896,
    .max_tile_cols_x = 7,
    .max_header_rate_x = 132 },
  { .max_picture_size = 2359296,
    .max_picture_size_x = 552960,
    .scale_factor_x = 4.0,
    .max_v_size_x = 960,
    .max_h_size_x = 576,
    .max_tile_cols_x = 4,
    .max_header_rate_x = 132 },
  { .max_picture_size = 2359296,
    .max_picture_size_x = 245760,
    .scale_factor_x = 9.0,
    .max_v_size_x = 640,
    .max_h_size_x = 384,
    .max_tile_cols_x = 3,
    .max_header_rate_x = 132 },
  { .max_picture_size = 8912896,
    .max_picture_size_x = 3768320,
    .scale_factor_x = 1.5,
    .max_v_size_x = 2560,
    .max_h_size_x = 1472,
    .max_tile_cols_x = 7,
    .max_header_rate_x = 132 },
  { .max_picture_size = 8912896,
    .max_picture_size_x = 2088960,
    .scale_factor_x = 4.0,
    .max_v_size_x = 1920,
    .max_h_size_x = 1088,
    .max_tile_cols_x = 4,
    .max_header_rate_x = 132 },
  { .max_picture_size = 8912896,
    .max_picture_size_x = 983040,
    .scale_factor_x = 9.0,
    .max_v_size_x = 1280,
    .max_h_size_x = 768,
    .max_tile_cols_x = 3,
    .max_header_rate_x = 132 },
  { .max_picture_size = 35651584,
    .max_picture_size_x = 11673600,
    .scale_factor_x = 1.5,
    .max_v_size_x = 5120,
    .max_h_size_x = 2280,
    .max_tile_cols_x = 13,
    .max_header_rate_x = 132 },
  { .max_picture_size = 35651584,
    .max_picture_size_x = 8355840,
    .scale_factor_x = 4.0,
    .max_v_size_x = 3840,
    .max_h_size_x = 2176,
    .max_tile_cols_x = 8,
    .max_header_rate_x = 132 },
  { .max_picture_size = 35651584,
    .max_picture_size_x = 3768320,
    .scale_factor_x = 9.0,
    .max_v_size_x = 2560,
    .max_h_size_x = 1472,
    .max_tile_cols_x = 5,
    .max_header_rate_x = 132 },
  { .max_picture_size = 142606336,
    .max_picture_size_x = 58982400,
    .scale_factor_x = 1.5,
    .max_v_size_x = 10240,
    .max_h_size_x = 5760,
    .max_tile_cols_x = 26,
    .max_header_rate_x = 132 },
  { .max_picture_size = 142606336,
    .max_picture_size_x = 33177600,
    .scale_factor_x = 4.0,
    .max_v_size_x = 7680,
    .max_h_size_x = 4320,
    .max_tile_cols_x = 16,
    .max_header_rate_x = 132 },
  { .max_picture_size = 142606336,
    .max_picture_size_x = 14745600,
    .scale_factor_x = 9.0,
    .max_v_size_x = 5120,
    .max_h_size_x = 2880,
    .max_tile_cols_x = 11,
    .max_header_rate_x = 132 },
  { .max_picture_size = 530841600,
    .max_picture_size_x = 235929600,
    .scale_factor_x = 1.5,
    .max_v_size_x = 20480,
    .max_h_size_x = 11520,
    .max_tile_cols_x = 52,
    .max_header_rate_x = 132 },
  { .max_picture_size = 530841600,
    .max_picture_size_x = 132710400,
    .scale_factor_x = 4.0,
    .max_v_size_x = 15360,
    .max_h_size_x = 8640,
    .max_tile_cols_x = 32,
    .max_header_rate_x = 132 },
  { .max_picture_size = 530841600,
    .max_picture_size_x = 58982400,
    .scale_factor_x = 9.0,
    .max_v_size_x = 10240,
    .max_h_size_x = 5760,
    .max_tile_cols_x = 21,
    .max_header_rate_x = 132 },
};

typedef enum {
  LUMA_PIC_SIZE_TOO_LARGE,
  LUMA_PIC_H_SIZE_TOO_LARGE,
  LUMA_PIC_V_SIZE_TOO_LARGE,
  LUMA_PIC_H_SIZE_TOO_SMALL,
  LUMA_PIC_V_SIZE_TOO_SMALL,
  TOO_MANY_TILE_COLUMNS,
  TOO_MANY_TILES,
  TILE_RATE_TOO_HIGH,
  TILE_TOO_LARGE,
#if CONFIG_G018
  TILE_WIDTH_TOO_LARGE,
#endif  // CONFIG_G018
  CROPPED_TILE_WIDTH_TOO_SMALL,
  CROPPED_TILE_HEIGHT_TOO_SMALL,
  TILE_WIDTH_INVALID,
  FRAME_HEADER_RATE_TOO_HIGH,
  DISPLAY_RATE_TOO_HIGH,
  DECODE_RATE_TOO_HIGH,
  FRAME_SYMBOL_COUNT_TOO_HIGH,
  CS_TOO_HIGH,
  TILE_SIZE_HEADER_RATE_TOO_HIGH,
  BITRATE_TOO_HIGH,
  DECODER_MODEL_FAIL,
  REF_FRAMES_FAIL,

  TARGET_LEVEL_FAIL_IDS,
  TARGET_LEVEL_OK,
} TARGET_LEVEL_FAIL_ID;

static const char *level_fail_messages[TARGET_LEVEL_FAIL_IDS] = {
  "The picture size is too large.",
  "The picture width is too large.",
  "The picture height is too large.",
  "The picture width is too small.",
  "The picture height is too small.",
  "Too many tile columns are used.",
  "Too many tiles are used.",
  "The tile rate is too high.",
  "The tile size is too large.",
#if CONFIG_G018
  "The tile width is too large.",
#endif  // CONFIG_G018
  "The cropped tile width is less than 8.",
  "The cropped tile height is less than 8.",
  "The tile width is invalid.",
  "The frame header rate is too high.",
  "The display luma sample rate is too high.",
  "The decoded luma sample rate is too high.",
  "The number of frame symbols is too high.",
  "The compression size is too high.",
  "The product of max tile size and header rate is too high.",
  "The bitrate is too high.",
  "The decoder model fails.",
  "The number of reference frames is invalid.",
};

static const double bitrate_profile_factor_table[] = { 1.0, 2.0, 3.0 };
static const double picture_size_profile_factor_table[] = { 15.0, 30.0, 36.0 };

static const char level_string[SEQ_LEVEL_MAX + 1][9] = {
  "2.0",      "2.1",      "3.0",      "3.1",      "4.0",      "4.1",
  "5.0",      "5.1",      "5.2",      "5.3",      "6.0",      "6.1",
  "6.2",      "6.3",      "7.0",      "7.1",      "7.2",      "7.3",
  "8.0",      "8.1",      "8.2",      "8.3",      "reserved", "reserved",
  "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
  "reserved", "31"
};

static double get_max_bitrate(const AV2LevelSpec *const level_spec, int tier,
                              BITSTREAM_PROFILE profile
#if CONFIG_AV2_PROFILES
                              ,
                              int subsampling_x, int subsampling_y,
                              int monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
                              ,
                              double multistream_scalling_x
#endif  // CONFIG_F428_MULTISTREAM
) {
  if (level_spec->level < SEQ_LEVEL_4_0) tier = 0;
#if CONFIG_F428_MULTISTREAM
  const double scale =
      multistream_scalling_x == 0.0 ? 1 : multistream_scalling_x;
  const double bitrate_basis =
      (tier ? level_spec->high_mbps / scale : level_spec->main_mbps / scale) *
      1e6;
#else
  const double bitrate_basis =
      (tier ? level_spec->high_mbps : level_spec->main_mbps) * 1e6;
#endif  // CONFIG_F428_MULTISTREAM
#if CONFIG_AV2_PROFILES
  uint32_t chroma_format_idc = CHROMA_FORMAT_420;
  av2_get_chroma_format_idc(subsampling_x, subsampling_y, monochrome,
                            &chroma_format_idc);
  const int profile_scaling_factor =
      get_profile_scaling_factor(profile, chroma_format_idc);
  double bitrate_profile_factor =
      bitrate_profile_factor_table[profile_scaling_factor];
#else
  const double bitrate_profile_factor =
      profile == PROFILE_0 ? 1.0 : (profile == PROFILE_1 ? 2.0 : 3.0);
#endif  // CONFIG_AV2_PROFILES
  return bitrate_basis * bitrate_profile_factor;
}

// Returns maximum allowed compressed size in bytes.
static double get_max_compressed_size(const AV2LevelSpec *const level_spec,
                                      int tier, BITSTREAM_PROFILE profile,
                                      int luma_sample_count,
                                      double frame_parsing_time
#if CONFIG_AV2_PROFILES
                                      ,
                                      int subsampling_x, int subsampling_y,
                                      int monochrome
#endif  // CONFIG_AV2_PROFILES
) {
  if (level_spec->level < SEQ_LEVEL_4_0) tier = 0;
  const double min_comp_basis =
      (tier ? level_spec->high_cr : level_spec->main_cr);
#if CONFIG_AV2_PROFILES
  uint32_t chroma_format_idc = CHROMA_FORMAT_420;

  av2_get_chroma_format_idc(subsampling_x, subsampling_y, monochrome,
                            &chroma_format_idc);
  const int profile_scaling_factor =
      get_profile_scaling_factor(profile, chroma_format_idc);
  double picture_size_profile_factor =
      picture_size_profile_factor_table[profile_scaling_factor];
#else
  const double picture_size_profile_factor =
      profile == PROFILE_0 ? 15.0 : (profile == PROFILE_1 ? 30.0 : 36.0);
#endif  // CONFIG_AV2_PROFILES

  double max_compressed_size =
      ((long long)(frame_parsing_time * level_spec->max_decode_rate *
                   picture_size_profile_factor) >>
       3) /
      min_comp_basis;

  double limit =
      ((long long)(luma_sample_count * picture_size_profile_factor) >> 3) *
      1.25;

  return AVMMIN(limit, max_compressed_size);
}

static double get_max_frame_symbol_count(const AV2LevelSpec *const level_spec,
                                         int tier, BITSTREAM_PROFILE profile,
                                         double frame_parsing_time
#if CONFIG_AV2_PROFILES
                                         ,
                                         int subsampling_x, int subsampling_y,
                                         int monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
                                         ,
                                         double multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
) {
  if (level_spec->level < SEQ_LEVEL_4_0) tier = 0;
  const double min_comp_basis =
      (tier ? level_spec->high_cr : level_spec->main_cr);
#if CONFIG_AV2_PROFILES
  uint32_t chroma_format_idc = CHROMA_FORMAT_420;

  av2_get_chroma_format_idc(subsampling_x, subsampling_y, monochrome,
                            &chroma_format_idc);
  const int profile_scaling_factor =
      get_profile_scaling_factor(profile, chroma_format_idc);
  double picture_size_profile_factor =
      picture_size_profile_factor_table[profile_scaling_factor];
#else
  const double picture_size_profile_factor =
      profile == PROFILE_0 ? 15.0 : (profile == PROFILE_1 ? 30.0 : 36.0);
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
  double scale = multi_stream_scaling_x == 0 ? 1 : multi_stream_scaling_x;
  double max_frame_symbol_count =
      frame_parsing_time * (level_spec->max_decode_rate / scale) *
      picture_size_profile_factor * (8 / (9 * min_comp_basis) + 1 / 48);
#else
  double max_frame_symbol_count =
      frame_parsing_time * level_spec->max_decode_rate *
      picture_size_profile_factor * (8 / (9 * min_comp_basis) + 1 / 48);
#endif  //  CONFIG_F428_MULTISTREAM
  return max_frame_symbol_count;
}

double av2_get_max_bitrate_for_level(AV2_LEVEL level_index, int tier,
                                     BITSTREAM_PROFILE profile
#if CONFIG_AV2_PROFILES
                                     ,
                                     int subsampling_x, int subsampling_y,
                                     int monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
                                     ,
                                     double multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
) {
  assert(is_valid_seq_level_idx(level_index));
  return get_max_bitrate(&av2_level_defs[level_index], tier, profile
#if CONFIG_AV2_PROFILES
                         ,
                         subsampling_x, subsampling_y, monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
                         ,
                         multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
  );
}

void av2_get_max_tiles_for_level(AV2_LEVEL level_index, int *const max_tiles,
                                 int *const max_tile_cols) {
  assert(is_valid_seq_level_idx(level_index));
  const AV2LevelSpec *const level_spec = &av2_level_defs[level_index];
  *max_tiles = level_spec->max_tiles;
  *max_tile_cols = level_spec->max_tile_cols;
}

// We assume time t to be valid if and only if t >= 0.0.
// So INVALID_TIME can be defined as anything less than 0.
#define INVALID_TIME (-1.0)

// This corresponds to "free_buffer" in the spec.
static void release_buffer(DECODER_MODEL *const decoder_model, int idx) {
  assert(idx >= 0 && idx < BUFFER_POOL_MAX_SIZE);
  FRAME_BUFFER *const this_buffer = &decoder_model->frame_buffer_pool[idx];
  this_buffer->decoder_ref_count = 0;
  this_buffer->player_ref_count = 0;
  this_buffer->display_index = -1;
  this_buffer->presentation_time = INVALID_TIME;
}

static void initialize_buffer_pool(DECODER_MODEL *const decoder_model,
                                   const int ref_frames) {
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    release_buffer(decoder_model, i);
  }
  for (int i = 0; i < ref_frames; ++i) {
    decoder_model->vbi[i] = -1;
  }
}

static int get_free_buffer(DECODER_MODEL *const decoder_model) {
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count == 0 &&
        this_buffer->player_ref_count == 0)
      return i;
  }
  return -1;
}

static void update_ref_buffers(const AV2_COMMON *const cm,
                               DECODER_MODEL *const decoder_model, int idx,
                               int refresh_frame_flags) {
  FRAME_BUFFER *const this_buffer = &decoder_model->frame_buffer_pool[idx];
  for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
    if (refresh_frame_flags & (1 << i)) {
      const int pre_idx = decoder_model->vbi[i];
      if (pre_idx != -1) {
        --decoder_model->frame_buffer_pool[pre_idx].decoder_ref_count;
      }
      decoder_model->vbi[i] = idx;
      ++this_buffer->decoder_ref_count;
    }
  }
}

bool is_filter_enabled_frame(const AV2_COMMON *const cm) {
  bool inloop_filtering_enabled =
      cm->lf.apply_deblocking_filter[0] != 0 ||
      cm->lf.apply_deblocking_filter[1] != 0 ||
      cm->cdef_info.cdef_frame_enable != 0 ||
      cm->cur_frame->ccso_info.ccso_enable[0] != 0 ||
      cm->cur_frame->ccso_info.ccso_enable[1] != 0 ||
      cm->cur_frame->ccso_info.ccso_enable[2] != 0 ||
      cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[2].frame_restoration_type != RESTORE_NONE ||
      cm->gdf_info.gdf_mode != 0;

  return inloop_filtering_enabled;
}

// The time (in seconds) required to decode a frame.
static double time_to_decode_frame(const AV2_COMMON *const cm,
                                   int64_t max_decode_rate) {
  if (cm->show_existing_frame) return 0.0;
  const FRAME_TYPE frame_type = cm->current_frame.frame_type;
  int luma_samples = 0;
  if (frame_type == KEY_FRAME || frame_type == INTRA_ONLY_FRAME) {
    if (cm->features.allow_global_intrabc && is_filter_enabled_frame(cm))
      luma_samples = 2 * cm->width * cm->height;
    else
      luma_samples = cm->width * cm->height;
  } else {
    const SequenceHeader *const seq_params = &cm->seq_params;
    const int max_frame_width = seq_params->max_frame_width;
    const int max_frame_height = seq_params->max_frame_height;
    luma_samples = max_frame_width * max_frame_height;
  }

  return luma_samples / (double)max_decode_rate;
}

// Release frame buffers that are no longer needed for decode or display.
// It corresponds to "start_decode_at_removal_time" in the spec.
static void release_processed_frames(DECODER_MODEL *const decoder_model,
                                     double removal_time) {
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    FRAME_BUFFER *const this_buffer = &decoder_model->frame_buffer_pool[i];
    if (this_buffer->player_ref_count > 0) {
      if (this_buffer->presentation_time >= 0.0 &&
          this_buffer->presentation_time <= removal_time) {
        this_buffer->player_ref_count = 0;
        if (this_buffer->decoder_ref_count == 0) {
          release_buffer(decoder_model, i);
        }
      }
    }
  }
}

static int frames_in_buffer_pool(const DECODER_MODEL *const decoder_model) {
  int frames_in_pool = 0;
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count > 0 ||
        this_buffer->player_ref_count > 0) {
      ++frames_in_pool;
    }
  }
  return frames_in_pool;
}

static double get_presentation_time(const DECODER_MODEL *const decoder_model,
                                    int display_index) {
  if (decoder_model->mode == SCHEDULE_MODE) {
    assert(0 && "SCHEDULE_MODE NOT SUPPORTED");
    return INVALID_TIME;
  } else {
    const double initial_presentation_delay =
        decoder_model->initial_presentation_delay;
    // Can't decide presentation time until the initial presentation delay is
    // known.
    if (initial_presentation_delay < 0.0) return INVALID_TIME;

    return initial_presentation_delay +
           display_index * decoder_model->num_ticks_per_picture *
               decoder_model->display_clock_tick;
  }
}

#define MAX_TIME 1e16
double time_next_buffer_is_free(const DECODER_MODEL *const decoder_model) {
  if (decoder_model->num_decoded_frame == 0) {
    return (double)decoder_model->decoder_buffer_delay / 90000.0;
  }

  double buf_free_time = MAX_TIME;
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count == 0) {
      if (this_buffer->player_ref_count == 0) {
        return decoder_model->current_time;
      }
      const double presentation_time = this_buffer->presentation_time;
      if (presentation_time >= 0.0 && presentation_time < buf_free_time) {
        buf_free_time = presentation_time;
      }
    }
  }
  return buf_free_time < MAX_TIME ? buf_free_time : INVALID_TIME;
}
#undef MAX_TIME

static double get_removal_time(const DECODER_MODEL *const decoder_model) {
  if (decoder_model->mode == SCHEDULE_MODE) {
    assert(0 && "SCHEDULE_MODE IS NOT SUPPORTED YET");
    return INVALID_TIME;
  } else {
    return time_next_buffer_is_free(decoder_model);
  }
}

void av2_decoder_model_print_status(const DECODER_MODEL *const decoder_model) {
  printf(
      "\n status %d, num_frame %3d, num_decoded_frame %3d, "
      "num_shown_frame %3d, current time %6.2f, frames in buffer %2d, "
      "presentation delay %6.2f, total interval %6.2f\n",
      decoder_model->status, decoder_model->num_frame,
      decoder_model->num_decoded_frame, decoder_model->num_shown_frame,
      decoder_model->current_time, frames_in_buffer_pool(decoder_model),
      decoder_model->initial_presentation_delay,
      decoder_model->dfg_interval_queue.total_interval);
  for (int i = 0; i < 10; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    printf("buffer %d, decode count %d, display count %d, present time %6.4f\n",
           i, this_buffer->decoder_ref_count, this_buffer->player_ref_count,
           this_buffer->presentation_time);
  }
}

// op_index is the operating point index.
void av2_decoder_model_init(const AV2_COMP *const cpi, AV2_LEVEL level,
                            int op_index, DECODER_MODEL *const decoder_model) {
  avm_clear_system_state();
  decoder_model->status = DECODER_MODEL_OK;
  decoder_model->level = level;

  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  decoder_model->bit_rate = get_max_bitrate(
      av2_level_defs + level, cpi->tier[op_index], seq_params->seq_profile_idc
#if CONFIG_AV2_PROFILES
      ,
      seq_params->subsampling_x, seq_params->subsampling_y,
      seq_params->monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
      ,
      cpi->level_params.multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
  );

  // TODO(huisu or anyone): implement SCHEDULE_MODE.
  decoder_model->mode = RESOURCE_MODE;
  decoder_model->encoder_buffer_delay = 20000;
  decoder_model->decoder_buffer_delay = 70000;
  decoder_model->is_low_delay_mode = false;

  decoder_model->first_bit_arrival_time = 0.0;
  decoder_model->last_bit_arrival_time = 0.0;
  decoder_model->coded_bits = 0;

  decoder_model->removal_time = INVALID_TIME;
  decoder_model->presentation_time = INVALID_TIME;
  decoder_model->decode_samples = 0;
  decoder_model->display_samples = 0;
  decoder_model->max_decode_rate = 0.0;
  decoder_model->max_tile_rate_satisfy = true;
  decoder_model->compressed_size_satisfy = true;
  decoder_model->frame_symbol_count_satisfy = true;
  decoder_model->max_display_rate = 0.0;

  decoder_model->num_frame = -1;
  decoder_model->num_decoded_frame = -1;
  decoder_model->num_shown_frame = -1;
  decoder_model->current_time = 0.0;

  initialize_buffer_pool(decoder_model, cm->seq_params.ref_frames);

  DFG_INTERVAL_QUEUE *const dfg_interval_queue =
      &decoder_model->dfg_interval_queue;
  dfg_interval_queue->total_interval = 0.0;
  dfg_interval_queue->head = 0;
  dfg_interval_queue->size = 0;

  if (cm->ci_params_encoder.ci_timing_info_present_flag) {
    decoder_model->num_ticks_per_picture =
        cm->ci_params_encoder.timing_info.num_ticks_per_elemental_duration;
    decoder_model->display_clock_tick =
        cm->ci_params_encoder.timing_info.num_ticks_per_elemental_duration /
        cm->ci_params_encoder.timing_info.time_scale;
  } else {
    decoder_model->num_ticks_per_picture = 1;
    decoder_model->display_clock_tick = 1.0 / cpi->framerate;
  }

  decoder_model->initial_display_delay =
      seq_params->op_params[op_index].initial_display_delay;
  decoder_model->initial_presentation_delay = INVALID_TIME;
  decoder_model->decode_rate = av2_level_defs[level].max_decode_rate;
}

int av2_get_max_level_ref_frames(const AV2_COMMON *const cm, OBU_TYPE obu_type,
                                 AV2_LEVEL level_index) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int cap = (seq_params->ref_frames != 8) ? 16 : 8;

  const int max_picture_size = av2_level_defs[level_index].max_picture_size;

  const int64_t current_picture_size =
      seq_params->max_frame_width * seq_params->max_frame_height;

  int64_t limit = (int64_t)(max_picture_size * 8) / current_picture_size;

  const int decode_count =
      cm->features.allow_global_intrabc && is_filter_enabled_frame(cm) ? 2 : 1;

  if (decode_count == 2 &&
      (obu_type != OBU_CLK || seq_params->max_mlayer_id != 0)) {
    limit -= 1;
  }
  const int max_level_ref_frames = (int)AVMMIN(cap, limit);
  return max_level_ref_frames;
}

void av2_decoder_model_process_frame(const AV2_COMP *const cpi,
                                     size_t coded_bits,
                                     DECODER_MODEL *const decoder_model,
                                     AV2LevelSpec *const level_spec) {
  if (!decoder_model || decoder_model->status != DECODER_MODEL_OK) return;

  avm_clear_system_state();

  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
#if CONFIG_F428_MULTISTREAM
  const AV2LevelParams *const level_params = &cpi->level_params;
  const double multi_stream_scaling_x =
      level_params->multi_stream_scaling_x == 0
          ? 1.0
          : level_params->multi_stream_scaling_x;
#endif  //  CONFIG_F428_MULTISTREAM
  const int luma_pic_size = cm->width * cm->height;
  const int show_existing_frame = cm->show_existing_frame;
  const int show_frame = cm->immediate_output_picture || show_existing_frame;
  ++decoder_model->num_frame;
  if (!show_existing_frame) ++decoder_model->num_decoded_frame;
  if (show_frame) ++decoder_model->num_shown_frame;
  decoder_model->coded_bits += coded_bits;

  int display_idx = -1;
  if (show_existing_frame) {
    display_idx = decoder_model->vbi[cm->sef_ref_fb_idx];
    if (display_idx < 0) {
      decoder_model->status = DECODE_EXISTING_FRAME_BUF_EMPTY;
      return;
    }
  } else {
    const double removal_time = get_removal_time(decoder_model);
    if (removal_time < 0.0) {
      decoder_model->status = DECODE_FRAME_BUF_UNAVAILABLE;
      return;
    }

    const int previous_decode_samples = decoder_model->decode_samples;
    const double previous_removal_time = decoder_model->removal_time;
    assert(previous_removal_time < removal_time);
    decoder_model->removal_time = removal_time;
    decoder_model->decode_samples = luma_pic_size;
    bool is_global_intrabc_inloop_filtered_frame =
        cm->features.allow_global_intrabc && is_filter_enabled_frame(cm);
    const double dt = is_global_intrabc_inloop_filtered_frame
                          ? 0.5 * (removal_time - previous_removal_time)
                          : (removal_time - previous_removal_time);
    const int64_t this_decode_rate = (int64_t)(previous_decode_samples / dt);

    decoder_model->max_decode_rate =
        AVMMAX(decoder_model->max_decode_rate, this_decode_rate);
#if CONFIG_F428_MULTISTREAM
    const int scaled =
        (int)((level_spec->max_tiles / multi_stream_scaling_x) * 120.0 * dt);
#else
    const int scaled = (int)(level_spec->max_tiles * 120.0 * dt);
#endif  //  CONFIG_F428_MULTISTREAM
    int max_tile_limit = AVMMIN(level_spec->max_tiles, AVMMAX(1, scaled));

    decoder_model->max_tile_rate_satisfy =
        decoder_model->max_tile_rate_satisfy &&
        (cm->tiles.rows * cm->tiles.cols <= max_tile_limit);

    AV2_LEVEL level = decoder_model->level;

    double compressed_size_limit = get_max_compressed_size(
        av2_level_defs + level, cpi->tier[0], seq_params->seq_profile_idc,
        luma_pic_size, dt
#if CONFIG_AV2_PROFILES
        ,
        seq_params->subsampling_x, seq_params->subsampling_y,
        seq_params->monochrome
#endif
    );
    decoder_model->compressed_size_satisfy =
        decoder_model->compressed_size_satisfy &&
        ((coded_bits >> 3) <= compressed_size_limit);

    double frame_symbol_count_limit = get_max_frame_symbol_count(
        av2_level_defs + level, cpi->tier[0], seq_params->seq_profile_idc, dt
#if CONFIG_AV2_PROFILES
        ,
        seq_params->subsampling_x, seq_params->subsampling_y,
        seq_params->monochrome
#endif
#if CONFIG_F428_MULTISTREAM
        ,
        multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
    );
    decoder_model->frame_symbol_count_satisfy =
        decoder_model->frame_symbol_count_satisfy &&
        (cm->features.frame_symbol_count <= frame_symbol_count_limit);
    // A frame with show_existing_frame being false indicates the end of a DFG.
    // Update the bits arrival time of this DFG.
    const double buffer_delay = (decoder_model->encoder_buffer_delay +
                                 decoder_model->decoder_buffer_delay) /
                                90000.0;
    const double latest_arrival_time = removal_time - buffer_delay;
    decoder_model->first_bit_arrival_time =
        AVMMAX(decoder_model->last_bit_arrival_time, latest_arrival_time);
    decoder_model->last_bit_arrival_time =
        decoder_model->first_bit_arrival_time +
        (double)decoder_model->coded_bits / decoder_model->bit_rate;
    // Smoothing buffer underflows if the last bit arrives after the removal
    // time.
    if (decoder_model->last_bit_arrival_time > removal_time &&
        !decoder_model->is_low_delay_mode) {
      decoder_model->status = SMOOTHING_BUFFER_UNDERFLOW;
      return;
    }
    // Reset the coded bits for the next DFG.
    decoder_model->coded_bits = 0;

    // Check if the smoothing buffer overflows.
    DFG_INTERVAL_QUEUE *const queue = &decoder_model->dfg_interval_queue;
    if (queue->size >= DFG_INTERVAL_QUEUE_SIZE) {
      assert(0);
    }
    const double first_bit_arrival_time = decoder_model->first_bit_arrival_time;
    const double last_bit_arrival_time = decoder_model->last_bit_arrival_time;
    // Remove the DFGs with removal time earlier than last_bit_arrival_time.
    while (queue->buf[queue->head].removal_time <= last_bit_arrival_time &&
           queue->size > 0) {
      if (queue->buf[queue->head].removal_time - first_bit_arrival_time +
              queue->total_interval >
          1.0) {
        decoder_model->status = SMOOTHING_BUFFER_OVERFLOW;
        return;
      }
      queue->total_interval -= queue->buf[queue->head].last_bit_arrival_time -
                               queue->buf[queue->head].first_bit_arrival_time;
      queue->head = (queue->head + 1) % DFG_INTERVAL_QUEUE_SIZE;
      --queue->size;
    }
    // Push current DFG into the queue.
    const int queue_index =
        (queue->head + queue->size++) % DFG_INTERVAL_QUEUE_SIZE;
    queue->buf[queue_index].first_bit_arrival_time = first_bit_arrival_time;
    queue->buf[queue_index].last_bit_arrival_time = last_bit_arrival_time;
    queue->buf[queue_index].removal_time = removal_time;
    queue->total_interval += last_bit_arrival_time - first_bit_arrival_time;
    // The smoothing buffer can hold at most "bit_rate" bits, which is
    // equivalent to 1 second of total interval.
    if (queue->total_interval > 1.0) {
      decoder_model->status = SMOOTHING_BUFFER_OVERFLOW;
      return;
    }

    release_processed_frames(decoder_model, removal_time);
    decoder_model->current_time =
        removal_time + time_to_decode_frame(cm, decoder_model->decode_rate);

    const int cfbi = get_free_buffer(decoder_model);
    if (cfbi < 0) {
      decoder_model->status = DECODE_FRAME_BUF_UNAVAILABLE;
      return;
    }
    const CurrentFrame *const current_frame = &cm->current_frame;
    decoder_model->frame_buffer_pool[cfbi].frame_type =
        cm->current_frame.frame_type;
    display_idx = cfbi;
    update_ref_buffers(cm, decoder_model, cfbi,
                       current_frame->refresh_frame_flags);

    if (decoder_model->initial_presentation_delay < 0.0) {
      // Display can begin after required number of frames have been buffered.
      if (frames_in_buffer_pool(decoder_model) >=
          decoder_model->initial_display_delay) {
        decoder_model->initial_presentation_delay = decoder_model->current_time;
        // Update presentation time for each shown frame in the frame buffer.
        for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
          FRAME_BUFFER *const this_buffer =
              &decoder_model->frame_buffer_pool[i];
          if (this_buffer->player_ref_count == 0) continue;
          assert(this_buffer->display_index >= 0);
          this_buffer->presentation_time =
              get_presentation_time(decoder_model, this_buffer->display_index);
        }
      }
    }
  }

  // Display.
  if (show_frame) {
    assert(display_idx >= 0 && display_idx < BUFFER_POOL_MAX_SIZE);
    FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[display_idx];
    ++this_buffer->player_ref_count;
    this_buffer->display_index = decoder_model->num_shown_frame;
    const double presentation_time =
        get_presentation_time(decoder_model, this_buffer->display_index);
    this_buffer->presentation_time = presentation_time;
    if (presentation_time >= 0.0 &&
        decoder_model->current_time > presentation_time) {
      decoder_model->status = DISPLAY_FRAME_LATE;
      return;
    }

    const int previous_display_samples = decoder_model->display_samples;
    const double previous_presentation_time = decoder_model->presentation_time;
    decoder_model->display_samples = luma_pic_size;
    decoder_model->presentation_time = presentation_time;
    if (presentation_time >= 0.0 && previous_presentation_time >= 0.0) {
      assert(previous_presentation_time < presentation_time);
      const int64_t this_display_rate =
          (int64_t)(previous_display_samples /
                    (presentation_time - previous_presentation_time));
      decoder_model->max_display_rate =
          AVMMAX(decoder_model->max_display_rate, this_display_rate);
    }
  }
}
#if CONFIG_F428_MULTISTREAM
// Get the index of the level parameter entry in av2_substream_level_defs for
// sub-stream case given the level and the scaling factor.
// Should we define the behavior for levels below 4.0?
int level_to_sub_stream_level_index(AV2_LEVEL level, double scaling_factor_x) {
  int level_base =
      level < SEQ_LEVEL_5_0 ? 0 : ((level - SEQ_LEVEL_5_0) >> 2) + 1;
  int offset = scaling_factor_x == 1.5 ? 0 : (scaling_factor_x == 4.0 ? 1 : 2);
  return 3 * level_base + offset;
}
#endif  // CONFIG_F428_MULTISTREAM

void av2_init_level_info(AV2_COMP *cpi) {
  for (int op_index = 0; op_index < MAX_NUM_OPERATING_POINTS; ++op_index) {
    AV2LevelInfo *const this_level_info =
        cpi->level_params.level_info[op_index];
    if (!this_level_info) continue;
    memset(this_level_info, 0, sizeof(*this_level_info));
    AV2LevelSpec *const level_spec = &this_level_info->level_spec;
    level_spec->level = SEQ_LEVEL_MAX;
    AV2LevelStats *const level_stats = &this_level_info->level_stats;
    level_stats->min_cropped_tile_width = INT_MAX;
    level_stats->min_cropped_tile_height = INT_MAX;
    level_stats->min_frame_width = INT_MAX;
    level_stats->min_frame_height = INT_MAX;
    level_stats->tile_width_is_valid = 1;
    level_stats->min_cr = 1e8;

    FrameWindowBuffer *const frame_window_buffer =
        &this_level_info->frame_window_buffer;
    frame_window_buffer->num = 0;
    frame_window_buffer->start = 0;

    const AV2_COMMON *const cm = &cpi->common;
    const int upscaled_width = cm->width;
    const int height = cm->height;
    const int pic_size = upscaled_width * height;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const this_model = &this_level_info->decoder_models[level];
      const AV2LevelSpec *const spec = &av2_level_defs[level];
      if (upscaled_width > spec->max_h_size || height > spec->max_v_size ||
          pic_size > spec->max_picture_size) {
        // Turn off decoder model for this level as the frame size already
        // exceeds level constraints.
        this_model->status = DECODER_MODEL_DISABLED;
      } else {
        av2_decoder_model_init(cpi, level, op_index, this_model);
      }
    }
  }
}

static void get_temporal_parallel_params(int scalability_mode_idc,
                                         int *temporal_parallel_num,
                                         int *temporal_parallel_denom) {
  if (scalability_mode_idc < 0) {
    *temporal_parallel_num = 1;
    *temporal_parallel_denom = 1;
    return;
  }

  // TODO(huisu@): handle scalability cases.
  if (scalability_mode_idc == SCALABILITY_SS) {
    (void)scalability_mode_idc;
  } else {
    (void)scalability_mode_idc;
  }
}

#define MAX_TILE_SIZE (4096 * 2304)
#define MIN_FRAME_WIDTH 16
#define MIN_FRAME_HEIGHT 16

// (547430400 = 3840 * 2160 * 60 * 1.1)
#define MAX_TILE_SIZE_HEADER_RATE_PRODUCT 547430400

static TARGET_LEVEL_FAIL_ID check_level_constraints(
#if CONFIG_F428_MULTISTREAM
    const AV2_COMP *const cpi,
#endif
    const AV2LevelInfo *const level_info, AV2_LEVEL level, int tier,
    int is_still_picture, BITSTREAM_PROFILE profile, int check_bitrate
#if CONFIG_AV2_PROFILES
    ,
    int subsampling_x, int subsampling_y, int monochrome
#endif  // CONFIG_AV2_PROFILES
) {
  const DECODER_MODEL *const decoder_model = &level_info->decoder_models[level];
  const DECODER_MODEL_STATUS decoder_model_status = decoder_model->status;
  if (decoder_model_status != DECODER_MODEL_OK &&
      decoder_model_status != DECODER_MODEL_DISABLED) {
    return DECODER_MODEL_FAIL;
  }
#if CONFIG_F428_MULTISTREAM
  bool is_multi_stream = cpi->level_params.multi_stream_scaling_x == 1.5 ||
                         cpi->level_params.multi_stream_scaling_x == 4.0 ||
                         cpi->level_params.multi_stream_scaling_x == 9.0;
  double multi_stream_scaling_x =
      is_multi_stream ? cpi->level_params.multi_stream_scaling_x : 1.0;
  const int multi_stream_idx =
      is_multi_stream
          ? level_to_sub_stream_level_index(level, multi_stream_scaling_x)
          : 0;
  const AV2SubstreamLevelSpec *const target_sub_stream_level_spec =
      &av2_substream_level_defs[multi_stream_idx];
#endif  //  CONFIG_F428_MULTISTREAM
  const AV2LevelSpec *const level_spec = &level_info->level_spec;
  const AV2LevelSpec *const target_level_spec = &av2_level_defs[level];
  const AV2LevelStats *const level_stats = &level_info->level_stats;
  TARGET_LEVEL_FAIL_ID fail_id = TARGET_LEVEL_OK;
  do {
#if CONFIG_F428_MULTISTREAM
    const int max_picture_size =
        is_multi_stream ? (target_sub_stream_level_spec->max_v_size_x *
                           target_sub_stream_level_spec->max_h_size_x)
                        : target_level_spec->max_picture_size;
    const int max_h_size = is_multi_stream
                               ? target_sub_stream_level_spec->max_h_size_x
                               : target_level_spec->max_h_size;
    const int max_v_size = is_multi_stream
                               ? target_sub_stream_level_spec->max_v_size_x
                               : target_level_spec->max_v_size;
    const int max_tile_cols =
        is_multi_stream ? target_sub_stream_level_spec->max_tile_cols_x
                        : target_level_spec->max_tile_cols;
    const int max_tiles =
        (int)(target_level_spec->max_tiles / multi_stream_scaling_x);
    if (level_spec->max_picture_size > max_picture_size) {
#else
    if (level_spec->max_picture_size > target_level_spec->max_picture_size) {
#endif  //  CONFIG_F428_MULTISTREAM
      fail_id = LUMA_PIC_SIZE_TOO_LARGE;
      break;
    }
#if CONFIG_F428_MULTISTREAM
    if (level_spec->max_h_size > max_h_size) {
#else
    if (level_spec->max_h_size > target_level_spec->max_h_size) {
#endif  //  CONFIG_F428_MULTISTREAM
      fail_id = LUMA_PIC_H_SIZE_TOO_LARGE;
      break;
    }
#if CONFIG_F428_MULTISTREAM
    if (level_spec->max_v_size > max_v_size) {
#else
    if (level_spec->max_v_size > target_level_spec->max_v_size) {
#endif  //  CONFIG_F428_MULTISTREAM
      fail_id = LUMA_PIC_V_SIZE_TOO_LARGE;
      break;
    }
#if CONFIG_F428_MULTISTREAM
    if (level_spec->max_tile_cols > max_tile_cols) {
#else
    if (level_spec->max_tile_cols > target_level_spec->max_tile_cols) {
#endif  //  CONFIG_F428_MULTISTREAM
      fail_id = TOO_MANY_TILE_COLUMNS;
      break;
    }
#if CONFIG_F428_MULTISTREAM
    if (level_spec->max_tiles > max_tiles) {
#else
    if (level_spec->max_tiles > target_level_spec->max_tiles) {
#endif  //  CONFIG_F428_MULTISTREAM
      fail_id = TOO_MANY_TILES;
      break;
    }

    if (level_spec->max_tile_rate > target_level_spec->max_tiles * 120) {
      fail_id = TILE_RATE_TOO_HIGH;
      break;
    }

#if CONFIG_G018
    // check if tile area using scaled limit:
    // TileWidth * TileHeight <= av2_tile_area_scaling_factor[tier][level] *
    // 4096 * 2304/4
    const int level_idx = target_level_spec->level;
    const int tier_idx = (tier > 0) ? 1 : 0;
    if (level_idx != SEQ_LEVEL_MAX) {
      int scaling_factor = av2_tile_area_scaling_factor[tier_idx][level_idx];
      const uint32_t max_tile_area = (scaling_factor * MAX_TILE_SIZE) >> 2;
      if (level_stats->max_tile_size > (int)max_tile_area) {
        fail_id = TILE_TOO_LARGE;
        break;
      }

      // Check tile width using scaled limit
      // TileWidth <= av2_tile_width_scaling_factor[tier][level] *
      // MAX_TILE_WIDTH/4
      scaling_factor = av2_tile_width_scaling_factor[tier_idx][level_idx];
      const int max_tile_width_limit = (scaling_factor * MAX_TILE_WIDTH) >> 2;
      if (level_stats->max_tile_width > max_tile_width_limit) {
        fail_id = TILE_WIDTH_TOO_LARGE;
        break;
      }
    }
#else
    if (level_stats->max_tile_size > MAX_TILE_SIZE) {
      fail_id = TILE_TOO_LARGE;
      break;
    }
#endif  // CONFIG_G018

    if (level_stats->min_frame_width < MIN_FRAME_WIDTH) {
      fail_id = LUMA_PIC_H_SIZE_TOO_SMALL;
      break;
    }

    if (level_stats->min_frame_height < MIN_FRAME_HEIGHT) {
      fail_id = LUMA_PIC_V_SIZE_TOO_SMALL;
      break;
    }

    if (!level_stats->tile_width_is_valid) {
      fail_id = TILE_WIDTH_INVALID;
      break;
    }
    if (!is_still_picture) {
#if CONFIG_F428_MULTISTREAM
      const int max_header_rate =
          is_multi_stream ? target_sub_stream_level_spec->max_header_rate_x
                          : target_level_spec->max_header_rate;
      const double max_display_rate =
          (double)target_level_spec->max_display_rate / multi_stream_scaling_x;
      const double max_decode_rate =
          (double)target_level_spec->max_decode_rate / multi_stream_scaling_x;

      if (level_spec->max_header_rate > (max_header_rate * (1 + (tier * 2)))) {
#else
      if (level_spec->max_header_rate >
          (target_level_spec->max_header_rate * (1 + (tier * 2)))) {
#endif  //  CONFIG_F428_MULTISTREAM
        fail_id = FRAME_HEADER_RATE_TOO_HIGH;
        break;
      }
#if CONFIG_F428_MULTISTREAM
      if (decoder_model->max_display_rate > max_display_rate) {
#else
      if (decoder_model->max_display_rate >
          (double)target_level_spec->max_display_rate) {
#endif  //  CONFIG_F428_MULTISTREAM
        fail_id = DISPLAY_RATE_TOO_HIGH;
        break;
      }
#if CONFIG_F428_MULTISTREAM
      if (decoder_model->max_decode_rate > max_decode_rate) {
#else
      if (decoder_model->max_decode_rate > target_level_spec->max_decode_rate) {
#endif  //  CONFIG_F428_MULTISTREAM
        fail_id = DECODE_RATE_TOO_HIGH;
        break;
      }

      if (!decoder_model->max_tile_rate_satisfy) {
        fail_id = TOO_MANY_TILES;
        break;
      }
      if (!decoder_model->compressed_size_satisfy) {
        fail_id = CS_TOO_HIGH;
        break;
      }

      if (!decoder_model->frame_symbol_count_satisfy) {
        fail_id = FRAME_SYMBOL_COUNT_TOO_HIGH;
        break;
      }
    }

    if (check_bitrate) {
      // Check average bitrate instead of max_bitrate.
      const double bitrate_limit =
          get_max_bitrate(target_level_spec, tier, profile
#if CONFIG_AV2_PROFILES
                          ,
                          subsampling_x, subsampling_y, monochrome
#endif  // CONFIG_AV2_PROFILES
#if CONFIG_F428_MULTISTREAM
                          ,
                          cpi->level_params.multi_stream_scaling_x
#endif  //  CONFIG_F428_MULTISTREAM
          );
      const double avg_bitrate = level_stats->total_compressed_size * 8.0 /
                                 level_stats->total_time_encoded;
      if (avg_bitrate > bitrate_limit) {
        fail_id = BITRATE_TOO_HIGH;
        break;
      }
    }

    if (target_level_spec->level > SEQ_LEVEL_5_1) {
      int temporal_parallel_num;
      int temporal_parallel_denom;
      const int scalability_mode_idc = -1;
      get_temporal_parallel_params(scalability_mode_idc, &temporal_parallel_num,
                                   &temporal_parallel_denom);

      const int val = level_stats->max_tile_size * level_spec->max_header_rate *
                      temporal_parallel_denom / temporal_parallel_num;
#if CONFIG_G018
      /*MaxTileSizeInLumaSamples * NumFrameHeadersPerSec is less than or equal
       * to (av2_tile_area_scaling_factor[ TierIdx ][ LevelIdx ] * 547,430,400
       * )/ 4. The number of 547,430,400 corresponds to (where this number is
       * the decode luma sample rate of 3840x2160 * 60fps * 1.1).*/
      if (level_idx != SEQ_LEVEL_MAX) {
        const int scaling_factor =
            av2_tile_area_scaling_factor[tier_idx][level_idx];
        const uint64_t max_tile_size_header_rate =
            ((uint64_t)scaling_factor * MAX_TILE_SIZE_HEADER_RATE_PRODUCT) >> 2;
        if ((uint64_t)val > max_tile_size_header_rate) {
          fail_id = TILE_SIZE_HEADER_RATE_TOO_HIGH;
          break;
        }
      }
#else
      if (val > MAX_TILE_SIZE_HEADER_RATE_PRODUCT) {
        fail_id = TILE_SIZE_HEADER_RATE_TOO_HIGH;
        break;
      }
#endif  // CONFIG_G018
    }

  } while (0);

  return fail_id;
}

static void get_tile_stats(const AV2_COMMON *const cm,
                           const TileDataEnc *const tile_data,
                           int *max_tile_size, int *min_cropped_tile_width,
                           int *min_cropped_tile_height, int *tile_width_valid
#if CONFIG_G018
                           ,
                           int *max_tile_width
#endif  // CONFIG_G018
) {
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  *max_tile_size = 0;
  *min_cropped_tile_width = INT_MAX;
  *min_cropped_tile_height = INT_MAX;
  *tile_width_valid = 1;
#if CONFIG_G018
  *max_tile_width = 0;
#endif  // CONFIG_G018

  for (int tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (int tile_col = 0; tile_col < tile_cols; ++tile_col) {
      const TileInfo *const tile_info =
          &tile_data[tile_row * cm->tiles.cols + tile_col].tile_info;
      const int tile_width =
          (tile_info->mi_col_end - tile_info->mi_col_start) * MI_SIZE;
      const int tile_height =
          (tile_info->mi_row_end - tile_info->mi_row_start) * MI_SIZE;
      const int tile_size = tile_width * tile_height;
      *max_tile_size = AVMMAX(*max_tile_size, tile_size);
#if CONFIG_G018
      *max_tile_width = AVMMAX(*max_tile_width, tile_width);
#endif  // CONFIG_G018

      const int cropped_tile_width =
          cm->width - tile_info->mi_col_start * MI_SIZE;
      const int cropped_tile_height =
          cm->height - tile_info->mi_row_start * MI_SIZE;
      *min_cropped_tile_width =
          AVMMIN(*min_cropped_tile_width, cropped_tile_width);
      *min_cropped_tile_height =
          AVMMIN(*min_cropped_tile_height, cropped_tile_height);

      const int is_right_most_tile =
          tile_info->mi_col_end == cm->mi_params.mi_cols;
      if (!is_right_most_tile) {
        *tile_width_valid &= tile_width >= 64;
      }
    }
  }
}

static int store_frame_record(int64_t ts_start, int64_t ts_end,
                              size_t encoded_size, int pic_size,
                              int frame_header_count, int tiles,
                              int immediate_output_picture,
                              int show_existing_frame,
                              FrameWindowBuffer *const buffer) {
  if (buffer->num < FRAME_WINDOW_SIZE) {
    ++buffer->num;
  } else {
    buffer->start = (buffer->start + 1) % FRAME_WINDOW_SIZE;
  }
  const int new_idx = (buffer->start + buffer->num - 1) % FRAME_WINDOW_SIZE;
  FrameRecord *const record = &buffer->buf[new_idx];
  record->ts_start = ts_start;
  record->ts_end = ts_end;
  record->encoded_size_in_bytes = encoded_size;
  record->pic_size = pic_size;
  record->frame_header_count = frame_header_count;
  record->tiles = tiles;
  record->immediate_output_picture = immediate_output_picture;
  record->show_existing_frame = show_existing_frame;
  return new_idx;
}

// Count the number of frames encoded in the last "duration" ticks, in display
// time.
static int count_frames(const FrameWindowBuffer *const buffer,
                        int64_t duration) {
  const int current_idx = (buffer->start + buffer->num - 1) % FRAME_WINDOW_SIZE;
  // Assume current frame is shown frame.
  assert(buffer->buf[current_idx].immediate_output_picture);

  const int64_t current_time = buffer->buf[current_idx].ts_end;
  const int64_t time_limit = AVMMAX(current_time - duration, 0);
  int num_frames = 1;
  int index = current_idx - 1;
  for (int i = buffer->num - 2; i >= 0; --i, --index, ++num_frames) {
    if (index < 0) index = FRAME_WINDOW_SIZE - 1;
    const FrameRecord *const record = &buffer->buf[index];
    if (!record->immediate_output_picture) continue;
    const int64_t ts_start = record->ts_start;
    if (ts_start < time_limit) break;
  }

  return num_frames;
}

// Scan previously encoded frames and update level metrics accordingly.
static void scan_past_frames(const FrameWindowBuffer *const buffer,
                             int num_frames_to_scan,
                             AV2LevelSpec *const level_spec,
                             AV2LevelStats *const level_stats) {
  const int num_frames_in_buffer = buffer->num;
  int index = (buffer->start + num_frames_in_buffer - 1) % FRAME_WINDOW_SIZE;
  int frame_headers = 0;
  int tiles = 0;
  int64_t display_samples = 0;
  int64_t decoded_samples = 0;
  size_t encoded_size_in_bytes = 0;
  for (int i = 0; i < AVMMIN(num_frames_in_buffer, num_frames_to_scan); ++i) {
    const FrameRecord *const record = &buffer->buf[index];
    frame_headers += record->frame_header_count;
    if (!record->show_existing_frame) {
      decoded_samples += record->pic_size;
    }
    if (record->immediate_output_picture) {
      display_samples += record->pic_size;
    }
    tiles += record->tiles;
    encoded_size_in_bytes += record->encoded_size_in_bytes;
    --index;
    if (index < 0) index = FRAME_WINDOW_SIZE - 1;
  }
  level_spec->max_header_rate =
      AVMMAX(level_spec->max_header_rate, frame_headers);
  // TODO(huisu): we can now compute max display rate with the decoder model, so
  // these couple of lines can be removed. Keep them here for a while for
  // debugging purpose.
  level_spec->max_display_rate =
      AVMMAX(level_spec->max_display_rate, display_samples);
  level_spec->max_decode_rate =
      AVMMAX(level_spec->max_decode_rate, decoded_samples);
  level_spec->max_tile_rate = AVMMAX(level_spec->max_tile_rate, tiles);
  level_stats->max_bitrate =
      AVMMAX(level_stats->max_bitrate, (int)encoded_size_in_bytes * 8);
}

double av2_get_compression_ratio(const AV2_COMMON *const cm,
                                 size_t encoded_frame_size) {
  const int upscaled_width = cm->width;
  const int height = cm->height;
  const int luma_pic_size = upscaled_width * height;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const BITSTREAM_PROFILE profile = seq_params->seq_profile_idc;
#if CONFIG_AV2_PROFILES
  uint32_t chroma_format_idc = CHROMA_FORMAT_420;

  av2_get_chroma_format_idc(cm->seq_params.subsampling_x,
                            cm->seq_params.subsampling_y,
                            cm->seq_params.monochrome, &chroma_format_idc);
  const int profile_scaling_factor =
      get_profile_scaling_factor(profile, chroma_format_idc);
  const int picture_size_profile_factor =
      (int)picture_size_profile_factor_table[profile_scaling_factor];
#else
  const int picture_size_profile_factor =
      profile == PROFILE_0 ? 15 : (profile == PROFILE_1 ? 30 : 36);
#endif  // CONFIG_AV2_PROFILES
  encoded_frame_size =
      (encoded_frame_size > 129 ? encoded_frame_size - 128 : 1);
  const size_t uncompressed_frame_size =
      (luma_pic_size * picture_size_profile_factor) >> 3;
  return uncompressed_frame_size / (double)encoded_frame_size;
}

void av2_update_level_info(AV2_COMP *cpi, size_t size, int64_t ts_start,
                           int64_t ts_end) {
  AV2_COMMON *const cm = &cpi->common;
#if CONFIG_F428_MULTISTREAM
  AV2LevelParams *const level_params = &cpi->level_params;
#else
  const AV2LevelParams *const level_params = &cpi->level_params;
#endif  // CONFIG_F428_MULTISTREAM
  const int upscaled_width = cm->width;
  const int width = cm->width;
  const int height = cm->height;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  const int tiles = tile_cols * tile_rows;
  const int luma_pic_size = upscaled_width * height;
  const int frame_header_count = level_params->frame_header_count;
  const int immediate_output_picture = cm->immediate_output_picture;
  const int show_existing_frame = cm->show_existing_frame;
  int max_tile_size;
#if CONFIG_G018
  int max_tile_width;
#endif  // CONFIG_G018
  int min_cropped_tile_width;
  int min_cropped_tile_height;
  int tile_width_is_valid;
  get_tile_stats(cm, cpi->tile_data, &max_tile_size, &min_cropped_tile_width,
                 &min_cropped_tile_height, &tile_width_is_valid
#if CONFIG_G018
                 ,
                 &max_tile_width
#endif  // CONFIG_G018
  );

  avm_clear_system_state();
  const double compression_ratio = av2_get_compression_ratio(cm, size);

  const int tlayer_id = cm->tlayer_id;
  const int mlayer_id = cm->mlayer_id;
  const int xlayer_id = cm->xlayer_id;
  (void)xlayer_id;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const BITSTREAM_PROFILE profile = seq_params->seq_profile_idc;
  const int is_still_picture = seq_params->still_picture;
  // update level_stats
  // TODO(kyslov@) fix the implementation according to buffer model
  for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; ++i) {
    if (!is_in_operating_point(seq_params->operating_point_idc[i], tlayer_id,
                               mlayer_id) ||
        !((level_params->keep_level_stats >> i) & 1)) {
      continue;
    }

    AV2LevelInfo *const level_info = level_params->level_info[i];
    assert(level_info != NULL);
    AV2LevelStats *const level_stats = &level_info->level_stats;
#if CONFIG_F428_MULTISTREAM
    // update the multitream scaling factor.
    level_params->multi_stream_scaling_x = 0;
#endif  //  CONFIG_F428_MULTISTREAM
    level_stats->max_tile_size =
        AVMMAX(level_stats->max_tile_size, max_tile_size);
#if CONFIG_G018
    level_stats->max_tile_width =
        AVMMAX(level_stats->max_tile_width, max_tile_width);
#endif  // CONFIG_G018
    level_stats->min_cropped_tile_width =
        AVMMIN(level_stats->min_cropped_tile_width, min_cropped_tile_width);
    level_stats->min_cropped_tile_height =
        AVMMIN(level_stats->min_cropped_tile_height, min_cropped_tile_height);
    level_stats->tile_width_is_valid &= tile_width_is_valid;
    level_stats->min_frame_width = AVMMIN(level_stats->min_frame_width, width);
    level_stats->min_frame_height =
        AVMMIN(level_stats->min_frame_height, height);
    level_stats->min_cr = AVMMIN(level_stats->min_cr, compression_ratio);
    level_stats->total_compressed_size += (double)size;

    // update level_spec
    // TODO(kyslov@) update all spec fields
    AV2LevelSpec *const level_spec = &level_info->level_spec;
    level_spec->max_picture_size =
        AVMMAX(level_spec->max_picture_size, luma_pic_size);
    level_spec->max_h_size = AVMMAX(level_spec->max_h_size, cm->width);
    level_spec->max_v_size = AVMMAX(level_spec->max_v_size, height);
    level_spec->max_tile_cols = AVMMAX(level_spec->max_tile_cols, tile_cols);
    level_spec->max_tiles = AVMMAX(level_spec->max_tiles, tiles);

    // Store info. of current frame into FrameWindowBuffer.
    FrameWindowBuffer *const buffer = &level_info->frame_window_buffer;
    store_frame_record(ts_start, ts_end, size, luma_pic_size,
                       frame_header_count, tiles, immediate_output_picture,
                       show_existing_frame, buffer);
    if (immediate_output_picture) {
      // Count the number of frames encoded in the past 1 second.
      const int encoded_frames_in_last_second =
          immediate_output_picture ? count_frames(buffer, TICKS_PER_SEC) : 0;
      scan_past_frames(buffer, encoded_frames_in_last_second, level_spec,
                       level_stats);
      level_stats->total_time_encoded +=
          (cpi->time_stamps.prev_end_seen - cpi->time_stamps.prev_start_seen) /
          (double)TICKS_PER_SEC;
    }

    DECODER_MODEL *const decoder_models = level_info->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      av2_decoder_model_process_frame(cpi, size << 3, &decoder_models[level],
                                      level_spec);
    }

    // Check whether target level is met.
    const AV2_LEVEL target_level = level_params->target_seq_level_idx[i];
    if (target_level < SEQ_LEVELS) {
      assert(is_valid_seq_level_idx(target_level));
      const int tier = cpi->tier[i];
      const TARGET_LEVEL_FAIL_ID fail_id = check_level_constraints(
#if CONFIG_F428_MULTISTREAM
          cpi,
#endif  //  CONFIG_F428_MULTISTREAM
          level_info, target_level, tier, is_still_picture, profile, 0
#if CONFIG_AV2_PROFILES
          ,
          cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
          cm->seq_params.monochrome
#endif  // CONFIG_AV2_PROFILES
      );
      if (fail_id != TARGET_LEVEL_OK) {
        avm_internal_error(&cm->error, AVM_CODEC_ERROR,
                           "Failed to encode to the target level %s. %s",
                           level_string[target_level],
                           level_fail_messages[fail_id]);
      }
    }
  }
}

avm_codec_err_t av2_get_seq_level_idx(const AV2_COMP *cpi,
                                      const SequenceHeader *seq_params,

                                      const AV2LevelParams *level_params,
                                      int *seq_level_idx) {
  const int is_still_picture = seq_params->still_picture;
  const BITSTREAM_PROFILE profile = seq_params->seq_profile_idc;
  for (int op = 0; op < seq_params->operating_points_cnt_minus_1 + 1; ++op) {
    seq_level_idx[op] = (int)SEQ_LEVEL_MAX;
    if (!((level_params->keep_level_stats >> op) & 1)) continue;
    const int tier = cpi->tier[op];
    const AV2LevelInfo *const level_info = level_params->level_info[op];
    assert(level_info != NULL);
    for (int level = 0; level < SEQ_LEVELS; ++level) {
      if (!is_valid_seq_level_idx(level)) continue;
      const TARGET_LEVEL_FAIL_ID fail_id = check_level_constraints(
#if CONFIG_F428_MULTISTREAM
          cpi,
#endif
          level_info, level, tier, is_still_picture, profile, 1
#if CONFIG_AV2_PROFILES
          ,
          seq_params->subsampling_x, seq_params->subsampling_y,
          seq_params->monochrome
#endif  // CONFIG_AV2_PROFILES
      );
      if (fail_id == TARGET_LEVEL_OK) {
        seq_level_idx[op] = level;
        break;
      }
    }
  }

  return AVM_CODEC_OK;
}
