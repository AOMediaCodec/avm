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

#ifndef AVM_AV2_COMMON_ENTROPY_INITS_MODES_H_
#define AVM_AV2_COMMON_ENTROPY_INITS_MODES_H_

#include "config/avm_config.h"

#ifdef __cplusplus
extern "C" {
#endif
/* clang-format off */

static const avm_cdf_prob default_inter_single_mode_cdf[5][CDF_SIZE(3)] = {
  { AVM_CDF3(10043, 11100),   6 },
  { AVM_CDF3(21561, 21758),   1 },
  { AVM_CDF3(25411, 25714),   0 },
  { AVM_CDF3(14117, 14341),   0 },
  { AVM_CDF3(18288, 18577),   0 },
};

static const avm_cdf_prob default_inter_warp_mode_cdf[5][CDF_SIZE(2)] = {
  { AVM_CDF2(25999),   1 },
  { AVM_CDF2(14478),   7 },
  { AVM_CDF2(10868),   6 },
  { AVM_CDF2( 5256),  31 },
  { AVM_CDF2( 2722),  31 },
};

static const avm_cdf_prob default_is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)] = { AVM_CDF2(15095),   1 };

static const avm_cdf_prob default_refinemv_flag_cdf[24][CDF_SIZE(2)] = {
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(20204),  25 },
  { AVM_CDF2(17614),  43 },
  { AVM_CDF2(24443),  32 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
};

static const avm_cdf_prob default_drl_cdf[3][5][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(15721),  90 },
    { AVM_CDF2(21115),   0 },
    { AVM_CDF2(19567),   1 },
    { AVM_CDF2(17602),  93 },
    { AVM_CDF2(13319),  93 },
  },
  {
    { AVM_CDF2(18692),  93 },
    { AVM_CDF2(19343),  90 },
    { AVM_CDF2(18207),  90 },
    { AVM_CDF2(17908),  93 },
    { AVM_CDF2(18304),  93 },
  },
  {
    { AVM_CDF2(22157),  90 },
    { AVM_CDF2(23233),  90 },
    { AVM_CDF2(22782),  90 },
    { AVM_CDF2(22353),  93 },
    { AVM_CDF2(22457),  93 },
  },
};

static const avm_cdf_prob default_tip_drl_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(30662),   0 },
  { AVM_CDF2(23823),   6 },
  { AVM_CDF2(21676),   6 },
};

static const avm_cdf_prob default_skip_drl_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(21634),   0 },
  { AVM_CDF2(17376),   0 },
  { AVM_CDF2(18432),  75 },
};

static const avm_cdf_prob default_use_optflow_cdf[2][CDF_SIZE(2)] = {
  { AVM_CDF2(11582),  31 },
  { AVM_CDF2(24076),  26 },
};

static const avm_cdf_prob default_inter_compound_mode_is_joint_cdf[2][CDF_SIZE(2)] = {
  { AVM_CDF2(24720),   0 },
  { AVM_CDF2(32764),   0 },
};

static const avm_cdf_prob default_inter_compound_mode_non_joint_type_cdf[5][CDF_SIZE(5)] = {
  { AVM_CDF5(12177, 20001, 23193, 24448),  26 },
  { AVM_CDF5(21192, 25117, 27806, 27948),  31 },
  { AVM_CDF5(26779, 28724, 30192, 30249),  25 },
  { AVM_CDF5(12506, 17871, 21295, 21389),  31 },
  { AVM_CDF5(16948, 20335, 22582, 22617),  31 },
};

static const avm_cdf_prob default_inter_compound_mode_same_refs_cdf[5][CDF_SIZE(4)] = {
  { AVM_CDF4( 6386, 23344, 23348),  31 },
  { AVM_CDF4(10945, 24709, 24714),  31 },
  { AVM_CDF4(11517, 25230, 25258),  31 },
  { AVM_CDF4( 7563, 22176, 22180),  31 },
  { AVM_CDF4( 6629, 20955, 20966),   6 },
};

static const avm_cdf_prob default_cwp_idx_cdf[2][4][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(21704),  56 },
    { AVM_CDF2(15990),  31 },
    { AVM_CDF2(12544),  57 },
    { AVM_CDF2(25638),  62 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_jmvd_scale_mode_cdf[CDF_SIZE(5)] = { AVM_CDF5(23180, 24894, 26548, 29872),   1 };

static const avm_cdf_prob default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(3)] = { AVM_CDF3(23178, 26812),   0 };

static const avm_cdf_prob default_compound_type_cdf[CDF_SIZE(2)] = { AVM_CDF2(16894),  31 };

static const avm_cdf_prob default_amvd_mode_cdf[9][3][CDF_SIZE(2)] = {
  {
    { AVM_CDF2( 5980),  15 },
    { AVM_CDF2( 6091),  15 },
    { AVM_CDF2( 6237),  93 },
  },
  {
    { AVM_CDF2(  861),  30 },
    { AVM_CDF2(  847),  50 },
    { AVM_CDF2( 1198),  66 },
  },
  {
    { AVM_CDF2(  456),  53 },
    { AVM_CDF2(  431),  68 },
    { AVM_CDF2(  849),  68 },
  },
  {
    { AVM_CDF2(  409),  68 },
    { AVM_CDF2(  385),  68 },
    { AVM_CDF2(  581),  68 },
  },
  {
    { AVM_CDF2(16246),   0 },
    { AVM_CDF2( 9696),   0 },
    { AVM_CDF2( 8791),   0 },
  },
  {
    { AVM_CDF2(13199),   0 },
    { AVM_CDF2(10624),   0 },
    { AVM_CDF2( 8586),   1 },
  },
  {
    { AVM_CDF2( 5112),   1 },
    { AVM_CDF2( 3920),   3 },
    { AVM_CDF2( 3668),   3 },
  },
  {
    { AVM_CDF2(12017),  76 },
    { AVM_CDF2(10177),  75 },
    { AVM_CDF2( 9184),  75 },
  },
  {
    { AVM_CDF2(12111),   6 },
    { AVM_CDF2( 8056),   5 },
    { AVM_CDF2( 6641),  27 },
  },
};

static const avm_cdf_prob default_wedge_quad_cdf[CDF_SIZE(4)] = { AVM_CDF4( 6511, 18144, 27374),   1 };

static const avm_cdf_prob default_wedge_angle_cdf[4][CDF_SIZE(5)] = {
  { AVM_CDF5(10258, 15276, 19997, 26561),   6 },
  { AVM_CDF5(14039, 19183, 26143, 30047),   6 },
  { AVM_CDF5(19564, 22099, 25104, 29960),   1 },
  { AVM_CDF5(13808, 17950, 25715, 29008),   7 },
};

static const avm_cdf_prob default_wedge_dist_cdf[CDF_SIZE(4)] = { AVM_CDF4( 8203, 16994, 21032),  75 };

static const avm_cdf_prob default_wedge_dist_cdf2[CDF_SIZE(3)] = { AVM_CDF3(14463, 19115),  75 };

static const avm_cdf_prob default_warp_interintra_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(27980),  56 },
  { AVM_CDF2(29163),  56 },
  { AVM_CDF2(30658),  62 },
};

static const avm_cdf_prob default_interintra_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2(20569),   1 },
  { AVM_CDF2(17106),   1 },
  { AVM_CDF2(20948),   1 },
  { AVM_CDF2(25796),  32 },
};

static const avm_cdf_prob default_wedge_interintra_cdf[CDF_SIZE(2)] = { AVM_CDF2(16758),   0 };

static const avm_cdf_prob default_interintra_mode_cdf[4][CDF_SIZE(4)] = {
  { AVM_CDF4( 1819, 16131, 26802),  32 },
  { AVM_CDF4( 1442, 15840, 28441),   1 },
  { AVM_CDF4( 1995, 15814, 28221),   7 },
  { AVM_CDF4( 3564, 15440, 28048),  32 },
};

static const avm_cdf_prob default_warp_ref_idx0_cdf[1][CDF_SIZE(2)] = {
  { AVM_CDF2(18903),   0 },
};

static const avm_cdf_prob default_warp_ref_idx1_cdf[1][CDF_SIZE(2)] = {
  { AVM_CDF2(24500),  90 },
};

static const avm_cdf_prob default_warp_ref_idx2_cdf[1][CDF_SIZE(2)] = {
  { AVM_CDF2(25360),  75 },
};

static const avm_cdf_prob default_warp_causal_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2(14877),   1 },
  { AVM_CDF2(12801),   0 },
  { AVM_CDF2( 6885),   6 },
  { AVM_CDF2( 2987),  30 },
};

static const avm_cdf_prob default_warp_precision_idx_cdf[31][CDF_SIZE(2)] = {
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(31107),  56 },
  { AVM_CDF2(27357),  31 },
  { AVM_CDF2(26435),  26 },
  { AVM_CDF2(22585),  26 },
  { AVM_CDF2(20146),  27 },
  { AVM_CDF2(18671),  32 },
  { AVM_CDF2(18210),  32 },
  { AVM_CDF2(17968),  25 },
  { AVM_CDF2(17451),  40 },
  { AVM_CDF2(16736),  25 },
  { AVM_CDF2(16040),  25 },
  { AVM_CDF2(15633),  25 },
  { AVM_CDF2(13952),  25 },
  { AVM_CDF2(14893),  25 },
  { AVM_CDF2(13323),  25 },
  { AVM_CDF2(11830),  50 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(21783),  28 },
  { AVM_CDF2(20345),  15 },
  { AVM_CDF2(19398),  25 },
  { AVM_CDF2(17823),  35 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(20066),  25 },
  { AVM_CDF2(18893),   5 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
};

static const avm_cdf_prob default_warp_delta_param_cdf[2][CDF_SIZE(8)] = {
  { AVM_CDF8( 8995, 22970, 25406, 29673, 30295, 31670, 31863),   0 },
  { AVM_CDF8(13333, 24012, 26545, 30183, 30839, 31958, 32139),   0 },
};

static const avm_cdf_prob default_warp_delta_param_high_cdf[2][CDF_SIZE(8)] = {
  { AVM_CDF8( 8959, 14388, 19825, 21810, 25035, 28077, 29469),  15 },
  { AVM_CDF8( 9199, 14146, 19484, 21591, 24614, 28015, 29538),   0 },
};

static const avm_cdf_prob default_warp_param_sign_cdf[CDF_SIZE(2)] = { AVM_CDF2(14285),  93 };

static const avm_cdf_prob default_warp_extend_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(20359),   0 },
  { AVM_CDF2(20310),  75 },
  { AVM_CDF2(21759),  75 },
};

static const avm_cdf_prob default_warpmv_with_mvd_flag_cdf[CDF_SIZE(2)] = { AVM_CDF2(18452),   1 };

static const avm_cdf_prob default_bawp_cdf[2][CDF_SIZE(2)] = {
  { AVM_CDF2(26456),  31 },
  { AVM_CDF2( 5121),  31 },
};

static const avm_cdf_prob default_explicit_bawp_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(26966),  31 },
  { AVM_CDF2(15275),   6 },
  { AVM_CDF2(14613),  31 },
};

static const avm_cdf_prob default_explicit_bawp_scale_cdf[CDF_SIZE(2)] = { AVM_CDF2(21998),  32 };

static const avm_cdf_prob default_tip_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(30898),  93 },
  { AVM_CDF2(19665),   0 },
  { AVM_CDF2( 9477),  15 },
};

static const avm_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(2)] = { AVM_CDF2(22129),  31 };

static const avm_cdf_prob default_palette_y_size_cdf[CDF_SIZE(7)] = { AVM_CDF7( 8779, 15095, 20777, 24903, 27923, 30403),  32 };

static const avm_cdf_prob default_palette_uv_size_cdf[CDF_SIZE(7)] = { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 };

static const avm_cdf_prob default_identity_row_cdf_y[4][CDF_SIZE(3)] = {
  { AVM_CDF3(22515, 25751),  25 },
  { AVM_CDF3( 4014,  5233),  31 },
  { AVM_CDF3( 3548,  4163),  33 },
  { AVM_CDF3(12999, 32756),  56 },
};

static const avm_cdf_prob default_identity_row_cdf_uv[4][CDF_SIZE(3)] = {
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
};

static const avm_cdf_prob default_palette_y_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
{
  { AVM_CDF2(28140),  90 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2( 8582),   6 },
  { AVM_CDF2(27413),  32 },
  { AVM_CDF2(30429),  93 },
},
{
  { AVM_CDF3(25350, 29026),  90 },
  { AVM_CDF3(11363, 25273),   7 },
  { AVM_CDF3( 6841, 28579),   1 },
  { AVM_CDF3(21350, 26012),   6 },
  { AVM_CDF3(30573, 31646),  93 },
},
{
  { AVM_CDF4(23706, 26962, 29060),   0 },
  { AVM_CDF4( 9976, 22516, 27382),   1 },
  { AVM_CDF4( 6691, 25460, 29234),   6 },
  { AVM_CDF4(18909, 23925, 28403),  31 },
  { AVM_CDF4(30308, 31076, 31818),  93 },
},
{
  { AVM_CDF5(24116, 26957, 28486, 29941),   0 },
  { AVM_CDF5( 9568, 20472, 24294, 28942),  81 },
  { AVM_CDF5( 5706, 25243, 28040, 30406),  76 },
  { AVM_CDF5(20105, 22982, 27024, 28911),  31 },
  { AVM_CDF5(30897, 31342, 31766, 32199),  93 },
},
{
  { AVM_CDF6(20824, 24227, 25926, 27459, 29266),  75 },
  { AVM_CDF6( 8141, 18989, 21599, 26182, 28576),  75 },
  { AVM_CDF6( 5252, 24340, 26450, 28438, 30625),  75 },
  { AVM_CDF6(19519, 22695, 25587, 26972, 28423),   6 },
  { AVM_CDF6(30383, 30890, 31247, 31653, 32150),  78 },
},
{
  { AVM_CDF7(21628, 24512, 25873, 27054, 28131, 29539),  80 },
  { AVM_CDF7( 8028, 18264, 20613, 25424, 27112, 28906),  90 },
  { AVM_CDF7( 6489, 22242, 24461, 26394, 28350, 30510),  75 },
  { AVM_CDF7(22048, 24429, 26990, 27944, 28417, 29574),  76 },
  { AVM_CDF7(30801, 31205, 31472, 31728, 32005, 32305),  93 },
},
{
  { AVM_CDF8(22471, 25083, 25984, 26893, 27654, 28750, 29903),  93 },
  { AVM_CDF8( 7542, 17057, 19151, 23550, 25459, 27066, 28804),  90 },
  { AVM_CDF8( 7582, 20437, 22728, 24622, 26515, 28579, 30632),  90 },
  { AVM_CDF8(22102, 24144, 26916, 28151, 28846, 29212, 30153),   0 },
  { AVM_CDF8(30524, 30887, 31156, 31393, 31626, 31911, 32281),  93 },
},
};

static const avm_cdf_prob default_palette_y_mode_cdf[CDF_SIZE(2)] = { AVM_CDF2(30045),  62 };

static const avm_cdf_prob default_palette_uv_mode_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_comp_inter_cdf[5][CDF_SIZE(2)] = {
  { AVM_CDF2(26924),  15 },
  { AVM_CDF2(25000),   1 },
  { AVM_CDF2(17949),   1 },
  { AVM_CDF2(13581),   6 },
  { AVM_CDF2( 7034),   0 },
};

static const avm_cdf_prob default_single_ref_cdf[3][6][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(26469),   0 },
    { AVM_CDF2(28870),  30 },
    { AVM_CDF2(29662),   1 },
    { AVM_CDF2(29867),   6 },
    { AVM_CDF2(29772),   6 },
    { AVM_CDF2(29776),  26 },
  },
  {
    { AVM_CDF2(13631),   6 },
    { AVM_CDF2(18185),  37 },
    { AVM_CDF2(19992),  32 },
    { AVM_CDF2(18462),  62 },
    { AVM_CDF2(17451),  37 },
    { AVM_CDF2(11578),  62 },
  },
  {
    { AVM_CDF2( 2599),   0 },
    { AVM_CDF2( 5203),  31 },
    { AVM_CDF2( 5185),  31 },
    { AVM_CDF2( 3671),  31 },
    { AVM_CDF2( 3954),   6 },
    { AVM_CDF2( 1633),   5 },
  },
};

static const avm_cdf_prob default_comp_ref0_cdf[3][6][CDF_SIZE(2)] = {
  {
    { AVM_CDF2( 9272),  32 },
    { AVM_CDF2(17175),  62 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2( 1385),   0 },
    { AVM_CDF2( 4439),  31 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(  521),   0 },
    { AVM_CDF2( 1854),  25 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_comp_ref1_cdf[3][2][6][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(30729),  75 },
      { AVM_CDF2(29403),   5 },
      { AVM_CDF2(29037),   6 },
      { AVM_CDF2(29355),  31 },
      { AVM_CDF2(28573),   5 },
      { AVM_CDF2(27396),   7 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(19315),  31 },
      { AVM_CDF2(27821),   6 },
      { AVM_CDF2(27892),  31 },
      { AVM_CDF2(28695),  30 },
      { AVM_CDF2(29637),  51 },
    },
  },
  {
    {
      { AVM_CDF2(30432),   0 },
      { AVM_CDF2(20290),  31 },
      { AVM_CDF2(19855),  37 },
      { AVM_CDF2(18567),  62 },
      { AVM_CDF2(18331),  37 },
      { AVM_CDF2(14241),  62 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2( 5725),  31 },
      { AVM_CDF2(13420),  31 },
      { AVM_CDF2(12780),  32 },
      { AVM_CDF2(10781),  62 },
      { AVM_CDF2( 6424),  62 },
    },
  },
  {
    {
      { AVM_CDF2(11634),  31 },
      { AVM_CDF2(10093),  31 },
      { AVM_CDF2( 6065),  31 },
      { AVM_CDF2( 5408),  31 },
      { AVM_CDF2( 6411),  31 },
      { AVM_CDF2( 4075),  30 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(  898),  15 },
      { AVM_CDF2( 3127),   1 },
      { AVM_CDF2( 1775),   5 },
      { AVM_CDF2( 1217),   6 },
      { AVM_CDF2(  591),   5 },
    },
  },
};

static const avm_cdf_prob default_comp_group_idx_cdfs[12][CDF_SIZE(2)] = {
  { AVM_CDF2(17510),  26 },
  { AVM_CDF2(10382),   0 },
  { AVM_CDF2( 8336),  31 },
  { AVM_CDF2( 6054),   1 },
  { AVM_CDF2( 5764),   5 },
  { AVM_CDF2( 7154),  10 },
  { AVM_CDF2(15013),  26 },
  { AVM_CDF2( 8426),   0 },
  { AVM_CDF2( 8278),   1 },
  { AVM_CDF2( 4856),   1 },
  { AVM_CDF2( 3464),   0 },
  { AVM_CDF2( 5295),   0 },
};

static const avm_cdf_prob default_skip_mode_cdfs[3][CDF_SIZE(2)] = {
  { AVM_CDF2(30964),  93 },
  { AVM_CDF2(21769),   0 },
  { AVM_CDF2(12484),  15 },
};

static const avm_cdf_prob default_lossless_tx_size_cdf[4][2][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(16384),   1 },
    { AVM_CDF2(16384),   1 },
  },
  {
    { AVM_CDF2(16384),  75 },
    { AVM_CDF2(16384),  75 },
  },
  {
    { AVM_CDF2(16384),  75 },
    { AVM_CDF2(16384),  75 },
  },
  {
    { AVM_CDF2(16384),  75 },
    { AVM_CDF2(16384),  75 },
  },
};

static const avm_cdf_prob default_lossless_inter_tx_type_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_skip_txfm_cdfs[6][CDF_SIZE(2)] = {
  { AVM_CDF2(25865),  25 },
  { AVM_CDF2(14316),   0 },
  { AVM_CDF2( 4598),   0 },
  { AVM_CDF2(25612),   6 },
  { AVM_CDF2(12366),   1 },
  { AVM_CDF2( 3320),  90 },
};

static const avm_cdf_prob default_bru_mode_cdf[CDF_SIZE(3)] = { AVM_CDF3( 4124, 16615),   0 };

static const avm_cdf_prob default_intra_inter_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2( 1522),   1 },
  { AVM_CDF2(14381),   0 },
  { AVM_CDF2(10455),  25 },
  { AVM_CDF2(27796),   0 },
};

static const avm_cdf_prob default_intrabc_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(32085),   5 },
  { AVM_CDF2(15172),  30 },
  { AVM_CDF2( 4503),   0 },
};

static const avm_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = { AVM_CDF2(29993),   6 };

static const avm_cdf_prob default_intrabc_bv_precision_cdf[1][CDF_SIZE(2)] = {
  { AVM_CDF2(19778),  31 },
};

static const avm_cdf_prob default_switchable_flex_restore_cdf[3][3][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(25542),  62 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(25550),  37 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = { AVM_CDF2( 9051),  32 };

static const avm_cdf_prob default_wienerns_length_cdf[2][CDF_SIZE(2)] = {
  { AVM_CDF2( 4898),  56 },
  { AVM_CDF2( 6088),  26 },
};

static const avm_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = { AVM_CDF2(29286),  65 };

static const avm_cdf_prob default_wienerns_4part_cdf[1][CDF_SIZE(4)] = {
  { AVM_CDF4(16557, 24352, 29677),   6 },
};

static const avm_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = { AVM_CDF2(12799),  25 };

static const avm_cdf_prob default_ccso_cdf[3][4][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(18469),  62 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2( 4949),  37 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(23470),  37 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2( 6666),  37 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(22914),  37 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2( 6993),  37 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_cdef_strength_index0_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2(29034),  32 },
  { AVM_CDF2(16472),  32 },
  { AVM_CDF2( 5751),  32 },
  { AVM_CDF2( 3115),  31 },
};

static const avm_cdf_prob default_cdef_cdf[CDEF_STRENGTHS_NUM - 1][CDF_SIZE(CDEF_STRENGTHS_NUM)] = {
{ AVM_CDF2(17708),  32 },
{ AVM_CDF3(13413, 24899),  32 },
{ AVM_CDF4(10588, 19866, 26664),  37 },
{ AVM_CDF5(10131, 17874, 23876, 28766),  37 },
{ AVM_CDF6( 8363, 15451, 20811, 25453, 29393),  32 },
{ AVM_CDF7( 7372, 13867, 18969, 23278, 26977, 30156),  32 },
};

static const avm_cdf_prob default_gdf_cdf[CDF_SIZE(2)] = { AVM_CDF2(14593),  32 };

static const avm_cdf_prob default_mrl_index_cdf[3][CDF_SIZE(4)] = {
  { AVM_CDF4(29573, 31193, 32023),  78 },
  { AVM_CDF4(21812, 27066, 30279),  75 },
  { AVM_CDF4(16076, 23806, 28762),   1 },
};

static const avm_cdf_prob default_multi_line_mrl_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(19678),   6 },
  { AVM_CDF2(12287),   6 },
  { AVM_CDF2( 9574),   6 },
};

static const avm_cdf_prob default_dpcm_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_dpcm_vert_horz_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_dpcm_uv_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_dpcm_uv_vert_horz_cdf[CDF_SIZE(2)] = { AVM_CDF2(16384),   0 };

static const avm_cdf_prob default_fsc_mode_cdf[4][6][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(30503),   3 },
    { AVM_CDF2(31244),   3 },
    { AVM_CDF2(32254),  78 },
    { AVM_CDF2(32324),  93 },
    { AVM_CDF2(32582),  93 },
    { AVM_CDF2(32691),  93 },
  },
  {
    { AVM_CDF2(27437),   0 },
    { AVM_CDF2(27242),  90 },
    { AVM_CDF2(28040),  76 },
    { AVM_CDF2(27589),  76 },
    { AVM_CDF2(27234),   7 },
    { AVM_CDF2(23583),  62 },
  },
  {
    { AVM_CDF2(26068),  75 },
    { AVM_CDF2(22635),  75 },
    { AVM_CDF2(22069),   6 },
    { AVM_CDF2(19218),  32 },
    { AVM_CDF2(13701),  31 },
    { AVM_CDF2( 4636),  38 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(32016),  15 },
    { AVM_CDF2(32403),  93 },
    { AVM_CDF2(32583),  15 },
    { AVM_CDF2(32683),  76 },
  },
};

static const avm_cdf_prob default_cfl_index_cdf[CDF_SIZE(2)] = { AVM_CDF2(12507),  30 };

static const avm_cdf_prob default_cfl_mhccp_switch_cdf[CDF_SIZE(2)] = { AVM_CDF2(15499),  30 };

static const avm_cdf_prob default_cfl_sign_cdf[CDF_SIZE(8)] = { AVM_CDF8( 2421,  4332, 11256, 12766, 21386, 28725, 32087),  62 };

static const avm_cdf_prob default_cfl_alpha_cdf[6][CDF_SIZE(8)] = {
  { AVM_CDF8(21679, 25305, 30646, 31512, 32537, 32646, 32696),  62 },
  { AVM_CDF8( 8262, 16302, 24082, 29422, 31398, 32286, 32525),  62 },
  { AVM_CDF8(17235, 26166, 30378, 31305, 32373, 32549, 32668),  62 },
  { AVM_CDF8(17618, 25732, 27865, 30338, 31125, 31522, 32238),  62 },
  { AVM_CDF8(17542, 23066, 27907, 28728, 30702, 31165, 31435),  62 },
  { AVM_CDF8(17675, 24802, 30468, 30783, 31841, 32264, 32422),  62 },
};

static const avm_cdf_prob default_y_mode_set_cdf[CDF_SIZE(4)] = { AVM_CDF4(28863, 31022, 31724),  93 };

static const avm_cdf_prob default_y_mode_idx_cdf[3][CDF_SIZE(8)] = {
  { AVM_CDF8(15175, 20075, 21728, 24098, 26405, 27655, 28860),   5 },
  { AVM_CDF8(10114, 14957, 16815, 19127, 20147, 25583, 27169),   0 },
  { AVM_CDF8( 5636,  9004, 10456, 12122, 12744, 20325, 25607),   0 },
};

static const avm_cdf_prob default_y_mode_idx_offset_cdf[3][CDF_SIZE(6)] = {
  { AVM_CDF6(12743, 18172, 20194, 23648, 26419),   6 },
  { AVM_CDF6( 8976, 16084, 20827, 24595, 28496),  75 },
  { AVM_CDF6( 8784, 14556, 19710, 24903, 28724),  75 },
};

static const avm_cdf_prob default_uv_mode_cdf[2][CDF_SIZE(8)] = {
  { AVM_CDF8( 9363, 20957, 22865, 24753, 26411, 27983, 30428),  31 },
  { AVM_CDF8(21282, 23610, 28208, 29311, 30348, 31158, 31491),  30 },
};

static const avm_cdf_prob default_switchable_interp_cdf[16][CDF_SIZE(3)] = {
  { AVM_CDF3(29975, 32748),  25 },
  { AVM_CDF3( 2076, 32703),  75 },
  { AVM_CDF3(   19,  1768),  15 },
  { AVM_CDF3(17314, 27415),  31 },
  { AVM_CDF3(31286, 31994),   0 },
  { AVM_CDF3( 9581, 32608),  31 },
  { AVM_CDF3(  535,  1036),   0 },
  { AVM_CDF3(24819, 27722),  31 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3(10923, 21845),   0 },
};

static const avm_cdf_prob default_delta_q_cdf[CDF_SIZE(8)] = { AVM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613),  56 };

static const avm_cdf_prob default_intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)] = {
  {
    { 0 },
    { 0 },
    { 0 },
    { 0 },
  },
{
  { AVM_CDF7( 5026, 16816, 19974, 23404, 26845, 30499),   8 },
  { AVM_CDF7(14910, 25257, 26964, 29323, 30237, 31535),   0 },
  { AVM_CDF7(13759, 26108, 27688, 29793, 30265, 31576),  35 },
  { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
},
  {
    { AVM_CDF2(16384), 0 },
    { AVM_CDF2(16384), 0 },
    { AVM_CDF2(16384), 0 },
    { AVM_CDF2(16384), 0 },
  },
};

static const avm_cdf_prob default_intra_ext_tx_short_side_cdf[4][CDF_SIZE(4)] = {
  { AVM_CDF4(10692, 26586, 29231),  31 },
  { AVM_CDF4(26700, 32160, 32748),   6 },
  { AVM_CDF4(26915, 32411, 32748),   6 },
  { AVM_CDF4( 8192, 16384, 24576),   0 },
};

static const avm_cdf_prob default_inter_tx_type_set_cdf[2][3][4][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(14122),  25 },
      { AVM_CDF2( 8962),  31 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
    },
    {
      { AVM_CDF2(16423),  26 },
      { AVM_CDF2(23446),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
    },
    {
      { AVM_CDF2(23352),  25 },
      { AVM_CDF2(17069),  56 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
    },
  },
  {
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(20835),   0 },
      { AVM_CDF2(16384),   0 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(30720),   3 },
      { AVM_CDF2(16384),   0 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(28857),  17 },
      { AVM_CDF2(16384),   0 },
    },
  },
};

static const avm_cdf_prob default_inter_tx_type_idx_cdf[2][3][CDF_SIZE(8)] = {
  {
    { AVM_CDF8( 8914, 10732, 12270, 14822, 17128, 19586, 21964),  25 },
    { AVM_CDF8( 1160,  1555,  1972,  3414,  3962,  5151,  5908),  31 },
    { AVM_CDF8(22819, 24338, 25592, 27001, 28395, 29648, 30990),  25 },
  },
  {
    { AVM_CDF8( 1140,  1725,  2324, 14653, 19072, 23618, 28109),  31 },
    { AVM_CDF8(   58,   261,   587, 32317, 32556, 32626, 32708),   2 },
    { AVM_CDF8(17404, 17669, 18403, 24052, 26393, 28506, 30676),  57 },
  },
};

static const avm_cdf_prob default_inter_tx_type_offset_1_cdf[3][CDF_SIZE(8)] = {
  { AVM_CDF8( 3121,  6470,  9191, 12280, 17811, 22588, 27697),  93 },
  { AVM_CDF8(  338,   377,   571,   743,  7932, 11860, 17524),   1 },
  { AVM_CDF8( 3314,  7625, 10639, 14206, 19363, 23456, 28033),  93 },
};

static const avm_cdf_prob default_inter_tx_type_offset_2_cdf[3][CDF_SIZE(4)] = {
  { AVM_CDF4( 8669, 16533, 24855),  93 },
  { AVM_CDF4( 9441, 16413, 25276),  90 },
  { AVM_CDF4( 8767, 17611, 24876),  10 },
};


static const avm_cdf_prob default_inter_ext_tx_cdf[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)] = {
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
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
},
{
  {
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
},
};

static const avm_cdf_prob default_inter_ext_tx_short_side_cdf[3][4][CDF_SIZE(4)] = {
  {
    { AVM_CDF4( 8347, 20254, 24536),  31 },
    { AVM_CDF4(15994, 26294, 32748),   1 },
    { AVM_CDF4(21212, 27810, 32748),   1 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AVM_CDF4(21815, 25084, 26230),  62 },
    { AVM_CDF4(29354, 31747, 32748),  37 },
    { AVM_CDF4(31614, 32529, 32748),  36 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
  {
    { AVM_CDF4(10501, 20590, 24181),  31 },
    { AVM_CDF4(17596, 26388, 32748),  37 },
    { AVM_CDF4(15407, 26475, 32732),  60 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
  },
};

static const avm_cdf_prob default_tx_ext_32_cdf[2][CDF_SIZE(2)] = {
  { AVM_CDF2(   36),   0 },
  { AVM_CDF2(  128),  31 },
};

static const avm_cdf_prob default_stx_cdf[2][5][CDF_SIZE(4)] = {
  {
    { AVM_CDF4( 4486, 15589, 26440),  75 },
    { AVM_CDF4( 2357,  9504, 16641),   5 },
    { AVM_CDF4( 1364,  8034, 14431),   0 },
    { AVM_CDF4( 2472,  8725, 13853),  76 },
    { AVM_CDF4( 7523, 11681, 14783),   1 },
  },
  {
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4( 8192, 16384, 24576),   0 },
    { AVM_CDF4(10490, 15394, 22206),  31 },
    { AVM_CDF4(13967, 16953, 23109),  31 },
    { AVM_CDF4(20191, 21727, 25818),  32 },
  },
};

static const avm_cdf_prob default_cctx_type_cdf[CDF_SIZE(7)] = { AVM_CDF7(14350, 14836, 16054, 17075, 19408, 28530),  62 };

static const avm_cdf_prob default_pb_mv_most_probable_precision_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(20650),  31 },
  { AVM_CDF2(15758),  31 },
  { AVM_CDF2( 4571),  31 },
};

static const avm_cdf_prob default_pb_mv_precision_cdf[2][3][CDF_SIZE(3)] = {
  {
    { AVM_CDF3(10923, 21845),   0 },
    { AVM_CDF3(31340, 32505),  78 },
    { AVM_CDF3(26039, 32175),   0 },
  },
  {
    { AVM_CDF3(10923, 21845),   0 },
    { AVM_CDF3(32197, 32676),  75 },
    { AVM_CDF3(28679, 32626),   0 },
  },
};

static const avm_cdf_prob default_do_split_cdf[2][64][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(28084),   3 },
    { AVM_CDF2(23755),  93 },
    { AVM_CDF2(23634),  93 },
    { AVM_CDF2(19368),   3 },
    { AVM_CDF2(24961),   0 },
    { AVM_CDF2(14941),   1 },
    { AVM_CDF2(16154),   1 },
    { AVM_CDF2( 5905),   0 },
    { AVM_CDF2(21934),   0 },
    { AVM_CDF2(10440),  26 },
    { AVM_CDF2(11984),  31 },
    { AVM_CDF2( 3474),   0 },
    { AVM_CDF2(20492),  16 },
    { AVM_CDF2( 6963),   6 },
    { AVM_CDF2( 8099),  26 },
    { AVM_CDF2( 1529),   0 },
    { AVM_CDF2(24117),  92 },
    { AVM_CDF2( 7871),  10 },
    { AVM_CDF2(23604),   2 },
    { AVM_CDF2( 8429),  30 },
    { AVM_CDF2(27356),   2 },
    { AVM_CDF2(22441),   7 },
    { AVM_CDF2( 8897),  31 },
    { AVM_CDF2( 6811),  61 },
    { AVM_CDF2(17592),  16 },
    { AVM_CDF2( 5648),  32 },
    { AVM_CDF2( 5339),  26 },
    { AVM_CDF2( 1082),  26 },
    { AVM_CDF2(26143),  77 },
    { AVM_CDF2(11379),  85 },
    { AVM_CDF2(20142),  93 },
    { AVM_CDF2( 7401),   8 },
    { AVM_CDF2(26235),  82 },
    { AVM_CDF2(23674),  78 },
    { AVM_CDF2(12441),  77 },
    { AVM_CDF2(10482),  75 },
    { AVM_CDF2(20663),   0 },
    { AVM_CDF2( 4192),  27 },
    { AVM_CDF2( 5274),  33 },
    { AVM_CDF2(  713),   1 },
    { AVM_CDF2(28255),  75 },
    { AVM_CDF2(27370),  75 },
    { AVM_CDF2(23527),   0 },
    { AVM_CDF2(20990),   1 },
    { AVM_CDF2(26727),   0 },
    { AVM_CDF2(21187),   0 },
    { AVM_CDF2(25324),   0 },
    { AVM_CDF2(17838),   0 },
    { AVM_CDF2(26136),   0 },
    { AVM_CDF2(16591),   6 },
    { AVM_CDF2(19838),   1 },
    { AVM_CDF2(10605),  31 },
    { AVM_CDF2(22914),   1 },
    { AVM_CDF2(12609),  31 },
    { AVM_CDF2(11341),   0 },
    { AVM_CDF2( 4556),   0 },
    { AVM_CDF2(24218),   1 },
    { AVM_CDF2(13059),   7 },
    { AVM_CDF2(15378),  32 },
    { AVM_CDF2( 5858),  32 },
    { AVM_CDF2(21644),  32 },
    { AVM_CDF2( 7767),  31 },
    { AVM_CDF2( 8309),   6 },
    { AVM_CDF2( 1687),   0 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(25554),   0 },
    { AVM_CDF2(18892),   0 },
    { AVM_CDF2(18530),   0 },
    { AVM_CDF2(10806),   6 },
    { AVM_CDF2(22504),   1 },
    { AVM_CDF2(12140),  31 },
    { AVM_CDF2(11966),  31 },
    { AVM_CDF2( 4984),  30 },
    { AVM_CDF2(24460),  31 },
    { AVM_CDF2( 8698),  31 },
    { AVM_CDF2( 9655),  31 },
    { AVM_CDF2( 2563),  30 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(26227),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(17669),   1 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(26179),   1 },
    { AVM_CDF2(17889),   1 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(23500),   1 },
    { AVM_CDF2(13115),  31 },
    { AVM_CDF2(15253),  31 },
    { AVM_CDF2( 6458),  55 },
    { AVM_CDF2(22566),  31 },
    { AVM_CDF2(11497),  32 },
    { AVM_CDF2(10045),  31 },
    { AVM_CDF2( 3750),  26 },
  },
};

static const avm_cdf_prob default_do_square_split_cdf[2][8][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(18000),   7 },
    { AVM_CDF2(10521),  37 },
    { AVM_CDF2(11395),  62 },
    { AVM_CDF2( 4419),  32 },
    { AVM_CDF2(12996),  85 },
    { AVM_CDF2( 8185),  55 },
    { AVM_CDF2(10979),  36 },
    { AVM_CDF2( 5010),  32 },
  },
  {
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_rect_type_cdf[2][64][CDF_SIZE(2)] = {
  {
    { AVM_CDF2(14644),   0 },
    { AVM_CDF2(10173),  75 },
    { AVM_CDF2(18529),   0 },
    { AVM_CDF2(16071),  90 },
    { AVM_CDF2(20263),   1 },
    { AVM_CDF2(12813),   1 },
    { AVM_CDF2(26612),   0 },
    { AVM_CDF2(23277),   1 },
    { AVM_CDF2(10594),  76 },
    { AVM_CDF2( 7000),  75 },
    { AVM_CDF2(20002),   1 },
    { AVM_CDF2(12889),   2 },
    { AVM_CDF2(13854),  76 },
    { AVM_CDF2(10750),   1 },
    { AVM_CDF2(18380),   1 },
    { AVM_CDF2(17505),   6 },
    { AVM_CDF2(14430),   7 },
    { AVM_CDF2(11554),   2 },
    { AVM_CDF2(20078),   1 },
    { AVM_CDF2(19097),  76 },
    { AVM_CDF2(15278),   2 },
    { AVM_CDF2(10137),   1 },
    { AVM_CDF2(21921),   7 },
    { AVM_CDF2(14621),   6 },
    { AVM_CDF2(19330),   2 },
    { AVM_CDF2(15921),   1 },
    { AVM_CDF2(26218),   1 },
    { AVM_CDF2(24318),   1 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16066),  78 },
    { AVM_CDF2( 9225),   2 },
    { AVM_CDF2(22849),  31 },
    { AVM_CDF2(14817),  11 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(18543),  75 },
    { AVM_CDF2(13210),  10 },
    { AVM_CDF2(24367),  32 },
    { AVM_CDF2(18417),  25 },
    { AVM_CDF2(24701),   6 },
    { AVM_CDF2(18911),   7 },
    { AVM_CDF2(29590),   1 },
    { AVM_CDF2(27778),   7 },
    { AVM_CDF2( 3400),   1 },
    { AVM_CDF2(  935),  90 },
    { AVM_CDF2(10365),  32 },
    { AVM_CDF2( 1723),   1 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
  {
    { AVM_CDF2(15654),  76 },
    { AVM_CDF2(13500),  75 },
    { AVM_CDF2(19177),  91 },
    { AVM_CDF2(14739),  90 },
    { AVM_CDF2(18769),  75 },
    { AVM_CDF2(13500),   1 },
    { AVM_CDF2(23583),  75 },
    { AVM_CDF2(20927),  75 },
    { AVM_CDF2(15045),   1 },
    { AVM_CDF2(10528),   0 },
    { AVM_CDF2(22474),   6 },
    { AVM_CDF2(14250),   0 },
    { AVM_CDF2(16561),  75 },
    { AVM_CDF2(11427),  76 },
    { AVM_CDF2(21874),   6 },
    { AVM_CDF2(16344),  90 },
    { AVM_CDF2(21566),  31 },
    { AVM_CDF2(13357),   2 },
    { AVM_CDF2(27355),   1 },
    { AVM_CDF2(24117),   6 },
    { AVM_CDF2(10901),  77 },
    { AVM_CDF2( 5780),   0 },
    { AVM_CDF2(19056),  37 },
    { AVM_CDF2( 9141),   1 },
    { AVM_CDF2(20436),   7 },
    { AVM_CDF2(15693),  32 },
    { AVM_CDF2(26536),   6 },
    { AVM_CDF2(23667),  31 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(19241),   7 },
    { AVM_CDF2(13038),  32 },
    { AVM_CDF2(28903),  32 },
    { AVM_CDF2(24802),  32 },
    { AVM_CDF2( 9097),  32 },
    { AVM_CDF2( 2749),   6 },
    { AVM_CDF2(15201),  27 },
    { AVM_CDF2( 4449),   6 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
    { AVM_CDF2(16384),   0 },
  },
};

static const avm_cdf_prob default_do_ext_partition_cdf[2][1][64][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(31091),   3 },
      { AVM_CDF2(29638),   0 },
      { AVM_CDF2(28924),   0 },
      { AVM_CDF2(28653),   0 },
      { AVM_CDF2(30349),  93 },
      { AVM_CDF2(28265),  75 },
      { AVM_CDF2(27287),   0 },
      { AVM_CDF2(27721),   0 },
      { AVM_CDF2(29960),  93 },
      { AVM_CDF2(28345),  90 },
      { AVM_CDF2(27302),  90 },
      { AVM_CDF2(27252),  75 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(30342),   1 },
      { AVM_CDF2(27563),   6 },
      { AVM_CDF2(26168),   6 },
      { AVM_CDF2(23089),  32 },
      { AVM_CDF2(30643),   0 },
      { AVM_CDF2(28683),   1 },
      { AVM_CDF2(28009),   0 },
      { AVM_CDF2(26186),   0 },
      { AVM_CDF2(29222),   1 },
      { AVM_CDF2(25740),   0 },
      { AVM_CDF2(24079),   6 },
      { AVM_CDF2(19806),  76 },
      { AVM_CDF2(29409),  90 },
      { AVM_CDF2(26825),  93 },
      { AVM_CDF2(25919),  93 },
      { AVM_CDF2(24417),  93 },
    },
  },
  {
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(26866),  30 },
      { AVM_CDF2(24499),  25 },
      { AVM_CDF2(24732),  31 },
      { AVM_CDF2(23387),  25 },
      { AVM_CDF2(27477),  30 },
      { AVM_CDF2(25837),  31 },
      { AVM_CDF2(24621),  31 },
      { AVM_CDF2(23604),   5 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(24384),  26 },
      { AVM_CDF2(22113),  32 },
      { AVM_CDF2(21798),   2 },
      { AVM_CDF2(20067),   6 },
      { AVM_CDF2(26220),  31 },
      { AVM_CDF2(22997),  30 },
      { AVM_CDF2(22249),  31 },
      { AVM_CDF2(20091),   0 },
    },
  },
};

static const avm_cdf_prob default_do_uneven_4way_partition_cdf[2][1][64][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(25789),  31 },
      { AVM_CDF2(25290),  32 },
      { AVM_CDF2(24270),  31 },
      { AVM_CDF2(22994),  31 },
      { AVM_CDF2(25801),   1 },
      { AVM_CDF2(25260),   7 },
      { AVM_CDF2(24041),   1 },
      { AVM_CDF2(24281),   1 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(23038),   6 },
      { AVM_CDF2(19972),  40 },
      { AVM_CDF2(19723),   8 },
      { AVM_CDF2(17939),   0 },
      { AVM_CDF2(15574),   6 },
      { AVM_CDF2(13761),  76 },
      { AVM_CDF2(12917),  91 },
      { AVM_CDF2(11328),  75 },
      { AVM_CDF2(17295),  32 },
      { AVM_CDF2(14463),  38 },
      { AVM_CDF2(14724),   6 },
      { AVM_CDF2(11653),   1 },
      { AVM_CDF2(13202),  32 },
      { AVM_CDF2(10929),   7 },
      { AVM_CDF2(10348),   7 },
      { AVM_CDF2( 8276),   1 },
    },
  },
  {
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(24728),  31 },
      { AVM_CDF2(22673),  31 },
      { AVM_CDF2(21033),   6 },
      { AVM_CDF2(20321),   1 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(24631),  67 },
      { AVM_CDF2(21363),  15 },
      { AVM_CDF2(20201),  15 },
      { AVM_CDF2(17529),  82 },
      { AVM_CDF2(21042),   3 },
      { AVM_CDF2(18640),  80 },
      { AVM_CDF2(18183),  91 },
      { AVM_CDF2(15590),  76 },
    },
  },
};

static const avm_cdf_prob default_txfm_4way_partition_type_reduced_cdf[2][2][14][CDF_SIZE(7)] = {
  {
    {
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
    {
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
  },
  {
    {
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
    {
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
  },
};

static const avm_cdf_prob default_txfm_2or3_way_partition_type_cdf[2][2][2][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(25131),  30 },
      { AVM_CDF2(24514),  30 },
    },
    {
      { AVM_CDF2(19534),   6 },
      { AVM_CDF2(18637),   0 },
    },
  },
  {
    {
      { AVM_CDF2(30226),  50 },
      { AVM_CDF2(30703),  33 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
    },
  },
};

static const avm_cdf_prob default_most_probable_stx_set_cdf[CDF_SIZE(7)] = { AVM_CDF7(20712, 26263, 30623, 32732, 32736, 32740),  55 };

static const avm_cdf_prob default_most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(4)] = { AVM_CDF4(15897, 26144, 30010),   5 };

static const avm_cdf_prob default_txfm_do_partition_cdf[2][2][9][CDF_SIZE(2)] = {
  {
    {
      { AVM_CDF2(26330),  30 },
      { AVM_CDF2(29620),   0 },
      { AVM_CDF2(20420),  30 },
      { AVM_CDF2(21694),  31 },
      { AVM_CDF2(13317),  31 },
      { AVM_CDF2(15391),  31 },
      { AVM_CDF2(15952),  31 },
      { AVM_CDF2(14736),  31 },
      { AVM_CDF2(13810),   5 },
    },
    {
      { AVM_CDF2(31646),  55 },
      { AVM_CDF2(32393),  25 },
      { AVM_CDF2(30802),  26 },
      { AVM_CDF2(30485),  31 },
      { AVM_CDF2(20759),  32 },
      { AVM_CDF2(22159),   1 },
      { AVM_CDF2(26832),  31 },
      { AVM_CDF2(27351),   1 },
      { AVM_CDF2(24696),   1 },
    },
  },
  {
    {
      { AVM_CDF2(29308),   0 },
      { AVM_CDF2(32550),  93 },
      { AVM_CDF2(27963),   1 },
      { AVM_CDF2(27618),  31 },
      { AVM_CDF2(22367),   7 },
      { AVM_CDF2(23478),  35 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(27990),   0 },
    },
    {
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
      { AVM_CDF2(16384),   0 },
    },
  },
};

static const avm_cdf_prob default_txfm_4way_partition_type_cdf[2][2][14][CDF_SIZE(7)] = {
  {
    {
      { AVM_CDF7(32744, 32748, 32752, 32756, 32760, 32764),  60 },
      { AVM_CDF7( 3006, 22888, 27132, 29972, 29976, 32724),  31 },
      { AVM_CDF7( 3673,  8849, 27652, 27656, 29944, 29948),  31 },
      { AVM_CDF7( 5219, 19419, 28052, 28836, 29528, 31296),  31 },
      { AVM_CDF7( 3055, 19649, 22157, 27038, 27466, 31646),  31 },
      { AVM_CDF7( 6044, 11255, 26170, 26493, 28585, 29584),  31 },
      { AVM_CDF7( 5896, 20361, 25685, 27552, 28695, 31097),  31 },
      { AVM_CDF7( 2355, 17601, 21703, 26050, 27881, 31397),  32 },
      { AVM_CDF7( 4701, 13502, 24958, 26413, 28166, 30129),  32 },
      { AVM_CDF7( 3319, 16449, 21641, 27154, 29260, 31512),  32 },
      { AVM_CDF7( 2526, 17088, 19643, 29378, 29382, 32724),  31 },
      { AVM_CDF7( 2298,  4406, 23886, 23890, 30148, 30152),  31 },
      { AVM_CDF7( 1553, 16160, 18679, 27983, 29592, 32139),  32 },
      { AVM_CDF7( 2316,  4714, 22731, 23797, 29514, 30077),  37 },
    },
    {
      { AVM_CDF7(10790, 22602, 32736, 32740, 32744, 32748),  32 },
      { AVM_CDF7(14384, 19116, 26545, 28687, 28691, 32724),  31 },
      { AVM_CDF7(13910, 21690, 26343, 26347, 28432, 28436),  31 },
      { AVM_CDF7(15891, 21712, 28890, 29571, 30307, 31363),  32 },
      { AVM_CDF7(15946, 19447, 20270, 23423, 23858, 31148),  32 },
      { AVM_CDF7(16053, 16957, 20312, 20924, 24770, 25959),  32 },
      { AVM_CDF7(18479, 23053, 26582, 26966, 27567, 29836),  62 },
      { AVM_CDF7( 9312, 11882, 14354, 17792, 19827, 29477),  62 },
      { AVM_CDF7( 8490, 10668, 13295, 15353, 19952, 22575),  62 },
      { AVM_CDF7( 6404, 12066, 16173, 20041, 24512, 28421),  62 },
      { AVM_CDF7(10019, 14455, 17658, 27012, 27016, 32724),  37 },
      { AVM_CDF7( 9479, 14904, 19374, 19378, 28027, 28031),  37 },
      { AVM_CDF7( 3717,  7198,  8103, 20546, 23558, 31447),  62 },
      { AVM_CDF7( 4058,  5429,  8987, 13978, 25126, 26655),  62 },
    },
  },
  {
    {
      { AVM_CDF7(32434, 32490, 32545, 32601, 32657, 32712),  50 },
      { AVM_CDF7( 1491, 14241, 29930, 32517, 32524, 32724),  57 },
      { AVM_CDF7( 1719, 16525, 31000, 31004, 32575, 32579),  30 },
      { AVM_CDF7( 1645, 16749, 29324, 30425, 32016, 32485),  62 },
      { AVM_CDF7( 2908, 15802, 24689, 28470, 32122, 32542),  25 },
      { AVM_CDF7( 3470, 17931, 25841, 29589, 31907, 32465),  43 },
      { AVM_CDF7( 5638, 19594, 28693, 29977, 30703, 32154),  50 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 2734, 12129, 30342, 31805, 31844, 32724),  50 },
      { AVM_CDF7( 3849, 21783, 31043, 31056, 32181, 32193),  65 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
    {
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
      { AVM_CDF7( 4681,  9362, 14043, 18725, 23406, 28087),   0 },
    },
  },
};

static const avm_cdf_prob default_filter_dir_cdf[4][CDF_SIZE(3)] = {
  { AVM_CDF3(10923, 21845),   0 },
  { AVM_CDF3( 8795, 15105),   6 },
  { AVM_CDF3(10433, 15974),  32 },
  { AVM_CDF3(17085, 21689),  32 },
};

static const avm_cdf_prob default_cfl_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(20441),  30 },
  { AVM_CDF2(11610),   6 },
  { AVM_CDF2( 4643),   0 },
};

static const avm_cdf_prob default_region_type_cdf[4][CDF_SIZE(2)] = {
  { AVM_CDF2( 2635),  25 },
  { AVM_CDF2(  883),  25 },
  { AVM_CDF2(  503),  50 },
  { AVM_CDF2(  279),  55 },
};

static const avm_cdf_prob default_morph_pred_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(31715),  55 },
  { AVM_CDF2(19667),   1 },
  { AVM_CDF2(10555),  91 },
};

static const avm_cdf_prob default_seg_tree_cdf[CDF_SIZE(8)] = { AVM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 };

static const avm_cdf_prob default_seg_tree_cdf1[CDF_SIZE(8)] = { AVM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 };

static const avm_cdf_prob default_segment_pred_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
};

static const avm_cdf_prob default_spatial_pred_seg_tree_cdf[3][CDF_SIZE(8)] = {
  { AVM_CDF8( 5622,  7893, 16093, 18233, 27809, 28373, 32533),   0 },
  { AVM_CDF8(14274, 18230, 22557, 24935, 29980, 30851, 32344),   0 },
  { AVM_CDF8(27527, 28487, 28723, 28890, 32397, 32647, 32679),   0 },
};

static const avm_cdf_prob default_spatial_pred_seg_tree_cdf1[3][CDF_SIZE(8)] = {
  { AVM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
  { AVM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
  { AVM_CDF8( 4096,  8192, 12288, 16384, 20480, 24576, 28672),   0 },
};

static const avm_cdf_prob default_seg_id_ext_flag_cdf[3][CDF_SIZE(2)] = {
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
  { AVM_CDF2(16384),   0 },
};

/* clang-format on */
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_ENTROPY_INITS_MODES_H_
