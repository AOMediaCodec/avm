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

#include "av2/common/level.h"

#include <assert.h>

#include "avm/avm_integer.h"

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

// The encoder keeps its legacy level-to-row mapping, including levels below
// 4.0. This accessor shares the table without imposing the verifier's stricter
// level validation. The caller must supply a valid table index.
const AV2SubstreamLevelSpec *av2_get_substream_level_spec_by_index(int index) {
  assert(index >= 0 && index < (int)(sizeof(av2_substream_level_defs) /
                                     sizeof(av2_substream_level_defs[0])));
  return &av2_substream_level_defs[index];
}

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
