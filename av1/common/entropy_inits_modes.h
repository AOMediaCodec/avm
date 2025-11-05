/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_COMMON_ENTROPY_INITS_MODES_H_
#define AOM_AV1_COMMON_ENTROPY_INITS_MODES_H_

#include "config/aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif
/* clang-format off */

static const aom_cdf_prob default_inter_single_mode_cdf[5][CDF_SIZE(3)] = {
  { AOM_CDF3(11516, 11715),   0 },
  { AOM_CDF3(24656, 24680),   0 },
  { AOM_CDF3(27225, 27243),   0 },
  { AOM_CDF3(13331, 13366),   0 },
  { AOM_CDF3(16063, 16170),   0 },
};

static const aom_cdf_prob default_inter_warp_mode_cdf[5][CDF_SIZE(2)] = {
  { AOM_CDF2(31021), 118 },
  { AOM_CDF2(25430),  76 },
  { AOM_CDF2(22319),  76 },
  { AOM_CDF2(21114),   1 },
  { AOM_CDF2(17583),   1 },
};

static const aom_cdf_prob default_is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)] = { AOM_CDF2(11941),   0 };

static const aom_cdf_prob default_refinemv_flag_cdf[24][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_drl_cdf[3][5][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(20581), 118 },
    { AOM_CDF2(25770),  90 },
    { AOM_CDF2(27043),  75 },
    { AOM_CDF2(22024), 118 },
    { AOM_CDF2(16590), 118 },
  },
  {
    { AOM_CDF2(20638), 118 },
    { AOM_CDF2(20418),  90 },
    { AOM_CDF2(21113), 115 },
    { AOM_CDF2(19645), 123 },
    { AOM_CDF2(19650),  90 },
  },
  {
    { AOM_CDF2(26306),  90 },
    { AOM_CDF2(25139), 115 },
    { AOM_CDF2(23285),  76 },
    { AOM_CDF2(26265), 115 },
    { AOM_CDF2(23464), 118 },
  },
};

static const aom_cdf_prob default_tip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31561),   0 },
  { AOM_CDF2(27203),   0 },
  { AOM_CDF2(21916),   0 },
};

static const aom_cdf_prob default_skip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(25396),   0 },
  { AOM_CDF2(23881),   0 },
  { AOM_CDF2(21508),   0 },
};

static const aom_cdf_prob default_use_optflow_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_inter_compound_mode_is_joint_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(25397),   0 },
  { AOM_CDF2(32752),   0 },
};

static const aom_cdf_prob default_inter_compound_mode_non_joint_type_cdf[5][CDF_SIZE(5)] = {
  { AOM_CDF5(14431, 21078, 25375, 27262),   0 },
  { AOM_CDF5(22433, 25805, 28788, 30114),   0 },
  { AOM_CDF5(25873, 28046, 30292, 31275),   0 },
  { AOM_CDF5(17660, 22114, 26307, 27200),   0 },
  { AOM_CDF5(21832, 24917, 28451, 28996),   0 },
};

static const aom_cdf_prob default_inter_compound_mode_same_refs_cdf[5][CDF_SIZE(4)] = {
  { AOM_CDF4(10155, 29278, 29355),   0 },
  { AOM_CDF4(16755, 29980, 30097),   0 },
  { AOM_CDF4(20563, 30064, 30344),   0 },
  { AOM_CDF4(12042, 28942, 29047),   0 },
  { AOM_CDF4(13526, 28071, 28214),   0 },
};

static const aom_cdf_prob default_cwp_idx_cdf[2][4][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(13851),  31 },
    { AOM_CDF2(15058),  31 },
    { AOM_CDF2(21728),  31 },
    { AOM_CDF2(21219),  31 },
  },
  {
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
};

static const aom_cdf_prob default_jmvd_scale_mode_cdf[CDF_SIZE(5)] = { AOM_CDF5(18498, 21150, 23573, 28129),   1 };

static const aom_cdf_prob default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(3)] = { AOM_CDF3(24903, 28074),  75 };

static const aom_cdf_prob default_compound_type_cdf[CDF_SIZE(2)] = { AOM_CDF2(18804),   6 };

static const aom_cdf_prob default_amvd_mode_cdf[9][3][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(11007),   0 },
    { AOM_CDF2(11114),   0 },
    { AOM_CDF2(10230),   0 },
  },
  {
    { AOM_CDF2(11219),   0 },
    { AOM_CDF2(10158),   0 },
    { AOM_CDF2(10997),   0 },
  },
  {
    { AOM_CDF2( 8100),   0 },
    { AOM_CDF2( 6742),   0 },
    { AOM_CDF2( 9818),   0 },
  },
  {
    { AOM_CDF2( 7594),   0 },
    { AOM_CDF2( 6653),   0 },
    { AOM_CDF2( 7058),   0 },
  },
  {
    { AOM_CDF2(17652),   0 },
    { AOM_CDF2(14448),   0 },
    { AOM_CDF2(13409),   0 },
  },
  {
    { AOM_CDF2(14808),   0 },
    { AOM_CDF2(13708),   0 },
    { AOM_CDF2(13027),   0 },
  },
  {
    { AOM_CDF2( 8699),   0 },
    { AOM_CDF2( 7036),   0 },
    { AOM_CDF2( 7728),   0 },
  },
  {
    { AOM_CDF2( 9396),   0 },
    { AOM_CDF2( 8093),   0 },
    { AOM_CDF2(10339),   0 },
  },
  {
    { AOM_CDF2(13855),   0 },
    { AOM_CDF2(11902),   0 },
    { AOM_CDF2(11445),   0 },
  },
};

static const aom_cdf_prob default_wedge_quad_cdf[CDF_SIZE(4)] = { AOM_CDF4( 9105, 18210, 25489),  93 };

static const aom_cdf_prob default_wedge_angle_cdf[4][CDF_SIZE(5)] = {
  { AOM_CDF5( 6495, 14916, 23085, 27549),  76 },
  { AOM_CDF5(12000, 15000, 22500, 28500),  76 },
  { AOM_CDF5(16520, 21143, 25198, 28761),  76 },
  { AOM_CDF5(13800, 16100, 23000, 27600),  76 },
};

static const aom_cdf_prob default_wedge_dist_cdf[CDF_SIZE(4)] = { AOM_CDF4( 5746, 15860, 20435),  75 };

static const aom_cdf_prob default_wedge_dist_cdf2[CDF_SIZE(3)] = { AOM_CDF3(11164, 18454),  90 };

static const aom_cdf_prob default_warp_interintra_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_interintra_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2(30376),  75 },
  { AOM_CDF2(20784),   1 },
  { AOM_CDF2(22326),   1 },
  { AOM_CDF2(24412),   1 },
};

static const aom_cdf_prob default_wedge_interintra_cdf[CDF_SIZE(2)] = { AOM_CDF2(14247),  75 };

static const aom_cdf_prob default_interintra_mode_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4( 5420, 20952, 31034),   7 },
  { AOM_CDF4( 1948, 17325, 31146),  75 },
  { AOM_CDF4( 3623, 17784, 29374),   1 },
  { AOM_CDF4( 2843, 14004, 27752),   7 },
};

static const aom_cdf_prob default_warp_ref_idx0_cdf[1][CDF_SIZE(2)] = {
  { AOM_CDF2(21704),  90 },
};

static const aom_cdf_prob default_warp_ref_idx1_cdf[1][CDF_SIZE(2)] = {
  { AOM_CDF2(23581), 115 },
};

static const aom_cdf_prob default_warp_ref_idx2_cdf[1][CDF_SIZE(2)] = {
  { AOM_CDF2(21767), 123 },
};

static const aom_cdf_prob default_warp_causal_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2(17055),   0 },
  { AOM_CDF2(20889),   0 },
  { AOM_CDF2(17980),   0 },
  { AOM_CDF2(17863),   0 },
};

static const aom_cdf_prob default_warp_precision_idx_cdf[31][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_warp_delta_param_cdf[2][CDF_SIZE(8)] = {
  { AOM_CDF8( 9210, 20919, 23883, 28837, 29680, 31427, 31670),   0 },
  { AOM_CDF8(12722, 22620, 25595, 29576, 30362, 31750, 31968),   0 },
};

static const aom_cdf_prob default_warp_delta_param_high_cdf[2][CDF_SIZE(8)] = {
  { AOM_CDF8( 7419, 12373, 17584, 20281, 23728, 27184, 29430),   0 },
  { AOM_CDF8( 7417, 12119, 17352, 20027, 23490, 26882, 29213),   0 },
};

static const aom_cdf_prob default_warp_param_sign_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_warp_extend_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(20856),   0 },
  { AOM_CDF2(18023),   0 },
  { AOM_CDF2(16560),   0 },
};

static const aom_cdf_prob default_warpmv_with_mvd_flag_cdf[CDF_SIZE(2)] = { AOM_CDF2(16283),   0 };

static const aom_cdf_prob default_bawp_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(27422),   1 },
  { AOM_CDF2(15131),   6 },
};

static const aom_cdf_prob default_explicit_bawp_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(19664),   0 },
  { AOM_CDF2(21664),   0 },
  { AOM_CDF2(23664),   0 },
};

static const aom_cdf_prob default_explicit_bawp_scale_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_tip_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31852), 118 },
  { AOM_CDF2(18438),  90 },
  { AOM_CDF2( 8752),  90 },
};

static const aom_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(29069),   0 };

static const aom_cdf_prob default_palette_y_size_cdf[CDF_SIZE(7)] = { AOM_CDF7( 7882, 14719, 20582, 24895, 28455, 30765),  70 };

static const aom_cdf_prob default_palette_uv_size_cdf[CDF_SIZE(7)] = { AOM_CDF7( 8195, 19832, 25803, 29025, 30747, 31974),  50 };

static const aom_cdf_prob default_identity_row_cdf_y[4][CDF_SIZE(3)] = {
  { AOM_CDF3(10923, 21845),  25 },
  { AOM_CDF3(10923, 21845),  31 },
  { AOM_CDF3(10923, 21845),  33 },
  { AOM_CDF3(10923, 21845),  36 },
};

static const aom_cdf_prob default_identity_row_cdf_uv[4][CDF_SIZE(3)] = {
  { AOM_CDF3(10923, 21845),  32 },
  { AOM_CDF3(10923, 21845),  57 },
  { AOM_CDF3(10923, 21845),  30 },
  { AOM_CDF3(10923, 21845),  56 },
};

static const aom_cdf_prob default_palette_y_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
{
  { AOM_CDF2(27953), 120 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2( 8822),  60 },
  { AOM_CDF2(27664),  25 },
  { AOM_CDF2(30470),   0 },
},
{
  { AOM_CDF3(25275, 28972),  70 },
  { AOM_CDF3(12296, 25651),  85 },
  { AOM_CDF3( 7235, 28020),  45 },
  { AOM_CDF3(20976, 25896),  50 },
  { AOM_CDF3(30329, 31531),  60 },
},
{
  { AOM_CDF4(23381, 26612, 28974),  70 },
  { AOM_CDF4(10717, 22776, 27574),  70 },
  { AOM_CDF4( 6423, 25697, 29375), 110 },
  { AOM_CDF4(19518, 23623, 28343),  60 },
  { AOM_CDF4(30332, 31099, 31857),  50 },
},
{
  { AOM_CDF5(23985, 26707, 28381, 29936),  60 },
  { AOM_CDF5( 9530, 19953, 23789, 28724),  60 },
  { AOM_CDF5( 6047, 24926, 27833, 30371),   0 },
  { AOM_CDF5(20148, 23267, 27043, 28890),  70 },
  { AOM_CDF5(30829, 31286, 31746, 32197),   0 },
},
{
  { AOM_CDF6(21093, 24342, 26015, 27567, 29307),   0 },
  { AOM_CDF6( 8394, 18615, 21558, 26002, 28573),  50 },
  { AOM_CDF6( 5813, 23913, 26226, 28289, 30468),  10 },
  { AOM_CDF6(19001, 22337, 25424, 26705, 28663),  50 },
  { AOM_CDF6(30558, 31067, 31403, 31756, 32196),  60 },
},
{
  { AOM_CDF7(22109, 24619, 25982, 27240, 28446, 29822),  60 },
  { AOM_CDF7( 8462, 18350, 20440, 25007, 26774, 28765),   0 },
  { AOM_CDF7( 6112, 23012, 25022, 26868, 28683, 30708),   0 },
  { AOM_CDF7(22602, 24933, 27205, 28064, 28697, 29814), 100 },
  { AOM_CDF7(30803, 31187, 31462, 31737, 32020, 32331),  50 },
},
{
  { AOM_CDF8(22076, 24783, 25777, 26792, 27691, 28748, 29942),  50 },
  { AOM_CDF8( 8293, 17397, 19329, 23699, 25608, 27188, 28865),   0 },
  { AOM_CDF8( 7220, 21444, 23642, 25420, 27182, 28981, 30832),   0 },
  { AOM_CDF8(23128, 24999, 27546, 28542, 29286, 29744, 30499),  50 },
  { AOM_CDF8(30626, 30964, 31219, 31444, 31661, 31956, 32318),  10 },
},
};

static const aom_cdf_prob default_palette_y_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(24488),  55 };

static const aom_cdf_prob default_palette_uv_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(30790),  50 };

static const aom_cdf_prob default_comp_inter_cdf[5][CDF_SIZE(2)] = {
  { AOM_CDF2(27078),  75 },
  { AOM_CDF2(22913),   1 },
  { AOM_CDF2(15254),   1 },
  { AOM_CDF2(13473),   1 },
  { AOM_CDF2( 5765),   0 },
};

static const aom_cdf_prob default_single_ref_cdf[3][6][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(27505),   0 },
    { AOM_CDF2(26743),   0 },
    { AOM_CDF2(29193),  75 },
    { AOM_CDF2(29517),   0 },
    { AOM_CDF2(30241),   0 },
    { AOM_CDF2(30024),   1 },
  },
  {
    { AOM_CDF2(17869),   1 },
    { AOM_CDF2(16112),   6 },
    { AOM_CDF2(19968),   6 },
    { AOM_CDF2(17247),  31 },
    { AOM_CDF2(17293),  32 },
    { AOM_CDF2(11155),  32 },
  },
  {
    { AOM_CDF2( 6276),   0 },
    { AOM_CDF2( 5153),   0 },
    { AOM_CDF2( 6631),   1 },
    { AOM_CDF2( 4257),   6 },
    { AOM_CDF2( 3798),   1 },
    { AOM_CDF2( 1983),   0 },
  },
};

static const aom_cdf_prob default_comp_ref0_cdf[3][6][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(11015),  32 },
    { AOM_CDF2(14938),  32 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
  {
    { AOM_CDF2( 1829),  75 },
    { AOM_CDF2( 3838),   6 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
  {
    { AOM_CDF2( 1233),  75 },
    { AOM_CDF2( 1491),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
};

static const aom_cdf_prob default_comp_ref1_cdf[3][2][6][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(31947), 115 },
      { AOM_CDF2(29267),   5 },
      { AOM_CDF2(29617),   1 },
      { AOM_CDF2(30617),  26 },
      { AOM_CDF2(31720),   8 },
      { AOM_CDF2(31208),  32 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(19512),  31 },
      { AOM_CDF2(28511),   1 },
      { AOM_CDF2(27987),  31 },
      { AOM_CDF2(29764),  30 },
      { AOM_CDF2(29015),  51 },
    },
  },
  {
    {
      { AOM_CDF2(31833),  93 },
      { AOM_CDF2(26128),  31 },
      { AOM_CDF2(21282),  31 },
      { AOM_CDF2(18036),  57 },
      { AOM_CDF2(21050),  32 },
      { AOM_CDF2(14939),  57 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2( 7402),   1 },
      { AOM_CDF2(16893),   6 },
      { AOM_CDF2(13997),  31 },
      { AOM_CDF2(13067),  31 },
      { AOM_CDF2( 7202),  57 },
    },
  },
  {
    {
      { AOM_CDF2(26394),  31 },
      { AOM_CDF2(15795),  31 },
      { AOM_CDF2( 6816),  31 },
      { AOM_CDF2( 3530),  57 },
      { AOM_CDF2( 5621),  59 },
      { AOM_CDF2( 2839),  71 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2( 1576),  93 },
      { AOM_CDF2( 5081),   1 },
      { AOM_CDF2( 2205),   6 },
      { AOM_CDF2( 1859),   6 },
      { AOM_CDF2(  925),   6 },
    },
  },
};

static const aom_cdf_prob default_comp_group_idx_cdfs[12][CDF_SIZE(2)] = {
  { AOM_CDF2( 9916),   0 },
  { AOM_CDF2( 7647),   0 },
  { AOM_CDF2( 4172),   0 },
  { AOM_CDF2( 6353),   1 },
  { AOM_CDF2( 3423),   0 },
  { AOM_CDF2( 4917),   6 },
  { AOM_CDF2(11013),   0 },
  { AOM_CDF2( 5843),   0 },
  { AOM_CDF2( 7222),   1 },
  { AOM_CDF2( 3158),   1 },
  { AOM_CDF2( 2495),   0 },
  { AOM_CDF2( 4723),   3 },
};

static const aom_cdf_prob default_skip_mode_cdfs[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31439),   0 },
  { AOM_CDF2(22696),   0 },
  { AOM_CDF2(12045),   0 },
};

static const aom_cdf_prob default_lossless_tx_size_cdf[4][2][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(16384),   1 },
    { AOM_CDF2(16384),   1 },
  },
  {
    { AOM_CDF2(16384),  75 },
    { AOM_CDF2(16384),  75 },
  },
  {
    { AOM_CDF2(16384),  75 },
    { AOM_CDF2(16384),  75 },
  },
  {
    { AOM_CDF2(16384),  75 },
    { AOM_CDF2(16384),  75 },
  },
};

static const aom_cdf_prob default_lossless_inter_tx_type_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),  75 };

static const aom_cdf_prob default_skip_txfm_cdfs[6][CDF_SIZE(2)] = {
  { AOM_CDF2(23601),   0 },
  { AOM_CDF2(12657),  75 },
  { AOM_CDF2( 3777),  90 },
  { AOM_CDF2(23222),   1 },
  { AOM_CDF2( 8799),  76 },
  { AOM_CDF2( 1437),  90 },
};

static const aom_cdf_prob default_bru_mode_cdf[CDF_SIZE(3)] = { AOM_CDF3( 4124, 16615),   0 };

static const aom_cdf_prob default_intra_inter_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2( 2375),  75 },
  { AOM_CDF2(16902),  75 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(29584),   0 },
};

static const aom_cdf_prob default_intrabc_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(30958),   1 },
  { AOM_CDF2(19490),   0 },
  { AOM_CDF2( 8708),  90 },
};

static const aom_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(26560),  31 };

static const aom_cdf_prob default_intrabc_bv_precision_cdf[1][CDF_SIZE(2)] = {
  { AOM_CDF2(24576),   0 },
};

static const aom_cdf_prob default_switchable_flex_restore_cdf[3][3][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(21337),  37 },
    { AOM_CDF2(13763),  32 },
    { AOM_CDF2(13677),  37 },
  },
  {
    { AOM_CDF2(20429),  37 },
    { AOM_CDF2(22496),  37 },
    { AOM_CDF2(18867),  37 },
  },
  {
    { AOM_CDF2(19205),  37 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
  },
};

static const aom_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = { AOM_CDF2( 6995),  37 };

static const aom_cdf_prob default_wienerns_length_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),  61 },
  { AOM_CDF2(16384),  31 },
};

static const aom_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),  57 };

static const aom_cdf_prob default_wienerns_4part_cdf[1][CDF_SIZE(4)] = {
  { AOM_CDF4(16384, 24576, 28672),   7 },
};

static const aom_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = { AOM_CDF2(14330),   8 };

static const aom_cdf_prob default_ccso_cdf[3][4][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(24690),  37 },
    { AOM_CDF2(17161),  37 },
    { AOM_CDF2(10618),  37 },
    { AOM_CDF2( 7830),  37 },
  },
  {
    { AOM_CDF2(26090),  37 },
    { AOM_CDF2(18122),  37 },
    { AOM_CDF2(12248),  37 },
    { AOM_CDF2( 8523),  37 },
  },
  {
    { AOM_CDF2(27240),  37 },
    { AOM_CDF2(18679),  37 },
    { AOM_CDF2(10831),  37 },
    { AOM_CDF2( 7846),  37 },
  },
};

static const aom_cdf_prob default_cdef_strength_index0_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2(24690),  37 },
  { AOM_CDF2(17161),  37 },
  { AOM_CDF2(10618),  37 },
  { AOM_CDF2( 7830),  37 },
};

static const aom_cdf_prob default_cdef_cdf[CDEF_STRENGTHS_NUM - 1][CDF_SIZE(CDEF_STRENGTHS_NUM)] = {
{ AOM_CDF2(16384),   0 },
{ AOM_CDF3(10923, 21845),   0 },
{ AOM_CDF4( 8192, 16384, 24576),   0 },
{ AOM_CDF5( 6554, 13107, 19661, 26214),   0 },
{ AOM_CDF6( 5461, 10923, 16384, 21845, 27307),   0 },
{ AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
};

static const aom_cdf_prob default_gdf_cdf[CDF_SIZE(2)] = { AOM_CDF2(11570),   0 };

static const aom_cdf_prob default_mrl_index_cdf[3][CDF_SIZE(4)] = {
  { AOM_CDF4(28081, 30613, 31659),  78 },
  { AOM_CDF4(22175, 28045, 30623),  75 },
  { AOM_CDF4(17175, 25921, 29682),   1 },
};

static const aom_cdf_prob default_multi_line_mrl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(28081),   0 },
  { AOM_CDF2(22175),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_dpcm_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_dpcm_vert_horz_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_dpcm_uv_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_dpcm_uv_vert_horz_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384),   0 };

static const aom_cdf_prob default_fsc_mode_cdf[4][6][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(29820),  78 },
    { AOM_CDF2(31107),  78 },
    { AOM_CDF2(32018), 118 },
    { AOM_CDF2(32202), 118 },
    { AOM_CDF2(32482), 118 },
    { AOM_CDF2(32539), 123 },
  },
  {
    { AOM_CDF2(27906),  75 },
    { AOM_CDF2(27439),  90 },
    { AOM_CDF2(29059),   1 },
    { AOM_CDF2(28167),  76 },
    { AOM_CDF2(27696),   7 },
    { AOM_CDF2(22842),  54 },
  },
  {
    { AOM_CDF2(26882), 115 },
    { AOM_CDF2(22539),  95 },
    { AOM_CDF2(23495),   8 },
    { AOM_CDF2(18016),   7 },
    { AOM_CDF2(11559),  49 },
    { AOM_CDF2( 4688),   4 },
  },
  {
    { AOM_CDF2(29627), 118 },
    { AOM_CDF2(29794),  93 },
    { AOM_CDF2(32190), 123 },
    { AOM_CDF2(32289), 123 },
    { AOM_CDF2(32618), 123 },
    { AOM_CDF2(32583), 123 },
  },
};

static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(2)] = { AOM_CDF2(18000),   0 };

static const aom_cdf_prob default_cfl_mhccp_switch_cdf[CDF_SIZE(2)] = { AOM_CDF2(18000),   0 };

static const aom_cdf_prob default_cfl_sign_cdf[CDF_SIZE(8)] = { AOM_CDF8( 5534,  6742, 11998, 19905, 28459, 29805, 32596),  37 };

static const aom_cdf_prob default_cfl_alpha_cdf[6][CDF_SIZE(8)] = {
  { AOM_CDF8(10366, 17785, 28218, 30893, 32471, 32638, 32666),  62 },
  { AOM_CDF8( 4247, 18221, 24527, 31454, 32425, 32695, 32714),  37 },
  { AOM_CDF8(11483, 20769, 27162, 28811, 32007, 32287, 32375),  62 },
  { AOM_CDF8(27996, 31615, 32179, 32454, 32541, 32587, 32607),  62 },
  { AOM_CDF8(18158, 24791, 28870, 29367, 31384, 31714, 32004),  37 },
  { AOM_CDF8(18147, 27954, 31623, 31810, 31958, 32276, 32341),  62 },
};

static const aom_cdf_prob default_y_mode_set_cdf[CDF_SIZE(4)] = { AOM_CDF4(28618, 30909, 31555), 118 };

static const aom_cdf_prob default_y_mode_idx_cdf[3][CDF_SIZE(8)] = {
  { AOM_CDF8(14967, 20223, 22467, 24775, 26294, 27253, 28348),  75 },
  { AOM_CDF8(10399, 14457, 16589, 18447, 19804, 24728, 26455),  75 },
  { AOM_CDF8( 5342,  7123,  8352,  9283,  9845, 17570, 23158),  75 },
};

static const aom_cdf_prob default_y_mode_idx_offset_cdf[3][CDF_SIZE(6)] = {
  { AOM_CDF6(15256, 19609, 22218, 25531, 27739),  75 },
  { AOM_CDF6(11526, 16924, 21400, 25152, 28399),  75 },
  { AOM_CDF6(11465, 16384, 21089, 26781, 29709),  75 },
};

static const aom_cdf_prob default_uv_mode_cdf[2][CDF_SIZE(8)] = {
  { AOM_CDF8(13848, 18930, 20641, 22133, 23986, 25450, 28075),   0 },
  { AOM_CDF8(19268, 22648, 25651, 26449, 27288, 28840, 29451),   0 },
};

static const aom_cdf_prob default_switchable_interp_cdf[16][CDF_SIZE(3)] = {
  { AOM_CDF3(31476, 32736),   0 },
  { AOM_CDF3( 1637, 32702),  75 },
  { AOM_CDF3(   11,   709),  90 },
  { AOM_CDF3(27634, 32442),   6 },
  { AOM_CDF3(30451, 30981),   0 },
  { AOM_CDF3( 8963, 32500),   6 },
  { AOM_CDF3(  370,   693),  75 },
  { AOM_CDF3(25697, 27654),  31 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
  { AOM_CDF3(10923, 21845),   0 },
};

static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(8)] = { AOM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613),  56 };

static const aom_cdf_prob default_delta_lf_multi_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(28160, 32120, 32677),   0 },
  { AOM_CDF4(28160, 32120, 32677),   0 },
  { AOM_CDF4(28160, 32120, 32677),   0 },
  { AOM_CDF4(28160, 32120, 32677),   0 },
};

static const aom_cdf_prob default_delta_lf_cdf[CDF_SIZE(4)] = { AOM_CDF4( 8192, 16384, 24576),   0 };

static const aom_cdf_prob default_intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)] = {
  {
    { 0 },
    { 0 },
    { 0 },
    { 0 },
  },
{
  { AOM_CDF7( 3910, 13624, 16648, 19644, 23773, 27952),  35 },
  { AOM_CDF7(11788, 21074, 24067, 27345, 29126, 30842),  40 },
  { AOM_CDF7(11068, 21436, 24806, 28312, 29521, 31139),  60 },
  { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
},
  {
    { AOM_CDF2(16384), 0 },
    { AOM_CDF2(16384), 0 },
    { AOM_CDF2(16384), 0 },
    { AOM_CDF2(16384), 0 },
  },
};

static const aom_cdf_prob default_intra_ext_tx_short_side_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(11656, 26664, 29603),   0 },
  { AOM_CDF4(22336, 31457, 32748),   0 },
  { AOM_CDF4(24537, 32017, 32748),   0 },
  { AOM_CDF4( 8192, 16384, 24576),   0 },
};

static const aom_cdf_prob default_inter_tx_type_set_cdf[2][3][4][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(20576),   0 },
      { AOM_CDF2(16541),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16147),   0 },
      { AOM_CDF2(30302),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(25522),   0 },
      { AOM_CDF2(23776),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
  },
  {
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(22866),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(32277),   0 },
      { AOM_CDF2(16384),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(29701),   0 },
      { AOM_CDF2(16384),   0 },
    },
  },
};

static const aom_cdf_prob default_inter_tx_type_idx_cdf[2][3][CDF_SIZE(8)] = {
  {
    { AOM_CDF8(14380, 16658, 18988, 22077, 25194, 27959, 30950),   0 },
    { AOM_CDF8(  442,   561,   737,  1131,  1214,  1494,  1624),   0 },
    { AOM_CDF8(18414, 19897, 21481, 23996, 27098, 29474, 32457),   0 },
  },
  {
    { AOM_CDF8( 3612,  5799,  8468, 16149, 20607, 25655, 29211),   0 },
    { AOM_CDF8(  135,   279,   923, 31785, 32279, 32421, 32564),   0 },
    { AOM_CDF8(26230, 26927, 28614, 29682, 30095, 30901, 31759),   0 },
  },
};

static const aom_cdf_prob default_inter_tx_type_offset_1_cdf[3][CDF_SIZE(8)] = {
  { AOM_CDF8( 2787,  6375,  8620, 11630, 17688, 22228, 27481),   0 },
  { AOM_CDF8(  401,   499,   693,   829,  2141,  2356,  2791),   0 },
  { AOM_CDF8( 1984,  5908,  7465, 10371, 17345, 21585, 26968),   0 },
};

static const aom_cdf_prob default_inter_tx_type_offset_2_cdf[3][CDF_SIZE(4)] = {
  { AOM_CDF4(10244, 16768, 25390),   0 },
  { AOM_CDF4(10230, 16432, 24377),   0 },
  { AOM_CDF4( 8301, 19386, 25851),   0 },
};


static const aom_cdf_prob default_inter_ext_tx_cdf[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)] = {
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
    { AOM_CDF16( 9037, 10470, 11932, 13873, 15828, 17558, 19436, 20576, 21620, 22970, 23724, 24861, 27101, 28740, 30607),  75 },
    { AOM_CDF16( 4837,  5975,  7101,  8771, 10184, 11652, 13003, 16541, 18453, 20761, 22240, 24190, 26752, 28498, 30554),  75 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
  },
  {
    { AOM_CDF16( 2911,  3858,  4826,  5546,  5683,  6221,  6591, 16147, 16206, 16256, 16318, 16534, 21589, 24753, 28642),   6 },
    { AOM_CDF16(  409,   519,   682,  1046,  1123,  1381,  1501, 30302, 30605, 30678, 30823, 30928, 31926, 32088, 32409),   1 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
  },
  {
    { AOM_CDF16(14339, 15492, 16743, 18715, 21104, 22943, 25268, 25522, 25961, 26821, 27184, 27829, 29338, 30204, 31281),  78 },
    { AOM_CDF16(14549, 15847, 17117, 18394, 19905, 21029, 22382, 23776, 24880, 26193, 27197, 28407, 29553, 30713, 31757),  75 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
    { AOM_CDF16( 2048,  4096,  6144,  8192, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720),   0 },
  },
},
{
  {
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12( 2522,  4047,  5909, 11268, 14390, 17900, 20374, 22866, 25954, 27992, 30446),  75 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
  },
  {
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12(  133,   275,   909, 31300, 31753, 31888, 32028, 32277, 32529, 32562, 32617),  32 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
  },
  {
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
    { AOM_CDF12(23775, 24397, 25942, 26888, 27257, 27972, 28802, 29701, 30485, 31477, 32168),  56 },
    { AOM_CDF12( 2731,  5461,  8192, 10923, 13653, 16384, 19115, 21845, 24576, 27307, 30037),   0 },
  },
},
{
  {
    { AOM_CDF2( 5900),  32 },
    { AOM_CDF2(  984),  32 },
    { AOM_CDF2( 1539),  37 },
    { AOM_CDF2( 2809),  36 },
  },
  {
    { AOM_CDF2(  751),  77 },
    { AOM_CDF2(   19), 102 },
    { AOM_CDF2(   27), 120 },
    { AOM_CDF2(   20), 104 },
  },
  {
    { AOM_CDF2(23032),  60 },
    { AOM_CDF2(25224),  50 },
    { AOM_CDF2(30401),  50 },
    { AOM_CDF2(31447),  50 },
  },
},
{
  {
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
},
};

static const aom_cdf_prob default_inter_ext_tx_short_side_cdf[3][4][CDF_SIZE(4)] = {
  {
    { AOM_CDF4( 7821, 18687, 24236),   0 },
    { AOM_CDF4(14442, 26756, 32748),   0 },
    { AOM_CDF4(16946, 27485, 32748),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AOM_CDF4(20461, 26250, 29309),   0 },
    { AOM_CDF4(24931, 30589, 32748),   0 },
    { AOM_CDF4(28078, 31430, 32748),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AOM_CDF4( 7593, 15185, 16784),   0 },
    { AOM_CDF4(17164, 21845, 31208),   0 },
    { AOM_CDF4( 9362, 23406, 28087),   0 },
    { AOM_CDF4( 8192, 16384, 24576),   0 },
  },
};

static const aom_cdf_prob default_tx_ext_32_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(   67),   0 },
  { AOM_CDF2(  129),   0 },
};

static const aom_cdf_prob default_stx_cdf[2][5][CDF_SIZE(4)] = {
  {
    { AOM_CDF4(  293, 11683, 25053),  75 },
    { AOM_CDF4( 2952,  9945, 16750),   0 },
    { AOM_CDF4( 2684,  9484, 16065),  75 },
    { AOM_CDF4( 3552, 10398, 15130),  75 },
    { AOM_CDF4(10685, 14127, 17177),   1 },
  },
  {
    { AOM_CDF4(  293, 11683, 25053),   0 },
    { AOM_CDF4( 2952,  9945, 16750),   0 },
    { AOM_CDF4( 2684,  9484, 16065),   6 },
    { AOM_CDF4( 3552, 10398, 15130),   6 },
    { AOM_CDF4(10685, 14127, 17177),  31 },
  },
};

static const aom_cdf_prob default_cctx_type_cdf[CDF_SIZE(7)] = { AOM_CDF7(13038, 14157, 16570, 18922, 21570, 29304),  37 };

static const aom_cdf_prob default_pb_mv_most_probable_precision_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(27840),   0 },
  { AOM_CDF2(23276),   1 },
  { AOM_CDF2(14105),   0 },
};

static const aom_cdf_prob default_pb_mv_precision_cdf[2][3][CDF_SIZE(3)] = {
  {
    { AOM_CDF3(10923, 21845),   0 },
    { AOM_CDF3(30680, 31861),  78 },
    { AOM_CDF3(21154, 31023),   0 },
  },
  {
    { AOM_CDF3(10923, 21845),   0 },
    { AOM_CDF3(31613, 32191),  78 },
    { AOM_CDF3(25484, 32287),  75 },
  },
};

static const aom_cdf_prob default_do_split_cdf[2][64][CDF_SIZE(2)] = {
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

static const aom_cdf_prob default_do_square_split_cdf[2][8][CDF_SIZE(2)] = {
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

static const aom_cdf_prob default_rect_type_cdf[2][64][CDF_SIZE(2)] = {
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

static const aom_cdf_prob default_do_ext_partition_cdf[2][1][64][CDF_SIZE(2)] = {
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

static const aom_cdf_prob default_do_uneven_4way_partition_cdf[2][1][64][CDF_SIZE(2)] = {
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

static const aom_cdf_prob default_txfm_4way_partition_type_reduced_cdf[2][2][14][CDF_SIZE(7)] = {
  {
    {
      { AOM_CDF7(32414, 32473, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2876, 18313, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3923,  9350, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 6028, 14813, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3202, 14728, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 4109,  8558, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 6859, 14500, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3391, 11845, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3524, 10297, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 4258, 11816, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2794, 12996, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2464,  6875, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 4942, 22210, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 5470, 15586, 32532, 32591, 32650, 32709),   0 },
    },
    {
      { AOM_CDF7(13540, 21123, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 7415, 15547, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 4894, 11490, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 5041, 10187, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(14018, 23200, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(13864, 16376, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 8947, 12382, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(11792, 18010, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(14010, 21153, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 8570, 15398, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 5364, 12080, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3916, 11644, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(11052, 23760, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7(12450, 19865, 32532, 32591, 32650, 32709),   0 },
    },
  },
  {
    {
      { AOM_CDF7( 8192, 12288, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 1425, 11398, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2621, 11796, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 1489, 13405, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2185, 15292, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 3277, 16384, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 5461, 16384, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2185,  8738, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 4681, 18725, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
    },
    {
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 9362, 18724, 32532, 32591, 32650, 32709),   0 },
    },
  },
};

static const aom_cdf_prob default_txfm_2or3_way_partition_type_cdf[2][2][2][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(19001),   0 },
      { AOM_CDF2(19541),   0 },
    },
    {
      { AOM_CDF2(18801),   0 },
      { AOM_CDF2(18916),   0 },
    },
  },
  {
    {
      { AOM_CDF2(23406),   0 },
      { AOM_CDF2(28399),   0 },
    },
    {
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
    },
  },
};

static const aom_cdf_prob default_most_probable_stx_set_cdf[CDF_SIZE(7)] = { AOM_CDF7(16328, 21408, 25613, 27672, 29722, 31413),  75 };

static const aom_cdf_prob default_most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(4)] = { AOM_CDF4(16328, 21408, 25613),  75 };

static const aom_cdf_prob default_txfm_do_partition_cdf[2][2][9][CDF_SIZE(2)] = {
  {
    {
      { AOM_CDF2(16673),   0 },
      { AOM_CDF2(32057),   0 },
      { AOM_CDF2(21412),   0 },
      { AOM_CDF2(21101),   0 },
      { AOM_CDF2(15987),   0 },
      { AOM_CDF2(17681),   0 },
      { AOM_CDF2(15272),   0 },
      { AOM_CDF2(16944),   0 },
      { AOM_CDF2(15490),   0 },
    },
    {
      { AOM_CDF2(24996),   0 },
      { AOM_CDF2(29488),   0 },
      { AOM_CDF2(28130),   0 },
      { AOM_CDF2(25285),   0 },
      { AOM_CDF2(26292),   0 },
      { AOM_CDF2(26699),   0 },
      { AOM_CDF2(26963),   0 },
      { AOM_CDF2(28550),   0 },
      { AOM_CDF2(26414),   0 },
    },
  },
  {
    {
      { AOM_CDF2(26810),   0 },
      { AOM_CDF2(32361),   0 },
      { AOM_CDF2(28087),   0 },
      { AOM_CDF2(25777),   0 },
      { AOM_CDF2(24932),   0 },
      { AOM_CDF2(26103),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(16384),   0 },
      { AOM_CDF2(25753),   0 },
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
    },
  },
};

static const aom_cdf_prob default_txfm_4way_partition_type_cdf[2][2][14][CDF_SIZE(7)] = {
  {
    {
      { AOM_CDF7(32414, 32473, 32532, 32591, 32650, 32709),   0 },
      { AOM_CDF7( 2876, 18313, 22208, 28264, 28268, 32724),   0 },
      { AOM_CDF7( 3923,  9350, 23275, 23279, 27495, 27499),   0 },
      { AOM_CDF7( 6028, 14813, 20346, 23777, 26326, 29951),   0 },
      { AOM_CDF7( 3202, 14728, 18086, 23666, 25309, 30384),   0 },
      { AOM_CDF7( 4109,  8558, 19171, 20786, 24636, 27471),   0 },
      { AOM_CDF7( 6859, 14500, 20365, 23744, 26424, 29870),   0 },
      { AOM_CDF7( 3391, 11845, 16481, 21478, 25770, 30485),   0 },
      { AOM_CDF7( 3524, 10297, 17893, 21778, 25140, 28189),   0 },
      { AOM_CDF7( 4258, 11816, 17368, 23181, 27551, 30381),   0 },
      { AOM_CDF7( 2794, 12996, 16651, 28785, 28789, 32724),   0 },
      { AOM_CDF7( 2464,  6875, 17359, 17363, 27958, 27962),   0 },
      { AOM_CDF7( 2471, 11105, 15103, 24759, 27894, 31175),   0 },
      { AOM_CDF7( 2735,  7793, 16212, 19301, 27502, 29337),   0 },
    },
    {
      { AOM_CDF7(13540, 21123, 31685, 31956, 32226, 32497),   0 },
      { AOM_CDF7( 7415, 15547, 20331, 27028, 27267, 32529),   0 },
      { AOM_CDF7( 4894, 11490, 20427, 20640, 27023, 27236),   0 },
      { AOM_CDF7( 5041, 10187, 18064, 22265, 25941, 29092),   0 },
      { AOM_CDF7( 7009, 11600, 13266, 19645, 22915, 29904),   0 },
      { AOM_CDF7( 6932,  8188, 12803, 16517, 24421, 26826),   0 },
      { AOM_CDF7( 8947, 12382, 16998, 19382, 22593, 27492),   0 },
      { AOM_CDF7( 5896,  9005, 11167, 15812, 20029, 29177),   0 },
      { AOM_CDF7( 4670,  7051,  9974, 13755, 20083, 23804),   0 },
      { AOM_CDF7( 4285,  7699, 11562, 17252, 23015, 27159),   0 },
      { AOM_CDF7( 5364, 12080, 18435, 28666, 28711, 32723),   0 },
      { AOM_CDF7( 3916, 11644, 16796, 16831, 29161, 29196),   0 },
      { AOM_CDF7( 2763,  5940,  6963, 17047, 23789, 30088),   0 },
      { AOM_CDF7( 2490,  3973,  6413, 14837, 24746, 27462),   0 },
    },
  },
  {
    {
      { AOM_CDF7( 8192, 12288, 16384, 20480, 24576, 28672),   0 },
      { AOM_CDF7( 1425, 11398, 19946, 24220, 25645, 31343),   0 },
      { AOM_CDF7( 2621, 11796, 27525, 28836, 30147, 31457),   0 },
      { AOM_CDF7( 1489, 13405, 23831, 25321, 29789, 31279),   0 },
      { AOM_CDF7( 2185, 15292, 19661, 26214, 28399, 30583),   0 },
      { AOM_CDF7( 3277, 16384, 22938, 26214, 29491, 31130),   0 },
      { AOM_CDF7( 5461, 16384, 21845, 25486, 27307, 29127),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 2185,  8738, 21845, 24030, 26214, 30583),   0 },
      { AOM_CDF7( 4681, 18725, 26526, 28087, 29647, 31208),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
    {
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AOM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
  },
};

static const aom_cdf_prob default_filter_dir_cdf[4][CDF_SIZE(3)] = {
  { AOM_CDF3(16384, 24576),   1 },
  { AOM_CDF3(16384, 24576),   1 },
  { AOM_CDF3(16384, 24576),   1 },
  { AOM_CDF3(16384, 24576),  32 },
};

static const aom_cdf_prob default_cfl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(18484),   6 },
  { AOM_CDF2( 8591),   6 },
  { AOM_CDF2( 2151),   0 },
};

static const aom_cdf_prob default_region_type_cdf[4][CDF_SIZE(2)] = {
  { AOM_CDF2( 8192),   0 },
  { AOM_CDF2( 8192),   0 },
  { AOM_CDF2( 8192),   0 },
  { AOM_CDF2( 8192),   0 },
};

static const aom_cdf_prob default_morph_pred_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(19186),  50 },
  { AOM_CDF2(16483),   1 },
  { AOM_CDF2( 8242),  95 },
};

static const aom_cdf_prob default_seg_tree_cdf[CDF_SIZE(8)] = { AOM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 };

static const aom_cdf_prob default_seg_tree_cdf1[CDF_SIZE(8)] = { AOM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 };

static const aom_cdf_prob default_segment_pred_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

static const aom_cdf_prob default_spatial_pred_seg_tree_cdf[3][CDF_SIZE(8)] = {
  { AOM_CDF8( 5622,  7893, 16093, 18233, 27809, 28373, 32533),   0 },
  { AOM_CDF8(14274, 18230, 22557, 24935, 29980, 30851, 32344),   0 },
  { AOM_CDF8(27527, 28487, 28723, 28890, 32397, 32647, 32679),   0 },
};

static const aom_cdf_prob default_spatial_pred_seg_tree_cdf1[3][CDF_SIZE(8)] = {
  { AOM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
  { AOM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
  { AOM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
};

static const aom_cdf_prob default_seg_id_ext_flag_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
  { AOM_CDF2(16384),   0 },
};

/* clang-format on */
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AOM_AV1_COMMON_ENTROPY_INITS_MODES_H_
