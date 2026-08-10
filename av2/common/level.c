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

#include <float.h>
#include <inttypes.h>
#include <math.h>

#include "avm/avm_integer.h"
#include "avm_ports/system_state.h"
#include "av2/common/level.h"
#include "av2/common/tile_common.h"
#include "av2/common/timing.h"
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

int av2_get_level_compression_basis(int level_index, int tier,
                                    uint32_t *compression_basis) {
  if (compression_basis == NULL || level_index < 0 ||
      level_index >= SEQ_LEVELS || tier < 0 || tier > 1) {
    return 0;
  }
  const AV2LevelSpec *const level_spec = &av2_level_defs[level_index];
  const double basis = tier == 0 ? level_spec->main_cr : level_spec->high_cr;
  if (basis <= 0 || basis > UINT32_MAX) return 0;
  const uint32_t integer_basis = (uint32_t)basis;
  if ((double)integer_basis != basis) return 0;
  *compression_basis = integer_basis;
  return 1;
}

int av2_get_substream_level_spec(int level_index, uint32_t scale_numerator,
                                 uint32_t scale_denominator,
                                 AV2SubstreamLevelSpec *level_spec) {
  if (level_spec == NULL || level_index < SEQ_LEVEL_4_0 ||
      level_index >= SEQ_LEVELS) {
    return 0;
  }
  int scale_index;
  if (scale_numerator == 3 && scale_denominator == 2) {
    scale_index = 0;
  } else if (scale_numerator == 4 && scale_denominator == 1) {
    scale_index = 1;
  } else if (scale_numerator == 9 && scale_denominator == 1) {
    scale_index = 2;
  } else {
    return 0;
  }
  const int level_group = level_index < SEQ_LEVEL_5_0
                              ? 0
                              : ((level_index - SEQ_LEVEL_5_0) >> 2) + 1;
  const int index = 3 * level_group + scale_index;
  if (index < 0 || index >= (int)(sizeof(av2_substream_level_defs) /
                                  sizeof(av2_substream_level_defs[0]))) {
    return 0;
  }
  *level_spec = av2_substream_level_defs[index];
  return 1;
}

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
  TILE_WIDTH_TOO_LARGE,
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
  DECODER_MODEL_UNAVAILABLE,
  REF_FRAMES_FAIL,
  PRESENTATION_INTERVAL_TOO_SMALL,

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
  "The tile width is too large.",
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
  "The decoder model is unable to verify the target level.",
  "The number of reference frames is invalid.",
  "The presentation interval is too small.",
};

static const char level_string[SEQ_LEVEL_MAX + 1][9] = {
  "2.0",      "2.1",      "3.0",      "3.1",      "4.0",      "4.1",
  "5.0",      "5.1",      "5.2",      "5.3",      "6.0",      "6.1",
  "6.2",      "6.3",      "7.0",      "7.1",      "7.2",      "7.3",
  "8.0",      "8.1",      "8.2",      "8.3",      "reserved", "reserved",
  "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
  "reserved", "31"
};

static bool get_multistream_scale(double multistream_scaling_x,
                                  uint32_t *scale_numerator,
                                  uint32_t *scale_denominator) {
  if (scale_numerator == NULL || scale_denominator == NULL) return false;
  *scale_numerator = 1;
  *scale_denominator = 1;
  if (multistream_scaling_x == 0.0 || multistream_scaling_x == 1.0) {
    // No scaling.
  } else if (multistream_scaling_x == 1.5) {
    *scale_numerator = 3;
    *scale_denominator = 2;
  } else if (multistream_scaling_x == 4.0) {
    *scale_numerator = 4;
  } else if (multistream_scaling_x == 9.0) {
    *scale_numerator = 9;
  } else {
    return false;
  }
  return true;
}

static bool get_max_bitrate_rational(const AV2LevelSpec *const level_spec,
                                     int tier, BITSTREAM_PROFILE profile,
                                     double multistream_scaling_x,
                                     Av2DmRational *bit_rate) {
  if (bit_rate == NULL) return false;
  if (profile == CONFIGURABLE) return false;
  if (level_spec->level < SEQ_LEVEL_4_0 && tier != 0) return false;
  const int64_t base_rate =
      av2_max_level_bitrate(profile, level_spec->level, tier);
  if (base_rate <= 0) return false;
  uint32_t scale_numerator;
  uint32_t scale_denominator;
  if (!get_multistream_scale(multistream_scaling_x, &scale_numerator,
                             &scale_denominator)) {
    return false;
  }
  // Annex A does not define multistream substream limits below level 4.0.
  if (scale_numerator != scale_denominator &&
      level_spec->level < SEQ_LEVEL_4_0) {
    return false;
  }
  Av2DmRational base;
  Av2DmRational scaled;
  return av2_dm_rational_make((uint64_t)base_rate, 1, &base) &&
         av2_dm_rational_multiply_u64(&base, scale_denominator, &scaled) &&
         av2_dm_rational_divide_u64(&scaled, scale_numerator, bit_rate);
}

static long double unsigned_wide_to_long_double(
    const Av2DmUnsignedWide *value) {
  long double result = 0.0L;
  for (int i = 3; i >= 0; --i) {
    result = ldexpl(result, 64) + value->limbs[i];
  }
  return result;
}

static bool rational_to_long_double(const Av2DmRational *value,
                                    long double *result) {
  if (value == NULL || result == NULL || value->negative) return false;
  const long double denominator =
      unsigned_wide_to_long_double(&value->denominator);
  if (!(denominator > 0.0L)) return false;
  *result = unsigned_wide_to_long_double(&value->magnitude) / denominator;
  return isfinite(*result);
}

static bool rational_to_double(const Av2DmRational *value, double *result) {
  long double converted;
  if (result == NULL || !rational_to_long_double(value, &converted) ||
      converted > DBL_MAX) {
    return false;
  }
  *result = (double)converted;
  return isfinite(*result);
}

static double get_max_bitrate(const AV2LevelSpec *const level_spec, int tier,
                              BITSTREAM_PROFILE profile,
                              double multistream_scaling_x) {
  Av2DmRational bit_rate;
  double result;
  return get_max_bitrate_rational(level_spec, tier, profile,
                                  multistream_scaling_x, &bit_rate) &&
                 rational_to_double(&bit_rate, &result)
             ? result
             : 0.0;
}

double av2_get_max_bitrate_for_level(AV2_LEVEL level_index, int tier,
                                     BITSTREAM_PROFILE profile,
                                     double multi_stream_scaling_x) {
  assert(is_valid_seq_level_idx(level_index));
  return get_max_bitrate(&av2_level_defs[level_index], tier, profile,
                         multi_stream_scaling_x);
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

bool av2_encoder_decoder_model_reserve_dfg_intervals(
    DECODER_MODEL *decoder_model, size_t interval_count) {
  if (decoder_model == NULL) return false;
  DFG_INTERVAL_QUEUE *const queue = &decoder_model->dfg_interval_queue;
  if (queue->size > queue->capacity ||
      (queue->size > 0 && (queue->buf == NULL || queue->capacity == 0 ||
                           queue->head >= queue->capacity))) {
    return false;
  }
  if (interval_count <= queue->capacity) return true;
  if (interval_count > SIZE_MAX / sizeof(*queue->buf)) return false;

  size_t new_capacity = queue->capacity == 0 ? 64 : queue->capacity;
  while (new_capacity < interval_count) {
    if (new_capacity > SIZE_MAX / 2) {
      new_capacity = interval_count;
      break;
    }
    new_capacity *= 2;
  }
  if (new_capacity > SIZE_MAX / sizeof(*queue->buf)) return false;

  DFG_INTERVAL *const replacement =
      (DFG_INTERVAL *)avm_malloc(new_capacity * sizeof(*replacement));
  if (replacement == NULL) return false;
  for (size_t i = 0; i < queue->size; ++i) {
    replacement[i] = queue->buf[(queue->head + i) % queue->capacity];
  }
  avm_free(queue->buf);
  queue->buf = replacement;
  queue->head = 0;
  queue->capacity = new_capacity;
  return true;
}

bool av2_encoder_decoder_model_push_dfg_interval(DECODER_MODEL *decoder_model,
                                                 const DFG_INTERVAL *interval) {
  if (decoder_model == NULL || interval == NULL) return false;
  DFG_INTERVAL_QUEUE *const queue = &decoder_model->dfg_interval_queue;
  const double duration =
      interval->last_bit_arrival_time - interval->first_bit_arrival_time;
  if (!isfinite(interval->first_bit_arrival_time) ||
      !isfinite(interval->last_bit_arrival_time) ||
      !isfinite(interval->removal_time) || !isfinite(duration) ||
      duration < 0.0 || !isfinite(queue->total_interval) ||
      duration > DBL_MAX - queue->total_interval ||
      interval->coded_bits > UINT64_MAX - queue->total_bits) {
    return false;
  }
  if (queue->size == SIZE_MAX ||
      !av2_encoder_decoder_model_reserve_dfg_intervals(decoder_model,
                                                       queue->size + 1)) {
    return false;
  }
  const size_t queue_index = (queue->head + queue->size) % queue->capacity;
  queue->buf[queue_index] = *interval;
  ++queue->size;
  queue->total_interval += duration;
  queue->total_bits += interval->coded_bits;
  return true;
}

bool av2_encoder_decoder_model_smoothing_buffer_fits(
    const DECODER_MODEL *decoder_model, uint64_t coded_bits, bool *fits) {
  if (decoder_model == NULL || fits == NULL) return false;
  Av2DmRational fullness;
  int comparison;
  if (!av2_dm_rational_make(coded_bits, 1, &fullness) ||
      !av2_dm_rational_compare(&fullness, &decoder_model->buffer_size,
                               &comparison)) {
    return false;
  }
  *fits = comparison <= 0;
  return true;
}

bool av2_encoder_decoder_model_arrival_fits(const DECODER_MODEL *decoder_model,
                                            uint64_t coded_bits,
                                            double available_duration,
                                            bool *fits) {
  if (decoder_model == NULL || fits == NULL || !isfinite(available_duration)) {
    return false;
  }
  long double bit_rate;
  if (!rational_to_long_double(&decoder_model->bit_rate, &bit_rate) ||
      !(bit_rate > 0.0L)) {
    return false;
  }
  const long double available_bits = (long double)available_duration * bit_rate;
  if (!isfinite(available_bits)) return false;
  *fits = available_bits >= 0.0L && coded_bits <= available_bits;
  return true;
}

static bool smoothing_buffer_fits_with_partial_arrival(
    const DECODER_MODEL *decoder_model, uint64_t queued_bits,
    uint64_t current_dfg_bits, double partial_arrival_duration, bool *fits) {
  if (decoder_model == NULL || fits == NULL ||
      !isfinite(partial_arrival_duration)) {
    return false;
  }
  long double bit_rate;
  long double buffer_size;
  if (!rational_to_long_double(&decoder_model->bit_rate, &bit_rate) ||
      !rational_to_long_double(&decoder_model->buffer_size, &buffer_size) ||
      !(bit_rate > 0.0L)) {
    return false;
  }
  const long double duration =
      partial_arrival_duration > 0.0 ? partial_arrival_duration : 0.0L;
  long double partial_bits = duration * bit_rate;
  if (!isfinite(partial_bits)) return false;
  if (partial_bits > current_dfg_bits) partial_bits = current_dfg_bits;
  const long double fullness = queued_bits + partial_bits;
  if (!isfinite(fullness)) return false;
  *fits = fullness <= buffer_size;
  return true;
}

bool av2_encoder_decoder_model_count_obu_bytes(
    const uint8_t *data, size_t data_size, uint64_t *dfg_bytes,
    uint64_t *frame_compressed_bytes) {
  if (dfg_bytes == NULL || frame_compressed_bytes == NULL ||
      (data == NULL && data_size != 0)) {
    return false;
  }
  uint64_t compressed_bytes = 0;
  size_t offset = 0;
  while (offset < data_size) {
    const size_t remaining = data_size - offset;
    const size_t header_size = (data[offset] & 0x80) != 0 ? 2 : 1;
    if (remaining <= header_size) return false;
    const OBU_TYPE type = (OBU_TYPE)((data[offset] >> 2) & 0x1f);
    uint64_t payload_size;
    size_t length_field_size;
    if (avm_uleb_decode(data + offset + header_size, remaining - header_size,
                        &payload_size, &length_field_size) != 0 ||
        payload_size > SIZE_MAX - header_size - length_field_size) {
      return false;
    }
    const size_t obu_size =
        header_size + length_field_size + (size_t)payload_size;
    if (obu_size > remaining) return false;
    if (av2_obu_counts_toward_compressed_size(type)) {
      if ((uint64_t)obu_size > UINT64_MAX - compressed_bytes) return false;
      compressed_bytes += (uint64_t)obu_size;
    }
    offset += obu_size;
  }
  *dfg_bytes = (uint64_t)data_size;
  *frame_compressed_bytes = compressed_bytes;
  return true;
}

bool av2_encoder_decoder_model_accumulate_dfg_bits(DECODER_MODEL *decoder_model,
                                                   uint64_t frame_unit_bits,
                                                   bool closes_dfg,
                                                   uint64_t *closed_dfg_bits) {
  if (decoder_model == NULL || closed_dfg_bits == NULL ||
      frame_unit_bits > UINT64_MAX - decoder_model->coded_bits) {
    return false;
  }
  decoder_model->coded_bits += frame_unit_bits;
  *closed_dfg_bits = 0;
  if (closes_dfg) {
    *closed_dfg_bits = decoder_model->coded_bits;
    decoder_model->coded_bits = 0;
  }
  return true;
}

bool av2_encoder_decoder_model_get_compressed_size(
    uint64_t frame_compressed_bytes, int64_t *compressed_size) {
  if (compressed_size == NULL ||
      frame_compressed_bytes > (uint64_t)INT64_MAX + 128) {
    return false;
  }
  if (frame_compressed_bytes >= 128) {
    *compressed_size = (int64_t)(frame_compressed_bytes - 128);
  } else {
    *compressed_size = -(int64_t)(128 - frame_compressed_bytes);
  }
  return true;
}

void av2_encoder_decoder_model_destroy(DECODER_MODEL *decoder_model) {
  if (decoder_model == NULL) return;
  avm_free(decoder_model->dfg_interval_queue.buf);
  decoder_model->dfg_interval_queue.buf = NULL;
  decoder_model->dfg_interval_queue.head = 0;
  decoder_model->dfg_interval_queue.size = 0;
  decoder_model->dfg_interval_queue.capacity = 0;
  decoder_model->dfg_interval_queue.total_interval = 0.0;
  decoder_model->dfg_interval_queue.total_bits = 0;
}

void av2_encoder_decoder_models_destroy(AV2LevelInfo *level_info) {
  if (level_info == NULL) return;
  for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
    av2_encoder_decoder_model_destroy(&level_info->decoder_models[level]);
  }
}

// This corresponds to "free_buffer" in the spec.
static void release_buffer(DECODER_MODEL *const decoder_model, int idx) {
  assert(idx >= 0 && idx < BUFFER_POOL_MAX_SIZE);
  FRAME_BUFFER *const this_buffer = &decoder_model->frame_buffer_pool[idx];
  this_buffer->decoder_ref_count = 0;
  this_buffer->player_ref_count = 0;
  this_buffer->display_index = -1;
  this_buffer->presentation_time = INVALID_TIME;
  memset(&this_buffer->presentation, 0, sizeof(this_buffer->presentation));
}

static void initialize_buffer_pool(DECODER_MODEL *const decoder_model) {
  const int num_ref_frames = decoder_model->num_ref_frames;
  for (int i = 0; i < num_ref_frames + 2; ++i) {
    release_buffer(decoder_model, i);
  }
  for (int i = 0; i < num_ref_frames; ++i) {
    decoder_model->vbi[i] = -1;
  }
}

static int get_free_buffer(DECODER_MODEL *const decoder_model) {
  for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count == 0 &&
        this_buffer->player_ref_count == 0)
      return i;
  }
  return -1;
}

static bool release_decoder_reference(DECODER_MODEL *const decoder_model,
                                      int buffer_index) {
  if (buffer_index < 0 || buffer_index >= decoder_model->num_ref_frames + 2) {
    return false;
  }
  FRAME_BUFFER *const buffer = &decoder_model->frame_buffer_pool[buffer_index];
  if (buffer->decoder_ref_count == 0) return false;
  --buffer->decoder_ref_count;
  if (buffer->decoder_ref_count == 0 && buffer->player_ref_count == 0) {
    release_buffer(decoder_model, buffer_index);
  }
  return true;
}

bool av2_encoder_decoder_model_sync_invalid_ref_buffers(
    const AV2_COMMON *const cm, DECODER_MODEL *const decoder_model) {
  if (cm == NULL || decoder_model == NULL ||
      decoder_model->status != DECODER_MODEL_OK) {
    return false;
  }
  if (decoder_model->num_ref_frames < 1 ||
      decoder_model->num_ref_frames > REF_FRAMES ||
      cm->seq_params.ref_frames != decoder_model->num_ref_frames) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  for (int i = 0; i < decoder_model->num_ref_frames; ++i) {
    if (cm->ref_frame_map[i] == NULL && decoder_model->vbi[i] != -1) {
      if (!release_decoder_reference(decoder_model, decoder_model->vbi[i])) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
        return false;
      }
      decoder_model->vbi[i] = -1;
    }
  }
  return true;
}

static bool capture_presentation_descriptor(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model,
    uint64_t output_luma_samples, int buffer_index, uint64_t generation,
    bool implicit_output_eligible,
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation) {
  if (cpi == NULL || decoder_model == NULL || presentation == NULL ||
      decoder_model->status != DECODER_MODEL_OK) {
    return false;
  }
  const AV2_COMMON *const cm = &cpi->common;
  if (cm->cur_frame == NULL || decoder_model->num_frame < 0) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  const uint64_t layer_count = (uint64_t)cm->seq_params.max_mlayer_id + 1;
  if (layer_count == 0 ||
      cm->current_frame.display_order_hint > UINT64_MAX / layer_count) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  const uint64_t base_output_order =
      layer_count * cm->current_frame.display_order_hint;
  if ((uint64_t)cm->mlayer_id > UINT64_MAX - base_output_order) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }

  memset(presentation, 0, sizeof(*presentation));
  presentation->valid = true;
  presentation->implicit_output_eligible = implicit_output_eligible;
  presentation->restricted = cm->bridge_frame_info.is_bridge_frame
                                 ? cm->cur_frame->is_restricted
                                 : false;
  presentation->leading_frame = cm->is_leading_picture == 1;
  presentation->random_access_point =
      !cm->show_existing_frame &&
      (cm->current_frame.cm_obu_type == OBU_CLOSED_LOOP_KEY ||
       cm->current_frame.cm_obu_type == OBU_OPEN_LOOP_KEY ||
       cm->current_frame.cm_obu_type == OBU_RAS_FRAME);
  presentation->generation = generation;
  presentation->temporal_unit_index = decoder_model->temporal_unit_index;
  presentation->output_order = base_output_order + (uint64_t)cm->mlayer_id;
  presentation->order_hint = cm->current_frame.display_order_hint;
  presentation->output_luma_samples = output_luma_samples;
  presentation->presentation_time_present =
      cm->ci_params_encoder.ci_timing_info_present_flag &&
      !cm->ci_params_encoder.timing_info.equal_elemental_interval;
  presentation->presentation_time_ticks =
      cm->temporal_point_info_metadata.mtpi_frame_presentation_time;
  presentation->rap_epoch = decoder_model->rap_epoch;
  presentation->decode_completion_time = decoder_model->current_time;
  presentation->source_frame_unit_index = decoder_model->num_frame;
  presentation->buffer_index = buffer_index;
  presentation->xlayer_id = cm->xlayer_id;
  presentation->mlayer_id = cm->mlayer_id;
  presentation->temporal_id = cm->tlayer_id;
  return true;
}

bool av2_encoder_decoder_model_capture_current_generation(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model,
    uint64_t output_luma_samples) {
  if (cpi == NULL || decoder_model == NULL ||
      decoder_model->status != DECODER_MODEL_OK || decoder_model->cfbi < 0 ||
      decoder_model->cfbi >= decoder_model->num_ref_frames + 2 ||
      decoder_model->next_generation == UINT64_MAX) {
    if (decoder_model != NULL && decoder_model->status == DECODER_MODEL_OK) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    }
    return false;
  }
  const AV2_COMMON *const cm = &cpi->common;
  const bool random_access_point =
      cm->current_frame.cm_obu_type == OBU_CLOSED_LOOP_KEY ||
      cm->current_frame.cm_obu_type == OBU_OPEN_LOOP_KEY ||
      cm->current_frame.cm_obu_type == OBU_RAS_FRAME;
  if (random_access_point) {
    if (decoder_model->rap_epoch == UINT64_MAX) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return false;
    }
    ++decoder_model->rap_epoch;
  }
  FRAME_BUFFER *const buffer =
      &decoder_model->frame_buffer_pool[decoder_model->cfbi];
  if (!capture_presentation_descriptor(
          cpi, decoder_model, output_luma_samples, decoder_model->cfbi,
          ++decoder_model->next_generation,
          cm->cur_frame->implicit_output_picture != 0, &buffer->presentation)) {
    return false;
  }
  decoder_model->current_presentation = buffer->presentation;
  return true;
}

static bool update_ref_buffer(const AV2_COMMON *const cm,
                              DECODER_MODEL *const decoder_model, int ref_idx) {
  if (decoder_model->cfbi < 0 ||
      decoder_model->cfbi >= decoder_model->num_ref_frames + 2 || ref_idx < 0 ||
      ref_idx >= decoder_model->num_ref_frames) {
    return false;
  }
  const uint32_t ref_flag = 1u << ref_idx;
  if (decoder_model->mirrored_refresh_frame_flags & ref_flag) return true;
  FRAME_BUFFER *const this_buffer =
      &decoder_model->frame_buffer_pool[decoder_model->cfbi];
  const int pre_idx = decoder_model->vbi[ref_idx];
  if (pre_idx != -1 && !release_decoder_reference(decoder_model, pre_idx)) {
    return false;
  }
  if (cm->ref_frame_map[ref_idx] != NULL) {
    decoder_model->vbi[ref_idx] = decoder_model->cfbi;
    if (this_buffer->decoder_ref_count == UINT32_MAX) return false;
    ++this_buffer->decoder_ref_count;
  } else {
    decoder_model->vbi[ref_idx] = -1;
  }
  decoder_model->mirrored_refresh_frame_flags |= ref_flag;
  return true;
}

static bool update_ref_buffers(const AV2_COMMON *const cm,
                               DECODER_MODEL *const decoder_model,
                               int refresh_frame_flags) {
  if (cm->show_existing_frame) return true;
  for (int i = 0; i < decoder_model->num_ref_frames; ++i) {
    if ((refresh_frame_flags & (1 << i)) &&
        !update_ref_buffer(cm, decoder_model, i)) {
      return false;
    }
  }
  return true;
}

void av2_decoder_model_mirror_ref_buffer_for_operating_points(
    const AV2_COMP *const cpi, int ref_idx) {
  if (cpi == NULL || ref_idx < 0 ||
      ref_idx >= cpi->common.seq_params.ref_frames ||
      !cpi->level_params.keep_level_stats || is_stat_generation_stage(cpi) ||
      !((cpi->common.current_frame.refresh_frame_flags >> ref_idx) & 1)) {
    return;
  }
  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  for (int op = 0; op < seq_params->operating_points_cnt_minus_1 + 1; ++op) {
    if (!((cpi->level_params.keep_level_stats >> op) & 1) ||
        cpi->level_params.level_info[op] == NULL ||
        !is_in_operating_point(seq_params->operating_point_idc[op],
                               cm->tlayer_id, cm->mlayer_id)) {
      continue;
    }
    DECODER_MODEL *const decoder_models =
        cpi->level_params.level_info[op]->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      if (decoder_model->status == DECODER_MODEL_OK &&
          !update_ref_buffer(cm, decoder_model, ref_idx)) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      }
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
                                   double max_decode_rate) {
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

  return luma_samples / max_decode_rate;
}

// Release frame buffers that are no longer needed for decode or display.
// It corresponds to "start_decode_at_removal_time" in the spec.
static void release_processed_frames(DECODER_MODEL *const decoder_model,
                                     double removal_time) {
  for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
    FRAME_BUFFER *const this_buffer = &decoder_model->frame_buffer_pool[i];
    if (this_buffer->player_ref_count > 0) {
      // Presentation offsets assigned before the initial delay is known are
      // not yet absolute PresentationTimes and cannot release a buffer.
      if (decoder_model->initial_presentation_delay >= 0.0 &&
          this_buffer->presentation_time >= 0.0 &&
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
  for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count > 0 ||
        this_buffer->player_ref_count > 0) {
      ++frames_in_pool;
    }
  }
  return frames_in_pool;
}

#define MAX_TIME 1e16
static double time_next_buffer_is_free_with_source(
    const DECODER_MODEL *const decoder_model, bool *from_current_time) {
  if (from_current_time != NULL) *from_current_time = false;
  if (decoder_model->num_decoded_frame == 0) {
    return (double)decoder_model->decoder_buffer_delay / 90000.0;
  }

  double buf_free_time = MAX_TIME;
  for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    if (this_buffer->decoder_ref_count == 0) {
      if (this_buffer->player_ref_count == 0) {
        if (from_current_time != NULL) *from_current_time = true;
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

double time_next_buffer_is_free(const DECODER_MODEL *const decoder_model) {
  return time_next_buffer_is_free_with_source(decoder_model, NULL);
}
#undef MAX_TIME

static double get_removal_time(const DECODER_MODEL *const decoder_model,
                               bool *from_current_time) {
  if (decoder_model->mode == SCHEDULE_MODE) {
    assert(0 && "SCHEDULE_MODE IS NOT SUPPORTED YET");
    return INVALID_TIME;
  } else {
    return time_next_buffer_is_free_with_source(decoder_model,
                                                from_current_time);
  }
}

void av2_decoder_model_print_status(const DECODER_MODEL *const decoder_model) {
  printf("\n status %d, num_frame %3" PRId64 ", num_decoded_frame %3" PRId64
         ", num_shown_frame %3" PRId64
         ", current time %6.2f, frames in buffer %2d, "
         "presentation delay %6.2f, total interval %6.2f\n",
         decoder_model->status, decoder_model->num_frame,
         decoder_model->num_decoded_frame, decoder_model->num_shown_frame,
         decoder_model->current_time, frames_in_buffer_pool(decoder_model),
         decoder_model->initial_presentation_delay,
         decoder_model->dfg_interval_queue.total_interval);
  for (int i = 0; i < 10; ++i) {
    const FRAME_BUFFER *const this_buffer =
        &decoder_model->frame_buffer_pool[i];
    printf("buffer %d, decode count %" PRIu32 ", display count %" PRIu32
           ", present time %6.4f\n",
           i, this_buffer->decoder_ref_count, this_buffer->player_ref_count,
           this_buffer->presentation_time);
  }
}

// op_index is the operating point index.
void av2_decoder_model_init(const AV2_COMP *const cpi, AV2_LEVEL level,
                            int op_index, DECODER_MODEL *const decoder_model) {
  avm_clear_system_state();
  memset(decoder_model, 0, sizeof(*decoder_model));
  decoder_model->status = DECODER_MODEL_OK;
  decoder_model->level = level;
  decoder_model->operating_point = op_index;
  decoder_model->tier = cpi->tier[op_index];

  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  decoder_model->is_still_picture = seq_params->still_picture;
  uint32_t scale_numerator;
  uint32_t scale_denominator;
  Av2DmRational bit_rate;
  if (seq_params->seq_profile_idc == CONFIGURABLE ||
      !get_multistream_scale(cpi->level_params.multi_stream_scaling_x,
                             &scale_numerator, &scale_denominator) ||
      !av2_dm_get_level_limits(level, decoder_model->tier,
                               seq_params->seq_profile_idc,
                               &decoder_model->level_limits) ||
      !get_max_bitrate_rational(av2_level_defs + level, decoder_model->tier,
                                seq_params->seq_profile_idc,
                                cpi->level_params.multi_stream_scaling_x,
                                &bit_rate)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  decoder_model->multistream_scale_numerator = scale_numerator;
  decoder_model->multistream_scale_denominator = scale_denominator;
  decoder_model->bit_rate = bit_rate;
  if (!av2_dm_rational_multiply_u64(&decoder_model->level_limits.buffer_size,
                                    scale_denominator,
                                    &decoder_model->buffer_size) ||
      !av2_dm_rational_divide_u64(&decoder_model->buffer_size, scale_numerator,
                                  &decoder_model->buffer_size)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }

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
  decoder_model->max_decode_rate_satisfy = true;
  decoder_model->max_tile_rate_satisfy = true;
  decoder_model->compressed_size_satisfy = true;
  decoder_model->frame_symbol_count_satisfy = true;
  decoder_model->max_display_rate = 0.0;
  decoder_model->num_ref_frames = cm->seq_params.ref_frames;
  if (decoder_model->num_ref_frames < 1 ||
      decoder_model->num_ref_frames > REF_FRAMES) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  decoder_model->num_frames_current_tu = 0;
  decoder_model->min_presentation_interval_satisfy = true;

  decoder_model->num_frame = -1;
  decoder_model->num_decoded_frame = -1;
  decoder_model->num_shown_frame = -1;
  decoder_model->current_time = 0.0;
  decoder_model->last_output_mlayer = -1;
  decoder_model->last_output_xlayer = -1;
  decoder_model->last_display_index = -1;

  initialize_buffer_pool(decoder_model);

  DFG_INTERVAL_QUEUE *const dfg_interval_queue =
      &decoder_model->dfg_interval_queue;
  dfg_interval_queue->total_interval = 0.0;
  dfg_interval_queue->total_bits = 0;
  dfg_interval_queue->head = 0;
  dfg_interval_queue->size = 0;
  dfg_interval_queue->capacity = 0;
  dfg_interval_queue->buf = NULL;

  if (cm->ci_params_encoder.ci_timing_info_present_flag) {
    decoder_model->equal_picture_interval =
        cm->ci_params_encoder.timing_info.equal_elemental_interval != 0;
    decoder_model->num_ticks_per_picture =
        cm->ci_params_encoder.timing_info.num_ticks_per_elemental_duration;
    decoder_model->display_clock_tick =
        (double)cm->ci_params_encoder.timing_info.num_units_in_display_tick /
        cm->ci_params_encoder.timing_info.time_scale;
  } else {
    decoder_model->equal_picture_interval = true;
    decoder_model->num_ticks_per_picture = 1;
    decoder_model->display_clock_tick = 1.0 / cpi->framerate;
  }

  decoder_model->initial_display_delay =
      seq_params->seq_max_display_model_info_present_flag
          ? seq_params->seq_max_initial_display_delay_minus_1 + 1
          : seq_params->ref_frames + 2;
  decoder_model->initial_presentation_delay = INVALID_TIME;
  decoder_model->decode_rate =
      (double)decoder_model->level_limits.max_decode_rate * scale_denominator /
      scale_numerator;
  if (!(decoder_model->decode_rate > 0.0) ||
      !isfinite(decoder_model->decode_rate)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  decoder_model->initialized = true;
}

int av2_get_max_level_ref_frames(const AV2_COMMON *const cm, OBU_TYPE obu_type,
                                 AV2_LEVEL level_index) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int cap = (seq_params->ref_frames != 8) ? 16 : 8;

  const int max_picture_size = av2_level_defs[level_index].max_picture_size;

  const int64_t current_picture_size =
      seq_params->max_frame_width * seq_params->max_frame_height;

  int64_t limit = (int64_t)max_picture_size * 8 / current_picture_size;

  const int decode_count =
      cm->features.allow_global_intrabc && is_filter_enabled_frame(cm) ? 2 : 1;

  if (decode_count == 2 &&
      (obu_type != OBU_CLOSED_LOOP_KEY || seq_params->max_mlayer_id != 0)) {
    limit -= 1;
  }
  const int max_level_ref_frames = (int)AVMMIN(cap, limit);
  return max_level_ref_frames;
}

static bool encoder_dm_rational_less_than_or_equal(
    const Av2DmRational *observed, const Av2DmRational *limit, bool *result) {
  int comparison;
  if (!av2_dm_rational_compare(observed, limit, &comparison)) return false;
  *result = comparison <= 0;
  return true;
}

// In resource mode, a free buffer often makes the next removal time equal to
// Time after decoding the preceding frame. In that case FrameParsingTime is
// exactly LumaSampleCount / MaxDecodeRate. Evaluate all four Annex A limits by
// rational cross-products rather than reconstructing that interval from two
// rounded double removal times.
static bool check_frame_constraints_at_decode_limit(
    DECODER_MODEL *decoder_model, const ENCODER_DECODER_MODEL_FRAME *frame,
    uint64_t frame_parsing_time_decode_luma_samples) {
  const Av2DmLevelLimits *const limits = &decoder_model->level_limits;
  const uint32_t scale_numerator = decoder_model->multistream_scale_numerator;
  const uint32_t scale_denominator =
      decoder_model->multistream_scale_denominator;
  Av2DmRational observed;
  Av2DmRational limit;
  bool satisfies;

  if (frame_parsing_time_decode_luma_samples == 0) return false;
  const long double max_decode_rate = (long double)limits->max_decode_rate *
                                      scale_denominator / scale_numerator;
  const long double observed_decode_rate =
      max_decode_rate * frame->luma_sample_count /
      frame_parsing_time_decode_luma_samples;
  if (!isfinite(max_decode_rate) || !isfinite(observed_decode_rate)) {
    return false;
  }
  decoder_model->max_decode_rate =
      AVMMAX(decoder_model->max_decode_rate, observed_decode_rate);
  decoder_model->max_decode_rate_satisfy &=
      frame->luma_sample_count <= frame_parsing_time_decode_luma_samples;

  Av2DmRational dynamic_tile_limit;
  Av2DmRational max_tile_limit;
  Av2DmRational one;
  if (!av2_dm_rational_make(frame_parsing_time_decode_luma_samples, 1,
                            &dynamic_tile_limit) ||
      !av2_dm_rational_multiply_u64(&dynamic_tile_limit,
                                    (uint64_t)limits->max_tiles * 120,
                                    &dynamic_tile_limit) ||
      !av2_dm_rational_divide_u64(&dynamic_tile_limit, limits->max_decode_rate,
                                  &dynamic_tile_limit) ||
      !av2_dm_rational_make(limits->max_tiles, 1, &max_tile_limit) ||
      !av2_dm_rational_multiply_u64(&max_tile_limit, scale_denominator,
                                    &max_tile_limit) ||
      !av2_dm_rational_divide_u64(&max_tile_limit, scale_numerator,
                                  &max_tile_limit) ||
      !av2_dm_rational_make(1, 1, &one)) {
    return false;
  }
  int comparison;
  if (!av2_dm_rational_compare(&dynamic_tile_limit, &one, &comparison)) {
    return false;
  }
  if (comparison < 0) dynamic_tile_limit = one;
  if (!av2_dm_rational_compare(&dynamic_tile_limit, &max_tile_limit,
                               &comparison)) {
    return false;
  }
  if (comparison > 0) dynamic_tile_limit = max_tile_limit;
  if (!av2_dm_rational_make(frame->num_tiles, 1, &observed) ||
      !encoder_dm_rational_less_than_or_equal(&observed, &dynamic_tile_limit,
                                              &satisfies)) {
    return false;
  }
  decoder_model->max_tile_rate_satisfy &= satisfies;

  if (frame->luma_sample_count >
      UINT64_MAX / limits->picture_size_profile_factor) {
    return false;
  }
  const uint64_t picture_units =
      frame->luma_sample_count * limits->picture_size_profile_factor >> 3;
  Av2DmRational picture_compressed_limit;
  Av2DmRational rate_compressed_limit;
  if (!av2_dm_rational_make(picture_units, 1, &picture_compressed_limit) ||
      !av2_dm_rational_multiply_u64(&picture_compressed_limit, 5,
                                    &picture_compressed_limit) ||
      !av2_dm_rational_divide_u64(&picture_compressed_limit, 4,
                                  &picture_compressed_limit) ||
      !av2_dm_rational_make(frame_parsing_time_decode_luma_samples, 1,
                            &rate_compressed_limit) ||
      !av2_dm_rational_multiply_u64(&rate_compressed_limit,
                                    limits->picture_size_profile_factor,
                                    &rate_compressed_limit) ||
      !av2_dm_rational_divide_u64(&rate_compressed_limit,
                                  (uint64_t)8 * limits->min_compression_basis,
                                  &rate_compressed_limit) ||
      !av2_dm_rational_compare(&picture_compressed_limit,
                               &rate_compressed_limit, &comparison)) {
    return false;
  }
  limit = comparison <= 0 ? picture_compressed_limit : rate_compressed_limit;
  if (frame->compressed_size > 0) {
    if (!av2_dm_rational_make((uint64_t)frame->compressed_size, 1, &observed) ||
        !encoder_dm_rational_less_than_or_equal(&observed, &limit,
                                                &satisfies)) {
      return false;
    }
    decoder_model->compressed_size_satisfy &= satisfies;
  }

  Av2DmRational symbol_factor_a;
  Av2DmRational symbol_factor_b;
  if (!av2_dm_rational_make(8, (uint64_t)9 * limits->min_compression_basis,
                            &symbol_factor_a) ||
      !av2_dm_rational_make(1, 48, &symbol_factor_b) ||
      !av2_dm_rational_add(&symbol_factor_a, &symbol_factor_b, &limit) ||
      !av2_dm_rational_multiply_u64(
          &limit, frame_parsing_time_decode_luma_samples, &limit) ||
      !av2_dm_rational_multiply_u64(&limit, limits->picture_size_profile_factor,
                                    &limit) ||
      !av2_dm_rational_make(frame->frame_symbol_count, 1, &observed) ||
      !encoder_dm_rational_less_than_or_equal(&observed, &limit, &satisfies)) {
    return false;
  }
  decoder_model->frame_symbol_count_satisfy &= satisfies;
  return true;
}

bool av2_encoder_decoder_model_check_frame_constraints(
    DECODER_MODEL *decoder_model, const ENCODER_DECODER_MODEL_FRAME *frame,
    double frame_parsing_time, bool frame_parsing_time_at_decode_limit,
    uint64_t frame_parsing_time_decode_luma_samples) {
  if (decoder_model == NULL || frame == NULL || !frame->valid ||
      decoder_model->status != DECODER_MODEL_OK ||
      !(frame_parsing_time > 0.0) || !isfinite(frame_parsing_time)) {
    if (decoder_model != NULL && decoder_model->status == DECODER_MODEL_OK) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    }
    return false;
  }

  const Av2DmLevelLimits *const limits = &decoder_model->level_limits;
  if (limits->max_decode_rate == 0 || limits->max_tiles == 0 ||
      limits->picture_size_profile_factor == 0 ||
      limits->min_compression_basis == 0) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  if (frame_parsing_time_at_decode_limit) {
    if (!check_frame_constraints_at_decode_limit(
            decoder_model, frame, frame_parsing_time_decode_luma_samples)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return false;
    }
    return true;
  }
  const long double parsing_time = frame_parsing_time;
  const long double scale_numerator =
      decoder_model->multistream_scale_numerator;
  const long double scale_denominator =
      decoder_model->multistream_scale_denominator;
  if (!(scale_numerator > 0.0L) || !(scale_denominator > 0.0L)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  const long double max_decode_rate = (long double)limits->max_decode_rate *
                                      scale_denominator / scale_numerator;
  const long double max_tiles =
      (long double)limits->max_tiles * scale_denominator / scale_numerator;
  const long double observed_decode_rate =
      (long double)frame->luma_sample_count / parsing_time;
  if (!isfinite(max_decode_rate) || !isfinite(max_tiles) ||
      !isfinite(observed_decode_rate)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  decoder_model->max_decode_rate =
      AVMMAX(decoder_model->max_decode_rate, observed_decode_rate);
  decoder_model->max_decode_rate_satisfy &=
      observed_decode_rate <= max_decode_rate;

  const long double dynamic_tile_limit = max_tiles * 120.0L * parsing_time;
  const long double tile_limit =
      AVMMIN(max_tiles, AVMMAX(1.0L, dynamic_tile_limit));
  if (!isfinite(dynamic_tile_limit) || !isfinite(tile_limit)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  decoder_model->max_tile_rate_satisfy &=
      (long double)frame->num_tiles <= tile_limit;

  if (frame->luma_sample_count >
      UINT64_MAX / limits->picture_size_profile_factor) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  const uint64_t picture_units =
      frame->luma_sample_count * limits->picture_size_profile_factor >> 3;
  const long double compressed_limit_from_picture =
      (long double)picture_units * 5.0L / 4.0L;
  const long double compressed_limit_from_rate =
      parsing_time * max_decode_rate * limits->picture_size_profile_factor /
      ((long double)8 * limits->min_compression_basis);
  const long double compressed_limit =
      AVMMIN(compressed_limit_from_picture, compressed_limit_from_rate);
  if (!isfinite(compressed_limit_from_picture) ||
      !isfinite(compressed_limit_from_rate) || !isfinite(compressed_limit)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  decoder_model->compressed_size_satisfy &=
      frame->compressed_size <= 0 ||
      (long double)frame->compressed_size <= compressed_limit;

  const long double symbol_factor =
      8.0L / ((long double)9 * limits->min_compression_basis) + 1.0L / 48.0L;
  const long double symbol_limit = parsing_time * max_decode_rate *
                                   limits->picture_size_profile_factor *
                                   symbol_factor;
  if (!isfinite(symbol_factor) || !isfinite(symbol_limit)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  decoder_model->frame_symbol_count_satisfy &=
      (long double)frame->frame_symbol_count <= symbol_limit;
  return true;
}

bool av2_encoder_decoder_model_store_frame_constraints(
    DECODER_MODEL *decoder_model,
    const ENCODER_DECODER_MODEL_FRAME *current_frame,
    bool previous_frame_parsing_time_at_decode_limit) {
  if (decoder_model == NULL || current_frame == NULL || !current_frame->valid ||
      current_frame->decode_count == 0 ||
      decoder_model->status != DECODER_MODEL_OK ||
      !isfinite(current_frame->removal_time)) {
    if (decoder_model != NULL && decoder_model->status == DECODER_MODEL_OK) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    }
    return false;
  }

  if (decoder_model->pending_frame.valid) {
    const double frame_parsing_time =
        (current_frame->removal_time -
         decoder_model->pending_frame.removal_time) /
        decoder_model->pending_frame.decode_count;
    if (!av2_encoder_decoder_model_check_frame_constraints(
            decoder_model, &decoder_model->pending_frame, frame_parsing_time,
            previous_frame_parsing_time_at_decode_limit,
            decoder_model->pending_frame.luma_sample_count)) {
      return false;
    }
    decoder_model->last_frame_parsing_time = frame_parsing_time;
    decoder_model->last_frame_parsing_time_valid = true;
    decoder_model->last_frame_parsing_time_at_decode_limit =
        previous_frame_parsing_time_at_decode_limit;
    decoder_model->last_frame_parsing_time_decode_luma_samples =
        decoder_model->pending_frame.luma_sample_count;
  }

  decoder_model->pending_frame = *current_frame;
  decoder_model->frame_constraints_finalized = false;
  return true;
}

void av2_encoder_decoder_model_finalize_frame_constraints(
    DECODER_MODEL *decoder_model, bool is_still_picture) {
  if (decoder_model == NULL || !decoder_model->initialized ||
      decoder_model->status != DECODER_MODEL_OK ||
      decoder_model->frame_constraints_finalized) {
    return;
  }
  decoder_model->frame_constraints_finalized = true;
  if (is_still_picture || !decoder_model->pending_frame.valid) return;
  if (!decoder_model->last_frame_parsing_time_valid) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  (void)av2_encoder_decoder_model_check_frame_constraints(
      decoder_model, &decoder_model->pending_frame,
      decoder_model->last_frame_parsing_time,
      decoder_model->last_frame_parsing_time_at_decode_limit,
      decoder_model->last_frame_parsing_time_decode_luma_samples);
}

static bool update_max_display_rate(DECODER_MODEL *decoder_model,
                                    double display_duration) {
  if (decoder_model == NULL || !isfinite(display_duration)) return false;
  if (display_duration <= 0.0) {
    decoder_model->max_display_rate = LDBL_MAX;
    return true;
  }
  const long double display_rate =
      (long double)decoder_model->display_samples / display_duration;
  if (!isfinite(display_rate)) return false;
  decoder_model->max_display_rate =
      AVMMAX(decoder_model->max_display_rate, display_rate);
  return true;
}

static bool get_minimum_presentation_interval(
    const AV2_COMP *const cpi, const DECODER_MODEL *const decoder_model,
    double *const min_interval) {
  if (cpi == NULL || decoder_model == NULL || min_interval == NULL ||
      decoder_model->tier < 0 || decoder_model->tier > 1) {
    return false;
  }

  const SequenceHeader *const seq_params = &cpi->common.seq_params;
  Av2DmLevelLimits limits;
  if (!av2_dm_get_level_limits(decoder_model->level, decoder_model->tier,
                               seq_params->seq_profile_idc, &limits)) {
    return false;
  }
  const uint32_t scale_numerator = decoder_model->multistream_scale_numerator;
  const uint32_t scale_denominator =
      decoder_model->multistream_scale_denominator;
  if (scale_numerator == 0 || scale_denominator == 0 ||
      (scale_numerator != scale_denominator &&
       !av2_dm_apply_multistream_limits(
           decoder_model->level, decoder_model->tier,
           seq_params->seq_profile_idc, scale_numerator, scale_denominator,
           &limits))) {
    return false;
  }

  if (seq_params->max_frame_width <= 0 || seq_params->max_frame_height <= 0 ||
      decoder_model->num_frames_current_tu == 0 ||
      limits.max_display_rate == 0 || limits.max_decode_rate == 0 ||
      limits.max_header_rate == 0) {
    return false;
  }
  const uint64_t max_frame_width = (uint32_t)seq_params->max_frame_width;
  const uint64_t max_frame_height = (uint32_t)seq_params->max_frame_height;
  if (max_frame_width > UINT64_MAX / max_frame_height) return false;
  const uint64_t max_frame_samples = max_frame_width * max_frame_height;
  if (max_frame_samples > UINT64_MAX / decoder_model->num_frames_current_tu) {
    return false;
  }
  const uint64_t output_samples =
      max_frame_samples * decoder_model->num_frames_current_tu;

  const uint64_t tier_multiplier = 1 + ((uint64_t)decoder_model->tier << 1);
  if (limits.max_header_rate > UINT64_MAX / tier_multiplier) return false;
  const uint64_t max_frame_headers_per_second =
      limits.max_header_rate * tier_multiplier;

  const double sample_interval =
      (double)output_samples / (double)limits.max_display_rate;
  const double min_frame_time =
      (double)limits.max_decode_rate /
      ((double)max_frame_headers_per_second * (double)limits.max_display_rate);
  *min_interval = AVMMAX(sample_interval, min_frame_time);
  return isfinite(sample_interval) && isfinite(min_frame_time) &&
         isfinite(*min_interval);
}

void av2_encoder_decoder_model_finalize(DECODER_MODEL *decoder_model,
                                        bool is_still_picture) {
  if (decoder_model == NULL || !decoder_model->initialized ||
      decoder_model->finalized) {
    return;
  }
  decoder_model->finalized = true;
  if (decoder_model->status != DECODER_MODEL_OK) return;
  av2_encoder_decoder_model_finalize_frame_constraints(decoder_model,
                                                       is_still_picture);
  if (decoder_model->status != DECODER_MODEL_OK || is_still_picture ||
      decoder_model->display_samples == 0) {
    return;
  }
  if (!decoder_model->last_display_duration_valid) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  if (!update_max_display_rate(decoder_model,
                               decoder_model->last_display_duration)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
  }
}

static void av2_decoder_model_start_frame_decode(
    const AV2_COMP *const cpi, uint64_t dfg_bits, int64_t compressed_size,
    DECODER_MODEL *const decoder_model) {
  if (!decoder_model || decoder_model->status != DECODER_MODEL_OK) return;

  avm_clear_system_state();

  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  if (!av2_encoder_decoder_model_sync_invalid_ref_buffers(cm, decoder_model)) {
    return;
  }
  if (cpi->dm_starts_temporal_unit) {
    if (decoder_model->temporal_unit_started) {
      if (decoder_model->temporal_unit_index == UINT64_MAX) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
        return;
      }
      ++decoder_model->temporal_unit_index;
    }
    decoder_model->temporal_unit_started = true;
  } else if (!decoder_model->temporal_unit_started) {
    decoder_model->temporal_unit_started = true;
  }
  if (cm->current_frame.cm_obu_type == OBU_OPEN_LOOP_KEY) {
    decoder_model->olk_encountered = true;
  }
  if (decoder_model->olk_encountered && cpi->olk_encountered &&
      (cm->immediate_output_picture || cm->implicit_output_picture)) {
    decoder_model->olk_tu_order_hint = cm->current_frame.display_order_hint;
    decoder_model->olk_tu_order_hint_valid = true;
  }

  uint64_t luma_pic_size;

  const FRAME_TYPE frame_type = cm->current_frame.frame_type;

  if (frame_type == KEY_FRAME || frame_type == INTRA_ONLY_FRAME) {
    luma_pic_size = (uint64_t)cm->width * (uint32_t)cm->height;
  } else {
    luma_pic_size = (uint64_t)seq_params->max_frame_width *
                    (uint32_t)seq_params->max_frame_height;
  }

  const int show_existing_frame = cm->show_existing_frame;
  if (decoder_model->num_frame == INT64_MAX ||
      (!show_existing_frame && decoder_model->num_decoded_frame == INT64_MAX)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  ++decoder_model->num_frame;
  if (!show_existing_frame) ++decoder_model->num_decoded_frame;  // DfgNum
  uint64_t closed_dfg_bits;
  if (!av2_encoder_decoder_model_accumulate_dfg_bits(
          decoder_model, dfg_bits, !show_existing_frame, &closed_dfg_bits)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }

  if (!show_existing_frame) {
    decoder_model->mirrored_refresh_frame_flags = 0;
    bool removal_from_current_time;
    const double removal_time =
        get_removal_time(decoder_model, &removal_from_current_time);
    if (removal_time < 0.0) {
      decoder_model->status = DECODE_FRAME_BUF_UNAVAILABLE;
      return;
    }

    decoder_model->removal_time = removal_time;
    decoder_model->decode_samples = luma_pic_size;
    const uint64_t num_tiles = (uint64_t)cm->tiles.rows * cm->tiles.cols;
    if (num_tiles > UINT32_MAX) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    const ENCODER_DECODER_MODEL_FRAME current_frame = {
      true,
      removal_time,
      luma_pic_size,
      cm->features.allow_global_intrabc && is_filter_enabled_frame(cm) ? 2 : 1,
      (uint32_t)num_tiles,
      compressed_size,
      cm->features.frame_symbol_count,
    };
    if (!av2_encoder_decoder_model_store_frame_constraints(
            decoder_model, &current_frame, removal_from_current_time)) {
      return;
    }
    // A frame with show_existing_frame being false indicates the end of a DFG.
    // Update the bits arrival time of this DFG.
    const double buffer_delay = (decoder_model->encoder_buffer_delay +
                                 decoder_model->decoder_buffer_delay) /
                                90000.0;
    const double latest_arrival_time = removal_time - buffer_delay;
    decoder_model->first_bit_arrival_time =
        AVMMAX(decoder_model->last_bit_arrival_time, latest_arrival_time);
    double bit_rate;
    if (!rational_to_double(&decoder_model->bit_rate, &bit_rate) ||
        !(bit_rate > 0.0)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    decoder_model->last_bit_arrival_time =
        decoder_model->first_bit_arrival_time +
        (double)closed_dfg_bits / bit_rate;
    // Smoothing buffer underflows if the last bit arrives after the removal
    // time.
    bool dfg_available_at_removal;
    if (!av2_encoder_decoder_model_arrival_fits(
            decoder_model, closed_dfg_bits,
            removal_time - decoder_model->first_bit_arrival_time,
            &dfg_available_at_removal)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    if (!dfg_available_at_removal && !decoder_model->is_low_delay_mode) {
      decoder_model->status = SMOOTHING_BUFFER_UNDERFLOW;
      return;
    }
    // Check if the smoothing buffer overflows.
    DFG_INTERVAL_QUEUE *const queue = &decoder_model->dfg_interval_queue;
    const double first_bit_arrival_time = decoder_model->first_bit_arrival_time;
    const double last_bit_arrival_time = decoder_model->last_bit_arrival_time;
    // Remove the DFGs with removal time earlier than last_bit_arrival_time.
    while (queue->size > 0 &&
           queue->buf[queue->head].removal_time <= last_bit_arrival_time) {
      bool smoothing_buffer_fits;
      if (!smoothing_buffer_fits_with_partial_arrival(
              decoder_model, queue->total_bits, closed_dfg_bits,
              queue->buf[queue->head].removal_time - first_bit_arrival_time,
              &smoothing_buffer_fits)) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
        return;
      }
      if (!smoothing_buffer_fits) {
        decoder_model->status = SMOOTHING_BUFFER_OVERFLOW;
        return;
      }
      if (queue->buf[queue->head].coded_bits > queue->total_bits) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
        return;
      }
      queue->total_interval -= queue->buf[queue->head].last_bit_arrival_time -
                               queue->buf[queue->head].first_bit_arrival_time;
      queue->total_bits -= queue->buf[queue->head].coded_bits;
      queue->head = (queue->head + 1) % queue->capacity;
      --queue->size;
    }
    // Push current DFG into the queue.
    const DFG_INTERVAL interval = { first_bit_arrival_time,
                                    last_bit_arrival_time, removal_time,
                                    closed_dfg_bits };
    if (!av2_encoder_decoder_model_push_dfg_interval(decoder_model,
                                                     &interval)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    bool smoothing_buffer_fits;
    if (!av2_encoder_decoder_model_smoothing_buffer_fits(
            decoder_model, queue->total_bits, &smoothing_buffer_fits)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    if (!smoothing_buffer_fits) {
      decoder_model->status = SMOOTHING_BUFFER_OVERFLOW;
      return;
    }

    release_processed_frames(decoder_model, removal_time);

    decoder_model->cfbi = get_free_buffer(decoder_model);
    if (decoder_model->cfbi < 0) {
      decoder_model->status = DECODE_FRAME_BUF_UNAVAILABLE;
      return;
    }
    decoder_model->current_time =
        removal_time + time_to_decode_frame(cm, decoder_model->decode_rate);
    if (!av2_encoder_decoder_model_capture_current_generation(
            cpi, decoder_model, luma_pic_size)) {
      return;
    }
  } else if (!capture_presentation_descriptor(
                 cpi, decoder_model, luma_pic_size, -1, 0, false,
                 &decoder_model->current_presentation)) {
    return;
  }
}

static void av2_decoder_model_update_buffer_and_finish_frame_decode(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model) {
  if (decoder_model == NULL || decoder_model->status != DECODER_MODEL_OK) {
    return;
  }
  const AV2_COMMON *const cm = &cpi->common;

  const int show_existing_frame = cm->show_existing_frame;

  if (!show_existing_frame) {
    const CurrentFrame *const current_frame = &cm->current_frame;
    decoder_model->frame_buffer_pool[decoder_model->cfbi].frame_type =
        cm->current_frame.frame_type;
    if (!update_ref_buffers(cm, decoder_model,
                            current_frame->refresh_frame_flags)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }

    if (decoder_model->initial_presentation_delay < 0.0) {
      // Display can begin after required number of frames have been buffered.
      if (frames_in_buffer_pool(decoder_model) >=
          decoder_model->initial_display_delay) {
        decoder_model->initial_presentation_delay = decoder_model->current_time;
        if (decoder_model->presentation_time >= 0.0)
          decoder_model->presentation_time +=
              decoder_model->initial_presentation_delay;
        // Update presentation time for each shown frame in the frame buffer.
        for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
          FRAME_BUFFER *const this_buffer =
              &decoder_model->frame_buffer_pool[i];
          if (this_buffer->player_ref_count == 0) continue;
          assert(this_buffer->display_index >= 0);
          if (this_buffer->presentation_time >= 0.0) {
            this_buffer->presentation_time +=
                decoder_model->initial_presentation_delay;
          }
        }
      }
    }
  }
}
void av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
    const AV2_COMP *const cpi) {
  const AV2_COMMON *const cm = &cpi->common;
  const AV2LevelParams *const level_params = &cpi->level_params;
  const int tlayer_id = cm->tlayer_id;
  const int mlayer_id = cm->mlayer_id;
  const SequenceHeader *const seq_params = &cm->seq_params;
  // update level_stats
  // TODO(kyslov@) fix the implementation according to buffer model
  for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; ++i) {
    if (!is_in_operating_point(seq_params->operating_point_idc[i], tlayer_id,
                               mlayer_id) ||
        !((level_params->keep_level_stats >> i) & 1)) {
      continue;
    }
    AV2LevelInfo *const level_info = level_params->level_info[i];
    DECODER_MODEL *const decoder_models = level_info->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      av2_decoder_model_update_buffer_and_finish_frame_decode(
          cpi, &decoder_models[level]);
    }
  }
}

static bool decoder_model_get_eligible_output(
    DECODER_MODEL *const decoder_model, int ref_idx, int *const buffer_index) {
  if (ref_idx < 0 || ref_idx >= decoder_model->num_ref_frames) return false;
  const int index = decoder_model->vbi[ref_idx];
  if (index == -1) return false;
  if (index < 0 || index >= decoder_model->num_ref_frames + 2) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return false;
  }
  ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
      &decoder_model->frame_buffer_pool[index].presentation;
  if (!presentation->valid || presentation->buffer_index != index) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return false;
  }
  if (!presentation->implicit_output_eligible ||
      presentation->normative_output_done || presentation->restricted) {
    return false;
  }
  if (buffer_index != NULL) *buffer_index = index;
  return true;
}

static const ENCODER_DM_RAP_PRESENTATION_ANCHOR *find_rap_anchor(
    const DECODER_MODEL *const decoder_model, uint64_t rap_epoch) {
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE + 2; ++i) {
    const ENCODER_DM_RAP_PRESENTATION_ANCHOR *const anchor =
        &decoder_model->rap_presentation_anchors[i];
    if (anchor->valid && anchor->rap_epoch == rap_epoch) return anchor;
  }
  return NULL;
}

static bool rap_epoch_is_live(const DECODER_MODEL *const decoder_model,
                              uint64_t rap_epoch) {
  for (int i = 0; i < decoder_model->num_ref_frames + 2; ++i) {
    const ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
        &decoder_model->frame_buffer_pool[i].presentation;
    if (presentation->valid && presentation->rap_epoch == rap_epoch) {
      return true;
    }
  }
  return decoder_model->current_presentation.valid &&
         decoder_model->current_presentation.rap_epoch == rap_epoch;
}

static bool store_rap_anchor(DECODER_MODEL *const decoder_model,
                             uint64_t rap_epoch, double presentation_offset) {
  ENCODER_DM_RAP_PRESENTATION_ANCHOR *free_anchor = NULL;
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE + 2; ++i) {
    ENCODER_DM_RAP_PRESENTATION_ANCHOR *const anchor =
        &decoder_model->rap_presentation_anchors[i];
    if (anchor->valid && anchor->rap_epoch == rap_epoch) {
      anchor->presentation_offset = presentation_offset;
      return true;
    }
    if ((!anchor->valid ||
         !rap_epoch_is_live(decoder_model, anchor->rap_epoch)) &&
        free_anchor == NULL) {
      free_anchor = anchor;
    }
  }
  if (free_anchor == NULL) return false;
  free_anchor->valid = true;
  free_anchor->rap_epoch = rap_epoch;
  free_anchor->presentation_offset = presentation_offset;
  return true;
}

static bool get_presentation_offset(
    DECODER_MODEL *const decoder_model,
    const ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation,
    double *const offset) {
  if (decoder_model->num_shown_frame < 0) {
    *offset = 0.0;
    return true;
  }
  if (decoder_model->equal_picture_interval) {
    if (!decoder_model->last_presentation_offset_valid) return false;
    if (decoder_model->last_output_temporal_unit_valid &&
        decoder_model->last_output_temporal_unit ==
            presentation->temporal_unit_index) {
      *offset = decoder_model->last_presentation_offset;
      return true;
    }
    *offset = decoder_model->last_presentation_offset +
              decoder_model->num_ticks_per_picture *
                  decoder_model->display_clock_tick;
    return isfinite(*offset);
  }
  if (!presentation->presentation_time_present) return false;
  uint64_t base_epoch = presentation->rap_epoch;
  if (presentation->random_access_point || presentation->leading_frame) {
    if (base_epoch == 0) return false;
    --base_epoch;
  }
  double base = 0.0;
  if (base_epoch != 0) {
    const ENCODER_DM_RAP_PRESENTATION_ANCHOR *const anchor =
        find_rap_anchor(decoder_model, base_epoch);
    if (anchor == NULL) return false;
    base = anchor->presentation_offset;
  }
  *offset = base + presentation->presentation_time_ticks *
                       decoder_model->display_clock_tick;
  return isfinite(*offset);
}

static void av2_decoder_model_check_output_frame(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model,
    int buffer_index,
    const ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation,
    bool completes_implicit_output) {
  if (decoder_model == NULL || decoder_model->status != DECODER_MODEL_OK) {
    return;
  }
  if (buffer_index < 0 || buffer_index >= decoder_model->num_ref_frames + 2 ||
      presentation == NULL || !presentation->valid ||
      presentation->output_luma_samples == 0) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  FRAME_BUFFER *const this_buffer =
      &decoder_model->frame_buffer_pool[buffer_index];
  ENCODER_DM_PRESENTATION_DESCRIPTOR *const generation =
      &this_buffer->presentation;
  if (!generation->valid || generation->buffer_index != buffer_index) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  double presentation_offset;
  if (!get_presentation_offset(decoder_model, presentation,
                               &presentation_offset)) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  if (completes_implicit_output) {
    generation->normative_output_done = true;
  }
  if (this_buffer->player_ref_count == UINT32_MAX) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  ++this_buffer->player_ref_count;
  if (decoder_model->num_shown_frame == INT64_MAX) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  decoder_model->num_shown_frame++;
  if (decoder_model->last_output_xlayer != -1 &&
      decoder_model->last_output_xlayer != presentation->xlayer_id) {
    decoder_model->status = DECODER_MODEL_MULTIPLE_XLAYERS;
    return;
  }
  if (decoder_model->last_display_index == INT_MAX) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  ++decoder_model->last_display_index;
  decoder_model->last_output_mlayer = presentation->mlayer_id;
  decoder_model->last_output_xlayer = presentation->xlayer_id;
  const bool starts_new_output_temporal_unit =
      decoder_model->last_output_temporal_unit_valid &&
      presentation->temporal_unit_index !=
          decoder_model->last_output_temporal_unit;
  const uint64_t output_rap_epoch =
      presentation->leading_frame && presentation->rap_epoch != 0
          ? presentation->rap_epoch - 1
          : presentation->rap_epoch;
  decoder_model->last_output_temporal_unit = presentation->temporal_unit_index;
  decoder_model->last_output_temporal_unit_valid = true;

  this_buffer->display_index = decoder_model->last_display_index;

  double presentation_time = presentation_offset;
  if (decoder_model->initial_presentation_delay >= 0.0) {
    presentation_time += decoder_model->initial_presentation_delay;
  }
  if (!isfinite(presentation_time)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  this_buffer->presentation_time = presentation_time;
  if (decoder_model->initial_presentation_delay >= 0.0) {
    if (presentation_time >= 0.0 &&
        decoder_model->current_time > presentation_time) {
      decoder_model->status = DISPLAY_FRAME_LATE;
      return;
    }
    if (generation->decode_completion_time > presentation_time) {
      decoder_model->status = DISPLAY_FRAME_LATE;
      return;
    }
  }

  const double previous_presentation_time = decoder_model->presentation_time;
  if (decoder_model->last_presentation_offset_valid &&
      decoder_model->previous_output_rap_epoch_valid &&
      decoder_model->previous_output_rap_epoch == output_rap_epoch &&
      presentation_offset < decoder_model->last_presentation_offset) {
    decoder_model->min_presentation_interval_satisfy = false;
  }
  if (starts_new_output_temporal_unit && presentation_time >= 0.0 &&
      previous_presentation_time >= 0.0) {
    // A new temporal unit has started.  Compute metrics over the inter-TU
    // interval using the previous TU's accumulated display samples.
    const double interval = presentation_time - previous_presentation_time;

    // Peak display rate across TU boundaries (Annex A).
    if (!update_max_display_rate(decoder_model, interval)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    decoder_model->last_display_duration = interval;
    decoder_model->last_display_duration_valid = true;
    if (interval <= 0.0)
      decoder_model->min_presentation_interval_satisfy = false;
    double min_interval;
    if (!get_minimum_presentation_interval(cpi, decoder_model, &min_interval)) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    decoder_model->min_presentation_interval_satisfy =
        decoder_model->min_presentation_interval_satisfy &&
        (interval >= min_interval);

    // Reset per-TU accumulators for the new temporal unit.
    decoder_model->display_samples = 0;
    decoder_model->num_frames_current_tu = 0;
  }
  // Accumulate this frame's samples into the current temporal unit.
  if (presentation->output_luma_samples >
      UINT64_MAX - decoder_model->display_samples) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  decoder_model->display_samples += presentation->output_luma_samples;
  if (decoder_model->num_frames_current_tu == UINT64_MAX) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
    return;
  }
  ++decoder_model->num_frames_current_tu;
  if (previous_presentation_time < 0.0 || starts_new_output_temporal_unit) {
    decoder_model->presentation_time = presentation_time;
  }
  decoder_model->last_presentation_offset = presentation_offset;
  decoder_model->last_presentation_offset_valid = true;
  decoder_model->previous_output_rap_epoch = output_rap_epoch;
  decoder_model->previous_output_rap_epoch_valid = true;
  if (presentation->random_access_point &&
      !store_rap_anchor(decoder_model, presentation->rap_epoch,
                        presentation_offset)) {
    decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
  }
}

static void decoder_model_observe_output_frame_buffers(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model,
    int trigger_ref_idx, int trigger_buffer_index,
    const ENCODER_DM_PRESENTATION_DESCRIPTOR *const trigger_presentation,
    bool trigger_completes_implicit_output) {
  if (decoder_model->status != DECODER_MODEL_OK || trigger_buffer_index < 0 ||
      trigger_buffer_index >= decoder_model->num_ref_frames + 2 ||
      trigger_presentation == NULL || !trigger_presentation->valid) {
    if (decoder_model->status == DECODER_MODEL_OK) {
      decoder_model->status = DECODE_EXISTING_FRAME_BUF_EMPTY;
    }
    return;
  }

  while (decoder_model->status == DECODER_MODEL_OK) {
    int output_ref_idx = trigger_ref_idx;
    int output_buffer_index = trigger_buffer_index;
    uint64_t output_order = trigger_presentation->output_order;
    for (int i = 0; i < decoder_model->num_ref_frames; ++i) {
      int candidate_index;
      if (decoder_model_get_eligible_output(decoder_model, i,
                                            &candidate_index)) {
        const uint64_t candidate_order =
            decoder_model->frame_buffer_pool[candidate_index]
                .presentation.output_order;
        if (candidate_order < output_order) {
          output_ref_idx = i;
          output_buffer_index = candidate_index;
          output_order = candidate_order;
        }
      }
    }
    if (output_ref_idx == trigger_ref_idx) break;
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const output_presentation =
        &decoder_model->frame_buffer_pool[output_buffer_index].presentation;
    av2_decoder_model_check_output_frame(
        cpi, decoder_model, output_buffer_index, output_presentation, true);
  }
  if (decoder_model->status != DECODER_MODEL_OK) return;

  av2_decoder_model_check_output_frame(cpi, decoder_model, trigger_buffer_index,
                                       trigger_presentation,
                                       trigger_completes_implicit_output);
  if (decoder_model->status != DECODER_MODEL_OK) return;

  for (int k = 1; k <= decoder_model->num_ref_frames; ++k) {
    if (trigger_presentation->output_order > UINT64_MAX - (uint64_t)k) {
      decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
      return;
    }
    const uint64_t target_order =
        trigger_presentation->output_order + (uint64_t)k;
    bool made_output = false;
    for (int i = 0; i < decoder_model->num_ref_frames; ++i) {
      int candidate_index;
      if (decoder_model_get_eligible_output(decoder_model, i,
                                            &candidate_index)) {
        ENCODER_DM_PRESENTATION_DESCRIPTOR *const candidate =
            &decoder_model->frame_buffer_pool[candidate_index].presentation;
        if (candidate->output_order == target_order) {
          av2_decoder_model_check_output_frame(
              cpi, decoder_model, candidate_index, candidate, true);
          made_output = true;
        }
      }
      if (decoder_model->status != DECODER_MODEL_OK) return;
    }
    if (!made_output) break;
  }
}

void av2_decoder_model_observe_output_frame_buffers_for_operating_points(
    const AV2_COMP *const cpi, int ref_idx) {
  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const AV2LevelParams *const level_params = &cpi->level_params;
  const int tlayer_id = cm->tlayer_id;
  const int mlayer_id = cm->mlayer_id;

  for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; ++i) {
    if (!is_in_operating_point(seq_params->operating_point_idc[i], tlayer_id,
                               mlayer_id) ||
        !((level_params->keep_level_stats >> i) & 1)) {
      continue;
    }
    AV2LevelInfo *const level_info = level_params->level_info[i];
    if (level_info == NULL) continue;
    DECODER_MODEL *const decoder_models = level_info->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      if (decoder_model->status != DECODER_MODEL_OK) continue;
      if (ref_idx < 0 && !cm->show_existing_frame && decoder_model->cfbi >= 0 &&
          decoder_model->cfbi < decoder_model->num_ref_frames + 2) {
        const ENCODER_DM_PRESENTATION_DESCRIPTOR *const current_generation =
            &decoder_model->frame_buffer_pool[decoder_model->cfbi].presentation;
        if (current_generation->valid &&
            decoder_model->current_presentation.valid &&
            current_generation->buffer_index == decoder_model->cfbi &&
            decoder_model->current_presentation.buffer_index ==
                decoder_model->cfbi &&
            current_generation->generation ==
                decoder_model->current_presentation.generation &&
            current_generation->normative_output_done) {
          continue;
        }
      }
      int trigger_buffer_index = decoder_model->cfbi;
      const ENCODER_DM_PRESENTATION_DESCRIPTOR *trigger_presentation =
          &decoder_model->current_presentation;
      bool trigger_completes_implicit_output = !cm->show_existing_frame;
      if (ref_idx >= 0) {
        if (ref_idx >= decoder_model->num_ref_frames ||
            decoder_model->vbi[ref_idx] == -1) {
          decoder_model->status = DECODE_EXISTING_FRAME_BUF_EMPTY;
          continue;
        }
        trigger_buffer_index = decoder_model->vbi[ref_idx];
        trigger_presentation =
            &decoder_model->frame_buffer_pool[trigger_buffer_index]
                 .presentation;
        trigger_completes_implicit_output = true;
      } else if (cm->show_existing_frame) {
        const int generation_ref_idx = cm->sef_ref_fb_idx;
        if (generation_ref_idx < 0 ||
            generation_ref_idx >= decoder_model->num_ref_frames ||
            decoder_model->vbi[generation_ref_idx] == -1) {
          decoder_model->status = DECODE_EXISTING_FRAME_BUF_EMPTY;
          continue;
        }
        trigger_buffer_index = decoder_model->vbi[generation_ref_idx];
      }
      decoder_model_observe_output_frame_buffers(
          cpi, decoder_model, ref_idx, trigger_buffer_index,
          trigger_presentation, trigger_completes_implicit_output);
    }
  }
}

void av2_decoder_model_observe_displaced_output_for_operating_points(
    const AV2_COMP *const cpi, int ref_idx) {
  if (cpi == NULL || ref_idx < 0) return;
  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const AV2LevelParams *const level_params = &cpi->level_params;
  if (ref_idx >= seq_params->ref_frames || !level_params->keep_level_stats ||
      is_stat_generation_stage(cpi)) {
    return;
  }

  for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; ++i) {
    if (!((level_params->keep_level_stats >> i) & 1) ||
        level_params->level_info[i] == NULL ||
        !is_in_operating_point(seq_params->operating_point_idc[i],
                               cm->tlayer_id, cm->mlayer_id)) {
      continue;
    }
    DECODER_MODEL *const decoder_models =
        level_params->level_info[i]->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      if (decoder_model->status != DECODER_MODEL_OK) continue;
      const int buffer_index = decoder_model->vbi[ref_idx];
      // A newly initialized model intentionally has no ownership of stale
      // encoder references from the preceding CVS.
      if (buffer_index == -1) continue;
      if (buffer_index < 0 ||
          buffer_index >= decoder_model->num_ref_frames + 2) {
        decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
        continue;
      }
      FRAME_BUFFER *const buffer =
          &decoder_model->frame_buffer_pool[buffer_index];
      ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
          &buffer->presentation;
      if (!presentation->valid || presentation->buffer_index != buffer_index) {
        decoder_model->status = DECODER_MODEL_INCOMPLETE;
        continue;
      }
      if (!is_in_operating_point(seq_params->operating_point_idc[i],
                                 presentation->temporal_id,
                                 presentation->mlayer_id) ||
          !presentation->implicit_output_eligible ||
          presentation->normative_output_done) {
        continue;
      }
      decoder_model_observe_output_frame_buffers(
          cpi, decoder_model, ref_idx, buffer_index, presentation, true);
    }
  }
}

void av2_decoder_model_observe_restricted_output_for_operating_points(
    const AV2_COMP *const cpi) {
  if (cpi == NULL || !cpi->level_params.keep_level_stats ||
      is_stat_generation_stage(cpi)) {
    return;
  }
  const AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  for (int op = 0; op < seq_params->operating_points_cnt_minus_1 + 1; ++op) {
    if (!((cpi->level_params.keep_level_stats >> op) & 1) ||
        cpi->level_params.level_info[op] == NULL ||
        !is_in_operating_point(seq_params->operating_point_idc[op],
                               cm->tlayer_id, cm->mlayer_id)) {
      continue;
    }
    DECODER_MODEL *const decoder_models =
        cpi->level_params.level_info[op]->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      if (decoder_model->status != DECODER_MODEL_OK) continue;
      for (int ref_idx = 0; ref_idx < decoder_model->num_ref_frames;
           ++ref_idx) {
        const int buffer_index = decoder_model->vbi[ref_idx];
        if (buffer_index == -1) continue;
        if (buffer_index < 0 ||
            buffer_index >= decoder_model->num_ref_frames + 2) {
          decoder_model->status = DECODER_MODEL_INTERNAL_ERROR;
          break;
        }
        ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
            &decoder_model->frame_buffer_pool[buffer_index].presentation;
        if (!presentation->valid ||
            presentation->buffer_index != buffer_index) {
          decoder_model->status = DECODER_MODEL_INCOMPLETE;
          break;
        }
        if (!is_mlayer_transitively_dependent(
                seq_params, presentation->mlayer_id, cm->mlayer_id)) {
          continue;
        }
        int eligible_buffer_index;
        if (decoder_model_get_eligible_output(decoder_model, ref_idx,
                                              &eligible_buffer_index)) {
          decoder_model_observe_output_frame_buffers(
              cpi, decoder_model, ref_idx, eligible_buffer_index, presentation,
              true);
        }
        presentation->restricted = true;
        if (decoder_model->status != DECODER_MODEL_OK) break;
      }
    }
  }
}

static void decoder_model_flush_implicit_output(
    const AV2_COMP *const cpi, DECODER_MODEL *const decoder_model,
    bool olk_limit) {
  if (olk_limit && !decoder_model->olk_encountered) return;
  if (olk_limit && !decoder_model->olk_tu_order_hint_valid) {
    decoder_model->status = DECODER_MODEL_INCOMPLETE;
    return;
  }
  while (decoder_model->status == DECODER_MODEL_OK) {
    int output_buffer_index = -1;
    uint64_t output_order = 0;
    for (int i = 0; i < decoder_model->num_ref_frames; ++i) {
      int candidate_index;
      if (!decoder_model_get_eligible_output(decoder_model, i,
                                             &candidate_index)) {
        continue;
      }
      ENCODER_DM_PRESENTATION_DESCRIPTOR *const candidate =
          &decoder_model->frame_buffer_pool[candidate_index].presentation;
      if (olk_limit &&
          candidate->order_hint >= decoder_model->olk_tu_order_hint) {
        continue;
      }
      if (output_buffer_index == -1 ||
          candidate->output_order <= output_order) {
        output_buffer_index = candidate_index;
        output_order = candidate->output_order;
      }
    }
    if (output_buffer_index == -1) return;
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
        &decoder_model->frame_buffer_pool[output_buffer_index].presentation;
    av2_decoder_model_check_output_frame(
        cpi, decoder_model, output_buffer_index, presentation, true);
  }
}

void av2_decoder_model_flush_implicit_output_for_operating_points(
    const AV2_COMP *const cpi, bool olk_limit) {
  if (cpi == NULL || !cpi->level_params.keep_level_stats ||
      is_stat_generation_stage(cpi)) {
    return;
  }
  const SequenceHeader *const seq_params = &cpi->common.seq_params;
  const int operating_point_count =
      olk_limit ? seq_params->operating_points_cnt_minus_1 + 1
                : MAX_NUM_OPERATING_POINTS;
  for (int i = 0; i < operating_point_count; ++i) {
    if (!((cpi->level_params.keep_level_stats >> i) & 1) ||
        cpi->level_params.level_info[i] == NULL ||
        (olk_limit && !is_in_operating_point(seq_params->operating_point_idc[i],
                                             cpi->common.tlayer_id,
                                             cpi->common.mlayer_id))) {
      continue;
    }
    DECODER_MODEL *const decoder_models =
        cpi->level_params.level_info[i]->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      decoder_model_flush_implicit_output(cpi, decoder_model, olk_limit);
      if (olk_limit) {
        decoder_model->olk_encountered = false;
        decoder_model->olk_tu_order_hint_valid = false;
      }
    }
  }
}

void av2_encoder_decoder_model_finish_for_operating_points(
    const AV2_COMP *const cpi) {
  if (cpi == NULL || !cpi->level_params.keep_level_stats ||
      is_stat_generation_stage(cpi)) {
    return;
  }
  av2_decoder_model_flush_implicit_output_for_operating_points(cpi, false);
  for (int op = 0; op < MAX_NUM_OPERATING_POINTS; ++op) {
    if (!((cpi->level_params.keep_level_stats >> op) & 1) ||
        cpi->level_params.level_info[op] == NULL) {
      continue;
    }
    DECODER_MODEL *const decoder_models =
        cpi->level_params.level_info[op]->decoder_models;
    for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      DECODER_MODEL *const decoder_model = &decoder_models[level];
      av2_encoder_decoder_model_finalize(decoder_model,
                                         decoder_model->is_still_picture);
    }
  }
}

// Get the index of the level parameter entry in av2_substream_level_defs for
// sub-stream case given the level and the scaling factor.
// Should we define the behavior for levels below 4.0?
int level_to_sub_stream_level_index(AV2_LEVEL level, double scaling_factor_x) {
  int level_base =
      level < SEQ_LEVEL_5_0 ? 0 : ((level - SEQ_LEVEL_5_0) >> 2) + 1;
  int offset = scaling_factor_x == 1.5 ? 0 : (scaling_factor_x == 4.0 ? 1 : 2);
  return 3 * level_base + offset;
}

void av2_init_level_info(AV2_COMP *cpi) {
  for (int op_index = 0; op_index < MAX_NUM_OPERATING_POINTS; ++op_index) {
    AV2LevelInfo *const this_level_info =
        cpi->level_params.level_info[op_index];
    if (!this_level_info) continue;
    av2_encoder_decoder_models_destroy(this_level_info);
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

ENCODER_DM_RESULT_CLASS av2_encoder_decoder_model_classify_status(
    DECODER_MODEL_STATUS status) {
  switch (status) {
    case DECODER_MODEL_OK:
    case DECODER_MODEL_DISABLED: return ENCODER_DM_RESULT_PASS;
    case DECODE_FRAME_BUF_UNAVAILABLE:
    case DECODE_EXISTING_FRAME_BUF_EMPTY:
    case DISPLAY_FRAME_LATE:
    case SMOOTHING_BUFFER_UNDERFLOW:
    case SMOOTHING_BUFFER_OVERFLOW: return ENCODER_DM_RESULT_VIOLATION;
    case DECODER_MODEL_MULTIPLE_XLAYERS:
    case DECODER_MODEL_UNSUPPORTED:
    case DECODER_MODEL_INCOMPLETE:
    case DECODER_MODEL_INTERNAL_ERROR: return ENCODER_DM_RESULT_UNAVAILABLE;
  }
  return ENCODER_DM_RESULT_UNAVAILABLE;
}

static TARGET_LEVEL_FAIL_ID check_level_constraints(
    const AV2_COMP *const cpi, const AV2LevelInfo *const level_info,
    AV2_LEVEL level, int tier, int is_still_picture, BITSTREAM_PROFILE profile,
    int check_bitrate) {
  const DECODER_MODEL *const decoder_model = &level_info->decoder_models[level];
  const DECODER_MODEL_STATUS decoder_model_status = decoder_model->status;
  const ENCODER_DM_RESULT_CLASS model_result =
      av2_encoder_decoder_model_classify_status(decoder_model_status);
  if (model_result == ENCODER_DM_RESULT_VIOLATION) {
    return DECODER_MODEL_FAIL;
  }
  bool model_unavailable = model_result == ENCODER_DM_RESULT_UNAVAILABLE;
  if (decoder_model_status == DECODER_MODEL_OK && !decoder_model->initialized) {
    model_unavailable = true;
  }
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
  const AV2LevelSpec *const level_spec = &level_info->level_spec;
  const AV2LevelSpec *const target_level_spec = &av2_level_defs[level];
  const AV2LevelStats *const level_stats = &level_info->level_stats;
  TARGET_LEVEL_FAIL_ID fail_id = TARGET_LEVEL_OK;
  do {
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
      fail_id = LUMA_PIC_SIZE_TOO_LARGE;
      break;
    }
    if (level_spec->max_h_size > max_h_size) {
      fail_id = LUMA_PIC_H_SIZE_TOO_LARGE;
      break;
    }
    if (level_spec->max_v_size > max_v_size) {
      fail_id = LUMA_PIC_V_SIZE_TOO_LARGE;
      break;
    }
    if (level_spec->max_tile_cols > max_tile_cols) {
      fail_id = TOO_MANY_TILE_COLUMNS;
      break;
    }
    if (level_spec->max_tiles > max_tiles) {
      fail_id = TOO_MANY_TILES;
      break;
    }

    if (level_spec->max_tile_rate > target_level_spec->max_tiles * 120) {
      fail_id = TILE_RATE_TOO_HIGH;
      break;
    }

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
    if (!is_still_picture && decoder_model->initialized) {
      const int max_header_rate =
          is_multi_stream ? target_sub_stream_level_spec->max_header_rate_x
                          : target_level_spec->max_header_rate;
      const double max_display_rate =
          (double)target_level_spec->max_display_rate / multi_stream_scaling_x;

      if (level_spec->max_header_rate > (max_header_rate * (1 + (tier * 2)))) {
        fail_id = FRAME_HEADER_RATE_TOO_HIGH;
        break;
      }
      if (decoder_model->max_display_rate > max_display_rate) {
        fail_id = DISPLAY_RATE_TOO_HIGH;
        break;
      }
      if (!decoder_model->max_decode_rate_satisfy) {
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
      if (!decoder_model->min_presentation_interval_satisfy) {
        fail_id = PRESENTATION_INTERVAL_TOO_SMALL;
        break;
      }
    }

    if (check_bitrate) {
      // Check average bitrate instead of max_bitrate.
      if (!(level_stats->total_time_encoded > 0.0) ||
          !isfinite(level_stats->total_time_encoded) ||
          !isfinite(level_stats->total_compressed_size)) {
        model_unavailable = true;
        break;
      }
      const double bitrate_limit =
          get_max_bitrate(target_level_spec, tier, profile,
                          cpi->level_params.multi_stream_scaling_x);
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
    }

  } while (0);

  if (fail_id == TARGET_LEVEL_OK && model_unavailable) {
    return DECODER_MODEL_UNAVAILABLE;
  }
  return fail_id;
}

static bool level_info_has_model_state(const AV2LevelInfo *level_info) {
  if (level_info == NULL) return false;
  for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
    const DECODER_MODEL *const model = &level_info->decoder_models[level];
    if (model->initialized || model->status != DECODER_MODEL_OK) return true;
  }
  return false;
}

void av2_encoder_check_target_level(AV2_COMP *cpi, bool all_operating_points) {
  if (cpi == NULL || !cpi->level_params.keep_level_stats ||
      is_stat_generation_stage(cpi)) {
    return;
  }
  AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  AV2LevelParams *const level_params = &cpi->level_params;
  const int operating_point_count =
      all_operating_points ? MAX_NUM_OPERATING_POINTS
                           : seq_params->operating_points_cnt_minus_1 + 1;
  for (int op = 0; op < operating_point_count; ++op) {
    if (!((level_params->keep_level_stats >> op) & 1) ||
        level_params->level_info[op] == NULL ||
        (!all_operating_points &&
         !is_in_operating_point(seq_params->operating_point_idc[op],
                                cm->tlayer_id, cm->mlayer_id))) {
      continue;
    }
    AV2LevelInfo *const level_info = level_params->level_info[op];
    if (!level_info_has_model_state(level_info)) continue;
    const AV2_LEVEL target_level = level_params->target_seq_level_idx[op];
    if (target_level >= SEQ_LEVELS) continue;
    assert(is_valid_seq_level_idx(target_level));
    const DECODER_MODEL *const target_model =
        &level_info->decoder_models[target_level];
    const int tier =
        target_model->initialized ? target_model->tier : cpi->tier[op];
    const int is_still_picture = target_model->initialized
                                     ? target_model->is_still_picture
                                     : seq_params->still_picture;
    const TARGET_LEVEL_FAIL_ID fail_id = check_level_constraints(
        cpi, level_info, target_level, tier, is_still_picture,
        seq_params->seq_profile_idc, 0);
    if (fail_id != TARGET_LEVEL_OK) {
      avm_internal_error(&cm->error, AVM_CODEC_ERROR,
                         "Failed to encode to the target level %s. %s",
                         level_string[target_level],
                         level_fail_messages[fail_id]);
    }
  }
}

static void get_tile_stats(const AV2_COMMON *const cm,
                           const TileDataEnc *const tile_data,
                           int *max_tile_size, int *min_cropped_tile_width,
                           int *min_cropped_tile_height, int *tile_width_valid,
                           int *max_tile_width) {
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  *max_tile_size = 0;
  *min_cropped_tile_width = INT_MAX;
  *min_cropped_tile_height = INT_MAX;
  *tile_width_valid = 1;
  *max_tile_width = 0;

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
      *max_tile_width = AVMMAX(*max_tile_width, tile_width);

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
  const uint64_t luma_pic_size = (uint64_t)upscaled_width * (uint64_t)height;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const BITSTREAM_PROFILE profile = seq_params->seq_profile_idc;

  AV2ProfileLevelFactors factors;
  const bool profile_supported = profile < RESERVED_PROFILES_START;
  if ((!profile_supported ||
       !av2_get_profile_level_factors(profile, &factors)) &&
      !av2_get_profile_level_factors(MAIN_420_10_IP0, &factors)) {
    return 0.0;
  }
  encoded_frame_size =
      (encoded_frame_size > 129 ? encoded_frame_size - 128 : 1);
  const uint64_t uncompressed_frame_size =
      luma_pic_size * factors.picture_size_profile_factor >> 3;
  return uncompressed_frame_size / (double)encoded_frame_size;
}

void av2_update_level_info(AV2_COMP *cpi, const uint8_t *data, size_t size,
                           int64_t ts_start, int64_t ts_end,
                           bool has_serialized_frame_unit,
                           uint64_t dfg_prefix_bits) {
  AV2_COMMON *const cm = &cpi->common;
  AV2LevelParams *const level_params = &cpi->level_params;
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
  int max_tile_width;
  int min_cropped_tile_width;
  int min_cropped_tile_height;
  int tile_width_is_valid;
  get_tile_stats(cm, cpi->tile_data, &max_tile_size, &min_cropped_tile_width,
                 &min_cropped_tile_height, &tile_width_is_valid,
                 &max_tile_width);

  avm_clear_system_state();
  const double compression_ratio = av2_get_compression_ratio(cm, size);

  const int tlayer_id = cm->tlayer_id;
  const int mlayer_id = cm->mlayer_id;
  const int xlayer_id = cm->xlayer_id;
  (void)xlayer_id;
  const SequenceHeader *const seq_params = &cm->seq_params;
  uint64_t dfg_bits = 0;
  int64_t compressed_size = 0;
  bool model_accounting_valid = true;
  if (has_serialized_frame_unit) {
    uint64_t dfg_bytes;
    uint64_t frame_compressed_bytes;
    model_accounting_valid =
        !cpi->dm_frame_symbol_count_overflow &&
        av2_encoder_decoder_model_count_obu_bytes(data, size, &dfg_bytes,
                                                  &frame_compressed_bytes) &&
        dfg_bytes <= (UINT64_MAX - dfg_prefix_bits) / 8 &&
        av2_encoder_decoder_model_get_compressed_size(frame_compressed_bytes,
                                                      &compressed_size);
    if (model_accounting_valid) {
      dfg_bits = dfg_bytes * 8 + dfg_prefix_bits;
    }
  }
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
    level_stats->max_tile_size =
        AVMMAX(level_stats->max_tile_size, max_tile_size);
    level_stats->max_tile_width =
        AVMMAX(level_stats->max_tile_width, max_tile_width);
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

    if (has_serialized_frame_unit) {
      DECODER_MODEL *const decoder_models = level_info->decoder_models;
      for (AV2_LEVEL level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
        if (decoder_models[level].status != DECODER_MODEL_OK) continue;
        if (!model_accounting_valid) {
          decoder_models[level].status = DECODER_MODEL_INTERNAL_ERROR;
        } else {
          av2_decoder_model_start_frame_decode(cpi, dfg_bits, compressed_size,
                                               &decoder_models[level]);
        }
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
      if ((tier != 0 || (level_params->multi_stream_scaling_x != 0.0 &&
                         level_params->multi_stream_scaling_x != 1.0)) &&
          level < SEQ_LEVEL_4_0) {
        continue;
      }
      const TARGET_LEVEL_FAIL_ID fail_id = check_level_constraints(
          cpi, level_info, level, tier, is_still_picture, profile, 1);
      if (fail_id == DECODER_MODEL_UNAVAILABLE) break;
      if (fail_id == TARGET_LEVEL_OK) {
        seq_level_idx[op] = level;
        break;
      }
    }
  }

  return AVM_CODEC_OK;
}
