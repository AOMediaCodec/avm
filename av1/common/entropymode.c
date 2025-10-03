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

#include "aom_mem/aom_mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/enums.h"
#include "av1/common/reconinter.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"
#include "av1/common/txb_common.h"
#include "av1/encoder/mcomp.h"

static const aom_cdf_prob
    default_mrl_index_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(MRL_LINE_NUMBER)] = {
      { AOM_CDF4(28081, 30613, 31659), 78 },
      { AOM_CDF4(22175, 28045, 30623), 75 },
      { AOM_CDF4(17175, 25921, 29682), 1 },
    };

static const aom_cdf_prob default_multi_line_mrl_cdf[MRL_INDEX_CONTEXTS]
                                                    [CDF_SIZE(2)] = {
                                                      { AOM_CDF2(28081), 0 },
                                                      { AOM_CDF2(22175), 0 },
                                                      { AOM_CDF2(16384), 0 },
                                                    };

static const aom_cdf_prob default_dpcm_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384) };
static const aom_cdf_prob default_dpcm_vert_horz_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
static const aom_cdf_prob default_dpcm_uv_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
static const aom_cdf_prob default_dpcm_uv_vert_horz_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384)
};

static const aom_cdf_prob default_fsc_mode_cdf[FSC_MODE_CONTEXTS]
                                              [FSC_BSIZE_CONTEXTS]
                                              [CDF_SIZE(FSC_MODES)] = {
                                                {
                                                    { AOM_CDF2(29820), 78 },
                                                    { AOM_CDF2(31107), 78 },
                                                    { AOM_CDF2(32018), 118 },
                                                    { AOM_CDF2(32202), 118 },
                                                    { AOM_CDF2(32482), 118 },
                                                    { AOM_CDF2(32539), 123 },
                                                },
                                                {
                                                    { AOM_CDF2(27906), 75 },
                                                    { AOM_CDF2(27439), 90 },
                                                    { AOM_CDF2(29059), 1 },
                                                    { AOM_CDF2(28167), 76 },
                                                    { AOM_CDF2(27696), 7 },
                                                    { AOM_CDF2(22842), 54 },
                                                },
                                                {
                                                    { AOM_CDF2(26882), 115 },
                                                    { AOM_CDF2(22539), 95 },
                                                    { AOM_CDF2(23495), 8 },
                                                    { AOM_CDF2(18016), 7 },
                                                    { AOM_CDF2(11559), 49 },
                                                    { AOM_CDF2(4688), 4 },
                                                },
                                                {
                                                    { AOM_CDF2(29627), 118 },
                                                    { AOM_CDF2(29794), 93 },
                                                    { AOM_CDF2(32190), 123 },
                                                    { AOM_CDF2(32289), 123 },
                                                    { AOM_CDF2(32618), 123 },
                                                    { AOM_CDF2(32583), 123 },
                                                },
                                              };

#if MHCCP_RUNTIME_FLAG
static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(
    CFL_TYPE_COUNT - 1)] = { AOM_CDF2(18000), 0 };
static const aom_cdf_prob default_cfl_mhccp_switch_cdf[CDF_SIZE(
    CFL_MHCCP_SWITCH_NUM)] = { AOM_CDF2(18000), 0 };
#else
static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(
    CFL_TYPE_COUNT - 1)] = { AOM_CDF3(4124, 16615), 75 };
#endif  // MHCCP_RUNTIME_FLAG
static const aom_cdf_prob default_filter_dir_cdf[MHCCP_CONTEXT_GROUP_SIZE]
                                                [CDF_SIZE(MHCCP_MODE_NUM)] = {
                                                  { AOM_CDF3(16384, 24576), 1 },
                                                  { AOM_CDF3(16384, 24576), 1 },
                                                  { AOM_CDF3(16384, 24576), 1 },
                                                  { AOM_CDF3(16384, 24576),
                                                    32 },
                                                };

static const aom_cdf_prob default_y_mode_set_cdf[CDF_SIZE(INTRA_MODE_SETS)] = {
  AOM_CDF4(28618, 30909, 31555), 118
};

#if CONFIG_REDUCE_SYMBOL_SIZE
static const aom_cdf_prob default_y_mode_idx_cdf[Y_MODE_CONTEXTS][CDF_SIZE(
    LUMA_INTRA_MODE_INDEX_COUNT)] = {
  { AOM_CDF8(14967, 20223, 22467, 24775, 26294, 27253, 28348), 75 },
  { AOM_CDF8(10399, 14457, 16589, 18447, 19804, 24728, 26455), 75 },
  { AOM_CDF8(5342, 7123, 8352, 9283, 9845, 17570, 23158), 75 },
};
static const aom_cdf_prob
    default_y_mode_idx_offset_cdf[Y_MODE_CONTEXTS][CDF_SIZE(
        LUMA_INTRA_MODE_OFFSET_COUNT)] = {
      { AOM_CDF6(15256, 19609, 22218, 25531, 27739), 75 },
      { AOM_CDF6(11526, 16924, 21400, 25152, 28399), 75 },
      { AOM_CDF6(11465, 16384, 21089, 26781, 29709), 75 },
    };
static const aom_cdf_prob default_uv_mode_cdf[UV_MODE_CONTEXTS][CDF_SIZE(
    CHROMA_INTRA_MODE_INDEX_COUNT)] = {
  { AOM_CDF8(13848, 18930, 20641, 22133, 23986, 25450, 28075), 0 },
  { AOM_CDF8(19268, 22648, 25651, 26449, 27288, 28840, 29451), 0 },
};
#else
static const aom_cdf_prob
    default_y_first_mode_cdf[Y_MODE_CONTEXTS][CDF_SIZE(FIRST_MODE_COUNT)] = {
      { AOM_CDF13(14967, 20223, 22467, 24775, 26294, 27253, 28348, 30404, 30994,
                  31347, 31791, 32090),
        75 },
      { AOM_CDF13(10399, 14457, 16589, 18447, 19804, 24728, 26455, 28680, 29718,
                  30583, 31310, 31936),
        75 },
      { AOM_CDF13(5342, 7123, 8352, 9283, 9845, 17570, 23158, 26522, 27963,
                  29340, 31013, 31870),
        75 },
    };
#if !CONFIG_CTX_Y_SECOND_MODE
static const aom_cdf_prob
    default_y_second_mode_cdf[Y_MODE_CONTEXTS][CDF_SIZE(SECOND_MODE_COUNT)] = {
      { AOM_CDF16(2614, 4872, 7327, 9645, 11546, 13349, 15504, 17424, 19397,
                  21573, 23212, 25033, 26890, 28681, 30112),
        1 },
      { AOM_CDF16(2837, 4903, 6949, 8850, 11141, 13110, 14637, 16551, 18362,
                  20178, 22150, 24988, 27556, 29191, 30451),
        90 },
      { AOM_CDF16(2444, 4419, 5731, 6909, 9743, 12013, 14058, 15701, 17458,
                  19690, 21835, 25356, 27731, 29157, 30362),
        76 },
    };
#endif  // !CONFIG_CTX_Y_SECOND_MODE
static const aom_cdf_prob
    default_uv_mode_cdf[UV_MODE_CONTEXTS][CDF_SIZE(UV_INTRA_MODES - 1)] = {
      { AOM_CDF13(13848, 18930, 20641, 22133, 23986, 25450, 28075, 28950, 29740,
                  30647, 31182, 31880),
        0 },
      { AOM_CDF13(19268, 22648, 25651, 26449, 27288, 28840, 29451, 30120, 30622,
                  31606, 31844, 32144),
        0 },
    };
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
static const aom_cdf_prob default_cfl_cdf[CFL_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(18484), 6 },
  { AOM_CDF2(8591), 6 },
  { AOM_CDF2(2151), 0 },
};

static aom_cdf_prob default_region_type_cdf[INTER_SDP_BSIZE_GROUP]
                                           [CDF_SIZE(REGION_TYPES)] = {
                                             // w * h <= 128
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 512
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 1024
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 4096
                                             { AOM_CDF2(8192), 0 },
                                           };
// clang-format off
#if CONFIG_NEW_PART_CTX
aom_cdf_prob default_do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(29592),   3 },
    { AOM_CDF2(25157),   0 },
    { AOM_CDF2(24896),   3 },
    { AOM_CDF2(18550),   3 },
    { AOM_CDF2(26167),   0 },
    { AOM_CDF2(16439),   0 },
    { AOM_CDF2(18221),   1 },
    { AOM_CDF2( 8357),   0 },
    { AOM_CDF2(23673),   0 },
    { AOM_CDF2(12166),  26 },
    { AOM_CDF2(14523),  26 },
    { AOM_CDF2( 4830),   0 },
    { AOM_CDF2(21600),  15 },
    { AOM_CDF2( 7914),  26 },
    { AOM_CDF2(11061),  26 },
    { AOM_CDF2( 2383),   0 },
    { AOM_CDF2(24735),  17 },
    { AOM_CDF2(10913),  75 },
    { AOM_CDF2(23823),  20 },
    { AOM_CDF2(11191),  70 },
    { AOM_CDF2(28036),  27 },
    { AOM_CDF2(21661),  27 },
    { AOM_CDF2(14269),  34 },
    { AOM_CDF2( 9134),  50 },
    { AOM_CDF2(19651),   1 },
    { AOM_CDF2( 6508),  32 },
    { AOM_CDF2( 7948),  41 },
    { AOM_CDF2( 1824),  25 },
    { AOM_CDF2(26166),  99 },
    { AOM_CDF2(12700),  20 },
    { AOM_CDF2(20665), 100 },
    { AOM_CDF2( 8113),  50 },
    { AOM_CDF2(25468), 122 },
    { AOM_CDF2(23585), 100 },
    { AOM_CDF2(16272), 100 },
    { AOM_CDF2(13812), 100 },
    { AOM_CDF2(25374),  19 },
    { AOM_CDF2( 4948),  32 },
    { AOM_CDF2( 8097),  95 },
    { AOM_CDF2( 1079),  47 },
    { AOM_CDF2(28437),  75 },
    { AOM_CDF2(26588),  75 },
    { AOM_CDF2(22900),   0 },
    { AOM_CDF2(18688),   1 },
    { AOM_CDF2(27263),   0 },
    { AOM_CDF2(21031),   0 },
    { AOM_CDF2(26901),   0 },
    { AOM_CDF2(17084),   0 },
    { AOM_CDF2(26802),   0 },
    { AOM_CDF2(17834),   1 },
    { AOM_CDF2(20733),   1 },
    { AOM_CDF2(12114),  31 },
    { AOM_CDF2(22883),   1 },
    { AOM_CDF2(12440),  31 },
    { AOM_CDF2(12447),   0 },
    { AOM_CDF2( 5380),  25 },
    { AOM_CDF2(25790),   0 },
    { AOM_CDF2(16300),   9 },
    { AOM_CDF2(18314),  32 },
    { AOM_CDF2( 9351),  37 },
    { AOM_CDF2(22881),  32 },
    { AOM_CDF2( 8456),  31 },
    { AOM_CDF2(11142),   9 },
    { AOM_CDF2( 2431),  25 },
  },
  {
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(25023),   0 },
    { AOM_CDF2(20754),   0 },
    { AOM_CDF2(20240),   0 },
    { AOM_CDF2(16994),   0 },
    { AOM_CDF2(23601),   1 },
    { AOM_CDF2(18512),  31 },
    { AOM_CDF2(18745),  31 },
    { AOM_CDF2(13554),  31 },
    { AOM_CDF2(27392),   6 },
    { AOM_CDF2(19251),  32 },
    { AOM_CDF2(22019),  32 },
    { AOM_CDF2(13900),  31 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(24433),   4 },
    { AOM_CDF2(23923),  22 },
    { AOM_CDF2(20977),   1 },
    { AOM_CDF2(18697),   1 },
    { AOM_CDF2(22912),   1 },
    { AOM_CDF2(19497),   1 },
    { AOM_CDF2(20455),   6 },
    { AOM_CDF2(16519),   1 },
    { AOM_CDF2(22789),  47 },
    { AOM_CDF2(18840),  10 },
    { AOM_CDF2(20664),   6 },
    { AOM_CDF2(16212),  34 },
    { AOM_CDF2(23284),  38 },
    { AOM_CDF2(15481),  32 },
    { AOM_CDF2(16501),  35 },
    { AOM_CDF2( 9912),  31 },
  },
};

aom_cdf_prob default_do_square_split_cdf[PARTITION_STRUCTURE_NUM][SQUARE_SPLIT_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(21371),   7 },
    { AOM_CDF2(14164),  56 },
    { AOM_CDF2(15382),  62 },
    { AOM_CDF2( 7023),  37 },
    { AOM_CDF2(19803),  75 },
    { AOM_CDF2(11920),  60 },
    { AOM_CDF2(14061),  35 },
    { AOM_CDF2( 6299),  32 },
  },
  {
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
};

aom_cdf_prob default_rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(14338),   0 },
    { AOM_CDF2(10228),   0 },
    { AOM_CDF2(18329),   0 },
    { AOM_CDF2(15630),  75 },
    { AOM_CDF2(20106),   0 },
    { AOM_CDF2(13528),   0 },
    { AOM_CDF2(25273),   0 },
    { AOM_CDF2(21875),   1 },
    { AOM_CDF2(11348),  75 },
    { AOM_CDF2( 7569),  15 },
    { AOM_CDF2(20957),   1 },
    { AOM_CDF2(14158),   1 },
    { AOM_CDF2(13127),  91 },
    { AOM_CDF2( 9850),   1 },
    { AOM_CDF2(17267),   1 },
    { AOM_CDF2(16081),   6 },
    { AOM_CDF2(14602),   7 },
    { AOM_CDF2(11661),   7 },
    { AOM_CDF2(19594),   1 },
    { AOM_CDF2(19302),   6 },
    { AOM_CDF2(14336),   1 },
    { AOM_CDF2( 8642),   1 },
    { AOM_CDF2(22984),   7 },
    { AOM_CDF2(13828),   6 },
    { AOM_CDF2(17574),   1 },
    { AOM_CDF2(12716),  31 },
    { AOM_CDF2(24743),   1 },
    { AOM_CDF2(22007),   1 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(15380),   0 },
    { AOM_CDF2( 9860),  47 },
    { AOM_CDF2(19503),  31 },
    { AOM_CDF2(14132),  39 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16602),   0 },
    { AOM_CDF2(13446),  10 },
    { AOM_CDF2(19306),  10 },
    { AOM_CDF2(17273),  25 },
    { AOM_CDF2(22921),   1 },
    { AOM_CDF2(18378),  15 },
    { AOM_CDF2(28277),   0 },
    { AOM_CDF2(26248),   0 },
    { AOM_CDF2( 5795),   1 },
    { AOM_CDF2( 1483),  90 },
    { AOM_CDF2(11973),  30 },
    { AOM_CDF2( 2872),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
  {
    { AOM_CDF2(15399),  76 },
    { AOM_CDF2(14137),  75 },
    { AOM_CDF2(17710), 121 },
    { AOM_CDF2(14838),  90 },
    { AOM_CDF2(17776),  84 },
    { AOM_CDF2(14815),  21 },
    { AOM_CDF2(21224),  75 },
    { AOM_CDF2(19659),  76 },
    { AOM_CDF2(14762),   5 },
    { AOM_CDF2(13021),   0 },
    { AOM_CDF2(18088),   5 },
    { AOM_CDF2(15202),   0 },
    { AOM_CDF2(16855), 100 },
    { AOM_CDF2(13189),  76 },
    { AOM_CDF2(19008),   7 },
    { AOM_CDF2(15846),  75 },
    { AOM_CDF2(18420),  50 },
    { AOM_CDF2(12793),  95 },
    { AOM_CDF2(23424),  22 },
    { AOM_CDF2(22374),   6 },
    { AOM_CDF2(12213),  97 },
    { AOM_CDF2( 8571),  75 },
    { AOM_CDF2(20413),  12 },
    { AOM_CDF2(11571),   1 },
    { AOM_CDF2(15862),  85 },
    { AOM_CDF2(10112),  24 },
    { AOM_CDF2(22575),  12 },
    { AOM_CDF2(17188),   6 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(18577), 100 },
    { AOM_CDF2(14008), 100 },
    { AOM_CDF2(22927),  35 },
    { AOM_CDF2(20558),  95 },
    { AOM_CDF2(11916),  45 },
    { AOM_CDF2( 5718),   6 },
    { AOM_CDF2(15864),  45 },
    { AOM_CDF2( 6207),  26 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
};

aom_cdf_prob default_do_ext_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_CONTEXTS][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(31443),  78 },
      { AOM_CDF2(28558),   0 },
      { AOM_CDF2(28437),   0 },
      { AOM_CDF2(25094),  15 },
      { AOM_CDF2(30737),  93 },
      { AOM_CDF2(27830),  75 },
      { AOM_CDF2(27423),  75 },
      { AOM_CDF2(26170),   0 },
      { AOM_CDF2(30343),  93 },
      { AOM_CDF2(27986),  15 },
      { AOM_CDF2(27168),  75 },
      { AOM_CDF2(26844),  25 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(30314),   1 },
      { AOM_CDF2(26226),   9 },
      { AOM_CDF2(25222),   9 },
      { AOM_CDF2(22803),  22 },
      { AOM_CDF2(30598),  75 },
      { AOM_CDF2(27892),  75 },
      { AOM_CDF2(27462),  90 },
      { AOM_CDF2(25206),  90 },
      { AOM_CDF2(29897),   1 },
      { AOM_CDF2(24820),  45 },
      { AOM_CDF2(24435),   5 },
      { AOM_CDF2(20217),  17 },
      { AOM_CDF2(28940),  90 },
      { AOM_CDF2(25901),  98 },
      { AOM_CDF2(24666), 118 },
      { AOM_CDF2(23826),  75 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),  31 },
      { AOM_CDF2(16384),  25 },
      { AOM_CDF2(16384),  31 },
      { AOM_CDF2(16384),  25 },
      { AOM_CDF2(16384),  31 },
      { AOM_CDF2(16384),  31 },
      { AOM_CDF2(16384),  31 },
      { AOM_CDF2(16384),  30 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),  25 },
      { AOM_CDF2(16384), 100 },
      { AOM_CDF2(16384), 100 },
      { AOM_CDF2(16384),  10 },
      { AOM_CDF2(16384),  32 },
      { AOM_CDF2(16384),  30 },
      { AOM_CDF2(16384),  34 },
      { AOM_CDF2(16384),   0 },
    },
  },
};

aom_cdf_prob default_do_uneven_4way_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_CONTEXTS][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(22788),  75 },
      { AOM_CDF2(20501),   1 },
      { AOM_CDF2(20361),   1 },
      { AOM_CDF2(18603),   5 },
      { AOM_CDF2(21947),  19 },
      { AOM_CDF2(20816),   9 },
      { AOM_CDF2(19610),   9 },
      { AOM_CDF2(20458),  32 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(18501),  24 },
      { AOM_CDF2(15193),   0 },
      { AOM_CDF2(15645),  90 },
      { AOM_CDF2(13389),  80 },
      { AOM_CDF2(14267),   6 },
      { AOM_CDF2(12424),  99 },
      { AOM_CDF2(12381),  91 },
      { AOM_CDF2(10715),  78 },
      { AOM_CDF2(15353),   0 },
      { AOM_CDF2(13584),   0 },
      { AOM_CDF2(13401),  75 },
      { AOM_CDF2(11604),  95 },
      { AOM_CDF2(10973),  10 },
      { AOM_CDF2( 9351),  80 },
      { AOM_CDF2( 9200),  84 },
      { AOM_CDF2( 7561),  75 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),  22 },
      { AOM_CDF2(16384),  75 },
      { AOM_CDF2(16384),  80 },
      { AOM_CDF2(16384),  76 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),  25 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384), 100 },
      { AOM_CDF2(16384), 100 },
      { AOM_CDF2(16384),  10 },
      { AOM_CDF2(16384),  20 },
      { AOM_CDF2(16384),   6 },
    },
  },
};
#else  // CONFIG_NEW_PART_CTX
static aom_cdf_prob default_do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(29124),   3 },
    { AOM_CDF2(25989),   0 },
    { AOM_CDF2(26134),   0 },
    { AOM_CDF2(22598),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(24835),   0 },
    { AOM_CDF2(13076),  30 },
    { AOM_CDF2(13914),   0 },
    { AOM_CDF2( 4568),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(20273),   0 },
    { AOM_CDF2( 6505),   1 },
    { AOM_CDF2( 8134),   1 },
    { AOM_CDF2( 1368),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(18191),   1 },
    { AOM_CDF2( 4286),   1 },
    { AOM_CDF2( 5428),   1 },
    { AOM_CDF2(  451),  90 },
    { AOM_CDF2(24696),   1 },
    { AOM_CDF2( 8308),  20 },
    { AOM_CDF2(20294),   1 },
    { AOM_CDF2( 4680),   2 },
    { AOM_CDF2(27050),   1 },
    { AOM_CDF2(23328),   8 },
    { AOM_CDF2(10390),   5 },
    { AOM_CDF2( 6201),  35 },
    { AOM_CDF2(20285),   7 },
    { AOM_CDF2( 3108),   7 },
    { AOM_CDF2( 3939),   7 },
    { AOM_CDF2(  338),  90 },
    { AOM_CDF2(16384),  45 },
    { AOM_CDF2(16384),  25 },
    { AOM_CDF2(16384),  75 },
    { AOM_CDF2(16384),  50 },
    { AOM_CDF2(16384),  48 },
    { AOM_CDF2(16384),  85 },
    { AOM_CDF2(16384),  50 },
    { AOM_CDF2(16384),  55 },
    { AOM_CDF2(16384),  30 },
    { AOM_CDF2(16384),  50 },
    { AOM_CDF2(16384),  60 },
    { AOM_CDF2(16384),  70 },
    { AOM_CDF2(26859),   0 },
    { AOM_CDF2(24959),   0 },
    { AOM_CDF2(21408),   0 },
    { AOM_CDF2(18157),   0 },
    { AOM_CDF2(25672),   0 },
    { AOM_CDF2(19642),   0 },
    { AOM_CDF2(23743),  75 },
    { AOM_CDF2(16258),   1 },
    { AOM_CDF2(23700),   0 },
    { AOM_CDF2(13174),   1 },
    { AOM_CDF2(12294),   1 },
    { AOM_CDF2( 4252),   1 },
    { AOM_CDF2(21681),   1 },
    { AOM_CDF2( 7655),  31 },
    { AOM_CDF2(10653),   1 },
    { AOM_CDF2( 1746),   0 },
    { AOM_CDF2(22557),   1 },
    { AOM_CDF2(12078),   1 },
    { AOM_CDF2(10102),   1 },
    { AOM_CDF2( 3972),   1 },
    { AOM_CDF2(22781),   1 },
    { AOM_CDF2( 4286),   6 },
    { AOM_CDF2(11580),   6 },
    { AOM_CDF2(  754),  75 },
  },
  {
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(26640),   0 },
    { AOM_CDF2(18937),   0 },
    { AOM_CDF2(19158),   0 },
    { AOM_CDF2( 9621),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(23131),   0 },
    { AOM_CDF2(11693),   0 },
    { AOM_CDF2(12674),   0 },
    { AOM_CDF2( 5134),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(25911),  26 },
    { AOM_CDF2( 8024),  31 },
    { AOM_CDF2(10088),  31 },
    { AOM_CDF2( 2405),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(26386),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(17603),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(25282),   0 },
    { AOM_CDF2(17888),   1 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(22367),   1 },
    { AOM_CDF2(12022),   1 },
    { AOM_CDF2(13435),   1 },
    { AOM_CDF2( 5567),   1 },
    { AOM_CDF2(21597),   1 },
    { AOM_CDF2( 8599),  31 },
    { AOM_CDF2(10530),   1 },
    { AOM_CDF2( 3209),   1 },
  },
};

static aom_cdf_prob default_rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(16632) },
    { AOM_CDF2(14396) },
    { AOM_CDF2(21487) },
    { AOM_CDF2(20206) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(21637) },
    { AOM_CDF2(17534) },
    { AOM_CDF2(25819) },
    { AOM_CDF2(24299) },
    { AOM_CDF2(12980) },
    { AOM_CDF2(9528) },
    { AOM_CDF2(19658) },
    { AOM_CDF2(13569) },
    { AOM_CDF2(15646) },
    { AOM_CDF2(13276) },
    { AOM_CDF2(19640) },
    { AOM_CDF2(18650) },
    { AOM_CDF2(17410) },
    { AOM_CDF2(15421) },
    { AOM_CDF2(23449) },
    { AOM_CDF2(25189) },
    { AOM_CDF2(16394) },
    { AOM_CDF2(11499) },
    { AOM_CDF2(20431) },
    { AOM_CDF2(13172) },
    { AOM_CDF2(19195) },
    { AOM_CDF2(15909) },
    { AOM_CDF2(25010) },
    { AOM_CDF2(23313) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(18586) },
    { AOM_CDF2(13980) },
    { AOM_CDF2(23988) },
    { AOM_CDF2(18083) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(18657) },
    { AOM_CDF2(15920) },
    { AOM_CDF2(21002) },
    { AOM_CDF2(17167) },
    { AOM_CDF2(23113) },
    { AOM_CDF2(19037) },
    { AOM_CDF2(28161) },
    { AOM_CDF2(28060) },
    { AOM_CDF2(8663) },
    { AOM_CDF2(3778) },
    { AOM_CDF2(12277) },
    { AOM_CDF2(3572) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) }
  },
  {
    { AOM_CDF2(17586) },
    { AOM_CDF2(13839) },
    { AOM_CDF2(24154) },
    { AOM_CDF2(18747) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(19774) },
    { AOM_CDF2(14969) },
    { AOM_CDF2(22500) },
    { AOM_CDF2(18783) },
    { AOM_CDF2(14609) },
    { AOM_CDF2(11344) },
    { AOM_CDF2(19697) },
    { AOM_CDF2(15023) },
    { AOM_CDF2(16561) },
    { AOM_CDF2(12673) },
    { AOM_CDF2(20356) },
    { AOM_CDF2(17216) },
    { AOM_CDF2(20586) },
    { AOM_CDF2(16134) },
    { AOM_CDF2(26161) },
    { AOM_CDF2(25255) },
    { AOM_CDF2(13728) },
    { AOM_CDF2(9057) },
    { AOM_CDF2(18008) },
    { AOM_CDF2(12445) },
    { AOM_CDF2(18147) },
    { AOM_CDF2(12282) },
    { AOM_CDF2(23453) },
    { AOM_CDF2(18228) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(20609) },
    { AOM_CDF2(15570) },
    { AOM_CDF2(26474) },
    { AOM_CDF2(24612) },
    { AOM_CDF2(10955) },
    { AOM_CDF2(5180) },
    { AOM_CDF2(14826) },
    { AOM_CDF2(4926) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) },
    { AOM_CDF2(16384) }
  }
};

// Note: For the partition CDFs below, most entries are unused. An optimized
// implementation could create smaller arrays with only used values + some
// mapping tables.
static aom_cdf_prob default_do_ext_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(30374),   0 },
      { AOM_CDF2(26622),   1 },
      { AOM_CDF2(28445),   0 },
      { AOM_CDF2(24420),   5 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(29776),  75 },
      { AOM_CDF2(30081),  75 },
      { AOM_CDF2(26329),  75 },
      { AOM_CDF2(26553),  75 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(29815),  90 },
      { AOM_CDF2(30245),  90 },
      { AOM_CDF2(27811),  90 },
      { AOM_CDF2(29991),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(30529),   6 },
      { AOM_CDF2(22504),   5 },
      { AOM_CDF2(28178),  31 },
      { AOM_CDF2(18091),   1 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(27708),   1 },
      { AOM_CDF2(22616),   2 },
      { AOM_CDF2(22968),   1 },
      { AOM_CDF2(17243),  76 },
      { AOM_CDF2(31652),   6 },
      { AOM_CDF2(29959),  30 },
      { AOM_CDF2(31063),  33 },
      { AOM_CDF2(28323),  25 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(31320),   0 },
      { AOM_CDF2(29764),   0 },
      { AOM_CDF2(28459),   0 },
      { AOM_CDF2(26712),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(30177),  75 },
      { AOM_CDF2(26200),   0 },
      { AOM_CDF2(29284),  75 },
      { AOM_CDF2(25098),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(28439),  76 },
      { AOM_CDF2(24512),  75 },
      { AOM_CDF2(27734),  90 },
      { AOM_CDF2(24235), 123 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(30974),   6 },
      { AOM_CDF2(27002),  30 },
      { AOM_CDF2(26183),  15 },
      { AOM_CDF2(21088),  75 },
      { AOM_CDF2(30924),  34 },
      { AOM_CDF2(29431),  27 },
      { AOM_CDF2(30647),  30 },
      { AOM_CDF2(29578),  30 },
      { AOM_CDF2(29966),   1 },
      { AOM_CDF2(24062),  90 },
      { AOM_CDF2(26597),  75 },
      { AOM_CDF2(23758),  90 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(26567),   0 },
      { AOM_CDF2(25500),  90 },
      { AOM_CDF2(25498),  75 },
      { AOM_CDF2(23040),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(28927),  90 },
      { AOM_CDF2(30473),  90 },
      { AOM_CDF2(28385),  75 },
      { AOM_CDF2(29703),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(26388),   1 },
      { AOM_CDF2(20307),  85 },
      { AOM_CDF2(24514),   1 },
      { AOM_CDF2(18943),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(29401),  75 },
      { AOM_CDF2(28119),  90 },
      { AOM_CDF2(28306),  90 },
      { AOM_CDF2(26664),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(27670),  75 },
      { AOM_CDF2(25041),  75 },
      { AOM_CDF2(27637),  75 },
      { AOM_CDF2(24525), 118 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(27433),   1 },
      { AOM_CDF2(24359),   0 },
      { AOM_CDF2(26879),   7 },
      { AOM_CDF2(21974),  90 },
    },
  },
};

static aom_cdf_prob default_do_uneven_4way_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(25594),  32 },
      { AOM_CDF2(26662),  37 },
      { AOM_CDF2(23258),  31 },
      { AOM_CDF2(25937),  31 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(18463),   1 },
      { AOM_CDF2(21278),   1 },
      { AOM_CDF2(15723),   1 },
      { AOM_CDF2(21518),  76 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(21482),  84 },
      { AOM_CDF2(21465),   5 },
      { AOM_CDF2(17046),   1 },
      { AOM_CDF2(17486),   4 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(17099),   4 },
      { AOM_CDF2(18009), 120 },
      { AOM_CDF2(14770),   2 },
      { AOM_CDF2(12864),   6 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(21820),  36 },
      { AOM_CDF2(18958),  31 },
      { AOM_CDF2(22172),  33 },
      { AOM_CDF2(19650),  31 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(17667),   7 },
      { AOM_CDF2(15004),   1 },
      { AOM_CDF2(21319),   7 },
      { AOM_CDF2(18349),   1 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(18004),   5 },
      { AOM_CDF2(11674),  75 },
      { AOM_CDF2(20247),   1 },
      { AOM_CDF2(12717),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(15650),  11 },
      { AOM_CDF2( 9665),  76 },
      { AOM_CDF2(16485),   5 },
      { AOM_CDF2(12276),  76 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(21791),   7 },
      { AOM_CDF2(20207),  10 },
      { AOM_CDF2(15587),   1 },
      { AOM_CDF2(14737),  75 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(25587),  95 },
      { AOM_CDF2(23971),   0 },
      { AOM_CDF2(19175),   2 },
      { AOM_CDF2(18438),   9 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(17263),   4 },
      { AOM_CDF2(13073),   0 },
      { AOM_CDF2(17558),  10 },
      { AOM_CDF2(13821),  75 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(19913),  92 },
      { AOM_CDF2(14634),  76 },
      { AOM_CDF2(21532), 120 },
      { AOM_CDF2(13431),  90 },
    },
  },
};

static aom_cdf_prob default_uneven_4way_partition_type_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS][CDF_SIZE(NUM_UNEVEN_4WAY_PARTS)] = {
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20145),   7 },
      { AOM_CDF2(17764),  20 },
      { AOM_CDF2(21287),  76 },
      { AOM_CDF2(18390),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20199),  75 },
      { AOM_CDF2(14004),  85 },
      { AOM_CDF2(21802),   1 },
      { AOM_CDF2(12823),   1 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20575),  75 },
      { AOM_CDF2(19346),   0 },
      { AOM_CDF2(11846),  94 },
      { AOM_CDF2(18208),  84 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20629), 120 },
      { AOM_CDF2(13515),  75 },
      { AOM_CDF2(22933),  77 },
      { AOM_CDF2(12920),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(19829),   9 },
      { AOM_CDF2(21829),  75 },
      { AOM_CDF2(17777),  78 },
      { AOM_CDF2(20539),  75 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20670),  91 },
      { AOM_CDF2(21944),  75 },
      { AOM_CDF2(18194),  20 },
      { AOM_CDF2(18027),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(14358),   4 },
      { AOM_CDF2(16024),   1 },
      { AOM_CDF2(20834),  95 },
      { AOM_CDF2(22649),  90 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(14482),  20 },
      { AOM_CDF2(22030),   1 },
      { AOM_CDF2(13087),  95 },
      { AOM_CDF2(25881),  90 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(20800),  20 },
      { AOM_CDF2(16527),  75 },
      { AOM_CDF2(12658),  19 },
      { AOM_CDF2(13199),  82 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(18817),   0 },
      { AOM_CDF2(13405),  75 },
      { AOM_CDF2(19621),  82 },
      { AOM_CDF2(17896),  10 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(15020),  30 },
      { AOM_CDF2(21346),  76 },
      { AOM_CDF2(18912),   0 },
      { AOM_CDF2(18209),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(18928),  95 },
      { AOM_CDF2(21604),  75 },
      { AOM_CDF2(17944), 100 },
      { AOM_CDF2(10374),  90 },
    },
  },
};
#endif  // CONFIG_NEW_PART_CTX

#if !CONFIG_NEW_PART_CTX
 aom_cdf_prob
    default_do_square_split_cdf[PARTITION_STRUCTURE_NUM][SQUARE_SPLIT_CONTEXTS][CDF_SIZE(2)] = {
      // Luma
      {
        // BLOCK_128X128
        { AOM_CDF2(20966), 20 },
        { AOM_CDF2(13180), 30 },
        { AOM_CDF2(11157), 37 },
        { AOM_CDF2( 4258), 6 },
        // BLOCK_256X256
        { AOM_CDF2(20286), 25 },
        { AOM_CDF2(14079), 25 },
        { AOM_CDF2(13942), 35 },
        { AOM_CDF2( 6138), 15 },
      },
      // Chroma
      {
        // BLOCK_128X128
        { AOM_CDF2(24144), 25 },
        { AOM_CDF2(13301), 35 },
        { AOM_CDF2(13196), 35 },
        { AOM_CDF2( 7273), 6 },
        // BLOCK_256X256
        { AOM_CDF2(16384), 0 },
        { AOM_CDF2(16384), 0 },
        { AOM_CDF2(16384), 0 },
        { AOM_CDF2(16384), 0 },
      },
    };
  #endif  // !CONFIG_NEW_PART_CTX

// clang-format on
static const aom_cdf_prob
    default_inter_ext_tx_short_side_cdf[EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(
        4)] = { { { AOM_CDF4(7821, 18687, 24236) },
                  { AOM_CDF4(14442, 26756, 32748) },
                  { AOM_CDF4(16946, 27485, 32748) },
                  { AOM_CDF4(8192, 16384, 24576) } },
                { { AOM_CDF4(20461, 26250, 29309) },
                  { AOM_CDF4(24931, 30589, 32748) },
                  { AOM_CDF4(28078, 31430, 32748) },
                  { AOM_CDF4(8192, 16384, 24576) } },
                { { AOM_CDF4(7593, 15185, 16784) },
                  { AOM_CDF4(17164, 21845, 31208) },
                  { AOM_CDF4(9362, 23406, 28087) },
                  { AOM_CDF4(8192, 16384, 24576) } } };

static const aom_cdf_prob
    default_intra_ext_tx_short_side_cdf[EXT_TX_SIZES][CDF_SIZE(4)] = {
      { AOM_CDF4(11656, 26664, 29603) },
      { AOM_CDF4(22336, 31457, 32748) },
      { AOM_CDF4(24537, 32017, 32748) },
      { AOM_CDF4(8192, 16384, 24576) }
    };

static const aom_cdf_prob default_tx_ext_32_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(67) }, { AOM_CDF2(129) }
};

static const aom_cdf_prob
    default_intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][CDF_SIZE(
        TX_TYPES)] = {
      {
          { 0 },  // unused
          { 0 },  // unused
          { 0 },  // unused
          { 0 },  // unused
      },
      {
#if CONFIG_COEFF_PARSING
          { AOM_CDF7(3910, 13624, 16648, 19644, 23773, 27952), 35 },
          { AOM_CDF7(11788, 21074, 24067, 27345, 29126, 30842), 40 },
          { AOM_CDF7(11068, 21436, 24806, 28312, 29521, 31139), 60 },
#else
          { AOM_CDF7(3910, 13624, 16648, 19644, 23773, 27952), 78 },
          { AOM_CDF7(11788, 21074, 24067, 27345, 29126, 30842), 78 },
          { AOM_CDF7(11068, 21436, 24806, 28312, 29521, 31139), 75 },
#endif
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
      },
      {
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
      },
    };

#if CONFIG_REDUCE_SYMBOL_SIZE
static const aom_cdf_prob
    default_inter_tx_type_set_cdf[2][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(2)] = {
      {
          {
              { AOM_CDF2(20576), 0 },
              { AOM_CDF2(16541), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(16147), 0 },
              { AOM_CDF2(30302), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(25522), 0 },
              { AOM_CDF2(23776), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
      },
      {
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(22866), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(32277), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(29701), 0 },
              { AOM_CDF2(16384), 0 },
          },
      },
    };

static const aom_cdf_prob
    default_inter_tx_type_idx_cdf[2][EOB_TX_CTXS][CDF_SIZE(
        INTER_TX_TYPE_INDEX_COUNT)] = {
      {
          { AOM_CDF8(14380, 16658, 18988, 22077, 25194, 27959, 30950), 0 },
          { AOM_CDF8(442, 561, 737, 1131, 1214, 1494, 1624), 0 },
          { AOM_CDF8(18414, 19897, 21481, 23996, 27098, 29474, 32457), 0 },
      },
      {
          { AOM_CDF8(3612, 5799, 8468, 16149, 20607, 25655, 29211), 0 },
          { AOM_CDF8(135, 279, 923, 31785, 32279, 32421, 32564), 0 },
          { AOM_CDF8(26230, 26927, 28614, 29682, 30095, 30901, 31759), 0 },
      },
    };

static const aom_cdf_prob
    default_inter_tx_type_offset_1_cdf[EOB_TX_CTXS][CDF_SIZE(
        INTER_TX_TYPE_OFFSET1_COUNT)] = {
      { AOM_CDF8(2787, 6375, 8620, 11630, 17688, 22228, 27481), 0 },
      { AOM_CDF8(401, 499, 693, 829, 2141, 2356, 2791), 0 },
      { AOM_CDF8(1984, 5908, 7465, 10371, 17345, 21585, 26968), 0 },
    };

static const aom_cdf_prob
    default_inter_tx_type_offset_2_cdf[EOB_TX_CTXS][CDF_SIZE(
        INTER_TX_TYPE_OFFSET2_COUNT)] = {
      { AOM_CDF4(10244, 16768, 25390), 0 },
      { AOM_CDF4(10230, 16432, 24377), 0 },
      { AOM_CDF4(8301, 19386, 25851), 0 },
    };
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

static const aom_cdf_prob default_inter_ext_tx_cdf
    [EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)] = {
      {
          {
              { 0 },
              { 0 },
              { 0 },
              { 0 },
          },
          {
              { 0 },
              { 0 },
              { 0 },
              { 0 },
          },
          {
              { 0 },
              { 0 },
              { 0 },
              { 0 },
          },
      },
      {
          {
              { AOM_CDF16(9037, 10470, 11932, 13873, 15828, 17558, 19436, 20576,
                          21620, 22970, 23724, 24861, 27101, 28740, 30607),
                75 },
              { AOM_CDF16(4837, 5975, 7101, 8771, 10184, 11652, 13003, 16541,
                          18453, 20761, 22240, 24190, 26752, 28498, 30554),
                75 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
          },
          {
              { AOM_CDF16(2911, 3858, 4826, 5546, 5683, 6221, 6591, 16147,
                          16206, 16256, 16318, 16534, 21589, 24753, 28642),
                6 },
              { AOM_CDF16(409, 519, 682, 1046, 1123, 1381, 1501, 30302, 30605,
                          30678, 30823, 30928, 31926, 32088, 32409),
                1 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
          },
          {
              { AOM_CDF16(14339, 15492, 16743, 18715, 21104, 22943, 25268,
                          25522, 25961, 26821, 27184, 27829, 29338, 30204,
                          31281),
                78 },
              { AOM_CDF16(14549, 15847, 17117, 18394, 19905, 21029, 22382,
                          23776, 24880, 26193, 27197, 28407, 29553, 30713,
                          31757),
                75 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720),
                0 },
          },
      },
      {
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(2522, 4047, 5909, 11268, 14390, 17900, 20374, 22866,
                          25954, 27992, 30446),
                75 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
          },
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(133, 275, 909, 31300, 31753, 31888, 32028, 32277,
                          32529, 32562, 32617),
                32 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
          },
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
              { AOM_CDF12(23775, 24397, 25942, 26888, 27257, 27972, 28802,
                          29701, 30485, 31477, 32168),
                56 },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037),
                0 },
          },
      },
      {
          {
              { AOM_CDF2(5900), 32 },
              { AOM_CDF2(984), 32 },
              { AOM_CDF2(1539), 37 },
              { AOM_CDF2(2809), 36 },
          },
          {
              { AOM_CDF2(751), 77 },
              { AOM_CDF2(19), 102 },
              { AOM_CDF2(27), 120 },
              { AOM_CDF2(20), 104 },
          },
          {
              { AOM_CDF2(23032), 60 },
              { AOM_CDF2(25224), 50 },
              { AOM_CDF2(30401), 50 },
              { AOM_CDF2(31447), 50 },
          },
      },
#if CONFIG_REDUCED_TX_SET_EXT
      {
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
      },
#endif  // CONFIG_REDUCED_TX_SET_EXT
    };

#if CONFIG_REDUCE_CCTX_CTX
static const aom_cdf_prob default_cctx_type_cdf[CDF_SIZE(CCTX_TYPES)] = {
  AOM_CDF7(13038, 14157, 16570, 18922, 21570, 29304), 37
};
#else
static const aom_cdf_prob
    default_cctx_type_cdf[EXT_TX_SIZES][CCTX_CONTEXTS][CDF_SIZE(CCTX_TYPES)] = {
      {
          { AOM_CDF7(13038, 14157, 16570, 18922, 21570, 29304), 37 },
          { AOM_CDF7(7845, 11645, 19580, 30162, 30743, 32105), 37 },
          { AOM_CDF7(7803, 8030, 8539, 8804, 14035, 27508), 31 },
      },
      {
          { AOM_CDF7(15209, 15895, 17250, 18318, 22207, 28985), 37 },
          { AOM_CDF7(8382, 13176, 22153, 29274, 30191, 31824), 32 },
          { AOM_CDF7(8873, 9024, 9380, 9579, 17115, 27581), 32 },
      },
      {
          { AOM_CDF7(13749, 14571, 15851, 17116, 21061, 27694), 62 },
          { AOM_CDF7(6394, 10801, 23941, 30067, 30970, 31965), 62 },
          { AOM_CDF7(6064, 6274, 6575, 6738, 14472, 24699), 37 },
      },
      {
          { AOM_CDF7(15554, 16146, 16650, 17042, 22370, 30311), 62 },
          { AOM_CDF7(16617, 20499, 22518, 25469, 27410, 31370), 62 },
          { AOM_CDF7(8334, 8505, 8837, 8945, 15490, 28506), 62 },
      },
    };
#endif  // CONFIG_REDUCE_CCTX_CTX

static const aom_cdf_prob default_cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)] = {
  AOM_CDF8(5534, 6742, 11998, 19905, 28459, 29805, 32596), 37
};

static const aom_cdf_prob
    default_cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)] = {
      { AOM_CDF8(10366, 17785, 28218, 30893, 32471, 32638, 32666), 62 },
      { AOM_CDF8(4247, 18221, 24527, 31454, 32425, 32695, 32714), 37 },
      { AOM_CDF8(11483, 20769, 27162, 28811, 32007, 32287, 32375), 62 },
      { AOM_CDF8(27996, 31615, 32179, 32454, 32541, 32587, 32607), 62 },
      { AOM_CDF8(18158, 24791, 28870, 29367, 31384, 31714, 32004), 37 },
      { AOM_CDF8(18147, 27954, 31623, 31810, 31958, 32276, 32341), 62 },
    };

static const aom_cdf_prob
    default_switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(
        SWITCHABLE_FILTERS)] = {
      { AOM_CDF3(31476, 32736), 0 }, { AOM_CDF3(1637, 32702), 75 },
      { AOM_CDF3(11, 709), 90 },     { AOM_CDF3(27634, 32442), 6 },
      { AOM_CDF3(30451, 30981), 0 }, { AOM_CDF3(8963, 32500), 6 },
      { AOM_CDF3(370, 693), 75 },    { AOM_CDF3(25697, 27654), 31 },
      { AOM_CDF3(10923, 21845), 0 }, { AOM_CDF3(10923, 21845), 0 },
      { AOM_CDF3(10923, 21845), 0 }, { AOM_CDF3(10923, 21845), 0 },
      { AOM_CDF3(10923, 21845), 0 }, { AOM_CDF3(10923, 21845), 0 },
      { AOM_CDF3(10923, 21845), 0 }, { AOM_CDF3(10923, 21845), 0 },
    };

static const aom_cdf_prob default_is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(11941),
};

static const aom_cdf_prob
    default_inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)] = {
      { AOM_CDF2(31021), 118 }, { AOM_CDF2(25430), 76 },
      { AOM_CDF2(22319), 76 },  { AOM_CDF2(21114), 1 },
      { AOM_CDF2(17583), 1 },
#if !CONFIG_WARP_EXTEND_SIMPLIFICATION
      { AOM_CDF2(14631), 1 },   { AOM_CDF2(13520), 6 },
      { AOM_CDF2(9907), 1 },
#endif  // !CONFIG_WARP_EXTEND_SIMPLIFICATION
    };

static const aom_cdf_prob
    default_inter_single_mode_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
        INTER_SINGLE_MODES)] = {
      { AOM_CDF3(11516, 11715), 0 }, { AOM_CDF3(24656, 24680), 0 },
      { AOM_CDF3(27225, 27243), 0 }, { AOM_CDF3(13331, 13366), 0 },
      { AOM_CDF3(16063, 16170), 0 },
    };

static const aom_cdf_prob default_drl_cdf[3][DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
  { { AOM_CDF2(20581), 118 },
    { AOM_CDF2(25770), 90 },
    { AOM_CDF2(27043), 75 },
    { AOM_CDF2(22024), 118 },
    { AOM_CDF2(16590), 118 } },
  { { AOM_CDF2(20638), 118 },
    { AOM_CDF2(20418), 90 },
    { AOM_CDF2(21113), 115 },
    { AOM_CDF2(19645), 123 },
    { AOM_CDF2(19650), 90 } },
  { { AOM_CDF2(26306), 90 },
    { AOM_CDF2(25139), 115 },
    { AOM_CDF2(23285), 76 },
    { AOM_CDF2(26265), 115 },
    { AOM_CDF2(23464), 118 } }
};

static const aom_cdf_prob default_cwp_idx_cdf[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1]
                                             [CDF_SIZE(2)] = {
                                               {
                                                   { AOM_CDF2(13851), 31 },
                                                   { AOM_CDF2(15058), 31 },
                                                   { AOM_CDF2(21728), 31 },
                                                   { AOM_CDF2(21219), 31 },
                                               },
                                               {
                                                   { AOM_CDF2(16384), 0 },
                                                   { AOM_CDF2(16384), 0 },
                                                   { AOM_CDF2(16384), 0 },
                                                   { AOM_CDF2(16384), 0 },
                                               },
                                             };

static const aom_cdf_prob default_jmvd_scale_mode_cdf[CDF_SIZE(
    JOINT_NEWMV_SCALE_FACTOR_CNT)] = { AOM_CDF5(18498, 21150, 23573, 28129),
                                       1 };

static const aom_cdf_prob default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(
    JOINT_AMVD_SCALE_FACTOR_CNT)] = { AOM_CDF3(24903, 28074), 75 };

static const aom_cdf_prob default_skip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(25396), 0 }, { AOM_CDF2(23881), 0 }, { AOM_CDF2(21508), 0 }
};
static const aom_cdf_prob default_tip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31561) }, { AOM_CDF2(27203) }, { AOM_CDF2(21916) }
};

static const aom_cdf_prob default_use_optflow_cdf[OPFL_MODE_CONTEXTS]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(16384) },
                                                   { AOM_CDF2(16384) },
                                                 };

static const aom_cdf_prob
    default_inter_compound_mode_is_joint_cdf[NUM_CTX_IS_JOINT]
                                            [CDF_SIZE(NUM_OPTIONS_IS_JOINT)] = {
                                              { AOM_CDF2(25397) },
                                              { AOM_CDF2(32752) },
                                            };

static const aom_cdf_prob default_inter_compound_mode_non_joint_type_cdf
    [NUM_CTX_NON_JOINT_TYPE][CDF_SIZE(NUM_OPTIONS_NON_JOINT_TYPE)] = {
      { AOM_CDF5(14431, 21078, 25375, 27262) },
      { AOM_CDF5(22433, 25805, 28788, 30114) },
      { AOM_CDF5(25873, 28046, 30292, 31275) },
      { AOM_CDF5(17660, 22114, 26307, 27200) },
      { AOM_CDF5(21832, 24917, 28451, 28996) }
    };

static const aom_cdf_prob
    default_inter_compound_mode_same_refs_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
        INTER_COMPOUND_SAME_REFS_TYPES)] = { { AOM_CDF4(10155, 29278, 29355) },
                                             { AOM_CDF4(16755, 29980, 30097) },
                                             { AOM_CDF4(20563, 30064, 30344) },
                                             { AOM_CDF4(12042, 28942, 29047) },
                                             { AOM_CDF4(13526, 28071,
                                                        28214) } };

static const aom_cdf_prob default_warp_interintra_cdf[BLOCK_SIZE_GROUPS]
                                                     [CDF_SIZE(2)] = {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                     };

static const aom_cdf_prob default_interintra_cdf[BLOCK_SIZE_GROUPS]
                                                [CDF_SIZE(2)] = {
                                                  { AOM_CDF2(30376), 75 },
                                                  { AOM_CDF2(20784), 1 },
                                                  { AOM_CDF2(22326), 1 },
                                                  { AOM_CDF2(24412), 1 },
                                                };

static const aom_cdf_prob default_interintra_mode_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(5420, 20952, 31034), 7 },
  { AOM_CDF4(1948, 17325, 31146), 75 },
  { AOM_CDF4(3623, 17784, 29374), 1 },
  { AOM_CDF4(2843, 14004, 27752), 7 },
};

static const aom_cdf_prob default_wedge_interintra_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14247), 75
};

static const aom_cdf_prob default_compound_type_cdf[CDF_SIZE(
    MASKED_COMPOUND_TYPES)] = { AOM_CDF2(18804), 6 };

static const aom_cdf_prob
    default_amvd_mode_cdf[NUM_AMVD_MODES][AMVD_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { { AOM_CDF2(11007) }, { AOM_CDF2(11114) }, { AOM_CDF2(10230) } },
      { { AOM_CDF2(11219) }, { AOM_CDF2(10158) }, { AOM_CDF2(10997) } },
      { { AOM_CDF2(8100) }, { AOM_CDF2(6742) }, { AOM_CDF2(9818) } },
      { { AOM_CDF2(7594) }, { AOM_CDF2(6653) }, { AOM_CDF2(7058) } },
      { { AOM_CDF2(17652) }, { AOM_CDF2(14448) }, { AOM_CDF2(13409) } },
      { { AOM_CDF2(14808) }, { AOM_CDF2(13708) }, { AOM_CDF2(13027) } },
      { { AOM_CDF2(8699) }, { AOM_CDF2(7036) }, { AOM_CDF2(7728) } },
      { { AOM_CDF2(9396) }, { AOM_CDF2(8093) }, { AOM_CDF2(10339) } },
      { { AOM_CDF2(13855) }, { AOM_CDF2(11902) }, { AOM_CDF2(11445) } },
    };
/*wedge_angle_dir is first decoded. Depending on the wedge angle_dir, the
 * wedge_angle is decoded. Depending on the wedge_angle, the wedge_dist is
 * decoded.*/
#if CONFIG_REDUCE_SYMBOL_SIZE
static const aom_cdf_prob default_wedge_quad_cdf[CDF_SIZE(WEDGE_QUADS)] = {
  AOM_CDF4(9105, 18210, 25489), 93
};
static const aom_cdf_prob default_wedge_angle_cdf[WEDGE_QUADS][CDF_SIZE(
    QUAD_WEDGE_ANGLES)] = { { AOM_CDF5(6495, 14916, 23085, 27549), 76 },
                            { AOM_CDF5(12000, 15000, 22500, 28500), 76 },
                            { AOM_CDF5(16520, 21143, 25198, 28761), 76 },
                            { AOM_CDF5(13800, 16100, 23000, 27600), 76 } };
#else
static const aom_cdf_prob default_wedge_angle_dir_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(18210), 93
};

static const aom_cdf_prob
    default_wedge_angle_0_cdf[CDF_SIZE(H_WEDGE_ANGLES)] = {
      AOM_CDF10(2165, 4972, 7695, 9183, 11019, 19336, 21240, 25971, 29663), 76
    };
static const aom_cdf_prob default_wedge_angle_1_cdf[CDF_SIZE(
    H_WEDGE_ANGLES)] = {
  AOM_CDF10(9441, 12082, 14399, 16435, 18353, 24264, 25188, 28656, 30548), 76
};
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
static const aom_cdf_prob default_wedge_dist_cdf[CDF_SIZE(NUM_WEDGE_DIST)] = {
  AOM_CDF4(5746, 15860, 20435), 75
};
static const aom_cdf_prob default_wedge_dist_cdf2[CDF_SIZE(
    NUM_WEDGE_DIST - 1)] = { AOM_CDF3(11164, 18454), 90 };

static const aom_cdf_prob default_warp_causal_cdf[WARP_CAUSAL_MODE_CTX]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(17055) },
                                                   { AOM_CDF2(20889) },
                                                   { AOM_CDF2(17980) },
                                                   { AOM_CDF2(17863) }
                                                 };

static const aom_cdf_prob
    default_warp_precision_idx_cdf[BLOCK_SIZES_ALL][CDF_SIZE(
        NUM_WARP_PRECISION_MODES)] = {
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 },
    };

#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
static const aom_cdf_prob default_warp_causal_warpmv_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(20285)
};
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL

static const aom_cdf_prob default_warp_ref_idx0_cdf[WARP_REF_CONTEXTS]
                                                   [CDF_SIZE(2)] = {
                                                     { AOM_CDF2(21704), 90 },
                                                   };
static const aom_cdf_prob default_warp_ref_idx1_cdf[WARP_REF_CONTEXTS]
                                                   [CDF_SIZE(2)] = {
                                                     { AOM_CDF2(23581), 115 },
                                                   };
static const aom_cdf_prob default_warp_ref_idx2_cdf[WARP_REF_CONTEXTS]
                                                   [CDF_SIZE(2)] = {
                                                     { AOM_CDF2(21767), 123 },
                                                   };

static const aom_cdf_prob default_warpmv_with_mvd_flag_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16283),
};

static const aom_cdf_prob
    default_warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUMSYMBOLS_LOW)] = {
      { AOM_CDF8(9210, 20919, 23883, 28837, 29680, 31427, 31670) },
      { AOM_CDF8(12722, 22620, 25595, 29576, 30362, 31750, 31968) }
    };
static const aom_cdf_prob default_warp_delta_param_high_cdf[2][CDF_SIZE(
    WARP_DELTA_NUMSYMBOLS_HIGH)] = {
  { AOM_CDF8(7419, 12373, 17584, 20281, 23728, 27184, 29430) },
  { AOM_CDF8(7417, 12119, 17352, 20027, 23490, 26882, 29213) }
};

static const aom_cdf_prob default_warp_param_sign_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };

static const aom_cdf_prob default_warp_extend_cdf[WARP_EXTEND_CTX][CDF_SIZE(
    2)] = { { AOM_CDF2(20856) }, { AOM_CDF2(18023) }, { AOM_CDF2(16560) } };

static const aom_cdf_prob
    default_refinemv_flag_cdf[NUM_REFINEMV_CTX][CDF_SIZE(2)] = {
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
    };

static const aom_cdf_prob default_bawp_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(27422), 1 },
  { AOM_CDF2(15131), 6 },
};

static const aom_cdf_prob
    default_explicit_bawp_cdf[BAWP_SCALES_CTX_COUNT][CDF_SIZE(2)] = {
      { AOM_CDF2(19664) }, { AOM_CDF2(21664) }, { AOM_CDF2(23664) }
    };
static const aom_cdf_prob default_explicit_bawp_scale_cdf[CDF_SIZE(
    EXPLICIT_BAWP_SCALE_CNT)] = { AOM_CDF2(16384) };

static const aom_cdf_prob default_intra_inter_cdf[INTRA_INTER_CONTEXTS]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(2375), 75 },
                                                   { AOM_CDF2(16902), 75 },
                                                   { AOM_CDF2(16384), 0 },
                                                   { AOM_CDF2(29584), 0 },
                                                 };

static const aom_cdf_prob default_tip_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31852), 118 },
  { AOM_CDF2(18438), 90 },
  { AOM_CDF2(8752), 90 },
};

static const aom_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(
    TIP_PRED_MODES)] = { AOM_CDF2(29069), 0 };

static const aom_cdf_prob
    default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(27078), 75 }, { AOM_CDF2(22913), 1 }, { AOM_CDF2(15254), 1 },
      { AOM_CDF2(13473), 1 },  { AOM_CDF2(5765), 0 },
    };

static const aom_cdf_prob default_single_ref_cdf[REF_CONTEXTS]
                                                [INTER_REFS_PER_FRAME - 1]
                                                [CDF_SIZE(2)] = {
                                                  {
                                                      { AOM_CDF2(27505), 0 },
                                                      { AOM_CDF2(26743), 0 },
                                                      { AOM_CDF2(29193), 75 },
                                                      { AOM_CDF2(29517), 0 },
                                                      { AOM_CDF2(30241), 0 },
                                                      { AOM_CDF2(30024), 1 },
                                                  },
                                                  {
                                                      { AOM_CDF2(17869), 1 },
                                                      { AOM_CDF2(16112), 6 },
                                                      { AOM_CDF2(19968), 6 },
                                                      { AOM_CDF2(17247), 31 },
                                                      { AOM_CDF2(17293), 32 },
                                                      { AOM_CDF2(11155), 32 },
                                                  },
                                                  {
                                                      { AOM_CDF2(6276), 0 },
                                                      { AOM_CDF2(5153), 0 },
                                                      { AOM_CDF2(6631), 1 },
                                                      { AOM_CDF2(4257), 6 },
                                                      { AOM_CDF2(3798), 1 },
                                                      { AOM_CDF2(1983), 0 },
                                                  },
                                                };

static const aom_cdf_prob default_comp_ref0_cdf[REF_CONTEXTS]
                                               [INTER_REFS_PER_FRAME - 1]
                                               [CDF_SIZE(2)] = {
                                                 {
                                                     { AOM_CDF2(11015), 32 },
                                                     { AOM_CDF2(14938), 32 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                 },
                                                 {
                                                     { AOM_CDF2(1829), 75 },
                                                     { AOM_CDF2(3838), 6 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                 },
                                                 {
                                                     { AOM_CDF2(1233), 75 },
                                                     { AOM_CDF2(1491), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                     { AOM_CDF2(16384), 0 },
                                                 },
                                               };

static const aom_cdf_prob
    default_comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
                         [INTER_REFS_PER_FRAME - 1][CDF_SIZE(2)] = {
                           {
                               {
                                   { AOM_CDF2(31947), 115 },
                                   { AOM_CDF2(29267), 5 },
                                   { AOM_CDF2(29617), 1 },
                                   { AOM_CDF2(30617), 26 },
                                   { AOM_CDF2(31720), 8 },
                                   { AOM_CDF2(31208), 32 },
                               },
                               {
                                   { AOM_CDF2(16384), 0 },
                                   { AOM_CDF2(19512), 31 },
                                   { AOM_CDF2(28511), 1 },
                                   { AOM_CDF2(27987), 31 },
                                   { AOM_CDF2(29764), 30 },
                                   { AOM_CDF2(29015), 51 },
                               },
                           },
                           {
                               {
                                   { AOM_CDF2(31833), 93 },
                                   { AOM_CDF2(26128), 31 },
                                   { AOM_CDF2(21282), 31 },
                                   { AOM_CDF2(18036), 57 },
                                   { AOM_CDF2(21050), 32 },
                                   { AOM_CDF2(14939), 57 },
                               },
                               {
                                   { AOM_CDF2(16384), 0 },
                                   { AOM_CDF2(7402), 1 },
                                   { AOM_CDF2(16893), 6 },
                                   { AOM_CDF2(13997), 31 },
                                   { AOM_CDF2(13067), 31 },
                                   { AOM_CDF2(7202), 57 },
                               },
                           },
                           {
                               {
                                   { AOM_CDF2(26394), 31 },
                                   { AOM_CDF2(15795), 31 },
                                   { AOM_CDF2(6816), 31 },
                                   { AOM_CDF2(3530), 57 },
                                   { AOM_CDF2(5621), 59 },
                                   { AOM_CDF2(2839), 71 },
                               },
                               {
                                   { AOM_CDF2(16384), 0 },
                                   { AOM_CDF2(1576), 93 },
                                   { AOM_CDF2(5081), 1 },
                                   { AOM_CDF2(2205), 6 },
                                   { AOM_CDF2(1859), 6 },
                                   { AOM_CDF2(925), 6 },
                               },
                           },
                         };

static const aom_cdf_prob default_palette_y_size_cdf[CDF_SIZE(7)] = {
  AOM_CDF7(7882, 14719, 20582, 24895, 28455, 30765), 70
};

static const aom_cdf_prob default_palette_uv_size_cdf[CDF_SIZE(7)] = {
  AOM_CDF7(8195, 19832, 25803, 29025, 30747, 31974), 50
};

const aom_cdf_prob default_palette_y_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(24488),
                                                               55 };

static const aom_cdf_prob default_palette_uv_mode_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(30790), 50
};

static const aom_cdf_prob
    default_identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AOM_CDF3(10923, 21845), 25 },
      { AOM_CDF3(10923, 21845), 31 },
      { AOM_CDF3(10923, 21845), 33 },
      { AOM_CDF3(10923, 21845), 36 },
    };
static const aom_cdf_prob
    default_identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AOM_CDF3(10923, 21845), 32 },
      { AOM_CDF3(10923, 21845), 57 },
      { AOM_CDF3(10923, 21845), 30 },
      { AOM_CDF3(10923, 21845), 56 },
    };
#if !CONFIG_PLT_DIR_CTX
static const aom_cdf_prob default_palette_direction_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(21697), 6
};
#endif  // !CONFIG_PLT_DIR_CTX

static const aom_cdf_prob default_palette_y_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(27953), 120 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(8822), 60 },
          { AOM_CDF2(27664), 25 },
          { AOM_CDF2(30470), 0 },
      },
      {
          { AOM_CDF3(25275, 28972), 70 },
          { AOM_CDF3(12296, 25651), 85 },
          { AOM_CDF3(7235, 28020), 45 },
          { AOM_CDF3(20976, 25896), 50 },
          { AOM_CDF3(30329, 31531), 60 },
      },
      {
          { AOM_CDF4(23381, 26612, 28974), 70 },
          { AOM_CDF4(10717, 22776, 27574), 70 },
          { AOM_CDF4(6423, 25697, 29375), 110 },
          { AOM_CDF4(19518, 23623, 28343), 60 },
          { AOM_CDF4(30332, 31099, 31857), 50 },
      },
      {
          { AOM_CDF5(23985, 26707, 28381, 29936), 60 },
          { AOM_CDF5(9530, 19953, 23789, 28724), 60 },
          { AOM_CDF5(6047, 24926, 27833, 30371), 0 },
          { AOM_CDF5(20148, 23267, 27043, 28890), 70 },
          { AOM_CDF5(30829, 31286, 31746, 32197), 0 },
      },
      {
          { AOM_CDF6(21093, 24342, 26015, 27567, 29307), 0 },
          { AOM_CDF6(8394, 18615, 21558, 26002, 28573), 50 },
          { AOM_CDF6(5813, 23913, 26226, 28289, 30468), 10 },
          { AOM_CDF6(19001, 22337, 25424, 26705, 28663), 50 },
          { AOM_CDF6(30558, 31067, 31403, 31756, 32196), 60 },
      },
      {
          { AOM_CDF7(22109, 24619, 25982, 27240, 28446, 29822), 60 },
          { AOM_CDF7(8462, 18350, 20440, 25007, 26774, 28765), 0 },
          { AOM_CDF7(6112, 23012, 25022, 26868, 28683, 30708), 0 },
          { AOM_CDF7(22602, 24933, 27205, 28064, 28697, 29814), 100 },
          { AOM_CDF7(30803, 31187, 31462, 31737, 32020, 32331), 50 },
      },
      {
          { AOM_CDF8(22076, 24783, 25777, 26792, 27691, 28748, 29942), 50 },
          { AOM_CDF8(8293, 17397, 19329, 23699, 25608, 27188, 28865), 0 },
          { AOM_CDF8(7220, 21444, 23642, 25420, 27182, 28981, 30832), 0 },
          { AOM_CDF8(23128, 24999, 27546, 28542, 29286, 29744, 30499), 50 },
          { AOM_CDF8(30626, 30964, 31219, 31444, 31661, 31956, 32318), 10 },
      },
    };

static const aom_cdf_prob
    default_txfm_do_partition_cdf[FSC_MODES][2][TXFM_SPLIT_GROUP][CDF_SIZE(
        2)] = { { { { AOM_CDF2(16673) },
                    { AOM_CDF2(32057) },
                    { AOM_CDF2(21412) },
                    { AOM_CDF2(21101) },
                    { AOM_CDF2(15987) },
                    { AOM_CDF2(17681) },
                    { AOM_CDF2(15272) },
                    { AOM_CDF2(16944) },
                    { AOM_CDF2(15490) } },
                  { { AOM_CDF2(24996) },
                    { AOM_CDF2(29488) },
                    { AOM_CDF2(28130) },
                    { AOM_CDF2(25285) },
                    { AOM_CDF2(26292) },
                    { AOM_CDF2(26699) },
                    { AOM_CDF2(26963) },
                    { AOM_CDF2(28550) },
                    { AOM_CDF2(26414) } } },
                { { { AOM_CDF2(26810) },
                    { AOM_CDF2(32361) },
                    { AOM_CDF2(28087) },
                    { AOM_CDF2(25777) },
                    { AOM_CDF2(24932) },
                    { AOM_CDF2(26103) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(25753) } },
                  { { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) },
                    { AOM_CDF2(16384) } } } };

static const aom_cdf_prob default_txfm_4way_partition_type_cdf
    [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
    [CDF_SIZE(TX_PARTITION_TYPE_NUM)] = {
      { { { AOM_CDF7(32414, 32473, 32532, 32591, 32650, 32709) },
          { AOM_CDF7(2876, 18313, 22208, 28264, 28268, 32724) },
          { AOM_CDF7(3923, 9350, 23275, 23279, 27495, 27499) },
          { AOM_CDF7(6028, 14813, 20346, 23777, 26326, 29951) },
          { AOM_CDF7(3202, 14728, 18086, 23666, 25309, 30384) },
          { AOM_CDF7(4109, 8558, 19171, 20786, 24636, 27471) },
          { AOM_CDF7(6859, 14500, 20365, 23744, 26424, 29870) },
          { AOM_CDF7(3391, 11845, 16481, 21478, 25770, 30485) },
          { AOM_CDF7(3524, 10297, 17893, 21778, 25140, 28189) },
          { AOM_CDF7(4258, 11816, 17368, 23181, 27551, 30381) },
          { AOM_CDF7(2794, 12996, 16651, 28785, 28789, 32724) },
          { AOM_CDF7(2464, 6875, 17359, 17363, 27958, 27962) },
          { AOM_CDF7(2471, 11105, 15103, 24759, 27894, 31175) },
          { AOM_CDF7(2735, 7793, 16212, 19301, 27502, 29337) } },
        { { AOM_CDF7(13540, 21123, 31685, 31956, 32226, 32497) },
          { AOM_CDF7(7415, 15547, 20331, 27028, 27267, 32529) },
          { AOM_CDF7(4894, 11490, 20427, 20640, 27023, 27236) },
          { AOM_CDF7(5041, 10187, 18064, 22265, 25941, 29092) },
          { AOM_CDF7(7009, 11600, 13266, 19645, 22915, 29904) },
          { AOM_CDF7(6932, 8188, 12803, 16517, 24421, 26826) },
          { AOM_CDF7(8947, 12382, 16998, 19382, 22593, 27492) },
          { AOM_CDF7(5896, 9005, 11167, 15812, 20029, 29177) },
          { AOM_CDF7(4670, 7051, 9974, 13755, 20083, 23804) },
          { AOM_CDF7(4285, 7699, 11562, 17252, 23015, 27159) },
          { AOM_CDF7(5364, 12080, 18435, 28666, 28711, 32723) },
          { AOM_CDF7(3916, 11644, 16796, 16831, 29161, 29196) },
          { AOM_CDF7(2763, 5940, 6963, 17047, 23789, 30088) },
          { AOM_CDF7(2490, 3973, 6413, 14837, 24746, 27462) } } },
      { { { AOM_CDF7(8192, 12288, 16384, 20480, 24576, 28672) },
          { AOM_CDF7(1425, 11398, 19946, 24220, 25645, 31343) },
          { AOM_CDF7(2621, 11796, 27525, 28836, 30147, 31457) },
          { AOM_CDF7(1489, 13405, 23831, 25321, 29789, 31279) },
          { AOM_CDF7(2185, 15292, 19661, 26214, 28399, 30583) },
          { AOM_CDF7(3277, 16384, 22938, 26214, 29491, 31130) },
          { AOM_CDF7(5461, 16384, 21845, 25486, 27307, 29127) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(2185, 8738, 21845, 24030, 26214, 30583) },
          { AOM_CDF7(4681, 18725, 26526, 28087, 29647, 31208) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) } },
        { { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) } } }
    };

// default CDF for the reduced transform partition mode
static const aom_cdf_prob default_txfm_4way_partition_type_reduced_cdf
    [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
    [CDF_SIZE(TX_PARTITION_TYPE_NUM)] = {
      {
          {
              { AOM_CDF7(32414, 32473, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2876, 18313, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3923, 9350, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(6028, 14813, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3202, 14728, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(4109, 8558, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(6859, 14500, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3391, 11845, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3524, 10297, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(4258, 11816, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2794, 12996, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2464, 6875, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(4942, 22210, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(5470, 15586, 32532, 32591, 32650, 32709) },
          },
          {
              { AOM_CDF7(13540, 21123, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(7415, 15547, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(4894, 11490, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(5041, 10187, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(14018, 23200, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(13864, 16376, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(8947, 12382, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(11792, 18010, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(14010, 21153, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(8570, 15398, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(5364, 12080, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3916, 11644, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(11052, 23760, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(12450, 19865, 32532, 32591, 32650, 32709) },
          },
      },
      {
          {
              { AOM_CDF7(8192, 12288, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(1425, 11398, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2621, 11796, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(1489, 13405, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2185, 15292, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(3277, 16384, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(5461, 16384, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(2185, 8738, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(4681, 18725, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
          },
          {
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
              { AOM_CDF7(9362, 18724, 32532, 32591, 32650, 32709) },
          },
      },
    };

static const aom_cdf_prob default_txfm_2or3_way_partition_type_cdf
    [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1][CDF_SIZE(2)] = {
      { { { AOM_CDF2(19001) }, { AOM_CDF2(19541) } },
        { { AOM_CDF2(18801) }, { AOM_CDF2(18916) } } },
      { { { AOM_CDF2(23406) }, { AOM_CDF2(28399) } },
        { { AOM_CDF2(16384) }, { AOM_CDF2(16384) } } }
    };

static const aom_cdf_prob
    default_lossless_tx_size_cdf[BLOCK_SIZE_GROUPS][2][CDF_SIZE(2)] = {
      {
          { AOM_CDF2(16384), 1 },
          { AOM_CDF2(16384), 1 },
      },
      {
          { AOM_CDF2(16384), 75 },
          { AOM_CDF2(16384), 75 },
      },
      {
          { AOM_CDF2(16384), 75 },
          { AOM_CDF2(16384), 75 },
      },
      {
          { AOM_CDF2(16384), 75 },
          { AOM_CDF2(16384), 75 },
      },
    };
static const aom_cdf_prob default_lossless_inter_tx_type_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384), 75
};

static const aom_cdf_prob default_skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(23601), 0 }, { AOM_CDF2(12657), 75 }, { AOM_CDF2(3777), 90 },
  { AOM_CDF2(23222), 1 }, { AOM_CDF2(8799), 76 },  { AOM_CDF2(1437), 90 },
};

static const aom_cdf_prob default_bru_mode_cdf[CDF_SIZE(3)] = { AOM_CDF3(
    4124, 16615) };

static const aom_cdf_prob default_skip_mode_cdfs[SKIP_MODE_CONTEXTS]
                                                [CDF_SIZE(2)] = {
                                                  { AOM_CDF2(31439), 0 },
                                                  { AOM_CDF2(22696), 0 },
                                                  { AOM_CDF2(12045), 0 },
                                                };

static const aom_cdf_prob
    default_comp_group_idx_cdfs[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(9916), 0 },  { AOM_CDF2(7647), 0 }, { AOM_CDF2(4172), 0 },
      { AOM_CDF2(6353), 1 },  { AOM_CDF2(3423), 0 }, { AOM_CDF2(4917), 6 },
      { AOM_CDF2(11013), 0 }, { AOM_CDF2(5843), 0 }, { AOM_CDF2(7222), 1 },
      { AOM_CDF2(3158), 1 },  { AOM_CDF2(2495), 0 }, { AOM_CDF2(4723), 3 },
    };

static const aom_cdf_prob default_intrabc_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(30958), 1 },
  { AOM_CDF2(19490), 0 },
  { AOM_CDF2(8708), 90 },
};

static const aom_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(26560), 31
};

#if !CONFIG_BYPASS_INTRABC_DRL_IDX
static const aom_cdf_prob default_intrabc_drl_idx_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(22959), 123 },
  { AOM_CDF2(19303), 123 },
  { AOM_CDF2(18859), 124 },
};
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX

aom_cdf_prob
    default_intrabc_bv_precision_cdf[NUM_BV_PRECISION_CONTEXTS]
                                    [CDF_SIZE(NUM_ALLOWED_BV_PRECISIONS)] = {
                                      { AOM_CDF2(24576), 0 },
                                    };

static const aom_cdf_prob default_morph_pred_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(19186), 50 },
  { AOM_CDF2(16483), 1 },
  { AOM_CDF2(8242), 95 },
};

static const aom_cdf_prob
    default_switchable_flex_restore_cdf[MAX_LR_FLEX_SWITCHABLE_BITS]
                                       [MAX_MB_PLANE][CDF_SIZE(2)] = {
                                         {
                                             { AOM_CDF2(21337), 37 },
                                             { AOM_CDF2(13763), 32 },
                                             { AOM_CDF2(13677), 37 },
                                         },
                                         {
                                             { AOM_CDF2(20429), 37 },
                                             { AOM_CDF2(22496), 37 },
                                             { AOM_CDF2(18867), 37 },
                                         },
                                         {
                                             { AOM_CDF2(19205), 37 },
                                             { AOM_CDF2(16384), 0 },
                                             { AOM_CDF2(16384), 0 },
                                         },
                                       };

static const aom_cdf_prob default_ccso_cdf[3][CCSO_CONTEXT][CDF_SIZE(2)] = {
  { { AOM_CDF2(24690), 37 },
    { AOM_CDF2(17161), 37 },
    { AOM_CDF2(10618), 37 },
    { AOM_CDF2(7830), 37 } },
  { { AOM_CDF2(26090), 37 },
    { AOM_CDF2(18122), 37 },
    { AOM_CDF2(12248), 37 },
    { AOM_CDF2(8523), 37 } },
  { { AOM_CDF2(27240), 37 },
    { AOM_CDF2(18679), 37 },
    { AOM_CDF2(10831), 37 },
    { AOM_CDF2(7846), 37 } }
};

static const aom_cdf_prob
    default_cdef_strength_index0_cdf[CDEF_STRENGTH_INDEX0_CTX][CDF_SIZE(2)] = {
      { AOM_CDF2(24690), 37 },
      { AOM_CDF2(17161), 37 },
      { AOM_CDF2(10618), 37 },
      { AOM_CDF2(7830), 37 }
    };

static const aom_cdf_prob
    default_cdef_cdf[CDEF_STRENGTHS_NUM - 1][CDF_SIZE(CDEF_STRENGTHS_NUM)] = {
      { AOM_CDF2(16384) },
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF5(6554, 13107, 19661, 26214) },
      { AOM_CDF6(5461, 10923, 16384, 21845, 27307) },
      { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
    };

static const aom_cdf_prob default_gdf_cdf[CDF_SIZE(2)] = { AOM_CDF2(11570) };

static const aom_cdf_prob default_wienerns_length_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(16384), 61 },
  { AOM_CDF2(16384), 31 },
};
static const aom_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384), 57
};

static const aom_cdf_prob
    default_wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)] = {
      { AOM_CDF4(16384, 24576, 28672), 7 },
    };

static const aom_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(6995), 37
};

static const aom_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14330), 8
};
#if !CONFIG_MERGE_PARA_CTX
static const aom_cdf_prob default_merged_param_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14319), 7
};
#endif  // !CONFIG_MERGE_PARA_CTX

static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)] = {
  AOM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613), 56
};

static const aom_cdf_prob default_delta_lf_multi_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
};

static const aom_cdf_prob default_delta_lf_cdf[CDF_SIZE(4)] = {
  AOM_CDF4(8192, 16384, 24576), 0
};

#if CONFIG_EXT_SEG
static const aom_cdf_prob default_seg_tree_cdf[CDF_SIZE(MAX_SEGMENTS_8)] = {
  AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672)
};
static const aom_cdf_prob default_seg_tree_cdf1[CDF_SIZE(MAX_SEGMENTS_8)] = {
  AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672)
};
#else   // CONFIG_EXT_SEG
static const aom_cdf_prob default_seg_tree_cdf[CDF_SIZE(MAX_SEGMENTS)] = {
  AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672)
};
#endif  // CONFIG_EXT_SEG

static const aom_cdf_prob
    default_segment_pred_cdf[SEG_TEMPORAL_PRED_CTXS][CDF_SIZE(2)] = {
      { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }
    };

static const aom_cdf_prob
#if CONFIG_EXT_SEG
    default_spatial_pred_seg_tree_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
        MAX_SEGMENTS_8)] =
#else   // CONFIG_EXT_SEG
    default_spatial_pred_seg_tree_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
        MAX_SEGMENTS)] =
#endif  // CONFIG_EXT_SEG
        {
          {
              AOM_CDF8(5622, 7893, 16093, 18233, 27809, 28373, 32533),
          },
          {
              AOM_CDF8(14274, 18230, 22557, 24935, 29980, 30851, 32344),
          },
          {
              AOM_CDF8(27527, 28487, 28723, 28890, 32397, 32647, 32679),
          },
        };

#if CONFIG_EXT_SEG
static const aom_cdf_prob
    default_spatial_pred_seg_tree_cdf1[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
        MAX_SEGMENTS_8)] = {
      {
          AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
      },
      {
          AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
      },
      {
          AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
      },
    };

static const aom_cdf_prob
    default_seg_id_ext_flag_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(2)] = {
      { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }
    };
#endif  // CONFIG_EXT_SEG

static const aom_cdf_prob default_stx_cdf[2][TX_SIZES][CDF_SIZE(STX_TYPES)] = {
  {
      { AOM_CDF4(293, 11683, 25053), 75 },
      { AOM_CDF4(2952, 9945, 16750), 0 },
      { AOM_CDF4(2684, 9484, 16065), 75 },
      { AOM_CDF4(3552, 10398, 15130), 75 },
      { AOM_CDF4(10685, 14127, 17177), 1 },
  },
  {
      { AOM_CDF4(293, 11683, 25053), 0 },
      { AOM_CDF4(2952, 9945, 16750), 0 },
      { AOM_CDF4(2684, 9484, 16065), 6 },
      { AOM_CDF4(3552, 10398, 15130), 6 },
      { AOM_CDF4(10685, 14127, 17177), 31 },
  },
};

static const aom_cdf_prob
    default_most_probable_stx_set_cdf[CDF_SIZE(IST_DIR_SIZE)] = {
      AOM_CDF7(16328, 21408, 25613, 27672, 29722, 31413),
      75,
    };
static const aom_cdf_prob default_most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(
    IST_REDUCE_SET_SIZE_ADST_ADST)] = {
  AOM_CDF4(16328, 21408, 25613),
  75,
};

static const aom_cdf_prob
    default_pb_mv_most_probable_precision_cdf[NUM_MV_PREC_MPP_CONTEXT]
                                             [CDF_SIZE(2)] = {
                                               { AOM_CDF2(27840), 0 },
                                               { AOM_CDF2(23276), 1 },
                                               { AOM_CDF2(14105), 0 },
                                             };
static const aom_cdf_prob
    default_pb_mv_precision_cdf[MV_PREC_DOWN_CONTEXTS]
                               [NUM_PB_FLEX_QUALIFIED_MAX_PREC]
                               [CDF_SIZE(FLEX_MV_COSTS_SIZE)] = {
                                 {
                                     { AOM_CDF3(10923, 21845), 0 },
                                     { AOM_CDF3(30680, 31861), 78 },
                                     { AOM_CDF3(21154, 31023), 0 },
                                 },
                                 {
                                     { AOM_CDF3(10923, 21845), 0 },
                                     { AOM_CDF3(31613, 32191), 78 },
                                     { AOM_CDF3(25484, 32287), 75 },
                                 },
                               };

#define MAX_COLOR_CONTEXT_HASH 8

#define NUM_PALETTE_NEIGHBORS 3  // left, top-left and top.

static INLINE void swap_color_order(uint8_t *color_order,
                                    uint8_t *color_order_status, int switch_idx,
                                    int max_idx, int *color_order_cnt) {
  color_order[switch_idx] = max_idx;
  color_order_status[max_idx] = 1;
  (*color_order_cnt)++;
}

static INLINE int derive_color_index_ctx(uint8_t *color_order, int *color_idx,
                                         const uint8_t *color_map, int stride,
                                         int r, int c) {
  int color_index_ctx = 0;
  uint8_t color_status[PALETTE_MAX_SIZE] = { 0 };
  int color_cnt = 0;
  for (int j = 0; j < PALETTE_MAX_SIZE; ++j) {
    color_order[j] = j;
  }

  if (r > 0 && c > 0) {
    int color_neighbors[3] = { 0 };
    color_neighbors[0] = color_map[r * stride + c - 1];
    color_neighbors[1] = color_map[(r - 1) * stride + c - 1];
    color_neighbors[2] = color_map[(r - 1) * stride + c];

    if (color_neighbors[0] == color_neighbors[1] &&
        color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 4;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 3;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[1],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[1]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[2],
                       &color_cnt);
    } else if (color_neighbors[1] == color_neighbors[2]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[2],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[0],
                       &color_cnt);
    } else {
      color_index_ctx = 1;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[2],
                       &color_cnt);
      swap_color_order(color_order, color_status, 2, color_neighbors[1],
                       &color_cnt);
    }
  } else if (c == 0 && r > 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[(r - 1) * stride + c];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  } else if (c > 0 && r == 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[r * stride + c - 1];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  }

  int write_idx = color_cnt;
  for (int read_idx = 0; read_idx < PALETTE_MAX_SIZE; read_idx++) {
    if (color_status[read_idx] == 0) {
      color_order[write_idx] = read_idx;
      write_idx++;
    }
  }

  if (color_idx != NULL) {
    // If any of the neighbor color has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbor
    const int current_color = *color_idx = color_map[r * stride + c];
    for (int idx = 0; idx < PALETTE_MAX_SIZE; idx++) {
      if (color_order[idx] == current_color) {
        *color_idx = idx;
        break;
      }
    }
  }
  return color_index_ctx;
}

int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, uint8_t *color_order,
                                        int *color_idx) {
  assert(r > 0 || c > 0);

  int color_index_ctx =
      derive_color_index_ctx(color_order, color_idx, color_map, stride, r, c);
  return color_index_ctx;
}

#undef NUM_PALETTE_NEIGHBORS
#undef MAX_COLOR_CONTEXT_HASH

static void init_mode_probs(FRAME_CONTEXT *fc,
                            const SequenceHeader *const seq_params) {
  (void)seq_params;
  av1_copy(fc->palette_y_size_cdf, default_palette_y_size_cdf);
  av1_copy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf);
  av1_copy(fc->identity_row_cdf_y, default_identity_row_cdf_y);
  av1_copy(fc->identity_row_cdf_uv, default_identity_row_cdf_uv);
#if !CONFIG_PLT_DIR_CTX
  av1_copy(fc->palette_direction_cdf, default_palette_direction_cdf);
#endif  // !CONFIG_PLT_DIR_CTX
  av1_copy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf);
  av1_copy(fc->comp_inter_cdf, default_comp_inter_cdf);
  av1_copy(fc->tip_cdf, default_tip_cdf);
  av1_copy(fc->tip_pred_mode_cdf, default_tip_pred_mode_cdf);
  av1_copy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf);
  av1_copy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf);
  av1_copy(fc->single_ref_cdf, default_single_ref_cdf);
  av1_copy(fc->comp_ref0_cdf, default_comp_ref0_cdf);
  av1_copy(fc->comp_ref1_cdf, default_comp_ref1_cdf);
  av1_copy(fc->txfm_do_partition_cdf, default_txfm_do_partition_cdf);
  av1_copy(fc->txfm_2or3_way_partition_type_cdf,
           default_txfm_2or3_way_partition_type_cdf);
  av1_copy(fc->txfm_4way_partition_type_cdf,
           default_txfm_4way_partition_type_cdf);
  if (seq_params->reduced_tx_part_set) {
    av1_copy(fc->txfm_4way_partition_type_cdf,
             default_txfm_4way_partition_type_reduced_cdf);
  }
  av1_copy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs);
  av1_copy(fc->inter_single_mode_cdf, default_inter_single_mode_cdf);

  av1_copy(fc->inter_warp_mode_cdf, default_inter_warp_mode_cdf);
  av1_copy(fc->is_warpmv_or_warp_newmv_cdf,
           default_is_warpmv_or_warp_newmv_cdf);

  av1_copy(fc->drl_cdf, default_drl_cdf);

  av1_copy(fc->refinemv_flag_cdf, default_refinemv_flag_cdf);
  av1_copy(fc->warp_causal_cdf, default_warp_causal_cdf);
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  av1_copy(fc->warp_causal_warpmv_cdf, default_warp_causal_warpmv_cdf);
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  av1_copy(fc->warp_ref_idx_cdf[0], default_warp_ref_idx0_cdf);
  av1_copy(fc->warp_ref_idx_cdf[1], default_warp_ref_idx1_cdf);
  av1_copy(fc->warp_ref_idx_cdf[2], default_warp_ref_idx2_cdf);
  av1_copy(fc->warpmv_with_mvd_flag_cdf, default_warpmv_with_mvd_flag_cdf);
  av1_copy(fc->warp_precision_idx_cdf, default_warp_precision_idx_cdf);

  av1_copy(fc->warp_delta_param_cdf, default_warp_delta_param_cdf);
  av1_copy(fc->warp_delta_param_high_cdf, default_warp_delta_param_high_cdf);
  av1_copy(fc->warp_param_sign_cdf, default_warp_param_sign_cdf);
  av1_copy(fc->warp_extend_cdf, default_warp_extend_cdf);
  av1_copy(fc->skip_drl_cdf, default_skip_drl_cdf);
  av1_copy(fc->tip_drl_cdf, default_tip_drl_cdf);
  av1_copy(fc->bawp_cdf[0], default_bawp_cdf[0]);
  av1_copy(fc->bawp_cdf[1], default_bawp_cdf[1]);
  av1_copy(fc->explicit_bawp_cdf, default_explicit_bawp_cdf);
  av1_copy(fc->explicit_bawp_scale_cdf, default_explicit_bawp_scale_cdf);
  av1_copy(fc->use_optflow_cdf, default_use_optflow_cdf);

  av1_copy(fc->cwp_idx_cdf, default_cwp_idx_cdf);
  av1_copy(fc->jmvd_scale_mode_cdf, default_jmvd_scale_mode_cdf);
  av1_copy(fc->jmvd_amvd_scale_mode_cdf, default_jmvd_amvd_scale_mode_cdf);

  av1_copy(fc->inter_compound_mode_is_joint_cdf,
           default_inter_compound_mode_is_joint_cdf);
  av1_copy(fc->inter_compound_mode_non_joint_type_cdf,
           default_inter_compound_mode_non_joint_type_cdf);

  av1_copy(fc->inter_compound_mode_same_refs_cdf,
           default_inter_compound_mode_same_refs_cdf);
  av1_copy(fc->compound_type_cdf, default_compound_type_cdf);
  av1_copy(fc->amvd_mode_cdf, default_amvd_mode_cdf);
#if CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->wedge_quad_cdf, default_wedge_quad_cdf);
  av1_copy(fc->wedge_angle_cdf, default_wedge_angle_cdf);
#else
  av1_copy(fc->wedge_angle_dir_cdf, default_wedge_angle_dir_cdf);
  av1_copy(fc->wedge_angle_0_cdf, default_wedge_angle_0_cdf);
  av1_copy(fc->wedge_angle_1_cdf, default_wedge_angle_1_cdf);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->wedge_dist_cdf, default_wedge_dist_cdf);
  av1_copy(fc->wedge_dist_cdf2, default_wedge_dist_cdf2);
  av1_copy(fc->interintra_cdf, default_interintra_cdf);
  av1_copy(fc->warp_interintra_cdf, default_warp_interintra_cdf);
  av1_copy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf);
  av1_copy(fc->interintra_mode_cdf, default_interintra_mode_cdf);
  av1_copy(fc->seg.pred_cdf, default_segment_pred_cdf);
  av1_copy(fc->seg.tree_cdf, default_seg_tree_cdf);
#if CONFIG_EXT_SEG
  av1_copy(fc->seg.tree_cdf1, default_seg_tree_cdf1);
#endif  // CONFIG_EXT_SEG
  av1_copy(fc->switchable_flex_restore_cdf,
           default_switchable_flex_restore_cdf);
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    av1_copy(fc->ccso_cdf[plane], default_ccso_cdf[plane]);
  }
  av1_copy(fc->cdef_strength_index0_cdf, default_cdef_strength_index0_cdf);
  av1_copy(fc->cdef_cdf, default_cdef_cdf);
  av1_copy(fc->gdf_cdf, default_gdf_cdf);
  av1_copy(fc->wienerns_restore_cdf, default_wienerns_restore_cdf);
  av1_copy(fc->wienerns_length_cdf, default_wienerns_length_cdf);
  av1_copy(fc->wienerns_uv_sym_cdf, default_wienerns_uv_sym_cdf);
  av1_copy(fc->wienerns_4part_cdf, default_wienerns_4part_cdf);
  av1_copy(fc->pc_wiener_restore_cdf, default_pc_wiener_restore_cdf);
#if !CONFIG_MERGE_PARA_CTX
  av1_copy(fc->merged_param_cdf, default_merged_param_cdf);
#endif  // CONFIG_MERGE_PARA_CTX
  av1_copy(fc->y_mode_set_cdf, default_y_mode_set_cdf);
#if CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->y_mode_idx_cdf, default_y_mode_idx_cdf);
  av1_copy(fc->y_mode_idx_offset_cdf, default_y_mode_idx_offset_cdf);
#else
  av1_copy(fc->y_mode_idx_cdf_0, default_y_mode_idx_cdf_0);
#if !CONFIG_CTX_Y_SECOND_MODE
  av1_copy(fc->y_mode_idx_cdf_1, default_y_second_mode_cdf);
#endif  // !CONFIG_CTX_Y_SECOND_MODE
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->uv_mode_cdf, default_uv_mode_cdf);
  av1_copy(fc->cfl_cdf, default_cfl_cdf);
  av1_copy(fc->mrl_index_cdf, default_mrl_index_cdf);
  av1_copy(fc->multi_line_mrl_cdf, default_multi_line_mrl_cdf);
  av1_copy(fc->fsc_mode_cdf, default_fsc_mode_cdf);
  av1_copy(fc->dpcm_cdf, default_dpcm_cdf);
  av1_copy(fc->dpcm_vert_horz_cdf, default_dpcm_vert_horz_cdf);
  av1_copy(fc->dpcm_uv_cdf, default_dpcm_uv_cdf);
  av1_copy(fc->dpcm_uv_vert_horz_cdf, default_dpcm_uv_vert_horz_cdf);
  av1_copy(fc->cfl_index_cdf, default_cfl_index_cdf);
#if MHCCP_RUNTIME_FLAG
  av1_copy(fc->cfl_mhccp_cdf, default_cfl_mhccp_switch_cdf);
#endif  // CONFIG_ENABLE_MHCCP
  av1_copy(fc->filter_dir_cdf, default_filter_dir_cdf);
  av1_copy(fc->switchable_interp_cdf, default_switchable_interp_cdf);
  av1_copy(fc->region_type_cdf, default_region_type_cdf);
  av1_copy(fc->do_split_cdf, default_do_split_cdf);
  av1_copy(fc->do_square_split_cdf, default_do_square_split_cdf);
  av1_copy(fc->rect_type_cdf, default_rect_type_cdf);
  av1_copy(fc->do_ext_partition_cdf, default_do_ext_partition_cdf);
  av1_copy(fc->do_uneven_4way_partition_cdf,
           default_do_uneven_4way_partition_cdf);
#if !CONFIG_NEW_PART_CTX
  av1_copy(fc->uneven_4way_partition_type_cdf,
           default_uneven_4way_partition_type_cdf);
#endif  // !CONFIG_NEW_PART_CTX
  av1_copy(fc->intra_ext_tx_short_side_cdf,
           default_intra_ext_tx_short_side_cdf);
  av1_copy(fc->inter_ext_tx_short_side_cdf,
           default_inter_ext_tx_short_side_cdf);
  av1_copy(fc->tx_ext_32_cdf, default_tx_ext_32_cdf);
  av1_copy(fc->lossless_tx_size_cdf, default_lossless_tx_size_cdf);
  av1_copy(fc->lossless_inter_tx_type_cdf, default_lossless_inter_tx_type_cdf);
  av1_copy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf);
  av1_copy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf);
#if CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->inter_tx_type_set, default_inter_tx_type_set_cdf);
  av1_copy(fc->inter_tx_type_idx, default_inter_tx_type_idx_cdf);
  av1_copy(fc->inter_tx_type_offset_1, default_inter_tx_type_offset_1_cdf);
  av1_copy(fc->inter_tx_type_offset_2, default_inter_tx_type_offset_2_cdf);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  av1_copy(fc->bru_mode_cdf, default_bru_mode_cdf);
  av1_copy(fc->skip_mode_cdfs, default_skip_mode_cdfs);
  av1_copy(fc->skip_txfm_cdfs, default_skip_txfm_cdfs);
  av1_copy(fc->intra_inter_cdf, default_intra_inter_cdf);
  for (int i = 0; i < SPATIAL_PREDICTION_PROBS; i++) {
    av1_copy(fc->seg.spatial_pred_seg_cdf[i],
             default_spatial_pred_seg_tree_cdf[i]);
#if CONFIG_EXT_SEG
    av1_copy(fc->seg.spatial_pred_seg_cdf1[i],
             default_spatial_pred_seg_tree_cdf1[i]);
    av1_copy(fc->seg.seg_id_ext_flag_cdf[i], default_seg_id_ext_flag_cdf[i]);
#endif  // CONFIG_EXT_SEG
  }
  av1_copy(fc->delta_q_cdf, default_delta_q_cdf);
  av1_copy(fc->delta_lf_cdf, default_delta_lf_cdf);
  av1_copy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf);
  av1_copy(fc->cfl_sign_cdf, default_cfl_sign_cdf);
  av1_copy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf);
  av1_copy(fc->intrabc_cdf, default_intrabc_cdf);
  av1_copy(fc->intrabc_mode_cdf, default_intrabc_mode_cdf);
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  av1_copy(fc->intrabc_drl_idx_cdf, default_intrabc_drl_idx_cdf);
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX
  av1_copy(fc->intrabc_bv_precision_cdf, default_intrabc_bv_precision_cdf);
  av1_copy(fc->morph_pred_cdf, default_morph_pred_cdf);
  av1_copy(fc->stx_cdf, default_stx_cdf);
  av1_copy(fc->most_probable_stx_set_cdf, default_most_probable_stx_set_cdf);
  av1_copy(fc->most_probable_stx_set_cdf_ADST_ADST,
           default_most_probable_stx_set_cdf_ADST_ADST);
  av1_copy(fc->pb_mv_precision_cdf, default_pb_mv_precision_cdf);
  av1_copy(fc->pb_mv_mpp_flag_cdf, default_pb_mv_most_probable_precision_cdf);
  av1_copy(fc->cctx_type_cdf, default_cctx_type_cdf);
}

void av1_set_default_ref_deltas(int8_t *ref_deltas) {
  assert(ref_deltas != NULL);

  ref_deltas[0] = -1;
  ref_deltas[1] = -1;
  ref_deltas[2] = -1;
  ref_deltas[3] = 0;
  ref_deltas[4] = 0;
  ref_deltas[5] = 0;
  ref_deltas[6] = 0;
  ref_deltas[INTRA_FRAME_INDEX] = 1;
  ref_deltas[TIP_FRAME_INDEX] = 0;
}

void av1_set_default_mode_deltas(int8_t *mode_deltas) {
  assert(mode_deltas != NULL);

  mode_deltas[0] = 0;
  mode_deltas[1] = 0;
}

static void set_default_lf_deltas(struct loopfilter *lf) {
  lf->mode_ref_delta_enabled = 0;
  lf->mode_ref_delta_update = 0;
}

static AOM_INLINE void cumulative_avg_cdf_symbol(
    aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr, int num_cdfs,
    int cdf_stride, int nsymbs, unsigned int total_tiles_log2) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      const int index = i * cdf_stride + j;
      cdf_ptr_left[index] =
          cdf_ptr_left[index] + (cdf_ptr_tr[index] >> total_tiles_log2);
      assert(cdf_ptr_left[index] >= 0 && cdf_ptr_left[index] < CDF_PROB_TOP);
    }
  }
}

#define CUMULATIVE_AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  CUMULATIVE_AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))
#define CUMULATIVE_AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)   \
  do {                                                                        \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;                  \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                      \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);          \
    int num_cdfs = array_size / cdf_stride;                                   \
    cumulative_avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, \
                              nsymbs, total_tiles_log2);                      \
  } while (0)

static void cumulative_avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr,
                               int total_tiles_log2) {
#if !CONFIG_VQ_MVD_CODING
  CUMULATIVE_AVERAGE_CDF(nmv_left->joints_cdf, nmv_tr->joints_cdf, 4);
#else
#if CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_set_cdf,
                         nmv_tr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_0[prec],
                           nmv_tr->joint_shell_class_cdf_0[prec],
                           num_mv_class_0);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                             nmv_tr->joint_shell_class_cdf_1[prec],
                             num_mv_class_1 - 1);
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                             nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                             nmv_tr->joint_shell_class_cdf_1[prec],
                             num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                             nmv_tr->joint_shell_class_cdf[prec],
                             num_mv_class - 1);
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                             nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                             nmv_tr->joint_shell_class_cdf[prec], num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
                         nmv_tr->shell_offset_low_class_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
                         nmv_tr->shell_offset_class2_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf,
                         nmv_tr->shell_offset_other_class_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->col_mv_greater_flags_cdf,
                         nmv_tr->col_mv_greater_flags_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->col_mv_index_cdf, nmv_tr->col_mv_index_cdf,
                         2);

#endif  // !CONFIG_VQ_MVD_CODING
  CUMULATIVE_AVERAGE_CDF(nmv_left->amvd_joints_cdf, nmv_tr->amvd_joints_cdf,
                         MV_JOINTS);
  for (int i = 0; i < 2; i++) {
#if !CONFIG_VQ_MVD_CODING
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].classes_cdf,
                           nmv_tr->comps[i].classes_cdf, MV_CLASSES);
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].amvd_classes_cdf,
                           nmv_tr->comps[i].amvd_classes_cdf, MV_CLASSES);
#else
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].amvd_indices_cdf,
                           nmv_tr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].class0_fp_cdf,
                           nmv_tr->comps[i].class0_fp_cdf, 2);
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].fp_cdf, nmv_tr->comps[i].fp_cdf,
                           2);
#endif  //! CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].class0_hp_cdf,
                           nmv_tr->comps[i].class0_hp_cdf, 2);
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].hp_cdf, nmv_tr->comps[i].hp_cdf,
                           2);
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].class0_cdf,
                           nmv_tr->comps[i].class0_cdf, CLASS0_SIZE);
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].bits_cdf,
                           nmv_tr->comps[i].bits_cdf, 2);
#endif  // !CONFIG_VQ_MVD_CODING
  }
}

// This function facilitates the averaging of CDFs from different tiles.
void av1_cumulative_avg_cdf_symbols(FRAME_CONTEXT *ctx_left,
                                    FRAME_CONTEXT *ctx_tr,
                                    unsigned int total_tiles_log2) {
  CUMULATIVE_AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
#if !CONFIG_BY_PASS_V_SIGN
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_dc_sign_cdf, ctx_tr->v_dc_sign_cdf, 2);
#endif  // !CONFIG_BY_PASS_V_SIGN
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
                         EOB_MAX_SYMS - 6);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
                         EOB_MAX_SYMS - 5);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
                         EOB_MAX_SYMS - 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
                         EOB_MAX_SYMS - 3);
#if CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
                         EOB_MAX_SYMS - 3);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
                         EOB_MAX_SYMS - 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
                         EOB_MAX_SYMS - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
                         EOB_MAX_SYMS);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_eob_cdf,
                         ctx_tr->coeff_base_eob_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_bob_cdf,
                         ctx_tr->coeff_base_bob_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf,
                         ctx_tr->intra_dip_mode_n6_cdf, 6);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_cdf, ctx_tr->coeff_base_lf_cdf,
                         LF_BASE_SYMBOLS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_eob_cdf,
                         ctx_tr->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_lf_cdf, ctx_tr->coeff_br_lf_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->idtx_sign_cdf, ctx_tr->idtx_sign_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_cdf_idtx,
                         ctx_tr->coeff_base_cdf_idtx, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_cdf_idtx, ctx_tr->coeff_br_cdf_idtx,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_single_mode_cdf,
                         ctx_tr->inter_single_mode_cdf, INTER_SINGLE_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf,
                         4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf,
                         ctx_tr->coeff_base_eob_uv_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf,
                         ctx_tr->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
                         ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
#if !CONFIG_COEFF_BR_LF_UV_BYPASS
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_lf_uv_cdf,
                         ctx_tr->coeff_br_lf_uv_cdf, BR_CDF_SIZE);
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_warp_mode_cdf,
                         ctx_tr->inter_warp_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
                         ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
                         REFINEMV_NUM_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_drl_cdf, ctx_tr->tip_drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
                         ctx_tr->inter_compound_mode_is_joint_cdf,
                         NUM_OPTIONS_IS_JOINT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
                         ctx_tr->inter_compound_mode_non_joint_type_cdf,
                         NUM_OPTIONS_NON_JOINT_TYPE);

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
                         ctx_tr->inter_compound_mode_same_refs_cdf,
                         INTER_COMPOUND_SAME_REFS_TYPES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->amvd_mode_cdf, ctx_tr->amvd_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf,
                         ctx_tr->jmvd_scale_mode_cdf,
                         JOINT_NEWMV_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
                         ctx_tr->jmvd_amvd_scale_mode_cdf,
                         JOINT_AMVD_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
                         MASKED_COMPOUND_TYPES);
#if CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_quad_cdf, ctx_tr->wedge_quad_cdf,
                         WEDGE_QUADS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_angle_cdf, ctx_tr->wedge_angle_cdf,
                         QUAD_WEDGE_ANGLES);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_angle_dir_cdf,
                         ctx_tr->wedge_angle_dir_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_angle_0_cdf, ctx_tr->wedge_angle_0_cdf,
                         H_WEDGE_ANGLES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_angle_1_cdf, ctx_tr->wedge_angle_1_cdf,
                         H_WEDGE_ANGLES);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_dist_cdf, ctx_tr->wedge_dist_cdf,
                         NUM_WEDGE_DIST);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_dist_cdf2, ctx_tr->wedge_dist_cdf2,
                         NUM_WEDGE_DIST - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_interintra_cdf,
                         ctx_tr->warp_interintra_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_interintra_cdf,
                         ctx_tr->wedge_interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_mode_cdf,
                         ctx_tr->interintra_mode_cdf, INTERINTRA_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_cdf,
                         ctx_tr->warp_delta_param_cdf,
                         WARP_DELTA_NUMSYMBOLS_LOW);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_precision_idx_cdf,
                         ctx_tr->warp_precision_idx_cdf,
                         NUM_WARP_PRECISION_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
                         ctx_tr->warp_delta_param_high_cdf,
                         WARP_DELTA_NUMSYMBOLS_HIGH);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_param_sign_cdf,
                         ctx_tr->warp_param_sign_cdf, 2);
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_causal_warpmv_cdf,
                         ctx_tr->warp_causal_warpmv_cdf, 2);
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
                         ctx_tr->warpmv_with_mvd_flag_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
                         ctx_tr->explicit_bawp_scale_cdf,
                         EXPLICIT_BAWP_SCALE_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
                         TIP_PRED_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_y,
                         ctx_tr->identity_row_cdf_y, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_uv,
                         ctx_tr->identity_row_cdf_uv, 3);
#if !CONFIG_PLT_DIR_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_direction_cdf,
                         ctx_tr->palette_direction_cdf, 2);
#endif  // !CONFIG_PLT_DIR_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_cdf, ctx_tr->cfl_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_y_size_cdf,
                         ctx_tr->palette_y_size_cdf, PALETTE_SIZES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_uv_size_cdf,
                         ctx_tr->palette_uv_size_cdf, PALETTE_SIZES);

  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                              ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                              CDF_SIZE(PALETTE_COLORS));
  }
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_y_mode_cdf,
                         ctx_tr->palette_y_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_uv_mode_cdf,
                         ctx_tr->palette_uv_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_do_partition_cdf,
                         ctx_tr->txfm_do_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
                         ctx_tr->txfm_4way_partition_type_cdf,
                         TX_PARTITION_TYPE_NUM);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
                         ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->lossless_tx_size_cdf,
                         ctx_tr->lossless_tx_size_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->lossless_inter_tx_type_cdf,
                         ctx_tr->lossless_inter_tx_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_group_idx_cdf,
                         ctx_tr->comp_group_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->bru_mode_cdf, ctx_tr->bru_mode_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  cumulative_avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, total_tiles_log2);
  cumulative_avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, total_tiles_log2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf,
                         2);
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_drl_idx_cdf,
                         ctx_tr->intrabc_drl_idx_cdf, 2);
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
                         ctx_tr->intrabc_bv_precision_cdf,
                         NUM_ALLOWED_BV_PRECISIONS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
#if CONFIG_EXT_SEG
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf,
                         MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf1, ctx_tr->seg.tree_cdf1,
                         MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
                         ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf1,
                         ctx_tr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.seg_id_ext_flag_cdf,
                         ctx_tr->seg.seg_id_ext_flag_cdf, 2);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf,
                         MAX_SEGMENTS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
                         ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
#endif  // CONFIG_EXT_SEG
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
                         ctx_tr->switchable_flex_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
                         ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                              CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  CUMULATIVE_AVERAGE_CDF(ctx_left->gdf_cdf, ctx_tr->gdf_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_restore_cdf,
                         ctx_tr->wienerns_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_length_cdf,
                         ctx_tr->wienerns_length_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf,
                         ctx_tr->wienerns_uv_sym_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_4part_cdf,
                         ctx_tr->wienerns_4part_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf,
                         ctx_tr->pc_wiener_restore_cdf, 2);
#if !CONFIG_MERGE_PARA_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->merged_param_cdf, ctx_tr->merged_param_cdf,
                         2);
#endif  // !CONFIG_MERGE_PARA_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf,
                         FSC_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf,
                         MRL_LINE_NUMBER);
  CUMULATIVE_AVERAGE_CDF(ctx_left->multi_line_mrl_cdf,
                         ctx_tr->multi_line_mrl_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf,
                         ctx_tr->dpcm_vert_horz_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf,
                         ctx_tr->dpcm_uv_vert_horz_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf,
                         MHCCP_MODE_NUM);
#if MHCCP_RUNTIME_FLAG
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_mhccp_cdf, ctx_tr->cfl_mhccp_cdf,
                         CFL_MHCCP_SWITCH_NUM);
#endif  // MHCCP_RUNTIME_FLAG
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
                         CFL_TYPE_COUNT - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
                         INTRA_MODE_SETS);
#if CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf, ctx_tr->y_mode_idx_cdf,
                         LUMA_INTRA_MODE_INDEX_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_offset_cdf,
                         ctx_tr->y_mode_idx_offset_cdf,
                         LUMA_INTRA_MODE_OFFSET_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
                         CHROMA_INTRA_MODE_INDEX_COUNT);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf_0, ctx_tr->y_mode_idx_cdf_0,
                         FIRST_MODE_COUNT);
#if !CONFIG_CTX_Y_SECOND_MODE
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf_1, ctx_tr->y_mode_idx_cdf_1,
                         SECOND_MODE_COUNT);
#endif  // !CONFIG_CTX_Y_SECOND_MODE
  CUMULATIVE_AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
                         UV_INTRA_MODES - 1);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf,
                         REGION_TYPES);

  CUMULATIVE_AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_square_split_cdf,
                         ctx_tr->do_square_split_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->rect_type_cdf, ctx_tr->rect_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_ext_partition_cdf,
                         ctx_tr->do_ext_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_uneven_4way_partition_cdf,
                         ctx_tr->do_uneven_4way_partition_cdf, 2);
#if !CONFIG_NEW_PART_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->uneven_4way_partition_type_cdf,
                         ctx_tr->uneven_4way_partition_type_cdf,
                         NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_interp_cdf,
                         ctx_tr->switchable_interp_cdf, SWITCHABLE_FILTERS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf,
                         DELTA_Q_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf,
                         DELTA_LF_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_lf_multi_cdf,
                         ctx_tr->delta_lf_multi_cdf, DELTA_LF_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
                         ctx_tr->inter_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
                         ctx_tr->intra_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1],
                            ctx_tr->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2],
                            ctx_tr->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1],
                            ctx_tr->inter_ext_tx_cdf[1], INTER_TX_SET1,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2],
                            ctx_tr->inter_ext_tx_cdf[2], INTER_TX_SET2,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3],
                            ctx_tr->inter_ext_tx_cdf[3], INTER_TX_SET3,
                            CDF_SIZE(TX_TYPES));
#if CONFIG_REDUCED_TX_SET_EXT
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[4],
                            ctx_tr->inter_ext_tx_cdf[4], INTER_TX_SET4,
                            CDF_SIZE(TX_TYPES));
#endif  // CONFIG_REDUCED_TX_SET_EXT
#if CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_set, ctx_tr->inter_tx_type_set,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_idx, ctx_tr->inter_tx_type_idx,
                         INTER_TX_TYPE_INDEX_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_offset_1,
                         ctx_tr->inter_tx_type_offset_1,
                         INTER_TX_TYPE_OFFSET1_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_offset_2,
                         ctx_tr->inter_tx_type_offset_2,
                         INTER_TX_TYPE_OFFSET2_COUNT);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf,
                         CFL_JOINT_SIGNS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
                         CFL_ALPHABET_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
                         ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
                         ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
                         IST_REDUCE_SET_SIZE_ADST_ADST);

  CUMULATIVE_AVERAGE_CDF(ctx_left->pb_mv_mpp_flag_cdf,
                         ctx_tr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      CUMULATIVE_AVG_CDF_STRIDE(
          ctx_left->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          ctx_tr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_ph_cdf, ctx_tr->coeff_base_ph_cdf,
                         4);
#if !CONFIG_COEFF_BR_PH_BYPASS
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_ph_cdf, ctx_tr->coeff_br_ph_cdf, 4);
#endif  // !CONFIG_COEFF_BR_PH_BYPASS
  CUMULATIVE_AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf,
                         CCTX_TYPES);
}

static AOM_INLINE void shift_cdf_symbol(aom_cdf_prob *cdf_ptr, int num_cdfs,
                                        int cdf_stride, int nsymbs,
                                        unsigned int total_tiles_log2) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      const int index = i * cdf_stride + j;
      cdf_ptr[index] = (cdf_ptr[index] >> total_tiles_log2);
      assert(cdf_ptr[index] >= 0 && cdf_ptr[index] < CDF_PROB_TOP);
    }
  }
}

#define SHIFT_CDF(cname_cdf, nsymbs) \
  SHIFT_CDF_STRIDE(cname_cdf, nsymbs, CDF_SIZE(nsymbs))
#define SHIFT_CDF_STRIDE(cname_cdf, nsymbs, cdf_stride)                        \
  do {                                                                         \
    aom_cdf_prob *cdf_ptr = (aom_cdf_prob *)cname_cdf;                         \
    int array_size = (int)sizeof(cname_cdf) / sizeof(aom_cdf_prob);            \
    int num_cdfs = array_size / cdf_stride;                                    \
    shift_cdf_symbol(cdf_ptr, num_cdfs, cdf_stride, nsymbs, total_tiles_log2); \
  } while (0)

static void shift_nmv(nmv_context *nmv_ptr, int total_tiles_log2) {
#if !CONFIG_VQ_MVD_CODING
  SHIFT_CDF(nmv_ptr->joints_cdf, 4);
#else
#if CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(nmv_ptr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_0[prec], num_mv_class_0);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_1[prec], num_mv_class_1 - 1);
      SHIFT_CDF(nmv_ptr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_1[prec], num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf[prec], num_mv_class - 1);
      SHIFT_CDF(nmv_ptr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf[prec], num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(nmv_ptr->shell_offset_low_class_cdf, 2);
  SHIFT_CDF(nmv_ptr->shell_offset_class2_cdf, 2);
  SHIFT_CDF(nmv_ptr->shell_offset_other_class_cdf, 2);
  SHIFT_CDF(nmv_ptr->col_mv_greater_flags_cdf, 2);
  SHIFT_CDF(nmv_ptr->col_mv_index_cdf, 2);

#endif  // !CONFIG_VQ_MVD_CODING
  SHIFT_CDF(nmv_ptr->amvd_joints_cdf, MV_JOINTS);
  for (int i = 0; i < 2; i++) {
#if !CONFIG_VQ_MVD_CODING
    SHIFT_CDF(nmv_ptr->comps[i].classes_cdf, MV_CLASSES);
    SHIFT_CDF(nmv_ptr->comps[i].amvd_classes_cdf, MV_CLASSES);
#else
    SHIFT_CDF(nmv_ptr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    SHIFT_CDF(nmv_ptr->comps[i].class0_fp_cdf, 2);
    SHIFT_CDF(nmv_ptr->comps[i].fp_cdf, 2);
#endif  //! CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    SHIFT_CDF(nmv_ptr->comps[i].class0_hp_cdf, 2);
    SHIFT_CDF(nmv_ptr->comps[i].hp_cdf, 2);
    SHIFT_CDF(nmv_ptr->comps[i].class0_cdf, CLASS0_SIZE);
    SHIFT_CDF(nmv_ptr->comps[i].bits_cdf, 2);
#endif  // !CONFIG_VQ_MVD_CODING
  }
}

// This function facilitates the shift of CDFs from number of tiles.
void av1_shift_cdf_symbols(FRAME_CONTEXT *ctx_ptr,
                           unsigned int total_tiles_log2) {
  SHIFT_CDF(ctx_ptr->txb_skip_cdf, 2);
  SHIFT_CDF(ctx_ptr->v_txb_skip_cdf, 2);
  SHIFT_CDF(ctx_ptr->eob_extra_cdf, 2);
  SHIFT_CDF(ctx_ptr->dc_sign_cdf, 2);
#if !CONFIG_BY_PASS_V_SIGN
  SHIFT_CDF(ctx_ptr->v_dc_sign_cdf, 2);
#endif  // !CONFIG_BY_PASS_V_SIGN
  SHIFT_CDF(ctx_ptr->eob_flag_cdf16, EOB_MAX_SYMS - 6);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf32, EOB_MAX_SYMS - 5);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf64, EOB_MAX_SYMS - 4);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf128, EOB_MAX_SYMS - 3);
#if CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->eob_flag_cdf256, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf512, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf1024, EOB_MAX_SYMS - 3);
#else
  SHIFT_CDF(ctx_ptr->eob_flag_cdf256, EOB_MAX_SYMS - 2);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf512, EOB_MAX_SYMS - 1);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf1024, EOB_MAX_SYMS);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->coeff_base_eob_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_bob_cdf, 3);
  SHIFT_CDF(ctx_ptr->intra_dip_cdf, 2);
  SHIFT_CDF(ctx_ptr->intra_dip_mode_n6_cdf, 6);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  SHIFT_CDF(ctx_ptr->coeff_br_lf_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf, 4);
  SHIFT_CDF(ctx_ptr->idtx_sign_cdf, 2);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf_idtx, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf_idtx, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->inter_single_mode_cdf, INTER_SINGLE_MODES);
  SHIFT_CDF(ctx_ptr->coeff_base_uv_cdf, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_uv_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_eob_uv_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
#if !CONFIG_COEFF_BR_LF_UV_BYPASS
  SHIFT_CDF(ctx_ptr->coeff_br_lf_uv_cdf, BR_CDF_SIZE);
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS

  SHIFT_CDF(ctx_ptr->inter_warp_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->is_warpmv_or_warp_newmv_cdf, 2);

  SHIFT_CDF(ctx_ptr->refinemv_flag_cdf, REFINEMV_NUM_MODES);
  SHIFT_CDF(ctx_ptr->drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->skip_drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->tip_drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->use_optflow_cdf, 2);

  SHIFT_CDF(ctx_ptr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  SHIFT_CDF(ctx_ptr->inter_compound_mode_non_joint_type_cdf,
            NUM_OPTIONS_NON_JOINT_TYPE);

  SHIFT_CDF(ctx_ptr->inter_compound_mode_same_refs_cdf,
            INTER_COMPOUND_SAME_REFS_TYPES);
  SHIFT_CDF(ctx_ptr->cwp_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->amvd_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->jmvd_scale_mode_cdf, JOINT_NEWMV_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->compound_type_cdf, MASKED_COMPOUND_TYPES);
#if CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->wedge_quad_cdf, WEDGE_QUADS);
  SHIFT_CDF(ctx_ptr->wedge_angle_cdf, QUAD_WEDGE_ANGLES);
#else
  SHIFT_CDF(ctx_ptr->wedge_angle_dir_cdf, 2);
  SHIFT_CDF(ctx_ptr->wedge_angle_0_cdf, H_WEDGE_ANGLES);
  SHIFT_CDF(ctx_ptr->wedge_angle_1_cdf, H_WEDGE_ANGLES);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->wedge_dist_cdf, NUM_WEDGE_DIST);
  SHIFT_CDF(ctx_ptr->wedge_dist_cdf2, NUM_WEDGE_DIST - 1);
  SHIFT_CDF(ctx_ptr->warp_interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->wedge_interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->interintra_mode_cdf, INTERINTRA_MODES);
  SHIFT_CDF(ctx_ptr->warp_causal_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_delta_param_cdf, WARP_DELTA_NUMSYMBOLS_LOW);

  SHIFT_CDF(ctx_ptr->warp_precision_idx_cdf, NUM_WARP_PRECISION_MODES);
  SHIFT_CDF(ctx_ptr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  SHIFT_CDF(ctx_ptr->warp_param_sign_cdf, 2);
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  SHIFT_CDF(ctx_ptr->warp_causal_warpmv_cdf, 2);
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  SHIFT_CDF(ctx_ptr->warp_ref_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->warpmv_with_mvd_flag_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_extend_cdf, 2);

  SHIFT_CDF(ctx_ptr->bawp_cdf, 2);
  SHIFT_CDF(ctx_ptr->explicit_bawp_cdf, 2);
  SHIFT_CDF(ctx_ptr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);

  SHIFT_CDF(ctx_ptr->tip_cdf, 2);
  SHIFT_CDF(ctx_ptr->tip_pred_mode_cdf, TIP_PRED_MODES);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_y, 3);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_uv, 3);
#if !CONFIG_PLT_DIR_CTX
  SHIFT_CDF(ctx_ptr->palette_direction_cdf, 2);
#endif  // !CONFIG_PLT_DIR_CTX
  SHIFT_CDF(ctx_ptr->cfl_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_y_size_cdf, PALETTE_SIZES);
  SHIFT_CDF(ctx_ptr->palette_uv_size_cdf, PALETTE_SIZES);

  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    SHIFT_CDF_STRIDE(ctx_ptr->palette_y_color_index_cdf[j], nsymbs,
                     CDF_SIZE(PALETTE_COLORS));
  }
  SHIFT_CDF(ctx_ptr->palette_y_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_uv_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_inter_cdf, 2);
  SHIFT_CDF(ctx_ptr->single_ref_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref0_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref1_cdf, 2);
  SHIFT_CDF(ctx_ptr->txfm_do_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
  SHIFT_CDF(ctx_ptr->txfm_2or3_way_partition_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->lossless_tx_size_cdf, 2);
  SHIFT_CDF(ctx_ptr->lossless_inter_tx_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_group_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->bru_mode_cdf, 3);
  SHIFT_CDF(ctx_ptr->skip_mode_cdfs, 2);
  SHIFT_CDF(ctx_ptr->skip_txfm_cdfs, 2);
  SHIFT_CDF(ctx_ptr->intra_inter_cdf, 2);
  shift_nmv(&ctx_ptr->nmvc, total_tiles_log2);
  shift_nmv(&ctx_ptr->ndvc, total_tiles_log2);
  SHIFT_CDF(ctx_ptr->intrabc_cdf, 2);
  SHIFT_CDF(ctx_ptr->intrabc_mode_cdf, 2);
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  SHIFT_CDF(ctx_ptr->intrabc_drl_idx_cdf, 2);
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX
  SHIFT_CDF(ctx_ptr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
  SHIFT_CDF(ctx_ptr->morph_pred_cdf, 2);
  SHIFT_CDF(ctx_ptr->seg.pred_cdf, 2);
#if CONFIG_EXT_SEG
  SHIFT_CDF(ctx_ptr->seg.tree_cdf, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.tree_cdf1, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.seg_id_ext_flag_cdf, 2);
#else
  SHIFT_CDF(ctx_ptr->seg.tree_cdf, MAX_SEGMENTS);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
#endif  // CONFIG_EXT_SEG
  SHIFT_CDF(ctx_ptr->switchable_flex_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->ccso_cdf, 2);
  SHIFT_CDF(ctx_ptr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    SHIFT_CDF_STRIDE(ctx_ptr->cdef_cdf[j], j + 2, CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  SHIFT_CDF(ctx_ptr->gdf_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_length_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_uv_sym_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_4part_cdf, 4);
  SHIFT_CDF(ctx_ptr->pc_wiener_restore_cdf, 2);
#if !CONFIG_MERGE_PARA_CTX
  SHIFT_CDF(ctx_ptr->merged_param_cdf, 2);
#endif  // !CONFIG_MERGE_PARA_CTX
  SHIFT_CDF(ctx_ptr->fsc_mode_cdf, FSC_MODES);
  SHIFT_CDF(ctx_ptr->mrl_index_cdf, MRL_LINE_NUMBER);
  SHIFT_CDF(ctx_ptr->multi_line_mrl_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_vert_horz_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_vert_horz_cdf, 2);

  SHIFT_CDF(ctx_ptr->filter_dir_cdf, MHCCP_MODE_NUM);
#if MHCCP_RUNTIME_FLAG
  SHIFT_CDF(ctx_ptr->cfl_mhccp_cdf, CFL_MHCCP_SWITCH_NUM);
#endif  // MHCCP_RUNTIME_FLAG
  SHIFT_CDF(ctx_ptr->cfl_index_cdf, CFL_TYPE_COUNT - 1);
  SHIFT_CDF(ctx_ptr->y_mode_set_cdf, INTRA_MODE_SETS);
#if CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf, LUMA_INTRA_MODE_INDEX_COUNT);
  SHIFT_CDF(ctx_ptr->y_mode_idx_offset_cdf, LUMA_INTRA_MODE_OFFSET_COUNT);
  SHIFT_CDF(ctx_ptr->uv_mode_cdf, CHROMA_INTRA_MODE_INDEX_COUNT);
#else
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf_0, FIRST_MODE_COUNT);
#if !CONFIG_CTX_Y_SECOND_MODE
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf_1, SECOND_MODE_COUNT);
#endif  // !CONFIG_CTX_Y_SECOND_MODE
  SHIFT_CDF(ctx_ptr->uv_mode_cdf, UV_INTRA_MODES - 1);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->region_type_cdf, REGION_TYPES);

  SHIFT_CDF(ctx_ptr->do_split_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_square_split_cdf, 2);
  SHIFT_CDF(ctx_ptr->rect_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_ext_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_uneven_4way_partition_cdf, 2);
#if !CONFIG_NEW_PART_CTX
  SHIFT_CDF(ctx_ptr->uneven_4way_partition_type_cdf, NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
  SHIFT_CDF(ctx_ptr->switchable_interp_cdf, SWITCHABLE_FILTERS);
  SHIFT_CDF(ctx_ptr->delta_q_cdf, DELTA_Q_PROBS + 1);
  SHIFT_CDF(ctx_ptr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  SHIFT_CDF(ctx_ptr->delta_lf_multi_cdf, DELTA_LF_PROBS + 1);
  SHIFT_CDF(ctx_ptr->inter_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->intra_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->tx_ext_32_cdf, 2);
  SHIFT_CDF_STRIDE(ctx_ptr->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[1], INTER_TX_SET1,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[2], INTER_TX_SET2,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[3], INTER_TX_SET3,
                   CDF_SIZE(TX_TYPES));
#if CONFIG_REDUCED_TX_SET_EXT
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[4], INTER_TX_SET4,
                   CDF_SIZE(TX_TYPES));
#endif  // CONFIG_REDUCED_TX_SET_EXT
#if CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->inter_tx_type_set, 2);
  SHIFT_CDF(ctx_ptr->inter_tx_type_idx, INTER_TX_TYPE_INDEX_COUNT);
  SHIFT_CDF(ctx_ptr->inter_tx_type_offset_1, INTER_TX_TYPE_OFFSET1_COUNT);
  SHIFT_CDF(ctx_ptr->inter_tx_type_offset_2, INTER_TX_TYPE_OFFSET2_COUNT);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(ctx_ptr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  SHIFT_CDF(ctx_ptr->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
  SHIFT_CDF(ctx_ptr->stx_cdf, STX_TYPES);
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf_ADST_ADST,
            IST_REDUCE_SET_SIZE_ADST_ADST);

  SHIFT_CDF(ctx_ptr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      SHIFT_CDF_STRIDE(
          ctx_ptr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  SHIFT_CDF(ctx_ptr->coeff_base_ph_cdf, 4);
#if !CONFIG_COEFF_BR_PH_BYPASS
  SHIFT_CDF(ctx_ptr->coeff_br_ph_cdf, 4);
#endif  // !CONFIG_COEFF_BR_PH_BYPASS
  SHIFT_CDF(ctx_ptr->cctx_type_cdf, CCTX_TYPES);
}

static void avg_cdf_symbol(aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr,
                           int num_cdfs, int cdf_stride, int nsymbs,
                           int wt_left, int wt_tr, unsigned int offset,
                           unsigned int shift) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      cdf_ptr_left[i * cdf_stride + j] =
          (aom_cdf_prob)(((int)cdf_ptr_left[i * cdf_stride + j] * wt_left +
                          (int)cdf_ptr_tr[i * cdf_stride + j] * wt_tr +
                          offset) >>
                         shift);
      assert(cdf_ptr_left[i * cdf_stride + j] >= 0 &&
             cdf_ptr_left[i * cdf_stride + j] < CDF_PROB_TOP);
    }
  }
}

#define AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))
#define AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)           \
  do {                                                                     \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;               \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                   \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);       \
    int num_cdfs = array_size / cdf_stride;                                \
    avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, nsymbs, \
                   wt_left, wt_tr, offset, shift);                         \
  } while (0)

static void avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr, int wt_left,
                    int wt_tr, unsigned int offset, unsigned int shift) {
#if !CONFIG_VQ_MVD_CODING
  AVERAGE_CDF(nmv_left->joints_cdf, nmv_tr->joints_cdf, 4);
#else
#if CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(nmv_left->joint_shell_set_cdf, nmv_tr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    AVERAGE_CDF(nmv_left->joint_shell_class_cdf_0[prec],
                nmv_tr->joint_shell_class_cdf_0[prec], num_mv_class_0);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                  nmv_tr->joint_shell_class_cdf_1[prec], num_mv_class_1 - 1);
      AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                  nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                  nmv_tr->joint_shell_class_cdf_1[prec], num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                  nmv_tr->joint_shell_class_cdf[prec], num_mv_class - 1);
      AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                  nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                  nmv_tr->joint_shell_class_cdf[prec], num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
              nmv_tr->shell_offset_low_class_cdf, 2);
  AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
              nmv_tr->shell_offset_class2_cdf, 2);
  for (int i = 0; i < NUM_CTX_CLASS_OFFSETS; i++) {
    AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf[i],
                nmv_tr->shell_offset_other_class_cdf[i], 2);
  }
  AVERAGE_CDF(nmv_left->col_mv_greater_flags_cdf,
              nmv_tr->col_mv_greater_flags_cdf, 2);
  AVERAGE_CDF(nmv_left->col_mv_index_cdf, nmv_tr->col_mv_index_cdf, 2);

#endif  // !CONFIG_VQ_MVD_CODING
  AVERAGE_CDF(nmv_left->amvd_joints_cdf, nmv_tr->amvd_joints_cdf, MV_JOINTS);
  for (int i = 0; i < 2; i++) {
#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].classes_cdf, nmv_tr->comps[i].classes_cdf,
                MV_CLASSES);
    AVERAGE_CDF(nmv_left->comps[i].amvd_classes_cdf,
                nmv_tr->comps[i].amvd_classes_cdf, MV_CLASSES);
#else
    AVERAGE_CDF(nmv_left->comps[i].amvd_indices_cdf,
                nmv_tr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].class0_fp_cdf,
                nmv_tr->comps[i].class0_fp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].fp_cdf, nmv_tr->comps[i].fp_cdf, 2);
#endif  //! CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    AVERAGE_CDF(nmv_left->comps[i].class0_hp_cdf,
                nmv_tr->comps[i].class0_hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].hp_cdf, nmv_tr->comps[i].hp_cdf, 2);
    AVERAGE_CDF(nmv_left->comps[i].class0_cdf, nmv_tr->comps[i].class0_cdf,
                CLASS0_SIZE);
    AVERAGE_CDF(nmv_left->comps[i].bits_cdf, nmv_tr->comps[i].bits_cdf, 2);
#endif  // !CONFIG_VQ_MVD_CODING
  }
}

// In case of row-based multi-threading of encoder, since we always
// keep a top - right sync, we can average the top - right SB's CDFs and
// the left SB's CDFs and use the same for current SB's encoding to
// improve the performance. This function facilitates the averaging
// of CDF and used only when row-mt is enabled in encoder.
void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr) {
  unsigned int shift = compute_log2(wt_left + wt_tr);
  assert(shift - 1 < 32);
  unsigned int offset = 1 << (shift - 1);
  AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
  AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
  AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
#if !CONFIG_BY_PASS_V_SIGN
  AVERAGE_CDF(ctx_left->v_dc_sign_cdf, ctx_tr->v_dc_sign_cdf, 2);
#endif  // !CONFIG_BY_PASS_V_SIGN
  AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
              EOB_MAX_SYMS - 6);
  AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
              EOB_MAX_SYMS - 5);
  AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
              EOB_MAX_SYMS - 4);
  AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
              EOB_MAX_SYMS - 3);
#if CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
              EOB_MAX_SYMS - 3);
#else
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
              EOB_MAX_SYMS - 2);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
              EOB_MAX_SYMS - 1);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
              EOB_MAX_SYMS);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->coeff_base_eob_cdf, ctx_tr->coeff_base_eob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_bob_cdf, ctx_tr->coeff_base_bob_cdf, 3);
  AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf, ctx_tr->intra_dip_mode_n6_cdf,
              6);
  AVERAGE_CDF(ctx_left->coeff_base_lf_cdf, ctx_tr->coeff_base_lf_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_cdf, ctx_tr->coeff_base_lf_eob_cdf,
              LF_BASE_SYMBOLS - 1);
  AVERAGE_CDF(ctx_left->coeff_br_lf_cdf, ctx_tr->coeff_br_lf_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  AVERAGE_CDF(ctx_left->idtx_sign_cdf, ctx_tr->idtx_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->coeff_base_cdf_idtx, ctx_tr->coeff_base_cdf_idtx, 4);
  AVERAGE_CDF(ctx_left->coeff_br_cdf_idtx, ctx_tr->coeff_br_cdf_idtx,
              BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->inter_single_mode_cdf, ctx_tr->inter_single_mode_cdf,
              INTER_SINGLE_MODES);
  AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf, ctx_tr->coeff_base_eob_uv_cdf,
              3);
  AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf, ctx_tr->coeff_base_lf_uv_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
              ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
#if !CONFIG_COEFF_BR_LF_UV_BYPASS
  AVERAGE_CDF(ctx_left->coeff_br_lf_uv_cdf, ctx_tr->coeff_br_lf_uv_cdf,
              BR_CDF_SIZE);
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS

  AVERAGE_CDF(ctx_left->inter_warp_mode_cdf, ctx_tr->inter_warp_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
              ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);
  AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
              REFINEMV_NUM_MODES);
  AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
  AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
  AVERAGE_CDF(ctx_left->tip_drl_cdf, ctx_tr->tip_drl_cdf, 2);

  AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

  AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
              ctx_tr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
              ctx_tr->inter_compound_mode_non_joint_type_cdf,
              NUM_OPTIONS_NON_JOINT_TYPE);

  AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
              ctx_tr->inter_compound_mode_same_refs_cdf,
              INTER_COMPOUND_SAME_REFS_TYPES);
  AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->amvd_mode_cdf, ctx_tr->amvd_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf, ctx_tr->jmvd_scale_mode_cdf,
              JOINT_NEWMV_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
              ctx_tr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
              MASKED_COMPOUND_TYPES);
#if CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->wedge_quad_cdf, ctx_tr->wedge_quad_cdf, WEDGE_QUADS);
  AVERAGE_CDF(ctx_left->wedge_angle_cdf, ctx_tr->wedge_angle_cdf,
              QUAD_WEDGE_ANGLES);
#else
  AVERAGE_CDF(ctx_left->wedge_angle_dir_cdf, ctx_tr->wedge_angle_dir_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_angle_0_cdf, ctx_tr->wedge_angle_0_cdf,
              H_WEDGE_ANGLES);
  AVERAGE_CDF(ctx_left->wedge_angle_1_cdf, ctx_tr->wedge_angle_1_cdf,
              H_WEDGE_ANGLES);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->wedge_dist_cdf, ctx_tr->wedge_dist_cdf, NUM_WEDGE_DIST);
  AVERAGE_CDF(ctx_left->wedge_dist_cdf2, ctx_tr->wedge_dist_cdf2,
              NUM_WEDGE_DIST - 1);
  AVERAGE_CDF(ctx_left->warp_interintra_cdf, ctx_tr->warp_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_interintra_cdf, ctx_tr->wedge_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_mode_cdf, ctx_tr->interintra_mode_cdf,
              INTERINTRA_MODES);
  AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
  AVERAGE_CDF(ctx_left->warp_delta_param_cdf, ctx_tr->warp_delta_param_cdf,
              WARP_DELTA_NUMSYMBOLS_LOW);
  AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
              ctx_tr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  AVERAGE_CDF(ctx_left->warp_param_sign_cdf, ctx_tr->warp_param_sign_cdf, 2);
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  AVERAGE_CDF(ctx_left->warp_causal_warpmv_cdf, ctx_tr->warp_causal_warpmv_cdf,
              2);
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
              ctx_tr->warpmv_with_mvd_flag_cdf, 2);

  AVERAGE_CDF(ctx_left->warp_precision_idx_cdf, ctx_tr->warp_precision_idx_cdf,
              NUM_WARP_PRECISION_MODES);

  AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

  AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
              ctx_tr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
  AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
  AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
              TIP_PRED_MODES);
  AVERAGE_CDF(ctx_left->palette_y_size_cdf, ctx_tr->palette_y_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->palette_uv_size_cdf, ctx_tr->palette_uv_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->identity_row_cdf_y, ctx_tr->identity_row_cdf_y, 3);
  AVERAGE_CDF(ctx_left->identity_row_cdf_uv, ctx_tr->identity_row_cdf_uv, 3);
#if !CONFIG_PLT_DIR_CTX
  AVERAGE_CDF(ctx_left->palette_direction_cdf, ctx_tr->palette_direction_cdf,
              2);
#endif  // !CONFIG_PLT_DIR_CTX
  AVERAGE_CDF(ctx_left->cfl_cdf, ctx_tr->cfl_cdf, 2);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                   ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
  }
  AVERAGE_CDF(ctx_left->palette_y_mode_cdf, ctx_tr->palette_y_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_uv_mode_cdf, ctx_tr->palette_uv_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
  AVERAGE_CDF(ctx_left->txfm_do_partition_cdf, ctx_tr->txfm_do_partition_cdf,
              2);
  AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
              ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
  AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
              ctx_tr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
  AVERAGE_CDF(ctx_left->lossless_tx_size_cdf, ctx_tr->lossless_tx_size_cdf, 2);
  AVERAGE_CDF(ctx_left->lossless_inter_tx_type_cdf,
              ctx_tr->lossless_inter_tx_type_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_group_idx_cdf, ctx_tr->comp_group_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->bru_mode_cdf, ctx_tr->bru_mode_cdf, 3);
  AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, wt_left, wt_tr, offset, shift);
  avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, wt_left, wt_tr, offset, shift);
  AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
  AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf, 2);
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  AVERAGE_CDF(ctx_left->intrabc_drl_idx_cdf, ctx_tr->intrabc_drl_idx_cdf, 2);
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX
  AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
              ctx_tr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
  AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
#if CONFIG_EXT_SEG
  AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.tree_cdf1, ctx_tr->seg.tree_cdf1, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf1,
              ctx_tr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.seg_id_ext_flag_cdf,
              ctx_tr->seg.seg_id_ext_flag_cdf, 2);
#else
  AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
#endif  // CONFIG_EXT_SEG
  AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
              ctx_tr->switchable_flex_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
  AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
              ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                   CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  AVERAGE_CDF(ctx_left->gdf_cdf, ctx_tr->gdf_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_restore_cdf, ctx_tr->wienerns_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_length_cdf, ctx_tr->wienerns_length_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf, ctx_tr->wienerns_uv_sym_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_4part_cdf, ctx_tr->wienerns_4part_cdf, 4);
  AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf, ctx_tr->pc_wiener_restore_cdf,
              2);
#if !CONFIG_MERGE_PARA_CTX
  AVERAGE_CDF(ctx_left->merged_param_cdf, ctx_tr->merged_param_cdf, 2);
#endif  // !CONFIG_MERGE_PARA_CTX
  AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf, FSC_MODES);
  AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf, MRL_LINE_NUMBER);
  AVERAGE_CDF(ctx_left->multi_line_mrl_cdf, ctx_tr->multi_line_mrl_cdf, 2);

  AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf, ctx_tr->dpcm_vert_horz_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf, ctx_tr->dpcm_uv_vert_horz_cdf,
              2);

  AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf, MHCCP_MODE_NUM);
#if MHCCP_RUNTIME_FLAG
  AVERAGE_CDF(ctx_left->cfl_mhccp_cdf, ctx_tr->cfl_mhccp_cdf,
              CFL_MHCCP_SWITCH_NUM);
#endif  // CONFIG_RUNTIME_FLAG
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
              CFL_TYPE_COUNT - 1);
  AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
              INTRA_MODE_SETS);
#if CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf, ctx_tr->y_mode_idx_cdf,
              LUMA_INTRA_MODE_INDEX_COUNT);
  AVERAGE_CDF(ctx_left->y_mode_idx_offset_cdf, ctx_tr->y_mode_idx_offset_cdf,
              LUMA_INTRA_MODE_OFFSET_COUNT);
  AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
              CHROMA_INTRA_MODE_INDEX_COUNT);
#else
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_0, ctx_tr->y_mode_idx_cdf_0,
              FIRST_MODE_COUNT);
#if !CONFIG_CTX_Y_SECOND_MODE
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_1, ctx_tr->y_mode_idx_cdf_1,
              SECOND_MODE_COUNT);
#endif  // !CONFIG_CTX_Y_SECOND_MODE
  AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf, UV_INTRA_MODES - 1);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf, REGION_TYPES);

  AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
  AVERAGE_CDF(ctx_left->do_square_split_cdf, ctx_tr->do_square_split_cdf, 2);
  AVERAGE_CDF(ctx_left->rect_type_cdf, ctx_tr->rect_type_cdf, 2);
  AVERAGE_CDF(ctx_left->do_ext_partition_cdf, ctx_tr->do_ext_partition_cdf, 2);
  AVERAGE_CDF(ctx_left->do_uneven_4way_partition_cdf,
              ctx_tr->do_uneven_4way_partition_cdf, 2);
#if !CONFIG_NEW_PART_CTX
  AVERAGE_CDF(ctx_left->uneven_4way_partition_type_cdf,
              ctx_tr->uneven_4way_partition_type_cdf, NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
  AVERAGE_CDF(ctx_left->switchable_interp_cdf, ctx_tr->switchable_interp_cdf,
              SWITCHABLE_FILTERS);
  AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf, DELTA_Q_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_multi_cdf, ctx_tr->delta_lf_multi_cdf,
              DELTA_LF_PROBS + 1);
  AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
              ctx_tr->inter_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
              ctx_tr->intra_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1], ctx_tr->intra_ext_tx_cdf[1],
                 INTRA_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2], ctx_tr->intra_ext_tx_cdf[2],
                 INTRA_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1], ctx_tr->inter_ext_tx_cdf[1],
                 INTER_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2], ctx_tr->inter_ext_tx_cdf[2],
                 INTER_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3], ctx_tr->inter_ext_tx_cdf[3],
                 INTER_TX_SET3, CDF_SIZE(TX_TYPES));
#if CONFIG_REDUCED_TX_SET_EXT
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[4], ctx_tr->inter_ext_tx_cdf[4],
                 INTER_TX_SET4, CDF_SIZE(TX_TYPES));
#endif  // CONFIG_REDUCED_TX_SET_EXT
#if CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->inter_tx_type_set, ctx_tr->inter_tx_type_set, 2);
  AVERAGE_CDF(ctx_left->inter_tx_type_idx, ctx_tr->inter_tx_type_idx,
              INTER_TX_TYPE_INDEX_COUNT);
  AVERAGE_CDF(ctx_left->inter_tx_type_offset_1, ctx_tr->inter_tx_type_offset_1,
              INTER_TX_TYPE_OFFSET1_COUNT);
  AVERAGE_CDF(ctx_left->inter_tx_type_offset_2, ctx_tr->inter_tx_type_offset_2,
              INTER_TX_TYPE_OFFSET2_COUNT);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
              CFL_ALPHABET_SIZE);
  AVG_CDF_STRIDE(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES,
                 CDF_SIZE(STX_TYPES));
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
              ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
              ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
              IST_REDUCE_SET_SIZE_ADST_ADST);

  AVERAGE_CDF(ctx_left->pb_mv_mpp_flag_cdf, ctx_tr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      AVG_CDF_STRIDE(
          ctx_left->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          ctx_tr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  AVERAGE_CDF(ctx_left->coeff_base_ph_cdf, ctx_tr->coeff_base_ph_cdf, 4);
#if !CONFIG_COEFF_BR_PH_BYPASS
  AVERAGE_CDF(ctx_left->coeff_br_ph_cdf, ctx_tr->coeff_br_ph_cdf, 4);
#endif  // !CONFIG_COEFF_BR_PH_BYPASS
  AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf, CCTX_TYPES);
}

void av1_setup_frame_contexts(AV1_COMMON *cm) {
  // Store the frame context into a special slot (not associated with any
  // reference buffer), so that we can set up cm->pre_fc correctly later
  // This function must ONLY be called when cm->fc has been initialized with
  // default probs, either by av1_setup_past_independence or after manually
  // initializing them
  *cm->default_frame_context = *cm->fc;
  // TODO(jack.haughton@argondesign.com): don't think this should be necessary,
  // but could do with fuller testing
  if (cm->tiles.large_scale) {
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      RefCntBuffer *const buf = get_ref_frame_buf(cm, i);
      if (buf != NULL) buf->frame_context = *cm->fc;
    }
    for (int i = 0; i < FRAME_BUFFERS; ++i)
      cm->buffer_pool->frame_bufs[i].frame_context = *cm->fc;
  }
}

void av1_setup_past_independence(AV1_COMMON *cm) {
  // Reset the segment feature data to the default stats:
  // Features disabled, 0, with delta coding (Default state).
  av1_clearall_segfeatures(&cm->seg);

  if (cm->cur_frame->seg_map) {
    memset(cm->cur_frame->seg_map, 0,
           (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
  }

  // reset mode ref deltas
  av1_set_default_ref_deltas(cm->cur_frame->ref_deltas);
  av1_set_default_mode_deltas(cm->cur_frame->mode_deltas);
  set_default_lf_deltas(&cm->lf);

  av1_default_coef_probs(cm);
  init_mode_probs(cm->fc, &cm->seq_params);
  av1_init_mv_probs(cm);
  cm->fc->initialized = 1;
  av1_setup_frame_contexts(cm);
}
